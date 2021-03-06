//
// Created by 朱熙 on 2018/6/4.
//

#ifndef PARSERSERVER_VM_H
#define PARSERSERVER_VM_H

#include "../../../include/global.h"
#include "../lexer/Lexer.h"
#include "../../util/StringUtil.hpp"
#include "../xmlReader/XmlReader.h"
#include <boost/interprocess/ipc/message_queue.hpp>
#include "./robot_state.h"

using namespace boost::interprocess;

#define NUMERIC_LIMITS 7

struct COMMAND {
    bool isPowerOn;
    bool isStop;
    bool isPause;
    int dos;
};

enum number_type {
    SHORT = 0,
    INT,
    DOUBLE
};

union numberUnion {
    short num_short;
    int num_int;
    double num_double;
};

struct NUMBER_STRUCT {
    number_type numberType;
    numberUnion number;
};
typedef map<string, NUMBER_STRUCT> GLOBAL_VARIABLE;      //变量

//全局变量结构体
struct GLOBAL_PARAMS {
    bool dis[16];
    bool dos[16];
    GLOBAL_VARIABLE global_variable;
};

//比较运算符
struct Arith {
    Tag ope;     //运算符 < > == >= <= !=
    string strVariable;  //变量名
    NUMBER_STRUCT numberValue;
    GLOBAL_VARIABLE* global_variable;

    Arith(string& str, GLOBAL_VARIABLE* pGlobal) {

        vector<string> strArithItems = StringUtil::splitWithFlag(str);

        global_variable = pGlobal;

        if (strArithItems[3] == "<") ope = GT;
        else if (strArithItems[3] == ">") ope = LT;
        else if (strArithItems[3] == "==") ope = EQ;
        else if (strArithItems[3] == ">=") ope = GE;
        else if (strArithItems[3] == "<=") ope = LE;
        else if (strArithItems[3] == "!=") ope = NE;

        strVariable = strArithItems[2];

        if (strArithItems[0] == "int") {numberValue.numberType = INT; numberValue.number.num_int = atoi(strArithItems[4].c_str());}
        else if (strArithItems[0] == "short") {numberValue.numberType = SHORT; numberValue.number.num_short = (short)atoi(strArithItems[4].c_str());}
        else {numberValue.numberType = DOUBLE; numberValue.number.num_double = atof(strArithItems[4].c_str());}

    }
    ~Arith(){ global_variable = nullptr; }

    virtual bool eval() {
        NUMBER_STRUCT g_value = (*global_variable)[strVariable];
        auto a = g_value.numberType == SHORT ? g_value.number.num_short: g_value.numberType == INT ? g_value.number.num_int: g_value.number.num_double;
        auto b = numberValue.numberType == SHORT ? numberValue.number.num_short: numberValue.numberType == INT ? numberValue.number.num_int: numberValue.number.num_double;

        bool result = false;
        switch (ope) {
            case EQ : result =  a==b ? true : false; break;
            case NE : result =  a!=b ? true : false; break;
            case GT : result =  a>b ? true : false; break;
            case LT : result =  a<b ? true : false; break;
            case GE : result =  a>=b ? true : false; break;
            case LE : result =  a<=b ? true : false; break;
            default : result = false; printf("unsupported int option %d\n", ope); break;
        }
        return result;
    }

};

//执行节点
struct Node{
    int id;

    virtual void eval(){}
    virtual string toString(){return "";}
};

/**
 * vm（虚拟机）类
 * 用于根据生成的语法树进行执行
 */
class Vm {
public:

    Vm();
    ~Vm();

    /**
     * 复位vm
     */
    void reset();
    /**
     * 启动vm
     */
    void start();
    /**
     * 停止vm
     */
    void stop();
    /**
     * 发送数据到boost：mq通道
     * @param strData
     */
    void sendToMq(string strData);
    /**
     * 设置vm中dos
     * @param dos
     */
    void setDos(int dos);
    /**
     * 设置vm中power状态
     * @param bState
     */
    void setPowerState(bool bState);
    /**
     * 设置play状态
     * @param bState
     */
    void setPlayState(bool bState);
    /**
     * 设置暂停状态
     * @param bState
     */
    void setPauseState(bool bState);

    /**
     * 根据语法树指定命令
     * @param nodeList 语法树
     */
    void evalNodeList(list<Node *> &nodeList);

private:
    void sendCommand();

public:
    GLOBAL_PARAMS m_globalParams;       //dis，dos，全局变量
    bool m_bStop;                       //脚本执行结束标志
    bool m_bWaitRes;                    //是否需要等待返回值

    list<Node*> m_nodeList;             //生成的语法树
    XmlReader m_xmlReader;              //xml解析器

    RobotState* m_robotState;       //robot状态
    int m_curScriptId;              //当前脚本id
    string m_curResult;             //当前返回值

private:
    src::severity_channel_logger<severity_level, std::string> scl;


    condition_variable m_cv;
    shared_memory_object m_shm;     //下发当前COMMAND共享内存
    mapped_region m_region;         //共享内存映射，用于读写共享内存

    message_queue *m_pMqSend;       //下发脚本mq
    message_queue *m_pMqRecv;       //接受脚本执行结果mq
    thread *m_pProcessThread;       //vm启动线程
    COMMAND m_command;              //poweron，pause，dos等命令
};

//movej语法节点
struct Movej_Node : Node {
    double axis[7];
    double a;
    double v;
    int t;
    double r;

    Vm * pVm;
    MECHANICALARM* pMechanicalarm;
    bool *bStop;

    string toString() override {
        stringstream ss;
        ss << "movej(" << "[" ;
        for (int i=0; i<7; i++) {
            ss << axis[i];
            if (i<6) ss<<',';
        }
        ss<<"],"<<a<<","<<v <<"," << t <<"," << r << ")";
        return ss.str();
    }
    void eval() override {
        if (*bStop) return;

        bool res = true;
        for (int i = 0; i < 7; ++i) {
            if (axis[i] > pMechanicalarm->movej.paxis[i].max ||
                axis[i] < pMechanicalarm->movej.paxis[i].min) {
                res = false;
                break;
            }
        }

        if (a > pMechanicalarm->movej.a.max || a < pMechanicalarm->movej.a.min ) res = false;
        if (v > pMechanicalarm->movej.v.max || a < pMechanicalarm->movej.v.min ) res = false;
        if (t > pMechanicalarm->movej.t.max || a < pMechanicalarm->movej.t.min ) res = false;
        if (r > pMechanicalarm->movej.r.max || a < pMechanicalarm->movej.r.min ) res = false;

        if (res) {
            string str = this->toString();
            pVm->sendToMq(str);
        }
        else
            *bStop = true;
    }
};

//movel语法节点
struct Movel_Node : Node {
    double pose[7];
    double a;
    double v;
    int t;
    double r;

    Vm * pVm;
    MECHANICALARM* pMechanicalarm;
    bool *bStop;

    string toString() override {
        stringstream ss;
        ss << "movel(" << "[" ;
        for (int i=0; i<7; i++) {
            ss << pose[i];
            if (i<6) ss<<',';
        }
        ss<<"],"<<a<<","<<v <<"," << t <<"," << r << ")";
        return ss.str();
    }
    void eval() override {
        if (*bStop) return;

        bool res = true;
        for (int i = 0; i < 7; ++i) {
            if (pose[i] > pMechanicalarm->movel.pose[i].max ||
                    pose[i] < pMechanicalarm->movel.pose[i].min) {
                res = false;
                break;
            }
        }

        if (a > pMechanicalarm->movel.a.max || a < pMechanicalarm->movel.a.min ) res = false;
        if (v > pMechanicalarm->movel.v.max || a < pMechanicalarm->movel.v.min ) res = false;
        if (t > pMechanicalarm->movel.t.max || a < pMechanicalarm->movel.t.min ) res = false;
        if (r > pMechanicalarm->movel.r.max || a < pMechanicalarm->movel.r.min ) res = false;

        if (res) {
            string str = this->toString();
            pVm->sendToMq(str);
        }
        else
            *bStop = true;
    }
};

struct Speedj_Node : Node {
    double axis[7];
    double a;
    int t_min;

    Vm * pVm;
    MECHANICALARM* pMechanicalarm;
    bool *bStop;

    string toString() override {
        stringstream ss;
        ss.precision(NUMERIC_LIMITS);
        ss << "speedj(" << "[" ;
        for (int i=0; i<7; i++) {
            ss << axis[i];
            if (i<6) ss<<',';
        }
        ss<<"],"<<a<<","<<t_min<< ")";
        return ss.str();
    }

    void eval() override {
        if (*bStop) return;

        bool res = true;
        for (int i = 0; i < 7; ++i) {
            if (axis[i] > pMechanicalarm->speedj.pd_axis[i].max ||
                    axis[i] < pMechanicalarm->speedj.pd_axis[i].min) {
                res = false;
                break;
            }
        }

        if (a > pMechanicalarm->speedj.a.max || a < pMechanicalarm->speedj.a.min ) res = false;
        if (t_min > pMechanicalarm->speedj.t_min.max || a < pMechanicalarm->speedj.t_min.min ) res = false;


        if (res) {
            string str = this->toString();
            pVm->sendToMq(str);
        }
        else
            *bStop = true;
    }
};

struct Speedl_Node : Node {
    double xd[6];
    double a;
    int t_min;

    Vm * pVm;
    MECHANICALARM* pMechanicalarm;
    bool *bStop;

    string toString() override {
        stringstream ss;
        ss << "speedl(" << "[" ;
        for (int i=0; i<6; i++) {
            ss << xd[i];
            if (i<5) ss<<',';
        }
        ss<<"],"<<a<<","<<t_min << ")";
        return ss.str();
    }

    void eval() override {
        if (*bStop) return;

        bool res = true;
        for (int i = 0; i < 6; ++i) {
            if (xd[i] > pMechanicalarm->speedl.xd[i].max ||
                xd[i] < pMechanicalarm->speedl.xd[i].min) {
                res = false;
                break;
            }
        }

        if (a > pMechanicalarm->speedl.a.max || a < pMechanicalarm->speedl.a.min ) res = false;
        if (t_min > pMechanicalarm->speedl.t_min.max || a < pMechanicalarm->speedl.t_min.min ) res = false;


        if (res) {
            string str = this->toString();
            pVm->sendToMq(str);
        }
        else
            *bStop = true;
    }

};

struct Stopj_Node : Node {
    double a;

    Vm * pVm;
    MECHANICALARM* pMechanicalarm;
    bool *bStop;

    string toString() override {
        stringstream ss;
        ss << "stopj(" << a << ")";
        return ss.str();
    }

    void eval() override {
        if (*bStop) return;

        bool res = true;
        if (a > pMechanicalarm->stopj.a.max || a < pMechanicalarm->stopj.a.min ) res = false;

        if (res) {
            string str = this->toString();
            pVm->sendToMq(str);
        }
        else
            *bStop = true;
    }
};

struct Stopl_Node : Node {
    double a;

    Vm * pVm;
    MECHANICALARM* pMechanicalarm;
    bool *bStop;

    string toString() override {
        stringstream ss;
        ss << "stopl(" << a << ")";
        return ss.str();
    }
    void eval() override {
        if (*bStop) return;

        bool res = true;
        if (a > pMechanicalarm->stopl.a.max || a < pMechanicalarm->stopl.a.min ) res = false;

        if (res){
            string str = this->toString();
            pVm->sendToMq(str);
        }
        else
            *bStop = true;
    }
};

struct Teach_Mode_Node : Node {
    int x;
    int y;
    int z;
    int rx;
    int ry;
    int rz;


    Vm * pVm;
    MECHANICALARM* pMechanicalarm;
    bool *bStop;

    string toString() override {
        stringstream ss;
        ss << "teach_mode(" << x << "," << y << ","<< z << ","<< rx << ","<< ry << ","<< rz << ")";
        return ss.str();
    }

    void eval() override {
        if (*bStop) return;

        bool res = true;
        if (x > pMechanicalarm->teach_mode.x.max || x < pMechanicalarm->teach_mode.x.min ) res = false;
        if (y > pMechanicalarm->teach_mode.y.max || y < pMechanicalarm->teach_mode.y.min ) res = false;
        if (z > pMechanicalarm->teach_mode.z.max || z < pMechanicalarm->teach_mode.z.min ) res = false;
        if (rx > pMechanicalarm->teach_mode.rx.max || rx < pMechanicalarm->teach_mode.rx.min ) res = false;
        if (ry > pMechanicalarm->teach_mode.ry.max || ry < pMechanicalarm->teach_mode.ry.min ) res = false;
        if (rz > pMechanicalarm->teach_mode.rz.max || rz < pMechanicalarm->teach_mode.rz.min ) res = false;


        if (res){
            string str = this->toString();
            pVm->sendToMq(str);
        }
        else
            *bStop = true;
    }
};

struct End_Teach_Mode_Node : Node {

    Vm * pVm;
    MECHANICALARM* pMechanicalarm;
    bool *bStop;

    string toString() override {
        stringstream ss;
        ss << "end_teach_mode()";
        return ss.str();
    }

    void eval() override {
        if (*bStop) return;

        string str = this->toString();
        pVm->sendToMq(str);
    }
};

struct MoveCamera_Node : Node {
    Vm * pVm;
    bool *bStop;

    string toString() override {
        stringstream ss;
        ss << "MoveCamera()";
        return ss.str();
    }
    void eval() override {
        if (*bStop) return;

        string str = this->toString();
        pVm->sendToMq(str);
    }
};

//if else 节点
struct Ifelse_Node : Node {

    bool ifIoRadio;
    map<int, int> disMap;
    map<int, int> dosMap;
    bool ifParamRadio;
    vector<Arith> ariths;

    Vm * pVm;
    list<Node*> ifNodeList;
    list<Node*> elseNodeList;

    GLOBAL_PARAMS* pGlobalParams;
    bool *bStop;

    Ifelse_Node(){};
    ~Ifelse_Node(){
        for (auto node : ifNodeList) {
            delete node;
            node = nullptr;
        }

        for (auto node : elseNodeList) {
            delete node;
            node = nullptr;
        }
    }

    void eval() override {
        if (*bStop) return;

         bool bIo = false;

        if (ifIoRadio) {    //标志位位1，即有任意一项符合即为true
            bool bDis = false, bDos = false;

            for (auto dis : disMap) {
                bDis |= (pGlobalParams->dis[dis.first] == dis.second);
                if (bDis) break;
            }
                
            for (auto dos : dosMap) {
                bDos |= (pGlobalParams->dos[dos.first] == dos.second);
                if (bDos) break;
            }
            bIo = bDis|bDos;

        } else {            //标志位位0，即dis及dos中项全对应时为true
            bool bDis = true, bDos = true;

            for (auto dis : disMap)
                bDis &= (pGlobalParams->dis[dis.first] == dis.second);

            for (auto dos : dosMap)
                bDos &= (pGlobalParams->dos[dos.first] == dos.second);

            bIo = bDis&bDos;
        }

        bool bParam = true;
        for (auto arith : ariths)
            bParam &= arith.eval();

        if (bIo && bParam) {
            pVm->evalNodeList(ifNodeList);
//            for(auto node : ifNodeList)
//                node->eval();
        }
        else {
            pVm->evalNodeList(elseNodeList);
//            for (auto node : elseNodeList) {
//                node->eval();
//            }
        }
    }

};

//wait 节点
struct Wait_Node : Node {
    bool ifIoRadio;
    map<int, int> disMap;
    map<int, int> dosMap;
    bool ifParamRadio;
    vector<Arith> ariths;

    int sleepMs;

    GLOBAL_PARAMS* pGlobalParams;
    bool *bStop;

    void eval() override {
        if (*bStop) return;

        bool bIo = false;

        if (ifIoRadio) {    //标志位位1，即有任意一项符合即为true
            bool bDis = false, bDos = false;

            for (auto dis : disMap) {
                bDis |= (pGlobalParams->dis[dis.first] == dis.second);
                if (bDis) break;
            }
                
            for (auto dos : dosMap) {
                bDos |= (pGlobalParams->dos[dos.first] == dos.second);
                if (bDos) break;
            }
            bIo = bDis|bDos;

        } else {            //标志位位0，即dis及dos中项全对应时为true
            bool bDis = true, bDos = true;

            for (auto dis : disMap)
                bDis &= (pGlobalParams->dis[dis.first] == dis.second);

            for (auto dos : dosMap)
                bDos &= (pGlobalParams->dos[dos.first] == dos.second);

            bIo = bDis&bDos;
        }

        bool bParam = true;
        for (auto arith : ariths)
            bParam &= arith.eval();

        if (bIo && bParam) {
            usleep(sleepMs*1000);
        }

    }
};


struct While_Node : Node {
    list<Node*> nodeList;
    Vm * pVm;
    bool *bStop;

    While_Node(){};
    ~While_Node() {
        for (auto node : nodeList) {
            delete node;
            node = nullptr;
        }
    }

    void eval() override {
        while (!*bStop) {
            pVm->evalNodeList(nodeList);
        }
    }
};

struct Stop_Node : Node {
    bool *bStop;

    void eval() override {
        *bStop = true;
    }
};

struct Set_Node : Node {
    vector<int> dos;
    map<string, NUMBER_STRUCT> params;

    Vm * pVm;
    GLOBAL_PARAMS* pGlobalParams;
    bool *bStop;

    void eval() override {
        if (*bStop) return;

        for (int i=0; i< 16; i++) {
            pGlobalParams->dos[i] = 0;
        }

        for (auto io : dos) {
            pGlobalParams->dos[io] = 1;
        }

        for (auto param : params) {
            pGlobalParams->global_variable[param.first] = param.second;
        }

        pVm->setDos(IntegerUtil::Binary2Integer(pGlobalParams->dos, 16));
    }

};

struct Set_Digital_Out_Node : Node {
    
    int setDigitalOutIndex;
    int value;

    Vm * pVm;
    GLOBAL_PARAMS* pGlobalParams;
    bool *bStop;

    void eval() override {
        if (*bStop) return;

        pGlobalParams->dos[setDigitalOutIndex] = value;

        pVm->setDos(IntegerUtil::Binary2Integer(pGlobalParams->dos, 16));
    }

};




#endif //PARSERSERVER_VM_H
