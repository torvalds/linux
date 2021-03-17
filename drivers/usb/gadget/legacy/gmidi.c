// SPDX-License-Identifier: GPL-2.0
/*
 * gmidi.c -- USB MIDI Gadget Driver
 *
 * Copyright (C) 2006 Thumtronics Pty Ltd.
 * Developed for Thumtronics by Grey Innovation
 * Ben Williamson <ben.williamson@greyinnovation.com>
 *
 * This code is based in part on:
 *
 * Gadget Zero driver, Copyright (C) 2003-2004 David Brownell.
 * USB Audio driver, Copyright (C) 2002 by Takashi Iwai.
 * USB MIDI driver, Copyright (C) 2002-2005 Clemens Ladisch.
 *
 * Refer to the USB Device Class Definition for MIDI Devices:
 * http://www.usb.org/developers/devclass_docs/midi10.pdf
 */

/* #define VERBOSE_DEBUG */

#include <linux/kernel.h>
#include <linux/module.h>

#include <sound/initval.h>

#include <linux/usb/composite.h>
#include <linux/usb/gadget.h>

#include "u_midi.h"

/*-------------------------------------------------------------------------*/

MODULE_AUTHOR("Ben Williamson");
MODULE_LICENSE("GPL v2");

static const char longname[] = "MIDI Gadget";

USB_GADGET_COMPOSITE_OPTIONS();

static int index = SNDRV_DEFAULT_IDX1;
module_param(index, int, S_IRUGO);
MODULE_PARM_DESC(index, "Index value for the USB MIDI Gadget adapter.");

static char *id = SNDRV_DEFAULT_STR1;
module_param(id, charp, S_IRUGO);
MODULE_PARM_DESC(id, "ID string for the USB MIDI Gadget adapter.");

static unsigned int buflen = 512;
module_param(buflen, uint, S_IRUGO);
MODULE_PARM_DESC(buflen, "MIDI buffer length");

static unsigned int qlen = 32;
module_param(qlen, uint, S_IRUGO);
MODULE_PARM_DESC(qlen, "USB read and write request queue length");

static unsigned int in_ports = 1;
module_param(in_ports, uint, S_IRUGO);
MODULE_PARM_DESC(in_ports, "Number of MIDI input ports");

static unsigned int out_ports = 1;
module_param(out_ports, uint, S_IRUGO);
MODULE_PARM_DESC(out_ports, "Number of MIDI output ports");

/* Thanks to Grey Innovation for donating this product ID.
 *
 * DO NOT REUSE THESE IDs with a protocol-incompatible driver!!  Ever!!
 * Instead:  allocate your own, using normal USB-IF procedures.
 */
#define DRIVER_VENDOR_NUM	0x17b3		/* Grey Innovation */
#define DRIVER_PRODUCT_NUM	0x0004		/* Linux-USB "MIDI Gadget" */

/* string IDs are assigned dynamically */

#define STRING_DESCRIPTION_IDX		USB_GADGET_FIRST_AVAIL_IDX

static struct usb_device_descriptor device_desc = {
	.bLength =		USB_DT_DEVICE_SIZE,
	.bDescriptorType =	USB_DT_DEVICE,
	/* .bcdUSB = DYNAMIC */
	.bDeviceClass =		USB_CLASS_PER_INTERFACE,
	.idVendor =		cpu_to_le16(DRIVER_VENDOR_NUM),
	.idProduct =		cpu_to_le16(DRIVER_PRODUCT_NUM),
	/* .iManufacturer =	DYNAMIC */
	/* .iProduct =		DYNAMIC */
	.bNumConfigurations =	1,
};

static struct usb_string strings_dev[] = {
	[USB_GADGET_MANUFACTURER_IDX].s	= "Grey Innovation",
	[USB_GADGET_PRODUCT_IDX].s	= "MIDI Gadget",
	[USB_GADGET_SERIAL_IDX].s	= "",
	[STRING_DESCRIPTION_IDX].s	= "MIDI",
	{  } /* end of list */
};

static struct usb_gadget_strings stringtab_dev = {
	.language	= 0x0409,	/* en-us */
	.strings	= strings_dev,
};

static struct usb_gadget_strings *dev_strings[] = {
	&stringtab_dev,
	NULL,
};

static struct usb_function_instance *fi_midi;
static struct usb_function *f_midi;

static int midi_unbind(struct usb_composite_dev *dev)
{
	usb_put_function(f_midi);
	usb_put_function_instance(fi_midi);
	return 0;
}

static struct usb_configuration midi_config = {
	.label		= "MIDI Gadget",
	.bConfigurationValue = 1,
	/* .iConfiguration = DYNAMIC */
	.bmAttributes	= USB_CONFIG_ATT_ONE,
	.MaxPower	= CONFIG_USB_GADGET_VBUS_DRAW,
};

static int midi_bind_config(struct usb_configuration *c)
{
	int status;

	f_midi = usb_get_function(fi_midi);
	if (IS_ERR(f_midi))
		return PTR_ERR(f_midi);

	status = usb_add_function(c, f_midi);
	if (status < 0) {
		usb_put_function(f_midi);
		return status;
	}

	return 0;
}

static int midi_bind(struct usb_composite_dev *cdev)
{
	struct f_midi_opts *midi_opts;
	int status;

	fi_midi = usb_get_function_instance("midi");
	if (IS_ERR(fi_midi))
		return PTR_ERR(fi_midi);

	midi_opts = container_of(fi_midi, struct f_midi_opts, func_inst);
	midi_opts->index = index;
	midi_opts->id = id;
	midi_opts->in_ports = in_ports;
	midi_opts->out_ports = out_ports;
	midi_opts->buflen = buflen;
	midi_opts->qlen = qlen;

	status = usb_string_ids_tab(cdev, strings_dev);
	if (status < 0)
		goto put;
	device_desc.iManufacturer = strings_dev[USB_GADGET_MANUFACTURER_IDX].id;
	device_desc.iProduct = strings_dev[USB_GADGET_PRODUCT_IDX].id;
	midi_config.iConfiguration = strings_dev[STRING_DESCRIPTION_IDX].id;

	status = usb_add_config(cdev, &midi_config, midi_bind_config);
	if (status < 0)
		goto put;
	usb_composite_overwrite_options(cdev, &coverwrite);
	pr_info("%s\n", longname);
	return 0;
put:
	usb_put_function_instance(fi_midi);
	return status;
}

static struct usb_composite_driver midi_driver = {
	.name		= longname,
	.dev		= &device_desc,
	.strings	= dev_strings,
	.max_speed	= USB_SPEED_HIGH,
	.bind		= midi_bind,
	.unbind		= midi_unbind,
};

module_usb_composite_driver(midi_driver);
