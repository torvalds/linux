/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _ASM_POWERPC_IDLE_H
#define _ASM_POWERPC_IDLE_H
#include <asm/runlatch.h>
#include <asm/paca.h>

#ifdef CONFIG_PPC_PSERIES
DECLARE_PER_CPU(u64, idle_spurr_cycles);
DECLARE_PER_CPU(u64, idle_entry_purr_snap);
DECLARE_PER_CPU(u64, idle_entry_spurr_snap);

static inline void snapshot_purr_idle_entry(void)
{
	*this_cpu_ptr(&idle_entry_purr_snap) = mfspr(SPRN_PURR);
}

static inline void snapshot_spurr_idle_entry(void)
{
	*this_cpu_ptr(&idle_entry_spurr_snap) = mfspr(SPRN_SPURR);
}

static inline void update_idle_purr_accounting(void)
{
	u64 wait_cycles;
	u64 in_purr = *this_cpu_ptr(&idle_entry_purr_snap);

	wait_cycles = be64_to_cpu(get_lppaca()->wait_state_cycles);
	wait_cycles += mfspr(SPRN_PURR) - in_purr;
	get_lppaca()->wait_state_cycles = cpu_to_be64(wait_cycles);
}

static inline void update_idle_spurr_accounting(void)
{
	u64 *idle_spurr_cycles_ptr = this_cpu_ptr(&idle_spurr_cycles);
	u64 in_spurr = *this_cpu_ptr(&idle_entry_spurr_snap);

	*idle_spurr_cycles_ptr += mfspr(SPRN_SPURR) - in_spurr;
}

static inline void pseries_idle_prolog(void)
{
	ppc64_runlatch_off();
	snapshot_purr_idle_entry();
	snapshot_spurr_idle_entry();
	/*
	 * Indicate to the HV that we are idle. Now would be
	 * a good time to find other work to dispatch.
	 */
	get_lppaca()->idle = 1;
}

static inline void pseries_idle_epilog(void)
{
	update_idle_purr_accounting();
	update_idle_spurr_accounting();
	get_lppaca()->idle = 0;
	ppc64_runlatch_on();
}

static inline u64 read_this_idle_purr(void)
{
	/*
	 * If we are reading from an idle context, update the
	 * idle-purr cycles corresponding to the last idle period.
	 * Since the idle context is not yet over, take a fresh
	 * snapshot of the idle-purr.
	 */
	if (unlikely(get_lppaca()->idle == 1)) {
		update_idle_purr_accounting();
		snapshot_purr_idle_entry();
	}

	return be64_to_cpu(get_lppaca()->wait_state_cycles);
}

static inline u64 read_this_idle_spurr(void)
{
	/*
	 * If we are reading from an idle context, update the
	 * idle-spurr cycles corresponding to the last idle period.
	 * Since the idle context is not yet over, take a fresh
	 * snapshot of the idle-spurr.
	 */
	if (get_lppaca()->idle == 1) {
		update_idle_spurr_accounting();
		snapshot_spurr_idle_entry();
	}

	return *this_cpu_ptr(&idle_spurr_cycles);
}

#endif /* CONFIG_PPC_PSERIES */
#endif
