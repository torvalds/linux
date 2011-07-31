#ifndef __ASM_SH_PAGE_H
#define __ASM_SH_PAGE_H

/*
 * Copyright (C) 1999  Niibe Yutaka
 */

#include <linux/const.h>

/* PAGE_SHIFT determines the page size */
#if defined(CONFIG_PAGE_SIZE_4KB)
# define PAGE_SHIFT	12
#elif defined(CONFIG_PAGE_SIZE_8KB)
# define PAGE_SHIFT	13
#elif defined(CONFIG_PAGE_SIZE_16KB)
# define PAGE_SHIFT	14
#elif defined(CONFIG_PAGE_SIZE_64KB)
# define PAGE_SHIFT	16
#else
# error "Bogus kernel page size?"
#endif

#define PAGE_SIZE	(_AC(1, UL) << PAGE_SHIFT)
#define PAGE_MASK	(~(PAGE_SIZE-1))
#define PTE_MASK	PAGE_MASK

#if defined(CONFIG_HUGETLB_PAGE_SIZE_64K)
#define HPAGE_SHIFT	16
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_256K)
#define HPAGE_SHIFT	18
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_1MB)
#define HPAGE_SHIFT	20
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_4MB)
#define HPAGE_SHIFT	22
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_64MB)
#define HPAGE_SHIFT	26
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_512MB)
#define HPAGE_SHIFT	29
#endif

#ifdef CONFIG_HUGETLB_PAGE
#define HPAGE_SIZE		(1UL << HPAGE_SHIFT)
#define HPAGE_MASK		(~(HPAGE_SIZE-1))
#define HUGETLB_PAGE_ORDER	(HPAGE_SHIFT-PAGE_SHIFT)
#endif

#ifndef __ASSEMBLY__
#include <asm/uncached.h>

extern unsigned long shm_align_mask;
extern unsigned long max_low_pfn, min_low_pfn;
extern unsigned long memory_start, memory_end, memory_limit;

static inline unsigned long
pages_do_alias(unsigned long addr1, unsigned long addr2)
{
	return (addr1 ^ addr2) & shm_align_mask;
}

#define clear_page(page)	memset((void *)(page), 0, PAGE_SIZE)
extern void copy_page(void *to, void *from);

struct page;
struct vm_area_struct;

extern void copy_user_highpage(struct page *to, struct page *from,
			       unsigned long vaddr, struct vm_area_struct *vma);
#define __HAVE_ARCH_COPY_USER_HIGHPAGE
extern void clear_user_highpage(struct page *page, unsigned long vaddr);
#define clear_user_highpage	clear_user_highpage

/*
 * These are used to make use of C type-checking..
 */
#ifdef CONFIG_X2TLB
typedef struct { unsigned long pte_low, pte_high; } pte_t;
typedef struct { unsigned long long pgprot; } pgprot_t;
typedef struct { unsigned long long pgd; } pgd_t;
#define pte_val(x) \
	((x).pte_low | ((unsigned long long)(x).pte_high << 32))
#define __pte(x) \
	({ pte_t __pte = {(x), ((unsigned long long)(x)) >> 32}; __pte; })
#elif defined(CONFIG_SUPERH32)
typedef struct { unsigned long pte_low; } pte_t;
typedef struct { unsigned long pgprot; } pgprot_t;
typedef struct { unsigned long pgd; } pgd_t;
#define pte_val(x)	((x).pte_low)
#define __pte(x)	((pte_t) { (x) } )
#else
typedef struct { unsigned long long pte_low; } pte_t;
typedef struct { unsigned long long pgprot; } pgprot_t;
typedef struct { unsigned long pgd; } pgd_t;
#define pte_val(x)	((x).pte_low)
#define __pte(x)	((pte_t) { (x) } )
#endif

#define pgd_val(x)	((x).pgd)
#define pgprot_val(x)	((x).pgprot)

#define __pgd(x) ((pgd_t) { (x) } )
#define __pgprot(x)	((pgprot_t) { (x) } )

typedef struct page *pgtable_t;

#define pte_pgprot(x) __pgprot(pte_val(x) & PTE_FLAGS_MASK)

#endif /* !__ASSEMBLY__ */

/*
 * __MEMORY_START and SIZE are the physical addresses and size of RAM.
 */
#define __MEMORY_START		CONFIG_MEMORY_START
#define __MEMORY_SIZE		CONFIG_MEMORY_SIZE

/*
 * PAGE_OFFSET is the virtual address of the start of kernel address
 * space.
 */
#define PAGE_OFFSET		CONFIG_PAGE_OFFSET

/*
 * Virtual to physical RAM address translation.
 *
 * In 29 bit mode, the physical offset of RAM from address 0 is visible in
 * the kernel virtual address space, and thus we don't have to take
 * this into account when translating. However in 32 bit mode this offset
 * is not visible (it is part of the PMB mapping) and so needs to be
 * added or subtracted as required.
 */
#ifdef CONFIG_PMB
#define ___pa(x)	((x)-PAGE_OFFSET+__MEMORY_START)
#define ___va(x)	((x)+PAGE_OFFSET-__MEMORY_START)
#else
#define ___pa(x)	((x)-PAGE_OFFSET)
#define ___va(x)	((x)+PAGE_OFFSET)
#endif

#ifndef __ASSEMBLY__
#define __pa(x)		___pa((unsigned long)x)
#define __va(x)		(void *)___va((unsigned long)x)
#endif /* !__ASSEMBLY__ */

#ifdef CONFIG_UNCACHED_MAPPING
#define UNCAC_ADDR(addr)	((addr) - PAGE_OFFSET + uncached_start)
#define CAC_ADDR(addr)		((addr) - uncached_start + PAGE_OFFSET)
#else
#define UNCAC_ADDR(addr)	((addr))
#define CAC_ADDR(addr)		((addr))
#endif

#define pfn_to_kaddr(pfn)	__va((pfn) << PAGE_SHIFT)
#define page_to_phys(page)	(page_to_pfn(page) << PAGE_SHIFT)

/*
 * PFN = physical frame number (ie PFN 0 == physical address 0)
 * PFN_START is the PFN of the first page of RAM. By defining this we
 * don't have struct page entries for the portion of address space
 * between physical address 0 and the start of RAM.
 */
#define PFN_START		(__MEMORY_START >> PAGE_SHIFT)
#define ARCH_PFN_OFFSET		(PFN_START)
#define virt_to_page(kaddr)	pfn_to_page(__pa(kaddr) >> PAGE_SHIFT)
#ifdef CONFIG_FLATMEM
#define pfn_valid(pfn)		((pfn) >= min_low_pfn && (pfn) < max_low_pfn)
#endif
#define virt_addr_valid(kaddr)	pfn_valid(__pa(kaddr) >> PAGE_SHIFT)

#define VM_DATA_DEFAULT_FLAGS	(VM_READ | VM_WRITE | VM_EXEC | \
				 VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC)

#include <asm-generic/memory_model.h>
#include <asm-generic/getorder.h>

/* vDSO support */
#ifdef CONFIG_VSYSCALL
#define __HAVE_ARCH_GATE_AREA
#endif

/*
 * Some drivers need to perform DMA into kmalloc'ed buffers
 * and so we have to increase the kmalloc minalign for this.
 */
#define ARCH_DMA_MINALIGN	L1_CACHE_BYTES

#ifdef CONFIG_SUPERH64
/*
 * While BYTES_PER_WORD == 4 on the current sh64 ABI, GCC will still
 * happily generate {ld/st}.q pairs, requiring us to have 8-byte
 * alignment to avoid traps. The kmalloc alignment is gauranteed by
 * virtue of L1_CACHE_BYTES, requiring this to only be special cased
 * for slab caches.
 */
#define ARCH_SLAB_MINALIGN	8
#endif

#endif /* __ASM_SH_PAGE_H */
