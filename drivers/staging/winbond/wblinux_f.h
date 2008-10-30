#ifndef __WBLINUX_F_H
#define __WBLINUX_F_H

#include "adapter.h"
#include "mds_s.h"

//=========================================================================
// Copyright (c) 1996-2004 Winbond Electronic Corporation
//
// wblinux_f.h
//
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
unsigned char WbWLanInitialize(struct wb35_adapter *adapter);

#endif
