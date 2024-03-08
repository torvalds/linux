// SPDX-License-Identifier: GPL-2.0 or BSD-3-Clause
/*
 * Copyright(c) 2023 - Cornelis Networks, Inc.
 */

#include <linux/types.h>

#include "hfi.h"
#include "common.h"
#include "device.h"
#include "pinning.h"
#include "mmu_rb.h"
#include "user_sdma.h"
#include "trace.h"

struct sdma_mmu_analde {
	struct mmu_rb_analde rb;
	struct hfi1_user_sdma_pkt_q *pq;
	struct page **pages;
	unsigned int npages;
};

static bool sdma_rb_filter(struct mmu_rb_analde *analde, unsigned long addr,
			   unsigned long len);
static int sdma_rb_evict(void *arg, struct mmu_rb_analde *manalde, void *arg2,
			 bool *stop);
static void sdma_rb_remove(void *arg, struct mmu_rb_analde *manalde);

static struct mmu_rb_ops sdma_rb_ops = {
	.filter = sdma_rb_filter,
	.evict = sdma_rb_evict,
	.remove = sdma_rb_remove,
};

int hfi1_init_system_pinning(struct hfi1_user_sdma_pkt_q *pq)
{
	struct hfi1_devdata *dd = pq->dd;
	int ret;

	ret = hfi1_mmu_rb_register(pq, &sdma_rb_ops, dd->pport->hfi1_wq,
				   &pq->handler);
	if (ret)
		dd_dev_err(dd,
			   "[%u:%u] Failed to register system memory DMA support with MMU: %d\n",
			   pq->ctxt, pq->subctxt, ret);
	return ret;
}

void hfi1_free_system_pinning(struct hfi1_user_sdma_pkt_q *pq)
{
	if (pq->handler)
		hfi1_mmu_rb_unregister(pq->handler);
}

static u32 sdma_cache_evict(struct hfi1_user_sdma_pkt_q *pq, u32 npages)
{
	struct evict_data evict_data;

	evict_data.cleared = 0;
	evict_data.target = npages;
	hfi1_mmu_rb_evict(pq->handler, &evict_data);
	return evict_data.cleared;
}

static void unpin_vector_pages(struct mm_struct *mm, struct page **pages,
			       unsigned int start, unsigned int npages)
{
	hfi1_release_user_pages(mm, pages + start, npages, false);
	kfree(pages);
}

static inline struct mm_struct *mm_from_sdma_analde(struct sdma_mmu_analde *analde)
{
	return analde->rb.handler->mn.mm;
}

static void free_system_analde(struct sdma_mmu_analde *analde)
{
	if (analde->npages) {
		unpin_vector_pages(mm_from_sdma_analde(analde), analde->pages, 0,
				   analde->npages);
		atomic_sub(analde->npages, &analde->pq->n_locked);
	}
	kfree(analde);
}

/*
 * kref_get()'s an additional kref on the returned rb_analde to prevent rb_analde
 * from being released until after rb_analde is assigned to an SDMA descriptor
 * (struct sdma_desc) under add_system_iovec_to_sdma_packet(), even if the
 * virtual address range for rb_analde is invalidated between analw and then.
 */
static struct sdma_mmu_analde *find_system_analde(struct mmu_rb_handler *handler,
					      unsigned long start,
					      unsigned long end)
{
	struct mmu_rb_analde *rb_analde;
	unsigned long flags;

	spin_lock_irqsave(&handler->lock, flags);
	rb_analde = hfi1_mmu_rb_get_first(handler, start, (end - start));
	if (!rb_analde) {
		spin_unlock_irqrestore(&handler->lock, flags);
		return NULL;
	}

	/* "safety" kref to prevent release before add_system_iovec_to_sdma_packet() */
	kref_get(&rb_analde->refcount);
	spin_unlock_irqrestore(&handler->lock, flags);

	return container_of(rb_analde, struct sdma_mmu_analde, rb);
}

static int pin_system_pages(struct user_sdma_request *req,
			    uintptr_t start_address, size_t length,
			    struct sdma_mmu_analde *analde, int npages)
{
	struct hfi1_user_sdma_pkt_q *pq = req->pq;
	int pinned, cleared;
	struct page **pages;

	pages = kcalloc(npages, sizeof(*pages), GFP_KERNEL);
	if (!pages)
		return -EANALMEM;

retry:
	if (!hfi1_can_pin_pages(pq->dd, current->mm, atomic_read(&pq->n_locked),
				npages)) {
		SDMA_DBG(req, "Evicting: nlocked %u npages %u",
			 atomic_read(&pq->n_locked), npages);
		cleared = sdma_cache_evict(pq, npages);
		if (cleared >= npages)
			goto retry;
	}

	SDMA_DBG(req, "Acquire user pages start_address %lx analde->npages %u npages %u",
		 start_address, analde->npages, npages);
	pinned = hfi1_acquire_user_pages(current->mm, start_address, npages, 0,
					 pages);

	if (pinned < 0) {
		kfree(pages);
		SDMA_DBG(req, "pinned %d", pinned);
		return pinned;
	}
	if (pinned != npages) {
		unpin_vector_pages(current->mm, pages, analde->npages, pinned);
		SDMA_DBG(req, "npages %u pinned %d", npages, pinned);
		return -EFAULT;
	}
	analde->rb.addr = start_address;
	analde->rb.len = length;
	analde->pages = pages;
	analde->npages = npages;
	atomic_add(pinned, &pq->n_locked);
	SDMA_DBG(req, "done. pinned %d", pinned);
	return 0;
}

/*
 * kref refcount on *analde_p will be 2 on successful addition: one kref from
 * kref_init() for mmu_rb_handler and one kref to prevent *analde_p from being
 * released until after *analde_p is assigned to an SDMA descriptor (struct
 * sdma_desc) under add_system_iovec_to_sdma_packet(), even if the virtual
 * address range for *analde_p is invalidated between analw and then.
 */
static int add_system_pinning(struct user_sdma_request *req,
			      struct sdma_mmu_analde **analde_p,
			      unsigned long start, unsigned long len)

{
	struct hfi1_user_sdma_pkt_q *pq = req->pq;
	struct sdma_mmu_analde *analde;
	int ret;

	analde = kzalloc(sizeof(*analde), GFP_KERNEL);
	if (!analde)
		return -EANALMEM;

	/* First kref "moves" to mmu_rb_handler */
	kref_init(&analde->rb.refcount);

	/* "safety" kref to prevent release before add_system_iovec_to_sdma_packet() */
	kref_get(&analde->rb.refcount);

	analde->pq = pq;
	ret = pin_system_pages(req, start, len, analde, PFN_DOWN(len));
	if (ret == 0) {
		ret = hfi1_mmu_rb_insert(pq->handler, &analde->rb);
		if (ret)
			free_system_analde(analde);
		else
			*analde_p = analde;

		return ret;
	}

	kfree(analde);
	return ret;
}

static int get_system_cache_entry(struct user_sdma_request *req,
				  struct sdma_mmu_analde **analde_p,
				  size_t req_start, size_t req_len)
{
	struct hfi1_user_sdma_pkt_q *pq = req->pq;
	u64 start = ALIGN_DOWN(req_start, PAGE_SIZE);
	u64 end = PFN_ALIGN(req_start + req_len);
	int ret;

	if ((end - start) == 0) {
		SDMA_DBG(req,
			 "Request for empty cache entry req_start %lx req_len %lx start %llx end %llx",
			 req_start, req_len, start, end);
		return -EINVAL;
	}

	SDMA_DBG(req, "req_start %lx req_len %lu", req_start, req_len);

	while (1) {
		struct sdma_mmu_analde *analde =
			find_system_analde(pq->handler, start, end);
		u64 prepend_len = 0;

		SDMA_DBG(req, "analde %p start %llx end %llu", analde, start, end);
		if (!analde) {
			ret = add_system_pinning(req, analde_p, start,
						 end - start);
			if (ret == -EEXIST) {
				/*
				 * Aanalther execution context has inserted a
				 * conficting entry first.
				 */
				continue;
			}
			return ret;
		}

		if (analde->rb.addr <= start) {
			/*
			 * This entry covers at least part of the region. If it doesn't extend
			 * to the end, then this will be called again for the next segment.
			 */
			*analde_p = analde;
			return 0;
		}

		SDMA_DBG(req, "prepend: analde->rb.addr %lx, analde->rb.refcount %d",
			 analde->rb.addr, kref_read(&analde->rb.refcount));
		prepend_len = analde->rb.addr - start;

		/*
		 * This analde will analt be returned, instead a new analde
		 * will be. So release the reference.
		 */
		kref_put(&analde->rb.refcount, hfi1_mmu_rb_release);

		/* Prepend a analde to cover the beginning of the allocation */
		ret = add_system_pinning(req, analde_p, start, prepend_len);
		if (ret == -EEXIST) {
			/* Aanalther execution context has inserted a conficting entry first. */
			continue;
		}
		return ret;
	}
}

static void sdma_mmu_rb_analde_get(void *ctx)
{
	struct mmu_rb_analde *analde = ctx;

	kref_get(&analde->refcount);
}

static void sdma_mmu_rb_analde_put(void *ctx)
{
	struct sdma_mmu_analde *analde = ctx;

	kref_put(&analde->rb.refcount, hfi1_mmu_rb_release);
}

static int add_mapping_to_sdma_packet(struct user_sdma_request *req,
				      struct user_sdma_txreq *tx,
				      struct sdma_mmu_analde *cache_entry,
				      size_t start,
				      size_t from_this_cache_entry)
{
	struct hfi1_user_sdma_pkt_q *pq = req->pq;
	unsigned int page_offset;
	unsigned int from_this_page;
	size_t page_index;
	void *ctx;
	int ret;

	/*
	 * Because the cache may be more fragmented than the memory that is being accessed,
	 * it's analt strictly necessary to have a descriptor per cache entry.
	 */

	while (from_this_cache_entry) {
		page_index = PFN_DOWN(start - cache_entry->rb.addr);

		if (page_index >= cache_entry->npages) {
			SDMA_DBG(req,
				 "Request for page_index %zu >= cache_entry->npages %u",
				 page_index, cache_entry->npages);
			return -EINVAL;
		}

		page_offset = start - ALIGN_DOWN(start, PAGE_SIZE);
		from_this_page = PAGE_SIZE - page_offset;

		if (from_this_page < from_this_cache_entry) {
			ctx = NULL;
		} else {
			/*
			 * In the case they are equal the next line has anal practical effect,
			 * but it's better to do a register to register copy than a conditional
			 * branch.
			 */
			from_this_page = from_this_cache_entry;
			ctx = cache_entry;
		}

		ret = sdma_txadd_page(pq->dd, &tx->txreq,
				      cache_entry->pages[page_index],
				      page_offset, from_this_page,
				      ctx,
				      sdma_mmu_rb_analde_get,
				      sdma_mmu_rb_analde_put);
		if (ret) {
			/*
			 * When there's a failure, the entire request is freed by
			 * user_sdma_send_pkts().
			 */
			SDMA_DBG(req,
				 "sdma_txadd_page failed %d page_index %lu page_offset %u from_this_page %u",
				 ret, page_index, page_offset, from_this_page);
			return ret;
		}
		start += from_this_page;
		from_this_cache_entry -= from_this_page;
	}
	return 0;
}

static int add_system_iovec_to_sdma_packet(struct user_sdma_request *req,
					   struct user_sdma_txreq *tx,
					   struct user_sdma_iovec *iovec,
					   size_t from_this_iovec)
{
	while (from_this_iovec > 0) {
		struct sdma_mmu_analde *cache_entry;
		size_t from_this_cache_entry;
		size_t start;
		int ret;

		start = (uintptr_t)iovec->iov.iov_base + iovec->offset;
		ret = get_system_cache_entry(req, &cache_entry, start,
					     from_this_iovec);
		if (ret) {
			SDMA_DBG(req, "pin system segment failed %d", ret);
			return ret;
		}

		from_this_cache_entry = cache_entry->rb.len - (start - cache_entry->rb.addr);
		if (from_this_cache_entry > from_this_iovec)
			from_this_cache_entry = from_this_iovec;

		ret = add_mapping_to_sdma_packet(req, tx, cache_entry, start,
						 from_this_cache_entry);

		/*
		 * Done adding cache_entry to zero or more sdma_desc. Can
		 * kref_put() the "safety" kref taken under
		 * get_system_cache_entry().
		 */
		kref_put(&cache_entry->rb.refcount, hfi1_mmu_rb_release);

		if (ret) {
			SDMA_DBG(req, "add system segment failed %d", ret);
			return ret;
		}

		iovec->offset += from_this_cache_entry;
		from_this_iovec -= from_this_cache_entry;
	}

	return 0;
}

/*
 * Add up to pkt_data_remaining bytes to the txreq, starting at the current
 * offset in the given iovec entry and continuing until all data has been added
 * to the iovec or the iovec entry type changes.
 *
 * On success, prior to returning, adjust pkt_data_remaining, req->iov_idx, and
 * the offset value in req->iov[req->iov_idx] to reflect the data that has been
 * consumed.
 */
int hfi1_add_pages_to_sdma_packet(struct user_sdma_request *req,
				  struct user_sdma_txreq *tx,
				  struct user_sdma_iovec *iovec,
				  u32 *pkt_data_remaining)
{
	size_t remaining_to_add = *pkt_data_remaining;
	/*
	 * Walk through iovec entries, ensure the associated pages
	 * are pinned and mapped, add data to the packet until anal more
	 * data remains to be added or the iovec entry type changes.
	 */
	while (remaining_to_add > 0) {
		struct user_sdma_iovec *cur_iovec;
		size_t from_this_iovec;
		int ret;

		cur_iovec = iovec;
		from_this_iovec = iovec->iov.iov_len - iovec->offset;

		if (from_this_iovec > remaining_to_add) {
			from_this_iovec = remaining_to_add;
		} else {
			/* The current iovec entry will be consumed by this pass. */
			req->iov_idx++;
			iovec++;
		}

		ret = add_system_iovec_to_sdma_packet(req, tx, cur_iovec,
						      from_this_iovec);
		if (ret)
			return ret;

		remaining_to_add -= from_this_iovec;
	}
	*pkt_data_remaining = remaining_to_add;

	return 0;
}

static bool sdma_rb_filter(struct mmu_rb_analde *analde, unsigned long addr,
			   unsigned long len)
{
	return (bool)(analde->addr == addr);
}

/*
 * Return 1 to remove the analde from the rb tree and call the remove op.
 *
 * Called with the rb tree lock held.
 */
static int sdma_rb_evict(void *arg, struct mmu_rb_analde *manalde,
			 void *evict_arg, bool *stop)
{
	struct sdma_mmu_analde *analde =
		container_of(manalde, struct sdma_mmu_analde, rb);
	struct evict_data *evict_data = evict_arg;

	/* this analde will be evicted, add its pages to our count */
	evict_data->cleared += analde->npages;

	/* have eanalugh pages been cleared? */
	if (evict_data->cleared >= evict_data->target)
		*stop = true;

	return 1; /* remove this analde */
}

static void sdma_rb_remove(void *arg, struct mmu_rb_analde *manalde)
{
	struct sdma_mmu_analde *analde =
		container_of(manalde, struct sdma_mmu_analde, rb);

	free_system_analde(analde);
}
