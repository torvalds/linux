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

static int gennvm_get_area(struct nvm_dev *dev, sector_t *lba, sector_t len)
{
	struct gen_nvm *gn = dev->mp;
	struct gennvm_area *area, *prev, *next;
	sector_t begin = 0;
	sector_t max_sectors = (dev->sec_size * dev->total_secs) >> 9;

	if (len > max_sectors)
		return -EINVAL;

	area = kmalloc(sizeof(struct gennvm_area), GFP_KERNEL);
	if (!area)
		return -ENOMEM;

	prev = NULL;

	spin_lock(&dev->lock);
	list_for_each_entry(next, &gn->area_list, list) {
		if (begin + len > next->begin) {
			begin = next->end;
			prev = next;
			continue;
		}
		break;
	}

	if ((begin + len) > max_sectors) {
		spin_unlock(&dev->lock);
		kfree(area);
		return -EINVAL;
	}

	area->begin = *lba = begin;
	area->end = begin + len;

	if (prev) /* insert into sorted order */
		list_add(&area->list, &prev->list);
	else
		list_add(&area->list, &gn->area_list);
	spin_unlock(&dev->lock);

	return 0;
}

static void gennvm_put_area(struct nvm_dev *dev, sector_t begin)
{
	struct gen_nvm *gn = dev->mp;
	struct gennvm_area *area;

	spin_lock(&dev->lock);
	list_for_each_entry(area, &gn->area_list, list) {
		if (area->begin != begin)
			continue;

		list_del(&area->list);
		spin_unlock(&dev->lock);
		kfree(area);
		return;
	}
	spin_unlock(&dev->lock);
}

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
		lun->vlun.nr_open_blocks = 0;
		lun->vlun.nr_closed_blocks = 0;
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

	lun = &gn->luns[(dev->luns_per_chnl * ppa.g.ch) + ppa.g.lun];

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
		lun->vlun.nr_free_blocks--;
	}

	return 0;
}

static int gennvm_block_map(u64 slba, u32 nlb, __le64 *entries, void *private)
{
	struct nvm_dev *dev = private;
	struct gen_nvm *gn = dev->mp;
	u64 elba = slba + nlb;
	struct gen_lun *lun;
	struct nvm_block *blk;
	u64 i;
	int lun_id;

	if (unlikely(elba > dev->total_secs)) {
		pr_err("gennvm: L2P data from device is out of bounds!\n");
		return -EINVAL;
	}

	for (i = 0; i < nlb; i++) {
		u64 pba = le64_to_cpu(entries[i]);

		if (unlikely(pba >= dev->total_secs && pba != U64_MAX)) {
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

		if (!blk->state) {
			/* at this point, we don't know anything about the
			 * block. It's up to the FTL on top to re-etablish the
			 * block state. The block is assumed to be open.
			 */
			list_move_tail(&blk->list, &lun->used_list);
			blk->state = NVM_BLK_ST_OPEN;
			lun->vlun.nr_free_blocks--;
			lun->vlun.nr_open_blocks++;
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

			ret = dev->ops->get_bb_tbl(dev, ppa,
						dev->blks_per_lun,
						gennvm_block_bb, gn);
			if (ret)
				pr_err("gennvm: could not read BB table\n");
		}
	}

	if ((dev->identity.dom & NVM_RSP_L2P) && dev->ops->get_l2p_tbl) {
		ret = dev->ops->get_l2p_tbl(dev, 0, dev->total_secs,
							gennvm_block_map, dev);
		if (ret) {
			pr_err("gennvm: could not read L2P table.\n");
			pr_warn("gennvm: default block initialization");
		}
	}

	return 0;
}

static void gennvm_free(struct nvm_dev *dev)
{
	gennvm_blocks_free(dev);
	gennvm_luns_free(dev);
	kfree(dev->mp);
	dev->mp = NULL;
}

static int gennvm_register(struct nvm_dev *dev)
{
	struct gen_nvm *gn;
	int ret;

	if (!try_module_get(THIS_MODULE))
		return -ENODEV;

	gn = kzalloc(sizeof(struct gen_nvm), GFP_KERNEL);
	if (!gn)
		return -ENOMEM;

	gn->dev = dev;
	gn->nr_luns = dev->nr_luns;
	INIT_LIST_HEAD(&gn->area_list);
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
	gennvm_free(dev);
	module_put(THIS_MODULE);
	return ret;
}

static void gennvm_unregister(struct nvm_dev *dev)
{
	gennvm_free(dev);
	module_put(THIS_MODULE);
}

static struct nvm_block *gennvm_get_blk_unlocked(struct nvm_dev *dev,
				struct nvm_lun *vlun, unsigned long flags)
{
	struct gen_lun *lun = container_of(vlun, struct gen_lun, vlun);
	struct nvm_block *blk = NULL;
	int is_gc = flags & NVM_IOTYPE_GC;

	assert_spin_locked(&vlun->lock);

	if (list_empty(&lun->free_list)) {
		pr_err_ratelimited("gennvm: lun %u have no free pages available",
								lun->vlun.id);
		goto out;
	}

	if (!is_gc && lun->vlun.nr_free_blocks < lun->reserved_blocks)
		goto out;

	blk = list_first_entry(&lun->free_list, struct nvm_block, list);
	list_move_tail(&blk->list, &lun->used_list);
	blk->state = NVM_BLK_ST_OPEN;

	lun->vlun.nr_free_blocks--;
	lun->vlun.nr_open_blocks++;

out:
	return blk;
}

static struct nvm_block *gennvm_get_blk(struct nvm_dev *dev,
				struct nvm_lun *vlun, unsigned long flags)
{
	struct nvm_block *blk;

	spin_lock(&vlun->lock);
	blk = gennvm_get_blk_unlocked(dev, vlun, flags);
	spin_unlock(&vlun->lock);
	return blk;
}

static void gennvm_put_blk_unlocked(struct nvm_dev *dev, struct nvm_block *blk)
{
	struct nvm_lun *vlun = blk->lun;
	struct gen_lun *lun = container_of(vlun, struct gen_lun, vlun);

	assert_spin_locked(&vlun->lock);

	if (blk->state & NVM_BLK_ST_OPEN) {
		list_move_tail(&blk->list, &lun->free_list);
		lun->vlun.nr_open_blocks--;
		lun->vlun.nr_free_blocks++;
		blk->state = NVM_BLK_ST_FREE;
	} else if (blk->state & NVM_BLK_ST_CLOSED) {
		list_move_tail(&blk->list, &lun->free_list);
		lun->vlun.nr_closed_blocks--;
		lun->vlun.nr_free_blocks++;
		blk->state = NVM_BLK_ST_FREE;
	} else if (blk->state & NVM_BLK_ST_BAD) {
		list_move_tail(&blk->list, &lun->bb_list);
		lun->vlun.nr_bad_blocks++;
		blk->state = NVM_BLK_ST_BAD;
	} else {
		WARN_ON_ONCE(1);
		pr_err("gennvm: erroneous block type (%lu -> %u)\n",
							blk->id, blk->state);
		list_move_tail(&blk->list, &lun->bb_list);
		lun->vlun.nr_bad_blocks++;
		blk->state = NVM_BLK_ST_BAD;
	}
}

static void gennvm_put_blk(struct nvm_dev *dev, struct nvm_block *blk)
{
	struct nvm_lun *vlun = blk->lun;

	spin_lock(&vlun->lock);
	gennvm_put_blk_unlocked(dev, blk);
	spin_unlock(&vlun->lock);
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
	blk->state = type;
}

/* mark block bad. It is expected the target recover from the error. */
static void gennvm_mark_blk_bad(struct nvm_dev *dev, struct nvm_rq *rqd)
{
	int i;

	if (!dev->ops->set_bb_tbl)
		return;

	if (dev->ops->set_bb_tbl(dev, rqd, 1))
		return;

	nvm_addr_to_generic_mode(dev, rqd);

	/* look up blocks and mark them as bad */
	if (rqd->nr_pages > 1)
		for (i = 0; i < rqd->nr_pages; i++)
			gennvm_blk_set_type(dev, &rqd->ppa_list[i],
						NVM_BLK_ST_BAD);
	else
		gennvm_blk_set_type(dev, &rqd->ppa_addr, NVM_BLK_ST_BAD);
}

static void gennvm_end_io(struct nvm_rq *rqd)
{
	struct nvm_tgt_instance *ins = rqd->ins;

	switch (rqd->error) {
	case NVM_RSP_SUCCESS:
	case NVM_RSP_ERR_EMPTYPAGE:
		break;
	case NVM_RSP_ERR_FAILWRITE:
		gennvm_mark_blk_bad(rqd->dev, rqd);
	}

	ins->tt->end_io(rqd);
}

static int gennvm_submit_io(struct nvm_dev *dev, struct nvm_rq *rqd)
{
	if (!dev->ops->submit_io)
		return -ENODEV;

	/* Convert address space */
	nvm_generic_to_addr_mode(dev, rqd);

	rqd->dev = dev;
	rqd->end_io = gennvm_end_io;
	return dev->ops->submit_io(dev, rqd);
}

static int gennvm_erase_blk(struct nvm_dev *dev, struct nvm_block *blk,
							unsigned long flags)
{
	struct ppa_addr addr = block_to_ppa(dev, blk);

	return nvm_erase_ppa(dev, &addr, 1);
}

static int gennvm_reserve_lun(struct nvm_dev *dev, int lunid)
{
	return test_and_set_bit(lunid, dev->lun_map);
}

static void gennvm_release_lun(struct nvm_dev *dev, int lunid)
{
	WARN_ON(!test_and_clear_bit(lunid, dev->lun_map));
}

static struct nvm_lun *gennvm_get_lun(struct nvm_dev *dev, int lunid)
{
	struct gen_nvm *gn = dev->mp;

	if (unlikely(lunid >= dev->nr_luns))
		return NULL;

	return &gn->luns[lunid].vlun;
}

static void gennvm_lun_info_print(struct nvm_dev *dev)
{
	struct gen_nvm *gn = dev->mp;
	struct gen_lun *lun;
	unsigned int i;


	gennvm_for_each_lun(gn, lun, i) {
		spin_lock(&lun->vlun.lock);

		pr_info("%s: lun%8u\t%u\t%u\t%u\t%u\n",
				dev->name, i,
				lun->vlun.nr_free_blocks,
				lun->vlun.nr_open_blocks,
				lun->vlun.nr_closed_blocks,
				lun->vlun.nr_bad_blocks);

		spin_unlock(&lun->vlun.lock);
	}
}

static struct nvmm_type gennvm = {
	.name			= "gennvm",
	.version		= {0, 1, 0},

	.register_mgr		= gennvm_register,
	.unregister_mgr		= gennvm_unregister,

	.get_blk_unlocked	= gennvm_get_blk_unlocked,
	.put_blk_unlocked	= gennvm_put_blk_unlocked,

	.get_blk		= gennvm_get_blk,
	.put_blk		= gennvm_put_blk,

	.submit_io		= gennvm_submit_io,
	.erase_blk		= gennvm_erase_blk,

	.get_lun		= gennvm_get_lun,
	.reserve_lun		= gennvm_reserve_lun,
	.release_lun		= gennvm_release_lun,
	.lun_info_print		= gennvm_lun_info_print,

	.get_area		= gennvm_get_area,
	.put_area		= gennvm_put_area,

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
