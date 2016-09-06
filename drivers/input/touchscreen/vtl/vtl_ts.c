/*
 * VTL CTP driver
 *
 * Copyright (C) 2013 VTL Corporation
 * Copyright (C) 2016 Freescale Semiconductor, Inc
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/pm_qos.h>
#include <linux/slab.h>
#include <linux/types.h>

#define FORCE_SINGLE_EVENT 1

#include "vtl_ts.h"

#define MIN_X       0x00
#define MIN_Y       0x00
#define MAX_X       1023
#define MAX_Y       767
#define MAX_AREA    0xff
#define MAX_FINGERS 2


/* Global or static variables */
struct ts_driver g_driver;

static struct ts_info g_ts = {
	.driver = &g_driver,
};
static struct ts_info *pg_ts = &g_ts;

static struct i2c_device_id vtl_ts_id[] = {
	{ DRIVER_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, vtl_ts_id);


static int vtl_ts_config(struct ts_info *ts)
{
	struct device *dev;

	DEBUG();

	dev = &ts->driver->client->dev;

	/* ts config */
	ts->config_info.touch_point_number = TOUCH_POINT_NUM;

	pr_info("Configuring vtl\n");
	ts->config_info.screen_max_x = SCREEN_MAX_X;
	ts->config_info.screen_max_y = SCREEN_MAX_y;
	return 0;
}

void vtl_ts_free_gpio(void)
{
	struct ts_info *ts;

	ts = pg_ts;
	DEBUG();

	gpio_free(ts->config_info.irq_gpio_number);
}

void vtl_ts_hw_reset(void)
{
	struct ts_info *ts;

	ts = pg_ts;
	DEBUG();

	gpio_set_value(ts->config_info.rst_gpio_number, 0);
	mdelay(50);
	gpio_set_value(ts->config_info.rst_gpio_number, 1);
}

static irqreturn_t vtl_ts_irq(int irq, void *dev)
{
	struct ts_info *ts;

	ts = pg_ts;
	DEBUG();

	queue_work(ts->driver->workqueue, &ts->driver->event_work);

	return IRQ_HANDLED;
}

static union ts_xy_data *vtl_read_xy_data(struct ts_info *ts)
{
	struct i2c_msg msgs;
	int err;

	DEBUG();

	msgs.addr = ts->driver->client->addr;
	msgs.flags = 0x01;
	msgs.len = sizeof(ts->xy_data.buf);
	msgs.buf = ts->xy_data.buf;

	err = i2c_transfer(ts->driver->client->adapter, &msgs, 1);
	if (err != 1) {
		pr_err("___%s:i2c read err___\n", __func__);
		return NULL;
	}
	return &ts->xy_data;
}

static void vtl_report_xy_coord(struct input_dev *input_dev,
				union ts_xy_data *xy_data,
				unsigned char touch_point_number)
{
	struct ts_info *ts;
	int id;
	int sync;
	int x, y;
	unsigned int press;
	static unsigned int release;

	ts = pg_ts;
	DEBUG();

	/* report points */
	sync = 0;  press = 0;
	for (id = 0; id < touch_point_number; id++) {
		if ((xy_data->point[id].xhi != 0xFF) &&
		    (xy_data->point[id].yhi != 0xFF) &&
		   ((xy_data->point[id].status == 1) ||
		    (xy_data->point[id].status == 2))) {
			x = (xy_data->point[id].xhi<<4) |
			    (xy_data->point[id].xlo&0xF);
			y = (xy_data->point[id].yhi<<4) |
			    (xy_data->point[id].ylo&0xF);

		if (ts->config_info.exchange_x_y_flag)
			swap(x, y);

		if (ts->config_info.revert_x_flag)
			x = ts->config_info.screen_max_x - x;

		if (ts->config_info.revert_y_flag)
			y = ts->config_info.screen_max_y - y;
#ifndef FORCE_SINGLE_EVENT
			input_mt_slot(input_dev, xy_data->point[id].id - 1);
			input_mt_report_slot_state(input_dev,
						   MT_TOOL_FINGER, true);
			input_report_abs(input_dev, ABS_MT_POSITION_X, x);
			input_report_abs(input_dev, ABS_MT_POSITION_Y, y);
			input_report_abs(input_dev, ABS_MT_TOUCH_MAJOR, 30);
			input_report_abs(input_dev, ABS_MT_WIDTH_MAJOR, 128);
#else
			input_report_abs(input_dev, ABS_X, x);
			input_report_abs(input_dev, ABS_Y, y);
			input_report_key(input_dev, BTN_TOUCH, 1);
			input_report_abs(input_dev, ABS_PRESSURE, 1);
#endif
			sync = 1;
			press |= 0x01 << (xy_data->point[id].id - 1);
		}
	}

	release &= (release ^ press); /*release point flag */
	for (id = 0; id < touch_point_number; id++) {
		if (release & (0x01 << id)) {
#ifndef FORCE_SINGLE_EVENT
			input_mt_slot(input_dev, id);
			input_mt_report_slot_state(input_dev,
						   MT_TOOL_FINGER, false);
#else
			input_report_key(input_dev, BTN_TOUCH, 0);
			input_report_abs(input_dev, ABS_PRESSURE, 0);
#endif
			sync = 1;
		}

	}
	release = press;

	if (sync)
		input_sync(input_dev);
}

static void vtl_ts_workfunc(struct work_struct *work)
{

	union ts_xy_data *xy_data;
	struct input_dev *input_dev;
	unsigned char touch_point_number;

	DEBUG();

	input_dev = pg_ts->driver->input_dev;
	touch_point_number = pg_ts->config_info.touch_point_number;

	xy_data = vtl_read_xy_data(pg_ts);
	if (xy_data != NULL)
		vtl_report_xy_coord(input_dev, xy_data, touch_point_number);
	else
		pr_err("____xy_data error___\n");
}

#ifdef CONFIG_PM_SLEEP
int vtl_ts_suspend(struct device *dev)
{
	struct ts_info *ts;

	ts = pg_ts;
	DEBUG();

	disable_irq(ts->config_info.irq_number);
	cancel_work_sync(&ts->driver->event_work);

	return 0;
}

int vtl_ts_resume(struct device *dev)
{
	struct ts_info *ts;

	ts = pg_ts;
	DEBUG();

	/* Hardware reset */
	vtl_ts_hw_reset();
	enable_irq(ts->config_info.irq_number);

	return 0;
}
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
static void vtl_ts_early_suspend(struct early_suspend *handler)
{
	struct ts_info *ts;

	ts = pg_ts;
	DEBUG();

	vtl_ts_suspend(ts->driver->client, PMSG_SUSPEND);
}

static void vtl_ts_early_resume(struct early_suspend *handler)
{
	struct ts_info *ts;

	ts = pg_ts;
	DEBUG();

	vtl_ts_resume(ts->driver->client);
}
#endif

int  vtl_ts_remove(struct i2c_client *client)
{
	struct ts_info *ts;

	ts = pg_ts;
	DEBUG();

	/* Driver clean up */

	free_irq(ts->config_info.irq_number, ts);
	vtl_ts_free_gpio();

#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&ts->driver->early_suspend);
#endif

	cancel_work_sync(&ts->driver->event_work);
	destroy_workqueue(ts->driver->workqueue);

	input_unregister_device(ts->driver->input_dev);
	input_free_device(ts->driver->input_dev);

	if (ts->driver->proc_entry != NULL)
		remove_proc_entry(DRIVER_NAME, NULL);

	return 0;
}

static int init_input_dev(struct ts_info *ts)
{
	struct input_dev *input_dev;
	struct device *dev;
	int err;

	DEBUG();

	dev = &ts->driver->client->dev;

	/* allocate input device */
	ts->driver->input_dev = devm_input_allocate_device(dev);
	if (ts->driver->input_dev == NULL) {
		dev_err(dev, "Unable to allocate input device for device %s\n",
			DRIVER_NAME);
		return -1;
	}

	input_dev = ts->driver->input_dev;

	input_dev->name = "VTL for wld";
	input_dev->phys = "I2C";
	input_dev->id.bustype = BUS_I2C;
	input_dev->id.vendor  = 0xaaaa;
	input_dev->id.product = 0x5555;
	input_dev->id.version = 0x0001;
	input_dev->dev.parent = dev;

	/* config input device */
	__set_bit(EV_SYN, input_dev->evbit);
	__set_bit(EV_KEY, input_dev->evbit);
	__set_bit(EV_ABS, input_dev->evbit);

#ifdef FORCE_SINGLE_EVENT
	input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
	input_set_abs_params(input_dev, ABS_PRESSURE, 0, 1, 0, 0);
	input_set_abs_params(input_dev, ABS_X, MIN_X, MAX_X, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, MIN_Y, MAX_Y, 0, 0);
#else
	__set_bit(INPUT_PROP_DIRECT, input_dev->propbit);

	input_mt_init_slots(input_dev, TOUCH_POINT_NUM, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_X, 0,
			     ts->config_info.screen_max_x, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_Y, 0,
			     ts->config_info.screen_max_y, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_WIDTH_MAJOR, 0, 255, 0, 0);
#endif
	/* register input device */
	err = input_register_device(input_dev);
	if (err) {
		dev_err(dev, "Unable to register input device for device %s\n",
			DRIVER_NAME);
		return -1;
	}

	return 0;
}

int ct36x_test_tp(struct i2c_client *client)
{
	struct i2c_msg msgs;
	char buf;

	msgs.addr = 0x7F;
	msgs.flags = 0x01;
	msgs.len = 1;
	msgs.buf = &buf;

	if (i2c_transfer(client->adapter, &msgs, 1) != 1)
		return -1;

	return 0;
}

int vtl_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int err = -1;
	struct ts_info *ts;
	struct device *dev;

	ts = pg_ts;
	ts->driver->client = client;
	dev = &ts->driver->client->dev;

	/*Probing TouchScreen*/
	pr_info("Probing vtl touchscreen, touchscreen node found\n");
	if (ct36x_test_tp(client) < 0) {
			pr_err("vtl tp not found\n");
			goto ERR_TS_CONFIG;
	}

	/* Request platform resources (gpio/interrupt pins) */
	err = vtl_ts_config(ts);
	if (err) {
		dev_err(dev, "VTL touch screen config Failed.\n");
		goto ERR_TS_CONFIG;
	}

	/*Requestion GPIO*/
	ts->config_info.rst_gpio_number = of_get_gpio(client->dev.of_node, 0);
	if (gpio_is_valid(ts->config_info.rst_gpio_number)) {
		err = devm_gpio_request(dev,
					ts->config_info.rst_gpio_number, NULL);
		if (err) {
			dev_err(dev, "Unable to request GPIO %d\n",
				ts->config_info.rst_gpio_number);
			return err;
		}
	}

	/* Check I2C Functionality */
	err = i2c_check_functionality(client->adapter, I2C_FUNC_I2C);
	if (!err) {
		dev_err(dev, "Check I2C Functionality Failed.\n");
		return -ENODEV;
	}

	err = devm_request_threaded_irq(dev, client->irq,
					NULL, vtl_ts_irq,
					IRQF_ONESHOT,
					client->name, ts);
	if (err) {
		dev_err(&client->dev, "VTL Failed to register interrupt\n");

		goto ERR_IRQ_REQ;
	}

	vtl_ts_hw_reset();

	/*init input dev*/
	err = init_input_dev(ts);
	if (err) {

		dev_err(dev, "init input dev failed.\n");
		goto ERR_INIT_INPUT;
	}

	/* register early suspend */
#ifdef CONFIG_HAS_EARLYSUSPEND
	ts->driver->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	ts->driver->early_suspend.suspend = vtl_ts_early_suspend;
	ts->driver->early_suspend.resume = vtl_ts_early_resume;
	register_early_suspend(&ts->driver->early_suspend);
#endif
	/* Create work queue */
	INIT_WORK(&ts->driver->event_work, vtl_ts_workfunc);
	ts->driver->workqueue = create_singlethread_workqueue(DRIVER_NAME);

	return 0;

ERR_IRQ_REQ:
	cancel_work_sync(&ts->driver->event_work);
	destroy_workqueue(ts->driver->workqueue);

ERR_INIT_INPUT:
	input_free_device(ts->driver->input_dev);
	gpio_free(ts->config_info.rst_gpio_number);
ERR_TS_CONFIG:

	return err;
}


static SIMPLE_DEV_PM_OPS(vtl_ts_pm_ops, vtl_ts_suspend, vtl_ts_resume);

static const struct of_device_id vtl_ts_dt_ids[] = {
	{ .compatible = "vtl,ct365", },
	{ }
};
MODULE_DEVICE_TABLE(of, vtl_ts_dt_ids);


static struct i2c_driver vtl_ts_driver  = {
	.probe      = vtl_ts_probe,
	.remove     = vtl_ts_remove,
	.id_table   = vtl_ts_id,
	.driver = {
		.owner  = THIS_MODULE,
		.name   = DRIVER_NAME,
		.pm     = &vtl_ts_pm_ops,
		.of_match_table = of_match_ptr(vtl_ts_dt_ids),
	},
};

module_i2c_driver(vtl_ts_driver);

MODULE_AUTHOR("VTL");
MODULE_DESCRIPTION("VTL TouchScreen driver");
MODULE_LICENSE("GPL");
