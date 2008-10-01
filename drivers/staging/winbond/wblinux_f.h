//=========================================================================
// Copyright (c) 1996-2004 Winbond Electronic Corporation
//
// wblinux_f.h
//
u32 WBLINUX_MemoryAlloc(  void* *VirtualAddress,  u32 Length );
s32 EncapAtomicInc(  PADAPTER Adapter,  void* pAtomic );
s32 EncapAtomicDec(  PADAPTER Adapter,  void* pAtomic );
void WBLinux_ReceivePacket(  PADAPTER Adapter,  PRXLAYER1 pRxLayer1 );
unsigned char WBLINUX_Initial(  PADAPTER Adapter );
int wb35_start_xmit(struct sk_buff *skb, struct net_device *netdev );
void WBLINUX_GetNextPacket(  PADAPTER Adapter,  PDESCRIPTOR pDes );
void WBLINUX_GetNextPacketCompleted(  PADAPTER Adapter,  PDESCRIPTOR pDes );
void WBLINUX_stop(  PADAPTER Adapter );
void WBLINUX_Destroy(  PADAPTER Adapter );
void wb35_set_multicast( struct net_device *netdev );
struct net_device_stats * wb35_netdev_stats( struct net_device *netdev );
void WBLINUX_stop(  PADAPTER Adapter );
void WbWlanHalt(  PADAPTER Adapter );
void WBLINUX_ConnectStatus(  PADAPTER Adapter,  u32 flag );



