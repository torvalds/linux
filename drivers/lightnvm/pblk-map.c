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
 * pblk-map.c - pblk's lba-ppa mapping strategy
 *
 */

#include "pblk.h"

static void pblk_map_page_data(struct pblk *pblk, unsigned int sentry,
			       struct ppa_addr *ppa_list,
			       unsigned long *lun_bitmap,
			       struct pblk_sec_meta *meta_list,
			       unsigned int valid_secs)
{
	struct pblk_line *line = pblk_line_get_data(pblk);
	struct pblk_emeta *emeta;
	struct pblk_w_ctx *w_ctx;
	__le64 *lba_list;
	u64 paddr;
	int nr_secs = pblk->min_write_pgs;
	int i;

	if (pblk_line_is_full(line)) {
		struct pblk_line *prev_line = line;

		line = pblk_line_replace_data(pblk);
		pblk_line_close_meta(pblk, prev_line);
	}

	emeta = line->emeta;
	lba_list = emeta_to_lbas(pblk, emeta->buf);

	paddr = pblk_alloc_page(pblk, line, nr_secs);

	for (i = 0; i < nr_secs; i++, paddr++) {
		__le64 addr_empty = cpu_to_le64(ADDR_EMPTY);

		/* ppa to be sent to the device */
		ppa_list[i] = addr_to_gen_ppa(pblk, paddr, line->id);

		/* Write context for target bio completion on write buffer. Note
		 * that the write buffer is protected by the sync backpointer,
		 * and a single writer thread have access to each specific entry
		 * at a time. Thus, it is safe to modify the context for the
		 * entry we are setting up for submission without taking any
		 * lock or memory barrier.
		 */
		if (i < valid_secs) {
			kref_get(&line->ref);
			w_ctx = pblk_rb_w_ctx(&pblk->rwb, sentry + i);
			w_ctx->ppa = ppa_list[i];
			meta_list[i].lba = cpu_to_le64(w_ctx->lba);
			lba_list[paddr] = cpu_to_le64(w_ctx->lba);
			if (lba_list[paddr] != addr_empty)
				line->nr_valid_lbas++;
		} else {
			lba_list[paddr] = meta_list[i].lba = addr_empty;
			__pblk_map_invalidate(pblk, line, paddr);
		}
	}

	pblk_down_rq(pblk, ppa_list, nr_secs, lun_bitmap);
}

void pblk_map_rq(struct pblk *pblk, struct nvm_rq *rqd, unsigned int sentry,
		 unsigned long *lun_bitmap, unsigned int valid_secs,
		 unsigned int off)
{
	struct pblk_sec_meta *meta_list = rqd->meta_list;
	unsigned int map_secs;
	int min = pblk->min_write_pgs;
	int i;

	for (i = off; i < rqd->nr_ppas; i += min) {
		map_secs = (i + min > valid_secs) ? (valid_secs % min) : min;
		pblk_map_page_data(pblk, sentry + i, &rqd->ppa_list[i],
					lun_bitmap, &meta_list[i], map_secs);
	}
}

/* only if erase_ppa is set, acquire erase semaphore */
void pblk_map_erase_rq(struct pblk *pblk, struct nvm_rq *rqd,
		       unsigned int sentry, unsigned long *lun_bitmap,
		       unsigned int valid_secs, struct ppa_addr *erase_ppa)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	struct pblk_line_meta *lm = &pblk->lm;
	struct pblk_sec_meta *meta_list = rqd->meta_list;
	struct pblk_line *e_line, *d_line;
	unsigned int map_secs;
	int min = pblk->min_write_pgs;
	int i, erase_lun;

	for (i = 0; i < rqd->nr_ppas; i += min) {
		map_secs = (i + min > valid_secs) ? (valid_secs % min) : min;
		pblk_map_page_data(pblk, sentry + i, &rqd->ppa_list[i],
					lun_bitmap, &meta_list[i], map_secs);

		erase_lun = pblk_ppa_to_pos(geo, rqd->ppa_list[i]);

		/* line can change after page map. We might also be writing the
		 * last line.
		 */
		e_line = pblk_line_get_erase(pblk);
		if (!e_line)
			return pblk_map_rq(pblk, rqd, sentry, lun_bitmap,
							valid_secs, i + min);

		spin_lock(&e_line->lock);
		if (!test_bit(erase_lun, e_line->erase_bitmap)) {
			set_bit(erase_lun, e_line->erase_bitmap);
			atomic_dec(&e_line->left_eblks);

			*erase_ppa = rqd->ppa_list[i];
			erase_ppa->g.blk = e_line->id;

			spin_unlock(&e_line->lock);

			/* Avoid evaluating e_line->left_eblks */
			return pblk_map_rq(pblk, rqd, sentry, lun_bitmap,
							valid_secs, i + min);
		}
		spin_unlock(&e_line->lock);
	}

	d_line = pblk_line_get_data(pblk);

	/* line can change after page map. We might also be writing the
	 * last line.
	 */
	e_line = pblk_line_get_erase(pblk);
	if (!e_line)
		return;

	/* Erase blocks that are bad in this line but might not be in next */
	if (unlikely(pblk_ppa_empty(*erase_ppa)) &&
			bitmap_weight(d_line->blk_bitmap, lm->blk_per_line)) {
		int bit = -1;

retry:
		bit = find_next_bit(d_line->blk_bitmap,
						lm->blk_per_line, bit + 1);
		if (bit >= lm->blk_per_line)
			return;

		spin_lock(&e_line->lock);
		if (test_bit(bit, e_line->erase_bitmap)) {
			spin_unlock(&e_line->lock);
			goto retry;
		}
		spin_unlock(&e_line->lock);

		set_bit(bit, e_line->erase_bitmap);
		atomic_dec(&e_line->left_eblks);
		*erase_ppa = pblk->luns[bit].bppa; /* set ch and lun */
		erase_ppa->g.blk = e_line->id;
	}
}
