/*
 *  arch/arm/include/asm/proc-fns.h
 *
 *  Copyright (C) 1997-1999 Russell King
 *  Copyright (C) 2000 Deep Blue Solutions Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_PROCFNS_H
#define __ASM_PROCFNS_H

#ifdef __KERNEL__

#include <asm/glue-proc.h>
#include <asm/page.h>

#ifndef __ASSEMBLY__

struct mm_struct;

/*
 * Don't change this structure - ASM code relies on it.
 */
extern struct processor {
	/* MISC
	 * get data abort address/flags
	 */
	void (*_data_abort)(unsigned long pc);
	/*
	 * Retrieve prefetch fault address
	 */
	unsigned long (*_prefetch_abort)(unsigned long lr);
	/*
	 * Set up any processor specifics
	 */
	void (*_proc_init)(void);
	/*
	 * Check for processor bugs
	 */
	void (*check_bugs)(void);
	/*
	 * Disable any processor specifics
	 */
	void (*_proc_fin)(void);
	/*
	 * Special stuff for a reset
	 */
	void (*reset)(unsigned long addr, bool hvc) __attribute__((noreturn));
	/*
	 * Idle the processor
	 */
	int (*_do_idle)(void);
	/*
	 * Processor architecture specific
	 */
	/*
	 * clean a virtual address range from the
	 * D-cache without flushing the cache.
	 */
	void (*dcache_clean_area)(void *addr, int size);

	/*
	 * Set the page table
	 */
	void (*switch_mm)(phys_addr_t pgd_phys, struct mm_struct *mm);
	/*
	 * Set a possibly extended PTE.  Non-extended PTEs should
	 * ignore 'ext'.
	 */
#ifdef CONFIG_ARM_LPAE
	void (*set_pte_ext)(pte_t *ptep, pte_t pte);
#else
	void (*set_pte_ext)(pte_t *ptep, pte_t pte, unsigned int ext);
#endif

	/* Suspend/resume */
	unsigned int suspend_size;
	void (*do_suspend)(void *);
	void (*do_resume)(void *);
} processor;

#ifndef MULTI_CPU
extern void cpu_proc_init(void);
extern void cpu_proc_fin(void);
extern int cpu_do_idle(void);
extern void cpu_dcache_clean_area(void *, int);
extern void cpu_do_switch_mm(phys_addr_t pgd_phys, struct mm_struct *mm);
#ifdef CONFIG_ARM_LPAE
extern void cpu_set_pte_ext(pte_t *ptep, pte_t pte);
#else
extern void cpu_set_pte_ext(pte_t *ptep, pte_t pte, unsigned int ext);
#endif
extern void cpu_reset(unsigned long addr, bool hvc) __attribute__((noreturn));

/* These three are private to arch/arm/kernel/suspend.c */
extern void cpu_do_suspend(void *);
extern void cpu_do_resume(void *);
#else
#define cpu_proc_init			processor._proc_init
#define cpu_check_bugs			processor.check_bugs
#define cpu_proc_fin			processor._proc_fin
#define cpu_reset			processor.reset
#define cpu_do_idle			processor._do_idle
#define cpu_dcache_clean_area		processor.dcache_clean_area
#define cpu_set_pte_ext			processor.set_pte_ext
#define cpu_do_switch_mm		processor.switch_mm

/* These three are private to arch/arm/kernel/suspend.c */
#define cpu_do_suspend			processor.do_suspend
#define cpu_do_resume			processor.do_resume
#endif

extern void cpu_resume(void);

#include <asm/memory.h>

#ifdef CONFIG_MMU

#define cpu_switch_mm(pgd,mm) cpu_do_switch_mm(virt_to_phys(pgd),mm)

#ifdef CONFIG_ARM_LPAE

#define cpu_get_ttbr(nr)					\
	({							\
		u64 ttbr;					\
		__asm__("mrrc	p15, " #nr ", %Q0, %R0, c2"	\
			: "=r" (ttbr));				\
		ttbr;						\
	})

#define cpu_get_pgd()	\
	({						\
		u64 pg = cpu_get_ttbr(0);		\
		pg &= ~(PTRS_PER_PGD*sizeof(pgd_t)-1);	\
		(pgd_t *)phys_to_virt(pg);		\
	})
#else
#define cpu_get_pgd()	\
	({						\
		unsigned long pg;			\
		__asm__("mrc	p15, 0, %0, c2, c0, 0"	\
			 : "=r" (pg) : : "cc");		\
		pg &= ~0x3fff;				\
		(pgd_t *)phys_to_virt(pg);		\
	})
#endif

#else	/*!CONFIG_MMU */

#define cpu_switch_mm(pgd,mm)	{ }

#endif

#endif /* __ASSEMBLY__ */
#endif /* __KERNEL__ */
#endif /* __ASM_PROCFNS_H */
