/*
 *	Precise Delay Loops for parisc
 *
 *	based on code by:
 *	Copyright (C) 1993 Linus Torvalds
 *	Copyright (C) 1997 Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 *	Copyright (C) 2008 Jiri Hladky <hladky _dot_ jiri _at_ gmail _dot_ com>
 *
 *	parisc implementation:
 *	Copyright (C) 2013 Helge Deller <deller@gmx.de>
 */


#include <linux/module.h>
#include <linux/preempt.h>
#include <linux/init.h>

#include <asm/processor.h>
#include <asm/delay.h>

#include <asm/special_insns.h>    /* for mfctl() */
#include <asm/processor.h> /* for boot_cpu_data */

/* CR16 based delay: */
static void __cr16_delay(unsigned long __loops)
{
	/*
	 * Note: Due to unsigned math, cr16 rollovers shouldn't be
	 * a problem here. However, on 32 bit, we need to make sure
	 * we don't pass in too big a value. The current default
	 * value of MAX_UDELAY_MS should help prevent this.
	 */
	u32 bclock, now, loops = __loops;
	int cpu;

	preempt_disable();
	cpu = smp_processor_id();
	bclock = mfctl(16);
	for (;;) {
		now = mfctl(16);
		if ((now - bclock) >= loops)
			break;

		/* Allow RT tasks to run */
		preempt_enable();
		asm volatile("	nop\n");
		barrier();
		preempt_disable();

		/*
		 * It is possible that we moved to another CPU, and
		 * since CR16's are per-cpu we need to calculate
		 * that. The delay must guarantee that we wait "at
		 * least" the amount of time. Being moved to another
		 * CPU could make the wait longer but we just need to
		 * make sure we waited long enough. Rebalance the
		 * counter for this CPU.
		 */
		if (unlikely(cpu != smp_processor_id())) {
			loops -= (now - bclock);
			cpu = smp_processor_id();
			bclock = mfctl(16);
		}
	}
	preempt_enable();
}


void __udelay(unsigned long usecs)
{
	__cr16_delay(usecs * ((unsigned long)boot_cpu_data.cpu_hz / 1000000UL));
}
EXPORT_SYMBOL(__udelay);
