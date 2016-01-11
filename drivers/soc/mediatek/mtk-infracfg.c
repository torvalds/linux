/*
 * Copyright (c) 2015 Pengutronix, Sascha Hauer <kernel@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/export.h>
#include <linux/jiffies.h>
#include <linux/regmap.h>
#include <linux/soc/mediatek/infracfg.h>
#include <asm/processor.h>

#define INFRA_TOPAXI_PROTECTEN		0x0220
#define INFRA_TOPAXI_PROTECTSTA1	0x0228

/**
 * mtk_infracfg_set_bus_protection - enable bus protection
 * @regmap: The infracfg regmap
 * @mask: The mask containing the protection bits to be enabled.
 *
 * This function enables the bus protection bits for disabled power
 * domains so that the system does not hang when some unit accesses the
 * bus while in power down.
 */
int mtk_infracfg_set_bus_protection(struct regmap *infracfg, u32 mask)
{
	unsigned long expired;
	u32 val;
	int ret;

	regmap_update_bits(infracfg, INFRA_TOPAXI_PROTECTEN, mask, mask);

	expired = jiffies + HZ;

	while (1) {
		ret = regmap_read(infracfg, INFRA_TOPAXI_PROTECTSTA1, &val);
		if (ret)
			return ret;

		if ((val & mask) == mask)
			break;

		cpu_relax();
		if (time_after(jiffies, expired))
			return -EIO;
	}

	return 0;
}

/**
 * mtk_infracfg_clear_bus_protection - disable bus protection
 * @regmap: The infracfg regmap
 * @mask: The mask containing the protection bits to be disabled.
 *
 * This function disables the bus protection bits previously enabled with
 * mtk_infracfg_set_bus_protection.
 */
int mtk_infracfg_clear_bus_protection(struct regmap *infracfg, u32 mask)
{
	unsigned long expired;
	int ret;

	regmap_update_bits(infracfg, INFRA_TOPAXI_PROTECTEN, mask, 0);

	expired = jiffies + HZ;

	while (1) {
		u32 val;

		ret = regmap_read(infracfg, INFRA_TOPAXI_PROTECTSTA1, &val);
		if (ret)
			return ret;

		if (!(val & mask))
			break;

		cpu_relax();
		if (time_after(jiffies, expired))
			return -EIO;
	}

	return 0;
}
