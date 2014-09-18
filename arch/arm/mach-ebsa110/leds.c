/*
 * Driver for the LED found on the EBSA110 machine
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

#include <asm/mach-types.h>

#include "core.h"

#if defined(CONFIG_NEW_LEDS) && defined(CONFIG_LEDS_CLASS)
static void ebsa110_led_set(struct led_classdev *cdev,
			      enum led_brightness b)
{
	u8 reg = __raw_readb(SOFT_BASE);

	if (b != LED_OFF)
		reg |= 0x80;
	else
		reg &= ~0x80;

	__raw_writeb(reg, SOFT_BASE);
}

static enum led_brightness ebsa110_led_get(struct led_classdev *cdev)
{
	u8 reg = __raw_readb(SOFT_BASE);

	return (reg & 0x80) ? LED_FULL : LED_OFF;
}

static int __init ebsa110_leds_init(void)
{

	struct led_classdev *cdev;
	int ret;

	if (!machine_is_ebsa110())
		return -ENODEV;

	cdev = kzalloc(sizeof(*cdev), GFP_KERNEL);
	if (!cdev)
		return -ENOMEM;

	cdev->name = "ebsa110:0";
	cdev->brightness_set = ebsa110_led_set;
	cdev->brightness_get = ebsa110_led_get;
	cdev->default_trigger = "heartbeat";

	ret = led_classdev_register(NULL, cdev);
	if (ret	< 0) {
		kfree(cdev);
		return ret;
	}

	return 0;
}

/*
 * Since we may have triggers on any subsystem, defer registration
 * until after subsystem_init.
 */
fs_initcall(ebsa110_leds_init);
#endif
