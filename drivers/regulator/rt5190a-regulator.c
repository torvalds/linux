// SPDX-License-Identifier: GPL-2.0+

#include <dt-bindings/regulator/richtek,rt5190a-regulator.h>
#include <linux/bits.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>

#define RT5190A_REG_MANUFACTURE		0x00
#define RT5190A_REG_BUCK2VSEL		0x04
#define RT5190A_REG_BUCK3VSEL		0x05
#define RT5190A_REG_DCDCCNTL		0x06
#define RT5190A_REG_ENABLE		0x07
#define RT5190A_REG_DISCHARGE		0x09
#define RT5190A_REG_PROTMODE		0x0A
#define RT5190A_REG_MUTECNTL		0x0B
#define RT5190A_REG_PGSTAT		0x0F
#define RT5190A_REG_OVINT		0x10
#define RT5190A_REG_HOTDIEMASK		0x17

#define RT5190A_VSEL_MASK		GENMASK(6, 0)
#define RT5190A_RID_BITMASK(rid)	BIT(rid + 1)
#define RT5190A_BUCK1_DISCHG_MASK	GENMASK(1, 0)
#define RT5190A_BUCK1_DISCHG_ONVAL	0x01
#define RT5190A_OVERVOLT_MASK		GENMASK(7, 0)
#define RT5190A_UNDERVOLT_MASK		GENMASK(15, 8)
#define RT5190A_CH234OT_MASK		BIT(29)
#define RT5190A_CHIPOT_MASK		BIT(28)

#define RT5190A_BUCK23_MINUV		600000
#define RT5190A_BUCK23_MAXUV		1400000
#define RT5190A_BUCK23_STEPUV		10000
#define RT5190A_BUCK23_STEPNUM		((1400000 - 600000) / 10000 + 1)

enum {
	RT5190A_IDX_BUCK1 = 0,
	RT5190A_IDX_BUCK2,
	RT5190A_IDX_BUCK3,
	RT5190A_IDX_BUCK4,
	RT5190A_IDX_LDO,
	RT5190A_MAX_IDX
};

struct rt5190a_priv {
	struct device *dev;
	struct regmap *regmap;
	struct regulator_desc rdesc[RT5190A_MAX_IDX];
	struct regulator_dev *rdev[RT5190A_MAX_IDX];
};

static int rt5190a_get_error_flags(struct regulator_dev *rdev,
				   unsigned int *flags)
{
	struct regmap *regmap = rdev_get_regmap(rdev);
	int rid = rdev_get_id(rdev);
	unsigned int pgood_stat;
	int ret;

	ret = regmap_read(regmap, RT5190A_REG_PGSTAT, &pgood_stat);
	if (ret)
		return ret;

	if (!(pgood_stat & RT5190A_RID_BITMASK(rid)))
		*flags = REGULATOR_ERROR_FAIL;
	else
		*flags = 0;

	return 0;
}

static int rt5190a_fixed_buck_set_mode(struct regulator_dev *rdev,
				       unsigned int mode)
{
	struct regmap *regmap = rdev_get_regmap(rdev);
	int rid = rdev_get_id(rdev);
	unsigned int mask = RT5190A_RID_BITMASK(rid), val;

	switch (mode) {
	case REGULATOR_MODE_FAST:
		val = mask;
		break;
	case REGULATOR_MODE_NORMAL:
		val = 0;
		break;
	default:
		return -EINVAL;
	}

	return regmap_update_bits(regmap, RT5190A_REG_DCDCCNTL, mask, val);
}

static unsigned int rt5190a_fixed_buck_get_mode(struct regulator_dev *rdev)
{
	struct regmap *regmap = rdev_get_regmap(rdev);
	int rid = rdev_get_id(rdev);
	unsigned int val;
	int ret;

	ret = regmap_read(regmap, RT5190A_REG_DCDCCNTL, &val);
	if (ret) {
		dev_err(&rdev->dev, "Failed to get mode [%d]\n", ret);
		return ret;
	}

	if (val & RT5190A_RID_BITMASK(rid))
		return REGULATOR_MODE_FAST;

	return REGULATOR_MODE_NORMAL;
}

static const struct regulator_ops rt5190a_ranged_buck_ops = {
	.enable	= regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.list_voltage = regulator_list_voltage_linear,
	.set_active_discharge = regulator_set_active_discharge_regmap,
	.get_error_flags = rt5190a_get_error_flags,
};

static const struct regulator_ops rt5190a_fixed_buck_ops = {
	.enable	= regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.set_active_discharge = regulator_set_active_discharge_regmap,
	.set_mode = rt5190a_fixed_buck_set_mode,
	.get_mode = rt5190a_fixed_buck_get_mode,
	.get_error_flags = rt5190a_get_error_flags,
};

static const struct regulator_ops rt5190a_fixed_ldo_ops = {
	.enable	= regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.set_active_discharge = regulator_set_active_discharge_regmap,
	.get_error_flags = rt5190a_get_error_flags,
};

static irqreturn_t rt5190a_irq_handler(int irq, void *data)
{
	struct rt5190a_priv *priv = data;
	__le32 raws;
	unsigned int events, fields;
	static const struct {
		unsigned int bitmask;
		unsigned int report;
	} event_tbl[] = {
		{ RT5190A_OVERVOLT_MASK, REGULATOR_ERROR_REGULATION_OUT },
		{ RT5190A_UNDERVOLT_MASK, REGULATOR_ERROR_UNDER_VOLTAGE }
	};
	int i, j, ret;

	ret = regmap_raw_read(priv->regmap, RT5190A_REG_OVINT, &raws,
			      sizeof(raws));
	if (ret) {
		dev_err(priv->dev, "Failed to read events\n");
		return IRQ_NONE;
	}

	events = le32_to_cpu(raws);

	ret = regmap_raw_write(priv->regmap, RT5190A_REG_OVINT, &raws,
			       sizeof(raws));
	if (ret)
		dev_err(priv->dev, "Failed to write-clear events\n");

	/* Handle OV,UV events */
	for (i = 0; i < ARRAY_SIZE(event_tbl); i++) {
		fields = events & event_tbl[i].bitmask;
		fields >>= ffs(event_tbl[i].bitmask) - 1;

		for (j = 0; j < RT5190A_MAX_IDX; j++) {
			if (!(fields & RT5190A_RID_BITMASK(j)))
				continue;

			regulator_notifier_call_chain(priv->rdev[j],
						      event_tbl[i].report,
						      NULL);
		}
	}

	/* Handle CH234 OT event */
	if (events & RT5190A_CH234OT_MASK) {
		for (j = RT5190A_IDX_BUCK2; j < RT5190A_IDX_LDO; j++) {
			regulator_notifier_call_chain(priv->rdev[j],
						      REGULATOR_ERROR_OVER_TEMP,
						      NULL);
		}
	}

	/* Warning if CHIP OT occur */
	if (events & RT5190A_CHIPOT_MASK)
		dev_warn(priv->dev, "CHIP overheat\n");

	return IRQ_HANDLED;
}

static unsigned int rt5190a_of_map_mode(unsigned int mode)
{
	switch (mode) {
	case RT5190A_OPMODE_AUTO:
		return REGULATOR_MODE_NORMAL;
	case RT5190A_OPMODE_FPWM:
		return REGULATOR_MODE_FAST;
	default:
		return REGULATOR_MODE_INVALID;
	}
}

static int rt5190a_of_parse_cb(struct rt5190a_priv *priv, int rid,
			       struct of_regulator_match *match)
{
	struct regulator_desc *desc = priv->rdesc + rid;
	struct regulator_init_data *init_data = match->init_data;
	struct device_node *np = match->of_node;
	bool latchup_enable;
	unsigned int mask = RT5190A_RID_BITMASK(rid), val;

	if (!init_data)
		return 0;

	switch (rid) {
	case RT5190A_IDX_BUCK1:
	case RT5190A_IDX_BUCK4:
	case RT5190A_IDX_LDO:
		init_data->constraints.apply_uV = 0;

		if (init_data->constraints.min_uV ==
				init_data->constraints.max_uV)
			desc->fixed_uV = init_data->constraints.min_uV;
		else {
			dev_err(priv->dev,
				"Variable voltage for fixed regulator\n");
			return -EINVAL;
		}
		break;
	default:
		break;
	}

	latchup_enable = of_property_read_bool(np, "richtek,latchup-enable");

	/* latchup: 0, default hiccup: 1 */
	val = !latchup_enable ? mask : 0;

	return regmap_update_bits(priv->regmap, RT5190A_REG_PROTMODE, mask, val);
}

static void rt5190a_fillin_regulator_desc(struct regulator_desc *desc, int rid)
{
	static const char * const regu_name[] = { "buck1", "buck2",
						  "buck3", "buck4",
						  "ldo" };
	static const char * const supply[] = { NULL, "vin2", "vin3", "vin4",
					       "vinldo" };

	desc->name = regu_name[rid];
	desc->supply_name = supply[rid];
	desc->owner = THIS_MODULE;
	desc->type = REGULATOR_VOLTAGE;
	desc->id = rid;
	desc->enable_reg = RT5190A_REG_ENABLE;
	desc->enable_mask = RT5190A_RID_BITMASK(rid);
	desc->active_discharge_reg = RT5190A_REG_DISCHARGE;
	desc->active_discharge_mask = RT5190A_RID_BITMASK(rid);
	desc->active_discharge_on = RT5190A_RID_BITMASK(rid);

	switch (rid) {
	case RT5190A_IDX_BUCK1:
		desc->active_discharge_mask = RT5190A_BUCK1_DISCHG_MASK;
		desc->active_discharge_on = RT5190A_BUCK1_DISCHG_ONVAL;
		desc->n_voltages = 1;
		desc->ops = &rt5190a_fixed_buck_ops;
		desc->of_map_mode = rt5190a_of_map_mode;
		break;
	case RT5190A_IDX_BUCK2:
		desc->vsel_reg = RT5190A_REG_BUCK2VSEL;
		desc->vsel_mask = RT5190A_VSEL_MASK;
		desc->min_uV = RT5190A_BUCK23_MINUV;
		desc->uV_step = RT5190A_BUCK23_STEPUV;
		desc->n_voltages = RT5190A_BUCK23_STEPNUM;
		desc->ops = &rt5190a_ranged_buck_ops;
		break;
	case RT5190A_IDX_BUCK3:
		desc->vsel_reg = RT5190A_REG_BUCK3VSEL;
		desc->vsel_mask = RT5190A_VSEL_MASK;
		desc->min_uV = RT5190A_BUCK23_MINUV;
		desc->uV_step = RT5190A_BUCK23_STEPUV;
		desc->n_voltages = RT5190A_BUCK23_STEPNUM;
		desc->ops = &rt5190a_ranged_buck_ops;
		break;
	case RT5190A_IDX_BUCK4:
		desc->n_voltages = 1;
		desc->ops = &rt5190a_fixed_buck_ops;
		desc->of_map_mode = rt5190a_of_map_mode;
		break;
	case RT5190A_IDX_LDO:
		desc->n_voltages = 1;
		desc->ops = &rt5190a_fixed_ldo_ops;
		break;
	}
}

static struct of_regulator_match rt5190a_regulator_match[] = {
	{ .name = "buck1", },
	{ .name = "buck2", },
	{ .name = "buck3", },
	{ .name = "buck4", },
	{ .name = "ldo", }
};

static int rt5190a_parse_regulator_dt_data(struct rt5190a_priv *priv)
{
	struct device_node *regulator_np;
	struct regulator_desc *reg_desc;
	struct of_regulator_match *match;
	int i, ret;

	for (i = 0; i < RT5190A_MAX_IDX; i++) {
		reg_desc = priv->rdesc + i;
		match = rt5190a_regulator_match + i;

		rt5190a_fillin_regulator_desc(reg_desc, i);

		match->desc = reg_desc;
	}

	regulator_np = of_get_child_by_name(priv->dev->of_node, "regulators");
	if (!regulator_np) {
		dev_err(priv->dev, "Could not find 'regulators' node\n");
		return -ENODEV;
	}

	ret = of_regulator_match(priv->dev, regulator_np,
				 rt5190a_regulator_match,
				 ARRAY_SIZE(rt5190a_regulator_match));

	of_node_put(regulator_np);

	if (ret < 0) {
		dev_err(priv->dev,
			"Error parsing regulator init data: %d\n", ret);
		return ret;
	}

	for (i = 0; i < RT5190A_MAX_IDX; i++) {
		match = rt5190a_regulator_match + i;

		ret = rt5190a_of_parse_cb(priv, i, match);
		if (ret) {
			dev_err(priv->dev, "Failed in [%d] of_parse_cb\n", i);
			return ret;
		}
	}

	return 0;
}

static const struct reg_sequence rt5190a_init_patch[] = {
	{ 0x09, 0x3d, },
	{ 0x0a, 0x3e, },
	{ 0x0b, 0x01, },
	{ 0x10, 0xff, },
	{ 0x11, 0xff, },
	{ 0x12, 0xff, },
	{ 0x13, 0xff, },
	{ 0x14, 0, },
	{ 0x15, 0, },
	{ 0x16, 0x3e, },
	{ 0x17, 0, }
};

static int rt5190a_device_initialize(struct rt5190a_priv *priv)
{
	bool mute_enable;
	int ret;

	ret = regmap_register_patch(priv->regmap, rt5190a_init_patch,
				    ARRAY_SIZE(rt5190a_init_patch));
	if (ret) {
		dev_err(priv->dev, "Failed to do register patch\n");
		return ret;
	}

	mute_enable = device_property_read_bool(priv->dev,
						"richtek,mute-enable");

	if (mute_enable) {
		ret = regmap_write(priv->regmap, RT5190A_REG_MUTECNTL, 0x00);
		if (ret) {
			dev_err(priv->dev, "Failed to enable mute function\n");
			return ret;
		}
	}

	return 0;
}

static int rt5190a_device_check(struct rt5190a_priv *priv)
{
	u16 devid;
	int ret;

	ret = regmap_raw_read(priv->regmap, RT5190A_REG_MANUFACTURE, &devid,
			      sizeof(devid));
	if (ret)
		return ret;

	if (devid) {
		dev_err(priv->dev, "Incorrect device id 0x%04x\n", devid);
		return -ENODEV;
	}

	return 0;
}

static const struct regmap_config rt5190a_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = RT5190A_REG_HOTDIEMASK,
};

static int rt5190a_probe(struct i2c_client *i2c)
{
	struct rt5190a_priv *priv;
	struct regulator_config cfg = {};
	int i, ret;

	priv = devm_kzalloc(&i2c->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = &i2c->dev;

	priv->regmap = devm_regmap_init_i2c(i2c, &rt5190a_regmap_config);
	if (IS_ERR(priv->regmap)) {
		dev_err(&i2c->dev, "Failed to allocate regmap\n");
		return PTR_ERR(priv->regmap);
	}

	ret = rt5190a_device_check(priv);
	if (ret) {
		dev_err(&i2c->dev, "Failed to check device %d\n", ret);
		return ret;
	}

	ret = rt5190a_device_initialize(priv);
	if (ret) {
		dev_err(&i2c->dev, "Failed to initialize the device\n");
		return ret;
	}

	ret = rt5190a_parse_regulator_dt_data(priv);
	if (ret) {
		dev_err(&i2c->dev, "Failed to parse regulator dt\n");
		return ret;
	}

	cfg.dev = &i2c->dev;
	cfg.regmap = priv->regmap;

	for (i = 0; i < RT5190A_MAX_IDX; i++) {
		struct regulator_desc *desc = priv->rdesc + i;
		struct of_regulator_match *match = rt5190a_regulator_match + i;

		cfg.init_data = match->init_data;
		cfg.of_node = match->of_node;

		priv->rdev[i] = devm_regulator_register(&i2c->dev, desc, &cfg);
		if (IS_ERR(priv->rdev[i])) {
			dev_err(&i2c->dev, "Failed to register regulator %s\n",
				desc->name);
			return PTR_ERR(priv->rdev[i]);
		}
	}

	if (i2c->irq) {
		ret = devm_request_threaded_irq(&i2c->dev, i2c->irq, NULL,
						rt5190a_irq_handler,
						IRQF_ONESHOT,
						dev_name(&i2c->dev), priv);
		if (ret) {
			dev_err(&i2c->dev, "Failed to register interrupt\n");
			return ret;
		}
	}

	return 0;
}

static const struct of_device_id __maybe_unused rt5190a_device_table[] = {
	{ .compatible = "richtek,rt5190a", },
	{}
};
MODULE_DEVICE_TABLE(of, rt5190a_device_table);

static struct i2c_driver rt5190a_driver = {
	.driver = {
		.name = "rt5190a",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = rt5190a_device_table,
	},
	.probe = rt5190a_probe,
};
module_i2c_driver(rt5190a_driver);

MODULE_AUTHOR("ChiYuan Huang <cy_huang@richtek.com>");
MODULE_DESCRIPTION("Richtek RT5190A Regulator Driver");
MODULE_LICENSE("GPL v2");
