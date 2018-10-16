/*
 * f_mass_storage.c -- Mass Storage USB Composite Function
 *
 * Copyright (C) 2003-2008 Alan Stern
 * Copyright (C) 2009 Samsung Electronics
 *                    Author: Michal Nazarewicz <mina86@mina86.com>
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

/*
 * The Mass Storage Function acts as a USB Mass Storage device,
 * appearing to the host as a disk drive or as a CD-ROM drive.  In
 * addition to providing an example of a genuinely useful composite
 * function for a USB device, it also illustrates a technique of
 * double-buffering for increased throughput.
 *
 * For more information about MSF and in particular its module
 * parameters and sysfs interface read the
 * <Documentation/usb/mass-storage.txt> file.
 */

/*
 * MSF is configured by specifying a fsg_config structure.  It has the
 * following fields:
 *
 *	nluns		Number of LUNs function have (anywhere from 1
 *				to FSG_MAX_LUNS).
 *	luns		An array of LUN configuration values.  This
 *				should be filled for each LUN that
 *				function will include (ie. for "nluns"
 *				LUNs).  Each element of the array has
 *				the following fields:
 *	->filename	The path to the backing file for the LUN.
 *				Required if LUN is not marked as
 *				removable.
 *	->ro		Flag specifying access to the LUN shall be
 *				read-only.  This is implied if CD-ROM
 *				emulation is enabled as well as when
 *				it was impossible to open "filename"
 *				in R/W mode.
 *	->removable	Flag specifying that LUN shall be indicated as
 *				being removable.
 *	->cdrom		Flag specifying that LUN shall be reported as
 *				being a CD-ROM.
 *	->nofua		Flag specifying that FUA flag in SCSI WRITE(10,12)
 *				commands for this LUN shall be ignored.
 *
 *	vendor_name
 *	product_name
 *	release		Information used as a reply to INQUIRY
 *				request.  To use default set to NULL,
 *				NULL, 0xffff respectively.  The first
 *				field should be 8 and the second 16
 *				characters or less.
 *
 *	can_stall	Set to permit function to halt bulk endpoints.
 *				Disabled on some USB devices known not
 *				to work correctly.  You should set it
 *				to true.
 *
 * If "removable" is not set for a LUN then a backing file must be
 * specified.  If it is set, then NULL filename means the LUN's medium
 * is not loaded (an empty string as "filename" in the fsg_config
 * structure causes error).  The CD-ROM emulation includes a single
 * data track and no audio tracks; hence there need be only one
 * backing file per LUN.
 *
 * This function is heavily based on "File-backed Storage Gadget" by
 * Alan Stern which in turn is heavily based on "Gadget Zero" by David
 * Brownell.  The driver's SCSI command interface was based on the
 * "Information technology - Small Computer System Interface - 2"
 * document from X3T9.2 Project 375D, Revision 10L, 7-SEP-93,
 * available at <http://www.t10.org/ftp/t10/drafts/s2/s2-r10l.pdf>.
 * The single exception is opcode 0x23 (READ FORMAT CAPACITIES), which
 * was based on the "Universal Serial Bus Mass Storage Class UFI
 * Command Specification" document, Revision 1.0, December 14, 1998,
 * available at
 * <http://www.usb.org/developers/devclass_docs/usbmass-ufi10.pdf>.
 */

/*
 *				Driver Design
 *
 * The MSF is fairly straightforward.  There is a main kernel
 * thread that handles most of the work.  Interrupt routines field
 * callbacks from the controller driver: bulk- and interrupt-request
 * completion notifications, endpoint-0 events, and disconnect events.
 * Completion events are passed to the main thread by wakeup calls.  Many
 * ep0 requests are handled at interrupt time, but SetInterface,
 * SetConfiguration, and device reset requests are forwarded to the
 * thread in the form of "exceptions" using SIGUSR1 signals (since they
 * should interrupt any ongoing file I/O operations).
 *
 * The thread's main routine implements the standard command/data/status
 * parts of a SCSI interaction.  It and its subroutines are full of tests
 * for pending signals/exceptions -- all this polling is necessary since
 * the kernel has no setjmp/longjmp equivalents.  (Maybe this is an
 * indication that the driver really wants to be running in userspace.)
 * An important point is that so long as the thread is alive it keeps an
 * open reference to the backing file.  This will prevent unmounting
 * the backing file's underlying filesystem and could cause problems
 * during system shutdown, for example.  To prevent such problems, the
 * thread catches INT, TERM, and KILL signals and converts them into
 * an EXIT exception.
 *
 * In normal operation the main thread is started during the gadget's
 * fsg_bind() callback and stopped during fsg_unbind().  But it can
 * also exit when it receives a signal, and there's no point leaving
 * the gadget running when the thread is dead.  As of this moment, MSF
 * provides no way to deregister the gadget when thread dies -- maybe
 * a callback functions is needed.
 *
 * To provide maximum throughput, the driver uses a circular pipeline of
 * buffer heads (struct fsg_buffhd).  In principle the pipeline can be
 * arbitrarily long; in practice the benefits don't justify having more
 * than 2 stages (i.e., double buffering).  But it helps to think of the
 * pipeline as being a long one.  Each buffer head contains a bulk-in and
 * a bulk-out request pointer (since the buffer can be used for both
 * output and input -- directions always are given from the host's
 * point of view) as well as a pointer to the buffer and various state
 * variables.
 *
 * Use of the pipeline follows a simple protocol.  There is a variable
 * (fsg->next_buffhd_to_fill) that points to the next buffer head to use.
 * At any time that buffer head may still be in use from an earlier
 * request, so each buffer head has a state variable indicating whether
 * it is EMPTY, FULL, or BUSY.  Typical use involves waiting for the
 * buffer head to be EMPTY, filling the buffer either by file I/O or by
 * USB I/O (during which the buffer head is BUSY), and marking the buffer
 * head FULL when the I/O is complete.  Then the buffer will be emptied
 * (again possibly by USB I/O, during which it is marked BUSY) and
 * finally marked EMPTY again (possibly by a completion routine).
 *
 * A module parameter tells the driver to avoid stalling the bulk
 * endpoints wherever the transport specification allows.  This is
 * necessary for some UDCs like the SuperH, which cannot reliably clear a
 * halt on a bulk endpoint.  However, under certain circumstances the
 * Bulk-only specification requires a stall.  In such cases the driver
 * will halt the endpoint and set a flag indicating that it should clear
 * the halt in software during the next device reset.  Hopefully this
 * will permit everything to work correctly.  Furthermore, although the
 * specification allows the bulk-out endpoint to halt when the host sends
 * too much data, implementing this would cause an unavoidable race.
 * The driver will always use the "no-stall" approach for OUT transfers.
 *
 * One subtle point concerns sending status-stage responses for ep0
 * requests.  Some of these requests, such as device reset, can involve
 * interrupting an ongoing file I/O operation, which might take an
 * arbitrarily long time.  During that delay the host might give up on
 * the original ep0 request and issue a new one.  When that happens the
 * driver should not notify the host about completion of the original
 * request, as the host will no longer be waiting for it.  So the driver
 * assigns to each ep0 request a unique tag, and it keeps track of the
 * tag value of the request associated with a long-running exception
 * (device-reset, interface-change, or configuration-change).  When the
 * exception handler is finished, the status-stage response is submitted
 * only if the current ep0 request tag is equal to the exception request
 * tag.  Thus only the most recently received ep0 request will get a
 * status-stage response.
 *
 * Warning: This driver source file is too long.  It ought to be split up
 * into a header file plus about 3 separate .c files, to handle the details
 * of the Gadget, USB Mass Storage, and SCSI protocols.
 */


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
#include <linux/sched/signal.h>
#include <linux/limits.h>
#include <linux/rwsem.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/freezer.h>
#include <linux/module.h>
#include <linux/uaccess.h>

#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/usb/composite.h>

#include <linux/nospec.h>

#include "configfs.h"


/*------------------------------------------------------------------------*/

#define FSG_DRIVER_DESC		"Mass Storage Function"
#define FSG_DRIVER_VERSION	"2009/09/11"

static const char fsg_string_interface[] = "Mass Storage";

#include "storage_common.h"
#include "f_mass_storage.h"

/* Static strings, in UTF-8 (for simplicity we use only ASCII characters) */
static struct usb_string		fsg_strings[] = {
	{FSG_STRING_INTERFACE,		fsg_string_interface},
	{}
};

static struct usb_gadget_strings	fsg_stringtab = {
	.language	= 0x0409,		/* en-us */
	.strings	= fsg_strings,
};

static struct usb_gadget_strings *fsg_strings_array[] = {
	&fsg_stringtab,
	NULL,
};

/*-------------------------------------------------------------------------*/

struct fsg_dev;
struct fsg_common;

/* Data shared by all the FSG instances. */
struct fsg_common {
	struct usb_gadget	*gadget;
	struct usb_composite_dev *cdev;
	struct fsg_dev		*fsg, *new_fsg;
	wait_queue_head_t	io_wait;
	wait_queue_head_t	fsg_wait;

	/* filesem protects: backing files in use */
	struct rw_semaphore	filesem;

	/* lock protects: state and thread_task */
	spinlock_t		lock;

	struct usb_ep		*ep0;		/* Copy of gadget->ep0 */
	struct usb_request	*ep0req;	/* Copy of cdev->req */
	unsigned int		ep0_req_tag;

	struct fsg_buffhd	*next_buffhd_to_fill;
	struct fsg_buffhd	*next_buffhd_to_drain;
	struct fsg_buffhd	*buffhds;
	unsigned int		fsg_num_buffers;

	int			cmnd_size;
	u8			cmnd[MAX_COMMAND_SIZE];

	unsigned int		lun;
	struct fsg_lun		*luns[FSG_MAX_LUNS];
	struct fsg_lun		*curlun;

	unsigned int		bulk_out_maxpacket;
	enum fsg_state		state;		/* For exception handling */
	unsigned int		exception_req_tag;

	enum data_direction	data_dir;
	u32			data_size;
	u32			data_size_from_cmnd;
	u32			tag;
	u32			residue;
	u32			usb_amount_left;

	unsigned int		can_stall:1;
	unsigned int		free_storage_on_release:1;
	unsigned int		phase_error:1;
	unsigned int		short_packet_received:1;
	unsigned int		bad_lun_okay:1;
	unsigned int		running:1;
	unsigned int		sysfs:1;

	struct completion	thread_notifier;
	struct task_struct	*thread_task;

	/* Gadget's private data. */
	void			*private_data;

	char inquiry_string[INQUIRY_STRING_LEN];

	struct kref		ref;
};

struct fsg_dev {
	struct usb_function	function;
	struct usb_gadget	*gadget;	/* Copy of cdev->gadget */
	struct fsg_common	*common;

	u16			interface_number;

	unsigned int		bulk_in_enabled:1;
	unsigned int		bulk_out_enabled:1;

	unsigned long		atomic_bitflags;
#define IGNORE_BULK_OUT		0

	struct usb_ep		*bulk_in;
	struct usb_ep		*bulk_out;
};

static inline int __fsg_is_set(struct fsg_common *common,
			       const char *func, unsigned line)
{
	if (common->fsg)
		return 1;
	ERROR(common, "common->fsg is NULL in %s at %u\n", func, line);
	WARN_ON(1);
	return 0;
}

#define fsg_is_set(common) likely(__fsg_is_set(common, __func__, __LINE__))

static inline struct fsg_dev *fsg_from_func(struct usb_function *f)
{
	return container_of(f, struct fsg_dev, function);
}

typedef void (*fsg_routine_t)(struct fsg_dev *);

static int exception_in_progress(struct fsg_common *common)
{
	return common->state > FSG_STATE_NORMAL;
}

/* Make bulk-out requests be divisible by the maxpacket size */
static void set_bulk_out_req_length(struct fsg_common *common,
				    struct fsg_buffhd *bh, unsigned int length)
{
	unsigned int	rem;

	bh->bulk_out_intended_length = length;
	rem = length % common->bulk_out_maxpacket;
	if (rem > 0)
		length += common->bulk_out_maxpacket - rem;
	bh->outreq->length = length;
}


/*-------------------------------------------------------------------------*/

static int fsg_set_halt(struct fsg_dev *fsg, struct usb_ep *ep)
{
	const char	*name;

	if (ep == fsg->bulk_in)
		name = "bulk-in";
	else if (ep == fsg->bulk_out)
		name = "bulk-out";
	else
		name = ep->name;
	DBG(fsg, "%s set halt\n", name);
	return usb_ep_set_halt(ep);
}


/*-------------------------------------------------------------------------*/

/* These routines may be called in process context or in_irq */

static void raise_exception(struct fsg_common *common, enum fsg_state new_state)
{
	unsigned long		flags;

	/*
	 * Do nothing if a higher-priority exception is already in progress.
	 * If a lower-or-equal priority exception is in progress, preempt it
	 * and notify the main thread by sending it a signal.
	 */
	spin_lock_irqsave(&common->lock, flags);
	if (common->state <= new_state) {
		common->exception_req_tag = common->ep0_req_tag;
		common->state = new_state;
		if (common->thread_task)
			send_sig_info(SIGUSR1, SEND_SIG_FORCED,
				      common->thread_task);
	}
	spin_unlock_irqrestore(&common->lock, flags);
}


/*-------------------------------------------------------------------------*/

static int ep0_queue(struct fsg_common *common)
{
	int	rc;

	rc = usb_ep_queue(common->ep0, common->ep0req, GFP_ATOMIC);
	common->ep0->driver_data = common;
	if (rc != 0 && rc != -ESHUTDOWN) {
		/* We can't do much more than wait for a reset */
		WARNING(common, "error in submission: %s --> %d\n",
			common->ep0->name, rc);
	}
	return rc;
}


/*-------------------------------------------------------------------------*/

/* Completion handlers. These always run in_irq. */

static void bulk_in_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct fsg_common	*common = ep->driver_data;
	struct fsg_buffhd	*bh = req->context;

	if (req->status || req->actual != req->length)
		DBG(common, "%s --> %d, %u/%u\n", __func__,
		    req->status, req->actual, req->length);
	if (req->status == -ECONNRESET)		/* Request was cancelled */
		usb_ep_fifo_flush(ep);

	/* Synchronize with the smp_load_acquire() in sleep_thread() */
	smp_store_release(&bh->state, BUF_STATE_EMPTY);
	wake_up(&common->io_wait);
}

static void bulk_out_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct fsg_common	*common = ep->driver_data;
	struct fsg_buffhd	*bh = req->context;

	dump_msg(common, "bulk-out", req->buf, req->actual);
	if (req->status || req->actual != bh->bulk_out_intended_length)
		DBG(common, "%s --> %d, %u/%u\n", __func__,
		    req->status, req->actual, bh->bulk_out_intended_length);
	if (req->status == -ECONNRESET)		/* Request was cancelled */
		usb_ep_fifo_flush(ep);

	/* Synchronize with the smp_load_acquire() in sleep_thread() */
	smp_store_release(&bh->state, BUF_STATE_FULL);
	wake_up(&common->io_wait);
}

static int _fsg_common_get_max_lun(struct fsg_common *common)
{
	int i = ARRAY_SIZE(common->luns) - 1;

	while (i >= 0 && !common->luns[i])
		--i;

	return i;
}

static int fsg_setup(struct usb_function *f,
		     const struct usb_ctrlrequest *ctrl)
{
	struct fsg_dev		*fsg = fsg_from_func(f);
	struct usb_request	*req = fsg->common->ep0req;
	u16			w_index = le16_to_cpu(ctrl->wIndex);
	u16			w_value = le16_to_cpu(ctrl->wValue);
	u16			w_length = le16_to_cpu(ctrl->wLength);

	if (!fsg_is_set(fsg->common))
		return -EOPNOTSUPP;

	++fsg->common->ep0_req_tag;	/* Record arrival of a new request */
	req->context = NULL;
	req->length = 0;
	dump_msg(fsg, "ep0-setup", (u8 *) ctrl, sizeof(*ctrl));

	switch (ctrl->bRequest) {

	case US_BULK_RESET_REQUEST:
		if (ctrl->bRequestType !=
		    (USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE))
			break;
		if (w_index != fsg->interface_number || w_value != 0 ||
				w_length != 0)
			return -EDOM;

		/*
		 * Raise an exception to stop the current operation
		 * and reinitialize our state.
		 */
		DBG(fsg, "bulk reset request\n");
		raise_exception(fsg->common, FSG_STATE_PROTOCOL_RESET);
		return USB_GADGET_DELAYED_STATUS;

	case US_BULK_GET_MAX_LUN:
		if (ctrl->bRequestType !=
		    (USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE))
			break;
		if (w_index != fsg->interface_number || w_value != 0 ||
				w_length != 1)
			return -EDOM;
		VDBG(fsg, "get max LUN\n");
		*(u8 *)req->buf = _fsg_common_get_max_lun(fsg->common);

		/* Respond with data/status */
		req->length = min((u16)1, w_length);
		return ep0_queue(fsg->common);
	}

	VDBG(fsg,
	     "unknown class-specific control req %02x.%02x v%04x i%04x l%u\n",
	     ctrl->bRequestType, ctrl->bRequest,
	     le16_to_cpu(ctrl->wValue), w_index, w_length);
	return -EOPNOTSUPP;
}


/*-------------------------------------------------------------------------*/

/* All the following routines run in process context */

/* Use this for bulk or interrupt transfers, not ep0 */
static int start_transfer(struct fsg_dev *fsg, struct usb_ep *ep,
			   struct usb_request *req)
{
	int	rc;

	if (ep == fsg->bulk_in)
		dump_msg(fsg, "bulk-in", req->buf, req->length);

	rc = usb_ep_queue(ep, req, GFP_KERNEL);
	if (rc) {

		/* We can't do much more than wait for a reset */
		req->status = rc;

		/*
		 * Note: currently the net2280 driver fails zero-length
		 * submissions if DMA is enabled.
		 */
		if (rc != -ESHUTDOWN &&
				!(rc == -EOPNOTSUPP && req->length == 0))
			WARNING(fsg, "error in submission: %s --> %d\n",
					ep->name, rc);
	}
	return rc;
}

static bool start_in_transfer(struct fsg_common *common, struct fsg_buffhd *bh)
{
	if (!fsg_is_set(common))
		return false;
	bh->state = BUF_STATE_SENDING;
	if (start_transfer(common->fsg, common->fsg->bulk_in, bh->inreq))
		bh->state = BUF_STATE_EMPTY;
	return true;
}

static bool start_out_transfer(struct fsg_common *common, struct fsg_buffhd *bh)
{
	if (!fsg_is_set(common))
		return false;
	bh->state = BUF_STATE_RECEIVING;
	if (start_transfer(common->fsg, common->fsg->bulk_out, bh->outreq))
		bh->state = BUF_STATE_FULL;
	return true;
}

static int sleep_thread(struct fsg_common *common, bool can_freeze,
		struct fsg_buffhd *bh)
{
	int	rc;

	/* Wait until a signal arrives or bh is no longer busy */
	if (can_freeze)
		/*
		 * synchronize with the smp_store_release(&bh->state) in
		 * bulk_in_complete() or bulk_out_complete()
		 */
		rc = wait_event_freezable(common->io_wait,
				bh && smp_load_acquire(&bh->state) >=
					BUF_STATE_EMPTY);
	else
		rc = wait_event_interruptible(common->io_wait,
				bh && smp_load_acquire(&bh->state) >=
					BUF_STATE_EMPTY);
	return rc ? -EINTR : 0;
}


/*-------------------------------------------------------------------------*/

static int do_read(struct fsg_common *common)
{
	struct fsg_lun		*curlun = common->curlun;
	u32			lba;
	struct fsg_buffhd	*bh;
	int			rc;
	u32			amount_left;
	loff_t			file_offset, file_offset_tmp;
	unsigned int		amount;
	ssize_t			nread;

	/*
	 * Get the starting Logical Block Address and check that it's
	 * not too big.
	 */
	if (common->cmnd[0] == READ_6)
		lba = get_unaligned_be24(&common->cmnd[1]);
	else {
		lba = get_unaligned_be32(&common->cmnd[2]);

		/*
		 * We allow DPO (Disable Page Out = don't save data in the
		 * cache) and FUA (Force Unit Access = don't read from the
		 * cache), but we don't implement them.
		 */
		if ((common->cmnd[1] & ~0x18) != 0) {
			curlun->sense_data = SS_INVALID_FIELD_IN_CDB;
			return -EINVAL;
		}
	}
	if (lba >= curlun->num_sectors) {
		curlun->sense_data = SS_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE;
		return -EINVAL;
	}
	file_offset = ((loff_t) lba) << curlun->blkbits;

	/* Carry out the file reads */
	amount_left = common->data_size_from_cmnd;
	if (unlikely(amount_left == 0))
		return -EIO;		/* No default reply */

	for (;;) {
		/*
		 * Figure out how much we need to read:
		 * Try to read the remaining amount.
		 * But don't read more than the buffer size.
		 * And don't try to read past the end of the file.
		 */
		amount = min(amount_left, FSG_BUFLEN);
		amount = min((loff_t)amount,
			     curlun->file_length - file_offset);

		/* Wait for the next buffer to become available */
		bh = common->next_buffhd_to_fill;
		rc = sleep_thread(common, false, bh);
		if (rc)
			return rc;

		/*
		 * If we were asked to read past the end of file,
		 * end with an empty buffer.
		 */
		if (amount == 0) {
			curlun->sense_data =
					SS_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE;
			curlun->sense_data_info =
					file_offset >> curlun->blkbits;
			curlun->info_valid = 1;
			bh->inreq->length = 0;
			bh->state = BUF_STATE_FULL;
			break;
		}

		/* Perform the read */
		file_offset_tmp = file_offset;
		nread = kernel_read(curlun->filp, bh->buf, amount,
				&file_offset_tmp);
		VLDBG(curlun, "file read %u @ %llu -> %d\n", amount,
		      (unsigned long long)file_offset, (int)nread);
		if (signal_pending(current))
			return -EINTR;

		if (nread < 0) {
			LDBG(curlun, "error in file read: %d\n", (int)nread);
			nread = 0;
		} else if (nread < amount) {
			LDBG(curlun, "partial file read: %d/%u\n",
			     (int)nread, amount);
			nread = round_down(nread, curlun->blksize);
		}
		file_offset  += nread;
		amount_left  -= nread;
		common->residue -= nread;

		/*
		 * Except at the end of the transfer, nread will be
		 * equal to the buffer size, which is divisible by the
		 * bulk-in maxpacket size.
		 */
		bh->inreq->length = nread;
		bh->state = BUF_STATE_FULL;

		/* If an error occurred, report it and its position */
		if (nread < amount) {
			curlun->sense_data = SS_UNRECOVERED_READ_ERROR;
			curlun->sense_data_info =
					file_offset >> curlun->blkbits;
			curlun->info_valid = 1;
			break;
		}

		if (amount_left == 0)
			break;		/* No more left to read */

		/* Send this buffer and go read some more */
		bh->inreq->zero = 0;
		if (!start_in_transfer(common, bh))
			/* Don't know what to do if common->fsg is NULL */
			return -EIO;
		common->next_buffhd_to_fill = bh->next;
	}

	return -EIO;		/* No default reply */
}


/*-------------------------------------------------------------------------*/

static int do_write(struct fsg_common *common)
{
	struct fsg_lun		*curlun = common->curlun;
	u32			lba;
	struct fsg_buffhd	*bh;
	int			get_some_more;
	u32			amount_left_to_req, amount_left_to_write;
	loff_t			usb_offset, file_offset, file_offset_tmp;
	unsigned int		amount;
	ssize_t			nwritten;
	int			rc;

	if (curlun->ro) {
		curlun->sense_data = SS_WRITE_PROTECTED;
		return -EINVAL;
	}
	spin_lock(&curlun->filp->f_lock);
	curlun->filp->f_flags &= ~O_SYNC;	/* Default is not to wait */
	spin_unlock(&curlun->filp->f_lock);

	/*
	 * Get the starting Logical Block Address and check that it's
	 * not too big
	 */
	if (common->cmnd[0] == WRITE_6)
		lba = get_unaligned_be24(&common->cmnd[1]);
	else {
		lba = get_unaligned_be32(&common->cmnd[2]);

		/*
		 * We allow DPO (Disable Page Out = don't save data in the
		 * cache) and FUA (Force Unit Access = write directly to the
		 * medium).  We don't implement DPO; we implement FUA by
		 * performing synchronous output.
		 */
		if (common->cmnd[1] & ~0x18) {
			curlun->sense_data = SS_INVALID_FIELD_IN_CDB;
			return -EINVAL;
		}
		if (!curlun->nofua && (common->cmnd[1] & 0x08)) { /* FUA */
			spin_lock(&curlun->filp->f_lock);
			curlun->filp->f_flags |= O_SYNC;
			spin_unlock(&curlun->filp->f_lock);
		}
	}
	if (lba >= curlun->num_sectors) {
		curlun->sense_data = SS_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE;
		return -EINVAL;
	}

	/* Carry out the file writes */
	get_some_more = 1;
	file_offset = usb_offset = ((loff_t) lba) << curlun->blkbits;
	amount_left_to_req = common->data_size_from_cmnd;
	amount_left_to_write = common->data_size_from_cmnd;

	while (amount_left_to_write > 0) {

		/* Queue a request for more data from the host */
		bh = common->next_buffhd_to_fill;
		if (bh->state == BUF_STATE_EMPTY && get_some_more) {

			/*
			 * Figure out how much we want to get:
			 * Try to get the remaining amount,
			 * but not more than the buffer size.
			 */
			amount = min(amount_left_to_req, FSG_BUFLEN);

			/* Beyond the end of the backing file? */
			if (usb_offset >= curlun->file_length) {
				get_some_more = 0;
				curlun->sense_data =
					SS_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE;
				curlun->sense_data_info =
					usb_offset >> curlun->blkbits;
				curlun->info_valid = 1;
				continue;
			}

			/* Get the next buffer */
			usb_offset += amount;
			common->usb_amount_left -= amount;
			amount_left_to_req -= amount;
			if (amount_left_to_req == 0)
				get_some_more = 0;

			/*
			 * Except at the end of the transfer, amount will be
			 * equal to the buffer size, which is divisible by
			 * the bulk-out maxpacket size.
			 */
			set_bulk_out_req_length(common, bh, amount);
			if (!start_out_transfer(common, bh))
				/* Dunno what to do if common->fsg is NULL */
				return -EIO;
			common->next_buffhd_to_fill = bh->next;
			continue;
		}

		/* Write the received data to the backing file */
		bh = common->next_buffhd_to_drain;
		if (bh->state == BUF_STATE_EMPTY && !get_some_more)
			break;			/* We stopped early */

		/* Wait for the data to be received */
		rc = sleep_thread(common, false, bh);
		if (rc)
			return rc;

		common->next_buffhd_to_drain = bh->next;
		bh->state = BUF_STATE_EMPTY;

		/* Did something go wrong with the transfer? */
		if (bh->outreq->status != 0) {
			curlun->sense_data = SS_COMMUNICATION_FAILURE;
			curlun->sense_data_info =
					file_offset >> curlun->blkbits;
			curlun->info_valid = 1;
			break;
		}

		amount = bh->outreq->actual;
		if (curlun->file_length - file_offset < amount) {
			LERROR(curlun, "write %u @ %llu beyond end %llu\n",
				       amount, (unsigned long long)file_offset,
				       (unsigned long long)curlun->file_length);
			amount = curlun->file_length - file_offset;
		}

		/*
		 * Don't accept excess data.  The spec doesn't say
		 * what to do in this case.  We'll ignore the error.
		 */
		amount = min(amount, bh->bulk_out_intended_length);

		/* Don't write a partial block */
		amount = round_down(amount, curlun->blksize);
		if (amount == 0)
			goto empty_write;

		/* Perform the write */
		file_offset_tmp = file_offset;
		nwritten = kernel_write(curlun->filp, bh->buf, amount,
				&file_offset_tmp);
		VLDBG(curlun, "file write %u @ %llu -> %d\n", amount,
				(unsigned long long)file_offset, (int)nwritten);
		if (signal_pending(current))
			return -EINTR;		/* Interrupted! */

		if (nwritten < 0) {
			LDBG(curlun, "error in file write: %d\n",
					(int) nwritten);
			nwritten = 0;
		} else if (nwritten < amount) {
			LDBG(curlun, "partial file write: %d/%u\n",
					(int) nwritten, amount);
			nwritten = round_down(nwritten, curlun->blksize);
		}
		file_offset += nwritten;
		amount_left_to_write -= nwritten;
		common->residue -= nwritten;

		/* If an error occurred, report it and its position */
		if (nwritten < amount) {
			curlun->sense_data = SS_WRITE_ERROR;
			curlun->sense_data_info =
					file_offset >> curlun->blkbits;
			curlun->info_valid = 1;
			break;
		}

 empty_write:
		/* Did the host decide to stop early? */
		if (bh->outreq->actual < bh->bulk_out_intended_length) {
			common->short_packet_received = 1;
			break;
		}
	}

	return -EIO;		/* No default reply */
}


/*-------------------------------------------------------------------------*/

static int do_synchronize_cache(struct fsg_common *common)
{
	struct fsg_lun	*curlun = common->curlun;
	int		rc;

	/* We ignore the requested LBA and write out all file's
	 * dirty data buffers. */
	rc = fsg_lun_fsync_sub(curlun);
	if (rc)
		curlun->sense_data = SS_WRITE_ERROR;
	return 0;
}


/*-------------------------------------------------------------------------*/

static void invalidate_sub(struct fsg_lun *curlun)
{
	struct file	*filp = curlun->filp;
	struct inode	*inode = file_inode(filp);
	unsigned long	rc;

	rc = invalidate_mapping_pages(inode->i_mapping, 0, -1);
	VLDBG(curlun, "invalidate_mapping_pages -> %ld\n", rc);
}

static int do_verify(struct fsg_common *common)
{
	struct fsg_lun		*curlun = common->curlun;
	u32			lba;
	u32			verification_length;
	struct fsg_buffhd	*bh = common->next_buffhd_to_fill;
	loff_t			file_offset, file_offset_tmp;
	u32			amount_left;
	unsigned int		amount;
	ssize_t			nread;

	/*
	 * Get the starting Logical Block Address and check that it's
	 * not too big.
	 */
	lba = get_unaligned_be32(&common->cmnd[2]);
	if (lba >= curlun->num_sectors) {
		curlun->sense_data = SS_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE;
		return -EINVAL;
	}

	/*
	 * We allow DPO (Disable Page Out = don't save data in the
	 * cache) but we don't implement it.
	 */
	if (common->cmnd[1] & ~0x10) {
		curlun->sense_data = SS_INVALID_FIELD_IN_CDB;
		return -EINVAL;
	}

	verification_length = get_unaligned_be16(&common->cmnd[7]);
	if (unlikely(verification_length == 0))
		return -EIO;		/* No default reply */

	/* Prepare to carry out the file verify */
	amount_left = verification_length << curlun->blkbits;
	file_offset = ((loff_t) lba) << curlun->blkbits;

	/* Write out all the dirty buffers before invalidating them */
	fsg_lun_fsync_sub(curlun);
	if (signal_pending(current))
		return -EINTR;

	invalidate_sub(curlun);
	if (signal_pending(current))
		return -EINTR;

	/* Just try to read the requested blocks */
	while (amount_left > 0) {
		/*
		 * Figure out how much we need to read:
		 * Try to read the remaining amount, but not more than
		 * the buffer size.
		 * And don't try to read past the end of the file.
		 */
		amount = min(amount_left, FSG_BUFLEN);
		amount = min((loff_t)amount,
			     curlun->file_length - file_offset);
		if (amount == 0) {
			curlun->sense_data =
					SS_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE;
			curlun->sense_data_info =
				file_offset >> curlun->blkbits;
			curlun->info_valid = 1;
			break;
		}

		/* Perform the read */
		file_offset_tmp = file_offset;
		nread = kernel_read(curlun->filp, bh->buf, amount,
				&file_offset_tmp);
		VLDBG(curlun, "file read %u @ %llu -> %d\n", amount,
				(unsigned long long) file_offset,
				(int) nread);
		if (signal_pending(current))
			return -EINTR;

		if (nread < 0) {
			LDBG(curlun, "error in file verify: %d\n", (int)nread);
			nread = 0;
		} else if (nread < amount) {
			LDBG(curlun, "partial file verify: %d/%u\n",
			     (int)nread, amount);
			nread = round_down(nread, curlun->blksize);
		}
		if (nread == 0) {
			curlun->sense_data = SS_UNRECOVERED_READ_ERROR;
			curlun->sense_data_info =
				file_offset >> curlun->blkbits;
			curlun->info_valid = 1;
			break;
		}
		file_offset += nread;
		amount_left -= nread;
	}
	return 0;
}


/*-------------------------------------------------------------------------*/

static int do_inquiry(struct fsg_common *common, struct fsg_buffhd *bh)
{
	struct fsg_lun *curlun = common->curlun;
	u8	*buf = (u8 *) bh->buf;

	if (!curlun) {		/* Unsupported LUNs are okay */
		common->bad_lun_okay = 1;
		memset(buf, 0, 36);
		buf[0] = TYPE_NO_LUN;	/* Unsupported, no device-type */
		buf[4] = 31;		/* Additional length */
		return 36;
	}

	buf[0] = curlun->cdrom ? TYPE_ROM : TYPE_DISK;
	buf[1] = curlun->removable ? 0x80 : 0;
	buf[2] = 2;		/* ANSI SCSI level 2 */
	buf[3] = 2;		/* SCSI-2 INQUIRY data format */
	buf[4] = 31;		/* Additional length */
	buf[5] = 0;		/* No special options */
	buf[6] = 0;
	buf[7] = 0;
	if (curlun->inquiry_string[0])
		memcpy(buf + 8, curlun->inquiry_string,
		       sizeof(curlun->inquiry_string));
	else
		memcpy(buf + 8, common->inquiry_string,
		       sizeof(common->inquiry_string));
	return 36;
}

static int do_request_sense(struct fsg_common *common, struct fsg_buffhd *bh)
{
	struct fsg_lun	*curlun = common->curlun;
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
		common->bad_lun_okay = 1;
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
	put_unaligned_be32(sdinfo, &buf[3]);	/* Sense information */
	buf[7] = 18 - 8;			/* Additional sense length */
	buf[12] = ASC(sd);
	buf[13] = ASCQ(sd);
	return 18;
}

static int do_read_capacity(struct fsg_common *common, struct fsg_buffhd *bh)
{
	struct fsg_lun	*curlun = common->curlun;
	u32		lba = get_unaligned_be32(&common->cmnd[2]);
	int		pmi = common->cmnd[8];
	u8		*buf = (u8 *)bh->buf;

	/* Check the PMI and LBA fields */
	if (pmi > 1 || (pmi == 0 && lba != 0)) {
		curlun->sense_data = SS_INVALID_FIELD_IN_CDB;
		return -EINVAL;
	}

	put_unaligned_be32(curlun->num_sectors - 1, &buf[0]);
						/* Max logical block */
	put_unaligned_be32(curlun->blksize, &buf[4]);/* Block length */
	return 8;
}

static int do_read_header(struct fsg_common *common, struct fsg_buffhd *bh)
{
	struct fsg_lun	*curlun = common->curlun;
	int		msf = common->cmnd[1] & 0x02;
	u32		lba = get_unaligned_be32(&common->cmnd[2]);
	u8		*buf = (u8 *)bh->buf;

	if (common->cmnd[1] & ~0x02) {		/* Mask away MSF */
		curlun->sense_data = SS_INVALID_FIELD_IN_CDB;
		return -EINVAL;
	}
	if (lba >= curlun->num_sectors) {
		curlun->sense_data = SS_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE;
		return -EINVAL;
	}

	memset(buf, 0, 8);
	buf[0] = 0x01;		/* 2048 bytes of user data, rest is EC */
	store_cdrom_address(&buf[4], msf, lba);
	return 8;
}

static int do_read_toc(struct fsg_common *common, struct fsg_buffhd *bh)
{
	struct fsg_lun	*curlun = common->curlun;
	int		msf = common->cmnd[1] & 0x02;
	int		start_track = common->cmnd[6];
	u8		*buf = (u8 *)bh->buf;

	if ((common->cmnd[1] & ~0x02) != 0 ||	/* Mask away MSF */
			start_track > 1) {
		curlun->sense_data = SS_INVALID_FIELD_IN_CDB;
		return -EINVAL;
	}

	memset(buf, 0, 20);
	buf[1] = (20-2);		/* TOC data length */
	buf[2] = 1;			/* First track number */
	buf[3] = 1;			/* Last track number */
	buf[5] = 0x16;			/* Data track, copying allowed */
	buf[6] = 0x01;			/* Only track is number 1 */
	store_cdrom_address(&buf[8], msf, 0);

	buf[13] = 0x16;			/* Lead-out track is data */
	buf[14] = 0xAA;			/* Lead-out track number */
	store_cdrom_address(&buf[16], msf, curlun->num_sectors);
	return 20;
}

static int do_mode_sense(struct fsg_common *common, struct fsg_buffhd *bh)
{
	struct fsg_lun	*curlun = common->curlun;
	int		mscmnd = common->cmnd[0];
	u8		*buf = (u8 *) bh->buf;
	u8		*buf0 = buf;
	int		pc, page_code;
	int		changeable_values, all_pages;
	int		valid_page = 0;
	int		len, limit;

	if ((common->cmnd[1] & ~0x08) != 0) {	/* Mask away DBD */
		curlun->sense_data = SS_INVALID_FIELD_IN_CDB;
		return -EINVAL;
	}
	pc = common->cmnd[2] >> 6;
	page_code = common->cmnd[2] & 0x3f;
	if (pc == 3) {
		curlun->sense_data = SS_SAVING_PARAMETERS_NOT_SUPPORTED;
		return -EINVAL;
	}
	changeable_values = (pc == 1);
	all_pages = (page_code == 0x3f);

	/*
	 * Write the mode parameter header.  Fixed values are: default
	 * medium type, no cache control (DPOFUA), and no block descriptors.
	 * The only variable value is the WriteProtect bit.  We will fill in
	 * the mode data length later.
	 */
	memset(buf, 0, 8);
	if (mscmnd == MODE_SENSE) {
		buf[2] = (curlun->ro ? 0x80 : 0x00);		/* WP, DPOFUA */
		buf += 4;
		limit = 255;
	} else {			/* MODE_SENSE_10 */
		buf[3] = (curlun->ro ? 0x80 : 0x00);		/* WP, DPOFUA */
		buf += 8;
		limit = 65535;		/* Should really be FSG_BUFLEN */
	}

	/* No block descriptors */

	/*
	 * The mode pages, in numerical order.  The only page we support
	 * is the Caching page.
	 */
	if (page_code == 0x08 || all_pages) {
		valid_page = 1;
		buf[0] = 0x08;		/* Page code */
		buf[1] = 10;		/* Page length */
		memset(buf+2, 0, 10);	/* None of the fields are changeable */

		if (!changeable_values) {
			buf[2] = 0x04;	/* Write cache enable, */
					/* Read cache not disabled */
					/* No cache retention priorities */
			put_unaligned_be16(0xffff, &buf[4]);
					/* Don't disable prefetch */
					/* Minimum prefetch = 0 */
			put_unaligned_be16(0xffff, &buf[8]);
					/* Maximum prefetch */
			put_unaligned_be16(0xffff, &buf[10]);
					/* Maximum prefetch ceiling */
		}
		buf += 12;
	}

	/*
	 * Check that a valid page was requested and the mode data length
	 * isn't too long.
	 */
	len = buf - buf0;
	if (!valid_page || len > limit) {
		curlun->sense_data = SS_INVALID_FIELD_IN_CDB;
		return -EINVAL;
	}

	/*  Store the mode data length */
	if (mscmnd == MODE_SENSE)
		buf0[0] = len - 1;
	else
		put_unaligned_be16(len - 2, buf0);
	return len;
}

static int do_start_stop(struct fsg_common *common)
{
	struct fsg_lun	*curlun = common->curlun;
	int		loej, start;

	if (!curlun) {
		return -EINVAL;
	} else if (!curlun->removable) {
		curlun->sense_data = SS_INVALID_COMMAND;
		return -EINVAL;
	} else if ((common->cmnd[1] & ~0x01) != 0 || /* Mask away Immed */
		   (common->cmnd[4] & ~0x03) != 0) { /* Mask LoEj, Start */
		curlun->sense_data = SS_INVALID_FIELD_IN_CDB;
		return -EINVAL;
	}

	loej  = common->cmnd[4] & 0x02;
	start = common->cmnd[4] & 0x01;

	/*
	 * Our emulation doesn't support mounting; the medium is
	 * available for use as soon as it is loaded.
	 */
	if (start) {
		if (!fsg_lun_is_open(curlun)) {
			curlun->sense_data = SS_MEDIUM_NOT_PRESENT;
			return -EINVAL;
		}
		return 0;
	}

	/* Are we allowed to unload the media? */
	if (curlun->prevent_medium_removal) {
		LDBG(curlun, "unload attempt prevented\n");
		curlun->sense_data = SS_MEDIUM_REMOVAL_PREVENTED;
		return -EINVAL;
	}

	if (!loej)
		return 0;

	up_read(&common->filesem);
	down_write(&common->filesem);
	fsg_lun_close(curlun);
	up_write(&common->filesem);
	down_read(&common->filesem);

	return 0;
}

static int do_prevent_allow(struct fsg_common *common)
{
	struct fsg_lun	*curlun = common->curlun;
	int		prevent;

	if (!common->curlun) {
		return -EINVAL;
	} else if (!common->curlun->removable) {
		common->curlun->sense_data = SS_INVALID_COMMAND;
		return -EINVAL;
	}

	prevent = common->cmnd[4] & 0x01;
	if ((common->cmnd[4] & ~0x01) != 0) {	/* Mask away Prevent */
		curlun->sense_data = SS_INVALID_FIELD_IN_CDB;
		return -EINVAL;
	}

	if (curlun->prevent_medium_removal && !prevent)
		fsg_lun_fsync_sub(curlun);
	curlun->prevent_medium_removal = prevent;
	return 0;
}

static int do_read_format_capacities(struct fsg_common *common,
			struct fsg_buffhd *bh)
{
	struct fsg_lun	*curlun = common->curlun;
	u8		*buf = (u8 *) bh->buf;

	buf[0] = buf[1] = buf[2] = 0;
	buf[3] = 8;	/* Only the Current/Maximum Capacity Descriptor */
	buf += 4;

	put_unaligned_be32(curlun->num_sectors, &buf[0]);
						/* Number of blocks */
	put_unaligned_be32(curlun->blksize, &buf[4]);/* Block length */
	buf[4] = 0x02;				/* Current capacity */
	return 12;
}

static int do_mode_select(struct fsg_common *common, struct fsg_buffhd *bh)
{
	struct fsg_lun	*curlun = common->curlun;

	/* We don't support MODE SELECT */
	if (curlun)
		curlun->sense_data = SS_INVALID_COMMAND;
	return -EINVAL;
}


/*-------------------------------------------------------------------------*/

static int halt_bulk_in_endpoint(struct fsg_dev *fsg)
{
	int	rc;

	rc = fsg_set_halt(fsg, fsg->bulk_in);
	if (rc == -EAGAIN)
		VDBG(fsg, "delayed bulk-in endpoint halt\n");
	while (rc != 0) {
		if (rc != -EAGAIN) {
			WARNING(fsg, "usb_ep_set_halt -> %d\n", rc);
			rc = 0;
			break;
		}

		/* Wait for a short time and then try again */
		if (msleep_interruptible(100) != 0)
			return -EINTR;
		rc = usb_ep_set_halt(fsg->bulk_in);
	}
	return rc;
}

static int wedge_bulk_in_endpoint(struct fsg_dev *fsg)
{
	int	rc;

	DBG(fsg, "bulk-in set wedge\n");
	rc = usb_ep_set_wedge(fsg->bulk_in);
	if (rc == -EAGAIN)
		VDBG(fsg, "delayed bulk-in endpoint wedge\n");
	while (rc != 0) {
		if (rc != -EAGAIN) {
			WARNING(fsg, "usb_ep_set_wedge -> %d\n", rc);
			rc = 0;
			break;
		}

		/* Wait for a short time and then try again */
		if (msleep_interruptible(100) != 0)
			return -EINTR;
		rc = usb_ep_set_wedge(fsg->bulk_in);
	}
	return rc;
}

static int throw_away_data(struct fsg_common *common)
{
	struct fsg_buffhd	*bh, *bh2;
	u32			amount;
	int			rc;

	for (bh = common->next_buffhd_to_drain;
	     bh->state != BUF_STATE_EMPTY || common->usb_amount_left > 0;
	     bh = common->next_buffhd_to_drain) {

		/* Try to submit another request if we need one */
		bh2 = common->next_buffhd_to_fill;
		if (bh2->state == BUF_STATE_EMPTY &&
				common->usb_amount_left > 0) {
			amount = min(common->usb_amount_left, FSG_BUFLEN);

			/*
			 * Except at the end of the transfer, amount will be
			 * equal to the buffer size, which is divisible by
			 * the bulk-out maxpacket size.
			 */
			set_bulk_out_req_length(common, bh2, amount);
			if (!start_out_transfer(common, bh2))
				/* Dunno what to do if common->fsg is NULL */
				return -EIO;
			common->next_buffhd_to_fill = bh2->next;
			common->usb_amount_left -= amount;
			continue;
		}

		/* Wait for the data to be received */
		rc = sleep_thread(common, false, bh);
		if (rc)
			return rc;

		/* Throw away the data in a filled buffer */
		bh->state = BUF_STATE_EMPTY;
		common->next_buffhd_to_drain = bh->next;

		/* A short packet or an error ends everything */
		if (bh->outreq->actual < bh->bulk_out_intended_length ||
				bh->outreq->status != 0) {
			raise_exception(common, FSG_STATE_ABORT_BULK_OUT);
			return -EINTR;
		}
	}
	return 0;
}

static int finish_reply(struct fsg_common *common)
{
	struct fsg_buffhd	*bh = common->next_buffhd_to_fill;
	int			rc = 0;

	switch (common->data_dir) {
	case DATA_DIR_NONE:
		break;			/* Nothing to send */

	/*
	 * If we don't know whether the host wants to read or write,
	 * this must be CB or CBI with an unknown command.  We mustn't
	 * try to send or receive any data.  So stall both bulk pipes
	 * if we can and wait for a reset.
	 */
	case DATA_DIR_UNKNOWN:
		if (!common->can_stall) {
			/* Nothing */
		} else if (fsg_is_set(common)) {
			fsg_set_halt(common->fsg, common->fsg->bulk_out);
			rc = halt_bulk_in_endpoint(common->fsg);
		} else {
			/* Don't know what to do if common->fsg is NULL */
			rc = -EIO;
		}
		break;

	/* All but the last buffer of data must have already been sent */
	case DATA_DIR_TO_HOST:
		if (common->data_size == 0) {
			/* Nothing to send */

		/* Don't know what to do if common->fsg is NULL */
		} else if (!fsg_is_set(common)) {
			rc = -EIO;

		/* If there's no residue, simply send the last buffer */
		} else if (common->residue == 0) {
			bh->inreq->zero = 0;
			if (!start_in_transfer(common, bh))
				return -EIO;
			common->next_buffhd_to_fill = bh->next;

		/*
		 * For Bulk-only, mark the end of the data with a short
		 * packet.  If we are allowed to stall, halt the bulk-in
		 * endpoint.  (Note: This violates the Bulk-Only Transport
		 * specification, which requires us to pad the data if we
		 * don't halt the endpoint.  Presumably nobody will mind.)
		 */
		} else {
			bh->inreq->zero = 1;
			if (!start_in_transfer(common, bh))
				rc = -EIO;
			common->next_buffhd_to_fill = bh->next;
			if (common->can_stall)
				rc = halt_bulk_in_endpoint(common->fsg);
		}
		break;

	/*
	 * We have processed all we want from the data the host has sent.
	 * There may still be outstanding bulk-out requests.
	 */
	case DATA_DIR_FROM_HOST:
		if (common->residue == 0) {
			/* Nothing to receive */

		/* Did the host stop sending unexpectedly early? */
		} else if (common->short_packet_received) {
			raise_exception(common, FSG_STATE_ABORT_BULK_OUT);
			rc = -EINTR;

		/*
		 * We haven't processed all the incoming data.  Even though
		 * we may be allowed to stall, doing so would cause a race.
		 * The controller may already have ACK'ed all the remaining
		 * bulk-out packets, in which case the host wouldn't see a
		 * STALL.  Not realizing the endpoint was halted, it wouldn't
		 * clear the halt -- leading to problems later on.
		 */
#if 0
		} else if (common->can_stall) {
			if (fsg_is_set(common))
				fsg_set_halt(common->fsg,
					     common->fsg->bulk_out);
			raise_exception(common, FSG_STATE_ABORT_BULK_OUT);
			rc = -EINTR;
#endif

		/*
		 * We can't stall.  Read in the excess data and throw it
		 * all away.
		 */
		} else {
			rc = throw_away_data(common);
		}
		break;
	}
	return rc;
}

static void send_status(struct fsg_common *common)
{
	struct fsg_lun		*curlun = common->curlun;
	struct fsg_buffhd	*bh;
	struct bulk_cs_wrap	*csw;
	int			rc;
	u8			status = US_BULK_STAT_OK;
	u32			sd, sdinfo = 0;

	/* Wait for the next buffer to become available */
	bh = common->next_buffhd_to_fill;
	rc = sleep_thread(common, false, bh);
	if (rc)
		return;

	if (curlun) {
		sd = curlun->sense_data;
		sdinfo = curlun->sense_data_info;
	} else if (common->bad_lun_okay)
		sd = SS_NO_SENSE;
	else
		sd = SS_LOGICAL_UNIT_NOT_SUPPORTED;

	if (common->phase_error) {
		DBG(common, "sending phase-error status\n");
		status = US_BULK_STAT_PHASE;
		sd = SS_INVALID_COMMAND;
	} else if (sd != SS_NO_SENSE) {
		DBG(common, "sending command-failure status\n");
		status = US_BULK_STAT_FAIL;
		VDBG(common, "  sense data: SK x%02x, ASC x%02x, ASCQ x%02x;"
				"  info x%x\n",
				SK(sd), ASC(sd), ASCQ(sd), sdinfo);
	}

	/* Store and send the Bulk-only CSW */
	csw = (void *)bh->buf;

	csw->Signature = cpu_to_le32(US_BULK_CS_SIGN);
	csw->Tag = common->tag;
	csw->Residue = cpu_to_le32(common->residue);
	csw->Status = status;

	bh->inreq->length = US_BULK_CS_WRAP_LEN;
	bh->inreq->zero = 0;
	if (!start_in_transfer(common, bh))
		/* Don't know what to do if common->fsg is NULL */
		return;

	common->next_buffhd_to_fill = bh->next;
	return;
}


/*-------------------------------------------------------------------------*/

/*
 * Check whether the command is properly formed and whether its data size
 * and direction agree with the values we already have.
 */
static int check_command(struct fsg_common *common, int cmnd_size,
			 enum data_direction data_dir, unsigned int mask,
			 int needs_medium, const char *name)
{
	int			i;
	unsigned int		lun = common->cmnd[1] >> 5;
	static const char	dirletter[4] = {'u', 'o', 'i', 'n'};
	char			hdlen[20];
	struct fsg_lun		*curlun;

	hdlen[0] = 0;
	if (common->data_dir != DATA_DIR_UNKNOWN)
		sprintf(hdlen, ", H%c=%u", dirletter[(int) common->data_dir],
			common->data_size);
	VDBG(common, "SCSI command: %s;  Dc=%d, D%c=%u;  Hc=%d%s\n",
	     name, cmnd_size, dirletter[(int) data_dir],
	     common->data_size_from_cmnd, common->cmnd_size, hdlen);

	/*
	 * We can't reply at all until we know the correct data direction
	 * and size.
	 */
	if (common->data_size_from_cmnd == 0)
		data_dir = DATA_DIR_NONE;
	if (common->data_size < common->data_size_from_cmnd) {
		/*
		 * Host data size < Device data size is a phase error.
		 * Carry out the command, but only transfer as much as
		 * we are allowed.
		 */
		common->data_size_from_cmnd = common->data_size;
		common->phase_error = 1;
	}
	common->residue = common->data_size;
	common->usb_amount_left = common->data_size;

	/* Conflicting data directions is a phase error */
	if (common->data_dir != data_dir && common->data_size_from_cmnd > 0) {
		common->phase_error = 1;
		return -EINVAL;
	}

	/* Verify the length of the command itself */
	if (cmnd_size != common->cmnd_size) {

		/*
		 * Special case workaround: There are plenty of buggy SCSI
		 * implementations. Many have issues with cbw->Length
		 * field passing a wrong command size. For those cases we
		 * always try to work around the problem by using the length
		 * sent by the host side provided it is at least as large
		 * as the correct command length.
		 * Examples of such cases would be MS-Windows, which issues
		 * REQUEST SENSE with cbw->Length == 12 where it should
		 * be 6, and xbox360 issuing INQUIRY, TEST UNIT READY and
		 * REQUEST SENSE with cbw->Length == 10 where it should
		 * be 6 as well.
		 */
		if (cmnd_size <= common->cmnd_size) {
			DBG(common, "%s is buggy! Expected length %d "
			    "but we got %d\n", name,
			    cmnd_size, common->cmnd_size);
			cmnd_size = common->cmnd_size;
		} else {
			common->phase_error = 1;
			return -EINVAL;
		}
	}

	/* Check that the LUN values are consistent */
	if (common->lun != lun)
		DBG(common, "using LUN %u from CBW, not LUN %u from CDB\n",
		    common->lun, lun);

	/* Check the LUN */
	curlun = common->curlun;
	if (curlun) {
		if (common->cmnd[0] != REQUEST_SENSE) {
			curlun->sense_data = SS_NO_SENSE;
			curlun->sense_data_info = 0;
			curlun->info_valid = 0;
		}
	} else {
		common->bad_lun_okay = 0;

		/*
		 * INQUIRY and REQUEST SENSE commands are explicitly allowed
		 * to use unsupported LUNs; all others may not.
		 */
		if (common->cmnd[0] != INQUIRY &&
		    common->cmnd[0] != REQUEST_SENSE) {
			DBG(common, "unsupported LUN %u\n", common->lun);
			return -EINVAL;
		}
	}

	/*
	 * If a unit attention condition exists, only INQUIRY and
	 * REQUEST SENSE commands are allowed; anything else must fail.
	 */
	if (curlun && curlun->unit_attention_data != SS_NO_SENSE &&
	    common->cmnd[0] != INQUIRY &&
	    common->cmnd[0] != REQUEST_SENSE) {
		curlun->sense_data = curlun->unit_attention_data;
		curlun->unit_attention_data = SS_NO_SENSE;
		return -EINVAL;
	}

	/* Check that only command bytes listed in the mask are non-zero */
	common->cmnd[1] &= 0x1f;			/* Mask away the LUN */
	for (i = 1; i < cmnd_size; ++i) {
		if (common->cmnd[i] && !(mask & (1 << i))) {
			if (curlun)
				curlun->sense_data = SS_INVALID_FIELD_IN_CDB;
			return -EINVAL;
		}
	}

	/* If the medium isn't mounted and the command needs to access
	 * it, return an error. */
	if (curlun && !fsg_lun_is_open(curlun) && needs_medium) {
		curlun->sense_data = SS_MEDIUM_NOT_PRESENT;
		return -EINVAL;
	}

	return 0;
}

/* wrapper of check_command for data size in blocks handling */
static int check_command_size_in_blocks(struct fsg_common *common,
		int cmnd_size, enum data_direction data_dir,
		unsigned int mask, int needs_medium, const char *name)
{
	if (common->curlun)
		common->data_size_from_cmnd <<= common->curlun->blkbits;
	return check_command(common, cmnd_size, data_dir,
			mask, needs_medium, name);
}

static int do_scsi_command(struct fsg_common *common)
{
	struct fsg_buffhd	*bh;
	int			rc;
	int			reply = -EINVAL;
	int			i;
	static char		unknown[16];

	dump_cdb(common);

	/* Wait for the next buffer to become available for data or status */
	bh = common->next_buffhd_to_fill;
	common->next_buffhd_to_drain = bh;
	rc = sleep_thread(common, false, bh);
	if (rc)
		return rc;

	common->phase_error = 0;
	common->short_packet_received = 0;

	down_read(&common->filesem);	/* We're using the backing file */
	switch (common->cmnd[0]) {

	case INQUIRY:
		common->data_size_from_cmnd = common->cmnd[4];
		reply = check_command(common, 6, DATA_DIR_TO_HOST,
				      (1<<4), 0,
				      "INQUIRY");
		if (reply == 0)
			reply = do_inquiry(common, bh);
		break;

	case MODE_SELECT:
		common->data_size_from_cmnd = common->cmnd[4];
		reply = check_command(common, 6, DATA_DIR_FROM_HOST,
				      (1<<1) | (1<<4), 0,
				      "MODE SELECT(6)");
		if (reply == 0)
			reply = do_mode_select(common, bh);
		break;

	case MODE_SELECT_10:
		common->data_size_from_cmnd =
			get_unaligned_be16(&common->cmnd[7]);
		reply = check_command(common, 10, DATA_DIR_FROM_HOST,
				      (1<<1) | (3<<7), 0,
				      "MODE SELECT(10)");
		if (reply == 0)
			reply = do_mode_select(common, bh);
		break;

	case MODE_SENSE:
		common->data_size_from_cmnd = common->cmnd[4];
		reply = check_command(common, 6, DATA_DIR_TO_HOST,
				      (1<<1) | (1<<2) | (1<<4), 0,
				      "MODE SENSE(6)");
		if (reply == 0)
			reply = do_mode_sense(common, bh);
		break;

	case MODE_SENSE_10:
		common->data_size_from_cmnd =
			get_unaligned_be16(&common->cmnd[7]);
		reply = check_command(common, 10, DATA_DIR_TO_HOST,
				      (1<<1) | (1<<2) | (3<<7), 0,
				      "MODE SENSE(10)");
		if (reply == 0)
			reply = do_mode_sense(common, bh);
		break;

	case ALLOW_MEDIUM_REMOVAL:
		common->data_size_from_cmnd = 0;
		reply = check_command(common, 6, DATA_DIR_NONE,
				      (1<<4), 0,
				      "PREVENT-ALLOW MEDIUM REMOVAL");
		if (reply == 0)
			reply = do_prevent_allow(common);
		break;

	case READ_6:
		i = common->cmnd[4];
		common->data_size_from_cmnd = (i == 0) ? 256 : i;
		reply = check_command_size_in_blocks(common, 6,
				      DATA_DIR_TO_HOST,
				      (7<<1) | (1<<4), 1,
				      "READ(6)");
		if (reply == 0)
			reply = do_read(common);
		break;

	case READ_10:
		common->data_size_from_cmnd =
				get_unaligned_be16(&common->cmnd[7]);
		reply = check_command_size_in_blocks(common, 10,
				      DATA_DIR_TO_HOST,
				      (1<<1) | (0xf<<2) | (3<<7), 1,
				      "READ(10)");
		if (reply == 0)
			reply = do_read(common);
		break;

	case READ_12:
		common->data_size_from_cmnd =
				get_unaligned_be32(&common->cmnd[6]);
		reply = check_command_size_in_blocks(common, 12,
				      DATA_DIR_TO_HOST,
				      (1<<1) | (0xf<<2) | (0xf<<6), 1,
				      "READ(12)");
		if (reply == 0)
			reply = do_read(common);
		break;

	case READ_CAPACITY:
		common->data_size_from_cmnd = 8;
		reply = check_command(common, 10, DATA_DIR_TO_HOST,
				      (0xf<<2) | (1<<8), 1,
				      "READ CAPACITY");
		if (reply == 0)
			reply = do_read_capacity(common, bh);
		break;

	case READ_HEADER:
		if (!common->curlun || !common->curlun->cdrom)
			goto unknown_cmnd;
		common->data_size_from_cmnd =
			get_unaligned_be16(&common->cmnd[7]);
		reply = check_command(common, 10, DATA_DIR_TO_HOST,
				      (3<<7) | (0x1f<<1), 1,
				      "READ HEADER");
		if (reply == 0)
			reply = do_read_header(common, bh);
		break;

	case READ_TOC:
		if (!common->curlun || !common->curlun->cdrom)
			goto unknown_cmnd;
		common->data_size_from_cmnd =
			get_unaligned_be16(&common->cmnd[7]);
		reply = check_command(common, 10, DATA_DIR_TO_HOST,
				      (7<<6) | (1<<1), 1,
				      "READ TOC");
		if (reply == 0)
			reply = do_read_toc(common, bh);
		break;

	case READ_FORMAT_CAPACITIES:
		common->data_size_from_cmnd =
			get_unaligned_be16(&common->cmnd[7]);
		reply = check_command(common, 10, DATA_DIR_TO_HOST,
				      (3<<7), 1,
				      "READ FORMAT CAPACITIES");
		if (reply == 0)
			reply = do_read_format_capacities(common, bh);
		break;

	case REQUEST_SENSE:
		common->data_size_from_cmnd = common->cmnd[4];
		reply = check_command(common, 6, DATA_DIR_TO_HOST,
				      (1<<4), 0,
				      "REQUEST SENSE");
		if (reply == 0)
			reply = do_request_sense(common, bh);
		break;

	case START_STOP:
		common->data_size_from_cmnd = 0;
		reply = check_command(common, 6, DATA_DIR_NONE,
				      (1<<1) | (1<<4), 0,
				      "START-STOP UNIT");
		if (reply == 0)
			reply = do_start_stop(common);
		break;

	case SYNCHRONIZE_CACHE:
		common->data_size_from_cmnd = 0;
		reply = check_command(common, 10, DATA_DIR_NONE,
				      (0xf<<2) | (3<<7), 1,
				      "SYNCHRONIZE CACHE");
		if (reply == 0)
			reply = do_synchronize_cache(common);
		break;

	case TEST_UNIT_READY:
		common->data_size_from_cmnd = 0;
		reply = check_command(common, 6, DATA_DIR_NONE,
				0, 1,
				"TEST UNIT READY");
		break;

	/*
	 * Although optional, this command is used by MS-Windows.  We
	 * support a minimal version: BytChk must be 0.
	 */
	case VERIFY:
		common->data_size_from_cmnd = 0;
		reply = check_command(common, 10, DATA_DIR_NONE,
				      (1<<1) | (0xf<<2) | (3<<7), 1,
				      "VERIFY");
		if (reply == 0)
			reply = do_verify(common);
		break;

	case WRITE_6:
		i = common->cmnd[4];
		common->data_size_from_cmnd = (i == 0) ? 256 : i;
		reply = check_command_size_in_blocks(common, 6,
				      DATA_DIR_FROM_HOST,
				      (7<<1) | (1<<4), 1,
				      "WRITE(6)");
		if (reply == 0)
			reply = do_write(common);
		break;

	case WRITE_10:
		common->data_size_from_cmnd =
				get_unaligned_be16(&common->cmnd[7]);
		reply = check_command_size_in_blocks(common, 10,
				      DATA_DIR_FROM_HOST,
				      (1<<1) | (0xf<<2) | (3<<7), 1,
				      "WRITE(10)");
		if (reply == 0)
			reply = do_write(common);
		break;

	case WRITE_12:
		common->data_size_from_cmnd =
				get_unaligned_be32(&common->cmnd[6]);
		reply = check_command_size_in_blocks(common, 12,
				      DATA_DIR_FROM_HOST,
				      (1<<1) | (0xf<<2) | (0xf<<6), 1,
				      "WRITE(12)");
		if (reply == 0)
			reply = do_write(common);
		break;

	/*
	 * Some mandatory commands that we recognize but don't implement.
	 * They don't mean much in this setting.  It's left as an exercise
	 * for anyone interested to implement RESERVE and RELEASE in terms
	 * of Posix locks.
	 */
	case FORMAT_UNIT:
	case RELEASE:
	case RESERVE:
	case SEND_DIAGNOSTIC:
		/* Fall through */

	default:
unknown_cmnd:
		common->data_size_from_cmnd = 0;
		sprintf(unknown, "Unknown x%02x", common->cmnd[0]);
		reply = check_command(common, common->cmnd_size,
				      DATA_DIR_UNKNOWN, ~0, 0, unknown);
		if (reply == 0) {
			common->curlun->sense_data = SS_INVALID_COMMAND;
			reply = -EINVAL;
		}
		break;
	}
	up_read(&common->filesem);

	if (reply == -EINTR || signal_pending(current))
		return -EINTR;

	/* Set up the single reply buffer for finish_reply() */
	if (reply == -EINVAL)
		reply = 0;		/* Error reply length */
	if (reply >= 0 && common->data_dir == DATA_DIR_TO_HOST) {
		reply = min((u32)reply, common->data_size_from_cmnd);
		bh->inreq->length = reply;
		bh->state = BUF_STATE_FULL;
		common->residue -= reply;
	}				/* Otherwise it's already set */

	return 0;
}


/*-------------------------------------------------------------------------*/

static int received_cbw(struct fsg_dev *fsg, struct fsg_buffhd *bh)
{
	struct usb_request	*req = bh->outreq;
	struct bulk_cb_wrap	*cbw = req->buf;
	struct fsg_common	*common = fsg->common;

	/* Was this a real packet?  Should it be ignored? */
	if (req->status || test_bit(IGNORE_BULK_OUT, &fsg->atomic_bitflags))
		return -EINVAL;

	/* Is the CBW valid? */
	if (req->actual != US_BULK_CB_WRAP_LEN ||
			cbw->Signature != cpu_to_le32(
				US_BULK_CB_SIGN)) {
		DBG(fsg, "invalid CBW: len %u sig 0x%x\n",
				req->actual,
				le32_to_cpu(cbw->Signature));

		/*
		 * The Bulk-only spec says we MUST stall the IN endpoint
		 * (6.6.1), so it's unavoidable.  It also says we must
		 * retain this state until the next reset, but there's
		 * no way to tell the controller driver it should ignore
		 * Clear-Feature(HALT) requests.
		 *
		 * We aren't required to halt the OUT endpoint; instead
		 * we can simply accept and discard any data received
		 * until the next reset.
		 */
		wedge_bulk_in_endpoint(fsg);
		set_bit(IGNORE_BULK_OUT, &fsg->atomic_bitflags);
		return -EINVAL;
	}

	/* Is the CBW meaningful? */
	if (cbw->Lun >= ARRAY_SIZE(common->luns) ||
	    cbw->Flags & ~US_BULK_FLAG_IN || cbw->Length <= 0 ||
	    cbw->Length > MAX_COMMAND_SIZE) {
		DBG(fsg, "non-meaningful CBW: lun = %u, flags = 0x%x, "
				"cmdlen %u\n",
				cbw->Lun, cbw->Flags, cbw->Length);

		/*
		 * We can do anything we want here, so let's stall the
		 * bulk pipes if we are allowed to.
		 */
		if (common->can_stall) {
			fsg_set_halt(fsg, fsg->bulk_out);
			halt_bulk_in_endpoint(fsg);
		}
		return -EINVAL;
	}

	/* Save the command for later */
	common->cmnd_size = cbw->Length;
	memcpy(common->cmnd, cbw->CDB, common->cmnd_size);
	if (cbw->Flags & US_BULK_FLAG_IN)
		common->data_dir = DATA_DIR_TO_HOST;
	else
		common->data_dir = DATA_DIR_FROM_HOST;
	common->data_size = le32_to_cpu(cbw->DataTransferLength);
	if (common->data_size == 0)
		common->data_dir = DATA_DIR_NONE;
	common->lun = cbw->Lun;
	if (common->lun < ARRAY_SIZE(common->luns))
		common->curlun = common->luns[common->lun];
	else
		common->curlun = NULL;
	common->tag = cbw->Tag;
	return 0;
}

static int get_next_command(struct fsg_common *common)
{
	struct fsg_buffhd	*bh;
	int			rc = 0;

	/* Wait for the next buffer to become available */
	bh = common->next_buffhd_to_fill;
	rc = sleep_thread(common, true, bh);
	if (rc)
		return rc;

	/* Queue a request to read a Bulk-only CBW */
	set_bulk_out_req_length(common, bh, US_BULK_CB_WRAP_LEN);
	if (!start_out_transfer(common, bh))
		/* Don't know what to do if common->fsg is NULL */
		return -EIO;

	/*
	 * We will drain the buffer in software, which means we
	 * can reuse it for the next filling.  No need to advance
	 * next_buffhd_to_fill.
	 */

	/* Wait for the CBW to arrive */
	rc = sleep_thread(common, true, bh);
	if (rc)
		return rc;

	rc = fsg_is_set(common) ? received_cbw(common->fsg, bh) : -EIO;
	bh->state = BUF_STATE_EMPTY;

	return rc;
}


/*-------------------------------------------------------------------------*/

static int alloc_request(struct fsg_common *common, struct usb_ep *ep,
		struct usb_request **preq)
{
	*preq = usb_ep_alloc_request(ep, GFP_ATOMIC);
	if (*preq)
		return 0;
	ERROR(common, "can't allocate request for %s\n", ep->name);
	return -ENOMEM;
}

/* Reset interface setting and re-init endpoint state (toggle etc). */
static int do_set_interface(struct fsg_common *common, struct fsg_dev *new_fsg)
{
	struct fsg_dev *fsg;
	int i, rc = 0;

	if (common->running)
		DBG(common, "reset interface\n");

reset:
	/* Deallocate the requests */
	if (common->fsg) {
		fsg = common->fsg;

		for (i = 0; i < common->fsg_num_buffers; ++i) {
			struct fsg_buffhd *bh = &common->buffhds[i];

			if (bh->inreq) {
				usb_ep_free_request(fsg->bulk_in, bh->inreq);
				bh->inreq = NULL;
			}
			if (bh->outreq) {
				usb_ep_free_request(fsg->bulk_out, bh->outreq);
				bh->outreq = NULL;
			}
		}

		/* Disable the endpoints */
		if (fsg->bulk_in_enabled) {
			usb_ep_disable(fsg->bulk_in);
			fsg->bulk_in_enabled = 0;
		}
		if (fsg->bulk_out_enabled) {
			usb_ep_disable(fsg->bulk_out);
			fsg->bulk_out_enabled = 0;
		}

		common->fsg = NULL;
		wake_up(&common->fsg_wait);
	}

	common->running = 0;
	if (!new_fsg || rc)
		return rc;

	common->fsg = new_fsg;
	fsg = common->fsg;

	/* Enable the endpoints */
	rc = config_ep_by_speed(common->gadget, &(fsg->function), fsg->bulk_in);
	if (rc)
		goto reset;
	rc = usb_ep_enable(fsg->bulk_in);
	if (rc)
		goto reset;
	fsg->bulk_in->driver_data = common;
	fsg->bulk_in_enabled = 1;

	rc = config_ep_by_speed(common->gadget, &(fsg->function),
				fsg->bulk_out);
	if (rc)
		goto reset;
	rc = usb_ep_enable(fsg->bulk_out);
	if (rc)
		goto reset;
	fsg->bulk_out->driver_data = common;
	fsg->bulk_out_enabled = 1;
	common->bulk_out_maxpacket = usb_endpoint_maxp(fsg->bulk_out->desc);
	clear_bit(IGNORE_BULK_OUT, &fsg->atomic_bitflags);

	/* Allocate the requests */
	for (i = 0; i < common->fsg_num_buffers; ++i) {
		struct fsg_buffhd	*bh = &common->buffhds[i];

		rc = alloc_request(common, fsg->bulk_in, &bh->inreq);
		if (rc)
			goto reset;
		rc = alloc_request(common, fsg->bulk_out, &bh->outreq);
		if (rc)
			goto reset;
		bh->inreq->buf = bh->outreq->buf = bh->buf;
		bh->inreq->context = bh->outreq->context = bh;
		bh->inreq->complete = bulk_in_complete;
		bh->outreq->complete = bulk_out_complete;
	}

	common->running = 1;
	for (i = 0; i < ARRAY_SIZE(common->luns); ++i)
		if (common->luns[i])
			common->luns[i]->unit_attention_data =
				SS_RESET_OCCURRED;
	return rc;
}


/****************************** ALT CONFIGS ******************************/

static int fsg_set_alt(struct usb_function *f, unsigned intf, unsigned alt)
{
	struct fsg_dev *fsg = fsg_from_func(f);
	fsg->common->new_fsg = fsg;
	raise_exception(fsg->common, FSG_STATE_CONFIG_CHANGE);
	return USB_GADGET_DELAYED_STATUS;
}

static void fsg_disable(struct usb_function *f)
{
	struct fsg_dev *fsg = fsg_from_func(f);
	fsg->common->new_fsg = NULL;
	raise_exception(fsg->common, FSG_STATE_CONFIG_CHANGE);
}


/*-------------------------------------------------------------------------*/

static void handle_exception(struct fsg_common *common)
{
	int			i;
	struct fsg_buffhd	*bh;
	enum fsg_state		old_state;
	struct fsg_lun		*curlun;
	unsigned int		exception_req_tag;

	/*
	 * Clear the existing signals.  Anything but SIGUSR1 is converted
	 * into a high-priority EXIT exception.
	 */
	for (;;) {
		int sig = kernel_dequeue_signal(NULL);
		if (!sig)
			break;
		if (sig != SIGUSR1) {
			spin_lock_irq(&common->lock);
			if (common->state < FSG_STATE_EXIT)
				DBG(common, "Main thread exiting on signal\n");
			common->state = FSG_STATE_EXIT;
			spin_unlock_irq(&common->lock);
		}
	}

	/* Cancel all the pending transfers */
	if (likely(common->fsg)) {
		for (i = 0; i < common->fsg_num_buffers; ++i) {
			bh = &common->buffhds[i];
			if (bh->state == BUF_STATE_SENDING)
				usb_ep_dequeue(common->fsg->bulk_in, bh->inreq);
			if (bh->state == BUF_STATE_RECEIVING)
				usb_ep_dequeue(common->fsg->bulk_out,
					       bh->outreq);

			/* Wait for a transfer to become idle */
			if (sleep_thread(common, false, bh))
				return;
		}

		/* Clear out the controller's fifos */
		if (common->fsg->bulk_in_enabled)
			usb_ep_fifo_flush(common->fsg->bulk_in);
		if (common->fsg->bulk_out_enabled)
			usb_ep_fifo_flush(common->fsg->bulk_out);
	}

	/*
	 * Reset the I/O buffer states and pointers, the SCSI
	 * state, and the exception.  Then invoke the handler.
	 */
	spin_lock_irq(&common->lock);

	for (i = 0; i < common->fsg_num_buffers; ++i) {
		bh = &common->buffhds[i];
		bh->state = BUF_STATE_EMPTY;
	}
	common->next_buffhd_to_fill = &common->buffhds[0];
	common->next_buffhd_to_drain = &common->buffhds[0];
	exception_req_tag = common->exception_req_tag;
	old_state = common->state;
	common->state = FSG_STATE_NORMAL;

	if (old_state != FSG_STATE_ABORT_BULK_OUT) {
		for (i = 0; i < ARRAY_SIZE(common->luns); ++i) {
			curlun = common->luns[i];
			if (!curlun)
				continue;
			curlun->prevent_medium_removal = 0;
			curlun->sense_data = SS_NO_SENSE;
			curlun->unit_attention_data = SS_NO_SENSE;
			curlun->sense_data_info = 0;
			curlun->info_valid = 0;
		}
	}
	spin_unlock_irq(&common->lock);

	/* Carry out any extra actions required for the exception */
	switch (old_state) {
	case FSG_STATE_NORMAL:
		break;

	case FSG_STATE_ABORT_BULK_OUT:
		send_status(common);
		break;

	case FSG_STATE_PROTOCOL_RESET:
		/*
		 * In case we were forced against our will to halt a
		 * bulk endpoint, clear the halt now.  (The SuperH UDC
		 * requires this.)
		 */
		if (!fsg_is_set(common))
			break;
		if (test_and_clear_bit(IGNORE_BULK_OUT,
				       &common->fsg->atomic_bitflags))
			usb_ep_clear_halt(common->fsg->bulk_in);

		if (common->ep0_req_tag == exception_req_tag)
			ep0_queue(common);	/* Complete the status stage */

		/*
		 * Technically this should go here, but it would only be
		 * a waste of time.  Ditto for the INTERFACE_CHANGE and
		 * CONFIG_CHANGE cases.
		 */
		/* for (i = 0; i < common->ARRAY_SIZE(common->luns); ++i) */
		/*	if (common->luns[i]) */
		/*		common->luns[i]->unit_attention_data = */
		/*			SS_RESET_OCCURRED;  */
		break;

	case FSG_STATE_CONFIG_CHANGE:
		do_set_interface(common, common->new_fsg);
		if (common->new_fsg)
			usb_composite_setup_continue(common->cdev);
		break;

	case FSG_STATE_EXIT:
		do_set_interface(common, NULL);		/* Free resources */
		spin_lock_irq(&common->lock);
		common->state = FSG_STATE_TERMINATED;	/* Stop the thread */
		spin_unlock_irq(&common->lock);
		break;

	case FSG_STATE_TERMINATED:
		break;
	}
}


/*-------------------------------------------------------------------------*/

static int fsg_main_thread(void *common_)
{
	struct fsg_common	*common = common_;
	int			i;

	/*
	 * Allow the thread to be killed by a signal, but set the signal mask
	 * to block everything but INT, TERM, KILL, and USR1.
	 */
	allow_signal(SIGINT);
	allow_signal(SIGTERM);
	allow_signal(SIGKILL);
	allow_signal(SIGUSR1);

	/* Allow the thread to be frozen */
	set_freezable();

	/* The main loop */
	while (common->state != FSG_STATE_TERMINATED) {
		if (exception_in_progress(common) || signal_pending(current)) {
			handle_exception(common);
			continue;
		}

		if (!common->running) {
			sleep_thread(common, true, NULL);
			continue;
		}

		if (get_next_command(common) || exception_in_progress(common))
			continue;
		if (do_scsi_command(common) || exception_in_progress(common))
			continue;
		if (finish_reply(common) || exception_in_progress(common))
			continue;
		send_status(common);
	}

	spin_lock_irq(&common->lock);
	common->thread_task = NULL;
	spin_unlock_irq(&common->lock);

	/* Eject media from all LUNs */

	down_write(&common->filesem);
	for (i = 0; i < ARRAY_SIZE(common->luns); i++) {
		struct fsg_lun *curlun = common->luns[i];

		if (curlun && fsg_lun_is_open(curlun))
			fsg_lun_close(curlun);
	}
	up_write(&common->filesem);

	/* Let fsg_unbind() know the thread has exited */
	complete_and_exit(&common->thread_notifier, 0);
}


/*************************** DEVICE ATTRIBUTES ***************************/

static ssize_t ro_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct fsg_lun		*curlun = fsg_lun_from_dev(dev);

	return fsg_show_ro(curlun, buf);
}

static ssize_t nofua_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct fsg_lun		*curlun = fsg_lun_from_dev(dev);

	return fsg_show_nofua(curlun, buf);
}

static ssize_t file_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct fsg_lun		*curlun = fsg_lun_from_dev(dev);
	struct rw_semaphore	*filesem = dev_get_drvdata(dev);

	return fsg_show_file(curlun, filesem, buf);
}

static ssize_t ro_store(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct fsg_lun		*curlun = fsg_lun_from_dev(dev);
	struct rw_semaphore	*filesem = dev_get_drvdata(dev);

	return fsg_store_ro(curlun, filesem, buf, count);
}

static ssize_t nofua_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct fsg_lun		*curlun = fsg_lun_from_dev(dev);

	return fsg_store_nofua(curlun, buf, count);
}

static ssize_t file_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct fsg_lun		*curlun = fsg_lun_from_dev(dev);
	struct rw_semaphore	*filesem = dev_get_drvdata(dev);

	return fsg_store_file(curlun, filesem, buf, count);
}

static DEVICE_ATTR_RW(nofua);
/* mode wil be set in fsg_lun_attr_is_visible() */
static DEVICE_ATTR(ro, 0, ro_show, ro_store);
static DEVICE_ATTR(file, 0, file_show, file_store);

/****************************** FSG COMMON ******************************/

static void fsg_common_release(struct kref *ref);

static void fsg_lun_release(struct device *dev)
{
	/* Nothing needs to be done */
}

void fsg_common_get(struct fsg_common *common)
{
	kref_get(&common->ref);
}
EXPORT_SYMBOL_GPL(fsg_common_get);

void fsg_common_put(struct fsg_common *common)
{
	kref_put(&common->ref, fsg_common_release);
}
EXPORT_SYMBOL_GPL(fsg_common_put);

static struct fsg_common *fsg_common_setup(struct fsg_common *common)
{
	if (!common) {
		common = kzalloc(sizeof(*common), GFP_KERNEL);
		if (!common)
			return ERR_PTR(-ENOMEM);
		common->free_storage_on_release = 1;
	} else {
		common->free_storage_on_release = 0;
	}
	init_rwsem(&common->filesem);
	spin_lock_init(&common->lock);
	kref_init(&common->ref);
	init_completion(&common->thread_notifier);
	init_waitqueue_head(&common->io_wait);
	init_waitqueue_head(&common->fsg_wait);
	common->state = FSG_STATE_TERMINATED;
	memset(common->luns, 0, sizeof(common->luns));

	return common;
}

void fsg_common_set_sysfs(struct fsg_common *common, bool sysfs)
{
	common->sysfs = sysfs;
}
EXPORT_SYMBOL_GPL(fsg_common_set_sysfs);

static void _fsg_common_free_buffers(struct fsg_buffhd *buffhds, unsigned n)
{
	if (buffhds) {
		struct fsg_buffhd *bh = buffhds;
		while (n--) {
			kfree(bh->buf);
			++bh;
		}
		kfree(buffhds);
	}
}

int fsg_common_set_num_buffers(struct fsg_common *common, unsigned int n)
{
	struct fsg_buffhd *bh, *buffhds;
	int i;

	buffhds = kcalloc(n, sizeof(*buffhds), GFP_KERNEL);
	if (!buffhds)
		return -ENOMEM;

	/* Data buffers cyclic list */
	bh = buffhds;
	i = n;
	goto buffhds_first_it;
	do {
		bh->next = bh + 1;
		++bh;
buffhds_first_it:
		bh->buf = kmalloc(FSG_BUFLEN, GFP_KERNEL);
		if (unlikely(!bh->buf))
			goto error_release;
	} while (--i);
	bh->next = buffhds;

	_fsg_common_free_buffers(common->buffhds, common->fsg_num_buffers);
	common->fsg_num_buffers = n;
	common->buffhds = buffhds;

	return 0;

error_release:
	/*
	 * "buf"s pointed to by heads after n - i are NULL
	 * so releasing them won't hurt
	 */
	_fsg_common_free_buffers(buffhds, n);

	return -ENOMEM;
}
EXPORT_SYMBOL_GPL(fsg_common_set_num_buffers);

void fsg_common_remove_lun(struct fsg_lun *lun)
{
	if (device_is_registered(&lun->dev))
		device_unregister(&lun->dev);
	fsg_lun_close(lun);
	kfree(lun);
}
EXPORT_SYMBOL_GPL(fsg_common_remove_lun);

static void _fsg_common_remove_luns(struct fsg_common *common, int n)
{
	int i;

	for (i = 0; i < n; ++i)
		if (common->luns[i]) {
			fsg_common_remove_lun(common->luns[i]);
			common->luns[i] = NULL;
		}
}

void fsg_common_remove_luns(struct fsg_common *common)
{
	_fsg_common_remove_luns(common, ARRAY_SIZE(common->luns));
}
EXPORT_SYMBOL_GPL(fsg_common_remove_luns);

void fsg_common_free_buffers(struct fsg_common *common)
{
	_fsg_common_free_buffers(common->buffhds, common->fsg_num_buffers);
	common->buffhds = NULL;
}
EXPORT_SYMBOL_GPL(fsg_common_free_buffers);

int fsg_common_set_cdev(struct fsg_common *common,
			 struct usb_composite_dev *cdev, bool can_stall)
{
	struct usb_string *us;

	common->gadget = cdev->gadget;
	common->ep0 = cdev->gadget->ep0;
	common->ep0req = cdev->req;
	common->cdev = cdev;

	us = usb_gstrings_attach(cdev, fsg_strings_array,
				 ARRAY_SIZE(fsg_strings));
	if (IS_ERR(us))
		return PTR_ERR(us);

	fsg_intf_desc.iInterface = us[FSG_STRING_INTERFACE].id;

	/*
	 * Some peripheral controllers are known not to be able to
	 * halt bulk endpoints correctly.  If one of them is present,
	 * disable stalls.
	 */
	common->can_stall = can_stall &&
			gadget_is_stall_supported(common->gadget);

	return 0;
}
EXPORT_SYMBOL_GPL(fsg_common_set_cdev);

static struct attribute *fsg_lun_dev_attrs[] = {
	&dev_attr_ro.attr,
	&dev_attr_file.attr,
	&dev_attr_nofua.attr,
	NULL
};

static umode_t fsg_lun_dev_is_visible(struct kobject *kobj,
				      struct attribute *attr, int idx)
{
	struct device *dev = kobj_to_dev(kobj);
	struct fsg_lun *lun = fsg_lun_from_dev(dev);

	if (attr == &dev_attr_ro.attr)
		return lun->cdrom ? S_IRUGO : (S_IWUSR | S_IRUGO);
	if (attr == &dev_attr_file.attr)
		return lun->removable ? (S_IWUSR | S_IRUGO) : S_IRUGO;
	return attr->mode;
}

static const struct attribute_group fsg_lun_dev_group = {
	.attrs = fsg_lun_dev_attrs,
	.is_visible = fsg_lun_dev_is_visible,
};

static const struct attribute_group *fsg_lun_dev_groups[] = {
	&fsg_lun_dev_group,
	NULL
};

int fsg_common_create_lun(struct fsg_common *common, struct fsg_lun_config *cfg,
			  unsigned int id, const char *name,
			  const char **name_pfx)
{
	struct fsg_lun *lun;
	char *pathbuf, *p;
	int rc = -ENOMEM;

	if (id >= ARRAY_SIZE(common->luns))
		return -ENODEV;

	if (common->luns[id])
		return -EBUSY;

	if (!cfg->filename && !cfg->removable) {
		pr_err("no file given for LUN%d\n", id);
		return -EINVAL;
	}

	lun = kzalloc(sizeof(*lun), GFP_KERNEL);
	if (!lun)
		return -ENOMEM;

	lun->name_pfx = name_pfx;

	lun->cdrom = !!cfg->cdrom;
	lun->ro = cfg->cdrom || cfg->ro;
	lun->initially_ro = lun->ro;
	lun->removable = !!cfg->removable;

	if (!common->sysfs) {
		/* we DON'T own the name!*/
		lun->name = name;
	} else {
		lun->dev.release = fsg_lun_release;
		lun->dev.parent = &common->gadget->dev;
		lun->dev.groups = fsg_lun_dev_groups;
		dev_set_drvdata(&lun->dev, &common->filesem);
		dev_set_name(&lun->dev, "%s", name);
		lun->name = dev_name(&lun->dev);

		rc = device_register(&lun->dev);
		if (rc) {
			pr_info("failed to register LUN%d: %d\n", id, rc);
			put_device(&lun->dev);
			goto error_sysfs;
		}
	}

	common->luns[id] = lun;

	if (cfg->filename) {
		rc = fsg_lun_open(lun, cfg->filename);
		if (rc)
			goto error_lun;
	}

	pathbuf = kmalloc(PATH_MAX, GFP_KERNEL);
	p = "(no medium)";
	if (fsg_lun_is_open(lun)) {
		p = "(error)";
		if (pathbuf) {
			p = file_path(lun->filp, pathbuf, PATH_MAX);
			if (IS_ERR(p))
				p = "(error)";
		}
	}
	pr_info("LUN: %s%s%sfile: %s\n",
	      lun->removable ? "removable " : "",
	      lun->ro ? "read only " : "",
	      lun->cdrom ? "CD-ROM " : "",
	      p);
	kfree(pathbuf);

	return 0;

error_lun:
	if (device_is_registered(&lun->dev))
		device_unregister(&lun->dev);
	fsg_lun_close(lun);
	common->luns[id] = NULL;
error_sysfs:
	kfree(lun);
	return rc;
}
EXPORT_SYMBOL_GPL(fsg_common_create_lun);

int fsg_common_create_luns(struct fsg_common *common, struct fsg_config *cfg)
{
	char buf[8]; /* enough for 100000000 different numbers, decimal */
	int i, rc;

	fsg_common_remove_luns(common);

	for (i = 0; i < cfg->nluns; ++i) {
		snprintf(buf, sizeof(buf), "lun%d", i);
		rc = fsg_common_create_lun(common, &cfg->luns[i], i, buf, NULL);
		if (rc)
			goto fail;
	}

	pr_info("Number of LUNs=%d\n", cfg->nluns);

	return 0;

fail:
	_fsg_common_remove_luns(common, i);
	return rc;
}
EXPORT_SYMBOL_GPL(fsg_common_create_luns);

void fsg_common_set_inquiry_string(struct fsg_common *common, const char *vn,
				   const char *pn)
{
	int i;

	/* Prepare inquiryString */
	i = get_default_bcdDevice();
	snprintf(common->inquiry_string, sizeof(common->inquiry_string),
		 "%-8s%-16s%04x", vn ?: "Linux",
		 /* Assume product name dependent on the first LUN */
		 pn ?: ((*common->luns)->cdrom
		     ? "File-CD Gadget"
		     : "File-Stor Gadget"),
		 i);
}
EXPORT_SYMBOL_GPL(fsg_common_set_inquiry_string);

static void fsg_common_release(struct kref *ref)
{
	struct fsg_common *common = container_of(ref, struct fsg_common, ref);
	int i;

	/* If the thread isn't already dead, tell it to exit now */
	if (common->state != FSG_STATE_TERMINATED) {
		raise_exception(common, FSG_STATE_EXIT);
		wait_for_completion(&common->thread_notifier);
	}

	for (i = 0; i < ARRAY_SIZE(common->luns); ++i) {
		struct fsg_lun *lun = common->luns[i];
		if (!lun)
			continue;
		fsg_lun_close(lun);
		if (device_is_registered(&lun->dev))
			device_unregister(&lun->dev);
		kfree(lun);
	}

	_fsg_common_free_buffers(common->buffhds, common->fsg_num_buffers);
	if (common->free_storage_on_release)
		kfree(common);
}


/*-------------------------------------------------------------------------*/

static int fsg_bind(struct usb_configuration *c, struct usb_function *f)
{
	struct fsg_dev		*fsg = fsg_from_func(f);
	struct fsg_common	*common = fsg->common;
	struct usb_gadget	*gadget = c->cdev->gadget;
	int			i;
	struct usb_ep		*ep;
	unsigned		max_burst;
	int			ret;
	struct fsg_opts		*opts;

	/* Don't allow to bind if we don't have at least one LUN */
	ret = _fsg_common_get_max_lun(common);
	if (ret < 0) {
		pr_err("There should be at least one LUN.\n");
		return -EINVAL;
	}

	opts = fsg_opts_from_func_inst(f->fi);
	if (!opts->no_configfs) {
		ret = fsg_common_set_cdev(fsg->common, c->cdev,
					  fsg->common->can_stall);
		if (ret)
			return ret;
		fsg_common_set_inquiry_string(fsg->common, NULL, NULL);
	}

	if (!common->thread_task) {
		common->state = FSG_STATE_NORMAL;
		common->thread_task =
			kthread_create(fsg_main_thread, common, "file-storage");
		if (IS_ERR(common->thread_task)) {
			ret = PTR_ERR(common->thread_task);
			common->thread_task = NULL;
			common->state = FSG_STATE_TERMINATED;
			return ret;
		}
		DBG(common, "I/O thread pid: %d\n",
		    task_pid_nr(common->thread_task));
		wake_up_process(common->thread_task);
	}

	fsg->gadget = gadget;

	/* New interface */
	i = usb_interface_id(c, f);
	if (i < 0)
		goto fail;
	fsg_intf_desc.bInterfaceNumber = i;
	fsg->interface_number = i;

	/* Find all the endpoints we will use */
	ep = usb_ep_autoconfig(gadget, &fsg_fs_bulk_in_desc);
	if (!ep)
		goto autoconf_fail;
	fsg->bulk_in = ep;

	ep = usb_ep_autoconfig(gadget, &fsg_fs_bulk_out_desc);
	if (!ep)
		goto autoconf_fail;
	fsg->bulk_out = ep;

	/* Assume endpoint addresses are the same for both speeds */
	fsg_hs_bulk_in_desc.bEndpointAddress =
		fsg_fs_bulk_in_desc.bEndpointAddress;
	fsg_hs_bulk_out_desc.bEndpointAddress =
		fsg_fs_bulk_out_desc.bEndpointAddress;

	/* Calculate bMaxBurst, we know packet size is 1024 */
	max_burst = min_t(unsigned, FSG_BUFLEN / 1024, 15);

	fsg_ss_bulk_in_desc.bEndpointAddress =
		fsg_fs_bulk_in_desc.bEndpointAddress;
	fsg_ss_bulk_in_comp_desc.bMaxBurst = max_burst;

	fsg_ss_bulk_out_desc.bEndpointAddress =
		fsg_fs_bulk_out_desc.bEndpointAddress;
	fsg_ss_bulk_out_comp_desc.bMaxBurst = max_burst;

	ret = usb_assign_descriptors(f, fsg_fs_function, fsg_hs_function,
			fsg_ss_function, fsg_ss_function);
	if (ret)
		goto autoconf_fail;

	return 0;

autoconf_fail:
	ERROR(fsg, "unable to autoconfigure all endpoints\n");
	i = -ENOTSUPP;
fail:
	/* terminate the thread */
	if (fsg->common->state != FSG_STATE_TERMINATED) {
		raise_exception(fsg->common, FSG_STATE_EXIT);
		wait_for_completion(&fsg->common->thread_notifier);
	}
	return i;
}

/****************************** ALLOCATE FUNCTION *************************/

static void fsg_unbind(struct usb_configuration *c, struct usb_function *f)
{
	struct fsg_dev		*fsg = fsg_from_func(f);
	struct fsg_common	*common = fsg->common;

	DBG(fsg, "unbind\n");
	if (fsg->common->fsg == fsg) {
		fsg->common->new_fsg = NULL;
		raise_exception(fsg->common, FSG_STATE_CONFIG_CHANGE);
		/* FIXME: make interruptible or killable somehow? */
		wait_event(common->fsg_wait, common->fsg != fsg);
	}

	usb_free_all_descriptors(&fsg->function);
}

static inline struct fsg_lun_opts *to_fsg_lun_opts(struct config_item *item)
{
	return container_of(to_config_group(item), struct fsg_lun_opts, group);
}

static inline struct fsg_opts *to_fsg_opts(struct config_item *item)
{
	return container_of(to_config_group(item), struct fsg_opts,
			    func_inst.group);
}

static void fsg_lun_attr_release(struct config_item *item)
{
	struct fsg_lun_opts *lun_opts;

	lun_opts = to_fsg_lun_opts(item);
	kfree(lun_opts);
}

static struct configfs_item_operations fsg_lun_item_ops = {
	.release		= fsg_lun_attr_release,
};

static ssize_t fsg_lun_opts_file_show(struct config_item *item, char *page)
{
	struct fsg_lun_opts *opts = to_fsg_lun_opts(item);
	struct fsg_opts *fsg_opts = to_fsg_opts(opts->group.cg_item.ci_parent);

	return fsg_show_file(opts->lun, &fsg_opts->common->filesem, page);
}

static ssize_t fsg_lun_opts_file_store(struct config_item *item,
				       const char *page, size_t len)
{
	struct fsg_lun_opts *opts = to_fsg_lun_opts(item);
	struct fsg_opts *fsg_opts = to_fsg_opts(opts->group.cg_item.ci_parent);

	return fsg_store_file(opts->lun, &fsg_opts->common->filesem, page, len);
}

CONFIGFS_ATTR(fsg_lun_opts_, file);

static ssize_t fsg_lun_opts_ro_show(struct config_item *item, char *page)
{
	return fsg_show_ro(to_fsg_lun_opts(item)->lun, page);
}

static ssize_t fsg_lun_opts_ro_store(struct config_item *item,
				       const char *page, size_t len)
{
	struct fsg_lun_opts *opts = to_fsg_lun_opts(item);
	struct fsg_opts *fsg_opts = to_fsg_opts(opts->group.cg_item.ci_parent);

	return fsg_store_ro(opts->lun, &fsg_opts->common->filesem, page, len);
}

CONFIGFS_ATTR(fsg_lun_opts_, ro);

static ssize_t fsg_lun_opts_removable_show(struct config_item *item,
					   char *page)
{
	return fsg_show_removable(to_fsg_lun_opts(item)->lun, page);
}

static ssize_t fsg_lun_opts_removable_store(struct config_item *item,
				       const char *page, size_t len)
{
	return fsg_store_removable(to_fsg_lun_opts(item)->lun, page, len);
}

CONFIGFS_ATTR(fsg_lun_opts_, removable);

static ssize_t fsg_lun_opts_cdrom_show(struct config_item *item, char *page)
{
	return fsg_show_cdrom(to_fsg_lun_opts(item)->lun, page);
}

static ssize_t fsg_lun_opts_cdrom_store(struct config_item *item,
				       const char *page, size_t len)
{
	struct fsg_lun_opts *opts = to_fsg_lun_opts(item);
	struct fsg_opts *fsg_opts = to_fsg_opts(opts->group.cg_item.ci_parent);

	return fsg_store_cdrom(opts->lun, &fsg_opts->common->filesem, page,
			       len);
}

CONFIGFS_ATTR(fsg_lun_opts_, cdrom);

static ssize_t fsg_lun_opts_nofua_show(struct config_item *item, char *page)
{
	return fsg_show_nofua(to_fsg_lun_opts(item)->lun, page);
}

static ssize_t fsg_lun_opts_nofua_store(struct config_item *item,
				       const char *page, size_t len)
{
	return fsg_store_nofua(to_fsg_lun_opts(item)->lun, page, len);
}

CONFIGFS_ATTR(fsg_lun_opts_, nofua);

static ssize_t fsg_lun_opts_inquiry_string_show(struct config_item *item,
						char *page)
{
	return fsg_show_inquiry_string(to_fsg_lun_opts(item)->lun, page);
}

static ssize_t fsg_lun_opts_inquiry_string_store(struct config_item *item,
						 const char *page, size_t len)
{
	return fsg_store_inquiry_string(to_fsg_lun_opts(item)->lun, page, len);
}

CONFIGFS_ATTR(fsg_lun_opts_, inquiry_string);

static struct configfs_attribute *fsg_lun_attrs[] = {
	&fsg_lun_opts_attr_file,
	&fsg_lun_opts_attr_ro,
	&fsg_lun_opts_attr_removable,
	&fsg_lun_opts_attr_cdrom,
	&fsg_lun_opts_attr_nofua,
	&fsg_lun_opts_attr_inquiry_string,
	NULL,
};

static struct config_item_type fsg_lun_type = {
	.ct_item_ops	= &fsg_lun_item_ops,
	.ct_attrs	= fsg_lun_attrs,
	.ct_owner	= THIS_MODULE,
};

static struct config_group *fsg_lun_make(struct config_group *group,
					 const char *name)
{
	struct fsg_lun_opts *opts;
	struct fsg_opts *fsg_opts;
	struct fsg_lun_config config;
	char *num_str;
	u8 num;
	int ret;

	num_str = strchr(name, '.');
	if (!num_str) {
		pr_err("Unable to locate . in LUN.NUMBER\n");
		return ERR_PTR(-EINVAL);
	}
	num_str++;

	ret = kstrtou8(num_str, 0, &num);
	if (ret)
		return ERR_PTR(ret);

	fsg_opts = to_fsg_opts(&group->cg_item);
	if (num >= FSG_MAX_LUNS)
		return ERR_PTR(-ERANGE);
	num = array_index_nospec(num, FSG_MAX_LUNS);

	mutex_lock(&fsg_opts->lock);
	if (fsg_opts->refcnt || fsg_opts->common->luns[num]) {
		ret = -EBUSY;
		goto out;
	}

	opts = kzalloc(sizeof(*opts), GFP_KERNEL);
	if (!opts) {
		ret = -ENOMEM;
		goto out;
	}

	memset(&config, 0, sizeof(config));
	config.removable = true;

	ret = fsg_common_create_lun(fsg_opts->common, &config, num, name,
				    (const char **)&group->cg_item.ci_name);
	if (ret) {
		kfree(opts);
		goto out;
	}
	opts->lun = fsg_opts->common->luns[num];
	opts->lun_id = num;
	mutex_unlock(&fsg_opts->lock);

	config_group_init_type_name(&opts->group, name, &fsg_lun_type);

	return &opts->group;
out:
	mutex_unlock(&fsg_opts->lock);
	return ERR_PTR(ret);
}

static void fsg_lun_drop(struct config_group *group, struct config_item *item)
{
	struct fsg_lun_opts *lun_opts;
	struct fsg_opts *fsg_opts;

	lun_opts = to_fsg_lun_opts(item);
	fsg_opts = to_fsg_opts(&group->cg_item);

	mutex_lock(&fsg_opts->lock);
	if (fsg_opts->refcnt) {
		struct config_item *gadget;

		gadget = group->cg_item.ci_parent->ci_parent;
		unregister_gadget_item(gadget);
	}

	fsg_common_remove_lun(lun_opts->lun);
	fsg_opts->common->luns[lun_opts->lun_id] = NULL;
	lun_opts->lun_id = 0;
	mutex_unlock(&fsg_opts->lock);

	config_item_put(item);
}

static void fsg_attr_release(struct config_item *item)
{
	struct fsg_opts *opts = to_fsg_opts(item);

	usb_put_function_instance(&opts->func_inst);
}

static struct configfs_item_operations fsg_item_ops = {
	.release		= fsg_attr_release,
};

static ssize_t fsg_opts_stall_show(struct config_item *item, char *page)
{
	struct fsg_opts *opts = to_fsg_opts(item);
	int result;

	mutex_lock(&opts->lock);
	result = sprintf(page, "%d", opts->common->can_stall);
	mutex_unlock(&opts->lock);

	return result;
}

static ssize_t fsg_opts_stall_store(struct config_item *item, const char *page,
				    size_t len)
{
	struct fsg_opts *opts = to_fsg_opts(item);
	int ret;
	bool stall;

	mutex_lock(&opts->lock);

	if (opts->refcnt) {
		mutex_unlock(&opts->lock);
		return -EBUSY;
	}

	ret = strtobool(page, &stall);
	if (!ret) {
		opts->common->can_stall = stall;
		ret = len;
	}

	mutex_unlock(&opts->lock);

	return ret;
}

CONFIGFS_ATTR(fsg_opts_, stall);

#ifdef CONFIG_USB_GADGET_DEBUG_FILES
static ssize_t fsg_opts_num_buffers_show(struct config_item *item, char *page)
{
	struct fsg_opts *opts = to_fsg_opts(item);
	int result;

	mutex_lock(&opts->lock);
	result = sprintf(page, "%d", opts->common->fsg_num_buffers);
	mutex_unlock(&opts->lock);

	return result;
}

static ssize_t fsg_opts_num_buffers_store(struct config_item *item,
					  const char *page, size_t len)
{
	struct fsg_opts *opts = to_fsg_opts(item);
	int ret;
	u8 num;

	mutex_lock(&opts->lock);
	if (opts->refcnt) {
		ret = -EBUSY;
		goto end;
	}
	ret = kstrtou8(page, 0, &num);
	if (ret)
		goto end;

	fsg_common_set_num_buffers(opts->common, num);
	ret = len;

end:
	mutex_unlock(&opts->lock);
	return ret;
}

CONFIGFS_ATTR(fsg_opts_, num_buffers);
#endif

static struct configfs_attribute *fsg_attrs[] = {
	&fsg_opts_attr_stall,
#ifdef CONFIG_USB_GADGET_DEBUG_FILES
	&fsg_opts_attr_num_buffers,
#endif
	NULL,
};

static struct configfs_group_operations fsg_group_ops = {
	.make_group	= fsg_lun_make,
	.drop_item	= fsg_lun_drop,
};

static struct config_item_type fsg_func_type = {
	.ct_item_ops	= &fsg_item_ops,
	.ct_group_ops	= &fsg_group_ops,
	.ct_attrs	= fsg_attrs,
	.ct_owner	= THIS_MODULE,
};

static void fsg_free_inst(struct usb_function_instance *fi)
{
	struct fsg_opts *opts;

	opts = fsg_opts_from_func_inst(fi);
	fsg_common_put(opts->common);
	kfree(opts);
}

static struct usb_function_instance *fsg_alloc_inst(void)
{
	struct fsg_opts *opts;
	struct fsg_lun_config config;
	int rc;

	opts = kzalloc(sizeof(*opts), GFP_KERNEL);
	if (!opts)
		return ERR_PTR(-ENOMEM);
	mutex_init(&opts->lock);
	opts->func_inst.free_func_inst = fsg_free_inst;
	opts->common = fsg_common_setup(opts->common);
	if (IS_ERR(opts->common)) {
		rc = PTR_ERR(opts->common);
		goto release_opts;
	}

	rc = fsg_common_set_num_buffers(opts->common,
					CONFIG_USB_GADGET_STORAGE_NUM_BUFFERS);
	if (rc)
		goto release_opts;

	pr_info(FSG_DRIVER_DESC ", version: " FSG_DRIVER_VERSION "\n");

	memset(&config, 0, sizeof(config));
	config.removable = true;
	rc = fsg_common_create_lun(opts->common, &config, 0, "lun.0",
			(const char **)&opts->func_inst.group.cg_item.ci_name);
	if (rc)
		goto release_buffers;

	opts->lun0.lun = opts->common->luns[0];
	opts->lun0.lun_id = 0;

	config_group_init_type_name(&opts->func_inst.group, "", &fsg_func_type);

	config_group_init_type_name(&opts->lun0.group, "lun.0", &fsg_lun_type);
	configfs_add_default_group(&opts->lun0.group, &opts->func_inst.group);

	return &opts->func_inst;

release_buffers:
	fsg_common_free_buffers(opts->common);
release_opts:
	kfree(opts);
	return ERR_PTR(rc);
}

static void fsg_free(struct usb_function *f)
{
	struct fsg_dev *fsg;
	struct fsg_opts *opts;

	fsg = container_of(f, struct fsg_dev, function);
	opts = container_of(f->fi, struct fsg_opts, func_inst);

	mutex_lock(&opts->lock);
	opts->refcnt--;
	mutex_unlock(&opts->lock);

	kfree(fsg);
}

static struct usb_function *fsg_alloc(struct usb_function_instance *fi)
{
	struct fsg_opts *opts = fsg_opts_from_func_inst(fi);
	struct fsg_common *common = opts->common;
	struct fsg_dev *fsg;

	fsg = kzalloc(sizeof(*fsg), GFP_KERNEL);
	if (unlikely(!fsg))
		return ERR_PTR(-ENOMEM);

	mutex_lock(&opts->lock);
	opts->refcnt++;
	mutex_unlock(&opts->lock);

	fsg->function.name	= FSG_DRIVER_DESC;
	fsg->function.bind	= fsg_bind;
	fsg->function.unbind	= fsg_unbind;
	fsg->function.setup	= fsg_setup;
	fsg->function.set_alt	= fsg_set_alt;
	fsg->function.disable	= fsg_disable;
	fsg->function.free_func	= fsg_free;

	fsg->common               = common;

	return &fsg->function;
}

DECLARE_USB_FUNCTION_INIT(mass_storage, fsg_alloc_inst, fsg_alloc);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Michal Nazarewicz");

/************************* Module parameters *************************/


void fsg_config_from_params(struct fsg_config *cfg,
		       const struct fsg_module_parameters *params,
		       unsigned int fsg_num_buffers)
{
	struct fsg_lun_config *lun;
	unsigned i;

	/* Configure LUNs */
	cfg->nluns =
		min(params->luns ?: (params->file_count ?: 1u),
		    (unsigned)FSG_MAX_LUNS);
	for (i = 0, lun = cfg->luns; i < cfg->nluns; ++i, ++lun) {
		lun->ro = !!params->ro[i];
		lun->cdrom = !!params->cdrom[i];
		lun->removable = !!params->removable[i];
		lun->filename =
			params->file_count > i && params->file[i][0]
			? params->file[i]
			: NULL;
	}

	/* Let MSF use defaults */
	cfg->vendor_name = NULL;
	cfg->product_name = NULL;

	cfg->ops = NULL;
	cfg->private_data = NULL;

	/* Finalise */
	cfg->can_stall = params->stall;
	cfg->fsg_num_buffers = fsg_num_buffers;
}
EXPORT_SYMBOL_GPL(fsg_config_from_params);
