/*
 * USB HandSpring Visor, Palm m50x, and Sony Clie driver
 * (supports all of the Palm OS USB devices)
 *
 *	Copyright (C) 1999 - 2004
 *	    Greg Kroah-Hartman (greg@kroah.com)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License version
 *	2 as published by the Free Software Foundation.
 *
 * See Documentation/usb/usb-serial.txt for more information on using this
 * driver
 *
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>
#include <linux/usb/cdc.h>
#include "visor.h"

/*
 * Version Information
 */
#define DRIVER_AUTHOR "Greg Kroah-Hartman <greg@kroah.com>"
#define DRIVER_DESC "USB HandSpring Visor / Palm OS driver"

/* function prototypes for a handspring visor */
static int  visor_open(struct tty_struct *tty, struct usb_serial_port *port);
static void visor_close(struct usb_serial_port *port);
static int  visor_probe(struct usb_serial *serial,
					const struct usb_device_id *id);
static int  visor_calc_num_ports(struct usb_serial *serial);
static void visor_read_int_callback(struct urb *urb);
static int  clie_3_5_startup(struct usb_serial *serial);
static int  treo_attach(struct usb_serial *serial);
static int clie_5_attach(struct usb_serial *serial);
static int palm_os_3_probe(struct usb_serial *serial,
					const struct usb_device_id *id);
static int palm_os_4_probe(struct usb_serial *serial,
					const struct usb_device_id *id);

/* Parameters that may be passed into the module. */
static int debug;
static __u16 vendor;
static __u16 product;

static struct usb_device_id id_table [] = {
	{ USB_DEVICE(HANDSPRING_VENDOR_ID, HANDSPRING_VISOR_ID),
		.driver_info = (kernel_ulong_t)&palm_os_3_probe },
	{ USB_DEVICE(HANDSPRING_VENDOR_ID, HANDSPRING_TREO_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(HANDSPRING_VENDOR_ID, HANDSPRING_TREO600_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(GSPDA_VENDOR_ID, GSPDA_XPLORE_M68_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(PALM_VENDOR_ID, PALM_M500_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(PALM_VENDOR_ID, PALM_M505_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(PALM_VENDOR_ID, PALM_M515_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(PALM_VENDOR_ID, PALM_I705_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(PALM_VENDOR_ID, PALM_M100_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(PALM_VENDOR_ID, PALM_M125_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(PALM_VENDOR_ID, PALM_M130_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(PALM_VENDOR_ID, PALM_TUNGSTEN_T_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(PALM_VENDOR_ID, PALM_TREO_650),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(PALM_VENDOR_ID, PALM_TUNGSTEN_Z_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(PALM_VENDOR_ID, PALM_ZIRE_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(SONY_VENDOR_ID, SONY_CLIE_4_0_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(SONY_VENDOR_ID, SONY_CLIE_S360_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(SONY_VENDOR_ID, SONY_CLIE_4_1_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(SONY_VENDOR_ID, SONY_CLIE_NX60_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(SONY_VENDOR_ID, SONY_CLIE_NZ90V_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(SONY_VENDOR_ID, SONY_CLIE_TJ25_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(ACER_VENDOR_ID, ACER_S10_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(SAMSUNG_VENDOR_ID, SAMSUNG_SCH_I330_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(SAMSUNG_VENDOR_ID, SAMSUNG_SPH_I500_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(TAPWAVE_VENDOR_ID, TAPWAVE_ZODIAC_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(GARMIN_VENDOR_ID, GARMIN_IQUE_3600_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(ACEECA_VENDOR_ID, ACEECA_MEZ1000_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(KYOCERA_VENDOR_ID, KYOCERA_7135_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(FOSSIL_VENDOR_ID, FOSSIL_ABACUS_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ },					/* optional parameter entry */
	{ }					/* Terminating entry */
};

static struct usb_device_id clie_id_5_table [] = {
	{ USB_DEVICE(SONY_VENDOR_ID, SONY_CLIE_UX50_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ },					/* optional parameter entry */
	{ }					/* Terminating entry */
};

static struct usb_device_id clie_id_3_5_table [] = {
	{ USB_DEVICE(SONY_VENDOR_ID, SONY_CLIE_3_5_ID) },
	{ }					/* Terminating entry */
};

static struct usb_device_id id_table_combined [] = {
	{ USB_DEVICE(HANDSPRING_VENDOR_ID, HANDSPRING_VISOR_ID) },
	{ USB_DEVICE(HANDSPRING_VENDOR_ID, HANDSPRING_TREO_ID) },
	{ USB_DEVICE(HANDSPRING_VENDOR_ID, HANDSPRING_TREO600_ID) },
	{ USB_DEVICE(GSPDA_VENDOR_ID, GSPDA_XPLORE_M68_ID) },
	{ USB_DEVICE(PALM_VENDOR_ID, PALM_M500_ID) },
	{ USB_DEVICE(PALM_VENDOR_ID, PALM_M505_ID) },
	{ USB_DEVICE(PALM_VENDOR_ID, PALM_M515_ID) },
	{ USB_DEVICE(PALM_VENDOR_ID, PALM_I705_ID) },
	{ USB_DEVICE(PALM_VENDOR_ID, PALM_M100_ID) },
	{ USB_DEVICE(PALM_VENDOR_ID, PALM_M125_ID) },
	{ USB_DEVICE(PALM_VENDOR_ID, PALM_M130_ID) },
	{ USB_DEVICE(PALM_VENDOR_ID, PALM_TUNGSTEN_T_ID) },
	{ USB_DEVICE(PALM_VENDOR_ID, PALM_TREO_650) },
	{ USB_DEVICE(PALM_VENDOR_ID, PALM_TUNGSTEN_Z_ID) },
	{ USB_DEVICE(PALM_VENDOR_ID, PALM_ZIRE_ID) },
	{ USB_DEVICE(SONY_VENDOR_ID, SONY_CLIE_3_5_ID) },
	{ USB_DEVICE(SONY_VENDOR_ID, SONY_CLIE_4_0_ID) },
	{ USB_DEVICE(SONY_VENDOR_ID, SONY_CLIE_S360_ID) },
	{ USB_DEVICE(SONY_VENDOR_ID, SONY_CLIE_4_1_ID) },
	{ USB_DEVICE(SONY_VENDOR_ID, SONY_CLIE_NX60_ID) },
	{ USB_DEVICE(SONY_VENDOR_ID, SONY_CLIE_NZ90V_ID) },
	{ USB_DEVICE(SONY_VENDOR_ID, SONY_CLIE_UX50_ID) },
	{ USB_DEVICE(SONY_VENDOR_ID, SONY_CLIE_TJ25_ID) },
	{ USB_DEVICE(SAMSUNG_VENDOR_ID, SAMSUNG_SCH_I330_ID) },
	{ USB_DEVICE(SAMSUNG_VENDOR_ID, SAMSUNG_SPH_I500_ID) },
	{ USB_DEVICE(TAPWAVE_VENDOR_ID, TAPWAVE_ZODIAC_ID) },
	{ USB_DEVICE(GARMIN_VENDOR_ID, GARMIN_IQUE_3600_ID) },
	{ USB_DEVICE(ACEECA_VENDOR_ID, ACEECA_MEZ1000_ID) },
	{ USB_DEVICE(KYOCERA_VENDOR_ID, KYOCERA_7135_ID) },
	{ USB_DEVICE(FOSSIL_VENDOR_ID, FOSSIL_ABACUS_ID) },
	{ },					/* optional parameter entry */
	{ }					/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, id_table_combined);

static struct usb_driver visor_driver = {
	.name =		"visor",
	.probe =	usb_serial_probe,
	.disconnect =	usb_serial_disconnect,
	.id_table =	id_table_combined,
	.no_dynamic_id = 	1,
};

/* All of the device info needed for the Handspring Visor,
   and Palm 4.0 devices */
static struct usb_serial_driver handspring_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"visor",
	},
	.description =		"Handspring Visor / Palm OS",
	.usb_driver =		&visor_driver,
	.id_table =		id_table,
	.num_ports =		2,
	.bulk_out_size =	256,
	.open =			visor_open,
	.close =		visor_close,
	.throttle =		usb_serial_generic_throttle,
	.unthrottle =		usb_serial_generic_unthrottle,
	.attach =		treo_attach,
	.probe =		visor_probe,
	.calc_num_ports =	visor_calc_num_ports,
	.read_int_callback =	visor_read_int_callback,
};

/* All of the device info needed for the Clie UX50, TH55 Palm 5.0 devices */
static struct usb_serial_driver clie_5_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"clie_5",
	},
	.description =		"Sony Clie 5.0",
	.usb_driver =		&visor_driver,
	.id_table =		clie_id_5_table,
	.num_ports =		2,
	.bulk_out_size =	256,
	.open =			visor_open,
	.close =		visor_close,
	.throttle =		usb_serial_generic_throttle,
	.unthrottle =		usb_serial_generic_unthrottle,
	.attach =		clie_5_attach,
	.probe =		visor_probe,
	.calc_num_ports =	visor_calc_num_ports,
	.read_int_callback =	visor_read_int_callback,
};

/* device info for the Sony Clie OS version 3.5 */
static struct usb_serial_driver clie_3_5_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"clie_3.5",
	},
	.description =		"Sony Clie 3.5",
	.usb_driver =		&visor_driver,
	.id_table =		clie_id_3_5_table,
	.num_ports =		1,
	.bulk_out_size =	256,
	.open =			visor_open,
	.close =		visor_close,
	.throttle =		usb_serial_generic_throttle,
	.unthrottle =		usb_serial_generic_unthrottle,
	.attach =		clie_3_5_startup,
};

/******************************************************************************
 * Handspring Visor specific driver functions
 ******************************************************************************/
static int visor_open(struct tty_struct *tty, struct usb_serial_port *port)
{
	int result = 0;

	dbg("%s - port %d", __func__, port->number);

	if (!port->read_urb) {
		/* this is needed for some brain dead Sony devices */
		dev_err(&port->dev, "Device lied about number of ports, please use a lower one.\n");
		return -ENODEV;
	}

	/* Start reading from the device */
	result = usb_serial_generic_open(tty, port);
	if (result)
		goto exit;

	if (port->interrupt_in_urb) {
		dbg("%s - adding interrupt input for treo", __func__);
		result = usb_submit_urb(port->interrupt_in_urb, GFP_KERNEL);
		if (result)
			dev_err(&port->dev,
			    "%s - failed submitting interrupt urb, error %d\n",
							__func__, result);
	}
exit:
	return result;
}


static void visor_close(struct usb_serial_port *port)
{
	unsigned char *transfer_buffer;

	dbg("%s - port %d", __func__, port->number);

	/* shutdown our urbs */
	usb_serial_generic_close(port);
	usb_kill_urb(port->interrupt_in_urb);

	mutex_lock(&port->serial->disc_mutex);
	if (!port->serial->disconnected) {
		/* Try to send shutdown message, unless the device is gone */
		transfer_buffer =  kmalloc(0x12, GFP_KERNEL);
		if (transfer_buffer) {
			usb_control_msg(port->serial->dev,
					 usb_rcvctrlpipe(port->serial->dev, 0),
					 VISOR_CLOSE_NOTIFICATION, 0xc2,
					 0x0000, 0x0000,
					 transfer_buffer, 0x12, 300);
			kfree(transfer_buffer);
		}
	}
	mutex_unlock(&port->serial->disc_mutex);
}

static void visor_read_int_callback(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	int status = urb->status;
	int result;

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
		dbg("%s - nonzero urb status received: %d",
		    __func__, status);
		goto exit;
	}

	/*
	 * This information is still unknown what it can be used for.
	 * If anyone has an idea, please let the author know...
	 *
	 * Rumor has it this endpoint is used to notify when data
	 * is ready to be read from the bulk ones.
	 */
	usb_serial_debug_data(debug, &port->dev, __func__,
			      urb->actual_length, urb->transfer_buffer);

exit:
	result = usb_submit_urb(urb, GFP_ATOMIC);
	if (result)
		dev_err(&urb->dev->dev,
				"%s - Error %d submitting interrupt urb\n",
							__func__, result);
}

static int palm_os_3_probe(struct usb_serial *serial,
						const struct usb_device_id *id)
{
	struct device *dev = &serial->dev->dev;
	struct visor_connection_info *connection_info;
	unsigned char *transfer_buffer;
	char *string;
	int retval = 0;
	int i;
	int num_ports = 0;

	dbg("%s", __func__);

	transfer_buffer = kmalloc(sizeof(*connection_info), GFP_KERNEL);
	if (!transfer_buffer) {
		dev_err(dev, "%s - kmalloc(%Zd) failed.\n", __func__,
			sizeof(*connection_info));
		return -ENOMEM;
	}

	/* send a get connection info request */
	retval = usb_control_msg(serial->dev,
				  usb_rcvctrlpipe(serial->dev, 0),
				  VISOR_GET_CONNECTION_INFORMATION,
				  0xc2, 0x0000, 0x0000, transfer_buffer,
				  sizeof(*connection_info), 300);
	if (retval < 0) {
		dev_err(dev, "%s - error %d getting connection information\n",
			__func__, retval);
		goto exit;
	}

	if (retval == sizeof(*connection_info)) {
			connection_info = (struct visor_connection_info *)
							transfer_buffer;

		num_ports = le16_to_cpu(connection_info->num_ports);
		for (i = 0; i < num_ports; ++i) {
			switch (
			   connection_info->connections[i].port_function_id) {
			case VISOR_FUNCTION_GENERIC:
				string = "Generic";
				break;
			case VISOR_FUNCTION_DEBUGGER:
				string = "Debugger";
				break;
			case VISOR_FUNCTION_HOTSYNC:
				string = "HotSync";
				break;
			case VISOR_FUNCTION_CONSOLE:
				string = "Console";
				break;
			case VISOR_FUNCTION_REMOTE_FILE_SYS:
				string = "Remote File System";
				break;
			default:
				string = "unknown";
				break;
			}
			dev_info(dev, "%s: port %d, is for %s use\n",
				serial->type->description,
				connection_info->connections[i].port, string);
		}
	}
	/*
	* Handle devices that report invalid stuff here.
	*/
	if (num_ports == 0 || num_ports > 2) {
		dev_warn(dev, "%s: No valid connect info available\n",
			serial->type->description);
		num_ports = 2;
	}

	dev_info(dev, "%s: Number of ports: %d\n", serial->type->description,
		num_ports);

	/*
	 * save off our num_ports info so that we can use it in the
	 * calc_num_ports callback
	 */
	usb_set_serial_data(serial, (void *)(long)num_ports);

	/* ask for the number of bytes available, but ignore the
	   response as it is broken */
	retval = usb_control_msg(serial->dev,
				  usb_rcvctrlpipe(serial->dev, 0),
				  VISOR_REQUEST_BYTES_AVAILABLE,
				  0xc2, 0x0000, 0x0005, transfer_buffer,
				  0x02, 300);
	if (retval < 0)
		dev_err(dev, "%s - error %d getting bytes available request\n",
			__func__, retval);
	retval = 0;

exit:
	kfree(transfer_buffer);

	return retval;
}

static int palm_os_4_probe(struct usb_serial *serial,
						const struct usb_device_id *id)
{
	struct device *dev = &serial->dev->dev;
	struct palm_ext_connection_info *connection_info;
	unsigned char *transfer_buffer;
	int retval;

	dbg("%s", __func__);

	transfer_buffer =  kmalloc(sizeof(*connection_info), GFP_KERNEL);
	if (!transfer_buffer) {
		dev_err(dev, "%s - kmalloc(%Zd) failed.\n", __func__,
			sizeof(*connection_info));
		return -ENOMEM;
	}

	retval = usb_control_msg(serial->dev,
				  usb_rcvctrlpipe(serial->dev, 0),
				  PALM_GET_EXT_CONNECTION_INFORMATION,
				  0xc2, 0x0000, 0x0000, transfer_buffer,
				  sizeof(*connection_info), 300);
	if (retval < 0)
		dev_err(dev, "%s - error %d getting connection info\n",
			__func__, retval);
	else
		usb_serial_debug_data(debug, &serial->dev->dev, __func__,
				      retval, transfer_buffer);

	kfree(transfer_buffer);
	return 0;
}


static int visor_probe(struct usb_serial *serial,
					const struct usb_device_id *id)
{
	int retval = 0;
	int (*startup)(struct usb_serial *serial,
					const struct usb_device_id *id);

	dbg("%s", __func__);

	/*
	 * some Samsung Android phones in modem mode have the same ID
	 * as SPH-I500, but they are ACM devices, so dont bind to them
	 */
	if (id->idVendor == SAMSUNG_VENDOR_ID &&
		id->idProduct == SAMSUNG_SPH_I500_ID &&
		serial->dev->descriptor.bDeviceClass == USB_CLASS_COMM &&
		serial->dev->descriptor.bDeviceSubClass ==
			USB_CDC_SUBCLASS_ACM)
		return -ENODEV;

	if (serial->dev->actconfig->desc.bConfigurationValue != 1) {
		dev_err(&serial->dev->dev, "active config #%d != 1 ??\n",
			serial->dev->actconfig->desc.bConfigurationValue);
		return -ENODEV;
	}

	if (id->driver_info) {
		startup = (void *)id->driver_info;
		retval = startup(serial, id);
	}

	return retval;
}

static int visor_calc_num_ports(struct usb_serial *serial)
{
	int num_ports = (int)(long)(usb_get_serial_data(serial));

	if (num_ports)
		usb_set_serial_data(serial, NULL);

	return num_ports;
}

static int clie_3_5_startup(struct usb_serial *serial)
{
	struct device *dev = &serial->dev->dev;
	int result;
	u8 *data;

	dbg("%s", __func__);

	data = kmalloc(1, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	/*
	 * Note that PEG-300 series devices expect the following two calls.
	 */

	/* get the config number */
	result = usb_control_msg(serial->dev, usb_rcvctrlpipe(serial->dev, 0),
				  USB_REQ_GET_CONFIGURATION, USB_DIR_IN,
				  0, 0, data, 1, 3000);
	if (result < 0) {
		dev_err(dev, "%s: get config number failed: %d\n",
							__func__, result);
		goto out;
	}
	if (result != 1) {
		dev_err(dev, "%s: get config number bad return length: %d\n",
							__func__, result);
		result = -EIO;
		goto out;
	}

	/* get the interface number */
	result = usb_control_msg(serial->dev, usb_rcvctrlpipe(serial->dev, 0),
				  USB_REQ_GET_INTERFACE,
				  USB_DIR_IN | USB_RECIP_INTERFACE,
				  0, 0, data, 1, 3000);
	if (result < 0) {
		dev_err(dev, "%s: get interface number failed: %d\n",
							__func__, result);
		goto out;
	}
	if (result != 1) {
		dev_err(dev,
			"%s: get interface number bad return length: %d\n",
							__func__, result);
		result = -EIO;
		goto out;
	}

	result = 0;
out:
	kfree(data);

	return result;
}

static int treo_attach(struct usb_serial *serial)
{
	struct usb_serial_port *swap_port;

	/* Only do this endpoint hack for the Handspring devices with
	 * interrupt in endpoints, which for now are the Treo devices. */
	if (!((le16_to_cpu(serial->dev->descriptor.idVendor)
						== HANDSPRING_VENDOR_ID) ||
		(le16_to_cpu(serial->dev->descriptor.idVendor)
						== KYOCERA_VENDOR_ID)) ||
		(serial->num_interrupt_in == 0))
		return 0;

	dbg("%s", __func__);

	/*
	* It appears that Treos and Kyoceras want to use the
	* 1st bulk in endpoint to communicate with the 2nd bulk out endpoint,
	* so let's swap the 1st and 2nd bulk in and interrupt endpoints.
	* Note that swapping the bulk out endpoints would break lots of
	* apps that want to communicate on the second port.
	*/
#define COPY_PORT(dest, src)						\
	do { \
		dest->read_urb = src->read_urb;				\
		dest->bulk_in_endpointAddress = src->bulk_in_endpointAddress;\
		dest->bulk_in_buffer = src->bulk_in_buffer;		\
		dest->interrupt_in_urb = src->interrupt_in_urb;		\
		dest->interrupt_in_endpointAddress = \
					src->interrupt_in_endpointAddress;\
		dest->interrupt_in_buffer = src->interrupt_in_buffer;	\
	} while (0);

	swap_port = kmalloc(sizeof(*swap_port), GFP_KERNEL);
	if (!swap_port)
		return -ENOMEM;
	COPY_PORT(swap_port, serial->port[0]);
	COPY_PORT(serial->port[0], serial->port[1]);
	COPY_PORT(serial->port[1], swap_port);
	kfree(swap_port);

	return 0;
}

static int clie_5_attach(struct usb_serial *serial)
{
	struct usb_serial_port *port;
	unsigned int pipe;
	int j;

	dbg("%s", __func__);

	/* TH55 registers 2 ports.
	   Communication in from the UX50/TH55 uses bulk_in_endpointAddress
	   from port 0. Communication out to the UX50/TH55 uses
	   bulk_out_endpointAddress from port 1

	   Lets do a quick and dirty mapping
	 */

	/* some sanity check */
	if (serial->num_ports < 2)
		return -1;

	/* port 0 now uses the modified endpoint Address */
	port = serial->port[0];
	port->bulk_out_endpointAddress =
				serial->port[1]->bulk_out_endpointAddress;

	pipe = usb_sndbulkpipe(serial->dev, port->bulk_out_endpointAddress);
	for (j = 0; j < ARRAY_SIZE(port->write_urbs); ++j)
		port->write_urbs[j]->pipe = pipe;

	return 0;
}

static int __init visor_init(void)
{
	int i, retval;
	/* Only if parameters were passed to us */
	if (vendor > 0 && product > 0) {
		struct usb_device_id usb_dev_temp[] = {
			{
				USB_DEVICE(vendor, product),
				.driver_info =
					(kernel_ulong_t) &palm_os_4_probe
			}
		};

		/* Find the last entry in id_table */
		for (i = 0;; i++) {
			if (id_table[i].idVendor == 0) {
				id_table[i] = usb_dev_temp[0];
				break;
			}
		}
		/* Find the last entry in id_table_combined */
		for (i = 0;; i++) {
			if (id_table_combined[i].idVendor == 0) {
				id_table_combined[i] = usb_dev_temp[0];
				break;
			}
		}
		printk(KERN_INFO KBUILD_MODNAME
		       ": Untested USB device specified at time of module insertion\n");
		printk(KERN_INFO KBUILD_MODNAME
		       ": Warning: This is not guaranteed to work\n");
		printk(KERN_INFO KBUILD_MODNAME
		       ": Using a newer kernel is preferred to this method\n");
		printk(KERN_INFO KBUILD_MODNAME
		       ": Adding Palm OS protocol 4.x support for unknown device: 0x%x/0x%x\n",
			vendor, product);
	}
	retval = usb_serial_register(&handspring_device);
	if (retval)
		goto failed_handspring_register;
	retval = usb_serial_register(&clie_3_5_device);
	if (retval)
		goto failed_clie_3_5_register;
	retval = usb_serial_register(&clie_5_device);
	if (retval)
		goto failed_clie_5_register;
	retval = usb_register(&visor_driver);
	if (retval)
		goto failed_usb_register;
	printk(KERN_INFO KBUILD_MODNAME ": " DRIVER_DESC "\n");

	return 0;
failed_usb_register:
	usb_serial_deregister(&clie_5_device);
failed_clie_5_register:
	usb_serial_deregister(&clie_3_5_device);
failed_clie_3_5_register:
	usb_serial_deregister(&handspring_device);
failed_handspring_register:
	return retval;
}


static void __exit visor_exit (void)
{
	usb_deregister(&visor_driver);
	usb_serial_deregister(&handspring_device);
	usb_serial_deregister(&clie_3_5_device);
	usb_serial_deregister(&clie_5_device);
}


module_init(visor_init);
module_exit(visor_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug enabled or not");

module_param(vendor, ushort, 0);
MODULE_PARM_DESC(vendor, "User specified vendor ID");
module_param(product, ushort, 0);
MODULE_PARM_DESC(product, "User specified product ID");

