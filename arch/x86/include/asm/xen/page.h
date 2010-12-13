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
#define FOREIGN_FRAME_BIT	(1UL<<31)
#define FOREIGN_FRAME(m)	((m) | FOREIGN_FRAME_BIT)

/* Maximum amount of memory we can handle in a domain in pages */
#define MAX_DOMAIN_PAGES						\
    ((unsigned long)((u64)CONFIG_XEN_MAX_DOMAIN_MEMORY * 1024 * 1024 * 1024 / PAGE_SIZE))

extern unsigned long *machine_to_phys_mapping;
extern unsigned int   machine_to_phys_order;

extern unsigned long get_phys_to_machine(unsigned long pfn);
extern bool set_phys_to_machine(unsigned long pfn, unsigned long mfn);

extern int m2p_add_override(unsigned long mfn, struct page *page);
extern int m2p_remove_override(struct page *page);
extern struct page *m2p_find_override(unsigned long mfn);
extern unsigned long m2p_find_override_pfn(unsigned long mfn, unsigned long pfn);

static inline unsigned long pfn_to_mfn(unsigned long pfn)
{
	unsigned long mfn;

	if (xen_feature(XENFEAT_auto_translated_physmap))
		return pfn;

	mfn = get_phys_to_machine(pfn);

	if (mfn != INVALID_P2M_ENTRY)
		mfn &= ~FOREIGN_FRAME_BIT;

	return mfn;
}

static inline int phys_to_machine_mapping_valid(unsigned long pfn)
{
	if (xen_feature(XENFEAT_auto_translated_physmap))
		return 1;

	return get_phys_to_machine(pfn) != INVALID_P2M_ENTRY;
}

static inline unsigned long mfn_to_pfn(unsigned long mfn)
{
	unsigned long pfn;

	if (xen_feature(XENFEAT_auto_translated_physmap))
		return mfn;

	pfn = 0;
	/*
	 * The array access can fail (e.g., device space beyond end of RAM).
	 * In such cases it doesn't matter what we return (we return garbage),
	 * but we must handle the fault without crashing!
	 */
	__get_user(pfn, &machine_to_phys_mapping[mfn]);

	/*
	 * If this appears to be a foreign mfn (because the pfn
	 * doesn't map back to the mfn), then check the local override
	 * table to see if there's a better pfn to use.
	 */
	if (get_phys_to_machine(pfn) != mfn)
		pfn = m2p_find_override_pfn(mfn, pfn);

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
	unsigned long pfn = mfn_to_pfn(mfn);
	if (get_phys_to_machine(pfn) != mfn)
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

#endif /* _ASM_X86_XEN_PAGE_H */
