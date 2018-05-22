// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2017 Linaro Ltd.
 *
 * Author: Linus Walleij <linus.walleij@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/of.h>

#define GLOBAL_WORD_ID				0x00
#define GEMINI_GLOBAL_ARB1_CTRL			0x2c
#define GEMINI_ARB1_BURST_MASK			GENMASK(21, 16)
#define GEMINI_ARB1_BURST_SHIFT			16
/* These all define the priority on the BUS2 backplane */
#define GEMINI_ARB1_PRIO_MASK			GENMASK(9, 0)
#define GEMINI_ARB1_DMAC_HIGH_PRIO		BIT(0)
#define GEMINI_ARB1_IDE_HIGH_PRIO		BIT(1)
#define GEMINI_ARB1_RAID_HIGH_PRIO		BIT(2)
#define GEMINI_ARB1_SECURITY_HIGH_PRIO		BIT(3)
#define GEMINI_ARB1_GMAC0_HIGH_PRIO		BIT(4)
#define GEMINI_ARB1_GMAC1_HIGH_PRIO		BIT(5)
#define GEMINI_ARB1_USB0_HIGH_PRIO		BIT(6)
#define GEMINI_ARB1_USB1_HIGH_PRIO		BIT(7)
#define GEMINI_ARB1_PCI_HIGH_PRIO		BIT(8)
#define GEMINI_ARB1_TVE_HIGH_PRIO		BIT(9)

#define GEMINI_DEFAULT_BURST_SIZE		0x20
#define GEMINI_DEFAULT_PRIO			(GEMINI_ARB1_GMAC0_HIGH_PRIO | \
						 GEMINI_ARB1_GMAC1_HIGH_PRIO)

static int __init gemini_soc_init(void)
{
	struct regmap *map;
	u32 rev;
	u32 val;
	int ret;

	/* Multiplatform guard, only proceed on Gemini */
	if (!of_machine_is_compatible("cortina,gemini"))
		return 0;

	map = syscon_regmap_lookup_by_compatible("cortina,gemini-syscon");
	if (IS_ERR(map))
		return PTR_ERR(map);
	ret = regmap_read(map, GLOBAL_WORD_ID, &rev);
	if (ret)
		return ret;

	val = (GEMINI_DEFAULT_BURST_SIZE << GEMINI_ARB1_BURST_SHIFT) |
		GEMINI_DEFAULT_PRIO;

	/* Set up system arbitration */
	regmap_update_bits(map,
			   GEMINI_GLOBAL_ARB1_CTRL,
			   GEMINI_ARB1_BURST_MASK | GEMINI_ARB1_PRIO_MASK,
			   val);

	pr_info("Gemini SoC %04x revision %02x, set arbitration %08x\n",
		rev >> 8, rev & 0xff, val);

	return 0;
}
subsys_initcall(gemini_soc_init);
