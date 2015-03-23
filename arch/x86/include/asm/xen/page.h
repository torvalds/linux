#ifndef _ASM_X86_XEN_PAGE_H
#define _ASM_X86_XEN_PAGE_H

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/pfn.h>
#include <linux/mm.h>

#include <asm/uaccess.h>
#include <asm/page.h>
#include <asm/pgtable.h>

#include <xen/interface/xen.h>
#include <xen/grant_table.h>
#include <xen/features.h>

/* Xen machine address */
typedef struct xmaddr {
	phys_addr_t maddr;
} xmaddr_t;

/* Xen pseudo-physical address */
typedef struct xpaddr {
	phys_addr_t paddr;
} xpaddr_t;

#define XMADDR(x)	((xmaddr_t) { .maddr = (x) })
#define XPADDR(x)	((xpaddr_t) { .paddr = (x) })

/**** MACHINE <-> PHYSICAL CONVERSION MACROS ****/
#define INVALID_P2M_ENTRY	(~0UL)
#define FOREIGN_FRAME_BIT	(1UL<<(BITS_PER_LONG-1))
#define IDENTITY_FRAME_BIT	(1UL<<(BITS_PER_LONG-2))
#define FOREIGN_FRAME(m)	((m) | FOREIGN_FRAME_BIT)
#define IDENTITY_FRAME(m)	((m) | IDENTITY_FRAME_BIT)

/* Maximum amount of memory we can handle in a domain in pages */
#define MAX_DOMAIN_PAGES						\
    ((unsigned long)((u64)CONFIG_XEN_MAX_DOMAIN_MEMORY * 1024 * 1024 * 1024 / PAGE_SIZE))

extern unsigned long *machine_to_phys_mapping;
extern unsigned long  machine_to_phys_nr;
extern unsigned long *xen_p2m_addr;
extern unsigned long  xen_p2m_size;
extern unsigned long  xen_max_p2m_pfn;

extern unsigned long get_phys_to_machine(unsigned long pfn);
extern bool set_phys_to_machine(unsigned long pfn, unsigned long mfn);
extern bool __set_phys_to_machine(unsigned long pfn, unsigned long mfn);
extern unsigned long set_phys_range_identity(unsigned long pfn_s,
					     unsigned long pfn_e);

extern int set_foreign_p2m_mapping(struct gnttab_map_grant_ref *map_ops,
				   struct gnttab_map_grant_ref *kmap_ops,
				   struct page **pages, unsigned int count);
extern int clear_foreign_p2m_mapping(struct gnttab_unmap_grant_ref *unmap_ops,
				     struct gnttab_unmap_grant_ref *kunmap_ops,
				     struct page **pages, unsigned int count);

/*
 * Helper functions to write or read unsigned long values to/from
 * memory, when the access may fault.
 */
static inline int xen_safe_write_ulong(unsigned long *addr, unsigned long val)
{
	return __put_user(val, (unsigned long __user *)addr);
}

static inline int xen_safe_read_ulong(unsigned long *addr, unsigned long *val)
{
	return __get_user(*val, (unsigned long __user *)addr);
}

/*
 * When to use pfn_to_mfn(), __pfn_to_mfn() or get_phys_to_machine():
 * - pfn_to_mfn() returns either INVALID_P2M_ENTRY or the mfn. No indicator
 *   bits (identity or foreign) are set.
 * - __pfn_to_mfn() returns the found entry of the p2m table. A possibly set
 *   identity or foreign indicator will be still set. __pfn_to_mfn() is
 *   encapsulating get_phys_to_machine() which is called in special cases only.
 * - get_phys_to_machine() is to be called by __pfn_to_mfn() only in special
 *   cases needing an extended handling.
 */
static inline unsigned long __pfn_to_mfn(unsigned long pfn)
{
	unsigned long mfn;

	if (pfn < xen_p2m_size)
		mfn = xen_p2m_addr[pfn];
	else if (unlikely(pfn < xen_max_p2m_pfn))
		return get_phys_to_machine(pfn);
	else
		return IDENTITY_FRAME(pfn);

	if (unlikely(mfn == INVALID_P2M_ENTRY))
		return get_phys_to_machine(pfn);

	return mfn;
}

static inline unsigned long pfn_to_mfn(unsigned long pfn)
{
	unsigned long mfn;

	if (xen_feature(XENFEAT_auto_translated_physmap))
		return pfn;

	mfn = __pfn_to_mfn(pfn);

	if (mfn != INVALID_P2M_ENTRY)
		mfn &= ~(FOREIGN_FRAME_BIT | IDENTITY_FRAME_BIT);

	return mfn;
}

static inline int phys_to_machine_mapping_valid(unsigned long pfn)
{
	if (xen_feature(XENFEAT_auto_translated_physmap))
		return 1;

	return __pfn_to_mfn(pfn) != INVALID_P2M_ENTRY;
}

static inline unsigned long mfn_to_pfn_no_overrides(unsigned long mfn)
{
	unsigned long pfn;
	int ret;

	if (xen_feature(XENFEAT_auto_translated_physmap))
		return mfn;

	if (unlikely(mfn >= machine_to_phys_nr))
		return ~0;

	/*
	 * The array access can fail (e.g., device space beyond end of RAM).
	 * In such cases it doesn't matter what we return (we return garbage),
	 * but we must handle the fault without crashing!
	 */
	ret = xen_safe_read_ulong(&machine_to_phys_mapping[mfn], &pfn);
	if (ret < 0)
		return ~0;

	return pfn;
}

static inline unsigned long mfn_to_pfn(unsigned long mfn)
{
	unsigned long pfn;

	if (xen_feature(XENFEAT_auto_translated_physmap))
		return mfn;

	pfn = mfn_to_pfn_no_overrides(mfn);
	if (__pfn_to_mfn(pfn) != mfn)
		pfn = ~0;

	/*
	 * pfn is ~0 if there are no entries in the m2p for mfn or the
	 * entry doesn't map back to the mfn.
	 */
	if (pfn == ~0 && __pfn_to_mfn(mfn) == IDENTITY_FRAME(mfn))
		pfn = mfn;

	return pfn;
}

static inline xmaddr_t phys_to_machine(xpaddr_t phys)
{
	unsigned offset = phys.paddr & ~PAGE_MASK;
	return XMADDR(PFN_PHYS(pfn_to_mfn(PFN_DOWN(phys.paddr))) | offset);
}

static inline xpaddr_t machine_to_phys(xmaddr_t machine)
{
	unsigned offset = machine.maddr & ~PAGE_MASK;
	return XPADDR(PFN_PHYS(mfn_to_pfn(PFN_DOWN(machine.maddr))) | offset);
}

/*
 * We detect special mappings in one of two ways:
 *  1. If the MFN is an I/O page then Xen will set the m2p entry
 *     to be outside our maximum possible pseudophys range.
 *  2. If the MFN belongs to a different domain then we will certainly
 *     not have MFN in our p2m table. Conversely, if the page is ours,
 *     then we'll have p2m(m2p(MFN))==MFN.
 * If we detect a special mapping then it doesn't have a 'struct page'.
 * We force !pfn_valid() by returning an out-of-range pointer.
 *
 * NB. These checks require that, for any MFN that is not in our reservation,
 * there is no PFN such that p2m(PFN) == MFN. Otherwise we can get confused if
 * we are foreign-mapping the MFN, and the other domain as m2p(MFN) == PFN.
 * Yikes! Various places must poke in INVALID_P2M_ENTRY for safety.
 *
 * NB2. When deliberately mapping foreign pages into the p2m table, you *must*
 *      use FOREIGN_FRAME(). This will cause pte_pfn() to choke on it, as we
 *      require. In all the cases we care about, the FOREIGN_FRAME bit is
 *      masked (e.g., pfn_to_mfn()) so behaviour there is correct.
 */
static inline unsigned long mfn_to_local_pfn(unsigned long mfn)
{
	unsigned long pfn;

	if (xen_feature(XENFEAT_auto_translated_physmap))
		return mfn;

	pfn = mfn_to_pfn(mfn);
	if (__pfn_to_mfn(pfn) != mfn)
		return -1; /* force !pfn_valid() */
	return pfn;
}

/* VIRT <-> MACHINE conversion */
#define virt_to_machine(v)	(phys_to_machine(XPADDR(__pa(v))))
#define virt_to_pfn(v)          (PFN_DOWN(__pa(v)))
#define virt_to_mfn(v)		(pfn_to_mfn(virt_to_pfn(v)))
#define mfn_to_virt(m)		(__va(mfn_to_pfn(m) << PAGE_SHIFT))

static inline unsigned long pte_mfn(pte_t pte)
{
	return (pte.pte & PTE_PFN_MASK) >> PAGE_SHIFT;
}

static inline pte_t mfn_pte(unsigned long page_nr, pgprot_t pgprot)
{
	pte_t pte;

	pte.pte = ((phys_addr_t)page_nr << PAGE_SHIFT) |
			massage_pgprot(pgprot);

	return pte;
}

static inline pteval_t pte_val_ma(pte_t pte)
{
	return pte.pte;
}

static inline pte_t __pte_ma(pteval_t x)
{
	return (pte_t) { .pte = x };
}

#define pmd_val_ma(v) ((v).pmd)
#ifdef __PAGETABLE_PUD_FOLDED
#define pud_val_ma(v) ((v).pgd.pgd)
#else
#define pud_val_ma(v) ((v).pud)
#endif
#define __pmd_ma(x)	((pmd_t) { (x) } )

#define pgd_val_ma(x)	((x).pgd)

void xen_set_domain_pte(pte_t *ptep, pte_t pteval, unsigned domid);

xmaddr_t arbitrary_virt_to_machine(void *address);
unsigned long arbitrary_virt_to_mfn(void *vaddr);
void make_lowmem_page_readonly(void *vaddr);
void make_lowmem_page_readwrite(void *vaddr);

#define xen_remap(cookie, size) ioremap((cookie), (size));
#define xen_unmap(cookie) iounmap((cookie))

static inline bool xen_arch_need_swiotlb(struct device *dev,
					 unsigned long pfn,
					 unsigned long mfn)
{
	return false;
}

#endif /* _ASM_X86_XEN_PAGE_H */
