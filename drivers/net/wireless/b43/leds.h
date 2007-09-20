#ifndef B43_LEDS_H_
#define B43_LEDS_H_

#include <linux/types.h>
#include <linux/timer.h>

struct b43_led {
	u8 behaviour;
	bool activelow;
	/* Index in the "leds" array in b43_wldev */
	u8 index;
	struct b43_wldev *dev;
	struct timer_list blink_timer;
	unsigned long blink_interval;
};

/* Delay between state changes when blinking in jiffies */
#define B43_LEDBLINK_SLOW		(HZ / 1)
#define B43_LEDBLINK_MEDIUM		(HZ / 4)
#define B43_LEDBLINK_FAST		(HZ / 8)

#define B43_LED_XFER_THRES		(HZ / 100)

#define B43_LED_BEHAVIOUR		0x7F
#define B43_LED_ACTIVELOW		0x80
enum {				/* LED behaviour values */
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

	/* Behaviour values for testing.
	 * With these values it is easier to figure out
	 * the real behaviour of leds, in case the SPROM
	 * is missing information.
	 */
	B43_LED_TEST_BLINKSLOW,
	B43_LED_TEST_BLINKMEDIUM,
	B43_LED_TEST_BLINKFAST,
};

int b43_leds_init(struct b43_wldev *dev);
void b43_leds_exit(struct b43_wldev *dev);
void b43_leds_update(struct b43_wldev *dev, int activity);
void b43_leds_switch_all(struct b43_wldev *dev, int on);

#endif /* B43_LEDS_H_ */
