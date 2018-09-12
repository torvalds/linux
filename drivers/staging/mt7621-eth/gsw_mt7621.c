/*   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   Copyright (C) 2009-2016 John Crispin <blogic@openwrt.org>
 *   Copyright (C) 2009-2016 Felix Fietkau <nbd@openwrt.org>
 *   Copyright (C) 2013-2016 Michael Lee <igvtee@gmail.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>

#include <ralink_regs.h>

#include "mtk_eth_soc.h"
#include "gsw_mt7620.h"

void mtk_switch_w32(struct mt7620_gsw *gsw, u32 val, unsigned int reg)
{
	iowrite32(val, gsw->base + reg);
}
EXPORT_SYMBOL_GPL(mtk_switch_w32);

u32 mtk_switch_r32(struct mt7620_gsw *gsw, unsigned int reg)
{
	return ioread32(gsw->base + reg);
}
EXPORT_SYMBOL_GPL(mtk_switch_r32);

static irqreturn_t gsw_interrupt_mt7621(int irq, void *_eth)
{
	struct mtk_eth *eth = (struct mtk_eth *)_eth;
	struct mt7620_gsw *gsw = (struct mt7620_gsw *)eth->sw_priv;
	u32 reg, i;

	reg = mt7530_mdio_r32(gsw, MT7530_SYS_INT_STS);

	for (i = 0; i < 5; i++) {
		unsigned int link;

		if ((reg & BIT(i)) == 0)
			continue;

		link = mt7530_mdio_r32(gsw, MT7530_PMSR_P(i)) & 0x1;

		if (link == eth->link[i])
			continue;

		eth->link[i] = link;
		if (link)
			netdev_info(*eth->netdev,
				    "port %d link up\n", i);
		else
			netdev_info(*eth->netdev,
				    "port %d link down\n", i);
	}

	mt7530_mdio_w32(gsw, MT7530_SYS_INT_STS, 0x1f);

	return IRQ_HANDLED;
}

static void mt7621_hw_init(struct mtk_eth *eth, struct mt7620_gsw *gsw,
			   struct device_node *np)
{
	u32 i;
	u32 val;

	/* hardware reset the switch */
	mtk_reset(eth, RST_CTRL_MCM);
	mdelay(10);

	/* reduce RGMII2 PAD driving strength */
	rt_sysc_m32(MT7621_MDIO_DRV_MASK, 0, SYSC_PAD_RGMII2_MDIO);

	/* gpio mux - RGMII1=Normal mode */
	rt_sysc_m32(BIT(14), 0, SYSC_GPIO_MODE);

	/* set GMAC1 RGMII mode */
	rt_sysc_m32(MT7621_GE1_MODE_MASK, 0, SYSC_REG_CFG1);

	/* enable MDIO to control MT7530 */
	rt_sysc_m32(3 << 12, 0, SYSC_GPIO_MODE);

	/* turn off all PHYs */
	for (i = 0; i <= 4; i++) {
		val = _mt7620_mii_read(gsw, i, 0x0);
		val |= BIT(11);
		_mt7620_mii_write(gsw, i, 0x0, val);
	}

	/* reset the switch */
	mt7530_mdio_w32(gsw, MT7530_SYS_CTRL,
			SYS_CTRL_SW_RST | SYS_CTRL_REG_RST);
	usleep_range(10, 20);

	if ((rt_sysc_r32(SYSC_REG_CHIP_REV_ID) & 0xFFFF) == 0x0101) {
		/* GE1, Force 1000M/FD, FC ON, MAX_RX_LENGTH 1536 */
		mtk_switch_w32(gsw, MAC_MCR_FIXED_LINK, MTK_MAC_P2_MCR);
		mt7530_mdio_w32(gsw, MT7530_PMCR_P(6), PMCR_FIXED_LINK);
	} else {
		/* GE1, Force 1000M/FD, FC ON, MAX_RX_LENGTH 1536 */
		mtk_switch_w32(gsw, MAC_MCR_FIXED_LINK_FC, MTK_MAC_P1_MCR);
		mt7530_mdio_w32(gsw, MT7530_PMCR_P(6), PMCR_FIXED_LINK_FC);
	}

	/* GE2, Link down */
	mtk_switch_w32(gsw, MAC_MCR_FORCE_MODE, MTK_MAC_P2_MCR);

	/* Enable Port 6, P5 as GMAC5, P5 disable */
	val = mt7530_mdio_r32(gsw, MT7530_MHWTRAP);
	/* Enable Port 6 */
	val &= ~MHWTRAP_P6_DIS;
	/* Disable Port 5 */
	val |= MHWTRAP_P5_DIS;
	/* manual override of HW-Trap */
	val |= MHWTRAP_MANUAL;
	mt7530_mdio_w32(gsw, MT7530_MHWTRAP, val);

	val = rt_sysc_r32(SYSC_REG_CFG);
	val = (val >> MT7621_XTAL_SHIFT) & MT7621_XTAL_MASK;
	if (val < MT7621_XTAL_25 && val >= MT7621_XTAL_40) {
		/* 40Mhz */

		/* disable MT7530 core clock */
		_mt7620_mii_write(gsw, 0, 13, 0x1f);
		_mt7620_mii_write(gsw, 0, 14, 0x410);
		_mt7620_mii_write(gsw, 0, 13, 0x401f);
		_mt7620_mii_write(gsw, 0, 14, 0x0);

		/* disable MT7530 PLL */
		_mt7620_mii_write(gsw, 0, 13, 0x1f);
		_mt7620_mii_write(gsw, 0, 14, 0x40d);
		_mt7620_mii_write(gsw, 0, 13, 0x401f);
		_mt7620_mii_write(gsw, 0, 14, 0x2020);

		/* for MT7530 core clock = 500Mhz */
		_mt7620_mii_write(gsw, 0, 13, 0x1f);
		_mt7620_mii_write(gsw, 0, 14, 0x40e);
		_mt7620_mii_write(gsw, 0, 13, 0x401f);
		_mt7620_mii_write(gsw, 0, 14, 0x119);

		/* enable MT7530 PLL */
		_mt7620_mii_write(gsw, 0, 13, 0x1f);
		_mt7620_mii_write(gsw, 0, 14, 0x40d);
		_mt7620_mii_write(gsw, 0, 13, 0x401f);
		_mt7620_mii_write(gsw, 0, 14, 0x2820);

		usleep_range(20, 40);

		/* enable MT7530 core clock */
		_mt7620_mii_write(gsw, 0, 13, 0x1f);
		_mt7620_mii_write(gsw, 0, 14, 0x410);
		_mt7620_mii_write(gsw, 0, 13, 0x401f);
	}

	/* RGMII */
	_mt7620_mii_write(gsw, 0, 14, 0x1);

	/* set MT7530 central align */
	mt7530_mdio_m32(gsw, BIT(0), P6ECR_INTF_MODE_RGMII, MT7530_P6ECR);
	mt7530_mdio_m32(gsw, TRGMII_TXCTRL_TXC_INV, 0,
			MT7530_TRGMII_TXCTRL);
	mt7530_mdio_w32(gsw, MT7530_TRGMII_TCK_CTRL, 0x855);

	/* delay setting for 10/1000M */
	mt7530_mdio_w32(gsw, MT7530_P5RGMIIRXCR,
			P5RGMIIRXCR_C_ALIGN | P5RGMIIRXCR_DELAY_2);
	mt7530_mdio_w32(gsw, MT7530_P5RGMIITXCR, 0x14);

	/* lower Tx Driving*/
	mt7530_mdio_w32(gsw, MT7530_TRGMII_TD0_ODT, 0x44);
	mt7530_mdio_w32(gsw, MT7530_TRGMII_TD1_ODT, 0x44);
	mt7530_mdio_w32(gsw, MT7530_TRGMII_TD2_ODT, 0x44);
	mt7530_mdio_w32(gsw, MT7530_TRGMII_TD3_ODT, 0x44);
	mt7530_mdio_w32(gsw, MT7530_TRGMII_TD4_ODT, 0x44);
	mt7530_mdio_w32(gsw, MT7530_TRGMII_TD5_ODT, 0x44);

	/* turn on all PHYs */
	for (i = 0; i <= 4; i++) {
		val = _mt7620_mii_read(gsw, i, 0);
		val &= ~BIT(11);
		_mt7620_mii_write(gsw, i, 0, val);
	}

#define MT7530_NUM_PORTS 8
#define REG_ESW_PORT_PCR(x)    (0x2004 | ((x) << 8))
#define REG_ESW_PORT_PVC(x)    (0x2010 | ((x) << 8))
#define REG_ESW_PORT_PPBV1(x)  (0x2014 | ((x) << 8))
#define MT7530_CPU_PORT                6

	/* This is copied from mt7530_apply_config in libreCMC driver */
	{
		int i;

		for (i = 0; i < MT7530_NUM_PORTS; i++)
			mt7530_mdio_w32(gsw, REG_ESW_PORT_PCR(i), 0x00400000);

		mt7530_mdio_w32(gsw, REG_ESW_PORT_PCR(MT7530_CPU_PORT),
				0x00ff0000);

		for (i = 0; i < MT7530_NUM_PORTS; i++)
			mt7530_mdio_w32(gsw, REG_ESW_PORT_PVC(i), 0x810000c0);
	}

	/* enable irq */
	mt7530_mdio_m32(gsw, 0, 3 << 16, MT7530_TOP_SIG_CTRL);
	mt7530_mdio_w32(gsw, MT7530_SYS_INT_EN, 0x1f);
}

static const struct of_device_id mediatek_gsw_match[] = {
	{ .compatible = "mediatek,mt7621-gsw" },
	{},
};
MODULE_DEVICE_TABLE(of, mediatek_gsw_match);

int mtk_gsw_init(struct mtk_eth *eth)
{
	struct device_node *np = eth->switch_np;
	struct platform_device *pdev = of_find_device_by_node(np);
	struct mt7620_gsw *gsw;

	if (!pdev)
		return -ENODEV;

	if (!of_device_is_compatible(np, mediatek_gsw_match->compatible))
		return -EINVAL;

	gsw = platform_get_drvdata(pdev);
	eth->sw_priv = gsw;

	if (!gsw->irq)
		return -EINVAL;

	request_irq(gsw->irq, gsw_interrupt_mt7621, 0,
		    "gsw", eth);
	disable_irq(gsw->irq);

	mt7621_hw_init(eth, gsw, np);

	enable_irq(gsw->irq);

	return 0;
}
EXPORT_SYMBOL_GPL(mtk_gsw_init);

static int mt7621_gsw_probe(struct platform_device *pdev)
{
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	struct mt7620_gsw *gsw;

	gsw = devm_kzalloc(&pdev->dev, sizeof(struct mt7620_gsw), GFP_KERNEL);
	if (!gsw)
		return -ENOMEM;

	gsw->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(gsw->base))
		return PTR_ERR(gsw->base);

	gsw->dev = &pdev->dev;
	gsw->irq = irq_of_parse_and_map(pdev->dev.of_node, 0);

	platform_set_drvdata(pdev, gsw);

	return 0;
}

static int mt7621_gsw_remove(struct platform_device *pdev)
{
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver gsw_driver = {
	.probe = mt7621_gsw_probe,
	.remove = mt7621_gsw_remove,
	.driver = {
		.name = "mt7621-gsw",
		.of_match_table = mediatek_gsw_match,
	},
};

module_platform_driver(gsw_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("John Crispin <blogic@openwrt.org>");
MODULE_DESCRIPTION("GBit switch driver for Mediatek MT7621 SoC");
