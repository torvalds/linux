/*
 * USB ConnectTech WhiteHEAT driver
 *
 *	Copyright (C) 2002
 *	    Connect Tech Inc.
 *
 *	Copyright (C) 1999 - 2001
 *	    Greg Kroah-Hartman (greg@kroah.com)
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 * See Documentation/usb/usb-serial.txt for more information on using this
 * driver
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <asm/termbits.h>
#include <linux/usb.h>
#include <linux/serial_reg.h>
#include <linux/serial.h>
#include <linux/usb/serial.h>
#include <linux/firmware.h>
#include <linux/ihex.h>
#include "whiteheat.h"			/* WhiteHEAT specific commands */

static bool debug;

#ifndef CMSPAR
#define CMSPAR 0
#endif

/*
 * Version Information
 */
#define DRIVER_VERSION "v2.0"
#define DRIVER_AUTHOR "Greg Kroah-Hartman <greg@kroah.com>, Stuart MacDonald <stuartm@connecttech.com>"
#define DRIVER_DESC "USB ConnectTech WhiteHEAT driver"

#define CONNECT_TECH_VENDOR_ID		0x0710
#define CONNECT_TECH_FAKE_WHITE_HEAT_ID	0x0001
#define CONNECT_TECH_WHITE_HEAT_ID	0x8001

/*
   ID tables for whiteheat are unusual, because we want to different
   things for different versions of the device.  Eventually, this
   will be doable from a single table.  But, for now, we define two
   separate ID tables, and then a third table that combines them
   just for the purpose of exporting the autoloading information.
*/
static const struct usb_device_id id_table_std[] = {
	{ USB_DEVICE(CONNECT_TECH_VENDOR_ID, CONNECT_TECH_WHITE_HEAT_ID) },
	{ }						/* Terminating entry */
};

static const struct usb_device_id id_table_prerenumeration[] = {
	{ USB_DEVICE(CONNECT_TECH_VENDOR_ID, CONNECT_TECH_FAKE_WHITE_HEAT_ID) },
	{ }						/* Terminating entry */
};

static const struct usb_device_id id_table_combined[] = {
	{ USB_DEVICE(CONNECT_TECH_VENDOR_ID, CONNECT_TECH_WHITE_HEAT_ID) },
	{ USB_DEVICE(CONNECT_TECH_VENDOR_ID, CONNECT_TECH_FAKE_WHITE_HEAT_ID) },
	{ }						/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, id_table_combined);

static struct usb_driver whiteheat_driver = {
	.name =		"whiteheat",
	.probe =	usb_serial_probe,
	.disconnect =	usb_serial_disconnect,
	.id_table =	id_table_combined,
};

/* function prototypes for the Connect Tech WhiteHEAT prerenumeration device */
static int  whiteheat_firmware_download(struct usb_serial *serial,
					const struct usb_device_id *id);
static int  whiteheat_firmware_attach(struct usb_serial *serial);

/* function prototypes for the Connect Tech WhiteHEAT serial converter */
static int  whiteheat_attach(struct usb_serial *serial);
static void whiteheat_release(struct usb_serial *serial);
static int  whiteheat_open(struct tty_struct *tty,
			struct usb_serial_port *port);
static void whiteheat_close(struct usb_serial_port *port);
static int  whiteheat_write(struct tty_struct *tty,
			struct usb_serial_port *port,
			const unsigned char *buf, int count);
static int  whiteheat_write_room(struct tty_struct *tty);
static int  whiteheat_ioctl(struct tty_struct *tty,
			unsigned int cmd, unsigned long arg);
static void whiteheat_set_termios(struct tty_struct *tty,
			struct usb_serial_port *port, struct ktermios *old);
static int  whiteheat_tiocmget(struct tty_struct *tty);
static int  whiteheat_tiocmset(struct tty_struct *tty,
			unsigned int set, unsigned int clear);
static void whiteheat_break_ctl(struct tty_struct *tty, int break_state);
static int  whiteheat_chars_in_buffer(struct tty_struct *tty);
static void whiteheat_throttle(struct tty_struct *tty);
static void whiteheat_unthrottle(struct tty_struct *tty);
static void whiteheat_read_callback(struct urb *urb);
static void whiteheat_write_callback(struct urb *urb);

static struct usb_serial_driver whiteheat_fake_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"whiteheatnofirm",
	},
	.description =		"Connect Tech - WhiteHEAT - (prerenumeration)",
	.id_table =		id_table_prerenumeration,
	.num_ports =		1,
	.probe =		whiteheat_firmware_download,
	.attach =		whiteheat_firmware_attach,
};

static struct usb_serial_driver whiteheat_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"whiteheat",
	},
	.description =		"Connect Tech - WhiteHEAT",
	.id_table =		id_table_std,
	.num_ports =		4,
	.attach =		whiteheat_attach,
	.release =		whiteheat_release,
	.open =			whiteheat_open,
	.close =		whiteheat_close,
	.write =		whiteheat_write,
	.write_room =		whiteheat_write_room,
	.ioctl =		whiteheat_ioctl,
	.set_termios =		whiteheat_set_termios,
	.break_ctl =		whiteheat_break_ctl,
	.tiocmget =		whiteheat_tiocmget,
	.tiocmset =		whiteheat_tiocmset,
	.chars_in_buffer =	whiteheat_chars_in_buffer,
	.throttle =		whiteheat_throttle,
	.unthrottle =		whiteheat_unthrottle,
	.read_bulk_callback =	whiteheat_read_callback,
	.write_bulk_callback =	whiteheat_write_callback,
};

static struct usb_serial_driver * const serial_drivers[] = {
	&whiteheat_fake_device, &whiteheat_device, NULL
};

struct whiteheat_command_private {
	struct mutex		mutex;
	__u8			port_running;
	__u8			command_finished;
	wait_queue_head_t	wait_command; /* for handling sleeping whilst
						 waiting for a command to
						 finish */
	__u8			result_buffer[64];
};


#define THROTTLED		0x01
#define ACTUALLY_THROTTLED	0x02

static int urb_pool_size = 8;

struct whiteheat_urb_wrap {
	struct list_head	list;
	struct urb		*urb;
};

struct whiteheat_private {
	spinlock_t		lock;
	__u8			flags;
	__u8			mcr;		/* FIXME: no locking on mcr */
	struct list_head	rx_urbs_free;
	struct list_head	rx_urbs_submitted;
	struct list_head	rx_urb_q;
	struct work_struct	rx_work;
	struct usb_serial_port	*port;
	struct list_head	tx_urbs_free;
	struct list_head	tx_urbs_submitted;
	struct mutex		deathwarrant;
};


/* local function prototypes */
static int start_command_port(struct usb_serial *serial);
static void stop_command_port(struct usb_serial *serial);
static void command_port_write_callback(struct urb *urb);
static void command_port_read_callback(struct urb *urb);

static int start_port_read(struct usb_serial_port *port);
static struct whiteheat_urb_wrap *urb_to_wrap(struct urb *urb,
						struct list_head *head);
static struct list_head *list_first(struct list_head *head);
static void rx_data_softint(struct work_struct *work);

static int firm_send_command(struct usb_serial_port *port, __u8 command,
						__u8 *data, __u8 datasize);
static int firm_open(struct usb_serial_port *port);
static int firm_close(struct usb_serial_port *port);
static void firm_setup_port(struct tty_struct *tty);
static int firm_set_rts(struct usb_serial_port *port, __u8 onoff);
static int firm_set_dtr(struct usb_serial_port *port, __u8 onoff);
static int firm_set_break(struct usb_serial_port *port, __u8 onoff);
static int firm_purge(struct usb_serial_port *port, __u8 rxtx);
static int firm_get_dtr_rts(struct usb_serial_port *port);
static int firm_report_tx_done(struct usb_serial_port *port);


#define COMMAND_PORT		4
#define COMMAND_TIMEOUT		(2*HZ)	/* 2 second timeout for a command */
#define	COMMAND_TIMEOUT_MS	2000
#define CLOSING_DELAY		(30 * HZ)


/*****************************************************************************
 * Connect Tech's White Heat prerenumeration driver functions
 *****************************************************************************/

/* steps to download the firmware to the WhiteHEAT device:
 - hold the reset (by writing to the reset bit of the CPUCS register)
 - download the VEND_AX.HEX file to the chip using VENDOR_REQUEST-ANCHOR_LOAD
 - release the reset (by writing to the CPUCS register)
 - download the WH.HEX file for all addresses greater than 0x1b3f using
   VENDOR_REQUEST-ANCHOR_EXTERNAL_RAM_LOAD
 - hold the reset
 - download the WH.HEX file for all addresses less than 0x1b40 using
   VENDOR_REQUEST_ANCHOR_LOAD
 - release the reset
 - device renumerated itself and comes up as new device id with all
   firmware download completed.
*/
static int whiteheat_firmware_download(struct usb_serial *serial,
					const struct usb_device_id *id)
{
	int response, ret = -ENOENT;
	const struct firmware *loader_fw = NULL, *firmware_fw = NULL;
	const struct ihex_binrec *record;

	dbg("%s", __func__);

	if (request_ihex_firmware(&firmware_fw, "whiteheat.fw",
				  &serial->dev->dev)) {
		dev_err(&serial->dev->dev,
			"%s - request \"whiteheat.fw\" failed\n", __func__);
		goto out;
	}
	if (request_ihex_firmware(&loader_fw, "whiteheat_loader.fw",
			     &serial->dev->dev)) {
		dev_err(&serial->dev->dev,
			"%s - request \"whiteheat_loader.fw\" failed\n",
			__func__);
		goto out;
	}
	ret = 0;
	response = ezusb_set_reset (serial, 1);

	record = (const struct ihex_binrec *)loader_fw->data;
	while (record) {
		response = ezusb_writememory (serial, be32_to_cpu(record->addr),
					      (unsigned char *)record->data,
					      be16_to_cpu(record->len), 0xa0);
		if (response < 0) {
			dev_err(&serial->dev->dev, "%s - ezusb_writememory "
				"failed for loader (%d %04X %p %d)\n",
				__func__, response, be32_to_cpu(record->addr),
				record->data, be16_to_cpu(record->len));
			break;
		}
		record = ihex_next_binrec(record);
	}

	response = ezusb_set_reset(serial, 0);

	record = (const struct ihex_binrec *)firmware_fw->data;
	while (record && be32_to_cpu(record->addr) < 0x1b40)
		record = ihex_next_binrec(record);
	while (record) {
		response = ezusb_writememory (serial, be32_to_cpu(record->addr),
					      (unsigned char *)record->data,
					      be16_to_cpu(record->len), 0xa3);
		if (response < 0) {
			dev_err(&serial->dev->dev, "%s - ezusb_writememory "
				"failed for first firmware step "
				"(%d %04X %p %d)\n", __func__, response,
				be32_to_cpu(record->addr), record->data,
				be16_to_cpu(record->len));
			break;
		}
		++record;
	}

	response = ezusb_set_reset(serial, 1);

	record = (const struct ihex_binrec *)firmware_fw->data;
	while (record && be32_to_cpu(record->addr) < 0x1b40) {
		response = ezusb_writememory (serial, be32_to_cpu(record->addr),
					      (unsigned char *)record->data,
					      be16_to_cpu(record->len), 0xa0);
		if (response < 0) {
			dev_err(&serial->dev->dev, "%s - ezusb_writememory "
				"failed for second firmware step "
				"(%d %04X %p %d)\n", __func__, response,
				be32_to_cpu(record->addr), record->data,
				be16_to_cpu(record->len));
			break;
		}
		++record;
	}
	ret = 0;
	response = ezusb_set_reset (serial, 0);
 out:
	release_firmware(loader_fw);
	release_firmware(firmware_fw);
	return ret;
}


static int whiteheat_firmware_attach(struct usb_serial *serial)
{
	/* We want this device to fail to have a driver assigned to it */
	return 1;
}


/*****************************************************************************
 * Connect Tech's White Heat serial driver functions
 *****************************************************************************/
static int whiteheat_attach(struct usb_serial *serial)
{
	struct usb_serial_port *command_port;
	struct whiteheat_command_private *command_info;
	struct usb_serial_port *port;
	struct whiteheat_private *info;
	struct whiteheat_hw_info *hw_info;
	int pipe;
	int ret;
	int alen;
	__u8 *command;
	__u8 *result;
	int i;
	int j;
	struct urb *urb;
	int buf_size;
	struct whiteheat_urb_wrap *wrap;
	struct list_head *tmp;

	command_port = serial->port[COMMAND_PORT];

	pipe = usb_sndbulkpipe(serial->dev,
			command_port->bulk_out_endpointAddress);
	command = kmalloc(2, GFP_KERNEL);
	if (!command)
		goto no_command_buffer;
	command[0] = WHITEHEAT_GET_HW_INFO;
	command[1] = 0;

	result = kmalloc(sizeof(*hw_info) + 1, GFP_KERNEL);
	if (!result)
		goto no_result_buffer;
	/*
	 * When the module is reloaded the firmware is still there and
	 * the endpoints are still in the usb core unchanged. This is the
	 * unlinking bug in disguise. Same for the call below.
	 */
	usb_clear_halt(serial->dev, pipe);
	ret = usb_bulk_msg(serial->dev, pipe, command, 2,
						&alen, COMMAND_TIMEOUT_MS);
	if (ret) {
		dev_err(&serial->dev->dev, "%s: Couldn't send command [%d]\n",
			serial->type->description, ret);
		goto no_firmware;
	} else if (alen != 2) {
		dev_err(&serial->dev->dev, "%s: Send command incomplete [%d]\n",
			serial->type->description, alen);
		goto no_firmware;
	}

	pipe = usb_rcvbulkpipe(serial->dev,
				command_port->bulk_in_endpointAddress);
	/* See the comment on the usb_clear_halt() above */
	usb_clear_halt(serial->dev, pipe);
	ret = usb_bulk_msg(serial->dev, pipe, result,
			sizeof(*hw_info) + 1, &alen, COMMAND_TIMEOUT_MS);
	if (ret) {
		dev_err(&serial->dev->dev, "%s: Couldn't get results [%d]\n",
			serial->type->description, ret);
		goto no_firmware;
	} else if (alen != sizeof(*hw_info) + 1) {
		dev_err(&serial->dev->dev, "%s: Get results incomplete [%d]\n",
			serial->type->description, alen);
		goto no_firmware;
	} else if (result[0] != command[0]) {
		dev_err(&serial->dev->dev, "%s: Command failed [%d]\n",
			serial->type->description, result[0]);
		goto no_firmware;
	}

	hw_info = (struct whiteheat_hw_info *)&result[1];

	dev_info(&serial->dev->dev, "%s: Driver %s: Firmware v%d.%02d\n",
		 serial->type->description, DRIVER_VERSION,
		 hw_info->sw_major_rev, hw_info->sw_minor_rev);

	for (i = 0; i < serial->num_ports; i++) {
		port = serial->port[i];

		info = kmalloc(sizeof(struct whiteheat_private), GFP_KERNEL);
		if (info == NULL) {
			dev_err(&port->dev,
				"%s: Out of memory for port structures\n",
				serial->type->description);
			goto no_private;
		}

		spin_lock_init(&info->lock);
		mutex_init(&info->deathwarrant);
		info->flags = 0;
		info->mcr = 0;
		INIT_WORK(&info->rx_work, rx_data_softint);
		info->port = port;

		INIT_LIST_HEAD(&info->rx_urbs_free);
		INIT_LIST_HEAD(&info->rx_urbs_submitted);
		INIT_LIST_HEAD(&info->rx_urb_q);
		INIT_LIST_HEAD(&info->tx_urbs_free);
		INIT_LIST_HEAD(&info->tx_urbs_submitted);

		for (j = 0; j < urb_pool_size; j++) {
			urb = usb_alloc_urb(0, GFP_KERNEL);
			if (!urb) {
				dev_err(&port->dev, "No free urbs available\n");
				goto no_rx_urb;
			}
			buf_size = port->read_urb->transfer_buffer_length;
			urb->transfer_buffer = kmalloc(buf_size, GFP_KERNEL);
			if (!urb->transfer_buffer) {
				dev_err(&port->dev,
					"Couldn't allocate urb buffer\n");
				goto no_rx_buf;
			}
			wrap = kmalloc(sizeof(*wrap), GFP_KERNEL);
			if (!wrap) {
				dev_err(&port->dev,
					"Couldn't allocate urb wrapper\n");
				goto no_rx_wrap;
			}
			usb_fill_bulk_urb(urb, serial->dev,
					usb_rcvbulkpipe(serial->dev,
						port->bulk_in_endpointAddress),
					urb->transfer_buffer, buf_size,
					whiteheat_read_callback, port);
			wrap->urb = urb;
			list_add(&wrap->list, &info->rx_urbs_free);

			urb = usb_alloc_urb(0, GFP_KERNEL);
			if (!urb) {
				dev_err(&port->dev, "No free urbs available\n");
				goto no_tx_urb;
			}
			buf_size = port->write_urb->transfer_buffer_length;
			urb->transfer_buffer = kmalloc(buf_size, GFP_KERNEL);
			if (!urb->transfer_buffer) {
				dev_err(&port->dev,
					"Couldn't allocate urb buffer\n");
				goto no_tx_buf;
			}
			wrap = kmalloc(sizeof(*wrap), GFP_KERNEL);
			if (!wrap) {
				dev_err(&port->dev,
					"Couldn't allocate urb wrapper\n");
				goto no_tx_wrap;
			}
			usb_fill_bulk_urb(urb, serial->dev,
					usb_sndbulkpipe(serial->dev,
						port->bulk_out_endpointAddress),
					urb->transfer_buffer, buf_size,
					whiteheat_write_callback, port);
			wrap->urb = urb;
			list_add(&wrap->list, &info->tx_urbs_free);
		}

		usb_set_serial_port_data(port, info);
	}

	command_info = kmalloc(sizeof(struct whiteheat_command_private),
								GFP_KERNEL);
	if (command_info == NULL) {
		dev_err(&serial->dev->dev,
			"%s: Out of memory for port structures\n",
			serial->type->description);
		goto no_command_private;
	}

	mutex_init(&command_info->mutex);
	command_info->port_running = 0;
	init_waitqueue_head(&command_info->wait_command);
	usb_set_serial_port_data(command_port, command_info);
	command_port->write_urb->complete = command_port_write_callback;
	command_port->read_urb->complete = command_port_read_callback;
	kfree(result);
	kfree(command);

	return 0;

no_firmware:
	/* Firmware likely not running */
	dev_err(&serial->dev->dev,
		"%s: Unable to retrieve firmware version, try replugging\n",
		serial->type->description);
	dev_err(&serial->dev->dev,
		"%s: If the firmware is not running (status led not blinking)\n",
		serial->type->description);
	dev_err(&serial->dev->dev,
		"%s: please contact support@connecttech.com\n",
		serial->type->description);
	kfree(result);
	return -ENODEV;

no_command_private:
	for (i = serial->num_ports - 1; i >= 0; i--) {
		port = serial->port[i];
		info = usb_get_serial_port_data(port);
		for (j = urb_pool_size - 1; j >= 0; j--) {
			tmp = list_first(&info->tx_urbs_free);
			list_del(tmp);
			wrap = list_entry(tmp, struct whiteheat_urb_wrap, list);
			urb = wrap->urb;
			kfree(wrap);
no_tx_wrap:
			kfree(urb->transfer_buffer);
no_tx_buf:
			usb_free_urb(urb);
no_tx_urb:
			tmp = list_first(&info->rx_urbs_free);
			list_del(tmp);
			wrap = list_entry(tmp, struct whiteheat_urb_wrap, list);
			urb = wrap->urb;
			kfree(wrap);
no_rx_wrap:
			kfree(urb->transfer_buffer);
no_rx_buf:
			usb_free_urb(urb);
no_rx_urb:
			;
		}
		kfree(info);
no_private:
		;
	}
	kfree(result);
no_result_buffer:
	kfree(command);
no_command_buffer:
	return -ENOMEM;
}


static void whiteheat_release(struct usb_serial *serial)
{
	struct usb_serial_port *command_port;
	struct usb_serial_port *port;
	struct whiteheat_private *info;
	struct whiteheat_urb_wrap *wrap;
	struct urb *urb;
	struct list_head *tmp;
	struct list_head *tmp2;
	int i;

	dbg("%s", __func__);

	/* free up our private data for our command port */
	command_port = serial->port[COMMAND_PORT];
	kfree(usb_get_serial_port_data(command_port));

	for (i = 0; i < serial->num_ports; i++) {
		port = serial->port[i];
		info = usb_get_serial_port_data(port);
		list_for_each_safe(tmp, tmp2, &info->rx_urbs_free) {
			list_del(tmp);
			wrap = list_entry(tmp, struct whiteheat_urb_wrap, list);
			urb = wrap->urb;
			kfree(wrap);
			kfree(urb->transfer_buffer);
			usb_free_urb(urb);
		}
		list_for_each_safe(tmp, tmp2, &info->tx_urbs_free) {
			list_del(tmp);
			wrap = list_entry(tmp, struct whiteheat_urb_wrap, list);
			urb = wrap->urb;
			kfree(wrap);
			kfree(urb->transfer_buffer);
			usb_free_urb(urb);
		}
		kfree(info);
	}
}

static int whiteheat_open(struct tty_struct *tty, struct usb_serial_port *port)
{
	int		retval = 0;

	dbg("%s - port %d", __func__, port->number);

	retval = start_command_port(port->serial);
	if (retval)
		goto exit;

	if (tty)
		tty->low_latency = 1;

	/* send an open port command */
	retval = firm_open(port);
	if (retval) {
		stop_command_port(port->serial);
		goto exit;
	}

	retval = firm_purge(port, WHITEHEAT_PURGE_RX | WHITEHEAT_PURGE_TX);
	if (retval) {
		firm_close(port);
		stop_command_port(port->serial);
		goto exit;
	}

	if (tty)
		firm_setup_port(tty);

	/* Work around HCD bugs */
	usb_clear_halt(port->serial->dev, port->read_urb->pipe);
	usb_clear_halt(port->serial->dev, port->write_urb->pipe);

	/* Start reading from the device */
	retval = start_port_read(port);
	if (retval) {
		dev_err(&port->dev,
			"%s - failed submitting read urb, error %d\n",
			__func__, retval);
		firm_close(port);
		stop_command_port(port->serial);
		goto exit;
	}

exit:
	dbg("%s - exit, retval = %d", __func__, retval);
	return retval;
}


static void whiteheat_close(struct usb_serial_port *port)
{
	struct whiteheat_private *info = usb_get_serial_port_data(port);
	struct whiteheat_urb_wrap *wrap;
	struct urb *urb;
	struct list_head *tmp;
	struct list_head *tmp2;

	dbg("%s - port %d", __func__, port->number);

	firm_report_tx_done(port);
	firm_close(port);

	/* shutdown our bulk reads and writes */
	mutex_lock(&info->deathwarrant);
	spin_lock_irq(&info->lock);
	list_for_each_safe(tmp, tmp2, &info->rx_urbs_submitted) {
		wrap = list_entry(tmp, struct whiteheat_urb_wrap, list);
		urb = wrap->urb;
		list_del(tmp);
		spin_unlock_irq(&info->lock);
		usb_kill_urb(urb);
		spin_lock_irq(&info->lock);
		list_add(tmp, &info->rx_urbs_free);
	}
	list_for_each_safe(tmp, tmp2, &info->rx_urb_q)
		list_move(tmp, &info->rx_urbs_free);
	list_for_each_safe(tmp, tmp2, &info->tx_urbs_submitted) {
		wrap = list_entry(tmp, struct whiteheat_urb_wrap, list);
		urb = wrap->urb;
		list_del(tmp);
		spin_unlock_irq(&info->lock);
		usb_kill_urb(urb);
		spin_lock_irq(&info->lock);
		list_add(tmp, &info->tx_urbs_free);
	}
	spin_unlock_irq(&info->lock);
	mutex_unlock(&info->deathwarrant);
	stop_command_port(port->serial);
}


static int whiteheat_write(struct tty_struct *tty,
	struct usb_serial_port *port, const unsigned char *buf, int count)
{
	struct whiteheat_private *info = usb_get_serial_port_data(port);
	struct whiteheat_urb_wrap *wrap;
	struct urb *urb;
	int result;
	int bytes;
	int sent = 0;
	unsigned long flags;
	struct list_head *tmp;

	dbg("%s - port %d", __func__, port->number);

	if (count == 0) {
		dbg("%s - write request of 0 bytes", __func__);
		return (0);
	}

	while (count) {
		spin_lock_irqsave(&info->lock, flags);
		if (list_empty(&info->tx_urbs_free)) {
			spin_unlock_irqrestore(&info->lock, flags);
			break;
		}
		tmp = list_first(&info->tx_urbs_free);
		list_del(tmp);
		spin_unlock_irqrestore(&info->lock, flags);

		wrap = list_entry(tmp, struct whiteheat_urb_wrap, list);
		urb = wrap->urb;
		bytes = (count > port->bulk_out_size) ?
					port->bulk_out_size : count;
		memcpy(urb->transfer_buffer, buf + sent, bytes);

		usb_serial_debug_data(debug, &port->dev,
				__func__, bytes, urb->transfer_buffer);

		urb->transfer_buffer_length = bytes;
		result = usb_submit_urb(urb, GFP_ATOMIC);
		if (result) {
			dev_err_console(port,
				"%s - failed submitting write urb, error %d\n",
				__func__, result);
			sent = result;
			spin_lock_irqsave(&info->lock, flags);
			list_add(tmp, &info->tx_urbs_free);
			spin_unlock_irqrestore(&info->lock, flags);
			break;
		} else {
			sent += bytes;
			count -= bytes;
			spin_lock_irqsave(&info->lock, flags);
			list_add(tmp, &info->tx_urbs_submitted);
			spin_unlock_irqrestore(&info->lock, flags);
		}
	}

	return sent;
}

static int whiteheat_write_room(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct whiteheat_private *info = usb_get_serial_port_data(port);
	struct list_head *tmp;
	int room = 0;
	unsigned long flags;

	dbg("%s - port %d", __func__, port->number);

	spin_lock_irqsave(&info->lock, flags);
	list_for_each(tmp, &info->tx_urbs_free)
		room++;
	spin_unlock_irqrestore(&info->lock, flags);
	room *= port->bulk_out_size;

	dbg("%s - returns %d", __func__, room);
	return (room);
}

static int whiteheat_tiocmget(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct whiteheat_private *info = usb_get_serial_port_data(port);
	unsigned int modem_signals = 0;

	dbg("%s - port %d", __func__, port->number);

	firm_get_dtr_rts(port);
	if (info->mcr & UART_MCR_DTR)
		modem_signals |= TIOCM_DTR;
	if (info->mcr & UART_MCR_RTS)
		modem_signals |= TIOCM_RTS;

	return modem_signals;
}

static int whiteheat_tiocmset(struct tty_struct *tty,
			       unsigned int set, unsigned int clear)
{
	struct usb_serial_port *port = tty->driver_data;
	struct whiteheat_private *info = usb_get_serial_port_data(port);

	dbg("%s - port %d", __func__, port->number);

	if (set & TIOCM_RTS)
		info->mcr |= UART_MCR_RTS;
	if (set & TIOCM_DTR)
		info->mcr |= UART_MCR_DTR;

	if (clear & TIOCM_RTS)
		info->mcr &= ~UART_MCR_RTS;
	if (clear & TIOCM_DTR)
		info->mcr &= ~UART_MCR_DTR;

	firm_set_dtr(port, info->mcr & UART_MCR_DTR);
	firm_set_rts(port, info->mcr & UART_MCR_RTS);
	return 0;
}


static int whiteheat_ioctl(struct tty_struct *tty,
					unsigned int cmd, unsigned long arg)
{
	struct usb_serial_port *port = tty->driver_data;
	struct serial_struct serstruct;
	void __user *user_arg = (void __user *)arg;

	dbg("%s - port %d, cmd 0x%.4x", __func__, port->number, cmd);

	switch (cmd) {
	case TIOCGSERIAL:
		memset(&serstruct, 0, sizeof(serstruct));
		serstruct.type = PORT_16654;
		serstruct.line = port->serial->minor;
		serstruct.port = port->number;
		serstruct.flags = ASYNC_SKIP_TEST | ASYNC_AUTO_IRQ;
		serstruct.xmit_fifo_size = port->bulk_out_size;
		serstruct.custom_divisor = 0;
		serstruct.baud_base = 460800;
		serstruct.close_delay = CLOSING_DELAY;
		serstruct.closing_wait = CLOSING_DELAY;

		if (copy_to_user(user_arg, &serstruct, sizeof(serstruct)))
			return -EFAULT;
		break;
	default:
		break;
	}

	return -ENOIOCTLCMD;
}


static void whiteheat_set_termios(struct tty_struct *tty,
	struct usb_serial_port *port, struct ktermios *old_termios)
{
	firm_setup_port(tty);
}

static void whiteheat_break_ctl(struct tty_struct *tty, int break_state)
{
	struct usb_serial_port *port = tty->driver_data;
	firm_set_break(port, break_state);
}


static int whiteheat_chars_in_buffer(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct whiteheat_private *info = usb_get_serial_port_data(port);
	struct list_head *tmp;
	struct whiteheat_urb_wrap *wrap;
	int chars = 0;
	unsigned long flags;

	dbg("%s - port %d", __func__, port->number);

	spin_lock_irqsave(&info->lock, flags);
	list_for_each(tmp, &info->tx_urbs_submitted) {
		wrap = list_entry(tmp, struct whiteheat_urb_wrap, list);
		chars += wrap->urb->transfer_buffer_length;
	}
	spin_unlock_irqrestore(&info->lock, flags);

	dbg("%s - returns %d", __func__, chars);
	return chars;
}


static void whiteheat_throttle(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct whiteheat_private *info = usb_get_serial_port_data(port);

	dbg("%s - port %d", __func__, port->number);

	spin_lock_irq(&info->lock);
	info->flags |= THROTTLED;
	spin_unlock_irq(&info->lock);
}


static void whiteheat_unthrottle(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct whiteheat_private *info = usb_get_serial_port_data(port);
	int actually_throttled;

	dbg("%s - port %d", __func__, port->number);

	spin_lock_irq(&info->lock);
	actually_throttled = info->flags & ACTUALLY_THROTTLED;
	info->flags &= ~(THROTTLED | ACTUALLY_THROTTLED);
	spin_unlock_irq(&info->lock);

	if (actually_throttled)
		rx_data_softint(&info->rx_work);
}


/*****************************************************************************
 * Connect Tech's White Heat callback routines
 *****************************************************************************/
static void command_port_write_callback(struct urb *urb)
{
	int status = urb->status;

	dbg("%s", __func__);

	if (status) {
		dbg("nonzero urb status: %d", status);
		return;
	}
}


static void command_port_read_callback(struct urb *urb)
{
	struct usb_serial_port *command_port = urb->context;
	struct whiteheat_command_private *command_info;
	int status = urb->status;
	unsigned char *data = urb->transfer_buffer;
	int result;

	dbg("%s", __func__);

	command_info = usb_get_serial_port_data(command_port);
	if (!command_info) {
		dbg("%s - command_info is NULL, exiting.", __func__);
		return;
	}
	if (status) {
		dbg("%s - nonzero urb status: %d", __func__, status);
		if (status != -ENOENT)
			command_info->command_finished = WHITEHEAT_CMD_FAILURE;
		wake_up(&command_info->wait_command);
		return;
	}

	usb_serial_debug_data(debug, &command_port->dev,
				__func__, urb->actual_length, data);

	if (data[0] == WHITEHEAT_CMD_COMPLETE) {
		command_info->command_finished = WHITEHEAT_CMD_COMPLETE;
		wake_up(&command_info->wait_command);
	} else if (data[0] == WHITEHEAT_CMD_FAILURE) {
		command_info->command_finished = WHITEHEAT_CMD_FAILURE;
		wake_up(&command_info->wait_command);
	} else if (data[0] == WHITEHEAT_EVENT) {
		/* These are unsolicited reports from the firmware, hence no
		   waiting command to wakeup */
		dbg("%s - event received", __func__);
	} else if (data[0] == WHITEHEAT_GET_DTR_RTS) {
		memcpy(command_info->result_buffer, &data[1],
						urb->actual_length - 1);
		command_info->command_finished = WHITEHEAT_CMD_COMPLETE;
		wake_up(&command_info->wait_command);
	} else
		dbg("%s - bad reply from firmware", __func__);

	/* Continue trying to always read */
	result = usb_submit_urb(command_port->read_urb, GFP_ATOMIC);
	if (result)
		dbg("%s - failed resubmitting read urb, error %d",
			__func__, result);
}


static void whiteheat_read_callback(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	struct whiteheat_urb_wrap *wrap;
	unsigned char *data = urb->transfer_buffer;
	struct whiteheat_private *info = usb_get_serial_port_data(port);
	int status = urb->status;

	dbg("%s - port %d", __func__, port->number);

	spin_lock(&info->lock);
	wrap = urb_to_wrap(urb, &info->rx_urbs_submitted);
	if (!wrap) {
		spin_unlock(&info->lock);
		dev_err(&port->dev, "%s - Not my urb!\n", __func__);
		return;
	}
	list_del(&wrap->list);
	spin_unlock(&info->lock);

	if (status) {
		dbg("%s - nonzero read bulk status received: %d",
		    __func__, status);
		spin_lock(&info->lock);
		list_add(&wrap->list, &info->rx_urbs_free);
		spin_unlock(&info->lock);
		return;
	}

	usb_serial_debug_data(debug, &port->dev,
				__func__, urb->actual_length, data);

	spin_lock(&info->lock);
	list_add_tail(&wrap->list, &info->rx_urb_q);
	if (info->flags & THROTTLED) {
		info->flags |= ACTUALLY_THROTTLED;
		spin_unlock(&info->lock);
		return;
	}
	spin_unlock(&info->lock);

	schedule_work(&info->rx_work);
}


static void whiteheat_write_callback(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	struct whiteheat_private *info = usb_get_serial_port_data(port);
	struct whiteheat_urb_wrap *wrap;
	int status = urb->status;

	dbg("%s - port %d", __func__, port->number);

	spin_lock(&info->lock);
	wrap = urb_to_wrap(urb, &info->tx_urbs_submitted);
	if (!wrap) {
		spin_unlock(&info->lock);
		dev_err(&port->dev, "%s - Not my urb!\n", __func__);
		return;
	}
	list_move(&wrap->list, &info->tx_urbs_free);
	spin_unlock(&info->lock);

	if (status) {
		dbg("%s - nonzero write bulk status received: %d",
		    __func__, status);
		return;
	}

	usb_serial_port_softint(port);
}


/*****************************************************************************
 * Connect Tech's White Heat firmware interface
 *****************************************************************************/
static int firm_send_command(struct usb_serial_port *port, __u8 command,
						__u8 *data, __u8 datasize)
{
	struct usb_serial_port *command_port;
	struct whiteheat_command_private *command_info;
	struct whiteheat_private *info;
	__u8 *transfer_buffer;
	int retval = 0;
	int t;

	dbg("%s - command %d", __func__, command);

	command_port = port->serial->port[COMMAND_PORT];
	command_info = usb_get_serial_port_data(command_port);
	mutex_lock(&command_info->mutex);
	command_info->command_finished = false;

	transfer_buffer = (__u8 *)command_port->write_urb->transfer_buffer;
	transfer_buffer[0] = command;
	memcpy(&transfer_buffer[1], data, datasize);
	command_port->write_urb->transfer_buffer_length = datasize + 1;
	retval = usb_submit_urb(command_port->write_urb, GFP_NOIO);
	if (retval) {
		dbg("%s - submit urb failed", __func__);
		goto exit;
	}

	/* wait for the command to complete */
	t = wait_event_timeout(command_info->wait_command,
		(bool)command_info->command_finished, COMMAND_TIMEOUT);
	if (!t)
		usb_kill_urb(command_port->write_urb);

	if (command_info->command_finished == false) {
		dbg("%s - command timed out.", __func__);
		retval = -ETIMEDOUT;
		goto exit;
	}

	if (command_info->command_finished == WHITEHEAT_CMD_FAILURE) {
		dbg("%s - command failed.", __func__);
		retval = -EIO;
		goto exit;
	}

	if (command_info->command_finished == WHITEHEAT_CMD_COMPLETE) {
		dbg("%s - command completed.", __func__);
		switch (command) {
		case WHITEHEAT_GET_DTR_RTS:
			info = usb_get_serial_port_data(port);
			memcpy(&info->mcr, command_info->result_buffer,
					sizeof(struct whiteheat_dr_info));
				break;
		}
	}
exit:
	mutex_unlock(&command_info->mutex);
	return retval;
}


static int firm_open(struct usb_serial_port *port)
{
	struct whiteheat_simple open_command;

	open_command.port = port->number - port->serial->minor + 1;
	return firm_send_command(port, WHITEHEAT_OPEN,
		(__u8 *)&open_command, sizeof(open_command));
}


static int firm_close(struct usb_serial_port *port)
{
	struct whiteheat_simple close_command;

	close_command.port = port->number - port->serial->minor + 1;
	return firm_send_command(port, WHITEHEAT_CLOSE,
			(__u8 *)&close_command, sizeof(close_command));
}


static void firm_setup_port(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct whiteheat_port_settings port_settings;
	unsigned int cflag = tty->termios->c_cflag;

	port_settings.port = port->number + 1;

	/* get the byte size */
	switch (cflag & CSIZE) {
	case CS5:	port_settings.bits = 5;   break;
	case CS6:	port_settings.bits = 6;   break;
	case CS7:	port_settings.bits = 7;   break;
	default:
	case CS8:	port_settings.bits = 8;   break;
	}
	dbg("%s - data bits = %d", __func__, port_settings.bits);

	/* determine the parity */
	if (cflag & PARENB)
		if (cflag & CMSPAR)
			if (cflag & PARODD)
				port_settings.parity = WHITEHEAT_PAR_MARK;
			else
				port_settings.parity = WHITEHEAT_PAR_SPACE;
		else
			if (cflag & PARODD)
				port_settings.parity = WHITEHEAT_PAR_ODD;
			else
				port_settings.parity = WHITEHEAT_PAR_EVEN;
	else
		port_settings.parity = WHITEHEAT_PAR_NONE;
	dbg("%s - parity = %c", __func__, port_settings.parity);

	/* figure out the stop bits requested */
	if (cflag & CSTOPB)
		port_settings.stop = 2;
	else
		port_settings.stop = 1;
	dbg("%s - stop bits = %d", __func__, port_settings.stop);

	/* figure out the flow control settings */
	if (cflag & CRTSCTS)
		port_settings.hflow = (WHITEHEAT_HFLOW_CTS |
						WHITEHEAT_HFLOW_RTS);
	else
		port_settings.hflow = WHITEHEAT_HFLOW_NONE;
	dbg("%s - hardware flow control = %s %s %s %s", __func__,
	    (port_settings.hflow & WHITEHEAT_HFLOW_CTS) ? "CTS" : "",
	    (port_settings.hflow & WHITEHEAT_HFLOW_RTS) ? "RTS" : "",
	    (port_settings.hflow & WHITEHEAT_HFLOW_DSR) ? "DSR" : "",
	    (port_settings.hflow & WHITEHEAT_HFLOW_DTR) ? "DTR" : "");

	/* determine software flow control */
	if (I_IXOFF(tty))
		port_settings.sflow = WHITEHEAT_SFLOW_RXTX;
	else
		port_settings.sflow = WHITEHEAT_SFLOW_NONE;
	dbg("%s - software flow control = %c", __func__, port_settings.sflow);

	port_settings.xon = START_CHAR(tty);
	port_settings.xoff = STOP_CHAR(tty);
	dbg("%s - XON = %2x, XOFF = %2x",
			__func__, port_settings.xon, port_settings.xoff);

	/* get the baud rate wanted */
	port_settings.baud = tty_get_baud_rate(tty);
	dbg("%s - baud rate = %d", __func__, port_settings.baud);

	/* fixme: should set validated settings */
	tty_encode_baud_rate(tty, port_settings.baud, port_settings.baud);
	/* handle any settings that aren't specified in the tty structure */
	port_settings.lloop = 0;

	/* now send the message to the device */
	firm_send_command(port, WHITEHEAT_SETUP_PORT,
			(__u8 *)&port_settings, sizeof(port_settings));
}


static int firm_set_rts(struct usb_serial_port *port, __u8 onoff)
{
	struct whiteheat_set_rdb rts_command;

	rts_command.port = port->number - port->serial->minor + 1;
	rts_command.state = onoff;
	return firm_send_command(port, WHITEHEAT_SET_RTS,
			(__u8 *)&rts_command, sizeof(rts_command));
}


static int firm_set_dtr(struct usb_serial_port *port, __u8 onoff)
{
	struct whiteheat_set_rdb dtr_command;

	dtr_command.port = port->number - port->serial->minor + 1;
	dtr_command.state = onoff;
	return firm_send_command(port, WHITEHEAT_SET_DTR,
			(__u8 *)&dtr_command, sizeof(dtr_command));
}


static int firm_set_break(struct usb_serial_port *port, __u8 onoff)
{
	struct whiteheat_set_rdb break_command;

	break_command.port = port->number - port->serial->minor + 1;
	break_command.state = onoff;
	return firm_send_command(port, WHITEHEAT_SET_BREAK,
			(__u8 *)&break_command, sizeof(break_command));
}


static int firm_purge(struct usb_serial_port *port, __u8 rxtx)
{
	struct whiteheat_purge purge_command;

	purge_command.port = port->number - port->serial->minor + 1;
	purge_command.what = rxtx;
	return firm_send_command(port, WHITEHEAT_PURGE,
			(__u8 *)&purge_command, sizeof(purge_command));
}


static int firm_get_dtr_rts(struct usb_serial_port *port)
{
	struct whiteheat_simple get_dr_command;

	get_dr_command.port = port->number - port->serial->minor + 1;
	return firm_send_command(port, WHITEHEAT_GET_DTR_RTS,
			(__u8 *)&get_dr_command, sizeof(get_dr_command));
}


static int firm_report_tx_done(struct usb_serial_port *port)
{
	struct whiteheat_simple close_command;

	close_command.port = port->number - port->serial->minor + 1;
	return firm_send_command(port, WHITEHEAT_REPORT_TX_DONE,
			(__u8 *)&close_command, sizeof(close_command));
}


/*****************************************************************************
 * Connect Tech's White Heat utility functions
 *****************************************************************************/
static int start_command_port(struct usb_serial *serial)
{
	struct usb_serial_port *command_port;
	struct whiteheat_command_private *command_info;
	int retval = 0;

	command_port = serial->port[COMMAND_PORT];
	command_info = usb_get_serial_port_data(command_port);
	mutex_lock(&command_info->mutex);
	if (!command_info->port_running) {
		/* Work around HCD bugs */
		usb_clear_halt(serial->dev, command_port->read_urb->pipe);

		retval = usb_submit_urb(command_port->read_urb, GFP_KERNEL);
		if (retval) {
			dev_err(&serial->dev->dev,
				"%s - failed submitting read urb, error %d\n",
				__func__, retval);
			goto exit;
		}
	}
	command_info->port_running++;

exit:
	mutex_unlock(&command_info->mutex);
	return retval;
}


static void stop_command_port(struct usb_serial *serial)
{
	struct usb_serial_port *command_port;
	struct whiteheat_command_private *command_info;

	command_port = serial->port[COMMAND_PORT];
	command_info = usb_get_serial_port_data(command_port);
	mutex_lock(&command_info->mutex);
	command_info->port_running--;
	if (!command_info->port_running)
		usb_kill_urb(command_port->read_urb);
	mutex_unlock(&command_info->mutex);
}


static int start_port_read(struct usb_serial_port *port)
{
	struct whiteheat_private *info = usb_get_serial_port_data(port);
	struct whiteheat_urb_wrap *wrap;
	struct urb *urb;
	int retval = 0;
	unsigned long flags;
	struct list_head *tmp;
	struct list_head *tmp2;

	spin_lock_irqsave(&info->lock, flags);

	list_for_each_safe(tmp, tmp2, &info->rx_urbs_free) {
		list_del(tmp);
		wrap = list_entry(tmp, struct whiteheat_urb_wrap, list);
		urb = wrap->urb;
		spin_unlock_irqrestore(&info->lock, flags);
		retval = usb_submit_urb(urb, GFP_KERNEL);
		if (retval) {
			spin_lock_irqsave(&info->lock, flags);
			list_add(tmp, &info->rx_urbs_free);
			list_for_each_safe(tmp, tmp2, &info->rx_urbs_submitted) {
				wrap = list_entry(tmp, struct whiteheat_urb_wrap, list);
				urb = wrap->urb;
				list_del(tmp);
				spin_unlock_irqrestore(&info->lock, flags);
				usb_kill_urb(urb);
				spin_lock_irqsave(&info->lock, flags);
				list_add(tmp, &info->rx_urbs_free);
			}
			break;
		}
		spin_lock_irqsave(&info->lock, flags);
		list_add(tmp, &info->rx_urbs_submitted);
	}

	spin_unlock_irqrestore(&info->lock, flags);

	return retval;
}


static struct whiteheat_urb_wrap *urb_to_wrap(struct urb *urb,
						struct list_head *head)
{
	struct whiteheat_urb_wrap *wrap;
	struct list_head *tmp;

	list_for_each(tmp, head) {
		wrap = list_entry(tmp, struct whiteheat_urb_wrap, list);
		if (wrap->urb == urb)
			return wrap;
	}

	return NULL;
}


static struct list_head *list_first(struct list_head *head)
{
	return head->next;
}


static void rx_data_softint(struct work_struct *work)
{
	struct whiteheat_private *info =
		container_of(work, struct whiteheat_private, rx_work);
	struct usb_serial_port *port = info->port;
	struct tty_struct *tty = tty_port_tty_get(&port->port);
	struct whiteheat_urb_wrap *wrap;
	struct urb *urb;
	unsigned long flags;
	struct list_head *tmp;
	struct list_head *tmp2;
	int result;
	int sent = 0;

	spin_lock_irqsave(&info->lock, flags);
	if (info->flags & THROTTLED) {
		spin_unlock_irqrestore(&info->lock, flags);
		goto out;
	}

	list_for_each_safe(tmp, tmp2, &info->rx_urb_q) {
		list_del(tmp);
		spin_unlock_irqrestore(&info->lock, flags);

		wrap = list_entry(tmp, struct whiteheat_urb_wrap, list);
		urb = wrap->urb;

		if (tty && urb->actual_length)
			sent += tty_insert_flip_string(tty,
				urb->transfer_buffer, urb->actual_length);

		result = usb_submit_urb(urb, GFP_ATOMIC);
		if (result) {
			dev_err(&port->dev,
				"%s - failed resubmitting read urb, error %d\n",
				__func__, result);
			spin_lock_irqsave(&info->lock, flags);
			list_add(tmp, &info->rx_urbs_free);
			continue;
		}

		spin_lock_irqsave(&info->lock, flags);
		list_add(tmp, &info->rx_urbs_submitted);
	}
	spin_unlock_irqrestore(&info->lock, flags);

	if (sent)
		tty_flip_buffer_push(tty);
out:
	tty_kref_put(tty);
}

module_usb_serial_driver(whiteheat_driver, serial_drivers);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

MODULE_FIRMWARE("whiteheat.fw");
MODULE_FIRMWARE("whiteheat_loader.fw");

module_param(urb_pool_size, int, 0);
MODULE_PARM_DESC(urb_pool_size, "Number of urbs to use for buffering");

module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug enabled or not");
