/*****************************************************************************
 *
 * Filename:      irda-usb.h
 * Version:       0.10
 * Description:   IrDA-USB Driver
 * Status:        Experimental 
 * Author:        Dag Brattli <dag@brattli.net>
 *
 *	Copyright (C) 2001, Roman Weissgaerber <weissg@vienna.at>
 *      Copyright (C) 2000, Dag Brattli <dag@brattli.net>
 *      Copyright (C) 2001, Jean Tourrilhes <jt@hpl.hp.com>
 *      Copyright (C) 2004, SigmaTel, Inc. <irquality@sigmatel.com>
 *      Copyright (C) 2005, Milan Beno <beno@pobox.sk>
 *      Copyright (C) 2006, Nick FEdchik <nick@fedchik.org.ua>
 *          
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *****************************************************************************/

#include <linux/time.h>

#include <net/irda/irda.h>
#include <net/irda/irda_device.h>      /* struct irlap_cb */

#define RX_COPY_THRESHOLD 200
#define IRDA_USB_MAX_MTU 2051
#define IRDA_USB_SPEED_MTU 64		/* Weird, but work like this */

/* Maximum number of active URB on the Rx path
 * This is the amount of buffers the we keep between the USB harware and the
 * IrDA stack.
 *
 * Note : the network layer does also queue the packets between us and the
 * IrDA stack, and is actually pretty fast and efficient in doing that.
 * Therefore, we don't need to have a large number of URBs, and we can
 * perfectly live happy with only one. We certainly don't need to keep the
 * full IrTTP window around here...
 * I repeat for those who have trouble to understand : 1 URB is plenty
 * good enough to handle back-to-back (brickwalled) frames. I tried it,
 * it works (it's the hardware that has trouble doing it).
 *
 * Having 2 URBs would allow the USB stack to process one URB while we take
 * care of the other and then swap the URBs...
 * On the other hand, increasing the number of URB will have penalities
 * in term of latency and will interact with the link management in IrLAP...
 * Jean II */
#define IU_MAX_ACTIVE_RX_URBS	1	/* Don't touch !!! */

/* When a Rx URB is passed back to us, we can't reuse it immediately,
 * because it may still be referenced by the USB layer. Therefore we
 * need to keep one extra URB in the Rx path.
 * Jean II */
#define IU_MAX_RX_URBS	(IU_MAX_ACTIVE_RX_URBS + 1)

/* Various ugly stuff to try to workaround generic problems */
/* Send speed command in case of timeout, just for trying to get things sane */
#define IU_BUG_KICK_TIMEOUT
/* Show the USB class descriptor */
#undef IU_DUMP_CLASS_DESC 
/* Assume a minimum round trip latency for USB transfer (in us)...
 * USB transfer are done in the next USB slot if there is no traffic
 * (1/19 msec) and is done at 12 Mb/s :
 * Waiting for slot + tx = (53us + 16us) * 2 = 137us minimum.
 * Rx notification will only be done at the end of the USB frame period :
 * OHCI : frame period = 1ms
 * UHCI : frame period = 1ms, but notification can take 2 or 3 ms :-(
 * EHCI : frame period = 125us */
#define IU_USB_MIN_RTT		500	/* This should be safe in most cases */

/* Inbound header */
#define MEDIA_BUSY    0x80

#define SPEED_2400     0x01
#define SPEED_9600     0x02
#define SPEED_19200    0x03
#define SPEED_38400    0x04
#define SPEED_57600    0x05
#define SPEED_115200   0x06
#define SPEED_576000   0x07
#define SPEED_1152000  0x08
#define SPEED_4000000  0x09
#define SPEED_16000000 0x0a

/* Basic capabilities */
#define IUC_DEFAULT	0x00	/* Basic device compliant with 1.0 spec */
/* Main bugs */
#define IUC_SPEED_BUG	0x01	/* Device doesn't set speed after the frame */
#define IUC_NO_WINDOW	0x02	/* Device doesn't behave with big Rx window */
#define IUC_NO_TURN	0x04	/* Device doesn't do turnaround by itself */
/* Not currently used */
#define IUC_SIR_ONLY	0x08	/* Device doesn't behave at FIR speeds */
#define IUC_SMALL_PKT	0x10	/* Device doesn't behave with big Rx packets */
#define IUC_MAX_WINDOW	0x20	/* Device underestimate the Rx window */
#define IUC_MAX_XBOFS	0x40	/* Device need more xbofs than advertised */
#define IUC_STIR421X	0x80	/* SigmaTel 4210/4220/4116 VFIR */

/* USB class definitions */
#define USB_IRDA_HEADER            0x01
#define USB_CLASS_IRDA             0x02 /* USB_CLASS_APP_SPEC subclass */
#define USB_DT_IRDA                0x21
#define USB_IRDA_STIR421X_HEADER   0x03
#define IU_SIGMATEL_MAX_RX_URBS    (IU_MAX_ACTIVE_RX_URBS + \
                                    USB_IRDA_STIR421X_HEADER)

struct irda_class_desc {
	__u8  bLength;
	__u8  bDescriptorType;
	__le16 bcdSpecRevision;
	__u8  bmDataSize;
	__u8  bmWindowSize;
	__u8  bmMinTurnaroundTime;
	__le16 wBaudRate;
	__u8  bmAdditionalBOFs;
	__u8  bIrdaRateSniff;
	__u8  bMaxUnicastList;
} __packed;

/* class specific interface request to get the IrDA-USB class descriptor
 * (6.2.5, USB-IrDA class spec 1.0) */

#define IU_REQ_GET_CLASS_DESC	0x06
#define STIR421X_MAX_PATCH_DOWNLOAD_SIZE 1023

struct irda_usb_cb {
	struct irda_class_desc *irda_desc;
	struct usb_device *usbdev;	/* init: probe_irda */
	struct usb_interface *usbintf;	/* init: probe_irda */
	int netopen;			/* Device is active for network */
	int present;			/* Device is present on the bus */
	__u32 capability;		/* Capability of the hardware */
	__u8  bulk_in_ep;		/* Rx Endpoint assignments */
	__u8  bulk_out_ep;		/* Tx Endpoint assignments */
	__u16 bulk_out_mtu;		/* Max Tx packet size in bytes */
	__u8  bulk_int_ep;		/* Interrupt Endpoint assignments */

	__u8  max_rx_urb;
	struct urb **rx_urb;	        /* URBs used to receive data frames */
	struct urb *idle_rx_urb;	/* Pointer to idle URB in Rx path */
	struct urb *tx_urb;		/* URB used to send data frames */
	struct urb *speed_urb;		/* URB used to send speed commands */
	
	struct net_device *netdev;	/* Yes! we are some kind of netdev. */
	struct irlap_cb   *irlap;	/* The link layer we are binded to */
	struct qos_info qos;
	char *speed_buff;		/* Buffer for speed changes */
	char *tx_buff;

	struct timeval stamp;
	struct timeval now;

	spinlock_t lock;		/* For serializing Tx operations */

	__u16 xbofs;			/* Current xbofs setting */
	__s16 new_xbofs;		/* xbofs we need to set */
	__u32 speed;			/* Current speed */
	__s32 new_speed;		/* speed we need to set */

	__u8 header_length;             /* USB-IrDA frame header size */
	int needspatch;        		/* device needs firmware patch */

	struct timer_list rx_defer_timer;	/* Wait for Rx error to clear */
};

