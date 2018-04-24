/*
 * Copyright(c) 2017 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * This code is based in part on work published here:
 *
 *	https://github.com/IAIK/KAISER
 *
 * The original work was written by and and signed off by for the Linux
 * kernel by:
 *
 *   Signed-off-by: Richard Fellner <richard.fellner@student.tugraz.at>
 *   Signed-off-by: Moritz Lipp <moritz.lipp@iaik.tugraz.at>
 *   Signed-off-by: Daniel Gruss <daniel.gruss@iaik.tugraz.at>
 *   Signed-off-by: Michael Schwarz <michael.schwarz@iaik.tugraz.at>
 *
 * Major changes to the original code by: Dave Hansen <dave.hansen@intel.com>
 * Mostly rewritten by Thomas Gleixner <tglx@linutronix.de> and
 *		       Andy Lutomirsky <luto@amacapital.net>
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/bug.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/uaccess.h>

#include <asm/cpufeature.h>
#include <asm/hypervisor.h>
#include <asm/vsyscall.h>
#include <asm/cmdline.h>
#include <asm/pti.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/tlbflush.h>
#include <asm/desc.h>

#undef pr_fmt
#define pr_fmt(fmt)     "Kernel/User page tables isolation: " fmt

/* Backporting helper */
#ifndef __GFP_NOTRACK
#define __GFP_NOTRACK	0
#endif

static void __init pti_print_if_insecure(const char *reason)
{
	if (boot_cpu_has_bug(X86_BUG_CPU_MELTDOWN))
		pr_info("%s\n", reason);
}

static void __init pti_print_if_secure(const char *reason)
{
	if (!boot_cpu_has_bug(X86_BUG_CPU_MELTDOWN))
		pr_info("%s\n", reason);
}

enum pti_mode {
	PTI_AUTO = 0,
	PTI_FORCE_OFF,
	PTI_FORCE_ON
} pti_mode;

void __init pti_check_boottime_disable(void)
{
	char arg[5];
	int ret;

	/* Assume mode is auto unless overridden. */
	pti_mode = PTI_AUTO;

	if (hypervisor_is_type(X86_HYPER_XEN_PV)) {
		pti_mode = PTI_FORCE_OFF;
		pti_print_if_insecure("disabled on XEN PV.");
		return;
	}

	ret = cmdline_find_option(boot_command_line, "pti", arg, sizeof(arg));
	if (ret > 0)  {
		if (ret == 3 && !strncmp(arg, "off", 3)) {
			pti_mode = PTI_FORCE_OFF;
			pti_print_if_insecure("disabled on command line.");
			return;
		}
		if (ret == 2 && !strncmp(arg, "on", 2)) {
			pti_mode = PTI_FORCE_ON;
			pti_print_if_secure("force enabled on command line.");
			goto enable;
		}
		if (ret == 4 && !strncmp(arg, "auto", 4)) {
			pti_mode = PTI_AUTO;
			goto autosel;
		}
	}

	if (cmdline_find_option_bool(boot_command_line, "nopti")) {
		pti_mode = PTI_FORCE_OFF;
		pti_print_if_insecure("disabled on command line.");
		return;
	}

autosel:
	if (!boot_cpu_has_bug(X86_BUG_CPU_MELTDOWN))
		return;
enable:
	setup_force_cpu_cap(X86_FEATURE_PTI);
}

pgd_t __pti_set_user_pgd(pgd_t *pgdp, pgd_t pgd)
{
	/*
	 * Changes to the high (kernel) portion of the kernelmode page
	 * tables are not automatically propagated to the usermode tables.
	 *
	 * Users should keep in mind that, unlike the kernelmode tables,
	 * there is no vmalloc_fault equivalent for the usermode tables.
	 * Top-level entries added to init_mm's usermode pgd after boot
	 * will not be automatically propagated to other mms.
	 */
	if (!pgdp_maps_userspace(pgdp))
		return pgd;

	/*
	 * The user page tables get the full PGD, accessible from
	 * userspace:
	 */
	kernel_to_user_pgdp(pgdp)->pgd = pgd.pgd;

	/*
	 * If this is normal user memory, make it NX in the kernel
	 * pagetables so that, if we somehow screw up and return to
	 * usermode with the kernel CR3 loaded, we'll get a page fault
	 * instead of allowing user code to execute with the wrong CR3.
	 *
	 * As exceptions, we don't set NX if:
	 *  - _PAGE_USER is not set.  This could be an executable
	 *     EFI runtime mapping or something similar, and the kernel
	 *     may execute from it
	 *  - we don't have NX support
	 *  - we're clearing the PGD (i.e. the new pgd is not present).
	 */
	if ((pgd.pgd & (_PAGE_USER|_PAGE_PRESENT)) == (_PAGE_USER|_PAGE_PRESENT) &&
	    (__supported_pte_mask & _PAGE_NX))
		pgd.pgd |= _PAGE_NX;

	/* return the copy of the PGD we want the kernel to use: */
	return pgd;
}

/*
 * Walk the user copy of the page tables (optionally) trying to allocate
 * page table pages on the way down.
 *
 * Returns a pointer to a P4D on success, or NULL on failure.
 */
static p4d_t *pti_user_pagetable_walk_p4d(unsigned long address)
{
	pgd_t *pgd = kernel_to_user_pgdp(pgd_offset_k(address));
	gfp_t gfp = (GFP_KERNEL | __GFP_NOTRACK | __GFP_ZERO);

	if (address < PAGE_OFFSET) {
		WARN_ONCE(1, "attempt to walk user address\n");
		return NULL;
	}

	if (pgd_none(*pgd)) {
		unsigned long new_p4d_page = __get_free_page(gfp);
		if (!new_p4d_page)
			return NULL;

		set_pgd(pgd, __pgd(_KERNPG_TABLE | __pa(new_p4d_page)));
	}
	BUILD_BUG_ON(pgd_large(*pgd) != 0);

	return p4d_offset(pgd, address);
}

/*
 * Walk the user copy of the page tables (optionally) trying to allocate
 * page table pages on the way down.
 *
 * Returns a pointer to a PMD on success, or NULL on failure.
 */
static pmd_t *pti_user_pagetable_walk_pmd(unsigned long address)
{
	gfp_t gfp = (GFP_KERNEL | __GFP_NOTRACK | __GFP_ZERO);
	p4d_t *p4d = pti_user_pagetable_walk_p4d(address);
	pud_t *pud;

	BUILD_BUG_ON(p4d_large(*p4d) != 0);
	if (p4d_none(*p4d)) {
		unsigned long new_pud_page = __get_free_page(gfp);
		if (!new_pud_page)
			return NULL;

		set_p4d(p4d, __p4d(_KERNPG_TABLE | __pa(new_pud_page)));
	}

	pud = pud_offset(p4d, address);
	/* The user page tables do not use large mappings: */
	if (pud_large(*pud)) {
		WARN_ON(1);
		return NULL;
	}
	if (pud_none(*pud)) {
		unsigned long new_pmd_page = __get_free_page(gfp);
		if (!new_pmd_page)
			return NULL;

		set_pud(pud, __pud(_KERNPG_TABLE | __pa(new_pmd_page)));
	}

	return pmd_offset(pud, address);
}

#ifdef CONFIG_X86_VSYSCALL_EMULATION
/*
 * Walk the shadow copy of the page tables (optionally) trying to allocate
 * page table pages on the way down.  Does not support large pages.
 *
 * Note: this is only used when mapping *new* kernel data into the
 * user/shadow page tables.  It is never used for userspace data.
 *
 * Returns a pointer to a PTE on success, or NULL on failure.
 */
static __init pte_t *pti_user_pagetable_walk_pte(unsigned long address)
{
	gfp_t gfp = (GFP_KERNEL | __GFP_NOTRACK | __GFP_ZERO);
	pmd_t *pmd = pti_user_pagetable_walk_pmd(address);
	pte_t *pte;

	/* We can't do anything sensible if we hit a large mapping. */
	if (pmd_large(*pmd)) {
		WARN_ON(1);
		return NULL;
	}

	if (pmd_none(*pmd)) {
		unsigned long new_pte_page = __get_free_page(gfp);
		if (!new_pte_page)
			return NULL;

		set_pmd(pmd, __pmd(_KERNPG_TABLE | __pa(new_pte_page)));
	}

	pte = pte_offset_kernel(pmd, address);
	if (pte_flags(*pte) & _PAGE_USER) {
		WARN_ONCE(1, "attempt to walk to user pte\n");
		return NULL;
	}
	return pte;
}

static void __init pti_setup_vsyscall(void)
{
	pte_t *pte, *target_pte;
	unsigned int level;

	pte = lookup_address(VSYSCALL_ADDR, &level);
	if (!pte || WARN_ON(level != PG_LEVEL_4K) || pte_none(*pte))
		return;

	target_pte = pti_user_pagetable_walk_pte(VSYSCALL_ADDR);
	if (WARN_ON(!target_pte))
		return;

	*target_pte = *pte;
	set_vsyscall_pgtable_user_bits(kernel_to_user_pgdp(swapper_pg_dir));
}
#else
static void __init pti_setup_vsyscall(void) { }
#endif

static void
pti_clone_pmds(unsigned long start, unsigned long end, pmdval_t clear)
{
	unsigned long addr;

	/*
	 * Clone the populated PMDs which cover start to end. These PMD areas
	 * can have holes.
	 */
	for (addr = start; addr < end; addr += PMD_SIZE) {
		pmd_t *pmd, *target_pmd;
		pgd_t *pgd;
		p4d_t *p4d;
		pud_t *pud;

		pgd = pgd_offset_k(addr);
		if (WARN_ON(pgd_none(*pgd)))
			return;
		p4d = p4d_offset(pgd, addr);
		if (WARN_ON(p4d_none(*p4d)))
			return;
		pud = pud_offset(p4d, addr);
		if (pud_none(*pud))
			continue;
		pmd = pmd_offset(pud, addr);
		if (pmd_none(*pmd))
			continue;

		target_pmd = pti_user_pagetable_walk_pmd(addr);
		if (WARN_ON(!target_pmd))
			return;

		/*
		 * Only clone present PMDs.  This ensures only setting
		 * _PAGE_GLOBAL on present PMDs.  This should only be
		 * called on well-known addresses anyway, so a non-
		 * present PMD would be a surprise.
		 */
		if (WARN_ON(!(pmd_flags(*pmd) & _PAGE_PRESENT)))
			return;

		/*
		 * Setting 'target_pmd' below creates a mapping in both
		 * the user and kernel page tables.  It is effectively
		 * global, so set it as global in both copies.  Note:
		 * the X86_FEATURE_PGE check is not _required_ because
		 * the CPU ignores _PAGE_GLOBAL when PGE is not
		 * supported.  The check keeps consistentency with
		 * code that only set this bit when supported.
		 */
		if (boot_cpu_has(X86_FEATURE_PGE))
			*pmd = pmd_set_flags(*pmd, _PAGE_GLOBAL);

		/*
		 * Copy the PMD.  That is, the kernelmode and usermode
		 * tables will share the last-level page tables of this
		 * address range
		 */
		*target_pmd = pmd_clear_flags(*pmd, clear);
	}
}

/*
 * Clone a single p4d (i.e. a top-level entry on 4-level systems and a
 * next-level entry on 5-level systems.
 */
static void __init pti_clone_p4d(unsigned long addr)
{
	p4d_t *kernel_p4d, *user_p4d;
	pgd_t *kernel_pgd;

	user_p4d = pti_user_pagetable_walk_p4d(addr);
	kernel_pgd = pgd_offset_k(addr);
	kernel_p4d = p4d_offset(kernel_pgd, addr);
	*user_p4d = *kernel_p4d;
}

/*
 * Clone the CPU_ENTRY_AREA into the user space visible page table.
 */
static void __init pti_clone_user_shared(void)
{
	pti_clone_p4d(CPU_ENTRY_AREA_BASE);
}

/*
 * Clone the ESPFIX P4D into the user space visible page table
 */
static void __init pti_setup_espfix64(void)
{
#ifdef CONFIG_X86_ESPFIX64
	pti_clone_p4d(ESPFIX_BASE_ADDR);
#endif
}

/*
 * Clone the populated PMDs of the entry and irqentry text and force it RO.
 */
static void __init pti_clone_entry_text(void)
{
	pti_clone_pmds((unsigned long) __entry_text_start,
			(unsigned long) __irqentry_text_end,
		       _PAGE_RW);
}

/*
 * Global pages and PCIDs are both ways to make kernel TLB entries
 * live longer, reduce TLB misses and improve kernel performance.
 * But, leaving all kernel text Global makes it potentially accessible
 * to Meltdown-style attacks which make it trivial to find gadgets or
 * defeat KASLR.
 *
 * Only use global pages when it is really worth it.
 */
static inline bool pti_kernel_image_global_ok(void)
{
	/*
	 * Systems with PCIDs get litlle benefit from global
	 * kernel text and are not worth the downsides.
	 */
	if (cpu_feature_enabled(X86_FEATURE_PCID))
		return false;

	/*
	 * Only do global kernel image for pti=auto.  Do the most
	 * secure thing (not global) if pti=on specified.
	 */
	if (pti_mode != PTI_AUTO)
		return false;

	/*
	 * K8 may not tolerate the cleared _PAGE_RW on the userspace
	 * global kernel image pages.  Do the safe thing (disable
	 * global kernel image).  This is unlikely to ever be
	 * noticed because PTI is disabled by default on AMD CPUs.
	 */
	if (boot_cpu_has(X86_FEATURE_K8))
		return false;

	return true;
}

/*
 * For some configurations, map all of kernel text into the user page
 * tables.  This reduces TLB misses, especially on non-PCID systems.
 */
void pti_clone_kernel_text(void)
{
	unsigned long start = PFN_ALIGN(_text);
	unsigned long end = ALIGN((unsigned long)_end, PMD_PAGE_SIZE);

	if (!pti_kernel_image_global_ok())
		return;

	pti_clone_pmds(start, end, _PAGE_RW);
}

/*
 * This is the only user for it and it is not arch-generic like
 * the other set_memory.h functions.  Just extern it.
 */
extern int set_memory_nonglobal(unsigned long addr, int numpages);
void pti_set_kernel_image_nonglobal(void)
{
	/*
	 * The identity map is created with PMDs, regardless of the
	 * actual length of the kernel.  We need to clear
	 * _PAGE_GLOBAL up to a PMD boundary, not just to the end
	 * of the image.
	 */
	unsigned long start = PFN_ALIGN(_text);
	unsigned long end = ALIGN((unsigned long)_end, PMD_PAGE_SIZE);

	if (pti_kernel_image_global_ok())
		return;

	pr_debug("set kernel image non-global\n");

	set_memory_nonglobal(start, (end - start) >> PAGE_SHIFT);
}

/*
 * Initialize kernel page table isolation
 */
void __init pti_init(void)
{
	if (!static_cpu_has(X86_FEATURE_PTI))
		return;

	pr_info("enabled\n");

	pti_clone_user_shared();

	/* Undo all global bits from the init pagetables in head_64.S: */
	pti_set_kernel_image_nonglobal();
	/* Replace some of the global bits just for shared entry text: */
	pti_clone_entry_text();
	pti_setup_espfix64();
	pti_setup_vsyscall();
}
