// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Naveen Kumar G <naveen.gaddipati@stericsson.com> for ST-Ericsson
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/workqueue.h>
#include <linux/input.h>
#include <linux/input/bu21013.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>
#include <linux/module.h>
#include <linux/gpio/consumer.h>
#include <linux/of.h>

#define MAX_FINGERS	2
#define RESET_DELAY	30
#define PENUP_TIMEOUT	(10)
#define DELTA_MIN	16
#define MASK_BITS	0x03
#define SHIFT_8		8
#define SHIFT_2		2
#define LENGTH_OF_BUFFER	11
#define I2C_RETRY_COUNT	5

#define BU21013_SENSORS_BTN_0_7_REG	0x70
#define BU21013_SENSORS_BTN_8_15_REG	0x71
#define BU21013_SENSORS_BTN_16_23_REG	0x72
#define BU21013_X1_POS_MSB_REG		0x73
#define BU21013_X1_POS_LSB_REG		0x74
#define BU21013_Y1_POS_MSB_REG		0x75
#define BU21013_Y1_POS_LSB_REG		0x76
#define BU21013_X2_POS_MSB_REG		0x77
#define BU21013_X2_POS_LSB_REG		0x78
#define BU21013_Y2_POS_MSB_REG		0x79
#define BU21013_Y2_POS_LSB_REG		0x7A
#define BU21013_INT_CLR_REG		0xE8
#define BU21013_INT_MODE_REG		0xE9
#define BU21013_GAIN_REG		0xEA
#define BU21013_OFFSET_MODE_REG		0xEB
#define BU21013_XY_EDGE_REG		0xEC
#define BU21013_RESET_REG		0xED
#define BU21013_CALIB_REG		0xEE
#define BU21013_DONE_REG		0xEF
#define BU21013_SENSOR_0_7_REG		0xF0
#define BU21013_SENSOR_8_15_REG		0xF1
#define BU21013_SENSOR_16_23_REG	0xF2
#define BU21013_POS_MODE1_REG		0xF3
#define BU21013_POS_MODE2_REG		0xF4
#define BU21013_CLK_MODE_REG		0xF5
#define BU21013_IDLE_REG		0xFA
#define BU21013_FILTER_REG		0xFB
#define BU21013_TH_ON_REG		0xFC
#define BU21013_TH_OFF_REG		0xFD


#define BU21013_RESET_ENABLE		0x01

#define BU21013_SENSORS_EN_0_7		0x3F
#define BU21013_SENSORS_EN_8_15		0xFC
#define BU21013_SENSORS_EN_16_23	0x1F

#define BU21013_POS_MODE1_0		0x02
#define BU21013_POS_MODE1_1		0x04
#define BU21013_POS_MODE1_2		0x08

#define BU21013_POS_MODE2_ZERO		0x01
#define BU21013_POS_MODE2_AVG1		0x02
#define BU21013_POS_MODE2_AVG2		0x04
#define BU21013_POS_MODE2_EN_XY		0x08
#define BU21013_POS_MODE2_EN_RAW	0x10
#define BU21013_POS_MODE2_MULTI		0x80

#define BU21013_CLK_MODE_DIV		0x01
#define BU21013_CLK_MODE_EXT		0x02
#define BU21013_CLK_MODE_CALIB		0x80

#define BU21013_IDLET_0			0x01
#define BU21013_IDLET_1			0x02
#define BU21013_IDLET_2			0x04
#define BU21013_IDLET_3			0x08
#define BU21013_IDLE_INTERMIT_EN	0x10

#define BU21013_DELTA_0_6	0x7F
#define BU21013_FILTER_EN	0x80

#define BU21013_INT_MODE_LEVEL	0x00
#define BU21013_INT_MODE_EDGE	0x01

#define BU21013_GAIN_0		0x01
#define BU21013_GAIN_1		0x02
#define BU21013_GAIN_2		0x04

#define BU21013_OFFSET_MODE_DEFAULT	0x00
#define BU21013_OFFSET_MODE_MOVE	0x01
#define BU21013_OFFSET_MODE_DISABLE	0x02

#define BU21013_TH_ON_0		0x01
#define BU21013_TH_ON_1		0x02
#define BU21013_TH_ON_2		0x04
#define BU21013_TH_ON_3		0x08
#define BU21013_TH_ON_4		0x10
#define BU21013_TH_ON_5		0x20
#define BU21013_TH_ON_6		0x40
#define BU21013_TH_ON_7		0x80
#define BU21013_TH_ON_MAX	0xFF

#define BU21013_TH_OFF_0	0x01
#define BU21013_TH_OFF_1	0x02
#define BU21013_TH_OFF_2	0x04
#define BU21013_TH_OFF_3	0x08
#define BU21013_TH_OFF_4	0x10
#define BU21013_TH_OFF_5	0x20
#define BU21013_TH_OFF_6	0x40
#define BU21013_TH_OFF_7	0x80
#define BU21013_TH_OFF_MAX	0xFF

#define BU21013_X_EDGE_0	0x01
#define BU21013_X_EDGE_1	0x02
#define BU21013_X_EDGE_2	0x04
#define BU21013_X_EDGE_3	0x08
#define BU21013_Y_EDGE_0	0x10
#define BU21013_Y_EDGE_1	0x20
#define BU21013_Y_EDGE_2	0x40
#define BU21013_Y_EDGE_3	0x80

#define BU21013_DONE	0x01
#define BU21013_NUMBER_OF_X_SENSORS	(6)
#define BU21013_NUMBER_OF_Y_SENSORS	(11)

#define DRIVER_TP	"bu21013_tp"

/**
 * struct bu21013_ts - touch panel data structure
 * @client: pointer to the i2c client
 * @wait: variable to wait_queue_head_t structure
 * @touch_stopped: touch stop flag
 * @chip: pointer to the touch panel controller
 * @in_dev: pointer to the input device structure
 * @regulator: pointer to the Regulator used for touch screen
 * @cs_gpiod: chip select GPIO line
 * @int_gpiod: touch interrupt GPIO line
 *
 * Touch panel device data structure
 */
struct bu21013_ts {
	struct i2c_client *client;
	wait_queue_head_t wait;
	const struct bu21013_platform_device *chip;
	struct input_dev *in_dev;
	struct regulator *regulator;
	struct gpio_desc *cs_gpiod;
	struct gpio_desc *int_gpiod;
	unsigned int irq;
	bool touch_stopped;
};

static int bu21013_read_block_data(struct bu21013_ts *ts, u8 *buf)
{
	int ret, i;

	for (i = 0; i < I2C_RETRY_COUNT; i++) {
		ret = i2c_smbus_read_i2c_block_data(ts->client,
						    BU21013_SENSORS_BTN_0_7_REG,
						    LENGTH_OF_BUFFER, buf);
		if (ret == LENGTH_OF_BUFFER)
			return 0;
	}

	return -EINVAL;
}

static int bu21013_do_touch_report(struct bu21013_ts *ts)
{
	u8	buf[LENGTH_OF_BUFFER];
	unsigned int pos_x[2], pos_y[2];
	bool	has_x_sensors, has_y_sensors;
	int	finger_down_count = 0;
	int	i;

	if (bu21013_read_block_data(ts, buf) < 0)
		return -EINVAL;

	has_x_sensors = hweight32(buf[0] & BU21013_SENSORS_EN_0_7);
	has_y_sensors = hweight32(((buf[1] & BU21013_SENSORS_EN_8_15) |
		((buf[2] & BU21013_SENSORS_EN_16_23) << SHIFT_8)) >> SHIFT_2);
	if (!has_x_sensors || !has_y_sensors)
		return 0;

	for (i = 0; i < MAX_FINGERS; i++) {
		const u8 *p = &buf[4 * i + 3];
		unsigned int x = p[0] << SHIFT_2 | (p[1] & MASK_BITS);
		unsigned int y = p[2] << SHIFT_2 | (p[3] & MASK_BITS);
		if (x == 0 || y == 0)
			continue;
		pos_x[finger_down_count] = x;
		pos_y[finger_down_count] = y;
		finger_down_count++;
	}

	if (finger_down_count) {
		if (finger_down_count == 2 &&
		    (abs(pos_x[0] - pos_x[1]) < DELTA_MIN ||
		     abs(pos_y[0] - pos_y[1]) < DELTA_MIN)) {
			return 0;
		}

		for (i = 0; i < finger_down_count; i++) {
			if (ts->chip->x_flip)
				pos_x[i] = ts->chip->touch_x_max - pos_x[i];
			if (ts->chip->y_flip)
				pos_y[i] = ts->chip->touch_y_max - pos_y[i];

			input_report_abs(ts->in_dev,
					 ABS_MT_POSITION_X, pos_x[i]);
			input_report_abs(ts->in_dev,
					 ABS_MT_POSITION_Y, pos_y[i]);
			input_mt_sync(ts->in_dev);
		}
	} else
		input_mt_sync(ts->in_dev);

	input_sync(ts->in_dev);

	return 0;
}

static irqreturn_t bu21013_gpio_irq(int irq, void *device_data)
{
	struct bu21013_ts *ts = device_data;
	int keep_polling;
	int error;

	do {
		error = bu21013_do_touch_report(ts);
		if (error) {
			dev_err(&ts->client->dev, "%s failed\n", __func__);
			break;
		}

		keep_polling = gpiod_get_value(ts->int_gpiod);
		if (keep_polling)
			wait_event_timeout(ts->wait, ts->touch_stopped,
					   msecs_to_jiffies(2));
	} while (keep_polling && !ts->touch_stopped);

	return IRQ_HANDLED;
}

static int bu21013_init_chip(struct bu21013_ts *ts)
{
	struct i2c_client *client = ts->client;
	int error;

	error = i2c_smbus_write_byte_data(client, BU21013_RESET_REG,
					  BU21013_RESET_ENABLE);
	if (error) {
		dev_err(&client->dev, "BU21013_RESET reg write failed\n");
		return error;
	}
	msleep(RESET_DELAY);

	error = i2c_smbus_write_byte_data(client, BU21013_SENSOR_0_7_REG,
					  BU21013_SENSORS_EN_0_7);
	if (error) {
		dev_err(&client->dev, "BU21013_SENSOR_0_7 reg write failed\n");
		return error;
	}

	error = i2c_smbus_write_byte_data(client, BU21013_SENSOR_8_15_REG,
					  BU21013_SENSORS_EN_8_15);
	if (error) {
		dev_err(&client->dev, "BU21013_SENSOR_8_15 reg write failed\n");
		return error;
	}

	error = i2c_smbus_write_byte_data(client, BU21013_SENSOR_16_23_REG,
					  BU21013_SENSORS_EN_16_23);
	if (error) {
		dev_err(&client->dev, "BU21013_SENSOR_16_23 reg write failed\n");
		return error;
	}

	error = i2c_smbus_write_byte_data(client, BU21013_POS_MODE1_REG,
					  BU21013_POS_MODE1_0 |
						BU21013_POS_MODE1_1);
	if (error) {
		dev_err(&client->dev, "BU21013_POS_MODE1 reg write failed\n");
		return error;
	}

	error = i2c_smbus_write_byte_data(client, BU21013_POS_MODE2_REG,
					  BU21013_POS_MODE2_ZERO |
						BU21013_POS_MODE2_AVG1 |
						BU21013_POS_MODE2_AVG2 |
						BU21013_POS_MODE2_EN_RAW |
						BU21013_POS_MODE2_MULTI);
	if (error) {
		dev_err(&client->dev, "BU21013_POS_MODE2 reg write failed\n");
		return error;
	}

	if (ts->chip->ext_clk)
		error = i2c_smbus_write_byte_data(client, BU21013_CLK_MODE_REG,
						  BU21013_CLK_MODE_EXT |
							BU21013_CLK_MODE_CALIB);
	else
		error = i2c_smbus_write_byte_data(client, BU21013_CLK_MODE_REG,
						  BU21013_CLK_MODE_DIV |
							BU21013_CLK_MODE_CALIB);
	if (error) {
		dev_err(&client->dev, "BU21013_CLK_MODE reg write failed\n");
		return error;
	}

	error = i2c_smbus_write_byte_data(client, BU21013_IDLE_REG,
					  BU21013_IDLET_0 |
						BU21013_IDLE_INTERMIT_EN);
	if (error) {
		dev_err(&client->dev, "BU21013_IDLE reg write failed\n");
		return error;
	}

	error = i2c_smbus_write_byte_data(client, BU21013_INT_MODE_REG,
					  BU21013_INT_MODE_LEVEL);
	if (error) {
		dev_err(&client->dev, "BU21013_INT_MODE reg write failed\n");
		return error;
	}

	error = i2c_smbus_write_byte_data(client, BU21013_FILTER_REG,
					  BU21013_DELTA_0_6 |
						BU21013_FILTER_EN);
	if (error) {
		dev_err(&client->dev, "BU21013_FILTER reg write failed\n");
		return error;
	}

	error = i2c_smbus_write_byte_data(client, BU21013_TH_ON_REG,
					  BU21013_TH_ON_5);
	if (error) {
		dev_err(&client->dev, "BU21013_TH_ON reg write failed\n");
		return error;
	}

	error = i2c_smbus_write_byte_data(client, BU21013_TH_OFF_REG,
					  BU21013_TH_OFF_4 | BU21013_TH_OFF_3);
	if (error) {
		dev_err(&client->dev, "BU21013_TH_OFF reg write failed\n");
		return error;
	}

	error = i2c_smbus_write_byte_data(client, BU21013_GAIN_REG,
					  BU21013_GAIN_0 | BU21013_GAIN_1);
	if (error) {
		dev_err(&client->dev, "BU21013_GAIN reg write failed\n");
		return error;
	}

	error = i2c_smbus_write_byte_data(client, BU21013_OFFSET_MODE_REG,
					  BU21013_OFFSET_MODE_DEFAULT);
	if (error) {
		dev_err(&client->dev, "BU21013_OFFSET_MODE reg write failed\n");
		return error;
	}

	error = i2c_smbus_write_byte_data(client, BU21013_XY_EDGE_REG,
					  BU21013_X_EDGE_0 |
						BU21013_X_EDGE_2 |
						BU21013_Y_EDGE_1 |
						BU21013_Y_EDGE_3);
	if (error) {
		dev_err(&client->dev, "BU21013_XY_EDGE reg write failed\n");
		return error;
	}

	error = i2c_smbus_write_byte_data(client, BU21013_DONE_REG,
					  BU21013_DONE);
	if (error) {
		dev_err(&client->dev, "BU21013_REG_DONE reg write failed\n");
		return error;
	}

	return 0;
}

/**
 * bu21013_free_irq() - frees IRQ registered for touchscreen
 * @ts: device structure pointer
 *
 * This function signals interrupt thread to stop processing and
 * frees interrupt.
 */
static void bu21013_free_irq(struct bu21013_ts *ts)
{
	ts->touch_stopped = true;
	wake_up(&ts->wait);
	free_irq(ts->irq, ts);
}

#ifdef CONFIG_OF
static const struct bu21013_platform_device *
bu21013_parse_dt(struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct bu21013_platform_device *pdata;

	if (!np) {
		dev_err(dev, "no device tree or platform data\n");
		return ERR_PTR(-EINVAL);
	}

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	pdata->y_flip = pdata->x_flip = false;

	pdata->x_flip = of_property_read_bool(np, "rohm,flip-x");
	pdata->y_flip = of_property_read_bool(np, "rohm,flip-y");

	of_property_read_u32(np, "rohm,touch-max-x", &pdata->touch_x_max);
	of_property_read_u32(np, "rohm,touch-max-y", &pdata->touch_y_max);

	pdata->ext_clk = false;

	return pdata;
}
#else
static inline const struct bu21013_platform_device *
bu21013_parse_dt(struct device *dev)
{
	dev_err(dev, "no platform data available\n");
	return ERR_PTR(-EINVAL);
}
#endif

static int bu21013_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	const struct bu21013_platform_device *pdata =
					dev_get_platdata(&client->dev);
	struct bu21013_ts *ts;
	struct input_dev *in_dev;
	int error;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&client->dev, "i2c smbus byte data not supported\n");
		return -EIO;
	}

	if (!pdata) {
		pdata = bu21013_parse_dt(&client->dev);
		if (IS_ERR(pdata))
			return PTR_ERR(pdata);
	}

	ts = kzalloc(sizeof(*ts), GFP_KERNEL);
	in_dev = input_allocate_device();
	if (!ts || !in_dev) {
		dev_err(&client->dev, "device memory alloc failed\n");
		error = -ENOMEM;
		goto err_free_mem;
	}

	/* Named "INT" on the chip, DT binding is "touch" */
	ts->int_gpiod = gpiod_get(&client->dev, "touch", GPIOD_IN);
	error = PTR_ERR_OR_ZERO(ts->int_gpiod);
	if (error) {
		if (error != -EPROBE_DEFER)
			dev_err(&client->dev, "failed to get INT GPIO\n");
		goto err_free_mem;
	}
	gpiod_set_consumer_name(ts->int_gpiod, "BU21013 INT");

	ts->in_dev = in_dev;
	ts->chip = pdata;
	ts->client = client;
	ts->irq = gpiod_to_irq(ts->int_gpiod);

	ts->regulator = regulator_get(&client->dev, "avdd");
	if (IS_ERR(ts->regulator)) {
		dev_err(&client->dev, "regulator_get failed\n");
		error = PTR_ERR(ts->regulator);
		goto err_put_int_gpio;
	}

	error = regulator_enable(ts->regulator);
	if (error < 0) {
		dev_err(&client->dev, "regulator enable failed\n");
		goto err_put_regulator;
	}

	ts->touch_stopped = false;
	init_waitqueue_head(&ts->wait);

	/* Named "CS" on the chip, DT binding is "reset" */
	ts->cs_gpiod = gpiod_get(&client->dev, "reset", GPIOD_OUT_HIGH);
	error = PTR_ERR_OR_ZERO(ts->cs_gpiod);
	if (error) {
		if (error != -EPROBE_DEFER)
			dev_err(&client->dev, "failed to get CS GPIO\n");
		goto err_disable_regulator;
	}
	gpiod_set_consumer_name(ts->cs_gpiod, "BU21013 CS");

	/* configure the touch panel controller */
	error = bu21013_init_chip(ts);
	if (error) {
		dev_err(&client->dev, "error in bu21013 config\n");
		goto err_cs_disable;
	}

	/* register the device to input subsystem */
	in_dev->name = DRIVER_TP;
	in_dev->id.bustype = BUS_I2C;
	in_dev->dev.parent = &client->dev;

	__set_bit(EV_SYN, in_dev->evbit);
	__set_bit(EV_KEY, in_dev->evbit);
	__set_bit(EV_ABS, in_dev->evbit);

	input_set_abs_params(in_dev, ABS_MT_POSITION_X,
			     0, pdata->touch_x_max, 0, 0);
	input_set_abs_params(in_dev, ABS_MT_POSITION_Y,
			     0, pdata->touch_y_max, 0, 0);
	input_set_drvdata(in_dev, ts);

	error = request_threaded_irq(ts->irq, NULL, bu21013_gpio_irq,
				     IRQF_TRIGGER_FALLING | IRQF_SHARED |
					IRQF_ONESHOT,
				     DRIVER_TP, ts);
	if (error) {
		dev_err(&client->dev, "request irq %d failed\n",
			ts->irq);
		goto err_cs_disable;
	}

	error = input_register_device(in_dev);
	if (error) {
		dev_err(&client->dev, "failed to register input device\n");
		goto err_free_irq;
	}

	device_init_wakeup(&client->dev, pdata->wakeup);
	i2c_set_clientdata(client, ts);

	return 0;

err_free_irq:
	bu21013_free_irq(ts);
err_cs_disable:
	gpiod_set_value(ts->cs_gpiod, 0);
	gpiod_put(ts->cs_gpiod);
err_disable_regulator:
	regulator_disable(ts->regulator);
err_put_regulator:
	regulator_put(ts->regulator);
err_put_int_gpio:
	gpiod_put(ts->int_gpiod);
err_free_mem:
	input_free_device(in_dev);
	kfree(ts);

	return error;
}

static int bu21013_remove(struct i2c_client *client)
{
	struct bu21013_ts *ts = i2c_get_clientdata(client);

	bu21013_free_irq(ts);

	gpiod_set_value(ts->cs_gpiod, 0);
	gpiod_put(ts->cs_gpiod);

	input_unregister_device(ts->in_dev);

	regulator_disable(ts->regulator);
	regulator_put(ts->regulator);

	gpiod_put(ts->int_gpiod);

	kfree(ts);

	return 0;
}

static int __maybe_unused bu21013_suspend(struct device *dev)
{
	struct bu21013_ts *ts = dev_get_drvdata(dev);
	struct i2c_client *client = ts->client;

	ts->touch_stopped = true;
	if (device_may_wakeup(&client->dev))
		enable_irq_wake(ts->irq);
	else
		disable_irq(ts->irq);

	regulator_disable(ts->regulator);

	return 0;
}

static int __maybe_unused bu21013_resume(struct device *dev)
{
	struct bu21013_ts *ts = dev_get_drvdata(dev);
	struct i2c_client *client = ts->client;
	int retval;

	retval = regulator_enable(ts->regulator);
	if (retval < 0) {
		dev_err(&client->dev, "bu21013 regulator enable failed\n");
		return retval;
	}

	retval = bu21013_init_chip(ts);
	if (retval < 0) {
		dev_err(&client->dev, "bu21013 controller config failed\n");
		return retval;
	}

	ts->touch_stopped = false;

	if (device_may_wakeup(&client->dev))
		disable_irq_wake(ts->irq);
	else
		enable_irq(ts->irq);

	return 0;
}

static SIMPLE_DEV_PM_OPS(bu21013_dev_pm_ops, bu21013_suspend, bu21013_resume);

static const struct i2c_device_id bu21013_id[] = {
	{ DRIVER_TP, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, bu21013_id);

static struct i2c_driver bu21013_driver = {
	.driver	= {
		.name	=	DRIVER_TP,
		.pm	=	&bu21013_dev_pm_ops,
	},
	.probe		=	bu21013_probe,
	.remove		=	bu21013_remove,
	.id_table	=	bu21013_id,
};

module_i2c_driver(bu21013_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Naveen Kumar G <naveen.gaddipati@stericsson.com>");
MODULE_DESCRIPTION("bu21013 touch screen controller driver");
