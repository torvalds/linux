// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2011 Florian Tobias Schandinat <FlorianSchandinat@gmx.de>
 */
/*
 * driver for Chrontel CH7301 DVI Transmitter
 */

#include <linux/slab.h>
#include "via_aux.h"


static const char *name = "CH7301 DVI Transmitter";


static void probe(struct via_aux_bus *bus, u8 addr)
{
	struct via_aux_drv drv = {
		.bus	=	bus,
		.addr	=	addr,
		.name	=	name};
	u8 tmp;

	if (!via_aux_read(&drv, 0x4B, &tmp, 1) || tmp != 0x17)
		return;

	printk(KERN_INFO "viafb: Found %s at address 0x%x\n", name, addr);
	via_aux_add(&drv);
}

void via_aux_ch7301_probe(struct via_aux_bus *bus)
{
	probe(bus, 0x75);
	probe(bus, 0x76);
}
