// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2011 Florian Tobias Schandinat <FlorianSchandinat@gmx.de>
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
		if (pos->cleanup)
			pos->cleanup(pos);

		list_del(&pos->chain);
		kfree(pos->data);
		kfree(pos);
	}

	kfree(bus);
}

const struct fb_videomode *via_aux_get_preferred_mode(struct via_aux_bus *bus)
{
	struct via_aux_drv *pos;
	const struct fb_videomode *mode = NULL;

	if (!bus)
		return NULL;

	list_for_each_entry(pos, &bus->drivers, chain) {
		if (pos->get_preferred_mode)
			mode = pos->get_preferred_mode(pos);
	}

	return mode;
}
