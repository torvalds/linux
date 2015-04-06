/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 *
 ******************************************************************************/
#ifndef __USB_OSINTF_H
#define __USB_OSINTF_H

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <usb_vendor_req.h>

#define USBD_HALTED(Status) ((u32)(Status) >> 30 == 3)

#ifdef CONFIG_80211N_HT
extern int rtw_ht_enable;
extern int rtw_cbw40_enable;
extern int rtw_ampdu_enable;/* for enable tx_ampdu */
#endif

extern int rtw_mc2u_disable;
extern char *rtw_initmac;

u8 usbvendorrequest(struct dvobj_priv *pdvobjpriv,
		    enum RT_USB_BREQUEST brequest,
		    enum RT_USB_WVALUE wvalue, u8 windex, void *data,
		    u8 datalen, u8 isdirectionin);

void netdev_br_init(struct net_device *netdev);
int pm_netdev_open(struct net_device *pnetdev,u8 bnormal);
int nat25_db_handle(struct rtw_adapter *priv, struct sk_buff *skb, int method);
int nat25_handle_frame(struct rtw_adapter *priv, struct sk_buff *skb);
void dhcp_flag_bcast(struct rtw_adapter *priv, struct sk_buff *skb);
void *scdb_findentry(struct rtw_adapter *priv, unsigned char *macaddr,
		     unsigned char *ipaddr);
void nat25_db_expire(struct rtw_adapter *priv);
u8 str_2char2num(u8 hch, u8 lch);
u8 str_2char2num(u8 hch, u8 lch);
u8 key_2char2num(u8 hch, u8 lch);
u8 convert_ip_addr(u8 hch, u8 mch, u8 lch);
void process_wmmps_data(struct rtw_adapter *padapter,
			struct recv_frame_hdr *precv_frame);
s32 c2h_evt_hdl(struct rtw_adapter *adapter, struct c2h_evt_hdr *c2h_evt,
		c2h_id_filter filter);

#endif
