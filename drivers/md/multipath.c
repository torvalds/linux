/*
 * multipath.c : Multiple Devices driver for Linux
 *
 * Copyright (C) 1999, 2000, 2001 Ingo Molnar, Red Hat
 *
 * Copyright (C) 1996, 1997, 1998 Ingo Molnar, Miguel de Icaza, Gadi Oxman
 *
 * MULTIPATH management functions.
 *
 * derived from raid1.c.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * You should have received a copy of the GNU General Public License
 * (for example /usr/src/linux/COPYING); if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/blkdev.h>
#include <linux/module.h>
#include <linux/raid/md_u.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include "md.h"
#include "multipath.h"

#define MAX_WORK_PER_DISK 128

#define	NR_RESERVED_BUFS	32

static int multipath_map (struct mpconf *conf)
{
	int i, disks = conf->raid_disks;

	/*
	 * Later we do read balancing on the read side
	 * now we use the first available disk.
	 */

	rcu_read_lock();
	for (i = 0; i < disks; i++) {
		struct md_rdev *rdev = rcu_dereference(conf->multipaths[i].rdev);
		if (rdev && test_bit(In_sync, &rdev->flags)) {
			atomic_inc(&rdev->nr_pending);
			rcu_read_unlock();
			return i;
		}
	}
	rcu_read_unlock();

	printk(KERN_ERR "multipath_map(): no more operational IO paths?\n");
	return (-1);
}

static void multipath_reschedule_retry (struct multipath_bh *mp_bh)
{
	unsigned long flags;
	struct mddev *mddev = mp_bh->mddev;
	struct mpconf *conf = mddev->private;

	spin_lock_irqsave(&conf->device_lock, flags);
	list_add(&mp_bh->retry_list, &conf->retry_list);
	spin_unlock_irqrestore(&conf->device_lock, flags);
	md_wakeup_thread(mddev->thread);
}

/*
 * multipath_end_bh_io() is called when we have finished servicing a multipathed
 * operation and are ready to return a success/failure code to the buffer
 * cache layer.
 */
static void multipath_end_bh_io (struct multipath_bh *mp_bh, int err)
{
	struct bio *bio = mp_bh->master_bio;
	struct mpconf *conf = mp_bh->mddev->private;

	bio_endio(bio, err);
	mempool_free(mp_bh, conf->pool);
}

static void multipath_end_request(struct bio *bio, int error)
{
	int uptodate = test_bit(BIO_UPTODATE, &bio->bi_flags);
	struct multipath_bh *mp_bh = bio->bi_private;
	struct mpconf *conf = mp_bh->mddev->private;
	struct md_rdev *rdev = conf->multipaths[mp_bh->path].rdev;

	if (uptodate)
		multipath_end_bh_io(mp_bh, 0);
	else if (!(bio->bi_rw & REQ_RAHEAD)) {
		/*
		 * oops, IO error:
		 */
		char b[BDEVNAME_SIZE];
		md_error (mp_bh->mddev, rdev);
		printk(KERN_ERR "multipath: %s: rescheduling sector %llu\n",
		       bdevname(rdev->bdev,b),
		       (unsigned long long)bio->bi_iter.bi_sector);
		multipath_reschedule_retry(mp_bh);
	} else
		multipath_end_bh_io(mp_bh, error);
	rdev_dec_pending(rdev, conf->mddev);
}

static void multipath_make_request(struct mddev *mddev, struct bio * bio)
{
	struct mpconf *conf = mddev->private;
	struct multipath_bh * mp_bh;
	struct multipath_info *multipath;

	if (unlikely(bio->bi_rw & REQ_FLUSH)) {
		md_flush_request(mddev, bio);
		return;
	}

	mp_bh = mempool_alloc(conf->pool, GFP_NOIO);

	mp_bh->master_bio = bio;
	mp_bh->mddev = mddev;

	mp_bh->path = multipath_map(conf);
	if (mp_bh->path < 0) {
		bio_endio(bio, -EIO);
		mempool_free(mp_bh, conf->pool);
		return;
	}
	multipath = conf->multipaths + mp_bh->path;

	mp_bh->bio = *bio;
	mp_bh->bio.bi_iter.bi_sector += multipath->rdev->data_offset;
	mp_bh->bio.bi_bdev = multipath->rdev->bdev;
	mp_bh->bio.bi_rw |= REQ_FAILFAST_TRANSPORT;
	mp_bh->bio.bi_end_io = multipath_end_request;
	mp_bh->bio.bi_private = mp_bh;
	generic_make_request(&mp_bh->bio);
	return;
}

static void multipath_status (struct seq_file *seq, struct mddev *mddev)
{
	struct mpconf *conf = mddev->private;
	int i;

	seq_printf (seq, " [%d/%d] [", conf->raid_disks,
		    conf->raid_disks - mddev->degraded);
	for (i = 0; i < conf->raid_disks; i++)
		seq_printf (seq, "%s",
			       conf->multipaths[i].rdev &&
			       test_bit(In_sync, &conf->multipaths[i].rdev->flags) ? "U" : "_");
	seq_printf (seq, "]");
}

static int multipath_congested(struct mddev *mddev, int bits)
{
	struct mpconf *conf = mddev->private;
	int i, ret = 0;

	rcu_read_lock();
	for (i = 0; i < mddev->raid_disks ; i++) {
		struct md_rdev *rdev = rcu_dereference(conf->multipaths[i].rdev);
		if (rdev && !test_bit(Faulty, &rdev->flags)) {
			struct request_queue *q = bdev_get_queue(rdev->bdev);

			ret |= bdi_congested(&q->backing_dev_info, bits);
			/* Just like multipath_map, we just check the
			 * first available device
			 */
			break;
		}
	}
	rcu_read_unlock();
	return ret;
}

/*
 * Careful, this can execute in IRQ contexts as well!
 */
static void multipath_error (struct mddev *mddev, struct md_rdev *rdev)
{
	struct mpconf *conf = mddev->private;
	char b[BDEVNAME_SIZE];

	if (conf->raid_disks - mddev->degraded <= 1) {
		/*
		 * Uh oh, we can do nothing if this is our last path, but
		 * first check if this is a queued request for a device
		 * which has just failed.
		 */
		printk(KERN_ALERT
		       "multipath: only one IO path left and IO error.\n");
		/* leave it active... it's all we have */
		return;
	}
	/*
	 * Mark disk as unusable
	 */
	if (test_and_clear_bit(In_sync, &rdev->flags)) {
		unsigned long flags;
		spin_lock_irqsave(&conf->device_lock, flags);
		mddev->degraded++;
		spin_unlock_irqrestore(&conf->device_lock, flags);
	}
	set_bit(Faulty, &rdev->flags);
	set_bit(MD_CHANGE_DEVS, &mddev->flags);
	printk(KERN_ALERT "multipath: IO failure on %s,"
	       " disabling IO path.\n"
	       "multipath: Operation continuing"
	       " on %d IO paths.\n",
	       bdevname(rdev->bdev, b),
	       conf->raid_disks - mddev->degraded);
}

static void print_multipath_conf (struct mpconf *conf)
{
	int i;
	struct multipath_info *tmp;

	printk("MULTIPATH conf printout:\n");
	if (!conf) {
		printk("(conf==NULL)\n");
		return;
	}
	printk(" --- wd:%d rd:%d\n", conf->raid_disks - conf->mddev->degraded,
			 conf->raid_disks);

	for (i = 0; i < conf->raid_disks; i++) {
		char b[BDEVNAME_SIZE];
		tmp = conf->multipaths + i;
		if (tmp->rdev)
			printk(" disk%d, o:%d, dev:%s\n",
				i,!test_bit(Faulty, &tmp->rdev->flags),
			       bdevname(tmp->rdev->bdev,b));
	}
}

static int multipath_add_disk(struct mddev *mddev, struct md_rdev *rdev)
{
	struct mpconf *conf = mddev->private;
	struct request_queue *q;
	int err = -EEXIST;
	int path;
	struct multipath_info *p;
	int first = 0;
	int last = mddev->raid_disks - 1;

	if (rdev->raid_disk >= 0)
		first = last = rdev->raid_disk;

	print_multipath_conf(conf);

	for (path = first; path <= last; path++)
		if ((p=conf->multipaths+path)->rdev == NULL) {
			q = rdev->bdev->bd_disk->queue;
			disk_stack_limits(mddev->gendisk, rdev->bdev,
					  rdev->data_offset << 9);

		/* as we don't honour merge_bvec_fn, we must never risk
		 * violating it, so limit ->max_segments to one, lying
		 * within a single page.
		 * (Note: it is very unlikely that a device with
		 * merge_bvec_fn will be involved in multipath.)
		 */
			if (q->merge_bvec_fn) {
				blk_queue_max_segments(mddev->queue, 1);
				blk_queue_segment_boundary(mddev->queue,
							   PAGE_CACHE_SIZE - 1);
			}

			spin_lock_irq(&conf->device_lock);
			mddev->degraded--;
			rdev->raid_disk = path;
			set_bit(In_sync, &rdev->flags);
			spin_unlock_irq(&conf->device_lock);
			rcu_assign_pointer(p->rdev, rdev);
			err = 0;
			md_integrity_add_rdev(rdev, mddev);
			break;
		}

	print_multipath_conf(conf);

	return err;
}

static int multipath_remove_disk(struct mddev *mddev, struct md_rdev *rdev)
{
	struct mpconf *conf = mddev->private;
	int err = 0;
	int number = rdev->raid_disk;
	struct multipath_info *p = conf->multipaths + number;

	print_multipath_conf(conf);

	if (rdev == p->rdev) {
		if (test_bit(In_sync, &rdev->flags) ||
		    atomic_read(&rdev->nr_pending)) {
			printk(KERN_ERR "hot-remove-disk, slot %d is identified"
			       " but is still operational!\n", number);
			err = -EBUSY;
			goto abort;
		}
		p->rdev = NULL;
		synchronize_rcu();
		if (atomic_read(&rdev->nr_pending)) {
			/* lost the race, try later */
			err = -EBUSY;
			p->rdev = rdev;
			goto abort;
		}
		err = md_integrity_register(mddev);
	}
abort:

	print_multipath_conf(conf);
	return err;
}

/*
 * This is a kernel thread which:
 *
 *	1.	Retries failed read operations on working multipaths.
 *	2.	Updates the raid superblock when problems encounter.
 *	3.	Performs writes following reads for array syncronising.
 */

static void multipathd(struct md_thread *thread)
{
	struct mddev *mddev = thread->mddev;
	struct multipath_bh *mp_bh;
	struct bio *bio;
	unsigned long flags;
	struct mpconf *conf = mddev->private;
	struct list_head *head = &conf->retry_list;

	md_check_recovery(mddev);
	for (;;) {
		char b[BDEVNAME_SIZE];
		spin_lock_irqsave(&conf->device_lock, flags);
		if (list_empty(head))
			break;
		mp_bh = list_entry(head->prev, struct multipath_bh, retry_list);
		list_del(head->prev);
		spin_unlock_irqrestore(&conf->device_lock, flags);

		bio = &mp_bh->bio;
		bio->bi_iter.bi_sector = mp_bh->master_bio->bi_iter.bi_sector;

		if ((mp_bh->path = multipath_map (conf))<0) {
			printk(KERN_ALERT "multipath: %s: unrecoverable IO read"
				" error for block %llu\n",
				bdevname(bio->bi_bdev,b),
				(unsigned long long)bio->bi_iter.bi_sector);
			multipath_end_bh_io(mp_bh, -EIO);
		} else {
			printk(KERN_ERR "multipath: %s: redirecting sector %llu"
				" to another IO path\n",
				bdevname(bio->bi_bdev,b),
				(unsigned long long)bio->bi_iter.bi_sector);
			*bio = *(mp_bh->master_bio);
			bio->bi_iter.bi_sector +=
				conf->multipaths[mp_bh->path].rdev->data_offset;
			bio->bi_bdev = conf->multipaths[mp_bh->path].rdev->bdev;
			bio->bi_rw |= REQ_FAILFAST_TRANSPORT;
			bio->bi_end_io = multipath_end_request;
			bio->bi_private = mp_bh;
			generic_make_request(bio);
		}
	}
	spin_unlock_irqrestore(&conf->device_lock, flags);
}

static sector_t multipath_size(struct mddev *mddev, sector_t sectors, int raid_disks)
{
	WARN_ONCE(sectors || raid_disks,
		  "%s does not support generic reshape\n", __func__);

	return mddev->dev_sectors;
}

static int multipath_run (struct mddev *mddev)
{
	struct mpconf *conf;
	int disk_idx;
	struct multipath_info *disk;
	struct md_rdev *rdev;
	int working_disks;

	if (md_check_no_bitmap(mddev))
		return -EINVAL;

	if (mddev->level != LEVEL_MULTIPATH) {
		printk("multipath: %s: raid level not set to multipath IO (%d)\n",
		       mdname(mddev), mddev->level);
		goto out;
	}
	/*
	 * copy the already verified devices into our private MULTIPATH
	 * bookkeeping area. [whatever we allocate in multipath_run(),
	 * should be freed in multipath_free()]
	 */

	conf = kzalloc(sizeof(struct mpconf), GFP_KERNEL);
	mddev->private = conf;
	if (!conf) {
		printk(KERN_ERR
			"multipath: couldn't allocate memory for %s\n",
			mdname(mddev));
		goto out;
	}

	conf->multipaths = kzalloc(sizeof(struct multipath_info)*mddev->raid_disks,
				   GFP_KERNEL);
	if (!conf->multipaths) {
		printk(KERN_ERR
			"multipath: couldn't allocate memory for %s\n",
			mdname(mddev));
		goto out_free_conf;
	}

	working_disks = 0;
	rdev_for_each(rdev, mddev) {
		disk_idx = rdev->raid_disk;
		if (disk_idx < 0 ||
		    disk_idx >= mddev->raid_disks)
			continue;

		disk = conf->multipaths + disk_idx;
		disk->rdev = rdev;
		disk_stack_limits(mddev->gendisk, rdev->bdev,
				  rdev->data_offset << 9);

		/* as we don't honour merge_bvec_fn, we must never risk
		 * violating it, not that we ever expect a device with
		 * a merge_bvec_fn to be involved in multipath */
		if (rdev->bdev->bd_disk->queue->merge_bvec_fn) {
			blk_queue_max_segments(mddev->queue, 1);
			blk_queue_segment_boundary(mddev->queue,
						   PAGE_CACHE_SIZE - 1);
		}

		if (!test_bit(Faulty, &rdev->flags))
			working_disks++;
	}

	conf->raid_disks = mddev->raid_disks;
	conf->mddev = mddev;
	spin_lock_init(&conf->device_lock);
	INIT_LIST_HEAD(&conf->retry_list);

	if (!working_disks) {
		printk(KERN_ERR "multipath: no operational IO paths for %s\n",
			mdname(mddev));
		goto out_free_conf;
	}
	mddev->degraded = conf->raid_disks - working_disks;

	conf->pool = mempool_create_kmalloc_pool(NR_RESERVED_BUFS,
						 sizeof(struct multipath_bh));
	if (conf->pool == NULL) {
		printk(KERN_ERR
			"multipath: couldn't allocate memory for %s\n",
			mdname(mddev));
		goto out_free_conf;
	}

	{
		mddev->thread = md_register_thread(multipathd, mddev,
						   "multipath");
		if (!mddev->thread) {
			printk(KERN_ERR "multipath: couldn't allocate thread"
				" for %s\n", mdname(mddev));
			goto out_free_conf;
		}
	}

	printk(KERN_INFO
		"multipath: array %s active with %d out of %d IO paths\n",
		mdname(mddev), conf->raid_disks - mddev->degraded,
	       mddev->raid_disks);
	/*
	 * Ok, everything is just fine now
	 */
	md_set_array_sectors(mddev, multipath_size(mddev, 0, 0));

	if (md_integrity_register(mddev))
		goto out_free_conf;

	return 0;

out_free_conf:
	if (conf->pool)
		mempool_destroy(conf->pool);
	kfree(conf->multipaths);
	kfree(conf);
	mddev->private = NULL;
out:
	return -EIO;
}

static void multipath_free(struct mddev *mddev, void *priv)
{
	struct mpconf *conf = priv;

	mempool_destroy(conf->pool);
	kfree(conf->multipaths);
	kfree(conf);
}

static struct md_personality multipath_personality =
{
	.name		= "multipath",
	.level		= LEVEL_MULTIPATH,
	.owner		= THIS_MODULE,
	.make_request	= multipath_make_request,
	.run		= multipath_run,
	.free		= multipath_free,
	.status		= multipath_status,
	.error_handler	= multipath_error,
	.hot_add_disk	= multipath_add_disk,
	.hot_remove_disk= multipath_remove_disk,
	.size		= multipath_size,
	.congested	= multipath_congested,
};

static int __init multipath_init (void)
{
	return register_md_personality (&multipath_personality);
}

static void __exit multipath_exit (void)
{
	unregister_md_personality (&multipath_personality);
}

module_init(multipath_init);
module_exit(multipath_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("simple multi-path personality for MD");
MODULE_ALIAS("md-personality-7"); /* MULTIPATH */
MODULE_ALIAS("md-multipath");
MODULE_ALIAS("md-level--4");
