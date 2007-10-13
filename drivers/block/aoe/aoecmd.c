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
#include <net/net_namespace.h>
#include <asm/unaligned.h>
#include "aoe.h"

#define TIMERTICK (HZ / 10)
#define MINTIMER (2 * TIMERTICK)
#define MAXTIMER (HZ << 1)

static int aoe_deadsecs = 60 * 3;
module_param(aoe_deadsecs, int, 0644);
MODULE_PARM_DESC(aoe_deadsecs, "After aoe_deadsecs seconds, give up and fail dev.");

struct sk_buff *
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
	if (bcnt > d->maxbcnt)
		bcnt = d->maxbcnt;

	/* initialize the headers & frame */
	skb = f->skb;
	h = aoe_hdr(skb);
	ah = (struct aoe_atahdr *) (h+1);
	skb_put(skb, sizeof *h + sizeof *ah);
	memset(h, 0, skb->len);
	f->tag = aoehdr_atainit(d, h);
	f->waited = 0;
	f->buf = buf;
	f->bufaddr = buf->bufaddr;
	f->bcnt = bcnt;
	f->lba = sector;

	/* set up ata header */
	ah->scnt = bcnt >> 9;
	put_lba(ah, sector);
	if (d->flags & DEVFL_EXT) {
		ah->aflags |= AOEAFL_EXT;
	} else {
		extbit = 0;
		ah->lba3 &= 0x0f;
		ah->lba3 |= 0xe0;	/* LBA bit + obsolete 0xa0 */
	}

	if (bio_data_dir(buf->bio) == WRITE) {
		skb_fill_page_desc(skb, 0, virt_to_page(f->bufaddr),
			offset_in_page(f->bufaddr), bcnt);
		ah->aflags |= AOEAFL_WRITE;
		skb->len += bcnt;
		skb->data_len = bcnt;
	} else {
		writebit = 0;
	}

	ah->cmdstat = WIN_READ | writebit | extbit;

	/* mark all tracking fields and load out */
	buf->nframesout += 1;
	buf->bufaddr += bcnt;
	buf->bv_resid -= bcnt;
/* printk(KERN_DEBUG "aoe: bv_resid=%ld\n", buf->bv_resid); */
	buf->resid -= bcnt;
	buf->sector += bcnt >> 9;
	if (buf->resid == 0) {
		d->inprocess = NULL;
	} else if (buf->bv_resid == 0) {
		buf->bv++;
		WARN_ON(buf->bv->bv_len == 0);
		buf->bv_resid = buf->bv->bv_len;
		buf->bufaddr = page_address(buf->bv->bv_page) + buf->bv->bv_offset;
	}

	skb->dev = d->ifp;
	skb = skb_clone(skb, GFP_ATOMIC);
	if (skb == NULL)
		return;
	if (d->sendq_hd)
		d->sendq_tl->next = skb;
	else
		d->sendq_hd = skb;
	d->sendq_tl = skb;
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
		h = aoe_hdr(skb);
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

static struct frame *
freeframe(struct aoedev *d)
{
	struct frame *f, *e;
	int n = 0;

	f = d->frames;
	e = f + d->nframes;
	for (; f<e; f++) {
		if (f->tag != FREETAG)
			continue;
		if (atomic_read(&skb_shinfo(f->skb)->dataref) == 1) {
			skb_shinfo(f->skb)->nr_frags = f->skb->data_len = 0;
			skb_trim(f->skb, 0);
			return f;
		}
		n++;
	}
	if (n == d->nframes)	/* wait for network layer */
		d->flags |= DEVFL_KICKME;

	return NULL;
}

/* enters with d->lock held */
void
aoecmd_work(struct aoedev *d)
{
	struct frame *f;
	struct buf *buf;

	if (d->flags & DEVFL_PAUSE) {
		if (!aoedev_isbusy(d))
			d->sendq_hd = aoecmd_cfg_pkts(d->aoemajor,
						d->aoeminor, &d->sendq_tl);
		return;
	}

loop:
	f = freeframe(d);
	if (f == NULL)
		return;
	if (d->inprocess == NULL) {
		if (list_empty(&d->bufq))
			return;
		buf = container_of(d->bufq.next, struct buf, bufs);
		list_del(d->bufq.next);
/*printk(KERN_DEBUG "aoe: bi_size=%ld\n", buf->bio->bi_size); */
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
	struct aoe_atahdr *ah;
	char buf[128];
	u32 n;

	n = newtag(d);

	snprintf(buf, sizeof buf,
		"%15s e%ld.%ld oldtag=%08x@%08lx newtag=%08x\n",
		"retransmit",
		d->aoemajor, d->aoeminor, f->tag, jiffies, n);
	aoechr_error(buf);

	skb = f->skb;
	h = aoe_hdr(skb);
	ah = (struct aoe_atahdr *) (h+1);
	f->tag = n;
	h->tag = cpu_to_be32(n);
	memcpy(h->dst, d->addr, sizeof h->dst);
	memcpy(h->src, d->ifp->dev_addr, sizeof h->src);

	n = DEFAULTBCNT / 512;
	if (ah->scnt > n) {
		ah->scnt = n;
		if (ah->aflags & AOEAFL_WRITE) {
			skb_fill_page_desc(skb, 0, virt_to_page(f->bufaddr),
				offset_in_page(f->bufaddr), DEFAULTBCNT);
			skb->len = sizeof *h + sizeof *ah + DEFAULTBCNT;
			skb->data_len = DEFAULTBCNT;
		}
		if (++d->lostjumbo > (d->nframes << 1))
		if (d->maxbcnt != DEFAULTBCNT) {
			printk(KERN_INFO "aoe: e%ld.%ld: too many lost jumbo on %s - using 1KB frames.\n",
				d->aoemajor, d->aoeminor, d->ifp->name);
			d->maxbcnt = DEFAULTBCNT;
			d->flags |= DEVFL_MAXBCNT;
		}
	}

	skb->dev = d->ifp;
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
		spin_unlock_irqrestore(&d->lock, flags);
		return;
	}
	f = d->frames;
	e = f + d->nframes;
	for (; f<e; f++) {
		if (f->tag != FREETAG && tsince(f->tag) >= timeout) {
			n = f->waited += timeout;
			n /= HZ;
			if (n > aoe_deadsecs) { /* waited too long for response */
				aoedev_downdev(d);
				break;
			}
			rexmit(d, f);
		}
	}
	if (d->flags & DEVFL_KICKME) {
		d->flags &= ~DEVFL_KICKME;
		aoecmd_work(d);
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

	if (d->ssize != ssize)
		printk(KERN_INFO "aoe: %012llx e%lu.%lu v%04x has %llu sectors\n",
			(unsigned long long)mac_addr(d->addr),
			d->aoemajor, d->aoeminor,
			d->fw_ver, (long long)ssize);
	d->ssize = ssize;
	d->geo.start = 0;
	if (d->gd != NULL) {
		d->gd->capacity = ssize;
		d->flags |= DEVFL_NEWSIZE;
	} else {
		if (d->flags & DEVFL_GDALLOC) {
			printk(KERN_ERR "aoe: can't schedule work for e%lu.%lu, %s\n",
			       d->aoemajor, d->aoeminor,
			       "it's already on!  This shouldn't happen.\n");
			return;
		}
		d->flags |= DEVFL_GDALLOC;
	}
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

void
aoecmd_ata_rsp(struct sk_buff *skb)
{
	struct aoedev *d;
	struct aoe_hdr *hin, *hout;
	struct aoe_atahdr *ahin, *ahout;
	struct frame *f;
	struct buf *buf;
	struct sk_buff *sl;
	register long n;
	ulong flags;
	char ebuf[128];
	u16 aoemajor;

	hin = aoe_hdr(skb);
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
	f = getframe(d, n);
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
	hout = aoe_hdr(f->skb);
	ahout = (struct aoe_atahdr *) (hout+1);
	buf = f->buf;

	if (ahout->cmdstat == WIN_IDENTIFY)
		d->flags &= ~DEVFL_PAUSE;
	if (ahin->cmdstat & 0xa9) {	/* these bits cleared on success */
		printk(KERN_ERR
			"aoe: ata error cmd=%2.2Xh stat=%2.2Xh from e%ld.%ld\n",
			ahout->cmdstat, ahin->cmdstat,
			d->aoemajor, d->aoeminor);
		if (buf)
			buf->flags |= BUFFL_FAIL;
	} else {
		n = ahout->scnt << 9;
		switch (ahout->cmdstat) {
		case WIN_READ:
		case WIN_READ_EXT:
			if (skb->len - sizeof *hin - sizeof *ahin < n) {
				printk(KERN_ERR
					"aoe: runt data size in read.  skb->len=%d\n",
					skb->len);
				/* fail frame f?  just returning will rexmit. */
				spin_unlock_irqrestore(&d->lock, flags);
				return;
			}
			memcpy(f->bufaddr, ahin+1, n);
		case WIN_WRITE:
		case WIN_WRITE_EXT:
			if (f->bcnt -= n) {
				skb = f->skb;
				f->bufaddr += n;
				put_lba(ahout, f->lba += ahout->scnt);
				n = f->bcnt;
				if (n > DEFAULTBCNT)
					n = DEFAULTBCNT;
				ahout->scnt = n >> 9;
				if (ahout->aflags & AOEAFL_WRITE) {
					skb_fill_page_desc(skb, 0,
						virt_to_page(f->bufaddr),
						offset_in_page(f->bufaddr), n);
					skb->len = sizeof *hout + sizeof *ahout + n;
					skb->data_len = n;
				}
				f->tag = newtag(d);
				hout->tag = cpu_to_be32(f->tag);
				skb->dev = d->ifp;
				skb = skb_clone(skb, GFP_ATOMIC);
				spin_unlock_irqrestore(&d->lock, flags);
				if (skb)
					aoenet_xmit(skb);
				return;
			}
			if (n > DEFAULTBCNT)
				d->lostjumbo = 0;
			break;
		case WIN_IDENTIFY:
			if (skb->len - sizeof *hin - sizeof *ahin < 512) {
				printk(KERN_INFO
					"aoe: runt data size in ataid.  skb->len=%d\n",
					skb->len);
				spin_unlock_irqrestore(&d->lock, flags);
				return;
			}
			ataid_complete(d, (char *) (ahin+1));
			break;
		default:
			printk(KERN_INFO
				"aoe: unrecognized ata command %2.2Xh for %d.%d\n",
				ahout->cmdstat,
				be16_to_cpu(get_unaligned(&hin->major)),
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
			bio_endio(buf->bio, n);
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
	struct sk_buff *sl;

	sl = aoecmd_cfg_pkts(aoemajor, aoeminor, NULL);

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

	f = freeframe(d);
	if (f == NULL) {
		printk(KERN_ERR "aoe: can't get a frame. This shouldn't happen.\n");
		return NULL;
	}

	/* initialize the headers & frame */
	skb = f->skb;
	h = aoe_hdr(skb);
	ah = (struct aoe_atahdr *) (h+1);
	skb_put(skb, sizeof *h + sizeof *ah);
	memset(h, 0, skb->len);
	f->tag = aoehdr_atainit(d, h);
	f->waited = 0;

	/* set up ata header */
	ah->scnt = 1;
	ah->cmdstat = WIN_IDENTIFY;
	ah->lba3 = 0xa0;

	skb->dev = d->ifp;

	d->rttavg = MAXTIMER;
	d->timer.function = rexmit_timer;

	return skb_clone(skb, GFP_ATOMIC);
}
 
void
aoecmd_cfg_rsp(struct sk_buff *skb)
{
	struct aoedev *d;
	struct aoe_hdr *h;
	struct aoe_cfghdr *ch;
	ulong flags, sysminor, aoemajor;
	struct sk_buff *sl;
	enum { MAXFRAMES = 16 };
	u16 n;

	h = aoe_hdr(skb);
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
	if (n > MAXFRAMES)	/* keep it reasonable */
		n = MAXFRAMES;

	d = aoedev_by_sysminor_m(sysminor, n);
	if (d == NULL) {
		printk(KERN_INFO "aoe: device sysminor_m failure\n");
		return;
	}

	spin_lock_irqsave(&d->lock, flags);

	/* permit device to migrate mac and network interface */
	d->ifp = skb->dev;
	memcpy(d->addr, h->src, sizeof d->addr);
	if (!(d->flags & DEVFL_MAXBCNT)) {
		n = d->ifp->mtu;
		n -= sizeof (struct aoe_hdr) + sizeof (struct aoe_atahdr);
		n /= 512;
		if (n > ch->scnt)
			n = ch->scnt;
		n = n ? n * 512 : DEFAULTBCNT;
		if (n != d->maxbcnt) {
			printk(KERN_INFO
				"aoe: e%ld.%ld: setting %d byte data frames on %s\n",
				d->aoemajor, d->aoeminor, n, d->ifp->name);
			d->maxbcnt = n;
		}
	}

	/* don't change users' perspective */
	if (d->nopen && !(d->flags & DEVFL_PAUSE)) {
		spin_unlock_irqrestore(&d->lock, flags);
		return;
	}
	d->flags |= DEVFL_PAUSE;	/* force pause */
	d->mintimer = MINTIMER;
	d->fw_ver = be16_to_cpu(ch->fwver);

	/* check for already outstanding ataid */
	sl = aoedev_isbusy(d) == 0 ? aoecmd_ata_id(d) : NULL;

	spin_unlock_irqrestore(&d->lock, flags);

	aoenet_xmit(sl);
}

