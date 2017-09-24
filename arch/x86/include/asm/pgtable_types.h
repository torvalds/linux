#ifndef _ASM_X86_PGTABLE_DEFS_H
#define _ASM_X86_PGTABLE_DEFS_H

#include <linux/const.h>
#include <asm/page_types.h>

#define FIRST_USER_ADDRESS	0UL

#define _PAGE_BIT_PRESENT	0	/* is present */
#define _PAGE_BIT_RW		1	/* writeable */
#define _PAGE_BIT_USER		2	/* userspace addressable */
#define _PAGE_BIT_PWT		3	/* page write through */
#define _PAGE_BIT_PCD		4	/* page cache disabled */
#define _PAGE_BIT_ACCESSED	5	/* was accessed (raised by CPU) */
#define _PAGE_BIT_DIRTY		6	/* was written to (raised by CPU) */
#define _PAGE_BIT_PSE		7	/* 4 MB (or 2MB) page */
#define _PAGE_BIT_PAT		7	/* on 4KB pages */
#define _PAGE_BIT_GLOBAL	8	/* Global TLB entry PPro+ */
#define _PAGE_BIT_SOFTW1	9	/* available for programmer */
#define _PAGE_BIT_SOFTW2	10	/* " */
#define _PAGE_BIT_SOFTW3	11	/* " */
#define _PAGE_BIT_PAT_LARGE	12	/* On 2MB or 1GB pages */
#define _PAGE_BIT_SPECIAL	_PAGE_BIT_SOFTW1
#define _PAGE_BIT_CPA_TEST	_PAGE_BIT_SOFTW1
#define _PAGE_BIT_SPLITTING	_PAGE_BIT_SOFTW2 /* only valid on a PSE pmd */
#define _PAGE_BIT_HIDDEN	_PAGE_BIT_SOFTW3 /* hidden by kmemcheck */
#define _PAGE_BIT_SOFT_DIRTY	_PAGE_BIT_SOFTW3 /* software dirty tracking */
#define _PAGE_BIT_NX           63       /* No execute: only valid after cpuid check */

/* If _PAGE_BIT_PRESENT is clear, we use these: */
/* - if the user mapped it with PROT_NONE; pte_present gives true */
#define _PAGE_BIT_PROTNONE	_PAGE_BIT_GLOBAL

#define _PAGE_PRESENT	(_AT(pteval_t, 1) << _PAGE_BIT_PRESENT)
#define _PAGE_RW	(_AT(pteval_t, 1) << _PAGE_BIT_RW)
#define _PAGE_USER	(_AT(pteval_t, 1) << _PAGE_BIT_USER)
#define _PAGE_PWT	(_AT(pteval_t, 1) << _PAGE_BIT_PWT)
#define _PAGE_PCD	(_AT(pteval_t, 1) << _PAGE_BIT_PCD)
#define _PAGE_ACCESSED	(_AT(pteval_t, 1) << _PAGE_BIT_ACCESSED)
#define _PAGE_DIRTY	(_AT(pteval_t, 1) << _PAGE_BIT_DIRTY)
#define _PAGE_PSE	(_AT(pteval_t, 1) << _PAGE_BIT_PSE)
#define _PAGE_GLOBAL	(_AT(pteval_t, 1) << _PAGE_BIT_GLOBAL)
#define _PAGE_SOFTW1	(_AT(pteval_t, 1) << _PAGE_BIT_SOFTW1)
#define _PAGE_SOFTW2	(_AT(pteval_t, 1) << _PAGE_BIT_SOFTW2)
#define _PAGE_PAT	(_AT(pteval_t, 1) << _PAGE_BIT_PAT)
#define _PAGE_PAT_LARGE (_AT(pteval_t, 1) << _PAGE_BIT_PAT_LARGE)
#define _PAGE_SPECIAL	(_AT(pteval_t, 1) << _PAGE_BIT_SPECIAL)
#define _PAGE_CPA_TEST	(_AT(pteval_t, 1) << _PAGE_BIT_CPA_TEST)
#define _PAGE_SPLITTING	(_AT(pteval_t, 1) << _PAGE_BIT_SPLITTING)
#define __HAVE_ARCH_PTE_SPECIAL

#ifdef CONFIG_KMEMCHECK
#define _PAGE_HIDDEN	(_AT(pteval_t, 1) << _PAGE_BIT_HIDDEN)
#else
#define _PAGE_HIDDEN	(_AT(pteval_t, 0))
#endif

/*
 * The same hidden bit is used by kmemcheck, but since kmemcheck
 * works on kernel pages while soft-dirty engine on user space,
 * they do not conflict with each other.
 */

#ifdef CONFIG_MEM_SOFT_DIRTY
#define _PAGE_SOFT_DIRTY	(_AT(pteval_t, 1) << _PAGE_BIT_SOFT_DIRTY)
#else
#define _PAGE_SOFT_DIRTY	(_AT(pteval_t, 0))
#endif

/*
 * Tracking soft dirty bit when a page goes to a swap is tricky.
 * We need a bit which can be stored in pte _and_ not conflict
 * with swap entry format. On x86 bits 6 and 7 are *not* involved
 * into swap entry computation, but bit 6 is used for nonlinear
 * file mapping, so we borrow bit 7 for soft dirty tracking.
 *
 * Please note that this bit must be treated as swap dirty page
 * mark if and only if the PTE has present bit clear!
 */
#ifdef CONFIG_MEM_SOFT_DIRTY
#define _PAGE_SWP_SOFT_DIRTY	_PAGE_PSE
#else
#define _PAGE_SWP_SOFT_DIRTY	(_AT(pteval_t, 0))
#endif

#if defined(CONFIG_X86_64) || defined(CONFIG_X86_PAE)
#define _PAGE_NX	(_AT(pteval_t, 1) << _PAGE_BIT_NX)
#else
#define _PAGE_NX	(_AT(pteval_t, 0))
#endif

#define _PAGE_PROTNONE  (_AT(pteval_t, 1) << _PAGE_BIT_PROTNONE)

#define _PAGE_TABLE	(_PAGE_PRESENT | _PAGE_RW | _PAGE_USER |	\
			 _PAGE_ACCESSED | _PAGE_DIRTY)
#define _KERNPG_TABLE	(_PAGE_PRESENT | _PAGE_RW | _PAGE_ACCESSED |	\
			 _PAGE_DIRTY)

/* Set of bits not changed in pte_modify */
#define _PAGE_CHG_MASK	(PTE_PFN_MASK | _PAGE_PCD | _PAGE_PWT |		\
			 _PAGE_SPECIAL | _PAGE_ACCESSED | _PAGE_DIRTY |	\
			 _PAGE_SOFT_DIRTY)
#define _HPAGE_CHG_MASK (_PAGE_CHG_MASK | _PAGE_PSE)

/* The ASID is the lower 12 bits of CR3 */
#define X86_CR3_PCID_ASID_MASK  (_AC((1<<12)-1,UL))

/* Mask for all the PCID-related bits in CR3: */
#define X86_CR3_PCID_MASK       (X86_CR3_PCID_NOFLUSH | X86_CR3_PCID_ASID_MASK)
#define X86_CR3_PCID_ASID_KERN  (_AC(0x0,UL))

#if defined(CONFIG_KAISER) && defined(CONFIG_X86_64)
/* Let X86_CR3_PCID_ASID_USER be usable for the X86_CR3_PCID_NOFLUSH bit */
#define X86_CR3_PCID_ASID_USER	(_AC(0x80,UL))

#define X86_CR3_PCID_KERN_FLUSH		(X86_CR3_PCID_ASID_KERN)
#define X86_CR3_PCID_USER_FLUSH		(X86_CR3_PCID_ASID_USER)
#define X86_CR3_PCID_KERN_NOFLUSH	(X86_CR3_PCID_NOFLUSH | X86_CR3_PCID_ASID_KERN)
#define X86_CR3_PCID_USER_NOFLUSH	(X86_CR3_PCID_NOFLUSH | X86_CR3_PCID_ASID_USER)
#else
#define X86_CR3_PCID_ASID_USER  (_AC(0x0,UL))
/*
 * PCIDs are unsupported on 32-bit and none of these bits can be
 * set in CR3:
 */
#define X86_CR3_PCID_KERN_FLUSH		(0)
#define X86_CR3_PCID_USER_FLUSH		(0)
#define X86_CR3_PCID_KERN_NOFLUSH	(0)
#define X86_CR3_PCID_USER_NOFLUSH	(0)
#endif

/*
 * The cache modes defined here are used to translate between pure SW usage
 * and the HW defined cache mode bits and/or PAT entries.
 *
 * The resulting bits for PWT, PCD and PAT should be chosen in a way
 * to have the WB mode at index 0 (all bits clear). This is the default
 * right now and likely would break too much if changed.
 */
#ifndef __ASSEMBLY__
enum page_cache_mode {
	_PAGE_CACHE_MODE_WB = 0,
	_PAGE_CACHE_MODE_WC = 1,
	_PAGE_CACHE_MODE_UC_MINUS = 2,
	_PAGE_CACHE_MODE_UC = 3,
	_PAGE_CACHE_MODE_WT = 4,
	_PAGE_CACHE_MODE_WP = 5,
	_PAGE_CACHE_MODE_NUM = 8
};
#endif

#define _PAGE_CACHE_MASK	(_PAGE_PAT | _PAGE_PCD | _PAGE_PWT)
#define _PAGE_NOCACHE		(cachemode2protval(_PAGE_CACHE_MODE_UC))

#define PAGE_NONE	__pgprot(_PAGE_PROTNONE | _PAGE_ACCESSED)
#define PAGE_SHARED	__pgprot(_PAGE_PRESENT | _PAGE_RW | _PAGE_USER | \
				 _PAGE_ACCESSED | _PAGE_NX)

#define PAGE_SHARED_EXEC	__pgprot(_PAGE_PRESENT | _PAGE_RW |	\
					 _PAGE_USER | _PAGE_ACCESSED)
#define PAGE_COPY_NOEXEC	__pgprot(_PAGE_PRESENT | _PAGE_USER |	\
					 _PAGE_ACCESSED | _PAGE_NX)
#define PAGE_COPY_EXEC		__pgprot(_PAGE_PRESENT | _PAGE_USER |	\
					 _PAGE_ACCESSED)
#define PAGE_COPY		PAGE_COPY_NOEXEC
#define PAGE_READONLY		__pgprot(_PAGE_PRESENT | _PAGE_USER |	\
					 _PAGE_ACCESSED | _PAGE_NX)
#define PAGE_READONLY_EXEC	__pgprot(_PAGE_PRESENT | _PAGE_USER |	\
					 _PAGE_ACCESSED)

#define __PAGE_KERNEL_EXEC						\
	(_PAGE_PRESENT | _PAGE_RW | _PAGE_DIRTY | _PAGE_ACCESSED | _PAGE_GLOBAL)
#define __PAGE_KERNEL		(__PAGE_KERNEL_EXEC | _PAGE_NX)

#define __PAGE_KERNEL_RO		(__PAGE_KERNEL & ~_PAGE_RW)
#define __PAGE_KERNEL_RX		(__PAGE_KERNEL_EXEC & ~_PAGE_RW)
#define __PAGE_KERNEL_NOCACHE		(__PAGE_KERNEL | _PAGE_NOCACHE)
#define __PAGE_KERNEL_VSYSCALL		(__PAGE_KERNEL_RX | _PAGE_USER)
#define __PAGE_KERNEL_VVAR		(__PAGE_KERNEL_RO | _PAGE_USER)
#define __PAGE_KERNEL_LARGE		(__PAGE_KERNEL | _PAGE_PSE)
#define __PAGE_KERNEL_LARGE_EXEC	(__PAGE_KERNEL_EXEC | _PAGE_PSE)

#define __PAGE_KERNEL_IO		(__PAGE_KERNEL)
#define __PAGE_KERNEL_IO_NOCACHE	(__PAGE_KERNEL_NOCACHE)

#define PAGE_KERNEL			__pgprot(__PAGE_KERNEL)
#define PAGE_KERNEL_RO			__pgprot(__PAGE_KERNEL_RO)
#define PAGE_KERNEL_EXEC		__pgprot(__PAGE_KERNEL_EXEC)
#define PAGE_KERNEL_RX			__pgprot(__PAGE_KERNEL_RX)
#define PAGE_KERNEL_NOCACHE		__pgprot(__PAGE_KERNEL_NOCACHE)
#define PAGE_KERNEL_LARGE		__pgprot(__PAGE_KERNEL_LARGE)
#define PAGE_KERNEL_LARGE_EXEC		__pgprot(__PAGE_KERNEL_LARGE_EXEC)
#define PAGE_KERNEL_VSYSCALL		__pgprot(__PAGE_KERNEL_VSYSCALL)
#define PAGE_KERNEL_VVAR		__pgprot(__PAGE_KERNEL_VVAR)

#define PAGE_KERNEL_IO			__pgprot(__PAGE_KERNEL_IO)
#define PAGE_KERNEL_IO_NOCACHE		__pgprot(__PAGE_KERNEL_IO_NOCACHE)

/*         xwr */
#define __P000	PAGE_NONE
#define __P001	PAGE_READONLY
#define __P010	PAGE_COPY
#define __P011	PAGE_COPY
#define __P100	PAGE_READONLY_EXEC
#define __P101	PAGE_READONLY_EXEC
#define __P110	PAGE_COPY_EXEC
#define __P111	PAGE_COPY_EXEC

#define __S000	PAGE_NONE
#define __S001	PAGE_READONLY
#define __S010	PAGE_SHARED
#define __S011	PAGE_SHARED
#define __S100	PAGE_READONLY_EXEC
#define __S101	PAGE_READONLY_EXEC
#define __S110	PAGE_SHARED_EXEC
#define __S111	PAGE_SHARED_EXEC

/*
 * early identity mapping  pte attrib macros.
 */
#ifdef CONFIG_X86_64
#define __PAGE_KERNEL_IDENT_LARGE_EXEC	__PAGE_KERNEL_LARGE_EXEC
#else
#define PTE_IDENT_ATTR	 0x003		/* PRESENT+RW */
#define PDE_IDENT_ATTR	 0x063		/* PRESENT+RW+DIRTY+ACCESSED */
#define PGD_IDENT_ATTR	 0x001		/* PRESENT (no other attributes) */
#endif

#ifdef CONFIG_X86_32
# include <asm/pgtable_32_types.h>
#else
# include <asm/pgtable_64_types.h>
#endif

#ifndef __ASSEMBLY__

#include <linux/types.h>

/* Extracts the PFN from a (pte|pmd|pud|pgd)val_t of a 4KB page */
#define PTE_PFN_MASK		((pteval_t)PHYSICAL_PAGE_MASK)

/* Extracts the flags from a (pte|pmd|pud|pgd)val_t of a 4KB page */
#define PTE_FLAGS_MASK		(~PTE_PFN_MASK)

typedef struct pgprot { pgprotval_t pgprot; } pgprot_t;

typedef struct { pgdval_t pgd; } pgd_t;

static inline pgd_t native_make_pgd(pgdval_t val)
{
	return (pgd_t) { val };
}

static inline pgdval_t native_pgd_val(pgd_t pgd)
{
	return pgd.pgd;
}

static inline pgdval_t pgd_flags(pgd_t pgd)
{
	return native_pgd_val(pgd) & PTE_FLAGS_MASK;
}

#if CONFIG_PGTABLE_LEVELS > 3
typedef struct { pudval_t pud; } pud_t;

static inline pud_t native_make_pud(pmdval_t val)
{
	return (pud_t) { val };
}

static inline pudval_t native_pud_val(pud_t pud)
{
	return pud.pud;
}
#else
#include <asm-generic/pgtable-nopud.h>

static inline pudval_t native_pud_val(pud_t pud)
{
	return native_pgd_val(pud.pgd);
}
#endif

#if CONFIG_PGTABLE_LEVELS > 2
typedef struct { pmdval_t pmd; } pmd_t;

static inline pmd_t native_make_pmd(pmdval_t val)
{
	return (pmd_t) { val };
}

static inline pmdval_t native_pmd_val(pmd_t pmd)
{
	return pmd.pmd;
}
#else
#include <asm-generic/pgtable-nopmd.h>

static inline pmdval_t native_pmd_val(pmd_t pmd)
{
	return native_pgd_val(pmd.pud.pgd);
}
#endif

static inline pudval_t pud_pfn_mask(pud_t pud)
{
	if (native_pud_val(pud) & _PAGE_PSE)
		return PHYSICAL_PUD_PAGE_MASK;
	else
		return PTE_PFN_MASK;
}

static inline pudval_t pud_flags_mask(pud_t pud)
{
	return ~pud_pfn_mask(pud);
}

static inline pudval_t pud_flags(pud_t pud)
{
	return native_pud_val(pud) & pud_flags_mask(pud);
}

static inline pmdval_t pmd_pfn_mask(pmd_t pmd)
{
	if (native_pmd_val(pmd) & _PAGE_PSE)
		return PHYSICAL_PMD_PAGE_MASK;
	else
		return PTE_PFN_MASK;
}

static inline pmdval_t pmd_flags_mask(pmd_t pmd)
{
	return ~pmd_pfn_mask(pmd);
}

static inline pmdval_t pmd_flags(pmd_t pmd)
{
	return native_pmd_val(pmd) & pmd_flags_mask(pmd);
}

static inline pte_t native_make_pte(pteval_t val)
{
	return (pte_t) { .pte = val };
}

static inline pteval_t native_pte_val(pte_t pte)
{
	return pte.pte;
}

static inline pteval_t pte_flags(pte_t pte)
{
	return native_pte_val(pte) & PTE_FLAGS_MASK;
}

#define pgprot_val(x)	((x).pgprot)
#define __pgprot(x)	((pgprot_t) { (x) } )

extern uint16_t __cachemode2pte_tbl[_PAGE_CACHE_MODE_NUM];
extern uint8_t __pte2cachemode_tbl[8];

#define __pte2cm_idx(cb)				\
	((((cb) >> (_PAGE_BIT_PAT - 2)) & 4) |		\
	 (((cb) >> (_PAGE_BIT_PCD - 1)) & 2) |		\
	 (((cb) >> _PAGE_BIT_PWT) & 1))
#define __cm_idx2pte(i)					\
	((((i) & 4) << (_PAGE_BIT_PAT - 2)) |		\
	 (((i) & 2) << (_PAGE_BIT_PCD - 1)) |		\
	 (((i) & 1) << _PAGE_BIT_PWT))

static inline unsigned long cachemode2protval(enum page_cache_mode pcm)
{
	if (likely(pcm == 0))
		return 0;
	return __cachemode2pte_tbl[pcm];
}
static inline pgprot_t cachemode2pgprot(enum page_cache_mode pcm)
{
	return __pgprot(cachemode2protval(pcm));
}
static inline enum page_cache_mode pgprot2cachemode(pgprot_t pgprot)
{
	unsigned long masked;

	masked = pgprot_val(pgprot) & _PAGE_CACHE_MASK;
	if (likely(masked == 0))
		return 0;
	return __pte2cachemode_tbl[__pte2cm_idx(masked)];
}
static inline pgprot_t pgprot_4k_2_large(pgprot_t pgprot)
{
	pgprotval_t val = pgprot_val(pgprot);
	pgprot_t new;

	pgprot_val(new) = (val & ~(_PAGE_PAT | _PAGE_PAT_LARGE)) |
		((val & _PAGE_PAT) << (_PAGE_BIT_PAT_LARGE - _PAGE_BIT_PAT));
	return new;
}
static inline pgprot_t pgprot_large_2_4k(pgprot_t pgprot)
{
	pgprotval_t val = pgprot_val(pgprot);
	pgprot_t new;

	pgprot_val(new) = (val & ~(_PAGE_PAT | _PAGE_PAT_LARGE)) |
			  ((val & _PAGE_PAT_LARGE) >>
			   (_PAGE_BIT_PAT_LARGE - _PAGE_BIT_PAT));
	return new;
}


typedef struct page *pgtable_t;

extern pteval_t __supported_pte_mask;
extern void set_nx(void);
extern int nx_enabled;

#define pgprot_writecombine	pgprot_writecombine
extern pgprot_t pgprot_writecombine(pgprot_t prot);

#define pgprot_writethrough	pgprot_writethrough
extern pgprot_t pgprot_writethrough(pgprot_t prot);

/* Indicate that x86 has its own track and untrack pfn vma functions */
#define __HAVE_PFNMAP_TRACKING

#define __HAVE_PHYS_MEM_ACCESS_PROT
struct file;
pgprot_t phys_mem_access_prot(struct file *file, unsigned long pfn,
                              unsigned long size, pgprot_t vma_prot);
int phys_mem_access_prot_allowed(struct file *file, unsigned long pfn,
                              unsigned long size, pgprot_t *vma_prot);

/* Install a pte for a particular vaddr in kernel space. */
void set_pte_vaddr(unsigned long vaddr, pte_t pte);

#ifdef CONFIG_X86_32
extern void native_pagetable_init(void);
#else
#define native_pagetable_init        paging_init
#endif

struct seq_file;
extern void arch_report_meminfo(struct seq_file *m);

enum pg_level {
	PG_LEVEL_NONE,
	PG_LEVEL_4K,
	PG_LEVEL_2M,
	PG_LEVEL_1G,
	PG_LEVEL_NUM
};

#ifdef CONFIG_PROC_FS
extern void update_page_count(int level, unsigned long pages);
#else
static inline void update_page_count(int level, unsigned long pages) { }
#endif

/*
 * Helper function that returns the kernel pagetable entry controlling
 * the virtual address 'address'. NULL means no pagetable entry present.
 * NOTE: the return type is pte_t but if the pmd is PSE then we return it
 * as a pte too.
 */
extern pte_t *lookup_address(unsigned long address, unsigned int *level);
extern pte_t *lookup_address_in_pgd(pgd_t *pgd, unsigned long address,
				    unsigned int *level);
extern pmd_t *lookup_pmd_address(unsigned long address);
extern phys_addr_t slow_virt_to_phys(void *__address);
extern int kernel_map_pages_in_pgd(pgd_t *pgd, u64 pfn, unsigned long address,
				   unsigned numpages, unsigned long page_flags);
void kernel_unmap_pages_in_pgd(pgd_t *root, unsigned long address,
			       unsigned numpages);
#endif	/* !__ASSEMBLY__ */

#endif /* _ASM_X86_PGTABLE_DEFS_H */
