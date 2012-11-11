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

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>

struct intf_priv {
	
	u8 *intf_dev;
	u32	max_iosz; 	//USB2.0: 128, USB1.1: 64, SDIO:64
	u32	max_xmitsz; //USB2.0: unlimited, SDIO:512
	u32	max_recvsz; //USB2.0: unlimited, SDIO:512

	volatile u8 *io_rwmem;
	volatile u8 *allocated_io_rwmem;
	u32	io_wsz; //unit: 4bytes
	u32	io_rsz;//unit: 4bytes
	u8 intf_status;	
	
	void (*_bus_io)(u8 *priv);	

/*
Under Sync. IRP (SDIO/USB)
A protection mechanism is necessary for the io_rwmem(read/write protocol)

Under Async. IRP (SDIO/USB)
The protection mechanism is through the pending queue.
*/

	_mutex ioctl_mutex;

	
#ifdef PLATFORM_LINUX	
	#ifdef CONFIG_USB_HCI	
	// when in USB, IO is through interrupt in/out endpoints
	struct usb_device 	*udev;
	PURB	piorw_urb;
	u8 io_irp_cnt;
	u8 bio_irp_pending;
	_sema io_retevt;
	_timer	io_timer;
	u8 bio_irp_timeout;
	u8 bio_timer_cancel;
	#endif
#endif

#ifdef PLATFORM_OS_XP
	#ifdef CONFIG_SDIO_HCI
		// below is for io_rwmem...	
		PMDL pmdl;
		PSDBUS_REQUEST_PACKET  sdrp;
		PSDBUS_REQUEST_PACKET  recv_sdrp;
		PSDBUS_REQUEST_PACKET  xmit_sdrp;

			PIRP		piorw_irp;

	#endif
	#ifdef CONFIG_USB_HCI
		PURB	piorw_urb;
		PIRP		piorw_irp;
		u8 io_irp_cnt;
		u8 bio_irp_pending;
		_sema io_retevt;	
	#endif	
#endif

};	


#ifdef CONFIG_R871X_TEST
int rtw_start_pseudo_adhoc(_adapter *padapter);
int rtw_stop_pseudo_adhoc(_adapter *padapter);
#endif

u8 rtw_init_drv_sw(_adapter *padapter);
u8 rtw_free_drv_sw(_adapter *padapter);
u8 rtw_reset_drv_sw(_adapter *padapter);

u32 rtw_start_drv_threads(_adapter *padapter);
void rtw_stop_drv_threads (_adapter *padapter);
void rtw_cancel_all_timer(_adapter *padapter);

#ifdef PLATFORM_LINUX
int rtw_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);

int rtw_init_netdev_name(struct net_device *pnetdev, const char *ifname);
struct net_device *rtw_init_netdev(_adapter *padapter);

#ifdef CONFIG_PROC_DEBUG
void rtw_proc_init_one(struct net_device *dev);
void rtw_proc_remove_one(struct net_device *dev);
#endif
#endif


#ifdef PLATFORM_FREEBSD
extern int rtw_ioctl(struct ifnet * ifp, u_long cmd, caddr_t data);
#endif

void rtw_ips_dev_unload(_adapter *padapter);
#ifdef CONFIG_IPS
int rtw_ips_pwr_up(_adapter *padapter);
void rtw_ips_pwr_down(_adapter *padapter);
#endif

#ifdef CONFIG_CONCURRENT_MODE
struct net_device *rtw_drv_if2_init(_adapter *pbuddy_padapter, char *name);
void rtw_drv_if2_free(_adapter *pbuddy_padapter);
#endif

#endif	//_OSDEP_INTF_H_

