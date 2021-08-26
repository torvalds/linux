// SPDX-License-Identifier: GPL-2.0-only
/*
 * TI keystone reboot driver
 *
 * Copyright (C) 2014 Texas Instruments Incorporated. https://www.ti.com/
 *
 * Author: Ivan Khoronzhuk <ivan.khoronzhuk@ti.com>
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/of_platform.h>

#define RSTYPE_RG			0x0
#define RSCTRL_RG			0x4
#define RSCFG_RG			0x8
#define RSISO_RG			0xc

#define RSCTRL_KEY_MASK			0x0000ffff
#define RSCTRL_RESET_MASK		BIT(16)
#define RSCTRL_KEY			0x5a69

#define RSMUX_OMODE_MASK		0xe
#define RSMUX_OMODE_RESET_ON		0xa
#define RSMUX_OMODE_RESET_OFF		0x0
#define RSMUX_LOCK_MASK			0x1
#define RSMUX_LOCK_SET			0x1

#define RSCFG_RSTYPE_SOFT		0x300f
#define RSCFG_RSTYPE_HARD		0x0

#define WDT_MUX_NUMBER			0x4

static int rspll_offset;
static struct regmap *pllctrl_regs;

/**
 * rsctrl_enable_rspll_write - enable access to RSCTRL, RSCFG
 * To be able to access to RSCTRL, RSCFG registers
 * we have to write a key before
 */
static inline int rsctrl_enable_rspll_write(void)
{
	return regmap_update_bits(pllctrl_regs, rspll_offset + RSCTRL_RG,
				  RSCTRL_KEY_MASK, RSCTRL_KEY);
}

static int rsctrl_restart_handler(struct notifier_block *this,
				  unsigned long mode, void *cmd)
{
	/* enable write access to RSTCTRL */
	rsctrl_enable_rspll_write();

	/* reset the SOC */
	regmap_update_bits(pllctrl_regs, rspll_offset + RSCTRL_RG,
			   RSCTRL_RESET_MASK, 0);

	return NOTIFY_DONE;
}

static struct notifier_block rsctrl_restart_nb = {
	.notifier_call = rsctrl_restart_handler,
	.priority = 128,
};

static const struct of_device_id rsctrl_of_match[] = {
	{.compatible = "ti,keystone-reset", },
	{},
};
MODULE_DEVICE_TABLE(of, rsctrl_of_match);

static int rsctrl_probe(struct platform_device *pdev)
{
	int i;
	int ret;
	u32 val;
	unsigned int rg;
	u32 rsmux_offset;
	struct regmap *devctrl_regs;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;

	if (!np)
		return -ENODEV;

	/* get regmaps */
	pllctrl_regs = syscon_regmap_lookup_by_phandle(np, "ti,syscon-pll");
	if (IS_ERR(pllctrl_regs))
		return PTR_ERR(pllctrl_regs);

	devctrl_regs = syscon_regmap_lookup_by_phandle(np, "ti,syscon-dev");
	if (IS_ERR(devctrl_regs))
		return PTR_ERR(devctrl_regs);

	ret = of_property_read_u32_index(np, "ti,syscon-pll", 1, &rspll_offset);
	if (ret) {
		dev_err(dev, "couldn't read the reset pll offset!\n");
		return -EINVAL;
	}

	ret = of_property_read_u32_index(np, "ti,syscon-dev", 1, &rsmux_offset);
	if (ret) {
		dev_err(dev, "couldn't read the rsmux offset!\n");
		return -EINVAL;
	}

	/* set soft/hard reset */
	val = of_property_read_bool(np, "ti,soft-reset");
	val = val ? RSCFG_RSTYPE_SOFT : RSCFG_RSTYPE_HARD;

	ret = rsctrl_enable_rspll_write();
	if (ret)
		return ret;

	ret = regmap_write(pllctrl_regs, rspll_offset + RSCFG_RG, val);
	if (ret)
		return ret;

	/* disable a reset isolation for all module clocks */
	ret = regmap_write(pllctrl_regs, rspll_offset + RSISO_RG, 0);
	if (ret)
		return ret;

	/* enable a reset for watchdogs from wdt-list */
	for (i = 0; i < WDT_MUX_NUMBER; i++) {
		ret = of_property_read_u32_index(np, "ti,wdt-list", i, &val);
		if (ret == -EOVERFLOW && !i) {
			dev_err(dev, "ti,wdt-list property has to contain at"
				"least one entry\n");
			return -EINVAL;
		} else if (ret) {
			break;
		}

		if (val >= WDT_MUX_NUMBER) {
			dev_err(dev, "ti,wdt-list property can contain "
				"only numbers < 4\n");
			return -EINVAL;
		}

		rg = rsmux_offset + val * 4;

		ret = regmap_update_bits(devctrl_regs, rg, RSMUX_OMODE_MASK,
					 RSMUX_OMODE_RESET_ON |
					 RSMUX_LOCK_SET);
		if (ret)
			return ret;
	}

	ret = register_restart_handler(&rsctrl_restart_nb);
	if (ret)
		dev_err(dev, "cannot register restart handler (err=%d)\n", ret);

	return ret;
}

static struct platform_driver rsctrl_driver = {
	.probe = rsctrl_probe,
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = rsctrl_of_match,
	},
};
module_platform_driver(rsctrl_driver);

MODULE_AUTHOR("Ivan Khoronzhuk <ivan.khoronzhuk@ti.com>");
MODULE_DESCRIPTION("Texas Instruments keystone reset driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" KBUILD_MODNAME);
