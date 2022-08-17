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
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>

/* register numbers */
#define IS31FL319X_SHUTDOWN		0x00

/* registers for 3190, 3191 and 3193 */
#define IS31FL3190_BREATHING		0x01
#define IS31FL3190_LEDMODE		0x02
#define IS31FL3190_CURRENT		0x03
#define IS31FL3190_PWM(channel)		(0x04 + channel)
#define IS31FL3190_DATA_UPDATE		0x07
#define IS31FL3190_T0(channel)		(0x0a + channel)
#define IS31FL3190_T1T2(channel)	(0x10 + channel)
#define IS31FL3190_T3T4(channel)	(0x16 + channel)
#define IS31FL3190_TIME_UPDATE		0x1c
#define IS31FL3190_LEDCONTROL		0x1d
#define IS31FL3190_RESET		0x2f

#define IS31FL3190_CURRENT_uA_MIN	5000
#define IS31FL3190_CURRENT_uA_DEFAULT	42000
#define IS31FL3190_CURRENT_uA_MAX	42000
#define IS31FL3190_CURRENT_MASK		GENMASK(4, 2)
#define IS31FL3190_CURRENT_5_mA		0x02
#define IS31FL3190_CURRENT_10_mA	0x01
#define IS31FL3190_CURRENT_17dot5_mA	0x04
#define IS31FL3190_CURRENT_30_mA	0x03
#define IS31FL3190_CURRENT_42_mA	0x00

/* registers for 3196 and 3199 */
#define IS31FL3196_CTRL1		0x01
#define IS31FL3196_CTRL2		0x02
#define IS31FL3196_CONFIG1		0x03
#define IS31FL3196_CONFIG2		0x04
#define IS31FL3196_RAMP_MODE		0x05
#define IS31FL3196_BREATH_MARK		0x06
#define IS31FL3196_PWM(channel)		(0x07 + channel)
#define IS31FL3196_DATA_UPDATE		0x10
#define IS31FL3196_T0(channel)		(0x11 + channel)
#define IS31FL3196_T123_1		0x1a
#define IS31FL3196_T123_2		0x1b
#define IS31FL3196_T123_3		0x1c
#define IS31FL3196_T4(channel)		(0x1d + channel)
#define IS31FL3196_TIME_UPDATE		0x26
#define IS31FL3196_RESET		0xff

#define IS31FL3196_REG_CNT		(IS31FL3196_RESET + 1)

#define IS31FL319X_MAX_LEDS		9

/* CS (Current Setting) in CONFIG2 register */
#define IS31FL3196_CONFIG2_CS_SHIFT	4
#define IS31FL3196_CONFIG2_CS_MASK	GENMASK(2, 0)
#define IS31FL3196_CONFIG2_CS_STEP_REF	12

#define IS31FL3196_CURRENT_uA_MIN	5000
#define IS31FL3196_CURRENT_uA_MAX	40000
#define IS31FL3196_CURRENT_uA_STEP	5000
#define IS31FL3196_CURRENT_uA_DEFAULT	20000

/* Audio gain in CONFIG2 register */
#define IS31FL3196_AUDIO_GAIN_DB_MAX	((u32)21)
#define IS31FL3196_AUDIO_GAIN_DB_STEP	3

/*
 * regmap is used as a cache of chip's register space,
 * to avoid reading back brightness values from chip,
 * which is known to hang.
 */
struct is31fl319x_chip {
	const struct is31fl319x_chipdef *cdef;
	struct i2c_client               *client;
	struct gpio_desc		*shutdown_gpio;
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
	u8 reset_reg;
	const struct regmap_config *is31fl319x_regmap_config;
	int (*brightness_set)(struct led_classdev *cdev, enum led_brightness brightness);
	u32 current_default;
	u32 current_min;
	u32 current_max;
	bool is_3196or3199;
};

static bool is31fl319x_readable_reg(struct device *dev, unsigned int reg)
{
	/* we have no readable registers */
	return false;
}

static bool is31fl3190_volatile_reg(struct device *dev, unsigned int reg)
{
	/* volatile registers are not cached */
	switch (reg) {
	case IS31FL3190_DATA_UPDATE:
	case IS31FL3190_TIME_UPDATE:
	case IS31FL3190_RESET:
		return true; /* always write-through */
	default:
		return false;
	}
}

static const struct reg_default is31fl3190_reg_defaults[] = {
	{ IS31FL3190_LEDMODE, 0x00 },
	{ IS31FL3190_CURRENT, 0x00 },
	{ IS31FL3190_PWM(0), 0x00 },
	{ IS31FL3190_PWM(1), 0x00 },
	{ IS31FL3190_PWM(2), 0x00 },
};

static struct regmap_config is31fl3190_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = IS31FL3190_RESET,
	.cache_type = REGCACHE_FLAT,
	.readable_reg = is31fl319x_readable_reg,
	.volatile_reg = is31fl3190_volatile_reg,
	.reg_defaults = is31fl3190_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(is31fl3190_reg_defaults),
};

static bool is31fl3196_volatile_reg(struct device *dev, unsigned int reg)
{
	/* volatile registers are not cached */
	switch (reg) {
	case IS31FL3196_DATA_UPDATE:
	case IS31FL3196_TIME_UPDATE:
	case IS31FL3196_RESET:
		return true; /* always write-through */
	default:
		return false;
	}
}

static const struct reg_default is31fl3196_reg_defaults[] = {
	{ IS31FL3196_CONFIG1, 0x00 },
	{ IS31FL3196_CONFIG2, 0x00 },
	{ IS31FL3196_PWM(0), 0x00 },
	{ IS31FL3196_PWM(1), 0x00 },
	{ IS31FL3196_PWM(2), 0x00 },
	{ IS31FL3196_PWM(3), 0x00 },
	{ IS31FL3196_PWM(4), 0x00 },
	{ IS31FL3196_PWM(5), 0x00 },
	{ IS31FL3196_PWM(6), 0x00 },
	{ IS31FL3196_PWM(7), 0x00 },
	{ IS31FL3196_PWM(8), 0x00 },
};

static struct regmap_config is31fl3196_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = IS31FL3196_REG_CNT,
	.cache_type = REGCACHE_FLAT,
	.readable_reg = is31fl319x_readable_reg,
	.volatile_reg = is31fl3196_volatile_reg,
	.reg_defaults = is31fl3196_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(is31fl3196_reg_defaults),
};

static int is31fl3190_brightness_set(struct led_classdev *cdev,
				     enum led_brightness brightness)
{
	struct is31fl319x_led *led = container_of(cdev, struct is31fl319x_led, cdev);
	struct is31fl319x_chip *is31 = led->chip;
	int chan = led - is31->leds;
	int ret;
	int i;
	u8 ctrl = 0;

	dev_dbg(&is31->client->dev, "channel %d: %d\n", chan, brightness);

	mutex_lock(&is31->lock);

	/* update PWM register */
	ret = regmap_write(is31->regmap, IS31FL3190_PWM(chan), brightness);
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

		ret = regmap_read(is31->regmap, IS31FL3190_PWM(i), &pwm_value);
		on = ret >= 0 && pwm_value > LED_OFF;

		ctrl |= on << i;
	}

	if (ctrl > 0) {
		dev_dbg(&is31->client->dev, "power up %02x\n", ctrl);
		regmap_write(is31->regmap, IS31FL3190_LEDCONTROL, ctrl);
		/* update PWMs */
		regmap_write(is31->regmap, IS31FL3190_DATA_UPDATE, 0x00);
		/* enable chip from shut down and enable all channels */
		ret = regmap_write(is31->regmap, IS31FL319X_SHUTDOWN, 0x20);
	} else {
		dev_dbg(&is31->client->dev, "power down\n");
		/* shut down (no need to clear LEDCONTROL) */
		ret = regmap_write(is31->regmap, IS31FL319X_SHUTDOWN, 0x01);
	}

out:
	mutex_unlock(&is31->lock);

	return ret;
}

static int is31fl3196_brightness_set(struct led_classdev *cdev,
				     enum led_brightness brightness)
{
	struct is31fl319x_led *led = container_of(cdev, struct is31fl319x_led, cdev);
	struct is31fl319x_chip *is31 = led->chip;
	int chan = led - is31->leds;
	int ret;
	int i;
	u8 ctrl1 = 0, ctrl2 = 0;

	dev_dbg(&is31->client->dev, "channel %d: %d\n", chan, brightness);

	mutex_lock(&is31->lock);

	/* update PWM register */
	ret = regmap_write(is31->regmap, IS31FL3196_PWM(chan), brightness);
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

		ret = regmap_read(is31->regmap, IS31FL3196_PWM(i), &pwm_value);
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
		regmap_write(is31->regmap, IS31FL3196_CTRL1, ctrl1);
		regmap_write(is31->regmap, IS31FL3196_CTRL2, ctrl2);
		/* update PWMs */
		regmap_write(is31->regmap, IS31FL3196_DATA_UPDATE, 0x00);
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

static const struct is31fl319x_chipdef is31fl3190_cdef = {
	.num_leds = 1,
	.reset_reg = IS31FL3190_RESET,
	.is31fl319x_regmap_config = &is31fl3190_regmap_config,
	.brightness_set = is31fl3190_brightness_set,
	.current_default = IS31FL3190_CURRENT_uA_DEFAULT,
	.current_min = IS31FL3190_CURRENT_uA_MIN,
	.current_max = IS31FL3190_CURRENT_uA_MAX,
	.is_3196or3199 = false,
};

static const struct is31fl319x_chipdef is31fl3193_cdef = {
	.num_leds = 3,
	.reset_reg = IS31FL3190_RESET,
	.is31fl319x_regmap_config = &is31fl3190_regmap_config,
	.brightness_set = is31fl3190_brightness_set,
	.current_default = IS31FL3190_CURRENT_uA_DEFAULT,
	.current_min = IS31FL3190_CURRENT_uA_MIN,
	.current_max = IS31FL3190_CURRENT_uA_MAX,
	.is_3196or3199 = false,
};

static const struct is31fl319x_chipdef is31fl3196_cdef = {
	.num_leds = 6,
	.reset_reg = IS31FL3196_RESET,
	.is31fl319x_regmap_config = &is31fl3196_regmap_config,
	.brightness_set = is31fl3196_brightness_set,
	.current_default = IS31FL3196_CURRENT_uA_DEFAULT,
	.current_min = IS31FL3196_CURRENT_uA_MIN,
	.current_max = IS31FL3196_CURRENT_uA_MAX,
	.is_3196or3199 = true,
};

static const struct is31fl319x_chipdef is31fl3199_cdef = {
	.num_leds = 9,
	.reset_reg = IS31FL3196_RESET,
	.is31fl319x_regmap_config = &is31fl3196_regmap_config,
	.brightness_set = is31fl3196_brightness_set,
	.current_default = IS31FL3196_CURRENT_uA_DEFAULT,
	.current_min = IS31FL3196_CURRENT_uA_MIN,
	.current_max = IS31FL3196_CURRENT_uA_MAX,
	.is_3196or3199 = true,
};

static const struct of_device_id of_is31fl319x_match[] = {
	{ .compatible = "issi,is31fl3190", .data = &is31fl3190_cdef, },
	{ .compatible = "issi,is31fl3191", .data = &is31fl3190_cdef, },
	{ .compatible = "issi,is31fl3193", .data = &is31fl3193_cdef, },
	{ .compatible = "issi,is31fl3196", .data = &is31fl3196_cdef, },
	{ .compatible = "issi,is31fl3199", .data = &is31fl3199_cdef, },
	{ .compatible = "si-en,sn3190",    .data = &is31fl3190_cdef, },
	{ .compatible = "si-en,sn3191",    .data = &is31fl3190_cdef, },
	{ .compatible = "si-en,sn3193",    .data = &is31fl3193_cdef, },
	{ .compatible = "si-en,sn3196",    .data = &is31fl3196_cdef, },
	{ .compatible = "si-en,sn3199",    .data = &is31fl3199_cdef, },
	{ }
};
MODULE_DEVICE_TABLE(of, of_is31fl319x_match);

static int is31fl319x_parse_child_fw(const struct device *dev,
				     const struct fwnode_handle *child,
				     struct is31fl319x_led *led,
				     struct is31fl319x_chip *is31)
{
	struct led_classdev *cdev = &led->cdev;
	int ret;

	if (fwnode_property_read_string(child, "label", &cdev->name))
		cdev->name = fwnode_get_name(child);

	ret = fwnode_property_read_string(child, "linux,default-trigger", &cdev->default_trigger);
	if (ret < 0 && ret != -EINVAL) /* is optional */
		return ret;

	led->max_microamp = is31->cdef->current_default;
	ret = fwnode_property_read_u32(child, "led-max-microamp", &led->max_microamp);
	if (!ret) {
		if (led->max_microamp < is31->cdef->current_min)
			return -EINVAL;	/* not supported */
		led->max_microamp = min(led->max_microamp,
					is31->cdef->current_max);
	}

	return 0;
}

static int is31fl319x_parse_fw(struct device *dev, struct is31fl319x_chip *is31)
{
	struct fwnode_handle *fwnode = dev_fwnode(dev), *child;
	int count;
	int ret;

	is31->shutdown_gpio = devm_gpiod_get_optional(dev, "shutdown", GPIOD_OUT_HIGH);
	if (IS_ERR(is31->shutdown_gpio))
		return dev_err_probe(dev, PTR_ERR(is31->shutdown_gpio),
				     "Failed to get shutdown gpio\n");

	is31->cdef = device_get_match_data(dev);

	count = 0;
	fwnode_for_each_available_child_node(fwnode, child)
		count++;

	dev_dbg(dev, "probing with %d leds defined in DT\n", count);

	if (!count || count > is31->cdef->num_leds)
		return dev_err_probe(dev, -ENODEV,
				     "Number of leds defined must be between 1 and %u\n",
				     is31->cdef->num_leds);

	fwnode_for_each_available_child_node(fwnode, child) {
		struct is31fl319x_led *led;
		u32 reg;

		ret = fwnode_property_read_u32(child, "reg", &reg);
		if (ret) {
			ret = dev_err_probe(dev, ret, "Failed to read led 'reg' property\n");
			goto put_child_node;
		}

		if (reg < 1 || reg > is31->cdef->num_leds) {
			ret = dev_err_probe(dev, -EINVAL, "invalid led reg %u\n", reg);
			goto put_child_node;
		}

		led = &is31->leds[reg - 1];

		if (led->configured) {
			ret = dev_err_probe(dev, -EINVAL, "led %u is already configured\n", reg);
			goto put_child_node;
		}

		ret = is31fl319x_parse_child_fw(dev, child, led, is31);
		if (ret) {
			ret = dev_err_probe(dev, ret, "led %u DT parsing failed\n", reg);
			goto put_child_node;
		}

		led->configured = true;
	}

	is31->audio_gain_db = 0;
	if (is31->cdef->is_3196or3199) {
		ret = fwnode_property_read_u32(fwnode, "audio-gain-db", &is31->audio_gain_db);
		if (!ret)
			is31->audio_gain_db = min(is31->audio_gain_db,
						  IS31FL3196_AUDIO_GAIN_DB_MAX);
	}

	return 0;

put_child_node:
	fwnode_handle_put(child);
	return ret;
}

static inline int is31fl3190_microamp_to_cs(struct device *dev, u32 microamp)
{
	switch (microamp) {
	case 5000:
		return IS31FL3190_CURRENT_5_mA;
	case 10000:
		return IS31FL3190_CURRENT_10_mA;
	case 17500:
		return IS31FL3190_CURRENT_17dot5_mA;
	case 30000:
		return IS31FL3190_CURRENT_30_mA;
	case 42000:
		return IS31FL3190_CURRENT_42_mA;
	default:
		dev_warn(dev, "Unsupported current value: %d, using 5000 µA!\n", microamp);
		return IS31FL3190_CURRENT_5_mA;
	}
}

static inline int is31fl3196_microamp_to_cs(struct device *dev, u32 microamp)
{
	/* round down to nearest supported value (range check done by caller) */
	u32 step = microamp / IS31FL3196_CURRENT_uA_STEP;

	return ((IS31FL3196_CONFIG2_CS_STEP_REF - step) &
		IS31FL3196_CONFIG2_CS_MASK) <<
		IS31FL3196_CONFIG2_CS_SHIFT; /* CS encoding */
}

static inline int is31fl3196_db_to_gain(u32 dezibel)
{
	/* round down to nearest supported value (range check done by caller) */
	return dezibel / IS31FL3196_AUDIO_GAIN_DB_STEP;
}

static int is31fl319x_probe(struct i2c_client *client)
{
	struct is31fl319x_chip *is31;
	struct device *dev = &client->dev;
	int err;
	int i = 0;
	u32 aggregated_led_microamp;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -EIO;

	is31 = devm_kzalloc(&client->dev, sizeof(*is31), GFP_KERNEL);
	if (!is31)
		return -ENOMEM;

	mutex_init(&is31->lock);
	err = devm_add_action(dev, (void (*)(void *))mutex_destroy, &is31->lock);
	if (err)
		return err;

	err = is31fl319x_parse_fw(&client->dev, is31);
	if (err)
		return err;

	if (is31->shutdown_gpio) {
		gpiod_direction_output(is31->shutdown_gpio, 0);
		mdelay(5);
		gpiod_direction_output(is31->shutdown_gpio, 1);
	}

	is31->client = client;
	is31->regmap = devm_regmap_init_i2c(client, is31->cdef->is31fl319x_regmap_config);
	if (IS_ERR(is31->regmap))
		return dev_err_probe(dev, PTR_ERR(is31->regmap), "failed to allocate register map\n");

	i2c_set_clientdata(client, is31);

	/* check for write-reply from chip (we can't read any registers) */
	err = regmap_write(is31->regmap, is31->cdef->reset_reg, 0x00);
	if (err < 0)
		return dev_err_probe(dev, err, "no response from chip write\n");

	/*
	 * Kernel conventions require per-LED led-max-microamp property.
	 * But the chip does not allow to limit individual LEDs.
	 * So we take minimum from all subnodes for safety of hardware.
	 */
	aggregated_led_microamp = is31->cdef->current_max;
	for (i = 0; i < is31->cdef->num_leds; i++)
		if (is31->leds[i].configured &&
		    is31->leds[i].max_microamp < aggregated_led_microamp)
			aggregated_led_microamp = is31->leds[i].max_microamp;

	if (is31->cdef->is_3196or3199)
		regmap_write(is31->regmap, IS31FL3196_CONFIG2,
			     is31fl3196_microamp_to_cs(dev, aggregated_led_microamp) |
			     is31fl3196_db_to_gain(is31->audio_gain_db));
	else
		regmap_update_bits(is31->regmap, IS31FL3190_CURRENT, IS31FL3190_CURRENT_MASK,
				   is31fl3190_microamp_to_cs(dev, aggregated_led_microamp));

	for (i = 0; i < is31->cdef->num_leds; i++) {
		struct is31fl319x_led *led = &is31->leds[i];

		if (!led->configured)
			continue;

		led->chip = is31;
		led->cdev.brightness_set_blocking = is31->cdef->brightness_set;

		err = devm_led_classdev_register(&client->dev, &led->cdev);
		if (err < 0)
			return err;
	}

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
	{ "sn3190" },
	{ "sn3191" },
	{ "sn3193" },
	{ "sn3196" },
	{ "sn3199" },
	{},
};
MODULE_DEVICE_TABLE(i2c, is31fl319x_id);

static struct i2c_driver is31fl319x_driver = {
	.driver   = {
		.name           = "leds-is31fl319x",
		.of_match_table = of_is31fl319x_match,
	},
	.probe_new = is31fl319x_probe,
	.id_table = is31fl319x_id,
};

module_i2c_driver(is31fl319x_driver);

MODULE_AUTHOR("H. Nikolaus Schaller <hns@goldelico.com>");
MODULE_AUTHOR("Andrey Utkin <andrey_utkin@fastmail.com>");
MODULE_DESCRIPTION("IS31FL319X LED driver");
MODULE_LICENSE("GPL v2");
