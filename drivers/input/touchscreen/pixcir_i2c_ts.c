/*
 * Driver for Pixcir I2C touchscreen controllers.
 *
 * Copyright (C) 2010-2011 Pixcir, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/pixcir_ts.h>

struct pixcir_i2c_ts_data {
	struct i2c_client *client;
	struct input_dev *input;
	const struct pixcir_ts_platform_data *chip;
	bool exiting;
};

static void pixcir_ts_poscheck(struct pixcir_i2c_ts_data *data)
{
	struct pixcir_i2c_ts_data *tsdata = data;
	u8 rdbuf[10], wrbuf[1] = { 0 };
	u8 touch;
	int ret;

	ret = i2c_master_send(tsdata->client, wrbuf, sizeof(wrbuf));
	if (ret != sizeof(wrbuf)) {
		dev_err(&tsdata->client->dev,
			"%s: i2c_master_send failed(), ret=%d\n",
			__func__, ret);
		return;
	}

	ret = i2c_master_recv(tsdata->client, rdbuf, sizeof(rdbuf));
	if (ret != sizeof(rdbuf)) {
		dev_err(&tsdata->client->dev,
			"%s: i2c_master_recv failed(), ret=%d\n",
			__func__, ret);
		return;
	}

	touch = rdbuf[0];
	if (touch) {
		u16 posx1 = (rdbuf[3] << 8) | rdbuf[2];
		u16 posy1 = (rdbuf[5] << 8) | rdbuf[4];
		u16 posx2 = (rdbuf[7] << 8) | rdbuf[6];
		u16 posy2 = (rdbuf[9] << 8) | rdbuf[8];

		input_report_key(tsdata->input, BTN_TOUCH, 1);
		input_report_abs(tsdata->input, ABS_X, posx1);
		input_report_abs(tsdata->input, ABS_Y, posy1);

		input_report_abs(tsdata->input, ABS_MT_POSITION_X, posx1);
		input_report_abs(tsdata->input, ABS_MT_POSITION_Y, posy1);
		input_mt_sync(tsdata->input);

		if (touch == 2) {
			input_report_abs(tsdata->input,
					 ABS_MT_POSITION_X, posx2);
			input_report_abs(tsdata->input,
					 ABS_MT_POSITION_Y, posy2);
			input_mt_sync(tsdata->input);
		}
	} else {
		input_report_key(tsdata->input, BTN_TOUCH, 0);
	}

	input_sync(tsdata->input);
}

static irqreturn_t pixcir_ts_isr(int irq, void *dev_id)
{
	struct pixcir_i2c_ts_data *tsdata = dev_id;

	while (!tsdata->exiting) {
		pixcir_ts_poscheck(tsdata);

		if (tsdata->chip->attb_read_val())
			break;

		msleep(20);
	}

	return IRQ_HANDLED;
}

#ifdef CONFIG_PM_SLEEP
static int pixcir_i2c_ts_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);

	if (device_may_wakeup(&client->dev))
		enable_irq_wake(client->irq);

	return 0;
}

static int pixcir_i2c_ts_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);

	if (device_may_wakeup(&client->dev))
		disable_irq_wake(client->irq);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(pixcir_dev_pm_ops,
			 pixcir_i2c_ts_suspend, pixcir_i2c_ts_resume);

static int __devinit pixcir_i2c_ts_probe(struct i2c_client *client,
					 const struct i2c_device_id *id)
{
	const struct pixcir_ts_platform_data *pdata = client->dev.platform_data;
	struct pixcir_i2c_ts_data *tsdata;
	struct input_dev *input;
	int error;

	if (!pdata) {
		dev_err(&client->dev, "platform data not defined\n");
		return -EINVAL;
	}

	tsdata = kzalloc(sizeof(*tsdata), GFP_KERNEL);
	input = input_allocate_device();
	if (!tsdata || !input) {
		dev_err(&client->dev, "Failed to allocate driver data!\n");
		error = -ENOMEM;
		goto err_free_mem;
	}

	tsdata->client = client;
	tsdata->input = input;
	tsdata->chip = pdata;

	input->name = client->name;
	input->id.bustype = BUS_I2C;
	input->dev.parent = &client->dev;

	__set_bit(EV_KEY, input->evbit);
	__set_bit(EV_ABS, input->evbit);
	__set_bit(BTN_TOUCH, input->keybit);
	input_set_abs_params(input, ABS_X, 0, pdata->x_max, 0, 0);
	input_set_abs_params(input, ABS_Y, 0, pdata->y_max, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_X, 0, pdata->x_max, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_Y, 0, pdata->y_max, 0, 0);

	input_set_drvdata(input, tsdata);

	error = request_threaded_irq(client->irq, NULL, pixcir_ts_isr,
				     IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
				     client->name, tsdata);
	if (error) {
		dev_err(&client->dev, "Unable to request touchscreen IRQ.\n");
		goto err_free_mem;
	}

	error = input_register_device(input);
	if (error)
		goto err_free_irq;

	i2c_set_clientdata(client, tsdata);
	device_init_wakeup(&client->dev, 1);

	return 0;

err_free_irq:
	free_irq(client->irq, tsdata);
err_free_mem:
	input_free_device(input);
	kfree(tsdata);
	return error;
}

static int __devexit pixcir_i2c_ts_remove(struct i2c_client *client)
{
	struct pixcir_i2c_ts_data *tsdata = i2c_get_clientdata(client);

	device_init_wakeup(&client->dev, 0);

	tsdata->exiting = true;
	mb();
	free_irq(client->irq, tsdata);

	input_unregister_device(tsdata->input);
	kfree(tsdata);

	return 0;
}

static const struct i2c_device_id pixcir_i2c_ts_id[] = {
	{ "pixcir_ts", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, pixcir_i2c_ts_id);

static struct i2c_driver pixcir_i2c_ts_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "pixcir_ts",
		.pm	= &pixcir_dev_pm_ops,
	},
	.probe		= pixcir_i2c_ts_probe,
	.remove		= __devexit_p(pixcir_i2c_ts_remove),
	.id_table	= pixcir_i2c_ts_id,
};

module_i2c_driver(pixcir_i2c_ts_driver);

MODULE_AUTHOR("Jianchun Bian <jcbian@pixcir.com.cn>, Dequan Meng <dqmeng@pixcir.com.cn>");
MODULE_DESCRIPTION("Pixcir I2C Touchscreen Driver");
MODULE_LICENSE("GPL");
