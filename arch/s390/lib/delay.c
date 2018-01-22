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
#include <linux/irq.h>
#include <asm/vtimer.h>
#include <asm/div64.h>
#include <asm/idle.h>

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

static void __udelay_disabled(unsigned long long usecs)
{
	unsigned long cr0, cr0_new, psw_mask;
	struct s390_idle_data idle;
	u64 end;

	end = get_tod_clock() + (usecs << 12);
	__ctl_store(cr0, 0, 0);
	cr0_new = cr0 & ~CR0_IRQ_SUBCLASS_MASK;
	cr0_new |= (1UL << (63 - 52)); /* enable clock comparator irq */
	__ctl_load(cr0_new, 0, 0);
	psw_mask = __extract_psw() | PSW_MASK_EXT | PSW_MASK_WAIT;
	set_clock_comparator(end);
	set_cpu_flag(CIF_IGNORE_IRQ);
	psw_idle(&idle, psw_mask);
	clear_cpu_flag(CIF_IGNORE_IRQ);
	set_clock_comparator(S390_lowcore.clock_comparator);
	__ctl_load(cr0, 0, 0);
}

static void __udelay_enabled(unsigned long long usecs)
{
	u64 clock_saved, end;

	end = get_tod_clock_fast() + (usecs << 12);
	do {
		clock_saved = 0;
		if (tod_after(S390_lowcore.clock_comparator, end)) {
			clock_saved = local_tick_disable();
			set_clock_comparator(end);
		}
		enabled_wait();
		if (clock_saved)
			local_tick_enable(clock_saved);
	} while (get_tod_clock_fast() < end);
}

/*
 * Waits for 'usecs' microseconds using the TOD clock comparator.
 */
void __udelay(unsigned long long usecs)
{
	unsigned long flags;

	preempt_disable();
	local_irq_save(flags);
	if (in_irq()) {
		__udelay_disabled(usecs);
		goto out;
	}
	if (in_softirq()) {
		if (raw_irqs_disabled_flags(flags))
			__udelay_disabled(usecs);
		else
			__udelay_enabled(usecs);
		goto out;
	}
	if (raw_irqs_disabled_flags(flags)) {
		local_bh_disable();
		__udelay_disabled(usecs);
		_local_bh_enable();
		goto out;
	}
	__udelay_enabled(usecs);
out:
	local_irq_restore(flags);
	preempt_enable();
}
EXPORT_SYMBOL(__udelay);

/*
 * Simple udelay variant. To be used on startup and reboot
 * when the interrupt handler isn't working.
 */
void udelay_simple(unsigned long long usecs)
{
	u64 end;

	end = get_tod_clock_fast() + (usecs << 12);
	while (get_tod_clock_fast() < end)
		cpu_relax();
}

void __ndelay(unsigned long long nsecs)
{
	u64 end;

	nsecs <<= 9;
	do_div(nsecs, 125);
	end = get_tod_clock_fast() + nsecs;
	if (nsecs & ~0xfffUL)
		__udelay(nsecs >> 12);
	while (get_tod_clock_fast() < end)
		barrier();
}
EXPORT_SYMBOL(__ndelay);
