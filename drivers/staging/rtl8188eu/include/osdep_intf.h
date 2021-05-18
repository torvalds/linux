/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/

#ifndef __OSDEP_INTF_H_
#define __OSDEP_INTF_H_

#include <osdep_service.h>
#include <drv_types.h>

extern char *rtw_initmac;
extern int rtw_mc2u_disable;

u8 rtw_init_drv_sw(struct adapter *padapter);
u8 rtw_free_drv_sw(struct adapter *padapter);
u8 rtw_reset_drv_sw(struct adapter *padapter);

void rtw_stop_drv_threads(struct adapter *padapter);
void rtw_cancel_all_timer(struct adapter *padapter);

int rtw_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);

struct net_device *rtw_init_netdev(void);
u16 rtw_recv_select_queue(struct sk_buff *skb);

int netdev_open(struct net_device *pnetdev);
int ips_netdrv_open(struct adapter *padapter);
void rtw_ips_dev_unload(struct adapter *padapter);
int rtw_ips_pwr_up(struct adapter *padapter);
void rtw_ips_pwr_down(struct adapter *padapter);

#endif	/* _OSDEP_INTF_H_ */
