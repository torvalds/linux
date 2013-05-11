#ifndef _ASM_IA64_PGTABLE_H
#define _ASM_IA64_PGTABLE_H

/*
 * This file contains the functions and defines necessary to modify and use
 * the IA-64 page table tree.
 *
 * This hopefully works with any (fixed) IA-64 page-size, as defined
 * in <asm/page.h>.
 *
 * Copyright (C) 1998-2005 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */


#include <asm/mman.h>
#include <asm/page.h>
#include <asm/processor.h>
#include <asm/types.h>

#define IA64_MAX_PHYS_BITS	50	/* max. number of physical address bits (architected) */

/*
 * First, define the various bits in a PTE.  Note that the PTE format
 * matches the VHPT short format, the firt doubleword of the VHPD long
 * format, and the first doubleword of the TLB insertion format.
 */
#define _PAGE_P_BIT		0
#define _PAGE_A_BIT		5
#define _PAGE_D_BIT		6

#define _PAGE_P			(1 << _PAGE_P_BIT)	/* page present bit */
#define _PAGE_MA_WB		(0x0 <<  2)	/* write back memory attribute */
#define _PAGE_MA_UC		(0x4 <<  2)	/* uncacheable memory attribute */
#define _PAGE_MA_UCE		(0x5 <<  2)	/* UC exported attribute */
#define _PAGE_MA_WC		(0x6 <<  2)	/* write coalescing memory attribute */
#define _PAGE_MA_NAT		(0x7 <<  2)	/* not-a-thing attribute */
#define _PAGE_MA_MASK		(0x7 <<  2)
#define _PAGE_PL_0		(0 <<  7)	/* privilege level 0 (kernel) */
#define _PAGE_PL_1		(1 <<  7)	/* privilege level 1 (unused) */
#define _PAGE_PL_2		(2 <<  7)	/* privilege level 2 (unused) */
#define _PAGE_PL_3		(3 <<  7)	/* privilege level 3 (user) */
#define _PAGE_PL_MASK		(3 <<  7)
#define _PAGE_AR_R		(0 <<  9)	/* read only */
#define _PAGE_AR_RX		(1 <<  9)	/* read & execute */
#define _PAGE_AR_RW		(2 <<  9)	/* read & write */
#define _PAGE_AR_RWX		(3 <<  9)	/* read, write & execute */
#define _PAGE_AR_R_RW		(4 <<  9)	/* read / read & write */
#define _PAGE_AR_RX_RWX		(5 <<  9)	/* read & exec / read, write & exec */
#define _PAGE_AR_RWX_RW		(6 <<  9)	/* read, write & exec / read & write */
#define _PAGE_AR_X_RX		(7 <<  9)	/* exec & promote / read & exec */
#define _PAGE_AR_MASK		(7 <<  9)
#define _PAGE_AR_SHIFT		9
#define _PAGE_A			(1 << _PAGE_A_BIT)	/* page accessed bit */
#define _PAGE_D			(1 << _PAGE_D_BIT)	/* page dirty bit */
#define _PAGE_PPN_MASK		(((__IA64_UL(1) << IA64_MAX_PHYS_BITS) - 1) & ~0xfffUL)
#define _PAGE_ED		(__IA64_UL(1) << 52)	/* exception deferral */
#define _PAGE_PROTNONE		(__IA64_UL(1) << 63)

/* Valid only for a PTE with the present bit cleared: */
#define _PAGE_FILE		(1 << 1)		/* see swap & file pte remarks below */

#define _PFN_MASK		_PAGE_PPN_MASK
/* Mask of bits which may be changed by pte_modify(); the odd bits are there for _PAGE_PROTNONE */
#define _PAGE_CHG_MASK	(_PAGE_P | _PAGE_PROTNONE | _PAGE_PL_MASK | _PAGE_AR_MASK | _PAGE_ED)

#define _PAGE_SIZE_4K	12
#define _PAGE_SIZE_8K	13
#define _PAGE_SIZE_16K	14
#define _PAGE_SIZE_64K	16
#define _PAGE_SIZE_256K	18
#define _PAGE_SIZE_1M	20
#define _PAGE_SIZE_4M	22
#define _PAGE_SIZE_16M	24
#define _PAGE_SIZE_64M	26
#define _PAGE_SIZE_256M	28
#define _PAGE_SIZE_1G	30
#define _PAGE_SIZE_4G	32

#define __ACCESS_BITS		_PAGE_ED | _PAGE_A | _PAGE_P | _PAGE_MA_WB
#define __DIRTY_BITS_NO_ED	_PAGE_A | _PAGE_P | _PAGE_D | _PAGE_MA_WB
#define __DIRTY_BITS		_PAGE_ED | __DIRTY_BITS_NO_ED

/*
 * How many pointers will a page table level hold expressed in shift
 */
#define PTRS_PER_PTD_SHIFT	(PAGE_SHIFT-3)

/*
 * Definitions for fourth level:
 */
#define PTRS_PER_PTE	(__IA64_UL(1) << (PTRS_PER_PTD_SHIFT))

/*
 * Definitions for third level:
 *
 * PMD_SHIFT determines the size of the area a third-level page table
 * can map.
 */
#define PMD_SHIFT	(PAGE_SHIFT + (PTRS_PER_PTD_SHIFT))
#define PMD_SIZE	(1UL << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE-1))
#define PTRS_PER_PMD	(1UL << (PTRS_PER_PTD_SHIFT))

#ifdef CONFIG_PGTABLE_4
/*
 * Definitions for second level:
 *
 * PUD_SHIFT determines the size of the area a second-level page table
 * can map.
 */
#define PUD_SHIFT	(PMD_SHIFT + (PTRS_PER_PTD_SHIFT))
#define PUD_SIZE	(1UL << PUD_SHIFT)
#define PUD_MASK	(~(PUD_SIZE-1))
#define PTRS_PER_PUD	(1UL << (PTRS_PER_PTD_SHIFT))
#endif

/*
 * Definitions for first level:
 *
 * PGDIR_SHIFT determines what a first-level page table entry can map.
 */
#ifdef CONFIG_PGTABLE_4
#define PGDIR_SHIFT		(PUD_SHIFT + (PTRS_PER_PTD_SHIFT))
#else
#define PGDIR_SHIFT		(PMD_SHIFT + (PTRS_PER_PTD_SHIFT))
#endif
#define PGDIR_SIZE		(__IA64_UL(1) << PGDIR_SHIFT)
#define PGDIR_MASK		(~(PGDIR_SIZE-1))
#define PTRS_PER_PGD_SHIFT	PTRS_PER_PTD_SHIFT
#define PTRS_PER_PGD		(1UL << PTRS_PER_PGD_SHIFT)
#define USER_PTRS_PER_PGD	(5*PTRS_PER_PGD/8)	/* regions 0-4 are user regions */
#define FIRST_USER_ADDRESS	0

/*
 * All the normal masks have the "page accessed" bits on, as any time
 * they are used, the page is accessed. They are cleared only by the
 * page-out routines.
 */
#define PAGE_NONE	__pgprot(_PAGE_PROTNONE | _PAGE_A)
#define PAGE_SHARED	__pgprot(__ACCESS_BITS | _PAGE_PL_3 | _PAGE_AR_RW)
#define PAGE_READONLY	__pgprot(__ACCESS_BITS | _PAGE_PL_3 | _PAGE_AR_R)
#define PAGE_COPY	__pgprot(__ACCESS_BITS | _PAGE_PL_3 | _PAGE_AR_R)
#define PAGE_COPY_EXEC	__pgprot(__ACCESS_BITS | _PAGE_PL_3 | _PAGE_AR_RX)
#define PAGE_GATE	__pgprot(__ACCESS_BITS | _PAGE_PL_0 | _PAGE_AR_X_RX)
#define PAGE_KERNEL	__pgprot(__DIRTY_BITS  | _PAGE_PL_0 | _PAGE_AR_RWX)
#define PAGE_KERNELRX	__pgprot(__ACCESS_BITS | _PAGE_PL_0 | _PAGE_AR_RX)
#define PAGE_KERNEL_UC	__pgprot(__DIRTY_BITS  | _PAGE_PL_0 | _PAGE_AR_RWX | \
				 _PAGE_MA_UC)

# ifndef __ASSEMBLY__

#include <linux/sched.h>	/* for mm_struct */
#include <linux/bitops.h>
#include <asm/cacheflush.h>
#include <asm/mmu_context.h>

/*
 * Next come the mappings that determine how mmap() protection bits
 * (PROT_EXEC, PROT_READ, PROT_WRITE, PROT_NONE) get implemented.  The
 * _P version gets used for a private shared memory segment, the _S
 * version gets used for a shared memory segment with MAP_SHARED on.
 * In a private shared memory segment, we do a copy-on-write if a task
 * attempts to write to the page.
 */
	/* xwr */
#define __P000	PAGE_NONE
#define __P001	PAGE_READONLY
#define __P010	PAGE_READONLY	/* write to priv pg -> copy & make writable */
#define __P011	PAGE_READONLY	/* ditto */
#define __P100	__pgprot(__ACCESS_BITS | _PAGE_PL_3 | _PAGE_AR_X_RX)
#define __P101	__pgprot(__ACCESS_BITS | _PAGE_PL_3 | _PAGE_AR_RX)
#define __P110	PAGE_COPY_EXEC
#define __P111	PAGE_COPY_EXEC

#define __S000	PAGE_NONE
#define __S001	PAGE_READONLY
#define __S010	PAGE_SHARED	/* we don't have (and don't need) write-only */
#define __S011	PAGE_SHARED
#define __S100	__pgprot(__ACCESS_BITS | _PAGE_PL_3 | _PAGE_AR_X_RX)
#define __S101	__pgprot(__ACCESS_BITS | _PAGE_PL_3 | _PAGE_AR_RX)
#define __S110	__pgprot(__ACCESS_BITS | _PAGE_PL_3 | _PAGE_AR_RWX)
#define __S111	__pgprot(__ACCESS_BITS | _PAGE_PL_3 | _PAGE_AR_RWX)

#define pgd_ERROR(e)	printk("%s:%d: bad pgd %016lx.\n", __FILE__, __LINE__, pgd_val(e))
#ifdef CONFIG_PGTABLE_4
#define pud_ERROR(e)	printk("%s:%d: bad pud %016lx.\n", __FILE__, __LINE__, pud_val(e))
#endif
#define pmd_ERROR(e)	printk("%s:%d: bad pmd %016lx.\n", __FILE__, __LINE__, pmd_val(e))
#define pte_ERROR(e)	printk("%s:%d: bad pte %016lx.\n", __FILE__, __LINE__, pte_val(e))


/*
 * Some definitions to translate between mem_map, PTEs, and page addresses:
 */


/* Quick test to see if ADDR is a (potentially) valid physical address. */
static inline long
ia64_phys_addr_valid (unsigned long addr)
{
	return (addr & (local_cpu_data->unimpl_pa_mask)) == 0;
}

/*
 * kern_addr_valid(ADDR) tests if ADDR is pointing to valid kernel
 * memory.  For the return value to be meaningful, ADDR must be >=
 * PAGE_OFFSET.  This operation can be relatively expensive (e.g.,
 * require a hash-, or multi-level tree-lookup or something of that
 * sort) but it guarantees to return TRUE only if accessing the page
 * at that address does not cause an error.  Note that there may be
 * addresses for which kern_addr_valid() returns FALSE even though an
 * access would not cause an error (e.g., this is typically true for
 * memory mapped I/O regions.
 *
 * XXX Need to implement this for IA-64.
 */
#define kern_addr_valid(addr)	(1)


/*
 * Now come the defines and routines to manage and access the three-level
 * page table.
 */


#define VMALLOC_START		(RGN_BASE(RGN_GATE) + 0x200000000UL)
#ifdef CONFIG_VIRTUAL_MEM_MAP
# define VMALLOC_END_INIT	(RGN_BASE(RGN_GATE) + (1UL << (4*PAGE_SHIFT - 9)))
extern unsigned long VMALLOC_END;
#else
#if defined(CONFIG_SPARSEMEM) && defined(CONFIG_SPARSEMEM_VMEMMAP)
/* SPARSEMEM_VMEMMAP uses half of vmalloc... */
# define VMALLOC_END		(RGN_BASE(RGN_GATE) + (1UL << (4*PAGE_SHIFT - 10)))
# define vmemmap		((struct page *)VMALLOC_END)
#else
# define VMALLOC_END		(RGN_BASE(RGN_GATE) + (1UL << (4*PAGE_SHIFT - 9)))
#endif
#endif

/* fs/proc/kcore.c */
#define	kc_vaddr_to_offset(v) ((v) - RGN_BASE(RGN_GATE))
#define	kc_offset_to_vaddr(o) ((o) + RGN_BASE(RGN_GATE))

#define RGN_MAP_SHIFT (PGDIR_SHIFT + PTRS_PER_PGD_SHIFT - 3)
#define RGN_MAP_LIMIT	((1UL << RGN_MAP_SHIFT) - PAGE_SIZE)	/* per region addr limit */

/*
 * Conversion functions: convert page frame number (pfn) and a protection value to a page
 * table entry (pte).
 */
#define pfn_pte(pfn, pgprot) \
({ pte_t __pte; pte_val(__pte) = ((pfn) << PAGE_SHIFT) | pgprot_val(pgprot); __pte; })

/* Extract pfn from pte.  */
#define pte_pfn(_pte)		((pte_val(_pte) & _PFN_MASK) >> PAGE_SHIFT)

#define mk_pte(page, pgprot)	pfn_pte(page_to_pfn(page), (pgprot))

/* This takes a physical page address that is used by the remapping functions */
#define mk_pte_phys(physpage, pgprot) \
({ pte_t __pte; pte_val(__pte) = physpage + pgprot_val(pgprot); __pte; })

#define pte_modify(_pte, newprot) \
	(__pte((pte_val(_pte) & ~_PAGE_CHG_MASK) | (pgprot_val(newprot) & _PAGE_CHG_MASK)))

#define pte_none(pte) 			(!pte_val(pte))
#define pte_present(pte)		(pte_val(pte) & (_PAGE_P | _PAGE_PROTNONE))
#define pte_clear(mm,addr,pte)		(pte_val(*(pte)) = 0UL)
/* pte_page() returns the "struct page *" corresponding to the PTE: */
#define pte_page(pte)			virt_to_page(((pte_val(pte) & _PFN_MASK) + PAGE_OFFSET))

#define pmd_none(pmd)			(!pmd_val(pmd))
#define pmd_bad(pmd)			(!ia64_phys_addr_valid(pmd_val(pmd)))
#define pmd_present(pmd)		(pmd_val(pmd) != 0UL)
#define pmd_clear(pmdp)			(pmd_val(*(pmdp)) = 0UL)
#define pmd_page_vaddr(pmd)		((unsigned long) __va(pmd_val(pmd) & _PFN_MASK))
#define pmd_page(pmd)			virt_to_page((pmd_val(pmd) + PAGE_OFFSET))

#define pud_none(pud)			(!pud_val(pud))
#define pud_bad(pud)			(!ia64_phys_addr_valid(pud_val(pud)))
#define pud_present(pud)		(pud_val(pud) != 0UL)
#define pud_clear(pudp)			(pud_val(*(pudp)) = 0UL)
#define pud_page_vaddr(pud)		((unsigned long) __va(pud_val(pud) & _PFN_MASK))
#define pud_page(pud)			virt_to_page((pud_val(pud) + PAGE_OFFSET))

#ifdef CONFIG_PGTABLE_4
#define pgd_none(pgd)			(!pgd_val(pgd))
#define pgd_bad(pgd)			(!ia64_phys_addr_valid(pgd_val(pgd)))
#define pgd_present(pgd)		(pgd_val(pgd) != 0UL)
#define pgd_clear(pgdp)			(pgd_val(*(pgdp)) = 0UL)
#define pgd_page_vaddr(pgd)		((unsigned long) __va(pgd_val(pgd) & _PFN_MASK))
#define pgd_page(pgd)			virt_to_page((pgd_val(pgd) + PAGE_OFFSET))
#endif

/*
 * The following have defined behavior only work if pte_present() is true.
 */
#define pte_write(pte)	((unsigned) (((pte_val(pte) & _PAGE_AR_MASK) >> _PAGE_AR_SHIFT) - 2) <= 4)
#define pte_exec(pte)		((pte_val(pte) & _PAGE_AR_RX) != 0)
#define pte_dirty(pte)		((pte_val(pte) & _PAGE_D) != 0)
#define pte_young(pte)		((pte_val(pte) & _PAGE_A) != 0)
#define pte_file(pte)		((pte_val(pte) & _PAGE_FILE) != 0)
#define pte_special(pte)	0

/*
 * Note: we convert AR_RWX to AR_RX and AR_RW to AR_R by clearing the 2nd bit in the
 * access rights:
 */
#define pte_wrprotect(pte)	(__pte(pte_val(pte) & ~_PAGE_AR_RW))
#define pte_mkwrite(pte)	(__pte(pte_val(pte) | _PAGE_AR_RW))
#define pte_mkold(pte)		(__pte(pte_val(pte) & ~_PAGE_A))
#define pte_mkyoung(pte)	(__pte(pte_val(pte) | _PAGE_A))
#define pte_mkclean(pte)	(__pte(pte_val(pte) & ~_PAGE_D))
#define pte_mkdirty(pte)	(__pte(pte_val(pte) | _PAGE_D))
#define pte_mkhuge(pte)		(__pte(pte_val(pte)))
#define pte_mkspecial(pte)	(pte)

/*
 * Because ia64's Icache and Dcache is not coherent (on a cpu), we need to
 * sync icache and dcache when we insert *new* executable page.
 *  __ia64_sync_icache_dcache() check Pg_arch_1 bit and flush icache
 * if necessary.
 *
 *  set_pte() is also called by the kernel, but we can expect that the kernel
 *  flushes icache explicitly if necessary.
 */
#define pte_present_exec_user(pte)\
	((pte_val(pte) & (_PAGE_P | _PAGE_PL_MASK | _PAGE_AR_RX)) == \
		(_PAGE_P | _PAGE_PL_3 | _PAGE_AR_RX))

extern void __ia64_sync_icache_dcache(pte_t pteval);
static inline void set_pte(pte_t *ptep, pte_t pteval)
{
	/* page is present && page is user  && page is executable
	 * && (page swapin or new page or page migraton
	 *	|| copy_on_write with page copying.)
	 */
	if (pte_present_exec_user(pteval) &&
	    (!pte_present(*ptep) ||
		pte_pfn(*ptep) != pte_pfn(pteval)))
		/* load_module() calles flush_icache_range() explicitly*/
		__ia64_sync_icache_dcache(pteval);
	*ptep = pteval;
}

#define set_pte_at(mm,addr,ptep,pteval) set_pte(ptep,pteval)

/*
 * Make page protection values cacheable, uncacheable, or write-
 * combining.  Note that "protection" is really a misnomer here as the
 * protection value contains the memory attribute bits, dirty bits, and
 * various other bits as well.
 */
#define pgprot_cacheable(prot)		__pgprot((pgprot_val(prot) & ~_PAGE_MA_MASK) | _PAGE_MA_WB)
#define pgprot_noncached(prot)		__pgprot((pgprot_val(prot) & ~_PAGE_MA_MASK) | _PAGE_MA_UC)
#define pgprot_writecombine(prot)	__pgprot((pgprot_val(prot) & ~_PAGE_MA_MASK) | _PAGE_MA_WC)

struct file;
extern pgprot_t phys_mem_access_prot(struct file *file, unsigned long pfn,
				     unsigned long size, pgprot_t vma_prot);
#define __HAVE_PHYS_MEM_ACCESS_PROT

static inline unsigned long
pgd_index (unsigned long address)
{
	unsigned long region = address >> 61;
	unsigned long l1index = (address >> PGDIR_SHIFT) & ((PTRS_PER_PGD >> 3) - 1);

	return (region << (PAGE_SHIFT - 6)) | l1index;
}

/* The offset in the 1-level directory is given by the 3 region bits
   (61..63) and the level-1 bits.  */
static inline pgd_t*
pgd_offset (const struct mm_struct *mm, unsigned long address)
{
	return mm->pgd + pgd_index(address);
}

/* In the kernel's mapped region we completely ignore the region number
   (since we know it's in region number 5). */
#define pgd_offset_k(addr) \
	(init_mm.pgd + (((addr) >> PGDIR_SHIFT) & (PTRS_PER_PGD - 1)))

/* Look up a pgd entry in the gate area.  On IA-64, the gate-area
   resides in the kernel-mapped segment, hence we use pgd_offset_k()
   here.  */
#define pgd_offset_gate(mm, addr)	pgd_offset_k(addr)

#ifdef CONFIG_PGTABLE_4
/* Find an entry in the second-level page table.. */
#define pud_offset(dir,addr) \
	((pud_t *) pgd_page_vaddr(*(dir)) + (((addr) >> PUD_SHIFT) & (PTRS_PER_PUD - 1)))
#endif

/* Find an entry in the third-level page table.. */
#define pmd_offset(dir,addr) \
	((pmd_t *) pud_page_vaddr(*(dir)) + (((addr) >> PMD_SHIFT) & (PTRS_PER_PMD - 1)))

/*
 * Find an entry in the third-level page table.  This looks more complicated than it
 * should be because some platforms place page tables in high memory.
 */
#define pte_index(addr)	 	(((addr) >> PAGE_SHIFT) & (PTRS_PER_PTE - 1))
#define pte_offset_kernel(dir,addr)	((pte_t *) pmd_page_vaddr(*(dir)) + pte_index(addr))
#define pte_offset_map(dir,addr)	pte_offset_kernel(dir, addr)
#define pte_unmap(pte)			do { } while (0)

/* atomic versions of the some PTE manipulations: */

static inline int
ptep_test_and_clear_young (struct vm_area_struct *vma, unsigned long addr, pte_t *ptep)
{
#ifdef CONFIG_SMP
	if (!pte_young(*ptep))
		return 0;
	return test_and_clear_bit(_PAGE_A_BIT, ptep);
#else
	pte_t pte = *ptep;
	if (!pte_young(pte))
		return 0;
	set_pte_at(vma->vm_mm, addr, ptep, pte_mkold(pte));
	return 1;
#endif
}

static inline pte_t
ptep_get_and_clear(struct mm_struct *mm, unsigned long addr, pte_t *ptep)
{
#ifdef CONFIG_SMP
	return __pte(xchg((long *) ptep, 0));
#else
	pte_t pte = *ptep;
	pte_clear(mm, addr, ptep);
	return pte;
#endif
}

static inline void
ptep_set_wrprotect(struct mm_struct *mm, unsigned long addr, pte_t *ptep)
{
#ifdef CONFIG_SMP
	unsigned long new, old;

	do {
		old = pte_val(*ptep);
		new = pte_val(pte_wrprotect(__pte (old)));
	} while (cmpxchg((unsigned long *) ptep, old, new) != old);
#else
	pte_t old_pte = *ptep;
	set_pte_at(mm, addr, ptep, pte_wrprotect(old_pte));
#endif
}

static inline int
pte_same (pte_t a, pte_t b)
{
	return pte_val(a) == pte_val(b);
}

#define update_mmu_cache(vma, address, ptep) do { } while (0)

extern pgd_t swapper_pg_dir[PTRS_PER_PGD];
extern void paging_init (void);

/*
 * Note: The macros below rely on the fact that MAX_SWAPFILES_SHIFT <= number of
 *	 bits in the swap-type field of the swap pte.  It would be nice to
 *	 enforce that, but we can't easily include <linux/swap.h> here.
 *	 (Of course, better still would be to define MAX_SWAPFILES_SHIFT here...).
 *
 * Format of swap pte:
 *	bit   0   : present bit (must be zero)
 *	bit   1   : _PAGE_FILE (must be zero)
 *	bits  2- 8: swap-type
 *	bits  9-62: swap offset
 *	bit  63   : _PAGE_PROTNONE bit
 *
 * Format of file pte:
 *	bit   0   : present bit (must be zero)
 *	bit   1   : _PAGE_FILE (must be one)
 *	bits  2-62: file_offset/PAGE_SIZE
 *	bit  63   : _PAGE_PROTNONE bit
 */
#define __swp_type(entry)		(((entry).val >> 2) & 0x7f)
#define __swp_offset(entry)		(((entry).val << 1) >> 10)
#define __swp_entry(type,offset)	((swp_entry_t) { ((type) << 2) | ((long) (offset) << 9) })
#define __pte_to_swp_entry(pte)		((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(x)		((pte_t) { (x).val })

#define PTE_FILE_MAX_BITS		61
#define pte_to_pgoff(pte)		((pte_val(pte) << 1) >> 3)
#define pgoff_to_pte(off)		((pte_t) { ((off) << 2) | _PAGE_FILE })

/*
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
extern unsigned long empty_zero_page[PAGE_SIZE/sizeof(unsigned long)];
extern struct page *zero_page_memmap_ptr;
#define ZERO_PAGE(vaddr) (zero_page_memmap_ptr)

/* We provide our own get_unmapped_area to cope with VA holes for userland */
#define HAVE_ARCH_UNMAPPED_AREA

#ifdef CONFIG_HUGETLB_PAGE
#define HUGETLB_PGDIR_SHIFT	(HPAGE_SHIFT + 2*(PAGE_SHIFT-3))
#define HUGETLB_PGDIR_SIZE	(__IA64_UL(1) << HUGETLB_PGDIR_SHIFT)
#define HUGETLB_PGDIR_MASK	(~(HUGETLB_PGDIR_SIZE-1))
#endif


#define __HAVE_ARCH_PTEP_SET_ACCESS_FLAGS
/*
 * Update PTEP with ENTRY, which is guaranteed to be a less
 * restrictive PTE.  That is, ENTRY may have the ACCESSED, DIRTY, and
 * WRITABLE bits turned on, when the value at PTEP did not.  The
 * WRITABLE bit may only be turned if SAFELY_WRITABLE is TRUE.
 *
 * SAFELY_WRITABLE is TRUE if we can update the value at PTEP without
 * having to worry about races.  On SMP machines, there are only two
 * cases where this is true:
 *
 *	(1) *PTEP has the PRESENT bit turned OFF
 *	(2) ENTRY has the DIRTY bit turned ON
 *
 * On ia64, we could implement this routine with a cmpxchg()-loop
 * which ORs in the _PAGE_A/_PAGE_D bit if they're set in ENTRY.
 * However, like on x86, we can get a more streamlined version by
 * observing that it is OK to drop ACCESSED bit updates when
 * SAFELY_WRITABLE is FALSE.  Besides being rare, all that would do is
 * result in an extra Access-bit fault, which would then turn on the
 * ACCESSED bit in the low-level fault handler (iaccess_bit or
 * daccess_bit in ivt.S).
 */
#ifdef CONFIG_SMP
# define ptep_set_access_flags(__vma, __addr, __ptep, __entry, __safely_writable) \
({									\
	int __changed = !pte_same(*(__ptep), __entry);			\
	if (__changed && __safely_writable) {				\
		set_pte(__ptep, __entry);				\
		flush_tlb_page(__vma, __addr);				\
	}								\
	__changed;							\
})
#else
# define ptep_set_access_flags(__vma, __addr, __ptep, __entry, __safely_writable) \
({									\
	int __changed = !pte_same(*(__ptep), __entry);			\
	if (__changed) {						\
		set_pte_at((__vma)->vm_mm, (__addr), __ptep, __entry);	\
		flush_tlb_page(__vma, __addr);				\
	}								\
	__changed;							\
})
#endif

#  ifdef CONFIG_VIRTUAL_MEM_MAP
  /* arch mem_map init routine is needed due to holes in a virtual mem_map */
#   define __HAVE_ARCH_MEMMAP_INIT
    extern void memmap_init (unsigned long size, int nid, unsigned long zone,
			     unsigned long start_pfn);
#  endif /* CONFIG_VIRTUAL_MEM_MAP */
# endif /* !__ASSEMBLY__ */

/*
 * Identity-mapped regions use a large page size.  We'll call such large pages
 * "granules".  If you can think of a better name that's unambiguous, let me
 * know...
 */
#if defined(CONFIG_IA64_GRANULE_64MB)
# define IA64_GRANULE_SHIFT	_PAGE_SIZE_64M
#elif defined(CONFIG_IA64_GRANULE_16MB)
# define IA64_GRANULE_SHIFT	_PAGE_SIZE_16M
#endif
#define IA64_GRANULE_SIZE	(1 << IA64_GRANULE_SHIFT)
/*
 * log2() of the page size we use to map the kernel image (IA64_TR_KERNEL):
 */
#define KERNEL_TR_PAGE_SHIFT	_PAGE_SIZE_64M
#define KERNEL_TR_PAGE_SIZE	(1 << KERNEL_TR_PAGE_SHIFT)

/*
 * No page table caches to initialise
 */
#define pgtable_cache_init()	do { } while (0)

/* These tell get_user_pages() that the first gate page is accessible from user-level.  */
#define FIXADDR_USER_START	GATE_ADDR
#ifdef HAVE_BUGGY_SEGREL
# define FIXADDR_USER_END	(GATE_ADDR + 2*PAGE_SIZE)
#else
# define FIXADDR_USER_END	(GATE_ADDR + 2*PERCPU_PAGE_SIZE)
#endif

#define __HAVE_ARCH_PTEP_TEST_AND_CLEAR_YOUNG
#define __HAVE_ARCH_PTEP_GET_AND_CLEAR
#define __HAVE_ARCH_PTEP_SET_WRPROTECT
#define __HAVE_ARCH_PTE_SAME
#define __HAVE_ARCH_PGD_OFFSET_GATE


#ifndef CONFIG_PGTABLE_4
#include <asm-generic/pgtable-nopud.h>
#endif
#include <asm-generic/pgtable.h>

#endif /* _ASM_IA64_PGTABLE_H */
