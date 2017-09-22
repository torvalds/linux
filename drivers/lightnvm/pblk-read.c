/*
 * Copyright (C) 2016 CNEX Labs
 * Initial release: Javier Gonzalez <javier@cnexlabs.com>
 *                  Matias Bjorling <matias@cnexlabs.com>
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
 * pblk-read.c - pblk's read path
 */

#include "pblk.h"

/*
 * There is no guarantee that the value read from cache has not been updated and
 * resides at another location in the cache. We guarantee though that if the
 * value is read from the cache, it belongs to the mapped lba. In order to
 * guarantee and order between writes and reads are ordered, a flush must be
 * issued.
 */
static int pblk_read_from_cache(struct pblk *pblk, struct bio *bio,
				sector_t lba, struct ppa_addr ppa,
				int bio_iter, bool advanced_bio)
{
#ifdef CONFIG_NVM_DEBUG
	/* Callers must ensure that the ppa points to a cache address */
	BUG_ON(pblk_ppa_empty(ppa));
	BUG_ON(!pblk_addr_in_cache(ppa));
#endif

	return pblk_rb_copy_to_bio(&pblk->rwb, bio, lba, ppa,
						bio_iter, advanced_bio);
}

static void pblk_read_ppalist_rq(struct pblk *pblk, struct nvm_rq *rqd,
				 unsigned long *read_bitmap)
{
	struct bio *bio = rqd->bio;
	struct ppa_addr ppas[PBLK_MAX_REQ_ADDRS];
	sector_t blba = pblk_get_lba(bio);
	int nr_secs = rqd->nr_ppas;
	bool advanced_bio = false;
	int i, j = 0;

	/* logic error: lba out-of-bounds. Ignore read request */
	if (blba + nr_secs >= pblk->rl.nr_secs) {
		WARN(1, "pblk: read lbas out of bounds\n");
		return;
	}

	pblk_lookup_l2p_seq(pblk, ppas, blba, nr_secs);

	for (i = 0; i < nr_secs; i++) {
		struct ppa_addr p = ppas[i];
		sector_t lba = blba + i;

retry:
		if (pblk_ppa_empty(p)) {
			WARN_ON(test_and_set_bit(i, read_bitmap));

			if (unlikely(!advanced_bio)) {
				bio_advance(bio, (i) * PBLK_EXPOSED_PAGE_SIZE);
				advanced_bio = true;
			}

			goto next;
		}

		/* Try to read from write buffer. The address is later checked
		 * on the write buffer to prevent retrieving overwritten data.
		 */
		if (pblk_addr_in_cache(p)) {
			if (!pblk_read_from_cache(pblk, bio, lba, p, i,
								advanced_bio)) {
				pblk_lookup_l2p_seq(pblk, &p, lba, 1);
				goto retry;
			}
			WARN_ON(test_and_set_bit(i, read_bitmap));
			advanced_bio = true;
#ifdef CONFIG_NVM_DEBUG
			atomic_long_inc(&pblk->cache_reads);
#endif
		} else {
			/* Read from media non-cached sectors */
			rqd->ppa_list[j++] = p;
		}

next:
		if (advanced_bio)
			bio_advance(bio, PBLK_EXPOSED_PAGE_SIZE);
	}

	if (pblk_io_aligned(pblk, nr_secs))
		rqd->flags = pblk_set_read_mode(pblk, PBLK_READ_SEQUENTIAL);
	else
		rqd->flags = pblk_set_read_mode(pblk, PBLK_READ_RANDOM);

#ifdef CONFIG_NVM_DEBUG
	atomic_long_add(nr_secs, &pblk->inflight_reads);
#endif
}

static int pblk_submit_read_io(struct pblk *pblk, struct nvm_rq *rqd)
{
	int err;

	err = pblk_submit_io(pblk, rqd);
	if (err)
		return NVM_IO_ERR;

	return NVM_IO_OK;
}

static void pblk_end_io_read(struct nvm_rq *rqd)
{
	struct pblk *pblk = rqd->private;
	struct nvm_tgt_dev *dev = pblk->dev;
	struct pblk_g_ctx *r_ctx = nvm_rq_to_pdu(rqd);
	struct bio *bio = rqd->bio;

	if (rqd->error)
		pblk_log_read_err(pblk, rqd);
#ifdef CONFIG_NVM_DEBUG
	else
		WARN_ONCE(bio->bi_status, "pblk: corrupted read error\n");
#endif

	nvm_dev_dma_free(dev->parent, rqd->meta_list, rqd->dma_meta_list);

	bio_put(bio);
	if (r_ctx->private) {
		struct bio *orig_bio = r_ctx->private;

#ifdef CONFIG_NVM_DEBUG
		WARN_ONCE(orig_bio->bi_status, "pblk: corrupted read bio\n");
#endif
		bio_endio(orig_bio);
		bio_put(orig_bio);
	}

#ifdef CONFIG_NVM_DEBUG
	atomic_long_add(rqd->nr_ppas, &pblk->sync_reads);
	atomic_long_sub(rqd->nr_ppas, &pblk->inflight_reads);
#endif

	pblk_free_rqd(pblk, rqd, READ);
	atomic_dec(&pblk->inflight_io);
}

static int pblk_fill_partial_read_bio(struct pblk *pblk, struct nvm_rq *rqd,
				      unsigned int bio_init_idx,
				      unsigned long *read_bitmap)
{
	struct bio *new_bio, *bio = rqd->bio;
	struct bio_vec src_bv, dst_bv;
	void *ppa_ptr = NULL;
	void *src_p, *dst_p;
	dma_addr_t dma_ppa_list = 0;
	int nr_secs = rqd->nr_ppas;
	int nr_holes = nr_secs - bitmap_weight(read_bitmap, nr_secs);
	int i, ret, hole;
	DECLARE_COMPLETION_ONSTACK(wait);

	new_bio = bio_alloc(GFP_KERNEL, nr_holes);
	if (!new_bio) {
		pr_err("pblk: could not alloc read bio\n");
		return NVM_IO_ERR;
	}

	if (pblk_bio_add_pages(pblk, new_bio, GFP_KERNEL, nr_holes))
		goto err;

	if (nr_holes != new_bio->bi_vcnt) {
		pr_err("pblk: malformed bio\n");
		goto err;
	}

	new_bio->bi_iter.bi_sector = 0; /* internal bio */
	bio_set_op_attrs(new_bio, REQ_OP_READ, 0);
	new_bio->bi_private = &wait;
	new_bio->bi_end_io = pblk_end_bio_sync;

	rqd->bio = new_bio;
	rqd->nr_ppas = nr_holes;
	rqd->flags = pblk_set_read_mode(pblk, PBLK_READ_RANDOM);
	rqd->end_io = NULL;

	if (unlikely(nr_secs > 1 && nr_holes == 1)) {
		ppa_ptr = rqd->ppa_list;
		dma_ppa_list = rqd->dma_ppa_list;
		rqd->ppa_addr = rqd->ppa_list[0];
	}

	ret = pblk_submit_read_io(pblk, rqd);
	if (ret) {
		bio_put(rqd->bio);
		pr_err("pblk: read IO submission failed\n");
		goto err;
	}

	if (!wait_for_completion_io_timeout(&wait,
				msecs_to_jiffies(PBLK_COMMAND_TIMEOUT_MS))) {
		pr_err("pblk: partial read I/O timed out\n");
	}

	if (rqd->error) {
		atomic_long_inc(&pblk->read_failed);
#ifdef CONFIG_NVM_DEBUG
		pblk_print_failed_rqd(pblk, rqd, rqd->error);
#endif
	}

	if (unlikely(nr_secs > 1 && nr_holes == 1)) {
		rqd->ppa_list = ppa_ptr;
		rqd->dma_ppa_list = dma_ppa_list;
	}

	/* Fill the holes in the original bio */
	i = 0;
	hole = find_first_zero_bit(read_bitmap, nr_secs);
	do {
		src_bv = new_bio->bi_io_vec[i++];
		dst_bv = bio->bi_io_vec[bio_init_idx + hole];

		src_p = kmap_atomic(src_bv.bv_page);
		dst_p = kmap_atomic(dst_bv.bv_page);

		memcpy(dst_p + dst_bv.bv_offset,
			src_p + src_bv.bv_offset,
			PBLK_EXPOSED_PAGE_SIZE);

		kunmap_atomic(src_p);
		kunmap_atomic(dst_p);

		mempool_free(src_bv.bv_page, pblk->page_pool);

		hole = find_next_zero_bit(read_bitmap, nr_secs, hole + 1);
	} while (hole < nr_secs);

	bio_put(new_bio);

	/* Complete the original bio and associated request */
	rqd->bio = bio;
	rqd->nr_ppas = nr_secs;
	rqd->private = pblk;

	bio_endio(bio);
	pblk_end_io_read(rqd);
	return NVM_IO_OK;

err:
	/* Free allocated pages in new bio */
	pblk_bio_free_pages(pblk, bio, 0, new_bio->bi_vcnt);
	rqd->private = pblk;
	pblk_end_io_read(rqd);
	return NVM_IO_ERR;
}

static void pblk_read_rq(struct pblk *pblk, struct nvm_rq *rqd,
			 unsigned long *read_bitmap)
{
	struct bio *bio = rqd->bio;
	struct ppa_addr ppa;
	sector_t lba = pblk_get_lba(bio);

	/* logic error: lba out-of-bounds. Ignore read request */
	if (lba >= pblk->rl.nr_secs) {
		WARN(1, "pblk: read lba out of bounds\n");
		return;
	}

	pblk_lookup_l2p_seq(pblk, &ppa, lba, 1);

#ifdef CONFIG_NVM_DEBUG
	atomic_long_inc(&pblk->inflight_reads);
#endif

retry:
	if (pblk_ppa_empty(ppa)) {
		WARN_ON(test_and_set_bit(0, read_bitmap));
		return;
	}

	/* Try to read from write buffer. The address is later checked on the
	 * write buffer to prevent retrieving overwritten data.
	 */
	if (pblk_addr_in_cache(ppa)) {
		if (!pblk_read_from_cache(pblk, bio, lba, ppa, 0, 1)) {
			pblk_lookup_l2p_seq(pblk, &ppa, lba, 1);
			goto retry;
		}
		WARN_ON(test_and_set_bit(0, read_bitmap));
#ifdef CONFIG_NVM_DEBUG
			atomic_long_inc(&pblk->cache_reads);
#endif
	} else {
		rqd->ppa_addr = ppa;
	}

	rqd->flags = pblk_set_read_mode(pblk, PBLK_READ_RANDOM);
}

int pblk_submit_read(struct pblk *pblk, struct bio *bio)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	unsigned int nr_secs = pblk_get_secs(bio);
	struct nvm_rq *rqd;
	unsigned long read_bitmap; /* Max 64 ppas per request */
	unsigned int bio_init_idx;
	int ret = NVM_IO_ERR;

	if (nr_secs > PBLK_MAX_REQ_ADDRS)
		return NVM_IO_ERR;

	bitmap_zero(&read_bitmap, nr_secs);

	rqd = pblk_alloc_rqd(pblk, READ);
	if (IS_ERR(rqd)) {
		pr_err_ratelimited("pblk: not able to alloc rqd");
		return NVM_IO_ERR;
	}

	rqd->opcode = NVM_OP_PREAD;
	rqd->bio = bio;
	rqd->nr_ppas = nr_secs;
	rqd->private = pblk;
	rqd->end_io = pblk_end_io_read;

	/* Save the index for this bio's start. This is needed in case
	 * we need to fill a partial read.
	 */
	bio_init_idx = pblk_get_bi_idx(bio);

	rqd->meta_list = nvm_dev_dma_alloc(dev->parent, GFP_KERNEL,
							&rqd->dma_meta_list);
	if (!rqd->meta_list) {
		pr_err("pblk: not able to allocate ppa list\n");
		goto fail_rqd_free;
	}

	if (nr_secs > 1) {
		rqd->ppa_list = rqd->meta_list + pblk_dma_meta_size;
		rqd->dma_ppa_list = rqd->dma_meta_list + pblk_dma_meta_size;

		pblk_read_ppalist_rq(pblk, rqd, &read_bitmap);
	} else {
		pblk_read_rq(pblk, rqd, &read_bitmap);
	}

	bio_get(bio);
	if (bitmap_full(&read_bitmap, nr_secs)) {
		bio_endio(bio);
		atomic_inc(&pblk->inflight_io);
		pblk_end_io_read(rqd);
		return NVM_IO_OK;
	}

	/* All sectors are to be read from the device */
	if (bitmap_empty(&read_bitmap, rqd->nr_ppas)) {
		struct bio *int_bio = NULL;
		struct pblk_g_ctx *r_ctx = nvm_rq_to_pdu(rqd);

		/* Clone read bio to deal with read errors internally */
		int_bio = bio_clone_fast(bio, GFP_KERNEL, pblk_bio_set);
		if (!int_bio) {
			pr_err("pblk: could not clone read bio\n");
			return NVM_IO_ERR;
		}

		rqd->bio = int_bio;
		r_ctx->private = bio;

		ret = pblk_submit_read_io(pblk, rqd);
		if (ret) {
			pr_err("pblk: read IO submission failed\n");
			if (int_bio)
				bio_put(int_bio);
			return ret;
		}

		return NVM_IO_OK;
	}

	/* The read bio request could be partially filled by the write buffer,
	 * but there are some holes that need to be read from the drive.
	 */
	ret = pblk_fill_partial_read_bio(pblk, rqd, bio_init_idx, &read_bitmap);
	if (ret) {
		pr_err("pblk: failed to perform partial read\n");
		return ret;
	}

	return NVM_IO_OK;

fail_rqd_free:
	pblk_free_rqd(pblk, rqd, READ);
	return ret;
}

static int read_ppalist_rq_gc(struct pblk *pblk, struct nvm_rq *rqd,
			      struct pblk_line *line, u64 *lba_list,
			      unsigned int nr_secs)
{
	struct ppa_addr ppas[PBLK_MAX_REQ_ADDRS];
	int valid_secs = 0;
	int i;

	pblk_lookup_l2p_rand(pblk, ppas, lba_list, nr_secs);

	for (i = 0; i < nr_secs; i++) {
		if (pblk_addr_in_cache(ppas[i]) || ppas[i].g.blk != line->id ||
						pblk_ppa_empty(ppas[i])) {
			lba_list[i] = ADDR_EMPTY;
			continue;
		}

		rqd->ppa_list[valid_secs++] = ppas[i];
	}

#ifdef CONFIG_NVM_DEBUG
	atomic_long_add(valid_secs, &pblk->inflight_reads);
#endif
	return valid_secs;
}

static int read_rq_gc(struct pblk *pblk, struct nvm_rq *rqd,
		      struct pblk_line *line, sector_t lba)
{
	struct ppa_addr ppa;
	int valid_secs = 0;

	if (lba == ADDR_EMPTY)
		goto out;

	/* logic error: lba out-of-bounds */
	if (lba >= pblk->rl.nr_secs) {
		WARN(1, "pblk: read lba out of bounds\n");
		goto out;
	}

	spin_lock(&pblk->trans_lock);
	ppa = pblk_trans_map_get(pblk, lba);
	spin_unlock(&pblk->trans_lock);

	/* Ignore updated values until the moment */
	if (pblk_addr_in_cache(ppa) || ppa.g.blk != line->id ||
							pblk_ppa_empty(ppa))
		goto out;

	rqd->ppa_addr = ppa;
	valid_secs = 1;

#ifdef CONFIG_NVM_DEBUG
	atomic_long_inc(&pblk->inflight_reads);
#endif

out:
	return valid_secs;
}

int pblk_submit_read_gc(struct pblk *pblk, u64 *lba_list, void *data,
			unsigned int nr_secs, unsigned int *secs_to_gc,
			struct pblk_line *line)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	struct bio *bio;
	struct nvm_rq rqd;
	int ret, data_len;
	DECLARE_COMPLETION_ONSTACK(wait);

	memset(&rqd, 0, sizeof(struct nvm_rq));

	rqd.meta_list = nvm_dev_dma_alloc(dev->parent, GFP_KERNEL,
							&rqd.dma_meta_list);
	if (!rqd.meta_list)
		return NVM_IO_ERR;

	if (nr_secs > 1) {
		rqd.ppa_list = rqd.meta_list + pblk_dma_meta_size;
		rqd.dma_ppa_list = rqd.dma_meta_list + pblk_dma_meta_size;

		*secs_to_gc = read_ppalist_rq_gc(pblk, &rqd, line, lba_list,
								nr_secs);
		if (*secs_to_gc == 1)
			rqd.ppa_addr = rqd.ppa_list[0];
	} else {
		*secs_to_gc = read_rq_gc(pblk, &rqd, line, lba_list[0]);
	}

	if (!(*secs_to_gc))
		goto out;

	data_len = (*secs_to_gc) * geo->sec_size;
	bio = pblk_bio_map_addr(pblk, data, *secs_to_gc, data_len,
						PBLK_KMALLOC_META, GFP_KERNEL);
	if (IS_ERR(bio)) {
		pr_err("pblk: could not allocate GC bio (%lu)\n", PTR_ERR(bio));
		goto err_free_dma;
	}

	bio->bi_iter.bi_sector = 0; /* internal bio */
	bio_set_op_attrs(bio, REQ_OP_READ, 0);

	rqd.opcode = NVM_OP_PREAD;
	rqd.end_io = pblk_end_io_sync;
	rqd.private = &wait;
	rqd.nr_ppas = *secs_to_gc;
	rqd.flags = pblk_set_read_mode(pblk, PBLK_READ_RANDOM);
	rqd.bio = bio;

	ret = pblk_submit_read_io(pblk, &rqd);
	if (ret) {
		bio_endio(bio);
		pr_err("pblk: GC read request failed\n");
		goto err_free_dma;
	}

	if (!wait_for_completion_io_timeout(&wait,
				msecs_to_jiffies(PBLK_COMMAND_TIMEOUT_MS))) {
		pr_err("pblk: GC read I/O timed out\n");
	}
	atomic_dec(&pblk->inflight_io);

	if (rqd.error) {
		atomic_long_inc(&pblk->read_failed_gc);
#ifdef CONFIG_NVM_DEBUG
		pblk_print_failed_rqd(pblk, &rqd, rqd.error);
#endif
	}

#ifdef CONFIG_NVM_DEBUG
	atomic_long_add(*secs_to_gc, &pblk->sync_reads);
	atomic_long_add(*secs_to_gc, &pblk->recov_gc_reads);
	atomic_long_sub(*secs_to_gc, &pblk->inflight_reads);
#endif

out:
	nvm_dev_dma_free(dev->parent, rqd.meta_list, rqd.dma_meta_list);
	return NVM_IO_OK;

err_free_dma:
	nvm_dev_dma_free(dev->parent, rqd.meta_list, rqd.dma_meta_list);
	return NVM_IO_ERR;
}
