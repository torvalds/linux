/*
 * drivers/input/touchscreen/sx865x.c
 *
 * Copyright (c) 2013 U-MoBo Srl
 *      Pierluigi Passaro <p.passaro@u-mobo.com>
 *
 * Using code from:
 *  - sx8650.c
 *      Copyright (c) 2009 Wayne Roberts
 *  - tsc2007.c
 *      Copyright (c) 2008 Kwangwoo Lee
 *  - ads7846.c
 *      Copyright (c) 2005 David Brownell
 *      Copyright (c) 2006 Nokia Corporation
 *  - corgi_ts.c
 *      Copyright (C) 2004-2005 Richard Purdie
 *  - omap_ts.[hc], ads7846.h, ts_osk.c
 *      Copyright (C) 2002 MontaVista Software
 *      Copyright (C) 2004 Texas Instruments
 *      Copyright (C) 2005 Dirk Behme
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include <linux/delay.h>

#if defined(CONFIG_MACH_MESON8B_ODROIDC)

#include <linux/amlogic/aml_gpio_consumer.h>

#endif

/* timeout expires after pen is lifted, no more PENIRQs comming */
/* adjust with POWDLY setting */
#define TS_TIMEOUT		(8 * 1000000)

/* analog channels */
#define CH_X			0
#define CH_Y			1
#define CH_Z1			2
#define CH_Z2			3
#define CH_AUX			4
#define CH_RX			5
#define CH_RY			6
#define CH_SEQ			7

/* commands */
#define SX865X_WRITE_REGISTER	0x00
#define SX865X_READ_REGISTER	0x40
#define SX865X_SELECT_CH(ch)	(0x80 | ch)
#define SX865X_CONVERT_CH(ch)	(0x90 | ch)
#define SX865X_POWDWN		0xb0	/* power down, ignore pen */
#define SX865X_PENDET		0xc0	/* " " with pen sensitivity */
#define SX865X_PENTRG		0xe0	/* " " " " and sample channels */

/* register addresses */
#define I2C_REG_CTRL0		0x00
#define I2C_REG_CTRL1		0x01
#define I2C_REG_CTRL2		0x02
#define I2C_REG_CTRL3		0x03
#define I2C_REG_CHANMASK	0x04
#define I2C_REG_STAT		0x05
#define I2C_REG_SOFTRESET	0x1f

#define I2C_EXTENDED_REG_STAT		0x24
#define I2C_EXTENDED_REG_SOFTRESET	0x3f

#define SOFTRESET_VALUE		0xde

/* bits for I2C_REG_STAT */
/* I2C_REG_STAT: end of conversion flag */
#define STATUS_CONVIRQ		0x80
/* I2C_REG_STAT: pen detected */
#define STATUS_PENIRQ		0x40

/* bits for I2C_EXTENDED_REG_STAT */
/* I2C_EXTENDED_REG_STAT: end of conversion flag */
#define EXTENDED_STATUS_CONVIRQ	0x08
/* I2C_EXTENDED_REG_STAT: pen detected */
#define EXTENDED_STATUS_PENIRQ	0x04

/* sx865x bits for RegCtrl1 */
#define CONDIRQ			0x20
/* no averaging */
#define FILT_NONE		0x00
/* 3 sample averaging */
#define FILT_3SA		0x01
/* 5 sample averaging */
#define FILT_5SA		0x02
/* 7 samples, sort, then average of 3 middle samples */
#define FILT_7SA		0x03

/* bits for register 2, I2CRegChanMsk */
#define CONV_X			0x80
#define CONV_Y			0x40
#define CONV_Z1			0x20
#define CONV_Z2			0x10
#define CONV_AUX		0x08
#define CONV_RX			0x04
#define CONV_RY			0x02

/* power delay: lower nibble of CTRL0 register */
#define POWDLY_IMMEDIATE	0x00
#define POWDLY_1_1US		0x01
#define POWDLY_2_2US		0x02
#define POWDLY_4_4US		0x03
#define POWDLY_8_9US		0x04
#define POWDLY_17_8US		0x05
#define POWDLY_35_5US		0x06
#define POWDLY_71US		0x07
#define POWDLY_140US		0x08
#define POWDLY_280US		0x09
#define POWDLY_570US		0x0a
#define POWDLY_1_1MS		0x0b
#define POWDLY_2_3MS		0x0c
#define POWDLY_4_6MS		0x0d
#define POWDLY_9MS		0x0e
#define POWDLY_18MS		0x0f

#define MAX_12BIT		((1 << 12) - 1)

/* when changing the channel mask, also change the read length appropriately */
#define CHAN_MASK		(CONV_X | CONV_Y | CONV_Z1 | CONV_RX | CONV_RY)
#define NUM_CHANNELS_SEQ	5
#define CHAN_READ_LENGTH	(NUM_CHANNELS_SEQ * 2)

#define SX_MULTITOUCH		0x01
#define SX_PROXIMITY_SENSING	0x02
#define SX_HAPTICS_GENERIC	0x04
#define SX_HAPTICS_IMMERSION	0x08
#define SX_EXTENDED_REGS	(SX_PROXIMITY_SENSING | SX_HAPTICS_GENERIC | SX_HAPTICS_IMMERSION)

#define SX865X_UP_SCANTIME_MS	(100)
#define SX865X_DOWN_SCANTIME_MS	(20)

struct ts_event {
	u16 x, y;
	u16 z1;
	u16 rx, ry;
};

struct sx865x {
	struct input_dev *input;
	struct ts_event tc;

	struct i2c_client *client;

	u32 invert_x;
	u32 invert_y;
	u32 swap_xy;
	u32 gpio_pendown;
	u32 gpio_reset;

#if defined(CONFIG_MACH_MESON8B_ODROIDC)
	int irq_bank;
#endif
	unsigned pendown;
	int irq;
};

static struct i2c_device_id sx865x_idtable[] = {
	{ "sx8650", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, sx865x_idtable);

static const struct of_device_id sx865x_of_match[] = {
	{ .compatible = "semtech,sx8650", .data = (void *)0 },
	{}
};

MODULE_DEVICE_TABLE(of, sx865x_of_match);

static void sx865x_send_event(struct sx865x *ts)
{
	u32 rt;
	u16 x, y, z1;

	x  = ts->tc.x;
	y  = ts->tc.y;
	z1 = ts->tc.z1;

	/* range filtering */
	if (y == MAX_12BIT)
		y = 0;

	/* compute touch pressure resistance */
	if (likely(y && z1))
		rt = z1;
	else
		rt = 0;

	/* Sample found inconsistent by debouncing or pressure is beyond
	 * the maximum. Don't report it to user space, repeat at least
	 * once more the measurement
	 */
	if (rt > MAX_12BIT) {
		dev_dbg(&ts->client->dev, "ignored pressure %d\n", rt);
		return;
	}

	/* NOTE: We can't rely on the pressure to determine the pen down
	 * state, even this controller has a pressure sensor. The pressure
	 * value can fluctuate for quite a while after lifting the pen and
	 * in some cases may not even settle at the expected value.
	 *
	 * The only safe way to check for the pen up condition is in the
	 * timer by reading the pen signal state (it's a GPIO _and_ IRQ).
	 */
	if (rt) {
		struct input_dev *input = ts->input;

		if (ts->invert_x)	x = (~x) & MAX_12BIT;

		if (ts->invert_y)	y = (~y) & MAX_12BIT;

		if (ts->swap_xy)	swap(x, y);

		if (!ts->pendown) {
			dev_dbg(&ts->client->dev, "DOWN\n");
			ts->pendown = 1;
			input_report_key(input, BTN_TOUCH, 1);
		}

		input_report_abs(input, ABS_X, x);
		input_report_abs(input, ABS_Y, y);
		input_report_abs(input, ABS_PRESSURE, rt);
		input_sync(input);

		dev_dbg(&ts->client->dev, "point(%4d,%4d), pressure (%4u)\n",
			x, y, rt);
	}
}

static int sx865x_read_values(struct sx865x *ts)
{
	s32 data;
	u16 vals[NUM_CHANNELS_SEQ+1];	/* +1 for last dummy read */
	int length;
	int i;

	memset(&(ts->tc), 0, sizeof(ts->tc));
	/* The protocol and raw data format from i2c interface:
	 * S Addr R A [DataLow] A [DataHigh] A (repeat) NA P
	 * Where DataLow has (channel | [D11-D8]), DataHigh has [D7-D0].
	 */
	length = i2c_master_recv(ts->client, (char *)vals, CHAN_READ_LENGTH);

	if (likely(length == CHAN_READ_LENGTH)) {
		length >>= 1;
		for (i = 0; i < length; i++) {
			u16 ch;
			data = swab16(vals[i]);
			if (unlikely(data & 0x8000)) {
				dev_dbg(&ts->client->dev,
					"hibit @ %d [0x%04x]\n", i, data);
				continue;
			}
			ch = data >> 12;
			if        (ch == CH_X) {
				ts->tc.x = data & 0xfff;
			} else if (ch == CH_Y) {
				ts->tc.y = data & 0xfff;
			} else if (ch == CH_Z1) {
				ts->tc.z1 = data & 0xfff;
			} else if (ch == CH_RX) {
				ts->tc.rx = data & 0xfff;
			} else if (ch == CH_RY) {
				ts->tc.ry = data & 0xfff;
			} else {
				dev_err(&ts->client->dev, "? CH%d %x\n",
					ch, data & 0xfff);
			}
		}
	} else {
		dev_err(&ts->client->dev, "%d = recv()\n", length);
	}

	dev_dbg(&ts->client->dev, "X:%03x Y:%03x Z1:%03x RX:%03x RY:%03x\n",
		ts->tc.x, ts->tc.y, ts->tc.z1, ts->tc.rx, ts->tc.ry);

	return !ts->tc.z1;	/* return 0 only if pressure not 0 */
}

static void sx865x_pen_up(struct sx865x *ts)
{
	struct input_dev *input = ts->input;

	/* This timer expires after PENIRQs havent been coming in for some time.
	 * It means that the pen is now UP. */
	input_report_key(input, BTN_TOUCH, 0);
	input_report_abs(input, ABS_PRESSURE, 0);
	input_sync(input);

	ts->pendown = 0;
	dev_dbg(&ts->client->dev, "UP\n");
}

static int sx865x_data_available(struct sx865x *ts)
{
	u8 status;

	status = i2c_smbus_read_byte_data(ts->client,
					(SX865X_READ_REGISTER | I2C_REG_STAT));
	return status & STATUS_CONVIRQ;
}

static int get_pendown_status(struct sx865x *ts)
{
	return	gpio_get_value(ts->gpio_pendown) ? 0 : 1;
}

static irqreturn_t sx865x_hw_irq(int irq, void *handle)
{
	struct sx865x *ts = handle;

	return get_pendown_status(ts) ? IRQ_WAKE_THREAD : IRQ_HANDLED;
}

static irqreturn_t sx865x_irq(int irq, void *handle)
{
	struct sx865x *ts = handle;

	while (sx865x_data_available(ts)) {
		/* valid data was read in */
		if (likely(sx865x_read_values(ts) == 0))
			sx865x_send_event(ts);
		else
			dev_dbg(&ts->client->dev, "data error!\n");

		msleep(SX865X_DOWN_SCANTIME_MS);
	}

	if (ts->pendown)
		sx865x_pen_up(ts);

	return IRQ_HANDLED;
}

static void sx865x_hw_reset(struct sx865x *ts)
{
	gpio_direction_output(ts->gpio_reset, 0);
	udelay(1000);
	gpio_direction_output(ts->gpio_reset, 1);
	udelay(1000);
}

static int sx865x_dt_probe(struct i2c_client *client, struct sx865x *ts)
{
	struct device_node *node = client->dev.of_node;
	const struct of_device_id *match;

	if (!node) {
		dev_err(&client->dev,
			"Device dost not have associated DT data\n");
		return -EINVAL;
	}

	match = of_match_device(sx865x_of_match, &client->dev);
	if (!match) {
		dev_err(&client->dev,
			"Unknown device model\n");
		return -EINVAL;
	}

	of_property_read_u32(node, "swap-xy",	   &ts->swap_xy);
	of_property_read_u32(node, "invert-x",	   &ts->invert_x);
	of_property_read_u32(node, "invert-y",	   &ts->invert_y);

#if defined(CONFIG_MACH_MESON8B_ODROIDC)
	{
		const char  *str;
		int err;

		if(of_property_read_string(node, "gpio-pendown", &str)) {
			dev_err(&client->dev,
				"Unknown ts-pendown\n");
			return -EINVAL;
		}
		ts->gpio_pendown = amlogic_gpio_name_map_num(str);

		err = amlogic_gpio_request_one(ts->gpio_pendown, GPIOF_IN,
						"ts-pendown");
		if (err) {
			dev_err(&client->dev,
				"failed to request/setup pendown GPIO%d: %d\n",
				ts->gpio_pendown, err);
			return -EINVAL;
		}

		if(of_property_read_string(node, "gpio-reset", &str)) {
			dev_err(&client->dev,
				"Unknown ts-reset\n");
			return -EINVAL;
		}
		ts->gpio_reset = amlogic_gpio_name_map_num(str);

		err = amlogic_gpio_request_one(ts->gpio_reset, GPIOF_OUT_INIT_LOW,
						"ts-reset");
		if (err) {
			dev_err(&client->dev,
				"failed to request/setup reset GPIO%d: %d\n",
				ts->gpio_reset, err);
			return -EINVAL;
		}
		sx865x_hw_reset(ts);
	}

#else
	ts->gpio_pendown = of_get_named_gpio(node, "gpio-pendown", 0);
	ts->gpio_reset   = of_get_named_gpio(node, "gpio-reset", 0);

	if (gpio_request(ts->gpio_pendown, "ts-pendown"))
		dev_err(&client->dev,
			"gpio request fail (%d)!\n", ts->gpio_pendown);
	else
		gpio_direction_input(ts->gpio_pendown);

	if (gpio_request(ts->gpio_reset, "ts-reset"))
		dev_err(&client->dev,
			"gpio request fail (%d)!\n", ts->gpio_reset);
	else
		sx865x_hw_reset(ts);
#endif

#if defined(CONFIG_MACH_MESON8B_ODROIDC)
	/* irq setup */
	ts->irq_bank = meson_fix_irqbank(ts->irq_bank);
	if (ts->irq_bank < 0) {
		dev_err(&client->dev,
			"Could not find irq bank!\n");
		return -EINVAL;
	}

	{
		int ret;
		/* AMLogic gpio irq setup */
		ret = amlogic_gpio_to_irq(ts->gpio_pendown, "ts-pendown",
			AML_GPIO_IRQ(ts->irq_bank, FILTER_NUM7, GPIO_IRQ_FALLING));

		if (ret) {
			dev_err(&client->dev,
				"AML_GPIO_IRQ setup fail!\n");
			return -EINVAL;
		}
		/* Amlogic gpio based irq setup */
		ts->irq = INT_GPIO_0 + ts->irq_bank;
	}
#else
	ts->irq = gpio_to_irq(ts->gpio_pendown);
	if (ts->irq < 0)
		return -EINVAL;
#endif

	/* platform data info display */
	dev_info(&client->dev, "swap_xy (%d)\n", 	ts->swap_xy);
	dev_info(&client->dev, "invert_x (%d)\n", 	ts->invert_x);
	dev_info(&client->dev, "invert_y (%d)\n", 	ts->invert_y);
	dev_info(&client->dev, "gpio pendown (%d)\n",	ts->gpio_pendown);
	dev_info(&client->dev, "gpio reset (%d)\n",	ts->gpio_reset);
	dev_info(&client->dev, "gpio irq (%d)\n",	ts->irq);

	return 0;
}

#if defined(CONFIG_MACH_MESON8B_ODROIDC)
static void sx865x_irq_free(struct i2c_client *client, struct sx865x *ts)
{
	int	irq_banks[2];

	meson_free_irq(ts->gpio_pendown, &irq_banks[0]);

	/* rising irq bank */
	if (irq_banks[0] != -1)
		free_irq(irq_banks[0] + INT_GPIO_0, ts);

	/* falling irq bank */
	if (irq_banks[1] != -1)
		free_irq(irq_banks[1] + INT_GPIO_0, ts);
}
#endif

static int sx865x_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct sx865x *ts;
	struct input_dev *input_dev;
	int err = 0;

	dev_info(&client->dev, "sx865x_probe()\n");

	if (!i2c_check_functionality(client->adapter,
					I2C_FUNC_SMBUS_READ_WORD_DATA))
		return -EIO;

	ts = devm_kzalloc(&client->dev, sizeof(struct sx865x), GFP_KERNEL);
	input_dev = devm_input_allocate_device(&client->dev);
	if (!ts || !input_dev)
		return -ENOMEM;

	if (sx865x_dt_probe(client, ts) != 0)
		return -EIO;

	i2c_set_clientdata(client, ts);

	input_dev->name 	= "SX865X Touchscreen";
	input_dev->id.bustype 	= BUS_I2C;
	input_dev->dev.parent 	= &client->dev;
	input_set_drvdata(input_dev, ts);

	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);

	input_set_abs_params(input_dev, ABS_X, 0, MAX_12BIT, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, MAX_12BIT, 0, 0);
	input_set_abs_params(input_dev, ABS_PRESSURE, 0, MAX_12BIT, 0, 0);

	/* soft reset: SX8650 fails to nak at the end, ignore return value */
	i2c_smbus_write_byte_data(client, I2C_REG_SOFTRESET, SOFTRESET_VALUE);

	/* set mask to convert X, Y, Z1, RX, RY for CH_SEQ */
	err = i2c_smbus_write_byte_data(client, I2C_REG_CHANMASK, CHAN_MASK);
	if (err != 0)	return -EIO;

	err = i2c_smbus_write_byte_data(client, I2C_REG_CTRL1,
					CONDIRQ | FILT_7SA);
	if (err != 0)	return -EIO;

	/* set POWDLY settling time -- adjust TS_TIMEOUT accordingly */
	err = i2c_smbus_write_byte_data(client, I2C_REG_CTRL0, POWDLY_1_1MS);
	if (err != 0)	return -EIO;

	/* enter pen-trigger mode */
	err = i2c_smbus_write_byte(client, SX865X_PENTRG);
	if (err != 0)	return -EIO;

	err = request_threaded_irq(ts->irq, sx865x_hw_irq, sx865x_irq,
			IRQF_DISABLED | IRQF_ONESHOT,
			client->dev.driver->name, ts);

	if (err < 0) {
		dev_err(&client->dev, "irq %d busy?\n", ts->irq);
		return -EIO;
	}

	err = input_register_device(input_dev);
	if (err)
		goto err_free_irq;

	ts->client = client;
	ts->input = input_dev;

	dev_info(&client->dev, "probe ok! registered with irq (%d)\n", ts->irq);

	return 0;

err_free_irq:
	if (ts->gpio_pendown)
		gpio_free(ts->gpio_pendown);
	if (ts->gpio_reset)
		gpio_free(ts->gpio_reset);
#if defined(CONFIG_MACH_MESON8B_ODROIDC)
	sx865x_irq_free(client, ts);
#else
	if (ts->irq)
		free_irq(ts->irq, ts);
#endif
	return err;
}

static int sx865x_remove(struct i2c_client *client)
{
	struct sx865x *ts = i2c_get_clientdata(client);
	struct sx865x_platform_data *pdata;

	pdata = client->dev.platform_data;

	if (ts->gpio_pendown)
		gpio_free(ts->gpio_pendown);
	if (ts->gpio_reset)
		gpio_free(ts->gpio_reset);
#if defined(CONFIG_MACH_MESON8B_ODROIDC)
	sx865x_irq_free(client, ts);
#else
	if (ts->irq)
		free_irq(ts->irq, ts);
#endif
	input_unregister_device(ts->input);

	return 0;
}

static struct i2c_driver sx865x_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "sx865x",
		.of_match_table = of_match_ptr(sx865x_of_match),
	},
	.id_table = sx865x_idtable,
	.probe = sx865x_probe,
	.remove = sx865x_remove,
};

module_i2c_driver(sx865x_driver);

MODULE_AUTHOR("Pierluigi Passaro <info@phoenixsoftware.it>");
MODULE_DESCRIPTION("SX865X TouchScreen Driver");
MODULE_LICENSE("GPL");
