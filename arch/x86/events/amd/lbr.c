// SPDX-License-Identifier: GPL-2.0
#include <linux/perf_event.h>
#include <asm/perf_event.h>

#include "../perf_event.h"

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
}

static int amd_pmu_lbr_setup_filter(struct perf_event *event)
{
	/* No LBR support */
	if (!x86_pmu.lbr_nr)
		return -EOPNOTSUPP;

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
}

void amd_pmu_lbr_add(struct perf_event *event)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);

	if (!x86_pmu.lbr_nr)
		return;

	perf_sched_cb_inc(event->ctx->pmu);

	if (!cpuc->lbr_users++ && !event->total_time_running)
		amd_pmu_lbr_reset();
}

void amd_pmu_lbr_del(struct perf_event *event)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);

	if (!x86_pmu.lbr_nr)
		return;

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
	u64 dbg_ctl, dbg_extn_cfg;

	if (!cpuc->lbr_users || !x86_pmu.lbr_nr)
		return;

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
