// SPDX-License-Identifier: GPL-2.0
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
#ifdef CONFIG_NVM_PBLK_DEBUG
	/* Callers must ensure that the ppa points to a cache address */
	BUG_ON(pblk_ppa_empty(ppa));
	BUG_ON(!pblk_addr_in_cache(ppa));
#endif

	return pblk_rb_copy_to_bio(&pblk->rwb, bio, lba, ppa,
						bio_iter, advanced_bio);
}

static void pblk_read_ppalist_rq(struct pblk *pblk, struct nvm_rq *rqd,
				 struct bio *bio, sector_t blba,
				 unsigned long *read_bitmap)
{
	void *meta_list = rqd->meta_list;
	struct ppa_addr ppas[NVM_MAX_VLBA];
	int nr_secs = rqd->nr_ppas;
	bool advanced_bio = false;
	int i, j = 0;

	pblk_lookup_l2p_seq(pblk, ppas, blba, nr_secs);

	for (i = 0; i < nr_secs; i++) {
		struct ppa_addr p = ppas[i];
		struct pblk_sec_meta *meta = pblk_get_meta(pblk, meta_list, i);
		sector_t lba = blba + i;

retry:
		if (pblk_ppa_empty(p)) {
			__le64 addr_empty = cpu_to_le64(ADDR_EMPTY);

			WARN_ON(test_and_set_bit(i, read_bitmap));
			meta->lba = addr_empty;

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
			meta->lba = cpu_to_le64(lba);
			advanced_bio = true;
#ifdef CONFIG_NVM_PBLK_DEBUG
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
		rqd->is_seq = 1;

#ifdef CONFIG_NVM_PBLK_DEBUG
	atomic_long_add(nr_secs, &pblk->inflight_reads);
#endif
}


static void pblk_read_check_seq(struct pblk *pblk, struct nvm_rq *rqd,
				sector_t blba)
{
	void *meta_list = rqd->meta_list;
	int nr_lbas = rqd->nr_ppas;
	int i;

	if (!pblk_is_oob_meta_supported(pblk))
		return;

	for (i = 0; i < nr_lbas; i++) {
		struct pblk_sec_meta *meta = pblk_get_meta(pblk, meta_list, i);
		u64 lba = le64_to_cpu(meta->lba);

		if (lba == ADDR_EMPTY)
			continue;

		if (lba != blba + i) {
#ifdef CONFIG_NVM_PBLK_DEBUG
			struct ppa_addr *ppa_list = nvm_rq_to_ppa_list(rqd);

			print_ppa(pblk, &ppa_list[i], "seq", i);
#endif
			pblk_err(pblk, "corrupted read LBA (%llu/%llu)\n",
							lba, (u64)blba + i);
			WARN_ON(1);
		}
	}
}

/*
 * There can be holes in the lba list.
 */
static void pblk_read_check_rand(struct pblk *pblk, struct nvm_rq *rqd,
				 u64 *lba_list, int nr_lbas)
{
	void *meta_lba_list = rqd->meta_list;
	int i, j;

	if (!pblk_is_oob_meta_supported(pblk))
		return;

	for (i = 0, j = 0; i < nr_lbas; i++) {
		struct pblk_sec_meta *meta = pblk_get_meta(pblk,
							   meta_lba_list, j);
		u64 lba = lba_list[i];
		u64 meta_lba;

		if (lba == ADDR_EMPTY)
			continue;

		meta_lba = le64_to_cpu(meta->lba);

		if (lba != meta_lba) {
#ifdef CONFIG_NVM_PBLK_DEBUG
			struct ppa_addr *ppa_list = nvm_rq_to_ppa_list(rqd);

			print_ppa(pblk, &ppa_list[j], "rnd", j);
#endif
			pblk_err(pblk, "corrupted read LBA (%llu/%llu)\n",
							meta_lba, lba);
			WARN_ON(1);
		}

		j++;
	}

	WARN_ONCE(j != rqd->nr_ppas, "pblk: corrupted random request\n");
}

static void pblk_end_user_read(struct bio *bio)
{
#ifdef CONFIG_NVM_PBLK_DEBUG
	WARN_ONCE(bio->bi_status, "pblk: corrupted read bio\n");
#endif
	bio_endio(bio);
}

static void __pblk_end_io_read(struct pblk *pblk, struct nvm_rq *rqd,
			       bool put_line)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct pblk_g_ctx *r_ctx = nvm_rq_to_pdu(rqd);
	struct bio *int_bio = rqd->bio;
	unsigned long start_time = r_ctx->start_time;

	generic_end_io_acct(dev->q, REQ_OP_READ, &pblk->disk->part0, start_time);

	if (rqd->error)
		pblk_log_read_err(pblk, rqd);

	pblk_read_check_seq(pblk, rqd, r_ctx->lba);

	if (int_bio)
		bio_put(int_bio);

	if (put_line)
		pblk_rq_to_line_put(pblk, rqd);

#ifdef CONFIG_NVM_PBLK_DEBUG
	atomic_long_add(rqd->nr_ppas, &pblk->sync_reads);
	atomic_long_sub(rqd->nr_ppas, &pblk->inflight_reads);
#endif

	pblk_free_rqd(pblk, rqd, PBLK_READ);
	atomic_dec(&pblk->inflight_io);
}

static void pblk_end_io_read(struct nvm_rq *rqd)
{
	struct pblk *pblk = rqd->private;
	struct pblk_g_ctx *r_ctx = nvm_rq_to_pdu(rqd);
	struct bio *bio = (struct bio *)r_ctx->private;

	pblk_end_user_read(bio);
	__pblk_end_io_read(pblk, rqd, true);
}

static void pblk_end_partial_read(struct nvm_rq *rqd)
{
	struct pblk *pblk = rqd->private;
	struct pblk_g_ctx *r_ctx = nvm_rq_to_pdu(rqd);
	struct pblk_pr_ctx *pr_ctx = r_ctx->private;
	struct pblk_sec_meta *meta;
	struct bio *new_bio = rqd->bio;
	struct bio *bio = pr_ctx->orig_bio;
	struct bio_vec src_bv, dst_bv;
	void *meta_list = rqd->meta_list;
	int bio_init_idx = pr_ctx->bio_init_idx;
	unsigned long *read_bitmap = pr_ctx->bitmap;
	int nr_secs = pr_ctx->orig_nr_secs;
	int nr_holes = nr_secs - bitmap_weight(read_bitmap, nr_secs);
	void *src_p, *dst_p;
	int hole, i;

	if (unlikely(nr_holes == 1)) {
		struct ppa_addr ppa;

		ppa = rqd->ppa_addr;
		rqd->ppa_list = pr_ctx->ppa_ptr;
		rqd->dma_ppa_list = pr_ctx->dma_ppa_list;
		rqd->ppa_list[0] = ppa;
	}

	for (i = 0; i < nr_secs; i++) {
		meta = pblk_get_meta(pblk, meta_list, i);
		pr_ctx->lba_list_media[i] = le64_to_cpu(meta->lba);
		meta->lba = cpu_to_le64(pr_ctx->lba_list_mem[i]);
	}

	/* Fill the holes in the original bio */
	i = 0;
	hole = find_first_zero_bit(read_bitmap, nr_secs);
	do {
		struct pblk_line *line;

		line = pblk_ppa_to_line(pblk, rqd->ppa_list[i]);
		kref_put(&line->ref, pblk_line_put);

		meta = pblk_get_meta(pblk, meta_list, hole);
		meta->lba = cpu_to_le64(pr_ctx->lba_list_media[i]);

		src_bv = new_bio->bi_io_vec[i++];
		dst_bv = bio->bi_io_vec[bio_init_idx + hole];

		src_p = kmap_atomic(src_bv.bv_page);
		dst_p = kmap_atomic(dst_bv.bv_page);

		memcpy(dst_p + dst_bv.bv_offset,
			src_p + src_bv.bv_offset,
			PBLK_EXPOSED_PAGE_SIZE);

		kunmap_atomic(src_p);
		kunmap_atomic(dst_p);

		mempool_free(src_bv.bv_page, &pblk->page_bio_pool);

		hole = find_next_zero_bit(read_bitmap, nr_secs, hole + 1);
	} while (hole < nr_secs);

	bio_put(new_bio);
	kfree(pr_ctx);

	/* restore original request */
	rqd->bio = NULL;
	rqd->nr_ppas = nr_secs;

	bio_endio(bio);
	__pblk_end_io_read(pblk, rqd, false);
}

static int pblk_setup_partial_read(struct pblk *pblk, struct nvm_rq *rqd,
			    unsigned int bio_init_idx,
			    unsigned long *read_bitmap,
			    int nr_holes)
{
	void *meta_list = rqd->meta_list;
	struct pblk_g_ctx *r_ctx = nvm_rq_to_pdu(rqd);
	struct pblk_pr_ctx *pr_ctx;
	struct bio *new_bio, *bio = r_ctx->private;
	int nr_secs = rqd->nr_ppas;
	int i;

	new_bio = bio_alloc(GFP_KERNEL, nr_holes);

	if (pblk_bio_add_pages(pblk, new_bio, GFP_KERNEL, nr_holes))
		goto fail_bio_put;

	if (nr_holes != new_bio->bi_vcnt) {
		WARN_ONCE(1, "pblk: malformed bio\n");
		goto fail_free_pages;
	}

	pr_ctx = kzalloc(sizeof(struct pblk_pr_ctx), GFP_KERNEL);
	if (!pr_ctx)
		goto fail_free_pages;

	for (i = 0; i < nr_secs; i++) {
		struct pblk_sec_meta *meta = pblk_get_meta(pblk, meta_list, i);

		pr_ctx->lba_list_mem[i] = le64_to_cpu(meta->lba);
	}

	new_bio->bi_iter.bi_sector = 0; /* internal bio */
	bio_set_op_attrs(new_bio, REQ_OP_READ, 0);

	rqd->bio = new_bio;
	rqd->nr_ppas = nr_holes;

	pr_ctx->orig_bio = bio;
	bitmap_copy(pr_ctx->bitmap, read_bitmap, NVM_MAX_VLBA);
	pr_ctx->bio_init_idx = bio_init_idx;
	pr_ctx->orig_nr_secs = nr_secs;
	r_ctx->private = pr_ctx;

	if (unlikely(nr_holes == 1)) {
		pr_ctx->ppa_ptr = rqd->ppa_list;
		pr_ctx->dma_ppa_list = rqd->dma_ppa_list;
		rqd->ppa_addr = rqd->ppa_list[0];
	}
	return 0;

fail_free_pages:
	pblk_bio_free_pages(pblk, new_bio, 0, new_bio->bi_vcnt);
fail_bio_put:
	bio_put(new_bio);

	return -ENOMEM;
}

static int pblk_partial_read_bio(struct pblk *pblk, struct nvm_rq *rqd,
				 unsigned int bio_init_idx,
				 unsigned long *read_bitmap, int nr_secs)
{
	int nr_holes;
	int ret;

	nr_holes = nr_secs - bitmap_weight(read_bitmap, nr_secs);

	if (pblk_setup_partial_read(pblk, rqd, bio_init_idx, read_bitmap,
				    nr_holes))
		return NVM_IO_ERR;

	rqd->end_io = pblk_end_partial_read;

	ret = pblk_submit_io(pblk, rqd);
	if (ret) {
		bio_put(rqd->bio);
		pblk_err(pblk, "partial read IO submission failed\n");
		goto err;
	}

	return NVM_IO_OK;

err:
	pblk_err(pblk, "failed to perform partial read\n");

	/* Free allocated pages in new bio */
	pblk_bio_free_pages(pblk, rqd->bio, 0, rqd->bio->bi_vcnt);
	__pblk_end_io_read(pblk, rqd, false);
	return NVM_IO_ERR;
}

static void pblk_read_rq(struct pblk *pblk, struct nvm_rq *rqd, struct bio *bio,
			 sector_t lba, unsigned long *read_bitmap)
{
	struct pblk_sec_meta *meta = pblk_get_meta(pblk, rqd->meta_list, 0);
	struct ppa_addr ppa;

	pblk_lookup_l2p_seq(pblk, &ppa, lba, 1);

#ifdef CONFIG_NVM_PBLK_DEBUG
	atomic_long_inc(&pblk->inflight_reads);
#endif

retry:
	if (pblk_ppa_empty(ppa)) {
		__le64 addr_empty = cpu_to_le64(ADDR_EMPTY);

		WARN_ON(test_and_set_bit(0, read_bitmap));
		meta->lba = addr_empty;
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
		meta->lba = cpu_to_le64(lba);

#ifdef CONFIG_NVM_PBLK_DEBUG
		atomic_long_inc(&pblk->cache_reads);
#endif
	} else {
		rqd->ppa_addr = ppa;
	}
}

int pblk_submit_read(struct pblk *pblk, struct bio *bio)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct request_queue *q = dev->q;
	sector_t blba = pblk_get_lba(bio);
	unsigned int nr_secs = pblk_get_secs(bio);
	struct pblk_g_ctx *r_ctx;
	struct nvm_rq *rqd;
	unsigned int bio_init_idx;
	DECLARE_BITMAP(read_bitmap, NVM_MAX_VLBA);
	int ret = NVM_IO_ERR;

	generic_start_io_acct(q, REQ_OP_READ, bio_sectors(bio),
			      &pblk->disk->part0);

	bitmap_zero(read_bitmap, nr_secs);

	rqd = pblk_alloc_rqd(pblk, PBLK_READ);

	rqd->opcode = NVM_OP_PREAD;
	rqd->nr_ppas = nr_secs;
	rqd->bio = NULL; /* cloned bio if needed */
	rqd->private = pblk;
	rqd->end_io = pblk_end_io_read;

	r_ctx = nvm_rq_to_pdu(rqd);
	r_ctx->start_time = jiffies;
	r_ctx->lba = blba;
	r_ctx->private = bio; /* original bio */

	/* Save the index for this bio's start. This is needed in case
	 * we need to fill a partial read.
	 */
	bio_init_idx = pblk_get_bi_idx(bio);

	if (pblk_alloc_rqd_meta(pblk, rqd))
		goto fail_rqd_free;

	if (nr_secs > 1)
		pblk_read_ppalist_rq(pblk, rqd, bio, blba, read_bitmap);
	else
		pblk_read_rq(pblk, rqd, bio, blba, read_bitmap);

	if (bitmap_full(read_bitmap, nr_secs)) {
		atomic_inc(&pblk->inflight_io);
		__pblk_end_io_read(pblk, rqd, false);
		return NVM_IO_DONE;
	}

	/* All sectors are to be read from the device */
	if (bitmap_empty(read_bitmap, rqd->nr_ppas)) {
		struct bio *int_bio = NULL;

		/* Clone read bio to deal with read errors internally */
		int_bio = bio_clone_fast(bio, GFP_KERNEL, &pblk_bio_set);
		if (!int_bio) {
			pblk_err(pblk, "could not clone read bio\n");
			goto fail_end_io;
		}

		rqd->bio = int_bio;

		if (pblk_submit_io(pblk, rqd)) {
			pblk_err(pblk, "read IO submission failed\n");
			ret = NVM_IO_ERR;
			goto fail_end_io;
		}

		return NVM_IO_OK;
	}

	/* The read bio request could be partially filled by the write buffer,
	 * but there are some holes that need to be read from the drive.
	 */
	ret = pblk_partial_read_bio(pblk, rqd, bio_init_idx, read_bitmap,
				    nr_secs);
	if (ret)
		goto fail_meta_free;

	return NVM_IO_OK;

fail_meta_free:
	nvm_dev_dma_free(dev->parent, rqd->meta_list, rqd->dma_meta_list);
fail_rqd_free:
	pblk_free_rqd(pblk, rqd, PBLK_READ);
	return ret;
fail_end_io:
	__pblk_end_io_read(pblk, rqd, false);
	return ret;
}

static int read_ppalist_rq_gc(struct pblk *pblk, struct nvm_rq *rqd,
			      struct pblk_line *line, u64 *lba_list,
			      u64 *paddr_list_gc, unsigned int nr_secs)
{
	struct ppa_addr ppa_list_l2p[NVM_MAX_VLBA];
	struct ppa_addr ppa_gc;
	int valid_secs = 0;
	int i;

	pblk_lookup_l2p_rand(pblk, ppa_list_l2p, lba_list, nr_secs);

	for (i = 0; i < nr_secs; i++) {
		if (lba_list[i] == ADDR_EMPTY)
			continue;

		ppa_gc = addr_to_gen_ppa(pblk, paddr_list_gc[i], line->id);
		if (!pblk_ppa_comp(ppa_list_l2p[i], ppa_gc)) {
			paddr_list_gc[i] = lba_list[i] = ADDR_EMPTY;
			continue;
		}

		rqd->ppa_list[valid_secs++] = ppa_list_l2p[i];
	}

#ifdef CONFIG_NVM_PBLK_DEBUG
	atomic_long_add(valid_secs, &pblk->inflight_reads);
#endif

	return valid_secs;
}

static int read_rq_gc(struct pblk *pblk, struct nvm_rq *rqd,
		      struct pblk_line *line, sector_t lba,
		      u64 paddr_gc)
{
	struct ppa_addr ppa_l2p, ppa_gc;
	int valid_secs = 0;

	if (lba == ADDR_EMPTY)
		goto out;

	/* logic error: lba out-of-bounds */
	if (lba >= pblk->rl.nr_secs) {
		WARN(1, "pblk: read lba out of bounds\n");
		goto out;
	}

	spin_lock(&pblk->trans_lock);
	ppa_l2p = pblk_trans_map_get(pblk, lba);
	spin_unlock(&pblk->trans_lock);

	ppa_gc = addr_to_gen_ppa(pblk, paddr_gc, line->id);
	if (!pblk_ppa_comp(ppa_l2p, ppa_gc))
		goto out;

	rqd->ppa_addr = ppa_l2p;
	valid_secs = 1;

#ifdef CONFIG_NVM_PBLK_DEBUG
	atomic_long_inc(&pblk->inflight_reads);
#endif

out:
	return valid_secs;
}

int pblk_submit_read_gc(struct pblk *pblk, struct pblk_gc_rq *gc_rq)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	struct bio *bio;
	struct nvm_rq rqd;
	int data_len;
	int ret = NVM_IO_OK;

	memset(&rqd, 0, sizeof(struct nvm_rq));

	ret = pblk_alloc_rqd_meta(pblk, &rqd);
	if (ret)
		return ret;

	if (gc_rq->nr_secs > 1) {
		gc_rq->secs_to_gc = read_ppalist_rq_gc(pblk, &rqd, gc_rq->line,
							gc_rq->lba_list,
							gc_rq->paddr_list,
							gc_rq->nr_secs);
		if (gc_rq->secs_to_gc == 1)
			rqd.ppa_addr = rqd.ppa_list[0];
	} else {
		gc_rq->secs_to_gc = read_rq_gc(pblk, &rqd, gc_rq->line,
							gc_rq->lba_list[0],
							gc_rq->paddr_list[0]);
	}

	if (!(gc_rq->secs_to_gc))
		goto out;

	data_len = (gc_rq->secs_to_gc) * geo->csecs;
	bio = pblk_bio_map_addr(pblk, gc_rq->data, gc_rq->secs_to_gc, data_len,
						PBLK_VMALLOC_META, GFP_KERNEL);
	if (IS_ERR(bio)) {
		pblk_err(pblk, "could not allocate GC bio (%lu)\n",
								PTR_ERR(bio));
		ret = PTR_ERR(bio);
		goto err_free_dma;
	}

	bio->bi_iter.bi_sector = 0; /* internal bio */
	bio_set_op_attrs(bio, REQ_OP_READ, 0);

	rqd.opcode = NVM_OP_PREAD;
	rqd.nr_ppas = gc_rq->secs_to_gc;
	rqd.bio = bio;

	if (pblk_submit_io_sync(pblk, &rqd)) {
		ret = -EIO;
		pblk_err(pblk, "GC read request failed\n");
		goto err_free_bio;
	}

	pblk_read_check_rand(pblk, &rqd, gc_rq->lba_list, gc_rq->nr_secs);

	atomic_dec(&pblk->inflight_io);

	if (rqd.error) {
		atomic_long_inc(&pblk->read_failed_gc);
#ifdef CONFIG_NVM_PBLK_DEBUG
		pblk_print_failed_rqd(pblk, &rqd, rqd.error);
#endif
	}

#ifdef CONFIG_NVM_PBLK_DEBUG
	atomic_long_add(gc_rq->secs_to_gc, &pblk->sync_reads);
	atomic_long_add(gc_rq->secs_to_gc, &pblk->recov_gc_reads);
	atomic_long_sub(gc_rq->secs_to_gc, &pblk->inflight_reads);
#endif

out:
	pblk_free_rqd_meta(pblk, &rqd);
	return ret;

err_free_bio:
	bio_put(bio);
err_free_dma:
	pblk_free_rqd_meta(pblk, &rqd);
	return ret;
}
