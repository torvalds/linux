/*
 * Cobalt Reset operations
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 1996, 1997 by Ralf Baechle
 * Copyright (C) 2001 by Liam Davies (ldavies@agile.tv)
 */
#include <linux/init.h>
#include <linux/io.h>
#include <linux/leds.h>

#include <asm/processor.h>

#include <cobalt.h>

#define RESET_PORT	((void __iomem *)CKSEG1ADDR(0x1c000000))
#define RESET		0x0f

DEFINE_LED_TRIGGER(power_off_led_trigger);

static int __init ledtrig_power_off_init(void)
{
	led_trigger_register_simple("power-off", &power_off_led_trigger);
	return 0;
}
device_initcall(ledtrig_power_off_init);

void cobalt_machine_halt(void)
{
	/*
	 * turn on power off LED on RaQ
	 */
	led_trigger_event(power_off_led_trigger, LED_FULL);

	local_irq_disable();
	while (1) {
		if (cpu_wait)
			cpu_wait();
	}
}

void cobalt_machine_restart(char *command)
{
	writeb(RESET, RESET_PORT);

	/* we should never get here */
	cobalt_machine_halt();
}
