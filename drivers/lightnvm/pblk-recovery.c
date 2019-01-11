// SPDX-License-Identifier: GPL-2.0
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
 *
 * The L2P recovery path is single threaded as the L2P table is updated in order
 * following the line sequence ID.
 */

#include "pblk.h"
#include "pblk-trace.h"

int pblk_recov_check_emeta(struct pblk *pblk, struct line_emeta *emeta_buf)
{
	u32 crc;

	crc = pblk_calc_emeta_crc(pblk, emeta_buf);
	if (le32_to_cpu(emeta_buf->crc) != crc)
		return 1;

	if (le32_to_cpu(emeta_buf->header.identifier) != PBLK_MAGIC)
		return 1;

	return 0;
}

static int pblk_recov_l2p_from_emeta(struct pblk *pblk, struct pblk_line *line)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	struct pblk_line_meta *lm = &pblk->lm;
	struct pblk_emeta *emeta = line->emeta;
	struct line_emeta *emeta_buf = emeta->buf;
	__le64 *lba_list;
	u64 data_start, data_end;
	u64 nr_valid_lbas, nr_lbas = 0;
	u64 i;

	lba_list = emeta_to_lbas(pblk, emeta_buf);
	if (!lba_list)
		return 1;

	data_start = pblk_line_smeta_start(pblk, line) + lm->smeta_sec;
	data_end = line->emeta_ssec;
	nr_valid_lbas = le64_to_cpu(emeta_buf->nr_valid_lbas);

	for (i = data_start; i < data_end; i++) {
		struct ppa_addr ppa;
		int pos;

		ppa = addr_to_gen_ppa(pblk, i, line->id);
		pos = pblk_ppa_to_pos(geo, ppa);

		/* Do not update bad blocks */
		if (test_bit(pos, line->blk_bitmap))
			continue;

		if (le64_to_cpu(lba_list[i]) == ADDR_EMPTY) {
			spin_lock(&line->lock);
			if (test_and_set_bit(i, line->invalid_bitmap))
				WARN_ONCE(1, "pblk: rec. double invalidate:\n");
			else
				le32_add_cpu(line->vsc, -1);
			spin_unlock(&line->lock);

			continue;
		}

		pblk_update_map(pblk, le64_to_cpu(lba_list[i]), ppa);
		nr_lbas++;
	}

	if (nr_valid_lbas != nr_lbas)
		pblk_err(pblk, "line %d - inconsistent lba list(%llu/%llu)\n",
				line->id, nr_valid_lbas, nr_lbas);

	line->left_msecs = 0;

	return 0;
}

static void pblk_update_line_wp(struct pblk *pblk, struct pblk_line *line,
				u64 written_secs)
{
	int i;

	for (i = 0; i < written_secs; i += pblk->min_write_pgs)
		pblk_alloc_page(pblk, line, pblk->min_write_pgs);
}

static u64 pblk_sec_in_open_line(struct pblk *pblk, struct pblk_line *line)
{
	struct pblk_line_meta *lm = &pblk->lm;
	int nr_bb = bitmap_weight(line->blk_bitmap, lm->blk_per_line);
	u64 written_secs = 0;
	int valid_chunks = 0;
	int i;

	for (i = 0; i < lm->blk_per_line; i++) {
		struct nvm_chk_meta *chunk = &line->chks[i];

		if (chunk->state & NVM_CHK_ST_OFFLINE)
			continue;

		written_secs += chunk->wp;
		valid_chunks++;
	}

	if (lm->blk_per_line - nr_bb != valid_chunks)
		pblk_err(pblk, "recovery line %d is bad\n", line->id);

	pblk_update_line_wp(pblk, line, written_secs - lm->smeta_sec);

	return written_secs;
}

struct pblk_recov_alloc {
	struct ppa_addr *ppa_list;
	void *meta_list;
	struct nvm_rq *rqd;
	void *data;
	dma_addr_t dma_ppa_list;
	dma_addr_t dma_meta_list;
};

static void pblk_recov_complete(struct kref *ref)
{
	struct pblk_pad_rq *pad_rq = container_of(ref, struct pblk_pad_rq, ref);

	complete(&pad_rq->wait);
}

static void pblk_end_io_recov(struct nvm_rq *rqd)
{
	struct ppa_addr *ppa_list = nvm_rq_to_ppa_list(rqd);
	struct pblk_pad_rq *pad_rq = rqd->private;
	struct pblk *pblk = pad_rq->pblk;

	pblk_up_chunk(pblk, ppa_list[0]);

	pblk_free_rqd(pblk, rqd, PBLK_WRITE_INT);

	atomic_dec(&pblk->inflight_io);
	kref_put(&pad_rq->ref, pblk_recov_complete);
}

/* pad line using line bitmap.  */
static int pblk_recov_pad_line(struct pblk *pblk, struct pblk_line *line,
			       int left_ppas)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	void *meta_list;
	struct pblk_pad_rq *pad_rq;
	struct nvm_rq *rqd;
	struct bio *bio;
	void *data;
	__le64 *lba_list = emeta_to_lbas(pblk, line->emeta->buf);
	u64 w_ptr = line->cur_sec;
	int left_line_ppas, rq_ppas, rq_len;
	int i, j;
	int ret = 0;

	spin_lock(&line->lock);
	left_line_ppas = line->left_msecs;
	spin_unlock(&line->lock);

	pad_rq = kmalloc(sizeof(struct pblk_pad_rq), GFP_KERNEL);
	if (!pad_rq)
		return -ENOMEM;

	data = vzalloc(array_size(pblk->max_write_pgs, geo->csecs));
	if (!data) {
		ret = -ENOMEM;
		goto free_rq;
	}

	pad_rq->pblk = pblk;
	init_completion(&pad_rq->wait);
	kref_init(&pad_rq->ref);

next_pad_rq:
	rq_ppas = pblk_calc_secs(pblk, left_ppas, 0, false);
	if (rq_ppas < pblk->min_write_pgs) {
		pblk_err(pblk, "corrupted pad line %d\n", line->id);
		goto fail_free_pad;
	}

	rq_len = rq_ppas * geo->csecs;

	bio = pblk_bio_map_addr(pblk, data, rq_ppas, rq_len,
						PBLK_VMALLOC_META, GFP_KERNEL);
	if (IS_ERR(bio)) {
		ret = PTR_ERR(bio);
		goto fail_free_pad;
	}

	bio->bi_iter.bi_sector = 0; /* internal bio */
	bio_set_op_attrs(bio, REQ_OP_WRITE, 0);

	rqd = pblk_alloc_rqd(pblk, PBLK_WRITE_INT);

	ret = pblk_alloc_rqd_meta(pblk, rqd);
	if (ret)
		goto fail_free_rqd;

	rqd->bio = bio;
	rqd->opcode = NVM_OP_PWRITE;
	rqd->is_seq = 1;
	rqd->nr_ppas = rq_ppas;
	rqd->end_io = pblk_end_io_recov;
	rqd->private = pad_rq;

	meta_list = rqd->meta_list;

	for (i = 0; i < rqd->nr_ppas; ) {
		struct ppa_addr ppa;
		int pos;

		w_ptr = pblk_alloc_page(pblk, line, pblk->min_write_pgs);
		ppa = addr_to_gen_ppa(pblk, w_ptr, line->id);
		pos = pblk_ppa_to_pos(geo, ppa);

		while (test_bit(pos, line->blk_bitmap)) {
			w_ptr += pblk->min_write_pgs;
			ppa = addr_to_gen_ppa(pblk, w_ptr, line->id);
			pos = pblk_ppa_to_pos(geo, ppa);
		}

		for (j = 0; j < pblk->min_write_pgs; j++, i++, w_ptr++) {
			struct ppa_addr dev_ppa;
			struct pblk_sec_meta *meta;
			__le64 addr_empty = cpu_to_le64(ADDR_EMPTY);

			dev_ppa = addr_to_gen_ppa(pblk, w_ptr, line->id);

			pblk_map_invalidate(pblk, dev_ppa);
			lba_list[w_ptr] = addr_empty;
			meta = pblk_get_meta(pblk, meta_list, i);
			meta->lba = addr_empty;
			rqd->ppa_list[i] = dev_ppa;
		}
	}

	kref_get(&pad_rq->ref);
	pblk_down_chunk(pblk, rqd->ppa_list[0]);

	ret = pblk_submit_io(pblk, rqd);
	if (ret) {
		pblk_err(pblk, "I/O submission failed: %d\n", ret);
		pblk_up_chunk(pblk, rqd->ppa_list[0]);
		goto fail_free_rqd;
	}

	left_line_ppas -= rq_ppas;
	left_ppas -= rq_ppas;
	if (left_ppas && left_line_ppas)
		goto next_pad_rq;

	kref_put(&pad_rq->ref, pblk_recov_complete);

	if (!wait_for_completion_io_timeout(&pad_rq->wait,
				msecs_to_jiffies(PBLK_COMMAND_TIMEOUT_MS))) {
		pblk_err(pblk, "pad write timed out\n");
		ret = -ETIME;
	}

	if (!pblk_line_is_full(line))
		pblk_err(pblk, "corrupted padded line: %d\n", line->id);

	vfree(data);
free_rq:
	kfree(pad_rq);
	return ret;

fail_free_rqd:
	pblk_free_rqd(pblk, rqd, PBLK_WRITE_INT);
	bio_put(bio);
fail_free_pad:
	kfree(pad_rq);
	vfree(data);
	return ret;
}

static int pblk_pad_distance(struct pblk *pblk, struct pblk_line *line)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	int distance = geo->mw_cunits * geo->all_luns * geo->ws_opt;

	return (distance > line->left_msecs) ? line->left_msecs : distance;
}

static int pblk_line_wp_is_unbalanced(struct pblk *pblk,
				      struct pblk_line *line)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	struct pblk_line_meta *lm = &pblk->lm;
	struct pblk_lun *rlun;
	struct nvm_chk_meta *chunk;
	struct ppa_addr ppa;
	u64 line_wp;
	int pos, i;

	rlun = &pblk->luns[0];
	ppa = rlun->bppa;
	pos = pblk_ppa_to_pos(geo, ppa);
	chunk = &line->chks[pos];

	line_wp = chunk->wp;

	for (i = 1; i < lm->blk_per_line; i++) {
		rlun = &pblk->luns[i];
		ppa = rlun->bppa;
		pos = pblk_ppa_to_pos(geo, ppa);
		chunk = &line->chks[pos];

		if (chunk->wp > line_wp)
			return 1;
		else if (chunk->wp < line_wp)
			line_wp = chunk->wp;
	}

	return 0;
}

static int pblk_recov_scan_oob(struct pblk *pblk, struct pblk_line *line,
			       struct pblk_recov_alloc p)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct pblk_line_meta *lm = &pblk->lm;
	struct nvm_geo *geo = &dev->geo;
	struct ppa_addr *ppa_list;
	void *meta_list;
	struct nvm_rq *rqd;
	struct bio *bio;
	void *data;
	dma_addr_t dma_ppa_list, dma_meta_list;
	__le64 *lba_list;
	u64 paddr = pblk_line_smeta_start(pblk, line) + lm->smeta_sec;
	bool padded = false;
	int rq_ppas, rq_len;
	int i, j;
	int ret;
	u64 left_ppas = pblk_sec_in_open_line(pblk, line) - lm->smeta_sec;

	if (pblk_line_wp_is_unbalanced(pblk, line))
		pblk_warn(pblk, "recovering unbalanced line (%d)\n", line->id);

	ppa_list = p.ppa_list;
	meta_list = p.meta_list;
	rqd = p.rqd;
	data = p.data;
	dma_ppa_list = p.dma_ppa_list;
	dma_meta_list = p.dma_meta_list;

	lba_list = emeta_to_lbas(pblk, line->emeta->buf);

next_rq:
	memset(rqd, 0, pblk_g_rq_size);

	rq_ppas = pblk_calc_secs(pblk, left_ppas, 0, false);
	if (!rq_ppas)
		rq_ppas = pblk->min_write_pgs;
	rq_len = rq_ppas * geo->csecs;

retry_rq:
	bio = bio_map_kern(dev->q, data, rq_len, GFP_KERNEL);
	if (IS_ERR(bio))
		return PTR_ERR(bio);

	bio->bi_iter.bi_sector = 0; /* internal bio */
	bio_set_op_attrs(bio, REQ_OP_READ, 0);
	bio_get(bio);

	rqd->bio = bio;
	rqd->opcode = NVM_OP_PREAD;
	rqd->meta_list = meta_list;
	rqd->nr_ppas = rq_ppas;
	rqd->ppa_list = ppa_list;
	rqd->dma_ppa_list = dma_ppa_list;
	rqd->dma_meta_list = dma_meta_list;

	if (pblk_io_aligned(pblk, rq_ppas))
		rqd->is_seq = 1;

	for (i = 0; i < rqd->nr_ppas; ) {
		struct ppa_addr ppa;
		int pos;

		ppa = addr_to_gen_ppa(pblk, paddr, line->id);
		pos = pblk_ppa_to_pos(geo, ppa);

		while (test_bit(pos, line->blk_bitmap)) {
			paddr += pblk->min_write_pgs;
			ppa = addr_to_gen_ppa(pblk, paddr, line->id);
			pos = pblk_ppa_to_pos(geo, ppa);
		}

		for (j = 0; j < pblk->min_write_pgs; j++, i++)
			rqd->ppa_list[i] =
				addr_to_gen_ppa(pblk, paddr + j, line->id);
	}

	ret = pblk_submit_io_sync(pblk, rqd);
	if (ret) {
		pblk_err(pblk, "I/O submission failed: %d\n", ret);
		bio_put(bio);
		return ret;
	}

	atomic_dec(&pblk->inflight_io);

	/* If a read fails, do a best effort by padding the line and retrying */
	if (rqd->error) {
		int pad_distance, ret;

		if (padded) {
			pblk_log_read_err(pblk, rqd);
			bio_put(bio);
			return -EINTR;
		}

		pad_distance = pblk_pad_distance(pblk, line);
		ret = pblk_recov_pad_line(pblk, line, pad_distance);
		if (ret) {
			bio_put(bio);
			return ret;
		}

		padded = true;
		bio_put(bio);
		goto retry_rq;
	}

	pblk_get_packed_meta(pblk, rqd);
	bio_put(bio);

	for (i = 0; i < rqd->nr_ppas; i++) {
		struct pblk_sec_meta *meta = pblk_get_meta(pblk, meta_list, i);
		u64 lba = le64_to_cpu(meta->lba);

		lba_list[paddr++] = cpu_to_le64(lba);

		if (lba == ADDR_EMPTY || lba > pblk->rl.nr_secs)
			continue;

		line->nr_valid_lbas++;
		pblk_update_map(pblk, lba, rqd->ppa_list[i]);
	}

	left_ppas -= rq_ppas;
	if (left_ppas > 0)
		goto next_rq;

#ifdef CONFIG_NVM_PBLK_DEBUG
	WARN_ON(padded && !pblk_line_is_full(line));
#endif

	return 0;
}

/* Scan line for lbas on out of bound area */
static int pblk_recov_l2p_from_oob(struct pblk *pblk, struct pblk_line *line)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	struct nvm_rq *rqd;
	struct ppa_addr *ppa_list;
	void *meta_list;
	struct pblk_recov_alloc p;
	void *data;
	dma_addr_t dma_ppa_list, dma_meta_list;
	int ret = 0;

	meta_list = nvm_dev_dma_alloc(dev->parent, GFP_KERNEL, &dma_meta_list);
	if (!meta_list)
		return -ENOMEM;

	ppa_list = (void *)(meta_list) + pblk_dma_meta_size(pblk);
	dma_ppa_list = dma_meta_list + pblk_dma_meta_size(pblk);

	data = kcalloc(pblk->max_write_pgs, geo->csecs, GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto free_meta_list;
	}

	rqd = mempool_alloc(&pblk->r_rq_pool, GFP_KERNEL);
	memset(rqd, 0, pblk_g_rq_size);

	p.ppa_list = ppa_list;
	p.meta_list = meta_list;
	p.rqd = rqd;
	p.data = data;
	p.dma_ppa_list = dma_ppa_list;
	p.dma_meta_list = dma_meta_list;

	ret = pblk_recov_scan_oob(pblk, line, p);
	if (ret) {
		pblk_err(pblk, "could not recover L2P form OOB\n");
		goto out;
	}

	if (pblk_line_is_full(line))
		pblk_line_recov_close(pblk, line);

out:
	mempool_free(rqd, &pblk->r_rq_pool);
	kfree(data);
free_meta_list:
	nvm_dev_dma_free(dev->parent, meta_list, dma_meta_list);

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

static u64 pblk_line_emeta_start(struct pblk *pblk, struct pblk_line *line)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	struct pblk_line_meta *lm = &pblk->lm;
	unsigned int emeta_secs;
	u64 emeta_start;
	struct ppa_addr ppa;
	int pos;

	emeta_secs = lm->emeta_sec[0];
	emeta_start = lm->sec_per_line;

	while (emeta_secs) {
		emeta_start--;
		ppa = addr_to_gen_ppa(pblk, emeta_start, line->id);
		pos = pblk_ppa_to_pos(geo, ppa);
		if (!test_bit(pos, line->blk_bitmap))
			emeta_secs--;
	}

	return emeta_start;
}

static int pblk_recov_check_line_version(struct pblk *pblk,
					 struct line_emeta *emeta)
{
	struct line_header *header = &emeta->header;

	if (header->version_major != EMETA_VERSION_MAJOR) {
		pblk_err(pblk, "line major version mismatch: %d, expected: %d\n",
			 header->version_major, EMETA_VERSION_MAJOR);
		return 1;
	}

#ifdef CONFIG_NVM_PBLK_DEBUG
	if (header->version_minor > EMETA_VERSION_MINOR)
		pblk_info(pblk, "newer line minor version found: %d\n",
				header->version_minor);
#endif

	return 0;
}

static void pblk_recov_wa_counters(struct pblk *pblk,
				   struct line_emeta *emeta)
{
	struct pblk_line_meta *lm = &pblk->lm;
	struct line_header *header = &emeta->header;
	struct wa_counters *wa = emeta_to_wa(lm, emeta);

	/* WA counters were introduced in emeta version 0.2 */
	if (header->version_major > 0 || header->version_minor >= 2) {
		u64 user = le64_to_cpu(wa->user);
		u64 pad = le64_to_cpu(wa->pad);
		u64 gc = le64_to_cpu(wa->gc);

		atomic64_set(&pblk->user_wa, user);
		atomic64_set(&pblk->pad_wa, pad);
		atomic64_set(&pblk->gc_wa, gc);

		pblk->user_rst_wa = user;
		pblk->pad_rst_wa = pad;
		pblk->gc_rst_wa = gc;
	}
}

static int pblk_line_was_written(struct pblk_line *line,
				 struct pblk *pblk)
{

	struct pblk_line_meta *lm = &pblk->lm;
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	struct nvm_chk_meta *chunk;
	struct ppa_addr bppa;
	int smeta_blk;

	if (line->state == PBLK_LINESTATE_BAD)
		return 0;

	smeta_blk = find_first_zero_bit(line->blk_bitmap, lm->blk_per_line);
	if (smeta_blk >= lm->blk_per_line)
		return 0;

	bppa = pblk->luns[smeta_blk].bppa;
	chunk = &line->chks[pblk_ppa_to_pos(geo, bppa)];

	if (chunk->state & NVM_CHK_ST_FREE)
		return 0;

	return 1;
}

static bool pblk_line_is_open(struct pblk *pblk, struct pblk_line *line)
{
	struct pblk_line_meta *lm = &pblk->lm;
	int i;

	for (i = 0; i < lm->blk_per_line; i++)
		if (line->chks[i].state & NVM_CHK_ST_OPEN)
			return true;

	return false;
}

struct pblk_line *pblk_recov_l2p(struct pblk *pblk)
{
	struct pblk_line_meta *lm = &pblk->lm;
	struct pblk_line_mgmt *l_mg = &pblk->l_mg;
	struct pblk_line *line, *tline, *data_line = NULL;
	struct pblk_smeta *smeta;
	struct pblk_emeta *emeta;
	struct line_smeta *smeta_buf;
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
	smeta = l_mg->sline_meta[meta_line];
	emeta = l_mg->eline_meta[meta_line];
	smeta_buf = (struct line_smeta *)smeta;
	spin_unlock(&l_mg->free_lock);

	/* Order data lines using their sequence number */
	for (i = 0; i < l_mg->nr_lines; i++) {
		u32 crc;

		line = &pblk->lines[i];

		memset(smeta, 0, lm->smeta_len);
		line->smeta = smeta;
		line->lun_bitmap = ((void *)(smeta_buf)) +
						sizeof(struct line_smeta);

		if (!pblk_line_was_written(line, pblk))
			continue;

		/* Lines that cannot be read are assumed as not written here */
		if (pblk_line_smeta_read(pblk, line))
			continue;

		crc = pblk_calc_smeta_crc(pblk, smeta_buf);
		if (le32_to_cpu(smeta_buf->crc) != crc)
			continue;

		if (le32_to_cpu(smeta_buf->header.identifier) != PBLK_MAGIC)
			continue;

		if (smeta_buf->header.version_major != SMETA_VERSION_MAJOR) {
			pblk_err(pblk, "found incompatible line version %u\n",
					smeta_buf->header.version_major);
			return ERR_PTR(-EINVAL);
		}

		/* The first valid instance uuid is used for initialization */
		if (!valid_uuid) {
			memcpy(pblk->instance_uuid, smeta_buf->header.uuid, 16);
			valid_uuid = 1;
		}

		if (memcmp(pblk->instance_uuid, smeta_buf->header.uuid, 16)) {
			pblk_debug(pblk, "ignore line %u due to uuid mismatch\n",
					i);
			continue;
		}

		/* Update line metadata */
		spin_lock(&line->lock);
		line->id = le32_to_cpu(smeta_buf->header.id);
		line->type = le16_to_cpu(smeta_buf->header.type);
		line->seq_nr = le64_to_cpu(smeta_buf->seq_nr);
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
		pblk_debug(pblk, "recovering data line %d, seq:%llu\n",
						line->id, smeta_buf->seq_nr);
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
		recovered_lines++;

		line->emeta_ssec = pblk_line_emeta_start(pblk, line);
		line->emeta = emeta;
		memset(line->emeta->buf, 0, lm->emeta_len[0]);

		if (pblk_line_is_open(pblk, line)) {
			pblk_recov_l2p_from_oob(pblk, line);
			goto next;
		}

		if (pblk_line_emeta_read(pblk, line, line->emeta->buf)) {
			pblk_recov_l2p_from_oob(pblk, line);
			goto next;
		}

		if (pblk_recov_check_emeta(pblk, line->emeta->buf)) {
			pblk_recov_l2p_from_oob(pblk, line);
			goto next;
		}

		if (pblk_recov_check_line_version(pblk, line->emeta->buf))
			return ERR_PTR(-EINVAL);

		pblk_recov_wa_counters(pblk, line->emeta->buf);

		if (pblk_recov_l2p_from_emeta(pblk, line))
			pblk_recov_l2p_from_oob(pblk, line);

next:
		if (pblk_line_is_full(line)) {
			struct list_head *move_list;

			spin_lock(&line->lock);
			line->state = PBLK_LINESTATE_CLOSED;
			trace_pblk_line_state(pblk_disk_name(pblk), line->id,
					line->state);
			move_list = pblk_line_gc_list(pblk, line);
			spin_unlock(&line->lock);

			spin_lock(&l_mg->gc_lock);
			list_move_tail(&line->list, move_list);
			spin_unlock(&l_mg->gc_lock);

			mempool_free(line->map_bitmap, l_mg->bitmap_pool);
			line->map_bitmap = NULL;
			line->smeta = NULL;
			line->emeta = NULL;
		} else {
			spin_lock(&line->lock);
			line->state = PBLK_LINESTATE_OPEN;
			spin_unlock(&line->lock);

			line->emeta->mem = 0;
			atomic_set(&line->emeta->sync, 0);

			trace_pblk_line_state(pblk_disk_name(pblk), line->id,
					line->state);

			data_line = line;
			line->meta_line = meta_line;

			open_lines++;
		}
	}

	if (!open_lines) {
		spin_lock(&l_mg->free_lock);
		WARN_ON_ONCE(!test_and_clear_bit(meta_line,
							&l_mg->meta_bitmap));
		spin_unlock(&l_mg->free_lock);
	} else {
		spin_lock(&l_mg->free_lock);
		/* Allocate next line for preparation */
		l_mg->data_next = pblk_line_get(pblk);
		if (l_mg->data_next) {
			l_mg->data_next->seq_nr = l_mg->d_seq_nr++;
			l_mg->data_next->type = PBLK_LINETYPE_DATA;
			is_next = 1;
		}
		spin_unlock(&l_mg->free_lock);
	}

	if (is_next)
		pblk_line_erase(pblk, l_mg->data_next);

out:
	if (found_lines != recovered_lines)
		pblk_err(pblk, "failed to recover all found lines %d/%d\n",
						found_lines, recovered_lines);

	return data_line;
}

/*
 * Pad current line
 */
int pblk_recov_pad(struct pblk *pblk)
{
	struct pblk_line *line;
	struct pblk_line_mgmt *l_mg = &pblk->l_mg;
	int left_msecs;
	int ret = 0;

	spin_lock(&l_mg->free_lock);
	line = l_mg->data_line;
	left_msecs = line->left_msecs;
	spin_unlock(&l_mg->free_lock);

	ret = pblk_recov_pad_line(pblk, line, left_msecs);
	if (ret) {
		pblk_err(pblk, "tear down padding failed (%d)\n", ret);
		return ret;
	}

	pblk_line_close_meta(pblk, line);
	return ret;
}
