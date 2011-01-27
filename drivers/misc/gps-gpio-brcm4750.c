/*
 * Copyright (C) 2010 Motorola, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307, USA
 */

#include <linux/err.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/gps-gpio-brcm4750.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

struct gps_gpio_brcm4750_platform_data *gps_gpio_data;
/* Wakeup timer state definition */

enum timer_state {
	/* Timer is inactive */
	TIMER_INACTIVE = 0,
	/* Timer is active, waitting for timeout */
	TIMER_ACTIVE,
	/* Timer has timeout, has wokenup process, waiting for next poll */
	TIMER_EXPIRED,
	/* Timer is unused. */
	TIMER_INVALID
};

static int gps_start_wakeup_timer( struct file *filp, unsigned long timer_val_msecs)
{
	ktime_t low_interval = ktime_set(timer_val_msecs/MSEC_PER_SEC,
		(timer_val_msecs%MSEC_PER_SEC)*NSEC_PER_MSEC);
	/* set the alarm expiry window to 500 msecs */
	ktime_t slack = ktime_set(0, 500*NSEC_PER_MSEC);
	ktime_t next = ktime_add(alarm_get_elapsed_realtime(), low_interval);
	/* set filp structure if it is first time timer is scheduled */
	if (filp->private_data == NULL)
	{
		filp->private_data = (void *)gps_gpio_data;
	}
	alarm_cancel(&gps_gpio_data->alarm);
	gps_gpio_data->timer_status = TIMER_ACTIVE;
	alarm_start_range(&gps_gpio_data->alarm, next, ktime_add(next, slack));
	return 0;
}

static int gps_stop_wakeup_timer( struct file *filp)
{
	if (filp->private_data == NULL)
		return 0;

	alarm_cancel(&gps_gpio_data->alarm);
	gps_gpio_data->timer_status = TIMER_INACTIVE;
	return 0;
}

static void gps_brcm4750_alarm(struct alarm* alarm)
{

	struct gps_gpio_brcm4750_platform_data *gps_gpio_data =
                                container_of(alarm, struct gps_gpio_brcm4750_platform_data, alarm);
	gps_gpio_data->timer_status = TIMER_EXPIRED;

	wake_lock_timeout(&gps_gpio_data->gps_brcm4750_wake, 5* HZ);
	/* trigger poll wait */
	wake_up_interruptible(&(gps_gpio_data->gps_brcm4750_wq));
}


static unsigned int gps_brcm_4750_poll(struct file *filp, poll_table *wait)
{
	unsigned int ret = 0;
	struct gps_gpio_brcm4750_platform_data *gps_gpio_data;
	/* If the timer is not present, do not permit this operation */
	if (filp->private_data == NULL)
	{
		return -EPERM;
	}

	gps_gpio_data = (struct gps_gpio_brcm4750_platform_data *)filp->private_data;
	if (gps_gpio_data->timer_status == TIMER_INVALID ||
		gps_gpio_data->timer_status == TIMER_INACTIVE)
	{
		return -EPERM;
	}

	/* Check whether the timer has already expired */
	if (gps_gpio_data->timer_status == TIMER_EXPIRED) {
		gps_gpio_data->timer_status = TIMER_INACTIVE;
		return POLLIN;
	}
	/* release wake lock before poll wait */

	wake_unlock(&gps_gpio_data->gps_brcm4750_wake);
	poll_wait(filp, &(gps_gpio_data->gps_brcm4750_wq), wait);

	if (gps_gpio_data->timer_status == TIMER_EXPIRED) {
		gps_gpio_data->timer_status = TIMER_INACTIVE;
		ret = POLLIN;
	}
	return ret;
}
static long gps_brcm4750_ioctl(struct file *filp,
			       unsigned int cmd, unsigned long arg)
{
	unsigned int gpio_val;

	if (cmd <= 0)
		return -EINVAL;

	switch (cmd) {
	case IOC_GPS_GPIO_RESET:
		if (copy_from_user((void *) &gpio_val, (void *) arg,
				sizeof(int)))
			return -EFAULT;
		if (!(gpio_val == 0 || gpio_val == 1))
			return -EINVAL;
		pr_info("%s: Setting gps gpio reset pin: %d\n",
			__func__, gpio_val);
		if (gps_gpio_data->set_reset_gpio)
			gps_gpio_data->set_reset_gpio(gpio_val);
		break;
	case IOC_GPS_GPIO_STANDBY:
		if (copy_from_user((void *) &gpio_val, (void *) arg,
				sizeof(int)))
			return -EFAULT;
		if (!(gpio_val == 0 || gpio_val == 1))
			return -EINVAL;
		pr_info("%s: Setting gps gpio standby pin to: %d\n",
			__func__, gpio_val);
		if (gps_gpio_data->set_standby_gpio)
			gps_gpio_data->set_standby_gpio(gpio_val);
		break;
	case IOC_GPS_START_TIMER:
		gps_start_wakeup_timer(filp, (unsigned long)arg);
		break;
	case IOC_GPS_STOP_TIMER:
		gps_stop_wakeup_timer(filp);
        break;
	default:
		pr_info("%s: Invalid GPS GPIO IOCTL command\n", __func__);
		return -EINVAL;
	}

	return 0;
}

static const struct file_operations gps_brcm4750_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl	= gps_brcm4750_ioctl,
	.poll = gps_brcm_4750_poll,
};

static struct miscdevice gps_gpio_miscdev = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= GPS_GPIO_DRIVER_NAME,
	.fops	= &gps_brcm4750_fops,
};

static int gps_gpio_brcm4750_probe(struct platform_device *pdev)
{
	gps_gpio_data = pdev->dev.platform_data;
	wake_lock_init(&gps_gpio_data->gps_brcm4750_wake, WAKE_LOCK_SUSPEND,
			"gps-brcm4750");
	alarm_init(&gps_gpio_data->alarm, ANDROID_ALARM_ELAPSED_REALTIME_WAKEUP,
			gps_brcm4750_alarm);
	init_waitqueue_head(&(gps_gpio_data->gps_brcm4750_wq));
	gps_gpio_data->timer_status = TIMER_INVALID;
	if (misc_register(&gps_gpio_miscdev)) {
		pr_info("%s: gps_brcm4750 misc_register failed\n", __func__);
		return -1;
	}
	return 0;
}

static int gps_gpio_brcm4750_remove(struct platform_device *pdev)
{
	if (gps_gpio_data->free_gpio)
		gps_gpio_data->free_gpio();
	return 0;
}

static struct platform_driver gps_gpio_brcm4750_driver = {
	.probe		= gps_gpio_brcm4750_probe,
	.remove		= gps_gpio_brcm4750_remove,
	.driver		= {
		.name		= GPS_GPIO_DRIVER_NAME,
		.owner		= THIS_MODULE,
	},
};

static int __init gps_gpio_brcm4750_init(void)
{
	return platform_driver_register(&gps_gpio_brcm4750_driver);
}

static void __exit gps_gpio_brcm4750_exit(void)
{
	platform_driver_unregister(&gps_gpio_brcm4750_driver);
}

module_init(gps_gpio_brcm4750_init);
module_exit(gps_gpio_brcm4750_exit);

MODULE_AUTHOR("Motorola");
MODULE_DESCRIPTION("GPS GPIO Controller and wake up timer for BRCM 4750");
MODULE_LICENSE("GPL");
