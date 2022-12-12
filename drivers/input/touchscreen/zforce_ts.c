// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012-2013 MundoReader S.L.
 * Author: Heiko Stuebner <heiko@sntech.de>
 *
 * based in parts on Nook zforce driver
 *
 * Copyright (C) 2010 Barnes & Noble, Inc.
 * Author: Pieter Truter<ptruter@intrinsyc.com>
 */

#include <linux/module.h>
#include <linux/hrtimer.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/input/mt.h>
#include <linux/platform_data/zforce_ts.h>
#include <linux/regulator/consumer.h>
#include <linux/of.h>

#define WAIT_TIMEOUT		msecs_to_jiffies(1000)

#define FRAME_START		0xee
#define FRAME_MAXSIZE		257

/* Offsets of the different parts of the payload the controller sends */
#define PAYLOAD_HEADER		0
#define PAYLOAD_LENGTH		1
#define PAYLOAD_BODY		2

/* Response offsets */
#define RESPONSE_ID		0
#define RESPONSE_DATA		1

/* Commands */
#define COMMAND_DEACTIVATE	0x00
#define COMMAND_INITIALIZE	0x01
#define COMMAND_RESOLUTION	0x02
#define COMMAND_SETCONFIG	0x03
#define COMMAND_DATAREQUEST	0x04
#define COMMAND_SCANFREQ	0x08
#define COMMAND_STATUS		0X1e

/*
 * Responses the controller sends as a result of
 * command requests
 */
#define RESPONSE_DEACTIVATE	0x00
#define RESPONSE_INITIALIZE	0x01
#define RESPONSE_RESOLUTION	0x02
#define RESPONSE_SETCONFIG	0x03
#define RESPONSE_SCANFREQ	0x08
#define RESPONSE_STATUS		0X1e

/*
 * Notifications are sent by the touch controller without
 * being requested by the driver and include for example
 * touch indications
 */
#define NOTIFICATION_TOUCH		0x04
#define NOTIFICATION_BOOTCOMPLETE	0x07
#define NOTIFICATION_OVERRUN		0x25
#define NOTIFICATION_PROXIMITY		0x26
#define NOTIFICATION_INVALID_COMMAND	0xfe

#define ZFORCE_REPORT_POINTS		2
#define ZFORCE_MAX_AREA			0xff

#define STATE_DOWN			0
#define STATE_MOVE			1
#define STATE_UP			2

#define SETCONFIG_DUALTOUCH		(1 << 0)

struct zforce_point {
	int coord_x;
	int coord_y;
	int state;
	int id;
	int area_major;
	int area_minor;
	int orientation;
	int pressure;
	int prblty;
};

/*
 * @client		the i2c_client
 * @input		the input device
 * @suspending		in the process of going to suspend (don't emit wakeup
 *			events for commands executed to suspend the device)
 * @suspended		device suspended
 * @access_mutex	serialize i2c-access, to keep multipart reads together
 * @command_done	completion to wait for the command result
 * @command_mutex	serialize commands sent to the ic
 * @command_waiting	the id of the command that is currently waiting
 *			for a result
 * @command_result	returned result of the command
 */
struct zforce_ts {
	struct i2c_client	*client;
	struct input_dev	*input;
	const struct zforce_ts_platdata *pdata;
	char			phys[32];

	struct regulator	*reg_vdd;

	struct gpio_desc	*gpio_int;
	struct gpio_desc	*gpio_rst;

	bool			suspending;
	bool			suspended;
	bool			boot_complete;

	/* Firmware version information */
	u16			version_major;
	u16			version_minor;
	u16			version_build;
	u16			version_rev;

	struct mutex		access_mutex;

	struct completion	command_done;
	struct mutex		command_mutex;
	int			command_waiting;
	int			command_result;
};

static int zforce_command(struct zforce_ts *ts, u8 cmd)
{
	struct i2c_client *client = ts->client;
	char buf[3];
	int ret;

	dev_dbg(&client->dev, "%s: 0x%x\n", __func__, cmd);

	buf[0] = FRAME_START;
	buf[1] = 1; /* data size, command only */
	buf[2] = cmd;

	mutex_lock(&ts->access_mutex);
	ret = i2c_master_send(client, &buf[0], ARRAY_SIZE(buf));
	mutex_unlock(&ts->access_mutex);
	if (ret < 0) {
		dev_err(&client->dev, "i2c send data request error: %d\n", ret);
		return ret;
	}

	return 0;
}

static void zforce_reset_assert(struct zforce_ts *ts)
{
	gpiod_set_value_cansleep(ts->gpio_rst, 1);
}

static void zforce_reset_deassert(struct zforce_ts *ts)
{
	gpiod_set_value_cansleep(ts->gpio_rst, 0);
}

static int zforce_send_wait(struct zforce_ts *ts, const char *buf, int len)
{
	struct i2c_client *client = ts->client;
	int ret;

	ret = mutex_trylock(&ts->command_mutex);
	if (!ret) {
		dev_err(&client->dev, "already waiting for a command\n");
		return -EBUSY;
	}

	dev_dbg(&client->dev, "sending %d bytes for command 0x%x\n",
		buf[1], buf[2]);

	ts->command_waiting = buf[2];

	mutex_lock(&ts->access_mutex);
	ret = i2c_master_send(client, buf, len);
	mutex_unlock(&ts->access_mutex);
	if (ret < 0) {
		dev_err(&client->dev, "i2c send data request error: %d\n", ret);
		goto unlock;
	}

	dev_dbg(&client->dev, "waiting for result for command 0x%x\n", buf[2]);

	if (wait_for_completion_timeout(&ts->command_done, WAIT_TIMEOUT) == 0) {
		ret = -ETIME;
		goto unlock;
	}

	ret = ts->command_result;

unlock:
	mutex_unlock(&ts->command_mutex);
	return ret;
}

static int zforce_command_wait(struct zforce_ts *ts, u8 cmd)
{
	struct i2c_client *client = ts->client;
	char buf[3];
	int ret;

	dev_dbg(&client->dev, "%s: 0x%x\n", __func__, cmd);

	buf[0] = FRAME_START;
	buf[1] = 1; /* data size, command only */
	buf[2] = cmd;

	ret = zforce_send_wait(ts, &buf[0], ARRAY_SIZE(buf));
	if (ret < 0) {
		dev_err(&client->dev, "i2c send data request error: %d\n", ret);
		return ret;
	}

	return 0;
}

static int zforce_resolution(struct zforce_ts *ts, u16 x, u16 y)
{
	struct i2c_client *client = ts->client;
	char buf[7] = { FRAME_START, 5, COMMAND_RESOLUTION,
			(x & 0xff), ((x >> 8) & 0xff),
			(y & 0xff), ((y >> 8) & 0xff) };

	dev_dbg(&client->dev, "set resolution to (%d,%d)\n", x, y);

	return zforce_send_wait(ts, &buf[0], ARRAY_SIZE(buf));
}

static int zforce_scan_frequency(struct zforce_ts *ts, u16 idle, u16 finger,
				 u16 stylus)
{
	struct i2c_client *client = ts->client;
	char buf[9] = { FRAME_START, 7, COMMAND_SCANFREQ,
			(idle & 0xff), ((idle >> 8) & 0xff),
			(finger & 0xff), ((finger >> 8) & 0xff),
			(stylus & 0xff), ((stylus >> 8) & 0xff) };

	dev_dbg(&client->dev,
		"set scan frequency to (idle: %d, finger: %d, stylus: %d)\n",
		idle, finger, stylus);

	return zforce_send_wait(ts, &buf[0], ARRAY_SIZE(buf));
}

static int zforce_setconfig(struct zforce_ts *ts, char b1)
{
	struct i2c_client *client = ts->client;
	char buf[7] = { FRAME_START, 5, COMMAND_SETCONFIG,
			b1, 0, 0, 0 };

	dev_dbg(&client->dev, "set config to (%d)\n", b1);

	return zforce_send_wait(ts, &buf[0], ARRAY_SIZE(buf));
}

static int zforce_start(struct zforce_ts *ts)
{
	struct i2c_client *client = ts->client;
	const struct zforce_ts_platdata *pdata = ts->pdata;
	int ret;

	dev_dbg(&client->dev, "starting device\n");

	ret = zforce_command_wait(ts, COMMAND_INITIALIZE);
	if (ret) {
		dev_err(&client->dev, "Unable to initialize, %d\n", ret);
		return ret;
	}

	ret = zforce_resolution(ts, pdata->x_max, pdata->y_max);
	if (ret) {
		dev_err(&client->dev, "Unable to set resolution, %d\n", ret);
		goto error;
	}

	ret = zforce_scan_frequency(ts, 10, 50, 50);
	if (ret) {
		dev_err(&client->dev, "Unable to set scan frequency, %d\n",
			ret);
		goto error;
	}

	ret = zforce_setconfig(ts, SETCONFIG_DUALTOUCH);
	if (ret) {
		dev_err(&client->dev, "Unable to set config\n");
		goto error;
	}

	/* start sending touch events */
	ret = zforce_command(ts, COMMAND_DATAREQUEST);
	if (ret) {
		dev_err(&client->dev, "Unable to request data\n");
		goto error;
	}

	/*
	 * Per NN, initial cal. take max. of 200msec.
	 * Allow time to complete this calibration
	 */
	msleep(200);

	return 0;

error:
	zforce_command_wait(ts, COMMAND_DEACTIVATE);
	return ret;
}

static int zforce_stop(struct zforce_ts *ts)
{
	struct i2c_client *client = ts->client;
	int ret;

	dev_dbg(&client->dev, "stopping device\n");

	/* Deactivates touch sensing and puts the device into sleep. */
	ret = zforce_command_wait(ts, COMMAND_DEACTIVATE);
	if (ret != 0) {
		dev_err(&client->dev, "could not deactivate device, %d\n",
			ret);
		return ret;
	}

	return 0;
}

static int zforce_touch_event(struct zforce_ts *ts, u8 *payload)
{
	struct i2c_client *client = ts->client;
	const struct zforce_ts_platdata *pdata = ts->pdata;
	struct zforce_point point;
	int count, i, num = 0;

	count = payload[0];
	if (count > ZFORCE_REPORT_POINTS) {
		dev_warn(&client->dev,
			 "too many coordinates %d, expected max %d\n",
			 count, ZFORCE_REPORT_POINTS);
		count = ZFORCE_REPORT_POINTS;
	}

	for (i = 0; i < count; i++) {
		point.coord_x =
			payload[9 * i + 2] << 8 | payload[9 * i + 1];
		point.coord_y =
			payload[9 * i + 4] << 8 | payload[9 * i + 3];

		if (point.coord_x > pdata->x_max ||
		    point.coord_y > pdata->y_max) {
			dev_warn(&client->dev, "coordinates (%d,%d) invalid\n",
				point.coord_x, point.coord_y);
			point.coord_x = point.coord_y = 0;
		}

		point.state = payload[9 * i + 5] & 0x0f;
		point.id = (payload[9 * i + 5] & 0xf0) >> 4;

		/* determine touch major, minor and orientation */
		point.area_major = max(payload[9 * i + 6],
					  payload[9 * i + 7]);
		point.area_minor = min(payload[9 * i + 6],
					  payload[9 * i + 7]);
		point.orientation = payload[9 * i + 6] > payload[9 * i + 7];

		point.pressure = payload[9 * i + 8];
		point.prblty = payload[9 * i + 9];

		dev_dbg(&client->dev,
			"point %d/%d: state %d, id %d, pressure %d, prblty %d, x %d, y %d, amajor %d, aminor %d, ori %d\n",
			i, count, point.state, point.id,
			point.pressure, point.prblty,
			point.coord_x, point.coord_y,
			point.area_major, point.area_minor,
			point.orientation);

		/* the zforce id starts with "1", so needs to be decreased */
		input_mt_slot(ts->input, point.id - 1);

		input_mt_report_slot_state(ts->input, MT_TOOL_FINGER,
						point.state != STATE_UP);

		if (point.state != STATE_UP) {
			input_report_abs(ts->input, ABS_MT_POSITION_X,
					 point.coord_x);
			input_report_abs(ts->input, ABS_MT_POSITION_Y,
					 point.coord_y);
			input_report_abs(ts->input, ABS_MT_TOUCH_MAJOR,
					 point.area_major);
			input_report_abs(ts->input, ABS_MT_TOUCH_MINOR,
					 point.area_minor);
			input_report_abs(ts->input, ABS_MT_ORIENTATION,
					 point.orientation);
			num++;
		}
	}

	input_mt_sync_frame(ts->input);

	input_mt_report_finger_count(ts->input, num);

	input_sync(ts->input);

	return 0;
}

static int zforce_read_packet(struct zforce_ts *ts, u8 *buf)
{
	struct i2c_client *client = ts->client;
	int ret;

	mutex_lock(&ts->access_mutex);

	/* read 2 byte message header */
	ret = i2c_master_recv(client, buf, 2);
	if (ret < 0) {
		dev_err(&client->dev, "error reading header: %d\n", ret);
		goto unlock;
	}

	if (buf[PAYLOAD_HEADER] != FRAME_START) {
		dev_err(&client->dev, "invalid frame start: %d\n", buf[0]);
		ret = -EIO;
		goto unlock;
	}

	if (buf[PAYLOAD_LENGTH] == 0) {
		dev_err(&client->dev, "invalid payload length: %d\n",
			buf[PAYLOAD_LENGTH]);
		ret = -EIO;
		goto unlock;
	}

	/* read the message */
	ret = i2c_master_recv(client, &buf[PAYLOAD_BODY], buf[PAYLOAD_LENGTH]);
	if (ret < 0) {
		dev_err(&client->dev, "error reading payload: %d\n", ret);
		goto unlock;
	}

	dev_dbg(&client->dev, "read %d bytes for response command 0x%x\n",
		buf[PAYLOAD_LENGTH], buf[PAYLOAD_BODY]);

unlock:
	mutex_unlock(&ts->access_mutex);
	return ret;
}

static void zforce_complete(struct zforce_ts *ts, int cmd, int result)
{
	struct i2c_client *client = ts->client;

	if (ts->command_waiting == cmd) {
		dev_dbg(&client->dev, "completing command 0x%x\n", cmd);
		ts->command_result = result;
		complete(&ts->command_done);
	} else {
		dev_dbg(&client->dev, "command %d not for us\n", cmd);
	}
}

static irqreturn_t zforce_irq(int irq, void *dev_id)
{
	struct zforce_ts *ts = dev_id;
	struct i2c_client *client = ts->client;

	if (ts->suspended && device_may_wakeup(&client->dev))
		pm_wakeup_event(&client->dev, 500);

	return IRQ_WAKE_THREAD;
}

static irqreturn_t zforce_irq_thread(int irq, void *dev_id)
{
	struct zforce_ts *ts = dev_id;
	struct i2c_client *client = ts->client;
	int ret;
	u8 payload_buffer[FRAME_MAXSIZE];
	u8 *payload;

	/*
	 * When still suspended, return.
	 * Due to the level-interrupt we will get re-triggered later.
	 */
	if (ts->suspended) {
		msleep(20);
		return IRQ_HANDLED;
	}

	dev_dbg(&client->dev, "handling interrupt\n");

	/* Don't emit wakeup events from commands run by zforce_suspend */
	if (!ts->suspending && device_may_wakeup(&client->dev))
		pm_stay_awake(&client->dev);

	/*
	 * Run at least once and exit the loop if
	 * - the optional interrupt GPIO isn't specified
	 *   (there is only one packet read per ISR invocation, then)
	 * or
	 * - the GPIO isn't active any more
	 *   (packet read until the level GPIO indicates that there is
	 *    no IRQ any more)
	 */
	do {
		ret = zforce_read_packet(ts, payload_buffer);
		if (ret < 0) {
			dev_err(&client->dev,
				"could not read packet, ret: %d\n", ret);
			break;
		}

		payload =  &payload_buffer[PAYLOAD_BODY];

		switch (payload[RESPONSE_ID]) {
		case NOTIFICATION_TOUCH:
			/*
			 * Always report touch-events received while
			 * suspending, when being a wakeup source
			 */
			if (ts->suspending && device_may_wakeup(&client->dev))
				pm_wakeup_event(&client->dev, 500);
			zforce_touch_event(ts, &payload[RESPONSE_DATA]);
			break;

		case NOTIFICATION_BOOTCOMPLETE:
			ts->boot_complete = payload[RESPONSE_DATA];
			zforce_complete(ts, payload[RESPONSE_ID], 0);
			break;

		case RESPONSE_INITIALIZE:
		case RESPONSE_DEACTIVATE:
		case RESPONSE_SETCONFIG:
		case RESPONSE_RESOLUTION:
		case RESPONSE_SCANFREQ:
			zforce_complete(ts, payload[RESPONSE_ID],
					payload[RESPONSE_DATA]);
			break;

		case RESPONSE_STATUS:
			/*
			 * Version Payload Results
			 * [2:major] [2:minor] [2:build] [2:rev]
			 */
			ts->version_major = (payload[RESPONSE_DATA + 1] << 8) |
						payload[RESPONSE_DATA];
			ts->version_minor = (payload[RESPONSE_DATA + 3] << 8) |
						payload[RESPONSE_DATA + 2];
			ts->version_build = (payload[RESPONSE_DATA + 5] << 8) |
						payload[RESPONSE_DATA + 4];
			ts->version_rev   = (payload[RESPONSE_DATA + 7] << 8) |
						payload[RESPONSE_DATA + 6];
			dev_dbg(&ts->client->dev,
				"Firmware Version %04x:%04x %04x:%04x\n",
				ts->version_major, ts->version_minor,
				ts->version_build, ts->version_rev);

			zforce_complete(ts, payload[RESPONSE_ID], 0);
			break;

		case NOTIFICATION_INVALID_COMMAND:
			dev_err(&ts->client->dev, "invalid command: 0x%x\n",
				payload[RESPONSE_DATA]);
			break;

		default:
			dev_err(&ts->client->dev,
				"unrecognized response id: 0x%x\n",
				payload[RESPONSE_ID]);
			break;
		}
	} while (gpiod_get_value_cansleep(ts->gpio_int));

	if (!ts->suspending && device_may_wakeup(&client->dev))
		pm_relax(&client->dev);

	dev_dbg(&client->dev, "finished interrupt\n");

	return IRQ_HANDLED;
}

static int zforce_input_open(struct input_dev *dev)
{
	struct zforce_ts *ts = input_get_drvdata(dev);

	return zforce_start(ts);
}

static void zforce_input_close(struct input_dev *dev)
{
	struct zforce_ts *ts = input_get_drvdata(dev);
	struct i2c_client *client = ts->client;
	int ret;

	ret = zforce_stop(ts);
	if (ret)
		dev_warn(&client->dev, "stopping zforce failed\n");

	return;
}

static int __maybe_unused zforce_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct zforce_ts *ts = i2c_get_clientdata(client);
	struct input_dev *input = ts->input;
	int ret = 0;

	mutex_lock(&input->mutex);
	ts->suspending = true;

	/*
	 * When configured as a wakeup source device should always wake
	 * the system, therefore start device if necessary.
	 */
	if (device_may_wakeup(&client->dev)) {
		dev_dbg(&client->dev, "suspend while being a wakeup source\n");

		/* Need to start device, if not open, to be a wakeup source. */
		if (!input_device_enabled(input)) {
			ret = zforce_start(ts);
			if (ret)
				goto unlock;
		}

		enable_irq_wake(client->irq);
	} else if (input_device_enabled(input)) {
		dev_dbg(&client->dev,
			"suspend without being a wakeup source\n");

		ret = zforce_stop(ts);
		if (ret)
			goto unlock;

		disable_irq(client->irq);
	}

	ts->suspended = true;

unlock:
	ts->suspending = false;
	mutex_unlock(&input->mutex);

	return ret;
}

static int __maybe_unused zforce_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct zforce_ts *ts = i2c_get_clientdata(client);
	struct input_dev *input = ts->input;
	int ret = 0;

	mutex_lock(&input->mutex);

	ts->suspended = false;

	if (device_may_wakeup(&client->dev)) {
		dev_dbg(&client->dev, "resume from being a wakeup source\n");

		disable_irq_wake(client->irq);

		/* need to stop device if it was not open on suspend */
		if (!input_device_enabled(input)) {
			ret = zforce_stop(ts);
			if (ret)
				goto unlock;
		}
	} else if (input_device_enabled(input)) {
		dev_dbg(&client->dev, "resume without being a wakeup source\n");

		enable_irq(client->irq);

		ret = zforce_start(ts);
		if (ret < 0)
			goto unlock;
	}

unlock:
	mutex_unlock(&input->mutex);

	return ret;
}

static SIMPLE_DEV_PM_OPS(zforce_pm_ops, zforce_suspend, zforce_resume);

static void zforce_reset(void *data)
{
	struct zforce_ts *ts = data;

	zforce_reset_assert(ts);

	udelay(10);

	if (!IS_ERR(ts->reg_vdd))
		regulator_disable(ts->reg_vdd);
}

static struct zforce_ts_platdata *zforce_parse_dt(struct device *dev)
{
	struct zforce_ts_platdata *pdata;
	struct device_node *np = dev->of_node;

	if (!np)
		return ERR_PTR(-ENOENT);

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		dev_err(dev, "failed to allocate platform data\n");
		return ERR_PTR(-ENOMEM);
	}

	if (of_property_read_u32(np, "x-size", &pdata->x_max)) {
		dev_err(dev, "failed to get x-size property\n");
		return ERR_PTR(-EINVAL);
	}

	if (of_property_read_u32(np, "y-size", &pdata->y_max)) {
		dev_err(dev, "failed to get y-size property\n");
		return ERR_PTR(-EINVAL);
	}

	return pdata;
}

static int zforce_probe(struct i2c_client *client)
{
	const struct zforce_ts_platdata *pdata = dev_get_platdata(&client->dev);
	struct zforce_ts *ts;
	struct input_dev *input_dev;
	int ret;

	if (!pdata) {
		pdata = zforce_parse_dt(&client->dev);
		if (IS_ERR(pdata))
			return PTR_ERR(pdata);
	}

	ts = devm_kzalloc(&client->dev, sizeof(struct zforce_ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	ts->gpio_rst = devm_gpiod_get_optional(&client->dev, "reset",
					       GPIOD_OUT_HIGH);
	if (IS_ERR(ts->gpio_rst)) {
		ret = PTR_ERR(ts->gpio_rst);
		dev_err(&client->dev,
			"failed to request reset GPIO: %d\n", ret);
		return ret;
	}

	if (ts->gpio_rst) {
		ts->gpio_int = devm_gpiod_get_optional(&client->dev, "irq",
						       GPIOD_IN);
		if (IS_ERR(ts->gpio_int)) {
			ret = PTR_ERR(ts->gpio_int);
			dev_err(&client->dev,
				"failed to request interrupt GPIO: %d\n", ret);
			return ret;
		}
	} else {
		/*
		 * Deprecated GPIO handling for compatibility
		 * with legacy binding.
		 */

		/* INT GPIO */
		ts->gpio_int = devm_gpiod_get_index(&client->dev, NULL, 0,
						    GPIOD_IN);
		if (IS_ERR(ts->gpio_int)) {
			ret = PTR_ERR(ts->gpio_int);
			dev_err(&client->dev,
				"failed to request interrupt GPIO: %d\n", ret);
			return ret;
		}

		/* RST GPIO */
		ts->gpio_rst = devm_gpiod_get_index(&client->dev, NULL, 1,
					    GPIOD_OUT_HIGH);
		if (IS_ERR(ts->gpio_rst)) {
			ret = PTR_ERR(ts->gpio_rst);
			dev_err(&client->dev,
				"failed to request reset GPIO: %d\n", ret);
			return ret;
		}
	}

	ts->reg_vdd = devm_regulator_get_optional(&client->dev, "vdd");
	if (IS_ERR(ts->reg_vdd)) {
		ret = PTR_ERR(ts->reg_vdd);
		if (ret == -EPROBE_DEFER)
			return ret;
	} else {
		ret = regulator_enable(ts->reg_vdd);
		if (ret)
			return ret;

		/*
		 * according to datasheet add 100us grace time after regular
		 * regulator enable delay.
		 */
		udelay(100);
	}

	ret = devm_add_action(&client->dev, zforce_reset, ts);
	if (ret) {
		dev_err(&client->dev, "failed to register reset action, %d\n",
			ret);

		/* hereafter the regulator will be disabled by the action */
		if (!IS_ERR(ts->reg_vdd))
			regulator_disable(ts->reg_vdd);

		return ret;
	}

	snprintf(ts->phys, sizeof(ts->phys),
		 "%s/input0", dev_name(&client->dev));

	input_dev = devm_input_allocate_device(&client->dev);
	if (!input_dev) {
		dev_err(&client->dev, "could not allocate input device\n");
		return -ENOMEM;
	}

	mutex_init(&ts->access_mutex);
	mutex_init(&ts->command_mutex);

	ts->pdata = pdata;
	ts->client = client;
	ts->input = input_dev;

	input_dev->name = "Neonode zForce touchscreen";
	input_dev->phys = ts->phys;
	input_dev->id.bustype = BUS_I2C;

	input_dev->open = zforce_input_open;
	input_dev->close = zforce_input_close;

	__set_bit(EV_KEY, input_dev->evbit);
	__set_bit(EV_SYN, input_dev->evbit);
	__set_bit(EV_ABS, input_dev->evbit);

	/* For multi touch */
	input_set_abs_params(input_dev, ABS_MT_POSITION_X, 0,
			     pdata->x_max, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_Y, 0,
			     pdata->y_max, 0, 0);

	input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR, 0,
			     ZFORCE_MAX_AREA, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_TOUCH_MINOR, 0,
			     ZFORCE_MAX_AREA, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_ORIENTATION, 0, 1, 0, 0);
	input_mt_init_slots(input_dev, ZFORCE_REPORT_POINTS, INPUT_MT_DIRECT);

	input_set_drvdata(ts->input, ts);

	init_completion(&ts->command_done);

	/*
	 * The zforce pulls the interrupt low when it has data ready.
	 * After it is triggered the isr thread runs until all the available
	 * packets have been read and the interrupt is high again.
	 * Therefore we can trigger the interrupt anytime it is low and do
	 * not need to limit it to the interrupt edge.
	 */
	ret = devm_request_threaded_irq(&client->dev, client->irq,
					zforce_irq, zforce_irq_thread,
					IRQF_TRIGGER_LOW | IRQF_ONESHOT,
					input_dev->name, ts);
	if (ret) {
		dev_err(&client->dev, "irq %d request failed\n", client->irq);
		return ret;
	}

	i2c_set_clientdata(client, ts);

	/* let the controller boot */
	zforce_reset_deassert(ts);

	ts->command_waiting = NOTIFICATION_BOOTCOMPLETE;
	if (wait_for_completion_timeout(&ts->command_done, WAIT_TIMEOUT) == 0)
		dev_warn(&client->dev, "bootcomplete timed out\n");

	/* need to start device to get version information */
	ret = zforce_command_wait(ts, COMMAND_INITIALIZE);
	if (ret) {
		dev_err(&client->dev, "unable to initialize, %d\n", ret);
		return ret;
	}

	/* this gets the firmware version among other information */
	ret = zforce_command_wait(ts, COMMAND_STATUS);
	if (ret < 0) {
		dev_err(&client->dev, "couldn't get status, %d\n", ret);
		zforce_stop(ts);
		return ret;
	}

	/* stop device and put it into sleep until it is opened */
	ret = zforce_stop(ts);
	if (ret < 0)
		return ret;

	device_set_wakeup_capable(&client->dev, true);

	ret = input_register_device(input_dev);
	if (ret) {
		dev_err(&client->dev, "could not register input device, %d\n",
			ret);
		return ret;
	}

	return 0;
}

static struct i2c_device_id zforce_idtable[] = {
	{ "zforce-ts", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, zforce_idtable);

#ifdef CONFIG_OF
static const struct of_device_id zforce_dt_idtable[] = {
	{ .compatible = "neonode,zforce" },
	{},
};
MODULE_DEVICE_TABLE(of, zforce_dt_idtable);
#endif

static struct i2c_driver zforce_driver = {
	.driver = {
		.name	= "zforce-ts",
		.pm	= &zforce_pm_ops,
		.of_match_table	= of_match_ptr(zforce_dt_idtable),
	},
	.probe_new	= zforce_probe,
	.id_table	= zforce_idtable,
};

module_i2c_driver(zforce_driver);

MODULE_AUTHOR("Heiko Stuebner <heiko@sntech.de>");
MODULE_DESCRIPTION("zForce TouchScreen Driver");
MODULE_LICENSE("GPL");
