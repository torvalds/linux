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
 * infrastructure for devices connected via I2C
 */

#include <linux/slab.h>
#include "via_aux.h"


struct via_aux_bus *via_aux_probe(struct i2c_adapter *adap)
{
	struct via_aux_bus *bus;

	if (!adap)
		return NULL;

	bus = kmalloc(sizeof(*bus), GFP_KERNEL);
	if (!bus)
		return NULL;

	bus->adap = adap;
	INIT_LIST_HEAD(&bus->drivers);

	via_aux_edid_probe(bus);
	via_aux_vt1636_probe(bus);
	via_aux_vt1632_probe(bus);
	via_aux_vt1631_probe(bus);
	via_aux_vt1625_probe(bus);
	via_aux_vt1622_probe(bus);
	via_aux_vt1621_probe(bus);
	via_aux_sii164_probe(bus);
	via_aux_ch7301_probe(bus);

	return bus;
}

void via_aux_free(struct via_aux_bus *bus)
{
	struct via_aux_drv *pos, *n;

	if (!bus)
		return;

	list_for_each_entry_safe(pos, n, &bus->drivers, chain) {
		list_del(&pos->chain);
		kfree(pos);
	}

	kfree(bus);
}
