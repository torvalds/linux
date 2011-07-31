/* Copyright (c) 2007 Coraid, Inc.  See COPYING for GPL terms. */
/*
 * aoeblk.c
 * block device routines
 */

#include <linux/hdreg.h>
#include <linux/blkdev.h>
#include <linux/backing-dev.h>
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/slab.h>
#include <linux/genhd.h>
#include <linux/netdevice.h>
#include <linux/smp_lock.h>
#include "aoe.h"

static struct kmem_cache *buf_pool_cache;

static ssize_t aoedisk_show_state(struct device *dev,
				  struct device_attribute *attr, char *page)
{
	struct gendisk *disk = dev_to_disk(dev);
	struct aoedev *d = disk->private_data;

	return snprintf(page, PAGE_SIZE,
			"%s%s\n",
			(d->flags & DEVFL_UP) ? "up" : "down",
			(d->flags & DEVFL_KICKME) ? ",kickme" :
			(d->nopen && !(d->flags & DEVFL_UP)) ? ",closewait" : "");
	/* I'd rather see nopen exported so we can ditch closewait */
}
static ssize_t aoedisk_show_mac(struct device *dev,
				struct device_attribute *attr, char *page)
{
	struct gendisk *disk = dev_to_disk(dev);
	struct aoedev *d = disk->private_data;
	struct aoetgt *t = d->targets[0];

	if (t == NULL)
		return snprintf(page, PAGE_SIZE, "none\n");
	return snprintf(page, PAGE_SIZE, "%pm\n", t->addr);
}
static ssize_t aoedisk_show_netif(struct device *dev,
				  struct device_attribute *attr, char *page)
{
	struct gendisk *disk = dev_to_disk(dev);
	struct aoedev *d = disk->private_data;
	struct net_device *nds[8], **nd, **nnd, **ne;
	struct aoetgt **t, **te;
	struct aoeif *ifp, *e;
	char *p;

	memset(nds, 0, sizeof nds);
	nd = nds;
	ne = nd + ARRAY_SIZE(nds);
	t = d->targets;
	te = t + NTARGETS;
	for (; t < te && *t; t++) {
		ifp = (*t)->ifs;
		e = ifp + NAOEIFS;
		for (; ifp < e && ifp->nd; ifp++) {
			for (nnd = nds; nnd < nd; nnd++)
				if (*nnd == ifp->nd)
					break;
			if (nnd == nd && nd != ne)
				*nd++ = ifp->nd;
		}
	}

	ne = nd;
	nd = nds;
	if (*nd == NULL)
		return snprintf(page, PAGE_SIZE, "none\n");
	for (p = page; nd < ne; nd++)
		p += snprintf(p, PAGE_SIZE - (p-page), "%s%s",
			p == page ? "" : ",", (*nd)->name);
	p += snprintf(p, PAGE_SIZE - (p-page), "\n");
	return p-page;
}
/* firmware version */
static ssize_t aoedisk_show_fwver(struct device *dev,
				  struct device_attribute *attr, char *page)
{
	struct gendisk *disk = dev_to_disk(dev);
	struct aoedev *d = disk->private_data;

	return snprintf(page, PAGE_SIZE, "0x%04x\n", (unsigned int) d->fw_ver);
}

static DEVICE_ATTR(state, S_IRUGO, aoedisk_show_state, NULL);
static DEVICE_ATTR(mac, S_IRUGO, aoedisk_show_mac, NULL);
static DEVICE_ATTR(netif, S_IRUGO, aoedisk_show_netif, NULL);
static struct device_attribute dev_attr_firmware_version = {
	.attr = { .name = "firmware-version", .mode = S_IRUGO },
	.show = aoedisk_show_fwver,
};

static struct attribute *aoe_attrs[] = {
	&dev_attr_state.attr,
	&dev_attr_mac.attr,
	&dev_attr_netif.attr,
	&dev_attr_firmware_version.attr,
	NULL,
};

static const struct attribute_group attr_group = {
	.attrs = aoe_attrs,
};

static int
aoedisk_add_sysfs(struct aoedev *d)
{
	return sysfs_create_group(&disk_to_dev(d->gd)->kobj, &attr_group);
}
void
aoedisk_rm_sysfs(struct aoedev *d)
{
	sysfs_remove_group(&disk_to_dev(d->gd)->kobj, &attr_group);
}

static int
aoeblk_open(struct block_device *bdev, fmode_t mode)
{
	struct aoedev *d = bdev->bd_disk->private_data;
	ulong flags;

	lock_kernel();
	spin_lock_irqsave(&d->lock, flags);
	if (d->flags & DEVFL_UP) {
		d->nopen++;
		spin_unlock_irqrestore(&d->lock, flags);
		unlock_kernel();
		return 0;
	}
	spin_unlock_irqrestore(&d->lock, flags);
	unlock_kernel();
	return -ENODEV;
}

static int
aoeblk_release(struct gendisk *disk, fmode_t mode)
{
	struct aoedev *d = disk->private_data;
	ulong flags;

	spin_lock_irqsave(&d->lock, flags);

	if (--d->nopen == 0) {
		spin_unlock_irqrestore(&d->lock, flags);
		aoecmd_cfg(d->aoemajor, d->aoeminor);
		return 0;
	}
	spin_unlock_irqrestore(&d->lock, flags);

	return 0;
}

static int
aoeblk_make_request(struct request_queue *q, struct bio *bio)
{
	struct sk_buff_head queue;
	struct aoedev *d;
	struct buf *buf;
	ulong flags;

	blk_queue_bounce(q, &bio);

	if (bio == NULL) {
		printk(KERN_ERR "aoe: bio is NULL\n");
		BUG();
		return 0;
	}
	d = bio->bi_bdev->bd_disk->private_data;
	if (d == NULL) {
		printk(KERN_ERR "aoe: bd_disk->private_data is NULL\n");
		BUG();
		bio_endio(bio, -ENXIO);
		return 0;
	} else if (bio->bi_rw & REQ_HARDBARRIER) {
		bio_endio(bio, -EOPNOTSUPP);
		return 0;
	} else if (bio->bi_io_vec == NULL) {
		printk(KERN_ERR "aoe: bi_io_vec is NULL\n");
		BUG();
		bio_endio(bio, -ENXIO);
		return 0;
	}
	buf = mempool_alloc(d->bufpool, GFP_NOIO);
	if (buf == NULL) {
		printk(KERN_INFO "aoe: buf allocation failure\n");
		bio_endio(bio, -ENOMEM);
		return 0;
	}
	memset(buf, 0, sizeof(*buf));
	INIT_LIST_HEAD(&buf->bufs);
	buf->stime = jiffies;
	buf->bio = bio;
	buf->resid = bio->bi_size;
	buf->sector = bio->bi_sector;
	buf->bv = &bio->bi_io_vec[bio->bi_idx];
	buf->bv_resid = buf->bv->bv_len;
	WARN_ON(buf->bv_resid == 0);
	buf->bv_off = buf->bv->bv_offset;

	spin_lock_irqsave(&d->lock, flags);

	if ((d->flags & DEVFL_UP) == 0) {
		printk(KERN_INFO "aoe: device %ld.%d is not up\n",
			d->aoemajor, d->aoeminor);
		spin_unlock_irqrestore(&d->lock, flags);
		mempool_free(buf, d->bufpool);
		bio_endio(bio, -ENXIO);
		return 0;
	}

	list_add_tail(&buf->bufs, &d->bufq);

	aoecmd_work(d);
	__skb_queue_head_init(&queue);
	skb_queue_splice_init(&d->sendq, &queue);

	spin_unlock_irqrestore(&d->lock, flags);
	aoenet_xmit(&queue);

	return 0;
}

static int
aoeblk_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	struct aoedev *d = bdev->bd_disk->private_data;

	if ((d->flags & DEVFL_UP) == 0) {
		printk(KERN_ERR "aoe: disk not up\n");
		return -ENODEV;
	}

	geo->cylinders = d->geo.cylinders;
	geo->heads = d->geo.heads;
	geo->sectors = d->geo.sectors;
	return 0;
}

static const struct block_device_operations aoe_bdops = {
	.open = aoeblk_open,
	.release = aoeblk_release,
	.getgeo = aoeblk_getgeo,
	.owner = THIS_MODULE,
};

/* alloc_disk and add_disk can sleep */
void
aoeblk_gdalloc(void *vp)
{
	struct aoedev *d = vp;
	struct gendisk *gd;
	ulong flags;

	gd = alloc_disk(AOE_PARTITIONS);
	if (gd == NULL) {
		printk(KERN_ERR
			"aoe: cannot allocate disk structure for %ld.%d\n",
			d->aoemajor, d->aoeminor);
		goto err;
	}

	d->bufpool = mempool_create_slab_pool(MIN_BUFS, buf_pool_cache);
	if (d->bufpool == NULL) {
		printk(KERN_ERR "aoe: cannot allocate bufpool for %ld.%d\n",
			d->aoemajor, d->aoeminor);
		goto err_disk;
	}

	d->blkq = blk_alloc_queue(GFP_KERNEL);
	if (!d->blkq)
		goto err_mempool;
	blk_queue_make_request(d->blkq, aoeblk_make_request);
	d->blkq->backing_dev_info.name = "aoe";
	if (bdi_init(&d->blkq->backing_dev_info))
		goto err_blkq;
	spin_lock_irqsave(&d->lock, flags);
	gd->major = AOE_MAJOR;
	gd->first_minor = d->sysminor * AOE_PARTITIONS;
	gd->fops = &aoe_bdops;
	gd->private_data = d;
	set_capacity(gd, d->ssize);
	snprintf(gd->disk_name, sizeof gd->disk_name, "etherd/e%ld.%d",
		d->aoemajor, d->aoeminor);

	gd->queue = d->blkq;
	d->gd = gd;
	d->flags &= ~DEVFL_GDALLOC;
	d->flags |= DEVFL_UP;

	spin_unlock_irqrestore(&d->lock, flags);

	add_disk(gd);
	aoedisk_add_sysfs(d);
	return;

err_blkq:
	blk_cleanup_queue(d->blkq);
	d->blkq = NULL;
err_mempool:
	mempool_destroy(d->bufpool);
err_disk:
	put_disk(gd);
err:
	spin_lock_irqsave(&d->lock, flags);
	d->flags &= ~DEVFL_GDALLOC;
	spin_unlock_irqrestore(&d->lock, flags);
}

void
aoeblk_exit(void)
{
	kmem_cache_destroy(buf_pool_cache);
}

int __init
aoeblk_init(void)
{
	buf_pool_cache = kmem_cache_create("aoe_bufs",
					   sizeof(struct buf),
					   0, 0, NULL);
	if (buf_pool_cache == NULL)
		return -ENOMEM;

	return 0;
}

