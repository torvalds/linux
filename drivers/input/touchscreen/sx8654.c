// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Semtech SX8654 I2C touchscreen controller.
 *
 * Copyright (c) 2015 Armadeus Systems
 *	Sébastien Szymanski <sebastien.szymanski@armadeus.com>
 *
 * Using code from:
 *  - sx865x.c
 *	Copyright (c) 2013 U-MoBo Srl
 *	Pierluigi Passaro <p.passaro@u-mobo.com>
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
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/touchscreen.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of.h>

/* register addresses */
#define I2C_REG_TOUCH0			0x00
#define I2C_REG_TOUCH1			0x01
#define I2C_REG_CHANMASK		0x04
#define I2C_REG_IRQMASK			0x22
#define I2C_REG_IRQSRC			0x23
#define I2C_REG_SOFTRESET		0x3f

#define I2C_REG_SX8650_STAT		0x05
#define SX8650_STAT_CONVIRQ		BIT(7)

/* commands */
#define CMD_READ_REGISTER		0x40
#define CMD_PENTRG			0xe0

/* value for I2C_REG_SOFTRESET */
#define SOFTRESET_VALUE			0xde

/* bits for I2C_REG_IRQSRC */
#define IRQ_PENTOUCH_TOUCHCONVDONE	BIT(3)
#define IRQ_PENRELEASE			BIT(2)

/* bits for RegTouch1 */
#define CONDIRQ				0x20
#define RPDNT_100K			0x00
#define FILT_7SA			0x03

/* bits for I2C_REG_CHANMASK */
#define CONV_X				BIT(7)
#define CONV_Y				BIT(6)

/* coordinates rate: higher nibble of CTRL0 register */
#define RATE_MANUAL			0x00
#define RATE_5000CPS			0xf0

/* power delay: lower nibble of CTRL0 register */
#define POWDLY_1_1MS			0x0b

/* for sx8650, as we have no pen release IRQ there: timeout in ns following the
 * last PENIRQ after which we assume the pen is lifted.
 */
#define SX8650_PENIRQ_TIMEOUT		msecs_to_jiffies(10)

#define MAX_12BIT			((1 << 12) - 1)
#define MAX_I2C_READ_LEN		10 /* see datasheet section 5.1.5 */

/* channel definition */
#define CH_X				0x00
#define CH_Y				0x01

struct sx865x_data {
	u8 cmd_manual;
	u8 chan_mask;
	bool has_irq_penrelease;
	bool has_reg_irqmask;
	irq_handler_t irqh;
};

struct sx8654 {
	struct input_dev *input;
	struct i2c_client *client;
	struct gpio_desc *gpio_reset;

	spinlock_t lock;	/* for input reporting from irq/timer */
	struct timer_list timer;

	struct touchscreen_properties props;

	const struct sx865x_data *data;
};

static inline void sx865x_penrelease(struct sx8654 *ts)
{
	struct input_dev *input_dev = ts->input;

	input_report_key(input_dev, BTN_TOUCH, 0);
	input_sync(input_dev);
}

static void sx865x_penrelease_timer_handler(struct timer_list *t)
{
	struct sx8654 *ts = from_timer(ts, t, timer);
	unsigned long flags;

	spin_lock_irqsave(&ts->lock, flags);
	sx865x_penrelease(ts);
	spin_unlock_irqrestore(&ts->lock, flags);
	dev_dbg(&ts->client->dev, "penrelease by timer\n");
}

static irqreturn_t sx8650_irq(int irq, void *handle)
{
	struct sx8654 *ts = handle;
	struct device *dev = &ts->client->dev;
	int len, i;
	unsigned long flags;
	u8 stat;
	u16 x, y;
	u16 ch;
	u16 chdata;
	__be16 data[MAX_I2C_READ_LEN / sizeof(__be16)];
	u8 nchan = hweight32(ts->data->chan_mask);
	u8 readlen = nchan * sizeof(*data);

	stat = i2c_smbus_read_byte_data(ts->client, CMD_READ_REGISTER
						    | I2C_REG_SX8650_STAT);

	if (!(stat & SX8650_STAT_CONVIRQ)) {
		dev_dbg(dev, "%s ignore stat [0x%02x]", __func__, stat);
		return IRQ_HANDLED;
	}

	len = i2c_master_recv(ts->client, (u8 *)data, readlen);
	if (len != readlen) {
		dev_dbg(dev, "ignore short recv (%d)\n", len);
		return IRQ_HANDLED;
	}

	spin_lock_irqsave(&ts->lock, flags);

	x = 0;
	y = 0;
	for (i = 0; i < nchan; i++) {
		chdata = be16_to_cpu(data[i]);

		if (unlikely(chdata == 0xFFFF)) {
			dev_dbg(dev, "invalid qualified data @ %d\n", i);
			continue;
		} else if (unlikely(chdata & 0x8000)) {
			dev_warn(dev, "hibit @ %d [0x%04x]\n", i, chdata);
			continue;
		}

		ch = chdata >> 12;
		if (ch == CH_X)
			x = chdata & MAX_12BIT;
		else if (ch == CH_Y)
			y = chdata & MAX_12BIT;
		else
			dev_warn(dev, "unknown channel %d [0x%04x]\n", ch,
				 chdata);
	}

	touchscreen_report_pos(ts->input, &ts->props, x, y, false);
	input_report_key(ts->input, BTN_TOUCH, 1);
	input_sync(ts->input);
	dev_dbg(dev, "point(%4d,%4d)\n", x, y);

	mod_timer(&ts->timer, jiffies + SX8650_PENIRQ_TIMEOUT);
	spin_unlock_irqrestore(&ts->lock, flags);

	return IRQ_HANDLED;
}

static irqreturn_t sx8654_irq(int irq, void *handle)
{
	struct sx8654 *sx8654 = handle;
	int irqsrc;
	u8 data[4];
	unsigned int x, y;
	int retval;

	irqsrc = i2c_smbus_read_byte_data(sx8654->client,
					  CMD_READ_REGISTER | I2C_REG_IRQSRC);
	dev_dbg(&sx8654->client->dev, "irqsrc = 0x%x", irqsrc);

	if (irqsrc < 0)
		goto out;

	if (irqsrc & IRQ_PENRELEASE) {
		dev_dbg(&sx8654->client->dev, "pen release interrupt");

		input_report_key(sx8654->input, BTN_TOUCH, 0);
		input_sync(sx8654->input);
	}

	if (irqsrc & IRQ_PENTOUCH_TOUCHCONVDONE) {
		dev_dbg(&sx8654->client->dev, "pen touch interrupt");

		retval = i2c_master_recv(sx8654->client, data, sizeof(data));
		if (retval != sizeof(data))
			goto out;

		/* invalid data */
		if (unlikely(data[0] & 0x80 || data[2] & 0x80))
			goto out;

		x = ((data[0] & 0xf) << 8) | (data[1]);
		y = ((data[2] & 0xf) << 8) | (data[3]);

		touchscreen_report_pos(sx8654->input, &sx8654->props, x, y,
				       false);
		input_report_key(sx8654->input, BTN_TOUCH, 1);
		input_sync(sx8654->input);

		dev_dbg(&sx8654->client->dev, "point(%4d,%4d)\n", x, y);
	}

out:
	return IRQ_HANDLED;
}

static int sx8654_reset(struct sx8654 *ts)
{
	int err;

	if (ts->gpio_reset) {
		gpiod_set_value_cansleep(ts->gpio_reset, 1);
		udelay(2); /* Tpulse > 1µs */
		gpiod_set_value_cansleep(ts->gpio_reset, 0);
	} else {
		dev_dbg(&ts->client->dev, "NRST unavailable, try softreset\n");
		err = i2c_smbus_write_byte_data(ts->client, I2C_REG_SOFTRESET,
						SOFTRESET_VALUE);
		if (err)
			return err;
	}

	return 0;
}

static int sx8654_open(struct input_dev *dev)
{
	struct sx8654 *sx8654 = input_get_drvdata(dev);
	struct i2c_client *client = sx8654->client;
	int error;

	/* enable pen trigger mode */
	error = i2c_smbus_write_byte_data(client, I2C_REG_TOUCH0,
					  RATE_5000CPS | POWDLY_1_1MS);
	if (error) {
		dev_err(&client->dev, "writing to I2C_REG_TOUCH0 failed");
		return error;
	}

	error = i2c_smbus_write_byte(client, CMD_PENTRG);
	if (error) {
		dev_err(&client->dev, "writing command CMD_PENTRG failed");
		return error;
	}

	enable_irq(client->irq);

	return 0;
}

static void sx8654_close(struct input_dev *dev)
{
	struct sx8654 *sx8654 = input_get_drvdata(dev);
	struct i2c_client *client = sx8654->client;
	int error;

	disable_irq(client->irq);

	if (!sx8654->data->has_irq_penrelease)
		del_timer_sync(&sx8654->timer);

	/* enable manual mode mode */
	error = i2c_smbus_write_byte(client, sx8654->data->cmd_manual);
	if (error) {
		dev_err(&client->dev, "writing command CMD_MANUAL failed");
		return;
	}

	error = i2c_smbus_write_byte_data(client, I2C_REG_TOUCH0, RATE_MANUAL);
	if (error) {
		dev_err(&client->dev, "writing to I2C_REG_TOUCH0 failed");
		return;
	}
}

static int sx8654_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct sx8654 *sx8654;
	struct input_dev *input;
	int error;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_READ_WORD_DATA))
		return -ENXIO;

	sx8654 = devm_kzalloc(&client->dev, sizeof(*sx8654), GFP_KERNEL);
	if (!sx8654)
		return -ENOMEM;

	sx8654->gpio_reset = devm_gpiod_get_optional(&client->dev, "reset",
						     GPIOD_OUT_HIGH);
	if (IS_ERR(sx8654->gpio_reset)) {
		error = PTR_ERR(sx8654->gpio_reset);
		if (error != -EPROBE_DEFER)
			dev_err(&client->dev, "unable to get reset-gpio: %d\n",
				error);
		return error;
	}
	dev_dbg(&client->dev, "got GPIO reset pin\n");

	sx8654->data = device_get_match_data(&client->dev);
	if (!sx8654->data)
		sx8654->data = (const struct sx865x_data *)id->driver_data;
	if (!sx8654->data) {
		dev_err(&client->dev, "invalid or missing device data\n");
		return -EINVAL;
	}

	if (!sx8654->data->has_irq_penrelease) {
		dev_dbg(&client->dev, "use timer for penrelease\n");
		timer_setup(&sx8654->timer, sx865x_penrelease_timer_handler, 0);
		spin_lock_init(&sx8654->lock);
	}

	input = devm_input_allocate_device(&client->dev);
	if (!input)
		return -ENOMEM;

	input->name = "SX8654 I2C Touchscreen";
	input->id.bustype = BUS_I2C;
	input->dev.parent = &client->dev;
	input->open = sx8654_open;
	input->close = sx8654_close;

	__set_bit(INPUT_PROP_DIRECT, input->propbit);
	input_set_capability(input, EV_KEY, BTN_TOUCH);
	input_set_abs_params(input, ABS_X, 0, MAX_12BIT, 0, 0);
	input_set_abs_params(input, ABS_Y, 0, MAX_12BIT, 0, 0);

	touchscreen_parse_properties(input, false, &sx8654->props);

	sx8654->client = client;
	sx8654->input = input;

	input_set_drvdata(sx8654->input, sx8654);

	error = sx8654_reset(sx8654);
	if (error) {
		dev_err(&client->dev, "reset failed");
		return error;
	}

	error = i2c_smbus_write_byte_data(client, I2C_REG_CHANMASK,
					  sx8654->data->chan_mask);
	if (error) {
		dev_err(&client->dev, "writing to I2C_REG_CHANMASK failed");
		return error;
	}

	if (sx8654->data->has_reg_irqmask) {
		error = i2c_smbus_write_byte_data(client, I2C_REG_IRQMASK,
						  IRQ_PENTOUCH_TOUCHCONVDONE |
							IRQ_PENRELEASE);
		if (error) {
			dev_err(&client->dev, "writing I2C_REG_IRQMASK failed");
			return error;
		}
	}

	error = i2c_smbus_write_byte_data(client, I2C_REG_TOUCH1,
					  CONDIRQ | RPDNT_100K | FILT_7SA);
	if (error) {
		dev_err(&client->dev, "writing to I2C_REG_TOUCH1 failed");
		return error;
	}

	error = devm_request_threaded_irq(&client->dev, client->irq,
					  NULL, sx8654->data->irqh,
					  IRQF_ONESHOT,
					  client->name, sx8654);
	if (error) {
		dev_err(&client->dev,
			"Failed to enable IRQ %d, error: %d\n",
			client->irq, error);
		return error;
	}

	/* Disable the IRQ, we'll enable it in sx8654_open() */
	disable_irq(client->irq);

	error = input_register_device(sx8654->input);
	if (error)
		return error;

	return 0;
}

static const struct sx865x_data sx8650_data = {
	.cmd_manual		= 0xb0,
	.has_irq_penrelease	= false,
	.has_reg_irqmask	= false,
	.chan_mask		= (CONV_X | CONV_Y),
	.irqh			= sx8650_irq,
};

static const struct sx865x_data sx8654_data = {
	.cmd_manual		= 0xc0,
	.has_irq_penrelease	= true,
	.has_reg_irqmask	= true,
	.chan_mask		= (CONV_X | CONV_Y),
	.irqh			= sx8654_irq,
};

#ifdef CONFIG_OF
static const struct of_device_id sx8654_of_match[] = {
	{
		.compatible = "semtech,sx8650",
		.data = &sx8650_data,
	}, {
		.compatible = "semtech,sx8654",
		.data = &sx8654_data,
	}, {
		.compatible = "semtech,sx8655",
		.data = &sx8654_data,
	}, {
		.compatible = "semtech,sx8656",
		.data = &sx8654_data,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, sx8654_of_match);
#endif

static const struct i2c_device_id sx8654_id_table[] = {
	{ .name = "semtech_sx8650", .driver_data = (long)&sx8650_data },
	{ .name = "semtech_sx8654", .driver_data = (long)&sx8654_data },
	{ .name = "semtech_sx8655", .driver_data = (long)&sx8654_data },
	{ .name = "semtech_sx8656", .driver_data = (long)&sx8654_data },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sx8654_id_table);

static struct i2c_driver sx8654_driver = {
	.driver = {
		.name = "sx8654",
		.of_match_table = of_match_ptr(sx8654_of_match),
	},
	.id_table = sx8654_id_table,
	.probe = sx8654_probe,
};
module_i2c_driver(sx8654_driver);

MODULE_AUTHOR("Sébastien Szymanski <sebastien.szymanski@armadeus.com>");
MODULE_DESCRIPTION("Semtech SX8654 I2C Touchscreen Driver");
MODULE_LICENSE("GPL");
