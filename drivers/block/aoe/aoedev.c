/* Copyright (c) 2012 Coraid, Inc.  See COPYING for GPL terms. */
/*
 * aoedev.c
 * AoE device utility functions; maintains device list.
 */

#include <linux/hdreg.h>
#include <linux/blkdev.h>
#include <linux/netdevice.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/bitmap.h>
#include <linux/kdev_t.h>
#include <linux/moduleparam.h>
#include "aoe.h"

static void dummy_timer(ulong);
static void aoedev_freedev(struct aoedev *);
static void freetgt(struct aoedev *d, struct aoetgt *t);
static void skbpoolfree(struct aoedev *d);

static int aoe_dyndevs = 1;
module_param(aoe_dyndevs, int, 0644);
MODULE_PARM_DESC(aoe_dyndevs, "Use dynamic minor numbers for devices.");

static struct aoedev *devlist;
static DEFINE_SPINLOCK(devlist_lock);

/* Because some systems will have one, many, or no
 *   - partitions,
 *   - slots per shelf,
 *   - or shelves,
 * we need some flexibility in the way the minor numbers
 * are allocated.  So they are dynamic.
 */
#define N_DEVS ((1U<<MINORBITS)/AOE_PARTITIONS)

static DEFINE_SPINLOCK(used_minors_lock);
static DECLARE_BITMAP(used_minors, N_DEVS);

static int
minor_get_dyn(ulong *sysminor)
{
	ulong flags;
	ulong n;
	int error = 0;

	spin_lock_irqsave(&used_minors_lock, flags);
	n = find_first_zero_bit(used_minors, N_DEVS);
	if (n < N_DEVS)
		set_bit(n, used_minors);
	else
		error = -1;
	spin_unlock_irqrestore(&used_minors_lock, flags);

	*sysminor = n * AOE_PARTITIONS;
	return error;
}

static int
minor_get_static(ulong *sysminor, ulong aoemaj, int aoemin)
{
	ulong flags;
	ulong n;
	int error = 0;
	enum {
		/* for backwards compatibility when !aoe_dyndevs,
		 * a static number of supported slots per shelf */
		NPERSHELF = 16,
	};

	n = aoemaj * NPERSHELF + aoemin;
	if (aoemin >= NPERSHELF || n >= N_DEVS) {
		pr_err("aoe: %s with e%ld.%d\n",
			"cannot use static minor device numbers",
			aoemaj, aoemin);
		error = -1;
	} else {
		spin_lock_irqsave(&used_minors_lock, flags);
		if (test_bit(n, used_minors)) {
			pr_err("aoe: %s %lu\n",
				"existing device already has static minor number",
				n);
			error = -1;
		} else
			set_bit(n, used_minors);
		spin_unlock_irqrestore(&used_minors_lock, flags);
	}

	*sysminor = n;
	return error;
}

static int
minor_get(ulong *sysminor, ulong aoemaj, int aoemin)
{
	if (aoe_dyndevs)
		return minor_get_dyn(sysminor);
	else
		return minor_get_static(sysminor, aoemaj, aoemin);
}

static void
minor_free(ulong minor)
{
	ulong flags;

	minor /= AOE_PARTITIONS;
	BUG_ON(minor >= N_DEVS);

	spin_lock_irqsave(&used_minors_lock, flags);
	BUG_ON(!test_bit(minor, used_minors));
	clear_bit(minor, used_minors);
	spin_unlock_irqrestore(&used_minors_lock, flags);
}

/*
 * Users who grab a pointer to the device with aoedev_by_aoeaddr
 * automatically get a reference count and must be responsible
 * for performing a aoedev_put.  With the addition of async
 * kthread processing I'm no longer confident that we can
 * guarantee consistency in the face of device flushes.
 *
 * For the time being, we only bother to add extra references for
 * frames sitting on the iocq.  When the kthreads finish processing
 * these frames, they will aoedev_put the device.
 */

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
	minor_free(d->sysminor);
	kfree(d);
}

/* return whether the user asked for this particular
 * device to be flushed
 */
static int
user_req(char *s, size_t slen, struct aoedev *d)
{
	char *p;
	size_t lim;

	if (!d->gd)
		return 0;
	p = strrchr(d->gd->disk_name, '/');
	if (!p)
		p = d->gd->disk_name;
	else
		p += 1;
	lim = sizeof(d->gd->disk_name);
	lim -= p - d->gd->disk_name;
	if (slen < lim)
		lim = slen;

	return !strncmp(s, p, lim);
}

int
aoedev_flush(const char __user *str, size_t cnt)
{
	ulong flags;
	struct aoedev *d, **dd;
	struct aoedev *rmd = NULL;
	char buf[16];
	int all = 0;
	int specified = 0;	/* flush a specific device */

	if (cnt >= 3) {
		if (cnt > sizeof buf)
			cnt = sizeof buf;
		if (copy_from_user(buf, str, cnt))
			return -EFAULT;
		all = !strncmp(buf, "all", 3);
		if (!all)
			specified = 1;
	}

	spin_lock_irqsave(&devlist_lock, flags);
	dd = &devlist;
	while ((d = *dd)) {
		spin_lock(&d->lock);
		if (specified) {
			if (!user_req(buf, cnt, d))
				goto skip;
		} else if ((!all && (d->flags & DEVFL_UP))
		|| (d->flags & (DEVFL_GDALLOC|DEVFL_NEWSIZE))
		|| d->nopen
		|| d->ref)
			goto skip;

		*dd = d->next;
		aoedev_downdev(d);
		d->flags |= DEVFL_TKILL;
		spin_unlock(&d->lock);
		d->next = rmd;
		rmd = d;
		continue;
skip:
		spin_unlock(&d->lock);
		dd = &d->next;
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

/* find it or allocate it */
struct aoedev *
aoedev_by_aoeaddr(ulong maj, int min, int do_alloc)
{
	struct aoedev *d;
	int i;
	ulong flags;
	ulong sysminor;

	spin_lock_irqsave(&devlist_lock, flags);

	for (d=devlist; d; d=d->next)
		if (d->aoemajor == maj && d->aoeminor == min) {
			d->ref++;
			break;
		}
	if (d || !do_alloc || minor_get(&sysminor, maj, min) < 0)
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
	d->aoemajor = maj;
	d->aoeminor = min;
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
	struct aoeif *ifp;

	for (ifp = t->ifs; ifp < &t->ifs[NAOEIFS]; ++ifp) {
		if (!ifp->nd)
			break;
		dev_put(ifp->nd);
	}

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
