#ifndef B43_LEDS_H_
#define B43_LEDS_H_

struct b43_wldev;

#ifdef CONFIG_B43_LEDS

#include <linux/types.h>
#include <linux/leds.h>
#include <linux/workqueue.h>


#define B43_LED_MAX_NAME_LEN	31

struct b43_led {
	struct b43_wl *wl;
	/* The LED class device */
	struct led_classdev led_dev;
	/* The index number of the LED. */
	u8 index;
	/* If activelow is true, the LED is ON if the
	 * bit is switched off. */
	bool activelow;
	/* The unique name string for this LED device. */
	char name[B43_LED_MAX_NAME_LEN + 1];
	/* The current status of the LED. This is updated locklessly. */
	atomic_t state;
	/* The active state in hardware. */
	bool hw_state;
};

struct b43_leds {
	struct b43_led led_tx;
	struct b43_led led_rx;
	struct b43_led led_radio;
	struct b43_led led_assoc;

	bool stop;
	struct work_struct work;
};

#define B43_MAX_NR_LEDS			4

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

void b43_leds_register(struct b43_wldev *dev);
void b43_leds_unregister(struct b43_wldev *dev);
void b43_leds_init(struct b43_wldev *dev);
void b43_leds_exit(struct b43_wldev *dev);
void b43_leds_stop(struct b43_wldev *dev);


#else /* CONFIG_B43_LEDS */
/* LED support disabled */

struct b43_leds {
	/* empty */
};

static inline void b43_leds_register(struct b43_wldev *dev)
{
}
static inline void b43_leds_unregister(struct b43_wldev *dev)
{
}
static inline void b43_leds_init(struct b43_wldev *dev)
{
}
static inline void b43_leds_exit(struct b43_wldev *dev)
{
}
static inline void b43_leds_stop(struct b43_wldev *dev)
{
}
#endif /* CONFIG_B43_LEDS */

#endif /* B43_LEDS_H_ */
