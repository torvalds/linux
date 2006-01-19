/* Copyright (c) 2004 Coraid, Inc.  See COPYING for GPL terms. */
/*
 * aoedev.c
 * AoE device utility functions; maintains device list.
 */

#include <linux/hdreg.h>
#include <linux/blkdev.h>
#include <linux/netdevice.h>
#include "aoe.h"

static struct aoedev *devlist;
static spinlock_t devlist_lock;

int
aoedev_isbusy(struct aoedev *d)
{
	struct frame *f, *e;

	f = d->frames;
	e = f + d->nframes;
	do {
		if (f->tag != FREETAG) {
			printk(KERN_DEBUG "aoe: %ld.%ld isbusy\n",
				d->aoemajor, d->aoeminor);
			return 1;
		}
	} while (++f < e);

	return 0;
}

struct aoedev *
aoedev_by_aoeaddr(int maj, int min)
{
	struct aoedev *d;
	ulong flags;

	spin_lock_irqsave(&devlist_lock, flags);

	for (d=devlist; d; d=d->next)
		if (d->aoemajor == maj && d->aoeminor == min)
			break;

	spin_unlock_irqrestore(&devlist_lock, flags);
	return d;
}

static void
dummy_timer(ulong vp)
{
	struct aoedev *d;

	d = (struct aoedev *)vp;
	if (d->flags & DEVFL_TKILL)
		return;
	d->timer.expires = jiffies + HZ;
	add_timer(&d->timer);
}

/* called with devlist lock held */
static struct aoedev *
aoedev_newdev(ulong nframes)
{
	struct aoedev *d;
	struct frame *f, *e;

	d = kzalloc(sizeof *d, GFP_ATOMIC);
	if (d == NULL)
		return NULL;
	f = kcalloc(nframes, sizeof *f, GFP_ATOMIC);
	if (f == NULL) {
		kfree(d);
		return NULL;
	}

	INIT_WORK(&d->work, aoecmd_sleepwork, d);

	d->nframes = nframes;
	d->frames = f;
	e = f + nframes;
	for (; f<e; f++)
		f->tag = FREETAG;

	spin_lock_init(&d->lock);
	init_timer(&d->timer);
	d->timer.data = (ulong) d;
	d->timer.function = dummy_timer;
	d->timer.expires = jiffies + HZ;
	add_timer(&d->timer);
	d->bufpool = NULL;	/* defer to aoeblk_gdalloc */
	INIT_LIST_HEAD(&d->bufq);
	d->next = devlist;
	devlist = d;

	return d;
}

void
aoedev_downdev(struct aoedev *d)
{
	struct frame *f, *e;
	struct buf *buf;
	struct bio *bio;

	f = d->frames;
	e = f + d->nframes;
	for (; f<e; f->tag = FREETAG, f->buf = NULL, f++) {
		if (f->tag == FREETAG || f->buf == NULL)
			continue;
		buf = f->buf;
		bio = buf->bio;
		if (--buf->nframesout == 0) {
			mempool_free(buf, d->bufpool);
			bio_endio(bio, bio->bi_size, -EIO);
		}
	}
	d->inprocess = NULL;

	while (!list_empty(&d->bufq)) {
		buf = container_of(d->bufq.next, struct buf, bufs);
		list_del(d->bufq.next);
		bio = buf->bio;
		mempool_free(buf, d->bufpool);
		bio_endio(bio, bio->bi_size, -EIO);
	}

	if (d->gd)
		d->gd->capacity = 0;

	d->flags &= ~(DEVFL_UP | DEVFL_PAUSE);
}

/* find it or malloc it */
struct aoedev *
aoedev_by_sysminor_m(ulong sysminor, ulong bufcnt)
{
	struct aoedev *d;
	ulong flags;

	spin_lock_irqsave(&devlist_lock, flags);

	for (d=devlist; d; d=d->next)
		if (d->sysminor == sysminor)
			break;

	if (d == NULL) {
		d = aoedev_newdev(bufcnt);
	 	if (d == NULL) {
			spin_unlock_irqrestore(&devlist_lock, flags);
			printk(KERN_INFO "aoe: aoedev_set: aoedev_newdev failure.\n");
			return NULL;
		}
		d->sysminor = sysminor;
		d->aoemajor = AOEMAJOR(sysminor);
		d->aoeminor = AOEMINOR(sysminor);
	}

	spin_unlock_irqrestore(&devlist_lock, flags);
	return d;
}

static void
aoedev_freedev(struct aoedev *d)
{
	if (d->gd) {
		aoedisk_rm_sysfs(d);
		del_gendisk(d->gd);
		put_disk(d->gd);
	}
	kfree(d->frames);
	if (d->bufpool)
		mempool_destroy(d->bufpool);
	kfree(d);
}

void
aoedev_exit(void)
{
	struct aoedev *d;
	ulong flags;

	flush_scheduled_work();

	while ((d = devlist)) {
		devlist = d->next;

		spin_lock_irqsave(&d->lock, flags);
		aoedev_downdev(d);
		d->flags |= DEVFL_TKILL;
		spin_unlock_irqrestore(&d->lock, flags);

		del_timer_sync(&d->timer);
		aoedev_freedev(d);
	}
}

int __init
aoedev_init(void)
{
	spin_lock_init(&devlist_lock);
	return 0;
}

