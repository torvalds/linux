// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2011 Florian Tobias Schandinat <FlorianSchandinat@gmx.de>
 */
/*
 * driver for VIA VT1621(M) TV Encoder
 */

#include <linux/slab.h>
#include "via_aux.h"


static const char *name = "VT1621(M) TV Encoder";


void via_aux_vt1621_probe(struct via_aux_bus *bus)
{
	struct via_aux_drv drv = {
		.bus	=	bus,
		.addr	=	0x20,
		.name	=	name};
	u8 tmp;

	if (!via_aux_read(&drv, 0x1B, &tmp, 1) || tmp != 0x02)
		return;

	printk(KERN_INFO "viafb: Found %s\n", name);
	via_aux_add(&drv);
}
