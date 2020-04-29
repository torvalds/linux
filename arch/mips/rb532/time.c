// SPDX-License-Identifier: GPL-2.0-only
/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 1999,2000 MIPS Technologies, Inc.  All rights reserved.
 *
 *  Setting up the clock on the MIPS boards.
 */

#include <linux/init.h>
#include <linux/kernel_stat.h>
#include <linux/ptrace.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/mc146818rtc.h>
#include <linux/irq.h>
#include <linux/timex.h>

#include <asm/mipsregs.h>
#include <asm/time.h>
#include <asm/mach-rc32434/rc32434.h>

extern unsigned int idt_cpu_freq;

/*
 * Figure out the r4k offset, the amount to increment the compare
 * register for each time tick. There is no RTC available.
 *
 * The RC32434 counts at half the CPU *core* speed.
 */
static unsigned long __init cal_r4koff(void)
{
	mips_hpt_frequency = idt_cpu_freq * IDT_CLOCK_MULT / 2;

	return mips_hpt_frequency / HZ;
}

void __init plat_time_init(void)
{
	unsigned int est_freq;
	unsigned long flags, r4k_offset;

	local_irq_save(flags);

	printk(KERN_INFO "calculating r4koff... ");
	r4k_offset = cal_r4koff();
	printk("%08lx(%d)\n", r4k_offset, (int) r4k_offset);

	est_freq = 2 * r4k_offset * HZ;
	est_freq += 5000;	/* round */
	est_freq -= est_freq % 10000;
	printk(KERN_INFO "CPU frequency %d.%02d MHz\n", est_freq / 1000000,
	       (est_freq % 1000000) * 100 / 1000000);
	local_irq_restore(flags);
}
