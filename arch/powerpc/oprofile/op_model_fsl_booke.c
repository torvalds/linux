/*
 * oprofile/op_model_e500.c
 *
 * Freescale Book-E oprofile support, based on ppc64 oprofile support
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
#include <linux/init.h>
#include <linux/smp.h>
#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/processor.h>
#include <asm/cputable.h>
#include <asm/reg_booke.h>
#include <asm/page.h>
#include <asm/pmc.h>
#include <asm/oprofile_impl.h>

static unsigned long reset_value[OP_MAX_COUNTER];

static int num_counters;
static int oprofile_running;

static inline unsigned int ctr_read(unsigned int i)
{
	switch(i) {
		case 0:
			return mfpmr(PMRN_PMC0);
		case 1:
			return mfpmr(PMRN_PMC1);
		case 2:
			return mfpmr(PMRN_PMC2);
		case 3:
			return mfpmr(PMRN_PMC3);
		default:
			return 0;
	}
}

static inline void ctr_write(unsigned int i, unsigned int val)
{
	switch(i) {
		case 0:
			mtpmr(PMRN_PMC0, val);
			break;
		case 1:
			mtpmr(PMRN_PMC1, val);
			break;
		case 2:
			mtpmr(PMRN_PMC2, val);
			break;
		case 3:
			mtpmr(PMRN_PMC3, val);
			break;
		default:
			break;
	}
}


static void fsl_booke_reg_setup(struct op_counter_config *ctr,
			     struct op_system_config *sys,
			     int num_ctrs)
{
	int i;

	num_counters = num_ctrs;

	/* freeze all counters */
	pmc_stop_ctrs();

	/* Our counters count up, and "count" refers to
	 * how much before the next interrupt, and we interrupt
	 * on overflow.  So we calculate the starting value
	 * which will give us "count" until overflow.
	 * Then we set the events on the enabled counters */
	for (i = 0; i < num_counters; ++i) {
		reset_value[i] = 0x80000000UL - ctr[i].count;

		init_pmc_stop(i);

		set_pmc_event(i, ctr[i].event);

		set_pmc_user_kernel(i, ctr[i].user, ctr[i].kernel);
	}
}

static void fsl_booke_start(struct op_counter_config *ctr)
{
	int i;

	mtmsr(mfmsr() | MSR_PMM);

	for (i = 0; i < num_counters; ++i) {
		if (ctr[i].enabled) {
			ctr_write(i, reset_value[i]);
			/* Set Each enabled counterd to only
			 * count when the Mark bit is not set */
			set_pmc_marked(i, 1, 0);
			pmc_start_ctr(i, 1);
		} else {
			ctr_write(i, 0);

			/* Set the ctr to be stopped */
			pmc_start_ctr(i, 0);
		}
	}

	/* Clear the freeze bit, and enable the interrupt.
	 * The counters won't actually start until the rfi clears
	 * the PMM bit */
	pmc_start_ctrs(1);

	oprofile_running = 1;

	pr_debug("start on cpu %d, pmgc0 %x\n", smp_processor_id(),
			mfpmr(PMRN_PMGC0));
}

static void fsl_booke_stop(void)
{
	/* freeze counters */
	pmc_stop_ctrs();

	oprofile_running = 0;

	pr_debug("stop on cpu %d, pmgc0 %x\n", smp_processor_id(),
			mfpmr(PMRN_PMGC0));

	mb();
}


static void fsl_booke_handle_interrupt(struct pt_regs *regs,
				    struct op_counter_config *ctr)
{
	unsigned long pc;
	int is_kernel;
	int val;
	int i;

	/* set the PMM bit (see comment below) */
	mtmsr(mfmsr() | MSR_PMM);

	pc = regs->nip;
	is_kernel = is_kernel_addr(pc);

	for (i = 0; i < num_counters; ++i) {
		val = ctr_read(i);
		if (val < 0) {
			if (oprofile_running && ctr[i].enabled) {
				oprofile_add_ext_sample(pc, regs, i, is_kernel);
				ctr_write(i, reset_value[i]);
			} else {
				ctr_write(i, 0);
			}
		}
	}

	/* The freeze bit was set by the interrupt. */
	/* Clear the freeze bit, and reenable the interrupt.
	 * The counters won't actually start until the rfi clears
	 * the PMM bit */
	pmc_start_ctrs(1);
}

struct op_powerpc_model op_model_fsl_booke = {
	.reg_setup		= fsl_booke_reg_setup,
	.start			= fsl_booke_start,
	.stop			= fsl_booke_stop,
	.handle_interrupt	= fsl_booke_handle_interrupt,
};
