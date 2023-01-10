// SPDX-License-Identifier: GPL-2.0-only
/*
 * Activity LED trigger
 *
 * Copyright (C) 2017 Willy Tarreau <w@1wt.eu>
 * Partially based on Atsushi Nemoto's ledtrig-heartbeat.c.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/panic_notifier.h>
#include <linux/reboot.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include "../leds.h"

static int panic_detected;

struct activity_data {
	struct timer_list timer;
	struct led_classdev *led_cdev;
	u64 last_used;
	u64 last_boot;
	int time_left;
	int state;
	int invert;
};

static void led_activity_function(struct timer_list *t)
{
	struct activity_data *activity_data = from_timer(activity_data, t,
							 timer);
	struct led_classdev *led_cdev = activity_data->led_cdev;
	unsigned int target;
	unsigned int usage;
	int delay;
	u64 curr_used;
	u64 curr_boot;
	s32 diff_used;
	s32 diff_boot;
	int cpus;
	int i;

	if (test_and_clear_bit(LED_BLINK_BRIGHTNESS_CHANGE, &led_cdev->work_flags))
		led_cdev->blink_brightness = led_cdev->new_blink_brightness;

	if (unlikely(panic_detected)) {
		/* full brightness in case of panic */
		led_set_brightness_nosleep(led_cdev, led_cdev->blink_brightness);
		return;
	}

	cpus = 0;
	curr_used = 0;

	for_each_possible_cpu(i) {
		struct kernel_cpustat kcpustat;

		kcpustat_cpu_fetch(&kcpustat, i);

		curr_used += kcpustat.cpustat[CPUTIME_USER]
			  +  kcpustat.cpustat[CPUTIME_NICE]
			  +  kcpustat.cpustat[CPUTIME_SYSTEM]
			  +  kcpustat.cpustat[CPUTIME_SOFTIRQ]
			  +  kcpustat.cpustat[CPUTIME_IRQ];
		cpus++;
	}

	/* We come here every 100ms in the worst case, so that's 100M ns of
	 * cumulated time. By dividing by 2^16, we get the time resolution
	 * down to 16us, ensuring we won't overflow 32-bit computations below
	 * even up to 3k CPUs, while keeping divides cheap on smaller systems.
	 */
	curr_boot = ktime_get_boottime_ns() * cpus;
	diff_boot = (curr_boot - activity_data->last_boot) >> 16;
	diff_used = (curr_used - activity_data->last_used) >> 16;
	activity_data->last_boot = curr_boot;
	activity_data->last_used = curr_used;

	if (diff_boot <= 0 || diff_used < 0)
		usage = 0;
	else if (diff_used >= diff_boot)
		usage = 100;
	else
		usage = 100 * diff_used / diff_boot;

	/*
	 * Now we know the total boot_time multiplied by the number of CPUs, and
	 * the total idle+wait time for all CPUs. We'll compare how they evolved
	 * since last call. The % of overall CPU usage is :
	 *
	 *      1 - delta_idle / delta_boot
	 *
	 * What we want is that when the CPU usage is zero, the LED must blink
	 * slowly with very faint flashes that are detectable but not disturbing
	 * (typically 10ms every second, or 10ms ON, 990ms OFF). Then we want
	 * blinking frequency to increase up to the point where the load is
	 * enough to saturate one core in multi-core systems or 50% in single
	 * core systems. At this point it should reach 10 Hz with a 10/90 duty
	 * cycle (10ms ON, 90ms OFF). After this point, the blinking frequency
	 * remains stable (10 Hz) and only the duty cycle increases to report
	 * the activity, up to the point where we have 90ms ON, 10ms OFF when
	 * all cores are saturated. It's important that the LED never stays in
	 * a steady state so that it's easy to distinguish an idle or saturated
	 * machine from a hung one.
	 *
	 * This gives us :
	 *   - a target CPU usage of min(50%, 100%/#CPU) for a 10% duty cycle
	 *     (10ms ON, 90ms OFF)
	 *   - below target :
	 *      ON_ms  = 10
	 *      OFF_ms = 90 + (1 - usage/target) * 900
	 *   - above target :
	 *      ON_ms  = 10 + (usage-target)/(100%-target) * 80
	 *      OFF_ms = 90 - (usage-target)/(100%-target) * 80
	 *
	 * In order to keep a good responsiveness, we cap the sleep time to
	 * 100 ms and keep track of the sleep time left. This allows us to
	 * quickly change it if needed.
	 */

	activity_data->time_left -= 100;
	if (activity_data->time_left <= 0) {
		activity_data->time_left = 0;
		activity_data->state = !activity_data->state;
		led_set_brightness_nosleep(led_cdev,
			(activity_data->state ^ activity_data->invert) ?
			led_cdev->blink_brightness : LED_OFF);
	}

	target = (cpus > 1) ? (100 / cpus) : 50;

	if (usage < target)
		delay = activity_data->state ?
			10 :                        /* ON  */
			990 - 900 * usage / target; /* OFF */
	else
		delay = activity_data->state ?
			10 + 80 * (usage - target) / (100 - target) : /* ON  */
			90 - 80 * (usage - target) / (100 - target);  /* OFF */


	if (!activity_data->time_left || delay <= activity_data->time_left)
		activity_data->time_left = delay;

	delay = min_t(int, activity_data->time_left, 100);
	mod_timer(&activity_data->timer, jiffies + msecs_to_jiffies(delay));
}

static ssize_t led_invert_show(struct device *dev,
                               struct device_attribute *attr, char *buf)
{
	struct activity_data *activity_data = led_trigger_get_drvdata(dev);

	return sprintf(buf, "%u\n", activity_data->invert);
}

static ssize_t led_invert_store(struct device *dev,
                                struct device_attribute *attr,
                                const char *buf, size_t size)
{
	struct activity_data *activity_data = led_trigger_get_drvdata(dev);
	unsigned long state;
	int ret;

	ret = kstrtoul(buf, 0, &state);
	if (ret)
		return ret;

	activity_data->invert = !!state;

	return size;
}

static DEVICE_ATTR(invert, 0644, led_invert_show, led_invert_store);

static struct attribute *activity_led_attrs[] = {
	&dev_attr_invert.attr,
	NULL
};
ATTRIBUTE_GROUPS(activity_led);

static int activity_activate(struct led_classdev *led_cdev)
{
	struct activity_data *activity_data;

	activity_data = kzalloc(sizeof(*activity_data), GFP_KERNEL);
	if (!activity_data)
		return -ENOMEM;

	led_set_trigger_data(led_cdev, activity_data);

	activity_data->led_cdev = led_cdev;
	timer_setup(&activity_data->timer, led_activity_function, 0);
	if (!led_cdev->blink_brightness)
		led_cdev->blink_brightness = led_cdev->max_brightness;
	led_activity_function(&activity_data->timer);
	set_bit(LED_BLINK_SW, &led_cdev->work_flags);

	return 0;
}

static void activity_deactivate(struct led_classdev *led_cdev)
{
	struct activity_data *activity_data = led_get_trigger_data(led_cdev);

	timer_shutdown_sync(&activity_data->timer);
	kfree(activity_data);
	clear_bit(LED_BLINK_SW, &led_cdev->work_flags);
}

static struct led_trigger activity_led_trigger = {
	.name       = "activity",
	.activate   = activity_activate,
	.deactivate = activity_deactivate,
	.groups     = activity_led_groups,
};

static int activity_reboot_notifier(struct notifier_block *nb,
                                    unsigned long code, void *unused)
{
	led_trigger_unregister(&activity_led_trigger);
	return NOTIFY_DONE;
}

static int activity_panic_notifier(struct notifier_block *nb,
                                   unsigned long code, void *unused)
{
	panic_detected = 1;
	return NOTIFY_DONE;
}

static struct notifier_block activity_reboot_nb = {
	.notifier_call = activity_reboot_notifier,
};

static struct notifier_block activity_panic_nb = {
	.notifier_call = activity_panic_notifier,
};

static int __init activity_init(void)
{
	int rc = led_trigger_register(&activity_led_trigger);

	if (!rc) {
		atomic_notifier_chain_register(&panic_notifier_list,
					       &activity_panic_nb);
		register_reboot_notifier(&activity_reboot_nb);
	}
	return rc;
}

static void __exit activity_exit(void)
{
	unregister_reboot_notifier(&activity_reboot_nb);
	atomic_notifier_chain_unregister(&panic_notifier_list,
					 &activity_panic_nb);
	led_trigger_unregister(&activity_led_trigger);
}

module_init(activity_init);
module_exit(activity_exit);

MODULE_AUTHOR("Willy Tarreau <w@1wt.eu>");
MODULE_DESCRIPTION("Activity LED trigger");
MODULE_LICENSE("GPL v2");
