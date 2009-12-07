/* Driver for Datafab USB Compact Flash reader
 *
 * datafab driver v0.1:
 *
 * First release
 *
 * Current development and maintenance by:
 *   (c) 2000 Jimmie Mayfield (mayfield+datafab@sackheads.org)
 *
 *   Many thanks to Robert Baruch for the SanDisk SmartMedia reader driver
 *   which I used as a template for this driver.
 *
 *   Some bugfixes and scatter-gather code by Gregory P. Smith 
 *   (greg-usb@electricrain.com)
 *
 *   Fix for media change by Joerg Schneider (js@joergschneider.com)
 *
 * Other contributors:
 *   (c) 2002 Alan Stern <stern@rowland.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * This driver attempts to support USB CompactFlash reader/writer devices
 * based on Datafab USB-to-ATA chips.  It was specifically developed for the 
 * Datafab MDCFE-B USB CompactFlash reader but has since been found to work 
 * with a variety of Datafab-based devices from a number of manufacturers.
 * I've received a report of this driver working with a Datafab-based
 * SmartMedia device though please be aware that I'm personally unable to
 * test SmartMedia support.
 *
 * This driver supports reading and writing.  If you're truly paranoid,
 * however, you can force the driver into a write-protected state by setting
 * the WP enable bits in datafab_handle_mode_sense().  See the comments
 * in that routine.
 */

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/slab.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>

#include "usb.h"
#include "transport.h"
#include "protocol.h"
#include "debug.h"

MODULE_DESCRIPTION("Driver for Datafab USB Compact Flash reader");
MODULE_AUTHOR("Jimmie Mayfield <mayfield+datafab@sackheads.org>");
MODULE_LICENSE("GPL");

struct datafab_info {
	unsigned long   sectors;	/* total sector count */
	unsigned long   ssize;		/* sector size in bytes */
	signed char	lun;		/* used for dual-slot readers */

	/* the following aren't used yet */
	unsigned char   sense_key;
	unsigned long   sense_asc;	/* additional sense code */
	unsigned long   sense_ascq;	/* additional sense code qualifier */
};

static int datafab_determine_lun(struct us_data *us,
				 struct datafab_info *info);


/*
 * The table of devices
 */
#define UNUSUAL_DEV(id_vendor, id_product, bcdDeviceMin, bcdDeviceMax, \
		    vendorName, productName, useProtocol, useTransport, \
		    initFunction, flags) \
{ USB_DEVICE_VER(id_vendor, id_product, bcdDeviceMin, bcdDeviceMax), \
  .driver_info = (flags)|(USB_US_TYPE_STOR<<24) }

struct usb_device_id datafab_usb_ids[] = {
#	include "unusual_datafab.h"
	{ }		/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, datafab_usb_ids);

#undef UNUSUAL_DEV

/*
 * The flags table
 */
#define UNUSUAL_DEV(idVendor, idProduct, bcdDeviceMin, bcdDeviceMax, \
		    vendor_name, product_name, use_protocol, use_transport, \
		    init_function, Flags) \
{ \
	.vendorName = vendor_name,	\
	.productName = product_name,	\
	.useProtocol = use_protocol,	\
	.useTransport = use_transport,	\
	.initFunction = init_function,	\
}

static struct us_unusual_dev datafab_unusual_dev_list[] = {
#	include "unusual_datafab.h"
	{ }		/* Terminating entry */
};

#undef UNUSUAL_DEV


static inline int
datafab_bulk_read(struct us_data *us, unsigned char *data, unsigned int len) {
	if (len == 0)
		return USB_STOR_XFER_GOOD;

	US_DEBUGP("datafab_bulk_read:  len = %d\n", len);
	return usb_stor_bulk_transfer_buf(us, us->recv_bulk_pipe,
			data, len, NULL);
}


static inline int
datafab_bulk_write(struct us_data *us, unsigned char *data, unsigned int len) {
	if (len == 0)
		return USB_STOR_XFER_GOOD;

	US_DEBUGP("datafab_bulk_write:  len = %d\n", len);
	return usb_stor_bulk_transfer_buf(us, us->send_bulk_pipe,
			data, len, NULL);
}


static int datafab_read_data(struct us_data *us,
			     struct datafab_info *info,
			     u32 sector,
			     u32 sectors)
{
	unsigned char *command = us->iobuf;
	unsigned char *buffer;
	unsigned char  thistime;
	unsigned int totallen, alloclen;
	int len, result;
	unsigned int sg_offset = 0;
	struct scatterlist *sg = NULL;

	// we're working in LBA mode.  according to the ATA spec, 
	// we can support up to 28-bit addressing.  I don't know if Datafab
	// supports beyond 24-bit addressing.  It's kind of hard to test 
	// since it requires > 8GB CF card.
	//
	if (sectors > 0x0FFFFFFF)
		return USB_STOR_TRANSPORT_ERROR;

	if (info->lun == -1) {
		result = datafab_determine_lun(us, info);
		if (result != USB_STOR_TRANSPORT_GOOD)
			return result;
	}

	totallen = sectors * info->ssize;

	// Since we don't read more than 64 KB at a time, we have to create
	// a bounce buffer and move the data a piece at a time between the
	// bounce buffer and the actual transfer buffer.

	alloclen = min(totallen, 65536u);
	buffer = kmalloc(alloclen, GFP_NOIO);
	if (buffer == NULL)
		return USB_STOR_TRANSPORT_ERROR;

	do {
		// loop, never allocate or transfer more than 64k at once
		// (min(128k, 255*info->ssize) is the real limit)

		len = min(totallen, alloclen);
		thistime = (len / info->ssize) & 0xff;

		command[0] = 0;
		command[1] = thistime;
		command[2] = sector & 0xFF;
		command[3] = (sector >> 8) & 0xFF;
		command[4] = (sector >> 16) & 0xFF;

		command[5] = 0xE0 + (info->lun << 4);
		command[5] |= (sector >> 24) & 0x0F;
		command[6] = 0x20;
		command[7] = 0x01;

		// send the read command
		result = datafab_bulk_write(us, command, 8);
		if (result != USB_STOR_XFER_GOOD)
			goto leave;

		// read the result
		result = datafab_bulk_read(us, buffer, len);
		if (result != USB_STOR_XFER_GOOD)
			goto leave;

		// Store the data in the transfer buffer
		usb_stor_access_xfer_buf(buffer, len, us->srb,
				 &sg, &sg_offset, TO_XFER_BUF);

		sector += thistime;
		totallen -= len;
	} while (totallen > 0);

	kfree(buffer);
	return USB_STOR_TRANSPORT_GOOD;

 leave:
	kfree(buffer);
	return USB_STOR_TRANSPORT_ERROR;
}


static int datafab_write_data(struct us_data *us,
			      struct datafab_info *info,
			      u32 sector,
			      u32 sectors)
{
	unsigned char *command = us->iobuf;
	unsigned char *reply = us->iobuf;
	unsigned char *buffer;
	unsigned char thistime;
	unsigned int totallen, alloclen;
	int len, result;
	unsigned int sg_offset = 0;
	struct scatterlist *sg = NULL;

	// we're working in LBA mode.  according to the ATA spec, 
	// we can support up to 28-bit addressing.  I don't know if Datafab
	// supports beyond 24-bit addressing.  It's kind of hard to test 
	// since it requires > 8GB CF card.
	//
	if (sectors > 0x0FFFFFFF)
		return USB_STOR_TRANSPORT_ERROR;

	if (info->lun == -1) {
		result = datafab_determine_lun(us, info);
		if (result != USB_STOR_TRANSPORT_GOOD)
			return result;
	}

	totallen = sectors * info->ssize;

	// Since we don't write more than 64 KB at a time, we have to create
	// a bounce buffer and move the data a piece at a time between the
	// bounce buffer and the actual transfer buffer.

	alloclen = min(totallen, 65536u);
	buffer = kmalloc(alloclen, GFP_NOIO);
	if (buffer == NULL)
		return USB_STOR_TRANSPORT_ERROR;

	do {
		// loop, never allocate or transfer more than 64k at once
		// (min(128k, 255*info->ssize) is the real limit)

		len = min(totallen, alloclen);
		thistime = (len / info->ssize) & 0xff;

		// Get the data from the transfer buffer
		usb_stor_access_xfer_buf(buffer, len, us->srb,
				&sg, &sg_offset, FROM_XFER_BUF);

		command[0] = 0;
		command[1] = thistime;
		command[2] = sector & 0xFF;
		command[3] = (sector >> 8) & 0xFF;
		command[4] = (sector >> 16) & 0xFF;

		command[5] = 0xE0 + (info->lun << 4);
		command[5] |= (sector >> 24) & 0x0F;
		command[6] = 0x30;
		command[7] = 0x02;

		// send the command
		result = datafab_bulk_write(us, command, 8);
		if (result != USB_STOR_XFER_GOOD)
			goto leave;

		// send the data
		result = datafab_bulk_write(us, buffer, len);
		if (result != USB_STOR_XFER_GOOD)
			goto leave;

		// read the result
		result = datafab_bulk_read(us, reply, 2);
		if (result != USB_STOR_XFER_GOOD)
			goto leave;

		if (reply[0] != 0x50 && reply[1] != 0) {
			US_DEBUGP("datafab_write_data:  Gah! "
				  "write return code: %02x %02x\n",
				  reply[0], reply[1]);
			result = USB_STOR_TRANSPORT_ERROR;
			goto leave;
		}

		sector += thistime;
		totallen -= len;
	} while (totallen > 0);

	kfree(buffer);
	return USB_STOR_TRANSPORT_GOOD;

 leave:
	kfree(buffer);
	return USB_STOR_TRANSPORT_ERROR;
}


static int datafab_determine_lun(struct us_data *us,
				 struct datafab_info *info)
{
	// Dual-slot readers can be thought of as dual-LUN devices.
	// We need to determine which card slot is being used.
	// We'll send an IDENTIFY DEVICE command and see which LUN responds...
	//
	// There might be a better way of doing this?

	static unsigned char scommand[8] = { 0, 1, 0, 0, 0, 0xa0, 0xec, 1 };
	unsigned char *command = us->iobuf;
	unsigned char *buf;
	int count = 0, rc;

	if (!info)
		return USB_STOR_TRANSPORT_ERROR;

	memcpy(command, scommand, 8);
	buf = kmalloc(512, GFP_NOIO);
	if (!buf)
		return USB_STOR_TRANSPORT_ERROR;

	US_DEBUGP("datafab_determine_lun:  locating...\n");

	// we'll try 3 times before giving up...
	//
	while (count++ < 3) {
		command[5] = 0xa0;

		rc = datafab_bulk_write(us, command, 8);
		if (rc != USB_STOR_XFER_GOOD) {
			rc = USB_STOR_TRANSPORT_ERROR;
			goto leave;
		}

		rc = datafab_bulk_read(us, buf, 512);
		if (rc == USB_STOR_XFER_GOOD) {
			info->lun = 0;
			rc = USB_STOR_TRANSPORT_GOOD;
			goto leave;
		}

		command[5] = 0xb0;

		rc = datafab_bulk_write(us, command, 8);
		if (rc != USB_STOR_XFER_GOOD) {
			rc = USB_STOR_TRANSPORT_ERROR;
			goto leave;
		}

		rc = datafab_bulk_read(us, buf, 512);
		if (rc == USB_STOR_XFER_GOOD) {
			info->lun = 1;
			rc = USB_STOR_TRANSPORT_GOOD;
			goto leave;
		}

		msleep(20);
	}

	rc = USB_STOR_TRANSPORT_ERROR;

 leave:
	kfree(buf);
	return rc;
}

static int datafab_id_device(struct us_data *us,
			     struct datafab_info *info)
{
	// this is a variation of the ATA "IDENTIFY DEVICE" command...according
	// to the ATA spec, 'Sector Count' isn't used but the Windows driver
	// sets this bit so we do too...
	//
	static unsigned char scommand[8] = { 0, 1, 0, 0, 0, 0xa0, 0xec, 1 };
	unsigned char *command = us->iobuf;
	unsigned char *reply;
	int rc;

	if (!info)
		return USB_STOR_TRANSPORT_ERROR;

	if (info->lun == -1) {
		rc = datafab_determine_lun(us, info);
		if (rc != USB_STOR_TRANSPORT_GOOD)
			return rc;
	}

	memcpy(command, scommand, 8);
	reply = kmalloc(512, GFP_NOIO);
	if (!reply)
		return USB_STOR_TRANSPORT_ERROR;

	command[5] += (info->lun << 4);

	rc = datafab_bulk_write(us, command, 8);
	if (rc != USB_STOR_XFER_GOOD) {
		rc = USB_STOR_TRANSPORT_ERROR;
		goto leave;
	}

	// we'll go ahead and extract the media capacity while we're here...
	//
	rc = datafab_bulk_read(us, reply, 512);
	if (rc == USB_STOR_XFER_GOOD) {
		// capacity is at word offset 57-58
		//
		info->sectors = ((u32)(reply[117]) << 24) | 
				((u32)(reply[116]) << 16) |
				((u32)(reply[115]) <<  8) | 
				((u32)(reply[114])      );
		rc = USB_STOR_TRANSPORT_GOOD;
		goto leave;
	}

	rc = USB_STOR_TRANSPORT_ERROR;

 leave:
	kfree(reply);
	return rc;
}


static int datafab_handle_mode_sense(struct us_data *us,
				     struct scsi_cmnd * srb, 
				     int sense_6)
{
	static unsigned char rw_err_page[12] = {
		0x1, 0xA, 0x21, 1, 0, 0, 0, 0, 1, 0, 0, 0
	};
	static unsigned char cache_page[12] = {
		0x8, 0xA, 0x1, 0, 0, 0, 0, 0, 0, 0, 0, 0
	};
	static unsigned char rbac_page[12] = {
		0x1B, 0xA, 0, 0x81, 0, 0, 0, 0, 0, 0, 0, 0
	};
	static unsigned char timer_page[8] = {
		0x1C, 0x6, 0, 0, 0, 0
	};
	unsigned char pc, page_code;
	unsigned int i = 0;
	struct datafab_info *info = (struct datafab_info *) (us->extra);
	unsigned char *ptr = us->iobuf;

	// most of this stuff is just a hack to get things working.  the
	// datafab reader doesn't present a SCSI interface so we
	// fudge the SCSI commands...
	//

	pc = srb->cmnd[2] >> 6;
	page_code = srb->cmnd[2] & 0x3F;

	switch (pc) {
	   case 0x0:
		US_DEBUGP("datafab_handle_mode_sense:  Current values\n");
		break;
	   case 0x1:
		US_DEBUGP("datafab_handle_mode_sense:  Changeable values\n");
		break;
	   case 0x2:
		US_DEBUGP("datafab_handle_mode_sense:  Default values\n");
		break;
	   case 0x3:
		US_DEBUGP("datafab_handle_mode_sense:  Saves values\n");
		break;
	}

	memset(ptr, 0, 8);
	if (sense_6) {
		ptr[2] = 0x00;		// WP enable: 0x80
		i = 4;
	} else {
		ptr[3] = 0x00;		// WP enable: 0x80
		i = 8;
	}

	switch (page_code) {
	   default:
		// vendor-specific mode
		info->sense_key = 0x05;
		info->sense_asc = 0x24;
		info->sense_ascq = 0x00;
		return USB_STOR_TRANSPORT_FAILED;

	   case 0x1:
		memcpy(ptr + i, rw_err_page, sizeof(rw_err_page));
		i += sizeof(rw_err_page);
		break;

	   case 0x8:
		memcpy(ptr + i, cache_page, sizeof(cache_page));
		i += sizeof(cache_page);
		break;

	   case 0x1B:
		memcpy(ptr + i, rbac_page, sizeof(rbac_page));
		i += sizeof(rbac_page);
		break;

	   case 0x1C:
		memcpy(ptr + i, timer_page, sizeof(timer_page));
		i += sizeof(timer_page);
		break;

	   case 0x3F:		// retrieve all pages
		memcpy(ptr + i, timer_page, sizeof(timer_page));
		i += sizeof(timer_page);
		memcpy(ptr + i, rbac_page, sizeof(rbac_page));
		i += sizeof(rbac_page);
		memcpy(ptr + i, cache_page, sizeof(cache_page));
		i += sizeof(cache_page);
		memcpy(ptr + i, rw_err_page, sizeof(rw_err_page));
		i += sizeof(rw_err_page);
		break;
	}

	if (sense_6)
		ptr[0] = i - 1;
	else
		((__be16 *) ptr)[0] = cpu_to_be16(i - 2);
	usb_stor_set_xfer_buf(ptr, i, srb);

	return USB_STOR_TRANSPORT_GOOD;
}

static void datafab_info_destructor(void *extra)
{
	// this routine is a placeholder...
	// currently, we don't allocate any extra memory so we're okay
}


// Transport for the Datafab MDCFE-B
//
static int datafab_transport(struct scsi_cmnd *srb, struct us_data *us)
{
	struct datafab_info *info;
	int rc;
	unsigned long block, blocks;
	unsigned char *ptr = us->iobuf;
	static unsigned char inquiry_reply[8] = {
		0x00, 0x80, 0x00, 0x01, 0x1F, 0x00, 0x00, 0x00
	};

	if (!us->extra) {
		us->extra = kzalloc(sizeof(struct datafab_info), GFP_NOIO);
		if (!us->extra) {
			US_DEBUGP("datafab_transport:  Gah! "
				  "Can't allocate storage for Datafab info struct!\n");
			return USB_STOR_TRANSPORT_ERROR;
		}
		us->extra_destructor = datafab_info_destructor;
  		((struct datafab_info *)us->extra)->lun = -1;
	}

	info = (struct datafab_info *) (us->extra);

	if (srb->cmnd[0] == INQUIRY) {
		US_DEBUGP("datafab_transport:  INQUIRY.  Returning bogus response");
		memcpy(ptr, inquiry_reply, sizeof(inquiry_reply));
		fill_inquiry_response(us, ptr, 36);
		return USB_STOR_TRANSPORT_GOOD;
	}

	if (srb->cmnd[0] == READ_CAPACITY) {
		info->ssize = 0x200;  // hard coded 512 byte sectors as per ATA spec
		rc = datafab_id_device(us, info);
		if (rc != USB_STOR_TRANSPORT_GOOD)
			return rc;

		US_DEBUGP("datafab_transport:  READ_CAPACITY:  %ld sectors, %ld bytes per sector\n",
			  info->sectors, info->ssize);

		// build the reply
		// we need the last sector, not the number of sectors
		((__be32 *) ptr)[0] = cpu_to_be32(info->sectors - 1);
		((__be32 *) ptr)[1] = cpu_to_be32(info->ssize);
		usb_stor_set_xfer_buf(ptr, 8, srb);

		return USB_STOR_TRANSPORT_GOOD;
	}

	if (srb->cmnd[0] == MODE_SELECT_10) {
		US_DEBUGP("datafab_transport:  Gah! MODE_SELECT_10.\n");
		return USB_STOR_TRANSPORT_ERROR;
	}

	// don't bother implementing READ_6 or WRITE_6.
	//
	if (srb->cmnd[0] == READ_10) {
		block = ((u32)(srb->cmnd[2]) << 24) | ((u32)(srb->cmnd[3]) << 16) |
			((u32)(srb->cmnd[4]) <<  8) | ((u32)(srb->cmnd[5]));

		blocks = ((u32)(srb->cmnd[7]) << 8) | ((u32)(srb->cmnd[8]));

		US_DEBUGP("datafab_transport:  READ_10: read block 0x%04lx  count %ld\n", block, blocks);
		return datafab_read_data(us, info, block, blocks);
	}

	if (srb->cmnd[0] == READ_12) {
		// we'll probably never see a READ_12 but we'll do it anyway...
		//
		block = ((u32)(srb->cmnd[2]) << 24) | ((u32)(srb->cmnd[3]) << 16) |
			((u32)(srb->cmnd[4]) <<  8) | ((u32)(srb->cmnd[5]));

		blocks = ((u32)(srb->cmnd[6]) << 24) | ((u32)(srb->cmnd[7]) << 16) |
			 ((u32)(srb->cmnd[8]) <<  8) | ((u32)(srb->cmnd[9]));

		US_DEBUGP("datafab_transport:  READ_12: read block 0x%04lx  count %ld\n", block, blocks);
		return datafab_read_data(us, info, block, blocks);
	}

	if (srb->cmnd[0] == WRITE_10) {
		block = ((u32)(srb->cmnd[2]) << 24) | ((u32)(srb->cmnd[3]) << 16) |
			((u32)(srb->cmnd[4]) <<  8) | ((u32)(srb->cmnd[5]));

		blocks = ((u32)(srb->cmnd[7]) << 8) | ((u32)(srb->cmnd[8]));

		US_DEBUGP("datafab_transport:  WRITE_10: write block 0x%04lx  count %ld\n", block, blocks);
		return datafab_write_data(us, info, block, blocks);
	}

	if (srb->cmnd[0] == WRITE_12) {
		// we'll probably never see a WRITE_12 but we'll do it anyway...
		//
		block = ((u32)(srb->cmnd[2]) << 24) | ((u32)(srb->cmnd[3]) << 16) |
			((u32)(srb->cmnd[4]) <<  8) | ((u32)(srb->cmnd[5]));

		blocks = ((u32)(srb->cmnd[6]) << 24) | ((u32)(srb->cmnd[7]) << 16) |
			 ((u32)(srb->cmnd[8]) <<  8) | ((u32)(srb->cmnd[9]));

		US_DEBUGP("datafab_transport:  WRITE_12: write block 0x%04lx  count %ld\n", block, blocks);
		return datafab_write_data(us, info, block, blocks);
	}

	if (srb->cmnd[0] == TEST_UNIT_READY) {
		US_DEBUGP("datafab_transport:  TEST_UNIT_READY.\n");
		return datafab_id_device(us, info);
	}

	if (srb->cmnd[0] == REQUEST_SENSE) {
		US_DEBUGP("datafab_transport:  REQUEST_SENSE.  Returning faked response\n");

		// this response is pretty bogus right now.  eventually if necessary
		// we can set the correct sense data.  so far though it hasn't been
		// necessary
		//
		memset(ptr, 0, 18);
		ptr[0] = 0xF0;
		ptr[2] = info->sense_key;
		ptr[7] = 11;
		ptr[12] = info->sense_asc;
		ptr[13] = info->sense_ascq;
		usb_stor_set_xfer_buf(ptr, 18, srb);

		return USB_STOR_TRANSPORT_GOOD;
	}

	if (srb->cmnd[0] == MODE_SENSE) {
		US_DEBUGP("datafab_transport:  MODE_SENSE_6 detected\n");
		return datafab_handle_mode_sense(us, srb, 1);
	}

	if (srb->cmnd[0] == MODE_SENSE_10) {
		US_DEBUGP("datafab_transport:  MODE_SENSE_10 detected\n");
		return datafab_handle_mode_sense(us, srb, 0);
	}

	if (srb->cmnd[0] == ALLOW_MEDIUM_REMOVAL) {
		// sure.  whatever.  not like we can stop the user from
		// popping the media out of the device (no locking doors, etc)
		//
		return USB_STOR_TRANSPORT_GOOD;
	}

	if (srb->cmnd[0] == START_STOP) {
		/* this is used by sd.c'check_scsidisk_media_change to detect
		   media change */
		US_DEBUGP("datafab_transport:  START_STOP.\n");
		/* the first datafab_id_device after a media change returns
		   an error (determined experimentally) */
		rc = datafab_id_device(us, info);
		if (rc == USB_STOR_TRANSPORT_GOOD) {
			info->sense_key = NO_SENSE;
			srb->result = SUCCESS;
		} else {
			info->sense_key = UNIT_ATTENTION;
			srb->result = SAM_STAT_CHECK_CONDITION;
		}
		return rc;
	}

	US_DEBUGP("datafab_transport:  Gah! Unknown command: %d (0x%x)\n",
		  srb->cmnd[0], srb->cmnd[0]);
	info->sense_key = 0x05;
	info->sense_asc = 0x20;
	info->sense_ascq = 0x00;
	return USB_STOR_TRANSPORT_FAILED;
}

static int datafab_probe(struct usb_interface *intf,
			 const struct usb_device_id *id)
{
	struct us_data *us;
	int result;

	result = usb_stor_probe1(&us, intf, id,
			(id - datafab_usb_ids) + datafab_unusual_dev_list);
	if (result)
		return result;

	us->transport_name  = "Datafab Bulk-Only";
	us->transport = datafab_transport;
	us->transport_reset = usb_stor_Bulk_reset;
	us->max_lun = 1;

	result = usb_stor_probe2(us);
	return result;
}

static struct usb_driver datafab_driver = {
	.name =		"ums-datafab",
	.probe =	datafab_probe,
	.disconnect =	usb_stor_disconnect,
	.suspend =	usb_stor_suspend,
	.resume =	usb_stor_resume,
	.reset_resume =	usb_stor_reset_resume,
	.pre_reset =	usb_stor_pre_reset,
	.post_reset =	usb_stor_post_reset,
	.id_table =	datafab_usb_ids,
	.soft_unbind =	1,
};

static int __init datafab_init(void)
{
	return usb_register(&datafab_driver);
}

static void __exit datafab_exit(void)
{
	usb_deregister(&datafab_driver);
}

module_init(datafab_init);
module_exit(datafab_exit);
