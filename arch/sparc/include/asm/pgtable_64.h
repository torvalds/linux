/* SPDX-License-Identifier: GPL-2.0 */
/*
 * pgtable.h: SpitFire page table operations.
 *
 * Copyright 1996,1997 David S. Miller (davem@caip.rutgers.edu)
 * Copyright 1997,1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#ifndef _SPARC64_PGTABLE_H
#define _SPARC64_PGTABLE_H

/* This file contains the functions and defines necessary to modify and use
 * the SpitFire page tables.
 */

#include <asm-generic/pgtable-nop4d.h>
#include <linux/compiler.h>
#include <linux/const.h>
#include <asm/types.h>
#include <asm/spitfire.h>
#include <asm/asi.h>
#include <asm/adi.h>
#include <asm/page.h>
#include <asm/processor.h>

/* The kernel image occupies 0x4000000 to 0x6000000 (4MB --> 96MB).
 * The page copy blockops can use 0x6000000 to 0x8000000.
 * The 8K TSB is mapped in the 0x8000000 to 0x8400000 range.
 * The 4M TSB is mapped in the 0x8400000 to 0x8800000 range.
 * The PROM resides in an area spanning 0xf0000000 to 0x100000000.
 * The vmalloc area spans 0x100000000 to 0x200000000.
 * Since modules need to be in the lowest 32-bits of the address space,
 * we place them right before the OBP area from 0x10000000 to 0xf0000000.
 * There is a single static kernel PMD which maps from 0x0 to address
 * 0x400000000.
 */
#define	TLBTEMP_BASE		_AC(0x0000000006000000,UL)
#define	TSBMAP_8K_BASE		_AC(0x0000000008000000,UL)
#define	TSBMAP_4M_BASE		_AC(0x0000000008400000,UL)
#define MODULES_VADDR		_AC(0x0000000010000000,UL)
#define MODULES_LEN		_AC(0x00000000e0000000,UL)
#define MODULES_END		_AC(0x00000000f0000000,UL)
#define LOW_OBP_ADDRESS		_AC(0x00000000f0000000,UL)
#define HI_OBP_ADDRESS		_AC(0x0000000100000000,UL)
#define VMALLOC_START		_AC(0x0000000100000000,UL)
#define VMEMMAP_BASE		VMALLOC_END

/* PMD_SHIFT determines the size of the area a second-level page
 * table can map
 */
#define PMD_SHIFT	(PAGE_SHIFT + (PAGE_SHIFT-3))
#define PMD_SIZE	(_AC(1,UL) << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE-1))
#define PMD_BITS	(PAGE_SHIFT - 3)

/* PUD_SHIFT determines the size of the area a third-level page
 * table can map
 */
#define PUD_SHIFT	(PMD_SHIFT + PMD_BITS)
#define PUD_SIZE	(_AC(1,UL) << PUD_SHIFT)
#define PUD_MASK	(~(PUD_SIZE-1))
#define PUD_BITS	(PAGE_SHIFT - 3)

/* PGDIR_SHIFT determines what a fourth-level page table entry can map */
#define PGDIR_SHIFT	(PUD_SHIFT + PUD_BITS)
#define PGDIR_SIZE	(_AC(1,UL) << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))
#define PGDIR_BITS	(PAGE_SHIFT - 3)

#if (MAX_PHYS_ADDRESS_BITS > PGDIR_SHIFT + PGDIR_BITS)
#error MAX_PHYS_ADDRESS_BITS exceeds what kernel page tables can support
#endif

#if (PGDIR_SHIFT + PGDIR_BITS) != 53
#error Page table parameters do not cover virtual address space properly.
#endif

#if (PMD_SHIFT != HPAGE_SHIFT)
#error PMD_SHIFT must equal HPAGE_SHIFT for transparent huge pages.
#endif

#ifndef __ASSEMBLY__

extern unsigned long VMALLOC_END;

#define vmemmap			((struct page *)VMEMMAP_BASE)

#include <linux/sched.h>

bool kern_addr_valid(unsigned long addr);

/* Entries per page directory level. */
#define PTRS_PER_PTE	(1UL << (PAGE_SHIFT-3))
#define PTRS_PER_PMD	(1UL << PMD_BITS)
#define PTRS_PER_PUD	(1UL << PUD_BITS)
#define PTRS_PER_PGD	(1UL << PGDIR_BITS)

/* Kernel has a separate 44bit address space. */
#define FIRST_USER_ADDRESS	0UL

#define pmd_ERROR(e)							\
	pr_err("%s:%d: bad pmd %p(%016lx) seen at (%pS)\n",		\
	       __FILE__, __LINE__, &(e), pmd_val(e), __builtin_return_address(0))
#define pud_ERROR(e)							\
	pr_err("%s:%d: bad pud %p(%016lx) seen at (%pS)\n",		\
	       __FILE__, __LINE__, &(e), pud_val(e), __builtin_return_address(0))
#define pgd_ERROR(e)							\
	pr_err("%s:%d: bad pgd %p(%016lx) seen at (%pS)\n",		\
	       __FILE__, __LINE__, &(e), pgd_val(e), __builtin_return_address(0))

#endif /* !(__ASSEMBLY__) */

/* PTE bits which are the same in SUN4U and SUN4V format.  */
#define _PAGE_VALID	  _AC(0x8000000000000000,UL) /* Valid TTE            */
#define _PAGE_R	  	  _AC(0x8000000000000000,UL) /* Keep ref bit uptodate*/
#define _PAGE_SPECIAL     _AC(0x0200000000000000,UL) /* Special page         */
#define _PAGE_PMD_HUGE    _AC(0x0100000000000000,UL) /* Huge page            */
#define _PAGE_PUD_HUGE    _PAGE_PMD_HUGE

/* SUN4U pte bits... */
#define _PAGE_SZ4MB_4U	  _AC(0x6000000000000000,UL) /* 4MB Page             */
#define _PAGE_SZ512K_4U	  _AC(0x4000000000000000,UL) /* 512K Page            */
#define _PAGE_SZ64K_4U	  _AC(0x2000000000000000,UL) /* 64K Page             */
#define _PAGE_SZ8K_4U	  _AC(0x0000000000000000,UL) /* 8K Page              */
#define _PAGE_NFO_4U	  _AC(0x1000000000000000,UL) /* No Fault Only        */
#define _PAGE_IE_4U	  _AC(0x0800000000000000,UL) /* Invert Endianness    */
#define _PAGE_SOFT2_4U	  _AC(0x07FC000000000000,UL) /* Software bits, set 2 */
#define _PAGE_SPECIAL_4U  _AC(0x0200000000000000,UL) /* Special page         */
#define _PAGE_PMD_HUGE_4U _AC(0x0100000000000000,UL) /* Huge page            */
#define _PAGE_RES1_4U	  _AC(0x0002000000000000,UL) /* Reserved             */
#define _PAGE_SZ32MB_4U	  _AC(0x0001000000000000,UL) /* (Panther) 32MB page  */
#define _PAGE_SZ256MB_4U  _AC(0x2001000000000000,UL) /* (Panther) 256MB page */
#define _PAGE_SZALL_4U	  _AC(0x6001000000000000,UL) /* All pgsz bits        */
#define _PAGE_SN_4U	  _AC(0x0000800000000000,UL) /* (Cheetah) Snoop      */
#define _PAGE_RES2_4U	  _AC(0x0000780000000000,UL) /* Reserved             */
#define _PAGE_PADDR_4U	  _AC(0x000007FFFFFFE000,UL) /* (Cheetah) pa[42:13]  */
#define _PAGE_SOFT_4U	  _AC(0x0000000000001F80,UL) /* Software bits:       */
#define _PAGE_EXEC_4U	  _AC(0x0000000000001000,UL) /* Executable SW bit    */
#define _PAGE_MODIFIED_4U _AC(0x0000000000000800,UL) /* Modified (dirty)     */
#define _PAGE_ACCESSED_4U _AC(0x0000000000000400,UL) /* Accessed (ref'd)     */
#define _PAGE_READ_4U	  _AC(0x0000000000000200,UL) /* Readable SW Bit      */
#define _PAGE_WRITE_4U	  _AC(0x0000000000000100,UL) /* Writable SW Bit      */
#define _PAGE_PRESENT_4U  _AC(0x0000000000000080,UL) /* Present              */
#define _PAGE_L_4U	  _AC(0x0000000000000040,UL) /* Locked TTE           */
#define _PAGE_CP_4U	  _AC(0x0000000000000020,UL) /* Cacheable in P-Cache */
#define _PAGE_CV_4U	  _AC(0x0000000000000010,UL) /* Cacheable in V-Cache */
#define _PAGE_E_4U	  _AC(0x0000000000000008,UL) /* side-Effect          */
#define _PAGE_P_4U	  _AC(0x0000000000000004,UL) /* Privileged Page      */
#define _PAGE_W_4U	  _AC(0x0000000000000002,UL) /* Writable             */

/* SUN4V pte bits... */
#define _PAGE_NFO_4V	  _AC(0x4000000000000000,UL) /* No Fault Only        */
#define _PAGE_SOFT2_4V	  _AC(0x3F00000000000000,UL) /* Software bits, set 2 */
#define _PAGE_MODIFIED_4V _AC(0x2000000000000000,UL) /* Modified (dirty)     */
#define _PAGE_ACCESSED_4V _AC(0x1000000000000000,UL) /* Accessed (ref'd)     */
#define _PAGE_READ_4V	  _AC(0x0800000000000000,UL) /* Readable SW Bit      */
#define _PAGE_WRITE_4V	  _AC(0x0400000000000000,UL) /* Writable SW Bit      */
#define _PAGE_SPECIAL_4V  _AC(0x0200000000000000,UL) /* Special page         */
#define _PAGE_PMD_HUGE_4V _AC(0x0100000000000000,UL) /* Huge page            */
#define _PAGE_PADDR_4V	  _AC(0x00FFFFFFFFFFE000,UL) /* paddr[55:13]         */
#define _PAGE_IE_4V	  _AC(0x0000000000001000,UL) /* Invert Endianness    */
#define _PAGE_E_4V	  _AC(0x0000000000000800,UL) /* side-Effect          */
#define _PAGE_CP_4V	  _AC(0x0000000000000400,UL) /* Cacheable in P-Cache */
#define _PAGE_CV_4V	  _AC(0x0000000000000200,UL) /* Cacheable in V-Cache */
/* Bit 9 is used to enable MCD corruption detection instead on M7 */
#define _PAGE_MCD_4V      _AC(0x0000000000000200,UL) /* Memory Corruption    */
#define _PAGE_P_4V	  _AC(0x0000000000000100,UL) /* Privileged Page      */
#define _PAGE_EXEC_4V	  _AC(0x0000000000000080,UL) /* Executable Page      */
#define _PAGE_W_4V	  _AC(0x0000000000000040,UL) /* Writable             */
#define _PAGE_SOFT_4V	  _AC(0x0000000000000030,UL) /* Software bits        */
#define _PAGE_PRESENT_4V  _AC(0x0000000000000010,UL) /* Present              */
#define _PAGE_RESV_4V	  _AC(0x0000000000000008,UL) /* Reserved             */
#define _PAGE_SZ16GB_4V	  _AC(0x0000000000000007,UL) /* 16GB Page            */
#define _PAGE_SZ2GB_4V	  _AC(0x0000000000000006,UL) /* 2GB Page             */
#define _PAGE_SZ256MB_4V  _AC(0x0000000000000005,UL) /* 256MB Page           */
#define _PAGE_SZ32MB_4V	  _AC(0x0000000000000004,UL) /* 32MB Page            */
#define _PAGE_SZ4MB_4V	  _AC(0x0000000000000003,UL) /* 4MB Page             */
#define _PAGE_SZ512K_4V	  _AC(0x0000000000000002,UL) /* 512K Page            */
#define _PAGE_SZ64K_4V	  _AC(0x0000000000000001,UL) /* 64K Page             */
#define _PAGE_SZ8K_4V	  _AC(0x0000000000000000,UL) /* 8K Page              */
#define _PAGE_SZALL_4V	  _AC(0x0000000000000007,UL) /* All pgsz bits        */

#define _PAGE_SZBITS_4U	_PAGE_SZ8K_4U
#define _PAGE_SZBITS_4V	_PAGE_SZ8K_4V

#if REAL_HPAGE_SHIFT != 22
#error REAL_HPAGE_SHIFT and _PAGE_SZHUGE_foo must match up
#endif

#define _PAGE_SZHUGE_4U	_PAGE_SZ4MB_4U
#define _PAGE_SZHUGE_4V	_PAGE_SZ4MB_4V

/* These are actually filled in at boot time by sun4{u,v}_pgprot_init() */
#define __P000	__pgprot(0)
#define __P001	__pgprot(0)
#define __P010	__pgprot(0)
#define __P011	__pgprot(0)
#define __P100	__pgprot(0)
#define __P101	__pgprot(0)
#define __P110	__pgprot(0)
#define __P111	__pgprot(0)

#define __S000	__pgprot(0)
#define __S001	__pgprot(0)
#define __S010	__pgprot(0)
#define __S011	__pgprot(0)
#define __S100	__pgprot(0)
#define __S101	__pgprot(0)
#define __S110	__pgprot(0)
#define __S111	__pgprot(0)

#ifndef __ASSEMBLY__

pte_t mk_pte_io(unsigned long, pgprot_t, int, unsigned long);

unsigned long pte_sz_bits(unsigned long size);

extern pgprot_t PAGE_KERNEL;
extern pgprot_t PAGE_KERNEL_LOCKED;
extern pgprot_t PAGE_COPY;
extern pgprot_t PAGE_SHARED;

/* XXX This ugliness is for the atyfb driver's sparc mmap() support. XXX */
extern unsigned long _PAGE_IE;
extern unsigned long _PAGE_E;
extern unsigned long _PAGE_CACHE;

extern unsigned long pg_iobits;
extern unsigned long _PAGE_ALL_SZ_BITS;

extern struct page *mem_map_zero;
#define ZERO_PAGE(vaddr)	(mem_map_zero)

/* PFNs are real physical page numbers.  However, mem_map only begins to record
 * per-page information starting at pfn_base.  This is to handle systems where
 * the first physical page in the machine is at some huge physical address,
 * such as 4GB.   This is common on a partitioned E10000, for example.
 */
static inline pte_t pfn_pte(unsigned long pfn, pgprot_t prot)
{
	unsigned long paddr = pfn << PAGE_SHIFT;

	BUILD_BUG_ON(_PAGE_SZBITS_4U != 0UL || _PAGE_SZBITS_4V != 0UL);
	return __pte(paddr | pgprot_val(prot));
}
#define mk_pte(page, pgprot)	pfn_pte(page_to_pfn(page), (pgprot))

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
static inline pmd_t pfn_pmd(unsigned long page_nr, pgprot_t pgprot)
{
	pte_t pte = pfn_pte(page_nr, pgprot);

	return __pmd(pte_val(pte));
}
#define mk_pmd(page, pgprot)	pfn_pmd(page_to_pfn(page), (pgprot))
#endif

/* This one can be done with two shifts.  */
static inline unsigned long pte_pfn(pte_t pte)
{
	unsigned long ret;

	__asm__ __volatile__(
	"\n661:	sllx		%1, %2, %0\n"
	"	srlx		%0, %3, %0\n"
	"	.section	.sun4v_2insn_patch, \"ax\"\n"
	"	.word		661b\n"
	"	sllx		%1, %4, %0\n"
	"	srlx		%0, %5, %0\n"
	"	.previous\n"
	: "=r" (ret)
	: "r" (pte_val(pte)),
	  "i" (21), "i" (21 + PAGE_SHIFT),
	  "i" (8), "i" (8 + PAGE_SHIFT));

	return ret;
}
#define pte_page(x) pfn_to_page(pte_pfn(x))

static inline pte_t pte_modify(pte_t pte, pgprot_t prot)
{
	unsigned long mask, tmp;

	/* SUN4U: 0x630107ffffffec38 (negated == 0x9cfef800000013c7)
	 * SUN4V: 0x33ffffffffffee07 (negated == 0xcc000000000011f8)
	 *
	 * Even if we use negation tricks the result is still a 6
	 * instruction sequence, so don't try to play fancy and just
	 * do the most straightforward implementation.
	 *
	 * Note: We encode this into 3 sun4v 2-insn patch sequences.
	 */

	BUILD_BUG_ON(_PAGE_SZBITS_4U != 0UL || _PAGE_SZBITS_4V != 0UL);
	__asm__ __volatile__(
	"\n661:	sethi		%%uhi(%2), %1\n"
	"	sethi		%%hi(%2), %0\n"
	"\n662:	or		%1, %%ulo(%2), %1\n"
	"	or		%0, %%lo(%2), %0\n"
	"\n663:	sllx		%1, 32, %1\n"
	"	or		%0, %1, %0\n"
	"	.section	.sun4v_2insn_patch, \"ax\"\n"
	"	.word		661b\n"
	"	sethi		%%uhi(%3), %1\n"
	"	sethi		%%hi(%3), %0\n"
	"	.word		662b\n"
	"	or		%1, %%ulo(%3), %1\n"
	"	or		%0, %%lo(%3), %0\n"
	"	.word		663b\n"
	"	sllx		%1, 32, %1\n"
	"	or		%0, %1, %0\n"
	"	.previous\n"
	"	.section	.sun_m7_2insn_patch, \"ax\"\n"
	"	.word		661b\n"
	"	sethi		%%uhi(%4), %1\n"
	"	sethi		%%hi(%4), %0\n"
	"	.word		662b\n"
	"	or		%1, %%ulo(%4), %1\n"
	"	or		%0, %%lo(%4), %0\n"
	"	.word		663b\n"
	"	sllx		%1, 32, %1\n"
	"	or		%0, %1, %0\n"
	"	.previous\n"
	: "=r" (mask), "=r" (tmp)
	: "i" (_PAGE_PADDR_4U | _PAGE_MODIFIED_4U | _PAGE_ACCESSED_4U |
	       _PAGE_CP_4U | _PAGE_CV_4U | _PAGE_E_4U |
	       _PAGE_SPECIAL | _PAGE_PMD_HUGE | _PAGE_SZALL_4U),
	  "i" (_PAGE_PADDR_4V | _PAGE_MODIFIED_4V | _PAGE_ACCESSED_4V |
	       _PAGE_CP_4V | _PAGE_CV_4V | _PAGE_E_4V |
	       _PAGE_SPECIAL | _PAGE_PMD_HUGE | _PAGE_SZALL_4V),
	  "i" (_PAGE_PADDR_4V | _PAGE_MODIFIED_4V | _PAGE_ACCESSED_4V |
	       _PAGE_CP_4V | _PAGE_E_4V |
	       _PAGE_SPECIAL | _PAGE_PMD_HUGE | _PAGE_SZALL_4V));

	return __pte((pte_val(pte) & mask) | (pgprot_val(prot) & ~mask));
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
static inline pmd_t pmd_modify(pmd_t pmd, pgprot_t newprot)
{
	pte_t pte = __pte(pmd_val(pmd));

	pte = pte_modify(pte, newprot);

	return __pmd(pte_val(pte));
}
#endif

static inline pgprot_t pgprot_noncached(pgprot_t prot)
{
	unsigned long val = pgprot_val(prot);

	__asm__ __volatile__(
	"\n661:	andn		%0, %2, %0\n"
	"	or		%0, %3, %0\n"
	"	.section	.sun4v_2insn_patch, \"ax\"\n"
	"	.word		661b\n"
	"	andn		%0, %4, %0\n"
	"	or		%0, %5, %0\n"
	"	.previous\n"
	"	.section	.sun_m7_2insn_patch, \"ax\"\n"
	"	.word		661b\n"
	"	andn		%0, %6, %0\n"
	"	or		%0, %5, %0\n"
	"	.previous\n"
	: "=r" (val)
	: "0" (val), "i" (_PAGE_CP_4U | _PAGE_CV_4U), "i" (_PAGE_E_4U),
	             "i" (_PAGE_CP_4V | _PAGE_CV_4V), "i" (_PAGE_E_4V),
	             "i" (_PAGE_CP_4V));

	return __pgprot(val);
}
/* Various pieces of code check for platform support by ifdef testing
 * on "pgprot_noncached".  That's broken and should be fixed, but for
 * now...
 */
#define pgprot_noncached pgprot_noncached

#if defined(CONFIG_HUGETLB_PAGE) || defined(CONFIG_TRANSPARENT_HUGEPAGE)
extern pte_t arch_make_huge_pte(pte_t entry, struct vm_area_struct *vma,
				struct page *page, int writable);
#define arch_make_huge_pte arch_make_huge_pte
static inline unsigned long __pte_default_huge_mask(void)
{
	unsigned long mask;

	__asm__ __volatile__(
	"\n661:	sethi		%%uhi(%1), %0\n"
	"	sllx		%0, 32, %0\n"
	"	.section	.sun4v_2insn_patch, \"ax\"\n"
	"	.word		661b\n"
	"	mov		%2, %0\n"
	"	nop\n"
	"	.previous\n"
	: "=r" (mask)
	: "i" (_PAGE_SZHUGE_4U), "i" (_PAGE_SZHUGE_4V));

	return mask;
}

static inline pte_t pte_mkhuge(pte_t pte)
{
	return __pte(pte_val(pte) | __pte_default_huge_mask());
}

static inline bool is_default_hugetlb_pte(pte_t pte)
{
	unsigned long mask = __pte_default_huge_mask();

	return (pte_val(pte) & mask) == mask;
}

static inline bool is_hugetlb_pmd(pmd_t pmd)
{
	return !!(pmd_val(pmd) & _PAGE_PMD_HUGE);
}

static inline bool is_hugetlb_pud(pud_t pud)
{
	return !!(pud_val(pud) & _PAGE_PUD_HUGE);
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
static inline pmd_t pmd_mkhuge(pmd_t pmd)
{
	pte_t pte = __pte(pmd_val(pmd));

	pte = pte_mkhuge(pte);
	pte_val(pte) |= _PAGE_PMD_HUGE;

	return __pmd(pte_val(pte));
}
#endif
#else
static inline bool is_hugetlb_pte(pte_t pte)
{
	return false;
}
#endif

static inline pte_t pte_mkdirty(pte_t pte)
{
	unsigned long val = pte_val(pte), tmp;

	__asm__ __volatile__(
	"\n661:	or		%0, %3, %0\n"
	"	nop\n"
	"\n662:	nop\n"
	"	nop\n"
	"	.section	.sun4v_2insn_patch, \"ax\"\n"
	"	.word		661b\n"
	"	sethi		%%uhi(%4), %1\n"
	"	sllx		%1, 32, %1\n"
	"	.word		662b\n"
	"	or		%1, %%lo(%4), %1\n"
	"	or		%0, %1, %0\n"
	"	.previous\n"
	: "=r" (val), "=r" (tmp)
	: "0" (val), "i" (_PAGE_MODIFIED_4U | _PAGE_W_4U),
	  "i" (_PAGE_MODIFIED_4V | _PAGE_W_4V));

	return __pte(val);
}

static inline pte_t pte_mkclean(pte_t pte)
{
	unsigned long val = pte_val(pte), tmp;

	__asm__ __volatile__(
	"\n661:	andn		%0, %3, %0\n"
	"	nop\n"
	"\n662:	nop\n"
	"	nop\n"
	"	.section	.sun4v_2insn_patch, \"ax\"\n"
	"	.word		661b\n"
	"	sethi		%%uhi(%4), %1\n"
	"	sllx		%1, 32, %1\n"
	"	.word		662b\n"
	"	or		%1, %%lo(%4), %1\n"
	"	andn		%0, %1, %0\n"
	"	.previous\n"
	: "=r" (val), "=r" (tmp)
	: "0" (val), "i" (_PAGE_MODIFIED_4U | _PAGE_W_4U),
	  "i" (_PAGE_MODIFIED_4V | _PAGE_W_4V));

	return __pte(val);
}

static inline pte_t pte_mkwrite(pte_t pte)
{
	unsigned long val = pte_val(pte), mask;

	__asm__ __volatile__(
	"\n661:	mov		%1, %0\n"
	"	nop\n"
	"	.section	.sun4v_2insn_patch, \"ax\"\n"
	"	.word		661b\n"
	"	sethi		%%uhi(%2), %0\n"
	"	sllx		%0, 32, %0\n"
	"	.previous\n"
	: "=r" (mask)
	: "i" (_PAGE_WRITE_4U), "i" (_PAGE_WRITE_4V));

	return __pte(val | mask);
}

static inline pte_t pte_wrprotect(pte_t pte)
{
	unsigned long val = pte_val(pte), tmp;

	__asm__ __volatile__(
	"\n661:	andn		%0, %3, %0\n"
	"	nop\n"
	"\n662:	nop\n"
	"	nop\n"
	"	.section	.sun4v_2insn_patch, \"ax\"\n"
	"	.word		661b\n"
	"	sethi		%%uhi(%4), %1\n"
	"	sllx		%1, 32, %1\n"
	"	.word		662b\n"
	"	or		%1, %%lo(%4), %1\n"
	"	andn		%0, %1, %0\n"
	"	.previous\n"
	: "=r" (val), "=r" (tmp)
	: "0" (val), "i" (_PAGE_WRITE_4U | _PAGE_W_4U),
	  "i" (_PAGE_WRITE_4V | _PAGE_W_4V));

	return __pte(val);
}

static inline pte_t pte_mkold(pte_t pte)
{
	unsigned long mask;

	__asm__ __volatile__(
	"\n661:	mov		%1, %0\n"
	"	nop\n"
	"	.section	.sun4v_2insn_patch, \"ax\"\n"
	"	.word		661b\n"
	"	sethi		%%uhi(%2), %0\n"
	"	sllx		%0, 32, %0\n"
	"	.previous\n"
	: "=r" (mask)
	: "i" (_PAGE_ACCESSED_4U), "i" (_PAGE_ACCESSED_4V));

	mask |= _PAGE_R;

	return __pte(pte_val(pte) & ~mask);
}

static inline pte_t pte_mkyoung(pte_t pte)
{
	unsigned long mask;

	__asm__ __volatile__(
	"\n661:	mov		%1, %0\n"
	"	nop\n"
	"	.section	.sun4v_2insn_patch, \"ax\"\n"
	"	.word		661b\n"
	"	sethi		%%uhi(%2), %0\n"
	"	sllx		%0, 32, %0\n"
	"	.previous\n"
	: "=r" (mask)
	: "i" (_PAGE_ACCESSED_4U), "i" (_PAGE_ACCESSED_4V));

	mask |= _PAGE_R;

	return __pte(pte_val(pte) | mask);
}

static inline pte_t pte_mkspecial(pte_t pte)
{
	pte_val(pte) |= _PAGE_SPECIAL;
	return pte;
}

static inline pte_t pte_mkmcd(pte_t pte)
{
	pte_val(pte) |= _PAGE_MCD_4V;
	return pte;
}

static inline pte_t pte_mknotmcd(pte_t pte)
{
	pte_val(pte) &= ~_PAGE_MCD_4V;
	return pte;
}

static inline unsigned long pte_young(pte_t pte)
{
	unsigned long mask;

	__asm__ __volatile__(
	"\n661:	mov		%1, %0\n"
	"	nop\n"
	"	.section	.sun4v_2insn_patch, \"ax\"\n"
	"	.word		661b\n"
	"	sethi		%%uhi(%2), %0\n"
	"	sllx		%0, 32, %0\n"
	"	.previous\n"
	: "=r" (mask)
	: "i" (_PAGE_ACCESSED_4U), "i" (_PAGE_ACCESSED_4V));

	return (pte_val(pte) & mask);
}

static inline unsigned long pte_dirty(pte_t pte)
{
	unsigned long mask;

	__asm__ __volatile__(
	"\n661:	mov		%1, %0\n"
	"	nop\n"
	"	.section	.sun4v_2insn_patch, \"ax\"\n"
	"	.word		661b\n"
	"	sethi		%%uhi(%2), %0\n"
	"	sllx		%0, 32, %0\n"
	"	.previous\n"
	: "=r" (mask)
	: "i" (_PAGE_MODIFIED_4U), "i" (_PAGE_MODIFIED_4V));

	return (pte_val(pte) & mask);
}

static inline unsigned long pte_write(pte_t pte)
{
	unsigned long mask;

	__asm__ __volatile__(
	"\n661:	mov		%1, %0\n"
	"	nop\n"
	"	.section	.sun4v_2insn_patch, \"ax\"\n"
	"	.word		661b\n"
	"	sethi		%%uhi(%2), %0\n"
	"	sllx		%0, 32, %0\n"
	"	.previous\n"
	: "=r" (mask)
	: "i" (_PAGE_WRITE_4U), "i" (_PAGE_WRITE_4V));

	return (pte_val(pte) & mask);
}

static inline unsigned long pte_exec(pte_t pte)
{
	unsigned long mask;

	__asm__ __volatile__(
	"\n661:	sethi		%%hi(%1), %0\n"
	"	.section	.sun4v_1insn_patch, \"ax\"\n"
	"	.word		661b\n"
	"	mov		%2, %0\n"
	"	.previous\n"
	: "=r" (mask)
	: "i" (_PAGE_EXEC_4U), "i" (_PAGE_EXEC_4V));

	return (pte_val(pte) & mask);
}

static inline unsigned long pte_present(pte_t pte)
{
	unsigned long val = pte_val(pte);

	__asm__ __volatile__(
	"\n661:	and		%0, %2, %0\n"
	"	.section	.sun4v_1insn_patch, \"ax\"\n"
	"	.word		661b\n"
	"	and		%0, %3, %0\n"
	"	.previous\n"
	: "=r" (val)
	: "0" (val), "i" (_PAGE_PRESENT_4U), "i" (_PAGE_PRESENT_4V));

	return val;
}

#define pte_accessible pte_accessible
static inline unsigned long pte_accessible(struct mm_struct *mm, pte_t a)
{
	return pte_val(a) & _PAGE_VALID;
}

static inline unsigned long pte_special(pte_t pte)
{
	return pte_val(pte) & _PAGE_SPECIAL;
}

#define pmd_leaf	pmd_large
static inline unsigned long pmd_large(pmd_t pmd)
{
	pte_t pte = __pte(pmd_val(pmd));

	return pte_val(pte) & _PAGE_PMD_HUGE;
}

static inline unsigned long pmd_pfn(pmd_t pmd)
{
	pte_t pte = __pte(pmd_val(pmd));

	return pte_pfn(pte);
}

#define pmd_write pmd_write
static inline unsigned long pmd_write(pmd_t pmd)
{
	pte_t pte = __pte(pmd_val(pmd));

	return pte_write(pte);
}

#define pud_write(pud)	pte_write(__pte(pud_val(pud)))

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
static inline unsigned long pmd_dirty(pmd_t pmd)
{
	pte_t pte = __pte(pmd_val(pmd));

	return pte_dirty(pte);
}

static inline unsigned long pmd_young(pmd_t pmd)
{
	pte_t pte = __pte(pmd_val(pmd));

	return pte_young(pte);
}

static inline unsigned long pmd_trans_huge(pmd_t pmd)
{
	pte_t pte = __pte(pmd_val(pmd));

	return pte_val(pte) & _PAGE_PMD_HUGE;
}

static inline pmd_t pmd_mkold(pmd_t pmd)
{
	pte_t pte = __pte(pmd_val(pmd));

	pte = pte_mkold(pte);

	return __pmd(pte_val(pte));
}

static inline pmd_t pmd_wrprotect(pmd_t pmd)
{
	pte_t pte = __pte(pmd_val(pmd));

	pte = pte_wrprotect(pte);

	return __pmd(pte_val(pte));
}

static inline pmd_t pmd_mkdirty(pmd_t pmd)
{
	pte_t pte = __pte(pmd_val(pmd));

	pte = pte_mkdirty(pte);

	return __pmd(pte_val(pte));
}

static inline pmd_t pmd_mkclean(pmd_t pmd)
{
	pte_t pte = __pte(pmd_val(pmd));

	pte = pte_mkclean(pte);

	return __pmd(pte_val(pte));
}

static inline pmd_t pmd_mkyoung(pmd_t pmd)
{
	pte_t pte = __pte(pmd_val(pmd));

	pte = pte_mkyoung(pte);

	return __pmd(pte_val(pte));
}

static inline pmd_t pmd_mkwrite(pmd_t pmd)
{
	pte_t pte = __pte(pmd_val(pmd));

	pte = pte_mkwrite(pte);

	return __pmd(pte_val(pte));
}

static inline pgprot_t pmd_pgprot(pmd_t entry)
{
	unsigned long val = pmd_val(entry);

	return __pgprot(val);
}
#endif

static inline int pmd_present(pmd_t pmd)
{
	return pmd_val(pmd) != 0UL;
}

#define pmd_none(pmd)			(!pmd_val(pmd))

/* pmd_bad() is only called on non-trans-huge PMDs.  Our encoding is
 * very simple, it's just the physical address.  PTE tables are of
 * size PAGE_SIZE so make sure the sub-PAGE_SIZE bits are clear and
 * the top bits outside of the range of any physical address size we
 * support are clear as well.  We also validate the physical itself.
 */
#define pmd_bad(pmd)			(pmd_val(pmd) & ~PAGE_MASK)

#define pud_none(pud)			(!pud_val(pud))

#define pud_bad(pud)			(pud_val(pud) & ~PAGE_MASK)

#define p4d_none(p4d)			(!p4d_val(p4d))

#define p4d_bad(p4d)			(p4d_val(p4d) & ~PAGE_MASK)

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
void set_pmd_at(struct mm_struct *mm, unsigned long addr,
		pmd_t *pmdp, pmd_t pmd);
#else
static inline void set_pmd_at(struct mm_struct *mm, unsigned long addr,
			      pmd_t *pmdp, pmd_t pmd)
{
	*pmdp = pmd;
}
#endif

static inline void pmd_set(struct mm_struct *mm, pmd_t *pmdp, pte_t *ptep)
{
	unsigned long val = __pa((unsigned long) (ptep));

	pmd_val(*pmdp) = val;
}

#define pud_set(pudp, pmdp)	\
	(pud_val(*(pudp)) = (__pa((unsigned long) (pmdp))))
static inline unsigned long pmd_page_vaddr(pmd_t pmd)
{
	pte_t pte = __pte(pmd_val(pmd));
	unsigned long pfn;

	pfn = pte_pfn(pte);

	return ((unsigned long) __va(pfn << PAGE_SHIFT));
}

static inline pmd_t *pud_pgtable(pud_t pud)
{
	pte_t pte = __pte(pud_val(pud));
	unsigned long pfn;

	pfn = pte_pfn(pte);

	return ((pmd_t *) __va(pfn << PAGE_SHIFT));
}

#define pmd_page(pmd) 			virt_to_page((void *)pmd_page_vaddr(pmd))
#define pud_page(pud)			virt_to_page((void *)pud_pgtable(pud))
#define pmd_clear(pmdp)			(pmd_val(*(pmdp)) = 0UL)
#define pud_present(pud)		(pud_val(pud) != 0U)
#define pud_clear(pudp)			(pud_val(*(pudp)) = 0UL)
#define p4d_page_vaddr(p4d)		\
	((unsigned long) __va(p4d_val(p4d)))
#define p4d_present(p4d)		(p4d_val(p4d) != 0U)
#define p4d_clear(p4dp)			(p4d_val(*(p4dp)) = 0UL)

/* only used by the stubbed out hugetlb gup code, should never be called */
#define p4d_page(p4d)			NULL

#define pud_leaf	pud_large
static inline unsigned long pud_large(pud_t pud)
{
	pte_t pte = __pte(pud_val(pud));

	return pte_val(pte) & _PAGE_PMD_HUGE;
}

static inline unsigned long pud_pfn(pud_t pud)
{
	pte_t pte = __pte(pud_val(pud));

	return pte_pfn(pte);
}

/* Same in both SUN4V and SUN4U.  */
#define pte_none(pte) 			(!pte_val(pte))

#define p4d_set(p4dp, pudp)	\
	(p4d_val(*(p4dp)) = (__pa((unsigned long) (pudp))))

/* We cannot include <linux/mm_types.h> at this point yet: */
extern struct mm_struct init_mm;

/* Actual page table PTE updates.  */
void tlb_batch_add(struct mm_struct *mm, unsigned long vaddr,
		   pte_t *ptep, pte_t orig, int fullmm,
		   unsigned int hugepage_shift);

static void maybe_tlb_batch_add(struct mm_struct *mm, unsigned long vaddr,
				pte_t *ptep, pte_t orig, int fullmm,
				unsigned int hugepage_shift)
{
	/* It is more efficient to let flush_tlb_kernel_range()
	 * handle init_mm tlb flushes.
	 *
	 * SUN4V NOTE: _PAGE_VALID is the same value in both the SUN4U
	 *             and SUN4V pte layout, so this inline test is fine.
	 */
	if (likely(mm != &init_mm) && pte_accessible(mm, orig))
		tlb_batch_add(mm, vaddr, ptep, orig, fullmm, hugepage_shift);
}

#define __HAVE_ARCH_PMDP_HUGE_GET_AND_CLEAR
static inline pmd_t pmdp_huge_get_and_clear(struct mm_struct *mm,
					    unsigned long addr,
					    pmd_t *pmdp)
{
	pmd_t pmd = *pmdp;
	set_pmd_at(mm, addr, pmdp, __pmd(0UL));
	return pmd;
}

static inline void __set_pte_at(struct mm_struct *mm, unsigned long addr,
			     pte_t *ptep, pte_t pte, int fullmm)
{
	pte_t orig = *ptep;

	*ptep = pte;
	maybe_tlb_batch_add(mm, addr, ptep, orig, fullmm, PAGE_SHIFT);
}

#define set_pte_at(mm,addr,ptep,pte)	\
	__set_pte_at((mm), (addr), (ptep), (pte), 0)

#define pte_clear(mm,addr,ptep)		\
	set_pte_at((mm), (addr), (ptep), __pte(0UL))

#define __HAVE_ARCH_PTE_CLEAR_NOT_PRESENT_FULL
#define pte_clear_not_present_full(mm,addr,ptep,fullmm)	\
	__set_pte_at((mm), (addr), (ptep), __pte(0UL), (fullmm))

#ifdef DCACHE_ALIASING_POSSIBLE
#define __HAVE_ARCH_MOVE_PTE
#define move_pte(pte, prot, old_addr, new_addr)				\
({									\
	pte_t newpte = (pte);						\
	if (tlb_type != hypervisor && pte_present(pte)) {		\
		unsigned long this_pfn = pte_pfn(pte);			\
									\
		if (pfn_valid(this_pfn) &&				\
		    (((old_addr) ^ (new_addr)) & (1 << 13)))		\
			flush_dcache_page_all(current->mm,		\
					      pfn_to_page(this_pfn));	\
	}								\
	newpte;								\
})
#endif

extern pgd_t swapper_pg_dir[PTRS_PER_PGD];

void paging_init(void);
unsigned long find_ecache_flush_span(unsigned long size);

struct seq_file;
void mmu_info(struct seq_file *);

struct vm_area_struct;
void update_mmu_cache(struct vm_area_struct *, unsigned long, pte_t *);
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
void update_mmu_cache_pmd(struct vm_area_struct *vma, unsigned long addr,
			  pmd_t *pmd);

#define __HAVE_ARCH_PMDP_INVALIDATE
extern pmd_t pmdp_invalidate(struct vm_area_struct *vma, unsigned long address,
			    pmd_t *pmdp);

#define __HAVE_ARCH_PGTABLE_DEPOSIT
void pgtable_trans_huge_deposit(struct mm_struct *mm, pmd_t *pmdp,
				pgtable_t pgtable);

#define __HAVE_ARCH_PGTABLE_WITHDRAW
pgtable_t pgtable_trans_huge_withdraw(struct mm_struct *mm, pmd_t *pmdp);
#endif

/* Encode and de-code a swap entry */
#define __swp_type(entry)	(((entry).val >> PAGE_SHIFT) & 0xffUL)
#define __swp_offset(entry)	((entry).val >> (PAGE_SHIFT + 8UL))
#define __swp_entry(type, offset)	\
	( (swp_entry_t) \
	  { \
		(((long)(type) << PAGE_SHIFT) | \
                 ((long)(offset) << (PAGE_SHIFT + 8UL))) \
	  } )
#define __pte_to_swp_entry(pte)		((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(x)		((pte_t) { (x).val })

int page_in_phys_avail(unsigned long paddr);

/*
 * For sparc32&64, the pfn in io_remap_pfn_range() carries <iospace> in
 * its high 4 bits.  These macros/functions put it there or get it from there.
 */
#define MK_IOSPACE_PFN(space, pfn)	(pfn | (space << (BITS_PER_LONG - 4)))
#define GET_IOSPACE(pfn)		(pfn >> (BITS_PER_LONG - 4))
#define GET_PFN(pfn)			(pfn & 0x0fffffffffffffffUL)

int remap_pfn_range(struct vm_area_struct *, unsigned long, unsigned long,
		    unsigned long, pgprot_t);

void adi_restore_tags(struct mm_struct *mm, struct vm_area_struct *vma,
		      unsigned long addr, pte_t pte);

int adi_save_tags(struct mm_struct *mm, struct vm_area_struct *vma,
		  unsigned long addr, pte_t oldpte);

#define __HAVE_ARCH_DO_SWAP_PAGE
static inline void arch_do_swap_page(struct mm_struct *mm,
				     struct vm_area_struct *vma,
				     unsigned long addr,
				     pte_t pte, pte_t oldpte)
{
	/* If this is a new page being mapped in, there can be no
	 * ADI tags stored away for this page. Skip looking for
	 * stored tags
	 */
	if (pte_none(oldpte))
		return;

	if (adi_state.enabled && (pte_val(pte) & _PAGE_MCD_4V))
		adi_restore_tags(mm, vma, addr, pte);
}

#define __HAVE_ARCH_UNMAP_ONE
static inline int arch_unmap_one(struct mm_struct *mm,
				 struct vm_area_struct *vma,
				 unsigned long addr, pte_t oldpte)
{
	if (adi_state.enabled && (pte_val(oldpte) & _PAGE_MCD_4V))
		return adi_save_tags(mm, vma, addr, oldpte);
	return 0;
}

static inline int io_remap_pfn_range(struct vm_area_struct *vma,
				     unsigned long from, unsigned long pfn,
				     unsigned long size, pgprot_t prot)
{
	unsigned long offset = GET_PFN(pfn) << PAGE_SHIFT;
	int space = GET_IOSPACE(pfn);
	unsigned long phys_base;

	phys_base = offset | (((unsigned long) space) << 32UL);

	return remap_pfn_range(vma, from, phys_base >> PAGE_SHIFT, size, prot);
}
#define io_remap_pfn_range io_remap_pfn_range

static inline unsigned long __untagged_addr(unsigned long start)
{
	if (adi_capable()) {
		long addr = start;

		/* If userspace has passed a versioned address, kernel
		 * will not find it in the VMAs since it does not store
		 * the version tags in the list of VMAs. Storing version
		 * tags in list of VMAs is impractical since they can be
		 * changed any time from userspace without dropping into
		 * kernel. Any address search in VMAs will be done with
		 * non-versioned addresses. Ensure the ADI version bits
		 * are dropped here by sign extending the last bit before
		 * ADI bits. IOMMU does not implement version tags.
		 */
		return (addr << (long)adi_nbits()) >> (long)adi_nbits();
	}

	return start;
}
#define untagged_addr(addr) \
	((__typeof__(addr))(__untagged_addr((unsigned long)(addr))))

static inline bool pte_access_permitted(pte_t pte, bool write)
{
	u64 prot;

	if (tlb_type == hypervisor) {
		prot = _PAGE_PRESENT_4V | _PAGE_P_4V;
		if (write)
			prot |= _PAGE_WRITE_4V;
	} else {
		prot = _PAGE_PRESENT_4U | _PAGE_P_4U;
		if (write)
			prot |= _PAGE_WRITE_4U;
	}

	return (pte_val(pte) & (prot | _PAGE_SPECIAL)) == prot;
}
#define pte_access_permitted pte_access_permitted

#include <asm/tlbflush.h>

/* We provide our own get_unmapped_area to cope with VA holes and
 * SHM area cache aliasing for userland.
 */
#define HAVE_ARCH_UNMAPPED_AREA
#define HAVE_ARCH_UNMAPPED_AREA_TOPDOWN

/* We provide a special get_unmapped_area for framebuffer mmaps to try and use
 * the largest alignment possible such that larget PTEs can be used.
 */
unsigned long get_fb_unmapped_area(struct file *filp, unsigned long,
				   unsigned long, unsigned long,
				   unsigned long);
#define HAVE_ARCH_FB_UNMAPPED_AREA

void sun4v_register_fault_status(void);
void sun4v_ktsb_register(void);
void __init cheetah_ecache_flush_init(void);
void sun4v_patch_tlb_handlers(void);

extern unsigned long cmdline_memory_size;

asmlinkage void do_sparc64_fault(struct pt_regs *regs);

#endif /* !(__ASSEMBLY__) */

#endif /* !(_SPARC64_PGTABLE_H) */
