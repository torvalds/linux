// SPDX-License-Identifier: GPL-2.0-only
/*
 * LED Triggers Core
 *
 * Copyright 2005-2007 Openedhand Ltd.
 *
 * Author: Richard Purdie <rpurdie@openedhand.com>
 */

#include <linux/bug.h>
#include <linux/cleanup.h>
#include <linux/compiler.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/lockdep.h>
#include <linux/spinlock.h>
#include <linux/device.h>
#include <linux/timer.h>
#include <linux/rwsem.h>
#include <linux/leds.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include "leds.h"

/*
 * Nests outside led_cdev->trigger_lock
 */
static DECLARE_RWSEM(triggers_list_lock);
static LIST_HEAD(trigger_list);

 /* Used by LED Class */

static inline bool
trigger_relevant(struct led_classdev *led_cdev, struct led_trigger *trig)
{
	return !trig->trigger_type || trig->trigger_type == led_cdev->trigger_type;
}

static bool __led_trigger_is_hw_controlled(struct led_classdev *led_cdev)
{
	lockdep_assert_held(&led_cdev->trigger_lock);

	if (!led_cdev->trigger)
		return false;

	if (!led_cdev->hw_control_trigger ||
	    strcmp(led_cdev->hw_control_trigger, led_cdev->trigger->name))
		return false;

	if (led_cdev->trigger->hw_offloaded)
		return led_cdev->trigger->hw_offloaded(led_cdev);

	dev_warn_once(led_cdev->dev,
		      "Hardware control trigger %s doesn't provide offloaded state\n",
		      led_cdev->trigger->name);

	/* Otherwise assume private triggers are always offloaded. */
	return led_cdev->trigger->trigger_type;
}

bool led_trigger_is_hw_controlled(struct led_classdev *led_cdev)
{
	guard(rwsem_read)(&led_cdev->trigger_lock);
	return __led_trigger_is_hw_controlled(led_cdev);
}
EXPORT_SYMBOL_GPL(led_trigger_is_hw_controlled);

static ssize_t trigger_write(struct file *filp, struct kobject *kobj,
			     const struct bin_attribute *bin_attr, char *buf,
			     loff_t pos, size_t count)
{
	struct device *dev = kobj_to_dev(kobj);
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct led_trigger *trig;
	int ret = count;

	mutex_lock(&led_cdev->led_access);

	if (led_sysfs_is_disabled(led_cdev)) {
		ret = -EBUSY;
		goto unlock;
	}

	if (sysfs_streq(buf, "none")) {
		led_trigger_remove(led_cdev);
		goto unlock;
	}

	if (sysfs_streq(buf, "default")) {
		led_trigger_set_default(led_cdev);
		goto unlock;
	}

	down_read(&triggers_list_lock);
	list_for_each_entry(trig, &trigger_list, next_trig) {
		if (sysfs_streq(buf, trig->name) && trigger_relevant(led_cdev, trig)) {
			down_write(&led_cdev->trigger_lock);
			led_trigger_set(led_cdev, trig);
			up_write(&led_cdev->trigger_lock);

			up_read(&triggers_list_lock);
			goto unlock;
		}
	}
	/* we come here only if buf matches no trigger */
	ret = -EINVAL;
	up_read(&triggers_list_lock);

unlock:
	mutex_unlock(&led_cdev->led_access);
	return ret;
}

__printf(3, 4)
static int led_trigger_snprintf(char *buf, ssize_t size, const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	if (size <= 0)
		i = vsnprintf(NULL, 0, fmt, args);
	else
		i = vscnprintf(buf, size, fmt, args);
	va_end(args);

	return i;
}

static int led_trigger_format(char *buf, size_t size,
			      struct led_classdev *led_cdev)
{
	struct led_trigger *trig;
	int len = led_trigger_snprintf(buf, size, "%s",
				       led_cdev->trigger ? "none" : "[none]");

	if (led_cdev->default_trigger)
		len += led_trigger_snprintf(buf + len, size - len, " default");

	list_for_each_entry(trig, &trigger_list, next_trig) {
		bool hit;

		if (!trigger_relevant(led_cdev, trig))
			continue;

		hit = led_cdev->trigger && !strcmp(led_cdev->trigger->name, trig->name);

		len += led_trigger_snprintf(buf + len, size - len,
					    " %s%s%s", hit ? "[" : "",
					    trig->name, hit ? "]" : "");
	}

	len += led_trigger_snprintf(buf + len, size - len, "\n");

	return len;
}

/*
 * It was stupid to create 10000 cpu triggers, but we are stuck with it now.
 * Don't make that mistake again. We work around it here by creating binary
 * attribute, which is not limited by length. This is _not_ good design, do not
 * copy it.
 */
static ssize_t trigger_read(struct file *filp, struct kobject *kobj,
			    const struct bin_attribute *attr, char *buf,
			    loff_t pos, size_t count)
{
	struct device *dev = kobj_to_dev(kobj);
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	void *data;
	int len;

	down_read(&triggers_list_lock);
	down_read(&led_cdev->trigger_lock);

	len = led_trigger_format(NULL, 0, led_cdev);
	data = kvmalloc(len + 1, GFP_KERNEL);
	if (!data) {
		up_read(&led_cdev->trigger_lock);
		up_read(&triggers_list_lock);
		return -ENOMEM;
	}
	len = led_trigger_format(data, len + 1, led_cdev);

	up_read(&led_cdev->trigger_lock);
	up_read(&triggers_list_lock);

	len = memory_read_from_buffer(buf, count, &pos, data, len);

	kvfree(data);

	return len;
}
static const BIN_ATTR_RW(trigger, 0);

static const struct bin_attribute *const led_trigger_bin_attrs[] = {
	&bin_attr_trigger,
	NULL
};

static ssize_t trigger_may_offload_to_hw_show(struct device *dev,
					      const struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	bool offloaded = led_trigger_is_hw_controlled(led_cdev);

	return sysfs_emit(buf, "%s%s%s\n",
			  offloaded ? "[" : "",
			  led_cdev->hw_control_trigger,
			  offloaded ? "]" : "");
}
static const DEVICE_ATTR_RO(trigger_may_offload_to_hw);

static const struct attribute *const led_trigger_attrs[] = {
	&dev_attr_trigger_may_offload_to_hw.attr,
	NULL
};

static umode_t led_trigger_is_visible(struct kobject *kobj,
				      const struct attribute *attr, int idx)
{
	struct device *dev = kobj_to_dev(kobj);
	struct led_classdev *led_cdev = dev_get_drvdata(dev);

	if (attr == &dev_attr_trigger_may_offload_to_hw.attr)
		return led_cdev->hw_control_trigger ? attr->mode : 0;

	return attr->mode;
}

const struct attribute_group led_trigger_group = {
	.bin_attrs = led_trigger_bin_attrs,
	.attrs_const = led_trigger_attrs,
	.is_visible_const = led_trigger_is_visible,
};
EXPORT_SYMBOL_GPL(led_trigger_group);

/* Caller must ensure led_cdev->trigger_lock held */
static int __led_trigger_set(struct led_classdev *led_cdev, struct led_trigger *trig,
			     bool hw_triggered)
{
	char *event = NULL;
	char *envp[2];
	const char *name;
	int ret;

	if (!led_cdev->trigger && !trig)
		return 0;

	name = trig ? trig->name : "none";
	event = kasprintf(GFP_KERNEL, "TRIGGER=%s", name);

	/* Remove any existing trigger */
	if (led_cdev->trigger) {
		spin_lock(&led_cdev->trigger->leddev_list_lock);
		list_del_rcu(&led_cdev->trig_list);
		spin_unlock(&led_cdev->trigger->leddev_list_lock);

		/* ensure it's no longer visible on the led_cdevs list */
		synchronize_rcu();

		cancel_work_sync(&led_cdev->set_brightness_work);
		led_stop_software_blink(led_cdev);
		device_remove_groups(led_cdev->dev, led_cdev->trigger->groups);
		if (led_cdev->trigger->deactivate)
			led_cdev->trigger->deactivate(led_cdev);
		led_cdev->trigger = NULL;
		led_cdev->trigger_data = NULL;
		led_cdev->activated = false;
		led_cdev->flags &= ~LED_INIT_DEFAULT_TRIGGER;

		/*
		 * Hardware may have selected a new brightness level during its
		 * hardware control transition, so only reset brightness if we
		 * are switching to another trigger or if the switching is not
		 * hardware triggered.
		 *
		 * Note that this does not apply to the error path, as running
		 * into the error path implies a none => private trigger
		 * transition. This hints that the LED driver and its private
		 * trigger must have some fundamental bugs, so the error path
		 * always turns off the LED to reset it to a certain state.
		 */
		if (trig || !hw_triggered)
			led_set_brightness(led_cdev, LED_OFF);
	}
	if (trig) {
		spin_lock(&trig->leddev_list_lock);
		list_add_tail_rcu(&led_cdev->trig_list, &trig->led_cdevs);
		spin_unlock(&trig->leddev_list_lock);
		led_cdev->trigger = trig;

		/*
		 * Some activate() calls use led_trigger_event() to initialize
		 * the brightness of the LED for which the trigger is being set.
		 * Ensure the led_cdev is visible on trig->led_cdevs for this.
		 */
		synchronize_rcu();

		/*
		 * If "set brightness to 0" is pending in workqueue,
		 * we don't want that to be reordered after ->activate()
		 */
		flush_work(&led_cdev->set_brightness_work);

		ret = 0;
		if (trig->activate)
			ret = trig->activate(led_cdev);
		else
			led_set_brightness(led_cdev, trig->brightness);
		if (ret)
			goto err_activate;

		ret = device_add_groups(led_cdev->dev, trig->groups);
		if (ret) {
			dev_err(led_cdev->dev, "Failed to add trigger attributes\n");
			goto err_add_groups;
		}
	}

	if (event) {
		envp[0] = event;
		envp[1] = NULL;
		if (kobject_uevent_env(&led_cdev->dev->kobj, KOBJ_CHANGE, envp))
			dev_err(led_cdev->dev,
				"%s: Error sending uevent\n", __func__);
		kfree(event);
	}

	return 0;

err_add_groups:

	if (trig->deactivate)
		trig->deactivate(led_cdev);
err_activate:

	spin_lock(&led_cdev->trigger->leddev_list_lock);
	list_del_rcu(&led_cdev->trig_list);
	spin_unlock(&led_cdev->trigger->leddev_list_lock);
	synchronize_rcu();
	led_cdev->trigger = NULL;
	led_cdev->trigger_data = NULL;
	led_set_brightness(led_cdev, LED_OFF);
	kfree(event);

	return ret;
}

int led_trigger_set(struct led_classdev *led_cdev, struct led_trigger *trig)
{
	return __led_trigger_set(led_cdev, trig, false);
}
EXPORT_SYMBOL_GPL(led_trigger_set);

void led_trigger_remove(struct led_classdev *led_cdev)
{
	down_write(&led_cdev->trigger_lock);
	led_trigger_set(led_cdev, NULL);
	up_write(&led_cdev->trigger_lock);
}
EXPORT_SYMBOL_GPL(led_trigger_remove);

void led_trigger_remove_hw_control(struct led_classdev *led_cdev)
{
	guard(rwsem_write)(&led_cdev->trigger_lock);

	if (__led_trigger_is_hw_controlled(led_cdev))
		led_trigger_set(led_cdev, NULL);
}
EXPORT_SYMBOL_GPL(led_trigger_remove_hw_control);

static bool led_match_default_trigger(struct led_classdev *led_cdev,
				      struct led_trigger *trig)
{
	if (!strcmp(led_cdev->default_trigger, trig->name) &&
	    trigger_relevant(led_cdev, trig)) {
		led_cdev->flags |= LED_INIT_DEFAULT_TRIGGER;
		led_trigger_set(led_cdev, trig);
		return true;
	}

	return false;
}

void led_trigger_set_default(struct led_classdev *led_cdev)
{
	struct led_trigger *trig;
	bool found = false;

	if (!led_cdev->default_trigger)
		return;

	if (!strcmp(led_cdev->default_trigger, "none")) {
		led_trigger_remove(led_cdev);
		return;
	}

	down_read(&triggers_list_lock);
	down_write(&led_cdev->trigger_lock);
	list_for_each_entry(trig, &trigger_list, next_trig) {
		found = led_match_default_trigger(led_cdev, trig);
		if (found)
			break;
	}
	up_write(&led_cdev->trigger_lock);
	up_read(&triggers_list_lock);

	/*
	 * If default trigger wasn't found, maybe trigger module isn't loaded yet.
	 * Once loaded it will re-probe with all led_cdev's.
	 */
	if (!found)
		request_module_nowait("ledtrig:%s", led_cdev->default_trigger);
}
EXPORT_SYMBOL_GPL(led_trigger_set_default);

/* LED Trigger Interface */

int led_trigger_register(struct led_trigger *trig)
{
	struct led_classdev *led_cdev;
	struct led_trigger *_trig;

	spin_lock_init(&trig->leddev_list_lock);
	INIT_LIST_HEAD(&trig->led_cdevs);

	down_write(&triggers_list_lock);
	/* Make sure the trigger's name isn't already in use */
	list_for_each_entry(_trig, &trigger_list, next_trig) {
		if (!strcmp(_trig->name, trig->name) &&
		    (trig->trigger_type == _trig->trigger_type ||
		     !trig->trigger_type || !_trig->trigger_type)) {
			up_write(&triggers_list_lock);
			return -EEXIST;
		}
	}
	/* Add to the list of led triggers */
	list_add_tail(&trig->next_trig, &trigger_list);
	up_write(&triggers_list_lock);

	/* Register with any LEDs that have this as a default trigger */
	down_read(&leds_list_lock);
	list_for_each_entry(led_cdev, &leds_list, node) {
		down_write(&led_cdev->trigger_lock);
		if (!led_cdev->trigger && led_cdev->default_trigger)
			led_match_default_trigger(led_cdev, trig);
		up_write(&led_cdev->trigger_lock);
	}
	up_read(&leds_list_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(led_trigger_register);

void led_trigger_unregister(struct led_trigger *trig)
{
	struct led_classdev *led_cdev;

	if (list_empty_careful(&trig->next_trig))
		return;

	/* Remove from the list of led triggers */
	down_write(&triggers_list_lock);
	list_del_init(&trig->next_trig);
	up_write(&triggers_list_lock);

	/* Remove anyone actively using this trigger */
	down_read(&leds_list_lock);
	list_for_each_entry(led_cdev, &leds_list, node) {
		down_write(&led_cdev->trigger_lock);
		if (led_cdev->trigger == trig)
			led_trigger_set(led_cdev, NULL);
		up_write(&led_cdev->trigger_lock);
	}
	up_read(&leds_list_lock);
}
EXPORT_SYMBOL_GPL(led_trigger_unregister);

static void devm_led_trigger_release(struct device *dev, void *res)
{
	led_trigger_unregister(*(struct led_trigger **)res);
}

int devm_led_trigger_register(struct device *dev,
			      struct led_trigger *trig)
{
	struct led_trigger **dr;
	int rc;

	dr = devres_alloc(devm_led_trigger_release, sizeof(*dr),
			  GFP_KERNEL);
	if (!dr)
		return -ENOMEM;

	*dr = trig;

	rc = led_trigger_register(trig);
	if (rc)
		devres_free(dr);
	else
		devres_add(dev, dr);

	return rc;
}
EXPORT_SYMBOL_GPL(devm_led_trigger_register);

#ifdef CONFIG_LEDS_TRIGGERS_HW_CHANGED

static void led_trigger_do_hw_control_transition(struct led_classdev *led_cdev, bool activate,
						 struct led_trigger *hc_trig)
{
	if (activate && !led_cdev->trigger) /* "none" => private trigger. */
		__led_trigger_set(led_cdev, hc_trig, true);
	else if (!activate && led_cdev->trigger == hc_trig) /* private trigger => "none". */
		__led_trigger_set(led_cdev, NULL, true);

	/* Already in the desired state, or another trigger is active, ignore. */
}

void led_trigger_hw_control_changed_worker(struct work_struct *work)
{
	struct led_classdev *led_cdev =
		container_of(work, struct led_classdev, trigger_hw_changed_work);
	bool activate = READ_ONCE(led_cdev->trigger_hw_changed);

	scoped_guard(rwsem_read, &triggers_list_lock) {
		struct led_trigger *trig;

		list_for_each_entry(trig, &trigger_list, next_trig) {
			if (trig->trigger_type == led_cdev->trigger_type &&
			    !strcmp(trig->name, led_cdev->hw_control_trigger)) {
				guard(rwsem_write)(&led_cdev->trigger_lock);

				led_trigger_do_hw_control_transition(led_cdev, activate, trig);
				return;
			}
		}
	}

	dev_warn(led_cdev->dev,
		 "Private trigger %s is not registered, can't toggle hardware control\n",
		 led_cdev->hw_control_trigger);
}
EXPORT_SYMBOL_GPL(led_trigger_hw_control_changed_worker);

void led_trigger_notify_hw_control_changed(struct led_classdev *led_cdev, bool activate)
{
	/* Restricted to private triggers. */
	if (WARN_ON(!(led_cdev->flags & LED_TRIG_HW_CHANGED) ||
		    !led_cdev->hw_control_trigger || !led_cdev->trigger_type))
		return;

	WRITE_ONCE(led_cdev->trigger_hw_changed, activate);

	schedule_work(&led_cdev->trigger_hw_changed_work);
}
EXPORT_SYMBOL_GPL(led_trigger_notify_hw_control_changed);

#endif /* CONFIG_LEDS_TRIGGERS_HW_CHANGED */

/* Simple LED Trigger Interface */

void led_trigger_event(struct led_trigger *trig,
			enum led_brightness brightness)
{
	struct led_classdev *led_cdev;

	if (!trig)
		return;

	trig->brightness = brightness;

	rcu_read_lock();
	list_for_each_entry_rcu(led_cdev, &trig->led_cdevs, trig_list)
		led_set_brightness(led_cdev, brightness);
	rcu_read_unlock();
}
EXPORT_SYMBOL_GPL(led_trigger_event);

void led_mc_trigger_event(struct led_trigger *trig,
			  unsigned int *intensity_value, unsigned int num_colors,
			  enum led_brightness brightness)
{
	struct led_classdev *led_cdev;

	if (!trig)
		return;

	rcu_read_lock();
	list_for_each_entry_rcu(led_cdev, &trig->led_cdevs, trig_list) {
		if (!(led_cdev->flags & LED_MULTI_COLOR))
			continue;

		led_mc_set_brightness(led_cdev, intensity_value, num_colors, brightness);
	}
	rcu_read_unlock();
}
EXPORT_SYMBOL_GPL(led_mc_trigger_event);

static void led_trigger_blink_setup(struct led_trigger *trig,
			     unsigned long delay_on,
			     unsigned long delay_off,
			     int oneshot,
			     int invert)
{
	struct led_classdev *led_cdev;

	if (!trig)
		return;

	rcu_read_lock();
	list_for_each_entry_rcu(led_cdev, &trig->led_cdevs, trig_list) {
		if (oneshot)
			led_blink_set_oneshot(led_cdev, &delay_on, &delay_off,
					      invert);
		else
			led_blink_set_nosleep(led_cdev, delay_on, delay_off);
	}
	rcu_read_unlock();
}

void led_trigger_blink(struct led_trigger *trig,
		       unsigned long delay_on,
		       unsigned long delay_off)
{
	led_trigger_blink_setup(trig, delay_on, delay_off, 0, 0);
}
EXPORT_SYMBOL_GPL(led_trigger_blink);

void led_trigger_blink_oneshot(struct led_trigger *trig,
			       unsigned long delay_on,
			       unsigned long delay_off,
			       int invert)
{
	led_trigger_blink_setup(trig, delay_on, delay_off, 1, invert);
}
EXPORT_SYMBOL_GPL(led_trigger_blink_oneshot);

void led_trigger_register_simple(const char *name, struct led_trigger **tp)
{
	struct led_trigger *trig;
	int err;

	trig = kzalloc_obj(struct led_trigger);

	if (trig) {
		trig->name = name;
		err = led_trigger_register(trig);
		if (err < 0) {
			kfree(trig);
			trig = NULL;
			pr_warn("LED trigger %s failed to register (%d)\n",
				name, err);
		}
	} else {
		pr_warn("LED trigger %s failed to register (no memory)\n",
			name);
	}
	*tp = trig;
}
EXPORT_SYMBOL_GPL(led_trigger_register_simple);

void led_trigger_unregister_simple(struct led_trigger *trig)
{
	if (trig)
		led_trigger_unregister(trig);
	kfree(trig);
}
EXPORT_SYMBOL_GPL(led_trigger_unregister_simple);
