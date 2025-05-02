// SPDX-License-Identifier: GPL-2.0-only
/*
 * LED Class Core
 *
 * Copyright 2005-2006 Openedhand Ltd.
 *
 * Author: Richard Purdie <rpurdie@openedhand.com>
 */

#include <linux/kernel.h>
#include <linux/led-class-multicolor.h>
#include <linux/leds.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/property.h>
#include <linux/rwsem.h>
#include <linux/slab.h>
#include <uapi/linux/uleds.h>
#include "leds.h"

DECLARE_RWSEM(leds_list_lock);
EXPORT_SYMBOL_GPL(leds_list_lock);

LIST_HEAD(leds_list);
EXPORT_SYMBOL_GPL(leds_list);

static const char * const led_colors[LED_COLOR_ID_MAX] = {
	[LED_COLOR_ID_WHITE] = "white",
	[LED_COLOR_ID_RED] = "red",
	[LED_COLOR_ID_GREEN] = "green",
	[LED_COLOR_ID_BLUE] = "blue",
	[LED_COLOR_ID_AMBER] = "amber",
	[LED_COLOR_ID_VIOLET] = "violet",
	[LED_COLOR_ID_YELLOW] = "yellow",
	[LED_COLOR_ID_IR] = "ir",
	[LED_COLOR_ID_MULTI] = "multicolor",
	[LED_COLOR_ID_RGB] = "rgb",
	[LED_COLOR_ID_PURPLE] = "purple",
	[LED_COLOR_ID_ORANGE] = "orange",
	[LED_COLOR_ID_PINK] = "pink",
	[LED_COLOR_ID_CYAN] = "cyan",
	[LED_COLOR_ID_LIME] = "lime",
};

static int __led_set_brightness(struct led_classdev *led_cdev, unsigned int value)
{
	if (!led_cdev->brightness_set)
		return -ENOTSUPP;

	led_cdev->brightness_set(led_cdev, value);

	return 0;
}

static int __led_set_brightness_blocking(struct led_classdev *led_cdev, unsigned int value)
{
	if (!led_cdev->brightness_set_blocking)
		return -ENOTSUPP;

	return led_cdev->brightness_set_blocking(led_cdev, value);
}

static void led_timer_function(struct timer_list *t)
{
	struct led_classdev *led_cdev = from_timer(led_cdev, t, blink_timer);
	unsigned long brightness;
	unsigned long delay;

	if (!led_cdev->blink_delay_on || !led_cdev->blink_delay_off) {
		led_set_brightness_nosleep(led_cdev, LED_OFF);
		clear_bit(LED_BLINK_SW, &led_cdev->work_flags);
		return;
	}

	if (test_and_clear_bit(LED_BLINK_ONESHOT_STOP,
			       &led_cdev->work_flags)) {
		clear_bit(LED_BLINK_SW, &led_cdev->work_flags);
		return;
	}

	brightness = led_get_brightness(led_cdev);
	if (!brightness) {
		/* Time to switch the LED on. */
		if (test_and_clear_bit(LED_BLINK_BRIGHTNESS_CHANGE,
					&led_cdev->work_flags))
			brightness = led_cdev->new_blink_brightness;
		else
			brightness = led_cdev->blink_brightness;
		delay = led_cdev->blink_delay_on;
	} else {
		/* Store the current brightness value to be able
		 * to restore it when the delay_off period is over.
		 */
		led_cdev->blink_brightness = brightness;
		brightness = LED_OFF;
		delay = led_cdev->blink_delay_off;
	}

	led_set_brightness_nosleep(led_cdev, brightness);

	/* Return in next iteration if led is in one-shot mode and we are in
	 * the final blink state so that the led is toggled each delay_on +
	 * delay_off milliseconds in worst case.
	 */
	if (test_bit(LED_BLINK_ONESHOT, &led_cdev->work_flags)) {
		if (test_bit(LED_BLINK_INVERT, &led_cdev->work_flags)) {
			if (brightness)
				set_bit(LED_BLINK_ONESHOT_STOP,
					&led_cdev->work_flags);
		} else {
			if (!brightness)
				set_bit(LED_BLINK_ONESHOT_STOP,
					&led_cdev->work_flags);
		}
	}

	mod_timer(&led_cdev->blink_timer, jiffies + msecs_to_jiffies(delay));
}

static void set_brightness_delayed_set_brightness(struct led_classdev *led_cdev,
						  unsigned int value)
{
	int ret;

	ret = __led_set_brightness(led_cdev, value);
	if (ret == -ENOTSUPP) {
		ret = __led_set_brightness_blocking(led_cdev, value);
		if (ret == -ENOTSUPP)
			/* No back-end support to set a fixed brightness value */
			return;
	}

	/* LED HW might have been unplugged, therefore don't warn */
	if (ret == -ENODEV && led_cdev->flags & LED_UNREGISTERING &&
	    led_cdev->flags & LED_HW_PLUGGABLE)
		return;

	if (ret < 0)
		dev_err(led_cdev->dev,
			"Setting an LED's brightness failed (%d)\n", ret);
}

static void set_brightness_delayed(struct work_struct *ws)
{
	struct led_classdev *led_cdev =
		container_of(ws, struct led_classdev, set_brightness_work);

	if (test_and_clear_bit(LED_BLINK_DISABLE, &led_cdev->work_flags)) {
		led_stop_software_blink(led_cdev);
		set_bit(LED_SET_BRIGHTNESS_OFF, &led_cdev->work_flags);
	}

	/*
	 * Triggers may call led_set_brightness(LED_OFF),
	 * led_set_brightness(LED_FULL) in quick succession to disable blinking
	 * and turn the LED on. Both actions may have been scheduled to run
	 * before this work item runs once. To make sure this works properly
	 * handle LED_SET_BRIGHTNESS_OFF first.
	 */
	if (test_and_clear_bit(LED_SET_BRIGHTNESS_OFF, &led_cdev->work_flags)) {
		set_brightness_delayed_set_brightness(led_cdev, LED_OFF);
		/*
		 * The consecutives led_set_brightness(LED_OFF),
		 * led_set_brightness(LED_FULL) could have been executed out of
		 * order (LED_FULL first), if the work_flags has been set
		 * between LED_SET_BRIGHTNESS_OFF and LED_SET_BRIGHTNESS of this
		 * work. To avoid ending with the LED turned off, turn the LED
		 * on again.
		 */
		if (led_cdev->delayed_set_value != LED_OFF)
			set_bit(LED_SET_BRIGHTNESS, &led_cdev->work_flags);
	}

	if (test_and_clear_bit(LED_SET_BRIGHTNESS, &led_cdev->work_flags))
		set_brightness_delayed_set_brightness(led_cdev, led_cdev->delayed_set_value);

	if (test_and_clear_bit(LED_SET_BLINK, &led_cdev->work_flags)) {
		unsigned long delay_on = led_cdev->delayed_delay_on;
		unsigned long delay_off = led_cdev->delayed_delay_off;

		led_blink_set(led_cdev, &delay_on, &delay_off);
	}
}

static void led_set_software_blink(struct led_classdev *led_cdev,
				   unsigned long delay_on,
				   unsigned long delay_off)
{
	int current_brightness;

	current_brightness = led_get_brightness(led_cdev);
	if (current_brightness)
		led_cdev->blink_brightness = current_brightness;
	if (!led_cdev->blink_brightness)
		led_cdev->blink_brightness = led_cdev->max_brightness;

	led_cdev->blink_delay_on = delay_on;
	led_cdev->blink_delay_off = delay_off;

	/* never on - just set to off */
	if (!delay_on) {
		led_set_brightness_nosleep(led_cdev, LED_OFF);
		return;
	}

	/* never off - just set to brightness */
	if (!delay_off) {
		led_set_brightness_nosleep(led_cdev,
					   led_cdev->blink_brightness);
		return;
	}

	set_bit(LED_BLINK_SW, &led_cdev->work_flags);
	mod_timer(&led_cdev->blink_timer, jiffies + 1);
}


static void led_blink_setup(struct led_classdev *led_cdev,
		     unsigned long *delay_on,
		     unsigned long *delay_off)
{
	if (!test_bit(LED_BLINK_ONESHOT, &led_cdev->work_flags) &&
	    led_cdev->blink_set &&
	    !led_cdev->blink_set(led_cdev, delay_on, delay_off))
		return;

	/* blink with 1 Hz as default if nothing specified */
	if (!*delay_on && !*delay_off)
		*delay_on = *delay_off = 500;

	led_set_software_blink(led_cdev, *delay_on, *delay_off);
}

void led_init_core(struct led_classdev *led_cdev)
{
	INIT_WORK(&led_cdev->set_brightness_work, set_brightness_delayed);

	timer_setup(&led_cdev->blink_timer, led_timer_function, 0);
}
EXPORT_SYMBOL_GPL(led_init_core);

void led_blink_set(struct led_classdev *led_cdev,
		   unsigned long *delay_on,
		   unsigned long *delay_off)
{
	timer_delete_sync(&led_cdev->blink_timer);

	clear_bit(LED_BLINK_SW, &led_cdev->work_flags);
	clear_bit(LED_BLINK_ONESHOT, &led_cdev->work_flags);
	clear_bit(LED_BLINK_ONESHOT_STOP, &led_cdev->work_flags);

	led_blink_setup(led_cdev, delay_on, delay_off);
}
EXPORT_SYMBOL_GPL(led_blink_set);

void led_blink_set_oneshot(struct led_classdev *led_cdev,
			   unsigned long *delay_on,
			   unsigned long *delay_off,
			   int invert)
{
	if (test_bit(LED_BLINK_ONESHOT, &led_cdev->work_flags) &&
	     timer_pending(&led_cdev->blink_timer))
		return;

	set_bit(LED_BLINK_ONESHOT, &led_cdev->work_flags);
	clear_bit(LED_BLINK_ONESHOT_STOP, &led_cdev->work_flags);

	if (invert)
		set_bit(LED_BLINK_INVERT, &led_cdev->work_flags);
	else
		clear_bit(LED_BLINK_INVERT, &led_cdev->work_flags);

	led_blink_setup(led_cdev, delay_on, delay_off);
}
EXPORT_SYMBOL_GPL(led_blink_set_oneshot);

void led_blink_set_nosleep(struct led_classdev *led_cdev, unsigned long delay_on,
			   unsigned long delay_off)
{
	/* If necessary delegate to a work queue task. */
	if (led_cdev->blink_set && led_cdev->brightness_set_blocking) {
		led_cdev->delayed_delay_on = delay_on;
		led_cdev->delayed_delay_off = delay_off;
		set_bit(LED_SET_BLINK, &led_cdev->work_flags);
		queue_work(led_cdev->wq, &led_cdev->set_brightness_work);
		return;
	}

	led_blink_set(led_cdev, &delay_on, &delay_off);
}
EXPORT_SYMBOL_GPL(led_blink_set_nosleep);

void led_stop_software_blink(struct led_classdev *led_cdev)
{
	timer_delete_sync(&led_cdev->blink_timer);
	led_cdev->blink_delay_on = 0;
	led_cdev->blink_delay_off = 0;
	clear_bit(LED_BLINK_SW, &led_cdev->work_flags);
}
EXPORT_SYMBOL_GPL(led_stop_software_blink);

void led_set_brightness(struct led_classdev *led_cdev, unsigned int brightness)
{
	/*
	 * If software blink is active, delay brightness setting
	 * until the next timer tick.
	 */
	if (test_bit(LED_BLINK_SW, &led_cdev->work_flags)) {
		/*
		 * If we need to disable soft blinking delegate this to the
		 * work queue task to avoid problems in case we are called
		 * from hard irq context.
		 */
		if (!brightness) {
			set_bit(LED_BLINK_DISABLE, &led_cdev->work_flags);
			queue_work(led_cdev->wq, &led_cdev->set_brightness_work);
		} else {
			set_bit(LED_BLINK_BRIGHTNESS_CHANGE,
				&led_cdev->work_flags);
			led_cdev->new_blink_brightness = brightness;
		}
		return;
	}

	led_set_brightness_nosleep(led_cdev, brightness);
}
EXPORT_SYMBOL_GPL(led_set_brightness);

void led_set_brightness_nopm(struct led_classdev *led_cdev, unsigned int value)
{
	/* Use brightness_set op if available, it is guaranteed not to sleep */
	if (!__led_set_brightness(led_cdev, value))
		return;

	/*
	 * Brightness setting can sleep, delegate it to a work queue task.
	 * value 0 / LED_OFF is special, since it also disables hw-blinking
	 * (sw-blink disable is handled in led_set_brightness()).
	 * To avoid a hw-blink-disable getting lost when a second brightness
	 * change is done immediately afterwards (before the work runs),
	 * it uses a separate work_flag.
	 */
	led_cdev->delayed_set_value = value;
	/* Ensure delayed_set_value is seen before work_flags modification */
	smp_mb__before_atomic();

	if (value)
		set_bit(LED_SET_BRIGHTNESS, &led_cdev->work_flags);
	else {
		clear_bit(LED_SET_BRIGHTNESS, &led_cdev->work_flags);
		clear_bit(LED_SET_BLINK, &led_cdev->work_flags);
		set_bit(LED_SET_BRIGHTNESS_OFF, &led_cdev->work_flags);
	}

	queue_work(led_cdev->wq, &led_cdev->set_brightness_work);
}
EXPORT_SYMBOL_GPL(led_set_brightness_nopm);

void led_set_brightness_nosleep(struct led_classdev *led_cdev, unsigned int value)
{
	led_cdev->brightness = min(value, led_cdev->max_brightness);

	if (led_cdev->flags & LED_SUSPENDED)
		return;

	led_set_brightness_nopm(led_cdev, led_cdev->brightness);
}
EXPORT_SYMBOL_GPL(led_set_brightness_nosleep);

int led_set_brightness_sync(struct led_classdev *led_cdev, unsigned int value)
{
	if (led_cdev->blink_delay_on || led_cdev->blink_delay_off)
		return -EBUSY;

	led_cdev->brightness = min(value, led_cdev->max_brightness);

	if (led_cdev->flags & LED_SUSPENDED)
		return 0;

	return __led_set_brightness_blocking(led_cdev, led_cdev->brightness);
}
EXPORT_SYMBOL_GPL(led_set_brightness_sync);

/*
 * This is a led-core function because just like led_set_brightness()
 * it is used in the kernel by e.g. triggers.
 */
void led_mc_set_brightness(struct led_classdev *led_cdev,
			   unsigned int *intensity_value, unsigned int num_colors,
			   unsigned int brightness)
{
	struct led_classdev_mc *mcled_cdev;
	unsigned int i;

	if (!(led_cdev->flags & LED_MULTI_COLOR)) {
		dev_err_once(led_cdev->dev, "error not a multi-color LED\n");
		return;
	}

	mcled_cdev = lcdev_to_mccdev(led_cdev);
	if (num_colors != mcled_cdev->num_colors) {
		dev_err_once(led_cdev->dev, "error num_colors mismatch %u != %u\n",
			     num_colors, mcled_cdev->num_colors);
		return;
	}

	for (i = 0; i < mcled_cdev->num_colors; i++)
		mcled_cdev->subled_info[i].intensity = intensity_value[i];

	led_set_brightness(led_cdev, brightness);
}
EXPORT_SYMBOL_GPL(led_mc_set_brightness);

int led_update_brightness(struct led_classdev *led_cdev)
{
	int ret;

	if (led_cdev->brightness_get) {
		ret = led_cdev->brightness_get(led_cdev);
		if (ret < 0)
			return ret;

		led_cdev->brightness = ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(led_update_brightness);

u32 *led_get_default_pattern(struct led_classdev *led_cdev, unsigned int *size)
{
	struct fwnode_handle *fwnode = led_cdev->dev->fwnode;
	u32 *pattern;
	int count;

	count = fwnode_property_count_u32(fwnode, "led-pattern");
	if (count < 0)
		return NULL;

	pattern = kcalloc(count, sizeof(*pattern), GFP_KERNEL);
	if (!pattern)
		return NULL;

	if (fwnode_property_read_u32_array(fwnode, "led-pattern", pattern, count)) {
		kfree(pattern);
		return NULL;
	}

	*size = count;

	return pattern;
}
EXPORT_SYMBOL_GPL(led_get_default_pattern);

/* Caller must ensure led_cdev->led_access held */
void led_sysfs_disable(struct led_classdev *led_cdev)
{
	lockdep_assert_held(&led_cdev->led_access);

	led_cdev->flags |= LED_SYSFS_DISABLE;
}
EXPORT_SYMBOL_GPL(led_sysfs_disable);

/* Caller must ensure led_cdev->led_access held */
void led_sysfs_enable(struct led_classdev *led_cdev)
{
	lockdep_assert_held(&led_cdev->led_access);

	led_cdev->flags &= ~LED_SYSFS_DISABLE;
}
EXPORT_SYMBOL_GPL(led_sysfs_enable);

static void led_parse_fwnode_props(struct device *dev,
				   struct fwnode_handle *fwnode,
				   struct led_properties *props)
{
	int ret;

	if (!fwnode)
		return;

	if (fwnode_property_present(fwnode, "label")) {
		ret = fwnode_property_read_string(fwnode, "label", &props->label);
		if (ret)
			dev_err(dev, "Error parsing 'label' property (%d)\n", ret);
		return;
	}

	if (fwnode_property_present(fwnode, "color")) {
		ret = fwnode_property_read_u32(fwnode, "color", &props->color);
		if (ret)
			dev_err(dev, "Error parsing 'color' property (%d)\n", ret);
		else if (props->color >= LED_COLOR_ID_MAX)
			dev_err(dev, "LED color identifier out of range\n");
		else
			props->color_present = true;
	}


	if (!fwnode_property_present(fwnode, "function"))
		return;

	ret = fwnode_property_read_string(fwnode, "function", &props->function);
	if (ret) {
		dev_err(dev,
			"Error parsing 'function' property (%d)\n",
			ret);
	}

	if (!fwnode_property_present(fwnode, "function-enumerator"))
		return;

	ret = fwnode_property_read_u32(fwnode, "function-enumerator",
				       &props->func_enum);
	if (ret) {
		dev_err(dev,
			"Error parsing 'function-enumerator' property (%d)\n",
			ret);
	} else {
		props->func_enum_present = true;
	}
}

int led_compose_name(struct device *dev, struct led_init_data *init_data,
		     char *led_classdev_name)
{
	struct led_properties props = {};
	struct fwnode_handle *fwnode = init_data->fwnode;
	const char *devicename = init_data->devicename;

	if (!led_classdev_name)
		return -EINVAL;

	led_parse_fwnode_props(dev, fwnode, &props);

	if (props.label) {
		/*
		 * If init_data.devicename is NULL, then it indicates that
		 * DT label should be used as-is for LED class device name.
		 * Otherwise the label is prepended with devicename to compose
		 * the final LED class device name.
		 */
		if (!devicename) {
			strscpy(led_classdev_name, props.label,
				LED_MAX_NAME_SIZE);
		} else {
			snprintf(led_classdev_name, LED_MAX_NAME_SIZE, "%s:%s",
				 devicename, props.label);
		}
	} else if (props.function || props.color_present) {
		char tmp_buf[LED_MAX_NAME_SIZE];

		if (props.func_enum_present) {
			snprintf(tmp_buf, LED_MAX_NAME_SIZE, "%s:%s-%d",
				 props.color_present ? led_colors[props.color] : "",
				 props.function ?: "", props.func_enum);
		} else {
			snprintf(tmp_buf, LED_MAX_NAME_SIZE, "%s:%s",
				 props.color_present ? led_colors[props.color] : "",
				 props.function ?: "");
		}
		if (init_data->devname_mandatory) {
			snprintf(led_classdev_name, LED_MAX_NAME_SIZE, "%s:%s",
				 devicename, tmp_buf);
		} else {
			strscpy(led_classdev_name, tmp_buf, LED_MAX_NAME_SIZE);

		}
	} else if (init_data->default_label) {
		if (!devicename) {
			dev_err(dev, "Legacy LED naming requires devicename segment");
			return -EINVAL;
		}
		snprintf(led_classdev_name, LED_MAX_NAME_SIZE, "%s:%s",
			 devicename, init_data->default_label);
	} else if (is_of_node(fwnode)) {
		strscpy(led_classdev_name, to_of_node(fwnode)->name,
			LED_MAX_NAME_SIZE);
	} else
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL_GPL(led_compose_name);

const char *led_get_color_name(u8 color_id)
{
	if (color_id >= ARRAY_SIZE(led_colors))
		return NULL;

	return led_colors[color_id];
}
EXPORT_SYMBOL_GPL(led_get_color_name);

enum led_default_state led_init_default_state_get(struct fwnode_handle *fwnode)
{
	const char *state = NULL;

	if (!fwnode_property_read_string(fwnode, "default-state", &state)) {
		if (!strcmp(state, "keep"))
			return LEDS_DEFSTATE_KEEP;
		if (!strcmp(state, "on"))
			return LEDS_DEFSTATE_ON;
	}

	return LEDS_DEFSTATE_OFF;
}
EXPORT_SYMBOL_GPL(led_init_default_state_get);
