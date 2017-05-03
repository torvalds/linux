/*
 * LED Kernel Multi-Control Trigger
 *
 * Control multi leds at one time using ioctl from userspace.
 *
 * Copyright 2017 Allen Zhang <zwp@rock-chips.com>
 *
 * Based on Richard Purdie's ledtrig-timer.c.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/ioctl.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>
#include <linux/leds.h>
#include "../leds.h"
#include "../leds-multi.h"

struct multi_ctrl_data {
	struct led_ctrl_data *data;
	struct led_classdev *led_cdev;
	struct delayed_work delay_trig_work;
	struct list_head node;
	struct led_ctrl_data old_data;
};

struct multi_ctrl_scroll_data {
	struct led_ctrl_scroll_data *data;
	volatile bool data_valid;
	struct delayed_work scroll_work;
};

struct multi_ctrl_breath_data {
	struct led_ctrl_breath_data *data;
	volatile bool data_valid;
	struct delayed_work breath_work;
};

enum leds_mode {
	LEDS_MODE_MULTI_SET = 0,
	LEDS_MODE_MULTI_SCROLL,
	LEDS_MODE_MULTI_BREATH,
	LEDS_MODE_MULTI_INVALID = 0xff,
};

#define bits(nr)	((u64)1 << (nr))

static DECLARE_RWSEM(multi_leds_list_lock);
static LIST_HEAD(multi_leds_list);

static int led_num;
static struct miscdevice multi_ctrl_miscdev;
static struct multi_ctrl_scroll_data *multi_ctrl_scroll_info;
static struct multi_ctrl_breath_data *multi_ctrl_breath_info;
static char *mult_ctrl_trigger[TRIG_MAX] = {
	"none",
	"default-on",
	"timer",
	"oneshot",
};

static struct led_ctrl_data leds_data[MAX_LEDS_NUMBER];
static struct led_ctrl_scroll_data leds_scroll_data;
static struct led_ctrl_breath_data leds_breath_data;
static enum leds_mode leds_pre_mode = LEDS_MODE_MULTI_INVALID;

static u64 multi_ctrl_calc_next_scroll_bitmap(u64 cur_bitmap)
{
	u64 bitmap = cur_bitmap << leds_scroll_data.shifts;
	u64 ret_bitmap = bitmap & (bits(led_num) - 1);

	if (bitmap > (bits(led_num) - 1))
		ret_bitmap |= bitmap >> led_num;
	else
		ret_bitmap = bitmap;

	return ret_bitmap;
}

static void multi_ctrl_scroll_work_fn(struct work_struct *ws)
{
	struct multi_ctrl_data *ctrl_data;
	int bit = 0;
	static u64 update_bits = ~0;

	down_read(&multi_leds_list_lock);
	if (!multi_ctrl_scroll_info->data_valid) {
		update_bits = bits(led_num) - 1;
		multi_ctrl_scroll_info->data_valid = true;
		pr_info("%s, new scroll work is queued\n", __func__);
	}
	if (unlikely(update_bits > (bits(led_num) - 1))) {
		pr_warn("%s,update_bits is exceed the max led numbers!\n",
			__func__);
		update_bits = bits(led_num) - 1;
	}

	if (leds_pre_mode != LEDS_MODE_MULTI_SCROLL) {
		update_bits = bits(led_num) - 1;
		leds_pre_mode = LEDS_MODE_MULTI_SCROLL;
	}

	list_for_each_entry(ctrl_data, &multi_leds_list, node) {
		struct led_classdev *led_cdev = ctrl_data->led_cdev;

		if (bit >= led_num) {
			dev_err(led_cdev->dev, "exceed the max number of muti_leds_list\n");
			break;
		}

		cancel_delayed_work_sync(&ctrl_data->delay_trig_work);

		/* only change the led status when bits updated */
		if (update_bits & bits(bit)) {
			if (leds_scroll_data.init_bitmap & bits(bit)) {
				led_trigger_set_by_name(led_cdev,
							mult_ctrl_trigger[TRIG_DEF_ON]);
			} else {
				led_trigger_remove(led_cdev);
			}
		}

		bit++;
	}
	update_bits = leds_scroll_data.init_bitmap;
	leds_scroll_data.init_bitmap = multi_ctrl_calc_next_scroll_bitmap(update_bits);
	update_bits ^= leds_scroll_data.init_bitmap;
	up_read(&multi_leds_list_lock);

	schedule_delayed_work(&multi_ctrl_scroll_info->scroll_work,
			      msecs_to_jiffies(leds_scroll_data.shift_delay_ms));
}

static void multi_ctrl_breath_work_fn(struct work_struct *ws)
{
	struct multi_ctrl_data *ctrl_data;
	int bit = 0;
	u32 bri_every_step;
	static u32 pre_brightness = LED_HALF;
	static u64 pre_bg_bitmap;
	static u64 pre_br_bitmap;
	static u64 pre_black_bitmap;

	down_read(&multi_leds_list_lock);
	if (!multi_ctrl_breath_info->data_valid) {
		pre_bg_bitmap = 0;
		pre_br_bitmap = 0;
		pre_black_bitmap = 0;
		multi_ctrl_breath_info->data_valid = true;
		pr_info("%s, new breath work is queued\n", __func__);
	}

	bri_every_step = LED_FULL / leds_breath_data.breath_steps;
	list_for_each_entry(ctrl_data, &multi_leds_list, node) {
		struct led_classdev *led_cdev = ctrl_data->led_cdev;

		if (bit >= led_num) {
			dev_err(led_cdev->dev, "exceed the max number of muti_leds_list\n");
			break;
		}

		cancel_delayed_work_sync(&ctrl_data->delay_trig_work);

		if (pre_bg_bitmap == 0) {
			if (leds_breath_data.background_bitmap & bits(bit)) {
				led_trigger_set_by_name(led_cdev,
							mult_ctrl_trigger[TRIG_DEF_ON]);
			}
		}

		if (leds_breath_data.breath_bitmap & bits(bit)) {
			/* only update the leds indicated by bitmap*/
			led_cdev->brightness =
				(pre_brightness + bri_every_step) % LED_FULL;
			if (unlikely(pre_br_bitmap == 0))
				led_trigger_set_by_name(led_cdev,
							mult_ctrl_trigger[TRIG_DEF_ON]);
			else
				led_set_brightness_async(led_cdev,
							 led_cdev->brightness);
		}

		if (pre_black_bitmap == 0) {
			if ((~(leds_breath_data.background_bitmap |
			     leds_breath_data.breath_bitmap)) &
			    bits(bit))
				led_trigger_remove(led_cdev);
		}
		bit++;
	}
	pre_brightness = (pre_brightness + bri_every_step) % LED_FULL;
	pre_bg_bitmap = leds_breath_data.background_bitmap;
	pre_br_bitmap = leds_breath_data.breath_bitmap;
	pre_black_bitmap = ~(pre_bg_bitmap | pre_br_bitmap);
	leds_pre_mode = LEDS_MODE_MULTI_BREATH;
	up_read(&multi_leds_list_lock);

	schedule_delayed_work(&multi_ctrl_breath_info->breath_work,
			      msecs_to_jiffies(leds_breath_data.change_delay_ms));
}

static void multi_ctrl_delay_trig_func(struct work_struct *ws)
{
	struct multi_ctrl_data *ctrl_data =
		container_of(ws, struct multi_ctrl_data, delay_trig_work.work);
	struct led_classdev *led_cdev = ctrl_data->led_cdev;
	struct led_ctrl_data *led_data = ctrl_data->data;

	/* set brightness*/
	if (led_data->brightness == LED_OFF) {
		led_trigger_remove(led_cdev);
		return;
	}
	/* set delay_on and delay_off */
	led_cdev->blink_delay_off = led_data->delay_off;
	led_cdev->blink_delay_on = led_data->delay_on;
	led_cdev->brightness = led_data->brightness;

	led_trigger_set_by_name(led_cdev, mult_ctrl_trigger[led_data->trigger]);

	if (led_data->trigger == TRIG_ONESHOT)
		led_blink_set_oneshot(led_cdev,
				      &led_cdev->blink_delay_on,
				      &led_cdev->blink_delay_off, 0);
}

static int multi_ctrl_set_led(struct multi_ctrl_data *ctrl_data)
{
	struct led_ctrl_data *led_data = ctrl_data->data;
	struct led_classdev *led_cdev = ctrl_data->led_cdev;

	if (!led_data || led_data->trigger >= TRIG_MAX)
		return -EINVAL;

	if (led_data->delayed_trigger_ms &&
	    (led_data->trigger == TRIG_TIMER ||
	    led_data->trigger == TRIG_ONESHOT)) {
		schedule_delayed_work(&ctrl_data->delay_trig_work,
				      msecs_to_jiffies(led_data->delayed_trigger_ms));
	} else {
		/* set brightness*/
		if (led_data->brightness == LED_OFF ||
		    led_data->trigger == TRIG_NONE) {
			led_trigger_remove(led_cdev);
			return 0;
		}
		/* set delay_on and delay_off */
		led_cdev->blink_delay_off = led_data->delay_off;
		led_cdev->blink_delay_on = led_data->delay_on;
		led_cdev->brightness = led_data->brightness;

		led_trigger_set_by_name(led_cdev,
					mult_ctrl_trigger[led_data->trigger]);

		if (led_data->trigger == TRIG_ONESHOT)
			led_blink_set_oneshot(led_cdev,
					      &led_cdev->blink_delay_on,
					      &led_cdev->blink_delay_off, 0);
	}

	return 0;
}

static void cancel_all_work_sync(void)
{
	multi_ctrl_scroll_info->data_valid = false;
	multi_ctrl_breath_info->data_valid = false;
	cancel_delayed_work_sync(&multi_ctrl_scroll_info->scroll_work);
	cancel_delayed_work_sync(&multi_ctrl_breath_info->breath_work);
}

static int multi_ctrl_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int multi_ctrl_release(struct inode *inode, struct file *file)
{
	return 0;
}

static long multi_ctrl_ioctl(struct file *file,
			     unsigned int cmd, unsigned long arg)
{
	int i = 0;
	int ret = 0;

	cancel_all_work_sync();

	switch (cmd) {
	case LEDS_MULTI_CTRL_IOCTL_MULTI_SET:
	{
		struct led_ctrl_data *argp = (struct led_ctrl_data *)arg;
		struct multi_ctrl_data *ctrl_data;
		bool mode_changed = false;

		down_read(&multi_leds_list_lock);

		if (leds_pre_mode != LEDS_MODE_MULTI_SET) {
			leds_pre_mode = LEDS_MODE_MULTI_SET;
			mode_changed = true;
		}

		if (copy_from_user(leds_data, argp,
				   sizeof(struct led_ctrl_data) * led_num)) {
			pr_err("%s, copy from user failed\n", __func__);
			up_read(&multi_leds_list_lock);
			ret = -EFAULT;
			break;
		}
		list_for_each_entry(ctrl_data, &multi_leds_list, node) {
			struct led_classdev *led_cdev = ctrl_data->led_cdev;

			cancel_delayed_work_sync(&ctrl_data->delay_trig_work);

			if (i >= led_num) {
				dev_err(led_cdev->dev, "exceed the max number of muti_leds_list\n");
				break;
			}
			ctrl_data->data = &leds_data[i++];
			if (!memcmp(&ctrl_data->old_data, ctrl_data->data,
				    sizeof(struct led_ctrl_data)) &&
				    !mode_changed) {
				continue;
			}
			multi_ctrl_set_led(ctrl_data);
			memcpy(&ctrl_data->old_data, ctrl_data->data,
			       sizeof(struct led_ctrl_data));
		}
		up_read(&multi_leds_list_lock);
		break;
	}
	case LEDS_MULTI_CTRL_IOCTL_GET_LED_NUMBER:
	{
		int __user *p = (int __user *)arg;

		ret = put_user(led_num, p);
		break;
	}
	case LEDS_MULTI_CTRL_IOCTL_MULTI_SET_SCROLL:
	{
		if (led_num > sizeof(leds_scroll_data.init_bitmap) * 8) {
			pr_err("registered leds is exeeded the size of scroll bitmap!\n");
			ret = -EINVAL;
			break;
		}

		if (copy_from_user(&leds_scroll_data,
				   (struct led_ctrl_scroll_data *)arg,
				   sizeof(struct led_ctrl_scroll_data))) {
			pr_err("%s, set scroll mode, copy from user failed\n",
			       __func__);
			ret = -EFAULT;
			break;
		}
		schedule_delayed_work(&multi_ctrl_scroll_info->scroll_work, 0);
		break;
	}
	case LEDS_MULTI_CTRL_IOCTL_MULTI_SET_BREATH:
	{
		if (led_num > sizeof(leds_breath_data.breath_bitmap) * 8) {
			pr_err("registered leds is exeeded the size of breath bitmap!\n");
			ret = -EINVAL;
			break;
		}

		/*
		 * background color bitmap will be set to default-on mode,
		 * so we can't set a bit in background bits if it's one of
		 * bit in breath color bitmap.
		 */
		leds_breath_data.background_bitmap &=
					~leds_breath_data.breath_bitmap;
		if (copy_from_user(&leds_breath_data,
				   (struct led_ctrl_breath_data *)arg,
				   sizeof(leds_breath_data))) {
			pr_err("%s, set breath mode, copy from user failed\n",
			       __func__);
			ret = -EFAULT;
			break;
		}
		schedule_delayed_work(&multi_ctrl_breath_info->breath_work, 0);
		break;
	}
	default:
		break;
	}

	return ret;
}

static const struct file_operations multi_ctrl_ops = {
	.owner = THIS_MODULE,
	.open = multi_ctrl_open,
	.release = multi_ctrl_release,
	.unlocked_ioctl = multi_ctrl_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = multi_ctrl_ioctl,
#endif
};

int led_multi_control_register(struct led_classdev *led_cdev)
{
	struct multi_ctrl_data *data;

	if (led_num++ >= MAX_LEDS_NUMBER)
		return -EINVAL;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data) {
		dev_err(led_cdev->dev, "malloc multi_ctrl_data failed\n");
		return -ENOMEM;
	}

	data->led_cdev = led_cdev;
	data->old_data.brightness = led_cdev->brightness;
	data->old_data.delay_off = led_cdev->blink_delay_off;
	data->old_data.delay_on = led_cdev->blink_delay_on;

	down_write(&multi_leds_list_lock);
	list_add_tail(&data->node, &multi_leds_list);
	up_write(&multi_leds_list_lock);

	INIT_DELAYED_WORK(&data->delay_trig_work,
			  multi_ctrl_delay_trig_func);

	return 0;
}

int led_multi_control_unregister(struct led_classdev *cdev)
{
	struct multi_ctrl_data *ctrl_data;

	if (led_num-- < 0)
		return -EINVAL;

	down_write(&multi_leds_list_lock);
	list_for_each_entry(ctrl_data, &multi_leds_list, node) {
		if (ctrl_data->led_cdev == cdev) {
			cancel_delayed_work_sync(&ctrl_data->delay_trig_work);
			list_del(&ctrl_data->node);
			break;
		}
	}
	up_write(&multi_leds_list_lock);
	kfree(ctrl_data);

	return 0;
}

int led_multi_control_init(struct device *dev)
{
	int ret;

	multi_ctrl_scroll_info =
			kzalloc(sizeof(*multi_ctrl_scroll_info), GFP_KERNEL);
	if (!multi_ctrl_scroll_info) {
		pr_err("malloc multi_ctrl_scroll_info failed\n");
		return -ENOMEM;
	}
	multi_ctrl_breath_info =
			kzalloc(sizeof(*multi_ctrl_breath_info), GFP_KERNEL);
	if (!multi_ctrl_breath_info) {
		pr_err("malloc leds_breath_info failed\n");
		ret = -ENOMEM;
		goto breath_info_malloc_err;
	}

	multi_ctrl_miscdev.fops = &multi_ctrl_ops;
	multi_ctrl_miscdev.parent = dev;
	multi_ctrl_miscdev.name = "led_multi_ctrl";
	ret = misc_register(&multi_ctrl_miscdev);
	if (ret < 0) {
		pr_err("Can't register misc dev for led multi-control.\n");
		goto miscdev_register_err;
	}
	led_num = 0;

	multi_ctrl_scroll_info->data = &leds_scroll_data;
	INIT_DELAYED_WORK(&multi_ctrl_scroll_info->scroll_work,
			  multi_ctrl_scroll_work_fn);
	multi_ctrl_breath_info->data = &leds_breath_data;
	INIT_DELAYED_WORK(&multi_ctrl_breath_info->breath_work,
			  multi_ctrl_breath_work_fn);

	return 0;

miscdev_register_err:
	kfree(multi_ctrl_breath_info);
	multi_ctrl_breath_info = NULL;

breath_info_malloc_err:
	kfree(multi_ctrl_scroll_info);
	multi_ctrl_scroll_info = NULL;

	return ret;
}

int led_multi_control_exit(struct device *dev)
{
	misc_deregister(&multi_ctrl_miscdev);
	cancel_delayed_work_sync(&multi_ctrl_scroll_info->scroll_work);
	cancel_delayed_work_sync(&multi_ctrl_breath_info->breath_work);
	if (multi_ctrl_scroll_info) {
		kfree(multi_ctrl_scroll_info);
		multi_ctrl_scroll_info = NULL;
	}
	if (multi_ctrl_breath_info) {
		kfree(multi_ctrl_breath_info);
		multi_ctrl_breath_info = NULL;
	}

	return 0;
}

MODULE_AUTHOR("Allen.zhang <zwp@rock-chips.com>");
MODULE_DESCRIPTION("Multi-Contorl LED trigger");
MODULE_LICENSE("GPL");
