/* Copyright (c) 2006 Coraid, Inc.  See COPYING for GPL terms. */
/*
 * aoeblk.c
 * block device routines
 */

#include <linux/hdreg.h>
#include <linux/blkdev.h>
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/genhd.h>
#include <linux/netdevice.h>
#include "aoe.h"

static kmem_cache_t *buf_pool_cache;

static ssize_t aoedisk_show_state(struct gendisk * disk, char *page)
{
	struct aoedev *d = disk->private_data;

	return snprintf(page, PAGE_SIZE,
			"%s%s\n",
			(d->flags & DEVFL_UP) ? "up" : "down",
			(d->flags & DEVFL_PAUSE) ? ",paused" :
			(d->nopen && !(d->flags & DEVFL_UP)) ? ",closewait" : "");
	/* I'd rather see nopen exported so we can ditch closewait */
}
static ssize_t aoedisk_show_mac(struct gendisk * disk, char *page)
{
	struct aoedev *d = disk->private_data;

	return snprintf(page, PAGE_SIZE, "%012llx\n",
			(unsigned long long)mac_addr(d->addr));
}
static ssize_t aoedisk_show_netif(struct gendisk * disk, char *page)
{
	struct aoedev *d = disk->private_data;

	return snprintf(page, PAGE_SIZE, "%s\n", d->ifp->name);
}
/* firmware version */
static ssize_t aoedisk_show_fwver(struct gendisk * disk, char *page)
{
	struct aoedev *d = disk->private_data;

	return snprintf(page, PAGE_SIZE, "0x%04x\n", (unsigned int) d->fw_ver);
}

static struct disk_attribute disk_attr_state = {
	.attr = {.name = "state", .mode = S_IRUGO },
	.show = aoedisk_show_state
};
static struct disk_attribute disk_attr_mac = {
	.attr = {.name = "mac", .mode = S_IRUGO },
	.show = aoedisk_show_mac
};
static struct disk_attribute disk_attr_netif = {
	.attr = {.name = "netif", .mode = S_IRUGO },
	.show = aoedisk_show_netif
};
static struct disk_attribute disk_attr_fwver = {
	.attr = {.name = "firmware-version", .mode = S_IRUGO },
	.show = aoedisk_show_fwver
};

static struct attribute *aoe_attrs[] = {
	&disk_attr_state.attr,
	&disk_attr_mac.attr,
	&disk_attr_netif.attr,
	&disk_attr_fwver.attr,
	NULL
};

static const struct attribute_group attr_group = {
	.attrs = aoe_attrs,
};

static int
aoedisk_add_sysfs(struct aoedev *d)
{
	return sysfs_create_group(&d->gd->kobj, &attr_group);
}
void
aoedisk_rm_sysfs(struct aoedev *d)
{
	sysfs_remove_group(&d->gd->kobj, &attr_group);
}

static int
aoeblk_open(struct inode *inode, struct file *filp)
{
	struct aoedev *d;
	ulong flags;

	d = inode->i_bdev->bd_disk->private_data;

	spin_lock_irqsave(&d->lock, flags);
	if (d->flags & DEVFL_UP) {
		d->nopen++;
		spin_unlock_irqrestore(&d->lock, flags);
		return 0;
	}
	spin_unlock_irqrestore(&d->lock, flags);
	return -ENODEV;
}

static int
aoeblk_release(struct inode *inode, struct file *filp)
{
	struct aoedev *d;
	ulong flags;

	d = inode->i_bdev->bd_disk->private_data;

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
aoeblk_make_request(request_queue_t *q, struct bio *bio)
{
	struct aoedev *d;
	struct buf *buf;
	struct sk_buff *sl;
	ulong flags;

	blk_queue_bounce(q, &bio);

	d = bio->bi_bdev->bd_disk->private_data;
	buf = mempool_alloc(d->bufpool, GFP_NOIO);
	if (buf == NULL) {
		printk(KERN_INFO "aoe: buf allocation failure\n");
		bio_endio(bio, bio->bi_size, -ENOMEM);
		return 0;
	}
	memset(buf, 0, sizeof(*buf));
	INIT_LIST_HEAD(&buf->bufs);
	buf->start_time = jiffies;
	buf->bio = bio;
	buf->resid = bio->bi_size;
	buf->sector = bio->bi_sector;
	buf->bv = &bio->bi_io_vec[bio->bi_idx];
	WARN_ON(buf->bv->bv_len == 0);
	buf->bv_resid = buf->bv->bv_len;
	buf->bufaddr = page_address(buf->bv->bv_page) + buf->bv->bv_offset;

	spin_lock_irqsave(&d->lock, flags);

	if ((d->flags & DEVFL_UP) == 0) {
		printk(KERN_INFO "aoe: device %ld.%ld is not up\n",
			d->aoemajor, d->aoeminor);
		spin_unlock_irqrestore(&d->lock, flags);
		mempool_free(buf, d->bufpool);
		bio_endio(bio, bio->bi_size, -ENXIO);
		return 0;
	}

	list_add_tail(&buf->bufs, &d->bufq);

	aoecmd_work(d);
	sl = d->sendq_hd;
	d->sendq_hd = d->sendq_tl = NULL;

	spin_unlock_irqrestore(&d->lock, flags);
	aoenet_xmit(sl);

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

static struct block_device_operations aoe_bdops = {
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
		printk(KERN_ERR "aoe: cannot allocate disk structure for %ld.%ld\n",
			d->aoemajor, d->aoeminor);
		spin_lock_irqsave(&d->lock, flags);
		d->flags &= ~DEVFL_GDALLOC;
		spin_unlock_irqrestore(&d->lock, flags);
		return;
	}

	d->bufpool = mempool_create_slab_pool(MIN_BUFS, buf_pool_cache);
	if (d->bufpool == NULL) {
		printk(KERN_ERR "aoe: cannot allocate bufpool for %ld.%ld\n",
			d->aoemajor, d->aoeminor);
		put_disk(gd);
		spin_lock_irqsave(&d->lock, flags);
		d->flags &= ~DEVFL_GDALLOC;
		spin_unlock_irqrestore(&d->lock, flags);
		return;
	}

	spin_lock_irqsave(&d->lock, flags);
	blk_queue_make_request(&d->blkq, aoeblk_make_request);
	gd->major = AOE_MAJOR;
	gd->first_minor = d->sysminor * AOE_PARTITIONS;
	gd->fops = &aoe_bdops;
	gd->private_data = d;
	gd->capacity = d->ssize;
	snprintf(gd->disk_name, sizeof gd->disk_name, "etherd/e%ld.%ld",
		d->aoemajor, d->aoeminor);

	gd->queue = &d->blkq;
	d->gd = gd;
	d->flags &= ~DEVFL_GDALLOC;
	d->flags |= DEVFL_UP;

	spin_unlock_irqrestore(&d->lock, flags);

	add_disk(gd);
	aoedisk_add_sysfs(d);
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
					   0, 0, NULL, NULL);
	if (buf_pool_cache == NULL)
		return -ENOMEM;

	return 0;
}

