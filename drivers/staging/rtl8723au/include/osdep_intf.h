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
 ******************************************************************************/

#ifndef __OSDEP_INTF_H_
#define __OSDEP_INTF_H_

#include <osdep_service.h>
#include <drv_types.h>

int rtw_init_drv_sw23a(struct rtw_adapter *padapter);
int rtw_free_drv_sw23a(struct rtw_adapter *padapter);
int rtw_reset_drv_sw23a(struct rtw_adapter *padapter);

void rtw_cancel_all_timer23a(struct rtw_adapter *padapter);

int rtw_init_netdev23a_name23a(struct net_device *pnetdev, const char *ifname);
struct net_device *rtw_init_netdev23a(struct rtw_adapter *padapter);

u16 rtw_recv_select_queue23a(struct sk_buff *skb);

void rtw_ips_dev_unload23a(struct rtw_adapter *padapter);

int rtw_ips_pwr_up23a(struct rtw_adapter *padapter);
void rtw_ips_pwr_down23a(struct rtw_adapter *padapter);

int rtw_drv_register_netdev(struct rtw_adapter *padapter);
void rtw_ndev_destructor(struct net_device *ndev);

int rtl8723au_inirp_init(struct rtw_adapter *Adapter);
int rtl8723au_inirp_deinit(struct rtw_adapter *Adapter);
void rtl8723a_usb_intf_stop(struct rtw_adapter *padapter);

#endif	/* _OSDEP_INTF_H_ */
