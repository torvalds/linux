// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for SanDisk SDDR-55 SmartMedia reader
 *
 * SDDR55 driver v0.1:
 *
 * First release
 *
 * Current development and maintenance by:
 *   (c) 2002 Simon Munton
 */

#include <linux/jiffies.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/slab.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>

#include "usb.h"
#include "transport.h"
#include "protocol.h"
#include "debug.h"
#include "scsiglue.h"

#define DRV_NAME "ums-sddr55"

MODULE_DESCRIPTION("Driver for SanDisk SDDR-55 SmartMedia reader");
MODULE_AUTHOR("Simon Munton");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("USB_STORAGE");

/*
 * The table of devices
 */
#define UNUSUAL_DEV(id_vendor, id_product, bcdDeviceMin, bcdDeviceMax, \
		    vendorName, productName, useProtocol, useTransport, \
		    initFunction, flags) \
{ USB_DEVICE_VER(id_vendor, id_product, bcdDeviceMin, bcdDeviceMax), \
  .driver_info = (flags) }

static const struct usb_device_id sddr55_usb_ids[] = {
#	include "unusual_sddr55.h"
	{ }		/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, sddr55_usb_ids);

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

static const struct us_unusual_dev sddr55_unusual_dev_list[] = {
#	include "unusual_sddr55.h"
	{ }		/* Terminating entry */
};

#undef UNUSUAL_DEV


#define short_pack(lsb,msb) ( ((u16)(lsb)) | ( ((u16)(msb))<<8 ) )
#define LSB_of(s) ((s)&0xFF)
#define MSB_of(s) ((s)>>8)
#define PAGESIZE  512

#define set_sense_info(sk, asc, ascq)	\
    do {				\
	info->sense_data[2] = sk;	\
	info->sense_data[12] = asc;	\
	info->sense_data[13] = ascq;	\
	} while (0)


struct sddr55_card_info {
	unsigned long	capacity;	/* Size of card in bytes */
	int		max_log_blks;	/* maximum number of logical blocks */
	int		pageshift;	/* log2 of pagesize */
	int		smallpageshift;	/* 1 if pagesize == 256 */
	int		blocksize;	/* Size of block in pages */
	int		blockshift;	/* log2 of blocksize */
	int		blockmask;	/* 2^blockshift - 1 */
	int		read_only;	/* non zero if card is write protected */
	int		force_read_only;	/* non zero if we find a map error*/
	int		*lba_to_pba;	/* logical to physical map */
	int		*pba_to_lba;	/* physical to logical map */
	int		fatal_error;	/* set if we detect something nasty */
	unsigned long 	last_access;	/* number of jiffies since we last talked to device */
	unsigned char   sense_data[18];
};


#define NOT_ALLOCATED		0xffffffff
#define BAD_BLOCK		0xffff
#define CIS_BLOCK		0x400
#define UNUSED_BLOCK		0x3ff

static int
sddr55_bulk_transport(struct us_data *us, int direction,
		      unsigned char *data, unsigned int len) {
	struct sddr55_card_info *info = (struct sddr55_card_info *)us->extra;
	unsigned int pipe = (direction == DMA_FROM_DEVICE) ?
			us->recv_bulk_pipe : us->send_bulk_pipe;

	if (!len)
		return USB_STOR_XFER_GOOD;
	info->last_access = jiffies;
	return usb_stor_bulk_transfer_buf(us, pipe, data, len, NULL);
}

/*
 * check if card inserted, if there is, update read_only status
 * return non zero if no card
 */

static int sddr55_status(struct us_data *us)
{
	int result;
	unsigned char *command = us->iobuf;
	unsigned char *status = us->iobuf;
	struct sddr55_card_info *info = (struct sddr55_card_info *)us->extra;

	/* send command */
	memset(command, 0, 8);
	command[5] = 0xB0;
	command[7] = 0x80;
	result = sddr55_bulk_transport(us,
		DMA_TO_DEVICE, command, 8);

	usb_stor_dbg(us, "Result for send_command in status %d\n", result);

	if (result != USB_STOR_XFER_GOOD) {
		set_sense_info (4, 0, 0);	/* hardware error */
		return USB_STOR_TRANSPORT_ERROR;
	}

	result = sddr55_bulk_transport(us,
		DMA_FROM_DEVICE, status,	4);

	/* expect to get short transfer if no card fitted */
	if (result == USB_STOR_XFER_SHORT || result == USB_STOR_XFER_STALLED) {
		/* had a short transfer, no card inserted, free map memory */
		kfree(info->lba_to_pba);
		kfree(info->pba_to_lba);
		info->lba_to_pba = NULL;
		info->pba_to_lba = NULL;

		info->fatal_error = 0;
		info->force_read_only = 0;

		set_sense_info (2, 0x3a, 0);	/* not ready, medium not present */
		return USB_STOR_TRANSPORT_FAILED;
	}

	if (result != USB_STOR_XFER_GOOD) {
		set_sense_info (4, 0, 0);	/* hardware error */
		return USB_STOR_TRANSPORT_FAILED;
	}
	
	/* check write protect status */
	info->read_only = (status[0] & 0x20);

	/* now read status */
	result = sddr55_bulk_transport(us,
		DMA_FROM_DEVICE, status,	2);

	if (result != USB_STOR_XFER_GOOD) {
		set_sense_info (4, 0, 0);	/* hardware error */
	}

	return (result == USB_STOR_XFER_GOOD ?
			USB_STOR_TRANSPORT_GOOD : USB_STOR_TRANSPORT_FAILED);
}


static int sddr55_read_data(struct us_data *us,
		unsigned int lba,
		unsigned int page,
		unsigned short sectors) {

	int result = USB_STOR_TRANSPORT_GOOD;
	unsigned char *command = us->iobuf;
	unsigned char *status = us->iobuf;
	struct sddr55_card_info *info = (struct sddr55_card_info *)us->extra;
	unsigned char *buffer;

	unsigned int pba;
	unsigned int address;

	unsigned short pages;
	unsigned int len, offset;
	struct scatterlist *sg;

	// Since we only read in one block at a time, we have to create
	// a bounce buffer and move the data a piece at a time between the
	// bounce buffer and the actual transfer buffer.

	len = min_t(unsigned int, sectors, info->blocksize >>
			info->smallpageshift) * PAGESIZE;
	buffer = kmalloc(len, GFP_NOIO);
	if (buffer == NULL)
		return USB_STOR_TRANSPORT_ERROR; /* out of memory */
	offset = 0;
	sg = NULL;

	while (sectors>0) {

		/* have we got to end? */
		if (lba >= info->max_log_blks)
			break;

		pba = info->lba_to_pba[lba];

		// Read as many sectors as possible in this block

		pages = min_t(unsigned int, sectors << info->smallpageshift,
				info->blocksize - page);
		len = pages << info->pageshift;

		usb_stor_dbg(us, "Read %02X pages, from PBA %04X (LBA %04X) page %02X\n",
			     pages, pba, lba, page);

		if (pba == NOT_ALLOCATED) {
			/* no pba for this lba, fill with zeroes */
			memset (buffer, 0, len);
		} else {

			address = (pba << info->blockshift) + page;

			command[0] = 0;
			command[1] = LSB_of(address>>16);
			command[2] = LSB_of(address>>8);
			command[3] = LSB_of(address);

			command[4] = 0;
			command[5] = 0xB0;
			command[6] = LSB_of(pages << (1 - info->smallpageshift));
			command[7] = 0x85;

			/* send command */
			result = sddr55_bulk_transport(us,
				DMA_TO_DEVICE, command, 8);

			usb_stor_dbg(us, "Result for send_command in read_data %d\n",
				     result);

			if (result != USB_STOR_XFER_GOOD) {
				result = USB_STOR_TRANSPORT_ERROR;
				goto leave;
			}

			/* read data */
			result = sddr55_bulk_transport(us,
				DMA_FROM_DEVICE, buffer, len);

			if (result != USB_STOR_XFER_GOOD) {
				result = USB_STOR_TRANSPORT_ERROR;
				goto leave;
			}

			/* now read status */
			result = sddr55_bulk_transport(us,
				DMA_FROM_DEVICE, status, 2);

			if (result != USB_STOR_XFER_GOOD) {
				result = USB_STOR_TRANSPORT_ERROR;
				goto leave;
			}

			/* check status for error */
			if (status[0] == 0xff && status[1] == 0x4) {
				set_sense_info (3, 0x11, 0);
				result = USB_STOR_TRANSPORT_FAILED;
				goto leave;
			}
		}

		// Store the data in the transfer buffer
		usb_stor_access_xfer_buf(buffer, len, us->srb,
				&sg, &offset, TO_XFER_BUF);

		page = 0;
		lba++;
		sectors -= pages >> info->smallpageshift;
	}

	result = USB_STOR_TRANSPORT_GOOD;

leave:
	kfree(buffer);

	return result;
}

static int sddr55_write_data(struct us_data *us,
		unsigned int lba,
		unsigned int page,
		unsigned short sectors) {

	int result = USB_STOR_TRANSPORT_GOOD;
	unsigned char *command = us->iobuf;
	unsigned char *status = us->iobuf;
	struct sddr55_card_info *info = (struct sddr55_card_info *)us->extra;
	unsigned char *buffer;

	unsigned int pba;
	unsigned int new_pba;
	unsigned int address;

	unsigned short pages;
	int i;
	unsigned int len, offset;
	struct scatterlist *sg;

	/* check if we are allowed to write */
	if (info->read_only || info->force_read_only) {
		set_sense_info (7, 0x27, 0);	/* read only */
		return USB_STOR_TRANSPORT_FAILED;
	}

	// Since we only write one block at a time, we have to create
	// a bounce buffer and move the data a piece at a time between the
	// bounce buffer and the actual transfer buffer.

	len = min_t(unsigned int, sectors, info->blocksize >>
			info->smallpageshift) * PAGESIZE;
	buffer = kmalloc(len, GFP_NOIO);
	if (buffer == NULL)
		return USB_STOR_TRANSPORT_ERROR;
	offset = 0;
	sg = NULL;

	while (sectors > 0) {

		/* have we got to end? */
		if (lba >= info->max_log_blks)
			break;

		pba = info->lba_to_pba[lba];

		// Write as many sectors as possible in this block

		pages = min_t(unsigned int, sectors << info->smallpageshift,
				info->blocksize - page);
		len = pages << info->pageshift;

		// Get the data from the transfer buffer
		usb_stor_access_xfer_buf(buffer, len, us->srb,
				&sg, &offset, FROM_XFER_BUF);

		usb_stor_dbg(us, "Write %02X pages, to PBA %04X (LBA %04X) page %02X\n",
			     pages, pba, lba, page);
			
		command[4] = 0;

		if (pba == NOT_ALLOCATED) {
			/* no pba allocated for this lba, find a free pba to use */

			int max_pba = (info->max_log_blks / 250 ) * 256;
			int found_count = 0;
			int found_pba = -1;

			/* set pba to first block in zone lba is in */
			pba = (lba / 1000) * 1024;

			usb_stor_dbg(us, "No PBA for LBA %04X\n", lba);

			if (max_pba > 1024)
				max_pba = 1024;

			/*
			 * Scan through the map looking for an unused block
			 * leave 16 unused blocks at start (or as many as
			 * possible) since the sddr55 seems to reuse a used
			 * block when it shouldn't if we don't leave space.
			 */
			for (i = 0; i < max_pba; i++, pba++) {
				if (info->pba_to_lba[pba] == UNUSED_BLOCK) {
					found_pba = pba;
					if (found_count++ > 16)
						break;
				}
			}

			pba = found_pba;

			if (pba == -1) {
				/* oh dear */
				usb_stor_dbg(us, "Couldn't find unallocated block\n");

				set_sense_info (3, 0x31, 0);	/* medium error */
				result = USB_STOR_TRANSPORT_FAILED;
				goto leave;
			}

			usb_stor_dbg(us, "Allocating PBA %04X for LBA %04X\n",
				     pba, lba);

			/* set writing to unallocated block flag */
			command[4] = 0x40;
		}

		address = (pba << info->blockshift) + page;

		command[1] = LSB_of(address>>16);
		command[2] = LSB_of(address>>8); 
		command[3] = LSB_of(address);

		/* set the lba into the command, modulo 1000 */
		command[0] = LSB_of(lba % 1000);
		command[6] = MSB_of(lba % 1000);

		command[4] |= LSB_of(pages >> info->smallpageshift);
		command[5] = 0xB0;
		command[7] = 0x86;

		/* send command */
		result = sddr55_bulk_transport(us,
			DMA_TO_DEVICE, command, 8);

		if (result != USB_STOR_XFER_GOOD) {
			usb_stor_dbg(us, "Result for send_command in write_data %d\n",
				     result);

			/* set_sense_info is superfluous here? */
			set_sense_info (3, 0x3, 0);/* peripheral write error */
			result = USB_STOR_TRANSPORT_FAILED;
			goto leave;
		}

		/* send the data */
		result = sddr55_bulk_transport(us,
			DMA_TO_DEVICE, buffer, len);

		if (result != USB_STOR_XFER_GOOD) {
			usb_stor_dbg(us, "Result for send_data in write_data %d\n",
				     result);

			/* set_sense_info is superfluous here? */
			set_sense_info (3, 0x3, 0);/* peripheral write error */
			result = USB_STOR_TRANSPORT_FAILED;
			goto leave;
		}

		/* now read status */
		result = sddr55_bulk_transport(us, DMA_FROM_DEVICE, status, 6);

		if (result != USB_STOR_XFER_GOOD) {
			usb_stor_dbg(us, "Result for get_status in write_data %d\n",
				     result);

			/* set_sense_info is superfluous here? */
			set_sense_info (3, 0x3, 0);/* peripheral write error */
			result = USB_STOR_TRANSPORT_FAILED;
			goto leave;
		}

		new_pba = (status[3] + (status[4] << 8) + (status[5] << 16))
						  >> info->blockshift;

		/* check if device-reported new_pba is out of range */
		if (new_pba >= (info->capacity >> (info->blockshift + info->pageshift))) {
			result = USB_STOR_TRANSPORT_FAILED;
			goto leave;
		}

		/* check status for error */
		if (status[0] == 0xff && status[1] == 0x4) {
			info->pba_to_lba[new_pba] = BAD_BLOCK;

			set_sense_info (3, 0x0c, 0);
			result = USB_STOR_TRANSPORT_FAILED;
			goto leave;
		}

		usb_stor_dbg(us, "Updating maps for LBA %04X: old PBA %04X, new PBA %04X\n",
			     lba, pba, new_pba);

		/* update the lba<->pba maps, note new_pba might be the same as pba */
		info->lba_to_pba[lba] = new_pba;
		info->pba_to_lba[pba] = UNUSED_BLOCK;

		/* check that new_pba wasn't already being used */
		if (info->pba_to_lba[new_pba] != UNUSED_BLOCK) {
			printk(KERN_ERR "sddr55 error: new PBA %04X already in use for LBA %04X\n",
				new_pba, info->pba_to_lba[new_pba]);
			info->fatal_error = 1;
			set_sense_info (3, 0x31, 0);
			result = USB_STOR_TRANSPORT_FAILED;
			goto leave;
		}

		/* update the pba<->lba maps for new_pba */
		info->pba_to_lba[new_pba] = lba % 1000;

		page = 0;
		lba++;
		sectors -= pages >> info->smallpageshift;
	}
	result = USB_STOR_TRANSPORT_GOOD;

 leave:
	kfree(buffer);
	return result;
}

static int sddr55_read_deviceID(struct us_data *us,
		unsigned char *manufacturerID,
		unsigned char *deviceID) {

	int result;
	unsigned char *command = us->iobuf;
	unsigned char *content = us->iobuf;

	memset(command, 0, 8);
	command[5] = 0xB0;
	command[7] = 0x84;
	result = sddr55_bulk_transport(us, DMA_TO_DEVICE, command, 8);

	usb_stor_dbg(us, "Result of send_control for device ID is %d\n",
		     result);

	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	result = sddr55_bulk_transport(us,
		DMA_FROM_DEVICE, content, 4);

	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	*manufacturerID = content[0];
	*deviceID = content[1];

	if (content[0] != 0xff)	{
    		result = sddr55_bulk_transport(us,
			DMA_FROM_DEVICE, content, 2);
	}

	return USB_STOR_TRANSPORT_GOOD;
}


static int sddr55_reset(struct us_data *us)
{
	return 0;
}


static unsigned long sddr55_get_capacity(struct us_data *us) {

	unsigned char manufacturerID;
	unsigned char deviceID;
	int result;
	struct sddr55_card_info *info = (struct sddr55_card_info *)us->extra;

	usb_stor_dbg(us, "Reading capacity...\n");

	result = sddr55_read_deviceID(us,
		&manufacturerID,
		&deviceID);

	usb_stor_dbg(us, "Result of read_deviceID is %d\n", result);

	if (result != USB_STOR_XFER_GOOD)
		return 0;

	usb_stor_dbg(us, "Device ID = %02X\n", deviceID);
	usb_stor_dbg(us, "Manuf  ID = %02X\n", manufacturerID);

	info->pageshift = 9;
	info->smallpageshift = 0;
	info->blocksize = 16;
	info->blockshift = 4;
	info->blockmask = 15;

	switch (deviceID) {

	case 0x6e: // 1MB
	case 0xe8:
	case 0xec:
		info->pageshift = 8;
		info->smallpageshift = 1;
		return 0x00100000;

	case 0xea: // 2MB
	case 0x64:
		info->pageshift = 8;
		info->smallpageshift = 1;
		fallthrough;
	case 0x5d: // 5d is a ROM card with pagesize 512.
		return 0x00200000;

	case 0xe3: // 4MB
	case 0xe5:
	case 0x6b:
	case 0xd5:
		return 0x00400000;

	case 0xe6: // 8MB
	case 0xd6:
		return 0x00800000;

	case 0x73: // 16MB
		info->blocksize = 32;
		info->blockshift = 5;
		info->blockmask = 31;
		return 0x01000000;

	case 0x75: // 32MB
		info->blocksize = 32;
		info->blockshift = 5;
		info->blockmask = 31;
		return 0x02000000;

	case 0x76: // 64MB
		info->blocksize = 32;
		info->blockshift = 5;
		info->blockmask = 31;
		return 0x04000000;

	case 0x79: // 128MB
		info->blocksize = 32;
		info->blockshift = 5;
		info->blockmask = 31;
		return 0x08000000;

	default: // unknown
		return 0;

	}
}

static int sddr55_read_map(struct us_data *us) {

	struct sddr55_card_info *info = (struct sddr55_card_info *)(us->extra);
	int numblocks;
	unsigned char *buffer;
	unsigned char *command = us->iobuf;
	int i;
	unsigned short lba;
	unsigned short max_lba;
	int result;

	if (!info->capacity)
		return -1;

	numblocks = info->capacity >> (info->blockshift + info->pageshift);
	
	buffer = kmalloc_array(numblocks, 2, GFP_NOIO );
	
	if (!buffer)
		return -1;

	memset(command, 0, 8);
	command[5] = 0xB0;
	command[6] = numblocks * 2 / 256;
	command[7] = 0x8A;

	result = sddr55_bulk_transport(us, DMA_TO_DEVICE, command, 8);

	if ( result != USB_STOR_XFER_GOOD) {
		kfree (buffer);
		return -1;
	}

	result = sddr55_bulk_transport(us, DMA_FROM_DEVICE, buffer, numblocks * 2);

	if ( result != USB_STOR_XFER_GOOD) {
		kfree (buffer);
		return -1;
	}

	result = sddr55_bulk_transport(us, DMA_FROM_DEVICE, command, 2);

	if ( result != USB_STOR_XFER_GOOD) {
		kfree (buffer);
		return -1;
	}

	kfree(info->lba_to_pba);
	kfree(info->pba_to_lba);
	info->lba_to_pba = kmalloc_array(numblocks, sizeof(int), GFP_NOIO);
	info->pba_to_lba = kmalloc_array(numblocks, sizeof(int), GFP_NOIO);

	if (info->lba_to_pba == NULL || info->pba_to_lba == NULL) {
		kfree(info->lba_to_pba);
		kfree(info->pba_to_lba);
		info->lba_to_pba = NULL;
		info->pba_to_lba = NULL;
		kfree(buffer);
		return -1;
	}

	memset(info->lba_to_pba, 0xff, numblocks*sizeof(int));
	memset(info->pba_to_lba, 0xff, numblocks*sizeof(int));

	/* set maximum lba */
	max_lba = info->max_log_blks;
	if (max_lba > 1000)
		max_lba = 1000;

	/*
	 * Each block is 64 bytes of control data, so block i is located in
	 * scatterlist block i*64/128k = i*(2^6)*(2^-17) = i*(2^-11)
	 */

	for (i=0; i<numblocks; i++) {
		int zone = i / 1024;

		lba = short_pack(buffer[i * 2], buffer[i * 2 + 1]);

			/*
			 * Every 1024 physical blocks ("zone"), the LBA numbers
			 * go back to zero, but are within a higher
			 * block of LBA's. Also, there is a maximum of
			 * 1000 LBA's per zone. In other words, in PBA
			 * 1024-2047 you will find LBA 0-999 which are
			 * really LBA 1000-1999. Yes, this wastes 24
			 * physical blocks per zone. Go figure. 
			 * These devices can have blocks go bad, so there
			 * are 24 spare blocks to use when blocks do go bad.
			 */

			/*
			 * SDDR55 returns 0xffff for a bad block, and 0x400 for the 
			 * CIS block. (Is this true for cards 8MB or less??)
			 * Record these in the physical to logical map
			 */ 

		info->pba_to_lba[i] = lba;

		if (lba >= max_lba) {
			continue;
		}
		
		if (info->lba_to_pba[lba + zone * 1000] != NOT_ALLOCATED &&
		    !info->force_read_only) {
			printk(KERN_WARNING
			       "sddr55: map inconsistency at LBA %04X\n",
			       lba + zone * 1000);
			info->force_read_only = 1;
		}

		if (lba<0x10 || (lba>=0x3E0 && lba<0x3EF))
			usb_stor_dbg(us, "LBA %04X <-> PBA %04X\n", lba, i);

		info->lba_to_pba[lba + zone * 1000] = i;
	}

	kfree(buffer);
	return 0;
}


static void sddr55_card_info_destructor(void *extra) {
	struct sddr55_card_info *info = (struct sddr55_card_info *)extra;

	if (!extra)
		return;

	kfree(info->lba_to_pba);
	kfree(info->pba_to_lba);
}


/*
 * Transport for the Sandisk SDDR-55
 */
static int sddr55_transport(struct scsi_cmnd *srb, struct us_data *us)
{
	int result;
	static const unsigned char inquiry_response[8] = {
		0x00, 0x80, 0x00, 0x02, 0x1F, 0x00, 0x00, 0x00
	};
 	// write-protected for now, no block descriptor support
	static const unsigned char mode_page_01[20] = {
		0x0, 0x12, 0x00, 0x80, 0x0, 0x0, 0x0, 0x0,
		0x01, 0x0A,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};
	unsigned char *ptr = us->iobuf;
	unsigned long capacity;
	unsigned int lba;
	unsigned int pba;
	unsigned int page;
	unsigned short pages;
	struct sddr55_card_info *info;

	if (!us->extra) {
		us->extra = kzalloc(
			sizeof(struct sddr55_card_info), GFP_NOIO);
		if (!us->extra)
			return USB_STOR_TRANSPORT_ERROR;
		us->extra_destructor = sddr55_card_info_destructor;
	}

	info = (struct sddr55_card_info *)(us->extra);

	if (srb->cmnd[0] == REQUEST_SENSE) {
		usb_stor_dbg(us, "request sense %02x/%02x/%02x\n",
			     info->sense_data[2],
			     info->sense_data[12],
			     info->sense_data[13]);

		memcpy (ptr, info->sense_data, sizeof info->sense_data);
		ptr[0] = 0x70;
		ptr[7] = 11;
		usb_stor_set_xfer_buf (ptr, sizeof info->sense_data, srb);
		memset (info->sense_data, 0, sizeof info->sense_data);

		return USB_STOR_TRANSPORT_GOOD;
	}

	memset (info->sense_data, 0, sizeof info->sense_data);

	/*
	 * Dummy up a response for INQUIRY since SDDR55 doesn't
	 * respond to INQUIRY commands
	 */

	if (srb->cmnd[0] == INQUIRY) {
		memcpy(ptr, inquiry_response, 8);
		fill_inquiry_response(us, ptr, 36);
		return USB_STOR_TRANSPORT_GOOD;
	}

	/*
	 * only check card status if the map isn't allocated, ie no card seen yet
	 * or if it's been over half a second since we last accessed it
	 */
	if (info->lba_to_pba == NULL || time_after(jiffies, info->last_access + HZ/2)) {

		/* check to see if a card is fitted */
		result = sddr55_status (us);
		if (result) {
			result = sddr55_status (us);
			if (!result) {
			set_sense_info (6, 0x28, 0);	/* new media, set unit attention, not ready to ready */
			}
			return USB_STOR_TRANSPORT_FAILED;
		}
	}

	/*
	 * if we detected a problem with the map when writing,
	 * don't allow any more access
	 */
	if (info->fatal_error) {

		set_sense_info (3, 0x31, 0);
		return USB_STOR_TRANSPORT_FAILED;
	}

	if (srb->cmnd[0] == READ_CAPACITY) {

		capacity = sddr55_get_capacity(us);

		if (!capacity) {
			set_sense_info (3, 0x30, 0); /* incompatible medium */
			return USB_STOR_TRANSPORT_FAILED;
		}

		info->capacity = capacity;

		/*
		 * figure out the maximum logical block number, allowing for
		 * the fact that only 250 out of every 256 are used
		 */
		info->max_log_blks = ((info->capacity >> (info->pageshift + info->blockshift)) / 256) * 250;

		/*
		 * Last page in the card, adjust as we only use 250 out of
		 * every 256 pages
		 */
		capacity = (capacity / 256) * 250;

		capacity /= PAGESIZE;
		capacity--;

		((__be32 *) ptr)[0] = cpu_to_be32(capacity);
		((__be32 *) ptr)[1] = cpu_to_be32(PAGESIZE);
		usb_stor_set_xfer_buf(ptr, 8, srb);

		sddr55_read_map(us);

		return USB_STOR_TRANSPORT_GOOD;
	}

	if (srb->cmnd[0] == MODE_SENSE_10) {

		memcpy(ptr, mode_page_01, sizeof mode_page_01);
		ptr[3] = (info->read_only || info->force_read_only) ? 0x80 : 0;
		usb_stor_set_xfer_buf(ptr, sizeof(mode_page_01), srb);

		if ( (srb->cmnd[2] & 0x3F) == 0x01 ) {
			usb_stor_dbg(us, "Dummy up request for mode page 1\n");
			return USB_STOR_TRANSPORT_GOOD;

		} else if ( (srb->cmnd[2] & 0x3F) == 0x3F ) {
			usb_stor_dbg(us, "Dummy up request for all mode pages\n");
			return USB_STOR_TRANSPORT_GOOD;
		}

		set_sense_info (5, 0x24, 0);	/* invalid field in command */
		return USB_STOR_TRANSPORT_FAILED;
	}

	if (srb->cmnd[0] == ALLOW_MEDIUM_REMOVAL) {

		usb_stor_dbg(us, "%s medium removal. Not that I can do anything about it...\n",
			     (srb->cmnd[4]&0x03) ? "Prevent" : "Allow");

		return USB_STOR_TRANSPORT_GOOD;

	}

	if (srb->cmnd[0] == READ_10 || srb->cmnd[0] == WRITE_10) {

		page = short_pack(srb->cmnd[3], srb->cmnd[2]);
		page <<= 16;
		page |= short_pack(srb->cmnd[5], srb->cmnd[4]);
		pages = short_pack(srb->cmnd[8], srb->cmnd[7]);

		page <<= info->smallpageshift;

		// convert page to block and page-within-block

		lba = page >> info->blockshift;
		page = page & info->blockmask;

		// locate physical block corresponding to logical block

		if (lba >= info->max_log_blks) {

			usb_stor_dbg(us, "Error: Requested LBA %04X exceeds maximum block %04X\n",
				     lba, info->max_log_blks - 1);

			set_sense_info (5, 0x24, 0);	/* invalid field in command */

			return USB_STOR_TRANSPORT_FAILED;
		}

		pba = info->lba_to_pba[lba];

		if (srb->cmnd[0] == WRITE_10) {
			usb_stor_dbg(us, "WRITE_10: write block %04X (LBA %04X) page %01X pages %d\n",
				     pba, lba, page, pages);

			return sddr55_write_data(us, lba, page, pages);
		} else {
			usb_stor_dbg(us, "READ_10: read block %04X (LBA %04X) page %01X pages %d\n",
				     pba, lba, page, pages);

			return sddr55_read_data(us, lba, page, pages);
		}
	}


	if (srb->cmnd[0] == TEST_UNIT_READY) {
		return USB_STOR_TRANSPORT_GOOD;
	}

	if (srb->cmnd[0] == START_STOP) {
		return USB_STOR_TRANSPORT_GOOD;
	}

	set_sense_info (5, 0x20, 0);	/* illegal command */

	return USB_STOR_TRANSPORT_FAILED; // FIXME: sense buffer?
}

static struct scsi_host_template sddr55_host_template;

static int sddr55_probe(struct usb_interface *intf,
			 const struct usb_device_id *id)
{
	struct us_data *us;
	int result;

	result = usb_stor_probe1(&us, intf, id,
			(id - sddr55_usb_ids) + sddr55_unusual_dev_list,
			&sddr55_host_template);
	if (result)
		return result;

	us->transport_name = "SDDR55";
	us->transport = sddr55_transport;
	us->transport_reset = sddr55_reset;
	us->max_lun = 0;

	result = usb_stor_probe2(us);
	return result;
}

static struct usb_driver sddr55_driver = {
	.name =		DRV_NAME,
	.probe =	sddr55_probe,
	.disconnect =	usb_stor_disconnect,
	.suspend =	usb_stor_suspend,
	.resume =	usb_stor_resume,
	.reset_resume =	usb_stor_reset_resume,
	.pre_reset =	usb_stor_pre_reset,
	.post_reset =	usb_stor_post_reset,
	.id_table =	sddr55_usb_ids,
	.soft_unbind =	1,
	.no_dynamic_id = 1,
};

module_usb_stor_driver(sddr55_driver, sddr55_host_template, DRV_NAME);
