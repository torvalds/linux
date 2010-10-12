/******************************************************************************
 * balloon.c
 *
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
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/bootmem.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/sysdev.h>
#include <linux/gfp.h>

#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <asm/tlb.h>

#include <asm/xen/hypervisor.h>
#include <asm/xen/hypercall.h>

#include <xen/xen.h>
#include <xen/interface/xen.h>
#include <xen/interface/memory.h>
#include <xen/xenbus.h>
#include <xen/features.h>
#include <xen/page.h>

#define PAGES2KB(_p) ((_p)<<(PAGE_SHIFT-10))

#define BALLOON_CLASS_NAME "xen_memory"

struct balloon_stats {
	/* We aim for 'current allocation' == 'target allocation'. */
	unsigned long current_pages;
	unsigned long target_pages;
	/*
	 * Drivers may alter the memory reservation independently, but they
	 * must inform the balloon driver so we avoid hitting the hard limit.
	 */
	unsigned long driver_pages;
	/* Number of pages in high- and low-memory balloons. */
	unsigned long balloon_low;
	unsigned long balloon_high;
};

static DEFINE_MUTEX(balloon_mutex);

static struct sys_device balloon_sysdev;

static int register_balloon(struct sys_device *sysdev);

static struct balloon_stats balloon_stats;

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
static DECLARE_WORK(balloon_worker, balloon_process);
static struct timer_list balloon_timer;

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
static void balloon_append(struct page *page)
{
	/* Lowmem is re-populated first, so highmem pages go at list tail. */
	if (PageHighMem(page)) {
		list_add_tail(&page->lru, &ballooned_pages);
		balloon_stats.balloon_high++;
		dec_totalhigh_pages();
	} else {
		list_add(&page->lru, &ballooned_pages);
		balloon_stats.balloon_low++;
	}

	totalram_pages--;
}

/* balloon_retrieve: rescue a page from the balloon, if it is not empty. */
static struct page *balloon_retrieve(void)
{
	struct page *page;

	if (list_empty(&ballooned_pages))
		return NULL;

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

static void balloon_alarm(unsigned long unused)
{
	schedule_work(&balloon_worker);
}

static unsigned long current_target(void)
{
	unsigned long target = balloon_stats.target_pages;

	target = min(target,
		     balloon_stats.current_pages +
		     balloon_stats.balloon_low +
		     balloon_stats.balloon_high);

	return target;
}

static int increase_reservation(unsigned long nr_pages)
{
	unsigned long  pfn, i, flags;
	struct page   *page;
	long           rc;
	struct xen_memory_reservation reservation = {
		.address_bits = 0,
		.extent_order = 0,
		.domid        = DOMID_SELF
	};

	if (nr_pages > ARRAY_SIZE(frame_list))
		nr_pages = ARRAY_SIZE(frame_list);

	spin_lock_irqsave(&xen_reservation_lock, flags);

	page = balloon_first_page();
	for (i = 0; i < nr_pages; i++) {
		BUG_ON(page == NULL);
		frame_list[i] = page_to_pfn(page);
		page = balloon_next_page(page);
	}

	set_xen_guest_handle(reservation.extent_start, frame_list);
	reservation.nr_extents = nr_pages;
	rc = HYPERVISOR_memory_op(XENMEM_populate_physmap, &reservation);
	if (rc < 0)
		goto out;

	for (i = 0; i < rc; i++) {
		page = balloon_retrieve();
		BUG_ON(page == NULL);

		pfn = page_to_pfn(page);
		BUG_ON(!xen_feature(XENFEAT_auto_translated_physmap) &&
		       phys_to_machine_mapping_valid(pfn));

		set_phys_to_machine(pfn, frame_list[i]);

		/* Link back into the page tables if not highmem. */
		if (pfn < max_low_pfn) {
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

 out:
	spin_unlock_irqrestore(&xen_reservation_lock, flags);

	return rc < 0 ? rc : rc != nr_pages;
}

static int decrease_reservation(unsigned long nr_pages)
{
	unsigned long  pfn, i, flags;
	struct page   *page;
	int            need_sleep = 0;
	int ret;
	struct xen_memory_reservation reservation = {
		.address_bits = 0,
		.extent_order = 0,
		.domid        = DOMID_SELF
	};

	if (nr_pages > ARRAY_SIZE(frame_list))
		nr_pages = ARRAY_SIZE(frame_list);

	for (i = 0; i < nr_pages; i++) {
		if ((page = alloc_page(GFP_BALLOON)) == NULL) {
			nr_pages = i;
			need_sleep = 1;
			break;
		}

		pfn = page_to_pfn(page);
		frame_list[i] = pfn_to_mfn(pfn);

		scrub_page(page);

		if (!PageHighMem(page)) {
			ret = HYPERVISOR_update_va_mapping(
				(unsigned long)__va(pfn << PAGE_SHIFT),
				__pte_ma(0), 0);
			BUG_ON(ret);
                }

	}

	/* Ensure that ballooned highmem pages don't have kmaps. */
	kmap_flush_unused();
	flush_tlb_all();

	spin_lock_irqsave(&xen_reservation_lock, flags);

	/* No more mappings: invalidate P2M and add to balloon. */
	for (i = 0; i < nr_pages; i++) {
		pfn = mfn_to_pfn(frame_list[i]);
		set_phys_to_machine(pfn, INVALID_P2M_ENTRY);
		balloon_append(pfn_to_page(pfn));
	}

	set_xen_guest_handle(reservation.extent_start, frame_list);
	reservation.nr_extents   = nr_pages;
	ret = HYPERVISOR_memory_op(XENMEM_decrease_reservation, &reservation);
	BUG_ON(ret != nr_pages);

	balloon_stats.current_pages -= nr_pages;

	spin_unlock_irqrestore(&xen_reservation_lock, flags);

	return need_sleep;
}

/*
 * We avoid multiple worker processes conflicting via the balloon mutex.
 * We may of course race updates of the target counts (which are protected
 * by the balloon lock), or with changes to the Xen hard limit, but we will
 * recover from these in time.
 */
static void balloon_process(struct work_struct *work)
{
	int need_sleep = 0;
	long credit;

	mutex_lock(&balloon_mutex);

	do {
		credit = current_target() - balloon_stats.current_pages;
		if (credit > 0)
			need_sleep = (increase_reservation(credit) != 0);
		if (credit < 0)
			need_sleep = (decrease_reservation(-credit) != 0);

#ifndef CONFIG_PREEMPT
		if (need_resched())
			schedule();
#endif
	} while ((credit != 0) && !need_sleep);

	/* Schedule more work if there is some still to be done. */
	if (current_target() != balloon_stats.current_pages)
		mod_timer(&balloon_timer, jiffies + HZ);

	mutex_unlock(&balloon_mutex);
}

/* Resets the Xen limit, sets new target, and kicks off processing. */
static void balloon_set_new_target(unsigned long target)
{
	/* No need for lock. Not read-modify-write updates. */
	balloon_stats.target_pages = target;
	schedule_work(&balloon_worker);
}

static struct xenbus_watch target_watch =
{
	.node = "memory/target"
};

/* React to a change in the target key */
static void watch_target(struct xenbus_watch *watch,
			 const char **vec, unsigned int len)
{
	unsigned long long new_target;
	int err;

	err = xenbus_scanf(XBT_NIL, "memory", "target", "%llu", &new_target);
	if (err != 1) {
		/* This is ok (for domain0 at least) - so just return */
		return;
	}

	/* The given memory/target value is in KiB, so it needs converting to
	 * pages. PAGE_SHIFT converts bytes to pages, hence PAGE_SHIFT - 10.
	 */
	balloon_set_new_target(new_target >> (PAGE_SHIFT - 10));
}

static int balloon_init_watcher(struct notifier_block *notifier,
				unsigned long event,
				void *data)
{
	int err;

	err = register_xenbus_watch(&target_watch);
	if (err)
		printk(KERN_ERR "Failed to set balloon watcher\n");

	return NOTIFY_DONE;
}

static struct notifier_block xenstore_notifier;

static int __init balloon_init(void)
{
	unsigned long pfn;
	struct page *page;

	if (!xen_pv_domain())
		return -ENODEV;

	pr_info("xen_balloon: Initialising balloon driver.\n");

	balloon_stats.current_pages = min(xen_start_info->nr_pages, max_pfn);
	balloon_stats.target_pages  = balloon_stats.current_pages;
	balloon_stats.balloon_low   = 0;
	balloon_stats.balloon_high  = 0;
	balloon_stats.driver_pages  = 0UL;

	init_timer(&balloon_timer);
	balloon_timer.data = 0;
	balloon_timer.function = balloon_alarm;

	register_balloon(&balloon_sysdev);

	/* Initialise the balloon with excess memory space. */
	for (pfn = xen_start_info->nr_pages; pfn < max_pfn; pfn++) {
		page = pfn_to_page(pfn);
		if (!PageReserved(page))
			balloon_append(page);
	}

	target_watch.callback = watch_target;
	xenstore_notifier.notifier_call = balloon_init_watcher;

	register_xenstore_notifier(&xenstore_notifier);

	return 0;
}

subsys_initcall(balloon_init);

static void balloon_exit(void)
{
    /* XXX - release balloon here */
    return;
}

module_exit(balloon_exit);

#define BALLOON_SHOW(name, format, args...)				\
	static ssize_t show_##name(struct sys_device *dev,		\
				   struct sysdev_attribute *attr,	\
				   char *buf)				\
	{								\
		return sprintf(buf, format, ##args);			\
	}								\
	static SYSDEV_ATTR(name, S_IRUGO, show_##name, NULL)

BALLOON_SHOW(current_kb, "%lu\n", PAGES2KB(balloon_stats.current_pages));
BALLOON_SHOW(low_kb, "%lu\n", PAGES2KB(balloon_stats.balloon_low));
BALLOON_SHOW(high_kb, "%lu\n", PAGES2KB(balloon_stats.balloon_high));
BALLOON_SHOW(driver_kb, "%lu\n", PAGES2KB(balloon_stats.driver_pages));

static ssize_t show_target_kb(struct sys_device *dev, struct sysdev_attribute *attr,
			      char *buf)
{
	return sprintf(buf, "%lu\n", PAGES2KB(balloon_stats.target_pages));
}

static ssize_t store_target_kb(struct sys_device *dev,
			       struct sysdev_attribute *attr,
			       const char *buf,
			       size_t count)
{
	char *endchar;
	unsigned long long target_bytes;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	target_bytes = simple_strtoull(buf, &endchar, 0) * 1024;

	balloon_set_new_target(target_bytes >> PAGE_SHIFT);

	return count;
}

static SYSDEV_ATTR(target_kb, S_IRUGO | S_IWUSR,
		   show_target_kb, store_target_kb);


static ssize_t show_target(struct sys_device *dev, struct sysdev_attribute *attr,
			      char *buf)
{
	return sprintf(buf, "%llu\n",
		       (unsigned long long)balloon_stats.target_pages
		       << PAGE_SHIFT);
}

static ssize_t store_target(struct sys_device *dev,
			    struct sysdev_attribute *attr,
			    const char *buf,
			    size_t count)
{
	char *endchar;
	unsigned long long target_bytes;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	target_bytes = memparse(buf, &endchar);

	balloon_set_new_target(target_bytes >> PAGE_SHIFT);

	return count;
}

static SYSDEV_ATTR(target, S_IRUGO | S_IWUSR,
		   show_target, store_target);


static struct sysdev_attribute *balloon_attrs[] = {
	&attr_target_kb,
	&attr_target,
};

static struct attribute *balloon_info_attrs[] = {
	&attr_current_kb.attr,
	&attr_low_kb.attr,
	&attr_high_kb.attr,
	&attr_driver_kb.attr,
	NULL
};

static struct attribute_group balloon_info_group = {
	.name = "info",
	.attrs = balloon_info_attrs,
};

static struct sysdev_class balloon_sysdev_class = {
	.name = BALLOON_CLASS_NAME,
};

static int register_balloon(struct sys_device *sysdev)
{
	int i, error;

	error = sysdev_class_register(&balloon_sysdev_class);
	if (error)
		return error;

	sysdev->id = 0;
	sysdev->cls = &balloon_sysdev_class;

	error = sysdev_register(sysdev);
	if (error) {
		sysdev_class_unregister(&balloon_sysdev_class);
		return error;
	}

	for (i = 0; i < ARRAY_SIZE(balloon_attrs); i++) {
		error = sysdev_create_file(sysdev, balloon_attrs[i]);
		if (error)
			goto fail;
	}

	error = sysfs_create_group(&sysdev->kobj, &balloon_info_group);
	if (error)
		goto fail;

	return 0;

 fail:
	while (--i >= 0)
		sysdev_remove_file(sysdev, balloon_attrs[i]);
	sysdev_unregister(sysdev);
	sysdev_class_unregister(&balloon_sysdev_class);
	return error;
}

MODULE_LICENSE("GPL");
