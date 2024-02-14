// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2011 Florian Tobias Schandinat <FlorianSchandinat@gmx.de>
 */
/*
 * driver for VIA VT1636 LVDS Transmitter
 */

#include <linux/slab.h>
#include "via_aux.h"


static const char *name = "VT1636 LVDS Transmitter";


void via_aux_vt1636_probe(struct via_aux_bus *bus)
{
	struct via_aux_drv drv = {
		.bus	=	bus,
		.addr	=	0x40,
		.name	=	name};
	/* check vendor id and device id */
	const u8 id[] = {0x06, 0x11, 0x45, 0x33}, len = ARRAY_SIZE(id);
	u8 tmp[ARRAY_SIZE(id)];

	if (!via_aux_read(&drv, 0x00, tmp, len) || memcmp(id, tmp, len))
		return;

	printk(KERN_INFO "viafb: Found %s\n", name);
	via_aux_add(&drv);
}
