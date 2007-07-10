#include <linux/types.h>
#include <linux/init.h>
#include <linux/kernel_stat.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/mc146818rtc.h>
#include <linux/mipsregs.h>
#include <linux/smp.h>
#include <linux/timex.h>

#include <asm/hardirq.h>
#include <asm/div64.h>
#include <asm/cpu.h>
#include <asm/time.h>
#include <asm/irq.h>
#include <asm/mc146818-time.h>
#include <asm/msc01_ic.h>

#include <asm/mips-boards/generic.h>
#include <asm/mips-boards/prom.h>
#include <asm/mips-boards/simint.h>


unsigned long cpu_khz;

irqreturn_t sim_timer_interrupt(int irq, void *dev_id)
{
#ifdef CONFIG_SMP
	int cpu = smp_processor_id();

	/*
	 * CPU 0 handles the global timer interrupt job
	 * resets count/compare registers to trigger next timer int.
	 */
#ifndef CONFIG_MIPS_MT_SMTC
	if (cpu == 0) {
		timer_interrupt(irq, dev_id);
	} else {
		/* Everyone else needs to reset the timer int here as
		   ll_local_timer_interrupt doesn't */
		/*
		 * FIXME: need to cope with counter underflow.
		 * More support needs to be added to kernel/time for
		 * counter/timer interrupts on multiple CPU's
		 */
		write_c0_compare (read_c0_count() + ( mips_hpt_frequency/HZ));
	}
#else /* SMTC */
	/*
	 *  In SMTC system, one Count/Compare set exists per VPE.
	 *  Which TC within a VPE gets the interrupt is essentially
	 *  random - we only know that it shouldn't be one with
	 *  IXMT set. Whichever TC gets the interrupt needs to
	 *  send special interprocessor interrupts to the other
	 *  TCs to make sure that they schedule, etc.
	 *
	 *  That code is specific to the SMTC kernel, not to
	 *  the simulation platform, so it's invoked from
	 *  the general MIPS timer_interrupt routine.
	 *
	 * We have a problem in that the interrupt vector code
	 * had to turn off the timer IM bit to avoid redundant
	 * entries, but we may never get to mips_cpu_irq_end
	 * to turn it back on again if the scheduler gets
	 * involved.  So we clear the pending timer here,
	 * and re-enable the mask...
	 */

	int vpflags = dvpe();
	write_c0_compare (read_c0_count() - 1);
	clear_c0_cause(0x100 << cp0_compare_irq);
	set_c0_status(0x100 << cp0_compare_irq);
	irq_enable_hazard();
	evpe(vpflags);

	if (cpu_data[cpu].vpe_id == 0)
		timer_interrupt(irq, dev_id);
	else
		write_c0_compare (read_c0_count() + ( mips_hpt_frequency/HZ));
	smtc_timer_broadcast(cpu_data[cpu].vpe_id);

#endif /* CONFIG_MIPS_MT_SMTC */

	/*
	 * every CPU should do profiling and process accounting
	 */
	local_timer_interrupt (irq, dev_id);

	return IRQ_HANDLED;
#else
	return timer_interrupt (irq, dev_id);
#endif
}



/*
 * Estimate CPU frequency.  Sets mips_hpt_frequency as a side-effect
 */
static unsigned int __init estimate_cpu_frequency(void)
{
	unsigned int prid = read_c0_prid() & 0xffff00;
	unsigned int count;

#if 1
	/*
	 * hardwire the board frequency to 12MHz.
	 */

	if ((prid == (PRID_COMP_MIPS | PRID_IMP_20KC)) ||
	    (prid == (PRID_COMP_MIPS | PRID_IMP_25KF)))
		count = 12000000;
	else
		count =  6000000;
#else
	unsigned int flags;

	local_irq_save(flags);

	/* Start counter exactly on falling edge of update flag */
	while (CMOS_READ(RTC_REG_A) & RTC_UIP);
	while (!(CMOS_READ(RTC_REG_A) & RTC_UIP));

	/* Start r4k counter. */
	write_c0_count(0);

	/* Read counter exactly on falling edge of update flag */
	while (CMOS_READ(RTC_REG_A) & RTC_UIP);
	while (!(CMOS_READ(RTC_REG_A) & RTC_UIP));

	count = read_c0_count();

	/* restore interrupts */
	local_irq_restore(flags);
#endif

	mips_hpt_frequency = count;

	if ((prid != (PRID_COMP_MIPS | PRID_IMP_20KC)) &&
	    (prid != (PRID_COMP_MIPS | PRID_IMP_25KF)))
		count *= 2;

	count += 5000;    /* round */
	count -= count%10000;

	return count;
}

void __init sim_time_init(void)
{
	unsigned int est_freq, flags;

	local_irq_save(flags);

	/* Set Data mode - binary. */
	CMOS_WRITE(CMOS_READ(RTC_CONTROL) | RTC_DM_BINARY, RTC_CONTROL);

	est_freq = estimate_cpu_frequency ();

	printk(KERN_INFO "CPU frequency %d.%02d MHz\n", est_freq / 1000000,
	       (est_freq % 1000000) * 100 / 1000000);

	cpu_khz = est_freq / 1000;

	local_irq_restore(flags);
}

static int mips_cpu_timer_irq;

static void mips_timer_dispatch(void)
{
	do_IRQ(mips_cpu_timer_irq);
}


void __init plat_timer_setup(struct irqaction *irq)
{
	if (cpu_has_veic) {
		set_vi_handler(MSC01E_INT_CPUCTR, mips_timer_dispatch);
		mips_cpu_timer_irq = MSC01E_INT_BASE + MSC01E_INT_CPUCTR;
	} else {
		if (cpu_has_vint)
			set_vi_handler(cp0_compare_irq, mips_timer_dispatch);
		mips_cpu_timer_irq = MIPS_CPU_IRQ_BASE + cp0_compare_irq;
	}

	/* we are using the cpu counter for timer interrupts */
	irq->handler = sim_timer_interrupt;
	setup_irq(mips_cpu_timer_irq, irq);

#ifdef CONFIG_SMP
	/* irq_desc(riptor) is a global resource, when the interrupt overlaps
	   on seperate cpu's the first one tries to handle the second interrupt.
	   The effect is that the int remains disabled on the second cpu.
	   Mark the interrupt with IRQ_PER_CPU to avoid any confusion */
	irq_desc[mips_cpu_timer_irq].flags |= IRQ_PER_CPU;
	set_irq_handler(mips_cpu_timer_irq, handle_percpu_irq);
#endif
}
