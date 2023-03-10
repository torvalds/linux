/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _ASM_POWERPC_PAGE_H
#define _ASM_POWERPC_PAGE_H

/*
 * Copyright (C) 2001,2005 IBM Corporation.
 */

#ifndef __ASSEMBLY__
#include <linux/types.h>
#include <linux/kernel.h>
#else
#include <asm/types.h>
#endif
#include <asm/asm-const.h>

/*
 * On regular PPC32 page size is 4K (but we support 4K/16K/64K/256K pages
 * on PPC44x and 4K/16K on 8xx). For PPC64 we support either 4K or 64K software
 * page size. When using 64K pages however, whether we are really supporting
 * 64K pages in HW or not is irrelevant to those definitions.
 */
#define PAGE_SHIFT		CONFIG_PPC_PAGE_SHIFT
#define PAGE_SIZE		(ASM_CONST(1) << PAGE_SHIFT)

#ifndef __ASSEMBLY__
#ifndef CONFIG_HUGETLB_PAGE
#define HPAGE_SHIFT PAGE_SHIFT
#elif defined(CONFIG_PPC_BOOK3S_64)
extern unsigned int hpage_shift;
#define HPAGE_SHIFT hpage_shift
#elif defined(CONFIG_PPC_8xx)
#define HPAGE_SHIFT		19	/* 512k pages */
#elif defined(CONFIG_PPC_E500)
#define HPAGE_SHIFT		22	/* 4M pages */
#endif
#define HPAGE_SIZE		((1UL) << HPAGE_SHIFT)
#define HPAGE_MASK		(~(HPAGE_SIZE - 1))
#define HUGETLB_PAGE_ORDER	(HPAGE_SHIFT - PAGE_SHIFT)
#define HUGE_MAX_HSTATE		(MMU_PAGE_COUNT-1)
#endif

/*
 * Subtle: (1 << PAGE_SHIFT) is an int, not an unsigned long. So if we
 * assign PAGE_MASK to a larger type it gets extended the way we want
 * (i.e. with 1s in the high bits)
 */
#define PAGE_MASK      (~((1 << PAGE_SHIFT) - 1))

/*
 * KERNELBASE is the virtual address of the start of the kernel, it's often
 * the same as PAGE_OFFSET, but _might not be_.
 *
 * The kdump dump kernel is one example where KERNELBASE != PAGE_OFFSET.
 *
 * PAGE_OFFSET is the virtual address of the start of lowmem.
 *
 * PHYSICAL_START is the physical address of the start of the kernel.
 *
 * MEMORY_START is the physical address of the start of lowmem.
 *
 * KERNELBASE, PAGE_OFFSET, and PHYSICAL_START are all configurable on
 * ppc32 and based on how they are set we determine MEMORY_START.
 *
 * For the linear mapping the following equation should be true:
 * KERNELBASE - PAGE_OFFSET = PHYSICAL_START - MEMORY_START
 *
 * Also, KERNELBASE >= PAGE_OFFSET and PHYSICAL_START >= MEMORY_START
 *
 * There are two ways to determine a physical address from a virtual one:
 * va = pa + PAGE_OFFSET - MEMORY_START
 * va = pa + KERNELBASE - PHYSICAL_START
 *
 * If you want to know something's offset from the start of the kernel you
 * should subtract KERNELBASE.
 *
 * If you want to test if something's a kernel address, use is_kernel_addr().
 */

#define KERNELBASE      ASM_CONST(CONFIG_KERNEL_START)
#define PAGE_OFFSET	ASM_CONST(CONFIG_PAGE_OFFSET)
#define LOAD_OFFSET	ASM_CONST((CONFIG_KERNEL_START-CONFIG_PHYSICAL_START))

#if defined(CONFIG_NONSTATIC_KERNEL)
#ifndef __ASSEMBLY__

extern phys_addr_t memstart_addr;
extern phys_addr_t kernstart_addr;

#if defined(CONFIG_RELOCATABLE) && defined(CONFIG_PPC32)
extern long long virt_phys_offset;
#endif

#endif /* __ASSEMBLY__ */
#define PHYSICAL_START	kernstart_addr

#else	/* !CONFIG_NONSTATIC_KERNEL */
#define PHYSICAL_START	ASM_CONST(CONFIG_PHYSICAL_START)
#endif

/* See Description below for VIRT_PHYS_OFFSET */
#if defined(CONFIG_PPC32) && defined(CONFIG_BOOKE)
#ifdef CONFIG_RELOCATABLE
#define VIRT_PHYS_OFFSET virt_phys_offset
#else
#define VIRT_PHYS_OFFSET (KERNELBASE - PHYSICAL_START)
#endif
#endif

#ifdef CONFIG_PPC64
#define MEMORY_START	0UL
#elif defined(CONFIG_NONSTATIC_KERNEL)
#define MEMORY_START	memstart_addr
#else
#define MEMORY_START	(PHYSICAL_START + PAGE_OFFSET - KERNELBASE)
#endif

#ifdef CONFIG_FLATMEM
#define ARCH_PFN_OFFSET		((unsigned long)(MEMORY_START >> PAGE_SHIFT))
#endif

#define virt_to_pfn(kaddr)	(__pa(kaddr) >> PAGE_SHIFT)
#define virt_to_page(kaddr)	pfn_to_page(virt_to_pfn(kaddr))
#define pfn_to_kaddr(pfn)	__va((pfn) << PAGE_SHIFT)

#define virt_addr_valid(vaddr)	({					\
	unsigned long _addr = (unsigned long)vaddr;			\
	_addr >= PAGE_OFFSET && _addr < (unsigned long)high_memory &&	\
	pfn_valid(virt_to_pfn(_addr));					\
})

/*
 * On Book-E parts we need __va to parse the device tree and we can't
 * determine MEMORY_START until then.  However we can determine PHYSICAL_START
 * from information at hand (program counter, TLB lookup).
 *
 * On BookE with RELOCATABLE && PPC32
 *
 *   With RELOCATABLE && PPC32,  we support loading the kernel at any physical
 *   address without any restriction on the page alignment.
 *
 *   We find the runtime address of _stext and relocate ourselves based on 
 *   the following calculation:
 *
 *  	  virtual_base = ALIGN_DOWN(KERNELBASE,256M) +
 *  				MODULO(_stext.run,256M)
 *   and create the following mapping:
 *
 * 	  ALIGN_DOWN(_stext.run,256M) => ALIGN_DOWN(KERNELBASE,256M)
 *
 *   When we process relocations, we cannot depend on the
 *   existing equation for the __va()/__pa() translations:
 *
 * 	   __va(x) = (x)  - PHYSICAL_START + KERNELBASE
 *
 *   Where:
 *   	 PHYSICAL_START = kernstart_addr = Physical address of _stext
 *  	 KERNELBASE = Compiled virtual address of _stext.
 *
 *   This formula holds true iff, kernel load address is TLB page aligned.
 *
 *   In our case, we need to also account for the shift in the kernel Virtual 
 *   address.
 *
 *   E.g.,
 *
 *   Let the kernel be loaded at 64MB and KERNELBASE be 0xc0000000 (same as PAGE_OFFSET).
 *   In this case, we would be mapping 0 to 0xc0000000, and kernstart_addr = 64M
 *
 *   Now __va(1MB) = (0x100000) - (0x4000000) + 0xc0000000
 *                 = 0xbc100000 , which is wrong.
 *
 *   Rather, it should be : 0xc0000000 + 0x100000 = 0xc0100000
 *      	according to our mapping.
 *
 *   Hence we use the following formula to get the translations right:
 *
 * 	  __va(x) = (x) - [ PHYSICAL_START - Effective KERNELBASE ]
 *
 * 	  Where :
 * 		PHYSICAL_START = dynamic load address.(kernstart_addr variable)
 * 		Effective KERNELBASE = virtual_base =
 * 				     = ALIGN_DOWN(KERNELBASE,256M) +
 * 						MODULO(PHYSICAL_START,256M)
 *
 * 	To make the cost of __va() / __pa() more light weight, we introduce
 * 	a new variable virt_phys_offset, which will hold :
 *
 * 	virt_phys_offset = Effective KERNELBASE - PHYSICAL_START
 * 			 = ALIGN_DOWN(KERNELBASE,256M) - 
 * 			 	ALIGN_DOWN(PHYSICALSTART,256M)
 *
 * 	Hence :
 *
 * 	__va(x) = x - PHYSICAL_START + Effective KERNELBASE
 * 		= x + virt_phys_offset
 *
 * 		and
 * 	__pa(x) = x + PHYSICAL_START - Effective KERNELBASE
 * 		= x - virt_phys_offset
 * 		
 * On non-Book-E PPC64 PAGE_OFFSET and MEMORY_START are constants so use
 * the other definitions for __va & __pa.
 */
#if defined(CONFIG_PPC32) && defined(CONFIG_BOOKE)
#define __va(x) ((void *)(unsigned long)((phys_addr_t)(x) + VIRT_PHYS_OFFSET))
#define __pa(x) ((phys_addr_t)(unsigned long)(x) - VIRT_PHYS_OFFSET)
#else
#ifdef CONFIG_PPC64

#define VIRTUAL_WARN_ON(x)	WARN_ON(IS_ENABLED(CONFIG_DEBUG_VIRTUAL) && (x))

/*
 * gcc miscompiles (unsigned long)(&static_var) - PAGE_OFFSET
 * with -mcmodel=medium, so we use & and | instead of - and + on 64-bit.
 * This also results in better code generation.
 */
#define __va(x)								\
({									\
	VIRTUAL_WARN_ON((unsigned long)(x) >= PAGE_OFFSET);		\
	(void *)(unsigned long)((phys_addr_t)(x) | PAGE_OFFSET);	\
})

#define __pa(x)								\
({									\
	VIRTUAL_WARN_ON((unsigned long)(x) < PAGE_OFFSET);		\
	(unsigned long)(x) & 0x0fffffffffffffffUL;			\
})

#else /* 32-bit, non book E */
#define __va(x) ((void *)(unsigned long)((phys_addr_t)(x) + PAGE_OFFSET - MEMORY_START))
#define __pa(x) ((unsigned long)(x) - PAGE_OFFSET + MEMORY_START)
#endif
#endif

/*
 * Unfortunately the PLT is in the BSS in the PPC32 ELF ABI,
 * and needs to be executable.  This means the whole heap ends
 * up being executable.
 */
#define VM_DATA_DEFAULT_FLAGS32	VM_DATA_FLAGS_TSK_EXEC
#define VM_DATA_DEFAULT_FLAGS64	VM_DATA_FLAGS_NON_EXEC

#ifdef __powerpc64__
#include <asm/page_64.h>
#else
#include <asm/page_32.h>
#endif

/*
 * Don't compare things with KERNELBASE or PAGE_OFFSET to test for
 * "kernelness", use is_kernel_addr() - it should do what you want.
 */
#ifdef CONFIG_PPC_BOOK3E_64
#define is_kernel_addr(x)	((x) >= 0x8000000000000000ul)
#elif defined(CONFIG_PPC_BOOK3S_64)
#define is_kernel_addr(x)	((x) >= PAGE_OFFSET)
#else
#define is_kernel_addr(x)	((x) >= TASK_SIZE)
#endif

#ifndef CONFIG_PPC_BOOK3S_64
/*
 * Use the top bit of the higher-level page table entries to indicate whether
 * the entries we point to contain hugepages.  This works because we know that
 * the page tables live in kernel space.  If we ever decide to support having
 * page tables at arbitrary addresses, this breaks and will have to change.
 */
#ifdef CONFIG_PPC64
#define PD_HUGE 0x8000000000000000UL
#else
#define PD_HUGE 0x80000000
#endif

#else	/* CONFIG_PPC_BOOK3S_64 */
/*
 * Book3S 64 stores real addresses in the hugepd entries to
 * avoid overlaps with _PAGE_PRESENT and _PAGE_PTE.
 */
#define HUGEPD_ADDR_MASK	(0x0ffffffffffffffful & ~HUGEPD_SHIFT_MASK)
#endif /* CONFIG_PPC_BOOK3S_64 */

/*
 * Some number of bits at the level of the page table that points to
 * a hugepte are used to encode the size.  This masks those bits.
 * On 8xx, HW assistance requires 4k alignment for the hugepte.
 */
#ifdef CONFIG_PPC_8xx
#define HUGEPD_SHIFT_MASK     0xfff
#else
#define HUGEPD_SHIFT_MASK     0x3f
#endif

#ifndef __ASSEMBLY__

#ifdef CONFIG_PPC_BOOK3S_64
#include <asm/pgtable-be-types.h>
#else
#include <asm/pgtable-types.h>
#endif

struct page;
extern void clear_user_page(void *page, unsigned long vaddr, struct page *pg);
extern void copy_user_page(void *to, void *from, unsigned long vaddr,
		struct page *p);
extern int devmem_is_allowed(unsigned long pfn);

#ifdef CONFIG_PPC_SMLPAR
void arch_free_page(struct page *page, int order);
#define HAVE_ARCH_FREE_PAGE
#endif

struct vm_area_struct;

extern unsigned long kernstart_virt_addr;

static inline unsigned long kaslr_offset(void)
{
	return kernstart_virt_addr - KERNELBASE;
}

#include <asm-generic/memory_model.h>
#endif /* __ASSEMBLY__ */

#endif /* _ASM_POWERPC_PAGE_H */
