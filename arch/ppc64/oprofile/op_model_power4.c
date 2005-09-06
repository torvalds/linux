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
#include <asm/systemcfg.h>
#include <asm/rtas.h>
#include <asm/oprofile_impl.h>

#define dbg(args...)

static unsigned long reset_value[OP_MAX_COUNTER];

static int oprofile_running;
static int mmcra_has_sihv;

/* mmcr values are set in power4_reg_setup, used in power4_cpu_setup */
static u32 mmcr0_val;
static u64 mmcr1_val;
static u32 mmcra_val;

/*
 * Since we do not have an NMI, backtracing through spinlocks is
 * only a best guess. In light of this, allow it to be disabled at
 * runtime.
 */
static int backtrace_spinlocks;

static void power4_reg_setup(struct op_counter_config *ctr,
			     struct op_system_config *sys,
			     int num_ctrs)
{
	int i;

	/*
	 * SIHV / SIPR bits are only implemented on POWER4+ (GQ) and above.
	 * However we disable it on all POWER4 until we verify it works
	 * (I was seeing some strange behaviour last time I tried).
	 *
	 * It has been verified to work on POWER5 so we enable it there.
	 */
	if (cpu_has_feature(CPU_FTR_MMCRA_SIHV))
		mmcra_has_sihv = 1;

	/*
	 * The performance counter event settings are given in the mmcr0,
	 * mmcr1 and mmcra values passed from the user in the
	 * op_system_config structure (sys variable).
	 */
	mmcr0_val = sys->mmcr0;
	mmcr1_val = sys->mmcr1;
	mmcra_val = sys->mmcra;

	backtrace_spinlocks = sys->backtrace_spinlocks;

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
}

extern void ppc64_enable_pmcs(void);

static void power4_cpu_setup(void *unused)
{
	unsigned int mmcr0 = mmcr0_val;
	unsigned long mmcra = mmcra_val;

	ppc64_enable_pmcs();

	/* set the freeze bit */
	mmcr0 |= MMCR0_FC;
	mtspr(SPRN_MMCR0, mmcr0);

	mmcr0 |= MMCR0_FCM1|MMCR0_PMXE|MMCR0_FCECE;
	mmcr0 |= MMCR0_PMC1CE|MMCR0_PMCjCE;
	mtspr(SPRN_MMCR0, mmcr0);

	mtspr(SPRN_MMCR1, mmcr1_val);

	mmcra |= MMCRA_SAMPLE_ENABLE;
	mtspr(SPRN_MMCRA, mmcra);

	dbg("setup on cpu %d, mmcr0 %lx\n", smp_processor_id(),
	    mfspr(SPRN_MMCR0));
	dbg("setup on cpu %d, mmcr1 %lx\n", smp_processor_id(),
	    mfspr(SPRN_MMCR1));
	dbg("setup on cpu %d, mmcra %lx\n", smp_processor_id(),
	    mfspr(SPRN_MMCRA));
}

static void power4_start(struct op_counter_config *ctr)
{
	int i;
	unsigned int mmcr0;

	/* set the PMM bit (see comment below) */
	mtmsrd(mfmsr() | MSR_PMM);

	for (i = 0; i < cur_cpu_spec->num_pmcs; ++i) {
		if (ctr[i].enabled) {
			ctr_write(i, reset_value[i]);
		} else {
			ctr_write(i, 0);
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
static void __attribute_used__ hypervisor_bucket(void)
{
}

static void __attribute_used__ rtas_bucket(void)
{
}

static void __attribute_used__ kernel_unknown_bucket(void)
{
}

static unsigned long check_spinlock_pc(struct pt_regs *regs,
				       unsigned long profile_pc)
{
	unsigned long pc = instruction_pointer(regs);

	/*
	 * If both the SIAR (sampled instruction) and the perfmon exception
	 * occurred in a spinlock region then we account the sample to the
	 * calling function. This isnt 100% correct, we really need soft
	 * IRQ disable so we always get the perfmon exception at the
	 * point at which the SIAR is set.
	 */
	if (backtrace_spinlocks && in_lock_functions(pc) &&
			in_lock_functions(profile_pc))
		return regs->link;
	else
		return profile_pc;
}

/*
 * On GQ and newer the MMCRA stores the HV and PR bits at the time
 * the SIAR was sampled. We use that to work out if the SIAR was sampled in
 * the hypervisor, our exception vectors or RTAS.
 */
static unsigned long get_pc(struct pt_regs *regs)
{
	unsigned long pc = mfspr(SPRN_SIAR);
	unsigned long mmcra;

	/* Cant do much about it */
	if (!mmcra_has_sihv)
		return check_spinlock_pc(regs, pc);

	mmcra = mfspr(SPRN_MMCRA);

	/* Were we in the hypervisor? */
	if ((systemcfg->platform == PLATFORM_PSERIES_LPAR) &&
	    (mmcra & MMCRA_SIHV))
		/* function descriptor madness */
		return *((unsigned long *)hypervisor_bucket);

	/* We were in userspace, nothing to do */
	if (mmcra & MMCRA_SIPR)
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
	if (pc < KERNELBASE)
		/* function descriptor madness */
		return *((unsigned long *)kernel_unknown_bucket);

	return check_spinlock_pc(regs, pc);
}

static int get_kernel(unsigned long pc)
{
	int is_kernel;

	if (!mmcra_has_sihv) {
		is_kernel = (pc >= KERNELBASE);
	} else {
		unsigned long mmcra = mfspr(SPRN_MMCRA);
		is_kernel = ((mmcra & MMCRA_SIPR) == 0);
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

	pc = get_pc(regs);
	is_kernel = get_kernel(pc);

	/* set the PMM bit (see comment below) */
	mtmsrd(mfmsr() | MSR_PMM);

	for (i = 0; i < cur_cpu_spec->num_pmcs; ++i) {
		val = ctr_read(i);
		if (val < 0) {
			if (oprofile_running && ctr[i].enabled) {
				oprofile_add_pc(pc, is_kernel, i);
				ctr_write(i, reset_value[i]);
			} else {
				ctr_write(i, 0);
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

	/*
	 * now clear the freeze bit, counting will not start until we
	 * rfid from this exception, because only at that point will
	 * the PMM bit be cleared
	 */
	mmcr0 &= ~MMCR0_FC;
	mtspr(SPRN_MMCR0, mmcr0);
}

struct op_ppc64_model op_model_power4 = {
	.reg_setup		= power4_reg_setup,
	.cpu_setup		= power4_cpu_setup,
	.start			= power4_start,
	.stop			= power4_stop,
	.handle_interrupt	= power4_handle_interrupt,
};
