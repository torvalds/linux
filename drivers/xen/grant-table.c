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

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>

#include <xen/interface/xen.h>
#include <xen/page.h>
#include <xen/grant_table.h>

#include <asm/pgtable.h>
#include <asm/sync_bitops.h>


/* External tools reserve first few grant table entries. */
#define NR_RESERVED_ENTRIES 8
#define GNTTAB_LIST_END 0xffffffff
#define GREFS_PER_GRANT_FRAME (PAGE_SIZE / sizeof(struct grant_entry))

static grant_ref_t **gnttab_list;
static unsigned int nr_grant_frames;
static unsigned int boot_max_nr_grant_frames;
static int gnttab_free_count;
static grant_ref_t gnttab_free_head;
static DEFINE_SPINLOCK(gnttab_list_lock);

static struct grant_entry *shared;

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
	int ref, rc;
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

static void update_grant_entry(grant_ref_t ref, domid_t domid,
			       unsigned long frame, unsigned flags)
{
	/*
	 * Introducing a valid entry into the grant table:
	 *  1. Write ent->domid.
	 *  2. Write ent->frame:
	 *      GTF_permit_access:   Frame to which access is permitted.
	 *      GTF_accept_transfer: Pseudo-phys frame slot being filled by new
	 *                           frame, or zero if none.
	 *  3. Write memory barrier (WMB).
	 *  4. Write ent->flags, inc. valid type.
	 */
	shared[ref].frame = frame;
	shared[ref].domid = domid;
	wmb();
	shared[ref].flags = flags;
}

/*
 * Public grant-issuing interface functions
 */
void gnttab_grant_foreign_access_ref(grant_ref_t ref, domid_t domid,
				     unsigned long frame, int readonly)
{
	update_grant_entry(ref, domid, frame,
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

int gnttab_query_foreign_access(grant_ref_t ref)
{
	u16 nflags;

	nflags = shared[ref].flags;

	return (nflags & (GTF_reading|GTF_writing));
}
EXPORT_SYMBOL_GPL(gnttab_query_foreign_access);

int gnttab_end_foreign_access_ref(grant_ref_t ref, int readonly)
{
	u16 flags, nflags;

	nflags = shared[ref].flags;
	do {
		flags = nflags;
		if (flags & (GTF_reading|GTF_writing)) {
			printk(KERN_ALERT "WARNING: g.e. still in use!\n");
			return 0;
		}
	} while ((nflags = sync_cmpxchg(&shared[ref].flags, flags, 0)) != flags);

	return 1;
}
EXPORT_SYMBOL_GPL(gnttab_end_foreign_access_ref);

void gnttab_end_foreign_access(grant_ref_t ref, int readonly,
			       unsigned long page)
{
	if (gnttab_end_foreign_access_ref(ref, readonly)) {
		put_free_entry(ref);
		if (page != 0)
			free_page(page);
	} else {
		/* XXX This needs to be fixed so that the ref and page are
		   placed on a list to be freed up later. */
		printk(KERN_WARNING
		       "WARNING: leaking g.e. and page still in use!\n");
	}
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
	update_grant_entry(ref, domid, pfn, GTF_accept_transfer);
}
EXPORT_SYMBOL_GPL(gnttab_grant_foreign_transfer_ref);

unsigned long gnttab_end_foreign_transfer_ref(grant_ref_t ref)
{
	unsigned long frame;
	u16           flags;

	/*
	 * If a transfer is not even yet started, try to reclaim the grant
	 * reference and return failure (== 0).
	 */
	while (!((flags = shared[ref].flags) & GTF_transfer_committed)) {
		if (sync_cmpxchg(&shared[ref].flags, flags, 0) == flags)
			return 0;
		cpu_relax();
	}

	/* If a transfer is in progress then wait until it is completed. */
	while (!(flags & GTF_transfer_completed)) {
		flags = shared[ref].flags;
		cpu_relax();
	}

	rmb();	/* Read the frame number /after/ reading completion status. */
	frame = shared[ref].frame;
	BUG_ON(frame == 0);

	return frame;
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
	spin_lock_irqsave(&gnttab_list_lock, flags);
	if (callback->next)
		goto out;
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

	new_nr_grant_frames = nr_grant_frames + more_frames;
	extra_entries       = more_frames * GREFS_PER_GRANT_FRAME;

	for (i = nr_grant_frames; i < new_nr_grant_frames; i++) {
		gnttab_list[i] = (grant_ref_t *)__get_free_page(GFP_ATOMIC);
		if (!gnttab_list[i])
			goto grow_nomem;
	}


	for (i = GREFS_PER_GRANT_FRAME * nr_grant_frames;
	     i < GREFS_PER_GRANT_FRAME * new_nr_grant_frames - 1; i++)
		gnttab_entry(i) = i + 1;

	gnttab_entry(i) = gnttab_free_head;
	gnttab_free_head = GREFS_PER_GRANT_FRAME * nr_grant_frames;
	gnttab_free_count += extra_entries;

	nr_grant_frames = new_nr_grant_frames;

	check_free_callbacks();

	return 0;

grow_nomem:
	for ( ; i >= nr_grant_frames; i--)
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

static inline unsigned int max_nr_grant_frames(void)
{
	unsigned int xen_max = __max_nr_grant_frames();

	if (xen_max > boot_max_nr_grant_frames)
		return boot_max_nr_grant_frames;
	return xen_max;
}

static int map_pte_fn(pte_t *pte, struct page *pmd_page,
		      unsigned long addr, void *data)
{
	unsigned long **frames = (unsigned long **)data;

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

static int gnttab_map(unsigned int start_idx, unsigned int end_idx)
{
	struct gnttab_setup_table setup;
	unsigned long *frames;
	unsigned int nr_gframes = end_idx + 1;
	int rc;

	frames = kmalloc(nr_gframes * sizeof(unsigned long), GFP_ATOMIC);
	if (!frames)
		return -ENOMEM;

	setup.dom        = DOMID_SELF;
	setup.nr_frames  = nr_gframes;
	setup.frame_list = frames;

	rc = HYPERVISOR_grant_table_op(GNTTABOP_setup_table, &setup, 1);
	if (rc == -ENOSYS) {
		kfree(frames);
		return -ENOSYS;
	}

	BUG_ON(rc || setup.status);

	if (shared == NULL) {
		struct vm_struct *area;
		area = alloc_vm_area(PAGE_SIZE * max_nr_grant_frames());
		BUG_ON(area == NULL);
		shared = area->addr;
	}
	rc = apply_to_page_range(&init_mm, (unsigned long)shared,
				 PAGE_SIZE * nr_gframes,
				 map_pte_fn, &frames);
	BUG_ON(rc);
	frames -= nr_gframes; /* adjust after map_pte_fn() */

	kfree(frames);

	return 0;
}

static int gnttab_resume(void)
{
	if (max_nr_grant_frames() < nr_grant_frames)
		return -ENOSYS;
	return gnttab_map(0, nr_grant_frames - 1);
}

static int gnttab_suspend(void)
{
	apply_to_page_range(&init_mm, (unsigned long)shared,
			    PAGE_SIZE * nr_grant_frames,
			    unmap_pte_fn, NULL);

	return 0;
}

static int gnttab_expand(unsigned int req_entries)
{
	int rc;
	unsigned int cur, extra;

	cur = nr_grant_frames;
	extra = ((req_entries + (GREFS_PER_GRANT_FRAME-1)) /
		 GREFS_PER_GRANT_FRAME);
	if (cur + extra > max_nr_grant_frames())
		return -ENOSPC;

	rc = gnttab_map(cur, cur + extra - 1);
	if (rc == 0)
		rc = grow_gnttab_list(extra);

	return rc;
}

static int __devinit gnttab_init(void)
{
	int i;
	unsigned int max_nr_glist_frames;
	unsigned int nr_init_grefs;

	if (!is_running_on_xen())
		return -ENODEV;

	nr_grant_frames = 1;
	boot_max_nr_grant_frames = __max_nr_grant_frames();

	/* Determine the maximum number of frames required for the
	 * grant reference free list on the current hypervisor.
	 */
	max_nr_glist_frames = (boot_max_nr_grant_frames *
			       GREFS_PER_GRANT_FRAME /
			       (PAGE_SIZE / sizeof(grant_ref_t)));

	gnttab_list = kmalloc(max_nr_glist_frames * sizeof(grant_ref_t *),
			      GFP_KERNEL);
	if (gnttab_list == NULL)
		return -ENOMEM;

	for (i = 0; i < nr_grant_frames; i++) {
		gnttab_list[i] = (grant_ref_t *)__get_free_page(GFP_KERNEL);
		if (gnttab_list[i] == NULL)
			goto ini_nomem;
	}

	if (gnttab_resume() < 0)
		return -ENODEV;

	nr_init_grefs = nr_grant_frames * GREFS_PER_GRANT_FRAME;

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
	return -ENOMEM;
}

core_initcall(gnttab_init);
