/*
 * drivers/usb/gadget/f_mass_storage.c
 *
 * Function Driver for USB Mass Storage
 *
 * Copyright (C) 2008 Google, Inc.
 * Author: Mike Lockwood <lockwood@android.com>
 *
 * Based heavily on the file_storage gadget driver in
 * drivers/usb/gadget/file_storage.c and licensed under the same terms:
 *
 * Copyright (C) 2003-2007 Alan Stern
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the above-listed copyright holders may not be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* #define DEBUG */
/* #define VERBOSE_DEBUG */
/* #define DUMP_MSGS */


#include <linux/blkdev.h>
#include <linux/completion.h>
#include <linux/dcache.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fcntl.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/kref.h>
#include <linux/kthread.h>
#include <linux/limits.h>
#include <linux/rwsem.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/switch.h>
#include <linux/freezer.h>
#include <linux/utsname.h>
#include <linux/wakelock.h>
#include <linux/platform_device.h>

#include <linux/usb.h>
#include <linux/usb_usual.h>
#include <linux/usb/ch9.h>
#include <linux/usb/android_composite.h>

#include "gadget_chips.h"


#define BULK_BUFFER_SIZE           4096

/* flush after every 4 meg of writes to avoid excessive block level caching */
#define MAX_UNFLUSHED_BYTES (4 * 1024 * 1024)

/*-------------------------------------------------------------------------*/

#define DRIVER_NAME		"usb_mass_storage"
#define MAX_LUNS		8

static const char shortname[] = DRIVER_NAME;

#ifdef DEBUG
#define LDBG(lun, fmt, args...) \
	dev_dbg(&(lun)->dev , fmt , ## args)
#define MDBG(fmt,args...) \
	printk(KERN_DEBUG DRIVER_NAME ": " fmt , ## args)
#else
#define LDBG(lun, fmt, args...) \
	do { } while (0)
#define MDBG(fmt,args...) \
	do { } while (0)
#undef VERBOSE_DEBUG
#undef DUMP_MSGS
#endif /* DEBUG */

#ifdef VERBOSE_DEBUG
#define VLDBG	LDBG
#else
#define VLDBG(lun, fmt, args...) \
	do { } while (0)
#endif /* VERBOSE_DEBUG */

#define LERROR(lun, fmt, args...) \
	dev_err(&(lun)->dev , fmt , ## args)
#define LWARN(lun, fmt, args...) \
	dev_warn(&(lun)->dev , fmt , ## args)
#define LINFO(lun, fmt, args...) \
	dev_info(&(lun)->dev , fmt , ## args)

#define MINFO(fmt,args...) \
	printk(KERN_INFO DRIVER_NAME ": " fmt , ## args)

#undef DBG
#undef VDBG
#undef ERROR
#undef WARNING
#undef INFO
#define DBG(d, fmt, args...) \
	dev_dbg(&(d)->cdev->gadget->dev , fmt , ## args)
#define VDBG(d, fmt, args...) \
	dev_vdbg(&(d)->cdev->gadget->dev , fmt , ## args)
#define ERROR(d, fmt, args...) \
	dev_err(&(d)->cdev->gadget->dev , fmt , ## args)
#define WARNING(d, fmt, args...) \
	dev_warn(&(d)->cdev->gadget->dev , fmt , ## args)
#define INFO(d, fmt, args...) \
	dev_info(&(d)->cdev->gadget->dev , fmt , ## args)


/*-------------------------------------------------------------------------*/

/* Bulk-only data structures */

/* Command Block Wrapper */
struct bulk_cb_wrap {
	__le32	Signature;		/* Contains 'USBC' */
	u32	Tag;			/* Unique per command id */
	__le32	DataTransferLength;	/* Size of the data */
	u8	Flags;			/* Direction in bit 7 */
	u8	Lun;			/* LUN (normally 0) */
	u8	Length;			/* Of the CDB, <= MAX_COMMAND_SIZE */
	u8	CDB[16];		/* Command Data Block */
};

#define USB_BULK_CB_WRAP_LEN	31
#define USB_BULK_CB_SIG		0x43425355	/* Spells out USBC */
#define USB_BULK_IN_FLAG	0x80

/* Command Status Wrapper */
struct bulk_cs_wrap {
	__le32	Signature;		/* Should = 'USBS' */
	u32	Tag;			/* Same as original command */
	__le32	Residue;		/* Amount not transferred */
	u8	Status;			/* See below */
};

#define USB_BULK_CS_WRAP_LEN	13
#define USB_BULK_CS_SIG		0x53425355	/* Spells out 'USBS' */
#define USB_STATUS_PASS		0
#define USB_STATUS_FAIL		1
#define USB_STATUS_PHASE_ERROR	2

/* Bulk-only class specific requests */
#define USB_BULK_RESET_REQUEST		0xff
#define USB_BULK_GET_MAX_LUN_REQUEST	0xfe

/* Length of a SCSI Command Data Block */
#define MAX_COMMAND_SIZE	16

/* SCSI commands that we recognize */
#define SC_FORMAT_UNIT			0x04
#define SC_INQUIRY			0x12
#define SC_MODE_SELECT_6		0x15
#define SC_MODE_SELECT_10		0x55
#define SC_MODE_SENSE_6			0x1a
#define SC_MODE_SENSE_10		0x5a
#define SC_PREVENT_ALLOW_MEDIUM_REMOVAL	0x1e
#define SC_READ_6			0x08
#define SC_READ_10			0x28
#define SC_READ_12			0xa8
#define SC_READ_CAPACITY		0x25
#define SC_READ_FORMAT_CAPACITIES	0x23
#define SC_RELEASE			0x17
#define SC_REQUEST_SENSE		0x03
#define SC_RESERVE			0x16
#define SC_SEND_DIAGNOSTIC		0x1d
#define SC_START_STOP_UNIT		0x1b
#define SC_SYNCHRONIZE_CACHE		0x35
#define SC_TEST_UNIT_READY		0x00
#define SC_VERIFY			0x2f
#define SC_WRITE_6			0x0a
#define SC_WRITE_10			0x2a
#define SC_WRITE_12			0xaa

/* SCSI Sense Key/Additional Sense Code/ASC Qualifier values */
#define SS_NO_SENSE				0
#define SS_COMMUNICATION_FAILURE		0x040800
#define SS_INVALID_COMMAND			0x052000
#define SS_INVALID_FIELD_IN_CDB			0x052400
#define SS_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE	0x052100
#define SS_LOGICAL_UNIT_NOT_SUPPORTED		0x052500
#define SS_MEDIUM_NOT_PRESENT			0x023a00
#define SS_MEDIUM_REMOVAL_PREVENTED		0x055302
#define SS_NOT_READY_TO_READY_TRANSITION	0x062800
#define SS_RESET_OCCURRED			0x062900
#define SS_SAVING_PARAMETERS_NOT_SUPPORTED	0x053900
#define SS_UNRECOVERED_READ_ERROR		0x031100
#define SS_WRITE_ERROR				0x030c02
#define SS_WRITE_PROTECTED			0x072700

#define SK(x)		((u8) ((x) >> 16))	/* Sense Key byte, etc. */
#define ASC(x)		((u8) ((x) >> 8))
#define ASCQ(x)		((u8) (x))


/*-------------------------------------------------------------------------*/

struct lun {
	struct file	*filp;
	loff_t		file_length;
	loff_t		num_sectors;
	unsigned int unflushed_bytes;

	unsigned int	ro : 1;
	unsigned int	prevent_medium_removal : 1;
	unsigned int	registered : 1;
	unsigned int	info_valid : 1;

	u32		sense_data;
	u32		sense_data_info;
	u32		unit_attention_data;

	struct device	dev;
};

#define backing_file_is_open(curlun)	((curlun)->filp != NULL)


static struct lun *dev_to_lun(struct device *dev)
{
	return container_of(dev, struct lun, dev);
}

/* Big enough to hold our biggest descriptor */
#define EP0_BUFSIZE	256

/* Number of buffers we will use.  2 is enough for double-buffering */
#define NUM_BUFFERS	2

enum fsg_buffer_state {
	BUF_STATE_EMPTY = 0,
	BUF_STATE_FULL,
	BUF_STATE_BUSY
};

struct fsg_buffhd {
	void				*buf;
	enum fsg_buffer_state		state;
	struct fsg_buffhd		*next;

	/* The NetChip 2280 is faster, and handles some protocol faults
	 * better, if we don't submit any short bulk-out read requests.
	 * So we will record the intended request length here. */
	unsigned int			bulk_out_intended_length;

	struct usb_request		*inreq;
	int				inreq_busy;
	struct usb_request		*outreq;
	int				outreq_busy;
};

enum fsg_state {
	/* This one isn't used anywhere */
	FSG_STATE_COMMAND_PHASE = -10,

	FSG_STATE_DATA_PHASE,
	FSG_STATE_STATUS_PHASE,

	FSG_STATE_IDLE = 0,
	FSG_STATE_ABORT_BULK_OUT,
	FSG_STATE_RESET,
	FSG_STATE_CONFIG_CHANGE,
	FSG_STATE_EXIT,
	FSG_STATE_TERMINATED
};

enum data_direction {
	DATA_DIR_UNKNOWN = 0,
	DATA_DIR_FROM_HOST,
	DATA_DIR_TO_HOST,
	DATA_DIR_NONE
};

struct fsg_dev {
	struct usb_function function;
	struct usb_composite_dev *cdev;

	/* optional "usb_mass_storage" platform device */
	struct platform_device *pdev;

	/* lock protects: state and all the req_busy's */
	spinlock_t		lock;

	/* filesem protects: backing files in use */
	struct rw_semaphore	filesem;

	/* reference counting: wait until all LUNs are released */
	struct kref		ref;

	unsigned int		bulk_out_maxpacket;
	enum fsg_state		state;		/* For exception handling */

	u8			config, new_config;

	unsigned int		running : 1;
	unsigned int		bulk_in_enabled : 1;
	unsigned int		bulk_out_enabled : 1;
	unsigned int		phase_error : 1;
	unsigned int		short_packet_received : 1;
	unsigned int		bad_lun_okay : 1;

	unsigned long		atomic_bitflags;
#define REGISTERED		0
#define CLEAR_BULK_HALTS	1
#define SUSPENDED		2

	struct usb_ep		*bulk_in;
	struct usb_ep		*bulk_out;

	struct fsg_buffhd	*next_buffhd_to_fill;
	struct fsg_buffhd	*next_buffhd_to_drain;
	struct fsg_buffhd	buffhds[NUM_BUFFERS];

	int			thread_wakeup_needed;
	struct completion	thread_notifier;
	struct task_struct	*thread_task;

	int			cmnd_size;
	u8			cmnd[MAX_COMMAND_SIZE];
	enum data_direction	data_dir;
	u32			data_size;
	u32			data_size_from_cmnd;
	u32			tag;
	unsigned int		lun;
	u32			residue;
	u32			usb_amount_left;

	unsigned int		nluns;
	struct lun		*luns;
	struct lun		*curlun;

	u32				buf_size;
	const char		*vendor;
	const char		*product;
	int				release;

	struct switch_dev sdev;

	struct wake_lock wake_lock;
};

static inline struct fsg_dev *func_to_dev(struct usb_function *f)
{
	return container_of(f, struct fsg_dev, function);
}

static int exception_in_progress(struct fsg_dev *fsg)
{
	return (fsg->state > FSG_STATE_IDLE);
}

/* Make bulk-out requests be divisible by the maxpacket size */
static void set_bulk_out_req_length(struct fsg_dev *fsg,
		struct fsg_buffhd *bh, unsigned int length)
{
	unsigned int	rem;

	bh->bulk_out_intended_length = length;
	rem = length % fsg->bulk_out_maxpacket;
	if (rem > 0)
		length += fsg->bulk_out_maxpacket - rem;
	bh->outreq->length = length;
}

static struct fsg_dev			*the_fsg;

static void	close_backing_file(struct fsg_dev *fsg, struct lun *curlun);
static void	close_all_backing_files(struct fsg_dev *fsg);
static int fsync_sub(struct lun *curlun);

/*-------------------------------------------------------------------------*/

#ifdef DUMP_MSGS

static void dump_msg(struct fsg_dev *fsg, const char *label,
		const u8 *buf, unsigned int length)
{
	if (length < 512) {
		DBG(fsg, "%s, length %u:\n", label, length);
		print_hex_dump(KERN_DEBUG, "", DUMP_PREFIX_OFFSET,
				16, 1, buf, length, 0);
	}
}

static void dump_cdb(struct fsg_dev *fsg)
{}

#else

static void dump_msg(struct fsg_dev *fsg, const char *label,
		const u8 *buf, unsigned int length)
{}

#ifdef VERBOSE_DEBUG

static void dump_cdb(struct fsg_dev *fsg)
{
	print_hex_dump(KERN_DEBUG, "SCSI CDB: ", DUMP_PREFIX_NONE,
			16, 1, fsg->cmnd, fsg->cmnd_size, 0);
}

#else

static void dump_cdb(struct fsg_dev *fsg)
{}

#endif /* VERBOSE_DEBUG */
#endif /* DUMP_MSGS */


/*-------------------------------------------------------------------------*/

/* Routines for unaligned data access */

static u16 get_be16(u8 *buf)
{
	return ((u16) buf[0] << 8) | ((u16) buf[1]);
}

static u32 get_be32(u8 *buf)
{
	return ((u32) buf[0] << 24) | ((u32) buf[1] << 16) |
			((u32) buf[2] << 8) | ((u32) buf[3]);
}

static void put_be16(u8 *buf, u16 val)
{
	buf[0] = val >> 8;
	buf[1] = val;
}

static void put_be32(u8 *buf, u32 val)
{
	buf[0] = val >> 24;
	buf[1] = val >> 16;
	buf[2] = val >> 8;
	buf[3] = val & 0xff;
}

/*-------------------------------------------------------------------------*/

/*
 * DESCRIPTORS ... most are static, but strings and (full) configuration
 * descriptors are built on demand.  Also the (static) config and interface
 * descriptors are adjusted during fsg_bind().
 */

/* There is only one interface. */

static struct usb_interface_descriptor
intf_desc = {
	.bLength =		sizeof intf_desc,
	.bDescriptorType =	USB_DT_INTERFACE,

	.bNumEndpoints =	2,		/* Adjusted during fsg_bind() */
	.bInterfaceClass =	USB_CLASS_MASS_STORAGE,
	.bInterfaceSubClass =	US_SC_SCSI,
	.bInterfaceProtocol =	US_PR_BULK,
};

/* Three full-speed endpoint descriptors: bulk-in, bulk-out,
 * and interrupt-in. */

static struct usb_endpoint_descriptor
fs_bulk_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	/* wMaxPacketSize set by autoconfiguration */
};

static struct usb_endpoint_descriptor
fs_bulk_out_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	/* wMaxPacketSize set by autoconfiguration */
};

static struct usb_descriptor_header *fs_function[] = {
	(struct usb_descriptor_header *) &intf_desc,
	(struct usb_descriptor_header *) &fs_bulk_in_desc,
	(struct usb_descriptor_header *) &fs_bulk_out_desc,
	NULL,
};
#define FS_FUNCTION_PRE_EP_ENTRIES	2


static struct usb_endpoint_descriptor
hs_bulk_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	/* bEndpointAddress copied from fs_bulk_in_desc during fsg_bind() */
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	__constant_cpu_to_le16(512),
};

static struct usb_endpoint_descriptor
hs_bulk_out_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	/* bEndpointAddress copied from fs_bulk_out_desc during fsg_bind() */
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	__constant_cpu_to_le16(512),
	.bInterval =		1,	/* NAK every 1 uframe */
};


static struct usb_descriptor_header *hs_function[] = {
	(struct usb_descriptor_header *) &intf_desc,
	(struct usb_descriptor_header *) &hs_bulk_in_desc,
	(struct usb_descriptor_header *) &hs_bulk_out_desc,
	NULL,
};

/* Maxpacket and other transfer characteristics vary by speed. */
static struct usb_endpoint_descriptor *
ep_desc(struct usb_gadget *g, struct usb_endpoint_descriptor *fs,
		struct usb_endpoint_descriptor *hs)
{
	if (gadget_is_dualspeed(g) && g->speed == USB_SPEED_HIGH)
		return hs;
	return fs;
}

/*-------------------------------------------------------------------------*/

/* These routines may be called in process context or in_irq */

/* Caller must hold fsg->lock */
static void wakeup_thread(struct fsg_dev *fsg)
{
	/* Tell the main thread that something has happened */
	fsg->thread_wakeup_needed = 1;
	if (fsg->thread_task)
		wake_up_process(fsg->thread_task);
}


static void raise_exception(struct fsg_dev *fsg, enum fsg_state new_state)
{
	unsigned long		flags;

	DBG(fsg, "raise_exception %d\n", (int)new_state);
	/* Do nothing if a higher-priority exception is already in progress.
	 * If a lower-or-equal priority exception is in progress, preempt it
	 * and notify the main thread by sending it a signal. */
	spin_lock_irqsave(&fsg->lock, flags);
	if (fsg->state <= new_state) {
		fsg->state = new_state;
		if (fsg->thread_task)
			send_sig_info(SIGUSR1, SEND_SIG_FORCED,
					fsg->thread_task);
	}
	spin_unlock_irqrestore(&fsg->lock, flags);
}


/*-------------------------------------------------------------------------*/

/* Bulk and interrupt endpoint completion handlers.
 * These always run in_irq. */

static void bulk_in_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct fsg_dev		*fsg = ep->driver_data;
	struct fsg_buffhd	*bh = req->context;
	unsigned long		flags;

	if (req->status || req->actual != req->length)
		DBG(fsg, "%s --> %d, %u/%u\n", __func__,
				req->status, req->actual, req->length);

	/* Hold the lock while we update the request and buffer states */
	smp_wmb();
	spin_lock_irqsave(&fsg->lock, flags);
	bh->inreq_busy = 0;
	bh->state = BUF_STATE_EMPTY;
	wakeup_thread(fsg);
	spin_unlock_irqrestore(&fsg->lock, flags);
}

static void bulk_out_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct fsg_dev		*fsg = ep->driver_data;
	struct fsg_buffhd	*bh = req->context;
	unsigned long		flags;

	dump_msg(fsg, "bulk-out", req->buf, req->actual);
	if (req->status || req->actual != bh->bulk_out_intended_length)
		DBG(fsg, "%s --> %d, %u/%u\n", __func__,
				req->status, req->actual,
				bh->bulk_out_intended_length);

	/* Hold the lock while we update the request and buffer states */
	smp_wmb();
	spin_lock_irqsave(&fsg->lock, flags);
	bh->outreq_busy = 0;
	bh->state = BUF_STATE_FULL;
	wakeup_thread(fsg);
	spin_unlock_irqrestore(&fsg->lock, flags);
}

static int fsg_function_setup(struct usb_function *f,
					const struct usb_ctrlrequest *ctrl)
{
	struct fsg_dev	*fsg = func_to_dev(f);
	struct usb_composite_dev *cdev = fsg->cdev;
	int			value = -EOPNOTSUPP;
	u16			w_index = le16_to_cpu(ctrl->wIndex);
	u16			w_value = le16_to_cpu(ctrl->wValue);
	u16			w_length = le16_to_cpu(ctrl->wLength);

	DBG(fsg, "fsg_function_setup\n");
	/* Handle Bulk-only class-specific requests */
	if ((ctrl->bRequestType & USB_TYPE_MASK) == USB_TYPE_CLASS) {
	DBG(fsg, "USB_TYPE_CLASS\n");
		switch (ctrl->bRequest) {
		case USB_BULK_RESET_REQUEST:
			if (ctrl->bRequestType != (USB_DIR_OUT |
					USB_TYPE_CLASS | USB_RECIP_INTERFACE))
				break;
			if (w_index != 0 || w_value != 0) {
				value = -EDOM;
				break;
			}

			/* Raise an exception to stop the current operation
			 * and reinitialize our state. */
			DBG(fsg, "bulk reset request\n");
			raise_exception(fsg, FSG_STATE_RESET);
			value = 0;
			break;

		case USB_BULK_GET_MAX_LUN_REQUEST:
			if (ctrl->bRequestType != (USB_DIR_IN |
					USB_TYPE_CLASS | USB_RECIP_INTERFACE))
				break;
			if (w_index != 0 || w_value != 0) {
				value = -EDOM;
				break;
			}
			VDBG(fsg, "get max LUN\n");
			*(u8 *)cdev->req->buf = fsg->nluns - 1;
			value = 1;
			break;
		}
	}

		/* respond with data transfer or status phase? */
		if (value >= 0) {
			int rc;
			cdev->req->zero = value < w_length;
			cdev->req->length = value;
			rc = usb_ep_queue(cdev->gadget->ep0, cdev->req, GFP_ATOMIC);
			if (rc < 0)
				printk("%s setup response queue error\n", __func__);
		}

	if (value == -EOPNOTSUPP)
		VDBG(fsg,
			"unknown class-specific control req "
			"%02x.%02x v%04x i%04x l%u\n",
			ctrl->bRequestType, ctrl->bRequest,
			le16_to_cpu(ctrl->wValue), w_index, w_length);
	return value;
}

/*-------------------------------------------------------------------------*/

/* All the following routines run in process context */


/* Use this for bulk or interrupt transfers, not ep0 */
static void start_transfer(struct fsg_dev *fsg, struct usb_ep *ep,
		struct usb_request *req, int *pbusy,
		enum fsg_buffer_state *state)
{
	int	rc;
	unsigned long		flags;

	DBG(fsg, "start_transfer req: %p, req->buf: %p\n", req, req->buf);
	if (ep == fsg->bulk_in)
		dump_msg(fsg, "bulk-in", req->buf, req->length);

	spin_lock_irqsave(&fsg->lock, flags);
	*pbusy = 1;
	*state = BUF_STATE_BUSY;
	spin_unlock_irqrestore(&fsg->lock, flags);
	rc = usb_ep_queue(ep, req, GFP_KERNEL);
	if (rc != 0) {
		*pbusy = 0;
		*state = BUF_STATE_EMPTY;

		/* We can't do much more than wait for a reset */

		/* Note: currently the net2280 driver fails zero-length
		 * submissions if DMA is enabled. */
		if (rc != -ESHUTDOWN && !(rc == -EOPNOTSUPP &&
						req->length == 0))
			WARN(fsg, "error in submission: %s --> %d\n",
				(ep == fsg->bulk_in ? "bulk-in" : "bulk-out"),
				rc);
	}
}


static int sleep_thread(struct fsg_dev *fsg)
{
	int	rc = 0;

	/* Wait until a signal arrives or we are woken up */
	for (;;) {
		try_to_freeze();
		set_current_state(TASK_INTERRUPTIBLE);
		if (signal_pending(current)) {
			rc = -EINTR;
			break;
		}
		if (fsg->thread_wakeup_needed)
			break;
		schedule();
	}
	__set_current_state(TASK_RUNNING);
	fsg->thread_wakeup_needed = 0;
	return rc;
}


/*-------------------------------------------------------------------------*/

static int do_read(struct fsg_dev *fsg)
{
	struct lun		*curlun = fsg->curlun;
	u32			lba;
	struct fsg_buffhd	*bh;
	int			rc;
	u32			amount_left;
	loff_t			file_offset, file_offset_tmp;
	unsigned int		amount;
	unsigned int		partial_page;
	ssize_t			nread;

	/* Get the starting Logical Block Address and check that it's
	 * not too big */
	if (fsg->cmnd[0] == SC_READ_6)
		lba = (fsg->cmnd[1] << 16) | get_be16(&fsg->cmnd[2]);
	else {
		lba = get_be32(&fsg->cmnd[2]);

		/* We allow DPO (Disable Page Out = don't save data in the
		 * cache) and FUA (Force Unit Access = don't read from the
		 * cache), but we don't implement them. */
		if ((fsg->cmnd[1] & ~0x18) != 0) {
			curlun->sense_data = SS_INVALID_FIELD_IN_CDB;
			return -EINVAL;
		}
	}
	if (lba >= curlun->num_sectors) {
		curlun->sense_data = SS_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE;
		return -EINVAL;
	}
	file_offset = ((loff_t) lba) << 9;

	/* Carry out the file reads */
	amount_left = fsg->data_size_from_cmnd;
	if (unlikely(amount_left == 0))
		return -EIO;		/* No default reply */

	for (;;) {

		/* Figure out how much we need to read:
		 * Try to read the remaining amount.
		 * But don't read more than the buffer size.
		 * And don't try to read past the end of the file.
		 * Finally, if we're not at a page boundary, don't read past
		 *	the next page.
		 * If this means reading 0 then we were asked to read past
		 *	the end of file. */
		amount = min((unsigned int) amount_left,
				(unsigned int)fsg->buf_size);
		amount = min((loff_t) amount,
				curlun->file_length - file_offset);
		partial_page = file_offset & (PAGE_CACHE_SIZE - 1);
		if (partial_page > 0)
			amount = min(amount, (unsigned int) PAGE_CACHE_SIZE -
					partial_page);

		/* Wait for the next buffer to become available */
		bh = fsg->next_buffhd_to_fill;
		while (bh->state != BUF_STATE_EMPTY) {
			rc = sleep_thread(fsg);
			if (rc)
				return rc;
		}

		/* If we were asked to read past the end of file,
		 * end with an empty buffer. */
		if (amount == 0) {
			curlun->sense_data =
					SS_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE;
			curlun->sense_data_info = file_offset >> 9;
			curlun->info_valid = 1;
			bh->inreq->length = 0;
			bh->state = BUF_STATE_FULL;
			break;
		}

		/* Perform the read */
		file_offset_tmp = file_offset;
		nread = vfs_read(curlun->filp,
				(char __user *) bh->buf,
				amount, &file_offset_tmp);
		VLDBG(curlun, "file read %u @ %llu -> %d\n", amount,
				(unsigned long long) file_offset,
				(int) nread);
		if (signal_pending(current))
			return -EINTR;

		if (nread < 0) {
			LDBG(curlun, "error in file read: %d\n",
					(int) nread);
			nread = 0;
		} else if (nread < amount) {
			LDBG(curlun, "partial file read: %d/%u\n",
					(int) nread, amount);
			nread -= (nread & 511);	/* Round down to a block */
		}
		file_offset  += nread;
		amount_left  -= nread;
		fsg->residue -= nread;
		bh->inreq->length = nread;
		bh->state = BUF_STATE_FULL;

		/* If an error occurred, report it and its position */
		if (nread < amount) {
			curlun->sense_data = SS_UNRECOVERED_READ_ERROR;
			curlun->sense_data_info = file_offset >> 9;
			curlun->info_valid = 1;
			break;
		}

		if (amount_left == 0)
			break;		/* No more left to read */

		/* Send this buffer and go read some more */
		start_transfer(fsg, fsg->bulk_in, bh->inreq,
				&bh->inreq_busy, &bh->state);
		fsg->next_buffhd_to_fill = bh->next;
	}

	return -EIO;		/* No default reply */
}


/*-------------------------------------------------------------------------*/

static int do_write(struct fsg_dev *fsg)
{
	struct lun		*curlun = fsg->curlun;
	u32			lba;
	struct fsg_buffhd	*bh;
	int			get_some_more;
	u32			amount_left_to_req, amount_left_to_write;
	loff_t			usb_offset, file_offset, file_offset_tmp;
	unsigned int		amount;
	unsigned int		partial_page;
	ssize_t			nwritten;
	int			rc;

	if (curlun->ro) {
		curlun->sense_data = SS_WRITE_PROTECTED;
		return -EINVAL;
	}
	curlun->filp->f_flags &= ~O_SYNC;	/* Default is not to wait */

	/* Get the starting Logical Block Address and check that it's
	 * not too big */
	if (fsg->cmnd[0] == SC_WRITE_6)
		lba = (fsg->cmnd[1] << 16) | get_be16(&fsg->cmnd[2]);
	else {
		lba = get_be32(&fsg->cmnd[2]);

		/* We allow DPO (Disable Page Out = don't save data in the
		 * cache) and FUA (Force Unit Access = write directly to the
		 * medium).  We don't implement DPO; we implement FUA by
		 * performing synchronous output. */
		if ((fsg->cmnd[1] & ~0x18) != 0) {
			curlun->sense_data = SS_INVALID_FIELD_IN_CDB;
			return -EINVAL;
		}
		if (fsg->cmnd[1] & 0x08)	/* FUA */
			curlun->filp->f_flags |= O_SYNC;
	}
	if (lba >= curlun->num_sectors) {
		curlun->sense_data = SS_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE;
		return -EINVAL;
	}

	/* Carry out the file writes */
	get_some_more = 1;
	file_offset = usb_offset = ((loff_t) lba) << 9;
	amount_left_to_req = amount_left_to_write = fsg->data_size_from_cmnd;

	while (amount_left_to_write > 0) {

		/* Queue a request for more data from the host */
		bh = fsg->next_buffhd_to_fill;
		if (bh->state == BUF_STATE_EMPTY && get_some_more) {

			/* Figure out how much we want to get:
			 * Try to get the remaining amount.
			 * But don't get more than the buffer size.
			 * And don't try to go past the end of the file.
			 * If we're not at a page boundary,
			 *	don't go past the next page.
			 * If this means getting 0, then we were asked
			 *	to write past the end of file.
			 * Finally, round down to a block boundary. */
			amount = min(amount_left_to_req, (u32)fsg->buf_size);
			amount = min((loff_t) amount, curlun->file_length -
					usb_offset);
			partial_page = usb_offset & (PAGE_CACHE_SIZE - 1);
			if (partial_page > 0)
				amount = min(amount,
	(unsigned int) PAGE_CACHE_SIZE - partial_page);

			if (amount == 0) {
				get_some_more = 0;
				curlun->sense_data =
					SS_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE;
				curlun->sense_data_info = usb_offset >> 9;
				curlun->info_valid = 1;
				continue;
			}
			amount -= (amount & 511);
			if (amount == 0) {

				/* Why were we were asked to transfer a
				 * partial block? */
				get_some_more = 0;
				continue;
			}

			/* Get the next buffer */
			usb_offset += amount;
			fsg->usb_amount_left -= amount;
			amount_left_to_req -= amount;
			if (amount_left_to_req == 0)
				get_some_more = 0;

			/* amount is always divisible by 512, hence by
			 * the bulk-out maxpacket size */
			bh->outreq->length = bh->bulk_out_intended_length =
					amount;
			start_transfer(fsg, fsg->bulk_out, bh->outreq,
					&bh->outreq_busy, &bh->state);
			fsg->next_buffhd_to_fill = bh->next;
			continue;
		}

		/* Write the received data to the backing file */
		bh = fsg->next_buffhd_to_drain;
		if (bh->state == BUF_STATE_EMPTY && !get_some_more)
			break;			/* We stopped early */
		if (bh->state == BUF_STATE_FULL) {
			smp_rmb();
			fsg->next_buffhd_to_drain = bh->next;
			bh->state = BUF_STATE_EMPTY;

			/* Did something go wrong with the transfer? */
			if (bh->outreq->status != 0) {
				curlun->sense_data = SS_COMMUNICATION_FAILURE;
				curlun->sense_data_info = file_offset >> 9;
				curlun->info_valid = 1;
				break;
			}

			amount = bh->outreq->actual;
			if (curlun->file_length - file_offset < amount) {
				LERROR(curlun,
	"write %u @ %llu beyond end %llu\n",
	amount, (unsigned long long) file_offset,
	(unsigned long long) curlun->file_length);
				amount = curlun->file_length - file_offset;
			}

			/* Perform the write */
			file_offset_tmp = file_offset;
			nwritten = vfs_write(curlun->filp,
					(char __user *) bh->buf,
					amount, &file_offset_tmp);
			VLDBG(curlun, "file write %u @ %llu -> %d\n", amount,
					(unsigned long long) file_offset,
					(int) nwritten);
			if (signal_pending(current))
				return -EINTR;		/* Interrupted! */

			if (nwritten < 0) {
				LDBG(curlun, "error in file write: %d\n",
						(int) nwritten);
				nwritten = 0;
			} else if (nwritten < amount) {
				LDBG(curlun, "partial file write: %d/%u\n",
						(int) nwritten, amount);
				nwritten -= (nwritten & 511);
						/* Round down to a block */
			}
			file_offset += nwritten;
			amount_left_to_write -= nwritten;
			fsg->residue -= nwritten;

#ifdef MAX_UNFLUSHED_BYTES
			curlun->unflushed_bytes += nwritten;
			if (curlun->unflushed_bytes >= MAX_UNFLUSHED_BYTES) {
				fsync_sub(curlun);
				curlun->unflushed_bytes = 0;
			}
#endif
			/* If an error occurred, report it and its position */
			if (nwritten < amount) {
				curlun->sense_data = SS_WRITE_ERROR;
				curlun->sense_data_info = file_offset >> 9;
				curlun->info_valid = 1;
				break;
			}

			/* Did the host decide to stop early? */
			if (bh->outreq->actual != bh->outreq->length) {
				fsg->short_packet_received = 1;
				break;
			}
			continue;
		}

		/* Wait for something to happen */
		rc = sleep_thread(fsg);
		if (rc)
			return rc;
	}

	return -EIO;		/* No default reply */
}


/*-------------------------------------------------------------------------*/

/* Sync the file data, don't bother with the metadata.
 * The caller must own fsg->filesem.
 * This code was copied from fs/buffer.c:sys_fdatasync(). */
static int fsync_sub(struct lun *curlun)
{
	struct file	*filp = curlun->filp;
	struct inode	*inode;
	int		rc, err;

	if (curlun->ro || !filp)
		return 0;
	if (!filp->f_op->fsync)
		return -EINVAL;

	inode = filp->f_path.dentry->d_inode;
	mutex_lock(&inode->i_mutex);
	rc = filemap_fdatawrite(inode->i_mapping);
	err = filp->f_op->fsync(filp, 1);
	if (!rc)
		rc = err;
	err = filemap_fdatawait(inode->i_mapping);
	if (!rc)
		rc = err;
	mutex_unlock(&inode->i_mutex);
	VLDBG(curlun, "fdatasync -> %d\n", rc);
	return rc;
}

static void fsync_all(struct fsg_dev *fsg)
{
	int	i;

	for (i = 0; i < fsg->nluns; ++i)
		fsync_sub(&fsg->luns[i]);
}

static int do_synchronize_cache(struct fsg_dev *fsg)
{
	struct lun	*curlun = fsg->curlun;
	int		rc;

	/* We ignore the requested LBA and write out all file's
	 * dirty data buffers. */
	rc = fsync_sub(curlun);
	if (rc)
		curlun->sense_data = SS_WRITE_ERROR;
	return 0;
}


/*-------------------------------------------------------------------------*/

static void invalidate_sub(struct lun *curlun)
{
	struct file	*filp = curlun->filp;
	struct inode	*inode = filp->f_path.dentry->d_inode;
	unsigned long	rc;

	rc = invalidate_mapping_pages(inode->i_mapping, 0, -1);
	VLDBG(curlun, "invalidate_inode_pages -> %ld\n", rc);
}

static int do_verify(struct fsg_dev *fsg)
{
	struct lun		*curlun = fsg->curlun;
	u32			lba;
	u32			verification_length;
	struct fsg_buffhd	*bh = fsg->next_buffhd_to_fill;
	loff_t			file_offset, file_offset_tmp;
	u32			amount_left;
	unsigned int		amount;
	ssize_t			nread;

	/* Get the starting Logical Block Address and check that it's
	 * not too big */
	lba = get_be32(&fsg->cmnd[2]);
	if (lba >= curlun->num_sectors) {
		curlun->sense_data = SS_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE;
		return -EINVAL;
	}

	/* We allow DPO (Disable Page Out = don't save data in the
	 * cache) but we don't implement it. */
	if ((fsg->cmnd[1] & ~0x10) != 0) {
		curlun->sense_data = SS_INVALID_FIELD_IN_CDB;
		return -EINVAL;
	}

	verification_length = get_be16(&fsg->cmnd[7]);
	if (unlikely(verification_length == 0))
		return -EIO;		/* No default reply */

	/* Prepare to carry out the file verify */
	amount_left = verification_length << 9;
	file_offset = ((loff_t) lba) << 9;

	/* Write out all the dirty buffers before invalidating them */
	fsync_sub(curlun);
	if (signal_pending(current))
		return -EINTR;

	invalidate_sub(curlun);
	if (signal_pending(current))
		return -EINTR;

	/* Just try to read the requested blocks */
	while (amount_left > 0) {

		/* Figure out how much we need to read:
		 * Try to read the remaining amount, but not more than
		 * the buffer size.
		 * And don't try to read past the end of the file.
		 * If this means reading 0 then we were asked to read
		 * past the end of file. */
		amount = min((unsigned int) amount_left,
				(unsigned int)fsg->buf_size);
		amount = min((loff_t) amount,
				curlun->file_length - file_offset);
		if (amount == 0) {
			curlun->sense_data =
					SS_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE;
			curlun->sense_data_info = file_offset >> 9;
			curlun->info_valid = 1;
			break;
		}

		/* Perform the read */
		file_offset_tmp = file_offset;
		nread = vfs_read(curlun->filp,
				(char __user *) bh->buf,
				amount, &file_offset_tmp);
		VLDBG(curlun, "file read %u @ %llu -> %d\n", amount,
				(unsigned long long) file_offset,
				(int) nread);
		if (signal_pending(current))
			return -EINTR;

		if (nread < 0) {
			LDBG(curlun, "error in file verify: %d\n",
					(int) nread);
			nread = 0;
		} else if (nread < amount) {
			LDBG(curlun, "partial file verify: %d/%u\n",
					(int) nread, amount);
			nread -= (nread & 511);	/* Round down to a sector */
		}
		if (nread == 0) {
			curlun->sense_data = SS_UNRECOVERED_READ_ERROR;
			curlun->sense_data_info = file_offset >> 9;
			curlun->info_valid = 1;
			break;
		}
		file_offset += nread;
		amount_left -= nread;
	}
	return 0;
}


/*-------------------------------------------------------------------------*/

static int do_inquiry(struct fsg_dev *fsg, struct fsg_buffhd *bh)
{
	u8	*buf = (u8 *) bh->buf;

	if (!fsg->curlun) {		/* Unsupported LUNs are okay */
		fsg->bad_lun_okay = 1;
		memset(buf, 0, 36);
		buf[0] = 0x7f;		/* Unsupported, no device-type */
		return 36;
	}

	memset(buf, 0, 8);	/* Non-removable, direct-access device */

	buf[1] = 0x80;	/* set removable bit */
	buf[2] = 2;		/* ANSI SCSI level 2 */
	buf[3] = 2;		/* SCSI-2 INQUIRY data format */
	buf[4] = 31;		/* Additional length */
				/* No special options */
	sprintf(buf + 8, "%-8s%-16s%04x", fsg->vendor,
			fsg->product, fsg->release);
	return 36;
}


static int do_request_sense(struct fsg_dev *fsg, struct fsg_buffhd *bh)
{
	struct lun	*curlun = fsg->curlun;
	u8		*buf = (u8 *) bh->buf;
	u32		sd, sdinfo;
	int		valid;

	/*
	 * From the SCSI-2 spec., section 7.9 (Unit attention condition):
	 *
	 * If a REQUEST SENSE command is received from an initiator
	 * with a pending unit attention condition (before the target
	 * generates the contingent allegiance condition), then the
	 * target shall either:
	 *   a) report any pending sense data and preserve the unit
	 *	attention condition on the logical unit, or,
	 *   b) report the unit attention condition, may discard any
	 *	pending sense data, and clear the unit attention
	 *	condition on the logical unit for that initiator.
	 *
	 * FSG normally uses option a); enable this code to use option b).
	 */
#if 0
	if (curlun && curlun->unit_attention_data != SS_NO_SENSE) {
		curlun->sense_data = curlun->unit_attention_data;
		curlun->unit_attention_data = SS_NO_SENSE;
	}
#endif

	if (!curlun) {		/* Unsupported LUNs are okay */
		fsg->bad_lun_okay = 1;
		sd = SS_LOGICAL_UNIT_NOT_SUPPORTED;
		sdinfo = 0;
		valid = 0;
	} else {
		sd = curlun->sense_data;
		sdinfo = curlun->sense_data_info;
		valid = curlun->info_valid << 7;
		curlun->sense_data = SS_NO_SENSE;
		curlun->sense_data_info = 0;
		curlun->info_valid = 0;
	}

	memset(buf, 0, 18);
	buf[0] = valid | 0x70;			/* Valid, current error */
	buf[2] = SK(sd);
	put_be32(&buf[3], sdinfo);		/* Sense information */
	buf[7] = 18 - 8;			/* Additional sense length */
	buf[12] = ASC(sd);
	buf[13] = ASCQ(sd);
	return 18;
}


static int do_read_capacity(struct fsg_dev *fsg, struct fsg_buffhd *bh)
{
	struct lun	*curlun = fsg->curlun;
	u32		lba = get_be32(&fsg->cmnd[2]);
	int		pmi = fsg->cmnd[8];
	u8		*buf = (u8 *) bh->buf;

	/* Check the PMI and LBA fields */
	if (pmi > 1 || (pmi == 0 && lba != 0)) {
		curlun->sense_data = SS_INVALID_FIELD_IN_CDB;
		return -EINVAL;
	}

	put_be32(&buf[0], curlun->num_sectors - 1);	/* Max logical block */
	put_be32(&buf[4], 512);				/* Block length */
	return 8;
}


static int do_mode_sense(struct fsg_dev *fsg, struct fsg_buffhd *bh)
{
	struct lun	*curlun = fsg->curlun;
	int		mscmnd = fsg->cmnd[0];
	u8		*buf = (u8 *) bh->buf;
	u8		*buf0 = buf;
	int		pc, page_code;
	int		changeable_values, all_pages;
	int		valid_page = 0;
	int		len, limit;

	if ((fsg->cmnd[1] & ~0x08) != 0) {		/* Mask away DBD */
		curlun->sense_data = SS_INVALID_FIELD_IN_CDB;
		return -EINVAL;
	}
	pc = fsg->cmnd[2] >> 6;
	page_code = fsg->cmnd[2] & 0x3f;
	if (pc == 3) {
		curlun->sense_data = SS_SAVING_PARAMETERS_NOT_SUPPORTED;
		return -EINVAL;
	}
	changeable_values = (pc == 1);
	all_pages = (page_code == 0x3f);

	/* Write the mode parameter header.  Fixed values are: default
	 * medium type, no cache control (DPOFUA), and no block descriptors.
	 * The only variable value is the WriteProtect bit.  We will fill in
	 * the mode data length later. */
	memset(buf, 0, 8);
	if (mscmnd == SC_MODE_SENSE_6) {
		buf[2] = (curlun->ro ? 0x80 : 0x00);		/* WP, DPOFUA */
		buf += 4;
		limit = 255;
	} else {			/* SC_MODE_SENSE_10 */
		buf[3] = (curlun->ro ? 0x80 : 0x00);		/* WP, DPOFUA */
		buf += 8;
		limit = 65535;
	}

	/* No block descriptors */

	/* Disabled to workaround USB reset problems with a Vista host.
	 */
#if 0
	/* The mode pages, in numerical order.  The only page we support
	 * is the Caching page. */
	if (page_code == 0x08 || all_pages) {
		valid_page = 1;
		buf[0] = 0x08;		/* Page code */
		buf[1] = 10;		/* Page length */
		memset(buf+2, 0, 10);	/* None of the fields are changeable */

		if (!changeable_values) {
			buf[2] = 0x04;	/* Write cache enable, */
					/* Read cache not disabled */
					/* No cache retention priorities */
			put_be16(&buf[4], 0xffff);  /* Don't disable prefetch */
					/* Minimum prefetch = 0 */
			put_be16(&buf[8], 0xffff);  /* Maximum prefetch */
			/* Maximum prefetch ceiling */
			put_be16(&buf[10], 0xffff);
		}
		buf += 12;
	}
#else
	valid_page = 1;
#endif

	/* Check that a valid page was requested and the mode data length
	 * isn't too long. */
	len = buf - buf0;
	if (!valid_page || len > limit) {
		curlun->sense_data = SS_INVALID_FIELD_IN_CDB;
		return -EINVAL;
	}

	/*  Store the mode data length */
	if (mscmnd == SC_MODE_SENSE_6)
		buf0[0] = len - 1;
	else
		put_be16(buf0, len - 2);
	return len;
}

static int do_start_stop(struct fsg_dev *fsg)
{
	struct lun	*curlun = fsg->curlun;
	int		loej, start;

	/* int immed = fsg->cmnd[1] & 0x01; */
	loej = fsg->cmnd[4] & 0x02;
	start = fsg->cmnd[4] & 0x01;

	if (loej) {
		/* eject request from the host */
		if (backing_file_is_open(curlun)) {
			close_backing_file(fsg, curlun);
			curlun->unit_attention_data = SS_MEDIUM_NOT_PRESENT;
		}
	}

	return 0;
}

static int do_prevent_allow(struct fsg_dev *fsg)
{
	struct lun	*curlun = fsg->curlun;
	int		prevent;

	prevent = fsg->cmnd[4] & 0x01;
	if ((fsg->cmnd[4] & ~0x01) != 0) {		/* Mask away Prevent */
		curlun->sense_data = SS_INVALID_FIELD_IN_CDB;
		return -EINVAL;
	}

	if (curlun->prevent_medium_removal && !prevent)
		fsync_sub(curlun);
	curlun->prevent_medium_removal = prevent;
	return 0;
}


static int do_read_format_capacities(struct fsg_dev *fsg,
			struct fsg_buffhd *bh)
{
	struct lun	*curlun = fsg->curlun;
	u8		*buf = (u8 *) bh->buf;

	buf[0] = buf[1] = buf[2] = 0;
	buf[3] = 8;	/* Only the Current/Maximum Capacity Descriptor */
	buf += 4;

	put_be32(&buf[0], curlun->num_sectors);	/* Number of blocks */
	put_be32(&buf[4], 512);				/* Block length */
	buf[4] = 0x02;					/* Current capacity */
	return 12;
}


static int do_mode_select(struct fsg_dev *fsg, struct fsg_buffhd *bh)
{
	struct lun	*curlun = fsg->curlun;

	/* We don't support MODE SELECT */
	curlun->sense_data = SS_INVALID_COMMAND;
	return -EINVAL;
}


/*-------------------------------------------------------------------------*/
#if 0
static int write_zero(struct fsg_dev *fsg)
{
	struct fsg_buffhd	*bh;
	int			rc;

	DBG(fsg, "write_zero\n");
	/* Wait for the next buffer to become available */
	bh = fsg->next_buffhd_to_fill;
	while (bh->state != BUF_STATE_EMPTY) {
		rc = sleep_thread(fsg);
		if (rc)
			return rc;
	}

	bh->inreq->length = 0;
	start_transfer(fsg, fsg->bulk_in, bh->inreq,
			&bh->inreq_busy, &bh->state);

	fsg->next_buffhd_to_fill = bh->next;
	return 0;
}
#endif

static int throw_away_data(struct fsg_dev *fsg)
{
	struct fsg_buffhd	*bh;
	u32			amount;
	int			rc;

	DBG(fsg, "throw_away_data\n");
	while ((bh = fsg->next_buffhd_to_drain)->state != BUF_STATE_EMPTY ||
			fsg->usb_amount_left > 0) {

		/* Throw away the data in a filled buffer */
		if (bh->state == BUF_STATE_FULL) {
			smp_rmb();
			bh->state = BUF_STATE_EMPTY;
			fsg->next_buffhd_to_drain = bh->next;

			/* A short packet or an error ends everything */
			if (bh->outreq->actual != bh->outreq->length ||
					bh->outreq->status != 0) {
				raise_exception(fsg, FSG_STATE_ABORT_BULK_OUT);
				return -EINTR;
			}
			continue;
		}

		/* Try to submit another request if we need one */
		bh = fsg->next_buffhd_to_fill;
		if (bh->state == BUF_STATE_EMPTY && fsg->usb_amount_left > 0) {
			amount = min(fsg->usb_amount_left, (u32) fsg->buf_size);

			/* amount is always divisible by 512, hence by
			 * the bulk-out maxpacket size */
			bh->outreq->length = bh->bulk_out_intended_length =
					amount;
			start_transfer(fsg, fsg->bulk_out, bh->outreq,
					&bh->outreq_busy, &bh->state);
			fsg->next_buffhd_to_fill = bh->next;
			fsg->usb_amount_left -= amount;
			continue;
		}

		/* Otherwise wait for something to happen */
		rc = sleep_thread(fsg);
		if (rc)
			return rc;
	}
	return 0;
}


static int finish_reply(struct fsg_dev *fsg)
{
	struct fsg_buffhd	*bh = fsg->next_buffhd_to_fill;
	int			rc = 0;

	switch (fsg->data_dir) {
	case DATA_DIR_NONE:
		break;			/* Nothing to send */

	case DATA_DIR_UNKNOWN:
		rc = -EINVAL;
		break;

	/* All but the last buffer of data must have already been sent */
	case DATA_DIR_TO_HOST:
		if (fsg->data_size == 0)
			;		/* Nothing to send */

		/* If there's no residue, simply send the last buffer */
		else if (fsg->residue == 0) {
			start_transfer(fsg, fsg->bulk_in, bh->inreq,
					&bh->inreq_busy, &bh->state);
			fsg->next_buffhd_to_fill = bh->next;
		} else {
			start_transfer(fsg, fsg->bulk_in, bh->inreq,
					&bh->inreq_busy, &bh->state);
			fsg->next_buffhd_to_fill = bh->next;
#if 0
			/* this is unnecessary, and was causing problems with MacOS */
			if (bh->inreq->length > 0)
				write_zero(fsg);
#endif
		}
		break;

	/* We have processed all we want from the data the host has sent.
	 * There may still be outstanding bulk-out requests. */
	case DATA_DIR_FROM_HOST:
		if (fsg->residue == 0)
			;		/* Nothing to receive */

		/* Did the host stop sending unexpectedly early? */
		else if (fsg->short_packet_received) {
			raise_exception(fsg, FSG_STATE_ABORT_BULK_OUT);
			rc = -EINTR;
		}

		/* We haven't processed all the incoming data.  Even though
		 * we may be allowed to stall, doing so would cause a race.
		 * The controller may already have ACK'ed all the remaining
		 * bulk-out packets, in which case the host wouldn't see a
		 * STALL.  Not realizing the endpoint was halted, it wouldn't
		 * clear the halt -- leading to problems later on. */
#if 0
		fsg_set_halt(fsg, fsg->bulk_out);
		raise_exception(fsg, FSG_STATE_ABORT_BULK_OUT);
		rc = -EINTR;
#endif

		/* We can't stall.  Read in the excess data and throw it
		 * all away. */
		else
			rc = throw_away_data(fsg);
		break;
	}
	return rc;
}


static int send_status(struct fsg_dev *fsg)
{
	struct lun		*curlun = fsg->curlun;
	struct fsg_buffhd	*bh;
	int			rc;
	u8			status = USB_STATUS_PASS;
	u32			sd, sdinfo = 0;
	struct bulk_cs_wrap	*csw;

	DBG(fsg, "send_status\n");
	/* Wait for the next buffer to become available */
	bh = fsg->next_buffhd_to_fill;
	while (bh->state != BUF_STATE_EMPTY) {
		rc = sleep_thread(fsg);
		if (rc)
			return rc;
	}

	if (curlun) {
		sd = curlun->sense_data;
		sdinfo = curlun->sense_data_info;
	} else if (fsg->bad_lun_okay)
		sd = SS_NO_SENSE;
	else
		sd = SS_LOGICAL_UNIT_NOT_SUPPORTED;

	if (fsg->phase_error) {
		DBG(fsg, "sending phase-error status\n");
		status = USB_STATUS_PHASE_ERROR;
		sd = SS_INVALID_COMMAND;
	} else if (sd != SS_NO_SENSE) {
		DBG(fsg, "sending command-failure status\n");
		status = USB_STATUS_FAIL;
		VDBG(fsg, "  sense data: SK x%02x, ASC x%02x, ASCQ x%02x;"
				"  info x%x\n",
				SK(sd), ASC(sd), ASCQ(sd), sdinfo);
	}

	csw = bh->buf;

	/* Store and send the Bulk-only CSW */
	csw->Signature = __constant_cpu_to_le32(USB_BULK_CS_SIG);
	csw->Tag = fsg->tag;
	csw->Residue = cpu_to_le32(fsg->residue);
	csw->Status = status;

	bh->inreq->length = USB_BULK_CS_WRAP_LEN;
	start_transfer(fsg, fsg->bulk_in, bh->inreq,
			&bh->inreq_busy, &bh->state);

	fsg->next_buffhd_to_fill = bh->next;
	return 0;
}


/*-------------------------------------------------------------------------*/

/* Check whether the command is properly formed and whether its data size
 * and direction agree with the values we already have. */
static int check_command(struct fsg_dev *fsg, int cmnd_size,
		enum data_direction data_dir, unsigned int mask,
		int needs_medium, const char *name)
{
	int			i;
	int			lun = fsg->cmnd[1] >> 5;
	static const char	dirletter[4] = {'u', 'o', 'i', 'n'};
	char			hdlen[20];
	struct lun		*curlun;

	hdlen[0] = 0;
	if (fsg->data_dir != DATA_DIR_UNKNOWN)
		sprintf(hdlen, ", H%c=%u", dirletter[(int) fsg->data_dir],
				fsg->data_size);
	VDBG(fsg, "SCSI command: %s;  Dc=%d, D%c=%u;  Hc=%d%s\n",
			name, cmnd_size, dirletter[(int) data_dir],
			fsg->data_size_from_cmnd, fsg->cmnd_size, hdlen);

	/* We can't reply at all until we know the correct data direction
	 * and size. */
	if (fsg->data_size_from_cmnd == 0)
		data_dir = DATA_DIR_NONE;
	if (fsg->data_dir == DATA_DIR_UNKNOWN) {	/* CB or CBI */
		fsg->data_dir = data_dir;
		fsg->data_size = fsg->data_size_from_cmnd;

	} else {					/* Bulk-only */
		if (fsg->data_size < fsg->data_size_from_cmnd) {

			/* Host data size < Device data size is a phase error.
			 * Carry out the command, but only transfer as much
			 * as we are allowed. */
			DBG(fsg, "phase error 1\n");
			fsg->data_size_from_cmnd = fsg->data_size;
			fsg->phase_error = 1;
		}
	}
	fsg->residue = fsg->usb_amount_left = fsg->data_size;

	/* Conflicting data directions is a phase error */
	if (fsg->data_dir != data_dir && fsg->data_size_from_cmnd > 0) {
		fsg->phase_error = 1;
		DBG(fsg, "phase error 2\n");
		return -EINVAL;
	}

	/* Verify the length of the command itself */
	if (cmnd_size != fsg->cmnd_size) {

		/* Special case workaround: MS-Windows issues REQUEST SENSE
		 * with cbw->Length == 12 (it should be 6). */
		if (fsg->cmnd[0] == SC_REQUEST_SENSE && fsg->cmnd_size == 12)
			cmnd_size = fsg->cmnd_size;
		else {
			fsg->phase_error = 1;
			return -EINVAL;
		}
	}

	/* Check that the LUN values are consistent */
	if (fsg->lun != lun)
		DBG(fsg, "using LUN %d from CBW, "
				"not LUN %d from CDB\n",
				fsg->lun, lun);

	/* Check the LUN */
	if (fsg->lun >= 0 && fsg->lun < fsg->nluns) {
		fsg->curlun = curlun = &fsg->luns[fsg->lun];
		if (fsg->cmnd[0] != SC_REQUEST_SENSE) {
			curlun->sense_data = SS_NO_SENSE;
			curlun->sense_data_info = 0;
			curlun->info_valid = 0;
		}
	} else {
		fsg->curlun = curlun = NULL;
		fsg->bad_lun_okay = 0;

		/* INQUIRY and REQUEST SENSE commands are explicitly allowed
		 * to use unsupported LUNs; all others may not. */
		if (fsg->cmnd[0] != SC_INQUIRY &&
				fsg->cmnd[0] != SC_REQUEST_SENSE) {
			DBG(fsg, "unsupported LUN %d\n", fsg->lun);
			return -EINVAL;
		}
	}

	/* If a unit attention condition exists, only INQUIRY and
	 * REQUEST SENSE commands are allowed; anything else must fail. */
	if (curlun && curlun->unit_attention_data != SS_NO_SENSE &&
			fsg->cmnd[0] != SC_INQUIRY &&
			fsg->cmnd[0] != SC_REQUEST_SENSE) {
		curlun->sense_data = curlun->unit_attention_data;
		curlun->unit_attention_data = SS_NO_SENSE;
		return -EINVAL;
	}

	/* Check that only command bytes listed in the mask are non-zero */
	fsg->cmnd[1] &= 0x1f;			/* Mask away the LUN */
	for (i = 1; i < cmnd_size; ++i) {
		if (fsg->cmnd[i] && !(mask & (1 << i))) {
			if (curlun)
				curlun->sense_data = SS_INVALID_FIELD_IN_CDB;
			DBG(fsg, "SS_INVALID_FIELD_IN_CDB\n");
			return -EINVAL;
		}
	}

	/* If the medium isn't mounted and the command needs to access
	 * it, return an error. */
	if (curlun && !backing_file_is_open(curlun) && needs_medium) {
		curlun->sense_data = SS_MEDIUM_NOT_PRESENT;
		DBG(fsg, "SS_MEDIUM_NOT_PRESENT\n");
		return -EINVAL;
	}

	return 0;
}


static int do_scsi_command(struct fsg_dev *fsg)
{
	struct fsg_buffhd	*bh;
	int			rc;
	int			reply = -EINVAL;
	int			i;
	static char		unknown[16];

	dump_cdb(fsg);

	/* Wait for the next buffer to become available for data or status */
	bh = fsg->next_buffhd_to_drain = fsg->next_buffhd_to_fill;
	while (bh->state != BUF_STATE_EMPTY) {
		rc = sleep_thread(fsg);
		if (rc)
			return rc;
	}
	fsg->phase_error = 0;
	fsg->short_packet_received = 0;

	down_read(&fsg->filesem);	/* We're using the backing file */
	switch (fsg->cmnd[0]) {

	case SC_INQUIRY:
		fsg->data_size_from_cmnd = fsg->cmnd[4];
		if ((reply = check_command(fsg, 6, DATA_DIR_TO_HOST,
				(1<<4), 0,
				"INQUIRY")) == 0)
			reply = do_inquiry(fsg, bh);
		break;

	case SC_MODE_SELECT_6:
		fsg->data_size_from_cmnd = fsg->cmnd[4];
		if ((reply = check_command(fsg, 6, DATA_DIR_FROM_HOST,
				(1<<1) | (1<<4), 0,
				"MODE SELECT(6)")) == 0)
			reply = do_mode_select(fsg, bh);
		break;

	case SC_MODE_SELECT_10:
		fsg->data_size_from_cmnd = get_be16(&fsg->cmnd[7]);
		if ((reply = check_command(fsg, 10, DATA_DIR_FROM_HOST,
				(1<<1) | (3<<7), 0,
				"MODE SELECT(10)")) == 0)
			reply = do_mode_select(fsg, bh);
		break;

	case SC_MODE_SENSE_6:
		fsg->data_size_from_cmnd = fsg->cmnd[4];
		if ((reply = check_command(fsg, 6, DATA_DIR_TO_HOST,
				(1<<1) | (1<<2) | (1<<4), 0,
				"MODE SENSE(6)")) == 0)
			reply = do_mode_sense(fsg, bh);
		break;

	case SC_MODE_SENSE_10:
		fsg->data_size_from_cmnd = get_be16(&fsg->cmnd[7]);
		if ((reply = check_command(fsg, 10, DATA_DIR_TO_HOST,
				(1<<1) | (1<<2) | (3<<7), 0,
				"MODE SENSE(10)")) == 0)
			reply = do_mode_sense(fsg, bh);
		break;

	case SC_PREVENT_ALLOW_MEDIUM_REMOVAL:
		fsg->data_size_from_cmnd = 0;
		if ((reply = check_command(fsg, 6, DATA_DIR_NONE,
				(1<<4), 0,
				"PREVENT-ALLOW MEDIUM REMOVAL")) == 0)
			reply = do_prevent_allow(fsg);
		break;

	case SC_READ_6:
		i = fsg->cmnd[4];
		fsg->data_size_from_cmnd = (i == 0 ? 256 : i) << 9;
		if ((reply = check_command(fsg, 6, DATA_DIR_TO_HOST,
				(7<<1) | (1<<4), 1,
				"READ(6)")) == 0)
			reply = do_read(fsg);
		break;

	case SC_READ_10:
		fsg->data_size_from_cmnd = get_be16(&fsg->cmnd[7]) << 9;
		if ((reply = check_command(fsg, 10, DATA_DIR_TO_HOST,
				(1<<1) | (0xf<<2) | (3<<7), 1,
				"READ(10)")) == 0)
			reply = do_read(fsg);
		break;

	case SC_READ_12:
		fsg->data_size_from_cmnd = get_be32(&fsg->cmnd[6]) << 9;
		if ((reply = check_command(fsg, 12, DATA_DIR_TO_HOST,
				(1<<1) | (0xf<<2) | (0xf<<6), 1,
				"READ(12)")) == 0)
			reply = do_read(fsg);
		break;

	case SC_READ_CAPACITY:
		fsg->data_size_from_cmnd = 8;
		if ((reply = check_command(fsg, 10, DATA_DIR_TO_HOST,
				(0xf<<2) | (1<<8), 1,
				"READ CAPACITY")) == 0)
			reply = do_read_capacity(fsg, bh);
		break;

	case SC_READ_FORMAT_CAPACITIES:
		fsg->data_size_from_cmnd = get_be16(&fsg->cmnd[7]);
		if ((reply = check_command(fsg, 10, DATA_DIR_TO_HOST,
				(3<<7), 1,
				"READ FORMAT CAPACITIES")) == 0)
			reply = do_read_format_capacities(fsg, bh);
		break;

	case SC_REQUEST_SENSE:
		fsg->data_size_from_cmnd = fsg->cmnd[4];
		if ((reply = check_command(fsg, 6, DATA_DIR_TO_HOST,
				(1<<4), 0,
				"REQUEST SENSE")) == 0)
			reply = do_request_sense(fsg, bh);
		break;

	case SC_START_STOP_UNIT:
		fsg->data_size_from_cmnd = 0;
		if ((reply = check_command(fsg, 6, DATA_DIR_NONE,
				(1<<1) | (1<<4), 0,
				"START-STOP UNIT")) == 0)
			reply = do_start_stop(fsg);
		break;

	case SC_SYNCHRONIZE_CACHE:
		fsg->data_size_from_cmnd = 0;
		if ((reply = check_command(fsg, 10, DATA_DIR_NONE,
				(0xf<<2) | (3<<7), 1,
				"SYNCHRONIZE CACHE")) == 0)
			reply = do_synchronize_cache(fsg);
		break;

	case SC_TEST_UNIT_READY:
		fsg->data_size_from_cmnd = 0;
		reply = check_command(fsg, 6, DATA_DIR_NONE,
				0, 1,
				"TEST UNIT READY");
		break;

	/* Although optional, this command is used by MS-Windows.  We
	 * support a minimal version: BytChk must be 0. */
	case SC_VERIFY:
		fsg->data_size_from_cmnd = 0;
		if ((reply = check_command(fsg, 10, DATA_DIR_NONE,
				(1<<1) | (0xf<<2) | (3<<7), 1,
				"VERIFY")) == 0)
			reply = do_verify(fsg);
		break;

	case SC_WRITE_6:
		i = fsg->cmnd[4];
		fsg->data_size_from_cmnd = (i == 0 ? 256 : i) << 9;
		if ((reply = check_command(fsg, 6, DATA_DIR_FROM_HOST,
				(7<<1) | (1<<4), 1,
				"WRITE(6)")) == 0)
			reply = do_write(fsg);
		break;

	case SC_WRITE_10:
		fsg->data_size_from_cmnd = get_be16(&fsg->cmnd[7]) << 9;
		if ((reply = check_command(fsg, 10, DATA_DIR_FROM_HOST,
				(1<<1) | (0xf<<2) | (3<<7), 1,
				"WRITE(10)")) == 0)
			reply = do_write(fsg);
		break;

	case SC_WRITE_12:
		fsg->data_size_from_cmnd = get_be32(&fsg->cmnd[6]) << 9;
		if ((reply = check_command(fsg, 12, DATA_DIR_FROM_HOST,
				(1<<1) | (0xf<<2) | (0xf<<6), 1,
				"WRITE(12)")) == 0)
			reply = do_write(fsg);
		break;

	/* Some mandatory commands that we recognize but don't implement.
	 * They don't mean much in this setting.  It's left as an exercise
	 * for anyone interested to implement RESERVE and RELEASE in terms
	 * of Posix locks. */
	case SC_FORMAT_UNIT:
	case SC_RELEASE:
	case SC_RESERVE:
	case SC_SEND_DIAGNOSTIC:
		/* Fall through */

	default:
		fsg->data_size_from_cmnd = 0;
		sprintf(unknown, "Unknown x%02x", fsg->cmnd[0]);
		if ((reply = check_command(fsg, fsg->cmnd_size,
				DATA_DIR_UNKNOWN, 0xff, 0, unknown)) == 0) {
			fsg->curlun->sense_data = SS_INVALID_COMMAND;
			reply = -EINVAL;
		}
		break;
	}
	up_read(&fsg->filesem);

	VDBG(fsg, "reply: %d, fsg->data_size_from_cmnd: %d\n",
			reply, fsg->data_size_from_cmnd);
	if (reply == -EINTR || signal_pending(current))
		return -EINTR;

	/* Set up the single reply buffer for finish_reply() */
	if (reply == -EINVAL)
		reply = 0;		/* Error reply length */
	if (reply >= 0 && fsg->data_dir == DATA_DIR_TO_HOST) {
		reply = min((u32) reply, fsg->data_size_from_cmnd);
		bh->inreq->length = reply;
		bh->state = BUF_STATE_FULL;
		fsg->residue -= reply;
	}				/* Otherwise it's already set */

	return 0;
}


/*-------------------------------------------------------------------------*/

static int received_cbw(struct fsg_dev *fsg, struct fsg_buffhd *bh)
{
	struct usb_request	*req = bh->outreq;
	struct bulk_cb_wrap	*cbw = req->buf;

	/* Was this a real packet? */
	if (req->status)
		return -EINVAL;

	/* Is the CBW valid? */
	if (req->actual != USB_BULK_CB_WRAP_LEN ||
			cbw->Signature != __constant_cpu_to_le32(
				USB_BULK_CB_SIG)) {
		DBG(fsg, "invalid CBW: len %u sig 0x%x\n",
				req->actual,
				le32_to_cpu(cbw->Signature));
		return -EINVAL;
	}

	/* Is the CBW meaningful? */
	if (cbw->Lun >= MAX_LUNS || cbw->Flags & ~USB_BULK_IN_FLAG ||
			cbw->Length <= 0 || cbw->Length > MAX_COMMAND_SIZE) {
		DBG(fsg, "non-meaningful CBW: lun = %u, flags = 0x%x, "
				"cmdlen %u\n",
				cbw->Lun, cbw->Flags, cbw->Length);
		return -EINVAL;
	}

	/* Save the command for later */
	fsg->cmnd_size = cbw->Length;
	memcpy(fsg->cmnd, cbw->CDB, fsg->cmnd_size);
	if (cbw->Flags & USB_BULK_IN_FLAG)
		fsg->data_dir = DATA_DIR_TO_HOST;
	else
		fsg->data_dir = DATA_DIR_FROM_HOST;
	fsg->data_size = le32_to_cpu(cbw->DataTransferLength);
	if (fsg->data_size == 0)
		fsg->data_dir = DATA_DIR_NONE;
	fsg->lun = cbw->Lun;
	fsg->tag = cbw->Tag;
	return 0;
}


static int get_next_command(struct fsg_dev *fsg)
{
	struct fsg_buffhd	*bh;
	int			rc = 0;

	/* Wait for the next buffer to become available */
	bh = fsg->next_buffhd_to_fill;
	while (bh->state != BUF_STATE_EMPTY) {
		rc = sleep_thread(fsg);
		if (rc) {
			usb_ep_dequeue(fsg->bulk_out, bh->outreq);
			bh->outreq_busy = 0;
			bh->state = BUF_STATE_EMPTY;
			return rc;
		}
	}

	/* Queue a request to read a Bulk-only CBW */
	set_bulk_out_req_length(fsg, bh, USB_BULK_CB_WRAP_LEN);
	start_transfer(fsg, fsg->bulk_out, bh->outreq,
			&bh->outreq_busy, &bh->state);

	/* We will drain the buffer in software, which means we
	 * can reuse it for the next filling.  No need to advance
	 * next_buffhd_to_fill. */

	/* Wait for the CBW to arrive */
	while (bh->state != BUF_STATE_FULL) {
		rc = sleep_thread(fsg);
		if (rc) {
			usb_ep_dequeue(fsg->bulk_out, bh->outreq);
			bh->outreq_busy = 0;
			bh->state = BUF_STATE_EMPTY;
			return rc;
		}
	}
	smp_rmb();
	rc = received_cbw(fsg, bh);
	bh->state = BUF_STATE_EMPTY;

	return rc;
}


/*-------------------------------------------------------------------------*/

static int enable_endpoint(struct fsg_dev *fsg, struct usb_ep *ep,
		const struct usb_endpoint_descriptor *d)
{
	int	rc;

	DBG(fsg, "usb_ep_enable %s\n", ep->name);
	ep->driver_data = fsg;
	rc = usb_ep_enable(ep, d);
	if (rc)
		ERROR(fsg, "can't enable %s, result %d\n", ep->name, rc);
	return rc;
}

static int alloc_request(struct fsg_dev *fsg, struct usb_ep *ep,
		struct usb_request **preq)
{
	*preq = usb_ep_alloc_request(ep, GFP_ATOMIC);
	if (*preq)
		return 0;
	ERROR(fsg, "can't allocate request for %s\n", ep->name);
	return -ENOMEM;
}

/*
 * Reset interface setting and re-init endpoint state (toggle etc).
 * Call with altsetting < 0 to disable the interface.  The only other
 * available altsetting is 0, which enables the interface.
 */
static int do_set_interface(struct fsg_dev *fsg, int altsetting)
{
	struct usb_composite_dev *cdev = fsg->cdev;
	int	rc = 0;
	int	i;
	const struct usb_endpoint_descriptor	*d;

	if (fsg->running)
		DBG(fsg, "reset interface\n");
reset:
	 /* Disable the endpoints */
	if (fsg->bulk_in_enabled) {
		DBG(fsg, "usb_ep_disable %s\n", fsg->bulk_in->name);
		usb_ep_disable(fsg->bulk_in);
		fsg->bulk_in_enabled = 0;
	}
	if (fsg->bulk_out_enabled) {
		DBG(fsg, "usb_ep_disable %s\n", fsg->bulk_out->name);
		usb_ep_disable(fsg->bulk_out);
		fsg->bulk_out_enabled = 0;
	}

	/* Deallocate the requests */
	for (i = 0; i < NUM_BUFFERS; ++i) {
		struct fsg_buffhd *bh = &fsg->buffhds[i];
		if (bh->inreq) {
			usb_ep_free_request(fsg->bulk_in, bh->inreq);
			bh->inreq = NULL;
		}
		if (bh->outreq) {
			usb_ep_free_request(fsg->bulk_out, bh->outreq);
			bh->outreq = NULL;
		}
	}


	fsg->running = 0;
	if (altsetting < 0 || rc != 0)
		return rc;

	DBG(fsg, "set interface %d\n", altsetting);

	/* Enable the endpoints */
	d = ep_desc(cdev->gadget, &fs_bulk_in_desc, &hs_bulk_in_desc);
	if ((rc = enable_endpoint(fsg, fsg->bulk_in, d)) != 0)
		goto reset;
	fsg->bulk_in_enabled = 1;

	d = ep_desc(cdev->gadget, &fs_bulk_out_desc, &hs_bulk_out_desc);
	if ((rc = enable_endpoint(fsg, fsg->bulk_out, d)) != 0)
		goto reset;
	fsg->bulk_out_enabled = 1;
	fsg->bulk_out_maxpacket = le16_to_cpu(d->wMaxPacketSize);

	/* Allocate the requests */
	for (i = 0; i < NUM_BUFFERS; ++i) {
		struct fsg_buffhd	*bh = &fsg->buffhds[i];

		rc = alloc_request(fsg, fsg->bulk_in, &bh->inreq);
		if (rc != 0)
			goto reset;
		rc = alloc_request(fsg, fsg->bulk_out, &bh->outreq);
		if (rc != 0)
			goto reset;
		bh->inreq->buf = bh->outreq->buf = bh->buf;
		bh->inreq->context = bh->outreq->context = bh;
		bh->inreq->complete = bulk_in_complete;
		bh->outreq->complete = bulk_out_complete;
	}

	fsg->running = 1;
	for (i = 0; i < fsg->nluns; ++i)
		fsg->luns[i].unit_attention_data = SS_RESET_OCCURRED;

	return rc;
}

static void adjust_wake_lock(struct fsg_dev *fsg)
{
	int ums_active = 0;
	int i;
	unsigned long		flags;

	spin_lock_irqsave(&fsg->lock, flags);

	if (fsg->config) {
		for (i = 0; i < fsg->nluns; ++i) {
			if (backing_file_is_open(&fsg->luns[i]))
				ums_active = 1;
		}
	}

	if (ums_active)
		wake_lock(&fsg->wake_lock);
	else
		wake_unlock(&fsg->wake_lock);

	spin_unlock_irqrestore(&fsg->lock, flags);
}

/*
 * Change our operational configuration.  This code must agree with the code
 * that returns config descriptors, and with interface altsetting code.
 *
 * It's also responsible for power management interactions.  Some
 * configurations might not work with our current power sources.
 * For now we just assume the gadget is always self-powered.
 */
static int do_set_config(struct fsg_dev *fsg, u8 new_config)
{
	int	rc = 0;

	/* Disable the single interface */
	if (fsg->config != 0) {
		DBG(fsg, "reset config\n");
		fsg->config = 0;
		rc = do_set_interface(fsg, -1);
	}

	/* Enable the interface */
	if (new_config != 0) {
		fsg->config = new_config;
		if ((rc = do_set_interface(fsg, 0)) != 0)
			fsg->config = 0;	// Reset on errors
	}

	switch_set_state(&fsg->sdev, new_config);
	adjust_wake_lock(fsg);
	return rc;
}


/*-------------------------------------------------------------------------*/

static void handle_exception(struct fsg_dev *fsg)
{
	siginfo_t		info;
	int			sig;
	int			i;
	int			num_active;
	struct fsg_buffhd	*bh;
	enum fsg_state		old_state;
	u8			new_config;
	struct lun		*curlun;
	int			rc;
	unsigned long		flags;

	DBG(fsg, "handle_exception state: %d\n", (int)fsg->state);
	/* Clear the existing signals.  Anything but SIGUSR1 is converted
	 * into a high-priority EXIT exception. */
	for (;;) {
		sig = dequeue_signal_lock(current, &current->blocked, &info);
		if (!sig)
			break;
		if (sig != SIGUSR1) {
			if (fsg->state < FSG_STATE_EXIT)
				DBG(fsg, "Main thread exiting on signal\n");
			raise_exception(fsg, FSG_STATE_EXIT);
		}
	}

	/* Cancel all the pending transfers */
	for (i = 0; i < NUM_BUFFERS; ++i) {
		bh = &fsg->buffhds[i];
		if (bh->inreq_busy)
			usb_ep_dequeue(fsg->bulk_in, bh->inreq);
		if (bh->outreq_busy)
			usb_ep_dequeue(fsg->bulk_out, bh->outreq);
	}

	/* Wait until everything is idle */
	for (;;) {
		num_active = 0;
		for (i = 0; i < NUM_BUFFERS; ++i) {
			bh = &fsg->buffhds[i];
			num_active += bh->outreq_busy;
		}
		if (num_active == 0)
			break;
		if (sleep_thread(fsg))
			return;
	}

	/*
	* Do NOT flush the fifo after set_interface()
	* Otherwise, it results in some data being lost
	*/
	if ((fsg->state != FSG_STATE_CONFIG_CHANGE) ||
		(fsg->new_config != 1))   {
		/* Clear out the controller's fifos */
		if (fsg->bulk_in_enabled)
			usb_ep_fifo_flush(fsg->bulk_in);
		if (fsg->bulk_out_enabled)
			usb_ep_fifo_flush(fsg->bulk_out);
	}
	/* Reset the I/O buffer states and pointers, the SCSI
	 * state, and the exception.  Then invoke the handler. */
	spin_lock_irqsave(&fsg->lock, flags);

	for (i = 0; i < NUM_BUFFERS; ++i) {
		bh = &fsg->buffhds[i];
		bh->state = BUF_STATE_EMPTY;
	}
	fsg->next_buffhd_to_fill = fsg->next_buffhd_to_drain =
			&fsg->buffhds[0];

	new_config = fsg->new_config;
	old_state = fsg->state;

	if (old_state == FSG_STATE_ABORT_BULK_OUT)
		fsg->state = FSG_STATE_STATUS_PHASE;
	else {
		for (i = 0; i < fsg->nluns; ++i) {
			curlun = &fsg->luns[i];
			curlun->prevent_medium_removal = 0;
			curlun->sense_data = curlun->unit_attention_data =
					SS_NO_SENSE;
			curlun->sense_data_info = 0;
			curlun->info_valid = 0;
		}
		fsg->state = FSG_STATE_IDLE;
	}
	spin_unlock_irqrestore(&fsg->lock, flags);

	/* Carry out any extra actions required for the exception */
	switch (old_state) {
	default:
		break;

	case FSG_STATE_ABORT_BULK_OUT:
		DBG(fsg, "FSG_STATE_ABORT_BULK_OUT\n");
		spin_lock_irqsave(&fsg->lock, flags);
		if (fsg->state == FSG_STATE_STATUS_PHASE)
			fsg->state = FSG_STATE_IDLE;
		spin_unlock_irqrestore(&fsg->lock, flags);
		break;

	case FSG_STATE_RESET:
		/* really not much to do here */
		break;

	case FSG_STATE_CONFIG_CHANGE:
		rc = do_set_config(fsg, new_config);
		if (new_config == 0) {
			/* We're using the backing file */
			down_read(&fsg->filesem);
			fsync_all(fsg);
			up_read(&fsg->filesem);
		}
		break;

	case FSG_STATE_EXIT:
	case FSG_STATE_TERMINATED:
		do_set_config(fsg, 0);			/* Free resources */
		spin_lock_irqsave(&fsg->lock, flags);
		fsg->state = FSG_STATE_TERMINATED;	/* Stop the thread */
		spin_unlock_irqrestore(&fsg->lock, flags);
		break;
	}
}


/*-------------------------------------------------------------------------*/

static int fsg_main_thread(void *fsg_)
{
	struct fsg_dev		*fsg = fsg_;
	unsigned long		flags;

	/* Allow the thread to be killed by a signal, but set the signal mask
	 * to block everything but INT, TERM, KILL, and USR1. */
	allow_signal(SIGINT);
	allow_signal(SIGTERM);
	allow_signal(SIGKILL);
	allow_signal(SIGUSR1);

	/* Allow the thread to be frozen */
	set_freezable();

	/* Arrange for userspace references to be interpreted as kernel
	 * pointers.  That way we can pass a kernel pointer to a routine
	 * that expects a __user pointer and it will work okay. */
	set_fs(get_ds());

	/* The main loop */
	while (fsg->state != FSG_STATE_TERMINATED) {
		if (exception_in_progress(fsg) || signal_pending(current)) {
			handle_exception(fsg);
			continue;
		}

		if (!fsg->running) {
			sleep_thread(fsg);
			continue;
		}

		if (get_next_command(fsg))
			continue;

		spin_lock_irqsave(&fsg->lock, flags);
		if (!exception_in_progress(fsg))
			fsg->state = FSG_STATE_DATA_PHASE;
		spin_unlock_irqrestore(&fsg->lock, flags);

		if (do_scsi_command(fsg) || finish_reply(fsg))
			continue;

		spin_lock_irqsave(&fsg->lock, flags);
		if (!exception_in_progress(fsg))
			fsg->state = FSG_STATE_STATUS_PHASE;
		spin_unlock_irqrestore(&fsg->lock, flags);

		if (send_status(fsg))
			continue;

		spin_lock_irqsave(&fsg->lock, flags);
		if (!exception_in_progress(fsg))
			fsg->state = FSG_STATE_IDLE;
		spin_unlock_irqrestore(&fsg->lock, flags);
	}

	spin_lock_irqsave(&fsg->lock, flags);
	fsg->thread_task = NULL;
	spin_unlock_irqrestore(&fsg->lock, flags);

	/* In case we are exiting because of a signal, unregister the
	 * gadget driver and close the backing file. */
	if (test_and_clear_bit(REGISTERED, &fsg->atomic_bitflags))
		close_all_backing_files(fsg);

	/* Let the unbind and cleanup routines know the thread has exited */
	complete_and_exit(&fsg->thread_notifier, 0);
}


/*-------------------------------------------------------------------------*/

/* If the next two routines are called while the gadget is registered,
 * the caller must own fsg->filesem for writing. */

static int open_backing_file(struct fsg_dev *fsg, struct lun *curlun,
	const char *filename)
{
	int				ro;
	struct file			*filp = NULL;
	int				rc = -EINVAL;
	struct inode			*inode = NULL;
	loff_t				size;
	loff_t				num_sectors;

	/* R/W if we can, R/O if we must */
	ro = curlun->ro;
	if (!ro) {
		filp = filp_open(filename, O_RDWR | O_LARGEFILE, 0);
		if (-EROFS == PTR_ERR(filp))
			ro = 1;
	}
	if (ro)
		filp = filp_open(filename, O_RDONLY | O_LARGEFILE, 0);
	if (IS_ERR(filp)) {
		LINFO(curlun, "unable to open backing file: %s\n", filename);
		return PTR_ERR(filp);
	}

	if (!(filp->f_mode & FMODE_WRITE))
		ro = 1;

	if (filp->f_path.dentry)
		inode = filp->f_path.dentry->d_inode;
	if (inode && S_ISBLK(inode->i_mode)) {
		if (bdev_read_only(inode->i_bdev))
			ro = 1;
	} else if (!inode || !S_ISREG(inode->i_mode)) {
		LINFO(curlun, "invalid file type: %s\n", filename);
		goto out;
	}

	/* If we can't read the file, it's no good.
	 * If we can't write the file, use it read-only. */
	if (!filp->f_op || !(filp->f_op->read || filp->f_op->aio_read)) {
		LINFO(curlun, "file not readable: %s\n", filename);
		goto out;
	}
	if (!(filp->f_op->write || filp->f_op->aio_write))
		ro = 1;

	size = i_size_read(inode->i_mapping->host);
	if (size < 0) {
		LINFO(curlun, "unable to find file size: %s\n", filename);
		rc = (int) size;
		goto out;
	}
	num_sectors = size >> 9;	/* File size in 512-byte sectors */
	if (num_sectors == 0) {
		LINFO(curlun, "file too small: %s\n", filename);
		rc = -ETOOSMALL;
		goto out;
	}

	get_file(filp);
	curlun->ro = ro;
	curlun->filp = filp;
	curlun->file_length = size;
	curlun->unflushed_bytes = 0;
	curlun->num_sectors = num_sectors;
	LDBG(curlun, "open backing file: %s size: %lld num_sectors: %lld\n",
			filename, size, num_sectors);
	rc = 0;
	adjust_wake_lock(fsg);

out:
	filp_close(filp, current->files);
	return rc;
}


static void close_backing_file(struct fsg_dev *fsg, struct lun *curlun)
{
	if (curlun->filp) {
		int rc;

		/*
		 * XXX: San: Ugly hack here added to ensure that
		 * our pages get synced to disk.
		 * Also drop caches here just to be extra-safe
		 */
		rc = vfs_fsync(curlun->filp, 1);
		if (rc < 0)
			printk(KERN_ERR "ums: Error syncing data (%d)\n", rc);
		/* drop_pagecache and drop_slab are no longer available */
		/* drop_pagecache(); */
		/* drop_slab(); */

		LDBG(curlun, "close backing file\n");
		fput(curlun->filp);
		curlun->filp = NULL;
		adjust_wake_lock(fsg);
	}
}

static void close_all_backing_files(struct fsg_dev *fsg)
{
	int	i;

	for (i = 0; i < fsg->nluns; ++i)
		close_backing_file(fsg, &fsg->luns[i]);
}

static ssize_t show_file(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct lun	*curlun = dev_to_lun(dev);
	struct fsg_dev	*fsg = dev_get_drvdata(dev);
	char		*p;
	ssize_t		rc;

	down_read(&fsg->filesem);
	if (backing_file_is_open(curlun)) {	/* Get the complete pathname */
		p = d_path(&curlun->filp->f_path, buf, PAGE_SIZE - 1);
		if (IS_ERR(p))
			rc = PTR_ERR(p);
		else {
			rc = strlen(p);
			memmove(buf, p, rc);
			buf[rc] = '\n';		/* Add a newline */
			buf[++rc] = 0;
		}
	} else {				/* No file, return 0 bytes */
		*buf = 0;
		rc = 0;
	}
	up_read(&fsg->filesem);
	return rc;
}

static ssize_t store_file(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct lun	*curlun = dev_to_lun(dev);
	struct fsg_dev	*fsg = dev_get_drvdata(dev);
	int		rc = 0;

	DBG(fsg, "store_file: \"%s\"\n", buf);
#if 0
	/* disabled because we need to allow closing the backing file if the media was removed */
	if (curlun->prevent_medium_removal && backing_file_is_open(curlun)) {
		LDBG(curlun, "eject attempt prevented\n");
		return -EBUSY;				/* "Door is locked" */
	}
#endif

	/* Remove a trailing newline */
	if (count > 0 && buf[count-1] == '\n')
		((char *) buf)[count-1] = 0;

	/* Eject current medium */
	down_write(&fsg->filesem);
	if (backing_file_is_open(curlun)) {
		close_backing_file(fsg, curlun);
		curlun->unit_attention_data = SS_MEDIUM_NOT_PRESENT;
	}

	/* Load new medium */
	if (count > 0 && buf[0]) {
		rc = open_backing_file(fsg, curlun, buf);
		if (rc == 0)
			curlun->unit_attention_data =
					SS_NOT_READY_TO_READY_TRANSITION;
	}
	up_write(&fsg->filesem);
	return (rc < 0 ? rc : count);
}


static DEVICE_ATTR(file, 0444, show_file, store_file);

/*-------------------------------------------------------------------------*/

static void fsg_release(struct kref *ref)
{
	struct fsg_dev	*fsg = container_of(ref, struct fsg_dev, ref);

	kfree(fsg->luns);
	kfree(fsg);
}

static void lun_release(struct device *dev)
{
	struct fsg_dev	*fsg = dev_get_drvdata(dev);

	kref_put(&fsg->ref, fsg_release);
}


/*-------------------------------------------------------------------------*/

static int __init fsg_alloc(void)
{
	struct fsg_dev		*fsg;

	fsg = kzalloc(sizeof *fsg, GFP_KERNEL);
	if (!fsg)
		return -ENOMEM;
	spin_lock_init(&fsg->lock);
	init_rwsem(&fsg->filesem);
	kref_init(&fsg->ref);
	init_completion(&fsg->thread_notifier);

	the_fsg = fsg;
	return 0;
}

static ssize_t print_switch_name(struct switch_dev *sdev, char *buf)
{
	return sprintf(buf, "%s\n", DRIVER_NAME);
}

static ssize_t print_switch_state(struct switch_dev *sdev, char *buf)
{
	struct fsg_dev	*fsg = container_of(sdev, struct fsg_dev, sdev);
	return sprintf(buf, "%s\n", (fsg->config ? "online" : "offline"));
}

static void
fsg_function_unbind(struct usb_configuration *c, struct usb_function *f)
{
	struct fsg_dev	*fsg = func_to_dev(f);
	int			i;
	struct lun		*curlun;

	DBG(fsg, "fsg_function_unbind\n");
	clear_bit(REGISTERED, &fsg->atomic_bitflags);

	/* Unregister the sysfs attribute files and the LUNs */
	for (i = 0; i < fsg->nluns; ++i) {
		curlun = &fsg->luns[i];
		if (curlun->registered) {
			device_remove_file(&curlun->dev, &dev_attr_file);
			device_unregister(&curlun->dev);
			curlun->registered = 0;
		}
	}

	/* If the thread isn't already dead, tell it to exit now */
	if (fsg->state != FSG_STATE_TERMINATED) {
		raise_exception(fsg, FSG_STATE_EXIT);
		wait_for_completion(&fsg->thread_notifier);

		/* The cleanup routine waits for this completion also */
		complete(&fsg->thread_notifier);
	}

	/* Free the data buffers */
	for (i = 0; i < NUM_BUFFERS; ++i)
		kfree(fsg->buffhds[i].buf);
	switch_dev_unregister(&fsg->sdev);
}

static int
fsg_function_bind(struct usb_configuration *c, struct usb_function *f)
{
	struct usb_composite_dev *cdev = c->cdev;
	struct fsg_dev	*fsg = func_to_dev(f);
	int			rc;
	int			i;
	int			id;
	struct lun		*curlun;
	struct usb_ep		*ep;
	char			*pathbuf, *p;

	fsg->cdev = cdev;
	DBG(fsg, "fsg_function_bind\n");

	dev_attr_file.attr.mode = 0644;

	/* Find out how many LUNs there should be */
	i = fsg->nluns;
	if (i == 0)
		i = 1;
	if (i > MAX_LUNS) {
		ERROR(fsg, "invalid number of LUNs: %d\n", i);
		rc = -EINVAL;
		goto out;
	}

	/* Create the LUNs, open their backing files, and register the
	 * LUN devices in sysfs. */
	fsg->luns = kzalloc(i * sizeof(struct lun), GFP_KERNEL);
	if (!fsg->luns) {
		rc = -ENOMEM;
		goto out;
	}
	fsg->nluns = i;

	for (i = 0; i < fsg->nluns; ++i) {
		curlun = &fsg->luns[i];
		curlun->ro = 0;
		curlun->dev.release = lun_release;
		/* use "usb_mass_storage" platform device as parent if available */
		if (fsg->pdev)
			curlun->dev.parent = &fsg->pdev->dev;
		else
			curlun->dev.parent = &cdev->gadget->dev;
		dev_set_drvdata(&curlun->dev, fsg);
		dev_set_name(&curlun->dev,"lun%d", i);

		rc = device_register(&curlun->dev);
		if (rc != 0) {
			INFO(fsg, "failed to register LUN%d: %d\n", i, rc);
			goto out;
		}
		rc = device_create_file(&curlun->dev, &dev_attr_file);
		if (rc != 0) {
			ERROR(fsg, "device_create_file failed: %d\n", rc);
			device_unregister(&curlun->dev);
			goto out;
		}
		curlun->registered = 1;
		kref_get(&fsg->ref);
	}

	/* allocate interface ID(s) */
	id = usb_interface_id(c, f);
	if (id < 0)
		return id;
	intf_desc.bInterfaceNumber = id;

	ep = usb_ep_autoconfig(cdev->gadget, &fs_bulk_in_desc);
	if (!ep)
		goto autoconf_fail;
	ep->driver_data = fsg;		/* claim the endpoint */
	fsg->bulk_in = ep;

	ep = usb_ep_autoconfig(cdev->gadget, &fs_bulk_out_desc);
	if (!ep)
		goto autoconf_fail;
	ep->driver_data = fsg;		/* claim the endpoint */
	fsg->bulk_out = ep;

	rc = -ENOMEM;

	if (gadget_is_dualspeed(cdev->gadget)) {
		/* Assume endpoint addresses are the same for both speeds */
		hs_bulk_in_desc.bEndpointAddress =
				fs_bulk_in_desc.bEndpointAddress;
		hs_bulk_out_desc.bEndpointAddress =
				fs_bulk_out_desc.bEndpointAddress;

		f->hs_descriptors = hs_function;
	}

	/* Allocate the data buffers */
	for (i = 0; i < NUM_BUFFERS; ++i) {
		struct fsg_buffhd	*bh = &fsg->buffhds[i];

		/* Allocate for the bulk-in endpoint.  We assume that
		 * the buffer will also work with the bulk-out (and
		 * interrupt-in) endpoint. */
		bh->buf = kmalloc(fsg->buf_size, GFP_KERNEL);
		if (!bh->buf)
			goto out;
		bh->next = bh + 1;
	}
	fsg->buffhds[NUM_BUFFERS - 1].next = &fsg->buffhds[0];

	fsg->thread_task = kthread_create(fsg_main_thread, fsg,
			shortname);
	if (IS_ERR(fsg->thread_task)) {
		rc = PTR_ERR(fsg->thread_task);
		ERROR(fsg, "kthread_create failed: %d\n", rc);
		goto out;
	}

	INFO(fsg, "Number of LUNs=%d\n", fsg->nluns);

	pathbuf = kmalloc(PATH_MAX, GFP_KERNEL);
	for (i = 0; i < fsg->nluns; ++i) {
		curlun = &fsg->luns[i];
		if (backing_file_is_open(curlun)) {
			p = NULL;
			if (pathbuf) {
				p = d_path(&curlun->filp->f_path,
					   pathbuf, PATH_MAX);
				if (IS_ERR(p))
					p = NULL;
			}
			LINFO(curlun, "ro=%d, file: %s\n",
					curlun->ro, (p ? p : "(error)"));
		}
	}
	kfree(pathbuf);

	set_bit(REGISTERED, &fsg->atomic_bitflags);

	/* Tell the thread to start working */
	wake_up_process(fsg->thread_task);
	return 0;

autoconf_fail:
	ERROR(fsg, "unable to autoconfigure all endpoints\n");
	rc = -ENOTSUPP;

out:
	DBG(fsg, "fsg_function_bind failed: %d\n", rc);
	fsg->state = FSG_STATE_TERMINATED;	/* The thread is dead */
	fsg_function_unbind(c, f);
	close_all_backing_files(fsg);
	return rc;
}

static int fsg_function_set_alt(struct usb_function *f,
		unsigned intf, unsigned alt)
{
	struct fsg_dev	*fsg = func_to_dev(f);
	DBG(fsg, "fsg_function_set_alt intf: %d alt: %d\n", intf, alt);
	fsg->new_config = 1;
	raise_exception(fsg, FSG_STATE_CONFIG_CHANGE);
	return 0;
}

static void fsg_function_disable(struct usb_function *f)
{
	struct fsg_dev	*fsg = func_to_dev(f);
	DBG(fsg, "fsg_function_disable\n");
	fsg->new_config = 0;
	raise_exception(fsg, FSG_STATE_CONFIG_CHANGE);
}

static int __init fsg_probe(struct platform_device *pdev)
{
	struct usb_mass_storage_platform_data *pdata = pdev->dev.platform_data;
	struct fsg_dev *fsg = the_fsg;

	fsg->pdev = pdev;
	printk(KERN_INFO "fsg_probe pdata: %p\n", pdata);

	if (pdata) {
		if (pdata->vendor)
			fsg->vendor = pdata->vendor;

		if (pdata->product)
			fsg->product = pdata->product;

		if (pdata->release)
			fsg->release = pdata->release;
		fsg->nluns = pdata->nluns;
	}

	return 0;
}

static struct platform_driver fsg_platform_driver = {
	.driver = { .name = "usb_mass_storage", },
	.probe = fsg_probe,
};

int mass_storage_bind_config(struct usb_configuration *c)
{
	int		rc;
	struct fsg_dev	*fsg;

	printk(KERN_INFO "mass_storage_bind_config\n");
	rc = fsg_alloc();
	if (rc)
		return rc;
	fsg = the_fsg;

	spin_lock_init(&fsg->lock);
	init_rwsem(&fsg->filesem);
	kref_init(&fsg->ref);
	init_completion(&fsg->thread_notifier);

	the_fsg->buf_size = BULK_BUFFER_SIZE;
	the_fsg->sdev.name = DRIVER_NAME;
	the_fsg->sdev.print_name = print_switch_name;
	the_fsg->sdev.print_state = print_switch_state;
	rc = switch_dev_register(&the_fsg->sdev);
	if (rc < 0)
		goto err_switch_dev_register;

	rc = platform_driver_register(&fsg_platform_driver);
	if (rc != 0)
		goto err_platform_driver_register;

	wake_lock_init(&the_fsg->wake_lock, WAKE_LOCK_SUSPEND,
			   "usb_mass_storage");

	fsg->cdev = c->cdev;
	fsg->function.name = shortname;
	fsg->function.descriptors = fs_function;
	fsg->function.bind = fsg_function_bind;
	fsg->function.unbind = fsg_function_unbind;
	fsg->function.setup = fsg_function_setup;
	fsg->function.set_alt = fsg_function_set_alt;
	fsg->function.disable = fsg_function_disable;

	rc = usb_add_function(c, &fsg->function);
	if (rc != 0)
		goto err_usb_add_function;


	return 0;

err_usb_add_function:
	wake_lock_destroy(&the_fsg->wake_lock);
	platform_driver_unregister(&fsg_platform_driver);
err_platform_driver_register:
	switch_dev_unregister(&the_fsg->sdev);
err_switch_dev_register:
	kref_put(&the_fsg->ref, fsg_release);

	return rc;
}

static struct android_usb_function mass_storage_function = {
	.name = "usb_mass_storage",
	.bind_config = mass_storage_bind_config,
};

static int __init init(void)
{
	printk(KERN_INFO "f_mass_storage init\n");
	android_register_function(&mass_storage_function);
	return 0;
}
module_init(init);

