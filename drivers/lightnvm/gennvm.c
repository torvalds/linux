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

static int gen_reserve_luns(struct nvm_dev *dev, struct nvm_target *t,
			    int lun_begin, int lun_end)
{
	int i;

	for (i = lun_begin; i <= lun_end; i++) {
		if (test_and_set_bit(i, dev->lun_map)) {
			pr_err("nvm: lun %d already allocated\n", i);
			goto err;
		}
	}

	return 0;

err:
	while (--i > lun_begin)
		clear_bit(i, dev->lun_map);

	return -EBUSY;
}

static void gen_release_luns_err(struct nvm_dev *dev, int lun_begin,
				 int lun_end)
{
	int i;

	for (i = lun_begin; i <= lun_end; i++)
		WARN_ON(!test_and_clear_bit(i, dev->lun_map));
}

static void gen_remove_tgt_dev(struct nvm_tgt_dev *tgt_dev)
{
	struct nvm_dev *dev = tgt_dev->parent;
	struct gen_dev_map *dev_map = tgt_dev->map;
	int i, j;

	for (i = 0; i < dev_map->nr_chnls; i++) {
		struct gen_ch_map *ch_map = &dev_map->chnls[i];
		int *lun_offs = ch_map->lun_offs;
		int ch = i + ch_map->ch_off;

		for (j = 0; j < ch_map->nr_luns; j++) {
			int lun = j + lun_offs[j];
			int lunid = (ch * dev->geo.luns_per_chnl) + lun;

			WARN_ON(!test_and_clear_bit(lunid, dev->lun_map));
		}

		kfree(ch_map->lun_offs);
	}

	kfree(dev_map->chnls);
	kfree(dev_map);
	kfree(tgt_dev->luns);
	kfree(tgt_dev);
}

static struct nvm_tgt_dev *gen_create_tgt_dev(struct nvm_dev *dev,
					      int lun_begin, int lun_end)
{
	struct nvm_tgt_dev *tgt_dev = NULL;
	struct gen_dev_map *dev_rmap = dev->rmap;
	struct gen_dev_map *dev_map;
	struct ppa_addr *luns;
	int nr_luns = lun_end - lun_begin + 1;
	int luns_left = nr_luns;
	int nr_chnls = nr_luns / dev->geo.luns_per_chnl;
	int nr_chnls_mod = nr_luns % dev->geo.luns_per_chnl;
	int bch = lun_begin / dev->geo.luns_per_chnl;
	int blun = lun_begin % dev->geo.luns_per_chnl;
	int lunid = 0;
	int lun_balanced = 1;
	int prev_nr_luns;
	int i, j;

	nr_chnls = nr_luns / dev->geo.luns_per_chnl;
	nr_chnls = (nr_chnls_mod == 0) ? nr_chnls : nr_chnls + 1;

	dev_map = kmalloc(sizeof(struct gen_dev_map), GFP_KERNEL);
	if (!dev_map)
		goto err_dev;

	dev_map->chnls = kcalloc(nr_chnls, sizeof(struct gen_ch_map),
								GFP_KERNEL);
	if (!dev_map->chnls)
		goto err_chnls;

	luns = kcalloc(nr_luns, sizeof(struct ppa_addr), GFP_KERNEL);
	if (!luns)
		goto err_luns;

	prev_nr_luns = (luns_left > dev->geo.luns_per_chnl) ?
					dev->geo.luns_per_chnl : luns_left;
	for (i = 0; i < nr_chnls; i++) {
		struct gen_ch_map *ch_rmap = &dev_rmap->chnls[i + bch];
		int *lun_roffs = ch_rmap->lun_offs;
		struct gen_ch_map *ch_map = &dev_map->chnls[i];
		int *lun_offs;
		int luns_in_chnl = (luns_left > dev->geo.luns_per_chnl) ?
					dev->geo.luns_per_chnl : luns_left;

		if (lun_balanced && prev_nr_luns != luns_in_chnl)
			lun_balanced = 0;

		ch_map->ch_off = ch_rmap->ch_off = bch;
		ch_map->nr_luns = luns_in_chnl;

		lun_offs = kcalloc(luns_in_chnl, sizeof(int), GFP_KERNEL);
		if (!lun_offs)
			goto err_ch;

		for (j = 0; j < luns_in_chnl; j++) {
			luns[lunid].ppa = 0;
			luns[lunid].g.ch = i;
			luns[lunid++].g.lun = j;

			lun_offs[j] = blun;
			lun_roffs[j + blun] = blun;
		}

		ch_map->lun_offs = lun_offs;

		/* when starting a new channel, lun offset is reset */
		blun = 0;
		luns_left -= luns_in_chnl;
	}

	dev_map->nr_chnls = nr_chnls;

	tgt_dev = kmalloc(sizeof(struct nvm_tgt_dev), GFP_KERNEL);
	if (!tgt_dev)
		goto err_ch;

	memcpy(&tgt_dev->geo, &dev->geo, sizeof(struct nvm_geo));
	/* Target device only owns a portion of the physical device */
	tgt_dev->geo.nr_chnls = nr_chnls;
	tgt_dev->geo.nr_luns = nr_luns;
	tgt_dev->geo.luns_per_chnl = (lun_balanced) ? prev_nr_luns : -1;
	tgt_dev->total_secs = nr_luns * tgt_dev->geo.sec_per_lun;
	tgt_dev->q = dev->q;
	tgt_dev->map = dev_map;
	tgt_dev->luns = luns;
	memcpy(&tgt_dev->identity, &dev->identity, sizeof(struct nvm_id));

	tgt_dev->parent = dev;

	return tgt_dev;
err_ch:
	while (--i > 0)
		kfree(dev_map->chnls[i].lun_offs);
	kfree(luns);
err_luns:
	kfree(dev_map->chnls);
err_chnls:
	kfree(dev_map);
err_dev:
	return tgt_dev;
}

static int gen_create_tgt(struct nvm_dev *dev, struct nvm_ioctl_create *create)
{
	struct gen_dev *gn = dev->mp;
	struct nvm_ioctl_create_simple *s = &create->conf.s;
	struct request_queue *tqueue;
	struct gendisk *tdisk;
	struct nvm_tgt_type *tt;
	struct nvm_target *t;
	struct nvm_tgt_dev *tgt_dev;
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

	if (gen_reserve_luns(dev, t, s->lun_begin, s->lun_end))
		goto err_t;

	tgt_dev = gen_create_tgt_dev(dev, s->lun_begin, s->lun_end);
	if (!tgt_dev) {
		pr_err("nvm: could not create target device\n");
		goto err_reserve;
	}

	tqueue = blk_alloc_queue_node(GFP_KERNEL, dev->q->node);
	if (!tqueue)
		goto err_dev;
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

	targetdata = tt->init(tgt_dev, tdisk);
	if (IS_ERR(targetdata))
		goto err_init;

	tdisk->private_data = targetdata;
	tqueue->queuedata = targetdata;

	blk_queue_max_hw_sectors(tqueue, 8 * dev->ops->max_phys_sect);

	set_capacity(tdisk, tt->capacity(targetdata));
	add_disk(tdisk);

	t->type = tt;
	t->disk = tdisk;
	t->dev = tgt_dev;

	mutex_lock(&gn->lock);
	list_add_tail(&t->list, &gn->targets);
	mutex_unlock(&gn->lock);

	return 0;
err_init:
	put_disk(tdisk);
err_queue:
	blk_cleanup_queue(tqueue);
err_dev:
	kfree(tgt_dev);
err_reserve:
	gen_release_luns_err(dev, s->lun_begin, s->lun_end);
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

	gen_remove_tgt_dev(t->dev);
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
	struct nvm_geo *geo = &dev->geo;
	struct gen_dev *gn = dev->mp;
	struct gen_area *area, *prev, *next;
	sector_t begin = 0;
	sector_t max_sectors = (geo->sec_size * dev->total_secs) >> 9;

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

static void gen_free(struct nvm_dev *dev)
{
	kfree(dev->mp);
	kfree(dev->rmap);
	dev->mp = NULL;
}

static int gen_register(struct nvm_dev *dev)
{
	struct gen_dev *gn;
	struct gen_dev_map *dev_rmap;
	int i, j;

	if (!try_module_get(THIS_MODULE))
		return -ENODEV;

	gn = kzalloc(sizeof(struct gen_dev), GFP_KERNEL);
	if (!gn)
		goto err_gn;

	dev_rmap = kmalloc(sizeof(struct gen_dev_map), GFP_KERNEL);
	if (!dev_rmap)
		goto err_rmap;

	dev_rmap->chnls = kcalloc(dev->geo.nr_chnls, sizeof(struct gen_ch_map),
								GFP_KERNEL);
	if (!dev_rmap->chnls)
		goto err_chnls;

	for (i = 0; i < dev->geo.nr_chnls; i++) {
		struct gen_ch_map *ch_rmap;
		int *lun_roffs;
		int luns_in_chnl = dev->geo.luns_per_chnl;

		ch_rmap = &dev_rmap->chnls[i];

		ch_rmap->ch_off = -1;
		ch_rmap->nr_luns = luns_in_chnl;

		lun_roffs = kcalloc(luns_in_chnl, sizeof(int), GFP_KERNEL);
		if (!lun_roffs)
			goto err_ch;

		for (j = 0; j < luns_in_chnl; j++)
			lun_roffs[j] = -1;

		ch_rmap->lun_offs = lun_roffs;
	}

	gn->dev = dev;
	gn->nr_luns = dev->geo.nr_luns;
	INIT_LIST_HEAD(&gn->area_list);
	mutex_init(&gn->lock);
	INIT_LIST_HEAD(&gn->targets);
	dev->mp = gn;
	dev->rmap = dev_rmap;

	return 1;
err_ch:
	while (--i >= 0)
		kfree(dev_rmap->chnls[i].lun_offs);
err_chnls:
	kfree(dev_rmap);
err_rmap:
	gen_free(dev);
err_gn:
	module_put(THIS_MODULE);
	return -ENOMEM;
}

static void gen_unregister(struct nvm_dev *dev)
{
	struct gen_dev *gn = dev->mp;
	struct nvm_target *t, *tmp;

	mutex_lock(&gn->lock);
	list_for_each_entry_safe(t, tmp, &gn->targets, list) {
		if (t->dev->parent != dev)
			continue;
		__gen_remove_target(t);
	}
	mutex_unlock(&gn->lock);

	gen_free(dev);
	module_put(THIS_MODULE);
}

static int gen_map_to_dev(struct nvm_tgt_dev *tgt_dev, struct ppa_addr *p)
{
	struct gen_dev_map *dev_map = tgt_dev->map;
	struct gen_ch_map *ch_map = &dev_map->chnls[p->g.ch];
	int lun_off = ch_map->lun_offs[p->g.lun];
	struct nvm_dev *dev = tgt_dev->parent;
	struct gen_dev_map *dev_rmap = dev->rmap;
	struct gen_ch_map *ch_rmap;
	int lun_roff;

	p->g.ch += ch_map->ch_off;
	p->g.lun += lun_off;

	ch_rmap = &dev_rmap->chnls[p->g.ch];
	lun_roff = ch_rmap->lun_offs[p->g.lun];

	if (unlikely(ch_rmap->ch_off < 0 || lun_roff < 0)) {
		pr_err("nvm: corrupted device partition table\n");
		return -EINVAL;
	}

	return 0;
}

static int gen_map_to_tgt(struct nvm_tgt_dev *tgt_dev, struct ppa_addr *p)
{
	struct nvm_dev *dev = tgt_dev->parent;
	struct gen_dev_map *dev_rmap = dev->rmap;
	struct gen_ch_map *ch_rmap = &dev_rmap->chnls[p->g.ch];
	int lun_roff = ch_rmap->lun_offs[p->g.lun];

	p->g.ch -= ch_rmap->ch_off;
	p->g.lun -= lun_roff;

	return 0;
}

static int gen_trans_rq(struct nvm_tgt_dev *tgt_dev, struct nvm_rq *rqd,
			int flag)
{
	gen_trans_fn *f;
	int i;
	int ret = 0;

	f = (flag == TRANS_TGT_TO_DEV) ? gen_map_to_dev : gen_map_to_tgt;

	if (rqd->nr_ppas == 1)
		return f(tgt_dev, &rqd->ppa_addr);

	for (i = 0; i < rqd->nr_ppas; i++) {
		ret = f(tgt_dev, &rqd->ppa_list[i]);
		if (ret)
			goto out;
	}

out:
	return ret;
}

static void gen_end_io(struct nvm_rq *rqd)
{
	struct nvm_tgt_dev *tgt_dev = rqd->dev;
	struct nvm_tgt_instance *ins = rqd->ins;

	/* Convert address space */
	if (tgt_dev)
		gen_trans_rq(tgt_dev, rqd, TRANS_DEV_TO_TGT);

	ins->tt->end_io(rqd);
}

static int gen_submit_io(struct nvm_tgt_dev *tgt_dev, struct nvm_rq *rqd)
{
	struct nvm_dev *dev = tgt_dev->parent;

	if (!dev->ops->submit_io)
		return -ENODEV;

	/* Convert address space */
	gen_trans_rq(tgt_dev, rqd, TRANS_TGT_TO_DEV);
	nvm_generic_to_addr_mode(dev, rqd);

	rqd->dev = tgt_dev;
	rqd->end_io = gen_end_io;
	return dev->ops->submit_io(dev, rqd);
}

static int gen_erase_blk(struct nvm_tgt_dev *tgt_dev, struct ppa_addr *p,
			 int flags)
{
	/* Convert address space */
	gen_map_to_dev(tgt_dev, p);

	return nvm_erase_ppa(tgt_dev->parent, p, 1, flags);
}

static struct ppa_addr gen_trans_ppa(struct nvm_tgt_dev *tgt_dev,
				     struct ppa_addr p, int direction)
{
	gen_trans_fn *f;
	struct ppa_addr ppa = p;

	f = (direction == TRANS_TGT_TO_DEV) ? gen_map_to_dev : gen_map_to_tgt;
	f(tgt_dev, &ppa);

	return ppa;
}

static void gen_part_to_tgt(struct nvm_dev *dev, sector_t *entries,
			       int len)
{
	struct nvm_geo *geo = &dev->geo;
	struct gen_dev_map *dev_rmap = dev->rmap;
	u64 i;

	for (i = 0; i < len; i++) {
		struct gen_ch_map *ch_rmap;
		int *lun_roffs;
		struct ppa_addr gaddr;
		u64 pba = le64_to_cpu(entries[i]);
		int off;
		u64 diff;

		if (!pba)
			continue;

		gaddr = linear_to_generic_addr(geo, pba);
		ch_rmap = &dev_rmap->chnls[gaddr.g.ch];
		lun_roffs = ch_rmap->lun_offs;

		off = gaddr.g.ch * geo->luns_per_chnl + gaddr.g.lun;

		diff = ((ch_rmap->ch_off * geo->luns_per_chnl) +
				(lun_roffs[gaddr.g.lun])) * geo->sec_per_lun;

		entries[i] -= cpu_to_le64(diff);
	}
}

static struct nvmm_type gen = {
	.name			= "gennvm",
	.version		= {0, 1, 0},

	.register_mgr		= gen_register,
	.unregister_mgr		= gen_unregister,

	.create_tgt		= gen_create_tgt,
	.remove_tgt		= gen_remove_tgt,

	.submit_io		= gen_submit_io,
	.erase_blk		= gen_erase_blk,

	.get_area		= gen_get_area,
	.put_area		= gen_put_area,

	.trans_ppa		= gen_trans_ppa,
	.part_to_tgt		= gen_part_to_tgt,
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
