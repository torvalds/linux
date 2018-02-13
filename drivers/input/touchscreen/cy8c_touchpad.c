/*
 * cy8c4014/cy8c4024 touch pad driver
 *
 * Copyright (C) 2017 Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/async.h>
#include <linux/gpio.h>
#include <asm/irq.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/proc_fs.h>
#include <linux/input/mt.h>
#include "tp_suspend.h"
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/wakelock.h>
#include <linux/timer.h>
#include <linux/jiffies.h>

struct cy8c_chipdef {
	int max_x;
	int max_y;
	int multi_touch_num;
	u8 reg_base;
};

struct cy8c_touch {
	struct i2c_client *client;
	struct input_dev *input;
	struct work_struct work;
	struct workqueue_struct *wq;
	int irq;
	struct tp_device tp;
	const struct cy8c_chipdef *cdef;
};

static const struct cy8c_chipdef cy8c4014_data = {
	.max_x = 0xfe,
	.max_y = 0xfe, /* only have x axis */
	.multi_touch_num = 1,
	.reg_base = 0x00,
};

static u32 i2c_cy8ctouch_read(struct cy8c_touch *ts,  u8 *buf, u32 num)
{
	struct i2c_client *client = ts->client;
	u8 reg = ts->cdef->reg_base;
	struct i2c_msg xfer_msg[2];
	int ret;

	xfer_msg[0].addr = client->addr;
	xfer_msg[0].len = 1;
	xfer_msg[0].flags = 0;
	xfer_msg[0].buf = &reg;

	xfer_msg[1].addr = client->addr;
	xfer_msg[1].len = num;
	xfer_msg[1].flags = I2C_M_RD;
	xfer_msg[1].buf = buf;

	ret = i2c_transfer(client->adapter, xfer_msg, ARRAY_SIZE(xfer_msg));

	if (ret != ARRAY_SIZE(xfer_msg))
		return ret;

	return 0;
}

static irqreturn_t cy8ctouch_irq(int irq, void *dev_id)
{
	struct cy8c_touch *ts = (struct cy8c_touch *)dev_id;

	disable_irq_nosync(ts->irq);
	queue_work(ts->wq, &ts->work);

	return IRQ_HANDLED;
}

static void cy8ctouch_ts_worker(struct work_struct *work)
{
	struct cy8c_touch *ts = container_of(work, struct cy8c_touch, work);
	struct i2c_client *client = ts->client;
	const struct cy8c_chipdef *cdef = ts->cdef;
	int rc;
	struct {
		u8 cur_x;
		u8 cur_y;
	} pos;

	rc = i2c_cy8ctouch_read(ts, (u8 *)&pos, sizeof(pos));
	if (rc != 0) {
		dev_err(&client->dev, "i2c read failed, ret = %d, line:%d\n",
			rc, __LINE__);
		enable_irq(ts->irq);
		return;
	}

	if (pos.cur_x <= cdef->max_x && pos.cur_y <= cdef->max_y) {
		/* finger down */
		dev_dbg(&client->dev, "Touch down, position is %d : %d\n",
			pos.cur_x, pos.cur_y);
		input_report_abs(ts->input, ABS_X, pos.cur_x);
		input_report_abs(ts->input, ABS_Y, pos.cur_y);
		input_report_key(ts->input, BTN_TOUCH, 1);
		input_sync(ts->input);
	} else if (pos.cur_x == 0xff && pos.cur_y == 0xff) {
		/* finger up */
		dev_dbg(&client->dev, "Touch up, position is %d : %d\n",
			pos.cur_x, pos.cur_y);
		input_report_key(ts->input, BTN_TOUCH, 0);
		input_sync(ts->input);
	}
	enable_irq(ts->irq);
}

static int cy8ctouch_init(struct i2c_client *client, struct cy8c_touch *ts)
{
	struct input_dev *input_device;
	int rc = 0;

	INIT_WORK(&ts->work, cy8ctouch_ts_worker);
	ts->wq = create_singlethread_workqueue("cy8c_touchpad_wq");
	if (!ts->wq) {
		dev_err(&client->dev, "Create workqueue failed!\n");
		return -ENOMEM;
	}

	input_device = devm_input_allocate_device(&ts->client->dev);
	if (!input_device) {
		dev_err(&client->dev, "Allocate input device failed!\n");
		destroy_workqueue(ts->wq);
		return -ENOMEM;
	}

	ts->input = input_device;
	input_device->name = "cy8c_touchpad";
	input_device->id.bustype = BUS_I2C;
	input_device->dev.parent = &client->dev;
	input_set_drvdata(input_device, ts);

	__set_bit(EV_ABS, input_device->evbit);
	__set_bit(EV_KEY, input_device->evbit);
	__set_bit(BTN_TOUCH, input_device->keybit);

	if (ts->cdef->max_x > 0) {
		__set_bit(ABS_X, input_device->absbit);
		input_set_abs_params(input_device,
				     ABS_X, 0, ts->cdef->max_x, 0, 0);
	} else {
		dev_err(&client->dev, "max_x is zero, touchpad don't have x axis\n");
	}

	if (ts->cdef->max_y > 0) {
		__set_bit(ABS_Y, input_device->absbit);
		input_set_abs_params(input_device,
				     ABS_Y, 0, ts->cdef->max_y, 0, 0);
	} else {
		dev_err(&client->dev, "max_y is zero, touchpad don't have y axis\n");
	}

	rc = input_register_device(input_device);
	if (rc)
		goto error_unreg_device;

	return 0;

error_unreg_device:
	destroy_workqueue(ts->wq);
	return rc;
}

static int cy8ctouch_suspend(struct  tp_device *tp_dev)
{
	struct cy8c_touch *ts = container_of(tp_dev, struct cy8c_touch, tp);

	dev_info(&ts->client->dev, "[cy8ctouch] Enter %s\n", __func__);

	return 0;
}

static int cy8ctouch_resume(struct  tp_device *tp_dev)
{
	struct cy8c_touch *ts = container_of(tp_dev, struct cy8c_touch, tp);

	dev_info(&ts->client->dev, "[cy8ctouch] Enter %s\n", __func__);

	return 0;
}

static const struct of_device_id of_cy8ctouch_dt_ids[] = {
	{.compatible = "cypress,cy8c4014", .data = &cy8c4014_data},
	{.compatible = "cypress,cy8c4024", .data = &cy8c4014_data},
	{ },
};
MODULE_DEVICE_TABLE(of, of_cy8ctouch_dt_ids);

static int
cy8ctouch_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	const struct of_device_id *of_dev_id;
	struct device *dev = &client->dev;
	struct cy8c_touch *ts;
	int rc;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "cy8ctouch I2C functionality not supported\n");
		return -ENODEV;
	}

	of_dev_id = of_match_device(of_cy8ctouch_dt_ids, dev);
	if (!of_dev_id)
		return -EINVAL;

	ts = devm_kzalloc(&client->dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	ts->tp.tp_suspend = cy8ctouch_suspend;
	ts->tp.tp_resume = cy8ctouch_resume;
	tp_register_fb(&ts->tp);

	ts->client = client;
	i2c_set_clientdata(client, ts);

	ts->cdef = of_dev_id->data;
	rc = cy8ctouch_init(client, ts);
	if (rc < 0) {
		dev_err(&client->dev, "cy8ctouch init failed\n");
		goto porbe_err_ret;
	}

	if (client->irq > 0) {
		rc = devm_request_irq(dev, client->irq, cy8ctouch_irq, IRQF_TRIGGER_FALLING,
				      client->name, ts);
		if (rc < 0) {
			dev_err(&client->dev, "cy8ctouch_probe: request irq failed\n");
			goto porbe_err_ret;
		}
	}

	return 0;

porbe_err_ret:
	return rc;
}

static int cy8ctouch_i2c_remove(struct i2c_client *client)
{
	struct cy8c_touch *ts = i2c_get_clientdata(client);

	cancel_work_sync(&ts->work);
	destroy_workqueue(ts->wq);

	return 0;
}

static const struct i2c_device_id cy8ctouch_id[] = {
	{ "cy8c4014", 0 },
	{ "cy8c4024", 0 },
	{ },
};

static struct i2c_driver cy8ctouch_i2c_driver  = {
	.driver = {
		.name  = "cy8c_touchpad",
		.of_match_table = of_match_ptr(of_cy8ctouch_dt_ids),
	},
	.probe		= cy8ctouch_i2c_probe,
	.remove		= cy8ctouch_i2c_remove,
	.id_table	= cy8ctouch_id,
};

module_i2c_driver(cy8ctouch_i2c_driver);

MODULE_AUTHOR("Wenping Zhang <zwp@rock-chips.com>");
MODULE_DESCRIPTION("Cypress Cy8cxxxx touchpad driver");
MODULE_LICENSE("GPL v2");
