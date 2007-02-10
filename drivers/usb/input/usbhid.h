#ifndef __USBHID_H
#define __USBHID_H

/*
 *  Copyright (c) 1999 Andreas Gal
 *  Copyright (c) 2000-2001 Vojtech Pavlik
 *  Copyright (c) 2006 Jiri Kosina
 */

/*
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/input.h>

/*  API provided by hid-core.c for USB HID drivers */
int usbhid_wait_io(struct hid_device* hid);
void usbhid_close(struct hid_device *hid);
int usbhid_open(struct hid_device *hid);
void usbhid_init_reports(struct hid_device *hid);
void usbhid_submit_report(struct hid_device *hid, struct hid_report *report, unsigned char dir);

/*
 * USB-specific HID struct, to be pointed to
 * from struct hid_device->driver_data
 */

struct usbhid_device {
	struct hid_device *hid;						/* pointer to corresponding HID dev */

	struct usb_interface *intf;                                     /* USB interface */
	int ifnum;                                                      /* USB interface number */

	unsigned int bufsize;                                           /* URB buffer size */

	struct urb *urbin;                                              /* Input URB */
	char *inbuf;                                                    /* Input buffer */
	dma_addr_t inbuf_dma;                                           /* Input buffer dma */
	spinlock_t inlock;                                              /* Input fifo spinlock */

	struct urb *urbctrl;                                            /* Control URB */
	struct usb_ctrlrequest *cr;                                     /* Control request struct */
	dma_addr_t cr_dma;                                              /* Control request struct dma */
	struct hid_control_fifo ctrl[HID_CONTROL_FIFO_SIZE];  		/* Control fifo */
	unsigned char ctrlhead, ctrltail;                               /* Control fifo head & tail */
	char *ctrlbuf;                                                  /* Control buffer */
	dma_addr_t ctrlbuf_dma;                                         /* Control buffer dma */
	spinlock_t ctrllock;                                            /* Control fifo spinlock */

	struct urb *urbout;                                             /* Output URB */
	struct hid_report *out[HID_CONTROL_FIFO_SIZE];                  /* Output pipe fifo */
	unsigned char outhead, outtail;                                 /* Output pipe fifo head & tail */
	char *outbuf;                                                   /* Output buffer */
	dma_addr_t outbuf_dma;                                          /* Output buffer dma */
	spinlock_t outlock;                                             /* Output fifo spinlock */

	unsigned long iofl;                                             /* I/O flags (CTRL_RUNNING, OUT_RUNNING) */
	struct timer_list io_retry;                                     /* Retry timer */
	unsigned long stop_retry;                                       /* Time to give up, in jiffies */
	unsigned int retry_delay;                                       /* Delay length in ms */
	struct work_struct reset_work;                                  /* Task context for resets */

};

#define	hid_to_usb_dev(hid_dev) \
	container_of(hid_dev->dev->parent, struct usb_device, dev)

#endif

