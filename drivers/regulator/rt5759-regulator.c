// SPDX-License-Identifier: GPL-2.0+

#include <linux/bits.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>

#define RT5759_REG_VENDORINFO	0x00
#define RT5759_REG_FREQ		0x01
#define RT5759_REG_VSEL		0x02
#define RT5759_REG_DCDCCTRL	0x03
#define RT5759_REG_STATUS	0x04
#define RT5759_REG_DCDCSET	0x05
#define RT5759A_REG_WDTEN	0x42

#define RT5759_TSTEP_MASK	GENMASK(3, 2)
#define RT5759_VSEL_MASK	GENMASK(6, 0)
#define RT5759_DISCHARGE_MASK	BIT(3)
#define RT5759_FPWM_MASK	BIT(2)
#define RT5759_ENABLE_MASK	BIT(1)
#define RT5759_OT_MASK		BIT(1)
#define RT5759_UV_MASK		BIT(0)
#define RT5957_OCLVL_MASK	GENMASK(7, 6)
#define RT5759_OCLVL_SHIFT	6
#define RT5957_OTLVL_MASK	GENMASK(5, 4)
#define RT5759_OTLVL_SHIFT	4
#define RT5759A_WDTEN_MASK	BIT(1)

#define RT5759_MANUFACTURER_ID	0x82
/* vsel range 0x00 ~ 0x5A */
#define RT5759_NUM_VOLTS	91
#define RT5759_MIN_UV		600000
#define RT5759_STEP_UV		10000
#define RT5759A_STEP_UV		12500
#define RT5759_MINSS_TIMEUS	1500

#define RT5759_PSKIP_MODE	0
#define RT5759_FPWM_MODE	1

enum {
	CHIP_TYPE_RT5759 = 0,
	CHIP_TYPE_RT5759A,
	CHIP_TYPE_MAX
};

struct rt5759_priv {
	struct device *dev;
	struct regmap *regmap;
	struct regulator_desc desc;
	unsigned long chip_type;
};

static int rt5759_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	struct regmap *regmap = rdev_get_regmap(rdev);
	unsigned int mode_val;

	switch (mode) {
	case REGULATOR_MODE_NORMAL:
		mode_val = 0;
		break;
	case REGULATOR_MODE_FAST:
		mode_val = RT5759_FPWM_MASK;
		break;
	default:
		return -EINVAL;
	}

	return regmap_update_bits(regmap, RT5759_REG_STATUS, RT5759_FPWM_MASK,
				  mode_val);
}

static unsigned int rt5759_get_mode(struct regulator_dev *rdev)
{
	struct regmap *regmap = rdev_get_regmap(rdev);
	unsigned int regval;
	int ret;

	ret = regmap_read(regmap, RT5759_REG_DCDCCTRL, &regval);
	if (ret)
		return REGULATOR_MODE_INVALID;

	if (regval & RT5759_FPWM_MASK)
		return REGULATOR_MODE_FAST;

	return REGULATOR_MODE_NORMAL;
}

static int rt5759_get_error_flags(struct regulator_dev *rdev,
				  unsigned int *flags)
{
	struct regmap *regmap = rdev_get_regmap(rdev);
	unsigned int status, events = 0;
	int ret;

	ret = regmap_read(regmap, RT5759_REG_STATUS, &status);
	if (ret)
		return ret;

	if (status & RT5759_OT_MASK)
		events |= REGULATOR_ERROR_OVER_TEMP;

	if (status & RT5759_UV_MASK)
		events |= REGULATOR_ERROR_UNDER_VOLTAGE;

	*flags = events;
	return 0;
}

static int rt5759_set_ocp(struct regulator_dev *rdev, int lim_uA, int severity,
			  bool enable)
{
	struct regmap *regmap = rdev_get_regmap(rdev);
	int ocp_lvl[] = { 9800000, 10800000, 11800000 };
	unsigned int ocp_regval;
	int i;

	/* Only support over current protection parameter */
	if (severity != REGULATOR_SEVERITY_PROT)
		return 0;

	if (enable) {
		/* Default ocp level is 10.8A */
		if (lim_uA == 0)
			lim_uA = 10800000;

		for (i = 0; i < ARRAY_SIZE(ocp_lvl); i++) {
			if (lim_uA <= ocp_lvl[i])
				break;
		}

		if (i == ARRAY_SIZE(ocp_lvl))
			i = ARRAY_SIZE(ocp_lvl) - 1;

		ocp_regval = i + 1;
	} else
		ocp_regval = 0;

	return regmap_update_bits(regmap, RT5759_REG_DCDCSET, RT5957_OCLVL_MASK,
				  ocp_regval << RT5759_OCLVL_SHIFT);
}

static int rt5759_set_otp(struct regulator_dev *rdev, int lim, int severity,
			  bool enable)
{
	struct regmap *regmap = rdev_get_regmap(rdev);
	int otp_lvl[] = { 140, 150, 170 };
	unsigned int otp_regval;
	int i;

	/* Only support over temperature protection parameter */
	if (severity != REGULATOR_SEVERITY_PROT)
		return 0;

	if (enable) {
		/* Default otp level is 150'c */
		if (lim == 0)
			lim = 150;

		for (i = 0; i < ARRAY_SIZE(otp_lvl); i++) {
			if (lim <= otp_lvl[i])
				break;
		}

		if (i == ARRAY_SIZE(otp_lvl))
			i = ARRAY_SIZE(otp_lvl) - 1;

		otp_regval = i + 1;
	} else
		otp_regval = 0;

	return regmap_update_bits(regmap, RT5759_REG_DCDCSET, RT5957_OTLVL_MASK,
				  otp_regval << RT5759_OTLVL_SHIFT);
}

static const struct regulator_ops rt5759_regulator_ops = {
	.list_voltage = regulator_list_voltage_linear,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.set_active_discharge = regulator_set_active_discharge_regmap,
	.set_mode = rt5759_set_mode,
	.get_mode = rt5759_get_mode,
	.set_ramp_delay = regulator_set_ramp_delay_regmap,
	.get_error_flags = rt5759_get_error_flags,
	.set_over_current_protection = rt5759_set_ocp,
	.set_thermal_protection = rt5759_set_otp,
};

static unsigned int rt5759_of_map_mode(unsigned int mode)
{
	switch (mode) {
	case RT5759_FPWM_MODE:
		return REGULATOR_MODE_FAST;
	case RT5759_PSKIP_MODE:
		return REGULATOR_MODE_NORMAL;
	default:
		return REGULATOR_MODE_INVALID;
	}
}

static const unsigned int rt5759_ramp_table[] = { 20000, 15000, 10000, 5000 };

static int rt5759_regulator_register(struct rt5759_priv *priv)
{
	struct device_node *np = priv->dev->of_node;
	struct regulator_desc *reg_desc = &priv->desc;
	struct regulator_config reg_cfg;
	struct regulator_dev *rdev;
	int ret;

	reg_desc->name = "rt5759-buck";
	reg_desc->type = REGULATOR_VOLTAGE;
	reg_desc->owner = THIS_MODULE;
	reg_desc->ops = &rt5759_regulator_ops;
	reg_desc->n_voltages = RT5759_NUM_VOLTS;
	reg_desc->min_uV = RT5759_MIN_UV;
	reg_desc->uV_step = RT5759_STEP_UV;
	reg_desc->vsel_reg = RT5759_REG_VSEL;
	reg_desc->vsel_mask = RT5759_VSEL_MASK;
	reg_desc->enable_reg = RT5759_REG_DCDCCTRL;
	reg_desc->enable_mask = RT5759_ENABLE_MASK;
	reg_desc->active_discharge_reg = RT5759_REG_DCDCCTRL;
	reg_desc->active_discharge_mask = RT5759_DISCHARGE_MASK;
	reg_desc->active_discharge_on = RT5759_DISCHARGE_MASK;
	reg_desc->ramp_reg = RT5759_REG_FREQ;
	reg_desc->ramp_mask = RT5759_TSTEP_MASK;
	reg_desc->ramp_delay_table = rt5759_ramp_table;
	reg_desc->n_ramp_values = ARRAY_SIZE(rt5759_ramp_table);
	reg_desc->enable_time = RT5759_MINSS_TIMEUS;
	reg_desc->of_map_mode = rt5759_of_map_mode;

	/*
	 * RT5759 step uV = 10000
	 * RT5759A step uV = 12500
	 */
	if (priv->chip_type == CHIP_TYPE_RT5759A)
		reg_desc->uV_step = RT5759A_STEP_UV;

	memset(&reg_cfg, 0, sizeof(reg_cfg));
	reg_cfg.dev = priv->dev;
	reg_cfg.of_node = np;
	reg_cfg.init_data = of_get_regulator_init_data(priv->dev, np, reg_desc);
	reg_cfg.regmap = priv->regmap;

	rdev = devm_regulator_register(priv->dev, reg_desc, &reg_cfg);
	if (IS_ERR(rdev)) {
		ret = PTR_ERR(rdev);
		dev_err(priv->dev, "Failed to register regulator (%d)\n", ret);
		return ret;
	}

	return 0;
}

static int rt5759_init_device_property(struct rt5759_priv *priv)
{
	unsigned int val = 0;

	/*
	 * Only RT5759A support external watchdog input
	 */
	if (priv->chip_type != CHIP_TYPE_RT5759A)
		return 0;

	if (device_property_read_bool(priv->dev, "richtek,watchdog-enable"))
		val = RT5759A_WDTEN_MASK;

	return regmap_update_bits(priv->regmap, RT5759A_REG_WDTEN,
				  RT5759A_WDTEN_MASK, val);
}

static int rt5759_manufacturer_check(struct rt5759_priv *priv)
{
	unsigned int vendor;
	int ret;

	ret = regmap_read(priv->regmap, RT5759_REG_VENDORINFO, &vendor);
	if (ret)
		return ret;

	if (vendor != RT5759_MANUFACTURER_ID) {
		dev_err(priv->dev, "vendor info not correct (%d)\n", vendor);
		return -EINVAL;
	}

	return 0;
}

static bool rt5759_is_accessible_reg(struct device *dev, unsigned int reg)
{
	struct rt5759_priv *priv = dev_get_drvdata(dev);

	if (reg <= RT5759_REG_DCDCSET)
		return true;

	if (priv->chip_type == CHIP_TYPE_RT5759A && reg == RT5759A_REG_WDTEN)
		return true;

	return false;
}

static const struct regmap_config rt5759_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = RT5759A_REG_WDTEN,
	.readable_reg = rt5759_is_accessible_reg,
	.writeable_reg = rt5759_is_accessible_reg,
};

static int rt5759_probe(struct i2c_client *i2c)
{
	struct rt5759_priv *priv;
	int ret;

	priv = devm_kzalloc(&i2c->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = &i2c->dev;
	priv->chip_type = (unsigned long)of_device_get_match_data(&i2c->dev);
	i2c_set_clientdata(i2c, priv);

	priv->regmap = devm_regmap_init_i2c(i2c, &rt5759_regmap_config);
	if (IS_ERR(priv->regmap)) {
		ret = PTR_ERR(priv->regmap);
		dev_err(&i2c->dev, "Failed to allocate regmap (%d)\n", ret);
		return ret;
	}

	ret = rt5759_manufacturer_check(priv);
	if (ret) {
		dev_err(&i2c->dev, "Failed to check device (%d)\n", ret);
		return ret;
	}

	ret = rt5759_init_device_property(priv);
	if (ret) {
		dev_err(&i2c->dev, "Failed to init device (%d)\n", ret);
		return ret;
	}

	return rt5759_regulator_register(priv);
}

static const struct of_device_id __maybe_unused rt5759_device_table[] = {
	{ .compatible = "richtek,rt5759", .data = (void *)CHIP_TYPE_RT5759 },
	{ .compatible = "richtek,rt5759a", .data = (void *)CHIP_TYPE_RT5759A },
	{}
};
MODULE_DEVICE_TABLE(of, rt5759_device_table);

static struct i2c_driver rt5759_driver = {
	.driver = {
		.name = "rt5759",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = of_match_ptr(rt5759_device_table),
	},
	.probe = rt5759_probe,
};
module_i2c_driver(rt5759_driver);

MODULE_AUTHOR("ChiYuan Huang <cy_huang@richtek.com>");
MODULE_DESCRIPTION("Richtek RT5759 Regulator Driver");
MODULE_LICENSE("GPL v2");
