#ifndef __WBLINUX_F_H
#define __WBLINUX_F_H

#include "core.h"
#include "mds_s.h"

//=========================================================================
// Copyright (c) 1996-2004 Winbond Electronic Corporation
//
// wblinux_f.h
//
unsigned char WBLINUX_Initial(  struct wbsoft_priv *adapter );
int wb35_start_xmit(struct sk_buff *skb, struct net_device *netdev );
void WBLINUX_stop(  struct wbsoft_priv *adapter );
void WBLINUX_Destroy(  struct wbsoft_priv *adapter );
void wb35_set_multicast( struct net_device *netdev );
struct net_device_stats * wb35_netdev_stats( struct net_device *netdev );
void WBLINUX_stop(  struct wbsoft_priv *adapter );
void WbWlanHalt(  struct wbsoft_priv *adapter );
unsigned char WbWLanInitialize(struct wbsoft_priv *adapter);

#endif
