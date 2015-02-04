/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>

#include "cpsw.h"

#define AM33XX_CTRL_MAC_LO_REG(offset, id) ((offset) + 0x8 * (id))
#define AM33XX_CTRL_MAC_HI_REG(offset, id) ((offset) + 0x8 * (id) + 0x4)

int cpsw_am33xx_cm_get_macid(struct device *dev, u16 offset, int slave,
			     u8 *mac_addr)
{
	u32 macid_lo;
	u32 macid_hi;
	struct regmap *syscon;

	syscon = syscon_regmap_lookup_by_phandle(dev->of_node, "syscon");
	if (IS_ERR(syscon)) {
		if (PTR_ERR(syscon) == -ENODEV)
			return 0;
		return PTR_ERR(syscon);
	}

	regmap_read(syscon, AM33XX_CTRL_MAC_LO_REG(offset, slave),
		    &macid_lo);
	regmap_read(syscon, AM33XX_CTRL_MAC_HI_REG(offset, slave),
		    &macid_hi);

	mac_addr[5] = (macid_lo >> 8) & 0xff;
	mac_addr[4] = macid_lo & 0xff;
	mac_addr[3] = (macid_hi >> 24) & 0xff;
	mac_addr[2] = (macid_hi >> 16) & 0xff;
	mac_addr[1] = (macid_hi >> 8) & 0xff;
	mac_addr[0] = macid_hi & 0xff;

	return 0;
}
EXPORT_SYMBOL_GPL(cpsw_am33xx_cm_get_macid);

MODULE_LICENSE("GPL");
