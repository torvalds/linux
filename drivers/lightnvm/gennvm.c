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
	struct gen_dev *gn = dev->mp;
	struct nvm_lun *lun;
	int i;

	for (i = lun_begin; i <= lun_end; i++) {
		if (test_and_set_bit(i, dev->lun_map)) {
			pr_err("nvm: lun %d already allocated\n", i);
			goto err;
		}

		lun = &gn->luns[i];
		list_add_tail(&lun->list, &t->lun_list);
	}

	return 0;

err:
	while (--i > lun_begin) {
		lun = &gn->luns[i];
		clear_bit(i, dev->lun_map);
		list_del(&lun->list);
	}

	return -EBUSY;
}

static void gen_release_luns(struct nvm_dev *dev, struct nvm_target *t)
{
	struct nvm_lun *lun, *tmp;

	list_for_each_entry_safe(lun, tmp, &t->lun_list, list) {
		WARN_ON(!test_and_clear_bit(lun->id, dev->lun_map));
		list_del(&lun->list);
	}
}

static void gen_remove_tgt_dev(struct nvm_tgt_dev *tgt_dev)
{
	kfree(tgt_dev);
}

static struct nvm_tgt_dev *gen_create_tgt_dev(struct nvm_dev *dev,
					      int lun_begin, int lun_end)
{
	struct nvm_tgt_dev *tgt_dev = NULL;
	int nr_luns = lun_end - lun_begin + 1;

	tgt_dev = kmalloc(sizeof(struct nvm_tgt_dev), GFP_KERNEL);
	if (!tgt_dev)
		goto out;

	memcpy(&tgt_dev->geo, &dev->geo, sizeof(struct nvm_geo));
	tgt_dev->geo.nr_chnls = (nr_luns / (dev->geo.luns_per_chnl + 1)) + 1;
	tgt_dev->geo.nr_luns = nr_luns;
	tgt_dev->total_secs = nr_luns * tgt_dev->geo.sec_per_lun;
	tgt_dev->q = dev->q;
	tgt_dev->ops = dev->ops;
	tgt_dev->mt = dev->mt;
	memcpy(&tgt_dev->identity, &dev->identity, sizeof(struct nvm_id));

	tgt_dev->parent = dev;

out:
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

	INIT_LIST_HEAD(&t->lun_list);

	if (gen_reserve_luns(dev, t, s->lun_begin, s->lun_end))
		goto err_t;

	tgt_dev = gen_create_tgt_dev(dev, s->lun_begin, s->lun_end);
	if (!tgt_dev)
		goto err_reserve;

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

	targetdata = tt->init(tgt_dev, tdisk, &t->lun_list);
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
	gen_release_luns(dev, t);
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

	gen_release_luns(t->dev->parent, t);
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

static void gen_luns_free(struct nvm_dev *dev)
{
	struct gen_dev *gn = dev->mp;

	kfree(gn->luns);
}

static int gen_luns_init(struct nvm_dev *dev, struct gen_dev *gn)
{
	struct nvm_geo *geo = &dev->geo;
	struct nvm_lun *lun;
	int i;

	gn->luns = kcalloc(geo->nr_luns, sizeof(struct nvm_lun), GFP_KERNEL);
	if (!gn->luns)
		return -ENOMEM;

	gen_for_each_lun(gn, lun, i) {
		INIT_LIST_HEAD(&lun->list);

		lun->id = i;
		lun->lun_id = i % geo->luns_per_chnl;
		lun->chnl_id = i / geo->luns_per_chnl;
	}
	return 0;
}

static void gen_free(struct nvm_dev *dev)
{
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
	gn->nr_luns = dev->geo.nr_luns;
	INIT_LIST_HEAD(&gn->area_list);
	mutex_init(&gn->lock);
	INIT_LIST_HEAD(&gn->targets);
	dev->mp = gn;

	ret = gen_luns_init(dev, gn);
	if (ret) {
		pr_err("gen: could not initialize luns\n");
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
		if (t->dev->parent != dev)
			continue;
		__gen_remove_target(t);
	}
	mutex_unlock(&gn->lock);

	gen_free(dev);
	module_put(THIS_MODULE);
}

static void gen_end_io(struct nvm_rq *rqd)
{
	struct nvm_tgt_instance *ins = rqd->ins;

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

static int gen_erase_blk(struct nvm_dev *dev, struct ppa_addr *p, int flags)
{
	return nvm_erase_ppa(dev, p, 1, flags);
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
