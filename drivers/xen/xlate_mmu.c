/*
 * MMU operations common to all auto-translated physmap guests.
 *
 * Copyright (C) 2015 Citrix Systems R&D Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#include <linux/kernel.h>
#include <linux/mm.h>

#include <asm/xen/hypercall.h>
#include <asm/xen/hypervisor.h>

#include <xen/xen.h>
#include <xen/page.h>
#include <xen/interface/xen.h>
#include <xen/interface/memory.h>

/* map fgmfn of domid to lpfn in the current domain */
static int map_foreign_page(unsigned long lpfn, unsigned long fgmfn,
			    unsigned int domid)
{
	int rc;
	struct xen_add_to_physmap_range xatp = {
		.domid = DOMID_SELF,
		.foreign_domid = domid,
		.size = 1,
		.space = XENMAPSPACE_gmfn_foreign,
	};
	xen_ulong_t idx = fgmfn;
	xen_pfn_t gpfn = lpfn;
	int err = 0;

	set_xen_guest_handle(xatp.idxs, &idx);
	set_xen_guest_handle(xatp.gpfns, &gpfn);
	set_xen_guest_handle(xatp.errs, &err);

	rc = HYPERVISOR_memory_op(XENMEM_add_to_physmap_range, &xatp);
	return rc < 0 ? rc : err;
}

struct remap_data {
	xen_pfn_t fgmfn; /* foreign domain's gmfn */
	pgprot_t prot;
	domid_t  domid;
	struct vm_area_struct *vma;
	int index;
	struct page **pages;
	struct xen_remap_mfn_info *info;
};

static int remap_pte_fn(pte_t *ptep, pgtable_t token, unsigned long addr,
			void *data)
{
	struct remap_data *info = data;
	struct page *page = info->pages[info->index++];
	unsigned long pfn = page_to_pfn(page);
	pte_t pte = pte_mkspecial(pfn_pte(pfn, info->prot));
	int rc;

	rc = map_foreign_page(pfn, info->fgmfn, info->domid);
	if (rc < 0)
		return rc;
	set_pte_at(info->vma->vm_mm, addr, ptep, pte);

	return 0;
}

int xen_xlate_remap_gfn_range(struct vm_area_struct *vma,
			      unsigned long addr,
			      xen_pfn_t gfn, int nr,
			      pgprot_t prot, unsigned domid,
			      struct page **pages)
{
	int err;
	struct remap_data data;

	/* TBD: Batching, current sole caller only does page at a time */
	if (nr > 1)
		return -EINVAL;

	data.fgmfn = gfn;
	data.prot = prot;
	data.domid = domid;
	data.vma = vma;
	data.index = 0;
	data.pages = pages;
	err = apply_to_page_range(vma->vm_mm, addr, nr << PAGE_SHIFT,
				  remap_pte_fn, &data);
	return err;
}
EXPORT_SYMBOL_GPL(xen_xlate_remap_gfn_range);

int xen_xlate_unmap_gfn_range(struct vm_area_struct *vma,
			      int nr, struct page **pages)
{
	int i;

	for (i = 0; i < nr; i++) {
		struct xen_remove_from_physmap xrp;
		unsigned long pfn;

		pfn = page_to_pfn(pages[i]);

		xrp.domid = DOMID_SELF;
		xrp.gpfn = pfn;
		(void)HYPERVISOR_memory_op(XENMEM_remove_from_physmap, &xrp);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(xen_xlate_unmap_gfn_range);
