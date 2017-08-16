/*
 * Copyright (C) 2015 IT University of Copenhagen. All rights reserved.
 * Initial release: Matias Bjorling <m@bjorling.me>
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
 */

#include <linux/list.h>
#include <linux/types.h>
#include <linux/sem.h>
#include <linux/bitmap.h>
#include <linux/moduleparam.h>
#include <linux/miscdevice.h>
#include <linux/lightnvm.h>
#include <linux/sched/sysctl.h>

static LIST_HEAD(nvm_tgt_types);
static DECLARE_RWSEM(nvm_tgtt_lock);
static LIST_HEAD(nvm_devices);
static DECLARE_RWSEM(nvm_lock);

/* Map between virtual and physical channel and lun */
struct nvm_ch_map {
	int ch_off;
	int nr_luns;
	int *lun_offs;
};

struct nvm_dev_map {
	struct nvm_ch_map *chnls;
	int nr_chnls;
};

struct nvm_area {
	struct list_head list;
	sector_t begin;
	sector_t end;	/* end is excluded */
};

static struct nvm_target *nvm_find_target(struct nvm_dev *dev, const char *name)
{
	struct nvm_target *tgt;

	list_for_each_entry(tgt, &dev->targets, list)
		if (!strcmp(name, tgt->disk->disk_name))
			return tgt;

	return NULL;
}

static int nvm_reserve_luns(struct nvm_dev *dev, int lun_begin, int lun_end)
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
	while (--i >= lun_begin)
		clear_bit(i, dev->lun_map);

	return -EBUSY;
}

static void nvm_release_luns_err(struct nvm_dev *dev, int lun_begin,
				 int lun_end)
{
	int i;

	for (i = lun_begin; i <= lun_end; i++)
		WARN_ON(!test_and_clear_bit(i, dev->lun_map));
}

static void nvm_remove_tgt_dev(struct nvm_tgt_dev *tgt_dev, int clear)
{
	struct nvm_dev *dev = tgt_dev->parent;
	struct nvm_dev_map *dev_map = tgt_dev->map;
	int i, j;

	for (i = 0; i < dev_map->nr_chnls; i++) {
		struct nvm_ch_map *ch_map = &dev_map->chnls[i];
		int *lun_offs = ch_map->lun_offs;
		int ch = i + ch_map->ch_off;

		if (clear) {
			for (j = 0; j < ch_map->nr_luns; j++) {
				int lun = j + lun_offs[j];
				int lunid = (ch * dev->geo.luns_per_chnl) + lun;

				WARN_ON(!test_and_clear_bit(lunid,
							dev->lun_map));
			}
		}

		kfree(ch_map->lun_offs);
	}

	kfree(dev_map->chnls);
	kfree(dev_map);

	kfree(tgt_dev->luns);
	kfree(tgt_dev);
}

static struct nvm_tgt_dev *nvm_create_tgt_dev(struct nvm_dev *dev,
					      int lun_begin, int lun_end)
{
	struct nvm_tgt_dev *tgt_dev = NULL;
	struct nvm_dev_map *dev_rmap = dev->rmap;
	struct nvm_dev_map *dev_map;
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

	dev_map = kmalloc(sizeof(struct nvm_dev_map), GFP_KERNEL);
	if (!dev_map)
		goto err_dev;

	dev_map->chnls = kcalloc(nr_chnls, sizeof(struct nvm_ch_map),
								GFP_KERNEL);
	if (!dev_map->chnls)
		goto err_chnls;

	luns = kcalloc(nr_luns, sizeof(struct ppa_addr), GFP_KERNEL);
	if (!luns)
		goto err_luns;

	prev_nr_luns = (luns_left > dev->geo.luns_per_chnl) ?
					dev->geo.luns_per_chnl : luns_left;
	for (i = 0; i < nr_chnls; i++) {
		struct nvm_ch_map *ch_rmap = &dev_rmap->chnls[i + bch];
		int *lun_roffs = ch_rmap->lun_offs;
		struct nvm_ch_map *ch_map = &dev_map->chnls[i];
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
	while (--i >= 0)
		kfree(dev_map->chnls[i].lun_offs);
	kfree(luns);
err_luns:
	kfree(dev_map->chnls);
err_chnls:
	kfree(dev_map);
err_dev:
	return tgt_dev;
}

static const struct block_device_operations nvm_fops = {
	.owner		= THIS_MODULE,
};

static int nvm_create_tgt(struct nvm_dev *dev, struct nvm_ioctl_create *create)
{
	struct nvm_ioctl_create_simple *s = &create->conf.s;
	struct request_queue *tqueue;
	struct gendisk *tdisk;
	struct nvm_tgt_type *tt;
	struct nvm_target *t;
	struct nvm_tgt_dev *tgt_dev;
	void *targetdata;
	int ret;

	tt = nvm_find_target_type(create->tgttype, 1);
	if (!tt) {
		pr_err("nvm: target type %s not found\n", create->tgttype);
		return -EINVAL;
	}

	mutex_lock(&dev->mlock);
	t = nvm_find_target(dev, create->tgtname);
	if (t) {
		pr_err("nvm: target name already exists.\n");
		mutex_unlock(&dev->mlock);
		return -EINVAL;
	}
	mutex_unlock(&dev->mlock);

	ret = nvm_reserve_luns(dev, s->lun_begin, s->lun_end);
	if (ret)
		return ret;

	t = kmalloc(sizeof(struct nvm_target), GFP_KERNEL);
	if (!t) {
		ret = -ENOMEM;
		goto err_reserve;
	}

	tgt_dev = nvm_create_tgt_dev(dev, s->lun_begin, s->lun_end);
	if (!tgt_dev) {
		pr_err("nvm: could not create target device\n");
		ret = -ENOMEM;
		goto err_t;
	}

	tdisk = alloc_disk(0);
	if (!tdisk) {
		ret = -ENOMEM;
		goto err_dev;
	}

	tqueue = blk_alloc_queue_node(GFP_KERNEL, dev->q->node);
	if (!tqueue) {
		ret = -ENOMEM;
		goto err_disk;
	}
	blk_queue_make_request(tqueue, tt->make_rq);

	strlcpy(tdisk->disk_name, create->tgtname, sizeof(tdisk->disk_name));
	tdisk->flags = GENHD_FL_EXT_DEVT;
	tdisk->major = 0;
	tdisk->first_minor = 0;
	tdisk->fops = &nvm_fops;
	tdisk->queue = tqueue;

	targetdata = tt->init(tgt_dev, tdisk, create->flags);
	if (IS_ERR(targetdata)) {
		ret = PTR_ERR(targetdata);
		goto err_init;
	}

	tdisk->private_data = targetdata;
	tqueue->queuedata = targetdata;

	blk_queue_max_hw_sectors(tqueue, 8 * dev->ops->max_phys_sect);

	set_capacity(tdisk, tt->capacity(targetdata));
	add_disk(tdisk);

	if (tt->sysfs_init && tt->sysfs_init(tdisk)) {
		ret = -ENOMEM;
		goto err_sysfs;
	}

	t->type = tt;
	t->disk = tdisk;
	t->dev = tgt_dev;

	mutex_lock(&dev->mlock);
	list_add_tail(&t->list, &dev->targets);
	mutex_unlock(&dev->mlock);

	return 0;
err_sysfs:
	if (tt->exit)
		tt->exit(targetdata);
err_init:
	blk_cleanup_queue(tqueue);
	tdisk->queue = NULL;
err_disk:
	put_disk(tdisk);
err_dev:
	nvm_remove_tgt_dev(tgt_dev, 0);
err_t:
	kfree(t);
err_reserve:
	nvm_release_luns_err(dev, s->lun_begin, s->lun_end);
	return ret;
}

static void __nvm_remove_target(struct nvm_target *t)
{
	struct nvm_tgt_type *tt = t->type;
	struct gendisk *tdisk = t->disk;
	struct request_queue *q = tdisk->queue;

	del_gendisk(tdisk);
	blk_cleanup_queue(q);

	if (tt->sysfs_exit)
		tt->sysfs_exit(tdisk);

	if (tt->exit)
		tt->exit(tdisk->private_data);

	nvm_remove_tgt_dev(t->dev, 1);
	put_disk(tdisk);

	list_del(&t->list);
	kfree(t);
}

/**
 * nvm_remove_tgt - Removes a target from the media manager
 * @dev:	device
 * @remove:	ioctl structure with target name to remove.
 *
 * Returns:
 * 0: on success
 * 1: on not found
 * <0: on error
 */
static int nvm_remove_tgt(struct nvm_dev *dev, struct nvm_ioctl_remove *remove)
{
	struct nvm_target *t;

	mutex_lock(&dev->mlock);
	t = nvm_find_target(dev, remove->tgtname);
	if (!t) {
		mutex_unlock(&dev->mlock);
		return 1;
	}
	__nvm_remove_target(t);
	mutex_unlock(&dev->mlock);

	return 0;
}

static int nvm_register_map(struct nvm_dev *dev)
{
	struct nvm_dev_map *rmap;
	int i, j;

	rmap = kmalloc(sizeof(struct nvm_dev_map), GFP_KERNEL);
	if (!rmap)
		goto err_rmap;

	rmap->chnls = kcalloc(dev->geo.nr_chnls, sizeof(struct nvm_ch_map),
								GFP_KERNEL);
	if (!rmap->chnls)
		goto err_chnls;

	for (i = 0; i < dev->geo.nr_chnls; i++) {
		struct nvm_ch_map *ch_rmap;
		int *lun_roffs;
		int luns_in_chnl = dev->geo.luns_per_chnl;

		ch_rmap = &rmap->chnls[i];

		ch_rmap->ch_off = -1;
		ch_rmap->nr_luns = luns_in_chnl;

		lun_roffs = kcalloc(luns_in_chnl, sizeof(int), GFP_KERNEL);
		if (!lun_roffs)
			goto err_ch;

		for (j = 0; j < luns_in_chnl; j++)
			lun_roffs[j] = -1;

		ch_rmap->lun_offs = lun_roffs;
	}

	dev->rmap = rmap;

	return 0;
err_ch:
	while (--i >= 0)
		kfree(rmap->chnls[i].lun_offs);
err_chnls:
	kfree(rmap);
err_rmap:
	return -ENOMEM;
}

static void nvm_unregister_map(struct nvm_dev *dev)
{
	struct nvm_dev_map *rmap = dev->rmap;
	int i;

	for (i = 0; i < dev->geo.nr_chnls; i++)
		kfree(rmap->chnls[i].lun_offs);

	kfree(rmap->chnls);
	kfree(rmap);
}

static void nvm_map_to_dev(struct nvm_tgt_dev *tgt_dev, struct ppa_addr *p)
{
	struct nvm_dev_map *dev_map = tgt_dev->map;
	struct nvm_ch_map *ch_map = &dev_map->chnls[p->g.ch];
	int lun_off = ch_map->lun_offs[p->g.lun];

	p->g.ch += ch_map->ch_off;
	p->g.lun += lun_off;
}

static void nvm_map_to_tgt(struct nvm_tgt_dev *tgt_dev, struct ppa_addr *p)
{
	struct nvm_dev *dev = tgt_dev->parent;
	struct nvm_dev_map *dev_rmap = dev->rmap;
	struct nvm_ch_map *ch_rmap = &dev_rmap->chnls[p->g.ch];
	int lun_roff = ch_rmap->lun_offs[p->g.lun];

	p->g.ch -= ch_rmap->ch_off;
	p->g.lun -= lun_roff;
}

static void nvm_ppa_tgt_to_dev(struct nvm_tgt_dev *tgt_dev,
				struct ppa_addr *ppa_list, int nr_ppas)
{
	int i;

	for (i = 0; i < nr_ppas; i++) {
		nvm_map_to_dev(tgt_dev, &ppa_list[i]);
		ppa_list[i] = generic_to_dev_addr(tgt_dev, ppa_list[i]);
	}
}

static void nvm_ppa_dev_to_tgt(struct nvm_tgt_dev *tgt_dev,
				struct ppa_addr *ppa_list, int nr_ppas)
{
	int i;

	for (i = 0; i < nr_ppas; i++) {
		ppa_list[i] = dev_to_generic_addr(tgt_dev, ppa_list[i]);
		nvm_map_to_tgt(tgt_dev, &ppa_list[i]);
	}
}

static void nvm_rq_tgt_to_dev(struct nvm_tgt_dev *tgt_dev, struct nvm_rq *rqd)
{
	if (rqd->nr_ppas == 1) {
		nvm_ppa_tgt_to_dev(tgt_dev, &rqd->ppa_addr, 1);
		return;
	}

	nvm_ppa_tgt_to_dev(tgt_dev, rqd->ppa_list, rqd->nr_ppas);
}

static void nvm_rq_dev_to_tgt(struct nvm_tgt_dev *tgt_dev, struct nvm_rq *rqd)
{
	if (rqd->nr_ppas == 1) {
		nvm_ppa_dev_to_tgt(tgt_dev, &rqd->ppa_addr, 1);
		return;
	}

	nvm_ppa_dev_to_tgt(tgt_dev, rqd->ppa_list, rqd->nr_ppas);
}

void nvm_part_to_tgt(struct nvm_dev *dev, sector_t *entries,
		     int len)
{
	struct nvm_geo *geo = &dev->geo;
	struct nvm_dev_map *dev_rmap = dev->rmap;
	u64 i;

	for (i = 0; i < len; i++) {
		struct nvm_ch_map *ch_rmap;
		int *lun_roffs;
		struct ppa_addr gaddr;
		u64 pba = le64_to_cpu(entries[i]);
		u64 diff;

		if (!pba)
			continue;

		gaddr = linear_to_generic_addr(geo, pba);
		ch_rmap = &dev_rmap->chnls[gaddr.g.ch];
		lun_roffs = ch_rmap->lun_offs;

		diff = ((ch_rmap->ch_off * geo->luns_per_chnl) +
				(lun_roffs[gaddr.g.lun])) * geo->sec_per_lun;

		entries[i] -= cpu_to_le64(diff);
	}
}
EXPORT_SYMBOL(nvm_part_to_tgt);

struct nvm_tgt_type *nvm_find_target_type(const char *name, int lock)
{
	struct nvm_tgt_type *tmp, *tt = NULL;

	if (lock)
		down_write(&nvm_tgtt_lock);

	list_for_each_entry(tmp, &nvm_tgt_types, list)
		if (!strcmp(name, tmp->name)) {
			tt = tmp;
			break;
		}

	if (lock)
		up_write(&nvm_tgtt_lock);
	return tt;
}
EXPORT_SYMBOL(nvm_find_target_type);

int nvm_register_tgt_type(struct nvm_tgt_type *tt)
{
	int ret = 0;

	down_write(&nvm_tgtt_lock);
	if (nvm_find_target_type(tt->name, 0))
		ret = -EEXIST;
	else
		list_add(&tt->list, &nvm_tgt_types);
	up_write(&nvm_tgtt_lock);

	return ret;
}
EXPORT_SYMBOL(nvm_register_tgt_type);

void nvm_unregister_tgt_type(struct nvm_tgt_type *tt)
{
	if (!tt)
		return;

	down_write(&nvm_lock);
	list_del(&tt->list);
	up_write(&nvm_lock);
}
EXPORT_SYMBOL(nvm_unregister_tgt_type);

void *nvm_dev_dma_alloc(struct nvm_dev *dev, gfp_t mem_flags,
							dma_addr_t *dma_handler)
{
	return dev->ops->dev_dma_alloc(dev, dev->dma_pool, mem_flags,
								dma_handler);
}
EXPORT_SYMBOL(nvm_dev_dma_alloc);

void nvm_dev_dma_free(struct nvm_dev *dev, void *addr, dma_addr_t dma_handler)
{
	dev->ops->dev_dma_free(dev->dma_pool, addr, dma_handler);
}
EXPORT_SYMBOL(nvm_dev_dma_free);

static struct nvm_dev *nvm_find_nvm_dev(const char *name)
{
	struct nvm_dev *dev;

	list_for_each_entry(dev, &nvm_devices, devices)
		if (!strcmp(name, dev->name))
			return dev;

	return NULL;
}

int nvm_set_tgt_bb_tbl(struct nvm_tgt_dev *tgt_dev, struct ppa_addr *ppas,
		       int nr_ppas, int type)
{
	struct nvm_dev *dev = tgt_dev->parent;
	struct nvm_rq rqd;
	int ret;

	if (nr_ppas > dev->ops->max_phys_sect) {
		pr_err("nvm: unable to update all blocks atomically\n");
		return -EINVAL;
	}

	memset(&rqd, 0, sizeof(struct nvm_rq));

	nvm_set_rqd_ppalist(tgt_dev, &rqd, ppas, nr_ppas, 1);
	nvm_rq_tgt_to_dev(tgt_dev, &rqd);

	ret = dev->ops->set_bb_tbl(dev, &rqd.ppa_addr, rqd.nr_ppas, type);
	nvm_free_rqd_ppalist(tgt_dev, &rqd);
	if (ret) {
		pr_err("nvm: failed bb mark\n");
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(nvm_set_tgt_bb_tbl);

int nvm_max_phys_sects(struct nvm_tgt_dev *tgt_dev)
{
	struct nvm_dev *dev = tgt_dev->parent;

	return dev->ops->max_phys_sect;
}
EXPORT_SYMBOL(nvm_max_phys_sects);

int nvm_submit_io(struct nvm_tgt_dev *tgt_dev, struct nvm_rq *rqd)
{
	struct nvm_dev *dev = tgt_dev->parent;
	int ret;

	if (!dev->ops->submit_io)
		return -ENODEV;

	nvm_rq_tgt_to_dev(tgt_dev, rqd);

	rqd->dev = tgt_dev;

	/* In case of error, fail with right address format */
	ret = dev->ops->submit_io(dev, rqd);
	if (ret)
		nvm_rq_dev_to_tgt(tgt_dev, rqd);
	return ret;
}
EXPORT_SYMBOL(nvm_submit_io);

static void nvm_end_io_sync(struct nvm_rq *rqd)
{
	struct completion *waiting = rqd->private;

	complete(waiting);
}

int nvm_erase_sync(struct nvm_tgt_dev *tgt_dev, struct ppa_addr *ppas,
								int nr_ppas)
{
	struct nvm_geo *geo = &tgt_dev->geo;
	struct nvm_rq rqd;
	int ret;
	DECLARE_COMPLETION_ONSTACK(wait);

	memset(&rqd, 0, sizeof(struct nvm_rq));

	rqd.opcode = NVM_OP_ERASE;
	rqd.end_io = nvm_end_io_sync;
	rqd.private = &wait;
	rqd.flags = geo->plane_mode >> 1;

	ret = nvm_set_rqd_ppalist(tgt_dev, &rqd, ppas, nr_ppas, 1);
	if (ret)
		return ret;

	ret = nvm_submit_io(tgt_dev, &rqd);
	if (ret) {
		pr_err("rrpr: erase I/O submission failed: %d\n", ret);
		goto free_ppa_list;
	}
	wait_for_completion_io(&wait);

free_ppa_list:
	nvm_free_rqd_ppalist(tgt_dev, &rqd);

	return ret;
}
EXPORT_SYMBOL(nvm_erase_sync);

int nvm_get_l2p_tbl(struct nvm_tgt_dev *tgt_dev, u64 slba, u32 nlb,
		    nvm_l2p_update_fn *update_l2p, void *priv)
{
	struct nvm_dev *dev = tgt_dev->parent;

	if (!dev->ops->get_l2p_tbl)
		return 0;

	return dev->ops->get_l2p_tbl(dev, slba, nlb, update_l2p, priv);
}
EXPORT_SYMBOL(nvm_get_l2p_tbl);

int nvm_get_area(struct nvm_tgt_dev *tgt_dev, sector_t *lba, sector_t len)
{
	struct nvm_dev *dev = tgt_dev->parent;
	struct nvm_geo *geo = &dev->geo;
	struct nvm_area *area, *prev, *next;
	sector_t begin = 0;
	sector_t max_sectors = (geo->sec_size * dev->total_secs) >> 9;

	if (len > max_sectors)
		return -EINVAL;

	area = kmalloc(sizeof(struct nvm_area), GFP_KERNEL);
	if (!area)
		return -ENOMEM;

	prev = NULL;

	spin_lock(&dev->lock);
	list_for_each_entry(next, &dev->area_list, list) {
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
		list_add(&area->list, &dev->area_list);
	spin_unlock(&dev->lock);

	return 0;
}
EXPORT_SYMBOL(nvm_get_area);

void nvm_put_area(struct nvm_tgt_dev *tgt_dev, sector_t begin)
{
	struct nvm_dev *dev = tgt_dev->parent;
	struct nvm_area *area;

	spin_lock(&dev->lock);
	list_for_each_entry(area, &dev->area_list, list) {
		if (area->begin != begin)
			continue;

		list_del(&area->list);
		spin_unlock(&dev->lock);
		kfree(area);
		return;
	}
	spin_unlock(&dev->lock);
}
EXPORT_SYMBOL(nvm_put_area);

int nvm_set_rqd_ppalist(struct nvm_tgt_dev *tgt_dev, struct nvm_rq *rqd,
			const struct ppa_addr *ppas, int nr_ppas, int vblk)
{
	struct nvm_dev *dev = tgt_dev->parent;
	struct nvm_geo *geo = &tgt_dev->geo;
	int i, plane_cnt, pl_idx;
	struct ppa_addr ppa;

	if ((!vblk || geo->plane_mode == NVM_PLANE_SINGLE) && nr_ppas == 1) {
		rqd->nr_ppas = nr_ppas;
		rqd->ppa_addr = ppas[0];

		return 0;
	}

	rqd->nr_ppas = nr_ppas;
	rqd->ppa_list = nvm_dev_dma_alloc(dev, GFP_KERNEL, &rqd->dma_ppa_list);
	if (!rqd->ppa_list) {
		pr_err("nvm: failed to allocate dma memory\n");
		return -ENOMEM;
	}

	if (!vblk) {
		for (i = 0; i < nr_ppas; i++)
			rqd->ppa_list[i] = ppas[i];
	} else {
		plane_cnt = geo->plane_mode;
		rqd->nr_ppas *= plane_cnt;

		for (i = 0; i < nr_ppas; i++) {
			for (pl_idx = 0; pl_idx < plane_cnt; pl_idx++) {
				ppa = ppas[i];
				ppa.g.pl = pl_idx;
				rqd->ppa_list[(pl_idx * nr_ppas) + i] = ppa;
			}
		}
	}

	return 0;
}
EXPORT_SYMBOL(nvm_set_rqd_ppalist);

void nvm_free_rqd_ppalist(struct nvm_tgt_dev *tgt_dev, struct nvm_rq *rqd)
{
	if (!rqd->ppa_list)
		return;

	nvm_dev_dma_free(tgt_dev->parent, rqd->ppa_list, rqd->dma_ppa_list);
}
EXPORT_SYMBOL(nvm_free_rqd_ppalist);

void nvm_end_io(struct nvm_rq *rqd)
{
	struct nvm_tgt_dev *tgt_dev = rqd->dev;

	/* Convert address space */
	if (tgt_dev)
		nvm_rq_dev_to_tgt(tgt_dev, rqd);

	if (rqd->end_io)
		rqd->end_io(rqd);
}
EXPORT_SYMBOL(nvm_end_io);

/*
 * folds a bad block list from its plane representation to its virtual
 * block representation. The fold is done in place and reduced size is
 * returned.
 *
 * If any of the planes status are bad or grown bad block, the virtual block
 * is marked bad. If not bad, the first plane state acts as the block state.
 */
int nvm_bb_tbl_fold(struct nvm_dev *dev, u8 *blks, int nr_blks)
{
	struct nvm_geo *geo = &dev->geo;
	int blk, offset, pl, blktype;

	if (nr_blks != geo->blks_per_lun * geo->plane_mode)
		return -EINVAL;

	for (blk = 0; blk < geo->blks_per_lun; blk++) {
		offset = blk * geo->plane_mode;
		blktype = blks[offset];

		/* Bad blocks on any planes take precedence over other types */
		for (pl = 0; pl < geo->plane_mode; pl++) {
			if (blks[offset + pl] &
					(NVM_BLK_T_BAD|NVM_BLK_T_GRWN_BAD)) {
				blktype = blks[offset + pl];
				break;
			}
		}

		blks[blk] = blktype;
	}

	return geo->blks_per_lun;
}
EXPORT_SYMBOL(nvm_bb_tbl_fold);

int nvm_get_tgt_bb_tbl(struct nvm_tgt_dev *tgt_dev, struct ppa_addr ppa,
		       u8 *blks)
{
	struct nvm_dev *dev = tgt_dev->parent;

	nvm_ppa_tgt_to_dev(tgt_dev, &ppa, 1);

	return dev->ops->get_bb_tbl(dev, ppa, blks);
}
EXPORT_SYMBOL(nvm_get_tgt_bb_tbl);

static int nvm_init_slc_tbl(struct nvm_dev *dev, struct nvm_id_group *grp)
{
	struct nvm_geo *geo = &dev->geo;
	int i;

	dev->lps_per_blk = geo->pgs_per_blk;
	dev->lptbl = kcalloc(dev->lps_per_blk, sizeof(int), GFP_KERNEL);
	if (!dev->lptbl)
		return -ENOMEM;

	/* Just a linear array */
	for (i = 0; i < dev->lps_per_blk; i++)
		dev->lptbl[i] = i;

	return 0;
}

static int nvm_init_mlc_tbl(struct nvm_dev *dev, struct nvm_id_group *grp)
{
	int i, p;
	struct nvm_id_lp_mlc *mlc = &grp->lptbl.mlc;

	if (!mlc->num_pairs)
		return 0;

	dev->lps_per_blk = mlc->num_pairs;
	dev->lptbl = kcalloc(dev->lps_per_blk, sizeof(int), GFP_KERNEL);
	if (!dev->lptbl)
		return -ENOMEM;

	/* The lower page table encoding consists of a list of bytes, where each
	 * has a lower and an upper half. The first half byte maintains the
	 * increment value and every value after is an offset added to the
	 * previous incrementation value
	 */
	dev->lptbl[0] = mlc->pairs[0] & 0xF;
	for (i = 1; i < dev->lps_per_blk; i++) {
		p = mlc->pairs[i >> 1];
		if (i & 0x1) /* upper */
			dev->lptbl[i] = dev->lptbl[i - 1] + ((p & 0xF0) >> 4);
		else /* lower */
			dev->lptbl[i] = dev->lptbl[i - 1] + (p & 0xF);
	}

	return 0;
}

static int nvm_core_init(struct nvm_dev *dev)
{
	struct nvm_id *id = &dev->identity;
	struct nvm_id_group *grp = &id->grp;
	struct nvm_geo *geo = &dev->geo;
	int ret;

	/* Whole device values */
	geo->nr_chnls = grp->num_ch;
	geo->luns_per_chnl = grp->num_lun;

	/* Generic device values */
	geo->pgs_per_blk = grp->num_pg;
	geo->blks_per_lun = grp->num_blk;
	geo->nr_planes = grp->num_pln;
	geo->fpg_size = grp->fpg_sz;
	geo->pfpg_size = grp->fpg_sz * grp->num_pln;
	geo->sec_size = grp->csecs;
	geo->oob_size = grp->sos;
	geo->sec_per_pg = grp->fpg_sz / grp->csecs;
	geo->mccap = grp->mccap;
	memcpy(&geo->ppaf, &id->ppaf, sizeof(struct nvm_addr_format));

	geo->plane_mode = NVM_PLANE_SINGLE;
	geo->max_rq_size = dev->ops->max_phys_sect * geo->sec_size;

	if (grp->mpos & 0x020202)
		geo->plane_mode = NVM_PLANE_DOUBLE;
	if (grp->mpos & 0x040404)
		geo->plane_mode = NVM_PLANE_QUAD;

	if (grp->mtype != 0) {
		pr_err("nvm: memory type not supported\n");
		return -EINVAL;
	}

	/* calculated values */
	geo->sec_per_pl = geo->sec_per_pg * geo->nr_planes;
	geo->sec_per_blk = geo->sec_per_pl * geo->pgs_per_blk;
	geo->sec_per_lun = geo->sec_per_blk * geo->blks_per_lun;
	geo->nr_luns = geo->luns_per_chnl * geo->nr_chnls;

	dev->total_secs = geo->nr_luns * geo->sec_per_lun;
	dev->lun_map = kcalloc(BITS_TO_LONGS(geo->nr_luns),
					sizeof(unsigned long), GFP_KERNEL);
	if (!dev->lun_map)
		return -ENOMEM;

	switch (grp->fmtype) {
	case NVM_ID_FMTYPE_SLC:
		if (nvm_init_slc_tbl(dev, grp)) {
			ret = -ENOMEM;
			goto err_fmtype;
		}
		break;
	case NVM_ID_FMTYPE_MLC:
		if (nvm_init_mlc_tbl(dev, grp)) {
			ret = -ENOMEM;
			goto err_fmtype;
		}
		break;
	default:
		pr_err("nvm: flash type not supported\n");
		ret = -EINVAL;
		goto err_fmtype;
	}

	INIT_LIST_HEAD(&dev->area_list);
	INIT_LIST_HEAD(&dev->targets);
	mutex_init(&dev->mlock);
	spin_lock_init(&dev->lock);

	ret = nvm_register_map(dev);
	if (ret)
		goto err_fmtype;

	blk_queue_logical_block_size(dev->q, geo->sec_size);
	return 0;
err_fmtype:
	kfree(dev->lun_map);
	return ret;
}

static void nvm_free(struct nvm_dev *dev)
{
	if (!dev)
		return;

	if (dev->dma_pool)
		dev->ops->destroy_dma_pool(dev->dma_pool);

	nvm_unregister_map(dev);
	kfree(dev->lptbl);
	kfree(dev->lun_map);
	kfree(dev);
}

static int nvm_init(struct nvm_dev *dev)
{
	struct nvm_geo *geo = &dev->geo;
	int ret = -EINVAL;

	if (dev->ops->identity(dev, &dev->identity)) {
		pr_err("nvm: device could not be identified\n");
		goto err;
	}

	pr_debug("nvm: ver:%x nvm_vendor:%x\n",
			dev->identity.ver_id, dev->identity.vmnt);

	if (dev->identity.ver_id != 1) {
		pr_err("nvm: device not supported by kernel.");
		goto err;
	}

	ret = nvm_core_init(dev);
	if (ret) {
		pr_err("nvm: could not initialize core structures.\n");
		goto err;
	}

	pr_info("nvm: registered %s [%u/%u/%u/%u/%u/%u]\n",
			dev->name, geo->sec_per_pg, geo->nr_planes,
			geo->pgs_per_blk, geo->blks_per_lun,
			geo->nr_luns, geo->nr_chnls);
	return 0;
err:
	pr_err("nvm: failed to initialize nvm\n");
	return ret;
}

struct nvm_dev *nvm_alloc_dev(int node)
{
	return kzalloc_node(sizeof(struct nvm_dev), GFP_KERNEL, node);
}
EXPORT_SYMBOL(nvm_alloc_dev);

int nvm_register(struct nvm_dev *dev)
{
	int ret;

	if (!dev->q || !dev->ops)
		return -EINVAL;

	if (dev->ops->max_phys_sect > 256) {
		pr_info("nvm: max sectors supported is 256.\n");
		return -EINVAL;
	}

	if (dev->ops->max_phys_sect > 1) {
		dev->dma_pool = dev->ops->create_dma_pool(dev, "ppalist");
		if (!dev->dma_pool) {
			pr_err("nvm: could not create dma pool\n");
			return -ENOMEM;
		}
	}

	ret = nvm_init(dev);
	if (ret)
		goto err_init;

	/* register device with a supported media manager */
	down_write(&nvm_lock);
	list_add(&dev->devices, &nvm_devices);
	up_write(&nvm_lock);

	return 0;
err_init:
	dev->ops->destroy_dma_pool(dev->dma_pool);
	return ret;
}
EXPORT_SYMBOL(nvm_register);

void nvm_unregister(struct nvm_dev *dev)
{
	struct nvm_target *t, *tmp;

	mutex_lock(&dev->mlock);
	list_for_each_entry_safe(t, tmp, &dev->targets, list) {
		if (t->dev->parent != dev)
			continue;
		__nvm_remove_target(t);
	}
	mutex_unlock(&dev->mlock);

	down_write(&nvm_lock);
	list_del(&dev->devices);
	up_write(&nvm_lock);

	nvm_free(dev);
}
EXPORT_SYMBOL(nvm_unregister);

static int __nvm_configure_create(struct nvm_ioctl_create *create)
{
	struct nvm_dev *dev;
	struct nvm_ioctl_create_simple *s;

	down_write(&nvm_lock);
	dev = nvm_find_nvm_dev(create->dev);
	up_write(&nvm_lock);

	if (!dev) {
		pr_err("nvm: device not found\n");
		return -EINVAL;
	}

	if (create->conf.type != NVM_CONFIG_TYPE_SIMPLE) {
		pr_err("nvm: config type not valid\n");
		return -EINVAL;
	}
	s = &create->conf.s;

	if (s->lun_begin == -1 && s->lun_end == -1) {
		s->lun_begin = 0;
		s->lun_end = dev->geo.nr_luns - 1;
	}

	if (s->lun_begin > s->lun_end || s->lun_end >= dev->geo.nr_luns) {
		pr_err("nvm: lun out of bound (%u:%u > %u)\n",
			s->lun_begin, s->lun_end, dev->geo.nr_luns - 1);
		return -EINVAL;
	}

	return nvm_create_tgt(dev, create);
}

static long nvm_ioctl_info(struct file *file, void __user *arg)
{
	struct nvm_ioctl_info *info;
	struct nvm_tgt_type *tt;
	int tgt_iter = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	info = memdup_user(arg, sizeof(struct nvm_ioctl_info));
	if (IS_ERR(info))
		return -EFAULT;

	info->version[0] = NVM_VERSION_MAJOR;
	info->version[1] = NVM_VERSION_MINOR;
	info->version[2] = NVM_VERSION_PATCH;

	down_write(&nvm_lock);
	list_for_each_entry(tt, &nvm_tgt_types, list) {
		struct nvm_ioctl_info_tgt *tgt = &info->tgts[tgt_iter];

		tgt->version[0] = tt->version[0];
		tgt->version[1] = tt->version[1];
		tgt->version[2] = tt->version[2];
		strncpy(tgt->tgtname, tt->name, NVM_TTYPE_NAME_MAX);

		tgt_iter++;
	}

	info->tgtsize = tgt_iter;
	up_write(&nvm_lock);

	if (copy_to_user(arg, info, sizeof(struct nvm_ioctl_info))) {
		kfree(info);
		return -EFAULT;
	}

	kfree(info);
	return 0;
}

static long nvm_ioctl_get_devices(struct file *file, void __user *arg)
{
	struct nvm_ioctl_get_devices *devices;
	struct nvm_dev *dev;
	int i = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	devices = kzalloc(sizeof(struct nvm_ioctl_get_devices), GFP_KERNEL);
	if (!devices)
		return -ENOMEM;

	down_write(&nvm_lock);
	list_for_each_entry(dev, &nvm_devices, devices) {
		struct nvm_ioctl_device_info *info = &devices->info[i];

		strlcpy(info->devname, dev->name, sizeof(info->devname));

		/* kept for compatibility */
		info->bmversion[0] = 1;
		info->bmversion[1] = 0;
		info->bmversion[2] = 0;
		strlcpy(info->bmname, "gennvm", sizeof(info->bmname));
		i++;

		if (i > 31) {
			pr_err("nvm: max 31 devices can be reported.\n");
			break;
		}
	}
	up_write(&nvm_lock);

	devices->nr_devices = i;

	if (copy_to_user(arg, devices,
			 sizeof(struct nvm_ioctl_get_devices))) {
		kfree(devices);
		return -EFAULT;
	}

	kfree(devices);
	return 0;
}

static long nvm_ioctl_dev_create(struct file *file, void __user *arg)
{
	struct nvm_ioctl_create create;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (copy_from_user(&create, arg, sizeof(struct nvm_ioctl_create)))
		return -EFAULT;

	create.dev[DISK_NAME_LEN - 1] = '\0';
	create.tgttype[NVM_TTYPE_NAME_MAX - 1] = '\0';
	create.tgtname[DISK_NAME_LEN - 1] = '\0';

	if (create.flags != 0) {
		__u32 flags = create.flags;

		/* Check for valid flags */
		if (flags & NVM_TARGET_FACTORY)
			flags &= ~NVM_TARGET_FACTORY;

		if (flags) {
			pr_err("nvm: flag not supported\n");
			return -EINVAL;
		}
	}

	return __nvm_configure_create(&create);
}

static long nvm_ioctl_dev_remove(struct file *file, void __user *arg)
{
	struct nvm_ioctl_remove remove;
	struct nvm_dev *dev;
	int ret = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (copy_from_user(&remove, arg, sizeof(struct nvm_ioctl_remove)))
		return -EFAULT;

	remove.tgtname[DISK_NAME_LEN - 1] = '\0';

	if (remove.flags != 0) {
		pr_err("nvm: no flags supported\n");
		return -EINVAL;
	}

	list_for_each_entry(dev, &nvm_devices, devices) {
		ret = nvm_remove_tgt(dev, &remove);
		if (!ret)
			break;
	}

	return ret;
}

/* kept for compatibility reasons */
static long nvm_ioctl_dev_init(struct file *file, void __user *arg)
{
	struct nvm_ioctl_dev_init init;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (copy_from_user(&init, arg, sizeof(struct nvm_ioctl_dev_init)))
		return -EFAULT;

	if (init.flags != 0) {
		pr_err("nvm: no flags supported\n");
		return -EINVAL;
	}

	return 0;
}

/* Kept for compatibility reasons */
static long nvm_ioctl_dev_factory(struct file *file, void __user *arg)
{
	struct nvm_ioctl_dev_factory fact;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (copy_from_user(&fact, arg, sizeof(struct nvm_ioctl_dev_factory)))
		return -EFAULT;

	fact.dev[DISK_NAME_LEN - 1] = '\0';

	if (fact.flags & ~(NVM_FACTORY_NR_BITS - 1))
		return -EINVAL;

	return 0;
}

static long nvm_ctl_ioctl(struct file *file, uint cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;

	switch (cmd) {
	case NVM_INFO:
		return nvm_ioctl_info(file, argp);
	case NVM_GET_DEVICES:
		return nvm_ioctl_get_devices(file, argp);
	case NVM_DEV_CREATE:
		return nvm_ioctl_dev_create(file, argp);
	case NVM_DEV_REMOVE:
		return nvm_ioctl_dev_remove(file, argp);
	case NVM_DEV_INIT:
		return nvm_ioctl_dev_init(file, argp);
	case NVM_DEV_FACTORY:
		return nvm_ioctl_dev_factory(file, argp);
	}
	return 0;
}

static const struct file_operations _ctl_fops = {
	.open = nonseekable_open,
	.unlocked_ioctl = nvm_ctl_ioctl,
	.owner = THIS_MODULE,
	.llseek  = noop_llseek,
};

static struct miscdevice _nvm_misc = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= "lightnvm",
	.nodename	= "lightnvm/control",
	.fops		= &_ctl_fops,
};
builtin_misc_device(_nvm_misc);
