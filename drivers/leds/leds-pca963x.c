// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2011 bct electronic GmbH
 * Copyright 2013 Qtechnology/AS
 *
 * Author: Peter Meerwald <p.meerwald@bct-electronic.com>
 * Author: Ricardo Ribalda <ribalda@kernel.org>
 *
 * Based on leds-pca955x.c
 *
 * LED driver for the PCA9633 I2C LED driver (7-bit slave address 0x62)
 * LED driver for the PCA9634/5 I2C LED driver (7-bit slave address set by hw.)
 *
 * Note that hardware blinking violates the leds infrastructure driver
 * interface since the hardware only supports blinking all LEDs with the
 * same delay_on/delay_off rates.  That is, only the LEDs that are set to
 * blink will actually blink but all LEDs that are set to blink will blink
 * in identical fashion.  The delay_on/delay_off values of the last LED
 * that is set to blink will be used for all of the blinking LEDs.
 * Hardware blinking is disabled by default but can be enabled by setting
 * the 'blink_type' member in the platform_data struct to 'PCA963X_HW_BLINK'
 * or by adding the 'nxp,hw-blink' property to the DTS.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/leds.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/of.h>

/* LED select registers determine the source that drives LED outputs */
#define PCA963X_LED_OFF		0x0	/* LED driver off */
#define PCA963X_LED_ON		0x1	/* LED driver on */
#define PCA963X_LED_PWM		0x2	/* Controlled through PWM */
#define PCA963X_LED_GRP_PWM	0x3	/* Controlled through PWM/GRPPWM */

#define PCA963X_MODE2_OUTDRV	0x04	/* Open-drain or totem pole */
#define PCA963X_MODE2_INVRT	0x10	/* Normal or inverted direction */
#define PCA963X_MODE2_DMBLNK	0x20	/* Enable blinking */

#define PCA963X_MODE1		0x00
#define PCA963X_MODE2		0x01
#define PCA963X_PWM_BASE	0x02

enum pca963x_type {
	pca9633,
	pca9634,
	pca9635,
};

struct pca963x_chipdef {
	u8			grppwm;
	u8			grpfreq;
	u8			ledout_base;
	int			n_leds;
	unsigned int		scaling;
};

static struct pca963x_chipdef pca963x_chipdefs[] = {
	[pca9633] = {
		.grppwm		= 0x6,
		.grpfreq	= 0x7,
		.ledout_base	= 0x8,
		.n_leds		= 4,
	},
	[pca9634] = {
		.grppwm		= 0xa,
		.grpfreq	= 0xb,
		.ledout_base	= 0xc,
		.n_leds		= 8,
	},
	[pca9635] = {
		.grppwm		= 0x12,
		.grpfreq	= 0x13,
		.ledout_base	= 0x14,
		.n_leds		= 16,
	},
};

/* Total blink period in milliseconds */
#define PCA963X_BLINK_PERIOD_MIN	42
#define PCA963X_BLINK_PERIOD_MAX	10667

static const struct i2c_device_id pca963x_id[] = {
	{ "pca9632", pca9633 },
	{ "pca9633", pca9633 },
	{ "pca9634", pca9634 },
	{ "pca9635", pca9635 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, pca963x_id);

struct pca963x;

struct pca963x_led {
	struct pca963x *chip;
	struct led_classdev led_cdev;
	int led_num; /* 0 .. 15 potentially */
	bool blinking;
	u8 gdc;
	u8 gfrq;
};

struct pca963x {
	struct pca963x_chipdef *chipdef;
	struct mutex mutex;
	struct i2c_client *client;
	unsigned long leds_on;
	struct pca963x_led leds[];
};

static int pca963x_brightness(struct pca963x_led *led,
			      enum led_brightness brightness)
{
	struct i2c_client *client = led->chip->client;
	struct pca963x_chipdef *chipdef = led->chip->chipdef;
	u8 ledout_addr, ledout, mask, val;
	int shift;
	int ret;

	ledout_addr = chipdef->ledout_base + (led->led_num / 4);
	shift = 2 * (led->led_num % 4);
	mask = 0x3 << shift;
	ledout = i2c_smbus_read_byte_data(client, ledout_addr);

	switch (brightness) {
	case LED_FULL:
		if (led->blinking) {
			val = (ledout & ~mask) | (PCA963X_LED_GRP_PWM << shift);
			ret = i2c_smbus_write_byte_data(client,
						PCA963X_PWM_BASE +
						led->led_num,
						LED_FULL);
		} else {
			val = (ledout & ~mask) | (PCA963X_LED_ON << shift);
		}
		ret = i2c_smbus_write_byte_data(client, ledout_addr, val);
		break;
	case LED_OFF:
		val = ledout & ~mask;
		ret = i2c_smbus_write_byte_data(client, ledout_addr, val);
		led->blinking = false;
		break;
	default:
		ret = i2c_smbus_write_byte_data(client,
						PCA963X_PWM_BASE +
						led->led_num,
						brightness);
		if (ret < 0)
			return ret;

		if (led->blinking)
			val = (ledout & ~mask) | (PCA963X_LED_GRP_PWM << shift);
		else
			val = (ledout & ~mask) | (PCA963X_LED_PWM << shift);

		ret = i2c_smbus_write_byte_data(client, ledout_addr, val);
		break;
	}

	return ret;
}

static void pca963x_blink(struct pca963x_led *led)
{
	struct i2c_client *client = led->chip->client;
	struct pca963x_chipdef *chipdef = led->chip->chipdef;
	u8 ledout_addr, ledout, mask, val, mode2;
	int shift;

	ledout_addr = chipdef->ledout_base + (led->led_num / 4);
	shift = 2 * (led->led_num % 4);
	mask = 0x3 << shift;
	mode2 = i2c_smbus_read_byte_data(client, PCA963X_MODE2);

	i2c_smbus_write_byte_data(client, chipdef->grppwm, led->gdc);

	i2c_smbus_write_byte_data(client, chipdef->grpfreq, led->gfrq);

	if (!(mode2 & PCA963X_MODE2_DMBLNK))
		i2c_smbus_write_byte_data(client, PCA963X_MODE2,
					  mode2 | PCA963X_MODE2_DMBLNK);

	mutex_lock(&led->chip->mutex);

	ledout = i2c_smbus_read_byte_data(client, ledout_addr);
	if ((ledout & mask) != (PCA963X_LED_GRP_PWM << shift)) {
		val = (ledout & ~mask) | (PCA963X_LED_GRP_PWM << shift);
		i2c_smbus_write_byte_data(client, ledout_addr, val);
	}

	mutex_unlock(&led->chip->mutex);
	led->blinking = true;
}

static int pca963x_power_state(struct pca963x_led *led)
{
	struct i2c_client *client = led->chip->client;
	unsigned long *leds_on = &led->chip->leds_on;
	unsigned long cached_leds = *leds_on;

	if (led->led_cdev.brightness)
		set_bit(led->led_num, leds_on);
	else
		clear_bit(led->led_num, leds_on);

	if (!(*leds_on) != !cached_leds)
		return i2c_smbus_write_byte_data(client, PCA963X_MODE1,
						 *leds_on ? 0 : BIT(4));

	return 0;
}

static int pca963x_led_set(struct led_classdev *led_cdev,
			   enum led_brightness value)
{
	struct pca963x_led *led;
	int ret;

	led = container_of(led_cdev, struct pca963x_led, led_cdev);

	mutex_lock(&led->chip->mutex);

	ret = pca963x_brightness(led, value);
	if (ret < 0)
		goto unlock;
	ret = pca963x_power_state(led);

unlock:
	mutex_unlock(&led->chip->mutex);
	return ret;
}

static unsigned int pca963x_period_scale(struct pca963x_led *led,
					 unsigned int val)
{
	unsigned int scaling = led->chip->chipdef->scaling;

	return scaling ? DIV_ROUND_CLOSEST(val * scaling, 1000) : val;
}

static int pca963x_blink_set(struct led_classdev *led_cdev,
			     unsigned long *delay_on, unsigned long *delay_off)
{
	unsigned long time_on, time_off, period;
	struct pca963x_led *led;
	u8 gdc, gfrq;

	led = container_of(led_cdev, struct pca963x_led, led_cdev);

	time_on = *delay_on;
	time_off = *delay_off;

	/* If both zero, pick reasonable defaults of 500ms each */
	if (!time_on && !time_off) {
		time_on = 500;
		time_off = 500;
	}

	period = pca963x_period_scale(led, time_on + time_off);

	/* If period not supported by hardware, default to someting sane. */
	if ((period < PCA963X_BLINK_PERIOD_MIN) ||
	    (period > PCA963X_BLINK_PERIOD_MAX)) {
		time_on = 500;
		time_off = 500;
		period = pca963x_period_scale(led, 1000);
	}

	/*
	 * From manual: duty cycle = (GDC / 256) ->
	 *	(time_on / period) = (GDC / 256) ->
	 *		GDC = ((time_on * 256) / period)
	 */
	gdc = (pca963x_period_scale(led, time_on) * 256) / period;

	/*
	 * From manual: period = ((GFRQ + 1) / 24) in seconds.
	 * So, period (in ms) = (((GFRQ + 1) / 24) * 1000) ->
	 *		GFRQ = ((period * 24 / 1000) - 1)
	 */
	gfrq = (period * 24 / 1000) - 1;

	led->gdc = gdc;
	led->gfrq = gfrq;

	pca963x_blink(led);
	led->led_cdev.brightness = LED_FULL;
	pca963x_led_set(led_cdev, LED_FULL);

	*delay_on = time_on;
	*delay_off = time_off;

	return 0;
}

static int pca963x_register_leds(struct i2c_client *client,
				 struct pca963x *chip)
{
	struct pca963x_chipdef *chipdef = chip->chipdef;
	struct pca963x_led *led = chip->leds;
	struct device *dev = &client->dev;
	struct fwnode_handle *child;
	bool hw_blink;
	s32 mode2;
	u32 reg;
	int ret;

	if (device_property_read_u32(dev, "nxp,period-scale",
				     &chipdef->scaling))
		chipdef->scaling = 1000;

	hw_blink = device_property_read_bool(dev, "nxp,hw-blink");

	mode2 = i2c_smbus_read_byte_data(client, PCA963X_MODE2);
	if (mode2 < 0)
		return mode2;

	/* default to open-drain unless totem pole (push-pull) is specified */
	if (device_property_read_bool(dev, "nxp,totem-pole"))
		mode2 |= PCA963X_MODE2_OUTDRV;
	else
		mode2 &= ~PCA963X_MODE2_OUTDRV;

	/* default to non-inverted output, unless inverted is specified */
	if (device_property_read_bool(dev, "nxp,inverted-out"))
		mode2 |= PCA963X_MODE2_INVRT;
	else
		mode2 &= ~PCA963X_MODE2_INVRT;

	ret = i2c_smbus_write_byte_data(client, PCA963X_MODE2, mode2);
	if (ret < 0)
		return ret;

	device_for_each_child_node(dev, child) {
		struct led_init_data init_data = {};
		char default_label[32];

		ret = fwnode_property_read_u32(child, "reg", &reg);
		if (ret || reg >= chipdef->n_leds) {
			dev_err(dev, "Invalid 'reg' property for node %pfw\n",
				child);
			ret = -EINVAL;
			goto err;
		}

		led->led_num = reg;
		led->chip = chip;
		led->led_cdev.brightness_set_blocking = pca963x_led_set;
		if (hw_blink)
			led->led_cdev.blink_set = pca963x_blink_set;
		led->blinking = false;

		init_data.fwnode = child;
		/* for backwards compatibility */
		init_data.devicename = "pca963x";
		snprintf(default_label, sizeof(default_label), "%d:%.2x:%u",
			 client->adapter->nr, client->addr, reg);
		init_data.default_label = default_label;

		ret = devm_led_classdev_register_ext(dev, &led->led_cdev,
						     &init_data);
		if (ret) {
			dev_err(dev, "Failed to register LED for node %pfw\n",
				child);
			goto err;
		}

		++led;
	}

	return 0;
err:
	fwnode_handle_put(child);
	return ret;
}

static const struct of_device_id of_pca963x_match[] = {
	{ .compatible = "nxp,pca9632", },
	{ .compatible = "nxp,pca9633", },
	{ .compatible = "nxp,pca9634", },
	{ .compatible = "nxp,pca9635", },
	{},
};
MODULE_DEVICE_TABLE(of, of_pca963x_match);

static int pca963x_probe(struct i2c_client *client)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(client);
	struct device *dev = &client->dev;
	struct pca963x_chipdef *chipdef;
	struct pca963x *chip;
	int i, count;

	chipdef = &pca963x_chipdefs[id->driver_data];

	count = device_get_child_node_count(dev);
	if (!count || count > chipdef->n_leds) {
		dev_err(dev, "Node %pfw must define between 1 and %d LEDs\n",
			dev_fwnode(dev), chipdef->n_leds);
		return -EINVAL;
	}

	chip = devm_kzalloc(dev, struct_size(chip, leds, count), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	i2c_set_clientdata(client, chip);

	mutex_init(&chip->mutex);
	chip->chipdef = chipdef;
	chip->client = client;

	/* Turn off LEDs by default*/
	for (i = 0; i < chipdef->n_leds / 4; i++)
		i2c_smbus_write_byte_data(client, chipdef->ledout_base + i, 0x00);

	/* Disable LED all-call address, and power down initially */
	i2c_smbus_write_byte_data(client, PCA963X_MODE1, BIT(4));

	return pca963x_register_leds(client, chip);
}

static struct i2c_driver pca963x_driver = {
	.driver = {
		.name	= "leds-pca963x",
		.of_match_table = of_pca963x_match,
	},
	.probe_new = pca963x_probe,
	.id_table = pca963x_id,
};

module_i2c_driver(pca963x_driver);

MODULE_AUTHOR("Peter Meerwald <p.meerwald@bct-electronic.com>");
MODULE_DESCRIPTION("PCA963X LED driver");
MODULE_LICENSE("GPL v2");
