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

#include <plat/cpu.h>
#include <plat/irqs.h>

#include <mach/map.h>

#include "sata_phy.h"

#define	SATA_TIME_LIMIT		1000

static struct i2c_client *i2c_client;

static struct i2c_driver sataphy_i2c_driver;

struct exynos_sata_phy {
	void __iomem *mmio;
	struct resource *mem;
	struct clk *clk;
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
	u16 time_limit_cnt = 0;
	while (!sata_is_reg(base, reg, checkbit, status)) {
		if (time_limit_cnt == SATA_TIME_LIMIT)
			return false;
		udelay(1000);
		time_limit_cnt++;
	}
	return true;
}

static int sataphy_init(struct sata_phy *phy)
{
	int ret;
	u32 val;

	/* Values to be written to enable 40 bits interface */
	u8 buf[] = { 0x3A, 0x0B };

	struct exynos_sata_phy *sata_phy;

	if (!i2c_client)
		return -EPROBE_DEFER;

	sata_phy = (struct exynos_sata_phy *)phy->priv_data;

	clk_enable(sata_phy->clk);

	if (sata_is_reg(sata_phy->mmio , EXYNOS5_SATA_CTRL0,
		CTRL0_P0_PHY_CALIBRATED, CTRL0_P0_PHY_CALIBRATED))
		return 0;

	writel(S5P_PMU_SATA_PHY_CONTROL_EN, EXYNOS5_SATA_PHY_CONTROL);

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

	writel(SATA_PHY_GENERATION3, sata_phy->mmio + EXYNOS5_SATA_MODE0);

	ret = i2c_master_send(i2c_client, buf, sizeof(buf));
	if (ret < 0)
		return -EINVAL;

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

static int sataphy_shutdown(struct sata_phy *phy)
{

	struct exynos_sata_phy *sata_phy;

	sata_phy = (struct exynos_sata_phy *)phy->priv_data;

	clk_disable(sata_phy->clk);

	return 0;
}

static int __init sata_i2c_probe(struct i2c_client *client,
			  const struct i2c_device_id *i2c_id)
{
	i2c_client = client;
	return 0;
}

static int __init sata_phy_probe(struct platform_device *pdev)
{
	struct exynos_sata_phy *sataphy;
	struct sata_phy *phy;
	struct resource *res;
	struct device *dev = &pdev->dev;
	int ret = 0;

	phy = kzalloc(sizeof(struct sata_phy), GFP_KERNEL);
	if (!phy) {
		dev_err(&pdev->dev, "failed to allocate memory\n");
		ret = -ENOMEM;
		goto out;
	}

	sataphy = kzalloc(sizeof(struct exynos_sata_phy), GFP_KERNEL);
	if (!sataphy) {
		dev_err(dev, "failed to allocate memory\n");
		ret = -ENOMEM;
		goto err0;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "Could not find IO resource\n");
		ret = -EINVAL;
		goto err1;
	}

	sataphy->mem = devm_request_mem_region(dev, res->start,
					resource_size(res), pdev->name);
	if (!sataphy->mem) {
		dev_err(dev, "Could not request IO resource\n");
		ret = -EINVAL;
		goto err1;
	}

	sataphy->mmio =
	    devm_ioremap(dev, res->start, resource_size(res));
	if (!sataphy->mmio) {
		dev_err(dev, "failed to remap IO\n");
		ret = -ENOMEM;
		goto err2;
	}

	sataphy->clk = devm_clk_get(dev, "sata-phy");
	if (IS_ERR(sataphy->clk)) {
		dev_err(dev, "failed to get clk for PHY\n");
		ret = PTR_ERR(sataphy->clk);
		goto err3;
	}

	phy->init = sataphy_init;
	phy->shutdown = sataphy_shutdown;
	phy->priv_data = (void *)sataphy;
	phy->dev = dev;

	ret = sata_add_phy(phy, SATA_PHY_GENERATION3);
	if (ret < 0)
		goto err4;

	ret = i2c_add_driver(&sataphy_i2c_driver);
	if (ret < 0)
		goto err5;

	platform_set_drvdata(pdev, phy);

	return ret;

 err5:
	sata_remove_phy(phy);

 err4:
	clk_disable(sataphy->clk);
	devm_clk_put(dev, sataphy->clk);

 err3:
	devm_iounmap(dev, sataphy->mmio);

 err2:
	devm_release_mem_region(dev, res->start, resource_size(res));

 err1:
	kfree(sataphy);

 err0:
	kfree(phy);

 out:
	return ret;
}

static int sata_phy_remove(struct platform_device *pdev)
{
	struct sata_phy *phy;
	struct exynos_sata_phy *sataphy;

	phy = platform_get_drvdata(pdev);

	sataphy = (struct exynos_sata_phy *)phy->priv_data;
	sata_remove_phy(phy);

	kfree(sataphy);
	kfree(phy);

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
	.probe = sata_phy_probe,
	.remove = sata_phy_remove,
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
		   .of_match_table = phy_i2c_device_match,
	},
	.probe = sata_i2c_probe,
	.id_table = phy_i2c_device_match,
};

module_platform_driver(sata_phy_driver);

MODULE_DESCRIPTION("EXYNOS SATA PHY DRIVER");
MODULE_AUTHOR("Vasanth Ananthan, <vasanth.a@samsung.com>");
MODULE_LICENSE("GPL");
