/*
 * Copyright (C) 2011 Marvell International Ltd. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/usb/otg.h>
#include <linux/platform_data/mv_usb.h>

#include "mv_u3d_phy.h"

/*
 * struct mv_u3d_phy - transceiver driver state
 * @phy: transceiver structure
 * @dev: The parent device supplied to the probe function
 * @clk: usb phy clock
 * @base: usb phy register memory base
 */
struct mv_u3d_phy {
	struct usb_phy	phy;
	struct mv_usb_platform_data *plat;
	struct device	*dev;
	struct clk	*clk;
	void __iomem	*base;
};

static u32 mv_u3d_phy_read(void __iomem *base, u32 reg)
{
	void __iomem *addr, *data;

	addr = base;
	data = base + 0x4;

	writel_relaxed(reg, addr);
	return readl_relaxed(data);
}

static void mv_u3d_phy_set(void __iomem *base, u32 reg, u32 value)
{
	void __iomem *addr, *data;
	u32 tmp;

	addr = base;
	data = base + 0x4;

	writel_relaxed(reg, addr);
	tmp = readl_relaxed(data);
	tmp |= value;
	writel_relaxed(tmp, data);
}

static void mv_u3d_phy_clear(void __iomem *base, u32 reg, u32 value)
{
	void __iomem *addr, *data;
	u32 tmp;

	addr = base;
	data = base + 0x4;

	writel_relaxed(reg, addr);
	tmp = readl_relaxed(data);
	tmp &= ~value;
	writel_relaxed(tmp, data);
}

static void mv_u3d_phy_write(void __iomem *base, u32 reg, u32 value)
{
	void __iomem *addr, *data;

	addr = base;
	data = base + 0x4;

	writel_relaxed(reg, addr);
	writel_relaxed(value, data);
}

void mv_u3d_phy_shutdown(struct usb_phy *phy)
{
	struct mv_u3d_phy *mv_u3d_phy;
	void __iomem *base;
	u32 val;

	mv_u3d_phy = container_of(phy, struct mv_u3d_phy, phy);
	base = mv_u3d_phy->base;

	/* Power down Reference Analog current, bit 15
	 * Power down PLL, bit 14
	 * Power down Receiver, bit 13
	 * Power down Transmitter, bit 12
	 * of USB3_POWER_PLL_CONTROL register
	 */
	val = mv_u3d_phy_read(base, USB3_POWER_PLL_CONTROL);
	val &= ~(USB3_POWER_PLL_CONTROL_PU);
	mv_u3d_phy_write(base, USB3_POWER_PLL_CONTROL, val);

	if (mv_u3d_phy->clk)
		clk_disable(mv_u3d_phy->clk);
}

static int mv_u3d_phy_init(struct usb_phy *phy)
{
	struct mv_u3d_phy *mv_u3d_phy;
	void __iomem *base;
	u32 val, count;

	/* enable usb3 phy */
	mv_u3d_phy = container_of(phy, struct mv_u3d_phy, phy);

	if (mv_u3d_phy->clk)
		clk_enable(mv_u3d_phy->clk);

	base = mv_u3d_phy->base;

	val = mv_u3d_phy_read(base, USB3_POWER_PLL_CONTROL);
	val &= ~(USB3_POWER_PLL_CONTROL_PU_MASK);
	val |= 0xF << USB3_POWER_PLL_CONTROL_PU_SHIFT;
	mv_u3d_phy_write(base, USB3_POWER_PLL_CONTROL, val);
	udelay(100);

	mv_u3d_phy_write(base, USB3_RESET_CONTROL,
			USB3_RESET_CONTROL_RESET_PIPE);
	udelay(100);

	mv_u3d_phy_write(base, USB3_RESET_CONTROL,
			USB3_RESET_CONTROL_RESET_PIPE
			| USB3_RESET_CONTROL_RESET_PHY);
	udelay(100);

	val = mv_u3d_phy_read(base, USB3_POWER_PLL_CONTROL);
	val &= ~(USB3_POWER_PLL_CONTROL_REF_FREF_SEL_MASK
		| USB3_POWER_PLL_CONTROL_PHY_MODE_MASK);
	val |=  (USB3_PLL_25MHZ << USB3_POWER_PLL_CONTROL_REF_FREF_SEL_SHIFT)
		| (0x5 << USB3_POWER_PLL_CONTROL_PHY_MODE_SHIFT);
	mv_u3d_phy_write(base, USB3_POWER_PLL_CONTROL, val);
	udelay(100);

	mv_u3d_phy_clear(base, USB3_KVCO_CALI_CONTROL,
		USB3_KVCO_CALI_CONTROL_USE_MAX_PLL_RATE_MASK);
	udelay(100);

	val = mv_u3d_phy_read(base, USB3_SQUELCH_FFE);
	val &= ~(USB3_SQUELCH_FFE_FFE_CAP_SEL_MASK
		| USB3_SQUELCH_FFE_FFE_RES_SEL_MASK
		| USB3_SQUELCH_FFE_SQ_THRESH_IN_MASK);
	val |= ((0xD << USB3_SQUELCH_FFE_FFE_CAP_SEL_SHIFT)
		| (0x7 << USB3_SQUELCH_FFE_FFE_RES_SEL_SHIFT)
		| (0x8 << USB3_SQUELCH_FFE_SQ_THRESH_IN_SHIFT));
	mv_u3d_phy_write(base, USB3_SQUELCH_FFE, val);
	udelay(100);

	val = mv_u3d_phy_read(base, USB3_GEN1_SET0);
	val &= ~USB3_GEN1_SET0_G1_TX_SLEW_CTRL_EN_MASK;
	val |= 1 << USB3_GEN1_SET0_G1_TX_EMPH_EN_SHIFT;
	mv_u3d_phy_write(base, USB3_GEN1_SET0, val);
	udelay(100);

	val = mv_u3d_phy_read(base, USB3_GEN2_SET0);
	val &= ~(USB3_GEN2_SET0_G2_TX_AMP_MASK
		| USB3_GEN2_SET0_G2_TX_EMPH_AMP_MASK
		| USB3_GEN2_SET0_G2_TX_SLEW_CTRL_EN_MASK);
	val |= ((0x14 << USB3_GEN2_SET0_G2_TX_AMP_SHIFT)
		| (1 << USB3_GEN2_SET0_G2_TX_AMP_ADJ_SHIFT)
		| (0xA << USB3_GEN2_SET0_G2_TX_EMPH_AMP_SHIFT)
		| (1 << USB3_GEN2_SET0_G2_TX_EMPH_EN_SHIFT));
	mv_u3d_phy_write(base, USB3_GEN2_SET0, val);
	udelay(100);

	mv_u3d_phy_read(base, USB3_TX_EMPPH);
	val &= ~(USB3_TX_EMPPH_AMP_MASK
		| USB3_TX_EMPPH_EN_MASK
		| USB3_TX_EMPPH_AMP_FORCE_MASK
		| USB3_TX_EMPPH_PAR1_MASK
		| USB3_TX_EMPPH_PAR2_MASK);
	val |= ((0xB << USB3_TX_EMPPH_AMP_SHIFT)
		| (1 << USB3_TX_EMPPH_EN_SHIFT)
		| (1 << USB3_TX_EMPPH_AMP_FORCE_SHIFT)
		| (0x1C << USB3_TX_EMPPH_PAR1_SHIFT)
		| (1 << USB3_TX_EMPPH_PAR2_SHIFT));

	mv_u3d_phy_write(base, USB3_TX_EMPPH, val);
	udelay(100);

	val = mv_u3d_phy_read(base, USB3_GEN2_SET1);
	val &= ~(USB3_GEN2_SET1_G2_RX_SELMUPI_MASK
		| USB3_GEN2_SET1_G2_RX_SELMUPF_MASK
		| USB3_GEN2_SET1_G2_RX_SELMUFI_MASK
		| USB3_GEN2_SET1_G2_RX_SELMUFF_MASK);
	val |= ((1 << USB3_GEN2_SET1_G2_RX_SELMUPI_SHIFT)
		| (1 << USB3_GEN2_SET1_G2_RX_SELMUPF_SHIFT)
		| (1 << USB3_GEN2_SET1_G2_RX_SELMUFI_SHIFT)
		| (1 << USB3_GEN2_SET1_G2_RX_SELMUFF_SHIFT));
	mv_u3d_phy_write(base, USB3_GEN2_SET1, val);
	udelay(100);

	val = mv_u3d_phy_read(base, USB3_DIGITAL_LOOPBACK_EN);
	val &= ~USB3_DIGITAL_LOOPBACK_EN_SEL_BITS_MASK;
	val |= 1 << USB3_DIGITAL_LOOPBACK_EN_SEL_BITS_SHIFT;
	mv_u3d_phy_write(base, USB3_DIGITAL_LOOPBACK_EN, val);
	udelay(100);

	val = mv_u3d_phy_read(base, USB3_IMPEDANCE_TX_SSC);
	val &= ~USB3_IMPEDANCE_TX_SSC_SSC_AMP_MASK;
	val |= 0xC << USB3_IMPEDANCE_TX_SSC_SSC_AMP_SHIFT;
	mv_u3d_phy_write(base, USB3_IMPEDANCE_TX_SSC, val);
	udelay(100);

	val = mv_u3d_phy_read(base, USB3_IMPEDANCE_CALI_CTRL);
	val &= ~USB3_IMPEDANCE_CALI_CTRL_IMP_CAL_THR_MASK;
	val |= 0x4 << USB3_IMPEDANCE_CALI_CTRL_IMP_CAL_THR_SHIFT;
	mv_u3d_phy_write(base, USB3_IMPEDANCE_CALI_CTRL, val);
	udelay(100);

	val = mv_u3d_phy_read(base, USB3_PHY_ISOLATION_MODE);
	val &= ~(USB3_PHY_ISOLATION_MODE_PHY_GEN_RX_MASK
		| USB3_PHY_ISOLATION_MODE_PHY_GEN_TX_MASK
		| USB3_PHY_ISOLATION_MODE_TX_DRV_IDLE_MASK);
	val |= ((1 << USB3_PHY_ISOLATION_MODE_PHY_GEN_RX_SHIFT)
		| (1 << USB3_PHY_ISOLATION_MODE_PHY_GEN_TX_SHIFT));
	mv_u3d_phy_write(base, USB3_PHY_ISOLATION_MODE, val);
	udelay(100);

	val = mv_u3d_phy_read(base, USB3_TXDETRX);
	val &= ~(USB3_TXDETRX_VTHSEL_MASK);
	val |= 0x1 << USB3_TXDETRX_VTHSEL_SHIFT;
	mv_u3d_phy_write(base, USB3_TXDETRX, val);
	udelay(100);

	dev_dbg(mv_u3d_phy->dev, "start calibration\n");

calstart:
	/* Perform Manual Calibration */
	mv_u3d_phy_set(base, USB3_KVCO_CALI_CONTROL,
		1 << USB3_KVCO_CALI_CONTROL_CAL_START_SHIFT);

	mdelay(1);

	count = 0;
	while (1) {
		val = mv_u3d_phy_read(base, USB3_KVCO_CALI_CONTROL);
		if (val & (1 << USB3_KVCO_CALI_CONTROL_CAL_DONE_SHIFT))
			break;
		else if (count > 50) {
			dev_dbg(mv_u3d_phy->dev, "calibration failure, retry...\n");
			goto calstart;
		}
		count++;
		mdelay(1);
	}

	/* active PIPE interface */
	mv_u3d_phy_write(base, USB3_PIPE_SM_CTRL,
		1 << USB3_PIPE_SM_CTRL_PHY_INIT_DONE);

	return 0;
}

static int mv_u3d_phy_probe(struct platform_device *pdev)
{
	struct mv_u3d_phy *mv_u3d_phy;
	struct mv_usb_platform_data *pdata;
	struct device *dev = &pdev->dev;
	struct resource *res;
	void __iomem	*phy_base;
	int	ret;

	pdata = pdev->dev.platform_data;
	if (!pdata) {
		dev_err(&pdev->dev, "%s: no platform data defined\n", __func__);
		return -EINVAL;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "missing mem resource\n");
		return -ENODEV;
	}

	phy_base = devm_request_and_ioremap(dev, res);
	if (!phy_base) {
		dev_err(dev, "%s: register mapping failed\n", __func__);
		return -ENXIO;
	}

	mv_u3d_phy = devm_kzalloc(dev, sizeof(*mv_u3d_phy), GFP_KERNEL);
	if (!mv_u3d_phy)
		return -ENOMEM;

	mv_u3d_phy->dev			= &pdev->dev;
	mv_u3d_phy->plat		= pdata;
	mv_u3d_phy->base		= phy_base;
	mv_u3d_phy->phy.dev		= mv_u3d_phy->dev;
	mv_u3d_phy->phy.label		= "mv-u3d-phy";
	mv_u3d_phy->phy.init		= mv_u3d_phy_init;
	mv_u3d_phy->phy.shutdown	= mv_u3d_phy_shutdown;

	ret = usb_add_phy(&mv_u3d_phy->phy, USB_PHY_TYPE_USB3);
	if (ret)
		goto err;

	if (!mv_u3d_phy->clk)
		mv_u3d_phy->clk = clk_get(mv_u3d_phy->dev, "u3dphy");

	platform_set_drvdata(pdev, mv_u3d_phy);

	dev_info(&pdev->dev, "Initialized Marvell USB 3.0 PHY\n");
err:
	return ret;
}

static int __exit mv_u3d_phy_remove(struct platform_device *pdev)
{
	struct mv_u3d_phy *mv_u3d_phy = platform_get_drvdata(pdev);

	usb_remove_phy(&mv_u3d_phy->phy);

	if (mv_u3d_phy->clk) {
		clk_put(mv_u3d_phy->clk);
		mv_u3d_phy->clk = NULL;
	}

	return 0;
}

static struct platform_driver mv_u3d_phy_driver = {
	.probe		= mv_u3d_phy_probe,
	.remove		= mv_u3d_phy_remove,
	.driver		= {
		.name	= "mv-u3d-phy",
		.owner	= THIS_MODULE,
	},
};

module_platform_driver(mv_u3d_phy_driver);
MODULE_DESCRIPTION("Marvell USB 3.0 PHY controller");
MODULE_AUTHOR("Yu Xu <yuxu@marvell.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:mv-u3d-phy");
