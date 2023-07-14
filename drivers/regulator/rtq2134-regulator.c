// SPDX-License-Identifier: GPL-2.0+

#include <linux/bitops.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>

enum {
	RTQ2134_IDX_BUCK1 = 0,
	RTQ2134_IDX_BUCK2,
	RTQ2134_IDX_BUCK3,
	RTQ2134_IDX_MAX
};

#define RTQ2134_AUTO_MODE		0
#define RTQ2134_FCCM_MODE		1

#define RTQ2134_BUCK_DVS0_CTRL		0
#define RTQ2134_BUCK_VSEL_CTRL		2

#define RTQ2134_REG_IO_CHIPNAME		0x01
#define RTQ2134_REG_FLT_RECORDTEMP	0x13
#define RTQ2134_REG_FLT_RECORDBUCK(_id)	(0x14 + (_id))
#define RTQ2134_REG_FLT_BUCKCTRL(_id)	(0x37 + (_id))
#define RTQ2134_REG_BUCK1_CFG0		0x42
#define RTQ2134_REG_BUCK1_DVS0CFG1	0x48
#define RTQ2134_REG_BUCK1_DVS0CFG0	0x49
#define RTQ2134_REG_BUCK1_DVS1CFG1	0x4A
#define RTQ2134_REG_BUCK1_DVS1CFG0	0x4B
#define RTQ2134_REG_BUCK1_DVSCFG	0x52
#define RTQ2134_REG_BUCK1_RSPCFG	0x54
#define RTQ2134_REG_BUCK2_CFG0		0x5F
#define RTQ2134_REG_BUCK2_DVS0CFG1	0x62
#define RTQ2134_REG_BUCK2_DVS0CFG0	0x63
#define RTQ2134_REG_BUCK2_DVS1CFG1	0x64
#define RTQ2134_REG_BUCK2_DVS1CFG0	0x65
#define RTQ2134_REG_BUCK2_DVSCFG	0x6C
#define RTQ2134_REG_BUCK2_RSPCFG	0x6E
#define RTQ2134_REG_BUCK3_CFG0		0x79
#define RTQ2134_REG_BUCK3_DVS0CFG1	0x7C
#define RTQ2134_REG_BUCK3_DVS0CFG0	0x7D
#define RTQ2134_REG_BUCK3_DVS1CFG1	0x7E
#define RTQ2134_REG_BUCK3_DVS1CFG0	0x7F
#define RTQ2134_REG_BUCK3_DVSCFG	0x86
#define RTQ2134_REG_BUCK3_RSPCFG	0x88
#define RTQ2134_REG_BUCK3_SLEWCTRL	0x89

#define RTQ2134_VOUT_MAXNUM		256
#define RTQ2134_VOUT_MASK		0xFF
#define RTQ2134_VOUTEN_MASK		BIT(0)
#define RTQ2134_ACTDISCHG_MASK		BIT(0)
#define RTQ2134_RSPUP_MASK		GENMASK(6, 4)
#define RTQ2134_FCCM_MASK		BIT(5)
#define RTQ2134_UVHICCUP_MASK		BIT(3)
#define RTQ2134_BUCKDVS_CTRL_MASK	GENMASK(1, 0)
#define RTQ2134_CHIPOT_MASK		BIT(2)
#define RTQ2134_BUCKOV_MASK		BIT(5)
#define RTQ2134_BUCKUV_MASK		BIT(4)

struct rtq2134_regulator_desc {
	struct regulator_desc desc;
	/* Extension for proprietary register and mask */
	unsigned int mode_reg;
	unsigned int mode_mask;
	unsigned int suspend_enable_reg;
	unsigned int suspend_enable_mask;
	unsigned int suspend_vsel_reg;
	unsigned int suspend_vsel_mask;
	unsigned int suspend_mode_reg;
	unsigned int suspend_mode_mask;
	unsigned int dvs_ctrl_reg;
};

static int rtq2134_buck_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	struct rtq2134_regulator_desc *desc =
		(struct rtq2134_regulator_desc *)rdev->desc;
	unsigned int val;

	if (mode == REGULATOR_MODE_NORMAL)
		val = RTQ2134_AUTO_MODE;
	else if (mode == REGULATOR_MODE_FAST)
		val = RTQ2134_FCCM_MODE;
	else
		return -EINVAL;

	val <<= ffs(desc->mode_mask) - 1;
	return regmap_update_bits(rdev->regmap, desc->mode_reg, desc->mode_mask,
				  val);
}

static unsigned int rtq2134_buck_get_mode(struct regulator_dev *rdev)
{
	struct rtq2134_regulator_desc *desc =
		(struct rtq2134_regulator_desc *)rdev->desc;
	unsigned int mode;
	int ret;

	ret = regmap_read(rdev->regmap, desc->mode_reg, &mode);
	if (ret)
		return ret;

	if (mode & desc->mode_mask)
		return REGULATOR_MODE_FAST;
	return REGULATOR_MODE_NORMAL;
}

static int rtq2134_buck_set_suspend_voltage(struct regulator_dev *rdev, int uV)
{
	struct rtq2134_regulator_desc *desc =
		(struct rtq2134_regulator_desc *)rdev->desc;
	int sel;

	sel = regulator_map_voltage_linear_range(rdev, uV, uV);
	if (sel < 0)
		return sel;

	sel <<= ffs(desc->suspend_vsel_mask) - 1;

	return regmap_update_bits(rdev->regmap, desc->suspend_vsel_reg,
				  desc->suspend_vsel_mask, sel);
}

static int rtq2134_buck_set_suspend_enable(struct regulator_dev *rdev)
{
	struct rtq2134_regulator_desc *desc =
		(struct rtq2134_regulator_desc *)rdev->desc;
	unsigned int val = desc->suspend_enable_mask;

	return regmap_update_bits(rdev->regmap, desc->suspend_enable_reg,
				  desc->suspend_enable_mask, val);
}

static int rtq2134_buck_set_suspend_disable(struct regulator_dev *rdev)
{
	struct rtq2134_regulator_desc *desc =
		(struct rtq2134_regulator_desc *)rdev->desc;

	return regmap_update_bits(rdev->regmap, desc->suspend_enable_reg,
				  desc->suspend_enable_mask, 0);
}

static int rtq2134_buck_set_suspend_mode(struct regulator_dev *rdev,
					 unsigned int mode)
{
	struct rtq2134_regulator_desc *desc =
		(struct rtq2134_regulator_desc *)rdev->desc;
	unsigned int val;

	if (mode == REGULATOR_MODE_NORMAL)
		val = RTQ2134_AUTO_MODE;
	else if (mode == REGULATOR_MODE_FAST)
		val = RTQ2134_FCCM_MODE;
	else
		return -EINVAL;

	val <<= ffs(desc->suspend_mode_mask) - 1;
	return regmap_update_bits(rdev->regmap, desc->suspend_mode_reg,
				  desc->suspend_mode_mask, val);
}

static int rtq2134_buck_get_error_flags(struct regulator_dev *rdev,
					unsigned int *flags)
{
	int rid = rdev_get_id(rdev);
	unsigned int chip_error, buck_error, events = 0;
	int ret;

	ret = regmap_read(rdev->regmap, RTQ2134_REG_FLT_RECORDTEMP,
			  &chip_error);
	if (ret) {
		dev_err(&rdev->dev, "Failed to get chip error flag\n");
		return ret;
	}

	ret = regmap_read(rdev->regmap, RTQ2134_REG_FLT_RECORDBUCK(rid),
			  &buck_error);
	if (ret) {
		dev_err(&rdev->dev, "Failed to get buck error flag\n");
		return ret;
	}

	if (chip_error & RTQ2134_CHIPOT_MASK)
		events |= REGULATOR_ERROR_OVER_TEMP;

	if (buck_error & RTQ2134_BUCKUV_MASK)
		events |= REGULATOR_ERROR_UNDER_VOLTAGE;

	if (buck_error & RTQ2134_BUCKOV_MASK)
		events |= REGULATOR_ERROR_REGULATION_OUT;

	*flags = events;
	return 0;
}

static const struct regulator_ops rtq2134_buck_ops = {
	.list_voltage = regulator_list_voltage_linear_range,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.set_active_discharge = regulator_set_active_discharge_regmap,
	.set_ramp_delay = regulator_set_ramp_delay_regmap,
	.set_mode = rtq2134_buck_set_mode,
	.get_mode = rtq2134_buck_get_mode,
	.set_suspend_voltage = rtq2134_buck_set_suspend_voltage,
	.set_suspend_enable = rtq2134_buck_set_suspend_enable,
	.set_suspend_disable = rtq2134_buck_set_suspend_disable,
	.set_suspend_mode = rtq2134_buck_set_suspend_mode,
	.get_error_flags = rtq2134_buck_get_error_flags,
};

static const struct linear_range rtq2134_buck_vout_ranges[] = {
	REGULATOR_LINEAR_RANGE(300000, 0, 200, 5000),
	REGULATOR_LINEAR_RANGE(1310000, 201, 255, 10000)
};

static unsigned int rtq2134_buck_of_map_mode(unsigned int mode)
{
	switch (mode) {
	case RTQ2134_AUTO_MODE:
		return REGULATOR_MODE_NORMAL;
	case RTQ2134_FCCM_MODE:
		return REGULATOR_MODE_FAST;
	}

	return REGULATOR_MODE_INVALID;
}

static int rtq2134_buck_of_parse_cb(struct device_node *np,
				    const struct regulator_desc *desc,
				    struct regulator_config *cfg)
{
	struct rtq2134_regulator_desc *rdesc =
		(struct rtq2134_regulator_desc *)desc;
	int rid = desc->id;
	bool uv_shutdown, vsel_dvs;
	unsigned int val;
	int ret;

	vsel_dvs = of_property_read_bool(np, "richtek,use-vsel-dvs");
	if (vsel_dvs)
		val = RTQ2134_BUCK_VSEL_CTRL;
	else
		val = RTQ2134_BUCK_DVS0_CTRL;

	ret = regmap_update_bits(cfg->regmap, rdesc->dvs_ctrl_reg,
				 RTQ2134_BUCKDVS_CTRL_MASK, val);
	if (ret)
		return ret;

	uv_shutdown = of_property_read_bool(np, "richtek,uv-shutdown");
	if (uv_shutdown)
		val = 0;
	else
		val = RTQ2134_UVHICCUP_MASK;

	return regmap_update_bits(cfg->regmap, RTQ2134_REG_FLT_BUCKCTRL(rid),
				  RTQ2134_UVHICCUP_MASK, val);
}

static const unsigned int rtq2134_buck_ramp_delay_table[] = {
	0, 16000, 0, 8000, 4000, 2000, 1000, 500
};

#define RTQ2134_BUCK_DESC(_id) { \
	.desc = { \
		.name = "rtq2134_buck" #_id, \
		.of_match = of_match_ptr("buck" #_id), \
		.regulators_node = of_match_ptr("regulators"), \
		.id = RTQ2134_IDX_BUCK##_id, \
		.type = REGULATOR_VOLTAGE, \
		.owner = THIS_MODULE, \
		.ops = &rtq2134_buck_ops, \
		.n_voltages = RTQ2134_VOUT_MAXNUM, \
		.linear_ranges = rtq2134_buck_vout_ranges, \
		.n_linear_ranges = ARRAY_SIZE(rtq2134_buck_vout_ranges), \
		.vsel_reg = RTQ2134_REG_BUCK##_id##_DVS0CFG1, \
		.vsel_mask = RTQ2134_VOUT_MASK, \
		.enable_reg = RTQ2134_REG_BUCK##_id##_DVS0CFG0, \
		.enable_mask = RTQ2134_VOUTEN_MASK, \
		.active_discharge_reg = RTQ2134_REG_BUCK##_id##_CFG0, \
		.active_discharge_mask = RTQ2134_ACTDISCHG_MASK, \
		.active_discharge_on = RTQ2134_ACTDISCHG_MASK, \
		.ramp_reg = RTQ2134_REG_BUCK##_id##_RSPCFG, \
		.ramp_mask = RTQ2134_RSPUP_MASK, \
		.ramp_delay_table = rtq2134_buck_ramp_delay_table, \
		.n_ramp_values = ARRAY_SIZE(rtq2134_buck_ramp_delay_table), \
		.of_map_mode = rtq2134_buck_of_map_mode, \
		.of_parse_cb = rtq2134_buck_of_parse_cb, \
	}, \
	.mode_reg = RTQ2134_REG_BUCK##_id##_DVS0CFG0, \
	.mode_mask = RTQ2134_FCCM_MASK, \
	.suspend_mode_reg = RTQ2134_REG_BUCK##_id##_DVS1CFG0, \
	.suspend_mode_mask = RTQ2134_FCCM_MASK, \
	.suspend_enable_reg = RTQ2134_REG_BUCK##_id##_DVS1CFG0, \
	.suspend_enable_mask = RTQ2134_VOUTEN_MASK, \
	.suspend_vsel_reg = RTQ2134_REG_BUCK##_id##_DVS1CFG1, \
	.suspend_vsel_mask = RTQ2134_VOUT_MASK, \
	.dvs_ctrl_reg = RTQ2134_REG_BUCK##_id##_DVSCFG, \
}

static const struct rtq2134_regulator_desc rtq2134_regulator_descs[] = {
	RTQ2134_BUCK_DESC(1),
	RTQ2134_BUCK_DESC(2),
	RTQ2134_BUCK_DESC(3)
};

static bool rtq2134_is_accissible_reg(struct device *dev, unsigned int reg)
{
	if (reg >= RTQ2134_REG_IO_CHIPNAME && reg <= RTQ2134_REG_BUCK3_SLEWCTRL)
		return true;
	return false;
}

static const struct regmap_config rtq2134_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = RTQ2134_REG_BUCK3_SLEWCTRL,

	.readable_reg = rtq2134_is_accissible_reg,
	.writeable_reg = rtq2134_is_accissible_reg,
};

static int rtq2134_probe(struct i2c_client *i2c)
{
	struct regmap *regmap;
	struct regulator_dev *rdev;
	struct regulator_config regulator_cfg = {};
	int i;

	regmap = devm_regmap_init_i2c(i2c, &rtq2134_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&i2c->dev, "Failed to allocate regmap\n");
		return PTR_ERR(regmap);
	}

	regulator_cfg.dev = &i2c->dev;
	regulator_cfg.regmap = regmap;
	for (i = 0; i < ARRAY_SIZE(rtq2134_regulator_descs); i++) {
		rdev = devm_regulator_register(&i2c->dev,
					       &rtq2134_regulator_descs[i].desc,
					       &regulator_cfg);
		if (IS_ERR(rdev)) {
			dev_err(&i2c->dev, "Failed to init %d regulator\n", i);
			return PTR_ERR(rdev);
		}
	}

	return 0;
}

static const struct of_device_id __maybe_unused rtq2134_device_tables[] = {
	{ .compatible = "richtek,rtq2134", },
	{}
};
MODULE_DEVICE_TABLE(of, rtq2134_device_tables);

static struct i2c_driver rtq2134_driver = {
	.driver = {
		.name = "rtq2134",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = rtq2134_device_tables,
	},
	.probe = rtq2134_probe,
};
module_i2c_driver(rtq2134_driver);

MODULE_AUTHOR("ChiYuan Huang <cy_huang@richtek.com>");
MODULE_DESCRIPTION("Richtek RTQ2134 Regulator Driver");
MODULE_LICENSE("GPL v2");
