// SPDX-License-Identifier: GPL-2.0+

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>

#define RTMV20_REG_DEVINFO	0x00
#define RTMV20_REG_PULSEDELAY	0x01
#define RTMV20_REG_PULSEWIDTH	0x03
#define RTMV20_REG_LDCTRL1	0x05
#define RTMV20_REG_ESPULSEWIDTH	0x06
#define RTMV20_REG_ESLDCTRL1	0x08
#define RTMV20_REG_LBP		0x0A
#define RTMV20_REG_LDCTRL2	0x0B
#define RTMV20_REG_FSIN1CTRL1	0x0D
#define RTMV20_REG_FSIN1CTRL3	0x0F
#define RTMV20_REG_FSIN2CTRL1	0x10
#define RTMV20_REG_FSIN2CTRL3	0x12
#define RTMV20_REG_ENCTRL	0x13
#define RTMV20_REG_STRBVSYNDLYL 0x29
#define RTMV20_REG_LDIRQ	0x30
#define RTMV20_REG_LDSTAT	0x40
#define RTMV20_REG_LDMASK	0x50

#define RTMV20_VID_MASK		GENMASK(7, 4)
#define RICHTEK_VID		0x80
#define RTMV20_LDCURR_MASK	GENMASK(7, 0)
#define RTMV20_DELAY_MASK	GENMASK(9, 0)
#define RTMV20_WIDTH_MASK	GENMASK(13, 0)
#define RTMV20_WIDTH2_MASK	GENMASK(7, 0)
#define RTMV20_LBPLVL_MASK	GENMASK(3, 0)
#define RTMV20_LBPEN_MASK	BIT(7)
#define RTMV20_STROBEPOL_MASK	BIT(1)
#define RTMV20_VSYNPOL_MASK	BIT(1)
#define RTMV20_FSINEN_MASK	BIT(7)
#define RTMV20_ESEN_MASK	BIT(6)
#define RTMV20_FSINOUT_MASK	BIT(2)
#define LDENABLE_MASK		(BIT(3) | BIT(0))

#define OTPEVT_MASK		BIT(4)
#define SHORTEVT_MASK		BIT(3)
#define OPENEVT_MASK		BIT(2)
#define LBPEVT_MASK		BIT(1)
#define OCPEVT_MASK		BIT(0)
#define FAILEVT_MASK		(SHORTEVT_MASK | OPENEVT_MASK | LBPEVT_MASK)

#define RTMV20_LSW_MINUA	0
#define RTMV20_LSW_MAXUA	6000000
#define RTMV20_LSW_STEPUA	30000

#define RTMV20_LSW_DEFAULTUA	3000000

#define RTMV20_I2CRDY_TIMEUS	200
#define RTMV20_CSRDY_TIMEUS	2000

struct rtmv20_priv {
	struct device *dev;
	struct regmap *regmap;
	struct gpio_desc *enable_gpio;
	struct regulator_dev *rdev;
};

static int rtmv20_lsw_enable(struct regulator_dev *rdev)
{
	struct rtmv20_priv *priv = rdev_get_drvdata(rdev);
	int ret;

	gpiod_set_value(priv->enable_gpio, 1);

	/* Wait for I2C can be accessed */
	usleep_range(RTMV20_I2CRDY_TIMEUS, RTMV20_I2CRDY_TIMEUS + 100);

	/* HW re-enable, disable cache only and sync regcache here */
	regcache_cache_only(priv->regmap, false);
	ret = regcache_sync(priv->regmap);
	if (ret)
		return ret;

	return regulator_enable_regmap(rdev);
}

static int rtmv20_lsw_disable(struct regulator_dev *rdev)
{
	struct rtmv20_priv *priv = rdev_get_drvdata(rdev);
	int ret;

	ret = regulator_disable_regmap(rdev);
	if (ret)
		return ret;

	/* Mark the regcache as dirty and cache only before HW disabled */
	regcache_cache_only(priv->regmap, true);
	regcache_mark_dirty(priv->regmap);

	gpiod_set_value(priv->enable_gpio, 0);

	return 0;
}

static const struct regulator_ops rtmv20_regulator_ops = {
	.set_current_limit = regulator_set_current_limit_regmap,
	.get_current_limit = regulator_get_current_limit_regmap,
	.enable = rtmv20_lsw_enable,
	.disable = rtmv20_lsw_disable,
	.is_enabled = regulator_is_enabled_regmap,
};

static const struct regulator_desc rtmv20_lsw_desc = {
	.name = "rtmv20,lsw",
	.of_match = of_match_ptr("lsw"),
	.type = REGULATOR_CURRENT,
	.owner = THIS_MODULE,
	.ops = &rtmv20_regulator_ops,
	.csel_reg = RTMV20_REG_LDCTRL1,
	.csel_mask = RTMV20_LDCURR_MASK,
	.enable_reg = RTMV20_REG_ENCTRL,
	.enable_mask = LDENABLE_MASK,
	.enable_time = RTMV20_CSRDY_TIMEUS,
};

static irqreturn_t rtmv20_irq_handler(int irq, void *data)
{
	struct rtmv20_priv *priv = data;
	unsigned int val;
	int ret;

	ret = regmap_read(priv->regmap, RTMV20_REG_LDIRQ, &val);
	if (ret) {
		dev_err(priv->dev, "Failed to get irq flags\n");
		return IRQ_NONE;
	}

	if (val & OTPEVT_MASK)
		regulator_notifier_call_chain(priv->rdev, REGULATOR_EVENT_OVER_TEMP, NULL);

	if (val & OCPEVT_MASK)
		regulator_notifier_call_chain(priv->rdev, REGULATOR_EVENT_OVER_CURRENT, NULL);

	if (val & FAILEVT_MASK)
		regulator_notifier_call_chain(priv->rdev, REGULATOR_EVENT_FAIL, NULL);

	return IRQ_HANDLED;
}

static u32 clamp_to_selector(u32 val, u32 min, u32 max, u32 step)
{
	u32 retval = clamp_val(val, min, max);

	return (retval - min) / step;
}

static int rtmv20_properties_init(struct rtmv20_priv *priv)
{
	const struct {
		const char *name;
		u32 def;
		u32 min;
		u32 max;
		u32 step;
		u32 addr;
		u32 mask;
	} props[] = {
		{ "richtek,ld-pulse-delay-us", 0, 0, 100000, 100, RTMV20_REG_PULSEDELAY,
			RTMV20_DELAY_MASK },
		{ "richtek,ld-pulse-width-us", 1200, 0, 10000, 1, RTMV20_REG_PULSEWIDTH,
			RTMV20_WIDTH_MASK },
		{ "richtek,fsin1-delay-us", 23000, 0, 100000, 100, RTMV20_REG_FSIN1CTRL1,
			RTMV20_DELAY_MASK },
		{ "richtek,fsin1-width-us", 160, 40, 10000, 40, RTMV20_REG_FSIN1CTRL3,
			RTMV20_WIDTH2_MASK },
		{ "richtek,fsin2-delay-us", 23000, 0, 100000, 100, RTMV20_REG_FSIN2CTRL1,
			RTMV20_DELAY_MASK },
		{ "richtek,fsin2-width-us", 160, 40, 10000, 40, RTMV20_REG_FSIN2CTRL3,
			RTMV20_WIDTH2_MASK },
		{ "richtek,es-pulse-width-us", 1200, 0, 10000, 1, RTMV20_REG_ESPULSEWIDTH,
			RTMV20_WIDTH_MASK },
		{ "richtek,es-ld-current-microamp", 3000000, 0, 6000000, 30000,
			RTMV20_REG_ESLDCTRL1, RTMV20_LDCURR_MASK },
		{ "richtek,lbp-level-microvolt", 2700000, 2400000, 3700000, 100000, RTMV20_REG_LBP,
			RTMV20_LBPLVL_MASK },
		{ "richtek,lbp-enable", 0, 0, 1, 1, RTMV20_REG_LBP, RTMV20_LBPEN_MASK },
		{ "richtek,strobe-polarity-high", 1, 0, 1, 1, RTMV20_REG_LDCTRL2,
			RTMV20_STROBEPOL_MASK },
		{ "richtek,vsync-polarity-high", 1, 0, 1, 1, RTMV20_REG_LDCTRL2,
			RTMV20_VSYNPOL_MASK },
		{ "richtek,fsin-enable", 0, 0, 1, 1, RTMV20_REG_ENCTRL, RTMV20_FSINEN_MASK },
		{ "richtek,fsin-output", 0, 0, 1, 1, RTMV20_REG_ENCTRL, RTMV20_FSINOUT_MASK },
		{ "richtek,es-enable", 0, 0, 1, 1, RTMV20_REG_ENCTRL, RTMV20_ESEN_MASK },
	};
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(props); i++) {
		__be16 bval16;
		u16 val16;
		u32 temp;
		int significant_bit = fls(props[i].mask);
		int shift = ffs(props[i].mask) - 1;

		if (props[i].max > 1) {
			ret = device_property_read_u32(priv->dev, props[i].name, &temp);
			if (ret)
				temp = props[i].def;
		} else
			temp = device_property_read_bool(priv->dev, props[i].name);

		temp = clamp_to_selector(temp, props[i].min, props[i].max, props[i].step);

		/* If significant bit is over 8, two byte access, others one */
		if (significant_bit > 8) {
			ret = regmap_raw_read(priv->regmap, props[i].addr, &bval16, sizeof(bval16));
			if (ret)
				return ret;

			val16 = be16_to_cpu(bval16);
			val16 &= ~props[i].mask;
			val16 |= (temp << shift);
			bval16 = cpu_to_be16(val16);

			ret = regmap_raw_write(priv->regmap, props[i].addr, &bval16,
					       sizeof(bval16));
		} else {
			ret = regmap_update_bits(priv->regmap, props[i].addr, props[i].mask,
						 temp << shift);
		}

		if (ret)
			return ret;
	}

	return 0;
}

static int rtmv20_check_chip_exist(struct rtmv20_priv *priv)
{
	unsigned int val;
	int ret;

	ret = regmap_read(priv->regmap, RTMV20_REG_DEVINFO, &val);
	if (ret)
		return ret;

	if ((val & RTMV20_VID_MASK) != RICHTEK_VID)
		return -ENODEV;

	return 0;
}

static bool rtmv20_is_accessible_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case RTMV20_REG_DEVINFO ... RTMV20_REG_STRBVSYNDLYL:
	case RTMV20_REG_LDIRQ:
	case RTMV20_REG_LDSTAT:
	case RTMV20_REG_LDMASK:
		return true;
	}
	return false;
}

static bool rtmv20_is_volatile_reg(struct device *dev, unsigned int reg)
{
	if (reg == RTMV20_REG_LDIRQ || reg == RTMV20_REG_LDSTAT)
		return true;
	return false;
}

static const struct regmap_config rtmv20_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.cache_type = REGCACHE_RBTREE,
	.max_register = RTMV20_REG_LDMASK,

	.writeable_reg = rtmv20_is_accessible_reg,
	.readable_reg = rtmv20_is_accessible_reg,
	.volatile_reg = rtmv20_is_volatile_reg,
};

static int rtmv20_probe(struct i2c_client *i2c)
{
	struct rtmv20_priv *priv;
	struct regulator_config config = {};
	int ret;

	priv = devm_kzalloc(&i2c->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = &i2c->dev;

	/* Before regmap register, configure HW enable to make I2C accessible */
	priv->enable_gpio = devm_gpiod_get(&i2c->dev, "enable", GPIOD_OUT_HIGH);
	if (IS_ERR(priv->enable_gpio)) {
		dev_err(&i2c->dev, "Failed to get enable gpio\n");
		return PTR_ERR(priv->enable_gpio);
	}

	/* Wait for I2C can be accessed */
	usleep_range(RTMV20_I2CRDY_TIMEUS, RTMV20_I2CRDY_TIMEUS + 100);

	priv->regmap = devm_regmap_init_i2c(i2c, &rtmv20_regmap_config);
	if (IS_ERR(priv->regmap)) {
		dev_err(&i2c->dev, "Failed to allocate register map\n");
		return PTR_ERR(priv->regmap);
	}

	ret = rtmv20_check_chip_exist(priv);
	if (ret) {
		dev_err(&i2c->dev, "Chip vendor info is not matched\n");
		return ret;
	}

	ret = rtmv20_properties_init(priv);
	if (ret) {
		dev_err(&i2c->dev, "Failed to init properties\n");
		return ret;
	}

	/*
	 * keep in shutdown mode to minimize the current consumption
	 * and also mark regcache as dirty
	 */
	regcache_cache_only(priv->regmap, true);
	regcache_mark_dirty(priv->regmap);
	gpiod_set_value(priv->enable_gpio, 0);

	config.dev = &i2c->dev;
	config.regmap = priv->regmap;
	config.driver_data = priv;
	priv->rdev = devm_regulator_register(&i2c->dev, &rtmv20_lsw_desc, &config);
	if (IS_ERR(priv->rdev)) {
		dev_err(&i2c->dev, "Failed to register regulator\n");
		return PTR_ERR(priv->rdev);
	}

	/* Unmask all events before IRQ registered */
	ret = regmap_write(priv->regmap, RTMV20_REG_LDMASK, 0);
	if (ret)
		return ret;

	return devm_request_threaded_irq(&i2c->dev, i2c->irq, NULL, rtmv20_irq_handler,
					 IRQF_ONESHOT, dev_name(&i2c->dev), priv);
}

static int __maybe_unused rtmv20_suspend(struct device *dev)
{
	struct i2c_client *i2c = to_i2c_client(dev);

	/*
	 * When system suspend, disable irq to prevent interrupt trigger
	 * during I2C bus suspend
	 */
	disable_irq(i2c->irq);
	if (device_may_wakeup(dev))
		enable_irq_wake(i2c->irq);

	return 0;
}

static int __maybe_unused rtmv20_resume(struct device *dev)
{
	struct i2c_client *i2c = to_i2c_client(dev);

	/* Enable irq after I2C bus already resume */
	enable_irq(i2c->irq);
	if (device_may_wakeup(dev))
		disable_irq_wake(i2c->irq);

	return 0;
}

static SIMPLE_DEV_PM_OPS(rtmv20_pm, rtmv20_suspend, rtmv20_resume);

static const struct of_device_id __maybe_unused rtmv20_of_id[] = {
	{ .compatible = "richtek,rtmv20", },
	{}
};
MODULE_DEVICE_TABLE(of, rtmv20_of_id);

static struct i2c_driver rtmv20_driver = {
	.driver = {
		.name = "rtmv20",
		.of_match_table = of_match_ptr(rtmv20_of_id),
		.pm = &rtmv20_pm,
	},
	.probe_new = rtmv20_probe,
};
module_i2c_driver(rtmv20_driver);

MODULE_AUTHOR("ChiYuan Huang <cy_huang@richtek.com>");
MODULE_DESCRIPTION("Richtek RTMV20 Regulator Driver");
MODULE_LICENSE("GPL v2");
