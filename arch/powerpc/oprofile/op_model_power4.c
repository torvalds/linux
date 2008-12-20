/*
 * Copyright (C) 2004 Anton Blanchard <anton@au.ibm.com>, IBM
 * Added mmcra[slot] support:
 * Copyright (C) 2006-2007 Will Schmidt <willschm@us.ibm.com>, IBM
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/oprofile.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <asm/firmware.h>
#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/processor.h>
#include <asm/cputable.h>
#include <asm/rtas.h>
#include <asm/oprofile_impl.h>
#include <asm/reg.h>

#define dbg(args...)

static unsigned long reset_value[OP_MAX_COUNTER];

static int oprofile_running;

/* mmcr values are set in power4_reg_setup, used in power4_cpu_setup */
static u32 mmcr0_val;
static u64 mmcr1_val;
static u64 mmcra_val;

static int power4_reg_setup(struct op_counter_config *ctr,
			     struct op_system_config *sys,
			     int num_ctrs)
{
	int i;

	/*
	 * The performance counter event settings are given in the mmcr0,
	 * mmcr1 and mmcra values passed from the user in the
	 * op_system_config structure (sys variable).
	 */
	mmcr0_val = sys->mmcr0;
	mmcr1_val = sys->mmcr1;
	mmcra_val = sys->mmcra;

	for (i = 0; i < cur_cpu_spec->num_pmcs; ++i)
		reset_value[i] = 0x80000000UL - ctr[i].count;

	/* setup user and kernel profiling */
	if (sys->enable_kernel)
		mmcr0_val &= ~MMCR0_KERNEL_DISABLE;
	else
		mmcr0_val |= MMCR0_KERNEL_DISABLE;

	if (sys->enable_user)
		mmcr0_val &= ~MMCR0_PROBLEM_DISABLE;
	else
		mmcr0_val |= MMCR0_PROBLEM_DISABLE;

	return 0;
}

extern void ppc_enable_pmcs(void);

/*
 * Older CPUs require the MMCRA sample bit to be always set, but newer 
 * CPUs only want it set for some groups. Eventually we will remove all
 * knowledge of this bit in the kernel, oprofile userspace should be
 * setting it when required.
 *
 * In order to keep current installations working we force the bit for
 * those older CPUs. Once everyone has updated their oprofile userspace we
 * can remove this hack.
 */
static inline int mmcra_must_set_sample(void)
{
	if (__is_processor(PV_POWER4) || __is_processor(PV_POWER4p) ||
	    __is_processor(PV_970) || __is_processor(PV_970FX) ||
	    __is_processor(PV_970MP) || __is_processor(PV_970GX))
		return 1;

	return 0;
}

static int power4_cpu_setup(struct op_counter_config *ctr)
{
	unsigned int mmcr0 = mmcr0_val;
	unsigned long mmcra = mmcra_val;

	ppc_enable_pmcs();

	/* set the freeze bit */
	mmcr0 |= MMCR0_FC;
	mtspr(SPRN_MMCR0, mmcr0);

	mmcr0 |= MMCR0_FCM1|MMCR0_PMXE|MMCR0_FCECE;
	mmcr0 |= MMCR0_PMC1CE|MMCR0_PMCjCE;
	mtspr(SPRN_MMCR0, mmcr0);

	mtspr(SPRN_MMCR1, mmcr1_val);

	if (mmcra_must_set_sample())
		mmcra |= MMCRA_SAMPLE_ENABLE;
	mtspr(SPRN_MMCRA, mmcra);

	dbg("setup on cpu %d, mmcr0 %lx\n", smp_processor_id(),
	    mfspr(SPRN_MMCR0));
	dbg("setup on cpu %d, mmcr1 %lx\n", smp_processor_id(),
	    mfspr(SPRN_MMCR1));
	dbg("setup on cpu %d, mmcra %lx\n", smp_processor_id(),
	    mfspr(SPRN_MMCRA));

	return 0;
}

static int power4_start(struct op_counter_config *ctr)
{
	int i;
	unsigned int mmcr0;

	/* set the PMM bit (see comment below) */
	mtmsrd(mfmsr() | MSR_PMM);

	for (i = 0; i < cur_cpu_spec->num_pmcs; ++i) {
		if (ctr[i].enabled) {
			classic_ctr_write(i, reset_value[i]);
		} else {
			classic_ctr_write(i, 0);
		}
	}

	mmcr0 = mfspr(SPRN_MMCR0);

	/*
	 * We must clear the PMAO bit on some (GQ) chips. Just do it
	 * all the time
	 */
	mmcr0 &= ~MMCR0_PMAO;

	/*
	 * now clear the freeze bit, counting will not start until we
	 * rfid from this excetion, because only at that point will
	 * the PMM bit be cleared
	 */
	mmcr0 &= ~MMCR0_FC;
	mtspr(SPRN_MMCR0, mmcr0);

	oprofile_running = 1;

	dbg("start on cpu %d, mmcr0 %x\n", smp_processor_id(), mmcr0);
	return 0;
}

static void power4_stop(void)
{
	unsigned int mmcr0;

	/* freeze counters */
	mmcr0 = mfspr(SPRN_MMCR0);
	mmcr0 |= MMCR0_FC;
	mtspr(SPRN_MMCR0, mmcr0);

	oprofile_running = 0;

	dbg("stop on cpu %d, mmcr0 %x\n", smp_processor_id(), mmcr0);

	mb();
}

/* Fake functions used by canonicalize_pc */
static void __used hypervisor_bucket(void)
{
}

static void __used rtas_bucket(void)
{
}

static void __used kernel_unknown_bucket(void)
{
}

/*
 * On GQ and newer the MMCRA stores the HV and PR bits at the time
 * the SIAR was sampled. We use that to work out if the SIAR was sampled in
 * the hypervisor, our exception vectors or RTAS.
 * If the MMCRA_SAMPLE_ENABLE bit is set, we can use the MMCRA[slot] bits
 * to more accurately identify the address of the sampled instruction. The
 * mmcra[slot] bits represent the slot number of a sampled instruction
 * within an instruction group.  The slot will contain a value between 1
 * and 5 if MMCRA_SAMPLE_ENABLE is set, otherwise 0.
 */
static unsigned long get_pc(struct pt_regs *regs)
{
	unsigned long pc = mfspr(SPRN_SIAR);
	unsigned long mmcra;
	unsigned long slot;

	/* Cant do much about it */
	if (!cur_cpu_spec->oprofile_mmcra_sihv)
		return pc;

	mmcra = mfspr(SPRN_MMCRA);

	if (mmcra & MMCRA_SAMPLE_ENABLE) {
		slot = ((mmcra & MMCRA_SLOT) >> MMCRA_SLOT_SHIFT);
		if (slot > 1)
			pc += 4 * (slot - 1);
	}

	/* Were we in the hypervisor? */
	if (firmware_has_feature(FW_FEATURE_LPAR) &&
	    (mmcra & cur_cpu_spec->oprofile_mmcra_sihv))
		/* function descriptor madness */
		return *((unsigned long *)hypervisor_bucket);

	/* We were in userspace, nothing to do */
	if (mmcra & cur_cpu_spec->oprofile_mmcra_sipr)
		return pc;

#ifdef CONFIG_PPC_RTAS
	/* Were we in RTAS? */
	if (pc >= rtas.base && pc < (rtas.base + rtas.size))
		/* function descriptor madness */
		return *((unsigned long *)rtas_bucket);
#endif

	/* Were we in our exception vectors or SLB real mode miss handler? */
	if (pc < 0x1000000UL)
		return (unsigned long)__va(pc);

	/* Not sure where we were */
	if (!is_kernel_addr(pc))
		/* function descriptor madness */
		return *((unsigned long *)kernel_unknown_bucket);

	return pc;
}

static int get_kernel(unsigned long pc, unsigned long mmcra)
{
	int is_kernel;

	if (!cur_cpu_spec->oprofile_mmcra_sihv) {
		is_kernel = is_kernel_addr(pc);
	} else {
		is_kernel = ((mmcra & cur_cpu_spec->oprofile_mmcra_sipr) == 0);
	}

	return is_kernel;
}

static void power4_handle_interrupt(struct pt_regs *regs,
				    struct op_counter_config *ctr)
{
	unsigned long pc;
	int is_kernel;
	int val;
	int i;
	unsigned int mmcr0;
	unsigned long mmcra;

	mmcra = mfspr(SPRN_MMCRA);

	pc = get_pc(regs);
	is_kernel = get_kernel(pc, mmcra);

	/* set the PMM bit (see comment below) */
	mtmsrd(mfmsr() | MSR_PMM);

	for (i = 0; i < cur_cpu_spec->num_pmcs; ++i) {
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

	mmcr0 = mfspr(SPRN_MMCR0);

	/* reset the perfmon trigger */
	mmcr0 |= MMCR0_PMXE;

	/*
	 * We must clear the PMAO bit on some (GQ) chips. Just do it
	 * all the time
	 */
	mmcr0 &= ~MMCR0_PMAO;

	/* Clear the appropriate bits in the MMCRA */
	mmcra &= ~cur_cpu_spec->oprofile_mmcra_clear;
	mtspr(SPRN_MMCRA, mmcra);

	/*
	 * now clear the freeze bit, counting will not start until we
	 * rfid from this exception, because only at that point will
	 * the PMM bit be cleared
	 */
	mmcr0 &= ~MMCR0_FC;
	mtspr(SPRN_MMCR0, mmcr0);
}

struct op_powerpc_model op_model_power4 = {
	.reg_setup		= power4_reg_setup,
	.cpu_setup		= power4_cpu_setup,
	.start			= power4_start,
	.stop			= power4_stop,
	.handle_interrupt	= power4_handle_interrupt,
};
