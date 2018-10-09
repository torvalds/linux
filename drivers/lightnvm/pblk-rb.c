// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2016 CNEX Labs
 * Initial release: Javier Gonzalez <javier@cnexlabs.com>
 *
 * Based upon the circular ringbuffer.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * pblk-rb.c - pblk's write buffer
 */

#include <linux/circ_buf.h>

#include "pblk.h"

static DECLARE_RWSEM(pblk_rb_lock);

static void pblk_rb_data_free(struct pblk_rb *rb)
{
	struct pblk_rb_pages *p, *t;

	down_write(&pblk_rb_lock);
	list_for_each_entry_safe(p, t, &rb->pages, list) {
		free_pages((unsigned long)page_address(p->pages), p->order);
		list_del(&p->list);
		kfree(p);
	}
	up_write(&pblk_rb_lock);
}

void pblk_rb_free(struct pblk_rb *rb)
{
	pblk_rb_data_free(rb);
	vfree(rb->entries);
}

/*
 * pblk_rb_calculate_size -- calculate the size of the write buffer
 */
static unsigned int pblk_rb_calculate_size(unsigned int nr_entries)
{
	/* Alloc a write buffer that can at least fit 128 entries */
	return (1 << max(get_count_order(nr_entries), 7));
}

/*
 * Initialize ring buffer. The data and metadata buffers must be previously
 * allocated and their size must be a power of two
 * (Documentation/core-api/circular-buffers.rst)
 */
int pblk_rb_init(struct pblk_rb *rb, unsigned int size, unsigned int threshold,
		 unsigned int seg_size)
{
	struct pblk *pblk = container_of(rb, struct pblk, rwb);
	struct pblk_rb_entry *entries;
	unsigned int init_entry = 0;
	unsigned int max_order = MAX_ORDER - 1;
	unsigned int power_size, power_seg_sz;
	unsigned int alloc_order, order, iter;
	unsigned int nr_entries;

	nr_entries = pblk_rb_calculate_size(size);
	entries = vzalloc(array_size(nr_entries, sizeof(struct pblk_rb_entry)));
	if (!entries)
		return -ENOMEM;

	power_size = get_count_order(size);
	power_seg_sz = get_count_order(seg_size);

	down_write(&pblk_rb_lock);
	rb->entries = entries;
	rb->seg_size = (1 << power_seg_sz);
	rb->nr_entries = (1 << power_size);
	rb->mem = rb->subm = rb->sync = rb->l2p_update = 0;
	rb->back_thres = threshold;
	rb->flush_point = EMPTY_ENTRY;

	spin_lock_init(&rb->w_lock);
	spin_lock_init(&rb->s_lock);

	INIT_LIST_HEAD(&rb->pages);

	alloc_order = power_size;
	if (alloc_order >= max_order) {
		order = max_order;
		iter = (1 << (alloc_order - max_order));
	} else {
		order = alloc_order;
		iter = 1;
	}

	do {
		struct pblk_rb_entry *entry;
		struct pblk_rb_pages *page_set;
		void *kaddr;
		unsigned long set_size;
		int i;

		page_set = kmalloc(sizeof(struct pblk_rb_pages), GFP_KERNEL);
		if (!page_set) {
			up_write(&pblk_rb_lock);
			vfree(entries);
			return -ENOMEM;
		}

		page_set->order = order;
		page_set->pages = alloc_pages(GFP_KERNEL, order);
		if (!page_set->pages) {
			kfree(page_set);
			pblk_rb_data_free(rb);
			up_write(&pblk_rb_lock);
			vfree(entries);
			return -ENOMEM;
		}
		kaddr = page_address(page_set->pages);

		entry = &rb->entries[init_entry];
		entry->data = kaddr;
		entry->cacheline = pblk_cacheline_to_addr(init_entry++);
		entry->w_ctx.flags = PBLK_WRITABLE_ENTRY;

		set_size = (1 << order);
		for (i = 1; i < set_size; i++) {
			entry = &rb->entries[init_entry];
			entry->cacheline = pblk_cacheline_to_addr(init_entry++);
			entry->data = kaddr + (i * rb->seg_size);
			entry->w_ctx.flags = PBLK_WRITABLE_ENTRY;
			bio_list_init(&entry->w_ctx.bios);
		}

		list_add_tail(&page_set->list, &rb->pages);
		iter--;
	} while (iter > 0);
	up_write(&pblk_rb_lock);

#ifdef CONFIG_NVM_PBLK_DEBUG
	atomic_set(&rb->inflight_flush_point, 0);
#endif

	/*
	 * Initialize rate-limiter, which controls access to the write buffer
	 * but user and GC I/O
	 */
	pblk_rl_init(&pblk->rl, rb->nr_entries);

	return 0;
}

static void clean_wctx(struct pblk_w_ctx *w_ctx)
{
	int flags;

	flags = READ_ONCE(w_ctx->flags);
	WARN_ONCE(!(flags & PBLK_SUBMITTED_ENTRY),
			"pblk: overwriting unsubmitted data\n");

	/* Release flags on context. Protect from writes and reads */
	smp_store_release(&w_ctx->flags, PBLK_WRITABLE_ENTRY);
	pblk_ppa_set_empty(&w_ctx->ppa);
	w_ctx->lba = ADDR_EMPTY;
}

#define pblk_rb_ring_count(head, tail, size) CIRC_CNT(head, tail, size)
#define pblk_rb_ring_space(rb, head, tail, size) \
					(CIRC_SPACE(head, tail, size))

/*
 * Buffer space is calculated with respect to the back pointer signaling
 * synchronized entries to the media.
 */
static unsigned int pblk_rb_space(struct pblk_rb *rb)
{
	unsigned int mem = READ_ONCE(rb->mem);
	unsigned int sync = READ_ONCE(rb->sync);

	return pblk_rb_ring_space(rb, mem, sync, rb->nr_entries);
}

unsigned int pblk_rb_ptr_wrap(struct pblk_rb *rb, unsigned int p,
			      unsigned int nr_entries)
{
	return (p + nr_entries) & (rb->nr_entries - 1);
}

/*
 * Buffer count is calculated with respect to the submission entry signaling the
 * entries that are available to send to the media
 */
unsigned int pblk_rb_read_count(struct pblk_rb *rb)
{
	unsigned int mem = READ_ONCE(rb->mem);
	unsigned int subm = READ_ONCE(rb->subm);

	return pblk_rb_ring_count(mem, subm, rb->nr_entries);
}

unsigned int pblk_rb_sync_count(struct pblk_rb *rb)
{
	unsigned int mem = READ_ONCE(rb->mem);
	unsigned int sync = READ_ONCE(rb->sync);

	return pblk_rb_ring_count(mem, sync, rb->nr_entries);
}

unsigned int pblk_rb_read_commit(struct pblk_rb *rb, unsigned int nr_entries)
{
	unsigned int subm;

	subm = READ_ONCE(rb->subm);
	/* Commit read means updating submission pointer */
	smp_store_release(&rb->subm, pblk_rb_ptr_wrap(rb, subm, nr_entries));

	return subm;
}

static int __pblk_rb_update_l2p(struct pblk_rb *rb, unsigned int to_update)
{
	struct pblk *pblk = container_of(rb, struct pblk, rwb);
	struct pblk_line *line;
	struct pblk_rb_entry *entry;
	struct pblk_w_ctx *w_ctx;
	unsigned int user_io = 0, gc_io = 0;
	unsigned int i;
	int flags;

	for (i = 0; i < to_update; i++) {
		entry = &rb->entries[rb->l2p_update];
		w_ctx = &entry->w_ctx;

		flags = READ_ONCE(entry->w_ctx.flags);
		if (flags & PBLK_IOTYPE_USER)
			user_io++;
		else if (flags & PBLK_IOTYPE_GC)
			gc_io++;
		else
			WARN(1, "pblk: unknown IO type\n");

		pblk_update_map_dev(pblk, w_ctx->lba, w_ctx->ppa,
							entry->cacheline);

		line = pblk_ppa_to_line(pblk, w_ctx->ppa);
		kref_put(&line->ref, pblk_line_put);
		clean_wctx(w_ctx);
		rb->l2p_update = pblk_rb_ptr_wrap(rb, rb->l2p_update, 1);
	}

	pblk_rl_out(&pblk->rl, user_io, gc_io);

	return 0;
}

/*
 * When we move the l2p_update pointer, we update the l2p table - lookups will
 * point to the physical address instead of to the cacheline in the write buffer
 * from this moment on.
 */
static int pblk_rb_update_l2p(struct pblk_rb *rb, unsigned int nr_entries,
			      unsigned int mem, unsigned int sync)
{
	unsigned int space, count;
	int ret = 0;

	lockdep_assert_held(&rb->w_lock);

	/* Update l2p only as buffer entries are being overwritten */
	space = pblk_rb_ring_space(rb, mem, rb->l2p_update, rb->nr_entries);
	if (space > nr_entries)
		goto out;

	count = nr_entries - space;
	/* l2p_update used exclusively under rb->w_lock */
	ret = __pblk_rb_update_l2p(rb, count);

out:
	return ret;
}

/*
 * Update the l2p entry for all sectors stored on the write buffer. This means
 * that all future lookups to the l2p table will point to a device address, not
 * to the cacheline in the write buffer.
 */
void pblk_rb_sync_l2p(struct pblk_rb *rb)
{
	unsigned int sync;
	unsigned int to_update;

	spin_lock(&rb->w_lock);

	/* Protect from reads and writes */
	sync = smp_load_acquire(&rb->sync);

	to_update = pblk_rb_ring_count(sync, rb->l2p_update, rb->nr_entries);
	__pblk_rb_update_l2p(rb, to_update);

	spin_unlock(&rb->w_lock);
}

/*
 * Write @nr_entries to ring buffer from @data buffer if there is enough space.
 * Typically, 4KB data chunks coming from a bio will be copied to the ring
 * buffer, thus the write will fail if not all incoming data can be copied.
 *
 */
static void __pblk_rb_write_entry(struct pblk_rb *rb, void *data,
				  struct pblk_w_ctx w_ctx,
				  struct pblk_rb_entry *entry)
{
	memcpy(entry->data, data, rb->seg_size);

	entry->w_ctx.lba = w_ctx.lba;
	entry->w_ctx.ppa = w_ctx.ppa;
}

void pblk_rb_write_entry_user(struct pblk_rb *rb, void *data,
			      struct pblk_w_ctx w_ctx, unsigned int ring_pos)
{
	struct pblk *pblk = container_of(rb, struct pblk, rwb);
	struct pblk_rb_entry *entry;
	int flags;

	entry = &rb->entries[ring_pos];
	flags = READ_ONCE(entry->w_ctx.flags);
#ifdef CONFIG_NVM_PBLK_DEBUG
	/* Caller must guarantee that the entry is free */
	BUG_ON(!(flags & PBLK_WRITABLE_ENTRY));
#endif

	__pblk_rb_write_entry(rb, data, w_ctx, entry);

	pblk_update_map_cache(pblk, w_ctx.lba, entry->cacheline);
	flags = w_ctx.flags | PBLK_WRITTEN_DATA;

	/* Release flags on write context. Protect from writes */
	smp_store_release(&entry->w_ctx.flags, flags);
}

void pblk_rb_write_entry_gc(struct pblk_rb *rb, void *data,
			    struct pblk_w_ctx w_ctx, struct pblk_line *line,
			    u64 paddr, unsigned int ring_pos)
{
	struct pblk *pblk = container_of(rb, struct pblk, rwb);
	struct pblk_rb_entry *entry;
	int flags;

	entry = &rb->entries[ring_pos];
	flags = READ_ONCE(entry->w_ctx.flags);
#ifdef CONFIG_NVM_PBLK_DEBUG
	/* Caller must guarantee that the entry is free */
	BUG_ON(!(flags & PBLK_WRITABLE_ENTRY));
#endif

	__pblk_rb_write_entry(rb, data, w_ctx, entry);

	if (!pblk_update_map_gc(pblk, w_ctx.lba, entry->cacheline, line, paddr))
		entry->w_ctx.lba = ADDR_EMPTY;

	flags = w_ctx.flags | PBLK_WRITTEN_DATA;

	/* Release flags on write context. Protect from writes */
	smp_store_release(&entry->w_ctx.flags, flags);
}

static int pblk_rb_flush_point_set(struct pblk_rb *rb, struct bio *bio,
				   unsigned int pos)
{
	struct pblk_rb_entry *entry;
	unsigned int sync, flush_point;

	pblk_rb_sync_init(rb, NULL);
	sync = READ_ONCE(rb->sync);

	if (pos == sync) {
		pblk_rb_sync_end(rb, NULL);
		return 0;
	}

#ifdef CONFIG_NVM_PBLK_DEBUG
	atomic_inc(&rb->inflight_flush_point);
#endif

	flush_point = (pos == 0) ? (rb->nr_entries - 1) : (pos - 1);
	entry = &rb->entries[flush_point];

	/* Protect flush points */
	smp_store_release(&rb->flush_point, flush_point);

	if (bio)
		bio_list_add(&entry->w_ctx.bios, bio);

	pblk_rb_sync_end(rb, NULL);

	return bio ? 1 : 0;
}

static int __pblk_rb_may_write(struct pblk_rb *rb, unsigned int nr_entries,
			       unsigned int *pos)
{
	unsigned int mem;
	unsigned int sync;
	unsigned int threshold;

	sync = READ_ONCE(rb->sync);
	mem = READ_ONCE(rb->mem);

	threshold = nr_entries + rb->back_thres;

	if (pblk_rb_ring_space(rb, mem, sync, rb->nr_entries) < threshold)
		return 0;

	if (pblk_rb_update_l2p(rb, nr_entries, mem, sync))
		return 0;

	*pos = mem;

	return 1;
}

static int pblk_rb_may_write(struct pblk_rb *rb, unsigned int nr_entries,
			     unsigned int *pos)
{
	if (!__pblk_rb_may_write(rb, nr_entries, pos))
		return 0;

	/* Protect from read count */
	smp_store_release(&rb->mem, pblk_rb_ptr_wrap(rb, *pos, nr_entries));
	return 1;
}

void pblk_rb_flush(struct pblk_rb *rb)
{
	struct pblk *pblk = container_of(rb, struct pblk, rwb);
	unsigned int mem = READ_ONCE(rb->mem);

	if (pblk_rb_flush_point_set(rb, NULL, mem))
		return;

	pblk_write_kick(pblk);
}

static int pblk_rb_may_write_flush(struct pblk_rb *rb, unsigned int nr_entries,
				   unsigned int *pos, struct bio *bio,
				   int *io_ret)
{
	unsigned int mem;

	if (!__pblk_rb_may_write(rb, nr_entries, pos))
		return 0;

	mem = pblk_rb_ptr_wrap(rb, *pos, nr_entries);
	*io_ret = NVM_IO_DONE;

	if (bio->bi_opf & REQ_PREFLUSH) {
		struct pblk *pblk = container_of(rb, struct pblk, rwb);

		atomic64_inc(&pblk->nr_flush);
		if (pblk_rb_flush_point_set(&pblk->rwb, bio, mem))
			*io_ret = NVM_IO_OK;
	}

	/* Protect from read count */
	smp_store_release(&rb->mem, mem);

	return 1;
}

/*
 * Atomically check that (i) there is space on the write buffer for the
 * incoming I/O, and (ii) the current I/O type has enough budget in the write
 * buffer (rate-limiter).
 */
int pblk_rb_may_write_user(struct pblk_rb *rb, struct bio *bio,
			   unsigned int nr_entries, unsigned int *pos)
{
	struct pblk *pblk = container_of(rb, struct pblk, rwb);
	int io_ret;

	spin_lock(&rb->w_lock);
	io_ret = pblk_rl_user_may_insert(&pblk->rl, nr_entries);
	if (io_ret) {
		spin_unlock(&rb->w_lock);
		return io_ret;
	}

	if (!pblk_rb_may_write_flush(rb, nr_entries, pos, bio, &io_ret)) {
		spin_unlock(&rb->w_lock);
		return NVM_IO_REQUEUE;
	}

	pblk_rl_user_in(&pblk->rl, nr_entries);
	spin_unlock(&rb->w_lock);

	return io_ret;
}

/*
 * Look at pblk_rb_may_write_user comment
 */
int pblk_rb_may_write_gc(struct pblk_rb *rb, unsigned int nr_entries,
			 unsigned int *pos)
{
	struct pblk *pblk = container_of(rb, struct pblk, rwb);

	spin_lock(&rb->w_lock);
	if (!pblk_rl_gc_may_insert(&pblk->rl, nr_entries)) {
		spin_unlock(&rb->w_lock);
		return 0;
	}

	if (!pblk_rb_may_write(rb, nr_entries, pos)) {
		spin_unlock(&rb->w_lock);
		return 0;
	}

	pblk_rl_gc_in(&pblk->rl, nr_entries);
	spin_unlock(&rb->w_lock);

	return 1;
}

/*
 * Read available entries on rb and add them to the given bio. To avoid a memory
 * copy, a page reference to the write buffer is used to be added to the bio.
 *
 * This function is used by the write thread to form the write bio that will
 * persist data on the write buffer to the media.
 */
unsigned int pblk_rb_read_to_bio(struct pblk_rb *rb, struct nvm_rq *rqd,
				 unsigned int pos, unsigned int nr_entries,
				 unsigned int count)
{
	struct pblk *pblk = container_of(rb, struct pblk, rwb);
	struct request_queue *q = pblk->dev->q;
	struct pblk_c_ctx *c_ctx = nvm_rq_to_pdu(rqd);
	struct bio *bio = rqd->bio;
	struct pblk_rb_entry *entry;
	struct page *page;
	unsigned int pad = 0, to_read = nr_entries;
	unsigned int i;
	int flags;

	if (count < nr_entries) {
		pad = nr_entries - count;
		to_read = count;
	}

	c_ctx->sentry = pos;
	c_ctx->nr_valid = to_read;
	c_ctx->nr_padded = pad;

	for (i = 0; i < to_read; i++) {
		entry = &rb->entries[pos];

		/* A write has been allowed into the buffer, but data is still
		 * being copied to it. It is ok to busy wait.
		 */
try:
		flags = READ_ONCE(entry->w_ctx.flags);
		if (!(flags & PBLK_WRITTEN_DATA)) {
			io_schedule();
			goto try;
		}

		page = virt_to_page(entry->data);
		if (!page) {
			pblk_err(pblk, "could not allocate write bio page\n");
			flags &= ~PBLK_WRITTEN_DATA;
			flags |= PBLK_SUBMITTED_ENTRY;
			/* Release flags on context. Protect from writes */
			smp_store_release(&entry->w_ctx.flags, flags);
			return NVM_IO_ERR;
		}

		if (bio_add_pc_page(q, bio, page, rb->seg_size, 0) !=
								rb->seg_size) {
			pblk_err(pblk, "could not add page to write bio\n");
			flags &= ~PBLK_WRITTEN_DATA;
			flags |= PBLK_SUBMITTED_ENTRY;
			/* Release flags on context. Protect from writes */
			smp_store_release(&entry->w_ctx.flags, flags);
			return NVM_IO_ERR;
		}

		flags &= ~PBLK_WRITTEN_DATA;
		flags |= PBLK_SUBMITTED_ENTRY;

		/* Release flags on context. Protect from writes */
		smp_store_release(&entry->w_ctx.flags, flags);

		pos = pblk_rb_ptr_wrap(rb, pos, 1);
	}

	if (pad) {
		if (pblk_bio_add_pages(pblk, bio, GFP_KERNEL, pad)) {
			pblk_err(pblk, "could not pad page in write bio\n");
			return NVM_IO_ERR;
		}

		if (pad < pblk->min_write_pgs)
			atomic64_inc(&pblk->pad_dist[pad - 1]);
		else
			pblk_warn(pblk, "padding more than min. sectors\n");

		atomic64_add(pad, &pblk->pad_wa);
	}

#ifdef CONFIG_NVM_PBLK_DEBUG
	atomic_long_add(pad, &pblk->padded_writes);
#endif

	return NVM_IO_OK;
}

/*
 * Copy to bio only if the lba matches the one on the given cache entry.
 * Otherwise, it means that the entry has been overwritten, and the bio should
 * be directed to disk.
 */
int pblk_rb_copy_to_bio(struct pblk_rb *rb, struct bio *bio, sector_t lba,
			struct ppa_addr ppa, int bio_iter, bool advanced_bio)
{
	struct pblk *pblk = container_of(rb, struct pblk, rwb);
	struct pblk_rb_entry *entry;
	struct pblk_w_ctx *w_ctx;
	struct ppa_addr l2p_ppa;
	u64 pos = pblk_addr_to_cacheline(ppa);
	void *data;
	int flags;
	int ret = 1;


#ifdef CONFIG_NVM_PBLK_DEBUG
	/* Caller must ensure that the access will not cause an overflow */
	BUG_ON(pos >= rb->nr_entries);
#endif
	entry = &rb->entries[pos];
	w_ctx = &entry->w_ctx;
	flags = READ_ONCE(w_ctx->flags);

	spin_lock(&rb->w_lock);
	spin_lock(&pblk->trans_lock);
	l2p_ppa = pblk_trans_map_get(pblk, lba);
	spin_unlock(&pblk->trans_lock);

	/* Check if the entry has been overwritten or is scheduled to be */
	if (!pblk_ppa_comp(l2p_ppa, ppa) || w_ctx->lba != lba ||
						flags & PBLK_WRITABLE_ENTRY) {
		ret = 0;
		goto out;
	}

	/* Only advance the bio if it hasn't been advanced already. If advanced,
	 * this bio is at least a partial bio (i.e., it has partially been
	 * filled with data from the cache). If part of the data resides on the
	 * media, we will read later on
	 */
	if (unlikely(!advanced_bio))
		bio_advance(bio, bio_iter * PBLK_EXPOSED_PAGE_SIZE);

	data = bio_data(bio);
	memcpy(data, entry->data, rb->seg_size);

out:
	spin_unlock(&rb->w_lock);
	return ret;
}

struct pblk_w_ctx *pblk_rb_w_ctx(struct pblk_rb *rb, unsigned int pos)
{
	unsigned int entry = pblk_rb_ptr_wrap(rb, pos, 0);

	return &rb->entries[entry].w_ctx;
}

unsigned int pblk_rb_sync_init(struct pblk_rb *rb, unsigned long *flags)
	__acquires(&rb->s_lock)
{
	if (flags)
		spin_lock_irqsave(&rb->s_lock, *flags);
	else
		spin_lock_irq(&rb->s_lock);

	return rb->sync;
}

void pblk_rb_sync_end(struct pblk_rb *rb, unsigned long *flags)
	__releases(&rb->s_lock)
{
	lockdep_assert_held(&rb->s_lock);

	if (flags)
		spin_unlock_irqrestore(&rb->s_lock, *flags);
	else
		spin_unlock_irq(&rb->s_lock);
}

unsigned int pblk_rb_sync_advance(struct pblk_rb *rb, unsigned int nr_entries)
{
	unsigned int sync, flush_point;
	lockdep_assert_held(&rb->s_lock);

	sync = READ_ONCE(rb->sync);
	flush_point = READ_ONCE(rb->flush_point);

	if (flush_point != EMPTY_ENTRY) {
		unsigned int secs_to_flush;

		secs_to_flush = pblk_rb_ring_count(flush_point, sync,
					rb->nr_entries);
		if (secs_to_flush < nr_entries) {
			/* Protect flush points */
			smp_store_release(&rb->flush_point, EMPTY_ENTRY);
		}
	}

	sync = pblk_rb_ptr_wrap(rb, sync, nr_entries);

	/* Protect from counts */
	smp_store_release(&rb->sync, sync);

	return sync;
}

/* Calculate how many sectors to submit up to the current flush point. */
unsigned int pblk_rb_flush_point_count(struct pblk_rb *rb)
{
	unsigned int subm, sync, flush_point;
	unsigned int submitted, to_flush;

	/* Protect flush points */
	flush_point = smp_load_acquire(&rb->flush_point);
	if (flush_point == EMPTY_ENTRY)
		return 0;

	/* Protect syncs */
	sync = smp_load_acquire(&rb->sync);

	subm = READ_ONCE(rb->subm);
	submitted = pblk_rb_ring_count(subm, sync, rb->nr_entries);

	/* The sync point itself counts as a sector to sync */
	to_flush = pblk_rb_ring_count(flush_point, sync, rb->nr_entries) + 1;

	return (submitted < to_flush) ? (to_flush - submitted) : 0;
}

int pblk_rb_tear_down_check(struct pblk_rb *rb)
{
	struct pblk_rb_entry *entry;
	int i;
	int ret = 0;

	spin_lock(&rb->w_lock);
	spin_lock_irq(&rb->s_lock);

	if ((rb->mem == rb->subm) && (rb->subm == rb->sync) &&
				(rb->sync == rb->l2p_update) &&
				(rb->flush_point == EMPTY_ENTRY)) {
		goto out;
	}

	if (!rb->entries) {
		ret = 1;
		goto out;
	}

	for (i = 0; i < rb->nr_entries; i++) {
		entry = &rb->entries[i];

		if (!entry->data) {
			ret = 1;
			goto out;
		}
	}

out:
	spin_unlock(&rb->w_lock);
	spin_unlock_irq(&rb->s_lock);

	return ret;
}

unsigned int pblk_rb_wrap_pos(struct pblk_rb *rb, unsigned int pos)
{
	return (pos & (rb->nr_entries - 1));
}

int pblk_rb_pos_oob(struct pblk_rb *rb, u64 pos)
{
	return (pos >= rb->nr_entries);
}

ssize_t pblk_rb_sysfs(struct pblk_rb *rb, char *buf)
{
	struct pblk *pblk = container_of(rb, struct pblk, rwb);
	struct pblk_c_ctx *c;
	ssize_t offset;
	int queued_entries = 0;

	spin_lock_irq(&rb->s_lock);
	list_for_each_entry(c, &pblk->compl_list, list)
		queued_entries++;
	spin_unlock_irq(&rb->s_lock);

	if (rb->flush_point != EMPTY_ENTRY)
		offset = scnprintf(buf, PAGE_SIZE,
			"%u\t%u\t%u\t%u\t%u\t%u\t%u - %u/%u/%u - %d\n",
			rb->nr_entries,
			rb->mem,
			rb->subm,
			rb->sync,
			rb->l2p_update,
#ifdef CONFIG_NVM_PBLK_DEBUG
			atomic_read(&rb->inflight_flush_point),
#else
			0,
#endif
			rb->flush_point,
			pblk_rb_read_count(rb),
			pblk_rb_space(rb),
			pblk_rb_flush_point_count(rb),
			queued_entries);
	else
		offset = scnprintf(buf, PAGE_SIZE,
			"%u\t%u\t%u\t%u\t%u\t%u\tNULL - %u/%u/%u - %d\n",
			rb->nr_entries,
			rb->mem,
			rb->subm,
			rb->sync,
			rb->l2p_update,
#ifdef CONFIG_NVM_PBLK_DEBUG
			atomic_read(&rb->inflight_flush_point),
#else
			0,
#endif
			pblk_rb_read_count(rb),
			pblk_rb_space(rb),
			pblk_rb_flush_point_count(rb),
			queued_entries);

	return offset;
}
