/* vi: ts=8 sw=8
 *
 * TI 3410/5052 USB Serial Driver
 *
 * Copyright (C) 2004 Texas Instruments
 *
 * This driver is based on the Linux io_ti driver, which is
 *   Copyright (C) 2000-2002 Inside Out Networks
 *   Copyright (C) 2001-2002 Greg Kroah-Hartman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * For questions or problems with this driver, contact Texas Instruments
 * technical support, or Al Borchers <alborchers@steinerpoint.com>, or
 * Peter Berger <pberger@brimson.com>.
 * 
 * This driver needs this hotplug script in /etc/hotplug/usb/ti_usb_3410_5052
 * or in /etc/hotplug.d/usb/ti_usb_3410_5052.hotplug to set the device
 * configuration.
 *
 * #!/bin/bash
 *
 * BOOT_CONFIG=1
 * ACTIVE_CONFIG=2
 *
 * if [[ "$ACTION" != "add" ]]
 * then
 * 	exit
 * fi
 *
 * CONFIG_PATH=/sys${DEVPATH%/?*}/bConfigurationValue
 *
 * if [[ 0`cat $CONFIG_PATH` -ne $BOOT_CONFIG ]]
 * then
 * 	exit
 * fi
 *
 * PRODUCT=${PRODUCT%/?*}		# delete version
 * VENDOR_ID=`printf "%d" 0x${PRODUCT%/?*}`
 * PRODUCT_ID=`printf "%d" 0x${PRODUCT#*?/}`
 *
 * PARAM_PATH=/sys/module/ti_usb_3410_5052/parameters
 *
 * function scan() {
 * 	s=$1
 * 	shift
 * 	for i
 * 	do
 * 		if [[ $s -eq $i ]]
 * 		then
 * 			return 0
 * 		fi
 * 	done
 * 	return 1
 * }
 *
 * IFS=$IFS,
 *
 * if (scan $VENDOR_ID 1105 `cat $PARAM_PATH/vendor_3410` &&
 * scan $PRODUCT_ID 13328 `cat $PARAM_PATH/product_3410`) ||
 * (scan $VENDOR_ID 1105 `cat $PARAM_PATH/vendor_5052` &&
 * scan $PRODUCT_ID 20562 20818 20570 20575 `cat $PARAM_PATH/product_5052`)
 * then
 * 	echo $ACTIVE_CONFIG > $CONFIG_PATH
 * fi
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
#include <linux/ioctl.h>
#include <linux/serial.h>
#include <linux/circ_buf.h>
#include <asm/uaccess.h>
#include <asm/semaphore.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>

#include "ti_usb_3410_5052.h"
#include "ti_fw_3410.h"		/* firmware image for 3410 */
#include "ti_fw_5052.h"		/* firmware image for 5052 */


/* Defines */

#define TI_DRIVER_VERSION	"v0.9"
#define TI_DRIVER_AUTHOR	"Al Borchers <alborchers@steinerpoint.com>"
#define TI_DRIVER_DESC		"TI USB 3410/5052 Serial Driver"

#define TI_FIRMWARE_BUF_SIZE	16284

#define TI_WRITE_BUF_SIZE	1024

#define TI_TRANSFER_TIMEOUT	2

#define TI_DEFAULT_LOW_LATENCY	0
#define TI_DEFAULT_CLOSING_WAIT	4000		/* in .01 secs */

/* supported setserial flags */
#define TI_SET_SERIAL_FLAGS	(ASYNC_LOW_LATENCY)

/* read urb states */
#define TI_READ_URB_RUNNING	0
#define TI_READ_URB_STOPPING	1
#define TI_READ_URB_STOPPED	2

#define TI_EXTRA_VID_PID_COUNT	5


/* Structures */

struct ti_port {
	int			tp_is_open;
	__u8			tp_msr;
	__u8			tp_lsr;
	__u8			tp_shadow_mcr;
	__u8			tp_uart_mode;	/* 232 or 485 modes */
	unsigned int		tp_uart_base_addr;
	int			tp_flags;
	int			tp_closing_wait;/* in .01 secs */
	struct async_icount	tp_icount;
	wait_queue_head_t	tp_msr_wait;	/* wait for msr change */
	wait_queue_head_t	tp_write_wait;
	struct ti_device	*tp_tdev;
	struct usb_serial_port	*tp_port;
	spinlock_t		tp_lock;
	int			tp_read_urb_state;
	int			tp_write_urb_in_use;
	struct circ_buf		*tp_write_buf;
};

struct ti_device {
	struct semaphore	td_open_close_sem;
	int			td_open_port_count;
	struct usb_serial	*td_serial;
	int			td_is_3410;
	int			td_urb_error;
};


/* Function Declarations */

static int ti_startup(struct usb_serial *serial);
static void ti_shutdown(struct usb_serial *serial);
static int ti_open(struct usb_serial_port *port, struct file *file);
static void ti_close(struct usb_serial_port *port, struct file *file);
static int ti_write(struct usb_serial_port *port, const unsigned char *data,
	int count);
static int ti_write_room(struct usb_serial_port *port);
static int ti_chars_in_buffer(struct usb_serial_port *port);
static void ti_throttle(struct usb_serial_port *port);
static void ti_unthrottle(struct usb_serial_port *port);
static int ti_ioctl(struct usb_serial_port *port, struct file *file, unsigned int cmd, unsigned long arg);
static void ti_set_termios(struct usb_serial_port *port,
	struct ktermios *old_termios);
static int ti_tiocmget(struct usb_serial_port *port, struct file *file);
static int ti_tiocmset(struct usb_serial_port *port, struct file *file,
	unsigned int set, unsigned int clear);
static void ti_break(struct usb_serial_port *port, int break_state);
static void ti_interrupt_callback(struct urb *urb);
static void ti_bulk_in_callback(struct urb *urb);
static void ti_bulk_out_callback(struct urb *urb);

static void ti_recv(struct device *dev, struct tty_struct *tty,
	unsigned char *data, int length);
static void ti_send(struct ti_port *tport);
static int ti_set_mcr(struct ti_port *tport, unsigned int mcr);
static int ti_get_lsr(struct ti_port *tport);
static int ti_get_serial_info(struct ti_port *tport,
	struct serial_struct __user *ret_arg);
static int ti_set_serial_info(struct ti_port *tport,
	struct serial_struct __user *new_arg);
static void ti_handle_new_msr(struct ti_port *tport, __u8 msr);

static void ti_drain(struct ti_port *tport, unsigned long timeout, int flush);

static void ti_stop_read(struct ti_port *tport, struct tty_struct *tty);
static int ti_restart_read(struct ti_port *tport, struct tty_struct *tty);

static int ti_command_out_sync(struct ti_device *tdev, __u8 command,
	__u16 moduleid, __u16 value, __u8 *data, int size);
static int ti_command_in_sync(struct ti_device *tdev, __u8 command,
	__u16 moduleid, __u16 value, __u8 *data, int size);

static int ti_write_byte(struct ti_device *tdev, unsigned long addr,
	__u8 mask, __u8 byte);

static int ti_download_firmware(struct ti_device *tdev,
	unsigned char *firmware, unsigned int firmware_size);

/* circular buffer */
static struct circ_buf *ti_buf_alloc(void);
static void ti_buf_free(struct circ_buf *cb);
static void ti_buf_clear(struct circ_buf *cb);
static int ti_buf_data_avail(struct circ_buf *cb);
static int ti_buf_space_avail(struct circ_buf *cb);
static int ti_buf_put(struct circ_buf *cb, const char *buf, int count);
static int ti_buf_get(struct circ_buf *cb, char *buf, int count);


/* Data */

/* module parameters */
static int debug;
static int low_latency = TI_DEFAULT_LOW_LATENCY;
static int closing_wait = TI_DEFAULT_CLOSING_WAIT;
static ushort vendor_3410[TI_EXTRA_VID_PID_COUNT];
static int vendor_3410_count;
static ushort product_3410[TI_EXTRA_VID_PID_COUNT];
static int product_3410_count;
static ushort vendor_5052[TI_EXTRA_VID_PID_COUNT];
static int vendor_5052_count;
static ushort product_5052[TI_EXTRA_VID_PID_COUNT];
static int product_5052_count;

/* supported devices */
/* the array dimension is the number of default entries plus */
/* TI_EXTRA_VID_PID_COUNT user defined entries plus 1 terminating */
/* null entry */
static struct usb_device_id ti_id_table_3410[1+TI_EXTRA_VID_PID_COUNT+1] = {
	{ USB_DEVICE(TI_VENDOR_ID, TI_3410_PRODUCT_ID) },
	{ USB_DEVICE(TI_VENDOR_ID, TI_3410_EZ430_ID) },
};

static struct usb_device_id ti_id_table_5052[4+TI_EXTRA_VID_PID_COUNT+1] = {
	{ USB_DEVICE(TI_VENDOR_ID, TI_5052_BOOT_PRODUCT_ID) },
	{ USB_DEVICE(TI_VENDOR_ID, TI_5152_BOOT_PRODUCT_ID) },
	{ USB_DEVICE(TI_VENDOR_ID, TI_5052_EEPROM_PRODUCT_ID) },
	{ USB_DEVICE(TI_VENDOR_ID, TI_5052_FIRMWARE_PRODUCT_ID) },
};

static struct usb_device_id ti_id_table_combined[] = {
	{ USB_DEVICE(TI_VENDOR_ID, TI_3410_PRODUCT_ID) },
	{ USB_DEVICE(TI_VENDOR_ID, TI_3410_EZ430_ID) },
	{ USB_DEVICE(TI_VENDOR_ID, TI_5052_BOOT_PRODUCT_ID) },
	{ USB_DEVICE(TI_VENDOR_ID, TI_5152_BOOT_PRODUCT_ID) },
	{ USB_DEVICE(TI_VENDOR_ID, TI_5052_EEPROM_PRODUCT_ID) },
	{ USB_DEVICE(TI_VENDOR_ID, TI_5052_FIRMWARE_PRODUCT_ID) },
	{ }
};

static struct usb_driver ti_usb_driver = {
	.name			= "ti_usb_3410_5052",
	.probe			= usb_serial_probe,
	.disconnect		= usb_serial_disconnect,
	.id_table		= ti_id_table_combined,
	.no_dynamic_id = 	1,
};

static struct usb_serial_driver ti_1port_device = {
	.driver = {
		.owner		= THIS_MODULE,
		.name		= "ti_usb_3410_5052_1",
	},
	.description		= "TI USB 3410 1 port adapter",
	.usb_driver		= &ti_usb_driver,
	.id_table		= ti_id_table_3410,
	.num_interrupt_in	= 1,
	.num_bulk_in		= 1,
	.num_bulk_out		= 1,
	.num_ports		= 1,
	.attach			= ti_startup,
	.shutdown		= ti_shutdown,
	.open			= ti_open,
	.close			= ti_close,
	.write			= ti_write,
	.write_room		= ti_write_room,
	.chars_in_buffer	= ti_chars_in_buffer,
	.throttle		= ti_throttle,
	.unthrottle		= ti_unthrottle,
	.ioctl			= ti_ioctl,
	.set_termios		= ti_set_termios,
	.tiocmget		= ti_tiocmget,
	.tiocmset		= ti_tiocmset,
	.break_ctl		= ti_break,
	.read_int_callback	= ti_interrupt_callback,
	.read_bulk_callback	= ti_bulk_in_callback,
	.write_bulk_callback	= ti_bulk_out_callback,
};

static struct usb_serial_driver ti_2port_device = {
	.driver = {
		.owner		= THIS_MODULE,
		.name		= "ti_usb_3410_5052_2",
	},
	.description		= "TI USB 5052 2 port adapter",
	.usb_driver		= &ti_usb_driver,
	.id_table		= ti_id_table_5052,
	.num_interrupt_in	= 1,
	.num_bulk_in		= 2,
	.num_bulk_out		= 2,
	.num_ports		= 2,
	.attach			= ti_startup,
	.shutdown		= ti_shutdown,
	.open			= ti_open,
	.close			= ti_close,
	.write			= ti_write,
	.write_room		= ti_write_room,
	.chars_in_buffer	= ti_chars_in_buffer,
	.throttle		= ti_throttle,
	.unthrottle		= ti_unthrottle,
	.ioctl			= ti_ioctl,
	.set_termios		= ti_set_termios,
	.tiocmget		= ti_tiocmget,
	.tiocmset		= ti_tiocmset,
	.break_ctl		= ti_break,
	.read_int_callback	= ti_interrupt_callback,
	.read_bulk_callback	= ti_bulk_in_callback,
	.write_bulk_callback	= ti_bulk_out_callback,
};


/* Module */

MODULE_AUTHOR(TI_DRIVER_AUTHOR);
MODULE_DESCRIPTION(TI_DRIVER_DESC);
MODULE_VERSION(TI_DRIVER_VERSION);
MODULE_LICENSE("GPL");

module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Enable debugging, 0=no, 1=yes");

module_param(low_latency, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(low_latency, "TTY low_latency flag, 0=off, 1=on, default is off");

module_param(closing_wait, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(closing_wait, "Maximum wait for data to drain in close, in .01 secs, default is 4000");

module_param_array(vendor_3410, ushort, &vendor_3410_count, S_IRUGO);
MODULE_PARM_DESC(vendor_3410, "Vendor ids for 3410 based devices, 1-5 short integers");
module_param_array(product_3410, ushort, &product_3410_count, S_IRUGO);
MODULE_PARM_DESC(product_3410, "Product ids for 3410 based devices, 1-5 short integers");
module_param_array(vendor_5052, ushort, &vendor_5052_count, S_IRUGO);
MODULE_PARM_DESC(vendor_5052, "Vendor ids for 5052 based devices, 1-5 short integers");
module_param_array(product_5052, ushort, &product_5052_count, S_IRUGO);
MODULE_PARM_DESC(product_5052, "Product ids for 5052 based devices, 1-5 short integers");

MODULE_DEVICE_TABLE(usb, ti_id_table_combined);


/* Functions */

static int __init ti_init(void)
{
	int i,j;
	int ret;

	/* insert extra vendor and product ids */
	j = ARRAY_SIZE(ti_id_table_3410) - TI_EXTRA_VID_PID_COUNT - 1;
	for (i=0; i<min(vendor_3410_count,product_3410_count); i++,j++) {
		ti_id_table_3410[j].idVendor = vendor_3410[i];
		ti_id_table_3410[j].idProduct = product_3410[i];
		ti_id_table_3410[j].match_flags = USB_DEVICE_ID_MATCH_DEVICE;
	}
	j = ARRAY_SIZE(ti_id_table_5052) - TI_EXTRA_VID_PID_COUNT - 1;
	for (i=0; i<min(vendor_5052_count,product_5052_count); i++,j++) {
		ti_id_table_5052[j].idVendor = vendor_5052[i];
		ti_id_table_5052[j].idProduct = product_5052[i];
		ti_id_table_5052[j].match_flags = USB_DEVICE_ID_MATCH_DEVICE;
	}

	ret = usb_serial_register(&ti_1port_device);
	if (ret)
		goto failed_1port;
	ret = usb_serial_register(&ti_2port_device);
	if (ret)
		goto failed_2port;

	ret = usb_register(&ti_usb_driver);
	if (ret)
		goto failed_usb;

	info(TI_DRIVER_DESC " " TI_DRIVER_VERSION);

	return 0;

failed_usb:
	usb_serial_deregister(&ti_2port_device);
failed_2port:
	usb_serial_deregister(&ti_1port_device);
failed_1port:
	return ret;
}


static void __exit ti_exit(void)
{
	usb_serial_deregister(&ti_1port_device);
	usb_serial_deregister(&ti_2port_device);
	usb_deregister(&ti_usb_driver);
}


module_init(ti_init);
module_exit(ti_exit);


static int ti_startup(struct usb_serial *serial)
{
	struct ti_device *tdev;
	struct ti_port *tport;
	struct usb_device *dev = serial->dev;
	int status;
	int i;


	dbg("%s - product 0x%4X, num configurations %d, configuration value %d",
	    __FUNCTION__, le16_to_cpu(dev->descriptor.idProduct),
	    dev->descriptor.bNumConfigurations,
	    dev->actconfig->desc.bConfigurationValue);

	/* create device structure */
	tdev = kzalloc(sizeof(struct ti_device), GFP_KERNEL);
	if (tdev == NULL) {
		dev_err(&dev->dev, "%s - out of memory\n", __FUNCTION__);
		return -ENOMEM;
	}
	sema_init(&tdev->td_open_close_sem, 1);
	tdev->td_serial = serial;
	usb_set_serial_data(serial, tdev);

	/* determine device type */
	if (usb_match_id(serial->interface, ti_id_table_3410))
		tdev->td_is_3410 = 1;
	dbg("%s - device type is %s", __FUNCTION__, tdev->td_is_3410 ? "3410" : "5052");

	/* if we have only 1 configuration, download firmware */
	if (dev->descriptor.bNumConfigurations == 1) {

		if (tdev->td_is_3410)
			status = ti_download_firmware(tdev, ti_fw_3410,
				sizeof(ti_fw_3410));
		else
			status = ti_download_firmware(tdev, ti_fw_5052,
				sizeof(ti_fw_5052));
		if (status)
			goto free_tdev;

		/* 3410 must be reset, 5052 resets itself */
		if (tdev->td_is_3410) {
			msleep_interruptible(100);
			usb_reset_device(dev);
		}

		status = -ENODEV;
		goto free_tdev;
	} 

	/* the second configuration must be set (in sysfs by hotplug script) */
	if (dev->actconfig->desc.bConfigurationValue == TI_BOOT_CONFIG) {
		status = -ENODEV;
		goto free_tdev;
	}

	/* set up port structures */
	for (i = 0; i < serial->num_ports; ++i) {
		tport = kzalloc(sizeof(struct ti_port), GFP_KERNEL);
		if (tport == NULL) {
			dev_err(&dev->dev, "%s - out of memory\n", __FUNCTION__);
			status = -ENOMEM;
			goto free_tports;
		}
		spin_lock_init(&tport->tp_lock);
		tport->tp_uart_base_addr = (i == 0 ? TI_UART1_BASE_ADDR : TI_UART2_BASE_ADDR);
		tport->tp_flags = low_latency ? ASYNC_LOW_LATENCY : 0;
		tport->tp_closing_wait = closing_wait;
		init_waitqueue_head(&tport->tp_msr_wait);
		init_waitqueue_head(&tport->tp_write_wait);
		tport->tp_write_buf = ti_buf_alloc();
		if (tport->tp_write_buf == NULL) {
			dev_err(&dev->dev, "%s - out of memory\n", __FUNCTION__);
			kfree(tport);
			status = -ENOMEM;
			goto free_tports;
		}
		tport->tp_port = serial->port[i];
		tport->tp_tdev = tdev;
		usb_set_serial_port_data(serial->port[i], tport);
		tport->tp_uart_mode = 0;	/* default is RS232 */
	}
	
	return 0;

free_tports:
	for (--i; i>=0; --i) {
		tport = usb_get_serial_port_data(serial->port[i]);
		ti_buf_free(tport->tp_write_buf);
		kfree(tport);
		usb_set_serial_port_data(serial->port[i], NULL);
	}
free_tdev:
	kfree(tdev);
	usb_set_serial_data(serial, NULL);
	return status;
}


static void ti_shutdown(struct usb_serial *serial)
{
	int i;
	struct ti_device *tdev = usb_get_serial_data(serial);
	struct ti_port *tport;

	dbg("%s", __FUNCTION__);

	for (i=0; i < serial->num_ports; ++i) {
		tport = usb_get_serial_port_data(serial->port[i]);
		if (tport) {
			ti_buf_free(tport->tp_write_buf);
			kfree(tport);
			usb_set_serial_port_data(serial->port[i], NULL);
		}
	}

	kfree(tdev);
	usb_set_serial_data(serial, NULL);
}


static int ti_open(struct usb_serial_port *port, struct file *file)
{
	struct ti_port *tport = usb_get_serial_port_data(port);
	struct ti_device *tdev;
	struct usb_device *dev;
	struct urb *urb;
	int port_number;
	int status;
	__u16 open_settings = (__u8)(TI_PIPE_MODE_CONTINOUS | 
			     TI_PIPE_TIMEOUT_ENABLE | 
			     (TI_TRANSFER_TIMEOUT << 2));

	dbg("%s - port %d", __FUNCTION__, port->number);

	if (tport == NULL)
		return -ENODEV;

	dev = port->serial->dev;
	tdev = tport->tp_tdev;

	/* only one open on any port on a device at a time */
	if (down_interruptible(&tdev->td_open_close_sem))
		return -ERESTARTSYS;

	if (port->tty)
		port->tty->low_latency = 
			(tport->tp_flags & ASYNC_LOW_LATENCY) ? 1 : 0;

	port_number = port->number - port->serial->minor;

	memset(&(tport->tp_icount), 0x00, sizeof(tport->tp_icount));

	tport->tp_msr = 0;
	tport->tp_shadow_mcr |= (TI_MCR_RTS | TI_MCR_DTR);

	/* start interrupt urb the first time a port is opened on this device */
	if (tdev->td_open_port_count == 0) {
		dbg("%s - start interrupt in urb", __FUNCTION__);
		urb = tdev->td_serial->port[0]->interrupt_in_urb;
		if (!urb) {
			dev_err(&port->dev, "%s - no interrupt urb\n", __FUNCTION__);
			status = -EINVAL;
			goto up_sem;
		}
		urb->complete = ti_interrupt_callback;
		urb->context = tdev;
		urb->dev = dev;
		status = usb_submit_urb(urb, GFP_KERNEL);
		if (status) {
			dev_err(&port->dev, "%s - submit interrupt urb failed, %d\n", __FUNCTION__, status);
			goto up_sem;
		}
	}

	ti_set_termios(port, NULL);

	dbg("%s - sending TI_OPEN_PORT", __FUNCTION__);
	status = ti_command_out_sync(tdev, TI_OPEN_PORT,
		(__u8)(TI_UART1_PORT + port_number), open_settings, NULL, 0);
	if (status) {
		dev_err(&port->dev, "%s - cannot send open command, %d\n", __FUNCTION__, status);
		goto unlink_int_urb;
	}

	dbg("%s - sending TI_START_PORT", __FUNCTION__);
	status = ti_command_out_sync(tdev, TI_START_PORT,
		(__u8)(TI_UART1_PORT + port_number), 0, NULL, 0);
	if (status) {
		dev_err(&port->dev, "%s - cannot send start command, %d\n", __FUNCTION__, status);
		goto unlink_int_urb;
	}

	dbg("%s - sending TI_PURGE_PORT", __FUNCTION__);
	status = ti_command_out_sync(tdev, TI_PURGE_PORT,
		(__u8)(TI_UART1_PORT + port_number), TI_PURGE_INPUT, NULL, 0);
	if (status) {
		dev_err(&port->dev, "%s - cannot clear input buffers, %d\n", __FUNCTION__, status);
		goto unlink_int_urb;
	}
	status = ti_command_out_sync(tdev, TI_PURGE_PORT,
		(__u8)(TI_UART1_PORT + port_number), TI_PURGE_OUTPUT, NULL, 0);
	if (status) {
		dev_err(&port->dev, "%s - cannot clear output buffers, %d\n", __FUNCTION__, status);
		goto unlink_int_urb;
	}

	/* reset the data toggle on the bulk endpoints to work around bug in
	 * host controllers where things get out of sync some times */
	usb_clear_halt(dev, port->write_urb->pipe);
	usb_clear_halt(dev, port->read_urb->pipe);

	ti_set_termios(port, NULL);

	dbg("%s - sending TI_OPEN_PORT (2)", __FUNCTION__);
	status = ti_command_out_sync(tdev, TI_OPEN_PORT,
		(__u8)(TI_UART1_PORT + port_number), open_settings, NULL, 0);
	if (status) {
		dev_err(&port->dev, "%s - cannot send open command (2), %d\n", __FUNCTION__, status);
		goto unlink_int_urb;
	}

	dbg("%s - sending TI_START_PORT (2)", __FUNCTION__);
	status = ti_command_out_sync(tdev, TI_START_PORT,
		(__u8)(TI_UART1_PORT + port_number), 0, NULL, 0);
	if (status) {
		dev_err(&port->dev, "%s - cannot send start command (2), %d\n", __FUNCTION__, status);
		goto unlink_int_urb;
	}

	/* start read urb */
	dbg("%s - start read urb", __FUNCTION__);
	urb = port->read_urb;
	if (!urb) {
		dev_err(&port->dev, "%s - no read urb\n", __FUNCTION__);
		status = -EINVAL;
		goto unlink_int_urb;
	}
	tport->tp_read_urb_state = TI_READ_URB_RUNNING;
	urb->complete = ti_bulk_in_callback;
	urb->context = tport;
	urb->dev = dev;
	status = usb_submit_urb(urb, GFP_KERNEL);
	if (status) {
		dev_err(&port->dev, "%s - submit read urb failed, %d\n", __FUNCTION__, status);
		goto unlink_int_urb;
	}

	tport->tp_is_open = 1;
	++tdev->td_open_port_count;

	goto up_sem;

unlink_int_urb:
	if (tdev->td_open_port_count == 0)
		usb_kill_urb(port->serial->port[0]->interrupt_in_urb);
up_sem:
	up(&tdev->td_open_close_sem);
	dbg("%s - exit %d", __FUNCTION__, status);
	return status;
}


static void ti_close(struct usb_serial_port *port, struct file *file)
{
	struct ti_device *tdev;
	struct ti_port *tport;
	int port_number;
	int status;
	int do_up;

	dbg("%s - port %d", __FUNCTION__, port->number);
			 
	tdev = usb_get_serial_data(port->serial);
	tport = usb_get_serial_port_data(port);
	if (tdev == NULL || tport == NULL)
		return;

	tport->tp_is_open = 0;

	ti_drain(tport, (tport->tp_closing_wait*HZ)/100, 1);

	usb_kill_urb(port->read_urb);
	usb_kill_urb(port->write_urb);
	tport->tp_write_urb_in_use = 0;

	port_number = port->number - port->serial->minor;

	dbg("%s - sending TI_CLOSE_PORT", __FUNCTION__);
	status = ti_command_out_sync(tdev, TI_CLOSE_PORT,
		     (__u8)(TI_UART1_PORT + port_number), 0, NULL, 0);
	if (status)
		dev_err(&port->dev, "%s - cannot send close port command, %d\n" , __FUNCTION__, status);

	/* if down is interrupted, continue anyway */
	do_up = !down_interruptible(&tdev->td_open_close_sem);
	--tport->tp_tdev->td_open_port_count;
	if (tport->tp_tdev->td_open_port_count <= 0) {
		/* last port is closed, shut down interrupt urb */
		usb_kill_urb(port->serial->port[0]->interrupt_in_urb);
		tport->tp_tdev->td_open_port_count = 0;
	}
	if (do_up)
		up(&tdev->td_open_close_sem);

	dbg("%s - exit", __FUNCTION__);
}


static int ti_write(struct usb_serial_port *port, const unsigned char *data,
	int count)
{
	struct ti_port *tport = usb_get_serial_port_data(port);
	unsigned long flags;

	dbg("%s - port %d", __FUNCTION__, port->number);

	if (count == 0) {
		dbg("%s - write request of 0 bytes", __FUNCTION__);
		return 0;
	}

	if (tport == NULL || !tport->tp_is_open)
		return -ENODEV;

	spin_lock_irqsave(&tport->tp_lock, flags);
	count = ti_buf_put(tport->tp_write_buf, data, count);
	spin_unlock_irqrestore(&tport->tp_lock, flags);

	ti_send(tport);

	return count;
}


static int ti_write_room(struct usb_serial_port *port)
{
	struct ti_port *tport = usb_get_serial_port_data(port);
	int room = 0;
	unsigned long flags;

	dbg("%s - port %d", __FUNCTION__, port->number);

	if (tport == NULL)
		return -ENODEV;
	
	spin_lock_irqsave(&tport->tp_lock, flags);
	room = ti_buf_space_avail(tport->tp_write_buf);
	spin_unlock_irqrestore(&tport->tp_lock, flags);

	dbg("%s - returns %d", __FUNCTION__, room);
	return room;
}


static int ti_chars_in_buffer(struct usb_serial_port *port)
{
	struct ti_port *tport = usb_get_serial_port_data(port);
	int chars = 0;
	unsigned long flags;

	dbg("%s - port %d", __FUNCTION__, port->number);

	if (tport == NULL)
		return -ENODEV;

	spin_lock_irqsave(&tport->tp_lock, flags);
	chars = ti_buf_data_avail(tport->tp_write_buf);
	spin_unlock_irqrestore(&tport->tp_lock, flags);

	dbg("%s - returns %d", __FUNCTION__, chars);
	return chars;
}


static void ti_throttle(struct usb_serial_port *port)
{
	struct ti_port *tport = usb_get_serial_port_data(port);
	struct tty_struct *tty;

	dbg("%s - port %d", __FUNCTION__, port->number);

	if (tport == NULL)
		return;

	tty = port->tty;
	if (!tty) {
		dbg("%s - no tty", __FUNCTION__);
		return;
	}

	if (I_IXOFF(tty) || C_CRTSCTS(tty))
		ti_stop_read(tport, tty);

}


static void ti_unthrottle(struct usb_serial_port *port)
{
	struct ti_port *tport = usb_get_serial_port_data(port);
	struct tty_struct *tty;
	int status;

	dbg("%s - port %d", __FUNCTION__, port->number);

	if (tport == NULL)
		return;

	tty = port->tty;
	if (!tty) {
		dbg("%s - no tty", __FUNCTION__);
		return;
	}

	if (I_IXOFF(tty) || C_CRTSCTS(tty)) {
		status = ti_restart_read(tport, tty);
		if (status)
			dev_err(&port->dev, "%s - cannot restart read, %d\n", __FUNCTION__, status);
	}
}


static int ti_ioctl(struct usb_serial_port *port, struct file *file,
	unsigned int cmd, unsigned long arg)
{
	struct ti_port *tport = usb_get_serial_port_data(port);
	struct async_icount cnow;
	struct async_icount cprev;

	dbg("%s - port %d, cmd = 0x%04X", __FUNCTION__, port->number, cmd);

	if (tport == NULL)
		return -ENODEV;

	switch (cmd) {
		case TIOCGSERIAL:
			dbg("%s - (%d) TIOCGSERIAL", __FUNCTION__, port->number);
			return ti_get_serial_info(tport, (struct serial_struct __user *)arg);
			break;

		case TIOCSSERIAL:
			dbg("%s - (%d) TIOCSSERIAL", __FUNCTION__, port->number);
			return ti_set_serial_info(tport, (struct serial_struct __user *)arg);
			break;

		case TIOCMIWAIT:
			dbg("%s - (%d) TIOCMIWAIT", __FUNCTION__, port->number);
			cprev = tport->tp_icount;
			while (1) {
				interruptible_sleep_on(&tport->tp_msr_wait);
				if (signal_pending(current))
					return -ERESTARTSYS;
				cnow = tport->tp_icount;
				if (cnow.rng == cprev.rng && cnow.dsr == cprev.dsr &&
				    cnow.dcd == cprev.dcd && cnow.cts == cprev.cts)
					return -EIO; /* no change => error */
				if (((arg & TIOCM_RNG) && (cnow.rng != cprev.rng)) ||
				    ((arg & TIOCM_DSR) && (cnow.dsr != cprev.dsr)) ||
				    ((arg & TIOCM_CD)  && (cnow.dcd != cprev.dcd)) ||
				    ((arg & TIOCM_CTS) && (cnow.cts != cprev.cts)) ) {
					return 0;
				}
				cprev = cnow;
			}
			break;

		case TIOCGICOUNT:
			dbg("%s - (%d) TIOCGICOUNT RX=%d, TX=%d", __FUNCTION__, port->number, tport->tp_icount.rx, tport->tp_icount.tx);
			if (copy_to_user((void __user *)arg, &tport->tp_icount, sizeof(tport->tp_icount)))
				return -EFAULT;
			return 0;
	}

	return -ENOIOCTLCMD;
}


static void ti_set_termios(struct usb_serial_port *port,
	struct ktermios *old_termios)
{
	struct ti_port *tport = usb_get_serial_port_data(port);
	struct tty_struct *tty = port->tty;
	struct ti_uart_config *config;
	tcflag_t cflag,iflag;
	int baud;
	int status;
	int port_number = port->number - port->serial->minor;
	unsigned int mcr;

	dbg("%s - port %d", __FUNCTION__, port->number);

	if (!tty || !tty->termios) {
		dbg("%s - no tty or termios", __FUNCTION__);
		return;
	}

	cflag = tty->termios->c_cflag;
	iflag = tty->termios->c_iflag;

	if (old_termios && cflag == old_termios->c_cflag
	&& iflag == old_termios->c_iflag) {
		dbg("%s - nothing to change", __FUNCTION__);
		return;
	}

	dbg("%s - clfag %08x, iflag %08x", __FUNCTION__, cflag, iflag);

	if (old_termios)
		dbg("%s - old clfag %08x, old iflag %08x", __FUNCTION__, old_termios->c_cflag, old_termios->c_iflag);

	if (tport == NULL)
		return;

	config = kmalloc(sizeof(*config), GFP_KERNEL);
	if (!config) {
		dev_err(&port->dev, "%s - out of memory\n", __FUNCTION__);
		return;
	}

	config->wFlags = 0;

	/* these flags must be set */
	config->wFlags |= TI_UART_ENABLE_MS_INTS;
	config->wFlags |= TI_UART_ENABLE_AUTO_START_DMA;
	config->bUartMode = (__u8)(tport->tp_uart_mode);

	switch (cflag & CSIZE) {
		case CS5:
			    config->bDataBits = TI_UART_5_DATA_BITS;
			    break;
		case CS6:
			    config->bDataBits = TI_UART_6_DATA_BITS;
			    break;
		case CS7:
			    config->bDataBits = TI_UART_7_DATA_BITS;
			    break;
		default:
		case CS8:
			    config->bDataBits = TI_UART_8_DATA_BITS;
			    break;
	}

	if (cflag & PARENB) {
		if (cflag & PARODD) {
			config->wFlags |= TI_UART_ENABLE_PARITY_CHECKING;
			config->bParity = TI_UART_ODD_PARITY;
		} else {
			config->wFlags |= TI_UART_ENABLE_PARITY_CHECKING;
			config->bParity = TI_UART_EVEN_PARITY;
		}
	} else {
		config->wFlags &= ~TI_UART_ENABLE_PARITY_CHECKING;
		config->bParity = TI_UART_NO_PARITY; 	
	}

	if (cflag & CSTOPB)
		config->bStopBits = TI_UART_2_STOP_BITS;
	else
		config->bStopBits = TI_UART_1_STOP_BITS;

	if (cflag & CRTSCTS) {
		/* RTS flow control must be off to drop RTS for baud rate B0 */
		if ((cflag & CBAUD) != B0)
			config->wFlags |= TI_UART_ENABLE_RTS_IN;
		config->wFlags |= TI_UART_ENABLE_CTS_OUT;
	} else {
		tty->hw_stopped = 0;
		ti_restart_read(tport, tty);
	}

	if (I_IXOFF(tty) || I_IXON(tty)) {
		config->cXon  = START_CHAR(tty);
		config->cXoff = STOP_CHAR(tty);

		if (I_IXOFF(tty))
			config->wFlags |= TI_UART_ENABLE_X_IN;
		else
			ti_restart_read(tport, tty);

		if (I_IXON(tty))
			config->wFlags |= TI_UART_ENABLE_X_OUT;
	}

	baud = tty_get_baud_rate(tty);
	if (!baud) baud = 9600;
	if (tport->tp_tdev->td_is_3410)
		config->wBaudRate = (__u16)((923077 + baud/2) / baud);
	else
		config->wBaudRate = (__u16)((461538 + baud/2) / baud);

	dbg("%s - BaudRate=%d, wBaudRate=%d, wFlags=0x%04X, bDataBits=%d, bParity=%d, bStopBits=%d, cXon=%d, cXoff=%d, bUartMode=%d",
	__FUNCTION__, baud, config->wBaudRate, config->wFlags, config->bDataBits, config->bParity, config->bStopBits, config->cXon, config->cXoff, config->bUartMode);

	cpu_to_be16s(&config->wBaudRate);
	cpu_to_be16s(&config->wFlags);

	status = ti_command_out_sync(tport->tp_tdev, TI_SET_CONFIG,
		(__u8)(TI_UART1_PORT + port_number), 0, (__u8 *)config,
		sizeof(*config));
	if (status)
		dev_err(&port->dev, "%s - cannot set config on port %d, %d\n", __FUNCTION__, port_number, status);

	/* SET_CONFIG asserts RTS and DTR, reset them correctly */
	mcr = tport->tp_shadow_mcr;
	/* if baud rate is B0, clear RTS and DTR */
	if ((cflag & CBAUD) == B0)
		mcr &= ~(TI_MCR_DTR | TI_MCR_RTS);
	status = ti_set_mcr(tport, mcr);
	if (status)
		dev_err(&port->dev, "%s - cannot set modem control on port %d, %d\n", __FUNCTION__, port_number, status);

	kfree(config);
}


static int ti_tiocmget(struct usb_serial_port *port, struct file *file)
{
	struct ti_port *tport = usb_get_serial_port_data(port);
	unsigned int result;
	unsigned int msr;
	unsigned int mcr;

	dbg("%s - port %d", __FUNCTION__, port->number);

	if (tport == NULL)
		return -ENODEV;

	msr = tport->tp_msr;
	mcr = tport->tp_shadow_mcr;

	result = ((mcr & TI_MCR_DTR) ? TIOCM_DTR : 0)
		| ((mcr & TI_MCR_RTS) ? TIOCM_RTS : 0)
		| ((mcr & TI_MCR_LOOP) ? TIOCM_LOOP : 0)
		| ((msr & TI_MSR_CTS) ? TIOCM_CTS : 0)
		| ((msr & TI_MSR_CD) ? TIOCM_CAR : 0)
		| ((msr & TI_MSR_RI) ? TIOCM_RI : 0)
		| ((msr & TI_MSR_DSR) ? TIOCM_DSR : 0);

	dbg("%s - 0x%04X", __FUNCTION__, result);

	return result;
}


static int ti_tiocmset(struct usb_serial_port *port, struct file *file,
	unsigned int set, unsigned int clear)
{
	struct ti_port *tport = usb_get_serial_port_data(port);
	unsigned int mcr;

	dbg("%s - port %d", __FUNCTION__, port->number);

	if (tport == NULL)
		return -ENODEV;

	mcr = tport->tp_shadow_mcr;

	if (set & TIOCM_RTS)
		mcr |= TI_MCR_RTS;
	if (set & TIOCM_DTR)
		mcr |= TI_MCR_DTR;
	if (set & TIOCM_LOOP)
		mcr |= TI_MCR_LOOP;

	if (clear & TIOCM_RTS)
		mcr &= ~TI_MCR_RTS;
	if (clear & TIOCM_DTR)
		mcr &= ~TI_MCR_DTR;
	if (clear & TIOCM_LOOP)
		mcr &= ~TI_MCR_LOOP;

	return ti_set_mcr(tport, mcr);
}


static void ti_break(struct usb_serial_port *port, int break_state)
{
	struct ti_port *tport = usb_get_serial_port_data(port);
	int status;

	dbg("%s - state = %d", __FUNCTION__, break_state);

	if (tport == NULL)
		return;

	ti_drain(tport, (tport->tp_closing_wait*HZ)/100, 0);

	status = ti_write_byte(tport->tp_tdev,
		tport->tp_uart_base_addr + TI_UART_OFFSET_LCR,
		TI_LCR_BREAK, break_state == -1 ? TI_LCR_BREAK : 0);

	if (status)
		dbg("%s - error setting break, %d", __FUNCTION__, status);
}


static void ti_interrupt_callback(struct urb *urb)
{
	struct ti_device *tdev = (struct ti_device *)urb->context;
	struct usb_serial_port *port;
	struct usb_serial *serial = tdev->td_serial;
	struct ti_port *tport;
	struct device *dev = &urb->dev->dev;
	unsigned char *data = urb->transfer_buffer;
	int length = urb->actual_length;
	int port_number;
	int function;
	int status;
	__u8 msr;

	dbg("%s", __FUNCTION__);

	switch (urb->status) {
	case 0:
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		dbg("%s - urb shutting down, %d", __FUNCTION__, urb->status);
		tdev->td_urb_error = 1;
		return;
	default:
		dev_err(dev, "%s - nonzero urb status, %d\n", __FUNCTION__, urb->status);
		tdev->td_urb_error = 1;
		goto exit;
	}

	if (length != 2) {
		dbg("%s - bad packet size, %d", __FUNCTION__, length);
		goto exit;
	}

	if (data[0] == TI_CODE_HARDWARE_ERROR) {
		dev_err(dev, "%s - hardware error, %d\n", __FUNCTION__, data[1]);
		goto exit;
	}

	port_number = TI_GET_PORT_FROM_CODE(data[0]);
	function = TI_GET_FUNC_FROM_CODE(data[0]);

	dbg("%s - port_number %d, function %d, data 0x%02X", __FUNCTION__, port_number, function, data[1]);

	if (port_number >= serial->num_ports) {
		dev_err(dev, "%s - bad port number, %d\n", __FUNCTION__, port_number);
		goto exit;
	}

	port = serial->port[port_number];

	tport = usb_get_serial_port_data(port);
	if (!tport)
		goto exit;

	switch (function) {
	case TI_CODE_DATA_ERROR:
		dev_err(dev, "%s - DATA ERROR, port %d, data 0x%02X\n", __FUNCTION__, port_number, data[1]);
		break;

	case TI_CODE_MODEM_STATUS:
		msr = data[1];
		dbg("%s - port %d, msr 0x%02X", __FUNCTION__, port_number, msr);
		ti_handle_new_msr(tport, msr);
		break;

	default:
		dev_err(dev, "%s - unknown interrupt code, 0x%02X\n", __FUNCTION__, data[1]);
		break;
	}

exit:
	status = usb_submit_urb(urb, GFP_ATOMIC);
	if (status)
		dev_err(dev, "%s - resubmit interrupt urb failed, %d\n", __FUNCTION__, status);
}


static void ti_bulk_in_callback(struct urb *urb)
{
	struct ti_port *tport = (struct ti_port *)urb->context;
	struct usb_serial_port *port = tport->tp_port;
	struct device *dev = &urb->dev->dev;
	int status = 0;

	dbg("%s", __FUNCTION__);

	switch (urb->status) {
	case 0:
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		dbg("%s - urb shutting down, %d", __FUNCTION__, urb->status);
		tport->tp_tdev->td_urb_error = 1;
		wake_up_interruptible(&tport->tp_write_wait);
		return;
	default:
		dev_err(dev, "%s - nonzero urb status, %d\n", __FUNCTION__, urb->status );
		tport->tp_tdev->td_urb_error = 1;
		wake_up_interruptible(&tport->tp_write_wait);
	}

	if (urb->status == -EPIPE)
		goto exit;

	if (urb->status) {
		dev_err(dev, "%s - stopping read!\n", __FUNCTION__);
		return;
	}

	if (port->tty && urb->actual_length) {
		usb_serial_debug_data(debug, dev, __FUNCTION__,
			urb->actual_length, urb->transfer_buffer);

		if (!tport->tp_is_open)
			dbg("%s - port closed, dropping data", __FUNCTION__);
		else
			ti_recv(&urb->dev->dev, port->tty, urb->transfer_buffer,
				urb->actual_length);

		spin_lock(&tport->tp_lock);
		tport->tp_icount.rx += urb->actual_length;
		spin_unlock(&tport->tp_lock);
	}

exit:
	/* continue to read unless stopping */
	spin_lock(&tport->tp_lock);
	if (tport->tp_read_urb_state == TI_READ_URB_RUNNING) {
		urb->dev = port->serial->dev;
		status = usb_submit_urb(urb, GFP_ATOMIC);
	} else if (tport->tp_read_urb_state == TI_READ_URB_STOPPING) {
		tport->tp_read_urb_state = TI_READ_URB_STOPPED;
	}
	spin_unlock(&tport->tp_lock);
	if (status)
		dev_err(dev, "%s - resubmit read urb failed, %d\n", __FUNCTION__, status);
}


static void ti_bulk_out_callback(struct urb *urb)
{
	struct ti_port *tport = (struct ti_port *)urb->context;
	struct usb_serial_port *port = tport->tp_port;
	struct device *dev = &urb->dev->dev;

	dbg("%s - port %d", __FUNCTION__, port->number);

	tport->tp_write_urb_in_use = 0;

	switch (urb->status) {
	case 0:
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		dbg("%s - urb shutting down, %d", __FUNCTION__, urb->status);
		tport->tp_tdev->td_urb_error = 1;
		wake_up_interruptible(&tport->tp_write_wait);
		return;
	default:
		dev_err(dev, "%s - nonzero urb status, %d\n", __FUNCTION__, urb->status);
		tport->tp_tdev->td_urb_error = 1;
		wake_up_interruptible(&tport->tp_write_wait);
	}

	/* send any buffered data */
	ti_send(tport);
}


static void ti_recv(struct device *dev, struct tty_struct *tty,
	unsigned char *data, int length)
{
	int cnt;

	do {
		cnt = tty_buffer_request_room(tty, length);
		if (cnt < length) {
			dev_err(dev, "%s - dropping data, %d bytes lost\n", __FUNCTION__, length - cnt);
			if(cnt == 0)
				break;
		}
		tty_insert_flip_string(tty, data, cnt);
		tty_flip_buffer_push(tty);
		data += cnt;
		length -= cnt;
	} while (length > 0);

}


static void ti_send(struct ti_port *tport)
{
	int count, result;
	struct usb_serial_port *port = tport->tp_port;
	struct tty_struct *tty = port->tty;
	unsigned long flags;


	dbg("%s - port %d", __FUNCTION__, port->number);

	spin_lock_irqsave(&tport->tp_lock, flags);

	if (tport->tp_write_urb_in_use) {
		spin_unlock_irqrestore(&tport->tp_lock, flags);
		return;
	}

	count = ti_buf_get(tport->tp_write_buf,
				port->write_urb->transfer_buffer,
				port->bulk_out_size);

	if (count == 0) {
		spin_unlock_irqrestore(&tport->tp_lock, flags);
		return;
	}

	tport->tp_write_urb_in_use = 1;

	spin_unlock_irqrestore(&tport->tp_lock, flags);

	usb_serial_debug_data(debug, &port->dev, __FUNCTION__, count, port->write_urb->transfer_buffer);

	usb_fill_bulk_urb(port->write_urb, port->serial->dev,
			   usb_sndbulkpipe(port->serial->dev,
					    port->bulk_out_endpointAddress),
			   port->write_urb->transfer_buffer, count,
			   ti_bulk_out_callback, tport);

	result = usb_submit_urb(port->write_urb, GFP_ATOMIC);
	if (result) {
		dev_err(&port->dev, "%s - submit write urb failed, %d\n", __FUNCTION__, result);
		tport->tp_write_urb_in_use = 0; 
		/* TODO: reschedule ti_send */
	} else {
		spin_lock_irqsave(&tport->tp_lock, flags);
		tport->tp_icount.tx += count;
		spin_unlock_irqrestore(&tport->tp_lock, flags);
	}

	/* more room in the buffer for new writes, wakeup */
	if (tty)
		tty_wakeup(tty);
	wake_up_interruptible(&tport->tp_write_wait);
}


static int ti_set_mcr(struct ti_port *tport, unsigned int mcr)
{
	int status;

	status = ti_write_byte(tport->tp_tdev,
		tport->tp_uart_base_addr + TI_UART_OFFSET_MCR,
		TI_MCR_RTS | TI_MCR_DTR | TI_MCR_LOOP, mcr);

	if (!status)
		tport->tp_shadow_mcr = mcr;

	return status;
}


static int ti_get_lsr(struct ti_port *tport)
{
	int size,status;
	struct ti_device *tdev = tport->tp_tdev;
	struct usb_serial_port *port = tport->tp_port;
	int port_number = port->number - port->serial->minor;
	struct ti_port_status *data;

	dbg("%s - port %d", __FUNCTION__, port->number);

	size = sizeof(struct ti_port_status);
	data = kmalloc(size, GFP_KERNEL);
	if (!data) {
		dev_err(&port->dev, "%s - out of memory\n", __FUNCTION__);
		return -ENOMEM;
	}

	status = ti_command_in_sync(tdev, TI_GET_PORT_STATUS,
		(__u8)(TI_UART1_PORT+port_number), 0, (__u8 *)data, size);
	if (status) {
		dev_err(&port->dev, "%s - get port status command failed, %d\n", __FUNCTION__, status);
		goto free_data;
	}

	dbg("%s - lsr 0x%02X", __FUNCTION__, data->bLSR);

	tport->tp_lsr = data->bLSR;

free_data:
	kfree(data);
	return status;
}


static int ti_get_serial_info(struct ti_port *tport,
	struct serial_struct __user *ret_arg)
{
	struct usb_serial_port *port = tport->tp_port;
	struct serial_struct ret_serial;

	if (!ret_arg)
		return -EFAULT;

	memset(&ret_serial, 0, sizeof(ret_serial));

	ret_serial.type = PORT_16550A;
	ret_serial.line = port->serial->minor;
	ret_serial.port = port->number - port->serial->minor;
	ret_serial.flags = tport->tp_flags;
	ret_serial.xmit_fifo_size = TI_WRITE_BUF_SIZE;
	ret_serial.baud_base = tport->tp_tdev->td_is_3410 ? 921600 : 460800;
	ret_serial.closing_wait = tport->tp_closing_wait;

	if (copy_to_user(ret_arg, &ret_serial, sizeof(*ret_arg)))
		return -EFAULT;

	return 0;
}


static int ti_set_serial_info(struct ti_port *tport,
	struct serial_struct __user *new_arg)
{
	struct usb_serial_port *port = tport->tp_port;
	struct serial_struct new_serial;

	if (copy_from_user(&new_serial, new_arg, sizeof(new_serial)))
		return -EFAULT;

	tport->tp_flags = new_serial.flags & TI_SET_SERIAL_FLAGS;
	if (port->tty)
		port->tty->low_latency =
			(tport->tp_flags & ASYNC_LOW_LATENCY) ? 1 : 0;
	tport->tp_closing_wait = new_serial.closing_wait;

	return 0;
}


static void ti_handle_new_msr(struct ti_port *tport, __u8 msr)
{
	struct async_icount *icount;
	struct tty_struct *tty;
	unsigned long flags;

	dbg("%s - msr 0x%02X", __FUNCTION__, msr);

	if (msr & TI_MSR_DELTA_MASK) {
		spin_lock_irqsave(&tport->tp_lock, flags);
		icount = &tport->tp_icount;
		if (msr & TI_MSR_DELTA_CTS)
			icount->cts++;
		if (msr & TI_MSR_DELTA_DSR)
			icount->dsr++;
		if (msr & TI_MSR_DELTA_CD)
			icount->dcd++;
		if (msr & TI_MSR_DELTA_RI)
			icount->rng++;
		wake_up_interruptible(&tport->tp_msr_wait);
		spin_unlock_irqrestore(&tport->tp_lock, flags);
	}

	tport->tp_msr = msr & TI_MSR_MASK;

	/* handle CTS flow control */
	tty = tport->tp_port->tty;
	if (tty && C_CRTSCTS(tty)) {
		if (msr & TI_MSR_CTS) {
			tty->hw_stopped = 0;
			tty_wakeup(tty);
		} else {
			tty->hw_stopped = 1;
		}
	}
}


static void ti_drain(struct ti_port *tport, unsigned long timeout, int flush)
{
	struct ti_device *tdev = tport->tp_tdev;
	struct usb_serial_port *port = tport->tp_port;
	wait_queue_t wait;
	unsigned long flags;

	dbg("%s - port %d", __FUNCTION__, port->number);

	spin_lock_irqsave(&tport->tp_lock, flags);

	/* wait for data to drain from the buffer */
	tdev->td_urb_error = 0;
	init_waitqueue_entry(&wait, current);
	add_wait_queue(&tport->tp_write_wait, &wait);
	for (;;) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (ti_buf_data_avail(tport->tp_write_buf) == 0
		|| timeout == 0 || signal_pending(current)
		|| tdev->td_urb_error
		|| !usb_get_intfdata(port->serial->interface))  /* disconnect */
			break;
		spin_unlock_irqrestore(&tport->tp_lock, flags);
		timeout = schedule_timeout(timeout);
		spin_lock_irqsave(&tport->tp_lock, flags);
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&tport->tp_write_wait, &wait);

	/* flush any remaining data in the buffer */
	if (flush)
		ti_buf_clear(tport->tp_write_buf);

	spin_unlock_irqrestore(&tport->tp_lock, flags);

	/* wait for data to drain from the device */
	/* wait for empty tx register, plus 20 ms */
	timeout += jiffies;
	tport->tp_lsr &= ~TI_LSR_TX_EMPTY;
	while ((long)(jiffies - timeout) < 0 && !signal_pending(current)
	&& !(tport->tp_lsr&TI_LSR_TX_EMPTY) && !tdev->td_urb_error
	&& usb_get_intfdata(port->serial->interface)) {  /* not disconnected */
		if (ti_get_lsr(tport))
			break;
		msleep_interruptible(20);
	}
}


static void ti_stop_read(struct ti_port *tport, struct tty_struct *tty)
{
	unsigned long flags;

	spin_lock_irqsave(&tport->tp_lock, flags);

	if (tport->tp_read_urb_state == TI_READ_URB_RUNNING)
		tport->tp_read_urb_state = TI_READ_URB_STOPPING;

	spin_unlock_irqrestore(&tport->tp_lock, flags);
}


static int ti_restart_read(struct ti_port *tport, struct tty_struct *tty)
{
	struct urb *urb;
	int status = 0;
	unsigned long flags;

	spin_lock_irqsave(&tport->tp_lock, flags);

	if (tport->tp_read_urb_state == TI_READ_URB_STOPPED) {
		tport->tp_read_urb_state = TI_READ_URB_RUNNING;
		urb = tport->tp_port->read_urb;
		spin_unlock_irqrestore(&tport->tp_lock, flags);
		urb->complete = ti_bulk_in_callback;
		urb->context = tport;
		urb->dev = tport->tp_port->serial->dev;
		status = usb_submit_urb(urb, GFP_KERNEL);
	} else  {
		tport->tp_read_urb_state = TI_READ_URB_RUNNING;
		spin_unlock_irqrestore(&tport->tp_lock, flags);
	}

	return status;
}


static int ti_command_out_sync(struct ti_device *tdev, __u8 command,
	__u16 moduleid, __u16 value, __u8 *data, int size)
{
	int status;

	status = usb_control_msg(tdev->td_serial->dev,
		usb_sndctrlpipe(tdev->td_serial->dev, 0), command,
		(USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_OUT),
		value, moduleid, data, size, 1000);

	if (status == size)
		status = 0;

	if (status > 0)
		status = -ECOMM;

	return status;
}


static int ti_command_in_sync(struct ti_device *tdev, __u8 command,
	__u16 moduleid, __u16 value, __u8 *data, int size)
{
	int status;

	status = usb_control_msg(tdev->td_serial->dev,
		usb_rcvctrlpipe(tdev->td_serial->dev, 0), command,
		(USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_IN),
		value, moduleid, data, size, 1000);

	if (status == size)
		status = 0;

	if (status > 0)
		status = -ECOMM;

	return status;
}


static int ti_write_byte(struct ti_device *tdev, unsigned long addr,
	__u8 mask, __u8 byte)
{
	int status;
	unsigned int size;
	struct ti_write_data_bytes *data;
	struct device *dev = &tdev->td_serial->dev->dev;

	dbg("%s - addr 0x%08lX, mask 0x%02X, byte 0x%02X", __FUNCTION__, addr, mask, byte);

	size = sizeof(struct ti_write_data_bytes) + 2;
	data = kmalloc(size, GFP_KERNEL);
	if (!data) {
		dev_err(dev, "%s - out of memory\n", __FUNCTION__);
		return -ENOMEM;
	}

	data->bAddrType = TI_RW_DATA_ADDR_XDATA;
	data->bDataType = TI_RW_DATA_BYTE;
	data->bDataCounter = 1;
	data->wBaseAddrHi = cpu_to_be16(addr>>16);
	data->wBaseAddrLo = cpu_to_be16(addr);
	data->bData[0] = mask;
	data->bData[1] = byte;

	status = ti_command_out_sync(tdev, TI_WRITE_DATA, TI_RAM_PORT, 0,
		(__u8 *)data, size);

	if (status < 0)
		dev_err(dev, "%s - failed, %d\n", __FUNCTION__, status);

	kfree(data);

	return status;
}


static int ti_download_firmware(struct ti_device *tdev,
	unsigned char *firmware, unsigned int firmware_size)
{
	int status = 0;
	int buffer_size;
	int pos;
	int len;
	int done;
	__u8 cs = 0;
	__u8 *buffer;
	struct usb_device *dev = tdev->td_serial->dev;
	struct ti_firmware_header *header;
	unsigned int pipe = usb_sndbulkpipe(dev,
		tdev->td_serial->port[0]->bulk_out_endpointAddress);


	buffer_size = TI_FIRMWARE_BUF_SIZE + sizeof(struct ti_firmware_header);
	buffer = kmalloc(buffer_size, GFP_KERNEL);
	if (!buffer) {
		dev_err(&dev->dev, "%s - out of memory\n", __FUNCTION__);
		return -ENOMEM;
	}

	memcpy(buffer, firmware, firmware_size);
	memset(buffer+firmware_size, 0xff, buffer_size-firmware_size);

	for(pos = sizeof(struct ti_firmware_header); pos < buffer_size; pos++)
		cs = (__u8)(cs + buffer[pos]);

	header = (struct ti_firmware_header *)buffer;
	header->wLength = cpu_to_le16((__u16)(buffer_size - sizeof(struct ti_firmware_header)));
	header->bCheckSum = cs;

	dbg("%s - downloading firmware", __FUNCTION__);
	for (pos = 0; pos < buffer_size; pos += done) {
		len = min(buffer_size - pos, TI_DOWNLOAD_MAX_PACKET_SIZE);
		status = usb_bulk_msg(dev, pipe, buffer+pos, len, &done, 1000);
		if (status)
			break;
	}

	kfree(buffer);

	if (status) {
		dev_err(&dev->dev, "%s - error downloading firmware, %d\n", __FUNCTION__, status);
		return status;
	}

	dbg("%s - download successful", __FUNCTION__);

	return 0;
}


/* Circular Buffer Functions */

/*
 * ti_buf_alloc
 *
 * Allocate a circular buffer and all associated memory.
 */

static struct circ_buf *ti_buf_alloc(void)
{
	struct circ_buf *cb;

	cb = kmalloc(sizeof(struct circ_buf), GFP_KERNEL);
	if (cb == NULL)
		return NULL;

	cb->buf = kmalloc(TI_WRITE_BUF_SIZE, GFP_KERNEL);
	if (cb->buf == NULL) {
		kfree(cb);
		return NULL;
	}

	ti_buf_clear(cb);

	return cb;
}


/*
 * ti_buf_free
 *
 * Free the buffer and all associated memory.
 */

static void ti_buf_free(struct circ_buf *cb)
{
	kfree(cb->buf);
	kfree(cb);
}


/*
 * ti_buf_clear
 *
 * Clear out all data in the circular buffer.
 */

static void ti_buf_clear(struct circ_buf *cb)
{
	cb->head = cb->tail = 0;
}


/*
 * ti_buf_data_avail
 *
 * Return the number of bytes of data available in the circular
 * buffer.
 */

static int ti_buf_data_avail(struct circ_buf *cb)
{
	return CIRC_CNT(cb->head,cb->tail,TI_WRITE_BUF_SIZE);
}


/*
 * ti_buf_space_avail
 *
 * Return the number of bytes of space available in the circular
 * buffer.
 */

static int ti_buf_space_avail(struct circ_buf *cb)
{
	return CIRC_SPACE(cb->head,cb->tail,TI_WRITE_BUF_SIZE);
}


/*
 * ti_buf_put
 *
 * Copy data data from a user buffer and put it into the circular buffer.
 * Restrict to the amount of space available.
 *
 * Return the number of bytes copied.
 */

static int ti_buf_put(struct circ_buf *cb, const char *buf, int count)
{
	int c, ret = 0;

	while (1) {
		c = CIRC_SPACE_TO_END(cb->head, cb->tail, TI_WRITE_BUF_SIZE);
		if (count < c)
			c = count;
		if (c <= 0)
			break;
		memcpy(cb->buf + cb->head, buf, c);
		cb->head = (cb->head + c) & (TI_WRITE_BUF_SIZE-1);
		buf += c;
		count -= c;
		ret += c;
	}

	return ret;
}


/*
 * ti_buf_get
 *
 * Get data from the circular buffer and copy to the given buffer.
 * Restrict to the amount of data available.
 *
 * Return the number of bytes copied.
 */

static int ti_buf_get(struct circ_buf *cb, char *buf, int count)
{
	int c, ret = 0;

	while (1) {
		c = CIRC_CNT_TO_END(cb->head, cb->tail, TI_WRITE_BUF_SIZE);
		if (count < c)
			c = count;
		if (c <= 0)
			break;
		memcpy(buf, cb->buf + cb->tail, c);
		cb->tail = (cb->tail + c) & (TI_WRITE_BUF_SIZE-1);
		buf += c;
		count -= c;
		ret += c;
	}

	return ret;
}
