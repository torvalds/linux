/*
 *  IBM eServer eHCA Infiniband device driver for Linux on POWER
 *
 *  internal queue handling
 *
 *  Authors: Waleri Fomin <fomin@de.ibm.com>
 *           Reinhard Ernst <rernst@de.ibm.com>
 *           Christoph Raisch <raisch@de.ibm.com>
 *
 *  Copyright (c) 2005 IBM Corporation
 *
 *  This source code is distributed under a dual license of GPL v2.0 and OpenIB
 *  BSD.
 *
 * OpenIB BSD License
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials
 * provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/slab.h>

#include "ehca_tools.h"
#include "ipz_pt_fn.h"
#include "ehca_classes.h"

#define PAGES_PER_KPAGE (PAGE_SIZE >> EHCA_PAGESHIFT)

struct kmem_cache *small_qp_cache;

void *ipz_qpageit_get_inc(struct ipz_queue *queue)
{
	void *ret = ipz_qeit_get(queue);
	queue->current_q_offset += queue->pagesize;
	if (queue->current_q_offset > queue->queue_length) {
		queue->current_q_offset -= queue->pagesize;
		ret = NULL;
	}
	if (((u64)ret) % queue->pagesize) {
		ehca_gen_err("ERROR!! not at PAGE-Boundary");
		return NULL;
	}
	return ret;
}

void *ipz_qeit_eq_get_inc(struct ipz_queue *queue)
{
	void *ret = ipz_qeit_get(queue);
	u64 last_entry_in_q = queue->queue_length - queue->qe_size;

	queue->current_q_offset += queue->qe_size;
	if (queue->current_q_offset > last_entry_in_q) {
		queue->current_q_offset = 0;
		queue->toggle_state = (~queue->toggle_state) & 1;
	}

	return ret;
}

int ipz_queue_abs_to_offset(struct ipz_queue *queue, u64 addr, u64 *q_offset)
{
	int i;
	for (i = 0; i < queue->queue_length / queue->pagesize; i++) {
		u64 page = (u64)virt_to_abs(queue->queue_pages[i]);
		if (addr >= page && addr < page + queue->pagesize) {
			*q_offset = addr - page + i * queue->pagesize;
			return 0;
		}
	}
	return -EINVAL;
}

#if PAGE_SHIFT < EHCA_PAGESHIFT
#error Kernel pages must be at least as large than eHCA pages (4K) !
#endif

/*
 * allocate pages for queue:
 * outer loop allocates whole kernel pages (page aligned) and
 * inner loop divides a kernel page into smaller hca queue pages
 */
static int alloc_queue_pages(struct ipz_queue *queue, const u32 nr_of_pages)
{
	int k, f = 0;
	u8 *kpage;

	while (f < nr_of_pages) {
		kpage = (u8 *)get_zeroed_page(GFP_KERNEL);
		if (!kpage)
			goto out;

		for (k = 0; k < PAGES_PER_KPAGE && f < nr_of_pages; k++) {
			queue->queue_pages[f] = (struct ipz_page *)kpage;
			kpage += EHCA_PAGESIZE;
			f++;
		}
	}
	return 1;

out:
	for (f = 0; f < nr_of_pages && queue->queue_pages[f];
	     f += PAGES_PER_KPAGE)
		free_page((unsigned long)(queue->queue_pages)[f]);
	return 0;
}

static int alloc_small_queue_page(struct ipz_queue *queue, struct ehca_pd *pd)
{
	int order = ilog2(queue->pagesize) - 9;
	struct ipz_small_queue_page *page;
	unsigned long bit;

	mutex_lock(&pd->lock);

	if (!list_empty(&pd->free[order]))
		page = list_entry(pd->free[order].next,
				  struct ipz_small_queue_page, list);
	else {
		page = kmem_cache_zalloc(small_qp_cache, GFP_KERNEL);
		if (!page)
			goto out;

		page->page = get_zeroed_page(GFP_KERNEL);
		if (!page->page) {
			kmem_cache_free(small_qp_cache, page);
			goto out;
		}

		list_add(&page->list, &pd->free[order]);
	}

	bit = find_first_zero_bit(page->bitmap, IPZ_SPAGE_PER_KPAGE >> order);
	__set_bit(bit, page->bitmap);
	page->fill++;

	if (page->fill == IPZ_SPAGE_PER_KPAGE >> order)
		list_move(&page->list, &pd->full[order]);

	mutex_unlock(&pd->lock);

	queue->queue_pages[0] = (void *)(page->page | (bit << (order + 9)));
	queue->small_page = page;
	queue->offset = bit << (order + 9);
	return 1;

out:
	ehca_err(pd->ib_pd.device, "failed to allocate small queue page");
	mutex_unlock(&pd->lock);
	return 0;
}

static void free_small_queue_page(struct ipz_queue *queue, struct ehca_pd *pd)
{
	int order = ilog2(queue->pagesize) - 9;
	struct ipz_small_queue_page *page = queue->small_page;
	unsigned long bit;
	int free_page = 0;

	bit = ((unsigned long)queue->queue_pages[0] & ~PAGE_MASK)
		>> (order + 9);

	mutex_lock(&pd->lock);

	__clear_bit(bit, page->bitmap);
	page->fill--;

	if (page->fill == 0) {
		list_del(&page->list);
		free_page = 1;
	}

	if (page->fill == (IPZ_SPAGE_PER_KPAGE >> order) - 1)
		/* the page was full until we freed the chunk */
		list_move_tail(&page->list, &pd->free[order]);

	mutex_unlock(&pd->lock);

	if (free_page) {
		free_page(page->page);
		kmem_cache_free(small_qp_cache, page);
	}
}

int ipz_queue_ctor(struct ehca_pd *pd, struct ipz_queue *queue,
		   const u32 nr_of_pages, const u32 pagesize,
		   const u32 qe_size, const u32 nr_of_sg,
		   int is_small)
{
	if (pagesize > PAGE_SIZE) {
		ehca_gen_err("FATAL ERROR: pagesize=%x "
			     "is greater than kernel page size", pagesize);
		return 0;
	}

	/* init queue fields */
	queue->queue_length = nr_of_pages * pagesize;
	queue->pagesize = pagesize;
	queue->qe_size = qe_size;
	queue->act_nr_of_sg = nr_of_sg;
	queue->current_q_offset = 0;
	queue->toggle_state = 1;
	queue->small_page = NULL;

	/* allocate queue page pointers */
	queue->queue_pages = kzalloc(nr_of_pages * sizeof(void *), GFP_KERNEL);
	if (!queue->queue_pages) {
		queue->queue_pages = vzalloc(nr_of_pages * sizeof(void *));
		if (!queue->queue_pages) {
			ehca_gen_err("Couldn't allocate queue page list");
			return 0;
		}
	}

	/* allocate actual queue pages */
	if (is_small) {
		if (!alloc_small_queue_page(queue, pd))
			goto ipz_queue_ctor_exit0;
	} else
		if (!alloc_queue_pages(queue, nr_of_pages))
			goto ipz_queue_ctor_exit0;

	return 1;

ipz_queue_ctor_exit0:
	ehca_gen_err("Couldn't alloc pages queue=%p "
		 "nr_of_pages=%x",  queue, nr_of_pages);
	if (is_vmalloc_addr(queue->queue_pages))
		vfree(queue->queue_pages);
	else
		kfree(queue->queue_pages);

	return 0;
}

int ipz_queue_dtor(struct ehca_pd *pd, struct ipz_queue *queue)
{
	int i, nr_pages;

	if (!queue || !queue->queue_pages) {
		ehca_gen_dbg("queue or queue_pages is NULL");
		return 0;
	}

	if (queue->small_page)
		free_small_queue_page(queue, pd);
	else {
		nr_pages = queue->queue_length / queue->pagesize;
		for (i = 0; i < nr_pages; i += PAGES_PER_KPAGE)
			free_page((unsigned long)queue->queue_pages[i]);
	}

	if (is_vmalloc_addr(queue->queue_pages))
		vfree(queue->queue_pages);
	else
		kfree(queue->queue_pages);

	return 1;
}

int ehca_init_small_qp_cache(void)
{
	small_qp_cache = kmem_cache_create("ehca_cache_small_qp",
					   sizeof(struct ipz_small_queue_page),
					   0, SLAB_HWCACHE_ALIGN, NULL);
	if (!small_qp_cache)
		return -ENOMEM;

	return 0;
}

void ehca_cleanup_small_qp_cache(void)
{
	kmem_cache_destroy(small_qp_cache);
}
