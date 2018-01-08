// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for Alauda-based card readers
 *
 * Current development and maintenance by:
 *   (c) 2005 Daniel Drake <dsd@gentoo.org>
 *
 * The 'Alauda' is a chip manufacturered by RATOC for OEM use.
 *
 * Alauda implements a vendor-specific command set to access two media reader
 * ports (XD, SmartMedia). This driver converts SCSI commands to the commands
 * which are accepted by these devices.
 *
 * The driver was developed through reverse-engineering, with the help of the
 * sddr09 driver which has many similarities, and with some help from the
 * (very old) vendor-supplied GPL sma03 driver.
 *
 * For protocol info, see http://alauda.sourceforge.net
 */

#include <linux/module.h>
#include <linux/slab.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>

#include "usb.h"
#include "transport.h"
#include "protocol.h"
#include "debug.h"
#include "scsiglue.h"

#define DRV_NAME "ums-alauda"

MODULE_DESCRIPTION("Driver for Alauda-based card readers");
MODULE_AUTHOR("Daniel Drake <dsd@gentoo.org>");
MODULE_LICENSE("GPL");

/*
 * Status bytes
 */
#define ALAUDA_STATUS_ERROR		0x01
#define ALAUDA_STATUS_READY		0x40

/*
 * Control opcodes (for request field)
 */
#define ALAUDA_GET_XD_MEDIA_STATUS	0x08
#define ALAUDA_GET_SM_MEDIA_STATUS	0x98
#define ALAUDA_ACK_XD_MEDIA_CHANGE	0x0a
#define ALAUDA_ACK_SM_MEDIA_CHANGE	0x9a
#define ALAUDA_GET_XD_MEDIA_SIG		0x86
#define ALAUDA_GET_SM_MEDIA_SIG		0x96

/*
 * Bulk command identity (byte 0)
 */
#define ALAUDA_BULK_CMD			0x40

/*
 * Bulk opcodes (byte 1)
 */
#define ALAUDA_BULK_GET_REDU_DATA	0x85
#define ALAUDA_BULK_READ_BLOCK		0x94
#define ALAUDA_BULK_ERASE_BLOCK		0xa3
#define ALAUDA_BULK_WRITE_BLOCK		0xb4
#define ALAUDA_BULK_GET_STATUS2		0xb7
#define ALAUDA_BULK_RESET_MEDIA		0xe0

/*
 * Port to operate on (byte 8)
 */
#define ALAUDA_PORT_XD			0x00
#define ALAUDA_PORT_SM			0x01

/*
 * LBA and PBA are unsigned ints. Special values.
 */
#define UNDEF    0xffff
#define SPARE    0xfffe
#define UNUSABLE 0xfffd

struct alauda_media_info {
	unsigned long capacity;		/* total media size in bytes */
	unsigned int pagesize;		/* page size in bytes */
	unsigned int blocksize;		/* number of pages per block */
	unsigned int uzonesize;		/* number of usable blocks per zone */
	unsigned int zonesize;		/* number of blocks per zone */
	unsigned int blockmask;		/* mask to get page from address */

	unsigned char pageshift;
	unsigned char blockshift;
	unsigned char zoneshift;

	u16 **lba_to_pba;		/* logical to physical block map */
	u16 **pba_to_lba;		/* physical to logical block map */
};

struct alauda_info {
	struct alauda_media_info port[2];
	int wr_ep;			/* endpoint to write data out of */

	unsigned char sense_key;
	unsigned long sense_asc;	/* additional sense code */
	unsigned long sense_ascq;	/* additional sense code qualifier */
};

#define short_pack(lsb,msb) ( ((u16)(lsb)) | ( ((u16)(msb))<<8 ) )
#define LSB_of(s) ((s)&0xFF)
#define MSB_of(s) ((s)>>8)

#define MEDIA_PORT(us) us->srb->device->lun
#define MEDIA_INFO(us) ((struct alauda_info *)us->extra)->port[MEDIA_PORT(us)]

#define PBA_LO(pba) ((pba & 0xF) << 5)
#define PBA_HI(pba) (pba >> 3)
#define PBA_ZONE(pba) (pba >> 11)

static int init_alauda(struct us_data *us);


/*
 * The table of devices
 */
#define UNUSUAL_DEV(id_vendor, id_product, bcdDeviceMin, bcdDeviceMax, \
		    vendorName, productName, useProtocol, useTransport, \
		    initFunction, flags) \
{ USB_DEVICE_VER(id_vendor, id_product, bcdDeviceMin, bcdDeviceMax), \
  .driver_info = (flags) }

static struct usb_device_id alauda_usb_ids[] = {
#	include "unusual_alauda.h"
	{ }		/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, alauda_usb_ids);

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

static struct us_unusual_dev alauda_unusual_dev_list[] = {
#	include "unusual_alauda.h"
	{ }		/* Terminating entry */
};

#undef UNUSUAL_DEV


/*
 * Media handling
 */

struct alauda_card_info {
	unsigned char id;		/* id byte */
	unsigned char chipshift;	/* 1<<cs bytes total capacity */
	unsigned char pageshift;	/* 1<<ps bytes in a page */
	unsigned char blockshift;	/* 1<<bs pages per block */
	unsigned char zoneshift;	/* 1<<zs blocks per zone */
};

static struct alauda_card_info alauda_card_ids[] = {
	/* NAND flash */
	{ 0x6e, 20, 8, 4, 8},	/* 1 MB */
	{ 0xe8, 20, 8, 4, 8},	/* 1 MB */
	{ 0xec, 20, 8, 4, 8},	/* 1 MB */
	{ 0x64, 21, 8, 4, 9}, 	/* 2 MB */
	{ 0xea, 21, 8, 4, 9},	/* 2 MB */
	{ 0x6b, 22, 9, 4, 9},	/* 4 MB */
	{ 0xe3, 22, 9, 4, 9},	/* 4 MB */
	{ 0xe5, 22, 9, 4, 9},	/* 4 MB */
	{ 0xe6, 23, 9, 4, 10},	/* 8 MB */
	{ 0x73, 24, 9, 5, 10},	/* 16 MB */
	{ 0x75, 25, 9, 5, 10},	/* 32 MB */
	{ 0x76, 26, 9, 5, 10},	/* 64 MB */
	{ 0x79, 27, 9, 5, 10},	/* 128 MB */
	{ 0x71, 28, 9, 5, 10},	/* 256 MB */

	/* MASK ROM */
	{ 0x5d, 21, 9, 4, 8},	/* 2 MB */
	{ 0xd5, 22, 9, 4, 9},	/* 4 MB */
	{ 0xd6, 23, 9, 4, 10},	/* 8 MB */
	{ 0x57, 24, 9, 4, 11},	/* 16 MB */
	{ 0x58, 25, 9, 4, 12},	/* 32 MB */
	{ 0,}
};

static struct alauda_card_info *alauda_card_find_id(unsigned char id)
{
	int i;

	for (i = 0; alauda_card_ids[i].id != 0; i++)
		if (alauda_card_ids[i].id == id)
			return &(alauda_card_ids[i]);
	return NULL;
}

/*
 * ECC computation.
 */

static unsigned char parity[256];
static unsigned char ecc2[256];

static void nand_init_ecc(void)
{
	int i, j, a;

	parity[0] = 0;
	for (i = 1; i < 256; i++)
		parity[i] = (parity[i&(i-1)] ^ 1);

	for (i = 0; i < 256; i++) {
		a = 0;
		for (j = 0; j < 8; j++) {
			if (i & (1<<j)) {
				if ((j & 1) == 0)
					a ^= 0x04;
				if ((j & 2) == 0)
					a ^= 0x10;
				if ((j & 4) == 0)
					a ^= 0x40;
			}
		}
		ecc2[i] = ~(a ^ (a<<1) ^ (parity[i] ? 0xa8 : 0));
	}
}

/* compute 3-byte ecc on 256 bytes */
static void nand_compute_ecc(unsigned char *data, unsigned char *ecc)
{
	int i, j, a;
	unsigned char par = 0, bit, bits[8] = {0};

	/* collect 16 checksum bits */
	for (i = 0; i < 256; i++) {
		par ^= data[i];
		bit = parity[data[i]];
		for (j = 0; j < 8; j++)
			if ((i & (1<<j)) == 0)
				bits[j] ^= bit;
	}

	/* put 4+4+4 = 12 bits in the ecc */
	a = (bits[3] << 6) + (bits[2] << 4) + (bits[1] << 2) + bits[0];
	ecc[0] = ~(a ^ (a<<1) ^ (parity[par] ? 0xaa : 0));

	a = (bits[7] << 6) + (bits[6] << 4) + (bits[5] << 2) + bits[4];
	ecc[1] = ~(a ^ (a<<1) ^ (parity[par] ? 0xaa : 0));

	ecc[2] = ecc2[par];
}

static int nand_compare_ecc(unsigned char *data, unsigned char *ecc)
{
	return (data[0] == ecc[0] && data[1] == ecc[1] && data[2] == ecc[2]);
}

static void nand_store_ecc(unsigned char *data, unsigned char *ecc)
{
	memcpy(data, ecc, 3);
}

/*
 * Alauda driver
 */

/*
 * Forget our PBA <---> LBA mappings for a particular port
 */
static void alauda_free_maps (struct alauda_media_info *media_info)
{
	unsigned int shift = media_info->zoneshift
		+ media_info->blockshift + media_info->pageshift;
	unsigned int num_zones = media_info->capacity >> shift;
	unsigned int i;

	if (media_info->lba_to_pba != NULL)
		for (i = 0; i < num_zones; i++) {
			kfree(media_info->lba_to_pba[i]);
			media_info->lba_to_pba[i] = NULL;
		}

	if (media_info->pba_to_lba != NULL)
		for (i = 0; i < num_zones; i++) {
			kfree(media_info->pba_to_lba[i]);
			media_info->pba_to_lba[i] = NULL;
		}
}

/*
 * Returns 2 bytes of status data
 * The first byte describes media status, and second byte describes door status
 */
static int alauda_get_media_status(struct us_data *us, unsigned char *data)
{
	int rc;
	unsigned char command;

	if (MEDIA_PORT(us) == ALAUDA_PORT_XD)
		command = ALAUDA_GET_XD_MEDIA_STATUS;
	else
		command = ALAUDA_GET_SM_MEDIA_STATUS;

	rc = usb_stor_ctrl_transfer(us, us->recv_ctrl_pipe,
		command, 0xc0, 0, 1, data, 2);

	usb_stor_dbg(us, "Media status %02X %02X\n", data[0], data[1]);

	return rc;
}

/*
 * Clears the "media was changed" bit so that we know when it changes again
 * in the future.
 */
static int alauda_ack_media(struct us_data *us)
{
	unsigned char command;

	if (MEDIA_PORT(us) == ALAUDA_PORT_XD)
		command = ALAUDA_ACK_XD_MEDIA_CHANGE;
	else
		command = ALAUDA_ACK_SM_MEDIA_CHANGE;

	return usb_stor_ctrl_transfer(us, us->send_ctrl_pipe,
		command, 0x40, 0, 1, NULL, 0);
}

/*
 * Retrieves a 4-byte media signature, which indicates manufacturer, capacity,
 * and some other details.
 */
static int alauda_get_media_signature(struct us_data *us, unsigned char *data)
{
	unsigned char command;

	if (MEDIA_PORT(us) == ALAUDA_PORT_XD)
		command = ALAUDA_GET_XD_MEDIA_SIG;
	else
		command = ALAUDA_GET_SM_MEDIA_SIG;

	return usb_stor_ctrl_transfer(us, us->recv_ctrl_pipe,
		command, 0xc0, 0, 0, data, 4);
}

/*
 * Resets the media status (but not the whole device?)
 */
static int alauda_reset_media(struct us_data *us)
{
	unsigned char *command = us->iobuf;

	memset(command, 0, 9);
	command[0] = ALAUDA_BULK_CMD;
	command[1] = ALAUDA_BULK_RESET_MEDIA;
	command[8] = MEDIA_PORT(us);

	return usb_stor_bulk_transfer_buf(us, us->send_bulk_pipe,
		command, 9, NULL);
}

/*
 * Examines the media and deduces capacity, etc.
 */
static int alauda_init_media(struct us_data *us)
{
	unsigned char *data = us->iobuf;
	int ready = 0;
	struct alauda_card_info *media_info;
	unsigned int num_zones;

	while (ready == 0) {
		msleep(20);

		if (alauda_get_media_status(us, data) != USB_STOR_XFER_GOOD)
			return USB_STOR_TRANSPORT_ERROR;

		if (data[0] & 0x10)
			ready = 1;
	}

	usb_stor_dbg(us, "We are ready for action!\n");

	if (alauda_ack_media(us) != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	msleep(10);

	if (alauda_get_media_status(us, data) != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	if (data[0] != 0x14) {
		usb_stor_dbg(us, "Media not ready after ack\n");
		return USB_STOR_TRANSPORT_ERROR;
	}

	if (alauda_get_media_signature(us, data) != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	usb_stor_dbg(us, "Media signature: %4ph\n", data);
	media_info = alauda_card_find_id(data[1]);
	if (media_info == NULL) {
		pr_warn("alauda_init_media: Unrecognised media signature: %4ph\n",
			data);
		return USB_STOR_TRANSPORT_ERROR;
	}

	MEDIA_INFO(us).capacity = 1 << media_info->chipshift;
	usb_stor_dbg(us, "Found media with capacity: %ldMB\n",
		     MEDIA_INFO(us).capacity >> 20);

	MEDIA_INFO(us).pageshift = media_info->pageshift;
	MEDIA_INFO(us).blockshift = media_info->blockshift;
	MEDIA_INFO(us).zoneshift = media_info->zoneshift;

	MEDIA_INFO(us).pagesize = 1 << media_info->pageshift;
	MEDIA_INFO(us).blocksize = 1 << media_info->blockshift;
	MEDIA_INFO(us).zonesize = 1 << media_info->zoneshift;

	MEDIA_INFO(us).uzonesize = ((1 << media_info->zoneshift) / 128) * 125;
	MEDIA_INFO(us).blockmask = MEDIA_INFO(us).blocksize - 1;

	num_zones = MEDIA_INFO(us).capacity >> (MEDIA_INFO(us).zoneshift
		+ MEDIA_INFO(us).blockshift + MEDIA_INFO(us).pageshift);
	MEDIA_INFO(us).pba_to_lba = kcalloc(num_zones, sizeof(u16*), GFP_NOIO);
	MEDIA_INFO(us).lba_to_pba = kcalloc(num_zones, sizeof(u16*), GFP_NOIO);

	if (alauda_reset_media(us) != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	return USB_STOR_TRANSPORT_GOOD;
}

/*
 * Examines the media status and does the right thing when the media has gone,
 * appeared, or changed.
 */
static int alauda_check_media(struct us_data *us)
{
	struct alauda_info *info = (struct alauda_info *) us->extra;
	unsigned char status[2];
	int rc;

	rc = alauda_get_media_status(us, status);

	/* Check for no media or door open */
	if ((status[0] & 0x80) || ((status[0] & 0x1F) == 0x10)
		|| ((status[1] & 0x01) == 0)) {
		usb_stor_dbg(us, "No media, or door open\n");
		alauda_free_maps(&MEDIA_INFO(us));
		info->sense_key = 0x02;
		info->sense_asc = 0x3A;
		info->sense_ascq = 0x00;
		return USB_STOR_TRANSPORT_FAILED;
	}

	/* Check for media change */
	if (status[0] & 0x08) {
		usb_stor_dbg(us, "Media change detected\n");
		alauda_free_maps(&MEDIA_INFO(us));
		alauda_init_media(us);

		info->sense_key = UNIT_ATTENTION;
		info->sense_asc = 0x28;
		info->sense_ascq = 0x00;
		return USB_STOR_TRANSPORT_FAILED;
	}

	return USB_STOR_TRANSPORT_GOOD;
}

/*
 * Checks the status from the 2nd status register
 * Returns 3 bytes of status data, only the first is known
 */
static int alauda_check_status2(struct us_data *us)
{
	int rc;
	unsigned char command[] = {
		ALAUDA_BULK_CMD, ALAUDA_BULK_GET_STATUS2,
		0, 0, 0, 0, 3, 0, MEDIA_PORT(us)
	};
	unsigned char data[3];

	rc = usb_stor_bulk_transfer_buf(us, us->send_bulk_pipe,
		command, 9, NULL);
	if (rc != USB_STOR_XFER_GOOD)
		return rc;

	rc = usb_stor_bulk_transfer_buf(us, us->recv_bulk_pipe,
		data, 3, NULL);
	if (rc != USB_STOR_XFER_GOOD)
		return rc;

	usb_stor_dbg(us, "%3ph\n", data);
	if (data[0] & ALAUDA_STATUS_ERROR)
		return USB_STOR_XFER_ERROR;

	return USB_STOR_XFER_GOOD;
}

/*
 * Gets the redundancy data for the first page of a PBA
 * Returns 16 bytes.
 */
static int alauda_get_redu_data(struct us_data *us, u16 pba, unsigned char *data)
{
	int rc;
	unsigned char command[] = {
		ALAUDA_BULK_CMD, ALAUDA_BULK_GET_REDU_DATA,
		PBA_HI(pba), PBA_ZONE(pba), 0, PBA_LO(pba), 0, 0, MEDIA_PORT(us)
	};

	rc = usb_stor_bulk_transfer_buf(us, us->send_bulk_pipe,
		command, 9, NULL);
	if (rc != USB_STOR_XFER_GOOD)
		return rc;

	return usb_stor_bulk_transfer_buf(us, us->recv_bulk_pipe,
		data, 16, NULL);
}

/*
 * Finds the first unused PBA in a zone
 * Returns the absolute PBA of an unused PBA, or 0 if none found.
 */
static u16 alauda_find_unused_pba(struct alauda_media_info *info,
	unsigned int zone)
{
	u16 *pba_to_lba = info->pba_to_lba[zone];
	unsigned int i;

	for (i = 0; i < info->zonesize; i++)
		if (pba_to_lba[i] == UNDEF)
			return (zone << info->zoneshift) + i;

	return 0;
}

/*
 * Reads the redundancy data for all PBA's in a zone
 * Produces lba <--> pba mappings
 */
static int alauda_read_map(struct us_data *us, unsigned int zone)
{
	unsigned char *data = us->iobuf;
	int result;
	int i, j;
	unsigned int zonesize = MEDIA_INFO(us).zonesize;
	unsigned int uzonesize = MEDIA_INFO(us).uzonesize;
	unsigned int lba_offset, lba_real, blocknum;
	unsigned int zone_base_lba = zone * uzonesize;
	unsigned int zone_base_pba = zone * zonesize;
	u16 *lba_to_pba = kcalloc(zonesize, sizeof(u16), GFP_NOIO);
	u16 *pba_to_lba = kcalloc(zonesize, sizeof(u16), GFP_NOIO);
	if (lba_to_pba == NULL || pba_to_lba == NULL) {
		result = USB_STOR_TRANSPORT_ERROR;
		goto error;
	}

	usb_stor_dbg(us, "Mapping blocks for zone %d\n", zone);

	/* 1024 PBA's per zone */
	for (i = 0; i < zonesize; i++)
		lba_to_pba[i] = pba_to_lba[i] = UNDEF;

	for (i = 0; i < zonesize; i++) {
		blocknum = zone_base_pba + i;

		result = alauda_get_redu_data(us, blocknum, data);
		if (result != USB_STOR_XFER_GOOD) {
			result = USB_STOR_TRANSPORT_ERROR;
			goto error;
		}

		/* special PBAs have control field 0^16 */
		for (j = 0; j < 16; j++)
			if (data[j] != 0)
				goto nonz;
		pba_to_lba[i] = UNUSABLE;
		usb_stor_dbg(us, "PBA %d has no logical mapping\n", blocknum);
		continue;

	nonz:
		/* unwritten PBAs have control field FF^16 */
		for (j = 0; j < 16; j++)
			if (data[j] != 0xff)
				goto nonff;
		continue;

	nonff:
		/* normal PBAs start with six FFs */
		if (j < 6) {
			usb_stor_dbg(us, "PBA %d has no logical mapping: reserved area = %02X%02X%02X%02X data status %02X block status %02X\n",
				     blocknum,
				     data[0], data[1], data[2], data[3],
				     data[4], data[5]);
			pba_to_lba[i] = UNUSABLE;
			continue;
		}

		if ((data[6] >> 4) != 0x01) {
			usb_stor_dbg(us, "PBA %d has invalid address field %02X%02X/%02X%02X\n",
				     blocknum, data[6], data[7],
				     data[11], data[12]);
			pba_to_lba[i] = UNUSABLE;
			continue;
		}

		/* check even parity */
		if (parity[data[6] ^ data[7]]) {
			printk(KERN_WARNING
			       "alauda_read_map: Bad parity in LBA for block %d"
			       " (%02X %02X)\n", i, data[6], data[7]);
			pba_to_lba[i] = UNUSABLE;
			continue;
		}

		lba_offset = short_pack(data[7], data[6]);
		lba_offset = (lba_offset & 0x07FF) >> 1;
		lba_real = lba_offset + zone_base_lba;

		/*
		 * Every 1024 physical blocks ("zone"), the LBA numbers
		 * go back to zero, but are within a higher block of LBA's.
		 * Also, there is a maximum of 1000 LBA's per zone.
		 * In other words, in PBA 1024-2047 you will find LBA 0-999
		 * which are really LBA 1000-1999. This allows for 24 bad
		 * or special physical blocks per zone.
		 */

		if (lba_offset >= uzonesize) {
			printk(KERN_WARNING
			       "alauda_read_map: Bad low LBA %d for block %d\n",
			       lba_real, blocknum);
			continue;
		}

		if (lba_to_pba[lba_offset] != UNDEF) {
			printk(KERN_WARNING
			       "alauda_read_map: "
			       "LBA %d seen for PBA %d and %d\n",
			       lba_real, lba_to_pba[lba_offset], blocknum);
			continue;
		}

		pba_to_lba[i] = lba_real;
		lba_to_pba[lba_offset] = blocknum;
		continue;
	}

	MEDIA_INFO(us).lba_to_pba[zone] = lba_to_pba;
	MEDIA_INFO(us).pba_to_lba[zone] = pba_to_lba;
	result = 0;
	goto out;

error:
	kfree(lba_to_pba);
	kfree(pba_to_lba);
out:
	return result;
}

/*
 * Checks to see whether we have already mapped a certain zone
 * If we haven't, the map is generated
 */
static void alauda_ensure_map_for_zone(struct us_data *us, unsigned int zone)
{
	if (MEDIA_INFO(us).lba_to_pba[zone] == NULL
		|| MEDIA_INFO(us).pba_to_lba[zone] == NULL)
		alauda_read_map(us, zone);
}

/*
 * Erases an entire block
 */
static int alauda_erase_block(struct us_data *us, u16 pba)
{
	int rc;
	unsigned char command[] = {
		ALAUDA_BULK_CMD, ALAUDA_BULK_ERASE_BLOCK, PBA_HI(pba),
		PBA_ZONE(pba), 0, PBA_LO(pba), 0x02, 0, MEDIA_PORT(us)
	};
	unsigned char buf[2];

	usb_stor_dbg(us, "Erasing PBA %d\n", pba);

	rc = usb_stor_bulk_transfer_buf(us, us->send_bulk_pipe,
		command, 9, NULL);
	if (rc != USB_STOR_XFER_GOOD)
		return rc;

	rc = usb_stor_bulk_transfer_buf(us, us->recv_bulk_pipe,
		buf, 2, NULL);
	if (rc != USB_STOR_XFER_GOOD)
		return rc;

	usb_stor_dbg(us, "Erase result: %02X %02X\n", buf[0], buf[1]);
	return rc;
}

/*
 * Reads data from a certain offset page inside a PBA, including interleaved
 * redundancy data. Returns (pagesize+64)*pages bytes in data.
 */
static int alauda_read_block_raw(struct us_data *us, u16 pba,
		unsigned int page, unsigned int pages, unsigned char *data)
{
	int rc;
	unsigned char command[] = {
		ALAUDA_BULK_CMD, ALAUDA_BULK_READ_BLOCK, PBA_HI(pba),
		PBA_ZONE(pba), 0, PBA_LO(pba) + page, pages, 0, MEDIA_PORT(us)
	};

	usb_stor_dbg(us, "pba %d page %d count %d\n", pba, page, pages);

	rc = usb_stor_bulk_transfer_buf(us, us->send_bulk_pipe,
		command, 9, NULL);
	if (rc != USB_STOR_XFER_GOOD)
		return rc;

	return usb_stor_bulk_transfer_buf(us, us->recv_bulk_pipe,
		data, (MEDIA_INFO(us).pagesize + 64) * pages, NULL);
}

/*
 * Reads data from a certain offset page inside a PBA, excluding redundancy
 * data. Returns pagesize*pages bytes in data. Note that data must be big enough
 * to hold (pagesize+64)*pages bytes of data, but you can ignore those 'extra'
 * trailing bytes outside this function.
 */
static int alauda_read_block(struct us_data *us, u16 pba,
		unsigned int page, unsigned int pages, unsigned char *data)
{
	int i, rc;
	unsigned int pagesize = MEDIA_INFO(us).pagesize;

	rc = alauda_read_block_raw(us, pba, page, pages, data);
	if (rc != USB_STOR_XFER_GOOD)
		return rc;

	/* Cut out the redundancy data */
	for (i = 0; i < pages; i++) {
		int dest_offset = i * pagesize;
		int src_offset = i * (pagesize + 64);
		memmove(data + dest_offset, data + src_offset, pagesize);
	}

	return rc;
}

/*
 * Writes an entire block of data and checks status after write.
 * Redundancy data must be already included in data. Data should be
 * (pagesize+64)*blocksize bytes in length.
 */
static int alauda_write_block(struct us_data *us, u16 pba, unsigned char *data)
{
	int rc;
	struct alauda_info *info = (struct alauda_info *) us->extra;
	unsigned char command[] = {
		ALAUDA_BULK_CMD, ALAUDA_BULK_WRITE_BLOCK, PBA_HI(pba),
		PBA_ZONE(pba), 0, PBA_LO(pba), 32, 0, MEDIA_PORT(us)
	};

	usb_stor_dbg(us, "pba %d\n", pba);

	rc = usb_stor_bulk_transfer_buf(us, us->send_bulk_pipe,
		command, 9, NULL);
	if (rc != USB_STOR_XFER_GOOD)
		return rc;

	rc = usb_stor_bulk_transfer_buf(us, info->wr_ep, data,
		(MEDIA_INFO(us).pagesize + 64) * MEDIA_INFO(us).blocksize,
		NULL);
	if (rc != USB_STOR_XFER_GOOD)
		return rc;

	return alauda_check_status2(us);
}

/*
 * Write some data to a specific LBA.
 */
static int alauda_write_lba(struct us_data *us, u16 lba,
		 unsigned int page, unsigned int pages,
		 unsigned char *ptr, unsigned char *blockbuffer)
{
	u16 pba, lbap, new_pba;
	unsigned char *bptr, *cptr, *xptr;
	unsigned char ecc[3];
	int i, result;
	unsigned int uzonesize = MEDIA_INFO(us).uzonesize;
	unsigned int zonesize = MEDIA_INFO(us).zonesize;
	unsigned int pagesize = MEDIA_INFO(us).pagesize;
	unsigned int blocksize = MEDIA_INFO(us).blocksize;
	unsigned int lba_offset = lba % uzonesize;
	unsigned int new_pba_offset;
	unsigned int zone = lba / uzonesize;

	alauda_ensure_map_for_zone(us, zone);

	pba = MEDIA_INFO(us).lba_to_pba[zone][lba_offset];
	if (pba == 1) {
		/*
		 * Maybe it is impossible to write to PBA 1.
		 * Fake success, but don't do anything.
		 */
		printk(KERN_WARNING
		       "alauda_write_lba: avoid writing to pba 1\n");
		return USB_STOR_TRANSPORT_GOOD;
	}

	new_pba = alauda_find_unused_pba(&MEDIA_INFO(us), zone);
	if (!new_pba) {
		printk(KERN_WARNING
		       "alauda_write_lba: Out of unused blocks\n");
		return USB_STOR_TRANSPORT_ERROR;
	}

	/* read old contents */
	if (pba != UNDEF) {
		result = alauda_read_block_raw(us, pba, 0,
			blocksize, blockbuffer);
		if (result != USB_STOR_XFER_GOOD)
			return result;
	} else {
		memset(blockbuffer, 0, blocksize * (pagesize + 64));
	}

	lbap = (lba_offset << 1) | 0x1000;
	if (parity[MSB_of(lbap) ^ LSB_of(lbap)])
		lbap ^= 1;

	/* check old contents and fill lba */
	for (i = 0; i < blocksize; i++) {
		bptr = blockbuffer + (i * (pagesize + 64));
		cptr = bptr + pagesize;
		nand_compute_ecc(bptr, ecc);
		if (!nand_compare_ecc(cptr+13, ecc)) {
			usb_stor_dbg(us, "Warning: bad ecc in page %d- of pba %d\n",
				     i, pba);
			nand_store_ecc(cptr+13, ecc);
		}
		nand_compute_ecc(bptr + (pagesize / 2), ecc);
		if (!nand_compare_ecc(cptr+8, ecc)) {
			usb_stor_dbg(us, "Warning: bad ecc in page %d+ of pba %d\n",
				     i, pba);
			nand_store_ecc(cptr+8, ecc);
		}
		cptr[6] = cptr[11] = MSB_of(lbap);
		cptr[7] = cptr[12] = LSB_of(lbap);
	}

	/* copy in new stuff and compute ECC */
	xptr = ptr;
	for (i = page; i < page+pages; i++) {
		bptr = blockbuffer + (i * (pagesize + 64));
		cptr = bptr + pagesize;
		memcpy(bptr, xptr, pagesize);
		xptr += pagesize;
		nand_compute_ecc(bptr, ecc);
		nand_store_ecc(cptr+13, ecc);
		nand_compute_ecc(bptr + (pagesize / 2), ecc);
		nand_store_ecc(cptr+8, ecc);
	}

	result = alauda_write_block(us, new_pba, blockbuffer);
	if (result != USB_STOR_XFER_GOOD)
		return result;

	new_pba_offset = new_pba - (zone * zonesize);
	MEDIA_INFO(us).pba_to_lba[zone][new_pba_offset] = lba;
	MEDIA_INFO(us).lba_to_pba[zone][lba_offset] = new_pba;
	usb_stor_dbg(us, "Remapped LBA %d to PBA %d\n", lba, new_pba);

	if (pba != UNDEF) {
		unsigned int pba_offset = pba - (zone * zonesize);
		result = alauda_erase_block(us, pba);
		if (result != USB_STOR_XFER_GOOD)
			return result;
		MEDIA_INFO(us).pba_to_lba[zone][pba_offset] = UNDEF;
	}

	return USB_STOR_TRANSPORT_GOOD;
}

/*
 * Read data from a specific sector address
 */
static int alauda_read_data(struct us_data *us, unsigned long address,
		unsigned int sectors)
{
	unsigned char *buffer;
	u16 lba, max_lba;
	unsigned int page, len, offset;
	unsigned int blockshift = MEDIA_INFO(us).blockshift;
	unsigned int pageshift = MEDIA_INFO(us).pageshift;
	unsigned int blocksize = MEDIA_INFO(us).blocksize;
	unsigned int pagesize = MEDIA_INFO(us).pagesize;
	unsigned int uzonesize = MEDIA_INFO(us).uzonesize;
	struct scatterlist *sg;
	int result;

	/*
	 * Since we only read in one block at a time, we have to create
	 * a bounce buffer and move the data a piece at a time between the
	 * bounce buffer and the actual transfer buffer.
	 * We make this buffer big enough to hold temporary redundancy data,
	 * which we use when reading the data blocks.
	 */

	len = min(sectors, blocksize) * (pagesize + 64);
	buffer = kmalloc(len, GFP_NOIO);
	if (!buffer)
		return USB_STOR_TRANSPORT_ERROR;

	/* Figure out the initial LBA and page */
	lba = address >> blockshift;
	page = (address & MEDIA_INFO(us).blockmask);
	max_lba = MEDIA_INFO(us).capacity >> (blockshift + pageshift);

	result = USB_STOR_TRANSPORT_GOOD;
	offset = 0;
	sg = NULL;

	while (sectors > 0) {
		unsigned int zone = lba / uzonesize; /* integer division */
		unsigned int lba_offset = lba - (zone * uzonesize);
		unsigned int pages;
		u16 pba;
		alauda_ensure_map_for_zone(us, zone);

		/* Not overflowing capacity? */
		if (lba >= max_lba) {
			usb_stor_dbg(us, "Error: Requested lba %u exceeds maximum %u\n",
				     lba, max_lba);
			result = USB_STOR_TRANSPORT_ERROR;
			break;
		}

		/* Find number of pages we can read in this block */
		pages = min(sectors, blocksize - page);
		len = pages << pageshift;

		/* Find where this lba lives on disk */
		pba = MEDIA_INFO(us).lba_to_pba[zone][lba_offset];

		if (pba == UNDEF) {	/* this lba was never written */
			usb_stor_dbg(us, "Read %d zero pages (LBA %d) page %d\n",
				     pages, lba, page);

			/*
			 * This is not really an error. It just means
			 * that the block has never been written.
			 * Instead of returning USB_STOR_TRANSPORT_ERROR
			 * it is better to return all zero data.
			 */

			memset(buffer, 0, len);
		} else {
			usb_stor_dbg(us, "Read %d pages, from PBA %d (LBA %d) page %d\n",
				     pages, pba, lba, page);

			result = alauda_read_block(us, pba, page, pages, buffer);
			if (result != USB_STOR_TRANSPORT_GOOD)
				break;
		}

		/* Store the data in the transfer buffer */
		usb_stor_access_xfer_buf(buffer, len, us->srb,
				&sg, &offset, TO_XFER_BUF);

		page = 0;
		lba++;
		sectors -= pages;
	}

	kfree(buffer);
	return result;
}

/*
 * Write data to a specific sector address
 */
static int alauda_write_data(struct us_data *us, unsigned long address,
		unsigned int sectors)
{
	unsigned char *buffer, *blockbuffer;
	unsigned int page, len, offset;
	unsigned int blockshift = MEDIA_INFO(us).blockshift;
	unsigned int pageshift = MEDIA_INFO(us).pageshift;
	unsigned int blocksize = MEDIA_INFO(us).blocksize;
	unsigned int pagesize = MEDIA_INFO(us).pagesize;
	struct scatterlist *sg;
	u16 lba, max_lba;
	int result;

	/*
	 * Since we don't write the user data directly to the device,
	 * we have to create a bounce buffer and move the data a piece
	 * at a time between the bounce buffer and the actual transfer buffer.
	 */

	len = min(sectors, blocksize) * pagesize;
	buffer = kmalloc(len, GFP_NOIO);
	if (!buffer)
		return USB_STOR_TRANSPORT_ERROR;

	/*
	 * We also need a temporary block buffer, where we read in the old data,
	 * overwrite parts with the new data, and manipulate the redundancy data
	 */
	blockbuffer = kmalloc((pagesize + 64) * blocksize, GFP_NOIO);
	if (!blockbuffer) {
		kfree(buffer);
		return USB_STOR_TRANSPORT_ERROR;
	}

	/* Figure out the initial LBA and page */
	lba = address >> blockshift;
	page = (address & MEDIA_INFO(us).blockmask);
	max_lba = MEDIA_INFO(us).capacity >> (pageshift + blockshift);

	result = USB_STOR_TRANSPORT_GOOD;
	offset = 0;
	sg = NULL;

	while (sectors > 0) {
		/* Write as many sectors as possible in this block */
		unsigned int pages = min(sectors, blocksize - page);
		len = pages << pageshift;

		/* Not overflowing capacity? */
		if (lba >= max_lba) {
			usb_stor_dbg(us, "Requested lba %u exceeds maximum %u\n",
				     lba, max_lba);
			result = USB_STOR_TRANSPORT_ERROR;
			break;
		}

		/* Get the data from the transfer buffer */
		usb_stor_access_xfer_buf(buffer, len, us->srb,
				&sg, &offset, FROM_XFER_BUF);

		result = alauda_write_lba(us, lba, page, pages, buffer,
			blockbuffer);
		if (result != USB_STOR_TRANSPORT_GOOD)
			break;

		page = 0;
		lba++;
		sectors -= pages;
	}

	kfree(buffer);
	kfree(blockbuffer);
	return result;
}

/*
 * Our interface with the rest of the world
 */

static void alauda_info_destructor(void *extra)
{
	struct alauda_info *info = (struct alauda_info *) extra;
	int port;

	if (!info)
		return;

	for (port = 0; port < 2; port++) {
		struct alauda_media_info *media_info = &info->port[port];

		alauda_free_maps(media_info);
		kfree(media_info->lba_to_pba);
		kfree(media_info->pba_to_lba);
	}
}

/*
 * Initialize alauda_info struct and find the data-write endpoint
 */
static int init_alauda(struct us_data *us)
{
	struct alauda_info *info;
	struct usb_host_interface *altsetting = us->pusb_intf->cur_altsetting;
	nand_init_ecc();

	us->extra = kzalloc(sizeof(struct alauda_info), GFP_NOIO);
	if (!us->extra)
		return USB_STOR_TRANSPORT_ERROR;

	info = (struct alauda_info *) us->extra;
	us->extra_destructor = alauda_info_destructor;

	info->wr_ep = usb_sndbulkpipe(us->pusb_dev,
		altsetting->endpoint[0].desc.bEndpointAddress
		& USB_ENDPOINT_NUMBER_MASK);

	return USB_STOR_TRANSPORT_GOOD;
}

static int alauda_transport(struct scsi_cmnd *srb, struct us_data *us)
{
	int rc;
	struct alauda_info *info = (struct alauda_info *) us->extra;
	unsigned char *ptr = us->iobuf;
	static unsigned char inquiry_response[36] = {
		0x00, 0x80, 0x00, 0x01, 0x1F, 0x00, 0x00, 0x00
	};

	if (srb->cmnd[0] == INQUIRY) {
		usb_stor_dbg(us, "INQUIRY - Returning bogus response\n");
		memcpy(ptr, inquiry_response, sizeof(inquiry_response));
		fill_inquiry_response(us, ptr, 36);
		return USB_STOR_TRANSPORT_GOOD;
	}

	if (srb->cmnd[0] == TEST_UNIT_READY) {
		usb_stor_dbg(us, "TEST_UNIT_READY\n");
		return alauda_check_media(us);
	}

	if (srb->cmnd[0] == READ_CAPACITY) {
		unsigned int num_zones;
		unsigned long capacity;

		rc = alauda_check_media(us);
		if (rc != USB_STOR_TRANSPORT_GOOD)
			return rc;

		num_zones = MEDIA_INFO(us).capacity >> (MEDIA_INFO(us).zoneshift
			+ MEDIA_INFO(us).blockshift + MEDIA_INFO(us).pageshift);

		capacity = num_zones * MEDIA_INFO(us).uzonesize
			* MEDIA_INFO(us).blocksize;

		/* Report capacity and page size */
		((__be32 *) ptr)[0] = cpu_to_be32(capacity - 1);
		((__be32 *) ptr)[1] = cpu_to_be32(512);

		usb_stor_set_xfer_buf(ptr, 8, srb);
		return USB_STOR_TRANSPORT_GOOD;
	}

	if (srb->cmnd[0] == READ_10) {
		unsigned int page, pages;

		rc = alauda_check_media(us);
		if (rc != USB_STOR_TRANSPORT_GOOD)
			return rc;

		page = short_pack(srb->cmnd[3], srb->cmnd[2]);
		page <<= 16;
		page |= short_pack(srb->cmnd[5], srb->cmnd[4]);
		pages = short_pack(srb->cmnd[8], srb->cmnd[7]);

		usb_stor_dbg(us, "READ_10: page %d pagect %d\n", page, pages);

		return alauda_read_data(us, page, pages);
	}

	if (srb->cmnd[0] == WRITE_10) {
		unsigned int page, pages;

		rc = alauda_check_media(us);
		if (rc != USB_STOR_TRANSPORT_GOOD)
			return rc;

		page = short_pack(srb->cmnd[3], srb->cmnd[2]);
		page <<= 16;
		page |= short_pack(srb->cmnd[5], srb->cmnd[4]);
		pages = short_pack(srb->cmnd[8], srb->cmnd[7]);

		usb_stor_dbg(us, "WRITE_10: page %d pagect %d\n", page, pages);

		return alauda_write_data(us, page, pages);
	}

	if (srb->cmnd[0] == REQUEST_SENSE) {
		usb_stor_dbg(us, "REQUEST_SENSE\n");

		memset(ptr, 0, 18);
		ptr[0] = 0xF0;
		ptr[2] = info->sense_key;
		ptr[7] = 11;
		ptr[12] = info->sense_asc;
		ptr[13] = info->sense_ascq;
		usb_stor_set_xfer_buf(ptr, 18, srb);

		return USB_STOR_TRANSPORT_GOOD;
	}

	if (srb->cmnd[0] == ALLOW_MEDIUM_REMOVAL) {
		/*
		 * sure.  whatever.  not like we can stop the user from popping
		 * the media out of the device (no locking doors, etc)
		 */
		return USB_STOR_TRANSPORT_GOOD;
	}

	usb_stor_dbg(us, "Gah! Unknown command: %d (0x%x)\n",
		     srb->cmnd[0], srb->cmnd[0]);
	info->sense_key = 0x05;
	info->sense_asc = 0x20;
	info->sense_ascq = 0x00;
	return USB_STOR_TRANSPORT_FAILED;
}

static struct scsi_host_template alauda_host_template;

static int alauda_probe(struct usb_interface *intf,
			 const struct usb_device_id *id)
{
	struct us_data *us;
	int result;

	result = usb_stor_probe1(&us, intf, id,
			(id - alauda_usb_ids) + alauda_unusual_dev_list,
			&alauda_host_template);
	if (result)
		return result;

	us->transport_name  = "Alauda Control/Bulk";
	us->transport = alauda_transport;
	us->transport_reset = usb_stor_Bulk_reset;
	us->max_lun = 1;

	result = usb_stor_probe2(us);
	return result;
}

static struct usb_driver alauda_driver = {
	.name =		DRV_NAME,
	.probe =	alauda_probe,
	.disconnect =	usb_stor_disconnect,
	.suspend =	usb_stor_suspend,
	.resume =	usb_stor_resume,
	.reset_resume =	usb_stor_reset_resume,
	.pre_reset =	usb_stor_pre_reset,
	.post_reset =	usb_stor_post_reset,
	.id_table =	alauda_usb_ids,
	.soft_unbind =	1,
	.no_dynamic_id = 1,
};

module_usb_stor_driver(alauda_driver, alauda_host_template, DRV_NAME);
