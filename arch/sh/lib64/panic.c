/*
 * Copyright (C) 2003  Richard Curnow, SuperH UK Limited
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/kernel.h>
#include <asm/io.h>
#include <asm/registers.h>

/* THIS IS A PHYSICAL ADDRESS */
#define HDSP2534_ADDR (0x04002100)

#ifdef CONFIG_SH_CAYMAN

static void poor_mans_delay(void)
{
	int i;
	for (i = 0; i < 2500000; i++) {
	}		/* poor man's delay */
}

static void show_value(unsigned long x)
{
	int i;
	unsigned nibble;
	for (i = 0; i < 8; i++) {
		nibble = ((x >> (i * 4)) & 0xf);

		ctrl_outb(nibble + ((nibble > 9) ? 55 : 48),
			  HDSP2534_ADDR + 0xe0 + ((7 - i) << 2));
	}
}

#endif

void
panic_handler(unsigned long panicPC, unsigned long panicSSR,
	      unsigned long panicEXPEVT)
{
#ifdef CONFIG_SH_CAYMAN
	while (1) {
		/* This piece of code displays the PC on the LED display */
		show_value(panicPC);
		poor_mans_delay();
		show_value(panicSSR);
		poor_mans_delay();
		show_value(panicEXPEVT);
		poor_mans_delay();
	}
#endif

	/* Never return from the panic handler */
	for (;;) ;

}
