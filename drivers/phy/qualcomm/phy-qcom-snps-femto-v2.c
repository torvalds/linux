// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/slab.h>

#define USB2_PHY_USB_PHY_UTMI_CTRL0		(0x3c)
#define SLEEPM					BIT(0)
#define OPMODE_MASK				GENMASK(4, 3)
#define OPMODE_NORMAL				(0x00)
#define OPMODE_NONDRIVING			BIT(3)
#define TERMSEL					BIT(5)

#define USB2_PHY_USB_PHY_UTMI_CTRL1		(0x40)
#define XCVRSEL					BIT(0)

#define USB2_PHY_USB_PHY_UTMI_CTRL5		(0x50)
#define POR					BIT(1)

#define USB2_PHY_USB_PHY_HS_PHY_CTRL_COMMON0	(0x54)
#define RETENABLEN				BIT(3)
#define FSEL_MASK				GENMASK(6, 4)
#define FSEL_DEFAULT				(0x3 << 4)

#define USB2_PHY_USB_PHY_HS_PHY_CTRL_COMMON1	(0x58)
#define VBUSVLDEXTSEL0				BIT(4)
#define PLLBTUNE				BIT(5)

#define USB2_PHY_USB_PHY_HS_PHY_CTRL_COMMON2	(0x5c)
#define VREGBYPASS				BIT(0)

#define USB2_PHY_USB_PHY_HS_PHY_CTRL1		(0x60)
#define VBUSVLDEXT0				BIT(0)

#define USB2_PHY_USB_PHY_HS_PHY_CTRL2		(0x64)
#define USB2_AUTO_RESUME			BIT(0)
#define USB2_SUSPEND_N				BIT(2)
#define USB2_SUSPEND_N_SEL			BIT(3)

#define USB2_PHY_USB_PHY_CFG0			(0x94)
#define UTMI_PHY_DATAPATH_CTRL_OVERRIDE_EN	BIT(0)
#define UTMI_PHY_CMN_CTRL_OVERRIDE_EN		BIT(1)

#define USB2_PHY_USB_PHY_REFCLK_CTRL		(0xa0)
#define REFCLK_SEL_MASK				GENMASK(1, 0)
#define REFCLK_SEL_DEFAULT			(0x2 << 0)

static const char * const qcom_snps_hsphy_vreg_names[] = {
	"vdda-pll", "vdda33", "vdda18",
};

#define SNPS_HS_NUM_VREGS		ARRAY_SIZE(qcom_snps_hsphy_vreg_names)

/**
 * struct qcom_snps_hsphy - snps hs phy attributes
 *
 * @dev: device structure
 *
 * @phy: generic phy
 * @base: iomapped memory space for snps hs phy
 *
 * @num_clks: number of clocks
 * @clks: array of clocks
 * @phy_reset: phy reset control
 * @vregs: regulator supplies bulk data
 * @phy_initialized: if PHY has been initialized correctly
 * @mode: contains the current mode the PHY is in
 * @update_seq_cfg: tuning parameters for phy init
 */
struct qcom_snps_hsphy {
	struct device *dev;

	struct phy *phy;
	void __iomem *base;

	int num_clks;
	struct clk_bulk_data *clks;
	struct reset_control *phy_reset;
	struct regulator_bulk_data vregs[SNPS_HS_NUM_VREGS];

	bool phy_initialized;
	enum phy_mode mode;
};

static int qcom_snps_hsphy_clk_init(struct qcom_snps_hsphy *hsphy)
{
	struct device *dev = hsphy->dev;

	hsphy->num_clks = 2;
	hsphy->clks = devm_kcalloc(dev, hsphy->num_clks, sizeof(*hsphy->clks), GFP_KERNEL);
	if (!hsphy->clks)
		return -ENOMEM;

	/*
	 * TODO: Currently no device tree instantiation of the PHY is using the clock.
	 * This needs to be fixed in order for this code to be able to use devm_clk_bulk_get().
	 */
	hsphy->clks[0].id = "cfg_ahb";
	hsphy->clks[0].clk = devm_clk_get_optional(dev, "cfg_ahb");
	if (IS_ERR(hsphy->clks[0].clk))
		return dev_err_probe(dev, PTR_ERR(hsphy->clks[0].clk),
				     "failed to get cfg_ahb clk\n");

	hsphy->clks[1].id = "ref";
	hsphy->clks[1].clk = devm_clk_get(dev, "ref");
	if (IS_ERR(hsphy->clks[1].clk))
		return dev_err_probe(dev, PTR_ERR(hsphy->clks[1].clk),
				     "failed to get ref clk\n");

	return 0;
}

static inline void qcom_snps_hsphy_write_mask(void __iomem *base, u32 offset,
						u32 mask, u32 val)
{
	u32 reg;

	reg = readl_relaxed(base + offset);
	reg &= ~mask;
	reg |= val & mask;
	writel_relaxed(reg, base + offset);

	/* Ensure above write is completed */
	readl_relaxed(base + offset);
}

static int qcom_snps_hsphy_suspend(struct qcom_snps_hsphy *hsphy)
{
	dev_dbg(&hsphy->phy->dev, "Suspend QCOM SNPS PHY\n");

	if (hsphy->mode == PHY_MODE_USB_HOST) {
		/* Enable auto-resume to meet remote wakeup timing */
		qcom_snps_hsphy_write_mask(hsphy->base,
					   USB2_PHY_USB_PHY_HS_PHY_CTRL2,
					   USB2_AUTO_RESUME,
					   USB2_AUTO_RESUME);
		usleep_range(500, 1000);
		qcom_snps_hsphy_write_mask(hsphy->base,
					   USB2_PHY_USB_PHY_HS_PHY_CTRL2,
					   0, USB2_AUTO_RESUME);
	}

	return 0;
}

static int qcom_snps_hsphy_resume(struct qcom_snps_hsphy *hsphy)
{
	dev_dbg(&hsphy->phy->dev, "Resume QCOM SNPS PHY, mode\n");

	return 0;
}

static int __maybe_unused qcom_snps_hsphy_runtime_suspend(struct device *dev)
{
	struct qcom_snps_hsphy *hsphy = dev_get_drvdata(dev);

	if (!hsphy->phy_initialized)
		return 0;

	return qcom_snps_hsphy_suspend(hsphy);
}

static int __maybe_unused qcom_snps_hsphy_runtime_resume(struct device *dev)
{
	struct qcom_snps_hsphy *hsphy = dev_get_drvdata(dev);

	if (!hsphy->phy_initialized)
		return 0;

	return qcom_snps_hsphy_resume(hsphy);
}

static int qcom_snps_hsphy_set_mode(struct phy *phy, enum phy_mode mode,
				    int submode)
{
	struct qcom_snps_hsphy *hsphy = phy_get_drvdata(phy);

	hsphy->mode = mode;
	return 0;
}

static int qcom_snps_hsphy_init(struct phy *phy)
{
	struct qcom_snps_hsphy *hsphy = phy_get_drvdata(phy);
	int ret;

	dev_vdbg(&phy->dev, "%s(): Initializing SNPS HS phy\n", __func__);

	ret = regulator_bulk_enable(ARRAY_SIZE(hsphy->vregs), hsphy->vregs);
	if (ret)
		return ret;

	ret = clk_bulk_prepare_enable(hsphy->num_clks, hsphy->clks);
	if (ret) {
		dev_err(&phy->dev, "failed to enable clocks, %d\n", ret);
		goto poweroff_phy;
	}

	ret = reset_control_assert(hsphy->phy_reset);
	if (ret) {
		dev_err(&phy->dev, "failed to assert phy_reset, %d\n", ret);
		goto disable_clks;
	}

	usleep_range(100, 150);

	ret = reset_control_deassert(hsphy->phy_reset);
	if (ret) {
		dev_err(&phy->dev, "failed to de-assert phy_reset, %d\n", ret);
		goto disable_clks;
	}

	qcom_snps_hsphy_write_mask(hsphy->base, USB2_PHY_USB_PHY_CFG0,
					UTMI_PHY_CMN_CTRL_OVERRIDE_EN,
					UTMI_PHY_CMN_CTRL_OVERRIDE_EN);
	qcom_snps_hsphy_write_mask(hsphy->base, USB2_PHY_USB_PHY_UTMI_CTRL5,
							POR, POR);
	qcom_snps_hsphy_write_mask(hsphy->base,
					USB2_PHY_USB_PHY_HS_PHY_CTRL_COMMON0,
					FSEL_MASK, 0);
	qcom_snps_hsphy_write_mask(hsphy->base,
					USB2_PHY_USB_PHY_HS_PHY_CTRL_COMMON1,
					PLLBTUNE, PLLBTUNE);
	qcom_snps_hsphy_write_mask(hsphy->base, USB2_PHY_USB_PHY_REFCLK_CTRL,
					REFCLK_SEL_DEFAULT, REFCLK_SEL_MASK);
	qcom_snps_hsphy_write_mask(hsphy->base,
					USB2_PHY_USB_PHY_HS_PHY_CTRL_COMMON1,
					VBUSVLDEXTSEL0, VBUSVLDEXTSEL0);
	qcom_snps_hsphy_write_mask(hsphy->base, USB2_PHY_USB_PHY_HS_PHY_CTRL1,
					VBUSVLDEXT0, VBUSVLDEXT0);

	qcom_snps_hsphy_write_mask(hsphy->base,
					USB2_PHY_USB_PHY_HS_PHY_CTRL_COMMON2,
					VREGBYPASS, VREGBYPASS);

	qcom_snps_hsphy_write_mask(hsphy->base, USB2_PHY_USB_PHY_HS_PHY_CTRL2,
					USB2_SUSPEND_N_SEL | USB2_SUSPEND_N,
					USB2_SUSPEND_N_SEL | USB2_SUSPEND_N);

	qcom_snps_hsphy_write_mask(hsphy->base, USB2_PHY_USB_PHY_UTMI_CTRL0,
					SLEEPM, SLEEPM);

	qcom_snps_hsphy_write_mask(hsphy->base, USB2_PHY_USB_PHY_UTMI_CTRL5,
					POR, 0);

	qcom_snps_hsphy_write_mask(hsphy->base, USB2_PHY_USB_PHY_HS_PHY_CTRL2,
					USB2_SUSPEND_N_SEL, 0);

	qcom_snps_hsphy_write_mask(hsphy->base, USB2_PHY_USB_PHY_CFG0,
					UTMI_PHY_CMN_CTRL_OVERRIDE_EN, 0);

	hsphy->phy_initialized = true;

	return 0;

disable_clks:
	clk_bulk_disable_unprepare(hsphy->num_clks, hsphy->clks);
poweroff_phy:
	regulator_bulk_disable(ARRAY_SIZE(hsphy->vregs), hsphy->vregs);

	return ret;
}

static int qcom_snps_hsphy_exit(struct phy *phy)
{
	struct qcom_snps_hsphy *hsphy = phy_get_drvdata(phy);

	reset_control_assert(hsphy->phy_reset);
	clk_bulk_disable_unprepare(hsphy->num_clks, hsphy->clks);
	regulator_bulk_disable(ARRAY_SIZE(hsphy->vregs), hsphy->vregs);
	hsphy->phy_initialized = false;

	return 0;
}

static const struct phy_ops qcom_snps_hsphy_gen_ops = {
	.init		= qcom_snps_hsphy_init,
	.exit		= qcom_snps_hsphy_exit,
	.set_mode	= qcom_snps_hsphy_set_mode,
	.owner		= THIS_MODULE,
};

static const struct of_device_id qcom_snps_hsphy_of_match_table[] = {
	{ .compatible	= "qcom,sm8150-usb-hs-phy", },
	{ .compatible	= "qcom,usb-snps-hs-7nm-phy", },
	{ .compatible	= "qcom,usb-snps-femto-v2-phy",	},
	{ }
};
MODULE_DEVICE_TABLE(of, qcom_snps_hsphy_of_match_table);

static const struct dev_pm_ops qcom_snps_hsphy_pm_ops = {
	SET_RUNTIME_PM_OPS(qcom_snps_hsphy_runtime_suspend,
			   qcom_snps_hsphy_runtime_resume, NULL)
};

static int qcom_snps_hsphy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct qcom_snps_hsphy *hsphy;
	struct phy_provider *phy_provider;
	struct phy *generic_phy;
	int ret, i;
	int num;

	hsphy = devm_kzalloc(dev, sizeof(*hsphy), GFP_KERNEL);
	if (!hsphy)
		return -ENOMEM;

	hsphy->dev = dev;

	hsphy->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(hsphy->base))
		return PTR_ERR(hsphy->base);

	ret = qcom_snps_hsphy_clk_init(hsphy);
	if (ret)
		return dev_err_probe(dev, ret, "failed to initialize clocks\n");

	hsphy->phy_reset = devm_reset_control_get_exclusive(&pdev->dev, NULL);
	if (IS_ERR(hsphy->phy_reset)) {
		dev_err(dev, "failed to get phy core reset\n");
		return PTR_ERR(hsphy->phy_reset);
	}

	num = ARRAY_SIZE(hsphy->vregs);
	for (i = 0; i < num; i++)
		hsphy->vregs[i].supply = qcom_snps_hsphy_vreg_names[i];

	ret = devm_regulator_bulk_get(dev, num, hsphy->vregs);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to get regulator supplies\n");

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	/*
	 * Prevent runtime pm from being ON by default. Users can enable
	 * it using power/control in sysfs.
	 */
	pm_runtime_forbid(dev);

	generic_phy = devm_phy_create(dev, NULL, &qcom_snps_hsphy_gen_ops);
	if (IS_ERR(generic_phy)) {
		ret = PTR_ERR(generic_phy);
		dev_err(dev, "failed to create phy, %d\n", ret);
		return ret;
	}
	hsphy->phy = generic_phy;

	dev_set_drvdata(dev, hsphy);
	phy_set_drvdata(generic_phy, hsphy);

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (!IS_ERR(phy_provider))
		dev_dbg(dev, "Registered Qcom-SNPS HS phy\n");
	else
		pm_runtime_disable(dev);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static struct platform_driver qcom_snps_hsphy_driver = {
	.probe		= qcom_snps_hsphy_probe,
	.driver = {
		.name	= "qcom-snps-hs-femto-v2-phy",
		.pm = &qcom_snps_hsphy_pm_ops,
		.of_match_table = qcom_snps_hsphy_of_match_table,
	},
};

module_platform_driver(qcom_snps_hsphy_driver);

MODULE_DESCRIPTION("Qualcomm SNPS FEMTO USB HS PHY V2 driver");
MODULE_LICENSE("GPL v2");
