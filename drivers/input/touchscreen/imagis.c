// SPDX-License-Identifier: GPL-2.0-only

#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/input/touchscreen.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>

#define IST3038C_HIB_ACCESS		(0x800B << 16)
#define IST3038C_DIRECT_ACCESS		BIT(31)
#define IST3038C_REG_CHIPID		0x40001000
#define IST3038C_REG_HIB_BASE		0x30000100
#define IST3038C_REG_TOUCH_STATUS	(IST3038C_REG_HIB_BASE | IST3038C_HIB_ACCESS)
#define IST3038C_REG_TOUCH_COORD	(IST3038C_REG_HIB_BASE | IST3038C_HIB_ACCESS | 0x8)
#define IST3038C_REG_INTR_MESSAGE	(IST3038C_REG_HIB_BASE | IST3038C_HIB_ACCESS | 0x4)
#define IST3038C_WHOAMI			0x38c
#define IST3038C_CHIP_ON_DELAY_MS	60
#define IST3038C_I2C_RETRY_COUNT	3
#define IST3038C_MAX_FINGER_NUM		10
#define IST3038C_X_MASK			GENMASK(23, 12)
#define IST3038C_X_SHIFT		12
#define IST3038C_Y_MASK			GENMASK(11, 0)
#define IST3038C_AREA_MASK		GENMASK(27, 24)
#define IST3038C_AREA_SHIFT		24
#define IST3038C_FINGER_COUNT_MASK	GENMASK(15, 12)
#define IST3038C_FINGER_COUNT_SHIFT	12
#define IST3038C_FINGER_STATUS_MASK	GENMASK(9, 0)

struct imagis_ts {
	struct i2c_client *client;
	struct input_dev *input_dev;
	struct touchscreen_properties prop;
	struct regulator_bulk_data supplies[2];
};

static int imagis_i2c_read_reg(struct imagis_ts *ts,
			       unsigned int reg, u32 *data)
{
	__be32 ret_be;
	__be32 reg_be = cpu_to_be32(reg);
	struct i2c_msg msg[] = {
		{
			.addr = ts->client->addr,
			.flags = 0,
			.buf = (unsigned char *)&reg_be,
			.len = sizeof(reg_be),
		}, {
			.addr = ts->client->addr,
			.flags = I2C_M_RD,
			.buf = (unsigned char *)&ret_be,
			.len = sizeof(ret_be),
		},
	};
	int ret, error;
	int retry = IST3038C_I2C_RETRY_COUNT;

	/* Retry in case the controller fails to respond */
	do {
		ret = i2c_transfer(ts->client->adapter, msg, ARRAY_SIZE(msg));
		if (ret == ARRAY_SIZE(msg)) {
			*data = be32_to_cpu(ret_be);
			return 0;
		}

		error = ret < 0 ? ret : -EIO;
		dev_err(&ts->client->dev,
			"%s - i2c_transfer failed: %d (%d)\n",
			__func__, error, ret);
	} while (--retry);

	return error;
}

static irqreturn_t imagis_interrupt(int irq, void *dev_id)
{
	struct imagis_ts *ts = dev_id;
	u32 intr_message, finger_status;
	unsigned int finger_count, finger_pressed;
	int i;
	int error;

	error = imagis_i2c_read_reg(ts, IST3038C_REG_INTR_MESSAGE,
				    &intr_message);
	if (error) {
		dev_err(&ts->client->dev,
			"failed to read the interrupt message: %d\n", error);
		goto out;
	}

	finger_count = (intr_message & IST3038C_FINGER_COUNT_MASK) >>
				IST3038C_FINGER_COUNT_SHIFT;
	if (finger_count > IST3038C_MAX_FINGER_NUM) {
		dev_err(&ts->client->dev,
			"finger count %d is more than maximum supported\n",
			finger_count);
		goto out;
	}

	finger_pressed = intr_message & IST3038C_FINGER_STATUS_MASK;

	for (i = 0; i < finger_count; i++) {
		error = imagis_i2c_read_reg(ts,
					    IST3038C_REG_TOUCH_COORD + (i * 4),
					    &finger_status);
		if (error) {
			dev_err(&ts->client->dev,
				"failed to read coordinates for finger %d: %d\n",
				i, error);
			goto out;
		}

		input_mt_slot(ts->input_dev, i);
		input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER,
					   finger_pressed & BIT(i));
		touchscreen_report_pos(ts->input_dev, &ts->prop,
				       (finger_status & IST3038C_X_MASK) >>
						IST3038C_X_SHIFT,
				       finger_status & IST3038C_Y_MASK, 1);
		input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR,
				 (finger_status & IST3038C_AREA_MASK) >>
					IST3038C_AREA_SHIFT);
	}

	input_mt_sync_frame(ts->input_dev);
	input_sync(ts->input_dev);

out:
	return IRQ_HANDLED;
}

static void imagis_power_off(void *_ts)
{
	struct imagis_ts *ts = _ts;

	regulator_bulk_disable(ARRAY_SIZE(ts->supplies), ts->supplies);
}

static int imagis_power_on(struct imagis_ts *ts)
{
	int error;

	error = regulator_bulk_enable(ARRAY_SIZE(ts->supplies), ts->supplies);
	if (error)
		return error;

	msleep(IST3038C_CHIP_ON_DELAY_MS);

	return 0;
}

static int imagis_start(struct imagis_ts *ts)
{
	int error;

	error = imagis_power_on(ts);
	if (error)
		return error;

	enable_irq(ts->client->irq);

	return 0;
}

static int imagis_stop(struct imagis_ts *ts)
{
	disable_irq(ts->client->irq);

	imagis_power_off(ts);

	return 0;
}

static int imagis_input_open(struct input_dev *dev)
{
	struct imagis_ts *ts = input_get_drvdata(dev);

	return imagis_start(ts);
}

static void imagis_input_close(struct input_dev *dev)
{
	struct imagis_ts *ts = input_get_drvdata(dev);

	imagis_stop(ts);
}

static int imagis_init_input_dev(struct imagis_ts *ts)
{
	struct input_dev *input_dev;
	int error;

	input_dev = devm_input_allocate_device(&ts->client->dev);
	if (!input_dev)
		return -ENOMEM;

	ts->input_dev = input_dev;

	input_dev->name = "Imagis capacitive touchscreen";
	input_dev->phys = "input/ts";
	input_dev->id.bustype = BUS_I2C;
	input_dev->open = imagis_input_open;
	input_dev->close = imagis_input_close;

	input_set_drvdata(input_dev, ts);

	input_set_capability(input_dev, EV_ABS, ABS_MT_POSITION_X);
	input_set_capability(input_dev, EV_ABS, ABS_MT_POSITION_Y);
	input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);

	touchscreen_parse_properties(input_dev, true, &ts->prop);
	if (!ts->prop.max_x || !ts->prop.max_y) {
		dev_err(&ts->client->dev,
			"Touchscreen-size-x and/or touchscreen-size-y not set in dts\n");
		return -EINVAL;
	}

	error = input_mt_init_slots(input_dev,
				    IST3038C_MAX_FINGER_NUM,
				    INPUT_MT_DIRECT | INPUT_MT_DROP_UNUSED);
	if (error) {
		dev_err(&ts->client->dev,
			"Failed to initialize MT slots: %d", error);
		return error;
	}

	error = input_register_device(input_dev);
	if (error) {
		dev_err(&ts->client->dev,
			"Failed to register input device: %d", error);
		return error;
	}

	return 0;
}

static int imagis_init_regulators(struct imagis_ts *ts)
{
	struct i2c_client *client = ts->client;

	ts->supplies[0].supply = "vdd";
	ts->supplies[1].supply = "vddio";
	return devm_regulator_bulk_get(&client->dev,
				       ARRAY_SIZE(ts->supplies),
				       ts->supplies);
}

static int imagis_probe(struct i2c_client *i2c)
{
	struct device *dev = &i2c->dev;
	struct imagis_ts *ts;
	int chip_id, error;

	ts = devm_kzalloc(dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	ts->client = i2c;

	error = imagis_init_regulators(ts);
	if (error) {
		dev_err(dev, "regulator init error: %d\n", error);
		return error;
	}

	error = imagis_power_on(ts);
	if (error) {
		dev_err(dev, "failed to enable regulators: %d\n", error);
		return error;
	}

	error = devm_add_action_or_reset(dev, imagis_power_off, ts);
	if (error) {
		dev_err(dev, "failed to install poweroff action: %d\n", error);
		return error;
	}

	error = imagis_i2c_read_reg(ts,
			IST3038C_REG_CHIPID | IST3038C_DIRECT_ACCESS,
			&chip_id);
	if (error) {
		dev_err(dev, "chip ID read failure: %d\n", error);
		return error;
	}

	if (chip_id != IST3038C_WHOAMI) {
		dev_err(dev, "unknown chip ID: 0x%x\n", chip_id);
		return -EINVAL;
	}

	error = devm_request_threaded_irq(dev, i2c->irq,
					  NULL, imagis_interrupt,
					  IRQF_ONESHOT | IRQF_NO_AUTOEN,
					  "imagis-touchscreen", ts);
	if (error) {
		dev_err(dev, "IRQ %d allocation failure: %d\n",
			i2c->irq, error);
		return error;
	}

	error = imagis_init_input_dev(ts);
	if (error)
		return error;

	return 0;
}

static int __maybe_unused imagis_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct imagis_ts *ts = i2c_get_clientdata(client);
	int retval = 0;

	mutex_lock(&ts->input_dev->mutex);

	if (input_device_enabled(ts->input_dev))
		retval = imagis_stop(ts);

	mutex_unlock(&ts->input_dev->mutex);

	return retval;
}

static int __maybe_unused imagis_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct imagis_ts *ts = i2c_get_clientdata(client);
	int retval = 0;

	mutex_lock(&ts->input_dev->mutex);

	if (input_device_enabled(ts->input_dev))
		retval = imagis_start(ts);

	mutex_unlock(&ts->input_dev->mutex);

	return retval;
}

static SIMPLE_DEV_PM_OPS(imagis_pm_ops, imagis_suspend, imagis_resume);

#ifdef CONFIG_OF
static const struct of_device_id imagis_of_match[] = {
	{ .compatible = "imagis,ist3038c", },
	{ },
};
MODULE_DEVICE_TABLE(of, imagis_of_match);
#endif

static struct i2c_driver imagis_ts_driver = {
	.driver = {
		.name = "imagis-touchscreen",
		.pm = &imagis_pm_ops,
		.of_match_table = of_match_ptr(imagis_of_match),
	},
	.probe_new = imagis_probe,
};

module_i2c_driver(imagis_ts_driver);

MODULE_DESCRIPTION("Imagis IST3038C Touchscreen Driver");
MODULE_AUTHOR("Markuss Broks <markuss.broks@gmail.com>");
MODULE_LICENSE("GPL");
