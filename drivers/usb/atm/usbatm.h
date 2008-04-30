/******************************************************************************
 *  usbatm.h - Generic USB xDSL driver core
 *
 *  Copyright (C) 2001, Alcatel
 *  Copyright (C) 2003, Duncan Sands, SolNegro, Josep Comas
 *  Copyright (C) 2004, David Woodhouse
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 *
 *  You should have received a copy of the GNU General Public License along with
 *  this program; if not, write to the Free Software Foundation, Inc., 59
 *  Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 ******************************************************************************/

#ifndef	_USBATM_H_
#define	_USBATM_H_

#include <linux/atm.h>
#include <linux/atmdev.h>
#include <linux/completion.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/stringify.h>
#include <linux/usb.h>
#include <linux/mutex.h>

/*
#define VERBOSE_DEBUG
*/

#ifdef DEBUG
#define UDSL_ASSERT(x)	BUG_ON(!(x))
#else
#define UDSL_ASSERT(x)	do { if (!(x)) warn("failed assertion '%s' at line %d", __stringify(x), __LINE__); } while(0)
#endif

#define usb_err(instance, format, arg...)	\
	dev_err(&(instance)->usb_intf->dev , format , ## arg)
#define usb_info(instance, format, arg...)	\
	dev_info(&(instance)->usb_intf->dev , format , ## arg)
#define usb_warn(instance, format, arg...)	\
	dev_warn(&(instance)->usb_intf->dev , format , ## arg)
#ifdef DEBUG
#define usb_dbg(instance, format, arg...)	\
        dev_printk(KERN_DEBUG , &(instance)->usb_intf->dev , format , ## arg)
#else
#define usb_dbg(instance, format, arg...)	\
	do {} while (0)
#endif

/* FIXME: move to dev_* once ATM is driver model aware */
#define atm_printk(level, instance, format, arg...)	\
	printk(level "ATM dev %d: " format ,		\
	(instance)->atm_dev->number , ## arg)

#define atm_err(instance, format, arg...)	\
	atm_printk(KERN_ERR, instance , format , ## arg)
#define atm_info(instance, format, arg...)	\
	atm_printk(KERN_INFO, instance , format , ## arg)
#define atm_warn(instance, format, arg...)	\
	atm_printk(KERN_WARNING, instance , format , ## arg)
#ifdef DEBUG
#define atm_dbg(instance, format, arg...)	\
	atm_printk(KERN_DEBUG, instance , format , ## arg)
#define atm_rldbg(instance, format, arg...)	\
	if (printk_ratelimit())				\
		atm_printk(KERN_DEBUG, instance , format , ## arg)
#else
#define atm_dbg(instance, format, arg...)	\
	do {} while (0)
#define atm_rldbg(instance, format, arg...)	\
	do {} while (0)
#endif


/* flags, set by mini-driver in bind() */

#define UDSL_SKIP_HEAVY_INIT	(1<<0)
#define UDSL_USE_ISOC		(1<<1)
#define UDSL_IGNORE_EILSEQ	(1<<2)


/* mini driver */

struct usbatm_data;

/*
*  Assuming all methods exist and succeed, they are called in this order:
*
*  	bind, heavy_init, atm_start, ..., atm_stop, unbind
*/

struct usbatm_driver {
	const char *driver_name;

	/* init device ... can sleep, or cause probe() failure */
        int (*bind) (struct usbatm_data *, struct usb_interface *,
		     const struct usb_device_id *id);

	/* additional device initialization that is too slow to be done in probe() */
        int (*heavy_init) (struct usbatm_data *, struct usb_interface *);

	/* cleanup device ... can sleep, but can't fail */
        void (*unbind) (struct usbatm_data *, struct usb_interface *);

	/* init ATM device ... can sleep, or cause ATM initialization failure */
	int (*atm_start) (struct usbatm_data *, struct atm_dev *);

	/* cleanup ATM device ... can sleep, but can't fail */
	void (*atm_stop) (struct usbatm_data *, struct atm_dev *);

        int bulk_in;	/* bulk rx endpoint */
        int isoc_in;	/* isochronous rx endpoint */
        int bulk_out;	/* bulk tx endpoint */

	unsigned rx_padding;
	unsigned tx_padding;
};

extern int usbatm_usb_probe(struct usb_interface *intf, const struct usb_device_id *id,
		struct usbatm_driver *driver);
extern void usbatm_usb_disconnect(struct usb_interface *intf);


struct usbatm_channel {
	int endpoint;			/* usb pipe */
	unsigned int stride;		/* ATM cell size + padding */
	unsigned int buf_size;		/* urb buffer size */
	unsigned int packet_size;	/* endpoint maxpacket */
	spinlock_t lock;
	struct list_head list;
	struct tasklet_struct tasklet;
	struct timer_list delay;
	struct usbatm_data *usbatm;
};

/* main driver data */

struct usbatm_data {
	/******************
	*  public fields  *
        ******************/

	/* mini driver */
	struct usbatm_driver *driver;
	void *driver_data;
	char driver_name[16];
	unsigned int flags; /* set by mini-driver in bind() */

	/* USB device */
	struct usb_device *usb_dev;
	struct usb_interface *usb_intf;
	char description[64];

	/* ATM device */
	struct atm_dev *atm_dev;

	/********************************
	*  private fields - do not use  *
        ********************************/

	struct kref refcount;
	struct mutex serialize;
	int disconnected;

	/* heavy init */
	struct task_struct *thread;
	struct completion thread_started;
	struct completion thread_exited;

	/* ATM device */
	struct list_head vcc_list;

	struct usbatm_channel rx_channel;
	struct usbatm_channel tx_channel;

	struct sk_buff_head sndqueue;
	struct sk_buff *current_skb;	/* being emptied */

	struct usbatm_vcc_data *cached_vcc;
	int cached_vci;
	short cached_vpi;

	unsigned char *cell_buf;	/* holds partial rx cell */
	unsigned int buf_usage;

	struct urb *urbs[0];
};

#endif	/* _USBATM_H_ */
