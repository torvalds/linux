/*
 * time.c: MIPS CPU Count/Compare timer hookup
 *
 * Author: Mark.Zhan, <rongkai.zhan@windriver.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 1997, 2004 by Ralf Baechle (ralf@linux-mips.org)
 * Copyright (C) 2006, Wind River System Inc.
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/param.h>	/* for HZ */
#include <linux/irq.h>
#include <linux/timex.h>
#include <linux/interrupt.h>

#include <asm/reboot.h>
#include <asm/time.h>
#include <asm/io.h>
#include <asm/bootinfo.h>
#include <asm/gt64120.h>

#define WRPPMC_CPU_CLK_FREQ 40000000 /* 40MHZ */

void __init wrppmc_timer_setup(struct irqaction *irq)
{
	/* Install ISR for timer interrupt */
	setup_irq(WRPPMC_MIPS_TIMER_IRQ, irq);

	/* to generate the first timer interrupt */
	write_c0_compare(mips_hpt_frequency/HZ);
	write_c0_count(0);
}

/*
 * Estimate CPU frequency.  Sets mips_hpt_frequency as a side-effect
 *
 * NOTE: We disable all GT64120 timers, and use MIPS processor internal
 * timer as the source of kernel clock tick.
 */
void __init wrppmc_time_init(void)
{
	/* Disable GT64120 timers */
	GT_WRITE(GT_TC_CONTROL_OFS, 0x00);
	GT_WRITE(GT_TC0_OFS, 0x00);
	GT_WRITE(GT_TC1_OFS, 0x00);
	GT_WRITE(GT_TC2_OFS, 0x00);
	GT_WRITE(GT_TC3_OFS, 0x00);

	/* Use MIPS compare/count internal timer */
	mips_hpt_frequency = WRPPMC_CPU_CLK_FREQ;
}
