/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_RISCV_QOS_H
#define _ASM_RISCV_QOS_H

#include <linux/percpu-defs.h>

#ifdef CONFIG_RISCV_ISA_SSQOSID

#include <linux/bitfield.h>
#include <linux/cpufeature.h>
#include <linux/sched.h>

#include <asm/csr.h>
#include <asm/hwcap.h>

/* cached value of srmcfg csr for each cpu */
DECLARE_PER_CPU(u32, cpu_srmcfg);

/* default srmcfg value for each cpu, set via resctrl cpu assignment */
DECLARE_PER_CPU(u32, cpu_srmcfg_default);

static inline void __switch_to_srmcfg(struct task_struct *next)
{
	u32 thread_srmcfg, default_srmcfg;

	thread_srmcfg = READ_ONCE(next->thread.srmcfg);
	default_srmcfg = __this_cpu_read(cpu_srmcfg_default);

	/*
	 * RCID and MCID inherit from cpu_srmcfg_default independently.
	 * RESCTRL_RESERVED_CLOSID and RESCTRL_RESERVED_RMID are both 0, so a
	 * zero field means "unassigned" and takes the CPU default.
	 */
	if (thread_srmcfg == 0) {
		thread_srmcfg = default_srmcfg;
	} else {
		u32 rcid = FIELD_GET(SRMCFG_RCID_MASK, thread_srmcfg);
		u32 mcid = FIELD_GET(SRMCFG_MCID_MASK, thread_srmcfg);

		if (rcid == 0 || mcid == 0) {
			if (rcid == 0)
				rcid = FIELD_GET(SRMCFG_RCID_MASK, default_srmcfg);
			if (mcid == 0)
				mcid = FIELD_GET(SRMCFG_MCID_MASK, default_srmcfg);
			thread_srmcfg = FIELD_PREP(SRMCFG_RCID_MASK, rcid) |
					FIELD_PREP(SRMCFG_MCID_MASK, mcid);
		}
	}

	if (thread_srmcfg != __this_cpu_read(cpu_srmcfg)) {
		/*
		 * No fence around the csrw. Ssqosid is silent on srmcfg
		 * ordering versus memory accesses, so a few accesses at the
		 * switch boundary may carry the previous RCID/MCID. The
		 * tagging inaccuracy is bounded and acceptable for QoS.
		 */
		__this_cpu_write(cpu_srmcfg, thread_srmcfg);
		csr_write(CSR_SRMCFG, thread_srmcfg);
	}
}

static __always_inline bool has_srmcfg(void)
{
	return riscv_has_extension_unlikely(RISCV_ISA_EXT_SSQOSID);
}

#else /* ! CONFIG_RISCV_ISA_SSQOSID  */

struct task_struct;
static __always_inline bool has_srmcfg(void) { return false; }
static inline void __switch_to_srmcfg(struct task_struct *next) { }

#endif /* CONFIG_RISCV_ISA_SSQOSID */
#endif /* _ASM_RISCV_QOS_H */
