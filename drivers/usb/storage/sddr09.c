/* Driver for SanDisk SDDR-09 SmartMedia reader
 *
 *   (c) 2000, 2001 Robert Baruch (autophile@starband.net)
 *   (c) 2002 Andries Brouwer (aeb@cwi.nl)
 * Developed with the assistance of:
 *   (c) 2002 Alan Stern <stern@rowland.org>
 *
 * The SanDisk SDDR-09 SmartMedia reader uses the Shuttle EUSB-01 chip.
 * This chip is a programmable USB controller. In the SDDR-09, it has
 * been programmed to obey a certain limited set of SCSI commands.
 * This driver translates the "real" SCSI commands to the SDDR-09 SCSI
 * commands.
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
 * Known vendor commands: 12 bytes, first byte is opcode
 *
 * E7: read scatter gather
 * E8: read
 * E9: write
 * EA: erase
 * EB: reset
 * EC: read status
 * ED: read ID
 * EE: write CIS (?)
 * EF: compute checksum (?)
 */

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/slab.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>

#include "usb.h"
#include "transport.h"
#include "protocol.h"
#include "debug.h"

MODULE_DESCRIPTION("Driver for SanDisk SDDR-09 SmartMedia reader");
MODULE_AUTHOR("Andries Brouwer <aeb@cwi.nl>, Robert Baruch <autophile@starband.net>");
MODULE_LICENSE("GPL");

static int usb_stor_sddr09_dpcm_init(struct us_data *us);
static int sddr09_transport(struct scsi_cmnd *srb, struct us_data *us);
static int usb_stor_sddr09_init(struct us_data *us);


/*
 * The table of devices
 */
#define UNUSUAL_DEV(id_vendor, id_product, bcdDeviceMin, bcdDeviceMax, \
		    vendorName, productName, useProtocol, useTransport, \
		    initFunction, flags) \
{ USB_DEVICE_VER(id_vendor, id_product, bcdDeviceMin, bcdDeviceMax), \
  .driver_info = (flags)|(USB_US_TYPE_STOR<<24) }

struct usb_device_id sddr09_usb_ids[] = {
#	include "unusual_sddr09.h"
	{ }		/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, sddr09_usb_ids);

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

static struct us_unusual_dev sddr09_unusual_dev_list[] = {
#	include "unusual_sddr09.h"
	{ }		/* Terminating entry */
};

#undef UNUSUAL_DEV


#define short_pack(lsb,msb) ( ((u16)(lsb)) | ( ((u16)(msb))<<8 ) )
#define LSB_of(s) ((s)&0xFF)
#define MSB_of(s) ((s)>>8)

/* #define US_DEBUGP printk */

/*
 * First some stuff that does not belong here:
 * data on SmartMedia and other cards, completely
 * unrelated to this driver.
 * Similar stuff occurs in <linux/mtd/nand_ids.h>.
 */

struct nand_flash_dev {
	int model_id;
	int chipshift;		/* 1<<cs bytes total capacity */
	char pageshift;		/* 1<<ps bytes in a page */
	char blockshift;	/* 1<<bs pages in an erase block */
	char zoneshift;		/* 1<<zs blocks in a zone */
				/* # of logical blocks is 125/128 of this */
	char pageadrlen;	/* length of an address in bytes - 1 */
};

/*
 * NAND Flash Manufacturer ID Codes
 */
#define NAND_MFR_AMD		0x01
#define NAND_MFR_NATSEMI	0x8f
#define NAND_MFR_TOSHIBA	0x98
#define NAND_MFR_SAMSUNG	0xec

static inline char *nand_flash_manufacturer(int manuf_id) {
	switch(manuf_id) {
	case NAND_MFR_AMD:
		return "AMD";
	case NAND_MFR_NATSEMI:
		return "NATSEMI";
	case NAND_MFR_TOSHIBA:
		return "Toshiba";
	case NAND_MFR_SAMSUNG:
		return "Samsung";
	default:
		return "unknown";
	}
}

/*
 * It looks like it is unnecessary to attach manufacturer to the
 * remaining data: SSFDC prescribes manufacturer-independent id codes.
 *
 * 256 MB NAND flash has a 5-byte ID with 2nd byte 0xaa, 0xba, 0xca or 0xda.
 */

static struct nand_flash_dev nand_flash_ids[] = {
	/* NAND flash */
	{ 0x6e, 20, 8, 4, 8, 2},	/* 1 MB */
	{ 0xe8, 20, 8, 4, 8, 2},	/* 1 MB */
	{ 0xec, 20, 8, 4, 8, 2},	/* 1 MB */
	{ 0x64, 21, 8, 4, 9, 2}, 	/* 2 MB */
	{ 0xea, 21, 8, 4, 9, 2},	/* 2 MB */
	{ 0x6b, 22, 9, 4, 9, 2},	/* 4 MB */
	{ 0xe3, 22, 9, 4, 9, 2},	/* 4 MB */
	{ 0xe5, 22, 9, 4, 9, 2},	/* 4 MB */
	{ 0xe6, 23, 9, 4, 10, 2},	/* 8 MB */
	{ 0x73, 24, 9, 5, 10, 2},	/* 16 MB */
	{ 0x75, 25, 9, 5, 10, 2},	/* 32 MB */
	{ 0x76, 26, 9, 5, 10, 3},	/* 64 MB */
	{ 0x79, 27, 9, 5, 10, 3},	/* 128 MB */

	/* MASK ROM */
	{ 0x5d, 21, 9, 4, 8, 2},	/* 2 MB */
	{ 0xd5, 22, 9, 4, 9, 2},	/* 4 MB */
	{ 0xd6, 23, 9, 4, 10, 2},	/* 8 MB */
	{ 0x57, 24, 9, 4, 11, 2},	/* 16 MB */
	{ 0x58, 25, 9, 4, 12, 2},	/* 32 MB */
	{ 0,}
};

static struct nand_flash_dev *
nand_find_id(unsigned char id) {
	int i;

	for (i = 0; i < ARRAY_SIZE(nand_flash_ids); i++)
		if (nand_flash_ids[i].model_id == id)
			return &(nand_flash_ids[i]);
	return NULL;
}

/*
 * ECC computation.
 */
static unsigned char parity[256];
static unsigned char ecc2[256];

static void nand_init_ecc(void) {
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
static void nand_compute_ecc(unsigned char *data, unsigned char *ecc) {
	int i, j, a;
	unsigned char par, bit, bits[8];

	par = 0;
	for (j = 0; j < 8; j++)
		bits[j] = 0;

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

static int nand_compare_ecc(unsigned char *data, unsigned char *ecc) {
	return (data[0] == ecc[0] && data[1] == ecc[1] && data[2] == ecc[2]);
}

static void nand_store_ecc(unsigned char *data, unsigned char *ecc) {
	memcpy(data, ecc, 3);
}

/*
 * The actual driver starts here.
 */

struct sddr09_card_info {
	unsigned long	capacity;	/* Size of card in bytes */
	int		pagesize;	/* Size of page in bytes */
	int		pageshift;	/* log2 of pagesize */
	int		blocksize;	/* Size of block in pages */
	int		blockshift;	/* log2 of blocksize */
	int		blockmask;	/* 2^blockshift - 1 */
	int		*lba_to_pba;	/* logical to physical map */
	int		*pba_to_lba;	/* physical to logical map */
	int		lbact;		/* number of available pages */
	int		flags;
#define	SDDR09_WP	1		/* write protected */
};

/*
 * On my 16MB card, control blocks have size 64 (16 real control bytes,
 * and 48 junk bytes). In reality of course the card uses 16 control bytes,
 * so the reader makes up the remaining 48. Don't know whether these numbers
 * depend on the card. For now a constant.
 */
#define CONTROL_SHIFT 6

/*
 * On my Combo CF/SM reader, the SM reader has LUN 1.
 * (and things fail with LUN 0).
 * It seems LUN is irrelevant for others.
 */
#define LUN	1
#define	LUNBITS	(LUN << 5)

/*
 * LBA and PBA are unsigned ints. Special values.
 */
#define UNDEF    0xffffffff
#define SPARE    0xfffffffe
#define UNUSABLE 0xfffffffd

static const int erase_bad_lba_entries = 0;

/* send vendor interface command (0x41) */
/* called for requests 0, 1, 8 */
static int
sddr09_send_command(struct us_data *us,
		    unsigned char request,
		    unsigned char direction,
		    unsigned char *xfer_data,
		    unsigned int xfer_len) {
	unsigned int pipe;
	unsigned char requesttype = (0x41 | direction);
	int rc;

	// Get the receive or send control pipe number

	if (direction == USB_DIR_IN)
		pipe = us->recv_ctrl_pipe;
	else
		pipe = us->send_ctrl_pipe;

	rc = usb_stor_ctrl_transfer(us, pipe, request, requesttype,
				   0, 0, xfer_data, xfer_len);
	switch (rc) {
		case USB_STOR_XFER_GOOD:	return 0;
		case USB_STOR_XFER_STALLED:	return -EPIPE;
		default:			return -EIO;
	}
}

static int
sddr09_send_scsi_command(struct us_data *us,
			 unsigned char *command,
			 unsigned int command_len) {
	return sddr09_send_command(us, 0, USB_DIR_OUT, command, command_len);
}

#if 0
/*
 * Test Unit Ready Command: 12 bytes.
 * byte 0: opcode: 00
 */
static int
sddr09_test_unit_ready(struct us_data *us) {
	unsigned char *command = us->iobuf;
	int result;

	memset(command, 0, 6);
	command[1] = LUNBITS;

	result = sddr09_send_scsi_command(us, command, 6);

	US_DEBUGP("sddr09_test_unit_ready returns %d\n", result);

	return result;
}
#endif

/*
 * Request Sense Command: 12 bytes.
 * byte 0: opcode: 03
 * byte 4: data length
 */
static int
sddr09_request_sense(struct us_data *us, unsigned char *sensebuf, int buflen) {
	unsigned char *command = us->iobuf;
	int result;

	memset(command, 0, 12);
	command[0] = 0x03;
	command[1] = LUNBITS;
	command[4] = buflen;

	result = sddr09_send_scsi_command(us, command, 12);
	if (result)
		return result;

	result = usb_stor_bulk_transfer_buf(us, us->recv_bulk_pipe,
			sensebuf, buflen, NULL);
	return (result == USB_STOR_XFER_GOOD ? 0 : -EIO);
}

/*
 * Read Command: 12 bytes.
 * byte 0: opcode: E8
 * byte 1: last two bits: 00: read data, 01: read blockwise control,
 *			10: read both, 11: read pagewise control.
 *	 It turns out we need values 20, 21, 22, 23 here (LUN 1).
 * bytes 2-5: address (interpretation depends on byte 1, see below)
 * bytes 10-11: count (idem)
 *
 * A page has 512 data bytes and 64 control bytes (16 control and 48 junk).
 * A read data command gets data in 512-byte pages.
 * A read control command gets control in 64-byte chunks.
 * A read both command gets data+control in 576-byte chunks.
 *
 * Blocks are groups of 32 pages, and read blockwise control jumps to the
 * next block, while read pagewise control jumps to the next page after
 * reading a group of 64 control bytes.
 * [Here 512 = 1<<pageshift, 32 = 1<<blockshift, 64 is constant?]
 *
 * (1 MB and 2 MB cards are a bit different, but I have only a 16 MB card.)
 */

static int
sddr09_readX(struct us_data *us, int x, unsigned long fromaddress,
	     int nr_of_pages, int bulklen, unsigned char *buf,
	     int use_sg) {

	unsigned char *command = us->iobuf;
	int result;

	command[0] = 0xE8;
	command[1] = LUNBITS | x;
	command[2] = MSB_of(fromaddress>>16);
	command[3] = LSB_of(fromaddress>>16); 
	command[4] = MSB_of(fromaddress & 0xFFFF);
	command[5] = LSB_of(fromaddress & 0xFFFF); 
	command[6] = 0;
	command[7] = 0;
	command[8] = 0;
	command[9] = 0;
	command[10] = MSB_of(nr_of_pages);
	command[11] = LSB_of(nr_of_pages);

	result = sddr09_send_scsi_command(us, command, 12);

	if (result) {
		US_DEBUGP("Result for send_control in sddr09_read2%d %d\n",
			  x, result);
		return result;
	}

	result = usb_stor_bulk_transfer_sg(us, us->recv_bulk_pipe,
				       buf, bulklen, use_sg, NULL);

	if (result != USB_STOR_XFER_GOOD) {
		US_DEBUGP("Result for bulk_transfer in sddr09_read2%d %d\n",
			  x, result);
		return -EIO;
	}
	return 0;
}

/*
 * Read Data
 *
 * fromaddress counts data shorts:
 * increasing it by 256 shifts the bytestream by 512 bytes;
 * the last 8 bits are ignored.
 *
 * nr_of_pages counts pages of size (1 << pageshift).
 */
static int
sddr09_read20(struct us_data *us, unsigned long fromaddress,
	      int nr_of_pages, int pageshift, unsigned char *buf, int use_sg) {
	int bulklen = nr_of_pages << pageshift;

	/* The last 8 bits of fromaddress are ignored. */
	return sddr09_readX(us, 0, fromaddress, nr_of_pages, bulklen,
			    buf, use_sg);
}

/*
 * Read Blockwise Control
 *
 * fromaddress gives the starting position (as in read data;
 * the last 8 bits are ignored); increasing it by 32*256 shifts
 * the output stream by 64 bytes.
 *
 * count counts control groups of size (1 << controlshift).
 * For me, controlshift = 6. Is this constant?
 *
 * After getting one control group, jump to the next block
 * (fromaddress += 8192).
 */
static int
sddr09_read21(struct us_data *us, unsigned long fromaddress,
	      int count, int controlshift, unsigned char *buf, int use_sg) {

	int bulklen = (count << controlshift);
	return sddr09_readX(us, 1, fromaddress, count, bulklen,
			    buf, use_sg);
}

/*
 * Read both Data and Control
 *
 * fromaddress counts data shorts, ignoring control:
 * increasing it by 256 shifts the bytestream by 576 = 512+64 bytes;
 * the last 8 bits are ignored.
 *
 * nr_of_pages counts pages of size (1 << pageshift) + (1 << controlshift).
 */
static int
sddr09_read22(struct us_data *us, unsigned long fromaddress,
	      int nr_of_pages, int pageshift, unsigned char *buf, int use_sg) {

	int bulklen = (nr_of_pages << pageshift) + (nr_of_pages << CONTROL_SHIFT);
	US_DEBUGP("sddr09_read22: reading %d pages, %d bytes\n",
		  nr_of_pages, bulklen);
	return sddr09_readX(us, 2, fromaddress, nr_of_pages, bulklen,
			    buf, use_sg);
}

#if 0
/*
 * Read Pagewise Control
 *
 * fromaddress gives the starting position (as in read data;
 * the last 8 bits are ignored); increasing it by 256 shifts
 * the output stream by 64 bytes.
 *
 * count counts control groups of size (1 << controlshift).
 * For me, controlshift = 6. Is this constant?
 *
 * After getting one control group, jump to the next page
 * (fromaddress += 256).
 */
static int
sddr09_read23(struct us_data *us, unsigned long fromaddress,
	      int count, int controlshift, unsigned char *buf, int use_sg) {

	int bulklen = (count << controlshift);
	return sddr09_readX(us, 3, fromaddress, count, bulklen,
			    buf, use_sg);
}
#endif

/*
 * Erase Command: 12 bytes.
 * byte 0: opcode: EA
 * bytes 6-9: erase address (big-endian, counting shorts, sector aligned).
 * 
 * Always precisely one block is erased; bytes 2-5 and 10-11 are ignored.
 * The byte address being erased is 2*Eaddress.
 * The CIS cannot be erased.
 */
static int
sddr09_erase(struct us_data *us, unsigned long Eaddress) {
	unsigned char *command = us->iobuf;
	int result;

	US_DEBUGP("sddr09_erase: erase address %lu\n", Eaddress);

	memset(command, 0, 12);
	command[0] = 0xEA;
	command[1] = LUNBITS;
	command[6] = MSB_of(Eaddress>>16);
	command[7] = LSB_of(Eaddress>>16);
	command[8] = MSB_of(Eaddress & 0xFFFF);
	command[9] = LSB_of(Eaddress & 0xFFFF);

	result = sddr09_send_scsi_command(us, command, 12);

	if (result)
		US_DEBUGP("Result for send_control in sddr09_erase %d\n",
			  result);

	return result;
}

/*
 * Write CIS Command: 12 bytes.
 * byte 0: opcode: EE
 * bytes 2-5: write address in shorts
 * bytes 10-11: sector count
 *
 * This writes at the indicated address. Don't know how it differs
 * from E9. Maybe it does not erase? However, it will also write to
 * the CIS.
 *
 * When two such commands on the same page follow each other directly,
 * the second one is not done.
 */

/*
 * Write Command: 12 bytes.
 * byte 0: opcode: E9
 * bytes 2-5: write address (big-endian, counting shorts, sector aligned).
 * bytes 6-9: erase address (big-endian, counting shorts, sector aligned).
 * bytes 10-11: sector count (big-endian, in 512-byte sectors).
 *
 * If write address equals erase address, the erase is done first,
 * otherwise the write is done first. When erase address equals zero
 * no erase is done?
 */
static int
sddr09_writeX(struct us_data *us,
	      unsigned long Waddress, unsigned long Eaddress,
	      int nr_of_pages, int bulklen, unsigned char *buf, int use_sg) {

	unsigned char *command = us->iobuf;
	int result;

	command[0] = 0xE9;
	command[1] = LUNBITS;

	command[2] = MSB_of(Waddress>>16);
	command[3] = LSB_of(Waddress>>16);
	command[4] = MSB_of(Waddress & 0xFFFF);
	command[5] = LSB_of(Waddress & 0xFFFF);

	command[6] = MSB_of(Eaddress>>16);
	command[7] = LSB_of(Eaddress>>16);
	command[8] = MSB_of(Eaddress & 0xFFFF);
	command[9] = LSB_of(Eaddress & 0xFFFF);

	command[10] = MSB_of(nr_of_pages);
	command[11] = LSB_of(nr_of_pages);

	result = sddr09_send_scsi_command(us, command, 12);

	if (result) {
		US_DEBUGP("Result for send_control in sddr09_writeX %d\n",
			  result);
		return result;
	}

	result = usb_stor_bulk_transfer_sg(us, us->send_bulk_pipe,
				       buf, bulklen, use_sg, NULL);

	if (result != USB_STOR_XFER_GOOD) {
		US_DEBUGP("Result for bulk_transfer in sddr09_writeX %d\n",
			  result);
		return -EIO;
	}
	return 0;
}

/* erase address, write same address */
static int
sddr09_write_inplace(struct us_data *us, unsigned long address,
		     int nr_of_pages, int pageshift, unsigned char *buf,
		     int use_sg) {
	int bulklen = (nr_of_pages << pageshift) + (nr_of_pages << CONTROL_SHIFT);
	return sddr09_writeX(us, address, address, nr_of_pages, bulklen,
			     buf, use_sg);
}

#if 0
/*
 * Read Scatter Gather Command: 3+4n bytes.
 * byte 0: opcode E7
 * byte 2: n
 * bytes 4i-1,4i,4i+1: page address
 * byte 4i+2: page count
 * (i=1..n)
 *
 * This reads several pages from the card to a single memory buffer.
 * The last two bits of byte 1 have the same meaning as for E8.
 */
static int
sddr09_read_sg_test_only(struct us_data *us) {
	unsigned char *command = us->iobuf;
	int result, bulklen, nsg, ct;
	unsigned char *buf;
	unsigned long address;

	nsg = bulklen = 0;
	command[0] = 0xE7;
	command[1] = LUNBITS;
	command[2] = 0;
	address = 040000; ct = 1;
	nsg++;
	bulklen += (ct << 9);
	command[4*nsg+2] = ct;
	command[4*nsg+1] = ((address >> 9) & 0xFF);
	command[4*nsg+0] = ((address >> 17) & 0xFF);
	command[4*nsg-1] = ((address >> 25) & 0xFF);

	address = 0340000; ct = 1;
	nsg++;
	bulklen += (ct << 9);
	command[4*nsg+2] = ct;
	command[4*nsg+1] = ((address >> 9) & 0xFF);
	command[4*nsg+0] = ((address >> 17) & 0xFF);
	command[4*nsg-1] = ((address >> 25) & 0xFF);

	address = 01000000; ct = 2;
	nsg++;
	bulklen += (ct << 9);
	command[4*nsg+2] = ct;
	command[4*nsg+1] = ((address >> 9) & 0xFF);
	command[4*nsg+0] = ((address >> 17) & 0xFF);
	command[4*nsg-1] = ((address >> 25) & 0xFF);

	command[2] = nsg;

	result = sddr09_send_scsi_command(us, command, 4*nsg+3);

	if (result) {
		US_DEBUGP("Result for send_control in sddr09_read_sg %d\n",
			  result);
		return result;
	}

	buf = kmalloc(bulklen, GFP_NOIO);
	if (!buf)
		return -ENOMEM;

	result = usb_stor_bulk_transfer_buf(us, us->recv_bulk_pipe,
				       buf, bulklen, NULL);
	kfree(buf);
	if (result != USB_STOR_XFER_GOOD) {
		US_DEBUGP("Result for bulk_transfer in sddr09_read_sg %d\n",
			  result);
		return -EIO;
	}

	return 0;
}
#endif

/*
 * Read Status Command: 12 bytes.
 * byte 0: opcode: EC
 *
 * Returns 64 bytes, all zero except for the first.
 * bit 0: 1: Error
 * bit 5: 1: Suspended
 * bit 6: 1: Ready
 * bit 7: 1: Not write-protected
 */

static int
sddr09_read_status(struct us_data *us, unsigned char *status) {

	unsigned char *command = us->iobuf;
	unsigned char *data = us->iobuf;
	int result;

	US_DEBUGP("Reading status...\n");

	memset(command, 0, 12);
	command[0] = 0xEC;
	command[1] = LUNBITS;

	result = sddr09_send_scsi_command(us, command, 12);
	if (result)
		return result;

	result = usb_stor_bulk_transfer_buf(us, us->recv_bulk_pipe,
				       data, 64, NULL);
	*status = data[0];
	return (result == USB_STOR_XFER_GOOD ? 0 : -EIO);
}

static int
sddr09_read_data(struct us_data *us,
		 unsigned long address,
		 unsigned int sectors) {

	struct sddr09_card_info *info = (struct sddr09_card_info *) us->extra;
	unsigned char *buffer;
	unsigned int lba, maxlba, pba;
	unsigned int page, pages;
	unsigned int len, offset;
	struct scatterlist *sg;
	int result;

	// Figure out the initial LBA and page
	lba = address >> info->blockshift;
	page = (address & info->blockmask);
	maxlba = info->capacity >> (info->pageshift + info->blockshift);
	if (lba >= maxlba)
		return -EIO;

	// Since we only read in one block at a time, we have to create
	// a bounce buffer and move the data a piece at a time between the
	// bounce buffer and the actual transfer buffer.

	len = min(sectors, (unsigned int) info->blocksize) * info->pagesize;
	buffer = kmalloc(len, GFP_NOIO);
	if (buffer == NULL) {
		printk(KERN_WARNING "sddr09_read_data: Out of memory\n");
		return -ENOMEM;
	}

	// This could be made much more efficient by checking for
	// contiguous LBA's. Another exercise left to the student.

	result = 0;
	offset = 0;
	sg = NULL;

	while (sectors > 0) {

		/* Find number of pages we can read in this block */
		pages = min(sectors, info->blocksize - page);
		len = pages << info->pageshift;

		/* Not overflowing capacity? */
		if (lba >= maxlba) {
			US_DEBUGP("Error: Requested lba %u exceeds "
				  "maximum %u\n", lba, maxlba);
			result = -EIO;
			break;
		}

		/* Find where this lba lives on disk */
		pba = info->lba_to_pba[lba];

		if (pba == UNDEF) {	/* this lba was never written */

			US_DEBUGP("Read %d zero pages (LBA %d) page %d\n",
				  pages, lba, page);

			/* This is not really an error. It just means
			   that the block has never been written.
			   Instead of returning an error
			   it is better to return all zero data. */

			memset(buffer, 0, len);

		} else {
			US_DEBUGP("Read %d pages, from PBA %d"
				  " (LBA %d) page %d\n",
				  pages, pba, lba, page);

			address = ((pba << info->blockshift) + page) << 
				info->pageshift;

			result = sddr09_read20(us, address>>1,
					pages, info->pageshift, buffer, 0);
			if (result)
				break;
		}

		// Store the data in the transfer buffer
		usb_stor_access_xfer_buf(buffer, len, us->srb,
				&sg, &offset, TO_XFER_BUF);

		page = 0;
		lba++;
		sectors -= pages;
	}

	kfree(buffer);
	return result;
}

static unsigned int
sddr09_find_unused_pba(struct sddr09_card_info *info, unsigned int lba) {
	static unsigned int lastpba = 1;
	int zonestart, end, i;

	zonestart = (lba/1000) << 10;
	end = info->capacity >> (info->blockshift + info->pageshift);
	end -= zonestart;
	if (end > 1024)
		end = 1024;

	for (i = lastpba+1; i < end; i++) {
		if (info->pba_to_lba[zonestart+i] == UNDEF) {
			lastpba = i;
			return zonestart+i;
		}
	}
	for (i = 0; i <= lastpba; i++) {
		if (info->pba_to_lba[zonestart+i] == UNDEF) {
			lastpba = i;
			return zonestart+i;
		}
	}
	return 0;
}

static int
sddr09_write_lba(struct us_data *us, unsigned int lba,
		 unsigned int page, unsigned int pages,
		 unsigned char *ptr, unsigned char *blockbuffer) {

	struct sddr09_card_info *info = (struct sddr09_card_info *) us->extra;
	unsigned long address;
	unsigned int pba, lbap;
	unsigned int pagelen;
	unsigned char *bptr, *cptr, *xptr;
	unsigned char ecc[3];
	int i, result, isnew;

	lbap = ((lba % 1000) << 1) | 0x1000;
	if (parity[MSB_of(lbap) ^ LSB_of(lbap)])
		lbap ^= 1;
	pba = info->lba_to_pba[lba];
	isnew = 0;

	if (pba == UNDEF) {
		pba = sddr09_find_unused_pba(info, lba);
		if (!pba) {
			printk(KERN_WARNING
			       "sddr09_write_lba: Out of unused blocks\n");
			return -ENOSPC;
		}
		info->pba_to_lba[pba] = lba;
		info->lba_to_pba[lba] = pba;
		isnew = 1;
	}

	if (pba == 1) {
		/* Maybe it is impossible to write to PBA 1.
		   Fake success, but don't do anything. */
		printk(KERN_WARNING "sddr09: avoid writing to pba 1\n");
		return 0;
	}

	pagelen = (1 << info->pageshift) + (1 << CONTROL_SHIFT);

	/* read old contents */
	address = (pba << (info->pageshift + info->blockshift));
	result = sddr09_read22(us, address>>1, info->blocksize,
			       info->pageshift, blockbuffer, 0);
	if (result)
		return result;

	/* check old contents and fill lba */
	for (i = 0; i < info->blocksize; i++) {
		bptr = blockbuffer + i*pagelen;
		cptr = bptr + info->pagesize;
		nand_compute_ecc(bptr, ecc);
		if (!nand_compare_ecc(cptr+13, ecc)) {
			US_DEBUGP("Warning: bad ecc in page %d- of pba %d\n",
				  i, pba);
			nand_store_ecc(cptr+13, ecc);
		}
		nand_compute_ecc(bptr+(info->pagesize / 2), ecc);
		if (!nand_compare_ecc(cptr+8, ecc)) {
			US_DEBUGP("Warning: bad ecc in page %d+ of pba %d\n",
				  i, pba);
			nand_store_ecc(cptr+8, ecc);
		}
		cptr[6] = cptr[11] = MSB_of(lbap);
		cptr[7] = cptr[12] = LSB_of(lbap);
	}

	/* copy in new stuff and compute ECC */
	xptr = ptr;
	for (i = page; i < page+pages; i++) {
		bptr = blockbuffer + i*pagelen;
		cptr = bptr + info->pagesize;
		memcpy(bptr, xptr, info->pagesize);
		xptr += info->pagesize;
		nand_compute_ecc(bptr, ecc);
		nand_store_ecc(cptr+13, ecc);
		nand_compute_ecc(bptr+(info->pagesize / 2), ecc);
		nand_store_ecc(cptr+8, ecc);
	}

	US_DEBUGP("Rewrite PBA %d (LBA %d)\n", pba, lba);

	result = sddr09_write_inplace(us, address>>1, info->blocksize,
				      info->pageshift, blockbuffer, 0);

	US_DEBUGP("sddr09_write_inplace returns %d\n", result);

#if 0
	{
		unsigned char status = 0;
		int result2 = sddr09_read_status(us, &status);
		if (result2)
			US_DEBUGP("sddr09_write_inplace: cannot read status\n");
		else if (status != 0xc0)
			US_DEBUGP("sddr09_write_inplace: status after write: 0x%x\n",
				  status);
	}
#endif

#if 0
	{
		int result2 = sddr09_test_unit_ready(us);
	}
#endif

	return result;
}

static int
sddr09_write_data(struct us_data *us,
		  unsigned long address,
		  unsigned int sectors) {

	struct sddr09_card_info *info = (struct sddr09_card_info *) us->extra;
	unsigned int lba, maxlba, page, pages;
	unsigned int pagelen, blocklen;
	unsigned char *blockbuffer;
	unsigned char *buffer;
	unsigned int len, offset;
	struct scatterlist *sg;
	int result;

	// Figure out the initial LBA and page
	lba = address >> info->blockshift;
	page = (address & info->blockmask);
	maxlba = info->capacity >> (info->pageshift + info->blockshift);
	if (lba >= maxlba)
		return -EIO;

	// blockbuffer is used for reading in the old data, overwriting
	// with the new data, and performing ECC calculations

	/* TODO: instead of doing kmalloc/kfree for each write,
	   add a bufferpointer to the info structure */

	pagelen = (1 << info->pageshift) + (1 << CONTROL_SHIFT);
	blocklen = (pagelen << info->blockshift);
	blockbuffer = kmalloc(blocklen, GFP_NOIO);
	if (!blockbuffer) {
		printk(KERN_WARNING "sddr09_write_data: Out of memory\n");
		return -ENOMEM;
	}

	// Since we don't write the user data directly to the device,
	// we have to create a bounce buffer and move the data a piece
	// at a time between the bounce buffer and the actual transfer buffer.

	len = min(sectors, (unsigned int) info->blocksize) * info->pagesize;
	buffer = kmalloc(len, GFP_NOIO);
	if (buffer == NULL) {
		printk(KERN_WARNING "sddr09_write_data: Out of memory\n");
		kfree(blockbuffer);
		return -ENOMEM;
	}

	result = 0;
	offset = 0;
	sg = NULL;

	while (sectors > 0) {

		// Write as many sectors as possible in this block

		pages = min(sectors, info->blocksize - page);
		len = (pages << info->pageshift);

		/* Not overflowing capacity? */
		if (lba >= maxlba) {
			US_DEBUGP("Error: Requested lba %u exceeds "
				  "maximum %u\n", lba, maxlba);
			result = -EIO;
			break;
		}

		// Get the data from the transfer buffer
		usb_stor_access_xfer_buf(buffer, len, us->srb,
				&sg, &offset, FROM_XFER_BUF);

		result = sddr09_write_lba(us, lba, page, pages,
				buffer, blockbuffer);
		if (result)
			break;

		page = 0;
		lba++;
		sectors -= pages;
	}

	kfree(buffer);
	kfree(blockbuffer);

	return result;
}

static int
sddr09_read_control(struct us_data *us,
		unsigned long address,
		unsigned int blocks,
		unsigned char *content,
		int use_sg) {

	US_DEBUGP("Read control address %lu, blocks %d\n",
		address, blocks);

	return sddr09_read21(us, address, blocks,
			     CONTROL_SHIFT, content, use_sg);
}

/*
 * Read Device ID Command: 12 bytes.
 * byte 0: opcode: ED
 *
 * Returns 2 bytes: Manufacturer ID and Device ID.
 * On more recent cards 3 bytes: the third byte is an option code A5
 * signifying that the secret command to read an 128-bit ID is available.
 * On still more recent cards 4 bytes: the fourth byte C0 means that
 * a second read ID cmd is available.
 */
static int
sddr09_read_deviceID(struct us_data *us, unsigned char *deviceID) {
	unsigned char *command = us->iobuf;
	unsigned char *content = us->iobuf;
	int result, i;

	memset(command, 0, 12);
	command[0] = 0xED;
	command[1] = LUNBITS;

	result = sddr09_send_scsi_command(us, command, 12);
	if (result)
		return result;

	result = usb_stor_bulk_transfer_buf(us, us->recv_bulk_pipe,
			content, 64, NULL);

	for (i = 0; i < 4; i++)
		deviceID[i] = content[i];

	return (result == USB_STOR_XFER_GOOD ? 0 : -EIO);
}

static int
sddr09_get_wp(struct us_data *us, struct sddr09_card_info *info) {
	int result;
	unsigned char status;

	result = sddr09_read_status(us, &status);
	if (result) {
		US_DEBUGP("sddr09_get_wp: read_status fails\n");
		return result;
	}
	US_DEBUGP("sddr09_get_wp: status 0x%02X", status);
	if ((status & 0x80) == 0) {
		info->flags |= SDDR09_WP;	/* write protected */
		US_DEBUGP(" WP");
	}
	if (status & 0x40)
		US_DEBUGP(" Ready");
	if (status & LUNBITS)
		US_DEBUGP(" Suspended");
	if (status & 0x1)
		US_DEBUGP(" Error");
	US_DEBUGP("\n");
	return 0;
}

#if 0
/*
 * Reset Command: 12 bytes.
 * byte 0: opcode: EB
 */
static int
sddr09_reset(struct us_data *us) {

	unsigned char *command = us->iobuf;

	memset(command, 0, 12);
	command[0] = 0xEB;
	command[1] = LUNBITS;

	return sddr09_send_scsi_command(us, command, 12);
}
#endif

static struct nand_flash_dev *
sddr09_get_cardinfo(struct us_data *us, unsigned char flags) {
	struct nand_flash_dev *cardinfo;
	unsigned char deviceID[4];
	char blurbtxt[256];
	int result;

	US_DEBUGP("Reading capacity...\n");

	result = sddr09_read_deviceID(us, deviceID);

	if (result) {
		US_DEBUGP("Result of read_deviceID is %d\n", result);
		printk(KERN_WARNING "sddr09: could not read card info\n");
		return NULL;
	}

	sprintf(blurbtxt, "sddr09: Found Flash card, ID = %02X %02X %02X %02X",
		deviceID[0], deviceID[1], deviceID[2], deviceID[3]);

	/* Byte 0 is the manufacturer */
	sprintf(blurbtxt + strlen(blurbtxt),
		": Manuf. %s",
		nand_flash_manufacturer(deviceID[0]));

	/* Byte 1 is the device type */
	cardinfo = nand_find_id(deviceID[1]);
	if (cardinfo) {
		/* MB or MiB? It is neither. A 16 MB card has
		   17301504 raw bytes, of which 16384000 are
		   usable for user data. */
		sprintf(blurbtxt + strlen(blurbtxt),
			", %d MB", 1<<(cardinfo->chipshift - 20));
	} else {
		sprintf(blurbtxt + strlen(blurbtxt),
			", type unrecognized");
	}

	/* Byte 2 is code to signal availability of 128-bit ID */
	if (deviceID[2] == 0xa5) {
		sprintf(blurbtxt + strlen(blurbtxt),
			", 128-bit ID");
	}

	/* Byte 3 announces the availability of another read ID command */
	if (deviceID[3] == 0xc0) {
		sprintf(blurbtxt + strlen(blurbtxt),
			", extra cmd");
	}

	if (flags & SDDR09_WP)
		sprintf(blurbtxt + strlen(blurbtxt),
			", WP");

	printk(KERN_WARNING "%s\n", blurbtxt);

	return cardinfo;
}

static int
sddr09_read_map(struct us_data *us) {

	struct sddr09_card_info *info = (struct sddr09_card_info *) us->extra;
	int numblocks, alloc_len, alloc_blocks;
	int i, j, result;
	unsigned char *buffer, *buffer_end, *ptr;
	unsigned int lba, lbact;

	if (!info->capacity)
		return -1;

	// size of a block is 1 << (blockshift + pageshift) bytes
	// divide into the total capacity to get the number of blocks

	numblocks = info->capacity >> (info->blockshift + info->pageshift);

	// read 64 bytes for every block (actually 1 << CONTROL_SHIFT)
	// but only use a 64 KB buffer
	// buffer size used must be a multiple of (1 << CONTROL_SHIFT)
#define SDDR09_READ_MAP_BUFSZ 65536

	alloc_blocks = min(numblocks, SDDR09_READ_MAP_BUFSZ >> CONTROL_SHIFT);
	alloc_len = (alloc_blocks << CONTROL_SHIFT);
	buffer = kmalloc(alloc_len, GFP_NOIO);
	if (buffer == NULL) {
		printk(KERN_WARNING "sddr09_read_map: out of memory\n");
		result = -1;
		goto done;
	}
	buffer_end = buffer + alloc_len;

#undef SDDR09_READ_MAP_BUFSZ

	kfree(info->lba_to_pba);
	kfree(info->pba_to_lba);
	info->lba_to_pba = kmalloc(numblocks*sizeof(int), GFP_NOIO);
	info->pba_to_lba = kmalloc(numblocks*sizeof(int), GFP_NOIO);

	if (info->lba_to_pba == NULL || info->pba_to_lba == NULL) {
		printk(KERN_WARNING "sddr09_read_map: out of memory\n");
		result = -1;
		goto done;
	}

	for (i = 0; i < numblocks; i++)
		info->lba_to_pba[i] = info->pba_to_lba[i] = UNDEF;

	/*
	 * Define lba-pba translation table
	 */

	ptr = buffer_end;
	for (i = 0; i < numblocks; i++) {
		ptr += (1 << CONTROL_SHIFT);
		if (ptr >= buffer_end) {
			unsigned long address;

			address = i << (info->pageshift + info->blockshift);
			result = sddr09_read_control(
				us, address>>1,
				min(alloc_blocks, numblocks - i),
				buffer, 0);
			if (result) {
				result = -1;
				goto done;
			}
			ptr = buffer;
		}

		if (i == 0 || i == 1) {
			info->pba_to_lba[i] = UNUSABLE;
			continue;
		}

		/* special PBAs have control field 0^16 */
		for (j = 0; j < 16; j++)
			if (ptr[j] != 0)
				goto nonz;
		info->pba_to_lba[i] = UNUSABLE;
		printk(KERN_WARNING "sddr09: PBA %d has no logical mapping\n",
		       i);
		continue;

	nonz:
		/* unwritten PBAs have control field FF^16 */
		for (j = 0; j < 16; j++)
			if (ptr[j] != 0xff)
				goto nonff;
		continue;

	nonff:
		/* normal PBAs start with six FFs */
		if (j < 6) {
			printk(KERN_WARNING
			       "sddr09: PBA %d has no logical mapping: "
			       "reserved area = %02X%02X%02X%02X "
			       "data status %02X block status %02X\n",
			       i, ptr[0], ptr[1], ptr[2], ptr[3],
			       ptr[4], ptr[5]);
			info->pba_to_lba[i] = UNUSABLE;
			continue;
		}

		if ((ptr[6] >> 4) != 0x01) {
			printk(KERN_WARNING
			       "sddr09: PBA %d has invalid address field "
			       "%02X%02X/%02X%02X\n",
			       i, ptr[6], ptr[7], ptr[11], ptr[12]);
			info->pba_to_lba[i] = UNUSABLE;
			continue;
		}

		/* check even parity */
		if (parity[ptr[6] ^ ptr[7]]) {
			printk(KERN_WARNING
			       "sddr09: Bad parity in LBA for block %d"
			       " (%02X %02X)\n", i, ptr[6], ptr[7]);
			info->pba_to_lba[i] = UNUSABLE;
			continue;
		}

		lba = short_pack(ptr[7], ptr[6]);
		lba = (lba & 0x07FF) >> 1;

		/*
		 * Every 1024 physical blocks ("zone"), the LBA numbers
		 * go back to zero, but are within a higher block of LBA's.
		 * Also, there is a maximum of 1000 LBA's per zone.
		 * In other words, in PBA 1024-2047 you will find LBA 0-999
		 * which are really LBA 1000-1999. This allows for 24 bad
		 * or special physical blocks per zone.
		 */

		if (lba >= 1000) {
			printk(KERN_WARNING
			       "sddr09: Bad low LBA %d for block %d\n",
			       lba, i);
			goto possibly_erase;
		}

		lba += 1000*(i/0x400);

		if (info->lba_to_pba[lba] != UNDEF) {
			printk(KERN_WARNING
			       "sddr09: LBA %d seen for PBA %d and %d\n",
			       lba, info->lba_to_pba[lba], i);
			goto possibly_erase;
		}

		info->pba_to_lba[i] = lba;
		info->lba_to_pba[lba] = i;
		continue;

	possibly_erase:
		if (erase_bad_lba_entries) {
			unsigned long address;

			address = (i << (info->pageshift + info->blockshift));
			sddr09_erase(us, address>>1);
			info->pba_to_lba[i] = UNDEF;
		} else
			info->pba_to_lba[i] = UNUSABLE;
	}

	/*
	 * Approximate capacity. This is not entirely correct yet,
	 * since a zone with less than 1000 usable pages leads to
	 * missing LBAs. Especially if it is the last zone, some
	 * LBAs can be past capacity.
	 */
	lbact = 0;
	for (i = 0; i < numblocks; i += 1024) {
		int ct = 0;

		for (j = 0; j < 1024 && i+j < numblocks; j++) {
			if (info->pba_to_lba[i+j] != UNUSABLE) {
				if (ct >= 1000)
					info->pba_to_lba[i+j] = SPARE;
				else
					ct++;
			}
		}
		lbact += ct;
	}
	info->lbact = lbact;
	US_DEBUGP("Found %d LBA's\n", lbact);
	result = 0;

 done:
	if (result != 0) {
		kfree(info->lba_to_pba);
		kfree(info->pba_to_lba);
		info->lba_to_pba = NULL;
		info->pba_to_lba = NULL;
	}
	kfree(buffer);
	return result;
}

static void
sddr09_card_info_destructor(void *extra) {
	struct sddr09_card_info *info = (struct sddr09_card_info *)extra;

	if (!info)
		return;

	kfree(info->lba_to_pba);
	kfree(info->pba_to_lba);
}

static int
sddr09_common_init(struct us_data *us) {
	int result;

	/* set the configuration -- STALL is an acceptable response here */
	if (us->pusb_dev->actconfig->desc.bConfigurationValue != 1) {
		US_DEBUGP("active config #%d != 1 ??\n", us->pusb_dev
				->actconfig->desc.bConfigurationValue);
		return -EINVAL;
	}

	result = usb_reset_configuration(us->pusb_dev);
	US_DEBUGP("Result of usb_reset_configuration is %d\n", result);
	if (result == -EPIPE) {
		US_DEBUGP("-- stall on control interface\n");
	} else if (result != 0) {
		/* it's not a stall, but another error -- time to bail */
		US_DEBUGP("-- Unknown error.  Rejecting device\n");
		return -EINVAL;
	}

	us->extra = kzalloc(sizeof(struct sddr09_card_info), GFP_NOIO);
	if (!us->extra)
		return -ENOMEM;
	us->extra_destructor = sddr09_card_info_destructor;

	nand_init_ecc();
	return 0;
}


/*
 * This is needed at a very early stage. If this is not listed in the
 * unusual devices list but called from here then LUN 0 of the combo reader
 * is not recognized. But I do not know what precisely these calls do.
 */
static int
usb_stor_sddr09_dpcm_init(struct us_data *us) {
	int result;
	unsigned char *data = us->iobuf;

	result = sddr09_common_init(us);
	if (result)
		return result;

	result = sddr09_send_command(us, 0x01, USB_DIR_IN, data, 2);
	if (result) {
		US_DEBUGP("sddr09_init: send_command fails\n");
		return result;
	}

	US_DEBUGP("SDDR09init: %02X %02X\n", data[0], data[1]);
	// get 07 02

	result = sddr09_send_command(us, 0x08, USB_DIR_IN, data, 2);
	if (result) {
		US_DEBUGP("sddr09_init: 2nd send_command fails\n");
		return result;
	}

	US_DEBUGP("SDDR09init: %02X %02X\n", data[0], data[1]);
	// get 07 00

	result = sddr09_request_sense(us, data, 18);
	if (result == 0 && data[2] != 0) {
		int j;
		for (j=0; j<18; j++)
			printk(" %02X", data[j]);
		printk("\n");
		// get 70 00 00 00 00 00 00 * 00 00 00 00 00 00
		// 70: current command
		// sense key 0, sense code 0, extd sense code 0
		// additional transfer length * = sizeof(data) - 7
		// Or: 70 00 06 00 00 00 00 0b 00 00 00 00 28 00 00 00 00 00
		// sense key 06, sense code 28: unit attention,
		// not ready to ready transition
	}

	// test unit ready

	return 0;		/* not result */
}

/*
 * Transport for the Microtech DPCM-USB
 */
static int dpcm_transport(struct scsi_cmnd *srb, struct us_data *us)
{
	int ret;

	US_DEBUGP("dpcm_transport: LUN=%d\n", srb->device->lun);

	switch (srb->device->lun) {
	case 0:

		/*
		 * LUN 0 corresponds to the CompactFlash card reader.
		 */
		ret = usb_stor_CB_transport(srb, us);
		break;

	case 1:

		/*
		 * LUN 1 corresponds to the SmartMedia card reader.
		 */

		/*
		 * Set the LUN to 0 (just in case).
		 */
		srb->device->lun = 0;
		ret = sddr09_transport(srb, us);
		srb->device->lun = 1;
		break;

	default:
		US_DEBUGP("dpcm_transport: Invalid LUN %d\n",
				srb->device->lun);
		ret = USB_STOR_TRANSPORT_ERROR;
		break;
	}
	return ret;
}


/*
 * Transport for the Sandisk SDDR-09
 */
static int sddr09_transport(struct scsi_cmnd *srb, struct us_data *us)
{
	static unsigned char sensekey = 0, sensecode = 0;
	static unsigned char havefakesense = 0;
	int result, i;
	unsigned char *ptr = us->iobuf;
	unsigned long capacity;
	unsigned int page, pages;

	struct sddr09_card_info *info;

	static unsigned char inquiry_response[8] = {
		0x00, 0x80, 0x00, 0x02, 0x1F, 0x00, 0x00, 0x00
	};

	/* note: no block descriptor support */
	static unsigned char mode_page_01[19] = {
		0x00, 0x0F, 0x00, 0x0, 0x0, 0x0, 0x00,
		0x01, 0x0A,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};

	info = (struct sddr09_card_info *)us->extra;

	if (srb->cmnd[0] == REQUEST_SENSE && havefakesense) {
		/* for a faked command, we have to follow with a faked sense */
		memset(ptr, 0, 18);
		ptr[0] = 0x70;
		ptr[2] = sensekey;
		ptr[7] = 11;
		ptr[12] = sensecode;
		usb_stor_set_xfer_buf(ptr, 18, srb);
		sensekey = sensecode = havefakesense = 0;
		return USB_STOR_TRANSPORT_GOOD;
	}

	havefakesense = 1;

	/* Dummy up a response for INQUIRY since SDDR09 doesn't
	   respond to INQUIRY commands */

	if (srb->cmnd[0] == INQUIRY) {
		memcpy(ptr, inquiry_response, 8);
		fill_inquiry_response(us, ptr, 36);
		return USB_STOR_TRANSPORT_GOOD;
	}

	if (srb->cmnd[0] == READ_CAPACITY) {
		struct nand_flash_dev *cardinfo;

		sddr09_get_wp(us, info);	/* read WP bit */

		cardinfo = sddr09_get_cardinfo(us, info->flags);
		if (!cardinfo) {
			/* probably no media */
		init_error:
			sensekey = 0x02;	/* not ready */
			sensecode = 0x3a;	/* medium not present */
			return USB_STOR_TRANSPORT_FAILED;
		}

		info->capacity = (1 << cardinfo->chipshift);
		info->pageshift = cardinfo->pageshift;
		info->pagesize = (1 << info->pageshift);
		info->blockshift = cardinfo->blockshift;
		info->blocksize = (1 << info->blockshift);
		info->blockmask = info->blocksize - 1;

		// map initialization, must follow get_cardinfo()
		if (sddr09_read_map(us)) {
			/* probably out of memory */
			goto init_error;
		}

		// Report capacity

		capacity = (info->lbact << info->blockshift) - 1;

		((__be32 *) ptr)[0] = cpu_to_be32(capacity);

		// Report page size

		((__be32 *) ptr)[1] = cpu_to_be32(info->pagesize);
		usb_stor_set_xfer_buf(ptr, 8, srb);

		return USB_STOR_TRANSPORT_GOOD;
	}

	if (srb->cmnd[0] == MODE_SENSE_10) {
		int modepage = (srb->cmnd[2] & 0x3F);

		/* They ask for the Read/Write error recovery page,
		   or for all pages. */
		/* %% We should check DBD %% */
		if (modepage == 0x01 || modepage == 0x3F) {
			US_DEBUGP("SDDR09: Dummy up request for "
				  "mode page 0x%x\n", modepage);

			memcpy(ptr, mode_page_01, sizeof(mode_page_01));
			((__be16*)ptr)[0] = cpu_to_be16(sizeof(mode_page_01) - 2);
			ptr[3] = (info->flags & SDDR09_WP) ? 0x80 : 0;
			usb_stor_set_xfer_buf(ptr, sizeof(mode_page_01), srb);
			return USB_STOR_TRANSPORT_GOOD;
		}

		sensekey = 0x05;	/* illegal request */
		sensecode = 0x24;	/* invalid field in CDB */
		return USB_STOR_TRANSPORT_FAILED;
	}

	if (srb->cmnd[0] == ALLOW_MEDIUM_REMOVAL)
		return USB_STOR_TRANSPORT_GOOD;

	havefakesense = 0;

	if (srb->cmnd[0] == READ_10) {

		page = short_pack(srb->cmnd[3], srb->cmnd[2]);
		page <<= 16;
		page |= short_pack(srb->cmnd[5], srb->cmnd[4]);
		pages = short_pack(srb->cmnd[8], srb->cmnd[7]);

		US_DEBUGP("READ_10: read page %d pagect %d\n",
			  page, pages);

		result = sddr09_read_data(us, page, pages);
		return (result == 0 ? USB_STOR_TRANSPORT_GOOD :
				USB_STOR_TRANSPORT_ERROR);
	}

	if (srb->cmnd[0] == WRITE_10) {

		page = short_pack(srb->cmnd[3], srb->cmnd[2]);
		page <<= 16;
		page |= short_pack(srb->cmnd[5], srb->cmnd[4]);
		pages = short_pack(srb->cmnd[8], srb->cmnd[7]);

		US_DEBUGP("WRITE_10: write page %d pagect %d\n",
			  page, pages);

		result = sddr09_write_data(us, page, pages);
		return (result == 0 ? USB_STOR_TRANSPORT_GOOD :
				USB_STOR_TRANSPORT_ERROR);
	}

	/* catch-all for all other commands, except
	 * pass TEST_UNIT_READY and REQUEST_SENSE through
	 */
	if (srb->cmnd[0] != TEST_UNIT_READY &&
	    srb->cmnd[0] != REQUEST_SENSE) {
		sensekey = 0x05;	/* illegal request */
		sensecode = 0x20;	/* invalid command */
		havefakesense = 1;
		return USB_STOR_TRANSPORT_FAILED;
	}

	for (; srb->cmd_len<12; srb->cmd_len++)
		srb->cmnd[srb->cmd_len] = 0;

	srb->cmnd[1] = LUNBITS;

	ptr[0] = 0;
	for (i=0; i<12; i++)
		sprintf(ptr+strlen(ptr), "%02X ", srb->cmnd[i]);

	US_DEBUGP("SDDR09: Send control for command %s\n", ptr);

	result = sddr09_send_scsi_command(us, srb->cmnd, 12);
	if (result) {
		US_DEBUGP("sddr09_transport: sddr09_send_scsi_command "
			  "returns %d\n", result);
		return USB_STOR_TRANSPORT_ERROR;
	}

	if (scsi_bufflen(srb) == 0)
		return USB_STOR_TRANSPORT_GOOD;

	if (srb->sc_data_direction == DMA_TO_DEVICE ||
	    srb->sc_data_direction == DMA_FROM_DEVICE) {
		unsigned int pipe = (srb->sc_data_direction == DMA_TO_DEVICE)
				? us->send_bulk_pipe : us->recv_bulk_pipe;

		US_DEBUGP("SDDR09: %s %d bytes\n",
			  (srb->sc_data_direction == DMA_TO_DEVICE) ?
			  "sending" : "receiving",
			  scsi_bufflen(srb));

		result = usb_stor_bulk_srb(us, pipe, srb);

		return (result == USB_STOR_XFER_GOOD ?
			USB_STOR_TRANSPORT_GOOD : USB_STOR_TRANSPORT_ERROR);
	} 

	return USB_STOR_TRANSPORT_GOOD;
}

/*
 * Initialization routine for the sddr09 subdriver
 */
static int
usb_stor_sddr09_init(struct us_data *us) {
	return sddr09_common_init(us);
}

static int sddr09_probe(struct usb_interface *intf,
			 const struct usb_device_id *id)
{
	struct us_data *us;
	int result;

	result = usb_stor_probe1(&us, intf, id,
			(id - sddr09_usb_ids) + sddr09_unusual_dev_list);
	if (result)
		return result;

	if (us->protocol == US_PR_DPCM_USB) {
		us->transport_name = "Control/Bulk-EUSB/SDDR09";
		us->transport = dpcm_transport;
		us->transport_reset = usb_stor_CB_reset;
		us->max_lun = 1;
	} else {
		us->transport_name = "EUSB/SDDR09";
		us->transport = sddr09_transport;
		us->transport_reset = usb_stor_CB_reset;
		us->max_lun = 0;
	}

	result = usb_stor_probe2(us);
	return result;
}

static struct usb_driver sddr09_driver = {
	.name =		"ums-sddr09",
	.probe =	sddr09_probe,
	.disconnect =	usb_stor_disconnect,
	.suspend =	usb_stor_suspend,
	.resume =	usb_stor_resume,
	.reset_resume =	usb_stor_reset_resume,
	.pre_reset =	usb_stor_pre_reset,
	.post_reset =	usb_stor_post_reset,
	.id_table =	sddr09_usb_ids,
	.soft_unbind =	1,
};

static int __init sddr09_init(void)
{
	return usb_register(&sddr09_driver);
}

static void __exit sddr09_exit(void)
{
	usb_deregister(&sddr09_driver);
}

module_init(sddr09_init);
module_exit(sddr09_exit);
