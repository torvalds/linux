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
 * pblk-core.c - pblk's core functionality
 *
 */

#include "pblk.h"

static void pblk_mark_bb(struct pblk *pblk, struct pblk_line *line,
			 struct ppa_addr *ppa)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	int pos = pblk_dev_ppa_to_pos(geo, *ppa);

	pr_debug("pblk: erase failed: line:%d, pos:%d\n", line->id, pos);
	atomic_long_inc(&pblk->erase_failed);

	atomic_dec(&line->blk_in_line);
	if (test_and_set_bit(pos, line->blk_bitmap))
		pr_err("pblk: attempted to erase bb: line:%d, pos:%d\n",
							line->id, pos);

	pblk_line_run_ws(pblk, NULL, ppa, pblk_line_mark_bb, pblk->bb_wq);
}

static void __pblk_end_io_erase(struct pblk *pblk, struct nvm_rq *rqd)
{
	struct pblk_line *line;

	line = &pblk->lines[pblk_dev_ppa_to_line(rqd->ppa_addr)];
	atomic_dec(&line->left_seblks);

	if (rqd->error) {
		struct ppa_addr *ppa;

		ppa = kmalloc(sizeof(struct ppa_addr), GFP_ATOMIC);
		if (!ppa)
			return;

		*ppa = rqd->ppa_addr;
		pblk_mark_bb(pblk, line, ppa);
	}

	atomic_dec(&pblk->inflight_io);
}

/* Erase completion assumes that only one block is erased at the time */
static void pblk_end_io_erase(struct nvm_rq *rqd)
{
	struct pblk *pblk = rqd->private;

	__pblk_end_io_erase(pblk, rqd);
	mempool_free(rqd, pblk->g_rq_pool);
}

void __pblk_map_invalidate(struct pblk *pblk, struct pblk_line *line,
			   u64 paddr)
{
	struct pblk_line_mgmt *l_mg = &pblk->l_mg;
	struct list_head *move_list = NULL;

	/* Lines being reclaimed (GC'ed) cannot be invalidated. Before the L2P
	 * table is modified with reclaimed sectors, a check is done to endure
	 * that newer updates are not overwritten.
	 */
	spin_lock(&line->lock);
	if (line->state == PBLK_LINESTATE_GC ||
					line->state == PBLK_LINESTATE_FREE) {
		spin_unlock(&line->lock);
		return;
	}

	if (test_and_set_bit(paddr, line->invalid_bitmap)) {
		WARN_ONCE(1, "pblk: double invalidate\n");
		spin_unlock(&line->lock);
		return;
	}
	le32_add_cpu(line->vsc, -1);

	if (line->state == PBLK_LINESTATE_CLOSED)
		move_list = pblk_line_gc_list(pblk, line);
	spin_unlock(&line->lock);

	if (move_list) {
		spin_lock(&l_mg->gc_lock);
		spin_lock(&line->lock);
		/* Prevent moving a line that has just been chosen for GC */
		if (line->state == PBLK_LINESTATE_GC ||
					line->state == PBLK_LINESTATE_FREE) {
			spin_unlock(&line->lock);
			spin_unlock(&l_mg->gc_lock);
			return;
		}
		spin_unlock(&line->lock);

		list_move_tail(&line->list, move_list);
		spin_unlock(&l_mg->gc_lock);
	}
}

void pblk_map_invalidate(struct pblk *pblk, struct ppa_addr ppa)
{
	struct pblk_line *line;
	u64 paddr;
	int line_id;

#ifdef CONFIG_NVM_DEBUG
	/* Callers must ensure that the ppa points to a device address */
	BUG_ON(pblk_addr_in_cache(ppa));
	BUG_ON(pblk_ppa_empty(ppa));
#endif

	line_id = pblk_tgt_ppa_to_line(ppa);
	line = &pblk->lines[line_id];
	paddr = pblk_dev_ppa_to_line_addr(pblk, ppa);

	__pblk_map_invalidate(pblk, line, paddr);
}

static void pblk_invalidate_range(struct pblk *pblk, sector_t slba,
				  unsigned int nr_secs)
{
	sector_t lba;

	spin_lock(&pblk->trans_lock);
	for (lba = slba; lba < slba + nr_secs; lba++) {
		struct ppa_addr ppa;

		ppa = pblk_trans_map_get(pblk, lba);

		if (!pblk_addr_in_cache(ppa) && !pblk_ppa_empty(ppa))
			pblk_map_invalidate(pblk, ppa);

		pblk_ppa_set_empty(&ppa);
		pblk_trans_map_set(pblk, lba, ppa);
	}
	spin_unlock(&pblk->trans_lock);
}

struct nvm_rq *pblk_alloc_rqd(struct pblk *pblk, int rw)
{
	mempool_t *pool;
	struct nvm_rq *rqd;
	int rq_size;

	if (rw == WRITE) {
		pool = pblk->w_rq_pool;
		rq_size = pblk_w_rq_size;
	} else {
		pool = pblk->g_rq_pool;
		rq_size = pblk_g_rq_size;
	}

	rqd = mempool_alloc(pool, GFP_KERNEL);
	memset(rqd, 0, rq_size);

	return rqd;
}

void pblk_free_rqd(struct pblk *pblk, struct nvm_rq *rqd, int rw)
{
	mempool_t *pool;

	if (rw == WRITE)
		pool = pblk->w_rq_pool;
	else
		pool = pblk->g_rq_pool;

	mempool_free(rqd, pool);
}

void pblk_bio_free_pages(struct pblk *pblk, struct bio *bio, int off,
			 int nr_pages)
{
	struct bio_vec bv;
	int i;

	WARN_ON(off + nr_pages != bio->bi_vcnt);

	for (i = off; i < nr_pages + off; i++) {
		bv = bio->bi_io_vec[i];
		mempool_free(bv.bv_page, pblk->page_bio_pool);
	}
}

int pblk_bio_add_pages(struct pblk *pblk, struct bio *bio, gfp_t flags,
		       int nr_pages)
{
	struct request_queue *q = pblk->dev->q;
	struct page *page;
	int i, ret;

	for (i = 0; i < nr_pages; i++) {
		page = mempool_alloc(pblk->page_bio_pool, flags);
		if (!page)
			goto err;

		ret = bio_add_pc_page(q, bio, page, PBLK_EXPOSED_PAGE_SIZE, 0);
		if (ret != PBLK_EXPOSED_PAGE_SIZE) {
			pr_err("pblk: could not add page to bio\n");
			mempool_free(page, pblk->page_bio_pool);
			goto err;
		}
	}

	return 0;
err:
	pblk_bio_free_pages(pblk, bio, 0, i - 1);
	return -1;
}

static void pblk_write_kick(struct pblk *pblk)
{
	wake_up_process(pblk->writer_ts);
	mod_timer(&pblk->wtimer, jiffies + msecs_to_jiffies(1000));
}

void pblk_write_timer_fn(unsigned long data)
{
	struct pblk *pblk = (struct pblk *)data;

	/* kick the write thread every tick to flush outstanding data */
	pblk_write_kick(pblk);
}

void pblk_write_should_kick(struct pblk *pblk)
{
	unsigned int secs_avail = pblk_rb_read_count(&pblk->rwb);

	if (secs_avail >= pblk->min_write_pgs)
		pblk_write_kick(pblk);
}

void pblk_end_bio_sync(struct bio *bio)
{
	struct completion *waiting = bio->bi_private;

	complete(waiting);
}

void pblk_end_io_sync(struct nvm_rq *rqd)
{
	struct completion *waiting = rqd->private;

	complete(waiting);
}

void pblk_wait_for_meta(struct pblk *pblk)
{
	do {
		if (!atomic_read(&pblk->inflight_io))
			break;

		schedule();
	} while (1);
}

static void pblk_flush_writer(struct pblk *pblk)
{
	pblk_rb_flush(&pblk->rwb);
	do {
		if (!pblk_rb_sync_count(&pblk->rwb))
			break;

		pblk_write_kick(pblk);
		schedule();
	} while (1);
}

struct list_head *pblk_line_gc_list(struct pblk *pblk, struct pblk_line *line)
{
	struct pblk_line_meta *lm = &pblk->lm;
	struct pblk_line_mgmt *l_mg = &pblk->l_mg;
	struct list_head *move_list = NULL;
	int vsc = le32_to_cpu(*line->vsc);

	lockdep_assert_held(&line->lock);

	if (!vsc) {
		if (line->gc_group != PBLK_LINEGC_FULL) {
			line->gc_group = PBLK_LINEGC_FULL;
			move_list = &l_mg->gc_full_list;
		}
	} else if (vsc < lm->high_thrs) {
		if (line->gc_group != PBLK_LINEGC_HIGH) {
			line->gc_group = PBLK_LINEGC_HIGH;
			move_list = &l_mg->gc_high_list;
		}
	} else if (vsc < lm->mid_thrs) {
		if (line->gc_group != PBLK_LINEGC_MID) {
			line->gc_group = PBLK_LINEGC_MID;
			move_list = &l_mg->gc_mid_list;
		}
	} else if (vsc < line->sec_in_line) {
		if (line->gc_group != PBLK_LINEGC_LOW) {
			line->gc_group = PBLK_LINEGC_LOW;
			move_list = &l_mg->gc_low_list;
		}
	} else if (vsc == line->sec_in_line) {
		if (line->gc_group != PBLK_LINEGC_EMPTY) {
			line->gc_group = PBLK_LINEGC_EMPTY;
			move_list = &l_mg->gc_empty_list;
		}
	} else {
		line->state = PBLK_LINESTATE_CORRUPT;
		line->gc_group = PBLK_LINEGC_NONE;
		move_list =  &l_mg->corrupt_list;
		pr_err("pblk: corrupted vsc for line %d, vsc:%d (%d/%d/%d)\n",
						line->id, vsc,
						line->sec_in_line,
						lm->high_thrs, lm->mid_thrs);
	}

	return move_list;
}

void pblk_discard(struct pblk *pblk, struct bio *bio)
{
	sector_t slba = pblk_get_lba(bio);
	sector_t nr_secs = pblk_get_secs(bio);

	pblk_invalidate_range(pblk, slba, nr_secs);
}

struct ppa_addr pblk_get_lba_map(struct pblk *pblk, sector_t lba)
{
	struct ppa_addr ppa;

	spin_lock(&pblk->trans_lock);
	ppa = pblk_trans_map_get(pblk, lba);
	spin_unlock(&pblk->trans_lock);

	return ppa;
}

void pblk_log_write_err(struct pblk *pblk, struct nvm_rq *rqd)
{
	atomic_long_inc(&pblk->write_failed);
#ifdef CONFIG_NVM_DEBUG
	pblk_print_failed_rqd(pblk, rqd, rqd->error);
#endif
}

void pblk_log_read_err(struct pblk *pblk, struct nvm_rq *rqd)
{
	/* Empty page read is not necessarily an error (e.g., L2P recovery) */
	if (rqd->error == NVM_RSP_ERR_EMPTYPAGE) {
		atomic_long_inc(&pblk->read_empty);
		return;
	}

	switch (rqd->error) {
	case NVM_RSP_WARN_HIGHECC:
		atomic_long_inc(&pblk->read_high_ecc);
		break;
	case NVM_RSP_ERR_FAILECC:
	case NVM_RSP_ERR_FAILCRC:
		atomic_long_inc(&pblk->read_failed);
		break;
	default:
		pr_err("pblk: unknown read error:%d\n", rqd->error);
	}
#ifdef CONFIG_NVM_DEBUG
	pblk_print_failed_rqd(pblk, rqd, rqd->error);
#endif
}

void pblk_set_sec_per_write(struct pblk *pblk, int sec_per_write)
{
	pblk->sec_per_write = sec_per_write;
}

int pblk_submit_io(struct pblk *pblk, struct nvm_rq *rqd)
{
	struct nvm_tgt_dev *dev = pblk->dev;

#ifdef CONFIG_NVM_DEBUG
	struct ppa_addr *ppa_list;

	ppa_list = (rqd->nr_ppas > 1) ? rqd->ppa_list : &rqd->ppa_addr;
	if (pblk_boundary_ppa_checks(dev, ppa_list, rqd->nr_ppas)) {
		WARN_ON(1);
		return -EINVAL;
	}

	if (rqd->opcode == NVM_OP_PWRITE) {
		struct pblk_line *line;
		struct ppa_addr ppa;
		int i;

		for (i = 0; i < rqd->nr_ppas; i++) {
			ppa = ppa_list[i];
			line = &pblk->lines[pblk_dev_ppa_to_line(ppa)];

			spin_lock(&line->lock);
			if (line->state != PBLK_LINESTATE_OPEN) {
				pr_err("pblk: bad ppa: line:%d,state:%d\n",
							line->id, line->state);
				WARN_ON(1);
				spin_unlock(&line->lock);
				return -EINVAL;
			}
			spin_unlock(&line->lock);
		}
	}
#endif

	atomic_inc(&pblk->inflight_io);

	return nvm_submit_io(dev, rqd);
}

struct bio *pblk_bio_map_addr(struct pblk *pblk, void *data,
			      unsigned int nr_secs, unsigned int len,
			      int alloc_type, gfp_t gfp_mask)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	void *kaddr = data;
	struct page *page;
	struct bio *bio;
	int i, ret;

	if (alloc_type == PBLK_KMALLOC_META)
		return bio_map_kern(dev->q, kaddr, len, gfp_mask);

	bio = bio_kmalloc(gfp_mask, nr_secs);
	if (!bio)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < nr_secs; i++) {
		page = vmalloc_to_page(kaddr);
		if (!page) {
			pr_err("pblk: could not map vmalloc bio\n");
			bio_put(bio);
			bio = ERR_PTR(-ENOMEM);
			goto out;
		}

		ret = bio_add_pc_page(dev->q, bio, page, PAGE_SIZE, 0);
		if (ret != PAGE_SIZE) {
			pr_err("pblk: could not add page to bio\n");
			bio_put(bio);
			bio = ERR_PTR(-ENOMEM);
			goto out;
		}

		kaddr += PAGE_SIZE;
	}
out:
	return bio;
}

int pblk_calc_secs(struct pblk *pblk, unsigned long secs_avail,
		   unsigned long secs_to_flush)
{
	int max = pblk->sec_per_write;
	int min = pblk->min_write_pgs;
	int secs_to_sync = 0;

	if (secs_avail >= max)
		secs_to_sync = max;
	else if (secs_avail >= min)
		secs_to_sync = min * (secs_avail / min);
	else if (secs_to_flush)
		secs_to_sync = min;

	return secs_to_sync;
}

void pblk_dealloc_page(struct pblk *pblk, struct pblk_line *line, int nr_secs)
{
	u64 addr;
	int i;

	spin_lock(&line->lock);
	addr = find_next_zero_bit(line->map_bitmap,
					pblk->lm.sec_per_line, line->cur_sec);
	line->cur_sec = addr - nr_secs;

	for (i = 0; i < nr_secs; i++, line->cur_sec--)
		WARN_ON(!test_and_clear_bit(line->cur_sec, line->map_bitmap));
	spin_unlock(&line->lock);
}

u64 __pblk_alloc_page(struct pblk *pblk, struct pblk_line *line, int nr_secs)
{
	u64 addr;
	int i;

	lockdep_assert_held(&line->lock);

	/* logic error: ppa out-of-bounds. Prevent generating bad address */
	if (line->cur_sec + nr_secs > pblk->lm.sec_per_line) {
		WARN(1, "pblk: page allocation out of bounds\n");
		nr_secs = pblk->lm.sec_per_line - line->cur_sec;
	}

	line->cur_sec = addr = find_next_zero_bit(line->map_bitmap,
					pblk->lm.sec_per_line, line->cur_sec);
	for (i = 0; i < nr_secs; i++, line->cur_sec++)
		WARN_ON(test_and_set_bit(line->cur_sec, line->map_bitmap));

	return addr;
}

u64 pblk_alloc_page(struct pblk *pblk, struct pblk_line *line, int nr_secs)
{
	u64 addr;

	/* Lock needed in case a write fails and a recovery needs to remap
	 * failed write buffer entries
	 */
	spin_lock(&line->lock);
	addr = __pblk_alloc_page(pblk, line, nr_secs);
	line->left_msecs -= nr_secs;
	WARN(line->left_msecs < 0, "pblk: page allocation out of bounds\n");
	spin_unlock(&line->lock);

	return addr;
}

u64 pblk_lookup_page(struct pblk *pblk, struct pblk_line *line)
{
	u64 paddr;

	spin_lock(&line->lock);
	paddr = find_next_zero_bit(line->map_bitmap,
					pblk->lm.sec_per_line, line->cur_sec);
	spin_unlock(&line->lock);

	return paddr;
}

/*
 * Submit emeta to one LUN in the raid line at the time to avoid a deadlock when
 * taking the per LUN semaphore.
 */
static int pblk_line_submit_emeta_io(struct pblk *pblk, struct pblk_line *line,
				     void *emeta_buf, u64 paddr, int dir)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	struct pblk_line_mgmt *l_mg = &pblk->l_mg;
	struct pblk_line_meta *lm = &pblk->lm;
	void *ppa_list, *meta_list;
	struct bio *bio;
	struct nvm_rq rqd;
	dma_addr_t dma_ppa_list, dma_meta_list;
	int min = pblk->min_write_pgs;
	int left_ppas = lm->emeta_sec[0];
	int id = line->id;
	int rq_ppas, rq_len;
	int cmd_op, bio_op;
	int i, j;
	int ret;
	DECLARE_COMPLETION_ONSTACK(wait);

	if (dir == WRITE) {
		bio_op = REQ_OP_WRITE;
		cmd_op = NVM_OP_PWRITE;
	} else if (dir == READ) {
		bio_op = REQ_OP_READ;
		cmd_op = NVM_OP_PREAD;
	} else
		return -EINVAL;

	meta_list = nvm_dev_dma_alloc(dev->parent, GFP_KERNEL,
							&dma_meta_list);
	if (!meta_list)
		return -ENOMEM;

	ppa_list = meta_list + pblk_dma_meta_size;
	dma_ppa_list = dma_meta_list + pblk_dma_meta_size;

next_rq:
	memset(&rqd, 0, sizeof(struct nvm_rq));

	rq_ppas = pblk_calc_secs(pblk, left_ppas, 0);
	rq_len = rq_ppas * geo->sec_size;

	bio = pblk_bio_map_addr(pblk, emeta_buf, rq_ppas, rq_len,
					l_mg->emeta_alloc_type, GFP_KERNEL);
	if (IS_ERR(bio)) {
		ret = PTR_ERR(bio);
		goto free_rqd_dma;
	}

	bio->bi_iter.bi_sector = 0; /* internal bio */
	bio_set_op_attrs(bio, bio_op, 0);

	rqd.bio = bio;
	rqd.meta_list = meta_list;
	rqd.ppa_list = ppa_list;
	rqd.dma_meta_list = dma_meta_list;
	rqd.dma_ppa_list = dma_ppa_list;
	rqd.opcode = cmd_op;
	rqd.nr_ppas = rq_ppas;
	rqd.end_io = pblk_end_io_sync;
	rqd.private = &wait;

	if (dir == WRITE) {
		struct pblk_sec_meta *meta_list = rqd.meta_list;

		rqd.flags = pblk_set_progr_mode(pblk, WRITE);
		for (i = 0; i < rqd.nr_ppas; ) {
			spin_lock(&line->lock);
			paddr = __pblk_alloc_page(pblk, line, min);
			spin_unlock(&line->lock);
			for (j = 0; j < min; j++, i++, paddr++) {
				meta_list[i].lba = cpu_to_le64(ADDR_EMPTY);
				rqd.ppa_list[i] =
					addr_to_gen_ppa(pblk, paddr, id);
			}
		}
	} else {
		for (i = 0; i < rqd.nr_ppas; ) {
			struct ppa_addr ppa = addr_to_gen_ppa(pblk, paddr, id);
			int pos = pblk_dev_ppa_to_pos(geo, ppa);
			int read_type = PBLK_READ_RANDOM;

			if (pblk_io_aligned(pblk, rq_ppas))
				read_type = PBLK_READ_SEQUENTIAL;
			rqd.flags = pblk_set_read_mode(pblk, read_type);

			while (test_bit(pos, line->blk_bitmap)) {
				paddr += min;
				if (pblk_boundary_paddr_checks(pblk, paddr)) {
					pr_err("pblk: corrupt emeta line:%d\n",
								line->id);
					bio_put(bio);
					ret = -EINTR;
					goto free_rqd_dma;
				}

				ppa = addr_to_gen_ppa(pblk, paddr, id);
				pos = pblk_dev_ppa_to_pos(geo, ppa);
			}

			if (pblk_boundary_paddr_checks(pblk, paddr + min)) {
				pr_err("pblk: corrupt emeta line:%d\n",
								line->id);
				bio_put(bio);
				ret = -EINTR;
				goto free_rqd_dma;
			}

			for (j = 0; j < min; j++, i++, paddr++)
				rqd.ppa_list[i] =
					addr_to_gen_ppa(pblk, paddr, line->id);
		}
	}

	ret = pblk_submit_io(pblk, &rqd);
	if (ret) {
		pr_err("pblk: emeta I/O submission failed: %d\n", ret);
		bio_put(bio);
		goto free_rqd_dma;
	}

	if (!wait_for_completion_io_timeout(&wait,
				msecs_to_jiffies(PBLK_COMMAND_TIMEOUT_MS))) {
		pr_err("pblk: emeta I/O timed out\n");
	}
	atomic_dec(&pblk->inflight_io);
	reinit_completion(&wait);

	if (likely(pblk->l_mg.emeta_alloc_type == PBLK_VMALLOC_META))
		bio_put(bio);

	if (rqd.error) {
		if (dir == WRITE)
			pblk_log_write_err(pblk, &rqd);
		else
			pblk_log_read_err(pblk, &rqd);
	}

	emeta_buf += rq_len;
	left_ppas -= rq_ppas;
	if (left_ppas)
		goto next_rq;
free_rqd_dma:
	nvm_dev_dma_free(dev->parent, rqd.meta_list, rqd.dma_meta_list);
	return ret;
}

u64 pblk_line_smeta_start(struct pblk *pblk, struct pblk_line *line)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	struct pblk_line_meta *lm = &pblk->lm;
	int bit;

	/* This usually only happens on bad lines */
	bit = find_first_zero_bit(line->blk_bitmap, lm->blk_per_line);
	if (bit >= lm->blk_per_line)
		return -1;

	return bit * geo->sec_per_pl;
}

static int pblk_line_submit_smeta_io(struct pblk *pblk, struct pblk_line *line,
				     u64 paddr, int dir)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct pblk_line_meta *lm = &pblk->lm;
	struct bio *bio;
	struct nvm_rq rqd;
	__le64 *lba_list = NULL;
	int i, ret;
	int cmd_op, bio_op;
	int flags;
	DECLARE_COMPLETION_ONSTACK(wait);

	if (dir == WRITE) {
		bio_op = REQ_OP_WRITE;
		cmd_op = NVM_OP_PWRITE;
		flags = pblk_set_progr_mode(pblk, WRITE);
		lba_list = emeta_to_lbas(pblk, line->emeta->buf);
	} else if (dir == READ) {
		bio_op = REQ_OP_READ;
		cmd_op = NVM_OP_PREAD;
		flags = pblk_set_read_mode(pblk, PBLK_READ_SEQUENTIAL);
	} else
		return -EINVAL;

	memset(&rqd, 0, sizeof(struct nvm_rq));

	rqd.meta_list = nvm_dev_dma_alloc(dev->parent, GFP_KERNEL,
							&rqd.dma_meta_list);
	if (!rqd.meta_list)
		return -ENOMEM;

	rqd.ppa_list = rqd.meta_list + pblk_dma_meta_size;
	rqd.dma_ppa_list = rqd.dma_meta_list + pblk_dma_meta_size;

	bio = bio_map_kern(dev->q, line->smeta, lm->smeta_len, GFP_KERNEL);
	if (IS_ERR(bio)) {
		ret = PTR_ERR(bio);
		goto free_ppa_list;
	}

	bio->bi_iter.bi_sector = 0; /* internal bio */
	bio_set_op_attrs(bio, bio_op, 0);

	rqd.bio = bio;
	rqd.opcode = cmd_op;
	rqd.flags = flags;
	rqd.nr_ppas = lm->smeta_sec;
	rqd.end_io = pblk_end_io_sync;
	rqd.private = &wait;

	for (i = 0; i < lm->smeta_sec; i++, paddr++) {
		struct pblk_sec_meta *meta_list = rqd.meta_list;

		rqd.ppa_list[i] = addr_to_gen_ppa(pblk, paddr, line->id);

		if (dir == WRITE) {
			__le64 addr_empty = cpu_to_le64(ADDR_EMPTY);

			meta_list[i].lba = lba_list[paddr] = addr_empty;
		}
	}

	/*
	 * This I/O is sent by the write thread when a line is replace. Since
	 * the write thread is the only one sending write and erase commands,
	 * there is no need to take the LUN semaphore.
	 */
	ret = pblk_submit_io(pblk, &rqd);
	if (ret) {
		pr_err("pblk: smeta I/O submission failed: %d\n", ret);
		bio_put(bio);
		goto free_ppa_list;
	}

	if (!wait_for_completion_io_timeout(&wait,
				msecs_to_jiffies(PBLK_COMMAND_TIMEOUT_MS))) {
		pr_err("pblk: smeta I/O timed out\n");
	}
	atomic_dec(&pblk->inflight_io);

	if (rqd.error) {
		if (dir == WRITE)
			pblk_log_write_err(pblk, &rqd);
		else
			pblk_log_read_err(pblk, &rqd);
	}

free_ppa_list:
	nvm_dev_dma_free(dev->parent, rqd.meta_list, rqd.dma_meta_list);

	return ret;
}

int pblk_line_read_smeta(struct pblk *pblk, struct pblk_line *line)
{
	u64 bpaddr = pblk_line_smeta_start(pblk, line);

	return pblk_line_submit_smeta_io(pblk, line, bpaddr, READ);
}

int pblk_line_read_emeta(struct pblk *pblk, struct pblk_line *line,
			 void *emeta_buf)
{
	return pblk_line_submit_emeta_io(pblk, line, emeta_buf,
						line->emeta_ssec, READ);
}

static void pblk_setup_e_rq(struct pblk *pblk, struct nvm_rq *rqd,
			    struct ppa_addr ppa)
{
	rqd->opcode = NVM_OP_ERASE;
	rqd->ppa_addr = ppa;
	rqd->nr_ppas = 1;
	rqd->flags = pblk_set_progr_mode(pblk, ERASE);
	rqd->bio = NULL;
}

static int pblk_blk_erase_sync(struct pblk *pblk, struct ppa_addr ppa)
{
	struct nvm_rq rqd;
	int ret = 0;
	DECLARE_COMPLETION_ONSTACK(wait);

	memset(&rqd, 0, sizeof(struct nvm_rq));

	pblk_setup_e_rq(pblk, &rqd, ppa);

	rqd.end_io = pblk_end_io_sync;
	rqd.private = &wait;

	/* The write thread schedules erases so that it minimizes disturbances
	 * with writes. Thus, there is no need to take the LUN semaphore.
	 */
	ret = pblk_submit_io(pblk, &rqd);
	if (ret) {
		struct nvm_tgt_dev *dev = pblk->dev;
		struct nvm_geo *geo = &dev->geo;

		pr_err("pblk: could not sync erase line:%d,blk:%d\n",
					pblk_dev_ppa_to_line(ppa),
					pblk_dev_ppa_to_pos(geo, ppa));

		rqd.error = ret;
		goto out;
	}

	if (!wait_for_completion_io_timeout(&wait,
				msecs_to_jiffies(PBLK_COMMAND_TIMEOUT_MS))) {
		pr_err("pblk: sync erase timed out\n");
	}

out:
	rqd.private = pblk;
	__pblk_end_io_erase(pblk, &rqd);

	return ret;
}

int pblk_line_erase(struct pblk *pblk, struct pblk_line *line)
{
	struct pblk_line_meta *lm = &pblk->lm;
	struct ppa_addr ppa;
	int ret, bit = -1;

	/* Erase only good blocks, one at a time */
	do {
		spin_lock(&line->lock);
		bit = find_next_zero_bit(line->erase_bitmap, lm->blk_per_line,
								bit + 1);
		if (bit >= lm->blk_per_line) {
			spin_unlock(&line->lock);
			break;
		}

		ppa = pblk->luns[bit].bppa; /* set ch and lun */
		ppa.g.blk = line->id;

		atomic_dec(&line->left_eblks);
		WARN_ON(test_and_set_bit(bit, line->erase_bitmap));
		spin_unlock(&line->lock);

		ret = pblk_blk_erase_sync(pblk, ppa);
		if (ret) {
			pr_err("pblk: failed to erase line %d\n", line->id);
			return ret;
		}
	} while (1);

	return 0;
}

static void pblk_line_setup_metadata(struct pblk_line *line,
				     struct pblk_line_mgmt *l_mg,
				     struct pblk_line_meta *lm)
{
	int meta_line;

	lockdep_assert_held(&l_mg->free_lock);

retry_meta:
	meta_line = find_first_zero_bit(&l_mg->meta_bitmap, PBLK_DATA_LINES);
	if (meta_line == PBLK_DATA_LINES) {
		spin_unlock(&l_mg->free_lock);
		io_schedule();
		spin_lock(&l_mg->free_lock);
		goto retry_meta;
	}

	set_bit(meta_line, &l_mg->meta_bitmap);
	line->meta_line = meta_line;

	line->smeta = l_mg->sline_meta[meta_line];
	line->emeta = l_mg->eline_meta[meta_line];

	memset(line->smeta, 0, lm->smeta_len);
	memset(line->emeta->buf, 0, lm->emeta_len[0]);

	line->emeta->mem = 0;
	atomic_set(&line->emeta->sync, 0);
}

/* For now lines are always assumed full lines. Thus, smeta former and current
 * lun bitmaps are omitted.
 */
static int pblk_line_init_metadata(struct pblk *pblk, struct pblk_line *line,
				  struct pblk_line *cur)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	struct pblk_line_meta *lm = &pblk->lm;
	struct pblk_line_mgmt *l_mg = &pblk->l_mg;
	struct pblk_emeta *emeta = line->emeta;
	struct line_emeta *emeta_buf = emeta->buf;
	struct line_smeta *smeta_buf = (struct line_smeta *)line->smeta;
	int nr_blk_line;

	/* After erasing the line, new bad blocks might appear and we risk
	 * having an invalid line
	 */
	nr_blk_line = lm->blk_per_line -
			bitmap_weight(line->blk_bitmap, lm->blk_per_line);
	if (nr_blk_line < lm->min_blk_line) {
		spin_lock(&l_mg->free_lock);
		spin_lock(&line->lock);
		line->state = PBLK_LINESTATE_BAD;
		spin_unlock(&line->lock);

		list_add_tail(&line->list, &l_mg->bad_list);
		spin_unlock(&l_mg->free_lock);

		pr_debug("pblk: line %d is bad\n", line->id);

		return 0;
	}

	/* Run-time metadata */
	line->lun_bitmap = ((void *)(smeta_buf)) + sizeof(struct line_smeta);

	/* Mark LUNs allocated in this line (all for now) */
	bitmap_set(line->lun_bitmap, 0, lm->lun_bitmap_len);

	smeta_buf->header.identifier = cpu_to_le32(PBLK_MAGIC);
	memcpy(smeta_buf->header.uuid, pblk->instance_uuid, 16);
	smeta_buf->header.id = cpu_to_le32(line->id);
	smeta_buf->header.type = cpu_to_le16(line->type);
	smeta_buf->header.version = cpu_to_le16(1);

	/* Start metadata */
	smeta_buf->seq_nr = cpu_to_le64(line->seq_nr);
	smeta_buf->window_wr_lun = cpu_to_le32(geo->nr_luns);

	/* Fill metadata among lines */
	if (cur) {
		memcpy(line->lun_bitmap, cur->lun_bitmap, lm->lun_bitmap_len);
		smeta_buf->prev_id = cpu_to_le32(cur->id);
		cur->emeta->buf->next_id = cpu_to_le32(line->id);
	} else {
		smeta_buf->prev_id = cpu_to_le32(PBLK_LINE_EMPTY);
	}

	/* All smeta must be set at this point */
	smeta_buf->header.crc = cpu_to_le32(
			pblk_calc_meta_header_crc(pblk, &smeta_buf->header));
	smeta_buf->crc = cpu_to_le32(pblk_calc_smeta_crc(pblk, smeta_buf));

	/* End metadata */
	memcpy(&emeta_buf->header, &smeta_buf->header,
						sizeof(struct line_header));
	emeta_buf->seq_nr = cpu_to_le64(line->seq_nr);
	emeta_buf->nr_lbas = cpu_to_le64(line->sec_in_line);
	emeta_buf->nr_valid_lbas = cpu_to_le64(0);
	emeta_buf->next_id = cpu_to_le32(PBLK_LINE_EMPTY);
	emeta_buf->crc = cpu_to_le32(0);
	emeta_buf->prev_id = smeta_buf->prev_id;

	return 1;
}

/* For now lines are always assumed full lines. Thus, smeta former and current
 * lun bitmaps are omitted.
 */
static int pblk_line_init_bb(struct pblk *pblk, struct pblk_line *line,
			     int init)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	struct pblk_line_meta *lm = &pblk->lm;
	struct pblk_line_mgmt *l_mg = &pblk->l_mg;
	int nr_bb = 0;
	u64 off;
	int bit = -1;

	line->sec_in_line = lm->sec_per_line;

	/* Capture bad block information on line mapping bitmaps */
	while ((bit = find_next_bit(line->blk_bitmap, lm->blk_per_line,
					bit + 1)) < lm->blk_per_line) {
		off = bit * geo->sec_per_pl;
		bitmap_shift_left(l_mg->bb_aux, l_mg->bb_template, off,
							lm->sec_per_line);
		bitmap_or(line->map_bitmap, line->map_bitmap, l_mg->bb_aux,
							lm->sec_per_line);
		line->sec_in_line -= geo->sec_per_blk;
		if (bit >= lm->emeta_bb)
			nr_bb++;
	}

	/* Mark smeta metadata sectors as bad sectors */
	bit = find_first_zero_bit(line->blk_bitmap, lm->blk_per_line);
	off = bit * geo->sec_per_pl;
	bitmap_set(line->map_bitmap, off, lm->smeta_sec);
	line->sec_in_line -= lm->smeta_sec;
	line->smeta_ssec = off;
	line->cur_sec = off + lm->smeta_sec;

	if (init && pblk_line_submit_smeta_io(pblk, line, off, WRITE)) {
		pr_debug("pblk: line smeta I/O failed. Retry\n");
		return 1;
	}

	bitmap_copy(line->invalid_bitmap, line->map_bitmap, lm->sec_per_line);

	/* Mark emeta metadata sectors as bad sectors. We need to consider bad
	 * blocks to make sure that there are enough sectors to store emeta
	 */
	bit = lm->sec_per_line;
	off = lm->sec_per_line - lm->emeta_sec[0];
	bitmap_set(line->invalid_bitmap, off, lm->emeta_sec[0]);
	while (nr_bb) {
		off -= geo->sec_per_pl;
		if (!test_bit(off, line->invalid_bitmap)) {
			bitmap_set(line->invalid_bitmap, off, geo->sec_per_pl);
			nr_bb--;
		}
	}

	line->sec_in_line -= lm->emeta_sec[0];
	line->emeta_ssec = off;
	line->nr_valid_lbas = 0;
	line->left_msecs = line->sec_in_line;
	*line->vsc = cpu_to_le32(line->sec_in_line);

	if (lm->sec_per_line - line->sec_in_line !=
		bitmap_weight(line->invalid_bitmap, lm->sec_per_line)) {
		spin_lock(&line->lock);
		line->state = PBLK_LINESTATE_BAD;
		spin_unlock(&line->lock);

		list_add_tail(&line->list, &l_mg->bad_list);
		pr_err("pblk: unexpected line %d is bad\n", line->id);

		return 0;
	}

	return 1;
}

static int pblk_line_prepare(struct pblk *pblk, struct pblk_line *line)
{
	struct pblk_line_meta *lm = &pblk->lm;
	int blk_in_line = atomic_read(&line->blk_in_line);

	line->map_bitmap = mempool_alloc(pblk->line_meta_pool, GFP_ATOMIC);
	if (!line->map_bitmap)
		return -ENOMEM;
	memset(line->map_bitmap, 0, lm->sec_bitmap_len);

	/* invalid_bitmap is special since it is used when line is closed. No
	 * need to zeroized; it will be initialized using bb info form
	 * map_bitmap
	 */
	line->invalid_bitmap = mempool_alloc(pblk->line_meta_pool, GFP_ATOMIC);
	if (!line->invalid_bitmap) {
		mempool_free(line->map_bitmap, pblk->line_meta_pool);
		return -ENOMEM;
	}

	spin_lock(&line->lock);
	if (line->state != PBLK_LINESTATE_FREE) {
		mempool_free(line->invalid_bitmap, pblk->line_meta_pool);
		mempool_free(line->map_bitmap, pblk->line_meta_pool);
		spin_unlock(&line->lock);
		WARN(1, "pblk: corrupted line %d, state %d\n",
							line->id, line->state);
		return -EAGAIN;
	}

	line->state = PBLK_LINESTATE_OPEN;

	atomic_set(&line->left_eblks, blk_in_line);
	atomic_set(&line->left_seblks, blk_in_line);

	line->meta_distance = lm->meta_distance;
	spin_unlock(&line->lock);

	/* Bad blocks do not need to be erased */
	bitmap_copy(line->erase_bitmap, line->blk_bitmap, lm->blk_per_line);

	kref_init(&line->ref);

	return 0;
}

int pblk_line_recov_alloc(struct pblk *pblk, struct pblk_line *line)
{
	struct pblk_line_mgmt *l_mg = &pblk->l_mg;
	int ret;

	spin_lock(&l_mg->free_lock);
	l_mg->data_line = line;
	list_del(&line->list);

	ret = pblk_line_prepare(pblk, line);
	if (ret) {
		list_add(&line->list, &l_mg->free_list);
		spin_unlock(&l_mg->free_lock);
		return ret;
	}
	spin_unlock(&l_mg->free_lock);

	pblk_rl_free_lines_dec(&pblk->rl, line);

	if (!pblk_line_init_bb(pblk, line, 0)) {
		list_add(&line->list, &l_mg->free_list);
		return -EINTR;
	}

	return 0;
}

void pblk_line_recov_close(struct pblk *pblk, struct pblk_line *line)
{
	mempool_free(line->map_bitmap, pblk->line_meta_pool);
	line->map_bitmap = NULL;
	line->smeta = NULL;
	line->emeta = NULL;
}

struct pblk_line *pblk_line_get(struct pblk *pblk)
{
	struct pblk_line_mgmt *l_mg = &pblk->l_mg;
	struct pblk_line_meta *lm = &pblk->lm;
	struct pblk_line *line;
	int ret, bit;

	lockdep_assert_held(&l_mg->free_lock);

retry:
	if (list_empty(&l_mg->free_list)) {
		pr_err("pblk: no free lines\n");
		return NULL;
	}

	line = list_first_entry(&l_mg->free_list, struct pblk_line, list);
	list_del(&line->list);
	l_mg->nr_free_lines--;

	bit = find_first_zero_bit(line->blk_bitmap, lm->blk_per_line);
	if (unlikely(bit >= lm->blk_per_line)) {
		spin_lock(&line->lock);
		line->state = PBLK_LINESTATE_BAD;
		spin_unlock(&line->lock);

		list_add_tail(&line->list, &l_mg->bad_list);

		pr_debug("pblk: line %d is bad\n", line->id);
		goto retry;
	}

	ret = pblk_line_prepare(pblk, line);
	if (ret) {
		if (ret == -EAGAIN) {
			list_add(&line->list, &l_mg->corrupt_list);
			goto retry;
		} else {
			pr_err("pblk: failed to prepare line %d\n", line->id);
			list_add(&line->list, &l_mg->free_list);
			l_mg->nr_free_lines++;
			return NULL;
		}
	}

	return line;
}

static struct pblk_line *pblk_line_retry(struct pblk *pblk,
					 struct pblk_line *line)
{
	struct pblk_line_mgmt *l_mg = &pblk->l_mg;
	struct pblk_line *retry_line;

retry:
	spin_lock(&l_mg->free_lock);
	retry_line = pblk_line_get(pblk);
	if (!retry_line) {
		l_mg->data_line = NULL;
		spin_unlock(&l_mg->free_lock);
		return NULL;
	}

	retry_line->smeta = line->smeta;
	retry_line->emeta = line->emeta;
	retry_line->meta_line = line->meta_line;

	pblk_line_free(pblk, line);
	l_mg->data_line = retry_line;
	spin_unlock(&l_mg->free_lock);

	pblk_rl_free_lines_dec(&pblk->rl, retry_line);

	if (pblk_line_erase(pblk, retry_line))
		goto retry;

	return retry_line;
}

static void pblk_set_space_limit(struct pblk *pblk)
{
	struct pblk_rl *rl = &pblk->rl;

	atomic_set(&rl->rb_space, 0);
}

struct pblk_line *pblk_line_get_first_data(struct pblk *pblk)
{
	struct pblk_line_mgmt *l_mg = &pblk->l_mg;
	struct pblk_line *line;
	int is_next = 0;

	spin_lock(&l_mg->free_lock);
	line = pblk_line_get(pblk);
	if (!line) {
		spin_unlock(&l_mg->free_lock);
		return NULL;
	}

	line->seq_nr = l_mg->d_seq_nr++;
	line->type = PBLK_LINETYPE_DATA;
	l_mg->data_line = line;

	pblk_line_setup_metadata(line, l_mg, &pblk->lm);

	/* Allocate next line for preparation */
	l_mg->data_next = pblk_line_get(pblk);
	if (!l_mg->data_next) {
		/* If we cannot get a new line, we need to stop the pipeline.
		 * Only allow as many writes in as we can store safely and then
		 * fail gracefully
		 */
		pblk_set_space_limit(pblk);

		l_mg->data_next = NULL;
	} else {
		l_mg->data_next->seq_nr = l_mg->d_seq_nr++;
		l_mg->data_next->type = PBLK_LINETYPE_DATA;
		is_next = 1;
	}
	spin_unlock(&l_mg->free_lock);

	if (pblk_line_erase(pblk, line)) {
		line = pblk_line_retry(pblk, line);
		if (!line)
			return NULL;
	}

	pblk_rl_free_lines_dec(&pblk->rl, line);
	if (is_next)
		pblk_rl_free_lines_dec(&pblk->rl, l_mg->data_next);

retry_setup:
	if (!pblk_line_init_metadata(pblk, line, NULL)) {
		line = pblk_line_retry(pblk, line);
		if (!line)
			return NULL;

		goto retry_setup;
	}

	if (!pblk_line_init_bb(pblk, line, 1)) {
		line = pblk_line_retry(pblk, line);
		if (!line)
			return NULL;

		goto retry_setup;
	}

	return line;
}

static void pblk_stop_writes(struct pblk *pblk, struct pblk_line *line)
{
	lockdep_assert_held(&pblk->l_mg.free_lock);

	pblk_set_space_limit(pblk);
	pblk->state = PBLK_STATE_STOPPING;
}

void pblk_pipeline_stop(struct pblk *pblk)
{
	struct pblk_line_mgmt *l_mg = &pblk->l_mg;
	int ret;

	spin_lock(&l_mg->free_lock);
	if (pblk->state == PBLK_STATE_RECOVERING ||
					pblk->state == PBLK_STATE_STOPPED) {
		spin_unlock(&l_mg->free_lock);
		return;
	}
	pblk->state = PBLK_STATE_RECOVERING;
	spin_unlock(&l_mg->free_lock);

	pblk_flush_writer(pblk);
	pblk_wait_for_meta(pblk);

	ret = pblk_recov_pad(pblk);
	if (ret) {
		pr_err("pblk: could not close data on teardown(%d)\n", ret);
		return;
	}

	flush_workqueue(pblk->bb_wq);
	pblk_line_close_meta_sync(pblk);

	spin_lock(&l_mg->free_lock);
	pblk->state = PBLK_STATE_STOPPED;
	l_mg->data_line = NULL;
	l_mg->data_next = NULL;
	spin_unlock(&l_mg->free_lock);
}

void pblk_line_replace_data(struct pblk *pblk)
{
	struct pblk_line_mgmt *l_mg = &pblk->l_mg;
	struct pblk_line *cur, *new;
	unsigned int left_seblks;
	int is_next = 0;

	cur = l_mg->data_line;
	new = l_mg->data_next;
	if (!new)
		return;
	l_mg->data_line = new;

	spin_lock(&l_mg->free_lock);
	if (pblk->state != PBLK_STATE_RUNNING) {
		l_mg->data_line = NULL;
		l_mg->data_next = NULL;
		spin_unlock(&l_mg->free_lock);
		return;
	}

	pblk_line_setup_metadata(new, l_mg, &pblk->lm);
	spin_unlock(&l_mg->free_lock);

retry_erase:
	left_seblks = atomic_read(&new->left_seblks);
	if (left_seblks) {
		/* If line is not fully erased, erase it */
		if (atomic_read(&new->left_eblks)) {
			if (pblk_line_erase(pblk, new))
				return;
		} else {
			io_schedule();
		}
		goto retry_erase;
	}

retry_setup:
	if (!pblk_line_init_metadata(pblk, new, cur)) {
		new = pblk_line_retry(pblk, new);
		if (!new)
			return;

		goto retry_setup;
	}

	if (!pblk_line_init_bb(pblk, new, 1)) {
		new = pblk_line_retry(pblk, new);
		if (!new)
			return;

		goto retry_setup;
	}

	/* Allocate next line for preparation */
	spin_lock(&l_mg->free_lock);
	l_mg->data_next = pblk_line_get(pblk);
	if (!l_mg->data_next) {
		/* If we cannot get a new line, we need to stop the pipeline.
		 * Only allow as many writes in as we can store safely and then
		 * fail gracefully
		 */
		pblk_stop_writes(pblk, new);
		l_mg->data_next = NULL;
	} else {
		l_mg->data_next->seq_nr = l_mg->d_seq_nr++;
		l_mg->data_next->type = PBLK_LINETYPE_DATA;
		is_next = 1;
	}
	spin_unlock(&l_mg->free_lock);

	if (is_next)
		pblk_rl_free_lines_dec(&pblk->rl, l_mg->data_next);
}

void pblk_line_free(struct pblk *pblk, struct pblk_line *line)
{
	if (line->map_bitmap)
		mempool_free(line->map_bitmap, pblk->line_meta_pool);
	if (line->invalid_bitmap)
		mempool_free(line->invalid_bitmap, pblk->line_meta_pool);

	*line->vsc = cpu_to_le32(EMPTY_ENTRY);

	line->map_bitmap = NULL;
	line->invalid_bitmap = NULL;
	line->smeta = NULL;
	line->emeta = NULL;
}

void pblk_line_put(struct kref *ref)
{
	struct pblk_line *line = container_of(ref, struct pblk_line, ref);
	struct pblk *pblk = line->pblk;
	struct pblk_line_mgmt *l_mg = &pblk->l_mg;

	spin_lock(&line->lock);
	WARN_ON(line->state != PBLK_LINESTATE_GC);
	line->state = PBLK_LINESTATE_FREE;
	line->gc_group = PBLK_LINEGC_NONE;
	pblk_line_free(pblk, line);
	spin_unlock(&line->lock);

	spin_lock(&l_mg->free_lock);
	list_add_tail(&line->list, &l_mg->free_list);
	l_mg->nr_free_lines++;
	spin_unlock(&l_mg->free_lock);

	pblk_rl_free_lines_inc(&pblk->rl, line);
}

int pblk_blk_erase_async(struct pblk *pblk, struct ppa_addr ppa)
{
	struct nvm_rq *rqd;
	int err;

	rqd = mempool_alloc(pblk->g_rq_pool, GFP_KERNEL);
	memset(rqd, 0, pblk_g_rq_size);

	pblk_setup_e_rq(pblk, rqd, ppa);

	rqd->end_io = pblk_end_io_erase;
	rqd->private = pblk;

	/* The write thread schedules erases so that it minimizes disturbances
	 * with writes. Thus, there is no need to take the LUN semaphore.
	 */
	err = pblk_submit_io(pblk, rqd);
	if (err) {
		struct nvm_tgt_dev *dev = pblk->dev;
		struct nvm_geo *geo = &dev->geo;

		pr_err("pblk: could not async erase line:%d,blk:%d\n",
					pblk_dev_ppa_to_line(ppa),
					pblk_dev_ppa_to_pos(geo, ppa));
	}

	return err;
}

struct pblk_line *pblk_line_get_data(struct pblk *pblk)
{
	return pblk->l_mg.data_line;
}

/* For now, always erase next line */
struct pblk_line *pblk_line_get_erase(struct pblk *pblk)
{
	return pblk->l_mg.data_next;
}

int pblk_line_is_full(struct pblk_line *line)
{
	return (line->left_msecs == 0);
}

void pblk_line_close_meta_sync(struct pblk *pblk)
{
	struct pblk_line_mgmt *l_mg = &pblk->l_mg;
	struct pblk_line_meta *lm = &pblk->lm;
	struct pblk_line *line, *tline;
	LIST_HEAD(list);

	spin_lock(&l_mg->close_lock);
	if (list_empty(&l_mg->emeta_list)) {
		spin_unlock(&l_mg->close_lock);
		return;
	}

	list_cut_position(&list, &l_mg->emeta_list, l_mg->emeta_list.prev);
	spin_unlock(&l_mg->close_lock);

	list_for_each_entry_safe(line, tline, &list, list) {
		struct pblk_emeta *emeta = line->emeta;

		while (emeta->mem < lm->emeta_len[0]) {
			int ret;

			ret = pblk_submit_meta_io(pblk, line);
			if (ret) {
				pr_err("pblk: sync meta line %d failed (%d)\n",
							line->id, ret);
				return;
			}
		}
	}

	pblk_wait_for_meta(pblk);
	flush_workqueue(pblk->close_wq);
}

static void pblk_line_should_sync_meta(struct pblk *pblk)
{
	if (pblk_rl_is_limit(&pblk->rl))
		pblk_line_close_meta_sync(pblk);
}

void pblk_line_close(struct pblk *pblk, struct pblk_line *line)
{
	struct pblk_line_mgmt *l_mg = &pblk->l_mg;
	struct list_head *move_list;

#ifdef CONFIG_NVM_DEBUG
	struct pblk_line_meta *lm = &pblk->lm;

	WARN(!bitmap_full(line->map_bitmap, lm->sec_per_line),
				"pblk: corrupt closed line %d\n", line->id);
#endif

	spin_lock(&l_mg->free_lock);
	WARN_ON(!test_and_clear_bit(line->meta_line, &l_mg->meta_bitmap));
	spin_unlock(&l_mg->free_lock);

	spin_lock(&l_mg->gc_lock);
	spin_lock(&line->lock);
	WARN_ON(line->state != PBLK_LINESTATE_OPEN);
	line->state = PBLK_LINESTATE_CLOSED;
	move_list = pblk_line_gc_list(pblk, line);

	list_add_tail(&line->list, move_list);

	mempool_free(line->map_bitmap, pblk->line_meta_pool);
	line->map_bitmap = NULL;
	line->smeta = NULL;
	line->emeta = NULL;

	spin_unlock(&line->lock);
	spin_unlock(&l_mg->gc_lock);

	pblk_gc_should_kick(pblk);
}

void pblk_line_close_meta(struct pblk *pblk, struct pblk_line *line)
{
	struct pblk_line_mgmt *l_mg = &pblk->l_mg;
	struct pblk_line_meta *lm = &pblk->lm;
	struct pblk_emeta *emeta = line->emeta;
	struct line_emeta *emeta_buf = emeta->buf;

	/* No need for exact vsc value; avoid a big line lock and take aprox. */
	memcpy(emeta_to_vsc(pblk, emeta_buf), l_mg->vsc_list, lm->vsc_list_len);
	memcpy(emeta_to_bb(emeta_buf), line->blk_bitmap, lm->blk_bitmap_len);

	emeta_buf->nr_valid_lbas = cpu_to_le64(line->nr_valid_lbas);
	emeta_buf->crc = cpu_to_le32(pblk_calc_emeta_crc(pblk, emeta_buf));

	spin_lock(&l_mg->close_lock);
	spin_lock(&line->lock);
	list_add_tail(&line->list, &l_mg->emeta_list);
	spin_unlock(&line->lock);
	spin_unlock(&l_mg->close_lock);

	pblk_line_should_sync_meta(pblk);
}

void pblk_line_close_ws(struct work_struct *work)
{
	struct pblk_line_ws *line_ws = container_of(work, struct pblk_line_ws,
									ws);
	struct pblk *pblk = line_ws->pblk;
	struct pblk_line *line = line_ws->line;

	pblk_line_close(pblk, line);
	mempool_free(line_ws, pblk->line_ws_pool);
}

void pblk_line_mark_bb(struct work_struct *work)
{
	struct pblk_line_ws *line_ws = container_of(work, struct pblk_line_ws,
									ws);
	struct pblk *pblk = line_ws->pblk;
	struct nvm_tgt_dev *dev = pblk->dev;
	struct ppa_addr *ppa = line_ws->priv;
	int ret;

	ret = nvm_set_tgt_bb_tbl(dev, ppa, 1, NVM_BLK_T_GRWN_BAD);
	if (ret) {
		struct pblk_line *line;
		int pos;

		line = &pblk->lines[pblk_dev_ppa_to_line(*ppa)];
		pos = pblk_dev_ppa_to_pos(&dev->geo, *ppa);

		pr_err("pblk: failed to mark bb, line:%d, pos:%d\n",
				line->id, pos);
	}

	kfree(ppa);
	mempool_free(line_ws, pblk->line_ws_pool);
}

void pblk_line_run_ws(struct pblk *pblk, struct pblk_line *line, void *priv,
		      void (*work)(struct work_struct *),
		      struct workqueue_struct *wq)
{
	struct pblk_line_ws *line_ws;

	line_ws = mempool_alloc(pblk->line_ws_pool, GFP_ATOMIC);
	if (!line_ws)
		return;

	line_ws->pblk = pblk;
	line_ws->line = line;
	line_ws->priv = priv;

	INIT_WORK(&line_ws->ws, work);
	queue_work(wq, &line_ws->ws);
}

static void __pblk_down_page(struct pblk *pblk, struct ppa_addr *ppa_list,
			     int nr_ppas, int pos)
{
	struct pblk_lun *rlun = &pblk->luns[pos];
	int ret;

	/*
	 * Only send one inflight I/O per LUN. Since we map at a page
	 * granurality, all ppas in the I/O will map to the same LUN
	 */
#ifdef CONFIG_NVM_DEBUG
	int i;

	for (i = 1; i < nr_ppas; i++)
		WARN_ON(ppa_list[0].g.lun != ppa_list[i].g.lun ||
				ppa_list[0].g.ch != ppa_list[i].g.ch);
#endif

	ret = down_timeout(&rlun->wr_sem, msecs_to_jiffies(30000));
	if (ret) {
		switch (ret) {
		case -ETIME:
			pr_err("pblk: lun semaphore timed out\n");
			break;
		case -EINTR:
			pr_err("pblk: lun semaphore timed out\n");
			break;
		}
	}
}

void pblk_down_page(struct pblk *pblk, struct ppa_addr *ppa_list, int nr_ppas)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	int pos = pblk_ppa_to_pos(geo, ppa_list[0]);

	__pblk_down_page(pblk, ppa_list, nr_ppas, pos);
}

void pblk_down_rq(struct pblk *pblk, struct ppa_addr *ppa_list, int nr_ppas,
		  unsigned long *lun_bitmap)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	int pos = pblk_ppa_to_pos(geo, ppa_list[0]);

	/* If the LUN has been locked for this same request, do no attempt to
	 * lock it again
	 */
	if (test_and_set_bit(pos, lun_bitmap))
		return;

	__pblk_down_page(pblk, ppa_list, nr_ppas, pos);
}

void pblk_up_page(struct pblk *pblk, struct ppa_addr *ppa_list, int nr_ppas)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	struct pblk_lun *rlun;
	int pos = pblk_ppa_to_pos(geo, ppa_list[0]);

#ifdef CONFIG_NVM_DEBUG
	int i;

	for (i = 1; i < nr_ppas; i++)
		WARN_ON(ppa_list[0].g.lun != ppa_list[i].g.lun ||
				ppa_list[0].g.ch != ppa_list[i].g.ch);
#endif

	rlun = &pblk->luns[pos];
	up(&rlun->wr_sem);
}

void pblk_up_rq(struct pblk *pblk, struct ppa_addr *ppa_list, int nr_ppas,
		unsigned long *lun_bitmap)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	struct pblk_lun *rlun;
	int nr_luns = geo->nr_luns;
	int bit = -1;

	while ((bit = find_next_bit(lun_bitmap, nr_luns, bit + 1)) < nr_luns) {
		rlun = &pblk->luns[bit];
		up(&rlun->wr_sem);
	}

	kfree(lun_bitmap);
}

void pblk_update_map(struct pblk *pblk, sector_t lba, struct ppa_addr ppa)
{
	struct ppa_addr l2p_ppa;

	/* logic error: lba out-of-bounds. Ignore update */
	if (!(lba < pblk->rl.nr_secs)) {
		WARN(1, "pblk: corrupted L2P map request\n");
		return;
	}

	spin_lock(&pblk->trans_lock);
	l2p_ppa = pblk_trans_map_get(pblk, lba);

	if (!pblk_addr_in_cache(l2p_ppa) && !pblk_ppa_empty(l2p_ppa))
		pblk_map_invalidate(pblk, l2p_ppa);

	pblk_trans_map_set(pblk, lba, ppa);
	spin_unlock(&pblk->trans_lock);
}

void pblk_update_map_cache(struct pblk *pblk, sector_t lba, struct ppa_addr ppa)
{
#ifdef CONFIG_NVM_DEBUG
	/* Callers must ensure that the ppa points to a cache address */
	BUG_ON(!pblk_addr_in_cache(ppa));
	BUG_ON(pblk_rb_pos_oob(&pblk->rwb, pblk_addr_to_cacheline(ppa)));
#endif

	pblk_update_map(pblk, lba, ppa);
}

int pblk_update_map_gc(struct pblk *pblk, sector_t lba, struct ppa_addr ppa,
		       struct pblk_line *gc_line)
{
	struct ppa_addr l2p_ppa;
	int ret = 1;

#ifdef CONFIG_NVM_DEBUG
	/* Callers must ensure that the ppa points to a cache address */
	BUG_ON(!pblk_addr_in_cache(ppa));
	BUG_ON(pblk_rb_pos_oob(&pblk->rwb, pblk_addr_to_cacheline(ppa)));
#endif

	/* logic error: lba out-of-bounds. Ignore update */
	if (!(lba < pblk->rl.nr_secs)) {
		WARN(1, "pblk: corrupted L2P map request\n");
		return 0;
	}

	spin_lock(&pblk->trans_lock);
	l2p_ppa = pblk_trans_map_get(pblk, lba);

	/* Prevent updated entries to be overwritten by GC */
	if (pblk_addr_in_cache(l2p_ppa) || pblk_ppa_empty(l2p_ppa) ||
				pblk_tgt_ppa_to_line(l2p_ppa) != gc_line->id) {
		ret = 0;
		goto out;
	}

	pblk_trans_map_set(pblk, lba, ppa);
out:
	spin_unlock(&pblk->trans_lock);
	return ret;
}

void pblk_update_map_dev(struct pblk *pblk, sector_t lba, struct ppa_addr ppa,
			 struct ppa_addr entry_line)
{
	struct ppa_addr l2p_line;

#ifdef CONFIG_NVM_DEBUG
	/* Callers must ensure that the ppa points to a device address */
	BUG_ON(pblk_addr_in_cache(ppa));
#endif
	/* Invalidate and discard padded entries */
	if (lba == ADDR_EMPTY) {
#ifdef CONFIG_NVM_DEBUG
		atomic_long_inc(&pblk->padded_wb);
#endif
		pblk_map_invalidate(pblk, ppa);
		return;
	}

	/* logic error: lba out-of-bounds. Ignore update */
	if (!(lba < pblk->rl.nr_secs)) {
		WARN(1, "pblk: corrupted L2P map request\n");
		return;
	}

	spin_lock(&pblk->trans_lock);
	l2p_line = pblk_trans_map_get(pblk, lba);

	/* Do not update L2P if the cacheline has been updated. In this case,
	 * the mapped ppa must be invalidated
	 */
	if (l2p_line.ppa != entry_line.ppa) {
		if (!pblk_ppa_empty(ppa))
			pblk_map_invalidate(pblk, ppa);
		goto out;
	}

#ifdef CONFIG_NVM_DEBUG
	WARN_ON(!pblk_addr_in_cache(l2p_line) && !pblk_ppa_empty(l2p_line));
#endif

	pblk_trans_map_set(pblk, lba, ppa);
out:
	spin_unlock(&pblk->trans_lock);
}

void pblk_lookup_l2p_seq(struct pblk *pblk, struct ppa_addr *ppas,
			 sector_t blba, int nr_secs)
{
	int i;

	spin_lock(&pblk->trans_lock);
	for (i = 0; i < nr_secs; i++)
		ppas[i] = pblk_trans_map_get(pblk, blba + i);
	spin_unlock(&pblk->trans_lock);
}

void pblk_lookup_l2p_rand(struct pblk *pblk, struct ppa_addr *ppas,
			  u64 *lba_list, int nr_secs)
{
	sector_t lba;
	int i;

	spin_lock(&pblk->trans_lock);
	for (i = 0; i < nr_secs; i++) {
		lba = lba_list[i];
		if (lba == ADDR_EMPTY) {
			ppas[i].ppa = ADDR_EMPTY;
		} else {
			/* logic error: lba out-of-bounds. Ignore update */
			if (!(lba < pblk->rl.nr_secs)) {
				WARN(1, "pblk: corrupted L2P map request\n");
				continue;
			}
			ppas[i] = pblk_trans_map_get(pblk, lba);
		}
	}
	spin_unlock(&pblk->trans_lock);
}
