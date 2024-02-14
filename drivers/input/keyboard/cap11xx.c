// SPDX-License-Identifier: GPL-2.0-only
/*
 * Input driver for Microchip CAP11xx based capacitive touch sensors
 *
 * (c) 2014 Daniel Mack <linux@zonque.org>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/leds.h>
#include <linux/of_irq.h>
#include <linux/regmap.h>
#include <linux/i2c.h>
#include <linux/gpio/consumer.h>

#define CAP11XX_REG_MAIN_CONTROL	0x00
#define CAP11XX_REG_MAIN_CONTROL_GAIN_SHIFT	(6)
#define CAP11XX_REG_MAIN_CONTROL_GAIN_MASK	(0xc0)
#define CAP11XX_REG_MAIN_CONTROL_DLSEEP		BIT(4)
#define CAP11XX_REG_GENERAL_STATUS	0x02
#define CAP11XX_REG_SENSOR_INPUT	0x03
#define CAP11XX_REG_NOISE_FLAG_STATUS	0x0a
#define CAP11XX_REG_SENOR_DELTA(X)	(0x10 + (X))
#define CAP11XX_REG_SENSITIVITY_CONTROL	0x1f
#define CAP11XX_REG_CONFIG		0x20
#define CAP11XX_REG_SENSOR_ENABLE	0x21
#define CAP11XX_REG_SENSOR_CONFIG	0x22
#define CAP11XX_REG_SENSOR_CONFIG2	0x23
#define CAP11XX_REG_SAMPLING_CONFIG	0x24
#define CAP11XX_REG_CALIBRATION		0x26
#define CAP11XX_REG_INT_ENABLE		0x27
#define CAP11XX_REG_REPEAT_RATE		0x28
#define CAP11XX_REG_MT_CONFIG		0x2a
#define CAP11XX_REG_MT_PATTERN_CONFIG	0x2b
#define CAP11XX_REG_MT_PATTERN		0x2d
#define CAP11XX_REG_RECALIB_CONFIG	0x2f
#define CAP11XX_REG_SENSOR_THRESH(X)	(0x30 + (X))
#define CAP11XX_REG_SENSOR_NOISE_THRESH	0x38
#define CAP11XX_REG_STANDBY_CHANNEL	0x40
#define CAP11XX_REG_STANDBY_CONFIG	0x41
#define CAP11XX_REG_STANDBY_SENSITIVITY	0x42
#define CAP11XX_REG_STANDBY_THRESH	0x43
#define CAP11XX_REG_CONFIG2		0x44
#define CAP11XX_REG_CONFIG2_ALT_POL	BIT(6)
#define CAP11XX_REG_SENSOR_BASE_CNT(X)	(0x50 + (X))
#define CAP11XX_REG_LED_POLARITY	0x73
#define CAP11XX_REG_LED_OUTPUT_CONTROL	0x74

#define CAP11XX_REG_LED_DUTY_CYCLE_1	0x90
#define CAP11XX_REG_LED_DUTY_CYCLE_2	0x91
#define CAP11XX_REG_LED_DUTY_CYCLE_3	0x92
#define CAP11XX_REG_LED_DUTY_CYCLE_4	0x93

#define CAP11XX_REG_LED_DUTY_MIN_MASK	(0x0f)
#define CAP11XX_REG_LED_DUTY_MIN_MASK_SHIFT	(0)
#define CAP11XX_REG_LED_DUTY_MAX_MASK	(0xf0)
#define CAP11XX_REG_LED_DUTY_MAX_MASK_SHIFT	(4)
#define CAP11XX_REG_LED_DUTY_MAX_VALUE	(15)

#define CAP11XX_REG_SENSOR_CALIB	(0xb1 + (X))
#define CAP11XX_REG_SENSOR_CALIB_LSB1	0xb9
#define CAP11XX_REG_SENSOR_CALIB_LSB2	0xba
#define CAP11XX_REG_PRODUCT_ID		0xfd
#define CAP11XX_REG_MANUFACTURER_ID	0xfe
#define CAP11XX_REG_REVISION		0xff

#define CAP11XX_MANUFACTURER_ID	0x5d

#ifdef CONFIG_LEDS_CLASS
struct cap11xx_led {
	struct cap11xx_priv *priv;
	struct led_classdev cdev;
	u32 reg;
};
#endif

struct cap11xx_priv {
	struct regmap *regmap;
	struct input_dev *idev;

	struct cap11xx_led *leds;
	int num_leds;

	/* config */
	u32 keycodes[];
};

struct cap11xx_hw_model {
	u8 product_id;
	unsigned int num_channels;
	unsigned int num_leds;
	bool no_gain;
};

enum {
	CAP1106,
	CAP1126,
	CAP1188,
	CAP1206,
};

static const struct cap11xx_hw_model cap11xx_devices[] = {
	[CAP1106] = { .product_id = 0x55, .num_channels = 6, .num_leds = 0, .no_gain = false },
	[CAP1126] = { .product_id = 0x53, .num_channels = 6, .num_leds = 2, .no_gain = false },
	[CAP1188] = { .product_id = 0x50, .num_channels = 8, .num_leds = 8, .no_gain = false },
	[CAP1206] = { .product_id = 0x67, .num_channels = 6, .num_leds = 0, .no_gain = true },
};

static const struct reg_default cap11xx_reg_defaults[] = {
	{ CAP11XX_REG_MAIN_CONTROL,		0x00 },
	{ CAP11XX_REG_GENERAL_STATUS,		0x00 },
	{ CAP11XX_REG_SENSOR_INPUT,		0x00 },
	{ CAP11XX_REG_NOISE_FLAG_STATUS,	0x00 },
	{ CAP11XX_REG_SENSITIVITY_CONTROL,	0x2f },
	{ CAP11XX_REG_CONFIG,			0x20 },
	{ CAP11XX_REG_SENSOR_ENABLE,		0x3f },
	{ CAP11XX_REG_SENSOR_CONFIG,		0xa4 },
	{ CAP11XX_REG_SENSOR_CONFIG2,		0x07 },
	{ CAP11XX_REG_SAMPLING_CONFIG,		0x39 },
	{ CAP11XX_REG_CALIBRATION,		0x00 },
	{ CAP11XX_REG_INT_ENABLE,		0x3f },
	{ CAP11XX_REG_REPEAT_RATE,		0x3f },
	{ CAP11XX_REG_MT_CONFIG,		0x80 },
	{ CAP11XX_REG_MT_PATTERN_CONFIG,	0x00 },
	{ CAP11XX_REG_MT_PATTERN,		0x3f },
	{ CAP11XX_REG_RECALIB_CONFIG,		0x8a },
	{ CAP11XX_REG_SENSOR_THRESH(0),		0x40 },
	{ CAP11XX_REG_SENSOR_THRESH(1),		0x40 },
	{ CAP11XX_REG_SENSOR_THRESH(2),		0x40 },
	{ CAP11XX_REG_SENSOR_THRESH(3),		0x40 },
	{ CAP11XX_REG_SENSOR_THRESH(4),		0x40 },
	{ CAP11XX_REG_SENSOR_THRESH(5),		0x40 },
	{ CAP11XX_REG_SENSOR_NOISE_THRESH,	0x01 },
	{ CAP11XX_REG_STANDBY_CHANNEL,		0x00 },
	{ CAP11XX_REG_STANDBY_CONFIG,		0x39 },
	{ CAP11XX_REG_STANDBY_SENSITIVITY,	0x02 },
	{ CAP11XX_REG_STANDBY_THRESH,		0x40 },
	{ CAP11XX_REG_CONFIG2,			0x40 },
	{ CAP11XX_REG_LED_POLARITY,		0x00 },
	{ CAP11XX_REG_SENSOR_CALIB_LSB1,	0x00 },
	{ CAP11XX_REG_SENSOR_CALIB_LSB2,	0x00 },
};

static bool cap11xx_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CAP11XX_REG_MAIN_CONTROL:
	case CAP11XX_REG_SENSOR_INPUT:
	case CAP11XX_REG_SENOR_DELTA(0):
	case CAP11XX_REG_SENOR_DELTA(1):
	case CAP11XX_REG_SENOR_DELTA(2):
	case CAP11XX_REG_SENOR_DELTA(3):
	case CAP11XX_REG_SENOR_DELTA(4):
	case CAP11XX_REG_SENOR_DELTA(5):
	case CAP11XX_REG_PRODUCT_ID:
	case CAP11XX_REG_MANUFACTURER_ID:
	case CAP11XX_REG_REVISION:
		return true;
	}

	return false;
}

static const struct regmap_config cap11xx_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = CAP11XX_REG_REVISION,
	.reg_defaults = cap11xx_reg_defaults,

	.num_reg_defaults = ARRAY_SIZE(cap11xx_reg_defaults),
	.cache_type = REGCACHE_RBTREE,
	.volatile_reg = cap11xx_volatile_reg,
};

static irqreturn_t cap11xx_thread_func(int irq_num, void *data)
{
	struct cap11xx_priv *priv = data;
	unsigned int status;
	int ret, i;

	/*
	 * Deassert interrupt. This needs to be done before reading the status
	 * registers, which will not carry valid values otherwise.
	 */
	ret = regmap_update_bits(priv->regmap, CAP11XX_REG_MAIN_CONTROL, 1, 0);
	if (ret < 0)
		goto out;

	ret = regmap_read(priv->regmap, CAP11XX_REG_SENSOR_INPUT, &status);
	if (ret < 0)
		goto out;

	for (i = 0; i < priv->idev->keycodemax; i++)
		input_report_key(priv->idev, priv->keycodes[i],
				 status & (1 << i));

	input_sync(priv->idev);

out:
	return IRQ_HANDLED;
}

static int cap11xx_set_sleep(struct cap11xx_priv *priv, bool sleep)
{
	/*
	 * DLSEEP mode will turn off all LEDS, prevent this
	 */
	if (IS_ENABLED(CONFIG_LEDS_CLASS) && priv->num_leds)
		return 0;

	return regmap_update_bits(priv->regmap, CAP11XX_REG_MAIN_CONTROL,
				  CAP11XX_REG_MAIN_CONTROL_DLSEEP,
				  sleep ? CAP11XX_REG_MAIN_CONTROL_DLSEEP : 0);
}

static int cap11xx_input_open(struct input_dev *idev)
{
	struct cap11xx_priv *priv = input_get_drvdata(idev);

	return cap11xx_set_sleep(priv, false);
}

static void cap11xx_input_close(struct input_dev *idev)
{
	struct cap11xx_priv *priv = input_get_drvdata(idev);

	cap11xx_set_sleep(priv, true);
}

#ifdef CONFIG_LEDS_CLASS
static int cap11xx_led_set(struct led_classdev *cdev,
			    enum led_brightness value)
{
	struct cap11xx_led *led = container_of(cdev, struct cap11xx_led, cdev);
	struct cap11xx_priv *priv = led->priv;

	/*
	 * All LEDs share the same duty cycle as this is a HW
	 * limitation. Brightness levels per LED are either
	 * 0 (OFF) and 1 (ON).
	 */
	return regmap_update_bits(priv->regmap,
				  CAP11XX_REG_LED_OUTPUT_CONTROL,
				  BIT(led->reg),
				  value ? BIT(led->reg) : 0);
}

static int cap11xx_init_leds(struct device *dev,
			     struct cap11xx_priv *priv, int num_leds)
{
	struct device_node *node = dev->of_node, *child;
	struct cap11xx_led *led;
	int cnt = of_get_child_count(node);
	int error;

	if (!num_leds || !cnt)
		return 0;

	if (cnt > num_leds)
		return -EINVAL;

	led = devm_kcalloc(dev, cnt, sizeof(struct cap11xx_led), GFP_KERNEL);
	if (!led)
		return -ENOMEM;

	priv->leds = led;

	error = regmap_update_bits(priv->regmap,
				CAP11XX_REG_LED_OUTPUT_CONTROL, 0xff, 0);
	if (error)
		return error;

	error = regmap_update_bits(priv->regmap, CAP11XX_REG_LED_DUTY_CYCLE_4,
				CAP11XX_REG_LED_DUTY_MAX_MASK,
				CAP11XX_REG_LED_DUTY_MAX_VALUE <<
				CAP11XX_REG_LED_DUTY_MAX_MASK_SHIFT);
	if (error)
		return error;

	for_each_child_of_node(node, child) {
		u32 reg;

		led->cdev.name =
			of_get_property(child, "label", NULL) ? : child->name;
		led->cdev.default_trigger =
			of_get_property(child, "linux,default-trigger", NULL);
		led->cdev.flags = 0;
		led->cdev.brightness_set_blocking = cap11xx_led_set;
		led->cdev.max_brightness = 1;
		led->cdev.brightness = LED_OFF;

		error = of_property_read_u32(child, "reg", &reg);
		if (error != 0 || reg >= num_leds) {
			of_node_put(child);
			return -EINVAL;
		}

		led->reg = reg;
		led->priv = priv;

		error = devm_led_classdev_register(dev, &led->cdev);
		if (error) {
			of_node_put(child);
			return error;
		}

		priv->num_leds++;
		led++;
	}

	return 0;
}
#else
static int cap11xx_init_leds(struct device *dev,
			     struct cap11xx_priv *priv, int num_leds)
{
	return 0;
}
#endif

static int cap11xx_i2c_probe(struct i2c_client *i2c_client,
			     const struct i2c_device_id *id)
{
	struct device *dev = &i2c_client->dev;
	struct cap11xx_priv *priv;
	struct device_node *node;
	const struct cap11xx_hw_model *cap;
	int i, error, irq, gain = 0;
	unsigned int val, rev;
	u32 gain32;

	if (id->driver_data >= ARRAY_SIZE(cap11xx_devices)) {
		dev_err(dev, "Invalid device ID %lu\n", id->driver_data);
		return -EINVAL;
	}

	cap = &cap11xx_devices[id->driver_data];
	if (!cap || !cap->num_channels) {
		dev_err(dev, "Invalid device configuration\n");
		return -EINVAL;
	}

	priv = devm_kzalloc(dev,
			    struct_size(priv, keycodes, cap->num_channels),
			    GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->regmap = devm_regmap_init_i2c(i2c_client, &cap11xx_regmap_config);
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	error = regmap_read(priv->regmap, CAP11XX_REG_PRODUCT_ID, &val);
	if (error)
		return error;

	if (val != cap->product_id) {
		dev_err(dev, "Product ID: Got 0x%02x, expected 0x%02x\n",
			val, cap->product_id);
		return -ENXIO;
	}

	error = regmap_read(priv->regmap, CAP11XX_REG_MANUFACTURER_ID, &val);
	if (error)
		return error;

	if (val != CAP11XX_MANUFACTURER_ID) {
		dev_err(dev, "Manufacturer ID: Got 0x%02x, expected 0x%02x\n",
			val, CAP11XX_MANUFACTURER_ID);
		return -ENXIO;
	}

	error = regmap_read(priv->regmap, CAP11XX_REG_REVISION, &rev);
	if (error < 0)
		return error;

	dev_info(dev, "CAP11XX detected, revision 0x%02x\n", rev);
	node = dev->of_node;

	if (!of_property_read_u32(node, "microchip,sensor-gain", &gain32)) {
		if (cap->no_gain)
			dev_warn(dev,
				 "This version doesn't support sensor gain\n");
		else if (is_power_of_2(gain32) && gain32 <= 8)
			gain = ilog2(gain32);
		else
			dev_err(dev, "Invalid sensor-gain value %d\n", gain32);
	}

	if (id->driver_data != CAP1206) {
		if (of_property_read_bool(node, "microchip,irq-active-high")) {
			error = regmap_update_bits(priv->regmap,
						   CAP11XX_REG_CONFIG2,
						   CAP11XX_REG_CONFIG2_ALT_POL,
						   0);
			if (error)
				return error;
		}
	}

	/* Provide some useful defaults */
	for (i = 0; i < cap->num_channels; i++)
		priv->keycodes[i] = KEY_A + i;

	of_property_read_u32_array(node, "linux,keycodes",
				   priv->keycodes, cap->num_channels);

	if (!cap->no_gain) {
		error = regmap_update_bits(priv->regmap,
				CAP11XX_REG_MAIN_CONTROL,
				CAP11XX_REG_MAIN_CONTROL_GAIN_MASK,
				gain << CAP11XX_REG_MAIN_CONTROL_GAIN_SHIFT);
		if (error)
			return error;
	}

	/* Disable autorepeat. The Linux input system has its own handling. */
	error = regmap_write(priv->regmap, CAP11XX_REG_REPEAT_RATE, 0);
	if (error)
		return error;

	priv->idev = devm_input_allocate_device(dev);
	if (!priv->idev)
		return -ENOMEM;

	priv->idev->name = "CAP11XX capacitive touch sensor";
	priv->idev->id.bustype = BUS_I2C;
	priv->idev->evbit[0] = BIT_MASK(EV_KEY);

	if (of_property_read_bool(node, "autorepeat"))
		__set_bit(EV_REP, priv->idev->evbit);

	for (i = 0; i < cap->num_channels; i++)
		__set_bit(priv->keycodes[i], priv->idev->keybit);

	__clear_bit(KEY_RESERVED, priv->idev->keybit);

	priv->idev->keycode = priv->keycodes;
	priv->idev->keycodesize = sizeof(priv->keycodes[0]);
	priv->idev->keycodemax = cap->num_channels;

	priv->idev->id.vendor = CAP11XX_MANUFACTURER_ID;
	priv->idev->id.product = cap->product_id;
	priv->idev->id.version = rev;

	priv->idev->open = cap11xx_input_open;
	priv->idev->close = cap11xx_input_close;

	error = cap11xx_init_leds(dev, priv, cap->num_leds);
	if (error)
		return error;

	input_set_drvdata(priv->idev, priv);

	/*
	 * Put the device in deep sleep mode for now.
	 * ->open() will bring it back once the it is actually needed.
	 */
	cap11xx_set_sleep(priv, true);

	error = input_register_device(priv->idev);
	if (error)
		return error;

	irq = irq_of_parse_and_map(node, 0);
	if (!irq) {
		dev_err(dev, "Unable to parse or map IRQ\n");
		return -ENXIO;
	}

	error = devm_request_threaded_irq(dev, irq, NULL, cap11xx_thread_func,
					  IRQF_ONESHOT, dev_name(dev), priv);
	if (error)
		return error;

	return 0;
}

static const struct of_device_id cap11xx_dt_ids[] = {
	{ .compatible = "microchip,cap1106", },
	{ .compatible = "microchip,cap1126", },
	{ .compatible = "microchip,cap1188", },
	{ .compatible = "microchip,cap1206", },
	{}
};
MODULE_DEVICE_TABLE(of, cap11xx_dt_ids);

static const struct i2c_device_id cap11xx_i2c_ids[] = {
	{ "cap1106", CAP1106 },
	{ "cap1126", CAP1126 },
	{ "cap1188", CAP1188 },
	{ "cap1206", CAP1206 },
	{}
};
MODULE_DEVICE_TABLE(i2c, cap11xx_i2c_ids);

static struct i2c_driver cap11xx_i2c_driver = {
	.driver = {
		.name	= "cap11xx",
		.of_match_table = cap11xx_dt_ids,
	},
	.id_table	= cap11xx_i2c_ids,
	.probe		= cap11xx_i2c_probe,
};

module_i2c_driver(cap11xx_i2c_driver);

MODULE_DESCRIPTION("Microchip CAP11XX driver");
MODULE_AUTHOR("Daniel Mack <linux@zonque.org>");
MODULE_LICENSE("GPL v2");
