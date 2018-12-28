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
 * pblk-core.c - pblk's core functionality
 *
 */

#define CREATE_TRACE_POINTS

#include "pblk.h"
#include "pblk-trace.h"

static void pblk_line_mark_bb(struct work_struct *work)
{
	struct pblk_line_ws *line_ws = container_of(work, struct pblk_line_ws,
									ws);
	struct pblk *pblk = line_ws->pblk;
	struct nvm_tgt_dev *dev = pblk->dev;
	struct ppa_addr *ppa = line_ws->priv;
	int ret;

	ret = nvm_set_chunk_meta(dev, ppa, 1, NVM_BLK_T_GRWN_BAD);
	if (ret) {
		struct pblk_line *line;
		int pos;

		line = pblk_ppa_to_line(pblk, *ppa);
		pos = pblk_ppa_to_pos(&dev->geo, *ppa);

		pblk_err(pblk, "failed to mark bb, line:%d, pos:%d\n",
				line->id, pos);
	}

	kfree(ppa);
	mempool_free(line_ws, &pblk->gen_ws_pool);
}

static void pblk_mark_bb(struct pblk *pblk, struct pblk_line *line,
			 struct ppa_addr ppa_addr)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	struct ppa_addr *ppa;
	int pos = pblk_ppa_to_pos(geo, ppa_addr);

	pblk_debug(pblk, "erase failed: line:%d, pos:%d\n", line->id, pos);
	atomic_long_inc(&pblk->erase_failed);

	atomic_dec(&line->blk_in_line);
	if (test_and_set_bit(pos, line->blk_bitmap))
		pblk_err(pblk, "attempted to erase bb: line:%d, pos:%d\n",
							line->id, pos);

	/* Not necessary to mark bad blocks on 2.0 spec. */
	if (geo->version == NVM_OCSSD_SPEC_20)
		return;

	ppa = kmalloc(sizeof(struct ppa_addr), GFP_ATOMIC);
	if (!ppa)
		return;

	*ppa = ppa_addr;
	pblk_gen_run_ws(pblk, NULL, ppa, pblk_line_mark_bb,
						GFP_ATOMIC, pblk->bb_wq);
}

static void __pblk_end_io_erase(struct pblk *pblk, struct nvm_rq *rqd)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	struct nvm_chk_meta *chunk;
	struct pblk_line *line;
	int pos;

	line = pblk_ppa_to_line(pblk, rqd->ppa_addr);
	pos = pblk_ppa_to_pos(geo, rqd->ppa_addr);
	chunk = &line->chks[pos];

	atomic_dec(&line->left_seblks);

	if (rqd->error) {
		trace_pblk_chunk_reset(pblk_disk_name(pblk),
				&rqd->ppa_addr, PBLK_CHUNK_RESET_FAILED);

		chunk->state = NVM_CHK_ST_OFFLINE;
		pblk_mark_bb(pblk, line, rqd->ppa_addr);
	} else {
		trace_pblk_chunk_reset(pblk_disk_name(pblk),
				&rqd->ppa_addr, PBLK_CHUNK_RESET_DONE);

		chunk->state = NVM_CHK_ST_FREE;
	}

	trace_pblk_chunk_state(pblk_disk_name(pblk), &rqd->ppa_addr,
				chunk->state);

	atomic_dec(&pblk->inflight_io);
}

/* Erase completion assumes that only one block is erased at the time */
static void pblk_end_io_erase(struct nvm_rq *rqd)
{
	struct pblk *pblk = rqd->private;

	__pblk_end_io_erase(pblk, rqd);
	mempool_free(rqd, &pblk->e_rq_pool);
}

/*
 * Get information for all chunks from the device.
 *
 * The caller is responsible for freeing (vmalloc) the returned structure
 */
struct nvm_chk_meta *pblk_get_chunk_meta(struct pblk *pblk)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	struct nvm_chk_meta *meta;
	struct ppa_addr ppa;
	unsigned long len;
	int ret;

	ppa.ppa = 0;

	len = geo->all_chunks * sizeof(*meta);
	meta = vzalloc(len);
	if (!meta)
		return ERR_PTR(-ENOMEM);

	ret = nvm_get_chunk_meta(dev, ppa, geo->all_chunks, meta);
	if (ret) {
		kfree(meta);
		return ERR_PTR(-EIO);
	}

	return meta;
}

struct nvm_chk_meta *pblk_chunk_get_off(struct pblk *pblk,
					      struct nvm_chk_meta *meta,
					      struct ppa_addr ppa)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	int ch_off = ppa.m.grp * geo->num_chk * geo->num_lun;
	int lun_off = ppa.m.pu * geo->num_chk;
	int chk_off = ppa.m.chk;

	return meta + ch_off + lun_off + chk_off;
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
	WARN_ON(line->state == PBLK_LINESTATE_FREE);

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
		if (line->state == PBLK_LINESTATE_GC) {
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

#ifdef CONFIG_NVM_PBLK_DEBUG
	/* Callers must ensure that the ppa points to a device address */
	BUG_ON(pblk_addr_in_cache(ppa));
	BUG_ON(pblk_ppa_empty(ppa));
#endif

	line = pblk_ppa_to_line(pblk, ppa);
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

int pblk_alloc_rqd_meta(struct pblk *pblk, struct nvm_rq *rqd)
{
	struct nvm_tgt_dev *dev = pblk->dev;

	rqd->meta_list = nvm_dev_dma_alloc(dev->parent, GFP_KERNEL,
							&rqd->dma_meta_list);
	if (!rqd->meta_list)
		return -ENOMEM;

	if (rqd->nr_ppas == 1)
		return 0;

	rqd->ppa_list = rqd->meta_list + pblk_dma_meta_size(pblk);
	rqd->dma_ppa_list = rqd->dma_meta_list + pblk_dma_meta_size(pblk);

	return 0;
}

void pblk_free_rqd_meta(struct pblk *pblk, struct nvm_rq *rqd)
{
	struct nvm_tgt_dev *dev = pblk->dev;

	if (rqd->meta_list)
		nvm_dev_dma_free(dev->parent, rqd->meta_list,
				rqd->dma_meta_list);
}

/* Caller must guarantee that the request is a valid type */
struct nvm_rq *pblk_alloc_rqd(struct pblk *pblk, int type)
{
	mempool_t *pool;
	struct nvm_rq *rqd;
	int rq_size;

	switch (type) {
	case PBLK_WRITE:
	case PBLK_WRITE_INT:
		pool = &pblk->w_rq_pool;
		rq_size = pblk_w_rq_size;
		break;
	case PBLK_READ:
		pool = &pblk->r_rq_pool;
		rq_size = pblk_g_rq_size;
		break;
	default:
		pool = &pblk->e_rq_pool;
		rq_size = pblk_g_rq_size;
	}

	rqd = mempool_alloc(pool, GFP_KERNEL);
	memset(rqd, 0, rq_size);

	return rqd;
}

/* Typically used on completion path. Cannot guarantee request consistency */
void pblk_free_rqd(struct pblk *pblk, struct nvm_rq *rqd, int type)
{
	mempool_t *pool;

	switch (type) {
	case PBLK_WRITE:
		kfree(((struct pblk_c_ctx *)nvm_rq_to_pdu(rqd))->lun_bitmap);
		/* fall through */
	case PBLK_WRITE_INT:
		pool = &pblk->w_rq_pool;
		break;
	case PBLK_READ:
		pool = &pblk->r_rq_pool;
		break;
	case PBLK_ERASE:
		pool = &pblk->e_rq_pool;
		break;
	default:
		pblk_err(pblk, "trying to free unknown rqd type\n");
		return;
	}

	pblk_free_rqd_meta(pblk, rqd);
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
		mempool_free(bv.bv_page, &pblk->page_bio_pool);
	}
}

int pblk_bio_add_pages(struct pblk *pblk, struct bio *bio, gfp_t flags,
		       int nr_pages)
{
	struct request_queue *q = pblk->dev->q;
	struct page *page;
	int i, ret;

	for (i = 0; i < nr_pages; i++) {
		page = mempool_alloc(&pblk->page_bio_pool, flags);

		ret = bio_add_pc_page(q, bio, page, PBLK_EXPOSED_PAGE_SIZE, 0);
		if (ret != PBLK_EXPOSED_PAGE_SIZE) {
			pblk_err(pblk, "could not add page to bio\n");
			mempool_free(page, &pblk->page_bio_pool);
			goto err;
		}
	}

	return 0;
err:
	pblk_bio_free_pages(pblk, bio, (bio->bi_vcnt - i), i);
	return -1;
}

void pblk_write_kick(struct pblk *pblk)
{
	wake_up_process(pblk->writer_ts);
	mod_timer(&pblk->wtimer, jiffies + msecs_to_jiffies(1000));
}

void pblk_write_timer_fn(struct timer_list *t)
{
	struct pblk *pblk = from_timer(pblk, t, wtimer);

	/* kick the write thread every tick to flush outstanding data */
	pblk_write_kick(pblk);
}

void pblk_write_should_kick(struct pblk *pblk)
{
	unsigned int secs_avail = pblk_rb_read_count(&pblk->rwb);

	if (secs_avail >= pblk->min_write_pgs_data)
		pblk_write_kick(pblk);
}

static void pblk_wait_for_meta(struct pblk *pblk)
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
	int packed_meta = (le32_to_cpu(*line->vsc) / pblk->min_write_pgs_data)
			* (pblk->min_write_pgs - pblk->min_write_pgs_data);
	int vsc = le32_to_cpu(*line->vsc) + packed_meta;

	lockdep_assert_held(&line->lock);

	if (line->w_err_gc->has_write_err) {
		if (line->gc_group != PBLK_LINEGC_WERR) {
			line->gc_group = PBLK_LINEGC_WERR;
			move_list = &l_mg->gc_werr_list;
			pblk_rl_werr_line_in(&pblk->rl);
		}
	} else if (!vsc) {
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
		trace_pblk_line_state(pblk_disk_name(pblk), line->id,
					line->state);

		line->gc_group = PBLK_LINEGC_NONE;
		move_list =  &l_mg->corrupt_list;
		pblk_err(pblk, "corrupted vsc for line %d, vsc:%d (%d/%d/%d)\n",
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

void pblk_log_write_err(struct pblk *pblk, struct nvm_rq *rqd)
{
	atomic_long_inc(&pblk->write_failed);
#ifdef CONFIG_NVM_PBLK_DEBUG
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
		pblk_err(pblk, "unknown read error:%d\n", rqd->error);
	}
#ifdef CONFIG_NVM_PBLK_DEBUG
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

	atomic_inc(&pblk->inflight_io);

#ifdef CONFIG_NVM_PBLK_DEBUG
	if (pblk_check_io(pblk, rqd))
		return NVM_IO_ERR;
#endif

	return nvm_submit_io(dev, rqd);
}

void pblk_check_chunk_state_update(struct pblk *pblk, struct nvm_rq *rqd)
{
	struct ppa_addr *ppa_list = nvm_rq_to_ppa_list(rqd);

	int i;

	for (i = 0; i < rqd->nr_ppas; i++) {
		struct ppa_addr *ppa = &ppa_list[i];
		struct nvm_chk_meta *chunk = pblk_dev_ppa_to_chunk(pblk, *ppa);
		u64 caddr = pblk_dev_ppa_to_chunk_addr(pblk, *ppa);

		if (caddr == 0)
			trace_pblk_chunk_state(pblk_disk_name(pblk),
							ppa, NVM_CHK_ST_OPEN);
		else if (caddr == (chunk->cnlb - 1))
			trace_pblk_chunk_state(pblk_disk_name(pblk),
							ppa, NVM_CHK_ST_CLOSED);
	}
}

int pblk_submit_io_sync(struct pblk *pblk, struct nvm_rq *rqd)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	int ret;

	atomic_inc(&pblk->inflight_io);

#ifdef CONFIG_NVM_PBLK_DEBUG
	if (pblk_check_io(pblk, rqd))
		return NVM_IO_ERR;
#endif

	ret = nvm_submit_io_sync(dev, rqd);

	if (trace_pblk_chunk_state_enabled() && !ret &&
	    rqd->opcode == NVM_OP_PWRITE)
		pblk_check_chunk_state_update(pblk, rqd);

	return ret;
}

int pblk_submit_io_sync_sem(struct pblk *pblk, struct nvm_rq *rqd)
{
	struct ppa_addr *ppa_list;
	int ret;

	ppa_list = (rqd->nr_ppas > 1) ? rqd->ppa_list : &rqd->ppa_addr;

	pblk_down_chunk(pblk, ppa_list[0]);
	ret = pblk_submit_io_sync(pblk, rqd);
	pblk_up_chunk(pblk, ppa_list[0]);

	return ret;
}

static void pblk_bio_map_addr_endio(struct bio *bio)
{
	bio_put(bio);
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
			pblk_err(pblk, "could not map vmalloc bio\n");
			bio_put(bio);
			bio = ERR_PTR(-ENOMEM);
			goto out;
		}

		ret = bio_add_pc_page(dev->q, bio, page, PAGE_SIZE, 0);
		if (ret != PAGE_SIZE) {
			pblk_err(pblk, "could not add page to bio\n");
			bio_put(bio);
			bio = ERR_PTR(-ENOMEM);
			goto out;
		}

		kaddr += PAGE_SIZE;
	}

	bio->bi_end_io = pblk_bio_map_addr_endio;
out:
	return bio;
}

int pblk_calc_secs(struct pblk *pblk, unsigned long secs_avail,
		   unsigned long secs_to_flush, bool skip_meta)
{
	int max = pblk->sec_per_write;
	int min = pblk->min_write_pgs;
	int secs_to_sync = 0;

	if (skip_meta && pblk->min_write_pgs_data != pblk->min_write_pgs)
		min = max = pblk->min_write_pgs_data;

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

	return bit * geo->ws_opt;
}

int pblk_line_smeta_read(struct pblk *pblk, struct pblk_line *line)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct pblk_line_meta *lm = &pblk->lm;
	struct bio *bio;
	struct nvm_rq rqd;
	u64 paddr = pblk_line_smeta_start(pblk, line);
	int i, ret;

	memset(&rqd, 0, sizeof(struct nvm_rq));

	ret = pblk_alloc_rqd_meta(pblk, &rqd);
	if (ret)
		return ret;

	bio = bio_map_kern(dev->q, line->smeta, lm->smeta_len, GFP_KERNEL);
	if (IS_ERR(bio)) {
		ret = PTR_ERR(bio);
		goto clear_rqd;
	}

	bio->bi_iter.bi_sector = 0; /* internal bio */
	bio_set_op_attrs(bio, REQ_OP_READ, 0);

	rqd.bio = bio;
	rqd.opcode = NVM_OP_PREAD;
	rqd.nr_ppas = lm->smeta_sec;
	rqd.is_seq = 1;

	for (i = 0; i < lm->smeta_sec; i++, paddr++)
		rqd.ppa_list[i] = addr_to_gen_ppa(pblk, paddr, line->id);

	ret = pblk_submit_io_sync(pblk, &rqd);
	if (ret) {
		pblk_err(pblk, "smeta I/O submission failed: %d\n", ret);
		bio_put(bio);
		goto clear_rqd;
	}

	atomic_dec(&pblk->inflight_io);

	if (rqd.error)
		pblk_log_read_err(pblk, &rqd);

clear_rqd:
	pblk_free_rqd_meta(pblk, &rqd);
	return ret;
}

static int pblk_line_smeta_write(struct pblk *pblk, struct pblk_line *line,
				 u64 paddr)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct pblk_line_meta *lm = &pblk->lm;
	struct bio *bio;
	struct nvm_rq rqd;
	__le64 *lba_list = emeta_to_lbas(pblk, line->emeta->buf);
	__le64 addr_empty = cpu_to_le64(ADDR_EMPTY);
	int i, ret;

	memset(&rqd, 0, sizeof(struct nvm_rq));

	ret = pblk_alloc_rqd_meta(pblk, &rqd);
	if (ret)
		return ret;

	bio = bio_map_kern(dev->q, line->smeta, lm->smeta_len, GFP_KERNEL);
	if (IS_ERR(bio)) {
		ret = PTR_ERR(bio);
		goto clear_rqd;
	}

	bio->bi_iter.bi_sector = 0; /* internal bio */
	bio_set_op_attrs(bio, REQ_OP_WRITE, 0);

	rqd.bio = bio;
	rqd.opcode = NVM_OP_PWRITE;
	rqd.nr_ppas = lm->smeta_sec;
	rqd.is_seq = 1;

	for (i = 0; i < lm->smeta_sec; i++, paddr++) {
		struct pblk_sec_meta *meta = pblk_get_meta(pblk,
							   rqd.meta_list, i);

		rqd.ppa_list[i] = addr_to_gen_ppa(pblk, paddr, line->id);
		meta->lba = lba_list[paddr] = addr_empty;
	}

	ret = pblk_submit_io_sync_sem(pblk, &rqd);
	if (ret) {
		pblk_err(pblk, "smeta I/O submission failed: %d\n", ret);
		bio_put(bio);
		goto clear_rqd;
	}

	atomic_dec(&pblk->inflight_io);

	if (rqd.error) {
		pblk_log_write_err(pblk, &rqd);
		ret = -EIO;
	}

clear_rqd:
	pblk_free_rqd_meta(pblk, &rqd);
	return ret;
}

int pblk_line_emeta_read(struct pblk *pblk, struct pblk_line *line,
			 void *emeta_buf)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	struct pblk_line_mgmt *l_mg = &pblk->l_mg;
	struct pblk_line_meta *lm = &pblk->lm;
	void *ppa_list, *meta_list;
	struct bio *bio;
	struct nvm_rq rqd;
	u64 paddr = line->emeta_ssec;
	dma_addr_t dma_ppa_list, dma_meta_list;
	int min = pblk->min_write_pgs;
	int left_ppas = lm->emeta_sec[0];
	int line_id = line->id;
	int rq_ppas, rq_len;
	int i, j;
	int ret;

	meta_list = nvm_dev_dma_alloc(dev->parent, GFP_KERNEL,
							&dma_meta_list);
	if (!meta_list)
		return -ENOMEM;

	ppa_list = meta_list + pblk_dma_meta_size(pblk);
	dma_ppa_list = dma_meta_list + pblk_dma_meta_size(pblk);

next_rq:
	memset(&rqd, 0, sizeof(struct nvm_rq));

	rq_ppas = pblk_calc_secs(pblk, left_ppas, 0, false);
	rq_len = rq_ppas * geo->csecs;

	bio = pblk_bio_map_addr(pblk, emeta_buf, rq_ppas, rq_len,
					l_mg->emeta_alloc_type, GFP_KERNEL);
	if (IS_ERR(bio)) {
		ret = PTR_ERR(bio);
		goto free_rqd_dma;
	}

	bio->bi_iter.bi_sector = 0; /* internal bio */
	bio_set_op_attrs(bio, REQ_OP_READ, 0);

	rqd.bio = bio;
	rqd.meta_list = meta_list;
	rqd.ppa_list = ppa_list;
	rqd.dma_meta_list = dma_meta_list;
	rqd.dma_ppa_list = dma_ppa_list;
	rqd.opcode = NVM_OP_PREAD;
	rqd.nr_ppas = rq_ppas;

	for (i = 0; i < rqd.nr_ppas; ) {
		struct ppa_addr ppa = addr_to_gen_ppa(pblk, paddr, line_id);
		int pos = pblk_ppa_to_pos(geo, ppa);

		if (pblk_io_aligned(pblk, rq_ppas))
			rqd.is_seq = 1;

		while (test_bit(pos, line->blk_bitmap)) {
			paddr += min;
			if (pblk_boundary_paddr_checks(pblk, paddr)) {
				bio_put(bio);
				ret = -EINTR;
				goto free_rqd_dma;
			}

			ppa = addr_to_gen_ppa(pblk, paddr, line_id);
			pos = pblk_ppa_to_pos(geo, ppa);
		}

		if (pblk_boundary_paddr_checks(pblk, paddr + min)) {
			bio_put(bio);
			ret = -EINTR;
			goto free_rqd_dma;
		}

		for (j = 0; j < min; j++, i++, paddr++)
			rqd.ppa_list[i] = addr_to_gen_ppa(pblk, paddr, line_id);
	}

	ret = pblk_submit_io_sync(pblk, &rqd);
	if (ret) {
		pblk_err(pblk, "emeta I/O submission failed: %d\n", ret);
		bio_put(bio);
		goto free_rqd_dma;
	}

	atomic_dec(&pblk->inflight_io);

	if (rqd.error)
		pblk_log_read_err(pblk, &rqd);

	emeta_buf += rq_len;
	left_ppas -= rq_ppas;
	if (left_ppas)
		goto next_rq;

free_rqd_dma:
	nvm_dev_dma_free(dev->parent, rqd.meta_list, rqd.dma_meta_list);
	return ret;
}

static void pblk_setup_e_rq(struct pblk *pblk, struct nvm_rq *rqd,
			    struct ppa_addr ppa)
{
	rqd->opcode = NVM_OP_ERASE;
	rqd->ppa_addr = ppa;
	rqd->nr_ppas = 1;
	rqd->is_seq = 1;
	rqd->bio = NULL;
}

static int pblk_blk_erase_sync(struct pblk *pblk, struct ppa_addr ppa)
{
	struct nvm_rq rqd = {NULL};
	int ret;

	trace_pblk_chunk_reset(pblk_disk_name(pblk), &ppa,
				PBLK_CHUNK_RESET_START);

	pblk_setup_e_rq(pblk, &rqd, ppa);

	/* The write thread schedules erases so that it minimizes disturbances
	 * with writes. Thus, there is no need to take the LUN semaphore.
	 */
	ret = pblk_submit_io_sync(pblk, &rqd);
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
		ppa.a.blk = line->id;

		atomic_dec(&line->left_eblks);
		WARN_ON(test_and_set_bit(bit, line->erase_bitmap));
		spin_unlock(&line->lock);

		ret = pblk_blk_erase_sync(pblk, ppa);
		if (ret) {
			pblk_err(pblk, "failed to erase line %d\n", line->id);
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
		trace_pblk_line_state(pblk_disk_name(pblk), line->id,
					line->state);
		spin_unlock(&line->lock);

		list_add_tail(&line->list, &l_mg->bad_list);
		spin_unlock(&l_mg->free_lock);

		pblk_debug(pblk, "line %d is bad\n", line->id);

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
	smeta_buf->header.version_major = SMETA_VERSION_MAJOR;
	smeta_buf->header.version_minor = SMETA_VERSION_MINOR;

	/* Start metadata */
	smeta_buf->seq_nr = cpu_to_le64(line->seq_nr);
	smeta_buf->window_wr_lun = cpu_to_le32(geo->all_luns);

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

	emeta_buf->header.version_major = EMETA_VERSION_MAJOR;
	emeta_buf->header.version_minor = EMETA_VERSION_MINOR;
	emeta_buf->header.crc = cpu_to_le32(
			pblk_calc_meta_header_crc(pblk, &emeta_buf->header));

	emeta_buf->seq_nr = cpu_to_le64(line->seq_nr);
	emeta_buf->nr_lbas = cpu_to_le64(line->sec_in_line);
	emeta_buf->nr_valid_lbas = cpu_to_le64(0);
	emeta_buf->next_id = cpu_to_le32(PBLK_LINE_EMPTY);
	emeta_buf->crc = cpu_to_le32(0);
	emeta_buf->prev_id = smeta_buf->prev_id;

	return 1;
}

static int pblk_line_alloc_bitmaps(struct pblk *pblk, struct pblk_line *line)
{
	struct pblk_line_meta *lm = &pblk->lm;
	struct pblk_line_mgmt *l_mg = &pblk->l_mg;

	line->map_bitmap = mempool_alloc(l_mg->bitmap_pool, GFP_KERNEL);
	if (!line->map_bitmap)
		return -ENOMEM;

	memset(line->map_bitmap, 0, lm->sec_bitmap_len);

	/* will be initialized using bb info from map_bitmap */
	line->invalid_bitmap = mempool_alloc(l_mg->bitmap_pool, GFP_KERNEL);
	if (!line->invalid_bitmap) {
		mempool_free(line->map_bitmap, l_mg->bitmap_pool);
		line->map_bitmap = NULL;
		return -ENOMEM;
	}

	return 0;
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
	u64 off;
	int bit = -1;
	int emeta_secs;

	line->sec_in_line = lm->sec_per_line;

	/* Capture bad block information on line mapping bitmaps */
	while ((bit = find_next_bit(line->blk_bitmap, lm->blk_per_line,
					bit + 1)) < lm->blk_per_line) {
		off = bit * geo->ws_opt;
		bitmap_shift_left(l_mg->bb_aux, l_mg->bb_template, off,
							lm->sec_per_line);
		bitmap_or(line->map_bitmap, line->map_bitmap, l_mg->bb_aux,
							lm->sec_per_line);
		line->sec_in_line -= geo->clba;
	}

	/* Mark smeta metadata sectors as bad sectors */
	bit = find_first_zero_bit(line->blk_bitmap, lm->blk_per_line);
	off = bit * geo->ws_opt;
	bitmap_set(line->map_bitmap, off, lm->smeta_sec);
	line->sec_in_line -= lm->smeta_sec;
	line->smeta_ssec = off;
	line->cur_sec = off + lm->smeta_sec;

	if (init && pblk_line_smeta_write(pblk, line, off)) {
		pblk_debug(pblk, "line smeta I/O failed. Retry\n");
		return 0;
	}

	bitmap_copy(line->invalid_bitmap, line->map_bitmap, lm->sec_per_line);

	/* Mark emeta metadata sectors as bad sectors. We need to consider bad
	 * blocks to make sure that there are enough sectors to store emeta
	 */
	emeta_secs = lm->emeta_sec[0];
	off = lm->sec_per_line;
	while (emeta_secs) {
		off -= geo->ws_opt;
		if (!test_bit(off, line->invalid_bitmap)) {
			bitmap_set(line->invalid_bitmap, off, geo->ws_opt);
			emeta_secs -= geo->ws_opt;
		}
	}

	line->emeta_ssec = off;
	line->sec_in_line -= lm->emeta_sec[0];
	line->nr_valid_lbas = 0;
	line->left_msecs = line->sec_in_line;
	*line->vsc = cpu_to_le32(line->sec_in_line);

	if (lm->sec_per_line - line->sec_in_line !=
		bitmap_weight(line->invalid_bitmap, lm->sec_per_line)) {
		spin_lock(&line->lock);
		line->state = PBLK_LINESTATE_BAD;
		trace_pblk_line_state(pblk_disk_name(pblk), line->id,
					line->state);
		spin_unlock(&line->lock);

		list_add_tail(&line->list, &l_mg->bad_list);
		pblk_err(pblk, "unexpected line %d is bad\n", line->id);

		return 0;
	}

	return 1;
}

static int pblk_prepare_new_line(struct pblk *pblk, struct pblk_line *line)
{
	struct pblk_line_meta *lm = &pblk->lm;
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	int blk_to_erase = atomic_read(&line->blk_in_line);
	int i;

	for (i = 0; i < lm->blk_per_line; i++) {
		struct pblk_lun *rlun = &pblk->luns[i];
		int pos = pblk_ppa_to_pos(geo, rlun->bppa);
		int state = line->chks[pos].state;

		/* Free chunks should not be erased */
		if (state & NVM_CHK_ST_FREE) {
			set_bit(pblk_ppa_to_pos(geo, rlun->bppa),
							line->erase_bitmap);
			blk_to_erase--;
		}
	}

	return blk_to_erase;
}

static int pblk_line_prepare(struct pblk *pblk, struct pblk_line *line)
{
	struct pblk_line_meta *lm = &pblk->lm;
	int blk_in_line = atomic_read(&line->blk_in_line);
	int blk_to_erase;

	/* Bad blocks do not need to be erased */
	bitmap_copy(line->erase_bitmap, line->blk_bitmap, lm->blk_per_line);

	spin_lock(&line->lock);

	/* If we have not written to this line, we need to mark up free chunks
	 * as already erased
	 */
	if (line->state == PBLK_LINESTATE_NEW) {
		blk_to_erase = pblk_prepare_new_line(pblk, line);
		line->state = PBLK_LINESTATE_FREE;
		trace_pblk_line_state(pblk_disk_name(pblk), line->id,
					line->state);
	} else {
		blk_to_erase = blk_in_line;
	}

	if (blk_in_line < lm->min_blk_line) {
		spin_unlock(&line->lock);
		return -EAGAIN;
	}

	if (line->state != PBLK_LINESTATE_FREE) {
		WARN(1, "pblk: corrupted line %d, state %d\n",
							line->id, line->state);
		spin_unlock(&line->lock);
		return -EINTR;
	}

	line->state = PBLK_LINESTATE_OPEN;
	trace_pblk_line_state(pblk_disk_name(pblk), line->id,
				line->state);

	atomic_set(&line->left_eblks, blk_to_erase);
	atomic_set(&line->left_seblks, blk_to_erase);

	line->meta_distance = lm->meta_distance;
	spin_unlock(&line->lock);

	kref_init(&line->ref);

	return 0;
}

/* Line allocations in the recovery path are always single threaded */
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

	ret = pblk_line_alloc_bitmaps(pblk, line);
	if (ret)
		goto fail;

	if (!pblk_line_init_bb(pblk, line, 0)) {
		ret = -EINTR;
		goto fail;
	}

	pblk_rl_free_lines_dec(&pblk->rl, line, true);
	return 0;

fail:
	spin_lock(&l_mg->free_lock);
	list_add(&line->list, &l_mg->free_list);
	spin_unlock(&l_mg->free_lock);

	return ret;
}

void pblk_line_recov_close(struct pblk *pblk, struct pblk_line *line)
{
	struct pblk_line_mgmt *l_mg = &pblk->l_mg;

	mempool_free(line->map_bitmap, l_mg->bitmap_pool);
	line->map_bitmap = NULL;
	line->smeta = NULL;
	line->emeta = NULL;
}

static void pblk_line_reinit(struct pblk_line *line)
{
	*line->vsc = cpu_to_le32(EMPTY_ENTRY);

	line->map_bitmap = NULL;
	line->invalid_bitmap = NULL;
	line->smeta = NULL;
	line->emeta = NULL;
}

void pblk_line_free(struct pblk_line *line)
{
	struct pblk *pblk = line->pblk;
	struct pblk_line_mgmt *l_mg = &pblk->l_mg;

	mempool_free(line->map_bitmap, l_mg->bitmap_pool);
	mempool_free(line->invalid_bitmap, l_mg->bitmap_pool);

	pblk_line_reinit(line);
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
		pblk_err(pblk, "no free lines\n");
		return NULL;
	}

	line = list_first_entry(&l_mg->free_list, struct pblk_line, list);
	list_del(&line->list);
	l_mg->nr_free_lines--;

	bit = find_first_zero_bit(line->blk_bitmap, lm->blk_per_line);
	if (unlikely(bit >= lm->blk_per_line)) {
		spin_lock(&line->lock);
		line->state = PBLK_LINESTATE_BAD;
		trace_pblk_line_state(pblk_disk_name(pblk), line->id,
					line->state);
		spin_unlock(&line->lock);

		list_add_tail(&line->list, &l_mg->bad_list);

		pblk_debug(pblk, "line %d is bad\n", line->id);
		goto retry;
	}

	ret = pblk_line_prepare(pblk, line);
	if (ret) {
		switch (ret) {
		case -EAGAIN:
			list_add(&line->list, &l_mg->bad_list);
			goto retry;
		case -EINTR:
			list_add(&line->list, &l_mg->corrupt_list);
			goto retry;
		default:
			pblk_err(pblk, "failed to prepare line %d\n", line->id);
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

	retry_line->map_bitmap = line->map_bitmap;
	retry_line->invalid_bitmap = line->invalid_bitmap;
	retry_line->smeta = line->smeta;
	retry_line->emeta = line->emeta;
	retry_line->meta_line = line->meta_line;

	pblk_line_reinit(line);

	l_mg->data_line = retry_line;
	spin_unlock(&l_mg->free_lock);

	pblk_rl_free_lines_dec(&pblk->rl, line, false);

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
	}
	spin_unlock(&l_mg->free_lock);

	if (pblk_line_alloc_bitmaps(pblk, line))
		return NULL;

	if (pblk_line_erase(pblk, line)) {
		line = pblk_line_retry(pblk, line);
		if (!line)
			return NULL;
	}

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

	pblk_rl_free_lines_dec(&pblk->rl, line, true);

	return line;
}

void pblk_ppa_to_line_put(struct pblk *pblk, struct ppa_addr ppa)
{
	struct pblk_line *line;

	line = pblk_ppa_to_line(pblk, ppa);
	kref_put(&line->ref, pblk_line_put_wq);
}

void pblk_rq_to_line_put(struct pblk *pblk, struct nvm_rq *rqd)
{
	struct ppa_addr *ppa_list;
	int i;

	ppa_list = (rqd->nr_ppas > 1) ? rqd->ppa_list : &rqd->ppa_addr;

	for (i = 0; i < rqd->nr_ppas; i++)
		pblk_ppa_to_line_put(pblk, ppa_list[i]);
}

static void pblk_stop_writes(struct pblk *pblk, struct pblk_line *line)
{
	lockdep_assert_held(&pblk->l_mg.free_lock);

	pblk_set_space_limit(pblk);
	pblk->state = PBLK_STATE_STOPPING;
	trace_pblk_state(pblk_disk_name(pblk), pblk->state);
}

static void pblk_line_close_meta_sync(struct pblk *pblk)
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
				pblk_err(pblk, "sync meta line %d failed (%d)\n",
							line->id, ret);
				return;
			}
		}
	}

	pblk_wait_for_meta(pblk);
	flush_workqueue(pblk->close_wq);
}

void __pblk_pipeline_flush(struct pblk *pblk)
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
	trace_pblk_state(pblk_disk_name(pblk), pblk->state);
	spin_unlock(&l_mg->free_lock);

	pblk_flush_writer(pblk);
	pblk_wait_for_meta(pblk);

	ret = pblk_recov_pad(pblk);
	if (ret) {
		pblk_err(pblk, "could not close data on teardown(%d)\n", ret);
		return;
	}

	flush_workqueue(pblk->bb_wq);
	pblk_line_close_meta_sync(pblk);
}

void __pblk_pipeline_stop(struct pblk *pblk)
{
	struct pblk_line_mgmt *l_mg = &pblk->l_mg;

	spin_lock(&l_mg->free_lock);
	pblk->state = PBLK_STATE_STOPPED;
	trace_pblk_state(pblk_disk_name(pblk), pblk->state);
	l_mg->data_line = NULL;
	l_mg->data_next = NULL;
	spin_unlock(&l_mg->free_lock);
}

void pblk_pipeline_stop(struct pblk *pblk)
{
	__pblk_pipeline_flush(pblk);
	__pblk_pipeline_stop(pblk);
}

struct pblk_line *pblk_line_replace_data(struct pblk *pblk)
{
	struct pblk_line_mgmt *l_mg = &pblk->l_mg;
	struct pblk_line *cur, *new = NULL;
	unsigned int left_seblks;

	new = l_mg->data_next;
	if (!new)
		goto out;

	spin_lock(&l_mg->free_lock);
	cur = l_mg->data_line;
	l_mg->data_line = new;

	pblk_line_setup_metadata(new, l_mg, &pblk->lm);
	spin_unlock(&l_mg->free_lock);

retry_erase:
	left_seblks = atomic_read(&new->left_seblks);
	if (left_seblks) {
		/* If line is not fully erased, erase it */
		if (atomic_read(&new->left_eblks)) {
			if (pblk_line_erase(pblk, new))
				goto out;
		} else {
			io_schedule();
		}
		goto retry_erase;
	}

	if (pblk_line_alloc_bitmaps(pblk, new))
		return NULL;

retry_setup:
	if (!pblk_line_init_metadata(pblk, new, cur)) {
		new = pblk_line_retry(pblk, new);
		if (!new)
			goto out;

		goto retry_setup;
	}

	if (!pblk_line_init_bb(pblk, new, 1)) {
		new = pblk_line_retry(pblk, new);
		if (!new)
			goto out;

		goto retry_setup;
	}

	pblk_rl_free_lines_dec(&pblk->rl, new, true);

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
	}
	spin_unlock(&l_mg->free_lock);

out:
	return new;
}

static void __pblk_line_put(struct pblk *pblk, struct pblk_line *line)
{
	struct pblk_line_mgmt *l_mg = &pblk->l_mg;
	struct pblk_gc *gc = &pblk->gc;

	spin_lock(&line->lock);
	WARN_ON(line->state != PBLK_LINESTATE_GC);
	line->state = PBLK_LINESTATE_FREE;
	trace_pblk_line_state(pblk_disk_name(pblk), line->id,
					line->state);
	line->gc_group = PBLK_LINEGC_NONE;
	pblk_line_free(line);

	if (line->w_err_gc->has_write_err) {
		pblk_rl_werr_line_out(&pblk->rl);
		line->w_err_gc->has_write_err = 0;
	}

	spin_unlock(&line->lock);
	atomic_dec(&gc->pipeline_gc);

	spin_lock(&l_mg->free_lock);
	list_add_tail(&line->list, &l_mg->free_list);
	l_mg->nr_free_lines++;
	spin_unlock(&l_mg->free_lock);

	pblk_rl_free_lines_inc(&pblk->rl, line);
}

static void pblk_line_put_ws(struct work_struct *work)
{
	struct pblk_line_ws *line_put_ws = container_of(work,
						struct pblk_line_ws, ws);
	struct pblk *pblk = line_put_ws->pblk;
	struct pblk_line *line = line_put_ws->line;

	__pblk_line_put(pblk, line);
	mempool_free(line_put_ws, &pblk->gen_ws_pool);
}

void pblk_line_put(struct kref *ref)
{
	struct pblk_line *line = container_of(ref, struct pblk_line, ref);
	struct pblk *pblk = line->pblk;

	__pblk_line_put(pblk, line);
}

void pblk_line_put_wq(struct kref *ref)
{
	struct pblk_line *line = container_of(ref, struct pblk_line, ref);
	struct pblk *pblk = line->pblk;
	struct pblk_line_ws *line_put_ws;

	line_put_ws = mempool_alloc(&pblk->gen_ws_pool, GFP_ATOMIC);
	if (!line_put_ws)
		return;

	line_put_ws->pblk = pblk;
	line_put_ws->line = line;
	line_put_ws->priv = NULL;

	INIT_WORK(&line_put_ws->ws, pblk_line_put_ws);
	queue_work(pblk->r_end_wq, &line_put_ws->ws);
}

int pblk_blk_erase_async(struct pblk *pblk, struct ppa_addr ppa)
{
	struct nvm_rq *rqd;
	int err;

	rqd = pblk_alloc_rqd(pblk, PBLK_ERASE);

	pblk_setup_e_rq(pblk, rqd, ppa);

	rqd->end_io = pblk_end_io_erase;
	rqd->private = pblk;

	trace_pblk_chunk_reset(pblk_disk_name(pblk),
				&ppa, PBLK_CHUNK_RESET_START);

	/* The write thread schedules erases so that it minimizes disturbances
	 * with writes. Thus, there is no need to take the LUN semaphore.
	 */
	err = pblk_submit_io(pblk, rqd);
	if (err) {
		struct nvm_tgt_dev *dev = pblk->dev;
		struct nvm_geo *geo = &dev->geo;

		pblk_err(pblk, "could not async erase line:%d,blk:%d\n",
					pblk_ppa_to_line_id(ppa),
					pblk_ppa_to_pos(geo, ppa));
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

static void pblk_line_should_sync_meta(struct pblk *pblk)
{
	if (pblk_rl_is_limit(&pblk->rl))
		pblk_line_close_meta_sync(pblk);
}

void pblk_line_close(struct pblk *pblk, struct pblk_line *line)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	struct pblk_line_meta *lm = &pblk->lm;
	struct pblk_line_mgmt *l_mg = &pblk->l_mg;
	struct list_head *move_list;
	int i;

#ifdef CONFIG_NVM_PBLK_DEBUG
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

	mempool_free(line->map_bitmap, l_mg->bitmap_pool);
	line->map_bitmap = NULL;
	line->smeta = NULL;
	line->emeta = NULL;

	for (i = 0; i < lm->blk_per_line; i++) {
		struct pblk_lun *rlun = &pblk->luns[i];
		int pos = pblk_ppa_to_pos(geo, rlun->bppa);
		int state = line->chks[pos].state;

		if (!(state & NVM_CHK_ST_OFFLINE))
			state = NVM_CHK_ST_CLOSED;
	}

	spin_unlock(&line->lock);
	spin_unlock(&l_mg->gc_lock);

	trace_pblk_line_state(pblk_disk_name(pblk), line->id,
					line->state);
}

void pblk_line_close_meta(struct pblk *pblk, struct pblk_line *line)
{
	struct pblk_line_mgmt *l_mg = &pblk->l_mg;
	struct pblk_line_meta *lm = &pblk->lm;
	struct pblk_emeta *emeta = line->emeta;
	struct line_emeta *emeta_buf = emeta->buf;
	struct wa_counters *wa = emeta_to_wa(lm, emeta_buf);

	/* No need for exact vsc value; avoid a big line lock and take aprox. */
	memcpy(emeta_to_vsc(pblk, emeta_buf), l_mg->vsc_list, lm->vsc_list_len);
	memcpy(emeta_to_bb(emeta_buf), line->blk_bitmap, lm->blk_bitmap_len);

	wa->user = cpu_to_le64(atomic64_read(&pblk->user_wa));
	wa->pad = cpu_to_le64(atomic64_read(&pblk->pad_wa));
	wa->gc = cpu_to_le64(atomic64_read(&pblk->gc_wa));

	if (le32_to_cpu(emeta_buf->header.identifier) != PBLK_MAGIC) {
		emeta_buf->header.identifier = cpu_to_le32(PBLK_MAGIC);
		memcpy(emeta_buf->header.uuid, pblk->instance_uuid, 16);
		emeta_buf->header.id = cpu_to_le32(line->id);
		emeta_buf->header.type = cpu_to_le16(line->type);
		emeta_buf->header.version_major = EMETA_VERSION_MAJOR;
		emeta_buf->header.version_minor = EMETA_VERSION_MINOR;
		emeta_buf->header.crc = cpu_to_le32(
			pblk_calc_meta_header_crc(pblk, &emeta_buf->header));
	}

	emeta_buf->nr_valid_lbas = cpu_to_le64(line->nr_valid_lbas);
	emeta_buf->crc = cpu_to_le32(pblk_calc_emeta_crc(pblk, emeta_buf));

	spin_lock(&l_mg->close_lock);
	spin_lock(&line->lock);

	/* Update the in-memory start address for emeta, in case it has
	 * shifted due to write errors
	 */
	if (line->emeta_ssec != line->cur_sec)
		line->emeta_ssec = line->cur_sec;

	list_add_tail(&line->list, &l_mg->emeta_list);
	spin_unlock(&line->lock);
	spin_unlock(&l_mg->close_lock);

	pblk_line_should_sync_meta(pblk);
}

static void pblk_save_lba_list(struct pblk *pblk, struct pblk_line *line)
{
	struct pblk_line_meta *lm = &pblk->lm;
	struct pblk_line_mgmt *l_mg = &pblk->l_mg;
	unsigned int lba_list_size = lm->emeta_len[2];
	struct pblk_w_err_gc *w_err_gc = line->w_err_gc;
	struct pblk_emeta *emeta = line->emeta;

	w_err_gc->lba_list = pblk_malloc(lba_list_size,
					 l_mg->emeta_alloc_type, GFP_KERNEL);
	memcpy(w_err_gc->lba_list, emeta_to_lbas(pblk, emeta->buf),
				lba_list_size);
}

void pblk_line_close_ws(struct work_struct *work)
{
	struct pblk_line_ws *line_ws = container_of(work, struct pblk_line_ws,
									ws);
	struct pblk *pblk = line_ws->pblk;
	struct pblk_line *line = line_ws->line;
	struct pblk_w_err_gc *w_err_gc = line->w_err_gc;

	/* Write errors makes the emeta start address stored in smeta invalid,
	 * so keep a copy of the lba list until we've gc'd the line
	 */
	if (w_err_gc->has_write_err)
		pblk_save_lba_list(pblk, line);

	pblk_line_close(pblk, line);
	mempool_free(line_ws, &pblk->gen_ws_pool);
}

void pblk_gen_run_ws(struct pblk *pblk, struct pblk_line *line, void *priv,
		      void (*work)(struct work_struct *), gfp_t gfp_mask,
		      struct workqueue_struct *wq)
{
	struct pblk_line_ws *line_ws;

	line_ws = mempool_alloc(&pblk->gen_ws_pool, gfp_mask);

	line_ws->pblk = pblk;
	line_ws->line = line;
	line_ws->priv = priv;

	INIT_WORK(&line_ws->ws, work);
	queue_work(wq, &line_ws->ws);
}

static void __pblk_down_chunk(struct pblk *pblk, int pos)
{
	struct pblk_lun *rlun = &pblk->luns[pos];
	int ret;

	/*
	 * Only send one inflight I/O per LUN. Since we map at a page
	 * granurality, all ppas in the I/O will map to the same LUN
	 */

	ret = down_timeout(&rlun->wr_sem, msecs_to_jiffies(30000));
	if (ret == -ETIME || ret == -EINTR)
		pblk_err(pblk, "taking lun semaphore timed out: err %d\n",
				-ret);
}

void pblk_down_chunk(struct pblk *pblk, struct ppa_addr ppa)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	int pos = pblk_ppa_to_pos(geo, ppa);

	__pblk_down_chunk(pblk, pos);
}

void pblk_down_rq(struct pblk *pblk, struct ppa_addr ppa,
		  unsigned long *lun_bitmap)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	int pos = pblk_ppa_to_pos(geo, ppa);

	/* If the LUN has been locked for this same request, do no attempt to
	 * lock it again
	 */
	if (test_and_set_bit(pos, lun_bitmap))
		return;

	__pblk_down_chunk(pblk, pos);
}

void pblk_up_chunk(struct pblk *pblk, struct ppa_addr ppa)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	struct pblk_lun *rlun;
	int pos = pblk_ppa_to_pos(geo, ppa);

	rlun = &pblk->luns[pos];
	up(&rlun->wr_sem);
}

void pblk_up_rq(struct pblk *pblk, unsigned long *lun_bitmap)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	struct pblk_lun *rlun;
	int num_lun = geo->all_luns;
	int bit = -1;

	while ((bit = find_next_bit(lun_bitmap, num_lun, bit + 1)) < num_lun) {
		rlun = &pblk->luns[bit];
		up(&rlun->wr_sem);
	}
}

void pblk_update_map(struct pblk *pblk, sector_t lba, struct ppa_addr ppa)
{
	struct ppa_addr ppa_l2p;

	/* logic error: lba out-of-bounds. Ignore update */
	if (!(lba < pblk->rl.nr_secs)) {
		WARN(1, "pblk: corrupted L2P map request\n");
		return;
	}

	spin_lock(&pblk->trans_lock);
	ppa_l2p = pblk_trans_map_get(pblk, lba);

	if (!pblk_addr_in_cache(ppa_l2p) && !pblk_ppa_empty(ppa_l2p))
		pblk_map_invalidate(pblk, ppa_l2p);

	pblk_trans_map_set(pblk, lba, ppa);
	spin_unlock(&pblk->trans_lock);
}

void pblk_update_map_cache(struct pblk *pblk, sector_t lba, struct ppa_addr ppa)
{

#ifdef CONFIG_NVM_PBLK_DEBUG
	/* Callers must ensure that the ppa points to a cache address */
	BUG_ON(!pblk_addr_in_cache(ppa));
	BUG_ON(pblk_rb_pos_oob(&pblk->rwb, pblk_addr_to_cacheline(ppa)));
#endif

	pblk_update_map(pblk, lba, ppa);
}

int pblk_update_map_gc(struct pblk *pblk, sector_t lba, struct ppa_addr ppa_new,
		       struct pblk_line *gc_line, u64 paddr_gc)
{
	struct ppa_addr ppa_l2p, ppa_gc;
	int ret = 1;

#ifdef CONFIG_NVM_PBLK_DEBUG
	/* Callers must ensure that the ppa points to a cache address */
	BUG_ON(!pblk_addr_in_cache(ppa_new));
	BUG_ON(pblk_rb_pos_oob(&pblk->rwb, pblk_addr_to_cacheline(ppa_new)));
#endif

	/* logic error: lba out-of-bounds. Ignore update */
	if (!(lba < pblk->rl.nr_secs)) {
		WARN(1, "pblk: corrupted L2P map request\n");
		return 0;
	}

	spin_lock(&pblk->trans_lock);
	ppa_l2p = pblk_trans_map_get(pblk, lba);
	ppa_gc = addr_to_gen_ppa(pblk, paddr_gc, gc_line->id);

	if (!pblk_ppa_comp(ppa_l2p, ppa_gc)) {
		spin_lock(&gc_line->lock);
		WARN(!test_bit(paddr_gc, gc_line->invalid_bitmap),
						"pblk: corrupted GC update");
		spin_unlock(&gc_line->lock);

		ret = 0;
		goto out;
	}

	pblk_trans_map_set(pblk, lba, ppa_new);
out:
	spin_unlock(&pblk->trans_lock);
	return ret;
}

void pblk_update_map_dev(struct pblk *pblk, sector_t lba,
			 struct ppa_addr ppa_mapped, struct ppa_addr ppa_cache)
{
	struct ppa_addr ppa_l2p;

#ifdef CONFIG_NVM_PBLK_DEBUG
	/* Callers must ensure that the ppa points to a device address */
	BUG_ON(pblk_addr_in_cache(ppa_mapped));
#endif
	/* Invalidate and discard padded entries */
	if (lba == ADDR_EMPTY) {
		atomic64_inc(&pblk->pad_wa);
#ifdef CONFIG_NVM_PBLK_DEBUG
		atomic_long_inc(&pblk->padded_wb);
#endif
		if (!pblk_ppa_empty(ppa_mapped))
			pblk_map_invalidate(pblk, ppa_mapped);
		return;
	}

	/* logic error: lba out-of-bounds. Ignore update */
	if (!(lba < pblk->rl.nr_secs)) {
		WARN(1, "pblk: corrupted L2P map request\n");
		return;
	}

	spin_lock(&pblk->trans_lock);
	ppa_l2p = pblk_trans_map_get(pblk, lba);

	/* Do not update L2P if the cacheline has been updated. In this case,
	 * the mapped ppa must be invalidated
	 */
	if (!pblk_ppa_comp(ppa_l2p, ppa_cache)) {
		if (!pblk_ppa_empty(ppa_mapped))
			pblk_map_invalidate(pblk, ppa_mapped);
		goto out;
	}

#ifdef CONFIG_NVM_PBLK_DEBUG
	WARN_ON(!pblk_addr_in_cache(ppa_l2p) && !pblk_ppa_empty(ppa_l2p));
#endif

	pblk_trans_map_set(pblk, lba, ppa_mapped);
out:
	spin_unlock(&pblk->trans_lock);
}

void pblk_lookup_l2p_seq(struct pblk *pblk, struct ppa_addr *ppas,
			 sector_t blba, int nr_secs)
{
	int i;

	spin_lock(&pblk->trans_lock);
	for (i = 0; i < nr_secs; i++) {
		struct ppa_addr ppa;

		ppa = ppas[i] = pblk_trans_map_get(pblk, blba + i);

		/* If the L2P entry maps to a line, the reference is valid */
		if (!pblk_ppa_empty(ppa) && !pblk_addr_in_cache(ppa)) {
			struct pblk_line *line = pblk_ppa_to_line(pblk, ppa);

			kref_get(&line->ref);
		}
	}
	spin_unlock(&pblk->trans_lock);
}

void pblk_lookup_l2p_rand(struct pblk *pblk, struct ppa_addr *ppas,
			  u64 *lba_list, int nr_secs)
{
	u64 lba;
	int i;

	spin_lock(&pblk->trans_lock);
	for (i = 0; i < nr_secs; i++) {
		lba = lba_list[i];
		if (lba != ADDR_EMPTY) {
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

void *pblk_get_meta_for_writes(struct pblk *pblk, struct nvm_rq *rqd)
{
	void *buffer;

	if (pblk_is_oob_meta_supported(pblk)) {
		/* Just use OOB metadata buffer as always */
		buffer = rqd->meta_list;
	} else {
		/* We need to reuse last page of request (packed metadata)
		 * in similar way as traditional oob metadata
		 */
		buffer = page_to_virt(
			rqd->bio->bi_io_vec[rqd->bio->bi_vcnt - 1].bv_page);
	}

	return buffer;
}

void pblk_get_packed_meta(struct pblk *pblk, struct nvm_rq *rqd)
{
	void *meta_list = rqd->meta_list;
	void *page;
	int i = 0;

	if (pblk_is_oob_meta_supported(pblk))
		return;

	page = page_to_virt(rqd->bio->bi_io_vec[rqd->bio->bi_vcnt - 1].bv_page);
	/* We need to fill oob meta buffer with data from packed metadata */
	for (; i < rqd->nr_ppas; i++)
		memcpy(pblk_get_meta(pblk, meta_list, i),
			page + (i * sizeof(struct pblk_sec_meta)),
			sizeof(struct pblk_sec_meta));
}
