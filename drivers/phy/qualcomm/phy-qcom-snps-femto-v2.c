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
#define SIDDQ					BIT(2)
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

#define USB2_PHY_USB_PHY_HS_PHY_OVERRIDE_X0		(0x6c)
#define USB2_PHY_USB_PHY_HS_PHY_OVERRIDE_X1		(0x70)
#define USB2_PHY_USB_PHY_HS_PHY_OVERRIDE_X2		(0x74)
#define USB2_PHY_USB_PHY_HS_PHY_OVERRIDE_X3		(0x78)
#define PARAM_OVRD_MASK				0xFF

#define USB2_PHY_USB_PHY_CFG0			(0x94)
#define UTMI_PHY_DATAPATH_CTRL_OVERRIDE_EN	BIT(0)
#define UTMI_PHY_CMN_CTRL_OVERRIDE_EN		BIT(1)

#define USB2_PHY_USB_PHY_REFCLK_CTRL		(0xa0)
#define REFCLK_SEL_MASK				GENMASK(1, 0)
#define REFCLK_SEL_DEFAULT			(0x2 << 0)

#define HS_DISCONNECT_MASK			GENMASK(2, 0)
#define SQUELCH_DETECTOR_MASK			GENMASK(7, 5)

#define HS_AMPLITUDE_MASK			GENMASK(3, 0)
#define PREEMPHASIS_DURATION_MASK		BIT(5)
#define PREEMPHASIS_AMPLITUDE_MASK		GENMASK(7, 6)

#define HS_RISE_FALL_MASK			GENMASK(1, 0)
#define HS_CROSSOVER_VOLTAGE_MASK		GENMASK(3, 2)
#define HS_OUTPUT_IMPEDANCE_MASK		GENMASK(5, 4)

#define LS_FS_OUTPUT_IMPEDANCE_MASK		GENMASK(3, 0)

static const char * const qcom_snps_hsphy_vreg_names[] = {
	"vdda-pll", "vdda33", "vdda18",
};

#define SNPS_HS_NUM_VREGS		ARRAY_SIZE(qcom_snps_hsphy_vreg_names)

struct override_param {
	s32	value;
	u8	reg_val;
};

struct override_param_map {
	const char *prop_name;
	const struct override_param *param_table;
	u8 table_size;
	u8 reg_offset;
	u8 param_mask;
};

struct phy_override_seq {
	bool	need_update;
	u8	offset;
	u8	value;
	u8	mask;
};

#define NUM_HSPHY_TUNING_PARAMS	(9)

/**
 * struct qcom_snps_hsphy - snps hs phy attributes
 *
 * @phy: generic phy
 * @base: iomapped memory space for snps hs phy
 *
 * @cfg_ahb_clk: AHB2PHY interface clock
 * @ref_clk: phy reference clock
 * @iface_clk: phy interface clock
 * @phy_reset: phy reset control
 * @vregs: regulator supplies bulk data
 * @phy_initialized: if PHY has been initialized correctly
 * @mode: contains the current mode the PHY is in
 */
struct qcom_snps_hsphy {
	struct phy *phy;
	void __iomem *base;

	struct clk *cfg_ahb_clk;
	struct clk *ref_clk;
	struct reset_control *phy_reset;
	struct regulator_bulk_data vregs[SNPS_HS_NUM_VREGS];

	bool phy_initialized;
	enum phy_mode mode;
	struct phy_override_seq update_seq_cfg[NUM_HSPHY_TUNING_PARAMS];
};

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

	clk_disable_unprepare(hsphy->cfg_ahb_clk);
	return 0;
}

static int qcom_snps_hsphy_resume(struct qcom_snps_hsphy *hsphy)
{
	int ret;

	dev_dbg(&hsphy->phy->dev, "Resume QCOM SNPS PHY, mode\n");

	ret = clk_prepare_enable(hsphy->cfg_ahb_clk);
	if (ret) {
		dev_err(&hsphy->phy->dev, "failed to enable cfg ahb clock\n");
		return ret;
	}

	return 0;
}

static int __maybe_unused qcom_snps_hsphy_runtime_suspend(struct device *dev)
{
	struct qcom_snps_hsphy *hsphy = dev_get_drvdata(dev);

	if (!hsphy->phy_initialized)
		return 0;

	qcom_snps_hsphy_suspend(hsphy);
	return 0;
}

static int __maybe_unused qcom_snps_hsphy_runtime_resume(struct device *dev)
{
	struct qcom_snps_hsphy *hsphy = dev_get_drvdata(dev);

	if (!hsphy->phy_initialized)
		return 0;

	qcom_snps_hsphy_resume(hsphy);
	return 0;
}

static int qcom_snps_hsphy_set_mode(struct phy *phy, enum phy_mode mode,
				    int submode)
{
	struct qcom_snps_hsphy *hsphy = phy_get_drvdata(phy);

	hsphy->mode = mode;
	return 0;
}

static const struct override_param hs_disconnect_sc7280[] = {
	{ -272, 0 },
	{ 0, 1 },
	{ 317, 2 },
	{ 630, 3 },
	{ 973, 4 },
	{ 1332, 5 },
	{ 1743, 6 },
	{ 2156, 7 },
};

static const struct override_param squelch_det_threshold_sc7280[] = {
	{ -2090, 7 },
	{ -1560, 6 },
	{ -1030, 5 },
	{ -530, 4 },
	{ 0, 3 },
	{ 530, 2 },
	{ 1060, 1 },
	{ 1590, 0 },
};

static const struct override_param hs_amplitude_sc7280[] = {
	{ -660, 0 },
	{ -440, 1 },
	{ -220, 2 },
	{ 0, 3 },
	{ 230, 4 },
	{ 440, 5 },
	{ 650, 6 },
	{ 890, 7 },
	{ 1110, 8 },
	{ 1330, 9 },
	{ 1560, 10 },
	{ 1780, 11 },
	{ 2000, 12 },
	{ 2220, 13 },
	{ 2430, 14 },
	{ 2670, 15 },
};

static const struct override_param preemphasis_duration_sc7280[] = {
	{ 10000, 1 },
	{ 20000, 0 },
};

static const struct override_param preemphasis_amplitude_sc7280[] = {
	{ 10000, 1 },
	{ 20000, 2 },
	{ 30000, 3 },
	{ 40000, 0 },
};

static const struct override_param hs_rise_fall_time_sc7280[] = {
	{ -4100, 3 },
	{ 0, 2 },
	{ 2810, 1 },
	{ 5430, 0 },
};

static const struct override_param hs_crossover_voltage_sc7280[] = {
	{ -31000, 1 },
	{ 0, 3 },
	{ 28000, 2 },
};

static const struct override_param hs_output_impedance_sc7280[] = {
	{ -2300000, 3 },
	{ 0, 2 },
	{ 2600000, 1 },
	{ 6100000, 0 },
};

static const struct override_param ls_fs_output_impedance_sc7280[] = {
	{ -1053, 15 },
	{ -557, 7 },
	{ 0, 3 },
	{ 612, 1 },
	{ 1310, 0 },
};

static const struct override_param_map sc7280_snps_7nm_phy[] = {
	{
		"qcom,hs-disconnect-bp",
		hs_disconnect_sc7280,
		ARRAY_SIZE(hs_disconnect_sc7280),
		USB2_PHY_USB_PHY_HS_PHY_OVERRIDE_X0,
		HS_DISCONNECT_MASK
	},
	{
		"qcom,squelch-detector-bp",
		squelch_det_threshold_sc7280,
		ARRAY_SIZE(squelch_det_threshold_sc7280),
		USB2_PHY_USB_PHY_HS_PHY_OVERRIDE_X0,
		SQUELCH_DETECTOR_MASK
	},
	{
		"qcom,hs-amplitude-bp",
		hs_amplitude_sc7280,
		ARRAY_SIZE(hs_amplitude_sc7280),
		USB2_PHY_USB_PHY_HS_PHY_OVERRIDE_X1,
		HS_AMPLITUDE_MASK
	},
	{
		"qcom,pre-emphasis-duration-bp",
		preemphasis_duration_sc7280,
		ARRAY_SIZE(preemphasis_duration_sc7280),
		USB2_PHY_USB_PHY_HS_PHY_OVERRIDE_X1,
		PREEMPHASIS_DURATION_MASK,
	},
	{
		"qcom,pre-emphasis-amplitude-bp",
		preemphasis_amplitude_sc7280,
		ARRAY_SIZE(preemphasis_amplitude_sc7280),
		USB2_PHY_USB_PHY_HS_PHY_OVERRIDE_X1,
		PREEMPHASIS_AMPLITUDE_MASK,
	},
	{
		"qcom,hs-rise-fall-time-bp",
		hs_rise_fall_time_sc7280,
		ARRAY_SIZE(hs_rise_fall_time_sc7280),
		USB2_PHY_USB_PHY_HS_PHY_OVERRIDE_X2,
		HS_RISE_FALL_MASK
	},
	{
		"qcom,hs-crossover-voltage-microvolt",
		hs_crossover_voltage_sc7280,
		ARRAY_SIZE(hs_crossover_voltage_sc7280),
		USB2_PHY_USB_PHY_HS_PHY_OVERRIDE_X2,
		HS_CROSSOVER_VOLTAGE_MASK
	},
	{
		"qcom,hs-output-impedance-micro-ohms",
		hs_output_impedance_sc7280,
		ARRAY_SIZE(hs_output_impedance_sc7280),
		USB2_PHY_USB_PHY_HS_PHY_OVERRIDE_X2,
		HS_OUTPUT_IMPEDANCE_MASK,
	},
	{
		"qcom,ls-fs-output-impedance-bp",
		ls_fs_output_impedance_sc7280,
		ARRAY_SIZE(ls_fs_output_impedance_sc7280),
		USB2_PHY_USB_PHY_HS_PHY_OVERRIDE_X3,
		LS_FS_OUTPUT_IMPEDANCE_MASK,
	},
	{},
};

static int qcom_snps_hsphy_init(struct phy *phy)
{
	struct qcom_snps_hsphy *hsphy = phy_get_drvdata(phy);
	int ret, i;

	dev_vdbg(&phy->dev, "%s(): Initializing SNPS HS phy\n", __func__);

	ret = regulator_bulk_enable(ARRAY_SIZE(hsphy->vregs), hsphy->vregs);
	if (ret)
		return ret;

	ret = clk_prepare_enable(hsphy->cfg_ahb_clk);
	if (ret) {
		dev_err(&phy->dev, "failed to enable cfg ahb clock, %d\n", ret);
		goto poweroff_phy;
	}

	ret = reset_control_assert(hsphy->phy_reset);
	if (ret) {
		dev_err(&phy->dev, "failed to assert phy_reset, %d\n", ret);
		goto disable_ahb_clk;
	}

	usleep_range(100, 150);

	ret = reset_control_deassert(hsphy->phy_reset);
	if (ret) {
		dev_err(&phy->dev, "failed to de-assert phy_reset, %d\n", ret);
		goto disable_ahb_clk;
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

	for (i = 0; i < ARRAY_SIZE(hsphy->update_seq_cfg); i++) {
		if (hsphy->update_seq_cfg[i].need_update)
			qcom_snps_hsphy_write_mask(hsphy->base,
					hsphy->update_seq_cfg[i].offset,
					hsphy->update_seq_cfg[i].mask,
					hsphy->update_seq_cfg[i].value);
	}

	qcom_snps_hsphy_write_mask(hsphy->base,
					USB2_PHY_USB_PHY_HS_PHY_CTRL_COMMON2,
					VREGBYPASS, VREGBYPASS);

	qcom_snps_hsphy_write_mask(hsphy->base, USB2_PHY_USB_PHY_HS_PHY_CTRL2,
					USB2_SUSPEND_N_SEL | USB2_SUSPEND_N,
					USB2_SUSPEND_N_SEL | USB2_SUSPEND_N);

	qcom_snps_hsphy_write_mask(hsphy->base, USB2_PHY_USB_PHY_UTMI_CTRL0,
					SLEEPM, SLEEPM);

	qcom_snps_hsphy_write_mask(hsphy->base, USB2_PHY_USB_PHY_HS_PHY_CTRL_COMMON0,
				   SIDDQ, 0);

	qcom_snps_hsphy_write_mask(hsphy->base, USB2_PHY_USB_PHY_UTMI_CTRL5,
					POR, 0);

	qcom_snps_hsphy_write_mask(hsphy->base, USB2_PHY_USB_PHY_HS_PHY_CTRL2,
					USB2_SUSPEND_N_SEL, 0);

	qcom_snps_hsphy_write_mask(hsphy->base, USB2_PHY_USB_PHY_CFG0,
					UTMI_PHY_CMN_CTRL_OVERRIDE_EN, 0);

	hsphy->phy_initialized = true;

	return 0;

disable_ahb_clk:
	clk_disable_unprepare(hsphy->cfg_ahb_clk);
poweroff_phy:
	regulator_bulk_disable(ARRAY_SIZE(hsphy->vregs), hsphy->vregs);

	return ret;
}

static int qcom_snps_hsphy_exit(struct phy *phy)
{
	struct qcom_snps_hsphy *hsphy = phy_get_drvdata(phy);

	reset_control_assert(hsphy->phy_reset);
	clk_disable_unprepare(hsphy->cfg_ahb_clk);
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
	{ .compatible	= "qcom,usb-snps-hs-5nm-phy", },
	{
		.compatible	= "qcom,usb-snps-hs-7nm-phy",
		.data		= &sc7280_snps_7nm_phy,
	},
	{ .compatible	= "qcom,usb-snps-femto-v2-phy",	},
	{ }
};
MODULE_DEVICE_TABLE(of, qcom_snps_hsphy_of_match_table);

static const struct dev_pm_ops qcom_snps_hsphy_pm_ops = {
	SET_RUNTIME_PM_OPS(qcom_snps_hsphy_runtime_suspend,
			   qcom_snps_hsphy_runtime_resume, NULL)
};

static void qcom_snps_hsphy_override_param_update_val(
			const struct override_param_map map,
			s32 dt_val, struct phy_override_seq *seq_entry)
{
	int i;

	/*
	 * Param table for each param is in increasing order
	 * of dt values. We need to iterate over the list to
	 * select the entry that matches the dt value and pick
	 * up the corresponding register value.
	 */
	for (i = 0; i < map.table_size - 1; i++) {
		if (map.param_table[i].value == dt_val)
			break;
	}

	seq_entry->need_update = true;
	seq_entry->offset = map.reg_offset;
	seq_entry->mask = map.param_mask;
	seq_entry->value = map.param_table[i].reg_val << __ffs(map.param_mask);
}

static void qcom_snps_hsphy_read_override_param_seq(struct device *dev)
{
	struct device_node *node = dev->of_node;
	s32 val;
	int ret, i;
	struct qcom_snps_hsphy *hsphy;
	const struct override_param_map *cfg = of_device_get_match_data(dev);

	if (!cfg)
		return;

	hsphy = dev_get_drvdata(dev);

	for (i = 0; cfg[i].prop_name != NULL; i++) {
		ret = of_property_read_s32(node, cfg[i].prop_name, &val);
		if (ret)
			continue;

		qcom_snps_hsphy_override_param_update_val(cfg[i], val,
					&hsphy->update_seq_cfg[i]);
		dev_dbg(&hsphy->phy->dev, "Read param: %s dt_val: %d reg_val: 0x%x\n",
			cfg[i].prop_name, val, hsphy->update_seq_cfg[i].value);

	}
}

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

	hsphy->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(hsphy->base))
		return PTR_ERR(hsphy->base);

	hsphy->ref_clk = devm_clk_get(dev, "ref");
	if (IS_ERR(hsphy->ref_clk))
		return dev_err_probe(dev, PTR_ERR(hsphy->ref_clk),
				     "failed to get ref clk\n");

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
	qcom_snps_hsphy_read_override_param_seq(dev);

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
