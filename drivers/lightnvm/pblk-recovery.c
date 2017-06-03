/*
 * Copyright (C) 2016 CNEX Labs
 * Initial: Javier Gonzalez <javier@cnexlabs.com>
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
 * pblk-recovery.c - pblk's recovery path
 */

#include "pblk.h"

void pblk_submit_rec(struct work_struct *work)
{
	struct pblk_rec_ctx *recovery =
			container_of(work, struct pblk_rec_ctx, ws_rec);
	struct pblk *pblk = recovery->pblk;
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_rq *rqd = recovery->rqd;
	struct pblk_c_ctx *c_ctx = nvm_rq_to_pdu(rqd);
	int max_secs = nvm_max_phys_sects(dev);
	struct bio *bio;
	unsigned int nr_rec_secs;
	unsigned int pgs_read;
	int ret;

	nr_rec_secs = bitmap_weight((unsigned long int *)&rqd->ppa_status,
								max_secs);

	bio = bio_alloc(GFP_KERNEL, nr_rec_secs);
	if (!bio) {
		pr_err("pblk: not able to create recovery bio\n");
		return;
	}

	bio->bi_iter.bi_sector = 0;
	bio_set_op_attrs(bio, REQ_OP_WRITE, 0);
	rqd->bio = bio;
	rqd->nr_ppas = nr_rec_secs;

	pgs_read = pblk_rb_read_to_bio_list(&pblk->rwb, bio, &recovery->failed,
								nr_rec_secs);
	if (pgs_read != nr_rec_secs) {
		pr_err("pblk: could not read recovery entries\n");
		goto err;
	}

	if (pblk_setup_w_rec_rq(pblk, rqd, c_ctx)) {
		pr_err("pblk: could not setup recovery request\n");
		goto err;
	}

#ifdef CONFIG_NVM_DEBUG
	atomic_long_add(nr_rec_secs, &pblk->recov_writes);
#endif

	ret = pblk_submit_io(pblk, rqd);
	if (ret) {
		pr_err("pblk: I/O submission failed: %d\n", ret);
		goto err;
	}

	mempool_free(recovery, pblk->rec_pool);
	return;

err:
	bio_put(bio);
	pblk_free_rqd(pblk, rqd, WRITE);
}

int pblk_recov_setup_rq(struct pblk *pblk, struct pblk_c_ctx *c_ctx,
			struct pblk_rec_ctx *recovery, u64 *comp_bits,
			unsigned int comp)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	int max_secs = nvm_max_phys_sects(dev);
	struct nvm_rq *rec_rqd;
	struct pblk_c_ctx *rec_ctx;
	int nr_entries = c_ctx->nr_valid + c_ctx->nr_padded;

	rec_rqd = pblk_alloc_rqd(pblk, WRITE);
	if (IS_ERR(rec_rqd)) {
		pr_err("pblk: could not create recovery req.\n");
		return -ENOMEM;
	}

	rec_ctx = nvm_rq_to_pdu(rec_rqd);

	/* Copy completion bitmap, but exclude the first X completed entries */
	bitmap_shift_right((unsigned long int *)&rec_rqd->ppa_status,
				(unsigned long int *)comp_bits,
				comp, max_secs);

	/* Save the context for the entries that need to be re-written and
	 * update current context with the completed entries.
	 */
	rec_ctx->sentry = pblk_rb_wrap_pos(&pblk->rwb, c_ctx->sentry + comp);
	if (comp >= c_ctx->nr_valid) {
		rec_ctx->nr_valid = 0;
		rec_ctx->nr_padded = nr_entries - comp;

		c_ctx->nr_padded = comp - c_ctx->nr_valid;
	} else {
		rec_ctx->nr_valid = c_ctx->nr_valid - comp;
		rec_ctx->nr_padded = c_ctx->nr_padded;

		c_ctx->nr_valid = comp;
		c_ctx->nr_padded = 0;
	}

	recovery->rqd = rec_rqd;
	recovery->pblk = pblk;

	return 0;
}

__le64 *pblk_recov_get_lba_list(struct pblk *pblk, struct line_emeta *emeta)
{
	u32 crc;

	crc = pblk_calc_emeta_crc(pblk, emeta);
	if (le32_to_cpu(emeta->crc) != crc)
		return NULL;

	if (le32_to_cpu(emeta->header.identifier) != PBLK_MAGIC)
		return NULL;

	return pblk_line_emeta_to_lbas(emeta);
}

static int pblk_recov_l2p_from_emeta(struct pblk *pblk, struct pblk_line *line)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	struct pblk_line_meta *lm = &pblk->lm;
	struct line_emeta *emeta = line->emeta;
	__le64 *lba_list;
	int data_start;
	int nr_data_lbas, nr_valid_lbas, nr_lbas = 0;
	int i;

	lba_list = pblk_recov_get_lba_list(pblk, emeta);
	if (!lba_list)
		return 1;

	data_start = pblk_line_smeta_start(pblk, line) + lm->smeta_sec;
	nr_data_lbas = lm->sec_per_line - lm->emeta_sec;
	nr_valid_lbas = le64_to_cpu(emeta->nr_valid_lbas);

	for (i = data_start; i < nr_data_lbas && nr_lbas < nr_valid_lbas; i++) {
		struct ppa_addr ppa;
		int pos;

		ppa = addr_to_pblk_ppa(pblk, i, line->id);
		pos = pblk_ppa_to_pos(geo, ppa);

		/* Do not update bad blocks */
		if (test_bit(pos, line->blk_bitmap))
			continue;

		if (le64_to_cpu(lba_list[i]) == ADDR_EMPTY) {
			spin_lock(&line->lock);
			if (test_and_set_bit(i, line->invalid_bitmap))
				WARN_ONCE(1, "pblk: rec. double invalidate:\n");
			else
				line->vsc--;
			spin_unlock(&line->lock);

			continue;
		}

		pblk_update_map(pblk, le64_to_cpu(lba_list[i]), ppa);
		nr_lbas++;
	}

	if (nr_valid_lbas != nr_lbas)
		pr_err("pblk: line %d - inconsistent lba list(%llu/%d)\n",
				line->id, line->emeta->nr_valid_lbas, nr_lbas);

	line->left_msecs = 0;

	return 0;
}

static int pblk_calc_sec_in_line(struct pblk *pblk, struct pblk_line *line)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	struct pblk_line_meta *lm = &pblk->lm;
	int nr_bb = bitmap_weight(line->blk_bitmap, lm->blk_per_line);

	return lm->sec_per_line - lm->smeta_sec - lm->emeta_sec -
				nr_bb * geo->sec_per_blk;
}

struct pblk_recov_alloc {
	struct ppa_addr *ppa_list;
	struct pblk_sec_meta *meta_list;
	struct nvm_rq *rqd;
	void *data;
	dma_addr_t dma_ppa_list;
	dma_addr_t dma_meta_list;
};

static int pblk_recov_read_oob(struct pblk *pblk, struct pblk_line *line,
			       struct pblk_recov_alloc p, u64 r_ptr)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	struct ppa_addr *ppa_list;
	struct pblk_sec_meta *meta_list;
	struct nvm_rq *rqd;
	struct bio *bio;
	void *data;
	dma_addr_t dma_ppa_list, dma_meta_list;
	u64 r_ptr_int;
	int left_ppas;
	int rq_ppas, rq_len;
	int i, j;
	int ret = 0;
	DECLARE_COMPLETION_ONSTACK(wait);

	ppa_list = p.ppa_list;
	meta_list = p.meta_list;
	rqd = p.rqd;
	data = p.data;
	dma_ppa_list = p.dma_ppa_list;
	dma_meta_list = p.dma_meta_list;

	left_ppas = line->cur_sec - r_ptr;
	if (!left_ppas)
		return 0;

	r_ptr_int = r_ptr;

next_read_rq:
	memset(rqd, 0, pblk_r_rq_size);

	rq_ppas = pblk_calc_secs(pblk, left_ppas, 0);
	if (!rq_ppas)
		rq_ppas = pblk->min_write_pgs;
	rq_len = rq_ppas * geo->sec_size;

	bio = bio_map_kern(dev->q, data, rq_len, GFP_KERNEL);
	if (IS_ERR(bio))
		return PTR_ERR(bio);

	bio->bi_iter.bi_sector = 0; /* internal bio */
	bio_set_op_attrs(bio, REQ_OP_READ, 0);

	rqd->bio = bio;
	rqd->opcode = NVM_OP_PREAD;
	rqd->flags = pblk_set_read_mode(pblk);
	rqd->meta_list = meta_list;
	rqd->nr_ppas = rq_ppas;
	rqd->ppa_list = ppa_list;
	rqd->dma_ppa_list = dma_ppa_list;
	rqd->dma_meta_list = dma_meta_list;
	rqd->end_io = pblk_end_io_sync;
	rqd->private = &wait;

	for (i = 0; i < rqd->nr_ppas; ) {
		struct ppa_addr ppa;
		int pos;

		ppa = addr_to_gen_ppa(pblk, r_ptr_int, line->id);
		pos = pblk_dev_ppa_to_pos(geo, ppa);

		while (test_bit(pos, line->blk_bitmap)) {
			r_ptr_int += pblk->min_write_pgs;
			ppa = addr_to_gen_ppa(pblk, r_ptr_int, line->id);
			pos = pblk_dev_ppa_to_pos(geo, ppa);
		}

		for (j = 0; j < pblk->min_write_pgs; j++, i++, r_ptr_int++)
			rqd->ppa_list[i] =
				addr_to_gen_ppa(pblk, r_ptr_int, line->id);
	}

	/* If read fails, more padding is needed */
	ret = pblk_submit_io(pblk, rqd);
	if (ret) {
		pr_err("pblk: I/O submission failed: %d\n", ret);
		return ret;
	}

	if (!wait_for_completion_io_timeout(&wait,
				msecs_to_jiffies(PBLK_COMMAND_TIMEOUT_MS))) {
		pr_err("pblk: L2P recovery read timed out\n");
		return -EINTR;
	}

	reinit_completion(&wait);

	/* At this point, the read should not fail. If it does, it is a problem
	 * we cannot recover from here. Need FTL log.
	 */
	if (rqd->error) {
		pr_err("pblk: L2P recovery failed (%d)\n", rqd->error);
		return -EINTR;
	}

	for (i = 0; i < rqd->nr_ppas; i++) {
		u64 lba = le64_to_cpu(meta_list[i].lba);

		if (lba == ADDR_EMPTY || lba > pblk->rl.nr_secs)
			continue;

		pblk_update_map(pblk, lba, rqd->ppa_list[i]);
	}

	left_ppas -= rq_ppas;
	if (left_ppas > 0)
		goto next_read_rq;

	return 0;
}

static int pblk_recov_pad_oob(struct pblk *pblk, struct pblk_line *line,
			      struct pblk_recov_alloc p, int left_ppas)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	struct ppa_addr *ppa_list;
	struct pblk_sec_meta *meta_list;
	struct nvm_rq *rqd;
	struct bio *bio;
	void *data;
	dma_addr_t dma_ppa_list, dma_meta_list;
	__le64 *lba_list = pblk_line_emeta_to_lbas(line->emeta);
	u64 w_ptr = line->cur_sec;
	int left_line_ppas = line->left_msecs;
	int rq_ppas, rq_len;
	int i, j;
	int ret = 0;
	DECLARE_COMPLETION_ONSTACK(wait);

	ppa_list = p.ppa_list;
	meta_list = p.meta_list;
	rqd = p.rqd;
	data = p.data;
	dma_ppa_list = p.dma_ppa_list;
	dma_meta_list = p.dma_meta_list;

next_pad_rq:
	rq_ppas = pblk_calc_secs(pblk, left_ppas, 0);
	if (!rq_ppas)
		rq_ppas = pblk->min_write_pgs;
	rq_len = rq_ppas * geo->sec_size;

	bio = bio_map_kern(dev->q, data, rq_len, GFP_KERNEL);
	if (IS_ERR(bio))
		return PTR_ERR(bio);

	bio->bi_iter.bi_sector = 0; /* internal bio */
	bio_set_op_attrs(bio, REQ_OP_WRITE, 0);

	memset(rqd, 0, pblk_r_rq_size);

	rqd->bio = bio;
	rqd->opcode = NVM_OP_PWRITE;
	rqd->flags = pblk_set_progr_mode(pblk, WRITE);
	rqd->meta_list = meta_list;
	rqd->nr_ppas = rq_ppas;
	rqd->ppa_list = ppa_list;
	rqd->dma_ppa_list = dma_ppa_list;
	rqd->dma_meta_list = dma_meta_list;
	rqd->end_io = pblk_end_io_sync;
	rqd->private = &wait;

	for (i = 0; i < rqd->nr_ppas; ) {
		struct ppa_addr ppa;
		int pos;

		w_ptr = pblk_alloc_page(pblk, line, pblk->min_write_pgs);
		ppa = addr_to_pblk_ppa(pblk, w_ptr, line->id);
		pos = pblk_ppa_to_pos(geo, ppa);

		while (test_bit(pos, line->blk_bitmap)) {
			w_ptr += pblk->min_write_pgs;
			ppa = addr_to_pblk_ppa(pblk, w_ptr, line->id);
			pos = pblk_ppa_to_pos(geo, ppa);
		}

		for (j = 0; j < pblk->min_write_pgs; j++, i++, w_ptr++) {
			struct ppa_addr dev_ppa;

			dev_ppa = addr_to_gen_ppa(pblk, w_ptr, line->id);

			pblk_map_invalidate(pblk, dev_ppa);
			meta_list[i].lba = cpu_to_le64(ADDR_EMPTY);
			lba_list[w_ptr] = cpu_to_le64(ADDR_EMPTY);
			rqd->ppa_list[i] = dev_ppa;
		}
	}

	ret = pblk_submit_io(pblk, rqd);
	if (ret) {
		pr_err("pblk: I/O submission failed: %d\n", ret);
		return ret;
	}

	if (!wait_for_completion_io_timeout(&wait,
				msecs_to_jiffies(PBLK_COMMAND_TIMEOUT_MS))) {
		pr_err("pblk: L2P recovery write timed out\n");
	}
	reinit_completion(&wait);

	left_line_ppas -= rq_ppas;
	left_ppas -= rq_ppas;
	if (left_ppas > 0 && left_line_ppas)
		goto next_pad_rq;

	return 0;
}

/* When this function is called, it means that not all upper pages have been
 * written in a page that contains valid data. In order to recover this data, we
 * first find the write pointer on the device, then we pad all necessary
 * sectors, and finally attempt to read the valid data
 */
static int pblk_recov_scan_all_oob(struct pblk *pblk, struct pblk_line *line,
				   struct pblk_recov_alloc p)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	struct ppa_addr *ppa_list;
	struct pblk_sec_meta *meta_list;
	struct nvm_rq *rqd;
	struct bio *bio;
	void *data;
	dma_addr_t dma_ppa_list, dma_meta_list;
	u64 w_ptr = 0, r_ptr;
	int rq_ppas, rq_len;
	int i, j;
	int ret = 0;
	int rec_round;
	int left_ppas = pblk_calc_sec_in_line(pblk, line) - line->cur_sec;
	DECLARE_COMPLETION_ONSTACK(wait);

	ppa_list = p.ppa_list;
	meta_list = p.meta_list;
	rqd = p.rqd;
	data = p.data;
	dma_ppa_list = p.dma_ppa_list;
	dma_meta_list = p.dma_meta_list;

	/* we could recover up until the line write pointer */
	r_ptr = line->cur_sec;
	rec_round = 0;

next_rq:
	memset(rqd, 0, pblk_r_rq_size);

	rq_ppas = pblk_calc_secs(pblk, left_ppas, 0);
	if (!rq_ppas)
		rq_ppas = pblk->min_write_pgs;
	rq_len = rq_ppas * geo->sec_size;

	bio = bio_map_kern(dev->q, data, rq_len, GFP_KERNEL);
	if (IS_ERR(bio))
		return PTR_ERR(bio);

	bio->bi_iter.bi_sector = 0; /* internal bio */
	bio_set_op_attrs(bio, REQ_OP_READ, 0);

	rqd->bio = bio;
	rqd->opcode = NVM_OP_PREAD;
	rqd->flags = pblk_set_read_mode(pblk);
	rqd->meta_list = meta_list;
	rqd->nr_ppas = rq_ppas;
	rqd->ppa_list = ppa_list;
	rqd->dma_ppa_list = dma_ppa_list;
	rqd->dma_meta_list = dma_meta_list;
	rqd->end_io = pblk_end_io_sync;
	rqd->private = &wait;

	for (i = 0; i < rqd->nr_ppas; ) {
		struct ppa_addr ppa;
		int pos;

		w_ptr = pblk_alloc_page(pblk, line, pblk->min_write_pgs);
		ppa = addr_to_gen_ppa(pblk, w_ptr, line->id);
		pos = pblk_dev_ppa_to_pos(geo, ppa);

		while (test_bit(pos, line->blk_bitmap)) {
			w_ptr += pblk->min_write_pgs;
			ppa = addr_to_gen_ppa(pblk, w_ptr, line->id);
			pos = pblk_dev_ppa_to_pos(geo, ppa);
		}

		for (j = 0; j < pblk->min_write_pgs; j++, i++, w_ptr++)
			rqd->ppa_list[i] =
				addr_to_gen_ppa(pblk, w_ptr, line->id);
	}

	ret = pblk_submit_io(pblk, rqd);
	if (ret) {
		pr_err("pblk: I/O submission failed: %d\n", ret);
		return ret;
	}

	if (!wait_for_completion_io_timeout(&wait,
				msecs_to_jiffies(PBLK_COMMAND_TIMEOUT_MS))) {
		pr_err("pblk: L2P recovery read timed out\n");
	}
	reinit_completion(&wait);

	/* This should not happen since the read failed during normal recovery,
	 * but the media works funny sometimes...
	 */
	if (!rec_round++ && !rqd->error) {
		rec_round = 0;
		for (i = 0; i < rqd->nr_ppas; i++, r_ptr++) {
			u64 lba = le64_to_cpu(meta_list[i].lba);

			if (lba == ADDR_EMPTY || lba > pblk->rl.nr_secs)
				continue;

			pblk_update_map(pblk, lba, rqd->ppa_list[i]);
		}
	}

	/* Reached the end of the written line */
	if (rqd->error == NVM_RSP_ERR_EMPTYPAGE) {
		int pad_secs, nr_error_bits, bit;
		int ret;

		bit = find_first_bit((void *)&rqd->ppa_status, rqd->nr_ppas);
		nr_error_bits = rqd->nr_ppas - bit;

		/* Roll back failed sectors */
		line->cur_sec -= nr_error_bits;
		line->left_msecs += nr_error_bits;
		bitmap_clear(line->map_bitmap, line->cur_sec, nr_error_bits);

		pad_secs = pblk_pad_distance(pblk);
		if (pad_secs > line->left_msecs)
			pad_secs = line->left_msecs;

		ret = pblk_recov_pad_oob(pblk, line, p, pad_secs);
		if (ret)
			pr_err("pblk: OOB padding failed (err:%d)\n", ret);

		ret = pblk_recov_read_oob(pblk, line, p, r_ptr);
		if (ret)
			pr_err("pblk: OOB read failed (err:%d)\n", ret);

		line->left_ssecs = line->left_msecs;
		left_ppas = 0;
	}

	left_ppas -= rq_ppas;
	if (left_ppas > 0)
		goto next_rq;

	return ret;
}

static int pblk_recov_scan_oob(struct pblk *pblk, struct pblk_line *line,
			       struct pblk_recov_alloc p, int *done)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	struct ppa_addr *ppa_list;
	struct pblk_sec_meta *meta_list;
	struct nvm_rq *rqd;
	struct bio *bio;
	void *data;
	dma_addr_t dma_ppa_list, dma_meta_list;
	u64 paddr;
	int rq_ppas, rq_len;
	int i, j;
	int ret = 0;
	int left_ppas = pblk_calc_sec_in_line(pblk, line);
	DECLARE_COMPLETION_ONSTACK(wait);

	ppa_list = p.ppa_list;
	meta_list = p.meta_list;
	rqd = p.rqd;
	data = p.data;
	dma_ppa_list = p.dma_ppa_list;
	dma_meta_list = p.dma_meta_list;

	*done = 1;

next_rq:
	memset(rqd, 0, pblk_r_rq_size);

	rq_ppas = pblk_calc_secs(pblk, left_ppas, 0);
	if (!rq_ppas)
		rq_ppas = pblk->min_write_pgs;
	rq_len = rq_ppas * geo->sec_size;

	bio = bio_map_kern(dev->q, data, rq_len, GFP_KERNEL);
	if (IS_ERR(bio))
		return PTR_ERR(bio);

	bio->bi_iter.bi_sector = 0; /* internal bio */
	bio_set_op_attrs(bio, REQ_OP_READ, 0);

	rqd->bio = bio;
	rqd->opcode = NVM_OP_PREAD;
	rqd->flags = pblk_set_read_mode(pblk);
	rqd->meta_list = meta_list;
	rqd->nr_ppas = rq_ppas;
	rqd->ppa_list = ppa_list;
	rqd->dma_ppa_list = dma_ppa_list;
	rqd->dma_meta_list = dma_meta_list;
	rqd->end_io = pblk_end_io_sync;
	rqd->private = &wait;

	for (i = 0; i < rqd->nr_ppas; ) {
		struct ppa_addr ppa;
		int pos;

		paddr = pblk_alloc_page(pblk, line, pblk->min_write_pgs);
		ppa = addr_to_gen_ppa(pblk, paddr, line->id);
		pos = pblk_dev_ppa_to_pos(geo, ppa);

		while (test_bit(pos, line->blk_bitmap)) {
			paddr += pblk->min_write_pgs;
			ppa = addr_to_gen_ppa(pblk, paddr, line->id);
			pos = pblk_dev_ppa_to_pos(geo, ppa);
		}

		for (j = 0; j < pblk->min_write_pgs; j++, i++, paddr++)
			rqd->ppa_list[i] =
				addr_to_gen_ppa(pblk, paddr, line->id);
	}

	ret = pblk_submit_io(pblk, rqd);
	if (ret) {
		pr_err("pblk: I/O submission failed: %d\n", ret);
		bio_put(bio);
		return ret;
	}

	if (!wait_for_completion_io_timeout(&wait,
				msecs_to_jiffies(PBLK_COMMAND_TIMEOUT_MS))) {
		pr_err("pblk: L2P recovery read timed out\n");
	}
	reinit_completion(&wait);

	/* Reached the end of the written line */
	if (rqd->error) {
		int nr_error_bits, bit;

		bit = find_first_bit((void *)&rqd->ppa_status, rqd->nr_ppas);
		nr_error_bits = rqd->nr_ppas - bit;

		/* Roll back failed sectors */
		line->cur_sec -= nr_error_bits;
		line->left_msecs += nr_error_bits;
		line->left_ssecs = line->left_msecs;
		bitmap_clear(line->map_bitmap, line->cur_sec, nr_error_bits);

		left_ppas = 0;
		rqd->nr_ppas = bit;

		if (rqd->error != NVM_RSP_ERR_EMPTYPAGE)
			*done = 0;
	}

	for (i = 0; i < rqd->nr_ppas; i++) {
		u64 lba = le64_to_cpu(meta_list[i].lba);

		if (lba == ADDR_EMPTY || lba > pblk->rl.nr_secs)
			continue;

		pblk_update_map(pblk, lba, rqd->ppa_list[i]);
	}

	left_ppas -= rq_ppas;
	if (left_ppas > 0)
		goto next_rq;

	return ret;
}

/* Scan line for lbas on out of bound area */
static int pblk_recov_l2p_from_oob(struct pblk *pblk, struct pblk_line *line)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	struct nvm_rq *rqd;
	struct ppa_addr *ppa_list;
	struct pblk_sec_meta *meta_list;
	struct pblk_recov_alloc p;
	void *data;
	dma_addr_t dma_ppa_list, dma_meta_list;
	int done, ret = 0;

	rqd = pblk_alloc_rqd(pblk, READ);
	if (IS_ERR(rqd))
		return PTR_ERR(rqd);

	meta_list = nvm_dev_dma_alloc(dev->parent, GFP_KERNEL, &dma_meta_list);
	if (!meta_list) {
		ret = -ENOMEM;
		goto free_rqd;
	}

	ppa_list = (void *)(meta_list) + pblk_dma_meta_size;
	dma_ppa_list = dma_meta_list + pblk_dma_meta_size;

	data = kcalloc(pblk->max_write_pgs, geo->sec_size, GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto free_meta_list;
	}

	p.ppa_list = ppa_list;
	p.meta_list = meta_list;
	p.rqd = rqd;
	p.data = data;
	p.dma_ppa_list = dma_ppa_list;
	p.dma_meta_list = dma_meta_list;

	ret = pblk_recov_scan_oob(pblk, line, p, &done);
	if (ret) {
		pr_err("pblk: could not recover L2P from OOB\n");
		goto out;
	}

	if (!done) {
		ret = pblk_recov_scan_all_oob(pblk, line, p);
		if (ret) {
			pr_err("pblk: could not recover L2P from OOB\n");
			goto out;
		}
	}

	if (pblk_line_is_full(line))
		pblk_line_recov_close(pblk, line);

out:
	kfree(data);
free_meta_list:
	nvm_dev_dma_free(dev->parent, meta_list, dma_meta_list);
free_rqd:
	pblk_free_rqd(pblk, rqd, READ);

	return ret;
}

/* Insert lines ordered by sequence number (seq_num) on list */
static void pblk_recov_line_add_ordered(struct list_head *head,
					struct pblk_line *line)
{
	struct pblk_line *t = NULL;

	list_for_each_entry(t, head, list)
		if (t->seq_nr > line->seq_nr)
			break;

	__list_add(&line->list, t->list.prev, &t->list);
}

struct pblk_line *pblk_recov_l2p(struct pblk *pblk)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	struct pblk_line_meta *lm = &pblk->lm;
	struct pblk_line_mgmt *l_mg = &pblk->l_mg;
	struct pblk_line *line, *tline, *data_line = NULL;
	struct line_smeta *smeta;
	struct line_emeta *emeta;
	int found_lines = 0, recovered_lines = 0, open_lines = 0;
	int is_next = 0;
	int meta_line;
	int i, valid_uuid = 0;
	LIST_HEAD(recov_list);

	/* TODO: Implement FTL snapshot */

	/* Scan recovery - takes place when FTL snapshot fails */
	spin_lock(&l_mg->free_lock);
	meta_line = find_first_zero_bit(&l_mg->meta_bitmap, PBLK_DATA_LINES);
	set_bit(meta_line, &l_mg->meta_bitmap);
	smeta = l_mg->sline_meta[meta_line].meta;
	emeta = l_mg->eline_meta[meta_line].meta;
	spin_unlock(&l_mg->free_lock);

	/* Order data lines using their sequence number */
	for (i = 0; i < l_mg->nr_lines; i++) {
		u32 crc;

		line = &pblk->lines[i];

		memset(smeta, 0, lm->smeta_len);
		line->smeta = smeta;
		line->lun_bitmap = ((void *)(smeta)) +
						sizeof(struct line_smeta);

		/* Lines that cannot be read are assumed as not written here */
		if (pblk_line_read_smeta(pblk, line))
			continue;

		crc = pblk_calc_smeta_crc(pblk, smeta);
		if (le32_to_cpu(smeta->crc) != crc)
			continue;

		if (le32_to_cpu(smeta->header.identifier) != PBLK_MAGIC)
			continue;

		if (le16_to_cpu(smeta->header.version) != 1) {
			pr_err("pblk: found incompatible line version %u\n",
					smeta->header.version);
			return ERR_PTR(-EINVAL);
		}

		/* The first valid instance uuid is used for initialization */
		if (!valid_uuid) {
			memcpy(pblk->instance_uuid, smeta->header.uuid, 16);
			valid_uuid = 1;
		}

		if (memcmp(pblk->instance_uuid, smeta->header.uuid, 16)) {
			pr_debug("pblk: ignore line %u due to uuid mismatch\n",
					i);
			continue;
		}

		/* Update line metadata */
		spin_lock(&line->lock);
		line->id = le32_to_cpu(line->smeta->header.id);
		line->type = le16_to_cpu(line->smeta->header.type);
		line->seq_nr = le64_to_cpu(line->smeta->seq_nr);
		spin_unlock(&line->lock);

		/* Update general metadata */
		spin_lock(&l_mg->free_lock);
		if (line->seq_nr >= l_mg->d_seq_nr)
			l_mg->d_seq_nr = line->seq_nr + 1;
		l_mg->nr_free_lines--;
		spin_unlock(&l_mg->free_lock);

		if (pblk_line_recov_alloc(pblk, line))
			goto out;

		pblk_recov_line_add_ordered(&recov_list, line);
		found_lines++;
		pr_debug("pblk: recovering data line %d, seq:%llu\n",
						line->id, smeta->seq_nr);
	}

	if (!found_lines) {
		pblk_setup_uuid(pblk);

		spin_lock(&l_mg->free_lock);
		WARN_ON_ONCE(!test_and_clear_bit(meta_line,
							&l_mg->meta_bitmap));
		spin_unlock(&l_mg->free_lock);

		goto out;
	}

	/* Verify closed blocks and recover this portion of L2P table*/
	list_for_each_entry_safe(line, tline, &recov_list, list) {
		int off, nr_bb;

		recovered_lines++;
		/* Calculate where emeta starts based on the line bb */
		off = lm->sec_per_line - lm->emeta_sec;
		nr_bb = bitmap_weight(line->blk_bitmap, lm->blk_per_line);
		off -= nr_bb * geo->sec_per_pl;

		memset(emeta, 0, lm->emeta_len);
		line->emeta = emeta;
		line->emeta_ssec = off;

		if (pblk_line_read_emeta(pblk, line)) {
			pblk_recov_l2p_from_oob(pblk, line);
			goto next;
		}

		if (pblk_recov_l2p_from_emeta(pblk, line))
			pblk_recov_l2p_from_oob(pblk, line);

next:
		if (pblk_line_is_full(line)) {
			struct list_head *move_list;

			spin_lock(&line->lock);
			line->state = PBLK_LINESTATE_CLOSED;
			move_list = pblk_line_gc_list(pblk, line);
			spin_unlock(&line->lock);

			spin_lock(&l_mg->gc_lock);
			list_move_tail(&line->list, move_list);
			spin_unlock(&l_mg->gc_lock);

			mempool_free(line->map_bitmap, pblk->line_meta_pool);
			line->map_bitmap = NULL;
			line->smeta = NULL;
			line->emeta = NULL;
		} else {
			if (open_lines > 1)
				pr_err("pblk: failed to recover L2P\n");

			open_lines++;
			line->meta_line = meta_line;
			data_line = line;
		}
	}

	spin_lock(&l_mg->free_lock);
	if (!open_lines) {
		WARN_ON_ONCE(!test_and_clear_bit(meta_line,
							&l_mg->meta_bitmap));
		pblk_line_replace_data(pblk);
	} else {
		/* Allocate next line for preparation */
		l_mg->data_next = pblk_line_get(pblk);
		if (l_mg->data_next) {
			l_mg->data_next->seq_nr = l_mg->d_seq_nr++;
			l_mg->data_next->type = PBLK_LINETYPE_DATA;
			is_next = 1;
		}
	}
	spin_unlock(&l_mg->free_lock);

	if (is_next) {
		pblk_line_erase(pblk, l_mg->data_next);
		pblk_rl_free_lines_dec(&pblk->rl, l_mg->data_next);
	}

out:
	if (found_lines != recovered_lines)
		pr_err("pblk: failed to recover all found lines %d/%d\n",
						found_lines, recovered_lines);

	return data_line;
}

/*
 * Pad until smeta can be read on current data line
 */
void pblk_recov_pad(struct pblk *pblk)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	struct pblk_line *line;
	struct pblk_line_mgmt *l_mg = &pblk->l_mg;
	struct nvm_rq *rqd;
	struct pblk_recov_alloc p;
	struct ppa_addr *ppa_list;
	struct pblk_sec_meta *meta_list;
	void *data;
	dma_addr_t dma_ppa_list, dma_meta_list;

	spin_lock(&l_mg->free_lock);
	line = l_mg->data_line;
	spin_unlock(&l_mg->free_lock);

	rqd = pblk_alloc_rqd(pblk, READ);
	if (IS_ERR(rqd))
		return;

	meta_list = nvm_dev_dma_alloc(dev->parent, GFP_KERNEL, &dma_meta_list);
	if (!meta_list)
		goto free_rqd;

	ppa_list = (void *)(meta_list) + pblk_dma_meta_size;
	dma_ppa_list = dma_meta_list + pblk_dma_meta_size;

	data = kcalloc(pblk->max_write_pgs, geo->sec_size, GFP_KERNEL);
	if (!data)
		goto free_meta_list;

	p.ppa_list = ppa_list;
	p.meta_list = meta_list;
	p.rqd = rqd;
	p.data = data;
	p.dma_ppa_list = dma_ppa_list;
	p.dma_meta_list = dma_meta_list;

	if (pblk_recov_pad_oob(pblk, line, p, line->left_msecs)) {
		pr_err("pblk: Tear down padding failed\n");
		goto free_data;
	}

	pblk_line_close(pblk, line);

free_data:
	kfree(data);
free_meta_list:
	nvm_dev_dma_free(dev->parent, meta_list, dma_meta_list);
free_rqd:
	pblk_free_rqd(pblk, rqd, READ);
}
