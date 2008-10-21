/*
 *	Serial Device Initialisation for Lasi/Asp/Wax/Dino
 *
 *	(c) Copyright Matthew Wilcox <willy@debian.org> 2001-2002
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 */

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/serial_core.h>
#include <linux/signal.h>
#include <linux/slab.h>
#include <linux/types.h>

#include <asm/hardware.h>
#include <asm/parisc-device.h>
#include <asm/io.h>

#include "8250.h"

static int __init serial_init_chip(struct parisc_device *dev)
{
	struct uart_port port;
	unsigned long address;
	int err;

	if (!dev->irq) {
		/* We find some unattached serial ports by walking native
		 * busses.  These should be silently ignored.  Otherwise,
		 * what we have here is a missing parent device, so tell
		 * the user what they're missing.
		 */
		if (parisc_parent(dev)->id.hw_type != HPHW_IOA)
			printk(KERN_INFO
				"Serial: device 0x%lx not configured.\n"
				"Enable support for Wax, Lasi, Asp or Dino.\n",
				dev->hpa.start);
		return -ENODEV;
	}

	address = dev->hpa.start;
	if (dev->id.sversion != 0x8d)
		address += 0x800;

	memset(&port, 0, sizeof(port));
	port.iotype	= UPIO_MEM;
	/* 7.272727MHz on Lasi.  Assumed the same for Dino, Wax and Timi. */
	port.uartclk	= 7272727;
	port.mapbase	= address;
	port.membase	= ioremap_nocache(address, 16);
	port.irq	= dev->irq;
	port.flags	= UPF_BOOT_AUTOCONF;
	port.dev	= &dev->dev;

	err = serial8250_register_port(&port);
	if (err < 0) {
		printk(KERN_WARNING
			"serial8250_register_port returned error %d\n", err);
		iounmap(port.membase);
		return err;
	}

	return 0;
}

static struct parisc_device_id serial_tbl[] = {
	{ HPHW_FIO, HVERSION_REV_ANY_ID, HVERSION_ANY_ID, 0x00075 },
	{ HPHW_FIO, HVERSION_REV_ANY_ID, HVERSION_ANY_ID, 0x0008c },
	{ HPHW_FIO, HVERSION_REV_ANY_ID, HVERSION_ANY_ID, 0x0008d },
	{ 0 }
};

/* Hack.  Some machines have SERIAL_0 attached to Lasi and SERIAL_1
 * attached to Dino.  Unfortunately, Dino appears before Lasi in the device
 * tree.  To ensure that ttyS0 == SERIAL_0, we register two drivers; one
 * which only knows about Lasi and then a second which will find all the
 * other serial ports.  HPUX ignores this problem.
 */
static struct parisc_device_id lasi_tbl[] = {
	{ HPHW_FIO, HVERSION_REV_ANY_ID, 0x03B, 0x0008C }, /* C1xx/C1xxL */
	{ HPHW_FIO, HVERSION_REV_ANY_ID, 0x03C, 0x0008C }, /* B132L */
	{ HPHW_FIO, HVERSION_REV_ANY_ID, 0x03D, 0x0008C }, /* B160L */
	{ HPHW_FIO, HVERSION_REV_ANY_ID, 0x03E, 0x0008C }, /* B132L+ */
	{ HPHW_FIO, HVERSION_REV_ANY_ID, 0x03F, 0x0008C }, /* B180L+ */
	{ HPHW_FIO, HVERSION_REV_ANY_ID, 0x046, 0x0008C }, /* Rocky2 120 */
	{ HPHW_FIO, HVERSION_REV_ANY_ID, 0x047, 0x0008C }, /* Rocky2 150 */
	{ HPHW_FIO, HVERSION_REV_ANY_ID, 0x04E, 0x0008C }, /* Kiji L2 132 */
	{ HPHW_FIO, HVERSION_REV_ANY_ID, 0x056, 0x0008C }, /* Raven+ */
	{ 0 }
};


MODULE_DEVICE_TABLE(parisc, serial_tbl);

static struct parisc_driver lasi_driver = {
	.name		= "serial_1",
	.id_table	= lasi_tbl,
	.probe		= serial_init_chip,
};

static struct parisc_driver serial_driver = {
	.name		= "serial",
	.id_table	= serial_tbl,
	.probe		= serial_init_chip,
};

static int __init probe_serial_gsc(void)
{
	register_parisc_driver(&lasi_driver);
	register_parisc_driver(&serial_driver);
	return 0;
}

module_init(probe_serial_gsc);

MODULE_LICENSE("GPL");
