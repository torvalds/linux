/*
 * linux/arch/arm/mach-omap1/leds-osk.c
 *
 * LED driver for OSK, and optionally Mistral QVGA, boards
 */
#include <linux/init.h>
#include <linux/workqueue.h>
#include <linux/i2c/tps65010.h>

#include <asm/hardware.h>
#include <asm/leds.h>
#include <asm/system.h>

#include <asm/arch/gpio.h>

#include "leds.h"


#define LED_STATE_ENABLED	(1 << 0)
#define LED_STATE_CLAIMED	(1 << 1)
static u8 led_state;

#define	GREEN_LED		(1 << 0)	/* TPS65010 LED1 */
#define	AMBER_LED		(1 << 1)	/* TPS65010 LED2 */
#define	RED_LED			(1 << 2)	/* TPS65010 GPIO2 */
#define	TIMER_LED		(1 << 3)	/* Mistral board */
#define	IDLE_LED		(1 << 4)	/* Mistral board */
static u8 hw_led_state;


/* TPS65010 leds are changed using i2c -- from a task context.
 * Using one of these for the "idle" LED would be impractical...
 */
#define	TPS_LEDS	(GREEN_LED | RED_LED | AMBER_LED)

static u8 tps_leds_change;

static void tps_work(struct work_struct *unused)
{
	for (;;) {
		u8	leds;

		local_irq_disable();
		leds = tps_leds_change;
		tps_leds_change = 0;
		local_irq_enable();

		if (!leds)
			break;

		/* careful:  the set_led() value is on/off/blink */
		if (leds & GREEN_LED)
			tps65010_set_led(LED1, !!(hw_led_state & GREEN_LED));
		if (leds & AMBER_LED)
			tps65010_set_led(LED2, !!(hw_led_state & AMBER_LED));

		/* the gpio led doesn't have that issue */
		if (leds & RED_LED)
			tps65010_set_gpio_out_value(GPIO2,
					!(hw_led_state & RED_LED));
	}
}

static DECLARE_WORK(work, tps_work);

#ifdef	CONFIG_OMAP_OSK_MISTRAL

/* For now, all system indicators require the Mistral board, since that
 * LED can be manipulated without a task context.  This LED is either red,
 * or green, but not both; it can't give the full "disco led" effect.
 */

#define GPIO_LED_RED		3
#define GPIO_LED_GREEN		OMAP_MPUIO(4)

static void mistral_setled(void)
{
	int	red = 0;
	int	green = 0;

	if (hw_led_state & TIMER_LED)
		red = 1;
	else if (hw_led_state & IDLE_LED)
		green = 1;
	// else both sides are disabled

	omap_set_gpio_dataout(GPIO_LED_GREEN, green);
	omap_set_gpio_dataout(GPIO_LED_RED, red);
}

#endif

void osk_leds_event(led_event_t evt)
{
	unsigned long	flags;
	u16		leds;

	local_irq_save(flags);

	if (!(led_state & LED_STATE_ENABLED) && evt != led_start)
		goto done;

	leds = hw_led_state;
	switch (evt) {
	case led_start:
		led_state |= LED_STATE_ENABLED;
		hw_led_state = 0;
		leds = ~0;
		break;

	case led_halted:
	case led_stop:
		led_state &= ~LED_STATE_ENABLED;
		hw_led_state = 0;
		// NOTE:  work may still be pending!!
		break;

	case led_claim:
		led_state |= LED_STATE_CLAIMED;
		hw_led_state = 0;
		leds = ~0;
		break;

	case led_release:
		led_state &= ~LED_STATE_CLAIMED;
		hw_led_state = 0;
		break;

#ifdef	CONFIG_OMAP_OSK_MISTRAL

	case led_timer:
		hw_led_state ^= TIMER_LED;
		mistral_setled();
		break;

	case led_idle_start:	/* idle == off */
		hw_led_state &= ~IDLE_LED;
		mistral_setled();
		break;

	case led_idle_end:
		hw_led_state |= IDLE_LED;
		mistral_setled();
		break;

#endif	/* CONFIG_OMAP_OSK_MISTRAL */

	/* "green" == tps LED1 (leftmost, normally power-good)
	 * works only with DC adapter, not on battery power!
	 */
	case led_green_on:
		if (led_state & LED_STATE_CLAIMED)
			hw_led_state |= GREEN_LED;
		break;
	case led_green_off:
		if (led_state & LED_STATE_CLAIMED)
			hw_led_state &= ~GREEN_LED;
		break;

	/* "amber" == tps LED2 (middle) */
	case led_amber_on:
		if (led_state & LED_STATE_CLAIMED)
			hw_led_state |= AMBER_LED;
		break;
	case led_amber_off:
		if (led_state & LED_STATE_CLAIMED)
			hw_led_state &= ~AMBER_LED;
		break;

	/* "red" == LED on tps gpio3 (rightmost) */
	case led_red_on:
		if (led_state & LED_STATE_CLAIMED)
			hw_led_state |= RED_LED;
		break;
	case led_red_off:
		if (led_state & LED_STATE_CLAIMED)
			hw_led_state &= ~RED_LED;
		break;

	default:
		break;
	}

	leds ^= hw_led_state;
	leds &= TPS_LEDS;
	if (leds && (led_state & LED_STATE_CLAIMED)) {
		tps_leds_change |= leds;
		schedule_work(&work);
	}

done:
	local_irq_restore(flags);
}
