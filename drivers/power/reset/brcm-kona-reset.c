// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2016 Broadcom

#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>

#define RSTMGR_REG_WR_ACCESS_OFFSET	0
#define RSTMGR_REG_CHIP_SOFT_RST_OFFSET	4

#define RSTMGR_WR_PASSWORD		0xa5a5
#define RSTMGR_WR_PASSWORD_SHIFT	8
#define RSTMGR_WR_ACCESS_ENABLE		1

static void __iomem *kona_reset_base;

static int kona_reset_handler(struct sys_off_data *data)
{
	/*
	 * A soft reset is triggered by writing a 0 to bit 0 of the soft reset
	 * register. To write to that register we must first write the password
	 * and the enable bit in the write access enable register.
	 */
	writel((RSTMGR_WR_PASSWORD << RSTMGR_WR_PASSWORD_SHIFT) |
		RSTMGR_WR_ACCESS_ENABLE,
		kona_reset_base + RSTMGR_REG_WR_ACCESS_OFFSET);
	writel(0, kona_reset_base + RSTMGR_REG_CHIP_SOFT_RST_OFFSET);

	return NOTIFY_DONE;
}

static int kona_reset_probe(struct platform_device *pdev)
{
	kona_reset_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(kona_reset_base))
		return PTR_ERR(kona_reset_base);

	return devm_register_sys_off_handler(&pdev->dev, SYS_OFF_MODE_RESTART,
					     128, kona_reset_handler, NULL);
}

static const struct of_device_id of_match[] = {
	{ .compatible = "brcm,bcm21664-resetmgr" },
	{},
};

static struct platform_driver bcm_kona_reset_driver = {
	.probe = kona_reset_probe,
	.driver = {
		.name = "brcm-kona-reset",
		.of_match_table = of_match,
	},
};

builtin_platform_driver(bcm_kona_reset_driver);
