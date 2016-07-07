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
 * Implementation of a general nvm manager for Open-Channel SSDs.
 */

#include "gennvm.h"

static struct nvm_target *gen_find_target(struct gen_dev *gn, const char *name)
{
	struct nvm_target *tgt;

	list_for_each_entry(tgt, &gn->targets, list)
		if (!strcmp(name, tgt->disk->disk_name))
			return tgt;

	return NULL;
}

static const struct block_device_operations gen_fops = {
	.owner		= THIS_MODULE,
};

static int gen_create_tgt(struct nvm_dev *dev, struct nvm_ioctl_create *create)
{
	struct gen_dev *gn = dev->mp;
	struct nvm_ioctl_create_simple *s = &create->conf.s;
	struct request_queue *tqueue;
	struct gendisk *tdisk;
	struct nvm_tgt_type *tt;
	struct nvm_target *t;
	void *targetdata;

	tt = nvm_find_target_type(create->tgttype, 1);
	if (!tt) {
		pr_err("nvm: target type %s not found\n", create->tgttype);
		return -EINVAL;
	}

	mutex_lock(&gn->lock);
	t = gen_find_target(gn, create->tgtname);
	if (t) {
		pr_err("nvm: target name already exists.\n");
		mutex_unlock(&gn->lock);
		return -EINVAL;
	}
	mutex_unlock(&gn->lock);

	t = kmalloc(sizeof(struct nvm_target), GFP_KERNEL);
	if (!t)
		return -ENOMEM;

	tqueue = blk_alloc_queue_node(GFP_KERNEL, dev->q->node);
	if (!tqueue)
		goto err_t;
	blk_queue_make_request(tqueue, tt->make_rq);

	tdisk = alloc_disk(0);
	if (!tdisk)
		goto err_queue;

	sprintf(tdisk->disk_name, "%s", create->tgtname);
	tdisk->flags = GENHD_FL_EXT_DEVT;
	tdisk->major = 0;
	tdisk->first_minor = 0;
	tdisk->fops = &gen_fops;
	tdisk->queue = tqueue;

	targetdata = tt->init(dev, tdisk, s->lun_begin, s->lun_end);
	if (IS_ERR(targetdata))
		goto err_init;

	tdisk->private_data = targetdata;
	tqueue->queuedata = targetdata;

	blk_queue_max_hw_sectors(tqueue, 8 * dev->ops->max_phys_sect);

	set_capacity(tdisk, tt->capacity(targetdata));
	add_disk(tdisk);

	t->type = tt;
	t->disk = tdisk;
	t->dev = dev;

	mutex_lock(&gn->lock);
	list_add_tail(&t->list, &gn->targets);
	mutex_unlock(&gn->lock);

	return 0;
err_init:
	put_disk(tdisk);
err_queue:
	blk_cleanup_queue(tqueue);
err_t:
	kfree(t);
	return -ENOMEM;
}

static void __gen_remove_target(struct nvm_target *t)
{
	struct nvm_tgt_type *tt = t->type;
	struct gendisk *tdisk = t->disk;
	struct request_queue *q = tdisk->queue;

	del_gendisk(tdisk);
	blk_cleanup_queue(q);

	if (tt->exit)
		tt->exit(tdisk->private_data);

	put_disk(tdisk);

	list_del(&t->list);
	kfree(t);
}

/**
 * gen_remove_tgt - Removes a target from the media manager
 * @dev:	device
 * @remove:	ioctl structure with target name to remove.
 *
 * Returns:
 * 0: on success
 * 1: on not found
 * <0: on error
 */
static int gen_remove_tgt(struct nvm_dev *dev, struct nvm_ioctl_remove *remove)
{
	struct gen_dev *gn = dev->mp;
	struct nvm_target *t;

	if (!gn)
		return 1;

	mutex_lock(&gn->lock);
	t = gen_find_target(gn, remove->tgtname);
	if (!t) {
		mutex_unlock(&gn->lock);
		return 1;
	}
	__gen_remove_target(t);
	mutex_unlock(&gn->lock);

	return 0;
}

static int gen_get_area(struct nvm_dev *dev, sector_t *lba, sector_t len)
{
	struct gen_dev *gn = dev->mp;
	struct gen_area *area, *prev, *next;
	sector_t begin = 0;
	sector_t max_sectors = (dev->sec_size * dev->total_secs) >> 9;

	if (len > max_sectors)
		return -EINVAL;

	area = kmalloc(sizeof(struct gen_area), GFP_KERNEL);
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

static void gen_put_area(struct nvm_dev *dev, sector_t begin)
{
	struct gen_dev *gn = dev->mp;
	struct gen_area *area;

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

static void gen_blocks_free(struct nvm_dev *dev)
{
	struct gen_dev *gn = dev->mp;
	struct gen_lun *lun;
	int i;

	gen_for_each_lun(gn, lun, i) {
		if (!lun->vlun.blocks)
			break;
		vfree(lun->vlun.blocks);
	}
}

static void gen_luns_free(struct nvm_dev *dev)
{
	struct gen_dev *gn = dev->mp;

	kfree(gn->luns);
}

static int gen_luns_init(struct nvm_dev *dev, struct gen_dev *gn)
{
	struct gen_lun *lun;
	int i;

	gn->luns = kcalloc(dev->nr_luns, sizeof(struct gen_lun), GFP_KERNEL);
	if (!gn->luns)
		return -ENOMEM;

	gen_for_each_lun(gn, lun, i) {
		spin_lock_init(&lun->vlun.lock);
		INIT_LIST_HEAD(&lun->free_list);
		INIT_LIST_HEAD(&lun->used_list);
		INIT_LIST_HEAD(&lun->bb_list);

		lun->reserved_blocks = 2; /* for GC only */
		lun->vlun.id = i;
		lun->vlun.lun_id = i % dev->luns_per_chnl;
		lun->vlun.chnl_id = i / dev->luns_per_chnl;
		lun->vlun.nr_free_blocks = dev->blks_per_lun;
	}
	return 0;
}

static int gen_block_bb(struct gen_dev *gn, struct ppa_addr ppa,
							u8 *blks, int nr_blks)
{
	struct nvm_dev *dev = gn->dev;
	struct gen_lun *lun;
	struct nvm_block *blk;
	int i;

	nr_blks = nvm_bb_tbl_fold(dev, blks, nr_blks);
	if (nr_blks < 0)
		return nr_blks;

	lun = &gn->luns[(dev->luns_per_chnl * ppa.g.ch) + ppa.g.lun];

	for (i = 0; i < nr_blks; i++) {
		if (blks[i] == 0)
			continue;

		blk = &lun->vlun.blocks[i];
		list_move_tail(&blk->list, &lun->bb_list);
		lun->vlun.nr_free_blocks--;
	}

	return 0;
}

static int gen_block_map(u64 slba, u32 nlb, __le64 *entries, void *private)
{
	struct nvm_dev *dev = private;
	struct gen_dev *gn = dev->mp;
	u64 elba = slba + nlb;
	struct gen_lun *lun;
	struct nvm_block *blk;
	u64 i;
	int lun_id;

	if (unlikely(elba > dev->total_secs)) {
		pr_err("gen: L2P data from device is out of bounds!\n");
		return -EINVAL;
	}

	for (i = 0; i < nlb; i++) {
		u64 pba = le64_to_cpu(entries[i]);

		if (unlikely(pba >= dev->total_secs && pba != U64_MAX)) {
			pr_err("gen: L2P data entry is out of bounds!\n");
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
			blk->state = NVM_BLK_ST_TGT;
			lun->vlun.nr_free_blocks--;
		}
	}

	return 0;
}

static int gen_blocks_init(struct nvm_dev *dev, struct gen_dev *gn)
{
	struct gen_lun *lun;
	struct nvm_block *block;
	sector_t lun_iter, blk_iter, cur_block_id = 0;
	int ret, nr_blks;
	u8 *blks;

	nr_blks = dev->blks_per_lun * dev->plane_mode;
	blks = kmalloc(nr_blks, GFP_KERNEL);
	if (!blks)
		return -ENOMEM;

	gen_for_each_lun(gn, lun, lun_iter) {
		lun->vlun.blocks = vzalloc(sizeof(struct nvm_block) *
							dev->blks_per_lun);
		if (!lun->vlun.blocks) {
			kfree(blks);
			return -ENOMEM;
		}

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
			ppa.g.lun = lun->vlun.lun_id;

			ret = nvm_get_bb_tbl(dev, ppa, blks);
			if (ret)
				pr_err("gen: could not get BB table\n");

			ret = gen_block_bb(gn, ppa, blks, nr_blks);
			if (ret)
				pr_err("gen: BB table map failed\n");
		}
	}

	if ((dev->identity.dom & NVM_RSP_L2P) && dev->ops->get_l2p_tbl) {
		ret = dev->ops->get_l2p_tbl(dev, 0, dev->total_secs,
							gen_block_map, dev);
		if (ret) {
			pr_err("gen: could not read L2P table.\n");
			pr_warn("gen: default block initialization");
		}
	}

	kfree(blks);
	return 0;
}

static void gen_free(struct nvm_dev *dev)
{
	gen_blocks_free(dev);
	gen_luns_free(dev);
	kfree(dev->mp);
	dev->mp = NULL;
}

static int gen_register(struct nvm_dev *dev)
{
	struct gen_dev *gn;
	int ret;

	if (!try_module_get(THIS_MODULE))
		return -ENODEV;

	gn = kzalloc(sizeof(struct gen_dev), GFP_KERNEL);
	if (!gn)
		return -ENOMEM;

	gn->dev = dev;
	gn->nr_luns = dev->nr_luns;
	INIT_LIST_HEAD(&gn->area_list);
	mutex_init(&gn->lock);
	INIT_LIST_HEAD(&gn->targets);
	dev->mp = gn;

	ret = gen_luns_init(dev, gn);
	if (ret) {
		pr_err("gen: could not initialize luns\n");
		goto err;
	}

	ret = gen_blocks_init(dev, gn);
	if (ret) {
		pr_err("gen: could not initialize blocks\n");
		goto err;
	}

	return 1;
err:
	gen_free(dev);
	module_put(THIS_MODULE);
	return ret;
}

static void gen_unregister(struct nvm_dev *dev)
{
	struct gen_dev *gn = dev->mp;
	struct nvm_target *t, *tmp;

	mutex_lock(&gn->lock);
	list_for_each_entry_safe(t, tmp, &gn->targets, list) {
		if (t->dev != dev)
			continue;
		__gen_remove_target(t);
	}
	mutex_unlock(&gn->lock);

	gen_free(dev);
	module_put(THIS_MODULE);
}

static struct nvm_block *gen_get_blk(struct nvm_dev *dev,
				struct nvm_lun *vlun, unsigned long flags)
{
	struct gen_lun *lun = container_of(vlun, struct gen_lun, vlun);
	struct nvm_block *blk = NULL;
	int is_gc = flags & NVM_IOTYPE_GC;

	spin_lock(&vlun->lock);
	if (list_empty(&lun->free_list)) {
		pr_err_ratelimited("gen: lun %u have no free pages available",
								lun->vlun.id);
		goto out;
	}

	if (!is_gc && lun->vlun.nr_free_blocks < lun->reserved_blocks)
		goto out;

	blk = list_first_entry(&lun->free_list, struct nvm_block, list);

	list_move_tail(&blk->list, &lun->used_list);
	blk->state = NVM_BLK_ST_TGT;
	lun->vlun.nr_free_blocks--;
out:
	spin_unlock(&vlun->lock);
	return blk;
}

static void gen_put_blk(struct nvm_dev *dev, struct nvm_block *blk)
{
	struct nvm_lun *vlun = blk->lun;
	struct gen_lun *lun = container_of(vlun, struct gen_lun, vlun);

	spin_lock(&vlun->lock);
	if (blk->state & NVM_BLK_ST_TGT) {
		list_move_tail(&blk->list, &lun->free_list);
		lun->vlun.nr_free_blocks++;
		blk->state = NVM_BLK_ST_FREE;
	} else if (blk->state & NVM_BLK_ST_BAD) {
		list_move_tail(&blk->list, &lun->bb_list);
		blk->state = NVM_BLK_ST_BAD;
	} else {
		WARN_ON_ONCE(1);
		pr_err("gen: erroneous block type (%lu -> %u)\n",
							blk->id, blk->state);
		list_move_tail(&blk->list, &lun->bb_list);
	}
	spin_unlock(&vlun->lock);
}

static void gen_mark_blk(struct nvm_dev *dev, struct ppa_addr ppa, int type)
{
	struct gen_dev *gn = dev->mp;
	struct gen_lun *lun;
	struct nvm_block *blk;

	pr_debug("gen: ppa  (ch: %u lun: %u blk: %u pg: %u) -> %u\n",
			ppa.g.ch, ppa.g.lun, ppa.g.blk, ppa.g.pg, type);

	if (unlikely(ppa.g.ch > dev->nr_chnls ||
					ppa.g.lun > dev->luns_per_chnl ||
					ppa.g.blk > dev->blks_per_lun)) {
		WARN_ON_ONCE(1);
		pr_err("gen: ppa broken (ch: %u > %u lun: %u > %u blk: %u > %u",
				ppa.g.ch, dev->nr_chnls,
				ppa.g.lun, dev->luns_per_chnl,
				ppa.g.blk, dev->blks_per_lun);
		return;
	}

	lun = &gn->luns[(dev->luns_per_chnl * ppa.g.ch) + ppa.g.lun];
	blk = &lun->vlun.blocks[ppa.g.blk];

	/* will be moved to bb list on put_blk from target */
	blk->state = type;
}

/*
 * mark block bad in gen. It is expected that the target recovers separately
 */
static void gen_mark_blk_bad(struct nvm_dev *dev, struct nvm_rq *rqd)
{
	int bit = -1;
	int max_secs = dev->ops->max_phys_sect;
	void *comp_bits = &rqd->ppa_status;

	nvm_addr_to_generic_mode(dev, rqd);

	/* look up blocks and mark them as bad */
	if (rqd->nr_ppas == 1) {
		gen_mark_blk(dev, rqd->ppa_addr, NVM_BLK_ST_BAD);
		return;
	}

	while ((bit = find_next_bit(comp_bits, max_secs, bit + 1)) < max_secs)
		gen_mark_blk(dev, rqd->ppa_list[bit], NVM_BLK_ST_BAD);
}

static void gen_end_io(struct nvm_rq *rqd)
{
	struct nvm_tgt_instance *ins = rqd->ins;

	if (rqd->error == NVM_RSP_ERR_FAILWRITE)
		gen_mark_blk_bad(rqd->dev, rqd);

	ins->tt->end_io(rqd);
}

static int gen_submit_io(struct nvm_dev *dev, struct nvm_rq *rqd)
{
	if (!dev->ops->submit_io)
		return -ENODEV;

	/* Convert address space */
	nvm_generic_to_addr_mode(dev, rqd);

	rqd->dev = dev;
	rqd->end_io = gen_end_io;
	return dev->ops->submit_io(dev, rqd);
}

static int gen_erase_blk(struct nvm_dev *dev, struct nvm_block *blk,
							unsigned long flags)
{
	struct ppa_addr addr = block_to_ppa(dev, blk);

	return nvm_erase_ppa(dev, &addr, 1);
}

static int gen_reserve_lun(struct nvm_dev *dev, int lunid)
{
	return test_and_set_bit(lunid, dev->lun_map);
}

static void gen_release_lun(struct nvm_dev *dev, int lunid)
{
	WARN_ON(!test_and_clear_bit(lunid, dev->lun_map));
}

static struct nvm_lun *gen_get_lun(struct nvm_dev *dev, int lunid)
{
	struct gen_dev *gn = dev->mp;

	if (unlikely(lunid >= dev->nr_luns))
		return NULL;

	return &gn->luns[lunid].vlun;
}

static void gen_lun_info_print(struct nvm_dev *dev)
{
	struct gen_dev *gn = dev->mp;
	struct gen_lun *lun;
	unsigned int i;


	gen_for_each_lun(gn, lun, i) {
		spin_lock(&lun->vlun.lock);

		pr_info("%s: lun%8u\t%u\n", dev->name, i,
						lun->vlun.nr_free_blocks);

		spin_unlock(&lun->vlun.lock);
	}
}

static struct nvmm_type gen = {
	.name			= "gennvm",
	.version		= {0, 1, 0},

	.register_mgr		= gen_register,
	.unregister_mgr		= gen_unregister,

	.create_tgt		= gen_create_tgt,
	.remove_tgt		= gen_remove_tgt,

	.get_blk		= gen_get_blk,
	.put_blk		= gen_put_blk,

	.submit_io		= gen_submit_io,
	.erase_blk		= gen_erase_blk,

	.mark_blk		= gen_mark_blk,

	.get_lun		= gen_get_lun,
	.reserve_lun		= gen_reserve_lun,
	.release_lun		= gen_release_lun,
	.lun_info_print		= gen_lun_info_print,

	.get_area		= gen_get_area,
	.put_area		= gen_put_area,

};

static int __init gen_module_init(void)
{
	return nvm_register_mgr(&gen);
}

static void gen_module_exit(void)
{
	nvm_unregister_mgr(&gen);
}

module_init(gen_module_init);
module_exit(gen_module_exit);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("General media manager for Open-Channel SSDs");
