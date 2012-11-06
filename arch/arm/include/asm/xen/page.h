#ifndef _ASM_ARM_XEN_PAGE_H
#define _ASM_ARM_XEN_PAGE_H

#include <asm/page.h>
#include <asm/pgtable.h>

#include <linux/pfn.h>
#include <linux/types.h>

#include <xen/interface/grant_table.h>

#define pfn_to_mfn(pfn)			(pfn)
#define phys_to_machine_mapping_valid(pfn) (1)
#define mfn_to_pfn(mfn)			(mfn)
#define mfn_to_virt(m)			(__va(mfn_to_pfn(m) << PAGE_SHIFT))

#define pte_mfn	    pte_pfn
#define mfn_pte	    pfn_pte

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

#define INVALID_P2M_ENTRY      (~0UL)

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
/* VIRT <-> MACHINE conversion */
#define virt_to_machine(v)	(phys_to_machine(XPADDR(__pa(v))))
#define virt_to_pfn(v)          (PFN_DOWN(__pa(v)))
#define virt_to_mfn(v)		(pfn_to_mfn(virt_to_pfn(v)))
#define mfn_to_virt(m)		(__va(mfn_to_pfn(m) << PAGE_SHIFT))

static inline xmaddr_t arbitrary_virt_to_machine(void *vaddr)
{
	/* TODO: assuming it is mapped in the kernel 1:1 */
	return virt_to_machine(vaddr);
}

/* TODO: this shouldn't be here but it is because the frontend drivers
 * are using it (its rolled in headers) even though we won't hit the code path.
 * So for right now just punt with this.
 */
static inline pte_t *lookup_address(unsigned long address, unsigned int *level)
{
	BUG();
	return NULL;
}

static inline int m2p_add_override(unsigned long mfn, struct page *page,
		struct gnttab_map_grant_ref *kmap_op)
{
	return 0;
}

static inline int m2p_remove_override(struct page *page, bool clear_pte)
{
	return 0;
}

static inline bool __set_phys_to_machine(unsigned long pfn, unsigned long mfn)
{
	BUG_ON(pfn != mfn && mfn != INVALID_P2M_ENTRY);
	return true;
}

static inline bool set_phys_to_machine(unsigned long pfn, unsigned long mfn)
{
	return __set_phys_to_machine(pfn, mfn);
}
#endif /* _ASM_ARM_XEN_PAGE_H */
