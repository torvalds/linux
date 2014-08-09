/*
 * Input driver for Microchip CAP1106, 6 channel capacitive touch sensor
 *
 * http://www.microchip.com/wwwproducts/Devices.aspx?product=CAP1106
 *
 * (c) 2014 Daniel Mack <linux@zonque.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/of_irq.h>
#include <linux/regmap.h>
#include <linux/i2c.h>
#include <linux/gpio/consumer.h>

#define CAP1106_REG_MAIN_CONTROL	0x00
#define CAP1106_REG_MAIN_CONTROL_GAIN_SHIFT	(6)
#define CAP1106_REG_MAIN_CONTROL_GAIN_MASK	(0xc0)
#define CAP1106_REG_MAIN_CONTROL_DLSEEP		BIT(4)
#define CAP1106_REG_GENERAL_STATUS	0x02
#define CAP1106_REG_SENSOR_INPUT	0x03
#define CAP1106_REG_NOISE_FLAG_STATUS	0x0a
#define CAP1106_REG_SENOR_DELTA(X)	(0x10 + (X))
#define CAP1106_REG_SENSITIVITY_CONTROL	0x1f
#define CAP1106_REG_CONFIG		0x20
#define CAP1106_REG_SENSOR_ENABLE	0x21
#define CAP1106_REG_SENSOR_CONFIG	0x22
#define CAP1106_REG_SENSOR_CONFIG2	0x23
#define CAP1106_REG_SAMPLING_CONFIG	0x24
#define CAP1106_REG_CALIBRATION		0x25
#define CAP1106_REG_INT_ENABLE		0x26
#define CAP1106_REG_REPEAT_RATE		0x28
#define CAP1106_REG_MT_CONFIG		0x2a
#define CAP1106_REG_MT_PATTERN_CONFIG	0x2b
#define CAP1106_REG_MT_PATTERN		0x2d
#define CAP1106_REG_RECALIB_CONFIG	0x2f
#define CAP1106_REG_SENSOR_THRESH(X)	(0x30 + (X))
#define CAP1106_REG_SENSOR_NOISE_THRESH	0x38
#define CAP1106_REG_STANDBY_CHANNEL	0x40
#define CAP1106_REG_STANDBY_CONFIG	0x41
#define CAP1106_REG_STANDBY_SENSITIVITY	0x42
#define CAP1106_REG_STANDBY_THRESH	0x43
#define CAP1106_REG_CONFIG2		0x44
#define CAP1106_REG_SENSOR_BASE_CNT(X)	(0x50 + (X))
#define CAP1106_REG_SENSOR_CALIB	(0xb1 + (X))
#define CAP1106_REG_SENSOR_CALIB_LSB1	0xb9
#define CAP1106_REG_SENSOR_CALIB_LSB2	0xba
#define CAP1106_REG_PRODUCT_ID		0xfd
#define CAP1106_REG_MANUFACTURER_ID	0xfe
#define CAP1106_REG_REVISION		0xff

#define CAP1106_NUM_CHN 6
#define CAP1106_PRODUCT_ID	0x55
#define CAP1106_MANUFACTURER_ID	0x5d

struct cap1106_priv {
	struct regmap *regmap;
	struct input_dev *idev;

	/* config */
	unsigned int keycodes[CAP1106_NUM_CHN];
};

static const struct reg_default cap1106_reg_defaults[] = {
	{ CAP1106_REG_MAIN_CONTROL,		0x00 },
	{ CAP1106_REG_GENERAL_STATUS,		0x00 },
	{ CAP1106_REG_SENSOR_INPUT,		0x00 },
	{ CAP1106_REG_NOISE_FLAG_STATUS,	0x00 },
	{ CAP1106_REG_SENSITIVITY_CONTROL,	0x2f },
	{ CAP1106_REG_CONFIG,			0x20 },
	{ CAP1106_REG_SENSOR_ENABLE,		0x3f },
	{ CAP1106_REG_SENSOR_CONFIG,		0xa4 },
	{ CAP1106_REG_SENSOR_CONFIG2,		0x07 },
	{ CAP1106_REG_SAMPLING_CONFIG,		0x39 },
	{ CAP1106_REG_CALIBRATION,		0x00 },
	{ CAP1106_REG_INT_ENABLE,		0x3f },
	{ CAP1106_REG_REPEAT_RATE,		0x3f },
	{ CAP1106_REG_MT_CONFIG,		0x80 },
	{ CAP1106_REG_MT_PATTERN_CONFIG,	0x00 },
	{ CAP1106_REG_MT_PATTERN,		0x3f },
	{ CAP1106_REG_RECALIB_CONFIG,		0x8a },
	{ CAP1106_REG_SENSOR_THRESH(0),		0x40 },
	{ CAP1106_REG_SENSOR_THRESH(1),		0x40 },
	{ CAP1106_REG_SENSOR_THRESH(2),		0x40 },
	{ CAP1106_REG_SENSOR_THRESH(3),		0x40 },
	{ CAP1106_REG_SENSOR_THRESH(4),		0x40 },
	{ CAP1106_REG_SENSOR_THRESH(5),		0x40 },
	{ CAP1106_REG_SENSOR_NOISE_THRESH,	0x01 },
	{ CAP1106_REG_STANDBY_CHANNEL,		0x00 },
	{ CAP1106_REG_STANDBY_CONFIG,		0x39 },
	{ CAP1106_REG_STANDBY_SENSITIVITY,	0x02 },
	{ CAP1106_REG_STANDBY_THRESH,		0x40 },
	{ CAP1106_REG_CONFIG2,			0x40 },
	{ CAP1106_REG_SENSOR_CALIB_LSB1,	0x00 },
	{ CAP1106_REG_SENSOR_CALIB_LSB2,	0x00 },
};

static bool cap1106_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CAP1106_REG_MAIN_CONTROL:
	case CAP1106_REG_SENSOR_INPUT:
	case CAP1106_REG_SENOR_DELTA(0):
	case CAP1106_REG_SENOR_DELTA(1):
	case CAP1106_REG_SENOR_DELTA(2):
	case CAP1106_REG_SENOR_DELTA(3):
	case CAP1106_REG_SENOR_DELTA(4):
	case CAP1106_REG_SENOR_DELTA(5):
	case CAP1106_REG_PRODUCT_ID:
	case CAP1106_REG_MANUFACTURER_ID:
	case CAP1106_REG_REVISION:
		return true;
	}

	return false;
}

static const struct regmap_config cap1106_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = CAP1106_REG_REVISION,
	.reg_defaults = cap1106_reg_defaults,

	.num_reg_defaults = ARRAY_SIZE(cap1106_reg_defaults),
	.cache_type = REGCACHE_RBTREE,
	.volatile_reg = cap1106_volatile_reg,
};

static irqreturn_t cap1106_thread_func(int irq_num, void *data)
{
	struct cap1106_priv *priv = data;
	unsigned int status;
	int ret, i;

	/*
	 * Deassert interrupt. This needs to be done before reading the status
	 * registers, which will not carry valid values otherwise.
	 */
	ret = regmap_update_bits(priv->regmap, CAP1106_REG_MAIN_CONTROL, 1, 0);
	if (ret < 0)
		goto out;

	ret = regmap_read(priv->regmap, CAP1106_REG_SENSOR_INPUT, &status);
	if (ret < 0)
		goto out;

	for (i = 0; i < CAP1106_NUM_CHN; i++)
		input_report_key(priv->idev, priv->keycodes[i],
				 status & (1 << i));

	input_sync(priv->idev);

out:
	return IRQ_HANDLED;
}

static int cap1106_set_sleep(struct cap1106_priv *priv, bool sleep)
{
	return regmap_update_bits(priv->regmap, CAP1106_REG_MAIN_CONTROL,
				  CAP1106_REG_MAIN_CONTROL_DLSEEP,
				  sleep ? CAP1106_REG_MAIN_CONTROL_DLSEEP : 0);
}

static int cap1106_input_open(struct input_dev *idev)
{
	struct cap1106_priv *priv = input_get_drvdata(idev);

	return cap1106_set_sleep(priv, false);
}

static void cap1106_input_close(struct input_dev *idev)
{
	struct cap1106_priv *priv = input_get_drvdata(idev);

	cap1106_set_sleep(priv, true);
}

static int cap1106_i2c_probe(struct i2c_client *i2c_client,
			     const struct i2c_device_id *id)
{
	struct device *dev = &i2c_client->dev;
	struct cap1106_priv *priv;
	struct device_node *node;
	int i, error, irq, gain = 0;
	unsigned int val, rev;
	u32 gain32, keycodes[CAP1106_NUM_CHN];

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->regmap = devm_regmap_init_i2c(i2c_client, &cap1106_regmap_config);
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	error = regmap_read(priv->regmap, CAP1106_REG_PRODUCT_ID, &val);
	if (error)
		return error;

	if (val != CAP1106_PRODUCT_ID) {
		dev_err(dev, "Product ID: Got 0x%02x, expected 0x%02x\n",
			val, CAP1106_PRODUCT_ID);
		return -ENODEV;
	}

	error = regmap_read(priv->regmap, CAP1106_REG_MANUFACTURER_ID, &val);
	if (error)
		return error;

	if (val != CAP1106_MANUFACTURER_ID) {
		dev_err(dev, "Manufacturer ID: Got 0x%02x, expected 0x%02x\n",
			val, CAP1106_MANUFACTURER_ID);
		return -ENODEV;
	}

	error = regmap_read(priv->regmap, CAP1106_REG_REVISION, &rev);
	if (error < 0)
		return error;

	dev_info(dev, "CAP1106 detected, revision 0x%02x\n", rev);
	i2c_set_clientdata(i2c_client, priv);
	node = dev->of_node;

	if (!of_property_read_u32(node, "microchip,sensor-gain", &gain32)) {
		if (is_power_of_2(gain32) && gain32 <= 8)
			gain = ilog2(gain32);
		else
			dev_err(dev, "Invalid sensor-gain value %d\n", gain32);
	}

	BUILD_BUG_ON(ARRAY_SIZE(keycodes) != ARRAY_SIZE(priv->keycodes));

	/* Provide some useful defaults */
	for (i = 0; i < ARRAY_SIZE(keycodes); i++)
		keycodes[i] = KEY_A + i;

	of_property_read_u32_array(node, "linux,keycodes",
				   keycodes, ARRAY_SIZE(keycodes));

	for (i = 0; i < ARRAY_SIZE(keycodes); i++)
		priv->keycodes[i] = keycodes[i];

	error = regmap_update_bits(priv->regmap, CAP1106_REG_MAIN_CONTROL,
				   CAP1106_REG_MAIN_CONTROL_GAIN_MASK,
				   gain << CAP1106_REG_MAIN_CONTROL_GAIN_SHIFT);
	if (error)
		return error;

	/* Disable autorepeat. The Linux input system has its own handling. */
	error = regmap_write(priv->regmap, CAP1106_REG_REPEAT_RATE, 0);
	if (error)
		return error;

	priv->idev = devm_input_allocate_device(dev);
	if (!priv->idev)
		return -ENOMEM;

	priv->idev->name = "CAP1106 capacitive touch sensor";
	priv->idev->id.bustype = BUS_I2C;
	priv->idev->evbit[0] = BIT_MASK(EV_KEY);

	if (of_property_read_bool(node, "autorepeat"))
		__set_bit(EV_REP, priv->idev->evbit);

	for (i = 0; i < CAP1106_NUM_CHN; i++)
		__set_bit(priv->keycodes[i], priv->idev->keybit);

	priv->idev->id.vendor = CAP1106_MANUFACTURER_ID;
	priv->idev->id.product = CAP1106_PRODUCT_ID;
	priv->idev->id.version = rev;

	priv->idev->open = cap1106_input_open;
	priv->idev->close = cap1106_input_close;

	input_set_drvdata(priv->idev, priv);

	/*
	 * Put the device in deep sleep mode for now.
	 * ->open() will bring it back once the it is actually needed.
	 */
	cap1106_set_sleep(priv, true);

	error = input_register_device(priv->idev);
	if (error)
		return error;

	irq = irq_of_parse_and_map(node, 0);
	if (!irq) {
		dev_err(dev, "Unable to parse or map IRQ\n");
		return -ENXIO;
	}

	error = devm_request_threaded_irq(dev, irq, NULL, cap1106_thread_func,
					  IRQF_ONESHOT, dev_name(dev), priv);
	if (error)
		return error;

	return 0;
}

static const struct of_device_id cap1106_dt_ids[] = {
	{ .compatible = "microchip,cap1106", },
	{}
};
MODULE_DEVICE_TABLE(of, cap1106_dt_ids);

static const struct i2c_device_id cap1106_i2c_ids[] = {
	{ "cap1106", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, cap1106_i2c_ids);

static struct i2c_driver cap1106_i2c_driver = {
	.driver = {
		.name	= "cap1106",
		.owner	= THIS_MODULE,
		.of_match_table = cap1106_dt_ids,
	},
	.id_table	= cap1106_i2c_ids,
	.probe		= cap1106_i2c_probe,
};

module_i2c_driver(cap1106_i2c_driver);

MODULE_ALIAS("platform:cap1106");
MODULE_DESCRIPTION("Microchip CAP1106 driver");
MODULE_AUTHOR("Daniel Mack <linux@zonque.org>");
MODULE_LICENSE("GPL v2");
