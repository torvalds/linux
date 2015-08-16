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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
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

void rtw_stop_drv_threads (struct adapter *padapter);
void rtw_cancel_all_timer(struct adapter *padapter);

int rtw_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);

int rtw_init_netdev_name(struct net_device *pnetdev, const char *ifname);
struct net_device *rtw_init_netdev(struct adapter *padapter);
u16 rtw_recv_select_queue(struct sk_buff *skb);
void rtw_proc_remove_one(struct net_device *dev);

int pm_netdev_open(struct net_device *pnetdev, u8 bnormal);
void rtw_ips_dev_unload(struct adapter *padapter);
int rtw_ips_pwr_up(struct adapter *padapter);
void rtw_ips_pwr_down(struct adapter *padapter);

#endif	/* _OSDEP_INTF_H_ */
