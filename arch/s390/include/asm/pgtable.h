/*
 *  S390 version
 *    Copyright IBM Corp. 1999, 2000
 *    Author(s): Hartmut Penner (hp@de.ibm.com)
 *               Ulrich Weigand (weigand@de.ibm.com)
 *               Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 *  Derived from "include/asm-i386/pgtable.h"
 */

#ifndef _ASM_S390_PGTABLE_H
#define _ASM_S390_PGTABLE_H

/*
 * The Linux memory management assumes a three-level page table setup. For
 * s390 31 bit we "fold" the mid level into the top-level page table, so
 * that we physically have the same two-level page table as the s390 mmu
 * expects in 31 bit mode. For s390 64 bit we use three of the five levels
 * the hardware provides (region first and region second tables are not
 * used).
 *
 * The "pgd_xxx()" functions are trivial for a folded two-level
 * setup: the pgd is never bad, and a pmd always exists (as it's folded
 * into the pgd entry)
 *
 * This file contains the functions and defines necessary to modify and use
 * the S390 page table tree.
 */
#ifndef __ASSEMBLY__
#include <linux/sched.h>
#include <linux/mm_types.h>
#include <linux/page-flags.h>
#include <asm/bug.h>
#include <asm/page.h>

extern pgd_t swapper_pg_dir[] __attribute__ ((aligned (4096)));
extern void paging_init(void);
extern void vmem_map_init(void);

/*
 * The S390 doesn't have any external MMU info: the kernel page
 * tables contain all the necessary information.
 */
#define update_mmu_cache(vma, address, ptep)     do { } while (0)
#define update_mmu_cache_pmd(vma, address, ptep) do { } while (0)

/*
 * ZERO_PAGE is a global shared page that is always zero; used
 * for zero-mapped memory areas etc..
 */

extern unsigned long empty_zero_page;
extern unsigned long zero_page_mask;

#define ZERO_PAGE(vaddr) \
	(virt_to_page((void *)(empty_zero_page + \
	 (((unsigned long)(vaddr)) &zero_page_mask))))
#define __HAVE_COLOR_ZERO_PAGE

/* TODO: s390 cannot support io_remap_pfn_range... */
#endif /* !__ASSEMBLY__ */

/*
 * PMD_SHIFT determines the size of the area a second-level page
 * table can map
 * PGDIR_SHIFT determines what a third-level page table entry can map
 */
#ifndef CONFIG_64BIT
# define PMD_SHIFT	20
# define PUD_SHIFT	20
# define PGDIR_SHIFT	20
#else /* CONFIG_64BIT */
# define PMD_SHIFT	20
# define PUD_SHIFT	31
# define PGDIR_SHIFT	42
#endif /* CONFIG_64BIT */

#define PMD_SIZE        (1UL << PMD_SHIFT)
#define PMD_MASK        (~(PMD_SIZE-1))
#define PUD_SIZE	(1UL << PUD_SHIFT)
#define PUD_MASK	(~(PUD_SIZE-1))
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

/*
 * entries per page directory level: the S390 is two-level, so
 * we don't really have any PMD directory physically.
 * for S390 segment-table entries are combined to one PGD
 * that leads to 1024 pte per pgd
 */
#define PTRS_PER_PTE	256
#ifndef CONFIG_64BIT
#define PTRS_PER_PMD	1
#define PTRS_PER_PUD	1
#else /* CONFIG_64BIT */
#define PTRS_PER_PMD	2048
#define PTRS_PER_PUD	2048
#endif /* CONFIG_64BIT */
#define PTRS_PER_PGD	2048

#define FIRST_USER_ADDRESS  0

#define pte_ERROR(e) \
	printk("%s:%d: bad pte %p.\n", __FILE__, __LINE__, (void *) pte_val(e))
#define pmd_ERROR(e) \
	printk("%s:%d: bad pmd %p.\n", __FILE__, __LINE__, (void *) pmd_val(e))
#define pud_ERROR(e) \
	printk("%s:%d: bad pud %p.\n", __FILE__, __LINE__, (void *) pud_val(e))
#define pgd_ERROR(e) \
	printk("%s:%d: bad pgd %p.\n", __FILE__, __LINE__, (void *) pgd_val(e))

#ifndef __ASSEMBLY__
/*
 * The vmalloc and module area will always be on the topmost area of the kernel
 * mapping. We reserve 96MB (31bit) / 128GB (64bit) for vmalloc and modules.
 * On 64 bit kernels we have a 2GB area at the top of the vmalloc area where
 * modules will reside. That makes sure that inter module branches always
 * happen without trampolines and in addition the placement within a 2GB frame
 * is branch prediction unit friendly.
 */
extern unsigned long VMALLOC_START;
extern unsigned long VMALLOC_END;
extern struct page *vmemmap;

#define VMEM_MAX_PHYS ((unsigned long) vmemmap)

#ifdef CONFIG_64BIT
extern unsigned long MODULES_VADDR;
extern unsigned long MODULES_END;
#define MODULES_VADDR	MODULES_VADDR
#define MODULES_END	MODULES_END
#define MODULES_LEN	(1UL << 31)
#endif

/*
 * A 31 bit pagetable entry of S390 has following format:
 *  |   PFRA          |    |  OS  |
 * 0                   0IP0
 * 00000000001111111111222222222233
 * 01234567890123456789012345678901
 *
 * I Page-Invalid Bit:    Page is not available for address-translation
 * P Page-Protection Bit: Store access not possible for page
 *
 * A 31 bit segmenttable entry of S390 has following format:
 *  |   P-table origin      |  |PTL
 * 0                         IC
 * 00000000001111111111222222222233
 * 01234567890123456789012345678901
 *
 * I Segment-Invalid Bit:    Segment is not available for address-translation
 * C Common-Segment Bit:     Segment is not private (PoP 3-30)
 * PTL Page-Table-Length:    Page-table length (PTL+1*16 entries -> up to 256)
 *
 * The 31 bit segmenttable origin of S390 has following format:
 *
 *  |S-table origin   |     | STL |
 * X                   **GPS
 * 00000000001111111111222222222233
 * 01234567890123456789012345678901
 *
 * X Space-Switch event:
 * G Segment-Invalid Bit:     *
 * P Private-Space Bit:       Segment is not private (PoP 3-30)
 * S Storage-Alteration:
 * STL Segment-Table-Length:  Segment-table length (STL+1*16 entries -> up to 2048)
 *
 * A 64 bit pagetable entry of S390 has following format:
 * |			 PFRA			      |0IPC|  OS  |
 * 0000000000111111111122222222223333333333444444444455555555556666
 * 0123456789012345678901234567890123456789012345678901234567890123
 *
 * I Page-Invalid Bit:    Page is not available for address-translation
 * P Page-Protection Bit: Store access not possible for page
 * C Change-bit override: HW is not required to set change bit
 *
 * A 64 bit segmenttable entry of S390 has following format:
 * |        P-table origin                              |      TT
 * 0000000000111111111122222222223333333333444444444455555555556666
 * 0123456789012345678901234567890123456789012345678901234567890123
 *
 * I Segment-Invalid Bit:    Segment is not available for address-translation
 * C Common-Segment Bit:     Segment is not private (PoP 3-30)
 * P Page-Protection Bit: Store access not possible for page
 * TT Type 00
 *
 * A 64 bit region table entry of S390 has following format:
 * |        S-table origin                             |   TF  TTTL
 * 0000000000111111111122222222223333333333444444444455555555556666
 * 0123456789012345678901234567890123456789012345678901234567890123
 *
 * I Segment-Invalid Bit:    Segment is not available for address-translation
 * TT Type 01
 * TF
 * TL Table length
 *
 * The 64 bit regiontable origin of S390 has following format:
 * |      region table origon                          |       DTTL
 * 0000000000111111111122222222223333333333444444444455555555556666
 * 0123456789012345678901234567890123456789012345678901234567890123
 *
 * X Space-Switch event:
 * G Segment-Invalid Bit:  
 * P Private-Space Bit:    
 * S Storage-Alteration:
 * R Real space
 * TL Table-Length:
 *
 * A storage key has the following format:
 * | ACC |F|R|C|0|
 *  0   3 4 5 6 7
 * ACC: access key
 * F  : fetch protection bit
 * R  : referenced bit
 * C  : changed bit
 */

/* Hardware bits in the page table entry */
#define _PAGE_CO	0x100		/* HW Change-bit override */
#define _PAGE_PROTECT	0x200		/* HW read-only bit  */
#define _PAGE_INVALID	0x400		/* HW invalid bit    */
#define _PAGE_LARGE	0x800		/* Bit to mark a large pte */

/* Software bits in the page table entry */
#define _PAGE_PRESENT	0x001		/* SW pte present bit */
#define _PAGE_TYPE	0x002		/* SW pte type bit */
#define _PAGE_YOUNG	0x004		/* SW pte young bit */
#define _PAGE_DIRTY	0x008		/* SW pte dirty bit */
#define _PAGE_WRITE	0x010		/* SW pte write bit */
#define _PAGE_SPECIAL	0x020		/* SW associated with special page */
#define __HAVE_ARCH_PTE_SPECIAL

/* Set of bits not changed in pte_modify */
#define _PAGE_CHG_MASK		(PAGE_MASK | _PAGE_SPECIAL | _PAGE_CO | \
				 _PAGE_DIRTY | _PAGE_YOUNG)

/*
 * handle_pte_fault uses pte_present, pte_none and pte_file to find out the
 * pte type WITHOUT holding the page table lock. The _PAGE_PRESENT bit
 * is used to distinguish present from not-present ptes. It is changed only
 * with the page table lock held.
 *
 * The following table gives the different possible bit combinations for
 * the pte hardware and software bits in the last 12 bits of a pte:
 *
 *			842100000000
 *			000084210000
 *			000000008421
 *			.IR....wdytp
 * empty		.10....00000
 * swap			.10....xxx10
 * file			.11....xxxx0
 * prot-none, clean	.11....00x01
 * prot-none, dirty	.10....01x01
 * read-only, clean	.01....00x01
 * read-only, dirty	.01....01x01
 * read-write, clean	.01....10x01
 * read-write, dirty	.00....11x01
 *
 * pte_present is true for the bit pattern .xx...xxxxx1, (pte & 0x001) == 0x001
 * pte_none    is true for the bit pattern .10...xxxx00, (pte & 0x603) == 0x400
 * pte_file    is true for the bit pattern .11...xxxxx0, (pte & 0x601) == 0x600
 * pte_swap    is true for the bit pattern .10...xxxx10, (pte & 0x603) == 0x402
 */

#ifndef CONFIG_64BIT

/* Bits in the segment table address-space-control-element */
#define _ASCE_SPACE_SWITCH	0x80000000UL	/* space switch event	    */
#define _ASCE_ORIGIN_MASK	0x7ffff000UL	/* segment table origin	    */
#define _ASCE_PRIVATE_SPACE	0x100	/* private space control	    */
#define _ASCE_ALT_EVENT		0x80	/* storage alteration event control */
#define _ASCE_TABLE_LENGTH	0x7f	/* 128 x 64 entries = 8k	    */

/* Bits in the segment table entry */
#define _SEGMENT_ENTRY_ORIGIN	0x7fffffc0UL	/* page table origin	    */
#define _SEGMENT_ENTRY_PROTECT	0x200	/* page protection bit		    */
#define _SEGMENT_ENTRY_INVALID	0x20	/* invalid segment table entry	    */
#define _SEGMENT_ENTRY_COMMON	0x10	/* common segment bit		    */
#define _SEGMENT_ENTRY_PTL	0x0f	/* page table length		    */

#define _SEGMENT_ENTRY		(_SEGMENT_ENTRY_PTL)
#define _SEGMENT_ENTRY_EMPTY	(_SEGMENT_ENTRY_INVALID)

/* Page status table bits for virtualization */
#define PGSTE_ACC_BITS	0xf0000000UL
#define PGSTE_FP_BIT	0x08000000UL
#define PGSTE_PCL_BIT	0x00800000UL
#define PGSTE_HR_BIT	0x00400000UL
#define PGSTE_HC_BIT	0x00200000UL
#define PGSTE_GR_BIT	0x00040000UL
#define PGSTE_GC_BIT	0x00020000UL
#define PGSTE_UR_BIT	0x00008000UL
#define PGSTE_UC_BIT	0x00004000UL	/* user dirty (migration) */
#define PGSTE_IN_BIT	0x00002000UL	/* IPTE notify bit */

#else /* CONFIG_64BIT */

/* Bits in the segment/region table address-space-control-element */
#define _ASCE_ORIGIN		~0xfffUL/* segment table origin		    */
#define _ASCE_PRIVATE_SPACE	0x100	/* private space control	    */
#define _ASCE_ALT_EVENT		0x80	/* storage alteration event control */
#define _ASCE_SPACE_SWITCH	0x40	/* space switch event		    */
#define _ASCE_REAL_SPACE	0x20	/* real space control		    */
#define _ASCE_TYPE_MASK		0x0c	/* asce table type mask		    */
#define _ASCE_TYPE_REGION1	0x0c	/* region first table type	    */
#define _ASCE_TYPE_REGION2	0x08	/* region second table type	    */
#define _ASCE_TYPE_REGION3	0x04	/* region third table type	    */
#define _ASCE_TYPE_SEGMENT	0x00	/* segment table type		    */
#define _ASCE_TABLE_LENGTH	0x03	/* region table length		    */

/* Bits in the region table entry */
#define _REGION_ENTRY_ORIGIN	~0xfffUL/* region/segment table origin	    */
#define _REGION_ENTRY_PROTECT	0x200	/* region protection bit	    */
#define _REGION_ENTRY_INVALID	0x20	/* invalid region table entry	    */
#define _REGION_ENTRY_TYPE_MASK	0x0c	/* region/segment table type mask   */
#define _REGION_ENTRY_TYPE_R1	0x0c	/* region first table type	    */
#define _REGION_ENTRY_TYPE_R2	0x08	/* region second table type	    */
#define _REGION_ENTRY_TYPE_R3	0x04	/* region third table type	    */
#define _REGION_ENTRY_LENGTH	0x03	/* region third length		    */

#define _REGION1_ENTRY		(_REGION_ENTRY_TYPE_R1 | _REGION_ENTRY_LENGTH)
#define _REGION1_ENTRY_EMPTY	(_REGION_ENTRY_TYPE_R1 | _REGION_ENTRY_INVALID)
#define _REGION2_ENTRY		(_REGION_ENTRY_TYPE_R2 | _REGION_ENTRY_LENGTH)
#define _REGION2_ENTRY_EMPTY	(_REGION_ENTRY_TYPE_R2 | _REGION_ENTRY_INVALID)
#define _REGION3_ENTRY		(_REGION_ENTRY_TYPE_R3 | _REGION_ENTRY_LENGTH)
#define _REGION3_ENTRY_EMPTY	(_REGION_ENTRY_TYPE_R3 | _REGION_ENTRY_INVALID)

#define _REGION3_ENTRY_LARGE	0x400	/* RTTE-format control, large page  */
#define _REGION3_ENTRY_RO	0x200	/* page protection bit		    */
#define _REGION3_ENTRY_CO	0x100	/* change-recording override	    */

/* Bits in the segment table entry */
#define _SEGMENT_ENTRY_ORIGIN_LARGE ~0xfffffUL /* large page address	    */
#define _SEGMENT_ENTRY_ORIGIN	~0x7ffUL/* segment table origin		    */
#define _SEGMENT_ENTRY_PROTECT	0x200	/* page protection bit		    */
#define _SEGMENT_ENTRY_INVALID	0x20	/* invalid segment table entry	    */

#define _SEGMENT_ENTRY		(0)
#define _SEGMENT_ENTRY_EMPTY	(_SEGMENT_ENTRY_INVALID)

#define _SEGMENT_ENTRY_LARGE	0x400	/* STE-format control, large page   */
#define _SEGMENT_ENTRY_CO	0x100	/* change-recording override   */
#define _SEGMENT_ENTRY_SPLIT	0x001	/* THP splitting bit */

#define _SEGMENT_ENTRY_SPLIT_BIT 0	/* THP splitting bit number */

/* Set of bits not changed in pmd_modify */
#define _SEGMENT_CHG_MASK	(_SEGMENT_ENTRY_ORIGIN | _SEGMENT_ENTRY_LARGE \
				 | _SEGMENT_ENTRY_SPLIT | _SEGMENT_ENTRY_CO)

/* Page status table bits for virtualization */
#define PGSTE_ACC_BITS	0xf000000000000000UL
#define PGSTE_FP_BIT	0x0800000000000000UL
#define PGSTE_PCL_BIT	0x0080000000000000UL
#define PGSTE_HR_BIT	0x0040000000000000UL
#define PGSTE_HC_BIT	0x0020000000000000UL
#define PGSTE_GR_BIT	0x0004000000000000UL
#define PGSTE_GC_BIT	0x0002000000000000UL
#define PGSTE_UR_BIT	0x0000800000000000UL
#define PGSTE_UC_BIT	0x0000400000000000UL	/* user dirty (migration) */
#define PGSTE_IN_BIT	0x0000200000000000UL	/* IPTE notify bit */

#endif /* CONFIG_64BIT */

/*
 * A user page table pointer has the space-switch-event bit, the
 * private-space-control bit and the storage-alteration-event-control
 * bit set. A kernel page table pointer doesn't need them.
 */
#define _ASCE_USER_BITS		(_ASCE_SPACE_SWITCH | _ASCE_PRIVATE_SPACE | \
				 _ASCE_ALT_EVENT)

/*
 * Page protection definitions.
 */
#define PAGE_NONE	__pgprot(_PAGE_PRESENT | _PAGE_INVALID)
#define PAGE_READ	__pgprot(_PAGE_PRESENT | _PAGE_PROTECT)
#define PAGE_WRITE	__pgprot(_PAGE_PRESENT | _PAGE_WRITE | _PAGE_PROTECT)

#define PAGE_SHARED	__pgprot(_PAGE_PRESENT | _PAGE_WRITE | _PAGE_DIRTY)
#define PAGE_KERNEL	__pgprot(_PAGE_PRESENT | _PAGE_WRITE | _PAGE_DIRTY)
#define PAGE_KERNEL_RO	__pgprot(_PAGE_PRESENT | _PAGE_PROTECT)

/*
 * On s390 the page table entry has an invalid bit and a read-only bit.
 * Read permission implies execute permission and write permission
 * implies read permission.
 */
         /*xwr*/
#define __P000	PAGE_NONE
#define __P001	PAGE_READ
#define __P010	PAGE_READ
#define __P011	PAGE_READ
#define __P100	PAGE_READ
#define __P101	PAGE_READ
#define __P110	PAGE_READ
#define __P111	PAGE_READ

#define __S000	PAGE_NONE
#define __S001	PAGE_READ
#define __S010	PAGE_WRITE
#define __S011	PAGE_WRITE
#define __S100	PAGE_READ
#define __S101	PAGE_READ
#define __S110	PAGE_WRITE
#define __S111	PAGE_WRITE

/*
 * Segment entry (large page) protection definitions.
 */
#define SEGMENT_NONE	__pgprot(_SEGMENT_ENTRY_INVALID | \
				 _SEGMENT_ENTRY_PROTECT)
#define SEGMENT_READ	__pgprot(_SEGMENT_ENTRY_PROTECT)
#define SEGMENT_WRITE	__pgprot(0)

static inline int mm_has_pgste(struct mm_struct *mm)
{
#ifdef CONFIG_PGSTE
	if (unlikely(mm->context.has_pgste))
		return 1;
#endif
	return 0;
}
/*
 * pgd/pmd/pte query functions
 */
#ifndef CONFIG_64BIT

static inline int pgd_present(pgd_t pgd) { return 1; }
static inline int pgd_none(pgd_t pgd)    { return 0; }
static inline int pgd_bad(pgd_t pgd)     { return 0; }

static inline int pud_present(pud_t pud) { return 1; }
static inline int pud_none(pud_t pud)	 { return 0; }
static inline int pud_large(pud_t pud)	 { return 0; }
static inline int pud_bad(pud_t pud)	 { return 0; }

#else /* CONFIG_64BIT */

static inline int pgd_present(pgd_t pgd)
{
	if ((pgd_val(pgd) & _REGION_ENTRY_TYPE_MASK) < _REGION_ENTRY_TYPE_R2)
		return 1;
	return (pgd_val(pgd) & _REGION_ENTRY_ORIGIN) != 0UL;
}

static inline int pgd_none(pgd_t pgd)
{
	if ((pgd_val(pgd) & _REGION_ENTRY_TYPE_MASK) < _REGION_ENTRY_TYPE_R2)
		return 0;
	return (pgd_val(pgd) & _REGION_ENTRY_INVALID) != 0UL;
}

static inline int pgd_bad(pgd_t pgd)
{
	/*
	 * With dynamic page table levels the pgd can be a region table
	 * entry or a segment table entry. Check for the bit that are
	 * invalid for either table entry.
	 */
	unsigned long mask =
		~_SEGMENT_ENTRY_ORIGIN & ~_REGION_ENTRY_INVALID &
		~_REGION_ENTRY_TYPE_MASK & ~_REGION_ENTRY_LENGTH;
	return (pgd_val(pgd) & mask) != 0;
}

static inline int pud_present(pud_t pud)
{
	if ((pud_val(pud) & _REGION_ENTRY_TYPE_MASK) < _REGION_ENTRY_TYPE_R3)
		return 1;
	return (pud_val(pud) & _REGION_ENTRY_ORIGIN) != 0UL;
}

static inline int pud_none(pud_t pud)
{
	if ((pud_val(pud) & _REGION_ENTRY_TYPE_MASK) < _REGION_ENTRY_TYPE_R3)
		return 0;
	return (pud_val(pud) & _REGION_ENTRY_INVALID) != 0UL;
}

static inline int pud_large(pud_t pud)
{
	if ((pud_val(pud) & _REGION_ENTRY_TYPE_MASK) != _REGION_ENTRY_TYPE_R3)
		return 0;
	return !!(pud_val(pud) & _REGION3_ENTRY_LARGE);
}

static inline int pud_bad(pud_t pud)
{
	/*
	 * With dynamic page table levels the pud can be a region table
	 * entry or a segment table entry. Check for the bit that are
	 * invalid for either table entry.
	 */
	unsigned long mask =
		~_SEGMENT_ENTRY_ORIGIN & ~_REGION_ENTRY_INVALID &
		~_REGION_ENTRY_TYPE_MASK & ~_REGION_ENTRY_LENGTH;
	return (pud_val(pud) & mask) != 0;
}

#endif /* CONFIG_64BIT */

static inline int pmd_present(pmd_t pmd)
{
	return pmd_val(pmd) != _SEGMENT_ENTRY_INVALID;
}

static inline int pmd_none(pmd_t pmd)
{
	return pmd_val(pmd) == _SEGMENT_ENTRY_INVALID;
}

static inline int pmd_large(pmd_t pmd)
{
#ifdef CONFIG_64BIT
	return (pmd_val(pmd) & _SEGMENT_ENTRY_LARGE) != 0;
#else
	return 0;
#endif
}

static inline int pmd_bad(pmd_t pmd)
{
	unsigned long mask = ~_SEGMENT_ENTRY_ORIGIN & ~_SEGMENT_ENTRY_INVALID;
	return (pmd_val(pmd) & mask) != _SEGMENT_ENTRY;
}

#define __HAVE_ARCH_PMDP_SPLITTING_FLUSH
extern void pmdp_splitting_flush(struct vm_area_struct *vma,
				 unsigned long addr, pmd_t *pmdp);

#define  __HAVE_ARCH_PMDP_SET_ACCESS_FLAGS
extern int pmdp_set_access_flags(struct vm_area_struct *vma,
				 unsigned long address, pmd_t *pmdp,
				 pmd_t entry, int dirty);

#define __HAVE_ARCH_PMDP_CLEAR_YOUNG_FLUSH
extern int pmdp_clear_flush_young(struct vm_area_struct *vma,
				  unsigned long address, pmd_t *pmdp);

#define __HAVE_ARCH_PMD_WRITE
static inline int pmd_write(pmd_t pmd)
{
	return (pmd_val(pmd) & _SEGMENT_ENTRY_PROTECT) == 0;
}

static inline int pmd_young(pmd_t pmd)
{
	return 0;
}

static inline int pte_present(pte_t pte)
{
	/* Bit pattern: (pte & 0x001) == 0x001 */
	return (pte_val(pte) & _PAGE_PRESENT) != 0;
}

static inline int pte_none(pte_t pte)
{
	/* Bit pattern: pte == 0x400 */
	return pte_val(pte) == _PAGE_INVALID;
}

static inline int pte_file(pte_t pte)
{
	/* Bit pattern: (pte & 0x601) == 0x600 */
	return (pte_val(pte) & (_PAGE_INVALID | _PAGE_PROTECT | _PAGE_PRESENT))
		== (_PAGE_INVALID | _PAGE_PROTECT);
}

static inline int pte_special(pte_t pte)
{
	return (pte_val(pte) & _PAGE_SPECIAL);
}

#define __HAVE_ARCH_PTE_SAME
static inline int pte_same(pte_t a, pte_t b)
{
	return pte_val(a) == pte_val(b);
}

static inline pgste_t pgste_get_lock(pte_t *ptep)
{
	unsigned long new = 0;
#ifdef CONFIG_PGSTE
	unsigned long old;

	preempt_disable();
	asm(
		"	lg	%0,%2\n"
		"0:	lgr	%1,%0\n"
		"	nihh	%0,0xff7f\n"	/* clear PCL bit in old */
		"	oihh	%1,0x0080\n"	/* set PCL bit in new */
		"	csg	%0,%1,%2\n"
		"	jl	0b\n"
		: "=&d" (old), "=&d" (new), "=Q" (ptep[PTRS_PER_PTE])
		: "Q" (ptep[PTRS_PER_PTE]) : "cc", "memory");
#endif
	return __pgste(new);
}

static inline void pgste_set_unlock(pte_t *ptep, pgste_t pgste)
{
#ifdef CONFIG_PGSTE
	asm(
		"	nihh	%1,0xff7f\n"	/* clear PCL bit */
		"	stg	%1,%0\n"
		: "=Q" (ptep[PTRS_PER_PTE])
		: "d" (pgste_val(pgste)), "Q" (ptep[PTRS_PER_PTE])
		: "cc", "memory");
	preempt_enable();
#endif
}

static inline pgste_t pgste_get(pte_t *ptep)
{
	unsigned long pgste = 0;
#ifdef CONFIG_PGSTE
	pgste = *(unsigned long *)(ptep + PTRS_PER_PTE);
#endif
	return __pgste(pgste);
}

static inline void pgste_set(pte_t *ptep, pgste_t pgste)
{
#ifdef CONFIG_PGSTE
	*(pgste_t *)(ptep + PTRS_PER_PTE) = pgste;
#endif
}

static inline pgste_t pgste_update_all(pte_t *ptep, pgste_t pgste)
{
#ifdef CONFIG_PGSTE
	unsigned long address, bits;
	unsigned char skey;

	if (pte_val(*ptep) & _PAGE_INVALID)
		return pgste;
	address = pte_val(*ptep) & PAGE_MASK;
	skey = page_get_storage_key(address);
	bits = skey & (_PAGE_CHANGED | _PAGE_REFERENCED);
	/* Clear page changed & referenced bit in the storage key */
	if (bits & _PAGE_CHANGED)
		page_set_storage_key(address, skey ^ bits, 0);
	else if (bits)
		page_reset_referenced(address);
	/* Transfer page changed & referenced bit to guest bits in pgste */
	pgste_val(pgste) |= bits << 48;		/* GR bit & GC bit */
	/* Get host changed & referenced bits from pgste */
	bits |= (pgste_val(pgste) & (PGSTE_HR_BIT | PGSTE_HC_BIT)) >> 52;
	/* Transfer page changed & referenced bit to kvm user bits */
	pgste_val(pgste) |= bits << 45;		/* PGSTE_UR_BIT & PGSTE_UC_BIT */
	/* Clear relevant host bits in pgste. */
	pgste_val(pgste) &= ~(PGSTE_HR_BIT | PGSTE_HC_BIT);
	pgste_val(pgste) &= ~(PGSTE_ACC_BITS | PGSTE_FP_BIT);
	/* Copy page access key and fetch protection bit to pgste */
	pgste_val(pgste) |=
		(unsigned long) (skey & (_PAGE_ACC_BITS | _PAGE_FP_BIT)) << 56;
	/* Transfer referenced bit to pte */
	pte_val(*ptep) |= (bits & _PAGE_REFERENCED) << 1;
#endif
	return pgste;

}

static inline pgste_t pgste_update_young(pte_t *ptep, pgste_t pgste)
{
#ifdef CONFIG_PGSTE
	int young;

	if (pte_val(*ptep) & _PAGE_INVALID)
		return pgste;
	/* Get referenced bit from storage key */
	young = page_reset_referenced(pte_val(*ptep) & PAGE_MASK);
	if (young)
		pgste_val(pgste) |= PGSTE_GR_BIT;
	/* Get host referenced bit from pgste */
	if (pgste_val(pgste) & PGSTE_HR_BIT) {
		pgste_val(pgste) &= ~PGSTE_HR_BIT;
		young = 1;
	}
	/* Transfer referenced bit to kvm user bits and pte */
	if (young) {
		pgste_val(pgste) |= PGSTE_UR_BIT;
		pte_val(*ptep) |= _PAGE_YOUNG;
	}
#endif
	return pgste;
}

static inline void pgste_set_key(pte_t *ptep, pgste_t pgste, pte_t entry)
{
#ifdef CONFIG_PGSTE
	unsigned long address;
	unsigned long nkey;

	if (pte_val(entry) & _PAGE_INVALID)
		return;
	VM_BUG_ON(!(pte_val(*ptep) & _PAGE_INVALID));
	address = pte_val(entry) & PAGE_MASK;
	/*
	 * Set page access key and fetch protection bit from pgste.
	 * The guest C/R information is still in the PGSTE, set real
	 * key C/R to 0.
	 */
	nkey = (pgste_val(pgste) & (PGSTE_ACC_BITS | PGSTE_FP_BIT)) >> 56;
	page_set_storage_key(address, nkey, 0);
#endif
}

static inline void pgste_set_pte(pte_t *ptep, pte_t entry)
{
	if (!MACHINE_HAS_ESOP && (pte_val(entry) & _PAGE_WRITE)) {
		/*
		 * Without enhanced suppression-on-protection force
		 * the dirty bit on for all writable ptes.
		 */
		pte_val(entry) |= _PAGE_DIRTY;
		pte_val(entry) &= ~_PAGE_PROTECT;
	}
	*ptep = entry;
}

/**
 * struct gmap_struct - guest address space
 * @mm: pointer to the parent mm_struct
 * @table: pointer to the page directory
 * @asce: address space control element for gmap page table
 * @crst_list: list of all crst tables used in the guest address space
 */
struct gmap {
	struct list_head list;
	struct mm_struct *mm;
	unsigned long *table;
	unsigned long asce;
	void *private;
	struct list_head crst_list;
};

/**
 * struct gmap_rmap - reverse mapping for segment table entries
 * @gmap: pointer to the gmap_struct
 * @entry: pointer to a segment table entry
 * @vmaddr: virtual address in the guest address space
 */
struct gmap_rmap {
	struct list_head list;
	struct gmap *gmap;
	unsigned long *entry;
	unsigned long vmaddr;
};

/**
 * struct gmap_pgtable - gmap information attached to a page table
 * @vmaddr: address of the 1MB segment in the process virtual memory
 * @mapper: list of segment table entries mapping a page table
 */
struct gmap_pgtable {
	unsigned long vmaddr;
	struct list_head mapper;
};

/**
 * struct gmap_notifier - notify function block for page invalidation
 * @notifier_call: address of callback function
 */
struct gmap_notifier {
	struct list_head list;
	void (*notifier_call)(struct gmap *gmap, unsigned long address);
};

struct gmap *gmap_alloc(struct mm_struct *mm);
void gmap_free(struct gmap *gmap);
void gmap_enable(struct gmap *gmap);
void gmap_disable(struct gmap *gmap);
int gmap_map_segment(struct gmap *gmap, unsigned long from,
		     unsigned long to, unsigned long len);
int gmap_unmap_segment(struct gmap *gmap, unsigned long to, unsigned long len);
unsigned long __gmap_translate(unsigned long address, struct gmap *);
unsigned long gmap_translate(unsigned long address, struct gmap *);
unsigned long __gmap_fault(unsigned long address, struct gmap *);
unsigned long gmap_fault(unsigned long address, struct gmap *);
void gmap_discard(unsigned long from, unsigned long to, struct gmap *);

void gmap_register_ipte_notifier(struct gmap_notifier *);
void gmap_unregister_ipte_notifier(struct gmap_notifier *);
int gmap_ipte_notify(struct gmap *, unsigned long start, unsigned long len);
void gmap_do_ipte_notify(struct mm_struct *, unsigned long addr, pte_t *);

static inline pgste_t pgste_ipte_notify(struct mm_struct *mm,
					unsigned long addr,
					pte_t *ptep, pgste_t pgste)
{
#ifdef CONFIG_PGSTE
	if (pgste_val(pgste) & PGSTE_IN_BIT) {
		pgste_val(pgste) &= ~PGSTE_IN_BIT;
		gmap_do_ipte_notify(mm, addr, ptep);
	}
#endif
	return pgste;
}

/*
 * Certain architectures need to do special things when PTEs
 * within a page table are directly modified.  Thus, the following
 * hook is made available.
 */
static inline void set_pte_at(struct mm_struct *mm, unsigned long addr,
			      pte_t *ptep, pte_t entry)
{
	pgste_t pgste;

	if (mm_has_pgste(mm)) {
		pgste = pgste_get_lock(ptep);
		pgste_set_key(ptep, pgste, entry);
		pgste_set_pte(ptep, entry);
		pgste_set_unlock(ptep, pgste);
	} else {
		if (!(pte_val(entry) & _PAGE_INVALID) && MACHINE_HAS_EDAT1)
			pte_val(entry) |= _PAGE_CO;
		*ptep = entry;
	}
}

/*
 * query functions pte_write/pte_dirty/pte_young only work if
 * pte_present() is true. Undefined behaviour if not..
 */
static inline int pte_write(pte_t pte)
{
	return (pte_val(pte) & _PAGE_WRITE) != 0;
}

static inline int pte_dirty(pte_t pte)
{
	return (pte_val(pte) & _PAGE_DIRTY) != 0;
}

static inline int pte_young(pte_t pte)
{
#ifdef CONFIG_PGSTE
	if (pte_val(pte) & _PAGE_YOUNG)
		return 1;
#endif
	return 0;
}

/*
 * pgd/pmd/pte modification functions
 */

static inline void pgd_clear(pgd_t *pgd)
{
#ifdef CONFIG_64BIT
	if ((pgd_val(*pgd) & _REGION_ENTRY_TYPE_MASK) == _REGION_ENTRY_TYPE_R2)
		pgd_val(*pgd) = _REGION2_ENTRY_EMPTY;
#endif
}

static inline void pud_clear(pud_t *pud)
{
#ifdef CONFIG_64BIT
	if ((pud_val(*pud) & _REGION_ENTRY_TYPE_MASK) == _REGION_ENTRY_TYPE_R3)
		pud_val(*pud) = _REGION3_ENTRY_EMPTY;
#endif
}

static inline void pmd_clear(pmd_t *pmdp)
{
	pmd_val(*pmdp) = _SEGMENT_ENTRY_INVALID;
}

static inline void pte_clear(struct mm_struct *mm, unsigned long addr, pte_t *ptep)
{
	pte_val(*ptep) = _PAGE_INVALID;
}

/*
 * The following pte modification functions only work if
 * pte_present() is true. Undefined behaviour if not..
 */
static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	pte_val(pte) &= _PAGE_CHG_MASK;
	pte_val(pte) |= pgprot_val(newprot);
	if ((pte_val(pte) & _PAGE_DIRTY) && (pte_val(pte) & _PAGE_WRITE))
		pte_val(pte) &= ~_PAGE_PROTECT;
	return pte;
}

static inline pte_t pte_wrprotect(pte_t pte)
{
	pte_val(pte) &= ~_PAGE_WRITE;
	pte_val(pte) |= _PAGE_PROTECT;
	return pte;
}

static inline pte_t pte_mkwrite(pte_t pte)
{
	pte_val(pte) |= _PAGE_WRITE;
	if (pte_val(pte) & _PAGE_DIRTY)
		pte_val(pte) &= ~_PAGE_PROTECT;
	return pte;
}

static inline pte_t pte_mkclean(pte_t pte)
{
	pte_val(pte) &= ~_PAGE_DIRTY;
	pte_val(pte) |= _PAGE_PROTECT;
	return pte;
}

static inline pte_t pte_mkdirty(pte_t pte)
{
	pte_val(pte) |= _PAGE_DIRTY;
	if (pte_val(pte) & _PAGE_WRITE)
		pte_val(pte) &= ~_PAGE_PROTECT;
	return pte;
}

static inline pte_t pte_mkold(pte_t pte)
{
#ifdef CONFIG_PGSTE
	pte_val(pte) &= ~_PAGE_YOUNG;
#endif
	return pte;
}

static inline pte_t pte_mkyoung(pte_t pte)
{
	return pte;
}

static inline pte_t pte_mkspecial(pte_t pte)
{
	pte_val(pte) |= _PAGE_SPECIAL;
	return pte;
}

#ifdef CONFIG_HUGETLB_PAGE
static inline pte_t pte_mkhuge(pte_t pte)
{
	pte_val(pte) |= _PAGE_LARGE;
	return pte;
}
#endif

/*
 * Get (and clear) the user dirty bit for a pte.
 */
static inline int ptep_test_and_clear_user_dirty(struct mm_struct *mm,
						 pte_t *ptep)
{
	pgste_t pgste;
	int dirty = 0;

	if (mm_has_pgste(mm)) {
		pgste = pgste_get_lock(ptep);
		pgste = pgste_update_all(ptep, pgste);
		dirty = !!(pgste_val(pgste) & PGSTE_UC_BIT);
		pgste_val(pgste) &= ~PGSTE_UC_BIT;
		pgste_set_unlock(ptep, pgste);
		return dirty;
	}
	return dirty;
}

/*
 * Get (and clear) the user referenced bit for a pte.
 */
static inline int ptep_test_and_clear_user_young(struct mm_struct *mm,
						 pte_t *ptep)
{
	pgste_t pgste;
	int young = 0;

	if (mm_has_pgste(mm)) {
		pgste = pgste_get_lock(ptep);
		pgste = pgste_update_young(ptep, pgste);
		young = !!(pgste_val(pgste) & PGSTE_UR_BIT);
		pgste_val(pgste) &= ~PGSTE_UR_BIT;
		pgste_set_unlock(ptep, pgste);
	}
	return young;
}

#define __HAVE_ARCH_PTEP_TEST_AND_CLEAR_YOUNG
static inline int ptep_test_and_clear_young(struct vm_area_struct *vma,
					    unsigned long addr, pte_t *ptep)
{
	pgste_t pgste;
	pte_t pte;

	if (mm_has_pgste(vma->vm_mm)) {
		pgste = pgste_get_lock(ptep);
		pgste = pgste_update_young(ptep, pgste);
		pte = *ptep;
		*ptep = pte_mkold(pte);
		pgste_set_unlock(ptep, pgste);
		return pte_young(pte);
	}
	return 0;
}

#define __HAVE_ARCH_PTEP_CLEAR_YOUNG_FLUSH
static inline int ptep_clear_flush_young(struct vm_area_struct *vma,
					 unsigned long address, pte_t *ptep)
{
	/* No need to flush TLB
	 * On s390 reference bits are in storage key and never in TLB
	 * With virtualization we handle the reference bit, without we
	 * we can simply return */
	return ptep_test_and_clear_young(vma, address, ptep);
}

static inline void __ptep_ipte(unsigned long address, pte_t *ptep)
{
	if (!(pte_val(*ptep) & _PAGE_INVALID)) {
#ifndef CONFIG_64BIT
		/* pto must point to the start of the segment table */
		pte_t *pto = (pte_t *) (((unsigned long) ptep) & 0x7ffffc00);
#else
		/* ipte in zarch mode can do the math */
		pte_t *pto = ptep;
#endif
		asm volatile(
			"	ipte	%2,%3"
			: "=m" (*ptep) : "m" (*ptep),
			  "a" (pto), "a" (address));
	}
}

static inline void ptep_flush_lazy(struct mm_struct *mm,
				   unsigned long address, pte_t *ptep)
{
	int active = (mm == current->active_mm) ? 1 : 0;

	if (atomic_read(&mm->context.attach_count) > active)
		__ptep_ipte(address, ptep);
	else
		mm->context.flush_mm = 1;
}

/*
 * This is hard to understand. ptep_get_and_clear and ptep_clear_flush
 * both clear the TLB for the unmapped pte. The reason is that
 * ptep_get_and_clear is used in common code (e.g. change_pte_range)
 * to modify an active pte. The sequence is
 *   1) ptep_get_and_clear
 *   2) set_pte_at
 *   3) flush_tlb_range
 * On s390 the tlb needs to get flushed with the modification of the pte
 * if the pte is active. The only way how this can be implemented is to
 * have ptep_get_and_clear do the tlb flush. In exchange flush_tlb_range
 * is a nop.
 */
#define __HAVE_ARCH_PTEP_GET_AND_CLEAR
static inline pte_t ptep_get_and_clear(struct mm_struct *mm,
				       unsigned long address, pte_t *ptep)
{
	pgste_t pgste;
	pte_t pte;

	if (mm_has_pgste(mm)) {
		pgste = pgste_get_lock(ptep);
		pgste = pgste_ipte_notify(mm, address, ptep, pgste);
	}

	pte = *ptep;
	ptep_flush_lazy(mm, address, ptep);
	pte_val(*ptep) = _PAGE_INVALID;

	if (mm_has_pgste(mm)) {
		pgste = pgste_update_all(&pte, pgste);
		pgste_set_unlock(ptep, pgste);
	}
	return pte;
}

#define __HAVE_ARCH_PTEP_MODIFY_PROT_TRANSACTION
static inline pte_t ptep_modify_prot_start(struct mm_struct *mm,
					   unsigned long address,
					   pte_t *ptep)
{
	pgste_t pgste;
	pte_t pte;

	if (mm_has_pgste(mm)) {
		pgste = pgste_get_lock(ptep);
		pgste_ipte_notify(mm, address, ptep, pgste);
	}

	pte = *ptep;
	ptep_flush_lazy(mm, address, ptep);

	if (mm_has_pgste(mm)) {
		pgste = pgste_update_all(&pte, pgste);
		pgste_set(ptep, pgste);
	}
	return pte;
}

static inline void ptep_modify_prot_commit(struct mm_struct *mm,
					   unsigned long address,
					   pte_t *ptep, pte_t pte)
{
	pgste_t pgste;

	if (mm_has_pgste(mm)) {
		pgste = pgste_get(ptep);
		pgste_set_key(ptep, pgste, pte);
		pgste_set_pte(ptep, pte);
		pgste_set_unlock(ptep, pgste);
	} else
		*ptep = pte;
}

#define __HAVE_ARCH_PTEP_CLEAR_FLUSH
static inline pte_t ptep_clear_flush(struct vm_area_struct *vma,
				     unsigned long address, pte_t *ptep)
{
	pgste_t pgste;
	pte_t pte;

	if (mm_has_pgste(vma->vm_mm)) {
		pgste = pgste_get_lock(ptep);
		pgste = pgste_ipte_notify(vma->vm_mm, address, ptep, pgste);
	}

	pte = *ptep;
	__ptep_ipte(address, ptep);
	pte_val(*ptep) = _PAGE_INVALID;

	if (mm_has_pgste(vma->vm_mm)) {
		pgste = pgste_update_all(&pte, pgste);
		pgste_set_unlock(ptep, pgste);
	}
	return pte;
}

/*
 * The batched pte unmap code uses ptep_get_and_clear_full to clear the
 * ptes. Here an optimization is possible. tlb_gather_mmu flushes all
 * tlbs of an mm if it can guarantee that the ptes of the mm_struct
 * cannot be accessed while the batched unmap is running. In this case
 * full==1 and a simple pte_clear is enough. See tlb.h.
 */
#define __HAVE_ARCH_PTEP_GET_AND_CLEAR_FULL
static inline pte_t ptep_get_and_clear_full(struct mm_struct *mm,
					    unsigned long address,
					    pte_t *ptep, int full)
{
	pgste_t pgste;
	pte_t pte;

	if (!full && mm_has_pgste(mm)) {
		pgste = pgste_get_lock(ptep);
		pgste = pgste_ipte_notify(mm, address, ptep, pgste);
	}

	pte = *ptep;
	if (!full)
		ptep_flush_lazy(mm, address, ptep);
	pte_val(*ptep) = _PAGE_INVALID;

	if (!full && mm_has_pgste(mm)) {
		pgste = pgste_update_all(&pte, pgste);
		pgste_set_unlock(ptep, pgste);
	}
	return pte;
}

#define __HAVE_ARCH_PTEP_SET_WRPROTECT
static inline pte_t ptep_set_wrprotect(struct mm_struct *mm,
				       unsigned long address, pte_t *ptep)
{
	pgste_t pgste;
	pte_t pte = *ptep;

	if (pte_write(pte)) {
		if (mm_has_pgste(mm)) {
			pgste = pgste_get_lock(ptep);
			pgste = pgste_ipte_notify(mm, address, ptep, pgste);
		}

		ptep_flush_lazy(mm, address, ptep);
		pte = pte_wrprotect(pte);

		if (mm_has_pgste(mm)) {
			pgste_set_pte(ptep, pte);
			pgste_set_unlock(ptep, pgste);
		} else
			*ptep = pte;
	}
	return pte;
}

#define __HAVE_ARCH_PTEP_SET_ACCESS_FLAGS
static inline int ptep_set_access_flags(struct vm_area_struct *vma,
					unsigned long address, pte_t *ptep,
					pte_t entry, int dirty)
{
	pgste_t pgste;

	if (pte_same(*ptep, entry))
		return 0;
	if (mm_has_pgste(vma->vm_mm)) {
		pgste = pgste_get_lock(ptep);
		pgste = pgste_ipte_notify(vma->vm_mm, address, ptep, pgste);
	}

	__ptep_ipte(address, ptep);

	if (mm_has_pgste(vma->vm_mm)) {
		pgste_set_pte(ptep, entry);
		pgste_set_unlock(ptep, pgste);
	} else
		*ptep = entry;
	return 1;
}

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
static inline pte_t mk_pte_phys(unsigned long physpage, pgprot_t pgprot)
{
	pte_t __pte;
	pte_val(__pte) = physpage + pgprot_val(pgprot);
	return __pte;
}

static inline pte_t mk_pte(struct page *page, pgprot_t pgprot)
{
	unsigned long physpage = page_to_phys(page);
	pte_t __pte = mk_pte_phys(physpage, pgprot);

	if (pte_write(__pte) && PageDirty(page))
		__pte = pte_mkdirty(__pte);
	return __pte;
}

#define pgd_index(address) (((address) >> PGDIR_SHIFT) & (PTRS_PER_PGD-1))
#define pud_index(address) (((address) >> PUD_SHIFT) & (PTRS_PER_PUD-1))
#define pmd_index(address) (((address) >> PMD_SHIFT) & (PTRS_PER_PMD-1))
#define pte_index(address) (((address) >> PAGE_SHIFT) & (PTRS_PER_PTE-1))

#define pgd_offset(mm, address) ((mm)->pgd + pgd_index(address))
#define pgd_offset_k(address) pgd_offset(&init_mm, address)

#ifndef CONFIG_64BIT

#define pmd_deref(pmd) (pmd_val(pmd) & _SEGMENT_ENTRY_ORIGIN)
#define pud_deref(pmd) ({ BUG(); 0UL; })
#define pgd_deref(pmd) ({ BUG(); 0UL; })

#define pud_offset(pgd, address) ((pud_t *) pgd)
#define pmd_offset(pud, address) ((pmd_t *) pud + pmd_index(address))

#else /* CONFIG_64BIT */

#define pmd_deref(pmd) (pmd_val(pmd) & _SEGMENT_ENTRY_ORIGIN)
#define pud_deref(pud) (pud_val(pud) & _REGION_ENTRY_ORIGIN)
#define pgd_deref(pgd) (pgd_val(pgd) & _REGION_ENTRY_ORIGIN)

static inline pud_t *pud_offset(pgd_t *pgd, unsigned long address)
{
	pud_t *pud = (pud_t *) pgd;
	if ((pgd_val(*pgd) & _REGION_ENTRY_TYPE_MASK) == _REGION_ENTRY_TYPE_R2)
		pud = (pud_t *) pgd_deref(*pgd);
	return pud  + pud_index(address);
}

static inline pmd_t *pmd_offset(pud_t *pud, unsigned long address)
{
	pmd_t *pmd = (pmd_t *) pud;
	if ((pud_val(*pud) & _REGION_ENTRY_TYPE_MASK) == _REGION_ENTRY_TYPE_R3)
		pmd = (pmd_t *) pud_deref(*pud);
	return pmd + pmd_index(address);
}

#endif /* CONFIG_64BIT */

#define pfn_pte(pfn,pgprot) mk_pte_phys(__pa((pfn) << PAGE_SHIFT),(pgprot))
#define pte_pfn(x) (pte_val(x) >> PAGE_SHIFT)
#define pte_page(x) pfn_to_page(pte_pfn(x))

#define pmd_page(pmd) pfn_to_page(pmd_val(pmd) >> PAGE_SHIFT)

/* Find an entry in the lowest level page table.. */
#define pte_offset(pmd, addr) ((pte_t *) pmd_deref(*(pmd)) + pte_index(addr))
#define pte_offset_kernel(pmd, address) pte_offset(pmd,address)
#define pte_offset_map(pmd, address) pte_offset_kernel(pmd, address)
#define pte_unmap(pte) do { } while (0)

static inline void __pmd_idte(unsigned long address, pmd_t *pmdp)
{
	unsigned long sto = (unsigned long) pmdp -
			    pmd_index(address) * sizeof(pmd_t);

	if (!(pmd_val(*pmdp) & _SEGMENT_ENTRY_INVALID)) {
		asm volatile(
			"	.insn	rrf,0xb98e0000,%2,%3,0,0"
			: "=m" (*pmdp)
			: "m" (*pmdp), "a" (sto),
			  "a" ((address & HPAGE_MASK))
			: "cc"
		);
	}
}

static inline void __pmd_csp(pmd_t *pmdp)
{
	register unsigned long reg2 asm("2") = pmd_val(*pmdp);
	register unsigned long reg3 asm("3") = pmd_val(*pmdp) |
					       _SEGMENT_ENTRY_INVALID;
	register unsigned long reg4 asm("4") = ((unsigned long) pmdp) + 5;

	asm volatile(
		"	csp %1,%3"
		: "=m" (*pmdp)
		: "d" (reg2), "d" (reg3), "d" (reg4), "m" (*pmdp) : "cc");
}

#if defined(CONFIG_TRANSPARENT_HUGEPAGE) || defined(CONFIG_HUGETLB_PAGE)
static inline unsigned long massage_pgprot_pmd(pgprot_t pgprot)
{
	/*
	 * pgprot is PAGE_NONE, PAGE_READ, or PAGE_WRITE (see __Pxxx / __Sxxx)
	 * Convert to segment table entry format.
	 */
	if (pgprot_val(pgprot) == pgprot_val(PAGE_NONE))
		return pgprot_val(SEGMENT_NONE);
	if (pgprot_val(pgprot) == pgprot_val(PAGE_READ))
		return pgprot_val(SEGMENT_READ);
	return pgprot_val(SEGMENT_WRITE);
}

static inline pmd_t pmd_modify(pmd_t pmd, pgprot_t newprot)
{
	pmd_val(pmd) &= _SEGMENT_CHG_MASK;
	pmd_val(pmd) |= massage_pgprot_pmd(newprot);
	return pmd;
}

static inline pmd_t mk_pmd_phys(unsigned long physpage, pgprot_t pgprot)
{
	pmd_t __pmd;
	pmd_val(__pmd) = physpage + massage_pgprot_pmd(pgprot);
	return __pmd;
}

static inline pmd_t pmd_mkwrite(pmd_t pmd)
{
	/* Do not clobber PROT_NONE pages! */
	if (!(pmd_val(pmd) & _SEGMENT_ENTRY_INVALID))
		pmd_val(pmd) &= ~_SEGMENT_ENTRY_PROTECT;
	return pmd;
}
#endif /* CONFIG_TRANSPARENT_HUGEPAGE || CONFIG_HUGETLB_PAGE */

#ifdef CONFIG_TRANSPARENT_HUGEPAGE

#define __HAVE_ARCH_PGTABLE_DEPOSIT
extern void pgtable_trans_huge_deposit(struct mm_struct *mm, pmd_t *pmdp,
				       pgtable_t pgtable);

#define __HAVE_ARCH_PGTABLE_WITHDRAW
extern pgtable_t pgtable_trans_huge_withdraw(struct mm_struct *mm, pmd_t *pmdp);

static inline int pmd_trans_splitting(pmd_t pmd)
{
	return pmd_val(pmd) & _SEGMENT_ENTRY_SPLIT;
}

static inline void set_pmd_at(struct mm_struct *mm, unsigned long addr,
			      pmd_t *pmdp, pmd_t entry)
{
	if (!(pmd_val(entry) & _SEGMENT_ENTRY_INVALID) && MACHINE_HAS_EDAT1)
		pmd_val(entry) |= _SEGMENT_ENTRY_CO;
	*pmdp = entry;
}

static inline pmd_t pmd_mkhuge(pmd_t pmd)
{
	pmd_val(pmd) |= _SEGMENT_ENTRY_LARGE;
	return pmd;
}

static inline pmd_t pmd_wrprotect(pmd_t pmd)
{
	pmd_val(pmd) |= _SEGMENT_ENTRY_PROTECT;
	return pmd;
}

static inline pmd_t pmd_mkdirty(pmd_t pmd)
{
	/* No dirty bit in the segment table entry. */
	return pmd;
}

static inline pmd_t pmd_mkold(pmd_t pmd)
{
	/* No referenced bit in the segment table entry. */
	return pmd;
}

static inline pmd_t pmd_mkyoung(pmd_t pmd)
{
	/* No referenced bit in the segment table entry. */
	return pmd;
}

#define __HAVE_ARCH_PMDP_TEST_AND_CLEAR_YOUNG
static inline int pmdp_test_and_clear_young(struct vm_area_struct *vma,
					    unsigned long address, pmd_t *pmdp)
{
	unsigned long pmd_addr = pmd_val(*pmdp) & HPAGE_MASK;
	long tmp, rc;
	int counter;

	rc = 0;
	if (MACHINE_HAS_RRBM) {
		counter = PTRS_PER_PTE >> 6;
		asm volatile(
			"0:	.insn	rre,0xb9ae0000,%0,%3\n"	/* rrbm */
			"	ogr	%1,%0\n"
			"	la	%3,0(%4,%3)\n"
			"	brct	%2,0b\n"
			: "=&d" (tmp), "+&d" (rc), "+d" (counter),
			  "+a" (pmd_addr)
			: "a" (64 * 4096UL) : "cc");
		rc = !!rc;
	} else {
		counter = PTRS_PER_PTE;
		asm volatile(
			"0:	rrbe	0,%2\n"
			"	la	%2,0(%3,%2)\n"
			"	brc	12,1f\n"
			"	lhi	%0,1\n"
			"1:	brct	%1,0b\n"
			: "+d" (rc), "+d" (counter), "+a" (pmd_addr)
			: "a" (4096UL) : "cc");
	}
	return rc;
}

#define __HAVE_ARCH_PMDP_GET_AND_CLEAR
static inline pmd_t pmdp_get_and_clear(struct mm_struct *mm,
				       unsigned long address, pmd_t *pmdp)
{
	pmd_t pmd = *pmdp;

	__pmd_idte(address, pmdp);
	pmd_clear(pmdp);
	return pmd;
}

#define __HAVE_ARCH_PMDP_CLEAR_FLUSH
static inline pmd_t pmdp_clear_flush(struct vm_area_struct *vma,
				     unsigned long address, pmd_t *pmdp)
{
	return pmdp_get_and_clear(vma->vm_mm, address, pmdp);
}

#define __HAVE_ARCH_PMDP_INVALIDATE
static inline void pmdp_invalidate(struct vm_area_struct *vma,
				   unsigned long address, pmd_t *pmdp)
{
	__pmd_idte(address, pmdp);
}

#define __HAVE_ARCH_PMDP_SET_WRPROTECT
static inline void pmdp_set_wrprotect(struct mm_struct *mm,
				      unsigned long address, pmd_t *pmdp)
{
	pmd_t pmd = *pmdp;

	if (pmd_write(pmd)) {
		__pmd_idte(address, pmdp);
		set_pmd_at(mm, address, pmdp, pmd_wrprotect(pmd));
	}
}

#define pfn_pmd(pfn, pgprot)	mk_pmd_phys(__pa((pfn) << PAGE_SHIFT), (pgprot))
#define mk_pmd(page, pgprot)	pfn_pmd(page_to_pfn(page), (pgprot))

static inline int pmd_trans_huge(pmd_t pmd)
{
	return pmd_val(pmd) & _SEGMENT_ENTRY_LARGE;
}

static inline int has_transparent_hugepage(void)
{
	return MACHINE_HAS_HPAGE ? 1 : 0;
}

static inline unsigned long pmd_pfn(pmd_t pmd)
{
	return pmd_val(pmd) >> PAGE_SHIFT;
}
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

/*
 * 31 bit swap entry format:
 * A page-table entry has some bits we have to treat in a special way.
 * Bits 0, 20 and bit 23 have to be zero, otherwise an specification
 * exception will occur instead of a page translation exception. The
 * specifiation exception has the bad habit not to store necessary
 * information in the lowcore.
 * Bits 21, 22, 30 and 31 are used to indicate the page type.
 * A swap pte is indicated by bit pattern (pte & 0x603) == 0x402
 * This leaves the bits 1-19 and bits 24-29 to store type and offset.
 * We use the 5 bits from 25-29 for the type and the 20 bits from 1-19
 * plus 24 for the offset.
 * 0|     offset        |0110|o|type |00|
 * 0 0000000001111111111 2222 2 22222 33
 * 0 1234567890123456789 0123 4 56789 01
 *
 * 64 bit swap entry format:
 * A page-table entry has some bits we have to treat in a special way.
 * Bits 52 and bit 55 have to be zero, otherwise an specification
 * exception will occur instead of a page translation exception. The
 * specifiation exception has the bad habit not to store necessary
 * information in the lowcore.
 * Bits 53, 54, 62 and 63 are used to indicate the page type.
 * A swap pte is indicated by bit pattern (pte & 0x603) == 0x402
 * This leaves the bits 0-51 and bits 56-61 to store type and offset.
 * We use the 5 bits from 57-61 for the type and the 53 bits from 0-51
 * plus 56 for the offset.
 * |                      offset                        |0110|o|type |00|
 *  0000000000111111111122222222223333333333444444444455 5555 5 55566 66
 *  0123456789012345678901234567890123456789012345678901 2345 6 78901 23
 */
#ifndef CONFIG_64BIT
#define __SWP_OFFSET_MASK (~0UL >> 12)
#else
#define __SWP_OFFSET_MASK (~0UL >> 11)
#endif
static inline pte_t mk_swap_pte(unsigned long type, unsigned long offset)
{
	pte_t pte;
	offset &= __SWP_OFFSET_MASK;
	pte_val(pte) = _PAGE_INVALID | _PAGE_TYPE | ((type & 0x1f) << 2) |
		((offset & 1UL) << 7) | ((offset & ~1UL) << 11);
	return pte;
}

#define __swp_type(entry)	(((entry).val >> 2) & 0x1f)
#define __swp_offset(entry)	(((entry).val >> 11) | (((entry).val >> 7) & 1))
#define __swp_entry(type,offset) ((swp_entry_t) { pte_val(mk_swap_pte((type),(offset))) })

#define __pte_to_swp_entry(pte)	((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(x)	((pte_t) { (x).val })

#ifndef CONFIG_64BIT
# define PTE_FILE_MAX_BITS	26
#else /* CONFIG_64BIT */
# define PTE_FILE_MAX_BITS	59
#endif /* CONFIG_64BIT */

#define pte_to_pgoff(__pte) \
	((((__pte).pte >> 12) << 7) + (((__pte).pte >> 1) & 0x7f))

#define pgoff_to_pte(__off) \
	((pte_t) { ((((__off) & 0x7f) << 1) + (((__off) >> 7) << 12)) \
		   | _PAGE_INVALID | _PAGE_PROTECT })

#endif /* !__ASSEMBLY__ */

#define kern_addr_valid(addr)   (1)

extern int vmem_add_mapping(unsigned long start, unsigned long size);
extern int vmem_remove_mapping(unsigned long start, unsigned long size);
extern int s390_enable_sie(void);

/*
 * No page table caches to initialise
 */
static inline void pgtable_cache_init(void) { }
static inline void check_pgt_cache(void) { }

#include <asm-generic/pgtable.h>

#endif /* _S390_PAGE_H */
