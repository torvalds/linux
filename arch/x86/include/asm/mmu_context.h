/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_MMU_CONTEXT_H
#define _ASM_X86_MMU_CONTEXT_H

#include <asm/desc.h>
#include <linux/atomic.h>
#include <linux/mm_types.h>
#include <linux/pkeys.h>

#include <trace/events/tlb.h>

#include <asm/pgalloc.h>
#include <asm/tlbflush.h>
#include <asm/paravirt.h>
#include <asm/debugreg.h>

extern atomic64_t last_mm_ctx_id;

#ifndef CONFIG_PARAVIRT_XXL
static inline void paravirt_activate_mm(struct mm_struct *prev,
					struct mm_struct *next)
{
}
#endif	/* !CONFIG_PARAVIRT_XXL */

#ifdef CONFIG_PERF_EVENTS
DECLARE_STATIC_KEY_FALSE(rdpmc_never_available_key);
DECLARE_STATIC_KEY_FALSE(rdpmc_always_available_key);
void cr4_update_pce(void *ignored);
#endif

#ifdef CONFIG_MODIFY_LDT_SYSCALL
/*
 * ldt_structs can be allocated, used, and freed, but they are never
 * modified while live.
 */
struct ldt_struct {
	/*
	 * Xen requires page-aligned LDTs with special permissions.  This is
	 * needed to prevent us from installing evil descriptors such as
	 * call gates.  On native, we could merge the ldt_struct and LDT
	 * allocations, but it's not worth trying to optimize.
	 */
	struct desc_struct	*entries;
	unsigned int		nr_entries;

	/*
	 * If PTI is in use, then the entries array is not mapped while we're
	 * in user mode.  The whole array will be aliased at the addressed
	 * given by ldt_slot_va(slot).  We use two slots so that we can allocate
	 * and map, and enable a new LDT without invalidating the mapping
	 * of an older, still-in-use LDT.
	 *
	 * slot will be -1 if this LDT doesn't have an alias mapping.
	 */
	int			slot;
};

/*
 * Used for LDT copy/destruction.
 */
static inline void init_new_context_ldt(struct mm_struct *mm)
{
	mm->context.ldt = NULL;
	init_rwsem(&mm->context.ldt_usr_sem);
}
int ldt_dup_context(struct mm_struct *oldmm, struct mm_struct *mm);
void destroy_context_ldt(struct mm_struct *mm);
void ldt_arch_exit_mmap(struct mm_struct *mm);
#else	/* CONFIG_MODIFY_LDT_SYSCALL */
static inline void init_new_context_ldt(struct mm_struct *mm) { }
static inline int ldt_dup_context(struct mm_struct *oldmm,
				  struct mm_struct *mm)
{
	return 0;
}
static inline void destroy_context_ldt(struct mm_struct *mm) { }
static inline void ldt_arch_exit_mmap(struct mm_struct *mm) { }
#endif

#ifdef CONFIG_MODIFY_LDT_SYSCALL
extern void load_mm_ldt(struct mm_struct *mm);
extern void switch_ldt(struct mm_struct *prev, struct mm_struct *next);
#else
static inline void load_mm_ldt(struct mm_struct *mm)
{
	clear_LDT();
}
static inline void switch_ldt(struct mm_struct *prev, struct mm_struct *next)
{
	DEBUG_LOCKS_WARN_ON(preemptible());
}
#endif

extern void enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk);

/*
 * Init a new mm.  Used on mm copies, like at fork()
 * and on mm's that are brand-new, like at execve().
 */
static inline int init_new_context(struct task_struct *tsk,
				   struct mm_struct *mm)
{
	mutex_init(&mm->context.lock);

	mm->context.ctx_id = atomic64_inc_return(&last_mm_ctx_id);
	atomic64_set(&mm->context.tlb_gen, 0);

#ifdef CONFIG_X86_INTEL_MEMORY_PROTECTION_KEYS
	if (cpu_feature_enabled(X86_FEATURE_OSPKE)) {
		/* pkey 0 is the default and allocated implicitly */
		mm->context.pkey_allocation_map = 0x1;
		/* -1 means unallocated or invalid */
		mm->context.execute_only_pkey = -1;
	}
#endif
	init_new_context_ldt(mm);
	return 0;
}
static inline void destroy_context(struct mm_struct *mm)
{
	destroy_context_ldt(mm);
}

extern void switch_mm(struct mm_struct *prev, struct mm_struct *next,
		      struct task_struct *tsk);

extern void switch_mm_irqs_off(struct mm_struct *prev, struct mm_struct *next,
			       struct task_struct *tsk);
#define switch_mm_irqs_off switch_mm_irqs_off

#define activate_mm(prev, next)			\
do {						\
	paravirt_activate_mm((prev), (next));	\
	switch_mm((prev), (next), NULL);	\
} while (0);

#ifdef CONFIG_X86_32
#define deactivate_mm(tsk, mm)			\
do {						\
	lazy_load_gs(0);			\
} while (0)
#else
#define deactivate_mm(tsk, mm)			\
do {						\
	load_gs_index(0);			\
	loadsegment(fs, 0);			\
} while (0)
#endif

static inline void arch_dup_pkeys(struct mm_struct *oldmm,
				  struct mm_struct *mm)
{
#ifdef CONFIG_X86_INTEL_MEMORY_PROTECTION_KEYS
	if (!cpu_feature_enabled(X86_FEATURE_OSPKE))
		return;

	/* Duplicate the oldmm pkey state in mm: */
	mm->context.pkey_allocation_map = oldmm->context.pkey_allocation_map;
	mm->context.execute_only_pkey   = oldmm->context.execute_only_pkey;
#endif
}

static inline int arch_dup_mmap(struct mm_struct *oldmm, struct mm_struct *mm)
{
	arch_dup_pkeys(oldmm, mm);
	paravirt_arch_dup_mmap(oldmm, mm);
	return ldt_dup_context(oldmm, mm);
}

static inline void arch_exit_mmap(struct mm_struct *mm)
{
	paravirt_arch_exit_mmap(mm);
	ldt_arch_exit_mmap(mm);
}

#ifdef CONFIG_X86_64
static inline bool is_64bit_mm(struct mm_struct *mm)
{
	return	!IS_ENABLED(CONFIG_IA32_EMULATION) ||
		!(mm->context.ia32_compat == TIF_IA32);
}
#else
static inline bool is_64bit_mm(struct mm_struct *mm)
{
	return false;
}
#endif

static inline void arch_unmap(struct mm_struct *mm, unsigned long start,
			      unsigned long end)
{
}

/*
 * We only want to enforce protection keys on the current process
 * because we effectively have no access to PKRU for other
 * processes or any way to tell *which * PKRU in a threaded
 * process we could use.
 *
 * So do not enforce things if the VMA is not from the current
 * mm, or if we are in a kernel thread.
 */
static inline bool arch_vma_access_permitted(struct vm_area_struct *vma,
		bool write, bool execute, bool foreign)
{
	/* pkeys never affect instruction fetches */
	if (execute)
		return true;
	/* allow access if the VMA is not one from this process */
	if (foreign || vma_is_foreign(vma))
		return true;
	return __pkru_allows_pkey(vma_pkey(vma), write);
}

unsigned long __get_current_cr3_fast(void);

#endif /* _ASM_X86_MMU_CONTEXT_H */
