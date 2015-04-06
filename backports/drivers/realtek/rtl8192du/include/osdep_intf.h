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

#ifndef __OSDEP_INTF_H_
#define __OSDEP_INTF_H_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>

struct intf_priv {

	u8 *intf_dev;
	u32	max_iosz;	/* USB2.0: 128, USB1.1: 64, SDIO:64 */
	u32	max_xmitsz; /* USB2.0: unlimited, SDIO:512 */
	u32	max_recvsz; /* USB2.0: unlimited, SDIO:512 */

	volatile u8 *io_rwmem;
	volatile u8 *allocated_io_rwmem;
	u32	io_wsz; /* unit: 4bytes */
	u32	io_rsz;/* unit: 4bytes */
	u8 intf_status;

	void (*_bus_io)(u8 *priv);

/*
Under Sync. IRP (SDIO/USB)
A protection mechanism is necessary for the io_rwmem(read/write protocol)

Under Async. IRP (SDIO/USB)
The protection mechanism is through the pending queue.
*/

	_mutex ioctl_mutex;

	/*  when in USB, IO is through interrupt in/out endpoints */
	struct usb_device	*udev;
	struct urb *piorw_urb;
	u8 io_irp_cnt;
	u8 bio_irp_pending;
	struct  semaphore io_retevt;
	struct timer_list io_timer;
	u8 bio_irp_timeout;
	u8 bio_timer_cancel;
};


#ifdef CONFIG_R871X_TEST
int rtw_start_pseudo_adhoc(struct rtw_adapter *padapter);
int rtw_stop_pseudo_adhoc(struct rtw_adapter *padapter);
#endif

u8 rtw_init_drv_sw(struct rtw_adapter *padapter);
u8 rtw_free_drv_sw(struct rtw_adapter *padapter);
u8 rtw_reset_drv_sw(struct rtw_adapter *padapter);

u32 rtw_start_drv_threads(struct rtw_adapter *padapter);
void rtw_stop_drv_threads (struct rtw_adapter *padapter);
void rtw_cancel_all_timer(struct rtw_adapter *padapter);

int rtw_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);

int rtw_init_netdev_name(struct net_device *pnetdev, const char *ifname);
struct net_device *rtw_init_netdev(struct rtw_adapter *padapter);

#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,35))
u16 rtw_recv_select_queue(struct sk_buff *skb);
#endif /* LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,35) */

#ifdef CONFIG_PROC_DEBUG
void rtw_proc_init_one(struct net_device *dev);
void rtw_proc_remove_one(struct net_device *dev);
#else /* CONFIG_PROC_DEBUG */
static void rtw_proc_init_one(struct net_device *dev) {}
static void rtw_proc_remove_one(struct net_device *dev) {}
#endif /* CONFIG_PROC_DEBUG */

void rtw_ips_dev_unload(struct rtw_adapter *padapter);
#ifdef CONFIG_IPS
int rtw_ips_pwr_up(struct rtw_adapter *padapter);
void rtw_ips_pwr_down(struct rtw_adapter *padapter);
#endif

#ifdef CONFIG_CONCURRENT_MODE
struct _io_ops;
struct rtw_adapter *rtw_drv_if2_init(struct rtw_adapter *primary_padapter, char *name, void (*set_intf_ops)(struct _io_ops *pops));
void rtw_drv_if2_free(struct rtw_adapter *if2);
void rtw_drv_if2_stop(struct rtw_adapter *if2);
#ifdef CONFIG_MULTI_VIR_IFACES
struct dvobj_priv;
_adapter *rtw_drv_add_vir_if (struct rtw_adapter *primary_padapter, char *name,	void (*set_intf_ops)(struct _io_ops *pops));
void rtw_drv_stop_vir_ifaces(struct dvobj_priv *dvobj);
void rtw_drv_free_vir_ifaces(struct dvobj_priv *dvobj);
#endif /* CONFIG_MULTI_VIR_IFACES */
#endif

void rtw_ndev_destructor(struct net_device *ndev);

#endif	/* _OSDEP_INTF_H_ */
