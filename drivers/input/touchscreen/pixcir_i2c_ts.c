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
#include <linux/gpio.h>

struct pixcir_i2c_ts_data {
	struct i2c_client *client;
	struct input_dev *input;
	const struct pixcir_ts_platform_data *chip;
	bool running;
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
	const struct pixcir_ts_platform_data *pdata = tsdata->chip;

	while (tsdata->running) {
		pixcir_ts_poscheck(tsdata);

		if (gpio_get_value(pdata->gpio_attb))
			break;

		msleep(20);
	}

	return IRQ_HANDLED;
}

static int pixcir_set_power_mode(struct pixcir_i2c_ts_data *ts,
				 enum pixcir_power_mode mode)
{
	struct device *dev = &ts->client->dev;
	int ret;

	ret = i2c_smbus_read_byte_data(ts->client, PIXCIR_REG_POWER_MODE);
	if (ret < 0) {
		dev_err(dev, "%s: can't read reg 0x%x : %d\n",
			__func__, PIXCIR_REG_POWER_MODE, ret);
		return ret;
	}

	ret &= ~PIXCIR_POWER_MODE_MASK;
	ret |= mode;

	/* Always AUTO_IDLE */
	ret |= PIXCIR_POWER_ALLOW_IDLE;

	ret = i2c_smbus_write_byte_data(ts->client, PIXCIR_REG_POWER_MODE, ret);
	if (ret < 0) {
		dev_err(dev, "%s: can't write reg 0x%x : %d\n",
			__func__, PIXCIR_REG_POWER_MODE, ret);
		return ret;
	}

	return 0;
}

/*
 * Set the interrupt mode for the device i.e. ATTB line behaviour
 *
 * @polarity : 1 for active high, 0 for active low.
 */
static int pixcir_set_int_mode(struct pixcir_i2c_ts_data *ts,
			       enum pixcir_int_mode mode, bool polarity)
{
	struct device *dev = &ts->client->dev;
	int ret;

	ret = i2c_smbus_read_byte_data(ts->client, PIXCIR_REG_INT_MODE);
	if (ret < 0) {
		dev_err(dev, "%s: can't read reg 0x%x : %d\n",
			__func__, PIXCIR_REG_INT_MODE, ret);
		return ret;
	}

	ret &= ~PIXCIR_INT_MODE_MASK;
	ret |= mode;

	if (polarity)
		ret |= PIXCIR_INT_POL_HIGH;
	else
		ret &= ~PIXCIR_INT_POL_HIGH;

	ret = i2c_smbus_write_byte_data(ts->client, PIXCIR_REG_INT_MODE, ret);
	if (ret < 0) {
		dev_err(dev, "%s: can't write reg 0x%x : %d\n",
			__func__, PIXCIR_REG_INT_MODE, ret);
		return ret;
	}

	return 0;
}

/*
 * Enable/disable interrupt generation
 */
static int pixcir_int_enable(struct pixcir_i2c_ts_data *ts, bool enable)
{
	struct device *dev = &ts->client->dev;
	int ret;

	ret = i2c_smbus_read_byte_data(ts->client, PIXCIR_REG_INT_MODE);
	if (ret < 0) {
		dev_err(dev, "%s: can't read reg 0x%x : %d\n",
			__func__, PIXCIR_REG_INT_MODE, ret);
		return ret;
	}

	if (enable)
		ret |= PIXCIR_INT_ENABLE;
	else
		ret &= ~PIXCIR_INT_ENABLE;

	ret = i2c_smbus_write_byte_data(ts->client, PIXCIR_REG_INT_MODE, ret);
	if (ret < 0) {
		dev_err(dev, "%s: can't write reg 0x%x : %d\n",
			__func__, PIXCIR_REG_INT_MODE, ret);
		return ret;
	}

	return 0;
}

static int pixcir_start(struct pixcir_i2c_ts_data *ts)
{
	struct device *dev = &ts->client->dev;
	int error;

	/* LEVEL_TOUCH interrupt with active low polarity */
	error = pixcir_set_int_mode(ts, PIXCIR_INT_LEVEL_TOUCH, 0);
	if (error) {
		dev_err(dev, "Failed to set interrupt mode: %d\n", error);
		return error;
	}

	ts->running = true;
	mb();	/* Update status before IRQ can fire */

	/* enable interrupt generation */
	error = pixcir_int_enable(ts, true);
	if (error) {
		dev_err(dev, "Failed to enable interrupt generation: %d\n",
			error);
		return error;
	}

	return 0;
}

static int pixcir_stop(struct pixcir_i2c_ts_data *ts)
{
	int error;

	/* Disable interrupt generation */
	error = pixcir_int_enable(ts, false);
	if (error) {
		dev_err(&ts->client->dev,
			"Failed to disable interrupt generation: %d\n",
			error);
		return error;
	}

	/* Exit ISR if running, no more report parsing */
	ts->running = false;
	mb();	/* update status before we synchronize irq */

	/* Wait till running ISR is complete */
	synchronize_irq(ts->client->irq);

	return 0;
}

static int pixcir_input_open(struct input_dev *dev)
{
	struct pixcir_i2c_ts_data *ts = input_get_drvdata(dev);

	return pixcir_start(ts);
}

static void pixcir_input_close(struct input_dev *dev)
{
	struct pixcir_i2c_ts_data *ts = input_get_drvdata(dev);

	pixcir_stop(ts);
}

#ifdef CONFIG_PM_SLEEP
static int pixcir_i2c_ts_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct pixcir_i2c_ts_data *ts = i2c_get_clientdata(client);
	struct input_dev *input = ts->input;
	int ret = 0;

	mutex_lock(&input->mutex);

	if (device_may_wakeup(&client->dev)) {
		if (!input->users) {
			ret = pixcir_start(ts);
			if (ret) {
				dev_err(dev, "Failed to start\n");
				goto unlock;
			}
		}

		enable_irq_wake(client->irq);
	} else if (input->users) {
		ret = pixcir_stop(ts);
	}

unlock:
	mutex_unlock(&input->mutex);

	return ret;
}

static int pixcir_i2c_ts_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct pixcir_i2c_ts_data *ts = i2c_get_clientdata(client);
	struct input_dev *input = ts->input;
	int ret = 0;

	mutex_lock(&input->mutex);

	if (device_may_wakeup(&client->dev)) {
		disable_irq_wake(client->irq);

		if (!input->users) {
			ret = pixcir_stop(ts);
			if (ret) {
				dev_err(dev, "Failed to stop\n");
				goto unlock;
			}
		}
	} else if (input->users) {
		ret = pixcir_start(ts);
	}

unlock:
	mutex_unlock(&input->mutex);

	return ret;
}
#endif

static SIMPLE_DEV_PM_OPS(pixcir_dev_pm_ops,
			 pixcir_i2c_ts_suspend, pixcir_i2c_ts_resume);

static int pixcir_i2c_ts_probe(struct i2c_client *client,
					 const struct i2c_device_id *id)
{
	const struct pixcir_ts_platform_data *pdata =
			dev_get_platdata(&client->dev);
	struct device *dev = &client->dev;
	struct pixcir_i2c_ts_data *tsdata;
	struct input_dev *input;
	int error;

	if (!pdata) {
		dev_err(&client->dev, "platform data not defined\n");
		return -EINVAL;
	}

	if (!gpio_is_valid(pdata->gpio_attb)) {
		dev_err(dev, "Invalid gpio_attb in pdata\n");
		return -EINVAL;
	}

	tsdata = devm_kzalloc(dev, sizeof(*tsdata), GFP_KERNEL);
	if (!tsdata)
		return -ENOMEM;

	input = devm_input_allocate_device(dev);
	if (!input) {
		dev_err(dev, "Failed to allocate input device\n");
		return -ENOMEM;
	}

	tsdata->client = client;
	tsdata->input = input;
	tsdata->chip = pdata;

	input->name = client->name;
	input->id.bustype = BUS_I2C;
	input->open = pixcir_input_open;
	input->close = pixcir_input_close;
	input->dev.parent = &client->dev;

	__set_bit(EV_KEY, input->evbit);
	__set_bit(EV_ABS, input->evbit);
	__set_bit(BTN_TOUCH, input->keybit);
	input_set_abs_params(input, ABS_X, 0, pdata->x_max, 0, 0);
	input_set_abs_params(input, ABS_Y, 0, pdata->y_max, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_X, 0, pdata->x_max, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_Y, 0, pdata->y_max, 0, 0);

	input_set_drvdata(input, tsdata);

	error = devm_gpio_request_one(dev, pdata->gpio_attb,
				      GPIOF_DIR_IN, "pixcir_i2c_attb");
	if (error) {
		dev_err(dev, "Failed to request ATTB gpio\n");
		return error;
	}

	error = devm_request_threaded_irq(dev, client->irq, NULL, pixcir_ts_isr,
					  IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					  client->name, tsdata);
	if (error) {
		dev_err(dev, "failed to request irq %d\n", client->irq);
		return error;
	}

	/* Always be in IDLE mode to save power, device supports auto wake */
	error = pixcir_set_power_mode(tsdata, PIXCIR_POWER_IDLE);
	if (error) {
		dev_err(dev, "Failed to set IDLE mode\n");
		return error;
	}

	/* Stop device till opened */
	error = pixcir_stop(tsdata);
	if (error)
		return error;

	error = input_register_device(input);
	if (error)
		return error;

	i2c_set_clientdata(client, tsdata);
	device_init_wakeup(&client->dev, 1);

	return 0;
}

static int pixcir_i2c_ts_remove(struct i2c_client *client)
{
	device_init_wakeup(&client->dev, 0);

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
	.remove		= pixcir_i2c_ts_remove,
	.id_table	= pixcir_i2c_ts_id,
};

module_i2c_driver(pixcir_i2c_ts_driver);

MODULE_AUTHOR("Jianchun Bian <jcbian@pixcir.com.cn>, Dequan Meng <dqmeng@pixcir.com.cn>");
MODULE_DESCRIPTION("Pixcir I2C Touchscreen Driver");
MODULE_LICENSE("GPL");
