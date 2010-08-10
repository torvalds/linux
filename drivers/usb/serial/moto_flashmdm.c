/*
 * Motorola Modem flash mode driver
 *
 * Copyright (C) 2008 Greg Kroah-Hartman <greg@kroah.com>
 * Copyright (C) 2009 Motorola, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/tty.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>
#include <linux/io.h>
#include <mach/io.h>
#if defined(CONFIG_ARCH_OMAP34XX)
#include "../host/ehci-omap.h"
#endif

static struct usb_device_id id_table[] = {
	{USB_DEVICE_AND_INTERFACE_INFO(0x22b8, 0x2db4, 0x0a, 0, 0xfc)},
	{},
};

MODULE_DEVICE_TABLE(usb, id_table);

#define MOTO_FLASHMDM_BULKOUT_SIZE	8192

#if defined(CONFIG_ARCH_OMAP34XX)
/* disable the uhh_sysconfig auto idle bit,
 * so that, host iclk will not be disabled and BP
 * can re-enumerated on AP
 */
static void omap_flashmdm_disable_uhh_smart_idle(void)
{
	u32 sysconfig;

	sysconfig = omap_readl(OMAP_UHH_SYSCONFIG);
	sysconfig &= ~(1 << OMAP_UHH_SYSCONFIG_AUTOIDLE_SHIFT);
	omap_writel(sysconfig, OMAP_UHH_SYSCONFIG);
}
#endif

static int moto_flashmdm_attach(struct usb_serial *serial)
{
	struct usb_serial_port *port = serial->port[0];
	int i;

	if (port->bulk_out_size >= MOTO_FLASHMDM_BULKOUT_SIZE) {
		dev_info(&serial->dev->dev,
			 "bulk_out_size %d\n", port->bulk_out_size);
		return 0;
	}

	kfree(port->bulk_out_buffer);
	port->bulk_out_size = MOTO_FLASHMDM_BULKOUT_SIZE;
	port->bulk_out_buffer = kmalloc(port->bulk_out_size, GFP_KERNEL);
	if (!port->bulk_out_buffer) {
		dev_err(&serial->dev->dev,
			"Couldn't allocate bulk_out_buffer\n");
		return -ENOMEM;
	}
	usb_fill_bulk_urb(port->write_urb, serial->dev,
			  usb_sndbulkpipe(serial->dev,
					  port->bulk_out_endpointAddress),
			  port->bulk_out_buffer, port->bulk_out_size,
			  serial->type->write_bulk_callback, port);

	for (i = 0; i < ARRAY_SIZE(port->write_urbs); ++i) {
		kfree(port->bulk_out_buffers[i]);
		port->bulk_out_buffers[i] = kmalloc(port->bulk_out_size,
						    GFP_KERNEL);
		if (!port->bulk_out_buffers[i]) {
			dev_err(&serial->dev->dev,
				"Couldn't allocate bulk_out_buffer\n");
			return -ENOMEM;
		}
		usb_fill_bulk_urb(port->write_urbs[i], serial->dev,
				  usb_sndbulkpipe(serial->dev,
						  port->bulk_out_endpointAddress),
				  port->bulk_out_buffers[i], port->bulk_out_size,
				  serial->type->write_bulk_callback, port);
	}

#if defined(CONFIG_ARCH_OMAP34XX)
	/* need to disable the AUTO IDLE for the usb iclk */
	omap_flashmdm_disable_uhh_smart_idle();
#endif

	return 0;
}

static struct usb_driver moto_flashmdm_driver = {
	.name = "moto-flashmdm",
	.probe = usb_serial_probe,
	.disconnect = usb_serial_disconnect,
	.id_table = id_table,
	.no_dynamic_id = 1,

};

static struct usb_serial_driver moto_flashmdm_device = {
	.driver = {
		   .owner = THIS_MODULE,
		   .name = "moto-flashmdm",
		   },
	.id_table = id_table,
	.num_ports = 1,
	.attach = moto_flashmdm_attach,
};

static int __init moto_flashmdm_init(void)
{
	int retval;

	retval = usb_serial_register(&moto_flashmdm_device);
	if (retval)
		return retval;
	retval = usb_register(&moto_flashmdm_driver);
	if (retval)
		usb_serial_deregister(&moto_flashmdm_device);
	return retval;
}

static void __exit moto_flashmdm_exit(void)
{
	usb_deregister(&moto_flashmdm_driver);
	usb_serial_deregister(&moto_flashmdm_device);
}

module_init(moto_flashmdm_init);
module_exit(moto_flashmdm_exit);
MODULE_LICENSE("GPL");

