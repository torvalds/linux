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
#include <linux/cpu.h>

#include <asm/cpufeature.h>
#include <asm/hypervisor.h>
#include <asm/vsyscall.h>
#include <asm/cmdline.h>
#include <asm/pti.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/tlbflush.h>
#include <asm/desc.h>
#include <asm/sections.h>

#undef pr_fmt
#define pr_fmt(fmt)     "Kernel/User page tables isolation: " fmt

/* Backporting helper */
#ifndef __GFP_NOTRACK
#define __GFP_NOTRACK	0
#endif

/*
 * Define the page-table levels we clone for user-space on 32
 * and 64 bit.
 */
#ifdef CONFIG_X86_64
#define	PTI_LEVEL_KERNEL_IMAGE	PTI_CLONE_PMD
#else
#define	PTI_LEVEL_KERNEL_IMAGE	PTI_CLONE_PTE
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

	if (cmdline_find_option_bool(boot_command_line, "nopti") ||
	    cpu_mitigations_off()) {
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

pgd_t __pti_set_user_pgtbl(pgd_t *pgdp, pgd_t pgd)
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
		if (WARN_ON_ONCE(!new_p4d_page))
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
	p4d_t *p4d;
	pud_t *pud;

	p4d = pti_user_pagetable_walk_p4d(address);
	if (!p4d)
		return NULL;

	BUILD_BUG_ON(p4d_large(*p4d) != 0);
	if (p4d_none(*p4d)) {
		unsigned long new_pud_page = __get_free_page(gfp);
		if (WARN_ON_ONCE(!new_pud_page))
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
		if (WARN_ON_ONCE(!new_pmd_page))
			return NULL;

		set_pud(pud, __pud(_KERNPG_TABLE | __pa(new_pmd_page)));
	}

	return pmd_offset(pud, address);
}

/*
 * Walk the shadow copy of the page tables (optionally) trying to allocate
 * page table pages on the way down.  Does not support large pages.
 *
 * Note: this is only used when mapping *new* kernel data into the
 * user/shadow page tables.  It is never used for userspace data.
 *
 * Returns a pointer to a PTE on success, or NULL on failure.
 */
static pte_t *pti_user_pagetable_walk_pte(unsigned long address)
{
	gfp_t gfp = (GFP_KERNEL | __GFP_NOTRACK | __GFP_ZERO);
	pmd_t *pmd;
	pte_t *pte;

	pmd = pti_user_pagetable_walk_pmd(address);
	if (!pmd)
		return NULL;

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

#ifdef CONFIG_X86_VSYSCALL_EMULATION
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

enum pti_clone_level {
	PTI_CLONE_PMD,
	PTI_CLONE_PTE,
};

static void
pti_clone_pgtable(unsigned long start, unsigned long end,
		  enum pti_clone_level level)
{
	unsigned long addr;

	/*
	 * Clone the populated PMDs which cover start to end. These PMD areas
	 * can have holes.
	 */
	for (addr = start; addr < end;) {
		pte_t *pte, *target_pte;
		pmd_t *pmd, *target_pmd;
		pgd_t *pgd;
		p4d_t *p4d;
		pud_t *pud;

		/* Overflow check */
		if (addr < start)
			break;

		pgd = pgd_offset_k(addr);
		if (WARN_ON(pgd_none(*pgd)))
			return;
		p4d = p4d_offset(pgd, addr);
		if (WARN_ON(p4d_none(*p4d)))
			return;

		pud = pud_offset(p4d, addr);
		if (pud_none(*pud)) {
			WARN_ON_ONCE(addr & ~PUD_MASK);
			addr = round_up(addr + 1, PUD_SIZE);
			continue;
		}

		pmd = pmd_offset(pud, addr);
		if (pmd_none(*pmd)) {
			WARN_ON_ONCE(addr & ~PMD_MASK);
			addr = round_up(addr + 1, PMD_SIZE);
			continue;
		}

		if (pmd_large(*pmd) || level == PTI_CLONE_PMD) {
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
			*target_pmd = *pmd;

			addr += PMD_SIZE;

		} else if (level == PTI_CLONE_PTE) {

			/* Walk the page-table down to the pte level */
			pte = pte_offset_kernel(pmd, addr);
			if (pte_none(*pte)) {
				addr += PAGE_SIZE;
				continue;
			}

			/* Only clone present PTEs */
			if (WARN_ON(!(pte_flags(*pte) & _PAGE_PRESENT)))
				return;

			/* Allocate PTE in the user page-table */
			target_pte = pti_user_pagetable_walk_pte(addr);
			if (WARN_ON(!target_pte))
				return;

			/* Set GLOBAL bit in both PTEs */
			if (boot_cpu_has(X86_FEATURE_PGE))
				*pte = pte_set_flags(*pte, _PAGE_GLOBAL);

			/* Clone the PTE */
			*target_pte = *pte;

			addr += PAGE_SIZE;

		} else {
			BUG();
		}
	}
}

#ifdef CONFIG_X86_64
/*
 * Clone a single p4d (i.e. a top-level entry on 4-level systems and a
 * next-level entry on 5-level systems.
 */
static void __init pti_clone_p4d(unsigned long addr)
{
	p4d_t *kernel_p4d, *user_p4d;
	pgd_t *kernel_pgd;

	user_p4d = pti_user_pagetable_walk_p4d(addr);
	if (!user_p4d)
		return;

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

#else /* CONFIG_X86_64 */

/*
 * On 32 bit PAE systems with 1GB of Kernel address space there is only
 * one pgd/p4d for the whole kernel. Cloning that would map the whole
 * address space into the user page-tables, making PTI useless. So clone
 * the page-table on the PMD level to prevent that.
 */
static void __init pti_clone_user_shared(void)
{
	unsigned long start, end;

	start = CPU_ENTRY_AREA_BASE;
	end   = start + (PAGE_SIZE * CPU_ENTRY_AREA_PAGES);

	pti_clone_pgtable(start, end, PTI_CLONE_PMD);
}
#endif /* CONFIG_X86_64 */

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
static void pti_clone_entry_text(void)
{
	pti_clone_pgtable((unsigned long) __entry_text_start,
			  (unsigned long) __irqentry_text_end,
			  PTI_CLONE_PMD);

	/*
	 * If CFI is enabled, also map jump tables, so the entry code can
	 * make indirect calls.
	 */
	if (IS_ENABLED(CONFIG_CFI_CLANG))
		pti_clone_pgtable((unsigned long) __cfi_jt_start,
				  (unsigned long) __cfi_jt_end,
				  PTI_CLONE_PMD);
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

	/*
	 * RANDSTRUCT derives its hardening benefits from the
	 * attacker's lack of knowledge about the layout of kernel
	 * data structures.  Keep the kernel image non-global in
	 * cases where RANDSTRUCT is in use to help keep the layout a
	 * secret.
	 */
	if (IS_ENABLED(CONFIG_GCC_PLUGIN_RANDSTRUCT))
		return false;

	return true;
}

/*
 * This is the only user for these and it is not arch-generic
 * like the other set_memory.h functions.  Just extern them.
 */
extern int set_memory_nonglobal(unsigned long addr, int numpages);
extern int set_memory_global(unsigned long addr, int numpages);

/*
 * For some configurations, map all of kernel text into the user page
 * tables.  This reduces TLB misses, especially on non-PCID systems.
 */
static void pti_clone_kernel_text(void)
{
	/*
	 * rodata is part of the kernel image and is normally
	 * readable on the filesystem or on the web.  But, do not
	 * clone the areas past rodata, they might contain secrets.
	 */
	unsigned long start = PFN_ALIGN(_text);
	unsigned long end_clone  = (unsigned long)__end_rodata_aligned;
	unsigned long end_global = PFN_ALIGN((unsigned long)_etext);

	if (!pti_kernel_image_global_ok())
		return;

	pr_debug("mapping partial kernel image into user address space\n");

	/*
	 * Note that this will undo _some_ of the work that
	 * pti_set_kernel_image_nonglobal() did to clear the
	 * global bit.
	 */
	pti_clone_pgtable(start, end_clone, PTI_LEVEL_KERNEL_IMAGE);

	/*
	 * pti_clone_pgtable() will set the global bit in any PMDs
	 * that it clones, but we also need to get any PTEs in
	 * the last level for areas that are not huge-page-aligned.
	 */

	/* Set the global bit for normal non-__init kernel text: */
	set_memory_global(start, (end_global - start) >> PAGE_SHIFT);
}

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

	/*
	 * This clears _PAGE_GLOBAL from the entire kernel image.
	 * pti_clone_kernel_text() map put _PAGE_GLOBAL back for
	 * areas that are mapped to userspace.
	 */
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

#ifdef CONFIG_X86_32
	/*
	 * We check for X86_FEATURE_PCID here. But the init-code will
	 * clear the feature flag on 32 bit because the feature is not
	 * supported on 32 bit anyway. To print the warning we need to
	 * check with cpuid directly again.
	 */
	if (cpuid_ecx(0x1) & BIT(17)) {
		/* Use printk to work around pr_fmt() */
		printk(KERN_WARNING "\n");
		printk(KERN_WARNING "************************************************************\n");
		printk(KERN_WARNING "** WARNING! WARNING! WARNING! WARNING! WARNING! WARNING!  **\n");
		printk(KERN_WARNING "**                                                        **\n");
		printk(KERN_WARNING "** You are using 32-bit PTI on a 64-bit PCID-capable CPU. **\n");
		printk(KERN_WARNING "** Your performance will increase dramatically if you     **\n");
		printk(KERN_WARNING "** switch to a 64-bit kernel!                             **\n");
		printk(KERN_WARNING "**                                                        **\n");
		printk(KERN_WARNING "** WARNING! WARNING! WARNING! WARNING! WARNING! WARNING!  **\n");
		printk(KERN_WARNING "************************************************************\n");
	}
#endif

	pti_clone_user_shared();

	/* Undo all global bits from the init pagetables in head_64.S: */
	pti_set_kernel_image_nonglobal();
	/* Replace some of the global bits just for shared entry text: */
	pti_clone_entry_text();
	pti_setup_espfix64();
	pti_setup_vsyscall();
}

/*
 * Finalize the kernel mappings in the userspace page-table. Some of the
 * mappings for the kernel image might have changed since pti_init()
 * cloned them. This is because parts of the kernel image have been
 * mapped RO and/or NX.  These changes need to be cloned again to the
 * userspace page-table.
 */
void pti_finalize(void)
{
	if (!boot_cpu_has(X86_FEATURE_PTI))
		return;
	/*
	 * We need to clone everything (again) that maps parts of the
	 * kernel image.
	 */
	pti_clone_entry_text();
	pti_clone_kernel_text();

	debug_checkwx_user();
}
