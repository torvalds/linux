/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#ifndef __OSDEP_INTF_H_
#define __OSDEP_INTF_H_

#include "osdep_service.h"
#include "drv_types.h"

struct intf_priv {
	u8 *intf_dev;
	u32	max_iosz;	/* USB2.0: 128, USB1.1: 64, SDIO:64 */
	u32	max_xmitsz; /* USB2.0: unlimited, SDIO:512 */
	u32	max_recvsz; /* USB2.0: unlimited, SDIO:512 */

	u8 *io_rwmem;
	u8 *allocated_io_rwmem;
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
	struct mutex ioctl_mutex;
	/*  when in USB, IO is through interrupt in/out endpoints */
	struct usb_device	*udev;
	struct urb *piorw_urb;
	u8 io_irp_cnt;
	u8 bio_irp_pending;
	struct semaphore  io_retevt;
	struct timer_list io_timer;
	u8 bio_irp_timeout;
	u8 bio_timer_cancel;
};

u8 rtw_init_drv_sw(struct adapter *padapter);
u8 rtw_free_drv_sw(struct adapter *padapter);
u8 rtw_reset_drv_sw(struct adapter *padapter);

u32 rtw_start_drv_threads(struct adapter *padapter);
void rtw_stop_drv_threads (struct adapter *padapter);
void rtw_cancel_all_timer(struct adapter *padapter);

int rtw_init_netdev_name(struct net_device *pnetdev, const char *ifname);
struct net_device *rtw_init_netdev(struct adapter *padapter);
u16 rtw_recv_select_queue(struct sk_buff *skb);
void rtw_proc_init_one(struct net_device *dev);
void rtw_proc_remove_one(struct net_device *dev);

void rtw_ips_dev_unload(struct adapter *padapter);

int rtw_ips_pwr_up(struct adapter *padapter);
void rtw_ips_pwr_down(struct adapter *padapter);
int rtw_hw_suspend(struct adapter *padapter);
int rtw_hw_resume(struct adapter *padapter);

#endif	/* _OSDEP_INTF_H_ */
