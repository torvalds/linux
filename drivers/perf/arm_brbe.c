// SPDX-License-Identifier: GPL-2.0-only
/*
 * Branch Record Buffer Extension Driver.
 *
 * Copyright (C) 2022-2025 ARM Limited
 *
 * Author: Anshuman Khandual <anshuman.khandual@arm.com>
 */
#include <linux/types.h>
#include <linux/bitmap.h>
#include <linux/perf/arm_pmu.h>
#include "arm_brbe.h"

#define BRBFCR_EL1_BRANCH_FILTERS (BRBFCR_EL1_DIRECT   | \
				   BRBFCR_EL1_INDIRECT | \
				   BRBFCR_EL1_RTN      | \
				   BRBFCR_EL1_INDCALL  | \
				   BRBFCR_EL1_DIRCALL  | \
				   BRBFCR_EL1_CONDDIR)

/*
 * BRBTS_EL1 is currently not used for branch stack implementation
 * purpose but BRBCR_ELx.TS needs to have a valid value from all
 * available options. BRBCR_ELx_TS_VIRTUAL is selected for this.
 */
#define BRBCR_ELx_DEFAULT_TS      FIELD_PREP(BRBCR_ELx_TS_MASK, BRBCR_ELx_TS_VIRTUAL)

/*
 * BRBE Buffer Organization
 *
 * BRBE buffer is arranged as multiple banks of 32 branch record
 * entries each. An individual branch record in a given bank could
 * be accessed, after selecting the bank in BRBFCR_EL1.BANK and
 * accessing the registers i.e [BRBSRC, BRBTGT, BRBINF] set with
 * indices [0..31].
 *
 * Bank 0
 *
 *	---------------------------------	------
 *	| 00 | BRBSRC | BRBTGT | BRBINF |	| 00 |
 *	---------------------------------	------
 *	| 01 | BRBSRC | BRBTGT | BRBINF |	| 01 |
 *	---------------------------------	------
 *	| .. | BRBSRC | BRBTGT | BRBINF |	| .. |
 *	---------------------------------	------
 *	| 31 | BRBSRC | BRBTGT | BRBINF |	| 31 |
 *	---------------------------------	------
 *
 * Bank 1
 *
 *	---------------------------------	------
 *	| 32 | BRBSRC | BRBTGT | BRBINF |	| 00 |
 *	---------------------------------	------
 *	| 33 | BRBSRC | BRBTGT | BRBINF |	| 01 |
 *	---------------------------------	------
 *	| .. | BRBSRC | BRBTGT | BRBINF |	| .. |
 *	---------------------------------	------
 *	| 63 | BRBSRC | BRBTGT | BRBINF |	| 31 |
 *	---------------------------------	------
 */
#define BRBE_BANK_MAX_ENTRIES	32

struct brbe_regset {
	u64 brbsrc;
	u64 brbtgt;
	u64 brbinf;
};

#define PERF_BR_ARM64_MAX (PERF_BR_MAX + PERF_BR_NEW_MAX)

struct brbe_hw_attr {
	int	brbe_version;
	int	brbe_cc;
	int	brbe_nr;
	int	brbe_format;
};

#define BRBE_REGN_CASE(n, case_macro) \
	case n: case_macro(n); break

#define BRBE_REGN_SWITCH(x, case_macro)				\
	do {							\
		switch (x) {					\
		BRBE_REGN_CASE(0, case_macro);			\
		BRBE_REGN_CASE(1, case_macro);			\
		BRBE_REGN_CASE(2, case_macro);			\
		BRBE_REGN_CASE(3, case_macro);			\
		BRBE_REGN_CASE(4, case_macro);			\
		BRBE_REGN_CASE(5, case_macro);			\
		BRBE_REGN_CASE(6, case_macro);			\
		BRBE_REGN_CASE(7, case_macro);			\
		BRBE_REGN_CASE(8, case_macro);			\
		BRBE_REGN_CASE(9, case_macro);			\
		BRBE_REGN_CASE(10, case_macro);			\
		BRBE_REGN_CASE(11, case_macro);			\
		BRBE_REGN_CASE(12, case_macro);			\
		BRBE_REGN_CASE(13, case_macro);			\
		BRBE_REGN_CASE(14, case_macro);			\
		BRBE_REGN_CASE(15, case_macro);			\
		BRBE_REGN_CASE(16, case_macro);			\
		BRBE_REGN_CASE(17, case_macro);			\
		BRBE_REGN_CASE(18, case_macro);			\
		BRBE_REGN_CASE(19, case_macro);			\
		BRBE_REGN_CASE(20, case_macro);			\
		BRBE_REGN_CASE(21, case_macro);			\
		BRBE_REGN_CASE(22, case_macro);			\
		BRBE_REGN_CASE(23, case_macro);			\
		BRBE_REGN_CASE(24, case_macro);			\
		BRBE_REGN_CASE(25, case_macro);			\
		BRBE_REGN_CASE(26, case_macro);			\
		BRBE_REGN_CASE(27, case_macro);			\
		BRBE_REGN_CASE(28, case_macro);			\
		BRBE_REGN_CASE(29, case_macro);			\
		BRBE_REGN_CASE(30, case_macro);			\
		BRBE_REGN_CASE(31, case_macro);			\
		default: WARN(1, "Invalid BRB* index %d\n", x);	\
		}						\
	} while (0)

#define RETURN_READ_BRBSRCN(n) \
	return read_sysreg_s(SYS_BRBSRC_EL1(n))
static inline u64 get_brbsrc_reg(int idx)
{
	BRBE_REGN_SWITCH(idx, RETURN_READ_BRBSRCN);
	return 0;
}

#define RETURN_READ_BRBTGTN(n) \
	return read_sysreg_s(SYS_BRBTGT_EL1(n))
static u64 get_brbtgt_reg(int idx)
{
	BRBE_REGN_SWITCH(idx, RETURN_READ_BRBTGTN);
	return 0;
}

#define RETURN_READ_BRBINFN(n) \
	return read_sysreg_s(SYS_BRBINF_EL1(n))
static u64 get_brbinf_reg(int idx)
{
	BRBE_REGN_SWITCH(idx, RETURN_READ_BRBINFN);
	return 0;
}

static u64 brbe_record_valid(u64 brbinf)
{
	return FIELD_GET(BRBINFx_EL1_VALID_MASK, brbinf);
}

static bool brbe_invalid(u64 brbinf)
{
	return brbe_record_valid(brbinf) == BRBINFx_EL1_VALID_NONE;
}

static bool brbe_record_is_complete(u64 brbinf)
{
	return brbe_record_valid(brbinf) == BRBINFx_EL1_VALID_FULL;
}

static bool brbe_record_is_source_only(u64 brbinf)
{
	return brbe_record_valid(brbinf) == BRBINFx_EL1_VALID_SOURCE;
}

static bool brbe_record_is_target_only(u64 brbinf)
{
	return brbe_record_valid(brbinf) == BRBINFx_EL1_VALID_TARGET;
}

static int brbinf_get_in_tx(u64 brbinf)
{
	return FIELD_GET(BRBINFx_EL1_T_MASK, brbinf);
}

static int brbinf_get_mispredict(u64 brbinf)
{
	return FIELD_GET(BRBINFx_EL1_MPRED_MASK, brbinf);
}

static int brbinf_get_lastfailed(u64 brbinf)
{
	return FIELD_GET(BRBINFx_EL1_LASTFAILED_MASK, brbinf);
}

static u16 brbinf_get_cycles(u64 brbinf)
{
	u32 exp, mant, cycles;
	/*
	 * Captured cycle count is unknown and hence
	 * should not be passed on to userspace.
	 */
	if (brbinf & BRBINFx_EL1_CCU)
		return 0;

	exp = FIELD_GET(BRBINFx_EL1_CC_EXP_MASK, brbinf);
	mant = FIELD_GET(BRBINFx_EL1_CC_MANT_MASK, brbinf);

	if (!exp)
		return mant;

	cycles = (mant | 0x100) << (exp - 1);

	return min(cycles, U16_MAX);
}

static int brbinf_get_type(u64 brbinf)
{
	return FIELD_GET(BRBINFx_EL1_TYPE_MASK, brbinf);
}

static int brbinf_get_el(u64 brbinf)
{
	return FIELD_GET(BRBINFx_EL1_EL_MASK, brbinf);
}

void brbe_invalidate(void)
{
	/* Ensure all branches before this point are recorded */
	isb();
	asm volatile(BRB_IALL_INSN);
	/* Ensure all branch records are invalidated after this point */
	isb();
}

static bool valid_brbe_nr(int brbe_nr)
{
	return brbe_nr == BRBIDR0_EL1_NUMREC_8 ||
	       brbe_nr == BRBIDR0_EL1_NUMREC_16 ||
	       brbe_nr == BRBIDR0_EL1_NUMREC_32 ||
	       brbe_nr == BRBIDR0_EL1_NUMREC_64;
}

static bool valid_brbe_cc(int brbe_cc)
{
	return brbe_cc == BRBIDR0_EL1_CC_20_BIT;
}

static bool valid_brbe_format(int brbe_format)
{
	return brbe_format == BRBIDR0_EL1_FORMAT_FORMAT_0;
}

static bool valid_brbidr(u64 brbidr)
{
	int brbe_format, brbe_cc, brbe_nr;

	brbe_format = FIELD_GET(BRBIDR0_EL1_FORMAT_MASK, brbidr);
	brbe_cc = FIELD_GET(BRBIDR0_EL1_CC_MASK, brbidr);
	brbe_nr = FIELD_GET(BRBIDR0_EL1_NUMREC_MASK, brbidr);

	return valid_brbe_format(brbe_format) && valid_brbe_cc(brbe_cc) && valid_brbe_nr(brbe_nr);
}

static bool valid_brbe_version(int brbe_version)
{
	return brbe_version == ID_AA64DFR0_EL1_BRBE_IMP ||
	       brbe_version == ID_AA64DFR0_EL1_BRBE_BRBE_V1P1;
}

static void select_brbe_bank(int bank)
{
	u64 brbfcr;

	brbfcr = read_sysreg_s(SYS_BRBFCR_EL1);
	brbfcr &= ~BRBFCR_EL1_BANK_MASK;
	brbfcr |= SYS_FIELD_PREP(BRBFCR_EL1, BANK, bank);
	write_sysreg_s(brbfcr, SYS_BRBFCR_EL1);
	/*
	 * Arm ARM (DDI 0487K.a) D.18.4 rule PPBZP requires explicit sync
	 * between setting BANK and accessing branch records.
	 */
	isb();
}

static bool __read_brbe_regset(struct brbe_regset *entry, int idx)
{
	entry->brbinf = get_brbinf_reg(idx);

	if (brbe_invalid(entry->brbinf))
		return false;

	entry->brbsrc = get_brbsrc_reg(idx);
	entry->brbtgt = get_brbtgt_reg(idx);
	return true;
}

/*
 * Generic perf branch filters supported on BRBE
 *
 * New branch filters need to be evaluated whether they could be supported on
 * BRBE. This ensures that such branch filters would not just be accepted, to
 * fail silently. PERF_SAMPLE_BRANCH_HV is a special case that is selectively
 * supported only on platforms where kernel is in hyp mode.
 */
#define BRBE_EXCLUDE_BRANCH_FILTERS (PERF_SAMPLE_BRANCH_ABORT_TX	| \
				     PERF_SAMPLE_BRANCH_IN_TX		| \
				     PERF_SAMPLE_BRANCH_NO_TX		| \
				     PERF_SAMPLE_BRANCH_CALL_STACK	| \
				     PERF_SAMPLE_BRANCH_COUNTERS)

#define BRBE_ALLOWED_BRANCH_TYPES   (PERF_SAMPLE_BRANCH_ANY		| \
				     PERF_SAMPLE_BRANCH_ANY_CALL	| \
				     PERF_SAMPLE_BRANCH_ANY_RETURN	| \
				     PERF_SAMPLE_BRANCH_IND_CALL	| \
				     PERF_SAMPLE_BRANCH_COND		| \
				     PERF_SAMPLE_BRANCH_IND_JUMP	| \
				     PERF_SAMPLE_BRANCH_CALL)


#define BRBE_ALLOWED_BRANCH_FILTERS (PERF_SAMPLE_BRANCH_USER		| \
				     PERF_SAMPLE_BRANCH_KERNEL		| \
				     PERF_SAMPLE_BRANCH_HV		| \
				     BRBE_ALLOWED_BRANCH_TYPES		| \
				     PERF_SAMPLE_BRANCH_NO_FLAGS	| \
				     PERF_SAMPLE_BRANCH_NO_CYCLES	| \
				     PERF_SAMPLE_BRANCH_TYPE_SAVE	| \
				     PERF_SAMPLE_BRANCH_HW_INDEX	| \
				     PERF_SAMPLE_BRANCH_PRIV_SAVE)

#define BRBE_PERF_BRANCH_FILTERS    (BRBE_ALLOWED_BRANCH_FILTERS	| \
				     BRBE_EXCLUDE_BRANCH_FILTERS)

/*
 * BRBE supports the following functional branch type filters while
 * generating branch records. These branch filters can be enabled,
 * either individually or as a group i.e ORing multiple filters
 * with each other.
 *
 * BRBFCR_EL1_CONDDIR  - Conditional direct branch
 * BRBFCR_EL1_DIRCALL  - Direct call
 * BRBFCR_EL1_INDCALL  - Indirect call
 * BRBFCR_EL1_INDIRECT - Indirect branch
 * BRBFCR_EL1_DIRECT   - Direct branch
 * BRBFCR_EL1_RTN      - Subroutine return
 */
static u64 branch_type_to_brbfcr(int branch_type)
{
	u64 brbfcr = 0;

	if (branch_type & PERF_SAMPLE_BRANCH_ANY) {
		brbfcr |= BRBFCR_EL1_BRANCH_FILTERS;
		return brbfcr;
	}

	if (branch_type & PERF_SAMPLE_BRANCH_ANY_CALL) {
		brbfcr |= BRBFCR_EL1_INDCALL;
		brbfcr |= BRBFCR_EL1_DIRCALL;
	}

	if (branch_type & PERF_SAMPLE_BRANCH_ANY_RETURN)
		brbfcr |= BRBFCR_EL1_RTN;

	if (branch_type & PERF_SAMPLE_BRANCH_IND_CALL)
		brbfcr |= BRBFCR_EL1_INDCALL;

	if (branch_type & PERF_SAMPLE_BRANCH_COND)
		brbfcr |= BRBFCR_EL1_CONDDIR;

	if (branch_type & PERF_SAMPLE_BRANCH_IND_JUMP)
		brbfcr |= BRBFCR_EL1_INDIRECT;

	if (branch_type & PERF_SAMPLE_BRANCH_CALL)
		brbfcr |= BRBFCR_EL1_DIRCALL;

	return brbfcr;
}

/*
 * BRBE supports the following privilege mode filters while generating
 * branch records.
 *
 * BRBCR_ELx_E0BRE - EL0 branch records
 * BRBCR_ELx_ExBRE - EL1/EL2 branch records
 *
 * BRBE also supports the following additional functional branch type
 * filters while generating branch records.
 *
 * BRBCR_ELx_EXCEPTION - Exception
 * BRBCR_ELx_ERTN     -  Exception return
 */
static u64 branch_type_to_brbcr(int branch_type)
{
	u64 brbcr = BRBCR_ELx_FZP | BRBCR_ELx_DEFAULT_TS;

	if (branch_type & PERF_SAMPLE_BRANCH_USER)
		brbcr |= BRBCR_ELx_E0BRE;

	/*
	 * When running in the hyp mode, writing into BRBCR_EL1
	 * actually writes into BRBCR_EL2 instead. Field E2BRE
	 * is also at the same position as E1BRE.
	 */
	if (branch_type & PERF_SAMPLE_BRANCH_KERNEL)
		brbcr |= BRBCR_ELx_ExBRE;

	if (branch_type & PERF_SAMPLE_BRANCH_HV) {
		if (is_kernel_in_hyp_mode())
			brbcr |= BRBCR_ELx_ExBRE;
	}

	if (!(branch_type & PERF_SAMPLE_BRANCH_NO_CYCLES))
		brbcr |= BRBCR_ELx_CC;

	if (!(branch_type & PERF_SAMPLE_BRANCH_NO_FLAGS))
		brbcr |= BRBCR_ELx_MPRED;

	/*
	 * The exception and exception return branches could be
	 * captured, irrespective of the perf event's privilege.
	 * If the perf event does not have enough privilege for
	 * a given exception level, then addresses which falls
	 * under that exception level will be reported as zero
	 * for the captured branch record, creating source only
	 * or target only records.
	 */
	if (branch_type & PERF_SAMPLE_BRANCH_KERNEL) {
		if (branch_type & PERF_SAMPLE_BRANCH_ANY) {
			brbcr |= BRBCR_ELx_EXCEPTION;
			brbcr |= BRBCR_ELx_ERTN;
		}

		if (branch_type & PERF_SAMPLE_BRANCH_ANY_CALL)
			brbcr |= BRBCR_ELx_EXCEPTION;

		if (branch_type & PERF_SAMPLE_BRANCH_ANY_RETURN)
			brbcr |= BRBCR_ELx_ERTN;
	}
	return brbcr;
}

bool brbe_branch_attr_valid(struct perf_event *event)
{
	u64 branch_type = event->attr.branch_sample_type;

	/*
	 * Ensure both perf branch filter allowed and exclude
	 * masks are always in sync with the generic perf ABI.
	 */
	BUILD_BUG_ON(BRBE_PERF_BRANCH_FILTERS != (PERF_SAMPLE_BRANCH_MAX - 1));

	if (branch_type & BRBE_EXCLUDE_BRANCH_FILTERS) {
		pr_debug("requested branch filter not supported 0x%llx\n", branch_type);
		return false;
	}

	/* Ensure at least 1 branch type is enabled */
	if (!(branch_type & BRBE_ALLOWED_BRANCH_TYPES)) {
		pr_debug("no branch type enabled 0x%llx\n", branch_type);
		return false;
	}

	/*
	 * No branches are recorded in guests nor nVHE hypervisors, so
	 * excluding the host or both kernel and user is invalid.
	 *
	 * Ideally we'd just require exclude_guest and exclude_hv, but setting
	 * event filters with perf for kernel or user don't set exclude_guest.
	 * So effectively, exclude_guest and exclude_hv are ignored.
	 */
	if (event->attr.exclude_host || (event->attr.exclude_user && event->attr.exclude_kernel)) {
		pr_debug("branch filter in hypervisor or guest only not supported 0x%llx\n", branch_type);
		return false;
	}

	event->hw.branch_reg.config = branch_type_to_brbfcr(event->attr.branch_sample_type);
	event->hw.extra_reg.config = branch_type_to_brbcr(event->attr.branch_sample_type);

	return true;
}

unsigned int brbe_num_branch_records(const struct arm_pmu *armpmu)
{
	return FIELD_GET(BRBIDR0_EL1_NUMREC_MASK, armpmu->reg_brbidr);
}

void brbe_probe(struct arm_pmu *armpmu)
{
	u64 brbidr, aa64dfr0 = read_sysreg_s(SYS_ID_AA64DFR0_EL1);
	u32 brbe;

	brbe = cpuid_feature_extract_unsigned_field(aa64dfr0, ID_AA64DFR0_EL1_BRBE_SHIFT);
	if (!valid_brbe_version(brbe))
		return;

	brbidr = read_sysreg_s(SYS_BRBIDR0_EL1);
	if (!valid_brbidr(brbidr))
		return;

	armpmu->reg_brbidr = brbidr;
}

/*
 * BRBE is assumed to be disabled/paused on entry
 */
void brbe_enable(const struct arm_pmu *arm_pmu)
{
	struct pmu_hw_events *cpuc = this_cpu_ptr(arm_pmu->hw_events);
	u64 brbfcr = 0, brbcr = 0;

	/*
	 * Discard existing records to avoid a discontinuity, e.g. records
	 * missed during handling an overflow.
	 */
	brbe_invalidate();

	/*
	 * Merge the permitted branch filters of all events.
	 */
	for (int i = 0; i < ARMPMU_MAX_HWEVENTS; i++) {
		struct perf_event *event = cpuc->events[i];

		if (event && has_branch_stack(event)) {
			brbfcr |= event->hw.branch_reg.config;
			brbcr |= event->hw.extra_reg.config;
		}
	}

	/*
	 * In VHE mode with MDCR_EL2.HPMN equal to PMCR_EL0.N, BRBCR_EL1.FZP
	 * controls freezing the branch records on counter overflow rather than
	 * BRBCR_EL2.FZP (which writes to BRBCR_EL1 are redirected to).
	 * The exception levels are enabled/disabled in BRBCR_EL2, so keep EL1
	 * and EL0 recording disabled for guests.
	 *
	 * As BRBCR_EL1 CC and MPRED bits also need to match, use the same
	 * value for both registers just masking the exception levels.
	 */
	if (is_kernel_in_hyp_mode())
		write_sysreg_s(brbcr & ~(BRBCR_ELx_ExBRE | BRBCR_ELx_E0BRE), SYS_BRBCR_EL12);
	write_sysreg_s(brbcr, SYS_BRBCR_EL1);
	/* Ensure BRBCR_ELx settings take effect before unpausing */
	isb();

	/* Finally write SYS_BRBFCR_EL to unpause BRBE */
	write_sysreg_s(brbfcr, SYS_BRBFCR_EL1);
	/* Synchronization in PMCR write ensures ordering WRT PMU enabling */
}

void brbe_disable(void)
{
	/*
	 * No need for synchronization here as synchronization in PMCR write
	 * ensures ordering and in the interrupt handler this is a NOP as
	 * we're already paused.
	 */
	write_sysreg_s(BRBFCR_EL1_PAUSED, SYS_BRBFCR_EL1);
	write_sysreg_s(0, SYS_BRBCR_EL1);
}

static const int brbe_type_to_perf_type_map[BRBINFx_EL1_TYPE_DEBUG_EXIT + 1][2] = {
	[BRBINFx_EL1_TYPE_DIRECT_UNCOND] = { PERF_BR_UNCOND, 0 },
	[BRBINFx_EL1_TYPE_INDIRECT] = { PERF_BR_IND, 0 },
	[BRBINFx_EL1_TYPE_DIRECT_LINK] = { PERF_BR_CALL, 0 },
	[BRBINFx_EL1_TYPE_INDIRECT_LINK] = { PERF_BR_IND_CALL, 0 },
	[BRBINFx_EL1_TYPE_RET] = { PERF_BR_RET, 0 },
	[BRBINFx_EL1_TYPE_DIRECT_COND] = { PERF_BR_COND, 0 },
	[BRBINFx_EL1_TYPE_CALL] = { PERF_BR_SYSCALL, 0 },
	[BRBINFx_EL1_TYPE_ERET] = { PERF_BR_ERET, 0 },
	[BRBINFx_EL1_TYPE_IRQ] = { PERF_BR_IRQ, 0 },
	[BRBINFx_EL1_TYPE_TRAP] = { PERF_BR_IRQ, 0 },
	[BRBINFx_EL1_TYPE_SERROR] = { PERF_BR_SERROR, 0 },
	[BRBINFx_EL1_TYPE_ALIGN_FAULT] = { PERF_BR_EXTEND_ABI, PERF_BR_NEW_FAULT_ALGN },
	[BRBINFx_EL1_TYPE_INSN_FAULT] = { PERF_BR_EXTEND_ABI, PERF_BR_NEW_FAULT_INST },
	[BRBINFx_EL1_TYPE_DATA_FAULT] = { PERF_BR_EXTEND_ABI, PERF_BR_NEW_FAULT_DATA },
};

static void brbe_set_perf_entry_type(struct perf_branch_entry *entry, u64 brbinf)
{
	int brbe_type = brbinf_get_type(brbinf);

	if (brbe_type <= BRBINFx_EL1_TYPE_DEBUG_EXIT) {
		const int *br_type = brbe_type_to_perf_type_map[brbe_type];

		entry->type = br_type[0];
		entry->new_type = br_type[1];
	}
}

static int brbinf_get_perf_priv(u64 brbinf)
{
	int brbe_el = brbinf_get_el(brbinf);

	switch (brbe_el) {
	case BRBINFx_EL1_EL_EL0:
		return PERF_BR_PRIV_USER;
	case BRBINFx_EL1_EL_EL1:
		return PERF_BR_PRIV_KERNEL;
	case BRBINFx_EL1_EL_EL2:
		if (is_kernel_in_hyp_mode())
			return PERF_BR_PRIV_KERNEL;
		return PERF_BR_PRIV_HV;
	default:
		pr_warn_once("%d - unknown branch privilege captured\n", brbe_el);
		return PERF_BR_PRIV_UNKNOWN;
	}
}

static bool perf_entry_from_brbe_regset(int index, struct perf_branch_entry *entry,
					const struct perf_event *event)
{
	struct brbe_regset bregs;
	u64 brbinf;

	if (!__read_brbe_regset(&bregs, index))
		return false;

	brbinf = bregs.brbinf;
	perf_clear_branch_entry_bitfields(entry);
	if (brbe_record_is_complete(brbinf)) {
		entry->from = bregs.brbsrc;
		entry->to = bregs.brbtgt;
	} else if (brbe_record_is_source_only(brbinf)) {
		entry->from = bregs.brbsrc;
		entry->to = 0;
	} else if (brbe_record_is_target_only(brbinf)) {
		entry->from = 0;
		entry->to = bregs.brbtgt;
	}

	brbe_set_perf_entry_type(entry, brbinf);

	if (!branch_sample_no_cycles(event))
		entry->cycles = brbinf_get_cycles(brbinf);

	if (!branch_sample_no_flags(event)) {
		/* Mispredict info is available for source only and complete branch records. */
		if (!brbe_record_is_target_only(brbinf)) {
			entry->mispred = brbinf_get_mispredict(brbinf);
			entry->predicted = !entry->mispred;
		}

		/*
		 * Currently TME feature is neither implemented in any hardware
		 * nor it is being supported in the kernel. Just warn here once
		 * if TME related information shows up rather unexpectedly.
		 */
		if (brbinf_get_lastfailed(brbinf) || brbinf_get_in_tx(brbinf))
			pr_warn_once("Unknown transaction states\n");
	}

	/*
	 * Branch privilege level is available for target only and complete
	 * branch records.
	 */
	if (!brbe_record_is_source_only(brbinf))
		entry->priv = brbinf_get_perf_priv(brbinf);

	return true;
}

#define PERF_BR_ARM64_ALL (				\
	BIT(PERF_BR_COND) |				\
	BIT(PERF_BR_UNCOND) |				\
	BIT(PERF_BR_IND) |				\
	BIT(PERF_BR_CALL) |				\
	BIT(PERF_BR_IND_CALL) |				\
	BIT(PERF_BR_RET))

#define PERF_BR_ARM64_ALL_KERNEL (			\
	BIT(PERF_BR_SYSCALL) |				\
	BIT(PERF_BR_IRQ) |				\
	BIT(PERF_BR_SERROR) |				\
	BIT(PERF_BR_MAX + PERF_BR_NEW_FAULT_ALGN) |	\
	BIT(PERF_BR_MAX + PERF_BR_NEW_FAULT_DATA) |	\
	BIT(PERF_BR_MAX + PERF_BR_NEW_FAULT_INST))

static void prepare_event_branch_type_mask(u64 branch_sample,
					   unsigned long *event_type_mask)
{
	if (branch_sample & PERF_SAMPLE_BRANCH_ANY) {
		if (branch_sample & PERF_SAMPLE_BRANCH_KERNEL)
			bitmap_from_u64(event_type_mask,
				BIT(PERF_BR_ERET) | PERF_BR_ARM64_ALL |
				PERF_BR_ARM64_ALL_KERNEL);
		else
			bitmap_from_u64(event_type_mask, PERF_BR_ARM64_ALL);
		return;
	}

	bitmap_zero(event_type_mask, PERF_BR_ARM64_MAX);

	if (branch_sample & PERF_SAMPLE_BRANCH_ANY_CALL) {
		if (branch_sample & PERF_SAMPLE_BRANCH_KERNEL)
			bitmap_from_u64(event_type_mask, PERF_BR_ARM64_ALL_KERNEL);

		set_bit(PERF_BR_CALL, event_type_mask);
		set_bit(PERF_BR_IND_CALL, event_type_mask);
	}

	if (branch_sample & PERF_SAMPLE_BRANCH_IND_JUMP)
		set_bit(PERF_BR_IND, event_type_mask);

	if (branch_sample & PERF_SAMPLE_BRANCH_COND)
		set_bit(PERF_BR_COND, event_type_mask);

	if (branch_sample & PERF_SAMPLE_BRANCH_CALL)
		set_bit(PERF_BR_CALL, event_type_mask);

	if (branch_sample & PERF_SAMPLE_BRANCH_IND_CALL)
		set_bit(PERF_BR_IND_CALL, event_type_mask);

	if (branch_sample & PERF_SAMPLE_BRANCH_ANY_RETURN) {
		set_bit(PERF_BR_RET, event_type_mask);

		if (branch_sample & PERF_SAMPLE_BRANCH_KERNEL)
			set_bit(PERF_BR_ERET, event_type_mask);
	}
}

/*
 * BRBE is configured with an OR of permissions from all events, so there may
 * be events which have to be dropped or events where just the source or target
 * address has to be zeroed.
 */
static bool filter_branch_privilege(struct perf_branch_entry *entry, u64 branch_sample_type)
{
	bool from_user = access_ok((void __user *)(unsigned long)entry->from, 4);
	bool to_user = access_ok((void __user *)(unsigned long)entry->to, 4);
	bool exclude_kernel = !((branch_sample_type & PERF_SAMPLE_BRANCH_KERNEL) ||
		(is_kernel_in_hyp_mode() && (branch_sample_type & PERF_SAMPLE_BRANCH_HV)));

	/* We can only have a half record if permissions have not been expanded */
	if (!entry->from || !entry->to)
		return true;

	/*
	 * If record is within a single exception level, just need to either
	 * drop or keep the entire record.
	 */
	if (from_user == to_user)
		return ((entry->priv == PERF_BR_PRIV_KERNEL) && !exclude_kernel) ||
			((entry->priv == PERF_BR_PRIV_USER) &&
			 (branch_sample_type & PERF_SAMPLE_BRANCH_USER));

	/*
	 * Record is across exception levels, mask addresses for the exception
	 * level we're not capturing.
	 */
	if (!(branch_sample_type & PERF_SAMPLE_BRANCH_USER)) {
		if (from_user)
			entry->from = 0;
		if (to_user)
			entry->to = 0;
	}

	if (exclude_kernel) {
		if (!from_user)
			entry->from = 0;
		if (!to_user)
			entry->to = 0;
	}

	return true;
}

static bool filter_branch_type(struct perf_branch_entry *entry,
			       const unsigned long *event_type_mask)
{
	if (entry->type == PERF_BR_EXTEND_ABI)
		return test_bit(PERF_BR_MAX + entry->new_type, event_type_mask);
	else
		return test_bit(entry->type, event_type_mask);
}

static bool filter_branch_record(struct perf_branch_entry *entry,
				 u64 branch_sample,
				 const unsigned long *event_type_mask)
{
	return filter_branch_type(entry, event_type_mask) &&
		filter_branch_privilege(entry, branch_sample);
}

void brbe_read_filtered_entries(struct perf_branch_stack *branch_stack,
				const struct perf_event *event)
{
	struct arm_pmu *cpu_pmu = to_arm_pmu(event->pmu);
	int nr_hw = brbe_num_branch_records(cpu_pmu);
	int nr_banks = DIV_ROUND_UP(nr_hw, BRBE_BANK_MAX_ENTRIES);
	int nr_filtered = 0;
	u64 branch_sample_type = event->attr.branch_sample_type;
	DECLARE_BITMAP(event_type_mask, PERF_BR_ARM64_MAX);

	prepare_event_branch_type_mask(branch_sample_type, event_type_mask);

	for (int bank = 0; bank < nr_banks; bank++) {
		int nr_remaining = nr_hw - (bank * BRBE_BANK_MAX_ENTRIES);
		int nr_this_bank = min(nr_remaining, BRBE_BANK_MAX_ENTRIES);

		select_brbe_bank(bank);

		for (int i = 0; i < nr_this_bank; i++) {
			struct perf_branch_entry *pbe = &branch_stack->entries[nr_filtered];

			if (!perf_entry_from_brbe_regset(i, pbe, event))
				goto done;

			if (!filter_branch_record(pbe, branch_sample_type, event_type_mask))
				continue;

			nr_filtered++;
		}
	}

done:
	branch_stack->nr = nr_filtered;
}
