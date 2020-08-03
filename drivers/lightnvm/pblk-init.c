// SPDX-License-Identifier: GPL-2.0
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
#include "pblk-trace.h"

static unsigned int write_buffer_size;

module_param(write_buffer_size, uint, 0644);
MODULE_PARM_DESC(write_buffer_size, "number of entries in a write buffer");

struct pblk_global_caches {
	struct kmem_cache	*ws;
	struct kmem_cache	*rec;
	struct kmem_cache	*g_rq;
	struct kmem_cache	*w_rq;

	struct kref		kref;

	struct mutex		mutex; /* Ensures consistency between
					* caches and kref
					*/
};

static struct pblk_global_caches pblk_caches = {
	.mutex = __MUTEX_INITIALIZER(pblk_caches.mutex),
	.kref = KREF_INIT(0),
};

struct bio_set pblk_bio_set;

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

	/* Read requests must be <= 256kb due to NVMe's 64 bit completion bitmap
	 * constraint. Writes can be of arbitrary size.
	 */
	if (bio_data_dir(bio) == READ) {
		blk_queue_split(q, &bio);
		pblk_submit_read(pblk, bio);
	} else {
		/* Prevent deadlock in the case of a modest LUN configuration
		 * and large user I/Os. Unless stalled, the rate limiter
		 * leaves at least 256KB available for user I/O.
		 */
		if (pblk_get_secs(bio) > pblk_rl_max_io(&pblk->rl))
			blk_queue_split(q, &bio);

		pblk_write_to_cache(pblk, bio, PBLK_IOTYPE_USER);
	}

	return BLK_QC_T_NONE;
}

static size_t pblk_trans_map_size(struct pblk *pblk)
{
	int entry_size = 8;

	if (pblk->addrf_len < 32)
		entry_size = 4;

	return entry_size * pblk->capacity;
}

#ifdef CONFIG_NVM_PBLK_DEBUG
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
		guid_gen(&pblk->instance_uuid);
	} else {
		line = pblk_recov_l2p(pblk);
		if (IS_ERR(line)) {
			pblk_err(pblk, "could not recover l2p table\n");
			return -EFAULT;
		}
	}

#ifdef CONFIG_NVM_PBLK_DEBUG
	pblk_info(pblk, "init: L2P CRC: %x\n", pblk_l2p_crc(pblk));
#endif

	/* Free full lines directly as GC has not been started yet */
	pblk_gc_free_full_lines(pblk);

	if (!line) {
		/* Configure next line for user data */
		line = pblk_line_get_first_data(pblk);
		if (!line)
			return -EFAULT;
	}

	return 0;
}

static int pblk_l2p_init(struct pblk *pblk, bool factory_init)
{
	sector_t i;
	struct ppa_addr ppa;
	size_t map_size;
	int ret = 0;

	map_size = pblk_trans_map_size(pblk);
	pblk->trans_map = __vmalloc(map_size, GFP_KERNEL | __GFP_NOWARN |
				    __GFP_RETRY_MAYFAIL | __GFP_HIGHMEM);
	if (!pblk->trans_map) {
		pblk_err(pblk, "failed to allocate L2P (need %zu of memory)\n",
				map_size);
		return -ENOMEM;
	}

	pblk_ppa_set_empty(&ppa);

	for (i = 0; i < pblk->capacity; i++)
		pblk_trans_map_set(pblk, i, ppa);

	ret = pblk_l2p_recover(pblk, factory_init);
	if (ret)
		vfree(pblk->trans_map);

	return ret;
}

static void pblk_rwb_free(struct pblk *pblk)
{
	if (pblk_rb_tear_down_check(&pblk->rwb))
		pblk_err(pblk, "write buffer error on tear down\n");

	pblk_rb_free(&pblk->rwb);
}

static int pblk_rwb_init(struct pblk *pblk)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	unsigned long buffer_size;
	int pgs_in_buffer, threshold;

	threshold = geo->mw_cunits * geo->all_luns;
	pgs_in_buffer = (max(geo->mw_cunits, geo->ws_opt) + geo->ws_opt)
								* geo->all_luns;

	if (write_buffer_size && (write_buffer_size > pgs_in_buffer))
		buffer_size = write_buffer_size;
	else
		buffer_size = pgs_in_buffer;

	return pblk_rb_init(&pblk->rwb, buffer_size, threshold, geo->csecs);
}

static int pblk_set_addrf_12(struct pblk *pblk, struct nvm_geo *geo,
			     struct nvm_addrf_12 *dst)
{
	struct nvm_addrf_12 *src = (struct nvm_addrf_12 *)&geo->addrf;
	int power_len;

	/* Re-calculate channel and lun format to adapt to configuration */
	power_len = get_count_order(geo->num_ch);
	if (1 << power_len != geo->num_ch) {
		pblk_err(pblk, "supports only power-of-two channel config.\n");
		return -EINVAL;
	}
	dst->ch_len = power_len;

	power_len = get_count_order(geo->num_lun);
	if (1 << power_len != geo->num_lun) {
		pblk_err(pblk, "supports only power-of-two LUN config.\n");
		return -EINVAL;
	}
	dst->lun_len = power_len;

	dst->blk_len = src->blk_len;
	dst->pg_len = src->pg_len;
	dst->pln_len = src->pln_len;
	dst->sec_len = src->sec_len;

	dst->sec_offset = 0;
	dst->pln_offset = dst->sec_len;
	dst->ch_offset = dst->pln_offset + dst->pln_len;
	dst->lun_offset = dst->ch_offset + dst->ch_len;
	dst->pg_offset = dst->lun_offset + dst->lun_len;
	dst->blk_offset = dst->pg_offset + dst->pg_len;

	dst->sec_mask = ((1ULL << dst->sec_len) - 1) << dst->sec_offset;
	dst->pln_mask = ((1ULL << dst->pln_len) - 1) << dst->pln_offset;
	dst->ch_mask = ((1ULL << dst->ch_len) - 1) << dst->ch_offset;
	dst->lun_mask = ((1ULL << dst->lun_len) - 1) << dst->lun_offset;
	dst->pg_mask = ((1ULL << dst->pg_len) - 1) << dst->pg_offset;
	dst->blk_mask = ((1ULL << dst->blk_len) - 1) << dst->blk_offset;

	return dst->blk_offset + src->blk_len;
}

static int pblk_set_addrf_20(struct nvm_geo *geo, struct nvm_addrf *adst,
			     struct pblk_addrf *udst)
{
	struct nvm_addrf *src = &geo->addrf;

	adst->ch_len = get_count_order(geo->num_ch);
	adst->lun_len = get_count_order(geo->num_lun);
	adst->chk_len = src->chk_len;
	adst->sec_len = src->sec_len;

	adst->sec_offset = 0;
	adst->ch_offset = adst->sec_len;
	adst->lun_offset = adst->ch_offset + adst->ch_len;
	adst->chk_offset = adst->lun_offset + adst->lun_len;

	adst->sec_mask = ((1ULL << adst->sec_len) - 1) << adst->sec_offset;
	adst->chk_mask = ((1ULL << adst->chk_len) - 1) << adst->chk_offset;
	adst->lun_mask = ((1ULL << adst->lun_len) - 1) << adst->lun_offset;
	adst->ch_mask = ((1ULL << adst->ch_len) - 1) << adst->ch_offset;

	udst->sec_stripe = geo->ws_opt;
	udst->ch_stripe = geo->num_ch;
	udst->lun_stripe = geo->num_lun;

	udst->sec_lun_stripe = udst->sec_stripe * udst->ch_stripe;
	udst->sec_ws_stripe = udst->sec_lun_stripe * udst->lun_stripe;

	return adst->chk_offset + adst->chk_len;
}

static int pblk_set_addrf(struct pblk *pblk)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	int mod;

	switch (geo->version) {
	case NVM_OCSSD_SPEC_12:
		div_u64_rem(geo->clba, pblk->min_write_pgs, &mod);
		if (mod) {
			pblk_err(pblk, "bad configuration of sectors/pages\n");
			return -EINVAL;
		}

		pblk->addrf_len = pblk_set_addrf_12(pblk, geo,
							(void *)&pblk->addrf);
		break;
	case NVM_OCSSD_SPEC_20:
		pblk->addrf_len = pblk_set_addrf_20(geo, (void *)&pblk->addrf,
							&pblk->uaddrf);
		break;
	default:
		pblk_err(pblk, "OCSSD revision not supported (%d)\n",
								geo->version);
		return -EINVAL;
	}

	return 0;
}

static int pblk_create_global_caches(void)
{

	pblk_caches.ws = kmem_cache_create("pblk_blk_ws",
				sizeof(struct pblk_line_ws), 0, 0, NULL);
	if (!pblk_caches.ws)
		return -ENOMEM;

	pblk_caches.rec = kmem_cache_create("pblk_rec",
				sizeof(struct pblk_rec_ctx), 0, 0, NULL);
	if (!pblk_caches.rec)
		goto fail_destroy_ws;

	pblk_caches.g_rq = kmem_cache_create("pblk_g_rq", pblk_g_rq_size,
				0, 0, NULL);
	if (!pblk_caches.g_rq)
		goto fail_destroy_rec;

	pblk_caches.w_rq = kmem_cache_create("pblk_w_rq", pblk_w_rq_size,
				0, 0, NULL);
	if (!pblk_caches.w_rq)
		goto fail_destroy_g_rq;

	return 0;

fail_destroy_g_rq:
	kmem_cache_destroy(pblk_caches.g_rq);
fail_destroy_rec:
	kmem_cache_destroy(pblk_caches.rec);
fail_destroy_ws:
	kmem_cache_destroy(pblk_caches.ws);

	return -ENOMEM;
}

static int pblk_get_global_caches(void)
{
	int ret = 0;

	mutex_lock(&pblk_caches.mutex);

	if (kref_get_unless_zero(&pblk_caches.kref))
		goto out;

	ret = pblk_create_global_caches();
	if (!ret)
		kref_init(&pblk_caches.kref);

out:
	mutex_unlock(&pblk_caches.mutex);
	return ret;
}

static void pblk_destroy_global_caches(struct kref *ref)
{
	struct pblk_global_caches *c;

	c = container_of(ref, struct pblk_global_caches, kref);

	kmem_cache_destroy(c->ws);
	kmem_cache_destroy(c->rec);
	kmem_cache_destroy(c->g_rq);
	kmem_cache_destroy(c->w_rq);
}

static void pblk_put_global_caches(void)
{
	mutex_lock(&pblk_caches.mutex);
	kref_put(&pblk_caches.kref, pblk_destroy_global_caches);
	mutex_unlock(&pblk_caches.mutex);
}

static int pblk_core_init(struct pblk *pblk)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	int ret, max_write_ppas;

	atomic64_set(&pblk->user_wa, 0);
	atomic64_set(&pblk->pad_wa, 0);
	atomic64_set(&pblk->gc_wa, 0);
	pblk->user_rst_wa = 0;
	pblk->pad_rst_wa = 0;
	pblk->gc_rst_wa = 0;

	atomic64_set(&pblk->nr_flush, 0);
	pblk->nr_flush_rst = 0;

	pblk->min_write_pgs = geo->ws_opt;
	pblk->min_write_pgs_data = pblk->min_write_pgs;
	max_write_ppas = pblk->min_write_pgs * geo->all_luns;
	pblk->max_write_pgs = min_t(int, max_write_ppas, NVM_MAX_VLBA);
	pblk->max_write_pgs = min_t(int, pblk->max_write_pgs,
		queue_max_hw_sectors(dev->q) / (geo->csecs >> SECTOR_SHIFT));
	pblk_set_sec_per_write(pblk, pblk->min_write_pgs);

	pblk->oob_meta_size = geo->sos;
	if (!pblk_is_oob_meta_supported(pblk)) {
		/* For drives which does not have OOB metadata feature
		 * in order to support recovery feature we need to use
		 * so called packed metadata. Packed metada will store
		 * the same information as OOB metadata (l2p table mapping,
		 * but in the form of the single page at the end of
		 * every write request.
		 */
		if (pblk->min_write_pgs
			* sizeof(struct pblk_sec_meta) > PAGE_SIZE) {
			/* We want to keep all the packed metadata on single
			 * page per write requests. So we need to ensure that
			 * it will fit.
			 *
			 * This is more like sanity check, since there is
			 * no device with such a big minimal write size
			 * (above 1 metabytes).
			 */
			pblk_err(pblk, "Not supported min write size\n");
			return -EINVAL;
		}
		/* For packed meta approach we do some simplification.
		 * On read path we always issue requests which size
		 * equal to max_write_pgs, with all pages filled with
		 * user payload except of last one page which will be
		 * filled with packed metadata.
		 */
		pblk->max_write_pgs = pblk->min_write_pgs;
		pblk->min_write_pgs_data = pblk->min_write_pgs - 1;
	}

	pblk->pad_dist = kcalloc(pblk->min_write_pgs - 1, sizeof(atomic64_t),
								GFP_KERNEL);
	if (!pblk->pad_dist)
		return -ENOMEM;

	if (pblk_get_global_caches())
		goto fail_free_pad_dist;

	/* Internal bios can be at most the sectors signaled by the device. */
	ret = mempool_init_page_pool(&pblk->page_bio_pool, NVM_MAX_VLBA, 0);
	if (ret)
		goto free_global_caches;

	ret = mempool_init_slab_pool(&pblk->gen_ws_pool, PBLK_GEN_WS_POOL_SIZE,
				     pblk_caches.ws);
	if (ret)
		goto free_page_bio_pool;

	ret = mempool_init_slab_pool(&pblk->rec_pool, geo->all_luns,
				     pblk_caches.rec);
	if (ret)
		goto free_gen_ws_pool;

	ret = mempool_init_slab_pool(&pblk->r_rq_pool, geo->all_luns,
				     pblk_caches.g_rq);
	if (ret)
		goto free_rec_pool;

	ret = mempool_init_slab_pool(&pblk->e_rq_pool, geo->all_luns,
				     pblk_caches.g_rq);
	if (ret)
		goto free_r_rq_pool;

	ret = mempool_init_slab_pool(&pblk->w_rq_pool, geo->all_luns,
				     pblk_caches.w_rq);
	if (ret)
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

	if (pblk_set_addrf(pblk))
		goto free_r_end_wq;

	INIT_LIST_HEAD(&pblk->compl_list);
	INIT_LIST_HEAD(&pblk->resubmit_list);

	return 0;

free_r_end_wq:
	destroy_workqueue(pblk->r_end_wq);
free_bb_wq:
	destroy_workqueue(pblk->bb_wq);
free_close_wq:
	destroy_workqueue(pblk->close_wq);
free_w_rq_pool:
	mempool_exit(&pblk->w_rq_pool);
free_e_rq_pool:
	mempool_exit(&pblk->e_rq_pool);
free_r_rq_pool:
	mempool_exit(&pblk->r_rq_pool);
free_rec_pool:
	mempool_exit(&pblk->rec_pool);
free_gen_ws_pool:
	mempool_exit(&pblk->gen_ws_pool);
free_page_bio_pool:
	mempool_exit(&pblk->page_bio_pool);
free_global_caches:
	pblk_put_global_caches();
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

	mempool_exit(&pblk->page_bio_pool);
	mempool_exit(&pblk->gen_ws_pool);
	mempool_exit(&pblk->rec_pool);
	mempool_exit(&pblk->r_rq_pool);
	mempool_exit(&pblk->e_rq_pool);
	mempool_exit(&pblk->w_rq_pool);

	pblk_put_global_caches();
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
		kvfree(l_mg->eline_meta[i]->buf);
		kfree(l_mg->eline_meta[i]);
	}

	mempool_destroy(l_mg->bitmap_pool);
	kmem_cache_destroy(l_mg->bitmap_cache);
}

static void pblk_line_meta_free(struct pblk_line_mgmt *l_mg,
				struct pblk_line *line)
{
	struct pblk_w_err_gc *w_err_gc = line->w_err_gc;

	kfree(line->blk_bitmap);
	kfree(line->erase_bitmap);
	kfree(line->chks);

	kvfree(w_err_gc->lba_list);
	kfree(w_err_gc);
}

static void pblk_lines_free(struct pblk *pblk)
{
	struct pblk_line_mgmt *l_mg = &pblk->l_mg;
	struct pblk_line *line;
	int i;

	for (i = 0; i < l_mg->nr_lines; i++) {
		line = &pblk->lines[i];

		pblk_line_free(line);
		pblk_line_meta_free(l_mg, line);
	}

	pblk_line_mg_free(pblk);

	kfree(pblk->luns);
	kfree(pblk->lines);
}

static int pblk_luns_init(struct pblk *pblk)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	struct pblk_lun *rlun;
	int i;

	/* TODO: Implement unbalanced LUN support */
	if (geo->num_lun < 0) {
		pblk_err(pblk, "unbalanced LUN config.\n");
		return -EINVAL;
	}

	pblk->luns = kcalloc(geo->all_luns, sizeof(struct pblk_lun),
								GFP_KERNEL);
	if (!pblk->luns)
		return -ENOMEM;

	for (i = 0; i < geo->all_luns; i++) {
		/* Stripe across channels */
		int ch = i % geo->num_ch;
		int lun_raw = i / geo->num_ch;
		int lunid = lun_raw + ch * geo->num_lun;

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
			sizeof(struct wa_counters), geo->csecs);
	lm->emeta_len[1] = lm->emeta_sec[1] * geo->csecs;

	/* Round to sector size so that vsc_list starts on its own sector */
	lm->dsec_per_line = lm->sec_per_line - lm->emeta_sec[0];
	lm->emeta_sec[2] = DIV_ROUND_UP(lm->dsec_per_line * sizeof(u64),
			geo->csecs);
	lm->emeta_len[2] = lm->emeta_sec[2] * geo->csecs;

	lm->emeta_sec[3] = DIV_ROUND_UP(l_mg->nr_lines * sizeof(u32),
			geo->csecs);
	lm->emeta_len[3] = lm->emeta_sec[3] * geo->csecs;

	lm->vsc_list_len = l_mg->nr_lines * sizeof(u32);

	return (lm->emeta_len[1] + lm->emeta_len[2] + lm->emeta_len[3]);
}

static int pblk_set_provision(struct pblk *pblk, int nr_free_chks)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct pblk_line_mgmt *l_mg = &pblk->l_mg;
	struct pblk_line_meta *lm = &pblk->lm;
	struct nvm_geo *geo = &dev->geo;
	sector_t provisioned;
	int sec_meta, blk_meta, clba;
	int minimum;

	if (geo->op == NVM_TARGET_DEFAULT_OP)
		pblk->op = PBLK_DEFAULT_OP;
	else
		pblk->op = geo->op;

	minimum = pblk_get_min_chks(pblk);
	provisioned = nr_free_chks;
	provisioned *= (100 - pblk->op);
	sector_div(provisioned, 100);

	if ((nr_free_chks - provisioned) < minimum) {
		if (geo->op != NVM_TARGET_DEFAULT_OP) {
			pblk_err(pblk, "OP too small to create a sane instance\n");
			return -EINTR;
		}

		/* If the user did not specify an OP value, and PBLK_DEFAULT_OP
		 * is not enough, calculate and set sane value
		 */

		provisioned = nr_free_chks - minimum;
		pblk->op =  (100 * minimum) / nr_free_chks;
		pblk_info(pblk, "Default OP insufficient, adjusting OP to %d\n",
				pblk->op);
	}

	pblk->op_blks = nr_free_chks - provisioned;

	/* Internally pblk manages all free blocks, but all calculations based
	 * on user capacity consider only provisioned blocks
	 */
	pblk->rl.total_blocks = nr_free_chks;

	/* Consider sectors used for metadata */
	sec_meta = (lm->smeta_sec + lm->emeta_sec[0]) * l_mg->nr_free_lines;
	blk_meta = DIV_ROUND_UP(sec_meta, geo->clba);

	clba = (geo->clba / pblk->min_write_pgs) * pblk->min_write_pgs_data;
	pblk->capacity = (provisioned - blk_meta) * clba;

	atomic_set(&pblk->rl.free_blocks, nr_free_chks);
	atomic_set(&pblk->rl.free_user_blocks, nr_free_chks);

	return 0;
}

static int pblk_setup_line_meta_chk(struct pblk *pblk, struct pblk_line *line,
				   struct nvm_chk_meta *meta)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	struct pblk_line_meta *lm = &pblk->lm;
	int i, nr_bad_chks = 0;

	for (i = 0; i < lm->blk_per_line; i++) {
		struct pblk_lun *rlun = &pblk->luns[i];
		struct nvm_chk_meta *chunk;
		struct nvm_chk_meta *chunk_meta;
		struct ppa_addr ppa;
		int pos;

		ppa = rlun->bppa;
		pos = pblk_ppa_to_pos(geo, ppa);
		chunk = &line->chks[pos];

		ppa.m.chk = line->id;
		chunk_meta = pblk_chunk_get_off(pblk, meta, ppa);

		chunk->state = chunk_meta->state;
		chunk->type = chunk_meta->type;
		chunk->wi = chunk_meta->wi;
		chunk->slba = chunk_meta->slba;
		chunk->cnlb = chunk_meta->cnlb;
		chunk->wp = chunk_meta->wp;

		trace_pblk_chunk_state(pblk_disk_name(pblk), &ppa,
					chunk->state);

		if (chunk->type & NVM_CHK_TP_SZ_SPEC) {
			WARN_ONCE(1, "pblk: custom-sized chunks unsupported\n");
			continue;
		}

		if (!(chunk->state & NVM_CHK_ST_OFFLINE))
			continue;

		set_bit(pos, line->blk_bitmap);
		nr_bad_chks++;
	}

	return nr_bad_chks;
}

static long pblk_setup_line_meta(struct pblk *pblk, struct pblk_line *line,
				 void *chunk_meta, int line_id)
{
	struct pblk_line_mgmt *l_mg = &pblk->l_mg;
	struct pblk_line_meta *lm = &pblk->lm;
	long nr_bad_chks, chk_in_line;

	line->pblk = pblk;
	line->id = line_id;
	line->type = PBLK_LINETYPE_FREE;
	line->state = PBLK_LINESTATE_NEW;
	line->gc_group = PBLK_LINEGC_NONE;
	line->vsc = &l_mg->vsc_list[line_id];
	spin_lock_init(&line->lock);

	nr_bad_chks = pblk_setup_line_meta_chk(pblk, line, chunk_meta);

	chk_in_line = lm->blk_per_line - nr_bad_chks;
	if (nr_bad_chks < 0 || nr_bad_chks > lm->blk_per_line ||
					chk_in_line < lm->min_blk_line) {
		line->state = PBLK_LINESTATE_BAD;
		list_add_tail(&line->list, &l_mg->bad_list);
		return 0;
	}

	atomic_set(&line->blk_in_line, chk_in_line);
	list_add_tail(&line->list, &l_mg->free_list);
	l_mg->nr_free_lines++;

	return chk_in_line;
}

static int pblk_alloc_line_meta(struct pblk *pblk, struct pblk_line *line)
{
	struct pblk_line_meta *lm = &pblk->lm;

	line->blk_bitmap = kzalloc(lm->blk_bitmap_len, GFP_KERNEL);
	if (!line->blk_bitmap)
		return -ENOMEM;

	line->erase_bitmap = kzalloc(lm->blk_bitmap_len, GFP_KERNEL);
	if (!line->erase_bitmap)
		goto free_blk_bitmap;


	line->chks = kmalloc_array(lm->blk_per_line,
				   sizeof(struct nvm_chk_meta), GFP_KERNEL);
	if (!line->chks)
		goto free_erase_bitmap;

	line->w_err_gc = kzalloc(sizeof(struct pblk_w_err_gc), GFP_KERNEL);
	if (!line->w_err_gc)
		goto free_chks;

	return 0;

free_chks:
	kfree(line->chks);
free_erase_bitmap:
	kfree(line->erase_bitmap);
free_blk_bitmap:
	kfree(line->blk_bitmap);
	return -ENOMEM;
}

static int pblk_line_mg_init(struct pblk *pblk)
{
	struct nvm_tgt_dev *dev = pblk->dev;
	struct nvm_geo *geo = &dev->geo;
	struct pblk_line_mgmt *l_mg = &pblk->l_mg;
	struct pblk_line_meta *lm = &pblk->lm;
	int i, bb_distance;

	l_mg->nr_lines = geo->num_chk;
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
	INIT_LIST_HEAD(&l_mg->gc_werr_list);

	INIT_LIST_HEAD(&l_mg->emeta_list);

	l_mg->gc_lists[0] = &l_mg->gc_werr_list;
	l_mg->gc_lists[1] = &l_mg->gc_high_list;
	l_mg->gc_lists[2] = &l_mg->gc_mid_list;
	l_mg->gc_lists[3] = &l_mg->gc_low_list;

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

	l_mg->bitmap_cache = kmem_cache_create("pblk_lm_bitmap",
			lm->sec_bitmap_len, 0, 0, NULL);
	if (!l_mg->bitmap_cache)
		goto fail_free_smeta;

	/* the bitmap pool is used for both valid and map bitmaps */
	l_mg->bitmap_pool = mempool_create_slab_pool(PBLK_DATA_LINES * 2,
				l_mg->bitmap_cache);
	if (!l_mg->bitmap_pool)
		goto fail_destroy_bitmap_cache;

	/* emeta allocates three different buffers for managing metadata with
	 * in-memory and in-media layouts
	 */
	for (i = 0; i < PBLK_DATA_LINES; i++) {
		struct pblk_emeta *emeta;

		emeta = kmalloc(sizeof(struct pblk_emeta), GFP_KERNEL);
		if (!emeta)
			goto fail_free_emeta;

		emeta->buf = kvmalloc(lm->emeta_len[0], GFP_KERNEL);
		if (!emeta->buf) {
			kfree(emeta);
			goto fail_free_emeta;
		}

		emeta->nr_entries = lm->emeta_sec[0];
		l_mg->eline_meta[i] = emeta;
	}

	for (i = 0; i < l_mg->nr_lines; i++)
		l_mg->vsc_list[i] = cpu_to_le32(EMPTY_ENTRY);

	bb_distance = (geo->all_luns) * geo->ws_opt;
	for (i = 0; i < lm->sec_per_line; i += bb_distance)
		bitmap_set(l_mg->bb_template, i, geo->ws_opt);

	return 0;

fail_free_emeta:
	while (--i >= 0) {
		kvfree(l_mg->eline_meta[i]->buf);
		kfree(l_mg->eline_meta[i]);
	}

	mempool_destroy(l_mg->bitmap_pool);
fail_destroy_bitmap_cache:
	kmem_cache_destroy(l_mg->bitmap_cache);
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

	lm->sec_per_line = geo->clba * geo->all_luns;
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
	lm->smeta_sec = i * geo->ws_opt;
	lm->smeta_len = lm->smeta_sec * geo->csecs;

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
	lm->emeta_sec[0] = i * geo->ws_opt;
	lm->emeta_len[0] = lm->emeta_sec[0] * geo->csecs;

	emeta_len = calc_emeta_len(pblk);
	if (emeta_len > lm->emeta_len[0]) {
		i++;
		goto add_emeta_page;
	}

	lm->emeta_bb = geo->all_luns > i ? geo->all_luns - i : 0;

	lm->min_blk_line = 1;
	if (geo->all_luns > 1)
		lm->min_blk_line += DIV_ROUND_UP(lm->smeta_sec +
					lm->emeta_sec[0], geo->clba);

	if (lm->min_blk_line > lm->blk_per_line) {
		pblk_err(pblk, "config. not supported. Min. LUN in line:%d\n",
							lm->blk_per_line);
		return -EINVAL;
	}

	return 0;
}

static int pblk_lines_init(struct pblk *pblk)
{
	struct pblk_line_mgmt *l_mg = &pblk->l_mg;
	struct pblk_line *line;
	void *chunk_meta;
	int nr_free_chks = 0;
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

	chunk_meta = pblk_get_chunk_meta(pblk);
	if (IS_ERR(chunk_meta)) {
		ret = PTR_ERR(chunk_meta);
		goto fail_free_luns;
	}

	pblk->lines = kcalloc(l_mg->nr_lines, sizeof(struct pblk_line),
								GFP_KERNEL);
	if (!pblk->lines) {
		ret = -ENOMEM;
		goto fail_free_chunk_meta;
	}

	for (i = 0; i < l_mg->nr_lines; i++) {
		line = &pblk->lines[i];

		ret = pblk_alloc_line_meta(pblk, line);
		if (ret)
			goto fail_free_lines;

		nr_free_chks += pblk_setup_line_meta(pblk, line, chunk_meta, i);

		trace_pblk_line_state(pblk_disk_name(pblk), line->id,
								line->state);
	}

	if (!nr_free_chks) {
		pblk_err(pblk, "too many bad blocks prevent for sane instance\n");
		ret = -EINTR;
		goto fail_free_lines;
	}

	ret = pblk_set_provision(pblk, nr_free_chks);
	if (ret)
		goto fail_free_lines;

	vfree(chunk_meta);
	return 0;

fail_free_lines:
	while (--i >= 0)
		pblk_line_meta_free(l_mg, &pblk->lines[i]);
	kfree(pblk->lines);
fail_free_chunk_meta:
	vfree(chunk_meta);
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
			pblk_err(pblk, "could not allocate writer kthread (%d)\n",
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

static void pblk_tear_down(struct pblk *pblk, bool graceful)
{
	if (graceful)
		__pblk_pipeline_flush(pblk);
	__pblk_pipeline_stop(pblk);
	pblk_writer_stop(pblk);
	pblk_rb_sync_l2p(&pblk->rwb);
	pblk_rl_free(&pblk->rl);

	pblk_debug(pblk, "consistent tear down (graceful:%d)\n", graceful);
}

static void pblk_exit(void *private, bool graceful)
{
	struct pblk *pblk = private;

	pblk_gc_exit(pblk, graceful);
	pblk_tear_down(pblk, graceful);

#ifdef CONFIG_NVM_PBLK_DEBUG
	pblk_info(pblk, "exit: L2P CRC: %x\n", pblk_l2p_crc(pblk));
#endif

	pblk_free(pblk);
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

	pblk = kzalloc(sizeof(struct pblk), GFP_KERNEL);
	if (!pblk)
		return ERR_PTR(-ENOMEM);

	pblk->dev = dev;
	pblk->disk = tdisk;
	pblk->state = PBLK_STATE_RUNNING;
	trace_pblk_state(pblk_disk_name(pblk), pblk->state);
	pblk->gc.gc_enabled = 0;

	if (!(geo->version == NVM_OCSSD_SPEC_12 ||
					geo->version == NVM_OCSSD_SPEC_20)) {
		pblk_err(pblk, "OCSSD version not supported (%u)\n",
							geo->version);
		kfree(pblk);
		return ERR_PTR(-EINVAL);
	}

	if (geo->ext) {
		pblk_err(pblk, "extended metadata not supported\n");
		kfree(pblk);
		return ERR_PTR(-EINVAL);
	}

	spin_lock_init(&pblk->resubmit_lock);
	spin_lock_init(&pblk->trans_lock);
	spin_lock_init(&pblk->lock);

#ifdef CONFIG_NVM_PBLK_DEBUG
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
		pblk_err(pblk, "could not initialize core\n");
		goto fail;
	}

	ret = pblk_lines_init(pblk);
	if (ret) {
		pblk_err(pblk, "could not initialize lines\n");
		goto fail_free_core;
	}

	ret = pblk_rwb_init(pblk);
	if (ret) {
		pblk_err(pblk, "could not initialize write buffer\n");
		goto fail_free_lines;
	}

	ret = pblk_l2p_init(pblk, flags & NVM_TARGET_FACTORY);
	if (ret) {
		pblk_err(pblk, "could not initialize maps\n");
		goto fail_free_rwb;
	}

	ret = pblk_writer_init(pblk);
	if (ret) {
		if (ret != -EINTR)
			pblk_err(pblk, "could not initialize write thread\n");
		goto fail_free_l2p;
	}

	ret = pblk_gc_init(pblk);
	if (ret) {
		pblk_err(pblk, "could not initialize gc\n");
		goto fail_stop_writer;
	}

	/* inherit the size from the underlying device */
	blk_queue_logical_block_size(tqueue, queue_physical_block_size(bqueue));
	blk_queue_max_hw_sectors(tqueue, queue_max_hw_sectors(bqueue));

	blk_queue_write_cache(tqueue, true, false);

	tqueue->limits.discard_granularity = geo->clba * geo->csecs;
	tqueue->limits.discard_alignment = 0;
	blk_queue_max_discard_sectors(tqueue, UINT_MAX >> 9);
	blk_queue_flag_set(QUEUE_FLAG_DISCARD, tqueue);

	pblk_info(pblk, "luns:%u, lines:%d, secs:%llu, buf entries:%u\n",
			geo->all_luns, pblk->l_mg.nr_lines,
			(unsigned long long)pblk->capacity,
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

	ret = bioset_init(&pblk_bio_set, BIO_POOL_SIZE, 0, 0);
	if (ret)
		return ret;
	ret = nvm_register_tgt_type(&tt_pblk);
	if (ret)
		bioset_exit(&pblk_bio_set);
	return ret;
}

static void pblk_module_exit(void)
{
	bioset_exit(&pblk_bio_set);
	nvm_unregister_tgt_type(&tt_pblk);
}

module_init(pblk_module_init);
module_exit(pblk_module_exit);
MODULE_AUTHOR("Javier Gonzalez <javier@cnexlabs.com>");
MODULE_AUTHOR("Matias Bjorling <matias@cnexlabs.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Physical Block-Device for Open-Channel SSDs");
