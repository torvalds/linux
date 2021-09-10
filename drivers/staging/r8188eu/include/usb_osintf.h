/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#ifndef __USB_OSINTF_H
#define __USB_OSINTF_H

#include "osdep_service.h"
#include "drv_types.h"
#include "usb_vendor_req.h"

extern char *rtw_initmac;
extern int rtw_mc2u_disable;

#define USBD_HALTED(Status) ((u32)(Status) >> 30 == 3)

u8 usbvendorrequest(struct dvobj_priv *pdvobjpriv, enum bt_usb_request brequest,
		    enum rt_usb_wvalue wvalue, u8 windex, void *data,
		    u8 datalen, u8 isdirectionin);
int pm_netdev_open(struct net_device *pnetdev, u8 bnormal);
void netdev_br_init(struct net_device *netdev);
void dhcp_flag_bcast(struct adapter *priv, struct sk_buff *skb);
void *scdb_findEntry(struct adapter *priv, unsigned char *macAddr,
		     unsigned char *ipAddr);
void nat25_db_expire(struct adapter *priv);
int nat25_db_handle(struct adapter *priv, struct sk_buff *skb, int method);

#endif
