// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for AUO in-cell touchscreens
 *
 * Copyright (c) 2011 Heiko Stuebner <heiko@sntech.de>
 *
 * loosely based on auo_touch.c from Dell Streak vendor-kernel
 *
 * Copyright (c) 2008 QUALCOMM Incorporated.
 * Copyright (c) 2008 QUALCOMM USA, INC.
 */

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/property.h>

/*
 * Coordinate calculation:
 * X1 = X1_LSB + X1_MSB*256
 * Y1 = Y1_LSB + Y1_MSB*256
 * X2 = X2_LSB + X2_MSB*256
 * Y2 = Y2_LSB + Y2_MSB*256
 */
#define AUO_PIXCIR_REG_X1_LSB		0x00
#define AUO_PIXCIR_REG_X1_MSB		0x01
#define AUO_PIXCIR_REG_Y1_LSB		0x02
#define AUO_PIXCIR_REG_Y1_MSB		0x03
#define AUO_PIXCIR_REG_X2_LSB		0x04
#define AUO_PIXCIR_REG_X2_MSB		0x05
#define AUO_PIXCIR_REG_Y2_LSB		0x06
#define AUO_PIXCIR_REG_Y2_MSB		0x07

#define AUO_PIXCIR_REG_STRENGTH		0x0d
#define AUO_PIXCIR_REG_STRENGTH_X1_LSB	0x0e
#define AUO_PIXCIR_REG_STRENGTH_X1_MSB	0x0f

#define AUO_PIXCIR_REG_RAW_DATA_X	0x2b
#define AUO_PIXCIR_REG_RAW_DATA_Y	0x4f

#define AUO_PIXCIR_REG_X_SENSITIVITY	0x6f
#define AUO_PIXCIR_REG_Y_SENSITIVITY	0x70
#define AUO_PIXCIR_REG_INT_SETTING	0x71
#define AUO_PIXCIR_REG_INT_WIDTH	0x72
#define AUO_PIXCIR_REG_POWER_MODE	0x73

#define AUO_PIXCIR_REG_VERSION		0x77
#define AUO_PIXCIR_REG_CALIBRATE	0x78

#define AUO_PIXCIR_REG_TOUCHAREA_X1	0x1e
#define AUO_PIXCIR_REG_TOUCHAREA_Y1	0x1f
#define AUO_PIXCIR_REG_TOUCHAREA_X2	0x20
#define AUO_PIXCIR_REG_TOUCHAREA_Y2	0x21

#define AUO_PIXCIR_REG_EEPROM_CALIB_X	0x42
#define AUO_PIXCIR_REG_EEPROM_CALIB_Y	0xad

#define AUO_PIXCIR_INT_TPNUM_MASK	0xe0
#define AUO_PIXCIR_INT_TPNUM_SHIFT	5
#define AUO_PIXCIR_INT_RELEASE		(1 << 4)
#define AUO_PIXCIR_INT_ENABLE		(1 << 3)
#define AUO_PIXCIR_INT_POL_HIGH		(1 << 2)

/*
 * Interrupt modes:
 * periodical:		interrupt is asserted periodicaly
 * compare coordinates:	interrupt is asserted when coordinates change
 * indicate touch:	interrupt is asserted during touch
 */
#define AUO_PIXCIR_INT_PERIODICAL	0x00
#define AUO_PIXCIR_INT_COMP_COORD	0x01
#define AUO_PIXCIR_INT_TOUCH_IND	0x02
#define AUO_PIXCIR_INT_MODE_MASK	0x03

/*
 * Power modes:
 * active:	scan speed 60Hz
 * sleep:	scan speed 10Hz can be auto-activated, wakeup on 1st touch
 * deep sleep:	scan speed 1Hz can only be entered or left manually.
 */
#define AUO_PIXCIR_POWER_ACTIVE		0x00
#define AUO_PIXCIR_POWER_SLEEP		0x01
#define AUO_PIXCIR_POWER_DEEP_SLEEP	0x02
#define AUO_PIXCIR_POWER_MASK		0x03

#define AUO_PIXCIR_POWER_ALLOW_SLEEP	(1 << 2)
#define AUO_PIXCIR_POWER_IDLE_TIME(ms)	((ms & 0xf) << 4)

#define AUO_PIXCIR_CALIBRATE		0x03

#define AUO_PIXCIR_EEPROM_CALIB_X_LEN	62
#define AUO_PIXCIR_EEPROM_CALIB_Y_LEN	36

#define AUO_PIXCIR_RAW_DATA_X_LEN	18
#define AUO_PIXCIR_RAW_DATA_Y_LEN	11

#define AUO_PIXCIR_STRENGTH_ENABLE	(1 << 0)

/* Touchscreen absolute values */
#define AUO_PIXCIR_REPORT_POINTS	2
#define AUO_PIXCIR_MAX_AREA		0xff
#define AUO_PIXCIR_PENUP_TIMEOUT_MS	10

struct auo_pixcir_ts {
	struct i2c_client	*client;
	struct input_dev	*input;
	struct gpio_desc	*gpio_int;
	struct gpio_desc	*gpio_rst;
	char			phys[32];

	unsigned int		x_max;
	unsigned int		y_max;

	/* special handling for touch_indicate interrupt mode */
	bool			touch_ind_mode;

	wait_queue_head_t	wait;
	bool			stopped;
};

struct auo_point_t {
	int	coord_x;
	int	coord_y;
	int	area_major;
	int	area_minor;
	int	orientation;
};

static int auo_pixcir_collect_data(struct auo_pixcir_ts *ts,
				   struct auo_point_t *point)
{
	struct i2c_client *client = ts->client;
	uint8_t raw_coord[8];
	uint8_t raw_area[4];
	int i, ret;

	/* touch coordinates */
	ret = i2c_smbus_read_i2c_block_data(client, AUO_PIXCIR_REG_X1_LSB,
					    8, raw_coord);
	if (ret < 0) {
		dev_err(&client->dev, "failed to read coordinate, %d\n", ret);
		return ret;
	}

	/* touch area */
	ret = i2c_smbus_read_i2c_block_data(client, AUO_PIXCIR_REG_TOUCHAREA_X1,
					    4, raw_area);
	if (ret < 0) {
		dev_err(&client->dev, "could not read touch area, %d\n", ret);
		return ret;
	}

	for (i = 0; i < AUO_PIXCIR_REPORT_POINTS; i++) {
		point[i].coord_x =
			raw_coord[4 * i + 1] << 8 | raw_coord[4 * i];
		point[i].coord_y =
			raw_coord[4 * i + 3] << 8 | raw_coord[4 * i + 2];

		if (point[i].coord_x > ts->x_max ||
		    point[i].coord_y > ts->y_max) {
			dev_warn(&client->dev, "coordinates (%d,%d) invalid\n",
				point[i].coord_x, point[i].coord_y);
			point[i].coord_x = point[i].coord_y = 0;
		}

		/* determine touch major, minor and orientation */
		point[i].area_major = max(raw_area[2 * i], raw_area[2 * i + 1]);
		point[i].area_minor = min(raw_area[2 * i], raw_area[2 * i + 1]);
		point[i].orientation = raw_area[2 * i] > raw_area[2 * i + 1];
	}

	return 0;
}

static irqreturn_t auo_pixcir_interrupt(int irq, void *dev_id)
{
	struct auo_pixcir_ts *ts = dev_id;
	struct auo_point_t point[AUO_PIXCIR_REPORT_POINTS];
	int i;
	int ret;
	int fingers = 0;
	int abs = -1;

	while (!ts->stopped) {

		/* check for up event in touch touch_ind_mode */
		if (ts->touch_ind_mode) {
			if (gpiod_get_value_cansleep(ts->gpio_int) == 0) {
				input_mt_sync(ts->input);
				input_report_key(ts->input, BTN_TOUCH, 0);
				input_sync(ts->input);
				break;
			}
		}

		ret = auo_pixcir_collect_data(ts, point);
		if (ret < 0) {
			/* we want to loop only in touch_ind_mode */
			if (!ts->touch_ind_mode)
				break;

			wait_event_timeout(ts->wait, ts->stopped,
				msecs_to_jiffies(AUO_PIXCIR_PENUP_TIMEOUT_MS));
			continue;
		}

		for (i = 0; i < AUO_PIXCIR_REPORT_POINTS; i++) {
			if (point[i].coord_x > 0 || point[i].coord_y > 0) {
				input_report_abs(ts->input, ABS_MT_POSITION_X,
						 point[i].coord_x);
				input_report_abs(ts->input, ABS_MT_POSITION_Y,
						 point[i].coord_y);
				input_report_abs(ts->input, ABS_MT_TOUCH_MAJOR,
						 point[i].area_major);
				input_report_abs(ts->input, ABS_MT_TOUCH_MINOR,
						 point[i].area_minor);
				input_report_abs(ts->input, ABS_MT_ORIENTATION,
						 point[i].orientation);
				input_mt_sync(ts->input);

				/* use first finger as source for singletouch */
				if (fingers == 0)
					abs = i;

				/* number of touch points could also be queried
				 * via i2c but would require an additional call
				 */
				fingers++;
			}
		}

		input_report_key(ts->input, BTN_TOUCH, fingers > 0);

		if (abs > -1) {
			input_report_abs(ts->input, ABS_X, point[abs].coord_x);
			input_report_abs(ts->input, ABS_Y, point[abs].coord_y);
		}

		input_sync(ts->input);

		/* we want to loop only in touch_ind_mode */
		if (!ts->touch_ind_mode)
			break;

		wait_event_timeout(ts->wait, ts->stopped,
				 msecs_to_jiffies(AUO_PIXCIR_PENUP_TIMEOUT_MS));
	}

	return IRQ_HANDLED;
}

/*
 * Set the power mode of the device.
 * Valid modes are
 * - AUO_PIXCIR_POWER_ACTIVE
 * - AUO_PIXCIR_POWER_SLEEP - automatically left on first touch
 * - AUO_PIXCIR_POWER_DEEP_SLEEP
 */
static int auo_pixcir_power_mode(struct auo_pixcir_ts *ts, int mode)
{
	struct i2c_client *client = ts->client;
	int ret;

	ret = i2c_smbus_read_byte_data(client, AUO_PIXCIR_REG_POWER_MODE);
	if (ret < 0) {
		dev_err(&client->dev, "unable to read reg %Xh, %d\n",
			AUO_PIXCIR_REG_POWER_MODE, ret);
		return ret;
	}

	ret &= ~AUO_PIXCIR_POWER_MASK;
	ret |= mode;

	ret = i2c_smbus_write_byte_data(client, AUO_PIXCIR_REG_POWER_MODE, ret);
	if (ret) {
		dev_err(&client->dev, "unable to write reg %Xh, %d\n",
			AUO_PIXCIR_REG_POWER_MODE, ret);
		return ret;
	}

	return 0;
}

static int auo_pixcir_int_config(struct auo_pixcir_ts *ts, int int_setting)
{
	struct i2c_client *client = ts->client;
	int ret;

	ret = i2c_smbus_read_byte_data(client, AUO_PIXCIR_REG_INT_SETTING);
	if (ret < 0) {
		dev_err(&client->dev, "unable to read reg %Xh, %d\n",
			AUO_PIXCIR_REG_INT_SETTING, ret);
		return ret;
	}

	ret &= ~AUO_PIXCIR_INT_MODE_MASK;
	ret |= int_setting;
	ret |= AUO_PIXCIR_INT_POL_HIGH; /* always use high for interrupts */

	ret = i2c_smbus_write_byte_data(client, AUO_PIXCIR_REG_INT_SETTING,
					ret);
	if (ret < 0) {
		dev_err(&client->dev, "unable to write reg %Xh, %d\n",
			AUO_PIXCIR_REG_INT_SETTING, ret);
		return ret;
	}

	ts->touch_ind_mode = int_setting == AUO_PIXCIR_INT_TOUCH_IND;

	return 0;
}

/* control the generation of interrupts on the device side */
static int auo_pixcir_int_toggle(struct auo_pixcir_ts *ts, bool enable)
{
	struct i2c_client *client = ts->client;
	int ret;

	ret = i2c_smbus_read_byte_data(client, AUO_PIXCIR_REG_INT_SETTING);
	if (ret < 0) {
		dev_err(&client->dev, "unable to read reg %Xh, %d\n",
			AUO_PIXCIR_REG_INT_SETTING, ret);
		return ret;
	}

	if (enable)
		ret |= AUO_PIXCIR_INT_ENABLE;
	else
		ret &= ~AUO_PIXCIR_INT_ENABLE;

	ret = i2c_smbus_write_byte_data(client, AUO_PIXCIR_REG_INT_SETTING,
					ret);
	if (ret < 0) {
		dev_err(&client->dev, "unable to write reg %Xh, %d\n",
			AUO_PIXCIR_REG_INT_SETTING, ret);
		return ret;
	}

	return 0;
}

static int auo_pixcir_start(struct auo_pixcir_ts *ts)
{
	struct i2c_client *client = ts->client;
	int ret;

	ret = auo_pixcir_power_mode(ts, AUO_PIXCIR_POWER_ACTIVE);
	if (ret < 0) {
		dev_err(&client->dev, "could not set power mode, %d\n",
			ret);
		return ret;
	}

	ts->stopped = false;
	mb();
	enable_irq(client->irq);

	ret = auo_pixcir_int_toggle(ts, 1);
	if (ret < 0) {
		dev_err(&client->dev, "could not enable interrupt, %d\n",
			ret);
		disable_irq(client->irq);
		return ret;
	}

	return 0;
}

static int auo_pixcir_stop(struct auo_pixcir_ts *ts)
{
	struct i2c_client *client = ts->client;
	int ret;

	ret = auo_pixcir_int_toggle(ts, 0);
	if (ret < 0) {
		dev_err(&client->dev, "could not disable interrupt, %d\n",
			ret);
		return ret;
	}

	/* disable receiving of interrupts */
	disable_irq(client->irq);
	ts->stopped = true;
	mb();
	wake_up(&ts->wait);

	return auo_pixcir_power_mode(ts, AUO_PIXCIR_POWER_DEEP_SLEEP);
}

static int auo_pixcir_input_open(struct input_dev *dev)
{
	struct auo_pixcir_ts *ts = input_get_drvdata(dev);

	return auo_pixcir_start(ts);
}

static void auo_pixcir_input_close(struct input_dev *dev)
{
	struct auo_pixcir_ts *ts = input_get_drvdata(dev);

	auo_pixcir_stop(ts);
}

static int auo_pixcir_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct auo_pixcir_ts *ts = i2c_get_clientdata(client);
	struct input_dev *input = ts->input;
	int ret = 0;

	mutex_lock(&input->mutex);

	/* when configured as wakeup source, device should always wake system
	 * therefore start device if necessary
	 */
	if (device_may_wakeup(&client->dev)) {
		/* need to start device if not open, to be wakeup source */
		if (!input_device_enabled(input)) {
			ret = auo_pixcir_start(ts);
			if (ret)
				goto unlock;
		}

		enable_irq_wake(client->irq);
		ret = auo_pixcir_power_mode(ts, AUO_PIXCIR_POWER_SLEEP);
	} else if (input_device_enabled(input)) {
		ret = auo_pixcir_stop(ts);
	}

unlock:
	mutex_unlock(&input->mutex);

	return ret;
}

static int auo_pixcir_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct auo_pixcir_ts *ts = i2c_get_clientdata(client);
	struct input_dev *input = ts->input;
	int ret = 0;

	mutex_lock(&input->mutex);

	if (device_may_wakeup(&client->dev)) {
		disable_irq_wake(client->irq);

		/* need to stop device if it was not open on suspend */
		if (!input_device_enabled(input)) {
			ret = auo_pixcir_stop(ts);
			if (ret)
				goto unlock;
		}

		/* device wakes automatically from SLEEP */
	} else if (input_device_enabled(input)) {
		ret = auo_pixcir_start(ts);
	}

unlock:
	mutex_unlock(&input->mutex);

	return ret;
}

static DEFINE_SIMPLE_DEV_PM_OPS(auo_pixcir_pm_ops,
				auo_pixcir_suspend, auo_pixcir_resume);

static void auo_pixcir_reset(void *data)
{
	struct auo_pixcir_ts *ts = data;

	gpiod_set_value_cansleep(ts->gpio_rst, 1);
}

static int auo_pixcir_probe(struct i2c_client *client)
{
	struct auo_pixcir_ts *ts;
	struct input_dev *input_dev;
	int version;
	int error;

	ts = devm_kzalloc(&client->dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	input_dev = devm_input_allocate_device(&client->dev);
	if (!input_dev) {
		dev_err(&client->dev, "could not allocate input device\n");
		return -ENOMEM;
	}

	ts->client = client;
	ts->input = input_dev;
	ts->touch_ind_mode = 0;
	ts->stopped = true;
	init_waitqueue_head(&ts->wait);

	snprintf(ts->phys, sizeof(ts->phys),
		 "%s/input0", dev_name(&client->dev));

	if (device_property_read_u32(&client->dev, "x-size", &ts->x_max)) {
		dev_err(&client->dev, "failed to get x-size property\n");
		return -EINVAL;
	}

	if (device_property_read_u32(&client->dev, "y-size", &ts->y_max)) {
		dev_err(&client->dev, "failed to get y-size property\n");
		return -EINVAL;
	}

	input_dev->name = "AUO-Pixcir touchscreen";
	input_dev->phys = ts->phys;
	input_dev->id.bustype = BUS_I2C;

	input_dev->open = auo_pixcir_input_open;
	input_dev->close = auo_pixcir_input_close;

	__set_bit(EV_ABS, input_dev->evbit);
	__set_bit(EV_KEY, input_dev->evbit);

	__set_bit(BTN_TOUCH, input_dev->keybit);

	/* For single touch */
	input_set_abs_params(input_dev, ABS_X, 0, ts->x_max, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, ts->y_max, 0, 0);

	/* For multi touch */
	input_set_abs_params(input_dev, ABS_MT_POSITION_X, 0, ts->x_max, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_Y, 0, ts->y_max, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR,
			     0, AUO_PIXCIR_MAX_AREA, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_TOUCH_MINOR,
			     0, AUO_PIXCIR_MAX_AREA, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_ORIENTATION, 0, 1, 0, 0);

	input_set_drvdata(ts->input, ts);

	ts->gpio_int = devm_gpiod_get_index(&client->dev, NULL, 0, GPIOD_IN);
	error = PTR_ERR_OR_ZERO(ts->gpio_int);
	if (error) {
		dev_err(&client->dev,
			"request of int gpio failed: %d\n", error);
		return error;
	}

	gpiod_set_consumer_name(ts->gpio_int, "auo_pixcir_ts_int");

	/* Take the chip out of reset */
	ts->gpio_rst = devm_gpiod_get_index(&client->dev, NULL, 1,
					    GPIOD_OUT_LOW);
	error = PTR_ERR_OR_ZERO(ts->gpio_rst);
	if (error) {
		dev_err(&client->dev,
			"request of reset gpio failed: %d\n", error);
		return error;
	}

	gpiod_set_consumer_name(ts->gpio_rst, "auo_pixcir_ts_rst");

	error = devm_add_action_or_reset(&client->dev, auo_pixcir_reset, ts);
	if (error) {
		dev_err(&client->dev, "failed to register reset action, %d\n",
			error);
		return error;
	}

	msleep(200);

	version = i2c_smbus_read_byte_data(client, AUO_PIXCIR_REG_VERSION);
	if (version < 0) {
		error = version;
		return error;
	}

	dev_info(&client->dev, "firmware version 0x%X\n", version);

	/* default to asserting the interrupt when the screen is touched */
	error = auo_pixcir_int_config(ts, AUO_PIXCIR_INT_TOUCH_IND);
	if (error)
		return error;

	error = devm_request_threaded_irq(&client->dev, client->irq,
					  NULL, auo_pixcir_interrupt,
					  IRQF_ONESHOT,
					  input_dev->name, ts);
	if (error) {
		dev_err(&client->dev, "irq %d requested failed, %d\n",
			client->irq, error);
		return error;
	}

	/* stop device and put it into deep sleep until it is opened */
	error = auo_pixcir_stop(ts);
	if (error)
		return error;

	error = input_register_device(input_dev);
	if (error) {
		dev_err(&client->dev, "could not register input device, %d\n",
			error);
		return error;
	}

	i2c_set_clientdata(client, ts);

	return 0;
}

static const struct i2c_device_id auo_pixcir_idtable[] = {
	{ "auo_pixcir_ts" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, auo_pixcir_idtable);

#ifdef CONFIG_OF
static const struct of_device_id auo_pixcir_ts_dt_idtable[] = {
	{ .compatible = "auo,auo_pixcir_ts" },
	{},
};
MODULE_DEVICE_TABLE(of, auo_pixcir_ts_dt_idtable);
#endif

static struct i2c_driver auo_pixcir_driver = {
	.driver = {
		.name	= "auo_pixcir_ts",
		.pm	= pm_sleep_ptr(&auo_pixcir_pm_ops),
		.of_match_table	= of_match_ptr(auo_pixcir_ts_dt_idtable),
	},
	.probe		= auo_pixcir_probe,
	.id_table	= auo_pixcir_idtable,
};

module_i2c_driver(auo_pixcir_driver);

MODULE_DESCRIPTION("AUO-PIXCIR touchscreen driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Heiko Stuebner <heiko@sntech.de>");
