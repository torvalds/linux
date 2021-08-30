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

#define pr_fmt(fmt)		KBUILD_MODNAME ": " fmt

#include <linux/bcma/bcma.h>
#include <linux/brcmphy.h>
#include <linux/etherdevice.h>
#include <linux/of_address.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include "bgmac.h"

#define NICPM_PADRING_CFG		0x00000004
#define NICPM_IOMUX_CTRL		0x00000008

#define NICPM_PADRING_CFG_INIT_VAL	0x74000000
#define NICPM_IOMUX_CTRL_INIT_VAL_AX	0x21880000

#define NICPM_IOMUX_CTRL_INIT_VAL	0x3196e000
#define NICPM_IOMUX_CTRL_SPD_SHIFT	10
#define NICPM_IOMUX_CTRL_SPD_10M	0
#define NICPM_IOMUX_CTRL_SPD_100M	1
#define NICPM_IOMUX_CTRL_SPD_1000M	2

static u32 platform_bgmac_read(struct bgmac *bgmac, u16 offset)
{
	return readl(bgmac->plat.base + offset);
}

static void platform_bgmac_write(struct bgmac *bgmac, u16 offset, u32 value)
{
	writel(value, bgmac->plat.base + offset);
}

static u32 platform_bgmac_idm_read(struct bgmac *bgmac, u16 offset)
{
	return readl(bgmac->plat.idm_base + offset);
}

static void platform_bgmac_idm_write(struct bgmac *bgmac, u16 offset, u32 value)
{
	writel(value, bgmac->plat.idm_base + offset);
}

static bool platform_bgmac_clk_enabled(struct bgmac *bgmac)
{
	if (!bgmac->plat.idm_base)
		return true;

	if ((bgmac_idm_read(bgmac, BCMA_IOCTL) & BGMAC_CLK_EN) != BGMAC_CLK_EN)
		return false;
	if (bgmac_idm_read(bgmac, BCMA_RESET_CTL) & BCMA_RESET_CTL_RESET)
		return false;
	return true;
}

static void platform_bgmac_clk_enable(struct bgmac *bgmac, u32 flags)
{
	u32 val;

	if (!bgmac->plat.idm_base)
		return;

	/* The Reset Control register only contains a single bit to show if the
	 * controller is currently in reset.  Do a sanity check here, just in
	 * case the bootloader happened to leave the device in reset.
	 */
	val = bgmac_idm_read(bgmac, BCMA_RESET_CTL);
	if (val) {
		bgmac_idm_write(bgmac, BCMA_RESET_CTL, 0);
		bgmac_idm_read(bgmac, BCMA_RESET_CTL);
		udelay(1);
	}

	val = bgmac_idm_read(bgmac, BCMA_IOCTL);
	/* Some bits of BCMA_IOCTL set by HW/ATF and should not change */
	val |= flags & ~(BGMAC_AWCACHE | BGMAC_ARCACHE | BGMAC_AWUSER |
			 BGMAC_ARUSER);
	val |= BGMAC_CLK_EN;
	bgmac_idm_write(bgmac, BCMA_IOCTL, val);
	bgmac_idm_read(bgmac, BCMA_IOCTL);
	udelay(1);
}

static void platform_bgmac_cco_ctl_maskset(struct bgmac *bgmac, u32 offset,
					   u32 mask, u32 set)
{
	/* This shouldn't be encountered */
	WARN_ON(1);
}

static u32 platform_bgmac_get_bus_clock(struct bgmac *bgmac)
{
	/* This shouldn't be encountered */
	WARN_ON(1);

	return 0;
}

static void platform_bgmac_cmn_maskset32(struct bgmac *bgmac, u16 offset,
					 u32 mask, u32 set)
{
	/* This shouldn't be encountered */
	WARN_ON(1);
}

static void bgmac_nicpm_speed_set(struct net_device *net_dev)
{
	struct bgmac *bgmac = netdev_priv(net_dev);
	u32 val;

	if (!bgmac->plat.nicpm_base)
		return;

	/* SET RGMII IO CONFIG */
	writel(NICPM_PADRING_CFG_INIT_VAL,
	       bgmac->plat.nicpm_base + NICPM_PADRING_CFG);

	val = NICPM_IOMUX_CTRL_INIT_VAL;
	switch (bgmac->net_dev->phydev->speed) {
	default:
		netdev_err(net_dev, "Unsupported speed. Defaulting to 1000Mb\n");
		fallthrough;
	case SPEED_1000:
		val |= NICPM_IOMUX_CTRL_SPD_1000M << NICPM_IOMUX_CTRL_SPD_SHIFT;
		break;
	case SPEED_100:
		val |= NICPM_IOMUX_CTRL_SPD_100M << NICPM_IOMUX_CTRL_SPD_SHIFT;
		break;
	case SPEED_10:
		val |= NICPM_IOMUX_CTRL_SPD_10M << NICPM_IOMUX_CTRL_SPD_SHIFT;
		break;
	}

	writel(val, bgmac->plat.nicpm_base + NICPM_IOMUX_CTRL);

	bgmac_adjust_link(bgmac->net_dev);
}

static int platform_phy_connect(struct bgmac *bgmac)
{
	struct phy_device *phy_dev;

	if (bgmac->plat.nicpm_base)
		phy_dev = of_phy_get_and_connect(bgmac->net_dev,
						 bgmac->dev->of_node,
						 bgmac_nicpm_speed_set);
	else
		phy_dev = of_phy_get_and_connect(bgmac->net_dev,
						 bgmac->dev->of_node,
						 bgmac_adjust_link);
	if (!phy_dev) {
		dev_err(bgmac->dev, "PHY connection failed\n");
		return -ENODEV;
	}

	return 0;
}

static int bgmac_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct bgmac *bgmac;
	int ret;

	bgmac = bgmac_alloc(&pdev->dev);
	if (!bgmac)
		return -ENOMEM;

	platform_set_drvdata(pdev, bgmac);

	/* Set the features of the 4707 family */
	bgmac->feature_flags |= BGMAC_FEAT_CLKCTLST;
	bgmac->feature_flags |= BGMAC_FEAT_NO_RESET;
	bgmac->feature_flags |= BGMAC_FEAT_CMDCFG_SR_REV4;
	bgmac->feature_flags |= BGMAC_FEAT_TX_MASK_SETUP;
	bgmac->feature_flags |= BGMAC_FEAT_RX_MASK_SETUP;
	bgmac->feature_flags |= BGMAC_FEAT_IDM_MASK;

	bgmac->dev = &pdev->dev;
	bgmac->dma_dev = &pdev->dev;

	ret = of_get_mac_address(np, bgmac->net_dev->dev_addr);
	if (ret)
		dev_warn(&pdev->dev,
			 "MAC address not present in device tree\n");

	bgmac->irq = platform_get_irq(pdev, 0);
	if (bgmac->irq < 0)
		return bgmac->irq;

	bgmac->plat.base =
		devm_platform_ioremap_resource_byname(pdev, "amac_base");
	if (IS_ERR(bgmac->plat.base))
		return PTR_ERR(bgmac->plat.base);

	bgmac->plat.idm_base = devm_platform_ioremap_resource_byname(pdev, "idm_base");
	if (IS_ERR(bgmac->plat.idm_base))
		return PTR_ERR(bgmac->plat.idm_base);
	else
		bgmac->feature_flags &= ~BGMAC_FEAT_IDM_MASK;

	bgmac->plat.nicpm_base = devm_platform_ioremap_resource_byname(pdev, "nicpm_base");
	if (IS_ERR(bgmac->plat.nicpm_base))
		return PTR_ERR(bgmac->plat.nicpm_base);

	bgmac->read = platform_bgmac_read;
	bgmac->write = platform_bgmac_write;
	bgmac->idm_read = platform_bgmac_idm_read;
	bgmac->idm_write = platform_bgmac_idm_write;
	bgmac->clk_enabled = platform_bgmac_clk_enabled;
	bgmac->clk_enable = platform_bgmac_clk_enable;
	bgmac->cco_ctl_maskset = platform_bgmac_cco_ctl_maskset;
	bgmac->get_bus_clock = platform_bgmac_get_bus_clock;
	bgmac->cmn_maskset32 = platform_bgmac_cmn_maskset32;
	if (of_parse_phandle(np, "phy-handle", 0)) {
		bgmac->phy_connect = platform_phy_connect;
	} else {
		bgmac->phy_connect = bgmac_phy_connect_direct;
		bgmac->feature_flags |= BGMAC_FEAT_FORCE_SPEED_2500;
	}

	return bgmac_enet_probe(bgmac);
}

static int bgmac_remove(struct platform_device *pdev)
{
	struct bgmac *bgmac = platform_get_drvdata(pdev);

	bgmac_enet_remove(bgmac);

	return 0;
}

#ifdef CONFIG_PM
static int bgmac_suspend(struct device *dev)
{
	struct bgmac *bgmac = dev_get_drvdata(dev);

	return bgmac_enet_suspend(bgmac);
}

static int bgmac_resume(struct device *dev)
{
	struct bgmac *bgmac = dev_get_drvdata(dev);

	return bgmac_enet_resume(bgmac);
}

static const struct dev_pm_ops bgmac_pm_ops = {
	.suspend = bgmac_suspend,
	.resume = bgmac_resume
};

#define BGMAC_PM_OPS (&bgmac_pm_ops)
#else
#define BGMAC_PM_OPS NULL
#endif /* CONFIG_PM */

static const struct of_device_id bgmac_of_enet_match[] = {
	{.compatible = "brcm,amac",},
	{.compatible = "brcm,nsp-amac",},
	{.compatible = "brcm,ns2-amac",},
	{},
};

MODULE_DEVICE_TABLE(of, bgmac_of_enet_match);

static struct platform_driver bgmac_enet_driver = {
	.driver = {
		.name  = "bgmac-enet",
		.of_match_table = bgmac_of_enet_match,
		.pm = BGMAC_PM_OPS
	},
	.probe = bgmac_probe,
	.remove = bgmac_remove,
};

module_platform_driver(bgmac_enet_driver);
MODULE_LICENSE("GPL");
