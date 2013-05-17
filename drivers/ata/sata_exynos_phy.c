/*
 * Copyright (c) 2010-2012 Samsung Electronics Co., Ltd.
 *              http://www.samsung.com
 *
 * EXYNOS - SATA PHY controller driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/ahci_platform.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/io.h>
#include <linux/of_address.h>

#include <plat/cpu.h>
#include <plat/irqs.h>

#include <mach/map.h>

#include "sata_phy.h"
#include "sata_exynos_phy.h"

#define	SATA_TIME_LIMIT		1000

static struct i2c_client *i2c_client;

static struct i2c_driver sataphy_i2c_driver;

struct exynos_sata_phy {
	void __iomem *mmio;
	void __iomem *pmureg;
	struct clk *clk;
	struct device *dev;
	struct sata_phy phy;
};

static bool sata_is_reg(void __iomem *base, u32 reg, u32 checkbit, u32 status)
{
	if ((readl(base + reg) & checkbit) == status)
		return true;
	else
		return false;
}

static bool wait_for_reg_status(void __iomem *base, u32 reg, u32 checkbit,
				u32 status)
{
	unsigned long timeout = jiffies + usecs_to_jiffies(1000);

	while (time_before(jiffies, timeout)) {
		if (sata_is_reg(base, reg, checkbit, status))
			return true;
	}

	return false;
}

static int exynos_sataphy_parse_dt(struct device *dev,
					struct exynos_sata_phy *phy)
{
	struct device_node *sataphy_pmu;

	sataphy_pmu = of_get_child_by_name(dev->of_node, "sataphy-pmu");
	if (!sataphy_pmu) {
		dev_err(dev,
			"PMU control register for sata-phy not specified\n");
		return -ENODEV;
	}

	phy->pmureg = of_iomap(sataphy_pmu, 0);

	of_node_put(sataphy_pmu);

	if (!phy->pmureg) {
		dev_err(dev, "Can't get sata-phy pmu control register\n");
		return -EADDRNOTAVAIL;
	}

	return 0;
}

static int exynos_sataphy_init(struct sata_phy *phy)
{
	int ret;
	u32 val;

	/* Values to be written to enable 40 bits interface */
	u8 buf[] = { 0x3A, 0x0B };

	struct exynos_sata_phy *sata_phy;

	if (!i2c_client)
		return -EPROBE_DEFER;

	sata_phy = container_of(phy, struct exynos_sata_phy, phy);

	ret = clk_prepare_enable(sata_phy->clk);
	if (ret < 0) {
		dev_err(phy->dev, "failed to enable source clk\n");
		return ret;
	}

	if (sata_is_reg(sata_phy->mmio , EXYNOS5_SATA_CTRL0,
		CTRL0_P0_PHY_CALIBRATED, CTRL0_P0_PHY_CALIBRATED))
		return 0;

	writel(SATA_PHY_PMU_EN, sata_phy->pmureg);

	val = 0;
	writel(val, sata_phy->mmio + EXYNOS5_SATA_RESET);

	val = readl(sata_phy->mmio + EXYNOS5_SATA_RESET);
	val |= 0xFF;
	writel(val, sata_phy->mmio + EXYNOS5_SATA_RESET);

	val = readl(sata_phy->mmio + EXYNOS5_SATA_RESET);
	val |= LINK_RESET;
	writel(val, sata_phy->mmio + EXYNOS5_SATA_RESET);

	val = readl(sata_phy->mmio + EXYNOS5_SATA_RESET);
	val |= RESET_CMN_RST_N;
	writel(val, sata_phy->mmio + EXYNOS5_SATA_RESET);

	val = readl(sata_phy->mmio + EXYNOS5_SATA_PHSATA_CTRLM);
	val &= ~PHCTRLM_REF_RATE;
	writel(val, sata_phy->mmio + EXYNOS5_SATA_PHSATA_CTRLM);

	/* High speed enable for Gen3 */
	val = readl(sata_phy->mmio + EXYNOS5_SATA_PHSATA_CTRLM);
	val |= PHCTRLM_HIGH_SPEED;
	writel(val, sata_phy->mmio + EXYNOS5_SATA_PHSATA_CTRLM);

	val = readl(sata_phy->mmio + EXYNOS5_SATA_CTRL0);
	val |= CTRL0_P0_PHY_CALIBRATED_SEL | CTRL0_P0_PHY_CALIBRATED;
	writel(val, sata_phy->mmio + EXYNOS5_SATA_CTRL0);

	writel(0x2, sata_phy->mmio + EXYNOS5_SATA_MODE0);

	ret = i2c_master_send(i2c_client, buf, sizeof(buf));
	if (ret < 0)
		return -ENXIO;

	/* release cmu reset */
	val = readl(sata_phy->mmio + EXYNOS5_SATA_RESET);
	val &= ~RESET_CMN_RST_N;
	writel(val, sata_phy->mmio + EXYNOS5_SATA_RESET);

	val = readl(sata_phy->mmio + EXYNOS5_SATA_RESET);
	val |= RESET_CMN_RST_N;
	writel(val, sata_phy->mmio + EXYNOS5_SATA_RESET);

	if (wait_for_reg_status(sata_phy->mmio, EXYNOS5_SATA_PHSATA_STATM,
				PHSTATM_PLL_LOCKED, 1)) {
		return 0;
	}
	return -EINVAL;
}

static int exynos_sataphy_shutdown(struct sata_phy *phy)
{

	struct exynos_sata_phy *sata_phy;

	sata_phy = container_of(phy, struct exynos_sata_phy, phy);
	clk_disable_unprepare(sata_phy->clk);

	return 0;
}

static int exynos_sata_i2c_probe(struct i2c_client *client,
			  const struct i2c_device_id *i2c_id)
{
	i2c_client = client;
	return 0;
}

static int exynos_sata_phy_probe(struct platform_device *pdev)
{
	struct exynos_sata_phy *sataphy;
	struct device *dev = &pdev->dev;
	int ret = 0;
	sataphy = devm_kzalloc(dev, sizeof(struct exynos_sata_phy), GFP_KERNEL);
	if (!sataphy) {
		dev_err(dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	sataphy->mmio = of_iomap(dev->of_node, 0);
	if (!sataphy->mmio) {
		dev_err(dev, "failed to remap IO\n");
		return -EADDRNOTAVAIL;
	}

	ret = exynos_sataphy_parse_dt(dev, sataphy);
	if (ret != 0)
		goto err_iomap;

	sataphy->clk = devm_clk_get(dev, "sata_phyctrl");
	if (IS_ERR(sataphy->clk)) {
		dev_err(dev, "failed to get clk for PHY\n");
		ret = PTR_ERR(sataphy->clk);
		goto err_pmu;
	}

	sataphy->phy.init = exynos_sataphy_init;
	sataphy->phy.shutdown = exynos_sataphy_shutdown;
	sataphy->phy.dev = dev;

	ret = sata_add_phy(&sataphy->phy);
	if (ret < 0) {
		dev_err(dev, "PHY not registered with framework\n");
		goto err_iomap;
	}

	ret = i2c_add_driver(&sataphy_i2c_driver);
	if (ret < 0)
		goto err_phy;

	platform_set_drvdata(pdev, sataphy);

	return ret;

 err_phy:
	sata_remove_phy(&sataphy->phy);

 err_pmu:
	iounmap(sataphy->pmureg);

 err_iomap:
	iounmap(sataphy->mmio);

	return ret;
}

static int exynos_sata_phy_remove(struct platform_device *pdev)
{
	struct exynos_sata_phy *sataphy;

	sataphy = platform_get_drvdata(pdev);
	iounmap(sataphy->mmio);
	i2c_del_driver(&sataphy_i2c_driver);
	sata_remove_phy(&sataphy->phy);

	return 0;
}

static const struct of_device_id sata_phy_of_match[] = {
	{ .compatible = "samsung,exynos5-sata-phy", },
	{},
};

MODULE_DEVICE_TABLE(of, sata_phy_of_match);

static const struct i2c_device_id phy_i2c_device_match[] = {
	{ "sata-phy", 0 },
};

MODULE_DEVICE_TABLE(of, phy_i2c_device_match);

static struct platform_driver sata_phy_driver = {
	.probe = exynos_sata_phy_probe,
	.remove = exynos_sata_phy_remove,
	.driver = {
		   .name = "sata-phy",
		   .owner = THIS_MODULE,
		   .of_match_table = sata_phy_of_match,
	},
};

static struct i2c_driver sataphy_i2c_driver = {
	.driver = {
		   .name = "sata-phy-i2c",
		   .owner = THIS_MODULE,
		   .of_match_table = (void *)phy_i2c_device_match,
	},
	.probe = exynos_sata_i2c_probe,
	.id_table = phy_i2c_device_match,
};

module_platform_driver(sata_phy_driver);

MODULE_DESCRIPTION("EXYNOS SATA PHY DRIVER");
MODULE_AUTHOR("Vasanth Ananthan, <vasanth.a@samsung.com>");
MODULE_LICENSE("GPL");
