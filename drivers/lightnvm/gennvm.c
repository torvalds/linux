/*
 * Copyright (C) 2015 Matias Bjorling <m@bjorling.me>
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
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139,
 * USA.
 *
 * Implementation of a generic nvm manager for Open-Channel SSDs.
 */

#include "gennvm.h"

static void gennvm_blocks_free(struct nvm_dev *dev)
{
	struct gen_nvm *gn = dev->mp;
	struct gen_lun *lun;
	int i;

	gennvm_for_each_lun(gn, lun, i) {
		if (!lun->vlun.blocks)
			break;
		vfree(lun->vlun.blocks);
	}
}

static void gennvm_luns_free(struct nvm_dev *dev)
{
	struct gen_nvm *gn = dev->mp;

	kfree(gn->luns);
}

static int gennvm_luns_init(struct nvm_dev *dev, struct gen_nvm *gn)
{
	struct gen_lun *lun;
	int i;

	gn->luns = kcalloc(dev->nr_luns, sizeof(struct gen_lun), GFP_KERNEL);
	if (!gn->luns)
		return -ENOMEM;

	gennvm_for_each_lun(gn, lun, i) {
		spin_lock_init(&lun->vlun.lock);
		INIT_LIST_HEAD(&lun->free_list);
		INIT_LIST_HEAD(&lun->used_list);
		INIT_LIST_HEAD(&lun->bb_list);

		lun->reserved_blocks = 2; /* for GC only */
		lun->vlun.id = i;
		lun->vlun.lun_id = i % dev->luns_per_chnl;
		lun->vlun.chnl_id = i / dev->luns_per_chnl;
		lun->vlun.nr_free_blocks = dev->blks_per_lun;
		lun->vlun.nr_inuse_blocks = 0;
		lun->vlun.nr_bad_blocks = 0;
	}
	return 0;
}

static int gennvm_block_bb(struct ppa_addr ppa, int nr_blocks, u8 *blks,
								void *private)
{
	struct gen_nvm *gn = private;
	struct nvm_dev *dev = gn->dev;
	struct gen_lun *lun;
	struct nvm_block *blk;
	int i;

	ppa = dev_to_generic_addr(gn->dev, ppa);
	lun = &gn->luns[(dev->nr_luns * ppa.g.ch) + ppa.g.lun];

	for (i = 0; i < nr_blocks; i++) {
		if (blks[i] == 0)
			continue;

		blk = &lun->vlun.blocks[i];
		if (!blk) {
			pr_err("gennvm: BB data is out of bounds.\n");
			return -EINVAL;
		}

		list_move_tail(&blk->list, &lun->bb_list);
		lun->vlun.nr_bad_blocks++;
	}

	return 0;
}

static int gennvm_block_map(u64 slba, u32 nlb, __le64 *entries, void *private)
{
	struct nvm_dev *dev = private;
	struct gen_nvm *gn = dev->mp;
	sector_t max_pages = dev->total_pages * (dev->sec_size >> 9);
	u64 elba = slba + nlb;
	struct gen_lun *lun;
	struct nvm_block *blk;
	u64 i;
	int lun_id;

	if (unlikely(elba > dev->total_pages)) {
		pr_err("gennvm: L2P data from device is out of bounds!\n");
		return -EINVAL;
	}

	for (i = 0; i < nlb; i++) {
		u64 pba = le64_to_cpu(entries[i]);

		if (unlikely(pba >= max_pages && pba != U64_MAX)) {
			pr_err("gennvm: L2P data entry is out of bounds!\n");
			return -EINVAL;
		}

		/* Address zero is a special one. The first page on a disk is
		 * protected. It often holds internal device boot
		 * information.
		 */
		if (!pba)
			continue;

		/* resolve block from physical address */
		lun_id = div_u64(pba, dev->sec_per_lun);
		lun = &gn->luns[lun_id];

		/* Calculate block offset into lun */
		pba = pba - (dev->sec_per_lun * lun_id);
		blk = &lun->vlun.blocks[div_u64(pba, dev->sec_per_blk)];

		if (!blk->type) {
			/* at this point, we don't know anything about the
			 * block. It's up to the FTL on top to re-etablish the
			 * block state
			 */
			list_move_tail(&blk->list, &lun->used_list);
			blk->type = 1;
			lun->vlun.nr_free_blocks--;
			lun->vlun.nr_inuse_blocks++;
		}
	}

	return 0;
}

static int gennvm_blocks_init(struct nvm_dev *dev, struct gen_nvm *gn)
{
	struct gen_lun *lun;
	struct nvm_block *block;
	sector_t lun_iter, blk_iter, cur_block_id = 0;
	int ret;

	gennvm_for_each_lun(gn, lun, lun_iter) {
		lun->vlun.blocks = vzalloc(sizeof(struct nvm_block) *
							dev->blks_per_lun);
		if (!lun->vlun.blocks)
			return -ENOMEM;

		for (blk_iter = 0; blk_iter < dev->blks_per_lun; blk_iter++) {
			block = &lun->vlun.blocks[blk_iter];

			INIT_LIST_HEAD(&block->list);

			block->lun = &lun->vlun;
			block->id = cur_block_id++;

			/* First block is reserved for device */
			if (unlikely(lun_iter == 0 && blk_iter == 0)) {
				lun->vlun.nr_free_blocks--;
				continue;
			}

			list_add_tail(&block->list, &lun->free_list);
		}

		if (dev->ops->get_bb_tbl) {
			struct ppa_addr ppa;

			ppa.ppa = 0;
			ppa.g.ch = lun->vlun.chnl_id;
			ppa.g.lun = lun->vlun.id;
			ppa = generic_to_dev_addr(dev, ppa);

			ret = dev->ops->get_bb_tbl(dev->q, ppa,
						dev->blks_per_lun,
						gennvm_block_bb, gn);
			if (ret)
				pr_err("gennvm: could not read BB table\n");
		}
	}

	if (dev->ops->get_l2p_tbl) {
		ret = dev->ops->get_l2p_tbl(dev->q, 0, dev->total_pages,
							gennvm_block_map, dev);
		if (ret) {
			pr_err("gennvm: could not read L2P table.\n");
			pr_warn("gennvm: default block initialization");
		}
	}

	return 0;
}

static int gennvm_register(struct nvm_dev *dev)
{
	struct gen_nvm *gn;
	int ret;

	gn = kzalloc(sizeof(struct gen_nvm), GFP_KERNEL);
	if (!gn)
		return -ENOMEM;

	gn->dev = dev;
	gn->nr_luns = dev->nr_luns;
	dev->mp = gn;

	ret = gennvm_luns_init(dev, gn);
	if (ret) {
		pr_err("gennvm: could not initialize luns\n");
		goto err;
	}

	ret = gennvm_blocks_init(dev, gn);
	if (ret) {
		pr_err("gennvm: could not initialize blocks\n");
		goto err;
	}

	return 1;
err:
	kfree(gn);
	return ret;
}

static void gennvm_unregister(struct nvm_dev *dev)
{
	gennvm_blocks_free(dev);
	gennvm_luns_free(dev);
	kfree(dev->mp);
	dev->mp = NULL;
}

static struct nvm_block *gennvm_get_blk(struct nvm_dev *dev,
				struct nvm_lun *vlun, unsigned long flags)
{
	struct gen_lun *lun = container_of(vlun, struct gen_lun, vlun);
	struct nvm_block *blk = NULL;
	int is_gc = flags & NVM_IOTYPE_GC;

	spin_lock(&vlun->lock);

	if (list_empty(&lun->free_list)) {
		pr_err_ratelimited("gennvm: lun %u have no free pages available",
								lun->vlun.id);
		spin_unlock(&vlun->lock);
		goto out;
	}

	while (!is_gc && lun->vlun.nr_free_blocks < lun->reserved_blocks) {
		spin_unlock(&vlun->lock);
		goto out;
	}

	blk = list_first_entry(&lun->free_list, struct nvm_block, list);
	list_move_tail(&blk->list, &lun->used_list);
	blk->type = 1;

	lun->vlun.nr_free_blocks--;
	lun->vlun.nr_inuse_blocks++;

	spin_unlock(&vlun->lock);
out:
	return blk;
}

static void gennvm_put_blk(struct nvm_dev *dev, struct nvm_block *blk)
{
	struct nvm_lun *vlun = blk->lun;
	struct gen_lun *lun = container_of(vlun, struct gen_lun, vlun);

	spin_lock(&vlun->lock);

	switch (blk->type) {
	case 1:
		list_move_tail(&blk->list, &lun->free_list);
		lun->vlun.nr_free_blocks++;
		lun->vlun.nr_inuse_blocks--;
		blk->type = 0;
		break;
	case 2:
		list_move_tail(&blk->list, &lun->bb_list);
		lun->vlun.nr_bad_blocks++;
		lun->vlun.nr_inuse_blocks--;
		break;
	default:
		WARN_ON_ONCE(1);
		pr_err("gennvm: erroneous block type (%lu -> %u)\n",
							blk->id, blk->type);
		list_move_tail(&blk->list, &lun->bb_list);
		lun->vlun.nr_bad_blocks++;
		lun->vlun.nr_inuse_blocks--;
	}

	spin_unlock(&vlun->lock);
}

static void gennvm_addr_to_generic_mode(struct nvm_dev *dev, struct nvm_rq *rqd)
{
	int i;

	if (rqd->nr_pages > 1) {
		for (i = 0; i < rqd->nr_pages; i++)
			rqd->ppa_list[i] = dev_to_generic_addr(dev,
							rqd->ppa_list[i]);
	} else {
		rqd->ppa_addr = dev_to_generic_addr(dev, rqd->ppa_addr);
	}
}

static void gennvm_generic_to_addr_mode(struct nvm_dev *dev, struct nvm_rq *rqd)
{
	int i;

	if (rqd->nr_pages > 1) {
		for (i = 0; i < rqd->nr_pages; i++)
			rqd->ppa_list[i] = generic_to_dev_addr(dev,
							rqd->ppa_list[i]);
	} else {
		rqd->ppa_addr = generic_to_dev_addr(dev, rqd->ppa_addr);
	}
}

static int gennvm_submit_io(struct nvm_dev *dev, struct nvm_rq *rqd)
{
	if (!dev->ops->submit_io)
		return 0;

	/* Convert address space */
	gennvm_generic_to_addr_mode(dev, rqd);

	rqd->dev = dev;
	return dev->ops->submit_io(dev->q, rqd);
}

static void gennvm_blk_set_type(struct nvm_dev *dev, struct ppa_addr *ppa,
								int type)
{
	struct gen_nvm *gn = dev->mp;
	struct gen_lun *lun;
	struct nvm_block *blk;

	if (unlikely(ppa->g.ch > dev->nr_chnls ||
					ppa->g.lun > dev->luns_per_chnl ||
					ppa->g.blk > dev->blks_per_lun)) {
		WARN_ON_ONCE(1);
		pr_err("gennvm: ppa broken (ch: %u > %u lun: %u > %u blk: %u > %u",
				ppa->g.ch, dev->nr_chnls,
				ppa->g.lun, dev->luns_per_chnl,
				ppa->g.blk, dev->blks_per_lun);
		return;
	}

	lun = &gn->luns[ppa->g.lun * ppa->g.ch];
	blk = &lun->vlun.blocks[ppa->g.blk];

	/* will be moved to bb list on put_blk from target */
	blk->type = type;
}

/* mark block bad. It is expected the target recover from the error. */
static void gennvm_mark_blk_bad(struct nvm_dev *dev, struct nvm_rq *rqd)
{
	int i;

	if (!dev->ops->set_bb_tbl)
		return;

	if (dev->ops->set_bb_tbl(dev->q, rqd, 1))
		return;

	gennvm_addr_to_generic_mode(dev, rqd);

	/* look up blocks and mark them as bad */
	if (rqd->nr_pages > 1)
		for (i = 0; i < rqd->nr_pages; i++)
			gennvm_blk_set_type(dev, &rqd->ppa_list[i], 2);
	else
		gennvm_blk_set_type(dev, &rqd->ppa_addr, 2);
}

static int gennvm_end_io(struct nvm_rq *rqd, int error)
{
	struct nvm_tgt_instance *ins = rqd->ins;
	int ret = 0;

	switch (error) {
	case NVM_RSP_SUCCESS:
		break;
	case NVM_RSP_ERR_EMPTYPAGE:
		break;
	case NVM_RSP_ERR_FAILWRITE:
		gennvm_mark_blk_bad(rqd->dev, rqd);
	default:
		ret++;
	}

	ret += ins->tt->end_io(rqd, error);

	return ret;
}

static int gennvm_erase_blk(struct nvm_dev *dev, struct nvm_block *blk,
							unsigned long flags)
{
	int plane_cnt = 0, pl_idx, ret;
	struct ppa_addr addr;
	struct nvm_rq rqd;

	if (!dev->ops->erase_block)
		return 0;

	addr = block_to_ppa(dev, blk);

	if (dev->plane_mode == NVM_PLANE_SINGLE) {
		rqd.nr_pages = 1;
		rqd.ppa_addr = addr;
	} else {
		plane_cnt = (1 << dev->plane_mode);
		rqd.nr_pages = plane_cnt;

		rqd.ppa_list = nvm_dev_dma_alloc(dev, GFP_KERNEL,
							&rqd.dma_ppa_list);
		if (!rqd.ppa_list) {
			pr_err("gennvm: failed to allocate dma memory\n");
			return -ENOMEM;
		}

		for (pl_idx = 0; pl_idx < plane_cnt; pl_idx++) {
			addr.g.pl = pl_idx;
			rqd.ppa_list[pl_idx] = addr;
		}
	}

	gennvm_generic_to_addr_mode(dev, &rqd);

	ret = dev->ops->erase_block(dev->q, &rqd);

	if (plane_cnt)
		nvm_dev_dma_free(dev, rqd.ppa_list, rqd.dma_ppa_list);

	return ret;
}

static struct nvm_lun *gennvm_get_lun(struct nvm_dev *dev, int lunid)
{
	struct gen_nvm *gn = dev->mp;

	return &gn->luns[lunid].vlun;
}

static void gennvm_lun_info_print(struct nvm_dev *dev)
{
	struct gen_nvm *gn = dev->mp;
	struct gen_lun *lun;
	unsigned int i;


	gennvm_for_each_lun(gn, lun, i) {
		spin_lock(&lun->vlun.lock);

		pr_info("%s: lun%8u\t%u\t%u\t%u\n",
				dev->name, i,
				lun->vlun.nr_free_blocks,
				lun->vlun.nr_inuse_blocks,
				lun->vlun.nr_bad_blocks);

		spin_unlock(&lun->vlun.lock);
	}
}

static struct nvmm_type gennvm = {
	.name		= "gennvm",
	.version	= {0, 1, 0},

	.register_mgr	= gennvm_register,
	.unregister_mgr	= gennvm_unregister,

	.get_blk	= gennvm_get_blk,
	.put_blk	= gennvm_put_blk,

	.submit_io	= gennvm_submit_io,
	.end_io		= gennvm_end_io,
	.erase_blk	= gennvm_erase_blk,

	.get_lun	= gennvm_get_lun,
	.lun_info_print = gennvm_lun_info_print,
};

static int __init gennvm_module_init(void)
{
	return nvm_register_mgr(&gennvm);
}

static void gennvm_module_exit(void)
{
	nvm_unregister_mgr(&gennvm);
}

module_init(gennvm_module_init);
module_exit(gennvm_module_exit);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Generic media manager for Open-Channel SSDs");
