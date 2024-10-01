// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2011 Florian Tobias Schandinat <FlorianSchandinat@gmx.de>
 */
/*
 * driver for VIA VT1625(M) HDTV Encoder
 */

#include <linux/slab.h>
#include "via_aux.h"


static const char *name = "VT1625(M) HDTV Encoder";


static void probe(struct via_aux_bus *bus, u8 addr)
{
	struct via_aux_drv drv = {
		.bus	=	bus,
		.addr	=	addr,
		.name	=	name};
	u8 tmp;

	if (!via_aux_read(&drv, 0x1B, &tmp, 1) || tmp != 0x50)
		return;

	printk(KERN_INFO "viafb: Found %s at address 0x%x\n", name, addr);
	via_aux_add(&drv);
}

void via_aux_vt1625_probe(struct via_aux_bus *bus)
{
	probe(bus, 0x20);
	probe(bus, 0x21);
}
