/*
 * linux/arch/arm/mach-omap1/leds.c
 *
 * OMAP LEDs dispatcher
 */
#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/leds.h>
#include <asm/mach-types.h>

#include <mach/gpio.h>
#include <plat/mux.h>

#include "leds.h"

static int __init
omap_leds_init(void)
{
	if (!cpu_class_is_omap1())
		return -ENODEV;

	if (machine_is_omap_innovator())
		leds_event = innovator_leds_event;

	else if (machine_is_omap_h2()
			|| machine_is_omap_h3()
			|| machine_is_omap_perseus2())
		leds_event = h2p2_dbg_leds_event;

	else if (machine_is_omap_osk())
		leds_event = osk_leds_event;

	else
		return -1;

	if (machine_is_omap_h2()
			|| machine_is_omap_h3()
#ifdef	CONFIG_OMAP_OSK_MISTRAL
			|| machine_is_omap_osk()
#endif
			) {

		/* LED1/LED2 pins can be used as GPIO (as done here), or by
		 * the LPG (works even in deep sleep!), to drive a bicolor
		 * LED on the H2 sample board, and another on the H2/P2
		 * "surfer" expansion board.
		 *
		 * The same pins drive a LED on the OSK Mistral board, but
		 * that's a different kind of LED (just one color at a time).
		 */
		omap_cfg_reg(P18_1610_GPIO3);
		if (gpio_request(3, "LED red") == 0)
			gpio_direction_output(3, 1);
		else
			printk(KERN_WARNING "LED: can't get GPIO3/red?\n");

		omap_cfg_reg(MPUIO4);
		if (gpio_request(OMAP_MPUIO(4), "LED green") == 0)
			gpio_direction_output(OMAP_MPUIO(4), 1);
		else
			printk(KERN_WARNING "LED: can't get MPUIO4/green?\n");
	}

	leds_event(led_start);
	return 0;
}

__initcall(omap_leds_init);
