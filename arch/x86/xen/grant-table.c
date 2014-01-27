/******************************************************************************
 * grant_table.c
 * x86 specific part
 *
 * Granting foreign access to our memory reservation.
 *
 * Copyright (c) 2005-2006, Christopher Clark
 * Copyright (c) 2004-2005, K A Fraser
 * Copyright (c) 2008 Isaku Yamahata <yamahata at valinux co jp>
 *                    VA Linux Systems Japan. Split out x86 specific part.
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

#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>

#include <xen/interface/xen.h>
#include <xen/page.h>
#include <xen/grant_table.h>

#include <asm/pgtable.h>

static int map_pte_fn(pte_t *pte, struct page *pmd_page,
		      unsigned long addr, void *data)
{
	unsigned long **frames = (unsigned long **)data;

	set_pte_at(&init_mm, addr, pte, mfn_pte((*frames)[0], PAGE_KERNEL));
	(*frames)++;
	return 0;
}

/*
 * This function is used to map shared frames to store grant status. It is
 * different from map_pte_fn above, the frames type here is uint64_t.
 */
static int map_pte_fn_status(pte_t *pte, struct page *pmd_page,
			     unsigned long addr, void *data)
{
	uint64_t **frames = (uint64_t **)data;

	set_pte_at(&init_mm, addr, pte, mfn_pte((*frames)[0], PAGE_KERNEL));
	(*frames)++;
	return 0;
}

static int unmap_pte_fn(pte_t *pte, struct page *pmd_page,
			unsigned long addr, void *data)
{

	set_pte_at(&init_mm, addr, pte, __pte(0));
	return 0;
}

int arch_gnttab_map_shared(unsigned long *frames, unsigned long nr_gframes,
			   unsigned long max_nr_gframes,
			   void **__shared)
{
	int rc;
	void *shared = *__shared;

	if (shared == NULL) {
		struct vm_struct *area =
			alloc_vm_area(PAGE_SIZE * max_nr_gframes, NULL);
		BUG_ON(area == NULL);
		shared = area->addr;
		*__shared = shared;
	}

	rc = apply_to_page_range(&init_mm, (unsigned long)shared,
				 PAGE_SIZE * nr_gframes,
				 map_pte_fn, &frames);
	return rc;
}

int arch_gnttab_map_status(uint64_t *frames, unsigned long nr_gframes,
			   unsigned long max_nr_gframes,
			   grant_status_t **__shared)
{
	int rc;
	grant_status_t *shared = *__shared;

	if (shared == NULL) {
		/* No need to pass in PTE as we are going to do it
		 * in apply_to_page_range anyhow. */
		struct vm_struct *area =
			alloc_vm_area(PAGE_SIZE * max_nr_gframes, NULL);
		BUG_ON(area == NULL);
		shared = area->addr;
		*__shared = shared;
	}

	rc = apply_to_page_range(&init_mm, (unsigned long)shared,
				 PAGE_SIZE * nr_gframes,
				 map_pte_fn_status, &frames);
	return rc;
}

void arch_gnttab_unmap(void *shared, unsigned long nr_gframes)
{
	apply_to_page_range(&init_mm, (unsigned long)shared,
			    PAGE_SIZE * nr_gframes, unmap_pte_fn, NULL);
}
#ifdef CONFIG_XEN_PVH
#include <xen/balloon.h>
#include <xen/events.h>
#include <xen/xen.h>
#include <linux/slab.h>
static int __init xlated_setup_gnttab_pages(void)
{
	struct page **pages;
	xen_pfn_t *pfns;
	int rc;
	unsigned int i;
	unsigned long nr_grant_frames = gnttab_max_grant_frames();

	BUG_ON(nr_grant_frames == 0);
	pages = kcalloc(nr_grant_frames, sizeof(pages[0]), GFP_KERNEL);
	if (!pages)
		return -ENOMEM;

	pfns = kcalloc(nr_grant_frames, sizeof(pfns[0]), GFP_KERNEL);
	if (!pfns) {
		kfree(pages);
		return -ENOMEM;
	}
	rc = alloc_xenballooned_pages(nr_grant_frames, pages, 0 /* lowmem */);
	if (rc) {
		pr_warn("%s Couldn't balloon alloc %ld pfns rc:%d\n", __func__,
			nr_grant_frames, rc);
		kfree(pages);
		kfree(pfns);
		return rc;
	}
	for (i = 0; i < nr_grant_frames; i++)
		pfns[i] = page_to_pfn(pages[i]);

	rc = arch_gnttab_map_shared(pfns, nr_grant_frames, nr_grant_frames,
				    &xen_auto_xlat_grant_frames.vaddr);

	kfree(pages);
	if (rc) {
		pr_warn("%s Couldn't map %ld pfns rc:%d\n", __func__,
			nr_grant_frames, rc);
		free_xenballooned_pages(nr_grant_frames, pages);
		kfree(pfns);
		return rc;
	}

	xen_auto_xlat_grant_frames.pfn = pfns;
	xen_auto_xlat_grant_frames.count = nr_grant_frames;

	return 0;
}

static int __init xen_pvh_gnttab_setup(void)
{
	if (!xen_pvh_domain())
		return -ENODEV;

	return xlated_setup_gnttab_pages();
}
/* Call it _before_ __gnttab_init as we need to initialize the
 * xen_auto_xlat_grant_frames first. */
core_initcall(xen_pvh_gnttab_setup);
#endif
