/*
 * file_storage.c -- File-backed USB Storage Gadget, for USB development
 *
 * Copyright (C) 2003-2008 Alan Stern
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
 * The File-backed Storage Gadget acts as a USB Mass Storage device,
 * appearing to the host as a disk drive or as a CD-ROM drive.  In addition
 * to providing an example of a genuinely useful gadget driver for a USB
 * device, it also illustrates a technique of double-buffering for increased
 * throughput.  Last but not least, it gives an easy way to probe the
 * behavior of the Mass Storage drivers in a USB host.
 *
 * Backing storage is provided by a regular file or a block device, specified
 * by the "file" module parameter.  Access can be limited to read-only by
 * setting the optional "ro" module parameter.  (For CD-ROM emulation,
 * access is always read-only.)  The gadget will indicate that it has
 * removable media if the optional "removable" module parameter is set.
 *
 * The gadget supports the Control-Bulk (CB), Control-Bulk-Interrupt (CBI),
 * and Bulk-Only (also known as Bulk-Bulk-Bulk or BBB) transports, selected
 * by the optional "transport" module parameter.  It also supports the
 * following protocols: RBC (0x01), ATAPI or SFF-8020i (0x02), QIC-157 (0c03),
 * UFI (0x04), SFF-8070i (0x05), and transparent SCSI (0x06), selected by
 * the optional "protocol" module parameter.  In addition, the default
 * Vendor ID, Product ID, release number and serial number can be overridden.
 *
 * There is support for multiple logical units (LUNs), each of which has
 * its own backing file.  The number of LUNs can be set using the optional
 * "luns" module parameter (anywhere from 1 to 8), and the corresponding
 * files are specified using comma-separated lists for "file" and "ro".
 * The default number of LUNs is taken from the number of "file" elements;
 * it is 1 if "file" is not given.  If "removable" is not set then a backing
 * file must be specified for each LUN.  If it is set, then an unspecified
 * or empty backing filename means the LUN's medium is not loaded.  Ideally
 * each LUN would be settable independently as a disk drive or a CD-ROM
 * drive, but currently all LUNs have to be the same type.  The CD-ROM
 * emulation includes a single data track and no audio tracks; hence there
 * need be only one backing file per LUN.  Note also that the CD-ROM block
 * length is set to 512 rather than the more common value 2048.
 *
 * Requirements are modest; only a bulk-in and a bulk-out endpoint are
 * needed (an interrupt-out endpoint is also needed for CBI).  The memory
 * requirement amounts to two 16K buffers, size configurable by a parameter.
 * Support is included for both full-speed and high-speed operation.
 *
 * Note that the driver is slightly non-portable in that it assumes a
 * single memory/DMA buffer will be useable for bulk-in, bulk-out, and
 * interrupt-in endpoints.  With most device controllers this isn't an
 * issue, but there may be some with hardware restrictions that prevent
 * a buffer from being used by more than one endpoint.
 *
 * Module options:
 *
 *	file=filename[,filename...]
 *				Required if "removable" is not set, names of
 *					the files or block devices used for
 *					backing storage
 *	ro=b[,b...]		Default false, booleans for read-only access
 *	removable		Default false, boolean for removable media
 *	luns=N			Default N = number of filenames, number of
 *					LUNs to support
 *	nofua=b[,b...]		Default false, booleans for ignore FUA flag
 *					in SCSI WRITE(10,12) commands
 *	stall			Default determined according to the type of
 *					USB device controller (usually true),
 *					boolean to permit the driver to halt
 *					bulk endpoints
 *	cdrom			Default false, boolean for whether to emulate
 *					a CD-ROM drive
 *	transport=XXX		Default BBB, transport name (CB, CBI, or BBB)
 *	protocol=YYY		Default SCSI, protocol name (RBC, 8020 or
 *					ATAPI, QIC, UFI, 8070, or SCSI;
 *					also 1 - 6)
 *	vendor=0xVVVV		Default 0x0525 (NetChip), USB Vendor ID
 *	product=0xPPPP		Default 0xa4a5 (FSG), USB Product ID
 *	release=0xRRRR		Override the USB release number (bcdDevice)
 *	serial=HHHH...		Override serial number (string of hex chars)
 *	buflen=N		Default N=16384, buffer size used (will be
 *					rounded down to a multiple of
 *					PAGE_CACHE_SIZE)
 *
 * If CONFIG_USB_FILE_STORAGE_TEST is not set, only the "file", "ro",
 * "removable", "luns", "nofua", "stall", and "cdrom" options are available;
 * default values are used for everything else.
 *
 * The pathnames of the backing files and the ro settings are available in
 * the attribute files "file", "nofua", and "ro" in the lun<n> subdirectory of
 * the gadget's sysfs directory.  If the "removable" option is set, writing to
 * these files will simulate ejecting/loading the medium (writing an empty
 * line means eject) and adjusting a write-enable tab.  Changes to the ro
 * setting are not allowed when the medium is loaded or if CD-ROM emulation
 * is being used.
 *
 * This gadget driver is heavily based on "Gadget Zero" by David Brownell.
 * The driver's SCSI command interface was based on the "Information
 * technology - Small Computer System Interface - 2" document from
 * X3T9.2 Project 375D, Revision 10L, 7-SEP-93, available at
 * <http://www.t10.org/ftp/t10/drafts/s2/s2-r10l.pdf>.  The single exception
 * is opcode 0x23 (READ FORMAT CAPACITIES), which was based on the
 * "Universal Serial Bus Mass Storage Class UFI Command Specification"
 * document, Revision 1.0, December 14, 1998, available at
 * <http://www.usb.org/developers/devclass_docs/usbmass-ufi10.pdf>.
 */


/*
 *				Driver Design
 *
 * The FSG driver is fairly straightforward.  There is a main kernel
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
 * fsg_bind() callback and stopped during fsg_unbind().  But it can also
 * exit when it receives a signal, and there's no point leaving the
 * gadget running when the thread is dead.  So just before the thread
 * exits, it deregisters the gadget driver.  This makes things a little
 * tricky: The driver is deregistered at two places, and the exiting
 * thread can indirectly call fsg_unbind() which in turn can tell the
 * thread to exit.  The first problem is resolved through the use of the
 * REGISTERED atomic bitflag; the driver will only be deregistered once.
 * The second problem is resolved by having fsg_unbind() check
 * fsg->state; it won't try to stop the thread if the state is already
 * FSG_STATE_TERMINATED.
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



/*
 * Kbuild is not very cooperative with respect to linking separately
 * compiled library objects into one module.  So for now we won't use
 * separate compilation ... ensuring init/exit sections work to shrink
 * the runtime footprint, and giving us at least some parts of what
 * a "gcc --combine ... part1.c part2.c part3.c ... " build would.
 */
#include "usbstring.c"
#include "config.c"
#include "epautoconf.c"

/*-------------------------------------------------------------------------*/

#define DRIVER_DESC		"File-backed Storage Gadget"
#define DRIVER_NAME		"g_file_storage"
/* DRIVER_VERSION must be at least 6 characters long, as it is used
 * to generate a fallback serial number. */
#define DRIVER_VERSION		"20 November 2008"

static       char fsg_string_manufacturer[64];
static const char fsg_string_product[] = DRIVER_DESC;
static       char fsg_string_serial[13];
static const char fsg_string_config[] = "Self-powered";
static const char fsg_string_interface[] = "Mass Storage";


#include "storage_common.c"


MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("Alan Stern");
MODULE_LICENSE("Dual BSD/GPL");

/*
 * This driver assumes self-powered hardware and has no way for users to
 * trigger remote wakeup.  It uses autoconfiguration to select endpoints
 * and endpoint addresses.
 */


/*-------------------------------------------------------------------------*/


/* Encapsulate the module parameter settings */

static struct {
	char		*file[FSG_MAX_LUNS];
	int		ro[FSG_MAX_LUNS];
	int		nofua[FSG_MAX_LUNS];
	unsigned int	num_filenames;
	unsigned int	num_ros;
	unsigned int	num_nofuas;
	unsigned int	nluns;

	int		removable;
	int		can_stall;
	int		cdrom;

	char		*transport_parm;
	char		*protocol_parm;
	unsigned short	vendor;
	unsigned short	product;
	unsigned short	release;
	char		*serial;
	unsigned int	buflen;

	int		transport_type;
	char		*transport_name;
	int		protocol_type;
	char		*protocol_name;

} mod_data = {					// Default values
	.transport_parm		= "BBB",
	.protocol_parm		= "SCSI",
	.removable		= 0,
	.can_stall		= 1,
	.cdrom			= 0,
	.vendor			= FSG_VENDOR_ID,
	.product		= FSG_PRODUCT_ID,
	.release		= 0xffff,	// Use controller chip type
	.buflen			= 16384,
	};


module_param_array_named(file, mod_data.file, charp, &mod_data.num_filenames,
		S_IRUGO);
MODULE_PARM_DESC(file, "names of backing files or devices");

module_param_array_named(ro, mod_data.ro, bool, &mod_data.num_ros, S_IRUGO);
MODULE_PARM_DESC(ro, "true to force read-only");

module_param_array_named(nofua, mod_data.nofua, bool, &mod_data.num_nofuas,
		S_IRUGO);
MODULE_PARM_DESC(nofua, "true to ignore SCSI WRITE(10,12) FUA bit");

module_param_named(luns, mod_data.nluns, uint, S_IRUGO);
MODULE_PARM_DESC(luns, "number of LUNs");

module_param_named(removable, mod_data.removable, bool, S_IRUGO);
MODULE_PARM_DESC(removable, "true to simulate removable media");

module_param_named(stall, mod_data.can_stall, bool, S_IRUGO);
MODULE_PARM_DESC(stall, "false to prevent bulk stalls");

module_param_named(cdrom, mod_data.cdrom, bool, S_IRUGO);
MODULE_PARM_DESC(cdrom, "true to emulate cdrom instead of disk");

module_param_named(serial, mod_data.serial, charp, S_IRUGO);
MODULE_PARM_DESC(serial, "USB serial number");

/* In the non-TEST version, only the module parameters listed above
 * are available. */
#ifdef CONFIG_USB_FILE_STORAGE_TEST

module_param_named(transport, mod_data.transport_parm, charp, S_IRUGO);
MODULE_PARM_DESC(transport, "type of transport (BBB, CBI, or CB)");

module_param_named(protocol, mod_data.protocol_parm, charp, S_IRUGO);
MODULE_PARM_DESC(protocol, "type of protocol (RBC, 8020, QIC, UFI, "
		"8070, or SCSI)");

module_param_named(vendor, mod_data.vendor, ushort, S_IRUGO);
MODULE_PARM_DESC(vendor, "USB Vendor ID");

module_param_named(product, mod_data.product, ushort, S_IRUGO);
MODULE_PARM_DESC(product, "USB Product ID");

module_param_named(release, mod_data.release, ushort, S_IRUGO);
MODULE_PARM_DESC(release, "USB release number");

module_param_named(buflen, mod_data.buflen, uint, S_IRUGO);
MODULE_PARM_DESC(buflen, "I/O buffer size");

#endif /* CONFIG_USB_FILE_STORAGE_TEST */


/*
 * These definitions will permit the compiler to avoid generating code for
 * parts of the driver that aren't used in the non-TEST version.  Even gcc
 * can recognize when a test of a constant expression yields a dead code
 * path.
 */

#ifdef CONFIG_USB_FILE_STORAGE_TEST

#define transport_is_bbb()	(mod_data.transport_type == USB_PR_BULK)
#define transport_is_cbi()	(mod_data.transport_type == USB_PR_CBI)
#define protocol_is_scsi()	(mod_data.protocol_type == USB_SC_SCSI)

#else

#define transport_is_bbb()	1
#define transport_is_cbi()	0
#define protocol_is_scsi()	1

#endif /* CONFIG_USB_FILE_STORAGE_TEST */


/*-------------------------------------------------------------------------*/


struct fsg_dev {
	/* lock protects: state, all the req_busy's, and cbbuf_cmnd */
	spinlock_t		lock;
	struct usb_gadget	*gadget;

	/* filesem protects: backing files in use */
	struct rw_semaphore	filesem;

	/* reference counting: wait until all LUNs are released */
	struct kref		ref;

	struct usb_ep		*ep0;		// Handy copy of gadget->ep0
	struct usb_request	*ep0req;	// For control responses
	unsigned int		ep0_req_tag;
	const char		*ep0req_name;

	struct usb_request	*intreq;	// For interrupt responses
	int			intreq_busy;
	struct fsg_buffhd	*intr_buffhd;

	unsigned int		bulk_out_maxpacket;
	enum fsg_state		state;		// For exception handling
	unsigned int		exception_req_tag;

	u8			config, new_config;

	unsigned int		running : 1;
	unsigned int		bulk_in_enabled : 1;
	unsigned int		bulk_out_enabled : 1;
	unsigned int		intr_in_enabled : 1;
	unsigned int		phase_error : 1;
	unsigned int		short_packet_received : 1;
	unsigned int		bad_lun_okay : 1;

	unsigned long		atomic_bitflags;
#define REGISTERED		0
#define IGNORE_BULK_OUT		1
#define SUSPENDED		2

	struct usb_ep		*bulk_in;
	struct usb_ep		*bulk_out;
	struct usb_ep		*intr_in;

	struct fsg_buffhd	*next_buffhd_to_fill;
	struct fsg_buffhd	*next_buffhd_to_drain;
	struct fsg_buffhd	buffhds[FSG_NUM_BUFFERS];

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

	/* The CB protocol offers no way for a host to know when a command
	 * has completed.  As a result the next command may arrive early,
	 * and we will still have to handle it.  For that reason we need
	 * a buffer to store new commands when using CB (or CBI, which
	 * does not oblige a host to wait for command completion either). */
	int			cbbuf_cmnd_size;
	u8			cbbuf_cmnd[MAX_COMMAND_SIZE];

	unsigned int		nluns;
	struct fsg_lun		*luns;
	struct fsg_lun		*curlun;
};

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

static struct fsg_dev			*the_fsg;
static struct usb_gadget_driver		fsg_driver;


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

/*
 * DESCRIPTORS ... most are static, but strings and (full) configuration
 * descriptors are built on demand.  Also the (static) config and interface
 * descriptors are adjusted during fsg_bind().
 */

/* There is only one configuration. */
#define	CONFIG_VALUE		1

static struct usb_device_descriptor
device_desc = {
	.bLength =		sizeof device_desc,
	.bDescriptorType =	USB_DT_DEVICE,

	.bcdUSB =		cpu_to_le16(0x0200),
	.bDeviceClass =		USB_CLASS_PER_INTERFACE,

	/* The next three values can be overridden by module parameters */
	.idVendor =		cpu_to_le16(FSG_VENDOR_ID),
	.idProduct =		cpu_to_le16(FSG_PRODUCT_ID),
	.bcdDevice =		cpu_to_le16(0xffff),

	.iManufacturer =	FSG_STRING_MANUFACTURER,
	.iProduct =		FSG_STRING_PRODUCT,
	.iSerialNumber =	FSG_STRING_SERIAL,
	.bNumConfigurations =	1,
};

static struct usb_config_descriptor
config_desc = {
	.bLength =		sizeof config_desc,
	.bDescriptorType =	USB_DT_CONFIG,

	/* wTotalLength computed by usb_gadget_config_buf() */
	.bNumInterfaces =	1,
	.bConfigurationValue =	CONFIG_VALUE,
	.iConfiguration =	FSG_STRING_CONFIG,
	.bmAttributes =		USB_CONFIG_ATT_ONE | USB_CONFIG_ATT_SELFPOWER,
	.bMaxPower =		CONFIG_USB_GADGET_VBUS_DRAW / 2,
};


static struct usb_qualifier_descriptor
dev_qualifier = {
	.bLength =		sizeof dev_qualifier,
	.bDescriptorType =	USB_DT_DEVICE_QUALIFIER,

	.bcdUSB =		cpu_to_le16(0x0200),
	.bDeviceClass =		USB_CLASS_PER_INTERFACE,

	.bNumConfigurations =	1,
};



/*
 * Config descriptors must agree with the code that sets configurations
 * and with code managing interfaces and their altsettings.  They must
 * also handle different speeds and other-speed requests.
 */
static int populate_config_buf(struct usb_gadget *gadget,
		u8 *buf, u8 type, unsigned index)
{
	enum usb_device_speed			speed = gadget->speed;
	int					len;
	const struct usb_descriptor_header	**function;

	if (index > 0)
		return -EINVAL;

	if (gadget_is_dualspeed(gadget) && type == USB_DT_OTHER_SPEED_CONFIG)
		speed = (USB_SPEED_FULL + USB_SPEED_HIGH) - speed;
	function = gadget_is_dualspeed(gadget) && speed == USB_SPEED_HIGH
		? (const struct usb_descriptor_header **)fsg_hs_function
		: (const struct usb_descriptor_header **)fsg_fs_function;

	/* for now, don't advertise srp-only devices */
	if (!gadget_is_otg(gadget))
		function++;

	len = usb_gadget_config_buf(&config_desc, buf, EP0_BUFSIZE, function);
	((struct usb_config_descriptor *) buf)->bDescriptorType = type;
	return len;
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

/* The disconnect callback and ep0 routines.  These always run in_irq,
 * except that ep0_queue() is called in the main thread to acknowledge
 * completion of various requests: set config, set interface, and
 * Bulk-only device reset. */

static void fsg_disconnect(struct usb_gadget *gadget)
{
	struct fsg_dev		*fsg = get_gadget_data(gadget);

	DBG(fsg, "disconnect or port reset\n");
	raise_exception(fsg, FSG_STATE_DISCONNECT);
}


static int ep0_queue(struct fsg_dev *fsg)
{
	int	rc;

	rc = usb_ep_queue(fsg->ep0, fsg->ep0req, GFP_ATOMIC);
	if (rc != 0 && rc != -ESHUTDOWN) {

		/* We can't do much more than wait for a reset */
		WARNING(fsg, "error in submission: %s --> %d\n",
				fsg->ep0->name, rc);
	}
	return rc;
}

static void ep0_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct fsg_dev		*fsg = ep->driver_data;

	if (req->actual > 0)
		dump_msg(fsg, fsg->ep0req_name, req->buf, req->actual);
	if (req->status || req->actual != req->length)
		DBG(fsg, "%s --> %d, %u/%u\n", __func__,
				req->status, req->actual, req->length);
	if (req->status == -ECONNRESET)		// Request was cancelled
		usb_ep_fifo_flush(ep);

	if (req->status == 0 && req->context)
		((fsg_routine_t) (req->context))(fsg);
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
	if (req->status == -ECONNRESET)		// Request was cancelled
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
	if (req->status == -ECONNRESET)		// Request was cancelled
		usb_ep_fifo_flush(ep);

	/* Hold the lock while we update the request and buffer states */
	smp_wmb();
	spin_lock(&fsg->lock);
	bh->outreq_busy = 0;
	bh->state = BUF_STATE_FULL;
	wakeup_thread(fsg);
	spin_unlock(&fsg->lock);
}


#ifdef CONFIG_USB_FILE_STORAGE_TEST
static void intr_in_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct fsg_dev		*fsg = ep->driver_data;
	struct fsg_buffhd	*bh = req->context;

	if (req->status || req->actual != req->length)
		DBG(fsg, "%s --> %d, %u/%u\n", __func__,
				req->status, req->actual, req->length);
	if (req->status == -ECONNRESET)		// Request was cancelled
		usb_ep_fifo_flush(ep);

	/* Hold the lock while we update the request and buffer states */
	smp_wmb();
	spin_lock(&fsg->lock);
	fsg->intreq_busy = 0;
	bh->state = BUF_STATE_EMPTY;
	wakeup_thread(fsg);
	spin_unlock(&fsg->lock);
}

#else
static void intr_in_complete(struct usb_ep *ep, struct usb_request *req)
{}
#endif /* CONFIG_USB_FILE_STORAGE_TEST */


/*-------------------------------------------------------------------------*/

/* Ep0 class-specific handlers.  These always run in_irq. */

#ifdef CONFIG_USB_FILE_STORAGE_TEST
static void received_cbi_adsc(struct fsg_dev *fsg, struct fsg_buffhd *bh)
{
	struct usb_request	*req = fsg->ep0req;
	static u8		cbi_reset_cmnd[6] = {
			SC_SEND_DIAGNOSTIC, 4, 0xff, 0xff, 0xff, 0xff};

	/* Error in command transfer? */
	if (req->status || req->length != req->actual ||
			req->actual < 6 || req->actual > MAX_COMMAND_SIZE) {

		/* Not all controllers allow a protocol stall after
		 * receiving control-out data, but we'll try anyway. */
		fsg_set_halt(fsg, fsg->ep0);
		return;			// Wait for reset
	}

	/* Is it the special reset command? */
	if (req->actual >= sizeof cbi_reset_cmnd &&
			memcmp(req->buf, cbi_reset_cmnd,
				sizeof cbi_reset_cmnd) == 0) {

		/* Raise an exception to stop the current operation
		 * and reinitialize our state. */
		DBG(fsg, "cbi reset request\n");
		raise_exception(fsg, FSG_STATE_RESET);
		return;
	}

	VDBG(fsg, "CB[I] accept device-specific command\n");
	spin_lock(&fsg->lock);

	/* Save the command for later */
	if (fsg->cbbuf_cmnd_size)
		WARNING(fsg, "CB[I] overwriting previous command\n");
	fsg->cbbuf_cmnd_size = req->actual;
	memcpy(fsg->cbbuf_cmnd, req->buf, fsg->cbbuf_cmnd_size);

	wakeup_thread(fsg);
	spin_unlock(&fsg->lock);
}

#else
static void received_cbi_adsc(struct fsg_dev *fsg, struct fsg_buffhd *bh)
{}
#endif /* CONFIG_USB_FILE_STORAGE_TEST */


static int class_setup_req(struct fsg_dev *fsg,
		const struct usb_ctrlrequest *ctrl)
{
	struct usb_request	*req = fsg->ep0req;
	int			value = -EOPNOTSUPP;
	u16			w_index = le16_to_cpu(ctrl->wIndex);
	u16                     w_value = le16_to_cpu(ctrl->wValue);
	u16			w_length = le16_to_cpu(ctrl->wLength);

	if (!fsg->config)
		return value;

	/* Handle Bulk-only class-specific requests */
	if (transport_is_bbb()) {
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
			value = DELAYED_STATUS;
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
			*(u8 *) req->buf = fsg->nluns - 1;
			value = 1;
			break;
		}
	}

	/* Handle CBI class-specific requests */
	else {
		switch (ctrl->bRequest) {

		case USB_CBI_ADSC_REQUEST:
			if (ctrl->bRequestType != (USB_DIR_OUT |
					USB_TYPE_CLASS | USB_RECIP_INTERFACE))
				break;
			if (w_index != 0 || w_value != 0) {
				value = -EDOM;
				break;
			}
			if (w_length > MAX_COMMAND_SIZE) {
				value = -EOVERFLOW;
				break;
			}
			value = w_length;
			fsg->ep0req->context = received_cbi_adsc;
			break;
		}
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

/* Ep0 standard request handlers.  These always run in_irq. */

static int standard_setup_req(struct fsg_dev *fsg,
		const struct usb_ctrlrequest *ctrl)
{
	struct usb_request	*req = fsg->ep0req;
	int			value = -EOPNOTSUPP;
	u16			w_index = le16_to_cpu(ctrl->wIndex);
	u16			w_value = le16_to_cpu(ctrl->wValue);

	/* Usually this just stores reply data in the pre-allocated ep0 buffer,
	 * but config change events will also reconfigure hardware. */
	switch (ctrl->bRequest) {

	case USB_REQ_GET_DESCRIPTOR:
		if (ctrl->bRequestType != (USB_DIR_IN | USB_TYPE_STANDARD |
				USB_RECIP_DEVICE))
			break;
		switch (w_value >> 8) {

		case USB_DT_DEVICE:
			VDBG(fsg, "get device descriptor\n");
			value = sizeof device_desc;
			memcpy(req->buf, &device_desc, value);
			break;
		case USB_DT_DEVICE_QUALIFIER:
			VDBG(fsg, "get device qualifier\n");
			if (!gadget_is_dualspeed(fsg->gadget))
				break;
			value = sizeof dev_qualifier;
			memcpy(req->buf, &dev_qualifier, value);
			break;

		case USB_DT_OTHER_SPEED_CONFIG:
			VDBG(fsg, "get other-speed config descriptor\n");
			if (!gadget_is_dualspeed(fsg->gadget))
				break;
			goto get_config;
		case USB_DT_CONFIG:
			VDBG(fsg, "get configuration descriptor\n");
get_config:
			value = populate_config_buf(fsg->gadget,
					req->buf,
					w_value >> 8,
					w_value & 0xff);
			break;

		case USB_DT_STRING:
			VDBG(fsg, "get string descriptor\n");

			/* wIndex == language code */
			value = usb_gadget_get_string(&fsg_stringtab,
					w_value & 0xff, req->buf);
			break;
		}
		break;

	/* One config, two speeds */
	case USB_REQ_SET_CONFIGURATION:
		if (ctrl->bRequestType != (USB_DIR_OUT | USB_TYPE_STANDARD |
				USB_RECIP_DEVICE))
			break;
		VDBG(fsg, "set configuration\n");
		if (w_value == CONFIG_VALUE || w_value == 0) {
			fsg->new_config = w_value;

			/* Raise an exception to wipe out previous transaction
			 * state (queued bufs, etc) and set the new config. */
			raise_exception(fsg, FSG_STATE_CONFIG_CHANGE);
			value = DELAYED_STATUS;
		}
		break;
	case USB_REQ_GET_CONFIGURATION:
		if (ctrl->bRequestType != (USB_DIR_IN | USB_TYPE_STANDARD |
				USB_RECIP_DEVICE))
			break;
		VDBG(fsg, "get configuration\n");
		*(u8 *) req->buf = fsg->config;
		value = 1;
		break;

	case USB_REQ_SET_INTERFACE:
		if (ctrl->bRequestType != (USB_DIR_OUT| USB_TYPE_STANDARD |
				USB_RECIP_INTERFACE))
			break;
		if (fsg->config && w_index == 0) {

			/* Raise an exception to wipe out previous transaction
			 * state (queued bufs, etc) and install the new
			 * interface altsetting. */
			raise_exception(fsg, FSG_STATE_INTERFACE_CHANGE);
			value = DELAYED_STATUS;
		}
		break;
	case USB_REQ_GET_INTERFACE:
		if (ctrl->bRequestType != (USB_DIR_IN | USB_TYPE_STANDARD |
				USB_RECIP_INTERFACE))
			break;
		if (!fsg->config)
			break;
		if (w_index != 0) {
			value = -EDOM;
			break;
		}
		VDBG(fsg, "get interface\n");
		*(u8 *) req->buf = 0;
		value = 1;
		break;

	default:
		VDBG(fsg,
			"unknown control req %02x.%02x v%04x i%04x l%u\n",
			ctrl->bRequestType, ctrl->bRequest,
			w_value, w_index, le16_to_cpu(ctrl->wLength));
	}

	return value;
}


static int fsg_setup(struct usb_gadget *gadget,
		const struct usb_ctrlrequest *ctrl)
{
	struct fsg_dev		*fsg = get_gadget_data(gadget);
	int			rc;
	int			w_length = le16_to_cpu(ctrl->wLength);

	++fsg->ep0_req_tag;		// Record arrival of a new request
	fsg->ep0req->context = NULL;
	fsg->ep0req->length = 0;
	dump_msg(fsg, "ep0-setup", (u8 *) ctrl, sizeof(*ctrl));

	if ((ctrl->bRequestType & USB_TYPE_MASK) == USB_TYPE_CLASS)
		rc = class_setup_req(fsg, ctrl);
	else
		rc = standard_setup_req(fsg, ctrl);

	/* Respond with data/status or defer until later? */
	if (rc >= 0 && rc != DELAYED_STATUS) {
		rc = min(rc, w_length);
		fsg->ep0req->length = rc;
		fsg->ep0req->zero = rc < w_length;
		fsg->ep0req_name = (ctrl->bRequestType & USB_DIR_IN ?
				"ep0-in" : "ep0-out");
		rc = ep0_queue(fsg);
	}

	/* Device either stalls (rc < 0) or reports success */
	return rc;
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
	else if (ep == fsg->intr_in)
		dump_msg(fsg, "intr-in", req->buf, req->length);

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
	struct fsg_lun		*curlun = fsg->curlun;
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
		lba = get_unaligned_be24(&fsg->cmnd[1]);
	else {
		lba = get_unaligned_be32(&fsg->cmnd[2]);

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
		return -EIO;		// No default reply

	for (;;) {

		/* Figure out how much we need to read:
		 * Try to read the remaining amount.
		 * But don't read more than the buffer size.
		 * And don't try to read past the end of the file.
		 * Finally, if we're not at a page boundary, don't read past
		 *	the next page.
		 * If this means reading 0 then we were asked to read past
		 *	the end of file. */
		amount = min((unsigned int) amount_left, mod_data.buflen);
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
			nread -= (nread & 511);	// Round down to a block
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
			break;		// No more left to read

		/* Send this buffer and go read some more */
		bh->inreq->zero = 0;
		start_transfer(fsg, fsg->bulk_in, bh->inreq,
				&bh->inreq_busy, &bh->state);
		fsg->next_buffhd_to_fill = bh->next;
	}

	return -EIO;		// No default reply
}


/*-------------------------------------------------------------------------*/

static int do_write(struct fsg_dev *fsg)
{
	struct fsg_lun		*curlun = fsg->curlun;
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
	curlun->filp->f_flags &= ~O_SYNC;	// Default is not to wait
	spin_unlock(&curlun->filp->f_lock);

	/* Get the starting Logical Block Address and check that it's
	 * not too big */
	if (fsg->cmnd[0] == SC_WRITE_6)
		lba = get_unaligned_be24(&fsg->cmnd[1]);
	else {
		lba = get_unaligned_be32(&fsg->cmnd[2]);

		/* We allow DPO (Disable Page Out = don't save data in the
		 * cache) and FUA (Force Unit Access = write directly to the
		 * medium).  We don't implement DPO; we implement FUA by
		 * performing synchronous output. */
		if ((fsg->cmnd[1] & ~0x18) != 0) {
			curlun->sense_data = SS_INVALID_FIELD_IN_CDB;
			return -EINVAL;
		}
		/* FUA */
		if (!curlun->nofua && (fsg->cmnd[1] & 0x08)) {
			spin_lock(&curlun->filp->f_lock);
			curlun->filp->f_flags |= O_DSYNC;
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
			amount = min(amount_left_to_req, mod_data.buflen);
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
			bh->outreq->short_not_ok = 1;
			start_transfer(fsg, fsg->bulk_out, bh->outreq,
					&bh->outreq_busy, &bh->state);
			fsg->next_buffhd_to_fill = bh->next;
			continue;
		}

		/* Write the received data to the backing file */
		bh = fsg->next_buffhd_to_drain;
		if (bh->state == BUF_STATE_EMPTY && !get_some_more)
			break;			// We stopped early
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
				return -EINTR;		// Interrupted!

			if (nwritten < 0) {
				LDBG(curlun, "error in file write: %d\n",
						(int) nwritten);
				nwritten = 0;
			} else if (nwritten < amount) {
				LDBG(curlun, "partial file write: %d/%u\n",
						(int) nwritten, amount);
				nwritten -= (nwritten & 511);
						// Round down to a block
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

	return -EIO;		// No default reply
}


/*-------------------------------------------------------------------------*/

static int do_synchronize_cache(struct fsg_dev *fsg)
{
	struct fsg_lun	*curlun = fsg->curlun;
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
	VLDBG(curlun, "invalidate_mapping_pages -> %ld\n", rc);
}

static int do_verify(struct fsg_dev *fsg)
{
	struct fsg_lun		*curlun = fsg->curlun;
	u32			lba;
	u32			verification_length;
	struct fsg_buffhd	*bh = fsg->next_buffhd_to_fill;
	loff_t			file_offset, file_offset_tmp;
	u32			amount_left;
	unsigned int		amount;
	ssize_t			nread;

	/* Get the starting Logical Block Address and check that it's
	 * not too big */
	lba = get_unaligned_be32(&fsg->cmnd[2]);
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

	verification_length = get_unaligned_be16(&fsg->cmnd[7]);
	if (unlikely(verification_length == 0))
		return -EIO;		// No default reply

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
		amount = min((unsigned int) amount_left, mod_data.buflen);
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
			nread -= (nread & 511);	// Round down to a sector
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

	static char vendor_id[] = "Linux   ";
	static char product_disk_id[] = "File-Stor Gadget";
	static char product_cdrom_id[] = "File-CD Gadget  ";

	if (!fsg->curlun) {		// Unsupported LUNs are okay
		fsg->bad_lun_okay = 1;
		memset(buf, 0, 36);
		buf[0] = 0x7f;		// Unsupported, no device-type
		buf[4] = 31;		// Additional length
		return 36;
	}

	memset(buf, 0, 8);
	buf[0] = (mod_data.cdrom ? TYPE_CDROM : TYPE_DISK);
	if (mod_data.removable)
		buf[1] = 0x80;
	buf[2] = 2;		// ANSI SCSI level 2
	buf[3] = 2;		// SCSI-2 INQUIRY data format
	buf[4] = 31;		// Additional length
				// No special options
	sprintf(buf + 8, "%-8s%-16s%04x", vendor_id,
			(mod_data.cdrom ? product_cdrom_id :
				product_disk_id),
			mod_data.release);
	return 36;
}


static int do_request_sense(struct fsg_dev *fsg, struct fsg_buffhd *bh)
{
	struct fsg_lun	*curlun = fsg->curlun;
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

	if (!curlun) {		// Unsupported LUNs are okay
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
	buf[0] = valid | 0x70;			// Valid, current error
	buf[2] = SK(sd);
	put_unaligned_be32(sdinfo, &buf[3]);	/* Sense information */
	buf[7] = 18 - 8;			// Additional sense length
	buf[12] = ASC(sd);
	buf[13] = ASCQ(sd);
	return 18;
}


static int do_read_capacity(struct fsg_dev *fsg, struct fsg_buffhd *bh)
{
	struct fsg_lun	*curlun = fsg->curlun;
	u32		lba = get_unaligned_be32(&fsg->cmnd[2]);
	int		pmi = fsg->cmnd[8];
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
	struct fsg_lun	*curlun = fsg->curlun;
	int		msf = fsg->cmnd[1] & 0x02;
	u32		lba = get_unaligned_be32(&fsg->cmnd[2]);
	u8		*buf = (u8 *) bh->buf;

	if ((fsg->cmnd[1] & ~0x02) != 0) {		/* Mask away MSF */
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
	struct fsg_lun	*curlun = fsg->curlun;
	int		msf = fsg->cmnd[1] & 0x02;
	int		start_track = fsg->cmnd[6];
	u8		*buf = (u8 *) bh->buf;

	if ((fsg->cmnd[1] & ~0x02) != 0 ||		/* Mask away MSF */
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
	struct fsg_lun	*curlun = fsg->curlun;
	int		mscmnd = fsg->cmnd[0];
	u8		*buf = (u8 *) bh->buf;
	u8		*buf0 = buf;
	int		pc, page_code;
	int		changeable_values, all_pages;
	int		valid_page = 0;
	int		len, limit;

	if ((fsg->cmnd[1] & ~0x08) != 0) {		// Mask away DBD
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
		buf[2] = (curlun->ro ? 0x80 : 0x00);		// WP, DPOFUA
		buf += 4;
		limit = 255;
	} else {			// SC_MODE_SENSE_10
		buf[3] = (curlun->ro ? 0x80 : 0x00);		// WP, DPOFUA
		buf += 8;
		limit = 65535;		// Should really be mod_data.buflen
	}

	/* No block descriptors */

	/* The mode pages, in numerical order.  The only page we support
	 * is the Caching page. */
	if (page_code == 0x08 || all_pages) {
		valid_page = 1;
		buf[0] = 0x08;		// Page code
		buf[1] = 10;		// Page length
		memset(buf+2, 0, 10);	// None of the fields are changeable

		if (!changeable_values) {
			buf[2] = 0x04;	// Write cache enable,
					// Read cache not disabled
					// No cache retention priorities
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
	struct fsg_lun	*curlun = fsg->curlun;
	int		loej, start;

	if (!mod_data.removable) {
		curlun->sense_data = SS_INVALID_COMMAND;
		return -EINVAL;
	}

	// int immed = fsg->cmnd[1] & 0x01;
	loej = fsg->cmnd[4] & 0x02;
	start = fsg->cmnd[4] & 0x01;

#ifdef CONFIG_USB_FILE_STORAGE_TEST
	if ((fsg->cmnd[1] & ~0x01) != 0 ||		// Mask away Immed
			(fsg->cmnd[4] & ~0x03) != 0) {	// Mask LoEj, Start
		curlun->sense_data = SS_INVALID_FIELD_IN_CDB;
		return -EINVAL;
	}

	if (!start) {

		/* Are we allowed to unload the media? */
		if (curlun->prevent_medium_removal) {
			LDBG(curlun, "unload attempt prevented\n");
			curlun->sense_data = SS_MEDIUM_REMOVAL_PREVENTED;
			return -EINVAL;
		}
		if (loej) {		// Simulate an unload/eject
			up_read(&fsg->filesem);
			down_write(&fsg->filesem);
			fsg_lun_close(curlun);
			up_write(&fsg->filesem);
			down_read(&fsg->filesem);
		}
	} else {

		/* Our emulation doesn't support mounting; the medium is
		 * available for use as soon as it is loaded. */
		if (!fsg_lun_is_open(curlun)) {
			curlun->sense_data = SS_MEDIUM_NOT_PRESENT;
			return -EINVAL;
		}
	}
#endif
	return 0;
}


static int do_prevent_allow(struct fsg_dev *fsg)
{
	struct fsg_lun	*curlun = fsg->curlun;
	int		prevent;

	if (!mod_data.removable) {
		curlun->sense_data = SS_INVALID_COMMAND;
		return -EINVAL;
	}

	prevent = fsg->cmnd[4] & 0x01;
	if ((fsg->cmnd[4] & ~0x01) != 0) {		// Mask away Prevent
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
	struct fsg_lun	*curlun = fsg->curlun;
	u8		*buf = (u8 *) bh->buf;

	buf[0] = buf[1] = buf[2] = 0;
	buf[3] = 8;		// Only the Current/Maximum Capacity Descriptor
	buf += 4;

	put_unaligned_be32(curlun->num_sectors, &buf[0]);
						/* Number of blocks */
	put_unaligned_be32(512, &buf[4]);	/* Block length */
	buf[4] = 0x02;				/* Current capacity */
	return 12;
}


static int do_mode_select(struct fsg_dev *fsg, struct fsg_buffhd *bh)
{
	struct fsg_lun	*curlun = fsg->curlun;

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
	struct fsg_buffhd	*bh = fsg->next_buffhd_to_fill;
	u32			nkeep = bh->inreq->length;
	u32			nsend;
	int			rc;

	bh->state = BUF_STATE_EMPTY;		// For the first iteration
	fsg->usb_amount_left = nkeep + fsg->residue;
	while (fsg->usb_amount_left > 0) {

		/* Wait for the next buffer to be free */
		while (bh->state != BUF_STATE_EMPTY) {
			rc = sleep_thread(fsg);
			if (rc)
				return rc;
		}

		nsend = min(fsg->usb_amount_left, (u32) mod_data.buflen);
		memset(bh->buf + nkeep, 0, nsend - nkeep);
		bh->inreq->length = nsend;
		bh->inreq->zero = 0;
		start_transfer(fsg, fsg->bulk_in, bh->inreq,
				&bh->inreq_busy, &bh->state);
		bh = fsg->next_buffhd_to_fill = bh->next;
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
			amount = min(fsg->usb_amount_left,
					(u32) mod_data.buflen);

			/* amount is always divisible by 512, hence by
			 * the bulk-out maxpacket size */
			bh->outreq->length = bh->bulk_out_intended_length =
					amount;
			bh->outreq->short_not_ok = 1;
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
		break;			// Nothing to send

	/* If we don't know whether the host wants to read or write,
	 * this must be CB or CBI with an unknown command.  We mustn't
	 * try to send or receive any data.  So stall both bulk pipes
	 * if we can and wait for a reset. */
	case DATA_DIR_UNKNOWN:
		if (mod_data.can_stall) {
			fsg_set_halt(fsg, fsg->bulk_out);
			rc = halt_bulk_in_endpoint(fsg);
		}
		break;

	/* All but the last buffer of data must have already been sent */
	case DATA_DIR_TO_HOST:
		if (fsg->data_size == 0)
			;		// Nothing to send

		/* If there's no residue, simply send the last buffer */
		else if (fsg->residue == 0) {
			bh->inreq->zero = 0;
			start_transfer(fsg, fsg->bulk_in, bh->inreq,
					&bh->inreq_busy, &bh->state);
			fsg->next_buffhd_to_fill = bh->next;
		}

		/* There is a residue.  For CB and CBI, simply mark the end
		 * of the data with a short packet.  However, if we are
		 * allowed to stall, there was no data at all (residue ==
		 * data_size), and the command failed (invalid LUN or
		 * sense data is set), then halt the bulk-in endpoint
		 * instead. */
		else if (!transport_is_bbb()) {
			if (mod_data.can_stall &&
					fsg->residue == fsg->data_size &&
	(!fsg->curlun || fsg->curlun->sense_data != SS_NO_SENSE)) {
				bh->state = BUF_STATE_EMPTY;
				rc = halt_bulk_in_endpoint(fsg);
			} else {
				bh->inreq->zero = 1;
				start_transfer(fsg, fsg->bulk_in, bh->inreq,
						&bh->inreq_busy, &bh->state);
				fsg->next_buffhd_to_fill = bh->next;
			}
		}

		/* For Bulk-only, if we're allowed to stall then send the
		 * short packet and halt the bulk-in endpoint.  If we can't
		 * stall, pad out the remaining data with 0's. */
		else {
			if (mod_data.can_stall) {
				bh->inreq->zero = 1;
				start_transfer(fsg, fsg->bulk_in, bh->inreq,
						&bh->inreq_busy, &bh->state);
				fsg->next_buffhd_to_fill = bh->next;
				rc = halt_bulk_in_endpoint(fsg);
			} else
				rc = pad_with_zeros(fsg);
		}
		break;

	/* We have processed all we want from the data the host has sent.
	 * There may still be outstanding bulk-out requests. */
	case DATA_DIR_FROM_HOST:
		if (fsg->residue == 0)
			;		// Nothing to receive

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
		else if (mod_data.can_stall) {
			fsg_set_halt(fsg, fsg->bulk_out);
			raise_exception(fsg, FSG_STATE_ABORT_BULK_OUT);
			rc = -EINTR;
		}
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
	struct fsg_lun		*curlun = fsg->curlun;
	struct fsg_buffhd	*bh;
	int			rc;
	u8			status = USB_STATUS_PASS;
	u32			sd, sdinfo = 0;

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

	if (transport_is_bbb()) {
		struct bulk_cs_wrap	*csw = bh->buf;

		/* Store and send the Bulk-only CSW */
		csw->Signature = cpu_to_le32(USB_BULK_CS_SIG);
		csw->Tag = fsg->tag;
		csw->Residue = cpu_to_le32(fsg->residue);
		csw->Status = status;

		bh->inreq->length = USB_BULK_CS_WRAP_LEN;
		bh->inreq->zero = 0;
		start_transfer(fsg, fsg->bulk_in, bh->inreq,
				&bh->inreq_busy, &bh->state);

	} else if (mod_data.transport_type == USB_PR_CB) {

		/* Control-Bulk transport has no status phase! */
		return 0;

	} else {			// USB_PR_CBI
		struct interrupt_data	*buf = bh->buf;

		/* Store and send the Interrupt data.  UFI sends the ASC
		 * and ASCQ bytes.  Everything else sends a Type (which
		 * is always 0) and the status Value. */
		if (mod_data.protocol_type == USB_SC_UFI) {
			buf->bType = ASC(sd);
			buf->bValue = ASCQ(sd);
		} else {
			buf->bType = 0;
			buf->bValue = status;
		}
		fsg->intreq->length = CBI_INTERRUPT_DATA_LEN;

		fsg->intr_buffhd = bh;		// Point to the right buffhd
		fsg->intreq->buf = bh->inreq->buf;
		fsg->intreq->context = bh;
		start_transfer(fsg, fsg->intr_in, fsg->intreq,
				&fsg->intreq_busy, &bh->state);
	}

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
	struct fsg_lun		*curlun;

	/* Adjust the expected cmnd_size for protocol encapsulation padding.
	 * Transparent SCSI doesn't pad. */
	if (protocol_is_scsi())
		;

	/* There's some disagreement as to whether RBC pads commands or not.
	 * We'll play it safe and accept either form. */
	else if (mod_data.protocol_type == USB_SC_RBC) {
		if (fsg->cmnd_size == 12)
			cmnd_size = 12;

	/* All the other protocols pad to 12 bytes */
	} else
		cmnd_size = 12;

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
	if (fsg->data_dir == DATA_DIR_UNKNOWN) {	// CB or CBI
		fsg->data_dir = data_dir;
		fsg->data_size = fsg->data_size_from_cmnd;

	} else {					// Bulk-only
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
	if (cmnd_size != fsg->cmnd_size) {

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
		if (cmnd_size <= fsg->cmnd_size) {
			DBG(fsg, "%s is buggy! Expected length %d "
					"but we got %d\n", name,
					cmnd_size, fsg->cmnd_size);
			cmnd_size = fsg->cmnd_size;
		} else {
			fsg->phase_error = 1;
			return -EINVAL;
		}
	}

	/* Check that the LUN values are consistent */
	if (transport_is_bbb()) {
		if (fsg->lun != lun)
			DBG(fsg, "using LUN %d from CBW, "
					"not LUN %d from CDB\n",
					fsg->lun, lun);
	} else
		fsg->lun = lun;		// Use LUN from the command

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
	fsg->cmnd[1] &= 0x1f;			// Mask away the LUN
	for (i = 1; i < cmnd_size; ++i) {
		if (fsg->cmnd[i] && !(mask & (1 << i))) {
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

	down_read(&fsg->filesem);	// We're using the backing file
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
		fsg->data_size_from_cmnd = get_unaligned_be16(&fsg->cmnd[7]);
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
		fsg->data_size_from_cmnd = get_unaligned_be16(&fsg->cmnd[7]);
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
		fsg->data_size_from_cmnd =
				get_unaligned_be16(&fsg->cmnd[7]) << 9;
		if ((reply = check_command(fsg, 10, DATA_DIR_TO_HOST,
				(1<<1) | (0xf<<2) | (3<<7), 1,
				"READ(10)")) == 0)
			reply = do_read(fsg);
		break;

	case SC_READ_12:
		fsg->data_size_from_cmnd =
				get_unaligned_be32(&fsg->cmnd[6]) << 9;
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

	case SC_READ_HEADER:
		if (!mod_data.cdrom)
			goto unknown_cmnd;
		fsg->data_size_from_cmnd = get_unaligned_be16(&fsg->cmnd[7]);
		if ((reply = check_command(fsg, 10, DATA_DIR_TO_HOST,
				(3<<7) | (0x1f<<1), 1,
				"READ HEADER")) == 0)
			reply = do_read_header(fsg, bh);
		break;

	case SC_READ_TOC:
		if (!mod_data.cdrom)
			goto unknown_cmnd;
		fsg->data_size_from_cmnd = get_unaligned_be16(&fsg->cmnd[7]);
		if ((reply = check_command(fsg, 10, DATA_DIR_TO_HOST,
				(7<<6) | (1<<1), 1,
				"READ TOC")) == 0)
			reply = do_read_toc(fsg, bh);
		break;

	case SC_READ_FORMAT_CAPACITIES:
		fsg->data_size_from_cmnd = get_unaligned_be16(&fsg->cmnd[7]);
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
		fsg->data_size_from_cmnd =
				get_unaligned_be16(&fsg->cmnd[7]) << 9;
		if ((reply = check_command(fsg, 10, DATA_DIR_FROM_HOST,
				(1<<1) | (0xf<<2) | (3<<7), 1,
				"WRITE(10)")) == 0)
			reply = do_write(fsg);
		break;

	case SC_WRITE_12:
		fsg->data_size_from_cmnd =
				get_unaligned_be32(&fsg->cmnd[6]) << 9;
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
		// Fall through

	default:
 unknown_cmnd:
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

	if (reply == -EINTR || signal_pending(current))
		return -EINTR;

	/* Set up the single reply buffer for finish_reply() */
	if (reply == -EINVAL)
		reply = 0;		// Error reply length
	if (reply >= 0 && fsg->data_dir == DATA_DIR_TO_HOST) {
		reply = min((u32) reply, fsg->data_size_from_cmnd);
		bh->inreq->length = reply;
		bh->state = BUF_STATE_FULL;
		fsg->residue -= reply;
	}				// Otherwise it's already set

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
		if (mod_data.can_stall) {
			fsg_set_halt(fsg, fsg->bulk_out);
			halt_bulk_in_endpoint(fsg);
		}
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

	if (transport_is_bbb()) {

		/* Wait for the next buffer to become available */
		bh = fsg->next_buffhd_to_fill;
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

	} else {		// USB_PR_CB or USB_PR_CBI

		/* Wait for the next command to arrive */
		while (fsg->cbbuf_cmnd_size == 0) {
			rc = sleep_thread(fsg);
			if (rc)
				return rc;
		}

		/* Is the previous status interrupt request still busy?
		 * The host is allowed to skip reading the status,
		 * so we must cancel it. */
		if (fsg->intreq_busy)
			usb_ep_dequeue(fsg->intr_in, fsg->intreq);

		/* Copy the command and mark the buffer empty */
		fsg->data_dir = DATA_DIR_UNKNOWN;
		spin_lock_irq(&fsg->lock);
		fsg->cmnd_size = fsg->cbbuf_cmnd_size;
		memcpy(fsg->cmnd, fsg->cbbuf_cmnd, fsg->cmnd_size);
		fsg->cbbuf_cmnd_size = 0;
		spin_unlock_irq(&fsg->lock);
	}
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
	if (fsg->intreq) {
		usb_ep_free_request(fsg->intr_in, fsg->intreq);
		fsg->intreq = NULL;
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
	if (fsg->intr_in_enabled) {
		usb_ep_disable(fsg->intr_in);
		fsg->intr_in_enabled = 0;
	}

	fsg->running = 0;
	if (altsetting < 0 || rc != 0)
		return rc;

	DBG(fsg, "set interface %d\n", altsetting);

	/* Enable the endpoints */
	d = fsg_ep_desc(fsg->gadget,
			&fsg_fs_bulk_in_desc, &fsg_hs_bulk_in_desc);
	if ((rc = enable_endpoint(fsg, fsg->bulk_in, d)) != 0)
		goto reset;
	fsg->bulk_in_enabled = 1;

	d = fsg_ep_desc(fsg->gadget,
			&fsg_fs_bulk_out_desc, &fsg_hs_bulk_out_desc);
	if ((rc = enable_endpoint(fsg, fsg->bulk_out, d)) != 0)
		goto reset;
	fsg->bulk_out_enabled = 1;
	fsg->bulk_out_maxpacket = le16_to_cpu(d->wMaxPacketSize);
	clear_bit(IGNORE_BULK_OUT, &fsg->atomic_bitflags);

	if (transport_is_cbi()) {
		d = fsg_ep_desc(fsg->gadget,
				&fsg_fs_intr_in_desc, &fsg_hs_intr_in_desc);
		if ((rc = enable_endpoint(fsg, fsg->intr_in, d)) != 0)
			goto reset;
		fsg->intr_in_enabled = 1;
	}

	/* Allocate the requests */
	for (i = 0; i < FSG_NUM_BUFFERS; ++i) {
		struct fsg_buffhd	*bh = &fsg->buffhds[i];

		if ((rc = alloc_request(fsg, fsg->bulk_in, &bh->inreq)) != 0)
			goto reset;
		if ((rc = alloc_request(fsg, fsg->bulk_out, &bh->outreq)) != 0)
			goto reset;
		bh->inreq->buf = bh->outreq->buf = bh->buf;
		bh->inreq->context = bh->outreq->context = bh;
		bh->inreq->complete = bulk_in_complete;
		bh->outreq->complete = bulk_out_complete;
	}
	if (transport_is_cbi()) {
		if ((rc = alloc_request(fsg, fsg->intr_in, &fsg->intreq)) != 0)
			goto reset;
		fsg->intreq->complete = intr_in_complete;
	}

	fsg->running = 1;
	for (i = 0; i < fsg->nluns; ++i)
		fsg->luns[i].unit_attention_data = SS_RESET_OCCURRED;
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
		if ((rc = do_set_interface(fsg, 0)) != 0)
			fsg->config = 0;	// Reset on errors
		else {
			char *speed;

			switch (fsg->gadget->speed) {
			case USB_SPEED_LOW:	speed = "low";	break;
			case USB_SPEED_FULL:	speed = "full";	break;
			case USB_SPEED_HIGH:	speed = "high";	break;
			default: 		speed = "?";	break;
			}
			INFO(fsg, "%s speed config #%d\n", speed, fsg->config);
		}
	}
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
	if (fsg->intreq_busy)
		usb_ep_dequeue(fsg->intr_in, fsg->intreq);
	for (i = 0; i < FSG_NUM_BUFFERS; ++i) {
		bh = &fsg->buffhds[i];
		if (bh->inreq_busy)
			usb_ep_dequeue(fsg->bulk_in, bh->inreq);
		if (bh->outreq_busy)
			usb_ep_dequeue(fsg->bulk_out, bh->outreq);
	}

	/* Wait until everything is idle */
	for (;;) {
		num_active = fsg->intreq_busy;
		for (i = 0; i < FSG_NUM_BUFFERS; ++i) {
			bh = &fsg->buffhds[i];
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
	if (fsg->intr_in_enabled)
		usb_ep_fifo_flush(fsg->intr_in);

	/* Reset the I/O buffer states and pointers, the SCSI
	 * state, and the exception.  Then invoke the handler. */
	spin_lock_irq(&fsg->lock);

	for (i = 0; i < FSG_NUM_BUFFERS; ++i) {
		bh = &fsg->buffhds[i];
		bh->state = BUF_STATE_EMPTY;
	}
	fsg->next_buffhd_to_fill = fsg->next_buffhd_to_drain =
			&fsg->buffhds[0];

	exception_req_tag = fsg->exception_req_tag;
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
	spin_unlock_irq(&fsg->lock);

	/* Carry out any extra actions required for the exception */
	switch (old_state) {
	default:
		break;

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

		if (transport_is_bbb()) {
			if (fsg->ep0_req_tag == exception_req_tag)
				ep0_queue(fsg);	// Complete the status stage

		} else if (transport_is_cbi())
			send_status(fsg);	// Status by interrupt pipe

		/* Technically this should go here, but it would only be
		 * a waste of time.  Ditto for the INTERFACE_CHANGE and
		 * CONFIG_CHANGE cases. */
		// for (i = 0; i < fsg->nluns; ++i)
		//	fsg->luns[i].unit_attention_data = SS_RESET_OCCURRED;
		break;

	case FSG_STATE_INTERFACE_CHANGE:
		rc = do_set_interface(fsg, 0);
		if (fsg->ep0_req_tag != exception_req_tag)
			break;
		if (rc != 0)			// STALL on errors
			fsg_set_halt(fsg, fsg->ep0);
		else				// Complete the status stage
			ep0_queue(fsg);
		break;

	case FSG_STATE_CONFIG_CHANGE:
		rc = do_set_config(fsg, new_config);
		if (fsg->ep0_req_tag != exception_req_tag)
			break;
		if (rc != 0)			// STALL on errors
			fsg_set_halt(fsg, fsg->ep0);
		else				// Complete the status stage
			ep0_queue(fsg);
		break;

	case FSG_STATE_DISCONNECT:
		for (i = 0; i < fsg->nluns; ++i)
			fsg_lun_fsync_sub(fsg->luns + i);
		do_set_config(fsg, 0);		// Unconfigured state
		break;

	case FSG_STATE_EXIT:
	case FSG_STATE_TERMINATED:
		do_set_config(fsg, 0);			// Free resources
		spin_lock_irq(&fsg->lock);
		fsg->state = FSG_STATE_TERMINATED;	// Stop the thread
		spin_unlock_irq(&fsg->lock);
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

	/* If we are exiting because of a signal, unregister the
	 * gadget driver. */
	if (test_and_clear_bit(REGISTERED, &fsg->atomic_bitflags))
		usb_gadget_unregister_driver(&fsg_driver);

	/* Let the unbind and cleanup routines know the thread has exited */
	complete_and_exit(&fsg->thread_notifier, 0);
}


/*-------------------------------------------------------------------------*/


/* The write permissions and store_xxx pointers are set in fsg_bind() */
static DEVICE_ATTR(ro, 0444, fsg_show_ro, NULL);
static DEVICE_ATTR(nofua, 0644, fsg_show_nofua, NULL);
static DEVICE_ATTR(file, 0444, fsg_show_file, NULL);


/*-------------------------------------------------------------------------*/

static void fsg_release(struct kref *ref)
{
	struct fsg_dev	*fsg = container_of(ref, struct fsg_dev, ref);

	kfree(fsg->luns);
	kfree(fsg);
}

static void lun_release(struct device *dev)
{
	struct rw_semaphore	*filesem = dev_get_drvdata(dev);
	struct fsg_dev		*fsg =
		container_of(filesem, struct fsg_dev, filesem);

	kref_put(&fsg->ref, fsg_release);
}

static void /* __init_or_exit */ fsg_unbind(struct usb_gadget *gadget)
{
	struct fsg_dev		*fsg = get_gadget_data(gadget);
	int			i;
	struct fsg_lun		*curlun;
	struct usb_request	*req = fsg->ep0req;

	DBG(fsg, "unbind\n");
	clear_bit(REGISTERED, &fsg->atomic_bitflags);

	/* Unregister the sysfs attribute files and the LUNs */
	for (i = 0; i < fsg->nluns; ++i) {
		curlun = &fsg->luns[i];
		if (curlun->registered) {
			device_remove_file(&curlun->dev, &dev_attr_nofua);
			device_remove_file(&curlun->dev, &dev_attr_ro);
			device_remove_file(&curlun->dev, &dev_attr_file);
			fsg_lun_close(curlun);
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
	for (i = 0; i < FSG_NUM_BUFFERS; ++i)
		kfree(fsg->buffhds[i].buf);

	/* Free the request and buffer for endpoint 0 */
	if (req) {
		kfree(req->buf);
		usb_ep_free_request(fsg->ep0, req);
	}

	set_gadget_data(gadget, NULL);
}


static int __init check_parameters(struct fsg_dev *fsg)
{
	int	prot;
	int	gcnum;
	int	i;

	/* Store the default values */
	mod_data.transport_type = USB_PR_BULK;
	mod_data.transport_name = "Bulk-only";
	mod_data.protocol_type = USB_SC_SCSI;
	mod_data.protocol_name = "Transparent SCSI";

	/* Some peripheral controllers are known not to be able to
	 * halt bulk endpoints correctly.  If one of them is present,
	 * disable stalls.
	 */
	if (gadget_is_at91(fsg->gadget))
		mod_data.can_stall = 0;

	if (mod_data.release == 0xffff) {	// Parameter wasn't set
		gcnum = usb_gadget_controller_number(fsg->gadget);
		if (gcnum >= 0)
			mod_data.release = 0x0300 + gcnum;
		else {
			WARNING(fsg, "controller '%s' not recognized\n",
				fsg->gadget->name);
			mod_data.release = 0x0399;
		}
	}

	prot = simple_strtol(mod_data.protocol_parm, NULL, 0);

#ifdef CONFIG_USB_FILE_STORAGE_TEST
	if (strnicmp(mod_data.transport_parm, "BBB", 10) == 0) {
		;		// Use default setting
	} else if (strnicmp(mod_data.transport_parm, "CB", 10) == 0) {
		mod_data.transport_type = USB_PR_CB;
		mod_data.transport_name = "Control-Bulk";
	} else if (strnicmp(mod_data.transport_parm, "CBI", 10) == 0) {
		mod_data.transport_type = USB_PR_CBI;
		mod_data.transport_name = "Control-Bulk-Interrupt";
	} else {
		ERROR(fsg, "invalid transport: %s\n", mod_data.transport_parm);
		return -EINVAL;
	}

	if (strnicmp(mod_data.protocol_parm, "SCSI", 10) == 0 ||
			prot == USB_SC_SCSI) {
		;		// Use default setting
	} else if (strnicmp(mod_data.protocol_parm, "RBC", 10) == 0 ||
			prot == USB_SC_RBC) {
		mod_data.protocol_type = USB_SC_RBC;
		mod_data.protocol_name = "RBC";
	} else if (strnicmp(mod_data.protocol_parm, "8020", 4) == 0 ||
			strnicmp(mod_data.protocol_parm, "ATAPI", 10) == 0 ||
			prot == USB_SC_8020) {
		mod_data.protocol_type = USB_SC_8020;
		mod_data.protocol_name = "8020i (ATAPI)";
	} else if (strnicmp(mod_data.protocol_parm, "QIC", 3) == 0 ||
			prot == USB_SC_QIC) {
		mod_data.protocol_type = USB_SC_QIC;
		mod_data.protocol_name = "QIC-157";
	} else if (strnicmp(mod_data.protocol_parm, "UFI", 10) == 0 ||
			prot == USB_SC_UFI) {
		mod_data.protocol_type = USB_SC_UFI;
		mod_data.protocol_name = "UFI";
	} else if (strnicmp(mod_data.protocol_parm, "8070", 4) == 0 ||
			prot == USB_SC_8070) {
		mod_data.protocol_type = USB_SC_8070;
		mod_data.protocol_name = "8070i";
	} else {
		ERROR(fsg, "invalid protocol: %s\n", mod_data.protocol_parm);
		return -EINVAL;
	}

	mod_data.buflen &= PAGE_CACHE_MASK;
	if (mod_data.buflen <= 0) {
		ERROR(fsg, "invalid buflen\n");
		return -ETOOSMALL;
	}

#endif /* CONFIG_USB_FILE_STORAGE_TEST */

	/* Serial string handling.
	 * On a real device, the serial string would be loaded
	 * from permanent storage. */
	if (mod_data.serial) {
		const char *ch;
		unsigned len = 0;

		/* Sanity check :
		 * The CB[I] specification limits the serial string to
		 * 12 uppercase hexadecimal characters.
		 * BBB need at least 12 uppercase hexadecimal characters,
		 * with a maximum of 126. */
		for (ch = mod_data.serial; *ch; ++ch) {
			++len;
			if ((*ch < '0' || *ch > '9') &&
			    (*ch < 'A' || *ch > 'F')) { /* not uppercase hex */
				WARNING(fsg,
					"Invalid serial string character: %c; "
					"Failing back to default\n",
					*ch);
				goto fill_serial;
			}
		}
		if (len > 126 ||
		    (mod_data.transport_type == USB_PR_BULK && len < 12) ||
		    (mod_data.transport_type != USB_PR_BULK && len > 12)) {
			WARNING(fsg,
				"Invalid serial string length; "
				"Failing back to default\n");
			goto fill_serial;
		}
		fsg_strings[FSG_STRING_SERIAL - 1].s = mod_data.serial;
	} else {
		WARNING(fsg,
			"Userspace failed to provide serial number; "
			"Failing back to default\n");
fill_serial:
		/* Serial number not specified or invalid, make our own.
		 * We just encode it from the driver version string,
		 * 12 characters to comply with both CB[I] and BBB spec.
		 * Warning : Two devices running the same kernel will have
		 * the same fallback serial number. */
		for (i = 0; i < 12; i += 2) {
			unsigned char	c = DRIVER_VERSION[i / 2];

			if (!c)
				break;
			sprintf(&fsg_string_serial[i], "%02X", c);
		}
	}

	return 0;
}


static int __ref fsg_bind(struct usb_gadget *gadget)
{
	struct fsg_dev		*fsg = the_fsg;
	int			rc;
	int			i;
	struct fsg_lun		*curlun;
	struct usb_ep		*ep;
	struct usb_request	*req;
	char			*pathbuf, *p;

	fsg->gadget = gadget;
	set_gadget_data(gadget, fsg);
	fsg->ep0 = gadget->ep0;
	fsg->ep0->driver_data = fsg;

	if ((rc = check_parameters(fsg)) != 0)
		goto out;

	if (mod_data.removable) {	// Enable the store_xxx attributes
		dev_attr_file.attr.mode = 0644;
		dev_attr_file.store = fsg_store_file;
		if (!mod_data.cdrom) {
			dev_attr_ro.attr.mode = 0644;
			dev_attr_ro.store = fsg_store_ro;
		}
	}

	/* Only for removable media? */
	dev_attr_nofua.attr.mode = 0644;
	dev_attr_nofua.store = fsg_store_nofua;

	/* Find out how many LUNs there should be */
	i = mod_data.nluns;
	if (i == 0)
		i = max(mod_data.num_filenames, 1u);
	if (i > FSG_MAX_LUNS) {
		ERROR(fsg, "invalid number of LUNs: %d\n", i);
		rc = -EINVAL;
		goto out;
	}

	/* Create the LUNs, open their backing files, and register the
	 * LUN devices in sysfs. */
	fsg->luns = kzalloc(i * sizeof(struct fsg_lun), GFP_KERNEL);
	if (!fsg->luns) {
		rc = -ENOMEM;
		goto out;
	}
	fsg->nluns = i;

	for (i = 0; i < fsg->nluns; ++i) {
		curlun = &fsg->luns[i];
		curlun->cdrom = !!mod_data.cdrom;
		curlun->ro = mod_data.cdrom || mod_data.ro[i];
		curlun->initially_ro = curlun->ro;
		curlun->removable = mod_data.removable;
		curlun->nofua = mod_data.nofua[i];
		curlun->dev.release = lun_release;
		curlun->dev.parent = &gadget->dev;
		curlun->dev.driver = &fsg_driver.driver;
		dev_set_drvdata(&curlun->dev, &fsg->filesem);
		dev_set_name(&curlun->dev,"%s-lun%d",
			     dev_name(&gadget->dev), i);

		if ((rc = device_register(&curlun->dev)) != 0) {
			INFO(fsg, "failed to register LUN%d: %d\n", i, rc);
			goto out;
		}
		if ((rc = device_create_file(&curlun->dev,
					&dev_attr_ro)) != 0 ||
				(rc = device_create_file(&curlun->dev,
					&dev_attr_nofua)) != 0 ||
				(rc = device_create_file(&curlun->dev,
					&dev_attr_file)) != 0) {
			device_unregister(&curlun->dev);
			goto out;
		}
		curlun->registered = 1;
		kref_get(&fsg->ref);

		if (mod_data.file[i] && *mod_data.file[i]) {
			if ((rc = fsg_lun_open(curlun,
					mod_data.file[i])) != 0)
				goto out;
		} else if (!mod_data.removable) {
			ERROR(fsg, "no file given for LUN%d\n", i);
			rc = -EINVAL;
			goto out;
		}
	}

	/* Find all the endpoints we will use */
	usb_ep_autoconfig_reset(gadget);
	ep = usb_ep_autoconfig(gadget, &fsg_fs_bulk_in_desc);
	if (!ep)
		goto autoconf_fail;
	ep->driver_data = fsg;		// claim the endpoint
	fsg->bulk_in = ep;

	ep = usb_ep_autoconfig(gadget, &fsg_fs_bulk_out_desc);
	if (!ep)
		goto autoconf_fail;
	ep->driver_data = fsg;		// claim the endpoint
	fsg->bulk_out = ep;

	if (transport_is_cbi()) {
		ep = usb_ep_autoconfig(gadget, &fsg_fs_intr_in_desc);
		if (!ep)
			goto autoconf_fail;
		ep->driver_data = fsg;		// claim the endpoint
		fsg->intr_in = ep;
	}

	/* Fix up the descriptors */
	device_desc.bMaxPacketSize0 = fsg->ep0->maxpacket;
	device_desc.idVendor = cpu_to_le16(mod_data.vendor);
	device_desc.idProduct = cpu_to_le16(mod_data.product);
	device_desc.bcdDevice = cpu_to_le16(mod_data.release);

	i = (transport_is_cbi() ? 3 : 2);	// Number of endpoints
	fsg_intf_desc.bNumEndpoints = i;
	fsg_intf_desc.bInterfaceSubClass = mod_data.protocol_type;
	fsg_intf_desc.bInterfaceProtocol = mod_data.transport_type;
	fsg_fs_function[i + FSG_FS_FUNCTION_PRE_EP_ENTRIES] = NULL;

	if (gadget_is_dualspeed(gadget)) {
		fsg_hs_function[i + FSG_HS_FUNCTION_PRE_EP_ENTRIES] = NULL;

		/* Assume ep0 uses the same maxpacket value for both speeds */
		dev_qualifier.bMaxPacketSize0 = fsg->ep0->maxpacket;

		/* Assume endpoint addresses are the same for both speeds */
		fsg_hs_bulk_in_desc.bEndpointAddress =
			fsg_fs_bulk_in_desc.bEndpointAddress;
		fsg_hs_bulk_out_desc.bEndpointAddress =
			fsg_fs_bulk_out_desc.bEndpointAddress;
		fsg_hs_intr_in_desc.bEndpointAddress =
			fsg_fs_intr_in_desc.bEndpointAddress;
	}

	if (gadget_is_otg(gadget))
		fsg_otg_desc.bmAttributes |= USB_OTG_HNP;

	rc = -ENOMEM;

	/* Allocate the request and buffer for endpoint 0 */
	fsg->ep0req = req = usb_ep_alloc_request(fsg->ep0, GFP_KERNEL);
	if (!req)
		goto out;
	req->buf = kmalloc(EP0_BUFSIZE, GFP_KERNEL);
	if (!req->buf)
		goto out;
	req->complete = ep0_complete;

	/* Allocate the data buffers */
	for (i = 0; i < FSG_NUM_BUFFERS; ++i) {
		struct fsg_buffhd	*bh = &fsg->buffhds[i];

		/* Allocate for the bulk-in endpoint.  We assume that
		 * the buffer will also work with the bulk-out (and
		 * interrupt-in) endpoint. */
		bh->buf = kmalloc(mod_data.buflen, GFP_KERNEL);
		if (!bh->buf)
			goto out;
		bh->next = bh + 1;
	}
	fsg->buffhds[FSG_NUM_BUFFERS - 1].next = &fsg->buffhds[0];

	/* This should reflect the actual gadget power source */
	usb_gadget_set_selfpowered(gadget);

	snprintf(fsg_string_manufacturer, sizeof fsg_string_manufacturer,
			"%s %s with %s",
			init_utsname()->sysname, init_utsname()->release,
			gadget->name);

	fsg->thread_task = kthread_create(fsg_main_thread, fsg,
			"file-storage-gadget");
	if (IS_ERR(fsg->thread_task)) {
		rc = PTR_ERR(fsg->thread_task);
		goto out;
	}

	INFO(fsg, DRIVER_DESC ", version: " DRIVER_VERSION "\n");
	INFO(fsg, "Number of LUNs=%d\n", fsg->nluns);

	pathbuf = kmalloc(PATH_MAX, GFP_KERNEL);
	for (i = 0; i < fsg->nluns; ++i) {
		curlun = &fsg->luns[i];
		if (fsg_lun_is_open(curlun)) {
			p = NULL;
			if (pathbuf) {
				p = d_path(&curlun->filp->f_path,
					   pathbuf, PATH_MAX);
				if (IS_ERR(p))
					p = NULL;
			}
			LINFO(curlun, "ro=%d, nofua=%d, file: %s\n",
			      curlun->ro, curlun->nofua, (p ? p : "(error)"));
		}
	}
	kfree(pathbuf);

	DBG(fsg, "transport=%s (x%02x)\n",
			mod_data.transport_name, mod_data.transport_type);
	DBG(fsg, "protocol=%s (x%02x)\n",
			mod_data.protocol_name, mod_data.protocol_type);
	DBG(fsg, "VendorID=x%04x, ProductID=x%04x, Release=x%04x\n",
			mod_data.vendor, mod_data.product, mod_data.release);
	DBG(fsg, "removable=%d, stall=%d, cdrom=%d, buflen=%u\n",
			mod_data.removable, mod_data.can_stall,
			mod_data.cdrom, mod_data.buflen);
	DBG(fsg, "I/O thread pid: %d\n", task_pid_nr(fsg->thread_task));

	set_bit(REGISTERED, &fsg->atomic_bitflags);

	/* Tell the thread to start working */
	wake_up_process(fsg->thread_task);
	return 0;

autoconf_fail:
	ERROR(fsg, "unable to autoconfigure all endpoints\n");
	rc = -ENOTSUPP;

out:
	fsg->state = FSG_STATE_TERMINATED;	// The thread is dead
	fsg_unbind(gadget);
	complete(&fsg->thread_notifier);
	return rc;
}


/*-------------------------------------------------------------------------*/

static void fsg_suspend(struct usb_gadget *gadget)
{
	struct fsg_dev		*fsg = get_gadget_data(gadget);

	DBG(fsg, "suspend\n");
	set_bit(SUSPENDED, &fsg->atomic_bitflags);
}

static void fsg_resume(struct usb_gadget *gadget)
{
	struct fsg_dev		*fsg = get_gadget_data(gadget);

	DBG(fsg, "resume\n");
	clear_bit(SUSPENDED, &fsg->atomic_bitflags);
}


/*-------------------------------------------------------------------------*/

static struct usb_gadget_driver		fsg_driver = {
#ifdef CONFIG_USB_GADGET_DUALSPEED
	.speed		= USB_SPEED_HIGH,
#else
	.speed		= USB_SPEED_FULL,
#endif
	.function	= (char *) fsg_string_product,
	.bind		= fsg_bind,
	.unbind		= fsg_unbind,
	.disconnect	= fsg_disconnect,
	.setup		= fsg_setup,
	.suspend	= fsg_suspend,
	.resume		= fsg_resume,

	.driver		= {
		.name		= DRIVER_NAME,
		.owner		= THIS_MODULE,
		// .release = ...
		// .suspend = ...
		// .resume = ...
	},
};


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


static int __init fsg_init(void)
{
	int		rc;
	struct fsg_dev	*fsg;

	if ((rc = fsg_alloc()) != 0)
		return rc;
	fsg = the_fsg;
	if ((rc = usb_gadget_register_driver(&fsg_driver)) != 0)
		kref_put(&fsg->ref, fsg_release);
	return rc;
}
module_init(fsg_init);


static void __exit fsg_cleanup(void)
{
	struct fsg_dev	*fsg = the_fsg;

	/* Unregister the driver iff the thread hasn't already done so */
	if (test_and_clear_bit(REGISTERED, &fsg->atomic_bitflags))
		usb_gadget_unregister_driver(&fsg_driver);

	/* Wait for the thread to finish up */
	wait_for_completion(&fsg->thread_notifier);

	kref_put(&fsg->ref, fsg_release);
}
module_exit(fsg_cleanup);
