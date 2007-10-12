#ifndef B43legacy_LEDS_H_
#define B43legacy_LEDS_H_

#include <linux/types.h>
#include <linux/timer.h>


struct b43legacy_led {
	u8 behaviour;
	bool activelow;
	/* Index in the "leds" array in b43legacy_wldev */
	u8 index;
	struct b43legacy_wldev *dev;
	struct timer_list blink_timer;
	unsigned long blink_interval;
};

/* Delay between state changes when blinking in jiffies */
#define B43legacy_LEDBLINK_SLOW		(HZ / 1)
#define B43legacy_LEDBLINK_MEDIUM	(HZ / 4)
#define B43legacy_LEDBLINK_FAST		(HZ / 8)

#define B43legacy_LED_XFER_THRES	(HZ / 100)

#define B43legacy_LED_BEHAVIOUR		0x7F
#define B43legacy_LED_ACTIVELOW		0x80
enum { /* LED behaviour values */
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

	/* Behaviour values for testing.
	 * With these values it is easier to figure out
	 * the real behaviour of leds, in case the SPROM
	 * is missing information.
	 */
	B43legacy_LED_TEST_BLINKSLOW,
	B43legacy_LED_TEST_BLINKMEDIUM,
	B43legacy_LED_TEST_BLINKFAST,
};

int b43legacy_leds_init(struct b43legacy_wldev *dev);
void b43legacy_leds_exit(struct b43legacy_wldev *dev);
void b43legacy_leds_update(struct b43legacy_wldev *dev, int activity);
void b43legacy_leds_switch_all(struct b43legacy_wldev *dev, int on);

#endif /* B43legacy_LEDS_H_ */
