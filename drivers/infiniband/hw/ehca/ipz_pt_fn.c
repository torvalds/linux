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

#include "ehca_tools.h"
#include "ipz_pt_fn.h"

void *ipz_qpageit_get_inc(struct ipz_queue *queue)
{
	void *ret = ipz_qeit_get(queue);
	queue->current_q_offset += queue->pagesize;
	if (queue->current_q_offset > queue->queue_length) {
		queue->current_q_offset -= queue->pagesize;
		ret = NULL;
	}
	if (((u64)ret) % EHCA_PAGESIZE) {
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

int ipz_queue_ctor(struct ipz_queue *queue,
		   const u32 nr_of_pages,
		   const u32 pagesize, const u32 qe_size, const u32 nr_of_sg)
{
	int pages_per_kpage = PAGE_SIZE >> EHCA_PAGESHIFT;
	int f;

	if (pagesize > PAGE_SIZE) {
		ehca_gen_err("FATAL ERROR: pagesize=%x is greater "
			     "than kernel page size", pagesize);
		return 0;
	}
	if (!pages_per_kpage) {
		ehca_gen_err("FATAL ERROR: invalid kernel page size. "
			     "pages_per_kpage=%x", pages_per_kpage);
		return 0;
	}
	queue->queue_length = nr_of_pages * pagesize;
	queue->queue_pages = vmalloc(nr_of_pages * sizeof(void *));
	if (!queue->queue_pages) {
		ehca_gen_err("ERROR!! didn't get the memory");
		return 0;
	}
	memset(queue->queue_pages, 0, nr_of_pages * sizeof(void *));
	/*
	 * allocate pages for queue:
	 * outer loop allocates whole kernel pages (page aligned) and
	 * inner loop divides a kernel page into smaller hca queue pages
	 */
	f = 0;
	while (f < nr_of_pages) {
		u8 *kpage = (u8 *)get_zeroed_page(GFP_KERNEL);
		int k;
		if (!kpage)
			goto ipz_queue_ctor_exit0; /*NOMEM*/
		for (k = 0; k < pages_per_kpage && f < nr_of_pages; k++) {
			(queue->queue_pages)[f] = (struct ipz_page *)kpage;
			kpage += EHCA_PAGESIZE;
			f++;
		}
	}

	queue->current_q_offset = 0;
	queue->qe_size = qe_size;
	queue->act_nr_of_sg = nr_of_sg;
	queue->pagesize = pagesize;
	queue->toggle_state = 1;
	return 1;

 ipz_queue_ctor_exit0:
	ehca_gen_err("Couldn't get alloc pages queue=%p f=%x nr_of_pages=%x",
		     queue, f, nr_of_pages);
	for (f = 0; f < nr_of_pages; f += pages_per_kpage) {
		if (!(queue->queue_pages)[f])
			break;
		free_page((unsigned long)(queue->queue_pages)[f]);
	}
	return 0;
}

int ipz_queue_dtor(struct ipz_queue *queue)
{
	int pages_per_kpage = PAGE_SIZE >> EHCA_PAGESHIFT;
	int g;
	int nr_pages;

	if (!queue || !queue->queue_pages) {
		ehca_gen_dbg("queue or queue_pages is NULL");
		return 0;
	}
	nr_pages = queue->queue_length / queue->pagesize;
	for (g = 0; g < nr_pages; g += pages_per_kpage)
		free_page((unsigned long)(queue->queue_pages)[g]);
	vfree(queue->queue_pages);

	return 1;
}
