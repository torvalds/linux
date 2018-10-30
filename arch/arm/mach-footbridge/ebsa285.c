// SPDX-License-Identifier: GPL-2.0
/*
 * linux/arch/arm/mach-footbridge/ebsa285.c
 *
 * EBSA285 machine fixup
 */
#include <linux/init.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/leds.h>

#include <asm/hardware/dec21285.h>
#include <asm/mach-types.h>

#include <asm/mach/arch.h>

#include "common.h"

/* LEDs */
#if defined(CONFIG_NEW_LEDS) && defined(CONFIG_LEDS_CLASS)
#define XBUS_AMBER_L	BIT(0)
#define XBUS_GREEN_L	BIT(1)
#define XBUS_RED_L	BIT(2)
#define XBUS_TOGGLE	BIT(7)

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
	{ "ebsa285:amber", "cpu0", },
	{ "ebsa285:green", "heartbeat", },
	{ "ebsa285:red",},
};

static unsigned char hw_led_state;
static void __iomem *xbus;

static void ebsa285_led_set(struct led_classdev *cdev,
		enum led_brightness b)
{
	struct ebsa285_led *led = container_of(cdev,
			struct ebsa285_led, cdev);

	if (b == LED_OFF)
		hw_led_state |= led->mask;
	else
		hw_led_state &= ~led->mask;
	writeb(hw_led_state, xbus);
}

static enum led_brightness ebsa285_led_get(struct led_classdev *cdev)
{
	struct ebsa285_led *led = container_of(cdev,
			struct ebsa285_led, cdev);

	return hw_led_state & led->mask ? LED_OFF : LED_FULL;
}

static int __init ebsa285_leds_init(void)
{
	int i;

	if (!machine_is_ebsa285())
		return -ENODEV;

	xbus = ioremap(XBUS_CS2, SZ_4K);
	if (!xbus)
		return -ENOMEM;

	/* 3 LEDS all off */
	hw_led_state = XBUS_AMBER_L | XBUS_GREEN_L | XBUS_RED_L;
	writeb(hw_led_state, xbus);

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
	.init_early	= footbridge_sched_clock,
	.init_irq	= footbridge_init_irq,
	.init_time	= footbridge_timer_init,
	.restart	= footbridge_restart,
MACHINE_END

