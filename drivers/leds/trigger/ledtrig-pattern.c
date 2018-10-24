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

#define MAX_PATTERNS		1024
/*
 * When doing gradual dimming, the led brightness will be updated
 * every 50 milliseconds.
 */
#define UPDATE_INTERVAL		50

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
	bool is_hw_pattern;
	struct timer_list timer;
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

static void pattern_trig_timer_function(struct timer_list *t)
{
	struct pattern_trig_data *data = from_timer(data, t, timer);

	mutex_lock(&data->lock);

	for (;;) {
		if (!data->is_indefinite && !data->repeat)
			break;

		if (data->curr->brightness == data->next->brightness) {
			/* Step change of brightness */
			led_set_brightness(data->led_cdev,
					   data->curr->brightness);
			mod_timer(&data->timer,
				  jiffies + msecs_to_jiffies(data->curr->delta_t));
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
			mod_timer(&data->timer,
				  jiffies + msecs_to_jiffies(UPDATE_INTERVAL));

			/* Accumulate the gradual dimming time */
			data->delta_t += UPDATE_INTERVAL;
		}

		break;
	}

	mutex_unlock(&data->lock);
}

static int pattern_trig_start_pattern(struct led_classdev *led_cdev)
{
	struct pattern_trig_data *data = led_cdev->trigger_data;

	if (!data->npatterns)
		return 0;

	if (data->is_hw_pattern) {
		return led_cdev->pattern_set(led_cdev, data->patterns,
					     data->npatterns, data->repeat);
	}

	/* At least 2 tuples for software pattern. */
	if (data->npatterns < 2)
		return -EINVAL;

	data->delta_t = 0;
	data->curr = data->patterns;
	data->next = data->patterns + 1;
	data->timer.expires = jiffies;
	add_timer(&data->timer);

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

	return scnprintf(buf, PAGE_SIZE, "%d\n", repeat);
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

	/*
	 * Clear previous patterns' performence firstly, and remove the timer
	 * without mutex lock to avoid dead lock.
	 */
	del_timer_sync(&data->timer);

	mutex_lock(&data->lock);

	if (data->is_hw_pattern)
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
					  char *buf, bool hw_pattern)
{
	ssize_t count = 0;
	int i;

	mutex_lock(&data->lock);

	if (!data->npatterns || (data->is_hw_pattern ^ hw_pattern))
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

static ssize_t pattern_trig_store_patterns(struct led_classdev *led_cdev,
					   const char *buf, size_t count,
					   bool hw_pattern)
{
	struct pattern_trig_data *data = led_cdev->trigger_data;
	int ccount, cr, offset = 0, err = 0;

	/*
	 * Clear previous patterns' performence firstly, and remove the timer
	 * without mutex lock to avoid dead lock.
	 */
	del_timer_sync(&data->timer);

	mutex_lock(&data->lock);

	if (data->is_hw_pattern)
		led_cdev->pattern_clear(led_cdev);

	data->is_hw_pattern = hw_pattern;
	data->npatterns = 0;

	while (offset < count - 1 && data->npatterns < MAX_PATTERNS) {
		cr = 0;
		ccount = sscanf(buf + offset, "%d %u %n",
				&data->patterns[data->npatterns].brightness,
				&data->patterns[data->npatterns].delta_t, &cr);
		if (ccount != 2) {
			data->npatterns = 0;
			err = -EINVAL;
			goto out;
		}

		offset += cr;
		data->npatterns++;
	}

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

	return pattern_trig_show_patterns(data, buf, false);
}

static ssize_t pattern_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);

	return pattern_trig_store_patterns(led_cdev, buf, count, false);
}

static DEVICE_ATTR_RW(pattern);

static ssize_t hw_pattern_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct pattern_trig_data *data = led_cdev->trigger_data;

	return pattern_trig_show_patterns(data, buf, true);
}

static ssize_t hw_pattern_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);

	return pattern_trig_store_patterns(led_cdev, buf, count, true);
}

static DEVICE_ATTR_RW(hw_pattern);

static umode_t pattern_trig_attrs_mode(struct kobject *kobj,
				       struct attribute *attr, int index)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct led_classdev *led_cdev = dev_get_drvdata(dev);

	if (attr == &dev_attr_repeat.attr || attr == &dev_attr_pattern.attr)
		return attr->mode;
	else if (attr == &dev_attr_hw_pattern.attr && led_cdev->pattern_set)
		return attr->mode;

	return 0;
}

static struct attribute *pattern_trig_attrs[] = {
	&dev_attr_pattern.attr,
	&dev_attr_hw_pattern.attr,
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

	data->is_indefinite = true;
	data->last_repeat = -1;
	mutex_init(&data->lock);
	data->led_cdev = led_cdev;
	led_set_trigger_data(led_cdev, data);
	timer_setup(&data->timer, pattern_trig_timer_function, 0);
	led_cdev->activated = true;

	return 0;
}

static void pattern_trig_deactivate(struct led_classdev *led_cdev)
{
	struct pattern_trig_data *data = led_cdev->trigger_data;

	if (!led_cdev->activated)
		return;

	if (led_cdev->pattern_clear)
		led_cdev->pattern_clear(led_cdev);

	del_timer_sync(&data->timer);

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

MODULE_AUTHOR("Raphael Teysseyre <rteysseyre@gmail.com");
MODULE_AUTHOR("Baolin Wang <baolin.wang@linaro.org");
MODULE_DESCRIPTION("LED Pattern trigger");
MODULE_LICENSE("GPL v2");
