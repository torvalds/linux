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

#define PCA955X_BLINK_DEFAULT_MS	1000

enum pca955x_type {
	pca9550,
	pca9551,
	pca9552,
	ibm_pca9552,
	pca9553,
};

struct pca955x_chipdef {
	u8			bits;
	u8			slv_addr;	/* 7-bit slave address mask */
	int			slv_addr_shift;	/* Number of bits to ignore */
	int			blink_div;	/* PSC divider */
};

static const struct pca955x_chipdef pca955x_chipdefs[] = {
	[pca9550] = {
		.bits		= 2,
		.slv_addr	= /* 110000x */ 0x60,
		.slv_addr_shift	= 1,
		.blink_div	= 44,
	},
	[pca9551] = {
		.bits		= 8,
		.slv_addr	= /* 1100xxx */ 0x60,
		.slv_addr_shift	= 3,
		.blink_div	= 38,
	},
	[pca9552] = {
		.bits		= 16,
		.slv_addr	= /* 1100xxx */ 0x60,
		.slv_addr_shift	= 3,
		.blink_div	= 44,
	},
	[ibm_pca9552] = {
		.bits		= 16,
		.slv_addr	= /* 0110xxx */ 0x30,
		.slv_addr_shift	= 3,
		.blink_div	= 44,
	},
	[pca9553] = {
		.bits		= 4,
		.slv_addr	= /* 110001x */ 0x62,
		.slv_addr_shift	= 1,
		.blink_div	= 44,
	},
};

struct pca955x {
	struct mutex lock;
	struct pca955x_led *leds;
	const struct pca955x_chipdef	*chipdef;
	struct i2c_client	*client;
	unsigned long active_blink;
	unsigned long active_pins;
	unsigned long blink_period;
#ifdef CONFIG_LEDS_PCA955X_GPIO
	struct gpio_chip gpio;
#endif
};

struct pca955x_led {
	struct pca955x	*pca955x;
	struct led_classdev	led_cdev;
	int			led_num;	/* 0 .. 15 potentially */
	u32			type;
	enum led_default_state	default_state;
	struct fwnode_handle	*fwnode;
};

#define led_to_pca955x(l)	container_of(l, struct pca955x_led, led_cdev)

struct pca955x_platform_data {
	struct pca955x_led	*leds;
	int			num_leds;
};

/* 8 bits per input register */
static inline u8 pca955x_num_input_regs(u8 bits)
{
	return (bits + 7) / 8;
}

/* 4 bits per LED selector register */
static inline u8 pca955x_num_led_regs(u8 bits)
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

static inline int pca955x_ledstate(u8 ls, int led_num)
{
	return (ls >> (led_num << 1)) & 0x3;
}

/*
 * Write to frequency prescaler register, used to program the
 * period of the PWM output.  period = (PSCx + 1) / coeff
 * Where for pca9551 chips coeff = 38 and for all other chips coeff = 44
 */
static int pca955x_write_psc(struct pca955x *pca955x, int n, u8 val)
{
	u8 cmd = pca955x_num_input_regs(pca955x->chipdef->bits) + (2 * n);
	int ret;

	ret = i2c_smbus_write_byte_data(pca955x->client, cmd, val);
	if (ret < 0)
		dev_err(&pca955x->client->dev, "%s: reg 0x%x, val 0x%x, err %d\n", __func__, n,
			val, ret);
	return ret;
}

/*
 * Write to PWM register, which determines the duty cycle of the
 * output.  LED is OFF when the count is less than the value of this
 * register, and ON when it is greater.  If PWMx == 0, LED is always OFF.
 *
 * Duty cycle is (256 - PWMx) / 256
 */
static int pca955x_write_pwm(struct pca955x *pca955x, int n, u8 val)
{
	u8 cmd = pca955x_num_input_regs(pca955x->chipdef->bits) + 1 + (2 * n);
	int ret;

	ret = i2c_smbus_write_byte_data(pca955x->client, cmd, val);
	if (ret < 0)
		dev_err(&pca955x->client->dev, "%s: reg 0x%x, val 0x%x, err %d\n", __func__, n,
			val, ret);
	return ret;
}

/*
 * Write to LED selector register, which determines the source that
 * drives the LED output.
 */
static int pca955x_write_ls(struct pca955x *pca955x, int n, u8 val)
{
	u8 cmd = pca955x_num_input_regs(pca955x->chipdef->bits) + 4 + n;
	int ret;

	ret = i2c_smbus_write_byte_data(pca955x->client, cmd, val);
	if (ret < 0)
		dev_err(&pca955x->client->dev, "%s: reg 0x%x, val 0x%x, err %d\n", __func__, n,
			val, ret);
	return ret;
}

/*
 * Read the LED selector register, which determines the source that
 * drives the LED output.
 */
static int pca955x_read_ls(struct pca955x *pca955x, int n, u8 *val)
{
	u8 cmd = pca955x_num_input_regs(pca955x->chipdef->bits) + 4 + n;
	int ret;

	ret = i2c_smbus_read_byte_data(pca955x->client, cmd);
	if (ret < 0) {
		dev_err(&pca955x->client->dev, "%s: reg 0x%x, err %d\n", __func__, n, ret);
		return ret;
	}
	*val = (u8)ret;
	return 0;
}

static int pca955x_read_pwm(struct pca955x *pca955x, int n, u8 *val)
{
	u8 cmd = pca955x_num_input_regs(pca955x->chipdef->bits) + 1 + (2 * n);
	int ret;

	ret = i2c_smbus_read_byte_data(pca955x->client, cmd);
	if (ret < 0) {
		dev_err(&pca955x->client->dev, "%s: reg 0x%x, err %d\n", __func__, n, ret);
		return ret;
	}
	*val = (u8)ret;
	return 0;
}

static int pca955x_read_psc(struct pca955x *pca955x, int n, u8 *val)
{
	int ret;
	u8 cmd;

	cmd = pca955x_num_input_regs(pca955x->chipdef->bits) + (2 * n);
	ret = i2c_smbus_read_byte_data(pca955x->client, cmd);
	if (ret < 0) {
		dev_err(&pca955x->client->dev, "%s: reg 0x%x, err %d\n", __func__, n, ret);
		return ret;
	}
	*val = (u8)ret;
	return 0;
}

static enum led_brightness pca955x_led_get(struct led_classdev *led_cdev)
{
	struct pca955x_led *pca955x_led = led_to_pca955x(led_cdev);
	struct pca955x *pca955x = pca955x_led->pca955x;
	u8 ls, pwm;
	int ret;

	ret = pca955x_read_ls(pca955x, pca955x_led->led_num / 4, &ls);
	if (ret)
		return ret;

	switch (pca955x_ledstate(ls, pca955x_led->led_num % 4)) {
	case PCA955X_LS_LED_ON:
	case PCA955X_LS_BLINK0:
		ret = LED_FULL;
		break;
	case PCA955X_LS_LED_OFF:
		ret = LED_OFF;
		break;
	case PCA955X_LS_BLINK1:
		ret = pca955x_read_pwm(pca955x, 1, &pwm);
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
	struct pca955x_led *pca955x_led = led_to_pca955x(led_cdev);
	struct pca955x *pca955x = pca955x_led->pca955x;
	int reg = pca955x_led->led_num / 4;
	int bit = pca955x_led->led_num % 4;
	u8 ls;
	int ret;

	mutex_lock(&pca955x->lock);

	ret = pca955x_read_ls(pca955x, reg, &ls);
	if (ret)
		goto out;

	if (test_bit(pca955x_led->led_num, &pca955x->active_blink)) {
		if (value == LED_OFF) {
			clear_bit(pca955x_led->led_num, &pca955x->active_blink);
			ls = pca955x_ledsel(ls, bit, PCA955X_LS_LED_OFF);
		} else {
			/* No variable brightness for blinking LEDs */
			goto out;
		}
	} else {
		switch (value) {
		case LED_FULL:
			ls = pca955x_ledsel(ls, bit, PCA955X_LS_LED_ON);
			break;
		case LED_OFF:
			ls = pca955x_ledsel(ls, bit, PCA955X_LS_LED_OFF);
			break;
		default:
			/*
			 * Use PWM1 for all other values. This has the unwanted
			 * side effect of making all LEDs on the chip share the
			 * same brightness level if set to a value other than
			 * OFF or FULL. But, this is probably better than just
			 * turning off for all other values.
			 */
			ret = pca955x_write_pwm(pca955x, 1, 255 - value);
			if (ret)
				goto out;
			ls = pca955x_ledsel(ls, bit, PCA955X_LS_BLINK1);
			break;
		}
	}

	ret = pca955x_write_ls(pca955x, reg, ls);

out:
	mutex_unlock(&pca955x->lock);

	return ret;
}

static u8 pca955x_period_to_psc(struct pca955x *pca955x, unsigned long period)
{
	/* psc register value = (blink period * coeff) - 1 */
	period *= pca955x->chipdef->blink_div;
	period /= MSEC_PER_SEC;
	period -= 1;

	return period;
}

static unsigned long pca955x_psc_to_period(struct pca955x *pca955x, u8 psc)
{
	unsigned long period = psc;

	/* blink period = (psc register value + 1) / coeff */
	period += 1;
	period *= MSEC_PER_SEC;
	period /= pca955x->chipdef->blink_div;

	return period;
}

static int pca955x_led_blink(struct led_classdev *led_cdev,
			     unsigned long *delay_on, unsigned long *delay_off)
{
	struct pca955x_led *pca955x_led = led_to_pca955x(led_cdev);
	struct pca955x *pca955x = pca955x_led->pca955x;
	unsigned long period = *delay_on + *delay_off;
	int ret = 0;

	mutex_lock(&pca955x->lock);

	if (period) {
		if (*delay_on != *delay_off) {
			ret = -EINVAL;
			goto out;
		}

		if (period < pca955x_psc_to_period(pca955x, 0) ||
		    period > pca955x_psc_to_period(pca955x, 0xff)) {
			ret = -EINVAL;
			goto out;
		}
	} else {
		period = pca955x->active_blink ? pca955x->blink_period :
			PCA955X_BLINK_DEFAULT_MS;
	}

	if (!pca955x->active_blink ||
	    pca955x->active_blink == BIT(pca955x_led->led_num) ||
	    pca955x->blink_period == period) {
		u8 psc = pca955x_period_to_psc(pca955x, period);

		if (!test_and_set_bit(pca955x_led->led_num,
				      &pca955x->active_blink)) {
			u8 ls;
			int reg = pca955x_led->led_num / 4;
			int bit = pca955x_led->led_num % 4;

			ret = pca955x_read_ls(pca955x, reg, &ls);
			if (ret)
				goto out;

			ls = pca955x_ledsel(ls, bit, PCA955X_LS_BLINK0);
			ret = pca955x_write_ls(pca955x, reg, ls);
			if (ret)
				goto out;

			/*
			 * Force 50% duty cycle to maintain the specified
			 * blink rate.
			 */
			ret = pca955x_write_pwm(pca955x, 0, 128);
			if (ret)
				goto out;
		}

		if (pca955x->blink_period != period) {
			pca955x->blink_period = period;
			ret = pca955x_write_psc(pca955x, 0, psc);
			if (ret)
				goto out;
		}

		period = pca955x_psc_to_period(pca955x, psc);
		period /= 2;
		*delay_on = period;
		*delay_off = period;
	} else {
		ret = -EBUSY;
	}

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

static int pca955x_gpio_set_value(struct gpio_chip *gc, unsigned int offset,
				  int val)
{
	return pca955x_set_value(gc, offset, val);
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
pca955x_get_pdata(struct i2c_client *client, const struct pca955x_chipdef *chip)
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
		u32 reg;
		int res;

		res = fwnode_property_read_u32(child, "reg", &reg);
		if ((res != 0) || (reg >= chip->bits))
			continue;

		led = &pdata->leds[reg];
		led->type = PCA955X_TYPE_LED;
		led->fwnode = child;
		led->default_state = led_init_default_state_get(child);

		fwnode_property_read_u32(child, "type", &led->type);
	}

	pdata->num_leds = chip->bits;

	return pdata;
}

static int pca955x_probe(struct i2c_client *client)
{
	struct pca955x *pca955x;
	struct pca955x_led *pca955x_led;
	const struct pca955x_chipdef *chip;
	struct led_classdev *led;
	struct led_init_data init_data;
	struct i2c_adapter *adapter;
	u8 i, nls, psc0;
	u8 ls1[4];
	u8 ls2[4];
	struct pca955x_platform_data *pdata;
	bool keep_psc0 = false;
	bool set_default_label = false;
	char default_label[4];
	int bit, err, reg;

	chip = i2c_get_match_data(client);
	if (!chip)
		return dev_err_probe(&client->dev, -ENODEV, "unknown chip\n");

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

	dev_info(&client->dev, "Using %s %u-bit LED driver at slave address 0x%02x\n",
		 client->name, chip->bits, client->addr);

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	if (pdata->num_leds != chip->bits) {
		dev_err(&client->dev,
			"board info claims %d LEDs on a %u-bit chip\n",
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
	pca955x->blink_period = PCA955X_BLINK_DEFAULT_MS;

	init_data.devname_mandatory = false;
	init_data.devicename = "pca955x";

	nls = pca955x_num_led_regs(chip->bits);
	/* Use auto-increment feature to read all the LED selectors at once. */
	err = i2c_smbus_read_i2c_block_data(client,
					    0x10 | (pca955x_num_input_regs(chip->bits) + 4), nls,
					    ls1);
	if (err < 0)
		return err;

	for (i = 0; i < nls; i++)
		ls2[i] = ls1[i];

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
			bit = i % 4;
			reg = i / 4;
			led = &pca955x_led->led_cdev;
			led->brightness_set_blocking = pca955x_led_set;
			led->brightness_get = pca955x_led_get;
			led->blink_set = pca955x_led_blink;

			if (pdata->leds[i].default_state == LEDS_DEFSTATE_OFF)
				ls2[reg] = pca955x_ledsel(ls2[reg], bit, PCA955X_LS_LED_OFF);
			else if (pdata->leds[i].default_state == LEDS_DEFSTATE_ON)
				ls2[reg] = pca955x_ledsel(ls2[reg], bit, PCA955X_LS_LED_ON);
			else if (pca955x_ledstate(ls2[reg], bit) == PCA955X_LS_BLINK0) {
				keep_psc0 = true;
				set_bit(i, &pca955x->active_blink);
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
				snprintf(default_label, sizeof(default_label), "%hhu", i);
				init_data.default_label = default_label;
			} else {
				init_data.default_label = NULL;
			}

			err = devm_led_classdev_register_ext(&client->dev, led,
							     &init_data);
			if (err)
				return err;

			set_bit(i, &pca955x->active_pins);
		}
	}

	for (i = 0; i < nls; i++) {
		if (ls1[i] != ls2[i]) {
			err = pca955x_write_ls(pca955x, i, ls2[i]);
			if (err)
				return err;
		}
	}

	if (keep_psc0) {
		err = pca955x_read_psc(pca955x, 0, &psc0);
	} else {
		psc0 = pca955x_period_to_psc(pca955x, pca955x->blink_period);
		err = pca955x_write_psc(pca955x, 0, psc0);
	}

	if (err)
		return err;

	pca955x->blink_period = pca955x_psc_to_period(pca955x, psc0);

	/* Set PWM1 to fast frequency so we do not see flashing */
	err = pca955x_write_psc(pca955x, 1, 0);
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

static const struct i2c_device_id pca955x_id[] = {
	{ "pca9550", (kernel_ulong_t)&pca955x_chipdefs[pca9550] },
	{ "pca9551", (kernel_ulong_t)&pca955x_chipdefs[pca9551] },
	{ "pca9552", (kernel_ulong_t)&pca955x_chipdefs[pca9552] },
	{ "ibm-pca9552", (kernel_ulong_t)&pca955x_chipdefs[ibm_pca9552] },
	{ "pca9553", (kernel_ulong_t)&pca955x_chipdefs[pca9553] },
	{}
};
MODULE_DEVICE_TABLE(i2c, pca955x_id);

static const struct of_device_id of_pca955x_match[] = {
	{ .compatible = "nxp,pca9550", .data = &pca955x_chipdefs[pca9550] },
	{ .compatible = "nxp,pca9551", .data = &pca955x_chipdefs[pca9551] },
	{ .compatible = "nxp,pca9552", .data = &pca955x_chipdefs[pca9552] },
	{ .compatible = "ibm,pca9552", .data = &pca955x_chipdefs[ibm_pca9552] },
	{ .compatible = "nxp,pca9553", .data = &pca955x_chipdefs[pca9553] },
	{}
};
MODULE_DEVICE_TABLE(of, of_pca955x_match);

static struct i2c_driver pca955x_driver = {
	.driver = {
		.name	= "leds-pca955x",
		.of_match_table = of_pca955x_match,
	},
	.probe = pca955x_probe,
	.id_table = pca955x_id,
};

module_i2c_driver(pca955x_driver);

MODULE_AUTHOR("Nate Case <ncase@xes-inc.com>");
MODULE_DESCRIPTION("PCA955x LED driver");
MODULE_LICENSE("GPL v2");
