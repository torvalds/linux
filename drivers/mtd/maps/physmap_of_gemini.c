/*
 * Cortina Systems Gemini OF physmap add-on
 * Copyright (C) 2017 Linus Walleij <linus.walleij@linaro.org>
 *
 * This SoC has an elaborate flash control register, so we need to
 * detect and set it up when booting on this platform.
 */
#include <linux/export.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/mtd/map.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/bitops.h>
#include "physmap_of_gemini.h"

/*
 * The Flash-relevant parts of the global status register
 * These would also be relevant for a NAND driver.
 */
#define GLOBAL_STATUS			0x04
#define FLASH_TYPE_MASK			(0x3 << 24)
#define FLASH_TYPE_NAND_2K		(0x3 << 24)
#define FLASH_TYPE_NAND_512		(0x2 << 24)
#define FLASH_TYPE_PARALLEL		(0x1 << 24)
#define FLASH_TYPE_SERIAL		(0x0 << 24)
/* if parallel */
#define FLASH_WIDTH_16BIT		(1 << 23)	/* else 8 bit */
/* if serial */
#define FLASH_ATMEL			(1 << 23)	/* else STM */

#define FLASH_SIZE_MASK			(0x3 << 21)
#define NAND_256M			(0x3 << 21)	/* and more */
#define NAND_128M			(0x2 << 21)
#define NAND_64M			(0x1 << 21)
#define NAND_32M			(0x0 << 21)
#define ATMEL_16M			(0x3 << 21)	/* and more */
#define ATMEL_8M			(0x2 << 21)
#define ATMEL_4M_2M			(0x1 << 21)
#define ATMEL_1M			(0x0 << 21)	/* and less */
#define STM_32M				(1 << 22)	/* and more */
#define STM_16M				(0 << 22)	/* and less */

#define FLASH_PARALLEL_HIGH_PIN_CNT	(1 << 20)	/* else low pin cnt */

/* Miscellaneous Control Register */
#define GLOBAL_MISC_CTRL		0x30
#define FLASH_PADS_MASK			0x07
#define NAND_PADS_DISABLE		BIT(2)
#define PFLASH_PADS_DISABLE		BIT(1)
#define SFLASH_PADS_DISABLE		BIT(0)

static const struct of_device_id syscon_match[] = {
	{ .compatible = "cortina,gemini-syscon" },
	{ },
};

int of_flash_probe_gemini(struct platform_device *pdev,
			  struct device_node *np,
			  struct map_info *map)
{
	static struct regmap *rmap;
	struct device *dev = &pdev->dev;
	u32 val;
	int ret;

	/* Multiplatform guard */
	if (!of_device_is_compatible(np, "cortina,gemini-flash"))
		return 0;

	rmap = syscon_regmap_lookup_by_phandle(np, "syscon");
	if (IS_ERR(rmap)) {
		dev_err(dev, "no syscon\n");
		return PTR_ERR(rmap);
	}

	ret = regmap_read(rmap, GLOBAL_STATUS, &val);
	if (ret) {
		dev_err(dev, "failed to read global status register\n");
		return -ENODEV;
	}
	dev_dbg(dev, "global status reg: %08x\n", val);

	/*
	 * It would be contradictory if a physmap flash was NOT parallel.
	 */
	if ((val & FLASH_TYPE_MASK) != FLASH_TYPE_PARALLEL) {
		dev_err(dev, "flash is not parallel\n");
		return -ENODEV;
	}

	/*
	 * Complain if DT data and hardware definition is different.
	 */
	if (val & FLASH_WIDTH_16BIT) {
		if (map->bankwidth != 2)
			dev_warn(dev, "flash hardware say flash is 16 bit wide but DT says it is %d bits wide\n",
				 map->bankwidth * 8);
	} else {
		if (map->bankwidth != 1)
			dev_warn(dev, "flash hardware say flash is 8 bit wide but DT says it is %d bits wide\n",
				 map->bankwidth * 8);
	}

	/* Activate parallel (NOR flash) mode */
	ret = regmap_update_bits(rmap, GLOBAL_MISC_CTRL,
				 FLASH_PADS_MASK,
				 SFLASH_PADS_DISABLE | NAND_PADS_DISABLE);
	if (ret) {
		dev_err(dev, "unable to set up physmap pads\n");
		return -ENODEV;
	}

	dev_info(&pdev->dev, "initialized Gemini-specific physmap control\n");

	return 0;
}
