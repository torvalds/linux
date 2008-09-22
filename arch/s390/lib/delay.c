/*
 *  arch/s390/lib/delay.c
 *    Precise Delay Loops for S390
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 *
 *  Derived from "arch/i386/lib/delay.c"
 *    Copyright (C) 1993 Linus Torvalds
 *    Copyright (C) 1997 Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 */

#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/timex.h>
#include <linux/irqflags.h>
#include <linux/interrupt.h>

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

/*
 * Waits for 'usecs' microseconds using the TOD clock comparator.
 */
void __udelay(unsigned long usecs)
{
	u64 end, time, old_cc = 0;
	unsigned long flags, cr0, mask, dummy;
	int irq_context;

	irq_context = in_interrupt();
	if (!irq_context)
		local_bh_disable();
	local_irq_save(flags);
	if (raw_irqs_disabled_flags(flags)) {
		old_cc = local_tick_disable();
		S390_lowcore.clock_comparator = -1ULL;
		__ctl_store(cr0, 0, 0);
		dummy = (cr0 & 0xffff00e0) | 0x00000800;
		__ctl_load(dummy , 0, 0);
		mask = psw_kernel_bits | PSW_MASK_WAIT | PSW_MASK_EXT;
	} else
		mask = psw_kernel_bits | PSW_MASK_WAIT |
			PSW_MASK_EXT | PSW_MASK_IO;

	end = get_clock() + ((u64) usecs << 12);
	do {
		time = end < S390_lowcore.clock_comparator ?
			end : S390_lowcore.clock_comparator;
		set_clock_comparator(time);
		trace_hardirqs_on();
		__load_psw_mask(mask);
		local_irq_disable();
	} while (get_clock() < end);

	if (raw_irqs_disabled_flags(flags)) {
		__ctl_load(cr0, 0, 0);
		local_tick_enable(old_cc);
	}
	if (!irq_context)
		_local_bh_enable();
	set_clock_comparator(S390_lowcore.clock_comparator);
	local_irq_restore(flags);
}
