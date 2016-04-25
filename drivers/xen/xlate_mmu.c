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

typedef void (*xen_gfn_fn_t)(unsigned long gfn, void *data);

/* Break down the pages in 4KB chunk and call fn for each gfn */
static void xen_for_each_gfn(struct page **pages, unsigned nr_gfn,
			     xen_gfn_fn_t fn, void *data)
{
	unsigned long xen_pfn = 0;
	struct page *page;
	int i;

	for (i = 0; i < nr_gfn; i++) {
		if ((i % XEN_PFN_PER_PAGE) == 0) {
			page = pages[i / XEN_PFN_PER_PAGE];
			xen_pfn = page_to_xen_pfn(page);
		}
		fn(pfn_to_gfn(xen_pfn++), data);
	}
}

struct remap_data {
	xen_pfn_t *fgfn; /* foreign domain's gfn */
	int nr_fgfn; /* Number of foreign gfn left to map */
	pgprot_t prot;
	domid_t  domid;
	struct vm_area_struct *vma;
	int index;
	struct page **pages;
	struct xen_remap_gfn_info *info;
	int *err_ptr;
	int mapped;

	/* Hypercall parameters */
	int h_errs[XEN_PFN_PER_PAGE];
	xen_ulong_t h_idxs[XEN_PFN_PER_PAGE];
	xen_pfn_t h_gpfns[XEN_PFN_PER_PAGE];

	int h_iter;	/* Iterator */
};

static void setup_hparams(unsigned long gfn, void *data)
{
	struct remap_data *info = data;

	info->h_idxs[info->h_iter] = *info->fgfn;
	info->h_gpfns[info->h_iter] = gfn;
	info->h_errs[info->h_iter] = 0;

	info->h_iter++;
	info->fgfn++;
}

static int remap_pte_fn(pte_t *ptep, pgtable_t token, unsigned long addr,
			void *data)
{
	struct remap_data *info = data;
	struct page *page = info->pages[info->index++];
	pte_t pte = pte_mkspecial(pfn_pte(page_to_pfn(page), info->prot));
	int rc, nr_gfn;
	uint32_t i;
	struct xen_add_to_physmap_range xatp = {
		.domid = DOMID_SELF,
		.foreign_domid = info->domid,
		.space = XENMAPSPACE_gmfn_foreign,
	};

	nr_gfn = min_t(typeof(info->nr_fgfn), XEN_PFN_PER_PAGE, info->nr_fgfn);
	info->nr_fgfn -= nr_gfn;

	info->h_iter = 0;
	xen_for_each_gfn(&page, nr_gfn, setup_hparams, info);
	BUG_ON(info->h_iter != nr_gfn);

	set_xen_guest_handle(xatp.idxs, info->h_idxs);
	set_xen_guest_handle(xatp.gpfns, info->h_gpfns);
	set_xen_guest_handle(xatp.errs, info->h_errs);
	xatp.size = nr_gfn;

	rc = HYPERVISOR_memory_op(XENMEM_add_to_physmap_range, &xatp);

	/* info->err_ptr expect to have one error status per Xen PFN */
	for (i = 0; i < nr_gfn; i++) {
		int err = (rc < 0) ? rc : info->h_errs[i];

		*(info->err_ptr++) = err;
		if (!err)
			info->mapped++;
	}

	/*
	 * Note: The hypercall will return 0 in most of the case if even if
	 * all the fgmfn are not mapped. We still have to update the pte
	 * as the userspace may decide to continue.
	 */
	if (!rc)
		set_pte_at(info->vma->vm_mm, addr, ptep, pte);

	return 0;
}

int xen_xlate_remap_gfn_array(struct vm_area_struct *vma,
			      unsigned long addr,
			      xen_pfn_t *gfn, int nr,
			      int *err_ptr, pgprot_t prot,
			      unsigned domid,
			      struct page **pages)
{
	int err;
	struct remap_data data;
	unsigned long range = DIV_ROUND_UP(nr, XEN_PFN_PER_PAGE) << PAGE_SHIFT;

	/* Kept here for the purpose of making sure code doesn't break
	   x86 PVOPS */
	BUG_ON(!((vma->vm_flags & (VM_PFNMAP | VM_IO)) == (VM_PFNMAP | VM_IO)));

	data.fgfn = gfn;
	data.nr_fgfn = nr;
	data.prot  = prot;
	data.domid = domid;
	data.vma   = vma;
	data.pages = pages;
	data.index = 0;
	data.err_ptr = err_ptr;
	data.mapped = 0;

	err = apply_to_page_range(vma->vm_mm, addr, range,
				  remap_pte_fn, &data);
	return err < 0 ? err : data.mapped;
}
EXPORT_SYMBOL_GPL(xen_xlate_remap_gfn_array);

static void unmap_gfn(unsigned long gfn, void *data)
{
	struct xen_remove_from_physmap xrp;

	xrp.domid = DOMID_SELF;
	xrp.gpfn = gfn;
	(void)HYPERVISOR_memory_op(XENMEM_remove_from_physmap, &xrp);
}

int xen_xlate_unmap_gfn_range(struct vm_area_struct *vma,
			      int nr, struct page **pages)
{
	xen_for_each_gfn(pages, nr, unmap_gfn, NULL);

	return 0;
}
EXPORT_SYMBOL_GPL(xen_xlate_unmap_gfn_range);
