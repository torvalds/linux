/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_RISCV_SYNC_CORE_H
#define _ASM_RISCV_SYNC_CORE_H

/*
 * RISC-V implements return to user-space through an xRET instruction,
 * which is not core serializing.
 */
static inline void sync_core_before_usermode(void)
{
	asm volatile ("fence.i" ::: "memory");
}

#ifdef CONFIG_SMP
/*
 * Ensure the next switch_mm() on every CPU issues a core serializing
 * instruction for the given @mm.
 */
static inline void prepare_sync_core_cmd(struct mm_struct *mm)
{
	cpumask_setall(&mm->context.icache_stale_mask);
}
#else
static inline void prepare_sync_core_cmd(struct mm_struct *mm)
{
}
#endif /* CONFIG_SMP */

#endif /* _ASM_RISCV_SYNC_CORE_H */
