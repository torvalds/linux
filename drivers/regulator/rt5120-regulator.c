// SPDX-License-Identifier: GPL-2.0+

#include <linux/bits.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>

#define RT5120_REG_PGSTAT	0x03
#define RT5120_REG_CH1VID	0x06
#define RT5120_REG_CH1SLPVID	0x07
#define RT5120_REG_ENABLE	0x08
#define RT5120_REG_MODECTL	0x09
#define RT5120_REG_UVOVPROT	0x0A
#define RT5120_REG_SLPCTL	0x0C
#define RT5120_REG_INTSTAT	0x1E
#define RT5120_REG_DISCHG	0x1F

#define RT5120_OUTPG_MASK(rid)	BIT(rid + 1)
#define RT5120_OUTUV_MASK(rid)	BIT(rid + 9)
#define RT5120_OUTOV_MASK(rid)	BIT(rid + 16)
#define RT5120_CH1VID_MASK	GENMASK(6, 0)
#define RT5120_RIDEN_MASK(rid)	BIT(rid + 1)
#define RT5120_RADEN_MASK(rid)	BIT(rid)
#define RT5120_FPWM_MASK(rid)	BIT(rid + 1)
#define RT5120_UVHICCUP_MASK	BIT(1)
#define RT5120_OVHICCUP_MASK	BIT(0)
#define RT5120_HOTDIE_MASK	BIT(1)

#define RT5120_BUCK1_MINUV	600000
#define RT5120_BUCK1_MAXUV	1393750
#define RT5120_BUCK1_STEPUV	6250
#define RT5120_BUCK1_NUM_VOLT	0x80

#define RT5120_AUTO_MODE	0
#define RT5120_FPWM_MODE	1

enum {
	RT5120_REGULATOR_BUCK1 = 0,
	RT5120_REGULATOR_BUCK2,
	RT5120_REGULATOR_BUCK3,
	RT5120_REGULATOR_BUCK4,
	RT5120_REGULATOR_LDO,
	RT5120_REGULATOR_EXTEN,
	RT5120_MAX_REGULATOR
};

struct rt5120_priv {
	struct device *dev;
	struct regmap *regmap;
	struct regulator_desc rdesc[RT5120_MAX_REGULATOR];
};

static int rt5120_buck_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	struct regmap *regmap = rdev_get_regmap(rdev);
	int rid = rdev_get_id(rdev);
	unsigned int mask = RT5120_FPWM_MASK(rid), val;

	switch (mode) {
	case REGULATOR_MODE_NORMAL:
		val = 0;
		break;
	case REGULATOR_MODE_FAST:
		val = RT5120_FPWM_MASK(rid);
		break;
	default:
		return -EINVAL;
	}

	return regmap_update_bits(regmap, RT5120_REG_MODECTL, mask, val);
}

static unsigned int rt5120_buck_get_mode(struct regulator_dev *rdev)
{
	struct regmap *regmap = rdev_get_regmap(rdev);
	int ret, rid = rdev_get_id(rdev);
	unsigned int val;

	ret = regmap_read(regmap, RT5120_REG_MODECTL, &val);
	if (ret)
		return REGULATOR_MODE_INVALID;

	if (val & RT5120_FPWM_MASK(rid))
		return REGULATOR_MODE_FAST;

	return REGULATOR_MODE_NORMAL;
}

static int rt5120_regulator_get_error_flags(struct regulator_dev *rdev,
					    unsigned int *flags)
{
	struct regmap *regmap = rdev_get_regmap(rdev);
	unsigned int stat, hd_stat, cur_flags = 0;
	int rid = rdev_get_id(rdev), ret;

	/*
	 * reg 0x03/0x04/0x05 to indicate PG/UV/OV
	 * use block read to descrease I/O xfer time
	 */
	ret = regmap_raw_read(regmap, RT5120_REG_PGSTAT, &stat, 3);
	if (ret)
		return ret;

	ret = regmap_read(regmap, RT5120_REG_INTSTAT, &hd_stat);
	if (ret)
		return ret;

	if (!(stat & RT5120_OUTPG_MASK(rid))) {
		if (stat & RT5120_OUTUV_MASK(rid))
			cur_flags |= REGULATOR_ERROR_UNDER_VOLTAGE;

		if (stat & RT5120_OUTOV_MASK(rid))
			cur_flags |= REGULATOR_ERROR_REGULATION_OUT;
	}

	if (hd_stat & RT5120_HOTDIE_MASK)
		cur_flags |= REGULATOR_ERROR_OVER_TEMP;

	*flags = cur_flags;
	return 0;
}

static int rt5120_buck1_set_suspend_voltage(struct regulator_dev *rdev, int uV)
{
	struct regmap *regmap = rdev_get_regmap(rdev);
	int sel;

	if (uV < RT5120_BUCK1_MINUV || uV > RT5120_BUCK1_MAXUV)
		return -EINVAL;

	sel = (uV - RT5120_BUCK1_MINUV) / RT5120_BUCK1_STEPUV;
	return regmap_write(regmap, RT5120_REG_CH1SLPVID, sel);
}

static int rt5120_regulator_set_suspend_enable(struct regulator_dev *rdev)
{
	struct regmap *regmap = rdev_get_regmap(rdev);
	int rid = rdev_get_id(rdev);
	unsigned int mask = RT5120_RIDEN_MASK(rid);

	return regmap_update_bits(regmap, RT5120_REG_SLPCTL, mask, mask);
}

static int rt5120_regulator_set_suspend_disable(struct regulator_dev *rdev)
{
	struct regmap *regmap = rdev_get_regmap(rdev);
	int rid = rdev_get_id(rdev);
	unsigned int mask = RT5120_RIDEN_MASK(rid);

	return regmap_update_bits(regmap, RT5120_REG_SLPCTL, mask, 0);
}

static const struct regulator_ops rt5120_buck1_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_linear,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_active_discharge = regulator_set_active_discharge_regmap,
	.set_mode = rt5120_buck_set_mode,
	.get_mode = rt5120_buck_get_mode,
	.get_error_flags = rt5120_regulator_get_error_flags,
	.set_suspend_voltage = rt5120_buck1_set_suspend_voltage,
	.set_suspend_enable = rt5120_regulator_set_suspend_enable,
	.set_suspend_disable = rt5120_regulator_set_suspend_disable,
};

static const struct regulator_ops rt5120_buck234_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.set_active_discharge = regulator_set_active_discharge_regmap,
	.set_mode = rt5120_buck_set_mode,
	.get_mode = rt5120_buck_get_mode,
	.get_error_flags = rt5120_regulator_get_error_flags,
	.set_suspend_enable = rt5120_regulator_set_suspend_enable,
	.set_suspend_disable = rt5120_regulator_set_suspend_disable,
};

static const struct regulator_ops rt5120_ldo_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.set_active_discharge = regulator_set_active_discharge_regmap,
	.get_error_flags = rt5120_regulator_get_error_flags,
	.set_suspend_enable = rt5120_regulator_set_suspend_enable,
	.set_suspend_disable = rt5120_regulator_set_suspend_disable,
};

static const struct regulator_ops rt5120_exten_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.set_suspend_enable = rt5120_regulator_set_suspend_enable,
	.set_suspend_disable = rt5120_regulator_set_suspend_disable,
};

static unsigned int rt5120_buck_of_map_mode(unsigned int mode)
{
	switch (mode) {
	case RT5120_AUTO_MODE:
		return REGULATOR_MODE_NORMAL;
	case RT5120_FPWM_MODE:
		return REGULATOR_MODE_FAST;
	default:
		return REGULATOR_MODE_INVALID;
	}
}

static void rt5120_fillin_regulator_desc(struct regulator_desc *desc, int rid)
{
	static const char * const name[] = {
		"buck1", "buck2", "buck3", "buck4", "ldo", "exten" };
	static const char * const sname[] = {
		"vin1", "vin2", "vin3", "vin4", "vinldo", NULL };

	/* Common regulator property */
	desc->name = name[rid];
	desc->supply_name = sname[rid];
	desc->owner = THIS_MODULE;
	desc->type = REGULATOR_VOLTAGE;
	desc->id = rid;
	desc->enable_reg = RT5120_REG_ENABLE;
	desc->enable_mask = RT5120_RIDEN_MASK(rid);
	desc->active_discharge_reg = RT5120_REG_DISCHG;
	desc->active_discharge_mask = RT5120_RADEN_MASK(rid);
	desc->active_discharge_on = RT5120_RADEN_MASK(rid);
	/* Config n_voltages to 1 for all*/
	desc->n_voltages = 1;

	/* Only buck support mode change */
	if (rid >= RT5120_REGULATOR_BUCK1 && rid <= RT5120_REGULATOR_BUCK4)
		desc->of_map_mode = rt5120_buck_of_map_mode;

	/* RID specific property init */
	switch (rid) {
	case RT5120_REGULATOR_BUCK1:
		/* Only buck1 support voltage change by I2C */
		desc->n_voltages = RT5120_BUCK1_NUM_VOLT;
		desc->min_uV = RT5120_BUCK1_MINUV;
		desc->uV_step = RT5120_BUCK1_STEPUV;
		desc->vsel_reg = RT5120_REG_CH1VID;
		desc->vsel_mask = RT5120_CH1VID_MASK;
		desc->ops = &rt5120_buck1_ops;
		break;
	case RT5120_REGULATOR_BUCK2 ... RT5120_REGULATOR_BUCK4:
		desc->ops = &rt5120_buck234_ops;
		break;
	case RT5120_REGULATOR_LDO:
		desc->ops = &rt5120_ldo_ops;
		break;
	default:
		desc->ops = &rt5120_exten_ops;
	}
}

static int rt5120_of_parse_cb(struct rt5120_priv *priv, int rid,
			      struct of_regulator_match *match)
{
	struct regulator_desc *desc = priv->rdesc + rid;
	struct regulator_init_data *init_data = match->init_data;

	if (!init_data || rid == RT5120_REGULATOR_BUCK1)
		return 0;

	if (init_data->constraints.min_uV != init_data->constraints.max_uV) {
		dev_err(priv->dev, "Variable voltage for fixed regulator\n");
		return -EINVAL;
	}

	desc->fixed_uV = init_data->constraints.min_uV;
	return 0;
}

static struct of_regulator_match rt5120_regu_match[RT5120_MAX_REGULATOR] = {
	[RT5120_REGULATOR_BUCK1] = { .name = "buck1", },
	[RT5120_REGULATOR_BUCK2] = { .name = "buck2", },
	[RT5120_REGULATOR_BUCK3] = { .name = "buck3", },
	[RT5120_REGULATOR_BUCK4] = { .name = "buck4", },
	[RT5120_REGULATOR_LDO] = { .name = "ldo", },
	[RT5120_REGULATOR_EXTEN] = { .name = "exten", }
};

static int rt5120_parse_regulator_dt_data(struct rt5120_priv *priv)
{
	struct device *dev = priv->dev->parent;
	struct device_node *reg_node;
	int i, ret;

	for (i = 0; i < RT5120_MAX_REGULATOR; i++) {
		rt5120_fillin_regulator_desc(priv->rdesc + i, i);

		rt5120_regu_match[i].desc = priv->rdesc + i;
	}

	reg_node = of_get_child_by_name(dev->of_node, "regulators");
	if (!reg_node) {
		dev_err(priv->dev, "Couldn't find 'regulators' node\n");
		return -ENODEV;
	}

	ret = of_regulator_match(priv->dev, reg_node, rt5120_regu_match,
				 ARRAY_SIZE(rt5120_regu_match));

	of_node_put(reg_node);

	if (ret < 0) {
		dev_err(priv->dev,
			"Error parsing regulator init data (%d)\n", ret);
		return ret;
	}

	for (i = 0; i < RT5120_MAX_REGULATOR; i++) {
		ret = rt5120_of_parse_cb(priv, i, rt5120_regu_match + i);
		if (ret) {
			dev_err(priv->dev, "Failed in [%d] of_passe_cb\n", i);
			return ret;
		}
	}

	return 0;
}

static int rt5120_device_property_init(struct rt5120_priv *priv)
{
	struct device *dev = priv->dev->parent;
	struct device_node *np = dev->of_node;
	bool prot_enable;
	unsigned int prot_enable_val = 0;

	/* Assign UV/OV HW protection behavior */
	prot_enable = of_property_read_bool(np,
					    "richtek,enable-undervolt-hiccup");
	if (prot_enable)
		prot_enable_val |= RT5120_UVHICCUP_MASK;

	prot_enable = of_property_read_bool(np,
					    "richtek,enable-overvolt-hiccup");
	if (prot_enable)
		prot_enable_val |= RT5120_OVHICCUP_MASK;

	return regmap_update_bits(priv->regmap, RT5120_REG_UVOVPROT,
				  RT5120_UVHICCUP_MASK | RT5120_OVHICCUP_MASK,
				  prot_enable_val);
}

static int rt5120_regulator_probe(struct platform_device *pdev)
{
	struct rt5120_priv *priv;
	struct regulator_dev *rdev;
	struct regulator_config config = {};
	int i, ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = &pdev->dev;

	priv->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!priv->regmap) {
		dev_err(&pdev->dev, "Failed to init regmap\n");
		return -ENODEV;
	}

	ret = rt5120_device_property_init(priv);
	if (ret) {
		dev_err(&pdev->dev, "Failed to do property init\n");
		return ret;
	}

	ret = rt5120_parse_regulator_dt_data(priv);
	if (ret) {
		dev_err(&pdev->dev, "Failed to parse dt data\n");
		return ret;
	}

	config.dev = &pdev->dev;
	config.regmap = priv->regmap;

	for (i = 0; i < RT5120_MAX_REGULATOR; i++) {
		config.of_node = rt5120_regu_match[i].of_node;
		config.init_data = rt5120_regu_match[i].init_data;

		rdev = devm_regulator_register(&pdev->dev, priv->rdesc + i,
					       &config);
		if (IS_ERR(rdev)) {
			dev_err(&pdev->dev,
				"Failed to register regulator [%d]\n", i);
			return PTR_ERR(rdev);
		}
	}

	return 0;
}

static const struct platform_device_id rt5120_regulator_dev_table[] = {
	{ "rt5120-regulator", 0 },
	{}
};
MODULE_DEVICE_TABLE(platform, rt5120_regulator_dev_table);

static struct platform_driver rt5120_regulator_driver = {
	.driver = {
		.name = "rt5120-regulator",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.id_table = rt5120_regulator_dev_table,
	.probe = rt5120_regulator_probe,
};
module_platform_driver(rt5120_regulator_driver);

MODULE_AUTHOR("ChiYuan Huang <cy_huang@richtek.com>");
MODULE_DESCRIPTION("Richtek RT5120 regulator driver");
MODULE_LICENSE("GPL v2");
