/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_BOOK3S_64_PGTABLE_H_
#define _ASM_POWERPC_BOOK3S_64_PGTABLE_H_

#include <asm-generic/pgtable-nop4d.h>

#ifndef __ASSEMBLY__
#include <linux/mmdebug.h>
#include <linux/bug.h>
#include <linux/sizes.h>
#endif

/*
 * Common bits between hash and Radix page table
 */
#define _PAGE_BIT_SWAP_TYPE	0

#define _PAGE_EXEC		0x00001 /* execute permission */
#define _PAGE_WRITE		0x00002 /* write access allowed */
#define _PAGE_READ		0x00004	/* read access allowed */
#define _PAGE_RW		(_PAGE_READ | _PAGE_WRITE)
#define _PAGE_RWX		(_PAGE_READ | _PAGE_WRITE | _PAGE_EXEC)
#define _PAGE_PRIVILEGED	0x00008 /* kernel access only */
#define _PAGE_SAO		0x00010 /* Strong access order */
#define _PAGE_NON_IDEMPOTENT	0x00020 /* non idempotent memory */
#define _PAGE_TOLERANT		0x00030 /* tolerant memory, cache inhibited */
#define _PAGE_DIRTY		0x00080 /* C: page changed */
#define _PAGE_ACCESSED		0x00100 /* R: page referenced */
/*
 * Software bits
 */
#define _RPAGE_SW0		0x2000000000000000UL
#define _RPAGE_SW1		0x00800
#define _RPAGE_SW2		0x00400
#define _RPAGE_SW3		0x00200
#define _RPAGE_RSV1		0x00040UL

#define _RPAGE_PKEY_BIT4	0x1000000000000000UL
#define _RPAGE_PKEY_BIT3	0x0800000000000000UL
#define _RPAGE_PKEY_BIT2	0x0400000000000000UL
#define _RPAGE_PKEY_BIT1	0x0200000000000000UL
#define _RPAGE_PKEY_BIT0	0x0100000000000000UL

#define _PAGE_PTE		0x4000000000000000UL	/* distinguishes PTEs from pointers */
#define _PAGE_PRESENT		0x8000000000000000UL	/* pte contains a translation */
/*
 * We need to mark a pmd pte invalid while splitting. We can do that by clearing
 * the _PAGE_PRESENT bit. But then that will be taken as a swap pte. In order to
 * differentiate between two use a SW field when invalidating.
 *
 * We do that temporary invalidate for regular pte entry in ptep_set_access_flags
 *
 * This is used only when _PAGE_PRESENT is cleared.
 */
#define _PAGE_INVALID		_RPAGE_SW0

/*
 * Top and bottom bits of RPN which can be used by hash
 * translation mode, because we expect them to be zero
 * otherwise.
 */
#define _RPAGE_RPN0		0x01000
#define _RPAGE_RPN1		0x02000
#define _RPAGE_RPN43		0x0080000000000000UL
#define _RPAGE_RPN42		0x0040000000000000UL
#define _RPAGE_RPN41		0x0020000000000000UL

/* Max physical address bit as per radix table */
#define _RPAGE_PA_MAX		56

/*
 * Max physical address bit we will use for now.
 *
 * This is mostly a hardware limitation and for now Power9 has
 * a 51 bit limit.
 *
 * This is different from the number of physical bit required to address
 * the last byte of memory. That is defined by MAX_PHYSMEM_BITS.
 * MAX_PHYSMEM_BITS is a linux limitation imposed by the maximum
 * number of sections we can support (SECTIONS_SHIFT).
 *
 * This is different from Radix page table limitation above and
 * should always be less than that. The limit is done such that
 * we can overload the bits between _RPAGE_PA_MAX and _PAGE_PA_MAX
 * for hash linux page table specific bits.
 *
 * In order to be compatible with future hardware generations we keep
 * some offsets and limit this for now to 53
 */
#define _PAGE_PA_MAX		53

#define _PAGE_SOFT_DIRTY	_RPAGE_SW3 /* software: software dirty tracking */
#define _PAGE_SPECIAL		_RPAGE_SW2 /* software: special page */
#define _PAGE_DEVMAP		_RPAGE_SW1 /* software: ZONE_DEVICE page */

/*
 * Drivers request for cache inhibited pte mapping using _PAGE_NO_CACHE
 * Instead of fixing all of them, add an alternate define which
 * maps CI pte mapping.
 */
#define _PAGE_NO_CACHE		_PAGE_TOLERANT
/*
 * We support _RPAGE_PA_MAX bit real address in pte. On the linux side
 * we are limited by _PAGE_PA_MAX. Clear everything above _PAGE_PA_MAX
 * and every thing below PAGE_SHIFT;
 */
#define PTE_RPN_MASK	(((1UL << _PAGE_PA_MAX) - 1) & (PAGE_MASK))
/*
 * set of bits not changed in pmd_modify. Even though we have hash specific bits
 * in here, on radix we expect them to be zero.
 */
#define _HPAGE_CHG_MASK (PTE_RPN_MASK | _PAGE_HPTEFLAGS | _PAGE_DIRTY | \
			 _PAGE_ACCESSED | H_PAGE_THP_HUGE | _PAGE_PTE | \
			 _PAGE_SOFT_DIRTY | _PAGE_DEVMAP)
/*
 * user access blocked by key
 */
#define _PAGE_KERNEL_RW		(_PAGE_PRIVILEGED | _PAGE_RW | _PAGE_DIRTY)
#define _PAGE_KERNEL_RO		 (_PAGE_PRIVILEGED | _PAGE_READ)
#define _PAGE_KERNEL_RWX	(_PAGE_PRIVILEGED | _PAGE_DIRTY |	\
				 _PAGE_RW | _PAGE_EXEC)
/*
 * _PAGE_CHG_MASK masks of bits that are to be preserved across
 * pgprot changes
 */
#define _PAGE_CHG_MASK	(PTE_RPN_MASK | _PAGE_HPTEFLAGS | _PAGE_DIRTY | \
			 _PAGE_ACCESSED | _PAGE_SPECIAL | _PAGE_PTE |	\
			 _PAGE_SOFT_DIRTY | _PAGE_DEVMAP)

/*
 * We define 2 sets of base prot bits, one for basic pages (ie,
 * cacheable kernel and user pages) and one for non cacheable
 * pages. We always set _PAGE_COHERENT when SMP is enabled or
 * the processor might need it for DMA coherency.
 */
#define _PAGE_BASE_NC	(_PAGE_PRESENT | _PAGE_ACCESSED)
#define _PAGE_BASE	(_PAGE_BASE_NC)

/* Permission masks used to generate the __P and __S table,
 *
 * Note:__pgprot is defined in arch/powerpc/include/asm/page.h
 *
 * Write permissions imply read permissions for now (we could make write-only
 * pages on BookE but we don't bother for now). Execute permission control is
 * possible on platforms that define _PAGE_EXEC
 */
#define PAGE_NONE	__pgprot(_PAGE_BASE | _PAGE_PRIVILEGED)
#define PAGE_SHARED	__pgprot(_PAGE_BASE | _PAGE_RW)
#define PAGE_SHARED_X	__pgprot(_PAGE_BASE | _PAGE_RW | _PAGE_EXEC)
#define PAGE_COPY	__pgprot(_PAGE_BASE | _PAGE_READ)
#define PAGE_COPY_X	__pgprot(_PAGE_BASE | _PAGE_READ | _PAGE_EXEC)
#define PAGE_READONLY	__pgprot(_PAGE_BASE | _PAGE_READ)
#define PAGE_READONLY_X	__pgprot(_PAGE_BASE | _PAGE_READ | _PAGE_EXEC)

/* Permission masks used for kernel mappings */
#define PAGE_KERNEL	__pgprot(_PAGE_BASE | _PAGE_KERNEL_RW)
#define PAGE_KERNEL_NC	__pgprot(_PAGE_BASE_NC | _PAGE_KERNEL_RW | \
				 _PAGE_TOLERANT)
#define PAGE_KERNEL_NCG	__pgprot(_PAGE_BASE_NC | _PAGE_KERNEL_RW | \
				 _PAGE_NON_IDEMPOTENT)
#define PAGE_KERNEL_X	__pgprot(_PAGE_BASE | _PAGE_KERNEL_RWX)
#define PAGE_KERNEL_RO	__pgprot(_PAGE_BASE | _PAGE_KERNEL_RO)
#define PAGE_KERNEL_ROX	__pgprot(_PAGE_BASE | _PAGE_KERNEL_ROX)

/*
 * Protection used for kernel text. We want the debuggers to be able to
 * set breakpoints anywhere, so don't write protect the kernel text
 * on platforms where such control is possible.
 */
#if defined(CONFIG_KGDB) || defined(CONFIG_XMON) || defined(CONFIG_BDI_SWITCH) || \
	defined(CONFIG_KPROBES) || defined(CONFIG_DYNAMIC_FTRACE)
#define PAGE_KERNEL_TEXT	PAGE_KERNEL_X
#else
#define PAGE_KERNEL_TEXT	PAGE_KERNEL_ROX
#endif

/* Make modules code happy. We don't set RO yet */
#define PAGE_KERNEL_EXEC	PAGE_KERNEL_X
#define PAGE_AGP		(PAGE_KERNEL_NC)

#ifndef __ASSEMBLY__
/*
 * page table defines
 */
extern unsigned long __pte_index_size;
extern unsigned long __pmd_index_size;
extern unsigned long __pud_index_size;
extern unsigned long __pgd_index_size;
extern unsigned long __pud_cache_index;
#define PTE_INDEX_SIZE  __pte_index_size
#define PMD_INDEX_SIZE  __pmd_index_size
#define PUD_INDEX_SIZE  __pud_index_size
#define PGD_INDEX_SIZE  __pgd_index_size
/* pmd table use page table fragments */
#define PMD_CACHE_INDEX  0
#define PUD_CACHE_INDEX __pud_cache_index
/*
 * Because of use of pte fragments and THP, size of page table
 * are not always derived out of index size above.
 */
extern unsigned long __pte_table_size;
extern unsigned long __pmd_table_size;
extern unsigned long __pud_table_size;
extern unsigned long __pgd_table_size;
#define PTE_TABLE_SIZE	__pte_table_size
#define PMD_TABLE_SIZE	__pmd_table_size
#define PUD_TABLE_SIZE	__pud_table_size
#define PGD_TABLE_SIZE	__pgd_table_size

extern unsigned long __pmd_val_bits;
extern unsigned long __pud_val_bits;
extern unsigned long __pgd_val_bits;
#define PMD_VAL_BITS	__pmd_val_bits
#define PUD_VAL_BITS	__pud_val_bits
#define PGD_VAL_BITS	__pgd_val_bits

extern unsigned long __pte_frag_nr;
#define PTE_FRAG_NR __pte_frag_nr
extern unsigned long __pte_frag_size_shift;
#define PTE_FRAG_SIZE_SHIFT __pte_frag_size_shift
#define PTE_FRAG_SIZE (1UL << PTE_FRAG_SIZE_SHIFT)

extern unsigned long __pmd_frag_nr;
#define PMD_FRAG_NR __pmd_frag_nr
extern unsigned long __pmd_frag_size_shift;
#define PMD_FRAG_SIZE_SHIFT __pmd_frag_size_shift
#define PMD_FRAG_SIZE (1UL << PMD_FRAG_SIZE_SHIFT)

#define PTRS_PER_PTE	(1 << PTE_INDEX_SIZE)
#define PTRS_PER_PMD	(1 << PMD_INDEX_SIZE)
#define PTRS_PER_PUD	(1 << PUD_INDEX_SIZE)
#define PTRS_PER_PGD	(1 << PGD_INDEX_SIZE)

/* PMD_SHIFT determines what a second-level page table entry can map */
#define PMD_SHIFT	(PAGE_SHIFT + PTE_INDEX_SIZE)
#define PMD_SIZE	(1UL << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE-1))

/* PUD_SHIFT determines what a third-level page table entry can map */
#define PUD_SHIFT	(PMD_SHIFT + PMD_INDEX_SIZE)
#define PUD_SIZE	(1UL << PUD_SHIFT)
#define PUD_MASK	(~(PUD_SIZE-1))

/* PGDIR_SHIFT determines what a fourth-level page table entry can map */
#define PGDIR_SHIFT	(PUD_SHIFT + PUD_INDEX_SIZE)
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

/* Bits to mask out from a PMD to get to the PTE page */
#define PMD_MASKED_BITS		0xc0000000000000ffUL
/* Bits to mask out from a PUD to get to the PMD page */
#define PUD_MASKED_BITS		0xc0000000000000ffUL
/* Bits to mask out from a PGD to get to the PUD page */
#define P4D_MASKED_BITS		0xc0000000000000ffUL

/*
 * Used as an indicator for rcu callback functions
 */
enum pgtable_index {
	PTE_INDEX = 0,
	PMD_INDEX,
	PUD_INDEX,
	PGD_INDEX,
	/*
	 * Below are used with 4k page size and hugetlb
	 */
	HTLB_16M_INDEX,
	HTLB_16G_INDEX,
};

extern unsigned long __vmalloc_start;
extern unsigned long __vmalloc_end;
#define VMALLOC_START	__vmalloc_start
#define VMALLOC_END	__vmalloc_end

static inline unsigned int ioremap_max_order(void)
{
	if (radix_enabled())
		return PUD_SHIFT;
	return 7 + PAGE_SHIFT; /* default from linux/vmalloc.h */
}
#define IOREMAP_MAX_ORDER ioremap_max_order()

extern unsigned long __kernel_virt_start;
extern unsigned long __kernel_io_start;
extern unsigned long __kernel_io_end;
#define KERN_VIRT_START __kernel_virt_start
#define KERN_IO_START  __kernel_io_start
#define KERN_IO_END __kernel_io_end

extern struct page *vmemmap;
extern unsigned long pci_io_base;
#endif /* __ASSEMBLY__ */

#include <asm/book3s/64/hash.h>
#include <asm/book3s/64/radix.h>

#if H_MAX_PHYSMEM_BITS > R_MAX_PHYSMEM_BITS
#define  MAX_PHYSMEM_BITS	H_MAX_PHYSMEM_BITS
#else
#define  MAX_PHYSMEM_BITS	R_MAX_PHYSMEM_BITS
#endif


#ifdef CONFIG_PPC_64K_PAGES
#include <asm/book3s/64/pgtable-64k.h>
#else
#include <asm/book3s/64/pgtable-4k.h>
#endif

#include <asm/barrier.h>
/*
 * IO space itself carved into the PIO region (ISA and PHB IO space) and
 * the ioremap space
 *
 *  ISA_IO_BASE = KERN_IO_START, 64K reserved area
 *  PHB_IO_BASE = ISA_IO_BASE + 64K to ISA_IO_BASE + 2G, PHB IO spaces
 * IOREMAP_BASE = ISA_IO_BASE + 2G to VMALLOC_START + PGTABLE_RANGE
 */
#define FULL_IO_SIZE	0x80000000ul
#define  ISA_IO_BASE	(KERN_IO_START)
#define  ISA_IO_END	(KERN_IO_START + 0x10000ul)
#define  PHB_IO_BASE	(ISA_IO_END)
#define  PHB_IO_END	(KERN_IO_START + FULL_IO_SIZE)
#define IOREMAP_BASE	(PHB_IO_END)
#define IOREMAP_START	(ioremap_bot)
#define IOREMAP_END	(KERN_IO_END - FIXADDR_SIZE)
#define FIXADDR_SIZE	SZ_32M

/* Advertise special mapping type for AGP */
#define HAVE_PAGE_AGP

#ifndef __ASSEMBLY__

/*
 * This is the default implementation of various PTE accessors, it's
 * used in all cases except Book3S with 64K pages where we have a
 * concept of sub-pages
 */
#ifndef __real_pte

#define __real_pte(e, p, o)		((real_pte_t){(e)})
#define __rpte_to_pte(r)	((r).pte)
#define __rpte_to_hidx(r,index)	(pte_val(__rpte_to_pte(r)) >> H_PAGE_F_GIX_SHIFT)

#define pte_iterate_hashed_subpages(rpte, psize, va, index, shift)       \
	do {							         \
		index = 0;					         \
		shift = mmu_psize_defs[psize].shift;		         \

#define pte_iterate_hashed_end() } while(0)

/*
 * We expect this to be called only for user addresses or kernel virtual
 * addresses other than the linear mapping.
 */
#define pte_pagesize_index(mm, addr, pte)	MMU_PAGE_4K

#endif /* __real_pte */

static inline unsigned long pte_update(struct mm_struct *mm, unsigned long addr,
				       pte_t *ptep, unsigned long clr,
				       unsigned long set, int huge)
{
	if (radix_enabled())
		return radix__pte_update(mm, addr, ptep, clr, set, huge);
	return hash__pte_update(mm, addr, ptep, clr, set, huge);
}
/*
 * For hash even if we have _PAGE_ACCESSED = 0, we do a pte_update.
 * We currently remove entries from the hashtable regardless of whether
 * the entry was young or dirty.
 *
 * We should be more intelligent about this but for the moment we override
 * these functions and force a tlb flush unconditionally
 * For radix: H_PAGE_HASHPTE should be zero. Hence we can use the same
 * function for both hash and radix.
 */
static inline int __ptep_test_and_clear_young(struct mm_struct *mm,
					      unsigned long addr, pte_t *ptep)
{
	unsigned long old;

	if ((pte_raw(*ptep) & cpu_to_be64(_PAGE_ACCESSED | H_PAGE_HASHPTE)) == 0)
		return 0;
	old = pte_update(mm, addr, ptep, _PAGE_ACCESSED, 0, 0);
	return (old & _PAGE_ACCESSED) != 0;
}

#define __HAVE_ARCH_PTEP_TEST_AND_CLEAR_YOUNG
#define ptep_test_and_clear_young(__vma, __addr, __ptep)	\
({								\
	int __r;						\
	__r = __ptep_test_and_clear_young((__vma)->vm_mm, __addr, __ptep); \
	__r;							\
})

static inline int __pte_write(pte_t pte)
{
	return !!(pte_raw(pte) & cpu_to_be64(_PAGE_WRITE));
}

#ifdef CONFIG_NUMA_BALANCING
#define pte_savedwrite pte_savedwrite
static inline bool pte_savedwrite(pte_t pte)
{
	/*
	 * Saved write ptes are prot none ptes that doesn't have
	 * privileged bit sit. We mark prot none as one which has
	 * present and pviliged bit set and RWX cleared. To mark
	 * protnone which used to have _PAGE_WRITE set we clear
	 * the privileged bit.
	 */
	return !(pte_raw(pte) & cpu_to_be64(_PAGE_RWX | _PAGE_PRIVILEGED));
}
#else
#define pte_savedwrite pte_savedwrite
static inline bool pte_savedwrite(pte_t pte)
{
	return false;
}
#endif

static inline int pte_write(pte_t pte)
{
	return __pte_write(pte) || pte_savedwrite(pte);
}

static inline int pte_read(pte_t pte)
{
	return !!(pte_raw(pte) & cpu_to_be64(_PAGE_READ));
}

#define __HAVE_ARCH_PTEP_SET_WRPROTECT
static inline void ptep_set_wrprotect(struct mm_struct *mm, unsigned long addr,
				      pte_t *ptep)
{
	if (__pte_write(*ptep))
		pte_update(mm, addr, ptep, _PAGE_WRITE, 0, 0);
	else if (unlikely(pte_savedwrite(*ptep)))
		pte_update(mm, addr, ptep, 0, _PAGE_PRIVILEGED, 0);
}

#define __HAVE_ARCH_HUGE_PTEP_SET_WRPROTECT
static inline void huge_ptep_set_wrprotect(struct mm_struct *mm,
					   unsigned long addr, pte_t *ptep)
{
	/*
	 * We should not find protnone for hugetlb, but this complete the
	 * interface.
	 */
	if (__pte_write(*ptep))
		pte_update(mm, addr, ptep, _PAGE_WRITE, 0, 1);
	else if (unlikely(pte_savedwrite(*ptep)))
		pte_update(mm, addr, ptep, 0, _PAGE_PRIVILEGED, 1);
}

#define __HAVE_ARCH_PTEP_GET_AND_CLEAR
static inline pte_t ptep_get_and_clear(struct mm_struct *mm,
				       unsigned long addr, pte_t *ptep)
{
	unsigned long old = pte_update(mm, addr, ptep, ~0UL, 0, 0);
	return __pte(old);
}

#define __HAVE_ARCH_PTEP_GET_AND_CLEAR_FULL
static inline pte_t ptep_get_and_clear_full(struct mm_struct *mm,
					    unsigned long addr,
					    pte_t *ptep, int full)
{
	if (full && radix_enabled()) {
		/*
		 * We know that this is a full mm pte clear and
		 * hence can be sure there is no parallel set_pte.
		 */
		return radix__ptep_get_and_clear_full(mm, addr, ptep, full);
	}
	return ptep_get_and_clear(mm, addr, ptep);
}


static inline void pte_clear(struct mm_struct *mm, unsigned long addr,
			     pte_t * ptep)
{
	pte_update(mm, addr, ptep, ~0UL, 0, 0);
}

static inline int pte_dirty(pte_t pte)
{
	return !!(pte_raw(pte) & cpu_to_be64(_PAGE_DIRTY));
}

static inline int pte_young(pte_t pte)
{
	return !!(pte_raw(pte) & cpu_to_be64(_PAGE_ACCESSED));
}

static inline int pte_special(pte_t pte)
{
	return !!(pte_raw(pte) & cpu_to_be64(_PAGE_SPECIAL));
}

static inline bool pte_exec(pte_t pte)
{
	return !!(pte_raw(pte) & cpu_to_be64(_PAGE_EXEC));
}


#ifdef CONFIG_HAVE_ARCH_SOFT_DIRTY
static inline bool pte_soft_dirty(pte_t pte)
{
	return !!(pte_raw(pte) & cpu_to_be64(_PAGE_SOFT_DIRTY));
}

static inline pte_t pte_mksoft_dirty(pte_t pte)
{
	return __pte_raw(pte_raw(pte) | cpu_to_be64(_PAGE_SOFT_DIRTY));
}

static inline pte_t pte_clear_soft_dirty(pte_t pte)
{
	return __pte_raw(pte_raw(pte) & cpu_to_be64(~_PAGE_SOFT_DIRTY));
}
#endif /* CONFIG_HAVE_ARCH_SOFT_DIRTY */

#ifdef CONFIG_NUMA_BALANCING
static inline int pte_protnone(pte_t pte)
{
	return (pte_raw(pte) & cpu_to_be64(_PAGE_PRESENT | _PAGE_PTE | _PAGE_RWX)) ==
		cpu_to_be64(_PAGE_PRESENT | _PAGE_PTE);
}

#define pte_mk_savedwrite pte_mk_savedwrite
static inline pte_t pte_mk_savedwrite(pte_t pte)
{
	/*
	 * Used by Autonuma subsystem to preserve the write bit
	 * while marking the pte PROT_NONE. Only allow this
	 * on PROT_NONE pte
	 */
	VM_BUG_ON((pte_raw(pte) & cpu_to_be64(_PAGE_PRESENT | _PAGE_RWX | _PAGE_PRIVILEGED)) !=
		  cpu_to_be64(_PAGE_PRESENT | _PAGE_PRIVILEGED));
	return __pte_raw(pte_raw(pte) & cpu_to_be64(~_PAGE_PRIVILEGED));
}

#define pte_clear_savedwrite pte_clear_savedwrite
static inline pte_t pte_clear_savedwrite(pte_t pte)
{
	/*
	 * Used by KSM subsystem to make a protnone pte readonly.
	 */
	VM_BUG_ON(!pte_protnone(pte));
	return __pte_raw(pte_raw(pte) | cpu_to_be64(_PAGE_PRIVILEGED));
}
#else
#define pte_clear_savedwrite pte_clear_savedwrite
static inline pte_t pte_clear_savedwrite(pte_t pte)
{
	VM_WARN_ON(1);
	return __pte_raw(pte_raw(pte) & cpu_to_be64(~_PAGE_WRITE));
}
#endif /* CONFIG_NUMA_BALANCING */

static inline bool pte_hw_valid(pte_t pte)
{
	return (pte_raw(pte) & cpu_to_be64(_PAGE_PRESENT | _PAGE_PTE)) ==
		cpu_to_be64(_PAGE_PRESENT | _PAGE_PTE);
}

static inline int pte_present(pte_t pte)
{
	/*
	 * A pte is considerent present if _PAGE_PRESENT is set.
	 * We also need to consider the pte present which is marked
	 * invalid during ptep_set_access_flags. Hence we look for _PAGE_INVALID
	 * if we find _PAGE_PRESENT cleared.
	 */

	if (pte_hw_valid(pte))
		return true;
	return (pte_raw(pte) & cpu_to_be64(_PAGE_INVALID | _PAGE_PTE)) ==
		cpu_to_be64(_PAGE_INVALID | _PAGE_PTE);
}

#ifdef CONFIG_PPC_MEM_KEYS
extern bool arch_pte_access_permitted(u64 pte, bool write, bool execute);
#else
static inline bool arch_pte_access_permitted(u64 pte, bool write, bool execute)
{
	return true;
}
#endif /* CONFIG_PPC_MEM_KEYS */

static inline bool pte_user(pte_t pte)
{
	return !(pte_raw(pte) & cpu_to_be64(_PAGE_PRIVILEGED));
}

#define pte_access_permitted pte_access_permitted
static inline bool pte_access_permitted(pte_t pte, bool write)
{
	/*
	 * _PAGE_READ is needed for any access and will be
	 * cleared for PROT_NONE
	 */
	if (!pte_present(pte) || !pte_user(pte) || !pte_read(pte))
		return false;

	if (write && !pte_write(pte))
		return false;

	return arch_pte_access_permitted(pte_val(pte), write, 0);
}

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 *
 * Even if PTEs can be unsigned long long, a PFN is always an unsigned
 * long for now.
 */
static inline pte_t pfn_pte(unsigned long pfn, pgprot_t pgprot)
{
	VM_BUG_ON(pfn >> (64 - PAGE_SHIFT));
	VM_BUG_ON((pfn << PAGE_SHIFT) & ~PTE_RPN_MASK);

	return __pte(((pte_basic_t)pfn << PAGE_SHIFT) | pgprot_val(pgprot) | _PAGE_PTE);
}

static inline unsigned long pte_pfn(pte_t pte)
{
	return (pte_val(pte) & PTE_RPN_MASK) >> PAGE_SHIFT;
}

/* Generic modifiers for PTE bits */
static inline pte_t pte_wrprotect(pte_t pte)
{
	if (unlikely(pte_savedwrite(pte)))
		return pte_clear_savedwrite(pte);
	return __pte_raw(pte_raw(pte) & cpu_to_be64(~_PAGE_WRITE));
}

static inline pte_t pte_exprotect(pte_t pte)
{
	return __pte_raw(pte_raw(pte) & cpu_to_be64(~_PAGE_EXEC));
}

static inline pte_t pte_mkclean(pte_t pte)
{
	return __pte_raw(pte_raw(pte) & cpu_to_be64(~_PAGE_DIRTY));
}

static inline pte_t pte_mkold(pte_t pte)
{
	return __pte_raw(pte_raw(pte) & cpu_to_be64(~_PAGE_ACCESSED));
}

static inline pte_t pte_mkexec(pte_t pte)
{
	return __pte_raw(pte_raw(pte) | cpu_to_be64(_PAGE_EXEC));
}

static inline pte_t pte_mkwrite(pte_t pte)
{
	/*
	 * write implies read, hence set both
	 */
	return __pte_raw(pte_raw(pte) | cpu_to_be64(_PAGE_RW));
}

static inline pte_t pte_mkdirty(pte_t pte)
{
	return __pte_raw(pte_raw(pte) | cpu_to_be64(_PAGE_DIRTY | _PAGE_SOFT_DIRTY));
}

static inline pte_t pte_mkyoung(pte_t pte)
{
	return __pte_raw(pte_raw(pte) | cpu_to_be64(_PAGE_ACCESSED));
}

static inline pte_t pte_mkspecial(pte_t pte)
{
	return __pte_raw(pte_raw(pte) | cpu_to_be64(_PAGE_SPECIAL));
}

static inline pte_t pte_mkhuge(pte_t pte)
{
	return pte;
}

static inline pte_t pte_mkdevmap(pte_t pte)
{
	return __pte_raw(pte_raw(pte) | cpu_to_be64(_PAGE_SPECIAL | _PAGE_DEVMAP));
}

static inline pte_t pte_mkprivileged(pte_t pte)
{
	return __pte_raw(pte_raw(pte) | cpu_to_be64(_PAGE_PRIVILEGED));
}

static inline pte_t pte_mkuser(pte_t pte)
{
	return __pte_raw(pte_raw(pte) & cpu_to_be64(~_PAGE_PRIVILEGED));
}

/*
 * This is potentially called with a pmd as the argument, in which case it's not
 * safe to check _PAGE_DEVMAP unless we also confirm that _PAGE_PTE is set.
 * That's because the bit we use for _PAGE_DEVMAP is not reserved for software
 * use in page directory entries (ie. non-ptes).
 */
static inline int pte_devmap(pte_t pte)
{
	u64 mask = cpu_to_be64(_PAGE_DEVMAP | _PAGE_PTE);

	return (pte_raw(pte) & mask) == mask;
}

static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	/* FIXME!! check whether this need to be a conditional */
	return __pte_raw((pte_raw(pte) & cpu_to_be64(_PAGE_CHG_MASK)) |
			 cpu_to_be64(pgprot_val(newprot)));
}

/* Encode and de-code a swap entry */
#define MAX_SWAPFILES_CHECK() do { \
	BUILD_BUG_ON(MAX_SWAPFILES_SHIFT > SWP_TYPE_BITS); \
	/*							\
	 * Don't have overlapping bits with _PAGE_HPTEFLAGS	\
	 * We filter HPTEFLAGS on set_pte.			\
	 */							\
	BUILD_BUG_ON(_PAGE_HPTEFLAGS & (0x1f << _PAGE_BIT_SWAP_TYPE)); \
	BUILD_BUG_ON(_PAGE_HPTEFLAGS & _PAGE_SWP_SOFT_DIRTY);	\
	} while (0)

#define SWP_TYPE_BITS 5
#define __swp_type(x)		(((x).val >> _PAGE_BIT_SWAP_TYPE) \
				& ((1UL << SWP_TYPE_BITS) - 1))
#define __swp_offset(x)		(((x).val & PTE_RPN_MASK) >> PAGE_SHIFT)
#define __swp_entry(type, offset)	((swp_entry_t) { \
				((type) << _PAGE_BIT_SWAP_TYPE) \
				| (((offset) << PAGE_SHIFT) & PTE_RPN_MASK)})
/*
 * swp_entry_t must be independent of pte bits. We build a swp_entry_t from
 * swap type and offset we get from swap and convert that to pte to find a
 * matching pte in linux page table.
 * Clear bits not found in swap entries here.
 */
#define __pte_to_swp_entry(pte)	((swp_entry_t) { pte_val((pte)) & ~_PAGE_PTE })
#define __swp_entry_to_pte(x)	__pte((x).val | _PAGE_PTE)
#define __pmd_to_swp_entry(pmd)	(__pte_to_swp_entry(pmd_pte(pmd)))
#define __swp_entry_to_pmd(x)	(pte_pmd(__swp_entry_to_pte(x)))

#ifdef CONFIG_MEM_SOFT_DIRTY
#define _PAGE_SWP_SOFT_DIRTY   (1UL << (SWP_TYPE_BITS + _PAGE_BIT_SWAP_TYPE))
#else
#define _PAGE_SWP_SOFT_DIRTY	0UL
#endif /* CONFIG_MEM_SOFT_DIRTY */

#ifdef CONFIG_HAVE_ARCH_SOFT_DIRTY
static inline pte_t pte_swp_mksoft_dirty(pte_t pte)
{
	return __pte_raw(pte_raw(pte) | cpu_to_be64(_PAGE_SWP_SOFT_DIRTY));
}

static inline bool pte_swp_soft_dirty(pte_t pte)
{
	return !!(pte_raw(pte) & cpu_to_be64(_PAGE_SWP_SOFT_DIRTY));
}

static inline pte_t pte_swp_clear_soft_dirty(pte_t pte)
{
	return __pte_raw(pte_raw(pte) & cpu_to_be64(~_PAGE_SWP_SOFT_DIRTY));
}
#endif /* CONFIG_HAVE_ARCH_SOFT_DIRTY */

static inline bool check_pte_access(unsigned long access, unsigned long ptev)
{
	/*
	 * This check for _PAGE_RWX and _PAGE_PRESENT bits
	 */
	if (access & ~ptev)
		return false;
	/*
	 * This check for access to privilege space
	 */
	if ((access & _PAGE_PRIVILEGED) != (ptev & _PAGE_PRIVILEGED))
		return false;

	return true;
}
/*
 * Generic functions with hash/radix callbacks
 */

static inline void __ptep_set_access_flags(struct vm_area_struct *vma,
					   pte_t *ptep, pte_t entry,
					   unsigned long address,
					   int psize)
{
	if (radix_enabled())
		return radix__ptep_set_access_flags(vma, ptep, entry,
						    address, psize);
	return hash__ptep_set_access_flags(ptep, entry);
}

#define __HAVE_ARCH_PTE_SAME
static inline int pte_same(pte_t pte_a, pte_t pte_b)
{
	if (radix_enabled())
		return radix__pte_same(pte_a, pte_b);
	return hash__pte_same(pte_a, pte_b);
}

static inline int pte_none(pte_t pte)
{
	if (radix_enabled())
		return radix__pte_none(pte);
	return hash__pte_none(pte);
}

static inline void __set_pte_at(struct mm_struct *mm, unsigned long addr,
				pte_t *ptep, pte_t pte, int percpu)
{

	VM_WARN_ON(!(pte_raw(pte) & cpu_to_be64(_PAGE_PTE)));
	/*
	 * Keep the _PAGE_PTE added till we are sure we handle _PAGE_PTE
	 * in all the callers.
	 */
	pte = __pte_raw(pte_raw(pte) | cpu_to_be64(_PAGE_PTE));

	if (radix_enabled())
		return radix__set_pte_at(mm, addr, ptep, pte, percpu);
	return hash__set_pte_at(mm, addr, ptep, pte, percpu);
}

#define _PAGE_CACHE_CTL	(_PAGE_SAO | _PAGE_NON_IDEMPOTENT | _PAGE_TOLERANT)

#define pgprot_noncached pgprot_noncached
static inline pgprot_t pgprot_noncached(pgprot_t prot)
{
	return __pgprot((pgprot_val(prot) & ~_PAGE_CACHE_CTL) |
			_PAGE_NON_IDEMPOTENT);
}

#define pgprot_noncached_wc pgprot_noncached_wc
static inline pgprot_t pgprot_noncached_wc(pgprot_t prot)
{
	return __pgprot((pgprot_val(prot) & ~_PAGE_CACHE_CTL) |
			_PAGE_TOLERANT);
}

#define pgprot_cached pgprot_cached
static inline pgprot_t pgprot_cached(pgprot_t prot)
{
	return __pgprot((pgprot_val(prot) & ~_PAGE_CACHE_CTL));
}

#define pgprot_writecombine pgprot_writecombine
static inline pgprot_t pgprot_writecombine(pgprot_t prot)
{
	return pgprot_noncached_wc(prot);
}
/*
 * check a pte mapping have cache inhibited property
 */
static inline bool pte_ci(pte_t pte)
{
	__be64 pte_v = pte_raw(pte);

	if (((pte_v & cpu_to_be64(_PAGE_CACHE_CTL)) == cpu_to_be64(_PAGE_TOLERANT)) ||
	    ((pte_v & cpu_to_be64(_PAGE_CACHE_CTL)) == cpu_to_be64(_PAGE_NON_IDEMPOTENT)))
		return true;
	return false;
}

static inline void pmd_clear(pmd_t *pmdp)
{
	if (IS_ENABLED(CONFIG_DEBUG_VM) && !radix_enabled()) {
		/*
		 * Don't use this if we can possibly have a hash page table
		 * entry mapping this.
		 */
		WARN_ON((pmd_val(*pmdp) & (H_PAGE_HASHPTE | _PAGE_PTE)) == (H_PAGE_HASHPTE | _PAGE_PTE));
	}
	*pmdp = __pmd(0);
}

static inline int pmd_none(pmd_t pmd)
{
	return !pmd_raw(pmd);
}

static inline int pmd_present(pmd_t pmd)
{
	/*
	 * A pmd is considerent present if _PAGE_PRESENT is set.
	 * We also need to consider the pmd present which is marked
	 * invalid during a split. Hence we look for _PAGE_INVALID
	 * if we find _PAGE_PRESENT cleared.
	 */
	if (pmd_raw(pmd) & cpu_to_be64(_PAGE_PRESENT | _PAGE_INVALID))
		return true;

	return false;
}

static inline int pmd_is_serializing(pmd_t pmd)
{
	/*
	 * If the pmd is undergoing a split, the _PAGE_PRESENT bit is clear
	 * and _PAGE_INVALID is set (see pmd_present, pmdp_invalidate).
	 *
	 * This condition may also occur when flushing a pmd while flushing
	 * it (see ptep_modify_prot_start), so callers must ensure this
	 * case is fine as well.
	 */
	if ((pmd_raw(pmd) & cpu_to_be64(_PAGE_PRESENT | _PAGE_INVALID)) ==
						cpu_to_be64(_PAGE_INVALID))
		return true;

	return false;
}

static inline int pmd_bad(pmd_t pmd)
{
	if (radix_enabled())
		return radix__pmd_bad(pmd);
	return hash__pmd_bad(pmd);
}

static inline void pud_clear(pud_t *pudp)
{
	if (IS_ENABLED(CONFIG_DEBUG_VM) && !radix_enabled()) {
		/*
		 * Don't use this if we can possibly have a hash page table
		 * entry mapping this.
		 */
		WARN_ON((pud_val(*pudp) & (H_PAGE_HASHPTE | _PAGE_PTE)) == (H_PAGE_HASHPTE | _PAGE_PTE));
	}
	*pudp = __pud(0);
}

static inline int pud_none(pud_t pud)
{
	return !pud_raw(pud);
}

static inline int pud_present(pud_t pud)
{
	return !!(pud_raw(pud) & cpu_to_be64(_PAGE_PRESENT));
}

extern struct page *pud_page(pud_t pud);
extern struct page *pmd_page(pmd_t pmd);
static inline pte_t pud_pte(pud_t pud)
{
	return __pte_raw(pud_raw(pud));
}

static inline pud_t pte_pud(pte_t pte)
{
	return __pud_raw(pte_raw(pte));
}
#define pud_write(pud)		pte_write(pud_pte(pud))

static inline int pud_bad(pud_t pud)
{
	if (radix_enabled())
		return radix__pud_bad(pud);
	return hash__pud_bad(pud);
}

#define pud_access_permitted pud_access_permitted
static inline bool pud_access_permitted(pud_t pud, bool write)
{
	return pte_access_permitted(pud_pte(pud), write);
}

#define __p4d_raw(x)	((p4d_t) { __pgd_raw(x) })
static inline __be64 p4d_raw(p4d_t x)
{
	return pgd_raw(x.pgd);
}

#define p4d_write(p4d)		pte_write(p4d_pte(p4d))

static inline void p4d_clear(p4d_t *p4dp)
{
	*p4dp = __p4d(0);
}

static inline int p4d_none(p4d_t p4d)
{
	return !p4d_raw(p4d);
}

static inline int p4d_present(p4d_t p4d)
{
	return !!(p4d_raw(p4d) & cpu_to_be64(_PAGE_PRESENT));
}

static inline pte_t p4d_pte(p4d_t p4d)
{
	return __pte_raw(p4d_raw(p4d));
}

static inline p4d_t pte_p4d(pte_t pte)
{
	return __p4d_raw(pte_raw(pte));
}

static inline int p4d_bad(p4d_t p4d)
{
	if (radix_enabled())
		return radix__p4d_bad(p4d);
	return hash__p4d_bad(p4d);
}

#define p4d_access_permitted p4d_access_permitted
static inline bool p4d_access_permitted(p4d_t p4d, bool write)
{
	return pte_access_permitted(p4d_pte(p4d), write);
}

extern struct page *p4d_page(p4d_t p4d);

/* Pointers in the page table tree are physical addresses */
#define __pgtable_ptr_val(ptr)	__pa(ptr)

#define pud_page_vaddr(pud)	__va(pud_val(pud) & ~PUD_MASKED_BITS)
#define p4d_page_vaddr(p4d)	__va(p4d_val(p4d) & ~P4D_MASKED_BITS)

#define pte_ERROR(e) \
	pr_err("%s:%d: bad pte %08lx.\n", __FILE__, __LINE__, pte_val(e))
#define pmd_ERROR(e) \
	pr_err("%s:%d: bad pmd %08lx.\n", __FILE__, __LINE__, pmd_val(e))
#define pud_ERROR(e) \
	pr_err("%s:%d: bad pud %08lx.\n", __FILE__, __LINE__, pud_val(e))
#define pgd_ERROR(e) \
	pr_err("%s:%d: bad pgd %08lx.\n", __FILE__, __LINE__, pgd_val(e))

static inline int map_kernel_page(unsigned long ea, unsigned long pa, pgprot_t prot)
{
	if (radix_enabled()) {
#if defined(CONFIG_PPC_RADIX_MMU) && defined(DEBUG_VM)
		unsigned long page_size = 1 << mmu_psize_defs[mmu_io_psize].shift;
		WARN((page_size != PAGE_SIZE), "I/O page size != PAGE_SIZE");
#endif
		return radix__map_kernel_page(ea, pa, prot, PAGE_SIZE);
	}
	return hash__map_kernel_page(ea, pa, prot);
}

static inline int __meminit vmemmap_create_mapping(unsigned long start,
						   unsigned long page_size,
						   unsigned long phys)
{
	if (radix_enabled())
		return radix__vmemmap_create_mapping(start, page_size, phys);
	return hash__vmemmap_create_mapping(start, page_size, phys);
}

#ifdef CONFIG_MEMORY_HOTPLUG
static inline void vmemmap_remove_mapping(unsigned long start,
					  unsigned long page_size)
{
	if (radix_enabled())
		return radix__vmemmap_remove_mapping(start, page_size);
	return hash__vmemmap_remove_mapping(start, page_size);
}
#endif

static inline pte_t pmd_pte(pmd_t pmd)
{
	return __pte_raw(pmd_raw(pmd));
}

static inline pmd_t pte_pmd(pte_t pte)
{
	return __pmd_raw(pte_raw(pte));
}

static inline pte_t *pmdp_ptep(pmd_t *pmd)
{
	return (pte_t *)pmd;
}
#define pmd_pfn(pmd)		pte_pfn(pmd_pte(pmd))
#define pmd_dirty(pmd)		pte_dirty(pmd_pte(pmd))
#define pmd_young(pmd)		pte_young(pmd_pte(pmd))
#define pmd_mkold(pmd)		pte_pmd(pte_mkold(pmd_pte(pmd)))
#define pmd_wrprotect(pmd)	pte_pmd(pte_wrprotect(pmd_pte(pmd)))
#define pmd_mkdirty(pmd)	pte_pmd(pte_mkdirty(pmd_pte(pmd)))
#define pmd_mkclean(pmd)	pte_pmd(pte_mkclean(pmd_pte(pmd)))
#define pmd_mkyoung(pmd)	pte_pmd(pte_mkyoung(pmd_pte(pmd)))
#define pmd_mkwrite(pmd)	pte_pmd(pte_mkwrite(pmd_pte(pmd)))
#define pmd_mk_savedwrite(pmd)	pte_pmd(pte_mk_savedwrite(pmd_pte(pmd)))
#define pmd_clear_savedwrite(pmd)	pte_pmd(pte_clear_savedwrite(pmd_pte(pmd)))

#ifdef CONFIG_HAVE_ARCH_SOFT_DIRTY
#define pmd_soft_dirty(pmd)    pte_soft_dirty(pmd_pte(pmd))
#define pmd_mksoft_dirty(pmd)  pte_pmd(pte_mksoft_dirty(pmd_pte(pmd)))
#define pmd_clear_soft_dirty(pmd) pte_pmd(pte_clear_soft_dirty(pmd_pte(pmd)))

#ifdef CONFIG_ARCH_ENABLE_THP_MIGRATION
#define pmd_swp_mksoft_dirty(pmd)	pte_pmd(pte_swp_mksoft_dirty(pmd_pte(pmd)))
#define pmd_swp_soft_dirty(pmd)		pte_swp_soft_dirty(pmd_pte(pmd))
#define pmd_swp_clear_soft_dirty(pmd)	pte_pmd(pte_swp_clear_soft_dirty(pmd_pte(pmd)))
#endif
#endif /* CONFIG_HAVE_ARCH_SOFT_DIRTY */

#ifdef CONFIG_NUMA_BALANCING
static inline int pmd_protnone(pmd_t pmd)
{
	return pte_protnone(pmd_pte(pmd));
}
#endif /* CONFIG_NUMA_BALANCING */

#define pmd_write(pmd)		pte_write(pmd_pte(pmd))
#define __pmd_write(pmd)	__pte_write(pmd_pte(pmd))
#define pmd_savedwrite(pmd)	pte_savedwrite(pmd_pte(pmd))

#define pmd_access_permitted pmd_access_permitted
static inline bool pmd_access_permitted(pmd_t pmd, bool write)
{
	/*
	 * pmdp_invalidate sets this combination (which is not caught by
	 * !pte_present() check in pte_access_permitted), to prevent
	 * lock-free lookups, as part of the serialize_against_pte_lookup()
	 * synchronisation.
	 *
	 * This also catches the case where the PTE's hardware PRESENT bit is
	 * cleared while TLB is flushed, which is suboptimal but should not
	 * be frequent.
	 */
	if (pmd_is_serializing(pmd))
		return false;

	return pte_access_permitted(pmd_pte(pmd), write);
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
extern pmd_t pfn_pmd(unsigned long pfn, pgprot_t pgprot);
extern pmd_t mk_pmd(struct page *page, pgprot_t pgprot);
extern pmd_t pmd_modify(pmd_t pmd, pgprot_t newprot);
extern void set_pmd_at(struct mm_struct *mm, unsigned long addr,
		       pmd_t *pmdp, pmd_t pmd);
static inline void update_mmu_cache_pmd(struct vm_area_struct *vma,
					unsigned long addr, pmd_t *pmd)
{
}

extern int hash__has_transparent_hugepage(void);
static inline int has_transparent_hugepage(void)
{
	if (radix_enabled())
		return radix__has_transparent_hugepage();
	return hash__has_transparent_hugepage();
}
#define has_transparent_hugepage has_transparent_hugepage

static inline unsigned long
pmd_hugepage_update(struct mm_struct *mm, unsigned long addr, pmd_t *pmdp,
		    unsigned long clr, unsigned long set)
{
	if (radix_enabled())
		return radix__pmd_hugepage_update(mm, addr, pmdp, clr, set);
	return hash__pmd_hugepage_update(mm, addr, pmdp, clr, set);
}

/*
 * returns true for pmd migration entries, THP, devmap, hugetlb
 * But compile time dependent on THP config
 */
static inline int pmd_large(pmd_t pmd)
{
	return !!(pmd_raw(pmd) & cpu_to_be64(_PAGE_PTE));
}

/*
 * For radix we should always find H_PAGE_HASHPTE zero. Hence
 * the below will work for radix too
 */
static inline int __pmdp_test_and_clear_young(struct mm_struct *mm,
					      unsigned long addr, pmd_t *pmdp)
{
	unsigned long old;

	if ((pmd_raw(*pmdp) & cpu_to_be64(_PAGE_ACCESSED | H_PAGE_HASHPTE)) == 0)
		return 0;
	old = pmd_hugepage_update(mm, addr, pmdp, _PAGE_ACCESSED, 0);
	return ((old & _PAGE_ACCESSED) != 0);
}

#define __HAVE_ARCH_PMDP_SET_WRPROTECT
static inline void pmdp_set_wrprotect(struct mm_struct *mm, unsigned long addr,
				      pmd_t *pmdp)
{
	if (__pmd_write((*pmdp)))
		pmd_hugepage_update(mm, addr, pmdp, _PAGE_WRITE, 0);
	else if (unlikely(pmd_savedwrite(*pmdp)))
		pmd_hugepage_update(mm, addr, pmdp, 0, _PAGE_PRIVILEGED);
}

/*
 * Only returns true for a THP. False for pmd migration entry.
 * We also need to return true when we come across a pte that
 * in between a thp split. While splitting THP, we mark the pmd
 * invalid (pmdp_invalidate()) before we set it with pte page
 * address. A pmd_trans_huge() check against a pmd entry during that time
 * should return true.
 * We should not call this on a hugetlb entry. We should check for HugeTLB
 * entry using vma->vm_flags
 * The page table walk rule is explained in Documentation/vm/transhuge.rst
 */
static inline int pmd_trans_huge(pmd_t pmd)
{
	if (!pmd_present(pmd))
		return false;

	if (radix_enabled())
		return radix__pmd_trans_huge(pmd);
	return hash__pmd_trans_huge(pmd);
}

#define __HAVE_ARCH_PMD_SAME
static inline int pmd_same(pmd_t pmd_a, pmd_t pmd_b)
{
	if (radix_enabled())
		return radix__pmd_same(pmd_a, pmd_b);
	return hash__pmd_same(pmd_a, pmd_b);
}

static inline pmd_t pmd_mkhuge(pmd_t pmd)
{
	if (radix_enabled())
		return radix__pmd_mkhuge(pmd);
	return hash__pmd_mkhuge(pmd);
}

#define __HAVE_ARCH_PMDP_SET_ACCESS_FLAGS
extern int pmdp_set_access_flags(struct vm_area_struct *vma,
				 unsigned long address, pmd_t *pmdp,
				 pmd_t entry, int dirty);

#define __HAVE_ARCH_PMDP_TEST_AND_CLEAR_YOUNG
extern int pmdp_test_and_clear_young(struct vm_area_struct *vma,
				     unsigned long address, pmd_t *pmdp);

#define __HAVE_ARCH_PMDP_HUGE_GET_AND_CLEAR
static inline pmd_t pmdp_huge_get_and_clear(struct mm_struct *mm,
					    unsigned long addr, pmd_t *pmdp)
{
	if (radix_enabled())
		return radix__pmdp_huge_get_and_clear(mm, addr, pmdp);
	return hash__pmdp_huge_get_and_clear(mm, addr, pmdp);
}

static inline pmd_t pmdp_collapse_flush(struct vm_area_struct *vma,
					unsigned long address, pmd_t *pmdp)
{
	if (radix_enabled())
		return radix__pmdp_collapse_flush(vma, address, pmdp);
	return hash__pmdp_collapse_flush(vma, address, pmdp);
}
#define pmdp_collapse_flush pmdp_collapse_flush

#define __HAVE_ARCH_PMDP_HUGE_GET_AND_CLEAR_FULL
pmd_t pmdp_huge_get_and_clear_full(struct vm_area_struct *vma,
				   unsigned long addr,
				   pmd_t *pmdp, int full);

#define __HAVE_ARCH_PGTABLE_DEPOSIT
static inline void pgtable_trans_huge_deposit(struct mm_struct *mm,
					      pmd_t *pmdp, pgtable_t pgtable)
{
	if (radix_enabled())
		return radix__pgtable_trans_huge_deposit(mm, pmdp, pgtable);
	return hash__pgtable_trans_huge_deposit(mm, pmdp, pgtable);
}

#define __HAVE_ARCH_PGTABLE_WITHDRAW
static inline pgtable_t pgtable_trans_huge_withdraw(struct mm_struct *mm,
						    pmd_t *pmdp)
{
	if (radix_enabled())
		return radix__pgtable_trans_huge_withdraw(mm, pmdp);
	return hash__pgtable_trans_huge_withdraw(mm, pmdp);
}

#define __HAVE_ARCH_PMDP_INVALIDATE
extern pmd_t pmdp_invalidate(struct vm_area_struct *vma, unsigned long address,
			     pmd_t *pmdp);

#define pmd_move_must_withdraw pmd_move_must_withdraw
struct spinlock;
extern int pmd_move_must_withdraw(struct spinlock *new_pmd_ptl,
				  struct spinlock *old_pmd_ptl,
				  struct vm_area_struct *vma);
/*
 * Hash translation mode use the deposited table to store hash pte
 * slot information.
 */
#define arch_needs_pgtable_deposit arch_needs_pgtable_deposit
static inline bool arch_needs_pgtable_deposit(void)
{
	if (radix_enabled())
		return false;
	return true;
}
extern void serialize_against_pte_lookup(struct mm_struct *mm);


static inline pmd_t pmd_mkdevmap(pmd_t pmd)
{
	if (radix_enabled())
		return radix__pmd_mkdevmap(pmd);
	return hash__pmd_mkdevmap(pmd);
}

static inline int pmd_devmap(pmd_t pmd)
{
	return pte_devmap(pmd_pte(pmd));
}

static inline int pud_devmap(pud_t pud)
{
	return 0;
}

static inline int pgd_devmap(pgd_t pgd)
{
	return 0;
}
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

static inline int pud_pfn(pud_t pud)
{
	/*
	 * Currently all calls to pud_pfn() are gated around a pud_devmap()
	 * check so this should never be used. If it grows another user we
	 * want to know about it.
	 */
	BUILD_BUG();
	return 0;
}
#define __HAVE_ARCH_PTEP_MODIFY_PROT_TRANSACTION
pte_t ptep_modify_prot_start(struct vm_area_struct *, unsigned long, pte_t *);
void ptep_modify_prot_commit(struct vm_area_struct *, unsigned long,
			     pte_t *, pte_t, pte_t);

/*
 * Returns true for a R -> RW upgrade of pte
 */
static inline bool is_pte_rw_upgrade(unsigned long old_val, unsigned long new_val)
{
	if (!(old_val & _PAGE_READ))
		return false;

	if ((!(old_val & _PAGE_WRITE)) && (new_val & _PAGE_WRITE))
		return true;

	return false;
}

/*
 * Like pmd_huge() and pmd_large(), but works regardless of config options
 */
#define pmd_is_leaf pmd_is_leaf
#define pmd_leaf pmd_is_leaf
static inline bool pmd_is_leaf(pmd_t pmd)
{
	return !!(pmd_raw(pmd) & cpu_to_be64(_PAGE_PTE));
}

#define pud_is_leaf pud_is_leaf
#define pud_leaf pud_is_leaf
static inline bool pud_is_leaf(pud_t pud)
{
	return !!(pud_raw(pud) & cpu_to_be64(_PAGE_PTE));
}

#define p4d_is_leaf p4d_is_leaf
#define p4d_leaf p4d_is_leaf
static inline bool p4d_is_leaf(p4d_t p4d)
{
	return !!(p4d_raw(p4d) & cpu_to_be64(_PAGE_PTE));
}

#endif /* __ASSEMBLY__ */
#endif /* _ASM_POWERPC_BOOK3S_64_PGTABLE_H_ */
