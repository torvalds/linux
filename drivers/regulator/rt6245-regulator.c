// SPDX-License-Identifier: GPL-2.0+

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>

#define RT6245_VIRT_OCLIMIT	0x00
#define RT6245_VIRT_OTLEVEL	0x01
#define RT6245_VIRT_PGDLYTIME	0x02
#define RT6245_VIRT_SLEWRATE	0x03
#define RT6245_VIRT_SWFREQ	0x04
#define RT6245_VIRT_VOUT	0x05

#define RT6245_VOUT_MASK	GENMASK(6, 0)
#define RT6245_SLEW_MASK	GENMASK(2, 0)
#define RT6245_CHKSUM_MASK	BIT(7)
#define RT6245_CODE_MASK	GENMASK(6, 0)

/* HW Enable + Soft start time */
#define RT6245_ENTIME_IN_US	5000

#define RT6245_VOUT_MINUV	437500
#define RT6245_VOUT_MAXUV	1387500
#define RT6245_VOUT_STEPUV	12500
#define RT6245_NUM_VOUT		((RT6245_VOUT_MAXUV - RT6245_VOUT_MINUV) / RT6245_VOUT_STEPUV + 1)

struct rt6245_priv {
	struct gpio_desc *enable_gpio;
	bool enable_state;
};

static int rt6245_enable(struct regulator_dev *rdev)
{
	struct rt6245_priv *priv = rdev_get_drvdata(rdev);
	struct regmap *regmap = rdev_get_regmap(rdev);
	int ret;

	if (!priv->enable_gpio)
		return 0;

	gpiod_direction_output(priv->enable_gpio, 1);
	usleep_range(RT6245_ENTIME_IN_US, RT6245_ENTIME_IN_US + 1000);

	regcache_cache_only(regmap, false);
	ret = regcache_sync(regmap);
	if (ret)
		return ret;

	priv->enable_state = true;
	return 0;
}

static int rt6245_disable(struct regulator_dev *rdev)
{
	struct rt6245_priv *priv = rdev_get_drvdata(rdev);
	struct regmap *regmap = rdev_get_regmap(rdev);

	if (!priv->enable_gpio)
		return -EINVAL;

	regcache_cache_only(regmap, true);
	regcache_mark_dirty(regmap);

	gpiod_direction_output(priv->enable_gpio, 0);

	priv->enable_state = false;
	return 0;
}

static int rt6245_is_enabled(struct regulator_dev *rdev)
{
	struct rt6245_priv *priv = rdev_get_drvdata(rdev);

	return priv->enable_state ? 1 : 0;
}

static const struct regulator_ops rt6245_regulator_ops = {
	.list_voltage = regulator_list_voltage_linear,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_ramp_delay = regulator_set_ramp_delay_regmap,
	.enable = rt6245_enable,
	.disable = rt6245_disable,
	.is_enabled = rt6245_is_enabled,
};

/* ramp delay dividend is 12500 uV/uS, and divisor from 1 to 8 */
static const unsigned int rt6245_ramp_delay_table[] = {
	12500, 6250, 4167, 3125, 2500, 2083, 1786, 1562
};

static const struct regulator_desc rt6245_regulator_desc = {
	.name = "rt6245-regulator",
	.ops = &rt6245_regulator_ops,
	.type = REGULATOR_VOLTAGE,
	.min_uV = RT6245_VOUT_MINUV,
	.uV_step = RT6245_VOUT_STEPUV,
	.n_voltages = RT6245_NUM_VOUT,
	.ramp_delay_table = rt6245_ramp_delay_table,
	.n_ramp_values = ARRAY_SIZE(rt6245_ramp_delay_table),
	.owner = THIS_MODULE,
	.vsel_reg = RT6245_VIRT_VOUT,
	.vsel_mask = RT6245_VOUT_MASK,
	.ramp_reg = RT6245_VIRT_SLEWRATE,
	.ramp_mask = RT6245_SLEW_MASK,
};

static int rt6245_init_device_properties(struct device *dev)
{
	const struct {
		const char *name;
		unsigned int reg;
	} rt6245_props[] = {
		{ "richtek,oc-level-select",  RT6245_VIRT_OCLIMIT },
		{ "richtek,ot-level-select", RT6245_VIRT_OTLEVEL },
		{ "richtek,pgdly-time-select", RT6245_VIRT_PGDLYTIME },
		{ "richtek,switch-freq-select", RT6245_VIRT_SWFREQ }
	};
	struct regmap *regmap = dev_get_regmap(dev, NULL);
	u8 propval;
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(rt6245_props); i++) {
		ret = device_property_read_u8(dev, rt6245_props[i].name, &propval);
		if (ret)
			continue;

		ret = regmap_write(regmap, rt6245_props[i].reg, propval);
		if (ret) {
			dev_err(dev, "Fail to apply [%s:%d]\n", rt6245_props[i].name, propval);
			return ret;
		}
	}

	return 0;
}

static int rt6245_reg_write(void *context, unsigned int reg, unsigned int val)
{
	struct i2c_client *i2c = context;
	const u8 func_base[] = { 0x6F, 0x73, 0x78, 0x61, 0x7C, 0 };
	unsigned int code, bit_count;

	code = func_base[reg];
	code += val;

	/* xor checksum for bit 6 to 0 */
	bit_count = hweight8(code & RT6245_CODE_MASK);
	if (bit_count % 2)
		code |= RT6245_CHKSUM_MASK;
	else
		code &= ~RT6245_CHKSUM_MASK;

	return i2c_smbus_write_byte(i2c, code);
}

static const struct reg_default rt6245_reg_defaults[] = {
	/* Default over current 14A */
	{ RT6245_VIRT_OCLIMIT, 2 },
	/* Default over temperature 150'c */
	{ RT6245_VIRT_OTLEVEL, 0 },
	/* Default power good delay time 10us */
	{ RT6245_VIRT_PGDLYTIME, 1 },
	/* Default slewrate 12.5mV/uS */
	{ RT6245_VIRT_SLEWRATE, 0 },
	/* Default switch frequency 800KHz */
	{ RT6245_VIRT_SWFREQ, 1 },
	/* Default voltage 750mV */
	{ RT6245_VIRT_VOUT, 0x19 }
};

static const struct regmap_config rt6245_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = RT6245_VIRT_VOUT,
	.cache_type = REGCACHE_FLAT,
	.reg_defaults = rt6245_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(rt6245_reg_defaults),
	.reg_write = rt6245_reg_write,
};

static int rt6245_probe(struct i2c_client *i2c)
{
	struct rt6245_priv *priv;
	struct regmap *regmap;
	struct regulator_config regulator_cfg = {};
	struct regulator_dev *rdev;
	int ret;

	priv = devm_kzalloc(&i2c->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->enable_state = true;

	priv->enable_gpio = devm_gpiod_get_optional(&i2c->dev, "enable", GPIOD_OUT_HIGH);
	if (IS_ERR(priv->enable_gpio)) {
		dev_err(&i2c->dev, "Failed to get 'enable' gpio\n");
		return PTR_ERR(priv->enable_gpio);
	}

	usleep_range(RT6245_ENTIME_IN_US, RT6245_ENTIME_IN_US + 1000);

	regmap = devm_regmap_init(&i2c->dev, NULL, i2c, &rt6245_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&i2c->dev, "Failed to initialize the regmap\n");
		return PTR_ERR(regmap);
	}

	ret = rt6245_init_device_properties(&i2c->dev);
	if (ret) {
		dev_err(&i2c->dev, "Failed to initialize device properties\n");
		return ret;
	}

	regulator_cfg.dev = &i2c->dev;
	regulator_cfg.of_node = i2c->dev.of_node;
	regulator_cfg.regmap = regmap;
	regulator_cfg.driver_data = priv;
	regulator_cfg.init_data = of_get_regulator_init_data(&i2c->dev, i2c->dev.of_node,
							     &rt6245_regulator_desc);
	rdev = devm_regulator_register(&i2c->dev, &rt6245_regulator_desc, &regulator_cfg);
	if (IS_ERR(rdev)) {
		dev_err(&i2c->dev, "Failed to register regulator\n");
		return PTR_ERR(rdev);
	}

	return 0;
}

static const struct of_device_id __maybe_unused rt6245_of_match_table[] = {
	{ .compatible = "richtek,rt6245", },
	{}
};
MODULE_DEVICE_TABLE(of, rt6245_of_match_table);

static struct i2c_driver rt6245_driver = {
	.driver = {
		.name = "rt6245",
		.of_match_table = rt6245_of_match_table,
	},
	.probe_new = rt6245_probe,
};
module_i2c_driver(rt6245_driver);

MODULE_AUTHOR("ChiYuan Huang <cy_huang@richtek.com>");
MODULE_DESCRIPTION("Richtek RT6245 Regulator Driver");
MODULE_LICENSE("GPL v2");
