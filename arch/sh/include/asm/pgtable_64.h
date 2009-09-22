#ifndef __ASM_SH_PGTABLE_64_H
#define __ASM_SH_PGTABLE_64_H

/*
 * include/asm-sh/pgtable_64.h
 *
 * This file contains the functions and defines necessary to modify and use
 * the SuperH page table tree.
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 * Copyright (C) 2003, 2004  Paul Mundt
 * Copyright (C) 2003, 2004  Richard Curnow
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/threads.h>
#include <asm/processor.h>
#include <asm/page.h>

/*
 * Error outputs.
 */
#define pte_ERROR(e) \
	printk("%s:%d: bad pte %016Lx.\n", __FILE__, __LINE__, pte_val(e))
#define pgd_ERROR(e) \
	printk("%s:%d: bad pgd %08lx.\n", __FILE__, __LINE__, pgd_val(e))

/*
 * Table setting routines. Used within arch/mm only.
 */
#define set_pmd(pmdptr, pmdval) (*(pmdptr) = pmdval)

static __inline__ void set_pte(pte_t *pteptr, pte_t pteval)
{
	unsigned long long x = ((unsigned long long) pteval.pte_low);
	unsigned long long *xp = (unsigned long long *) pteptr;
	/*
	 * Sign-extend based on NPHYS.
	 */
	*(xp) = (x & NPHYS_SIGN) ? (x | NPHYS_MASK) : x;
}
#define set_pte_at(mm,addr,ptep,pteval) set_pte(ptep,pteval)

static __inline__ void pmd_set(pmd_t *pmdp,pte_t *ptep)
{
	pmd_val(*pmdp) = (unsigned long) ptep;
}

/*
 * PGD defines. Top level.
 */

/* To find an entry in a generic PGD. */
#define pgd_index(address) (((address) >> PGDIR_SHIFT) & (PTRS_PER_PGD-1))
#define __pgd_offset(address) pgd_index(address)
#define pgd_offset(mm, address) ((mm)->pgd+pgd_index(address))

/* To find an entry in a kernel PGD. */
#define pgd_offset_k(address) pgd_offset(&init_mm, address)

#define __pud_offset(address)	(((address) >> PUD_SHIFT) & (PTRS_PER_PUD-1))
#define __pmd_offset(address)	(((address) >> PMD_SHIFT) & (PTRS_PER_PMD-1))

/*
 * PMD level access routines. Same notes as above.
 */
#define _PMD_EMPTY		0x0
/* Either the PMD is empty or present, it's not paged out */
#define pmd_present(pmd_entry)	(pmd_val(pmd_entry) & _PAGE_PRESENT)
#define pmd_clear(pmd_entry_p)	(set_pmd((pmd_entry_p), __pmd(_PMD_EMPTY)))
#define pmd_none(pmd_entry)	(pmd_val((pmd_entry)) == _PMD_EMPTY)
#define pmd_bad(pmd_entry)	((pmd_val(pmd_entry) & (~PAGE_MASK & ~_PAGE_USER)) != _KERNPG_TABLE)

#define pmd_page_vaddr(pmd_entry) \
	((unsigned long) __va(pmd_val(pmd_entry) & PAGE_MASK))

#define pmd_page(pmd) \
	(virt_to_page(pmd_val(pmd)))

/* PMD to PTE dereferencing */
#define pte_index(address) \
		((address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1))

#define __pte_offset(address)	pte_index(address)

#define pte_offset_kernel(dir, addr) \
		((pte_t *) ((pmd_val(*(dir))) & PAGE_MASK) + pte_index((addr)))

#define pte_offset_map(dir,addr)	pte_offset_kernel(dir, addr)
#define pte_offset_map_nested(dir,addr)	pte_offset_kernel(dir, addr)
#define pte_unmap(pte)		do { } while (0)
#define pte_unmap_nested(pte)	do { } while (0)

#ifndef __ASSEMBLY__
#define IOBASE_VADDR	0xff000000
#define IOBASE_END	0xffffffff

/*
 * PTEL coherent flags.
 * See Chapter 17 ST50 CPU Core Volume 1, Architecture.
 */
/* The bits that are required in the SH-5 TLB are placed in the h/w-defined
   positions, to avoid expensive bit shuffling on every refill.  The remaining
   bits are used for s/w purposes and masked out on each refill.

   Note, the PTE slots are used to hold data of type swp_entry_t when a page is
   swapped out.  Only the _PAGE_PRESENT flag is significant when the page is
   swapped out, and it must be placed so that it doesn't overlap either the
   type or offset fields of swp_entry_t.  For x86, offset is at [31:8] and type
   at [6:1], with _PAGE_PRESENT at bit 0 for both pte_t and swp_entry_t.  This
   scheme doesn't map to SH-5 because bit [0] controls cacheability.  So bit
   [2] is used for _PAGE_PRESENT and the type field of swp_entry_t is split
   into 2 pieces.  That is handled by SWP_ENTRY and SWP_TYPE below. */
#define _PAGE_WT	0x001  /* CB0: if cacheable, 1->write-thru, 0->write-back */
#define _PAGE_DEVICE	0x001  /* CB0: if uncacheable, 1->device (i.e. no write-combining or reordering at bus level) */
#define _PAGE_CACHABLE	0x002  /* CB1: uncachable/cachable */
#define _PAGE_PRESENT	0x004  /* software: page referenced */
#define _PAGE_FILE	0x004  /* software: only when !present */
#define _PAGE_SIZE0	0x008  /* SZ0-bit : size of page */
#define _PAGE_SIZE1	0x010  /* SZ1-bit : size of page */
#define _PAGE_SHARED	0x020  /* software: reflects PTEH's SH */
#define _PAGE_READ	0x040  /* PR0-bit : read access allowed */
#define _PAGE_EXECUTE	0x080  /* PR1-bit : execute access allowed */
#define _PAGE_WRITE	0x100  /* PR2-bit : write access allowed */
#define _PAGE_USER	0x200  /* PR3-bit : user space access allowed */
#define _PAGE_DIRTY	0x400  /* software: page accessed in write */
#define _PAGE_ACCESSED	0x800  /* software: page referenced */

/* Mask which drops software flags */
#define _PAGE_FLAGS_HARDWARE_MASK	0xfffffffffffff3dbLL

/*
 * HugeTLB support
 */
#if defined(CONFIG_HUGETLB_PAGE_SIZE_64K)
#define _PAGE_SZHUGE	(_PAGE_SIZE0)
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_1MB)
#define _PAGE_SZHUGE	(_PAGE_SIZE1)
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_512MB)
#define _PAGE_SZHUGE	(_PAGE_SIZE0 | _PAGE_SIZE1)
#endif

/*
 * Stub out _PAGE_SZHUGE if we don't have a good definition for it,
 * to make pte_mkhuge() happy.
 */
#ifndef _PAGE_SZHUGE
# define _PAGE_SZHUGE	(0)
#endif

/*
 * Default flags for a Kernel page.
 * This is fundametally also SHARED because the main use of this define
 * (other than for PGD/PMD entries) is for the VMALLOC pool which is
 * contextless.
 *
 * _PAGE_EXECUTE is required for modules
 *
 */
#define _KERNPG_TABLE	(_PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE | \
			 _PAGE_EXECUTE | \
			 _PAGE_CACHABLE | _PAGE_ACCESSED | _PAGE_DIRTY | \
			 _PAGE_SHARED)

/* Default flags for a User page */
#define _PAGE_TABLE	(_KERNPG_TABLE | _PAGE_USER)

#define _PAGE_CHG_MASK	(PTE_MASK | _PAGE_ACCESSED | _PAGE_DIRTY)

/*
 * We have full permissions (Read/Write/Execute/Shared).
 */
#define _PAGE_COMMON	(_PAGE_PRESENT | _PAGE_USER | \
			 _PAGE_CACHABLE | _PAGE_ACCESSED)

#define PAGE_NONE	__pgprot(_PAGE_CACHABLE | _PAGE_ACCESSED)
#define PAGE_SHARED	__pgprot(_PAGE_COMMON | _PAGE_READ | _PAGE_WRITE | \
				 _PAGE_SHARED)
#define PAGE_EXECREAD	__pgprot(_PAGE_COMMON | _PAGE_READ | _PAGE_EXECUTE)

/*
 * We need to include PAGE_EXECUTE in PAGE_COPY because it is the default
 * protection mode for the stack.
 */
#define PAGE_COPY	PAGE_EXECREAD

#define PAGE_READONLY	__pgprot(_PAGE_COMMON | _PAGE_READ)
#define PAGE_WRITEONLY	__pgprot(_PAGE_COMMON | _PAGE_WRITE)
#define PAGE_RWX	__pgprot(_PAGE_COMMON | _PAGE_READ | \
				 _PAGE_WRITE | _PAGE_EXECUTE)
#define PAGE_KERNEL	__pgprot(_KERNPG_TABLE)

#define PAGE_KERNEL_NOCACHE \
			__pgprot(_PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE | \
				 _PAGE_EXECUTE | _PAGE_ACCESSED | \
				 _PAGE_DIRTY | _PAGE_SHARED)

/* Make it a device mapping for maximum safety (e.g. for mapping device
   registers into user-space via /dev/map).  */
#define pgprot_noncached(x) __pgprot(((x).pgprot & ~(_PAGE_CACHABLE)) | _PAGE_DEVICE)
#define pgprot_writecombine(prot) __pgprot(pgprot_val(prot) & ~_PAGE_CACHABLE)

/*
 * Handling allocation failures during page table setup.
 */
extern void __handle_bad_pmd_kernel(pmd_t * pmd);
#define __handle_bad_pmd(x)	__handle_bad_pmd_kernel(x)

/*
 * PTE level access routines.
 *
 * Note1:
 * It's the tree walk leaf. This is physical address to be stored.
 *
 * Note 2:
 * Regarding the choice of _PTE_EMPTY:

   We must choose a bit pattern that cannot be valid, whether or not the page
   is present.  bit[2]==1 => present, bit[2]==0 => swapped out.  If swapped
   out, bits [31:8], [6:3], [1:0] are under swapper control, so only bit[7] is
   left for us to select.  If we force bit[7]==0 when swapped out, we could use
   the combination bit[7,2]=2'b10 to indicate an empty PTE.  Alternatively, if
   we force bit[7]==1 when swapped out, we can use all zeroes to indicate
   empty.  This is convenient, because the page tables get cleared to zero
   when they are allocated.

 */
#define _PTE_EMPTY	0x0
#define pte_present(x)	(pte_val(x) & _PAGE_PRESENT)
#define pte_clear(mm,addr,xp)	(set_pte_at(mm, addr, xp, __pte(_PTE_EMPTY)))
#define pte_none(x)	(pte_val(x) == _PTE_EMPTY)

/*
 * Some definitions to translate between mem_map, PTEs, and page
 * addresses:
 */

/*
 * Given a PTE, return the index of the mem_map[] entry corresponding
 * to the page frame the PTE. Get the absolute physical address, make
 * a relative physical address and translate it to an index.
 */
#define pte_pagenr(x)		(((unsigned long) (pte_val(x)) - \
				 __MEMORY_START) >> PAGE_SHIFT)

/*
 * Given a PTE, return the "struct page *".
 */
#define pte_page(x)		(mem_map + pte_pagenr(x))

/*
 * Return number of (down rounded) MB corresponding to x pages.
 */
#define pages_to_mb(x) ((x) >> (20-PAGE_SHIFT))


/*
 * The following have defined behavior only work if pte_present() is true.
 */
static inline int pte_dirty(pte_t pte)  { return pte_val(pte) & _PAGE_DIRTY; }
static inline int pte_young(pte_t pte)  { return pte_val(pte) & _PAGE_ACCESSED; }
static inline int pte_file(pte_t pte)   { return pte_val(pte) & _PAGE_FILE; }
static inline int pte_write(pte_t pte)  { return pte_val(pte) & _PAGE_WRITE; }
static inline int pte_special(pte_t pte){ return 0; }

static inline pte_t pte_wrprotect(pte_t pte)	{ set_pte(&pte, __pte(pte_val(pte) & ~_PAGE_WRITE)); return pte; }
static inline pte_t pte_mkclean(pte_t pte)	{ set_pte(&pte, __pte(pte_val(pte) & ~_PAGE_DIRTY)); return pte; }
static inline pte_t pte_mkold(pte_t pte)	{ set_pte(&pte, __pte(pte_val(pte) & ~_PAGE_ACCESSED)); return pte; }
static inline pte_t pte_mkwrite(pte_t pte)	{ set_pte(&pte, __pte(pte_val(pte) | _PAGE_WRITE)); return pte; }
static inline pte_t pte_mkdirty(pte_t pte)	{ set_pte(&pte, __pte(pte_val(pte) | _PAGE_DIRTY)); return pte; }
static inline pte_t pte_mkyoung(pte_t pte)	{ set_pte(&pte, __pte(pte_val(pte) | _PAGE_ACCESSED)); return pte; }
static inline pte_t pte_mkhuge(pte_t pte)	{ set_pte(&pte, __pte(pte_val(pte) | _PAGE_SZHUGE)); return pte; }
static inline pte_t pte_mkspecial(pte_t pte)	{ return pte; }


/*
 * Conversion functions: convert a page and protection to a page entry.
 *
 * extern pte_t mk_pte(struct page *page, pgprot_t pgprot)
 */
#define mk_pte(page,pgprot)							\
({										\
	pte_t __pte;								\
										\
	set_pte(&__pte, __pte((((page)-mem_map) << PAGE_SHIFT) | 		\
		__MEMORY_START | pgprot_val((pgprot))));			\
	__pte;									\
})

/*
 * This takes a (absolute) physical page address that is used
 * by the remapping functions
 */
#define mk_pte_phys(physpage, pgprot) \
({ pte_t __pte; set_pte(&__pte, __pte(physpage | pgprot_val(pgprot))); __pte; })

static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{ set_pte(&pte, __pte((pte_val(pte) & _PAGE_CHG_MASK) | pgprot_val(newprot))); return pte; }

/* Encode and decode a swap entry */
#define __swp_type(x)			(((x).val & 3) + (((x).val >> 1) & 0x3c))
#define __swp_offset(x)			((x).val >> 8)
#define __swp_entry(type, offset)	((swp_entry_t) { ((offset << 8) + ((type & 0x3c) << 1) + (type & 3)) })
#define __pte_to_swp_entry(pte)		((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(x)		((pte_t) { (x).val })

/* Encode and decode a nonlinear file mapping entry */
#define PTE_FILE_MAX_BITS		29
#define pte_to_pgoff(pte)		(pte_val(pte))
#define pgoff_to_pte(off)		((pte_t) { (off) | _PAGE_FILE })

#endif /* !__ASSEMBLY__ */

#define pfn_pte(pfn, prot)	__pte(((pfn) << PAGE_SHIFT) | pgprot_val(prot))
#define pfn_pmd(pfn, prot)	__pmd(((pfn) << PAGE_SHIFT) | pgprot_val(prot))

#endif /* __ASM_SH_PGTABLE_64_H */
