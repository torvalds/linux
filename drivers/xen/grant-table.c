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
#include <linux/delay.h>
#include <linux/hardirq.h>

#include <xen/xen.h>
#include <xen/interface/xen.h>
#include <xen/page.h>
#include <xen/grant_table.h>
#include <xen/interface/memory.h>
#include <xen/hvc-console.h>
#include <asm/xen/hypercall.h>
#include <asm/xen/interface.h>

#include <asm/pgtable.h>
#include <asm/sync_bitops.h>

/* External tools reserve first few grant table entries. */
#define NR_RESERVED_ENTRIES 8
#define GNTTAB_LIST_END 0xffffffff
#define GREFS_PER_GRANT_FRAME \
(grant_table_version == 1 ?                      \
(PAGE_SIZE / sizeof(struct grant_entry_v1)) :   \
(PAGE_SIZE / sizeof(union grant_entry_v2)))

static grant_ref_t **gnttab_list;
static unsigned int nr_grant_frames;
static unsigned int boot_max_nr_grant_frames;
static int gnttab_free_count;
static grant_ref_t gnttab_free_head;
static DEFINE_SPINLOCK(gnttab_list_lock);
unsigned long xen_hvm_resume_frames;
EXPORT_SYMBOL_GPL(xen_hvm_resume_frames);

static union {
	struct grant_entry_v1 *v1;
	union grant_entry_v2 *v2;
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
	int (*map_frames)(unsigned long *frames, unsigned int nr_gframes);
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
	/*
	 * Grant a domain to access a range of bytes within the page referred by
	 * an available grant entry. Ref parameter is reference of a grant entry
	 * which will be sub-page accessed, domid is id of grantee domain, frame
	 * is frame address of subpage grant, flags is grant type and flag
	 * information, page_off is offset of the range of bytes, and length is
	 * length of bytes to be accessed.
	 */
	void (*update_subpage_entry)(grant_ref_t ref, domid_t domid,
				     unsigned long frame, int flags,
				     unsigned page_off, unsigned length);
	/*
	 * Redirect an available grant entry on domain A to another grant
	 * reference of domain B, then allow domain C to use grant reference
	 * of domain B transitively. Ref parameter is an available grant entry
	 * reference on domain A, domid is id of domain C which accesses grant
	 * entry transitively, flags is grant type and flag information,
	 * trans_domid is id of domain B whose grant entry is finally accessed
	 * transitively, trans_gref is grant entry transitive reference of
	 * domain B.
	 */
	void (*update_trans_entry)(grant_ref_t ref, domid_t domid, int flags,
				   domid_t trans_domid, grant_ref_t trans_gref);
};

static struct gnttab_ops *gnttab_interface;

/*This reflects status of grant entries, so act as a global value*/
static grant_status_t *grstatus;

static int grant_table_version;

static struct gnttab_free_callback *gnttab_free_callback_list;

static int gnttab_expand(unsigned int req_entries);

#define RPP (PAGE_SIZE / sizeof(grant_ref_t))
#define SPP (PAGE_SIZE / sizeof(grant_status_t))

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
 * Following applies to gnttab_update_entry_v1 and gnttab_update_entry_v2.
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

static void gnttab_update_entry_v2(grant_ref_t ref, domid_t domid,
				   unsigned long frame, unsigned flags)
{
	gnttab_shared.v2[ref].hdr.domid = domid;
	gnttab_shared.v2[ref].full_page.frame = frame;
	wmb();
	gnttab_shared.v2[ref].hdr.flags = GTF_permit_access | flags;
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

static void gnttab_update_subpage_entry_v2(grant_ref_t ref, domid_t domid,
					   unsigned long frame, int flags,
					   unsigned page_off, unsigned length)
{
	gnttab_shared.v2[ref].sub_page.frame = frame;
	gnttab_shared.v2[ref].sub_page.page_off = page_off;
	gnttab_shared.v2[ref].sub_page.length = length;
	gnttab_shared.v2[ref].hdr.domid = domid;
	wmb();
	gnttab_shared.v2[ref].hdr.flags =
				GTF_permit_access | GTF_sub_page | flags;
}

int gnttab_grant_foreign_access_subpage_ref(grant_ref_t ref, domid_t domid,
					    unsigned long frame, int flags,
					    unsigned page_off,
					    unsigned length)
{
	if (flags & (GTF_accept_transfer | GTF_reading |
		     GTF_writing | GTF_transitive))
		return -EPERM;

	if (gnttab_interface->update_subpage_entry == NULL)
		return -ENOSYS;

	gnttab_interface->update_subpage_entry(ref, domid, frame, flags,
					       page_off, length);

	return 0;
}
EXPORT_SYMBOL_GPL(gnttab_grant_foreign_access_subpage_ref);

int gnttab_grant_foreign_access_subpage(domid_t domid, unsigned long frame,
					int flags, unsigned page_off,
					unsigned length)
{
	int ref, rc;

	ref = get_free_entries(1);
	if (unlikely(ref < 0))
		return -ENOSPC;

	rc = gnttab_grant_foreign_access_subpage_ref(ref, domid, frame, flags,
						     page_off, length);
	if (rc < 0) {
		put_free_entry(ref);
		return rc;
	}

	return ref;
}
EXPORT_SYMBOL_GPL(gnttab_grant_foreign_access_subpage);

bool gnttab_subpage_grants_available(void)
{
	return gnttab_interface->update_subpage_entry != NULL;
}
EXPORT_SYMBOL_GPL(gnttab_subpage_grants_available);

static void gnttab_update_trans_entry_v2(grant_ref_t ref, domid_t domid,
					 int flags, domid_t trans_domid,
					 grant_ref_t trans_gref)
{
	gnttab_shared.v2[ref].transitive.trans_domid = trans_domid;
	gnttab_shared.v2[ref].transitive.gref = trans_gref;
	gnttab_shared.v2[ref].hdr.domid = domid;
	wmb();
	gnttab_shared.v2[ref].hdr.flags =
				GTF_permit_access | GTF_transitive | flags;
}

int gnttab_grant_foreign_access_trans_ref(grant_ref_t ref, domid_t domid,
					  int flags, domid_t trans_domid,
					  grant_ref_t trans_gref)
{
	if (flags & (GTF_accept_transfer | GTF_reading |
		     GTF_writing | GTF_sub_page))
		return -EPERM;

	if (gnttab_interface->update_trans_entry == NULL)
		return -ENOSYS;

	gnttab_interface->update_trans_entry(ref, domid, flags, trans_domid,
					     trans_gref);

	return 0;
}
EXPORT_SYMBOL_GPL(gnttab_grant_foreign_access_trans_ref);

int gnttab_grant_foreign_access_trans(domid_t domid, int flags,
				      domid_t trans_domid,
				      grant_ref_t trans_gref)
{
	int ref, rc;

	ref = get_free_entries(1);
	if (unlikely(ref < 0))
		return -ENOSPC;

	rc = gnttab_grant_foreign_access_trans_ref(ref, domid, flags,
						   trans_domid, trans_gref);
	if (rc < 0) {
		put_free_entry(ref);
		return rc;
	}

	return ref;
}
EXPORT_SYMBOL_GPL(gnttab_grant_foreign_access_trans);

bool gnttab_trans_grants_available(void)
{
	return gnttab_interface->update_trans_entry != NULL;
}
EXPORT_SYMBOL_GPL(gnttab_trans_grants_available);

static int gnttab_query_foreign_access_v1(grant_ref_t ref)
{
	return gnttab_shared.v1[ref].flags & (GTF_reading|GTF_writing);
}

static int gnttab_query_foreign_access_v2(grant_ref_t ref)
{
	return grstatus[ref] & (GTF_reading|GTF_writing);
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

static int gnttab_end_foreign_access_ref_v2(grant_ref_t ref, int readonly)
{
	gnttab_shared.v2[ref].hdr.flags = 0;
	mb();
	if (grstatus[ref] & (GTF_reading|GTF_writing)) {
		return 0;
	} else {
		/* The read of grstatus needs to have acquire
		semantics.  On x86, reads already have
		that, and we just need to protect against
		compiler reorderings.  On other
		architectures we may need a full
		barrier. */
#ifdef CONFIG_X86
		barrier();
#else
		mb();
#endif
	}

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
				pr_info("g.e. %#x still pending\n",
					entry->ref);
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

static unsigned long gnttab_end_foreign_transfer_ref_v2(grant_ref_t ref)
{
	unsigned long frame;
	u16           flags;
	u16          *pflags;

	pflags = &gnttab_shared.v2[ref].hdr.flags;

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

	rmb();  /* Read the frame number /after/ reading completion status. */
	frame = gnttab_shared.v2[ref].full_page.frame;
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
		printk(KERN_ERR "%s: %s eagain grant\n", func, current->comm);
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

int gnttab_map_refs(struct gnttab_map_grant_ref *map_ops,
		    struct gnttab_map_grant_ref *kmap_ops,
		    struct page **pages, unsigned int count)
{
	int i, ret;
	bool lazy = false;
	pte_t *pte;
	unsigned long mfn;

	ret = HYPERVISOR_grant_table_op(GNTTABOP_map_grant_ref, map_ops, count);
	if (ret)
		return ret;

	/* Retry eagain maps */
	for (i = 0; i < count; i++)
		if (map_ops[i].status == GNTST_eagain)
			gnttab_retry_eagain_gop(GNTTABOP_map_grant_ref, map_ops + i,
						&map_ops[i].status, __func__);

	if (xen_feature(XENFEAT_auto_translated_physmap))
		return ret;

	if (!in_interrupt() && paravirt_get_lazy_mode() == PARAVIRT_LAZY_NONE) {
		arch_enter_lazy_mmu_mode();
		lazy = true;
	}

	for (i = 0; i < count; i++) {
		/* Do not add to override if the map failed. */
		if (map_ops[i].status)
			continue;

		if (map_ops[i].flags & GNTMAP_contains_pte) {
			pte = (pte_t *) (mfn_to_virt(PFN_DOWN(map_ops[i].host_addr)) +
				(map_ops[i].host_addr & ~PAGE_MASK));
			mfn = pte_mfn(*pte);
		} else {
			mfn = PFN_DOWN(map_ops[i].dev_bus_addr);
		}
		ret = m2p_add_override(mfn, pages[i], kmap_ops ?
				       &kmap_ops[i] : NULL);
		if (ret)
			return ret;
	}

	if (lazy)
		arch_leave_lazy_mmu_mode();

	return ret;
}
EXPORT_SYMBOL_GPL(gnttab_map_refs);

int gnttab_unmap_refs(struct gnttab_unmap_grant_ref *unmap_ops,
		      struct gnttab_map_grant_ref *kmap_ops,
		      struct page **pages, unsigned int count)
{
	int i, ret;
	bool lazy = false;

	ret = HYPERVISOR_grant_table_op(GNTTABOP_unmap_grant_ref, unmap_ops, count);
	if (ret)
		return ret;

	if (xen_feature(XENFEAT_auto_translated_physmap))
		return ret;

	if (!in_interrupt() && paravirt_get_lazy_mode() == PARAVIRT_LAZY_NONE) {
		arch_enter_lazy_mmu_mode();
		lazy = true;
	}

	for (i = 0; i < count; i++) {
		ret = m2p_remove_override(pages[i], kmap_ops ?
				       &kmap_ops[i] : NULL);
		if (ret)
			return ret;
	}

	if (lazy)
		arch_leave_lazy_mmu_mode();

	return ret;
}
EXPORT_SYMBOL_GPL(gnttab_unmap_refs);

static unsigned nr_status_frames(unsigned nr_grant_frames)
{
	return (nr_grant_frames * GREFS_PER_GRANT_FRAME + SPP - 1) / SPP;
}

static int gnttab_map_frames_v1(unsigned long *frames, unsigned int nr_gframes)
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

static int gnttab_map_frames_v2(unsigned long *frames, unsigned int nr_gframes)
{
	uint64_t *sframes;
	unsigned int nr_sframes;
	struct gnttab_get_status_frames getframes;
	int rc;

	nr_sframes = nr_status_frames(nr_gframes);

	/* No need for kzalloc as it is initialized in following hypercall
	 * GNTTABOP_get_status_frames.
	 */
	sframes = kmalloc(nr_sframes  * sizeof(uint64_t), GFP_ATOMIC);
	if (!sframes)
		return -ENOMEM;

	getframes.dom        = DOMID_SELF;
	getframes.nr_frames  = nr_sframes;
	set_xen_guest_handle(getframes.frame_list, sframes);

	rc = HYPERVISOR_grant_table_op(GNTTABOP_get_status_frames,
				       &getframes, 1);
	if (rc == -ENOSYS) {
		kfree(sframes);
		return -ENOSYS;
	}

	BUG_ON(rc || getframes.status);

	rc = arch_gnttab_map_status(sframes, nr_sframes,
				    nr_status_frames(gnttab_max_grant_frames()),
				    &grstatus);
	BUG_ON(rc);
	kfree(sframes);

	rc = arch_gnttab_map_shared(frames, nr_gframes,
				    gnttab_max_grant_frames(),
				    &gnttab_shared.addr);
	BUG_ON(rc);

	return 0;
}

static void gnttab_unmap_frames_v2(void)
{
	arch_gnttab_unmap(gnttab_shared.addr, nr_grant_frames);
	arch_gnttab_unmap(grstatus, nr_status_frames(nr_grant_frames));
}

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

static struct gnttab_ops gnttab_v2_ops = {
	.map_frames			= gnttab_map_frames_v2,
	.unmap_frames			= gnttab_unmap_frames_v2,
	.update_entry			= gnttab_update_entry_v2,
	.end_foreign_access_ref		= gnttab_end_foreign_access_ref_v2,
	.end_foreign_transfer_ref	= gnttab_end_foreign_transfer_ref_v2,
	.query_foreign_access		= gnttab_query_foreign_access_v2,
	.update_subpage_entry		= gnttab_update_subpage_entry_v2,
	.update_trans_entry		= gnttab_update_trans_entry_v2,
};

static void gnttab_request_version(void)
{
	int rc;
	struct gnttab_set_version gsv;

	if (xen_hvm_domain())
		gsv.version = 1;
	else
		gsv.version = 2;
	rc = HYPERVISOR_grant_table_op(GNTTABOP_set_version, &gsv, 1);
	if (rc == 0 && gsv.version == 2) {
		grant_table_version = 2;
		gnttab_interface = &gnttab_v2_ops;
	} else if (grant_table_version == 2) {
		/*
		 * If we've already used version 2 features,
		 * but then suddenly discover that they're not
		 * available (e.g. migrating to an older
		 * version of Xen), almost unbounded badness
		 * can happen.
		 */
		panic("we need grant tables version 2, but only version 1 is available");
	} else {
		grant_table_version = 1;
		gnttab_interface = &gnttab_v1_ops;
	}
	printk(KERN_INFO "Grant tables using version %d layout.\n",
		grant_table_version);
}

int gnttab_resume(void)
{
	unsigned int max_nr_gframes;

	gnttab_request_version();
	max_nr_gframes = gnttab_max_grant_frames();
	if (max_nr_gframes < nr_grant_frames)
		return -ENOSYS;

	if (xen_pv_domain())
		return gnttab_map(0, nr_grant_frames - 1);

	if (gnttab_shared.addr == NULL) {
		gnttab_shared.addr = ioremap(xen_hvm_resume_frames,
						PAGE_SIZE * max_nr_gframes);
		if (gnttab_shared.addr == NULL) {
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
	gnttab_interface->unmap_frames();
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
	int ret;

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
		if (gnttab_list[i] == NULL) {
			ret = -ENOMEM;
			goto ini_nomem;
		}
	}

	if (gnttab_resume() < 0) {
		ret = -ENODEV;
		goto ini_nomem;
	}

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
	return ret;
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
