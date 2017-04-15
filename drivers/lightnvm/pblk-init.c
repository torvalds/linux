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

static struct kmem_cache *pblk_blk_ws_cache, *pblk_rec_cache, *pblk_r_rq_cache,
					*pblk_w_rq_cache, *pblk_line_meta_cache;
static DECLARE_RWSEM(pblk_lock);

static int pblk_rw_io(struct request_queue *q, struct pblk *pblk,
			  struct bio *bio)
{
	int ret;

	/* Read requests must be <= 256kb due to NVMe's 64 bit completion bitmap
	 * constraint. Writes can be of arbitrary size.
	 */
	if (bio_data_dir(bio) == READ) {
		blk_queue_split(q, &bio, q->bio_split);
		ret = pblk_submit_read(pblk, bio);
		if (ret == NVM_IO_DONE && bio_flagged(bio, BIO_CLONED))
			bio_put(bio);

		return ret;
	}

	/* Prevent deadlock in the case of a modest LUN configuration and large
	 * user I/Os. Unless stalled, the rate limiter leaves at least 256KB
	 * available for user I/O.
	 */
	if (unlikely(pblk_get_secs(bio) >= pblk_rl_sysfs_rate_show(&pblk->rl)))
		blk_queue_split(q, &bio, q->bio_split);

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

static void pblk_l2p_free(struct pblk *pblk)
{
	vfree(pblk->trans_map);
}

static int pblk_l2p_init(struct pblk *pblk)
{
	sector_t i;
	struct ppa_addr ppa;
	int entry_size = 8;

	if (pblk->ppaf_bitsize < 32)
		entry_size = 4;

	pblk->trans_map = vmalloc(entry_size * pblk->rl.nr_secs);
	if (!pblk->trans_map)
		return -ENOMEM;

	pblk_ppa_set_empty(&ppa);

	for (i = 0; i < pblk->rl.nr_secs; i++)
		pblk_trans_map_set(pblk, i, ppa);

	return 0;
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
#define PAGE_POOL_SIZE 16
#define ADDR_POOL_SIZE 64

static int pblk_set_ppaf(struct pblk *pblk)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	struct nvm_addr_format ppaf = geo->ppaf;
	int power_len;

	/* Re-calculate channel and lun format to adapt to configuration */
	power_len = get_count_order(geo->nr_chnls);
	if (1 << power_len != geo->nr_chnls) {
		pr_err("pblk: supports only power-of-two channel config.\n");
		return -EINVAL;
	}
	ppaf.ch_len = power_len;

	power_len = get_count_order(geo->luns_per_chnl);
	if (1 << power_len != geo->luns_per_chnl) {
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
	char cache_name[PBLK_CACHE_NAME_LEN];

	down_write(&pblk_lock);
	pblk_blk_ws_cache = kmem_cache_create("pblk_blk_ws",
				sizeof(struct pblk_line_ws), 0, 0, NULL);
	if (!pblk_blk_ws_cache) {
		up_write(&pblk_lock);
		return -ENOMEM;
	}

	pblk_rec_cache = kmem_cache_create("pblk_rec",
				sizeof(struct pblk_rec_ctx), 0, 0, NULL);
	if (!pblk_rec_cache) {
		kmem_cache_destroy(pblk_blk_ws_cache);
		up_write(&pblk_lock);
		return -ENOMEM;
	}

	pblk_r_rq_cache = kmem_cache_create("pblk_r_rq", pblk_r_rq_size,
				0, 0, NULL);
	if (!pblk_r_rq_cache) {
		kmem_cache_destroy(pblk_blk_ws_cache);
		kmem_cache_destroy(pblk_rec_cache);
		up_write(&pblk_lock);
		return -ENOMEM;
	}

	pblk_w_rq_cache = kmem_cache_create("pblk_w_rq", pblk_w_rq_size,
				0, 0, NULL);
	if (!pblk_w_rq_cache) {
		kmem_cache_destroy(pblk_blk_ws_cache);
		kmem_cache_destroy(pblk_rec_cache);
		kmem_cache_destroy(pblk_r_rq_cache);
		up_write(&pblk_lock);
		return -ENOMEM;
	}

	snprintf(cache_name, sizeof(cache_name), "pblk_line_m_%s",
							pblk->disk->disk_name);
	pblk_line_meta_cache = kmem_cache_create(cache_name,
				pblk->lm.sec_bitmap_len, 0, 0, NULL);
	if (!pblk_line_meta_cache) {
		kmem_cache_destroy(pblk_blk_ws_cache);
		kmem_cache_destroy(pblk_rec_cache);
		kmem_cache_destroy(pblk_r_rq_cache);
		kmem_cache_destroy(pblk_w_rq_cache);
		up_write(&pblk_lock);
		return -ENOMEM;
	}
	up_write(&pblk_lock);

	return 0;
}

static int pblk_core_init(struct pblk *pblk)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	int max_write_ppas;
	int mod;

	pblk->min_write_pgs = geo->sec_per_pl * (geo->sec_size / PAGE_SIZE);
	max_write_ppas = pblk->min_write_pgs * geo->nr_luns;
	pblk->max_write_pgs = (max_write_ppas < nvm_max_phys_sects(dev)) ?
				max_write_ppas : nvm_max_phys_sects(dev);
	pblk->pgs_in_buffer = NVM_MEM_PAGE_WRITE * geo->sec_per_pg *
						geo->nr_planes * geo->nr_luns;

	if (pblk->max_write_pgs > PBLK_MAX_REQ_ADDRS) {
		pr_err("pblk: cannot support device max_phys_sect\n");
		return -EINVAL;
	}

	div_u64_rem(geo->sec_per_blk, pblk->min_write_pgs, &mod);
	if (mod) {
		pr_err("pblk: bad configuration of sectors/pages\n");
		return -EINVAL;
	}

	if (pblk_init_global_caches(pblk))
		return -ENOMEM;

	pblk->page_pool = mempool_create_page_pool(PAGE_POOL_SIZE, 0);
	if (!pblk->page_pool)
		return -ENOMEM;

	pblk->line_ws_pool = mempool_create_slab_pool(geo->nr_luns,
							pblk_blk_ws_cache);
	if (!pblk->line_ws_pool)
		goto free_page_pool;

	pblk->rec_pool = mempool_create_slab_pool(geo->nr_luns, pblk_rec_cache);
	if (!pblk->rec_pool)
		goto free_blk_ws_pool;

	pblk->r_rq_pool = mempool_create_slab_pool(64, pblk_r_rq_cache);
	if (!pblk->r_rq_pool)
		goto free_rec_pool;

	pblk->w_rq_pool = mempool_create_slab_pool(64, pblk_w_rq_cache);
	if (!pblk->w_rq_pool)
		goto free_r_rq_pool;

	pblk->line_meta_pool =
			mempool_create_slab_pool(16, pblk_line_meta_cache);
	if (!pblk->line_meta_pool)
		goto free_w_rq_pool;

	pblk->kw_wq = alloc_workqueue("pblk-aux-wq",
					WQ_MEM_RECLAIM | WQ_UNBOUND, 1);
	if (!pblk->kw_wq)
		goto free_line_meta_pool;

	if (pblk_set_ppaf(pblk))
		goto free_kw_wq;

	if (pblk_rwb_init(pblk))
		goto free_kw_wq;

	INIT_LIST_HEAD(&pblk->compl_list);
	return 0;

free_kw_wq:
	destroy_workqueue(pblk->kw_wq);
free_line_meta_pool:
	mempool_destroy(pblk->line_meta_pool);
free_w_rq_pool:
	mempool_destroy(pblk->w_rq_pool);
free_r_rq_pool:
	mempool_destroy(pblk->r_rq_pool);
free_rec_pool:
	mempool_destroy(pblk->rec_pool);
free_blk_ws_pool:
	mempool_destroy(pblk->line_ws_pool);
free_page_pool:
	mempool_destroy(pblk->page_pool);
	return -ENOMEM;
}

static void pblk_core_free(struct pblk *pblk)
{
	if (pblk->kw_wq)
		destroy_workqueue(pblk->kw_wq);

	mempool_destroy(pblk->page_pool);
	mempool_destroy(pblk->line_ws_pool);
	mempool_destroy(pblk->rec_pool);
	mempool_destroy(pblk->r_rq_pool);
	mempool_destroy(pblk->w_rq_pool);
	mempool_destroy(pblk->line_meta_pool);

	kmem_cache_destroy(pblk_blk_ws_cache);
	kmem_cache_destroy(pblk_rec_cache);
	kmem_cache_destroy(pblk_r_rq_cache);
	kmem_cache_destroy(pblk_w_rq_cache);
	kmem_cache_destroy(pblk_line_meta_cache);
}

static void pblk_luns_free(struct pblk *pblk)
{
	kfree(pblk->luns);
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
		kfree(line->blk_bitmap);
		kfree(line->erase_bitmap);
	}
	spin_unlock(&l_mg->free_lock);
}

static void pblk_line_meta_free(struct pblk *pblk)
{
	struct pblk_line_mgmt *l_mg = &pblk->l_mg;
	int i;

	kfree(l_mg->bb_template);
	kfree(l_mg->bb_aux);

	for (i = 0; i < PBLK_DATA_LINES; i++) {
		pblk_mfree(l_mg->sline_meta[i].meta, l_mg->smeta_alloc_type);
		pblk_mfree(l_mg->eline_meta[i].meta, l_mg->emeta_alloc_type);
	}

	kfree(pblk->lines);
}

static int pblk_bb_discovery(struct nvm_tgt_dev *dev, struct pblk_lun *rlun)
{
	struct nvm_geo *geo = &dev->geo;
	struct ppa_addr ppa;
	u8 *blks;
	int nr_blks, ret;

	nr_blks = geo->blks_per_lun * geo->plane_mode;
	blks = kmalloc(nr_blks, GFP_KERNEL);
	if (!blks)
		return -ENOMEM;

	ppa.ppa = 0;
	ppa.g.ch = rlun->bppa.g.ch;
	ppa.g.lun = rlun->bppa.g.lun;

	ret = nvm_get_tgt_bb_tbl(dev, ppa, blks);
	if (ret)
		goto out;

	nr_blks = nvm_bb_tbl_fold(dev->parent, blks, nr_blks);
	if (nr_blks < 0) {
		kfree(blks);
		ret = nr_blks;
	}

	rlun->bb_list = blks;

out:
	return ret;
}

static int pblk_bb_line(struct pblk *pblk, struct pblk_line *line)
{
	struct pblk_line_meta *lm = &pblk->lm;
	struct pblk_lun *rlun;
	int bb_cnt = 0;
	int i;

	line->blk_bitmap = kzalloc(lm->blk_bitmap_len, GFP_KERNEL);
	if (!line->blk_bitmap)
		return -ENOMEM;

	line->erase_bitmap = kzalloc(lm->blk_bitmap_len, GFP_KERNEL);
	if (!line->erase_bitmap) {
		kfree(line->blk_bitmap);
		return -ENOMEM;
	}

	for (i = 0; i < lm->blk_per_line; i++) {
		rlun = &pblk->luns[i];
		if (rlun->bb_list[line->id] == NVM_BLK_T_FREE)
			continue;

		set_bit(i, line->blk_bitmap);
		bb_cnt++;
	}

	return bb_cnt;
}

static int pblk_luns_init(struct pblk *pblk, struct ppa_addr *luns)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	struct pblk_lun *rlun;
	int i, ret;

	/* TODO: Implement unbalanced LUN support */
	if (geo->luns_per_chnl < 0) {
		pr_err("pblk: unbalanced LUN config.\n");
		return -EINVAL;
	}

	pblk->luns = kcalloc(geo->nr_luns, sizeof(struct pblk_lun), GFP_KERNEL);
	if (!pblk->luns)
		return -ENOMEM;

	for (i = 0; i < geo->nr_luns; i++) {
		/* Stripe across channels */
		int ch = i % geo->nr_chnls;
		int lun_raw = i / geo->nr_chnls;
		int lunid = lun_raw + ch * geo->luns_per_chnl;

		rlun = &pblk->luns[i];
		rlun->bppa = luns[lunid];

		sema_init(&rlun->wr_sem, 1);

		ret = pblk_bb_discovery(dev, rlun);
		if (ret) {
			while (--i >= 0)
				kfree(pblk->luns[i].bb_list);
			return ret;
		}
	}

	return 0;
}

static int pblk_lines_configure(struct pblk *pblk, int flags)
{
	struct pblk_line *line = NULL;
	int ret = 0;

	if (!(flags & NVM_TARGET_FACTORY)) {
		line = pblk_recov_l2p(pblk);
		if (IS_ERR(line)) {
			pr_err("pblk: could not recover l2p table\n");
			ret = -EFAULT;
		}
	}

	if (!line) {
		/* Configure next line for user data */
		line = pblk_line_get_first_data(pblk);
		if (!line) {
			pr_err("pblk: line list corrupted\n");
			ret = -EFAULT;
		}
	}

	return ret;
}

/* See comment over struct line_emeta definition */
static unsigned int calc_emeta_len(struct pblk *pblk, struct pblk_line_meta *lm)
{
	return (sizeof(struct line_emeta) +
			((lm->sec_per_line - lm->emeta_sec) * sizeof(u64)) +
			(pblk->l_mg.nr_lines * sizeof(u32)) +
			lm->blk_bitmap_len);
}

static void pblk_set_provision(struct pblk *pblk, long nr_free_blks)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	sector_t provisioned;

	pblk->over_pct = 20;

	provisioned = nr_free_blks;
	provisioned *= (100 - pblk->over_pct);
	sector_div(provisioned, 100);

	/* Internally pblk manages all free blocks, but all calculations based
	 * on user capacity consider only provisioned blocks
	 */
	pblk->rl.total_blocks = nr_free_blks;
	pblk->rl.nr_secs = nr_free_blks * geo->sec_per_blk;
	pblk->capacity = provisioned * geo->sec_per_blk;
	atomic_set(&pblk->rl.free_blocks, nr_free_blks);
}

static int pblk_lines_init(struct pblk *pblk)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	struct pblk_line_mgmt *l_mg = &pblk->l_mg;
	struct pblk_line_meta *lm = &pblk->lm;
	struct pblk_line *line;
	unsigned int smeta_len, emeta_len;
	long nr_bad_blks, nr_meta_blks, nr_free_blks;
	int bb_distance;
	int i;
	int ret;

	lm->sec_per_line = geo->sec_per_blk * geo->nr_luns;
	lm->blk_per_line = geo->nr_luns;
	lm->blk_bitmap_len = BITS_TO_LONGS(geo->nr_luns) * sizeof(long);
	lm->sec_bitmap_len = BITS_TO_LONGS(lm->sec_per_line) * sizeof(long);
	lm->lun_bitmap_len = BITS_TO_LONGS(geo->nr_luns) * sizeof(long);
	lm->high_thrs = lm->sec_per_line / 2;
	lm->mid_thrs = lm->sec_per_line / 4;

	/* Calculate necessary pages for smeta. See comment over struct
	 * line_smeta definition
	 */
	lm->smeta_len = sizeof(struct line_smeta) +
				PBLK_LINE_NR_LUN_BITMAP * lm->lun_bitmap_len;

	i = 1;
add_smeta_page:
	lm->smeta_sec = i * geo->sec_per_pl;
	lm->smeta_len = lm->smeta_sec * geo->sec_size;

	smeta_len = sizeof(struct line_smeta) +
				PBLK_LINE_NR_LUN_BITMAP * lm->lun_bitmap_len;
	if (smeta_len > lm->smeta_len) {
		i++;
		goto add_smeta_page;
	}

	/* Calculate necessary pages for emeta. See comment over struct
	 * line_emeta definition
	 */
	i = 1;
add_emeta_page:
	lm->emeta_sec = i * geo->sec_per_pl;
	lm->emeta_len = lm->emeta_sec * geo->sec_size;

	emeta_len = calc_emeta_len(pblk, lm);
	if (emeta_len > lm->emeta_len) {
		i++;
		goto add_emeta_page;
	}
	lm->emeta_bb = geo->nr_luns - i;

	nr_meta_blks = (lm->smeta_sec + lm->emeta_sec +
				(geo->sec_per_blk / 2)) / geo->sec_per_blk;
	lm->min_blk_line = nr_meta_blks + 1;

	l_mg->nr_lines = geo->blks_per_lun;
	l_mg->log_line = l_mg->data_line = NULL;
	l_mg->l_seq_nr = l_mg->d_seq_nr = 0;
	l_mg->nr_free_lines = 0;
	bitmap_zero(&l_mg->meta_bitmap, PBLK_DATA_LINES);

	/* smeta is always small enough to fit on a kmalloc memory allocation,
	 * emeta depends on the number of LUNs allocated to the pblk instance
	 */
	l_mg->smeta_alloc_type = PBLK_KMALLOC_META;
	for (i = 0; i < PBLK_DATA_LINES; i++) {
		l_mg->sline_meta[i].meta = kmalloc(lm->smeta_len, GFP_KERNEL);
		if (!l_mg->sline_meta[i].meta)
			while (--i >= 0) {
				kfree(l_mg->sline_meta[i].meta);
				ret = -ENOMEM;
				goto fail;
			}
	}

	if (lm->emeta_len > KMALLOC_MAX_CACHE_SIZE) {
		l_mg->emeta_alloc_type = PBLK_VMALLOC_META;

		for (i = 0; i < PBLK_DATA_LINES; i++) {
			l_mg->eline_meta[i].meta = vmalloc(lm->emeta_len);
			if (!l_mg->eline_meta[i].meta)
				while (--i >= 0) {
					vfree(l_mg->eline_meta[i].meta);
					ret = -ENOMEM;
					goto fail;
				}
		}
	} else {
		l_mg->emeta_alloc_type = PBLK_KMALLOC_META;

		for (i = 0; i < PBLK_DATA_LINES; i++) {
			l_mg->eline_meta[i].meta =
					kmalloc(lm->emeta_len, GFP_KERNEL);
			if (!l_mg->eline_meta[i].meta)
				while (--i >= 0) {
					kfree(l_mg->eline_meta[i].meta);
					ret = -ENOMEM;
					goto fail;
				}
		}
	}

	l_mg->bb_template = kzalloc(lm->sec_bitmap_len, GFP_KERNEL);
	if (!l_mg->bb_template) {
		ret = -ENOMEM;
		goto fail_free_meta;
	}

	l_mg->bb_aux = kzalloc(lm->sec_bitmap_len, GFP_KERNEL);
	if (!l_mg->bb_aux) {
		ret = -ENOMEM;
		goto fail_free_bb_template;
	}

	bb_distance = (geo->nr_luns) * geo->sec_per_pl;
	for (i = 0; i < lm->sec_per_line; i += bb_distance)
		bitmap_set(l_mg->bb_template, i, geo->sec_per_pl);

	INIT_LIST_HEAD(&l_mg->free_list);
	INIT_LIST_HEAD(&l_mg->corrupt_list);
	INIT_LIST_HEAD(&l_mg->bad_list);
	INIT_LIST_HEAD(&l_mg->gc_full_list);
	INIT_LIST_HEAD(&l_mg->gc_high_list);
	INIT_LIST_HEAD(&l_mg->gc_mid_list);
	INIT_LIST_HEAD(&l_mg->gc_low_list);
	INIT_LIST_HEAD(&l_mg->gc_empty_list);

	l_mg->gc_lists[0] = &l_mg->gc_high_list;
	l_mg->gc_lists[1] = &l_mg->gc_mid_list;
	l_mg->gc_lists[2] = &l_mg->gc_low_list;

	spin_lock_init(&l_mg->free_lock);
	spin_lock_init(&l_mg->gc_lock);

	pblk->lines = kcalloc(l_mg->nr_lines, sizeof(struct pblk_line),
								GFP_KERNEL);
	if (!pblk->lines) {
		ret = -ENOMEM;
		goto fail_free_bb_aux;
	}

	nr_free_blks = 0;
	for (i = 0; i < l_mg->nr_lines; i++) {
		line = &pblk->lines[i];

		line->pblk = pblk;
		line->id = i;
		line->type = PBLK_LINETYPE_FREE;
		line->state = PBLK_LINESTATE_FREE;
		line->gc_group = PBLK_LINEGC_NONE;
		spin_lock_init(&line->lock);

		nr_bad_blks = pblk_bb_line(pblk, line);
		if (nr_bad_blks < 0 || nr_bad_blks > lm->blk_per_line) {
			ret = -EINVAL;
			goto fail_free_lines;
		}

		line->blk_in_line = lm->blk_per_line - nr_bad_blks;
		if (line->blk_in_line < lm->min_blk_line) {
			line->state = PBLK_LINESTATE_BAD;
			list_add_tail(&line->list, &l_mg->bad_list);
			continue;
		}

		nr_free_blks += line->blk_in_line;

		l_mg->nr_free_lines++;
		list_add_tail(&line->list, &l_mg->free_list);
	}

	pblk_set_provision(pblk, nr_free_blks);

	sema_init(&pblk->erase_sem, 1);

	/* Cleanup per-LUN bad block lists - managed within lines on run-time */
	for (i = 0; i < geo->nr_luns; i++)
		kfree(pblk->luns[i].bb_list);

	return 0;
fail_free_lines:
	kfree(pblk->lines);
fail_free_bb_aux:
	kfree(l_mg->bb_aux);
fail_free_bb_template:
	kfree(l_mg->bb_template);
fail_free_meta:
	for (i = 0; i < PBLK_DATA_LINES; i++) {
		pblk_mfree(l_mg->sline_meta[i].meta, l_mg->smeta_alloc_type);
		pblk_mfree(l_mg->eline_meta[i].meta, l_mg->emeta_alloc_type);
	}
fail:
	for (i = 0; i < geo->nr_luns; i++)
		kfree(pblk->luns[i].bb_list);

	return ret;
}

static int pblk_writer_init(struct pblk *pblk)
{
	setup_timer(&pblk->wtimer, pblk_write_timer_fn, (unsigned long)pblk);
	mod_timer(&pblk->wtimer, jiffies + msecs_to_jiffies(100));

	pblk->writer_ts = kthread_create(pblk_write_ts, pblk, "pblk-writer-t");
	if (IS_ERR(pblk->writer_ts)) {
		pr_err("pblk: could not allocate writer kthread\n");
		return PTR_ERR(pblk->writer_ts);
	}

	return 0;
}

static void pblk_writer_stop(struct pblk *pblk)
{
	if (pblk->writer_ts)
		kthread_stop(pblk->writer_ts);
	del_timer(&pblk->wtimer);
}

static void pblk_free(struct pblk *pblk)
{
	pblk_luns_free(pblk);
	pblk_lines_free(pblk);
	pblk_line_meta_free(pblk);
	pblk_core_free(pblk);
	pblk_l2p_free(pblk);

	kfree(pblk);
}

static void pblk_tear_down(struct pblk *pblk)
{
	pblk_flush_writer(pblk);
	pblk_writer_stop(pblk);
	pblk_rb_sync_l2p(&pblk->rwb);
	pblk_recov_pad(pblk);
	pblk_rwb_free(pblk);
	pblk_rl_free(&pblk->rl);

	pr_debug("pblk: consistent tear down\n");
}

static void pblk_exit(void *private)
{
	struct pblk *pblk = private;

	down_write(&pblk_lock);
	pblk_gc_exit(pblk);
	pblk_tear_down(pblk);
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
		pr_err("pblk: device-side L2P table not supported. (%x)\n",
							dev->identity.dom);
		return ERR_PTR(-EINVAL);
	}

	pblk = kzalloc(sizeof(struct pblk), GFP_KERNEL);
	if (!pblk)
		return ERR_PTR(-ENOMEM);

	pblk->dev = dev;
	pblk->disk = tdisk;

	spin_lock_init(&pblk->trans_lock);
	spin_lock_init(&pblk->lock);

	if (flags & NVM_TARGET_FACTORY)
		pblk_setup_uuid(pblk);

#ifdef CONFIG_NVM_DEBUG
	atomic_long_set(&pblk->inflight_writes, 0);
	atomic_long_set(&pblk->padded_writes, 0);
	atomic_long_set(&pblk->padded_wb, 0);
	atomic_long_set(&pblk->nr_flush, 0);
	atomic_long_set(&pblk->req_writes, 0);
	atomic_long_set(&pblk->sub_writes, 0);
	atomic_long_set(&pblk->sync_writes, 0);
	atomic_long_set(&pblk->compl_writes, 0);
	atomic_long_set(&pblk->inflight_reads, 0);
	atomic_long_set(&pblk->sync_reads, 0);
	atomic_long_set(&pblk->recov_writes, 0);
	atomic_long_set(&pblk->recov_writes, 0);
	atomic_long_set(&pblk->recov_gc_writes, 0);
#endif

	atomic_long_set(&pblk->read_failed, 0);
	atomic_long_set(&pblk->read_empty, 0);
	atomic_long_set(&pblk->read_high_ecc, 0);
	atomic_long_set(&pblk->read_failed_gc, 0);
	atomic_long_set(&pblk->write_failed, 0);
	atomic_long_set(&pblk->erase_failed, 0);

	ret = pblk_luns_init(pblk, dev->luns);
	if (ret) {
		pr_err("pblk: could not initialize luns\n");
		goto fail;
	}

	ret = pblk_lines_init(pblk);
	if (ret) {
		pr_err("pblk: could not initialize lines\n");
		goto fail_free_luns;
	}

	ret = pblk_core_init(pblk);
	if (ret) {
		pr_err("pblk: could not initialize core\n");
		goto fail_free_line_meta;
	}

	ret = pblk_l2p_init(pblk);
	if (ret) {
		pr_err("pblk: could not initialize maps\n");
		goto fail_free_core;
	}

	ret = pblk_lines_configure(pblk, flags);
	if (ret) {
		pr_err("pblk: could not configure lines\n");
		goto fail_free_l2p;
	}

	ret = pblk_writer_init(pblk);
	if (ret) {
		pr_err("pblk: could not initialize write thread\n");
		goto fail_free_lines;
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

	tqueue->limits.discard_granularity = geo->pgs_per_blk * geo->pfpg_size;
	tqueue->limits.discard_alignment = 0;
	blk_queue_max_discard_sectors(tqueue, UINT_MAX >> 9);
	queue_flag_set_unlocked(QUEUE_FLAG_DISCARD, tqueue);

	pr_info("pblk init: luns:%u, lines:%d, secs:%llu, buf entries:%u\n",
			geo->nr_luns, pblk->l_mg.nr_lines,
			(unsigned long long)pblk->rl.nr_secs,
			pblk->rwb.nr_entries);

	wake_up_process(pblk->writer_ts);
	return pblk;

fail_stop_writer:
	pblk_writer_stop(pblk);
fail_free_lines:
	pblk_lines_free(pblk);
fail_free_l2p:
	pblk_l2p_free(pblk);
fail_free_core:
	pblk_core_free(pblk);
fail_free_line_meta:
	pblk_line_meta_free(pblk);
fail_free_luns:
	pblk_luns_free(pblk);
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
};

static int __init pblk_module_init(void)
{
	return nvm_register_tgt_type(&tt_pblk);
}

static void pblk_module_exit(void)
{
	nvm_unregister_tgt_type(&tt_pblk);
}

module_init(pblk_module_init);
module_exit(pblk_module_exit);
MODULE_AUTHOR("Javier Gonzalez <javier@cnexlabs.com>");
MODULE_AUTHOR("Matias Bjorling <matias@cnexlabs.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Physical Block-Device for Open-Channel SSDs");
