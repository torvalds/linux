/******************************************************************************
 * Xen balloon driver - enables returning/claiming memory to/from Xen.
 *
 * Copyright (c) 2003, B Dragovic
 * Copyright (c) 2003-2004, M Williamson, K Fraser
 * Copyright (c) 2005 Dan M. Smith, IBM Corporation
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
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/bootmem.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/gfp.h>

#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/tlb.h>
#include <asm/e820.h>

#include <asm/xen/hypervisor.h>
#include <asm/xen/hypercall.h>

#include <xen/xen.h>
#include <xen/interface/xen.h>
#include <xen/interface/memory.h>
#include <xen/balloon.h>
#include <xen/features.h>
#include <xen/page.h>

/*
 * balloon_process() state:
 *
 * BP_DONE: done or nothing to do,
 * BP_EAGAIN: error, go to sleep,
 * BP_ECANCELED: error, balloon operation canceled.
 */

enum bp_state {
	BP_DONE,
	BP_EAGAIN,
	BP_ECANCELED
};


static DEFINE_MUTEX(balloon_mutex);

struct balloon_stats balloon_stats;
EXPORT_SYMBOL_GPL(balloon_stats);

/* We increase/decrease in batches which fit in a page */
static unsigned long frame_list[PAGE_SIZE / sizeof(unsigned long)];

#ifdef CONFIG_HIGHMEM
#define inc_totalhigh_pages() (totalhigh_pages++)
#define dec_totalhigh_pages() (totalhigh_pages--)
#else
#define inc_totalhigh_pages() do {} while(0)
#define dec_totalhigh_pages() do {} while(0)
#endif

/* List of ballooned pages, threaded through the mem_map array. */
static LIST_HEAD(ballooned_pages);

/* Main work function, always executed in process context. */
static void balloon_process(struct work_struct *work);
static DECLARE_DELAYED_WORK(balloon_worker, balloon_process);

/* When ballooning out (allocating memory to return to Xen) we don't really
   want the kernel to try too hard since that can trigger the oom killer. */
#define GFP_BALLOON \
	(GFP_HIGHUSER | __GFP_NOWARN | __GFP_NORETRY | __GFP_NOMEMALLOC)

static void scrub_page(struct page *page)
{
#ifdef CONFIG_XEN_SCRUB_PAGES
	clear_highpage(page);
#endif
}

/* balloon_append: add the given page to the balloon. */
static void __balloon_append(struct page *page)
{
	/* Lowmem is re-populated first, so highmem pages go at list tail. */
	if (PageHighMem(page)) {
		list_add_tail(&page->lru, &ballooned_pages);
		balloon_stats.balloon_high++;
	} else {
		list_add(&page->lru, &ballooned_pages);
		balloon_stats.balloon_low++;
	}
}

static void balloon_append(struct page *page)
{
	__balloon_append(page);
	if (PageHighMem(page))
		dec_totalhigh_pages();
	totalram_pages--;
}

/* balloon_retrieve: rescue a page from the balloon, if it is not empty. */
static struct page *balloon_retrieve(bool prefer_highmem)
{
	struct page *page;

	if (list_empty(&ballooned_pages))
		return NULL;

	if (prefer_highmem)
		page = list_entry(ballooned_pages.prev, struct page, lru);
	else
		page = list_entry(ballooned_pages.next, struct page, lru);
	list_del(&page->lru);

	if (PageHighMem(page)) {
		balloon_stats.balloon_high--;
		inc_totalhigh_pages();
	}
	else
		balloon_stats.balloon_low--;

	totalram_pages++;

	return page;
}

static struct page *balloon_first_page(void)
{
	if (list_empty(&ballooned_pages))
		return NULL;
	return list_entry(ballooned_pages.next, struct page, lru);
}

static struct page *balloon_next_page(struct page *page)
{
	struct list_head *next = page->lru.next;
	if (next == &ballooned_pages)
		return NULL;
	return list_entry(next, struct page, lru);
}

static enum bp_state update_schedule(enum bp_state state)
{
	if (state == BP_DONE) {
		balloon_stats.schedule_delay = 1;
		balloon_stats.retry_count = 1;
		return BP_DONE;
	}

	++balloon_stats.retry_count;

	if (balloon_stats.max_retry_count != RETRY_UNLIMITED &&
			balloon_stats.retry_count > balloon_stats.max_retry_count) {
		balloon_stats.schedule_delay = 1;
		balloon_stats.retry_count = 1;
		return BP_ECANCELED;
	}

	balloon_stats.schedule_delay <<= 1;

	if (balloon_stats.schedule_delay > balloon_stats.max_schedule_delay)
		balloon_stats.schedule_delay = balloon_stats.max_schedule_delay;

	return BP_EAGAIN;
}

static long current_credit(void)
{
	unsigned long target = balloon_stats.target_pages;

	target = min(target,
		     balloon_stats.current_pages +
		     balloon_stats.balloon_low +
		     balloon_stats.balloon_high);

	return target - balloon_stats.current_pages;
}

static enum bp_state increase_reservation(unsigned long nr_pages)
{
	int rc;
	unsigned long  pfn, i;
	struct page   *page;
	struct xen_memory_reservation reservation = {
		.address_bits = 0,
		.extent_order = 0,
		.domid        = DOMID_SELF
	};

	if (nr_pages > ARRAY_SIZE(frame_list))
		nr_pages = ARRAY_SIZE(frame_list);

	page = balloon_first_page();
	for (i = 0; i < nr_pages; i++) {
		if (!page) {
			nr_pages = i;
			break;
		}
		frame_list[i] = page_to_pfn(page);
		page = balloon_next_page(page);
	}

	set_xen_guest_handle(reservation.extent_start, frame_list);
	reservation.nr_extents = nr_pages;
	rc = HYPERVISOR_memory_op(XENMEM_populate_physmap, &reservation);
	if (rc <= 0)
		return BP_EAGAIN;

	for (i = 0; i < rc; i++) {
		page = balloon_retrieve(false);
		BUG_ON(page == NULL);

		pfn = page_to_pfn(page);
		BUG_ON(!xen_feature(XENFEAT_auto_translated_physmap) &&
		       phys_to_machine_mapping_valid(pfn));

		set_phys_to_machine(pfn, frame_list[i]);

		/* Link back into the page tables if not highmem. */
		if (xen_pv_domain() && !PageHighMem(page)) {
			int ret;
			ret = HYPERVISOR_update_va_mapping(
				(unsigned long)__va(pfn << PAGE_SHIFT),
				mfn_pte(frame_list[i], PAGE_KERNEL),
				0);
			BUG_ON(ret);
		}

		/* Relinquish the page back to the allocator. */
		ClearPageReserved(page);
		init_page_count(page);
		__free_page(page);
	}

	balloon_stats.current_pages += rc;

	return BP_DONE;
}

static enum bp_state decrease_reservation(unsigned long nr_pages, gfp_t gfp)
{
	enum bp_state state = BP_DONE;
	unsigned long  pfn, i;
	struct page   *page;
	int ret;
	struct xen_memory_reservation reservation = {
		.address_bits = 0,
		.extent_order = 0,
		.domid        = DOMID_SELF
	};

	if (nr_pages > ARRAY_SIZE(frame_list))
		nr_pages = ARRAY_SIZE(frame_list);

	for (i = 0; i < nr_pages; i++) {
		if ((page = alloc_page(gfp)) == NULL) {
			nr_pages = i;
			state = BP_EAGAIN;
			break;
		}

		pfn = page_to_pfn(page);
		frame_list[i] = pfn_to_mfn(pfn);

		scrub_page(page);

		if (xen_pv_domain() && !PageHighMem(page)) {
			ret = HYPERVISOR_update_va_mapping(
				(unsigned long)__va(pfn << PAGE_SHIFT),
				__pte_ma(0), 0);
			BUG_ON(ret);
                }

	}

	/* Ensure that ballooned highmem pages don't have kmaps. */
	kmap_flush_unused();
	flush_tlb_all();

	/* No more mappings: invalidate P2M and add to balloon. */
	for (i = 0; i < nr_pages; i++) {
		pfn = mfn_to_pfn(frame_list[i]);
		__set_phys_to_machine(pfn, INVALID_P2M_ENTRY);
		balloon_append(pfn_to_page(pfn));
	}

	set_xen_guest_handle(reservation.extent_start, frame_list);
	reservation.nr_extents   = nr_pages;
	ret = HYPERVISOR_memory_op(XENMEM_decrease_reservation, &reservation);
	BUG_ON(ret != nr_pages);

	balloon_stats.current_pages -= nr_pages;

	return state;
}

/*
 * We avoid multiple worker processes conflicting via the balloon mutex.
 * We may of course race updates of the target counts (which are protected
 * by the balloon lock), or with changes to the Xen hard limit, but we will
 * recover from these in time.
 */
static void balloon_process(struct work_struct *work)
{
	enum bp_state state = BP_DONE;
	long credit;

	mutex_lock(&balloon_mutex);

	do {
		credit = current_credit();

		if (credit > 0)
			state = increase_reservation(credit);

		if (credit < 0)
			state = decrease_reservation(-credit, GFP_BALLOON);

		state = update_schedule(state);

#ifndef CONFIG_PREEMPT
		if (need_resched())
			schedule();
#endif
	} while (credit && state == BP_DONE);

	/* Schedule more work if there is some still to be done. */
	if (state == BP_EAGAIN)
		schedule_delayed_work(&balloon_worker, balloon_stats.schedule_delay * HZ);

	mutex_unlock(&balloon_mutex);
}

/* Resets the Xen limit, sets new target, and kicks off processing. */
void balloon_set_new_target(unsigned long target)
{
	/* No need for lock. Not read-modify-write updates. */
	balloon_stats.target_pages = target;
	schedule_delayed_work(&balloon_worker, 0);
}
EXPORT_SYMBOL_GPL(balloon_set_new_target);

/**
 * alloc_xenballooned_pages - get pages that have been ballooned out
 * @nr_pages: Number of pages to get
 * @pages: pages returned
 * @return 0 on success, error otherwise
 */
int alloc_xenballooned_pages(int nr_pages, struct page** pages)
{
	int pgno = 0;
	struct page* page;
	mutex_lock(&balloon_mutex);
	while (pgno < nr_pages) {
		page = balloon_retrieve(true);
		if (page) {
			pages[pgno++] = page;
		} else {
			enum bp_state st;
			st = decrease_reservation(nr_pages - pgno, GFP_HIGHUSER);
			if (st != BP_DONE)
				goto out_undo;
		}
	}
	mutex_unlock(&balloon_mutex);
	return 0;
 out_undo:
	while (pgno)
		balloon_append(pages[--pgno]);
	/* Free the memory back to the kernel soon */
	schedule_delayed_work(&balloon_worker, 0);
	mutex_unlock(&balloon_mutex);
	return -ENOMEM;
}
EXPORT_SYMBOL(alloc_xenballooned_pages);

/**
 * free_xenballooned_pages - return pages retrieved with get_ballooned_pages
 * @nr_pages: Number of pages
 * @pages: pages to return
 */
void free_xenballooned_pages(int nr_pages, struct page** pages)
{
	int i;

	mutex_lock(&balloon_mutex);

	for (i = 0; i < nr_pages; i++) {
		if (pages[i])
			balloon_append(pages[i]);
	}

	/* The balloon may be too large now. Shrink it if needed. */
	if (current_credit())
		schedule_delayed_work(&balloon_worker, 0);

	mutex_unlock(&balloon_mutex);
}
EXPORT_SYMBOL(free_xenballooned_pages);

static int __init balloon_init(void)
{
	unsigned long pfn, extra_pfn_end;
	struct page *page;

	if (!xen_domain())
		return -ENODEV;

	pr_info("xen/balloon: Initialising balloon driver.\n");

	balloon_stats.current_pages = xen_pv_domain() ? min(xen_start_info->nr_pages, max_pfn) : max_pfn;
	balloon_stats.target_pages  = balloon_stats.current_pages;
	balloon_stats.balloon_low   = 0;
	balloon_stats.balloon_high  = 0;

	balloon_stats.schedule_delay = 1;
	balloon_stats.max_schedule_delay = 32;
	balloon_stats.retry_count = 1;
	balloon_stats.max_retry_count = RETRY_UNLIMITED;

	/*
	 * Initialise the balloon with excess memory space.  We need
	 * to make sure we don't add memory which doesn't exist or
	 * logically exist.  The E820 map can be trimmed to be smaller
	 * than the amount of physical memory due to the mem= command
	 * line parameter.  And if this is a 32-bit non-HIGHMEM kernel
	 * on a system with memory which requires highmem to access,
	 * don't try to use it.
	 */
	extra_pfn_end = min(min(max_pfn, e820_end_of_ram_pfn()),
			    (unsigned long)PFN_DOWN(xen_extra_mem_start + xen_extra_mem_size));
	for (pfn = PFN_UP(xen_extra_mem_start);
	     pfn < extra_pfn_end;
	     pfn++) {
		page = pfn_to_page(pfn);
		/* totalram_pages and totalhigh_pages do not include the boot-time
		   balloon extension, so don't subtract from it. */
		__balloon_append(page);
	}

	return 0;
}

subsys_initcall(balloon_init);

MODULE_LICENSE("GPL");
