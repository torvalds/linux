/*
 * Edgeport USB Serial Converter driver
 *
 * Copyright (C) 2000-2002 Inside Out Networks, All rights reserved.
 * Copyright (C) 2001-2002 Greg Kroah-Hartman <greg@kroah.com>
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 * Supports the following devices:
 *	EP/1 EP/2 EP/4 EP/21 EP/22 EP/221 EP/42 EP/421 WATCHPORT
 *
 * For questions or problems with this driver, contact Inside Out
 * Networks technical support, or Peter Berger <pberger@brimson.com>,
 * or Al Borchers <alborchers@steinerpoint.com>.
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
#include <linux/mutex.h>
#include <linux/serial.h>
#include <linux/kfifo.h>
#include <linux/ioctl.h>
#include <linux/firmware.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>

#include "io_16654.h"
#include "io_usbvend.h"
#include "io_ti.h"

/*
 * Version Information
 */
#define DRIVER_VERSION "v0.7mode043006"
#define DRIVER_AUTHOR "Greg Kroah-Hartman <greg@kroah.com> and David Iacovelli"
#define DRIVER_DESC "Edgeport USB Serial Driver"

#define EPROM_PAGE_SIZE		64


/* different hardware types */
#define HARDWARE_TYPE_930	0
#define HARDWARE_TYPE_TIUMP	1

/* IOCTL_PRIVATE_TI_GET_MODE Definitions */
#define	TI_MODE_CONFIGURING	0   /* Device has not entered start device */
#define	TI_MODE_BOOT		1   /* Staying in boot mode		   */
#define TI_MODE_DOWNLOAD	2   /* Made it to download mode		   */
#define TI_MODE_TRANSITIONING	3   /* Currently in boot mode but
				       transitioning to download mode	   */

/* read urb state */
#define EDGE_READ_URB_RUNNING	0
#define EDGE_READ_URB_STOPPING	1
#define EDGE_READ_URB_STOPPED	2

#define EDGE_CLOSING_WAIT	4000	/* in .01 sec */

#define EDGE_OUT_BUF_SIZE	1024


/* Product information read from the Edgeport */
struct product_info {
	int	TiMode;			/* Current TI Mode  */
	__u8	hardware_type;		/* Type of hardware */
} __attribute__((packed));

struct edgeport_port {
	__u16 uart_base;
	__u16 dma_address;
	__u8 shadow_msr;
	__u8 shadow_mcr;
	__u8 shadow_lsr;
	__u8 lsr_mask;
	__u32 ump_read_timeout;		/* Number of milliseconds the UMP will
					   wait without data before completing
					   a read short */
	int baud_rate;
	int close_pending;
	int lsr_event;
	struct async_icount	icount;
	wait_queue_head_t	delta_msr_wait;	/* for handling sleeping while
						   waiting for msr change to
						   happen */
	struct edgeport_serial	*edge_serial;
	struct usb_serial_port	*port;
	__u8 bUartMode;		/* Port type, 0: RS232, etc. */
	spinlock_t ep_lock;
	int ep_read_urb_state;
	int ep_write_urb_in_use;
	struct kfifo write_fifo;
};

struct edgeport_serial {
	struct product_info product_info;
	u8 TI_I2C_Type;			/* Type of I2C in UMP */
	u8 TiReadI2C;			/* Set to TRUE if we have read the
					   I2c in Boot Mode */
	struct mutex es_lock;
	int num_ports_open;
	struct usb_serial *serial;
};


/* Devices that this driver supports */
static const struct usb_device_id edgeport_1port_id_table[] = {
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_TI_EDGEPORT_1) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_TI_TI3410_EDGEPORT_1) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_TI_TI3410_EDGEPORT_1I) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_WP_PROXIMITY) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_WP_MOTION) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_WP_MOISTURE) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_WP_TEMPERATURE) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_WP_HUMIDITY) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_WP_POWER) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_WP_LIGHT) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_WP_RADIATION) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_WP_DISTANCE) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_WP_ACCELERATION) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_WP_PROX_DIST) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_PLUS_PWR_HP4CD) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_PLUS_PWR_PCI) },
	{ }
};

static const struct usb_device_id edgeport_2port_id_table[] = {
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_TI_EDGEPORT_2) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_TI_EDGEPORT_2C) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_TI_EDGEPORT_2I) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_TI_EDGEPORT_421) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_TI_EDGEPORT_21) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_TI_EDGEPORT_42) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_TI_EDGEPORT_4) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_TI_EDGEPORT_4I) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_TI_EDGEPORT_22I) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_TI_EDGEPORT_221C) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_TI_EDGEPORT_22C) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_TI_EDGEPORT_21C) },
	/* The 4, 8 and 16 port devices show up as multiple 2 port devices */
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_TI_EDGEPORT_4S) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_TI_EDGEPORT_8) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_TI_EDGEPORT_8S) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_TI_EDGEPORT_416) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_TI_EDGEPORT_416B) },
	{ }
};

/* Devices that this driver supports */
static const struct usb_device_id id_table_combined[] = {
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_TI_EDGEPORT_1) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_TI_TI3410_EDGEPORT_1) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_TI_TI3410_EDGEPORT_1I) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_WP_PROXIMITY) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_WP_MOTION) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_WP_MOISTURE) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_WP_TEMPERATURE) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_WP_HUMIDITY) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_WP_POWER) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_WP_LIGHT) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_WP_RADIATION) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_WP_DISTANCE) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_WP_ACCELERATION) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_WP_PROX_DIST) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_PLUS_PWR_HP4CD) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_PLUS_PWR_PCI) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_TI_EDGEPORT_2) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_TI_EDGEPORT_2C) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_TI_EDGEPORT_2I) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_TI_EDGEPORT_421) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_TI_EDGEPORT_21) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_TI_EDGEPORT_42) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_TI_EDGEPORT_4) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_TI_EDGEPORT_4I) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_TI_EDGEPORT_22I) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_TI_EDGEPORT_221C) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_TI_EDGEPORT_22C) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_TI_EDGEPORT_21C) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_TI_EDGEPORT_4S) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_TI_EDGEPORT_8) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_TI_EDGEPORT_8S) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_TI_EDGEPORT_416) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_TI_EDGEPORT_416B) },
	{ }
};

MODULE_DEVICE_TABLE(usb, id_table_combined);

static struct usb_driver io_driver = {
	.name =		"io_ti",
	.probe =	usb_serial_probe,
	.disconnect =	usb_serial_disconnect,
	.id_table =	id_table_combined,
};


static unsigned char OperationalMajorVersion;
static unsigned char OperationalMinorVersion;
static unsigned short OperationalBuildNumber;

static bool debug;

static int closing_wait = EDGE_CLOSING_WAIT;
static bool ignore_cpu_rev;
static int default_uart_mode;		/* RS232 */

static void edge_tty_recv(struct device *dev, struct tty_struct *tty,
			  unsigned char *data, int length);

static void stop_read(struct edgeport_port *edge_port);
static int restart_read(struct edgeport_port *edge_port);

static void edge_set_termios(struct tty_struct *tty,
		struct usb_serial_port *port, struct ktermios *old_termios);
static void edge_send(struct tty_struct *tty);

/* sysfs attributes */
static int edge_create_sysfs_attrs(struct usb_serial_port *port);
static int edge_remove_sysfs_attrs(struct usb_serial_port *port);


static int ti_vread_sync(struct usb_device *dev, __u8 request,
				__u16 value, __u16 index, u8 *data, int size)
{
	int status;

	status = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0), request,
			(USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_IN),
			value, index, data, size, 1000);
	if (status < 0)
		return status;
	if (status != size) {
		dbg("%s - wanted to write %d, but only wrote %d",
					     __func__, size, status);
		return -ECOMM;
	}
	return 0;
}

static int ti_vsend_sync(struct usb_device *dev, __u8 request,
				__u16 value, __u16 index, u8 *data, int size)
{
	int status;

	status = usb_control_msg(dev, usb_sndctrlpipe(dev, 0), request,
			(USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_OUT),
			value, index, data, size, 1000);
	if (status < 0)
		return status;
	if (status != size) {
		dbg("%s - wanted to write %d, but only wrote %d",
		     __func__, size, status);
		return -ECOMM;
	}
	return 0;
}

static int send_cmd(struct usb_device *dev, __u8 command,
				__u8 moduleid, __u16 value, u8 *data,
				int size)
{
	return ti_vsend_sync(dev, command, value, moduleid, data, size);
}

/* clear tx/rx buffers and fifo in TI UMP */
static int purge_port(struct usb_serial_port *port, __u16 mask)
{
	int port_number = port->number - port->serial->minor;

	dbg("%s - port %d, mask %x", __func__, port_number, mask);

	return send_cmd(port->serial->dev,
					UMPC_PURGE_PORT,
					(__u8)(UMPM_UART1_PORT + port_number),
					mask,
					NULL,
					0);
}

/**
 * read_download_mem - Read edgeport memory from TI chip
 * @dev: usb device pointer
 * @start_address: Device CPU address at which to read
 * @length: Length of above data
 * @address_type: Can read both XDATA and I2C
 * @buffer: pointer to input data buffer
 */
static int read_download_mem(struct usb_device *dev, int start_address,
				int length, __u8 address_type, __u8 *buffer)
{
	int status = 0;
	__u8 read_length;
	__be16 be_start_address;

	dbg("%s - @ %x for %d", __func__, start_address, length);

	/* Read in blocks of 64 bytes
	 * (TI firmware can't handle more than 64 byte reads)
	 */
	while (length) {
		if (length > 64)
			read_length = 64;
		else
			read_length = (__u8)length;

		if (read_length > 1) {
			dbg("%s - @ %x for %d", __func__,
			     start_address, read_length);
		}
		be_start_address = cpu_to_be16(start_address);
		status = ti_vread_sync(dev, UMPC_MEMORY_READ,
					(__u16)address_type,
					(__force __u16)be_start_address,
					buffer, read_length);

		if (status) {
			dbg("%s - ERROR %x", __func__, status);
			return status;
		}

		if (read_length > 1)
			usb_serial_debug_data(debug, &dev->dev, __func__,
					      read_length, buffer);

		/* Update pointers/length */
		start_address += read_length;
		buffer += read_length;
		length -= read_length;
	}

	return status;
}

static int read_ram(struct usb_device *dev, int start_address,
						int length, __u8 *buffer)
{
	return read_download_mem(dev, start_address, length,
					DTK_ADDR_SPACE_XDATA, buffer);
}

/* Read edgeport memory to a given block */
static int read_boot_mem(struct edgeport_serial *serial,
				int start_address, int length, __u8 *buffer)
{
	int status = 0;
	int i;

	for (i = 0; i < length; i++) {
		status = ti_vread_sync(serial->serial->dev,
				UMPC_MEMORY_READ, serial->TI_I2C_Type,
				(__u16)(start_address+i), &buffer[i], 0x01);
		if (status) {
			dbg("%s - ERROR %x", __func__, status);
			return status;
		}
	}

	dbg("%s - start_address = %x, length = %d",
					__func__, start_address, length);
	usb_serial_debug_data(debug, &serial->serial->dev->dev,
					__func__, length, buffer);

	serial->TiReadI2C = 1;

	return status;
}

/* Write given block to TI EPROM memory */
static int write_boot_mem(struct edgeport_serial *serial,
				int start_address, int length, __u8 *buffer)
{
	int status = 0;
	int i;
	u8 *temp;

	/* Must do a read before write */
	if (!serial->TiReadI2C) {
		temp = kmalloc(1, GFP_KERNEL);
		if (!temp) {
			dev_err(&serial->serial->dev->dev,
					"%s - out of memory\n", __func__);
			return -ENOMEM;
		}
		status = read_boot_mem(serial, 0, 1, temp);
		kfree(temp);
		if (status)
			return status;
	}

	for (i = 0; i < length; ++i) {
		status = ti_vsend_sync(serial->serial->dev,
				UMPC_MEMORY_WRITE, buffer[i],
				(__u16)(i + start_address), NULL, 0);
		if (status)
			return status;
	}

	dbg("%s - start_sddr = %x, length = %d",
					__func__, start_address, length);
	usb_serial_debug_data(debug, &serial->serial->dev->dev,
					__func__, length, buffer);

	return status;
}


/* Write edgeport I2C memory to TI chip	*/
static int write_i2c_mem(struct edgeport_serial *serial,
		int start_address, int length, __u8 address_type, __u8 *buffer)
{
	int status = 0;
	int write_length;
	__be16 be_start_address;

	/* We can only send a maximum of 1 aligned byte page at a time */

	/* calculate the number of bytes left in the first page */
	write_length = EPROM_PAGE_SIZE -
				(start_address & (EPROM_PAGE_SIZE - 1));

	if (write_length > length)
		write_length = length;

	dbg("%s - BytesInFirstPage Addr = %x, length = %d",
					__func__, start_address, write_length);
	usb_serial_debug_data(debug, &serial->serial->dev->dev,
						__func__, write_length, buffer);

	/* Write first page */
	be_start_address = cpu_to_be16(start_address);
	status = ti_vsend_sync(serial->serial->dev,
				UMPC_MEMORY_WRITE, (__u16)address_type,
				(__force __u16)be_start_address,
				buffer,	write_length);
	if (status) {
		dbg("%s - ERROR %d", __func__, status);
		return status;
	}

	length		-= write_length;
	start_address	+= write_length;
	buffer		+= write_length;

	/* We should be aligned now -- can write
	   max page size bytes at a time */
	while (length) {
		if (length > EPROM_PAGE_SIZE)
			write_length = EPROM_PAGE_SIZE;
		else
			write_length = length;

		dbg("%s - Page Write Addr = %x, length = %d",
					__func__, start_address, write_length);
		usb_serial_debug_data(debug, &serial->serial->dev->dev,
					__func__, write_length, buffer);

		/* Write next page */
		be_start_address = cpu_to_be16(start_address);
		status = ti_vsend_sync(serial->serial->dev, UMPC_MEMORY_WRITE,
				(__u16)address_type,
				(__force __u16)be_start_address,
				buffer, write_length);
		if (status) {
			dev_err(&serial->serial->dev->dev, "%s - ERROR %d\n",
					__func__, status);
			return status;
		}

		length		-= write_length;
		start_address	+= write_length;
		buffer		+= write_length;
	}
	return status;
}

/* Examine the UMP DMA registers and LSR
 *
 * Check the MSBit of the X and Y DMA byte count registers.
 * A zero in this bit indicates that the TX DMA buffers are empty
 * then check the TX Empty bit in the UART.
 */
static int tx_active(struct edgeport_port *port)
{
	int status;
	struct out_endpoint_desc_block *oedb;
	__u8 *lsr;
	int bytes_left = 0;

	oedb = kmalloc(sizeof(*oedb), GFP_KERNEL);
	if (!oedb) {
		dev_err(&port->port->dev, "%s - out of memory\n", __func__);
		return -ENOMEM;
	}

	lsr = kmalloc(1, GFP_KERNEL);	/* Sigh, that's right, just one byte,
					   as not all platforms can do DMA
					   from stack */
	if (!lsr) {
		kfree(oedb);
		return -ENOMEM;
	}
	/* Read the DMA Count Registers */
	status = read_ram(port->port->serial->dev, port->dma_address,
						sizeof(*oedb), (void *)oedb);
	if (status)
		goto exit_is_tx_active;

	dbg("%s - XByteCount    0x%X", __func__, oedb->XByteCount);

	/* and the LSR */
	status = read_ram(port->port->serial->dev,
			port->uart_base + UMPMEM_OFFS_UART_LSR, 1, lsr);

	if (status)
		goto exit_is_tx_active;
	dbg("%s - LSR = 0x%X", __func__, *lsr);

	/* If either buffer has data or we are transmitting then return TRUE */
	if ((oedb->XByteCount & 0x80) != 0)
		bytes_left += 64;

	if ((*lsr & UMP_UART_LSR_TX_MASK) == 0)
		bytes_left += 1;

	/* We return Not Active if we get any kind of error */
exit_is_tx_active:
	dbg("%s - return %d", __func__, bytes_left);

	kfree(lsr);
	kfree(oedb);
	return bytes_left;
}

static void chase_port(struct edgeport_port *port, unsigned long timeout,
								int flush)
{
	int baud_rate;
	struct tty_struct *tty = tty_port_tty_get(&port->port->port);
	wait_queue_t wait;
	unsigned long flags;

	if (!timeout)
		timeout = (HZ * EDGE_CLOSING_WAIT)/100;

	/* wait for data to drain from the buffer */
	spin_lock_irqsave(&port->ep_lock, flags);
	init_waitqueue_entry(&wait, current);
	add_wait_queue(&tty->write_wait, &wait);
	for (;;) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (kfifo_len(&port->write_fifo) == 0
		|| timeout == 0 || signal_pending(current)
		|| !usb_get_intfdata(port->port->serial->interface))
			/* disconnect */
			break;
		spin_unlock_irqrestore(&port->ep_lock, flags);
		timeout = schedule_timeout(timeout);
		spin_lock_irqsave(&port->ep_lock, flags);
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&tty->write_wait, &wait);
	if (flush)
		kfifo_reset_out(&port->write_fifo);
	spin_unlock_irqrestore(&port->ep_lock, flags);
	tty_kref_put(tty);

	/* wait for data to drain from the device */
	timeout += jiffies;
	while ((long)(jiffies - timeout) < 0 && !signal_pending(current)
	&& usb_get_intfdata(port->port->serial->interface)) {
		/* not disconnected */
		if (!tx_active(port))
			break;
		msleep(10);
	}

	/* disconnected */
	if (!usb_get_intfdata(port->port->serial->interface))
		return;

	/* wait one more character time, based on baud rate */
	/* (tx_active doesn't seem to wait for the last byte) */
	baud_rate = port->baud_rate;
	if (baud_rate == 0)
		baud_rate = 50;
	msleep(max(1, DIV_ROUND_UP(10000, baud_rate)));
}

static int choose_config(struct usb_device *dev)
{
	/*
	 * There may be multiple configurations on this device, in which case
	 * we would need to read and parse all of them to find out which one
	 * we want. However, we just support one config at this point,
	 * configuration # 1, which is Config Descriptor 0.
	 */

	dbg("%s - Number of Interfaces = %d",
				__func__, dev->config->desc.bNumInterfaces);
	dbg("%s - MAX Power            = %d",
				__func__, dev->config->desc.bMaxPower * 2);

	if (dev->config->desc.bNumInterfaces != 1) {
		dev_err(&dev->dev, "%s - bNumInterfaces is not 1, ERROR!\n",
								__func__);
		return -ENODEV;
	}

	return 0;
}

static int read_rom(struct edgeport_serial *serial,
				int start_address, int length, __u8 *buffer)
{
	int status;

	if (serial->product_info.TiMode == TI_MODE_DOWNLOAD) {
		status = read_download_mem(serial->serial->dev,
					       start_address,
					       length,
					       serial->TI_I2C_Type,
					       buffer);
	} else {
		status = read_boot_mem(serial, start_address, length,
								buffer);
	}
	return status;
}

static int write_rom(struct edgeport_serial *serial, int start_address,
						int length, __u8 *buffer)
{
	if (serial->product_info.TiMode == TI_MODE_BOOT)
		return write_boot_mem(serial, start_address, length,
								buffer);

	if (serial->product_info.TiMode == TI_MODE_DOWNLOAD)
		return write_i2c_mem(serial, start_address, length,
						serial->TI_I2C_Type, buffer);
	return -EINVAL;
}



/* Read a descriptor header from I2C based on type */
static int get_descriptor_addr(struct edgeport_serial *serial,
				int desc_type, struct ti_i2c_desc *rom_desc)
{
	int start_address;
	int status;

	/* Search for requested descriptor in I2C */
	start_address = 2;
	do {
		status = read_rom(serial,
				   start_address,
				   sizeof(struct ti_i2c_desc),
				   (__u8 *)rom_desc);
		if (status)
			return 0;

		if (rom_desc->Type == desc_type)
			return start_address;

		start_address = start_address + sizeof(struct ti_i2c_desc)
							+ rom_desc->Size;

	} while ((start_address < TI_MAX_I2C_SIZE) && rom_desc->Type);

	return 0;
}

/* Validate descriptor checksum */
static int valid_csum(struct ti_i2c_desc *rom_desc, __u8 *buffer)
{
	__u16 i;
	__u8 cs = 0;

	for (i = 0; i < rom_desc->Size; i++)
		cs = (__u8)(cs + buffer[i]);

	if (cs != rom_desc->CheckSum) {
		dbg("%s - Mismatch %x - %x", __func__, rom_desc->CheckSum, cs);
		return -EINVAL;
	}
	return 0;
}

/* Make sure that the I2C image is good */
static int check_i2c_image(struct edgeport_serial *serial)
{
	struct device *dev = &serial->serial->dev->dev;
	int status = 0;
	struct ti_i2c_desc *rom_desc;
	int start_address = 2;
	__u8 *buffer;
	__u16 ttype;

	rom_desc = kmalloc(sizeof(*rom_desc), GFP_KERNEL);
	if (!rom_desc) {
		dev_err(dev, "%s - out of memory\n", __func__);
		return -ENOMEM;
	}
	buffer = kmalloc(TI_MAX_I2C_SIZE, GFP_KERNEL);
	if (!buffer) {
		dev_err(dev, "%s - out of memory when allocating buffer\n",
								__func__);
		kfree(rom_desc);
		return -ENOMEM;
	}

	/* Read the first byte (Signature0) must be 0x52 or 0x10 */
	status = read_rom(serial, 0, 1, buffer);
	if (status)
		goto out;

	if (*buffer != UMP5152 && *buffer != UMP3410) {
		dev_err(dev, "%s - invalid buffer signature\n", __func__);
		status = -ENODEV;
		goto out;
	}

	do {
		/* Validate the I2C */
		status = read_rom(serial,
				start_address,
				sizeof(struct ti_i2c_desc),
				(__u8 *)rom_desc);
		if (status)
			break;

		if ((start_address + sizeof(struct ti_i2c_desc) +
					rom_desc->Size) > TI_MAX_I2C_SIZE) {
			status = -ENODEV;
			dbg("%s - structure too big, erroring out.", __func__);
			break;
		}

		dbg("%s Type = 0x%x", __func__, rom_desc->Type);

		/* Skip type 2 record */
		ttype = rom_desc->Type & 0x0f;
		if (ttype != I2C_DESC_TYPE_FIRMWARE_BASIC
			&& ttype != I2C_DESC_TYPE_FIRMWARE_AUTO) {
			/* Read the descriptor data */
			status = read_rom(serial, start_address +
						sizeof(struct ti_i2c_desc),
						rom_desc->Size, buffer);
			if (status)
				break;

			status = valid_csum(rom_desc, buffer);
			if (status)
				break;
		}
		start_address = start_address + sizeof(struct ti_i2c_desc) +
								rom_desc->Size;

	} while ((rom_desc->Type != I2C_DESC_TYPE_ION) &&
				(start_address < TI_MAX_I2C_SIZE));

	if ((rom_desc->Type != I2C_DESC_TYPE_ION) ||
				(start_address > TI_MAX_I2C_SIZE))
		status = -ENODEV;

out:
	kfree(buffer);
	kfree(rom_desc);
	return status;
}

static int get_manuf_info(struct edgeport_serial *serial, __u8 *buffer)
{
	int status;
	int start_address;
	struct ti_i2c_desc *rom_desc;
	struct edge_ti_manuf_descriptor *desc;

	rom_desc = kmalloc(sizeof(*rom_desc), GFP_KERNEL);
	if (!rom_desc) {
		dev_err(&serial->serial->dev->dev, "%s - out of memory\n",
								__func__);
		return -ENOMEM;
	}
	start_address = get_descriptor_addr(serial, I2C_DESC_TYPE_ION,
								rom_desc);

	if (!start_address) {
		dbg("%s - Edge Descriptor not found in I2C", __func__);
		status = -ENODEV;
		goto exit;
	}

	/* Read the descriptor data */
	status = read_rom(serial, start_address+sizeof(struct ti_i2c_desc),
						rom_desc->Size, buffer);
	if (status)
		goto exit;

	status = valid_csum(rom_desc, buffer);

	desc = (struct edge_ti_manuf_descriptor *)buffer;
	dbg("%s - IonConfig      0x%x", __func__, desc->IonConfig);
	dbg("%s - Version          %d", __func__, desc->Version);
	dbg("%s - Cpu/Board      0x%x", __func__, desc->CpuRev_BoardRev);
	dbg("%s - NumPorts         %d", __func__, desc->NumPorts);
	dbg("%s - NumVirtualPorts  %d", __func__, desc->NumVirtualPorts);
	dbg("%s - TotalPorts       %d", __func__, desc->TotalPorts);

exit:
	kfree(rom_desc);
	return status;
}

/* Build firmware header used for firmware update */
static int build_i2c_fw_hdr(__u8 *header, struct device *dev)
{
	__u8 *buffer;
	int buffer_size;
	int i;
	int err;
	__u8 cs = 0;
	struct ti_i2c_desc *i2c_header;
	struct ti_i2c_image_header *img_header;
	struct ti_i2c_firmware_rec *firmware_rec;
	const struct firmware *fw;
	const char *fw_name = "edgeport/down3.bin";

	/* In order to update the I2C firmware we must change the type 2 record
	 * to type 0xF2.  This will force the UMP to come up in Boot Mode.
	 * Then while in boot mode, the driver will download the latest
	 * firmware (padded to 15.5k) into the UMP ram.  And finally when the
	 * device comes back up in download mode the driver will cause the new
	 * firmware to be copied from the UMP Ram to I2C and the firmware will
	 * update the record type from 0xf2 to 0x02.
	 */

	/* Allocate a 15.5k buffer + 2 bytes for version number
	 * (Firmware Record) */
	buffer_size = (((1024 * 16) - 512 ) +
			sizeof(struct ti_i2c_firmware_rec));

	buffer = kmalloc(buffer_size, GFP_KERNEL);
	if (!buffer) {
		dev_err(dev, "%s - out of memory\n", __func__);
		return -ENOMEM;
	}

	// Set entire image of 0xffs
	memset(buffer, 0xff, buffer_size);

	err = request_firmware(&fw, fw_name, dev);
	if (err) {
		printk(KERN_ERR "Failed to load image \"%s\" err %d\n",
		       fw_name, err);
		kfree(buffer);
		return err;
	}

	/* Save Download Version Number */
	OperationalMajorVersion = fw->data[0];
	OperationalMinorVersion = fw->data[1];
	OperationalBuildNumber = fw->data[2] | (fw->data[3] << 8);

	/* Copy version number into firmware record */
	firmware_rec = (struct ti_i2c_firmware_rec *)buffer;

	firmware_rec->Ver_Major	= OperationalMajorVersion;
	firmware_rec->Ver_Minor	= OperationalMinorVersion;

	/* Pointer to fw_down memory image */
	img_header = (struct ti_i2c_image_header *)&fw->data[4];

	memcpy(buffer + sizeof(struct ti_i2c_firmware_rec),
		&fw->data[4 + sizeof(struct ti_i2c_image_header)],
		le16_to_cpu(img_header->Length));

	release_firmware(fw);

	for (i=0; i < buffer_size; i++) {
		cs = (__u8)(cs + buffer[i]);
	}

	kfree(buffer);

	/* Build new header */
	i2c_header =  (struct ti_i2c_desc *)header;
	firmware_rec =  (struct ti_i2c_firmware_rec*)i2c_header->Data;

	i2c_header->Type	= I2C_DESC_TYPE_FIRMWARE_BLANK;
	i2c_header->Size	= (__u16)buffer_size;
	i2c_header->CheckSum	= cs;
	firmware_rec->Ver_Major	= OperationalMajorVersion;
	firmware_rec->Ver_Minor	= OperationalMinorVersion;

	return 0;
}

/* Try to figure out what type of I2c we have */
static int i2c_type_bootmode(struct edgeport_serial *serial)
{
	int status;
	u8 *data;

	data = kmalloc(1, GFP_KERNEL);
	if (!data) {
		dev_err(&serial->serial->dev->dev,
				"%s - out of memory\n", __func__);
		return -ENOMEM;
	}

	/* Try to read type 2 */
	status = ti_vread_sync(serial->serial->dev, UMPC_MEMORY_READ,
				DTK_ADDR_SPACE_I2C_TYPE_II, 0, data, 0x01);
	if (status)
		dbg("%s - read 2 status error = %d", __func__, status);
	else
		dbg("%s - read 2 data = 0x%x", __func__, *data);
	if ((!status) && (*data == UMP5152 || *data == UMP3410)) {
		dbg("%s - ROM_TYPE_II", __func__);
		serial->TI_I2C_Type = DTK_ADDR_SPACE_I2C_TYPE_II;
		goto out;
	}

	/* Try to read type 3 */
	status = ti_vread_sync(serial->serial->dev, UMPC_MEMORY_READ,
				DTK_ADDR_SPACE_I2C_TYPE_III, 0,	data, 0x01);
	if (status)
		dbg("%s - read 3 status error = %d", __func__, status);
	else
		dbg("%s - read 2 data = 0x%x", __func__, *data);
	if ((!status) && (*data == UMP5152 || *data == UMP3410)) {
		dbg("%s - ROM_TYPE_III", __func__);
		serial->TI_I2C_Type = DTK_ADDR_SPACE_I2C_TYPE_III;
		goto out;
	}

	dbg("%s - Unknown", __func__);
	serial->TI_I2C_Type = DTK_ADDR_SPACE_I2C_TYPE_II;
	status = -ENODEV;
out:
	kfree(data);
	return status;
}

static int bulk_xfer(struct usb_serial *serial, void *buffer,
						int length, int *num_sent)
{
	int status;

	status = usb_bulk_msg(serial->dev,
			usb_sndbulkpipe(serial->dev,
				serial->port[0]->bulk_out_endpointAddress),
			buffer, length, num_sent, 1000);
	return status;
}

/* Download given firmware image to the device (IN BOOT MODE) */
static int download_code(struct edgeport_serial *serial, __u8 *image,
							int image_length)
{
	int status = 0;
	int pos;
	int transfer;
	int done;

	/* Transfer firmware image */
	for (pos = 0; pos < image_length; ) {
		/* Read the next buffer from file */
		transfer = image_length - pos;
		if (transfer > EDGE_FW_BULK_MAX_PACKET_SIZE)
			transfer = EDGE_FW_BULK_MAX_PACKET_SIZE;

		/* Transfer data */
		status = bulk_xfer(serial->serial, &image[pos],
							transfer, &done);
		if (status)
			break;
		/* Advance buffer pointer */
		pos += done;
	}

	return status;
}

/* FIXME!!! */
static int config_boot_dev(struct usb_device *dev)
{
	return 0;
}

static int ti_cpu_rev(struct edge_ti_manuf_descriptor *desc)
{
	return TI_GET_CPU_REVISION(desc->CpuRev_BoardRev);
}

/**
 * DownloadTIFirmware - Download run-time operating firmware to the TI5052
 *
 * This routine downloads the main operating code into the TI5052, using the
 * boot code already burned into E2PROM or ROM.
 */
static int download_fw(struct edgeport_serial *serial)
{
	struct device *dev = &serial->serial->dev->dev;
	int status = 0;
	int start_address;
	struct edge_ti_manuf_descriptor *ti_manuf_desc;
	struct usb_interface_descriptor *interface;
	int download_cur_ver;
	int download_new_ver;

	/* This routine is entered by both the BOOT mode and the Download mode
	 * We can determine which code is running by the reading the config
	 * descriptor and if we have only one bulk pipe it is in boot mode
	 */
	serial->product_info.hardware_type = HARDWARE_TYPE_TIUMP;

	/* Default to type 2 i2c */
	serial->TI_I2C_Type = DTK_ADDR_SPACE_I2C_TYPE_II;

	status = choose_config(serial->serial->dev);
	if (status)
		return status;

	interface = &serial->serial->interface->cur_altsetting->desc;
	if (!interface) {
		dev_err(dev, "%s - no interface set, error!\n", __func__);
		return -ENODEV;
	}

	/*
	 * Setup initial mode -- the default mode 0 is TI_MODE_CONFIGURING
	 * if we have more than one endpoint we are definitely in download
	 * mode
	 */
	if (interface->bNumEndpoints > 1)
		serial->product_info.TiMode = TI_MODE_DOWNLOAD;
	else
		/* Otherwise we will remain in configuring mode */
		serial->product_info.TiMode = TI_MODE_CONFIGURING;

	/********************************************************************/
	/* Download Mode */
	/********************************************************************/
	if (serial->product_info.TiMode == TI_MODE_DOWNLOAD) {
		struct ti_i2c_desc *rom_desc;

		dbg("%s - RUNNING IN DOWNLOAD MODE", __func__);

		status = check_i2c_image(serial);
		if (status) {
			dbg("%s - DOWNLOAD MODE -- BAD I2C", __func__);
			return status;
		}

		/* Validate Hardware version number
		 * Read Manufacturing Descriptor from TI Based Edgeport
		 */
		ti_manuf_desc = kmalloc(sizeof(*ti_manuf_desc), GFP_KERNEL);
		if (!ti_manuf_desc) {
			dev_err(dev, "%s - out of memory.\n", __func__);
			return -ENOMEM;
		}
		status = get_manuf_info(serial, (__u8 *)ti_manuf_desc);
		if (status) {
			kfree(ti_manuf_desc);
			return status;
		}

		/* Check version number of ION descriptor */
		if (!ignore_cpu_rev && ti_cpu_rev(ti_manuf_desc) < 2) {
			dbg("%s - Wrong CPU Rev %d (Must be 2)",
				__func__, ti_cpu_rev(ti_manuf_desc));
			kfree(ti_manuf_desc);
			return -EINVAL;
  		}

		rom_desc = kmalloc(sizeof(*rom_desc), GFP_KERNEL);
		if (!rom_desc) {
			dev_err(dev, "%s - out of memory.\n", __func__);
			kfree(ti_manuf_desc);
			return -ENOMEM;
		}

		/* Search for type 2 record (firmware record) */
		start_address = get_descriptor_addr(serial,
				I2C_DESC_TYPE_FIRMWARE_BASIC, rom_desc);
		if (start_address != 0) {
			struct ti_i2c_firmware_rec *firmware_version;
			u8 *record;

			dbg("%s - Found Type FIRMWARE (Type 2) record",
								__func__);

			firmware_version = kmalloc(sizeof(*firmware_version),
								GFP_KERNEL);
			if (!firmware_version) {
				dev_err(dev, "%s - out of memory.\n", __func__);
				kfree(rom_desc);
				kfree(ti_manuf_desc);
				return -ENOMEM;
			}

			/* Validate version number
			 * Read the descriptor data
			 */
			status = read_rom(serial, start_address +
					sizeof(struct ti_i2c_desc),
					sizeof(struct ti_i2c_firmware_rec),
					(__u8 *)firmware_version);
			if (status) {
				kfree(firmware_version);
				kfree(rom_desc);
				kfree(ti_manuf_desc);
				return status;
			}

			/* Check version number of download with current
			   version in I2c */
			download_cur_ver = (firmware_version->Ver_Major << 8) +
					   (firmware_version->Ver_Minor);
			download_new_ver = (OperationalMajorVersion << 8) +
					   (OperationalMinorVersion);

			dbg("%s - >> FW Versions Device %d.%d  Driver %d.%d",
			    __func__,
			    firmware_version->Ver_Major,
			    firmware_version->Ver_Minor,
			    OperationalMajorVersion,
			    OperationalMinorVersion);

			/* Check if we have an old version in the I2C and
			   update if necessary */
			if (download_cur_ver < download_new_ver) {
				dbg("%s - Update I2C dld from %d.%d to %d.%d",
				    __func__,
				    firmware_version->Ver_Major,
				    firmware_version->Ver_Minor,
				    OperationalMajorVersion,
				    OperationalMinorVersion);

				record = kmalloc(1, GFP_KERNEL);
				if (!record) {
					dev_err(dev, "%s - out of memory.\n",
							__func__);
					kfree(firmware_version);
					kfree(rom_desc);
					kfree(ti_manuf_desc);
					return -ENOMEM;
				}
				/* In order to update the I2C firmware we must
				 * change the type 2 record to type 0xF2. This
				 * will force the UMP to come up in Boot Mode.
				 * Then while in boot mode, the driver will
				 * download the latest firmware (padded to
				 * 15.5k) into the UMP ram. Finally when the
				 * device comes back up in download mode the
				 * driver will cause the new firmware to be
				 * copied from the UMP Ram to I2C and the
				 * firmware will update the record type from
				 * 0xf2 to 0x02.
				 */
				*record = I2C_DESC_TYPE_FIRMWARE_BLANK;

				/* Change the I2C Firmware record type to
				   0xf2 to trigger an update */
				status = write_rom(serial, start_address,
						sizeof(*record), record);
				if (status) {
					kfree(record);
					kfree(firmware_version);
					kfree(rom_desc);
					kfree(ti_manuf_desc);
					return status;
				}

				/* verify the write -- must do this in order
				 * for write to complete before we do the
				 * hardware reset
				 */
				status = read_rom(serial,
							start_address,
							sizeof(*record),
							record);
				if (status) {
					kfree(record);
					kfree(firmware_version);
					kfree(rom_desc);
					kfree(ti_manuf_desc);
					return status;
				}

				if (*record != I2C_DESC_TYPE_FIRMWARE_BLANK) {
					dev_err(dev,
						"%s - error resetting device\n",
						__func__);
					kfree(record);
					kfree(firmware_version);
					kfree(rom_desc);
					kfree(ti_manuf_desc);
					return -ENODEV;
				}

				dbg("%s - HARDWARE RESET", __func__);

				/* Reset UMP -- Back to BOOT MODE */
				status = ti_vsend_sync(serial->serial->dev,
						UMPC_HARDWARE_RESET,
						0, 0, NULL, 0);

				dbg("%s - HARDWARE RESET return %d",
						__func__, status);

				/* return an error on purpose. */
				kfree(record);
				kfree(firmware_version);
				kfree(rom_desc);
				kfree(ti_manuf_desc);
				return -ENODEV;
			}
			kfree(firmware_version);
		}
		/* Search for type 0xF2 record (firmware blank record) */
		else if ((start_address = get_descriptor_addr(serial, I2C_DESC_TYPE_FIRMWARE_BLANK, rom_desc)) != 0) {
#define HEADER_SIZE	(sizeof(struct ti_i2c_desc) + \
					sizeof(struct ti_i2c_firmware_rec))
			__u8 *header;
			__u8 *vheader;

			header = kmalloc(HEADER_SIZE, GFP_KERNEL);
			if (!header) {
				dev_err(dev, "%s - out of memory.\n", __func__);
				kfree(rom_desc);
				kfree(ti_manuf_desc);
				return -ENOMEM;
			}

			vheader = kmalloc(HEADER_SIZE, GFP_KERNEL);
			if (!vheader) {
				dev_err(dev, "%s - out of memory.\n", __func__);
				kfree(header);
				kfree(rom_desc);
				kfree(ti_manuf_desc);
				return -ENOMEM;
			}

			dbg("%s - Found Type BLANK FIRMWARE (Type F2) record",
								__func__);

			/*
			 * In order to update the I2C firmware we must change
			 * the type 2 record to type 0xF2. This will force the
			 * UMP to come up in Boot Mode.  Then while in boot
			 * mode, the driver will download the latest firmware
			 * (padded to 15.5k) into the UMP ram. Finally when the
			 * device comes back up in download mode the driver
			 * will cause the new firmware to be copied from the
			 * UMP Ram to I2C and the firmware will update the
			 * record type from 0xf2 to 0x02.
			 */
			status = build_i2c_fw_hdr(header, dev);
			if (status) {
				kfree(vheader);
				kfree(header);
				kfree(rom_desc);
				kfree(ti_manuf_desc);
				return -EINVAL;
			}

			/* Update I2C with type 0xf2 record with correct
			   size and checksum */
			status = write_rom(serial,
						start_address,
						HEADER_SIZE,
						header);
			if (status) {
				kfree(vheader);
				kfree(header);
				kfree(rom_desc);
				kfree(ti_manuf_desc);
				return -EINVAL;
			}

			/* verify the write -- must do this in order for
			   write to complete before we do the hardware reset */
			status = read_rom(serial, start_address,
							HEADER_SIZE, vheader);

			if (status) {
				dbg("%s - can't read header back", __func__);
				kfree(vheader);
				kfree(header);
				kfree(rom_desc);
				kfree(ti_manuf_desc);
				return status;
			}
			if (memcmp(vheader, header, HEADER_SIZE)) {
				dbg("%s - write download record failed",
					__func__);
				kfree(vheader);
				kfree(header);
				kfree(rom_desc);
				kfree(ti_manuf_desc);
				return -EINVAL;
			}

			kfree(vheader);
			kfree(header);

			dbg("%s - Start firmware update", __func__);

			/* Tell firmware to copy download image into I2C */
			status = ti_vsend_sync(serial->serial->dev,
					UMPC_COPY_DNLD_TO_I2C, 0, 0, NULL, 0);

		  	dbg("%s - Update complete 0x%x", __func__, status);
			if (status) {
				dev_err(dev,
					"%s - UMPC_COPY_DNLD_TO_I2C failed\n",
								__func__);
				kfree(rom_desc);
				kfree(ti_manuf_desc);
				return status;
			}
		}

		// The device is running the download code
		kfree(rom_desc);
		kfree(ti_manuf_desc);
		return 0;
	}

	/********************************************************************/
	/* Boot Mode */
	/********************************************************************/
	dbg("%s - RUNNING IN BOOT MODE", __func__);

	/* Configure the TI device so we can use the BULK pipes for download */
	status = config_boot_dev(serial->serial->dev);
	if (status)
		return status;

	if (le16_to_cpu(serial->serial->dev->descriptor.idVendor)
							!= USB_VENDOR_ID_ION) {
		dbg("%s - VID = 0x%x", __func__,
		     le16_to_cpu(serial->serial->dev->descriptor.idVendor));
		serial->TI_I2C_Type = DTK_ADDR_SPACE_I2C_TYPE_II;
		goto stayinbootmode;
	}

	/* We have an ION device (I2c Must be programmed)
	   Determine I2C image type */
	if (i2c_type_bootmode(serial))
		goto stayinbootmode;

	/* Check for ION Vendor ID and that the I2C is valid */
	if (!check_i2c_image(serial)) {
		struct ti_i2c_image_header *header;
		int i;
		__u8 cs = 0;
		__u8 *buffer;
		int buffer_size;
		int err;
		const struct firmware *fw;
		const char *fw_name = "edgeport/down3.bin";

		/* Validate Hardware version number
		 * Read Manufacturing Descriptor from TI Based Edgeport
		 */
		ti_manuf_desc = kmalloc(sizeof(*ti_manuf_desc), GFP_KERNEL);
		if (!ti_manuf_desc) {
			dev_err(dev, "%s - out of memory.\n", __func__);
			return -ENOMEM;
		}
		status = get_manuf_info(serial, (__u8 *)ti_manuf_desc);
		if (status) {
			kfree(ti_manuf_desc);
			goto stayinbootmode;
		}

		/* Check for version 2 */
		if (!ignore_cpu_rev && ti_cpu_rev(ti_manuf_desc) < 2) {
			dbg("%s - Wrong CPU Rev %d (Must be 2)",
					__func__, ti_cpu_rev(ti_manuf_desc));
			kfree(ti_manuf_desc);
			goto stayinbootmode;
		}

		kfree(ti_manuf_desc);

		/*
		 * In order to update the I2C firmware we must change the type
		 * 2 record to type 0xF2. This will force the UMP to come up
		 * in Boot Mode.  Then while in boot mode, the driver will
		 * download the latest firmware (padded to 15.5k) into the
		 * UMP ram. Finally when the device comes back up in download
		 * mode the driver will cause the new firmware to be copied
		 * from the UMP Ram to I2C and the firmware will update the
		 * record type from 0xf2 to 0x02.
		 *
		 * Do we really have to copy the whole firmware image,
		 * or could we do this in place!
		 */

		/* Allocate a 15.5k buffer + 3 byte header */
		buffer_size = (((1024 * 16) - 512) +
					sizeof(struct ti_i2c_image_header));
		buffer = kmalloc(buffer_size, GFP_KERNEL);
		if (!buffer) {
			dev_err(dev, "%s - out of memory\n", __func__);
			return -ENOMEM;
		}

		/* Initialize the buffer to 0xff (pad the buffer) */
		memset(buffer, 0xff, buffer_size);

		err = request_firmware(&fw, fw_name, dev);
		if (err) {
			printk(KERN_ERR "Failed to load image \"%s\" err %d\n",
			       fw_name, err);
			kfree(buffer);
			return err;
		}
		memcpy(buffer, &fw->data[4], fw->size - 4);
		release_firmware(fw);

		for (i = sizeof(struct ti_i2c_image_header);
				i < buffer_size; i++) {
			cs = (__u8)(cs + buffer[i]);
		}

		header = (struct ti_i2c_image_header *)buffer;

		/* update length and checksum after padding */
		header->Length 	 = cpu_to_le16((__u16)(buffer_size -
					sizeof(struct ti_i2c_image_header)));
		header->CheckSum = cs;

		/* Download the operational code  */
		dbg("%s - Downloading operational code image (TI UMP)",
								__func__);
		status = download_code(serial, buffer, buffer_size);

		kfree(buffer);

		if (status) {
			dbg("%s - Error downloading operational code image",
								__func__);
			return status;
		}

		/* Device will reboot */
		serial->product_info.TiMode = TI_MODE_TRANSITIONING;

		dbg("%s - Download successful -- Device rebooting...",
								__func__);

		/* return an error on purpose */
		return -ENODEV;
	}

stayinbootmode:
	/* Eprom is invalid or blank stay in boot mode */
	dbg("%s - STAYING IN BOOT MODE", __func__);
	serial->product_info.TiMode = TI_MODE_BOOT;

	return 0;
}


static int ti_do_config(struct edgeport_port *port, int feature, int on)
{
	int port_number = port->port->number - port->port->serial->minor;
	on = !!on;	/* 1 or 0 not bitmask */
	return send_cmd(port->port->serial->dev,
			feature, (__u8)(UMPM_UART1_PORT + port_number),
			on, NULL, 0);
}


static int restore_mcr(struct edgeport_port *port, __u8 mcr)
{
	int status = 0;

	dbg("%s - %x", __func__, mcr);

	status = ti_do_config(port, UMPC_SET_CLR_DTR, mcr & MCR_DTR);
	if (status)
		return status;
	status = ti_do_config(port, UMPC_SET_CLR_RTS, mcr & MCR_RTS);
	if (status)
		return status;
	return ti_do_config(port, UMPC_SET_CLR_LOOPBACK, mcr & MCR_LOOPBACK);
}

/* Convert TI LSR to standard UART flags */
static __u8 map_line_status(__u8 ti_lsr)
{
	__u8 lsr = 0;

#define MAP_FLAG(flagUmp, flagUart)    \
	if (ti_lsr & flagUmp) \
		lsr |= flagUart;

	MAP_FLAG(UMP_UART_LSR_OV_MASK, LSR_OVER_ERR)	/* overrun */
	MAP_FLAG(UMP_UART_LSR_PE_MASK, LSR_PAR_ERR)	/* parity error */
	MAP_FLAG(UMP_UART_LSR_FE_MASK, LSR_FRM_ERR)	/* framing error */
	MAP_FLAG(UMP_UART_LSR_BR_MASK, LSR_BREAK)	/* break detected */
	MAP_FLAG(UMP_UART_LSR_RX_MASK, LSR_RX_AVAIL)	/* rx data available */
	MAP_FLAG(UMP_UART_LSR_TX_MASK, LSR_TX_EMPTY)	/* tx hold reg empty */

#undef MAP_FLAG

	return lsr;
}

static void handle_new_msr(struct edgeport_port *edge_port, __u8 msr)
{
	struct async_icount *icount;
	struct tty_struct *tty;

	dbg("%s - %02x", __func__, msr);

	if (msr & (EDGEPORT_MSR_DELTA_CTS | EDGEPORT_MSR_DELTA_DSR |
			EDGEPORT_MSR_DELTA_RI | EDGEPORT_MSR_DELTA_CD)) {
		icount = &edge_port->icount;

		/* update input line counters */
		if (msr & EDGEPORT_MSR_DELTA_CTS)
			icount->cts++;
		if (msr & EDGEPORT_MSR_DELTA_DSR)
			icount->dsr++;
		if (msr & EDGEPORT_MSR_DELTA_CD)
			icount->dcd++;
		if (msr & EDGEPORT_MSR_DELTA_RI)
			icount->rng++;
		wake_up_interruptible(&edge_port->delta_msr_wait);
	}

	/* Save the new modem status */
	edge_port->shadow_msr = msr & 0xf0;

	tty = tty_port_tty_get(&edge_port->port->port);
	/* handle CTS flow control */
	if (tty && C_CRTSCTS(tty)) {
		if (msr & EDGEPORT_MSR_CTS) {
			tty->hw_stopped = 0;
			tty_wakeup(tty);
		} else {
			tty->hw_stopped = 1;
		}
	}
	tty_kref_put(tty);
}

static void handle_new_lsr(struct edgeport_port *edge_port, int lsr_data,
							__u8 lsr, __u8 data)
{
	struct async_icount *icount;
	__u8 new_lsr = (__u8)(lsr & (__u8)(LSR_OVER_ERR | LSR_PAR_ERR |
						LSR_FRM_ERR | LSR_BREAK));
	struct tty_struct *tty;

	dbg("%s - %02x", __func__, new_lsr);

	edge_port->shadow_lsr = lsr;

	if (new_lsr & LSR_BREAK)
		/*
		 * Parity and Framing errors only count if they
		 * occur exclusive of a break being received.
		 */
		new_lsr &= (__u8)(LSR_OVER_ERR | LSR_BREAK);

	/* Place LSR data byte into Rx buffer */
	if (lsr_data) {
		tty = tty_port_tty_get(&edge_port->port->port);
		if (tty) {
			edge_tty_recv(&edge_port->port->dev, tty, &data, 1);
			tty_kref_put(tty);
		}
	}

	/* update input line counters */
	icount = &edge_port->icount;
	if (new_lsr & LSR_BREAK)
		icount->brk++;
	if (new_lsr & LSR_OVER_ERR)
		icount->overrun++;
	if (new_lsr & LSR_PAR_ERR)
		icount->parity++;
	if (new_lsr & LSR_FRM_ERR)
		icount->frame++;
}


static void edge_interrupt_callback(struct urb *urb)
{
	struct edgeport_serial *edge_serial = urb->context;
	struct usb_serial_port *port;
	struct edgeport_port *edge_port;
	unsigned char *data = urb->transfer_buffer;
	int length = urb->actual_length;
	int port_number;
	int function;
	int retval;
	__u8 lsr;
	__u8 msr;
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
		dev_err(&urb->dev->dev, "%s - nonzero urb status received: "
			"%d\n", __func__, status);
		goto exit;
	}

	if (!length) {
		dbg("%s - no data in urb", __func__);
		goto exit;
	}

	usb_serial_debug_data(debug, &edge_serial->serial->dev->dev,
						__func__, length, data);

	if (length != 2) {
		dbg("%s - expecting packet of size 2, got %d",
							__func__, length);
		goto exit;
	}

	port_number = TIUMP_GET_PORT_FROM_CODE(data[0]);
	function    = TIUMP_GET_FUNC_FROM_CODE(data[0]);
	dbg("%s - port_number %d, function %d, info 0x%x",
	     __func__, port_number, function, data[1]);
	port = edge_serial->serial->port[port_number];
	edge_port = usb_get_serial_port_data(port);
	if (!edge_port) {
		dbg("%s - edge_port not found", __func__);
		return;
	}
	switch (function) {
	case TIUMP_INTERRUPT_CODE_LSR:
		lsr = map_line_status(data[1]);
		if (lsr & UMP_UART_LSR_DATA_MASK) {
			/* Save the LSR event for bulk read
			   completion routine */
			dbg("%s - LSR Event Port %u LSR Status = %02x",
			     __func__, port_number, lsr);
			edge_port->lsr_event = 1;
			edge_port->lsr_mask = lsr;
		} else {
			dbg("%s - ===== Port %d LSR Status = %02x ======",
			     __func__, port_number, lsr);
			handle_new_lsr(edge_port, 0, lsr, 0);
		}
		break;

	case TIUMP_INTERRUPT_CODE_MSR:	/* MSR */
		/* Copy MSR from UMP */
		msr = data[1];
		dbg("%s - ===== Port %u MSR Status = %02x ======",
		     __func__, port_number, msr);
		handle_new_msr(edge_port, msr);
		break;

	default:
		dev_err(&urb->dev->dev,
			"%s - Unknown Interrupt code from UMP %x\n",
			__func__, data[1]);
		break;

	}

exit:
	retval = usb_submit_urb(urb, GFP_ATOMIC);
	if (retval)
		dev_err(&urb->dev->dev,
			"%s - usb_submit_urb failed with result %d\n",
			 __func__, retval);
}

static void edge_bulk_in_callback(struct urb *urb)
{
	struct edgeport_port *edge_port = urb->context;
	unsigned char *data = urb->transfer_buffer;
	struct tty_struct *tty;
	int retval = 0;
	int port_number;
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
		dev_err(&urb->dev->dev,
			"%s - nonzero read bulk status received: %d\n",
			     __func__, status);
	}

	if (status == -EPIPE)
		goto exit;

	if (status) {
		dev_err(&urb->dev->dev, "%s - stopping read!\n", __func__);
		return;
	}

	port_number = edge_port->port->number - edge_port->port->serial->minor;

	if (edge_port->lsr_event) {
		edge_port->lsr_event = 0;
		dbg("%s ===== Port %u LSR Status = %02x, Data = %02x ======",
		     __func__, port_number, edge_port->lsr_mask, *data);
		handle_new_lsr(edge_port, 1, edge_port->lsr_mask, *data);
		/* Adjust buffer length/pointer */
		--urb->actual_length;
		++data;
	}

	tty = tty_port_tty_get(&edge_port->port->port);
	if (tty && urb->actual_length) {
		usb_serial_debug_data(debug, &edge_port->port->dev,
					__func__, urb->actual_length, data);
		if (edge_port->close_pending)
			dbg("%s - close pending, dropping data on the floor",
								__func__);
		else
			edge_tty_recv(&edge_port->port->dev, tty, data,
							urb->actual_length);
		edge_port->icount.rx += urb->actual_length;
	}
	tty_kref_put(tty);

exit:
	/* continue read unless stopped */
	spin_lock(&edge_port->ep_lock);
	if (edge_port->ep_read_urb_state == EDGE_READ_URB_RUNNING)
		retval = usb_submit_urb(urb, GFP_ATOMIC);
	else if (edge_port->ep_read_urb_state == EDGE_READ_URB_STOPPING)
		edge_port->ep_read_urb_state = EDGE_READ_URB_STOPPED;

	spin_unlock(&edge_port->ep_lock);
	if (retval)
		dev_err(&urb->dev->dev,
			"%s - usb_submit_urb failed with result %d\n",
			 __func__, retval);
}

static void edge_tty_recv(struct device *dev, struct tty_struct *tty,
					unsigned char *data, int length)
{
	int queued;

	queued = tty_insert_flip_string(tty, data, length);
	if (queued < length)
		dev_err(dev, "%s - dropping data, %d bytes lost\n",
			__func__, length - queued);
	tty_flip_buffer_push(tty);
}

static void edge_bulk_out_callback(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	struct edgeport_port *edge_port = usb_get_serial_port_data(port);
	int status = urb->status;
	struct tty_struct *tty;

	dbg("%s - port %d", __func__, port->number);

	edge_port->ep_write_urb_in_use = 0;

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
		dev_err_console(port, "%s - nonzero write bulk status "
			"received: %d\n", __func__, status);
	}

	/* send any buffered data */
	tty = tty_port_tty_get(&port->port);
	edge_send(tty);
	tty_kref_put(tty);
}

static int edge_open(struct tty_struct *tty, struct usb_serial_port *port)
{
	struct edgeport_port *edge_port = usb_get_serial_port_data(port);
	struct edgeport_serial *edge_serial;
	struct usb_device *dev;
	struct urb *urb;
	int port_number;
	int status;
	u16 open_settings;
	u8 transaction_timeout;

	dbg("%s - port %d", __func__, port->number);

	if (edge_port == NULL)
		return -ENODEV;

	port_number = port->number - port->serial->minor;
	switch (port_number) {
	case 0:
		edge_port->uart_base = UMPMEM_BASE_UART1;
		edge_port->dma_address = UMPD_OEDB1_ADDRESS;
		break;
	case 1:
		edge_port->uart_base = UMPMEM_BASE_UART2;
		edge_port->dma_address = UMPD_OEDB2_ADDRESS;
		break;
	default:
		dev_err(&port->dev, "Unknown port number!!!\n");
		return -ENODEV;
	}

	dbg("%s - port_number = %d, uart_base = %04x, dma_address = %04x",
				__func__, port_number, edge_port->uart_base,
				edge_port->dma_address);

	dev = port->serial->dev;

	memset(&(edge_port->icount), 0x00, sizeof(edge_port->icount));
	init_waitqueue_head(&edge_port->delta_msr_wait);

	/* turn off loopback */
	status = ti_do_config(edge_port, UMPC_SET_CLR_LOOPBACK, 0);
	if (status) {
		dev_err(&port->dev,
				"%s - cannot send clear loopback command, %d\n",
			__func__, status);
		return status;
	}

	/* set up the port settings */
	if (tty)
		edge_set_termios(tty, port, tty->termios);

	/* open up the port */

	/* milliseconds to timeout for DMA transfer */
	transaction_timeout = 2;

	edge_port->ump_read_timeout =
				max(20, ((transaction_timeout * 3) / 2));

	/* milliseconds to timeout for DMA transfer */
	open_settings = (u8)(UMP_DMA_MODE_CONTINOUS |
			     UMP_PIPE_TRANS_TIMEOUT_ENA |
			     (transaction_timeout << 2));

	dbg("%s - Sending UMPC_OPEN_PORT", __func__);

	/* Tell TI to open and start the port */
	status = send_cmd(dev, UMPC_OPEN_PORT,
		(u8)(UMPM_UART1_PORT + port_number), open_settings, NULL, 0);
	if (status) {
		dev_err(&port->dev, "%s - cannot send open command, %d\n",
							__func__, status);
		return status;
	}

	/* Start the DMA? */
	status = send_cmd(dev, UMPC_START_PORT,
		(u8)(UMPM_UART1_PORT + port_number), 0, NULL, 0);
	if (status) {
		dev_err(&port->dev, "%s - cannot send start DMA command, %d\n",
							__func__, status);
		return status;
	}

	/* Clear TX and RX buffers in UMP */
	status = purge_port(port, UMP_PORT_DIR_OUT | UMP_PORT_DIR_IN);
	if (status) {
		dev_err(&port->dev,
			"%s - cannot send clear buffers command, %d\n",
			__func__, status);
		return status;
	}

	/* Read Initial MSR */
	status = ti_vread_sync(dev, UMPC_READ_MSR, 0,
				(__u16)(UMPM_UART1_PORT + port_number),
				&edge_port->shadow_msr, 1);
	if (status) {
		dev_err(&port->dev, "%s - cannot send read MSR command, %d\n",
							__func__, status);
		return status;
	}

	dbg("ShadowMSR 0x%X", edge_port->shadow_msr);

	/* Set Initial MCR */
	edge_port->shadow_mcr = MCR_RTS | MCR_DTR;
	dbg("ShadowMCR 0x%X", edge_port->shadow_mcr);

	edge_serial = edge_port->edge_serial;
	if (mutex_lock_interruptible(&edge_serial->es_lock))
		return -ERESTARTSYS;
	if (edge_serial->num_ports_open == 0) {
		/* we are the first port to open, post the interrupt urb */
		urb = edge_serial->serial->port[0]->interrupt_in_urb;
		if (!urb) {
			dev_err(&port->dev,
				"%s - no interrupt urb present, exiting\n",
				__func__);
			status = -EINVAL;
			goto release_es_lock;
		}
		urb->context = edge_serial;
		status = usb_submit_urb(urb, GFP_KERNEL);
		if (status) {
			dev_err(&port->dev,
				"%s - usb_submit_urb failed with value %d\n",
					__func__, status);
			goto release_es_lock;
		}
	}

	/*
	 * reset the data toggle on the bulk endpoints to work around bug in
	 * host controllers where things get out of sync some times
	 */
	usb_clear_halt(dev, port->write_urb->pipe);
	usb_clear_halt(dev, port->read_urb->pipe);

	/* start up our bulk read urb */
	urb = port->read_urb;
	if (!urb) {
		dev_err(&port->dev, "%s - no read urb present, exiting\n",
								__func__);
		status = -EINVAL;
		goto unlink_int_urb;
	}
	edge_port->ep_read_urb_state = EDGE_READ_URB_RUNNING;
	urb->context = edge_port;
	status = usb_submit_urb(urb, GFP_KERNEL);
	if (status) {
		dev_err(&port->dev,
			"%s - read bulk usb_submit_urb failed with value %d\n",
				__func__, status);
		goto unlink_int_urb;
	}

	++edge_serial->num_ports_open;

	dbg("%s - exited", __func__);

	goto release_es_lock;

unlink_int_urb:
	if (edge_port->edge_serial->num_ports_open == 0)
		usb_kill_urb(port->serial->port[0]->interrupt_in_urb);
release_es_lock:
	mutex_unlock(&edge_serial->es_lock);
	return status;
}

static void edge_close(struct usb_serial_port *port)
{
	struct edgeport_serial *edge_serial;
	struct edgeport_port *edge_port;
	int port_number;
	int status;

	dbg("%s - port %d", __func__, port->number);

	edge_serial = usb_get_serial_data(port->serial);
	edge_port = usb_get_serial_port_data(port);
	if (edge_serial == NULL || edge_port == NULL)
		return;

	/* The bulkreadcompletion routine will check
	 * this flag and dump add read data */
	edge_port->close_pending = 1;

	/* chase the port close and flush */
	chase_port(edge_port, (HZ * closing_wait) / 100, 1);

	usb_kill_urb(port->read_urb);
	usb_kill_urb(port->write_urb);
	edge_port->ep_write_urb_in_use = 0;

	/* assuming we can still talk to the device,
	 * send a close port command to it */
	dbg("%s - send umpc_close_port", __func__);
	port_number = port->number - port->serial->minor;
	status = send_cmd(port->serial->dev,
				     UMPC_CLOSE_PORT,
				     (__u8)(UMPM_UART1_PORT + port_number),
				     0,
				     NULL,
				     0);
	mutex_lock(&edge_serial->es_lock);
	--edge_port->edge_serial->num_ports_open;
	if (edge_port->edge_serial->num_ports_open <= 0) {
		/* last port is now closed, let's shut down our interrupt urb */
		usb_kill_urb(port->serial->port[0]->interrupt_in_urb);
		edge_port->edge_serial->num_ports_open = 0;
	}
	mutex_unlock(&edge_serial->es_lock);
	edge_port->close_pending = 0;

	dbg("%s - exited", __func__);
}

static int edge_write(struct tty_struct *tty, struct usb_serial_port *port,
				const unsigned char *data, int count)
{
	struct edgeport_port *edge_port = usb_get_serial_port_data(port);

	dbg("%s - port %d", __func__, port->number);

	if (count == 0) {
		dbg("%s - write request of 0 bytes", __func__);
		return 0;
	}

	if (edge_port == NULL)
		return -ENODEV;
	if (edge_port->close_pending == 1)
		return -ENODEV;

	count = kfifo_in_locked(&edge_port->write_fifo, data, count,
							&edge_port->ep_lock);
	edge_send(tty);

	return count;
}

static void edge_send(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	int count, result;
	struct edgeport_port *edge_port = usb_get_serial_port_data(port);
	unsigned long flags;


	dbg("%s - port %d", __func__, port->number);

	spin_lock_irqsave(&edge_port->ep_lock, flags);

	if (edge_port->ep_write_urb_in_use) {
		spin_unlock_irqrestore(&edge_port->ep_lock, flags);
		return;
	}

	count = kfifo_out(&edge_port->write_fifo,
				port->write_urb->transfer_buffer,
				port->bulk_out_size);

	if (count == 0) {
		spin_unlock_irqrestore(&edge_port->ep_lock, flags);
		return;
	}

	edge_port->ep_write_urb_in_use = 1;

	spin_unlock_irqrestore(&edge_port->ep_lock, flags);

	usb_serial_debug_data(debug, &port->dev, __func__, count,
				port->write_urb->transfer_buffer);

	/* set up our urb */
	port->write_urb->transfer_buffer_length = count;

	/* send the data out the bulk port */
	result = usb_submit_urb(port->write_urb, GFP_ATOMIC);
	if (result) {
		dev_err_console(port,
			"%s - failed submitting write urb, error %d\n",
				__func__, result);
		edge_port->ep_write_urb_in_use = 0;
		/* TODO: reschedule edge_send */
	} else
		edge_port->icount.tx += count;

	/* wakeup any process waiting for writes to complete */
	/* there is now more room in the buffer for new writes */
	if (tty)
		tty_wakeup(tty);
}

static int edge_write_room(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct edgeport_port *edge_port = usb_get_serial_port_data(port);
	int room = 0;
	unsigned long flags;

	dbg("%s - port %d", __func__, port->number);

	if (edge_port == NULL)
		return 0;
	if (edge_port->close_pending == 1)
		return 0;

	spin_lock_irqsave(&edge_port->ep_lock, flags);
	room = kfifo_avail(&edge_port->write_fifo);
	spin_unlock_irqrestore(&edge_port->ep_lock, flags);

	dbg("%s - returns %d", __func__, room);
	return room;
}

static int edge_chars_in_buffer(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct edgeport_port *edge_port = usb_get_serial_port_data(port);
	int chars = 0;
	unsigned long flags;

	dbg("%s - port %d", __func__, port->number);

	if (edge_port == NULL)
		return 0;
	if (edge_port->close_pending == 1)
		return 0;

	spin_lock_irqsave(&edge_port->ep_lock, flags);
	chars = kfifo_len(&edge_port->write_fifo);
	spin_unlock_irqrestore(&edge_port->ep_lock, flags);

	dbg("%s - returns %d", __func__, chars);
	return chars;
}

static void edge_throttle(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct edgeport_port *edge_port = usb_get_serial_port_data(port);
	int status;

	dbg("%s - port %d", __func__, port->number);

	if (edge_port == NULL)
		return;

	/* if we are implementing XON/XOFF, send the stop character */
	if (I_IXOFF(tty)) {
		unsigned char stop_char = STOP_CHAR(tty);
		status = edge_write(tty, port, &stop_char, 1);
		if (status <= 0) {
			dev_err(&port->dev, "%s - failed to write stop character, %d\n", __func__, status);
		}
	}

	/* if we are implementing RTS/CTS, stop reads */
	/* and the Edgeport will clear the RTS line */
	if (C_CRTSCTS(tty))
		stop_read(edge_port);

}

static void edge_unthrottle(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct edgeport_port *edge_port = usb_get_serial_port_data(port);
	int status;

	dbg("%s - port %d", __func__, port->number);

	if (edge_port == NULL)
		return;

	/* if we are implementing XON/XOFF, send the start character */
	if (I_IXOFF(tty)) {
		unsigned char start_char = START_CHAR(tty);
		status = edge_write(tty, port, &start_char, 1);
		if (status <= 0) {
			dev_err(&port->dev, "%s - failed to write start character, %d\n", __func__, status);
		}
	}
	/* if we are implementing RTS/CTS, restart reads */
	/* are the Edgeport will assert the RTS line */
	if (C_CRTSCTS(tty)) {
		status = restart_read(edge_port);
		if (status)
			dev_err(&port->dev,
				"%s - read bulk usb_submit_urb failed: %d\n",
							__func__, status);
	}

}

static void stop_read(struct edgeport_port *edge_port)
{
	unsigned long flags;

	spin_lock_irqsave(&edge_port->ep_lock, flags);

	if (edge_port->ep_read_urb_state == EDGE_READ_URB_RUNNING)
		edge_port->ep_read_urb_state = EDGE_READ_URB_STOPPING;
	edge_port->shadow_mcr &= ~MCR_RTS;

	spin_unlock_irqrestore(&edge_port->ep_lock, flags);
}

static int restart_read(struct edgeport_port *edge_port)
{
	struct urb *urb;
	int status = 0;
	unsigned long flags;

	spin_lock_irqsave(&edge_port->ep_lock, flags);

	if (edge_port->ep_read_urb_state == EDGE_READ_URB_STOPPED) {
		urb = edge_port->port->read_urb;
		status = usb_submit_urb(urb, GFP_ATOMIC);
	}
	edge_port->ep_read_urb_state = EDGE_READ_URB_RUNNING;
	edge_port->shadow_mcr |= MCR_RTS;

	spin_unlock_irqrestore(&edge_port->ep_lock, flags);

	return status;
}

static void change_port_settings(struct tty_struct *tty,
		struct edgeport_port *edge_port, struct ktermios *old_termios)
{
	struct ump_uart_config *config;
	int baud;
	unsigned cflag;
	int status;
	int port_number = edge_port->port->number -
					edge_port->port->serial->minor;

	dbg("%s - port %d", __func__, edge_port->port->number);

	config = kmalloc (sizeof (*config), GFP_KERNEL);
	if (!config) {
		*tty->termios = *old_termios;
		dev_err(&edge_port->port->dev, "%s - out of memory\n",
								__func__);
		return;
	}

	cflag = tty->termios->c_cflag;

	config->wFlags = 0;

	/* These flags must be set */
	config->wFlags |= UMP_MASK_UART_FLAGS_RECEIVE_MS_INT;
	config->wFlags |= UMP_MASK_UART_FLAGS_AUTO_START_ON_ERR;
	config->bUartMode = (__u8)(edge_port->bUartMode);

	switch (cflag & CSIZE) {
	case CS5:
		    config->bDataBits = UMP_UART_CHAR5BITS;
		    dbg("%s - data bits = 5", __func__);
		    break;
	case CS6:
		    config->bDataBits = UMP_UART_CHAR6BITS;
		    dbg("%s - data bits = 6", __func__);
		    break;
	case CS7:
		    config->bDataBits = UMP_UART_CHAR7BITS;
		    dbg("%s - data bits = 7", __func__);
		    break;
	default:
	case CS8:
		    config->bDataBits = UMP_UART_CHAR8BITS;
		    dbg("%s - data bits = 8", __func__);
			    break;
	}

	if (cflag & PARENB) {
		if (cflag & PARODD) {
			config->wFlags |= UMP_MASK_UART_FLAGS_PARITY;
			config->bParity = UMP_UART_ODDPARITY;
			dbg("%s - parity = odd", __func__);
		} else {
			config->wFlags |= UMP_MASK_UART_FLAGS_PARITY;
			config->bParity = UMP_UART_EVENPARITY;
			dbg("%s - parity = even", __func__);
		}
	} else {
		config->bParity = UMP_UART_NOPARITY;
		dbg("%s - parity = none", __func__);
	}

	if (cflag & CSTOPB) {
		config->bStopBits = UMP_UART_STOPBIT2;
		dbg("%s - stop bits = 2", __func__);
	} else {
		config->bStopBits = UMP_UART_STOPBIT1;
		dbg("%s - stop bits = 1", __func__);
	}

	/* figure out the flow control settings */
	if (cflag & CRTSCTS) {
		config->wFlags |= UMP_MASK_UART_FLAGS_OUT_X_CTS_FLOW;
		config->wFlags |= UMP_MASK_UART_FLAGS_RTS_FLOW;
		dbg("%s - RTS/CTS is enabled", __func__);
	} else {
		dbg("%s - RTS/CTS is disabled", __func__);
		tty->hw_stopped = 0;
		restart_read(edge_port);
	}

	/* if we are implementing XON/XOFF, set the start and stop
	   character in the device */
	config->cXon  = START_CHAR(tty);
	config->cXoff = STOP_CHAR(tty);

	/* if we are implementing INBOUND XON/XOFF */
	if (I_IXOFF(tty)) {
		config->wFlags |= UMP_MASK_UART_FLAGS_IN_X;
		dbg("%s - INBOUND XON/XOFF is enabled, XON = %2x, XOFF = %2x",
		     __func__, config->cXon, config->cXoff);
	} else
		dbg("%s - INBOUND XON/XOFF is disabled", __func__);

	/* if we are implementing OUTBOUND XON/XOFF */
	if (I_IXON(tty)) {
		config->wFlags |= UMP_MASK_UART_FLAGS_OUT_X;
		dbg("%s - OUTBOUND XON/XOFF is enabled, XON = %2x, XOFF = %2x",
		     __func__, config->cXon, config->cXoff);
	} else
		dbg("%s - OUTBOUND XON/XOFF is disabled", __func__);

	tty->termios->c_cflag &= ~CMSPAR;

	/* Round the baud rate */
	baud = tty_get_baud_rate(tty);
	if (!baud) {
		/* pick a default, any default... */
		baud = 9600;
	} else
		tty_encode_baud_rate(tty, baud, baud);

	edge_port->baud_rate = baud;
	config->wBaudRate = (__u16)((461550L + baud/2) / baud);

	/* FIXME: Recompute actual baud from divisor here */

	dbg("%s - baud rate = %d, wBaudRate = %d", __func__, baud,
							config->wBaudRate);

	dbg("wBaudRate:   %d", (int)(461550L / config->wBaudRate));
	dbg("wFlags:    0x%x", config->wFlags);
	dbg("bDataBits:   %d", config->bDataBits);
	dbg("bParity:     %d", config->bParity);
	dbg("bStopBits:   %d", config->bStopBits);
	dbg("cXon:        %d", config->cXon);
	dbg("cXoff:       %d", config->cXoff);
	dbg("bUartMode:   %d", config->bUartMode);

	/* move the word values into big endian mode */
	cpu_to_be16s(&config->wFlags);
	cpu_to_be16s(&config->wBaudRate);

	status = send_cmd(edge_port->port->serial->dev, UMPC_SET_CONFIG,
				(__u8)(UMPM_UART1_PORT + port_number),
				0, (__u8 *)config, sizeof(*config));
	if (status)
		dbg("%s - error %d when trying to write config to device",
		     __func__, status);
	kfree(config);
}

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
	/* change the port settings to the new ones specified */
	change_port_settings(tty, edge_port, old_termios);
}

static int edge_tiocmset(struct tty_struct *tty,
					unsigned int set, unsigned int clear)
{
	struct usb_serial_port *port = tty->driver_data;
	struct edgeport_port *edge_port = usb_get_serial_port_data(port);
	unsigned int mcr;
	unsigned long flags;

	dbg("%s - port %d", __func__, port->number);

	spin_lock_irqsave(&edge_port->ep_lock, flags);
	mcr = edge_port->shadow_mcr;
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

	edge_port->shadow_mcr = mcr;
	spin_unlock_irqrestore(&edge_port->ep_lock, flags);

	restore_mcr(edge_port, mcr);
	return 0;
}

static int edge_tiocmget(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct edgeport_port *edge_port = usb_get_serial_port_data(port);
	unsigned int result = 0;
	unsigned int msr;
	unsigned int mcr;
	unsigned long flags;

	dbg("%s - port %d", __func__, port->number);

	spin_lock_irqsave(&edge_port->ep_lock, flags);

	msr = edge_port->shadow_msr;
	mcr = edge_port->shadow_mcr;
	result = ((mcr & MCR_DTR)	? TIOCM_DTR: 0)	  /* 0x002 */
		  | ((mcr & MCR_RTS)	? TIOCM_RTS: 0)   /* 0x004 */
		  | ((msr & EDGEPORT_MSR_CTS)	? TIOCM_CTS: 0)   /* 0x020 */
		  | ((msr & EDGEPORT_MSR_CD)	? TIOCM_CAR: 0)   /* 0x040 */
		  | ((msr & EDGEPORT_MSR_RI)	? TIOCM_RI:  0)   /* 0x080 */
		  | ((msr & EDGEPORT_MSR_DSR)	? TIOCM_DSR: 0);  /* 0x100 */


	dbg("%s -- %x", __func__, result);
	spin_unlock_irqrestore(&edge_port->ep_lock, flags);

	return result;
}

static int edge_get_icount(struct tty_struct *tty,
				struct serial_icounter_struct *icount)
{
	struct usb_serial_port *port = tty->driver_data;
	struct edgeport_port *edge_port = usb_get_serial_port_data(port);
	struct async_icount *ic = &edge_port->icount;

	icount->cts = ic->cts;
	icount->dsr = ic->dsr;
	icount->rng = ic->rng;
	icount->dcd = ic->dcd;
	icount->tx = ic->tx;
        icount->rx = ic->rx;
        icount->frame = ic->frame;
        icount->parity = ic->parity;
        icount->overrun = ic->overrun;
        icount->brk = ic->brk;
        icount->buf_overrun = ic->buf_overrun;
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
	tmp.xmit_fifo_size	= edge_port->port->bulk_out_size;
	tmp.baud_base		= 9600;
	tmp.close_delay		= 5*HZ;
	tmp.closing_wait	= closing_wait;

	if (copy_to_user(retinfo, &tmp, sizeof(*retinfo)))
		return -EFAULT;
	return 0;
}

static int edge_ioctl(struct tty_struct *tty,
					unsigned int cmd, unsigned long arg)
{
	struct usb_serial_port *port = tty->driver_data;
	struct edgeport_port *edge_port = usb_get_serial_port_data(port);
	struct async_icount cnow;
	struct async_icount cprev;

	dbg("%s - port %d, cmd = 0x%x", __func__, port->number, cmd);

	switch (cmd) {
	case TIOCGSERIAL:
		dbg("%s - (%d) TIOCGSERIAL", __func__, port->number);
		return get_serial_info(edge_port,
				(struct serial_struct __user *) arg);
	case TIOCMIWAIT:
		dbg("%s - (%d) TIOCMIWAIT", __func__, port->number);
		cprev = edge_port->icount;
		while (1) {
			interruptible_sleep_on(&edge_port->delta_msr_wait);
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
		/* not reached */
		break;
	}
	return -ENOIOCTLCMD;
}

static void edge_break(struct tty_struct *tty, int break_state)
{
	struct usb_serial_port *port = tty->driver_data;
	struct edgeport_port *edge_port = usb_get_serial_port_data(port);
	int status;
	int bv = 0;	/* Off */

	dbg("%s - state = %d", __func__, break_state);

	/* chase the port close */
	chase_port(edge_port, 0, 0);

	if (break_state == -1)
		bv = 1;	/* On */
	status = ti_do_config(edge_port, UMPC_SET_CLR_BREAK, bv);
	if (status)
		dbg("%s - error %d sending break set/clear command.",
		     __func__, status);
}

static int edge_startup(struct usb_serial *serial)
{
	struct edgeport_serial *edge_serial;
	struct edgeport_port *edge_port;
	struct usb_device *dev;
	int status;
	int i;

	dev = serial->dev;

	/* create our private serial structure */
	edge_serial = kzalloc(sizeof(struct edgeport_serial), GFP_KERNEL);
	if (edge_serial == NULL) {
		dev_err(&serial->dev->dev, "%s - Out of memory\n", __func__);
		return -ENOMEM;
	}
	mutex_init(&edge_serial->es_lock);
	edge_serial->serial = serial;
	usb_set_serial_data(serial, edge_serial);

	status = download_fw(edge_serial);
	if (status) {
		kfree(edge_serial);
		return status;
	}

	/* set up our port private structures */
	for (i = 0; i < serial->num_ports; ++i) {
		edge_port = kzalloc(sizeof(struct edgeport_port), GFP_KERNEL);
		if (edge_port == NULL) {
			dev_err(&serial->dev->dev, "%s - Out of memory\n",
								__func__);
			goto cleanup;
		}
		spin_lock_init(&edge_port->ep_lock);
		if (kfifo_alloc(&edge_port->write_fifo, EDGE_OUT_BUF_SIZE,
								GFP_KERNEL)) {
			dev_err(&serial->dev->dev, "%s - Out of memory\n",
								__func__);
			kfree(edge_port);
			goto cleanup;
		}
		edge_port->port = serial->port[i];
		edge_port->edge_serial = edge_serial;
		usb_set_serial_port_data(serial->port[i], edge_port);
		edge_port->bUartMode = default_uart_mode;
	}

	return 0;

cleanup:
	for (--i; i >= 0; --i) {
		edge_port = usb_get_serial_port_data(serial->port[i]);
		kfifo_free(&edge_port->write_fifo);
		kfree(edge_port);
		usb_set_serial_port_data(serial->port[i], NULL);
	}
	kfree(edge_serial);
	usb_set_serial_data(serial, NULL);
	return -ENOMEM;
}

static void edge_disconnect(struct usb_serial *serial)
{
	dbg("%s", __func__);
}

static void edge_release(struct usb_serial *serial)
{
	int i;
	struct edgeport_port *edge_port;

	dbg("%s", __func__);

	for (i = 0; i < serial->num_ports; ++i) {
		edge_port = usb_get_serial_port_data(serial->port[i]);
		kfifo_free(&edge_port->write_fifo);
		kfree(edge_port);
	}
	kfree(usb_get_serial_data(serial));
}


/* Sysfs Attributes */

static ssize_t show_uart_mode(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct usb_serial_port *port = to_usb_serial_port(dev);
	struct edgeport_port *edge_port = usb_get_serial_port_data(port);

	return sprintf(buf, "%d\n", edge_port->bUartMode);
}

static ssize_t store_uart_mode(struct device *dev,
	struct device_attribute *attr, const char *valbuf, size_t count)
{
	struct usb_serial_port *port = to_usb_serial_port(dev);
	struct edgeport_port *edge_port = usb_get_serial_port_data(port);
	unsigned int v = simple_strtoul(valbuf, NULL, 0);

	dbg("%s: setting uart_mode = %d", __func__, v);

	if (v < 256)
		edge_port->bUartMode = v;
	else
		dev_err(dev, "%s - uart_mode %d is invalid\n", __func__, v);

	return count;
}

static DEVICE_ATTR(uart_mode, S_IWUSR | S_IRUGO, show_uart_mode,
							store_uart_mode);

static int edge_create_sysfs_attrs(struct usb_serial_port *port)
{
	return device_create_file(&port->dev, &dev_attr_uart_mode);
}

static int edge_remove_sysfs_attrs(struct usb_serial_port *port)
{
	device_remove_file(&port->dev, &dev_attr_uart_mode);
	return 0;
}


static struct usb_serial_driver edgeport_1port_device = {
	.driver = {
		.owner		= THIS_MODULE,
		.name		= "edgeport_ti_1",
	},
	.description		= "Edgeport TI 1 port adapter",
	.id_table		= edgeport_1port_id_table,
	.num_ports		= 1,
	.open			= edge_open,
	.close			= edge_close,
	.throttle		= edge_throttle,
	.unthrottle		= edge_unthrottle,
	.attach			= edge_startup,
	.disconnect		= edge_disconnect,
	.release		= edge_release,
	.port_probe		= edge_create_sysfs_attrs,
	.port_remove		= edge_remove_sysfs_attrs,
	.ioctl			= edge_ioctl,
	.set_termios		= edge_set_termios,
	.tiocmget		= edge_tiocmget,
	.tiocmset		= edge_tiocmset,
	.get_icount		= edge_get_icount,
	.write			= edge_write,
	.write_room		= edge_write_room,
	.chars_in_buffer	= edge_chars_in_buffer,
	.break_ctl		= edge_break,
	.read_int_callback	= edge_interrupt_callback,
	.read_bulk_callback	= edge_bulk_in_callback,
	.write_bulk_callback	= edge_bulk_out_callback,
};

static struct usb_serial_driver edgeport_2port_device = {
	.driver = {
		.owner		= THIS_MODULE,
		.name		= "edgeport_ti_2",
	},
	.description		= "Edgeport TI 2 port adapter",
	.id_table		= edgeport_2port_id_table,
	.num_ports		= 2,
	.open			= edge_open,
	.close			= edge_close,
	.throttle		= edge_throttle,
	.unthrottle		= edge_unthrottle,
	.attach			= edge_startup,
	.disconnect		= edge_disconnect,
	.release		= edge_release,
	.port_probe		= edge_create_sysfs_attrs,
	.port_remove		= edge_remove_sysfs_attrs,
	.ioctl			= edge_ioctl,
	.set_termios		= edge_set_termios,
	.tiocmget		= edge_tiocmget,
	.tiocmset		= edge_tiocmset,
	.write			= edge_write,
	.write_room		= edge_write_room,
	.chars_in_buffer	= edge_chars_in_buffer,
	.break_ctl		= edge_break,
	.read_int_callback	= edge_interrupt_callback,
	.read_bulk_callback	= edge_bulk_in_callback,
	.write_bulk_callback	= edge_bulk_out_callback,
};

static struct usb_serial_driver * const serial_drivers[] = {
	&edgeport_1port_device, &edgeport_2port_device, NULL
};

module_usb_serial_driver(io_driver, serial_drivers);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
MODULE_FIRMWARE("edgeport/down3.bin");

module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug enabled or not");

module_param(closing_wait, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(closing_wait, "Maximum wait for data to drain, in .01 secs");

module_param(ignore_cpu_rev, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(ignore_cpu_rev,
			"Ignore the cpu revision when connecting to a device");

module_param(default_uart_mode, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(default_uart_mode, "Default uart_mode, 0=RS232, ...");
