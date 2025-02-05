// SPDX-License-Identifier: GPL-2.0

/*
 * LED pattern trigger
 *
 * Idea discussed with Pavel Machek. Raphael Teysseyre implemented
 * the first version, Baolin Wang simplified and improved the approach.
 */

#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/hrtimer.h>

#define MAX_PATTERNS		1024
/*
 * When doing gradual dimming, the led brightness will be updated
 * every 50 milliseconds.
 */
#define UPDATE_INTERVAL		50

enum pattern_type {
	PATTERN_TYPE_SW, /* Use standard timer for software pattern */
	PATTERN_TYPE_HR, /* Use hrtimer for software pattern */
	PATTERN_TYPE_HW, /* Hardware pattern */
};

struct pattern_trig_data {
	struct led_classdev *led_cdev;
	struct led_pattern patterns[MAX_PATTERNS];
	struct led_pattern *curr;
	struct led_pattern *next;
	struct mutex lock;
	u32 npatterns;
	int repeat;
	int last_repeat;
	int delta_t;
	bool is_indefinite;
	enum pattern_type type;
	struct timer_list timer;
	struct hrtimer hrtimer;
};

static void pattern_trig_update_patterns(struct pattern_trig_data *data)
{
	data->curr = data->next;
	if (!data->is_indefinite && data->curr == data->patterns)
		data->repeat--;

	if (data->next == data->patterns + data->npatterns - 1)
		data->next = data->patterns;
	else
		data->next++;

	data->delta_t = 0;
}

static int pattern_trig_compute_brightness(struct pattern_trig_data *data)
{
	int step_brightness;

	/*
	 * If current tuple's duration is less than the dimming interval,
	 * we should treat it as a step change of brightness instead of
	 * doing gradual dimming.
	 */
	if (data->delta_t == 0 || data->curr->delta_t < UPDATE_INTERVAL)
		return data->curr->brightness;

	step_brightness = abs(data->next->brightness - data->curr->brightness);
	step_brightness = data->delta_t * step_brightness / data->curr->delta_t;

	if (data->next->brightness > data->curr->brightness)
		return data->curr->brightness + step_brightness;
	else
		return data->curr->brightness - step_brightness;
}

static void pattern_trig_timer_start(struct pattern_trig_data *data)
{
	if (data->type == PATTERN_TYPE_HR) {
		hrtimer_start(&data->hrtimer, ns_to_ktime(0), HRTIMER_MODE_REL);
	} else {
		data->timer.expires = jiffies;
		add_timer(&data->timer);
	}
}

static void pattern_trig_timer_cancel(struct pattern_trig_data *data)
{
	if (data->type == PATTERN_TYPE_HR)
		hrtimer_cancel(&data->hrtimer);
	else
		del_timer_sync(&data->timer);
}

static void pattern_trig_timer_restart(struct pattern_trig_data *data,
				       unsigned long interval)
{
	if (data->type == PATTERN_TYPE_HR)
		hrtimer_forward_now(&data->hrtimer, ms_to_ktime(interval));
	else
		mod_timer(&data->timer, jiffies + msecs_to_jiffies(interval));
}

static void pattern_trig_timer_common_function(struct pattern_trig_data *data)
{
	for (;;) {
		if (!data->is_indefinite && !data->repeat)
			break;

		if (data->curr->brightness == data->next->brightness) {
			/* Step change of brightness */
			led_set_brightness(data->led_cdev,
					   data->curr->brightness);
			pattern_trig_timer_restart(data, data->curr->delta_t);
			if (!data->next->delta_t) {
				/* Skip the tuple with zero duration */
				pattern_trig_update_patterns(data);
			}
			/* Select next tuple */
			pattern_trig_update_patterns(data);
		} else {
			/* Gradual dimming */

			/*
			 * If the accumulation time is larger than current
			 * tuple's duration, we should go next one and re-check
			 * if we repeated done.
			 */
			if (data->delta_t > data->curr->delta_t) {
				pattern_trig_update_patterns(data);
				continue;
			}

			led_set_brightness(data->led_cdev,
					   pattern_trig_compute_brightness(data));
			pattern_trig_timer_restart(data, UPDATE_INTERVAL);

			/* Accumulate the gradual dimming time */
			data->delta_t += UPDATE_INTERVAL;
		}

		break;
	}
}

static void pattern_trig_timer_function(struct timer_list *t)
{
	struct pattern_trig_data *data = from_timer(data, t, timer);

	return pattern_trig_timer_common_function(data);
}

static enum hrtimer_restart pattern_trig_hrtimer_function(struct hrtimer *t)
{
	struct pattern_trig_data *data =
		container_of(t, struct pattern_trig_data, hrtimer);

	pattern_trig_timer_common_function(data);
	if (!data->is_indefinite && !data->repeat)
		return HRTIMER_NORESTART;

	return HRTIMER_RESTART;
}

static int pattern_trig_start_pattern(struct led_classdev *led_cdev)
{
	struct pattern_trig_data *data = led_cdev->trigger_data;

	if (!data->npatterns)
		return 0;

	if (data->type == PATTERN_TYPE_HW) {
		return led_cdev->pattern_set(led_cdev, data->patterns,
					     data->npatterns, data->repeat);
	}

	/* At least 2 tuples for software pattern. */
	if (data->npatterns < 2)
		return -EINVAL;

	data->delta_t = 0;
	data->curr = data->patterns;
	data->next = data->patterns + 1;
	pattern_trig_timer_start(data);

	return 0;
}

static ssize_t repeat_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct pattern_trig_data *data = led_cdev->trigger_data;
	int repeat;

	mutex_lock(&data->lock);

	repeat = data->last_repeat;

	mutex_unlock(&data->lock);

	return sysfs_emit(buf, "%d\n", repeat);
}

static ssize_t repeat_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct pattern_trig_data *data = led_cdev->trigger_data;
	int err, res;

	err = kstrtos32(buf, 10, &res);
	if (err)
		return err;

	/* Number 0 and negative numbers except -1 are invalid. */
	if (res < -1 || res == 0)
		return -EINVAL;

	mutex_lock(&data->lock);

	pattern_trig_timer_cancel(data);

	if (data->type == PATTERN_TYPE_HW)
		led_cdev->pattern_clear(led_cdev);

	data->last_repeat = data->repeat = res;
	/* -1 means repeat indefinitely */
	if (data->repeat == -1)
		data->is_indefinite = true;
	else
		data->is_indefinite = false;

	err = pattern_trig_start_pattern(led_cdev);

	mutex_unlock(&data->lock);
	return err < 0 ? err : count;
}

static DEVICE_ATTR_RW(repeat);

static ssize_t pattern_trig_show_patterns(struct pattern_trig_data *data,
					  char *buf, enum pattern_type type)
{
	ssize_t count = 0;
	int i;

	mutex_lock(&data->lock);

	if (!data->npatterns || data->type != type)
		goto out;

	for (i = 0; i < data->npatterns; i++) {
		count += scnprintf(buf + count, PAGE_SIZE - count,
				   "%d %u ",
				   data->patterns[i].brightness,
				   data->patterns[i].delta_t);
	}

	buf[count - 1] = '\n';

out:
	mutex_unlock(&data->lock);
	return count;
}

static int pattern_trig_store_patterns_string(struct pattern_trig_data *data,
					      const char *buf, size_t count)
{
	int ccount, cr, offset = 0;

	while (offset < count - 1 && data->npatterns < MAX_PATTERNS) {
		cr = 0;
		ccount = sscanf(buf + offset, "%u %u %n",
				&data->patterns[data->npatterns].brightness,
				&data->patterns[data->npatterns].delta_t, &cr);

		if (ccount != 2 ||
		    data->patterns[data->npatterns].brightness > data->led_cdev->max_brightness) {
			data->npatterns = 0;
			return -EINVAL;
		}

		offset += cr;
		data->npatterns++;
	}

	return 0;
}

static int pattern_trig_store_patterns_int(struct pattern_trig_data *data,
					   const u32 *buf, size_t count)
{
	unsigned int i;

	for (i = 0; i < count; i += 2) {
		data->patterns[data->npatterns].brightness = buf[i];
		data->patterns[data->npatterns].delta_t = buf[i + 1];
		data->npatterns++;
	}

	return 0;
}

static ssize_t pattern_trig_store_patterns(struct led_classdev *led_cdev,
					   const char *buf, const u32 *buf_int,
					   size_t count, enum pattern_type type)
{
	struct pattern_trig_data *data = led_cdev->trigger_data;
	int err = 0;

	mutex_lock(&data->lock);

	pattern_trig_timer_cancel(data);

	if (data->type == PATTERN_TYPE_HW)
		led_cdev->pattern_clear(led_cdev);

	data->type = type;
	data->npatterns = 0;

	if (buf)
		err = pattern_trig_store_patterns_string(data, buf, count);
	else
		err = pattern_trig_store_patterns_int(data, buf_int, count);
	if (err)
		goto out;

	err = pattern_trig_start_pattern(led_cdev);
	if (err)
		data->npatterns = 0;

out:
	mutex_unlock(&data->lock);
	return err < 0 ? err : count;
}

static ssize_t pattern_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct pattern_trig_data *data = led_cdev->trigger_data;

	return pattern_trig_show_patterns(data, buf, PATTERN_TYPE_SW);
}

static ssize_t pattern_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);

	return pattern_trig_store_patterns(led_cdev, buf, NULL, count,
					   PATTERN_TYPE_SW);
}

static DEVICE_ATTR_RW(pattern);

static ssize_t hw_pattern_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct pattern_trig_data *data = led_cdev->trigger_data;

	return pattern_trig_show_patterns(data, buf, PATTERN_TYPE_HW);
}

static ssize_t hw_pattern_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);

	return pattern_trig_store_patterns(led_cdev, buf, NULL, count,
					   PATTERN_TYPE_HW);
}

static DEVICE_ATTR_RW(hw_pattern);

static ssize_t hr_pattern_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct pattern_trig_data *data = led_cdev->trigger_data;

	return pattern_trig_show_patterns(data, buf, PATTERN_TYPE_HR);
}

static ssize_t hr_pattern_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);

	return pattern_trig_store_patterns(led_cdev, buf, NULL, count,
					   PATTERN_TYPE_HR);
}

static DEVICE_ATTR_RW(hr_pattern);

static umode_t pattern_trig_attrs_mode(struct kobject *kobj,
				       struct attribute *attr, int index)
{
	struct device *dev = kobj_to_dev(kobj);
	struct led_classdev *led_cdev = dev_get_drvdata(dev);

	if (attr == &dev_attr_repeat.attr || attr == &dev_attr_pattern.attr)
		return attr->mode;
	else if (attr == &dev_attr_hr_pattern.attr)
		return attr->mode;
	else if (attr == &dev_attr_hw_pattern.attr && led_cdev->pattern_set)
		return attr->mode;

	return 0;
}

static struct attribute *pattern_trig_attrs[] = {
	&dev_attr_pattern.attr,
	&dev_attr_hw_pattern.attr,
	&dev_attr_hr_pattern.attr,
	&dev_attr_repeat.attr,
	NULL
};

static const struct attribute_group pattern_trig_group = {
	.attrs = pattern_trig_attrs,
	.is_visible = pattern_trig_attrs_mode,
};

static const struct attribute_group *pattern_trig_groups[] = {
	&pattern_trig_group,
	NULL,
};

static void pattern_init(struct led_classdev *led_cdev)
{
	unsigned int size = 0;
	u32 *pattern;
	int err;

	pattern = led_get_default_pattern(led_cdev, &size);
	if (!pattern)
		return;

	if (size % 2) {
		dev_warn(led_cdev->dev, "Expected pattern of tuples\n");
		goto out;
	}

	err = pattern_trig_store_patterns(led_cdev, NULL, pattern, size,
					  PATTERN_TYPE_SW);
	if (err < 0)
		dev_warn(led_cdev->dev,
			 "Pattern initialization failed with error %d\n", err);

out:
	kfree(pattern);
}

static int pattern_trig_activate(struct led_classdev *led_cdev)
{
	struct pattern_trig_data *data;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	if (!!led_cdev->pattern_set ^ !!led_cdev->pattern_clear) {
		dev_warn(led_cdev->dev,
			 "Hardware pattern ops validation failed\n");
		led_cdev->pattern_set = NULL;
		led_cdev->pattern_clear = NULL;
	}

	data->type = PATTERN_TYPE_SW;
	data->is_indefinite = true;
	data->last_repeat = -1;
	mutex_init(&data->lock);
	data->led_cdev = led_cdev;
	led_set_trigger_data(led_cdev, data);
	timer_setup(&data->timer, pattern_trig_timer_function, 0);
	hrtimer_init(&data->hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	data->hrtimer.function = pattern_trig_hrtimer_function;
	led_cdev->activated = true;

	if (led_cdev->flags & LED_INIT_DEFAULT_TRIGGER) {
		pattern_init(led_cdev);
		/*
		 * Mark as initialized even on pattern_init() error because
		 * any consecutive call to it would produce the same error.
		 */
		led_cdev->flags &= ~LED_INIT_DEFAULT_TRIGGER;
	}

	return 0;
}

static void pattern_trig_deactivate(struct led_classdev *led_cdev)
{
	struct pattern_trig_data *data = led_cdev->trigger_data;

	if (!led_cdev->activated)
		return;

	if (led_cdev->pattern_clear)
		led_cdev->pattern_clear(led_cdev);

	timer_shutdown_sync(&data->timer);
	hrtimer_cancel(&data->hrtimer);

	led_set_brightness(led_cdev, LED_OFF);
	kfree(data);
	led_cdev->activated = false;
}

static struct led_trigger pattern_led_trigger = {
	.name = "pattern",
	.activate = pattern_trig_activate,
	.deactivate = pattern_trig_deactivate,
	.groups = pattern_trig_groups,
};

static int __init pattern_trig_init(void)
{
	return led_trigger_register(&pattern_led_trigger);
}

static void __exit pattern_trig_exit(void)
{
	led_trigger_unregister(&pattern_led_trigger);
}

module_init(pattern_trig_init);
module_exit(pattern_trig_exit);

MODULE_AUTHOR("Raphael Teysseyre <rteysseyre@gmail.com>");
MODULE_AUTHOR("Baolin Wang <baolin.wang@linaro.org>");
MODULE_DESCRIPTION("LED Pattern trigger");
MODULE_LICENSE("GPL v2");
