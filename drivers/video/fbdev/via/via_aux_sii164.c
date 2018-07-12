/*
 * Copyright 2011 Florian Tobias Schandinat <FlorianSchandinat@gmx.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation;
 * either version 2, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTIES OR REPRESENTATIONS; without even
 * the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
/*
 * driver for Silicon Image SiI 164 PanelLink Transmitter
 */

#include <linux/slab.h>
#include "via_aux.h"


static const char *name = "SiI 164 PanelLink Transmitter";


static void probe(struct via_aux_bus *bus, u8 addr)
{
	struct via_aux_drv drv = {
		.bus	=	bus,
		.addr	=	addr,
		.name	=	name};
	/* check vendor id and device id */
	const u8 id[] = {0x01, 0x00, 0x06, 0x00}, len = ARRAY_SIZE(id);
	u8 tmp[ARRAY_SIZE(id)];

	if (!via_aux_read(&drv, 0x00, tmp, len) || memcmp(id, tmp, len))
		return;

	printk(KERN_INFO "viafb: Found %s at address 0x%x\n", name, addr);
	via_aux_add(&drv);
}

void via_aux_sii164_probe(struct via_aux_bus *bus)
{
	u8 i;

	for (i = 0x38; i <= 0x3F; i++)
		probe(bus, i);
}
