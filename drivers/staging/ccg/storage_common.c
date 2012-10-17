/*
 * storage_common.c -- Common definitions for mass storage functionality
 *
 * Copyright (C) 2003-2008 Alan Stern
 * Copyeight (C) 2009 Samsung Electronics
 * Author: Michal Nazarewicz (mina86@mina86.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */


/*
 * This file requires the following identifiers used in USB strings to
 * be defined (each of type pointer to char):
 *  - fsg_string_manufacturer -- name of the manufacturer
 *  - fsg_string_product      -- name of the product
 *  - fsg_string_config       -- name of the configuration
 *  - fsg_string_interface    -- name of the interface
 * The first four are only needed when FSG_DESCRIPTORS_DEVICE_STRINGS
 * macro is defined prior to including this file.
 */

/*
 * When FSG_NO_INTR_EP is defined fsg_fs_intr_in_desc and
 * fsg_hs_intr_in_desc objects as well as
 * FSG_FS_FUNCTION_PRE_EP_ENTRIES and FSG_HS_FUNCTION_PRE_EP_ENTRIES
 * macros are not defined.
 *
 * When FSG_NO_DEVICE_STRINGS is defined FSG_STRING_MANUFACTURER,
 * FSG_STRING_PRODUCT, FSG_STRING_SERIAL and FSG_STRING_CONFIG are not
 * defined (as well as corresponding entries in string tables are
 * missing) and FSG_STRING_INTERFACE has value of zero.
 *
 * When FSG_NO_OTG is defined fsg_otg_desc won't be defined.
 */

/*
 * When USB_GADGET_DEBUG_FILES is defined the module param num_buffers
 * sets the number of pipeline buffers (length of the fsg_buffhd array).
 * The valid range of num_buffers is: num >= 2 && num <= 4.
 */


#include <linux/usb/storage.h>
#include <scsi/scsi.h>
#include <asm/unaligned.h>


/*
 * Thanks to NetChip Technologies for donating this product ID.
 *
 * DO NOT REUSE THESE IDs with any other driver!!  Ever!!
 * Instead:  allocate your own, using normal USB-IF procedures.
 */
#define FSG_VENDOR_ID	0x0525	/* NetChip */
#define FSG_PRODUCT_ID	0xa4a5	/* Linux-USB File-backed Storage Gadget */


/*-------------------------------------------------------------------------*/


#ifndef DEBUG
#undef VERBOSE_DEBUG
#undef DUMP_MSGS
#endif /* !DEBUG */

#ifdef VERBOSE_DEBUG
#define VLDBG	LDBG
#else
#define VLDBG(lun, fmt, args...) do { } while (0)
#endif /* VERBOSE_DEBUG */

#define LDBG(lun, fmt, args...)   dev_dbg (&(lun)->dev, fmt, ## args)
#define LERROR(lun, fmt, args...) dev_err (&(lun)->dev, fmt, ## args)
#define LWARN(lun, fmt, args...)  dev_warn(&(lun)->dev, fmt, ## args)
#define LINFO(lun, fmt, args...)  dev_info(&(lun)->dev, fmt, ## args)

/*
 * Keep those macros in sync with those in
 * include/linux/usb/composite.h or else GCC will complain.  If they
 * are identical (the same names of arguments, white spaces in the
 * same places) GCC will allow redefinition otherwise (even if some
 * white space is removed or added) warning will be issued.
 *
 * Those macros are needed here because File Storage Gadget does not
 * include the composite.h header.  For composite gadgets those macros
 * are redundant since composite.h is included any way.
 *
 * One could check whether those macros are already defined (which
 * would indicate composite.h had been included) or not (which would
 * indicate we were in FSG) but this is not done because a warning is
 * desired if definitions here differ from the ones in composite.h.
 *
 * We want the definitions to match and be the same in File Storage
 * Gadget as well as Mass Storage Function (and so composite gadgets
 * using MSF).  If someone changes them in composite.h it will produce
 * a warning in this file when building MSF.
 */
#define DBG(d, fmt, args...)     dev_dbg(&(d)->gadget->dev , fmt , ## args)
#define VDBG(d, fmt, args...)    dev_vdbg(&(d)->gadget->dev , fmt , ## args)
#define ERROR(d, fmt, args...)   dev_err(&(d)->gadget->dev , fmt , ## args)
#define WARNING(d, fmt, args...) dev_warn(&(d)->gadget->dev , fmt , ## args)
#define INFO(d, fmt, args...)    dev_info(&(d)->gadget->dev , fmt , ## args)



#ifdef DUMP_MSGS

#  define dump_msg(fsg, /* const char * */ label,			\
		   /* const u8 * */ buf, /* unsigned */ length) do {	\
	if (length < 512) {						\
		DBG(fsg, "%s, length %u:\n", label, length);		\
		print_hex_dump(KERN_DEBUG, "", DUMP_PREFIX_OFFSET,	\
			       16, 1, buf, length, 0);			\
	}								\
} while (0)

#  define dump_cdb(fsg) do { } while (0)

#else

#  define dump_msg(fsg, /* const char * */ label, \
		   /* const u8 * */ buf, /* unsigned */ length) do { } while (0)

#  ifdef VERBOSE_DEBUG

#    define dump_cdb(fsg)						\
	print_hex_dump(KERN_DEBUG, "SCSI CDB: ", DUMP_PREFIX_NONE,	\
		       16, 1, (fsg)->cmnd, (fsg)->cmnd_size, 0)		\

#  else

#    define dump_cdb(fsg) do { } while (0)

#  endif /* VERBOSE_DEBUG */

#endif /* DUMP_MSGS */

/*-------------------------------------------------------------------------*/

/* CBI Interrupt data structure */
struct interrupt_data {
	u8	bType;
	u8	bValue;
};

#define CBI_INTERRUPT_DATA_LEN		2

/* CBI Accept Device-Specific Command request */
#define USB_CBI_ADSC_REQUEST		0x00


/* Length of a SCSI Command Data Block */
#define MAX_COMMAND_SIZE	16

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


struct fsg_lun {
	struct file	*filp;
	loff_t		file_length;
	loff_t		num_sectors;

	unsigned int	initially_ro:1;
	unsigned int	ro:1;
	unsigned int	removable:1;
	unsigned int	cdrom:1;
	unsigned int	prevent_medium_removal:1;
	unsigned int	registered:1;
	unsigned int	info_valid:1;
	unsigned int	nofua:1;

	u32		sense_data;
	u32		sense_data_info;
	u32		unit_attention_data;

	unsigned int	blkbits;	/* Bits of logical block size of bound block device */
	unsigned int	blksize;	/* logical block size of bound block device */
	struct device	dev;
};

#define fsg_lun_is_open(curlun)	((curlun)->filp != NULL)

static struct fsg_lun *fsg_lun_from_dev(struct device *dev)
{
	return container_of(dev, struct fsg_lun, dev);
}


/* Big enough to hold our biggest descriptor */
#define EP0_BUFSIZE	256
#define DELAYED_STATUS	(EP0_BUFSIZE + 999)	/* An impossibly large value */

#ifdef CONFIG_USB_GADGET_DEBUG_FILES

static unsigned int fsg_num_buffers = CONFIG_USB_GADGET_STORAGE_NUM_BUFFERS;
module_param_named(num_buffers, fsg_num_buffers, uint, S_IRUGO);
MODULE_PARM_DESC(num_buffers, "Number of pipeline buffers");

#else

/*
 * Number of buffers we will use.
 * 2 is usually enough for good buffering pipeline
 */
#define fsg_num_buffers	CONFIG_USB_GADGET_STORAGE_NUM_BUFFERS

#endif /* CONFIG_USB_DEBUG */

/* check if fsg_num_buffers is within a valid range */
static inline int fsg_num_buffers_validate(void)
{
	if (fsg_num_buffers >= 2 && fsg_num_buffers <= 4)
		return 0;
	pr_err("fsg_num_buffers %u is out of range (%d to %d)\n",
	       fsg_num_buffers, 2 ,4);
	return -EINVAL;
}

/* Default size of buffer length. */
#define FSG_BUFLEN	((u32)16384)

/* Maximal number of LUNs supported in mass storage function */
#define FSG_MAX_LUNS	8

enum fsg_buffer_state {
	BUF_STATE_EMPTY = 0,
	BUF_STATE_FULL,
	BUF_STATE_BUSY
};

struct fsg_buffhd {
	void				*buf;
	enum fsg_buffer_state		state;
	struct fsg_buffhd		*next;

	/*
	 * The NetChip 2280 is faster, and handles some protocol faults
	 * better, if we don't submit any short bulk-out read requests.
	 * So we will record the intended request length here.
	 */
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
	FSG_STATE_INTERFACE_CHANGE,
	FSG_STATE_CONFIG_CHANGE,
	FSG_STATE_DISCONNECT,
	FSG_STATE_EXIT,
	FSG_STATE_TERMINATED
};

enum data_direction {
	DATA_DIR_UNKNOWN = 0,
	DATA_DIR_FROM_HOST,
	DATA_DIR_TO_HOST,
	DATA_DIR_NONE
};


/*-------------------------------------------------------------------------*/


static inline u32 get_unaligned_be24(u8 *buf)
{
	return 0xffffff & (u32) get_unaligned_be32(buf - 1);
}


/*-------------------------------------------------------------------------*/


enum {
#ifndef FSG_NO_DEVICE_STRINGS
	FSG_STRING_MANUFACTURER	= 1,
	FSG_STRING_PRODUCT,
	FSG_STRING_SERIAL,
	FSG_STRING_CONFIG,
#endif
	FSG_STRING_INTERFACE
};


#ifndef FSG_NO_OTG
static struct usb_otg_descriptor
fsg_otg_desc = {
	.bLength =		sizeof fsg_otg_desc,
	.bDescriptorType =	USB_DT_OTG,

	.bmAttributes =		USB_OTG_SRP,
};
#endif

/* There is only one interface. */

static struct usb_interface_descriptor
fsg_intf_desc = {
	.bLength =		sizeof fsg_intf_desc,
	.bDescriptorType =	USB_DT_INTERFACE,

	.bNumEndpoints =	2,		/* Adjusted during fsg_bind() */
	.bInterfaceClass =	USB_CLASS_MASS_STORAGE,
	.bInterfaceSubClass =	USB_SC_SCSI,	/* Adjusted during fsg_bind() */
	.bInterfaceProtocol =	USB_PR_BULK,	/* Adjusted during fsg_bind() */
	.iInterface =		FSG_STRING_INTERFACE,
};

/*
 * Three full-speed endpoint descriptors: bulk-in, bulk-out, and
 * interrupt-in.
 */

static struct usb_endpoint_descriptor
fsg_fs_bulk_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	/* wMaxPacketSize set by autoconfiguration */
};

static struct usb_endpoint_descriptor
fsg_fs_bulk_out_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	/* wMaxPacketSize set by autoconfiguration */
};

#ifndef FSG_NO_INTR_EP

static struct usb_endpoint_descriptor
fsg_fs_intr_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_INT,
	.wMaxPacketSize =	cpu_to_le16(2),
	.bInterval =		32,	/* frames -> 32 ms */
};

#ifndef FSG_NO_OTG
#  define FSG_FS_FUNCTION_PRE_EP_ENTRIES	2
#else
#  define FSG_FS_FUNCTION_PRE_EP_ENTRIES	1
#endif

#endif

static struct usb_descriptor_header *fsg_fs_function[] = {
#ifndef FSG_NO_OTG
	(struct usb_descriptor_header *) &fsg_otg_desc,
#endif
	(struct usb_descriptor_header *) &fsg_intf_desc,
	(struct usb_descriptor_header *) &fsg_fs_bulk_in_desc,
	(struct usb_descriptor_header *) &fsg_fs_bulk_out_desc,
#ifndef FSG_NO_INTR_EP
	(struct usb_descriptor_header *) &fsg_fs_intr_in_desc,
#endif
	NULL,
};


/*
 * USB 2.0 devices need to expose both high speed and full speed
 * descriptors, unless they only run at full speed.
 *
 * That means alternate endpoint descriptors (bigger packets)
 * and a "device qualifier" ... plus more construction options
 * for the configuration descriptor.
 */
static struct usb_endpoint_descriptor
fsg_hs_bulk_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	/* bEndpointAddress copied from fs_bulk_in_desc during fsg_bind() */
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(512),
};

static struct usb_endpoint_descriptor
fsg_hs_bulk_out_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	/* bEndpointAddress copied from fs_bulk_out_desc during fsg_bind() */
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(512),
	.bInterval =		1,	/* NAK every 1 uframe */
};

#ifndef FSG_NO_INTR_EP

static struct usb_endpoint_descriptor
fsg_hs_intr_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	/* bEndpointAddress copied from fs_intr_in_desc during fsg_bind() */
	.bmAttributes =		USB_ENDPOINT_XFER_INT,
	.wMaxPacketSize =	cpu_to_le16(2),
	.bInterval =		9,	/* 2**(9-1) = 256 uframes -> 32 ms */
};

#ifndef FSG_NO_OTG
#  define FSG_HS_FUNCTION_PRE_EP_ENTRIES	2
#else
#  define FSG_HS_FUNCTION_PRE_EP_ENTRIES	1
#endif

#endif

static struct usb_descriptor_header *fsg_hs_function[] = {
#ifndef FSG_NO_OTG
	(struct usb_descriptor_header *) &fsg_otg_desc,
#endif
	(struct usb_descriptor_header *) &fsg_intf_desc,
	(struct usb_descriptor_header *) &fsg_hs_bulk_in_desc,
	(struct usb_descriptor_header *) &fsg_hs_bulk_out_desc,
#ifndef FSG_NO_INTR_EP
	(struct usb_descriptor_header *) &fsg_hs_intr_in_desc,
#endif
	NULL,
};

static struct usb_endpoint_descriptor
fsg_ss_bulk_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	/* bEndpointAddress copied from fs_bulk_in_desc during fsg_bind() */
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(1024),
};

static struct usb_ss_ep_comp_descriptor fsg_ss_bulk_in_comp_desc = {
	.bLength =		sizeof(fsg_ss_bulk_in_comp_desc),
	.bDescriptorType =	USB_DT_SS_ENDPOINT_COMP,

	/*.bMaxBurst =		DYNAMIC, */
};

static struct usb_endpoint_descriptor
fsg_ss_bulk_out_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	/* bEndpointAddress copied from fs_bulk_out_desc during fsg_bind() */
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(1024),
};

static struct usb_ss_ep_comp_descriptor fsg_ss_bulk_out_comp_desc = {
	.bLength =		sizeof(fsg_ss_bulk_in_comp_desc),
	.bDescriptorType =	USB_DT_SS_ENDPOINT_COMP,

	/*.bMaxBurst =		DYNAMIC, */
};

#ifndef FSG_NO_INTR_EP

static struct usb_endpoint_descriptor
fsg_ss_intr_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	/* bEndpointAddress copied from fs_intr_in_desc during fsg_bind() */
	.bmAttributes =		USB_ENDPOINT_XFER_INT,
	.wMaxPacketSize =	cpu_to_le16(2),
	.bInterval =		9,	/* 2**(9-1) = 256 uframes -> 32 ms */
};

static struct usb_ss_ep_comp_descriptor fsg_ss_intr_in_comp_desc = {
	.bLength =		sizeof(fsg_ss_bulk_in_comp_desc),
	.bDescriptorType =	USB_DT_SS_ENDPOINT_COMP,

	.wBytesPerInterval =	cpu_to_le16(2),
};

#ifndef FSG_NO_OTG
#  define FSG_SS_FUNCTION_PRE_EP_ENTRIES	2
#else
#  define FSG_SS_FUNCTION_PRE_EP_ENTRIES	1
#endif

#endif

static __maybe_unused struct usb_ext_cap_descriptor fsg_ext_cap_desc = {
	.bLength =		USB_DT_USB_EXT_CAP_SIZE,
	.bDescriptorType =	USB_DT_DEVICE_CAPABILITY,
	.bDevCapabilityType =	USB_CAP_TYPE_EXT,

	.bmAttributes =		cpu_to_le32(USB_LPM_SUPPORT),
};

static __maybe_unused struct usb_ss_cap_descriptor fsg_ss_cap_desc = {
	.bLength =		USB_DT_USB_SS_CAP_SIZE,
	.bDescriptorType =	USB_DT_DEVICE_CAPABILITY,
	.bDevCapabilityType =	USB_SS_CAP_TYPE,

	/* .bmAttributes = LTM is not supported yet */

	.wSpeedSupported =	cpu_to_le16(USB_LOW_SPEED_OPERATION
		| USB_FULL_SPEED_OPERATION
		| USB_HIGH_SPEED_OPERATION
		| USB_5GBPS_OPERATION),
	.bFunctionalitySupport = USB_LOW_SPEED_OPERATION,
	.bU1devExitLat =	USB_DEFAULT_U1_DEV_EXIT_LAT,
	.bU2DevExitLat =	cpu_to_le16(USB_DEFAULT_U2_DEV_EXIT_LAT),
};

static __maybe_unused struct usb_bos_descriptor fsg_bos_desc = {
	.bLength =		USB_DT_BOS_SIZE,
	.bDescriptorType =	USB_DT_BOS,

	.wTotalLength =		cpu_to_le16(USB_DT_BOS_SIZE
				+ USB_DT_USB_EXT_CAP_SIZE
				+ USB_DT_USB_SS_CAP_SIZE),

	.bNumDeviceCaps =	2,
};

static struct usb_descriptor_header *fsg_ss_function[] = {
#ifndef FSG_NO_OTG
	(struct usb_descriptor_header *) &fsg_otg_desc,
#endif
	(struct usb_descriptor_header *) &fsg_intf_desc,
	(struct usb_descriptor_header *) &fsg_ss_bulk_in_desc,
	(struct usb_descriptor_header *) &fsg_ss_bulk_in_comp_desc,
	(struct usb_descriptor_header *) &fsg_ss_bulk_out_desc,
	(struct usb_descriptor_header *) &fsg_ss_bulk_out_comp_desc,
#ifndef FSG_NO_INTR_EP
	(struct usb_descriptor_header *) &fsg_ss_intr_in_desc,
	(struct usb_descriptor_header *) &fsg_ss_intr_in_comp_desc,
#endif
	NULL,
};

/* Maxpacket and other transfer characteristics vary by speed. */
static __maybe_unused struct usb_endpoint_descriptor *
fsg_ep_desc(struct usb_gadget *g, struct usb_endpoint_descriptor *fs,
		struct usb_endpoint_descriptor *hs,
		struct usb_endpoint_descriptor *ss)
{
	if (gadget_is_superspeed(g) && g->speed == USB_SPEED_SUPER)
		return ss;
	else if (gadget_is_dualspeed(g) && g->speed == USB_SPEED_HIGH)
		return hs;
	return fs;
}


/* Static strings, in UTF-8 (for simplicity we use only ASCII characters) */
static struct usb_string		fsg_strings[] = {
#ifndef FSG_NO_DEVICE_STRINGS
	{FSG_STRING_MANUFACTURER,	fsg_string_manufacturer},
	{FSG_STRING_PRODUCT,		fsg_string_product},
	{FSG_STRING_SERIAL,		""},
	{FSG_STRING_CONFIG,		fsg_string_config},
#endif
	{FSG_STRING_INTERFACE,		fsg_string_interface},
	{}
};

static struct usb_gadget_strings	fsg_stringtab = {
	.language	= 0x0409,		/* en-us */
	.strings	= fsg_strings,
};


 /*-------------------------------------------------------------------------*/

/*
 * If the next two routines are called while the gadget is registered,
 * the caller must own fsg->filesem for writing.
 */

static void fsg_lun_close(struct fsg_lun *curlun)
{
	if (curlun->filp) {
		LDBG(curlun, "close backing file\n");
		fput(curlun->filp);
		curlun->filp = NULL;
	}
}


static int fsg_lun_open(struct fsg_lun *curlun, const char *filename)
{
	int				ro;
	struct file			*filp = NULL;
	int				rc = -EINVAL;
	struct inode			*inode = NULL;
	loff_t				size;
	loff_t				num_sectors;
	loff_t				min_sectors;
	unsigned int			blkbits;
	unsigned int			blksize;

	/* R/W if we can, R/O if we must */
	ro = curlun->initially_ro;
	if (!ro) {
		filp = filp_open(filename, O_RDWR | O_LARGEFILE, 0);
		if (PTR_ERR(filp) == -EROFS || PTR_ERR(filp) == -EACCES)
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

	inode = filp->f_path.dentry->d_inode;
	if ((!S_ISREG(inode->i_mode) && !S_ISBLK(inode->i_mode))) {
		LINFO(curlun, "invalid file type: %s\n", filename);
		goto out;
	}

	/*
	 * If we can't read the file, it's no good.
	 * If we can't write the file, use it read-only.
	 */
	if (!(filp->f_op->read || filp->f_op->aio_read)) {
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

	if (curlun->cdrom) {
		blksize = 2048;
		blkbits = 11;
	} else if (inode->i_bdev) {
		blksize = bdev_logical_block_size(inode->i_bdev);
		blkbits = blksize_bits(blksize);
	} else {
		blksize = 512;
		blkbits = 9;
	}

	num_sectors = size >> blkbits; /* File size in logic-block-size blocks */
	min_sectors = 1;
	if (curlun->cdrom) {
		min_sectors = 300;	/* Smallest track is 300 frames */
		if (num_sectors >= 256*60*75) {
			num_sectors = 256*60*75 - 1;
			LINFO(curlun, "file too big: %s\n", filename);
			LINFO(curlun, "using only first %d blocks\n",
					(int) num_sectors);
		}
	}
	if (num_sectors < min_sectors) {
		LINFO(curlun, "file too small: %s\n", filename);
		rc = -ETOOSMALL;
		goto out;
	}

	if (fsg_lun_is_open(curlun))
		fsg_lun_close(curlun);

	curlun->blksize = blksize;
	curlun->blkbits = blkbits;
	curlun->ro = ro;
	curlun->filp = filp;
	curlun->file_length = size;
	curlun->num_sectors = num_sectors;
	LDBG(curlun, "open backing file: %s\n", filename);
	return 0;

out:
	fput(filp);
	return rc;
}


/*-------------------------------------------------------------------------*/

/*
 * Sync the file data, don't bother with the metadata.
 * This code was copied from fs/buffer.c:sys_fdatasync().
 */
static int fsg_lun_fsync_sub(struct fsg_lun *curlun)
{
	struct file	*filp = curlun->filp;

	if (curlun->ro || !filp)
		return 0;
	return vfs_fsync(filp, 1);
}

static void store_cdrom_address(u8 *dest, int msf, u32 addr)
{
	if (msf) {
		/* Convert to Minutes-Seconds-Frames */
		addr >>= 2;		/* Convert to 2048-byte frames */
		addr += 2*75;		/* Lead-in occupies 2 seconds */
		dest[3] = addr % 75;	/* Frames */
		addr /= 75;
		dest[2] = addr % 60;	/* Seconds */
		addr /= 60;
		dest[1] = addr;		/* Minutes */
		dest[0] = 0;		/* Reserved */
	} else {
		/* Absolute sector */
		put_unaligned_be32(addr, dest);
	}
}


/*-------------------------------------------------------------------------*/


static ssize_t fsg_show_ro(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct fsg_lun	*curlun = fsg_lun_from_dev(dev);

	return sprintf(buf, "%d\n", fsg_lun_is_open(curlun)
				  ? curlun->ro
				  : curlun->initially_ro);
}

static ssize_t fsg_show_nofua(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct fsg_lun	*curlun = fsg_lun_from_dev(dev);

	return sprintf(buf, "%u\n", curlun->nofua);
}

static ssize_t fsg_show_file(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct fsg_lun	*curlun = fsg_lun_from_dev(dev);
	struct rw_semaphore	*filesem = dev_get_drvdata(dev);
	char		*p;
	ssize_t		rc;

	down_read(filesem);
	if (fsg_lun_is_open(curlun)) {	/* Get the complete pathname */
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
	up_read(filesem);
	return rc;
}


static ssize_t fsg_store_ro(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	ssize_t		rc;
	struct fsg_lun	*curlun = fsg_lun_from_dev(dev);
	struct rw_semaphore	*filesem = dev_get_drvdata(dev);
	unsigned	ro;

	rc = kstrtouint(buf, 2, &ro);
	if (rc)
		return rc;

	/*
	 * Allow the write-enable status to change only while the
	 * backing file is closed.
	 */
	down_read(filesem);
	if (fsg_lun_is_open(curlun)) {
		LDBG(curlun, "read-only status change prevented\n");
		rc = -EBUSY;
	} else {
		curlun->ro = ro;
		curlun->initially_ro = ro;
		LDBG(curlun, "read-only status set to %d\n", curlun->ro);
		rc = count;
	}
	up_read(filesem);
	return rc;
}

static ssize_t fsg_store_nofua(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct fsg_lun	*curlun = fsg_lun_from_dev(dev);
	unsigned	nofua;
	int		ret;

	ret = kstrtouint(buf, 2, &nofua);
	if (ret)
		return ret;

	/* Sync data when switching from async mode to sync */
	if (!nofua && curlun->nofua)
		fsg_lun_fsync_sub(curlun);

	curlun->nofua = nofua;

	return count;
}

static ssize_t fsg_store_file(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct fsg_lun	*curlun = fsg_lun_from_dev(dev);
	struct rw_semaphore	*filesem = dev_get_drvdata(dev);
	int		rc = 0;

	if (curlun->prevent_medium_removal && fsg_lun_is_open(curlun)) {
		LDBG(curlun, "eject attempt prevented\n");
		return -EBUSY;				/* "Door is locked" */
	}

	/* Remove a trailing newline */
	if (count > 0 && buf[count-1] == '\n')
		((char *) buf)[count-1] = 0;		/* Ugh! */

	/* Load new medium */
	down_write(filesem);
	if (count > 0 && buf[0]) {
		/* fsg_lun_open() will close existing file if any. */
		rc = fsg_lun_open(curlun, buf);
		if (rc == 0)
			curlun->unit_attention_data =
					SS_NOT_READY_TO_READY_TRANSITION;
	} else if (fsg_lun_is_open(curlun)) {
		fsg_lun_close(curlun);
		curlun->unit_attention_data = SS_MEDIUM_NOT_PRESENT;
	}
	up_write(filesem);
	return (rc < 0 ? rc : count);
}
