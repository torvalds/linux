// SPDX-License-Identifier: GPL-2.0
#include <linux/perf_event.h>
#include <linux/types.h>

#include <asm/perf_event.h>
#include <asm/msr.h>

#include "../perf_event.h"

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
#define LBR_CALL_STACK_BIT	9 /* enable call stack */

/*
 * Following bit only exists in Linux; we mask it out before writing it to
 * the actual MSR. But it helps the constraint perf code to understand
 * that this is a separate configuration.
 */
#define LBR_NO_INFO_BIT	       63 /* don't read LBR_INFO. */

#define LBR_KERNEL	(1 << LBR_KERNEL_BIT)
#define LBR_USER	(1 << LBR_USER_BIT)
#define LBR_JCC		(1 << LBR_JCC_BIT)
#define LBR_REL_CALL	(1 << LBR_REL_CALL_BIT)
#define LBR_IND_CALL	(1 << LBR_IND_CALL_BIT)
#define LBR_RETURN	(1 << LBR_RETURN_BIT)
#define LBR_REL_JMP	(1 << LBR_REL_JMP_BIT)
#define LBR_IND_JMP	(1 << LBR_IND_JMP_BIT)
#define LBR_FAR		(1 << LBR_FAR_BIT)
#define LBR_CALL_STACK	(1 << LBR_CALL_STACK_BIT)
#define LBR_NO_INFO	(1ULL << LBR_NO_INFO_BIT)

#define LBR_PLM (LBR_KERNEL | LBR_USER)

#define LBR_SEL_MASK	0x3ff	/* valid bits in LBR_SELECT */
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

#define LBR_FROM_FLAG_MISPRED	BIT_ULL(63)
#define LBR_FROM_FLAG_IN_TX	BIT_ULL(62)
#define LBR_FROM_FLAG_ABORT	BIT_ULL(61)

#define LBR_FROM_SIGNEXT_2MSB	(BIT_ULL(60) | BIT_ULL(59))

/*
 * Intel LBR_CTL bits
 *
 * Hardware branch filter for Arch LBR
 */
#define ARCH_LBR_KERNEL_BIT		1  /* capture at ring0 */
#define ARCH_LBR_USER_BIT		2  /* capture at ring > 0 */
#define ARCH_LBR_CALL_STACK_BIT		3  /* enable call stack */
#define ARCH_LBR_JCC_BIT		16 /* capture conditional branches */
#define ARCH_LBR_REL_JMP_BIT		17 /* capture relative jumps */
#define ARCH_LBR_IND_JMP_BIT		18 /* capture indirect jumps */
#define ARCH_LBR_REL_CALL_BIT		19 /* capture relative calls */
#define ARCH_LBR_IND_CALL_BIT		20 /* capture indirect calls */
#define ARCH_LBR_RETURN_BIT		21 /* capture near returns */
#define ARCH_LBR_OTHER_BRANCH_BIT	22 /* capture other branches */

#define ARCH_LBR_KERNEL			(1ULL << ARCH_LBR_KERNEL_BIT)
#define ARCH_LBR_USER			(1ULL << ARCH_LBR_USER_BIT)
#define ARCH_LBR_CALL_STACK		(1ULL << ARCH_LBR_CALL_STACK_BIT)
#define ARCH_LBR_JCC			(1ULL << ARCH_LBR_JCC_BIT)
#define ARCH_LBR_REL_JMP		(1ULL << ARCH_LBR_REL_JMP_BIT)
#define ARCH_LBR_IND_JMP		(1ULL << ARCH_LBR_IND_JMP_BIT)
#define ARCH_LBR_REL_CALL		(1ULL << ARCH_LBR_REL_CALL_BIT)
#define ARCH_LBR_IND_CALL		(1ULL << ARCH_LBR_IND_CALL_BIT)
#define ARCH_LBR_RETURN			(1ULL << ARCH_LBR_RETURN_BIT)
#define ARCH_LBR_OTHER_BRANCH		(1ULL << ARCH_LBR_OTHER_BRANCH_BIT)

#define ARCH_LBR_ANY			 \
	(ARCH_LBR_JCC			|\
	 ARCH_LBR_REL_JMP		|\
	 ARCH_LBR_IND_JMP		|\
	 ARCH_LBR_REL_CALL		|\
	 ARCH_LBR_IND_CALL		|\
	 ARCH_LBR_RETURN		|\
	 ARCH_LBR_OTHER_BRANCH)

#define ARCH_LBR_CTL_MASK			0x7f000e

static void intel_pmu_lbr_filter(struct cpu_hw_events *cpuc);

static __always_inline bool is_lbr_call_stack_bit_set(u64 config)
{
	if (static_cpu_has(X86_FEATURE_ARCH_LBR))
		return !!(config & ARCH_LBR_CALL_STACK);

	return !!(config & LBR_CALL_STACK);
}

/*
 * We only support LBR implementations that have FREEZE_LBRS_ON_PMI
 * otherwise it becomes near impossible to get a reliable stack.
 */

static void __intel_pmu_lbr_enable(bool pmi)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	u64 debugctl, lbr_select = 0, orig_debugctl;

	/*
	 * No need to unfreeze manually, as v4 can do that as part
	 * of the GLOBAL_STATUS ack.
	 */
	if (pmi && x86_pmu.version >= 4)
		return;

	/*
	 * No need to reprogram LBR_SELECT in a PMI, as it
	 * did not change.
	 */
	if (cpuc->lbr_sel)
		lbr_select = cpuc->lbr_sel->config & x86_pmu.lbr_sel_mask;
	if (!static_cpu_has(X86_FEATURE_ARCH_LBR) && !pmi && cpuc->lbr_sel)
		wrmsrl(MSR_LBR_SELECT, lbr_select);

	rdmsrl(MSR_IA32_DEBUGCTLMSR, debugctl);
	orig_debugctl = debugctl;

	if (!static_cpu_has(X86_FEATURE_ARCH_LBR))
		debugctl |= DEBUGCTLMSR_LBR;
	/*
	 * LBR callstack does not work well with FREEZE_LBRS_ON_PMI.
	 * If FREEZE_LBRS_ON_PMI is set, PMI near call/return instructions
	 * may cause superfluous increase/decrease of LBR_TOS.
	 */
	if (is_lbr_call_stack_bit_set(lbr_select))
		debugctl &= ~DEBUGCTLMSR_FREEZE_LBRS_ON_PMI;
	else
		debugctl |= DEBUGCTLMSR_FREEZE_LBRS_ON_PMI;

	if (orig_debugctl != debugctl)
		wrmsrl(MSR_IA32_DEBUGCTLMSR, debugctl);

	if (static_cpu_has(X86_FEATURE_ARCH_LBR))
		wrmsrl(MSR_ARCH_LBR_CTL, lbr_select | ARCH_LBR_CTL_LBREN);
}

void intel_pmu_lbr_reset_32(void)
{
	int i;

	for (i = 0; i < x86_pmu.lbr_nr; i++)
		wrmsrl(x86_pmu.lbr_from + i, 0);
}

void intel_pmu_lbr_reset_64(void)
{
	int i;

	for (i = 0; i < x86_pmu.lbr_nr; i++) {
		wrmsrl(x86_pmu.lbr_from + i, 0);
		wrmsrl(x86_pmu.lbr_to   + i, 0);
		if (x86_pmu.lbr_has_info)
			wrmsrl(x86_pmu.lbr_info + i, 0);
	}
}

static void intel_pmu_arch_lbr_reset(void)
{
	/* Write to ARCH_LBR_DEPTH MSR, all LBR entries are reset to 0 */
	wrmsrl(MSR_ARCH_LBR_DEPTH, x86_pmu.lbr_nr);
}

void intel_pmu_lbr_reset(void)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);

	if (!x86_pmu.lbr_nr)
		return;

	x86_pmu.lbr_reset();

	cpuc->last_task_ctx = NULL;
	cpuc->last_log_id = 0;
	if (!static_cpu_has(X86_FEATURE_ARCH_LBR) && cpuc->lbr_select)
		wrmsrl(MSR_LBR_SELECT, 0);
}

/*
 * TOS = most recently recorded branch
 */
static inline u64 intel_pmu_lbr_tos(void)
{
	u64 tos;

	rdmsrl(x86_pmu.lbr_tos, tos);
	return tos;
}

enum {
	LBR_NONE,
	LBR_VALID,
};

/*
 * For format LBR_FORMAT_EIP_FLAGS2, bits 61:62 in MSR_LAST_BRANCH_FROM_x
 * are the TSX flags when TSX is supported, but when TSX is not supported
 * they have no consistent behavior:
 *
 *   - For wrmsr(), bits 61:62 are considered part of the sign extension.
 *   - For HW updates (branch captures) bits 61:62 are always OFF and are not
 *     part of the sign extension.
 *
 * Therefore, if:
 *
 *   1) LBR format LBR_FORMAT_EIP_FLAGS2
 *   2) CPU has no TSX support enabled
 *
 * ... then any value passed to wrmsr() must be sign extended to 63 bits and any
 * value from rdmsr() must be converted to have a 61 bits sign extension,
 * ignoring the TSX flags.
 */
static inline bool lbr_from_signext_quirk_needed(void)
{
	bool tsx_support = boot_cpu_has(X86_FEATURE_HLE) ||
			   boot_cpu_has(X86_FEATURE_RTM);

	return !tsx_support;
}

static DEFINE_STATIC_KEY_FALSE(lbr_from_quirk_key);

/* If quirk is enabled, ensure sign extension is 63 bits: */
inline u64 lbr_from_signext_quirk_wr(u64 val)
{
	if (static_branch_unlikely(&lbr_from_quirk_key)) {
		/*
		 * Sign extend into bits 61:62 while preserving bit 63.
		 *
		 * Quirk is enabled when TSX is disabled. Therefore TSX bits
		 * in val are always OFF and must be changed to be sign
		 * extension bits. Since bits 59:60 are guaranteed to be
		 * part of the sign extension bits, we can just copy them
		 * to 61:62.
		 */
		val |= (LBR_FROM_SIGNEXT_2MSB & val) << 2;
	}
	return val;
}

/*
 * If quirk is needed, ensure sign extension is 61 bits:
 */
static u64 lbr_from_signext_quirk_rd(u64 val)
{
	if (static_branch_unlikely(&lbr_from_quirk_key)) {
		/*
		 * Quirk is on when TSX is not enabled. Therefore TSX
		 * flags must be read as OFF.
		 */
		val &= ~(LBR_FROM_FLAG_IN_TX | LBR_FROM_FLAG_ABORT);
	}
	return val;
}

static __always_inline void wrlbr_from(unsigned int idx, u64 val)
{
	val = lbr_from_signext_quirk_wr(val);
	wrmsrl(x86_pmu.lbr_from + idx, val);
}

static __always_inline void wrlbr_to(unsigned int idx, u64 val)
{
	wrmsrl(x86_pmu.lbr_to + idx, val);
}

static __always_inline void wrlbr_info(unsigned int idx, u64 val)
{
	wrmsrl(x86_pmu.lbr_info + idx, val);
}

static __always_inline u64 rdlbr_from(unsigned int idx, struct lbr_entry *lbr)
{
	u64 val;

	if (lbr)
		return lbr->from;

	rdmsrl(x86_pmu.lbr_from + idx, val);

	return lbr_from_signext_quirk_rd(val);
}

static __always_inline u64 rdlbr_to(unsigned int idx, struct lbr_entry *lbr)
{
	u64 val;

	if (lbr)
		return lbr->to;

	rdmsrl(x86_pmu.lbr_to + idx, val);

	return val;
}

static __always_inline u64 rdlbr_info(unsigned int idx, struct lbr_entry *lbr)
{
	u64 val;

	if (lbr)
		return lbr->info;

	rdmsrl(x86_pmu.lbr_info + idx, val);

	return val;
}

static inline void
wrlbr_all(struct lbr_entry *lbr, unsigned int idx, bool need_info)
{
	wrlbr_from(idx, lbr->from);
	wrlbr_to(idx, lbr->to);
	if (need_info)
		wrlbr_info(idx, lbr->info);
}

static inline bool
rdlbr_all(struct lbr_entry *lbr, unsigned int idx, bool need_info)
{
	u64 from = rdlbr_from(idx, NULL);

	/* Don't read invalid entry */
	if (!from)
		return false;

	lbr->from = from;
	lbr->to = rdlbr_to(idx, NULL);
	if (need_info)
		lbr->info = rdlbr_info(idx, NULL);

	return true;
}

void intel_pmu_lbr_restore(void *ctx)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	struct x86_perf_task_context *task_ctx = ctx;
	bool need_info = x86_pmu.lbr_has_info;
	u64 tos = task_ctx->tos;
	unsigned lbr_idx, mask;
	int i;

	mask = x86_pmu.lbr_nr - 1;
	for (i = 0; i < task_ctx->valid_lbrs; i++) {
		lbr_idx = (tos - i) & mask;
		wrlbr_all(&task_ctx->lbr[i], lbr_idx, need_info);
	}

	for (; i < x86_pmu.lbr_nr; i++) {
		lbr_idx = (tos - i) & mask;
		wrlbr_from(lbr_idx, 0);
		wrlbr_to(lbr_idx, 0);
		if (need_info)
			wrlbr_info(lbr_idx, 0);
	}

	wrmsrl(x86_pmu.lbr_tos, tos);

	if (cpuc->lbr_select)
		wrmsrl(MSR_LBR_SELECT, task_ctx->lbr_sel);
}

static void intel_pmu_arch_lbr_restore(void *ctx)
{
	struct x86_perf_task_context_arch_lbr *task_ctx = ctx;
	struct lbr_entry *entries = task_ctx->entries;
	int i;

	/* Fast reset the LBRs before restore if the call stack is not full. */
	if (!entries[x86_pmu.lbr_nr - 1].from)
		intel_pmu_arch_lbr_reset();

	for (i = 0; i < x86_pmu.lbr_nr; i++) {
		if (!entries[i].from)
			break;
		wrlbr_all(&entries[i], i, true);
	}
}

/*
 * Restore the Architecture LBR state from the xsave area in the perf
 * context data for the task via the XRSTORS instruction.
 */
static void intel_pmu_arch_lbr_xrstors(void *ctx)
{
	struct x86_perf_task_context_arch_lbr_xsave *task_ctx = ctx;

	xrstors(&task_ctx->xsave, XFEATURE_MASK_LBR);
}

static __always_inline bool lbr_is_reset_in_cstate(void *ctx)
{
	if (static_cpu_has(X86_FEATURE_ARCH_LBR))
		return x86_pmu.lbr_deep_c_reset && !rdlbr_from(0, NULL);

	return !rdlbr_from(((struct x86_perf_task_context *)ctx)->tos, NULL);
}

static void __intel_pmu_lbr_restore(void *ctx)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);

	if (task_context_opt(ctx)->lbr_callstack_users == 0 ||
	    task_context_opt(ctx)->lbr_stack_state == LBR_NONE) {
		intel_pmu_lbr_reset();
		return;
	}

	/*
	 * Does not restore the LBR registers, if
	 * - No one else touched them, and
	 * - Was not cleared in Cstate
	 */
	if ((ctx == cpuc->last_task_ctx) &&
	    (task_context_opt(ctx)->log_id == cpuc->last_log_id) &&
	    !lbr_is_reset_in_cstate(ctx)) {
		task_context_opt(ctx)->lbr_stack_state = LBR_NONE;
		return;
	}

	x86_pmu.lbr_restore(ctx);

	task_context_opt(ctx)->lbr_stack_state = LBR_NONE;
}

void intel_pmu_lbr_save(void *ctx)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	struct x86_perf_task_context *task_ctx = ctx;
	bool need_info = x86_pmu.lbr_has_info;
	unsigned lbr_idx, mask;
	u64 tos;
	int i;

	mask = x86_pmu.lbr_nr - 1;
	tos = intel_pmu_lbr_tos();
	for (i = 0; i < x86_pmu.lbr_nr; i++) {
		lbr_idx = (tos - i) & mask;
		if (!rdlbr_all(&task_ctx->lbr[i], lbr_idx, need_info))
			break;
	}
	task_ctx->valid_lbrs = i;
	task_ctx->tos = tos;

	if (cpuc->lbr_select)
		rdmsrl(MSR_LBR_SELECT, task_ctx->lbr_sel);
}

static void intel_pmu_arch_lbr_save(void *ctx)
{
	struct x86_perf_task_context_arch_lbr *task_ctx = ctx;
	struct lbr_entry *entries = task_ctx->entries;
	int i;

	for (i = 0; i < x86_pmu.lbr_nr; i++) {
		if (!rdlbr_all(&entries[i], i, true))
			break;
	}

	/* LBR call stack is not full. Reset is required in restore. */
	if (i < x86_pmu.lbr_nr)
		entries[x86_pmu.lbr_nr - 1].from = 0;
}

/*
 * Save the Architecture LBR state to the xsave area in the perf
 * context data for the task via the XSAVES instruction.
 */
static void intel_pmu_arch_lbr_xsaves(void *ctx)
{
	struct x86_perf_task_context_arch_lbr_xsave *task_ctx = ctx;

	xsaves(&task_ctx->xsave, XFEATURE_MASK_LBR);
}

static void __intel_pmu_lbr_save(void *ctx)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);

	if (task_context_opt(ctx)->lbr_callstack_users == 0) {
		task_context_opt(ctx)->lbr_stack_state = LBR_NONE;
		return;
	}

	x86_pmu.lbr_save(ctx);

	task_context_opt(ctx)->lbr_stack_state = LBR_VALID;

	cpuc->last_task_ctx = ctx;
	cpuc->last_log_id = ++task_context_opt(ctx)->log_id;
}

void intel_pmu_lbr_swap_task_ctx(struct perf_event_pmu_context *prev_epc,
				 struct perf_event_pmu_context *next_epc)
{
	void *prev_ctx_data, *next_ctx_data;

	swap(prev_epc->task_ctx_data, next_epc->task_ctx_data);

	/*
	 * Architecture specific synchronization makes sense in case
	 * both prev_epc->task_ctx_data and next_epc->task_ctx_data
	 * pointers are allocated.
	 */

	prev_ctx_data = next_epc->task_ctx_data;
	next_ctx_data = prev_epc->task_ctx_data;

	if (!prev_ctx_data || !next_ctx_data)
		return;

	swap(task_context_opt(prev_ctx_data)->lbr_callstack_users,
	     task_context_opt(next_ctx_data)->lbr_callstack_users);
}

void intel_pmu_lbr_sched_task(struct perf_event_pmu_context *pmu_ctx, bool sched_in)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	void *task_ctx;

	if (!cpuc->lbr_users)
		return;

	/*
	 * If LBR callstack feature is enabled and the stack was saved when
	 * the task was scheduled out, restore the stack. Otherwise flush
	 * the LBR stack.
	 */
	task_ctx = pmu_ctx ? pmu_ctx->task_ctx_data : NULL;
	if (task_ctx) {
		if (sched_in)
			__intel_pmu_lbr_restore(task_ctx);
		else
			__intel_pmu_lbr_save(task_ctx);
		return;
	}

	/*
	 * Since a context switch can flip the address space and LBR entries
	 * are not tagged with an identifier, we need to wipe the LBR, even for
	 * per-cpu events. You simply cannot resolve the branches from the old
	 * address space.
	 */
	if (sched_in)
		intel_pmu_lbr_reset();
}

static inline bool branch_user_callstack(unsigned br_sel)
{
	return (br_sel & X86_BR_USER) && (br_sel & X86_BR_CALL_STACK);
}

void intel_pmu_lbr_add(struct perf_event *event)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);

	if (!x86_pmu.lbr_nr)
		return;

	if (event->hw.flags & PERF_X86_EVENT_LBR_SELECT)
		cpuc->lbr_select = 1;

	cpuc->br_sel = event->hw.branch_reg.reg;

	if (branch_user_callstack(cpuc->br_sel) && event->pmu_ctx->task_ctx_data)
		task_context_opt(event->pmu_ctx->task_ctx_data)->lbr_callstack_users++;

	/*
	 * Request pmu::sched_task() callback, which will fire inside the
	 * regular perf event scheduling, so that call will:
	 *
	 *  - restore or wipe; when LBR-callstack,
	 *  - wipe; otherwise,
	 *
	 * when this is from __perf_event_task_sched_in().
	 *
	 * However, if this is from perf_install_in_context(), no such callback
	 * will follow and we'll need to reset the LBR here if this is the
	 * first LBR event.
	 *
	 * The problem is, we cannot tell these cases apart... but we can
	 * exclude the biggest chunk of cases by looking at
	 * event->total_time_running. An event that has accrued runtime cannot
	 * be 'new'. Conversely, a new event can get installed through the
	 * context switch path for the first time.
	 */
	if (x86_pmu.intel_cap.pebs_baseline && event->attr.precise_ip > 0)
		cpuc->lbr_pebs_users++;
	perf_sched_cb_inc(event->pmu);
	if (!cpuc->lbr_users++ && !event->total_time_running)
		intel_pmu_lbr_reset();
}

void release_lbr_buffers(void)
{
	struct kmem_cache *kmem_cache;
	struct cpu_hw_events *cpuc;
	int cpu;

	if (!static_cpu_has(X86_FEATURE_ARCH_LBR))
		return;

	for_each_possible_cpu(cpu) {
		cpuc = per_cpu_ptr(&cpu_hw_events, cpu);
		kmem_cache = x86_get_pmu(cpu)->task_ctx_cache;
		if (kmem_cache && cpuc->lbr_xsave) {
			kmem_cache_free(kmem_cache, cpuc->lbr_xsave);
			cpuc->lbr_xsave = NULL;
		}
	}
}

void reserve_lbr_buffers(void)
{
	struct kmem_cache *kmem_cache;
	struct cpu_hw_events *cpuc;
	int cpu;

	if (!static_cpu_has(X86_FEATURE_ARCH_LBR))
		return;

	for_each_possible_cpu(cpu) {
		cpuc = per_cpu_ptr(&cpu_hw_events, cpu);
		kmem_cache = x86_get_pmu(cpu)->task_ctx_cache;
		if (!kmem_cache || cpuc->lbr_xsave)
			continue;

		cpuc->lbr_xsave = kmem_cache_alloc_node(kmem_cache,
							GFP_KERNEL | __GFP_ZERO,
							cpu_to_node(cpu));
	}
}

void intel_pmu_lbr_del(struct perf_event *event)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);

	if (!x86_pmu.lbr_nr)
		return;

	if (branch_user_callstack(cpuc->br_sel) &&
	    event->pmu_ctx->task_ctx_data)
		task_context_opt(event->pmu_ctx->task_ctx_data)->lbr_callstack_users--;

	if (event->hw.flags & PERF_X86_EVENT_LBR_SELECT)
		cpuc->lbr_select = 0;

	if (x86_pmu.intel_cap.pebs_baseline && event->attr.precise_ip > 0)
		cpuc->lbr_pebs_users--;
	cpuc->lbr_users--;
	WARN_ON_ONCE(cpuc->lbr_users < 0);
	WARN_ON_ONCE(cpuc->lbr_pebs_users < 0);
	perf_sched_cb_dec(event->pmu);

	/*
	 * The logged occurrences information is only valid for the
	 * current LBR group. If another LBR group is scheduled in
	 * later, the information from the stale LBRs will be wrongly
	 * interpreted. Reset the LBRs here.
	 *
	 * Only clear once for a branch counter group with the leader
	 * event. Because
	 * - Cannot simply reset the LBRs with the !cpuc->lbr_users.
	 *   Because it's possible that the last LBR user is not in a
	 *   branch counter group, e.g., a branch_counters group +
	 *   several normal LBR events.
	 * - The LBR reset can be done with any one of the events in a
	 *   branch counter group, since they are always scheduled together.
	 *   It's easy to force the leader event an LBR event.
	 */
	if (is_branch_counters_group(event) && event == event->group_leader)
		intel_pmu_lbr_reset();
}

static inline bool vlbr_exclude_host(void)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);

	return test_bit(INTEL_PMC_IDX_FIXED_VLBR,
		(unsigned long *)&cpuc->intel_ctrl_guest_mask);
}

void intel_pmu_lbr_enable_all(bool pmi)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);

	if (cpuc->lbr_users && !vlbr_exclude_host())
		__intel_pmu_lbr_enable(pmi);
}

void intel_pmu_lbr_disable_all(void)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);

	if (cpuc->lbr_users && !vlbr_exclude_host()) {
		if (static_cpu_has(X86_FEATURE_ARCH_LBR))
			return __intel_pmu_arch_lbr_disable();

		__intel_pmu_lbr_disable();
	}
}

void intel_pmu_lbr_read_32(struct cpu_hw_events *cpuc)
{
	unsigned long mask = x86_pmu.lbr_nr - 1;
	struct perf_branch_entry *br = cpuc->lbr_entries;
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

		perf_clear_branch_entry_bitfields(br);

		br->from	= msr_lastbranch.from;
		br->to		= msr_lastbranch.to;
		br++;
	}
	cpuc->lbr_stack.nr = i;
	cpuc->lbr_stack.hw_idx = tos;
}

/*
 * Due to lack of segmentation in Linux the effective address (offset)
 * is the same as the linear address, allowing us to merge the LIP and EIP
 * LBR formats.
 */
void intel_pmu_lbr_read_64(struct cpu_hw_events *cpuc)
{
	bool need_info = false, call_stack = false;
	unsigned long mask = x86_pmu.lbr_nr - 1;
	struct perf_branch_entry *br = cpuc->lbr_entries;
	u64 tos = intel_pmu_lbr_tos();
	int i;
	int out = 0;
	int num = x86_pmu.lbr_nr;

	if (cpuc->lbr_sel) {
		need_info = !(cpuc->lbr_sel->config & LBR_NO_INFO);
		if (cpuc->lbr_sel->config & LBR_CALL_STACK)
			call_stack = true;
	}

	for (i = 0; i < num; i++) {
		unsigned long lbr_idx = (tos - i) & mask;
		u64 from, to, mis = 0, pred = 0, in_tx = 0, abort = 0;
		u16 cycles = 0;

		from = rdlbr_from(lbr_idx, NULL);
		to   = rdlbr_to(lbr_idx, NULL);

		/*
		 * Read LBR call stack entries
		 * until invalid entry (0s) is detected.
		 */
		if (call_stack && !from)
			break;

		if (x86_pmu.lbr_has_info) {
			if (need_info) {
				u64 info;

				info = rdlbr_info(lbr_idx, NULL);
				mis = !!(info & LBR_INFO_MISPRED);
				pred = !mis;
				cycles = (info & LBR_INFO_CYCLES);
				if (x86_pmu.lbr_has_tsx) {
					in_tx = !!(info & LBR_INFO_IN_TX);
					abort = !!(info & LBR_INFO_ABORT);
				}
			}
		} else {
			int skip = 0;

			if (x86_pmu.lbr_from_flags) {
				mis = !!(from & LBR_FROM_FLAG_MISPRED);
				pred = !mis;
				skip = 1;
			}
			if (x86_pmu.lbr_has_tsx) {
				in_tx = !!(from & LBR_FROM_FLAG_IN_TX);
				abort = !!(from & LBR_FROM_FLAG_ABORT);
				skip = 3;
			}
			from = (u64)((((s64)from) << skip) >> skip);

			if (x86_pmu.lbr_to_cycles) {
				cycles = ((to >> 48) & LBR_INFO_CYCLES);
				to = (u64)((((s64)to) << 16) >> 16);
			}
		}

		/*
		 * Some CPUs report duplicated abort records,
		 * with the second entry not having an abort bit set.
		 * Skip them here. This loop runs backwards,
		 * so we need to undo the previous record.
		 * If the abort just happened outside the window
		 * the extra entry cannot be removed.
		 */
		if (abort && x86_pmu.lbr_double_abort && out > 0)
			out--;

		perf_clear_branch_entry_bitfields(br+out);
		br[out].from	 = from;
		br[out].to	 = to;
		br[out].mispred	 = mis;
		br[out].predicted = pred;
		br[out].in_tx	 = in_tx;
		br[out].abort	 = abort;
		br[out].cycles	 = cycles;
		out++;
	}
	cpuc->lbr_stack.nr = out;
	cpuc->lbr_stack.hw_idx = tos;
}

static DEFINE_STATIC_KEY_FALSE(x86_lbr_mispred);
static DEFINE_STATIC_KEY_FALSE(x86_lbr_cycles);
static DEFINE_STATIC_KEY_FALSE(x86_lbr_type);

static __always_inline int get_lbr_br_type(u64 info)
{
	int type = 0;

	if (static_branch_likely(&x86_lbr_type))
		type = (info & LBR_INFO_BR_TYPE) >> LBR_INFO_BR_TYPE_OFFSET;

	return type;
}

static __always_inline bool get_lbr_mispred(u64 info)
{
	bool mispred = 0;

	if (static_branch_likely(&x86_lbr_mispred))
		mispred = !!(info & LBR_INFO_MISPRED);

	return mispred;
}

static __always_inline u16 get_lbr_cycles(u64 info)
{
	u16 cycles = info & LBR_INFO_CYCLES;

	if (static_cpu_has(X86_FEATURE_ARCH_LBR) &&
	    (!static_branch_likely(&x86_lbr_cycles) ||
	     !(info & LBR_INFO_CYC_CNT_VALID)))
		cycles = 0;

	return cycles;
}

static_assert((64 - PERF_BRANCH_ENTRY_INFO_BITS_MAX) > LBR_INFO_BR_CNTR_NUM * LBR_INFO_BR_CNTR_BITS);

static void intel_pmu_store_lbr(struct cpu_hw_events *cpuc,
				struct lbr_entry *entries)
{
	struct perf_branch_entry *e;
	struct lbr_entry *lbr;
	u64 from, to, info;
	int i;

	for (i = 0; i < x86_pmu.lbr_nr; i++) {
		lbr = entries ? &entries[i] : NULL;
		e = &cpuc->lbr_entries[i];

		from = rdlbr_from(i, lbr);
		/*
		 * Read LBR entries until invalid entry (0s) is detected.
		 */
		if (!from)
			break;

		to = rdlbr_to(i, lbr);
		info = rdlbr_info(i, lbr);

		perf_clear_branch_entry_bitfields(e);

		e->from		= from;
		e->to		= to;
		e->mispred	= get_lbr_mispred(info);
		e->predicted	= !e->mispred;
		e->in_tx	= !!(info & LBR_INFO_IN_TX);
		e->abort	= !!(info & LBR_INFO_ABORT);
		e->cycles	= get_lbr_cycles(info);
		e->type		= get_lbr_br_type(info);

		/*
		 * Leverage the reserved field of cpuc->lbr_entries[i] to
		 * temporarily store the branch counters information.
		 * The later code will decide what content can be disclosed
		 * to the perf tool. Pleae see intel_pmu_lbr_counters_reorder().
		 */
		e->reserved	= (info >> LBR_INFO_BR_CNTR_OFFSET) & LBR_INFO_BR_CNTR_FULL_MASK;
	}

	cpuc->lbr_stack.nr = i;
}

/*
 * The enabled order may be different from the counter order.
 * Update the lbr_counters with the enabled order.
 */
static void intel_pmu_lbr_counters_reorder(struct cpu_hw_events *cpuc,
					   struct perf_event *event)
{
	int i, j, pos = 0, order[X86_PMC_IDX_MAX];
	struct perf_event *leader, *sibling;
	u64 src, dst, cnt;

	leader = event->group_leader;
	if (branch_sample_counters(leader))
		order[pos++] = leader->hw.idx;

	for_each_sibling_event(sibling, leader) {
		if (!branch_sample_counters(sibling))
			continue;
		order[pos++] = sibling->hw.idx;
	}

	WARN_ON_ONCE(!pos);

	for (i = 0; i < cpuc->lbr_stack.nr; i++) {
		src = cpuc->lbr_entries[i].reserved;
		dst = 0;
		for (j = 0; j < pos; j++) {
			cnt = (src >> (order[j] * LBR_INFO_BR_CNTR_BITS)) & LBR_INFO_BR_CNTR_MASK;
			dst |= cnt << j * LBR_INFO_BR_CNTR_BITS;
		}
		cpuc->lbr_counters[i] = dst;
		cpuc->lbr_entries[i].reserved = 0;
	}
}

void intel_pmu_lbr_save_brstack(struct perf_sample_data *data,
				struct cpu_hw_events *cpuc,
				struct perf_event *event)
{
	if (is_branch_counters_group(event)) {
		intel_pmu_lbr_counters_reorder(cpuc, event);
		perf_sample_save_brstack(data, event, &cpuc->lbr_stack, cpuc->lbr_counters);
		return;
	}

	perf_sample_save_brstack(data, event, &cpuc->lbr_stack, NULL);
}

static void intel_pmu_arch_lbr_read(struct cpu_hw_events *cpuc)
{
	intel_pmu_store_lbr(cpuc, NULL);
}

static void intel_pmu_arch_lbr_read_xsave(struct cpu_hw_events *cpuc)
{
	struct x86_perf_task_context_arch_lbr_xsave *xsave = cpuc->lbr_xsave;

	if (!xsave) {
		intel_pmu_store_lbr(cpuc, NULL);
		return;
	}
	xsaves(&xsave->xsave, XFEATURE_MASK_LBR);

	intel_pmu_store_lbr(cpuc, xsave->lbr.entries);
}

void intel_pmu_lbr_read(void)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);

	/*
	 * Don't read when all LBRs users are using adaptive PEBS.
	 *
	 * This could be smarter and actually check the event,
	 * but this simple approach seems to work for now.
	 */
	if (!cpuc->lbr_users || vlbr_exclude_host() ||
	    cpuc->lbr_users == cpuc->lbr_pebs_users)
		return;

	x86_pmu.lbr_read(cpuc);

	intel_pmu_lbr_filter(cpuc);
}

/*
 * SW filter is used:
 * - in case there is no HW filter
 * - in case the HW filter has errata or limitations
 */
static int intel_pmu_setup_sw_lbr_filter(struct perf_event *event)
{
	u64 br_type = event->attr.branch_sample_type;
	int mask = 0;

	if (br_type & PERF_SAMPLE_BRANCH_USER)
		mask |= X86_BR_USER;

	if (br_type & PERF_SAMPLE_BRANCH_KERNEL)
		mask |= X86_BR_KERNEL;

	/* we ignore BRANCH_HV here */

	if (br_type & PERF_SAMPLE_BRANCH_ANY)
		mask |= X86_BR_ANY;

	if (br_type & PERF_SAMPLE_BRANCH_ANY_CALL)
		mask |= X86_BR_ANY_CALL;

	if (br_type & PERF_SAMPLE_BRANCH_ANY_RETURN)
		mask |= X86_BR_RET | X86_BR_IRET | X86_BR_SYSRET;

	if (br_type & PERF_SAMPLE_BRANCH_IND_CALL)
		mask |= X86_BR_IND_CALL;

	if (br_type & PERF_SAMPLE_BRANCH_ABORT_TX)
		mask |= X86_BR_ABORT;

	if (br_type & PERF_SAMPLE_BRANCH_IN_TX)
		mask |= X86_BR_IN_TX;

	if (br_type & PERF_SAMPLE_BRANCH_NO_TX)
		mask |= X86_BR_NO_TX;

	if (br_type & PERF_SAMPLE_BRANCH_COND)
		mask |= X86_BR_JCC;

	if (br_type & PERF_SAMPLE_BRANCH_CALL_STACK) {
		if (!x86_pmu_has_lbr_callstack())
			return -EOPNOTSUPP;
		if (mask & ~(X86_BR_USER | X86_BR_KERNEL))
			return -EINVAL;
		mask |= X86_BR_CALL | X86_BR_IND_CALL | X86_BR_RET |
			X86_BR_CALL_STACK;
	}

	if (br_type & PERF_SAMPLE_BRANCH_IND_JUMP)
		mask |= X86_BR_IND_JMP;

	if (br_type & PERF_SAMPLE_BRANCH_CALL)
		mask |= X86_BR_CALL | X86_BR_ZERO_CALL;

	if (br_type & PERF_SAMPLE_BRANCH_TYPE_SAVE)
		mask |= X86_BR_TYPE_SAVE;

	/*
	 * stash actual user request into reg, it may
	 * be used by fixup code for some CPU
	 */
	event->hw.branch_reg.reg = mask;
	return 0;
}

/*
 * setup the HW LBR filter
 * Used only when available, may not be enough to disambiguate
 * all branches, may need the help of the SW filter
 */
static int intel_pmu_setup_hw_lbr_filter(struct perf_event *event)
{
	struct hw_perf_event_extra *reg;
	u64 br_type = event->attr.branch_sample_type;
	u64 mask = 0, v;
	int i;

	for (i = 0; i < PERF_SAMPLE_BRANCH_MAX_SHIFT; i++) {
		if (!(br_type & (1ULL << i)))
			continue;

		v = x86_pmu.lbr_sel_map[i];
		if (v == LBR_NOT_SUPP)
			return -EOPNOTSUPP;

		if (v != LBR_IGN)
			mask |= v;
	}

	reg = &event->hw.branch_reg;
	reg->idx = EXTRA_REG_LBR;

	if (static_cpu_has(X86_FEATURE_ARCH_LBR)) {
		reg->config = mask;

		/*
		 * The Arch LBR HW can retrieve the common branch types
		 * from the LBR_INFO. It doesn't require the high overhead
		 * SW disassemble.
		 * Enable the branch type by default for the Arch LBR.
		 */
		reg->reg |= X86_BR_TYPE_SAVE;
		return 0;
	}

	/*
	 * The first 9 bits (LBR_SEL_MASK) in LBR_SELECT operate
	 * in suppress mode. So LBR_SELECT should be set to
	 * (~mask & LBR_SEL_MASK) | (mask & ~LBR_SEL_MASK)
	 * But the 10th bit LBR_CALL_STACK does not operate
	 * in suppress mode.
	 */
	reg->config = mask ^ (x86_pmu.lbr_sel_mask & ~LBR_CALL_STACK);

	if ((br_type & PERF_SAMPLE_BRANCH_NO_CYCLES) &&
	    (br_type & PERF_SAMPLE_BRANCH_NO_FLAGS) &&
	    x86_pmu.lbr_has_info)
		reg->config |= LBR_NO_INFO;

	return 0;
}

int intel_pmu_setup_lbr_filter(struct perf_event *event)
{
	int ret = 0;

	/*
	 * no LBR on this PMU
	 */
	if (!x86_pmu.lbr_nr)
		return -EOPNOTSUPP;

	/*
	 * setup SW LBR filter
	 */
	ret = intel_pmu_setup_sw_lbr_filter(event);
	if (ret)
		return ret;

	/*
	 * setup HW LBR filter, if any
	 */
	if (x86_pmu.lbr_sel_map)
		ret = intel_pmu_setup_hw_lbr_filter(event);

	return ret;
}

enum {
	ARCH_LBR_BR_TYPE_JCC			= 0,
	ARCH_LBR_BR_TYPE_NEAR_IND_JMP		= 1,
	ARCH_LBR_BR_TYPE_NEAR_REL_JMP		= 2,
	ARCH_LBR_BR_TYPE_NEAR_IND_CALL		= 3,
	ARCH_LBR_BR_TYPE_NEAR_REL_CALL		= 4,
	ARCH_LBR_BR_TYPE_NEAR_RET		= 5,
	ARCH_LBR_BR_TYPE_KNOWN_MAX		= ARCH_LBR_BR_TYPE_NEAR_RET,

	ARCH_LBR_BR_TYPE_MAP_MAX		= 16,
};

static const int arch_lbr_br_type_map[ARCH_LBR_BR_TYPE_MAP_MAX] = {
	[ARCH_LBR_BR_TYPE_JCC]			= X86_BR_JCC,
	[ARCH_LBR_BR_TYPE_NEAR_IND_JMP]		= X86_BR_IND_JMP,
	[ARCH_LBR_BR_TYPE_NEAR_REL_JMP]		= X86_BR_JMP,
	[ARCH_LBR_BR_TYPE_NEAR_IND_CALL]	= X86_BR_IND_CALL,
	[ARCH_LBR_BR_TYPE_NEAR_REL_CALL]	= X86_BR_CALL,
	[ARCH_LBR_BR_TYPE_NEAR_RET]		= X86_BR_RET,
};

/*
 * implement actual branch filter based on user demand.
 * Hardware may not exactly satisfy that request, thus
 * we need to inspect opcodes. Mismatched branches are
 * discarded. Therefore, the number of branches returned
 * in PERF_SAMPLE_BRANCH_STACK sample may vary.
 */
static void
intel_pmu_lbr_filter(struct cpu_hw_events *cpuc)
{
	u64 from, to;
	int br_sel = cpuc->br_sel;
	int i, j, type, to_plm;
	bool compress = false;

	/* if sampling all branches, then nothing to filter */
	if (((br_sel & X86_BR_ALL) == X86_BR_ALL) &&
	    ((br_sel & X86_BR_TYPE_SAVE) != X86_BR_TYPE_SAVE))
		return;

	for (i = 0; i < cpuc->lbr_stack.nr; i++) {

		from = cpuc->lbr_entries[i].from;
		to = cpuc->lbr_entries[i].to;
		type = cpuc->lbr_entries[i].type;

		/*
		 * Parse the branch type recorded in LBR_x_INFO MSR.
		 * Doesn't support OTHER_BRANCH decoding for now.
		 * OTHER_BRANCH branch type still rely on software decoding.
		 */
		if (static_cpu_has(X86_FEATURE_ARCH_LBR) &&
		    type <= ARCH_LBR_BR_TYPE_KNOWN_MAX) {
			to_plm = kernel_ip(to) ? X86_BR_KERNEL : X86_BR_USER;
			type = arch_lbr_br_type_map[type] | to_plm;
		} else
			type = branch_type(from, to, cpuc->lbr_entries[i].abort);
		if (type != X86_BR_NONE && (br_sel & X86_BR_ANYTX)) {
			if (cpuc->lbr_entries[i].in_tx)
				type |= X86_BR_IN_TX;
			else
				type |= X86_BR_NO_TX;
		}

		/* if type does not correspond, then discard */
		if (type == X86_BR_NONE || (br_sel & type) != type) {
			cpuc->lbr_entries[i].from = 0;
			compress = true;
		}

		if ((br_sel & X86_BR_TYPE_SAVE) == X86_BR_TYPE_SAVE)
			cpuc->lbr_entries[i].type = common_branch_type(type);
	}

	if (!compress)
		return;

	/* remove all entries with from=0 */
	for (i = 0; i < cpuc->lbr_stack.nr; ) {
		if (!cpuc->lbr_entries[i].from) {
			j = i;
			while (++j < cpuc->lbr_stack.nr) {
				cpuc->lbr_entries[j-1] = cpuc->lbr_entries[j];
				cpuc->lbr_counters[j-1] = cpuc->lbr_counters[j];
			}
			cpuc->lbr_stack.nr--;
			if (!cpuc->lbr_entries[i].from)
				continue;
		}
		i++;
	}
}

void intel_pmu_store_pebs_lbrs(struct lbr_entry *lbr)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);

	/* Cannot get TOS for large PEBS and Arch LBR */
	if (static_cpu_has(X86_FEATURE_ARCH_LBR) ||
	    (cpuc->n_pebs == cpuc->n_large_pebs))
		cpuc->lbr_stack.hw_idx = -1ULL;
	else
		cpuc->lbr_stack.hw_idx = intel_pmu_lbr_tos();

	intel_pmu_store_lbr(cpuc, lbr);
	intel_pmu_lbr_filter(cpuc);
}

/*
 * Map interface branch filters onto LBR filters
 */
static const int nhm_lbr_sel_map[PERF_SAMPLE_BRANCH_MAX_SHIFT] = {
	[PERF_SAMPLE_BRANCH_ANY_SHIFT]		= LBR_ANY,
	[PERF_SAMPLE_BRANCH_USER_SHIFT]		= LBR_USER,
	[PERF_SAMPLE_BRANCH_KERNEL_SHIFT]	= LBR_KERNEL,
	[PERF_SAMPLE_BRANCH_HV_SHIFT]		= LBR_IGN,
	[PERF_SAMPLE_BRANCH_ANY_RETURN_SHIFT]	= LBR_RETURN | LBR_REL_JMP
						| LBR_IND_JMP | LBR_FAR,
	/*
	 * NHM/WSM erratum: must include REL_JMP+IND_JMP to get CALL branches
	 */
	[PERF_SAMPLE_BRANCH_ANY_CALL_SHIFT] =
	 LBR_REL_CALL | LBR_IND_CALL | LBR_REL_JMP | LBR_IND_JMP | LBR_FAR,
	/*
	 * NHM/WSM erratum: must include IND_JMP to capture IND_CALL
	 */
	[PERF_SAMPLE_BRANCH_IND_CALL_SHIFT] = LBR_IND_CALL | LBR_IND_JMP,
	[PERF_SAMPLE_BRANCH_COND_SHIFT]     = LBR_JCC,
	[PERF_SAMPLE_BRANCH_IND_JUMP_SHIFT] = LBR_IND_JMP,
};

static const int snb_lbr_sel_map[PERF_SAMPLE_BRANCH_MAX_SHIFT] = {
	[PERF_SAMPLE_BRANCH_ANY_SHIFT]		= LBR_ANY,
	[PERF_SAMPLE_BRANCH_USER_SHIFT]		= LBR_USER,
	[PERF_SAMPLE_BRANCH_KERNEL_SHIFT]	= LBR_KERNEL,
	[PERF_SAMPLE_BRANCH_HV_SHIFT]		= LBR_IGN,
	[PERF_SAMPLE_BRANCH_ANY_RETURN_SHIFT]	= LBR_RETURN | LBR_FAR,
	[PERF_SAMPLE_BRANCH_ANY_CALL_SHIFT]	= LBR_REL_CALL | LBR_IND_CALL
						| LBR_FAR,
	[PERF_SAMPLE_BRANCH_IND_CALL_SHIFT]	= LBR_IND_CALL,
	[PERF_SAMPLE_BRANCH_COND_SHIFT]		= LBR_JCC,
	[PERF_SAMPLE_BRANCH_IND_JUMP_SHIFT]	= LBR_IND_JMP,
	[PERF_SAMPLE_BRANCH_CALL_SHIFT]		= LBR_REL_CALL,
};

static const int hsw_lbr_sel_map[PERF_SAMPLE_BRANCH_MAX_SHIFT] = {
	[PERF_SAMPLE_BRANCH_ANY_SHIFT]		= LBR_ANY,
	[PERF_SAMPLE_BRANCH_USER_SHIFT]		= LBR_USER,
	[PERF_SAMPLE_BRANCH_KERNEL_SHIFT]	= LBR_KERNEL,
	[PERF_SAMPLE_BRANCH_HV_SHIFT]		= LBR_IGN,
	[PERF_SAMPLE_BRANCH_ANY_RETURN_SHIFT]	= LBR_RETURN | LBR_FAR,
	[PERF_SAMPLE_BRANCH_ANY_CALL_SHIFT]	= LBR_REL_CALL | LBR_IND_CALL
						| LBR_FAR,
	[PERF_SAMPLE_BRANCH_IND_CALL_SHIFT]	= LBR_IND_CALL,
	[PERF_SAMPLE_BRANCH_COND_SHIFT]		= LBR_JCC,
	[PERF_SAMPLE_BRANCH_CALL_STACK_SHIFT]	= LBR_REL_CALL | LBR_IND_CALL
						| LBR_RETURN | LBR_CALL_STACK,
	[PERF_SAMPLE_BRANCH_IND_JUMP_SHIFT]	= LBR_IND_JMP,
	[PERF_SAMPLE_BRANCH_CALL_SHIFT]		= LBR_REL_CALL,
};

static int arch_lbr_ctl_map[PERF_SAMPLE_BRANCH_MAX_SHIFT] = {
	[PERF_SAMPLE_BRANCH_ANY_SHIFT]		= ARCH_LBR_ANY,
	[PERF_SAMPLE_BRANCH_USER_SHIFT]		= ARCH_LBR_USER,
	[PERF_SAMPLE_BRANCH_KERNEL_SHIFT]	= ARCH_LBR_KERNEL,
	[PERF_SAMPLE_BRANCH_HV_SHIFT]		= LBR_IGN,
	[PERF_SAMPLE_BRANCH_ANY_RETURN_SHIFT]	= ARCH_LBR_RETURN |
						  ARCH_LBR_OTHER_BRANCH,
	[PERF_SAMPLE_BRANCH_ANY_CALL_SHIFT]     = ARCH_LBR_REL_CALL |
						  ARCH_LBR_IND_CALL |
						  ARCH_LBR_OTHER_BRANCH,
	[PERF_SAMPLE_BRANCH_IND_CALL_SHIFT]     = ARCH_LBR_IND_CALL,
	[PERF_SAMPLE_BRANCH_COND_SHIFT]         = ARCH_LBR_JCC,
	[PERF_SAMPLE_BRANCH_CALL_STACK_SHIFT]   = ARCH_LBR_REL_CALL |
						  ARCH_LBR_IND_CALL |
						  ARCH_LBR_RETURN |
						  ARCH_LBR_CALL_STACK,
	[PERF_SAMPLE_BRANCH_IND_JUMP_SHIFT]	= ARCH_LBR_IND_JMP,
	[PERF_SAMPLE_BRANCH_CALL_SHIFT]		= ARCH_LBR_REL_CALL,
};

/* core */
void __init intel_pmu_lbr_init_core(void)
{
	x86_pmu.lbr_nr     = 4;
	x86_pmu.lbr_tos    = MSR_LBR_TOS;
	x86_pmu.lbr_from   = MSR_LBR_CORE_FROM;
	x86_pmu.lbr_to     = MSR_LBR_CORE_TO;

	/*
	 * SW branch filter usage:
	 * - compensate for lack of HW filter
	 */
}

/* nehalem/westmere */
void __init intel_pmu_lbr_init_nhm(void)
{
	x86_pmu.lbr_nr     = 16;
	x86_pmu.lbr_tos    = MSR_LBR_TOS;
	x86_pmu.lbr_from   = MSR_LBR_NHM_FROM;
	x86_pmu.lbr_to     = MSR_LBR_NHM_TO;

	x86_pmu.lbr_sel_mask = LBR_SEL_MASK;
	x86_pmu.lbr_sel_map  = nhm_lbr_sel_map;

	/*
	 * SW branch filter usage:
	 * - workaround LBR_SEL errata (see above)
	 * - support syscall, sysret capture.
	 *   That requires LBR_FAR but that means far
	 *   jmp need to be filtered out
	 */
}

/* sandy bridge */
void __init intel_pmu_lbr_init_snb(void)
{
	x86_pmu.lbr_nr	 = 16;
	x86_pmu.lbr_tos	 = MSR_LBR_TOS;
	x86_pmu.lbr_from = MSR_LBR_NHM_FROM;
	x86_pmu.lbr_to   = MSR_LBR_NHM_TO;

	x86_pmu.lbr_sel_mask = LBR_SEL_MASK;
	x86_pmu.lbr_sel_map  = snb_lbr_sel_map;

	/*
	 * SW branch filter usage:
	 * - support syscall, sysret capture.
	 *   That requires LBR_FAR but that means far
	 *   jmp need to be filtered out
	 */
}

static inline struct kmem_cache *
create_lbr_kmem_cache(size_t size, size_t align)
{
	return kmem_cache_create("x86_lbr", size, align, 0, NULL);
}

/* haswell */
void intel_pmu_lbr_init_hsw(void)
{
	size_t size = sizeof(struct x86_perf_task_context);

	x86_pmu.lbr_nr	 = 16;
	x86_pmu.lbr_tos	 = MSR_LBR_TOS;
	x86_pmu.lbr_from = MSR_LBR_NHM_FROM;
	x86_pmu.lbr_to   = MSR_LBR_NHM_TO;

	x86_pmu.lbr_sel_mask = LBR_SEL_MASK;
	x86_pmu.lbr_sel_map  = hsw_lbr_sel_map;

	x86_get_pmu(smp_processor_id())->task_ctx_cache = create_lbr_kmem_cache(size, 0);
}

/* skylake */
__init void intel_pmu_lbr_init_skl(void)
{
	size_t size = sizeof(struct x86_perf_task_context);

	x86_pmu.lbr_nr	 = 32;
	x86_pmu.lbr_tos	 = MSR_LBR_TOS;
	x86_pmu.lbr_from = MSR_LBR_NHM_FROM;
	x86_pmu.lbr_to   = MSR_LBR_NHM_TO;
	x86_pmu.lbr_info = MSR_LBR_INFO_0;

	x86_pmu.lbr_sel_mask = LBR_SEL_MASK;
	x86_pmu.lbr_sel_map  = hsw_lbr_sel_map;

	x86_get_pmu(smp_processor_id())->task_ctx_cache = create_lbr_kmem_cache(size, 0);

	/*
	 * SW branch filter usage:
	 * - support syscall, sysret capture.
	 *   That requires LBR_FAR but that means far
	 *   jmp need to be filtered out
	 */
}

/* atom */
void __init intel_pmu_lbr_init_atom(void)
{
	/*
	 * only models starting at stepping 10 seems
	 * to have an operational LBR which can freeze
	 * on PMU interrupt
	 */
	if (boot_cpu_data.x86_model == 28
	    && boot_cpu_data.x86_stepping < 10) {
		pr_cont("LBR disabled due to erratum");
		return;
	}

	x86_pmu.lbr_nr	   = 8;
	x86_pmu.lbr_tos    = MSR_LBR_TOS;
	x86_pmu.lbr_from   = MSR_LBR_CORE_FROM;
	x86_pmu.lbr_to     = MSR_LBR_CORE_TO;

	/*
	 * SW branch filter usage:
	 * - compensate for lack of HW filter
	 */
}

/* slm */
void __init intel_pmu_lbr_init_slm(void)
{
	x86_pmu.lbr_nr	   = 8;
	x86_pmu.lbr_tos    = MSR_LBR_TOS;
	x86_pmu.lbr_from   = MSR_LBR_CORE_FROM;
	x86_pmu.lbr_to     = MSR_LBR_CORE_TO;

	x86_pmu.lbr_sel_mask = LBR_SEL_MASK;
	x86_pmu.lbr_sel_map  = nhm_lbr_sel_map;

	/*
	 * SW branch filter usage:
	 * - compensate for lack of HW filter
	 */
	pr_cont("8-deep LBR, ");
}

/* Knights Landing */
void intel_pmu_lbr_init_knl(void)
{
	x86_pmu.lbr_nr	   = 8;
	x86_pmu.lbr_tos    = MSR_LBR_TOS;
	x86_pmu.lbr_from   = MSR_LBR_NHM_FROM;
	x86_pmu.lbr_to     = MSR_LBR_NHM_TO;

	x86_pmu.lbr_sel_mask = LBR_SEL_MASK;
	x86_pmu.lbr_sel_map  = snb_lbr_sel_map;

	/* Knights Landing does have MISPREDICT bit */
	if (x86_pmu.intel_cap.lbr_format == LBR_FORMAT_LIP)
		x86_pmu.intel_cap.lbr_format = LBR_FORMAT_EIP_FLAGS;
}

void intel_pmu_lbr_init(void)
{
	switch (x86_pmu.intel_cap.lbr_format) {
	case LBR_FORMAT_EIP_FLAGS2:
		x86_pmu.lbr_has_tsx = 1;
		x86_pmu.lbr_from_flags = 1;
		if (lbr_from_signext_quirk_needed())
			static_branch_enable(&lbr_from_quirk_key);
		break;

	case LBR_FORMAT_EIP_FLAGS:
		x86_pmu.lbr_from_flags = 1;
		break;

	case LBR_FORMAT_INFO:
		x86_pmu.lbr_has_tsx = 1;
		fallthrough;
	case LBR_FORMAT_INFO2:
		x86_pmu.lbr_has_info = 1;
		break;

	case LBR_FORMAT_TIME:
		x86_pmu.lbr_from_flags = 1;
		x86_pmu.lbr_to_cycles = 1;
		break;
	}

	if (x86_pmu.lbr_has_info) {
		/*
		 * Only used in combination with baseline pebs.
		 */
		static_branch_enable(&x86_lbr_mispred);
		static_branch_enable(&x86_lbr_cycles);
	}
}

/*
 * LBR state size is variable based on the max number of registers.
 * This calculates the expected state size, which should match
 * what the hardware enumerates for the size of XFEATURE_LBR.
 */
static inline unsigned int get_lbr_state_size(void)
{
	return sizeof(struct arch_lbr_state) +
	       x86_pmu.lbr_nr * sizeof(struct lbr_entry);
}

static bool is_arch_lbr_xsave_available(void)
{
	if (!boot_cpu_has(X86_FEATURE_XSAVES))
		return false;

	/*
	 * Check the LBR state with the corresponding software structure.
	 * Disable LBR XSAVES support if the size doesn't match.
	 */
	if (xfeature_size(XFEATURE_LBR) == 0)
		return false;

	if (WARN_ON(xfeature_size(XFEATURE_LBR) != get_lbr_state_size()))
		return false;

	return true;
}

void __init intel_pmu_arch_lbr_init(void)
{
	struct pmu *pmu = x86_get_pmu(smp_processor_id());
	union cpuid28_eax eax;
	union cpuid28_ebx ebx;
	union cpuid28_ecx ecx;
	unsigned int unused_edx;
	bool arch_lbr_xsave;
	size_t size;
	u64 lbr_nr;

	/* Arch LBR Capabilities */
	cpuid(28, &eax.full, &ebx.full, &ecx.full, &unused_edx);

	lbr_nr = fls(eax.split.lbr_depth_mask) * 8;
	if (!lbr_nr)
		goto clear_arch_lbr;

	/* Apply the max depth of Arch LBR */
	if (wrmsrl_safe(MSR_ARCH_LBR_DEPTH, lbr_nr))
		goto clear_arch_lbr;

	x86_pmu.lbr_depth_mask = eax.split.lbr_depth_mask;
	x86_pmu.lbr_deep_c_reset = eax.split.lbr_deep_c_reset;
	x86_pmu.lbr_lip = eax.split.lbr_lip;
	x86_pmu.lbr_cpl = ebx.split.lbr_cpl;
	x86_pmu.lbr_filter = ebx.split.lbr_filter;
	x86_pmu.lbr_call_stack = ebx.split.lbr_call_stack;
	x86_pmu.lbr_mispred = ecx.split.lbr_mispred;
	x86_pmu.lbr_timed_lbr = ecx.split.lbr_timed_lbr;
	x86_pmu.lbr_br_type = ecx.split.lbr_br_type;
	x86_pmu.lbr_counters = ecx.split.lbr_counters;
	x86_pmu.lbr_nr = lbr_nr;

	if (!!x86_pmu.lbr_counters)
		x86_pmu.flags |= PMU_FL_BR_CNTR;

	if (x86_pmu.lbr_mispred)
		static_branch_enable(&x86_lbr_mispred);
	if (x86_pmu.lbr_timed_lbr)
		static_branch_enable(&x86_lbr_cycles);
	if (x86_pmu.lbr_br_type)
		static_branch_enable(&x86_lbr_type);

	arch_lbr_xsave = is_arch_lbr_xsave_available();
	if (arch_lbr_xsave) {
		size = sizeof(struct x86_perf_task_context_arch_lbr_xsave) +
		       get_lbr_state_size();
		pmu->task_ctx_cache = create_lbr_kmem_cache(size,
							    XSAVE_ALIGNMENT);
	}

	if (!pmu->task_ctx_cache) {
		arch_lbr_xsave = false;

		size = sizeof(struct x86_perf_task_context_arch_lbr) +
		       lbr_nr * sizeof(struct lbr_entry);
		pmu->task_ctx_cache = create_lbr_kmem_cache(size, 0);
	}

	x86_pmu.lbr_from = MSR_ARCH_LBR_FROM_0;
	x86_pmu.lbr_to = MSR_ARCH_LBR_TO_0;
	x86_pmu.lbr_info = MSR_ARCH_LBR_INFO_0;

	/* LBR callstack requires both CPL and Branch Filtering support */
	if (!x86_pmu.lbr_cpl ||
	    !x86_pmu.lbr_filter ||
	    !x86_pmu.lbr_call_stack)
		arch_lbr_ctl_map[PERF_SAMPLE_BRANCH_CALL_STACK_SHIFT] = LBR_NOT_SUPP;

	if (!x86_pmu.lbr_cpl) {
		arch_lbr_ctl_map[PERF_SAMPLE_BRANCH_USER_SHIFT] = LBR_NOT_SUPP;
		arch_lbr_ctl_map[PERF_SAMPLE_BRANCH_KERNEL_SHIFT] = LBR_NOT_SUPP;
	} else if (!x86_pmu.lbr_filter) {
		arch_lbr_ctl_map[PERF_SAMPLE_BRANCH_ANY_SHIFT] = LBR_NOT_SUPP;
		arch_lbr_ctl_map[PERF_SAMPLE_BRANCH_ANY_RETURN_SHIFT] = LBR_NOT_SUPP;
		arch_lbr_ctl_map[PERF_SAMPLE_BRANCH_ANY_CALL_SHIFT] = LBR_NOT_SUPP;
		arch_lbr_ctl_map[PERF_SAMPLE_BRANCH_IND_CALL_SHIFT] = LBR_NOT_SUPP;
		arch_lbr_ctl_map[PERF_SAMPLE_BRANCH_COND_SHIFT] = LBR_NOT_SUPP;
		arch_lbr_ctl_map[PERF_SAMPLE_BRANCH_IND_JUMP_SHIFT] = LBR_NOT_SUPP;
		arch_lbr_ctl_map[PERF_SAMPLE_BRANCH_CALL_SHIFT] = LBR_NOT_SUPP;
	}

	x86_pmu.lbr_ctl_mask = ARCH_LBR_CTL_MASK;
	x86_pmu.lbr_ctl_map  = arch_lbr_ctl_map;

	if (!x86_pmu.lbr_cpl && !x86_pmu.lbr_filter)
		x86_pmu.lbr_ctl_map = NULL;

	x86_pmu.lbr_reset = intel_pmu_arch_lbr_reset;
	if (arch_lbr_xsave) {
		x86_pmu.lbr_save = intel_pmu_arch_lbr_xsaves;
		x86_pmu.lbr_restore = intel_pmu_arch_lbr_xrstors;
		x86_pmu.lbr_read = intel_pmu_arch_lbr_read_xsave;
		pr_cont("XSAVE ");
	} else {
		x86_pmu.lbr_save = intel_pmu_arch_lbr_save;
		x86_pmu.lbr_restore = intel_pmu_arch_lbr_restore;
		x86_pmu.lbr_read = intel_pmu_arch_lbr_read;
	}

	pr_cont("Architectural LBR, ");

	return;

clear_arch_lbr:
	setup_clear_cpu_cap(X86_FEATURE_ARCH_LBR);
}

/**
 * x86_perf_get_lbr - get the LBR records information
 *
 * @lbr: the caller's memory to store the LBR records information
 */
void x86_perf_get_lbr(struct x86_pmu_lbr *lbr)
{
	lbr->nr = x86_pmu.lbr_nr;
	lbr->from = x86_pmu.lbr_from;
	lbr->to = x86_pmu.lbr_to;
	lbr->info = x86_pmu.lbr_info;
	lbr->has_callstack = x86_pmu_has_lbr_callstack();
}
EXPORT_SYMBOL_GPL(x86_perf_get_lbr);

struct event_constraint vlbr_constraint =
	__EVENT_CONSTRAINT(INTEL_FIXED_VLBR_EVENT, (1ULL << INTEL_PMC_IDX_FIXED_VLBR),
			  FIXED_EVENT_FLAGS, 1, 0, PERF_X86_EVENT_LBR_SELECT);
