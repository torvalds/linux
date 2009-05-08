/*
 * Copyright (C) 2003  Richard Curnow, SuperH UK Limited
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/kernel.h>
#include <linux/io.h>
#include <cpu/registers.h>

/* THIS IS A PHYSICAL ADDRESS */
#define HDSP2534_ADDR (0x04002100)

static void poor_mans_delay(void)
{
	int i;

	for (i = 0; i < 2500000; i++)
		cpu_relax();
}

static void show_value(unsigned long x)
{
	int i;
	unsigned nibble;
	for (i = 0; i < 8; i++) {
		nibble = ((x >> (i * 4)) & 0xf);

		__raw_writeb(nibble + ((nibble > 9) ? 55 : 48),
			  HDSP2534_ADDR + 0xe0 + ((7 - i) << 2));
	}
}

void
panic_handler(unsigned long panicPC, unsigned long panicSSR,
	      unsigned long panicEXPEVT)
{
	while (1) {
		/* This piece of code displays the PC on the LED display */
		show_value(panicPC);
		poor_mans_delay();
		show_value(panicSSR);
		poor_mans_delay();
		show_value(panicEXPEVT);
		poor_mans_delay();
	}
}
