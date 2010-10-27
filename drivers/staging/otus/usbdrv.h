/*
 * Copyright (c) 2007-2008 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/*                                                                      */
/*  Module Name : usbdrv.h                                              */
/*                                                                      */
/*  Abstract                                                            */
/*     This module contains network interface up/down related definition*/
/*                                                                      */
/*  NOTES                                                               */
/*     Platform dependent.                                              */
/*                                                                      */
/************************************************************************/

#ifndef _USBDRV_H
#define _USBDRV_H

#define WLAN_USB    0
#define WLAN_PCI    1

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/uaccess.h>
#include <linux/wireless.h>
#include <linux/if_arp.h>
#include <linux/slab.h>
#include <linux/io.h>

#include "zdcompat.h"

#include "oal_dt.h"
#include "oal_marc.h"
#include "80211core/pub_zfi.h"
/* #include "pub_zfw.h"	*/
#include "80211core/pub_usb.h"

#include <linux/usb.h>
/* Please include header files for device type in the beginning of this file */
#define urb_t                       struct urb

#define usb_complete_t              usb_complete_t
#define pipe_t                      u32_t

/* USB Endpoint definition */
#define USB_WLAN_TX_PIPE                    1
#define USB_WLAN_RX_PIPE                    2
#define USB_REG_IN_PIPE                     3
#define USB_REG_OUT_PIPE                    4

#ifdef ZM_HOSTAPD_SUPPORT
#include "athr_common.h"
#endif

/**************************************************************************
**		Descriptor Data Structure
***************************************************************************/
struct driver_stats {
	struct net_device_stats net_stats;
};

#define ZM_MAX_RX_BUFFER_SIZE               8192

#if ZM_USB_TX_STREAM_MODE == 1
#define ZM_MAX_TX_AGGREGATE_NUM             4
#define ZM_USB_TX_BUF_SIZE                  8096
#define ZM_MAX_TX_URB_NUM                   4
#else
#define ZM_USB_TX_BUF_SIZE                  2048
#define ZM_MAX_TX_URB_NUM                   8
#endif
#define ZM_USB_REG_MAX_BUF_SIZE             64
#define ZM_MAX_RX_URB_NUM                   16
#define ZM_MAX_TX_BUF_NUM                   128

typedef struct UsbTxQ {
    zbuf_t *buf;
    u8_t hdr[80];
    u16_t hdrlen;
    u8_t snap[8];
    u16_t snapLen;
    u8_t tail[16];
    u16_t tailLen;
    u16_t offset;
} UsbTxQ_t;


struct zdap_ioctl {
	u16_t cmd;		  /* Command to run */
	u32_t addr;		  /* Length of the data buffer */
	u32_t value;		/* Pointer to the data buffer */
	u8_t	data[0x100];
};

#define ZM_OAL_MAX_STA_SUPPORT 16

struct usbdrv_private {
	/* linux used */
	struct net_device 	*device;
#if (WLAN_HOSTIF == WLAN_PCI)
	struct pci_dev 		*pdev;
#endif
#if (WLAN_HOSTIF == WLAN_USB)
	struct usb_device	*udev;
	struct usb_interface    *interface;
#endif
	struct driver_stats drv_stats;
	char ifname[IFNAMSIZ];
	int			  using_dac;
	u8_t			rev_id;		/* adapter PCI revision ID */
	rwlock_t 		isolate_lock;
    spinlock_t      cs_lock;
	int 			driver_isolated;
#if (WLAN_HOSTIF == WLAN_PCI)
	void			*regp;
#endif

	 /* timer for heart beat */
	struct timer_list hbTimer10ms;

	/* For driver core */
	void *wd;

#if (WLAN_HOSTIF == WLAN_USB)
	u8_t		      txUsbBuf[ZM_MAX_TX_URB_NUM][ZM_USB_TX_BUF_SIZE];
	u8_t		      regUsbReadBuf[ZM_USB_REG_MAX_BUF_SIZE];
	u8_t		      regUsbWriteBuf[ZM_USB_REG_MAX_BUF_SIZE];
	urb_t			*WlanTxDataUrb[ZM_MAX_TX_URB_NUM];
	urb_t			*WlanRxDataUrb[ZM_MAX_RX_URB_NUM];
	urb_t			*RegOutUrb;
	urb_t			*RegInUrb;
	UsbTxQ_t		  UsbTxBufQ[ZM_MAX_TX_BUF_NUM];
	zbuf_t		    *UsbRxBufQ[ZM_MAX_RX_URB_NUM];
	 u16_t		     TxBufHead;
	 u16_t		     TxBufTail;
	 u16_t		     TxBufCnt;
	 u16_t		     TxUrbHead;
	 u16_t		     TxUrbTail;
	 u16_t		     TxUrbCnt;
	 u16_t		     RxBufHead;
	 u16_t		     RxBufTail;
	 u16_t		     RxBufCnt;
#endif

#if ZM_USB_STREAM_MODE == 1
	 zbuf_t		    *reamin_buf;
#endif

#ifdef ZM_HOSTAPD_SUPPORT
	 struct athr_wlan_param  athr_wpa_req;
#endif
	 struct sock	      *netlink_sk;
	 u8_t	     DeviceOpened; /* CWYang(+) */
	 u8_t	     supIe[50];
	 u8_t	     supLen;
	 struct ieee80211req_wpaie stawpaie[ZM_OAL_MAX_STA_SUPPORT];
	 u8_t	     forwardMgmt;

	 struct zfCbUsbFuncTbl usbCbFunctions;

	 /* For keventd */
	 u32_t		     flags;
	 unsigned long	    kevent_flags;
	 u16_t		     kevent_ready;

	 struct semaphore	 ioctl_sem;
	 struct work_struct      kevent;
	 wait_queue_head_t	wait_queue_event;
#ifdef ZM_HALPLUS_LOCK
	 unsigned long	    hal_irqFlag;
#endif
	 u16_t		     adapterState;
};

/* WDS */
#define ZM_WDS_PORT_NUMBER  6

struct zsWdsStruct {
    struct net_device *dev;
    u16_t openFlag;
};

/* VAP */
#define ZM_VAP_PORT_NUMBER  7

struct zsVapStruct {
    struct net_device *dev;
    u16_t openFlag;
};

/***************************************/

#define ZM_IOCTL_REG_READ			0x01
#define ZM_IOCTL_REG_WRITE			0x02
#define ZM_IOCTL_MEM_DUMP			0x03
#define ZM_IOCTL_REG_DUMP			0x05
#define ZM_IOCTL_TXD_DUMP 			0x06
#define ZM_IOCTL_RXD_DUMP 			0x07
#define ZM_IOCTL_MEM_READ			0x0B
#define ZM_IOCTL_MEM_WRITE			0x0C
#define ZM_IOCTL_DMA_TEST	    		0x10
#define ZM_IOCTL_REG_TEST	    		0x11
#define ZM_IOCTL_TEST		 		0x80
#define ZM_IOCTL_TALLY				0x81 /* CWYang(+) */
#define ZM_IOCTL_RTS				0xA0
#define ZM_IOCTL_MIX_MODE			0xA1
#define ZM_IOCTL_FRAG				0xA2
#define ZM_IOCTL_SCAN				0xA3
#define ZM_IOCTL_KEY				0xA4
#define ZM_IOCTL_RATE				0xA5
#define ZM_IOCTL_ENCRYPTION_MODE		0xA6
#define ZM_IOCTL_GET_TXCNT			0xA7
#define ZM_IOCTL_GET_DEAGG_CNT    		0xA8
#define ZM_IOCTL_DURATION_MODE			0xA9
#define ZM_IOCTL_SET_AES_KEY			0xAA
#define ZM_IOCTL_SET_AES_MODE			0xAB
#define ZM_IOCTL_SIGNAL_STRENGTH		0xAC /* CWYang(+) */
#define ZM_IOCTL_SIGNAL_QUALITY			0xAD /* CWYang(+) */
#define ZM_IOCTL_SET_PIBSS_MODE			0xAE

#define	ZDAPIOCTL				SIOCDEVPRIVATE

enum devState {
    Opened,
    Enabled,
    Disabled,
    Closed
};

#endif	/* _USBDRV_H */

