/*
 * linux/arch/arm/mach-omap1/leds-osk.c
 *
 * LED driver for OSK with optional Mistral QVGA board
 */
#include <linux/gpio.h>
#include <linux/init.h>

#include <mach/hardware.h>
#include <asm/leds.h>

#include "leds.h"


#define LED_STATE_ENABLED	(1 << 0)
#define LED_STATE_CLAIMED	(1 << 1)
static u8 led_state;

#define	TIMER_LED		(1 << 3)	/* Mistral board */
#define	IDLE_LED		(1 << 4)	/* Mistral board */
static u8 hw_led_state;


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
	/* else both sides are disabled */

	gpio_set_value(GPIO_LED_GREEN, green);
	gpio_set_value(GPIO_LED_RED, red);
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

	default:
		break;
	}

	leds ^= hw_led_state;

done:
	local_irq_restore(flags);
}
