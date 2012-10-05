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

/*
 * Users who grab a pointer to the device with aoedev_by_aoeaddr or
 * aoedev_by_sysminor_m automatically get a reference count and must
 * be responsible for performing a aoedev_put.  With the addition of
 * async kthread processing I'm no longer confident that we can
 * guarantee consistency in the face of device flushes.
 *
 * For the time being, we only bother to add extra references for
 * frames sitting on the iocq.  When the kthreads finish processing
 * these frames, they will aoedev_put the device.
 */
struct aoedev *
aoedev_by_aoeaddr(int maj, int min)
{
	struct aoedev *d;
	ulong flags;

	spin_lock_irqsave(&devlist_lock, flags);

	for (d=devlist; d; d=d->next)
		if (d->aoemajor == maj && d->aoeminor == min) {
			d->ref++;
			break;
		}

	spin_unlock_irqrestore(&devlist_lock, flags);
	return d;
}

void
aoedev_put(struct aoedev *d)
{
	ulong flags;

	spin_lock_irqsave(&devlist_lock, flags);
	d->ref--;
	spin_unlock_irqrestore(&devlist_lock, flags);
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

static void
aoe_failip(struct aoedev *d)
{
	struct request *rq;
	struct bio *bio;
	unsigned long n;

	aoe_failbuf(d, d->ip.buf);

	rq = d->ip.rq;
	if (rq == NULL)
		return;
	while ((bio = d->ip.nxbio)) {
		clear_bit(BIO_UPTODATE, &bio->bi_flags);
		d->ip.nxbio = bio->bi_next;
		n = (unsigned long) rq->special;
		rq->special = (void *) --n;
	}
	if ((unsigned long) rq->special == 0)
		aoe_end_request(d, rq, 0);
}

void
aoedev_downdev(struct aoedev *d)
{
	struct aoetgt *t, **tt, **te;
	struct frame *f;
	struct list_head *head, *pos, *nx;
	struct request *rq;
	int i;

	d->flags &= ~DEVFL_UP;

	/* clean out active buffers */
	for (i = 0; i < NFACTIVE; i++) {
		head = &d->factive[i];
		list_for_each_safe(pos, nx, head) {
			f = list_entry(pos, struct frame, head);
			list_del(pos);
			if (f->buf) {
				f->buf->nframesout--;
				aoe_failbuf(d, f->buf);
			}
			aoe_freetframe(f);
		}
	}
	/* reset window dressings */
	tt = d->targets;
	te = tt + NTARGETS;
	for (; tt < te && (t = *tt); tt++) {
		t->maxout = t->nframes;
		t->nout = 0;
	}

	/* clean out the in-process request (if any) */
	aoe_failip(d);
	d->htgt = NULL;

	/* fast fail all pending I/O */
	if (d->blkq) {
		while ((rq = blk_peek_request(d->blkq))) {
			blk_start_request(rq);
			aoe_end_request(d, rq, 1);
		}
	}

	if (d->gd)
		set_capacity(d->gd, 0);
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
		blk_cleanup_queue(d->blkq);
	}
	t = d->targets;
	e = t + NTARGETS;
	for (; t < e && *t; t++)
		freetgt(d, *t);
	if (d->bufpool)
		mempool_destroy(d->bufpool);
	skbpoolfree(d);
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
		|| d->nopen
		|| d->ref) {
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

/* This has been confirmed to occur once with Tms=3*1000 due to the
 * driver changing link and not processing its transmit ring.  The
 * problem is hard enough to solve by returning an error that I'm
 * still punting on "solving" this.
 */
static void
skbfree(struct sk_buff *skb)
{
	enum { Sms = 250, Tms = 30 * 1000};
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
	skb->truesize -= skb->data_len;
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
	int i;
	ulong flags;

	spin_lock_irqsave(&devlist_lock, flags);

	for (d=devlist; d; d=d->next)
		if (d->sysminor == sysminor) {
			d->ref++;
			break;
		}
	if (d)
		goto out;
	d = kcalloc(1, sizeof *d, GFP_ATOMIC);
	if (!d)
		goto out;
	INIT_WORK(&d->work, aoecmd_sleepwork);
	spin_lock_init(&d->lock);
	skb_queue_head_init(&d->skbpool);
	init_timer(&d->timer);
	d->timer.data = (ulong) d;
	d->timer.function = dummy_timer;
	d->timer.expires = jiffies + HZ;
	add_timer(&d->timer);
	d->bufpool = NULL;	/* defer to aoeblk_gdalloc */
	d->tgt = d->targets;
	d->ref = 1;
	for (i = 0; i < NFACTIVE; i++)
		INIT_LIST_HEAD(&d->factive[i]);
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
	struct frame *f;
	struct list_head *pos, *nx, *head;

	head = &t->ffree;
	list_for_each_safe(pos, nx, head) {
		list_del(pos);
		f = list_entry(pos, struct frame, head);
		skbfree(f->skb);
		kfree(f);
	}
	kfree(t);
}

void
aoedev_exit(void)
{
	struct aoedev *d;
	ulong flags;

	aoe_flush_iocq();
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
