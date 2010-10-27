/*
 * Driver for cypress touch screen controller
 *
 * Copyright (c) 2009 Aava Mobile
 *
 * Some cleanups by Alan Cox <alan@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/input/cy8ctmg110_pdata.h>

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
	int reset_pin;
	int irq_pin;
};

/*
 * cy8ctmg110_power is the routine that is called when touch hardware
 * will powered off or on.
 */
static void cy8ctmg110_power(struct cy8ctmg110 *ts, bool poweron)
{
	if (ts->reset_pin)
		gpio_direction_output(ts->reset_pin, 1 - poweron);
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
	if (ret != 1) {
		dev_err(&client->dev, "i2c write data cmd failed\n");
		return ret ? ret : -EIO;
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
		{ client->addr, 0, 1, &cmd },
		/* Second read data from position */
		{ client->addr, I2C_M_RD, len, data }
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
	int x, y;

	memset(reg_p, 0, CY8CTMG110_REG_MAX);

	/* Reading coordinates */
	if (cy8ctmg110_read_regs(tsc, reg_p, 9, CY8CTMG110_TOUCH_X1) != 0)
		return -EIO;

	y = reg_p[2] << 8 | reg_p[3];
	x = reg_p[0] << 8 | reg_p[1];

	/* Number of touch */
	if (reg_p[8] == 0) {
		input_report_key(input, BTN_TOUCH, 0);
	} else  {
		input_report_key(input, BTN_TOUCH, 1);
		input_report_abs(input, ABS_X, x);
		input_report_abs(input, ABS_Y, y);
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

static int __devinit cy8ctmg110_probe(struct i2c_client *client,
					const struct i2c_device_id *id)
{
	const struct cy8ctmg110_pdata *pdata = client->dev.platform_data;
	struct cy8ctmg110 *ts;
	struct input_dev *input_dev;
	int err;

	/* No pdata no way forward */
	if (pdata == NULL) {
		dev_err(&client->dev, "no pdata\n");
		return -ENODEV;
	}

	if (!i2c_check_functionality(client->adapter,
					I2C_FUNC_SMBUS_READ_WORD_DATA))
		return -EIO;

	ts = kzalloc(sizeof(struct cy8ctmg110), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!ts || !input_dev) {
		err = -ENOMEM;
		goto err_free_mem;
	}

	ts->client = client;
	ts->input = input_dev;

	snprintf(ts->phys, sizeof(ts->phys),
		 "%s/input0", dev_name(&client->dev));

	input_dev->name = CY8CTMG110_DRIVER_NAME " Touchscreen";
	input_dev->phys = ts->phys;
	input_dev->id.bustype = BUS_I2C;
	input_dev->dev.parent = &client->dev;

	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);

	input_set_abs_params(input_dev, ABS_X,
			CY8CTMG110_X_MIN, CY8CTMG110_X_MAX, 0, 0);
	input_set_abs_params(input_dev, ABS_Y,
			CY8CTMG110_Y_MIN, CY8CTMG110_Y_MAX, 0, 0);

	if (ts->reset_pin) {
		err = gpio_request(ts->reset_pin, NULL);
		if (err) {
			dev_err(&client->dev,
				"Unable to request GPIO pin %d.\n",
				ts->reset_pin);
			goto err_free_mem;
		}
	}

	cy8ctmg110_power(ts, true);
	cy8ctmg110_set_sleepmode(ts, false);

	err = gpio_request(ts->irq_pin, "touch_irq_key");
	if (err < 0) {
		dev_err(&client->dev,
			"Failed to request GPIO %d, error %d\n",
			ts->irq_pin, err);
		goto err_shutoff_device;
	}

	err = gpio_direction_input(ts->irq_pin);
	if (err < 0) {
		dev_err(&client->dev,
			"Failed to configure input direction for GPIO %d, error %d\n",
			ts->irq_pin, err);
		goto err_free_irq_gpio;
	}

	client->irq = gpio_to_irq(ts->irq_pin);
	if (client->irq < 0) {
		err = client->irq;
		dev_err(&client->dev,
			"Unable to get irq number for GPIO %d, error %d\n",
			ts->irq_pin, err);
		goto err_free_irq_gpio;
	}

	err = request_threaded_irq(client->irq, NULL, cy8ctmg110_irq_thread,
				   IRQF_TRIGGER_RISING, "touch_reset_key", ts);
	if (err < 0) {
		dev_err(&client->dev,
			"irq %d busy? error %d\n", client->irq, err);
		goto err_free_irq_gpio;
	}

	err = input_register_device(input_dev);
	if (err)
		goto err_free_irq;

	i2c_set_clientdata(client, ts);
	device_init_wakeup(&client->dev, 1);
	return 0;

err_free_irq:
	free_irq(client->irq, ts);
err_free_irq_gpio:
	gpio_free(ts->irq_pin);
err_shutoff_device:
	cy8ctmg110_set_sleepmode(ts, true);
	cy8ctmg110_power(ts, false);
	if (ts->reset_pin)
		gpio_free(ts->reset_pin);
err_free_mem:
	input_free_device(input_dev);
	kfree(ts);
	return err;
}

#ifdef CONFIG_PM
static int cy8ctmg110_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct cy8ctmg110 *ts = i2c_get_clientdata(client);

	if (device_may_wakeup(&client->dev))
		enable_irq_wake(client->irq);
	else {
		cy8ctmg110_set_sleepmode(ts, true);
		cy8ctmg110_power(ts, false);
	}
	return 0;
}

static int cy8ctmg110_resume(struct i2c_client *client)
{
	struct cy8ctmg110 *ts = i2c_get_clientdata(client);

	if (device_may_wakeup(&client->dev))
		disable_irq_wake(client->irq);
	else {
		cy8ctmg110_power(ts, true);
		cy8ctmg110_set_sleepmode(ts, false);
	}
	return 0;
}
#endif

static int __devexit cy8ctmg110_remove(struct i2c_client *client)
{
	struct cy8ctmg110 *ts = i2c_get_clientdata(client);

	cy8ctmg110_set_sleepmode(ts, true);
	cy8ctmg110_power(ts, false);

	free_irq(client->irq, ts);
	input_unregister_device(ts->input);
	gpio_free(ts->irq_pin);
	if (ts->reset_pin)
		gpio_free(ts->reset_pin);
	kfree(ts);

	return 0;
}

static struct i2c_device_id cy8ctmg110_idtable[] = {
	{ CY8CTMG110_DRIVER_NAME, 1 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, cy8ctmg110_idtable);

static struct i2c_driver cy8ctmg110_driver = {
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= CY8CTMG110_DRIVER_NAME,
	},
	.id_table	= cy8ctmg110_idtable,
	.probe		= cy8ctmg110_probe,
	.remove		= __devexit_p(cy8ctmg110_remove),
#ifdef CONFIG_PM
	.suspend	= cy8ctmg110_suspend,
	.resume		= cy8ctmg110_resume,
#endif
};

static int __init cy8ctmg110_init(void)
{
	return i2c_add_driver(&cy8ctmg110_driver);
}

static void __exit cy8ctmg110_exit(void)
{
	i2c_del_driver(&cy8ctmg110_driver);
}

module_init(cy8ctmg110_init);
module_exit(cy8ctmg110_exit);

MODULE_AUTHOR("Samuli Konttila <samuli.konttila@aavamobile.com>");
MODULE_DESCRIPTION("cy8ctmg110 TouchScreen Driver");
MODULE_LICENSE("GPL v2");
