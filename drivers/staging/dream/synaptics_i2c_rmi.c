/*
 * Support for synaptics touchscreen.
 *
 * Copyright (C) 2007 Google, Inc.
 * Author: Arve Hjønnevåg <arve@android.com>
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
 * http://www.synaptics.com/sites/default/files/511_000099_01F.pdf
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/slab.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include "synaptics_i2c_rmi.h"

static struct workqueue_struct *synaptics_wq;

struct synaptics_ts_data {
	u16 addr;
	struct i2c_client *client;
	struct input_dev *input_dev;
	int use_irq;
	struct hrtimer timer;
	struct work_struct  work;
	u16 max[2];
	int snap_state[2][2];
	int snap_down_on[2];
	int snap_down_off[2];
	int snap_up_on[2];
	int snap_up_off[2];
	int snap_down[2];
	int snap_up[2];
	u32 flags;
	int (*power)(int on);
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif
};

static int i2c_set(struct synaptics_ts_data *ts, u8 reg, u8 val, char *msg)
{
	int ret = i2c_smbus_write_byte_data(ts->client, reg, val);
	if (ret < 0)
		pr_err("i2c_smbus_write_byte_data failed (%s)\n", msg);
	return ret;
}

static int i2c_read(struct synaptics_ts_data *ts, u8 reg, char *msg)
{
	int ret = i2c_smbus_read_byte_data(ts->client, reg);
	if (ret < 0)
		pr_err("i2c_smbus_read_byte_data failed (%s)\n", msg);
	return ret;
}
#ifdef CONFIG_HAS_EARLYSUSPEND
static void synaptics_ts_early_suspend(struct early_suspend *h);
static void synaptics_ts_late_resume(struct early_suspend *h);
#endif

static int synaptics_init_panel(struct synaptics_ts_data *ts)
{
	int ret;

	ret = i2c_set(ts, 0xff, 0x10, "set page select");
	if (ret == 0)
		ret = i2c_set(ts, 0x41, 0x04, "set No Clip Z");

	ret = i2c_set(ts, 0xff, 0x04, "fallback page select");
	ret = i2c_set(ts, 0xf0, 0x81, "select 80 reports per second");
	return ret;
}

static void decode_report(struct synaptics_ts_data *ts, u8 *buf)
{
/*
 * This sensor sends two 6-byte absolute finger reports, an optional
 * 2-byte relative report followed by a status byte. This function
 * reads the two finger reports and transforms the coordinates
 * according the platform data so they can be aligned with the lcd
 * behind the touchscreen. Typically we flip the y-axis since the
 * sensor uses the bottom left corner as the origin, but if the sensor
 * is mounted upside down the platform data will request that the
 * x-axis should be flipped instead. The snap to inactive edge border
 * are used to allow tapping the edges of the screen on the G1. The
 * active area of the touchscreen is smaller than the lcd. When the
 * finger gets close the edge of the screen we snap it to the
 * edge. This allows ui elements at the edge of the screen to be hit,
 * and it prevents hitting ui elements that are not at the edge of the
 * screen when the finger is touching the edge.
 */
	int pos[2][2];
	int f, a;
	int base = 2;
	int z = buf[1];
	int w = buf[0] >> 4;
	int finger = buf[0] & 7;
	int finger2_pressed;

	for (f = 0; f < 2; f++) {
		u32 flip_flag = SYNAPTICS_FLIP_X;
		for (a = 0; a < 2; a++) {
			int p = buf[base + 1];
			p |= (u16)(buf[base] & 0x1f) << 8;
			if (ts->flags & flip_flag)
				p = ts->max[a] - p;
			if (ts->flags & SYNAPTICS_SNAP_TO_INACTIVE_EDGE) {
				if (ts->snap_state[f][a]) {
					if (p <= ts->snap_down_off[a])
						p = ts->snap_down[a];
					else if (p >= ts->snap_up_off[a])
						p = ts->snap_up[a];
					else
						ts->snap_state[f][a] = 0;
				} else {
					if (p <= ts->snap_down_on[a]) {
						p = ts->snap_down[a];
						ts->snap_state[f][a] = 1;
					} else if (p >= ts->snap_up_on[a]) {
						p = ts->snap_up[a];
						ts->snap_state[f][a] = 1;
					}
				}
			}
			pos[f][a] = p;
			base += 2;
			flip_flag <<= 1;
		}
		base += 2;
		if (ts->flags & SYNAPTICS_SWAP_XY)
			swap(pos[f][0], pos[f][1]);
	}
	if (z) {
		input_report_abs(ts->input_dev, ABS_X, pos[0][0]);
		input_report_abs(ts->input_dev, ABS_Y, pos[0][1]);
	}
	input_report_abs(ts->input_dev, ABS_PRESSURE, z);
	input_report_abs(ts->input_dev, ABS_TOOL_WIDTH, w);
	input_report_key(ts->input_dev, BTN_TOUCH, finger);
	finger2_pressed = finger > 1 && finger != 7;
	input_report_key(ts->input_dev, BTN_2, finger2_pressed);
	if (finger2_pressed) {
		input_report_abs(ts->input_dev, ABS_HAT0X, pos[1][0]);
		input_report_abs(ts->input_dev, ABS_HAT0Y, pos[1][1]);
	}
	input_sync(ts->input_dev);
}

static void synaptics_ts_work_func(struct work_struct *work)
{
	int i;
	int ret;
	int bad_data = 0;
	struct i2c_msg msg[2];
	u8 start_reg = 0;
	u8 buf[15];
	struct synaptics_ts_data *ts =
		container_of(work, struct synaptics_ts_data, work);

	msg[0].addr = ts->client->addr;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = &start_reg;
	msg[1].addr = ts->client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = sizeof(buf);
	msg[1].buf = buf;

	for (i = 0; i < ((ts->use_irq && !bad_data) ? 1 : 10); i++) {
		ret = i2c_transfer(ts->client->adapter, msg, 2);
		if (ret < 0) {
			pr_err("ts_work: i2c_transfer failed\n");
			bad_data = 1;
			continue;
		}
		if ((buf[14] & 0xc0) != 0x40) {
			pr_warning("synaptics_ts_work_func:"
			       " bad read %x %x %x %x %x %x %x %x %x"
			       " %x %x %x %x %x %x, ret %d\n",
			       buf[0], buf[1], buf[2], buf[3],
			       buf[4], buf[5], buf[6], buf[7],
			       buf[8], buf[9], buf[10], buf[11],
			       buf[12], buf[13], buf[14], ret);
			if (bad_data)
				synaptics_init_panel(ts);
			bad_data = 1;
			continue;
		}
		bad_data = 0;
		if ((buf[14] & 1) == 0)
			break;

		decode_report(ts, buf);
	}
	if (ts->use_irq)
		enable_irq(ts->client->irq);
}

static enum hrtimer_restart synaptics_ts_timer_func(struct hrtimer *timer)
{
	struct synaptics_ts_data *ts =
		container_of(timer, struct synaptics_ts_data, timer);

	queue_work(synaptics_wq, &ts->work);

	hrtimer_start(&ts->timer, ktime_set(0, 12500000), HRTIMER_MODE_REL);
	return HRTIMER_NORESTART;
}

static irqreturn_t synaptics_ts_irq_handler(int irq, void *dev_id)
{
	struct synaptics_ts_data *ts = dev_id;

	disable_irq_nosync(ts->client->irq);
	queue_work(synaptics_wq, &ts->work);
	return IRQ_HANDLED;
}

static int detect(struct synaptics_ts_data *ts, u32 *panel_version)
{
	int ret;
	int retry = 10;

	ret = i2c_set(ts, 0xf4, 0x01, "reset device");

	while (retry-- > 0) {
		ret = i2c_smbus_read_byte_data(ts->client, 0xe4);
		if (ret >= 0)
			break;
		msleep(100);
	}
	if (ret < 0) {
		pr_err("i2c_smbus_read_byte_data failed\n");
		return ret;
	}

	*panel_version = ret << 8;
	ret = i2c_read(ts, 0xe5, "product minor");
	if (ret < 0)
		return ret;
	*panel_version |= ret;

	ret = i2c_read(ts, 0xe3, "property");
	if (ret < 0)
		return ret;

	pr_info("synaptics: version %x, product property %x\n",
		*panel_version, ret);
	return 0;
}

static void compute_areas(struct synaptics_ts_data *ts,
			  struct synaptics_i2c_rmi_platform_data *pdata,
			  u16 max_x, u16 max_y)
{
	int inactive_area_left;
	int inactive_area_right;
	int inactive_area_top;
	int inactive_area_bottom;
	int snap_left_on;
	int snap_left_off;
	int snap_right_on;
	int snap_right_off;
	int snap_top_on;
	int snap_top_off;
	int snap_bottom_on;
	int snap_bottom_off;
	int fuzz_x;
	int fuzz_y;
	int fuzz_p;
	int fuzz_w;
	int swapped = !!(ts->flags & SYNAPTICS_SWAP_XY);

	inactive_area_left = pdata->inactive_left;
	inactive_area_right = pdata->inactive_right;
	inactive_area_top = pdata->inactive_top;
	inactive_area_bottom = pdata->inactive_bottom;
	snap_left_on = pdata->snap_left_on;
	snap_left_off = pdata->snap_left_off;
	snap_right_on = pdata->snap_right_on;
	snap_right_off = pdata->snap_right_off;
	snap_top_on = pdata->snap_top_on;
	snap_top_off = pdata->snap_top_off;
	snap_bottom_on = pdata->snap_bottom_on;
	snap_bottom_off = pdata->snap_bottom_off;
	fuzz_x = pdata->fuzz_x;
	fuzz_y = pdata->fuzz_y;
	fuzz_p = pdata->fuzz_p;
	fuzz_w = pdata->fuzz_w;

	inactive_area_left = inactive_area_left * max_x / 0x10000;
	inactive_area_right = inactive_area_right * max_x / 0x10000;
	inactive_area_top = inactive_area_top * max_y / 0x10000;
	inactive_area_bottom = inactive_area_bottom * max_y / 0x10000;
	snap_left_on = snap_left_on * max_x / 0x10000;
	snap_left_off = snap_left_off * max_x / 0x10000;
	snap_right_on = snap_right_on * max_x / 0x10000;
	snap_right_off = snap_right_off * max_x / 0x10000;
	snap_top_on = snap_top_on * max_y / 0x10000;
	snap_top_off = snap_top_off * max_y / 0x10000;
	snap_bottom_on = snap_bottom_on * max_y / 0x10000;
	snap_bottom_off = snap_bottom_off * max_y / 0x10000;
	fuzz_x = fuzz_x * max_x / 0x10000;
	fuzz_y = fuzz_y * max_y / 0x10000;


	ts->snap_down[swapped] = -inactive_area_left;
	ts->snap_up[swapped] = max_x + inactive_area_right;
	ts->snap_down[!swapped] = -inactive_area_top;
	ts->snap_up[!swapped] = max_y + inactive_area_bottom;
	ts->snap_down_on[swapped] = snap_left_on;
	ts->snap_down_off[swapped] = snap_left_off;
	ts->snap_up_on[swapped] = max_x - snap_right_on;
	ts->snap_up_off[swapped] = max_x - snap_right_off;
	ts->snap_down_on[!swapped] = snap_top_on;
	ts->snap_down_off[!swapped] = snap_top_off;
	ts->snap_up_on[!swapped] = max_y - snap_bottom_on;
	ts->snap_up_off[!swapped] = max_y - snap_bottom_off;
	pr_info("synaptics_ts_probe: max_x %d, max_y %d\n", max_x, max_y);
	pr_info("synaptics_ts_probe: inactive_x %d %d, inactive_y %d %d\n",
	       inactive_area_left, inactive_area_right,
	       inactive_area_top, inactive_area_bottom);
	pr_info("synaptics_ts_probe: snap_x %d-%d %d-%d, snap_y %d-%d %d-%d\n",
	       snap_left_on, snap_left_off, snap_right_on, snap_right_off,
	       snap_top_on, snap_top_off, snap_bottom_on, snap_bottom_off);

	input_set_abs_params(ts->input_dev, ABS_X,
			     -inactive_area_left, max_x + inactive_area_right,
			     fuzz_x, 0);
	input_set_abs_params(ts->input_dev, ABS_Y,
			     -inactive_area_top, max_y + inactive_area_bottom,
			     fuzz_y, 0);
	input_set_abs_params(ts->input_dev, ABS_PRESSURE, 0, 255, fuzz_p, 0);
	input_set_abs_params(ts->input_dev, ABS_TOOL_WIDTH, 0, 15, fuzz_w, 0);
	input_set_abs_params(ts->input_dev, ABS_HAT0X, -inactive_area_left,
			     max_x + inactive_area_right, fuzz_x, 0);
	input_set_abs_params(ts->input_dev, ABS_HAT0Y, -inactive_area_top,
			     max_y + inactive_area_bottom, fuzz_y, 0);
}

static struct synaptics_i2c_rmi_platform_data fake_pdata;

static int __devinit synaptics_ts_probe(
	struct i2c_client *client, const struct i2c_device_id *id)
{
	struct synaptics_ts_data *ts;
	u8 buf0[4];
	u8 buf1[8];
	struct i2c_msg msg[2];
	int ret = 0;
	struct synaptics_i2c_rmi_platform_data *pdata;
	u32 panel_version = 0;
	u16 max_x, max_y;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_err("synaptics_ts_probe: need I2C_FUNC_I2C\n");
		ret = -ENODEV;
		goto err_check_functionality_failed;
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_WORD_DATA)) {
		pr_err("synaptics_ts_probe: need I2C_FUNC_SMBUS_WORD_DATA\n");
		ret = -ENODEV;
		goto err_check_functionality_failed;
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_WORD_DATA)) {
		pr_err("synaptics_ts_probe: need I2C_FUNC_SMBUS_WORD_DATA\n");
		ret = -ENODEV;
		goto err_check_functionality_failed;
	}

	ts = kzalloc(sizeof(*ts), GFP_KERNEL);
	if (ts == NULL) {
		ret = -ENOMEM;
		goto err_alloc_data_failed;
	}
	INIT_WORK(&ts->work, synaptics_ts_work_func);
	ts->client = client;
	i2c_set_clientdata(client, ts);
	pdata = client->dev.platform_data;
	if (pdata)
		ts->power = pdata->power;
	else
		pdata = &fake_pdata;

	if (ts->power) {
		ret = ts->power(1);
		if (ret < 0) {
			pr_err("synaptics_ts_probe power on failed\n");
			goto err_power_failed;
		}
	}

	ret = detect(ts, &panel_version);
	if (ret)
		goto err_detect_failed;

	while (pdata->version > panel_version)
		pdata++;
	ts->flags = pdata->flags;

	ret = i2c_read(ts, 0xf0, "device control");
	if (ret < 0)
		goto err_detect_failed;
	pr_info("synaptics: device control %x\n", ret);

	ret = i2c_read(ts, 0xf1, "interrupt enable");
	if (ret < 0)
		goto err_detect_failed;
	pr_info("synaptics_ts_probe: interrupt enable %x\n", ret);

	ret = i2c_set(ts, 0xf1, 0, "disable interrupt");
	if (ret < 0)
		goto err_detect_failed;

	msg[0].addr = ts->client->addr;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = buf0;
	buf0[0] = 0xe0;
	msg[1].addr = ts->client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = 8;
	msg[1].buf = buf1;
	ret = i2c_transfer(ts->client->adapter, msg, 2);
	if (ret < 0) {
		pr_err("i2c_transfer failed\n");
		goto err_detect_failed;
	}
	pr_info("synaptics_ts_probe: 0xe0: %x %x %x %x %x %x %x %x\n",
	       buf1[0], buf1[1], buf1[2], buf1[3],
	       buf1[4], buf1[5], buf1[6], buf1[7]);

	ret = i2c_set(ts, 0xff, 0x10, "page select = 0x10");
	if (ret < 0)
		goto err_detect_failed;

	ret = i2c_smbus_read_word_data(ts->client, 0x04);
	if (ret < 0) {
		pr_err("i2c_smbus_read_word_data failed\n");
		goto err_detect_failed;
	}
	ts->max[0] = max_x = (ret >> 8 & 0xff) | ((ret & 0x1f) << 8);
	ret = i2c_smbus_read_word_data(ts->client, 0x06);
	if (ret < 0) {
		pr_err("i2c_smbus_read_word_data failed\n");
		goto err_detect_failed;
	}
	ts->max[1] = max_y = (ret >> 8 & 0xff) | ((ret & 0x1f) << 8);
	if (ts->flags & SYNAPTICS_SWAP_XY)
		swap(max_x, max_y);

	/* will also switch back to page 0x04 */
	ret = synaptics_init_panel(ts);
	if (ret < 0) {
		pr_err("synaptics_init_panel failed\n");
		goto err_detect_failed;
	}

	ts->input_dev = input_allocate_device();
	if (ts->input_dev == NULL) {
		ret = -ENOMEM;
		pr_err("synaptics: Failed to allocate input device\n");
		goto err_input_dev_alloc_failed;
	}
	ts->input_dev->name = "synaptics-rmi-touchscreen";
	ts->input_dev->phys = "msm/input0";
	ts->input_dev->id.bustype = BUS_I2C;

	__set_bit(EV_SYN, ts->input_dev->evbit);
	__set_bit(EV_KEY, ts->input_dev->evbit);
	__set_bit(BTN_TOUCH, ts->input_dev->keybit);
	__set_bit(BTN_2, ts->input_dev->keybit);
	__set_bit(EV_ABS, ts->input_dev->evbit);

	compute_areas(ts, pdata, max_x, max_y);


	ret = input_register_device(ts->input_dev);
	if (ret) {
		pr_err("synaptics: Unable to register %s input device\n",
		       ts->input_dev->name);
		goto err_input_register_device_failed;
	}
	if (client->irq) {
		ret = request_irq(client->irq, synaptics_ts_irq_handler,
				  0, client->name, ts);
		if (ret == 0) {
			ret = i2c_set(ts, 0xf1, 0x01, "enable abs int");
			if (ret)
				free_irq(client->irq, ts);
		}
		if (ret == 0)
			ts->use_irq = 1;
		else
			dev_err(&client->dev, "request_irq failed\n");
	}
	if (!ts->use_irq) {
		hrtimer_init(&ts->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		ts->timer.function = synaptics_ts_timer_func;
		hrtimer_start(&ts->timer, ktime_set(1, 0), HRTIMER_MODE_REL);
	}
#ifdef CONFIG_HAS_EARLYSUSPEND
	ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	ts->early_suspend.suspend = synaptics_ts_early_suspend;
	ts->early_suspend.resume = synaptics_ts_late_resume;
	register_early_suspend(&ts->early_suspend);
#endif

	pr_info("synaptics: Start touchscreen %s in %s mode\n",
		ts->input_dev->name, ts->use_irq ? "interrupt" : "polling");

	return 0;

err_input_register_device_failed:
	input_free_device(ts->input_dev);

err_input_dev_alloc_failed:
err_detect_failed:
err_power_failed:
	kfree(ts);
err_alloc_data_failed:
err_check_functionality_failed:
	return ret;
}

static int synaptics_ts_remove(struct i2c_client *client)
{
	struct synaptics_ts_data *ts = i2c_get_clientdata(client);
#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&ts->early_suspend);
#endif
	if (ts->use_irq)
		free_irq(client->irq, ts);
	else
		hrtimer_cancel(&ts->timer);
	input_unregister_device(ts->input_dev);
	kfree(ts);
	return 0;
}

#ifdef CONFIG_PM
static int synaptics_ts_suspend(struct i2c_client *client, pm_message_t mesg)
{
	int ret;
	struct synaptics_ts_data *ts = i2c_get_clientdata(client);

	if (ts->use_irq)
		disable_irq(client->irq);
	else
		hrtimer_cancel(&ts->timer);
	ret = cancel_work_sync(&ts->work);
	if (ret && ts->use_irq) /* if work was pending disable-count is now 2 */
		enable_irq(client->irq);
	i2c_set(ts, 0xf1, 0, "disable interrupt");
	i2c_set(ts, 0xf0, 0x86, "deep sleep");

	if (ts->power) {
		ret = ts->power(0);
		if (ret < 0)
			pr_err("synaptics_ts_suspend power off failed\n");
	}
	return 0;
}

static int synaptics_ts_resume(struct i2c_client *client)
{
	int ret;
	struct synaptics_ts_data *ts = i2c_get_clientdata(client);

	if (ts->power) {
		ret = ts->power(1);
		if (ret < 0)
			pr_err("synaptics_ts_resume power on failed\n");
	}

	synaptics_init_panel(ts);

	if (ts->use_irq) {
		enable_irq(client->irq);
		i2c_set(ts, 0xf1, 0x01, "enable abs int");
	} else
		hrtimer_start(&ts->timer, ktime_set(1, 0), HRTIMER_MODE_REL);

	return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void synaptics_ts_early_suspend(struct early_suspend *h)
{
	struct synaptics_ts_data *ts;
	ts = container_of(h, struct synaptics_ts_data, early_suspend);
	synaptics_ts_suspend(ts->client, PMSG_SUSPEND);
}

static void synaptics_ts_late_resume(struct early_suspend *h)
{
	struct synaptics_ts_data *ts;
	ts = container_of(h, struct synaptics_ts_data, early_suspend);
	synaptics_ts_resume(ts->client);
}
#endif
#else
#define synaptics_ts_suspend NULL
#define synaptics_ts_resume NULL
#endif



static const struct i2c_device_id synaptics_ts_id[] = {
	{ SYNAPTICS_I2C_RMI_NAME, 0 },
	{ }
};

static struct i2c_driver synaptics_ts_driver = {
	.probe		= synaptics_ts_probe,
	.remove		= synaptics_ts_remove,
#ifndef CONFIG_HAS_EARLYSUSPEND
	.suspend	= synaptics_ts_suspend,
	.resume		= synaptics_ts_resume,
#endif
	.id_table	= synaptics_ts_id,
	.driver = {
		.name	= SYNAPTICS_I2C_RMI_NAME,
	},
};

static int __devinit synaptics_ts_init(void)
{
	synaptics_wq = create_singlethread_workqueue("synaptics_wq");
	if (!synaptics_wq)
		return -ENOMEM;
	return i2c_add_driver(&synaptics_ts_driver);
}

static void __exit synaptics_ts_exit(void)
{
	i2c_del_driver(&synaptics_ts_driver);
	if (synaptics_wq)
		destroy_workqueue(synaptics_wq);
}

module_init(synaptics_ts_init);
module_exit(synaptics_ts_exit);

MODULE_DESCRIPTION("Synaptics Touchscreen Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Arve Hjønnevåg <arve@android.com>");
