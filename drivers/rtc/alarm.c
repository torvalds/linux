/* drivers/rtc/alarm.c
 *
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

#include <asm/mach/time.h>
#include <linux/android_alarm.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/sysdev.h>
#include <linux/wakelock.h>

#define ANDROID_ALARM_PRINT_ERRORS (1U << 0)
#define ANDROID_ALARM_PRINT_INIT_STATUS (1U << 1)
#define ANDROID_ALARM_PRINT_INFO (1U << 2)
#define ANDROID_ALARM_PRINT_IO (1U << 3)
#define ANDROID_ALARM_PRINT_INT (1U << 4)
#define ANDROID_ALARM_PRINT_FLOW (1U << 5)

#if 0
#define ANDROID_ALARM_DPRINTF_MASK (~0)
#define ANDROID_ALARM_DPRINTF(debug_level_mask, args...) \
	do { \
		if (ANDROID_ALARM_DPRINTF_MASK & debug_level_mask) { \
			printk(args); \
		} \
	} while (0)
#else
#define ANDROID_ALARM_DPRINTF(args...)
#endif

#define ANDROID_ALARM_WAKEUP_MASK ( \
	ANDROID_ALARM_RTC_WAKEUP_MASK | \
	ANDROID_ALARM_ELAPSED_REALTIME_WAKEUP_MASK)

/* support old usespace code */
#define ANDROID_ALARM_SET_OLD               _IOW('a', 2, time_t) /* set alarm */
#define ANDROID_ALARM_SET_AND_WAIT_OLD      _IOW('a', 3, time_t)

static struct rtc_device *alarm_rtc_dev;
static int alarm_opened;
static DEFINE_SPINLOCK(alarm_slock);
static DEFINE_MUTEX(alarm_setrtc_mutex);
static struct wake_lock alarm_wake_lock;
static struct wake_lock alarm_rtc_wake_lock;
static DECLARE_WAIT_QUEUE_HEAD(alarm_wait_queue);
static uint32_t alarm_pending;
static uint32_t alarm_enabled;
static uint32_t wait_pending;
static struct platform_device *alarm_platform_dev;
static struct hrtimer alarm_timer[ANDROID_ALARM_TYPE_COUNT];
static struct timespec alarm_time[ANDROID_ALARM_TYPE_COUNT];
static struct timespec elapsed_rtc_delta;

static void alarm_start_hrtimer(enum android_alarm_type alarm_type)
{
	struct timespec hr_alarm_time;
	if (!(alarm_enabled & (1U << alarm_type)))
		return;
	hr_alarm_time = alarm_time[alarm_type];
	if (alarm_type == ANDROID_ALARM_ELAPSED_REALTIME_WAKEUP ||
	    alarm_type == ANDROID_ALARM_ELAPSED_REALTIME)
		set_normalized_timespec(&hr_alarm_time,
			hr_alarm_time.tv_sec + elapsed_rtc_delta.tv_sec,
			hr_alarm_time.tv_nsec + elapsed_rtc_delta.tv_nsec);
	ANDROID_ALARM_DPRINTF(ANDROID_ALARM_PRINT_FLOW,
		"alarm start hrtimer %d at %ld.%09ld\n",
		alarm_type, hr_alarm_time.tv_sec, hr_alarm_time.tv_nsec);
	hrtimer_start(&alarm_timer[alarm_type],
		      timespec_to_ktime(hr_alarm_time), HRTIMER_MODE_ABS);
}

static long alarm_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int rv = 0;
	unsigned long flags;
	int i;
	struct timespec new_alarm_time;
	struct timespec new_rtc_time;
	struct timespec tmp_time;
	struct rtc_time rtc_new_rtc_time;
	enum android_alarm_type alarm_type = ANDROID_ALARM_IOCTL_TO_TYPE(cmd);
	uint32_t alarm_type_mask = 1U << alarm_type;

	if (alarm_type >= ANDROID_ALARM_TYPE_COUNT)
		return -EINVAL;

	if (ANDROID_ALARM_BASE_CMD(cmd) != ANDROID_ALARM_GET_TIME(0)) {
		if ((file->f_flags & O_ACCMODE) == O_RDONLY)
			return -EPERM;
		if (file->private_data == NULL &&
		    cmd != ANDROID_ALARM_SET_RTC) {
			spin_lock_irqsave(&alarm_slock, flags);
			if (alarm_opened) {
				spin_unlock_irqrestore(&alarm_slock, flags);
				return -EBUSY;
			}
			alarm_opened = 1;
			file->private_data = (void *)1;
			spin_unlock_irqrestore(&alarm_slock, flags);
		}
	}

	switch (ANDROID_ALARM_BASE_CMD(cmd)) {
	case ANDROID_ALARM_CLEAR(0):
		spin_lock_irqsave(&alarm_slock, flags);
		ANDROID_ALARM_DPRINTF(ANDROID_ALARM_PRINT_IO,
				      "alarm %d clear\n", alarm_type);
		hrtimer_try_to_cancel(&alarm_timer[alarm_type]);
		if (alarm_pending) {
			alarm_pending &= ~alarm_type_mask;
			if (!alarm_pending && !wait_pending)
				wake_unlock(&alarm_wake_lock);
		}
		alarm_enabled &= ~alarm_type_mask;
		spin_unlock_irqrestore(&alarm_slock, flags);
		break;

	case ANDROID_ALARM_SET_OLD:
	case ANDROID_ALARM_SET_AND_WAIT_OLD:
		if (get_user(new_alarm_time.tv_sec, (int __user *)arg)) {
			rv = -EFAULT;
			goto err1;
		}
		new_alarm_time.tv_nsec = 0;
		goto from_old_alarm_set;

	case ANDROID_ALARM_SET_AND_WAIT(0):
	case ANDROID_ALARM_SET(0):
		if (copy_from_user(&new_alarm_time, (void __user *)arg,
		    sizeof(new_alarm_time))) {
			rv = -EFAULT;
			goto err1;
		}
from_old_alarm_set:
		spin_lock_irqsave(&alarm_slock, flags);
		ANDROID_ALARM_DPRINTF(ANDROID_ALARM_PRINT_IO,
			"alarm %d set %ld.%09ld\n", alarm_type,
			new_alarm_time.tv_sec, new_alarm_time.tv_nsec);
		alarm_time[alarm_type] = new_alarm_time;
		alarm_enabled |= alarm_type_mask;
		alarm_start_hrtimer(alarm_type);
		spin_unlock_irqrestore(&alarm_slock, flags);
		if (ANDROID_ALARM_BASE_CMD(cmd) != ANDROID_ALARM_SET_AND_WAIT(0)
		    && cmd != ANDROID_ALARM_SET_AND_WAIT_OLD)
			break;
		/* fall though */
	case ANDROID_ALARM_WAIT:
		spin_lock_irqsave(&alarm_slock, flags);
		ANDROID_ALARM_DPRINTF(ANDROID_ALARM_PRINT_IO, "alarm wait\n");
		if (!alarm_pending && wait_pending) {
			wake_unlock(&alarm_wake_lock);
			wait_pending = 0;
		}
		spin_unlock_irqrestore(&alarm_slock, flags);
		rv = wait_event_interruptible(alarm_wait_queue, alarm_pending);
		if (rv)
			goto err1;
		spin_lock_irqsave(&alarm_slock, flags);
		rv = alarm_pending;
		wait_pending = 1;
		alarm_pending = 0;
		if (rv & ANDROID_ALARM_WAKEUP_MASK)
			wake_unlock(&alarm_rtc_wake_lock);
		spin_unlock_irqrestore(&alarm_slock, flags);
		break;
	case ANDROID_ALARM_SET_RTC:
		if (copy_from_user(&new_rtc_time, (void __user *)arg,
		    sizeof(new_rtc_time))) {
			rv = -EFAULT;
			goto err1;
		}
		rtc_time_to_tm(new_rtc_time.tv_sec, &rtc_new_rtc_time);

		ANDROID_ALARM_DPRINTF(ANDROID_ALARM_PRINT_IO,
			"set rtc %ld %ld - rtc %02d:%02d:%02d %02d/%02d/%04d\n",
			new_rtc_time.tv_sec, new_rtc_time.tv_nsec,
			rtc_new_rtc_time.tm_hour, rtc_new_rtc_time.tm_min,
			rtc_new_rtc_time.tm_sec, rtc_new_rtc_time.tm_mon + 1,
			rtc_new_rtc_time.tm_mday,
			rtc_new_rtc_time.tm_year + 1900);

		mutex_lock(&alarm_setrtc_mutex);
		spin_lock_irqsave(&alarm_slock, flags);
		for (i = 0; i < ANDROID_ALARM_SYSTEMTIME; i++)
			hrtimer_try_to_cancel(&alarm_timer[i]);
		getnstimeofday(&tmp_time);
		elapsed_rtc_delta = timespec_sub(elapsed_rtc_delta,
					timespec_sub(tmp_time, new_rtc_time));
		spin_unlock_irqrestore(&alarm_slock, flags);
		rv = do_settimeofday(&new_rtc_time);
		spin_lock_irqsave(&alarm_slock, flags);
		for (i = 0; i < ANDROID_ALARM_SYSTEMTIME; i++)
			alarm_start_hrtimer(i);
		spin_unlock_irqrestore(&alarm_slock, flags);
		if (rv < 0) {
			ANDROID_ALARM_DPRINTF(ANDROID_ALARM_PRINT_ERRORS,
					      "Failed to set time\n");
			mutex_unlock(&alarm_setrtc_mutex);
			goto err1;
		}
		rv = rtc_set_time(alarm_rtc_dev, &rtc_new_rtc_time);
		spin_lock_irqsave(&alarm_slock, flags);
		alarm_pending |= ANDROID_ALARM_TIME_CHANGE_MASK;
		wake_up(&alarm_wait_queue);
		spin_unlock_irqrestore(&alarm_slock, flags);
		mutex_unlock(&alarm_setrtc_mutex);
		if (rv < 0) {
			ANDROID_ALARM_DPRINTF(ANDROID_ALARM_PRINT_ERRORS,
			    "Failed to set RTC, time will be lost on reboot\n");
			goto err1;
		}
		break;
	case ANDROID_ALARM_GET_TIME(0):
		mutex_lock(&alarm_setrtc_mutex);
		spin_lock_irqsave(&alarm_slock, flags);
		if (alarm_type != ANDROID_ALARM_SYSTEMTIME) {
			getnstimeofday(&tmp_time);
			if (alarm_type >= ANDROID_ALARM_ELAPSED_REALTIME_WAKEUP)
				tmp_time = timespec_sub(tmp_time,
							elapsed_rtc_delta);
		} else
			ktime_get_ts(&tmp_time);
		spin_unlock_irqrestore(&alarm_slock, flags);
		mutex_unlock(&alarm_setrtc_mutex);
		if (copy_to_user((void __user *)arg, &tmp_time,
		    sizeof(tmp_time))) {
			rv = -EFAULT;
			goto err1;
		}
		break;

	default:
		rv = -EINVAL;
		goto err1;
	}
err1:
	return rv;
}

static int alarm_open(struct inode *inode, struct file *file)
{
	file->private_data = NULL;
	return 0;
}

static int alarm_release(struct inode *inode, struct file *file)
{
	int i;
	unsigned long flags;

	spin_lock_irqsave(&alarm_slock, flags);
	if (file->private_data != 0) {
		for (i = 0; i < ANDROID_ALARM_TYPE_COUNT; i++) {
			uint32_t alarm_type_mask = 1U << i;
			if (alarm_enabled & alarm_type_mask) {
				ANDROID_ALARM_DPRINTF(ANDROID_ALARM_PRINT_INFO,
					"alarm_release: clear alarm, "
					"pending %d\n",
					!!(alarm_pending & alarm_type_mask));
				alarm_enabled &= ~alarm_type_mask;
			}
			spin_unlock_irqrestore(&alarm_slock, flags);
			hrtimer_cancel(&alarm_timer[i]);
			spin_lock_irqsave(&alarm_slock, flags);
		}
		if (alarm_pending | wait_pending) {
			if (alarm_pending)
				ANDROID_ALARM_DPRINTF(ANDROID_ALARM_PRINT_INFO,
					"alarm_release: clear pending alarms "
					"%x\n", alarm_pending);
			wake_unlock(&alarm_wake_lock);
			wait_pending = 0;
			alarm_pending = 0;
		}
		alarm_opened = 0;
	}
	spin_unlock_irqrestore(&alarm_slock, flags);
	return 0;
}

static enum hrtimer_restart alarm_timer_triggered(struct hrtimer *timer)
{
	unsigned long flags;
	enum android_alarm_type alarm_type = (timer - alarm_timer);
	uint32_t alarm_type_mask = 1U << alarm_type;


	ANDROID_ALARM_DPRINTF(ANDROID_ALARM_PRINT_INT,
			      "alarm_timer_triggered type %d\n", alarm_type);
	spin_lock_irqsave(&alarm_slock, flags);
	if (alarm_enabled & alarm_type_mask) {
		wake_lock_timeout(&alarm_wake_lock, 5 * HZ);
		alarm_enabled &= ~alarm_type_mask;
		alarm_pending |= alarm_type_mask;
		wake_up(&alarm_wait_queue);
	}
	spin_unlock_irqrestore(&alarm_slock, flags);
	return HRTIMER_NORESTART;
}

static void alarm_triggered_func(void *p)
{
	struct rtc_device *rtc = alarm_rtc_dev;
	if (!(rtc->irq_data & RTC_AF))
		return;
	ANDROID_ALARM_DPRINTF(ANDROID_ALARM_PRINT_INT, "rtc alarm triggered\n");
	wake_lock_timeout(&alarm_rtc_wake_lock, 1 * HZ);
}

int alarm_suspend(struct platform_device *pdev, pm_message_t state)
{
	int                 err = 0;
	unsigned long       flags;
	struct rtc_wkalrm   rtc_alarm;
	struct rtc_time     rtc_current_rtc_time;
	unsigned long       rtc_current_time;
	unsigned long       rtc_alarm_time;
	struct timespec     rtc_current_timespec;
	struct timespec     rtc_delta;
	struct timespec     elapsed_realtime_alarm_time;

	ANDROID_ALARM_DPRINTF(ANDROID_ALARM_PRINT_FLOW,
			      "alarm_suspend(%p, %d)\n", pdev, state.event);
	spin_lock_irqsave(&alarm_slock, flags);
	if (alarm_pending && !wake_lock_active(&alarm_wake_lock)) {
		ANDROID_ALARM_DPRINTF(ANDROID_ALARM_PRINT_INFO,
				      "alarm pending\n");
		err = -EBUSY;
		goto err1;
	}
	if (alarm_enabled & ANDROID_ALARM_WAKEUP_MASK) {
		spin_unlock_irqrestore(&alarm_slock, flags);
		if (alarm_enabled & ANDROID_ALARM_RTC_WAKEUP_MASK)
			hrtimer_cancel(&alarm_timer[ANDROID_ALARM_RTC_WAKEUP]);
		if (alarm_enabled & ANDROID_ALARM_ELAPSED_REALTIME_WAKEUP_MASK)
			hrtimer_cancel(&alarm_timer[
					ANDROID_ALARM_ELAPSED_REALTIME_WAKEUP]);

		rtc_read_time(alarm_rtc_dev, &rtc_current_rtc_time);
		rtc_current_timespec.tv_nsec = 0;
		rtc_tm_to_time(&rtc_current_rtc_time,
			       &rtc_current_timespec.tv_sec);
		save_time_delta(&rtc_delta, &rtc_current_timespec);
		set_normalized_timespec(&elapsed_realtime_alarm_time,
			alarm_time[ANDROID_ALARM_ELAPSED_REALTIME_WAKEUP]
			.tv_sec + elapsed_rtc_delta.tv_sec,
			alarm_time[ANDROID_ALARM_ELAPSED_REALTIME_WAKEUP]
			.tv_nsec + elapsed_rtc_delta.tv_nsec);
		if ((alarm_enabled & ANDROID_ALARM_RTC_WAKEUP_MASK) &&
		    (!(alarm_enabled &
		       ANDROID_ALARM_ELAPSED_REALTIME_WAKEUP_MASK) ||
		     timespec_compare(&alarm_time[ANDROID_ALARM_RTC_WAKEUP],
				      &elapsed_realtime_alarm_time) < 0))
			rtc_alarm_time = timespec_sub(
					alarm_time[ANDROID_ALARM_RTC_WAKEUP],
					rtc_delta).tv_sec;
		else
			rtc_alarm_time = timespec_sub(
				elapsed_realtime_alarm_time, rtc_delta).tv_sec;
		rtc_time_to_tm(rtc_alarm_time, &rtc_alarm.time);
		rtc_alarm.enabled = 1;
		rtc_set_alarm(alarm_rtc_dev, &rtc_alarm);
		rtc_read_time(alarm_rtc_dev, &rtc_current_rtc_time);
		rtc_tm_to_time(&rtc_current_rtc_time, &rtc_current_time);
		ANDROID_ALARM_DPRINTF(ANDROID_ALARM_PRINT_INFO,
			"rtc alarm set at %ld, now %ld, rtc delta %ld.%09ld\n",
			rtc_alarm_time, rtc_current_time,
			rtc_delta.tv_sec, rtc_delta.tv_nsec);
		if (rtc_current_time + 1 >= rtc_alarm_time) {
			ANDROID_ALARM_DPRINTF(ANDROID_ALARM_PRINT_INFO,
					      "alarm about to go off\n");
			memset(&rtc_alarm, 0, sizeof(rtc_alarm));
			rtc_alarm.enabled = 0;
			rtc_set_alarm(alarm_rtc_dev, &rtc_alarm);

			spin_lock_irqsave(&alarm_slock, flags);
			wake_lock_timeout(&alarm_rtc_wake_lock, 2 * HZ);
			alarm_start_hrtimer(ANDROID_ALARM_RTC_WAKEUP);
			alarm_start_hrtimer(
				ANDROID_ALARM_ELAPSED_REALTIME_WAKEUP);
			err = -EBUSY;
			spin_unlock_irqrestore(&alarm_slock, flags);
		}
	} else {
err1:
		spin_unlock_irqrestore(&alarm_slock, flags);
	}
	return err;
}

int alarm_resume(struct platform_device *pdev)
{
	struct rtc_wkalrm alarm;
	ANDROID_ALARM_DPRINTF(ANDROID_ALARM_PRINT_FLOW,
			      "alarm_resume(%p)\n", pdev);
	if (alarm_enabled & ANDROID_ALARM_WAKEUP_MASK) {
		memset(&alarm, 0, sizeof(alarm));
		alarm.enabled = 0;
		rtc_set_alarm(alarm_rtc_dev, &alarm);
		alarm_start_hrtimer(ANDROID_ALARM_RTC_WAKEUP);
		alarm_start_hrtimer(ANDROID_ALARM_ELAPSED_REALTIME_WAKEUP);
	}
	return 0;
}

static struct rtc_task alarm_rtc_task = {
	.func = alarm_triggered_func
};

static struct file_operations alarm_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = alarm_ioctl,
	.open = alarm_open,
	.release = alarm_release,
};

static struct miscdevice alarm_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "alarm",
	.fops = &alarm_fops,
};

static int rtc_alarm_add_device(struct device *dev,
				struct class_interface *class_intf)
{
	int err;
	struct rtc_device *rtc = to_rtc_device(dev);

	mutex_lock(&alarm_setrtc_mutex);

	if (alarm_rtc_dev) {
		err = -EBUSY;
		goto err1;
	}

	err = misc_register(&alarm_device);
	if (err)
		goto err1;
	alarm_platform_dev =
		platform_device_register_simple("alarm", -1, NULL, 0);
	if (IS_ERR(alarm_platform_dev)) {
		err = PTR_ERR(alarm_platform_dev);
		goto err2;
	}
	err = rtc_irq_register(rtc, &alarm_rtc_task);
	if (err)
		goto err3;
	alarm_rtc_dev = rtc;
	mutex_unlock(&alarm_setrtc_mutex);

	ANDROID_ALARM_DPRINTF(ANDROID_ALARM_PRINT_INFO, "alarm: parent %p\n",
			      alarm_platform_dev->dev.power.pm_parent);
	return 0;

err3:
	platform_device_unregister(alarm_platform_dev);
err2:
	misc_deregister(&alarm_device);
err1:
	mutex_unlock(&alarm_setrtc_mutex);
	return err;
}

static void rtc_alarm_remove_device(struct device *dev,
				    struct class_interface *class_intf)
{
	if (dev == &alarm_rtc_dev->dev) {
		rtc_irq_unregister(alarm_rtc_dev, &alarm_rtc_task);
		platform_device_unregister(alarm_platform_dev);
		misc_deregister(&alarm_device);
		alarm_rtc_dev = NULL;
	}
}

static struct class_interface rtc_alarm_interface = {
	.add_dev = &rtc_alarm_add_device,
	.remove_dev = &rtc_alarm_remove_device,
};

static struct platform_driver alarm_driver = {
	.suspend = alarm_suspend,
	.resume = alarm_resume,
	.driver = {
		.name = "alarm"
	}
};

static int __init alarm_late_init(void)
{
	unsigned long   flags;
	struct timespec system_time;

	/* this needs to run after the rtc is read at boot */
	spin_lock_irqsave(&alarm_slock, flags);
	/* We read the current rtc and system time so we can later calulate
	 * elasped realtime to be (boot_systemtime + rtc - boot_rtc) ==
	 * (rtc - (boot_rtc - boot_systemtime))
	 */
	getnstimeofday(&elapsed_rtc_delta);
	ktime_get_ts(&system_time);
	elapsed_rtc_delta = timespec_sub(elapsed_rtc_delta, system_time);
	spin_unlock_irqrestore(&alarm_slock, flags);

	ANDROID_ALARM_DPRINTF(ANDROID_ALARM_PRINT_INFO,
		"alarm_late_init: rtc to elapsed realtime delta %ld.%09ld\n",
		elapsed_rtc_delta.tv_sec, elapsed_rtc_delta.tv_nsec);
	return 0;
}

static int __init alarm_init(void)
{
	int err;
	int i;

	for (i = 0; i < ANDROID_ALARM_SYSTEMTIME; i++) {
		hrtimer_init(&alarm_timer[i], CLOCK_REALTIME, HRTIMER_MODE_ABS);
		alarm_timer[i].function = alarm_timer_triggered;
	}
	hrtimer_init(&alarm_timer[ANDROID_ALARM_SYSTEMTIME],
		     CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
	alarm_timer[ANDROID_ALARM_SYSTEMTIME].function = alarm_timer_triggered;
	err = platform_driver_register(&alarm_driver);
	if (err < 0)
		goto err1;
	wake_lock_init(&alarm_wake_lock, WAKE_LOCK_SUSPEND, "alarm");
	wake_lock_init(&alarm_rtc_wake_lock, WAKE_LOCK_SUSPEND, "alarm_rtc");
	rtc_alarm_interface.class = rtc_class;
	err = class_interface_register(&rtc_alarm_interface);
	if (err < 0)
		goto err2;

	return 0;

err2:
	wake_lock_destroy(&alarm_rtc_wake_lock);
	wake_lock_destroy(&alarm_wake_lock);
	platform_driver_unregister(&alarm_driver);
err1:
	return err;
}

static void  __exit alarm_exit(void)
{
	class_interface_unregister(&rtc_alarm_interface);
	wake_lock_destroy(&alarm_rtc_wake_lock);
	wake_lock_destroy(&alarm_wake_lock);
	platform_driver_unregister(&alarm_driver);
}

late_initcall(alarm_late_init);
module_init(alarm_init);
module_exit(alarm_exit);

