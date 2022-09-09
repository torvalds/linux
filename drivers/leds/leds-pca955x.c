// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2007-2008 Extreme Engineering Solutions, Inc.
 *
 * Author: Nate Case <ncase@xes-inc.com>
 *
 * LED driver for various PCA955x I2C LED drivers
 *
 * Supported devices:
 *
 *	Device		Description		7-bit slave address
 *	------		-----------		-------------------
 *	PCA9550		2-bit driver		0x60 .. 0x61
 *	PCA9551		8-bit driver		0x60 .. 0x67
 *	PCA9552		16-bit driver		0x60 .. 0x67
 *	PCA9553/01	4-bit driver		0x62
 *	PCA9553/02	4-bit driver		0x63
 *
 * Philips PCA955x LED driver chips follow a register map as shown below:
 *
 *	Control Register		Description
 *	----------------		-----------
 *	0x0				Input register 0
 *					..
 *	NUM_INPUT_REGS - 1		Last Input register X
 *
 *	NUM_INPUT_REGS			Frequency prescaler 0
 *	NUM_INPUT_REGS + 1		PWM register 0
 *	NUM_INPUT_REGS + 2		Frequency prescaler 1
 *	NUM_INPUT_REGS + 3		PWM register 1
 *
 *	NUM_INPUT_REGS + 4		LED selector 0
 *	NUM_INPUT_REGS + 4
 *	    + NUM_LED_REGS - 1		Last LED selector
 *
 *  where NUM_INPUT_REGS and NUM_LED_REGS vary depending on how many
 *  bits the chip supports.
 */

#include <linux/bitops.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio/driver.h>
#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/string.h>

#include <dt-bindings/leds/leds-pca955x.h>

/* LED select registers determine the source that drives LED outputs */
#define PCA955X_LS_LED_ON	0x0	/* Output LOW */
#define PCA955X_LS_LED_OFF	0x1	/* Output HI-Z */
#define PCA955X_LS_BLINK0	0x2	/* Blink at PWM0 rate */
#define PCA955X_LS_BLINK1	0x3	/* Blink at PWM1 rate */

#define PCA955X_GPIO_INPUT	LED_OFF
#define PCA955X_GPIO_HIGH	LED_OFF
#define PCA955X_GPIO_LOW	LED_FULL

enum pca955x_type {
	pca9550,
	pca9551,
	pca9552,
	ibm_pca9552,
	pca9553,
};

struct pca955x_chipdef {
	int			bits;
	u8			slv_addr;	/* 7-bit slave address mask */
	int			slv_addr_shift;	/* Number of bits to ignore */
};

static struct pca955x_chipdef pca955x_chipdefs[] = {
	[pca9550] = {
		.bits		= 2,
		.slv_addr	= /* 110000x */ 0x60,
		.slv_addr_shift	= 1,
	},
	[pca9551] = {
		.bits		= 8,
		.slv_addr	= /* 1100xxx */ 0x60,
		.slv_addr_shift	= 3,
	},
	[pca9552] = {
		.bits		= 16,
		.slv_addr	= /* 1100xxx */ 0x60,
		.slv_addr_shift	= 3,
	},
	[ibm_pca9552] = {
		.bits		= 16,
		.slv_addr	= /* 0110xxx */ 0x30,
		.slv_addr_shift	= 3,
	},
	[pca9553] = {
		.bits		= 4,
		.slv_addr	= /* 110001x */ 0x62,
		.slv_addr_shift	= 1,
	},
};

static const struct i2c_device_id pca955x_id[] = {
	{ "pca9550", pca9550 },
	{ "pca9551", pca9551 },
	{ "pca9552", pca9552 },
	{ "ibm-pca9552", ibm_pca9552 },
	{ "pca9553", pca9553 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, pca955x_id);

struct pca955x {
	struct mutex lock;
	struct pca955x_led *leds;
	struct pca955x_chipdef	*chipdef;
	struct i2c_client	*client;
	unsigned long active_pins;
#ifdef CONFIG_LEDS_PCA955X_GPIO
	struct gpio_chip gpio;
#endif
};

struct pca955x_led {
	struct pca955x	*pca955x;
	struct led_classdev	led_cdev;
	int			led_num;	/* 0 .. 15 potentially */
	u32			type;
	int			default_state;
	struct fwnode_handle	*fwnode;
};

struct pca955x_platform_data {
	struct pca955x_led	*leds;
	int			num_leds;
};

/* 8 bits per input register */
static inline int pca95xx_num_input_regs(int bits)
{
	return (bits + 7) / 8;
}

/* 4 bits per LED selector register */
static inline int pca95xx_num_led_regs(int bits)
{
	return (bits + 3)  / 4;
}

/*
 * Return an LED selector register value based on an existing one, with
 * the appropriate 2-bit state value set for the given LED number (0-3).
 */
static inline u8 pca955x_ledsel(u8 oldval, int led_num, int state)
{
	return (oldval & (~(0x3 << (led_num << 1)))) |
		((state & 0x3) << (led_num << 1));
}

/*
 * Write to frequency prescaler register, used to program the
 * period of the PWM output.  period = (PSCx + 1) / 38
 */
static int pca955x_write_psc(struct i2c_client *client, int n, u8 val)
{
	struct pca955x *pca955x = i2c_get_clientdata(client);
	u8 cmd = pca95xx_num_input_regs(pca955x->chipdef->bits) + (2 * n);
	int ret;

	ret = i2c_smbus_write_byte_data(client, cmd, val);
	if (ret < 0)
		dev_err(&client->dev, "%s: reg 0x%x, val 0x%x, err %d\n",
			__func__, n, val, ret);
	return ret;
}

/*
 * Write to PWM register, which determines the duty cycle of the
 * output.  LED is OFF when the count is less than the value of this
 * register, and ON when it is greater.  If PWMx == 0, LED is always OFF.
 *
 * Duty cycle is (256 - PWMx) / 256
 */
static int pca955x_write_pwm(struct i2c_client *client, int n, u8 val)
{
	struct pca955x *pca955x = i2c_get_clientdata(client);
	u8 cmd = pca95xx_num_input_regs(pca955x->chipdef->bits) + 1 + (2 * n);
	int ret;

	ret = i2c_smbus_write_byte_data(client, cmd, val);
	if (ret < 0)
		dev_err(&client->dev, "%s: reg 0x%x, val 0x%x, err %d\n",
			__func__, n, val, ret);
	return ret;
}

/*
 * Write to LED selector register, which determines the source that
 * drives the LED output.
 */
static int pca955x_write_ls(struct i2c_client *client, int n, u8 val)
{
	struct pca955x *pca955x = i2c_get_clientdata(client);
	u8 cmd = pca95xx_num_input_regs(pca955x->chipdef->bits) + 4 + n;
	int ret;

	ret = i2c_smbus_write_byte_data(client, cmd, val);
	if (ret < 0)
		dev_err(&client->dev, "%s: reg 0x%x, val 0x%x, err %d\n",
			__func__, n, val, ret);
	return ret;
}

/*
 * Read the LED selector register, which determines the source that
 * drives the LED output.
 */
static int pca955x_read_ls(struct i2c_client *client, int n, u8 *val)
{
	struct pca955x *pca955x = i2c_get_clientdata(client);
	u8 cmd = pca95xx_num_input_regs(pca955x->chipdef->bits) + 4 + n;
	int ret;

	ret = i2c_smbus_read_byte_data(client, cmd);
	if (ret < 0) {
		dev_err(&client->dev, "%s: reg 0x%x, err %d\n",
			__func__, n, ret);
		return ret;
	}
	*val = (u8)ret;
	return 0;
}

static int pca955x_read_pwm(struct i2c_client *client, int n, u8 *val)
{
	struct pca955x *pca955x = i2c_get_clientdata(client);
	u8 cmd = pca95xx_num_input_regs(pca955x->chipdef->bits) + 1 + (2 * n);
	int ret;

	ret = i2c_smbus_read_byte_data(client, cmd);
	if (ret < 0) {
		dev_err(&client->dev, "%s: reg 0x%x, err %d\n",
			__func__, n, ret);
		return ret;
	}
	*val = (u8)ret;
	return 0;
}

static enum led_brightness pca955x_led_get(struct led_classdev *led_cdev)
{
	struct pca955x_led *pca955x_led = container_of(led_cdev,
						       struct pca955x_led,
						       led_cdev);
	struct pca955x *pca955x = pca955x_led->pca955x;
	u8 ls, pwm;
	int ret;

	ret = pca955x_read_ls(pca955x->client, pca955x_led->led_num / 4, &ls);
	if (ret)
		return ret;

	ls = (ls >> ((pca955x_led->led_num % 4) << 1)) & 0x3;
	switch (ls) {
	case PCA955X_LS_LED_ON:
		ret = LED_FULL;
		break;
	case PCA955X_LS_LED_OFF:
		ret = LED_OFF;
		break;
	case PCA955X_LS_BLINK0:
		ret = LED_HALF;
		break;
	case PCA955X_LS_BLINK1:
		ret = pca955x_read_pwm(pca955x->client, 1, &pwm);
		if (ret)
			return ret;
		ret = 255 - pwm;
		break;
	}

	return ret;
}

static int pca955x_led_set(struct led_classdev *led_cdev,
			    enum led_brightness value)
{
	struct pca955x_led *pca955x_led;
	struct pca955x *pca955x;
	u8 ls;
	int chip_ls;	/* which LSx to use (0-3 potentially) */
	int ls_led;	/* which set of bits within LSx to use (0-3) */
	int ret;

	pca955x_led = container_of(led_cdev, struct pca955x_led, led_cdev);
	pca955x = pca955x_led->pca955x;

	chip_ls = pca955x_led->led_num / 4;
	ls_led = pca955x_led->led_num % 4;

	mutex_lock(&pca955x->lock);

	ret = pca955x_read_ls(pca955x->client, chip_ls, &ls);
	if (ret)
		goto out;

	switch (value) {
	case LED_FULL:
		ls = pca955x_ledsel(ls, ls_led, PCA955X_LS_LED_ON);
		break;
	case LED_OFF:
		ls = pca955x_ledsel(ls, ls_led, PCA955X_LS_LED_OFF);
		break;
	case LED_HALF:
		ls = pca955x_ledsel(ls, ls_led, PCA955X_LS_BLINK0);
		break;
	default:
		/*
		 * Use PWM1 for all other values.  This has the unwanted
		 * side effect of making all LEDs on the chip share the
		 * same brightness level if set to a value other than
		 * OFF, HALF, or FULL.  But, this is probably better than
		 * just turning off for all other values.
		 */
		ret = pca955x_write_pwm(pca955x->client, 1, 255 - value);
		if (ret)
			goto out;
		ls = pca955x_ledsel(ls, ls_led, PCA955X_LS_BLINK1);
		break;
	}

	ret = pca955x_write_ls(pca955x->client, chip_ls, ls);

out:
	mutex_unlock(&pca955x->lock);

	return ret;
}

#ifdef CONFIG_LEDS_PCA955X_GPIO
/*
 * Read the INPUT register, which contains the state of LEDs.
 */
static int pca955x_read_input(struct i2c_client *client, int n, u8 *val)
{
	int ret = i2c_smbus_read_byte_data(client, n);

	if (ret < 0) {
		dev_err(&client->dev, "%s: reg 0x%x, err %d\n",
			__func__, n, ret);
		return ret;
	}
	*val = (u8)ret;
	return 0;

}

static int pca955x_gpio_request_pin(struct gpio_chip *gc, unsigned int offset)
{
	struct pca955x *pca955x = gpiochip_get_data(gc);

	return test_and_set_bit(offset, &pca955x->active_pins) ? -EBUSY : 0;
}

static void pca955x_gpio_free_pin(struct gpio_chip *gc, unsigned int offset)
{
	struct pca955x *pca955x = gpiochip_get_data(gc);

	clear_bit(offset, &pca955x->active_pins);
}

static int pca955x_set_value(struct gpio_chip *gc, unsigned int offset,
			     int val)
{
	struct pca955x *pca955x = gpiochip_get_data(gc);
	struct pca955x_led *led = &pca955x->leds[offset];

	if (val)
		return pca955x_led_set(&led->led_cdev, PCA955X_GPIO_HIGH);

	return pca955x_led_set(&led->led_cdev, PCA955X_GPIO_LOW);
}

static void pca955x_gpio_set_value(struct gpio_chip *gc, unsigned int offset,
				   int val)
{
	pca955x_set_value(gc, offset, val);
}

static int pca955x_gpio_get_value(struct gpio_chip *gc, unsigned int offset)
{
	struct pca955x *pca955x = gpiochip_get_data(gc);
	struct pca955x_led *led = &pca955x->leds[offset];
	u8 reg = 0;

	/* There is nothing we can do about errors */
	pca955x_read_input(pca955x->client, led->led_num / 8, &reg);

	return !!(reg & (1 << (led->led_num % 8)));
}

static int pca955x_gpio_direction_input(struct gpio_chip *gc,
					unsigned int offset)
{
	struct pca955x *pca955x = gpiochip_get_data(gc);
	struct pca955x_led *led = &pca955x->leds[offset];

	/* To use as input ensure pin is not driven. */
	return pca955x_led_set(&led->led_cdev, PCA955X_GPIO_INPUT);
}

static int pca955x_gpio_direction_output(struct gpio_chip *gc,
					 unsigned int offset, int val)
{
	return pca955x_set_value(gc, offset, val);
}
#endif /* CONFIG_LEDS_PCA955X_GPIO */

static struct pca955x_platform_data *
pca955x_get_pdata(struct i2c_client *client, struct pca955x_chipdef *chip)
{
	struct pca955x_platform_data *pdata;
	struct pca955x_led *led;
	struct fwnode_handle *child;
	int count;

	count = device_get_child_node_count(&client->dev);
	if (count > chip->bits)
		return ERR_PTR(-ENODEV);

	pdata = devm_kzalloc(&client->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	pdata->leds = devm_kcalloc(&client->dev,
				   chip->bits, sizeof(struct pca955x_led),
				   GFP_KERNEL);
	if (!pdata->leds)
		return ERR_PTR(-ENOMEM);

	device_for_each_child_node(&client->dev, child) {
		const char *state;
		u32 reg;
		int res;

		res = fwnode_property_read_u32(child, "reg", &reg);
		if ((res != 0) || (reg >= chip->bits))
			continue;

		led = &pdata->leds[reg];
		led->type = PCA955X_TYPE_LED;
		led->fwnode = child;
		fwnode_property_read_u32(child, "type", &led->type);

		if (!fwnode_property_read_string(child, "default-state",
						 &state)) {
			if (!strcmp(state, "keep"))
				led->default_state = LEDS_GPIO_DEFSTATE_KEEP;
			else if (!strcmp(state, "on"))
				led->default_state = LEDS_GPIO_DEFSTATE_ON;
			else
				led->default_state = LEDS_GPIO_DEFSTATE_OFF;
		} else {
			led->default_state = LEDS_GPIO_DEFSTATE_OFF;
		}
	}

	pdata->num_leds = chip->bits;

	return pdata;
}

static const struct of_device_id of_pca955x_match[] = {
	{ .compatible = "nxp,pca9550", .data = (void *)pca9550 },
	{ .compatible = "nxp,pca9551", .data = (void *)pca9551 },
	{ .compatible = "nxp,pca9552", .data = (void *)pca9552 },
	{ .compatible = "ibm,pca9552", .data = (void *)ibm_pca9552 },
	{ .compatible = "nxp,pca9553", .data = (void *)pca9553 },
	{},
};
MODULE_DEVICE_TABLE(of, of_pca955x_match);

static int pca955x_probe(struct i2c_client *client)
{
	struct pca955x *pca955x;
	struct pca955x_led *pca955x_led;
	struct pca955x_chipdef *chip;
	struct led_classdev *led;
	struct led_init_data init_data;
	struct i2c_adapter *adapter;
	int i, err;
	struct pca955x_platform_data *pdata;
	bool set_default_label = false;
	bool keep_pwm = false;
	char default_label[8];
	enum pca955x_type chip_type;
	const void *md = device_get_match_data(&client->dev);

	if (md) {
		chip_type = (enum pca955x_type)md;
	} else {
		const struct i2c_device_id *id = i2c_match_id(pca955x_id,
							      client);

		if (id) {
			chip_type = (enum pca955x_type)id->driver_data;
		} else {
			dev_err(&client->dev, "unknown chip\n");
			return -ENODEV;
		}
	}

	chip = &pca955x_chipdefs[chip_type];
	adapter = client->adapter;
	pdata = dev_get_platdata(&client->dev);
	if (!pdata) {
		pdata =	pca955x_get_pdata(client, chip);
		if (IS_ERR(pdata))
			return PTR_ERR(pdata);
	}

	/* Make sure the slave address / chip type combo given is possible */
	if ((client->addr & ~((1 << chip->slv_addr_shift) - 1)) !=
	    chip->slv_addr) {
		dev_err(&client->dev, "invalid slave address %02x\n",
			client->addr);
		return -ENODEV;
	}

	dev_info(&client->dev, "leds-pca955x: Using %s %d-bit LED driver at "
		 "slave address 0x%02x\n", client->name, chip->bits,
		 client->addr);

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	if (pdata->num_leds != chip->bits) {
		dev_err(&client->dev,
			"board info claims %d LEDs on a %d-bit chip\n",
			pdata->num_leds, chip->bits);
		return -ENODEV;
	}

	pca955x = devm_kzalloc(&client->dev, sizeof(*pca955x), GFP_KERNEL);
	if (!pca955x)
		return -ENOMEM;

	pca955x->leds = devm_kcalloc(&client->dev, chip->bits,
				     sizeof(*pca955x_led), GFP_KERNEL);
	if (!pca955x->leds)
		return -ENOMEM;

	i2c_set_clientdata(client, pca955x);

	mutex_init(&pca955x->lock);
	pca955x->client = client;
	pca955x->chipdef = chip;

	init_data.devname_mandatory = false;
	init_data.devicename = "pca955x";

	for (i = 0; i < chip->bits; i++) {
		pca955x_led = &pca955x->leds[i];
		pca955x_led->led_num = i;
		pca955x_led->pca955x = pca955x;
		pca955x_led->type = pdata->leds[i].type;

		switch (pca955x_led->type) {
		case PCA955X_TYPE_NONE:
		case PCA955X_TYPE_GPIO:
			break;
		case PCA955X_TYPE_LED:
			led = &pca955x_led->led_cdev;
			led->brightness_set_blocking = pca955x_led_set;
			led->brightness_get = pca955x_led_get;

			if (pdata->leds[i].default_state ==
			    LEDS_GPIO_DEFSTATE_OFF) {
				err = pca955x_led_set(led, LED_OFF);
				if (err)
					return err;
			} else if (pdata->leds[i].default_state ==
				   LEDS_GPIO_DEFSTATE_ON) {
				err = pca955x_led_set(led, LED_FULL);
				if (err)
					return err;
			}

			init_data.fwnode = pdata->leds[i].fwnode;

			if (is_of_node(init_data.fwnode)) {
				if (to_of_node(init_data.fwnode)->name[0] ==
				    '\0')
					set_default_label = true;
				else
					set_default_label = false;
			} else {
				set_default_label = true;
			}

			if (set_default_label) {
				snprintf(default_label, sizeof(default_label),
					 "%d", i);
				init_data.default_label = default_label;
			} else {
				init_data.default_label = NULL;
			}

			err = devm_led_classdev_register_ext(&client->dev, led,
							     &init_data);
			if (err)
				return err;

			set_bit(i, &pca955x->active_pins);

			/*
			 * For default-state == "keep", let the core update the
			 * brightness from the hardware, then check the
			 * brightness to see if it's using PWM1. If so, PWM1
			 * should not be written below.
			 */
			if (pdata->leds[i].default_state ==
			    LEDS_GPIO_DEFSTATE_KEEP) {
				if (led->brightness != LED_FULL &&
				    led->brightness != LED_OFF &&
				    led->brightness != LED_HALF)
					keep_pwm = true;
			}
		}
	}

	/* PWM0 is used for half brightness or 50% duty cycle */
	err = pca955x_write_pwm(client, 0, 255 - LED_HALF);
	if (err)
		return err;

	if (!keep_pwm) {
		/* PWM1 is used for variable brightness, default to OFF */
		err = pca955x_write_pwm(client, 1, 0);
		if (err)
			return err;
	}

	/* Set to fast frequency so we do not see flashing */
	err = pca955x_write_psc(client, 0, 0);
	if (err)
		return err;
	err = pca955x_write_psc(client, 1, 0);
	if (err)
		return err;

#ifdef CONFIG_LEDS_PCA955X_GPIO
	pca955x->gpio.label = "gpio-pca955x";
	pca955x->gpio.direction_input = pca955x_gpio_direction_input;
	pca955x->gpio.direction_output = pca955x_gpio_direction_output;
	pca955x->gpio.set = pca955x_gpio_set_value;
	pca955x->gpio.get = pca955x_gpio_get_value;
	pca955x->gpio.request = pca955x_gpio_request_pin;
	pca955x->gpio.free = pca955x_gpio_free_pin;
	pca955x->gpio.can_sleep = 1;
	pca955x->gpio.base = -1;
	pca955x->gpio.ngpio = chip->bits;
	pca955x->gpio.parent = &client->dev;
	pca955x->gpio.owner = THIS_MODULE;

	err = devm_gpiochip_add_data(&client->dev, &pca955x->gpio,
				     pca955x);
	if (err) {
		/* Use data->gpio.dev as a flag for freeing gpiochip */
		pca955x->gpio.parent = NULL;
		dev_warn(&client->dev, "could not add gpiochip\n");
		return err;
	}
	dev_info(&client->dev, "gpios %i...%i\n",
		 pca955x->gpio.base, pca955x->gpio.base +
		 pca955x->gpio.ngpio - 1);
#endif

	return 0;
}

static struct i2c_driver pca955x_driver = {
	.driver = {
		.name	= "leds-pca955x",
		.of_match_table = of_pca955x_match,
	},
	.probe_new = pca955x_probe,
	.id_table = pca955x_id,
};

module_i2c_driver(pca955x_driver);

MODULE_AUTHOR("Nate Case <ncase@xes-inc.com>");
MODULE_DESCRIPTION("PCA955x LED driver");
MODULE_LICENSE("GPL v2");
