/* Copyright (c) 2006 Coraid, Inc.  See COPYING for GPL terms. */
/*
 * aoecmd.c
 * Filesystem request handling methods
 */

#include <linux/hdreg.h>
#include <linux/blkdev.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/genhd.h>
#include <linux/moduleparam.h>
#include <net/net_namespace.h>
#include <asm/unaligned.h>
#include "aoe.h"

static int aoe_deadsecs = 60 * 3;
module_param(aoe_deadsecs, int, 0644);
MODULE_PARM_DESC(aoe_deadsecs, "After aoe_deadsecs seconds, give up and fail dev.");

static int aoe_maxout = 16;
module_param(aoe_maxout, int, 0644);
MODULE_PARM_DESC(aoe_maxout,
	"Only aoe_maxout outstanding packets for every MAC on eX.Y.");

static struct sk_buff *
new_skb(ulong len)
{
	struct sk_buff *skb;

	skb = alloc_skb(len, GFP_ATOMIC);
	if (skb) {
		skb_reset_mac_header(skb);
		skb_reset_network_header(skb);
		skb->protocol = __constant_htons(ETH_P_AOE);
		skb->priority = 0;
		skb->next = skb->prev = NULL;

		/* tell the network layer not to perform IP checksums
		 * or to get the NIC to do it
		 */
		skb->ip_summed = CHECKSUM_NONE;
	}
	return skb;
}

static struct frame *
getframe(struct aoetgt *t, int tag)
{
	struct frame *f, *e;

	f = t->frames;
	e = f + t->nframes;
	for (; f<e; f++)
		if (f->tag == tag)
			return f;
	return NULL;
}

/*
 * Leave the top bit clear so we have tagspace for userland.
 * The bottom 16 bits are the xmit tick for rexmit/rttavg processing.
 * This driver reserves tag -1 to mean "unused frame."
 */
static int
newtag(struct aoetgt *t)
{
	register ulong n;

	n = jiffies & 0xffff;
	return n |= (++t->lasttag & 0x7fff) << 16;
}

static int
aoehdr_atainit(struct aoedev *d, struct aoetgt *t, struct aoe_hdr *h)
{
	u32 host_tag = newtag(t);

	memcpy(h->src, t->ifp->nd->dev_addr, sizeof h->src);
	memcpy(h->dst, t->addr, sizeof h->dst);
	h->type = __constant_cpu_to_be16(ETH_P_AOE);
	h->verfl = AOE_HVER;
	h->major = cpu_to_be16(d->aoemajor);
	h->minor = d->aoeminor;
	h->cmd = AOECMD_ATA;
	h->tag = cpu_to_be32(host_tag);

	return host_tag;
}

static inline void
put_lba(struct aoe_atahdr *ah, sector_t lba)
{
	ah->lba0 = lba;
	ah->lba1 = lba >>= 8;
	ah->lba2 = lba >>= 8;
	ah->lba3 = lba >>= 8;
	ah->lba4 = lba >>= 8;
	ah->lba5 = lba >>= 8;
}

static void
ifrotate(struct aoetgt *t)
{
	t->ifp++;
	if (t->ifp >= &t->ifs[NAOEIFS] || t->ifp->nd == NULL)
		t->ifp = t->ifs;
	if (t->ifp->nd == NULL) {
		printk(KERN_INFO "aoe: no interface to rotate to\n");
		BUG();
	}
}

static void
skb_pool_put(struct aoedev *d, struct sk_buff *skb)
{
	if (!d->skbpool_hd)
		d->skbpool_hd = skb;
	else
		d->skbpool_tl->next = skb;
	d->skbpool_tl = skb;
}

static struct sk_buff *
skb_pool_get(struct aoedev *d)
{
	struct sk_buff *skb;

	skb = d->skbpool_hd;
	if (skb && atomic_read(&skb_shinfo(skb)->dataref) == 1) {
		d->skbpool_hd = skb->next;
		skb->next = NULL;
		return skb;
	}
	if (d->nskbpool < NSKBPOOLMAX
	&& (skb = new_skb(ETH_ZLEN))) {
		d->nskbpool++;
		return skb;
	}
	return NULL;
}

/* freeframe is where we do our load balancing so it's a little hairy. */
static struct frame *
freeframe(struct aoedev *d)
{
	struct frame *f, *e, *rf;
	struct aoetgt **t;
	struct sk_buff *skb;

	if (d->targets[0] == NULL) {	/* shouldn't happen, but I'm paranoid */
		printk(KERN_ERR "aoe: NULL TARGETS!\n");
		return NULL;
	}
	t = d->tgt;
	t++;
	if (t >= &d->targets[NTARGETS] || !*t)
		t = d->targets;
	for (;;) {
		if ((*t)->nout < (*t)->maxout
		&& t != d->htgt
		&& (*t)->ifp->nd) {
			rf = NULL;
			f = (*t)->frames;
			e = f + (*t)->nframes;
			for (; f < e; f++) {
				if (f->tag != FREETAG)
					continue;
				skb = f->skb;
				if (!skb
				&& !(f->skb = skb = new_skb(ETH_ZLEN)))
					continue;
				if (atomic_read(&skb_shinfo(skb)->dataref)
					!= 1) {
					if (!rf)
						rf = f;
					continue;
				}
gotone:				skb_shinfo(skb)->nr_frags = skb->data_len = 0;
				skb_trim(skb, 0);
				d->tgt = t;
				ifrotate(*t);
				return f;
			}
			/* Work can be done, but the network layer is
			   holding our precious packets.  Try to grab
			   one from the pool. */
			f = rf;
			if (f == NULL) {	/* more paranoia */
				printk(KERN_ERR
					"aoe: freeframe: %s.\n",
					"unexpected null rf");
				d->flags |= DEVFL_KICKME;
				return NULL;
			}
			skb = skb_pool_get(d);
			if (skb) {
				skb_pool_put(d, f->skb);
				f->skb = skb;
				goto gotone;
			}
			(*t)->dataref++;
			if ((*t)->nout == 0)
				d->flags |= DEVFL_KICKME;
		}
		if (t == d->tgt)	/* we've looped and found nada */
			break;
		t++;
		if (t >= &d->targets[NTARGETS] || !*t)
			t = d->targets;
	}
	return NULL;
}

static int
aoecmd_ata_rw(struct aoedev *d)
{
	struct frame *f;
	struct aoe_hdr *h;
	struct aoe_atahdr *ah;
	struct buf *buf;
	struct bio_vec *bv;
	struct aoetgt *t;
	struct sk_buff *skb;
	ulong bcnt;
	char writebit, extbit;

	writebit = 0x10;
	extbit = 0x4;

	f = freeframe(d);
	if (f == NULL)
		return 0;
	t = *d->tgt;
	buf = d->inprocess;
	bv = buf->bv;
	bcnt = t->ifp->maxbcnt;
	if (bcnt == 0)
		bcnt = DEFAULTBCNT;
	if (bcnt > buf->bv_resid)
		bcnt = buf->bv_resid;
	/* initialize the headers & frame */
	skb = f->skb;
	h = (struct aoe_hdr *) skb_mac_header(skb);
	ah = (struct aoe_atahdr *) (h+1);
	skb_put(skb, sizeof *h + sizeof *ah);
	memset(h, 0, skb->len);
	f->tag = aoehdr_atainit(d, t, h);
	t->nout++;
	f->waited = 0;
	f->buf = buf;
	f->bufaddr = page_address(bv->bv_page) + buf->bv_off;
	f->bcnt = bcnt;
	f->lba = buf->sector;

	/* set up ata header */
	ah->scnt = bcnt >> 9;
	put_lba(ah, buf->sector);
	if (d->flags & DEVFL_EXT) {
		ah->aflags |= AOEAFL_EXT;
	} else {
		extbit = 0;
		ah->lba3 &= 0x0f;
		ah->lba3 |= 0xe0;	/* LBA bit + obsolete 0xa0 */
	}
	if (bio_data_dir(buf->bio) == WRITE) {
		skb_fill_page_desc(skb, 0, bv->bv_page, buf->bv_off, bcnt);
		ah->aflags |= AOEAFL_WRITE;
		skb->len += bcnt;
		skb->data_len = bcnt;
		t->wpkts++;
	} else {
		t->rpkts++;
		writebit = 0;
	}

	ah->cmdstat = WIN_READ | writebit | extbit;

	/* mark all tracking fields and load out */
	buf->nframesout += 1;
	buf->bv_off += bcnt;
	buf->bv_resid -= bcnt;
	buf->resid -= bcnt;
	buf->sector += bcnt >> 9;
	if (buf->resid == 0) {
		d->inprocess = NULL;
	} else if (buf->bv_resid == 0) {
		buf->bv = ++bv;
		buf->bv_resid = bv->bv_len;
		WARN_ON(buf->bv_resid == 0);
		buf->bv_off = bv->bv_offset;
	}

	skb->dev = t->ifp->nd;
	skb = skb_clone(skb, GFP_ATOMIC);
	if (skb) {
		if (d->sendq_hd)
			d->sendq_tl->next = skb;
		else
			d->sendq_hd = skb;
		d->sendq_tl = skb;
	}
	return 1;
}

/* some callers cannot sleep, and they can call this function,
 * transmitting the packets later, when interrupts are on
 */
static struct sk_buff *
aoecmd_cfg_pkts(ushort aoemajor, unsigned char aoeminor, struct sk_buff **tail)
{
	struct aoe_hdr *h;
	struct aoe_cfghdr *ch;
	struct sk_buff *skb, *sl, *sl_tail;
	struct net_device *ifp;

	sl = sl_tail = NULL;

	read_lock(&dev_base_lock);
	for_each_netdev(&init_net, ifp) {
		dev_hold(ifp);
		if (!is_aoe_netif(ifp))
			goto cont;

		skb = new_skb(sizeof *h + sizeof *ch);
		if (skb == NULL) {
			printk(KERN_INFO "aoe: skb alloc failure\n");
			goto cont;
		}
		skb_put(skb, sizeof *h + sizeof *ch);
		skb->dev = ifp;
		if (sl_tail == NULL)
			sl_tail = skb;
		h = (struct aoe_hdr *) skb_mac_header(skb);
		memset(h, 0, sizeof *h + sizeof *ch);

		memset(h->dst, 0xff, sizeof h->dst);
		memcpy(h->src, ifp->dev_addr, sizeof h->src);
		h->type = __constant_cpu_to_be16(ETH_P_AOE);
		h->verfl = AOE_HVER;
		h->major = cpu_to_be16(aoemajor);
		h->minor = aoeminor;
		h->cmd = AOECMD_CFG;

		skb->next = sl;
		sl = skb;
cont:
		dev_put(ifp);
	}
	read_unlock(&dev_base_lock);

	if (tail != NULL)
		*tail = sl_tail;
	return sl;
}

static void
resend(struct aoedev *d, struct aoetgt *t, struct frame *f)
{
	struct sk_buff *skb;
	struct aoe_hdr *h;
	struct aoe_atahdr *ah;
	char buf[128];
	u32 n;

	ifrotate(t);
	n = newtag(t);
	skb = f->skb;
	h = (struct aoe_hdr *) skb_mac_header(skb);
	ah = (struct aoe_atahdr *) (h+1);

	snprintf(buf, sizeof buf,
		"%15s e%ld.%d oldtag=%08x@%08lx newtag=%08x "
		"s=%012llx d=%012llx nout=%d\n",
		"retransmit", d->aoemajor, d->aoeminor, f->tag, jiffies, n,
		mac_addr(h->src),
		mac_addr(h->dst), t->nout);
	aoechr_error(buf);

	f->tag = n;
	h->tag = cpu_to_be32(n);
	memcpy(h->dst, t->addr, sizeof h->dst);
	memcpy(h->src, t->ifp->nd->dev_addr, sizeof h->src);

	switch (ah->cmdstat) {
	default:
		break;
	case WIN_READ:
	case WIN_READ_EXT:
	case WIN_WRITE:
	case WIN_WRITE_EXT:
		put_lba(ah, f->lba);

		n = f->bcnt;
		if (n > DEFAULTBCNT)
			n = DEFAULTBCNT;
		ah->scnt = n >> 9;
		if (ah->aflags & AOEAFL_WRITE) {
			skb_fill_page_desc(skb, 0, virt_to_page(f->bufaddr),
				offset_in_page(f->bufaddr), n);
			skb->len = sizeof *h + sizeof *ah + n;
			skb->data_len = n;
		}
	}
	skb->dev = t->ifp->nd;
	skb = skb_clone(skb, GFP_ATOMIC);
	if (skb == NULL)
		return;
	if (d->sendq_hd)
		d->sendq_tl->next = skb;
	else
		d->sendq_hd = skb;
	d->sendq_tl = skb;
}

static int
tsince(int tag)
{
	int n;

	n = jiffies & 0xffff;
	n -= tag & 0xffff;
	if (n < 0)
		n += 1<<16;
	return n;
}

static struct aoeif *
getif(struct aoetgt *t, struct net_device *nd)
{
	struct aoeif *p, *e;

	p = t->ifs;
	e = p + NAOEIFS;
	for (; p < e; p++)
		if (p->nd == nd)
			return p;
	return NULL;
}

static struct aoeif *
addif(struct aoetgt *t, struct net_device *nd)
{
	struct aoeif *p;

	p = getif(t, NULL);
	if (!p)
		return NULL;
	p->nd = nd;
	p->maxbcnt = DEFAULTBCNT;
	p->lost = 0;
	p->lostjumbo = 0;
	return p;
}

static void
ejectif(struct aoetgt *t, struct aoeif *ifp)
{
	struct aoeif *e;
	ulong n;

	e = t->ifs + NAOEIFS - 1;
	n = (e - ifp) * sizeof *ifp;
	memmove(ifp, ifp+1, n);
	e->nd = NULL;
}

static int
sthtith(struct aoedev *d)
{
	struct frame *f, *e, *nf;
	struct sk_buff *skb;
	struct aoetgt *ht = *d->htgt;

	f = ht->frames;
	e = f + ht->nframes;
	for (; f < e; f++) {
		if (f->tag == FREETAG)
			continue;
		nf = freeframe(d);
		if (!nf)
			return 0;
		skb = nf->skb;
		*nf = *f;
		f->skb = skb;
		f->tag = FREETAG;
		nf->waited = 0;
		ht->nout--;
		(*d->tgt)->nout++;
		resend(d, *d->tgt, nf);
	}
	/* he's clean, he's useless.  take away his interfaces */
	memset(ht->ifs, 0, sizeof ht->ifs);
	d->htgt = NULL;
	return 1;
}

static inline unsigned char
ata_scnt(unsigned char *packet) {
	struct aoe_hdr *h;
	struct aoe_atahdr *ah;

	h = (struct aoe_hdr *) packet;
	ah = (struct aoe_atahdr *) (h+1);
	return ah->scnt;
}

static void
rexmit_timer(ulong vp)
{
	struct aoedev *d;
	struct aoetgt *t, **tt, **te;
	struct aoeif *ifp;
	struct frame *f, *e;
	struct sk_buff *sl;
	register long timeout;
	ulong flags, n;

	d = (struct aoedev *) vp;
	sl = NULL;

	/* timeout is always ~150% of the moving average */
	timeout = d->rttavg;
	timeout += timeout >> 1;

	spin_lock_irqsave(&d->lock, flags);

	if (d->flags & DEVFL_TKILL) {
		spin_unlock_irqrestore(&d->lock, flags);
		return;
	}
	tt = d->targets;
	te = tt + NTARGETS;
	for (; tt < te && *tt; tt++) {
		t = *tt;
		f = t->frames;
		e = f + t->nframes;
		for (; f < e; f++) {
			if (f->tag == FREETAG
			|| tsince(f->tag) < timeout)
				continue;
			n = f->waited += timeout;
			n /= HZ;
			if (n > aoe_deadsecs) {
				/* waited too long.  device failure. */
				aoedev_downdev(d);
				break;
			}

			if (n > HELPWAIT /* see if another target can help */
			&& (tt != d->targets || d->targets[1]))
				d->htgt = tt;

			if (t->nout == t->maxout) {
				if (t->maxout > 1)
					t->maxout--;
				t->lastwadj = jiffies;
			}

			ifp = getif(t, f->skb->dev);
			if (ifp && ++ifp->lost > (t->nframes << 1)
			&& (ifp != t->ifs || t->ifs[1].nd)) {
				ejectif(t, ifp);
				ifp = NULL;
			}

			if (ata_scnt(skb_mac_header(f->skb)) > DEFAULTBCNT / 512
			&& ifp && ++ifp->lostjumbo > (t->nframes << 1)
			&& ifp->maxbcnt != DEFAULTBCNT) {
				printk(KERN_INFO
					"aoe: e%ld.%d: "
					"too many lost jumbo on "
					"%s:%012llx - "
					"falling back to %d frames.\n",
					d->aoemajor, d->aoeminor,
					ifp->nd->name, mac_addr(t->addr),
					DEFAULTBCNT);
				ifp->maxbcnt = 0;
			}
			resend(d, t, f);
		}

		/* window check */
		if (t->nout == t->maxout
		&& t->maxout < t->nframes
		&& (jiffies - t->lastwadj)/HZ > 10) {
			t->maxout++;
			t->lastwadj = jiffies;
		}
	}

	if (d->sendq_hd) {
		n = d->rttavg <<= 1;
		if (n > MAXTIMER)
			d->rttavg = MAXTIMER;
	}

	if (d->flags & DEVFL_KICKME || d->htgt) {
		d->flags &= ~DEVFL_KICKME;
		aoecmd_work(d);
	}

	sl = d->sendq_hd;
	d->sendq_hd = d->sendq_tl = NULL;

	d->timer.expires = jiffies + TIMERTICK;
	add_timer(&d->timer);

	spin_unlock_irqrestore(&d->lock, flags);

	aoenet_xmit(sl);
}

/* enters with d->lock held */
void
aoecmd_work(struct aoedev *d)
{
	struct buf *buf;
loop:
	if (d->htgt && !sthtith(d))
		return;
	if (d->inprocess == NULL) {
		if (list_empty(&d->bufq))
			return;
		buf = container_of(d->bufq.next, struct buf, bufs);
		list_del(d->bufq.next);
		d->inprocess = buf;
	}
	if (aoecmd_ata_rw(d))
		goto loop;
}

/* this function performs work that has been deferred until sleeping is OK
 */
void
aoecmd_sleepwork(struct work_struct *work)
{
	struct aoedev *d = container_of(work, struct aoedev, work);

	if (d->flags & DEVFL_GDALLOC)
		aoeblk_gdalloc(d);

	if (d->flags & DEVFL_NEWSIZE) {
		struct block_device *bd;
		unsigned long flags;
		u64 ssize;

		ssize = d->gd->capacity;
		bd = bdget_disk(d->gd, 0);

		if (bd) {
			mutex_lock(&bd->bd_inode->i_mutex);
			i_size_write(bd->bd_inode, (loff_t)ssize<<9);
			mutex_unlock(&bd->bd_inode->i_mutex);
			bdput(bd);
		}
		spin_lock_irqsave(&d->lock, flags);
		d->flags |= DEVFL_UP;
		d->flags &= ~DEVFL_NEWSIZE;
		spin_unlock_irqrestore(&d->lock, flags);
	}
}

static void
ataid_complete(struct aoedev *d, struct aoetgt *t, unsigned char *id)
{
	u64 ssize;
	u16 n;

	/* word 83: command set supported */
	n = le16_to_cpu(get_unaligned((__le16 *) &id[83<<1]));

	/* word 86: command set/feature enabled */
	n |= le16_to_cpu(get_unaligned((__le16 *) &id[86<<1]));

	if (n & (1<<10)) {	/* bit 10: LBA 48 */
		d->flags |= DEVFL_EXT;

		/* word 100: number lba48 sectors */
		ssize = le64_to_cpu(get_unaligned((__le64 *) &id[100<<1]));

		/* set as in ide-disk.c:init_idedisk_capacity */
		d->geo.cylinders = ssize;
		d->geo.cylinders /= (255 * 63);
		d->geo.heads = 255;
		d->geo.sectors = 63;
	} else {
		d->flags &= ~DEVFL_EXT;

		/* number lba28 sectors */
		ssize = le32_to_cpu(get_unaligned((__le32 *) &id[60<<1]));

		/* NOTE: obsolete in ATA 6 */
		d->geo.cylinders = le16_to_cpu(get_unaligned((__le16 *) &id[54<<1]));
		d->geo.heads = le16_to_cpu(get_unaligned((__le16 *) &id[55<<1]));
		d->geo.sectors = le16_to_cpu(get_unaligned((__le16 *) &id[56<<1]));
	}

	if (d->ssize != ssize)
		printk(KERN_INFO
			"aoe: %012llx e%ld.%d v%04x has %llu sectors\n",
			mac_addr(t->addr),
			d->aoemajor, d->aoeminor,
			d->fw_ver, (long long)ssize);
	d->ssize = ssize;
	d->geo.start = 0;
	if (d->flags & (DEVFL_GDALLOC|DEVFL_NEWSIZE))
		return;
	if (d->gd != NULL) {
		d->gd->capacity = ssize;
		d->flags |= DEVFL_NEWSIZE;
	} else
		d->flags |= DEVFL_GDALLOC;
	schedule_work(&d->work);
}

static void
calc_rttavg(struct aoedev *d, int rtt)
{
	register long n;

	n = rtt;
	if (n < 0) {
		n = -rtt;
		if (n < MINTIMER)
			n = MINTIMER;
		else if (n > MAXTIMER)
			n = MAXTIMER;
		d->mintimer += (n - d->mintimer) >> 1;
	} else if (n < d->mintimer)
		n = d->mintimer;
	else if (n > MAXTIMER)
		n = MAXTIMER;

	/* g == .25; cf. Congestion Avoidance and Control, Jacobson & Karels; 1988 */
	n -= d->rttavg;
	d->rttavg += n >> 2;
}

static struct aoetgt *
gettgt(struct aoedev *d, char *addr)
{
	struct aoetgt **t, **e;

	t = d->targets;
	e = t + NTARGETS;
	for (; t < e && *t; t++)
		if (memcmp((*t)->addr, addr, sizeof((*t)->addr)) == 0)
			return *t;
	return NULL;
}

static inline void
diskstats(struct gendisk *disk, struct bio *bio, ulong duration)
{
	unsigned long n_sect = bio->bi_size >> 9;
	const int rw = bio_data_dir(bio);

	disk_stat_inc(disk, ios[rw]);
	disk_stat_add(disk, ticks[rw], duration);
	disk_stat_add(disk, sectors[rw], n_sect);
	disk_stat_add(disk, io_ticks, duration);
}

void
aoecmd_ata_rsp(struct sk_buff *skb)
{
	struct aoedev *d;
	struct aoe_hdr *hin, *hout;
	struct aoe_atahdr *ahin, *ahout;
	struct frame *f;
	struct buf *buf;
	struct sk_buff *sl;
	struct aoetgt *t;
	struct aoeif *ifp;
	register long n;
	ulong flags;
	char ebuf[128];
	u16 aoemajor;

	hin = (struct aoe_hdr *) skb_mac_header(skb);
	aoemajor = be16_to_cpu(get_unaligned(&hin->major));
	d = aoedev_by_aoeaddr(aoemajor, hin->minor);
	if (d == NULL) {
		snprintf(ebuf, sizeof ebuf, "aoecmd_ata_rsp: ata response "
			"for unknown device %d.%d\n",
			 aoemajor, hin->minor);
		aoechr_error(ebuf);
		return;
	}

	spin_lock_irqsave(&d->lock, flags);

	n = be32_to_cpu(get_unaligned(&hin->tag));
	t = gettgt(d, hin->src);
	if (t == NULL) {
		printk(KERN_INFO "aoe: can't find target e%ld.%d:%012llx\n",
			d->aoemajor, d->aoeminor, mac_addr(hin->src));
		spin_unlock_irqrestore(&d->lock, flags);
		return;
	}
	f = getframe(t, n);
	if (f == NULL) {
		calc_rttavg(d, -tsince(n));
		spin_unlock_irqrestore(&d->lock, flags);
		snprintf(ebuf, sizeof ebuf,
			"%15s e%d.%d    tag=%08x@%08lx\n",
			"unexpected rsp",
			be16_to_cpu(get_unaligned(&hin->major)),
			hin->minor,
			be32_to_cpu(get_unaligned(&hin->tag)),
			jiffies);
		aoechr_error(ebuf);
		return;
	}

	calc_rttavg(d, tsince(f->tag));

	ahin = (struct aoe_atahdr *) (hin+1);
	hout = (struct aoe_hdr *) skb_mac_header(f->skb);
	ahout = (struct aoe_atahdr *) (hout+1);
	buf = f->buf;

	if (ahin->cmdstat & 0xa9) {	/* these bits cleared on success */
		printk(KERN_ERR
			"aoe: ata error cmd=%2.2Xh stat=%2.2Xh from e%ld.%d\n",
			ahout->cmdstat, ahin->cmdstat,
			d->aoemajor, d->aoeminor);
		if (buf)
			buf->flags |= BUFFL_FAIL;
	} else {
		if (d->htgt && t == *d->htgt) /* I'll help myself, thank you. */
			d->htgt = NULL;
		n = ahout->scnt << 9;
		switch (ahout->cmdstat) {
		case WIN_READ:
		case WIN_READ_EXT:
			if (skb->len - sizeof *hin - sizeof *ahin < n) {
				printk(KERN_ERR
					"aoe: %s.  skb->len=%d need=%ld\n",
					"runt data size in read", skb->len, n);
				/* fail frame f?  just returning will rexmit. */
				spin_unlock_irqrestore(&d->lock, flags);
				return;
			}
			memcpy(f->bufaddr, ahin+1, n);
		case WIN_WRITE:
		case WIN_WRITE_EXT:
			ifp = getif(t, skb->dev);
			if (ifp) {
				ifp->lost = 0;
				if (n > DEFAULTBCNT)
					ifp->lostjumbo = 0;
			}
			if (f->bcnt -= n) {
				f->lba += n >> 9;
				f->bufaddr += n;
				resend(d, t, f);
				goto xmit;
			}
			break;
		case WIN_IDENTIFY:
			if (skb->len - sizeof *hin - sizeof *ahin < 512) {
				printk(KERN_INFO
					"aoe: runt data size in ataid.  skb->len=%d\n",
					skb->len);
				spin_unlock_irqrestore(&d->lock, flags);
				return;
			}
			ataid_complete(d, t, (char *) (ahin+1));
			break;
		default:
			printk(KERN_INFO
				"aoe: unrecognized ata command %2.2Xh for %d.%d\n",
				ahout->cmdstat,
				be16_to_cpu(get_unaligned(&hin->major)),
				hin->minor);
		}
	}

	if (buf && --buf->nframesout == 0 && buf->resid == 0) {
		diskstats(d->gd, buf->bio, jiffies - buf->stime);
		n = (buf->flags & BUFFL_FAIL) ? -EIO : 0;
		bio_endio(buf->bio, n);
		mempool_free(buf, d->bufpool);
	}

	f->buf = NULL;
	f->tag = FREETAG;
	t->nout--;

	aoecmd_work(d);
xmit:
	sl = d->sendq_hd;
	d->sendq_hd = d->sendq_tl = NULL;

	spin_unlock_irqrestore(&d->lock, flags);
	aoenet_xmit(sl);
}

void
aoecmd_cfg(ushort aoemajor, unsigned char aoeminor)
{
	struct sk_buff *sl;

	sl = aoecmd_cfg_pkts(aoemajor, aoeminor, NULL);

	aoenet_xmit(sl);
}
 
struct sk_buff *
aoecmd_ata_id(struct aoedev *d)
{
	struct aoe_hdr *h;
	struct aoe_atahdr *ah;
	struct frame *f;
	struct sk_buff *skb;
	struct aoetgt *t;

	f = freeframe(d);
	if (f == NULL)
		return NULL;

	t = *d->tgt;

	/* initialize the headers & frame */
	skb = f->skb;
	h = (struct aoe_hdr *) skb_mac_header(skb);
	ah = (struct aoe_atahdr *) (h+1);
	skb_put(skb, sizeof *h + sizeof *ah);
	memset(h, 0, skb->len);
	f->tag = aoehdr_atainit(d, t, h);
	t->nout++;
	f->waited = 0;

	/* set up ata header */
	ah->scnt = 1;
	ah->cmdstat = WIN_IDENTIFY;
	ah->lba3 = 0xa0;

	skb->dev = t->ifp->nd;

	d->rttavg = MAXTIMER;
	d->timer.function = rexmit_timer;

	return skb_clone(skb, GFP_ATOMIC);
}
 
static struct aoetgt *
addtgt(struct aoedev *d, char *addr, ulong nframes)
{
	struct aoetgt *t, **tt, **te;
	struct frame *f, *e;

	tt = d->targets;
	te = tt + NTARGETS;
	for (; tt < te && *tt; tt++)
		;

	if (tt == te)
		return NULL;

	t = kcalloc(1, sizeof *t, GFP_ATOMIC);
	if (!t)
		return NULL;
	f = kcalloc(nframes, sizeof *f, GFP_ATOMIC);
	if (!f) {
		kfree(t);
		return NULL;
	}

	t->nframes = nframes;
	t->frames = f;
	e = f + nframes;
	for (; f < e; f++)
		f->tag = FREETAG;
	memcpy(t->addr, addr, sizeof t->addr);
	t->ifp = t->ifs;
	t->maxout = t->nframes;
	return *tt = t;
}

void
aoecmd_cfg_rsp(struct sk_buff *skb)
{
	struct aoedev *d;
	struct aoe_hdr *h;
	struct aoe_cfghdr *ch;
	struct aoetgt *t;
	struct aoeif *ifp;
	ulong flags, sysminor, aoemajor;
	struct sk_buff *sl;
	u16 n;

	h = (struct aoe_hdr *) skb_mac_header(skb);
	ch = (struct aoe_cfghdr *) (h+1);

	/*
	 * Enough people have their dip switches set backwards to
	 * warrant a loud message for this special case.
	 */
	aoemajor = be16_to_cpu(get_unaligned(&h->major));
	if (aoemajor == 0xfff) {
		printk(KERN_ERR "aoe: Warning: shelf address is all ones.  "
			"Check shelf dip switches.\n");
		return;
	}

	sysminor = SYSMINOR(aoemajor, h->minor);
	if (sysminor * AOE_PARTITIONS + AOE_PARTITIONS > MINORMASK) {
		printk(KERN_INFO "aoe: e%ld.%d: minor number too large\n",
			aoemajor, (int) h->minor);
		return;
	}

	n = be16_to_cpu(ch->bufcnt);
	if (n > aoe_maxout)	/* keep it reasonable */
		n = aoe_maxout;

	d = aoedev_by_sysminor_m(sysminor);
	if (d == NULL) {
		printk(KERN_INFO "aoe: device sysminor_m failure\n");
		return;
	}

	spin_lock_irqsave(&d->lock, flags);

	t = gettgt(d, h->src);
	if (!t) {
		t = addtgt(d, h->src, n);
		if (!t) {
			printk(KERN_INFO
				"aoe: device addtgt failure; "
				"too many targets?\n");
			spin_unlock_irqrestore(&d->lock, flags);
			return;
		}
	}
	ifp = getif(t, skb->dev);
	if (!ifp) {
		ifp = addif(t, skb->dev);
		if (!ifp) {
			printk(KERN_INFO
				"aoe: device addif failure; "
				"too many interfaces?\n");
			spin_unlock_irqrestore(&d->lock, flags);
			return;
		}
	}
	if (ifp->maxbcnt) {
		n = ifp->nd->mtu;
		n -= sizeof (struct aoe_hdr) + sizeof (struct aoe_atahdr);
		n /= 512;
		if (n > ch->scnt)
			n = ch->scnt;
		n = n ? n * 512 : DEFAULTBCNT;
		if (n != ifp->maxbcnt) {
			printk(KERN_INFO
				"aoe: e%ld.%d: setting %d%s%s:%012llx\n",
				d->aoemajor, d->aoeminor, n,
				" byte data frames on ", ifp->nd->name,
				mac_addr(t->addr));
			ifp->maxbcnt = n;
		}
	}

	/* don't change users' perspective */
	if (d->nopen) {
		spin_unlock_irqrestore(&d->lock, flags);
		return;
	}
	d->fw_ver = be16_to_cpu(ch->fwver);

	sl = aoecmd_ata_id(d);

	spin_unlock_irqrestore(&d->lock, flags);

	aoenet_xmit(sl);
}

void
aoecmd_cleanslate(struct aoedev *d)
{
	struct aoetgt **t, **te;
	struct aoeif *p, *e;

	d->mintimer = MINTIMER;

	t = d->targets;
	te = t + NTARGETS;
	for (; t < te && *t; t++) {
		(*t)->maxout = (*t)->nframes;
		p = (*t)->ifs;
		e = p + NAOEIFS;
		for (; p < e; p++) {
			p->lostjumbo = 0;
			p->lost = 0;
			p->maxbcnt = DEFAULTBCNT;
		}
	}
}
