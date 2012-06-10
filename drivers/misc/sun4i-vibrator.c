/* Vibrator driver for sun4i platform
 * ported from msm pmic vibrator driver
 *  by tom cubie <tangliang@allwinnertech.com>
 *
 * Copyright (C) 2011 AllWinner Technology.
 *
 * Copyright (C) 2008 HTC Corporation.
 * Copyright (C) 2007 Google, Inc.
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
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/hrtimer.h>
#include <linux/timed_output.h>
//#include <linux/ktime.h>

#include <mach/system.h>
#include <mach/hardware.h>
#include <plat/sys_config.h>

static struct work_struct vibrator_work;
static struct hrtimer vibe_timer;
static spinlock_t vibe_lock;
static int vibe_state;
static int vibe_off;
static script_gpio_set_t vibe_gpio;
static unsigned vibe_gpio_handler;

static void set_sun4i_vibrator(int on)
{
	if(on) {
		gpio_write_one_pin_value(vibe_gpio_handler, !vibe_off, NULL);
	}
	else{
		gpio_write_one_pin_value(vibe_gpio_handler, vibe_off, NULL);
	}

}

static void update_vibrator(struct work_struct *work)
{
	set_sun4i_vibrator(vibe_state);
}

static void vibrator_enable(struct timed_output_dev *dev, int value)
{
	unsigned long	flags;

	spin_lock_irqsave(&vibe_lock, flags);
	hrtimer_cancel(&vibe_timer);

	if (value <= 0)
		vibe_state = 0;
	else {
		value = (value > 15000 ? 15000 : value);
		vibe_state = 1;
		hrtimer_start(&vibe_timer,
			ktime_set(value / 1000, (value % 1000) * 1000000),
			HRTIMER_MODE_REL);
	}
	spin_unlock_irqrestore(&vibe_lock, flags);

	schedule_work(&vibrator_work);
}

static int vibrator_get_time(struct timed_output_dev *dev)
{
	struct timespec time_tmp;
	if (hrtimer_active(&vibe_timer)) {
		ktime_t r = hrtimer_get_remaining(&vibe_timer);
		time_tmp = ktime_to_timespec(r);
		//return r.tv.sec * 1000 + r.tv.nsec/1000000;
		return time_tmp.tv_sec* 1000 + time_tmp.tv_nsec/1000000;
	} else
		return 0;
}

static enum hrtimer_restart vibrator_timer_func(struct hrtimer *timer)
{
	vibe_state = 0;
	schedule_work(&vibrator_work);
	return HRTIMER_NORESTART;
}

static struct timed_output_dev sun4i_vibrator = {
	.name = "sun4i-vibrator",
	.get_time = vibrator_get_time,
	.enable = vibrator_enable,
};

static int __init sun4i_vibrator_init(void)
{
	int vibe_used;
	int err = -1;

	pr_info("hello, sun4i_vibrator init\n");
	err = script_parser_fetch("motor_para", "motor_used",
					&vibe_used, sizeof(vibe_used)/sizeof(int));
	if(err) {
		pr_err("%s script_parser_fetch \"motor_para\" \"motor_used\" error = %d\n",
				__FUNCTION__, err);
		goto exit;
	}

	if(!vibe_used) {
		pr_err("%s motor is not used in config\n", __FUNCTION__);
		err = -1;
		goto exit;
	}

	err = script_parser_fetch("motor_para", "motor_shake",
					(int *)&vibe_gpio, sizeof(vibe_gpio)/sizeof(int));

	if(err) {
		pr_err("%s script_parser_fetch \"motor_para\" \"motor_shaked\" error = %d\n",
				__FUNCTION__, err);
		goto exit;
	}

	vibe_off = vibe_gpio.data;
	pr_debug("vibe_off is %d\n", vibe_off);

	vibe_gpio_handler = gpio_request_ex("motor_para", "motor_shake");

	if(!vibe_gpio_handler) {
		pr_err("%s request motor gpio err\n", __FUNCTION__);
		err = -1;
		goto exit;
	}

	INIT_WORK(&vibrator_work, update_vibrator);

	spin_lock_init(&vibe_lock);
	vibe_state = 0;
	hrtimer_init(&vibe_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	vibe_timer.function = vibrator_timer_func;

	timed_output_dev_register(&sun4i_vibrator);

exit:
	return err;
}

static void __exit sun4i_vibrator_exit(void)
{
	pr_info("bye, sun4i_vibrator_exit\n");
	timed_output_dev_unregister(&sun4i_vibrator);
	gpio_release(vibe_gpio_handler, 0);
}
module_init(sun4i_vibrator_init);
module_exit(sun4i_vibrator_exit);

MODULE_DESCRIPTION("timed output vibrator device for sun4i");
MODULE_LICENSE("GPL");

