/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#ifndef __OSDEP_INTF_H_
#define __OSDEP_INTF_H_

#include "osdep_service.h"
#include "drv_types.h"

int netdev_open(struct net_device *pnetdev);
int netdev_close(struct net_device *pnetdev);

u8 rtw_init_drv_sw(struct adapter *padapter);
void rtw_free_drv_sw(struct adapter *padapter);
void rtw_reset_drv_sw(struct adapter *padapter);

int rtw_start_drv_threads(struct adapter *padapter);
void rtw_stop_drv_threads (struct adapter *padapter);
void rtw_cancel_all_timer(struct adapter *padapter);

int rtw_init_netdev_name(struct net_device *pnetdev, const char *ifname);
struct net_device *rtw_init_netdev(struct adapter *padapter);
u16 rtw_recv_select_queue(struct sk_buff *skb);

void rtw_ips_dev_unload(struct adapter *padapter);

int rtw_ips_pwr_up(struct adapter *padapter);
void rtw_ips_pwr_down(struct adapter *padapter);

#endif	/* _OSDEP_INTF_H_ */
