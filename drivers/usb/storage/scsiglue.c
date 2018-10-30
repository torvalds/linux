// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for USB Mass Storage compliant devices
 * SCSI layer glue code
 *
 * Current development and maintenance by:
 *   (c) 1999-2002 Matthew Dharm (mdharm-usb@one-eyed-alien.net)
 *
 * Developed with the assistance of:
 *   (c) 2000 David L. Brown, Jr. (usb-storage@davidb.org)
 *   (c) 2000 Stephen J. Gowdy (SGowdy@lbl.gov)
 *
 * Initial work by:
 *   (c) 1999 Michael Gee (michael@linuxspecific.com)
 *
 * This driver is based on the 'USB Mass Storage Class' document. This
 * describes in detail the protocol used to communicate with such
 * devices.  Clearly, the designers had SCSI and ATAPI commands in
 * mind when they created this document.  The commands are all very
 * similar to commands in the SCSI-II and ATAPI specifications.
 *
 * It is important to note that in a number of cases this class
 * exhibits class-specific exemptions from the USB specification.
 * Notably the usage of NAK, STALL and ACK differs from the norm, in
 * that they are used to communicate wait, failed and OK on commands.
 *
 * Also, for certain devices, the interrupt endpoint is used to convey
 * status of a command.
 */

#include <linux/module.h>
#include <linux/mutex.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_devinfo.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_eh.h>

#include "usb.h"
#include "scsiglue.h"
#include "debug.h"
#include "transport.h"
#include "protocol.h"

/*
 * Vendor IDs for companies that seem to include the READ CAPACITY bug
 * in all their devices
 */
#define VENDOR_ID_NOKIA		0x0421
#define VENDOR_ID_NIKON		0x04b0
#define VENDOR_ID_PENTAX	0x0a17
#define VENDOR_ID_MOTOROLA	0x22b8

/***********************************************************************
 * Host functions 
 ***********************************************************************/

static const char* host_info(struct Scsi_Host *host)
{
	struct us_data *us = host_to_us(host);
	return us->scsi_name;
}

static int slave_alloc (struct scsi_device *sdev)
{
	struct us_data *us = host_to_us(sdev->host);

	/*
	 * Set the INQUIRY transfer length to 36.  We don't use any of
	 * the extra data and many devices choke if asked for more or
	 * less than 36 bytes.
	 */
	sdev->inquiry_len = 36;

	/*
	 * USB has unusual DMA-alignment requirements: Although the
	 * starting address of each scatter-gather element doesn't matter,
	 * the length of each element except the last must be divisible
	 * by the Bulk maxpacket value.  There's currently no way to
	 * express this by block-layer constraints, so we'll cop out
	 * and simply require addresses to be aligned at 512-byte
	 * boundaries.  This is okay since most block I/O involves
	 * hardware sectors that are multiples of 512 bytes in length,
	 * and since host controllers up through USB 2.0 have maxpacket
	 * values no larger than 512.
	 *
	 * But it doesn't suffice for Wireless USB, where Bulk maxpacket
	 * values can be as large as 2048.  To make that work properly
	 * will require changes to the block layer.
	 */
	blk_queue_update_dma_alignment(sdev->request_queue, (512 - 1));

	/* Tell the SCSI layer if we know there is more than one LUN */
	if (us->protocol == USB_PR_BULK && us->max_lun > 0)
		sdev->sdev_bflags |= BLIST_FORCELUN;

	return 0;
}

static int slave_configure(struct scsi_device *sdev)
{
	struct us_data *us = host_to_us(sdev->host);

	/*
	 * Many devices have trouble transferring more than 32KB at a time,
	 * while others have trouble with more than 64K. At this time we
	 * are limiting both to 32K (64 sectores).
	 */
	if (us->fflags & (US_FL_MAX_SECTORS_64 | US_FL_MAX_SECTORS_MIN)) {
		unsigned int max_sectors = 64;

		if (us->fflags & US_FL_MAX_SECTORS_MIN)
			max_sectors = PAGE_SIZE >> 9;
		if (queue_max_hw_sectors(sdev->request_queue) > max_sectors)
			blk_queue_max_hw_sectors(sdev->request_queue,
					      max_sectors);
	} else if (sdev->type == TYPE_TAPE) {
		/*
		 * Tapes need much higher max_sector limits, so just
		 * raise it to the maximum possible (4 GB / 512) and
		 * let the queue segment size sort out the real limit.
		 */
		blk_queue_max_hw_sectors(sdev->request_queue, 0x7FFFFF);
	} else if (us->pusb_dev->speed >= USB_SPEED_SUPER) {
		/*
		 * USB3 devices will be limited to 2048 sectors. This gives us
		 * better throughput on most devices.
		 */
		blk_queue_max_hw_sectors(sdev->request_queue, 2048);
	}

	/*
	 * Some USB host controllers can't do DMA; they have to use PIO.
	 * They indicate this by setting their dma_mask to NULL.  For
	 * such controllers we need to make sure the block layer sets
	 * up bounce buffers in addressable memory.
	 */
	if (!us->pusb_dev->bus->controller->dma_mask)
		blk_queue_bounce_limit(sdev->request_queue, BLK_BOUNCE_HIGH);

	/*
	 * We can't put these settings in slave_alloc() because that gets
	 * called before the device type is known.  Consequently these
	 * settings can't be overridden via the scsi devinfo mechanism.
	 */
	if (sdev->type == TYPE_DISK) {

		/*
		 * Some vendors seem to put the READ CAPACITY bug into
		 * all their devices -- primarily makers of cell phones
		 * and digital cameras.  Since these devices always use
		 * flash media and can be expected to have an even number
		 * of sectors, we will always enable the CAPACITY_HEURISTICS
		 * flag unless told otherwise.
		 */
		switch (le16_to_cpu(us->pusb_dev->descriptor.idVendor)) {
		case VENDOR_ID_NOKIA:
		case VENDOR_ID_NIKON:
		case VENDOR_ID_PENTAX:
		case VENDOR_ID_MOTOROLA:
			if (!(us->fflags & (US_FL_FIX_CAPACITY |
					US_FL_CAPACITY_OK)))
				us->fflags |= US_FL_CAPACITY_HEURISTICS;
			break;
		}

		/*
		 * Disk-type devices use MODE SENSE(6) if the protocol
		 * (SubClass) is Transparent SCSI, otherwise they use
		 * MODE SENSE(10).
		 */
		if (us->subclass != USB_SC_SCSI && us->subclass != USB_SC_CYP_ATACB)
			sdev->use_10_for_ms = 1;

		/*
		 *Many disks only accept MODE SENSE transfer lengths of
		 * 192 bytes (that's what Windows uses).
		 */
		sdev->use_192_bytes_for_3f = 1;

		/*
		 * Some devices don't like MODE SENSE with page=0x3f,
		 * which is the command used for checking if a device
		 * is write-protected.  Now that we tell the sd driver
		 * to do a 192-byte transfer with this command the
		 * majority of devices work fine, but a few still can't
		 * handle it.  The sd driver will simply assume those
		 * devices are write-enabled.
		 */
		if (us->fflags & US_FL_NO_WP_DETECT)
			sdev->skip_ms_page_3f = 1;

		/*
		 * A number of devices have problems with MODE SENSE for
		 * page x08, so we will skip it.
		 */
		sdev->skip_ms_page_8 = 1;

		/* Some devices don't handle VPD pages correctly */
		sdev->skip_vpd_pages = 1;

		/* Do not attempt to use REPORT SUPPORTED OPERATION CODES */
		sdev->no_report_opcodes = 1;

		/* Do not attempt to use WRITE SAME */
		sdev->no_write_same = 1;

		/*
		 * Some disks return the total number of blocks in response
		 * to READ CAPACITY rather than the highest block number.
		 * If this device makes that mistake, tell the sd driver.
		 */
		if (us->fflags & US_FL_FIX_CAPACITY)
			sdev->fix_capacity = 1;

		/*
		 * A few disks have two indistinguishable version, one of
		 * which reports the correct capacity and the other does not.
		 * The sd driver has to guess which is the case.
		 */
		if (us->fflags & US_FL_CAPACITY_HEURISTICS)
			sdev->guess_capacity = 1;

		/* Some devices cannot handle READ_CAPACITY_16 */
		if (us->fflags & US_FL_NO_READ_CAPACITY_16)
			sdev->no_read_capacity_16 = 1;

		/*
		 * Many devices do not respond properly to READ_CAPACITY_16.
		 * Tell the SCSI layer to try READ_CAPACITY_10 first.
		 * However some USB 3.0 drive enclosures return capacity
		 * modulo 2TB. Those must use READ_CAPACITY_16
		 */
		if (!(us->fflags & US_FL_NEEDS_CAP16))
			sdev->try_rc_10_first = 1;

		/* assume SPC3 or latter devices support sense size > 18 */
		if (sdev->scsi_level > SCSI_SPC_2)
			us->fflags |= US_FL_SANE_SENSE;

		/*
		 * USB-IDE bridges tend to report SK = 0x04 (Non-recoverable
		 * Hardware Error) when any low-level error occurs,
		 * recoverable or not.  Setting this flag tells the SCSI
		 * midlayer to retry such commands, which frequently will
		 * succeed and fix the error.  The worst this can lead to
		 * is an occasional series of retries that will all fail.
		 */
		sdev->retry_hwerror = 1;

		/*
		 * USB disks should allow restart.  Some drives spin down
		 * automatically, requiring a START-STOP UNIT command.
		 */
		sdev->allow_restart = 1;

		/*
		 * Some USB cardreaders have trouble reading an sdcard's last
		 * sector in a larger then 1 sector read, since the performance
		 * impact is negligible we set this flag for all USB disks
		 */
		sdev->last_sector_bug = 1;

		/*
		 * Enable last-sector hacks for single-target devices using
		 * the Bulk-only transport, unless we already know the
		 * capacity will be decremented or is correct.
		 */
		if (!(us->fflags & (US_FL_FIX_CAPACITY | US_FL_CAPACITY_OK |
					US_FL_SCM_MULT_TARG)) &&
				us->protocol == USB_PR_BULK)
			us->use_last_sector_hacks = 1;

		/* Check if write cache default on flag is set or not */
		if (us->fflags & US_FL_WRITE_CACHE)
			sdev->wce_default_on = 1;

		/* A few buggy USB-ATA bridges don't understand FUA */
		if (us->fflags & US_FL_BROKEN_FUA)
			sdev->broken_fua = 1;

		/* Some even totally fail to indicate a cache */
		if (us->fflags & US_FL_ALWAYS_SYNC) {
			/* don't read caching information */
			sdev->skip_ms_page_8 = 1;
			sdev->skip_ms_page_3f = 1;
			/* assume sync is needed */
			sdev->wce_default_on = 1;
		}
	} else {

		/*
		 * Non-disk-type devices don't need to blacklist any pages
		 * or to force 192-byte transfer lengths for MODE SENSE.
		 * But they do need to use MODE SENSE(10).
		 */
		sdev->use_10_for_ms = 1;

		/* Some (fake) usb cdrom devices don't like READ_DISC_INFO */
		if (us->fflags & US_FL_NO_READ_DISC_INFO)
			sdev->no_read_disc_info = 1;
	}

	/*
	 * The CB and CBI transports have no way to pass LUN values
	 * other than the bits in the second byte of a CDB.  But those
	 * bits don't get set to the LUN value if the device reports
	 * scsi_level == 0 (UNKNOWN).  Hence such devices must necessarily
	 * be single-LUN.
	 */
	if ((us->protocol == USB_PR_CB || us->protocol == USB_PR_CBI) &&
			sdev->scsi_level == SCSI_UNKNOWN)
		us->max_lun = 0;

	/*
	 * Some devices choke when they receive a PREVENT-ALLOW MEDIUM
	 * REMOVAL command, so suppress those commands.
	 */
	if (us->fflags & US_FL_NOT_LOCKABLE)
		sdev->lockable = 0;

	/*
	 * this is to satisfy the compiler, tho I don't think the 
	 * return code is ever checked anywhere.
	 */
	return 0;
}

static int target_alloc(struct scsi_target *starget)
{
	struct us_data *us = host_to_us(dev_to_shost(starget->dev.parent));

	/*
	 * Some USB drives don't support REPORT LUNS, even though they
	 * report a SCSI revision level above 2.  Tell the SCSI layer
	 * not to issue that command; it will perform a normal sequential
	 * scan instead.
	 */
	starget->no_report_luns = 1;

	/*
	 * The UFI spec treats the Peripheral Qualifier bits in an
	 * INQUIRY result as reserved and requires devices to set them
	 * to 0.  However the SCSI spec requires these bits to be set
	 * to 3 to indicate when a LUN is not present.
	 *
	 * Let the scanning code know if this target merely sets
	 * Peripheral Device Type to 0x1f to indicate no LUN.
	 */
	if (us->subclass == USB_SC_UFI)
		starget->pdt_1f_for_no_lun = 1;

	return 0;
}

/* queue a command */
/* This is always called with scsi_lock(host) held */
static int queuecommand_lck(struct scsi_cmnd *srb,
			void (*done)(struct scsi_cmnd *))
{
	struct us_data *us = host_to_us(srb->device->host);

	/* check for state-transition errors */
	if (us->srb != NULL) {
		printk(KERN_ERR USB_STORAGE "Error in %s: us->srb = %p\n",
			__func__, us->srb);
		return SCSI_MLQUEUE_HOST_BUSY;
	}

	/* fail the command if we are disconnecting */
	if (test_bit(US_FLIDX_DISCONNECTING, &us->dflags)) {
		usb_stor_dbg(us, "Fail command during disconnect\n");
		srb->result = DID_NO_CONNECT << 16;
		done(srb);
		return 0;
	}

	if ((us->fflags & US_FL_NO_ATA_1X) &&
			(srb->cmnd[0] == ATA_12 || srb->cmnd[0] == ATA_16)) {
		memcpy(srb->sense_buffer, usb_stor_sense_invalidCDB,
		       sizeof(usb_stor_sense_invalidCDB));
		srb->result = SAM_STAT_CHECK_CONDITION;
		done(srb);
		return 0;
	}

	/* enqueue the command and wake up the control thread */
	srb->scsi_done = done;
	us->srb = srb;
	complete(&us->cmnd_ready);

	return 0;
}

static DEF_SCSI_QCMD(queuecommand)

/***********************************************************************
 * Error handling functions
 ***********************************************************************/

/* Command timeout and abort */
static int command_abort(struct scsi_cmnd *srb)
{
	struct us_data *us = host_to_us(srb->device->host);

	usb_stor_dbg(us, "%s called\n", __func__);

	/*
	 * us->srb together with the TIMED_OUT, RESETTING, and ABORTING
	 * bits are protected by the host lock.
	 */
	scsi_lock(us_to_host(us));

	/* Is this command still active? */
	if (us->srb != srb) {
		scsi_unlock(us_to_host(us));
		usb_stor_dbg(us, "-- nothing to abort\n");
		return FAILED;
	}

	/*
	 * Set the TIMED_OUT bit.  Also set the ABORTING bit, but only if
	 * a device reset isn't already in progress (to avoid interfering
	 * with the reset).  Note that we must retain the host lock while
	 * calling usb_stor_stop_transport(); otherwise it might interfere
	 * with an auto-reset that begins as soon as we release the lock.
	 */
	set_bit(US_FLIDX_TIMED_OUT, &us->dflags);
	if (!test_bit(US_FLIDX_RESETTING, &us->dflags)) {
		set_bit(US_FLIDX_ABORTING, &us->dflags);
		usb_stor_stop_transport(us);
	}
	scsi_unlock(us_to_host(us));

	/* Wait for the aborted command to finish */
	wait_for_completion(&us->notify);
	return SUCCESS;
}

/*
 * This invokes the transport reset mechanism to reset the state of the
 * device
 */
static int device_reset(struct scsi_cmnd *srb)
{
	struct us_data *us = host_to_us(srb->device->host);
	int result;

	usb_stor_dbg(us, "%s called\n", __func__);

	/* lock the device pointers and do the reset */
	mutex_lock(&(us->dev_mutex));
	result = us->transport_reset(us);
	mutex_unlock(&us->dev_mutex);

	return result < 0 ? FAILED : SUCCESS;
}

/* Simulate a SCSI bus reset by resetting the device's USB port. */
static int bus_reset(struct scsi_cmnd *srb)
{
	struct us_data *us = host_to_us(srb->device->host);
	int result;

	usb_stor_dbg(us, "%s called\n", __func__);

	result = usb_stor_port_reset(us);
	return result < 0 ? FAILED : SUCCESS;
}

/*
 * Report a driver-initiated device reset to the SCSI layer.
 * Calling this for a SCSI-initiated reset is unnecessary but harmless.
 * The caller must own the SCSI host lock.
 */
void usb_stor_report_device_reset(struct us_data *us)
{
	int i;
	struct Scsi_Host *host = us_to_host(us);

	scsi_report_device_reset(host, 0, 0);
	if (us->fflags & US_FL_SCM_MULT_TARG) {
		for (i = 1; i < host->max_id; ++i)
			scsi_report_device_reset(host, 0, i);
	}
}

/*
 * Report a driver-initiated bus reset to the SCSI layer.
 * Calling this for a SCSI-initiated reset is unnecessary but harmless.
 * The caller must not own the SCSI host lock.
 */
void usb_stor_report_bus_reset(struct us_data *us)
{
	struct Scsi_Host *host = us_to_host(us);

	scsi_lock(host);
	scsi_report_bus_reset(host, 0);
	scsi_unlock(host);
}

/***********************************************************************
 * /proc/scsi/ functions
 ***********************************************************************/

static int write_info(struct Scsi_Host *host, char *buffer, int length)
{
	/* if someone is sending us data, just throw it away */
	return length;
}

static int show_info (struct seq_file *m, struct Scsi_Host *host)
{
	struct us_data *us = host_to_us(host);
	const char *string;

	/* print the controller name */
	seq_printf(m, "   Host scsi%d: usb-storage\n", host->host_no);

	/* print product, vendor, and serial number strings */
	if (us->pusb_dev->manufacturer)
		string = us->pusb_dev->manufacturer;
	else if (us->unusual_dev->vendorName)
		string = us->unusual_dev->vendorName;
	else
		string = "Unknown";
	seq_printf(m, "       Vendor: %s\n", string);
	if (us->pusb_dev->product)
		string = us->pusb_dev->product;
	else if (us->unusual_dev->productName)
		string = us->unusual_dev->productName;
	else
		string = "Unknown";
	seq_printf(m, "      Product: %s\n", string);
	if (us->pusb_dev->serial)
		string = us->pusb_dev->serial;
	else
		string = "None";
	seq_printf(m, "Serial Number: %s\n", string);

	/* show the protocol and transport */
	seq_printf(m, "     Protocol: %s\n", us->protocol_name);
	seq_printf(m, "    Transport: %s\n", us->transport_name);

	/* show the device flags */
	seq_printf(m, "       Quirks:");

#define US_FLAG(name, value) \
	if (us->fflags & value) seq_printf(m, " " #name);
US_DO_ALL_FLAGS
#undef US_FLAG
	seq_putc(m, '\n');
	return 0;
}

/***********************************************************************
 * Sysfs interface
 ***********************************************************************/

/* Output routine for the sysfs max_sectors file */
static ssize_t max_sectors_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct scsi_device *sdev = to_scsi_device(dev);

	return sprintf(buf, "%u\n", queue_max_hw_sectors(sdev->request_queue));
}

/* Input routine for the sysfs max_sectors file */
static ssize_t max_sectors_store(struct device *dev, struct device_attribute *attr, const char *buf,
		size_t count)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	unsigned short ms;

	if (sscanf(buf, "%hu", &ms) > 0) {
		blk_queue_max_hw_sectors(sdev->request_queue, ms);
		return count;
	}
	return -EINVAL;
}
static DEVICE_ATTR_RW(max_sectors);

static struct device_attribute *sysfs_device_attr_list[] = {
	&dev_attr_max_sectors,
	NULL,
};

/*
 * this defines our host template, with which we'll allocate hosts
 */

static const struct scsi_host_template usb_stor_host_template = {
	/* basic userland interface stuff */
	.name =				"usb-storage",
	.proc_name =			"usb-storage",
	.show_info =			show_info,
	.write_info =			write_info,
	.info =				host_info,

	/* command interface -- queued only */
	.queuecommand =			queuecommand,

	/* error and abort handlers */
	.eh_abort_handler =		command_abort,
	.eh_device_reset_handler =	device_reset,
	.eh_bus_reset_handler =		bus_reset,

	/* queue commands only, only one command per LUN */
	.can_queue =			1,

	/* unknown initiator id */
	.this_id =			-1,

	.slave_alloc =			slave_alloc,
	.slave_configure =		slave_configure,
	.target_alloc =			target_alloc,

	/* lots of sg segments can be handled */
	.sg_tablesize =			SG_MAX_SEGMENTS,


	/*
	 * Limit the total size of a transfer to 120 KB.
	 *
	 * Some devices are known to choke with anything larger. It seems like
	 * the problem stems from the fact that original IDE controllers had
	 * only an 8-bit register to hold the number of sectors in one transfer
	 * and even those couldn't handle a full 256 sectors.
	 *
	 * Because we want to make sure we interoperate with as many devices as
	 * possible, we will maintain a 240 sector transfer size limit for USB
	 * Mass Storage devices.
	 *
	 * Tests show that other operating have similar limits with Microsoft
	 * Windows 7 limiting transfers to 128 sectors for both USB2 and USB3
	 * and Apple Mac OS X 10.11 limiting transfers to 256 sectors for USB2
	 * and 2048 for USB3 devices.
	 */
	.max_sectors =                  240,

	/*
	 * merge commands... this seems to help performance, but
	 * periodically someone should test to see which setting is more
	 * optimal.
	 */
	.use_clustering =		1,

	/* emulated HBA */
	.emulated =			1,

	/* we do our own delay after a device or bus reset */
	.skip_settle_delay =		1,

	/* sysfs device attributes */
	.sdev_attrs =			sysfs_device_attr_list,

	/* module management */
	.module =			THIS_MODULE
};

void usb_stor_host_template_init(struct scsi_host_template *sht,
				 const char *name, struct module *owner)
{
	*sht = usb_stor_host_template;
	sht->name = name;
	sht->proc_name = name;
	sht->module = owner;
}
EXPORT_SYMBOL_GPL(usb_stor_host_template_init);

/* To Report "Illegal Request: Invalid Field in CDB */
unsigned char usb_stor_sense_invalidCDB[18] = {
	[0]	= 0x70,			    /* current error */
	[2]	= ILLEGAL_REQUEST,	    /* Illegal Request = 0x05 */
	[7]	= 0x0a,			    /* additional length */
	[12]	= 0x24			    /* Invalid Field in CDB */
};
EXPORT_SYMBOL_GPL(usb_stor_sense_invalidCDB);
