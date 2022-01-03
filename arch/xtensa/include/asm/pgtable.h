/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * include/asm-xtensa/pgtable.h
 *
 * Copyright (C) 2001 - 2013 Tensilica Inc.
 */

#ifndef _XTENSA_PGTABLE_H
#define _XTENSA_PGTABLE_H

#include <asm/page.h>
#include <asm/kmem_layout.h>
#include <asm-generic/pgtable-nopmd.h>

/*
 * We only use two ring levels, user and kernel space.
 */

#ifdef CONFIG_MMU
#define USER_RING		1	/* user ring level */
#else
#define USER_RING		0
#endif
#define KERNEL_RING		0	/* kernel ring level */

/*
 * The Xtensa architecture port of Linux has a two-level page table system,
 * i.e. the logical three-level Linux page table layout is folded.
 * Each task has the following memory page tables:
 *
 *   PGD table (page directory), ie. 3rd-level page table:
 *	One page (4 kB) of 1024 (PTRS_PER_PGD) pointers to PTE tables
 *	(Architectures that don't have the PMD folded point to the PMD tables)
 *
 *	The pointer to the PGD table for a given task can be retrieved from
 *	the task structure (struct task_struct*) t, e.g. current():
 *	  (t->mm ? t->mm : t->active_mm)->pgd
 *
 *   PMD tables (page middle-directory), ie. 2nd-level page tables:
 *	Absent for the Xtensa architecture (folded, PTRS_PER_PMD == 1).
 *
 *   PTE tables (page table entry), ie. 1st-level page tables:
 *	One page (4 kB) of 1024 (PTRS_PER_PTE) PTEs with a special PTE
 *	invalid_pte_table for absent mappings.
 *
 * The individual pages are 4 kB big with special pages for the empty_zero_page.
 */

#define PGDIR_SHIFT	22
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

/*
 * Entries per page directory level: we use two-level, so
 * we don't really have any PMD directory physically.
 */
#define PTRS_PER_PTE		1024
#define PTRS_PER_PTE_SHIFT	10
#define PTRS_PER_PGD		1024
#define PGD_ORDER		0
#define USER_PTRS_PER_PGD	(TASK_SIZE/PGDIR_SIZE)
#define FIRST_USER_PGD_NR	(FIRST_USER_ADDRESS >> PGDIR_SHIFT)

#ifdef CONFIG_MMU
/*
 * Virtual memory area. We keep a distance to other memory regions to be
 * on the safe side. We also use this area for cache aliasing.
 */
#define VMALLOC_START		(XCHAL_KSEG_CACHED_VADDR - 0x10000000)
#define VMALLOC_END		(VMALLOC_START + 0x07FEFFFF)
#define TLBTEMP_BASE_1		(VMALLOC_START + 0x08000000)
#define TLBTEMP_BASE_2		(TLBTEMP_BASE_1 + DCACHE_WAY_SIZE)
#if 2 * DCACHE_WAY_SIZE > ICACHE_WAY_SIZE
#define TLBTEMP_SIZE		(2 * DCACHE_WAY_SIZE)
#else
#define TLBTEMP_SIZE		ICACHE_WAY_SIZE
#endif

#else

#define VMALLOC_START		__XTENSA_UL_CONST(0)
#define VMALLOC_END		__XTENSA_UL_CONST(0xffffffff)

#endif

/*
 * For the Xtensa architecture, the PTE layout is as follows:
 *
 *		31------12  11  10-9   8-6  5-4  3-2  1-0
 *		+-----------------------------------------+
 *		|           |   Software   |   HARDWARE   |
 *		|    PPN    |          ADW | RI |Attribute|
 *		+-----------------------------------------+
 *   pte_none	|             MBZ          | 01 | 11 | 00 |
 *		+-----------------------------------------+
 *   present	|    PPN    | 0 | 00 | ADW | RI | CA | wx |
 *		+- - - - - - - - - - - - - - - - - - - - -+
 *   (PAGE_NONE)|    PPN    | 0 | 00 | ADW | 01 | 11 | 11 |
 *		+-----------------------------------------+
 *   swap	|     index     |   type   | 01 | 11 | 00 |
 *		+-----------------------------------------+
 *
 * For T1050 hardware and earlier the layout differs for present and (PAGE_NONE)
 *		+-----------------------------------------+
 *   present	|    PPN    | 0 | 00 | ADW | RI | CA | w1 |
 *		+-----------------------------------------+
 *   (PAGE_NONE)|    PPN    | 0 | 00 | ADW | 01 | 01 | 00 |
 *		+-----------------------------------------+
 *
 *  Legend:
 *   PPN        Physical Page Number
 *   ADW	software: accessed (young) / dirty / writable
 *   RI         ring (0=privileged, 1=user, 2 and 3 are unused)
 *   CA		cache attribute: 00 bypass, 01 writeback, 10 writethrough
 *		(11 is invalid and used to mark pages that are not present)
 *   w		page is writable (hw)
 *   x		page is executable (hw)
 *   index      swap offset / PAGE_SIZE (bit 11-31: 21 bits -> 8 GB)
 *		(note that the index is always non-zero)
 *   type       swap type (5 bits -> 32 types)
 *
 *  Notes:
 *   - (PROT_NONE) is a special case of 'present' but causes an exception for
 *     any access (read, write, and execute).
 *   - 'multihit-exception' has the highest priority of all MMU exceptions,
 *     so the ring must be set to 'RING_USER' even for 'non-present' pages.
 *   - on older hardware, the exectuable flag was not supported and
 *     used as a 'valid' flag, so it needs to be always set.
 *   - we need to keep track of certain flags in software (dirty and young)
 *     to do this, we use write exceptions and have a separate software w-flag.
 *   - attribute value 1101 (and 1111 on T1050 and earlier) is reserved
 */

#define _PAGE_ATTRIB_MASK	0xf

#define _PAGE_HW_EXEC		(1<<0)	/* hardware: page is executable */
#define _PAGE_HW_WRITE		(1<<1)	/* hardware: page is writable */

#define _PAGE_CA_BYPASS		(0<<2)	/* bypass, non-speculative */
#define _PAGE_CA_WB		(1<<2)	/* write-back */
#define _PAGE_CA_WT		(2<<2)	/* write-through */
#define _PAGE_CA_MASK		(3<<2)
#define _PAGE_CA_INVALID	(3<<2)

/* We use invalid attribute values to distinguish special pte entries */
#if XCHAL_HW_VERSION_MAJOR < 2000
#define _PAGE_HW_VALID		0x01	/* older HW needed this bit set */
#define _PAGE_NONE		0x04
#else
#define _PAGE_HW_VALID		0x00
#define _PAGE_NONE		0x0f
#endif

#define _PAGE_USER		(1<<4)	/* user access (ring=1) */

/* Software */
#define _PAGE_WRITABLE_BIT	6
#define _PAGE_WRITABLE		(1<<6)	/* software: page writable */
#define _PAGE_DIRTY		(1<<7)	/* software: page dirty */
#define _PAGE_ACCESSED		(1<<8)	/* software: page accessed (read) */

#ifdef CONFIG_MMU

#define _PAGE_CHG_MASK	   (PAGE_MASK | _PAGE_ACCESSED | _PAGE_DIRTY)
#define _PAGE_PRESENT	   (_PAGE_HW_VALID | _PAGE_CA_WB | _PAGE_ACCESSED)

#define PAGE_NONE	   __pgprot(_PAGE_NONE | _PAGE_USER)
#define PAGE_COPY	   __pgprot(_PAGE_PRESENT | _PAGE_USER)
#define PAGE_COPY_EXEC	   __pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_HW_EXEC)
#define PAGE_READONLY	   __pgprot(_PAGE_PRESENT | _PAGE_USER)
#define PAGE_READONLY_EXEC __pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_HW_EXEC)
#define PAGE_SHARED	   __pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_WRITABLE)
#define PAGE_SHARED_EXEC \
	__pgprot(_PAGE_PRESENT | _PAGE_USER | _PAGE_WRITABLE | _PAGE_HW_EXEC)
#define PAGE_KERNEL	   __pgprot(_PAGE_PRESENT | _PAGE_HW_WRITE)
#define PAGE_KERNEL_RO	   __pgprot(_PAGE_PRESENT)
#define PAGE_KERNEL_EXEC   __pgprot(_PAGE_PRESENT|_PAGE_HW_WRITE|_PAGE_HW_EXEC)

#if (DCACHE_WAY_SIZE > PAGE_SIZE)
# define _PAGE_DIRECTORY   (_PAGE_HW_VALID | _PAGE_ACCESSED | _PAGE_CA_BYPASS)
#else
# define _PAGE_DIRECTORY   (_PAGE_HW_VALID | _PAGE_ACCESSED | _PAGE_CA_WB)
#endif

#else /* no mmu */

# define _PAGE_CHG_MASK  (PAGE_MASK | _PAGE_ACCESSED | _PAGE_DIRTY)
# define PAGE_NONE       __pgprot(0)
# define PAGE_SHARED     __pgprot(0)
# define PAGE_COPY       __pgprot(0)
# define PAGE_READONLY   __pgprot(0)
# define PAGE_KERNEL     __pgprot(0)

#endif

/*
 * On certain configurations of Xtensa MMUs (eg. the initial Linux config),
 * the MMU can't do page protection for execute, and considers that the same as
 * read.  Also, write permissions may imply read permissions.
 * What follows is the closest we can get by reasonable means..
 * See linux/mm/mmap.c for protection_map[] array that uses these definitions.
 */
#define __P000	PAGE_NONE		/* private --- */
#define __P001	PAGE_READONLY		/* private --r */
#define __P010	PAGE_COPY		/* private -w- */
#define __P011	PAGE_COPY		/* private -wr */
#define __P100	PAGE_READONLY_EXEC	/* private x-- */
#define __P101	PAGE_READONLY_EXEC	/* private x-r */
#define __P110	PAGE_COPY_EXEC		/* private xw- */
#define __P111	PAGE_COPY_EXEC		/* private xwr */

#define __S000	PAGE_NONE		/* shared  --- */
#define __S001	PAGE_READONLY		/* shared  --r */
#define __S010	PAGE_SHARED		/* shared  -w- */
#define __S011	PAGE_SHARED		/* shared  -wr */
#define __S100	PAGE_READONLY_EXEC	/* shared  x-- */
#define __S101	PAGE_READONLY_EXEC	/* shared  x-r */
#define __S110	PAGE_SHARED_EXEC	/* shared  xw- */
#define __S111	PAGE_SHARED_EXEC	/* shared  xwr */

#ifndef __ASSEMBLY__

#define pte_ERROR(e) \
	printk("%s:%d: bad pte %08lx.\n", __FILE__, __LINE__, pte_val(e))
#define pgd_ERROR(e) \
	printk("%s:%d: bad pgd entry %08lx.\n", __FILE__, __LINE__, pgd_val(e))

extern unsigned long empty_zero_page[1024];

#define ZERO_PAGE(vaddr) (virt_to_page(empty_zero_page))

#ifdef CONFIG_MMU
extern pgd_t swapper_pg_dir[PAGE_SIZE/sizeof(pgd_t)];
extern void paging_init(void);
#else
# define swapper_pg_dir NULL
static inline void paging_init(void) { }
#endif

/*
 * The pmd contains the kernel virtual address of the pte page.
 */
#define pmd_page_vaddr(pmd) ((unsigned long)(pmd_val(pmd) & PAGE_MASK))
#define pmd_page(pmd) virt_to_page(pmd_val(pmd))

/*
 * pte status.
 */
# define pte_none(pte)	 (pte_val(pte) == (_PAGE_CA_INVALID | _PAGE_USER))
#if XCHAL_HW_VERSION_MAJOR < 2000
# define pte_present(pte) ((pte_val(pte) & _PAGE_CA_MASK) != _PAGE_CA_INVALID)
#else
# define pte_present(pte)						\
	(((pte_val(pte) & _PAGE_CA_MASK) != _PAGE_CA_INVALID)		\
	 || ((pte_val(pte) & _PAGE_ATTRIB_MASK) == _PAGE_NONE))
#endif
#define pte_clear(mm,addr,ptep)						\
	do { update_pte(ptep, __pte(_PAGE_CA_INVALID | _PAGE_USER)); } while (0)

#define pmd_none(pmd)	 (!pmd_val(pmd))
#define pmd_present(pmd) (pmd_val(pmd) & PAGE_MASK)
#define pmd_bad(pmd)	 (pmd_val(pmd) & ~PAGE_MASK)
#define pmd_clear(pmdp)	 do { set_pmd(pmdp, __pmd(0)); } while (0)

static inline int pte_write(pte_t pte) { return pte_val(pte) & _PAGE_WRITABLE; }
static inline int pte_dirty(pte_t pte) { return pte_val(pte) & _PAGE_DIRTY; }
static inline int pte_young(pte_t pte) { return pte_val(pte) & _PAGE_ACCESSED; }

static inline pte_t pte_wrprotect(pte_t pte)
	{ pte_val(pte) &= ~(_PAGE_WRITABLE | _PAGE_HW_WRITE); return pte; }
static inline pte_t pte_mkclean(pte_t pte)
	{ pte_val(pte) &= ~(_PAGE_DIRTY | _PAGE_HW_WRITE); return pte; }
static inline pte_t pte_mkold(pte_t pte)
	{ pte_val(pte) &= ~_PAGE_ACCESSED; return pte; }
static inline pte_t pte_mkdirty(pte_t pte)
	{ pte_val(pte) |= _PAGE_DIRTY; return pte; }
static inline pte_t pte_mkyoung(pte_t pte)
	{ pte_val(pte) |= _PAGE_ACCESSED; return pte; }
static inline pte_t pte_mkwrite(pte_t pte)
	{ pte_val(pte) |= _PAGE_WRITABLE; return pte; }

#define pgprot_noncached(prot) \
		((__pgprot((pgprot_val(prot) & ~_PAGE_CA_MASK) | \
			   _PAGE_CA_BYPASS)))

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */

#define pte_pfn(pte)		(pte_val(pte) >> PAGE_SHIFT)
#define pte_same(a,b)		(pte_val(a) == pte_val(b))
#define pte_page(x)		pfn_to_page(pte_pfn(x))
#define pfn_pte(pfn, prot)	__pte(((pfn) << PAGE_SHIFT) | pgprot_val(prot))
#define mk_pte(page, prot)	pfn_pte(page_to_pfn(page), prot)

static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	return __pte((pte_val(pte) & _PAGE_CHG_MASK) | pgprot_val(newprot));
}

/*
 * Certain architectures need to do special things when pte's
 * within a page table are directly modified.  Thus, the following
 * hook is made available.
 */
static inline void update_pte(pte_t *ptep, pte_t pteval)
{
	*ptep = pteval;
#if (DCACHE_WAY_SIZE > PAGE_SIZE) && XCHAL_DCACHE_IS_WRITEBACK
	__asm__ __volatile__ ("dhwb %0, 0" :: "a" (ptep));
#endif

}

struct mm_struct;

static inline void
set_pte_at(struct mm_struct *mm, unsigned long addr, pte_t *ptep, pte_t pteval)
{
	update_pte(ptep, pteval);
}

static inline void set_pte(pte_t *ptep, pte_t pteval)
{
	update_pte(ptep, pteval);
}

static inline void
set_pmd(pmd_t *pmdp, pmd_t pmdval)
{
	*pmdp = pmdval;
}

struct vm_area_struct;

static inline int
ptep_test_and_clear_young(struct vm_area_struct *vma, unsigned long addr,
			  pte_t *ptep)
{
	pte_t pte = *ptep;
	if (!pte_young(pte))
		return 0;
	update_pte(ptep, pte_mkold(pte));
	return 1;
}

static inline pte_t
ptep_get_and_clear(struct mm_struct *mm, unsigned long addr, pte_t *ptep)
{
	pte_t pte = *ptep;
	pte_clear(mm, addr, ptep);
	return pte;
}

static inline void
ptep_set_wrprotect(struct mm_struct *mm, unsigned long addr, pte_t *ptep)
{
	pte_t pte = *ptep;
	update_pte(ptep, pte_wrprotect(pte));
}

/*
 * Encode and decode a swap and file entry.
 */
#define SWP_TYPE_BITS		5
#define MAX_SWAPFILES_CHECK() BUILD_BUG_ON(MAX_SWAPFILES_SHIFT > SWP_TYPE_BITS)

#define __swp_type(entry)	(((entry).val >> 6) & 0x1f)
#define __swp_offset(entry)	((entry).val >> 11)
#define __swp_entry(type,offs)	\
	((swp_entry_t){((type) << 6) | ((offs) << 11) | \
	 _PAGE_CA_INVALID | _PAGE_USER})
#define __pte_to_swp_entry(pte)	((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(x)	((pte_t) { (x).val })

#endif /*  !defined (__ASSEMBLY__) */


#ifdef __ASSEMBLY__

/* Assembly macro _PGD_INDEX is the same as C pgd_index(unsigned long),
 *                _PGD_OFFSET as C pgd_offset(struct mm_struct*, unsigned long),
 *                _PMD_OFFSET as C pmd_offset(pgd_t*, unsigned long)
 *                _PTE_OFFSET as C pte_offset(pmd_t*, unsigned long)
 *
 * Note: We require an additional temporary register which can be the same as
 *       the register that holds the address.
 *
 * ((pte_t*) ((unsigned long)(pmd_val(*pmd) & PAGE_MASK)) + pte_index(addr))
 *
 */
#define _PGD_INDEX(rt,rs)	extui	rt, rs, PGDIR_SHIFT, 32-PGDIR_SHIFT
#define _PTE_INDEX(rt,rs)	extui	rt, rs, PAGE_SHIFT, PTRS_PER_PTE_SHIFT

#define _PGD_OFFSET(mm,adr,tmp)		l32i	mm, mm, MM_PGD;		\
					_PGD_INDEX(tmp, adr);		\
					addx4	mm, tmp, mm

#define _PTE_OFFSET(pmd,adr,tmp)	_PTE_INDEX(tmp, adr);		\
					srli	pmd, pmd, PAGE_SHIFT;	\
					slli	pmd, pmd, PAGE_SHIFT;	\
					addx4	pmd, tmp, pmd

#else

#define kern_addr_valid(addr)	(1)

extern  void update_mmu_cache(struct vm_area_struct * vma,
			      unsigned long address, pte_t *ptep);

typedef pte_t *pte_addr_t;

void update_mmu_tlb(struct vm_area_struct *vma,
		    unsigned long address, pte_t *ptep);
#define __HAVE_ARCH_UPDATE_MMU_TLB

#endif /* !defined (__ASSEMBLY__) */

#define __HAVE_ARCH_PTEP_TEST_AND_CLEAR_YOUNG
#define __HAVE_ARCH_PTEP_GET_AND_CLEAR
#define __HAVE_ARCH_PTEP_SET_WRPROTECT
#define __HAVE_ARCH_PTEP_MKDIRTY
#define __HAVE_ARCH_PTE_SAME
/* We provide our own get_unmapped_area to cope with
 * SHM area cache aliasing for userland.
 */
#define HAVE_ARCH_UNMAPPED_AREA

#endif /* _XTENSA_PGTABLE_H */
