// SPDX-License-Identifier: GPL-2.0
/*
 *    Precise Delay Loops for S390
 *
 *    Copyright IBM Corp. 1999, 2008
 *    Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>,
 *		 Heiko Carstens <heiko.carstens@de.ibm.com>,
 */

#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/timex.h>
#include <linux/export.h>
#include <linux/irqflags.h>
#include <linux/interrupt.h>
#include <linux/jump_label.h>
#include <linux/irq.h>
#include <asm/vtimer.h>
#include <asm/div64.h>
#include <asm/idle.h>

static DEFINE_STATIC_KEY_FALSE(udelay_ready);

void __init udelay_enable(void)
{
	static_branch_enable(&udelay_ready);
}

void __delay(unsigned long loops)
{
        /*
         * To end the bloody studid and useless discussion about the
         * BogoMips number I took the liberty to define the __delay
         * function in a way that that resulting BogoMips number will
         * yield the megahertz number of the cpu. The important function
         * is udelay and that is done using the tod clock. -- martin.
         */
	asm volatile("0: brct %0,0b" : : "d" ((loops/2) + 1));
}
EXPORT_SYMBOL(__delay);

static void delay_loop(unsigned long delta, bool simple)
{
	unsigned long end;

	if (static_branch_likely(&udelay_ready) && !simple) {
		end = get_tod_clock_monotonic() + delta;
		while (!tod_after(get_tod_clock_monotonic(), end))
			cpu_relax();
	} else {
		end = get_tod_clock() + delta;
		while (!tod_after(get_tod_clock(), end))
			cpu_relax();
	}
}

void __udelay(unsigned long usecs)
{
	delay_loop(usecs << 12, 0);
}
EXPORT_SYMBOL(__udelay);

/*
 * Simple udelay variant. To be used on startup and reboot
 * when the interrupt handler isn't working.
 */
void udelay_simple(unsigned long usecs)
{
	delay_loop(usecs << 12, 1);
}

void __ndelay(unsigned long nsecs)
{
	nsecs <<= 9;
	do_div(nsecs, 125);
	delay_loop(nsecs, 0);
}
EXPORT_SYMBOL(__ndelay);
