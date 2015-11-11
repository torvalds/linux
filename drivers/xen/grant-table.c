/******************************************************************************
 * grant_table.c
 *
 * Granting foreign access to our memory reservation.
 *
 * Copyright (c) 2005-2006, Christopher Clark
 * Copyright (c) 2004-2005, K A Fraser
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

#define pr_fmt(fmt) "xen:" KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/hardirq.h>
#include <linux/workqueue.h>

#include <xen/xen.h>
#include <xen/interface/xen.h>
#include <xen/page.h>
#include <xen/grant_table.h>
#include <xen/interface/memory.h>
#include <xen/hvc-console.h>
#include <xen/swiotlb-xen.h>
#include <xen/balloon.h>
#include <asm/xen/hypercall.h>
#include <asm/xen/interface.h>

#include <asm/pgtable.h>
#include <asm/sync_bitops.h>

/* External tools reserve first few grant table entries. */
#define NR_RESERVED_ENTRIES 8
#define GNTTAB_LIST_END 0xffffffff

static grant_ref_t **gnttab_list;
static unsigned int nr_grant_frames;
static int gnttab_free_count;
static grant_ref_t gnttab_free_head;
static DEFINE_SPINLOCK(gnttab_list_lock);
struct grant_frames xen_auto_xlat_grant_frames;

static union {
	struct grant_entry_v1 *v1;
	void *addr;
} gnttab_shared;

/*This is a structure of function pointers for grant table*/
struct gnttab_ops {
	/*
	 * Mapping a list of frames for storing grant entries. Frames parameter
	 * is used to store grant table address when grant table being setup,
	 * nr_gframes is the number of frames to map grant table. Returning
	 * GNTST_okay means success and negative value means failure.
	 */
	int (*map_frames)(xen_pfn_t *frames, unsigned int nr_gframes);
	/*
	 * Release a list of frames which are mapped in map_frames for grant
	 * entry status.
	 */
	void (*unmap_frames)(void);
	/*
	 * Introducing a valid entry into the grant table, granting the frame of
	 * this grant entry to domain for accessing or transfering. Ref
	 * parameter is reference of this introduced grant entry, domid is id of
	 * granted domain, frame is the page frame to be granted, and flags is
	 * status of the grant entry to be updated.
	 */
	void (*update_entry)(grant_ref_t ref, domid_t domid,
			     unsigned long frame, unsigned flags);
	/*
	 * Stop granting a grant entry to domain for accessing. Ref parameter is
	 * reference of a grant entry whose grant access will be stopped,
	 * readonly is not in use in this function. If the grant entry is
	 * currently mapped for reading or writing, just return failure(==0)
	 * directly and don't tear down the grant access. Otherwise, stop grant
	 * access for this entry and return success(==1).
	 */
	int (*end_foreign_access_ref)(grant_ref_t ref, int readonly);
	/*
	 * Stop granting a grant entry to domain for transfer. Ref parameter is
	 * reference of a grant entry whose grant transfer will be stopped. If
	 * tranfer has not started, just reclaim the grant entry and return
	 * failure(==0). Otherwise, wait for the transfer to complete and then
	 * return the frame.
	 */
	unsigned long (*end_foreign_transfer_ref)(grant_ref_t ref);
	/*
	 * Query the status of a grant entry. Ref parameter is reference of
	 * queried grant entry, return value is the status of queried entry.
	 * Detailed status(writing/reading) can be gotten from the return value
	 * by bit operations.
	 */
	int (*query_foreign_access)(grant_ref_t ref);
};

struct unmap_refs_callback_data {
	struct completion completion;
	int result;
};

static struct gnttab_ops *gnttab_interface;

static int grant_table_version;
static int grefs_per_grant_frame;

static struct gnttab_free_callback *gnttab_free_callback_list;

static int gnttab_expand(unsigned int req_entries);

#define RPP (PAGE_SIZE / sizeof(grant_ref_t))

static inline grant_ref_t *__gnttab_entry(grant_ref_t entry)
{
	return &gnttab_list[(entry) / RPP][(entry) % RPP];
}
/* This can be used as an l-value */
#define gnttab_entry(entry) (*__gnttab_entry(entry))

static int get_free_entries(unsigned count)
{
	unsigned long flags;
	int ref, rc = 0;
	grant_ref_t head;

	spin_lock_irqsave(&gnttab_list_lock, flags);

	if ((gnttab_free_count < count) &&
	    ((rc = gnttab_expand(count - gnttab_free_count)) < 0)) {
		spin_unlock_irqrestore(&gnttab_list_lock, flags);
		return rc;
	}

	ref = head = gnttab_free_head;
	gnttab_free_count -= count;
	while (count-- > 1)
		head = gnttab_entry(head);
	gnttab_free_head = gnttab_entry(head);
	gnttab_entry(head) = GNTTAB_LIST_END;

	spin_unlock_irqrestore(&gnttab_list_lock, flags);

	return ref;
}

static void do_free_callbacks(void)
{
	struct gnttab_free_callback *callback, *next;

	callback = gnttab_free_callback_list;
	gnttab_free_callback_list = NULL;

	while (callback != NULL) {
		next = callback->next;
		if (gnttab_free_count >= callback->count) {
			callback->next = NULL;
			callback->fn(callback->arg);
		} else {
			callback->next = gnttab_free_callback_list;
			gnttab_free_callback_list = callback;
		}
		callback = next;
	}
}

static inline void check_free_callbacks(void)
{
	if (unlikely(gnttab_free_callback_list))
		do_free_callbacks();
}

static void put_free_entry(grant_ref_t ref)
{
	unsigned long flags;
	spin_lock_irqsave(&gnttab_list_lock, flags);
	gnttab_entry(ref) = gnttab_free_head;
	gnttab_free_head = ref;
	gnttab_free_count++;
	check_free_callbacks();
	spin_unlock_irqrestore(&gnttab_list_lock, flags);
}

/*
 * Following applies to gnttab_update_entry_v1.
 * Introducing a valid entry into the grant table:
 *  1. Write ent->domid.
 *  2. Write ent->frame:
 *      GTF_permit_access:   Frame to which access is permitted.
 *      GTF_accept_transfer: Pseudo-phys frame slot being filled by new
 *                           frame, or zero if none.
 *  3. Write memory barrier (WMB).
 *  4. Write ent->flags, inc. valid type.
 */
static void gnttab_update_entry_v1(grant_ref_t ref, domid_t domid,
				   unsigned long frame, unsigned flags)
{
	gnttab_shared.v1[ref].domid = domid;
	gnttab_shared.v1[ref].frame = frame;
	wmb();
	gnttab_shared.v1[ref].flags = flags;
}

/*
 * Public grant-issuing interface functions
 */
void gnttab_grant_foreign_access_ref(grant_ref_t ref, domid_t domid,
				     unsigned long frame, int readonly)
{
	gnttab_interface->update_entry(ref, domid, frame,
			   GTF_permit_access | (readonly ? GTF_readonly : 0));
}
EXPORT_SYMBOL_GPL(gnttab_grant_foreign_access_ref);

int gnttab_grant_foreign_access(domid_t domid, unsigned long frame,
				int readonly)
{
	int ref;

	ref = get_free_entries(1);
	if (unlikely(ref < 0))
		return -ENOSPC;

	gnttab_grant_foreign_access_ref(ref, domid, frame, readonly);

	return ref;
}
EXPORT_SYMBOL_GPL(gnttab_grant_foreign_access);

static int gnttab_query_foreign_access_v1(grant_ref_t ref)
{
	return gnttab_shared.v1[ref].flags & (GTF_reading|GTF_writing);
}

int gnttab_query_foreign_access(grant_ref_t ref)
{
	return gnttab_interface->query_foreign_access(ref);
}
EXPORT_SYMBOL_GPL(gnttab_query_foreign_access);

static int gnttab_end_foreign_access_ref_v1(grant_ref_t ref, int readonly)
{
	u16 flags, nflags;
	u16 *pflags;

	pflags = &gnttab_shared.v1[ref].flags;
	nflags = *pflags;
	do {
		flags = nflags;
		if (flags & (GTF_reading|GTF_writing))
			return 0;
	} while ((nflags = sync_cmpxchg(pflags, flags, 0)) != flags);

	return 1;
}

static inline int _gnttab_end_foreign_access_ref(grant_ref_t ref, int readonly)
{
	return gnttab_interface->end_foreign_access_ref(ref, readonly);
}

int gnttab_end_foreign_access_ref(grant_ref_t ref, int readonly)
{
	if (_gnttab_end_foreign_access_ref(ref, readonly))
		return 1;
	pr_warn("WARNING: g.e. %#x still in use!\n", ref);
	return 0;
}
EXPORT_SYMBOL_GPL(gnttab_end_foreign_access_ref);

struct deferred_entry {
	struct list_head list;
	grant_ref_t ref;
	bool ro;
	uint16_t warn_delay;
	struct page *page;
};
static LIST_HEAD(deferred_list);
static void gnttab_handle_deferred(unsigned long);
static DEFINE_TIMER(deferred_timer, gnttab_handle_deferred, 0, 0);

static void gnttab_handle_deferred(unsigned long unused)
{
	unsigned int nr = 10;
	struct deferred_entry *first = NULL;
	unsigned long flags;

	spin_lock_irqsave(&gnttab_list_lock, flags);
	while (nr--) {
		struct deferred_entry *entry
			= list_first_entry(&deferred_list,
					   struct deferred_entry, list);

		if (entry == first)
			break;
		list_del(&entry->list);
		spin_unlock_irqrestore(&gnttab_list_lock, flags);
		if (_gnttab_end_foreign_access_ref(entry->ref, entry->ro)) {
			put_free_entry(entry->ref);
			if (entry->page) {
				pr_debug("freeing g.e. %#x (pfn %#lx)\n",
					 entry->ref, page_to_pfn(entry->page));
				__free_page(entry->page);
			} else
				pr_info("freeing g.e. %#x\n", entry->ref);
			kfree(entry);
			entry = NULL;
		} else {
			if (!--entry->warn_delay)
				pr_info("g.e. %#x still pending\n", entry->ref);
			if (!first)
				first = entry;
		}
		spin_lock_irqsave(&gnttab_list_lock, flags);
		if (entry)
			list_add_tail(&entry->list, &deferred_list);
		else if (list_empty(&deferred_list))
			break;
	}
	if (!list_empty(&deferred_list) && !timer_pending(&deferred_timer)) {
		deferred_timer.expires = jiffies + HZ;
		add_timer(&deferred_timer);
	}
	spin_unlock_irqrestore(&gnttab_list_lock, flags);
}

static void gnttab_add_deferred(grant_ref_t ref, bool readonly,
				struct page *page)
{
	struct deferred_entry *entry = kmalloc(sizeof(*entry), GFP_ATOMIC);
	const char *what = KERN_WARNING "leaking";

	if (entry) {
		unsigned long flags;

		entry->ref = ref;
		entry->ro = readonly;
		entry->page = page;
		entry->warn_delay = 60;
		spin_lock_irqsave(&gnttab_list_lock, flags);
		list_add_tail(&entry->list, &deferred_list);
		if (!timer_pending(&deferred_timer)) {
			deferred_timer.expires = jiffies + HZ;
			add_timer(&deferred_timer);
		}
		spin_unlock_irqrestore(&gnttab_list_lock, flags);
		what = KERN_DEBUG "deferring";
	}
	printk("%s g.e. %#x (pfn %#lx)\n",
	       what, ref, page ? page_to_pfn(page) : -1);
}

void gnttab_end_foreign_access(grant_ref_t ref, int readonly,
			       unsigned long page)
{
	if (gnttab_end_foreign_access_ref(ref, readonly)) {
		put_free_entry(ref);
		if (page != 0)
			free_page(page);
	} else
		gnttab_add_deferred(ref, readonly,
				    page ? virt_to_page(page) : NULL);
}
EXPORT_SYMBOL_GPL(gnttab_end_foreign_access);

int gnttab_grant_foreign_transfer(domid_t domid, unsigned long pfn)
{
	int ref;

	ref = get_free_entries(1);
	if (unlikely(ref < 0))
		return -ENOSPC;
	gnttab_grant_foreign_transfer_ref(ref, domid, pfn);

	return ref;
}
EXPORT_SYMBOL_GPL(gnttab_grant_foreign_transfer);

void gnttab_grant_foreign_transfer_ref(grant_ref_t ref, domid_t domid,
				       unsigned long pfn)
{
	gnttab_interface->update_entry(ref, domid, pfn, GTF_accept_transfer);
}
EXPORT_SYMBOL_GPL(gnttab_grant_foreign_transfer_ref);

static unsigned long gnttab_end_foreign_transfer_ref_v1(grant_ref_t ref)
{
	unsigned long frame;
	u16           flags;
	u16          *pflags;

	pflags = &gnttab_shared.v1[ref].flags;

	/*
	 * If a transfer is not even yet started, try to reclaim the grant
	 * reference and return failure (== 0).
	 */
	while (!((flags = *pflags) & GTF_transfer_committed)) {
		if (sync_cmpxchg(pflags, flags, 0) == flags)
			return 0;
		cpu_relax();
	}

	/* If a transfer is in progress then wait until it is completed. */
	while (!(flags & GTF_transfer_completed)) {
		flags = *pflags;
		cpu_relax();
	}

	rmb();	/* Read the frame number /after/ reading completion status. */
	frame = gnttab_shared.v1[ref].frame;
	BUG_ON(frame == 0);

	return frame;
}

unsigned long gnttab_end_foreign_transfer_ref(grant_ref_t ref)
{
	return gnttab_interface->end_foreign_transfer_ref(ref);
}
EXPORT_SYMBOL_GPL(gnttab_end_foreign_transfer_ref);

unsigned long gnttab_end_foreign_transfer(grant_ref_t ref)
{
	unsigned long frame = gnttab_end_foreign_transfer_ref(ref);
	put_free_entry(ref);
	return frame;
}
EXPORT_SYMBOL_GPL(gnttab_end_foreign_transfer);

void gnttab_free_grant_reference(grant_ref_t ref)
{
	put_free_entry(ref);
}
EXPORT_SYMBOL_GPL(gnttab_free_grant_reference);

void gnttab_free_grant_references(grant_ref_t head)
{
	grant_ref_t ref;
	unsigned long flags;
	int count = 1;
	if (head == GNTTAB_LIST_END)
		return;
	spin_lock_irqsave(&gnttab_list_lock, flags);
	ref = head;
	while (gnttab_entry(ref) != GNTTAB_LIST_END) {
		ref = gnttab_entry(ref);
		count++;
	}
	gnttab_entry(ref) = gnttab_free_head;
	gnttab_free_head = head;
	gnttab_free_count += count;
	check_free_callbacks();
	spin_unlock_irqrestore(&gnttab_list_lock, flags);
}
EXPORT_SYMBOL_GPL(gnttab_free_grant_references);

int gnttab_alloc_grant_references(u16 count, grant_ref_t *head)
{
	int h = get_free_entries(count);

	if (h < 0)
		return -ENOSPC;

	*head = h;

	return 0;
}
EXPORT_SYMBOL_GPL(gnttab_alloc_grant_references);

int gnttab_empty_grant_references(const grant_ref_t *private_head)
{
	return (*private_head == GNTTAB_LIST_END);
}
EXPORT_SYMBOL_GPL(gnttab_empty_grant_references);

int gnttab_claim_grant_reference(grant_ref_t *private_head)
{
	grant_ref_t g = *private_head;
	if (unlikely(g == GNTTAB_LIST_END))
		return -ENOSPC;
	*private_head = gnttab_entry(g);
	return g;
}
EXPORT_SYMBOL_GPL(gnttab_claim_grant_reference);

void gnttab_release_grant_reference(grant_ref_t *private_head,
				    grant_ref_t release)
{
	gnttab_entry(release) = *private_head;
	*private_head = release;
}
EXPORT_SYMBOL_GPL(gnttab_release_grant_reference);

void gnttab_request_free_callback(struct gnttab_free_callback *callback,
				  void (*fn)(void *), void *arg, u16 count)
{
	unsigned long flags;
	struct gnttab_free_callback *cb;

	spin_lock_irqsave(&gnttab_list_lock, flags);

	/* Check if the callback is already on the list */
	cb = gnttab_free_callback_list;
	while (cb) {
		if (cb == callback)
			goto out;
		cb = cb->next;
	}

	callback->fn = fn;
	callback->arg = arg;
	callback->count = count;
	callback->next = gnttab_free_callback_list;
	gnttab_free_callback_list = callback;
	check_free_callbacks();
out:
	spin_unlock_irqrestore(&gnttab_list_lock, flags);
}
EXPORT_SYMBOL_GPL(gnttab_request_free_callback);

void gnttab_cancel_free_callback(struct gnttab_free_callback *callback)
{
	struct gnttab_free_callback **pcb;
	unsigned long flags;

	spin_lock_irqsave(&gnttab_list_lock, flags);
	for (pcb = &gnttab_free_callback_list; *pcb; pcb = &(*pcb)->next) {
		if (*pcb == callback) {
			*pcb = callback->next;
			break;
		}
	}
	spin_unlock_irqrestore(&gnttab_list_lock, flags);
}
EXPORT_SYMBOL_GPL(gnttab_cancel_free_callback);

static int grow_gnttab_list(unsigned int more_frames)
{
	unsigned int new_nr_grant_frames, extra_entries, i;
	unsigned int nr_glist_frames, new_nr_glist_frames;

	BUG_ON(grefs_per_grant_frame == 0);

	new_nr_grant_frames = nr_grant_frames + more_frames;
	extra_entries       = more_frames * grefs_per_grant_frame;

	nr_glist_frames = (nr_grant_frames * grefs_per_grant_frame + RPP - 1) / RPP;
	new_nr_glist_frames =
		(new_nr_grant_frames * grefs_per_grant_frame + RPP - 1) / RPP;
	for (i = nr_glist_frames; i < new_nr_glist_frames; i++) {
		gnttab_list[i] = (grant_ref_t *)__get_free_page(GFP_ATOMIC);
		if (!gnttab_list[i])
			goto grow_nomem;
	}


	for (i = grefs_per_grant_frame * nr_grant_frames;
	     i < grefs_per_grant_frame * new_nr_grant_frames - 1; i++)
		gnttab_entry(i) = i + 1;

	gnttab_entry(i) = gnttab_free_head;
	gnttab_free_head = grefs_per_grant_frame * nr_grant_frames;
	gnttab_free_count += extra_entries;

	nr_grant_frames = new_nr_grant_frames;

	check_free_callbacks();

	return 0;

grow_nomem:
	while (i-- > nr_glist_frames)
		free_page((unsigned long) gnttab_list[i]);
	return -ENOMEM;
}

static unsigned int __max_nr_grant_frames(void)
{
	struct gnttab_query_size query;
	int rc;

	query.dom = DOMID_SELF;

	rc = HYPERVISOR_grant_table_op(GNTTABOP_query_size, &query, 1);
	if ((rc < 0) || (query.status != GNTST_okay))
		return 4; /* Legacy max supported number of frames */

	return query.max_nr_frames;
}

unsigned int gnttab_max_grant_frames(void)
{
	unsigned int xen_max = __max_nr_grant_frames();
	static unsigned int boot_max_nr_grant_frames;

	/* First time, initialize it properly. */
	if (!boot_max_nr_grant_frames)
		boot_max_nr_grant_frames = __max_nr_grant_frames();

	if (xen_max > boot_max_nr_grant_frames)
		return boot_max_nr_grant_frames;
	return xen_max;
}
EXPORT_SYMBOL_GPL(gnttab_max_grant_frames);

int gnttab_setup_auto_xlat_frames(phys_addr_t addr)
{
	xen_pfn_t *pfn;
	unsigned int max_nr_gframes = __max_nr_grant_frames();
	unsigned int i;
	void *vaddr;

	if (xen_auto_xlat_grant_frames.count)
		return -EINVAL;

	vaddr = xen_remap(addr, XEN_PAGE_SIZE * max_nr_gframes);
	if (vaddr == NULL) {
		pr_warn("Failed to ioremap gnttab share frames (addr=%pa)!\n",
			&addr);
		return -ENOMEM;
	}
	pfn = kcalloc(max_nr_gframes, sizeof(pfn[0]), GFP_KERNEL);
	if (!pfn) {
		xen_unmap(vaddr);
		return -ENOMEM;
	}
	for (i = 0; i < max_nr_gframes; i++)
		pfn[i] = XEN_PFN_DOWN(addr) + i;

	xen_auto_xlat_grant_frames.vaddr = vaddr;
	xen_auto_xlat_grant_frames.pfn = pfn;
	xen_auto_xlat_grant_frames.count = max_nr_gframes;

	return 0;
}
EXPORT_SYMBOL_GPL(gnttab_setup_auto_xlat_frames);

void gnttab_free_auto_xlat_frames(void)
{
	if (!xen_auto_xlat_grant_frames.count)
		return;
	kfree(xen_auto_xlat_grant_frames.pfn);
	xen_unmap(xen_auto_xlat_grant_frames.vaddr);

	xen_auto_xlat_grant_frames.pfn = NULL;
	xen_auto_xlat_grant_frames.count = 0;
	xen_auto_xlat_grant_frames.vaddr = NULL;
}
EXPORT_SYMBOL_GPL(gnttab_free_auto_xlat_frames);

/**
 * gnttab_alloc_pages - alloc pages suitable for grant mapping into
 * @nr_pages: number of pages to alloc
 * @pages: returns the pages
 */
int gnttab_alloc_pages(int nr_pages, struct page **pages)
{
	int i;
	int ret;

	ret = alloc_xenballooned_pages(nr_pages, pages);
	if (ret < 0)
		return ret;

	for (i = 0; i < nr_pages; i++) {
#if BITS_PER_LONG < 64
		struct xen_page_foreign *foreign;

		foreign = kzalloc(sizeof(*foreign), GFP_KERNEL);
		if (!foreign) {
			gnttab_free_pages(nr_pages, pages);
			return -ENOMEM;
		}
		set_page_private(pages[i], (unsigned long)foreign);
#endif
		SetPagePrivate(pages[i]);
	}

	return 0;
}
EXPORT_SYMBOL(gnttab_alloc_pages);

/**
 * gnttab_free_pages - free pages allocated by gnttab_alloc_pages()
 * @nr_pages; number of pages to free
 * @pages: the pages
 */
void gnttab_free_pages(int nr_pages, struct page **pages)
{
	int i;

	for (i = 0; i < nr_pages; i++) {
		if (PagePrivate(pages[i])) {
#if BITS_PER_LONG < 64
			kfree((void *)page_private(pages[i]));
#endif
			ClearPagePrivate(pages[i]);
		}
	}
	free_xenballooned_pages(nr_pages, pages);
}
EXPORT_SYMBOL(gnttab_free_pages);

/* Handling of paged out grant targets (GNTST_eagain) */
#define MAX_DELAY 256
static inline void
gnttab_retry_eagain_gop(unsigned int cmd, void *gop, int16_t *status,
						const char *func)
{
	unsigned delay = 1;

	do {
		BUG_ON(HYPERVISOR_grant_table_op(cmd, gop, 1));
		if (*status == GNTST_eagain)
			msleep(delay++);
	} while ((*status == GNTST_eagain) && (delay < MAX_DELAY));

	if (delay >= MAX_DELAY) {
		pr_err("%s: %s eagain grant\n", func, current->comm);
		*status = GNTST_bad_page;
	}
}

void gnttab_batch_map(struct gnttab_map_grant_ref *batch, unsigned count)
{
	struct gnttab_map_grant_ref *op;

	if (HYPERVISOR_grant_table_op(GNTTABOP_map_grant_ref, batch, count))
		BUG();
	for (op = batch; op < batch + count; op++)
		if (op->status == GNTST_eagain)
			gnttab_retry_eagain_gop(GNTTABOP_map_grant_ref, op,
						&op->status, __func__);
}
EXPORT_SYMBOL_GPL(gnttab_batch_map);

void gnttab_batch_copy(struct gnttab_copy *batch, unsigned count)
{
	struct gnttab_copy *op;

	if (HYPERVISOR_grant_table_op(GNTTABOP_copy, batch, count))
		BUG();
	for (op = batch; op < batch + count; op++)
		if (op->status == GNTST_eagain)
			gnttab_retry_eagain_gop(GNTTABOP_copy, op,
						&op->status, __func__);
}
EXPORT_SYMBOL_GPL(gnttab_batch_copy);

void gnttab_foreach_grant_in_range(struct page *page,
				   unsigned int offset,
				   unsigned int len,
				   xen_grant_fn_t fn,
				   void *data)
{
	unsigned int goffset;
	unsigned int glen;
	unsigned long xen_pfn;

	len = min_t(unsigned int, PAGE_SIZE - offset, len);
	goffset = xen_offset_in_page(offset);

	xen_pfn = page_to_xen_pfn(page) + XEN_PFN_DOWN(offset);

	while (len) {
		glen = min_t(unsigned int, XEN_PAGE_SIZE - goffset, len);
		fn(pfn_to_gfn(xen_pfn), goffset, glen, data);

		goffset = 0;
		xen_pfn++;
		len -= glen;
	}
}
EXPORT_SYMBOL_GPL(gnttab_foreach_grant_in_range);

void gnttab_foreach_grant(struct page **pages,
			  unsigned int nr_grefs,
			  xen_grant_fn_t fn,
			  void *data)
{
	unsigned int goffset = 0;
	unsigned long xen_pfn = 0;
	unsigned int i;

	for (i = 0; i < nr_grefs; i++) {
		if ((i % XEN_PFN_PER_PAGE) == 0) {
			xen_pfn = page_to_xen_pfn(pages[i / XEN_PFN_PER_PAGE]);
			goffset = 0;
		}

		fn(pfn_to_gfn(xen_pfn), goffset, XEN_PAGE_SIZE, data);

		goffset += XEN_PAGE_SIZE;
		xen_pfn++;
	}
}

int gnttab_map_refs(struct gnttab_map_grant_ref *map_ops,
		    struct gnttab_map_grant_ref *kmap_ops,
		    struct page **pages, unsigned int count)
{
	int i, ret;

	ret = HYPERVISOR_grant_table_op(GNTTABOP_map_grant_ref, map_ops, count);
	if (ret)
		return ret;

	for (i = 0; i < count; i++) {
		/* Retry eagain maps */
		if (map_ops[i].status == GNTST_eagain)
			gnttab_retry_eagain_gop(GNTTABOP_map_grant_ref, map_ops + i,
						&map_ops[i].status, __func__);

		if (map_ops[i].status == GNTST_okay) {
			struct xen_page_foreign *foreign;

			SetPageForeign(pages[i]);
			foreign = xen_page_foreign(pages[i]);
			foreign->domid = map_ops[i].dom;
			foreign->gref = map_ops[i].ref;
		}
	}

	return set_foreign_p2m_mapping(map_ops, kmap_ops, pages, count);
}
EXPORT_SYMBOL_GPL(gnttab_map_refs);

int gnttab_unmap_refs(struct gnttab_unmap_grant_ref *unmap_ops,
		      struct gnttab_unmap_grant_ref *kunmap_ops,
		      struct page **pages, unsigned int count)
{
	unsigned int i;
	int ret;

	ret = HYPERVISOR_grant_table_op(GNTTABOP_unmap_grant_ref, unmap_ops, count);
	if (ret)
		return ret;

	for (i = 0; i < count; i++)
		ClearPageForeign(pages[i]);

	return clear_foreign_p2m_mapping(unmap_ops, kunmap_ops, pages, count);
}
EXPORT_SYMBOL_GPL(gnttab_unmap_refs);

#define GNTTAB_UNMAP_REFS_DELAY 5

static void __gnttab_unmap_refs_async(struct gntab_unmap_queue_data* item);

static void gnttab_unmap_work(struct work_struct *work)
{
	struct gntab_unmap_queue_data
		*unmap_data = container_of(work, 
					   struct gntab_unmap_queue_data,
					   gnttab_work.work);
	if (unmap_data->age != UINT_MAX)
		unmap_data->age++;
	__gnttab_unmap_refs_async(unmap_data);
}

static void __gnttab_unmap_refs_async(struct gntab_unmap_queue_data* item)
{
	int ret;
	int pc;

	for (pc = 0; pc < item->count; pc++) {
		if (page_count(item->pages[pc]) > 1) {
			unsigned long delay = GNTTAB_UNMAP_REFS_DELAY * (item->age + 1);
			schedule_delayed_work(&item->gnttab_work,
					      msecs_to_jiffies(delay));
			return;
		}
	}

	ret = gnttab_unmap_refs(item->unmap_ops, item->kunmap_ops,
				item->pages, item->count);
	item->done(ret, item);
}

void gnttab_unmap_refs_async(struct gntab_unmap_queue_data* item)
{
	INIT_DELAYED_WORK(&item->gnttab_work, gnttab_unmap_work);
	item->age = 0;

	__gnttab_unmap_refs_async(item);
}
EXPORT_SYMBOL_GPL(gnttab_unmap_refs_async);

static void unmap_refs_callback(int result,
		struct gntab_unmap_queue_data *data)
{
	struct unmap_refs_callback_data *d = data->data;

	d->result = result;
	complete(&d->completion);
}

int gnttab_unmap_refs_sync(struct gntab_unmap_queue_data *item)
{
	struct unmap_refs_callback_data data;

	init_completion(&data.completion);
	item->data = &data;
	item->done = &unmap_refs_callback;
	gnttab_unmap_refs_async(item);
	wait_for_completion(&data.completion);

	return data.result;
}
EXPORT_SYMBOL_GPL(gnttab_unmap_refs_sync);

static int gnttab_map_frames_v1(xen_pfn_t *frames, unsigned int nr_gframes)
{
	int rc;

	rc = arch_gnttab_map_shared(frames, nr_gframes,
				    gnttab_max_grant_frames(),
				    &gnttab_shared.addr);
	BUG_ON(rc);

	return 0;
}

static void gnttab_unmap_frames_v1(void)
{
	arch_gnttab_unmap(gnttab_shared.addr, nr_grant_frames);
}

static int gnttab_map(unsigned int start_idx, unsigned int end_idx)
{
	struct gnttab_setup_table setup;
	xen_pfn_t *frames;
	unsigned int nr_gframes = end_idx + 1;
	int rc;

	if (xen_feature(XENFEAT_auto_translated_physmap)) {
		struct xen_add_to_physmap xatp;
		unsigned int i = end_idx;
		rc = 0;
		BUG_ON(xen_auto_xlat_grant_frames.count < nr_gframes);
		/*
		 * Loop backwards, so that the first hypercall has the largest
		 * index, ensuring that the table will grow only once.
		 */
		do {
			xatp.domid = DOMID_SELF;
			xatp.idx = i;
			xatp.space = XENMAPSPACE_grant_table;
			xatp.gpfn = xen_auto_xlat_grant_frames.pfn[i];
			rc = HYPERVISOR_memory_op(XENMEM_add_to_physmap, &xatp);
			if (rc != 0) {
				pr_warn("grant table add_to_physmap failed, err=%d\n",
					rc);
				break;
			}
		} while (i-- > start_idx);

		return rc;
	}

	/* No need for kzalloc as it is initialized in following hypercall
	 * GNTTABOP_setup_table.
	 */
	frames = kmalloc(nr_gframes * sizeof(unsigned long), GFP_ATOMIC);
	if (!frames)
		return -ENOMEM;

	setup.dom        = DOMID_SELF;
	setup.nr_frames  = nr_gframes;
	set_xen_guest_handle(setup.frame_list, frames);

	rc = HYPERVISOR_grant_table_op(GNTTABOP_setup_table, &setup, 1);
	if (rc == -ENOSYS) {
		kfree(frames);
		return -ENOSYS;
	}

	BUG_ON(rc || setup.status);

	rc = gnttab_interface->map_frames(frames, nr_gframes);

	kfree(frames);

	return rc;
}

static struct gnttab_ops gnttab_v1_ops = {
	.map_frames			= gnttab_map_frames_v1,
	.unmap_frames			= gnttab_unmap_frames_v1,
	.update_entry			= gnttab_update_entry_v1,
	.end_foreign_access_ref		= gnttab_end_foreign_access_ref_v1,
	.end_foreign_transfer_ref	= gnttab_end_foreign_transfer_ref_v1,
	.query_foreign_access		= gnttab_query_foreign_access_v1,
};

static void gnttab_request_version(void)
{
	/* Only version 1 is used, which will always be available. */
	grant_table_version = 1;
	grefs_per_grant_frame = XEN_PAGE_SIZE / sizeof(struct grant_entry_v1);
	gnttab_interface = &gnttab_v1_ops;

	pr_info("Grant tables using version %d layout\n", grant_table_version);
}

static int gnttab_setup(void)
{
	unsigned int max_nr_gframes;

	max_nr_gframes = gnttab_max_grant_frames();
	if (max_nr_gframes < nr_grant_frames)
		return -ENOSYS;

	if (xen_feature(XENFEAT_auto_translated_physmap) && gnttab_shared.addr == NULL) {
		gnttab_shared.addr = xen_auto_xlat_grant_frames.vaddr;
		if (gnttab_shared.addr == NULL) {
			pr_warn("gnttab share frames (addr=0x%08lx) is not mapped!\n",
				(unsigned long)xen_auto_xlat_grant_frames.vaddr);
			return -ENOMEM;
		}
	}
	return gnttab_map(0, nr_grant_frames - 1);
}

int gnttab_resume(void)
{
	gnttab_request_version();
	return gnttab_setup();
}

int gnttab_suspend(void)
{
	if (!xen_feature(XENFEAT_auto_translated_physmap))
		gnttab_interface->unmap_frames();
	return 0;
}

static int gnttab_expand(unsigned int req_entries)
{
	int rc;
	unsigned int cur, extra;

	BUG_ON(grefs_per_grant_frame == 0);
	cur = nr_grant_frames;
	extra = ((req_entries + (grefs_per_grant_frame-1)) /
		 grefs_per_grant_frame);
	if (cur + extra > gnttab_max_grant_frames())
		return -ENOSPC;

	rc = gnttab_map(cur, cur + extra - 1);
	if (rc == 0)
		rc = grow_gnttab_list(extra);

	return rc;
}

int gnttab_init(void)
{
	int i;
	unsigned long max_nr_grant_frames;
	unsigned int max_nr_glist_frames, nr_glist_frames;
	unsigned int nr_init_grefs;
	int ret;

	gnttab_request_version();
	max_nr_grant_frames = gnttab_max_grant_frames();
	nr_grant_frames = 1;

	/* Determine the maximum number of frames required for the
	 * grant reference free list on the current hypervisor.
	 */
	BUG_ON(grefs_per_grant_frame == 0);
	max_nr_glist_frames = (max_nr_grant_frames *
			       grefs_per_grant_frame / RPP);

	gnttab_list = kmalloc(max_nr_glist_frames * sizeof(grant_ref_t *),
			      GFP_KERNEL);
	if (gnttab_list == NULL)
		return -ENOMEM;

	nr_glist_frames = (nr_grant_frames * grefs_per_grant_frame + RPP - 1) / RPP;
	for (i = 0; i < nr_glist_frames; i++) {
		gnttab_list[i] = (grant_ref_t *)__get_free_page(GFP_KERNEL);
		if (gnttab_list[i] == NULL) {
			ret = -ENOMEM;
			goto ini_nomem;
		}
	}

	ret = arch_gnttab_init(max_nr_grant_frames);
	if (ret < 0)
		goto ini_nomem;

	if (gnttab_setup() < 0) {
		ret = -ENODEV;
		goto ini_nomem;
	}

	nr_init_grefs = nr_grant_frames * grefs_per_grant_frame;

	for (i = NR_RESERVED_ENTRIES; i < nr_init_grefs - 1; i++)
		gnttab_entry(i) = i + 1;

	gnttab_entry(nr_init_grefs - 1) = GNTTAB_LIST_END;
	gnttab_free_count = nr_init_grefs - NR_RESERVED_ENTRIES;
	gnttab_free_head  = NR_RESERVED_ENTRIES;

	printk("Grant table initialized\n");
	return 0;

 ini_nomem:
	for (i--; i >= 0; i--)
		free_page((unsigned long)gnttab_list[i]);
	kfree(gnttab_list);
	return ret;
}
EXPORT_SYMBOL_GPL(gnttab_init);

static int __gnttab_init(void)
{
	/* Delay grant-table initialization in the PV on HVM case */
	if (xen_hvm_domain())
		return 0;

	if (!xen_pv_domain())
		return -ENODEV;

	return gnttab_init();
}
/* Starts after core_initcall so that xen_pvh_gnttab_setup can be called
 * beforehand to initialize xen_auto_xlat_grant_frames. */
core_initcall_sync(__gnttab_init);
