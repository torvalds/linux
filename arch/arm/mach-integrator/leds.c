/*
 * Driver for the 4 user LEDs found on the Integrator AP/CP baseboard
 * Based on Versatile and RealView machine LED code
 *
 * License terms: GNU General Public License (GPL) version 2
 * Author: Bryan Wu <bryan.wu@canonical.com>
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/leds.h>

#include "hardware.h"
#include "cm.h"

#if defined(CONFIG_NEW_LEDS) && defined(CONFIG_LEDS_CLASS)

struct integrator_led {
	struct led_classdev	cdev;
};

/*
 * The triggers lines up below will only be used if the
 * LED triggers are compiled in.
 */
static const struct {
	const char *name;
	const char *trigger;
} integrator_leds[] = {
	{ "integrator:core_module", "cpu0", },
};

static void cm_led_set(struct led_classdev *cdev,
			      enum led_brightness b)
{
	if (b != LED_OFF)
		cm_control(CM_CTRL_LED, CM_CTRL_LED);
	else
		cm_control(CM_CTRL_LED, 0);
}

static enum led_brightness cm_led_get(struct led_classdev *cdev)
{
	u32 reg = cm_get();

	return (reg & CM_CTRL_LED) ? LED_FULL : LED_OFF;
}

static int __init integrator_leds_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(integrator_leds); i++) {
		struct integrator_led *led;

		led = kzalloc(sizeof(*led), GFP_KERNEL);
		if (!led)
			break;

		led->cdev.name = integrator_leds[i].name;
		led->cdev.brightness_set = cm_led_set;
		led->cdev.brightness_get = cm_led_get;
		led->cdev.default_trigger = integrator_leds[i].trigger;

		if (led_classdev_register(NULL, &led->cdev) < 0) {
			kfree(led);
			break;
		}
	}

	return 0;
}

/*
 * Since we may have triggers on any subsystem, defer registration
 * until after subsystem_init.
 */
fs_initcall(integrator_leds_init);
#endif
