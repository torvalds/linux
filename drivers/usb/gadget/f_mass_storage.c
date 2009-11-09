/*
 * f_mass_storage.c -- Mass Storage USB Composite Function
 *
 * Copyright (C) 2003-2008 Alan Stern
 * Copyright (C) 2009 Samsung Electronics
 *                    Author: Michal Nazarewicz <m.nazarewicz@samsung.com>
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
 * Function supports multiple logical units (LUNs).  Backing storage
 * for each LUN is provided by a regular file or a block device.
 * Access for each LUN can be limited to read-only.  Moreover, the
 * function can indicate that LUN is removable and/or CD-ROM.  (The
 * later implies read-only access.)
 *
 * MSF is configured by specifying a fsg_config structure.  It has the
 * following fields:
 *
 *	nluns		Number of LUNs function have (anywhere from 1
 *				to FSG_MAX_LUNS which is 8).
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
 *
 *	lun_name_format	A printf-like format for names of the LUN
 *				devices.  This determines how the
 *				directory in sysfs will be named.
 *				Unless you are using several MSFs in
 *				a single gadget (as opposed to single
 *				MSF in many configurations) you may
 *				leave it as NULL (in which case
 *				"lun%d" will be used).  In the format
 *				you can use "%d" to index LUNs for
 *				MSF's with more than one LUN.  (Beware
 *				that there is only one integer given
 *				as an argument for the format and
 *				specifying invalid format may cause
 *				unspecified behaviour.)
 *	thread_name	Name of the kernel thread process used by the
 *				MSF.  You can safely set it to NULL
 *				(in which case default "file-storage"
 *				will be used).
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
 * backing file per LUN.  Note also that the CD-ROM block length is
 * set to 512 rather than the more common value 2048.
 *
 *
 * MSF includes support for module parameters.  If gadget using it
 * decides to use it, the following module parameters will be
 * available:
 *
 *	file=filename[,filename...]
 *			Names of the files or block devices used for
 *				backing storage.
 *	ro=b[,b...]	Default false, boolean for read-only access.
 *	removable=b[,b...]
 *			Default true, boolean for removable media.
 *	cdrom=b[,b...]	Default false, boolean for whether to emulate
 *				a CD-ROM drive.
 *	luns=N		Default N = number of filenames, number of
 *				LUNs to support.
 *	stall		Default determined according to the type of
 *				USB device controller (usually true),
 *				boolean to permit the driver to halt
 *				bulk endpoints.
 *
 * The module parameters may be prefixed with some string.  You need
 * to consult gadget's documentation or source to verify whether it is
 * using those module parameters and if it does what are the prefixes
 * (look for FSG_MODULE_PARAMETERS() macro usage, what's inside it is
 * the prefix).
 *
 *
 * Requirements are modest; only a bulk-in and a bulk-out endpoint are
 * needed.  The memory requirement amounts to two 16K buffers, size
 * configurable by a parameter.  Support is included for both
 * full-speed and high-speed operation.
 *
 * Note that the driver is slightly non-portable in that it assumes a
 * single memory/DMA buffer will be useable for bulk-in, bulk-out, and
 * interrupt-in endpoints.  With most device controllers this isn't an
 * issue, but there may be some with hardware restrictions that prevent
 * a buffer from being used by more than one endpoint.
 *
 *
 * The pathnames of the backing files and the ro settings are
 * available in the attribute files "file" and "ro" in the lun<n> (or
 * to be more precise in a directory which name comes from
 * "lun_name_format" option!) subdirectory of the gadget's sysfs
 * directory.  If the "removable" option is set, writing to these
 * files will simulate ejecting/loading the medium (writing an empty
 * line means eject) and adjusting a write-enable tab.  Changes to the
 * ro setting are not allowed when the medium is loaded or if CD-ROM
 * emulation is being used.
 *
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
 * the gadget running when the thread is dead.  At of this moment, MSF
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
#include <linux/limits.h>
#include <linux/rwsem.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/freezer.h>
#include <linux/utsname.h>

#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>

#include "gadget_chips.h"



/*------------------------------------------------------------------------*/

#define FSG_DRIVER_DESC		"Mass Storage Function"
#define FSG_DRIVER_VERSION	"2009/09/11"

static const char fsg_string_interface[] = "Mass Storage";


#define FSG_NO_INTR_EP 1
#define FSG_BUFFHD_STATIC_BUFFER 1
#define FSG_NO_DEVICE_STRINGS    1
#define FSG_NO_OTG               1
#define FSG_NO_INTR_EP           1

#include "storage_common.c"


/*-------------------------------------------------------------------------*/


/* Data shared by all the FSG instances. */
struct fsg_common {
	struct usb_gadget	*gadget;

	/* filesem protects: backing files in use */
	struct rw_semaphore	filesem;

	struct fsg_buffhd	*next_buffhd_to_fill;
	struct fsg_buffhd	*next_buffhd_to_drain;
	struct fsg_buffhd	buffhds[FSG_NUM_BUFFERS];

	int			cmnd_size;
	u8			cmnd[MAX_COMMAND_SIZE];

	unsigned int		nluns;
	unsigned int		lun;
	struct fsg_lun		*luns;
	struct fsg_lun		*curlun;

	unsigned int		can_stall:1;
	unsigned int		free_storage_on_release:1;

	const char		*thread_name;

	/* Vendor (8 chars), product (16 chars), release (4
	 * hexadecimal digits) and NUL byte */
	char inquiry_string[8 + 16 + 4 + 1];

	struct kref		ref;
};


struct fsg_config {
	unsigned nluns;
	struct fsg_lun_config {
		const char *filename;
		char ro;
		char removable;
		char cdrom;
	} luns[FSG_MAX_LUNS];

	const char		*lun_name_format;
	const char		*thread_name;

	const char *vendor_name;		/*  8 characters or less */
	const char *product_name;		/* 16 characters or less */
	u16 release;

	char			can_stall;
};


struct fsg_dev {
	struct usb_function	function;
	struct usb_composite_dev *cdev;
	struct usb_gadget	*gadget;	/* Copy of cdev->gadget */
	struct fsg_common	*common;

	u16			interface_number;

	/* lock protects: state, all the req_busy's */
	spinlock_t		lock;

	struct usb_ep		*ep0;		/* Copy of gadget->ep0 */
	struct usb_request	*ep0req;	/* Copy of cdev->req */
	unsigned int		ep0_req_tag;
	const char		*ep0req_name;

	unsigned int		bulk_out_maxpacket;
	enum fsg_state		state;		/* For exception handling */
	unsigned int		exception_req_tag;

	u8			config, new_config;

	unsigned int		running:1;
	unsigned int		bulk_in_enabled:1;
	unsigned int		bulk_out_enabled:1;
	unsigned int		phase_error:1;
	unsigned int		short_packet_received:1;
	unsigned int		bad_lun_okay:1;
	unsigned int		can_stall:1;

	unsigned long		atomic_bitflags;
#define REGISTERED		0
#define IGNORE_BULK_OUT		1

	struct usb_ep		*bulk_in;
	struct usb_ep		*bulk_out;

	int			thread_wakeup_needed;
	struct completion	thread_notifier;
	struct task_struct	*thread_task;

	enum data_direction	data_dir;
	u32			data_size;
	u32			data_size_from_cmnd;
	u32			tag;
	u32			residue;
	u32			usb_amount_left;
};


static inline struct fsg_dev *fsg_from_func(struct usb_function *f)
{
	return container_of(f, struct fsg_dev, function);
}


typedef void (*fsg_routine_t)(struct fsg_dev *);

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

	/* Do nothing if a higher-priority exception is already in progress.
	 * If a lower-or-equal priority exception is in progress, preempt it
	 * and notify the main thread by sending it a signal. */
	spin_lock_irqsave(&fsg->lock, flags);
	if (fsg->state <= new_state) {
		fsg->exception_req_tag = fsg->ep0_req_tag;
		fsg->state = new_state;
		if (fsg->thread_task)
			send_sig_info(SIGUSR1, SEND_SIG_FORCED,
					fsg->thread_task);
	}
	spin_unlock_irqrestore(&fsg->lock, flags);
}


/*-------------------------------------------------------------------------*/

static int ep0_queue(struct fsg_dev *fsg)
{
	int	rc;

	rc = usb_ep_queue(fsg->ep0, fsg->ep0req, GFP_ATOMIC);
	fsg->ep0->driver_data = fsg;
	if (rc != 0 && rc != -ESHUTDOWN) {

		/* We can't do much more than wait for a reset */
		WARNING(fsg, "error in submission: %s --> %d\n",
				fsg->ep0->name, rc);
	}
	return rc;
}

/*-------------------------------------------------------------------------*/

/* Bulk and interrupt endpoint completion handlers.
 * These always run in_irq. */

static void bulk_in_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct fsg_dev		*fsg = ep->driver_data;
	struct fsg_buffhd	*bh = req->context;

	if (req->status || req->actual != req->length)
		DBG(fsg, "%s --> %d, %u/%u\n", __func__,
				req->status, req->actual, req->length);
	if (req->status == -ECONNRESET)		/* Request was cancelled */
		usb_ep_fifo_flush(ep);

	/* Hold the lock while we update the request and buffer states */
	smp_wmb();
	spin_lock(&fsg->lock);
	bh->inreq_busy = 0;
	bh->state = BUF_STATE_EMPTY;
	wakeup_thread(fsg);
	spin_unlock(&fsg->lock);
}

static void bulk_out_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct fsg_dev		*fsg = ep->driver_data;
	struct fsg_buffhd	*bh = req->context;

	dump_msg(fsg, "bulk-out", req->buf, req->actual);
	if (req->status || req->actual != bh->bulk_out_intended_length)
		DBG(fsg, "%s --> %d, %u/%u\n", __func__,
				req->status, req->actual,
				bh->bulk_out_intended_length);
	if (req->status == -ECONNRESET)		/* Request was cancelled */
		usb_ep_fifo_flush(ep);

	/* Hold the lock while we update the request and buffer states */
	smp_wmb();
	spin_lock(&fsg->lock);
	bh->outreq_busy = 0;
	bh->state = BUF_STATE_FULL;
	wakeup_thread(fsg);
	spin_unlock(&fsg->lock);
}


/*-------------------------------------------------------------------------*/

/* Ep0 class-specific handlers.  These always run in_irq. */

static int fsg_setup(struct usb_function *f,
		const struct usb_ctrlrequest *ctrl)
{
	struct fsg_dev		*fsg = fsg_from_func(f);
	struct usb_request	*req = fsg->ep0req;
	u16			w_index = le16_to_cpu(ctrl->wIndex);
	u16			w_value = le16_to_cpu(ctrl->wValue);
	u16			w_length = le16_to_cpu(ctrl->wLength);

	if (!fsg->config)
		return -EOPNOTSUPP;

	switch (ctrl->bRequest) {

	case USB_BULK_RESET_REQUEST:
		if (ctrl->bRequestType !=
		    (USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE))
			break;
		if (w_index != fsg->interface_number || w_value != 0)
			return -EDOM;

		/* Raise an exception to stop the current operation
		 * and reinitialize our state. */
		DBG(fsg, "bulk reset request\n");
		raise_exception(fsg, FSG_STATE_RESET);
		return DELAYED_STATUS;

	case USB_BULK_GET_MAX_LUN_REQUEST:
		if (ctrl->bRequestType !=
		    (USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE))
			break;
		if (w_index != fsg->interface_number || w_value != 0)
			return -EDOM;
		VDBG(fsg, "get max LUN\n");
		*(u8 *) req->buf = fsg->common->nluns - 1;
		return 1;
	}

	VDBG(fsg,
	     "unknown class-specific control req "
	     "%02x.%02x v%04x i%04x l%u\n",
	     ctrl->bRequestType, ctrl->bRequest,
	     le16_to_cpu(ctrl->wValue), w_index, w_length);
	return -EOPNOTSUPP;
}


/*-------------------------------------------------------------------------*/

/* All the following routines run in process context */


/* Use this for bulk or interrupt transfers, not ep0 */
static void start_transfer(struct fsg_dev *fsg, struct usb_ep *ep,
		struct usb_request *req, int *pbusy,
		enum fsg_buffer_state *state)
{
	int	rc;

	if (ep == fsg->bulk_in)
		dump_msg(fsg, "bulk-in", req->buf, req->length);

	spin_lock_irq(&fsg->lock);
	*pbusy = 1;
	*state = BUF_STATE_BUSY;
	spin_unlock_irq(&fsg->lock);
	rc = usb_ep_queue(ep, req, GFP_KERNEL);
	if (rc != 0) {
		*pbusy = 0;
		*state = BUF_STATE_EMPTY;

		/* We can't do much more than wait for a reset */

		/* Note: currently the net2280 driver fails zero-length
		 * submissions if DMA is enabled. */
		if (rc != -ESHUTDOWN && !(rc == -EOPNOTSUPP &&
						req->length == 0))
			WARNING(fsg, "error in submission: %s --> %d\n",
					ep->name, rc);
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
	struct fsg_lun		*curlun = fsg->common->curlun;
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
	if (fsg->common->cmnd[0] == SC_READ_6)
		lba = get_unaligned_be24(&fsg->common->cmnd[1]);
	else {
		lba = get_unaligned_be32(&fsg->common->cmnd[2]);

		/* We allow DPO (Disable Page Out = don't save data in the
		 * cache) and FUA (Force Unit Access = don't read from the
		 * cache), but we don't implement them. */
		if ((fsg->common->cmnd[1] & ~0x18) != 0) {
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
		amount = min(amount_left, FSG_BUFLEN);
		amount = min((loff_t) amount,
				curlun->file_length - file_offset);
		partial_page = file_offset & (PAGE_CACHE_SIZE - 1);
		if (partial_page > 0)
			amount = min(amount, (unsigned int) PAGE_CACHE_SIZE -
					partial_page);

		/* Wait for the next buffer to become available */
		bh = fsg->common->next_buffhd_to_fill;
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
		bh->inreq->zero = 0;
		start_transfer(fsg, fsg->bulk_in, bh->inreq,
				&bh->inreq_busy, &bh->state);
		fsg->common->next_buffhd_to_fill = bh->next;
	}

	return -EIO;		/* No default reply */
}


/*-------------------------------------------------------------------------*/

static int do_write(struct fsg_dev *fsg)
{
	struct fsg_lun		*curlun = fsg->common->curlun;
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
	spin_lock(&curlun->filp->f_lock);
	curlun->filp->f_flags &= ~O_SYNC;	/* Default is not to wait */
	spin_unlock(&curlun->filp->f_lock);

	/* Get the starting Logical Block Address and check that it's
	 * not too big */
	if (fsg->common->cmnd[0] == SC_WRITE_6)
		lba = get_unaligned_be24(&fsg->common->cmnd[1]);
	else {
		lba = get_unaligned_be32(&fsg->common->cmnd[2]);

		/* We allow DPO (Disable Page Out = don't save data in the
		 * cache) and FUA (Force Unit Access = write directly to the
		 * medium).  We don't implement DPO; we implement FUA by
		 * performing synchronous output. */
		if ((fsg->common->cmnd[1] & ~0x18) != 0) {
			curlun->sense_data = SS_INVALID_FIELD_IN_CDB;
			return -EINVAL;
		}
		if (fsg->common->cmnd[1] & 0x08) {	/* FUA */
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
	file_offset = usb_offset = ((loff_t) lba) << 9;
	amount_left_to_req = amount_left_to_write = fsg->data_size_from_cmnd;

	while (amount_left_to_write > 0) {

		/* Queue a request for more data from the host */
		bh = fsg->common->next_buffhd_to_fill;
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
			amount = min(amount_left_to_req, FSG_BUFLEN);
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
			bh->outreq->length = amount;
			bh->bulk_out_intended_length = amount;
			bh->outreq->short_not_ok = 1;
			start_transfer(fsg, fsg->bulk_out, bh->outreq,
					&bh->outreq_busy, &bh->state);
			fsg->common->next_buffhd_to_fill = bh->next;
			continue;
		}

		/* Write the received data to the backing file */
		bh = fsg->common->next_buffhd_to_drain;
		if (bh->state == BUF_STATE_EMPTY && !get_some_more)
			break;			/* We stopped early */
		if (bh->state == BUF_STATE_FULL) {
			smp_rmb();
			fsg->common->next_buffhd_to_drain = bh->next;
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

static int do_synchronize_cache(struct fsg_dev *fsg)
{
	struct fsg_lun	*curlun = fsg->common->curlun;
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
	struct inode	*inode = filp->f_path.dentry->d_inode;
	unsigned long	rc;

	rc = invalidate_mapping_pages(inode->i_mapping, 0, -1);
	VLDBG(curlun, "invalidate_inode_pages -> %ld\n", rc);
}

static int do_verify(struct fsg_dev *fsg)
{
	struct fsg_lun		*curlun = fsg->common->curlun;
	u32			lba;
	u32			verification_length;
	struct fsg_buffhd	*bh = fsg->common->next_buffhd_to_fill;
	loff_t			file_offset, file_offset_tmp;
	u32			amount_left;
	unsigned int		amount;
	ssize_t			nread;

	/* Get the starting Logical Block Address and check that it's
	 * not too big */
	lba = get_unaligned_be32(&fsg->common->cmnd[2]);
	if (lba >= curlun->num_sectors) {
		curlun->sense_data = SS_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE;
		return -EINVAL;
	}

	/* We allow DPO (Disable Page Out = don't save data in the
	 * cache) but we don't implement it. */
	if ((fsg->common->cmnd[1] & ~0x10) != 0) {
		curlun->sense_data = SS_INVALID_FIELD_IN_CDB;
		return -EINVAL;
	}

	verification_length = get_unaligned_be16(&fsg->common->cmnd[7]);
	if (unlikely(verification_length == 0))
		return -EIO;		/* No default reply */

	/* Prepare to carry out the file verify */
	amount_left = verification_length << 9;
	file_offset = ((loff_t) lba) << 9;

	/* Write out all the dirty buffers before invalidating them */
	fsg_lun_fsync_sub(curlun);
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
		amount = min(amount_left, FSG_BUFLEN);
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
	struct fsg_lun *curlun = fsg->common->curlun;
	u8	*buf = (u8 *) bh->buf;

	if (!curlun) {		/* Unsupported LUNs are okay */
		fsg->bad_lun_okay = 1;
		memset(buf, 0, 36);
		buf[0] = 0x7f;		/* Unsupported, no device-type */
		buf[4] = 31;		/* Additional length */
		return 36;
	}

	buf[0] = curlun->cdrom ? TYPE_CDROM : TYPE_DISK;
	buf[1] = curlun->removable ? 0x80 : 0;
	buf[2] = 2;		/* ANSI SCSI level 2 */
	buf[3] = 2;		/* SCSI-2 INQUIRY data format */
	buf[4] = 31;		/* Additional length */
	buf[5] = 0;		/* No special options */
	buf[6] = 0;
	buf[7] = 0;
	memcpy(buf + 8, fsg->common->inquiry_string,
	       sizeof fsg->common->inquiry_string);
	return 36;
}


static int do_request_sense(struct fsg_dev *fsg, struct fsg_buffhd *bh)
{
	struct fsg_lun	*curlun = fsg->common->curlun;
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
	put_unaligned_be32(sdinfo, &buf[3]);	/* Sense information */
	buf[7] = 18 - 8;			/* Additional sense length */
	buf[12] = ASC(sd);
	buf[13] = ASCQ(sd);
	return 18;
}


static int do_read_capacity(struct fsg_dev *fsg, struct fsg_buffhd *bh)
{
	struct fsg_lun	*curlun = fsg->common->curlun;
	u32		lba = get_unaligned_be32(&fsg->common->cmnd[2]);
	int		pmi = fsg->common->cmnd[8];
	u8		*buf = (u8 *) bh->buf;

	/* Check the PMI and LBA fields */
	if (pmi > 1 || (pmi == 0 && lba != 0)) {
		curlun->sense_data = SS_INVALID_FIELD_IN_CDB;
		return -EINVAL;
	}

	put_unaligned_be32(curlun->num_sectors - 1, &buf[0]);
						/* Max logical block */
	put_unaligned_be32(512, &buf[4]);	/* Block length */
	return 8;
}


static int do_read_header(struct fsg_dev *fsg, struct fsg_buffhd *bh)
{
	struct fsg_lun	*curlun = fsg->common->curlun;
	int		msf = fsg->common->cmnd[1] & 0x02;
	u32		lba = get_unaligned_be32(&fsg->common->cmnd[2]);
	u8		*buf = (u8 *) bh->buf;

	if ((fsg->common->cmnd[1] & ~0x02) != 0) {	/* Mask away MSF */
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


static int do_read_toc(struct fsg_dev *fsg, struct fsg_buffhd *bh)
{
	struct fsg_lun	*curlun = fsg->common->curlun;
	int		msf = fsg->common->cmnd[1] & 0x02;
	int		start_track = fsg->common->cmnd[6];
	u8		*buf = (u8 *) bh->buf;

	if ((fsg->common->cmnd[1] & ~0x02) != 0 ||	/* Mask away MSF */
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


static int do_mode_sense(struct fsg_dev *fsg, struct fsg_buffhd *bh)
{
	struct fsg_lun	*curlun = fsg->common->curlun;
	int		mscmnd = fsg->common->cmnd[0];
	u8		*buf = (u8 *) bh->buf;
	u8		*buf0 = buf;
	int		pc, page_code;
	int		changeable_values, all_pages;
	int		valid_page = 0;
	int		len, limit;

	if ((fsg->common->cmnd[1] & ~0x08) != 0) {	/* Mask away DBD */
		curlun->sense_data = SS_INVALID_FIELD_IN_CDB;
		return -EINVAL;
	}
	pc = fsg->common->cmnd[2] >> 6;
	page_code = fsg->common->cmnd[2] & 0x3f;
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
		limit = 65535;		/* Should really be FSG_BUFLEN */
	}

	/* No block descriptors */

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
		put_unaligned_be16(len - 2, buf0);
	return len;
}


static int do_start_stop(struct fsg_dev *fsg)
{
	if (!fsg->common->curlun) {
		return -EINVAL;
	} else if (!fsg->common->curlun->removable) {
		fsg->common->curlun->sense_data = SS_INVALID_COMMAND;
		return -EINVAL;
	}
	return 0;
}


static int do_prevent_allow(struct fsg_dev *fsg)
{
	struct fsg_lun	*curlun = fsg->common->curlun;
	int		prevent;

	if (!fsg->common->curlun) {
		return -EINVAL;
	} else if (!fsg->common->curlun->removable) {
		fsg->common->curlun->sense_data = SS_INVALID_COMMAND;
		return -EINVAL;
	}

	prevent = fsg->common->cmnd[4] & 0x01;
	if ((fsg->common->cmnd[4] & ~0x01) != 0) {	/* Mask away Prevent */
		curlun->sense_data = SS_INVALID_FIELD_IN_CDB;
		return -EINVAL;
	}

	if (curlun->prevent_medium_removal && !prevent)
		fsg_lun_fsync_sub(curlun);
	curlun->prevent_medium_removal = prevent;
	return 0;
}


static int do_read_format_capacities(struct fsg_dev *fsg,
			struct fsg_buffhd *bh)
{
	struct fsg_lun	*curlun = fsg->common->curlun;
	u8		*buf = (u8 *) bh->buf;

	buf[0] = buf[1] = buf[2] = 0;
	buf[3] = 8;	/* Only the Current/Maximum Capacity Descriptor */
	buf += 4;

	put_unaligned_be32(curlun->num_sectors, &buf[0]);
						/* Number of blocks */
	put_unaligned_be32(512, &buf[4]);	/* Block length */
	buf[4] = 0x02;				/* Current capacity */
	return 12;
}


static int do_mode_select(struct fsg_dev *fsg, struct fsg_buffhd *bh)
{
	struct fsg_lun	*curlun = fsg->common->curlun;

	/* We don't support MODE SELECT */
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

static int pad_with_zeros(struct fsg_dev *fsg)
{
	struct fsg_buffhd	*bh = fsg->common->next_buffhd_to_fill;
	u32			nkeep = bh->inreq->length;
	u32			nsend;
	int			rc;

	bh->state = BUF_STATE_EMPTY;		/* For the first iteration */
	fsg->usb_amount_left = nkeep + fsg->residue;
	while (fsg->usb_amount_left > 0) {

		/* Wait for the next buffer to be free */
		while (bh->state != BUF_STATE_EMPTY) {
			rc = sleep_thread(fsg);
			if (rc)
				return rc;
		}

		nsend = min(fsg->usb_amount_left, FSG_BUFLEN);
		memset(bh->buf + nkeep, 0, nsend - nkeep);
		bh->inreq->length = nsend;
		bh->inreq->zero = 0;
		start_transfer(fsg, fsg->bulk_in, bh->inreq,
				&bh->inreq_busy, &bh->state);
		bh = fsg->common->next_buffhd_to_fill = bh->next;
		fsg->usb_amount_left -= nsend;
		nkeep = 0;
	}
	return 0;
}

static int throw_away_data(struct fsg_dev *fsg)
{
	struct fsg_buffhd	*bh;
	u32			amount;
	int			rc;

	for (bh = fsg->common->next_buffhd_to_drain;
	     bh->state != BUF_STATE_EMPTY || fsg->usb_amount_left > 0;
	     bh = fsg->common->next_buffhd_to_drain) {

		/* Throw away the data in a filled buffer */
		if (bh->state == BUF_STATE_FULL) {
			smp_rmb();
			bh->state = BUF_STATE_EMPTY;
			fsg->common->next_buffhd_to_drain = bh->next;

			/* A short packet or an error ends everything */
			if (bh->outreq->actual != bh->outreq->length ||
					bh->outreq->status != 0) {
				raise_exception(fsg, FSG_STATE_ABORT_BULK_OUT);
				return -EINTR;
			}
			continue;
		}

		/* Try to submit another request if we need one */
		bh = fsg->common->next_buffhd_to_fill;
		if (bh->state == BUF_STATE_EMPTY && fsg->usb_amount_left > 0) {
			amount = min(fsg->usb_amount_left, FSG_BUFLEN);

			/* amount is always divisible by 512, hence by
			 * the bulk-out maxpacket size */
			bh->outreq->length = amount;
			bh->bulk_out_intended_length = amount;
			bh->outreq->short_not_ok = 1;
			start_transfer(fsg, fsg->bulk_out, bh->outreq,
					&bh->outreq_busy, &bh->state);
			fsg->common->next_buffhd_to_fill = bh->next;
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
	struct fsg_buffhd	*bh = fsg->common->next_buffhd_to_fill;
	int			rc = 0;

	switch (fsg->data_dir) {
	case DATA_DIR_NONE:
		break;			/* Nothing to send */

	/* If we don't know whether the host wants to read or write,
	 * this must be CB or CBI with an unknown command.  We mustn't
	 * try to send or receive any data.  So stall both bulk pipes
	 * if we can and wait for a reset. */
	case DATA_DIR_UNKNOWN:
		if (fsg->can_stall) {
			fsg_set_halt(fsg, fsg->bulk_out);
			rc = halt_bulk_in_endpoint(fsg);
		}
		break;

	/* All but the last buffer of data must have already been sent */
	case DATA_DIR_TO_HOST:
		if (fsg->data_size == 0) {
			/* Nothing to send */

		/* If there's no residue, simply send the last buffer */
		} else if (fsg->residue == 0) {
			bh->inreq->zero = 0;
			start_transfer(fsg, fsg->bulk_in, bh->inreq,
					&bh->inreq_busy, &bh->state);
			fsg->common->next_buffhd_to_fill = bh->next;

		/* For Bulk-only, if we're allowed to stall then send the
		 * short packet and halt the bulk-in endpoint.  If we can't
		 * stall, pad out the remaining data with 0's. */
		} else if (fsg->can_stall) {
			bh->inreq->zero = 1;
			start_transfer(fsg, fsg->bulk_in, bh->inreq,
				       &bh->inreq_busy, &bh->state);
			fsg->common->next_buffhd_to_fill = bh->next;
			rc = halt_bulk_in_endpoint(fsg);
		} else {
			rc = pad_with_zeros(fsg);
		}
		break;

	/* We have processed all we want from the data the host has sent.
	 * There may still be outstanding bulk-out requests. */
	case DATA_DIR_FROM_HOST:
		if (fsg->residue == 0) {
			/* Nothing to receive */

		/* Did the host stop sending unexpectedly early? */
		} else if (fsg->short_packet_received) {
			raise_exception(fsg, FSG_STATE_ABORT_BULK_OUT);
			rc = -EINTR;

		/* We haven't processed all the incoming data.  Even though
		 * we may be allowed to stall, doing so would cause a race.
		 * The controller may already have ACK'ed all the remaining
		 * bulk-out packets, in which case the host wouldn't see a
		 * STALL.  Not realizing the endpoint was halted, it wouldn't
		 * clear the halt -- leading to problems later on. */
#if 0
		} else if (fsg->can_stall) {
			fsg_set_halt(fsg, fsg->bulk_out);
			raise_exception(fsg, FSG_STATE_ABORT_BULK_OUT);
			rc = -EINTR;
#endif

		/* We can't stall.  Read in the excess data and throw it
		 * all away. */
		} else {
			rc = throw_away_data(fsg);
		}
		break;
	}
	return rc;
}


static int send_status(struct fsg_dev *fsg)
{
	struct fsg_lun		*curlun = fsg->common->curlun;
	struct fsg_buffhd	*bh;
	struct bulk_cs_wrap	*csw;
	int			rc;
	u8			status = USB_STATUS_PASS;
	u32			sd, sdinfo = 0;

	/* Wait for the next buffer to become available */
	bh = fsg->common->next_buffhd_to_fill;
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

	/* Store and send the Bulk-only CSW */
	csw = (void *)bh->buf;

	csw->Signature = cpu_to_le32(USB_BULK_CS_SIG);
	csw->Tag = fsg->tag;
	csw->Residue = cpu_to_le32(fsg->residue);
	csw->Status = status;

	bh->inreq->length = USB_BULK_CS_WRAP_LEN;
	bh->inreq->zero = 0;
	start_transfer(fsg, fsg->bulk_in, bh->inreq,
		       &bh->inreq_busy, &bh->state);

	fsg->common->next_buffhd_to_fill = bh->next;
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
	int			lun = fsg->common->cmnd[1] >> 5;
	static const char	dirletter[4] = {'u', 'o', 'i', 'n'};
	char			hdlen[20];
	struct fsg_lun		*curlun;

	hdlen[0] = 0;
	if (fsg->data_dir != DATA_DIR_UNKNOWN)
		sprintf(hdlen, ", H%c=%u", dirletter[(int) fsg->data_dir],
				fsg->data_size);
	VDBG(fsg, "SCSI command: %s;  Dc=%d, D%c=%u;  Hc=%d%s\n",
	     name, cmnd_size, dirletter[(int) data_dir],
	     fsg->data_size_from_cmnd, fsg->common->cmnd_size, hdlen);

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
			fsg->data_size_from_cmnd = fsg->data_size;
			fsg->phase_error = 1;
		}
	}
	fsg->residue = fsg->usb_amount_left = fsg->data_size;

	/* Conflicting data directions is a phase error */
	if (fsg->data_dir != data_dir && fsg->data_size_from_cmnd > 0) {
		fsg->phase_error = 1;
		return -EINVAL;
	}

	/* Verify the length of the command itself */
	if (cmnd_size != fsg->common->cmnd_size) {

		/* Special case workaround: There are plenty of buggy SCSI
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
		if (cmnd_size <= fsg->common->cmnd_size) {
			DBG(fsg, "%s is buggy! Expected length %d "
			    "but we got %d\n", name,
			    cmnd_size, fsg->common->cmnd_size);
			cmnd_size = fsg->common->cmnd_size;
		} else {
			fsg->phase_error = 1;
			return -EINVAL;
		}
	}

	/* Check that the LUN values are consistent */
	if (fsg->common->lun != lun)
		DBG(fsg, "using LUN %d from CBW, not LUN %d from CDB\n",
		    fsg->common->lun, lun);

	/* Check the LUN */
	if (fsg->common->lun >= 0 && fsg->common->lun < fsg->common->nluns) {
		curlun = &fsg->common->luns[fsg->common->lun];
		fsg->common->curlun = curlun;
		if (fsg->common->cmnd[0] != SC_REQUEST_SENSE) {
			curlun->sense_data = SS_NO_SENSE;
			curlun->sense_data_info = 0;
			curlun->info_valid = 0;
		}
	} else {
		fsg->common->curlun = curlun = NULL;
		fsg->bad_lun_okay = 0;

		/* INQUIRY and REQUEST SENSE commands are explicitly allowed
		 * to use unsupported LUNs; all others may not. */
		if (fsg->common->cmnd[0] != SC_INQUIRY &&
		    fsg->common->cmnd[0] != SC_REQUEST_SENSE) {
			DBG(fsg, "unsupported LUN %d\n", fsg->common->lun);
			return -EINVAL;
		}
	}

	/* If a unit attention condition exists, only INQUIRY and
	 * REQUEST SENSE commands are allowed; anything else must fail. */
	if (curlun && curlun->unit_attention_data != SS_NO_SENSE &&
			fsg->common->cmnd[0] != SC_INQUIRY &&
			fsg->common->cmnd[0] != SC_REQUEST_SENSE) {
		curlun->sense_data = curlun->unit_attention_data;
		curlun->unit_attention_data = SS_NO_SENSE;
		return -EINVAL;
	}

	/* Check that only command bytes listed in the mask are non-zero */
	fsg->common->cmnd[1] &= 0x1f;			/* Mask away the LUN */
	for (i = 1; i < cmnd_size; ++i) {
		if (fsg->common->cmnd[i] && !(mask & (1 << i))) {
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


static int do_scsi_command(struct fsg_dev *fsg)
{
	struct fsg_buffhd	*bh;
	int			rc;
	int			reply = -EINVAL;
	int			i;
	static char		unknown[16];

	dump_cdb(fsg->common);

	/* Wait for the next buffer to become available for data or status */
	bh = fsg->common->next_buffhd_to_fill;
	fsg->common->next_buffhd_to_drain = bh;
	while (bh->state != BUF_STATE_EMPTY) {
		rc = sleep_thread(fsg);
		if (rc)
			return rc;
	}
	fsg->phase_error = 0;
	fsg->short_packet_received = 0;

	/* We're using the backing file */
	down_read(&fsg->common->filesem);
	switch (fsg->common->cmnd[0]) {

	case SC_INQUIRY:
		fsg->data_size_from_cmnd = fsg->common->cmnd[4];
		reply = check_command(fsg, 6, DATA_DIR_TO_HOST,
				      (1<<4), 0,
				      "INQUIRY");
		if (reply == 0)
			reply = do_inquiry(fsg, bh);
		break;

	case SC_MODE_SELECT_6:
		fsg->data_size_from_cmnd = fsg->common->cmnd[4];
		reply = check_command(fsg, 6, DATA_DIR_FROM_HOST,
				      (1<<1) | (1<<4), 0,
				      "MODE SELECT(6)");
		if (reply == 0)
			reply = do_mode_select(fsg, bh);
		break;

	case SC_MODE_SELECT_10:
		fsg->data_size_from_cmnd =
			get_unaligned_be16(&fsg->common->cmnd[7]);
		reply = check_command(fsg, 10, DATA_DIR_FROM_HOST,
				      (1<<1) | (3<<7), 0,
				      "MODE SELECT(10)");
		if (reply == 0)
			reply = do_mode_select(fsg, bh);
		break;

	case SC_MODE_SENSE_6:
		fsg->data_size_from_cmnd = fsg->common->cmnd[4];
		reply = check_command(fsg, 6, DATA_DIR_TO_HOST,
				      (1<<1) | (1<<2) | (1<<4), 0,
				      "MODE SENSE(6)");
		if (reply == 0)
			reply = do_mode_sense(fsg, bh);
		break;

	case SC_MODE_SENSE_10:
		fsg->data_size_from_cmnd =
			get_unaligned_be16(&fsg->common->cmnd[7]);
		reply = check_command(fsg, 10, DATA_DIR_TO_HOST,
				      (1<<1) | (1<<2) | (3<<7), 0,
				      "MODE SENSE(10)");
		if (reply == 0)
			reply = do_mode_sense(fsg, bh);
		break;

	case SC_PREVENT_ALLOW_MEDIUM_REMOVAL:
		fsg->data_size_from_cmnd = 0;
		reply = check_command(fsg, 6, DATA_DIR_NONE,
				      (1<<4), 0,
				      "PREVENT-ALLOW MEDIUM REMOVAL");
		if (reply == 0)
			reply = do_prevent_allow(fsg);
		break;

	case SC_READ_6:
		i = fsg->common->cmnd[4];
		fsg->data_size_from_cmnd = (i == 0 ? 256 : i) << 9;
		reply = check_command(fsg, 6, DATA_DIR_TO_HOST,
				      (7<<1) | (1<<4), 1,
				      "READ(6)");
		if (reply == 0)
			reply = do_read(fsg);
		break;

	case SC_READ_10:
		fsg->data_size_from_cmnd =
				get_unaligned_be16(&fsg->common->cmnd[7]) << 9;
		reply = check_command(fsg, 10, DATA_DIR_TO_HOST,
				      (1<<1) | (0xf<<2) | (3<<7), 1,
				      "READ(10)");
		if (reply == 0)
			reply = do_read(fsg);
		break;

	case SC_READ_12:
		fsg->data_size_from_cmnd =
				get_unaligned_be32(&fsg->common->cmnd[6]) << 9;
		reply = check_command(fsg, 12, DATA_DIR_TO_HOST,
				      (1<<1) | (0xf<<2) | (0xf<<6), 1,
				      "READ(12)");
		if (reply == 0)
			reply = do_read(fsg);
		break;

	case SC_READ_CAPACITY:
		fsg->data_size_from_cmnd = 8;
		reply = check_command(fsg, 10, DATA_DIR_TO_HOST,
				      (0xf<<2) | (1<<8), 1,
				      "READ CAPACITY");
		if (reply == 0)
			reply = do_read_capacity(fsg, bh);
		break;

	case SC_READ_HEADER:
		if (!fsg->common->curlun || !fsg->common->curlun->cdrom)
			goto unknown_cmnd;
		fsg->data_size_from_cmnd =
			get_unaligned_be16(&fsg->common->cmnd[7]);
		reply = check_command(fsg, 10, DATA_DIR_TO_HOST,
				      (3<<7) | (0x1f<<1), 1,
				      "READ HEADER");
		if (reply == 0)
			reply = do_read_header(fsg, bh);
		break;

	case SC_READ_TOC:
		if (!fsg->common->curlun || !fsg->common->curlun->cdrom)
			goto unknown_cmnd;
		fsg->data_size_from_cmnd =
			get_unaligned_be16(&fsg->common->cmnd[7]);
		reply = check_command(fsg, 10, DATA_DIR_TO_HOST,
				      (7<<6) | (1<<1), 1,
				      "READ TOC");
		if (reply == 0)
			reply = do_read_toc(fsg, bh);
		break;

	case SC_READ_FORMAT_CAPACITIES:
		fsg->data_size_from_cmnd =
			get_unaligned_be16(&fsg->common->cmnd[7]);
		reply = check_command(fsg, 10, DATA_DIR_TO_HOST,
				      (3<<7), 1,
				      "READ FORMAT CAPACITIES");
		if (reply == 0)
			reply = do_read_format_capacities(fsg, bh);
		break;

	case SC_REQUEST_SENSE:
		fsg->data_size_from_cmnd = fsg->common->cmnd[4];
		reply = check_command(fsg, 6, DATA_DIR_TO_HOST,
				      (1<<4), 0,
				      "REQUEST SENSE");
		if (reply == 0)
			reply = do_request_sense(fsg, bh);
		break;

	case SC_START_STOP_UNIT:
		fsg->data_size_from_cmnd = 0;
		reply = check_command(fsg, 6, DATA_DIR_NONE,
				      (1<<1) | (1<<4), 0,
				      "START-STOP UNIT");
		if (reply == 0)
			reply = do_start_stop(fsg);
		break;

	case SC_SYNCHRONIZE_CACHE:
		fsg->data_size_from_cmnd = 0;
		reply = check_command(fsg, 10, DATA_DIR_NONE,
				      (0xf<<2) | (3<<7), 1,
				      "SYNCHRONIZE CACHE");
		if (reply == 0)
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
		reply = check_command(fsg, 10, DATA_DIR_NONE,
				      (1<<1) | (0xf<<2) | (3<<7), 1,
				      "VERIFY");
		if (reply == 0)
			reply = do_verify(fsg);
		break;

	case SC_WRITE_6:
		i = fsg->common->cmnd[4];
		fsg->data_size_from_cmnd = (i == 0 ? 256 : i) << 9;
		reply = check_command(fsg, 6, DATA_DIR_FROM_HOST,
				      (7<<1) | (1<<4), 1,
				      "WRITE(6)");
		if (reply == 0)
			reply = do_write(fsg);
		break;

	case SC_WRITE_10:
		fsg->data_size_from_cmnd =
				get_unaligned_be16(&fsg->common->cmnd[7]) << 9;
		reply = check_command(fsg, 10, DATA_DIR_FROM_HOST,
				      (1<<1) | (0xf<<2) | (3<<7), 1,
				      "WRITE(10)");
		if (reply == 0)
			reply = do_write(fsg);
		break;

	case SC_WRITE_12:
		fsg->data_size_from_cmnd =
				get_unaligned_be32(&fsg->common->cmnd[6]) << 9;
		reply = check_command(fsg, 12, DATA_DIR_FROM_HOST,
				      (1<<1) | (0xf<<2) | (0xf<<6), 1,
				      "WRITE(12)");
		if (reply == 0)
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
unknown_cmnd:
		fsg->data_size_from_cmnd = 0;
		sprintf(unknown, "Unknown x%02x", fsg->common->cmnd[0]);
		reply = check_command(fsg, fsg->common->cmnd_size,
				      DATA_DIR_UNKNOWN, 0xff, 0, unknown);
		if (reply == 0) {
			fsg->common->curlun->sense_data = SS_INVALID_COMMAND;
			reply = -EINVAL;
		}
		break;
	}
	up_read(&fsg->common->filesem);

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
	struct usb_request		*req = bh->outreq;
	struct fsg_bulk_cb_wrap	*cbw = req->buf;

	/* Was this a real packet?  Should it be ignored? */
	if (req->status || test_bit(IGNORE_BULK_OUT, &fsg->atomic_bitflags))
		return -EINVAL;

	/* Is the CBW valid? */
	if (req->actual != USB_BULK_CB_WRAP_LEN ||
			cbw->Signature != cpu_to_le32(
				USB_BULK_CB_SIG)) {
		DBG(fsg, "invalid CBW: len %u sig 0x%x\n",
				req->actual,
				le32_to_cpu(cbw->Signature));

		/* The Bulk-only spec says we MUST stall the IN endpoint
		 * (6.6.1), so it's unavoidable.  It also says we must
		 * retain this state until the next reset, but there's
		 * no way to tell the controller driver it should ignore
		 * Clear-Feature(HALT) requests.
		 *
		 * We aren't required to halt the OUT endpoint; instead
		 * we can simply accept and discard any data received
		 * until the next reset. */
		wedge_bulk_in_endpoint(fsg);
		set_bit(IGNORE_BULK_OUT, &fsg->atomic_bitflags);
		return -EINVAL;
	}

	/* Is the CBW meaningful? */
	if (cbw->Lun >= FSG_MAX_LUNS || cbw->Flags & ~USB_BULK_IN_FLAG ||
			cbw->Length <= 0 || cbw->Length > MAX_COMMAND_SIZE) {
		DBG(fsg, "non-meaningful CBW: lun = %u, flags = 0x%x, "
				"cmdlen %u\n",
				cbw->Lun, cbw->Flags, cbw->Length);

		/* We can do anything we want here, so let's stall the
		 * bulk pipes if we are allowed to. */
		if (fsg->can_stall) {
			fsg_set_halt(fsg, fsg->bulk_out);
			halt_bulk_in_endpoint(fsg);
		}
		return -EINVAL;
	}

	/* Save the command for later */
	fsg->common->cmnd_size = cbw->Length;
	memcpy(fsg->common->cmnd, cbw->CDB, fsg->common->cmnd_size);
	if (cbw->Flags & USB_BULK_IN_FLAG)
		fsg->data_dir = DATA_DIR_TO_HOST;
	else
		fsg->data_dir = DATA_DIR_FROM_HOST;
	fsg->data_size = le32_to_cpu(cbw->DataTransferLength);
	if (fsg->data_size == 0)
		fsg->data_dir = DATA_DIR_NONE;
	fsg->common->lun = cbw->Lun;
	fsg->tag = cbw->Tag;
	return 0;
}


static int get_next_command(struct fsg_dev *fsg)
{
	struct fsg_buffhd	*bh;
	int			rc = 0;

	/* Wait for the next buffer to become available */
	bh = fsg->common->next_buffhd_to_fill;
	while (bh->state != BUF_STATE_EMPTY) {
		rc = sleep_thread(fsg);
		if (rc)
			return rc;
	}

	/* Queue a request to read a Bulk-only CBW */
	set_bulk_out_req_length(fsg, bh, USB_BULK_CB_WRAP_LEN);
	bh->outreq->short_not_ok = 1;
	start_transfer(fsg, fsg->bulk_out, bh->outreq,
		       &bh->outreq_busy, &bh->state);

	/* We will drain the buffer in software, which means we
	 * can reuse it for the next filling.  No need to advance
	 * next_buffhd_to_fill. */

	/* Wait for the CBW to arrive */
	while (bh->state != BUF_STATE_FULL) {
		rc = sleep_thread(fsg);
		if (rc)
			return rc;
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
	int	rc = 0;
	int	i;
	const struct usb_endpoint_descriptor	*d;

	if (fsg->running)
		DBG(fsg, "reset interface\n");

reset:
	/* Deallocate the requests */
	for (i = 0; i < FSG_NUM_BUFFERS; ++i) {
		struct fsg_buffhd *bh = &fsg->common->buffhds[i];

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

	fsg->running = 0;
	if (altsetting < 0 || rc != 0)
		return rc;

	DBG(fsg, "set interface %d\n", altsetting);

	/* Enable the endpoints */
	d = fsg_ep_desc(fsg->gadget,
			&fsg_fs_bulk_in_desc, &fsg_hs_bulk_in_desc);
	rc = enable_endpoint(fsg, fsg->bulk_in, d);
	if (rc != 0)
		goto reset;
	fsg->bulk_in_enabled = 1;

	d = fsg_ep_desc(fsg->gadget,
			&fsg_fs_bulk_out_desc, &fsg_hs_bulk_out_desc);
	rc = enable_endpoint(fsg, fsg->bulk_out, d);
	if (rc != 0)
		goto reset;
	fsg->bulk_out_enabled = 1;
	fsg->bulk_out_maxpacket = le16_to_cpu(d->wMaxPacketSize);
	clear_bit(IGNORE_BULK_OUT, &fsg->atomic_bitflags);

	/* Allocate the requests */
	for (i = 0; i < FSG_NUM_BUFFERS; ++i) {
		struct fsg_buffhd	*bh = &fsg->common->buffhds[i];

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
	for (i = 0; i < fsg->common->nluns; ++i)
		fsg->common->luns[i].unit_attention_data = SS_RESET_OCCURRED;
	return rc;
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
		rc = do_set_interface(fsg, 0);
		if (rc != 0)
			fsg->config = 0;	/* Reset on errors */
	}
	return rc;
}


/****************************** ALT CONFIGS ******************************/


static int fsg_set_alt(struct usb_function *f, unsigned intf, unsigned alt)
{
	struct fsg_dev *fsg = fsg_from_func(f);
	fsg->new_config = 1;
	raise_exception(fsg, FSG_STATE_CONFIG_CHANGE);
	return 0;
}

static void fsg_disable(struct usb_function *f)
{
	struct fsg_dev *fsg = fsg_from_func(f);
	fsg->new_config = 0;
	raise_exception(fsg, FSG_STATE_CONFIG_CHANGE);
}


/*-------------------------------------------------------------------------*/

static void handle_exception(struct fsg_dev *fsg)
{
	siginfo_t		info;
	int			sig;
	int			i;
	struct fsg_buffhd	*bh;
	enum fsg_state		old_state;
	u8			new_config;
	struct fsg_lun		*curlun;
	unsigned int		exception_req_tag;
	int			rc;

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
	for (i = 0; i < FSG_NUM_BUFFERS; ++i) {
		bh = &fsg->common->buffhds[i];
		if (bh->inreq_busy)
			usb_ep_dequeue(fsg->bulk_in, bh->inreq);
		if (bh->outreq_busy)
			usb_ep_dequeue(fsg->bulk_out, bh->outreq);
	}

	/* Wait until everything is idle */
	for (;;) {
		int num_active = 0;
		for (i = 0; i < FSG_NUM_BUFFERS; ++i) {
			bh = &fsg->common->buffhds[i];
			num_active += bh->inreq_busy + bh->outreq_busy;
		}
		if (num_active == 0)
			break;
		if (sleep_thread(fsg))
			return;
	}

	/* Clear out the controller's fifos */
	if (fsg->bulk_in_enabled)
		usb_ep_fifo_flush(fsg->bulk_in);
	if (fsg->bulk_out_enabled)
		usb_ep_fifo_flush(fsg->bulk_out);

	/* Reset the I/O buffer states and pointers, the SCSI
	 * state, and the exception.  Then invoke the handler. */
	spin_lock_irq(&fsg->lock);

	for (i = 0; i < FSG_NUM_BUFFERS; ++i) {
		bh = &fsg->common->buffhds[i];
		bh->state = BUF_STATE_EMPTY;
	}
	fsg->common->next_buffhd_to_fill = &fsg->common->buffhds[0];
	fsg->common->next_buffhd_to_drain = &fsg->common->buffhds[0];
	exception_req_tag = fsg->exception_req_tag;
	new_config = fsg->new_config;
	old_state = fsg->state;

	if (old_state == FSG_STATE_ABORT_BULK_OUT)
		fsg->state = FSG_STATE_STATUS_PHASE;
	else {
		for (i = 0; i < fsg->common->nluns; ++i) {
			curlun = &fsg->common->luns[i];
			curlun->prevent_medium_removal = 0;
			curlun->sense_data = SS_NO_SENSE;
			curlun->unit_attention_data = SS_NO_SENSE;
			curlun->sense_data_info = 0;
			curlun->info_valid = 0;
		}
		fsg->state = FSG_STATE_IDLE;
	}
	spin_unlock_irq(&fsg->lock);

	/* Carry out any extra actions required for the exception */
	switch (old_state) {
	case FSG_STATE_ABORT_BULK_OUT:
		send_status(fsg);
		spin_lock_irq(&fsg->lock);
		if (fsg->state == FSG_STATE_STATUS_PHASE)
			fsg->state = FSG_STATE_IDLE;
		spin_unlock_irq(&fsg->lock);
		break;

	case FSG_STATE_RESET:
		/* In case we were forced against our will to halt a
		 * bulk endpoint, clear the halt now.  (The SuperH UDC
		 * requires this.) */
		if (test_and_clear_bit(IGNORE_BULK_OUT, &fsg->atomic_bitflags))
			usb_ep_clear_halt(fsg->bulk_in);

		if (fsg->ep0_req_tag == exception_req_tag)
			ep0_queue(fsg);	/* Complete the status stage */

		/* Technically this should go here, but it would only be
		 * a waste of time.  Ditto for the INTERFACE_CHANGE and
		 * CONFIG_CHANGE cases. */
		/* for (i = 0; i < fsg->common->nluns; ++i) */
		/*	fsg->common->luns[i].unit_attention_data = */
		/*		SS_RESET_OCCURRED;  */
		break;

	case FSG_STATE_CONFIG_CHANGE:
		rc = do_set_config(fsg, new_config);
		if (fsg->ep0_req_tag != exception_req_tag)
			break;
		if (rc != 0)			/* STALL on errors */
			fsg_set_halt(fsg, fsg->ep0);
		else				/* Complete the status stage */
			ep0_queue(fsg);
		break;

	case FSG_STATE_EXIT:
	case FSG_STATE_TERMINATED:
		do_set_config(fsg, 0);			/* Free resources */
		spin_lock_irq(&fsg->lock);
		fsg->state = FSG_STATE_TERMINATED;	/* Stop the thread */
		spin_unlock_irq(&fsg->lock);
		break;

	case FSG_STATE_INTERFACE_CHANGE:
	case FSG_STATE_DISCONNECT:
	case FSG_STATE_COMMAND_PHASE:
	case FSG_STATE_DATA_PHASE:
	case FSG_STATE_STATUS_PHASE:
	case FSG_STATE_IDLE:
		break;
	}
}


/*-------------------------------------------------------------------------*/

static int fsg_main_thread(void *fsg_)
{
	struct fsg_dev		*fsg = fsg_;

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

		spin_lock_irq(&fsg->lock);
		if (!exception_in_progress(fsg))
			fsg->state = FSG_STATE_DATA_PHASE;
		spin_unlock_irq(&fsg->lock);

		if (do_scsi_command(fsg) || finish_reply(fsg))
			continue;

		spin_lock_irq(&fsg->lock);
		if (!exception_in_progress(fsg))
			fsg->state = FSG_STATE_STATUS_PHASE;
		spin_unlock_irq(&fsg->lock);

		if (send_status(fsg))
			continue;

		spin_lock_irq(&fsg->lock);
		if (!exception_in_progress(fsg))
			fsg->state = FSG_STATE_IDLE;
		spin_unlock_irq(&fsg->lock);
	}

	spin_lock_irq(&fsg->lock);
	fsg->thread_task = NULL;
	spin_unlock_irq(&fsg->lock);

	/* XXX */
	/* If we are exiting because of a signal, unregister the
	 * gadget driver. */
	/* if (test_and_clear_bit(REGISTERED, &fsg->atomic_bitflags)) */
	/* 	usb_gadget_unregister_driver(&fsg_driver); */

	/* Let the unbind and cleanup routines know the thread has exited */
	complete_and_exit(&fsg->thread_notifier, 0);
}


/*************************** DEVICE ATTRIBUTES ***************************/

/* Write permission is checked per LUN in store_*() functions. */
static DEVICE_ATTR(ro, 0644, fsg_show_ro, fsg_store_ro);
static DEVICE_ATTR(file, 0644, fsg_show_file, fsg_store_file);


/****************************** FSG COMMON ******************************/

static void fsg_common_release(struct kref *ref);

static void fsg_lun_release(struct device *dev)
{
	/* Nothing needs to be done */
}

static inline void fsg_common_get(struct fsg_common *common)
{
	kref_get(&common->ref);
}

static inline void fsg_common_put(struct fsg_common *common)
{
	kref_put(&common->ref, fsg_common_release);
}


static struct fsg_common *fsg_common_init(struct fsg_common *common,
					  struct usb_composite_dev *cdev,
					  struct fsg_config *cfg)
{
	struct usb_gadget *gadget = cdev->gadget;
	struct fsg_buffhd *bh;
	struct fsg_lun *curlun;
	struct fsg_lun_config *lcfg;
	int nluns, i, rc;
	char *pathbuf;

	/* Find out how many LUNs there should be */
	nluns = cfg->nluns;
	if (nluns < 1 || nluns > FSG_MAX_LUNS) {
		dev_err(&gadget->dev, "invalid number of LUNs: %u\n", nluns);
		return ERR_PTR(-EINVAL);
	}

	/* Allocate? */
	if (!common) {
		common = kzalloc(sizeof *common, GFP_KERNEL);
		if (!common)
			return ERR_PTR(-ENOMEM);
		common->free_storage_on_release = 1;
	} else {
		memset(common, 0, sizeof common);
		common->free_storage_on_release = 0;
	}
	common->gadget = gadget;

	/* Create the LUNs, open their backing files, and register the
	 * LUN devices in sysfs. */
	curlun = kzalloc(nluns * sizeof *curlun, GFP_KERNEL);
	if (!curlun) {
		kfree(common);
		return ERR_PTR(-ENOMEM);
	}
	common->luns = curlun;

	init_rwsem(&common->filesem);

	for (i = 0, lcfg = cfg->luns; i < nluns; ++i, ++curlun, ++lcfg) {
		curlun->cdrom = !!lcfg->cdrom;
		curlun->ro = lcfg->cdrom || lcfg->ro;
		curlun->removable = lcfg->removable;
		curlun->dev.release = fsg_lun_release;
		curlun->dev.parent = &gadget->dev;
		/* curlun->dev.driver = &fsg_driver.driver; XXX */
		dev_set_drvdata(&curlun->dev, &common->filesem);
		dev_set_name(&curlun->dev,
			     cfg->lun_name_format
			   ? cfg->lun_name_format
			   : "lun%d",
			     i);

		rc = device_register(&curlun->dev);
		if (rc) {
			INFO(common, "failed to register LUN%d: %d\n", i, rc);
			common->nluns = i;
			goto error_release;
		}

		rc = device_create_file(&curlun->dev, &dev_attr_ro);
		if (rc)
			goto error_luns;
		rc = device_create_file(&curlun->dev, &dev_attr_file);
		if (rc)
			goto error_luns;

		if (lcfg->filename) {
			rc = fsg_lun_open(curlun, lcfg->filename);
			if (rc)
				goto error_luns;
		} else if (!curlun->removable) {
			ERROR(common, "no file given for LUN%d\n", i);
			rc = -EINVAL;
			goto error_luns;
		}
	}
	common->nluns = nluns;


	/* Data buffers cyclic list */
	/* Buffers in buffhds are static -- no need for additional
	 * allocation. */
	bh = common->buffhds;
	i = FSG_NUM_BUFFERS - 1;
	do {
		bh->next = bh + 1;
	} while (++bh, --i);
	bh->next = common->buffhds;


	/* Prepare inquiryString */
	if (cfg->release != 0xffff) {
		i = cfg->release;
	} else {
		/* The sa1100 controller is not supported */
		i = gadget_is_sa1100(gadget)
			? -1
			: usb_gadget_controller_number(gadget);
		if (i >= 0) {
			i = 0x0300 + i;
		} else {
			WARNING(common, "controller '%s' not recognized\n",
				gadget->name);
			i = 0x0399;
		}
	}
#define OR(x, y) ((x) ? (x) : (y))
	snprintf(common->inquiry_string, sizeof common->inquiry_string,
		 "%-8s%-16s%04x",
		 OR(cfg->vendor_name, "Linux   "),
		 /* Assume product name dependent on the first LUN */
		 OR(cfg->product_name, common->luns->cdrom
				     ? "File-Stor Gadget"
				     : "File-CD Gadget  "),
		 i);


	/* Some peripheral controllers are known not to be able to
	 * halt bulk endpoints correctly.  If one of them is present,
	 * disable stalls.
	 */
	common->can_stall = cfg->can_stall &&
		!(gadget_is_sh(fsg->gadget) || gadget_is_at91(fsg->gadget));


	common->thread_name = OR(cfg->thread_name, "file-storage");
	kref_init(&common->ref);
#undef OR


	/* Information */
	INFO(common, FSG_DRIVER_DESC ", version: " FSG_DRIVER_VERSION "\n");
	INFO(common, "Number of LUNs=%d\n", common->nluns);

	pathbuf = kmalloc(PATH_MAX, GFP_KERNEL);
	for (i = 0, nluns = common->nluns, curlun = common->luns;
	     i < nluns;
	     ++curlun, ++i) {
		char *p = "(no medium)";
		if (fsg_lun_is_open(curlun)) {
			p = "(error)";
			if (pathbuf) {
				p = d_path(&curlun->filp->f_path,
					   pathbuf, PATH_MAX);
				if (IS_ERR(p))
					p = "(error)";
			}
		}
		LINFO(curlun, "LUN: %s%s%sfile: %s\n",
		      curlun->removable ? "removable " : "",
		      curlun->ro ? "read only " : "",
		      curlun->cdrom ? "CD-ROM " : "",
		      p);
	}
	kfree(pathbuf);

	return common;


error_luns:
	common->nluns = i + 1;
error_release:
	/* Call fsg_common_release() directly, ref might be not
	 * initialised */
	fsg_common_release(&common->ref);
	return ERR_PTR(rc);
}


static void fsg_common_release(struct kref *ref)
{
	struct fsg_common *common =
		container_of(ref, struct fsg_common, ref);
	unsigned i = common->nluns;
	struct fsg_lun *lun = common->luns;

	/* Beware tempting for -> do-while optimization: when in error
	 * recovery nluns may be zero. */

	for (; i; --i, ++lun) {
		device_remove_file(&lun->dev, &dev_attr_ro);
		device_remove_file(&lun->dev, &dev_attr_file);
		fsg_lun_close(lun);
		device_unregister(&lun->dev);
	}

	kfree(common->luns);
	if (common->free_storage_on_release)
		kfree(common);
}


/*-------------------------------------------------------------------------*/


static void fsg_unbind(struct usb_configuration *c, struct usb_function *f)
{
	struct fsg_dev		*fsg = fsg_from_func(f);

	DBG(fsg, "unbind\n");
	clear_bit(REGISTERED, &fsg->atomic_bitflags);

	/* If the thread isn't already dead, tell it to exit now */
	if (fsg->state != FSG_STATE_TERMINATED) {
		raise_exception(fsg, FSG_STATE_EXIT);
		wait_for_completion(&fsg->thread_notifier);

		/* The cleanup routine waits for this completion also */
		complete(&fsg->thread_notifier);
	}

	fsg_common_put(fsg->common);
	kfree(fsg);
}


static int fsg_bind(struct usb_configuration *c, struct usb_function *f)
{
	struct fsg_dev		*fsg = fsg_from_func(f);
	struct usb_gadget	*gadget = c->cdev->gadget;
	int			rc;
	int			i;
	struct usb_ep		*ep;

	fsg->gadget = gadget;
	fsg->ep0 = gadget->ep0;
	fsg->ep0req = c->cdev->req;

	/* New interface */
	i = usb_interface_id(c, f);
	if (i < 0)
		return i;
	fsg_intf_desc.bInterfaceNumber = i;
	fsg->interface_number = i;

	/* Find all the endpoints we will use */
	ep = usb_ep_autoconfig(gadget, &fsg_fs_bulk_in_desc);
	if (!ep)
		goto autoconf_fail;
	ep->driver_data = fsg;		/* claim the endpoint */
	fsg->bulk_in = ep;

	ep = usb_ep_autoconfig(gadget, &fsg_fs_bulk_out_desc);
	if (!ep)
		goto autoconf_fail;
	ep->driver_data = fsg;		/* claim the endpoint */
	fsg->bulk_out = ep;

	if (gadget_is_dualspeed(gadget)) {
		/* Assume endpoint addresses are the same for both speeds */
		fsg_hs_bulk_in_desc.bEndpointAddress =
			fsg_fs_bulk_in_desc.bEndpointAddress;
		fsg_hs_bulk_out_desc.bEndpointAddress =
			fsg_fs_bulk_out_desc.bEndpointAddress;
		f->hs_descriptors = fsg_hs_function;
	}


	/* maybe allocate device-global string IDs, and patch descriptors */
	if (fsg_strings[FSG_STRING_INTERFACE].id == 0) {
		i = usb_string_id(c->cdev);
		if (i < 0)
			return i;
		fsg_strings[FSG_STRING_INTERFACE].id = i;
		fsg_intf_desc.iInterface = i;
	}


	fsg->thread_task = kthread_create(fsg_main_thread, fsg,
					  fsg->common->thread_name);
	if (IS_ERR(fsg->thread_task)) {
		rc = PTR_ERR(fsg->thread_task);
		goto out;
	}

	DBG(fsg, "I/O thread pid: %d\n", task_pid_nr(fsg->thread_task));

	set_bit(REGISTERED, &fsg->atomic_bitflags);

	/* Tell the thread to start working */
	wake_up_process(fsg->thread_task);
	return 0;

autoconf_fail:
	ERROR(fsg, "unable to autoconfigure all endpoints\n");
	rc = -ENOTSUPP;

out:
	fsg->state = FSG_STATE_TERMINATED;	/* The thread is dead */
	fsg_unbind(c, f);
	complete(&fsg->thread_notifier);
	return rc;
}


/****************************** ADD FUNCTION ******************************/

static struct usb_gadget_strings *fsg_strings_array[] = {
	&fsg_stringtab,
	NULL,
};

static int fsg_add(struct usb_composite_dev *cdev,
		   struct usb_configuration *c,
		   struct fsg_common *common)
{
	struct fsg_dev *fsg;
	int rc;

	fsg = kzalloc(sizeof *fsg, GFP_KERNEL);
	if (unlikely(!fsg))
		return -ENOMEM;

	spin_lock_init(&fsg->lock);
	init_completion(&fsg->thread_notifier);

	fsg->cdev                 = cdev;
	fsg->function.name        = FSG_DRIVER_DESC;
	fsg->function.strings     = fsg_strings_array;
	fsg->function.descriptors = fsg_fs_function;
	fsg->function.bind        = fsg_bind;
	fsg->function.unbind      = fsg_unbind;
	fsg->function.setup       = fsg_setup;
	fsg->function.set_alt     = fsg_set_alt;
	fsg->function.disable     = fsg_disable;

	fsg->common               = common;
	/* Our caller holds a reference to common structure so we
	 * don't have to be worry about it being freed until we return
	 * from this function.  So instead of incrementing counter now
	 * and decrement in error recovery we increment it only when
	 * call to usb_add_function() was successful. */
	fsg->can_stall = common->can_stall;

	rc = usb_add_function(c, &fsg->function);

	if (likely(rc == 0))
		fsg_common_get(fsg->common);
	else
		kfree(fsg);

	return rc;
}



/************************* Module parameters *************************/


struct fsg_module_parameters {
	char		*file[FSG_MAX_LUNS];
	int		ro[FSG_MAX_LUNS];
	int		removable[FSG_MAX_LUNS];
	int		cdrom[FSG_MAX_LUNS];

	unsigned int	file_count, ro_count, removable_count, cdrom_count;
	unsigned int	luns;	/* nluns */
	int		stall;	/* can_stall */
};


#define _FSG_MODULE_PARAM_ARRAY(prefix, params, name, type, desc)	\
	module_param_array_named(prefix ## name, params.name, type,	\
				 &prefix ## params.name ## _count,	\
				 S_IRUGO);				\
	MODULE_PARM_DESC(prefix ## name, desc)

#define _FSG_MODULE_PARAM(prefix, params, name, type, desc)		\
	module_param_named(prefix ## name, params.name, type,		\
			   S_IRUGO);					\
	MODULE_PARM_DESC(prefix ## name, desc)

#define FSG_MODULE_PARAMETERS(prefix, params)				\
	_FSG_MODULE_PARAM_ARRAY(prefix, params, file, charp,		\
				"names of backing files or devices");	\
	_FSG_MODULE_PARAM_ARRAY(prefix, params, ro, bool,		\
				"true to force read-only");		\
	_FSG_MODULE_PARAM_ARRAY(prefix, params, removable, bool,	\
				"true to simulate removable media");	\
	_FSG_MODULE_PARAM_ARRAY(prefix, params, cdrom, bool,		\
				"true to simulate CD-ROM instead of disk"); \
	_FSG_MODULE_PARAM(prefix, params, luns, uint,			\
			  "number of LUNs");				\
	_FSG_MODULE_PARAM(prefix, params, stall, bool,			\
			  "false to prevent bulk stalls")


static void
fsg_config_from_params(struct fsg_config *cfg,
		       const struct fsg_module_parameters *params)
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
		lun->removable = /* Removable by default */
			params->removable_count <= i || params->removable[i];
		lun->filename =
			params->file_count > i && params->file[i][0]
			? params->file[i]
			: 0;
	}

	/* Let MSF use defaults */
	cfg->lun_name_format = 0;
	cfg->thread_name = 0;
	cfg->vendor_name = 0;
	cfg->product_name = 0;
	cfg->release = 0xffff;

	/* Finalise */
	cfg->can_stall = params->stall;
}

static inline struct fsg_common *
fsg_common_from_params(struct fsg_common *common,
		       struct usb_composite_dev *cdev,
		       const struct fsg_module_parameters *params)
	__attribute__((unused));
static inline struct fsg_common *
fsg_common_from_params(struct fsg_common *common,
		       struct usb_composite_dev *cdev,
		       const struct fsg_module_parameters *params)
{
	struct fsg_config cfg;
	fsg_config_from_params(&cfg, params);
	return fsg_common_init(common, cdev, &cfg);
}

