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

#include <linux/acpi.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_data/leds-pca963x.h>
#include <linux/slab.h>
#include <linux/string.h>

#define PCA963X_MAX_NAME_LEN	32

#define PCA963X_MODE1		0x00 /* mode 1 register addr */
#define PCA963X_MODE1_ALLCALL_ON	BIT(0) /* respond to LED All Call */
#define PCA963X_MODE1_RESPOND_SUB3	BIT(1) /* respond to Sub address 3 */
#define PCA963X_MODE1_RESPOND_SUB2	BIT(2) /* respond to Sub address 2 */
#define PCA963X_MODE1_RESPOND_SUB1	BIT(3) /* respond to Sub address 1 */
#define PCA963X_MODE1_SLEEP		BIT(4) /* put in low power mode */
#define PCA963X_MODE1_AI_ROLL_PWM	BIT(5) /* auto-increment only PWM's */
#define PCA963X_MODE1_AI_ROLL_GRP	BIT(6) /* AI only group-controls */
#define PCA963X_MODE1_AI_EN		BIT(7) /* enable Auto-Increment */

#define PCA963X_MODE2		0x01 /* mode 2 register addr */
#define PCA963X_MODE2_OUTNE_OUTDRV	BIT(0) /* outdrv determines LED state */
#define PCA963X_MODE2_OUTNE_HIZ		BIT(1) /* LED-state in Hi-Z */
#define PCA963X_MODE2_OUTDRV_TOTEM_POLE	BIT(2) /* outputs are totem-pole'd */
#define PCA963X_MODE2_OCH_ACK		BIT(3) /* out change on ACK else STOP */
#define PCA963X_MODE2_INVRT		BIT(4) /* output logic state inverted */
#define PCA963X_MODE2_DMBLNK		BIT(5) /* grp-ctrl blink else dimming */

#define PCA963X_PWM_ADDR(led)	(0x02 + (led))

#define PCA9633_GRPPWM		0x06 /* group PWM duty cycle ctrl for PCA9633 */
#define PCA9634_GRPPWM		0x0a /* group PWM duty cycle ctrl for PCA9634 */
#define PCA9635_GRPPWM		0x12 /* group PWM duty cycle ctrl for PCA9635 */
#define PCA9633_GRPFREQ		0x07 /* group frequency control for PCA9633 */
#define PCA9634_GRPFREQ		0x0b /* group frequency control for PCA9634 */
#define PCA9635_GRPFREQ		0x13 /* group frequency control for PCA9635 */

#define PCA9633_LEDOUT_BASE	0x08 /* LED output state 0 reg for PCA9633 */
#define PCA9634_LEDOUT_BASE	0x0c /* LED output state 0 reg for PCA9635 */
#define PCA9635_LEDOUT_BASE	0x14 /* LED output state 0 reg for PCA9634 */
#define PCA963X_LEDOUT_ADDR(ledout_base, led_num) \
	((ledout_base) + ((led_num) / 4))

#define PCA963X_LEDOUT_LED_OFF		0x0 /* LED off */
#define PCA963X_LEDOUT_LED_ON		0x1 /* LED on */
#define PCA963X_LEDOUT_LED_PWM		0x2 /* LED PWM mode */
#define PCA963X_LEDOUT_LED_GRP_PWM	0x3 /* LED PWM + group PWM mode */
#define PCA963X_LEDOUT_MASK		GENMASK(1, 0)

#define PCA963X_LEDOUT_LDR(drive, led_num)	\
	(((drive) & PCA963X_LEDOUT_MASK) << (((led_num) % 4) << 1))
#define PCA963X_LEDOUT_LDR_INV(drive, led_num) \
	(((drive) >> (((led_num) % 4) << 1)) & PCA963X_LEDOUT_MASK)

#define PCA9633_SUBADDR(x)	\
	((((x) - 1) % 0x3) + 0x09) /* I2C subaddr for PCA9633 */
#define PCA9634_SUBADDR(x)	\
	((((x) - 1) % 0x3) + 0x0e) /* I2C subaddr for PCA9634 */
#define PCA9635_SUBADDR(x)	\
	((((x) - 1) % 0x3) + 0x18) /* I2C subaddr for PCA9635 */
#define PCA963X_SUBADDR_SET(x)		(((x) << 1) & 0xfe)

#define PCA9633_ALLCALLADDR	0x0c /* I2C Led all call address for PCA9633 */
#define PCA9634_ALLCALLADDR	0x11 /* I2C Led all call address for PCA9634 */
#define PCA9635_ALLCALLADDR	0x1b /* I2C Led all call address for PCA9635 */
#define PCA963X_ALLCALLADDR_SET(x)	(((x) << 1) & 0xfe)

/* Software reset password */
#define PCA963X_PASSKEY1		0xa5
#define PCA963X_PASSKEY2		0x5a

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
		.grppwm		= PCA9633_GRPPWM,
		.grpfreq	= PCA9633_GRPFREQ,
		.ledout_base	= PCA9633_LEDOUT_BASE,
		.n_leds		= 4,
	},
	[pca9634] = {
		.grppwm		= PCA9634_GRPPWM,
		.grpfreq	= PCA9634_GRPFREQ,
		.ledout_base	= PCA9634_LEDOUT_BASE,
		.n_leds		= 8,
	},
	[pca9635] = {
		.grppwm		= PCA9635_GRPPWM,
		.grpfreq	= PCA9635_GRPFREQ,
		.ledout_base	= PCA9635_LEDOUT_BASE,
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

static const struct acpi_device_id pca963x_acpi_ids[] = {
	{ "PCA9632", pca9633 },
	{ "PCA9633", pca9633 },
	{ "PCA9634", pca9634 },
	{ "PCA9635", pca9635 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, pca963x_acpi_ids);

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
	char name[PCA963X_MAX_NAME_LEN];
	u8 gdc;
	u8 gfrq;
};

static int pca963x_brightness(struct pca963x_led *pca963x,
			       enum led_brightness brightness)
{
	u8 ledout_addr;
	u8 ledout;
	int ret;

	ledout_addr = PCA963X_LEDOUT_ADDR(pca963x->chip->chipdef->ledout_base,
					  pca963x->led_num);
	ledout = i2c_smbus_read_byte_data(pca963x->chip->client, ledout_addr);
	ledout &= ~PCA963X_LEDOUT_LDR(PCA963X_LEDOUT_MASK, pca963x->led_num);
	switch (brightness) {
	case LED_FULL:
		ledout |= PCA963X_LEDOUT_LDR(PCA963X_LEDOUT_LED_ON,
					     pca963x->led_num);
		break;
	case LED_OFF:
		ledout |= PCA963X_LEDOUT_LDR(PCA963X_LEDOUT_LED_OFF,
					     pca963x->led_num);
		break;
	default:
		ret = i2c_smbus_write_byte_data(pca963x->chip->client,
			PCA963X_PWM_ADDR(pca963x->led_num),
			brightness);
		if (ret < 0)
			return ret;
		ledout |= PCA963X_LEDOUT_LDR(PCA963X_LEDOUT_LED_PWM,
					     pca963x->led_num);
		break;
	}

	return i2c_smbus_write_byte_data(pca963x->chip->client, ledout_addr,
					 ledout);
}

static void pca963x_blink(struct pca963x_led *pca963x)
{
	u8 ledout_addr;
	u8 ledout;
	u8 mode2 = i2c_smbus_read_byte_data(pca963x->chip->client,
							PCA963X_MODE2);

	i2c_smbus_write_byte_data(pca963x->chip->client,
			pca963x->chip->chipdef->grppwm,	pca963x->gdc);

	i2c_smbus_write_byte_data(pca963x->chip->client,
			pca963x->chip->chipdef->grpfreq, pca963x->gfrq);

	if (!(mode2 & PCA963X_MODE2_DMBLNK))
		i2c_smbus_write_byte_data(pca963x->chip->client, PCA963X_MODE2,
			mode2 | PCA963X_MODE2_DMBLNK);

	ledout_addr = PCA963X_LEDOUT_ADDR(pca963x->chip->chipdef->ledout_base,
					  pca963x->led_num);
	mutex_lock(&pca963x->chip->mutex);
	ledout = i2c_smbus_read_byte_data(pca963x->chip->client, ledout_addr);
	if (PCA963X_LEDOUT_LDR_INV(ledout, pca963x->led_num) !=
	    PCA963X_LEDOUT_LED_GRP_PWM) {
		ledout |= PCA963X_LEDOUT_LDR(PCA963X_LEDOUT_LED_GRP_PWM,
					     pca963x->led_num);
		i2c_smbus_write_byte_data(pca963x->chip->client, ledout_addr,
					  ledout);
	}
	mutex_unlock(&pca963x->chip->mutex);
}

static int pca963x_power_state(struct pca963x_led *pca963x)
{
	unsigned long *leds_on = &pca963x->chip->leds_on;
	unsigned long cached_leds = pca963x->chip->leds_on;

	if (pca963x->led_cdev.brightness)
		set_bit(pca963x->led_num, leds_on);
	else
		clear_bit(pca963x->led_num, leds_on);

	if (!(*leds_on) != !cached_leds) {
		u8 mode1 = i2c_smbus_read_byte_data(pca963x->chip->client,
						    PCA963X_MODE1);

		if (*leds_on)
			mode1 &= ~PCA963X_MODE1_SLEEP;
		else
			mode1 |= PCA963X_MODE1_SLEEP;
		return i2c_smbus_write_byte_data(pca963x->chip->client,
			PCA963X_MODE1, mode1);
	}

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

#if IS_ENABLED(CONFIG_OF)
static struct pca963x_platform_data *
pca963x_dt_init(struct i2c_client *client, struct pca963x_chipdef *chip)
{
	struct device_node *np = client->dev.of_node, *child;
	struct pca963x_platform_data *pdata;
	struct led_info *pca963x_leds;
	int count;

	count = of_get_child_count(np);
	if (!count || count > chip->n_leds)
		return ERR_PTR(-ENODEV);

	pca963x_leds = devm_kzalloc(&client->dev,
			sizeof(struct led_info) * chip->n_leds, GFP_KERNEL);
	if (!pca963x_leds)
		return ERR_PTR(-ENOMEM);

	for_each_child_of_node(np, child) {
		struct led_info led = {};
		u32 reg;
		int res;

		res = of_property_read_u32(child, "reg", &reg);
		if ((res != 0) || (reg >= chip->n_leds))
			continue;
		led.name =
			of_get_property(child, "label", NULL) ? : child->name;
		led.default_trigger =
			of_get_property(child, "linux,default-trigger", NULL);
		pca963x_leds[reg] = led;
	}
	pdata = devm_kzalloc(&client->dev,
			     sizeof(struct pca963x_platform_data), GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	pdata->leds.leds = pca963x_leds;
	pdata->leds.num_leds = chip->n_leds;

	/* default to open-drain unless totem pole (push-pull) is specified */
	if (of_property_read_bool(np, "nxp,totem-pole"))
		pdata->outdrv = PCA963X_TOTEM_POLE;
	else
		pdata->outdrv = PCA963X_OPEN_DRAIN;

	/* default to software blinking unless hardware blinking is specified */
	if (of_property_read_bool(np, "nxp,hw-blink"))
		pdata->blink_type = PCA963X_HW_BLINK;
	else
		pdata->blink_type = PCA963X_SW_BLINK;

	if (of_property_read_u32(np, "nxp,period-scale", &chip->scaling))
		chip->scaling = 1000;

	/* default to non-inverted output, unless inverted is specified */
	if (of_property_read_bool(np, "nxp,inverted-out"))
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
#else
static struct pca963x_platform_data *
pca963x_dt_init(struct i2c_client *client, struct pca963x_chipdef *chip)
{
	return ERR_PTR(-ENODEV);
}
#endif

static int pca963x_probe(struct i2c_client *client,
					const struct i2c_device_id *id)
{
	struct pca963x *pca963x_chip;
	struct pca963x_led *pca963x;
	struct pca963x_platform_data *pdata;
	struct pca963x_chipdef *chip;
	int i, err;

	if (id) {
		chip = &pca963x_chipdefs[id->driver_data];
	} else {
		const struct acpi_device_id *acpi_id;

		acpi_id = acpi_match_device(pca963x_acpi_ids, &client->dev);
		if (!acpi_id)
			return -ENODEV;
		chip = &pca963x_chipdefs[acpi_id->driver_data];
	}
	pdata = dev_get_platdata(&client->dev);

	if (!pdata) {
		pdata = pca963x_dt_init(client, chip);
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

	pca963x_chip = devm_kzalloc(&client->dev, sizeof(*pca963x_chip),
								GFP_KERNEL);
	if (!pca963x_chip)
		return -ENOMEM;
	pca963x = devm_kzalloc(&client->dev, chip->n_leds * sizeof(*pca963x),
								GFP_KERNEL);
	if (!pca963x)
		return -ENOMEM;

	/* Check if chip actually exists on the bus */
	err = i2c_smbus_read_byte_data(client, PCA963X_MODE1);
	if (err < 0) {
		dev_err(&client->dev, "controller not found");
		return err;
	}

	i2c_set_clientdata(client, pca963x_chip);

	mutex_init(&pca963x_chip->mutex);
	pca963x_chip->chipdef = chip;
	pca963x_chip->client = client;
	pca963x_chip->leds = pca963x;

	/* Turn off LEDs by default*/
	for (i = 0; i < chip->n_leds / 4; i++)
		i2c_smbus_write_byte_data(client, chip->ledout_base + i,
				PCA963X_LEDOUT_LDR(PCA963X_LEDOUT_LED_OFF, i));

	for (i = 0; i < chip->n_leds; i++) {
		pca963x[i].led_num = i;
		pca963x[i].chip = pca963x_chip;

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

		err = led_classdev_register(&client->dev, &pca963x[i].led_cdev);
		if (err < 0)
			goto exit;
	}

	/* Disable LED all-call address, and power down initially */
	i2c_smbus_write_byte_data(client, PCA963X_MODE1, PCA963X_MODE1_SLEEP);

	if (pdata) {
		u8 mode2 = i2c_smbus_read_byte_data(pca963x->chip->client,
						    PCA963X_MODE2);
		/* Configure output: open-drain or totem pole (push-pull) */
		if (pdata->outdrv == PCA963X_OPEN_DRAIN)
			mode2 |= PCA963X_MODE2_OUTNE_OUTDRV;
		else
			mode2 |= PCA963X_MODE2_OUTNE_OUTDRV |
				 PCA963X_MODE2_OUTDRV_TOTEM_POLE;
		/* Configure direction: normal or inverted */
		if (pdata->dir == PCA963X_INVERTED)
			mode2 |= PCA963X_MODE2_INVRT;
		i2c_smbus_write_byte_data(pca963x->chip->client, PCA963X_MODE2,
					  mode2);
	}

	return 0;

exit:
	while (i--)
		led_classdev_unregister(&pca963x[i].led_cdev);

	return err;
}

static int pca963x_remove(struct i2c_client *client)
{
	struct pca963x *pca963x = i2c_get_clientdata(client);
	int i;

	for (i = 0; i < pca963x->chipdef->n_leds; i++)
		led_classdev_unregister(&pca963x->leds[i].led_cdev);

	return 0;
}

static struct i2c_driver pca963x_driver = {
	.driver = {
		.name	= "leds-pca963x",
		.of_match_table = of_match_ptr(of_pca963x_match),
		.acpi_match_table = ACPI_PTR(pca963x_acpi_ids),
	},
	.probe	= pca963x_probe,
	.remove	= pca963x_remove,
	.id_table = pca963x_id,
};

module_i2c_driver(pca963x_driver);

MODULE_AUTHOR("Peter Meerwald <p.meerwald@bct-electronic.com>");
MODULE_DESCRIPTION("PCA963X LED driver");
MODULE_LICENSE("GPL v2");
