// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2011 Florian Tobias Schandinat <FlorianSchandinat@gmx.de>
 */
/*
 * driver for VIA VT1632 DVI Transmitter
 */

#include <linux/slab.h>
#include "via_aux.h"


static const char *name = "VT1632 DVI Transmitter";


static void probe(struct via_aux_bus *bus, u8 addr)
{
	struct via_aux_drv drv = {
		.bus	=	bus,
		.addr	=	addr,
		.name	=	name};
	/* check vendor id and device id */
	const u8 id[] = {0x06, 0x11, 0x92, 0x31}, len = ARRAY_SIZE(id);
	u8 tmp[ARRAY_SIZE(id)];

	if (!via_aux_read(&drv, 0x00, tmp, len) || memcmp(id, tmp, len))
		return;

	printk(KERN_INFO "viafb: Found %s at address 0x%x\n", name, addr);
	via_aux_add(&drv);
}

void via_aux_vt1632_probe(struct via_aux_bus *bus)
{
	u8 i;

	for (i = 0x08; i <= 0x0F; i++)
		probe(bus, i);
}
