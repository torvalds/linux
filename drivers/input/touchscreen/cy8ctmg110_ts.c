// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for cypress touch screen controller
 *
 * Copyright (c) 2009 Aava Mobile
 *
 * Some cleanups by Alan Cox <alan@linux.intel.com>
 */

#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <asm/byteorder.h>

#define CY8CTMG110_DRIVER_NAME      "cy8ctmg110"

/* Touch coordinates */
#define CY8CTMG110_X_MIN		0
#define CY8CTMG110_Y_MIN		0
#define CY8CTMG110_X_MAX		759
#define CY8CTMG110_Y_MAX		465


/* cy8ctmg110 register definitions */
#define CY8CTMG110_TOUCH_WAKEUP_TIME	0
#define CY8CTMG110_TOUCH_SLEEP_TIME	2
#define CY8CTMG110_TOUCH_X1		3
#define CY8CTMG110_TOUCH_Y1		5
#define CY8CTMG110_TOUCH_X2		7
#define CY8CTMG110_TOUCH_Y2		9
#define CY8CTMG110_FINGERS		11
#define CY8CTMG110_GESTURE		12
#define CY8CTMG110_REG_MAX		13


/*
 * The touch driver structure.
 */
struct cy8ctmg110 {
	struct input_dev *input;
	char phys[32];
	struct i2c_client *client;
	struct gpio_desc *reset_gpio;
};

/*
 * cy8ctmg110_power is the routine that is called when touch hardware
 * is being powered off or on. When powering on this routine de-asserts
 * the RESET line, when powering off reset line is asserted.
 */
static void cy8ctmg110_power(struct cy8ctmg110 *ts, bool poweron)
{
	if (ts->reset_gpio)
		gpiod_set_value_cansleep(ts->reset_gpio, !poweron);
}

static int cy8ctmg110_write_regs(struct cy8ctmg110 *tsc, unsigned char reg,
		unsigned char len, unsigned char *value)
{
	struct i2c_client *client = tsc->client;
	int ret;
	unsigned char i2c_data[6];

	BUG_ON(len > 5);

	i2c_data[0] = reg;
	memcpy(i2c_data + 1, value, len);

	ret = i2c_master_send(client, i2c_data, len + 1);
	if (ret != len + 1) {
		dev_err(&client->dev, "i2c write data cmd failed\n");
		return ret < 0 ? ret : -EIO;
	}

	return 0;
}

static int cy8ctmg110_read_regs(struct cy8ctmg110 *tsc,
		unsigned char *data, unsigned char len, unsigned char cmd)
{
	struct i2c_client *client = tsc->client;
	int ret;
	struct i2c_msg msg[2] = {
		/* first write slave position to i2c devices */
		{
			.addr = client->addr,
			.len = 1,
			.buf = &cmd
		},
		/* Second read data from position */
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = len,
			.buf = data
		}
	};

	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret < 0)
		return ret;

	return 0;
}

static int cy8ctmg110_touch_pos(struct cy8ctmg110 *tsc)
{
	struct input_dev *input = tsc->input;
	unsigned char reg_p[CY8CTMG110_REG_MAX];

	memset(reg_p, 0, CY8CTMG110_REG_MAX);

	/* Reading coordinates */
	if (cy8ctmg110_read_regs(tsc, reg_p, 9, CY8CTMG110_TOUCH_X1) != 0)
		return -EIO;

	/* Number of touch */
	if (reg_p[8] == 0) {
		input_report_key(input, BTN_TOUCH, 0);
	} else  {
		input_report_key(input, BTN_TOUCH, 1);
		input_report_abs(input, ABS_X,
				 be16_to_cpup((__be16 *)(reg_p + 0)));
		input_report_abs(input, ABS_Y,
				 be16_to_cpup((__be16 *)(reg_p + 2)));
	}

	input_sync(input);

	return 0;
}

static int cy8ctmg110_set_sleepmode(struct cy8ctmg110 *ts, bool sleep)
{
	unsigned char reg_p[3];

	if (sleep) {
		reg_p[0] = 0x00;
		reg_p[1] = 0xff;
		reg_p[2] = 5;
	} else {
		reg_p[0] = 0x10;
		reg_p[1] = 0xff;
		reg_p[2] = 0;
	}

	return cy8ctmg110_write_regs(ts, CY8CTMG110_TOUCH_WAKEUP_TIME, 3, reg_p);
}

static irqreturn_t cy8ctmg110_irq_thread(int irq, void *dev_id)
{
	struct cy8ctmg110 *tsc = dev_id;

	cy8ctmg110_touch_pos(tsc);

	return IRQ_HANDLED;
}

static void cy8ctmg110_shut_off(void *_ts)
{
	struct cy8ctmg110 *ts = _ts;

	cy8ctmg110_set_sleepmode(ts, true);
	cy8ctmg110_power(ts, false);
}

static int cy8ctmg110_probe(struct i2c_client *client)
{
	struct cy8ctmg110 *ts;
	struct input_dev *input_dev;
	int err;

	if (!i2c_check_functionality(client->adapter,
					I2C_FUNC_SMBUS_READ_WORD_DATA))
		return -EIO;

	ts = devm_kzalloc(&client->dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	input_dev = devm_input_allocate_device(&client->dev);
	if (!input_dev)
		return -ENOMEM;

	ts->client = client;
	ts->input = input_dev;

	snprintf(ts->phys, sizeof(ts->phys),
		 "%s/input0", dev_name(&client->dev));

	input_dev->name = CY8CTMG110_DRIVER_NAME " Touchscreen";
	input_dev->phys = ts->phys;
	input_dev->id.bustype = BUS_I2C;

	input_set_capability(input_dev, EV_KEY, BTN_TOUCH);
	input_set_abs_params(input_dev, ABS_X,
			CY8CTMG110_X_MIN, CY8CTMG110_X_MAX, 4, 0);
	input_set_abs_params(input_dev, ABS_Y,
			CY8CTMG110_Y_MIN, CY8CTMG110_Y_MAX, 4, 0);

	/* Request and assert reset line */
	ts->reset_gpio = devm_gpiod_get_optional(&client->dev, NULL,
						 GPIOD_OUT_HIGH);
	if (IS_ERR(ts->reset_gpio)) {
		err = PTR_ERR(ts->reset_gpio);
		dev_err(&client->dev,
			"Unable to request reset GPIO: %d\n", err);
		return err;
	}

	cy8ctmg110_power(ts, true);
	cy8ctmg110_set_sleepmode(ts, false);

	err = devm_add_action_or_reset(&client->dev, cy8ctmg110_shut_off, ts);
	if (err)
		return err;

	err = devm_request_threaded_irq(&client->dev, client->irq,
					NULL, cy8ctmg110_irq_thread,
					IRQF_ONESHOT, "touch_reset_key", ts);
	if (err) {
		dev_err(&client->dev,
			"irq %d busy? error %d\n", client->irq, err);
		return err;
	}

	err = input_register_device(input_dev);
	if (err)
		return err;

	i2c_set_clientdata(client, ts);

	return 0;
}

static int __maybe_unused cy8ctmg110_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct cy8ctmg110 *ts = i2c_get_clientdata(client);

	if (!device_may_wakeup(&client->dev)) {
		cy8ctmg110_set_sleepmode(ts, true);
		cy8ctmg110_power(ts, false);
	}

	return 0;
}

static int __maybe_unused cy8ctmg110_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct cy8ctmg110 *ts = i2c_get_clientdata(client);

	if (!device_may_wakeup(&client->dev)) {
		cy8ctmg110_power(ts, true);
		cy8ctmg110_set_sleepmode(ts, false);
	}

	return 0;
}

static SIMPLE_DEV_PM_OPS(cy8ctmg110_pm, cy8ctmg110_suspend, cy8ctmg110_resume);

static const struct i2c_device_id cy8ctmg110_idtable[] = {
	{ CY8CTMG110_DRIVER_NAME, 1 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, cy8ctmg110_idtable);

static struct i2c_driver cy8ctmg110_driver = {
	.driver		= {
		.name	= CY8CTMG110_DRIVER_NAME,
		.pm	= &cy8ctmg110_pm,
	},
	.id_table	= cy8ctmg110_idtable,
	.probe_new	= cy8ctmg110_probe,
};

module_i2c_driver(cy8ctmg110_driver);

MODULE_AUTHOR("Samuli Konttila <samuli.konttila@aavamobile.com>");
MODULE_DESCRIPTION("cy8ctmg110 TouchScreen Driver");
MODULE_LICENSE("GPL v2");
