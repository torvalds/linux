/*
 * Copyright (C) 2015 IT University of Copenhagen (rrpc.c)
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
 * Implementation of a physical block-device target for Open-channel SSDs.
 *
 * pblk-init.c - pblk's initialization.
 */

#include "pblk.h"

static struct kmem_cache *pblk_ws_cache, *pblk_rec_cache, *pblk_g_rq_cache,
				*pblk_w_rq_cache;
static DECLARE_RWSEM(pblk_lock);
struct bio_set *pblk_bio_set;

static int pblk_rw_io(struct request_queue *q, struct pblk *pblk,
			  struct bio *bio)
{
	int ret;

	/* Read requests must be <= 256kb due to NVMe's 64 bit completion bitmap
	 * constraint. Writes can be of arbitrary size.
	 */
	if (bio_data_dir(bio) == READ) {
		blk_queue_split(q, &bio);
		ret = pblk_submit_read(pblk, bio);
		if (ret == NVM_IO_DONE && bio_flagged(bio, BIO_CLONED))
			bio_put(bio);

		return ret;
	}

	/* Prevent deadlock in the case of a modest LUN configuration and large
	 * user I/Os. Unless stalled, the rate limiter leaves at least 256KB
	 * available for user I/O.
	 */
	if (pblk_get_secs(bio) > pblk_rl_max_io(&pblk->rl))
		blk_queue_split(q, &bio);

	return pblk_write_to_cache(pblk, bio, PBLK_IOTYPE_USER);
}

static blk_qc_t pblk_make_rq(struct request_queue *q, struct bio *bio)
{
	struct pblk *pblk = q->queuedata;

	if (bio_op(bio) == REQ_OP_DISCARD) {
		pblk_discard(pblk, bio);
		if (!(bio->bi_opf & REQ_PREFLUSH)) {
			bio_endio(bio);
			return BLK_QC_T_NONE;
		}
	}

	switch (pblk_rw_io(q, pblk, bio)) {
	case NVM_IO_ERR:
		bio_io_error(bio);
		break;
	case NVM_IO_DONE:
		bio_endio(bio);
		break;
	}

	return BLK_QC_T_NONE;
}

static size_t pblk_trans_map_size(struct pblk *pblk)
{
	int entry_size = 8;

	if (pblk->ppaf_bitsize < 32)
		entry_size = 4;

	return entry_size * pblk->rl.nr_secs;
}

#ifdef CONFIG_NVM_DEBUG
static u32 pblk_l2p_crc(struct pblk *pblk)
{
	size_t map_size;
	u32 crc = ~(u32)0;

	map_size = pblk_trans_map_size(pblk);
	crc = crc32_le(crc, pblk->trans_map, map_size);
	return crc;
}
#endif

static void pblk_l2p_free(struct pblk *pblk)
{
	vfree(pblk->trans_map);
}

static int pblk_l2p_recover(struct pblk *pblk, bool factory_init)
{
	struct pblk_line *line = NULL;

	if (factory_init) {
		pblk_setup_uuid(pblk);
	} else {
		line = pblk_recov_l2p(pblk);
		if (IS_ERR(line)) {
			pr_err("pblk: could not recover l2p table\n");
			return -EFAULT;
		}
	}

#ifdef CONFIG_NVM_DEBUG
	pr_info("pblk init: L2P CRC: %x\n", pblk_l2p_crc(pblk));
#endif

	/* Free full lines directly as GC has not been started yet */
	pblk_gc_free_full_lines(pblk);

	if (!line) {
		/* Configure next line for user data */
		line = pblk_line_get_first_data(pblk);
		if (!line) {
			pr_err("pblk: line list corrupted\n");
			return -EFAULT;
		}
	}

	return 0;
}

static int pblk_l2p_init(struct pblk *pblk, bool factory_init)
{
	sector_t i;
	struct ppa_addr ppa;
	size_t map_size;

	map_size = pblk_trans_map_size(pblk);
	pblk->trans_map = vmalloc(map_size);
	if (!pblk->trans_map)
		return -ENOMEM;

	pblk_ppa_set_empty(&ppa);

	for (i = 0; i < pblk->rl.nr_secs; i++)
		pblk_trans_map_set(pblk, i, ppa);

	return pblk_l2p_recover(pblk, factory_init);
}

static void pblk_rwb_free(struct pblk *pblk)
{
	if (pblk_rb_tear_down_check(&pblk->rwb))
		pr_err("pblk: write buffer error on tear down\n");

	pblk_rb_data_free(&pblk->rwb);
	vfree(pblk_rb_entries_ref(&pblk->rwb));
}

static int pblk_rwb_init(struct pblk *pblk)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	struct pblk_rb_entry *entries;
	unsigned long nr_entries;
	unsigned int power_size, power_seg_sz;

	nr_entries = pblk_rb_calculate_size(pblk->pgs_in_buffer);

	entries = vzalloc(nr_entries * sizeof(struct pblk_rb_entry));
	if (!entries)
		return -ENOMEM;

	power_size = get_count_order(nr_entries);
	power_seg_sz = get_count_order(geo->sec_size);

	return pblk_rb_init(&pblk->rwb, entries, power_size, power_seg_sz);
}

/* Minimum pages needed within a lun */
#define ADDR_POOL_SIZE 64

static int pblk_set_ppaf(struct pblk *pblk)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	struct nvm_addr_format ppaf = geo->ppaf;
	int mod, power_len;

	div_u64_rem(geo->sec_per_chk, pblk->min_write_pgs, &mod);
	if (mod) {
		pr_err("pblk: bad configuration of sectors/pages\n");
		return -EINVAL;
	}

	/* Re-calculate channel and lun format to adapt to configuration */
	power_len = get_count_order(geo->nr_chnls);
	if (1 << power_len != geo->nr_chnls) {
		pr_err("pblk: supports only power-of-two channel config.\n");
		return -EINVAL;
	}
	ppaf.ch_len = power_len;

	power_len = get_count_order(geo->nr_luns);
	if (1 << power_len != geo->nr_luns) {
		pr_err("pblk: supports only power-of-two LUN config.\n");
		return -EINVAL;
	}
	ppaf.lun_len = power_len;

	pblk->ppaf.sec_offset = 0;
	pblk->ppaf.pln_offset = ppaf.sect_len;
	pblk->ppaf.ch_offset = pblk->ppaf.pln_offset + ppaf.pln_len;
	pblk->ppaf.lun_offset = pblk->ppaf.ch_offset + ppaf.ch_len;
	pblk->ppaf.pg_offset = pblk->ppaf.lun_offset + ppaf.lun_len;
	pblk->ppaf.blk_offset = pblk->ppaf.pg_offset + ppaf.pg_len;
	pblk->ppaf.sec_mask = (1ULL << ppaf.sect_len) - 1;
	pblk->ppaf.pln_mask = ((1ULL << ppaf.pln_len) - 1) <<
							pblk->ppaf.pln_offset;
	pblk->ppaf.ch_mask = ((1ULL << ppaf.ch_len) - 1) <<
							pblk->ppaf.ch_offset;
	pblk->ppaf.lun_mask = ((1ULL << ppaf.lun_len) - 1) <<
							pblk->ppaf.lun_offset;
	pblk->ppaf.pg_mask = ((1ULL << ppaf.pg_len) - 1) <<
							pblk->ppaf.pg_offset;
	pblk->ppaf.blk_mask = ((1ULL << ppaf.blk_len) - 1) <<
							pblk->ppaf.blk_offset;

	pblk->ppaf_bitsize = pblk->ppaf.blk_offset + ppaf.blk_len;

	return 0;
}

static int pblk_init_global_caches(struct pblk *pblk)
{
	down_write(&pblk_lock);
	pblk_ws_cache = kmem_cache_create("pblk_blk_ws",
				sizeof(struct pblk_line_ws), 0, 0, NULL);
	if (!pblk_ws_cache) {
		up_write(&pblk_lock);
		return -ENOMEM;
	}

	pblk_rec_cache = kmem_cache_create("pblk_rec",
				sizeof(struct pblk_rec_ctx), 0, 0, NULL);
	if (!pblk_rec_cache) {
		kmem_cache_destroy(pblk_ws_cache);
		up_write(&pblk_lock);
		return -ENOMEM;
	}

	pblk_g_rq_cache = kmem_cache_create("pblk_g_rq", pblk_g_rq_size,
				0, 0, NULL);
	if (!pblk_g_rq_cache) {
		kmem_cache_destroy(pblk_ws_cache);
		kmem_cache_destroy(pblk_rec_cache);
		up_write(&pblk_lock);
		return -ENOMEM;
	}

	pblk_w_rq_cache = kmem_cache_create("pblk_w_rq", pblk_w_rq_size,
				0, 0, NULL);
	if (!pblk_w_rq_cache) {
		kmem_cache_destroy(pblk_ws_cache);
		kmem_cache_destroy(pblk_rec_cache);
		kmem_cache_destroy(pblk_g_rq_cache);
		up_write(&pblk_lock);
		return -ENOMEM;
	}
	up_write(&pblk_lock);

	return 0;
}

static void pblk_free_global_caches(struct pblk *pblk)
{
	kmem_cache_destroy(pblk_ws_cache);
	kmem_cache_destroy(pblk_rec_cache);
	kmem_cache_destroy(pblk_g_rq_cache);
	kmem_cache_destroy(pblk_w_rq_cache);
}

static int pblk_core_init(struct pblk *pblk)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	int max_write_ppas;

	atomic64_set(&pblk->user_wa, 0);
	atomic64_set(&pblk->pad_wa, 0);
	atomic64_set(&pblk->gc_wa, 0);
	pblk->user_rst_wa = 0;
	pblk->pad_rst_wa = 0;
	pblk->gc_rst_wa = 0;

	atomic64_set(&pblk->nr_flush, 0);
	pblk->nr_flush_rst = 0;

	pblk->pgs_in_buffer = NVM_MEM_PAGE_WRITE * geo->sec_per_pg *
						geo->nr_planes * geo->all_luns;

	pblk->min_write_pgs = geo->sec_per_pl * (geo->sec_size / PAGE_SIZE);
	max_write_ppas = pblk->min_write_pgs * geo->all_luns;
	pblk->max_write_pgs = min_t(int, max_write_ppas, NVM_MAX_VLBA);
	pblk_set_sec_per_write(pblk, pblk->min_write_pgs);

	if (pblk->max_write_pgs > PBLK_MAX_REQ_ADDRS) {
		pr_err("pblk: vector list too big(%u > %u)\n",
				pblk->max_write_pgs, PBLK_MAX_REQ_ADDRS);
		return -EINVAL;
	}

	pblk->pad_dist = kzalloc((pblk->min_write_pgs - 1) * sizeof(atomic64_t),
								GFP_KERNEL);
	if (!pblk->pad_dist)
		return -ENOMEM;

	if (pblk_init_global_caches(pblk))
		goto fail_free_pad_dist;

	/* Internal bios can be at most the sectors signaled by the device. */
	pblk->page_bio_pool = mempool_create_page_pool(NVM_MAX_VLBA, 0);
	if (!pblk->page_bio_pool)
		goto free_global_caches;

	pblk->gen_ws_pool = mempool_create_slab_pool(PBLK_GEN_WS_POOL_SIZE,
							pblk_ws_cache);
	if (!pblk->gen_ws_pool)
		goto free_page_bio_pool;

	pblk->rec_pool = mempool_create_slab_pool(geo->all_luns,
							pblk_rec_cache);
	if (!pblk->rec_pool)
		goto free_gen_ws_pool;

	pblk->r_rq_pool = mempool_create_slab_pool(geo->all_luns,
							pblk_g_rq_cache);
	if (!pblk->r_rq_pool)
		goto free_rec_pool;

	pblk->e_rq_pool = mempool_create_slab_pool(geo->all_luns,
							pblk_g_rq_cache);
	if (!pblk->e_rq_pool)
		goto free_r_rq_pool;

	pblk->w_rq_pool = mempool_create_slab_pool(geo->all_luns,
							pblk_w_rq_cache);
	if (!pblk->w_rq_pool)
		goto free_e_rq_pool;

	pblk->close_wq = alloc_workqueue("pblk-close-wq",
			WQ_MEM_RECLAIM | WQ_UNBOUND, PBLK_NR_CLOSE_JOBS);
	if (!pblk->close_wq)
		goto free_w_rq_pool;

	pblk->bb_wq = alloc_workqueue("pblk-bb-wq",
			WQ_MEM_RECLAIM | WQ_UNBOUND, 0);
	if (!pblk->bb_wq)
		goto free_close_wq;

	pblk->r_end_wq = alloc_workqueue("pblk-read-end-wq",
			WQ_MEM_RECLAIM | WQ_UNBOUND, 0);
	if (!pblk->r_end_wq)
		goto free_bb_wq;

	if (pblk_set_ppaf(pblk))
		goto free_r_end_wq;

	INIT_LIST_HEAD(&pblk->compl_list);

	return 0;

free_r_end_wq:
	destroy_workqueue(pblk->r_end_wq);
free_bb_wq:
	destroy_workqueue(pblk->bb_wq);
free_close_wq:
	destroy_workqueue(pblk->close_wq);
free_w_rq_pool:
	mempool_destroy(pblk->w_rq_pool);
free_e_rq_pool:
	mempool_destroy(pblk->e_rq_pool);
free_r_rq_pool:
	mempool_destroy(pblk->r_rq_pool);
free_rec_pool:
	mempool_destroy(pblk->rec_pool);
free_gen_ws_pool:
	mempool_destroy(pblk->gen_ws_pool);
free_page_bio_pool:
	mempool_destroy(pblk->page_bio_pool);
free_global_caches:
	pblk_free_global_caches(pblk);
fail_free_pad_dist:
	kfree(pblk->pad_dist);
	return -ENOMEM;
}

static void pblk_core_free(struct pblk *pblk)
{
	if (pblk->close_wq)
		destroy_workqueue(pblk->close_wq);

	if (pblk->r_end_wq)
		destroy_workqueue(pblk->r_end_wq);

	if (pblk->bb_wq)
		destroy_workqueue(pblk->bb_wq);

	mempool_destroy(pblk->page_bio_pool);
	mempool_destroy(pblk->gen_ws_pool);
	mempool_destroy(pblk->rec_pool);
	mempool_destroy(pblk->r_rq_pool);
	mempool_destroy(pblk->e_rq_pool);
	mempool_destroy(pblk->w_rq_pool);

	pblk_free_global_caches(pblk);
	kfree(pblk->pad_dist);
}

static void pblk_line_mg_free(struct pblk *pblk)
{
	struct pblk_line_mgmt *l_mg = &pblk->l_mg;
	int i;

	kfree(l_mg->bb_template);
	kfree(l_mg->bb_aux);
	kfree(l_mg->vsc_list);

	for (i = 0; i < PBLK_DATA_LINES; i++) {
		kfree(l_mg->sline_meta[i]);
		pblk_mfree(l_mg->eline_meta[i]->buf, l_mg->emeta_alloc_type);
		kfree(l_mg->eline_meta[i]);
	}
}

static void pblk_line_meta_free(struct pblk_line *line)
{
	kfree(line->blk_bitmap);
	kfree(line->erase_bitmap);
}

static void pblk_lines_free(struct pblk *pblk)
{
	struct pblk_line_mgmt *l_mg = &pblk->l_mg;
	struct pblk_line *line;
	int i;

	spin_lock(&l_mg->free_lock);
	for (i = 0; i < l_mg->nr_lines; i++) {
		line = &pblk->lines[i];

		pblk_line_free(pblk, line);
		pblk_line_meta_free(line);
	}
	spin_unlock(&l_mg->free_lock);

	pblk_line_mg_free(pblk);

	kfree(pblk->luns);
	kfree(pblk->lines);
}

static int pblk_bb_get_tbl(struct nvm_tgt_dev *dev, struct pblk_lun *rlun,
			   u8 *blks, int nr_blks)
{
	struct ppa_addr ppa;
	int ret;

	ppa.ppa = 0;
	ppa.g.ch = rlun->bppa.g.ch;
	ppa.g.lun = rlun->bppa.g.lun;

	ret = nvm_get_tgt_bb_tbl(dev, ppa, blks);
	if (ret)
		return ret;

	nr_blks = nvm_bb_tbl_fold(dev->parent, blks, nr_blks);
	if (nr_blks < 0)
		return -EIO;

	return 0;
}

static void *pblk_bb_get_log(struct pblk *pblk)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	u8 *log;
	int i, nr_blks, blk_per_lun;
	int ret;

	blk_per_lun = geo->nr_chks * geo->plane_mode;
	nr_blks = blk_per_lun * geo->all_luns;

	log = kmalloc(nr_blks, GFP_KERNEL);
	if (!log)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < geo->all_luns; i++) {
		struct pblk_lun *rlun = &pblk->luns[i];
		u8 *log_pos = log + i * blk_per_lun;

		ret = pblk_bb_get_tbl(dev, rlun, log_pos, blk_per_lun);
		if (ret) {
			kfree(log);
			return ERR_PTR(-EIO);
		}
	}

	return log;
}

static int pblk_bb_line(struct pblk *pblk, struct pblk_line *line,
			u8 *bb_log, int blk_per_line)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	int i, bb_cnt = 0;
	int blk_per_lun = geo->nr_chks * geo->plane_mode;

	for (i = 0; i < blk_per_line; i++) {
		struct pblk_lun *rlun = &pblk->luns[i];
		u8 *lun_bb_log = bb_log + i * blk_per_lun;

		if (lun_bb_log[line->id] == NVM_BLK_T_FREE)
			continue;

		set_bit(pblk_ppa_to_pos(geo, rlun->bppa), line->blk_bitmap);
		bb_cnt++;
	}

	return bb_cnt;
}

static int pblk_luns_init(struct pblk *pblk)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	struct pblk_lun *rlun;
	int i;

	/* TODO: Implement unbalanced LUN support */
	if (geo->nr_luns < 0) {
		pr_err("pblk: unbalanced LUN config.\n");
		return -EINVAL;
	}

	pblk->luns = kcalloc(geo->all_luns, sizeof(struct pblk_lun),
								GFP_KERNEL);
	if (!pblk->luns)
		return -ENOMEM;

	for (i = 0; i < geo->all_luns; i++) {
		/* Stripe across channels */
		int ch = i % geo->nr_chnls;
		int lun_raw = i / geo->nr_chnls;
		int lunid = lun_raw + ch * geo->nr_luns;

		rlun = &pblk->luns[i];
		rlun->bppa = dev->luns[lunid];

		sema_init(&rlun->wr_sem, 1);
	}

	return 0;
}

/* See comment over struct line_emeta definition */
static unsigned int calc_emeta_len(struct pblk *pblk)
{
	struct pblk_line_meta *lm = &pblk->lm;
	struct pblk_line_mgmt *l_mg = &pblk->l_mg;
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;

	/* Round to sector size so that lba_list starts on its own sector */
	lm->emeta_sec[1] = DIV_ROUND_UP(
			sizeof(struct line_emeta) + lm->blk_bitmap_len +
			sizeof(struct wa_counters), geo->sec_size);
	lm->emeta_len[1] = lm->emeta_sec[1] * geo->sec_size;

	/* Round to sector size so that vsc_list starts on its own sector */
	lm->dsec_per_line = lm->sec_per_line - lm->emeta_sec[0];
	lm->emeta_sec[2] = DIV_ROUND_UP(lm->dsec_per_line * sizeof(u64),
			geo->sec_size);
	lm->emeta_len[2] = lm->emeta_sec[2] * geo->sec_size;

	lm->emeta_sec[3] = DIV_ROUND_UP(l_mg->nr_lines * sizeof(u32),
			geo->sec_size);
	lm->emeta_len[3] = lm->emeta_sec[3] * geo->sec_size;

	lm->vsc_list_len = l_mg->nr_lines * sizeof(u32);

	return (lm->emeta_len[1] + lm->emeta_len[2] + lm->emeta_len[3]);
}

static void pblk_set_provision(struct pblk *pblk, long nr_free_blks)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct pblk_line_mgmt *l_mg = &pblk->l_mg;
	struct pblk_line_meta *lm = &pblk->lm;
	struct nvm_geo *geo = &dev->geo;
	sector_t provisioned;
	int sec_meta, blk_meta;

	if (geo->op == NVM_TARGET_DEFAULT_OP)
		pblk->op = PBLK_DEFAULT_OP;
	else
		pblk->op = geo->op;

	provisioned = nr_free_blks;
	provisioned *= (100 - pblk->op);
	sector_div(provisioned, 100);

	pblk->op_blks = nr_free_blks - provisioned;

	/* Internally pblk manages all free blocks, but all calculations based
	 * on user capacity consider only provisioned blocks
	 */
	pblk->rl.total_blocks = nr_free_blks;
	pblk->rl.nr_secs = nr_free_blks * geo->sec_per_chk;

	/* Consider sectors used for metadata */
	sec_meta = (lm->smeta_sec + lm->emeta_sec[0]) * l_mg->nr_free_lines;
	blk_meta = DIV_ROUND_UP(sec_meta, geo->sec_per_chk);

	pblk->capacity = (provisioned - blk_meta) * geo->sec_per_chk;

	atomic_set(&pblk->rl.free_blocks, nr_free_blks);
	atomic_set(&pblk->rl.free_user_blocks, nr_free_blks);
}

static int pblk_setup_line_meta(struct pblk *pblk, struct pblk_line *line,
				void *chunk_log, long *nr_bad_blks)
{
	struct pblk_line_meta *lm = &pblk->lm;

	line->blk_bitmap = kzalloc(lm->blk_bitmap_len, GFP_KERNEL);
	if (!line->blk_bitmap)
		return -ENOMEM;

	line->erase_bitmap = kzalloc(lm->blk_bitmap_len, GFP_KERNEL);
	if (!line->erase_bitmap) {
		kfree(line->blk_bitmap);
		return -ENOMEM;
	}

	*nr_bad_blks = pblk_bb_line(pblk, line, chunk_log, lm->blk_per_line);

	return 0;
}

static int pblk_line_mg_init(struct pblk *pblk)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	struct pblk_line_mgmt *l_mg = &pblk->l_mg;
	struct pblk_line_meta *lm = &pblk->lm;
	int i, bb_distance;

	l_mg->nr_lines = geo->nr_chks;
	l_mg->log_line = l_mg->data_line = NULL;
	l_mg->l_seq_nr = l_mg->d_seq_nr = 0;
	l_mg->nr_free_lines = 0;
	bitmap_zero(&l_mg->meta_bitmap, PBLK_DATA_LINES);

	INIT_LIST_HEAD(&l_mg->free_list);
	INIT_LIST_HEAD(&l_mg->corrupt_list);
	INIT_LIST_HEAD(&l_mg->bad_list);
	INIT_LIST_HEAD(&l_mg->gc_full_list);
	INIT_LIST_HEAD(&l_mg->gc_high_list);
	INIT_LIST_HEAD(&l_mg->gc_mid_list);
	INIT_LIST_HEAD(&l_mg->gc_low_list);
	INIT_LIST_HEAD(&l_mg->gc_empty_list);

	INIT_LIST_HEAD(&l_mg->emeta_list);

	l_mg->gc_lists[0] = &l_mg->gc_high_list;
	l_mg->gc_lists[1] = &l_mg->gc_mid_list;
	l_mg->gc_lists[2] = &l_mg->gc_low_list;

	spin_lock_init(&l_mg->free_lock);
	spin_lock_init(&l_mg->close_lock);
	spin_lock_init(&l_mg->gc_lock);

	l_mg->vsc_list = kcalloc(l_mg->nr_lines, sizeof(__le32), GFP_KERNEL);
	if (!l_mg->vsc_list)
		goto fail;

	l_mg->bb_template = kzalloc(lm->sec_bitmap_len, GFP_KERNEL);
	if (!l_mg->bb_template)
		goto fail_free_vsc_list;

	l_mg->bb_aux = kzalloc(lm->sec_bitmap_len, GFP_KERNEL);
	if (!l_mg->bb_aux)
		goto fail_free_bb_template;

	/* smeta is always small enough to fit on a kmalloc memory allocation,
	 * emeta depends on the number of LUNs allocated to the pblk instance
	 */
	for (i = 0; i < PBLK_DATA_LINES; i++) {
		l_mg->sline_meta[i] = kmalloc(lm->smeta_len, GFP_KERNEL);
		if (!l_mg->sline_meta[i])
			goto fail_free_smeta;
	}

	/* emeta allocates three different buffers for managing metadata with
	 * in-memory and in-media layouts
	 */
	for (i = 0; i < PBLK_DATA_LINES; i++) {
		struct pblk_emeta *emeta;

		emeta = kmalloc(sizeof(struct pblk_emeta), GFP_KERNEL);
		if (!emeta)
			goto fail_free_emeta;

		if (lm->emeta_len[0] > KMALLOC_MAX_CACHE_SIZE) {
			l_mg->emeta_alloc_type = PBLK_VMALLOC_META;

			emeta->buf = vmalloc(lm->emeta_len[0]);
			if (!emeta->buf) {
				kfree(emeta);
				goto fail_free_emeta;
			}

			emeta->nr_entries = lm->emeta_sec[0];
			l_mg->eline_meta[i] = emeta;
		} else {
			l_mg->emeta_alloc_type = PBLK_KMALLOC_META;

			emeta->buf = kmalloc(lm->emeta_len[0], GFP_KERNEL);
			if (!emeta->buf) {
				kfree(emeta);
				goto fail_free_emeta;
			}

			emeta->nr_entries = lm->emeta_sec[0];
			l_mg->eline_meta[i] = emeta;
		}
	}

	for (i = 0; i < l_mg->nr_lines; i++)
		l_mg->vsc_list[i] = cpu_to_le32(EMPTY_ENTRY);

	bb_distance = (geo->all_luns) * geo->ws_opt;
	for (i = 0; i < lm->sec_per_line; i += bb_distance)
		bitmap_set(l_mg->bb_template, i, geo->ws_opt);

	return 0;

fail_free_emeta:
	while (--i >= 0) {
		if (l_mg->emeta_alloc_type == PBLK_VMALLOC_META)
			vfree(l_mg->eline_meta[i]->buf);
		else
			kfree(l_mg->eline_meta[i]->buf);
		kfree(l_mg->eline_meta[i]);
	}
fail_free_smeta:
	for (i = 0; i < PBLK_DATA_LINES; i++)
		kfree(l_mg->sline_meta[i]);
	kfree(l_mg->bb_aux);
fail_free_bb_template:
	kfree(l_mg->bb_template);
fail_free_vsc_list:
	kfree(l_mg->vsc_list);
fail:
	return -ENOMEM;
}

static int pblk_line_meta_init(struct pblk *pblk)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	struct pblk_line_meta *lm = &pblk->lm;
	unsigned int smeta_len, emeta_len;
	int i;

	lm->sec_per_line = geo->sec_per_chk * geo->all_luns;
	lm->blk_per_line = geo->all_luns;
	lm->blk_bitmap_len = BITS_TO_LONGS(geo->all_luns) * sizeof(long);
	lm->sec_bitmap_len = BITS_TO_LONGS(lm->sec_per_line) * sizeof(long);
	lm->lun_bitmap_len = BITS_TO_LONGS(geo->all_luns) * sizeof(long);
	lm->mid_thrs = lm->sec_per_line / 2;
	lm->high_thrs = lm->sec_per_line / 4;
	lm->meta_distance = (geo->all_luns / 2) * pblk->min_write_pgs;

	/* Calculate necessary pages for smeta. See comment over struct
	 * line_smeta definition
	 */
	i = 1;
add_smeta_page:
	lm->smeta_sec = i * geo->sec_per_pl;
	lm->smeta_len = lm->smeta_sec * geo->sec_size;

	smeta_len = sizeof(struct line_smeta) + lm->lun_bitmap_len;
	if (smeta_len > lm->smeta_len) {
		i++;
		goto add_smeta_page;
	}

	/* Calculate necessary pages for emeta. See comment over struct
	 * line_emeta definition
	 */
	i = 1;
add_emeta_page:
	lm->emeta_sec[0] = i * geo->sec_per_pl;
	lm->emeta_len[0] = lm->emeta_sec[0] * geo->sec_size;

	emeta_len = calc_emeta_len(pblk);
	if (emeta_len > lm->emeta_len[0]) {
		i++;
		goto add_emeta_page;
	}

	lm->emeta_bb = geo->all_luns > i ? geo->all_luns - i : 0;

	lm->min_blk_line = 1;
	if (geo->all_luns > 1)
		lm->min_blk_line += DIV_ROUND_UP(lm->smeta_sec +
					lm->emeta_sec[0], geo->sec_per_chk);

	if (lm->min_blk_line > lm->blk_per_line) {
		pr_err("pblk: config. not supported. Min. LUN in line:%d\n",
							lm->blk_per_line);
		return -EINVAL;
	}

	return 0;
}

static int pblk_lines_init(struct pblk *pblk)
{
	struct pblk_line_mgmt *l_mg = &pblk->l_mg;
	struct pblk_line_meta *lm = &pblk->lm;
	struct pblk_line *line;
	void *chunk_log;
	long nr_bad_blks = 0, nr_free_blks = 0;
	int i, ret;

	ret = pblk_line_meta_init(pblk);
	if (ret)
		return ret;

	ret = pblk_line_mg_init(pblk);
	if (ret)
		return ret;

	ret = pblk_luns_init(pblk);
	if (ret)
		goto fail_free_meta;

	chunk_log = pblk_bb_get_log(pblk);
	if (IS_ERR(chunk_log)) {
		pr_err("pblk: could not get bad block log (%lu)\n",
							PTR_ERR(chunk_log));
		ret = PTR_ERR(chunk_log);
		goto fail_free_luns;
	}

	pblk->lines = kcalloc(l_mg->nr_lines, sizeof(struct pblk_line),
								GFP_KERNEL);
	if (!pblk->lines) {
		ret = -ENOMEM;
		goto fail_free_chunk_log;
	}

	for (i = 0; i < l_mg->nr_lines; i++) {
		int chk_in_line;

		line = &pblk->lines[i];

		line->pblk = pblk;
		line->id = i;
		line->type = PBLK_LINETYPE_FREE;
		line->state = PBLK_LINESTATE_FREE;
		line->gc_group = PBLK_LINEGC_NONE;
		line->vsc = &l_mg->vsc_list[i];
		spin_lock_init(&line->lock);

		ret = pblk_setup_line_meta(pblk, line, chunk_log, &nr_bad_blks);
		if (ret)
			goto fail_free_lines;

		chk_in_line = lm->blk_per_line - nr_bad_blks;
		if (nr_bad_blks < 0 || nr_bad_blks > lm->blk_per_line ||
					chk_in_line < lm->min_blk_line) {
			line->state = PBLK_LINESTATE_BAD;
			list_add_tail(&line->list, &l_mg->bad_list);
			continue;
		}

		nr_free_blks += chk_in_line;
		atomic_set(&line->blk_in_line, chk_in_line);

		l_mg->nr_free_lines++;
		list_add_tail(&line->list, &l_mg->free_list);
	}

	pblk_set_provision(pblk, nr_free_blks);

	kfree(chunk_log);
	return 0;

fail_free_lines:
	while (--i >= 0)
		pblk_line_meta_free(&pblk->lines[i]);
	kfree(pblk->lines);
fail_free_chunk_log:
	kfree(chunk_log);
fail_free_luns:
	kfree(pblk->luns);
fail_free_meta:
	pblk_line_mg_free(pblk);

	return ret;
}

static int pblk_writer_init(struct pblk *pblk)
{
	pblk->writer_ts = kthread_create(pblk_write_ts, pblk, "pblk-writer-t");
	if (IS_ERR(pblk->writer_ts)) {
		int err = PTR_ERR(pblk->writer_ts);

		if (err != -EINTR)
			pr_err("pblk: could not allocate writer kthread (%d)\n",
					err);
		return err;
	}

	timer_setup(&pblk->wtimer, pblk_write_timer_fn, 0);
	mod_timer(&pblk->wtimer, jiffies + msecs_to_jiffies(100));

	return 0;
}

static void pblk_writer_stop(struct pblk *pblk)
{
	/* The pipeline must be stopped and the write buffer emptied before the
	 * write thread is stopped
	 */
	WARN(pblk_rb_read_count(&pblk->rwb),
			"Stopping not fully persisted write buffer\n");

	WARN(pblk_rb_sync_count(&pblk->rwb),
			"Stopping not fully synced write buffer\n");

	del_timer_sync(&pblk->wtimer);
	if (pblk->writer_ts)
		kthread_stop(pblk->writer_ts);
}

static void pblk_free(struct pblk *pblk)
{
	pblk_lines_free(pblk);
	pblk_l2p_free(pblk);
	pblk_rwb_free(pblk);
	pblk_core_free(pblk);

	kfree(pblk);
}

static void pblk_tear_down(struct pblk *pblk)
{
	pblk_pipeline_stop(pblk);
	pblk_writer_stop(pblk);
	pblk_rb_sync_l2p(&pblk->rwb);
	pblk_rl_free(&pblk->rl);

	pr_debug("pblk: consistent tear down\n");
}

static void pblk_exit(void *private)
{
	struct pblk *pblk = private;

	down_write(&pblk_lock);
	pblk_gc_exit(pblk);
	pblk_tear_down(pblk);

#ifdef CONFIG_NVM_DEBUG
	pr_info("pblk exit: L2P CRC: %x\n", pblk_l2p_crc(pblk));
#endif

	pblk_free(pblk);
	up_write(&pblk_lock);
}

static sector_t pblk_capacity(void *private)
{
	struct pblk *pblk = private;

	return pblk->capacity * NR_PHY_IN_LOG;
}

static void *pblk_init(struct nvm_tgt_dev *dev, struct gendisk *tdisk,
		       int flags)
{
	struct nvm_geo *geo = &dev->geo;
	struct request_queue *bqueue = dev->q;
	struct request_queue *tqueue = tdisk->queue;
	struct pblk *pblk;
	int ret;

	if (dev->identity.dom & NVM_RSP_L2P) {
		pr_err("pblk: host-side L2P table not supported. (%x)\n",
							dev->identity.dom);
		return ERR_PTR(-EINVAL);
	}

	pblk = kzalloc(sizeof(struct pblk), GFP_KERNEL);
	if (!pblk)
		return ERR_PTR(-ENOMEM);

	pblk->dev = dev;
	pblk->disk = tdisk;
	pblk->state = PBLK_STATE_RUNNING;
	pblk->gc.gc_enabled = 0;

	spin_lock_init(&pblk->trans_lock);
	spin_lock_init(&pblk->lock);

#ifdef CONFIG_NVM_DEBUG
	atomic_long_set(&pblk->inflight_writes, 0);
	atomic_long_set(&pblk->padded_writes, 0);
	atomic_long_set(&pblk->padded_wb, 0);
	atomic_long_set(&pblk->req_writes, 0);
	atomic_long_set(&pblk->sub_writes, 0);
	atomic_long_set(&pblk->sync_writes, 0);
	atomic_long_set(&pblk->inflight_reads, 0);
	atomic_long_set(&pblk->cache_reads, 0);
	atomic_long_set(&pblk->sync_reads, 0);
	atomic_long_set(&pblk->recov_writes, 0);
	atomic_long_set(&pblk->recov_writes, 0);
	atomic_long_set(&pblk->recov_gc_writes, 0);
	atomic_long_set(&pblk->recov_gc_reads, 0);
#endif

	atomic_long_set(&pblk->read_failed, 0);
	atomic_long_set(&pblk->read_empty, 0);
	atomic_long_set(&pblk->read_high_ecc, 0);
	atomic_long_set(&pblk->read_failed_gc, 0);
	atomic_long_set(&pblk->write_failed, 0);
	atomic_long_set(&pblk->erase_failed, 0);

	ret = pblk_core_init(pblk);
	if (ret) {
		pr_err("pblk: could not initialize core\n");
		goto fail;
	}

	ret = pblk_lines_init(pblk);
	if (ret) {
		pr_err("pblk: could not initialize lines\n");
		goto fail_free_core;
	}

	ret = pblk_rwb_init(pblk);
	if (ret) {
		pr_err("pblk: could not initialize write buffer\n");
		goto fail_free_lines;
	}

	ret = pblk_l2p_init(pblk, flags & NVM_TARGET_FACTORY);
	if (ret) {
		pr_err("pblk: could not initialize maps\n");
		goto fail_free_rwb;
	}

	ret = pblk_writer_init(pblk);
	if (ret) {
		if (ret != -EINTR)
			pr_err("pblk: could not initialize write thread\n");
		goto fail_free_l2p;
	}

	ret = pblk_gc_init(pblk);
	if (ret) {
		pr_err("pblk: could not initialize gc\n");
		goto fail_stop_writer;
	}

	/* inherit the size from the underlying device */
	blk_queue_logical_block_size(tqueue, queue_physical_block_size(bqueue));
	blk_queue_max_hw_sectors(tqueue, queue_max_hw_sectors(bqueue));

	blk_queue_write_cache(tqueue, true, false);

	tqueue->limits.discard_granularity = geo->sec_per_chk * geo->sec_size;
	tqueue->limits.discard_alignment = 0;
	blk_queue_max_discard_sectors(tqueue, UINT_MAX >> 9);
	blk_queue_flag_set(QUEUE_FLAG_DISCARD, tqueue);

	pr_info("pblk(%s): luns:%u, lines:%d, secs:%llu, buf entries:%u\n",
			tdisk->disk_name,
			geo->all_luns, pblk->l_mg.nr_lines,
			(unsigned long long)pblk->rl.nr_secs,
			pblk->rwb.nr_entries);

	wake_up_process(pblk->writer_ts);

	/* Check if we need to start GC */
	pblk_gc_should_kick(pblk);

	return pblk;

fail_stop_writer:
	pblk_writer_stop(pblk);
fail_free_l2p:
	pblk_l2p_free(pblk);
fail_free_rwb:
	pblk_rwb_free(pblk);
fail_free_lines:
	pblk_lines_free(pblk);
fail_free_core:
	pblk_core_free(pblk);
fail:
	kfree(pblk);
	return ERR_PTR(ret);
}

/* physical block device target */
static struct nvm_tgt_type tt_pblk = {
	.name		= "pblk",
	.version	= {1, 0, 0},

	.make_rq	= pblk_make_rq,
	.capacity	= pblk_capacity,

	.init		= pblk_init,
	.exit		= pblk_exit,

	.sysfs_init	= pblk_sysfs_init,
	.sysfs_exit	= pblk_sysfs_exit,
	.owner		= THIS_MODULE,
};

static int __init pblk_module_init(void)
{
	int ret;

	pblk_bio_set = bioset_create(BIO_POOL_SIZE, 0, 0);
	if (!pblk_bio_set)
		return -ENOMEM;
	ret = nvm_register_tgt_type(&tt_pblk);
	if (ret)
		bioset_free(pblk_bio_set);
	return ret;
}

static void pblk_module_exit(void)
{
	bioset_free(pblk_bio_set);
	nvm_unregister_tgt_type(&tt_pblk);
}

module_init(pblk_module_init);
module_exit(pblk_module_exit);
MODULE_AUTHOR("Javier Gonzalez <javier@cnexlabs.com>");
MODULE_AUTHOR("Matias Bjorling <matias@cnexlabs.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Physical Block-Device for Open-Channel SSDs");
