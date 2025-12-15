// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas RZ/G3E USB3.0 PHY driver
 *
 * Copyright (C) 2025 Renesas Electronics Corporation
 */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>

#define USB3_TEST_RESET				0x0000
#define USB3_TEST_UTMICTRL2			0x0b04
#define USB3_TEST_PRMCTRL5_R			0x0c10
#define USB3_TEST_PRMCTRL6_R			0x0c14

#define USB3_TEST_RSTCTRL			0x1000
#define USB3_TEST_CLKCTRL			0x1004
#define USB3_TEST_RAMCTRL			0x100c
#define USB3_TEST_CREGCTRL			0x1010
#define USB3_TEST_LANECONFIG0			0x1030

#define USB3_TEST_RESET_PORTRESET0_CTRL		BIT(9)
#define USB3_TEST_RESET_SIDDQ			BIT(3)
#define USB3_TEST_RESET_PHY_RESET		BIT(2)
#define USB3_TEST_RESET_PORTRESET0		BIT(1)
#define USB3_TEST_RESET_RELEASE_OVERRIDE	(0)

#define USB3_TEST_UTMICTRL2_CTRL_MASK		GENMASK(9, 8)
#define USB3_TEST_UTMICTRL2_MODE_MASK		GENMASK(1, 0)

#define USB3_TEST_PRMCTRL5_R_TXPREEMPAMPTUNE0_MASK	GENMASK(2, 1)

#define USB3_TEST_PRMCTRL6_R_OTGTUNE0_MASK	GENMASK(2, 0)

#define USB3_TEST_RSTCTRL_HARDRESET_ODEN	BIT(9)
#define USB3_TEST_RSTCTRL_PIPERESET_ODEN	BIT(8)
#define USB3_TEST_RSTCTRL_HARDRESET		BIT(1)
#define USB3_TEST_RSTCTRL_PIPERESET		BIT(0)
#define USB3_TEST_RSTCTRL_ASSERT	\
	(USB3_TEST_RSTCTRL_HARDRESET_ODEN | USB3_TEST_RSTCTRL_PIPERESET_ODEN | \
	 USB3_TEST_RSTCTRL_HARDRESET | USB3_TEST_RSTCTRL_PIPERESET)
#define USB3_TEST_RSTCTRL_RELEASE_HARDRESET	\
	(USB3_TEST_RSTCTRL_HARDRESET_ODEN | USB3_TEST_RSTCTRL_PIPERESET_ODEN | \
	 USB3_TEST_RSTCTRL_PIPERESET)
#define USB3_TEST_RSTCTRL_DEASSERT	\
	(USB3_TEST_RSTCTRL_HARDRESET_ODEN | USB3_TEST_RSTCTRL_PIPERESET_ODEN)
#define USB3_TEST_RSTCTRL_RELEASE_OVERRIDE	(0)

#define USB3_TEST_CLKCTRL_MPLLA_SSC_EN		BIT(2)

#define USB3_TEST_RAMCTRL_SRAM_INIT_DONE	BIT(2)
#define USB3_TEST_RAMCTRL_SRAM_EXT_LD_DONE	BIT(0)

#define USB3_TEST_CREGCTRL_PARA_SEL		BIT(8)

#define USB3_TEST_LANECONFIG0_DEFAULT		(0xd)

struct rz_usb3 {
	void __iomem *base;
	struct reset_control *rstc;
	bool skip_reinit;
};

static void rzg3e_phy_usb2test_phy_init(void __iomem *base)
{
	u32 val;

	val = readl(base + USB3_TEST_UTMICTRL2);
	val |= USB3_TEST_UTMICTRL2_CTRL_MASK | USB3_TEST_UTMICTRL2_MODE_MASK;
	writel(val, base + USB3_TEST_UTMICTRL2);

	val = readl(base + USB3_TEST_PRMCTRL5_R);
	val &= ~USB3_TEST_PRMCTRL5_R_TXPREEMPAMPTUNE0_MASK;
	val |= FIELD_PREP(USB3_TEST_PRMCTRL5_R_TXPREEMPAMPTUNE0_MASK, 2);
	writel(val, base + USB3_TEST_PRMCTRL5_R);

	val = readl(base + USB3_TEST_PRMCTRL6_R);
	val &= ~USB3_TEST_PRMCTRL6_R_OTGTUNE0_MASK;
	val |= FIELD_PREP(USB3_TEST_PRMCTRL6_R_OTGTUNE0_MASK, 7);
	writel(val, base + USB3_TEST_PRMCTRL6_R);

	val = readl(base + USB3_TEST_RESET);
	val &= ~USB3_TEST_RESET_SIDDQ;
	val |= USB3_TEST_RESET_PORTRESET0_CTRL | USB3_TEST_RESET_PHY_RESET |
	       USB3_TEST_RESET_PORTRESET0;
	writel(val, base + USB3_TEST_RESET);
	fsleep(10);

	val &= ~(USB3_TEST_RESET_PHY_RESET | USB3_TEST_RESET_PORTRESET0);
	writel(val, base + USB3_TEST_RESET);
	fsleep(10);

	val = readl(base + USB3_TEST_UTMICTRL2);
	val &= ~USB3_TEST_UTMICTRL2_CTRL_MASK;
	writel(val, base + USB3_TEST_UTMICTRL2);

	writel(USB3_TEST_RESET_RELEASE_OVERRIDE, base + USB3_TEST_RESET);
}

static int rzg3e_phy_usb3test_phy_init(void __iomem *base)
{
	int ret;
	u32 val;

	writel(USB3_TEST_CREGCTRL_PARA_SEL, base + USB3_TEST_CREGCTRL);
	writel(USB3_TEST_RSTCTRL_ASSERT, base + USB3_TEST_RSTCTRL);
	fsleep(20);

	writel(USB3_TEST_CLKCTRL_MPLLA_SSC_EN, base + USB3_TEST_CLKCTRL);
	writel(USB3_TEST_LANECONFIG0_DEFAULT, base + USB3_TEST_LANECONFIG0);
	writel(USB3_TEST_RSTCTRL_RELEASE_HARDRESET, base + USB3_TEST_RSTCTRL);

	ret = readl_poll_timeout_atomic(base + USB3_TEST_RAMCTRL, val,
					val & USB3_TEST_RAMCTRL_SRAM_INIT_DONE, 1, 10000);
	if (ret)
		return ret;

	writel(USB3_TEST_RSTCTRL_DEASSERT, base + USB3_TEST_RSTCTRL);
	writel(USB3_TEST_RAMCTRL_SRAM_EXT_LD_DONE, base + USB3_TEST_RAMCTRL);
	writel(USB3_TEST_RSTCTRL_RELEASE_OVERRIDE, base + USB3_TEST_RSTCTRL);

	return 0;
}

static int rzg3e_phy_usb3_init_helper(void __iomem *base)
{
	rzg3e_phy_usb2test_phy_init(base);

	return rzg3e_phy_usb3test_phy_init(base);
}

static int rzg3e_phy_usb3_init(struct phy *p)
{
	struct rz_usb3 *r = phy_get_drvdata(p);
	int ret = 0;

	if (!r->skip_reinit)
		ret = rzg3e_phy_usb3_init_helper(r->base);

	return ret;
}

static const struct phy_ops rzg3e_phy_usb3_ops = {
	.init = rzg3e_phy_usb3_init,
	.owner = THIS_MODULE,
};

static int rzg3e_phy_usb3_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct phy_provider *provider;
	struct rz_usb3 *r;
	struct phy *phy;
	int ret;

	r = devm_kzalloc(dev, sizeof(*r), GFP_KERNEL);
	if (!r)
		return -ENOMEM;

	r->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(r->base))
		return PTR_ERR(r->base);

	r->rstc = devm_reset_control_get_shared_deasserted(dev, NULL);
	if (IS_ERR(r->rstc))
		return dev_err_probe(dev, PTR_ERR(r->rstc), "failed to get deasserted reset\n");

	/*
	 * devm_phy_create() will call pm_runtime_enable(&phy->dev);
	 * And then, phy-core will manage runtime pm for this device.
	 */
	ret = devm_pm_runtime_enable(dev);
	if (ret < 0)
		return ret;

	phy = devm_phy_create(dev, NULL, &rzg3e_phy_usb3_ops);
	if (IS_ERR(phy))
		return dev_err_probe(dev, PTR_ERR(phy), "failed to create USB3 PHY\n");

	platform_set_drvdata(pdev, r);
	phy_set_drvdata(phy, r);

	provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(provider))
		return dev_err_probe(dev, PTR_ERR(provider), "failed to register PHY provider\n");

	return 0;
}

static int rzg3e_phy_usb3_suspend(struct device *dev)
{
	struct rz_usb3 *r = dev_get_drvdata(dev);

	pm_runtime_put(dev);
	reset_control_assert(r->rstc);
	r->skip_reinit = false;

	return 0;
}

static int rzg3e_phy_usb3_resume(struct device *dev)
{
	struct rz_usb3 *r = dev_get_drvdata(dev);
	int ret;

	ret = reset_control_deassert(r->rstc);
	if (ret)
		return ret;

	ret = pm_runtime_resume_and_get(dev);
	if (ret)
		goto reset_assert;

	ret = rzg3e_phy_usb3_init_helper(r->base);
	if (ret)
		goto pm_put;

	r->skip_reinit = true;

	return 0;

pm_put:
	pm_runtime_put(dev);
reset_assert:
	reset_control_assert(r->rstc);
	return ret;
}

static const struct dev_pm_ops rzg3e_phy_usb3_pm = {
	NOIRQ_SYSTEM_SLEEP_PM_OPS(rzg3e_phy_usb3_suspend, rzg3e_phy_usb3_resume)
};

static const struct of_device_id rzg3e_phy_usb3_match_table[] = {
	{ .compatible = "renesas,r9a09g047-usb3-phy" },
	{ /* Sentinel */ }
};

MODULE_DEVICE_TABLE(of, rzg3e_phy_usb3_match_table);
static struct platform_driver rzg3e_phy_usb3_driver = {
	.driver = {
		.name = "phy_rzg3e_usb3",
		.of_match_table = rzg3e_phy_usb3_match_table,
		.pm = pm_sleep_ptr(&rzg3e_phy_usb3_pm),
	},
	.probe	= rzg3e_phy_usb3_probe,
};
module_platform_driver(rzg3e_phy_usb3_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Renesas RZ/G3E USB3.0 PHY Driver");
MODULE_AUTHOR("biju.das.jz@bp.renesas.com>");
