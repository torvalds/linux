// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2004 Anton Blanchard <anton@au.ibm.com>, IBM
 * Added mmcra[slot] support:
 * Copyright (C) 2006-2007 Will Schmidt <willschm@us.ibm.com>, IBM
 */

#include <linux/oprofile.h>
#include <linux/smp.h>
#include <asm/firmware.h>
#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/cputable.h>
#include <asm/rtas.h>
#include <asm/oprofile_impl.h>
#include <asm/reg.h>

#define dbg(args...)
#define OPROFILE_PM_PMCSEL_MSK      0xffULL
#define OPROFILE_PM_UNIT_SHIFT      60
#define OPROFILE_PM_UNIT_MSK        0xfULL
#define OPROFILE_MAX_PMC_NUM        3
#define OPROFILE_PMSEL_FIELD_WIDTH  8
#define OPROFILE_UNIT_FIELD_WIDTH   4
#define MMCRA_SIAR_VALID_MASK       0x10000000ULL

static unsigned long reset_value[OP_MAX_COUNTER];

static int oprofile_running;
static int use_slot_nums;

/* mmcr values are set in power4_reg_setup, used in power4_cpu_setup */
static u32 mmcr0_val;
static u64 mmcr1_val;
static u64 mmcra_val;
static u32 cntr_marked_events;

static int power7_marked_instr_event(u64 mmcr1)
{
	u64 psel, unit;
	int pmc, cntr_marked_events = 0;

	/* Given the MMCR1 value, look at the field for each counter to
	 * determine if it is a marked event.  Code based on the function
	 * power7_marked_instr_event() in file arch/powerpc/perf/power7-pmu.c.
	 */
	for (pmc = 0; pmc < 4; pmc++) {
		psel = mmcr1 & (OPROFILE_PM_PMCSEL_MSK
				<< (OPROFILE_MAX_PMC_NUM - pmc)
				* OPROFILE_PMSEL_FIELD_WIDTH);
		psel = (psel >> ((OPROFILE_MAX_PMC_NUM - pmc)
				 * OPROFILE_PMSEL_FIELD_WIDTH)) & ~1ULL;
		unit = mmcr1 & (OPROFILE_PM_UNIT_MSK
				<< (OPROFILE_PM_UNIT_SHIFT
				    - (pmc * OPROFILE_PMSEL_FIELD_WIDTH )));
		unit = unit >> (OPROFILE_PM_UNIT_SHIFT
				- (pmc * OPROFILE_PMSEL_FIELD_WIDTH));

		switch (psel >> 4) {
		case 2:
			cntr_marked_events |= (pmc == 1 || pmc == 3) << pmc;
			break;
		case 3:
			if (psel == 0x3c) {
				cntr_marked_events |= (pmc == 0) << pmc;
				break;
			}

			if (psel == 0x3e) {
				cntr_marked_events |= (pmc != 1) << pmc;
				break;
			}

			cntr_marked_events |= 1 << pmc;
			break;
		case 4:
		case 5:
			cntr_marked_events |= (unit == 0xd) << pmc;
			break;
		case 6:
			if (psel == 0x64)
				cntr_marked_events |= (pmc >= 2) << pmc;
			break;
		case 8:
			cntr_marked_events |= (unit == 0xd) << pmc;
			break;
		}
	}
	return cntr_marked_events;
}

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

	/* Power 7+ and newer architectures:
	 * Determine which counter events in the group (the group of events is
	 * specified by the bit settings in the MMCR1 register) are marked
	 * events for use in the interrupt handler.  Do the calculation once
	 * before OProfile starts.  Information is used in the interrupt
	 * handler.  Starting with Power 7+ we only record the sample for
	 * marked events if the SIAR valid bit is set.  For non marked events
	 * the sample is always recorded.
	 */
	if (pvr_version_is(PVR_POWER7p))
		cntr_marked_events = power7_marked_instr_event(mmcr1_val);
	else
		cntr_marked_events = 0; /* For older processors, set the bit map
					 * to zero so the sample will always be
					 * be recorded.
					 */

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

	if (pvr_version_is(PVR_POWER4) || pvr_version_is(PVR_POWER4p) ||
	    pvr_version_is(PVR_970) || pvr_version_is(PVR_970FX) ||
	    pvr_version_is(PVR_970MP) || pvr_version_is(PVR_970GX) ||
	    pvr_version_is(PVR_POWER5) || pvr_version_is(PVR_POWER5p))
		use_slot_nums = 1;

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
	if (pvr_version_is(PVR_POWER4) || pvr_version_is(PVR_POWER4p) ||
	    pvr_version_is(PVR_970) || pvr_version_is(PVR_970FX) ||
	    pvr_version_is(PVR_970MP) || pvr_version_is(PVR_970GX))
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
	mtmsr(mfmsr() | MSR_PMM);

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

	/* Can't do much about it */
	if (!cur_cpu_spec->oprofile_mmcra_sihv)
		return pc;

	mmcra = mfspr(SPRN_MMCRA);

	if (use_slot_nums && (mmcra & MMCRA_SAMPLE_ENABLE)) {
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

static bool pmc_overflow(unsigned long val)
{
	if ((int)val < 0)
		return true;

	/*
	 * Events on POWER7 can roll back if a speculative event doesn't
	 * eventually complete. Unfortunately in some rare cases they will
	 * raise a performance monitor exception. We need to catch this to
	 * ensure we reset the PMC. In all cases the PMC will be 256 or less
	 * cycles from overflow.
	 *
	 * We only do this if the first pass fails to find any overflowing
	 * PMCs because a user might set a period of less than 256 and we
	 * don't want to mistakenly reset them.
	 */
	if (pvr_version_is(PVR_POWER7) && ((0x80000000 - val) <= 256))
		return true;

	return false;
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
	bool siar_valid = false;

	mmcra = mfspr(SPRN_MMCRA);

	pc = get_pc(regs);
	is_kernel = get_kernel(pc, mmcra);

	/* set the PMM bit (see comment below) */
	mtmsr(mfmsr() | MSR_PMM);

	/* Check that the SIAR  valid bit in MMCRA is set to 1. */
	if ((mmcra & MMCRA_SIAR_VALID_MASK) == MMCRA_SIAR_VALID_MASK)
		siar_valid = true;

	for (i = 0; i < cur_cpu_spec->num_pmcs; ++i) {
		val = classic_ctr_read(i);
		if (pmc_overflow(val)) {
			if (oprofile_running && ctr[i].enabled) {
				/* Power 7+ and newer architectures:
				 * If the event is a marked event, then only
				 * save the sample if the SIAR valid bit is
				 * set.  If the event is not marked, then
				 * always save the sample.
				 * Note, the Sample enable bit in the MMCRA
				 * register must be set to 1 if the group
				 * contains a marked event.
				 */
				if ((siar_valid &&
				     (cntr_marked_events & (1 << i)))
				    || !(cntr_marked_events & (1 << i)))
					oprofile_add_ext_sample(pc, regs, i,
								is_kernel);

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
