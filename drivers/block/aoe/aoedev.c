/* Copyright (c) 2007 Coraid, Inc.  See COPYING for GPL terms. */
/*
 * aoedev.c
 * AoE device utility functions; maintains device list.
 */

#include <linux/hdreg.h>
#include <linux/blkdev.h>
#include <linux/netdevice.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include "aoe.h"

static void dummy_timer(ulong);
static void aoedev_freedev(struct aoedev *);
static void freetgt(struct aoedev *d, struct aoetgt *t);
static void skbpoolfree(struct aoedev *d);

static struct aoedev *devlist;
static DEFINE_SPINLOCK(devlist_lock);

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
		set_capacity(d->gd, 0);

	d->flags &= ~DEVFL_UP;
}

static void
aoedev_freedev(struct aoedev *d)
{
	struct aoetgt **t, **e;

	cancel_work_sync(&d->work);
	if (d->gd) {
		aoedisk_rm_sysfs(d);
		del_gendisk(d->gd);
		put_disk(d->gd);
	}
	t = d->targets;
	e = t + NTARGETS;
	for (; t < e && *t; t++)
		freetgt(d, *t);
	if (d->bufpool)
		mempool_destroy(d->bufpool);
	skbpoolfree(d);
	blk_cleanup_queue(d->blkq);
	kfree(d);
}

int
aoedev_flush(const char __user *str, size_t cnt)
{
	ulong flags;
	struct aoedev *d, **dd;
	struct aoedev *rmd = NULL;
	char buf[16];
	int all = 0;

	if (cnt >= 3) {
		if (cnt > sizeof buf)
			cnt = sizeof buf;
		if (copy_from_user(buf, str, cnt))
			return -EFAULT;
		all = !strncmp(buf, "all", 3);
	}

	spin_lock_irqsave(&devlist_lock, flags);
	dd = &devlist;
	while ((d = *dd)) {
		spin_lock(&d->lock);
		if ((!all && (d->flags & DEVFL_UP))
		|| (d->flags & (DEVFL_GDALLOC|DEVFL_NEWSIZE))
		|| d->nopen) {
			spin_unlock(&d->lock);
			dd = &d->next;
			continue;
		}
		*dd = d->next;
		aoedev_downdev(d);
		d->flags |= DEVFL_TKILL;
		spin_unlock(&d->lock);
		d->next = rmd;
		rmd = d;
	}
	spin_unlock_irqrestore(&devlist_lock, flags);
	while ((d = rmd)) {
		rmd = d->next;
		del_timer_sync(&d->timer);
		aoedev_freedev(d);	/* must be able to sleep */
	}
	return 0;
}

/* I'm not really sure that this is a realistic problem, but if the
network driver goes gonzo let's just leak memory after complaining. */
static void
skbfree(struct sk_buff *skb)
{
	enum { Sms = 100, Tms = 3*1000};
	int i = Tms / Sms;

	if (skb == NULL)
		return;
	while (atomic_read(&skb_shinfo(skb)->dataref) != 1 && i-- > 0)
		msleep(Sms);
	if (i < 0) {
		printk(KERN_ERR
			"aoe: %s holds ref: %s\n",
			skb->dev ? skb->dev->name : "netif",
			"cannot free skb -- memory leaked.");
		return;
	}
	skb_shinfo(skb)->nr_frags = skb->data_len = 0;
	skb_trim(skb, 0);
	dev_kfree_skb(skb);
}

static void
skbpoolfree(struct aoedev *d)
{
	struct sk_buff *skb, *tmp;

	skb_queue_walk_safe(&d->skbpool, skb, tmp)
		skbfree(skb);

	__skb_queue_head_init(&d->skbpool);
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
	skb_queue_head_init(&d->sendq);
	skb_queue_head_init(&d->skbpool);
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
freetgt(struct aoedev *d, struct aoetgt *t)
{
	struct frame *f, *e;

	f = t->frames;
	e = f + t->nframes;
	for (; f < e; f++)
		skbfree(f->skb);
	kfree(t->frames);
	kfree(t);
}

void
aoedev_exit(void)
{
	struct aoedev *d;
	ulong flags;

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
	return 0;
}
