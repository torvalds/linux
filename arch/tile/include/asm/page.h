/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#ifndef _ASM_TILE_PAGE_H
#define _ASM_TILE_PAGE_H

#include <linux/const.h>
#include <hv/hypervisor.h>
#include <arch/chip.h>

/* PAGE_SHIFT and HPAGE_SHIFT determine the page sizes. */
#define PAGE_SHIFT	HV_LOG2_PAGE_SIZE_SMALL
#define HPAGE_SHIFT	HV_LOG2_PAGE_SIZE_LARGE

#define PAGE_SIZE	(_AC(1, UL) << PAGE_SHIFT)
#define HPAGE_SIZE	(_AC(1, UL) << HPAGE_SHIFT)

#define PAGE_MASK	(~(PAGE_SIZE - 1))
#define HPAGE_MASK	(~(HPAGE_SIZE - 1))

/*
 * If the Kconfig doesn't specify, set a maximum zone order that
 * is enough so that we can create huge pages from small pages given
 * the respective sizes of the two page types.  See <linux/mmzone.h>.
 */
#ifndef CONFIG_FORCE_MAX_ZONEORDER
#define CONFIG_FORCE_MAX_ZONEORDER (HPAGE_SHIFT - PAGE_SHIFT + 1)
#endif

#ifndef __ASSEMBLY__

#include <linux/types.h>
#include <linux/string.h>

struct page;

static inline void clear_page(void *page)
{
	memset(page, 0, PAGE_SIZE);
}

static inline void copy_page(void *to, void *from)
{
	memcpy(to, from, PAGE_SIZE);
}

static inline void clear_user_page(void *page, unsigned long vaddr,
				struct page *pg)
{
	clear_page(page);
}

static inline void copy_user_page(void *to, void *from, unsigned long vaddr,
				struct page *topage)
{
	copy_page(to, from);
}

/*
 * Hypervisor page tables are made of the same basic structure.
 */

typedef HV_PTE pte_t;
typedef HV_PTE pgd_t;
typedef HV_PTE pgprot_t;

/*
 * User L2 page tables are managed as one L2 page table per page,
 * because we use the page allocator for them.  This keeps the allocation
 * simple and makes it potentially useful to implement HIGHPTE at some point.
 * However, it's also inefficient, since L2 page tables are much smaller
 * than pages (currently 2KB vs 64KB).  So we should revisit this.
 */
typedef struct page *pgtable_t;

/* Must be a macro since it is used to create constants. */
#define __pgprot(val) hv_pte(val)

/* Rarely-used initializers, typically with a "zero" value. */
#define __pte(x) hv_pte(x)
#define __pgd(x) hv_pte(x)

static inline u64 pgprot_val(pgprot_t pgprot)
{
	return hv_pte_val(pgprot);
}

static inline u64 pte_val(pte_t pte)
{
	return hv_pte_val(pte);
}

static inline u64 pgd_val(pgd_t pgd)
{
	return hv_pte_val(pgd);
}

#ifdef __tilegx__

typedef HV_PTE pmd_t;

#define __pmd(x) hv_pte(x)

static inline u64 pmd_val(pmd_t pmd)
{
	return hv_pte_val(pmd);
}

#endif

static inline __attribute_const__ int get_order(unsigned long size)
{
	return BITS_PER_LONG - __builtin_clzl((size - 1) >> PAGE_SHIFT);
}

#endif /* !__ASSEMBLY__ */

#define HUGETLB_PAGE_ORDER	(HPAGE_SHIFT - PAGE_SHIFT)

#define HUGE_MAX_HSTATE		2

#ifdef CONFIG_HUGETLB_PAGE
#define HAVE_ARCH_HUGETLB_UNMAPPED_AREA
#endif

/* Each memory controller has PAs distinct in their high bits. */
#define NR_PA_HIGHBIT_SHIFT (CHIP_PA_WIDTH() - CHIP_LOG_NUM_MSHIMS())
#define NR_PA_HIGHBIT_VALUES (1 << CHIP_LOG_NUM_MSHIMS())
#define __pa_to_highbits(pa) ((phys_addr_t)(pa) >> NR_PA_HIGHBIT_SHIFT)
#define __pfn_to_highbits(pfn) ((pfn) >> (NR_PA_HIGHBIT_SHIFT - PAGE_SHIFT))

#ifdef __tilegx__

/*
 * We reserve the lower half of memory for user-space programs, and the
 * upper half for system code.  We re-map all of physical memory in the
 * upper half, which takes a quarter of our VA space.  Then we have
 * the vmalloc regions.  The supervisor code lives at 0xfffffff700000000,
 * with the hypervisor above that.
 *
 * Loadable kernel modules are placed immediately after the static
 * supervisor code, with each being allocated a 256MB region of
 * address space, so we don't have to worry about the range of "jal"
 * and other branch instructions.
 *
 * For now we keep life simple and just allocate one pmd (4GB) for vmalloc.
 * Similarly, for now we don't play any struct page mapping games.
 */

#if CHIP_PA_WIDTH() + 2 > CHIP_VA_WIDTH()
# error Too much PA to map with the VA available!
#endif
#define HALF_VA_SPACE           (_AC(1, UL) << (CHIP_VA_WIDTH() - 1))

#define MEM_LOW_END		(HALF_VA_SPACE - 1)         /* low half */
#define MEM_HIGH_START		(-HALF_VA_SPACE)            /* high half */
#define PAGE_OFFSET		MEM_HIGH_START
#define _VMALLOC_START		_AC(0xfffffff500000000, UL) /* 4 GB */
#define HUGE_VMAP_BASE		_AC(0xfffffff600000000, UL) /* 4 GB */
#define MEM_SV_START		_AC(0xfffffff700000000, UL) /* 256 MB */
#define MEM_SV_INTRPT		MEM_SV_START
#define MEM_MODULE_START	_AC(0xfffffff710000000, UL) /* 256 MB */
#define MEM_MODULE_END		(MEM_MODULE_START + (256*1024*1024))
#define MEM_HV_START		_AC(0xfffffff800000000, UL) /* 32 GB */

/* Highest DTLB address we will use */
#define KERNEL_HIGH_VADDR	MEM_SV_START

/* Since we don't currently provide any fixmaps, we use an impossible VA. */
#define FIXADDR_TOP             MEM_HV_START

#else /* !__tilegx__ */

/*
 * A PAGE_OFFSET of 0xC0000000 means that the kernel has
 * a virtual address space of one gigabyte, which limits the
 * amount of physical memory you can use to about 768MB.
 * If you want more physical memory than this then see the CONFIG_HIGHMEM
 * option in the kernel configuration.
 *
 * The top 16MB chunk in the table below is unavailable to Linux.  Since
 * the kernel interrupt vectors must live at ether 0xfe000000 or 0xfd000000
 * (depending on whether the kernel is at PL2 or Pl1), we map all of the
 * bottom of RAM at this address with a huge page table entry to minimize
 * its ITLB footprint (as well as at PAGE_OFFSET).  The last architected
 * requirement is that user interrupt vectors live at 0xfc000000, so we
 * make that range of memory available to user processes.  The remaining
 * regions are sized as shown; the first four addresses use the PL 1
 * values, and after that, we show "typical" values, since the actual
 * addresses depend on kernel #defines.
 *
 * MEM_HV_INTRPT                   0xfe000000
 * MEM_SV_INTRPT (kernel code)     0xfd000000
 * MEM_USER_INTRPT (user vector)   0xfc000000
 * FIX_KMAP_xxx                    0xf8000000 (via NR_CPUS * KM_TYPE_NR)
 * PKMAP_BASE                      0xf7000000 (via LAST_PKMAP)
 * HUGE_VMAP                       0xf3000000 (via CONFIG_NR_HUGE_VMAPS)
 * VMALLOC_START                   0xf0000000 (via __VMALLOC_RESERVE)
 * mapped LOWMEM                   0xc0000000
 */

#define MEM_USER_INTRPT		_AC(0xfc000000, UL)
#if CONFIG_KERNEL_PL == 1
#define MEM_SV_INTRPT		_AC(0xfd000000, UL)
#define MEM_HV_INTRPT		_AC(0xfe000000, UL)
#else
#define MEM_GUEST_INTRPT	_AC(0xfd000000, UL)
#define MEM_SV_INTRPT		_AC(0xfe000000, UL)
#define MEM_HV_INTRPT		_AC(0xff000000, UL)
#endif

#define INTRPT_SIZE		0x4000

/* Tolerate page size larger than the architecture interrupt region size. */
#if PAGE_SIZE > INTRPT_SIZE
#undef INTRPT_SIZE
#define INTRPT_SIZE PAGE_SIZE
#endif

#define KERNEL_HIGH_VADDR	MEM_USER_INTRPT
#define FIXADDR_TOP		(KERNEL_HIGH_VADDR - PAGE_SIZE)

#define PAGE_OFFSET		_AC(CONFIG_PAGE_OFFSET, UL)

/* On 32-bit architectures we mix kernel modules in with other vmaps. */
#define MEM_MODULE_START	VMALLOC_START
#define MEM_MODULE_END		VMALLOC_END

#endif /* __tilegx__ */

#ifndef __ASSEMBLY__

#ifdef CONFIG_HIGHMEM

/* Map kernel virtual addresses to page frames, in HPAGE_SIZE chunks. */
extern unsigned long pbase_map[];
extern void *vbase_map[];

static inline unsigned long kaddr_to_pfn(const volatile void *_kaddr)
{
	unsigned long kaddr = (unsigned long)_kaddr;
	return pbase_map[kaddr >> HPAGE_SHIFT] +
		((kaddr & (HPAGE_SIZE - 1)) >> PAGE_SHIFT);
}

static inline void *pfn_to_kaddr(unsigned long pfn)
{
	return vbase_map[__pfn_to_highbits(pfn)] + (pfn << PAGE_SHIFT);
}

static inline phys_addr_t virt_to_phys(const volatile void *kaddr)
{
	unsigned long pfn = kaddr_to_pfn(kaddr);
	return ((phys_addr_t)pfn << PAGE_SHIFT) +
		((unsigned long)kaddr & (PAGE_SIZE-1));
}

static inline void *phys_to_virt(phys_addr_t paddr)
{
	return pfn_to_kaddr(paddr >> PAGE_SHIFT) + (paddr & (PAGE_SIZE-1));
}

/* With HIGHMEM, we pack PAGE_OFFSET through high_memory with all valid VAs. */
static inline int virt_addr_valid(const volatile void *kaddr)
{
	extern void *high_memory;  /* copied from <linux/mm.h> */
	return ((unsigned long)kaddr >= PAGE_OFFSET && kaddr < high_memory);
}

#else /* !CONFIG_HIGHMEM */

static inline unsigned long kaddr_to_pfn(const volatile void *kaddr)
{
	return ((unsigned long)kaddr - PAGE_OFFSET) >> PAGE_SHIFT;
}

static inline void *pfn_to_kaddr(unsigned long pfn)
{
	return (void *)((pfn << PAGE_SHIFT) + PAGE_OFFSET);
}

static inline phys_addr_t virt_to_phys(const volatile void *kaddr)
{
	return (phys_addr_t)((unsigned long)kaddr - PAGE_OFFSET);
}

static inline void *phys_to_virt(phys_addr_t paddr)
{
	return (void *)((unsigned long)paddr + PAGE_OFFSET);
}

/* Check that the given address is within some mapped range of PAs. */
#define virt_addr_valid(kaddr) pfn_valid(kaddr_to_pfn(kaddr))

#endif /* !CONFIG_HIGHMEM */

/* All callers are not consistent in how they call these functions. */
#define __pa(kaddr) virt_to_phys((void *)(unsigned long)(kaddr))
#define __va(paddr) phys_to_virt((phys_addr_t)(paddr))

extern int devmem_is_allowed(unsigned long pagenr);

#ifdef CONFIG_FLATMEM
static inline int pfn_valid(unsigned long pfn)
{
	return pfn < max_mapnr;
}
#endif

/* Provide as macros since these require some other headers included. */
#define page_to_pa(page) ((phys_addr_t)(page_to_pfn(page)) << PAGE_SHIFT)
#define virt_to_page(kaddr) pfn_to_page(kaddr_to_pfn((void *)(kaddr)))
#define page_to_virt(page) pfn_to_kaddr(page_to_pfn(page))

struct mm_struct;
extern pte_t *virt_to_pte(struct mm_struct *mm, unsigned long addr);

#endif /* !__ASSEMBLY__ */

#define VM_DATA_DEFAULT_FLAGS \
	(VM_READ | VM_WRITE | VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC)

#include <asm-generic/memory_model.h>

#endif /* _ASM_TILE_PAGE_H */
