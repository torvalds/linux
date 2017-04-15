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
 * pblk-write.c - pblk's write path from write buffer to media
 */

#include "pblk.h"

static void pblk_sync_line(struct pblk *pblk, struct pblk_line *line)
{
#ifdef CONFIG_NVM_DEBUG
	atomic_long_inc(&pblk->sync_writes);
#endif

	/* Counter protected by rb sync lock */
	line->left_ssecs--;
	if (!line->left_ssecs)
		pblk_line_run_ws(pblk, line, NULL, pblk_line_close_ws);
}

static unsigned long pblk_end_w_bio(struct pblk *pblk, struct nvm_rq *rqd,
				    struct pblk_c_ctx *c_ctx)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct bio *original_bio;
	unsigned long ret;
	int i;

	for (i = 0; i < c_ctx->nr_valid; i++) {
		struct pblk_w_ctx *w_ctx;
		struct ppa_addr p;
		struct pblk_line *line;

		w_ctx = pblk_rb_w_ctx(&pblk->rwb, c_ctx->sentry + i);

		p = rqd->ppa_list[i];
		line = &pblk->lines[pblk_dev_ppa_to_line(p)];
		pblk_sync_line(pblk, line);

		while ((original_bio = bio_list_pop(&w_ctx->bios)))
			bio_endio(original_bio);
	}

#ifdef CONFIG_NVM_DEBUG
	atomic_long_add(c_ctx->nr_valid, &pblk->compl_writes);
#endif

	ret = pblk_rb_sync_advance(&pblk->rwb, c_ctx->nr_valid);

	if (rqd->meta_list)
		nvm_dev_dma_free(dev->parent, rqd->meta_list,
							rqd->dma_meta_list);

	bio_put(rqd->bio);
	pblk_free_rqd(pblk, rqd, WRITE);

	return ret;
}

static unsigned long pblk_end_queued_w_bio(struct pblk *pblk,
					   struct nvm_rq *rqd,
					   struct pblk_c_ctx *c_ctx)
{
	list_del(&c_ctx->list);
	return pblk_end_w_bio(pblk, rqd, c_ctx);
}

static void pblk_complete_write(struct pblk *pblk, struct nvm_rq *rqd,
				struct pblk_c_ctx *c_ctx)
{
	struct pblk_c_ctx *c, *r;
	unsigned long flags;
	unsigned long pos;

#ifdef CONFIG_NVM_DEBUG
	atomic_long_sub(c_ctx->nr_valid, &pblk->inflight_writes);
#endif

	pblk_up_rq(pblk, rqd->ppa_list, rqd->nr_ppas, c_ctx->lun_bitmap);

	pos = pblk_rb_sync_init(&pblk->rwb, &flags);
	if (pos == c_ctx->sentry) {
		pos = pblk_end_w_bio(pblk, rqd, c_ctx);

retry:
		list_for_each_entry_safe(c, r, &pblk->compl_list, list) {
			rqd = nvm_rq_from_c_ctx(c);
			if (c->sentry == pos) {
				pos = pblk_end_queued_w_bio(pblk, rqd, c);
				goto retry;
			}
		}
	} else {
		WARN_ON(nvm_rq_from_c_ctx(c_ctx) != rqd);
		list_add_tail(&c_ctx->list, &pblk->compl_list);
	}
	pblk_rb_sync_end(&pblk->rwb, &flags);
}

/* When a write fails, we are not sure whether the block has grown bad or a page
 * range is more susceptible to write errors. If a high number of pages fail, we
 * assume that the block is bad and we mark it accordingly. In all cases, we
 * remap and resubmit the failed entries as fast as possible; if a flush is
 * waiting on a completion, the whole stack would stall otherwise.
 */
static void pblk_end_w_fail(struct pblk *pblk, struct nvm_rq *rqd)
{
	void *comp_bits = &rqd->ppa_status;
	struct pblk_c_ctx *c_ctx = nvm_rq_to_pdu(rqd);
	struct pblk_rec_ctx *recovery;
	struct ppa_addr *ppa_list = rqd->ppa_list;
	int nr_ppas = rqd->nr_ppas;
	unsigned int c_entries;
	int bit, ret;

	if (unlikely(nr_ppas == 1))
		ppa_list = &rqd->ppa_addr;

	recovery = mempool_alloc(pblk->rec_pool, GFP_ATOMIC);
	if (!recovery) {
		pr_err("pblk: could not allocate recovery context\n");
		return;
	}
	INIT_LIST_HEAD(&recovery->failed);

	bit = -1;
	while ((bit = find_next_bit(comp_bits, nr_ppas, bit + 1)) < nr_ppas) {
		struct pblk_rb_entry *entry;
		struct ppa_addr ppa;

		/* Logic error */
		if (bit > c_ctx->nr_valid) {
			WARN_ONCE(1, "pblk: corrupted write request\n");
			goto out;
		}

		ppa = ppa_list[bit];
		entry = pblk_rb_sync_scan_entry(&pblk->rwb, &ppa);
		if (!entry) {
			pr_err("pblk: could not scan entry on write failure\n");
			goto out;
		}

		/* The list is filled first and emptied afterwards. No need for
		 * protecting it with a lock
		 */
		list_add_tail(&entry->index, &recovery->failed);
	}

	c_entries = find_first_bit(comp_bits, nr_ppas);
	ret = pblk_recov_setup_rq(pblk, c_ctx, recovery, comp_bits, c_entries);
	if (ret) {
		pr_err("pblk: could not recover from write failure\n");
		goto out;
	}

	INIT_WORK(&recovery->ws_rec, pblk_submit_rec);
	queue_work(pblk->kw_wq, &recovery->ws_rec);

out:
	pblk_complete_write(pblk, rqd, c_ctx);
}

static void pblk_end_io_write(struct nvm_rq *rqd)
{
	struct pblk *pblk = rqd->private;
	struct pblk_c_ctx *c_ctx = nvm_rq_to_pdu(rqd);

	if (rqd->error) {
		pblk_log_write_err(pblk, rqd);
		return pblk_end_w_fail(pblk, rqd);
	}
#ifdef CONFIG_NVM_DEBUG
	else
		WARN_ONCE(rqd->bio->bi_error, "pblk: corrupted write error\n");
#endif

	pblk_complete_write(pblk, rqd, c_ctx);
}

static int pblk_alloc_w_rq(struct pblk *pblk, struct nvm_rq *rqd,
			   unsigned int nr_secs)
{
	struct nvm_tgt_dev *dev = pblk->dev;

	/* Setup write request */
	rqd->opcode = NVM_OP_PWRITE;
	rqd->nr_ppas = nr_secs;
	rqd->flags = pblk_set_progr_mode(pblk, WRITE);
	rqd->private = pblk;
	rqd->end_io = pblk_end_io_write;

	rqd->meta_list = nvm_dev_dma_alloc(dev->parent, GFP_KERNEL,
							&rqd->dma_meta_list);
	if (!rqd->meta_list)
		return -ENOMEM;

	if (unlikely(nr_secs == 1))
		return 0;

	rqd->ppa_list = rqd->meta_list + pblk_dma_meta_size;
	rqd->dma_ppa_list = rqd->dma_meta_list + pblk_dma_meta_size;

	return 0;
}

static int pblk_setup_w_rq(struct pblk *pblk, struct nvm_rq *rqd,
			   struct pblk_c_ctx *c_ctx)
{
	struct pblk_line_meta *lm = &pblk->lm;
	struct pblk_line *e_line = pblk_line_get_data_next(pblk);
	struct ppa_addr erase_ppa;
	unsigned int valid = c_ctx->nr_valid;
	unsigned int padded = c_ctx->nr_padded;
	unsigned int nr_secs = valid + padded;
	unsigned long *lun_bitmap;
	int ret = 0;

	lun_bitmap = kzalloc(lm->lun_bitmap_len, GFP_KERNEL);
	if (!lun_bitmap) {
		ret = -ENOMEM;
		goto out;
	}
	c_ctx->lun_bitmap = lun_bitmap;

	ret = pblk_alloc_w_rq(pblk, rqd, nr_secs);
	if (ret) {
		kfree(lun_bitmap);
		goto out;
	}

	ppa_set_empty(&erase_ppa);
	if (likely(!e_line || !e_line->left_eblks))
		pblk_map_rq(pblk, rqd, c_ctx->sentry, lun_bitmap, valid, 0);
	else
		pblk_map_erase_rq(pblk, rqd, c_ctx->sentry, lun_bitmap,
							valid, &erase_ppa);

out:
	if (unlikely(e_line && !ppa_empty(erase_ppa))) {
		if (pblk_blk_erase_async(pblk, erase_ppa)) {
			struct nvm_tgt_dev *dev = pblk->dev;
			struct nvm_geo *geo = &dev->geo;
			int bit;

			e_line->left_eblks++;
			bit = erase_ppa.g.lun * geo->nr_chnls + erase_ppa.g.ch;
			WARN_ON(!test_and_clear_bit(bit, e_line->erase_bitmap));
			up(&pblk->erase_sem);
		}
	}

	return ret;
}

int pblk_setup_w_rec_rq(struct pblk *pblk, struct nvm_rq *rqd,
			struct pblk_c_ctx *c_ctx)
{
	struct pblk_line_meta *lm = &pblk->lm;
	unsigned long *lun_bitmap;
	int ret;

	lun_bitmap = kzalloc(lm->lun_bitmap_len, GFP_KERNEL);
	if (!lun_bitmap)
		return -ENOMEM;

	c_ctx->lun_bitmap = lun_bitmap;

	ret = pblk_alloc_w_rq(pblk, rqd, rqd->nr_ppas);
	if (ret)
		return ret;

	pblk_map_rq(pblk, rqd, c_ctx->sentry, lun_bitmap, c_ctx->nr_valid, 0);

	rqd->ppa_status = (u64)0;
	rqd->flags = pblk_set_progr_mode(pblk, WRITE);

	return ret;
}

static int pblk_calc_secs_to_sync(struct pblk *pblk, unsigned int secs_avail,
				  unsigned int secs_to_flush)
{
	int secs_to_sync;

	secs_to_sync = pblk_calc_secs(pblk, secs_avail, secs_to_flush);

#ifdef CONFIG_NVM_DEBUG
	if ((!secs_to_sync && secs_to_flush)
			|| (secs_to_sync < 0)
			|| (secs_to_sync > secs_avail && !secs_to_flush)) {
		pr_err("pblk: bad sector calculation (a:%d,s:%d,f:%d)\n",
				secs_avail, secs_to_sync, secs_to_flush);
	}
#endif

	return secs_to_sync;
}

static int pblk_submit_write(struct pblk *pblk)
{
	struct bio *bio;
	struct nvm_rq *rqd;
	struct pblk_c_ctx *c_ctx;
	unsigned int pgs_read;
	unsigned int secs_avail, secs_to_sync, secs_to_com;
	unsigned int secs_to_flush;
	unsigned long pos;
	int err;

	/* If there are no sectors in the cache, flushes (bios without data)
	 * will be cleared on the cache threads
	 */
	secs_avail = pblk_rb_read_count(&pblk->rwb);
	if (!secs_avail)
		return 1;

	secs_to_flush = pblk_rb_sync_point_count(&pblk->rwb);
	if (!secs_to_flush && secs_avail < pblk->min_write_pgs)
		return 1;

	rqd = pblk_alloc_rqd(pblk, WRITE);
	if (IS_ERR(rqd)) {
		pr_err("pblk: cannot allocate write req.\n");
		return 1;
	}
	c_ctx = nvm_rq_to_pdu(rqd);

	bio = bio_alloc(GFP_KERNEL, pblk->max_write_pgs);
	if (!bio) {
		pr_err("pblk: cannot allocate write bio\n");
		goto fail_free_rqd;
	}
	bio->bi_iter.bi_sector = 0; /* internal bio */
	bio_set_op_attrs(bio, REQ_OP_WRITE, 0);
	rqd->bio = bio;

	secs_to_sync = pblk_calc_secs_to_sync(pblk, secs_avail, secs_to_flush);
	if (secs_to_sync > pblk->max_write_pgs) {
		pr_err("pblk: bad buffer sync calculation\n");
		goto fail_put_bio;
	}

	secs_to_com = (secs_to_sync > secs_avail) ? secs_avail : secs_to_sync;
	pos = pblk_rb_read_commit(&pblk->rwb, secs_to_com);

	pgs_read = pblk_rb_read_to_bio(&pblk->rwb, bio, c_ctx, pos,
						secs_to_sync, secs_avail);
	if (!pgs_read) {
		pr_err("pblk: corrupted write bio\n");
		goto fail_put_bio;
	}

	if (c_ctx->nr_padded)
		if (pblk_bio_add_pages(pblk, bio, GFP_KERNEL, c_ctx->nr_padded))
			goto fail_put_bio;

	/* Assign lbas to ppas and populate request structure */
	err = pblk_setup_w_rq(pblk, rqd, c_ctx);
	if (err) {
		pr_err("pblk: could not setup write request\n");
		goto fail_free_bio;
	}

	err = pblk_submit_io(pblk, rqd);
	if (err) {
		pr_err("pblk: I/O submission failed: %d\n", err);
		goto fail_free_bio;
	}

#ifdef CONFIG_NVM_DEBUG
	atomic_long_add(secs_to_sync, &pblk->sub_writes);
#endif

	return 0;

fail_free_bio:
	if (c_ctx->nr_padded)
		pblk_bio_free_pages(pblk, bio, secs_to_sync, c_ctx->nr_padded);
fail_put_bio:
	bio_put(bio);
fail_free_rqd:
	pblk_free_rqd(pblk, rqd, WRITE);

	return 1;
}

int pblk_write_ts(void *data)
{
	struct pblk *pblk = data;

	while (!kthread_should_stop()) {
		if (!pblk_submit_write(pblk))
			continue;
		set_current_state(TASK_INTERRUPTIBLE);
		io_schedule();
	}

	return 0;
}
