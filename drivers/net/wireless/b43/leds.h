#ifndef B43_LEDS_H_
#define B43_LEDS_H_

struct b43_wldev;

#ifdef CONFIG_B43_LEDS

#include <linux/types.h>
#include <linux/leds.h>


#define B43_LED_MAX_NAME_LEN	31

struct b43_led {
	struct b43_wldev *dev;
	/* The LED class device */
	struct led_classdev led_dev;
	/* The index number of the LED. */
	u8 index;
	/* If activelow is true, the LED is ON if the
	 * bit is switched off. */
	bool activelow;
	/* The unique name string for this LED device. */
	char name[B43_LED_MAX_NAME_LEN + 1];
};

#define B43_LED_BEHAVIOUR		0x7F
#define B43_LED_ACTIVELOW		0x80
/* LED behaviour values */
enum b43_led_behaviour {
	B43_LED_OFF,
	B43_LED_ON,
	B43_LED_ACTIVITY,
	B43_LED_RADIO_ALL,
	B43_LED_RADIO_A,
	B43_LED_RADIO_B,
	B43_LED_MODE_BG,
	B43_LED_TRANSFER,
	B43_LED_APTRANSFER,
	B43_LED_WEIRD,		//FIXME
	B43_LED_ASSOC,
	B43_LED_INACTIVE,
};

void b43_leds_init(struct b43_wldev *dev);
void b43_leds_exit(struct b43_wldev *dev);


#else /* CONFIG_B43_LEDS */
/* LED support disabled */

struct b43_led {
	/* empty */
};

static inline void b43_leds_init(struct b43_wldev *dev)
{
}
static inline void b43_leds_exit(struct b43_wldev *dev)
{
}
#endif /* CONFIG_B43_LEDS */

#endif /* B43_LEDS_H_ */
