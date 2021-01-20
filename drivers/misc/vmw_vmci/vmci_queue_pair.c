// SPDX-License-Identifier: GPL-2.0-only
/*
 * VMware VMCI Driver
 *
 * Copyright (C) 2012 VMware, Inc. All rights reserved.
 */

#include <linux/vmw_vmci_defs.h>
#include <linux/vmw_vmci_api.h>
#include <linux/highmem.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pagemap.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/uio.h>
#include <linux/wait.h>
#include <linux/vmalloc.h>
#include <linux/skbuff.h>

#include "vmci_handle_array.h"
#include "vmci_queue_pair.h"
#include "vmci_datagram.h"
#include "vmci_resource.h"
#include "vmci_context.h"
#include "vmci_driver.h"
#include "vmci_event.h"
#include "vmci_route.h"

/*
 * In the following, we will distinguish between two kinds of VMX processes -
 * the ones with versions lower than VMCI_VERSION_NOVMVM that use specialized
 * VMCI page files in the VMX and supporting VM to VM communication and the
 * newer ones that use the guest memory directly. We will in the following
 * refer to the older VMX versions as old-style VMX'en, and the newer ones as
 * new-style VMX'en.
 *
 * The state transition datagram is as follows (the VMCIQPB_ prefix has been
 * removed for readability) - see below for more details on the transtions:
 *
 *            --------------  NEW  -------------
 *            |                                |
 *           \_/                              \_/
 *     CREATED_NO_MEM <-----------------> CREATED_MEM
 *            |    |                           |
 *            |    o-----------------------o   |
 *            |                            |   |
 *           \_/                          \_/ \_/
 *     ATTACHED_NO_MEM <----------------> ATTACHED_MEM
 *            |                            |   |
 *            |     o----------------------o   |
 *            |     |                          |
 *           \_/   \_/                        \_/
 *     SHUTDOWN_NO_MEM <----------------> SHUTDOWN_MEM
 *            |                                |
 *            |                                |
 *            -------------> gone <-------------
 *
 * In more detail. When a VMCI queue pair is first created, it will be in the
 * VMCIQPB_NEW state. It will then move into one of the following states:
 *
 * - VMCIQPB_CREATED_NO_MEM: this state indicates that either:
 *
 *     - the created was performed by a host endpoint, in which case there is
 *       no backing memory yet.
 *
 *     - the create was initiated by an old-style VMX, that uses
 *       vmci_qp_broker_set_page_store to specify the UVAs of the queue pair at
 *       a later point in time. This state can be distinguished from the one
 *       above by the context ID of the creator. A host side is not allowed to
 *       attach until the page store has been set.
 *
 * - VMCIQPB_CREATED_MEM: this state is the result when the queue pair
 *     is created by a VMX using the queue pair device backend that
 *     sets the UVAs of the queue pair immediately and stores the
 *     information for later attachers. At this point, it is ready for
 *     the host side to attach to it.
 *
 * Once the queue pair is in one of the created states (with the exception of
 * the case mentioned for older VMX'en above), it is possible to attach to the
 * queue pair. Again we have two new states possible:
 *
 * - VMCIQPB_ATTACHED_MEM: this state can be reached through the following
 *   paths:
 *
 *     - from VMCIQPB_CREATED_NO_MEM when a new-style VMX allocates a queue
 *       pair, and attaches to a queue pair previously created by the host side.
 *
 *     - from VMCIQPB_CREATED_MEM when the host side attaches to a queue pair
 *       already created by a guest.
 *
 *     - from VMCIQPB_ATTACHED_NO_MEM, when an old-style VMX calls
 *       vmci_qp_broker_set_page_store (see below).
 *
 * - VMCIQPB_ATTACHED_NO_MEM: If the queue pair already was in the
 *     VMCIQPB_CREATED_NO_MEM due to a host side create, an old-style VMX will
 *     bring the queue pair into this state. Once vmci_qp_broker_set_page_store
 *     is called to register the user memory, the VMCIQPB_ATTACH_MEM state
 *     will be entered.
 *
 * From the attached queue pair, the queue pair can enter the shutdown states
 * when either side of the queue pair detaches. If the guest side detaches
 * first, the queue pair will enter the VMCIQPB_SHUTDOWN_NO_MEM state, where
 * the content of the queue pair will no longer be available. If the host
 * side detaches first, the queue pair will either enter the
 * VMCIQPB_SHUTDOWN_MEM, if the guest memory is currently mapped, or
 * VMCIQPB_SHUTDOWN_NO_MEM, if the guest memory is not mapped
 * (e.g., the host detaches while a guest is stunned).
 *
 * New-style VMX'en will also unmap guest memory, if the guest is
 * quiesced, e.g., during a snapshot operation. In that case, the guest
 * memory will no longer be available, and the queue pair will transition from
 * *_MEM state to a *_NO_MEM state. The VMX may later map the memory once more,
 * in which case the queue pair will transition from the *_NO_MEM state at that
 * point back to the *_MEM state. Note that the *_NO_MEM state may have changed,
 * since the peer may have either attached or detached in the meantime. The
 * values are laid out such that ++ on a state will move from a *_NO_MEM to a
 * *_MEM state, and vice versa.
 */

/* The Kernel specific component of the struct vmci_queue structure. */
struct vmci_queue_kern_if {
	struct mutex __mutex;	/* Protects the queue. */
	struct mutex *mutex;	/* Shared by producer and consumer queues. */
	size_t num_pages;	/* Number of pages incl. header. */
	bool host;		/* Host or guest? */
	union {
		struct {
			dma_addr_t *pas;
			void **vas;
		} g;		/* Used by the guest. */
		struct {
			struct page **page;
			struct page **header_page;
		} h;		/* Used by the host. */
	} u;
};

/*
 * This structure is opaque to the clients.
 */
struct vmci_qp {
	struct vmci_handle handle;
	struct vmci_queue *produce_q;
	struct vmci_queue *consume_q;
	u64 produce_q_size;
	u64 consume_q_size;
	u32 peer;
	u32 flags;
	u32 priv_flags;
	bool guest_endpoint;
	unsigned int blocked;
	unsigned int generation;
	wait_queue_head_t event;
};

enum qp_broker_state {
	VMCIQPB_NEW,
	VMCIQPB_CREATED_NO_MEM,
	VMCIQPB_CREATED_MEM,
	VMCIQPB_ATTACHED_NO_MEM,
	VMCIQPB_ATTACHED_MEM,
	VMCIQPB_SHUTDOWN_NO_MEM,
	VMCIQPB_SHUTDOWN_MEM,
	VMCIQPB_GONE
};

#define QPBROKERSTATE_HAS_MEM(_qpb) (_qpb->state == VMCIQPB_CREATED_MEM || \
				     _qpb->state == VMCIQPB_ATTACHED_MEM || \
				     _qpb->state == VMCIQPB_SHUTDOWN_MEM)

/*
 * In the queue pair broker, we always use the guest point of view for
 * the produce and consume queue values and references, e.g., the
 * produce queue size stored is the guests produce queue size. The
 * host endpoint will need to swap these around. The only exception is
 * the local queue pairs on the host, in which case the host endpoint
 * that creates the queue pair will have the right orientation, and
 * the attaching host endpoint will need to swap.
 */
struct qp_entry {
	struct list_head list_item;
	struct vmci_handle handle;
	u32 peer;
	u32 flags;
	u64 produce_size;
	u64 consume_size;
	u32 ref_count;
};

struct qp_broker_entry {
	struct vmci_resource resource;
	struct qp_entry qp;
	u32 create_id;
	u32 attach_id;
	enum qp_broker_state state;
	bool require_trusted_attach;
	bool created_by_trusted;
	bool vmci_page_files;	/* Created by VMX using VMCI page files */
	struct vmci_queue *produce_q;
	struct vmci_queue *consume_q;
	struct vmci_queue_header saved_produce_q;
	struct vmci_queue_header saved_consume_q;
	vmci_event_release_cb wakeup_cb;
	void *client_data;
	void *local_mem;	/* Kernel memory for local queue pair */
};

struct qp_guest_endpoint {
	struct vmci_resource resource;
	struct qp_entry qp;
	u64 num_ppns;
	void *produce_q;
	void *consume_q;
	struct ppn_set ppn_set;
};

struct qp_list {
	struct list_head head;
	struct mutex mutex;	/* Protect queue list. */
};

static struct qp_list qp_broker_list = {
	.head = LIST_HEAD_INIT(qp_broker_list.head),
	.mutex = __MUTEX_INITIALIZER(qp_broker_list.mutex),
};

static struct qp_list qp_guest_endpoints = {
	.head = LIST_HEAD_INIT(qp_guest_endpoints.head),
	.mutex = __MUTEX_INITIALIZER(qp_guest_endpoints.mutex),
};

#define INVALID_VMCI_GUEST_MEM_ID  0
#define QPE_NUM_PAGES(_QPE) ((u32) \
			     (DIV_ROUND_UP(_QPE.produce_size, PAGE_SIZE) + \
			      DIV_ROUND_UP(_QPE.consume_size, PAGE_SIZE) + 2))


/*
 * Frees kernel VA space for a given queue and its queue header, and
 * frees physical data pages.
 */
static void qp_free_queue(void *q, u64 size)
{
	struct vmci_queue *queue = q;

	if (queue) {
		u64 i;

		/* Given size does not include header, so add in a page here. */
		for (i = 0; i < DIV_ROUND_UP(size, PAGE_SIZE) + 1; i++) {
			dma_free_coherent(&vmci_pdev->dev, PAGE_SIZE,
					  queue->kernel_if->u.g.vas[i],
					  queue->kernel_if->u.g.pas[i]);
		}

		vfree(queue);
	}
}

/*
 * Allocates kernel queue pages of specified size with IOMMU mappings,
 * plus space for the queue structure/kernel interface and the queue
 * header.
 */
static void *qp_alloc_queue(u64 size, u32 flags)
{
	u64 i;
	struct vmci_queue *queue;
	size_t pas_size;
	size_t vas_size;
	size_t queue_size = sizeof(*queue) + sizeof(*queue->kernel_if);
	u64 num_pages;

	if (size > SIZE_MAX - PAGE_SIZE)
		return NULL;
	num_pages = DIV_ROUND_UP(size, PAGE_SIZE) + 1;
	if (num_pages >
		 (SIZE_MAX - queue_size) /
		 (sizeof(*queue->kernel_if->u.g.pas) +
		  sizeof(*queue->kernel_if->u.g.vas)))
		return NULL;

	pas_size = num_pages * sizeof(*queue->kernel_if->u.g.pas);
	vas_size = num_pages * sizeof(*queue->kernel_if->u.g.vas);
	queue_size += pas_size + vas_size;

	queue = vmalloc(queue_size);
	if (!queue)
		return NULL;

	queue->q_header = NULL;
	queue->saved_header = NULL;
	queue->kernel_if = (struct vmci_queue_kern_if *)(queue + 1);
	queue->kernel_if->mutex = NULL;
	queue->kernel_if->num_pages = num_pages;
	queue->kernel_if->u.g.pas = (dma_addr_t *)(queue->kernel_if + 1);
	queue->kernel_if->u.g.vas =
		(void **)((u8 *)queue->kernel_if->u.g.pas + pas_size);
	queue->kernel_if->host = false;

	for (i = 0; i < num_pages; i++) {
		queue->kernel_if->u.g.vas[i] =
			dma_alloc_coherent(&vmci_pdev->dev, PAGE_SIZE,
					   &queue->kernel_if->u.g.pas[i],
					   GFP_KERNEL);
		if (!queue->kernel_if->u.g.vas[i]) {
			/* Size excl. the header. */
			qp_free_queue(queue, i * PAGE_SIZE);
			return NULL;
		}
	}

	/* Queue header is the first page. */
	queue->q_header = queue->kernel_if->u.g.vas[0];

	return queue;
}

/*
 * Copies from a given buffer or iovector to a VMCI Queue.  Uses
 * kmap()/kunmap() to dynamically map/unmap required portions of the queue
 * by traversing the offset -> page translation structure for the queue.
 * Assumes that offset + size does not wrap around in the queue.
 */
static int qp_memcpy_to_queue_iter(struct vmci_queue *queue,
				  u64 queue_offset,
				  struct iov_iter *from,
				  size_t size)
{
	struct vmci_queue_kern_if *kernel_if = queue->kernel_if;
	size_t bytes_copied = 0;

	while (bytes_copied < size) {
		const u64 page_index =
			(queue_offset + bytes_copied) / PAGE_SIZE;
		const size_t page_offset =
		    (queue_offset + bytes_copied) & (PAGE_SIZE - 1);
		void *va;
		size_t to_copy;

		if (kernel_if->host)
			va = kmap(kernel_if->u.h.page[page_index]);
		else
			va = kernel_if->u.g.vas[page_index + 1];
			/* Skip header. */

		if (size - bytes_copied > PAGE_SIZE - page_offset)
			/* Enough payload to fill up from this page. */
			to_copy = PAGE_SIZE - page_offset;
		else
			to_copy = size - bytes_copied;

		if (!copy_from_iter_full((u8 *)va + page_offset, to_copy,
					 from)) {
			if (kernel_if->host)
				kunmap(kernel_if->u.h.page[page_index]);
			return VMCI_ERROR_INVALID_ARGS;
		}
		bytes_copied += to_copy;
		if (kernel_if->host)
			kunmap(kernel_if->u.h.page[page_index]);
	}

	return VMCI_SUCCESS;
}

/*
 * Copies to a given buffer or iovector from a VMCI Queue.  Uses
 * kmap()/kunmap() to dynamically map/unmap required portions of the queue
 * by traversing the offset -> page translation structure for the queue.
 * Assumes that offset + size does not wrap around in the queue.
 */
static int qp_memcpy_from_queue_iter(struct iov_iter *to,
				    const struct vmci_queue *queue,
				    u64 queue_offset, size_t size)
{
	struct vmci_queue_kern_if *kernel_if = queue->kernel_if;
	size_t bytes_copied = 0;

	while (bytes_copied < size) {
		const u64 page_index =
			(queue_offset + bytes_copied) / PAGE_SIZE;
		const size_t page_offset =
		    (queue_offset + bytes_copied) & (PAGE_SIZE - 1);
		void *va;
		size_t to_copy;
		int err;

		if (kernel_if->host)
			va = kmap(kernel_if->u.h.page[page_index]);
		else
			va = kernel_if->u.g.vas[page_index + 1];
			/* Skip header. */

		if (size - bytes_copied > PAGE_SIZE - page_offset)
			/* Enough payload to fill up this page. */
			to_copy = PAGE_SIZE - page_offset;
		else
			to_copy = size - bytes_copied;

		err = copy_to_iter((u8 *)va + page_offset, to_copy, to);
		if (err != to_copy) {
			if (kernel_if->host)
				kunmap(kernel_if->u.h.page[page_index]);
			return VMCI_ERROR_INVALID_ARGS;
		}
		bytes_copied += to_copy;
		if (kernel_if->host)
			kunmap(kernel_if->u.h.page[page_index]);
	}

	return VMCI_SUCCESS;
}

/*
 * Allocates two list of PPNs --- one for the pages in the produce queue,
 * and the other for the pages in the consume queue. Intializes the list
 * of PPNs with the page frame numbers of the KVA for the two queues (and
 * the queue headers).
 */
static int qp_alloc_ppn_set(void *prod_q,
			    u64 num_produce_pages,
			    void *cons_q,
			    u64 num_consume_pages, struct ppn_set *ppn_set)
{
	u64 *produce_ppns;
	u64 *consume_ppns;
	struct vmci_queue *produce_q = prod_q;
	struct vmci_queue *consume_q = cons_q;
	u64 i;

	if (!produce_q || !num_produce_pages || !consume_q ||
	    !num_consume_pages || !ppn_set)
		return VMCI_ERROR_INVALID_ARGS;

	if (ppn_set->initialized)
		return VMCI_ERROR_ALREADY_EXISTS;

	produce_ppns =
	    kmalloc_array(num_produce_pages, sizeof(*produce_ppns),
			  GFP_KERNEL);
	if (!produce_ppns)
		return VMCI_ERROR_NO_MEM;

	consume_ppns =
	    kmalloc_array(num_consume_pages, sizeof(*consume_ppns),
			  GFP_KERNEL);
	if (!consume_ppns) {
		kfree(produce_ppns);
		return VMCI_ERROR_NO_MEM;
	}

	for (i = 0; i < num_produce_pages; i++)
		produce_ppns[i] =
			produce_q->kernel_if->u.g.pas[i] >> PAGE_SHIFT;

	for (i = 0; i < num_consume_pages; i++)
		consume_ppns[i] =
			consume_q->kernel_if->u.g.pas[i] >> PAGE_SHIFT;

	ppn_set->num_produce_pages = num_produce_pages;
	ppn_set->num_consume_pages = num_consume_pages;
	ppn_set->produce_ppns = produce_ppns;
	ppn_set->consume_ppns = consume_ppns;
	ppn_set->initialized = true;
	return VMCI_SUCCESS;
}

/*
 * Frees the two list of PPNs for a queue pair.
 */
static void qp_free_ppn_set(struct ppn_set *ppn_set)
{
	if (ppn_set->initialized) {
		/* Do not call these functions on NULL inputs. */
		kfree(ppn_set->produce_ppns);
		kfree(ppn_set->consume_ppns);
	}
	memset(ppn_set, 0, sizeof(*ppn_set));
}

/*
 * Populates the list of PPNs in the hypercall structure with the PPNS
 * of the produce queue and the consume queue.
 */
static int qp_populate_ppn_set(u8 *call_buf, const struct ppn_set *ppn_set)
{
	if (vmci_use_ppn64()) {
		memcpy(call_buf, ppn_set->produce_ppns,
		       ppn_set->num_produce_pages *
		       sizeof(*ppn_set->produce_ppns));
		memcpy(call_buf +
		       ppn_set->num_produce_pages *
		       sizeof(*ppn_set->produce_ppns),
		       ppn_set->consume_ppns,
		       ppn_set->num_consume_pages *
		       sizeof(*ppn_set->consume_ppns));
	} else {
		int i;
		u32 *ppns = (u32 *) call_buf;

		for (i = 0; i < ppn_set->num_produce_pages; i++)
			ppns[i] = (u32) ppn_set->produce_ppns[i];

		ppns = &ppns[ppn_set->num_produce_pages];

		for (i = 0; i < ppn_set->num_consume_pages; i++)
			ppns[i] = (u32) ppn_set->consume_ppns[i];
	}

	return VMCI_SUCCESS;
}

/*
 * Allocates kernel VA space of specified size plus space for the queue
 * and kernel interface.  This is different from the guest queue allocator,
 * because we do not allocate our own queue header/data pages here but
 * share those of the guest.
 */
static struct vmci_queue *qp_host_alloc_queue(u64 size)
{
	struct vmci_queue *queue;
	size_t queue_page_size;
	u64 num_pages;
	const size_t queue_size = sizeof(*queue) + sizeof(*(queue->kernel_if));

	if (size > SIZE_MAX - PAGE_SIZE)
		return NULL;
	num_pages = DIV_ROUND_UP(size, PAGE_SIZE) + 1;
	if (num_pages > (SIZE_MAX - queue_size) /
		 sizeof(*queue->kernel_if->u.h.page))
		return NULL;

	queue_page_size = num_pages * sizeof(*queue->kernel_if->u.h.page);

	queue = kzalloc(queue_size + queue_page_size, GFP_KERNEL);
	if (queue) {
		queue->q_header = NULL;
		queue->saved_header = NULL;
		queue->kernel_if = (struct vmci_queue_kern_if *)(queue + 1);
		queue->kernel_if->host = true;
		queue->kernel_if->mutex = NULL;
		queue->kernel_if->num_pages = num_pages;
		queue->kernel_if->u.h.header_page =
		    (struct page **)((u8 *)queue + queue_size);
		queue->kernel_if->u.h.page =
			&queue->kernel_if->u.h.header_page[1];
	}

	return queue;
}

/*
 * Frees kernel memory for a given queue (header plus translation
 * structure).
 */
static void qp_host_free_queue(struct vmci_queue *queue, u64 queue_size)
{
	kfree(queue);
}

/*
 * Initialize the mutex for the pair of queues.  This mutex is used to
 * protect the q_header and the buffer from changing out from under any
 * users of either queue.  Of course, it's only any good if the mutexes
 * are actually acquired.  Queue structure must lie on non-paged memory
 * or we cannot guarantee access to the mutex.
 */
static void qp_init_queue_mutex(struct vmci_queue *produce_q,
				struct vmci_queue *consume_q)
{
	/*
	 * Only the host queue has shared state - the guest queues do not
	 * need to synchronize access using a queue mutex.
	 */

	if (produce_q->kernel_if->host) {
		produce_q->kernel_if->mutex = &produce_q->kernel_if->__mutex;
		consume_q->kernel_if->mutex = &produce_q->kernel_if->__mutex;
		mutex_init(produce_q->kernel_if->mutex);
	}
}

/*
 * Cleans up the mutex for the pair of queues.
 */
static void qp_cleanup_queue_mutex(struct vmci_queue *produce_q,
				   struct vmci_queue *consume_q)
{
	if (produce_q->kernel_if->host) {
		produce_q->kernel_if->mutex = NULL;
		consume_q->kernel_if->mutex = NULL;
	}
}

/*
 * Acquire the mutex for the queue.  Note that the produce_q and
 * the consume_q share a mutex.  So, only one of the two need to
 * be passed in to this routine.  Either will work just fine.
 */
static void qp_acquire_queue_mutex(struct vmci_queue *queue)
{
	if (queue->kernel_if->host)
		mutex_lock(queue->kernel_if->mutex);
}

/*
 * Release the mutex for the queue.  Note that the produce_q and
 * the consume_q share a mutex.  So, only one of the two need to
 * be passed in to this routine.  Either will work just fine.
 */
static void qp_release_queue_mutex(struct vmci_queue *queue)
{
	if (queue->kernel_if->host)
		mutex_unlock(queue->kernel_if->mutex);
}

/*
 * Helper function to release pages in the PageStoreAttachInfo
 * previously obtained using get_user_pages.
 */
static void qp_release_pages(struct page **pages,
			     u64 num_pages, bool dirty)
{
	int i;

	for (i = 0; i < num_pages; i++) {
		if (dirty)
			set_page_dirty_lock(pages[i]);

		put_page(pages[i]);
		pages[i] = NULL;
	}
}

/*
 * Lock the user pages referenced by the {produce,consume}Buffer
 * struct into memory and populate the {produce,consume}Pages
 * arrays in the attach structure with them.
 */
static int qp_host_get_user_memory(u64 produce_uva,
				   u64 consume_uva,
				   struct vmci_queue *produce_q,
				   struct vmci_queue *consume_q)
{
	int retval;
	int err = VMCI_SUCCESS;

	retval = get_user_pages_fast((uintptr_t) produce_uva,
				     produce_q->kernel_if->num_pages,
				     FOLL_WRITE,
				     produce_q->kernel_if->u.h.header_page);
	if (retval < (int)produce_q->kernel_if->num_pages) {
		pr_debug("get_user_pages_fast(produce) failed (retval=%d)",
			retval);
		if (retval > 0)
			qp_release_pages(produce_q->kernel_if->u.h.header_page,
					retval, false);
		err = VMCI_ERROR_NO_MEM;
		goto out;
	}

	retval = get_user_pages_fast((uintptr_t) consume_uva,
				     consume_q->kernel_if->num_pages,
				     FOLL_WRITE,
				     consume_q->kernel_if->u.h.header_page);
	if (retval < (int)consume_q->kernel_if->num_pages) {
		pr_debug("get_user_pages_fast(consume) failed (retval=%d)",
			retval);
		if (retval > 0)
			qp_release_pages(consume_q->kernel_if->u.h.header_page,
					retval, false);
		qp_release_pages(produce_q->kernel_if->u.h.header_page,
				 produce_q->kernel_if->num_pages, false);
		err = VMCI_ERROR_NO_MEM;
	}

 out:
	return err;
}

/*
 * Registers the specification of the user pages used for backing a queue
 * pair. Enough information to map in pages is stored in the OS specific
 * part of the struct vmci_queue structure.
 */
static int qp_host_register_user_memory(struct vmci_qp_page_store *page_store,
					struct vmci_queue *produce_q,
					struct vmci_queue *consume_q)
{
	u64 produce_uva;
	u64 consume_uva;

	/*
	 * The new style and the old style mapping only differs in
	 * that we either get a single or two UVAs, so we split the
	 * single UVA range at the appropriate spot.
	 */
	produce_uva = page_store->pages;
	consume_uva = page_store->pages +
	    produce_q->kernel_if->num_pages * PAGE_SIZE;
	return qp_host_get_user_memory(produce_uva, consume_uva, produce_q,
				       consume_q);
}

/*
 * Releases and removes the references to user pages stored in the attach
 * struct.  Pages are released from the page cache and may become
 * swappable again.
 */
static void qp_host_unregister_user_memory(struct vmci_queue *produce_q,
					   struct vmci_queue *consume_q)
{
	qp_release_pages(produce_q->kernel_if->u.h.header_page,
			 produce_q->kernel_if->num_pages, true);
	memset(produce_q->kernel_if->u.h.header_page, 0,
	       sizeof(*produce_q->kernel_if->u.h.header_page) *
	       produce_q->kernel_if->num_pages);
	qp_release_pages(consume_q->kernel_if->u.h.header_page,
			 consume_q->kernel_if->num_pages, true);
	memset(consume_q->kernel_if->u.h.header_page, 0,
	       sizeof(*consume_q->kernel_if->u.h.header_page) *
	       consume_q->kernel_if->num_pages);
}

/*
 * Once qp_host_register_user_memory has been performed on a
 * queue, the queue pair headers can be mapped into the
 * kernel. Once mapped, they must be unmapped with
 * qp_host_unmap_queues prior to calling
 * qp_host_unregister_user_memory.
 * Pages are pinned.
 */
static int qp_host_map_queues(struct vmci_queue *produce_q,
			      struct vmci_queue *consume_q)
{
	int result;

	if (!produce_q->q_header || !consume_q->q_header) {
		struct page *headers[2];

		if (produce_q->q_header != consume_q->q_header)
			return VMCI_ERROR_QUEUEPAIR_MISMATCH;

		if (produce_q->kernel_if->u.h.header_page == NULL ||
		    *produce_q->kernel_if->u.h.header_page == NULL)
			return VMCI_ERROR_UNAVAILABLE;

		headers[0] = *produce_q->kernel_if->u.h.header_page;
		headers[1] = *consume_q->kernel_if->u.h.header_page;

		produce_q->q_header = vmap(headers, 2, VM_MAP, PAGE_KERNEL);
		if (produce_q->q_header != NULL) {
			consume_q->q_header =
			    (struct vmci_queue_header *)((u8 *)
							 produce_q->q_header +
							 PAGE_SIZE);
			result = VMCI_SUCCESS;
		} else {
			pr_warn("vmap failed\n");
			result = VMCI_ERROR_NO_MEM;
		}
	} else {
		result = VMCI_SUCCESS;
	}

	return result;
}

/*
 * Unmaps previously mapped queue pair headers from the kernel.
 * Pages are unpinned.
 */
static int qp_host_unmap_queues(u32 gid,
				struct vmci_queue *produce_q,
				struct vmci_queue *consume_q)
{
	if (produce_q->q_header) {
		if (produce_q->q_header < consume_q->q_header)
			vunmap(produce_q->q_header);
		else
			vunmap(consume_q->q_header);

		produce_q->q_header = NULL;
		consume_q->q_header = NULL;
	}

	return VMCI_SUCCESS;
}

/*
 * Finds the entry in the list corresponding to a given handle. Assumes
 * that the list is locked.
 */
static struct qp_entry *qp_list_find(struct qp_list *qp_list,
				     struct vmci_handle handle)
{
	struct qp_entry *entry;

	if (vmci_handle_is_invalid(handle))
		return NULL;

	list_for_each_entry(entry, &qp_list->head, list_item) {
		if (vmci_handle_is_equal(entry->handle, handle))
			return entry;
	}

	return NULL;
}

/*
 * Finds the entry in the list corresponding to a given handle.
 */
static struct qp_guest_endpoint *
qp_guest_handle_to_entry(struct vmci_handle handle)
{
	struct qp_guest_endpoint *entry;
	struct qp_entry *qp = qp_list_find(&qp_guest_endpoints, handle);

	entry = qp ? container_of(
		qp, struct qp_guest_endpoint, qp) : NULL;
	return entry;
}

/*
 * Finds the entry in the list corresponding to a given handle.
 */
static struct qp_broker_entry *
qp_broker_handle_to_entry(struct vmci_handle handle)
{
	struct qp_broker_entry *entry;
	struct qp_entry *qp = qp_list_find(&qp_broker_list, handle);

	entry = qp ? container_of(
		qp, struct qp_broker_entry, qp) : NULL;
	return entry;
}

/*
 * Dispatches a queue pair event message directly into the local event
 * queue.
 */
static int qp_notify_peer_local(bool attach, struct vmci_handle handle)
{
	u32 context_id = vmci_get_context_id();
	struct vmci_event_qp ev;

	ev.msg.hdr.dst = vmci_make_handle(context_id, VMCI_EVENT_HANDLER);
	ev.msg.hdr.src = vmci_make_handle(VMCI_HYPERVISOR_CONTEXT_ID,
					  VMCI_CONTEXT_RESOURCE_ID);
	ev.msg.hdr.payload_size = sizeof(ev) - sizeof(ev.msg.hdr);
	ev.msg.event_data.event =
	    attach ? VMCI_EVENT_QP_PEER_ATTACH : VMCI_EVENT_QP_PEER_DETACH;
	ev.payload.peer_id = context_id;
	ev.payload.handle = handle;

	return vmci_event_dispatch(&ev.msg.hdr);
}

/*
 * Allocates and initializes a qp_guest_endpoint structure.
 * Allocates a queue_pair rid (and handle) iff the given entry has
 * an invalid handle.  0 through VMCI_RESERVED_RESOURCE_ID_MAX
 * are reserved handles.  Assumes that the QP list mutex is held
 * by the caller.
 */
static struct qp_guest_endpoint *
qp_guest_endpoint_create(struct vmci_handle handle,
			 u32 peer,
			 u32 flags,
			 u64 produce_size,
			 u64 consume_size,
			 void *produce_q,
			 void *consume_q)
{
	int result;
	struct qp_guest_endpoint *entry;
	/* One page each for the queue headers. */
	const u64 num_ppns = DIV_ROUND_UP(produce_size, PAGE_SIZE) +
	    DIV_ROUND_UP(consume_size, PAGE_SIZE) + 2;

	if (vmci_handle_is_invalid(handle)) {
		u32 context_id = vmci_get_context_id();

		handle = vmci_make_handle(context_id, VMCI_INVALID_ID);
	}

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (entry) {
		entry->qp.peer = peer;
		entry->qp.flags = flags;
		entry->qp.produce_size = produce_size;
		entry->qp.consume_size = consume_size;
		entry->qp.ref_count = 0;
		entry->num_ppns = num_ppns;
		entry->produce_q = produce_q;
		entry->consume_q = consume_q;
		INIT_LIST_HEAD(&entry->qp.list_item);

		/* Add resource obj */
		result = vmci_resource_add(&entry->resource,
					   VMCI_RESOURCE_TYPE_QPAIR_GUEST,
					   handle);
		entry->qp.handle = vmci_resource_handle(&entry->resource);
		if ((result != VMCI_SUCCESS) ||
		    qp_list_find(&qp_guest_endpoints, entry->qp.handle)) {
			pr_warn("Failed to add new resource (handle=0x%x:0x%x), error: %d",
				handle.context, handle.resource, result);
			kfree(entry);
			entry = NULL;
		}
	}
	return entry;
}

/*
 * Frees a qp_guest_endpoint structure.
 */
static void qp_guest_endpoint_destroy(struct qp_guest_endpoint *entry)
{
	qp_free_ppn_set(&entry->ppn_set);
	qp_cleanup_queue_mutex(entry->produce_q, entry->consume_q);
	qp_free_queue(entry->produce_q, entry->qp.produce_size);
	qp_free_queue(entry->consume_q, entry->qp.consume_size);
	/* Unlink from resource hash table and free callback */
	vmci_resource_remove(&entry->resource);

	kfree(entry);
}

/*
 * Helper to make a queue_pairAlloc hypercall when the driver is
 * supporting a guest device.
 */
static int qp_alloc_hypercall(const struct qp_guest_endpoint *entry)
{
	struct vmci_qp_alloc_msg *alloc_msg;
	size_t msg_size;
	size_t ppn_size;
	int result;

	if (!entry || entry->num_ppns <= 2)
		return VMCI_ERROR_INVALID_ARGS;

	ppn_size = vmci_use_ppn64() ? sizeof(u64) : sizeof(u32);
	msg_size = sizeof(*alloc_msg) +
	    (size_t) entry->num_ppns * ppn_size;
	alloc_msg = kmalloc(msg_size, GFP_KERNEL);
	if (!alloc_msg)
		return VMCI_ERROR_NO_MEM;

	alloc_msg->hdr.dst = vmci_make_handle(VMCI_HYPERVISOR_CONTEXT_ID,
					      VMCI_QUEUEPAIR_ALLOC);
	alloc_msg->hdr.src = VMCI_ANON_SRC_HANDLE;
	alloc_msg->hdr.payload_size = msg_size - VMCI_DG_HEADERSIZE;
	alloc_msg->handle = entry->qp.handle;
	alloc_msg->peer = entry->qp.peer;
	alloc_msg->flags = entry->qp.flags;
	alloc_msg->produce_size = entry->qp.produce_size;
	alloc_msg->consume_size = entry->qp.consume_size;
	alloc_msg->num_ppns = entry->num_ppns;

	result = qp_populate_ppn_set((u8 *)alloc_msg + sizeof(*alloc_msg),
				     &entry->ppn_set);
	if (result == VMCI_SUCCESS)
		result = vmci_send_datagram(&alloc_msg->hdr);

	kfree(alloc_msg);

	return result;
}

/*
 * Helper to make a queue_pairDetach hypercall when the driver is
 * supporting a guest device.
 */
static int qp_detatch_hypercall(struct vmci_handle handle)
{
	struct vmci_qp_detach_msg detach_msg;

	detach_msg.hdr.dst = vmci_make_handle(VMCI_HYPERVISOR_CONTEXT_ID,
					      VMCI_QUEUEPAIR_DETACH);
	detach_msg.hdr.src = VMCI_ANON_SRC_HANDLE;
	detach_msg.hdr.payload_size = sizeof(handle);
	detach_msg.handle = handle;

	return vmci_send_datagram(&detach_msg.hdr);
}

/*
 * Adds the given entry to the list. Assumes that the list is locked.
 */
static void qp_list_add_entry(struct qp_list *qp_list, struct qp_entry *entry)
{
	if (entry)
		list_add(&entry->list_item, &qp_list->head);
}

/*
 * Removes the given entry from the list. Assumes that the list is locked.
 */
static void qp_list_remove_entry(struct qp_list *qp_list,
				 struct qp_entry *entry)
{
	if (entry)
		list_del(&entry->list_item);
}

/*
 * Helper for VMCI queue_pair detach interface. Frees the physical
 * pages for the queue pair.
 */
static int qp_detatch_guest_work(struct vmci_handle handle)
{
	int result;
	struct qp_guest_endpoint *entry;
	u32 ref_count = ~0;	/* To avoid compiler warning below */

	mutex_lock(&qp_guest_endpoints.mutex);

	entry = qp_guest_handle_to_entry(handle);
	if (!entry) {
		mutex_unlock(&qp_guest_endpoints.mutex);
		return VMCI_ERROR_NOT_FOUND;
	}

	if (entry->qp.flags & VMCI_QPFLAG_LOCAL) {
		result = VMCI_SUCCESS;

		if (entry->qp.ref_count > 1) {
			result = qp_notify_peer_local(false, handle);
			/*
			 * We can fail to notify a local queuepair
			 * because we can't allocate.  We still want
			 * to release the entry if that happens, so
			 * don't bail out yet.
			 */
		}
	} else {
		result = qp_detatch_hypercall(handle);
		if (result < VMCI_SUCCESS) {
			/*
			 * We failed to notify a non-local queuepair.
			 * That other queuepair might still be
			 * accessing the shared memory, so don't
			 * release the entry yet.  It will get cleaned
			 * up by VMCIqueue_pair_Exit() if necessary
			 * (assuming we are going away, otherwise why
			 * did this fail?).
			 */

			mutex_unlock(&qp_guest_endpoints.mutex);
			return result;
		}
	}

	/*
	 * If we get here then we either failed to notify a local queuepair, or
	 * we succeeded in all cases.  Release the entry if required.
	 */

	entry->qp.ref_count--;
	if (entry->qp.ref_count == 0)
		qp_list_remove_entry(&qp_guest_endpoints, &entry->qp);

	/* If we didn't remove the entry, this could change once we unlock. */
	if (entry)
		ref_count = entry->qp.ref_count;

	mutex_unlock(&qp_guest_endpoints.mutex);

	if (ref_count == 0)
		qp_guest_endpoint_destroy(entry);

	return result;
}

/*
 * This functions handles the actual allocation of a VMCI queue
 * pair guest endpoint. Allocates physical pages for the queue
 * pair. It makes OS dependent calls through generic wrappers.
 */
static int qp_alloc_guest_work(struct vmci_handle *handle,
			       struct vmci_queue **produce_q,
			       u64 produce_size,
			       struct vmci_queue **consume_q,
			       u64 consume_size,
			       u32 peer,
			       u32 flags,
			       u32 priv_flags)
{
	const u64 num_produce_pages =
	    DIV_ROUND_UP(produce_size, PAGE_SIZE) + 1;
	const u64 num_consume_pages =
	    DIV_ROUND_UP(consume_size, PAGE_SIZE) + 1;
	void *my_produce_q = NULL;
	void *my_consume_q = NULL;
	int result;
	struct qp_guest_endpoint *queue_pair_entry = NULL;

	if (priv_flags != VMCI_NO_PRIVILEGE_FLAGS)
		return VMCI_ERROR_NO_ACCESS;

	mutex_lock(&qp_guest_endpoints.mutex);

	queue_pair_entry = qp_guest_handle_to_entry(*handle);
	if (queue_pair_entry) {
		if (queue_pair_entry->qp.flags & VMCI_QPFLAG_LOCAL) {
			/* Local attach case. */
			if (queue_pair_entry->qp.ref_count > 1) {
				pr_devel("Error attempting to attach more than once\n");
				result = VMCI_ERROR_UNAVAILABLE;
				goto error_keep_entry;
			}

			if (queue_pair_entry->qp.produce_size != consume_size ||
			    queue_pair_entry->qp.consume_size !=
			    produce_size ||
			    queue_pair_entry->qp.flags !=
			    (flags & ~VMCI_QPFLAG_ATTACH_ONLY)) {
				pr_devel("Error mismatched queue pair in local attach\n");
				result = VMCI_ERROR_QUEUEPAIR_MISMATCH;
				goto error_keep_entry;
			}

			/*
			 * Do a local attach.  We swap the consume and
			 * produce queues for the attacher and deliver
			 * an attach event.
			 */
			result = qp_notify_peer_local(true, *handle);
			if (result < VMCI_SUCCESS)
				goto error_keep_entry;

			my_produce_q = queue_pair_entry->consume_q;
			my_consume_q = queue_pair_entry->produce_q;
			goto out;
		}

		result = VMCI_ERROR_ALREADY_EXISTS;
		goto error_keep_entry;
	}

	my_produce_q = qp_alloc_queue(produce_size, flags);
	if (!my_produce_q) {
		pr_warn("Error allocating pages for produce queue\n");
		result = VMCI_ERROR_NO_MEM;
		goto error;
	}

	my_consume_q = qp_alloc_queue(consume_size, flags);
	if (!my_consume_q) {
		pr_warn("Error allocating pages for consume queue\n");
		result = VMCI_ERROR_NO_MEM;
		goto error;
	}

	queue_pair_entry = qp_guest_endpoint_create(*handle, peer, flags,
						    produce_size, consume_size,
						    my_produce_q, my_consume_q);
	if (!queue_pair_entry) {
		pr_warn("Error allocating memory in %s\n", __func__);
		result = VMCI_ERROR_NO_MEM;
		goto error;
	}

	result = qp_alloc_ppn_set(my_produce_q, num_produce_pages, my_consume_q,
				  num_consume_pages,
				  &queue_pair_entry->ppn_set);
	if (result < VMCI_SUCCESS) {
		pr_warn("qp_alloc_ppn_set failed\n");
		goto error;
	}

	/*
	 * It's only necessary to notify the host if this queue pair will be
	 * attached to from another context.
	 */
	if (queue_pair_entry->qp.flags & VMCI_QPFLAG_LOCAL) {
		/* Local create case. */
		u32 context_id = vmci_get_context_id();

		/*
		 * Enforce similar checks on local queue pairs as we
		 * do for regular ones.  The handle's context must
		 * match the creator or attacher context id (here they
		 * are both the current context id) and the
		 * attach-only flag cannot exist during create.  We
		 * also ensure specified peer is this context or an
		 * invalid one.
		 */
		if (queue_pair_entry->qp.handle.context != context_id ||
		    (queue_pair_entry->qp.peer != VMCI_INVALID_ID &&
		     queue_pair_entry->qp.peer != context_id)) {
			result = VMCI_ERROR_NO_ACCESS;
			goto error;
		}

		if (queue_pair_entry->qp.flags & VMCI_QPFLAG_ATTACH_ONLY) {
			result = VMCI_ERROR_NOT_FOUND;
			goto error;
		}
	} else {
		result = qp_alloc_hypercall(queue_pair_entry);
		if (result < VMCI_SUCCESS) {
			pr_warn("qp_alloc_hypercall result = %d\n", result);
			goto error;
		}
	}

	qp_init_queue_mutex((struct vmci_queue *)my_produce_q,
			    (struct vmci_queue *)my_consume_q);

	qp_list_add_entry(&qp_guest_endpoints, &queue_pair_entry->qp);

 out:
	queue_pair_entry->qp.ref_count++;
	*handle = queue_pair_entry->qp.handle;
	*produce_q = (struct vmci_queue *)my_produce_q;
	*consume_q = (struct vmci_queue *)my_consume_q;

	/*
	 * We should initialize the queue pair header pages on a local
	 * queue pair create.  For non-local queue pairs, the
	 * hypervisor initializes the header pages in the create step.
	 */
	if ((queue_pair_entry->qp.flags & VMCI_QPFLAG_LOCAL) &&
	    queue_pair_entry->qp.ref_count == 1) {
		vmci_q_header_init((*produce_q)->q_header, *handle);
		vmci_q_header_init((*consume_q)->q_header, *handle);
	}

	mutex_unlock(&qp_guest_endpoints.mutex);

	return VMCI_SUCCESS;

 error:
	mutex_unlock(&qp_guest_endpoints.mutex);
	if (queue_pair_entry) {
		/* The queues will be freed inside the destroy routine. */
		qp_guest_endpoint_destroy(queue_pair_entry);
	} else {
		qp_free_queue(my_produce_q, produce_size);
		qp_free_queue(my_consume_q, consume_size);
	}
	return result;

 error_keep_entry:
	/* This path should only be used when an existing entry was found. */
	mutex_unlock(&qp_guest_endpoints.mutex);
	return result;
}

/*
 * The first endpoint issuing a queue pair allocation will create the state
 * of the queue pair in the queue pair broker.
 *
 * If the creator is a guest, it will associate a VMX virtual address range
 * with the queue pair as specified by the page_store. For compatibility with
 * older VMX'en, that would use a separate step to set the VMX virtual
 * address range, the virtual address range can be registered later using
 * vmci_qp_broker_set_page_store. In that case, a page_store of NULL should be
 * used.
 *
 * If the creator is the host, a page_store of NULL should be used as well,
 * since the host is not able to supply a page store for the queue pair.
 *
 * For older VMX and host callers, the queue pair will be created in the
 * VMCIQPB_CREATED_NO_MEM state, and for current VMX callers, it will be
 * created in VMCOQPB_CREATED_MEM state.
 */
static int qp_broker_create(struct vmci_handle handle,
			    u32 peer,
			    u32 flags,
			    u32 priv_flags,
			    u64 produce_size,
			    u64 consume_size,
			    struct vmci_qp_page_store *page_store,
			    struct vmci_ctx *context,
			    vmci_event_release_cb wakeup_cb,
			    void *client_data, struct qp_broker_entry **ent)
{
	struct qp_broker_entry *entry = NULL;
	const u32 context_id = vmci_ctx_get_id(context);
	bool is_local = flags & VMCI_QPFLAG_LOCAL;
	int result;
	u64 guest_produce_size;
	u64 guest_consume_size;

	/* Do not create if the caller asked not to. */
	if (flags & VMCI_QPFLAG_ATTACH_ONLY)
		return VMCI_ERROR_NOT_FOUND;

	/*
	 * Creator's context ID should match handle's context ID or the creator
	 * must allow the context in handle's context ID as the "peer".
	 */
	if (handle.context != context_id && handle.context != peer)
		return VMCI_ERROR_NO_ACCESS;

	if (VMCI_CONTEXT_IS_VM(context_id) && VMCI_CONTEXT_IS_VM(peer))
		return VMCI_ERROR_DST_UNREACHABLE;

	/*
	 * Creator's context ID for local queue pairs should match the
	 * peer, if a peer is specified.
	 */
	if (is_local && peer != VMCI_INVALID_ID && context_id != peer)
		return VMCI_ERROR_NO_ACCESS;

	entry = kzalloc(sizeof(*entry), GFP_ATOMIC);
	if (!entry)
		return VMCI_ERROR_NO_MEM;

	if (vmci_ctx_get_id(context) == VMCI_HOST_CONTEXT_ID && !is_local) {
		/*
		 * The queue pair broker entry stores values from the guest
		 * point of view, so a creating host side endpoint should swap
		 * produce and consume values -- unless it is a local queue
		 * pair, in which case no swapping is necessary, since the local
		 * attacher will swap queues.
		 */

		guest_produce_size = consume_size;
		guest_consume_size = produce_size;
	} else {
		guest_produce_size = produce_size;
		guest_consume_size = consume_size;
	}

	entry->qp.handle = handle;
	entry->qp.peer = peer;
	entry->qp.flags = flags;
	entry->qp.produce_size = guest_produce_size;
	entry->qp.consume_size = guest_consume_size;
	entry->qp.ref_count = 1;
	entry->create_id = context_id;
	entry->attach_id = VMCI_INVALID_ID;
	entry->state = VMCIQPB_NEW;
	entry->require_trusted_attach =
	    !!(context->priv_flags & VMCI_PRIVILEGE_FLAG_RESTRICTED);
	entry->created_by_trusted =
	    !!(priv_flags & VMCI_PRIVILEGE_FLAG_TRUSTED);
	entry->vmci_page_files = false;
	entry->wakeup_cb = wakeup_cb;
	entry->client_data = client_data;
	entry->produce_q = qp_host_alloc_queue(guest_produce_size);
	if (entry->produce_q == NULL) {
		result = VMCI_ERROR_NO_MEM;
		goto error;
	}
	entry->consume_q = qp_host_alloc_queue(guest_consume_size);
	if (entry->consume_q == NULL) {
		result = VMCI_ERROR_NO_MEM;
		goto error;
	}

	qp_init_queue_mutex(entry->produce_q, entry->consume_q);

	INIT_LIST_HEAD(&entry->qp.list_item);

	if (is_local) {
		u8 *tmp;

		entry->local_mem = kcalloc(QPE_NUM_PAGES(entry->qp),
					   PAGE_SIZE, GFP_KERNEL);
		if (entry->local_mem == NULL) {
			result = VMCI_ERROR_NO_MEM;
			goto error;
		}
		entry->state = VMCIQPB_CREATED_MEM;
		entry->produce_q->q_header = entry->local_mem;
		tmp = (u8 *)entry->local_mem + PAGE_SIZE *
		    (DIV_ROUND_UP(entry->qp.produce_size, PAGE_SIZE) + 1);
		entry->consume_q->q_header = (struct vmci_queue_header *)tmp;
	} else if (page_store) {
		/*
		 * The VMX already initialized the queue pair headers, so no
		 * need for the kernel side to do that.
		 */
		result = qp_host_register_user_memory(page_store,
						      entry->produce_q,
						      entry->consume_q);
		if (result < VMCI_SUCCESS)
			goto error;

		entry->state = VMCIQPB_CREATED_MEM;
	} else {
		/*
		 * A create without a page_store may be either a host
		 * side create (in which case we are waiting for the
		 * guest side to supply the memory) or an old style
		 * queue pair create (in which case we will expect a
		 * set page store call as the next step).
		 */
		entry->state = VMCIQPB_CREATED_NO_MEM;
	}

	qp_list_add_entry(&qp_broker_list, &entry->qp);
	if (ent != NULL)
		*ent = entry;

	/* Add to resource obj */
	result = vmci_resource_add(&entry->resource,
				   VMCI_RESOURCE_TYPE_QPAIR_HOST,
				   handle);
	if (result != VMCI_SUCCESS) {
		pr_warn("Failed to add new resource (handle=0x%x:0x%x), error: %d",
			handle.context, handle.resource, result);
		goto error;
	}

	entry->qp.handle = vmci_resource_handle(&entry->resource);
	if (is_local) {
		vmci_q_header_init(entry->produce_q->q_header,
				   entry->qp.handle);
		vmci_q_header_init(entry->consume_q->q_header,
				   entry->qp.handle);
	}

	vmci_ctx_qp_create(context, entry->qp.handle);

	return VMCI_SUCCESS;

 error:
	if (entry != NULL) {
		qp_host_free_queue(entry->produce_q, guest_produce_size);
		qp_host_free_queue(entry->consume_q, guest_consume_size);
		kfree(entry);
	}

	return result;
}

/*
 * Enqueues an event datagram to notify the peer VM attached to
 * the given queue pair handle about attach/detach event by the
 * given VM.  Returns Payload size of datagram enqueued on
 * success, error code otherwise.
 */
static int qp_notify_peer(bool attach,
			  struct vmci_handle handle,
			  u32 my_id,
			  u32 peer_id)
{
	int rv;
	struct vmci_event_qp ev;

	if (vmci_handle_is_invalid(handle) || my_id == VMCI_INVALID_ID ||
	    peer_id == VMCI_INVALID_ID)
		return VMCI_ERROR_INVALID_ARGS;

	/*
	 * In vmci_ctx_enqueue_datagram() we enforce the upper limit on
	 * number of pending events from the hypervisor to a given VM
	 * otherwise a rogue VM could do an arbitrary number of attach
	 * and detach operations causing memory pressure in the host
	 * kernel.
	 */

	ev.msg.hdr.dst = vmci_make_handle(peer_id, VMCI_EVENT_HANDLER);
	ev.msg.hdr.src = vmci_make_handle(VMCI_HYPERVISOR_CONTEXT_ID,
					  VMCI_CONTEXT_RESOURCE_ID);
	ev.msg.hdr.payload_size = sizeof(ev) - sizeof(ev.msg.hdr);
	ev.msg.event_data.event = attach ?
	    VMCI_EVENT_QP_PEER_ATTACH : VMCI_EVENT_QP_PEER_DETACH;
	ev.payload.handle = handle;
	ev.payload.peer_id = my_id;

	rv = vmci_datagram_dispatch(VMCI_HYPERVISOR_CONTEXT_ID,
				    &ev.msg.hdr, false);
	if (rv < VMCI_SUCCESS)
		pr_warn("Failed to enqueue queue_pair %s event datagram for context (ID=0x%x)\n",
			attach ? "ATTACH" : "DETACH", peer_id);

	return rv;
}

/*
 * The second endpoint issuing a queue pair allocation will attach to
 * the queue pair registered with the queue pair broker.
 *
 * If the attacher is a guest, it will associate a VMX virtual address
 * range with the queue pair as specified by the page_store. At this
 * point, the already attach host endpoint may start using the queue
 * pair, and an attach event is sent to it. For compatibility with
 * older VMX'en, that used a separate step to set the VMX virtual
 * address range, the virtual address range can be registered later
 * using vmci_qp_broker_set_page_store. In that case, a page_store of
 * NULL should be used, and the attach event will be generated once
 * the actual page store has been set.
 *
 * If the attacher is the host, a page_store of NULL should be used as
 * well, since the page store information is already set by the guest.
 *
 * For new VMX and host callers, the queue pair will be moved to the
 * VMCIQPB_ATTACHED_MEM state, and for older VMX callers, it will be
 * moved to the VMCOQPB_ATTACHED_NO_MEM state.
 */
static int qp_broker_attach(struct qp_broker_entry *entry,
			    u32 peer,
			    u32 flags,
			    u32 priv_flags,
			    u64 produce_size,
			    u64 consume_size,
			    struct vmci_qp_page_store *page_store,
			    struct vmci_ctx *context,
			    vmci_event_release_cb wakeup_cb,
			    void *client_data,
			    struct qp_broker_entry **ent)
{
	const u32 context_id = vmci_ctx_get_id(context);
	bool is_local = flags & VMCI_QPFLAG_LOCAL;
	int result;

	if (entry->state != VMCIQPB_CREATED_NO_MEM &&
	    entry->state != VMCIQPB_CREATED_MEM)
		return VMCI_ERROR_UNAVAILABLE;

	if (is_local) {
		if (!(entry->qp.flags & VMCI_QPFLAG_LOCAL) ||
		    context_id != entry->create_id) {
			return VMCI_ERROR_INVALID_ARGS;
		}
	} else if (context_id == entry->create_id ||
		   context_id == entry->attach_id) {
		return VMCI_ERROR_ALREADY_EXISTS;
	}

	if (VMCI_CONTEXT_IS_VM(context_id) &&
	    VMCI_CONTEXT_IS_VM(entry->create_id))
		return VMCI_ERROR_DST_UNREACHABLE;

	/*
	 * If we are attaching from a restricted context then the queuepair
	 * must have been created by a trusted endpoint.
	 */
	if ((context->priv_flags & VMCI_PRIVILEGE_FLAG_RESTRICTED) &&
	    !entry->created_by_trusted)
		return VMCI_ERROR_NO_ACCESS;

	/*
	 * If we are attaching to a queuepair that was created by a restricted
	 * context then we must be trusted.
	 */
	if (entry->require_trusted_attach &&
	    (!(priv_flags & VMCI_PRIVILEGE_FLAG_TRUSTED)))
		return VMCI_ERROR_NO_ACCESS;

	/*
	 * If the creator specifies VMCI_INVALID_ID in "peer" field, access
	 * control check is not performed.
	 */
	if (entry->qp.peer != VMCI_INVALID_ID && entry->qp.peer != context_id)
		return VMCI_ERROR_NO_ACCESS;

	if (entry->create_id == VMCI_HOST_CONTEXT_ID) {
		/*
		 * Do not attach if the caller doesn't support Host Queue Pairs
		 * and a host created this queue pair.
		 */

		if (!vmci_ctx_supports_host_qp(context))
			return VMCI_ERROR_INVALID_RESOURCE;

	} else if (context_id == VMCI_HOST_CONTEXT_ID) {
		struct vmci_ctx *create_context;
		bool supports_host_qp;

		/*
		 * Do not attach a host to a user created queue pair if that
		 * user doesn't support host queue pair end points.
		 */

		create_context = vmci_ctx_get(entry->create_id);
		supports_host_qp = vmci_ctx_supports_host_qp(create_context);
		vmci_ctx_put(create_context);

		if (!supports_host_qp)
			return VMCI_ERROR_INVALID_RESOURCE;
	}

	if ((entry->qp.flags & ~VMCI_QP_ASYMM) != (flags & ~VMCI_QP_ASYMM_PEER))
		return VMCI_ERROR_QUEUEPAIR_MISMATCH;

	if (context_id != VMCI_HOST_CONTEXT_ID) {
		/*
		 * The queue pair broker entry stores values from the guest
		 * point of view, so an attaching guest should match the values
		 * stored in the entry.
		 */

		if (entry->qp.produce_size != produce_size ||
		    entry->qp.consume_size != consume_size) {
			return VMCI_ERROR_QUEUEPAIR_MISMATCH;
		}
	} else if (entry->qp.produce_size != consume_size ||
		   entry->qp.consume_size != produce_size) {
		return VMCI_ERROR_QUEUEPAIR_MISMATCH;
	}

	if (context_id != VMCI_HOST_CONTEXT_ID) {
		/*
		 * If a guest attached to a queue pair, it will supply
		 * the backing memory.  If this is a pre NOVMVM vmx,
		 * the backing memory will be supplied by calling
		 * vmci_qp_broker_set_page_store() following the
		 * return of the vmci_qp_broker_alloc() call. If it is
		 * a vmx of version NOVMVM or later, the page store
		 * must be supplied as part of the
		 * vmci_qp_broker_alloc call.  Under all circumstances
		 * must the initially created queue pair not have any
		 * memory associated with it already.
		 */

		if (entry->state != VMCIQPB_CREATED_NO_MEM)
			return VMCI_ERROR_INVALID_ARGS;

		if (page_store != NULL) {
			/*
			 * Patch up host state to point to guest
			 * supplied memory. The VMX already
			 * initialized the queue pair headers, so no
			 * need for the kernel side to do that.
			 */

			result = qp_host_register_user_memory(page_store,
							      entry->produce_q,
							      entry->consume_q);
			if (result < VMCI_SUCCESS)
				return result;

			entry->state = VMCIQPB_ATTACHED_MEM;
		} else {
			entry->state = VMCIQPB_ATTACHED_NO_MEM;
		}
	} else if (entry->state == VMCIQPB_CREATED_NO_MEM) {
		/*
		 * The host side is attempting to attach to a queue
		 * pair that doesn't have any memory associated with
		 * it. This must be a pre NOVMVM vmx that hasn't set
		 * the page store information yet, or a quiesced VM.
		 */

		return VMCI_ERROR_UNAVAILABLE;
	} else {
		/* The host side has successfully attached to a queue pair. */
		entry->state = VMCIQPB_ATTACHED_MEM;
	}

	if (entry->state == VMCIQPB_ATTACHED_MEM) {
		result =
		    qp_notify_peer(true, entry->qp.handle, context_id,
				   entry->create_id);
		if (result < VMCI_SUCCESS)
			pr_warn("Failed to notify peer (ID=0x%x) of attach to queue pair (handle=0x%x:0x%x)\n",
				entry->create_id, entry->qp.handle.context,
				entry->qp.handle.resource);
	}

	entry->attach_id = context_id;
	entry->qp.ref_count++;
	if (wakeup_cb) {
		entry->wakeup_cb = wakeup_cb;
		entry->client_data = client_data;
	}

	/*
	 * When attaching to local queue pairs, the context already has
	 * an entry tracking the queue pair, so don't add another one.
	 */
	if (!is_local)
		vmci_ctx_qp_create(context, entry->qp.handle);

	if (ent != NULL)
		*ent = entry;

	return VMCI_SUCCESS;
}

/*
 * queue_pair_Alloc for use when setting up queue pair endpoints
 * on the host.
 */
static int qp_broker_alloc(struct vmci_handle handle,
			   u32 peer,
			   u32 flags,
			   u32 priv_flags,
			   u64 produce_size,
			   u64 consume_size,
			   struct vmci_qp_page_store *page_store,
			   struct vmci_ctx *context,
			   vmci_event_release_cb wakeup_cb,
			   void *client_data,
			   struct qp_broker_entry **ent,
			   bool *swap)
{
	const u32 context_id = vmci_ctx_get_id(context);
	bool create;
	struct qp_broker_entry *entry = NULL;
	bool is_local = flags & VMCI_QPFLAG_LOCAL;
	int result;

	if (vmci_handle_is_invalid(handle) ||
	    (flags & ~VMCI_QP_ALL_FLAGS) || is_local ||
	    !(produce_size || consume_size) ||
	    !context || context_id == VMCI_INVALID_ID ||
	    handle.context == VMCI_INVALID_ID) {
		return VMCI_ERROR_INVALID_ARGS;
	}

	if (page_store && !VMCI_QP_PAGESTORE_IS_WELLFORMED(page_store))
		return VMCI_ERROR_INVALID_ARGS;

	/*
	 * In the initial argument check, we ensure that non-vmkernel hosts
	 * are not allowed to create local queue pairs.
	 */

	mutex_lock(&qp_broker_list.mutex);

	if (!is_local && vmci_ctx_qp_exists(context, handle)) {
		pr_devel("Context (ID=0x%x) already attached to queue pair (handle=0x%x:0x%x)\n",
			 context_id, handle.context, handle.resource);
		mutex_unlock(&qp_broker_list.mutex);
		return VMCI_ERROR_ALREADY_EXISTS;
	}

	if (handle.resource != VMCI_INVALID_ID)
		entry = qp_broker_handle_to_entry(handle);

	if (!entry) {
		create = true;
		result =
		    qp_broker_create(handle, peer, flags, priv_flags,
				     produce_size, consume_size, page_store,
				     context, wakeup_cb, client_data, ent);
	} else {
		create = false;
		result =
		    qp_broker_attach(entry, peer, flags, priv_flags,
				     produce_size, consume_size, page_store,
				     context, wakeup_cb, client_data, ent);
	}

	mutex_unlock(&qp_broker_list.mutex);

	if (swap)
		*swap = (context_id == VMCI_HOST_CONTEXT_ID) &&
		    !(create && is_local);

	return result;
}

/*
 * This function implements the kernel API for allocating a queue
 * pair.
 */
static int qp_alloc_host_work(struct vmci_handle *handle,
			      struct vmci_queue **produce_q,
			      u64 produce_size,
			      struct vmci_queue **consume_q,
			      u64 consume_size,
			      u32 peer,
			      u32 flags,
			      u32 priv_flags,
			      vmci_event_release_cb wakeup_cb,
			      void *client_data)
{
	struct vmci_handle new_handle;
	struct vmci_ctx *context;
	struct qp_broker_entry *entry;
	int result;
	bool swap;

	if (vmci_handle_is_invalid(*handle)) {
		new_handle = vmci_make_handle(
			VMCI_HOST_CONTEXT_ID, VMCI_INVALID_ID);
	} else
		new_handle = *handle;

	context = vmci_ctx_get(VMCI_HOST_CONTEXT_ID);
	entry = NULL;
	result =
	    qp_broker_alloc(new_handle, peer, flags, priv_flags,
			    produce_size, consume_size, NULL, context,
			    wakeup_cb, client_data, &entry, &swap);
	if (result == VMCI_SUCCESS) {
		if (swap) {
			/*
			 * If this is a local queue pair, the attacher
			 * will swap around produce and consume
			 * queues.
			 */

			*produce_q = entry->consume_q;
			*consume_q = entry->produce_q;
		} else {
			*produce_q = entry->produce_q;
			*consume_q = entry->consume_q;
		}

		*handle = vmci_resource_handle(&entry->resource);
	} else {
		*handle = VMCI_INVALID_HANDLE;
		pr_devel("queue pair broker failed to alloc (result=%d)\n",
			 result);
	}
	vmci_ctx_put(context);
	return result;
}

/*
 * Allocates a VMCI queue_pair. Only checks validity of input
 * arguments. The real work is done in the host or guest
 * specific function.
 */
int vmci_qp_alloc(struct vmci_handle *handle,
		  struct vmci_queue **produce_q,
		  u64 produce_size,
		  struct vmci_queue **consume_q,
		  u64 consume_size,
		  u32 peer,
		  u32 flags,
		  u32 priv_flags,
		  bool guest_endpoint,
		  vmci_event_release_cb wakeup_cb,
		  void *client_data)
{
	if (!handle || !produce_q || !consume_q ||
	    (!produce_size && !consume_size) || (flags & ~VMCI_QP_ALL_FLAGS))
		return VMCI_ERROR_INVALID_ARGS;

	if (guest_endpoint) {
		return qp_alloc_guest_work(handle, produce_q,
					   produce_size, consume_q,
					   consume_size, peer,
					   flags, priv_flags);
	} else {
		return qp_alloc_host_work(handle, produce_q,
					  produce_size, consume_q,
					  consume_size, peer, flags,
					  priv_flags, wakeup_cb, client_data);
	}
}

/*
 * This function implements the host kernel API for detaching from
 * a queue pair.
 */
static int qp_detatch_host_work(struct vmci_handle handle)
{
	int result;
	struct vmci_ctx *context;

	context = vmci_ctx_get(VMCI_HOST_CONTEXT_ID);

	result = vmci_qp_broker_detach(handle, context);

	vmci_ctx_put(context);
	return result;
}

/*
 * Detaches from a VMCI queue_pair. Only checks validity of input argument.
 * Real work is done in the host or guest specific function.
 */
static int qp_detatch(struct vmci_handle handle, bool guest_endpoint)
{
	if (vmci_handle_is_invalid(handle))
		return VMCI_ERROR_INVALID_ARGS;

	if (guest_endpoint)
		return qp_detatch_guest_work(handle);
	else
		return qp_detatch_host_work(handle);
}

/*
 * Returns the entry from the head of the list. Assumes that the list is
 * locked.
 */
static struct qp_entry *qp_list_get_head(struct qp_list *qp_list)
{
	if (!list_empty(&qp_list->head)) {
		struct qp_entry *entry =
		    list_first_entry(&qp_list->head, struct qp_entry,
				     list_item);
		return entry;
	}

	return NULL;
}

void vmci_qp_broker_exit(void)
{
	struct qp_entry *entry;
	struct qp_broker_entry *be;

	mutex_lock(&qp_broker_list.mutex);

	while ((entry = qp_list_get_head(&qp_broker_list))) {
		be = (struct qp_broker_entry *)entry;

		qp_list_remove_entry(&qp_broker_list, entry);
		kfree(be);
	}

	mutex_unlock(&qp_broker_list.mutex);
}

/*
 * Requests that a queue pair be allocated with the VMCI queue
 * pair broker. Allocates a queue pair entry if one does not
 * exist. Attaches to one if it exists, and retrieves the page
 * files backing that queue_pair.  Assumes that the queue pair
 * broker lock is held.
 */
int vmci_qp_broker_alloc(struct vmci_handle handle,
			 u32 peer,
			 u32 flags,
			 u32 priv_flags,
			 u64 produce_size,
			 u64 consume_size,
			 struct vmci_qp_page_store *page_store,
			 struct vmci_ctx *context)
{
	return qp_broker_alloc(handle, peer, flags, priv_flags,
			       produce_size, consume_size,
			       page_store, context, NULL, NULL, NULL, NULL);
}

/*
 * VMX'en with versions lower than VMCI_VERSION_NOVMVM use a separate
 * step to add the UVAs of the VMX mapping of the queue pair. This function
 * provides backwards compatibility with such VMX'en, and takes care of
 * registering the page store for a queue pair previously allocated by the
 * VMX during create or attach. This function will move the queue pair state
 * to either from VMCIQBP_CREATED_NO_MEM to VMCIQBP_CREATED_MEM or
 * VMCIQBP_ATTACHED_NO_MEM to VMCIQBP_ATTACHED_MEM. If moving to the
 * attached state with memory, the queue pair is ready to be used by the
 * host peer, and an attached event will be generated.
 *
 * Assumes that the queue pair broker lock is held.
 *
 * This function is only used by the hosted platform, since there is no
 * issue with backwards compatibility for vmkernel.
 */
int vmci_qp_broker_set_page_store(struct vmci_handle handle,
				  u64 produce_uva,
				  u64 consume_uva,
				  struct vmci_ctx *context)
{
	struct qp_broker_entry *entry;
	int result;
	const u32 context_id = vmci_ctx_get_id(context);

	if (vmci_handle_is_invalid(handle) || !context ||
	    context_id == VMCI_INVALID_ID)
		return VMCI_ERROR_INVALID_ARGS;

	/*
	 * We only support guest to host queue pairs, so the VMX must
	 * supply UVAs for the mapped page files.
	 */

	if (produce_uva == 0 || consume_uva == 0)
		return VMCI_ERROR_INVALID_ARGS;

	mutex_lock(&qp_broker_list.mutex);

	if (!vmci_ctx_qp_exists(context, handle)) {
		pr_warn("Context (ID=0x%x) not attached to queue pair (handle=0x%x:0x%x)\n",
			context_id, handle.context, handle.resource);
		result = VMCI_ERROR_NOT_FOUND;
		goto out;
	}

	entry = qp_broker_handle_to_entry(handle);
	if (!entry) {
		result = VMCI_ERROR_NOT_FOUND;
		goto out;
	}

	/*
	 * If I'm the owner then I can set the page store.
	 *
	 * Or, if a host created the queue_pair and I'm the attached peer
	 * then I can set the page store.
	 */
	if (entry->create_id != context_id &&
	    (entry->create_id != VMCI_HOST_CONTEXT_ID ||
	     entry->attach_id != context_id)) {
		result = VMCI_ERROR_QUEUEPAIR_NOTOWNER;
		goto out;
	}

	if (entry->state != VMCIQPB_CREATED_NO_MEM &&
	    entry->state != VMCIQPB_ATTACHED_NO_MEM) {
		result = VMCI_ERROR_UNAVAILABLE;
		goto out;
	}

	result = qp_host_get_user_memory(produce_uva, consume_uva,
					 entry->produce_q, entry->consume_q);
	if (result < VMCI_SUCCESS)
		goto out;

	result = qp_host_map_queues(entry->produce_q, entry->consume_q);
	if (result < VMCI_SUCCESS) {
		qp_host_unregister_user_memory(entry->produce_q,
					       entry->consume_q);
		goto out;
	}

	if (entry->state == VMCIQPB_CREATED_NO_MEM)
		entry->state = VMCIQPB_CREATED_MEM;
	else
		entry->state = VMCIQPB_ATTACHED_MEM;

	entry->vmci_page_files = true;

	if (entry->state == VMCIQPB_ATTACHED_MEM) {
		result =
		    qp_notify_peer(true, handle, context_id, entry->create_id);
		if (result < VMCI_SUCCESS) {
			pr_warn("Failed to notify peer (ID=0x%x) of attach to queue pair (handle=0x%x:0x%x)\n",
				entry->create_id, entry->qp.handle.context,
				entry->qp.handle.resource);
		}
	}

	result = VMCI_SUCCESS;
 out:
	mutex_unlock(&qp_broker_list.mutex);
	return result;
}

/*
 * Resets saved queue headers for the given QP broker
 * entry. Should be used when guest memory becomes available
 * again, or the guest detaches.
 */
static void qp_reset_saved_headers(struct qp_broker_entry *entry)
{
	entry->produce_q->saved_header = NULL;
	entry->consume_q->saved_header = NULL;
}

/*
 * The main entry point for detaching from a queue pair registered with the
 * queue pair broker. If more than one endpoint is attached to the queue
 * pair, the first endpoint will mainly decrement a reference count and
 * generate a notification to its peer. The last endpoint will clean up
 * the queue pair state registered with the broker.
 *
 * When a guest endpoint detaches, it will unmap and unregister the guest
 * memory backing the queue pair. If the host is still attached, it will
 * no longer be able to access the queue pair content.
 *
 * If the queue pair is already in a state where there is no memory
 * registered for the queue pair (any *_NO_MEM state), it will transition to
 * the VMCIQPB_SHUTDOWN_NO_MEM state. This will also happen, if a guest
 * endpoint is the first of two endpoints to detach. If the host endpoint is
 * the first out of two to detach, the queue pair will move to the
 * VMCIQPB_SHUTDOWN_MEM state.
 */
int vmci_qp_broker_detach(struct vmci_handle handle, struct vmci_ctx *context)
{
	struct qp_broker_entry *entry;
	const u32 context_id = vmci_ctx_get_id(context);
	u32 peer_id;
	bool is_local = false;
	int result;

	if (vmci_handle_is_invalid(handle) || !context ||
	    context_id == VMCI_INVALID_ID) {
		return VMCI_ERROR_INVALID_ARGS;
	}

	mutex_lock(&qp_broker_list.mutex);

	if (!vmci_ctx_qp_exists(context, handle)) {
		pr_devel("Context (ID=0x%x) not attached to queue pair (handle=0x%x:0x%x)\n",
			 context_id, handle.context, handle.resource);
		result = VMCI_ERROR_NOT_FOUND;
		goto out;
	}

	entry = qp_broker_handle_to_entry(handle);
	if (!entry) {
		pr_devel("Context (ID=0x%x) reports being attached to queue pair(handle=0x%x:0x%x) that isn't present in broker\n",
			 context_id, handle.context, handle.resource);
		result = VMCI_ERROR_NOT_FOUND;
		goto out;
	}

	if (context_id != entry->create_id && context_id != entry->attach_id) {
		result = VMCI_ERROR_QUEUEPAIR_NOTATTACHED;
		goto out;
	}

	if (context_id == entry->create_id) {
		peer_id = entry->attach_id;
		entry->create_id = VMCI_INVALID_ID;
	} else {
		peer_id = entry->create_id;
		entry->attach_id = VMCI_INVALID_ID;
	}
	entry->qp.ref_count--;

	is_local = entry->qp.flags & VMCI_QPFLAG_LOCAL;

	if (context_id != VMCI_HOST_CONTEXT_ID) {
		bool headers_mapped;

		/*
		 * Pre NOVMVM vmx'en may detach from a queue pair
		 * before setting the page store, and in that case
		 * there is no user memory to detach from. Also, more
		 * recent VMX'en may detach from a queue pair in the
		 * quiesced state.
		 */

		qp_acquire_queue_mutex(entry->produce_q);
		headers_mapped = entry->produce_q->q_header ||
		    entry->consume_q->q_header;
		if (QPBROKERSTATE_HAS_MEM(entry)) {
			result =
			    qp_host_unmap_queues(INVALID_VMCI_GUEST_MEM_ID,
						 entry->produce_q,
						 entry->consume_q);
			if (result < VMCI_SUCCESS)
				pr_warn("Failed to unmap queue headers for queue pair (handle=0x%x:0x%x,result=%d)\n",
					handle.context, handle.resource,
					result);

			qp_host_unregister_user_memory(entry->produce_q,
						       entry->consume_q);

		}

		if (!headers_mapped)
			qp_reset_saved_headers(entry);

		qp_release_queue_mutex(entry->produce_q);

		if (!headers_mapped && entry->wakeup_cb)
			entry->wakeup_cb(entry->client_data);

	} else {
		if (entry->wakeup_cb) {
			entry->wakeup_cb = NULL;
			entry->client_data = NULL;
		}
	}

	if (entry->qp.ref_count == 0) {
		qp_list_remove_entry(&qp_broker_list, &entry->qp);

		if (is_local)
			kfree(entry->local_mem);

		qp_cleanup_queue_mutex(entry->produce_q, entry->consume_q);
		qp_host_free_queue(entry->produce_q, entry->qp.produce_size);
		qp_host_free_queue(entry->consume_q, entry->qp.consume_size);
		/* Unlink from resource hash table and free callback */
		vmci_resource_remove(&entry->resource);

		kfree(entry);

		vmci_ctx_qp_destroy(context, handle);
	} else {
		qp_notify_peer(false, handle, context_id, peer_id);
		if (context_id == VMCI_HOST_CONTEXT_ID &&
		    QPBROKERSTATE_HAS_MEM(entry)) {
			entry->state = VMCIQPB_SHUTDOWN_MEM;
		} else {
			entry->state = VMCIQPB_SHUTDOWN_NO_MEM;
		}

		if (!is_local)
			vmci_ctx_qp_destroy(context, handle);

	}
	result = VMCI_SUCCESS;
 out:
	mutex_unlock(&qp_broker_list.mutex);
	return result;
}

/*
 * Establishes the necessary mappings for a queue pair given a
 * reference to the queue pair guest memory. This is usually
 * called when a guest is unquiesced and the VMX is allowed to
 * map guest memory once again.
 */
int vmci_qp_broker_map(struct vmci_handle handle,
		       struct vmci_ctx *context,
		       u64 guest_mem)
{
	struct qp_broker_entry *entry;
	const u32 context_id = vmci_ctx_get_id(context);
	int result;

	if (vmci_handle_is_invalid(handle) || !context ||
	    context_id == VMCI_INVALID_ID)
		return VMCI_ERROR_INVALID_ARGS;

	mutex_lock(&qp_broker_list.mutex);

	if (!vmci_ctx_qp_exists(context, handle)) {
		pr_devel("Context (ID=0x%x) not attached to queue pair (handle=0x%x:0x%x)\n",
			 context_id, handle.context, handle.resource);
		result = VMCI_ERROR_NOT_FOUND;
		goto out;
	}

	entry = qp_broker_handle_to_entry(handle);
	if (!entry) {
		pr_devel("Context (ID=0x%x) reports being attached to queue pair (handle=0x%x:0x%x) that isn't present in broker\n",
			 context_id, handle.context, handle.resource);
		result = VMCI_ERROR_NOT_FOUND;
		goto out;
	}

	if (context_id != entry->create_id && context_id != entry->attach_id) {
		result = VMCI_ERROR_QUEUEPAIR_NOTATTACHED;
		goto out;
	}

	result = VMCI_SUCCESS;

	if (context_id != VMCI_HOST_CONTEXT_ID) {
		struct vmci_qp_page_store page_store;

		page_store.pages = guest_mem;
		page_store.len = QPE_NUM_PAGES(entry->qp);

		qp_acquire_queue_mutex(entry->produce_q);
		qp_reset_saved_headers(entry);
		result =
		    qp_host_register_user_memory(&page_store,
						 entry->produce_q,
						 entry->consume_q);
		qp_release_queue_mutex(entry->produce_q);
		if (result == VMCI_SUCCESS) {
			/* Move state from *_NO_MEM to *_MEM */

			entry->state++;

			if (entry->wakeup_cb)
				entry->wakeup_cb(entry->client_data);
		}
	}

 out:
	mutex_unlock(&qp_broker_list.mutex);
	return result;
}

/*
 * Saves a snapshot of the queue headers for the given QP broker
 * entry. Should be used when guest memory is unmapped.
 * Results:
 * VMCI_SUCCESS on success, appropriate error code if guest memory
 * can't be accessed..
 */
static int qp_save_headers(struct qp_broker_entry *entry)
{
	int result;

	if (entry->produce_q->saved_header != NULL &&
	    entry->consume_q->saved_header != NULL) {
		/*
		 *  If the headers have already been saved, we don't need to do
		 *  it again, and we don't want to map in the headers
		 *  unnecessarily.
		 */

		return VMCI_SUCCESS;
	}

	if (NULL == entry->produce_q->q_header ||
	    NULL == entry->consume_q->q_header) {
		result = qp_host_map_queues(entry->produce_q, entry->consume_q);
		if (result < VMCI_SUCCESS)
			return result;
	}

	memcpy(&entry->saved_produce_q, entry->produce_q->q_header,
	       sizeof(entry->saved_produce_q));
	entry->produce_q->saved_header = &entry->saved_produce_q;
	memcpy(&entry->saved_consume_q, entry->consume_q->q_header,
	       sizeof(entry->saved_consume_q));
	entry->consume_q->saved_header = &entry->saved_consume_q;

	return VMCI_SUCCESS;
}

/*
 * Removes all references to the guest memory of a given queue pair, and
 * will move the queue pair from state *_MEM to *_NO_MEM. It is usually
 * called when a VM is being quiesced where access to guest memory should
 * avoided.
 */
int vmci_qp_broker_unmap(struct vmci_handle handle,
			 struct vmci_ctx *context,
			 u32 gid)
{
	struct qp_broker_entry *entry;
	const u32 context_id = vmci_ctx_get_id(context);
	int result;

	if (vmci_handle_is_invalid(handle) || !context ||
	    context_id == VMCI_INVALID_ID)
		return VMCI_ERROR_INVALID_ARGS;

	mutex_lock(&qp_broker_list.mutex);

	if (!vmci_ctx_qp_exists(context, handle)) {
		pr_devel("Context (ID=0x%x) not attached to queue pair (handle=0x%x:0x%x)\n",
			 context_id, handle.context, handle.resource);
		result = VMCI_ERROR_NOT_FOUND;
		goto out;
	}

	entry = qp_broker_handle_to_entry(handle);
	if (!entry) {
		pr_devel("Context (ID=0x%x) reports being attached to queue pair (handle=0x%x:0x%x) that isn't present in broker\n",
			 context_id, handle.context, handle.resource);
		result = VMCI_ERROR_NOT_FOUND;
		goto out;
	}

	if (context_id != entry->create_id && context_id != entry->attach_id) {
		result = VMCI_ERROR_QUEUEPAIR_NOTATTACHED;
		goto out;
	}

	if (context_id != VMCI_HOST_CONTEXT_ID) {
		qp_acquire_queue_mutex(entry->produce_q);
		result = qp_save_headers(entry);
		if (result < VMCI_SUCCESS)
			pr_warn("Failed to save queue headers for queue pair (handle=0x%x:0x%x,result=%d)\n",
				handle.context, handle.resource, result);

		qp_host_unmap_queues(gid, entry->produce_q, entry->consume_q);

		/*
		 * On hosted, when we unmap queue pairs, the VMX will also
		 * unmap the guest memory, so we invalidate the previously
		 * registered memory. If the queue pair is mapped again at a
		 * later point in time, we will need to reregister the user
		 * memory with a possibly new user VA.
		 */
		qp_host_unregister_user_memory(entry->produce_q,
					       entry->consume_q);

		/*
		 * Move state from *_MEM to *_NO_MEM.
		 */
		entry->state--;

		qp_release_queue_mutex(entry->produce_q);
	}

	result = VMCI_SUCCESS;

 out:
	mutex_unlock(&qp_broker_list.mutex);
	return result;
}

/*
 * Destroys all guest queue pair endpoints. If active guest queue
 * pairs still exist, hypercalls to attempt detach from these
 * queue pairs will be made. Any failure to detach is silently
 * ignored.
 */
void vmci_qp_guest_endpoints_exit(void)
{
	struct qp_entry *entry;
	struct qp_guest_endpoint *ep;

	mutex_lock(&qp_guest_endpoints.mutex);

	while ((entry = qp_list_get_head(&qp_guest_endpoints))) {
		ep = (struct qp_guest_endpoint *)entry;

		/* Don't make a hypercall for local queue_pairs. */
		if (!(entry->flags & VMCI_QPFLAG_LOCAL))
			qp_detatch_hypercall(entry->handle);

		/* We cannot fail the exit, so let's reset ref_count. */
		entry->ref_count = 0;
		qp_list_remove_entry(&qp_guest_endpoints, entry);

		qp_guest_endpoint_destroy(ep);
	}

	mutex_unlock(&qp_guest_endpoints.mutex);
}

/*
 * Helper routine that will lock the queue pair before subsequent
 * operations.
 * Note: Non-blocking on the host side is currently only implemented in ESX.
 * Since non-blocking isn't yet implemented on the host personality we
 * have no reason to acquire a spin lock.  So to avoid the use of an
 * unnecessary lock only acquire the mutex if we can block.
 */
static void qp_lock(const struct vmci_qp *qpair)
{
	qp_acquire_queue_mutex(qpair->produce_q);
}

/*
 * Helper routine that unlocks the queue pair after calling
 * qp_lock.
 */
static void qp_unlock(const struct vmci_qp *qpair)
{
	qp_release_queue_mutex(qpair->produce_q);
}

/*
 * The queue headers may not be mapped at all times. If a queue is
 * currently not mapped, it will be attempted to do so.
 */
static int qp_map_queue_headers(struct vmci_queue *produce_q,
				struct vmci_queue *consume_q)
{
	int result;

	if (NULL == produce_q->q_header || NULL == consume_q->q_header) {
		result = qp_host_map_queues(produce_q, consume_q);
		if (result < VMCI_SUCCESS)
			return (produce_q->saved_header &&
				consume_q->saved_header) ?
			    VMCI_ERROR_QUEUEPAIR_NOT_READY :
			    VMCI_ERROR_QUEUEPAIR_NOTATTACHED;
	}

	return VMCI_SUCCESS;
}

/*
 * Helper routine that will retrieve the produce and consume
 * headers of a given queue pair. If the guest memory of the
 * queue pair is currently not available, the saved queue headers
 * will be returned, if these are available.
 */
static int qp_get_queue_headers(const struct vmci_qp *qpair,
				struct vmci_queue_header **produce_q_header,
				struct vmci_queue_header **consume_q_header)
{
	int result;

	result = qp_map_queue_headers(qpair->produce_q, qpair->consume_q);
	if (result == VMCI_SUCCESS) {
		*produce_q_header = qpair->produce_q->q_header;
		*consume_q_header = qpair->consume_q->q_header;
	} else if (qpair->produce_q->saved_header &&
		   qpair->consume_q->saved_header) {
		*produce_q_header = qpair->produce_q->saved_header;
		*consume_q_header = qpair->consume_q->saved_header;
		result = VMCI_SUCCESS;
	}

	return result;
}

/*
 * Callback from VMCI queue pair broker indicating that a queue
 * pair that was previously not ready, now either is ready or
 * gone forever.
 */
static int qp_wakeup_cb(void *client_data)
{
	struct vmci_qp *qpair = (struct vmci_qp *)client_data;

	qp_lock(qpair);
	while (qpair->blocked > 0) {
		qpair->blocked--;
		qpair->generation++;
		wake_up(&qpair->event);
	}
	qp_unlock(qpair);

	return VMCI_SUCCESS;
}

/*
 * Makes the calling thread wait for the queue pair to become
 * ready for host side access.  Returns true when thread is
 * woken up after queue pair state change, false otherwise.
 */
static bool qp_wait_for_ready_queue(struct vmci_qp *qpair)
{
	unsigned int generation;

	qpair->blocked++;
	generation = qpair->generation;
	qp_unlock(qpair);
	wait_event(qpair->event, generation != qpair->generation);
	qp_lock(qpair);

	return true;
}

/*
 * Enqueues a given buffer to the produce queue using the provided
 * function. As many bytes as possible (space available in the queue)
 * are enqueued.  Assumes the queue->mutex has been acquired.  Returns
 * VMCI_ERROR_QUEUEPAIR_NOSPACE if no space was available to enqueue
 * data, VMCI_ERROR_INVALID_SIZE, if any queue pointer is outside the
 * queue (as defined by the queue size), VMCI_ERROR_INVALID_ARGS, if
 * an error occured when accessing the buffer,
 * VMCI_ERROR_QUEUEPAIR_NOTATTACHED, if the queue pair pages aren't
 * available.  Otherwise, the number of bytes written to the queue is
 * returned.  Updates the tail pointer of the produce queue.
 */
static ssize_t qp_enqueue_locked(struct vmci_queue *produce_q,
				 struct vmci_queue *consume_q,
				 const u64 produce_q_size,
				 struct iov_iter *from)
{
	s64 free_space;
	u64 tail;
	size_t buf_size = iov_iter_count(from);
	size_t written;
	ssize_t result;

	result = qp_map_queue_headers(produce_q, consume_q);
	if (unlikely(result != VMCI_SUCCESS))
		return result;

	free_space = vmci_q_header_free_space(produce_q->q_header,
					      consume_q->q_header,
					      produce_q_size);
	if (free_space == 0)
		return VMCI_ERROR_QUEUEPAIR_NOSPACE;

	if (free_space < VMCI_SUCCESS)
		return (ssize_t) free_space;

	written = (size_t) (free_space > buf_size ? buf_size : free_space);
	tail = vmci_q_header_producer_tail(produce_q->q_header);
	if (likely(tail + written < produce_q_size)) {
		result = qp_memcpy_to_queue_iter(produce_q, tail, from, written);
	} else {
		/* Tail pointer wraps around. */

		const size_t tmp = (size_t) (produce_q_size - tail);

		result = qp_memcpy_to_queue_iter(produce_q, tail, from, tmp);
		if (result >= VMCI_SUCCESS)
			result = qp_memcpy_to_queue_iter(produce_q, 0, from,
						 written - tmp);
	}

	if (result < VMCI_SUCCESS)
		return result;

	vmci_q_header_add_producer_tail(produce_q->q_header, written,
					produce_q_size);
	return written;
}

/*
 * Dequeues data (if available) from the given consume queue. Writes data
 * to the user provided buffer using the provided function.
 * Assumes the queue->mutex has been acquired.
 * Results:
 * VMCI_ERROR_QUEUEPAIR_NODATA if no data was available to dequeue.
 * VMCI_ERROR_INVALID_SIZE, if any queue pointer is outside the queue
 * (as defined by the queue size).
 * VMCI_ERROR_INVALID_ARGS, if an error occured when accessing the buffer.
 * Otherwise the number of bytes dequeued is returned.
 * Side effects:
 * Updates the head pointer of the consume queue.
 */
static ssize_t qp_dequeue_locked(struct vmci_queue *produce_q,
				 struct vmci_queue *consume_q,
				 const u64 consume_q_size,
				 struct iov_iter *to,
				 bool update_consumer)
{
	size_t buf_size = iov_iter_count(to);
	s64 buf_ready;
	u64 head;
	size_t read;
	ssize_t result;

	result = qp_map_queue_headers(produce_q, consume_q);
	if (unlikely(result != VMCI_SUCCESS))
		return result;

	buf_ready = vmci_q_header_buf_ready(consume_q->q_header,
					    produce_q->q_header,
					    consume_q_size);
	if (buf_ready == 0)
		return VMCI_ERROR_QUEUEPAIR_NODATA;

	if (buf_ready < VMCI_SUCCESS)
		return (ssize_t) buf_ready;

	read = (size_t) (buf_ready > buf_size ? buf_size : buf_ready);
	head = vmci_q_header_consumer_head(produce_q->q_header);
	if (likely(head + read < consume_q_size)) {
		result = qp_memcpy_from_queue_iter(to, consume_q, head, read);
	} else {
		/* Head pointer wraps around. */

		const size_t tmp = (size_t) (consume_q_size - head);

		result = qp_memcpy_from_queue_iter(to, consume_q, head, tmp);
		if (result >= VMCI_SUCCESS)
			result = qp_memcpy_from_queue_iter(to, consume_q, 0,
						   read - tmp);

	}

	if (result < VMCI_SUCCESS)
		return result;

	if (update_consumer)
		vmci_q_header_add_consumer_head(produce_q->q_header,
						read, consume_q_size);

	return read;
}

/*
 * vmci_qpair_alloc() - Allocates a queue pair.
 * @qpair:      Pointer for the new vmci_qp struct.
 * @handle:     Handle to track the resource.
 * @produce_qsize:      Desired size of the producer queue.
 * @consume_qsize:      Desired size of the consumer queue.
 * @peer:       ContextID of the peer.
 * @flags:      VMCI flags.
 * @priv_flags: VMCI priviledge flags.
 *
 * This is the client interface for allocating the memory for a
 * vmci_qp structure and then attaching to the underlying
 * queue.  If an error occurs allocating the memory for the
 * vmci_qp structure no attempt is made to attach.  If an
 * error occurs attaching, then the structure is freed.
 */
int vmci_qpair_alloc(struct vmci_qp **qpair,
		     struct vmci_handle *handle,
		     u64 produce_qsize,
		     u64 consume_qsize,
		     u32 peer,
		     u32 flags,
		     u32 priv_flags)
{
	struct vmci_qp *my_qpair;
	int retval;
	struct vmci_handle src = VMCI_INVALID_HANDLE;
	struct vmci_handle dst = vmci_make_handle(peer, VMCI_INVALID_ID);
	enum vmci_route route;
	vmci_event_release_cb wakeup_cb;
	void *client_data;

	/*
	 * Restrict the size of a queuepair.  The device already
	 * enforces a limit on the total amount of memory that can be
	 * allocated to queuepairs for a guest.  However, we try to
	 * allocate this memory before we make the queuepair
	 * allocation hypercall.  On Linux, we allocate each page
	 * separately, which means rather than fail, the guest will
	 * thrash while it tries to allocate, and will become
	 * increasingly unresponsive to the point where it appears to
	 * be hung.  So we place a limit on the size of an individual
	 * queuepair here, and leave the device to enforce the
	 * restriction on total queuepair memory.  (Note that this
	 * doesn't prevent all cases; a user with only this much
	 * physical memory could still get into trouble.)  The error
	 * used by the device is NO_RESOURCES, so use that here too.
	 */

	if (produce_qsize + consume_qsize < max(produce_qsize, consume_qsize) ||
	    produce_qsize + consume_qsize > VMCI_MAX_GUEST_QP_MEMORY)
		return VMCI_ERROR_NO_RESOURCES;

	retval = vmci_route(&src, &dst, false, &route);
	if (retval < VMCI_SUCCESS)
		route = vmci_guest_code_active() ?
		    VMCI_ROUTE_AS_GUEST : VMCI_ROUTE_AS_HOST;

	if (flags & (VMCI_QPFLAG_NONBLOCK | VMCI_QPFLAG_PINNED)) {
		pr_devel("NONBLOCK OR PINNED set");
		return VMCI_ERROR_INVALID_ARGS;
	}

	my_qpair = kzalloc(sizeof(*my_qpair), GFP_KERNEL);
	if (!my_qpair)
		return VMCI_ERROR_NO_MEM;

	my_qpair->produce_q_size = produce_qsize;
	my_qpair->consume_q_size = consume_qsize;
	my_qpair->peer = peer;
	my_qpair->flags = flags;
	my_qpair->priv_flags = priv_flags;

	wakeup_cb = NULL;
	client_data = NULL;

	if (VMCI_ROUTE_AS_HOST == route) {
		my_qpair->guest_endpoint = false;
		if (!(flags & VMCI_QPFLAG_LOCAL)) {
			my_qpair->blocked = 0;
			my_qpair->generation = 0;
			init_waitqueue_head(&my_qpair->event);
			wakeup_cb = qp_wakeup_cb;
			client_data = (void *)my_qpair;
		}
	} else {
		my_qpair->guest_endpoint = true;
	}

	retval = vmci_qp_alloc(handle,
			       &my_qpair->produce_q,
			       my_qpair->produce_q_size,
			       &my_qpair->consume_q,
			       my_qpair->consume_q_size,
			       my_qpair->peer,
			       my_qpair->flags,
			       my_qpair->priv_flags,
			       my_qpair->guest_endpoint,
			       wakeup_cb, client_data);

	if (retval < VMCI_SUCCESS) {
		kfree(my_qpair);
		return retval;
	}

	*qpair = my_qpair;
	my_qpair->handle = *handle;

	return retval;
}
EXPORT_SYMBOL_GPL(vmci_qpair_alloc);

/*
 * vmci_qpair_detach() - Detatches the client from a queue pair.
 * @qpair:      Reference of a pointer to the qpair struct.
 *
 * This is the client interface for detaching from a VMCIQPair.
 * Note that this routine will free the memory allocated for the
 * vmci_qp structure too.
 */
int vmci_qpair_detach(struct vmci_qp **qpair)
{
	int result;
	struct vmci_qp *old_qpair;

	if (!qpair || !(*qpair))
		return VMCI_ERROR_INVALID_ARGS;

	old_qpair = *qpair;
	result = qp_detatch(old_qpair->handle, old_qpair->guest_endpoint);

	/*
	 * The guest can fail to detach for a number of reasons, and
	 * if it does so, it will cleanup the entry (if there is one).
	 * The host can fail too, but it won't cleanup the entry
	 * immediately, it will do that later when the context is
	 * freed.  Either way, we need to release the qpair struct
	 * here; there isn't much the caller can do, and we don't want
	 * to leak.
	 */

	memset(old_qpair, 0, sizeof(*old_qpair));
	old_qpair->handle = VMCI_INVALID_HANDLE;
	old_qpair->peer = VMCI_INVALID_ID;
	kfree(old_qpair);
	*qpair = NULL;

	return result;
}
EXPORT_SYMBOL_GPL(vmci_qpair_detach);

/*
 * vmci_qpair_get_produce_indexes() - Retrieves the indexes of the producer.
 * @qpair:      Pointer to the queue pair struct.
 * @producer_tail:      Reference used for storing producer tail index.
 * @consumer_head:      Reference used for storing the consumer head index.
 *
 * This is the client interface for getting the current indexes of the
 * QPair from the point of the view of the caller as the producer.
 */
int vmci_qpair_get_produce_indexes(const struct vmci_qp *qpair,
				   u64 *producer_tail,
				   u64 *consumer_head)
{
	struct vmci_queue_header *produce_q_header;
	struct vmci_queue_header *consume_q_header;
	int result;

	if (!qpair)
		return VMCI_ERROR_INVALID_ARGS;

	qp_lock(qpair);
	result =
	    qp_get_queue_headers(qpair, &produce_q_header, &consume_q_header);
	if (result == VMCI_SUCCESS)
		vmci_q_header_get_pointers(produce_q_header, consume_q_header,
					   producer_tail, consumer_head);
	qp_unlock(qpair);

	if (result == VMCI_SUCCESS &&
	    ((producer_tail && *producer_tail >= qpair->produce_q_size) ||
	     (consumer_head && *consumer_head >= qpair->produce_q_size)))
		return VMCI_ERROR_INVALID_SIZE;

	return result;
}
EXPORT_SYMBOL_GPL(vmci_qpair_get_produce_indexes);

/*
 * vmci_qpair_get_consume_indexes() - Retrieves the indexes of the consumer.
 * @qpair:      Pointer to the queue pair struct.
 * @consumer_tail:      Reference used for storing consumer tail index.
 * @producer_head:      Reference used for storing the producer head index.
 *
 * This is the client interface for getting the current indexes of the
 * QPair from the point of the view of the caller as the consumer.
 */
int vmci_qpair_get_consume_indexes(const struct vmci_qp *qpair,
				   u64 *consumer_tail,
				   u64 *producer_head)
{
	struct vmci_queue_header *produce_q_header;
	struct vmci_queue_header *consume_q_header;
	int result;

	if (!qpair)
		return VMCI_ERROR_INVALID_ARGS;

	qp_lock(qpair);
	result =
	    qp_get_queue_headers(qpair, &produce_q_header, &consume_q_header);
	if (result == VMCI_SUCCESS)
		vmci_q_header_get_pointers(consume_q_header, produce_q_header,
					   consumer_tail, producer_head);
	qp_unlock(qpair);

	if (result == VMCI_SUCCESS &&
	    ((consumer_tail && *consumer_tail >= qpair->consume_q_size) ||
	     (producer_head && *producer_head >= qpair->consume_q_size)))
		return VMCI_ERROR_INVALID_SIZE;

	return result;
}
EXPORT_SYMBOL_GPL(vmci_qpair_get_consume_indexes);

/*
 * vmci_qpair_produce_free_space() - Retrieves free space in producer queue.
 * @qpair:      Pointer to the queue pair struct.
 *
 * This is the client interface for getting the amount of free
 * space in the QPair from the point of the view of the caller as
 * the producer which is the common case.  Returns < 0 if err, else
 * available bytes into which data can be enqueued if > 0.
 */
s64 vmci_qpair_produce_free_space(const struct vmci_qp *qpair)
{
	struct vmci_queue_header *produce_q_header;
	struct vmci_queue_header *consume_q_header;
	s64 result;

	if (!qpair)
		return VMCI_ERROR_INVALID_ARGS;

	qp_lock(qpair);
	result =
	    qp_get_queue_headers(qpair, &produce_q_header, &consume_q_header);
	if (result == VMCI_SUCCESS)
		result = vmci_q_header_free_space(produce_q_header,
						  consume_q_header,
						  qpair->produce_q_size);
	else
		result = 0;

	qp_unlock(qpair);

	return result;
}
EXPORT_SYMBOL_GPL(vmci_qpair_produce_free_space);

/*
 * vmci_qpair_consume_free_space() - Retrieves free space in consumer queue.
 * @qpair:      Pointer to the queue pair struct.
 *
 * This is the client interface for getting the amount of free
 * space in the QPair from the point of the view of the caller as
 * the consumer which is not the common case.  Returns < 0 if err, else
 * available bytes into which data can be enqueued if > 0.
 */
s64 vmci_qpair_consume_free_space(const struct vmci_qp *qpair)
{
	struct vmci_queue_header *produce_q_header;
	struct vmci_queue_header *consume_q_header;
	s64 result;

	if (!qpair)
		return VMCI_ERROR_INVALID_ARGS;

	qp_lock(qpair);
	result =
	    qp_get_queue_headers(qpair, &produce_q_header, &consume_q_header);
	if (result == VMCI_SUCCESS)
		result = vmci_q_header_free_space(consume_q_header,
						  produce_q_header,
						  qpair->consume_q_size);
	else
		result = 0;

	qp_unlock(qpair);

	return result;
}
EXPORT_SYMBOL_GPL(vmci_qpair_consume_free_space);

/*
 * vmci_qpair_produce_buf_ready() - Gets bytes ready to read from
 * producer queue.
 * @qpair:      Pointer to the queue pair struct.
 *
 * This is the client interface for getting the amount of
 * enqueued data in the QPair from the point of the view of the
 * caller as the producer which is not the common case.  Returns < 0 if err,
 * else available bytes that may be read.
 */
s64 vmci_qpair_produce_buf_ready(const struct vmci_qp *qpair)
{
	struct vmci_queue_header *produce_q_header;
	struct vmci_queue_header *consume_q_header;
	s64 result;

	if (!qpair)
		return VMCI_ERROR_INVALID_ARGS;

	qp_lock(qpair);
	result =
	    qp_get_queue_headers(qpair, &produce_q_header, &consume_q_header);
	if (result == VMCI_SUCCESS)
		result = vmci_q_header_buf_ready(produce_q_header,
						 consume_q_header,
						 qpair->produce_q_size);
	else
		result = 0;

	qp_unlock(qpair);

	return result;
}
EXPORT_SYMBOL_GPL(vmci_qpair_produce_buf_ready);

/*
 * vmci_qpair_consume_buf_ready() - Gets bytes ready to read from
 * consumer queue.
 * @qpair:      Pointer to the queue pair struct.
 *
 * This is the client interface for getting the amount of
 * enqueued data in the QPair from the point of the view of the
 * caller as the consumer which is the normal case.  Returns < 0 if err,
 * else available bytes that may be read.
 */
s64 vmci_qpair_consume_buf_ready(const struct vmci_qp *qpair)
{
	struct vmci_queue_header *produce_q_header;
	struct vmci_queue_header *consume_q_header;
	s64 result;

	if (!qpair)
		return VMCI_ERROR_INVALID_ARGS;

	qp_lock(qpair);
	result =
	    qp_get_queue_headers(qpair, &produce_q_header, &consume_q_header);
	if (result == VMCI_SUCCESS)
		result = vmci_q_header_buf_ready(consume_q_header,
						 produce_q_header,
						 qpair->consume_q_size);
	else
		result = 0;

	qp_unlock(qpair);

	return result;
}
EXPORT_SYMBOL_GPL(vmci_qpair_consume_buf_ready);

/*
 * vmci_qpair_enqueue() - Throw data on the queue.
 * @qpair:      Pointer to the queue pair struct.
 * @buf:        Pointer to buffer containing data
 * @buf_size:   Length of buffer.
 * @buf_type:   Buffer type (Unused).
 *
 * This is the client interface for enqueueing data into the queue.
 * Returns number of bytes enqueued or < 0 on error.
 */
ssize_t vmci_qpair_enqueue(struct vmci_qp *qpair,
			   const void *buf,
			   size_t buf_size,
			   int buf_type)
{
	ssize_t result;
	struct iov_iter from;
	struct kvec v = {.iov_base = (void *)buf, .iov_len = buf_size};

	if (!qpair || !buf)
		return VMCI_ERROR_INVALID_ARGS;

	iov_iter_kvec(&from, WRITE, &v, 1, buf_size);

	qp_lock(qpair);

	do {
		result = qp_enqueue_locked(qpair->produce_q,
					   qpair->consume_q,
					   qpair->produce_q_size,
					   &from);

		if (result == VMCI_ERROR_QUEUEPAIR_NOT_READY &&
		    !qp_wait_for_ready_queue(qpair))
			result = VMCI_ERROR_WOULD_BLOCK;

	} while (result == VMCI_ERROR_QUEUEPAIR_NOT_READY);

	qp_unlock(qpair);

	return result;
}
EXPORT_SYMBOL_GPL(vmci_qpair_enqueue);

/*
 * vmci_qpair_dequeue() - Get data from the queue.
 * @qpair:      Pointer to the queue pair struct.
 * @buf:        Pointer to buffer for the data
 * @buf_size:   Length of buffer.
 * @buf_type:   Buffer type (Unused).
 *
 * This is the client interface for dequeueing data from the queue.
 * Returns number of bytes dequeued or < 0 on error.
 */
ssize_t vmci_qpair_dequeue(struct vmci_qp *qpair,
			   void *buf,
			   size_t buf_size,
			   int buf_type)
{
	ssize_t result;
	struct iov_iter to;
	struct kvec v = {.iov_base = buf, .iov_len = buf_size};

	if (!qpair || !buf)
		return VMCI_ERROR_INVALID_ARGS;

	iov_iter_kvec(&to, READ, &v, 1, buf_size);

	qp_lock(qpair);

	do {
		result = qp_dequeue_locked(qpair->produce_q,
					   qpair->consume_q,
					   qpair->consume_q_size,
					   &to, true);

		if (result == VMCI_ERROR_QUEUEPAIR_NOT_READY &&
		    !qp_wait_for_ready_queue(qpair))
			result = VMCI_ERROR_WOULD_BLOCK;

	} while (result == VMCI_ERROR_QUEUEPAIR_NOT_READY);

	qp_unlock(qpair);

	return result;
}
EXPORT_SYMBOL_GPL(vmci_qpair_dequeue);

/*
 * vmci_qpair_peek() - Peek at the data in the queue.
 * @qpair:      Pointer to the queue pair struct.
 * @buf:        Pointer to buffer for the data
 * @buf_size:   Length of buffer.
 * @buf_type:   Buffer type (Unused on Linux).
 *
 * This is the client interface for peeking into a queue.  (I.e.,
 * copy data from the queue without updating the head pointer.)
 * Returns number of bytes dequeued or < 0 on error.
 */
ssize_t vmci_qpair_peek(struct vmci_qp *qpair,
			void *buf,
			size_t buf_size,
			int buf_type)
{
	struct iov_iter to;
	struct kvec v = {.iov_base = buf, .iov_len = buf_size};
	ssize_t result;

	if (!qpair || !buf)
		return VMCI_ERROR_INVALID_ARGS;

	iov_iter_kvec(&to, READ, &v, 1, buf_size);

	qp_lock(qpair);

	do {
		result = qp_dequeue_locked(qpair->produce_q,
					   qpair->consume_q,
					   qpair->consume_q_size,
					   &to, false);

		if (result == VMCI_ERROR_QUEUEPAIR_NOT_READY &&
		    !qp_wait_for_ready_queue(qpair))
			result = VMCI_ERROR_WOULD_BLOCK;

	} while (result == VMCI_ERROR_QUEUEPAIR_NOT_READY);

	qp_unlock(qpair);

	return result;
}
EXPORT_SYMBOL_GPL(vmci_qpair_peek);

/*
 * vmci_qpair_enquev() - Throw data on the queue using iov.
 * @qpair:      Pointer to the queue pair struct.
 * @iov:        Pointer to buffer containing data
 * @iov_size:   Length of buffer.
 * @buf_type:   Buffer type (Unused).
 *
 * This is the client interface for enqueueing data into the queue.
 * This function uses IO vectors to handle the work. Returns number
 * of bytes enqueued or < 0 on error.
 */
ssize_t vmci_qpair_enquev(struct vmci_qp *qpair,
			  struct msghdr *msg,
			  size_t iov_size,
			  int buf_type)
{
	ssize_t result;

	if (!qpair)
		return VMCI_ERROR_INVALID_ARGS;

	qp_lock(qpair);

	do {
		result = qp_enqueue_locked(qpair->produce_q,
					   qpair->consume_q,
					   qpair->produce_q_size,
					   &msg->msg_iter);

		if (result == VMCI_ERROR_QUEUEPAIR_NOT_READY &&
		    !qp_wait_for_ready_queue(qpair))
			result = VMCI_ERROR_WOULD_BLOCK;

	} while (result == VMCI_ERROR_QUEUEPAIR_NOT_READY);

	qp_unlock(qpair);

	return result;
}
EXPORT_SYMBOL_GPL(vmci_qpair_enquev);

/*
 * vmci_qpair_dequev() - Get data from the queue using iov.
 * @qpair:      Pointer to the queue pair struct.
 * @iov:        Pointer to buffer for the data
 * @iov_size:   Length of buffer.
 * @buf_type:   Buffer type (Unused).
 *
 * This is the client interface for dequeueing data from the queue.
 * This function uses IO vectors to handle the work. Returns number
 * of bytes dequeued or < 0 on error.
 */
ssize_t vmci_qpair_dequev(struct vmci_qp *qpair,
			  struct msghdr *msg,
			  size_t iov_size,
			  int buf_type)
{
	ssize_t result;

	if (!qpair)
		return VMCI_ERROR_INVALID_ARGS;

	qp_lock(qpair);

	do {
		result = qp_dequeue_locked(qpair->produce_q,
					   qpair->consume_q,
					   qpair->consume_q_size,
					   &msg->msg_iter, true);

		if (result == VMCI_ERROR_QUEUEPAIR_NOT_READY &&
		    !qp_wait_for_ready_queue(qpair))
			result = VMCI_ERROR_WOULD_BLOCK;

	} while (result == VMCI_ERROR_QUEUEPAIR_NOT_READY);

	qp_unlock(qpair);

	return result;
}
EXPORT_SYMBOL_GPL(vmci_qpair_dequev);

/*
 * vmci_qpair_peekv() - Peek at the data in the queue using iov.
 * @qpair:      Pointer to the queue pair struct.
 * @iov:        Pointer to buffer for the data
 * @iov_size:   Length of buffer.
 * @buf_type:   Buffer type (Unused on Linux).
 *
 * This is the client interface for peeking into a queue.  (I.e.,
 * copy data from the queue without updating the head pointer.)
 * This function uses IO vectors to handle the work. Returns number
 * of bytes peeked or < 0 on error.
 */
ssize_t vmci_qpair_peekv(struct vmci_qp *qpair,
			 struct msghdr *msg,
			 size_t iov_size,
			 int buf_type)
{
	ssize_t result;

	if (!qpair)
		return VMCI_ERROR_INVALID_ARGS;

	qp_lock(qpair);

	do {
		result = qp_dequeue_locked(qpair->produce_q,
					   qpair->consume_q,
					   qpair->consume_q_size,
					   &msg->msg_iter, false);

		if (result == VMCI_ERROR_QUEUEPAIR_NOT_READY &&
		    !qp_wait_for_ready_queue(qpair))
			result = VMCI_ERROR_WOULD_BLOCK;

	} while (result == VMCI_ERROR_QUEUEPAIR_NOT_READY);

	qp_unlock(qpair);
	return result;
}
EXPORT_SYMBOL_GPL(vmci_qpair_peekv);
