/*
 * DIGITAL Shark LED control routines.
 *
 * Driver for the 3 user LEDs found on the Shark
 * Based on Versatile and RealView machine LED code
 *
 * License terms: GNU General Public License (GPL) version 2
 * Author: Bryan Wu <bryan.wu@canonical.com>
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/leds.h>

#include <asm/mach-types.h>

#if defined(CONFIG_NEW_LEDS) && defined(CONFIG_LEDS_CLASS)
struct shark_led {
	struct led_classdev cdev;
	u8 mask;
};

/*
 * The triggers lines up below will only be used if the
 * LED triggers are compiled in.
 */
static const struct {
	const char *name;
	const char *trigger;
} shark_leds[] = {
	{ "shark:amber0", "default-on", },	/* Bit 5 */
	{ "shark:green", "heartbeat", },	/* Bit 6 */
	{ "shark:amber1", "cpu0" },		/* Bit 7 */
};

static u16 led_reg_read(void)
{
	outw(0x09, 0x24);
	return inw(0x26);
}

static void led_reg_write(u16 value)
{
	outw(0x09, 0x24);
	outw(value, 0x26);
}

static void shark_led_set(struct led_classdev *cdev,
			      enum led_brightness b)
{
	struct shark_led *led = container_of(cdev,
						 struct shark_led, cdev);
	u16 reg = led_reg_read();

	if (b != LED_OFF)
		reg |= led->mask;
	else
		reg &= ~led->mask;

	led_reg_write(reg);
}

static enum led_brightness shark_led_get(struct led_classdev *cdev)
{
	struct shark_led *led = container_of(cdev,
						 struct shark_led, cdev);
	u16 reg = led_reg_read();

	return (reg & led->mask) ? LED_FULL : LED_OFF;
}

static int __init shark_leds_init(void)
{
	int i;
	u16 reg;

	if (!machine_is_shark())
		return -ENODEV;

	for (i = 0; i < ARRAY_SIZE(shark_leds); i++) {
		struct shark_led *led;

		led = kzalloc(sizeof(*led), GFP_KERNEL);
		if (!led)
			break;

		led->cdev.name = shark_leds[i].name;
		led->cdev.brightness_set = shark_led_set;
		led->cdev.brightness_get = shark_led_get;
		led->cdev.default_trigger = shark_leds[i].trigger;

		/* Count in 5 bits offset */
		led->mask = BIT(i + 5);

		if (led_classdev_register(NULL, &led->cdev) < 0) {
			kfree(led);
			break;
		}
	}

	/* Make LEDs independent of power-state */
	request_region(0x24, 4, "led_reg");
	reg = led_reg_read();
	reg |= 1 << 10;
	led_reg_write(reg);

	return 0;
}

/*
 * Since we may have triggers on any subsystem, defer registration
 * until after subsystem_init.
 */
fs_initcall(shark_leds_init);
#endif
