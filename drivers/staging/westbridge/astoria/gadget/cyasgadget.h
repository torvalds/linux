/* cyangadget.h - Linux USB Gadget driver file for the Cypress West Bridge
## ===========================
## Copyright (C) 2010  Cypress Semiconductor
##
## This program is free software; you can redistribute it and/or
## modify it under the terms of the GNU General Public License
## as published by the Free Software Foundation; either version 2
## of the License, or (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program; if not, write to the Free Software
## Foundation, Inc., 51 Franklin Street, Fifth Floor
## Boston, MA  02110-1301, USA.
## ===========================
*/

/*
 * Cypress West Bridge high/full speed USB device controller code
 * Based on the Netchip 2280 device controller by David Brownell
 * in the linux 2.6.10 kernel
 *
 * linux/drivers/usb/gadget/net2280.h
 */

/*
 * Copyright (C) 2002 NetChip Technology, Inc. (http://www.netchip.com)
 * Copyright (C) 2003 David Brownell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _INCLUDED_CYANGADGET_H_
#define _INCLUDED_CYANGADGET_H_

#include <linux/device.h>
#include <linux/moduleparam.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/sched.h>

#include "../include/linux/westbridge/cyastoria.h"
#include "../include/linux/westbridge/cyashal.h"
#include "../include/linux/westbridge/cyasdevice.h"
#include "cyasgadget_ioctl.h"

#include <linux/module.h>
#include <linux/init.h>

/*char driver defines, revisit*/
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/fs.h>		/* everything... */
#include <linux/errno.h>	/* error codes */
#include <linux/types.h>	/* size_t */
#include <linux/proc_fs.h>
#include <linux/fcntl.h>	/* O_ACCMODE */
#include <linux/seq_file.h>
#include <linux/cdev.h>
#include <linux/scatterlist.h>
#include <linux/pagemap.h>
#include <linux/vmalloc.h>	  /* vmalloc(), vfree */
#include <linux/msdos_fs.h> /*fat_alloc_cluster*/
#include <linux/buffer_head.h>
#include <asm/system.h>		/* cli(), *_flags */
#include <linux/uaccess.h>	/* copy_*_user */

extern int mpage_cleardirty(struct address_space *mapping, int num_pages);
extern int fat_get_block(struct inode *, sector_t , struct buffer_head *, int);
extern cy_as_device_handle *cyasdevice_getdevhandle(void);

/* Driver data structures and utilities */
typedef struct cyasgadget_ep {
	struct usb_ep				usb_ep_inst;
	struct cyasgadget			*dev;

	/* analogous to a host-side qh */
	struct list_head			queue;
	const struct usb_endpoint_descriptor	*desc;
	unsigned			num:8,
						fifo_size:12,
						in_fifo_validate:1,
						out_overflow:1,
						stopped:1,
						is_in:1,
						is_iso:1;
	cy_as_usb_end_point_config cyepconfig;
} cyasgadget_ep;

typedef struct cyasgadget_req {
	struct usb_request		req;
	struct list_head		queue;
	int	 ep_num;
	unsigned			mapped:1,
						valid:1,
						complete:1,
						ep_stopped:1;
} cyasgadget_req;

typedef struct cyasgadget {
	/* each device provides one gadget, several endpoints */
	struct usb_gadget			gadget;
	spinlock_t					lock;
	struct cyasgadget_ep		an_gadget_ep[16];
	struct usb_gadget_driver	 *driver;
	/* Handle to the West Bridge device */
	cy_as_device_handle			dev_handle;
	unsigned			enabled:1,
						protocol_stall:1,
						softconnect:1,
						outsetupreq:1;
	struct completion	thread_complete;
	wait_queue_head_t	thread_wq;
	struct semaphore	thread_sem;
	struct list_head	thread_queue;

	cy_bool tmtp_send_complete;
	cy_bool tmtp_get_complete;
	cy_bool tmtp_need_new_blk_tbl;
	/* Data member used to store the SendObjectComplete event data */
	cy_as_mtp_send_object_complete_data tmtp_send_complete_data;
	/* Data member used to store the GetObjectComplete event data */
	cy_as_mtp_get_object_complete_data tmtp_get_complete_data;

} cyasgadget;

static inline void set_halt(cyasgadget_ep *ep)
{
	return;
}

static inline void clear_halt(cyasgadget_ep *ep)
{
	return;
}

#define xprintk(dev, level, fmt, args...) \
	printk(level "%s %s: " fmt, driver_name, \
			pci_name(dev->pdev), ## args)

#ifdef DEBUG
#undef DEBUG
#define DEBUG(dev, fmt, args...) \
	xprintk(dev, KERN_DEBUG, fmt, ## args)
#else
#define DEBUG(dev, fmt, args...) \
	do { } while (0)
#endif /* DEBUG */

#ifdef VERBOSE
#define VDEBUG DEBUG
#else
#define VDEBUG(dev, fmt, args...) \
	do { } while (0)
#endif	/* VERBOSE */

#define ERROR(dev, fmt, args...) \
	xprintk(dev, KERN_ERR, fmt, ## args)
#define GADG_WARN(dev, fmt, args...) \
	xprintk(dev, KERN_WARNING, fmt, ## args)
#define INFO(dev, fmt, args...) \
	xprintk(dev, KERN_INFO, fmt, ## args)

/*-------------------------------------------------------------------------*/

static inline void start_out_naking(struct cyasgadget_ep *ep)
{
	return;
}

static inline void stop_out_naking(struct cyasgadget_ep *ep)
{
	return;
}

#endif	/* _INCLUDED_CYANGADGET_H_ */
