/*
 * linux/arch/arm/mach-footbridge/ebsa285.c
 *
 * EBSA285 machine fixup
 */
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/leds.h>

#include <asm/hardware/dec21285.h>
#include <asm/mach-types.h>

#include <asm/mach/arch.h>

#include "common.h"

/* LEDs */
#if defined(CONFIG_NEW_LEDS) && defined(CONFIG_LEDS_CLASS)
struct ebsa285_led {
	struct led_classdev     cdev;
	u8                      mask;
};

/*
 * The triggers lines up below will only be used if the
 * LED triggers are compiled in.
 */
static const struct {
	const char *name;
	const char *trigger;
} ebsa285_leds[] = {
	{ "ebsa285:amber", "heartbeat", },
	{ "ebsa285:green", "cpu0", },
	{ "ebsa285:red",},
};

static void ebsa285_led_set(struct led_classdev *cdev,
		enum led_brightness b)
{
	struct ebsa285_led *led = container_of(cdev,
			struct ebsa285_led, cdev);

	if (b != LED_OFF)
		*XBUS_LEDS |= led->mask;
	else
		*XBUS_LEDS &= ~led->mask;
}

static enum led_brightness ebsa285_led_get(struct led_classdev *cdev)
{
	struct ebsa285_led *led = container_of(cdev,
			struct ebsa285_led, cdev);

	return (*XBUS_LEDS & led->mask) ? LED_FULL : LED_OFF;
}

static int __init ebsa285_leds_init(void)
{
	int i;

	if (machine_is_ebsa285())
		return -ENODEV;

	/* 3 LEDS All ON */
	*XBUS_LEDS |= XBUS_LED_AMBER | XBUS_LED_GREEN | XBUS_LED_RED;

	for (i = 0; i < ARRAY_SIZE(ebsa285_leds); i++) {
		struct ebsa285_led *led;

		led = kzalloc(sizeof(*led), GFP_KERNEL);
		if (!led)
			break;

		led->cdev.name = ebsa285_leds[i].name;
		led->cdev.brightness_set = ebsa285_led_set;
		led->cdev.brightness_get = ebsa285_led_get;
		led->cdev.default_trigger = ebsa285_leds[i].trigger;
		led->mask = BIT(i);

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
fs_initcall(ebsa285_leds_init);
#endif

MACHINE_START(EBSA285, "EBSA285")
	/* Maintainer: Russell King */
	.atag_offset	= 0x100,
	.video_start	= 0x000a0000,
	.video_end	= 0x000bffff,
	.map_io		= footbridge_map_io,
	.init_irq	= footbridge_init_irq,
	.timer		= &footbridge_timer,
	.restart	= footbridge_restart,
MACHINE_END

