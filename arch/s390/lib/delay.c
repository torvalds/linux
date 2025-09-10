// SPDX-License-Identifier: GPL-2.0
/*
 *    Precise Delay Loops for S390
 *
 *    Copyright IBM Corp. 1999, 2008
 *    Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>,
 */

#include <linux/processor.h>
#include <linux/export.h>
#include <linux/delay.h>
#include <asm/div64.h>
#include <asm/timex.h>

void __delay(unsigned long loops)
{
	/*
	 * Loop 'loops' times. Callers must not assume a specific
	 * amount of time passes before this function returns.
	 */
	asm volatile("0: brct %0,0b" : : "d" ((loops/2) + 1));
}
EXPORT_SYMBOL(__delay);

static void delay_loop(unsigned long delta)
{
	unsigned long end;

	end = get_tod_clock_monotonic() + delta;
	while (!tod_after(get_tod_clock_monotonic(), end))
		cpu_relax();
}

void __udelay(unsigned long usecs)
{
	delay_loop(usecs << 12);
}
EXPORT_SYMBOL(__udelay);

void __ndelay(unsigned long nsecs)
{
	nsecs <<= 9;
	do_div(nsecs, 125);
	delay_loop(nsecs);
}
EXPORT_SYMBOL(__ndelay);
