/*
 * Copyright 2011 bct electronic GmbH
 * Copyright 2013 Qtechnology/AS
 *
 * Author: Peter Meerwald <p.meerwald@bct-electronic.com>
 * Author: Ricardo Ribalda <ricardo.ribalda@gmail.com>
 *
 * Based on leds-pca955x.c
 *
 * This file is subject to the terms and conditions of version 2 of
 * the GNU General Public License.  See the file COPYING in the main
 * directory of this archive for more details.
 *
 * LED driver for the PCA9633 I2C LED driver (7-bit slave address 0x62)
 * LED driver for the PCA9634 I2C LED driver (7-bit slave address set by hw.)
 *
 * Note that hardware blinking violates the leds infrastructure driver
 * interface since the hardware only supports blinking all LEDs with the
 * same delay_on/delay_off rates.  That is, only the LEDs that are set to
 * blink will actually blink but all LEDs that are set to blink will blink
 * in identical fashion.  The delay_on/delay_off values of the last LED
 * that is set to blink will be used for all of the blinking LEDs.
 * Hardware blinking is disabled by default but can be enabled by setting
 * the 'blink_type' member in the platform_data struct to 'PCA9633_HW_BLINK'
 * or by adding the 'nxp,hw-blink' property to the DTS.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/leds.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/platform_data/leds-pca9633.h>

/* LED select registers determine the source that drives LED outputs */
#define PCA9633_LED_OFF		0x0	/* LED driver off */
#define PCA9633_LED_ON		0x1	/* LED driver on */
#define PCA9633_LED_PWM		0x2	/* Controlled through PWM */
#define PCA9633_LED_GRP_PWM	0x3	/* Controlled through PWM/GRPPWM */

#define PCA9633_MODE2_DMBLNK	0x20	/* Enable blinking */

#define PCA9633_MODE1		0x00
#define PCA9633_MODE2		0x01
#define PCA9633_PWM_BASE	0x02

enum pca9633_type {
	pca9633,
	pca9634,
};

struct pca9633_chipdef {
	u8			grppwm;
	u8			grpfreq;
	u8			ledout_base;
	int			n_leds;
};

static struct pca9633_chipdef pca9633_chipdefs[] = {
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
};

/* Total blink period in milliseconds */
#define PCA9632_BLINK_PERIOD_MIN	42
#define PCA9632_BLINK_PERIOD_MAX	10667

static const struct i2c_device_id pca9633_id[] = {
	{ "pca9632", pca9633 },
	{ "pca9633", pca9633 },
	{ "pca9634", pca9634 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, pca9633_id);

enum pca9633_cmd {
	BRIGHTNESS_SET,
	BLINK_SET,
};

struct pca9633_led {
	struct i2c_client *client;
	struct pca9633_chipdef *chipdef;
	struct work_struct work;
	enum led_brightness brightness;
	struct led_classdev led_cdev;
	int led_num; /* 0 .. 7 potentially */
	enum pca9633_cmd cmd;
	char name[32];
	u8 gdc;
	u8 gfrq;
};

static void pca9633_brightness_work(struct pca9633_led *pca9633)
{
	u8 ledout_addr = pca9633->chipdef->ledout_base + (pca9633->led_num / 4);
	u8 ledout;
	int shift = 2 * (pca9633->led_num % 4);
	u8 mask = 0x3 << shift;

	ledout = i2c_smbus_read_byte_data(pca9633->client, ledout_addr);
	switch (pca9633->brightness) {
	case LED_FULL:
		i2c_smbus_write_byte_data(pca9633->client, ledout_addr,
			(ledout & ~mask) | (PCA9633_LED_ON << shift));
		break;
	case LED_OFF:
		i2c_smbus_write_byte_data(pca9633->client, ledout_addr,
			ledout & ~mask);
		break;
	default:
		i2c_smbus_write_byte_data(pca9633->client,
			PCA9633_PWM_BASE + pca9633->led_num,
			pca9633->brightness);
		i2c_smbus_write_byte_data(pca9633->client, ledout_addr,
			(ledout & ~mask) | (PCA9633_LED_PWM << shift));
		break;
	}
}

static void pca9633_blink_work(struct pca9633_led *pca9633)
{
	u8 ledout_addr = pca9633->chipdef->ledout_base + (pca9633->led_num / 4);
	u8 ledout = i2c_smbus_read_byte_data(pca9633->client, ledout_addr);
	u8 mode2 = i2c_smbus_read_byte_data(pca9633->client, PCA9633_MODE2);
	int shift = 2 * (pca9633->led_num % 4);
	u8 mask = 0x3 << shift;

	i2c_smbus_write_byte_data(pca9633->client, pca9633->chipdef->grppwm,
		pca9633->gdc);

	i2c_smbus_write_byte_data(pca9633->client, pca9633->chipdef->grpfreq,
		pca9633->gfrq);

	if (!(mode2 & PCA9633_MODE2_DMBLNK))
		i2c_smbus_write_byte_data(pca9633->client, PCA9633_MODE2,
			mode2 | PCA9633_MODE2_DMBLNK);

	if ((ledout & mask) != (PCA9633_LED_GRP_PWM << shift))
		i2c_smbus_write_byte_data(pca9633->client, ledout_addr,
			(ledout & ~mask) | (PCA9633_LED_GRP_PWM << shift));
}

static void pca9633_work(struct work_struct *work)
{
	struct pca9633_led *pca9633 = container_of(work,
		struct pca9633_led, work);

	switch (pca9633->cmd) {
	case BRIGHTNESS_SET:
		pca9633_brightness_work(pca9633);
		break;
	case BLINK_SET:
		pca9633_blink_work(pca9633);
		break;
	}
}

static void pca9633_led_set(struct led_classdev *led_cdev,
	enum led_brightness value)
{
	struct pca9633_led *pca9633;

	pca9633 = container_of(led_cdev, struct pca9633_led, led_cdev);

	pca9633->cmd = BRIGHTNESS_SET;
	pca9633->brightness = value;

	/*
	 * Must use workqueue for the actual I/O since I2C operations
	 * can sleep.
	 */
	schedule_work(&pca9633->work);
}

static int pca9633_blink_set(struct led_classdev *led_cdev,
		unsigned long *delay_on, unsigned long *delay_off)
{
	struct pca9633_led *pca9633;
	unsigned long time_on, time_off, period;
	u8 gdc, gfrq;

	pca9633 = container_of(led_cdev, struct pca9633_led, led_cdev);

	time_on = *delay_on;
	time_off = *delay_off;

	/* If both zero, pick reasonable defaults of 500ms each */
	if (!time_on && !time_off) {
		time_on = 500;
		time_off = 500;
	}

	period = time_on + time_off;

	/* If period not supported by hardware, default to someting sane. */
	if ((period < PCA9632_BLINK_PERIOD_MIN) ||
	    (period > PCA9632_BLINK_PERIOD_MAX)) {
		time_on = 500;
		time_off = 500;
		period = time_on + time_off;
	}

	/*
	 * From manual: duty cycle = (GDC / 256) ->
	 *	(time_on / period) = (GDC / 256) ->
	 *		GDC = ((time_on * 256) / period)
	 */
	gdc = (time_on * 256) / period;

	/*
	 * From manual: period = ((GFRQ + 1) / 24) in seconds.
	 * So, period (in ms) = (((GFRQ + 1) / 24) * 1000) ->
	 *		GFRQ = ((period * 24 / 1000) - 1)
	 */
	gfrq = (period * 24 / 1000) - 1;

	pca9633->cmd = BLINK_SET;
	pca9633->gdc = gdc;
	pca9633->gfrq = gfrq;

	/*
	 * Must use workqueue for the actual I/O since I2C operations
	 * can sleep.
	 */
	schedule_work(&pca9633->work);

	*delay_on = time_on;
	*delay_off = time_off;

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static struct pca9633_platform_data *
pca9633_dt_init(struct i2c_client *client, struct pca9633_chipdef *chip)
{
	struct device_node *np = client->dev.of_node, *child;
	struct pca9633_platform_data *pdata;
	struct led_info *pca9633_leds;
	int count;

	count = of_get_child_count(np);
	if (!count || count > chip->n_leds)
		return ERR_PTR(-ENODEV);

	pca9633_leds = devm_kzalloc(&client->dev,
			sizeof(struct led_info) * chip->n_leds, GFP_KERNEL);
	if (!pca9633_leds)
		return ERR_PTR(-ENOMEM);

	for_each_child_of_node(np, child) {
		struct led_info led;
		u32 reg;
		int res;

		led.name =
			of_get_property(child, "label", NULL) ? : child->name;
		led.default_trigger =
			of_get_property(child, "linux,default-trigger", NULL);
		res = of_property_read_u32(child, "reg", &reg);
		if (res != 0)
			continue;
		pca9633_leds[reg] = led;
	}
	pdata = devm_kzalloc(&client->dev,
			     sizeof(struct pca9633_platform_data), GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	pdata->leds.leds = pca9633_leds;
	pdata->leds.num_leds = count;

	/* default to open-drain unless totem pole (push-pull) is specified */
	if (of_property_read_bool(np, "nxp,totem-pole"))
		pdata->outdrv = PCA9633_TOTEM_POLE;
	else
		pdata->outdrv = PCA9633_OPEN_DRAIN;

	/* default to software blinking unless hardware blinking is specified */
	if (of_property_read_bool(np, "nxp,hw-blink"))
		pdata->blink_type = PCA9633_HW_BLINK;
	else
		pdata->blink_type = PCA9633_SW_BLINK;

	return pdata;
}

static const struct of_device_id of_pca9633_match[] = {
	{ .compatible = "nxp,pca9632", },
	{ .compatible = "nxp,pca9633", },
	{ .compatible = "nxp,pca9634", },
	{},
};
#else
static struct pca9633_platform_data *
pca9633_dt_init(struct i2c_client *client, struct pca9633_chipdef *chip)
{
	return ERR_PTR(-ENODEV);
}
#endif

static int pca9633_probe(struct i2c_client *client,
					const struct i2c_device_id *id)
{
	struct pca9633_led *pca9633;
	struct pca9633_platform_data *pdata;
	struct pca9633_chipdef *chip;
	int i, err;

	chip = &pca9633_chipdefs[id->driver_data];
	pdata = dev_get_platdata(&client->dev);

	if (!pdata) {
		pdata = pca9633_dt_init(client, chip);
		if (IS_ERR(pdata)) {
			dev_warn(&client->dev, "could not parse configuration\n");
			pdata = NULL;
		}
	}

	if (pdata && (pdata->leds.num_leds < 1 ||
				 pdata->leds.num_leds > chip->n_leds)) {
		dev_err(&client->dev, "board info must claim 1-%d LEDs",
								chip->n_leds);
		return -EINVAL;
	}

	pca9633 = devm_kzalloc(&client->dev, chip->n_leds * sizeof(*pca9633),
								GFP_KERNEL);
	if (!pca9633)
		return -ENOMEM;

	i2c_set_clientdata(client, pca9633);

	for (i = 0; i < chip->n_leds; i++) {
		pca9633[i].client = client;
		pca9633[i].led_num = i;
		pca9633[i].chipdef = chip;

		/* Platform data can specify LED names and default triggers */
		if (pdata && i < pdata->leds.num_leds) {
			if (pdata->leds.leds[i].name)
				snprintf(pca9633[i].name,
					 sizeof(pca9633[i].name), "pca9633:%s",
					 pdata->leds.leds[i].name);
			if (pdata->leds.leds[i].default_trigger)
				pca9633[i].led_cdev.default_trigger =
					pdata->leds.leds[i].default_trigger;
		}
		if (!pdata || i >= pdata->leds.num_leds ||
						!pdata->leds.leds[i].name)
			snprintf(pca9633[i].name, sizeof(pca9633[i].name),
				 "pca9633:%d:%.2x:%d", client->adapter->nr,
				 client->addr, i);

		pca9633[i].led_cdev.name = pca9633[i].name;
		pca9633[i].led_cdev.brightness_set = pca9633_led_set;

		if (pdata && pdata->blink_type == PCA9633_HW_BLINK)
			pca9633[i].led_cdev.blink_set = pca9633_blink_set;

		INIT_WORK(&pca9633[i].work, pca9633_work);

		err = led_classdev_register(&client->dev, &pca9633[i].led_cdev);
		if (err < 0)
			goto exit;
	}

	/* Disable LED all-call address and set normal mode */
	i2c_smbus_write_byte_data(client, PCA9633_MODE1, 0x00);

	/* Configure output: open-drain or totem pole (push-pull) */
	if (pdata && pdata->outdrv == PCA9633_OPEN_DRAIN)
		i2c_smbus_write_byte_data(client, PCA9633_MODE2, 0x01);

	/* Turn off LEDs */
	i2c_smbus_write_byte_data(client, chip->ledout_base, 0x00);
	if (chip->n_leds > 4)
		i2c_smbus_write_byte_data(client, chip->ledout_base + 1, 0x00);

	return 0;

exit:
	while (i--) {
		led_classdev_unregister(&pca9633[i].led_cdev);
		cancel_work_sync(&pca9633[i].work);
	}

	return err;
}

static int pca9633_remove(struct i2c_client *client)
{
	struct pca9633_led *pca9633 = i2c_get_clientdata(client);
	int i;

	for (i = 0; i < pca9633->chipdef->n_leds; i++)
		if (pca9633[i].client != NULL) {
			led_classdev_unregister(&pca9633[i].led_cdev);
			cancel_work_sync(&pca9633[i].work);
		}

	return 0;
}

static struct i2c_driver pca9633_driver = {
	.driver = {
		.name	= "leds-pca9633",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(of_pca9633_match),
	},
	.probe	= pca9633_probe,
	.remove	= pca9633_remove,
	.id_table = pca9633_id,
};

module_i2c_driver(pca9633_driver);

MODULE_AUTHOR("Peter Meerwald <p.meerwald@bct-electronic.com>");
MODULE_DESCRIPTION("PCA9633 LED driver");
MODULE_LICENSE("GPL v2");
