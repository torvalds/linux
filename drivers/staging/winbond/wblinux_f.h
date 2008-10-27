//=========================================================================
// Copyright (c) 1996-2004 Winbond Electronic Corporation
//
// wblinux_f.h
//
u32 WBLINUX_MemoryAlloc(  void* *VirtualAddress,  u32 Length );
s32 EncapAtomicInc(  struct wb35_adapter *adapter,  void* pAtomic );
s32 EncapAtomicDec(  struct wb35_adapter *adapter,  void* pAtomic );
void WBLinux_ReceivePacket(  struct wb35_adapter *adapter,  PRXLAYER1 pRxLayer1 );
unsigned char WBLINUX_Initial(  struct wb35_adapter *adapter );
int wb35_start_xmit(struct sk_buff *skb, struct net_device *netdev );
void WBLINUX_GetNextPacket(  struct wb35_adapter *adapter,  PDESCRIPTOR pDes );
void WBLINUX_GetNextPacketCompleted(  struct wb35_adapter *adapter,  PDESCRIPTOR pDes );
void WBLINUX_stop(  struct wb35_adapter *adapter );
void WBLINUX_Destroy(  struct wb35_adapter *adapter );
void wb35_set_multicast( struct net_device *netdev );
struct net_device_stats * wb35_netdev_stats( struct net_device *netdev );
void WBLINUX_stop(  struct wb35_adapter *adapter );
void WbWlanHalt(  struct wb35_adapter *adapter );
void WBLINUX_ConnectStatus(  struct wb35_adapter *adapter,  u32 flag );
unsigned char WbWLanInitialize(struct wb35_adapter *adapter);

