/*
 * linux/drivers/ide/ide-floppy.c	Version 0.99	Feb 24 2002
 *
 * Copyright (C) 1996 - 1999 Gadi Oxman <gadio@netvision.net.il>
 * Copyright (C) 2000 - 2002 Paul Bristow <paul@paulbristow.net>
 */

/*
 * IDE ATAPI floppy driver.
 *
 * The driver currently doesn't have any fancy features, just the bare
 * minimum read/write support.
 *
 * This driver supports the following IDE floppy drives:
 *
 * LS-120/240 SuperDisk
 * Iomega Zip 100/250
 * Iomega PC Card Clik!/PocketZip
 *
 * Many thanks to Lode Leroy <Lode.Leroy@www.ibase.be>, who tested so many
 * ALPHA patches to this driver on an EASYSTOR LS-120 ATAPI floppy drive.
 *
 * Ver 0.1   Oct 17 96   Initial test version, mostly based on ide-tape.c.
 * Ver 0.2   Oct 31 96   Minor changes.
 * Ver 0.3   Dec  2 96   Fixed error recovery bug.
 * Ver 0.4   Jan 26 97   Add support for the HDIO_GETGEO ioctl.
 * Ver 0.5   Feb 21 97   Add partitions support.
 *                       Use the minimum of the LBA and CHS capacities.
 *                       Avoid hwgroup->rq == NULL on the last irq.
 *                       Fix potential null dereferencing with DEBUG_LOG.
 * Ver 0.8   Dec  7 97   Increase irq timeout from 10 to 50 seconds.
 *                       Add media write-protect detection.
 *                       Issue START command only if TEST UNIT READY fails.
 *                       Add work-around for IOMEGA ZIP revision 21.D.
 *                       Remove idefloppy_get_capabilities().
 * Ver 0.9   Jul  4 99   Fix a bug which might have caused the number of
 *                        bytes requested on each interrupt to be zero.
 *                        Thanks to <shanos@es.co.nz> for pointing this out.
 * Ver 0.9.sv Jan 6 01   Sam Varshavchik <mrsam@courier-mta.com>
 *                       Implement low level formatting.  Reimplemented
 *                       IDEFLOPPY_CAPABILITIES_PAGE, since we need the srfp
 *                       bit.  My LS-120 drive barfs on
 *                       IDEFLOPPY_CAPABILITIES_PAGE, but maybe it's just me.
 *                       Compromise by not reporting a failure to get this
 *                       mode page.  Implemented four IOCTLs in order to
 *                       implement formatting.  IOCTls begin with 0x4600,
 *                       0x46 is 'F' as in Format.
 *            Jan 9 01   Userland option to select format verify.
 *                       Added PC_SUPPRESS_ERROR flag - some idefloppy drives
 *                       do not implement IDEFLOPPY_CAPABILITIES_PAGE, and
 *                       return a sense error.  Suppress error reporting in
 *                       this particular case in order to avoid spurious
 *                       errors in syslog.  The culprit is
 *                       idefloppy_get_capability_page(), so move it to
 *                       idefloppy_begin_format() so that it's not used
 *                       unless absolutely necessary.
 *                       If drive does not support format progress indication
 *                       monitor the dsc bit in the status register.
 *                       Also, O_NDELAY on open will allow the device to be
 *                       opened without a disk available.  This can be used to
 *                       open an unformatted disk, or get the device capacity.
 * Ver 0.91  Dec 11 99   Added IOMEGA Clik! drive support by 
 *     		   <paul@paulbristow.net>
 * Ver 0.92  Oct 22 00   Paul Bristow became official maintainer for this 
 *           		   driver.  Included Powerbook internal zip kludge.
 * Ver 0.93  Oct 24 00   Fixed bugs for Clik! drive
 *                        no disk on insert and disk change now works
 * Ver 0.94  Oct 27 00   Tidied up to remove strstr(Clik) everywhere
 * Ver 0.95  Nov  7 00   Brought across to kernel 2.4
 * Ver 0.96  Jan  7 01   Actually in line with release version of 2.4.0
 *                       including set_bit patch from Rusty Russell
 * Ver 0.97  Jul 22 01   Merge 0.91-0.96 onto 0.9.sv for ac series
 * Ver 0.97.sv Aug 3 01  Backported from 2.4.7-ac3
 * Ver 0.98  Oct 26 01   Split idefloppy_transfer_pc into two pieces to
 *                        fix a lost interrupt problem. It appears the busy
 *                        bit was being deasserted by my IOMEGA ATAPI ZIP 100
 *                        drive before the drive was actually ready.
 * Ver 0.98a Oct 29 01   Expose delay value so we can play.
 * Ver 0.99  Feb 24 02   Remove duplicate code, modify clik! detection code 
 *                        to support new PocketZip drives 
 */

#define IDEFLOPPY_VERSION "0.99.newide"

#include <linux/module.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/major.h>
#include <linux/errno.h>
#include <linux/genhd.h>
#include <linux/slab.h>
#include <linux/cdrom.h>
#include <linux/ide.h>
#include <linux/bitops.h>
#include <linux/mutex.h>

#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/unaligned.h>

/*
 *	The following are used to debug the driver.
 */
#define IDEFLOPPY_DEBUG_LOG		0
#define IDEFLOPPY_DEBUG_INFO		0
#define IDEFLOPPY_DEBUG_BUGS		1

/* #define IDEFLOPPY_DEBUG(fmt, args...) printk(KERN_INFO fmt, ## args) */
#define IDEFLOPPY_DEBUG( fmt, args... )

#if IDEFLOPPY_DEBUG_LOG
#define debug_log printk
#else
#define debug_log(fmt, args... ) do {} while(0)
#endif


/*
 *	Some drives require a longer irq timeout.
 */
#define IDEFLOPPY_WAIT_CMD		(5 * WAIT_CMD)

/*
 *	After each failed packet command we issue a request sense command
 *	and retry the packet command IDEFLOPPY_MAX_PC_RETRIES times.
 */
#define IDEFLOPPY_MAX_PC_RETRIES	3

/*
 *	With each packet command, we allocate a buffer of
 *	IDEFLOPPY_PC_BUFFER_SIZE bytes.
 */
#define IDEFLOPPY_PC_BUFFER_SIZE	256

/*
 *	In various places in the driver, we need to allocate storage
 *	for packet commands and requests, which will remain valid while
 *	we leave the driver to wait for an interrupt or a timeout event.
 */
#define IDEFLOPPY_PC_STACK		(10 + IDEFLOPPY_MAX_PC_RETRIES)

/*
 *	Our view of a packet command.
 */
typedef struct idefloppy_packet_command_s {
	u8 c[12];				/* Actual packet bytes */
	int retries;				/* On each retry, we increment retries */
	int error;				/* Error code */
	int request_transfer;			/* Bytes to transfer */
	int actually_transferred;		/* Bytes actually transferred */
	int buffer_size;			/* Size of our data buffer */
	int b_count;				/* Missing/Available data on the current buffer */
	struct request *rq;			/* The corresponding request */
	u8 *buffer;				/* Data buffer */
	u8 *current_position;			/* Pointer into the above buffer */
	void (*callback) (ide_drive_t *);	/* Called when this packet command is completed */
	u8 pc_buffer[IDEFLOPPY_PC_BUFFER_SIZE];	/* Temporary buffer */
	unsigned long flags;			/* Status/Action bit flags: long for set_bit */
} idefloppy_pc_t;

/*
 *	Packet command flag bits.
 */
#define	PC_ABORT			0	/* Set when an error is considered normal - We won't retry */
#define PC_DMA_RECOMMENDED		2	/* 1 when we prefer to use DMA if possible */
#define	PC_DMA_IN_PROGRESS		3	/* 1 while DMA in progress */
#define	PC_DMA_ERROR			4	/* 1 when encountered problem during DMA */
#define	PC_WRITING			5	/* Data direction */

#define	PC_SUPPRESS_ERROR		6	/* Suppress error reporting */

/*
 *	Removable Block Access Capabilities Page
 */
typedef struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
	unsigned	page_code	:6;	/* Page code - Should be 0x1b */
	unsigned	reserved1_6	:1;	/* Reserved */
	unsigned	ps		:1;	/* Should be 0 */
#elif defined(__BIG_ENDIAN_BITFIELD)
	unsigned	ps		:1;	/* Should be 0 */
	unsigned	reserved1_6	:1;	/* Reserved */
	unsigned	page_code	:6;	/* Page code - Should be 0x1b */
#else
#error "Bitfield endianness not defined! Check your byteorder.h"
#endif
	u8		page_length;		/* Page Length - Should be 0xa */
#if defined(__LITTLE_ENDIAN_BITFIELD)
	unsigned	reserved2	:6;
	unsigned	srfp		:1;	/* Supports reporting progress of format */
	unsigned	sflp		:1;	/* System floppy type device */
	unsigned	tlun		:3;	/* Total logical units supported by the device */
	unsigned	reserved3	:3;
	unsigned	sml		:1;	/* Single / Multiple lun supported */
	unsigned	ncd		:1;	/* Non cd optical device */
#elif defined(__BIG_ENDIAN_BITFIELD)
	unsigned	sflp		:1;	/* System floppy type device */
	unsigned	srfp		:1;	/* Supports reporting progress of format */
	unsigned	reserved2	:6;
	unsigned	ncd		:1;	/* Non cd optical device */
	unsigned	sml		:1;	/* Single / Multiple lun supported */
	unsigned	reserved3	:3;
	unsigned	tlun		:3;	/* Total logical units supported by the device */
#else
#error "Bitfield endianness not defined! Check your byteorder.h"
#endif
	u8		reserved[8];
} idefloppy_capabilities_page_t;

/*
 *	Flexible disk page.
 */
typedef struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
	unsigned	page_code	:6;	/* Page code - Should be 0x5 */
	unsigned	reserved1_6	:1;	/* Reserved */
	unsigned	ps		:1;	/* The device is capable of saving the page */
#elif defined(__BIG_ENDIAN_BITFIELD)
	unsigned	ps		:1;	/* The device is capable of saving the page */
	unsigned	reserved1_6	:1;	/* Reserved */
	unsigned	page_code	:6;	/* Page code - Should be 0x5 */
#else
#error "Bitfield endianness not defined! Check your byteorder.h"
#endif
	u8		page_length;		/* Page Length - Should be 0x1e */
	u16		transfer_rate;		/* In kilobits per second */
	u8		heads, sectors;		/* Number of heads, Number of sectors per track */
	u16		sector_size;		/* Byes per sector */
	u16		cyls;			/* Number of cylinders */
	u8		reserved10[10];
	u8		motor_delay;		/* Motor off delay */
	u8		reserved21[7];
	u16		rpm;			/* Rotations per minute */
	u8		reserved30[2];
} idefloppy_flexible_disk_page_t;
 
/*
 *	Format capacity
 */
typedef struct {
	u8		reserved[3];
	u8		length;			/* Length of the following descriptors in bytes */
} idefloppy_capacity_header_t;

typedef struct {
	u32		blocks;			/* Number of blocks */
#if defined(__LITTLE_ENDIAN_BITFIELD)
	unsigned	dc		:2;	/* Descriptor Code */
	unsigned	reserved	:6;
#elif defined(__BIG_ENDIAN_BITFIELD)
	unsigned	reserved	:6;
	unsigned	dc		:2;	/* Descriptor Code */
#else
#error "Bitfield endianness not defined! Check your byteorder.h"
#endif
	u8		length_msb;		/* Block Length (MSB)*/
	u16		length;			/* Block Length */
} idefloppy_capacity_descriptor_t;

#define CAPACITY_INVALID	0x00
#define CAPACITY_UNFORMATTED	0x01
#define CAPACITY_CURRENT	0x02
#define CAPACITY_NO_CARTRIDGE	0x03

/*
 *	Most of our global data which we need to save even as we leave the
 *	driver due to an interrupt or a timer event is stored in a variable
 *	of type idefloppy_floppy_t, defined below.
 */
typedef struct ide_floppy_obj {
	ide_drive_t	*drive;
	ide_driver_t	*driver;
	struct gendisk	*disk;
	struct kref	kref;

	/* Current packet command */
	idefloppy_pc_t *pc;
	/* Last failed packet command */
	idefloppy_pc_t *failed_pc;
	/* Packet command stack */
	idefloppy_pc_t pc_stack[IDEFLOPPY_PC_STACK];
	/* Next free packet command storage space */
	int pc_stack_index;
	struct request rq_stack[IDEFLOPPY_PC_STACK];
	/* We implement a circular array */
	int rq_stack_index;

	/*
	 *	Last error information
	 */
	u8 sense_key, asc, ascq;
	/* delay this long before sending packet command */
	u8 ticks;
	int progress_indication;

	/*
	 *	Device information
	 */
	/* Current format */
	int blocks, block_size, bs_factor;
	/* Last format capacity */
	idefloppy_capacity_descriptor_t capacity;
	/* Copy of the flexible disk page */
	idefloppy_flexible_disk_page_t flexible_disk_page;
	/* Write protect */
	int wp;
	/* Supports format progress report */
	int srfp;
	/* Status/Action flags */
	unsigned long flags;
} idefloppy_floppy_t;

#define IDEFLOPPY_TICKS_DELAY	HZ/20	/* default delay for ZIP 100 (50ms) */

/*
 *	Floppy flag bits values.
 */
#define IDEFLOPPY_DRQ_INTERRUPT		0	/* DRQ interrupt device */
#define IDEFLOPPY_MEDIA_CHANGED		1	/* Media may have changed */
#define IDEFLOPPY_USE_READ12		2	/* Use READ12/WRITE12 or READ10/WRITE10 */
#define	IDEFLOPPY_FORMAT_IN_PROGRESS	3	/* Format in progress */
#define IDEFLOPPY_CLIK_DRIVE	        4       /* Avoid commands not supported in Clik drive */
#define IDEFLOPPY_ZIP_DRIVE		5	/* Requires BH algorithm for packets */

/*
 *	ATAPI floppy drive packet commands
 */
#define IDEFLOPPY_FORMAT_UNIT_CMD	0x04
#define IDEFLOPPY_INQUIRY_CMD		0x12
#define IDEFLOPPY_MODE_SELECT_CMD	0x55
#define IDEFLOPPY_MODE_SENSE_CMD	0x5a
#define IDEFLOPPY_READ10_CMD		0x28
#define IDEFLOPPY_READ12_CMD		0xa8
#define IDEFLOPPY_READ_CAPACITY_CMD	0x23
#define IDEFLOPPY_REQUEST_SENSE_CMD	0x03
#define IDEFLOPPY_PREVENT_REMOVAL_CMD	0x1e
#define IDEFLOPPY_SEEK_CMD		0x2b
#define IDEFLOPPY_START_STOP_CMD	0x1b
#define IDEFLOPPY_TEST_UNIT_READY_CMD	0x00
#define IDEFLOPPY_VERIFY_CMD		0x2f
#define IDEFLOPPY_WRITE10_CMD		0x2a
#define IDEFLOPPY_WRITE12_CMD		0xaa
#define IDEFLOPPY_WRITE_VERIFY_CMD	0x2e

/*
 *	Defines for the mode sense command
 */
#define MODE_SENSE_CURRENT		0x00
#define MODE_SENSE_CHANGEABLE		0x01
#define MODE_SENSE_DEFAULT		0x02 
#define MODE_SENSE_SAVED		0x03

/*
 *	IOCTLs used in low-level formatting.
 */

#define	IDEFLOPPY_IOCTL_FORMAT_SUPPORTED	0x4600
#define	IDEFLOPPY_IOCTL_FORMAT_GET_CAPACITY	0x4601
#define	IDEFLOPPY_IOCTL_FORMAT_START		0x4602
#define IDEFLOPPY_IOCTL_FORMAT_GET_PROGRESS	0x4603

#if 0
/*
 *	Special requests for our block device strategy routine.
 */
#define	IDEFLOPPY_FIRST_RQ	90

/*
 * 	IDEFLOPPY_PC_RQ is used to queue a packet command in the request queue.
 */
#define	IDEFLOPPY_PC_RQ		90

#define IDEFLOPPY_LAST_RQ	90

/*
 *	A macro which can be used to check if a given request command
 *	originated in the driver or in the buffer cache layer.
 */
#define IDEFLOPPY_RQ_CMD(cmd) 	((cmd >= IDEFLOPPY_FIRST_RQ) && (cmd <= IDEFLOPPY_LAST_RQ))

#endif

/*
 *	Error codes which are returned in rq->errors to the higher part
 *	of the driver.
 */
#define	IDEFLOPPY_ERROR_GENERAL		101

/*
 *	The following is used to format the general configuration word of
 *	the ATAPI IDENTIFY DEVICE command.
 */
struct idefloppy_id_gcw {	
#if defined(__LITTLE_ENDIAN_BITFIELD)
	unsigned packet_size		:2;	/* Packet Size */
	unsigned reserved234		:3;	/* Reserved */
	unsigned drq_type		:2;	/* Command packet DRQ type */
	unsigned removable		:1;	/* Removable media */
	unsigned device_type		:5;	/* Device type */
	unsigned reserved13		:1;	/* Reserved */
	unsigned protocol		:2;	/* Protocol type */
#elif defined(__BIG_ENDIAN_BITFIELD)
	unsigned protocol		:2;	/* Protocol type */
	unsigned reserved13		:1;	/* Reserved */
	unsigned device_type		:5;	/* Device type */
	unsigned removable		:1;	/* Removable media */
	unsigned drq_type		:2;	/* Command packet DRQ type */
	unsigned reserved234		:3;	/* Reserved */
	unsigned packet_size		:2;	/* Packet Size */
#else
#error "Bitfield endianness not defined! Check your byteorder.h"
#endif
};

/*
 *	INQUIRY packet command - Data Format
 */
typedef struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
	unsigned	device_type	:5;	/* Peripheral Device Type */
	unsigned	reserved0_765	:3;	/* Peripheral Qualifier - Reserved */
	unsigned	reserved1_6t0	:7;	/* Reserved */
	unsigned	rmb		:1;	/* Removable Medium Bit */
	unsigned	ansi_version	:3;	/* ANSI Version */
	unsigned	ecma_version	:3;	/* ECMA Version */
	unsigned	iso_version	:2;	/* ISO Version */
	unsigned	response_format :4;	/* Response Data Format */
	unsigned	reserved3_45	:2;	/* Reserved */
	unsigned	reserved3_6	:1;	/* TrmIOP - Reserved */
	unsigned	reserved3_7	:1;	/* AENC - Reserved */
#elif defined(__BIG_ENDIAN_BITFIELD)
	unsigned	reserved0_765	:3;	/* Peripheral Qualifier - Reserved */
	unsigned	device_type	:5;	/* Peripheral Device Type */
	unsigned	rmb		:1;	/* Removable Medium Bit */
	unsigned	reserved1_6t0	:7;	/* Reserved */
	unsigned	iso_version	:2;	/* ISO Version */
	unsigned	ecma_version	:3;	/* ECMA Version */
	unsigned	ansi_version	:3;	/* ANSI Version */
	unsigned	reserved3_7	:1;	/* AENC - Reserved */
	unsigned	reserved3_6	:1;	/* TrmIOP - Reserved */
	unsigned	reserved3_45	:2;	/* Reserved */
	unsigned	response_format :4;	/* Response Data Format */
#else
#error "Bitfield endianness not defined! Check your byteorder.h"
#endif
	u8		additional_length;	/* Additional Length (total_length-4) */
	u8		rsv5, rsv6, rsv7;	/* Reserved */
	u8		vendor_id[8];		/* Vendor Identification */
	u8		product_id[16];		/* Product Identification */
	u8		revision_level[4];	/* Revision Level */
	u8		vendor_specific[20];	/* Vendor Specific - Optional */
	u8		reserved56t95[40];	/* Reserved - Optional */
						/* Additional information may be returned */
} idefloppy_inquiry_result_t;

/*
 *	REQUEST SENSE packet command result - Data Format.
 */
typedef struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
	unsigned	error_code	:7;	/* Current error (0x70) */
	unsigned	valid		:1;	/* The information field conforms to SFF-8070i */
	u8		reserved1	:8;	/* Reserved */
	unsigned	sense_key	:4;	/* Sense Key */
	unsigned	reserved2_4	:1;	/* Reserved */
	unsigned	ili		:1;	/* Incorrect Length Indicator */
	unsigned	reserved2_67	:2;
#elif defined(__BIG_ENDIAN_BITFIELD)
	unsigned	valid		:1;	/* The information field conforms to SFF-8070i */
	unsigned	error_code	:7;	/* Current error (0x70) */
	u8		reserved1	:8;	/* Reserved */
	unsigned	reserved2_67	:2;
	unsigned	ili		:1;	/* Incorrect Length Indicator */
	unsigned	reserved2_4	:1;	/* Reserved */
	unsigned	sense_key	:4;	/* Sense Key */
#else
#error "Bitfield endianness not defined! Check your byteorder.h"
#endif
	u32		information __attribute__ ((packed));
	u8		asl;			/* Additional sense length (n-7) */
	u32		command_specific;	/* Additional command specific information */
	u8		asc;			/* Additional Sense Code */
	u8		ascq;			/* Additional Sense Code Qualifier */
	u8		replaceable_unit_code;	/* Field Replaceable Unit Code */
	u8		sksv[3];
	u8		pad[2];			/* Padding to 20 bytes */
} idefloppy_request_sense_result_t;

/*
 *	Pages of the SELECT SENSE / MODE SENSE packet commands.
 */
#define	IDEFLOPPY_CAPABILITIES_PAGE	0x1b
#define IDEFLOPPY_FLEXIBLE_DISK_PAGE	0x05

/*
 *	Mode Parameter Header for the MODE SENSE packet command
 */
typedef struct {
	u16		mode_data_length;	/* Length of the following data transfer */
	u8		medium_type;		/* Medium Type */
#if defined(__LITTLE_ENDIAN_BITFIELD)
	unsigned	reserved3	:7;
	unsigned	wp		:1;	/* Write protect */
#elif defined(__BIG_ENDIAN_BITFIELD)
	unsigned	wp		:1;	/* Write protect */
	unsigned	reserved3	:7;
#else
#error "Bitfield endianness not defined! Check your byteorder.h"
#endif
	u8		reserved[4];
} idefloppy_mode_parameter_header_t;

static DEFINE_MUTEX(idefloppy_ref_mutex);

#define to_ide_floppy(obj) container_of(obj, struct ide_floppy_obj, kref)

#define ide_floppy_g(disk) \
	container_of((disk)->private_data, struct ide_floppy_obj, driver)

static struct ide_floppy_obj *ide_floppy_get(struct gendisk *disk)
{
	struct ide_floppy_obj *floppy = NULL;

	mutex_lock(&idefloppy_ref_mutex);
	floppy = ide_floppy_g(disk);
	if (floppy)
		kref_get(&floppy->kref);
	mutex_unlock(&idefloppy_ref_mutex);
	return floppy;
}

static void ide_floppy_release(struct kref *);

static void ide_floppy_put(struct ide_floppy_obj *floppy)
{
	mutex_lock(&idefloppy_ref_mutex);
	kref_put(&floppy->kref, ide_floppy_release);
	mutex_unlock(&idefloppy_ref_mutex);
}

/*
 *	Too bad. The drive wants to send us data which we are not ready to accept.
 *	Just throw it away.
 */
static void idefloppy_discard_data (ide_drive_t *drive, unsigned int bcount)
{
	while (bcount--)
		(void) HWIF(drive)->INB(IDE_DATA_REG);
}

#if IDEFLOPPY_DEBUG_BUGS
static void idefloppy_write_zeros (ide_drive_t *drive, unsigned int bcount)
{
	while (bcount--)
		HWIF(drive)->OUTB(0, IDE_DATA_REG);
}
#endif /* IDEFLOPPY_DEBUG_BUGS */


/*
 *	idefloppy_do_end_request is used to finish servicing a request.
 *
 *	For read/write requests, we will call ide_end_request to pass to the
 *	next buffer.
 */
static int idefloppy_do_end_request(ide_drive_t *drive, int uptodate, int nsecs)
{
	idefloppy_floppy_t *floppy = drive->driver_data;
	struct request *rq = HWGROUP(drive)->rq;
	int error;

	debug_log(KERN_INFO "Reached idefloppy_end_request\n");

	switch (uptodate) {
		case 0: error = IDEFLOPPY_ERROR_GENERAL; break;
		case 1: error = 0; break;
		default: error = uptodate;
	}
	if (error)
		floppy->failed_pc = NULL;
	/* Why does this happen? */
	if (!rq)
		return 0;
	if (!blk_special_request(rq)) {
		/* our real local end request function */
		ide_end_request(drive, uptodate, nsecs);
		return 0;
	}
	rq->errors = error;
	/* fixme: need to move this local also */
	ide_end_drive_cmd(drive, 0, 0);
	return 0;
}

static void idefloppy_input_buffers (ide_drive_t *drive, idefloppy_pc_t *pc, unsigned int bcount)
{
	struct request *rq = pc->rq;
	struct bio_vec *bvec;
	struct bio *bio;
	unsigned long flags;
	char *data;
	int count, i, done = 0;

	rq_for_each_bio(bio, rq) {
		bio_for_each_segment(bvec, bio, i) {
			if (!bcount)
				break;

			count = min(bvec->bv_len, bcount);

			data = bvec_kmap_irq(bvec, &flags);
			drive->hwif->atapi_input_bytes(drive, data, count);
			bvec_kunmap_irq(data, &flags);

			bcount -= count;
			pc->b_count += count;
			done += count;
		}
	}

	idefloppy_do_end_request(drive, 1, done >> 9);

	if (bcount) {
		printk(KERN_ERR "%s: leftover data in idefloppy_input_buffers, bcount == %d\n", drive->name, bcount);
		idefloppy_discard_data(drive, bcount);
	}
}

static void idefloppy_output_buffers (ide_drive_t *drive, idefloppy_pc_t *pc, unsigned int bcount)
{
	struct request *rq = pc->rq;
	struct bio *bio;
	struct bio_vec *bvec;
	unsigned long flags;
	int count, i, done = 0;
	char *data;

	rq_for_each_bio(bio, rq) {
		bio_for_each_segment(bvec, bio, i) {
			if (!bcount)
				break;

			count = min(bvec->bv_len, bcount);

			data = bvec_kmap_irq(bvec, &flags);
			drive->hwif->atapi_output_bytes(drive, data, count);
			bvec_kunmap_irq(data, &flags);

			bcount -= count;
			pc->b_count += count;
			done += count;
		}
	}

	idefloppy_do_end_request(drive, 1, done >> 9);

#if IDEFLOPPY_DEBUG_BUGS
	if (bcount) {
		printk(KERN_ERR "%s: leftover data in idefloppy_output_buffers, bcount == %d\n", drive->name, bcount);
		idefloppy_write_zeros(drive, bcount);
	}
#endif
}

static void idefloppy_update_buffers (ide_drive_t *drive, idefloppy_pc_t *pc)
{
	struct request *rq = pc->rq;
	struct bio *bio = rq->bio;

	while ((bio = rq->bio) != NULL)
		idefloppy_do_end_request(drive, 1, 0);
}

/*
 *	idefloppy_queue_pc_head generates a new packet command request in front
 *	of the request queue, before the current request, so that it will be
 *	processed immediately, on the next pass through the driver.
 */
static void idefloppy_queue_pc_head (ide_drive_t *drive,idefloppy_pc_t *pc,struct request *rq)
{
	struct ide_floppy_obj *floppy = drive->driver_data;

	ide_init_drive_cmd(rq);
	rq->buffer = (char *) pc;
	rq->cmd_type = REQ_TYPE_SPECIAL;
	rq->rq_disk = floppy->disk;
	(void) ide_do_drive_cmd(drive, rq, ide_preempt);
}

static idefloppy_pc_t *idefloppy_next_pc_storage (ide_drive_t *drive)
{
	idefloppy_floppy_t *floppy = drive->driver_data;

	if (floppy->pc_stack_index == IDEFLOPPY_PC_STACK)
		floppy->pc_stack_index=0;
	return (&floppy->pc_stack[floppy->pc_stack_index++]);
}

static struct request *idefloppy_next_rq_storage (ide_drive_t *drive)
{
	idefloppy_floppy_t *floppy = drive->driver_data;

	if (floppy->rq_stack_index == IDEFLOPPY_PC_STACK)
		floppy->rq_stack_index = 0;
	return (&floppy->rq_stack[floppy->rq_stack_index++]);
}

/*
 *	idefloppy_analyze_error is called on each failed packet command retry
 *	to analyze the request sense.
 */
static void idefloppy_analyze_error (ide_drive_t *drive,idefloppy_request_sense_result_t *result)
{
	idefloppy_floppy_t *floppy = drive->driver_data;

	floppy->sense_key = result->sense_key;
	floppy->asc = result->asc;
	floppy->ascq = result->ascq;
	floppy->progress_indication = result->sksv[0] & 0x80 ?
		(u16)get_unaligned((u16 *)(result->sksv+1)):0x10000;
	if (floppy->failed_pc)
		debug_log(KERN_INFO "ide-floppy: pc = %x, sense key = %x, "
			"asc = %x, ascq = %x\n", floppy->failed_pc->c[0],
			result->sense_key, result->asc, result->ascq);
	else
		debug_log(KERN_INFO "ide-floppy: sense key = %x, asc = %x, "
			"ascq = %x\n", result->sense_key,
			result->asc, result->ascq);
}

static void idefloppy_request_sense_callback (ide_drive_t *drive)
{
	idefloppy_floppy_t *floppy = drive->driver_data;

	debug_log(KERN_INFO "ide-floppy: Reached %s\n", __FUNCTION__);
	
	if (!floppy->pc->error) {
		idefloppy_analyze_error(drive,(idefloppy_request_sense_result_t *) floppy->pc->buffer);
		idefloppy_do_end_request(drive, 1, 0);
	} else {
		printk(KERN_ERR "Error in REQUEST SENSE itself - Aborting request!\n");
		idefloppy_do_end_request(drive, 0, 0);
	}
}

/*
 *	General packet command callback function.
 */
static void idefloppy_pc_callback (ide_drive_t *drive)
{
	idefloppy_floppy_t *floppy = drive->driver_data;
	
	debug_log(KERN_INFO "ide-floppy: Reached %s\n", __FUNCTION__);

	idefloppy_do_end_request(drive, floppy->pc->error ? 0 : 1, 0);
}

/*
 *	idefloppy_init_pc initializes a packet command.
 */
static void idefloppy_init_pc (idefloppy_pc_t *pc)
{
	memset(pc->c, 0, 12);
	pc->retries = 0;
	pc->flags = 0;
	pc->request_transfer = 0;
	pc->buffer = pc->pc_buffer;
	pc->buffer_size = IDEFLOPPY_PC_BUFFER_SIZE;
	pc->callback = &idefloppy_pc_callback;
}

static void idefloppy_create_request_sense_cmd (idefloppy_pc_t *pc)
{
	idefloppy_init_pc(pc);	
	pc->c[0] = IDEFLOPPY_REQUEST_SENSE_CMD;
	pc->c[4] = 255;
	pc->request_transfer = 18;
	pc->callback = &idefloppy_request_sense_callback;
}

/*
 *	idefloppy_retry_pc is called when an error was detected during the
 *	last packet command. We queue a request sense packet command in
 *	the head of the request list.
 */
static void idefloppy_retry_pc (ide_drive_t *drive)
{
	idefloppy_pc_t *pc;
	struct request *rq;
	atapi_error_t error;

	error.all = HWIF(drive)->INB(IDE_ERROR_REG);
	pc = idefloppy_next_pc_storage(drive);
	rq = idefloppy_next_rq_storage(drive);
	idefloppy_create_request_sense_cmd(pc);
	idefloppy_queue_pc_head(drive, pc, rq);
}

/*
 *	idefloppy_pc_intr is the usual interrupt handler which will be called
 *	during a packet command.
 */
static ide_startstop_t idefloppy_pc_intr (ide_drive_t *drive)
{
	idefloppy_floppy_t *floppy = drive->driver_data;
	atapi_status_t status;
	atapi_bcount_t bcount;
	atapi_ireason_t ireason;
	idefloppy_pc_t *pc = floppy->pc;
	struct request *rq = pc->rq;
	unsigned int temp;

	debug_log(KERN_INFO "ide-floppy: Reached %s interrupt handler\n",
		__FUNCTION__);

	if (test_bit(PC_DMA_IN_PROGRESS, &pc->flags)) {
		if (HWIF(drive)->ide_dma_end(drive)) {
			set_bit(PC_DMA_ERROR, &pc->flags);
		} else {
			pc->actually_transferred = pc->request_transfer;
			idefloppy_update_buffers(drive, pc);
		}
		debug_log(KERN_INFO "ide-floppy: DMA finished\n");
	}

	/* Clear the interrupt */
	status.all = HWIF(drive)->INB(IDE_STATUS_REG);

	if (!status.b.drq) {			/* No more interrupts */
		debug_log(KERN_INFO "Packet command completed, %d bytes "
			"transferred\n", pc->actually_transferred);
		clear_bit(PC_DMA_IN_PROGRESS, &pc->flags);

		local_irq_enable_in_hardirq();

		if (status.b.check || test_bit(PC_DMA_ERROR, &pc->flags)) {
			/* Error detected */
			debug_log(KERN_INFO "ide-floppy: %s: I/O error\n",
				drive->name);
			rq->errors++;
			if (pc->c[0] == IDEFLOPPY_REQUEST_SENSE_CMD) {
				printk(KERN_ERR "ide-floppy: I/O error in "
					"request sense command\n");
				return ide_do_reset(drive);
			}
			/* Retry operation */
			idefloppy_retry_pc(drive);
			/* queued, but not started */
			return ide_stopped;
		}
		pc->error = 0;
		if (floppy->failed_pc == pc)
			floppy->failed_pc = NULL;
		/* Command finished - Call the callback function */
		pc->callback(drive);
		return ide_stopped;
	}

	if (test_and_clear_bit(PC_DMA_IN_PROGRESS, &pc->flags)) {
		printk(KERN_ERR "ide-floppy: The floppy wants to issue "
			"more interrupts in DMA mode\n");
		(void)__ide_dma_off(drive);
		return ide_do_reset(drive);
	}

	/* Get the number of bytes to transfer */
	bcount.b.high = HWIF(drive)->INB(IDE_BCOUNTH_REG);
	bcount.b.low = HWIF(drive)->INB(IDE_BCOUNTL_REG);
	/* on this interrupt */
	ireason.all = HWIF(drive)->INB(IDE_IREASON_REG);

	if (ireason.b.cod) {
		printk(KERN_ERR "ide-floppy: CoD != 0 in idefloppy_pc_intr\n");
		return ide_do_reset(drive);
	}
	if (ireason.b.io == test_bit(PC_WRITING, &pc->flags)) {
		/* Hopefully, we will never get here */
		printk(KERN_ERR "ide-floppy: We wanted to %s, ",
			ireason.b.io ? "Write":"Read");
		printk(KERN_ERR "but the floppy wants us to %s !\n",
			ireason.b.io ? "Read":"Write");
		return ide_do_reset(drive);
	}
	if (!test_bit(PC_WRITING, &pc->flags)) {
		/* Reading - Check that we have enough space */
		temp = pc->actually_transferred + bcount.all;
		if (temp > pc->request_transfer) {
			if (temp > pc->buffer_size) {
				printk(KERN_ERR "ide-floppy: The floppy wants "
					"to send us more data than expected "
					"- discarding data\n");
				idefloppy_discard_data(drive,bcount.all);
				BUG_ON(HWGROUP(drive)->handler != NULL);
				ide_set_handler(drive,
						&idefloppy_pc_intr,
						IDEFLOPPY_WAIT_CMD,
						NULL);
				return ide_started;
			}
			debug_log(KERN_NOTICE "ide-floppy: The floppy wants to "
				"send us more data than expected - "
				"allowing transfer\n");
		}
	}
	if (test_bit(PC_WRITING, &pc->flags)) {
		if (pc->buffer != NULL)
			/* Write the current buffer */
			HWIF(drive)->atapi_output_bytes(drive,
						pc->current_position,
						bcount.all);
		else
			idefloppy_output_buffers(drive, pc, bcount.all);
	} else {
		if (pc->buffer != NULL)
			/* Read the current buffer */
			HWIF(drive)->atapi_input_bytes(drive,
						pc->current_position,
						bcount.all);
		else
			idefloppy_input_buffers(drive, pc, bcount.all);
	}
	/* Update the current position */
	pc->actually_transferred += bcount.all;
	pc->current_position += bcount.all;

	BUG_ON(HWGROUP(drive)->handler != NULL);
	ide_set_handler(drive, &idefloppy_pc_intr, IDEFLOPPY_WAIT_CMD, NULL);		/* And set the interrupt handler again */
	return ide_started;
}

/*
 * This is the original routine that did the packet transfer.
 * It fails at high speeds on the Iomega ZIP drive, so there's a slower version
 * for that drive below. The algorithm is chosen based on drive type
 */
static ide_startstop_t idefloppy_transfer_pc (ide_drive_t *drive)
{
	ide_startstop_t startstop;
	idefloppy_floppy_t *floppy = drive->driver_data;
	atapi_ireason_t ireason;

	if (ide_wait_stat(&startstop, drive, DRQ_STAT, BUSY_STAT, WAIT_READY)) {
		printk(KERN_ERR "ide-floppy: Strange, packet command "
				"initiated yet DRQ isn't asserted\n");
		return startstop;
	}
	ireason.all = HWIF(drive)->INB(IDE_IREASON_REG);
	if (!ireason.b.cod || ireason.b.io) {
		printk(KERN_ERR "ide-floppy: (IO,CoD) != (0,1) while "
				"issuing a packet command\n");
		return ide_do_reset(drive);
	}
	BUG_ON(HWGROUP(drive)->handler != NULL);
	/* Set the interrupt routine */
	ide_set_handler(drive, &idefloppy_pc_intr, IDEFLOPPY_WAIT_CMD, NULL);
	/* Send the actual packet */
	HWIF(drive)->atapi_output_bytes(drive, floppy->pc->c, 12);
	return ide_started;
}


/*
 * What we have here is a classic case of a top half / bottom half
 * interrupt service routine. In interrupt mode, the device sends
 * an interrupt to signal it's ready to receive a packet. However,
 * we need to delay about 2-3 ticks before issuing the packet or we
 * gets in trouble.
 *
 * So, follow carefully. transfer_pc1 is called as an interrupt (or
 * directly). In either case, when the device says it's ready for a 
 * packet, we schedule the packet transfer to occur about 2-3 ticks
 * later in transfer_pc2.
 */
static int idefloppy_transfer_pc2 (ide_drive_t *drive)
{
	idefloppy_floppy_t *floppy = drive->driver_data;

	/* Send the actual packet */
	HWIF(drive)->atapi_output_bytes(drive, floppy->pc->c, 12);
	/* Timeout for the packet command */
	return IDEFLOPPY_WAIT_CMD;
}

static ide_startstop_t idefloppy_transfer_pc1 (ide_drive_t *drive)
{
	idefloppy_floppy_t *floppy = drive->driver_data;
	ide_startstop_t startstop;
	atapi_ireason_t ireason;

	if (ide_wait_stat(&startstop, drive, DRQ_STAT, BUSY_STAT, WAIT_READY)) {
		printk(KERN_ERR "ide-floppy: Strange, packet command "
				"initiated yet DRQ isn't asserted\n");
		return startstop;
	}
	ireason.all = HWIF(drive)->INB(IDE_IREASON_REG);
	if (!ireason.b.cod || ireason.b.io) {
		printk(KERN_ERR "ide-floppy: (IO,CoD) != (0,1) "
				"while issuing a packet command\n");
		return ide_do_reset(drive);
	}
	/* 
	 * The following delay solves a problem with ATAPI Zip 100 drives
	 * where the Busy flag was apparently being deasserted before the
	 * unit was ready to receive data. This was happening on a
	 * 1200 MHz Athlon system. 10/26/01 25msec is too short,
	 * 40 and 50msec work well. idefloppy_pc_intr will not be actually
	 * used until after the packet is moved in about 50 msec.
	 */
	BUG_ON(HWGROUP(drive)->handler != NULL);
	ide_set_handler(drive, 
	  &idefloppy_pc_intr, 		/* service routine for packet command */
	  floppy->ticks,		/* wait this long before "failing" */
	  &idefloppy_transfer_pc2);	/* fail == transfer_pc2 */
	return ide_started;
}

/**
 * idefloppy_should_report_error()
 *
 * Supresses error messages resulting from Medium not present
 */
static inline int idefloppy_should_report_error(idefloppy_floppy_t *floppy)
{
	if (floppy->sense_key == 0x02 &&
	    floppy->asc       == 0x3a &&
	    floppy->ascq      == 0x00)
		return 0;
	return 1;
}

/*
 *	Issue a packet command
 */
static ide_startstop_t idefloppy_issue_pc (ide_drive_t *drive, idefloppy_pc_t *pc)
{
	idefloppy_floppy_t *floppy = drive->driver_data;
	ide_hwif_t *hwif = drive->hwif;
	atapi_feature_t feature;
	atapi_bcount_t bcount;
	ide_handler_t *pkt_xfer_routine;

#if 0 /* Accessing floppy->pc is not valid here, the previous pc may be gone
         and have lived on another thread's stack; that stack may have become
         unmapped meanwhile (CONFIG_DEBUG_PAGEALLOC). */
#if IDEFLOPPY_DEBUG_BUGS
	if (floppy->pc->c[0] == IDEFLOPPY_REQUEST_SENSE_CMD &&
	    pc->c[0] == IDEFLOPPY_REQUEST_SENSE_CMD) {
		printk(KERN_ERR "ide-floppy: possible ide-floppy.c bug - "
			"Two request sense in serial were issued\n");
	}
#endif /* IDEFLOPPY_DEBUG_BUGS */
#endif

	if (floppy->failed_pc == NULL &&
	    pc->c[0] != IDEFLOPPY_REQUEST_SENSE_CMD)
		floppy->failed_pc = pc;
	/* Set the current packet command */
	floppy->pc = pc;

	if (pc->retries > IDEFLOPPY_MAX_PC_RETRIES ||
	    test_bit(PC_ABORT, &pc->flags)) {
		/*
		 *	We will "abort" retrying a packet command in case
		 *	a legitimate error code was received.
		 */
		if (!test_bit(PC_ABORT, &pc->flags)) {
			if (!test_bit(PC_SUPPRESS_ERROR, &pc->flags)) {
				if (idefloppy_should_report_error(floppy))
					printk(KERN_ERR "ide-floppy: %s: I/O error, "
					       "pc = %2x, key = %2x, "
					       "asc = %2x, ascq = %2x\n",
					       drive->name, pc->c[0],
					       floppy->sense_key,
					       floppy->asc, floppy->ascq);
			}
			/* Giving up */
			pc->error = IDEFLOPPY_ERROR_GENERAL;
		}
		floppy->failed_pc = NULL;
		pc->callback(drive);
		return ide_stopped;
	}

	debug_log(KERN_INFO "Retry number - %d\n",pc->retries);

	pc->retries++;
	/* We haven't transferred any data yet */
	pc->actually_transferred = 0;
	pc->current_position = pc->buffer;
	bcount.all = min(pc->request_transfer, 63 * 1024);

	if (test_and_clear_bit(PC_DMA_ERROR, &pc->flags)) {
		(void)__ide_dma_off(drive);
	}
	feature.all = 0;

	if (test_bit(PC_DMA_RECOMMENDED, &pc->flags) && drive->using_dma)
		feature.b.dma = !hwif->dma_setup(drive);

	if (IDE_CONTROL_REG)
		HWIF(drive)->OUTB(drive->ctl, IDE_CONTROL_REG);
	/* Use PIO/DMA */
	HWIF(drive)->OUTB(feature.all, IDE_FEATURE_REG);
	HWIF(drive)->OUTB(bcount.b.high, IDE_BCOUNTH_REG);
	HWIF(drive)->OUTB(bcount.b.low, IDE_BCOUNTL_REG);
	HWIF(drive)->OUTB(drive->select.all, IDE_SELECT_REG);

	if (feature.b.dma) {	/* Begin DMA, if necessary */
		set_bit(PC_DMA_IN_PROGRESS, &pc->flags);
		hwif->dma_start(drive);
	}

	/* Can we transfer the packet when we get the interrupt or wait? */
	if (test_bit(IDEFLOPPY_ZIP_DRIVE, &floppy->flags)) {
		/* wait */
		pkt_xfer_routine = &idefloppy_transfer_pc1;
	} else {
		/* immediate */
		pkt_xfer_routine = &idefloppy_transfer_pc;
	}
	
	if (test_bit (IDEFLOPPY_DRQ_INTERRUPT, &floppy->flags)) {
		/* Issue the packet command */
		ide_execute_command(drive, WIN_PACKETCMD,
				pkt_xfer_routine,
				IDEFLOPPY_WAIT_CMD,
				NULL);
		return ide_started;
	} else {
		/* Issue the packet command */
		HWIF(drive)->OUTB(WIN_PACKETCMD, IDE_COMMAND_REG);
		return (*pkt_xfer_routine) (drive);
	}
}

static void idefloppy_rw_callback (ide_drive_t *drive)
{
	debug_log(KERN_INFO "ide-floppy: Reached idefloppy_rw_callback\n");

	idefloppy_do_end_request(drive, 1, 0);
	return;
}

static void idefloppy_create_prevent_cmd (idefloppy_pc_t *pc, int prevent)
{
	debug_log(KERN_INFO "ide-floppy: creating prevent removal command, "
		"prevent = %d\n", prevent);

	idefloppy_init_pc(pc);
	pc->c[0] = IDEFLOPPY_PREVENT_REMOVAL_CMD;
	pc->c[4] = prevent;
}

static void idefloppy_create_read_capacity_cmd (idefloppy_pc_t *pc)
{
	idefloppy_init_pc(pc);
	pc->c[0] = IDEFLOPPY_READ_CAPACITY_CMD;
	pc->c[7] = 255;
	pc->c[8] = 255;
	pc->request_transfer = 255;
}

static void idefloppy_create_format_unit_cmd (idefloppy_pc_t *pc, int b, int l,
					      int flags)
{
	idefloppy_init_pc(pc);
	pc->c[0] = IDEFLOPPY_FORMAT_UNIT_CMD;
	pc->c[1] = 0x17;

	memset(pc->buffer, 0, 12);
	pc->buffer[1] = 0xA2;
	/* Default format list header, u8 1: FOV/DCRT/IMM bits set */

	if (flags & 1)				/* Verify bit on... */
		pc->buffer[1] ^= 0x20;		/* ... turn off DCRT bit */
	pc->buffer[3] = 8;

	put_unaligned(htonl(b), (unsigned int *)(&pc->buffer[4]));
	put_unaligned(htonl(l), (unsigned int *)(&pc->buffer[8]));
	pc->buffer_size=12;
	set_bit(PC_WRITING, &pc->flags);
}

/*
 *	A mode sense command is used to "sense" floppy parameters.
 */
static void idefloppy_create_mode_sense_cmd (idefloppy_pc_t *pc, u8 page_code, u8 type)
{
	u16 length = sizeof(idefloppy_mode_parameter_header_t);
	
	idefloppy_init_pc(pc);
	pc->c[0] = IDEFLOPPY_MODE_SENSE_CMD;
	pc->c[1] = 0;
	pc->c[2] = page_code + (type << 6);

	switch (page_code) {
		case IDEFLOPPY_CAPABILITIES_PAGE:
			length += 12;
			break;
		case IDEFLOPPY_FLEXIBLE_DISK_PAGE:
			length += 32;
			break;
		default:
			printk(KERN_ERR "ide-floppy: unsupported page code "
				"in create_mode_sense_cmd\n");
	}
	put_unaligned(htons(length), (u16 *) &pc->c[7]);
	pc->request_transfer = length;
}

static void idefloppy_create_start_stop_cmd (idefloppy_pc_t *pc, int start)
{
	idefloppy_init_pc(pc);
	pc->c[0] = IDEFLOPPY_START_STOP_CMD;
	pc->c[4] = start;
}

static void idefloppy_create_test_unit_ready_cmd(idefloppy_pc_t *pc)
{
	idefloppy_init_pc(pc);
	pc->c[0] = IDEFLOPPY_TEST_UNIT_READY_CMD;
}

static void idefloppy_create_rw_cmd (idefloppy_floppy_t *floppy, idefloppy_pc_t *pc, struct request *rq, unsigned long sector)
{
	int block = sector / floppy->bs_factor;
	int blocks = rq->nr_sectors / floppy->bs_factor;
	int cmd = rq_data_dir(rq);

	debug_log("create_rw1%d_cmd: block == %d, blocks == %d\n",
		2 * test_bit (IDEFLOPPY_USE_READ12, &floppy->flags),
		block, blocks);

	idefloppy_init_pc(pc);
	if (test_bit(IDEFLOPPY_USE_READ12, &floppy->flags)) {
		pc->c[0] = cmd == READ ? IDEFLOPPY_READ12_CMD : IDEFLOPPY_WRITE12_CMD;
		put_unaligned(htonl(blocks), (unsigned int *) &pc->c[6]);
	} else {
		pc->c[0] = cmd == READ ? IDEFLOPPY_READ10_CMD : IDEFLOPPY_WRITE10_CMD;
		put_unaligned(htons(blocks), (unsigned short *) &pc->c[7]);
	}
	put_unaligned(htonl(block), (unsigned int *) &pc->c[2]);
	pc->callback = &idefloppy_rw_callback;
	pc->rq = rq;
	pc->b_count = cmd == READ ? 0 : rq->bio->bi_size;
	if (rq->cmd_flags & REQ_RW)
		set_bit(PC_WRITING, &pc->flags);
	pc->buffer = NULL;
	pc->request_transfer = pc->buffer_size = blocks * floppy->block_size;
	set_bit(PC_DMA_RECOMMENDED, &pc->flags);
}

static int
idefloppy_blockpc_cmd(idefloppy_floppy_t *floppy, idefloppy_pc_t *pc, struct request *rq)
{
	/*
	 * just support eject for now, it would not be hard to make the
	 * REQ_BLOCK_PC support fully-featured
	 */
	if (rq->cmd[0] != IDEFLOPPY_START_STOP_CMD)
		return 1;

	idefloppy_init_pc(pc);
	memcpy(pc->c, rq->cmd, sizeof(pc->c));
	return 0;
}

/*
 *	idefloppy_do_request is our request handling function.	
 */
static ide_startstop_t idefloppy_do_request (ide_drive_t *drive, struct request *rq, sector_t block_s)
{
	idefloppy_floppy_t *floppy = drive->driver_data;
	idefloppy_pc_t *pc;
	unsigned long block = (unsigned long)block_s;

	debug_log(KERN_INFO "dev: %s, flags: %lx, errors: %d\n",
			rq->rq_disk ? rq->rq_disk->disk_name : "?",
			rq->flags, rq->errors);
	debug_log(KERN_INFO "sector: %ld, nr_sectors: %ld, "
			"current_nr_sectors: %d\n", (long)rq->sector,
			rq->nr_sectors, rq->current_nr_sectors);

	if (rq->errors >= ERROR_MAX) {
		if (floppy->failed_pc != NULL) {
			if (idefloppy_should_report_error(floppy))
				printk(KERN_ERR "ide-floppy: %s: I/O error, pc = %2x,"
				       " key = %2x, asc = %2x, ascq = %2x\n",
				       drive->name, floppy->failed_pc->c[0],
				       floppy->sense_key, floppy->asc, floppy->ascq);
		}
		else
			printk(KERN_ERR "ide-floppy: %s: I/O error\n",
				drive->name);
		idefloppy_do_end_request(drive, 0, 0);
		return ide_stopped;
	}
	if (blk_fs_request(rq)) {
		if (((long)rq->sector % floppy->bs_factor) ||
		    (rq->nr_sectors % floppy->bs_factor)) {
			printk("%s: unsupported r/w request size\n",
				drive->name);
			idefloppy_do_end_request(drive, 0, 0);
			return ide_stopped;
		}
		pc = idefloppy_next_pc_storage(drive);
		idefloppy_create_rw_cmd(floppy, pc, rq, block);
	} else if (blk_special_request(rq)) {
		pc = (idefloppy_pc_t *) rq->buffer;
	} else if (blk_pc_request(rq)) {
		pc = idefloppy_next_pc_storage(drive);
		if (idefloppy_blockpc_cmd(floppy, pc, rq)) {
			idefloppy_do_end_request(drive, 0, 0);
			return ide_stopped;
		}
	} else {
		blk_dump_rq_flags(rq,
			"ide-floppy: unsupported command in queue");
		idefloppy_do_end_request(drive, 0, 0);
		return ide_stopped;
	}

	pc->rq = rq;
	return idefloppy_issue_pc(drive, pc);
}

/*
 *	idefloppy_queue_pc_tail adds a special packet command request to the
 *	tail of the request queue, and waits for it to be serviced.
 */
static int idefloppy_queue_pc_tail (ide_drive_t *drive,idefloppy_pc_t *pc)
{
	struct ide_floppy_obj *floppy = drive->driver_data;
	struct request rq;

	ide_init_drive_cmd (&rq);
	rq.buffer = (char *) pc;
	rq.cmd_type = REQ_TYPE_SPECIAL;
	rq.rq_disk = floppy->disk;

	return ide_do_drive_cmd(drive, &rq, ide_wait);
}

/*
 *	Look at the flexible disk page parameters. We will ignore the CHS
 *	capacity parameters and use the LBA parameters instead.
 */
static int idefloppy_get_flexible_disk_page (ide_drive_t *drive)
{
	idefloppy_floppy_t *floppy = drive->driver_data;
	idefloppy_pc_t pc;
	idefloppy_mode_parameter_header_t *header;
	idefloppy_flexible_disk_page_t *page;
	int capacity, lba_capacity;

	idefloppy_create_mode_sense_cmd(&pc, IDEFLOPPY_FLEXIBLE_DISK_PAGE, MODE_SENSE_CURRENT);
	if (idefloppy_queue_pc_tail(drive,&pc)) {
		printk(KERN_ERR "ide-floppy: Can't get flexible disk "
			"page parameters\n");
		return 1;
	}
	header = (idefloppy_mode_parameter_header_t *) pc.buffer;
	floppy->wp = header->wp;
	set_disk_ro(floppy->disk, floppy->wp);
	page = (idefloppy_flexible_disk_page_t *) (header + 1);

	page->transfer_rate = ntohs(page->transfer_rate);
	page->sector_size = ntohs(page->sector_size);
	page->cyls = ntohs(page->cyls);
	page->rpm = ntohs(page->rpm);
	capacity = page->cyls * page->heads * page->sectors * page->sector_size;
	if (memcmp (page, &floppy->flexible_disk_page, sizeof (idefloppy_flexible_disk_page_t)))
		printk(KERN_INFO "%s: %dkB, %d/%d/%d CHS, %d kBps, "
				"%d sector size, %d rpm\n",
			drive->name, capacity / 1024, page->cyls,
			page->heads, page->sectors,
			page->transfer_rate / 8, page->sector_size, page->rpm);

	floppy->flexible_disk_page = *page;
	drive->bios_cyl = page->cyls;
	drive->bios_head = page->heads;
	drive->bios_sect = page->sectors;
	lba_capacity = floppy->blocks * floppy->block_size;
	if (capacity < lba_capacity) {
		printk(KERN_NOTICE "%s: The disk reports a capacity of %d "
			"bytes, but the drive only handles %d\n",
			drive->name, lba_capacity, capacity);
		floppy->blocks = floppy->block_size ? capacity / floppy->block_size : 0;
	}
	return 0;
}

static int idefloppy_get_capability_page(ide_drive_t *drive)
{
	idefloppy_floppy_t *floppy = drive->driver_data;
	idefloppy_pc_t pc;
	idefloppy_mode_parameter_header_t *header;
	idefloppy_capabilities_page_t *page;

	floppy->srfp = 0;
	idefloppy_create_mode_sense_cmd(&pc, IDEFLOPPY_CAPABILITIES_PAGE,
						 MODE_SENSE_CURRENT);

	set_bit(PC_SUPPRESS_ERROR, &pc.flags);
	if (idefloppy_queue_pc_tail(drive,&pc)) {
		return 1;
	}

	header = (idefloppy_mode_parameter_header_t *) pc.buffer;
	page= (idefloppy_capabilities_page_t *)(header+1);
	floppy->srfp = page->srfp;
	return (0);
}

/*
 *	Determine if a media is present in the floppy drive, and if so,
 *	its LBA capacity.
 */
static int idefloppy_get_capacity (ide_drive_t *drive)
{
	idefloppy_floppy_t *floppy = drive->driver_data;
	idefloppy_pc_t pc;
	idefloppy_capacity_header_t *header;
	idefloppy_capacity_descriptor_t *descriptor;
	int i, descriptors, rc = 1, blocks, length;
	
	drive->bios_cyl = 0;
	drive->bios_head = drive->bios_sect = 0;
	floppy->blocks = floppy->bs_factor = 0;
	set_capacity(floppy->disk, 0);

	idefloppy_create_read_capacity_cmd(&pc);
	if (idefloppy_queue_pc_tail(drive, &pc)) {
		printk(KERN_ERR "ide-floppy: Can't get floppy parameters\n");
		return 1;
	}
	header = (idefloppy_capacity_header_t *) pc.buffer;
	descriptors = header->length / sizeof(idefloppy_capacity_descriptor_t);
	descriptor = (idefloppy_capacity_descriptor_t *) (header + 1);

	for (i = 0; i < descriptors; i++, descriptor++) {
		blocks = descriptor->blocks = ntohl(descriptor->blocks);
		length = descriptor->length = ntohs(descriptor->length);

		if (!i) 
		{
		switch (descriptor->dc) {
		/* Clik! drive returns this instead of CAPACITY_CURRENT */
		case CAPACITY_UNFORMATTED:
			if (!test_bit(IDEFLOPPY_CLIK_DRIVE, &floppy->flags))
                                /*
				 * If it is not a clik drive, break out
				 * (maintains previous driver behaviour)
				 */
				break;
		case CAPACITY_CURRENT:
			/* Normal Zip/LS-120 disks */
			if (memcmp(descriptor, &floppy->capacity, sizeof (idefloppy_capacity_descriptor_t)))
				printk(KERN_INFO "%s: %dkB, %d blocks, %d "
					"sector size\n", drive->name,
					blocks * length / 1024, blocks, length);
			floppy->capacity = *descriptor;
			if (!length || length % 512) {
				printk(KERN_NOTICE "%s: %d bytes block size "
					"not supported\n", drive->name, length);
			} else {
                                floppy->blocks = blocks;
                                floppy->block_size = length;
                                if ((floppy->bs_factor = length / 512) != 1)
                                        printk(KERN_NOTICE "%s: warning: non "
						"512 bytes block size not "
						"fully supported\n",
						drive->name);
                                rc = 0;
			}
			break;
		case CAPACITY_NO_CARTRIDGE:
			/*
			 * This is a KERN_ERR so it appears on screen
			 * for the user to see
			 */
			printk(KERN_ERR "%s: No disk in drive\n", drive->name);
			break;
		case CAPACITY_INVALID:
			printk(KERN_ERR "%s: Invalid capacity for disk "
				"in drive\n", drive->name);
			break;
		}
		}
		if (!i) {
			debug_log( "Descriptor 0 Code: %d\n",
				descriptor->dc);
		}
		debug_log( "Descriptor %d: %dkB, %d blocks, %d "
			"sector size\n", i, blocks * length / 1024, blocks,
			length);
	}

	/* Clik! disk does not support get_flexible_disk_page */
        if (!test_bit(IDEFLOPPY_CLIK_DRIVE, &floppy->flags)) {
		(void) idefloppy_get_flexible_disk_page(drive);
	}

	set_capacity(floppy->disk, floppy->blocks * floppy->bs_factor);
	return rc;
}

/*
** Obtain the list of formattable capacities.
** Very similar to idefloppy_get_capacity, except that we push the capacity
** descriptors to userland, instead of our own structures.
**
** Userland gives us the following structure:
**
** struct idefloppy_format_capacities {
**        int nformats;
**        struct {
**                int nblocks;
**                int blocksize;
**                } formats[];
**        } ;
**
** userland initializes nformats to the number of allocated formats[]
** records.  On exit we set nformats to the number of records we've
** actually initialized.
**
*/

static int idefloppy_get_format_capacities(ide_drive_t *drive, int __user *arg)
{
        idefloppy_pc_t pc;
	idefloppy_capacity_header_t *header;
        idefloppy_capacity_descriptor_t *descriptor;
	int i, descriptors, blocks, length;
	int u_array_size;
	int u_index;
	int __user *argp;

	if (get_user(u_array_size, arg))
		return (-EFAULT);

	if (u_array_size <= 0)
		return (-EINVAL);

	idefloppy_create_read_capacity_cmd(&pc);
	if (idefloppy_queue_pc_tail(drive, &pc)) {
		printk(KERN_ERR "ide-floppy: Can't get floppy parameters\n");
                return (-EIO);
        }
        header = (idefloppy_capacity_header_t *) pc.buffer;
        descriptors = header->length /
		sizeof(idefloppy_capacity_descriptor_t);
	descriptor = (idefloppy_capacity_descriptor_t *) (header + 1);

	u_index = 0;
	argp = arg + 1;

	/*
	** We always skip the first capacity descriptor.  That's the
	** current capacity.  We are interested in the remaining descriptors,
	** the formattable capacities.
	*/

	for (i=0; i<descriptors; i++, descriptor++) {
		if (u_index >= u_array_size)
			break;	/* User-supplied buffer too small */
		if (i == 0)
			continue;	/* Skip the first descriptor */

		blocks = ntohl(descriptor->blocks);
		length = ntohs(descriptor->length);

		if (put_user(blocks, argp))
			return(-EFAULT);
		++argp;

		if (put_user(length, argp))
			return (-EFAULT);
		++argp;

		++u_index;
	}

	if (put_user(u_index, arg))
		return (-EFAULT);
	return (0);
}

/*
** Send ATAPI_FORMAT_UNIT to the drive.
**
** Userland gives us the following structure:
**
** struct idefloppy_format_command {
**        int nblocks;
**        int blocksize;
**        int flags;
**        } ;
**
** flags is a bitmask, currently, the only defined flag is:
**
**        0x01 - verify media after format.
*/

static int idefloppy_begin_format(ide_drive_t *drive, int __user *arg)
{
	int blocks;
	int length;
	int flags;
	idefloppy_pc_t pc;

	if (get_user(blocks, arg) ||
	    get_user(length, arg+1) ||
	    get_user(flags, arg+2)) {
		return (-EFAULT);
	}

	/* Get the SFRP bit */
	(void) idefloppy_get_capability_page(drive);
	idefloppy_create_format_unit_cmd(&pc, blocks, length, flags);
	if (idefloppy_queue_pc_tail(drive, &pc)) {
                return (-EIO);
	}

	return (0);
}

/*
** Get ATAPI_FORMAT_UNIT progress indication.
**
** Userland gives a pointer to an int.  The int is set to a progress
** indicator 0-65536, with 65536=100%.
**
** If the drive does not support format progress indication, we just check
** the dsc bit, and return either 0 or 65536.
*/

static int idefloppy_get_format_progress(ide_drive_t *drive, int __user *arg)
{
	idefloppy_floppy_t *floppy = drive->driver_data;
	idefloppy_pc_t pc;
	int progress_indication = 0x10000;

	if (floppy->srfp) {
		idefloppy_create_request_sense_cmd(&pc);
		if (idefloppy_queue_pc_tail(drive, &pc)) {
			return (-EIO);
		}

		if (floppy->sense_key == 2 &&
		    floppy->asc == 4 &&
		    floppy->ascq == 4) {
			progress_indication = floppy->progress_indication;
		}
		/* Else assume format_unit has finished, and we're
		** at 0x10000 */
	} else {
		atapi_status_t status;
		unsigned long flags;

		local_irq_save(flags);
		status.all = HWIF(drive)->INB(IDE_STATUS_REG);
		local_irq_restore(flags);

		progress_indication = !status.b.dsc ? 0 : 0x10000;
	}
	if (put_user(progress_indication, arg))
		return (-EFAULT);

	return (0);
}

/*
 *	Return the current floppy capacity.
 */
static sector_t idefloppy_capacity (ide_drive_t *drive)
{
	idefloppy_floppy_t *floppy = drive->driver_data;
	unsigned long capacity = floppy->blocks * floppy->bs_factor;

	return capacity;
}

/*
 *	idefloppy_identify_device checks if we can support a drive,
 *	based on the ATAPI IDENTIFY command results.
 */
static int idefloppy_identify_device (ide_drive_t *drive,struct hd_driveid *id)
{
	struct idefloppy_id_gcw gcw;
#if IDEFLOPPY_DEBUG_INFO
	u16 mask,i;
	char buffer[80];
#endif /* IDEFLOPPY_DEBUG_INFO */

	*((u16 *) &gcw) = id->config;

#ifdef CONFIG_PPC
	/* kludge for Apple PowerBook internal zip */
	if ((gcw.device_type == 5) &&
	    !strstr(id->model, "CD-ROM") &&
	    strstr(id->model, "ZIP"))
		gcw.device_type = 0;			
#endif

#if IDEFLOPPY_DEBUG_INFO
	printk(KERN_INFO "Dumping ATAPI Identify Device floppy parameters\n");
	switch (gcw.protocol) {
		case 0: case 1: sprintf(buffer, "ATA");break;
		case 2:	sprintf(buffer, "ATAPI");break;
		case 3: sprintf(buffer, "Reserved (Unknown to ide-floppy)");break;
	}
	printk(KERN_INFO "Protocol Type: %s\n", buffer);
	switch (gcw.device_type) {
		case 0: sprintf(buffer, "Direct-access Device");break;
		case 1: sprintf(buffer, "Streaming Tape Device");break;
		case 2: case 3: case 4: sprintf (buffer, "Reserved");break;
		case 5: sprintf(buffer, "CD-ROM Device");break;
		case 6: sprintf(buffer, "Reserved");
		case 7: sprintf(buffer, "Optical memory Device");break;
		case 0x1f: sprintf(buffer, "Unknown or no Device type");break;
		default: sprintf(buffer, "Reserved");
	}
	printk(KERN_INFO "Device Type: %x - %s\n", gcw.device_type, buffer);
	printk(KERN_INFO "Removable: %s\n",gcw.removable ? "Yes":"No");	
	switch (gcw.drq_type) {
		case 0: sprintf(buffer, "Microprocessor DRQ");break;
		case 1: sprintf(buffer, "Interrupt DRQ");break;
		case 2: sprintf(buffer, "Accelerated DRQ");break;
		case 3: sprintf(buffer, "Reserved");break;
	}
	printk(KERN_INFO "Command Packet DRQ Type: %s\n", buffer);
	switch (gcw.packet_size) {
		case 0: sprintf(buffer, "12 bytes");break;
		case 1: sprintf(buffer, "16 bytes");break;
		default: sprintf(buffer, "Reserved");break;
	}
	printk(KERN_INFO "Command Packet Size: %s\n", buffer);
	printk(KERN_INFO "Model: %.40s\n",id->model);
	printk(KERN_INFO "Firmware Revision: %.8s\n",id->fw_rev);
	printk(KERN_INFO "Serial Number: %.20s\n",id->serial_no);
	printk(KERN_INFO "Write buffer size(?): %d bytes\n",id->buf_size*512);
	printk(KERN_INFO "DMA: %s",id->capability & 0x01 ? "Yes\n":"No\n");
	printk(KERN_INFO "LBA: %s",id->capability & 0x02 ? "Yes\n":"No\n");
	printk(KERN_INFO "IORDY can be disabled: %s",id->capability & 0x04 ? "Yes\n":"No\n");
	printk(KERN_INFO "IORDY supported: %s",id->capability & 0x08 ? "Yes\n":"Unknown\n");
	printk(KERN_INFO "ATAPI overlap supported: %s",id->capability & 0x20 ? "Yes\n":"No\n");
	printk(KERN_INFO "PIO Cycle Timing Category: %d\n",id->tPIO);
	printk(KERN_INFO "DMA Cycle Timing Category: %d\n",id->tDMA);
	printk(KERN_INFO "Single Word DMA supported modes:\n");
	for (i=0,mask=1;i<8;i++,mask=mask << 1) {
		if (id->dma_1word & mask)
			printk(KERN_INFO "   Mode %d%s\n", i,
			(id->dma_1word & (mask << 8)) ? " (active)" : "");
	}
	printk(KERN_INFO "Multi Word DMA supported modes:\n");
	for (i=0,mask=1;i<8;i++,mask=mask << 1) {
		if (id->dma_mword & mask)
			printk(KERN_INFO "   Mode %d%s\n", i,
			(id->dma_mword & (mask << 8)) ? " (active)" : "");
	}
	if (id->field_valid & 0x0002) {
		printk(KERN_INFO "Enhanced PIO Modes: %s\n",
			id->eide_pio_modes & 1 ? "Mode 3":"None");
		if (id->eide_dma_min == 0)
			sprintf(buffer, "Not supported");
		else
			sprintf(buffer, "%d ns",id->eide_dma_min);
		printk(KERN_INFO "Minimum Multi-word DMA cycle per word: %s\n", buffer);
		if (id->eide_dma_time == 0)
			sprintf(buffer, "Not supported");
		else
			sprintf(buffer, "%d ns",id->eide_dma_time);
		printk(KERN_INFO "Manufacturer\'s Recommended Multi-word cycle: %s\n", buffer);
		if (id->eide_pio == 0)
			sprintf(buffer, "Not supported");
		else
			sprintf(buffer, "%d ns",id->eide_pio);
		printk(KERN_INFO "Minimum PIO cycle without IORDY: %s\n",
			buffer);
		if (id->eide_pio_iordy == 0)
			sprintf(buffer, "Not supported");
		else
			sprintf(buffer, "%d ns",id->eide_pio_iordy);
		printk(KERN_INFO "Minimum PIO cycle with IORDY: %s\n", buffer);
	} else
		printk(KERN_INFO "According to the device, fields 64-70 are not valid.\n");
#endif /* IDEFLOPPY_DEBUG_INFO */

	if (gcw.protocol != 2)
		printk(KERN_ERR "ide-floppy: Protocol is not ATAPI\n");
	else if (gcw.device_type != 0)
		printk(KERN_ERR "ide-floppy: Device type is not set to floppy\n");
	else if (!gcw.removable)
		printk(KERN_ERR "ide-floppy: The removable flag is not set\n");
	else if (gcw.drq_type == 3) {
		printk(KERN_ERR "ide-floppy: Sorry, DRQ type %d not supported\n", gcw.drq_type);
	} else if (gcw.packet_size != 0) {
		printk(KERN_ERR "ide-floppy: Packet size is not 12 bytes long\n");
	} else
		return 1;
	return 0;
}

static void idefloppy_add_settings(ide_drive_t *drive)
{
	idefloppy_floppy_t *floppy = drive->driver_data;

/*
 *			drive	setting name	read/write	ioctl	ioctl		data type	min	max	mul_factor	div_factor	data pointer		set function
 */
	ide_add_setting(drive,	"bios_cyl",		SETTING_RW,					-1,			-1,			TYPE_INT,	0,	1023,				1,	1,	&drive->bios_cyl,		NULL);
	ide_add_setting(drive,	"bios_head",		SETTING_RW,					-1,			-1,			TYPE_BYTE,	0,	255,				1,	1,	&drive->bios_head,		NULL);
	ide_add_setting(drive,	"bios_sect",		SETTING_RW,					-1,			-1,			TYPE_BYTE,	0,	63,				1,	1,	&drive->bios_sect,		NULL);
	ide_add_setting(drive,	"ticks",		SETTING_RW,					-1,			-1,			TYPE_BYTE,	0,	255,				1,	1,	&floppy->ticks,		NULL);
}

/*
 *	Driver initialization.
 */
static void idefloppy_setup (ide_drive_t *drive, idefloppy_floppy_t *floppy)
{
	struct idefloppy_id_gcw gcw;

	*((u16 *) &gcw) = drive->id->config;
	floppy->pc = floppy->pc_stack;
	if (gcw.drq_type == 1)
		set_bit(IDEFLOPPY_DRQ_INTERRUPT, &floppy->flags);
	/*
	 *	We used to check revisions here. At this point however
	 *	I'm giving up. Just assume they are all broken, its easier.
	 *
	 *	The actual reason for the workarounds was likely
	 *	a driver bug after all rather than a firmware bug,
	 *	and the workaround below used to hide it. It should
	 *	be fixed as of version 1.9, but to be on the safe side
	 *	we'll leave the limitation below for the 2.2.x tree.
	 */

	if (!strncmp(drive->id->model, "IOMEGA ZIP 100 ATAPI", 20)) {
		set_bit(IDEFLOPPY_ZIP_DRIVE, &floppy->flags);
		/* This value will be visible in the /proc/ide/hdx/settings */
		floppy->ticks = IDEFLOPPY_TICKS_DELAY;
		blk_queue_max_sectors(drive->queue, 64);
	}

	/*
	*      Guess what?  The IOMEGA Clik! drive also needs the
	*      above fix.  It makes nasty clicking noises without
	*      it, so please don't remove this.
	*/
	if (strncmp(drive->id->model, "IOMEGA Clik!", 11) == 0) {
		blk_queue_max_sectors(drive->queue, 64);
		set_bit(IDEFLOPPY_CLIK_DRIVE, &floppy->flags);
	}


	(void) idefloppy_get_capacity(drive);
	idefloppy_add_settings(drive);
}

static void ide_floppy_remove(ide_drive_t *drive)
{
	idefloppy_floppy_t *floppy = drive->driver_data;
	struct gendisk *g = floppy->disk;

	ide_unregister_subdriver(drive, floppy->driver);

	del_gendisk(g);

	ide_floppy_put(floppy);
}

static void ide_floppy_release(struct kref *kref)
{
	struct ide_floppy_obj *floppy = to_ide_floppy(kref);
	ide_drive_t *drive = floppy->drive;
	struct gendisk *g = floppy->disk;

	drive->driver_data = NULL;
	g->private_data = NULL;
	put_disk(g);
	kfree(floppy);
}

#ifdef CONFIG_PROC_FS

static int proc_idefloppy_read_capacity
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_drive_t*drive = (ide_drive_t *)data;
	int len;

	len = sprintf(page,"%llu\n", (long long)idefloppy_capacity(drive));
	PROC_IDE_READ_RETURN(page,start,off,count,eof,len);
}

static ide_proc_entry_t idefloppy_proc[] = {
	{ "capacity",	S_IFREG|S_IRUGO,	proc_idefloppy_read_capacity, NULL },
	{ "geometry",	S_IFREG|S_IRUGO,	proc_ide_read_geometry,	NULL },
	{ NULL, 0, NULL, NULL }
};

#else

#define	idefloppy_proc	NULL

#endif	/* CONFIG_PROC_FS */

static int ide_floppy_probe(ide_drive_t *);

static ide_driver_t idefloppy_driver = {
	.gen_driver = {
		.owner		= THIS_MODULE,
		.name		= "ide-floppy",
		.bus		= &ide_bus_type,
	},
	.probe			= ide_floppy_probe,
	.remove			= ide_floppy_remove,
	.version		= IDEFLOPPY_VERSION,
	.media			= ide_floppy,
	.supports_dsc_overlap	= 0,
	.do_request		= idefloppy_do_request,
	.end_request		= idefloppy_do_end_request,
	.error			= __ide_error,
	.abort			= __ide_abort,
	.proc			= idefloppy_proc,
};

static int idefloppy_open(struct inode *inode, struct file *filp)
{
	struct gendisk *disk = inode->i_bdev->bd_disk;
	struct ide_floppy_obj *floppy;
	ide_drive_t *drive;
	idefloppy_pc_t pc;
	int ret = 0;

	debug_log(KERN_INFO "Reached idefloppy_open\n");

	if (!(floppy = ide_floppy_get(disk)))
		return -ENXIO;

	drive = floppy->drive;

	drive->usage++;

	if (drive->usage == 1) {
		clear_bit(IDEFLOPPY_FORMAT_IN_PROGRESS, &floppy->flags);
		/* Just in case */

		idefloppy_create_test_unit_ready_cmd(&pc);
		if (idefloppy_queue_pc_tail(drive, &pc)) {
			idefloppy_create_start_stop_cmd(&pc, 1);
			(void) idefloppy_queue_pc_tail(drive, &pc);
		}

		if (idefloppy_get_capacity (drive)
		   && (filp->f_flags & O_NDELAY) == 0
		    /*
		    ** Allow O_NDELAY to open a drive without a disk, or with
		    ** an unreadable disk, so that we can get the format
		    ** capacity of the drive or begin the format - Sam
		    */
		    ) {
			drive->usage--;
			ret = -EIO;
			goto out_put_floppy;
		}

		if (floppy->wp && (filp->f_mode & 2)) {
			drive->usage--;
			ret = -EROFS;
			goto out_put_floppy;
		}
		set_bit(IDEFLOPPY_MEDIA_CHANGED, &floppy->flags);
		/* IOMEGA Clik! drives do not support lock/unlock commands */
                if (!test_bit(IDEFLOPPY_CLIK_DRIVE, &floppy->flags)) {
			idefloppy_create_prevent_cmd(&pc, 1);
			(void) idefloppy_queue_pc_tail(drive, &pc);
		}
		check_disk_change(inode->i_bdev);
	} else if (test_bit(IDEFLOPPY_FORMAT_IN_PROGRESS, &floppy->flags)) {
		drive->usage--;
		ret = -EBUSY;
		goto out_put_floppy;
	}
	return 0;

out_put_floppy:
	ide_floppy_put(floppy);
	return ret;
}

static int idefloppy_release(struct inode *inode, struct file *filp)
{
	struct gendisk *disk = inode->i_bdev->bd_disk;
	struct ide_floppy_obj *floppy = ide_floppy_g(disk);
	ide_drive_t *drive = floppy->drive;
	idefloppy_pc_t pc;
	
	debug_log(KERN_INFO "Reached idefloppy_release\n");

	if (drive->usage == 1) {
		/* IOMEGA Clik! drives do not support lock/unlock commands */
                if (!test_bit(IDEFLOPPY_CLIK_DRIVE, &floppy->flags)) {
			idefloppy_create_prevent_cmd(&pc, 0);
			(void) idefloppy_queue_pc_tail(drive, &pc);
		}

		clear_bit(IDEFLOPPY_FORMAT_IN_PROGRESS, &floppy->flags);
	}
	drive->usage--;

	ide_floppy_put(floppy);

	return 0;
}

static int idefloppy_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	struct ide_floppy_obj *floppy = ide_floppy_g(bdev->bd_disk);
	ide_drive_t *drive = floppy->drive;

	geo->heads = drive->bios_head;
	geo->sectors = drive->bios_sect;
	geo->cylinders = (u16)drive->bios_cyl; /* truncate */
	return 0;
}

static int idefloppy_ioctl(struct inode *inode, struct file *file,
			unsigned int cmd, unsigned long arg)
{
	struct block_device *bdev = inode->i_bdev;
	struct ide_floppy_obj *floppy = ide_floppy_g(bdev->bd_disk);
	ide_drive_t *drive = floppy->drive;
	void __user *argp = (void __user *)arg;
	int err;
	int prevent = (arg) ? 1 : 0;
	idefloppy_pc_t pc;

	switch (cmd) {
	case CDROMEJECT:
		prevent = 0;
		/* fall through */
	case CDROM_LOCKDOOR:
		if (drive->usage > 1)
			return -EBUSY;

		/* The IOMEGA Clik! Drive doesn't support this command - no room for an eject mechanism */
                if (!test_bit(IDEFLOPPY_CLIK_DRIVE, &floppy->flags)) {
			idefloppy_create_prevent_cmd(&pc, prevent);
			(void) idefloppy_queue_pc_tail(drive, &pc);
		}
		if (cmd == CDROMEJECT) {
			idefloppy_create_start_stop_cmd(&pc, 2);
			(void) idefloppy_queue_pc_tail(drive, &pc);
		}
		return 0;
	case IDEFLOPPY_IOCTL_FORMAT_SUPPORTED:
		return 0;
	case IDEFLOPPY_IOCTL_FORMAT_GET_CAPACITY:
		return idefloppy_get_format_capacities(drive, argp);
	case IDEFLOPPY_IOCTL_FORMAT_START:

		if (!(file->f_mode & 2))
			return -EPERM;

		if (drive->usage > 1) {
			/* Don't format if someone is using the disk */

			clear_bit(IDEFLOPPY_FORMAT_IN_PROGRESS,
				  &floppy->flags);
			return -EBUSY;
		}

		set_bit(IDEFLOPPY_FORMAT_IN_PROGRESS, &floppy->flags);

		err = idefloppy_begin_format(drive, argp);
		if (err)
			clear_bit(IDEFLOPPY_FORMAT_IN_PROGRESS, &floppy->flags);
		return err;
		/*
		** Note, the bit will be cleared when the device is
		** closed.  This is the cleanest way to handle the
		** situation where the drive does not support
		** format progress reporting.
		*/
	case IDEFLOPPY_IOCTL_FORMAT_GET_PROGRESS:
		return idefloppy_get_format_progress(drive, argp);
	}
	return generic_ide_ioctl(drive, file, bdev, cmd, arg);
}

static int idefloppy_media_changed(struct gendisk *disk)
{
	struct ide_floppy_obj *floppy = ide_floppy_g(disk);
	ide_drive_t *drive = floppy->drive;

	/* do not scan partitions twice if this is a removable device */
	if (drive->attach) {
		drive->attach = 0;
		return 0;
	}
	return test_and_clear_bit(IDEFLOPPY_MEDIA_CHANGED, &floppy->flags);
}

static int idefloppy_revalidate_disk(struct gendisk *disk)
{
	struct ide_floppy_obj *floppy = ide_floppy_g(disk);
	set_capacity(disk, idefloppy_capacity(floppy->drive));
	return 0;
}

static struct block_device_operations idefloppy_ops = {
	.owner		= THIS_MODULE,
	.open		= idefloppy_open,
	.release	= idefloppy_release,
	.ioctl		= idefloppy_ioctl,
	.getgeo		= idefloppy_getgeo,
	.media_changed	= idefloppy_media_changed,
	.revalidate_disk= idefloppy_revalidate_disk
};

static int ide_floppy_probe(ide_drive_t *drive)
{
	idefloppy_floppy_t *floppy;
	struct gendisk *g;

	if (!strstr("ide-floppy", drive->driver_req))
		goto failed;
	if (!drive->present)
		goto failed;
	if (drive->media != ide_floppy)
		goto failed;
	if (!idefloppy_identify_device (drive, drive->id)) {
		printk (KERN_ERR "ide-floppy: %s: not supported by this version of ide-floppy\n", drive->name);
		goto failed;
	}
	if (drive->scsi) {
		printk("ide-floppy: passing drive %s to ide-scsi emulation.\n", drive->name);
		goto failed;
	}
	if ((floppy = (idefloppy_floppy_t *) kzalloc (sizeof (idefloppy_floppy_t), GFP_KERNEL)) == NULL) {
		printk (KERN_ERR "ide-floppy: %s: Can't allocate a floppy structure\n", drive->name);
		goto failed;
	}

	g = alloc_disk(1 << PARTN_BITS);
	if (!g)
		goto out_free_floppy;

	ide_init_disk(g, drive);

	ide_register_subdriver(drive, &idefloppy_driver);

	kref_init(&floppy->kref);

	floppy->drive = drive;
	floppy->driver = &idefloppy_driver;
	floppy->disk = g;

	g->private_data = &floppy->driver;

	drive->driver_data = floppy;

	idefloppy_setup (drive, floppy);

	g->minors = 1 << PARTN_BITS;
	g->driverfs_dev = &drive->gendev;
	g->flags = drive->removable ? GENHD_FL_REMOVABLE : 0;
	g->fops = &idefloppy_ops;
	drive->attach = 1;
	add_disk(g);
	return 0;

out_free_floppy:
	kfree(floppy);
failed:
	return -ENODEV;
}

MODULE_DESCRIPTION("ATAPI FLOPPY Driver");

static void __exit idefloppy_exit (void)
{
	driver_unregister(&idefloppy_driver.gen_driver);
}

static int __init idefloppy_init(void)
{
	printk("ide-floppy driver " IDEFLOPPY_VERSION "\n");
	return driver_register(&idefloppy_driver.gen_driver);
}

MODULE_ALIAS("ide:*m-floppy*");
module_init(idefloppy_init);
module_exit(idefloppy_exit);
MODULE_LICENSE("GPL");
