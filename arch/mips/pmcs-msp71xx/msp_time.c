// SPDX-License-Identifier: GPL-2.0-only
/*
 * Setting up the clock on MSP SOCs.  No RTC typically.
 *
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 1999,2000 MIPS Technologies, Inc.  All rights reserved.
 *
 * ########################################################################
 *
 * ########################################################################
 */

#include <linux/init.h>
#include <linux/kernel_stat.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/ptrace.h>

#include <asm/cevt-r4k.h>
#include <asm/mipsregs.h>
#include <asm/time.h>

#include <msp_prom.h>
#include <msp_int.h>
#include <msp_regs.h>

#define get_current_vpe()   \
	((read_c0_tcbind() >> TCBIND_CURVPE_SHIFT) & TCBIND_CURVPE)

static int tim_installed;

void __init plat_time_init(void)
{
	char	*endp, *s;
	unsigned long cpu_rate = 0;

	if (cpu_rate == 0) {
		s = prom_getenv("clkfreqhz");
		cpu_rate = simple_strtoul(s, &endp, 10);
		if (endp != NULL && *endp != 0) {
			printk(KERN_ERR
				"Clock rate in Hz parse error: %s\n", s);
			cpu_rate = 0;
		}
	}

	if (cpu_rate == 0) {
		s = prom_getenv("clkfreq");
		cpu_rate = 1000 * simple_strtoul(s, &endp, 10);
		if (endp != NULL && *endp != 0) {
			printk(KERN_ERR
				"Clock rate in MHz parse error: %s\n", s);
			cpu_rate = 0;
		}
	}

	if (cpu_rate == 0) {
#if defined(CONFIG_PMC_MSP7120_EVAL) \
 || defined(CONFIG_PMC_MSP7120_GW)
		cpu_rate = 400000000;
#elif defined(CONFIG_PMC_MSP7120_FPGA)
		cpu_rate = 25000000;
#else
		cpu_rate = 150000000;
#endif
		printk(KERN_ERR
			"Failed to determine CPU clock rate, "
			"assuming %ld hz ...\n", cpu_rate);
	}

	printk(KERN_WARNING "Clock rate set to %ld\n", cpu_rate);

	/* timer frequency is 1/2 clock rate */
	mips_hpt_frequency = cpu_rate/2;
}

unsigned int get_c0_compare_int(void)
{
	unsigned long flags = IRQF_PERCPU | IRQF_TIMER | IRQF_SHARED;

	/* MIPS_MT modes may want timer for second VPE */
	if ((get_current_vpe()) && !tim_installed) {
		if (request_irq(MSP_INT_VPE1_TIMER, c0_compare_interrupt, flags,
				"timer", c0_compare_interrupt))
			pr_err("Failed to register timer interrupt\n");
		tim_installed++;
	}

	return get_current_vpe() ? MSP_INT_VPE1_TIMER : MSP_INT_VPE0_TIMER;
}
