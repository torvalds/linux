// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2015-16 Golden Delicious Computers
 *
 * Author: Nikolaus Schaller <hns@goldelico.com>
 *
 * LED driver for the IS31FL319{0,1,3,6,9} to drive 1, 3, 6 or 9 light
 * effect LEDs.
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

/* register numbers */
#define IS31FL319X_SHUTDOWN		0x00
#define IS31FL319X_CTRL1		0x01
#define IS31FL319X_CTRL2		0x02
#define IS31FL319X_CONFIG1		0x03
#define IS31FL319X_CONFIG2		0x04
#define IS31FL319X_RAMP_MODE		0x05
#define IS31FL319X_BREATH_MASK		0x06
#define IS31FL319X_PWM(channel)		(0x07 + channel)
#define IS31FL319X_DATA_UPDATE		0x10
#define IS31FL319X_T0(channel)		(0x11 + channel)
#define IS31FL319X_T123_1		0x1a
#define IS31FL319X_T123_2		0x1b
#define IS31FL319X_T123_3		0x1c
#define IS31FL319X_T4(channel)		(0x1d + channel)
#define IS31FL319X_TIME_UPDATE		0x26
#define IS31FL319X_RESET		0xff

#define IS31FL319X_REG_CNT		(IS31FL319X_RESET + 1)

#define IS31FL319X_MAX_LEDS		9

/* CS (Current Setting) in CONFIG2 register */
#define IS31FL319X_CONFIG2_CS_SHIFT	4
#define IS31FL319X_CONFIG2_CS_MASK	0x7
#define IS31FL319X_CONFIG2_CS_STEP_REF	12

#define IS31FL319X_CURRENT_MIN		((u32)5000)
#define IS31FL319X_CURRENT_MAX		((u32)40000)
#define IS31FL319X_CURRENT_STEP		((u32)5000)
#define IS31FL319X_CURRENT_DEFAULT	((u32)20000)

/* Audio gain in CONFIG2 register */
#define IS31FL319X_AUDIO_GAIN_DB_MAX	((u32)21)
#define IS31FL319X_AUDIO_GAIN_DB_STEP	((u32)3)

/*
 * regmap is used as a cache of chip's register space,
 * to avoid reading back brightness values from chip,
 * which is known to hang.
 */
struct is31fl319x_chip {
	const struct is31fl319x_chipdef *cdef;
	struct i2c_client               *client;
	struct regmap                   *regmap;
	struct mutex                    lock;
	u32                             audio_gain_db;

	struct is31fl319x_led {
		struct is31fl319x_chip  *chip;
		struct led_classdev     cdev;
		u32                     max_microamp;
		bool                    configured;
	} leds[IS31FL319X_MAX_LEDS];
};

struct is31fl319x_chipdef {
	int num_leds;
};

static const struct is31fl319x_chipdef is31fl3190_cdef = {
	.num_leds = 1,
};

static const struct is31fl319x_chipdef is31fl3193_cdef = {
	.num_leds = 3,
};

static const struct is31fl319x_chipdef is31fl3196_cdef = {
	.num_leds = 6,
};

static const struct is31fl319x_chipdef is31fl3199_cdef = {
	.num_leds = 9,
};

static const struct of_device_id of_is31fl319x_match[] = {
	{ .compatible = "issi,is31fl3190", .data = &is31fl3190_cdef, },
	{ .compatible = "issi,is31fl3191", .data = &is31fl3190_cdef, },
	{ .compatible = "issi,is31fl3193", .data = &is31fl3193_cdef, },
	{ .compatible = "issi,is31fl3196", .data = &is31fl3196_cdef, },
	{ .compatible = "issi,is31fl3199", .data = &is31fl3199_cdef, },
	{ .compatible = "si-en,sn3199",    .data = &is31fl3199_cdef, },
	{ }
};
MODULE_DEVICE_TABLE(of, of_is31fl319x_match);

static int is31fl319x_brightness_set(struct led_classdev *cdev,
				     enum led_brightness brightness)
{
	struct is31fl319x_led *led = container_of(cdev, struct is31fl319x_led,
						  cdev);
	struct is31fl319x_chip *is31 = led->chip;
	int chan = led - is31->leds;
	int ret;
	int i;
	u8 ctrl1 = 0, ctrl2 = 0;

	dev_dbg(&is31->client->dev, "%s %d: %d\n", __func__, chan, brightness);

	mutex_lock(&is31->lock);

	/* update PWM register */
	ret = regmap_write(is31->regmap, IS31FL319X_PWM(chan), brightness);
	if (ret < 0)
		goto out;

	/* read current brightness of all PWM channels */
	for (i = 0; i < is31->cdef->num_leds; i++) {
		unsigned int pwm_value;
		bool on;

		/*
		 * since neither cdev nor the chip can provide
		 * the current setting, we read from the regmap cache
		 */

		ret = regmap_read(is31->regmap, IS31FL319X_PWM(i), &pwm_value);
		dev_dbg(&is31->client->dev, "%s read %d: ret=%d: %d\n",
			__func__, i, ret, pwm_value);
		on = ret >= 0 && pwm_value > LED_OFF;

		if (i < 3)
			ctrl1 |= on << i;       /* 0..2 => bit 0..2 */
		else if (i < 6)
			ctrl1 |= on << (i + 1); /* 3..5 => bit 4..6 */
		else
			ctrl2 |= on << (i - 6); /* 6..8 => bit 0..2 */
	}

	if (ctrl1 > 0 || ctrl2 > 0) {
		dev_dbg(&is31->client->dev, "power up %02x %02x\n",
			ctrl1, ctrl2);
		regmap_write(is31->regmap, IS31FL319X_CTRL1, ctrl1);
		regmap_write(is31->regmap, IS31FL319X_CTRL2, ctrl2);
		/* update PWMs */
		regmap_write(is31->regmap, IS31FL319X_DATA_UPDATE, 0x00);
		/* enable chip from shut down */
		ret = regmap_write(is31->regmap, IS31FL319X_SHUTDOWN, 0x01);
	} else {
		dev_dbg(&is31->client->dev, "power down\n");
		/* shut down (no need to clear CTRL1/2) */
		ret = regmap_write(is31->regmap, IS31FL319X_SHUTDOWN, 0x00);
	}

out:
	mutex_unlock(&is31->lock);

	return ret;
}

static int is31fl319x_parse_child_dt(const struct device *dev,
				     const struct device_node *child,
				     struct is31fl319x_led *led)
{
	struct led_classdev *cdev = &led->cdev;
	int ret;

	if (of_property_read_string(child, "label", &cdev->name))
		cdev->name = child->name;

	ret = of_property_read_string(child, "linux,default-trigger",
				      &cdev->default_trigger);
	if (ret < 0 && ret != -EINVAL) /* is optional */
		return ret;

	led->max_microamp = IS31FL319X_CURRENT_DEFAULT;
	ret = of_property_read_u32(child, "led-max-microamp",
				   &led->max_microamp);
	if (!ret) {
		if (led->max_microamp < IS31FL319X_CURRENT_MIN)
			return -EINVAL;	/* not supported */
		led->max_microamp = min(led->max_microamp,
					  IS31FL319X_CURRENT_MAX);
	}

	return 0;
}

static int is31fl319x_parse_dt(struct device *dev,
			       struct is31fl319x_chip *is31)
{
	struct device_node *np = dev->of_node, *child;
	const struct of_device_id *of_dev_id;
	int count;
	int ret;

	if (!np)
		return -ENODEV;

	of_dev_id = of_match_device(of_is31fl319x_match, dev);
	if (!of_dev_id) {
		dev_err(dev, "Failed to match device with supported chips\n");
		return -EINVAL;
	}

	is31->cdef = of_dev_id->data;

	count = of_get_child_count(np);

	dev_dbg(dev, "probe %s with %d leds defined in DT\n",
		of_dev_id->compatible, count);

	if (!count || count > is31->cdef->num_leds) {
		dev_err(dev, "Number of leds defined must be between 1 and %u\n",
			is31->cdef->num_leds);
		return -ENODEV;
	}

	for_each_child_of_node(np, child) {
		struct is31fl319x_led *led;
		u32 reg;

		ret = of_property_read_u32(child, "reg", &reg);
		if (ret) {
			dev_err(dev, "Failed to read led 'reg' property\n");
			goto put_child_node;
		}

		if (reg < 1 || reg > is31->cdef->num_leds) {
			dev_err(dev, "invalid led reg %u\n", reg);
			ret = -EINVAL;
			goto put_child_node;
		}

		led = &is31->leds[reg - 1];

		if (led->configured) {
			dev_err(dev, "led %u is already configured\n", reg);
			ret = -EINVAL;
			goto put_child_node;
		}

		ret = is31fl319x_parse_child_dt(dev, child, led);
		if (ret) {
			dev_err(dev, "led %u DT parsing failed\n", reg);
			goto put_child_node;
		}

		led->configured = true;
	}

	is31->audio_gain_db = 0;
	ret = of_property_read_u32(np, "audio-gain-db", &is31->audio_gain_db);
	if (!ret)
		is31->audio_gain_db = min(is31->audio_gain_db,
					  IS31FL319X_AUDIO_GAIN_DB_MAX);

	return 0;

put_child_node:
	of_node_put(child);
	return ret;
}

static bool is31fl319x_readable_reg(struct device *dev, unsigned int reg)
{ /* we have no readable registers */
	return false;
}

static bool is31fl319x_volatile_reg(struct device *dev, unsigned int reg)
{ /* volatile registers are not cached */
	switch (reg) {
	case IS31FL319X_DATA_UPDATE:
	case IS31FL319X_TIME_UPDATE:
	case IS31FL319X_RESET:
		return true; /* always write-through */
	default:
		return false;
	}
}

static const struct reg_default is31fl319x_reg_defaults[] = {
	{ IS31FL319X_CONFIG1, 0x00},
	{ IS31FL319X_CONFIG2, 0x00},
	{ IS31FL319X_PWM(0), 0x00},
	{ IS31FL319X_PWM(1), 0x00},
	{ IS31FL319X_PWM(2), 0x00},
	{ IS31FL319X_PWM(3), 0x00},
	{ IS31FL319X_PWM(4), 0x00},
	{ IS31FL319X_PWM(5), 0x00},
	{ IS31FL319X_PWM(6), 0x00},
	{ IS31FL319X_PWM(7), 0x00},
	{ IS31FL319X_PWM(8), 0x00},
};

static struct regmap_config regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = IS31FL319X_REG_CNT,
	.cache_type = REGCACHE_FLAT,
	.readable_reg = is31fl319x_readable_reg,
	.volatile_reg = is31fl319x_volatile_reg,
	.reg_defaults = is31fl319x_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(is31fl319x_reg_defaults),
};

static inline int is31fl319x_microamp_to_cs(struct device *dev, u32 microamp)
{ /* round down to nearest supported value (range check done by caller) */
	u32 step = microamp / IS31FL319X_CURRENT_STEP;

	return ((IS31FL319X_CONFIG2_CS_STEP_REF - step) &
		IS31FL319X_CONFIG2_CS_MASK) <<
		IS31FL319X_CONFIG2_CS_SHIFT; /* CS encoding */
}

static inline int is31fl319x_db_to_gain(u32 dezibel)
{ /* round down to nearest supported value (range check done by caller) */
	return dezibel / IS31FL319X_AUDIO_GAIN_DB_STEP;
}

static int is31fl319x_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	struct is31fl319x_chip *is31;
	struct device *dev = &client->dev;
	int err;
	int i = 0;
	u32 aggregated_led_microamp = IS31FL319X_CURRENT_MAX;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -EIO;

	is31 = devm_kzalloc(&client->dev, sizeof(*is31), GFP_KERNEL);
	if (!is31)
		return -ENOMEM;

	mutex_init(&is31->lock);

	err = is31fl319x_parse_dt(&client->dev, is31);
	if (err)
		goto free_mutex;

	is31->client = client;
	is31->regmap = devm_regmap_init_i2c(client, &regmap_config);
	if (IS_ERR(is31->regmap)) {
		dev_err(&client->dev, "failed to allocate register map\n");
		err = PTR_ERR(is31->regmap);
		goto free_mutex;
	}

	i2c_set_clientdata(client, is31);

	/* check for write-reply from chip (we can't read any registers) */
	err = regmap_write(is31->regmap, IS31FL319X_RESET, 0x00);
	if (err < 0) {
		dev_err(&client->dev, "no response from chip write: err = %d\n",
			err);
		err = -EIO; /* does not answer */
		goto free_mutex;
	}

	/*
	 * Kernel conventions require per-LED led-max-microamp property.
	 * But the chip does not allow to limit individual LEDs.
	 * So we take minimum from all subnodes for safety of hardware.
	 */
	for (i = 0; i < is31->cdef->num_leds; i++)
		if (is31->leds[i].configured &&
		    is31->leds[i].max_microamp < aggregated_led_microamp)
			aggregated_led_microamp = is31->leds[i].max_microamp;

	regmap_write(is31->regmap, IS31FL319X_CONFIG2,
		     is31fl319x_microamp_to_cs(dev, aggregated_led_microamp) |
		     is31fl319x_db_to_gain(is31->audio_gain_db));

	for (i = 0; i < is31->cdef->num_leds; i++) {
		struct is31fl319x_led *led = &is31->leds[i];

		if (!led->configured)
			continue;

		led->chip = is31;
		led->cdev.brightness_set_blocking = is31fl319x_brightness_set;

		err = devm_led_classdev_register(&client->dev, &led->cdev);
		if (err < 0)
			goto free_mutex;
	}

	return 0;

free_mutex:
	mutex_destroy(&is31->lock);
	return err;
}

static int is31fl319x_remove(struct i2c_client *client)
{
	struct is31fl319x_chip *is31 = i2c_get_clientdata(client);

	mutex_destroy(&is31->lock);
	return 0;
}

/*
 * i2c-core (and modalias) requires that id_table be properly filled,
 * even though it is not used for DeviceTree based instantiation.
 */
static const struct i2c_device_id is31fl319x_id[] = {
	{ "is31fl3190" },
	{ "is31fl3191" },
	{ "is31fl3193" },
	{ "is31fl3196" },
	{ "is31fl3199" },
	{ "sn3199" },
	{},
};
MODULE_DEVICE_TABLE(i2c, is31fl319x_id);

static struct i2c_driver is31fl319x_driver = {
	.driver   = {
		.name           = "leds-is31fl319x",
		.of_match_table = of_match_ptr(of_is31fl319x_match),
	},
	.probe    = is31fl319x_probe,
	.remove   = is31fl319x_remove,
	.id_table = is31fl319x_id,
};

module_i2c_driver(is31fl319x_driver);

MODULE_AUTHOR("H. Nikolaus Schaller <hns@goldelico.com>");
MODULE_AUTHOR("Andrey Utkin <andrey_utkin@fastmail.com>");
MODULE_DESCRIPTION("IS31FL319X LED driver");
MODULE_LICENSE("GPL v2");
