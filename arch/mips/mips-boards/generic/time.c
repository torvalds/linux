/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 1999,2000 MIPS Technologies, Inc.  All rights reserved.
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Setting up the clock on the MIPS boards.
 */

#include <linux/types.h>
#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel_stat.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/timex.h>
#include <linux/mc146818rtc.h>

#include <asm/mipsregs.h>
#include <asm/ptrace.h>
#include <asm/div64.h>
#include <asm/cpu.h>
#include <asm/time.h>
#include <asm/mc146818-time.h>

#include <asm/mips-boards/generic.h>
#include <asm/mips-boards/prom.h>

unsigned long cpu_khz;

#if defined(CONFIG_MIPS_SEAD)
#define ALLINTS (IE_IRQ0 | IE_IRQ1 | IE_IRQ5)
#else
#define ALLINTS (IE_IRQ0 | IE_IRQ1 | IE_IRQ2 | IE_IRQ3 | IE_IRQ4 | IE_IRQ5)
#endif

#if defined(CONFIG_MIPS_ATLAS)
static char display_string[] = "        LINUX ON ATLAS       ";
#endif
#if defined(CONFIG_MIPS_MALTA)
static char display_string[] = "        LINUX ON MALTA       ";
#endif
#if defined(CONFIG_MIPS_SEAD)
static char display_string[] = "        LINUX ON SEAD       ";
#endif
static unsigned int display_count = 0;
#define MAX_DISPLAY_COUNT (sizeof(display_string) - 8)

#define MIPS_CPU_TIMER_IRQ (NR_IRQS-1)

static unsigned int timer_tick_count=0;

void mips_timer_interrupt(struct pt_regs *regs)
{
	if ((timer_tick_count++ % HZ) == 0) {
		mips_display_message(&display_string[display_count++]);
		if (display_count == MAX_DISPLAY_COUNT)
		        display_count = 0;

	}

	ll_timer_interrupt(MIPS_CPU_TIMER_IRQ, regs);
}

/*
 * Estimate CPU frequency.  Sets mips_counter_frequency as a side-effect
 */
static unsigned int __init estimate_cpu_frequency(void)
{
	unsigned int prid = read_c0_prid() & 0xffff00;
	unsigned int count;

#ifdef CONFIG_MIPS_SEAD
	/*
	 * The SEAD board doesn't have a real time clock, so we can't
	 * really calculate the timer frequency
	 * For now we hardwire the SEAD board frequency to 12MHz.
	 */
	
	if ((prid == (PRID_COMP_MIPS | PRID_IMP_20KC)) ||
	    (prid == (PRID_COMP_MIPS | PRID_IMP_25KF)))
		count = 12000000;
	else
		count = 6000000;
#endif
#if defined(CONFIG_MIPS_ATLAS) || defined(CONFIG_MIPS_MALTA)
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

unsigned long __init mips_rtc_get_time(void)
{
	return mc146818_get_cmos_time();
}

void __init mips_time_init(void)
{
	unsigned int est_freq, flags;

	local_irq_save(flags);

#if defined(CONFIG_MIPS_ATLAS) || defined(CONFIG_MIPS_MALTA)
        /* Set Data mode - binary. */
        CMOS_WRITE(CMOS_READ(RTC_CONTROL) | RTC_DM_BINARY, RTC_CONTROL);
#endif

	est_freq = estimate_cpu_frequency ();

	printk("CPU frequency %d.%02d MHz\n", est_freq/1000000,
	       (est_freq%1000000)*100/1000000);

        cpu_khz = est_freq / 1000;

	local_irq_restore(flags);
}

void __init mips_timer_setup(struct irqaction *irq)
{
	/* we are using the cpu counter for timer interrupts */
	irq->handler = no_action;     /* we use our own handler */
	setup_irq(MIPS_CPU_TIMER_IRQ, irq);

        /* to generate the first timer interrupt */
	write_c0_compare (read_c0_count() + mips_hpt_frequency/HZ);
	set_c0_status(ALLINTS);
}
