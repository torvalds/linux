// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2014-2023, The Linux Foundation. All rights reserved.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/slab.h>

#define USB2PHY_PORT_UTMI_CTRL1		0x40

#define USB2PHY_PORT_UTMI_CTRL2		0x44
 #define UTMI_ULPI_SEL			BIT(7)
 #define UTMI_TEST_MUX_SEL		BIT(6)

#define HS_PHY_CTRL_REG			0x10
 #define UTMI_OTG_VBUS_VALID		BIT(20)
 #define SW_SESSVLD_SEL			BIT(28)

#define USB_PHY_UTMI_CTRL0		0x3c

#define USB_PHY_UTMI_CTRL5		0x50
 #define POR_EN				BIT(1)

#define USB_PHY_HS_PHY_CTRL_COMMON0	0x54
 #define COMMONONN			BIT(7)
 #define FSEL				BIT(4)
 #define RETENABLEN			BIT(3)
 #define FREQ_24MHZ			(BIT(6) | BIT(4))

#define USB_PHY_HS_PHY_CTRL2		0x64
 #define USB2_SUSPEND_N_SEL		BIT(3)
 #define USB2_SUSPEND_N			BIT(2)
 #define USB2_UTMI_CLK_EN		BIT(1)

#define USB_PHY_CFG0			0x94
 #define UTMI_PHY_OVERRIDE_EN		BIT(1)

#define USB_PHY_REFCLK_CTRL		0xa0
 #define CLKCORE			BIT(1)

#define USB2PHY_PORT_POWERDOWN		0xa4
 #define POWER_UP			BIT(0)
 #define POWER_DOWN			0

#define USB_PHY_FSEL_SEL		0xb8
 #define FREQ_SEL			BIT(0)

#define USB2PHY_USB_PHY_M31_XCFGI_1	0xbc
 #define USB2_0_TX_ENABLE		BIT(2)

#define USB2PHY_USB_PHY_M31_XCFGI_4	0xc8
 #define HSTX_SLEW_RATE_565PS		GENMASK(1, 0)
 #define PLL_CHARGING_PUMP_CURRENT_35UA	GENMASK(4, 3)
 #define ODT_VALUE_38_02_OHM		GENMASK(7, 6)

#define USB2PHY_USB_PHY_M31_XCFGI_5	0xcc
 #define ODT_VALUE_45_02_OHM		BIT(2)
 #define HSTX_PRE_EMPHASIS_LEVEL_0_55MA	BIT(0)

#define USB2PHY_USB_PHY_M31_XCFGI_11	0xe4
 #define XCFG_COARSE_TUNE_NUM		BIT(1)
 #define XCFG_FINE_TUNE_NUM		BIT(3)

struct m31_phy_regs {
	u32 off;
	u32 val;
	u32 delay;
};

struct m31_priv_data {
	bool				ulpi_mode;
	const struct m31_phy_regs	*regs;
	unsigned int			nregs;
};

static struct m31_phy_regs m31_ipq5332_regs[] = {
	{
		USB_PHY_CFG0,
		UTMI_PHY_OVERRIDE_EN,
		0
	},
	{
		USB_PHY_UTMI_CTRL5,
		POR_EN,
		15
	},
	{
		USB_PHY_FSEL_SEL,
		FREQ_SEL,
		0
	},
	{
		USB_PHY_HS_PHY_CTRL_COMMON0,
		COMMONONN | FREQ_24MHZ | RETENABLEN,
		0
	},
	{
		USB_PHY_UTMI_CTRL5,
		POR_EN,
		0
	},
	{
		USB_PHY_HS_PHY_CTRL2,
		USB2_SUSPEND_N_SEL | USB2_SUSPEND_N | USB2_UTMI_CLK_EN,
		0
	},
	{
		USB2PHY_USB_PHY_M31_XCFGI_11,
		XCFG_COARSE_TUNE_NUM  | XCFG_FINE_TUNE_NUM,
		0
	},
	{
		USB2PHY_USB_PHY_M31_XCFGI_4,
		HSTX_SLEW_RATE_565PS | PLL_CHARGING_PUMP_CURRENT_35UA | ODT_VALUE_38_02_OHM,
		0
	},
	{
		USB2PHY_USB_PHY_M31_XCFGI_1,
		USB2_0_TX_ENABLE,
		0
	},
	{
		USB2PHY_USB_PHY_M31_XCFGI_5,
		ODT_VALUE_45_02_OHM | HSTX_PRE_EMPHASIS_LEVEL_0_55MA,
		4
	},
	{
		USB_PHY_UTMI_CTRL5,
		0x0,
		0
	},
	{
		USB_PHY_HS_PHY_CTRL2,
		USB2_SUSPEND_N | USB2_UTMI_CLK_EN,
		0
	},
};

struct m31usb_phy {
	struct phy			*phy;
	void __iomem			*base;
	const struct m31_phy_regs	*regs;
	int				nregs;

	struct regulator		*vreg;
	struct clk			*clk;
	struct reset_control		*reset;

	bool				ulpi_mode;
};

static int m31usb_phy_init(struct phy *phy)
{
	struct m31usb_phy *qphy = phy_get_drvdata(phy);
	const struct m31_phy_regs *regs = qphy->regs;
	int i, ret;

	ret = regulator_enable(qphy->vreg);
	if (ret) {
		dev_err(&phy->dev, "failed to enable regulator, %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(qphy->clk);
	if (ret) {
		regulator_disable(qphy->vreg);
		dev_err(&phy->dev, "failed to enable cfg ahb clock, %d\n", ret);
		return ret;
	}

	/* Perform phy reset */
	reset_control_assert(qphy->reset);
	udelay(5);
	reset_control_deassert(qphy->reset);

	/* configure for ULPI mode if requested */
	if (qphy->ulpi_mode)
		writel(0x0, qphy->base + USB2PHY_PORT_UTMI_CTRL2);

	/* Enable the PHY */
	writel(POWER_UP, qphy->base + USB2PHY_PORT_POWERDOWN);

	/* Turn on phy ref clock */
	for (i = 0; i < qphy->nregs; i++) {
		writel(regs[i].val, qphy->base + regs[i].off);
		if (regs[i].delay)
			udelay(regs[i].delay);
	}

	return 0;
}

static int m31usb_phy_shutdown(struct phy *phy)
{
	struct m31usb_phy *qphy = phy_get_drvdata(phy);

	/* Disable the PHY */
	writel_relaxed(POWER_DOWN, qphy->base + USB2PHY_PORT_POWERDOWN);

	clk_disable_unprepare(qphy->clk);

	regulator_disable(qphy->vreg);

	return 0;
}

static const struct phy_ops m31usb_phy_gen_ops = {
	.power_on	= m31usb_phy_init,
	.power_off	= m31usb_phy_shutdown,
	.owner		= THIS_MODULE,
};

static int m31usb_phy_probe(struct platform_device *pdev)
{
	struct phy_provider *phy_provider;
	const struct m31_priv_data *data;
	struct device *dev = &pdev->dev;
	struct m31usb_phy *qphy;

	qphy = devm_kzalloc(dev, sizeof(*qphy), GFP_KERNEL);
	if (!qphy)
		return -ENOMEM;

	qphy->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(qphy->base))
		return PTR_ERR(qphy->base);

	qphy->reset = devm_reset_control_get_exclusive_by_index(dev, 0);
	if (IS_ERR(qphy->reset))
		return PTR_ERR(qphy->reset);

	qphy->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(qphy->clk))
		return dev_err_probe(dev, PTR_ERR(qphy->clk),
						"failed to get clk\n");

	data = of_device_get_match_data(dev);
	qphy->regs		= data->regs;
	qphy->nregs		= data->nregs;
	qphy->ulpi_mode		= data->ulpi_mode;

	qphy->phy = devm_phy_create(dev, NULL, &m31usb_phy_gen_ops);
	if (IS_ERR(qphy->phy))
		return dev_err_probe(dev, PTR_ERR(qphy->phy),
						"failed to create phy\n");

	qphy->vreg = devm_regulator_get(dev, "vdda-phy");
	if (IS_ERR(qphy->vreg))
		return dev_err_probe(dev, PTR_ERR(qphy->vreg),
						"failed to get vreg\n");

	phy_set_drvdata(qphy->phy, qphy);

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (!IS_ERR(phy_provider))
		dev_info(dev, "Registered M31 USB phy\n");

	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct m31_priv_data m31_ipq5332_data = {
	.ulpi_mode = false,
	.regs = m31_ipq5332_regs,
	.nregs = ARRAY_SIZE(m31_ipq5332_regs),
};

static const struct of_device_id m31usb_phy_id_table[] = {
	{ .compatible = "qcom,ipq5332-usb-hsphy", .data = &m31_ipq5332_data },
	{ },
};
MODULE_DEVICE_TABLE(of, m31usb_phy_id_table);

static struct platform_driver m31usb_phy_driver = {
	.probe = m31usb_phy_probe,
	.driver = {
		.name = "qcom-m31usb-phy",
		.of_match_table = m31usb_phy_id_table,
	},
};

module_platform_driver(m31usb_phy_driver);

MODULE_DESCRIPTION("USB2 Qualcomm M31 HSPHY driver");
MODULE_LICENSE("GPL");
