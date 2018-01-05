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

static unsigned long pblk_end_w_bio(struct pblk *pblk, struct nvm_rq *rqd,
				    struct pblk_c_ctx *c_ctx)
{
	struct bio *original_bio;
	unsigned long ret;
	int i;

	for (i = 0; i < c_ctx->nr_valid; i++) {
		struct pblk_w_ctx *w_ctx;

		w_ctx = pblk_rb_w_ctx(&pblk->rwb, c_ctx->sentry + i);
		while ((original_bio = bio_list_pop(&w_ctx->bios)))
			bio_endio(original_bio);
	}

	if (c_ctx->nr_padded)
		pblk_bio_free_pages(pblk, rqd->bio, c_ctx->nr_valid,
							c_ctx->nr_padded);

#ifdef CONFIG_NVM_DEBUG
	atomic_long_add(rqd->nr_ppas, &pblk->sync_writes);
#endif

	ret = pblk_rb_sync_advance(&pblk->rwb, c_ctx->nr_valid);

	bio_put(rqd->bio);
	pblk_free_rqd(pblk, rqd, PBLK_WRITE);

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

	INIT_LIST_HEAD(&recovery->failed);

	bit = -1;
	while ((bit = find_next_bit(comp_bits, nr_ppas, bit + 1)) < nr_ppas) {
		struct pblk_rb_entry *entry;
		struct ppa_addr ppa;

		/* Logic error */
		if (bit > c_ctx->nr_valid) {
			WARN_ONCE(1, "pblk: corrupted write request\n");
			mempool_free(recovery, pblk->rec_pool);
			goto out;
		}

		ppa = ppa_list[bit];
		entry = pblk_rb_sync_scan_entry(&pblk->rwb, &ppa);
		if (!entry) {
			pr_err("pblk: could not scan entry on write failure\n");
			mempool_free(recovery, pblk->rec_pool);
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
		mempool_free(recovery, pblk->rec_pool);
		goto out;
	}

	INIT_WORK(&recovery->ws_rec, pblk_submit_rec);
	queue_work(pblk->close_wq, &recovery->ws_rec);

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
		WARN_ONCE(rqd->bio->bi_status, "pblk: corrupted write error\n");
#endif

	pblk_complete_write(pblk, rqd, c_ctx);
	atomic_dec(&pblk->inflight_io);
}

static void pblk_end_io_write_meta(struct nvm_rq *rqd)
{
	struct pblk *pblk = rqd->private;
	struct pblk_g_ctx *m_ctx = nvm_rq_to_pdu(rqd);
	struct pblk_line *line = m_ctx->private;
	struct pblk_emeta *emeta = line->emeta;
	int sync;

	pblk_up_page(pblk, rqd->ppa_list, rqd->nr_ppas);

	if (rqd->error) {
		pblk_log_write_err(pblk, rqd);
		pr_err("pblk: metadata I/O failed. Line %d\n", line->id);
	}

	sync = atomic_add_return(rqd->nr_ppas, &emeta->sync);
	if (sync == emeta->nr_entries)
		pblk_gen_run_ws(pblk, line, NULL, pblk_line_close_ws,
						GFP_ATOMIC, pblk->close_wq);

	pblk_free_rqd(pblk, rqd, PBLK_WRITE_INT);

	atomic_dec(&pblk->inflight_io);
}

static int pblk_alloc_w_rq(struct pblk *pblk, struct nvm_rq *rqd,
			   unsigned int nr_secs,
			   nvm_end_io_fn(*end_io))
{
	struct nvm_tgt_dev *dev = pblk->dev;

	/* Setup write request */
	rqd->opcode = NVM_OP_PWRITE;
	rqd->nr_ppas = nr_secs;
	rqd->flags = pblk_set_progr_mode(pblk, PBLK_WRITE);
	rqd->private = pblk;
	rqd->end_io = end_io;

	rqd->meta_list = nvm_dev_dma_alloc(dev->parent, GFP_KERNEL,
							&rqd->dma_meta_list);
	if (!rqd->meta_list)
		return -ENOMEM;

	rqd->ppa_list = rqd->meta_list + pblk_dma_meta_size;
	rqd->dma_ppa_list = rqd->dma_meta_list + pblk_dma_meta_size;

	return 0;
}

static int pblk_setup_w_rq(struct pblk *pblk, struct nvm_rq *rqd,
			   struct ppa_addr *erase_ppa)
{
	struct pblk_line_meta *lm = &pblk->lm;
	struct pblk_line *e_line = pblk_line_get_erase(pblk);
	struct pblk_c_ctx *c_ctx = nvm_rq_to_pdu(rqd);
	unsigned int valid = c_ctx->nr_valid;
	unsigned int padded = c_ctx->nr_padded;
	unsigned int nr_secs = valid + padded;
	unsigned long *lun_bitmap;
	int ret;

	lun_bitmap = kzalloc(lm->lun_bitmap_len, GFP_KERNEL);
	if (!lun_bitmap)
		return -ENOMEM;
	c_ctx->lun_bitmap = lun_bitmap;

	ret = pblk_alloc_w_rq(pblk, rqd, nr_secs, pblk_end_io_write);
	if (ret) {
		kfree(lun_bitmap);
		return ret;
	}

	if (likely(!e_line || !atomic_read(&e_line->left_eblks)))
		pblk_map_rq(pblk, rqd, c_ctx->sentry, lun_bitmap, valid, 0);
	else
		pblk_map_erase_rq(pblk, rqd, c_ctx->sentry, lun_bitmap,
							valid, erase_ppa);

	return 0;
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

	ret = pblk_alloc_w_rq(pblk, rqd, rqd->nr_ppas, pblk_end_io_write);
	if (ret)
		return ret;

	pblk_map_rq(pblk, rqd, c_ctx->sentry, lun_bitmap, c_ctx->nr_valid, 0);

	rqd->ppa_status = (u64)0;
	rqd->flags = pblk_set_progr_mode(pblk, PBLK_WRITE);

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

int pblk_submit_meta_io(struct pblk *pblk, struct pblk_line *meta_line)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	struct pblk_line_mgmt *l_mg = &pblk->l_mg;
	struct pblk_line_meta *lm = &pblk->lm;
	struct pblk_emeta *emeta = meta_line->emeta;
	struct pblk_g_ctx *m_ctx;
	struct bio *bio;
	struct nvm_rq *rqd;
	void *data;
	u64 paddr;
	int rq_ppas = pblk->min_write_pgs;
	int id = meta_line->id;
	int rq_len;
	int i, j;
	int ret;

	rqd = pblk_alloc_rqd(pblk, PBLK_WRITE_INT);

	m_ctx = nvm_rq_to_pdu(rqd);
	m_ctx->private = meta_line;

	rq_len = rq_ppas * geo->sec_size;
	data = ((void *)emeta->buf) + emeta->mem;

	bio = pblk_bio_map_addr(pblk, data, rq_ppas, rq_len,
					l_mg->emeta_alloc_type, GFP_KERNEL);
	if (IS_ERR(bio)) {
		ret = PTR_ERR(bio);
		goto fail_free_rqd;
	}
	bio->bi_iter.bi_sector = 0; /* internal bio */
	bio_set_op_attrs(bio, REQ_OP_WRITE, 0);
	rqd->bio = bio;

	ret = pblk_alloc_w_rq(pblk, rqd, rq_ppas, pblk_end_io_write_meta);
	if (ret)
		goto fail_free_bio;

	for (i = 0; i < rqd->nr_ppas; ) {
		spin_lock(&meta_line->lock);
		paddr = __pblk_alloc_page(pblk, meta_line, rq_ppas);
		spin_unlock(&meta_line->lock);
		for (j = 0; j < rq_ppas; j++, i++, paddr++)
			rqd->ppa_list[i] = addr_to_gen_ppa(pblk, paddr, id);
	}

	emeta->mem += rq_len;
	if (emeta->mem >= lm->emeta_len[0]) {
		spin_lock(&l_mg->close_lock);
		list_del(&meta_line->list);
		spin_unlock(&l_mg->close_lock);
	}

	pblk_down_page(pblk, rqd->ppa_list, rqd->nr_ppas);

	ret = pblk_submit_io(pblk, rqd);
	if (ret) {
		pr_err("pblk: emeta I/O submission failed: %d\n", ret);
		goto fail_rollback;
	}

	return NVM_IO_OK;

fail_rollback:
	pblk_up_page(pblk, rqd->ppa_list, rqd->nr_ppas);
	spin_lock(&l_mg->close_lock);
	pblk_dealloc_page(pblk, meta_line, rq_ppas);
	list_add(&meta_line->list, &meta_line->list);
	spin_unlock(&l_mg->close_lock);
fail_free_bio:
	bio_put(bio);
fail_free_rqd:
	pblk_free_rqd(pblk, rqd, PBLK_WRITE_INT);
	return ret;
}

static inline bool pblk_valid_meta_ppa(struct pblk *pblk,
				       struct pblk_line *meta_line,
				       struct nvm_rq *data_rqd)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	struct pblk_c_ctx *data_c_ctx = nvm_rq_to_pdu(data_rqd);
	struct pblk_line *data_line = pblk_line_get_data(pblk);
	struct ppa_addr ppa, ppa_opt;
	u64 paddr;
	int pos_opt;

	/* Schedule a metadata I/O that is half the distance from the data I/O
	 * with regards to the number of LUNs forming the pblk instance. This
	 * balances LUN conflicts across every I/O.
	 *
	 * When the LUN configuration changes (e.g., due to GC), this distance
	 * can align, which would result on metadata and data I/Os colliding. In
	 * this case, modify the distance to not be optimal, but move the
	 * optimal in the right direction.
	 */
	paddr = pblk_lookup_page(pblk, meta_line);
	ppa = addr_to_gen_ppa(pblk, paddr, 0);
	ppa_opt = addr_to_gen_ppa(pblk, paddr + data_line->meta_distance, 0);
	pos_opt = pblk_ppa_to_pos(geo, ppa_opt);

	if (test_bit(pos_opt, data_c_ctx->lun_bitmap) ||
				test_bit(pos_opt, data_line->blk_bitmap))
		return true;

	if (unlikely(pblk_ppa_comp(ppa_opt, ppa)))
		data_line->meta_distance--;

	return false;
}

static struct pblk_line *pblk_should_submit_meta_io(struct pblk *pblk,
						    struct nvm_rq *data_rqd)
{
	struct pblk_line_meta *lm = &pblk->lm;
	struct pblk_line_mgmt *l_mg = &pblk->l_mg;
	struct pblk_line *meta_line;

	spin_lock(&l_mg->close_lock);
retry:
	if (list_empty(&l_mg->emeta_list)) {
		spin_unlock(&l_mg->close_lock);
		return NULL;
	}
	meta_line = list_first_entry(&l_mg->emeta_list, struct pblk_line, list);
	if (meta_line->emeta->mem >= lm->emeta_len[0])
		goto retry;
	spin_unlock(&l_mg->close_lock);

	if (!pblk_valid_meta_ppa(pblk, meta_line, data_rqd))
		return NULL;

	return meta_line;
}

static int pblk_submit_io_set(struct pblk *pblk, struct nvm_rq *rqd)
{
	struct ppa_addr erase_ppa;
	struct pblk_line *meta_line;
	int err;

	pblk_ppa_set_empty(&erase_ppa);

	/* Assign lbas to ppas and populate request structure */
	err = pblk_setup_w_rq(pblk, rqd, &erase_ppa);
	if (err) {
		pr_err("pblk: could not setup write request: %d\n", err);
		return NVM_IO_ERR;
	}

	meta_line = pblk_should_submit_meta_io(pblk, rqd);

	/* Submit data write for current data line */
	err = pblk_submit_io(pblk, rqd);
	if (err) {
		pr_err("pblk: data I/O submission failed: %d\n", err);
		return NVM_IO_ERR;
	}

	if (!pblk_ppa_empty(erase_ppa)) {
		/* Submit erase for next data line */
		if (pblk_blk_erase_async(pblk, erase_ppa)) {
			struct pblk_line *e_line = pblk_line_get_erase(pblk);
			struct nvm_tgt_dev *dev = pblk->dev;
			struct nvm_geo *geo = &dev->geo;
			int bit;

			atomic_inc(&e_line->left_eblks);
			bit = pblk_ppa_to_pos(geo, erase_ppa);
			WARN_ON(!test_and_clear_bit(bit, e_line->erase_bitmap));
		}
	}

	if (meta_line) {
		/* Submit metadata write for previous data line */
		err = pblk_submit_meta_io(pblk, meta_line);
		if (err) {
			pr_err("pblk: metadata I/O submission failed: %d", err);
			return NVM_IO_ERR;
		}
	}

	return NVM_IO_OK;
}

static void pblk_free_write_rqd(struct pblk *pblk, struct nvm_rq *rqd)
{
	struct pblk_c_ctx *c_ctx = nvm_rq_to_pdu(rqd);
	struct bio *bio = rqd->bio;

	if (c_ctx->nr_padded)
		pblk_bio_free_pages(pblk, bio, c_ctx->nr_valid,
							c_ctx->nr_padded);
}

static int pblk_submit_write(struct pblk *pblk)
{
	struct bio *bio;
	struct nvm_rq *rqd;
	unsigned int secs_avail, secs_to_sync, secs_to_com;
	unsigned int secs_to_flush;
	unsigned long pos;

	/* If there are no sectors in the cache, flushes (bios without data)
	 * will be cleared on the cache threads
	 */
	secs_avail = pblk_rb_read_count(&pblk->rwb);
	if (!secs_avail)
		return 1;

	secs_to_flush = pblk_rb_flush_point_count(&pblk->rwb);
	if (!secs_to_flush && secs_avail < pblk->min_write_pgs)
		return 1;

	secs_to_sync = pblk_calc_secs_to_sync(pblk, secs_avail, secs_to_flush);
	if (secs_to_sync > pblk->max_write_pgs) {
		pr_err("pblk: bad buffer sync calculation\n");
		return 1;
	}

	secs_to_com = (secs_to_sync > secs_avail) ? secs_avail : secs_to_sync;
	pos = pblk_rb_read_commit(&pblk->rwb, secs_to_com);

	bio = bio_alloc(GFP_KERNEL, secs_to_sync);

	bio->bi_iter.bi_sector = 0; /* internal bio */
	bio_set_op_attrs(bio, REQ_OP_WRITE, 0);

	rqd = pblk_alloc_rqd(pblk, PBLK_WRITE);
	rqd->bio = bio;

	if (pblk_rb_read_to_bio(&pblk->rwb, rqd, pos, secs_to_sync,
								secs_avail)) {
		pr_err("pblk: corrupted write bio\n");
		goto fail_put_bio;
	}

	if (pblk_submit_io_set(pblk, rqd))
		goto fail_free_bio;

#ifdef CONFIG_NVM_DEBUG
	atomic_long_add(secs_to_sync, &pblk->sub_writes);
#endif

	return 0;

fail_free_bio:
	pblk_free_write_rqd(pblk, rqd);
fail_put_bio:
	bio_put(bio);
	pblk_free_rqd(pblk, rqd, PBLK_WRITE);

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
