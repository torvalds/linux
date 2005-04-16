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
#include <linux/sched.h>
#include <linux/mm.h>
#include <asm/cacheflush.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/reboot.h>
#include <asm/system.h>
#include <asm/mipsregs.h>

void cobalt_machine_restart(char *command)
{
	*(volatile char *)0xbc000000 = 0x0f;

	/*
	 * Ouch, we're still alive ... This time we take the silver bullet ...
	 * ... and find that we leave the hardware in a state in which the
	 * kernel in the flush locks up somewhen during of after the PCI
	 * detection stuff.
	 */
	set_c0_status(ST0_BEV | ST0_ERL);
	change_c0_config(CONF_CM_CMASK, CONF_CM_UNCACHED);
	flush_cache_all();
	write_c0_wired(0);
	__asm__ __volatile__(
		"jr\t%0"
		:
		: "r" (0xbfc00000));
}

extern int led_state;
#define kLED            0xBC000000
#define LEDSet(x)       (*(volatile unsigned char *) kLED) = (( unsigned char)x)

void cobalt_machine_halt(void)
{
	int mark;

	/* Blink our cute? little LED (number 3)... */
	while (1) {
		led_state = led_state | ( 1 << 3 );
		LEDSet(led_state);
		mark = jiffies;
		while (jiffies<(mark+HZ));
		led_state = led_state & ~( 1 << 3 );
		LEDSet(led_state);
		mark = jiffies;
		while (jiffies<(mark+HZ));
	}
}

/*
 * This triggers the luser mode device driver for the power switch ;-)
 */
void cobalt_machine_power_off(void)
{
	printk("You can switch the machine off now.\n");
	cobalt_machine_halt();
}
