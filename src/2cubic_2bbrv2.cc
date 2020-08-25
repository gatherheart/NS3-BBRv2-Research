/** Network topology
 *
 *    100Mb/s, 1ms                            100Mb/s, 1ms
 * n0----L0--------|                    |------L5-------n6
 *                 |   50Mbps/s, 18ms   |
 *                 n4--------L4--------n5
 *    100Mb/s, 1ms |                    |    100Mb/s, 1ms
 * n1----L1--------|                    |------L6-------n7
 *		           |        			|
 *	        	   |			        |
 * n2----L2--------|			        |------L7-------n8
 * 		           |        			|
 *                 |		        	|
 * n3----L3--------|			        |------L8--------n9
 * 
 * 2 CUBIC, 2 BBR, 50M, 40ms, buffer = 1xBDP
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/traffic-control-module.h"
#include "ns3/dqc-module.h"
#include "ns3/log.h"
#include<stdio.h>
#include<iostream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>
#include <memory>
#include <chrono>
using namespace ns3;
using namespace dqc;
using namespace std;
NS_LOG_COMPONENT_DEFINE ("bbr-rtt");

uint32_t checkTimes;
double avgQueueSize;

// The times
double global_start_time;
double global_stop_time;
double sink_start_time;
double sink_stop_time;
double client_start_time;
double client_stop_time;

NodeContainer n0n4;
NodeContainer n1n4;
NodeContainer n2n4;
NodeContainer n3n4;
NodeContainer n4n5;
NodeContainer n5n6;
NodeContainer n5n7;
NodeContainer n5n8;
NodeContainer n5n9;

Ipv4InterfaceContainer i0i4;
Ipv4InterfaceContainer i1i4;
Ipv4InterfaceContainer i2i4;
Ipv4InterfaceContainer i3i4;
Ipv4InterfaceContainer i4i5;
Ipv4InterfaceContainer i5i6;
Ipv4InterfaceContainer i5i7;
Ipv4InterfaceContainer i5i8;
Ipv4InterfaceContainer i5i9;

typedef struct
{
uint64_t bps;
uint32_t msDelay;
uint32_t msQdelay;	
}link_config_t;
//unrelated topology
/*
   L3      L1      L4
configuration same as the above dumbbell topology
n0--L0--n2--L1--n3--L2--n4
n1--L3--n2--L1--n3--L4--n5
*/
link_config_t p4p[]={
[0]={100*1000000,1,20},
[1]={100*1000000,1,20},
[2]={100*1000000,1,20},
[3]={100*1000000,1,20},
[4]={50*1000000,18,20},
[5]={100*1000000,1,20},
[6]={100*1000000,1,20},
[7]={100*1000000,1,20},
[8]={100*1000000,1,20},
};
const uint32_t TOPO_DEFAULT_BW     = 50 * 1000 * 1000;    // in bps: 10Mbps
const uint32_t TOPO_DEFAULT_PDELAY =      18;    // in ms:   18ms
const uint32_t TOPO_DEFAULT_QDELAY =     20;    // in ms:  20ms
static void InstallDqc( dqc::CongestionControlType cc_type,
                        Ptr<Node> sender,Ptr<Node> receiver,
                        uint16_t send_port,uint16_t recv_port,
                        float startTime,float stopTime,
                        DqcTrace *trace, DqcTraceState *stat,
                        uint32_t max_bps=0,uint32_t cid=0,bool ecn=false,uint32_t emucons=1)
{
    Ptr<DqcSender> sendApp = CreateObject<DqcSender> (cc_type,ecn);
    Ptr<DqcReceiver> recvApp = CreateObject<DqcReceiver>();
    sender->AddApplication (sendApp);
    receiver->AddApplication (recvApp);
    sendApp->SetNumEmulatedConnections(emucons);
    Ptr<Ipv4> ipv4 = receiver->GetObject<Ipv4> ();
    Ipv4Address receiverIp = ipv4->GetAddress (1, 0).GetLocal ();
    recvApp->Bind(recv_port);
    sendApp->Bind(send_port);
    sendApp->ConfigurePeer(receiverIp,recv_port);
    sendApp->SetStartTime (Seconds (startTime));
    sendApp->SetStopTime (Seconds (stopTime));
    recvApp->SetStartTime (Seconds (startTime));
    recvApp->SetStopTime (Seconds (stopTime));
    if(max_bps>0){
        sendApp->SetMaxBandwidth(max_bps);
    }
    if(cid){
       sendApp->SetSenderId(cid);
        sendApp->SetCongestionId(cid);
    }
    if(trace){
        sendApp->SetBwTraceFuc(MakeCallback(&DqcTrace::OnBw,trace));
	sendApp->SetTraceOwdAtSender(MakeCallback(&DqcTrace::OnRtt,trace));

        recvApp->SetOwdTraceFuc(MakeCallback(&DqcTrace::OnOwd,trace));
        recvApp->SetGoodputTraceFuc(MakeCallback(&DqcTrace::OnGoodput,trace));
        recvApp->SetStatsTraceFuc(MakeCallback(&DqcTrace::OnStats,trace));
        trace->SetStatsTraceFuc(MakeCallback(&DqcTraceState::OnStats,stat));
    }
}
void ns3_rtt(int ins,std::string algo,DqcTraceState *stat,int sim_time=60,int loss_integer=0){
    std::string instance=std::to_string(ins);
    uint64_t linkBw   = TOPO_DEFAULT_BW;
    uint32_t msDelay  = TOPO_DEFAULT_PDELAY;
    uint16_t sendPort=1000;
    uint16_t recvPort=5000;

    double sim_dur=sim_time;
    int start_time=0;
    int end_time=sim_time;
    float appStart=start_time;
    float appStop=end_time;
    p4p[1].bps=linkBw;
    p4p[1].msDelay=msDelay;
    uint32_t owd1=p4p[0].msDelay+p4p[1].msDelay+p4p[2].msDelay;
    uint32_t owd2=p4p[3].msDelay+p4p[1].msDelay+p4p[4].msDelay;
    uint32_t owd=std::max(owd1,owd2);
    uint32_t msQdelay=owd*2;
    for(size_t i=0;i<sizeof(p4p)/sizeof(p4p[0]);i++){
        p4p[i].msQdelay=msQdelay;
    }
    NodeContainer c;
    c.Create (10);

    n0n4 = NodeContainer (c.Get (0), c.Get (4));
    n1n4 = NodeContainer (c.Get (1), c.Get (4));
    n2n4 = NodeContainer (c.Get (2), c.Get (4));
    n3n4 = NodeContainer (c.Get (3), c.Get (4));
    n4n5 = NodeContainer (c.Get (4), c.Get (5));
    n5n6 = NodeContainer (c.Get (5), c.Get (6));
    n5n7 = NodeContainer (c.Get (5), c.Get (7));
    n5n8 = NodeContainer (c.Get (5), c.Get (8));
    n5n9 = NodeContainer (c.Get (5), c.Get (9));

    link_config_t *config=p4p;
    uint32_t bufSize=0;	
    
    InternetStackHelper internet;
    internet.Install (c);
    
    NS_LOG_INFO ("Create channels");
    PointToPointHelper p2p;
    TrafficControlHelper tch;

    //L0
    bufSize =config[0].bps * config[0].msQdelay / 8000;
    p2p.SetQueue ("ns3::DropTailQueue",
                "Mode", StringValue ("QUEUE_MODE_BYTES"),
                "MaxBytes", UintegerValue (bufSize));
    p2p.SetDeviceAttribute ("DataRate", DataRateValue(DataRate (config[0].bps)));
    p2p.SetChannelAttribute ("Delay", TimeValue (MilliSeconds (config[0].msDelay)));
    NetDeviceContainer devn0n4 = p2p.Install (n0n4);
    NetDeviceContainer devn1n4 = p2p.Install (n1n4);
    NetDeviceContainer devn2n4 = p2p.Install (n2n4);
    NetDeviceContainer devn3n4 = p2p.Install (n3n4);
    NetDeviceContainer devn5n6 = p2p.Install (n5n6);
    NetDeviceContainer devn5n7 = p2p.Install (n5n7);
    NetDeviceContainer devn5n8 = p2p.Install (n5n8);
    NetDeviceContainer devn5n9 = p2p.Install (n5n9);
    
    //L4
    bufSize =config[4].bps * config[4].msQdelay / 8000;
    p2p.SetQueue ("ns3::DropTailQueue",
                "Mode", StringValue ("QUEUE_MODE_BYTES"),
                "MaxBytes", UintegerValue (bufSize));
    p2p.SetDeviceAttribute ("DataRate", DataRateValue(DataRate (config[4].bps)));
    p2p.SetChannelAttribute ("Delay", TimeValue (MilliSeconds (config[4].msDelay)));
    NetDeviceContainer devn4n5 = p2p.Install (n4n5);

    Ipv4AddressHelper ipv4;

    ipv4.SetBase ("10.1.1.0", "255.255.255.0");
    i0i4 = ipv4.Assign (devn0n4);
    tch.Uninstall (devn0n4);
    ipv4.SetBase ("10.1.2.0", "255.255.255.0");
    i1i4 = ipv4.Assign (devn1n4);
    tch.Uninstall (devn1n4);
    ipv4.SetBase ("10.1.3.0", "255.255.255.0");
    i2i4 = ipv4.Assign (devn2n4);
    tch.Uninstall (devn2n4);
    ipv4.SetBase ("10.1.4.0", "255.255.255.0");
    i3i4 = ipv4.Assign (devn3n4);
    tch.Uninstall (devn3n4);
    
    ipv4.SetBase ("10.1.5.0", "255.255.255.0");
    i4i5 = ipv4.Assign (devn4n5);
    tch.Uninstall (devn4n5);
  
    ipv4.SetBase ("10.1.6.0", "255.255.255.0");
    i5i6 = ipv4.Assign (devn5n6);
    tch.Uninstall (devn5n6);
    ipv4.SetBase ("10.1.7.0", "255.255.255.0");
    i5i7 = ipv4.Assign (devn5n7);
    tch.Uninstall (devn5n7);
    ipv4.SetBase ("10.1.8.0", "255.255.255.0");
    i5i8 = ipv4.Assign (devn5n8);
    tch.Uninstall (devn5n8);
    ipv4.SetBase ("10.1.9.0", "255.255.255.0");
    i5i9 = ipv4.Assign (devn5n9);
    tch.Uninstall (devn5n9);

    // Set up the routing
    Ipv4GlobalRoutingHelper::PopulateRoutingTables ();
    dqc::CongestionControlType cc=kCubicBytes;
    dqc::CongestionControlType cc2=kBBRv2;

    CongestionControlManager cong_ops_manager;
    RegisterCCManager(&cong_ops_manager);
    
    uint32_t max_bps=0;
    int test_pair=1;
    uint32_t sender_id=1;

    std::vector<std::unique_ptr<DqcTrace>> traces;
    std::string log;
    std::string delimiter="_";
    std::string prefix=instance+delimiter+algo+delimiter;
    log=prefix+std::to_string(test_pair);
    std::unique_ptr<DqcTrace> trace;

    trace.reset(new DqcTrace(test_pair));
    stat->ReisterAvgDelayId(test_pair);
    stat->RegisterCongestionType(test_pair);
    trace->Log(log,DqcTraceEnable::E_DQC_GOODPUT|DqcTraceEnable::E_DQC_RTT|DqcTraceEnable::E_DQC_BW|DqcTraceEnable::E_DQC_OWD
|DqcTraceEnable::E_DQC_STAT);  
    InstallDqc(cc,c.Get(0),c.Get(6),sendPort,recvPort,0,appStop,trace.get(),stat,max_bps,sender_id);
    sender_id++;
    test_pair++;
    sendPort++;
    recvPort++;
    traces.push_back(std::move(trace));

    trace.reset(new DqcTrace(test_pair));
    stat->ReisterAvgDelayId(test_pair);
    log=prefix+std::to_string(test_pair);
    trace->Log(log,DqcTraceEnable::E_DQC_GOODPUT|DqcTraceEnable::E_DQC_RTT|DqcTraceEnable::E_DQC_BW|DqcTraceEnable::E_DQC_OWD
|DqcTraceEnable::E_DQC_STAT);
	InstallDqc(cc,c.Get(1),c.Get(7),sendPort,recvPort,2,appStop,trace.get(),stat,max_bps,sender_id);
    sender_id++;
    test_pair++;
    sendPort++;
    recvPort++;
    traces.push_back(std::move(trace));
    
    trace.reset(new DqcTrace(test_pair));
    stat->ReisterAvgDelayId(test_pair);
    log=prefix+std::to_string(test_pair);
    trace->Log(log,DqcTraceEnable::E_DQC_GOODPUT|DqcTraceEnable::E_DQC_RTT|DqcTraceEnable::E_DQC_BW|DqcTraceEnable::E_DQC_OWD
|DqcTraceEnable::E_DQC_STAT);  
    InstallDqc(cc2,c.Get(2),c.Get(8),sendPort,recvPort,4,appStop,trace.get(),stat,max_bps,sender_id);
    sender_id++;
    test_pair++;
    sendPort++;
    recvPort++;
    traces.push_back(std::move(trace));

    trace.reset(new DqcTrace(test_pair));
    stat->ReisterAvgDelayId(test_pair);
    log=prefix+std::to_string(test_pair);
    trace->Log(log,DqcTraceEnable::E_DQC_GOODPUT|DqcTraceEnable::E_DQC_RTT|DqcTraceEnable::E_DQC_BW|DqcTraceEnable::E_DQC_OWD
|DqcTraceEnable::E_DQC_STAT);
	InstallDqc(cc2,c.Get(3),c.Get(9),sendPort,recvPort,6,appStop,trace.get(),stat,max_bps,sender_id);
    sender_id++;
    test_pair++;
    sendPort++;
    recvPort++;
    traces.push_back(std::move(trace));

    Simulator::Stop (Seconds(sim_dur));
    Simulator::Run ();
    Simulator::Destroy();  
    stat->Flush(linkBw,sim_dur);    
}
int main (int argc, char *argv[]){
    int sim_time=120;
    int ins[]={1};
    char *algos[]={"cubic"};
    for(int c=0;c<(int)sizeof(algos)/sizeof(algos[0]);c++){
        std::string cong=std::string(algos[c]);
        std::string name=cong;
        std::unique_ptr<DqcTraceState> stat;
        stat.reset(new DqcTraceState(name));
        auto inner_start = std::chrono::high_resolution_clock::now();
        for(int i=0;i<sizeof(ins)/sizeof(ins[0]);i++){
            ns3_rtt(ins[i],cong,stat.get(),sim_time);
        }
        auto inner_end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> tm = inner_end - inner_start;
        std::chrono::duration<double, std::ratio<60>> minutes =inner_end- inner_start;
        stat->RecordRuningTime(tm.count(),minutes.count());     
    }
    return 0;
}

