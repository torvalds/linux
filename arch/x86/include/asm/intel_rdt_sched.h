#ifndef _ASM_X86_INTEL_RDT_SCHED_H
#define _ASM_X86_INTEL_RDT_SCHED_H

#ifdef CONFIG_INTEL_RDT

#include <linux/sched.h>
#include <linux/jump_label.h>

#define IA32_PQR_ASSOC	0x0c8f

/**
 * struct intel_pqr_state - State cache for the PQR MSR
 * @rmid:		The cached Resource Monitoring ID
 * @closid:		The cached Class Of Service ID
 *
 * The upper 32 bits of IA32_PQR_ASSOC contain closid and the
 * lower 10 bits rmid. The update to IA32_PQR_ASSOC always
 * contains both parts, so we need to cache them.
 *
 * The cache also helps to avoid pointless updates if the value does
 * not change.
 */
struct intel_pqr_state {
	u32			rmid;
	u32			closid;
};

DECLARE_PER_CPU(struct intel_pqr_state, pqr_state);
DECLARE_PER_CPU_READ_MOSTLY(struct intel_pqr_state, rdt_cpu_default);

DECLARE_STATIC_KEY_FALSE(rdt_enable_key);
DECLARE_STATIC_KEY_FALSE(rdt_alloc_enable_key);

/*
 * __intel_rdt_sched_in() - Writes the task's CLOSid to IA32_PQR_MSR
 *
 * Following considerations are made so that this has minimal impact
 * on scheduler hot path:
 * - This will stay as no-op unless we are running on an Intel SKU
 *   which supports resource control and we enable by mounting the
 *   resctrl file system.
 * - Caches the per cpu CLOSid values and does the MSR write only
 *   when a task with a different CLOSid is scheduled in.
 *
 * Must be called with preemption disabled.
 */
static inline void __intel_rdt_sched_in(void)
{
	if (static_branch_likely(&rdt_alloc_enable_key)) {
		struct intel_pqr_state *state = this_cpu_ptr(&pqr_state);
		u32 closid;

		/*
		 * If this task has a closid assigned, use it.
		 * Else use the closid assigned to this cpu.
		 */
		closid = current->closid;
		if (closid == 0)
			closid = this_cpu_read(rdt_cpu_default.closid);

		if (closid != state->closid) {
			state->closid = closid;
			wrmsr(IA32_PQR_ASSOC, state->rmid, closid);
		}
	}
}

static inline void intel_rdt_sched_in(void)
{
	if (static_branch_likely(&rdt_enable_key))
		__intel_rdt_sched_in();
}

#else

static inline void intel_rdt_sched_in(void) {}

#endif /* CONFIG_INTEL_RDT */

#endif /* _ASM_X86_INTEL_RDT_SCHED_H */
