/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
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

#ifdef PLATFORM_LINUX

#ifdef CONFIG_USB_HCI
#include <linux/usb.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,21))
#include <linux/usb_ch9.h>
#else
#include <linux/usb/ch9.h>
#endif
#endif

#endif

#ifdef PLATFORM_OS_XP

#ifdef CONFIG_SDIO_HCI
#include <ntddsd.h>
#endif

#ifdef CONFIG_USB_HCI
#include <usb.h>
#include <usbioctl.h>
#include <usbdlib.h>
#endif

#endif

#define RND4(x)	(((x >> 2) + (((x & 3) == 0) ?  0: 1)) << 2)


struct intf_priv {
	
	u8 *intf_dev;
	//u32	max_iosz; 	//USB2.0: 128, USB1.1: 64, SDIO:64
#ifdef CONFIG_SDIO_HCI
	u32	max_xmitsz; //USB2.0: unlimited, SDIO:512
	u32	max_recvsz; //USB2.0: unlimited, SDIO:512

	volatile u8 *io_rwmem;
	volatile u8 *allocated_io_rwmem;
	//u8 intf_status;	
	
	//void (*_bus_io)(u8 *priv);	

/*
Under Sync. IRP (SDIO/USB)
A protection mechanism is necessary for the io_rwmem(read/write protocol)

Under Async. IRP (SDIO/USB)
The protection mechanism is through the pending queue.
*/

	_rwlock rwlock;	
#endif
	
#ifdef PLATFORM_LINUX	
	#ifdef CONFIG_USB_HCI	
	// when in USB, IO is through interrupt in/out endpoints
	struct usb_device 	*udev;
	PURB	piorw_urb;
	_sema	io_retevt;
	//_timer	io_timer;
	//u8	bio_irp_timeout;
	//u8	bio_timer_cancel;
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


struct intf_hdl;

extern uint _init_intf_hdl(_adapter *padapter, struct intf_hdl *pintf_hdl);
extern void _unload_intf_hdl(struct intf_priv *pintfpriv);

u32 rtl871x_open_fw(_adapter * padapter, void **pphfwfile_hdl, u8 **ppmappedfw);
void rtl871x_close_fw(_adapter *padapter, void *phfwfile_hdl);


#ifdef PLATFORM_LINUX
int r871x_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
#endif


#endif	//_OSDEP_INTF_H_

