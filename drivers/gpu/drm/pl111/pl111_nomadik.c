// SPDX-License-Identifier: GPL-2.0+
#include <linux/device.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/bitops.h>
#include <linux/module.h>
#include "pl111_nomadik.h"

#define PMU_CTRL_OFFSET 0x0000
#define PMU_CTRL_LCDNDIF BIT(26)

void pl111_nomadik_init(struct device *dev)
{
	struct regmap *pmu_regmap;

	/*
	 * Just bail out of this is not found, we could be running
	 * multiplatform on something else than Nomadik.
	 */
	pmu_regmap =
		syscon_regmap_lookup_by_compatible("stericsson,nomadik-pmu");
	if (IS_ERR(pmu_regmap))
		return;

	/*
	 * This bit in the PMU controller multiplexes the two graphics
	 * blocks found in the Nomadik STn8815. The other one is called
	 * MDIF (Master Display Interface) and gets muxed out here.
	 */
	regmap_update_bits(pmu_regmap,
			   PMU_CTRL_OFFSET,
			   PMU_CTRL_LCDNDIF,
			   0);
	dev_info(dev, "set Nomadik PMU mux to CLCD mode\n");
}
EXPORT_SYMBOL_GPL(pl111_nomadik_init);
