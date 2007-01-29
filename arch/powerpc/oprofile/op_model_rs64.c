/*
 * Copyright (C) 2004 Anton Blanchard <anton@au.ibm.com>, IBM
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
#include <asm/oprofile_impl.h>

#define dbg(args...)

static void ctrl_write(unsigned int i, unsigned int val)
{
	unsigned int tmp = 0;
	unsigned long shift = 0, mask = 0;

	dbg("ctrl_write %d %x\n", i, val);

	switch(i) {
	case 0:
		tmp = mfspr(SPRN_MMCR0);
		shift = 6;
		mask = 0x7F;
		break;
	case 1:
		tmp = mfspr(SPRN_MMCR0);
		shift = 0;
		mask = 0x3F;
		break;
	case 2:
		tmp = mfspr(SPRN_MMCR1);
		shift = 31 - 4;
		mask = 0x1F;
		break;
	case 3:
		tmp = mfspr(SPRN_MMCR1);
		shift = 31 - 9;
		mask = 0x1F;
		break;
	case 4:
		tmp = mfspr(SPRN_MMCR1);
		shift = 31 - 14;
		mask = 0x1F;
		break;
	case 5:
		tmp = mfspr(SPRN_MMCR1);
		shift = 31 - 19;
		mask = 0x1F;
		break;
	case 6:
		tmp = mfspr(SPRN_MMCR1);
		shift = 31 - 24;
		mask = 0x1F;
		break;
	case 7:
		tmp = mfspr(SPRN_MMCR1);
		shift = 31 - 28;
		mask = 0xF;
		break;
	}

	tmp = tmp & ~(mask << shift);
	tmp |= val << shift;

	switch(i) {
		case 0:
		case 1:
			mtspr(SPRN_MMCR0, tmp);
			break;
		default:
			mtspr(SPRN_MMCR1, tmp);
	}

	dbg("ctrl_write mmcr0 %lx mmcr1 %lx\n", mfspr(SPRN_MMCR0),
	       mfspr(SPRN_MMCR1));
}

static unsigned long reset_value[OP_MAX_COUNTER];

static int num_counters;

static void rs64_reg_setup(struct op_counter_config *ctr,
			   struct op_system_config *sys,
			   int num_ctrs)
{
	int i;

	num_counters = num_ctrs;

	for (i = 0; i < num_counters; ++i)
		reset_value[i] = 0x80000000UL - ctr[i].count;

	/* XXX setup user and kernel profiling */
}

static void rs64_cpu_setup(struct op_counter_config *ctr)
{
	unsigned int mmcr0;

	/* reset MMCR0 and set the freeze bit */
	mmcr0 = MMCR0_FC;
	mtspr(SPRN_MMCR0, mmcr0);

	/* reset MMCR1, MMCRA */
	mtspr(SPRN_MMCR1, 0);

	if (cpu_has_feature(CPU_FTR_MMCRA))
		mtspr(SPRN_MMCRA, 0);

	mmcr0 |= MMCR0_FCM1|MMCR0_PMXE|MMCR0_FCECE;
	/* Only applies to POWER3, but should be safe on RS64 */
	mmcr0 |= MMCR0_PMC1CE|MMCR0_PMCjCE;
	mtspr(SPRN_MMCR0, mmcr0);

	dbg("setup on cpu %d, mmcr0 %lx\n", smp_processor_id(),
	    mfspr(SPRN_MMCR0));
	dbg("setup on cpu %d, mmcr1 %lx\n", smp_processor_id(),
	    mfspr(SPRN_MMCR1));
}

static void rs64_start(struct op_counter_config *ctr)
{
	int i;
	unsigned int mmcr0;

	/* set the PMM bit (see comment below) */
	mtmsrd(mfmsr() | MSR_PMM);

	for (i = 0; i < num_counters; ++i) {
		if (ctr[i].enabled) {
			classic_ctr_write(i, reset_value[i]);
			ctrl_write(i, ctr[i].event);
		} else {
			classic_ctr_write(i, 0);
		}
	}

	mmcr0 = mfspr(SPRN_MMCR0);

	/*
	 * now clear the freeze bit, counting will not start until we
	 * rfid from this excetion, because only at that point will
	 * the PMM bit be cleared
	 */
	mmcr0 &= ~MMCR0_FC;
	mtspr(SPRN_MMCR0, mmcr0);

	dbg("start on cpu %d, mmcr0 %x\n", smp_processor_id(), mmcr0);
}

static void rs64_stop(void)
{
	unsigned int mmcr0;

	/* freeze counters */
	mmcr0 = mfspr(SPRN_MMCR0);
	mmcr0 |= MMCR0_FC;
	mtspr(SPRN_MMCR0, mmcr0);

	dbg("stop on cpu %d, mmcr0 %x\n", smp_processor_id(), mmcr0);

	mb();
}

static void rs64_handle_interrupt(struct pt_regs *regs,
				  struct op_counter_config *ctr)
{
	unsigned int mmcr0;
	int is_kernel;
	int val;
	int i;
	unsigned long pc = mfspr(SPRN_SIAR);

	is_kernel = is_kernel_addr(pc);

	/* set the PMM bit (see comment below) */
	mtmsrd(mfmsr() | MSR_PMM);

	for (i = 0; i < num_counters; ++i) {
		val = classic_ctr_read(i);
		if (val < 0) {
			if (ctr[i].enabled) {
				oprofile_add_ext_sample(pc, regs, i, is_kernel);
				classic_ctr_write(i, reset_value[i]);
			} else {
				classic_ctr_write(i, 0);
			}
		}
	}

	mmcr0 = mfspr(SPRN_MMCR0);

	/* reset the perfmon trigger */
	mmcr0 |= MMCR0_PMXE;

	/*
	 * now clear the freeze bit, counting will not start until we
	 * rfid from this exception, because only at that point will
	 * the PMM bit be cleared
	 */
	mmcr0 &= ~MMCR0_FC;
	mtspr(SPRN_MMCR0, mmcr0);
}

struct op_powerpc_model op_model_rs64 = {
	.reg_setup		= rs64_reg_setup,
	.cpu_setup		= rs64_cpu_setup,
	.start			= rs64_start,
	.stop			= rs64_stop,
	.handle_interrupt	= rs64_handle_interrupt,
};
