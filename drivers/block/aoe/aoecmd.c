/* Copyright (c) 2004 Coraid, Inc.  See COPYING for GPL terms. */
/*
 * aoecmd.c
 * Filesystem request handling methods
 */

#include <linux/hdreg.h>
#include <linux/blkdev.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <asm/unaligned.h>
#include "aoe.h"

#define TIMERTICK (HZ / 10)
#define MINTIMER (2 * TIMERTICK)
#define MAXTIMER (HZ << 1)
#define MAXWAIT (60 * 3)	/* After MAXWAIT seconds, give up and fail dev */

static struct sk_buff *
new_skb(struct net_device *if_dev, ulong len)
{
	struct sk_buff *skb;

	skb = alloc_skb(len, GFP_ATOMIC);
	if (skb) {
		skb->nh.raw = skb->mac.raw = skb->data;
		skb->dev = if_dev;
		skb->protocol = __constant_htons(ETH_P_AOE);
		skb->priority = 0;
		skb_put(skb, len);
		skb->next = skb->prev = NULL;

		/* tell the network layer not to perform IP checksums
		 * or to get the NIC to do it
		 */
		skb->ip_summed = CHECKSUM_NONE;
	}
	return skb;
}

static struct sk_buff *
skb_prepare(struct aoedev *d, struct frame *f)
{
	struct sk_buff *skb;
	char *p;

	skb = new_skb(d->ifp, f->ndata + f->writedatalen);
	if (!skb) {
		printk(KERN_INFO "aoe: skb_prepare: failure to allocate skb\n");
		return NULL;
	}

	p = skb->mac.raw;
	memcpy(p, f->data, f->ndata);

	if (f->writedatalen) {
		p += sizeof(struct aoe_hdr) + sizeof(struct aoe_atahdr);
		memcpy(p, f->bufaddr, f->writedatalen);
	}

	return skb;
}

static struct frame *
getframe(struct aoedev *d, int tag)
{
	struct frame *f, *e;

	f = d->frames;
	e = f + d->nframes;
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
newtag(struct aoedev *d)
{
	register ulong n;

	n = jiffies & 0xffff;
	return n |= (++d->lasttag & 0x7fff) << 16;
}

static int
aoehdr_atainit(struct aoedev *d, struct aoe_hdr *h)
{
	u32 host_tag = newtag(d);

	memcpy(h->src, d->ifp->dev_addr, sizeof h->src);
	memcpy(h->dst, d->addr, sizeof h->dst);
	h->type = __constant_cpu_to_be16(ETH_P_AOE);
	h->verfl = AOE_HVER;
	h->major = cpu_to_be16(d->aoemajor);
	h->minor = d->aoeminor;
	h->cmd = AOECMD_ATA;
	h->tag = cpu_to_be32(host_tag);

	return host_tag;
}

static void
aoecmd_ata_rw(struct aoedev *d, struct frame *f)
{
	struct aoe_hdr *h;
	struct aoe_atahdr *ah;
	struct buf *buf;
	struct sk_buff *skb;
	ulong bcnt;
	register sector_t sector;
	char writebit, extbit;

	writebit = 0x10;
	extbit = 0x4;

	buf = d->inprocess;

	sector = buf->sector;
	bcnt = buf->bv_resid;
	if (bcnt > MAXATADATA)
		bcnt = MAXATADATA;

	/* initialize the headers & frame */
	h = (struct aoe_hdr *) f->data;
	ah = (struct aoe_atahdr *) (h+1);
	f->ndata = sizeof *h + sizeof *ah;
	memset(h, 0, f->ndata);
	f->tag = aoehdr_atainit(d, h);
	f->waited = 0;
	f->buf = buf;
	f->bufaddr = buf->bufaddr;

	/* set up ata header */
	ah->scnt = bcnt >> 9;
	ah->lba0 = sector;
	ah->lba1 = sector >>= 8;
	ah->lba2 = sector >>= 8;
	ah->lba3 = sector >>= 8;
	if (d->flags & DEVFL_EXT) {
		ah->aflags |= AOEAFL_EXT;
		ah->lba4 = sector >>= 8;
		ah->lba5 = sector >>= 8;
	} else {
		extbit = 0;
		ah->lba3 &= 0x0f;
		ah->lba3 |= 0xe0;	/* LBA bit + obsolete 0xa0 */
	}

	if (bio_data_dir(buf->bio) == WRITE) {
		ah->aflags |= AOEAFL_WRITE;
		f->writedatalen = bcnt;
	} else {
		writebit = 0;
		f->writedatalen = 0;
	}

	ah->cmdstat = WIN_READ | writebit | extbit;

	/* mark all tracking fields and load out */
	buf->nframesout += 1;
	buf->bufaddr += bcnt;
	buf->bv_resid -= bcnt;
/* printk(KERN_INFO "aoe: bv_resid=%ld\n", buf->bv_resid); */
	buf->resid -= bcnt;
	buf->sector += bcnt >> 9;
	if (buf->resid == 0) {
		d->inprocess = NULL;
	} else if (buf->bv_resid == 0) {
		buf->bv++;
		buf->bv_resid = buf->bv->bv_len;
		buf->bufaddr = page_address(buf->bv->bv_page) + buf->bv->bv_offset;
	}

	skb = skb_prepare(d, f);
	if (skb) {
		skb->next = NULL;
		if (d->sendq_hd)
			d->sendq_tl->next = skb;
		else
			d->sendq_hd = skb;
		d->sendq_tl = skb;
	}
}

/* enters with d->lock held */
void
aoecmd_work(struct aoedev *d)
{
	struct frame *f;
	struct buf *buf;
loop:
	f = getframe(d, FREETAG);
	if (f == NULL)
		return;
	if (d->inprocess == NULL) {
		if (list_empty(&d->bufq))
			return;
		buf = container_of(d->bufq.next, struct buf, bufs);
		list_del(d->bufq.next);
/*printk(KERN_INFO "aoecmd_work: bi_size=%ld\n", buf->bio->bi_size); */
		d->inprocess = buf;
	}
	aoecmd_ata_rw(d, f);
	goto loop;
}

static void
rexmit(struct aoedev *d, struct frame *f)
{
	struct sk_buff *skb;
	struct aoe_hdr *h;
	char buf[128];
	u32 n;

	n = newtag(d);

	snprintf(buf, sizeof buf,
		"%15s e%ld.%ld oldtag=%08x@%08lx newtag=%08x\n",
		"retransmit",
		d->aoemajor, d->aoeminor, f->tag, jiffies, n);
	aoechr_error(buf);

	h = (struct aoe_hdr *) f->data;
	f->tag = n;
	h->tag = cpu_to_be32(n);

	skb = skb_prepare(d, f);
	if (skb) {
		skb->next = NULL;
		if (d->sendq_hd)
			d->sendq_tl->next = skb;
		else
			d->sendq_hd = skb;
		d->sendq_tl = skb;
	}
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

static void
rexmit_timer(ulong vp)
{
	struct aoedev *d;
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
tdie:		spin_unlock_irqrestore(&d->lock, flags);
		return;
	}
	f = d->frames;
	e = f + d->nframes;
	for (; f<e; f++) {
		if (f->tag != FREETAG && tsince(f->tag) >= timeout) {
			n = f->waited += timeout;
			n /= HZ;
			if (n > MAXWAIT) { /* waited too long.  device failure. */
				aoedev_downdev(d);
				goto tdie;
			}
			rexmit(d, f);
		}
	}

	sl = d->sendq_hd;
	d->sendq_hd = d->sendq_tl = NULL;
	if (sl) {
		n = d->rttavg <<= 1;
		if (n > MAXTIMER)
			d->rttavg = MAXTIMER;
	}

	d->timer.expires = jiffies + TIMERTICK;
	add_timer(&d->timer);

	spin_unlock_irqrestore(&d->lock, flags);

	aoenet_xmit(sl);
}

static void
ataid_complete(struct aoedev *d, unsigned char *id)
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
	d->ssize = ssize;
	d->geo.start = 0;
	if (d->gd != NULL) {
		d->gd->capacity = ssize;
		d->flags |= DEVFL_UP;
		return;
	}
	if (d->flags & DEVFL_WORKON) {
		printk(KERN_INFO "aoe: ataid_complete: can't schedule work, it's already on!  "
			"(This really shouldn't happen).\n");
		return;
	}
	INIT_WORK(&d->work, aoeblk_gdalloc, d);
	schedule_work(&d->work);
	d->flags |= DEVFL_WORKON;
}

static void
calc_rttavg(struct aoedev *d, int rtt)
{
	register long n;

	n = rtt;
	if (n < MINTIMER)
		n = MINTIMER;
	else if (n > MAXTIMER)
		n = MAXTIMER;

	/* g == .25; cf. Congestion Avoidance and Control, Jacobson & Karels; 1988 */
	n -= d->rttavg;
	d->rttavg += n >> 2;
}

void
aoecmd_ata_rsp(struct sk_buff *skb)
{
	struct aoedev *d;
	struct aoe_hdr *hin;
	struct aoe_atahdr *ahin, *ahout;
	struct frame *f;
	struct buf *buf;
	struct sk_buff *sl;
	register long n;
	ulong flags;
	char ebuf[128];
	u16 aoemajor;

	hin = (struct aoe_hdr *) skb->mac.raw;
	aoemajor = be16_to_cpu(hin->major);
	d = aoedev_by_aoeaddr(aoemajor, hin->minor);
	if (d == NULL) {
		snprintf(ebuf, sizeof ebuf, "aoecmd_ata_rsp: ata response "
			"for unknown device %d.%d\n",
			 aoemajor, hin->minor);
		aoechr_error(ebuf);
		return;
	}

	spin_lock_irqsave(&d->lock, flags);

	f = getframe(d, be32_to_cpu(hin->tag));
	if (f == NULL) {
		spin_unlock_irqrestore(&d->lock, flags);
		snprintf(ebuf, sizeof ebuf,
			"%15s e%d.%d    tag=%08x@%08lx\n",
			"unexpected rsp",
			be16_to_cpu(hin->major),
			hin->minor,
			be32_to_cpu(hin->tag),
			jiffies);
		aoechr_error(ebuf);
		return;
	}

	calc_rttavg(d, tsince(f->tag));

	ahin = (struct aoe_atahdr *) (hin+1);
	ahout = (struct aoe_atahdr *) (f->data + sizeof(struct aoe_hdr));
	buf = f->buf;

	if (ahin->cmdstat & 0xa9) {	/* these bits cleared on success */
		printk(KERN_CRIT "aoe: aoecmd_ata_rsp: ata error cmd=%2.2Xh "
			"stat=%2.2Xh from e%ld.%ld\n", 
			ahout->cmdstat, ahin->cmdstat,
			d->aoemajor, d->aoeminor);
		if (buf)
			buf->flags |= BUFFL_FAIL;
	} else {
		switch (ahout->cmdstat) {
		case WIN_READ:
		case WIN_READ_EXT:
			n = ahout->scnt << 9;
			if (skb->len - sizeof *hin - sizeof *ahin < n) {
				printk(KERN_CRIT "aoe: aoecmd_ata_rsp: runt "
					"ata data size in read.  skb->len=%d\n",
					skb->len);
				/* fail frame f?  just returning will rexmit. */
				spin_unlock_irqrestore(&d->lock, flags);
				return;
			}
			memcpy(f->bufaddr, ahin+1, n);
		case WIN_WRITE:
		case WIN_WRITE_EXT:
			break;
		case WIN_IDENTIFY:
			if (skb->len - sizeof *hin - sizeof *ahin < 512) {
				printk(KERN_INFO "aoe: aoecmd_ata_rsp: runt data size "
					"in ataid.  skb->len=%d\n", skb->len);
				spin_unlock_irqrestore(&d->lock, flags);
				return;
			}
			ataid_complete(d, (char *) (ahin+1));
			/* d->flags |= DEVFL_WC_UPDATE; */
			break;
		default:
			printk(KERN_INFO "aoe: aoecmd_ata_rsp: unrecognized "
			       "outbound ata command %2.2Xh for %d.%d\n", 
			       ahout->cmdstat,
			       be16_to_cpu(hin->major),
			       hin->minor);
		}
	}

	if (buf) {
		buf->nframesout -= 1;
		if (buf->nframesout == 0 && buf->resid == 0) {
			unsigned long duration = jiffies - buf->start_time;
			unsigned long n_sect = buf->bio->bi_size >> 9;
			struct gendisk *disk = d->gd;
			const int rw = bio_data_dir(buf->bio);

			disk_stat_inc(disk, ios[rw]);
			disk_stat_add(disk, ticks[rw], duration);
			disk_stat_add(disk, sectors[rw], n_sect);
			disk_stat_add(disk, io_ticks, duration);
			n = (buf->flags & BUFFL_FAIL) ? -EIO : 0;
			bio_endio(buf->bio, buf->bio->bi_size, n);
			mempool_free(buf, d->bufpool);
		}
	}

	f->buf = NULL;
	f->tag = FREETAG;

	aoecmd_work(d);

	sl = d->sendq_hd;
	d->sendq_hd = d->sendq_tl = NULL;

	spin_unlock_irqrestore(&d->lock, flags);

	aoenet_xmit(sl);
}

void
aoecmd_cfg(ushort aoemajor, unsigned char aoeminor)
{
	struct aoe_hdr *h;
	struct aoe_cfghdr *ch;
	struct sk_buff *skb, *sl;
	struct net_device *ifp;

	sl = NULL;

	read_lock(&dev_base_lock);
	for (ifp = dev_base; ifp; dev_put(ifp), ifp = ifp->next) {
		dev_hold(ifp);
		if (!is_aoe_netif(ifp))
			continue;

		skb = new_skb(ifp, sizeof *h + sizeof *ch);
		if (skb == NULL) {
			printk(KERN_INFO "aoe: aoecmd_cfg: skb alloc failure\n");
			continue;
		}
		h = (struct aoe_hdr *) skb->mac.raw;
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
	}
	read_unlock(&dev_base_lock);

	aoenet_xmit(sl);
}
 
/*
 * Since we only call this in one place (and it only prepares one frame)
 * we just return the skb.  Usually we'd chain it up to the aoedev sendq.
 */
static struct sk_buff *
aoecmd_ata_id(struct aoedev *d)
{
	struct aoe_hdr *h;
	struct aoe_atahdr *ah;
	struct frame *f;
	struct sk_buff *skb;

	f = getframe(d, FREETAG);
	if (f == NULL) {
		printk(KERN_CRIT "aoe: aoecmd_ata_id: can't get a frame.  "
			"This shouldn't happen.\n");
		return NULL;
	}

	/* initialize the headers & frame */
	h = (struct aoe_hdr *) f->data;
	ah = (struct aoe_atahdr *) (h+1);
	f->ndata = sizeof *h + sizeof *ah;
	memset(h, 0, f->ndata);
	f->tag = aoehdr_atainit(d, h);
	f->waited = 0;
	f->writedatalen = 0;

	/* this message initializes the device, so we reset the rttavg */
	d->rttavg = MAXTIMER;

	/* set up ata header */
	ah->scnt = 1;
	ah->cmdstat = WIN_IDENTIFY;
	ah->lba3 = 0xa0;

	skb = skb_prepare(d, f);

	/* we now want to start the rexmit tracking */
	d->flags &= ~DEVFL_TKILL;
	d->timer.data = (ulong) d;
	d->timer.function = rexmit_timer;
	d->timer.expires = jiffies + TIMERTICK;
	add_timer(&d->timer);

	return skb;
}
 
void
aoecmd_cfg_rsp(struct sk_buff *skb)
{
	struct aoedev *d;
	struct aoe_hdr *h;
	struct aoe_cfghdr *ch;
	ulong flags, sysminor, aoemajor;
	u16 bufcnt;
	struct sk_buff *sl;
	enum { MAXFRAMES = 8 };

	h = (struct aoe_hdr *) skb->mac.raw;
	ch = (struct aoe_cfghdr *) (h+1);

	/*
	 * Enough people have their dip switches set backwards to
	 * warrant a loud message for this special case.
	 */
	aoemajor = be16_to_cpu(h->major);
	if (aoemajor == 0xfff) {
		printk(KERN_CRIT "aoe: aoecmd_cfg_rsp: Warning: shelf "
			"address is all ones.  Check shelf dip switches\n");
		return;
	}

	sysminor = SYSMINOR(aoemajor, h->minor);
	if (sysminor * AOE_PARTITIONS + AOE_PARTITIONS > MINORMASK) {
		printk(KERN_INFO
			"aoe: e%ld.%d: minor number too large\n", 
			aoemajor, (int) h->minor);
		return;
	}

	bufcnt = be16_to_cpu(ch->bufcnt);
	if (bufcnt > MAXFRAMES)	/* keep it reasonable */
		bufcnt = MAXFRAMES;

	d = aoedev_set(sysminor, h->src, skb->dev, bufcnt);
	if (d == NULL) {
		printk(KERN_INFO "aoe: aoecmd_cfg_rsp: device set failure\n");
		return;
	}

	spin_lock_irqsave(&d->lock, flags);

	if (d->flags & (DEVFL_UP | DEVFL_CLOSEWAIT)) {
		spin_unlock_irqrestore(&d->lock, flags);
		return;
	}

	d->fw_ver = be16_to_cpu(ch->fwver);

	/* we get here only if the device is new */
	sl = aoecmd_ata_id(d);

	spin_unlock_irqrestore(&d->lock, flags);

	aoenet_xmit(sl);
}

