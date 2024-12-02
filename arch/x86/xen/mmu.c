// SPDX-License-Identifier: GPL-2.0

#include <linux/pfn.h>
#include <asm/xen/page.h>
#include <asm/xen/hypercall.h>
#include <xen/interface/memory.h>

#include "multicalls.h"
#include "mmu.h"

unsigned long arbitrary_virt_to_mfn(void *vaddr)
{
	xmaddr_t maddr = arbitrary_virt_to_machine(vaddr);

	return PFN_DOWN(maddr.maddr);
}

xmaddr_t arbitrary_virt_to_machine(void *vaddr)
{
	unsigned long address = (unsigned long)vaddr;
	unsigned int level;
	pte_t *pte;
	unsigned offset;

	/*
	 * if the PFN is in the linear mapped vaddr range, we can just use
	 * the (quick) virt_to_machine() p2m lookup
	 */
	if (virt_addr_valid(vaddr))
		return virt_to_machine(vaddr);

	/* otherwise we have to do a (slower) full page-table walk */

	pte = lookup_address(address, &level);
	BUG_ON(pte == NULL);
	offset = address & ~PAGE_MASK;
	return XMADDR(((phys_addr_t)pte_mfn(*pte) << PAGE_SHIFT) + offset);
}
EXPORT_SYMBOL_GPL(arbitrary_virt_to_machine);

/* Returns: 0 success */
int xen_unmap_domain_gfn_range(struct vm_area_struct *vma,
			       int nr, struct page **pages)
{
	if (xen_feature(XENFEAT_auto_translated_physmap))
		return xen_xlate_unmap_gfn_range(vma, nr, pages);

	if (!pages)
		return 0;

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(xen_unmap_domain_gfn_range);
