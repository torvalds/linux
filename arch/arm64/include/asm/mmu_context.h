/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Based on arch/arm/include/asm/mmu_context.h
 *
 * Copyright (C) 1996 Russell King.
 * Copyright (C) 2012 ARM Ltd.
 */
#ifndef __ASM_MMU_CONTEXT_H
#define __ASM_MMU_CONTEXT_H

#ifndef __ASSEMBLY__

#include <linux/compiler.h>
#include <linux/sched.h>
#include <linux/sched/hotplug.h>
#include <linux/mm_types.h>
#include <linux/pgtable.h>

#include <asm/cacheflush.h>
#include <asm/cpufeature.h>
#include <asm/daifflags.h>
#include <asm/proc-fns.h>
#include <asm-generic/mm_hooks.h>
#include <asm/cputype.h>
#include <asm/sysreg.h>
#include <asm/tlbflush.h>

extern bool rodata_full;

static inline void contextidr_thread_switch(struct task_struct *next)
{
	if (!IS_ENABLED(CONFIG_PID_IN_CONTEXTIDR))
		return;

	write_sysreg(task_pid_nr(next), contextidr_el1);
	isb();
}

/*
 * Set TTBR0 to reserved_pg_dir. No translations will be possible via TTBR0.
 */
static inline void cpu_set_reserved_ttbr0(void)
{
	unsigned long ttbr = phys_to_ttbr(__pa_symbol(reserved_pg_dir));

	write_sysreg(ttbr, ttbr0_el1);
	isb();
}

void cpu_do_switch_mm(phys_addr_t pgd_phys, struct mm_struct *mm);

static inline void cpu_switch_mm(pgd_t *pgd, struct mm_struct *mm)
{
	BUG_ON(pgd == swapper_pg_dir);
	cpu_set_reserved_ttbr0();
	cpu_do_switch_mm(virt_to_phys(pgd),mm);
}

/*
 * TCR.T0SZ value to use when the ID map is active. Usually equals
 * TCR_T0SZ(VA_BITS), unless system RAM is positioned very high in
 * physical memory, in which case it will be smaller.
 */
extern int idmap_t0sz;

/*
 * Ensure TCR.T0SZ is set to the provided value.
 */
static inline void __cpu_set_tcr_t0sz(unsigned long t0sz)
{
	unsigned long tcr = read_sysreg(tcr_el1);

	if ((tcr & TCR_T0SZ_MASK) >> TCR_T0SZ_OFFSET == t0sz)
		return;

	tcr &= ~TCR_T0SZ_MASK;
	tcr |= t0sz << TCR_T0SZ_OFFSET;
	write_sysreg(tcr, tcr_el1);
	isb();
}

#define cpu_set_default_tcr_t0sz()	__cpu_set_tcr_t0sz(TCR_T0SZ(vabits_actual))
#define cpu_set_idmap_tcr_t0sz()	__cpu_set_tcr_t0sz(idmap_t0sz)

/*
 * Remove the idmap from TTBR0_EL1 and install the pgd of the active mm.
 *
 * The idmap lives in the same VA range as userspace, but uses global entries
 * and may use a different TCR_EL1.T0SZ. To avoid issues resulting from
 * speculative TLB fetches, we must temporarily install the reserved page
 * tables while we invalidate the TLBs and set up the correct TCR_EL1.T0SZ.
 *
 * If current is a not a user task, the mm covers the TTBR1_EL1 page tables,
 * which should not be installed in TTBR0_EL1. In this case we can leave the
 * reserved page tables in place.
 */
static inline void cpu_uninstall_idmap(void)
{
	struct mm_struct *mm = current->active_mm;

	cpu_set_reserved_ttbr0();
	local_flush_tlb_all();
	cpu_set_default_tcr_t0sz();

	if (mm != &init_mm && !system_uses_ttbr0_pan())
		cpu_switch_mm(mm->pgd, mm);
}

static inline void __cpu_install_idmap(pgd_t *idmap)
{
	cpu_set_reserved_ttbr0();
	local_flush_tlb_all();
	cpu_set_idmap_tcr_t0sz();

	cpu_switch_mm(lm_alias(idmap), &init_mm);
}

static inline void cpu_install_idmap(void)
{
	__cpu_install_idmap(idmap_pg_dir);
}

/*
 * Load our new page tables. A strict BBM approach requires that we ensure that
 * TLBs are free of any entries that may overlap with the global mappings we are
 * about to install.
 *
 * For a real hibernate/resume/kexec cycle TTBR0 currently points to a zero
 * page, but TLBs may contain stale ASID-tagged entries (e.g. for EFI runtime
 * services), while for a userspace-driven test_resume cycle it points to
 * userspace page tables (and we must point it at a zero page ourselves).
 *
 * We change T0SZ as part of installing the idmap. This is undone by
 * cpu_uninstall_idmap() in __cpu_suspend_exit().
 */
static inline void cpu_install_ttbr0(phys_addr_t ttbr0, unsigned long t0sz)
{
	cpu_set_reserved_ttbr0();
	local_flush_tlb_all();
	__cpu_set_tcr_t0sz(t0sz);

	/* avoid cpu_switch_mm() and its SW-PAN and CNP interactions */
	write_sysreg(ttbr0, ttbr0_el1);
	isb();
}

/*
 * Atomically replaces the active TTBR1_EL1 PGD with a new VA-compatible PGD,
 * avoiding the possibility of conflicting TLB entries being allocated.
 */
static inline void cpu_replace_ttbr1(pgd_t *pgdp, pgd_t *idmap)
{
	typedef void (ttbr_replace_func)(phys_addr_t);
	extern ttbr_replace_func idmap_cpu_replace_ttbr1;
	ttbr_replace_func *replace_phys;
	unsigned long daif;

	/* phys_to_ttbr() zeros lower 2 bits of ttbr with 52-bit PA */
	phys_addr_t ttbr1 = phys_to_ttbr(virt_to_phys(pgdp));

	if (system_supports_cnp() && !WARN_ON(pgdp != lm_alias(swapper_pg_dir))) {
		/*
		 * cpu_replace_ttbr1() is used when there's a boot CPU
		 * up (i.e. cpufeature framework is not up yet) and
		 * latter only when we enable CNP via cpufeature's
		 * enable() callback.
		 * Also we rely on the cpu_hwcap bit being set before
		 * calling the enable() function.
		 */
		ttbr1 |= TTBR_CNP_BIT;
	}

	replace_phys = (void *)__pa_symbol(idmap_cpu_replace_ttbr1);

	__cpu_install_idmap(idmap);

	/*
	 * We really don't want to take *any* exceptions while TTBR1 is
	 * in the process of being replaced so mask everything.
	 */
	daif = local_daif_save();
	replace_phys(ttbr1);
	local_daif_restore(daif);

	cpu_uninstall_idmap();
}

/*
 * It would be nice to return ASIDs back to the allocator, but unfortunately
 * that introduces a race with a generation rollover where we could erroneously
 * free an ASID allocated in a future generation. We could workaround this by
 * freeing the ASID from the context of the dying mm (e.g. in arch_exit_mmap),
 * but we'd then need to make sure that we didn't dirty any TLBs afterwards.
 * Setting a reserved TTBR0 or EPD0 would work, but it all gets ugly when you
 * take CPU migration into account.
 */
void check_and_switch_context(struct mm_struct *mm);

#define init_new_context(tsk, mm) init_new_context(tsk, mm)
static inline int
init_new_context(struct task_struct *tsk, struct mm_struct *mm)
{
	atomic64_set(&mm->context.id, 0);
	refcount_set(&mm->context.pinned, 0);
	return 0;
}

#ifdef CONFIG_ARM64_SW_TTBR0_PAN
static inline void update_saved_ttbr0(struct task_struct *tsk,
				      struct mm_struct *mm)
{
	u64 ttbr;

	if (!system_uses_ttbr0_pan())
		return;

	if (mm == &init_mm)
		ttbr = phys_to_ttbr(__pa_symbol(reserved_pg_dir));
	else
		ttbr = phys_to_ttbr(virt_to_phys(mm->pgd)) | ASID(mm) << 48;

	WRITE_ONCE(task_thread_info(tsk)->ttbr0, ttbr);
}
#else
static inline void update_saved_ttbr0(struct task_struct *tsk,
				      struct mm_struct *mm)
{
}
#endif

#define enter_lazy_tlb enter_lazy_tlb
static inline void
enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk)
{
	/*
	 * We don't actually care about the ttbr0 mapping, so point it at the
	 * zero page.
	 */
	update_saved_ttbr0(tsk, &init_mm);
}

static inline void __switch_mm(struct mm_struct *next)
{
	/*
	 * init_mm.pgd does not contain any user mappings and it is always
	 * active for kernel addresses in TTBR1. Just set the reserved TTBR0.
	 */
	if (next == &init_mm) {
		cpu_set_reserved_ttbr0();
		return;
	}

	check_and_switch_context(next);
}

static inline void
switch_mm(struct mm_struct *prev, struct mm_struct *next,
	  struct task_struct *tsk)
{
	if (prev != next)
		__switch_mm(next);

	/*
	 * Update the saved TTBR0_EL1 of the scheduled-in task as the previous
	 * value may have not been initialised yet (activate_mm caller) or the
	 * ASID has changed since the last run (following the context switch
	 * of another thread of the same process).
	 */
	update_saved_ttbr0(tsk, next);
}

static inline const struct cpumask *
task_cpu_possible_mask(struct task_struct *p)
{
	if (!static_branch_unlikely(&arm64_mismatched_32bit_el0))
		return cpu_possible_mask;

	if (!is_compat_thread(task_thread_info(p)))
		return cpu_possible_mask;

	return system_32bit_el0_cpumask();
}
#define task_cpu_possible_mask	task_cpu_possible_mask

void verify_cpu_asid_bits(void);
void post_ttbr_update_workaround(void);

unsigned long arm64_mm_context_get(struct mm_struct *mm);
void arm64_mm_context_put(struct mm_struct *mm);

#define mm_untag_mask mm_untag_mask
static inline unsigned long mm_untag_mask(struct mm_struct *mm)
{
	return -1UL >> 8;
}

#include <asm-generic/mmu_context.h>

#endif /* !__ASSEMBLY__ */

#endif /* !__ASM_MMU_CONTEXT_H */
