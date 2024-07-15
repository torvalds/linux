/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2009 Chen Liqin <liqin.chen@sunplusct.com>
 * Copyright (C) 2012 Regents of the University of California
 * Copyright (C) 2017 SiFive
 * Copyright (C) 2017 XiaojingZhu <zhuxiaoj@ict.ac.cn>
 */

#ifndef _ASM_RISCV_PAGE_H
#define _ASM_RISCV_PAGE_H

#include <linux/pfn.h>
#include <linux/const.h>

#define PAGE_SHIFT	CONFIG_PAGE_SHIFT
#define PAGE_SIZE	(_AC(1, UL) << PAGE_SHIFT)
#define PAGE_MASK	(~(PAGE_SIZE - 1))

#define HPAGE_SHIFT		PMD_SHIFT
#define HPAGE_SIZE		(_AC(1, UL) << HPAGE_SHIFT)
#define HPAGE_MASK              (~(HPAGE_SIZE - 1))
#define HUGETLB_PAGE_ORDER      (HPAGE_SHIFT - PAGE_SHIFT)

/*
 * PAGE_OFFSET -- the first address of the first page of memory.
 * When not using MMU this corresponds to the first free page in
 * physical memory (aligned on a page boundary).
 */
#ifdef CONFIG_64BIT
#ifdef CONFIG_MMU
#define PAGE_OFFSET		kernel_map.page_offset
#else
#define PAGE_OFFSET		_AC(CONFIG_PAGE_OFFSET, UL)
#endif
/*
 * By default, CONFIG_PAGE_OFFSET value corresponds to SV57 address space so
 * define the PAGE_OFFSET value for SV48 and SV39.
 */
#define PAGE_OFFSET_L4		_AC(0xffffaf8000000000, UL)
#define PAGE_OFFSET_L3		_AC(0xffffffd800000000, UL)
#else
#define PAGE_OFFSET		_AC(CONFIG_PAGE_OFFSET, UL)
#endif /* CONFIG_64BIT */

#ifndef __ASSEMBLY__

#ifdef CONFIG_RISCV_ISA_ZICBOZ
void clear_page(void *page);
#else
#define clear_page(pgaddr)			memset((pgaddr), 0, PAGE_SIZE)
#endif
#define copy_page(to, from)			memcpy((to), (from), PAGE_SIZE)

#define clear_user_page(pgaddr, vaddr, page)	clear_page(pgaddr)
#define copy_user_page(vto, vfrom, vaddr, topg) \
			memcpy((vto), (vfrom), PAGE_SIZE)

/*
 * Use struct definitions to apply C type checking
 */

/* Page Global Directory entry */
typedef struct {
	unsigned long pgd;
} pgd_t;

/* Page Table entry */
typedef struct {
	unsigned long pte;
} pte_t;

typedef struct {
	unsigned long pgprot;
} pgprot_t;

typedef struct page *pgtable_t;

#define pte_val(x)	((x).pte)
#define pgd_val(x)	((x).pgd)
#define pgprot_val(x)	((x).pgprot)

#define __pte(x)	((pte_t) { (x) })
#define __pgd(x)	((pgd_t) { (x) })
#define __pgprot(x)	((pgprot_t) { (x) })

#ifdef CONFIG_64BIT
#define PTE_FMT "%016lx"
#else
#define PTE_FMT "%08lx"
#endif

#if defined(CONFIG_64BIT) && defined(CONFIG_MMU)
/*
 * We override this value as its generic definition uses __pa too early in
 * the boot process (before kernel_map.va_pa_offset is set).
 */
#define MIN_MEMBLOCK_ADDR      0
#endif

#ifdef CONFIG_MMU
#define ARCH_PFN_OFFSET		(PFN_DOWN((unsigned long)phys_ram_base))
#else
#define ARCH_PFN_OFFSET		(PAGE_OFFSET >> PAGE_SHIFT)
#endif /* CONFIG_MMU */

struct kernel_mapping {
	unsigned long page_offset;
	unsigned long virt_addr;
	unsigned long virt_offset;
	uintptr_t phys_addr;
	uintptr_t size;
	/* Offset between linear mapping virtual address and kernel load address */
	unsigned long va_pa_offset;
	/* Offset between kernel mapping virtual address and kernel load address */
	unsigned long va_kernel_pa_offset;
	unsigned long va_kernel_xip_pa_offset;
#ifdef CONFIG_XIP_KERNEL
	uintptr_t xiprom;
	uintptr_t xiprom_sz;
#endif
};

extern struct kernel_mapping kernel_map;
extern phys_addr_t phys_ram_base;

#define is_kernel_mapping(x)	\
	((x) >= kernel_map.virt_addr && (x) < (kernel_map.virt_addr + kernel_map.size))

#define is_linear_mapping(x)	\
	((x) >= PAGE_OFFSET && (!IS_ENABLED(CONFIG_64BIT) || (x) < PAGE_OFFSET + KERN_VIRT_SIZE))

#ifndef CONFIG_DEBUG_VIRTUAL
#define linear_mapping_pa_to_va(x)	((void *)((unsigned long)(x) + kernel_map.va_pa_offset))
#else
void *linear_mapping_pa_to_va(unsigned long x);
#endif
#define kernel_mapping_pa_to_va(y)	({					\
	unsigned long _y = (unsigned long)(y);					\
	(IS_ENABLED(CONFIG_XIP_KERNEL) && _y < phys_ram_base) ?			\
		(void *)(_y + kernel_map.va_kernel_xip_pa_offset) :		\
		(void *)(_y + kernel_map.va_kernel_pa_offset + XIP_OFFSET);	\
	})
#define __pa_to_va_nodebug(x)		linear_mapping_pa_to_va(x)

#ifndef CONFIG_DEBUG_VIRTUAL
#define linear_mapping_va_to_pa(x)	((unsigned long)(x) - kernel_map.va_pa_offset)
#else
phys_addr_t linear_mapping_va_to_pa(unsigned long x);
#endif
#define kernel_mapping_va_to_pa(y) ({						\
	unsigned long _y = (unsigned long)(y);					\
	(IS_ENABLED(CONFIG_XIP_KERNEL) && _y < kernel_map.virt_addr + XIP_OFFSET) ? \
		(_y - kernel_map.va_kernel_xip_pa_offset) :			\
		(_y - kernel_map.va_kernel_pa_offset - XIP_OFFSET);		\
	})

#define __va_to_pa_nodebug(x)	({						\
	unsigned long _x = x;							\
	is_linear_mapping(_x) ?							\
		linear_mapping_va_to_pa(_x) : kernel_mapping_va_to_pa(_x);	\
	})

#ifdef CONFIG_DEBUG_VIRTUAL
extern phys_addr_t __virt_to_phys(unsigned long x);
extern phys_addr_t __phys_addr_symbol(unsigned long x);
#else
#define __virt_to_phys(x)	__va_to_pa_nodebug(x)
#define __phys_addr_symbol(x)	__va_to_pa_nodebug(x)
#endif /* CONFIG_DEBUG_VIRTUAL */

#define __pa_symbol(x)	__phys_addr_symbol(RELOC_HIDE((unsigned long)(x), 0))
#define __pa(x)		__virt_to_phys((unsigned long)(x))
#define __va(x)		((void *)__pa_to_va_nodebug((phys_addr_t)(x)))

#define phys_to_pfn(phys)	(PFN_DOWN(phys))
#define pfn_to_phys(pfn)	(PFN_PHYS(pfn))

#define virt_to_pfn(vaddr)	(phys_to_pfn(__pa(vaddr)))
#define pfn_to_virt(pfn)	(__va(pfn_to_phys(pfn)))

#define virt_to_page(vaddr)	(pfn_to_page(virt_to_pfn(vaddr)))
#define page_to_virt(page)	(pfn_to_virt(page_to_pfn(page)))

#define page_to_phys(page)	(pfn_to_phys(page_to_pfn(page)))
#define phys_to_page(paddr)	(pfn_to_page(phys_to_pfn(paddr)))

#define sym_to_pfn(x)           __phys_to_pfn(__pa_symbol(x))

unsigned long kaslr_offset(void);

#endif /* __ASSEMBLY__ */

#define virt_addr_valid(vaddr)	({						\
	unsigned long _addr = (unsigned long)vaddr;				\
	(unsigned long)(_addr) >= PAGE_OFFSET && pfn_valid(virt_to_pfn(_addr));	\
})

#define VM_DATA_DEFAULT_FLAGS	VM_DATA_FLAGS_NON_EXEC

#include <asm-generic/memory_model.h>
#include <asm-generic/getorder.h>

#endif /* _ASM_RISCV_PAGE_H */
