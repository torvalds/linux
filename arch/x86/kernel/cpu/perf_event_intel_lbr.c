#include <linux/perf_event.h>
#include <linux/types.h>

#include <asm/perf_event.h>
#include <asm/msr.h>

#include "perf_event.h"

enum {
	LBR_FORMAT_32		= 0x00,
	LBR_FORMAT_LIP		= 0x01,
	LBR_FORMAT_EIP		= 0x02,
	LBR_FORMAT_EIP_FLAGS	= 0x03,
};

/*
 * Intel LBR_SELECT bits
 * Intel Vol3a, April 2011, Section 16.7 Table 16-10
 *
 * Hardware branch filter (not available on all CPUs)
 */
#define LBR_KERNEL_BIT		0 /* do not capture at ring0 */
#define LBR_USER_BIT		1 /* do not capture at ring > 0 */
#define LBR_JCC_BIT		2 /* do not capture conditional branches */
#define LBR_REL_CALL_BIT	3 /* do not capture relative calls */
#define LBR_IND_CALL_BIT	4 /* do not capture indirect calls */
#define LBR_RETURN_BIT		5 /* do not capture near returns */
#define LBR_IND_JMP_BIT		6 /* do not capture indirect jumps */
#define LBR_REL_JMP_BIT		7 /* do not capture relative jumps */
#define LBR_FAR_BIT		8 /* do not capture far branches */

#define LBR_KERNEL	(1 << LBR_KERNEL_BIT)
#define LBR_USER	(1 << LBR_USER_BIT)
#define LBR_JCC		(1 << LBR_JCC_BIT)
#define LBR_REL_CALL	(1 << LBR_REL_CALL_BIT)
#define LBR_IND_CALL	(1 << LBR_IND_CALL_BIT)
#define LBR_RETURN	(1 << LBR_RETURN_BIT)
#define LBR_REL_JMP	(1 << LBR_REL_JMP_BIT)
#define LBR_IND_JMP	(1 << LBR_IND_JMP_BIT)
#define LBR_FAR		(1 << LBR_FAR_BIT)

#define LBR_PLM (LBR_KERNEL | LBR_USER)

#define LBR_SEL_MASK	0x1ff	/* valid bits in LBR_SELECT */
#define LBR_NOT_SUPP	-1	/* LBR filter not supported */
#define LBR_IGN		0	/* ignored */

#define LBR_ANY		 \
	(LBR_JCC	|\
	 LBR_REL_CALL	|\
	 LBR_IND_CALL	|\
	 LBR_RETURN	|\
	 LBR_REL_JMP	|\
	 LBR_IND_JMP	|\
	 LBR_FAR)

#define LBR_FROM_FLAG_MISPRED  (1ULL << 63)

/*
 * We only support LBR implementations that have FREEZE_LBRS_ON_PMI
 * otherwise it becomes near impossible to get a reliable stack.
 */

static void __intel_pmu_lbr_enable(void)
{
	u64 debugctl;

	rdmsrl(MSR_IA32_DEBUGCTLMSR, debugctl);
	debugctl |= (DEBUGCTLMSR_LBR | DEBUGCTLMSR_FREEZE_LBRS_ON_PMI);
	wrmsrl(MSR_IA32_DEBUGCTLMSR, debugctl);
}

static void __intel_pmu_lbr_disable(void)
{
	u64 debugctl;

	rdmsrl(MSR_IA32_DEBUGCTLMSR, debugctl);
	debugctl &= ~(DEBUGCTLMSR_LBR | DEBUGCTLMSR_FREEZE_LBRS_ON_PMI);
	wrmsrl(MSR_IA32_DEBUGCTLMSR, debugctl);
}

static void intel_pmu_lbr_reset_32(void)
{
	int i;

	for (i = 0; i < x86_pmu.lbr_nr; i++)
		wrmsrl(x86_pmu.lbr_from + i, 0);
}

static void intel_pmu_lbr_reset_64(void)
{
	int i;

	for (i = 0; i < x86_pmu.lbr_nr; i++) {
		wrmsrl(x86_pmu.lbr_from + i, 0);
		wrmsrl(x86_pmu.lbr_to   + i, 0);
	}
}

void intel_pmu_lbr_reset(void)
{
	if (!x86_pmu.lbr_nr)
		return;

	if (x86_pmu.intel_cap.lbr_format == LBR_FORMAT_32)
		intel_pmu_lbr_reset_32();
	else
		intel_pmu_lbr_reset_64();
}

void intel_pmu_lbr_enable(struct perf_event *event)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);

	if (!x86_pmu.lbr_nr)
		return;

	/*
	 * Reset the LBR stack if we changed task context to
	 * avoid data leaks.
	 */

	if (event->ctx->task && cpuc->lbr_context != event->ctx) {
		intel_pmu_lbr_reset();
		cpuc->lbr_context = event->ctx;
	}

	cpuc->lbr_users++;
}

void intel_pmu_lbr_disable(struct perf_event *event)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);

	if (!x86_pmu.lbr_nr)
		return;

	cpuc->lbr_users--;
	WARN_ON_ONCE(cpuc->lbr_users < 0);

	if (cpuc->enabled && !cpuc->lbr_users)
		__intel_pmu_lbr_disable();
}

void intel_pmu_lbr_enable_all(void)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);

	if (cpuc->lbr_users)
		__intel_pmu_lbr_enable();
}

void intel_pmu_lbr_disable_all(void)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);

	if (cpuc->lbr_users)
		__intel_pmu_lbr_disable();
}

static inline u64 intel_pmu_lbr_tos(void)
{
	u64 tos;

	rdmsrl(x86_pmu.lbr_tos, tos);

	return tos;
}

static void intel_pmu_lbr_read_32(struct cpu_hw_events *cpuc)
{
	unsigned long mask = x86_pmu.lbr_nr - 1;
	u64 tos = intel_pmu_lbr_tos();
	int i;

	for (i = 0; i < x86_pmu.lbr_nr; i++) {
		unsigned long lbr_idx = (tos - i) & mask;
		union {
			struct {
				u32 from;
				u32 to;
			};
			u64     lbr;
		} msr_lastbranch;

		rdmsrl(x86_pmu.lbr_from + lbr_idx, msr_lastbranch.lbr);

		cpuc->lbr_entries[i].from	= msr_lastbranch.from;
		cpuc->lbr_entries[i].to		= msr_lastbranch.to;
		cpuc->lbr_entries[i].mispred	= 0;
		cpuc->lbr_entries[i].predicted	= 0;
		cpuc->lbr_entries[i].reserved	= 0;
	}
	cpuc->lbr_stack.nr = i;
}

/*
 * Due to lack of segmentation in Linux the effective address (offset)
 * is the same as the linear address, allowing us to merge the LIP and EIP
 * LBR formats.
 */
static void intel_pmu_lbr_read_64(struct cpu_hw_events *cpuc)
{
	unsigned long mask = x86_pmu.lbr_nr - 1;
	int lbr_format = x86_pmu.intel_cap.lbr_format;
	u64 tos = intel_pmu_lbr_tos();
	int i;

	for (i = 0; i < x86_pmu.lbr_nr; i++) {
		unsigned long lbr_idx = (tos - i) & mask;
		u64 from, to, mis = 0, pred = 0;

		rdmsrl(x86_pmu.lbr_from + lbr_idx, from);
		rdmsrl(x86_pmu.lbr_to   + lbr_idx, to);

		if (lbr_format == LBR_FORMAT_EIP_FLAGS) {
			mis = !!(from & LBR_FROM_FLAG_MISPRED);
			pred = !mis;
			from = (u64)((((s64)from) << 1) >> 1);
		}

		cpuc->lbr_entries[i].from	= from;
		cpuc->lbr_entries[i].to		= to;
		cpuc->lbr_entries[i].mispred	= mis;
		cpuc->lbr_entries[i].predicted	= pred;
		cpuc->lbr_entries[i].reserved	= 0;
	}
	cpuc->lbr_stack.nr = i;
}

void intel_pmu_lbr_read(void)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);

	if (!cpuc->lbr_users)
		return;

	if (x86_pmu.intel_cap.lbr_format == LBR_FORMAT_32)
		intel_pmu_lbr_read_32(cpuc);
	else
		intel_pmu_lbr_read_64(cpuc);
}

/*
 * Map interface branch filters onto LBR filters
 */
static const int nhm_lbr_sel_map[PERF_SAMPLE_BRANCH_MAX] = {
	[PERF_SAMPLE_BRANCH_ANY]	= LBR_ANY,
	[PERF_SAMPLE_BRANCH_USER]	= LBR_USER,
	[PERF_SAMPLE_BRANCH_KERNEL]	= LBR_KERNEL,
	[PERF_SAMPLE_BRANCH_HV]		= LBR_IGN,
	[PERF_SAMPLE_BRANCH_ANY_RETURN]	= LBR_RETURN | LBR_REL_JMP
					| LBR_IND_JMP | LBR_FAR,
	/*
	 * NHM/WSM erratum: must include REL_JMP+IND_JMP to get CALL branches
	 */
	[PERF_SAMPLE_BRANCH_ANY_CALL] =
	 LBR_REL_CALL | LBR_IND_CALL | LBR_REL_JMP | LBR_IND_JMP | LBR_FAR,
	/*
	 * NHM/WSM erratum: must include IND_JMP to capture IND_CALL
	 */
	[PERF_SAMPLE_BRANCH_IND_CALL] = LBR_IND_CALL | LBR_IND_JMP,
};

static const int snb_lbr_sel_map[PERF_SAMPLE_BRANCH_MAX] = {
	[PERF_SAMPLE_BRANCH_ANY]	= LBR_ANY,
	[PERF_SAMPLE_BRANCH_USER]	= LBR_USER,
	[PERF_SAMPLE_BRANCH_KERNEL]	= LBR_KERNEL,
	[PERF_SAMPLE_BRANCH_HV]		= LBR_IGN,
	[PERF_SAMPLE_BRANCH_ANY_RETURN]	= LBR_RETURN | LBR_FAR,
	[PERF_SAMPLE_BRANCH_ANY_CALL]	= LBR_REL_CALL | LBR_IND_CALL
					| LBR_FAR,
	[PERF_SAMPLE_BRANCH_IND_CALL]	= LBR_IND_CALL,
};

/* core */
void intel_pmu_lbr_init_core(void)
{
	x86_pmu.lbr_nr     = 4;
	x86_pmu.lbr_tos    = MSR_LBR_TOS;
	x86_pmu.lbr_from   = MSR_LBR_CORE_FROM;
	x86_pmu.lbr_to     = MSR_LBR_CORE_TO;

	pr_cont("4-deep LBR, ");
}

/* nehalem/westmere */
void intel_pmu_lbr_init_nhm(void)
{
	x86_pmu.lbr_nr     = 16;
	x86_pmu.lbr_tos    = MSR_LBR_TOS;
	x86_pmu.lbr_from   = MSR_LBR_NHM_FROM;
	x86_pmu.lbr_to     = MSR_LBR_NHM_TO;

	x86_pmu.lbr_sel_mask = LBR_SEL_MASK;
	x86_pmu.lbr_sel_map  = nhm_lbr_sel_map;

	pr_cont("16-deep LBR, ");
}

/* sandy bridge */
void intel_pmu_lbr_init_snb(void)
{
	x86_pmu.lbr_nr	 = 16;
	x86_pmu.lbr_tos	 = MSR_LBR_TOS;
	x86_pmu.lbr_from = MSR_LBR_NHM_FROM;
	x86_pmu.lbr_to   = MSR_LBR_NHM_TO;

	x86_pmu.lbr_sel_mask = LBR_SEL_MASK;
	x86_pmu.lbr_sel_map  = snb_lbr_sel_map;

	pr_cont("16-deep LBR, ");
}

/* atom */
void intel_pmu_lbr_init_atom(void)
{
	/*
	 * only models starting at stepping 10 seems
	 * to have an operational LBR which can freeze
	 * on PMU interrupt
	 */
	if (boot_cpu_data.x86_mask < 10) {
		pr_cont("LBR disabled due to erratum");
		return;
	}

	x86_pmu.lbr_nr	   = 8;
	x86_pmu.lbr_tos    = MSR_LBR_TOS;
	x86_pmu.lbr_from   = MSR_LBR_CORE_FROM;
	x86_pmu.lbr_to     = MSR_LBR_CORE_TO;

	pr_cont("8-deep LBR, ");
}
