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
#include <linux/jiffies.h>

#include <asm/io.h>
#include <asm/reboot.h>

#include <cobalt.h>

void cobalt_machine_halt(void)
{
	int state, last, diff;
	unsigned long mark;

	/*
	 * turn off bar on Qube, flash power off LED on RaQ (0.5Hz)
	 *
	 * restart if ENTER and SELECT are pressed
	 */

	last = COBALT_KEY_PORT;

	for (state = 0;;) {

		state ^= COBALT_LED_POWER_OFF;
		COBALT_LED_PORT = state;

		diff = COBALT_KEY_PORT ^ last;
		last ^= diff;

		if((diff & (COBALT_KEY_ENTER | COBALT_KEY_SELECT)) && !(~last & (COBALT_KEY_ENTER | COBALT_KEY_SELECT)))
			COBALT_LED_PORT = COBALT_LED_RESET;

		for (mark = jiffies; jiffies - mark < HZ;)
			;
	}
}

void cobalt_machine_restart(char *command)
{
	COBALT_LED_PORT = COBALT_LED_RESET;

	/* we should never get here */
	cobalt_machine_halt();
}

/*
 * This triggers the luser mode device driver for the power switch ;-)
 */
void cobalt_machine_power_off(void)
{
	printk("You can switch the machine off now.\n");
	cobalt_machine_halt();
}
