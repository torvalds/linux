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
#include <linux/platform_data/leds-pca963x.h>

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

struct pca963x_led;

struct pca963x {
	struct pca963x_chipdef *chipdef;
	struct mutex mutex;
	struct i2c_client *client;
	struct pca963x_led *leds;
	unsigned long leds_on;
};

struct pca963x_led {
	struct pca963x *chip;
	struct led_classdev led_cdev;
	int led_num; /* 0 .. 15 potentially */
	char name[32];
	u8 gdc;
	u8 gfrq;
};

static int pca963x_brightness(struct pca963x_led *pca963x,
			      enum led_brightness brightness)
{
	struct i2c_client *client = pca963x->chip->client;
	struct pca963x_chipdef *chipdef = pca963x->chip->chipdef;
	u8 ledout_addr, ledout, mask, val;
	int shift;
	int ret;

	ledout_addr = chipdef->ledout_base + (pca963x->led_num / 4);
	shift = 2 * (pca963x->led_num % 4);
	mask = 0x3 << shift;
	ledout = i2c_smbus_read_byte_data(client, ledout_addr);

	switch (brightness) {
	case LED_FULL:
		val = (ledout & ~mask) | (PCA963X_LED_ON << shift);
		ret = i2c_smbus_write_byte_data(client, ledout_addr, val);
		break;
	case LED_OFF:
		val = ledout & ~mask;
		ret = i2c_smbus_write_byte_data(client, ledout_addr, val);
		break;
	default:
		ret = i2c_smbus_write_byte_data(client,
						PCA963X_PWM_BASE +
						pca963x->led_num,
						brightness);
		if (ret < 0)
			return ret;

		val = (ledout & ~mask) | (PCA963X_LED_PWM << shift);
		ret = i2c_smbus_write_byte_data(client, ledout_addr, val);
		break;
	}

	return ret;
}

static void pca963x_blink(struct pca963x_led *pca963x)
{
	struct i2c_client *client = pca963x->chip->client;
	struct pca963x_chipdef *chipdef = pca963x->chip->chipdef;
	u8 ledout_addr, ledout, mask, val, mode2;
	int shift;

	ledout_addr = chipdef->ledout_base + (pca963x->led_num / 4);
	shift = 2 * (pca963x->led_num % 4);
	mask = 0x3 << shift;
	mode2 = i2c_smbus_read_byte_data(client, PCA963X_MODE2);

	i2c_smbus_write_byte_data(client, chipdef->grppwm, pca963x->gdc);

	i2c_smbus_write_byte_data(client, chipdef->grpfreq, pca963x->gfrq);

	if (!(mode2 & PCA963X_MODE2_DMBLNK))
		i2c_smbus_write_byte_data(client, PCA963X_MODE2,
					  mode2 | PCA963X_MODE2_DMBLNK);

	mutex_lock(&pca963x->chip->mutex);

	ledout = i2c_smbus_read_byte_data(client, ledout_addr);
	if ((ledout & mask) != (PCA963X_LED_GRP_PWM << shift)) {
		val = (ledout & ~mask) | (PCA963X_LED_GRP_PWM << shift);
		i2c_smbus_write_byte_data(client, ledout_addr, val);
	}

	mutex_unlock(&pca963x->chip->mutex);
}

static int pca963x_power_state(struct pca963x_led *pca963x)
{
	struct i2c_client *client = pca963x->chip->client;
	unsigned long *leds_on = &pca963x->chip->leds_on;
	unsigned long cached_leds = *leds_on;

	if (pca963x->led_cdev.brightness)
		set_bit(pca963x->led_num, leds_on);
	else
		clear_bit(pca963x->led_num, leds_on);

	if (!(*leds_on) != !cached_leds)
		return i2c_smbus_write_byte_data(client, PCA963X_MODE1,
						 *leds_on ? 0 : BIT(4));

	return 0;
}

static int pca963x_led_set(struct led_classdev *led_cdev,
			   enum led_brightness value)
{
	struct pca963x_led *pca963x;
	int ret;

	pca963x = container_of(led_cdev, struct pca963x_led, led_cdev);

	mutex_lock(&pca963x->chip->mutex);

	ret = pca963x_brightness(pca963x, value);
	if (ret < 0)
		goto unlock;
	ret = pca963x_power_state(pca963x);

unlock:
	mutex_unlock(&pca963x->chip->mutex);
	return ret;
}

static unsigned int pca963x_period_scale(struct pca963x_led *pca963x,
					 unsigned int val)
{
	unsigned int scaling = pca963x->chip->chipdef->scaling;

	return scaling ? DIV_ROUND_CLOSEST(val * scaling, 1000) : val;
}

static int pca963x_blink_set(struct led_classdev *led_cdev,
			     unsigned long *delay_on, unsigned long *delay_off)
{
	struct pca963x_led *pca963x;
	unsigned long time_on, time_off, period;
	u8 gdc, gfrq;

	pca963x = container_of(led_cdev, struct pca963x_led, led_cdev);

	time_on = *delay_on;
	time_off = *delay_off;

	/* If both zero, pick reasonable defaults of 500ms each */
	if (!time_on && !time_off) {
		time_on = 500;
		time_off = 500;
	}

	period = pca963x_period_scale(pca963x, time_on + time_off);

	/* If period not supported by hardware, default to someting sane. */
	if ((period < PCA963X_BLINK_PERIOD_MIN) ||
	    (period > PCA963X_BLINK_PERIOD_MAX)) {
		time_on = 500;
		time_off = 500;
		period = pca963x_period_scale(pca963x, 1000);
	}

	/*
	 * From manual: duty cycle = (GDC / 256) ->
	 *	(time_on / period) = (GDC / 256) ->
	 *		GDC = ((time_on * 256) / period)
	 */
	gdc = (pca963x_period_scale(pca963x, time_on) * 256) / period;

	/*
	 * From manual: period = ((GFRQ + 1) / 24) in seconds.
	 * So, period (in ms) = (((GFRQ + 1) / 24) * 1000) ->
	 *		GFRQ = ((period * 24 / 1000) - 1)
	 */
	gfrq = (period * 24 / 1000) - 1;

	pca963x->gdc = gdc;
	pca963x->gfrq = gfrq;

	pca963x_blink(pca963x);

	*delay_on = time_on;
	*delay_off = time_off;

	return 0;
}

static struct pca963x_platform_data *
pca963x_get_pdata(struct device *dev, struct pca963x_chipdef *chipdef)
{
	struct pca963x_platform_data *pdata;
	struct led_info *pca963x_leds;
	struct fwnode_handle *child;
	int count;

	count = device_get_child_node_count(dev);
	if (!count || count > chipdef->n_leds)
		return ERR_PTR(-ENODEV);

	pca963x_leds = devm_kcalloc(dev, chipdef->n_leds,
				    sizeof(struct led_info), GFP_KERNEL);
	if (!pca963x_leds)
		return ERR_PTR(-ENOMEM);

	device_for_each_child_node(dev, child) {
		struct led_info led = {};
		u32 reg;
		int res;

		res = fwnode_property_read_u32(child, "reg", &reg);
		if ((res != 0) || (reg >= chipdef->n_leds))
			continue;

		res = fwnode_property_read_string(child, "label", &led.name);
		if ((res != 0) && is_of_node(child))
			led.name = to_of_node(child)->name;

		fwnode_property_read_string(child, "linux,default-trigger",
					    &led.default_trigger);

		pca963x_leds[reg] = led;
	}
	pdata = devm_kzalloc(dev, sizeof(struct pca963x_platform_data),
			     GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	pdata->leds.leds = pca963x_leds;
	pdata->leds.num_leds = chipdef->n_leds;

	/* default to open-drain unless totem pole (push-pull) is specified */
	if (device_property_read_bool(dev, "nxp,totem-pole"))
		pdata->outdrv = PCA963X_TOTEM_POLE;
	else
		pdata->outdrv = PCA963X_OPEN_DRAIN;

	/* default to software blinking unless hardware blinking is specified */
	if (device_property_read_bool(dev, "nxp,hw-blink"))
		pdata->blink_type = PCA963X_HW_BLINK;
	else
		pdata->blink_type = PCA963X_SW_BLINK;

	if (device_property_read_u32(dev, "nxp,period-scale",
				     &chipdef->scaling))
		chipdef->scaling = 1000;

	/* default to non-inverted output, unless inverted is specified */
	if (device_property_read_bool(dev, "nxp,inverted-out"))
		pdata->dir = PCA963X_INVERTED;
	else
		pdata->dir = PCA963X_NORMAL;

	return pdata;
}

static const struct of_device_id of_pca963x_match[] = {
	{ .compatible = "nxp,pca9632", },
	{ .compatible = "nxp,pca9633", },
	{ .compatible = "nxp,pca9634", },
	{ .compatible = "nxp,pca9635", },
	{},
};
MODULE_DEVICE_TABLE(of, of_pca963x_match);

static int pca963x_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct pca963x_chipdef *chipdef;
	struct pca963x_platform_data *pdata;
	struct pca963x_led *pca963x;
	struct pca963x *chip;
	int i, err;

	chipdef = &pca963x_chipdefs[id->driver_data];
	pdata = dev_get_platdata(dev);

	if (!pdata) {
		pdata = pca963x_get_pdata(dev, chipdef);
		if (IS_ERR(pdata)) {
			dev_warn(dev, "could not parse configuration\n");
			pdata = NULL;
		}
	}

	if (pdata && (pdata->leds.num_leds < 1 ||
		      pdata->leds.num_leds > chipdef->n_leds)) {
		dev_err(dev, "board info must claim 1-%d LEDs",
			chipdef->n_leds);
		return -EINVAL;
	}

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;
	pca963x = devm_kcalloc(dev, chipdef->n_leds, sizeof(*pca963x), GFP_KERNEL);
	if (!pca963x)
		return -ENOMEM;

	i2c_set_clientdata(client, chip);

	mutex_init(&chip->mutex);
	chip->chipdef = chipdef;
	chip->client = client;
	chip->leds = pca963x;

	/* Turn off LEDs by default*/
	for (i = 0; i < chipdef->n_leds / 4; i++)
		i2c_smbus_write_byte_data(client, chipdef->ledout_base + i, 0x00);

	for (i = 0; i < chipdef->n_leds; i++) {
		pca963x[i].led_num = i;
		pca963x[i].chip = chip;

		/* Platform data can specify LED names and default triggers */
		if (pdata && i < pdata->leds.num_leds) {
			if (pdata->leds.leds[i].name)
				snprintf(pca963x[i].name,
					 sizeof(pca963x[i].name), "pca963x:%s",
					 pdata->leds.leds[i].name);
			if (pdata->leds.leds[i].default_trigger)
				pca963x[i].led_cdev.default_trigger =
					pdata->leds.leds[i].default_trigger;
		}
		if (!pdata || i >= pdata->leds.num_leds ||
						!pdata->leds.leds[i].name)
			snprintf(pca963x[i].name, sizeof(pca963x[i].name),
				 "pca963x:%d:%.2x:%d", client->adapter->nr,
				 client->addr, i);

		pca963x[i].led_cdev.name = pca963x[i].name;
		pca963x[i].led_cdev.brightness_set_blocking = pca963x_led_set;

		if (pdata && pdata->blink_type == PCA963X_HW_BLINK)
			pca963x[i].led_cdev.blink_set = pca963x_blink_set;

		err = devm_led_classdev_register(dev, &pca963x[i].led_cdev);
		if (err < 0)
			return err;
	}

	/* Disable LED all-call address, and power down initially */
	i2c_smbus_write_byte_data(client, PCA963X_MODE1, BIT(4));

	if (pdata) {
		u8 mode2 = i2c_smbus_read_byte_data(client, PCA963X_MODE2);
		/* Configure output: open-drain or totem pole (push-pull) */
		if (pdata->outdrv == PCA963X_OPEN_DRAIN)
			mode2 &= ~PCA963X_MODE2_OUTDRV;
		else
			mode2 |= PCA963X_MODE2_OUTDRV;
		/* Configure direction: normal or inverted */
		if (pdata->dir == PCA963X_INVERTED)
			mode2 |= PCA963X_MODE2_INVRT;
		i2c_smbus_write_byte_data(client, PCA963X_MODE2, mode2);
	}

	return 0;
}

static struct i2c_driver pca963x_driver = {
	.driver = {
		.name	= "leds-pca963x",
		.of_match_table = of_pca963x_match,
	},
	.probe	= pca963x_probe,
	.id_table = pca963x_id,
};

module_i2c_driver(pca963x_driver);

MODULE_AUTHOR("Peter Meerwald <p.meerwald@bct-electronic.com>");
MODULE_DESCRIPTION("PCA963X LED driver");
MODULE_LICENSE("GPL v2");
