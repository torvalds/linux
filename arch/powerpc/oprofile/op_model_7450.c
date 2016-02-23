/*
 * arch/powerpc/oprofile/op_model_7450.c
 *
 * Freescale 745x/744x oprofile support, based on fsl_booke support
 * Copyright (C) 2004 Anton Blanchard <anton@au.ibm.com>, IBM
 *
 * Copyright (c) 2004 Freescale Semiconductor, Inc
 *
 * Author: Andy Fleming
 * Maintainer: Kumar Gala <galak@kernel.crashing.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/oprofile.h>
#include <linux/smp.h>
#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/cputable.h>
#include <asm/page.h>
#include <asm/pmc.h>
#include <asm/oprofile_impl.h>

static unsigned long reset_value[OP_MAX_COUNTER];

static int oprofile_running;
static u32 mmcr0_val, mmcr1_val, mmcr2_val, num_pmcs;

#define MMCR0_PMC1_SHIFT	6
#define MMCR0_PMC2_SHIFT	0
#define MMCR1_PMC3_SHIFT	27
#define MMCR1_PMC4_SHIFT	22
#define MMCR1_PMC5_SHIFT	17
#define MMCR1_PMC6_SHIFT	11

#define mmcr0_event1(event) \
	((event << MMCR0_PMC1_SHIFT) & MMCR0_PMC1SEL)
#define mmcr0_event2(event) \
	((event << MMCR0_PMC2_SHIFT) & MMCR0_PMC2SEL)

#define mmcr1_event3(event) \
	((event << MMCR1_PMC3_SHIFT) & MMCR1_PMC3SEL)
#define mmcr1_event4(event) \
	((event << MMCR1_PMC4_SHIFT) & MMCR1_PMC4SEL)
#define mmcr1_event5(event) \
	((event << MMCR1_PMC5_SHIFT) & MMCR1_PMC5SEL)
#define mmcr1_event6(event) \
	((event << MMCR1_PMC6_SHIFT) & MMCR1_PMC6SEL)

#define MMCR0_INIT (MMCR0_FC | MMCR0_FCS | MMCR0_FCP | MMCR0_FCM1 | MMCR0_FCM0)

/* Unfreezes the counters on this CPU, enables the interrupt,
 * enables the counters to trigger the interrupt, and sets the
 * counters to only count when the mark bit is not set.
 */
static void pmc_start_ctrs(void)
{
	u32 mmcr0 = mfspr(SPRN_MMCR0);

	mmcr0 &= ~(MMCR0_FC | MMCR0_FCM0);
	mmcr0 |= (MMCR0_FCECE | MMCR0_PMC1CE | MMCR0_PMCnCE | MMCR0_PMXE);

	mtspr(SPRN_MMCR0, mmcr0);
}

/* Disables the counters on this CPU, and freezes them */
static void pmc_stop_ctrs(void)
{
	u32 mmcr0 = mfspr(SPRN_MMCR0);

	mmcr0 |= MMCR0_FC;
	mmcr0 &= ~(MMCR0_FCECE | MMCR0_PMC1CE | MMCR0_PMCnCE | MMCR0_PMXE);

	mtspr(SPRN_MMCR0, mmcr0);
}

/* Configures the counters on this CPU based on the global
 * settings */
static int fsl7450_cpu_setup(struct op_counter_config *ctr)
{
	/* freeze all counters */
	pmc_stop_ctrs();

	mtspr(SPRN_MMCR0, mmcr0_val);
	mtspr(SPRN_MMCR1, mmcr1_val);
	if (num_pmcs > 4)
		mtspr(SPRN_MMCR2, mmcr2_val);

	return 0;
}

/* Configures the global settings for the countes on all CPUs. */
static int fsl7450_reg_setup(struct op_counter_config *ctr,
			     struct op_system_config *sys,
			     int num_ctrs)
{
	int i;

	num_pmcs = num_ctrs;
	/* Our counters count up, and "count" refers to
	 * how much before the next interrupt, and we interrupt
	 * on overflow.  So we calculate the starting value
	 * which will give us "count" until overflow.
	 * Then we set the events on the enabled counters */
	for (i = 0; i < num_ctrs; ++i)
		reset_value[i] = 0x80000000UL - ctr[i].count;

	/* Set events for Counters 1 & 2 */
	mmcr0_val = MMCR0_INIT | mmcr0_event1(ctr[0].event)
		| mmcr0_event2(ctr[1].event);

	/* Setup user/kernel bits */
	if (sys->enable_kernel)
		mmcr0_val &= ~(MMCR0_FCS);

	if (sys->enable_user)
		mmcr0_val &= ~(MMCR0_FCP);

	/* Set events for Counters 3-6 */
	mmcr1_val = mmcr1_event3(ctr[2].event)
		| mmcr1_event4(ctr[3].event);
	if (num_ctrs > 4)
		mmcr1_val |= mmcr1_event5(ctr[4].event)
			| mmcr1_event6(ctr[5].event);

	mmcr2_val = 0;

	return 0;
}

/* Sets the counters on this CPU to the chosen values, and starts them */
static int fsl7450_start(struct op_counter_config *ctr)
{
	int i;

	mtmsr(mfmsr() | MSR_PMM);

	for (i = 0; i < num_pmcs; ++i) {
		if (ctr[i].enabled)
			classic_ctr_write(i, reset_value[i]);
		else
			classic_ctr_write(i, 0);
	}

	/* Clear the freeze bit, and enable the interrupt.
	 * The counters won't actually start until the rfi clears
	 * the PMM bit */
	pmc_start_ctrs();

	oprofile_running = 1;

	return 0;
}

/* Stop the counters on this CPU */
static void fsl7450_stop(void)
{
	/* freeze counters */
	pmc_stop_ctrs();

	oprofile_running = 0;

	mb();
}


/* Handle the interrupt on this CPU, and log a sample for each
 * event that triggered the interrupt */
static void fsl7450_handle_interrupt(struct pt_regs *regs,
				    struct op_counter_config *ctr)
{
	unsigned long pc;
	int is_kernel;
	int val;
	int i;

	/* set the PMM bit (see comment below) */
	mtmsr(mfmsr() | MSR_PMM);

	pc = mfspr(SPRN_SIAR);
	is_kernel = is_kernel_addr(pc);

	for (i = 0; i < num_pmcs; ++i) {
		val = classic_ctr_read(i);
		if (val < 0) {
			if (oprofile_running && ctr[i].enabled) {
				oprofile_add_ext_sample(pc, regs, i, is_kernel);
				classic_ctr_write(i, reset_value[i]);
			} else {
				classic_ctr_write(i, 0);
			}
		}
	}

	/* The freeze bit was set by the interrupt. */
	/* Clear the freeze bit, and reenable the interrupt.
	 * The counters won't actually start until the rfi clears
	 * the PM/M bit */
	pmc_start_ctrs();
}

struct op_powerpc_model op_model_7450= {
	.reg_setup		= fsl7450_reg_setup,
	.cpu_setup		= fsl7450_cpu_setup,
	.start			= fsl7450_start,
	.stop			= fsl7450_stop,
	.handle_interrupt	= fsl7450_handle_interrupt,
};
