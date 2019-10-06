// SPDX-License-Identifier: GPL-2.0
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
#include <linux/mtd/xip.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/bitops.h>
#include <linux/pinctrl/consumer.h>
#include "physmap-gemini.h"

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

static const struct of_device_id syscon_match[] = {
	{ .compatible = "cortina,gemini-syscon" },
	{ },
};

struct gemini_flash {
	struct device *dev;
	struct pinctrl *p;
	struct pinctrl_state *enabled_state;
	struct pinctrl_state *disabled_state;
};

/* Static local state */
static struct gemini_flash *gf;

static void gemini_flash_enable_pins(void)
{
	int ret;

	if (IS_ERR(gf->enabled_state))
		return;
	ret = pinctrl_select_state(gf->p, gf->enabled_state);
	if (ret)
		dev_err(gf->dev, "failed to enable pins\n");
}

static void gemini_flash_disable_pins(void)
{
	int ret;

	if (IS_ERR(gf->disabled_state))
		return;
	ret = pinctrl_select_state(gf->p, gf->disabled_state);
	if (ret)
		dev_err(gf->dev, "failed to disable pins\n");
}

static map_word __xipram gemini_flash_map_read(struct map_info *map,
					       unsigned long ofs)
{
	map_word ret;

	gemini_flash_enable_pins();
	ret = inline_map_read(map, ofs);
	gemini_flash_disable_pins();

	return ret;
}

static void __xipram gemini_flash_map_write(struct map_info *map,
					    const map_word datum,
					    unsigned long ofs)
{
	gemini_flash_enable_pins();
	inline_map_write(map, datum, ofs);
	gemini_flash_disable_pins();
}

static void __xipram gemini_flash_map_copy_from(struct map_info *map,
						void *to, unsigned long from,
						ssize_t len)
{
	gemini_flash_enable_pins();
	inline_map_copy_from(map, to, from, len);
	gemini_flash_disable_pins();
}

static void __xipram gemini_flash_map_copy_to(struct map_info *map,
					      unsigned long to,
					      const void *from, ssize_t len)
{
	gemini_flash_enable_pins();
	inline_map_copy_to(map, to, from, len);
	gemini_flash_disable_pins();
}

int of_flash_probe_gemini(struct platform_device *pdev,
			  struct device_node *np,
			  struct map_info *map)
{
	struct regmap *rmap;
	struct device *dev = &pdev->dev;
	u32 val;
	int ret;

	/* Multiplatform guard */
	if (!of_device_is_compatible(np, "cortina,gemini-flash"))
		return 0;

	gf = devm_kzalloc(dev, sizeof(*gf), GFP_KERNEL);
	if (!gf)
		return -ENOMEM;
	gf->dev = dev;

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

	gf->p = devm_pinctrl_get(dev);
	if (IS_ERR(gf->p)) {
		dev_err(dev, "no pinctrl handle\n");
		ret = PTR_ERR(gf->p);
		return ret;
	}

	gf->enabled_state = pinctrl_lookup_state(gf->p, "enabled");
	if (IS_ERR(gf->enabled_state))
		dev_err(dev, "no enabled pin control state\n");

	gf->disabled_state = pinctrl_lookup_state(gf->p, "disabled");
	if (IS_ERR(gf->enabled_state)) {
		dev_err(dev, "no disabled pin control state\n");
	} else {
		ret = pinctrl_select_state(gf->p, gf->disabled_state);
		if (ret)
			dev_err(gf->dev, "failed to disable pins\n");
	}

	map->read = gemini_flash_map_read;
	map->write = gemini_flash_map_write;
	map->copy_from = gemini_flash_map_copy_from;
	map->copy_to = gemini_flash_map_copy_to;

	dev_info(dev, "initialized Gemini-specific physmap control\n");

	return 0;
}
