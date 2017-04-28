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
 * The Linux memory management assumes a three-level page table setup.
 * For s390 64 bit we use up to four of the five levels the hardware
 * provides (region first tables are not used).
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
#include <linux/radix-tree.h>
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
#define PMD_SHIFT	20
#define PUD_SHIFT	31
#define PGDIR_SHIFT	42

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
#define PTRS_PER_PMD	2048
#define PTRS_PER_PUD	2048
#define PTRS_PER_PGD	2048

#define FIRST_USER_ADDRESS  0UL

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
 * The vmalloc and module area will always be on the topmost area of the
 * kernel mapping. We reserve 128GB (64bit) for vmalloc and modules.
 * On 64 bit kernels we have a 2GB area at the top of the vmalloc area where
 * modules will reside. That makes sure that inter module branches always
 * happen without trampolines and in addition the placement within a 2GB frame
 * is branch prediction unit friendly.
 */
extern unsigned long VMALLOC_START;
extern unsigned long VMALLOC_END;
extern struct page *vmemmap;

#define VMEM_MAX_PHYS ((unsigned long) vmemmap)

extern unsigned long MODULES_VADDR;
extern unsigned long MODULES_END;
#define MODULES_VADDR	MODULES_VADDR
#define MODULES_END	MODULES_END
#define MODULES_LEN	(1UL << 31)

static inline int is_module_addr(void *addr)
{
	BUILD_BUG_ON(MODULES_LEN > (1UL << 31));
	if (addr < (void *)MODULES_VADDR)
		return 0;
	if (addr > (void *)MODULES_END)
		return 0;
	return 1;
}

/*
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
#define _PAGE_PROTECT	0x200		/* HW read-only bit  */
#define _PAGE_INVALID	0x400		/* HW invalid bit    */
#define _PAGE_LARGE	0x800		/* Bit to mark a large pte */

/* Software bits in the page table entry */
#define _PAGE_PRESENT	0x001		/* SW pte present bit */
#define _PAGE_YOUNG	0x004		/* SW pte young bit */
#define _PAGE_DIRTY	0x008		/* SW pte dirty bit */
#define _PAGE_READ	0x010		/* SW pte read bit */
#define _PAGE_WRITE	0x020		/* SW pte write bit */
#define _PAGE_SPECIAL	0x040		/* SW associated with special page */
#define _PAGE_UNUSED	0x080		/* SW bit for pgste usage state */
#define __HAVE_ARCH_PTE_SPECIAL

#ifdef CONFIG_MEM_SOFT_DIRTY
#define _PAGE_SOFT_DIRTY 0x002		/* SW pte soft dirty bit */
#else
#define _PAGE_SOFT_DIRTY 0x000
#endif

/* Set of bits not changed in pte_modify */
#define _PAGE_CHG_MASK		(PAGE_MASK | _PAGE_SPECIAL | _PAGE_DIRTY | \
				 _PAGE_YOUNG | _PAGE_SOFT_DIRTY)

/*
 * handle_pte_fault uses pte_present and pte_none to find out the pte type
 * WITHOUT holding the page table lock. The _PAGE_PRESENT bit is used to
 * distinguish present from not-present ptes. It is changed only with the page
 * table lock held.
 *
 * The following table gives the different possible bit combinations for
 * the pte hardware and software bits in the last 12 bits of a pte
 * (. unassigned bit, x don't care, t swap type):
 *
 *				842100000000
 *				000084210000
 *				000000008421
 *				.IR.uswrdy.p
 * empty			.10.00000000
 * swap				.11..ttttt.0
 * prot-none, clean, old	.11.xx0000.1
 * prot-none, clean, young	.11.xx0001.1
 * prot-none, dirty, old	.10.xx0010.1
 * prot-none, dirty, young	.10.xx0011.1
 * read-only, clean, old	.11.xx0100.1
 * read-only, clean, young	.01.xx0101.1
 * read-only, dirty, old	.11.xx0110.1
 * read-only, dirty, young	.01.xx0111.1
 * read-write, clean, old	.11.xx1100.1
 * read-write, clean, young	.01.xx1101.1
 * read-write, dirty, old	.10.xx1110.1
 * read-write, dirty, young	.00.xx1111.1
 * HW-bits: R read-only, I invalid
 * SW-bits: p present, y young, d dirty, r read, w write, s special,
 *	    u unused, l large
 *
 * pte_none    is true for the bit pattern .10.00000000, pte == 0x400
 * pte_swap    is true for the bit pattern .11..ooooo.0, (pte & 0x201) == 0x200
 * pte_present is true for the bit pattern .xx.xxxxxx.1, (pte & 0x001) == 0x001
 */

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

/* Bits in the segment table entry */
#define _SEGMENT_ENTRY_BITS	0xfffffffffffffe33UL
#define _SEGMENT_ENTRY_BITS_LARGE 0xfffffffffff0ff33UL
#define _SEGMENT_ENTRY_ORIGIN_LARGE ~0xfffffUL /* large page address	    */
#define _SEGMENT_ENTRY_ORIGIN	~0x7ffUL/* segment table origin		    */
#define _SEGMENT_ENTRY_PROTECT	0x200	/* page protection bit		    */
#define _SEGMENT_ENTRY_INVALID	0x20	/* invalid segment table entry	    */

#define _SEGMENT_ENTRY		(0)
#define _SEGMENT_ENTRY_EMPTY	(_SEGMENT_ENTRY_INVALID)

#define _SEGMENT_ENTRY_DIRTY	0x2000	/* SW segment dirty bit */
#define _SEGMENT_ENTRY_YOUNG	0x1000	/* SW segment young bit */
#define _SEGMENT_ENTRY_SPLIT	0x0800	/* THP splitting bit */
#define _SEGMENT_ENTRY_LARGE	0x0400	/* STE-format control, large page */
#define _SEGMENT_ENTRY_READ	0x0002	/* SW segment read bit */
#define _SEGMENT_ENTRY_WRITE	0x0001	/* SW segment write bit */

#ifdef CONFIG_MEM_SOFT_DIRTY
#define _SEGMENT_ENTRY_SOFT_DIRTY 0x4000 /* SW segment soft dirty bit */
#else
#define _SEGMENT_ENTRY_SOFT_DIRTY 0x0000 /* SW segment soft dirty bit */
#endif

/*
 * Segment table entry encoding (R = read-only, I = invalid, y = young bit):
 *				dy..R...I...wr
 * prot-none, clean, old	00..1...1...00
 * prot-none, clean, young	01..1...1...00
 * prot-none, dirty, old	10..1...1...00
 * prot-none, dirty, young	11..1...1...00
 * read-only, clean, old	00..1...1...01
 * read-only, clean, young	01..1...0...01
 * read-only, dirty, old	10..1...1...01
 * read-only, dirty, young	11..1...0...01
 * read-write, clean, old	00..1...1...11
 * read-write, clean, young	01..1...0...11
 * read-write, dirty, old	10..0...1...11
 * read-write, dirty, young	11..0...0...11
 * The segment table origin is used to distinguish empty (origin==0) from
 * read-write, old segment table entries (origin!=0)
 * HW-bits: R read-only, I invalid
 * SW-bits: y young, d dirty, r read, w write
 */

#define _SEGMENT_ENTRY_SPLIT_BIT 11	/* THP splitting bit number */

/* Page status table bits for virtualization */
#define PGSTE_ACC_BITS	0xf000000000000000UL
#define PGSTE_FP_BIT	0x0800000000000000UL
#define PGSTE_PCL_BIT	0x0080000000000000UL
#define PGSTE_HR_BIT	0x0040000000000000UL
#define PGSTE_HC_BIT	0x0020000000000000UL
#define PGSTE_GR_BIT	0x0004000000000000UL
#define PGSTE_GC_BIT	0x0002000000000000UL
#define PGSTE_UC_BIT	0x0000800000000000UL	/* user dirty (migration) */
#define PGSTE_IN_BIT	0x0000400000000000UL	/* IPTE notify bit */

/* Guest Page State used for virtualization */
#define _PGSTE_GPS_ZERO		0x0000000080000000UL
#define _PGSTE_GPS_USAGE_MASK	0x0000000003000000UL
#define _PGSTE_GPS_USAGE_STABLE 0x0000000000000000UL
#define _PGSTE_GPS_USAGE_UNUSED 0x0000000001000000UL

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
#define PAGE_READ	__pgprot(_PAGE_PRESENT | _PAGE_READ | \
				 _PAGE_INVALID | _PAGE_PROTECT)
#define PAGE_WRITE	__pgprot(_PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE | \
				 _PAGE_INVALID | _PAGE_PROTECT)

#define PAGE_SHARED	__pgprot(_PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE | \
				 _PAGE_YOUNG | _PAGE_DIRTY)
#define PAGE_KERNEL	__pgprot(_PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE | \
				 _PAGE_YOUNG | _PAGE_DIRTY)
#define PAGE_KERNEL_RO	__pgprot(_PAGE_PRESENT | _PAGE_READ | _PAGE_YOUNG | \
				 _PAGE_PROTECT)

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
#define SEGMENT_READ	__pgprot(_SEGMENT_ENTRY_PROTECT | \
				 _SEGMENT_ENTRY_READ)
#define SEGMENT_WRITE	__pgprot(_SEGMENT_ENTRY_READ | \
				 _SEGMENT_ENTRY_WRITE)

static inline int mm_has_pgste(struct mm_struct *mm)
{
#ifdef CONFIG_PGSTE
	if (unlikely(mm->context.has_pgste))
		return 1;
#endif
	return 0;
}

static inline int mm_alloc_pgste(struct mm_struct *mm)
{
#ifdef CONFIG_PGSTE
	if (unlikely(mm->context.alloc_pgste))
		return 1;
#endif
	return 0;
}

/*
 * In the case that a guest uses storage keys
 * faults should no longer be backed by zero pages
 */
#define mm_forbids_zeropage mm_use_skey
static inline int mm_use_skey(struct mm_struct *mm)
{
#ifdef CONFIG_PGSTE
	if (mm->context.use_skey)
		return 1;
#endif
	return 0;
}

/*
 * pgd/pmd/pte query functions
 */
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
	return (pmd_val(pmd) & _SEGMENT_ENTRY_LARGE) != 0;
}

static inline unsigned long pmd_pfn(pmd_t pmd)
{
	unsigned long origin_mask;

	origin_mask = _SEGMENT_ENTRY_ORIGIN;
	if (pmd_large(pmd))
		origin_mask = _SEGMENT_ENTRY_ORIGIN_LARGE;
	return (pmd_val(pmd) & origin_mask) >> PAGE_SHIFT;
}

static inline int pmd_bad(pmd_t pmd)
{
	if (pmd_large(pmd))
		return (pmd_val(pmd) & ~_SEGMENT_ENTRY_BITS_LARGE) != 0;
	return (pmd_val(pmd) & ~_SEGMENT_ENTRY_BITS) != 0;
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
	return (pmd_val(pmd) & _SEGMENT_ENTRY_WRITE) != 0;
}

static inline int pmd_dirty(pmd_t pmd)
{
	int dirty = 1;
	if (pmd_large(pmd))
		dirty = (pmd_val(pmd) & _SEGMENT_ENTRY_DIRTY) != 0;
	return dirty;
}

static inline int pmd_young(pmd_t pmd)
{
	int young = 1;
	if (pmd_large(pmd))
		young = (pmd_val(pmd) & _SEGMENT_ENTRY_YOUNG) != 0;
	return young;
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

static inline int pte_swap(pte_t pte)
{
	/* Bit pattern: (pte & 0x201) == 0x200 */
	return (pte_val(pte) & (_PAGE_PROTECT | _PAGE_PRESENT))
		== _PAGE_PROTECT;
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

#ifdef CONFIG_NUMA_BALANCING
static inline int pte_protnone(pte_t pte)
{
	return pte_present(pte) && !(pte_val(pte) & _PAGE_READ);
}

static inline int pmd_protnone(pmd_t pmd)
{
	/* pmd_large(pmd) implies pmd_present(pmd) */
	return pmd_large(pmd) && !(pmd_val(pmd) & _SEGMENT_ENTRY_READ);
}
#endif

static inline int pte_soft_dirty(pte_t pte)
{
	return pte_val(pte) & _PAGE_SOFT_DIRTY;
}
#define pte_swp_soft_dirty pte_soft_dirty

static inline pte_t pte_mksoft_dirty(pte_t pte)
{
	pte_val(pte) |= _PAGE_SOFT_DIRTY;
	return pte;
}
#define pte_swp_mksoft_dirty pte_mksoft_dirty

static inline pte_t pte_clear_soft_dirty(pte_t pte)
{
	pte_val(pte) &= ~_PAGE_SOFT_DIRTY;
	return pte;
}
#define pte_swp_clear_soft_dirty pte_clear_soft_dirty

static inline int pmd_soft_dirty(pmd_t pmd)
{
	return pmd_val(pmd) & _SEGMENT_ENTRY_SOFT_DIRTY;
}

static inline pmd_t pmd_mksoft_dirty(pmd_t pmd)
{
	pmd_val(pmd) |= _SEGMENT_ENTRY_SOFT_DIRTY;
	return pmd;
}

static inline pmd_t pmd_clear_soft_dirty(pmd_t pmd)
{
	pmd_val(pmd) &= ~_SEGMENT_ENTRY_SOFT_DIRTY;
	return pmd;
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

static inline pgste_t pgste_update_all(pte_t *ptep, pgste_t pgste,
				       struct mm_struct *mm)
{
#ifdef CONFIG_PGSTE
	unsigned long address, bits, skey;

	if (!mm_use_skey(mm) || pte_val(*ptep) & _PAGE_INVALID)
		return pgste;
	address = pte_val(*ptep) & PAGE_MASK;
	skey = (unsigned long) page_get_storage_key(address);
	bits = skey & (_PAGE_CHANGED | _PAGE_REFERENCED);
	/* Transfer page changed & referenced bit to guest bits in pgste */
	pgste_val(pgste) |= bits << 48;		/* GR bit & GC bit */
	/* Copy page access key and fetch protection bit to pgste */
	pgste_val(pgste) &= ~(PGSTE_ACC_BITS | PGSTE_FP_BIT);
	pgste_val(pgste) |= (skey & (_PAGE_ACC_BITS | _PAGE_FP_BIT)) << 56;
#endif
	return pgste;

}

static inline void pgste_set_key(pte_t *ptep, pgste_t pgste, pte_t entry,
				 struct mm_struct *mm)
{
#ifdef CONFIG_PGSTE
	unsigned long address;
	unsigned long nkey;

	if (!mm_use_skey(mm) || pte_val(entry) & _PAGE_INVALID)
		return;
	VM_BUG_ON(!(pte_val(*ptep) & _PAGE_INVALID));
	address = pte_val(entry) & PAGE_MASK;
	/*
	 * Set page access key and fetch protection bit from pgste.
	 * The guest C/R information is still in the PGSTE, set real
	 * key C/R to 0.
	 */
	nkey = (pgste_val(pgste) & (PGSTE_ACC_BITS | PGSTE_FP_BIT)) >> 56;
	nkey |= (pgste_val(pgste) & (PGSTE_GR_BIT | PGSTE_GC_BIT)) >> 48;
	page_set_storage_key(address, nkey, 0);
#endif
}

static inline pgste_t pgste_set_pte(pte_t *ptep, pgste_t pgste, pte_t entry)
{
	if ((pte_val(entry) & _PAGE_PRESENT) &&
	    (pte_val(entry) & _PAGE_WRITE) &&
	    !(pte_val(entry) & _PAGE_INVALID)) {
		if (!MACHINE_HAS_ESOP) {
			/*
			 * Without enhanced suppression-on-protection force
			 * the dirty bit on for all writable ptes.
			 */
			pte_val(entry) |= _PAGE_DIRTY;
			pte_val(entry) &= ~_PAGE_PROTECT;
		}
		if (!(pte_val(entry) & _PAGE_PROTECT))
			/* This pte allows write access, set user-dirty */
			pgste_val(pgste) |= PGSTE_UC_BIT;
	}
	*ptep = entry;
	return pgste;
}

/**
 * struct gmap_struct - guest address space
 * @crst_list: list of all crst tables used in the guest address space
 * @mm: pointer to the parent mm_struct
 * @guest_to_host: radix tree with guest to host address translation
 * @host_to_guest: radix tree with pointer to segment table entries
 * @guest_table_lock: spinlock to protect all entries in the guest page table
 * @table: pointer to the page directory
 * @asce: address space control element for gmap page table
 * @pfault_enabled: defines if pfaults are applicable for the guest
 */
struct gmap {
	struct list_head list;
	struct list_head crst_list;
	struct mm_struct *mm;
	struct radix_tree_root guest_to_host;
	struct radix_tree_root host_to_guest;
	spinlock_t guest_table_lock;
	unsigned long *table;
	unsigned long asce;
	unsigned long asce_end;
	void *private;
	bool pfault_enabled;
};

/**
 * struct gmap_notifier - notify function block for page invalidation
 * @notifier_call: address of callback function
 */
struct gmap_notifier {
	struct list_head list;
	void (*notifier_call)(struct gmap *gmap, unsigned long gaddr);
};

struct gmap *gmap_alloc(struct mm_struct *mm, unsigned long limit);
void gmap_free(struct gmap *gmap);
void gmap_enable(struct gmap *gmap);
void gmap_disable(struct gmap *gmap);
int gmap_map_segment(struct gmap *gmap, unsigned long from,
		     unsigned long to, unsigned long len);
int gmap_unmap_segment(struct gmap *gmap, unsigned long to, unsigned long len);
unsigned long __gmap_translate(struct gmap *, unsigned long gaddr);
unsigned long gmap_translate(struct gmap *, unsigned long gaddr);
int __gmap_link(struct gmap *gmap, unsigned long gaddr, unsigned long vmaddr);
int gmap_fault(struct gmap *, unsigned long gaddr, unsigned int fault_flags);
void gmap_discard(struct gmap *, unsigned long from, unsigned long to);
void __gmap_zap(struct gmap *, unsigned long gaddr);
bool gmap_test_and_clear_dirty(unsigned long address, struct gmap *);


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

	if (pte_present(entry))
		pte_val(entry) &= ~_PAGE_UNUSED;
	if (mm_has_pgste(mm)) {
		pgste = pgste_get_lock(ptep);
		pgste_val(pgste) &= ~_PGSTE_GPS_ZERO;
		pgste_set_key(ptep, pgste, entry, mm);
		pgste = pgste_set_pte(ptep, pgste, entry);
		pgste_set_unlock(ptep, pgste);
	} else {
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
	return (pte_val(pte) & _PAGE_YOUNG) != 0;
}

#define __HAVE_ARCH_PTE_UNUSED
static inline int pte_unused(pte_t pte)
{
	return pte_val(pte) & _PAGE_UNUSED;
}

/*
 * pgd/pmd/pte modification functions
 */

static inline void pgd_clear(pgd_t *pgd)
{
	if ((pgd_val(*pgd) & _REGION_ENTRY_TYPE_MASK) == _REGION_ENTRY_TYPE_R2)
		pgd_val(*pgd) = _REGION2_ENTRY_EMPTY;
}

static inline void pud_clear(pud_t *pud)
{
	if ((pud_val(*pud) & _REGION_ENTRY_TYPE_MASK) == _REGION_ENTRY_TYPE_R3)
		pud_val(*pud) = _REGION3_ENTRY_EMPTY;
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
	/*
	 * newprot for PAGE_NONE, PAGE_READ and PAGE_WRITE has the
	 * invalid bit set, clear it again for readable, young pages
	 */
	if ((pte_val(pte) & _PAGE_YOUNG) && (pte_val(pte) & _PAGE_READ))
		pte_val(pte) &= ~_PAGE_INVALID;
	/*
	 * newprot for PAGE_READ and PAGE_WRITE has the page protection
	 * bit set, clear it again for writable, dirty pages
	 */
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
	pte_val(pte) |= _PAGE_DIRTY | _PAGE_SOFT_DIRTY;
	if (pte_val(pte) & _PAGE_WRITE)
		pte_val(pte) &= ~_PAGE_PROTECT;
	return pte;
}

static inline pte_t pte_mkold(pte_t pte)
{
	pte_val(pte) &= ~_PAGE_YOUNG;
	pte_val(pte) |= _PAGE_INVALID;
	return pte;
}

static inline pte_t pte_mkyoung(pte_t pte)
{
	pte_val(pte) |= _PAGE_YOUNG;
	if (pte_val(pte) & _PAGE_READ)
		pte_val(pte) &= ~_PAGE_INVALID;
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

static inline void __ptep_ipte(unsigned long address, pte_t *ptep)
{
	unsigned long pto = (unsigned long) ptep;

	/* Invalidation + global TLB flush for the pte */
	asm volatile(
		"	ipte	%2,%3"
		: "=m" (*ptep) : "m" (*ptep), "a" (pto), "a" (address));
}

static inline void __ptep_ipte_local(unsigned long address, pte_t *ptep)
{
	unsigned long pto = (unsigned long) ptep;

	/* Invalidation + local TLB flush for the pte */
	asm volatile(
		"	.insn rrf,0xb2210000,%2,%3,0,1"
		: "=m" (*ptep) : "m" (*ptep), "a" (pto), "a" (address));
}

static inline void __ptep_ipte_range(unsigned long address, int nr, pte_t *ptep)
{
	unsigned long pto = (unsigned long) ptep;

	/* Invalidate a range of ptes + global TLB flush of the ptes */
	do {
		asm volatile(
			"	.insn rrf,0xb2210000,%2,%0,%1,0"
			: "+a" (address), "+a" (nr) : "a" (pto) : "memory");
	} while (nr != 255);
}

static inline void ptep_flush_direct(struct mm_struct *mm,
				     unsigned long address, pte_t *ptep)
{
	int active, count;

	if (pte_val(*ptep) & _PAGE_INVALID)
		return;
	active = (mm == current->active_mm) ? 1 : 0;
	count = atomic_add_return(0x10000, &mm->context.attach_count);
	if (MACHINE_HAS_TLB_LC && (count & 0xffff) <= active &&
	    cpumask_equal(mm_cpumask(mm), cpumask_of(smp_processor_id())))
		__ptep_ipte_local(address, ptep);
	else
		__ptep_ipte(address, ptep);
	atomic_sub(0x10000, &mm->context.attach_count);
}

static inline void ptep_flush_lazy(struct mm_struct *mm,
				   unsigned long address, pte_t *ptep)
{
	int active, count;

	if (pte_val(*ptep) & _PAGE_INVALID)
		return;
	active = (mm == current->active_mm) ? 1 : 0;
	count = atomic_add_return(0x10000, &mm->context.attach_count);
	if ((count & 0xffff) <= active) {
		pte_val(*ptep) |= _PAGE_INVALID;
		mm->context.flush_mm = 1;
	} else
		__ptep_ipte(address, ptep);
	atomic_sub(0x10000, &mm->context.attach_count);
}

/*
 * Get (and clear) the user dirty bit for a pte.
 */
static inline int ptep_test_and_clear_user_dirty(struct mm_struct *mm,
						 unsigned long addr,
						 pte_t *ptep)
{
	pgste_t pgste;
	pte_t pte;
	int dirty;

	if (!mm_has_pgste(mm))
		return 0;
	pgste = pgste_get_lock(ptep);
	dirty = !!(pgste_val(pgste) & PGSTE_UC_BIT);
	pgste_val(pgste) &= ~PGSTE_UC_BIT;
	pte = *ptep;
	if (dirty && (pte_val(pte) & _PAGE_PRESENT)) {
		pgste = pgste_ipte_notify(mm, addr, ptep, pgste);
		__ptep_ipte(addr, ptep);
		if (MACHINE_HAS_ESOP || !(pte_val(pte) & _PAGE_WRITE))
			pte_val(pte) |= _PAGE_PROTECT;
		else
			pte_val(pte) |= _PAGE_INVALID;
		*ptep = pte;
	}
	pgste_set_unlock(ptep, pgste);
	return dirty;
}

#define __HAVE_ARCH_PTEP_TEST_AND_CLEAR_YOUNG
static inline int ptep_test_and_clear_young(struct vm_area_struct *vma,
					    unsigned long addr, pte_t *ptep)
{
	pgste_t pgste;
	pte_t pte, oldpte;
	int young;

	if (mm_has_pgste(vma->vm_mm)) {
		pgste = pgste_get_lock(ptep);
		pgste = pgste_ipte_notify(vma->vm_mm, addr, ptep, pgste);
	}

	oldpte = pte = *ptep;
	ptep_flush_direct(vma->vm_mm, addr, ptep);
	young = pte_young(pte);
	pte = pte_mkold(pte);

	if (mm_has_pgste(vma->vm_mm)) {
		pgste = pgste_update_all(&oldpte, pgste, vma->vm_mm);
		pgste = pgste_set_pte(ptep, pgste, pte);
		pgste_set_unlock(ptep, pgste);
	} else
		*ptep = pte;

	return young;
}

#define __HAVE_ARCH_PTEP_CLEAR_YOUNG_FLUSH
static inline int ptep_clear_flush_young(struct vm_area_struct *vma,
					 unsigned long address, pte_t *ptep)
{
	return ptep_test_and_clear_young(vma, address, ptep);
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
		pgste = pgste_update_all(&pte, pgste, mm);
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
		pgste = pgste_update_all(&pte, pgste, mm);
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
		pgste_set_key(ptep, pgste, pte, mm);
		pgste = pgste_set_pte(ptep, pgste, pte);
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
	ptep_flush_direct(vma->vm_mm, address, ptep);
	pte_val(*ptep) = _PAGE_INVALID;

	if (mm_has_pgste(vma->vm_mm)) {
		if ((pgste_val(pgste) & _PGSTE_GPS_USAGE_MASK) ==
		    _PGSTE_GPS_USAGE_UNUSED)
			pte_val(pte) |= _PAGE_UNUSED;
		pgste = pgste_update_all(&pte, pgste, vma->vm_mm);
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
		pgste = pgste_update_all(&pte, pgste, mm);
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
			pgste = pgste_set_pte(ptep, pgste, pte);
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
	pte_t oldpte;

	oldpte = *ptep;
	if (pte_same(oldpte, entry))
		return 0;
	if (mm_has_pgste(vma->vm_mm)) {
		pgste = pgste_get_lock(ptep);
		pgste = pgste_ipte_notify(vma->vm_mm, address, ptep, pgste);
	}

	ptep_flush_direct(vma->vm_mm, address, ptep);

	if (mm_has_pgste(vma->vm_mm)) {
		if (pte_val(oldpte) & _PAGE_INVALID)
			pgste_set_key(ptep, pgste, entry, vma->vm_mm);
		pgste = pgste_set_pte(ptep, pgste, entry);
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
	return pte_mkyoung(__pte);
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

#define pfn_pte(pfn,pgprot) mk_pte_phys(__pa((pfn) << PAGE_SHIFT),(pgprot))
#define pte_pfn(x) (pte_val(x) >> PAGE_SHIFT)
#define pte_page(x) pfn_to_page(pte_pfn(x))

#define pmd_page(pmd) pfn_to_page(pmd_pfn(pmd))

/* Find an entry in the lowest level page table.. */
#define pte_offset(pmd, addr) ((pte_t *) pmd_deref(*(pmd)) + pte_index(addr))
#define pte_offset_kernel(pmd, address) pte_offset(pmd,address)
#define pte_offset_map(pmd, address) pte_offset_kernel(pmd, address)
#define pte_unmap(pte) do { } while (0)

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

static inline pmd_t pmd_wrprotect(pmd_t pmd)
{
	pmd_val(pmd) &= ~_SEGMENT_ENTRY_WRITE;
	pmd_val(pmd) |= _SEGMENT_ENTRY_PROTECT;
	return pmd;
}

static inline pmd_t pmd_mkwrite(pmd_t pmd)
{
	pmd_val(pmd) |= _SEGMENT_ENTRY_WRITE;
	if (pmd_large(pmd) && !(pmd_val(pmd) & _SEGMENT_ENTRY_DIRTY))
		return pmd;
	pmd_val(pmd) &= ~_SEGMENT_ENTRY_PROTECT;
	return pmd;
}

static inline pmd_t pmd_mkclean(pmd_t pmd)
{
	if (pmd_large(pmd)) {
		pmd_val(pmd) &= ~_SEGMENT_ENTRY_DIRTY;
		pmd_val(pmd) |= _SEGMENT_ENTRY_PROTECT;
	}
	return pmd;
}

static inline pmd_t pmd_mkdirty(pmd_t pmd)
{
	if (pmd_large(pmd)) {
		pmd_val(pmd) |= _SEGMENT_ENTRY_DIRTY |
				_SEGMENT_ENTRY_SOFT_DIRTY;
		if (pmd_val(pmd) & _SEGMENT_ENTRY_WRITE)
			pmd_val(pmd) &= ~_SEGMENT_ENTRY_PROTECT;
	}
	return pmd;
}

static inline pmd_t pmd_mkyoung(pmd_t pmd)
{
	if (pmd_large(pmd)) {
		pmd_val(pmd) |= _SEGMENT_ENTRY_YOUNG;
		if (pmd_val(pmd) & _SEGMENT_ENTRY_READ)
			pmd_val(pmd) &= ~_SEGMENT_ENTRY_INVALID;
	}
	return pmd;
}

static inline pmd_t pmd_mkold(pmd_t pmd)
{
	if (pmd_large(pmd)) {
		pmd_val(pmd) &= ~_SEGMENT_ENTRY_YOUNG;
		pmd_val(pmd) |= _SEGMENT_ENTRY_INVALID;
	}
	return pmd;
}

static inline pmd_t pmd_modify(pmd_t pmd, pgprot_t newprot)
{
	if (pmd_large(pmd)) {
		pmd_val(pmd) &= _SEGMENT_ENTRY_ORIGIN_LARGE |
			_SEGMENT_ENTRY_DIRTY | _SEGMENT_ENTRY_YOUNG |
			_SEGMENT_ENTRY_LARGE | _SEGMENT_ENTRY_SPLIT |
			_SEGMENT_ENTRY_SOFT_DIRTY;
		pmd_val(pmd) |= massage_pgprot_pmd(newprot);
		if (!(pmd_val(pmd) & _SEGMENT_ENTRY_DIRTY))
			pmd_val(pmd) |= _SEGMENT_ENTRY_PROTECT;
		if (!(pmd_val(pmd) & _SEGMENT_ENTRY_YOUNG))
			pmd_val(pmd) |= _SEGMENT_ENTRY_INVALID;
		return pmd;
	}
	pmd_val(pmd) &= _SEGMENT_ENTRY_ORIGIN;
	pmd_val(pmd) |= massage_pgprot_pmd(newprot);
	return pmd;
}

static inline pmd_t mk_pmd_phys(unsigned long physpage, pgprot_t pgprot)
{
	pmd_t __pmd;
	pmd_val(__pmd) = physpage + massage_pgprot_pmd(pgprot);
	return __pmd;
}

#endif /* CONFIG_TRANSPARENT_HUGEPAGE || CONFIG_HUGETLB_PAGE */

static inline void __pmdp_csp(pmd_t *pmdp)
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

static inline void __pmdp_idte(unsigned long address, pmd_t *pmdp)
{
	unsigned long sto;

	sto = (unsigned long) pmdp - pmd_index(address) * sizeof(pmd_t);
	asm volatile(
		"	.insn	rrf,0xb98e0000,%2,%3,0,0"
		: "=m" (*pmdp)
		: "m" (*pmdp), "a" (sto), "a" ((address & HPAGE_MASK))
		: "cc" );
}

static inline void __pmdp_idte_local(unsigned long address, pmd_t *pmdp)
{
	unsigned long sto;

	sto = (unsigned long) pmdp - pmd_index(address) * sizeof(pmd_t);
	asm volatile(
		"	.insn	rrf,0xb98e0000,%2,%3,0,1"
		: "=m" (*pmdp)
		: "m" (*pmdp), "a" (sto), "a" ((address & HPAGE_MASK))
		: "cc" );
}

static inline void pmdp_flush_direct(struct mm_struct *mm,
				     unsigned long address, pmd_t *pmdp)
{
	int active, count;

	if (pmd_val(*pmdp) & _SEGMENT_ENTRY_INVALID)
		return;
	if (!MACHINE_HAS_IDTE) {
		__pmdp_csp(pmdp);
		return;
	}
	active = (mm == current->active_mm) ? 1 : 0;
	count = atomic_add_return(0x10000, &mm->context.attach_count);
	if (MACHINE_HAS_TLB_LC && (count & 0xffff) <= active &&
	    cpumask_equal(mm_cpumask(mm), cpumask_of(smp_processor_id())))
		__pmdp_idte_local(address, pmdp);
	else
		__pmdp_idte(address, pmdp);
	atomic_sub(0x10000, &mm->context.attach_count);
}

static inline void pmdp_flush_lazy(struct mm_struct *mm,
				   unsigned long address, pmd_t *pmdp)
{
	int active, count;

	if (pmd_val(*pmdp) & _SEGMENT_ENTRY_INVALID)
		return;
	active = (mm == current->active_mm) ? 1 : 0;
	count = atomic_add_return(0x10000, &mm->context.attach_count);
	if ((count & 0xffff) <= active) {
		pmd_val(*pmdp) |= _SEGMENT_ENTRY_INVALID;
		mm->context.flush_mm = 1;
	} else if (MACHINE_HAS_IDTE)
		__pmdp_idte(address, pmdp);
	else
		__pmdp_csp(pmdp);
	atomic_sub(0x10000, &mm->context.attach_count);
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE

#define __HAVE_ARCH_PGTABLE_DEPOSIT
extern void pgtable_trans_huge_deposit(struct mm_struct *mm, pmd_t *pmdp,
				       pgtable_t pgtable);

#define __HAVE_ARCH_PGTABLE_WITHDRAW
extern pgtable_t pgtable_trans_huge_withdraw(struct mm_struct *mm, pmd_t *pmdp);

static inline int pmd_trans_splitting(pmd_t pmd)
{
	return (pmd_val(pmd) & _SEGMENT_ENTRY_LARGE) &&
		(pmd_val(pmd) & _SEGMENT_ENTRY_SPLIT);
}

static inline void set_pmd_at(struct mm_struct *mm, unsigned long addr,
			      pmd_t *pmdp, pmd_t entry)
{
	*pmdp = entry;
}

static inline pmd_t pmd_mkhuge(pmd_t pmd)
{
	pmd_val(pmd) |= _SEGMENT_ENTRY_LARGE;
	pmd_val(pmd) |= _SEGMENT_ENTRY_YOUNG;
	pmd_val(pmd) |= _SEGMENT_ENTRY_PROTECT;
	return pmd;
}

#define __HAVE_ARCH_PMDP_TEST_AND_CLEAR_YOUNG
static inline int pmdp_test_and_clear_young(struct vm_area_struct *vma,
					    unsigned long address, pmd_t *pmdp)
{
	pmd_t pmd;

	pmd = *pmdp;
	pmdp_flush_direct(vma->vm_mm, address, pmdp);
	*pmdp = pmd_mkold(pmd);
	return pmd_young(pmd);
}

#define __HAVE_ARCH_PMDP_HUGE_GET_AND_CLEAR
static inline pmd_t pmdp_huge_get_and_clear(struct mm_struct *mm,
					    unsigned long address, pmd_t *pmdp)
{
	pmd_t pmd = *pmdp;

	pmdp_flush_direct(mm, address, pmdp);
	pmd_clear(pmdp);
	return pmd;
}

#define __HAVE_ARCH_PMDP_HUGE_GET_AND_CLEAR_FULL
static inline pmd_t pmdp_huge_get_and_clear_full(struct mm_struct *mm,
						 unsigned long address,
						 pmd_t *pmdp, int full)
{
	pmd_t pmd = *pmdp;

	if (!full)
		pmdp_flush_lazy(mm, address, pmdp);
	pmd_clear(pmdp);
	return pmd;
}

#define __HAVE_ARCH_PMDP_HUGE_CLEAR_FLUSH
static inline pmd_t pmdp_huge_clear_flush(struct vm_area_struct *vma,
					  unsigned long address, pmd_t *pmdp)
{
	return pmdp_huge_get_and_clear(vma->vm_mm, address, pmdp);
}

#define __HAVE_ARCH_PMDP_INVALIDATE
static inline void pmdp_invalidate(struct vm_area_struct *vma,
				   unsigned long address, pmd_t *pmdp)
{
	pmdp_flush_direct(vma->vm_mm, address, pmdp);
}

#define __HAVE_ARCH_PMDP_SET_WRPROTECT
static inline void pmdp_set_wrprotect(struct mm_struct *mm,
				      unsigned long address, pmd_t *pmdp)
{
	pmd_t pmd = *pmdp;

	if (pmd_write(pmd)) {
		pmdp_flush_direct(mm, address, pmdp);
		set_pmd_at(mm, address, pmdp, pmd_wrprotect(pmd));
	}
}

static inline pmd_t pmdp_collapse_flush(struct vm_area_struct *vma,
					unsigned long address,
					pmd_t *pmdp)
{
	return pmdp_huge_get_and_clear(vma->vm_mm, address, pmdp);
}
#define pmdp_collapse_flush pmdp_collapse_flush

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
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

/*
 * 64 bit swap entry format:
 * A page-table entry has some bits we have to treat in a special way.
 * Bits 52 and bit 55 have to be zero, otherwise a specification
 * exception will occur instead of a page translation exception. The
 * specification exception has the bad habit not to store necessary
 * information in the lowcore.
 * Bits 54 and 63 are used to indicate the page type.
 * A swap pte is indicated by bit pattern (pte & 0x201) == 0x200
 * This leaves the bits 0-51 and bits 56-62 to store type and offset.
 * We use the 5 bits from 57-61 for the type and the 52 bits from 0-51
 * for the offset.
 * |			  offset			|01100|type |00|
 * |0000000000111111111122222222223333333333444444444455|55555|55566|66|
 * |0123456789012345678901234567890123456789012345678901|23456|78901|23|
 */

#define __SWP_OFFSET_MASK	((1UL << 52) - 1)
#define __SWP_OFFSET_SHIFT	12
#define __SWP_TYPE_MASK		((1UL << 5) - 1)
#define __SWP_TYPE_SHIFT	2

static inline pte_t mk_swap_pte(unsigned long type, unsigned long offset)
{
	pte_t pte;

	pte_val(pte) = _PAGE_INVALID | _PAGE_PROTECT;
	pte_val(pte) |= (offset & __SWP_OFFSET_MASK) << __SWP_OFFSET_SHIFT;
	pte_val(pte) |= (type & __SWP_TYPE_MASK) << __SWP_TYPE_SHIFT;
	return pte;
}

static inline unsigned long __swp_type(swp_entry_t entry)
{
	return (entry.val >> __SWP_TYPE_SHIFT) & __SWP_TYPE_MASK;
}

static inline unsigned long __swp_offset(swp_entry_t entry)
{
	return (entry.val >> __SWP_OFFSET_SHIFT) & __SWP_OFFSET_MASK;
}

static inline swp_entry_t __swp_entry(unsigned long type, unsigned long offset)
{
	return (swp_entry_t) { pte_val(mk_swap_pte(type, offset)) };
}

#define __pte_to_swp_entry(pte)	((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(x)	((pte_t) { (x).val })

#endif /* !__ASSEMBLY__ */

#define kern_addr_valid(addr)   (1)

extern int vmem_add_mapping(unsigned long start, unsigned long size);
extern int vmem_remove_mapping(unsigned long start, unsigned long size);
extern int s390_enable_sie(void);
extern int s390_enable_skey(void);
extern void s390_reset_cmma(struct mm_struct *mm);

/* s390 has a private copy of get unmapped area to deal with cache synonyms */
#define HAVE_ARCH_UNMAPPED_AREA
#define HAVE_ARCH_UNMAPPED_AREA_TOPDOWN

/*
 * No page table caches to initialise
 */
static inline void pgtable_cache_init(void) { }
static inline void check_pgt_cache(void) { }

#include <asm-generic/pgtable.h>

#endif /* _S390_PAGE_H */
