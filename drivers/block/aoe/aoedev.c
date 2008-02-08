/* Copyright (c) 2006 Coraid, Inc.  See COPYING for GPL terms. */
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
	struct aoetgt **t, **te;
	struct frame *f, *e;

	t = d->targets;
	te = t + NTARGETS;
	for (; t < te && *t; t++) {
		f = (*t)->frames;
		e = f + (*t)->nframes;
		for (; f < e; f++)
			if (f->tag != FREETAG)
				return 1;
	}
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

void
aoedev_downdev(struct aoedev *d)
{
	struct aoetgt **t, **te;
	struct frame *f, *e;
	struct buf *buf;
	struct bio *bio;

	t = d->targets;
	te = t + NTARGETS;
	for (; t < te && *t; t++) {
		f = (*t)->frames;
		e = f + (*t)->nframes;
		for (; f < e; f->tag = FREETAG, f->buf = NULL, f++) {
			if (f->tag == FREETAG || f->buf == NULL)
				continue;
			buf = f->buf;
			bio = buf->bio;
			if (--buf->nframesout == 0
			&& buf != d->inprocess) {
				mempool_free(buf, d->bufpool);
				bio_endio(bio, -EIO);
			}
		}
		(*t)->maxout = (*t)->nframes;
		(*t)->nout = 0;
	}
	buf = d->inprocess;
	if (buf) {
		bio = buf->bio;
		mempool_free(buf, d->bufpool);
		bio_endio(bio, -EIO);
	}
	d->inprocess = NULL;
	d->htgt = NULL;

	while (!list_empty(&d->bufq)) {
		buf = container_of(d->bufq.next, struct buf, bufs);
		list_del(d->bufq.next);
		bio = buf->bio;
		mempool_free(buf, d->bufpool);
		bio_endio(bio, -EIO);
	}

	if (d->gd)
		d->gd->capacity = 0;

	d->flags &= ~DEVFL_UP;
}

/* find it or malloc it */
struct aoedev *
aoedev_by_sysminor_m(ulong sysminor)
{
	struct aoedev *d;
	ulong flags;

	spin_lock_irqsave(&devlist_lock, flags);

	for (d=devlist; d; d=d->next)
		if (d->sysminor == sysminor)
			break;
	if (d)
		goto out;
	d = kcalloc(1, sizeof *d, GFP_ATOMIC);
	if (!d)
		goto out;
	INIT_WORK(&d->work, aoecmd_sleepwork);
	spin_lock_init(&d->lock);
	init_timer(&d->timer);
	d->timer.data = (ulong) d;
	d->timer.function = dummy_timer;
	d->timer.expires = jiffies + HZ;
	add_timer(&d->timer);
	d->bufpool = NULL;	/* defer to aoeblk_gdalloc */
	d->tgt = d->targets;
	INIT_LIST_HEAD(&d->bufq);
	d->sysminor = sysminor;
	d->aoemajor = AOEMAJOR(sysminor);
	d->aoeminor = AOEMINOR(sysminor);
	d->mintimer = MINTIMER;
	d->next = devlist;
	devlist = d;
 out:
	spin_unlock_irqrestore(&devlist_lock, flags);
	return d;
}

static void
freetgt(struct aoetgt *t)
{
	struct frame *f, *e;

	f = t->frames;
	e = f + t->nframes;
	for (; f < e; f++) {
		skb_shinfo(f->skb)->nr_frags = 0;
		dev_kfree_skb(f->skb);
	}
	kfree(t->frames);
	kfree(t);
}

static void
aoedev_freedev(struct aoedev *d)
{
	struct aoetgt **t, **e;

	if (d->gd) {
		aoedisk_rm_sysfs(d);
		del_gendisk(d->gd);
		put_disk(d->gd);
	}
	t = d->targets;
	e = t + NTARGETS;
	for (; t < e && *t; t++)
		freetgt(*t);
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

