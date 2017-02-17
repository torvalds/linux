/*
 * Touch Screen driver for EETI's I2C connected touch screen panels
 *   Copyright (c) 2009 Daniel Mack <daniel@caiaq.de>
 *
 * See EETI's software guide for the protocol specification:
 *   http://home.eeti.com.tw/web20/eg/guide.htm
 *
 * Based on migor_ts.c
 *   Copyright (c) 2008 Magnus Damm
 *   Copyright (c) 2007 Ujjwal Pande <ujjwal@kenati.com>
 *
 * This file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU  General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/timer.h>
#include <linux/gpio.h>
#include <linux/input/eeti_ts.h>
#include <linux/slab.h>
#include <asm/unaligned.h>

static bool flip_x;
module_param(flip_x, bool, 0644);
MODULE_PARM_DESC(flip_x, "flip x coordinate");

static bool flip_y;
module_param(flip_y, bool, 0644);
MODULE_PARM_DESC(flip_y, "flip y coordinate");

struct eeti_ts {
	struct i2c_client *client;
	struct input_dev *input;
	struct work_struct work;
	struct mutex mutex;
	int irq_gpio, irq, irq_active_high;
};

#define EETI_TS_BITDEPTH	(11)
#define EETI_MAXVAL		((1 << (EETI_TS_BITDEPTH + 1)) - 1)

#define REPORT_BIT_PRESSED	BIT(0)
#define REPORT_BIT_AD0		BIT(1)
#define REPORT_BIT_AD1		BIT(2)
#define REPORT_BIT_HAS_PRESSURE	BIT(6)
#define REPORT_RES_BITS(v)	(((v) >> 1) + EETI_TS_BITDEPTH)

static inline int eeti_ts_irq_active(struct eeti_ts *eeti)
{
	return gpio_get_value(eeti->irq_gpio) == eeti->irq_active_high;
}

static void eeti_ts_read(struct work_struct *work)
{
	char buf[6];
	unsigned int x, y, res, pressed, to = 100;
	struct eeti_ts *eeti =
		container_of(work, struct eeti_ts, work);

	mutex_lock(&eeti->mutex);

	while (eeti_ts_irq_active(eeti) && --to)
		i2c_master_recv(eeti->client, buf, sizeof(buf));

	if (!to) {
		dev_err(&eeti->client->dev,
			"unable to clear IRQ - line stuck?\n");
		goto out;
	}

	/* drop non-report packets */
	if (!(buf[0] & 0x80))
		goto out;

	pressed = buf[0] & REPORT_BIT_PRESSED;
	res = REPORT_RES_BITS(buf[0] & (REPORT_BIT_AD0 | REPORT_BIT_AD1));

	x = get_unaligned_be16(&buf[1]);
	y = get_unaligned_be16(&buf[3]);

	/* fix the range to 11 bits */
	x >>= res - EETI_TS_BITDEPTH;
	y >>= res - EETI_TS_BITDEPTH;

	if (flip_x)
		x = EETI_MAXVAL - x;

	if (flip_y)
		y = EETI_MAXVAL - y;

	if (buf[0] & REPORT_BIT_HAS_PRESSURE)
		input_report_abs(eeti->input, ABS_PRESSURE, buf[5]);

	input_report_abs(eeti->input, ABS_X, x);
	input_report_abs(eeti->input, ABS_Y, y);
	input_report_key(eeti->input, BTN_TOUCH, !!pressed);
	input_sync(eeti->input);

out:
	mutex_unlock(&eeti->mutex);
}

static irqreturn_t eeti_ts_isr(int irq, void *dev_id)
{
	struct eeti_ts *eeti = dev_id;

	 /* postpone I2C transactions as we are atomic */
	schedule_work(&eeti->work);

	return IRQ_HANDLED;
}

static void eeti_ts_start(struct eeti_ts *eeti)
{
	enable_irq(eeti->irq);

	/* Read the events once to arm the IRQ */
	eeti_ts_read(&eeti->work);
}

static void eeti_ts_stop(struct eeti_ts *eeti)
{
	disable_irq(eeti->irq);
	cancel_work_sync(&eeti->work);
}

static int eeti_ts_open(struct input_dev *dev)
{
	struct eeti_ts *eeti = input_get_drvdata(dev);

	eeti_ts_start(eeti);

	return 0;
}

static void eeti_ts_close(struct input_dev *dev)
{
	struct eeti_ts *eeti = input_get_drvdata(dev);

	eeti_ts_stop(eeti);
}

static int eeti_ts_probe(struct i2c_client *client,
			 const struct i2c_device_id *idp)
{
	struct device *dev = &client->dev;
	struct eeti_ts_platform_data *pdata = dev_get_platdata(dev);
	struct eeti_ts *eeti;
	struct input_dev *input;
	unsigned int irq_flags;
	int error;

	/*
	 * In contrast to what's described in the datasheet, there seems
	 * to be no way of probing the presence of that device using I2C
	 * commands. So we need to blindly believe it is there, and wait
	 * for interrupts to occur.
	 */

	eeti = devm_kzalloc(dev, sizeof(*eeti), GFP_KERNEL);
	if (!eeti) {
		dev_err(dev, "failed to allocate driver data\n");
		return -ENOMEM;
	}

	mutex_init(&eeti->mutex);

	input = devm_input_allocate_device(dev);
	if (!input) {
		dev_err(dev, "Failed to allocate input device.\n");
		return -ENOMEM;
	}

	input_set_capability(input, EV_KEY, BTN_TOUCH);

	input_set_abs_params(input, ABS_X, 0, EETI_MAXVAL, 0, 0);
	input_set_abs_params(input, ABS_Y, 0, EETI_MAXVAL, 0, 0);
	input_set_abs_params(input, ABS_PRESSURE, 0, 0xff, 0, 0);

	input->name = client->name;
	input->id.bustype = BUS_I2C;
	input->open = eeti_ts_open;
	input->close = eeti_ts_close;

	eeti->client = client;
	eeti->input = input;
	eeti->irq_gpio = pdata->irq_gpio;
	eeti->irq = gpio_to_irq(pdata->irq_gpio);

	error = devm_gpio_request_one(dev, pdata->irq_gpio, GPIOF_IN,
				      client->name);
	if (error)
		return error;

	eeti->irq_active_high = pdata->irq_active_high;

	irq_flags = eeti->irq_active_high ?
		IRQF_TRIGGER_RISING : IRQF_TRIGGER_FALLING;

	INIT_WORK(&eeti->work, eeti_ts_read);
	i2c_set_clientdata(client, eeti);
	input_set_drvdata(input, eeti);

	error = input_register_device(input);
	if (error)
		return error;

	error = devm_request_irq(dev, eeti->irq, eeti_ts_isr, irq_flags,
				 client->name, eeti);
	if (error) {
		dev_err(dev, "Unable to request touchscreen IRQ: %d\n",
			error);
		return error;
	}

	/*
	 * Disable the device for now. It will be enabled once the
	 * input device is opened.
	 */
	eeti_ts_stop(eeti);

	return 0;
}

static int __maybe_unused eeti_ts_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct eeti_ts *eeti = i2c_get_clientdata(client);
	struct input_dev *input_dev = eeti->input;

	mutex_lock(&input_dev->mutex);

	if (input_dev->users)
		eeti_ts_stop(eeti);

	mutex_unlock(&input_dev->mutex);

	if (device_may_wakeup(&client->dev))
		enable_irq_wake(eeti->irq);

	return 0;
}

static int __maybe_unused eeti_ts_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct eeti_ts *eeti = i2c_get_clientdata(client);
	struct input_dev *input_dev = eeti->input;

	if (device_may_wakeup(&client->dev))
		disable_irq_wake(eeti->irq);

	mutex_lock(&input_dev->mutex);

	if (input_dev->users)
		eeti_ts_start(eeti);

	mutex_unlock(&input_dev->mutex);

	return 0;
}

static SIMPLE_DEV_PM_OPS(eeti_ts_pm, eeti_ts_suspend, eeti_ts_resume);

static const struct i2c_device_id eeti_ts_id[] = {
	{ "eeti_ts", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, eeti_ts_id);

static struct i2c_driver eeti_ts_driver = {
	.driver = {
		.name = "eeti_ts",
		.pm = &eeti_ts_pm,
	},
	.probe = eeti_ts_probe,
	.id_table = eeti_ts_id,
};

module_i2c_driver(eeti_ts_driver);

MODULE_DESCRIPTION("EETI Touchscreen driver");
MODULE_AUTHOR("Daniel Mack <daniel@caiaq.de>");
MODULE_LICENSE("GPL");
