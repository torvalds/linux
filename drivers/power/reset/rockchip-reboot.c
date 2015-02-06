/*
 * Rockchip SoCs Reboot Driver
 *
 * Copyright (C) 2015 Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/regmap.h>
#include <linux/of.h>
#include <linux/mfd/syscon.h>
#include <asm/system_misc.h>

static struct regmap *cru;

#define RK3368_CRU_APLLB_CON3	0x0c
#define RK3368_CRU_APLLL_CON3	0x01c
#define RK3368_CRU_CPLL_CON3	0x03c
#define RK3368_CRU_GPLL_CON3	0x04c
#define RK3368_CRU_NPLL_CON3	0x05c
#define RK3368_CRU_GLB_SRST_FST_VALUE	0x280
#define RK3368_CRU_GLB_SRST_SND_VALUE	0x284
#define RK3368_CRU_GLB_RST_CON	0x388

static void rk3368_reboot(char str, const char *cmd)
{
	/* pll enter slow mode */
	regmap_write(cru, RK3368_CRU_APLLB_CON3, 0x03000000);
	regmap_write(cru, RK3368_CRU_APLLL_CON3, 0x03000000);
	regmap_write(cru, RK3368_CRU_CPLL_CON3, 0x03000000);
	regmap_write(cru, RK3368_CRU_GPLL_CON3, 0x03000000);
	regmap_write(cru, RK3368_CRU_NPLL_CON3, 0x03000000);
	regmap_update_bits(cru, RK3368_CRU_GLB_RST_CON, 3 << 2, 1 << 2);
	regmap_write(cru, RK3368_CRU_GLB_SRST_SND_VALUE, 0xeca8);
}

static __init int rk3368_reboot_init(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;

	cru = syscon_regmap_lookup_by_phandle(np, "rockchip,cru");
	if (IS_ERR(cru)) {
		dev_err(&pdev->dev, "No rockchip,cru phandle specified");
		return PTR_ERR(cru);
	}

	arm_pm_restart = rk3368_reboot;

	return 0;
}

static struct of_device_id rockchip_reboot_of_match[] __refdata = {
	{ .compatible = "rockchip,rk3368-reboot", .data = rk3368_reboot_init },
	{}
};

static int __init rockchip_reboot_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	int (*init)(struct platform_device *);

	match = of_match_node(rockchip_reboot_of_match, pdev->dev.of_node);
	if (match) {
		init = match->data;
		if (init)
			return init(pdev);
	}

	return 0;
}

static struct platform_driver rockchip_reboot_driver = {
	.driver = {
		.name = "rockchip-reboot",
		.of_match_table = rockchip_reboot_of_match,
	},
};

static int __init rockchip_reboot_init(void)
{
	return platform_driver_probe(&rockchip_reboot_driver,
			rockchip_reboot_probe);
}
subsys_initcall(rockchip_reboot_init);
