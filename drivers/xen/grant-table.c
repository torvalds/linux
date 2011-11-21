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
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>
#include <linux/io.h>

#include <xen/xen.h>
#include <xen/interface/xen.h>
#include <xen/page.h>
#include <xen/grant_table.h>
#include <xen/interface/memory.h>
#include <asm/xen/hypercall.h>

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
unsigned long xen_hvm_resume_frames;
EXPORT_SYMBOL_GPL(xen_hvm_resume_frames);

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

	return nflags & (GTF_reading|GTF_writing);
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
	unsigned int nr_glist_frames, new_nr_glist_frames;

	new_nr_grant_frames = nr_grant_frames + more_frames;
	extra_entries       = more_frames * GREFS_PER_GRANT_FRAME;

	nr_glist_frames = (nr_grant_frames * GREFS_PER_GRANT_FRAME + RPP - 1) / RPP;
	new_nr_glist_frames =
		(new_nr_grant_frames * GREFS_PER_GRANT_FRAME + RPP - 1) / RPP;
	for (i = nr_glist_frames; i < new_nr_glist_frames; i++) {
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
	for ( ; i >= nr_glist_frames; i--)
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

	if (xen_max > boot_max_nr_grant_frames)
		return boot_max_nr_grant_frames;
	return xen_max;
}
EXPORT_SYMBOL_GPL(gnttab_max_grant_frames);

int gnttab_map_refs(struct gnttab_map_grant_ref *map_ops,
			struct gnttab_map_grant_ref *kmap_ops,
			struct page **pages, unsigned int count)
{
	int i, ret;
	pte_t *pte;
	unsigned long mfn;

	ret = HYPERVISOR_grant_table_op(GNTTABOP_map_grant_ref, map_ops, count);
	if (ret)
		return ret;

	if (xen_feature(XENFEAT_auto_translated_physmap))
		return ret;

	for (i = 0; i < count; i++) {
		/* Do not add to override if the map failed. */
		if (map_ops[i].status)
			continue;

		if (map_ops[i].flags & GNTMAP_contains_pte) {
			pte = (pte_t *) (mfn_to_virt(PFN_DOWN(map_ops[i].host_addr)) +
				(map_ops[i].host_addr & ~PAGE_MASK));
			mfn = pte_mfn(*pte);
		} else {
			/* If you really wanted to do this:
			 * mfn = PFN_DOWN(map_ops[i].dev_bus_addr);
			 *
			 * The reason we do not implement it is b/c on the
			 * unmap path (gnttab_unmap_refs) we have no means of
			 * checking whether the page is !GNTMAP_contains_pte.
			 *
			 * That is without some extra data-structure to carry
			 * the struct page, bool clear_pte, and list_head next
			 * tuples and deal with allocation/delallocation, etc.
			 *
			 * The users of this API set the GNTMAP_contains_pte
			 * flag so lets just return not supported until it
			 * becomes neccessary to implement.
			 */
			return -EOPNOTSUPP;
		}
		ret = m2p_add_override(mfn, pages[i], &kmap_ops[i]);
		if (ret)
			return ret;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(gnttab_map_refs);

int gnttab_unmap_refs(struct gnttab_unmap_grant_ref *unmap_ops,
		struct page **pages, unsigned int count)
{
	int i, ret;

	ret = HYPERVISOR_grant_table_op(GNTTABOP_unmap_grant_ref, unmap_ops, count);
	if (ret)
		return ret;

	if (xen_feature(XENFEAT_auto_translated_physmap))
		return ret;

	for (i = 0; i < count; i++) {
		ret = m2p_remove_override(pages[i], true /* clear the PTE */);
		if (ret)
			return ret;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(gnttab_unmap_refs);

static int gnttab_map(unsigned int start_idx, unsigned int end_idx)
{
	struct gnttab_setup_table setup;
	unsigned long *frames;
	unsigned int nr_gframes = end_idx + 1;
	int rc;

	if (xen_hvm_domain()) {
		struct xen_add_to_physmap xatp;
		unsigned int i = end_idx;
		rc = 0;
		/*
		 * Loop backwards, so that the first hypercall has the largest
		 * index, ensuring that the table will grow only once.
		 */
		do {
			xatp.domid = DOMID_SELF;
			xatp.idx = i;
			xatp.space = XENMAPSPACE_grant_table;
			xatp.gpfn = (xen_hvm_resume_frames >> PAGE_SHIFT) + i;
			rc = HYPERVISOR_memory_op(XENMEM_add_to_physmap, &xatp);
			if (rc != 0) {
				printk(KERN_WARNING
						"grant table add_to_physmap failed, err=%d\n", rc);
				break;
			}
		} while (i-- > start_idx);

		return rc;
	}

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

	rc = arch_gnttab_map_shared(frames, nr_gframes, gnttab_max_grant_frames(),
				    &shared);
	BUG_ON(rc);

	kfree(frames);

	return 0;
}

int gnttab_resume(void)
{
	unsigned int max_nr_gframes;

	max_nr_gframes = gnttab_max_grant_frames();
	if (max_nr_gframes < nr_grant_frames)
		return -ENOSYS;

	if (xen_pv_domain())
		return gnttab_map(0, nr_grant_frames - 1);

	if (!shared) {
		shared = ioremap(xen_hvm_resume_frames, PAGE_SIZE * max_nr_gframes);
		if (shared == NULL) {
			printk(KERN_WARNING
					"Failed to ioremap gnttab share frames!");
			return -ENOMEM;
		}
	}

	gnttab_map(0, nr_grant_frames - 1);

	return 0;
}

int gnttab_suspend(void)
{
	arch_gnttab_unmap_shared(shared, nr_grant_frames);
	return 0;
}

static int gnttab_expand(unsigned int req_entries)
{
	int rc;
	unsigned int cur, extra;

	cur = nr_grant_frames;
	extra = ((req_entries + (GREFS_PER_GRANT_FRAME-1)) /
		 GREFS_PER_GRANT_FRAME);
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
	unsigned int max_nr_glist_frames, nr_glist_frames;
	unsigned int nr_init_grefs;

	nr_grant_frames = 1;
	boot_max_nr_grant_frames = __max_nr_grant_frames();

	/* Determine the maximum number of frames required for the
	 * grant reference free list on the current hypervisor.
	 */
	max_nr_glist_frames = (boot_max_nr_grant_frames *
			       GREFS_PER_GRANT_FRAME / RPP);

	gnttab_list = kmalloc(max_nr_glist_frames * sizeof(grant_ref_t *),
			      GFP_KERNEL);
	if (gnttab_list == NULL)
		return -ENOMEM;

	nr_glist_frames = (nr_grant_frames * GREFS_PER_GRANT_FRAME + RPP - 1) / RPP;
	for (i = 0; i < nr_glist_frames; i++) {
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
EXPORT_SYMBOL_GPL(gnttab_init);

static int __devinit __gnttab_init(void)
{
	/* Delay grant-table initialization in the PV on HVM case */
	if (xen_hvm_domain())
		return 0;

	if (!xen_pv_domain())
		return -ENODEV;

	return gnttab_init();
}

core_initcall(__gnttab_init);
