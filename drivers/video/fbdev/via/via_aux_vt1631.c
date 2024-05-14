// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2011 Florian Tobias Schandinat <FlorianSchandinat@gmx.de>
 */
/*
 * driver for VIA VT1631 LVDS Transmitter
 */

#include <linux/slab.h>
#include "via_aux.h"


static const char *name = "VT1631 LVDS Transmitter";


void via_aux_vt1631_probe(struct via_aux_bus *bus)
{
	struct via_aux_drv drv = {
		.bus	=	bus,
		.addr	=	0x38,
		.name	=	name};
	/* check vendor id and device id */
	const u8 id[] = {0x06, 0x11, 0x91, 0x31}, len = ARRAY_SIZE(id);
	u8 tmp[ARRAY_SIZE(id)];

	if (!via_aux_read(&drv, 0x00, tmp, len) || memcmp(id, tmp, len))
		return;

	printk(KERN_INFO "viafb: Found %s\n", name);
	via_aux_add(&drv);
}
