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
 * contains both parts, so we need to cache them. This also
 * stores the user configured per cpu CLOSID and RMID.
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
DECLARE_STATIC_KEY_FALSE(rdt_mon_enable_key);

/*
 * __intel_rdt_sched_in() - Writes the task's CLOSid/RMID to IA32_PQR_MSR
 *
 * Following considerations are made so that this has minimal impact
 * on scheduler hot path:
 * - This will stay as no-op unless we are running on an Intel SKU
 *   which supports resource control or monitoring and we enable by
 *   mounting the resctrl file system.
 * - Caches the per cpu CLOSid/RMID values and does the MSR write only
 *   when a task with a different CLOSid/RMID is scheduled in.
 * - We allocate RMIDs/CLOSids globally in order to keep this as
 *   simple as possible.
 * Must be called with preemption disabled.
 */
static void __intel_rdt_sched_in(void)
{
	struct intel_pqr_state newstate = this_cpu_read(rdt_cpu_default);
	struct intel_pqr_state *curstate = this_cpu_ptr(&pqr_state);

	/*
	 * If this task has a closid/rmid assigned, use it.
	 * Else use the closid/rmid assigned to this cpu.
	 */
	if (static_branch_likely(&rdt_alloc_enable_key)) {
		if (current->closid)
			newstate.closid = current->closid;
	}

	if (static_branch_likely(&rdt_mon_enable_key)) {
		if (current->rmid)
			newstate.rmid = current->rmid;
	}

	if (newstate.closid != curstate->closid ||
	    newstate.rmid != curstate->rmid) {
		*curstate = newstate;
		wrmsr(IA32_PQR_ASSOC, newstate.rmid, newstate.closid);
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
