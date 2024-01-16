/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __USBHID_H
#define __USBHID_H

/*
 *  Copyright (c) 1999 Andreas Gal
 *  Copyright (c) 2000-2001 Vojtech Pavlik
 *  Copyright (c) 2006 Jiri Kosina
 */

/*
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/timer.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/input.h>

/*  API provided by hid-core.c for USB HID drivers */
void usbhid_init_reports(struct hid_device *hid);
struct usb_interface *usbhid_find_interface(int minor);

/* iofl flags */
#define HID_CTRL_RUNNING	1
#define HID_OUT_RUNNING		2
#define HID_IN_RUNNING		3
#define HID_RESET_PENDING	4
#define HID_SUSPENDED		5
#define HID_CLEAR_HALT		6
#define HID_DISCONNECTED	7
#define HID_STARTED		8
#define HID_KEYS_PRESSED	10
#define HID_NO_BANDWIDTH	11
#define HID_RESUME_RUNNING	12
/*
 * The device is opened, meaning there is a client that is interested
 * in data coming from the device.
 */
#define HID_OPENED		13
/*
 * We are polling input endpoint by [re]submitting IN URB, because
 * either HID device is opened or ALWAYS POLL quirk is set for the
 * device.
 */
#define HID_IN_POLLING		14

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

	struct urb *urbctrl;                                            /* Control URB */
	struct usb_ctrlrequest *cr;                                     /* Control request struct */
	struct hid_control_fifo ctrl[HID_CONTROL_FIFO_SIZE];  		/* Control fifo */
	unsigned char ctrlhead, ctrltail;                               /* Control fifo head & tail */
	char *ctrlbuf;                                                  /* Control buffer */
	dma_addr_t ctrlbuf_dma;                                         /* Control buffer dma */
	unsigned long last_ctrl;						/* record of last output for timeouts */

	struct urb *urbout;                                             /* Output URB */
	struct hid_output_fifo out[HID_CONTROL_FIFO_SIZE];              /* Output pipe fifo */
	unsigned char outhead, outtail;                                 /* Output pipe fifo head & tail */
	char *outbuf;                                                   /* Output buffer */
	dma_addr_t outbuf_dma;                                          /* Output buffer dma */
	unsigned long last_out;							/* record of last output for timeouts */

	struct mutex mutex;						/* start/stop/open/close */
	spinlock_t lock;						/* fifo spinlock */
	unsigned long iofl;                                             /* I/O flags (CTRL_RUNNING, OUT_RUNNING) */
	struct timer_list io_retry;                                     /* Retry timer */
	unsigned long stop_retry;                                       /* Time to give up, in jiffies */
	unsigned int retry_delay;                                       /* Delay length in ms */
	struct work_struct reset_work;                                  /* Task context for resets */
	wait_queue_head_t wait;						/* For sleeping */
};

#define	hid_to_usb_dev(hid_dev) \
	to_usb_device(hid_dev->dev.parent->parent)

#endif

