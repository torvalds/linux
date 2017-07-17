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

#define CTRL_MAC_LO_REG(offset, id) ((offset) + 0x8 * (id))
#define CTRL_MAC_HI_REG(offset, id) ((offset) + 0x8 * (id) + 0x4)

static int davinci_emac_3517_get_macid(struct device *dev, u16 offset,
				       int slave, u8 *mac_addr)
{
	u32 macid_lsb;
	u32 macid_msb;
	struct regmap *syscon;

	syscon = syscon_regmap_lookup_by_phandle(dev->of_node, "syscon");
	if (IS_ERR(syscon)) {
		if (PTR_ERR(syscon) == -ENODEV)
			return 0;
		return PTR_ERR(syscon);
	}

	regmap_read(syscon, CTRL_MAC_LO_REG(offset, slave), &macid_lsb);
	regmap_read(syscon, CTRL_MAC_HI_REG(offset, slave), &macid_msb);

	mac_addr[0] = (macid_msb >> 16) & 0xff;
	mac_addr[1] = (macid_msb >> 8)  & 0xff;
	mac_addr[2] = macid_msb & 0xff;
	mac_addr[3] = (macid_lsb >> 16) & 0xff;
	mac_addr[4] = (macid_lsb >> 8)  & 0xff;
	mac_addr[5] = macid_lsb & 0xff;

	return 0;
}

static int cpsw_am33xx_cm_get_macid(struct device *dev, u16 offset, int slave,
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

	regmap_read(syscon, CTRL_MAC_LO_REG(offset, slave), &macid_lo);
	regmap_read(syscon, CTRL_MAC_HI_REG(offset, slave), &macid_hi);

	mac_addr[5] = (macid_lo >> 8) & 0xff;
	mac_addr[4] = macid_lo & 0xff;
	mac_addr[3] = (macid_hi >> 24) & 0xff;
	mac_addr[2] = (macid_hi >> 16) & 0xff;
	mac_addr[1] = (macid_hi >> 8) & 0xff;
	mac_addr[0] = macid_hi & 0xff;

	return 0;
}

int ti_cm_get_macid(struct device *dev, int slave, u8 *mac_addr)
{
	if (of_machine_is_compatible("ti,dm8148"))
		return cpsw_am33xx_cm_get_macid(dev, 0x630, slave, mac_addr);

	if (of_machine_is_compatible("ti,am33xx"))
		return cpsw_am33xx_cm_get_macid(dev, 0x630, slave, mac_addr);

	if (of_device_is_compatible(dev->of_node, "ti,am3517-emac"))
		return davinci_emac_3517_get_macid(dev, 0x110, slave, mac_addr);

	if (of_device_is_compatible(dev->of_node, "ti,dm816-emac"))
		return cpsw_am33xx_cm_get_macid(dev, 0x30, slave, mac_addr);

	if (of_machine_is_compatible("ti,am43"))
		return cpsw_am33xx_cm_get_macid(dev, 0x630, slave, mac_addr);

	if (of_machine_is_compatible("ti,dra7"))
		return davinci_emac_3517_get_macid(dev, 0x514, slave, mac_addr);

	dev_err(dev, "incompatible machine/device type for reading mac address\n");
	return -ENOENT;
}
EXPORT_SYMBOL_GPL(ti_cm_get_macid);

MODULE_LICENSE("GPL");
