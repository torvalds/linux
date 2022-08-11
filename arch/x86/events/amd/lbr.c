// SPDX-License-Identifier: GPL-2.0
#include <linux/perf_event.h>
#include <asm/perf_event.h>

#include "../perf_event.h"

/* LBR Branch Select valid bits */
#define LBR_SELECT_MASK		0x1ff

/*
 * LBR Branch Select filter bits which when set, ensures that the
 * corresponding type of branches are not recorded
 */
#define LBR_SELECT_KERNEL		0	/* Branches ending in CPL = 0 */
#define LBR_SELECT_USER			1	/* Branches ending in CPL > 0 */
#define LBR_SELECT_JCC			2	/* Conditional branches */
#define LBR_SELECT_CALL_NEAR_REL	3	/* Near relative calls */
#define LBR_SELECT_CALL_NEAR_IND	4	/* Indirect relative calls */
#define LBR_SELECT_RET_NEAR		5	/* Near returns */
#define LBR_SELECT_JMP_NEAR_IND		6	/* Near indirect jumps (excl. calls and returns) */
#define LBR_SELECT_JMP_NEAR_REL		7	/* Near relative jumps (excl. calls) */
#define LBR_SELECT_FAR_BRANCH		8	/* Far branches */

#define LBR_KERNEL	BIT(LBR_SELECT_KERNEL)
#define LBR_USER	BIT(LBR_SELECT_USER)
#define LBR_JCC		BIT(LBR_SELECT_JCC)
#define LBR_REL_CALL	BIT(LBR_SELECT_CALL_NEAR_REL)
#define LBR_IND_CALL	BIT(LBR_SELECT_CALL_NEAR_IND)
#define LBR_RETURN	BIT(LBR_SELECT_RET_NEAR)
#define LBR_REL_JMP	BIT(LBR_SELECT_JMP_NEAR_REL)
#define LBR_IND_JMP	BIT(LBR_SELECT_JMP_NEAR_IND)
#define LBR_FAR		BIT(LBR_SELECT_FAR_BRANCH)
#define LBR_NOT_SUPP	-1	/* unsupported filter */
#define LBR_IGNORE	0

#define LBR_ANY		\
	(LBR_JCC | LBR_REL_CALL | LBR_IND_CALL | LBR_RETURN |	\
	 LBR_REL_JMP | LBR_IND_JMP | LBR_FAR)

struct branch_entry {
	union {
		struct {
			u64	ip:58;
			u64	ip_sign_ext:5;
			u64	mispredict:1;
		} split;
		u64		full;
	} from;

	union {
		struct {
			u64	ip:58;
			u64	ip_sign_ext:3;
			u64	reserved:1;
			u64	spec:1;
			u64	valid:1;
		} split;
		u64		full;
	} to;
};

static __always_inline void amd_pmu_lbr_set_from(unsigned int idx, u64 val)
{
	wrmsrl(MSR_AMD_SAMP_BR_FROM + idx * 2, val);
}

static __always_inline void amd_pmu_lbr_set_to(unsigned int idx, u64 val)
{
	wrmsrl(MSR_AMD_SAMP_BR_FROM + idx * 2 + 1, val);
}

static __always_inline u64 amd_pmu_lbr_get_from(unsigned int idx)
{
	u64 val;

	rdmsrl(MSR_AMD_SAMP_BR_FROM + idx * 2, val);

	return val;
}

static __always_inline u64 amd_pmu_lbr_get_to(unsigned int idx)
{
	u64 val;

	rdmsrl(MSR_AMD_SAMP_BR_FROM + idx * 2 + 1, val);

	return val;
}

static __always_inline u64 sign_ext_branch_ip(u64 ip)
{
	u32 shift = 64 - boot_cpu_data.x86_virt_bits;

	return (u64)(((s64)ip << shift) >> shift);
}

static void amd_pmu_lbr_filter(void)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	int br_sel = cpuc->br_sel, type, i, j;
	bool compress = false;
	u64 from, to;

	/* If sampling all branches, there is nothing to filter */
	if (((br_sel & X86_BR_ALL) == X86_BR_ALL) &&
	    ((br_sel & X86_BR_TYPE_SAVE) != X86_BR_TYPE_SAVE))
		return;

	for (i = 0; i < cpuc->lbr_stack.nr; i++) {
		from = cpuc->lbr_entries[i].from;
		to = cpuc->lbr_entries[i].to;
		type = branch_type(from, to, 0);

		/* If type does not correspond, then discard */
		if (type == X86_BR_NONE || (br_sel & type) != type) {
			cpuc->lbr_entries[i].from = 0;	/* mark invalid */
			compress = true;
		}

		if ((br_sel & X86_BR_TYPE_SAVE) == X86_BR_TYPE_SAVE)
			cpuc->lbr_entries[i].type = common_branch_type(type);
	}

	if (!compress)
		return;

	/* Remove all invalid entries */
	for (i = 0; i < cpuc->lbr_stack.nr; ) {
		if (!cpuc->lbr_entries[i].from) {
			j = i;
			while (++j < cpuc->lbr_stack.nr)
				cpuc->lbr_entries[j - 1] = cpuc->lbr_entries[j];
			cpuc->lbr_stack.nr--;
			if (!cpuc->lbr_entries[i].from)
				continue;
		}
		i++;
	}
}

void amd_pmu_lbr_read(void)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	struct perf_branch_entry *br = cpuc->lbr_entries;
	struct branch_entry entry;
	int out = 0, i;

	if (!cpuc->lbr_users)
		return;

	for (i = 0; i < x86_pmu.lbr_nr; i++) {
		entry.from.full	= amd_pmu_lbr_get_from(i);
		entry.to.full	= amd_pmu_lbr_get_to(i);

		/* Check if a branch has been logged */
		if (!entry.to.split.valid)
			continue;

		perf_clear_branch_entry_bitfields(br + out);

		br[out].from	= sign_ext_branch_ip(entry.from.split.ip);
		br[out].to	= sign_ext_branch_ip(entry.to.split.ip);
		br[out].mispred	= entry.from.split.mispredict;
		br[out].predicted = !br[out].mispred;
		out++;
	}

	cpuc->lbr_stack.nr = out;

	/*
	 * Internal register renaming always ensures that LBR From[0] and
	 * LBR To[0] always represent the TOS
	 */
	cpuc->lbr_stack.hw_idx = 0;

	/* Perform further software filtering */
	amd_pmu_lbr_filter();
}

static const int lbr_select_map[PERF_SAMPLE_BRANCH_MAX_SHIFT] = {
	[PERF_SAMPLE_BRANCH_USER_SHIFT]		= LBR_USER,
	[PERF_SAMPLE_BRANCH_KERNEL_SHIFT]	= LBR_KERNEL,
	[PERF_SAMPLE_BRANCH_HV_SHIFT]		= LBR_IGNORE,

	[PERF_SAMPLE_BRANCH_ANY_SHIFT]		= LBR_ANY,
	[PERF_SAMPLE_BRANCH_ANY_CALL_SHIFT]	= LBR_REL_CALL | LBR_IND_CALL | LBR_FAR,
	[PERF_SAMPLE_BRANCH_ANY_RETURN_SHIFT]	= LBR_RETURN | LBR_FAR,
	[PERF_SAMPLE_BRANCH_IND_CALL_SHIFT]	= LBR_IND_CALL,
	[PERF_SAMPLE_BRANCH_ABORT_TX_SHIFT]	= LBR_NOT_SUPP,
	[PERF_SAMPLE_BRANCH_IN_TX_SHIFT]	= LBR_NOT_SUPP,
	[PERF_SAMPLE_BRANCH_NO_TX_SHIFT]	= LBR_NOT_SUPP,
	[PERF_SAMPLE_BRANCH_COND_SHIFT]		= LBR_JCC,

	[PERF_SAMPLE_BRANCH_CALL_STACK_SHIFT]	= LBR_NOT_SUPP,
	[PERF_SAMPLE_BRANCH_IND_JUMP_SHIFT]	= LBR_IND_JMP,
	[PERF_SAMPLE_BRANCH_CALL_SHIFT]		= LBR_REL_CALL,

	[PERF_SAMPLE_BRANCH_NO_FLAGS_SHIFT]	= LBR_NOT_SUPP,
	[PERF_SAMPLE_BRANCH_NO_CYCLES_SHIFT]	= LBR_NOT_SUPP,
};

static int amd_pmu_lbr_setup_filter(struct perf_event *event)
{
	struct hw_perf_event_extra *reg = &event->hw.branch_reg;
	u64 br_type = event->attr.branch_sample_type;
	u64 mask = 0, v;
	int i;

	/* No LBR support */
	if (!x86_pmu.lbr_nr)
		return -EOPNOTSUPP;

	if (br_type & PERF_SAMPLE_BRANCH_USER)
		mask |= X86_BR_USER;

	if (br_type & PERF_SAMPLE_BRANCH_KERNEL)
		mask |= X86_BR_KERNEL;

	/* Ignore BRANCH_HV here */

	if (br_type & PERF_SAMPLE_BRANCH_ANY)
		mask |= X86_BR_ANY;

	if (br_type & PERF_SAMPLE_BRANCH_ANY_CALL)
		mask |= X86_BR_ANY_CALL;

	if (br_type & PERF_SAMPLE_BRANCH_ANY_RETURN)
		mask |= X86_BR_RET | X86_BR_IRET | X86_BR_SYSRET;

	if (br_type & PERF_SAMPLE_BRANCH_IND_CALL)
		mask |= X86_BR_IND_CALL;

	if (br_type & PERF_SAMPLE_BRANCH_COND)
		mask |= X86_BR_JCC;

	if (br_type & PERF_SAMPLE_BRANCH_IND_JUMP)
		mask |= X86_BR_IND_JMP;

	if (br_type & PERF_SAMPLE_BRANCH_CALL)
		mask |= X86_BR_CALL | X86_BR_ZERO_CALL;

	if (br_type & PERF_SAMPLE_BRANCH_TYPE_SAVE)
		mask |= X86_BR_TYPE_SAVE;

	reg->reg = mask;
	mask = 0;

	for (i = 0; i < PERF_SAMPLE_BRANCH_MAX_SHIFT; i++) {
		if (!(br_type & BIT_ULL(i)))
			continue;

		v = lbr_select_map[i];
		if (v == LBR_NOT_SUPP)
			return -EOPNOTSUPP;

		if (v != LBR_IGNORE)
			mask |= v;
	}

	/* Filter bits operate in suppress mode */
	reg->config = mask ^ LBR_SELECT_MASK;

	return 0;
}

int amd_pmu_lbr_hw_config(struct perf_event *event)
{
	int ret = 0;

	/* LBR is not recommended in counting mode */
	if (!is_sampling_event(event))
		return -EINVAL;

	ret = amd_pmu_lbr_setup_filter(event);
	if (!ret)
		event->attach_state |= PERF_ATTACH_SCHED_CB;

	return ret;
}

void amd_pmu_lbr_reset(void)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	int i;

	if (!x86_pmu.lbr_nr)
		return;

	/* Reset all branch records individually */
	for (i = 0; i < x86_pmu.lbr_nr; i++) {
		amd_pmu_lbr_set_from(i, 0);
		amd_pmu_lbr_set_to(i, 0);
	}

	cpuc->last_task_ctx = NULL;
	cpuc->last_log_id = 0;
	wrmsrl(MSR_AMD64_LBR_SELECT, 0);
}

void amd_pmu_lbr_add(struct perf_event *event)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	struct hw_perf_event_extra *reg = &event->hw.branch_reg;

	if (!x86_pmu.lbr_nr)
		return;

	if (has_branch_stack(event)) {
		cpuc->lbr_select = 1;
		cpuc->lbr_sel->config = reg->config;
		cpuc->br_sel = reg->reg;
	}

	perf_sched_cb_inc(event->ctx->pmu);

	if (!cpuc->lbr_users++ && !event->total_time_running)
		amd_pmu_lbr_reset();
}

void amd_pmu_lbr_del(struct perf_event *event)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);

	if (!x86_pmu.lbr_nr)
		return;

	if (has_branch_stack(event))
		cpuc->lbr_select = 0;

	cpuc->lbr_users--;
	WARN_ON_ONCE(cpuc->lbr_users < 0);
	perf_sched_cb_dec(event->ctx->pmu);
}

void amd_pmu_lbr_sched_task(struct perf_event_context *ctx, bool sched_in)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);

	/*
	 * A context switch can flip the address space and LBR entries are
	 * not tagged with an identifier. Hence, branches cannot be resolved
	 * from the old address space and the LBR records should be wiped.
	 */
	if (cpuc->lbr_users && sched_in)
		amd_pmu_lbr_reset();
}

void amd_pmu_lbr_enable_all(void)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	u64 lbr_select, dbg_ctl, dbg_extn_cfg;

	if (!cpuc->lbr_users || !x86_pmu.lbr_nr)
		return;

	/* Set hardware branch filter */
	if (cpuc->lbr_select) {
		lbr_select = cpuc->lbr_sel->config & LBR_SELECT_MASK;
		wrmsrl(MSR_AMD64_LBR_SELECT, lbr_select);
	}

	rdmsrl(MSR_IA32_DEBUGCTLMSR, dbg_ctl);
	rdmsrl(MSR_AMD_DBG_EXTN_CFG, dbg_extn_cfg);

	wrmsrl(MSR_IA32_DEBUGCTLMSR, dbg_ctl | DEBUGCTLMSR_FREEZE_LBRS_ON_PMI);
	wrmsrl(MSR_AMD_DBG_EXTN_CFG, dbg_extn_cfg | DBG_EXTN_CFG_LBRV2EN);
}

void amd_pmu_lbr_disable_all(void)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	u64 dbg_ctl, dbg_extn_cfg;

	if (!cpuc->lbr_users || !x86_pmu.lbr_nr)
		return;

	rdmsrl(MSR_AMD_DBG_EXTN_CFG, dbg_extn_cfg);
	rdmsrl(MSR_IA32_DEBUGCTLMSR, dbg_ctl);

	wrmsrl(MSR_AMD_DBG_EXTN_CFG, dbg_extn_cfg & ~DBG_EXTN_CFG_LBRV2EN);
	wrmsrl(MSR_IA32_DEBUGCTLMSR, dbg_ctl & ~DEBUGCTLMSR_FREEZE_LBRS_ON_PMI);
}

__init int amd_pmu_lbr_init(void)
{
	union cpuid_0x80000022_ebx ebx;

	if (x86_pmu.version < 2 || !boot_cpu_has(X86_FEATURE_AMD_LBR_V2))
		return -EOPNOTSUPP;

	/* Set number of entries */
	ebx.full = cpuid_ebx(EXT_PERFMON_DEBUG_FEATURES);
	x86_pmu.lbr_nr = ebx.split.lbr_v2_stack_sz;

	pr_cont("%d-deep LBR, ", x86_pmu.lbr_nr);

	return 0;
}
