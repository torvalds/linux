/*
 * Copyright (C) 2016 Broadcom
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/reboot.h>

#define RSTMGR_REG_WR_ACCESS_OFFSET	0
#define RSTMGR_REG_CHIP_SOFT_RST_OFFSET	4

#define RSTMGR_WR_PASSWORD		0xa5a5
#define RSTMGR_WR_PASSWORD_SHIFT	8
#define RSTMGR_WR_ACCESS_ENABLE		1

static void __iomem *kona_reset_base;

static int kona_reset_handler(struct notifier_block *this,
				unsigned long mode, void *cmd)
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

static struct notifier_block kona_reset_nb = {
	.notifier_call = kona_reset_handler,
	.priority = 128,
};

static int kona_reset_probe(struct platform_device *pdev)
{
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	kona_reset_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(kona_reset_base))
		return PTR_ERR(kona_reset_base);

	return register_restart_handler(&kona_reset_nb);
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
