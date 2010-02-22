/*
 * (C) 2003 David Woodhouse <dwmw2@infradead.org>
 *
 * Interface to Linux 2.5 block layer for MTD 'translation layers'.
 *
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/fs.h>
#include <linux/mtd/blktrans.h>
#include <linux/mtd/mtd.h>
#include <linux/blkdev.h>
#include <linux/blkpg.h>
#include <linux/spinlock.h>
#include <linux/hdreg.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <asm/uaccess.h>

#include "mtdcore.h"

static LIST_HEAD(blktrans_majors);


static int do_blktrans_request(struct mtd_blktrans_ops *tr,
			       struct mtd_blktrans_dev *dev,
			       struct request *req)
{
	unsigned long block, nsect;
	char *buf;

	block = blk_rq_pos(req) << 9 >> tr->blkshift;
	nsect = blk_rq_cur_bytes(req) >> tr->blkshift;

	buf = req->buffer;

	if (!blk_fs_request(req))
		return -EIO;

	if (blk_rq_pos(req) + blk_rq_cur_sectors(req) >
	    get_capacity(req->rq_disk))
		return -EIO;

	if (blk_discard_rq(req))
		return tr->discard(dev, block, nsect);

	switch(rq_data_dir(req)) {
	case READ:
		for (; nsect > 0; nsect--, block++, buf += tr->blksize)
			if (tr->readsect(dev, block, buf))
				return -EIO;
		rq_flush_dcache_pages(req);
		return 0;
	case WRITE:
		if (!tr->writesect)
			return -EIO;

		rq_flush_dcache_pages(req);
		for (; nsect > 0; nsect--, block++, buf += tr->blksize)
			if (tr->writesect(dev, block, buf))
				return -EIO;
		return 0;
	default:
		printk(KERN_NOTICE "Unknown request %u\n", rq_data_dir(req));
		return -EIO;
	}
}

static int mtd_blktrans_thread(void *arg)
{
	struct mtd_blktrans_dev *dev = arg;
	struct request_queue *rq = dev->rq;
	struct request *req = NULL;

	spin_lock_irq(rq->queue_lock);

	while (!kthread_should_stop()) {
		int res;

		if (!req && !(req = blk_fetch_request(rq))) {
			set_current_state(TASK_INTERRUPTIBLE);
			spin_unlock_irq(rq->queue_lock);
			schedule();
			spin_lock_irq(rq->queue_lock);
			continue;
		}

		spin_unlock_irq(rq->queue_lock);

		mutex_lock(&dev->lock);
		res = do_blktrans_request(dev->tr, dev, req);
		mutex_unlock(&dev->lock);

		spin_lock_irq(rq->queue_lock);

		if (!__blk_end_request_cur(req, res))
			req = NULL;
	}

	if (req)
		__blk_end_request_all(req, -EIO);

	spin_unlock_irq(rq->queue_lock);

	return 0;
}

static void mtd_blktrans_request(struct request_queue *rq)
{
	struct mtd_blktrans_dev *dev = rq->queuedata;
	wake_up_process(dev->thread);
}


static int blktrans_open(struct block_device *bdev, fmode_t mode)
{
	struct mtd_blktrans_dev *dev = bdev->bd_disk->private_data;
	struct mtd_blktrans_ops *tr = dev->tr;
	int ret = -ENODEV;

	if (!get_mtd_device(NULL, dev->mtd->index))
		goto out;

	if (!try_module_get(tr->owner))
		goto out_tr;

	/* FIXME: Locking. A hot pluggable device can go away
	   (del_mtd_device can be called for it) without its module
	   being unloaded. */
	dev->mtd->usecount++;

	ret = 0;
	if (tr->open && (ret = tr->open(dev))) {
		dev->mtd->usecount--;
		put_mtd_device(dev->mtd);
	out_tr:
		module_put(tr->owner);
	}
 out:
	return ret;
}

static int blktrans_release(struct gendisk *disk, fmode_t mode)
{
	struct mtd_blktrans_dev *dev = disk->private_data;
	struct mtd_blktrans_ops *tr = dev->tr;
	int ret = 0;

	if (tr->release)
		ret = tr->release(dev);

	if (!ret) {
		dev->mtd->usecount--;
		put_mtd_device(dev->mtd);
		module_put(tr->owner);
	}

	return ret;
}

static int blktrans_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	struct mtd_blktrans_dev *dev = bdev->bd_disk->private_data;

	if (dev->tr->getgeo)
		return dev->tr->getgeo(dev, geo);
	return -ENOTTY;
}

static int blktrans_ioctl(struct block_device *bdev, fmode_t mode,
			      unsigned int cmd, unsigned long arg)
{
	struct mtd_blktrans_dev *dev = bdev->bd_disk->private_data;
	struct mtd_blktrans_ops *tr = dev->tr;

	switch (cmd) {
	case BLKFLSBUF:
		if (tr->flush)
			return tr->flush(dev);
		/* The core code did the work, we had nothing to do. */
		return 0;
	default:
		return -ENOTTY;
	}
}

static const struct block_device_operations mtd_blktrans_ops = {
	.owner		= THIS_MODULE,
	.open		= blktrans_open,
	.release	= blktrans_release,
	.locked_ioctl	= blktrans_ioctl,
	.getgeo		= blktrans_getgeo,
};

int add_mtd_blktrans_dev(struct mtd_blktrans_dev *new)
{
	struct mtd_blktrans_ops *tr = new->tr;
	struct mtd_blktrans_dev *d;
	int last_devnum = -1;
	struct gendisk *gd;
	int ret;

	if (mutex_trylock(&mtd_table_mutex)) {
		mutex_unlock(&mtd_table_mutex);
		BUG();
	}

	list_for_each_entry(d, &tr->devs, list) {
		if (new->devnum == -1) {
			/* Use first free number */
			if (d->devnum != last_devnum+1) {
				/* Found a free devnum. Plug it in here */
				new->devnum = last_devnum+1;
				list_add_tail(&new->list, &d->list);
				goto added;
			}
		} else if (d->devnum == new->devnum) {
			/* Required number taken */
			return -EBUSY;
		} else if (d->devnum > new->devnum) {
			/* Required number was free */
			list_add_tail(&new->list, &d->list);
			goto added;
		}
		last_devnum = d->devnum;
	}

	ret = -EBUSY;
	if (new->devnum == -1)
		new->devnum = last_devnum+1;

	/* Check that the device and any partitions will get valid
	 * minor numbers and that the disk naming code below can cope
	 * with this number. */
	if (new->devnum > (MINORMASK >> tr->part_bits) ||
	    (tr->part_bits && new->devnum >= 27 * 26))
		goto error1;

	list_add_tail(&new->list, &tr->devs);
 added:
	mutex_init(&new->lock);
	if (!tr->writesect)
		new->readonly = 1;


	/* Create gendisk */
	ret = -ENOMEM;
	gd = alloc_disk(1 << tr->part_bits);

	if (!gd)
		goto error2;

	new->disk = gd;
	gd->private_data = new;
	gd->major = tr->major;
	gd->first_minor = (new->devnum) << tr->part_bits;
	gd->fops = &mtd_blktrans_ops;

	if (tr->part_bits)
		if (new->devnum < 26)
			snprintf(gd->disk_name, sizeof(gd->disk_name),
				 "%s%c", tr->name, 'a' + new->devnum);
		else
			snprintf(gd->disk_name, sizeof(gd->disk_name),
				 "%s%c%c", tr->name,
				 'a' - 1 + new->devnum / 26,
				 'a' + new->devnum % 26);
	else
		snprintf(gd->disk_name, sizeof(gd->disk_name),
			 "%s%d", tr->name, new->devnum);

	set_capacity(gd, (new->size * tr->blksize) >> 9);


	/* Create the request queue */
	spin_lock_init(&new->queue_lock);
	new->rq = blk_init_queue(mtd_blktrans_request, &new->queue_lock);

	if (!new->rq)
		goto error3;

	new->rq->queuedata = new;
	blk_queue_logical_block_size(new->rq, tr->blksize);

	if (tr->discard)
		queue_flag_set_unlocked(QUEUE_FLAG_DISCARD,
					new->rq);

	gd->queue = new->rq;

	/* Create processing thread */
	/* TODO: workqueue ? */
	new->thread = kthread_run(mtd_blktrans_thread, new,
			"%s%d", tr->name, new->mtd->index);
	if (IS_ERR(new->thread)) {
		ret = PTR_ERR(new->thread);
		goto error4;
	}
	gd->driverfs_dev = &new->mtd->dev;

	if (new->readonly)
		set_disk_ro(gd, 1);

	add_disk(gd);
	return 0;
error4:
	blk_cleanup_queue(new->rq);
error3:
	put_disk(new->disk);
error2:
	list_del(&new->list);
error1:
	kfree(new);
	return ret;
}

int del_mtd_blktrans_dev(struct mtd_blktrans_dev *old)
{
	if (mutex_trylock(&mtd_table_mutex)) {
		mutex_unlock(&mtd_table_mutex);
		BUG();
	}

	list_del(&old->list);

	/* stop new requests to arrive */
	del_gendisk(old->disk);

	/* Stop the thread */
	kthread_stop(old->thread);

	blk_cleanup_queue(old->rq);
	return 0;
}

static void blktrans_notify_remove(struct mtd_info *mtd)
{
	struct mtd_blktrans_ops *tr;
	struct mtd_blktrans_dev *dev, *next;

	list_for_each_entry(tr, &blktrans_majors, list)
		list_for_each_entry_safe(dev, next, &tr->devs, list)
			if (dev->mtd == mtd)
				tr->remove_dev(dev);
}

static void blktrans_notify_add(struct mtd_info *mtd)
{
	struct mtd_blktrans_ops *tr;

	if (mtd->type == MTD_ABSENT)
		return;

	list_for_each_entry(tr, &blktrans_majors, list)
		tr->add_mtd(tr, mtd);
}

static struct mtd_notifier blktrans_notifier = {
	.add = blktrans_notify_add,
	.remove = blktrans_notify_remove,
};

int register_mtd_blktrans(struct mtd_blktrans_ops *tr)
{
	struct mtd_info *mtd;
	int ret;

	/* Register the notifier if/when the first device type is
	   registered, to prevent the link/init ordering from fucking
	   us over. */
	if (!blktrans_notifier.list.next)
		register_mtd_user(&blktrans_notifier);


	mutex_lock(&mtd_table_mutex);

	ret = register_blkdev(tr->major, tr->name);
	if (ret) {
		printk(KERN_WARNING "Unable to register %s block device on major %d: %d\n",
		       tr->name, tr->major, ret);
		mutex_unlock(&mtd_table_mutex);
		return ret;
	}

	tr->blkshift = ffs(tr->blksize) - 1;

	INIT_LIST_HEAD(&tr->devs);
	list_add(&tr->list, &blktrans_majors);

	mtd_for_each_device(mtd)
		if (mtd->type != MTD_ABSENT)
			tr->add_mtd(tr, mtd);

	mutex_unlock(&mtd_table_mutex);

	return 0;
}

int deregister_mtd_blktrans(struct mtd_blktrans_ops *tr)
{
	struct mtd_blktrans_dev *dev, *next;

	mutex_lock(&mtd_table_mutex);


	/* Remove it from the list of active majors */
	list_del(&tr->list);

	list_for_each_entry_safe(dev, next, &tr->devs, list)
		tr->remove_dev(dev);

	unregister_blkdev(tr->major, tr->name);
	mutex_unlock(&mtd_table_mutex);

	BUG_ON(!list_empty(&tr->devs));
	return 0;
}

static void __exit mtd_blktrans_exit(void)
{
	/* No race here -- if someone's currently in register_mtd_blktrans
	   we're screwed anyway. */
	if (blktrans_notifier.list.next)
		unregister_mtd_user(&blktrans_notifier);
}

module_exit(mtd_blktrans_exit);

EXPORT_SYMBOL_GPL(register_mtd_blktrans);
EXPORT_SYMBOL_GPL(deregister_mtd_blktrans);
EXPORT_SYMBOL_GPL(add_mtd_blktrans_dev);
EXPORT_SYMBOL_GPL(del_mtd_blktrans_dev);

MODULE_AUTHOR("David Woodhouse <dwmw2@infradead.org>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Common interface to block layer for MTD 'translation layers'");
