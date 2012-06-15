/*
 * Edgeport USB Serial Converter driver
 *
 * Copyright (C) 2000 Inside Out Networks, All rights reserved.
 * Copyright (C) 2001-2002 Greg Kroah-Hartman <greg@kroah.com>
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 * Supports the following devices:
 *	Edgeport/4
 *	Edgeport/4t
 *	Edgeport/2
 *	Edgeport/4i
 *	Edgeport/2i
 *	Edgeport/421
 *	Edgeport/21
 *	Rapidport/4
 *	Edgeport/8
 *	Edgeport/2D8
 *	Edgeport/4D8
 *	Edgeport/8i
 *
 * For questions or problems with this driver, contact Inside Out
 * Networks technical support, or Peter Berger <pberger@brimson.com>,
 * or Al Borchers <alborchers@steinerpoint.com>.
 *
 */

#include <linux/kernel.h>
#include <linux/jiffies.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/serial.h>
#include <linux/ioctl.h>
#include <linux/wait.h>
#include <linux/firmware.h>
#include <linux/ihex.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>
#include "io_edgeport.h"
#include "io_ionsp.h"		/* info for the iosp messages */
#include "io_16654.h"		/* 16654 UART defines */

/*
 * Version Information
 */
#define DRIVER_VERSION "v2.7"
#define DRIVER_AUTHOR "Greg Kroah-Hartman <greg@kroah.com> and David Iacovelli"
#define DRIVER_DESC "Edgeport USB Serial Driver"

#define MAX_NAME_LEN		64

#define CHASE_TIMEOUT		(5*HZ)		/* 5 seconds */
#define OPEN_TIMEOUT		(5*HZ)		/* 5 seconds */
#define COMMAND_TIMEOUT		(5*HZ)		/* 5 seconds */

/* receive port state */
enum RXSTATE {
	EXPECT_HDR1 = 0,    /* Expect header byte 1 */
	EXPECT_HDR2 = 1,    /* Expect header byte 2 */
	EXPECT_DATA = 2,    /* Expect 'RxBytesRemaining' data */
	EXPECT_HDR3 = 3,    /* Expect header byte 3 (for status hdrs only) */
};


/* Transmit Fifo
 * This Transmit queue is an extension of the edgeport Rx buffer.
 * The maximum amount of data buffered in both the edgeport
 * Rx buffer (maxTxCredits) and this buffer will never exceed maxTxCredits.
 */
struct TxFifo {
	unsigned int	head;	/* index to head pointer (write) */
	unsigned int	tail;	/* index to tail pointer (read)  */
	unsigned int	count;	/* Bytes in queue */
	unsigned int	size;	/* Max size of queue (equal to Max number of TxCredits) */
	unsigned char	*fifo;	/* allocated Buffer */
};

/* This structure holds all of the local port information */
struct edgeport_port {
	__u16			txCredits;		/* our current credits for this port */
	__u16			maxTxCredits;		/* the max size of the port */

	struct TxFifo		txfifo;			/* transmit fifo -- size will be maxTxCredits */
	struct urb		*write_urb;		/* write URB for this port */
	bool			write_in_progress;	/* 'true' while a write URB is outstanding */
	spinlock_t		ep_lock;

	__u8			shadowLCR;		/* last LCR value received */
	__u8			shadowMCR;		/* last MCR value received */
	__u8			shadowMSR;		/* last MSR value received */
	__u8			shadowLSR;		/* last LSR value received */
	__u8			shadowXonChar;		/* last value set as XON char in Edgeport */
	__u8			shadowXoffChar;		/* last value set as XOFF char in Edgeport */
	__u8			validDataMask;
	__u32			baudRate;

	bool			open;
	bool			openPending;
	bool			commandPending;
	bool			closePending;
	bool			chaseResponsePending;

	wait_queue_head_t	wait_chase;		/* for handling sleeping while waiting for chase to finish */
	wait_queue_head_t	wait_open;		/* for handling sleeping while waiting for open to finish */
	wait_queue_head_t	wait_command;		/* for handling sleeping while waiting for command to finish */
	wait_queue_head_t	delta_msr_wait;		/* for handling sleeping while waiting for msr change to happen */

	struct async_icount	icount;
	struct usb_serial_port	*port;			/* loop back to the owner of this object */
};


/* This structure holds all of the individual device information */
struct edgeport_serial {
	char			name[MAX_NAME_LEN+2];		/* string name of this device */

	struct edge_manuf_descriptor	manuf_descriptor;	/* the manufacturer descriptor */
	struct edge_boot_descriptor	boot_descriptor;	/* the boot firmware descriptor */
	struct edgeport_product_info	product_info;		/* Product Info */
	struct edge_compatibility_descriptor epic_descriptor;	/* Edgeport compatible descriptor */
	int			is_epic;			/* flag if EPiC device or not */

	__u8			interrupt_in_endpoint;		/* the interrupt endpoint handle */
	unsigned char		*interrupt_in_buffer;		/* the buffer we use for the interrupt endpoint */
	struct urb		*interrupt_read_urb;		/* our interrupt urb */

	__u8			bulk_in_endpoint;		/* the bulk in endpoint handle */
	unsigned char		*bulk_in_buffer;		/* the buffer we use for the bulk in endpoint */
	struct urb		*read_urb;			/* our bulk read urb */
	bool			read_in_progress;
	spinlock_t		es_lock;

	__u8			bulk_out_endpoint;		/* the bulk out endpoint handle */

	__s16			rxBytesAvail;			/* the number of bytes that we need to read from this device */

	enum RXSTATE		rxState;			/* the current state of the bulk receive processor */
	__u8			rxHeader1;			/* receive header byte 1 */
	__u8			rxHeader2;			/* receive header byte 2 */
	__u8			rxHeader3;			/* receive header byte 3 */
	__u8			rxPort;				/* the port that we are currently receiving data for */
	__u8			rxStatusCode;			/* the receive status code */
	__u8			rxStatusParam;			/* the receive status paramater */
	__s16			rxBytesRemaining;		/* the number of port bytes left to read */
	struct usb_serial	*serial;			/* loop back to the owner of this object */
};

/* baud rate information */
struct divisor_table_entry {
	__u32   BaudRate;
	__u16  Divisor;
};

/*
 * Define table of divisors for Rev A EdgePort/4 hardware
 * These assume a 3.6864MHz crystal, the standard /16, and
 * MCR.7 = 0.
 */

static const struct divisor_table_entry divisor_table[] = {
	{   50,		4608},
	{   75,		3072},
	{   110,	2095},	/* 2094.545455 => 230450   => .0217 % over */
	{   134,	1713},	/* 1713.011152 => 230398.5 => .00065% under */
	{   150,	1536},
	{   300,	768},
	{   600,	384},
	{   1200,	192},
	{   1800,	128},
	{   2400,	96},
	{   4800,	48},
	{   7200,	32},
	{   9600,	24},
	{   14400,	16},
	{   19200,	12},
	{   38400,	6},
	{   57600,	4},
	{   115200,	2},
	{   230400,	1},
};

/* local variables */
static bool debug;

/* Number of outstanding Command Write Urbs */
static atomic_t CmdUrbs = ATOMIC_INIT(0);


/* local function prototypes */

/* function prototypes for all URB callbacks */
static void edge_interrupt_callback(struct urb *urb);
static void edge_bulk_in_callback(struct urb *urb);
static void edge_bulk_out_data_callback(struct urb *urb);
static void edge_bulk_out_cmd_callback(struct urb *urb);

/* function prototypes for the usbserial callbacks */
static int edge_open(struct tty_struct *tty, struct usb_serial_port *port);
static void edge_close(struct usb_serial_port *port);
static int edge_write(struct tty_struct *tty, struct usb_serial_port *port,
					const unsigned char *buf, int count);
static int edge_write_room(struct tty_struct *tty);
static int edge_chars_in_buffer(struct tty_struct *tty);
static void edge_throttle(struct tty_struct *tty);
static void edge_unthrottle(struct tty_struct *tty);
static void edge_set_termios(struct tty_struct *tty,
					struct usb_serial_port *port,
					struct ktermios *old_termios);
static int  edge_ioctl(struct tty_struct *tty,
					unsigned int cmd, unsigned long arg);
static void edge_break(struct tty_struct *tty, int break_state);
static int  edge_tiocmget(struct tty_struct *tty);
static int  edge_tiocmset(struct tty_struct *tty,
					unsigned int set, unsigned int clear);
static int  edge_get_icount(struct tty_struct *tty,
				struct serial_icounter_struct *icount);
static int  edge_startup(struct usb_serial *serial);
static void edge_disconnect(struct usb_serial *serial);
static void edge_release(struct usb_serial *serial);

#include "io_tables.h"	/* all of the devices that this driver supports */

/* function prototypes for all of our local functions */

static void  process_rcvd_data(struct edgeport_serial *edge_serial,
				unsigned char *buffer, __u16 bufferLength);
static void process_rcvd_status(struct edgeport_serial *edge_serial,
				__u8 byte2, __u8 byte3);
static void edge_tty_recv(struct device *dev, struct tty_struct *tty,
				unsigned char *data, int length);
static void handle_new_msr(struct edgeport_port *edge_port, __u8 newMsr);
static void handle_new_lsr(struct edgeport_port *edge_port, __u8 lsrData,
				__u8 lsr, __u8 data);
static int  send_iosp_ext_cmd(struct edgeport_port *edge_port, __u8 command,
				__u8 param);
static int  calc_baud_rate_divisor(int baud_rate, int *divisor);
static int  send_cmd_write_baud_rate(struct edgeport_port *edge_port,
				int baudRate);
static void change_port_settings(struct tty_struct *tty,
				struct edgeport_port *edge_port,
				struct ktermios *old_termios);
static int  send_cmd_write_uart_register(struct edgeport_port *edge_port,
				__u8 regNum, __u8 regValue);
static int  write_cmd_usb(struct edgeport_port *edge_port,
				unsigned char *buffer, int writeLength);
static void send_more_port_data(struct edgeport_serial *edge_serial,
				struct edgeport_port *edge_port);

static int sram_write(struct usb_serial *serial, __u16 extAddr, __u16 addr,
					__u16 length, const __u8 *data);
static int rom_read(struct usb_serial *serial, __u16 extAddr, __u16 addr,
						__u16 length, __u8 *data);
static int rom_write(struct usb_serial *serial, __u16 extAddr, __u16 addr,
					__u16 length, const __u8 *data);
static void get_manufacturing_desc(struct edgeport_serial *edge_serial);
static void get_boot_desc(struct edgeport_serial *edge_serial);
static void load_application_firmware(struct edgeport_serial *edge_serial);

static void unicode_to_ascii(char *string, int buflen,
				__le16 *unicode, int unicode_size);


/* ************************************************************************ */
/* ************************************************************************ */
/* ************************************************************************ */
/* ************************************************************************ */

/************************************************************************
 *									*
 * update_edgeport_E2PROM()	Compare current versions of		*
 *				Boot ROM and Manufacture 		*
 *				Descriptors with versions		*
 *				embedded in this driver			*
 *									*
 ************************************************************************/
static void update_edgeport_E2PROM(struct edgeport_serial *edge_serial)
{
	__u32 BootCurVer;
	__u32 BootNewVer;
	__u8 BootMajorVersion;
	__u8 BootMinorVersion;
	__u16 BootBuildNumber;
	__u32 Bootaddr;
	const struct ihex_binrec *rec;
	const struct firmware *fw;
	const char *fw_name;
	int response;

	switch (edge_serial->product_info.iDownloadFile) {
	case EDGE_DOWNLOAD_FILE_I930:
		fw_name	= "edgeport/boot.fw";
		break;
	case EDGE_DOWNLOAD_FILE_80251:
		fw_name	= "edgeport/boot2.fw";
		break;
	default:
		return;
	}

	response = request_ihex_firmware(&fw, fw_name,
					 &edge_serial->serial->dev->dev);
	if (response) {
		printk(KERN_ERR "Failed to load image \"%s\" err %d\n",
		       fw_name, response);
		return;
	}

	rec = (const struct ihex_binrec *)fw->data;
	BootMajorVersion = rec->data[0];
	BootMinorVersion = rec->data[1];
	BootBuildNumber = (rec->data[2] << 8) | rec->data[3];

	/* Check Boot Image Version */
	BootCurVer = (edge_serial->boot_descriptor.MajorVersion << 24) +
		     (edge_serial->boot_descriptor.MinorVersion << 16) +
		      le16_to_cpu(edge_serial->boot_descriptor.BuildNumber);

	BootNewVer = (BootMajorVersion << 24) +
		     (BootMinorVersion << 16) +
		      BootBuildNumber;

	dbg("Current Boot Image version %d.%d.%d",
	    edge_serial->boot_descriptor.MajorVersion,
	    edge_serial->boot_descriptor.MinorVersion,
	    le16_to_cpu(edge_serial->boot_descriptor.BuildNumber));


	if (BootNewVer > BootCurVer) {
		dbg("**Update Boot Image from %d.%d.%d to %d.%d.%d",
		    edge_serial->boot_descriptor.MajorVersion,
		    edge_serial->boot_descriptor.MinorVersion,
		    le16_to_cpu(edge_serial->boot_descriptor.BuildNumber),
		    BootMajorVersion, BootMinorVersion, BootBuildNumber);

		dbg("Downloading new Boot Image");

		for (rec = ihex_next_binrec(rec); rec;
		     rec = ihex_next_binrec(rec)) {
			Bootaddr = be32_to_cpu(rec->addr);
			response = rom_write(edge_serial->serial,
					     Bootaddr >> 16,
					     Bootaddr & 0xFFFF,
					     be16_to_cpu(rec->len),
					     &rec->data[0]);
			if (response < 0) {
				dev_err(&edge_serial->serial->dev->dev,
					"rom_write failed (%x, %x, %d)\n",
					Bootaddr >> 16, Bootaddr & 0xFFFF,
					be16_to_cpu(rec->len));
				break;
			}
		}
	} else {
		dbg("Boot Image -- already up to date");
	}
	release_firmware(fw);
}

#if 0
/************************************************************************
 *
 *  Get string descriptor from device
 *
 ************************************************************************/
static int get_string_desc(struct usb_device *dev, int Id,
				struct usb_string_descriptor **pRetDesc)
{
	struct usb_string_descriptor StringDesc;
	struct usb_string_descriptor *pStringDesc;

	dbg("%s - USB String ID = %d", __func__, Id);

	if (!usb_get_descriptor(dev, USB_DT_STRING, Id, &StringDesc,
						sizeof(StringDesc)))
		return 0;

	pStringDesc = kmalloc(StringDesc.bLength, GFP_KERNEL);
	if (!pStringDesc)
		return -1;

	if (!usb_get_descriptor(dev, USB_DT_STRING, Id, pStringDesc,
							StringDesc.bLength)) {
		kfree(pStringDesc);
		return -1;
	}

	*pRetDesc = pStringDesc;
	return 0;
}
#endif

static void dump_product_info(struct edgeport_product_info *product_info)
{
	/* Dump Product Info structure */
	dbg("**Product Information:");
	dbg("  ProductId             %x", product_info->ProductId);
	dbg("  NumPorts              %d", product_info->NumPorts);
	dbg("  ProdInfoVer           %d", product_info->ProdInfoVer);
	dbg("  IsServer              %d", product_info->IsServer);
	dbg("  IsRS232               %d", product_info->IsRS232);
	dbg("  IsRS422               %d", product_info->IsRS422);
	dbg("  IsRS485               %d", product_info->IsRS485);
	dbg("  RomSize               %d", product_info->RomSize);
	dbg("  RamSize               %d", product_info->RamSize);
	dbg("  CpuRev                %x", product_info->CpuRev);
	dbg("  BoardRev              %x", product_info->BoardRev);
	dbg("  BootMajorVersion      %d.%d.%d", product_info->BootMajorVersion,
	    product_info->BootMinorVersion,
	    le16_to_cpu(product_info->BootBuildNumber));
	dbg("  FirmwareMajorVersion  %d.%d.%d",
			product_info->FirmwareMajorVersion,
			product_info->FirmwareMinorVersion,
			le16_to_cpu(product_info->FirmwareBuildNumber));
	dbg("  ManufactureDescDate   %d/%d/%d",
			product_info->ManufactureDescDate[0],
			product_info->ManufactureDescDate[1],
			product_info->ManufactureDescDate[2]+1900);
	dbg("  iDownloadFile         0x%x", product_info->iDownloadFile);
	dbg("  EpicVer               %d", product_info->EpicVer);
}

static void get_product_info(struct edgeport_serial *edge_serial)
{
	struct edgeport_product_info *product_info = &edge_serial->product_info;

	memset(product_info, 0, sizeof(struct edgeport_product_info));

	product_info->ProductId = (__u16)(le16_to_cpu(edge_serial->serial->dev->descriptor.idProduct) & ~ION_DEVICE_ID_80251_NETCHIP);
	product_info->NumPorts = edge_serial->manuf_descriptor.NumPorts;
	product_info->ProdInfoVer = 0;

	product_info->RomSize = edge_serial->manuf_descriptor.RomSize;
	product_info->RamSize = edge_serial->manuf_descriptor.RamSize;
	product_info->CpuRev = edge_serial->manuf_descriptor.CpuRev;
	product_info->BoardRev = edge_serial->manuf_descriptor.BoardRev;

	product_info->BootMajorVersion =
				edge_serial->boot_descriptor.MajorVersion;
	product_info->BootMinorVersion =
				edge_serial->boot_descriptor.MinorVersion;
	product_info->BootBuildNumber =
				edge_serial->boot_descriptor.BuildNumber;

	memcpy(product_info->ManufactureDescDate,
			edge_serial->manuf_descriptor.DescDate,
			sizeof(edge_serial->manuf_descriptor.DescDate));

	/* check if this is 2nd generation hardware */
	if (le16_to_cpu(edge_serial->serial->dev->descriptor.idProduct)
					    & ION_DEVICE_ID_80251_NETCHIP)
		product_info->iDownloadFile = EDGE_DOWNLOAD_FILE_80251;
	else
		product_info->iDownloadFile = EDGE_DOWNLOAD_FILE_I930;
 
	/* Determine Product type and set appropriate flags */
	switch (DEVICE_ID_FROM_USB_PRODUCT_ID(product_info->ProductId)) {
	case ION_DEVICE_ID_EDGEPORT_COMPATIBLE:
	case ION_DEVICE_ID_EDGEPORT_4T:
	case ION_DEVICE_ID_EDGEPORT_4:
	case ION_DEVICE_ID_EDGEPORT_2:
	case ION_DEVICE_ID_EDGEPORT_8_DUAL_CPU:
	case ION_DEVICE_ID_EDGEPORT_8:
	case ION_DEVICE_ID_EDGEPORT_421:
	case ION_DEVICE_ID_EDGEPORT_21:
	case ION_DEVICE_ID_EDGEPORT_2_DIN:
	case ION_DEVICE_ID_EDGEPORT_4_DIN:
	case ION_DEVICE_ID_EDGEPORT_16_DUAL_CPU:
		product_info->IsRS232 = 1;
		break;

	case ION_DEVICE_ID_EDGEPORT_2I:	/* Edgeport/2 RS422/RS485 */
		product_info->IsRS422 = 1;
		product_info->IsRS485 = 1;
		break;

	case ION_DEVICE_ID_EDGEPORT_8I:	/* Edgeport/4 RS422 */
	case ION_DEVICE_ID_EDGEPORT_4I:	/* Edgeport/4 RS422 */
		product_info->IsRS422 = 1;
		break;
	}

	dump_product_info(product_info);
}

static int get_epic_descriptor(struct edgeport_serial *ep)
{
	int result;
	struct usb_serial *serial = ep->serial;
	struct edgeport_product_info *product_info = &ep->product_info;
	struct edge_compatibility_descriptor *epic = &ep->epic_descriptor;
	struct edge_compatibility_bits *bits;

	ep->is_epic = 0;
	result = usb_control_msg(serial->dev, usb_rcvctrlpipe(serial->dev, 0),
				 USB_REQUEST_ION_GET_EPIC_DESC,
				 0xC0, 0x00, 0x00,
				 &ep->epic_descriptor,
				 sizeof(struct edge_compatibility_descriptor),
				 300);

	dbg("%s result = %d", __func__, result);

	if (result > 0) {
		ep->is_epic = 1;
		memset(product_info, 0, sizeof(struct edgeport_product_info));

		product_info->NumPorts = epic->NumPorts;
		product_info->ProdInfoVer = 0;
		product_info->FirmwareMajorVersion = epic->MajorVersion;
		product_info->FirmwareMinorVersion = epic->MinorVersion;
		product_info->FirmwareBuildNumber = epic->BuildNumber;
		product_info->iDownloadFile = epic->iDownloadFile;
		product_info->EpicVer = epic->EpicVer;
		product_info->Epic = epic->Supports;
		product_info->ProductId = ION_DEVICE_ID_EDGEPORT_COMPATIBLE;
		dump_product_info(product_info);

		bits = &ep->epic_descriptor.Supports;
		dbg("**EPIC descriptor:");
		dbg("  VendEnableSuspend: %s", bits->VendEnableSuspend	? "TRUE": "FALSE");
		dbg("  IOSPOpen         : %s", bits->IOSPOpen		? "TRUE": "FALSE");
		dbg("  IOSPClose        : %s", bits->IOSPClose		? "TRUE": "FALSE");
		dbg("  IOSPChase        : %s", bits->IOSPChase		? "TRUE": "FALSE");
		dbg("  IOSPSetRxFlow    : %s", bits->IOSPSetRxFlow	? "TRUE": "FALSE");
		dbg("  IOSPSetTxFlow    : %s", bits->IOSPSetTxFlow	? "TRUE": "FALSE");
		dbg("  IOSPSetXChar     : %s", bits->IOSPSetXChar	? "TRUE": "FALSE");
		dbg("  IOSPRxCheck      : %s", bits->IOSPRxCheck	? "TRUE": "FALSE");
		dbg("  IOSPSetClrBreak  : %s", bits->IOSPSetClrBreak	? "TRUE": "FALSE");
		dbg("  IOSPWriteMCR     : %s", bits->IOSPWriteMCR	? "TRUE": "FALSE");
		dbg("  IOSPWriteLCR     : %s", bits->IOSPWriteLCR	? "TRUE": "FALSE");
		dbg("  IOSPSetBaudRate  : %s", bits->IOSPSetBaudRate	? "TRUE": "FALSE");
		dbg("  TrueEdgeport     : %s", bits->TrueEdgeport	? "TRUE": "FALSE");
	}

	return result;
}


/************************************************************************/
/************************************************************************/
/*            U S B  C A L L B A C K   F U N C T I O N S                */
/*            U S B  C A L L B A C K   F U N C T I O N S                */
/************************************************************************/
/************************************************************************/

/*****************************************************************************
 * edge_interrupt_callback
 *	this is the callback function for when we have received data on the
 *	interrupt endpoint.
 *****************************************************************************/
static void edge_interrupt_callback(struct urb *urb)
{
	struct edgeport_serial	*edge_serial = urb->context;
	struct edgeport_port *edge_port;
	struct usb_serial_port *port;
	struct tty_struct *tty;
	unsigned char *data = urb->transfer_buffer;
	int length = urb->actual_length;
	int bytes_avail;
	int position;
	int txCredits;
	int portNumber;
	int result;
	int status = urb->status;

	dbg("%s", __func__);

	switch (status) {
	case 0:
		/* success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dbg("%s - urb shutting down with status: %d",
						__func__, status);
		return;
	default:
		dbg("%s - nonzero urb status received: %d", __func__, status);
		goto exit;
	}

	/* process this interrupt-read even if there are no ports open */
	if (length) {
		usb_serial_debug_data(debug, &edge_serial->serial->dev->dev,
						__func__, length, data);

		if (length > 1) {
			bytes_avail = data[0] | (data[1] << 8);
			if (bytes_avail) {
				spin_lock(&edge_serial->es_lock);
				edge_serial->rxBytesAvail += bytes_avail;
				dbg("%s - bytes_avail=%d, rxBytesAvail=%d, read_in_progress=%d", __func__, bytes_avail, edge_serial->rxBytesAvail, edge_serial->read_in_progress);

				if (edge_serial->rxBytesAvail > 0 &&
				    !edge_serial->read_in_progress) {
					dbg("%s - posting a read", __func__);
					edge_serial->read_in_progress = true;

					/* we have pending bytes on the
					   bulk in pipe, send a request */
					result = usb_submit_urb(edge_serial->read_urb, GFP_ATOMIC);
					if (result) {
						dev_err(&edge_serial->serial->dev->dev, "%s - usb_submit_urb(read bulk) failed with result = %d\n", __func__, result);
						edge_serial->read_in_progress = false;
					}
				}
				spin_unlock(&edge_serial->es_lock);
			}
		}
		/* grab the txcredits for the ports if available */
		position = 2;
		portNumber = 0;
		while ((position < length) &&
				(portNumber < edge_serial->serial->num_ports)) {
			txCredits = data[position] | (data[position+1] << 8);
			if (txCredits) {
				port = edge_serial->serial->port[portNumber];
				edge_port = usb_get_serial_port_data(port);
				if (edge_port->open) {
					spin_lock(&edge_port->ep_lock);
					edge_port->txCredits += txCredits;
					spin_unlock(&edge_port->ep_lock);
					dbg("%s - txcredits for port%d = %d",
							__func__, portNumber,
							edge_port->txCredits);

					/* tell the tty driver that something
					   has changed */
					tty = tty_port_tty_get(
						&edge_port->port->port);
					if (tty) {
						tty_wakeup(tty);
						tty_kref_put(tty);
					}
					/* Since we have more credit, check
					   if more data can be sent */
					send_more_port_data(edge_serial,
								edge_port);
				}
			}
			position += 2;
			++portNumber;
		}
	}

exit:
	result = usb_submit_urb(urb, GFP_ATOMIC);
	if (result)
		dev_err(&urb->dev->dev,
			"%s - Error %d submitting control urb\n",
						__func__, result);
}


/*****************************************************************************
 * edge_bulk_in_callback
 *	this is the callback function for when we have received data on the
 *	bulk in endpoint.
 *****************************************************************************/
static void edge_bulk_in_callback(struct urb *urb)
{
	struct edgeport_serial	*edge_serial = urb->context;
	unsigned char		*data = urb->transfer_buffer;
	int			retval;
	__u16			raw_data_length;
	int status = urb->status;

	dbg("%s", __func__);

	if (status) {
		dbg("%s - nonzero read bulk status received: %d",
		    __func__, status);
		edge_serial->read_in_progress = false;
		return;
	}

	if (urb->actual_length == 0) {
		dbg("%s - read bulk callback with no data", __func__);
		edge_serial->read_in_progress = false;
		return;
	}

	raw_data_length = urb->actual_length;

	usb_serial_debug_data(debug, &edge_serial->serial->dev->dev,
					__func__, raw_data_length, data);

	spin_lock(&edge_serial->es_lock);

	/* decrement our rxBytes available by the number that we just got */
	edge_serial->rxBytesAvail -= raw_data_length;

	dbg("%s - Received = %d, rxBytesAvail %d", __func__,
				raw_data_length, edge_serial->rxBytesAvail);

	process_rcvd_data(edge_serial, data, urb->actual_length);

	/* check to see if there's any more data for us to read */
	if (edge_serial->rxBytesAvail > 0) {
		dbg("%s - posting a read", __func__);
		retval = usb_submit_urb(edge_serial->read_urb, GFP_ATOMIC);
		if (retval) {
			dev_err(&urb->dev->dev,
				"%s - usb_submit_urb(read bulk) failed, "
				"retval = %d\n", __func__, retval);
			edge_serial->read_in_progress = false;
		}
	} else {
		edge_serial->read_in_progress = false;
	}

	spin_unlock(&edge_serial->es_lock);
}


/*****************************************************************************
 * edge_bulk_out_data_callback
 *	this is the callback function for when we have finished sending
 *	serial data on the bulk out endpoint.
 *****************************************************************************/
static void edge_bulk_out_data_callback(struct urb *urb)
{
	struct edgeport_port *edge_port = urb->context;
	struct tty_struct *tty;
	int status = urb->status;

	dbg("%s", __func__);

	if (status) {
		dbg("%s - nonzero write bulk status received: %d",
		    __func__, status);
	}

	tty = tty_port_tty_get(&edge_port->port->port);

	if (tty && edge_port->open) {
		/* let the tty driver wakeup if it has a special
		   write_wakeup function */
		tty_wakeup(tty);
	}
	tty_kref_put(tty);

	/* Release the Write URB */
	edge_port->write_in_progress = false;

	/* Check if more data needs to be sent */
	send_more_port_data((struct edgeport_serial *)
		(usb_get_serial_data(edge_port->port->serial)), edge_port);
}


/*****************************************************************************
 * BulkOutCmdCallback
 *	this is the callback function for when we have finished sending a
 *	command	on the bulk out endpoint.
 *****************************************************************************/
static void edge_bulk_out_cmd_callback(struct urb *urb)
{
	struct edgeport_port *edge_port = urb->context;
	struct tty_struct *tty;
	int status = urb->status;

	dbg("%s", __func__);

	atomic_dec(&CmdUrbs);
	dbg("%s - FREE URB %p (outstanding %d)", __func__,
					urb, atomic_read(&CmdUrbs));


	/* clean up the transfer buffer */
	kfree(urb->transfer_buffer);

	/* Free the command urb */
	usb_free_urb(urb);

	if (status) {
		dbg("%s - nonzero write bulk status received: %d",
							__func__, status);
		return;
	}

	/* Get pointer to tty */
	tty = tty_port_tty_get(&edge_port->port->port);

	/* tell the tty driver that something has changed */
	if (tty && edge_port->open)
		tty_wakeup(tty);
	tty_kref_put(tty);

	/* we have completed the command */
	edge_port->commandPending = false;
	wake_up(&edge_port->wait_command);
}


/*****************************************************************************
 * Driver tty interface functions
 *****************************************************************************/

/*****************************************************************************
 * SerialOpen
 *	this function is called by the tty driver when a port is opened
 *	If successful, we return 0
 *	Otherwise we return a negative error number.
 *****************************************************************************/
static int edge_open(struct tty_struct *tty, struct usb_serial_port *port)
{
	struct edgeport_port *edge_port = usb_get_serial_port_data(port);
	struct usb_serial *serial;
	struct edgeport_serial *edge_serial;
	int response;

	dbg("%s - port %d", __func__, port->number);

	if (edge_port == NULL)
		return -ENODEV;

	/* see if we've set up our endpoint info yet (can't set it up
	   in edge_startup as the structures were not set up at that time.) */
	serial = port->serial;
	edge_serial = usb_get_serial_data(serial);
	if (edge_serial == NULL)
		return -ENODEV;
	if (edge_serial->interrupt_in_buffer == NULL) {
		struct usb_serial_port *port0 = serial->port[0];

		/* not set up yet, so do it now */
		edge_serial->interrupt_in_buffer =
					port0->interrupt_in_buffer;
		edge_serial->interrupt_in_endpoint =
					port0->interrupt_in_endpointAddress;
		edge_serial->interrupt_read_urb = port0->interrupt_in_urb;
		edge_serial->bulk_in_buffer = port0->bulk_in_buffer;
		edge_serial->bulk_in_endpoint =
					port0->bulk_in_endpointAddress;
		edge_serial->read_urb = port0->read_urb;
		edge_serial->bulk_out_endpoint =
					port0->bulk_out_endpointAddress;

		/* set up our interrupt urb */
		usb_fill_int_urb(edge_serial->interrupt_read_urb,
		      serial->dev,
		      usb_rcvintpipe(serial->dev,
				port0->interrupt_in_endpointAddress),
		      port0->interrupt_in_buffer,
		      edge_serial->interrupt_read_urb->transfer_buffer_length,
		      edge_interrupt_callback, edge_serial,
		      edge_serial->interrupt_read_urb->interval);

		/* set up our bulk in urb */
		usb_fill_bulk_urb(edge_serial->read_urb, serial->dev,
			usb_rcvbulkpipe(serial->dev,
				port0->bulk_in_endpointAddress),
			port0->bulk_in_buffer,
			edge_serial->read_urb->transfer_buffer_length,
			edge_bulk_in_callback, edge_serial);
		edge_serial->read_in_progress = false;

		/* start interrupt read for this edgeport
		 * this interrupt will continue as long
		 * as the edgeport is connected */
		response = usb_submit_urb(edge_serial->interrupt_read_urb,
								GFP_KERNEL);
		if (response) {
			dev_err(&port->dev,
				"%s - Error %d submitting control urb\n",
							__func__, response);
		}
	}

	/* initialize our wait queues */
	init_waitqueue_head(&edge_port->wait_open);
	init_waitqueue_head(&edge_port->wait_chase);
	init_waitqueue_head(&edge_port->delta_msr_wait);
	init_waitqueue_head(&edge_port->wait_command);

	/* initialize our icount structure */
	memset(&(edge_port->icount), 0x00, sizeof(edge_port->icount));

	/* initialize our port settings */
	edge_port->txCredits = 0;	/* Can't send any data yet */
	/* Must always set this bit to enable ints! */
	edge_port->shadowMCR = MCR_MASTER_IE;
	edge_port->chaseResponsePending = false;

	/* send a open port command */
	edge_port->openPending = true;
	edge_port->open        = false;
	response = send_iosp_ext_cmd(edge_port, IOSP_CMD_OPEN_PORT, 0);

	if (response < 0) {
		dev_err(&port->dev, "%s - error sending open port command\n",
								__func__);
		edge_port->openPending = false;
		return -ENODEV;
	}

	/* now wait for the port to be completely opened */
	wait_event_timeout(edge_port->wait_open, !edge_port->openPending,
								OPEN_TIMEOUT);

	if (!edge_port->open) {
		/* open timed out */
		dbg("%s - open timedout", __func__);
		edge_port->openPending = false;
		return -ENODEV;
	}

	/* create the txfifo */
	edge_port->txfifo.head	= 0;
	edge_port->txfifo.tail	= 0;
	edge_port->txfifo.count	= 0;
	edge_port->txfifo.size	= edge_port->maxTxCredits;
	edge_port->txfifo.fifo	= kmalloc(edge_port->maxTxCredits, GFP_KERNEL);

	if (!edge_port->txfifo.fifo) {
		dbg("%s - no memory", __func__);
		edge_close(port);
		return -ENOMEM;
	}

	/* Allocate a URB for the write */
	edge_port->write_urb = usb_alloc_urb(0, GFP_KERNEL);
	edge_port->write_in_progress = false;

	if (!edge_port->write_urb) {
		dbg("%s - no memory", __func__);
		edge_close(port);
		return -ENOMEM;
	}

	dbg("%s(%d) - Initialize TX fifo to %d bytes",
			__func__, port->number, edge_port->maxTxCredits);

	dbg("%s exited", __func__);

	return 0;
}


/************************************************************************
 *
 * block_until_chase_response
 *
 *	This function will block the close until one of the following:
 *		1. Response to our Chase comes from Edgeport
 *		2. A timeout of 10 seconds without activity has expired
 *		   (1K of Edgeport data @ 2400 baud ==> 4 sec to empty)
 *
 ************************************************************************/
static void block_until_chase_response(struct edgeport_port *edge_port)
{
	DEFINE_WAIT(wait);
	__u16 lastCredits;
	int timeout = 1*HZ;
	int loop = 10;

	while (1) {
		/* Save Last credits */
		lastCredits = edge_port->txCredits;

		/* Did we get our Chase response */
		if (!edge_port->chaseResponsePending) {
			dbg("%s - Got Chase Response", __func__);

			/* did we get all of our credit back? */
			if (edge_port->txCredits == edge_port->maxTxCredits) {
				dbg("%s - Got all credits", __func__);
				return;
			}
		}

		/* Block the thread for a while */
		prepare_to_wait(&edge_port->wait_chase, &wait,
						TASK_UNINTERRUPTIBLE);
		schedule_timeout(timeout);
		finish_wait(&edge_port->wait_chase, &wait);

		if (lastCredits == edge_port->txCredits) {
			/* No activity.. count down. */
			loop--;
			if (loop == 0) {
				edge_port->chaseResponsePending = false;
				dbg("%s - Chase TIMEOUT", __func__);
				return;
			}
		} else {
			/* Reset timeout value back to 10 seconds */
			dbg("%s - Last %d, Current %d", __func__,
					lastCredits, edge_port->txCredits);
			loop = 10;
		}
	}
}


/************************************************************************
 *
 * block_until_tx_empty
 *
 *	This function will block the close until one of the following:
 *		1. TX count are 0
 *		2. The edgeport has stopped
 *		3. A timeout of 3 seconds without activity has expired
 *
 ************************************************************************/
static void block_until_tx_empty(struct edgeport_port *edge_port)
{
	DEFINE_WAIT(wait);
	struct TxFifo *fifo = &edge_port->txfifo;
	__u32 lastCount;
	int timeout = HZ/10;
	int loop = 30;

	while (1) {
		/* Save Last count */
		lastCount = fifo->count;

		/* Is the Edgeport Buffer empty? */
		if (lastCount == 0) {
			dbg("%s - TX Buffer Empty", __func__);
			return;
		}

		/* Block the thread for a while */
		prepare_to_wait(&edge_port->wait_chase, &wait,
						TASK_UNINTERRUPTIBLE);
		schedule_timeout(timeout);
		finish_wait(&edge_port->wait_chase, &wait);

		dbg("%s wait", __func__);

		if (lastCount == fifo->count) {
			/* No activity.. count down. */
			loop--;
			if (loop == 0) {
				dbg("%s - TIMEOUT", __func__);
				return;
			}
		} else {
			/* Reset timeout value back to seconds */
			loop = 30;
		}
	}
}


/*****************************************************************************
 * edge_close
 *	this function is called by the tty driver when a port is closed
 *****************************************************************************/
static void edge_close(struct usb_serial_port *port)
{
	struct edgeport_serial *edge_serial;
	struct edgeport_port *edge_port;
	int status;

	dbg("%s - port %d", __func__, port->number);

	edge_serial = usb_get_serial_data(port->serial);
	edge_port = usb_get_serial_port_data(port);
	if (edge_serial == NULL || edge_port == NULL)
		return;

	/* block until tx is empty */
	block_until_tx_empty(edge_port);

	edge_port->closePending = true;

	if ((!edge_serial->is_epic) ||
	    ((edge_serial->is_epic) &&
	     (edge_serial->epic_descriptor.Supports.IOSPChase))) {
		/* flush and chase */
		edge_port->chaseResponsePending = true;

		dbg("%s - Sending IOSP_CMD_CHASE_PORT", __func__);
		status = send_iosp_ext_cmd(edge_port, IOSP_CMD_CHASE_PORT, 0);
		if (status == 0)
			/* block until chase finished */
			block_until_chase_response(edge_port);
		else
			edge_port->chaseResponsePending = false;
	}

	if ((!edge_serial->is_epic) ||
	    ((edge_serial->is_epic) &&
	     (edge_serial->epic_descriptor.Supports.IOSPClose))) {
	       /* close the port */
		dbg("%s - Sending IOSP_CMD_CLOSE_PORT", __func__);
		send_iosp_ext_cmd(edge_port, IOSP_CMD_CLOSE_PORT, 0);
	}

	/* port->close = true; */
	edge_port->closePending = false;
	edge_port->open = false;
	edge_port->openPending = false;

	usb_kill_urb(edge_port->write_urb);

	if (edge_port->write_urb) {
		/* if this urb had a transfer buffer already
				(old transfer) free it */
		kfree(edge_port->write_urb->transfer_buffer);
		usb_free_urb(edge_port->write_urb);
		edge_port->write_urb = NULL;
	}
	kfree(edge_port->txfifo.fifo);
	edge_port->txfifo.fifo = NULL;

	dbg("%s exited", __func__);
}

/*****************************************************************************
 * SerialWrite
 *	this function is called by the tty driver when data should be written
 *	to the port.
 *	If successful, we return the number of bytes written, otherwise we
 *	return a negative error number.
 *****************************************************************************/
static int edge_write(struct tty_struct *tty, struct usb_serial_port *port,
					const unsigned char *data, int count)
{
	struct edgeport_port *edge_port = usb_get_serial_port_data(port);
	struct TxFifo *fifo;
	int copySize;
	int bytesleft;
	int firsthalf;
	int secondhalf;
	unsigned long flags;

	dbg("%s - port %d", __func__, port->number);

	if (edge_port == NULL)
		return -ENODEV;

	/* get a pointer to the Tx fifo */
	fifo = &edge_port->txfifo;

	spin_lock_irqsave(&edge_port->ep_lock, flags);

	/* calculate number of bytes to put in fifo */
	copySize = min((unsigned int)count,
				(edge_port->txCredits - fifo->count));

	dbg("%s(%d) of %d byte(s) Fifo room  %d -- will copy %d bytes",
			__func__, port->number, count,
			edge_port->txCredits - fifo->count, copySize);

	/* catch writes of 0 bytes which the tty driver likes to give us,
	   and when txCredits is empty */
	if (copySize == 0) {
		dbg("%s - copySize = Zero", __func__);
		goto finish_write;
	}

	/* queue the data
	 * since we can never overflow the buffer we do not have to check for a
	 * full condition
	 *
	 * the copy is done is two parts -- first fill to the end of the buffer
	 * then copy the reset from the start of the buffer
	 */
	bytesleft = fifo->size - fifo->head;
	firsthalf = min(bytesleft, copySize);
	dbg("%s - copy %d bytes of %d into fifo ", __func__,
					firsthalf, bytesleft);

	/* now copy our data */
	memcpy(&fifo->fifo[fifo->head], data, firsthalf);
	usb_serial_debug_data(debug, &port->dev, __func__,
					firsthalf, &fifo->fifo[fifo->head]);

	/* update the index and size */
	fifo->head  += firsthalf;
	fifo->count += firsthalf;

	/* wrap the index */
	if (fifo->head == fifo->size)
		fifo->head = 0;

	secondhalf = copySize-firsthalf;

	if (secondhalf) {
		dbg("%s - copy rest of data %d", __func__, secondhalf);
		memcpy(&fifo->fifo[fifo->head], &data[firsthalf], secondhalf);
		usb_serial_debug_data(debug, &port->dev, __func__,
					secondhalf, &fifo->fifo[fifo->head]);
		/* update the index and size */
		fifo->count += secondhalf;
		fifo->head  += secondhalf;
		/* No need to check for wrap since we can not get to end of
		 * the fifo in this part
		 */
	}

finish_write:
	spin_unlock_irqrestore(&edge_port->ep_lock, flags);

	send_more_port_data((struct edgeport_serial *)
			usb_get_serial_data(port->serial), edge_port);

	dbg("%s wrote %d byte(s) TxCredits %d, Fifo %d", __func__,
				copySize, edge_port->txCredits, fifo->count);

	return copySize;
}


/************************************************************************
 *
 * send_more_port_data()
 *
 *	This routine attempts to write additional UART transmit data
 *	to a port over the USB bulk pipe. It is called (1) when new
 *	data has been written to a port's TxBuffer from higher layers
 *	(2) when the peripheral sends us additional TxCredits indicating
 *	that it can accept more	Tx data for a given port; and (3) when
 *	a bulk write completes successfully and we want to see if we
 *	can transmit more.
 *
 ************************************************************************/
static void send_more_port_data(struct edgeport_serial *edge_serial,
					struct edgeport_port *edge_port)
{
	struct TxFifo	*fifo = &edge_port->txfifo;
	struct urb	*urb;
	unsigned char	*buffer;
	int		status;
	int		count;
	int		bytesleft;
	int		firsthalf;
	int		secondhalf;
	unsigned long	flags;

	dbg("%s(%d)", __func__, edge_port->port->number);

	spin_lock_irqsave(&edge_port->ep_lock, flags);

	if (edge_port->write_in_progress ||
	    !edge_port->open             ||
	    (fifo->count == 0)) {
		dbg("%s(%d) EXIT - fifo %d, PendingWrite = %d",
				__func__, edge_port->port->number,
				fifo->count, edge_port->write_in_progress);
		goto exit_send;
	}

	/* since the amount of data in the fifo will always fit into the
	 * edgeport buffer we do not need to check the write length
	 *
	 * Do we have enough credits for this port to make it worthwhile
	 * to bother queueing a write. If it's too small, say a few bytes,
	 * it's better to wait for more credits so we can do a larger write.
	 */
	if (edge_port->txCredits < EDGE_FW_GET_TX_CREDITS_SEND_THRESHOLD(edge_port->maxTxCredits, EDGE_FW_BULK_MAX_PACKET_SIZE)) {
		dbg("%s(%d) Not enough credit - fifo %d TxCredit %d",
			__func__, edge_port->port->number, fifo->count,
			edge_port->txCredits);
		goto exit_send;
	}

	/* lock this write */
	edge_port->write_in_progress = true;

	/* get a pointer to the write_urb */
	urb = edge_port->write_urb;

	/* make sure transfer buffer is freed */
	kfree(urb->transfer_buffer);
	urb->transfer_buffer = NULL;

	/* build the data header for the buffer and port that we are about
	   to send out */
	count = fifo->count;
	buffer = kmalloc(count+2, GFP_ATOMIC);
	if (buffer == NULL) {
		dev_err_console(edge_port->port,
				"%s - no more kernel memory...\n", __func__);
		edge_port->write_in_progress = false;
		goto exit_send;
	}
	buffer[0] = IOSP_BUILD_DATA_HDR1(edge_port->port->number
				- edge_port->port->serial->minor, count);
	buffer[1] = IOSP_BUILD_DATA_HDR2(edge_port->port->number
				- edge_port->port->serial->minor, count);

	/* now copy our data */
	bytesleft =  fifo->size - fifo->tail;
	firsthalf = min(bytesleft, count);
	memcpy(&buffer[2], &fifo->fifo[fifo->tail], firsthalf);
	fifo->tail  += firsthalf;
	fifo->count -= firsthalf;
	if (fifo->tail == fifo->size)
		fifo->tail = 0;

	secondhalf = count-firsthalf;
	if (secondhalf) {
		memcpy(&buffer[2+firsthalf], &fifo->fifo[fifo->tail],
								secondhalf);
		fifo->tail  += secondhalf;
		fifo->count -= secondhalf;
	}

	if (count)
		usb_serial_debug_data(debug, &edge_port->port->dev,
						__func__, count, &buffer[2]);

	/* fill up the urb with all of our data and submit it */
	usb_fill_bulk_urb(urb, edge_serial->serial->dev,
			usb_sndbulkpipe(edge_serial->serial->dev,
					edge_serial->bulk_out_endpoint),
			buffer, count+2,
			edge_bulk_out_data_callback, edge_port);

	/* decrement the number of credits we have by the number we just sent */
	edge_port->txCredits -= count;
	edge_port->icount.tx += count;

	status = usb_submit_urb(urb, GFP_ATOMIC);
	if (status) {
		/* something went wrong */
		dev_err_console(edge_port->port,
			"%s - usb_submit_urb(write bulk) failed, status = %d, data lost\n",
				__func__, status);
		edge_port->write_in_progress = false;

		/* revert the credits as something bad happened. */
		edge_port->txCredits += count;
		edge_port->icount.tx -= count;
	}
	dbg("%s wrote %d byte(s) TxCredit %d, Fifo %d",
			__func__, count, edge_port->txCredits, fifo->count);

exit_send:
	spin_unlock_irqrestore(&edge_port->ep_lock, flags);
}


/*****************************************************************************
 * edge_write_room
 *	this function is called by the tty driver when it wants to know how
 *	many bytes of data we can accept for a specific port. If successful,
 *	we return the amount of room that we have for this port	(the txCredits)
 *	otherwise we return a negative error number.
 *****************************************************************************/
static int edge_write_room(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct edgeport_port *edge_port = usb_get_serial_port_data(port);
	int room;
	unsigned long flags;

	dbg("%s", __func__);

	if (edge_port == NULL)
		return 0;
	if (edge_port->closePending)
		return 0;

	dbg("%s - port %d", __func__, port->number);

	if (!edge_port->open) {
		dbg("%s - port not opened", __func__);
		return 0;
	}

	/* total of both buffers is still txCredit */
	spin_lock_irqsave(&edge_port->ep_lock, flags);
	room = edge_port->txCredits - edge_port->txfifo.count;
	spin_unlock_irqrestore(&edge_port->ep_lock, flags);

	dbg("%s - returns %d", __func__, room);
	return room;
}


/*****************************************************************************
 * edge_chars_in_buffer
 *	this function is called by the tty driver when it wants to know how
 *	many bytes of data we currently have outstanding in the port (data that
 *	has been written, but hasn't made it out the port yet)
 *	If successful, we return the number of bytes left to be written in the
 *	system,
 *	Otherwise we return a negative error number.
 *****************************************************************************/
static int edge_chars_in_buffer(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct edgeport_port *edge_port = usb_get_serial_port_data(port);
	int num_chars;
	unsigned long flags;

	dbg("%s", __func__);

	if (edge_port == NULL)
		return 0;
	if (edge_port->closePending)
		return 0;

	if (!edge_port->open) {
		dbg("%s - port not opened", __func__);
		return 0;
	}

	spin_lock_irqsave(&edge_port->ep_lock, flags);
	num_chars = edge_port->maxTxCredits - edge_port->txCredits +
						edge_port->txfifo.count;
	spin_unlock_irqrestore(&edge_port->ep_lock, flags);
	if (num_chars) {
		dbg("%s(port %d) - returns %d", __func__,
						port->number, num_chars);
	}

	return num_chars;
}


/*****************************************************************************
 * SerialThrottle
 *	this function is called by the tty driver when it wants to stop the data
 *	being read from the port.
 *****************************************************************************/
static void edge_throttle(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct edgeport_port *edge_port = usb_get_serial_port_data(port);
	int status;

	dbg("%s - port %d", __func__, port->number);

	if (edge_port == NULL)
		return;

	if (!edge_port->open) {
		dbg("%s - port not opened", __func__);
		return;
	}

	/* if we are implementing XON/XOFF, send the stop character */
	if (I_IXOFF(tty)) {
		unsigned char stop_char = STOP_CHAR(tty);
		status = edge_write(tty, port, &stop_char, 1);
		if (status <= 0)
			return;
	}

	/* if we are implementing RTS/CTS, toggle that line */
	if (tty->termios->c_cflag & CRTSCTS) {
		edge_port->shadowMCR &= ~MCR_RTS;
		status = send_cmd_write_uart_register(edge_port, MCR,
							edge_port->shadowMCR);
		if (status != 0)
			return;
	}
}


/*****************************************************************************
 * edge_unthrottle
 *	this function is called by the tty driver when it wants to resume the
 *	data being read from the port (called after SerialThrottle is called)
 *****************************************************************************/
static void edge_unthrottle(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct edgeport_port *edge_port = usb_get_serial_port_data(port);
	int status;

	dbg("%s - port %d", __func__, port->number);

	if (edge_port == NULL)
		return;

	if (!edge_port->open) {
		dbg("%s - port not opened", __func__);
		return;
	}

	/* if we are implementing XON/XOFF, send the start character */
	if (I_IXOFF(tty)) {
		unsigned char start_char = START_CHAR(tty);
		status = edge_write(tty, port, &start_char, 1);
		if (status <= 0)
			return;
	}
	/* if we are implementing RTS/CTS, toggle that line */
	if (tty->termios->c_cflag & CRTSCTS) {
		edge_port->shadowMCR |= MCR_RTS;
		send_cmd_write_uart_register(edge_port, MCR,
						edge_port->shadowMCR);
	}
}


/*****************************************************************************
 * SerialSetTermios
 *	this function is called by the tty driver when it wants to change
 * the termios structure
 *****************************************************************************/
static void edge_set_termios(struct tty_struct *tty,
	struct usb_serial_port *port, struct ktermios *old_termios)
{
	struct edgeport_port *edge_port = usb_get_serial_port_data(port);
	unsigned int cflag;

	cflag = tty->termios->c_cflag;
	dbg("%s - clfag %08x iflag %08x", __func__,
	    tty->termios->c_cflag, tty->termios->c_iflag);
	dbg("%s - old clfag %08x old iflag %08x", __func__,
	    old_termios->c_cflag, old_termios->c_iflag);

	dbg("%s - port %d", __func__, port->number);

	if (edge_port == NULL)
		return;

	if (!edge_port->open) {
		dbg("%s - port not opened", __func__);
		return;
	}

	/* change the port settings to the new ones specified */
	change_port_settings(tty, edge_port, old_termios);
}


/*****************************************************************************
 * get_lsr_info - get line status register info
 *
 * Purpose: Let user call ioctl() to get info when the UART physically
 * 	    is emptied.  On bus types like RS485, the transmitter must
 * 	    release the bus after transmitting. This must be done when
 * 	    the transmit shift register is empty, not be done when the
 * 	    transmit holding register is empty.  This functionality
 * 	    allows an RS485 driver to be written in user space.
 *****************************************************************************/
static int get_lsr_info(struct edgeport_port *edge_port,
						unsigned int __user *value)
{
	unsigned int result = 0;
	unsigned long flags;

	spin_lock_irqsave(&edge_port->ep_lock, flags);
	if (edge_port->maxTxCredits == edge_port->txCredits &&
	    edge_port->txfifo.count == 0) {
		dbg("%s -- Empty", __func__);
		result = TIOCSER_TEMT;
	}
	spin_unlock_irqrestore(&edge_port->ep_lock, flags);

	if (copy_to_user(value, &result, sizeof(int)))
		return -EFAULT;
	return 0;
}

static int edge_tiocmset(struct tty_struct *tty,
					unsigned int set, unsigned int clear)
{
	struct usb_serial_port *port = tty->driver_data;
	struct edgeport_port *edge_port = usb_get_serial_port_data(port);
	unsigned int mcr;

	dbg("%s - port %d", __func__, port->number);

	mcr = edge_port->shadowMCR;
	if (set & TIOCM_RTS)
		mcr |= MCR_RTS;
	if (set & TIOCM_DTR)
		mcr |= MCR_DTR;
	if (set & TIOCM_LOOP)
		mcr |= MCR_LOOPBACK;

	if (clear & TIOCM_RTS)
		mcr &= ~MCR_RTS;
	if (clear & TIOCM_DTR)
		mcr &= ~MCR_DTR;
	if (clear & TIOCM_LOOP)
		mcr &= ~MCR_LOOPBACK;

	edge_port->shadowMCR = mcr;

	send_cmd_write_uart_register(edge_port, MCR, edge_port->shadowMCR);

	return 0;
}

static int edge_tiocmget(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct edgeport_port *edge_port = usb_get_serial_port_data(port);
	unsigned int result = 0;
	unsigned int msr;
	unsigned int mcr;

	dbg("%s - port %d", __func__, port->number);

	msr = edge_port->shadowMSR;
	mcr = edge_port->shadowMCR;
	result = ((mcr & MCR_DTR)	? TIOCM_DTR: 0)	  /* 0x002 */
		  | ((mcr & MCR_RTS)	? TIOCM_RTS: 0)   /* 0x004 */
		  | ((msr & EDGEPORT_MSR_CTS)	? TIOCM_CTS: 0)   /* 0x020 */
		  | ((msr & EDGEPORT_MSR_CD)	? TIOCM_CAR: 0)   /* 0x040 */
		  | ((msr & EDGEPORT_MSR_RI)	? TIOCM_RI:  0)   /* 0x080 */
		  | ((msr & EDGEPORT_MSR_DSR)	? TIOCM_DSR: 0);  /* 0x100 */


	dbg("%s -- %x", __func__, result);

	return result;
}

static int edge_get_icount(struct tty_struct *tty,
				struct serial_icounter_struct *icount)
{
	struct usb_serial_port *port = tty->driver_data;
	struct edgeport_port *edge_port = usb_get_serial_port_data(port);
	struct async_icount cnow;
	cnow = edge_port->icount;

	icount->cts = cnow.cts;
	icount->dsr = cnow.dsr;
	icount->rng = cnow.rng;
	icount->dcd = cnow.dcd;
	icount->rx = cnow.rx;
	icount->tx = cnow.tx;
	icount->frame = cnow.frame;
	icount->overrun = cnow.overrun;
	icount->parity = cnow.parity;
	icount->brk = cnow.brk;
	icount->buf_overrun = cnow.buf_overrun;

	dbg("%s (%d) TIOCGICOUNT RX=%d, TX=%d",
			__func__,  port->number, icount->rx, icount->tx);
	return 0;
}

static int get_serial_info(struct edgeport_port *edge_port,
				struct serial_struct __user *retinfo)
{
	struct serial_struct tmp;

	if (!retinfo)
		return -EFAULT;

	memset(&tmp, 0, sizeof(tmp));

	tmp.type		= PORT_16550A;
	tmp.line		= edge_port->port->serial->minor;
	tmp.port		= edge_port->port->number;
	tmp.irq			= 0;
	tmp.flags		= ASYNC_SKIP_TEST | ASYNC_AUTO_IRQ;
	tmp.xmit_fifo_size	= edge_port->maxTxCredits;
	tmp.baud_base		= 9600;
	tmp.close_delay		= 5*HZ;
	tmp.closing_wait	= 30*HZ;

	if (copy_to_user(retinfo, &tmp, sizeof(*retinfo)))
		return -EFAULT;
	return 0;
}


/*****************************************************************************
 * SerialIoctl
 *	this function handles any ioctl calls to the driver
 *****************************************************************************/
static int edge_ioctl(struct tty_struct *tty,
					unsigned int cmd, unsigned long arg)
{
	struct usb_serial_port *port = tty->driver_data;
	DEFINE_WAIT(wait);
	struct edgeport_port *edge_port = usb_get_serial_port_data(port);
	struct async_icount cnow;
	struct async_icount cprev;

	dbg("%s - port %d, cmd = 0x%x", __func__, port->number, cmd);

	switch (cmd) {
	case TIOCSERGETLSR:
		dbg("%s (%d) TIOCSERGETLSR", __func__,  port->number);
		return get_lsr_info(edge_port, (unsigned int __user *) arg);

	case TIOCGSERIAL:
		dbg("%s (%d) TIOCGSERIAL", __func__,  port->number);
		return get_serial_info(edge_port, (struct serial_struct __user *) arg);

	case TIOCMIWAIT:
		dbg("%s (%d) TIOCMIWAIT", __func__,  port->number);
		cprev = edge_port->icount;
		while (1) {
			prepare_to_wait(&edge_port->delta_msr_wait,
						&wait, TASK_INTERRUPTIBLE);
			schedule();
			finish_wait(&edge_port->delta_msr_wait, &wait);
			/* see if a signal did it */
			if (signal_pending(current))
				return -ERESTARTSYS;
			cnow = edge_port->icount;
			if (cnow.rng == cprev.rng && cnow.dsr == cprev.dsr &&
			    cnow.dcd == cprev.dcd && cnow.cts == cprev.cts)
				return -EIO; /* no change => error */
			if (((arg & TIOCM_RNG) && (cnow.rng != cprev.rng)) ||
			    ((arg & TIOCM_DSR) && (cnow.dsr != cprev.dsr)) ||
			    ((arg & TIOCM_CD)  && (cnow.dcd != cprev.dcd)) ||
			    ((arg & TIOCM_CTS) && (cnow.cts != cprev.cts))) {
				return 0;
			}
			cprev = cnow;
		}
		/* NOTREACHED */
		break;

	}
	return -ENOIOCTLCMD;
}


/*****************************************************************************
 * SerialBreak
 *	this function sends a break to the port
 *****************************************************************************/
static void edge_break(struct tty_struct *tty, int break_state)
{
	struct usb_serial_port *port = tty->driver_data;
	struct edgeport_port *edge_port = usb_get_serial_port_data(port);
	struct edgeport_serial *edge_serial = usb_get_serial_data(port->serial);
	int status;

	if ((!edge_serial->is_epic) ||
	    ((edge_serial->is_epic) &&
	     (edge_serial->epic_descriptor.Supports.IOSPChase))) {
		/* flush and chase */
		edge_port->chaseResponsePending = true;

		dbg("%s - Sending IOSP_CMD_CHASE_PORT", __func__);
		status = send_iosp_ext_cmd(edge_port, IOSP_CMD_CHASE_PORT, 0);
		if (status == 0) {
			/* block until chase finished */
			block_until_chase_response(edge_port);
		} else {
			edge_port->chaseResponsePending = false;
		}
	}

	if ((!edge_serial->is_epic) ||
	    ((edge_serial->is_epic) &&
	     (edge_serial->epic_descriptor.Supports.IOSPSetClrBreak))) {
		if (break_state == -1) {
			dbg("%s - Sending IOSP_CMD_SET_BREAK", __func__);
			status = send_iosp_ext_cmd(edge_port,
						IOSP_CMD_SET_BREAK, 0);
		} else {
			dbg("%s - Sending IOSP_CMD_CLEAR_BREAK", __func__);
			status = send_iosp_ext_cmd(edge_port,
						IOSP_CMD_CLEAR_BREAK, 0);
		}
		if (status)
			dbg("%s - error sending break set/clear command.",
				__func__);
	}
}


/*****************************************************************************
 * process_rcvd_data
 *	this function handles the data received on the bulk in pipe.
 *****************************************************************************/
static void process_rcvd_data(struct edgeport_serial *edge_serial,
				unsigned char *buffer, __u16 bufferLength)
{
	struct usb_serial_port *port;
	struct edgeport_port *edge_port;
	struct tty_struct *tty;
	__u16 lastBufferLength;
	__u16 rxLen;

	dbg("%s", __func__);

	lastBufferLength = bufferLength + 1;

	while (bufferLength > 0) {
		/* failsafe incase we get a message that we don't understand */
		if (lastBufferLength == bufferLength) {
			dbg("%s - stuck in loop, exiting it.", __func__);
			break;
		}
		lastBufferLength = bufferLength;

		switch (edge_serial->rxState) {
		case EXPECT_HDR1:
			edge_serial->rxHeader1 = *buffer;
			++buffer;
			--bufferLength;

			if (bufferLength == 0) {
				edge_serial->rxState = EXPECT_HDR2;
				break;
			}
			/* otherwise, drop on through */
		case EXPECT_HDR2:
			edge_serial->rxHeader2 = *buffer;
			++buffer;
			--bufferLength;

			dbg("%s - Hdr1=%02X Hdr2=%02X", __func__,
			    edge_serial->rxHeader1, edge_serial->rxHeader2);
			/* Process depending on whether this header is
			 * data or status */

			if (IS_CMD_STAT_HDR(edge_serial->rxHeader1)) {
				/* Decode this status header and go to
				 * EXPECT_HDR1 (if we can process the status
				 * with only 2 bytes), or go to EXPECT_HDR3 to
				 * get the third byte. */
				edge_serial->rxPort =
				    IOSP_GET_HDR_PORT(edge_serial->rxHeader1);
				edge_serial->rxStatusCode =
				    IOSP_GET_STATUS_CODE(
						edge_serial->rxHeader1);

				if (!IOSP_STATUS_IS_2BYTE(
						edge_serial->rxStatusCode)) {
					/* This status needs additional bytes.
					 * Save what we have and then wait for
					 * more data.
					 */
					edge_serial->rxStatusParam
						= edge_serial->rxHeader2;
					edge_serial->rxState = EXPECT_HDR3;
					break;
				}
				/* We have all the header bytes, process the
				   status now */
				process_rcvd_status(edge_serial,
						edge_serial->rxHeader2, 0);
				edge_serial->rxState = EXPECT_HDR1;
				break;
			} else {
				edge_serial->rxPort =
				    IOSP_GET_HDR_PORT(edge_serial->rxHeader1);
				edge_serial->rxBytesRemaining =
				    IOSP_GET_HDR_DATA_LEN(
						edge_serial->rxHeader1,
						edge_serial->rxHeader2);
				dbg("%s - Data for Port %u Len %u",
						__func__,
						edge_serial->rxPort,
						edge_serial->rxBytesRemaining);

				/* ASSERT(DevExt->RxPort < DevExt->NumPorts);
				 * ASSERT(DevExt->RxBytesRemaining <
				 *		IOSP_MAX_DATA_LENGTH);
				 */

				if (bufferLength == 0) {
					edge_serial->rxState = EXPECT_DATA;
					break;
				}
				/* Else, drop through */
			}
		case EXPECT_DATA: /* Expect data */
			if (bufferLength < edge_serial->rxBytesRemaining) {
				rxLen = bufferLength;
				/* Expect data to start next buffer */
				edge_serial->rxState = EXPECT_DATA;
			} else {
				/* BufLen >= RxBytesRemaining */
				rxLen = edge_serial->rxBytesRemaining;
				/* Start another header next time */
				edge_serial->rxState = EXPECT_HDR1;
			}

			bufferLength -= rxLen;
			edge_serial->rxBytesRemaining -= rxLen;

			/* spit this data back into the tty driver if this
			   port is open */
			if (rxLen) {
				port = edge_serial->serial->port[
							edge_serial->rxPort];
				edge_port = usb_get_serial_port_data(port);
				if (edge_port->open) {
					tty = tty_port_tty_get(
						&edge_port->port->port);
					if (tty) {
						dbg("%s - Sending %d bytes to TTY for port %d",
							__func__, rxLen, edge_serial->rxPort);
						edge_tty_recv(&edge_serial->serial->dev->dev, tty, buffer, rxLen);
						tty_kref_put(tty);
					}
					edge_port->icount.rx += rxLen;
				}
				buffer += rxLen;
			}
			break;

		case EXPECT_HDR3:	/* Expect 3rd byte of status header */
			edge_serial->rxHeader3 = *buffer;
			++buffer;
			--bufferLength;

			/* We have all the header bytes, process the
			   status now */
			process_rcvd_status(edge_serial,
				edge_serial->rxStatusParam,
				edge_serial->rxHeader3);
			edge_serial->rxState = EXPECT_HDR1;
			break;
		}
	}
}


/*****************************************************************************
 * process_rcvd_status
 *	this function handles the any status messages received on the
 *	bulk in pipe.
 *****************************************************************************/
static void process_rcvd_status(struct edgeport_serial *edge_serial,
						__u8 byte2, __u8 byte3)
{
	struct usb_serial_port *port;
	struct edgeport_port *edge_port;
	struct tty_struct *tty;
	__u8 code = edge_serial->rxStatusCode;

	/* switch the port pointer to the one being currently talked about */
	port = edge_serial->serial->port[edge_serial->rxPort];
	edge_port = usb_get_serial_port_data(port);
	if (edge_port == NULL) {
		dev_err(&edge_serial->serial->dev->dev,
			"%s - edge_port == NULL for port %d\n",
					__func__, edge_serial->rxPort);
		return;
	}

	dbg("%s - port %d", __func__, edge_serial->rxPort);

	if (code == IOSP_EXT_STATUS) {
		switch (byte2) {
		case IOSP_EXT_STATUS_CHASE_RSP:
			/* we want to do EXT status regardless of port
			 * open/closed */
			dbg("%s - Port %u EXT CHASE_RSP Data = %02x",
					__func__, edge_serial->rxPort, byte3);
			/* Currently, the only EXT_STATUS is Chase, so process
			 * here instead of one more call to one more subroutine
			 * If/when more EXT_STATUS, there'll be more work to do
			 * Also, we currently clear flag and close the port
			 * regardless of content of above's Byte3.
			 * We could choose to do something else when Byte3 says
			 * Timeout on Chase from Edgeport, like wait longer in
			 * block_until_chase_response, but for now we don't.
			 */
			edge_port->chaseResponsePending = false;
			wake_up(&edge_port->wait_chase);
			return;

		case IOSP_EXT_STATUS_RX_CHECK_RSP:
			dbg("%s ========== Port %u CHECK_RSP Sequence = %02x =============", __func__, edge_serial->rxPort, byte3);
			/* Port->RxCheckRsp = true; */
			return;
		}
	}

	if (code == IOSP_STATUS_OPEN_RSP) {
		edge_port->txCredits = GET_TX_BUFFER_SIZE(byte3);
		edge_port->maxTxCredits = edge_port->txCredits;
		dbg("%s - Port %u Open Response Initial MSR = %02x TxBufferSize = %d", __func__, edge_serial->rxPort, byte2, edge_port->txCredits);
		handle_new_msr(edge_port, byte2);

		/* send the current line settings to the port so we are
		   in sync with any further termios calls */
		tty = tty_port_tty_get(&edge_port->port->port);
		if (tty) {
			change_port_settings(tty,
				edge_port, tty->termios);
			tty_kref_put(tty);
		}

		/* we have completed the open */
		edge_port->openPending = false;
		edge_port->open = true;
		wake_up(&edge_port->wait_open);
		return;
	}

	/* If port is closed, silently discard all rcvd status. We can
	 * have cases where buffered status is received AFTER the close
	 * port command is sent to the Edgeport.
	 */
	if (!edge_port->open || edge_port->closePending)
		return;

	switch (code) {
	/* Not currently sent by Edgeport */
	case IOSP_STATUS_LSR:
		dbg("%s - Port %u LSR Status = %02x",
					__func__, edge_serial->rxPort, byte2);
		handle_new_lsr(edge_port, false, byte2, 0);
		break;

	case IOSP_STATUS_LSR_DATA:
		dbg("%s - Port %u LSR Status = %02x, Data = %02x",
				__func__, edge_serial->rxPort, byte2, byte3);
		/* byte2 is LSR Register */
		/* byte3 is broken data byte */
		handle_new_lsr(edge_port, true, byte2, byte3);
		break;
	/*
	 *	case IOSP_EXT_4_STATUS:
	 *		dbg("%s - Port %u LSR Status = %02x Data = %02x",
	 *			__func__, edge_serial->rxPort, byte2, byte3);
	 *		break;
	 */
	case IOSP_STATUS_MSR:
		dbg("%s - Port %u MSR Status = %02x",
					__func__, edge_serial->rxPort, byte2);
		/*
		 * Process this new modem status and generate appropriate
		 * events, etc, based on the new status. This routine
		 * also saves the MSR in Port->ShadowMsr.
		 */
		handle_new_msr(edge_port, byte2);
		break;

	default:
		dbg("%s - Unrecognized IOSP status code %u", __func__, code);
		break;
	}
}


/*****************************************************************************
 * edge_tty_recv
 *	this function passes data on to the tty flip buffer
 *****************************************************************************/
static void edge_tty_recv(struct device *dev, struct tty_struct *tty,
					unsigned char *data, int length)
{
	int cnt;

	cnt = tty_insert_flip_string(tty, data, length);
	if (cnt < length) {
		dev_err(dev, "%s - dropping data, %d bytes lost\n",
				__func__, length - cnt);
	}
	data += cnt;
	length -= cnt;

	tty_flip_buffer_push(tty);
}


/*****************************************************************************
 * handle_new_msr
 *	this function handles any change to the msr register for a port.
 *****************************************************************************/
static void handle_new_msr(struct edgeport_port *edge_port, __u8 newMsr)
{
	struct  async_icount *icount;

	dbg("%s %02x", __func__, newMsr);

	if (newMsr & (EDGEPORT_MSR_DELTA_CTS | EDGEPORT_MSR_DELTA_DSR |
			EDGEPORT_MSR_DELTA_RI | EDGEPORT_MSR_DELTA_CD)) {
		icount = &edge_port->icount;

		/* update input line counters */
		if (newMsr & EDGEPORT_MSR_DELTA_CTS)
			icount->cts++;
		if (newMsr & EDGEPORT_MSR_DELTA_DSR)
			icount->dsr++;
		if (newMsr & EDGEPORT_MSR_DELTA_CD)
			icount->dcd++;
		if (newMsr & EDGEPORT_MSR_DELTA_RI)
			icount->rng++;
		wake_up_interruptible(&edge_port->delta_msr_wait);
	}

	/* Save the new modem status */
	edge_port->shadowMSR = newMsr & 0xf0;
}


/*****************************************************************************
 * handle_new_lsr
 *	this function handles any change to the lsr register for a port.
 *****************************************************************************/
static void handle_new_lsr(struct edgeport_port *edge_port, __u8 lsrData,
							__u8 lsr, __u8 data)
{
	__u8 newLsr = (__u8) (lsr & (__u8)
		(LSR_OVER_ERR | LSR_PAR_ERR | LSR_FRM_ERR | LSR_BREAK));
	struct async_icount *icount;

	dbg("%s - %02x", __func__, newLsr);

	edge_port->shadowLSR = lsr;

	if (newLsr & LSR_BREAK) {
		/*
		 * Parity and Framing errors only count if they
		 * occur exclusive of a break being
		 * received.
		 */
		newLsr &= (__u8)(LSR_OVER_ERR | LSR_BREAK);
	}

	/* Place LSR data byte into Rx buffer */
	if (lsrData) {
		struct tty_struct *tty =
				tty_port_tty_get(&edge_port->port->port);
		if (tty) {
			edge_tty_recv(&edge_port->port->dev, tty, &data, 1);
			tty_kref_put(tty);
		}
	}
	/* update input line counters */
	icount = &edge_port->icount;
	if (newLsr & LSR_BREAK)
		icount->brk++;
	if (newLsr & LSR_OVER_ERR)
		icount->overrun++;
	if (newLsr & LSR_PAR_ERR)
		icount->parity++;
	if (newLsr & LSR_FRM_ERR)
		icount->frame++;
}


/****************************************************************************
 * sram_write
 *	writes a number of bytes to the Edgeport device's sram starting at the
 *	given address.
 *	If successful returns the number of bytes written, otherwise it returns
 *	a negative error number of the problem.
 ****************************************************************************/
static int sram_write(struct usb_serial *serial, __u16 extAddr, __u16 addr,
					__u16 length, const __u8 *data)
{
	int result;
	__u16 current_length;
	unsigned char *transfer_buffer;

	dbg("%s - %x, %x, %d", __func__, extAddr, addr, length);

	transfer_buffer =  kmalloc(64, GFP_KERNEL);
	if (!transfer_buffer) {
		dev_err(&serial->dev->dev, "%s - kmalloc(%d) failed.\n",
							__func__, 64);
		return -ENOMEM;
	}

	/* need to split these writes up into 64 byte chunks */
	result = 0;
	while (length > 0) {
		if (length > 64)
			current_length = 64;
		else
			current_length = length;

/*		dbg("%s - writing %x, %x, %d", __func__,
					extAddr, addr, current_length); */
		memcpy(transfer_buffer, data, current_length);
		result = usb_control_msg(serial->dev,
					usb_sndctrlpipe(serial->dev, 0),
					USB_REQUEST_ION_WRITE_RAM,
					0x40, addr, extAddr, transfer_buffer,
					current_length, 300);
		if (result < 0)
			break;
		length -= current_length;
		addr += current_length;
		data += current_length;
	}

	kfree(transfer_buffer);
	return result;
}


/****************************************************************************
 * rom_write
 *	writes a number of bytes to the Edgeport device's ROM starting at the
 *	given address.
 *	If successful returns the number of bytes written, otherwise it returns
 *	a negative error number of the problem.
 ****************************************************************************/
static int rom_write(struct usb_serial *serial, __u16 extAddr, __u16 addr,
					__u16 length, const __u8 *data)
{
	int result;
	__u16 current_length;
	unsigned char *transfer_buffer;

/*	dbg("%s - %x, %x, %d", __func__, extAddr, addr, length); */

	transfer_buffer =  kmalloc(64, GFP_KERNEL);
	if (!transfer_buffer) {
		dev_err(&serial->dev->dev, "%s - kmalloc(%d) failed.\n",
								__func__, 64);
		return -ENOMEM;
	}

	/* need to split these writes up into 64 byte chunks */
	result = 0;
	while (length > 0) {
		if (length > 64)
			current_length = 64;
		else
			current_length = length;
/*		dbg("%s - writing %x, %x, %d", __func__,
					extAddr, addr, current_length); */
		memcpy(transfer_buffer, data, current_length);
		result = usb_control_msg(serial->dev,
					usb_sndctrlpipe(serial->dev, 0),
					USB_REQUEST_ION_WRITE_ROM, 0x40,
					addr, extAddr,
					transfer_buffer, current_length, 300);
		if (result < 0)
			break;
		length -= current_length;
		addr += current_length;
		data += current_length;
	}

	kfree(transfer_buffer);
	return result;
}


/****************************************************************************
 * rom_read
 *	reads a number of bytes from the Edgeport device starting at the given
 *	address.
 *	If successful returns the number of bytes read, otherwise it returns
 *	a negative error number of the problem.
 ****************************************************************************/
static int rom_read(struct usb_serial *serial, __u16 extAddr,
					__u16 addr, __u16 length, __u8 *data)
{
	int result;
	__u16 current_length;
	unsigned char *transfer_buffer;

	dbg("%s - %x, %x, %d", __func__, extAddr, addr, length);

	transfer_buffer =  kmalloc(64, GFP_KERNEL);
	if (!transfer_buffer) {
		dev_err(&serial->dev->dev,
			"%s - kmalloc(%d) failed.\n", __func__, 64);
		return -ENOMEM;
	}

	/* need to split these reads up into 64 byte chunks */
	result = 0;
	while (length > 0) {
		if (length > 64)
			current_length = 64;
		else
			current_length = length;
/*		dbg("%s - %x, %x, %d", __func__,
				extAddr, addr, current_length); */
		result = usb_control_msg(serial->dev,
					usb_rcvctrlpipe(serial->dev, 0),
					USB_REQUEST_ION_READ_ROM,
					0xC0, addr, extAddr, transfer_buffer,
					current_length, 300);
		if (result < 0)
			break;
		memcpy(data, transfer_buffer, current_length);
		length -= current_length;
		addr += current_length;
		data += current_length;
	}

	kfree(transfer_buffer);
	return result;
}


/****************************************************************************
 * send_iosp_ext_cmd
 *	Is used to send a IOSP message to the Edgeport device
 ****************************************************************************/
static int send_iosp_ext_cmd(struct edgeport_port *edge_port,
						__u8 command, __u8 param)
{
	unsigned char   *buffer;
	unsigned char   *currentCommand;
	int             length = 0;
	int             status = 0;

	dbg("%s - %d, %d", __func__, command, param);

	buffer = kmalloc(10, GFP_ATOMIC);
	if (!buffer) {
		dev_err(&edge_port->port->dev,
				"%s - kmalloc(%d) failed.\n", __func__, 10);
		return -ENOMEM;
	}

	currentCommand = buffer;

	MAKE_CMD_EXT_CMD(&currentCommand, &length,
		edge_port->port->number - edge_port->port->serial->minor,
		command, param);

	status = write_cmd_usb(edge_port, buffer, length);
	if (status) {
		/* something bad happened, let's free up the memory */
		kfree(buffer);
	}

	return status;
}


/*****************************************************************************
 * write_cmd_usb
 *	this function writes the given buffer out to the bulk write endpoint.
 *****************************************************************************/
static int write_cmd_usb(struct edgeport_port *edge_port,
					unsigned char *buffer, int length)
{
	struct edgeport_serial *edge_serial =
				usb_get_serial_data(edge_port->port->serial);
	int status = 0;
	struct urb *urb;

	usb_serial_debug_data(debug, &edge_port->port->dev,
						__func__, length, buffer);

	/* Allocate our next urb */
	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!urb)
		return -ENOMEM;

	atomic_inc(&CmdUrbs);
	dbg("%s - ALLOCATE URB %p (outstanding %d)",
				__func__, urb, atomic_read(&CmdUrbs));

	usb_fill_bulk_urb(urb, edge_serial->serial->dev,
			usb_sndbulkpipe(edge_serial->serial->dev,
					edge_serial->bulk_out_endpoint),
			buffer, length, edge_bulk_out_cmd_callback, edge_port);

	edge_port->commandPending = true;
	status = usb_submit_urb(urb, GFP_ATOMIC);

	if (status) {
		/* something went wrong */
		dev_err(&edge_port->port->dev,
		    "%s - usb_submit_urb(write command) failed, status = %d\n",
							__func__, status);
		usb_kill_urb(urb);
		usb_free_urb(urb);
		atomic_dec(&CmdUrbs);
		return status;
	}

#if 0
	wait_event(&edge_port->wait_command, !edge_port->commandPending);

	if (edge_port->commandPending) {
		/* command timed out */
		dbg("%s - command timed out", __func__);
		status = -EINVAL;
	}
#endif
	return status;
}


/*****************************************************************************
 * send_cmd_write_baud_rate
 *	this function sends the proper command to change the baud rate of the
 *	specified port.
 *****************************************************************************/
static int send_cmd_write_baud_rate(struct edgeport_port *edge_port,
								int baudRate)
{
	struct edgeport_serial *edge_serial =
				usb_get_serial_data(edge_port->port->serial);
	unsigned char *cmdBuffer;
	unsigned char *currCmd;
	int cmdLen = 0;
	int divisor;
	int status;
	unsigned char number =
		edge_port->port->number - edge_port->port->serial->minor;

	if (edge_serial->is_epic &&
	    !edge_serial->epic_descriptor.Supports.IOSPSetBaudRate) {
		dbg("SendCmdWriteBaudRate - NOT Setting baud rate for port = %d, baud = %d",
		    edge_port->port->number, baudRate);
		return 0;
	}

	dbg("%s - port = %d, baud = %d", __func__,
					edge_port->port->number, baudRate);

	status = calc_baud_rate_divisor(baudRate, &divisor);
	if (status) {
		dev_err(&edge_port->port->dev, "%s - bad baud rate\n",
								__func__);
		return status;
	}

	/* Alloc memory for the string of commands. */
	cmdBuffer =  kmalloc(0x100, GFP_ATOMIC);
	if (!cmdBuffer) {
		dev_err(&edge_port->port->dev,
			"%s - kmalloc(%d) failed.\n", __func__, 0x100);
		return -ENOMEM;
	}
	currCmd = cmdBuffer;

	/* Enable access to divisor latch */
	MAKE_CMD_WRITE_REG(&currCmd, &cmdLen, number, LCR, LCR_DL_ENABLE);

	/* Write the divisor itself */
	MAKE_CMD_WRITE_REG(&currCmd, &cmdLen, number, DLL, LOW8(divisor));
	MAKE_CMD_WRITE_REG(&currCmd, &cmdLen, number, DLM, HIGH8(divisor));

	/* Restore original value to disable access to divisor latch */
	MAKE_CMD_WRITE_REG(&currCmd, &cmdLen, number, LCR,
						edge_port->shadowLCR);

	status = write_cmd_usb(edge_port, cmdBuffer, cmdLen);
	if (status) {
		/* something bad happened, let's free up the memory */
		kfree(cmdBuffer);
	}

	return status;
}


/*****************************************************************************
 * calc_baud_rate_divisor
 *	this function calculates the proper baud rate divisor for the specified
 *	baud rate.
 *****************************************************************************/
static int calc_baud_rate_divisor(int baudrate, int *divisor)
{
	int i;
	__u16 custom;


	dbg("%s - %d", __func__, baudrate);

	for (i = 0; i < ARRAY_SIZE(divisor_table); i++) {
		if (divisor_table[i].BaudRate == baudrate) {
			*divisor = divisor_table[i].Divisor;
			return 0;
		}
	}

	/* We have tried all of the standard baud rates
	 * lets try to calculate the divisor for this baud rate
	 * Make sure the baud rate is reasonable */
	if (baudrate > 50 && baudrate < 230400) {
		/* get divisor */
		custom = (__u16)((230400L + baudrate/2) / baudrate);

		*divisor = custom;

		dbg("%s - Baud %d = %d", __func__, baudrate, custom);
		return 0;
	}

	return -1;
}


/*****************************************************************************
 * send_cmd_write_uart_register
 *  this function builds up a uart register message and sends to the device.
 *****************************************************************************/
static int send_cmd_write_uart_register(struct edgeport_port *edge_port,
						__u8 regNum, __u8 regValue)
{
	struct edgeport_serial *edge_serial =
				usb_get_serial_data(edge_port->port->serial);
	unsigned char *cmdBuffer;
	unsigned char *currCmd;
	unsigned long cmdLen = 0;
	int status;

	dbg("%s - write to %s register 0x%02x",
			(regNum == MCR) ? "MCR" : "LCR", __func__, regValue);

	if (edge_serial->is_epic &&
	    !edge_serial->epic_descriptor.Supports.IOSPWriteMCR &&
	    regNum == MCR) {
		dbg("SendCmdWriteUartReg - Not writing to MCR Register");
		return 0;
	}

	if (edge_serial->is_epic &&
	    !edge_serial->epic_descriptor.Supports.IOSPWriteLCR &&
	    regNum == LCR) {
		dbg("SendCmdWriteUartReg - Not writing to LCR Register");
		return 0;
	}

	/* Alloc memory for the string of commands. */
	cmdBuffer = kmalloc(0x10, GFP_ATOMIC);
	if (cmdBuffer == NULL)
		return -ENOMEM;

	currCmd = cmdBuffer;

	/* Build a cmd in the buffer to write the given register */
	MAKE_CMD_WRITE_REG(&currCmd, &cmdLen,
		edge_port->port->number - edge_port->port->serial->minor,
		regNum, regValue);

	status = write_cmd_usb(edge_port, cmdBuffer, cmdLen);
	if (status) {
		/* something bad happened, let's free up the memory */
		kfree(cmdBuffer);
	}

	return status;
}


/*****************************************************************************
 * change_port_settings
 *	This routine is called to set the UART on the device to match the
 *	specified new settings.
 *****************************************************************************/

static void change_port_settings(struct tty_struct *tty,
	struct edgeport_port *edge_port, struct ktermios *old_termios)
{
	struct edgeport_serial *edge_serial =
			usb_get_serial_data(edge_port->port->serial);
	int baud;
	unsigned cflag;
	__u8 mask = 0xff;
	__u8 lData;
	__u8 lParity;
	__u8 lStop;
	__u8 rxFlow;
	__u8 txFlow;
	int status;

	dbg("%s - port %d", __func__, edge_port->port->number);

	if (!edge_port->open &&
	    !edge_port->openPending) {
		dbg("%s - port not opened", __func__);
		return;
	}

	cflag = tty->termios->c_cflag;

	switch (cflag & CSIZE) {
	case CS5:
		lData = LCR_BITS_5; mask = 0x1f;
		dbg("%s - data bits = 5", __func__);
		break;
	case CS6:
		lData = LCR_BITS_6; mask = 0x3f;
		dbg("%s - data bits = 6", __func__);
		break;
	case CS7:
		lData = LCR_BITS_7; mask = 0x7f;
		dbg("%s - data bits = 7", __func__);
		break;
	default:
	case CS8:
		lData = LCR_BITS_8;
		dbg("%s - data bits = 8", __func__);
		break;
	}

	lParity = LCR_PAR_NONE;
	if (cflag & PARENB) {
		if (cflag & CMSPAR) {
			if (cflag & PARODD) {
				lParity = LCR_PAR_MARK;
				dbg("%s - parity = mark", __func__);
			} else {
				lParity = LCR_PAR_SPACE;
				dbg("%s - parity = space", __func__);
			}
		} else if (cflag & PARODD) {
			lParity = LCR_PAR_ODD;
			dbg("%s - parity = odd", __func__);
		} else {
			lParity = LCR_PAR_EVEN;
			dbg("%s - parity = even", __func__);
		}
	} else {
		dbg("%s - parity = none", __func__);
	}

	if (cflag & CSTOPB) {
		lStop = LCR_STOP_2;
		dbg("%s - stop bits = 2", __func__);
	} else {
		lStop = LCR_STOP_1;
		dbg("%s - stop bits = 1", __func__);
	}

	/* figure out the flow control settings */
	rxFlow = txFlow = 0x00;
	if (cflag & CRTSCTS) {
		rxFlow |= IOSP_RX_FLOW_RTS;
		txFlow |= IOSP_TX_FLOW_CTS;
		dbg("%s - RTS/CTS is enabled", __func__);
	} else {
		dbg("%s - RTS/CTS is disabled", __func__);
	}

	/* if we are implementing XON/XOFF, set the start and stop character
	   in the device */
	if (I_IXOFF(tty) || I_IXON(tty)) {
		unsigned char stop_char  = STOP_CHAR(tty);
		unsigned char start_char = START_CHAR(tty);

		if ((!edge_serial->is_epic) ||
		    ((edge_serial->is_epic) &&
		     (edge_serial->epic_descriptor.Supports.IOSPSetXChar))) {
			send_iosp_ext_cmd(edge_port,
					IOSP_CMD_SET_XON_CHAR, start_char);
			send_iosp_ext_cmd(edge_port,
					IOSP_CMD_SET_XOFF_CHAR, stop_char);
		}

		/* if we are implementing INBOUND XON/XOFF */
		if (I_IXOFF(tty)) {
			rxFlow |= IOSP_RX_FLOW_XON_XOFF;
			dbg("%s - INBOUND XON/XOFF is enabled, XON = %2x, XOFF = %2x",
					__func__, start_char, stop_char);
		} else {
			dbg("%s - INBOUND XON/XOFF is disabled", __func__);
		}

		/* if we are implementing OUTBOUND XON/XOFF */
		if (I_IXON(tty)) {
			txFlow |= IOSP_TX_FLOW_XON_XOFF;
			dbg("%s - OUTBOUND XON/XOFF is enabled, XON = %2x, XOFF = %2x",
					__func__, start_char, stop_char);
		} else {
			dbg("%s - OUTBOUND XON/XOFF is disabled", __func__);
		}
	}

	/* Set flow control to the configured value */
	if ((!edge_serial->is_epic) ||
	    ((edge_serial->is_epic) &&
	     (edge_serial->epic_descriptor.Supports.IOSPSetRxFlow)))
		send_iosp_ext_cmd(edge_port, IOSP_CMD_SET_RX_FLOW, rxFlow);
	if ((!edge_serial->is_epic) ||
	    ((edge_serial->is_epic) &&
	     (edge_serial->epic_descriptor.Supports.IOSPSetTxFlow)))
		send_iosp_ext_cmd(edge_port, IOSP_CMD_SET_TX_FLOW, txFlow);


	edge_port->shadowLCR &= ~(LCR_BITS_MASK | LCR_STOP_MASK | LCR_PAR_MASK);
	edge_port->shadowLCR |= (lData | lParity | lStop);

	edge_port->validDataMask = mask;

	/* Send the updated LCR value to the EdgePort */
	status = send_cmd_write_uart_register(edge_port, LCR,
							edge_port->shadowLCR);
	if (status != 0)
		return;

	/* set up the MCR register and send it to the EdgePort */
	edge_port->shadowMCR = MCR_MASTER_IE;
	if (cflag & CBAUD)
		edge_port->shadowMCR |= (MCR_DTR | MCR_RTS);

	status = send_cmd_write_uart_register(edge_port, MCR,
						edge_port->shadowMCR);
	if (status != 0)
		return;

	/* Determine divisor based on baud rate */
	baud = tty_get_baud_rate(tty);
	if (!baud) {
		/* pick a default, any default... */
		baud = 9600;
	}

	dbg("%s - baud rate = %d", __func__, baud);
	status = send_cmd_write_baud_rate(edge_port, baud);
	if (status == -1) {
		/* Speed change was not possible - put back the old speed */
		baud = tty_termios_baud_rate(old_termios);
		tty_encode_baud_rate(tty, baud, baud);
	}
}


/****************************************************************************
 * unicode_to_ascii
 *	Turns a string from Unicode into ASCII.
 *	Doesn't do a good job with any characters that are outside the normal
 *	ASCII range, but it's only for debugging...
 *	NOTE: expects the unicode in LE format
 ****************************************************************************/
static void unicode_to_ascii(char *string, int buflen,
					__le16 *unicode, int unicode_size)
{
	int i;

	if (buflen <= 0)	/* never happens, but... */
		return;
	--buflen;		/* space for nul */

	for (i = 0; i < unicode_size; i++) {
		if (i >= buflen)
			break;
		string[i] = (char)(le16_to_cpu(unicode[i]));
	}
	string[i] = 0x00;
}


/****************************************************************************
 * get_manufacturing_desc
 *	reads in the manufacturing descriptor and stores it into the serial
 *	structure.
 ****************************************************************************/
static void get_manufacturing_desc(struct edgeport_serial *edge_serial)
{
	int response;

	dbg("getting manufacturer descriptor");

	response = rom_read(edge_serial->serial,
				(EDGE_MANUF_DESC_ADDR & 0xffff0000) >> 16,
				(__u16)(EDGE_MANUF_DESC_ADDR & 0x0000ffff),
				EDGE_MANUF_DESC_LEN,
				(__u8 *)(&edge_serial->manuf_descriptor));

	if (response < 1)
		dev_err(&edge_serial->serial->dev->dev,
			"error in getting manufacturer descriptor\n");
	else {
		char string[30];
		dbg("**Manufacturer Descriptor");
		dbg("  RomSize:        %dK",
			edge_serial->manuf_descriptor.RomSize);
		dbg("  RamSize:        %dK",
			edge_serial->manuf_descriptor.RamSize);
		dbg("  CpuRev:         %d",
			edge_serial->manuf_descriptor.CpuRev);
		dbg("  BoardRev:       %d",
			edge_serial->manuf_descriptor.BoardRev);
		dbg("  NumPorts:       %d",
			edge_serial->manuf_descriptor.NumPorts);
		dbg("  DescDate:       %d/%d/%d",
			edge_serial->manuf_descriptor.DescDate[0],
			edge_serial->manuf_descriptor.DescDate[1],
			edge_serial->manuf_descriptor.DescDate[2]+1900);
		unicode_to_ascii(string, sizeof(string),
			edge_serial->manuf_descriptor.SerialNumber,
			edge_serial->manuf_descriptor.SerNumLength/2);
		dbg("  SerialNumber: %s", string);
		unicode_to_ascii(string, sizeof(string),
			edge_serial->manuf_descriptor.AssemblyNumber,
			edge_serial->manuf_descriptor.AssemblyNumLength/2);
		dbg("  AssemblyNumber: %s", string);
		unicode_to_ascii(string, sizeof(string),
		    edge_serial->manuf_descriptor.OemAssyNumber,
		    edge_serial->manuf_descriptor.OemAssyNumLength/2);
		dbg("  OemAssyNumber:  %s", string);
		dbg("  UartType:       %d",
			edge_serial->manuf_descriptor.UartType);
		dbg("  IonPid:         %d",
			edge_serial->manuf_descriptor.IonPid);
		dbg("  IonConfig:      %d",
			edge_serial->manuf_descriptor.IonConfig);
	}
}


/****************************************************************************
 * get_boot_desc
 *	reads in the bootloader descriptor and stores it into the serial
 *	structure.
 ****************************************************************************/
static void get_boot_desc(struct edgeport_serial *edge_serial)
{
	int response;

	dbg("getting boot descriptor");

	response = rom_read(edge_serial->serial,
				(EDGE_BOOT_DESC_ADDR & 0xffff0000) >> 16,
				(__u16)(EDGE_BOOT_DESC_ADDR & 0x0000ffff),
				EDGE_BOOT_DESC_LEN,
				(__u8 *)(&edge_serial->boot_descriptor));

	if (response < 1)
		dev_err(&edge_serial->serial->dev->dev,
				"error in getting boot descriptor\n");
	else {
		dbg("**Boot Descriptor:");
		dbg("  BootCodeLength: %d",
		    le16_to_cpu(edge_serial->boot_descriptor.BootCodeLength));
		dbg("  MajorVersion:   %d",
			edge_serial->boot_descriptor.MajorVersion);
		dbg("  MinorVersion:   %d",
			edge_serial->boot_descriptor.MinorVersion);
		dbg("  BuildNumber:    %d",
			le16_to_cpu(edge_serial->boot_descriptor.BuildNumber));
		dbg("  Capabilities:   0x%x",
		      le16_to_cpu(edge_serial->boot_descriptor.Capabilities));
		dbg("  UConfig0:       %d",
			edge_serial->boot_descriptor.UConfig0);
		dbg("  UConfig1:       %d",
			edge_serial->boot_descriptor.UConfig1);
	}
}


/****************************************************************************
 * load_application_firmware
 *	This is called to load the application firmware to the device
 ****************************************************************************/
static void load_application_firmware(struct edgeport_serial *edge_serial)
{
	const struct ihex_binrec *rec;
	const struct firmware *fw;
	const char *fw_name;
	const char *fw_info;
	int response;
	__u32 Operaddr;
	__u16 build;

	switch (edge_serial->product_info.iDownloadFile) {
		case EDGE_DOWNLOAD_FILE_I930:
			fw_info = "downloading firmware version (930)";
			fw_name	= "edgeport/down.fw";
			break;

		case EDGE_DOWNLOAD_FILE_80251:
			fw_info = "downloading firmware version (80251)";
			fw_name	= "edgeport/down2.fw";
			break;

		case EDGE_DOWNLOAD_FILE_NONE:
			dbg("No download file specified, skipping download");
			return;

		default:
			return;
	}

	response = request_ihex_firmware(&fw, fw_name,
				    &edge_serial->serial->dev->dev);
	if (response) {
		printk(KERN_ERR "Failed to load image \"%s\" err %d\n",
		       fw_name, response);
		return;
	}

	rec = (const struct ihex_binrec *)fw->data;
	build = (rec->data[2] << 8) | rec->data[3];

	dbg("%s %d.%d.%d", fw_info, rec->data[0], rec->data[1], build);

	edge_serial->product_info.FirmwareMajorVersion = rec->data[0];
	edge_serial->product_info.FirmwareMinorVersion = rec->data[1];
	edge_serial->product_info.FirmwareBuildNumber = cpu_to_le16(build);

	for (rec = ihex_next_binrec(rec); rec;
	     rec = ihex_next_binrec(rec)) {
		Operaddr = be32_to_cpu(rec->addr);
		response = sram_write(edge_serial->serial,
				     Operaddr >> 16,
				     Operaddr & 0xFFFF,
				     be16_to_cpu(rec->len),
				     &rec->data[0]);
		if (response < 0) {
			dev_err(&edge_serial->serial->dev->dev,
				"sram_write failed (%x, %x, %d)\n",
				Operaddr >> 16, Operaddr & 0xFFFF,
				be16_to_cpu(rec->len));
			break;
		}
	}

	dbg("sending exec_dl_code");
	response = usb_control_msg (edge_serial->serial->dev, 
				    usb_sndctrlpipe(edge_serial->serial->dev, 0), 
				    USB_REQUEST_ION_EXEC_DL_CODE, 
				    0x40, 0x4000, 0x0001, NULL, 0, 3000);

	release_firmware(fw);
}


/****************************************************************************
 * edge_startup
 ****************************************************************************/
static int edge_startup(struct usb_serial *serial)
{
	struct edgeport_serial *edge_serial;
	struct edgeport_port *edge_port;
	struct usb_device *dev;
	int i, j;
	int response;
	bool interrupt_in_found;
	bool bulk_in_found;
	bool bulk_out_found;
	static __u32 descriptor[3] = {	EDGE_COMPATIBILITY_MASK0,
					EDGE_COMPATIBILITY_MASK1,
					EDGE_COMPATIBILITY_MASK2 };

	dev = serial->dev;

	/* create our private serial structure */
	edge_serial = kzalloc(sizeof(struct edgeport_serial), GFP_KERNEL);
	if (edge_serial == NULL) {
		dev_err(&serial->dev->dev, "%s - Out of memory\n", __func__);
		return -ENOMEM;
	}
	spin_lock_init(&edge_serial->es_lock);
	edge_serial->serial = serial;
	usb_set_serial_data(serial, edge_serial);

	/* get the name for the device from the device */
	i = usb_string(dev, dev->descriptor.iManufacturer,
	    &edge_serial->name[0], MAX_NAME_LEN+1);
	if (i < 0)
		i = 0;
	edge_serial->name[i++] = ' ';
	usb_string(dev, dev->descriptor.iProduct,
	    &edge_serial->name[i], MAX_NAME_LEN+2 - i);

	dev_info(&serial->dev->dev, "%s detected\n", edge_serial->name);

	/* Read the epic descriptor */
	if (get_epic_descriptor(edge_serial) <= 0) {
		/* memcpy descriptor to Supports structures */
		memcpy(&edge_serial->epic_descriptor.Supports, descriptor,
		       sizeof(struct edge_compatibility_bits));

		/* get the manufacturing descriptor for this device */
		get_manufacturing_desc(edge_serial);

		/* get the boot descriptor */
		get_boot_desc(edge_serial);

		get_product_info(edge_serial);
	}

	/* set the number of ports from the manufacturing description */
	/* serial->num_ports = serial->product_info.NumPorts; */
	if ((!edge_serial->is_epic) &&
	    (edge_serial->product_info.NumPorts != serial->num_ports)) {
		dev_warn(&serial->dev->dev, "Device Reported %d serial ports "
			 "vs. core thinking we have %d ports, email "
			 "greg@kroah.com this information.\n",
			 edge_serial->product_info.NumPorts,
			 serial->num_ports);
	}

	dbg("%s - time 1 %ld", __func__, jiffies);

	/* If not an EPiC device */
	if (!edge_serial->is_epic) {
		/* now load the application firmware into this device */
		load_application_firmware(edge_serial);

		dbg("%s - time 2 %ld", __func__, jiffies);

		/* Check current Edgeport EEPROM and update if necessary */
		update_edgeport_E2PROM(edge_serial);

		dbg("%s - time 3 %ld", __func__, jiffies);

		/* set the configuration to use #1 */
/*		dbg("set_configuration 1"); */
/*		usb_set_configuration (dev, 1); */
	}
	dbg("  FirmwareMajorVersion  %d.%d.%d",
	    edge_serial->product_info.FirmwareMajorVersion,
	    edge_serial->product_info.FirmwareMinorVersion,
	    le16_to_cpu(edge_serial->product_info.FirmwareBuildNumber));

	/* we set up the pointers to the endpoints in the edge_open function,
	 * as the structures aren't created yet. */

	/* set up our port private structures */
	for (i = 0; i < serial->num_ports; ++i) {
		edge_port = kzalloc(sizeof(struct edgeport_port), GFP_KERNEL);
		if (edge_port == NULL) {
			dev_err(&serial->dev->dev, "%s - Out of memory\n",
								   __func__);
			for (j = 0; j < i; ++j) {
				kfree(usb_get_serial_port_data(serial->port[j]));
				usb_set_serial_port_data(serial->port[j],
									NULL);
			}
			usb_set_serial_data(serial, NULL);
			kfree(edge_serial);
			return -ENOMEM;
		}
		spin_lock_init(&edge_port->ep_lock);
		edge_port->port = serial->port[i];
		usb_set_serial_port_data(serial->port[i], edge_port);
	}

	response = 0;

	if (edge_serial->is_epic) {
		/* EPIC thing, set up our interrupt polling now and our read
		 * urb, so that the device knows it really is connected. */
		interrupt_in_found = bulk_in_found = bulk_out_found = false;
		for (i = 0; i < serial->interface->altsetting[0]
						.desc.bNumEndpoints; ++i) {
			struct usb_endpoint_descriptor *endpoint;
			int buffer_size;

			endpoint = &serial->interface->altsetting[0].
							endpoint[i].desc;
			buffer_size = usb_endpoint_maxp(endpoint);
			if (!interrupt_in_found &&
			    (usb_endpoint_is_int_in(endpoint))) {
				/* we found a interrupt in endpoint */
				dbg("found interrupt in");

				/* not set up yet, so do it now */
				edge_serial->interrupt_read_urb =
						usb_alloc_urb(0, GFP_KERNEL);
				if (!edge_serial->interrupt_read_urb) {
					dev_err(&dev->dev, "out of memory\n");
					return -ENOMEM;
				}
				edge_serial->interrupt_in_buffer =
					kmalloc(buffer_size, GFP_KERNEL);
				if (!edge_serial->interrupt_in_buffer) {
					dev_err(&dev->dev, "out of memory\n");
					usb_free_urb(edge_serial->interrupt_read_urb);
					return -ENOMEM;
				}
				edge_serial->interrupt_in_endpoint =
						endpoint->bEndpointAddress;

				/* set up our interrupt urb */
				usb_fill_int_urb(
					edge_serial->interrupt_read_urb,
					dev,
					usb_rcvintpipe(dev,
						endpoint->bEndpointAddress),
					edge_serial->interrupt_in_buffer,
					buffer_size,
					edge_interrupt_callback,
					edge_serial,
					endpoint->bInterval);

				interrupt_in_found = true;
			}

			if (!bulk_in_found &&
				(usb_endpoint_is_bulk_in(endpoint))) {
				/* we found a bulk in endpoint */
				dbg("found bulk in");

				/* not set up yet, so do it now */
				edge_serial->read_urb =
						usb_alloc_urb(0, GFP_KERNEL);
				if (!edge_serial->read_urb) {
					dev_err(&dev->dev, "out of memory\n");
					return -ENOMEM;
				}
				edge_serial->bulk_in_buffer =
					kmalloc(buffer_size, GFP_KERNEL);
				if (!edge_serial->bulk_in_buffer) {
					dev_err(&dev->dev, "out of memory\n");
					usb_free_urb(edge_serial->read_urb);
					return -ENOMEM;
				}
				edge_serial->bulk_in_endpoint =
						endpoint->bEndpointAddress;

				/* set up our bulk in urb */
				usb_fill_bulk_urb(edge_serial->read_urb, dev,
					usb_rcvbulkpipe(dev,
						endpoint->bEndpointAddress),
					edge_serial->bulk_in_buffer,
					usb_endpoint_maxp(endpoint),
					edge_bulk_in_callback,
					edge_serial);
				bulk_in_found = true;
			}

			if (!bulk_out_found &&
			    (usb_endpoint_is_bulk_out(endpoint))) {
				/* we found a bulk out endpoint */
				dbg("found bulk out");
				edge_serial->bulk_out_endpoint =
						endpoint->bEndpointAddress;
				bulk_out_found = true;
			}
		}

		if (!interrupt_in_found || !bulk_in_found || !bulk_out_found) {
			dev_err(&dev->dev, "Error - the proper endpoints "
				"were not found!\n");
			return -ENODEV;
		}

		/* start interrupt read for this edgeport this interrupt will
		 * continue as long as the edgeport is connected */
		response = usb_submit_urb(edge_serial->interrupt_read_urb,
								GFP_KERNEL);
		if (response)
			dev_err(&dev->dev,
				"%s - Error %d submitting control urb\n",
				__func__, response);
	}
	return response;
}


/****************************************************************************
 * edge_disconnect
 *	This function is called whenever the device is removed from the usb bus.
 ****************************************************************************/
static void edge_disconnect(struct usb_serial *serial)
{
	struct edgeport_serial *edge_serial = usb_get_serial_data(serial);

	dbg("%s", __func__);

	/* stop reads and writes on all ports */
	/* free up our endpoint stuff */
	if (edge_serial->is_epic) {
		usb_kill_urb(edge_serial->interrupt_read_urb);
		usb_free_urb(edge_serial->interrupt_read_urb);
		kfree(edge_serial->interrupt_in_buffer);

		usb_kill_urb(edge_serial->read_urb);
		usb_free_urb(edge_serial->read_urb);
		kfree(edge_serial->bulk_in_buffer);
	}
}


/****************************************************************************
 * edge_release
 *	This function is called when the device structure is deallocated.
 ****************************************************************************/
static void edge_release(struct usb_serial *serial)
{
	struct edgeport_serial *edge_serial = usb_get_serial_data(serial);
	int i;

	dbg("%s", __func__);

	for (i = 0; i < serial->num_ports; ++i)
		kfree(usb_get_serial_port_data(serial->port[i]));

	kfree(edge_serial);
}

module_usb_serial_driver(serial_drivers, id_table_combined);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
MODULE_FIRMWARE("edgeport/boot.fw");
MODULE_FIRMWARE("edgeport/boot2.fw");
MODULE_FIRMWARE("edgeport/down.fw");
MODULE_FIRMWARE("edgeport/down2.fw");

module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug enabled or not");
