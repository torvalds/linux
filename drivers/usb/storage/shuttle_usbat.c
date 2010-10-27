/* Driver for SCM Microsystems (a.k.a. Shuttle) USB-ATAPI cable
 *
 * Current development and maintenance by:
 *   (c) 2000, 2001 Robert Baruch (autophile@starband.net)
 *   (c) 2004, 2005 Daniel Drake <dsd@gentoo.org>
 *
 * Developed with the assistance of:
 *   (c) 2002 Alan Stern <stern@rowland.org>
 *
 * Flash support based on earlier work by:
 *   (c) 2002 Thomas Kreiling <usbdev@sm04.de>
 *
 * Many originally ATAPI devices were slightly modified to meet the USB
 * market by using some kind of translation from ATAPI to USB on the host,
 * and the peripheral would translate from USB back to ATAPI.
 *
 * SCM Microsystems (www.scmmicro.com) makes a device, sold to OEM's only, 
 * which does the USB-to-ATAPI conversion.  By obtaining the data sheet on
 * their device under nondisclosure agreement, I have been able to write
 * this driver for Linux.
 *
 * The chip used in the device can also be used for EPP and ISA translation
 * as well. This driver is only guaranteed to work with the ATAPI
 * translation.
 *
 * See the Kconfig help text for a list of devices known to be supported by
 * this driver.
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

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/cdrom.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>

#include "usb.h"
#include "transport.h"
#include "protocol.h"
#include "debug.h"

MODULE_DESCRIPTION("Driver for SCM Microsystems (a.k.a. Shuttle) USB-ATAPI cable");
MODULE_AUTHOR("Daniel Drake <dsd@gentoo.org>, Robert Baruch <autophile@starband.net>");
MODULE_LICENSE("GPL");

/* Supported device types */
#define USBAT_DEV_HP8200	0x01
#define USBAT_DEV_FLASH		0x02

#define USBAT_EPP_PORT		0x10
#define USBAT_EPP_REGISTER	0x30
#define USBAT_ATA		0x40
#define USBAT_ISA		0x50

/* Commands (need to be logically OR'd with an access type */
#define USBAT_CMD_READ_REG		0x00
#define USBAT_CMD_WRITE_REG		0x01
#define USBAT_CMD_READ_BLOCK	0x02
#define USBAT_CMD_WRITE_BLOCK	0x03
#define USBAT_CMD_COND_READ_BLOCK	0x04
#define USBAT_CMD_COND_WRITE_BLOCK	0x05
#define USBAT_CMD_WRITE_REGS	0x07

/* Commands (these don't need an access type) */
#define USBAT_CMD_EXEC_CMD	0x80
#define USBAT_CMD_SET_FEAT	0x81
#define USBAT_CMD_UIO		0x82

/* Methods of accessing UIO register */
#define USBAT_UIO_READ	1
#define USBAT_UIO_WRITE	0

/* Qualifier bits */
#define USBAT_QUAL_FCQ	0x20	/* full compare */
#define USBAT_QUAL_ALQ	0x10	/* auto load subcount */

/* USBAT Flash Media status types */
#define USBAT_FLASH_MEDIA_NONE	0
#define USBAT_FLASH_MEDIA_CF	1

/* USBAT Flash Media change types */
#define USBAT_FLASH_MEDIA_SAME	0
#define USBAT_FLASH_MEDIA_CHANGED	1

/* USBAT ATA registers */
#define USBAT_ATA_DATA      0x10  /* read/write data (R/W) */
#define USBAT_ATA_FEATURES  0x11  /* set features (W) */
#define USBAT_ATA_ERROR     0x11  /* error (R) */
#define USBAT_ATA_SECCNT    0x12  /* sector count (R/W) */
#define USBAT_ATA_SECNUM    0x13  /* sector number (R/W) */
#define USBAT_ATA_LBA_ME    0x14  /* cylinder low (R/W) */
#define USBAT_ATA_LBA_HI    0x15  /* cylinder high (R/W) */
#define USBAT_ATA_DEVICE    0x16  /* head/device selection (R/W) */
#define USBAT_ATA_STATUS    0x17  /* device status (R) */
#define USBAT_ATA_CMD       0x17  /* device command (W) */
#define USBAT_ATA_ALTSTATUS 0x0E  /* status (no clear IRQ) (R) */

/* USBAT User I/O Data registers */
#define USBAT_UIO_EPAD		0x80 /* Enable Peripheral Control Signals */
#define USBAT_UIO_CDT		0x40 /* Card Detect (Read Only) */
				     /* CDT = ACKD & !UI1 & !UI0 */
#define USBAT_UIO_1		0x20 /* I/O 1 */
#define USBAT_UIO_0		0x10 /* I/O 0 */
#define USBAT_UIO_EPP_ATA	0x08 /* 1=EPP mode, 0=ATA mode */
#define USBAT_UIO_UI1		0x04 /* Input 1 */
#define USBAT_UIO_UI0		0x02 /* Input 0 */
#define USBAT_UIO_INTR_ACK	0x01 /* Interrupt (ATA/ISA)/Acknowledge (EPP) */

/* USBAT User I/O Enable registers */
#define USBAT_UIO_DRVRST	0x80 /* Reset Peripheral */
#define USBAT_UIO_ACKD		0x40 /* Enable Card Detect */
#define USBAT_UIO_OE1		0x20 /* I/O 1 set=output/clr=input */
				     /* If ACKD=1, set OE1 to 1 also. */
#define USBAT_UIO_OE0		0x10 /* I/O 0 set=output/clr=input */
#define USBAT_UIO_ADPRST	0x01 /* Reset SCM chip */

/* USBAT Features */
#define USBAT_FEAT_ETEN	0x80	/* External trigger enable */
#define USBAT_FEAT_U1	0x08
#define USBAT_FEAT_U0	0x04
#define USBAT_FEAT_ET1	0x02
#define USBAT_FEAT_ET2	0x01

struct usbat_info {
	int devicetype;

	/* Used for Flash readers only */
	unsigned long sectors;     /* total sector count */
	unsigned long ssize;       /* sector size in bytes */

	unsigned char sense_key;
	unsigned long sense_asc;   /* additional sense code */
	unsigned long sense_ascq;  /* additional sense code qualifier */
};

#define short_pack(LSB,MSB) ( ((u16)(LSB)) | ( ((u16)(MSB))<<8 ) )
#define LSB_of(s) ((s)&0xFF)
#define MSB_of(s) ((s)>>8)

static int transferred = 0;

static int usbat_flash_transport(struct scsi_cmnd * srb, struct us_data *us);
static int usbat_hp8200e_transport(struct scsi_cmnd *srb, struct us_data *us);

static int init_usbat_cd(struct us_data *us);
static int init_usbat_flash(struct us_data *us);


/*
 * The table of devices
 */
#define UNUSUAL_DEV(id_vendor, id_product, bcdDeviceMin, bcdDeviceMax, \
		    vendorName, productName, useProtocol, useTransport, \
		    initFunction, flags) \
{ USB_DEVICE_VER(id_vendor, id_product, bcdDeviceMin, bcdDeviceMax), \
  .driver_info = (flags)|(USB_US_TYPE_STOR<<24) }

struct usb_device_id usbat_usb_ids[] = {
#	include "unusual_usbat.h"
	{ }		/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, usbat_usb_ids);

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

static struct us_unusual_dev usbat_unusual_dev_list[] = {
#	include "unusual_usbat.h"
	{ }		/* Terminating entry */
};

#undef UNUSUAL_DEV

/*
 * Convenience function to produce an ATA read/write sectors command
 * Use cmd=0x20 for read, cmd=0x30 for write
 */
static void usbat_pack_ata_sector_cmd(unsigned char *buf,
					unsigned char thistime,
					u32 sector, unsigned char cmd)
{
	buf[0] = 0;
	buf[1] = thistime;
	buf[2] = sector & 0xFF;
	buf[3] = (sector >>  8) & 0xFF;
	buf[4] = (sector >> 16) & 0xFF;
	buf[5] = 0xE0 | ((sector >> 24) & 0x0F);
	buf[6] = cmd;
}

/*
 * Convenience function to get the device type (flash or hp8200)
 */
static int usbat_get_device_type(struct us_data *us)
{
	return ((struct usbat_info*)us->extra)->devicetype;
}

/*
 * Read a register from the device
 */
static int usbat_read(struct us_data *us,
		      unsigned char access,
		      unsigned char reg,
		      unsigned char *content)
{
	return usb_stor_ctrl_transfer(us,
		us->recv_ctrl_pipe,
		access | USBAT_CMD_READ_REG,
		0xC0,
		(u16)reg,
		0,
		content,
		1);
}

/*
 * Write to a register on the device
 */
static int usbat_write(struct us_data *us,
		       unsigned char access,
		       unsigned char reg,
		       unsigned char content)
{
	return usb_stor_ctrl_transfer(us,
		us->send_ctrl_pipe,
		access | USBAT_CMD_WRITE_REG,
		0x40,
		short_pack(reg, content),
		0,
		NULL,
		0);
}

/*
 * Convenience function to perform a bulk read
 */
static int usbat_bulk_read(struct us_data *us,
			   void* buf,
			   unsigned int len,
			   int use_sg)
{
	if (len == 0)
		return USB_STOR_XFER_GOOD;

	US_DEBUGP("usbat_bulk_read: len = %d\n", len);
	return usb_stor_bulk_transfer_sg(us, us->recv_bulk_pipe, buf, len, use_sg, NULL);
}

/*
 * Convenience function to perform a bulk write
 */
static int usbat_bulk_write(struct us_data *us,
			    void* buf,
			    unsigned int len,
			    int use_sg)
{
	if (len == 0)
		return USB_STOR_XFER_GOOD;

	US_DEBUGP("usbat_bulk_write:  len = %d\n", len);
	return usb_stor_bulk_transfer_sg(us, us->send_bulk_pipe, buf, len, use_sg, NULL);
}

/*
 * Some USBAT-specific commands can only be executed over a command transport
 * This transport allows one (len=8) or two (len=16) vendor-specific commands
 * to be executed.
 */
static int usbat_execute_command(struct us_data *us,
								 unsigned char *commands,
								 unsigned int len)
{
	return usb_stor_ctrl_transfer(us, us->send_ctrl_pipe,
								  USBAT_CMD_EXEC_CMD, 0x40, 0, 0,
								  commands, len);
}

/*
 * Read the status register
 */
static int usbat_get_status(struct us_data *us, unsigned char *status)
{
	int rc;
	rc = usbat_read(us, USBAT_ATA, USBAT_ATA_STATUS, status);

	US_DEBUGP("usbat_get_status: 0x%02X\n", (unsigned short) (*status));
	return rc;
}

/*
 * Check the device status
 */
static int usbat_check_status(struct us_data *us)
{
	unsigned char *reply = us->iobuf;
	int rc;

	rc = usbat_get_status(us, reply);
	if (rc != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_FAILED;

	/* error/check condition (0x51 is ok) */
	if (*reply & 0x01 && *reply != 0x51)
		return USB_STOR_TRANSPORT_FAILED;

	/* device fault */
	if (*reply & 0x20)
		return USB_STOR_TRANSPORT_FAILED;

	return USB_STOR_TRANSPORT_GOOD;
}

/*
 * Stores critical information in internal registers in prepartion for the execution
 * of a conditional usbat_read_blocks or usbat_write_blocks call.
 */
static int usbat_set_shuttle_features(struct us_data *us,
				      unsigned char external_trigger,
				      unsigned char epp_control,
				      unsigned char mask_byte,
				      unsigned char test_pattern,
				      unsigned char subcountH,
				      unsigned char subcountL)
{
	unsigned char *command = us->iobuf;

	command[0] = 0x40;
	command[1] = USBAT_CMD_SET_FEAT;

	/*
	 * The only bit relevant to ATA access is bit 6
	 * which defines 8 bit data access (set) or 16 bit (unset)
	 */
	command[2] = epp_control;

	/*
	 * If FCQ is set in the qualifier (defined in R/W cmd), then bits U0, U1,
	 * ET1 and ET2 define an external event to be checked for on event of a
	 * _read_blocks or _write_blocks operation. The read/write will not take
	 * place unless the defined trigger signal is active.
	 */
	command[3] = external_trigger;

	/*
	 * The resultant byte of the mask operation (see mask_byte) is compared for
	 * equivalence with this test pattern. If equal, the read/write will take
	 * place.
	 */
	command[4] = test_pattern;

	/*
	 * This value is logically ANDed with the status register field specified
	 * in the read/write command.
	 */
	command[5] = mask_byte;

	/*
	 * If ALQ is set in the qualifier, this field contains the address of the
	 * registers where the byte count should be read for transferring the data.
	 * If ALQ is not set, then this field contains the number of bytes to be
	 * transferred.
	 */
	command[6] = subcountL;
	command[7] = subcountH;

	return usbat_execute_command(us, command, 8);
}

/*
 * Block, waiting for an ATA device to become not busy or to report
 * an error condition.
 */
static int usbat_wait_not_busy(struct us_data *us, int minutes)
{
	int i;
	int result;
	unsigned char *status = us->iobuf;

	/* Synchronizing cache on a CDR could take a heck of a long time,
	 * but probably not more than 10 minutes or so. On the other hand,
	 * doing a full blank on a CDRW at speed 1 will take about 75
	 * minutes!
	 */

	for (i=0; i<1200+minutes*60; i++) {

 		result = usbat_get_status(us, status);

		if (result!=USB_STOR_XFER_GOOD)
			return USB_STOR_TRANSPORT_ERROR;
		if (*status & 0x01) { /* check condition */
			result = usbat_read(us, USBAT_ATA, 0x10, status);
			return USB_STOR_TRANSPORT_FAILED;
		}
		if (*status & 0x20) /* device fault */
			return USB_STOR_TRANSPORT_FAILED;

		if ((*status & 0x80)==0x00) { /* not busy */
			US_DEBUGP("Waited not busy for %d steps\n", i);
			return USB_STOR_TRANSPORT_GOOD;
		}

		if (i<500)
			msleep(10); /* 5 seconds */
		else if (i<700)
			msleep(50); /* 10 seconds */
		else if (i<1200)
			msleep(100); /* 50 seconds */
		else
			msleep(1000); /* X minutes */
	}

	US_DEBUGP("Waited not busy for %d minutes, timing out.\n",
		minutes);
	return USB_STOR_TRANSPORT_FAILED;
}

/*
 * Read block data from the data register
 */
static int usbat_read_block(struct us_data *us,
			    void* buf,
			    unsigned short len,
			    int use_sg)
{
	int result;
	unsigned char *command = us->iobuf;

	if (!len)
		return USB_STOR_TRANSPORT_GOOD;

	command[0] = 0xC0;
	command[1] = USBAT_ATA | USBAT_CMD_READ_BLOCK;
	command[2] = USBAT_ATA_DATA;
	command[3] = 0;
	command[4] = 0;
	command[5] = 0;
	command[6] = LSB_of(len);
	command[7] = MSB_of(len);

	result = usbat_execute_command(us, command, 8);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	result = usbat_bulk_read(us, buf, len, use_sg);
	return (result == USB_STOR_XFER_GOOD ?
			USB_STOR_TRANSPORT_GOOD : USB_STOR_TRANSPORT_ERROR);
}

/*
 * Write block data via the data register
 */
static int usbat_write_block(struct us_data *us,
			     unsigned char access,
			     void* buf,
			     unsigned short len,
			     int minutes,
			     int use_sg)
{
	int result;
	unsigned char *command = us->iobuf;

	if (!len)
		return USB_STOR_TRANSPORT_GOOD;

	command[0] = 0x40;
	command[1] = access | USBAT_CMD_WRITE_BLOCK;
	command[2] = USBAT_ATA_DATA;
	command[3] = 0;
	command[4] = 0;
	command[5] = 0;
	command[6] = LSB_of(len);
	command[7] = MSB_of(len);

	result = usbat_execute_command(us, command, 8);

	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	result = usbat_bulk_write(us, buf, len, use_sg);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	return usbat_wait_not_busy(us, minutes);
}

/*
 * Process read and write requests
 */
static int usbat_hp8200e_rw_block_test(struct us_data *us,
				       unsigned char access,
				       unsigned char *registers,
				       unsigned char *data_out,
				       unsigned short num_registers,
				       unsigned char data_reg,
				       unsigned char status_reg,
				       unsigned char timeout,
				       unsigned char qualifier,
				       int direction,
				       void *buf,
				       unsigned short len,
				       int use_sg,
				       int minutes)
{
	int result;
	unsigned int pipe = (direction == DMA_FROM_DEVICE) ?
			us->recv_bulk_pipe : us->send_bulk_pipe;

	unsigned char *command = us->iobuf;
	int i, j;
	int cmdlen;
	unsigned char *data = us->iobuf;
	unsigned char *status = us->iobuf;

	BUG_ON(num_registers > US_IOBUF_SIZE/2);

	for (i=0; i<20; i++) {

		/*
		 * The first time we send the full command, which consists
		 * of downloading the SCSI command followed by downloading
		 * the data via a write-and-test.  Any other time we only
		 * send the command to download the data -- the SCSI command
		 * is still 'active' in some sense in the device.
		 * 
		 * We're only going to try sending the data 10 times. After
		 * that, we just return a failure.
		 */

		if (i==0) {
			cmdlen = 16;
			/*
			 * Write to multiple registers
			 * Not really sure the 0x07, 0x17, 0xfc, 0xe7 is
			 * necessary here, but that's what came out of the
			 * trace every single time.
			 */
			command[0] = 0x40;
			command[1] = access | USBAT_CMD_WRITE_REGS;
			command[2] = 0x07;
			command[3] = 0x17;
			command[4] = 0xFC;
			command[5] = 0xE7;
			command[6] = LSB_of(num_registers*2);
			command[7] = MSB_of(num_registers*2);
		} else
			cmdlen = 8;

		/* Conditionally read or write blocks */
		command[cmdlen-8] = (direction==DMA_TO_DEVICE ? 0x40 : 0xC0);
		command[cmdlen-7] = access |
				(direction==DMA_TO_DEVICE ?
				 USBAT_CMD_COND_WRITE_BLOCK : USBAT_CMD_COND_READ_BLOCK);
		command[cmdlen-6] = data_reg;
		command[cmdlen-5] = status_reg;
		command[cmdlen-4] = timeout;
		command[cmdlen-3] = qualifier;
		command[cmdlen-2] = LSB_of(len);
		command[cmdlen-1] = MSB_of(len);

		result = usbat_execute_command(us, command, cmdlen);

		if (result != USB_STOR_XFER_GOOD)
			return USB_STOR_TRANSPORT_ERROR;

		if (i==0) {

			for (j=0; j<num_registers; j++) {
				data[j<<1] = registers[j];
				data[1+(j<<1)] = data_out[j];
			}

			result = usbat_bulk_write(us, data, num_registers*2, 0);
			if (result != USB_STOR_XFER_GOOD)
				return USB_STOR_TRANSPORT_ERROR;

		}

		result = usb_stor_bulk_transfer_sg(us,
			pipe, buf, len, use_sg, NULL);

		/*
		 * If we get a stall on the bulk download, we'll retry
		 * the bulk download -- but not the SCSI command because
		 * in some sense the SCSI command is still 'active' and
		 * waiting for the data. Don't ask me why this should be;
		 * I'm only following what the Windoze driver did.
		 *
		 * Note that a stall for the test-and-read/write command means
		 * that the test failed. In this case we're testing to make
		 * sure that the device is error-free
		 * (i.e. bit 0 -- CHK -- of status is 0). The most likely
		 * hypothesis is that the USBAT chip somehow knows what
		 * the device will accept, but doesn't give the device any
		 * data until all data is received. Thus, the device would
		 * still be waiting for the first byte of data if a stall
		 * occurs, even if the stall implies that some data was
		 * transferred.
		 */

		if (result == USB_STOR_XFER_SHORT ||
				result == USB_STOR_XFER_STALLED) {

			/*
			 * If we're reading and we stalled, then clear
			 * the bulk output pipe only the first time.
			 */

			if (direction==DMA_FROM_DEVICE && i==0) {
				if (usb_stor_clear_halt(us,
						us->send_bulk_pipe) < 0)
					return USB_STOR_TRANSPORT_ERROR;
			}

			/*
			 * Read status: is the device angry, or just busy?
			 */

 			result = usbat_read(us, USBAT_ATA, 
				direction==DMA_TO_DEVICE ?
					USBAT_ATA_STATUS : USBAT_ATA_ALTSTATUS,
				status);

			if (result!=USB_STOR_XFER_GOOD)
				return USB_STOR_TRANSPORT_ERROR;
			if (*status & 0x01) /* check condition */
				return USB_STOR_TRANSPORT_FAILED;
			if (*status & 0x20) /* device fault */
				return USB_STOR_TRANSPORT_FAILED;

			US_DEBUGP("Redoing %s\n",
			  direction==DMA_TO_DEVICE ? "write" : "read");

		} else if (result != USB_STOR_XFER_GOOD)
			return USB_STOR_TRANSPORT_ERROR;
		else
			return usbat_wait_not_busy(us, minutes);

	}

	US_DEBUGP("Bummer! %s bulk data 20 times failed.\n",
		direction==DMA_TO_DEVICE ? "Writing" : "Reading");

	return USB_STOR_TRANSPORT_FAILED;
}

/*
 * Write to multiple registers:
 * Allows us to write specific data to any registers. The data to be written
 * gets packed in this sequence: reg0, data0, reg1, data1, ..., regN, dataN
 * which gets sent through bulk out.
 * Not designed for large transfers of data!
 */
static int usbat_multiple_write(struct us_data *us,
				unsigned char *registers,
				unsigned char *data_out,
				unsigned short num_registers)
{
	int i, result;
	unsigned char *data = us->iobuf;
	unsigned char *command = us->iobuf;

	BUG_ON(num_registers > US_IOBUF_SIZE/2);

	/* Write to multiple registers, ATA access */
	command[0] = 0x40;
	command[1] = USBAT_ATA | USBAT_CMD_WRITE_REGS;

	/* No relevance */
	command[2] = 0;
	command[3] = 0;
	command[4] = 0;
	command[5] = 0;

	/* Number of bytes to be transferred (incl. addresses and data) */
	command[6] = LSB_of(num_registers*2);
	command[7] = MSB_of(num_registers*2);

	/* The setup command */
	result = usbat_execute_command(us, command, 8);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	/* Create the reg/data, reg/data sequence */
	for (i=0; i<num_registers; i++) {
		data[i<<1] = registers[i];
		data[1+(i<<1)] = data_out[i];
	}

	/* Send the data */
	result = usbat_bulk_write(us, data, num_registers*2, 0);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	if (usbat_get_device_type(us) == USBAT_DEV_HP8200)
		return usbat_wait_not_busy(us, 0);
	else
		return USB_STOR_TRANSPORT_GOOD;
}

/*
 * Conditionally read blocks from device:
 * Allows us to read blocks from a specific data register, based upon the
 * condition that a status register can be successfully masked with a status
 * qualifier. If this condition is not initially met, the read will wait
 * up until a maximum amount of time has elapsed, as specified by timeout.
 * The read will start when the condition is met, otherwise the command aborts.
 *
 * The qualifier defined here is not the value that is masked, it defines
 * conditions for the write to take place. The actual masked qualifier (and
 * other related details) are defined beforehand with _set_shuttle_features().
 */
static int usbat_read_blocks(struct us_data *us,
			     void* buffer,
			     int len,
			     int use_sg)
{
	int result;
	unsigned char *command = us->iobuf;

	command[0] = 0xC0;
	command[1] = USBAT_ATA | USBAT_CMD_COND_READ_BLOCK;
	command[2] = USBAT_ATA_DATA;
	command[3] = USBAT_ATA_STATUS;
	command[4] = 0xFD; /* Timeout (ms); */
	command[5] = USBAT_QUAL_FCQ;
	command[6] = LSB_of(len);
	command[7] = MSB_of(len);

	/* Multiple block read setup command */
	result = usbat_execute_command(us, command, 8);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_FAILED;
	
	/* Read the blocks we just asked for */
	result = usbat_bulk_read(us, buffer, len, use_sg);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_FAILED;

	return USB_STOR_TRANSPORT_GOOD;
}

/*
 * Conditionally write blocks to device:
 * Allows us to write blocks to a specific data register, based upon the
 * condition that a status register can be successfully masked with a status
 * qualifier. If this condition is not initially met, the write will wait
 * up until a maximum amount of time has elapsed, as specified by timeout.
 * The read will start when the condition is met, otherwise the command aborts.
 *
 * The qualifier defined here is not the value that is masked, it defines
 * conditions for the write to take place. The actual masked qualifier (and
 * other related details) are defined beforehand with _set_shuttle_features().
 */
static int usbat_write_blocks(struct us_data *us,
			      void* buffer,
			      int len,
			      int use_sg)
{
	int result;
	unsigned char *command = us->iobuf;

	command[0] = 0x40;
	command[1] = USBAT_ATA | USBAT_CMD_COND_WRITE_BLOCK;
	command[2] = USBAT_ATA_DATA;
	command[3] = USBAT_ATA_STATUS;
	command[4] = 0xFD; /* Timeout (ms) */
	command[5] = USBAT_QUAL_FCQ;
	command[6] = LSB_of(len);
	command[7] = MSB_of(len);

	/* Multiple block write setup command */
	result = usbat_execute_command(us, command, 8);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_FAILED;
	
	/* Write the data */
	result = usbat_bulk_write(us, buffer, len, use_sg);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_FAILED;

	return USB_STOR_TRANSPORT_GOOD;
}

/*
 * Read the User IO register
 */
static int usbat_read_user_io(struct us_data *us, unsigned char *data_flags)
{
	int result;

	result = usb_stor_ctrl_transfer(us,
		us->recv_ctrl_pipe,
		USBAT_CMD_UIO,
		0xC0,
		0,
		0,
		data_flags,
		USBAT_UIO_READ);

	US_DEBUGP("usbat_read_user_io: UIO register reads %02X\n", (unsigned short) (*data_flags));

	return result;
}

/*
 * Write to the User IO register
 */
static int usbat_write_user_io(struct us_data *us,
			       unsigned char enable_flags,
			       unsigned char data_flags)
{
	return usb_stor_ctrl_transfer(us,
		us->send_ctrl_pipe,
		USBAT_CMD_UIO,
		0x40,
		short_pack(enable_flags, data_flags),
		0,
		NULL,
		USBAT_UIO_WRITE);
}

/*
 * Reset the device
 * Often needed on media change.
 */
static int usbat_device_reset(struct us_data *us)
{
	int rc;

	/*
	 * Reset peripheral, enable peripheral control signals
	 * (bring reset signal up)
	 */
	rc = usbat_write_user_io(us,
							 USBAT_UIO_DRVRST | USBAT_UIO_OE1 | USBAT_UIO_OE0,
							 USBAT_UIO_EPAD | USBAT_UIO_1);
	if (rc != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;
			
	/*
	 * Enable peripheral control signals
	 * (bring reset signal down)
	 */
	rc = usbat_write_user_io(us,
							 USBAT_UIO_OE1  | USBAT_UIO_OE0,
							 USBAT_UIO_EPAD | USBAT_UIO_1);
	if (rc != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	return USB_STOR_TRANSPORT_GOOD;
}

/*
 * Enable card detect
 */
static int usbat_device_enable_cdt(struct us_data *us)
{
	int rc;

	/* Enable peripheral control signals and card detect */
	rc = usbat_write_user_io(us,
							 USBAT_UIO_ACKD | USBAT_UIO_OE1  | USBAT_UIO_OE0,
							 USBAT_UIO_EPAD | USBAT_UIO_1);
	if (rc != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	return USB_STOR_TRANSPORT_GOOD;
}

/*
 * Determine if media is present.
 */
static int usbat_flash_check_media_present(unsigned char *uio)
{
	if (*uio & USBAT_UIO_UI0) {
		US_DEBUGP("usbat_flash_check_media_present: no media detected\n");
		return USBAT_FLASH_MEDIA_NONE;
	}

	return USBAT_FLASH_MEDIA_CF;
}

/*
 * Determine if media has changed since last operation
 */
static int usbat_flash_check_media_changed(unsigned char *uio)
{
	if (*uio & USBAT_UIO_0) {
		US_DEBUGP("usbat_flash_check_media_changed: media change detected\n");
		return USBAT_FLASH_MEDIA_CHANGED;
	}

	return USBAT_FLASH_MEDIA_SAME;
}

/*
 * Check for media change / no media and handle the situation appropriately
 */
static int usbat_flash_check_media(struct us_data *us,
				   struct usbat_info *info)
{
	int rc;
	unsigned char *uio = us->iobuf;

	rc = usbat_read_user_io(us, uio);
	if (rc != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	/* Check for media existence */
	rc = usbat_flash_check_media_present(uio);
	if (rc == USBAT_FLASH_MEDIA_NONE) {
		info->sense_key = 0x02;
		info->sense_asc = 0x3A;
		info->sense_ascq = 0x00;
		return USB_STOR_TRANSPORT_FAILED;
	}

	/* Check for media change */
	rc = usbat_flash_check_media_changed(uio);
	if (rc == USBAT_FLASH_MEDIA_CHANGED) {

		/* Reset and re-enable card detect */
		rc = usbat_device_reset(us);
		if (rc != USB_STOR_TRANSPORT_GOOD)
			return rc;
		rc = usbat_device_enable_cdt(us);
		if (rc != USB_STOR_TRANSPORT_GOOD)
			return rc;

		msleep(50);

		rc = usbat_read_user_io(us, uio);
		if (rc != USB_STOR_XFER_GOOD)
			return USB_STOR_TRANSPORT_ERROR;
		
		info->sense_key = UNIT_ATTENTION;
		info->sense_asc = 0x28;
		info->sense_ascq = 0x00;
		return USB_STOR_TRANSPORT_FAILED;
	}

	return USB_STOR_TRANSPORT_GOOD;
}

/*
 * Determine whether we are controlling a flash-based reader/writer,
 * or a HP8200-based CD drive.
 * Sets transport functions as appropriate.
 */
static int usbat_identify_device(struct us_data *us,
				 struct usbat_info *info)
{
	int rc;
	unsigned char status;

	if (!us || !info)
		return USB_STOR_TRANSPORT_ERROR;

	rc = usbat_device_reset(us);
	if (rc != USB_STOR_TRANSPORT_GOOD)
		return rc;
	msleep(500);

	/*
	 * In attempt to distinguish between HP CDRW's and Flash readers, we now
	 * execute the IDENTIFY PACKET DEVICE command. On ATA devices (i.e. flash
	 * readers), this command should fail with error. On ATAPI devices (i.e.
	 * CDROM drives), it should succeed.
	 */
	rc = usbat_write(us, USBAT_ATA, USBAT_ATA_CMD, 0xA1);
 	if (rc != USB_STOR_XFER_GOOD)
 		return USB_STOR_TRANSPORT_ERROR;

	rc = usbat_get_status(us, &status);
 	if (rc != USB_STOR_XFER_GOOD)
 		return USB_STOR_TRANSPORT_ERROR;

	/* Check for error bit, or if the command 'fell through' */
	if (status == 0xA1 || !(status & 0x01)) {
		/* Device is HP 8200 */
		US_DEBUGP("usbat_identify_device: Detected HP8200 CDRW\n");
		info->devicetype = USBAT_DEV_HP8200;
	} else {
		/* Device is a CompactFlash reader/writer */
		US_DEBUGP("usbat_identify_device: Detected Flash reader/writer\n");
		info->devicetype = USBAT_DEV_FLASH;
	}

	return USB_STOR_TRANSPORT_GOOD;
}

/*
 * Set the transport function based on the device type
 */
static int usbat_set_transport(struct us_data *us,
			       struct usbat_info *info,
			       int devicetype)
{

	if (!info->devicetype)
		info->devicetype = devicetype;

	if (!info->devicetype)
		usbat_identify_device(us, info);

	switch (info->devicetype) {
	default:
		return USB_STOR_TRANSPORT_ERROR;

	case  USBAT_DEV_HP8200:
		us->transport = usbat_hp8200e_transport;
		break;

	case USBAT_DEV_FLASH:
		us->transport = usbat_flash_transport;
		break;
	}

	return 0;
}

/*
 * Read the media capacity
 */
static int usbat_flash_get_sector_count(struct us_data *us,
					struct usbat_info *info)
{
	unsigned char registers[3] = {
		USBAT_ATA_SECCNT,
		USBAT_ATA_DEVICE,
		USBAT_ATA_CMD,
	};
	unsigned char  command[3] = { 0x01, 0xA0, 0xEC };
	unsigned char *reply;
	unsigned char status;
	int rc;

	if (!us || !info)
		return USB_STOR_TRANSPORT_ERROR;

	reply = kmalloc(512, GFP_NOIO);
	if (!reply)
		return USB_STOR_TRANSPORT_ERROR;

	/* ATA command : IDENTIFY DEVICE */
	rc = usbat_multiple_write(us, registers, command, 3);
	if (rc != USB_STOR_XFER_GOOD) {
		US_DEBUGP("usbat_flash_get_sector_count: Gah! identify_device failed\n");
		rc = USB_STOR_TRANSPORT_ERROR;
		goto leave;
	}

	/* Read device status */
	if (usbat_get_status(us, &status) != USB_STOR_XFER_GOOD) {
		rc = USB_STOR_TRANSPORT_ERROR;
		goto leave;
	}

	msleep(100);

	/* Read the device identification data */
	rc = usbat_read_block(us, reply, 512, 0);
	if (rc != USB_STOR_TRANSPORT_GOOD)
		goto leave;

	info->sectors = ((u32)(reply[117]) << 24) |
		((u32)(reply[116]) << 16) |
		((u32)(reply[115]) <<  8) |
		((u32)(reply[114])      );

	rc = USB_STOR_TRANSPORT_GOOD;

 leave:
	kfree(reply);
	return rc;
}

/*
 * Read data from device
 */
static int usbat_flash_read_data(struct us_data *us,
								 struct usbat_info *info,
								 u32 sector,
								 u32 sectors)
{
	unsigned char registers[7] = {
		USBAT_ATA_FEATURES,
		USBAT_ATA_SECCNT,
		USBAT_ATA_SECNUM,
		USBAT_ATA_LBA_ME,
		USBAT_ATA_LBA_HI,
		USBAT_ATA_DEVICE,
		USBAT_ATA_STATUS,
	};
	unsigned char command[7];
	unsigned char *buffer;
	unsigned char  thistime;
	unsigned int totallen, alloclen;
	int len, result;
	unsigned int sg_offset = 0;
	struct scatterlist *sg = NULL;

	result = usbat_flash_check_media(us, info);
	if (result != USB_STOR_TRANSPORT_GOOD)
		return result;

	/*
	 * we're working in LBA mode.  according to the ATA spec,
	 * we can support up to 28-bit addressing.  I don't know if Jumpshot
	 * supports beyond 24-bit addressing.  It's kind of hard to test
	 * since it requires > 8GB CF card.
	 */

	if (sector > 0x0FFFFFFF)
		return USB_STOR_TRANSPORT_ERROR;

	totallen = sectors * info->ssize;

	/*
	 * Since we don't read more than 64 KB at a time, we have to create
	 * a bounce buffer and move the data a piece at a time between the
	 * bounce buffer and the actual transfer buffer.
	 */

	alloclen = min(totallen, 65536u);
	buffer = kmalloc(alloclen, GFP_NOIO);
	if (buffer == NULL)
		return USB_STOR_TRANSPORT_ERROR;

	do {
		/*
		 * loop, never allocate or transfer more than 64k at once
		 * (min(128k, 255*info->ssize) is the real limit)
		 */
		len = min(totallen, alloclen);
		thistime = (len / info->ssize) & 0xff;
 
		/* ATA command 0x20 (READ SECTORS) */
		usbat_pack_ata_sector_cmd(command, thistime, sector, 0x20);

		/* Write/execute ATA read command */
		result = usbat_multiple_write(us, registers, command, 7);
		if (result != USB_STOR_TRANSPORT_GOOD)
			goto leave;

		/* Read the data we just requested */
		result = usbat_read_blocks(us, buffer, len, 0);
		if (result != USB_STOR_TRANSPORT_GOOD)
			goto leave;
  	 
		US_DEBUGP("usbat_flash_read_data:  %d bytes\n", len);
	
		/* Store the data in the transfer buffer */
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

/*
 * Write data to device
 */
static int usbat_flash_write_data(struct us_data *us,
								  struct usbat_info *info,
								  u32 sector,
								  u32 sectors)
{
	unsigned char registers[7] = {
		USBAT_ATA_FEATURES,
		USBAT_ATA_SECCNT,
		USBAT_ATA_SECNUM,
		USBAT_ATA_LBA_ME,
		USBAT_ATA_LBA_HI,
		USBAT_ATA_DEVICE,
		USBAT_ATA_STATUS,
	};
	unsigned char command[7];
	unsigned char *buffer;
	unsigned char  thistime;
	unsigned int totallen, alloclen;
	int len, result;
	unsigned int sg_offset = 0;
	struct scatterlist *sg = NULL;

	result = usbat_flash_check_media(us, info);
	if (result != USB_STOR_TRANSPORT_GOOD)
		return result;

	/*
	 * we're working in LBA mode.  according to the ATA spec,
	 * we can support up to 28-bit addressing.  I don't know if the device
	 * supports beyond 24-bit addressing.  It's kind of hard to test
	 * since it requires > 8GB media.
	 */

	if (sector > 0x0FFFFFFF)
		return USB_STOR_TRANSPORT_ERROR;

	totallen = sectors * info->ssize;

	/*
	 * Since we don't write more than 64 KB at a time, we have to create
	 * a bounce buffer and move the data a piece at a time between the
	 * bounce buffer and the actual transfer buffer.
	 */

	alloclen = min(totallen, 65536u);
	buffer = kmalloc(alloclen, GFP_NOIO);
	if (buffer == NULL)
		return USB_STOR_TRANSPORT_ERROR;

	do {
		/*
		 * loop, never allocate or transfer more than 64k at once
		 * (min(128k, 255*info->ssize) is the real limit)
		 */
		len = min(totallen, alloclen);
		thistime = (len / info->ssize) & 0xff;

		/* Get the data from the transfer buffer */
		usb_stor_access_xfer_buf(buffer, len, us->srb,
					 &sg, &sg_offset, FROM_XFER_BUF);

		/* ATA command 0x30 (WRITE SECTORS) */
		usbat_pack_ata_sector_cmd(command, thistime, sector, 0x30);

		/* Write/execute ATA write command */
		result = usbat_multiple_write(us, registers, command, 7);
		if (result != USB_STOR_TRANSPORT_GOOD)
			goto leave;

		/* Write the data */
		result = usbat_write_blocks(us, buffer, len, 0);
		if (result != USB_STOR_TRANSPORT_GOOD)
			goto leave;

		sector += thistime;
		totallen -= len;
	} while (totallen > 0);

	kfree(buffer);
	return result;

leave:
	kfree(buffer);
	return USB_STOR_TRANSPORT_ERROR;
}

/*
 * Squeeze a potentially huge (> 65535 byte) read10 command into
 * a little ( <= 65535 byte) ATAPI pipe
 */
static int usbat_hp8200e_handle_read10(struct us_data *us,
				       unsigned char *registers,
				       unsigned char *data,
				       struct scsi_cmnd *srb)
{
	int result = USB_STOR_TRANSPORT_GOOD;
	unsigned char *buffer;
	unsigned int len;
	unsigned int sector;
	unsigned int sg_offset = 0;
	struct scatterlist *sg = NULL;

	US_DEBUGP("handle_read10: transfersize %d\n",
		srb->transfersize);

	if (scsi_bufflen(srb) < 0x10000) {

		result = usbat_hp8200e_rw_block_test(us, USBAT_ATA, 
			registers, data, 19,
			USBAT_ATA_DATA, USBAT_ATA_STATUS, 0xFD,
			(USBAT_QUAL_FCQ | USBAT_QUAL_ALQ),
			DMA_FROM_DEVICE,
			scsi_sglist(srb),
			scsi_bufflen(srb), scsi_sg_count(srb), 1);

		return result;
	}

	/*
	 * Since we're requesting more data than we can handle in
	 * a single read command (max is 64k-1), we will perform
	 * multiple reads, but each read must be in multiples of
	 * a sector.  Luckily the sector size is in srb->transfersize
	 * (see linux/drivers/scsi/sr.c).
	 */

	if (data[7+0] == GPCMD_READ_CD) {
		len = short_pack(data[7+9], data[7+8]);
		len <<= 16;
		len |= data[7+7];
		US_DEBUGP("handle_read10: GPCMD_READ_CD: len %d\n", len);
		srb->transfersize = scsi_bufflen(srb)/len;
	}

	if (!srb->transfersize)  {
		srb->transfersize = 2048; /* A guess */
		US_DEBUGP("handle_read10: transfersize 0, forcing %d\n",
			srb->transfersize);
	}

	/*
	 * Since we only read in one block at a time, we have to create
	 * a bounce buffer and move the data a piece at a time between the
	 * bounce buffer and the actual transfer buffer.
	 */

	len = (65535/srb->transfersize) * srb->transfersize;
	US_DEBUGP("Max read is %d bytes\n", len);
	len = min(len, scsi_bufflen(srb));
	buffer = kmalloc(len, GFP_NOIO);
	if (buffer == NULL) /* bloody hell! */
		return USB_STOR_TRANSPORT_FAILED;
	sector = short_pack(data[7+3], data[7+2]);
	sector <<= 16;
	sector |= short_pack(data[7+5], data[7+4]);
	transferred = 0;

	while (transferred != scsi_bufflen(srb)) {

		if (len > scsi_bufflen(srb) - transferred)
			len = scsi_bufflen(srb) - transferred;

		data[3] = len&0xFF; 	  /* (cylL) = expected length (L) */
		data[4] = (len>>8)&0xFF;  /* (cylH) = expected length (H) */

		/* Fix up the SCSI command sector and num sectors */

		data[7+2] = MSB_of(sector>>16); /* SCSI command sector */
		data[7+3] = LSB_of(sector>>16);
		data[7+4] = MSB_of(sector&0xFFFF);
		data[7+5] = LSB_of(sector&0xFFFF);
		if (data[7+0] == GPCMD_READ_CD)
			data[7+6] = 0;
		data[7+7] = MSB_of(len / srb->transfersize); /* SCSI command */
		data[7+8] = LSB_of(len / srb->transfersize); /* num sectors */

		result = usbat_hp8200e_rw_block_test(us, USBAT_ATA, 
			registers, data, 19,
			USBAT_ATA_DATA, USBAT_ATA_STATUS, 0xFD, 
			(USBAT_QUAL_FCQ | USBAT_QUAL_ALQ),
			DMA_FROM_DEVICE,
			buffer,
			len, 0, 1);

		if (result != USB_STOR_TRANSPORT_GOOD)
			break;

		/* Store the data in the transfer buffer */
		usb_stor_access_xfer_buf(buffer, len, srb,
				 &sg, &sg_offset, TO_XFER_BUF);

		/* Update the amount transferred and the sector number */

		transferred += len;
		sector += len / srb->transfersize;

	} /* while transferred != scsi_bufflen(srb) */

	kfree(buffer);
	return result;
}

static int usbat_select_and_test_registers(struct us_data *us)
{
	int selector;
	unsigned char *status = us->iobuf;

	/* try device = master, then device = slave. */
	for (selector = 0xA0; selector <= 0xB0; selector += 0x10) {
		if (usbat_write(us, USBAT_ATA, USBAT_ATA_DEVICE, selector) !=
				USB_STOR_XFER_GOOD)
			return USB_STOR_TRANSPORT_ERROR;

		if (usbat_read(us, USBAT_ATA, USBAT_ATA_STATUS, status) != 
				USB_STOR_XFER_GOOD)
			return USB_STOR_TRANSPORT_ERROR;

		if (usbat_read(us, USBAT_ATA, USBAT_ATA_DEVICE, status) != 
				USB_STOR_XFER_GOOD)
			return USB_STOR_TRANSPORT_ERROR;

		if (usbat_read(us, USBAT_ATA, USBAT_ATA_LBA_ME, status) != 
				USB_STOR_XFER_GOOD)
			return USB_STOR_TRANSPORT_ERROR;

		if (usbat_read(us, USBAT_ATA, USBAT_ATA_LBA_HI, status) != 
				USB_STOR_XFER_GOOD)
			return USB_STOR_TRANSPORT_ERROR;

		if (usbat_write(us, USBAT_ATA, USBAT_ATA_LBA_ME, 0x55) != 
				USB_STOR_XFER_GOOD)
			return USB_STOR_TRANSPORT_ERROR;

		if (usbat_write(us, USBAT_ATA, USBAT_ATA_LBA_HI, 0xAA) != 
				USB_STOR_XFER_GOOD)
			return USB_STOR_TRANSPORT_ERROR;

		if (usbat_read(us, USBAT_ATA, USBAT_ATA_LBA_ME, status) != 
				USB_STOR_XFER_GOOD)
			return USB_STOR_TRANSPORT_ERROR;

		if (usbat_read(us, USBAT_ATA, USBAT_ATA_LBA_ME, status) != 
				USB_STOR_XFER_GOOD)
			return USB_STOR_TRANSPORT_ERROR;
	}

	return USB_STOR_TRANSPORT_GOOD;
}

/*
 * Initialize the USBAT processor and the storage device
 */
static int init_usbat(struct us_data *us, int devicetype)
{
	int rc;
	struct usbat_info *info;
	unsigned char subcountH = USBAT_ATA_LBA_HI;
	unsigned char subcountL = USBAT_ATA_LBA_ME;
	unsigned char *status = us->iobuf;

	us->extra = kzalloc(sizeof(struct usbat_info), GFP_NOIO);
	if (!us->extra) {
		US_DEBUGP("init_usbat: Gah! Can't allocate storage for usbat info struct!\n");
		return 1;
	}
	info = (struct usbat_info *) (us->extra);

	/* Enable peripheral control signals */
	rc = usbat_write_user_io(us,
				 USBAT_UIO_OE1 | USBAT_UIO_OE0,
				 USBAT_UIO_EPAD | USBAT_UIO_1);
	if (rc != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	US_DEBUGP("INIT 1\n");

	msleep(2000);

	rc = usbat_read_user_io(us, status);
	if (rc != USB_STOR_TRANSPORT_GOOD)
		return rc;

	US_DEBUGP("INIT 2\n");

	rc = usbat_read_user_io(us, status);
	if (rc != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	rc = usbat_read_user_io(us, status);
	if (rc != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	US_DEBUGP("INIT 3\n");

	rc = usbat_select_and_test_registers(us);
	if (rc != USB_STOR_TRANSPORT_GOOD)
		return rc;

	US_DEBUGP("INIT 4\n");

	rc = usbat_read_user_io(us, status);
	if (rc != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	US_DEBUGP("INIT 5\n");

	/* Enable peripheral control signals and card detect */
	rc = usbat_device_enable_cdt(us);
	if (rc != USB_STOR_TRANSPORT_GOOD)
		return rc;

	US_DEBUGP("INIT 6\n");

	rc = usbat_read_user_io(us, status);
	if (rc != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	US_DEBUGP("INIT 7\n");

	msleep(1400);

	rc = usbat_read_user_io(us, status);
	if (rc != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	US_DEBUGP("INIT 8\n");

	rc = usbat_select_and_test_registers(us);
	if (rc != USB_STOR_TRANSPORT_GOOD)
		return rc;

	US_DEBUGP("INIT 9\n");

	/* At this point, we need to detect which device we are using */
	if (usbat_set_transport(us, info, devicetype))
		return USB_STOR_TRANSPORT_ERROR;

	US_DEBUGP("INIT 10\n");

	if (usbat_get_device_type(us) == USBAT_DEV_FLASH) { 
		subcountH = 0x02;
		subcountL = 0x00;
	}
	rc = usbat_set_shuttle_features(us, (USBAT_FEAT_ETEN | USBAT_FEAT_ET2 | USBAT_FEAT_ET1),
									0x00, 0x88, 0x08, subcountH, subcountL);
	if (rc != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	US_DEBUGP("INIT 11\n");

	return USB_STOR_TRANSPORT_GOOD;
}

/*
 * Transport for the HP 8200e
 */
static int usbat_hp8200e_transport(struct scsi_cmnd *srb, struct us_data *us)
{
	int result;
	unsigned char *status = us->iobuf;
	unsigned char registers[32];
	unsigned char data[32];
	unsigned int len;
	int i;

	len = scsi_bufflen(srb);

	/* Send A0 (ATA PACKET COMMAND).
	   Note: I guess we're never going to get any of the ATA
	   commands... just ATA Packet Commands.
 	 */

	registers[0] = USBAT_ATA_FEATURES;
	registers[1] = USBAT_ATA_SECCNT;
	registers[2] = USBAT_ATA_SECNUM;
	registers[3] = USBAT_ATA_LBA_ME;
	registers[4] = USBAT_ATA_LBA_HI;
	registers[5] = USBAT_ATA_DEVICE;
	registers[6] = USBAT_ATA_CMD;
	data[0] = 0x00;
	data[1] = 0x00;
	data[2] = 0x00;
	data[3] = len&0xFF; 		/* (cylL) = expected length (L) */
	data[4] = (len>>8)&0xFF; 	/* (cylH) = expected length (H) */
	data[5] = 0xB0; 		/* (device sel) = slave */
	data[6] = 0xA0; 		/* (command) = ATA PACKET COMMAND */

	for (i=7; i<19; i++) {
		registers[i] = 0x10;
		data[i] = (i-7 >= srb->cmd_len) ? 0 : srb->cmnd[i-7];
	}

	result = usbat_get_status(us, status);
	US_DEBUGP("Status = %02X\n", *status);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;
	if (srb->cmnd[0] == TEST_UNIT_READY)
		transferred = 0;

	if (srb->sc_data_direction == DMA_TO_DEVICE) {

		result = usbat_hp8200e_rw_block_test(us, USBAT_ATA, 
			registers, data, 19,
			USBAT_ATA_DATA, USBAT_ATA_STATUS, 0xFD,
			(USBAT_QUAL_FCQ | USBAT_QUAL_ALQ),
			DMA_TO_DEVICE,
			scsi_sglist(srb),
			len, scsi_sg_count(srb), 10);

		if (result == USB_STOR_TRANSPORT_GOOD) {
			transferred += len;
			US_DEBUGP("Wrote %08X bytes\n", transferred);
		}

		return result;

	} else if (srb->cmnd[0] == READ_10 ||
		   srb->cmnd[0] == GPCMD_READ_CD) {

		return usbat_hp8200e_handle_read10(us, registers, data, srb);

	}

	if (len > 0xFFFF) {
		US_DEBUGP("Error: len = %08X... what do I do now?\n",
			len);
		return USB_STOR_TRANSPORT_ERROR;
	}

	result = usbat_multiple_write(us, registers, data, 7);

	if (result != USB_STOR_TRANSPORT_GOOD)
		return result;

	/*
	 * Write the 12-byte command header.
	 *
	 * If the command is BLANK then set the timer for 75 minutes.
	 * Otherwise set it for 10 minutes.
	 *
	 * NOTE: THE 8200 DOCUMENTATION STATES THAT BLANKING A CDRW
	 * AT SPEED 4 IS UNRELIABLE!!!
	 */

	result = usbat_write_block(us, USBAT_ATA, srb->cmnd, 12,
				   srb->cmnd[0] == GPCMD_BLANK ? 75 : 10, 0);

	if (result != USB_STOR_TRANSPORT_GOOD)
		return result;

	/* If there is response data to be read in then do it here. */

	if (len != 0 && (srb->sc_data_direction == DMA_FROM_DEVICE)) {

		/* How many bytes to read in? Check cylL register */

		if (usbat_read(us, USBAT_ATA, USBAT_ATA_LBA_ME, status) != 
		    	USB_STOR_XFER_GOOD) {
			return USB_STOR_TRANSPORT_ERROR;
		}

		if (len > 0xFF) { /* need to read cylH also */
			len = *status;
			if (usbat_read(us, USBAT_ATA, USBAT_ATA_LBA_HI, status) !=
				    USB_STOR_XFER_GOOD) {
				return USB_STOR_TRANSPORT_ERROR;
			}
			len += ((unsigned int) *status)<<8;
		}
		else
			len = *status;


		result = usbat_read_block(us, scsi_sglist(srb), len,
			                                   scsi_sg_count(srb));
	}

	return result;
}

/*
 * Transport for USBAT02-based CompactFlash and similar storage devices
 */
static int usbat_flash_transport(struct scsi_cmnd * srb, struct us_data *us)
{
	int rc;
	struct usbat_info *info = (struct usbat_info *) (us->extra);
	unsigned long block, blocks;
	unsigned char *ptr = us->iobuf;
	static unsigned char inquiry_response[36] = {
		0x00, 0x80, 0x00, 0x01, 0x1F, 0x00, 0x00, 0x00
	};

	if (srb->cmnd[0] == INQUIRY) {
		US_DEBUGP("usbat_flash_transport: INQUIRY. Returning bogus response.\n");
		memcpy(ptr, inquiry_response, sizeof(inquiry_response));
		fill_inquiry_response(us, ptr, 36);
		return USB_STOR_TRANSPORT_GOOD;
	}

	if (srb->cmnd[0] == READ_CAPACITY) {
		rc = usbat_flash_check_media(us, info);
		if (rc != USB_STOR_TRANSPORT_GOOD)
			return rc;

		rc = usbat_flash_get_sector_count(us, info);
		if (rc != USB_STOR_TRANSPORT_GOOD)
			return rc;

		/* hard coded 512 byte sectors as per ATA spec */
		info->ssize = 0x200;
		US_DEBUGP("usbat_flash_transport: READ_CAPACITY: %ld sectors, %ld bytes per sector\n",
			  info->sectors, info->ssize);

		/*
		 * build the reply
		 * note: must return the sector number of the last sector,
		 * *not* the total number of sectors
		 */
		((__be32 *) ptr)[0] = cpu_to_be32(info->sectors - 1);
		((__be32 *) ptr)[1] = cpu_to_be32(info->ssize);
		usb_stor_set_xfer_buf(ptr, 8, srb);

		return USB_STOR_TRANSPORT_GOOD;
	}

	if (srb->cmnd[0] == MODE_SELECT_10) {
		US_DEBUGP("usbat_flash_transport:  Gah! MODE_SELECT_10.\n");
		return USB_STOR_TRANSPORT_ERROR;
	}

	if (srb->cmnd[0] == READ_10) {
		block = ((u32)(srb->cmnd[2]) << 24) | ((u32)(srb->cmnd[3]) << 16) |
				((u32)(srb->cmnd[4]) <<  8) | ((u32)(srb->cmnd[5]));

		blocks = ((u32)(srb->cmnd[7]) << 8) | ((u32)(srb->cmnd[8]));

		US_DEBUGP("usbat_flash_transport:  READ_10: read block 0x%04lx  count %ld\n", block, blocks);
		return usbat_flash_read_data(us, info, block, blocks);
	}

	if (srb->cmnd[0] == READ_12) {
		/*
		 * I don't think we'll ever see a READ_12 but support it anyway
		 */
		block = ((u32)(srb->cmnd[2]) << 24) | ((u32)(srb->cmnd[3]) << 16) |
		        ((u32)(srb->cmnd[4]) <<  8) | ((u32)(srb->cmnd[5]));

		blocks = ((u32)(srb->cmnd[6]) << 24) | ((u32)(srb->cmnd[7]) << 16) |
		         ((u32)(srb->cmnd[8]) <<  8) | ((u32)(srb->cmnd[9]));

		US_DEBUGP("usbat_flash_transport: READ_12: read block 0x%04lx  count %ld\n", block, blocks);
		return usbat_flash_read_data(us, info, block, blocks);
	}

	if (srb->cmnd[0] == WRITE_10) {
		block = ((u32)(srb->cmnd[2]) << 24) | ((u32)(srb->cmnd[3]) << 16) |
		        ((u32)(srb->cmnd[4]) <<  8) | ((u32)(srb->cmnd[5]));

		blocks = ((u32)(srb->cmnd[7]) << 8) | ((u32)(srb->cmnd[8]));

		US_DEBUGP("usbat_flash_transport: WRITE_10: write block 0x%04lx  count %ld\n", block, blocks);
		return usbat_flash_write_data(us, info, block, blocks);
	}

	if (srb->cmnd[0] == WRITE_12) {
		/*
		 * I don't think we'll ever see a WRITE_12 but support it anyway
		 */
		block = ((u32)(srb->cmnd[2]) << 24) | ((u32)(srb->cmnd[3]) << 16) |
		        ((u32)(srb->cmnd[4]) <<  8) | ((u32)(srb->cmnd[5]));

		blocks = ((u32)(srb->cmnd[6]) << 24) | ((u32)(srb->cmnd[7]) << 16) |
		         ((u32)(srb->cmnd[8]) <<  8) | ((u32)(srb->cmnd[9]));

		US_DEBUGP("usbat_flash_transport: WRITE_12: write block 0x%04lx  count %ld\n", block, blocks);
		return usbat_flash_write_data(us, info, block, blocks);
	}


	if (srb->cmnd[0] == TEST_UNIT_READY) {
		US_DEBUGP("usbat_flash_transport: TEST_UNIT_READY.\n");

		rc = usbat_flash_check_media(us, info);
		if (rc != USB_STOR_TRANSPORT_GOOD)
			return rc;

		return usbat_check_status(us);
	}

	if (srb->cmnd[0] == REQUEST_SENSE) {
		US_DEBUGP("usbat_flash_transport: REQUEST_SENSE.\n");

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

	US_DEBUGP("usbat_flash_transport: Gah! Unknown command: %d (0x%x)\n",
			  srb->cmnd[0], srb->cmnd[0]);
	info->sense_key = 0x05;
	info->sense_asc = 0x20;
	info->sense_ascq = 0x00;
	return USB_STOR_TRANSPORT_FAILED;
}

static int init_usbat_cd(struct us_data *us)
{
	return init_usbat(us, USBAT_DEV_HP8200);
}

static int init_usbat_flash(struct us_data *us)
{
	return init_usbat(us, USBAT_DEV_FLASH);
}

static int usbat_probe(struct usb_interface *intf,
			 const struct usb_device_id *id)
{
	struct us_data *us;
	int result;

	result = usb_stor_probe1(&us, intf, id,
			(id - usbat_usb_ids) + usbat_unusual_dev_list);
	if (result)
		return result;

	/* The actual transport will be determined later by the
	 * initialization routine; this is just a placeholder.
	 */
	us->transport_name = "Shuttle USBAT";
	us->transport = usbat_flash_transport;
	us->transport_reset = usb_stor_CB_reset;
	us->max_lun = 1;

	result = usb_stor_probe2(us);
	return result;
}

static struct usb_driver usbat_driver = {
	.name =		"ums-usbat",
	.probe =	usbat_probe,
	.disconnect =	usb_stor_disconnect,
	.suspend =	usb_stor_suspend,
	.resume =	usb_stor_resume,
	.reset_resume =	usb_stor_reset_resume,
	.pre_reset =	usb_stor_pre_reset,
	.post_reset =	usb_stor_post_reset,
	.id_table =	usbat_usb_ids,
	.soft_unbind =	1,
};

static int __init usbat_init(void)
{
	return usb_register(&usbat_driver);
}

static void __exit usbat_exit(void)
{
	usb_deregister(&usbat_driver);
}

module_init(usbat_init);
module_exit(usbat_exit);
