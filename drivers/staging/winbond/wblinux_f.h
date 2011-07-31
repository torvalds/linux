#ifndef __WBLINUX_F_H
#define __WBLINUX_F_H

#include "core.h"
#include "mds_s.h"

/*
 * ====================================================================
 * Copyright (c) 1996-2004 Winbond Electronic Corporation
 *
 * wblinux_f.h
 * ====================================================================
 */
int wb35_start_xmit(struct sk_buff *skb, struct net_device *netdev);
void wb35_set_multicast(struct net_device *netdev);
struct net_device_stats *wb35_netdev_stats(struct net_device *netdev);
#endif
