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
				sector_t lba, struct ppa_addr ppa)
{
#ifdef CONFIG_NVM_PBLK_DEBUG
	/* Callers must ensure that the ppa points to a cache address */
	BUG_ON(pblk_ppa_empty(ppa));
	BUG_ON(!pblk_addr_in_cache(ppa));
#endif

	return pblk_rb_copy_to_bio(&pblk->rwb, bio, lba, ppa);
}

static int pblk_read_ppalist_rq(struct pblk *pblk, struct nvm_rq *rqd,
				 struct bio *bio, sector_t blba,
				 bool *from_cache)
{
	void *meta_list = rqd->meta_list;
	int nr_secs, i;

retry:
	nr_secs = pblk_lookup_l2p_seq(pblk, rqd->ppa_list, blba, rqd->nr_ppas,
					from_cache);

	if (!*from_cache)
		goto end;

	for (i = 0; i < nr_secs; i++) {
		struct pblk_sec_meta *meta = pblk_get_meta(pblk, meta_list, i);
		sector_t lba = blba + i;

		if (pblk_ppa_empty(rqd->ppa_list[i])) {
			__le64 addr_empty = cpu_to_le64(ADDR_EMPTY);

			meta->lba = addr_empty;
		} else if (pblk_addr_in_cache(rqd->ppa_list[i])) {
			/*
			 * Try to read from write buffer. The address is later
			 * checked on the write buffer to prevent retrieving
			 * overwritten data.
			 */
			if (!pblk_read_from_cache(pblk, bio, lba,
							rqd->ppa_list[i])) {
				if (i == 0) {
					/*
					 * We didn't call with bio_advance()
					 * yet, so we can just retry.
					 */
					goto retry;
				} else {
					/*
					 * We already call bio_advance()
					 * so we cannot retry and we need
					 * to quit that function in order
					 * to allow caller to handle the bio
					 * splitting in the current sector
					 * position.
					 */
					nr_secs = i;
					goto end;
				}
			}
			meta->lba = cpu_to_le64(lba);
#ifdef CONFIG_NVM_PBLK_DEBUG
			atomic_long_inc(&pblk->cache_reads);
#endif
		}
		bio_advance(bio, PBLK_EXPOSED_PAGE_SIZE);
	}

end:
	if (pblk_io_aligned(pblk, nr_secs))
		rqd->is_seq = 1;

#ifdef CONFIG_NVM_PBLK_DEBUG
	atomic_long_add(nr_secs, &pblk->inflight_reads);
#endif

	return nr_secs;
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

static void pblk_end_user_read(struct bio *bio, int error)
{
	if (error && error != NVM_RSP_WARN_HIGHECC)
		bio_io_error(bio);
	else
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

	pblk_end_user_read(bio, rqd->error);
	__pblk_end_io_read(pblk, rqd, true);
}

static void pblk_read_rq(struct pblk *pblk, struct nvm_rq *rqd, struct bio *bio,
			 sector_t lba, bool *from_cache)
{
	struct pblk_sec_meta *meta = pblk_get_meta(pblk, rqd->meta_list, 0);
	struct ppa_addr ppa;

	pblk_lookup_l2p_seq(pblk, &ppa, lba, 1, from_cache);

#ifdef CONFIG_NVM_PBLK_DEBUG
	atomic_long_inc(&pblk->inflight_reads);
#endif

retry:
	if (pblk_ppa_empty(ppa)) {
		__le64 addr_empty = cpu_to_le64(ADDR_EMPTY);

		meta->lba = addr_empty;
		return;
	}

	/* Try to read from write buffer. The address is later checked on the
	 * write buffer to prevent retrieving overwritten data.
	 */
	if (pblk_addr_in_cache(ppa)) {
		if (!pblk_read_from_cache(pblk, bio, lba, ppa)) {
			pblk_lookup_l2p_seq(pblk, &ppa, lba, 1, from_cache);
			goto retry;
		}

		meta->lba = cpu_to_le64(lba);

#ifdef CONFIG_NVM_PBLK_DEBUG
		atomic_long_inc(&pblk->cache_reads);
#endif
	} else {
		rqd->ppa_addr = ppa;
	}
}

void pblk_submit_read(struct pblk *pblk, struct bio *bio)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct request_queue *q = dev->q;
	sector_t blba = pblk_get_lba(bio);
	unsigned int nr_secs = pblk_get_secs(bio);
	bool from_cache;
	struct pblk_g_ctx *r_ctx;
	struct nvm_rq *rqd;
	struct bio *int_bio, *split_bio;

	generic_start_io_acct(q, REQ_OP_READ, bio_sectors(bio),
			      &pblk->disk->part0);

	rqd = pblk_alloc_rqd(pblk, PBLK_READ);

	rqd->opcode = NVM_OP_PREAD;
	rqd->nr_ppas = nr_secs;
	rqd->private = pblk;
	rqd->end_io = pblk_end_io_read;

	r_ctx = nvm_rq_to_pdu(rqd);
	r_ctx->start_time = jiffies;
	r_ctx->lba = blba;

	if (pblk_alloc_rqd_meta(pblk, rqd)) {
		bio_io_error(bio);
		pblk_free_rqd(pblk, rqd, PBLK_READ);
		return;
	}

	/* Clone read bio to deal internally with:
	 * -read errors when reading from drive
	 * -bio_advance() calls during cache reads
	 */
	int_bio = bio_clone_fast(bio, GFP_KERNEL, &pblk_bio_set);

	if (nr_secs > 1)
		nr_secs = pblk_read_ppalist_rq(pblk, rqd, int_bio, blba,
						&from_cache);
	else
		pblk_read_rq(pblk, rqd, int_bio, blba, &from_cache);

split_retry:
	r_ctx->private = bio; /* original bio */
	rqd->bio = int_bio; /* internal bio */

	if (from_cache && nr_secs == rqd->nr_ppas) {
		/* All data was read from cache, we can complete the IO. */
		pblk_end_user_read(bio, 0);
		atomic_inc(&pblk->inflight_io);
		__pblk_end_io_read(pblk, rqd, false);
	} else if (nr_secs != rqd->nr_ppas) {
		/* The read bio request could be partially filled by the write
		 * buffer, but there are some holes that need to be read from
		 * the drive. In order to handle this, we will use block layer
		 * mechanism to split this request in to smaller ones and make
		 * a chain of it.
		 */
		split_bio = bio_split(bio, nr_secs * NR_PHY_IN_LOG, GFP_KERNEL,
					&pblk_bio_set);
		bio_chain(split_bio, bio);
		generic_make_request(bio);

		/* New bio contains first N sectors of the previous one, so
		 * we can continue to use existing rqd, but we need to shrink
		 * the number of PPAs in it. New bio is also guaranteed that
		 * it contains only either data from cache or from drive, newer
		 * mix of them.
		 */
		bio = split_bio;
		rqd->nr_ppas = nr_secs;
		if (rqd->nr_ppas == 1)
			rqd->ppa_addr = rqd->ppa_list[0];

		/* Recreate int_bio - existing might have some needed internal
		 * fields modified already.
		 */
		bio_put(int_bio);
		int_bio = bio_clone_fast(bio, GFP_KERNEL, &pblk_bio_set);
		goto split_retry;
	} else if (pblk_submit_io(pblk, rqd)) {
		/* Submitting IO to drive failed, let's report an error */
		rqd->error = -ENODEV;
		pblk_end_io_read(rqd);
	}
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
	if (lba >= pblk->capacity) {
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
