/* SPDX-License-Identifier: GPL-2.0 */
#ifndef B43legacy_LEDS_H_
#define B43legacy_LEDS_H_

struct b43legacy_wldev;

#ifdef CONFIG_B43LEGACY_LEDS

#include <linux/types.h>
#include <linux/leds.h>


#define B43legacy_LED_MAX_NAME_LEN	31

struct b43legacy_led {
	struct b43legacy_wldev *dev;
	/* The LED class device */
	struct led_classdev led_dev;
	/* The index number of the LED. */
	u8 index;
	/* If activelow is true, the LED is ON if the
	 * bit is switched off. */
	bool activelow;
	/* The unique name string for this LED device. */
	char name[B43legacy_LED_MAX_NAME_LEN + 1];
};

#define B43legacy_LED_BEHAVIOUR		0x7F
#define B43legacy_LED_ACTIVELOW		0x80
/* LED behaviour values */
enum b43legacy_led_behaviour {
	B43legacy_LED_OFF,
	B43legacy_LED_ON,
	B43legacy_LED_ACTIVITY,
	B43legacy_LED_RADIO_ALL,
	B43legacy_LED_RADIO_A,
	B43legacy_LED_RADIO_B,
	B43legacy_LED_MODE_BG,
	B43legacy_LED_TRANSFER,
	B43legacy_LED_APTRANSFER,
	B43legacy_LED_WEIRD,
	B43legacy_LED_ASSOC,
	B43legacy_LED_INACTIVE,
};

void b43legacy_leds_init(struct b43legacy_wldev *dev);
void b43legacy_leds_exit(struct b43legacy_wldev *dev);

#else /* CONFIG_B43LEGACY_LEDS */
/* LED support disabled */

struct b43legacy_led {
	/* empty */
};

static inline void b43legacy_leds_init(struct b43legacy_wldev *dev)
{
}
static inline void b43legacy_leds_exit(struct b43legacy_wldev *dev)
{
}
#endif /* CONFIG_B43LEGACY_LEDS */

#endif /* B43legacy_LEDS_H_ */
