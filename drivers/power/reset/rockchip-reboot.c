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
#include <linux/rockchip/common.h>
#include <asm/system_misc.h>

/* high 24 bits is tag, low 8 bits is type */
#define SYS_LOADER_REBOOT_FLAG   0x5242C300
#define SYS_KERNRL_REBOOT_FLAG   0xC3524200

enum {
	BOOT_NORMAL = 0,/* normal boot */
	BOOT_LOADER,	/* enter loader rockusb mode */
	BOOT_MASKROM,	/* enter maskrom rockusb mode (not support now) */
	BOOT_RECOVER,	/* enter recover */
	BOOT_NORECOVER,	/* do not enter recover */
	BOOT_SECONDOS,	/* boot second OS (not support now) */
	BOOT_WIPEDATA,	/* enter recover and wipe data */
	BOOT_WIPEALL,	/* enter recover and wipe all data */
	BOOT_CHECKIMG,	/* check firmware img with backup part in loader mode */
	BOOT_FASTBOOT,	/* enter fast boot mode */
	BOOT_SECUREBOOT_DISABLE,
	BOOT_CHARGING,	/* enter charge mode */
	BOOT_MAX,	/* MAX VALID BOOT TYPE */
};

static struct regmap *cru;
static struct regmap *pmugrf;

#define RK3368_CRU_APLLB_CON3	0x0c
#define RK3368_CRU_APLLL_CON3	0x01c
#define RK3368_CRU_CPLL_CON3	0x03c
#define RK3368_CRU_GPLL_CON3	0x04c
#define RK3368_CRU_NPLL_CON3	0x05c
#define RK3368_CRU_GLB_SRST_FST_VALUE	0x280
#define RK3368_CRU_GLB_SRST_SND_VALUE	0x284
#define RK3368_CRU_GLB_RST_CON	0x388
#define RK3368_CRU_GLB_RST_ST	0x38c

#define RK3368_PMUGRF_OS_REG0	0x200
#define RK3368_PMUGRF_OS_REG1	0x204

static void rk3368_reboot(char str, const char *cmd)
{
	u32 flag, mode;

	rockchip_restart_get_boot_mode(cmd, &flag, &mode);
	/* for loader */
	regmap_write(pmugrf, RK3368_PMUGRF_OS_REG0, flag);
	/* for linux */
	regmap_write(pmugrf, RK3368_PMUGRF_OS_REG1, mode);

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
	u32 flag, mode, rst_st;
	struct device_node *np = pdev->dev.of_node;

	cru = syscon_regmap_lookup_by_phandle(np, "rockchip,cru");
	if (IS_ERR(cru)) {
		dev_err(&pdev->dev, "No rockchip,cru phandle specified");
		return PTR_ERR(cru);
	}

	pmugrf = syscon_regmap_lookup_by_phandle(np, "rockchip,pmugrf");
	if (IS_ERR(pmugrf)) {
		dev_err(&pdev->dev, "No rockchip,pmugrf phandle specified");
		return PTR_ERR(pmugrf);
	}

	regmap_read(pmugrf, RK3368_PMUGRF_OS_REG0, &flag);
	regmap_read(pmugrf, RK3368_PMUGRF_OS_REG1, &mode);
	regmap_read(cru, RK3368_CRU_GLB_RST_ST, &rst_st);

	if (flag == (SYS_KERNRL_REBOOT_FLAG | BOOT_RECOVER))
		mode = BOOT_MODE_RECOVERY;
	if (rst_st & ((1 << 4) | (1 << 5)))
		mode = BOOT_MODE_WATCHDOG;
	else if (rst_st & ((1 << 2) | (1 << 3)))
		mode = BOOT_MODE_TSADC;
	rockchip_boot_mode_init(flag, mode);

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
