/* Copyright (c) 2013 Coraid, Inc.  See COPYING for GPL terms. */
/*
 * aoeblk.c
 * block device routines
 */

#include <linux/kernel.h>
#include <linux/hdreg.h>
#include <linux/blk-mq.h>
#include <linux/backing-dev.h>
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/slab.h>
#include <linux/ratelimit.h>
#include <linux/genhd.h>
#include <linux/netdevice.h>
#include <linux/mutex.h>
#include <linux/export.h>
#include <linux/moduleparam.h>
#include <linux/debugfs.h>
#include <scsi/sg.h>
#include "aoe.h"

static DEFINE_MUTEX(aoeblk_mutex);
static struct kmem_cache *buf_pool_cache;
static struct dentry *aoe_debugfs_dir;

/* GPFS needs a larger value than the default. */
static int aoe_maxsectors;
module_param(aoe_maxsectors, int, 0644);
MODULE_PARM_DESC(aoe_maxsectors,
	"When nonzero, set the maximum number of sectors per I/O request");

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
	te = t + d->ntargets;
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
		p += scnprintf(p, PAGE_SIZE - (p-page), "%s%s",
			p == page ? "" : ",", (*nd)->name);
	p += scnprintf(p, PAGE_SIZE - (p-page), "\n");
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
static ssize_t aoedisk_show_payload(struct device *dev,
				    struct device_attribute *attr, char *page)
{
	struct gendisk *disk = dev_to_disk(dev);
	struct aoedev *d = disk->private_data;

	return snprintf(page, PAGE_SIZE, "%lu\n", d->maxbcnt);
}

static int aoedisk_debugfs_show(struct seq_file *s, void *ignored)
{
	struct aoedev *d;
	struct aoetgt **t, **te;
	struct aoeif *ifp, *ife;
	unsigned long flags;
	char c;

	d = s->private;
	seq_printf(s, "rttavg: %d rttdev: %d\n",
		d->rttavg >> RTTSCALE,
		d->rttdev >> RTTDSCALE);
	seq_printf(s, "nskbpool: %d\n", skb_queue_len(&d->skbpool));
	seq_printf(s, "kicked: %ld\n", d->kicked);
	seq_printf(s, "maxbcnt: %ld\n", d->maxbcnt);
	seq_printf(s, "ref: %ld\n", d->ref);

	spin_lock_irqsave(&d->lock, flags);
	t = d->targets;
	te = t + d->ntargets;
	for (; t < te && *t; t++) {
		c = '\t';
		seq_printf(s, "falloc: %ld\n", (*t)->falloc);
		seq_printf(s, "ffree: %p\n",
			list_empty(&(*t)->ffree) ? NULL : (*t)->ffree.next);
		seq_printf(s, "%pm:%d:%d:%d\n", (*t)->addr, (*t)->nout,
			(*t)->maxout, (*t)->nframes);
		seq_printf(s, "\tssthresh:%d\n", (*t)->ssthresh);
		seq_printf(s, "\ttaint:%d\n", (*t)->taint);
		seq_printf(s, "\tr:%d\n", (*t)->rpkts);
		seq_printf(s, "\tw:%d\n", (*t)->wpkts);
		ifp = (*t)->ifs;
		ife = ifp + ARRAY_SIZE((*t)->ifs);
		for (; ifp->nd && ifp < ife; ifp++) {
			seq_printf(s, "%c%s", c, ifp->nd->name);
			c = ',';
		}
		seq_puts(s, "\n");
	}
	spin_unlock_irqrestore(&d->lock, flags);

	return 0;
}

static int aoe_debugfs_open(struct inode *inode, struct file *file)
{
	return single_open(file, aoedisk_debugfs_show, inode->i_private);
}

static DEVICE_ATTR(state, 0444, aoedisk_show_state, NULL);
static DEVICE_ATTR(mac, 0444, aoedisk_show_mac, NULL);
static DEVICE_ATTR(netif, 0444, aoedisk_show_netif, NULL);
static struct device_attribute dev_attr_firmware_version = {
	.attr = { .name = "firmware-version", .mode = 0444 },
	.show = aoedisk_show_fwver,
};
static DEVICE_ATTR(payload, 0444, aoedisk_show_payload, NULL);

static struct attribute *aoe_attrs[] = {
	&dev_attr_state.attr,
	&dev_attr_mac.attr,
	&dev_attr_netif.attr,
	&dev_attr_firmware_version.attr,
	&dev_attr_payload.attr,
	NULL,
};

static const struct attribute_group aoe_attr_group = {
	.attrs = aoe_attrs,
};

static const struct attribute_group *aoe_attr_groups[] = {
	&aoe_attr_group,
	NULL,
};

static const struct file_operations aoe_debugfs_fops = {
	.open = aoe_debugfs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static void
aoedisk_add_debugfs(struct aoedev *d)
{
	char *p;

	if (aoe_debugfs_dir == NULL)
		return;
	p = strchr(d->gd->disk_name, '/');
	if (p == NULL)
		p = d->gd->disk_name;
	else
		p++;
	BUG_ON(*p == '\0');
	d->debugfs = debugfs_create_file(p, 0444, aoe_debugfs_dir, d,
					 &aoe_debugfs_fops);
}
void
aoedisk_rm_debugfs(struct aoedev *d)
{
	debugfs_remove(d->debugfs);
	d->debugfs = NULL;
}

static int
aoeblk_open(struct block_device *bdev, fmode_t mode)
{
	struct aoedev *d = bdev->bd_disk->private_data;
	ulong flags;

	if (!virt_addr_valid(d)) {
		pr_crit("aoe: invalid device pointer in %s\n",
			__func__);
		WARN_ON(1);
		return -ENODEV;
	}
	if (!(d->flags & DEVFL_UP) || d->flags & DEVFL_TKILL)
		return -ENODEV;

	mutex_lock(&aoeblk_mutex);
	spin_lock_irqsave(&d->lock, flags);
	if (d->flags & DEVFL_UP && !(d->flags & DEVFL_TKILL)) {
		d->nopen++;
		spin_unlock_irqrestore(&d->lock, flags);
		mutex_unlock(&aoeblk_mutex);
		return 0;
	}
	spin_unlock_irqrestore(&d->lock, flags);
	mutex_unlock(&aoeblk_mutex);
	return -ENODEV;
}

static void
aoeblk_release(struct gendisk *disk, fmode_t mode)
{
	struct aoedev *d = disk->private_data;
	ulong flags;

	spin_lock_irqsave(&d->lock, flags);

	if (--d->nopen == 0) {
		spin_unlock_irqrestore(&d->lock, flags);
		aoecmd_cfg(d->aoemajor, d->aoeminor);
		return;
	}
	spin_unlock_irqrestore(&d->lock, flags);
}

static blk_status_t aoeblk_queue_rq(struct blk_mq_hw_ctx *hctx,
				    const struct blk_mq_queue_data *bd)
{
	struct aoedev *d = hctx->queue->queuedata;

	spin_lock_irq(&d->lock);

	if ((d->flags & DEVFL_UP) == 0) {
		pr_info_ratelimited("aoe: device %ld.%d is not up\n",
			d->aoemajor, d->aoeminor);
		spin_unlock_irq(&d->lock);
		blk_mq_start_request(bd->rq);
		return BLK_STS_IOERR;
	}

	list_add_tail(&bd->rq->queuelist, &d->rq_list);
	aoecmd_work(d);
	spin_unlock_irq(&d->lock);
	return BLK_STS_OK;
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

static int
aoeblk_ioctl(struct block_device *bdev, fmode_t mode, uint cmd, ulong arg)
{
	struct aoedev *d;

	if (!arg)
		return -EINVAL;

	d = bdev->bd_disk->private_data;
	if ((d->flags & DEVFL_UP) == 0) {
		pr_err("aoe: disk not up\n");
		return -ENODEV;
	}

	if (cmd == HDIO_GET_IDENTITY) {
		if (!copy_to_user((void __user *) arg, &d->ident,
			sizeof(d->ident)))
			return 0;
		return -EFAULT;
	}

	/* udev calls scsi_id, which uses SG_IO, resulting in noise */
	if (cmd != SG_IO)
		pr_info("aoe: unknown ioctl 0x%x\n", cmd);

	return -ENOTTY;
}

static const struct block_device_operations aoe_bdops = {
	.open = aoeblk_open,
	.release = aoeblk_release,
	.ioctl = aoeblk_ioctl,
	.compat_ioctl = blkdev_compat_ptr_ioctl,
	.getgeo = aoeblk_getgeo,
	.owner = THIS_MODULE,
};

static const struct blk_mq_ops aoeblk_mq_ops = {
	.queue_rq	= aoeblk_queue_rq,
};

/* blk_mq_alloc_disk and add_disk can sleep */
void
aoeblk_gdalloc(void *vp)
{
	struct aoedev *d = vp;
	struct gendisk *gd;
	mempool_t *mp;
	struct blk_mq_tag_set *set;
	ulong flags;
	int late = 0;
	int err;

	spin_lock_irqsave(&d->lock, flags);
	if (d->flags & DEVFL_GDALLOC
	&& !(d->flags & DEVFL_TKILL)
	&& !(d->flags & DEVFL_GD_NOW))
		d->flags |= DEVFL_GD_NOW;
	else
		late = 1;
	spin_unlock_irqrestore(&d->lock, flags);
	if (late)
		return;

	mp = mempool_create(MIN_BUFS, mempool_alloc_slab, mempool_free_slab,
		buf_pool_cache);
	if (mp == NULL) {
		printk(KERN_ERR "aoe: cannot allocate bufpool for %ld.%d\n",
			d->aoemajor, d->aoeminor);
		goto err;
	}

	set = &d->tag_set;
	set->ops = &aoeblk_mq_ops;
	set->cmd_size = sizeof(struct aoe_req);
	set->nr_hw_queues = 1;
	set->queue_depth = 128;
	set->numa_node = NUMA_NO_NODE;
	set->flags = BLK_MQ_F_SHOULD_MERGE;
	err = blk_mq_alloc_tag_set(set);
	if (err) {
		pr_err("aoe: cannot allocate tag set for %ld.%d\n",
			d->aoemajor, d->aoeminor);
		goto err_mempool;
	}

	gd = blk_mq_alloc_disk(set, d);
	if (IS_ERR(gd)) {
		pr_err("aoe: cannot allocate block queue for %ld.%d\n",
			d->aoemajor, d->aoeminor);
		goto err_tagset;
	}

	spin_lock_irqsave(&d->lock, flags);
	WARN_ON(!(d->flags & DEVFL_GD_NOW));
	WARN_ON(!(d->flags & DEVFL_GDALLOC));
	WARN_ON(d->flags & DEVFL_TKILL);
	WARN_ON(d->gd);
	WARN_ON(d->flags & DEVFL_UP);
	blk_queue_max_hw_sectors(gd->queue, BLK_DEF_MAX_SECTORS);
	blk_queue_io_opt(gd->queue, SZ_2M);
	d->bufpool = mp;
	d->blkq = gd->queue;
	d->gd = gd;
	if (aoe_maxsectors)
		blk_queue_max_hw_sectors(gd->queue, aoe_maxsectors);
	gd->major = AOE_MAJOR;
	gd->first_minor = d->sysminor;
	gd->minors = AOE_PARTITIONS;
	gd->fops = &aoe_bdops;
	gd->private_data = d;
	set_capacity(gd, d->ssize);
	snprintf(gd->disk_name, sizeof gd->disk_name, "etherd/e%ld.%d",
		d->aoemajor, d->aoeminor);

	d->flags &= ~DEVFL_GDALLOC;
	d->flags |= DEVFL_UP;

	spin_unlock_irqrestore(&d->lock, flags);

	device_add_disk(NULL, gd, aoe_attr_groups);
	aoedisk_add_debugfs(d);

	spin_lock_irqsave(&d->lock, flags);
	WARN_ON(!(d->flags & DEVFL_GD_NOW));
	d->flags &= ~DEVFL_GD_NOW;
	spin_unlock_irqrestore(&d->lock, flags);
	return;

err_tagset:
	blk_mq_free_tag_set(set);
err_mempool:
	mempool_destroy(mp);
err:
	spin_lock_irqsave(&d->lock, flags);
	d->flags &= ~DEVFL_GD_NOW;
	schedule_work(&d->work);
	spin_unlock_irqrestore(&d->lock, flags);
}

void
aoeblk_exit(void)
{
	debugfs_remove_recursive(aoe_debugfs_dir);
	aoe_debugfs_dir = NULL;
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
	aoe_debugfs_dir = debugfs_create_dir("aoe", NULL);
	return 0;
}

