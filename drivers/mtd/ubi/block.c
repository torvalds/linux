// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014 Ezequiel Garcia
 * Copyright (c) 2011 Free Electrons
 *
 * Driver parameter handling strongly based on drivers/mtd/ubi/build.c
 *   Copyright (c) International Business Machines Corp., 2006
 *   Copyright (c) Nokia Corporation, 2007
 *   Authors: Artem Bityutskiy, Frank Haverkamp
 */

/*
 * Read-only block devices on top of UBI volumes
 *
 * A simple implementation to allow a block device to be layered on top of a
 * UBI volume. The implementation is provided by creating a static 1-to-1
 * mapping between the block device and the UBI volume.
 *
 * The addressed byte is obtained from the addressed block sector, which is
 * mapped linearly into the corresponding LEB:
 *
 *   LEB number = addressed byte / LEB size
 *
 * This feature is compiled in the UBI core, and adds a 'block' parameter
 * to allow early creation of block devices on top of UBI volumes. Runtime
 * block creation/removal for UBI volumes is provided through two UBI ioctls:
 * UBI_IOCVOLCRBLK and UBI_IOCVOLRMBLK.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/mtd/ubi.h>
#include <linux/workqueue.h>
#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/hdreg.h>
#include <linux/scatterlist.h>
#include <linux/idr.h>
#include <asm/div64.h>

#include "ubi-media.h"
#include "ubi.h"

/* Maximum number of supported devices */
#define UBIBLOCK_MAX_DEVICES 32

/* Maximum length of the 'block=' parameter */
#define UBIBLOCK_PARAM_LEN 63

/* Maximum number of comma-separated items in the 'block=' parameter */
#define UBIBLOCK_PARAM_COUNT 2

struct ubiblock_param {
	int ubi_num;
	int vol_id;
	char name[UBIBLOCK_PARAM_LEN+1];
};

struct ubiblock_pdu {
	struct work_struct work;
	struct ubi_sgl usgl;
};

/* Numbers of elements set in the @ubiblock_param array */
static int ubiblock_devs __initdata;

/* MTD devices specification parameters */
static struct ubiblock_param ubiblock_param[UBIBLOCK_MAX_DEVICES] __initdata;

struct ubiblock {
	struct ubi_volume_desc *desc;
	int ubi_num;
	int vol_id;
	int refcnt;
	int leb_size;

	struct gendisk *gd;
	struct request_queue *rq;

	struct workqueue_struct *wq;

	struct mutex dev_mutex;
	struct list_head list;
	struct blk_mq_tag_set tag_set;
};

/* Linked list of all ubiblock instances */
static LIST_HEAD(ubiblock_devices);
static DEFINE_IDR(ubiblock_minor_idr);
/* Protects ubiblock_devices and ubiblock_minor_idr */
static DEFINE_MUTEX(devices_mutex);
static int ubiblock_major;

static int __init ubiblock_set_param(const char *val,
				     const struct kernel_param *kp)
{
	int i, ret;
	size_t len;
	struct ubiblock_param *param;
	char buf[UBIBLOCK_PARAM_LEN];
	char *pbuf = &buf[0];
	char *tokens[UBIBLOCK_PARAM_COUNT];

	if (!val)
		return -EINVAL;

	len = strnlen(val, UBIBLOCK_PARAM_LEN);
	if (len == 0) {
		pr_warn("UBI: block: empty 'block=' parameter - ignored\n");
		return 0;
	}

	if (len == UBIBLOCK_PARAM_LEN) {
		pr_err("UBI: block: parameter \"%s\" is too long, max. is %d\n",
		       val, UBIBLOCK_PARAM_LEN);
		return -EINVAL;
	}

	strcpy(buf, val);

	/* Get rid of the final newline */
	if (buf[len - 1] == '\n')
		buf[len - 1] = '\0';

	for (i = 0; i < UBIBLOCK_PARAM_COUNT; i++)
		tokens[i] = strsep(&pbuf, ",");

	param = &ubiblock_param[ubiblock_devs];
	if (tokens[1]) {
		/* Two parameters: can be 'ubi, vol_id' or 'ubi, vol_name' */
		ret = kstrtoint(tokens[0], 10, &param->ubi_num);
		if (ret < 0)
			return -EINVAL;

		/* Second param can be a number or a name */
		ret = kstrtoint(tokens[1], 10, &param->vol_id);
		if (ret < 0) {
			param->vol_id = -1;
			strcpy(param->name, tokens[1]);
		}

	} else {
		/* One parameter: must be device path */
		strcpy(param->name, tokens[0]);
		param->ubi_num = -1;
		param->vol_id = -1;
	}

	ubiblock_devs++;

	return 0;
}

static const struct kernel_param_ops ubiblock_param_ops = {
	.set    = ubiblock_set_param,
};
module_param_cb(block, &ubiblock_param_ops, NULL, 0);
MODULE_PARM_DESC(block, "Attach block devices to UBI volumes. Parameter format: block=<path|dev,num|dev,name>.\n"
			"Multiple \"block\" parameters may be specified.\n"
			"UBI volumes may be specified by their number, name, or path to the device node.\n"
			"Examples\n"
			"Using the UBI volume path:\n"
			"ubi.block=/dev/ubi0_0\n"
			"Using the UBI device, and the volume name:\n"
			"ubi.block=0,rootfs\n"
			"Using both UBI device number and UBI volume number:\n"
			"ubi.block=0,0\n");

static struct ubiblock *find_dev_nolock(int ubi_num, int vol_id)
{
	struct ubiblock *dev;

	list_for_each_entry(dev, &ubiblock_devices, list)
		if (dev->ubi_num == ubi_num && dev->vol_id == vol_id)
			return dev;
	return NULL;
}

static int ubiblock_read(struct ubiblock_pdu *pdu)
{
	int ret, leb, offset, bytes_left, to_read;
	u64 pos;
	struct request *req = blk_mq_rq_from_pdu(pdu);
	struct ubiblock *dev = req->q->queuedata;

	to_read = blk_rq_bytes(req);
	pos = blk_rq_pos(req) << 9;

	/* Get LEB:offset address to read from */
	offset = do_div(pos, dev->leb_size);
	leb = pos;
	bytes_left = to_read;

	while (bytes_left) {
		/*
		 * We can only read one LEB at a time. Therefore if the read
		 * length is larger than one LEB size, we split the operation.
		 */
		if (offset + to_read > dev->leb_size)
			to_read = dev->leb_size - offset;

		ret = ubi_read_sg(dev->desc, leb, &pdu->usgl, offset, to_read);
		if (ret < 0)
			return ret;

		bytes_left -= to_read;
		to_read = bytes_left;
		leb += 1;
		offset = 0;
	}
	return 0;
}

static int ubiblock_open(struct block_device *bdev, fmode_t mode)
{
	struct ubiblock *dev = bdev->bd_disk->private_data;
	int ret;

	mutex_lock(&dev->dev_mutex);
	if (dev->refcnt > 0) {
		/*
		 * The volume is already open, just increase the reference
		 * counter.
		 */
		goto out_done;
	}

	/*
	 * We want users to be aware they should only mount us as read-only.
	 * It's just a paranoid check, as write requests will get rejected
	 * in any case.
	 */
	if (mode & FMODE_WRITE) {
		ret = -EROFS;
		goto out_unlock;
	}

	dev->desc = ubi_open_volume(dev->ubi_num, dev->vol_id, UBI_READONLY);
	if (IS_ERR(dev->desc)) {
		dev_err(disk_to_dev(dev->gd), "failed to open ubi volume %d_%d",
			dev->ubi_num, dev->vol_id);
		ret = PTR_ERR(dev->desc);
		dev->desc = NULL;
		goto out_unlock;
	}

out_done:
	dev->refcnt++;
	mutex_unlock(&dev->dev_mutex);
	return 0;

out_unlock:
	mutex_unlock(&dev->dev_mutex);
	return ret;
}

static void ubiblock_release(struct gendisk *gd, fmode_t mode)
{
	struct ubiblock *dev = gd->private_data;

	mutex_lock(&dev->dev_mutex);
	dev->refcnt--;
	if (dev->refcnt == 0) {
		ubi_close_volume(dev->desc);
		dev->desc = NULL;
	}
	mutex_unlock(&dev->dev_mutex);
}

static int ubiblock_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	/* Some tools might require this information */
	geo->heads = 1;
	geo->cylinders = 1;
	geo->sectors = get_capacity(bdev->bd_disk);
	geo->start = 0;
	return 0;
}

static const struct block_device_operations ubiblock_ops = {
	.owner = THIS_MODULE,
	.open = ubiblock_open,
	.release = ubiblock_release,
	.getgeo	= ubiblock_getgeo,
};

static void ubiblock_do_work(struct work_struct *work)
{
	int ret;
	struct ubiblock_pdu *pdu = container_of(work, struct ubiblock_pdu, work);
	struct request *req = blk_mq_rq_from_pdu(pdu);

	blk_mq_start_request(req);

	/*
	 * It is safe to ignore the return value of blk_rq_map_sg() because
	 * the number of sg entries is limited to UBI_MAX_SG_COUNT
	 * and ubi_read_sg() will check that limit.
	 */
	blk_rq_map_sg(req->q, req, pdu->usgl.sg);

	ret = ubiblock_read(pdu);
	rq_flush_dcache_pages(req);

	blk_mq_end_request(req, errno_to_blk_status(ret));
}

static blk_status_t ubiblock_queue_rq(struct blk_mq_hw_ctx *hctx,
			     const struct blk_mq_queue_data *bd)
{
	struct request *req = bd->rq;
	struct ubiblock *dev = hctx->queue->queuedata;
	struct ubiblock_pdu *pdu = blk_mq_rq_to_pdu(req);

	switch (req_op(req)) {
	case REQ_OP_READ:
		ubi_sgl_init(&pdu->usgl);
		queue_work(dev->wq, &pdu->work);
		return BLK_STS_OK;
	default:
		return BLK_STS_IOERR;
	}

}

static int ubiblock_init_request(struct blk_mq_tag_set *set,
		struct request *req, unsigned int hctx_idx,
		unsigned int numa_node)
{
	struct ubiblock_pdu *pdu = blk_mq_rq_to_pdu(req);

	sg_init_table(pdu->usgl.sg, UBI_MAX_SG_COUNT);
	INIT_WORK(&pdu->work, ubiblock_do_work);

	return 0;
}

static const struct blk_mq_ops ubiblock_mq_ops = {
	.queue_rq       = ubiblock_queue_rq,
	.init_request	= ubiblock_init_request,
};

static int calc_disk_capacity(struct ubi_volume_info *vi, u64 *disk_capacity)
{
	u64 size = vi->used_bytes >> 9;

	if (vi->used_bytes % 512) {
		pr_warn("UBI: block: volume size is not a multiple of 512, "
			"last %llu bytes are ignored!\n",
			vi->used_bytes - (size << 9));
	}

	if ((sector_t)size != size)
		return -EFBIG;

	*disk_capacity = size;

	return 0;
}

int ubiblock_create(struct ubi_volume_info *vi)
{
	struct ubiblock *dev;
	struct gendisk *gd;
	u64 disk_capacity;
	int ret;

	ret = calc_disk_capacity(vi, &disk_capacity);
	if (ret) {
		return ret;
	}

	/* Check that the volume isn't already handled */
	mutex_lock(&devices_mutex);
	if (find_dev_nolock(vi->ubi_num, vi->vol_id)) {
		ret = -EEXIST;
		goto out_unlock;
	}

	dev = kzalloc(sizeof(struct ubiblock), GFP_KERNEL);
	if (!dev) {
		ret = -ENOMEM;
		goto out_unlock;
	}

	mutex_init(&dev->dev_mutex);

	dev->ubi_num = vi->ubi_num;
	dev->vol_id = vi->vol_id;
	dev->leb_size = vi->usable_leb_size;

	/* Initialize the gendisk of this ubiblock device */
	gd = alloc_disk(1);
	if (!gd) {
		pr_err("UBI: block: alloc_disk failed\n");
		ret = -ENODEV;
		goto out_free_dev;
	}

	gd->fops = &ubiblock_ops;
	gd->major = ubiblock_major;
	gd->first_minor = idr_alloc(&ubiblock_minor_idr, dev, 0, 0, GFP_KERNEL);
	if (gd->first_minor < 0) {
		dev_err(disk_to_dev(gd),
			"block: dynamic minor allocation failed");
		ret = -ENODEV;
		goto out_put_disk;
	}
	gd->private_data = dev;
	sprintf(gd->disk_name, "ubiblock%d_%d", dev->ubi_num, dev->vol_id);
	set_capacity(gd, disk_capacity);
	dev->gd = gd;

	dev->tag_set.ops = &ubiblock_mq_ops;
	dev->tag_set.queue_depth = 64;
	dev->tag_set.numa_node = NUMA_NO_NODE;
	dev->tag_set.flags = BLK_MQ_F_SHOULD_MERGE;
	dev->tag_set.cmd_size = sizeof(struct ubiblock_pdu);
	dev->tag_set.driver_data = dev;
	dev->tag_set.nr_hw_queues = 1;

	ret = blk_mq_alloc_tag_set(&dev->tag_set);
	if (ret) {
		dev_err(disk_to_dev(dev->gd), "blk_mq_alloc_tag_set failed");
		goto out_remove_minor;
	}

	dev->rq = blk_mq_init_queue(&dev->tag_set);
	if (IS_ERR(dev->rq)) {
		dev_err(disk_to_dev(gd), "blk_mq_init_queue failed");
		ret = PTR_ERR(dev->rq);
		goto out_free_tags;
	}
	blk_queue_max_segments(dev->rq, UBI_MAX_SG_COUNT);

	dev->rq->queuedata = dev;
	dev->gd->queue = dev->rq;

	/*
	 * Create one workqueue per volume (per registered block device).
	 * Rembember workqueues are cheap, they're not threads.
	 */
	dev->wq = alloc_workqueue("%s", 0, 0, gd->disk_name);
	if (!dev->wq) {
		ret = -ENOMEM;
		goto out_free_queue;
	}

	list_add_tail(&dev->list, &ubiblock_devices);

	/* Must be the last step: anyone can call file ops from now on */
	add_disk(dev->gd);
	dev_info(disk_to_dev(dev->gd), "created from ubi%d:%d(%s)",
		 dev->ubi_num, dev->vol_id, vi->name);
	mutex_unlock(&devices_mutex);
	return 0;

out_free_queue:
	blk_cleanup_queue(dev->rq);
out_free_tags:
	blk_mq_free_tag_set(&dev->tag_set);
out_remove_minor:
	idr_remove(&ubiblock_minor_idr, gd->first_minor);
out_put_disk:
	put_disk(dev->gd);
out_free_dev:
	kfree(dev);
out_unlock:
	mutex_unlock(&devices_mutex);

	return ret;
}

static void ubiblock_cleanup(struct ubiblock *dev)
{
	/* Stop new requests to arrive */
	del_gendisk(dev->gd);
	/* Flush pending work */
	destroy_workqueue(dev->wq);
	/* Finally destroy the blk queue */
	blk_cleanup_queue(dev->rq);
	blk_mq_free_tag_set(&dev->tag_set);
	dev_info(disk_to_dev(dev->gd), "released");
	idr_remove(&ubiblock_minor_idr, dev->gd->first_minor);
	put_disk(dev->gd);
}

int ubiblock_remove(struct ubi_volume_info *vi)
{
	struct ubiblock *dev;
	int ret;

	mutex_lock(&devices_mutex);
	dev = find_dev_nolock(vi->ubi_num, vi->vol_id);
	if (!dev) {
		ret = -ENODEV;
		goto out_unlock;
	}

	/* Found a device, let's lock it so we can check if it's busy */
	mutex_lock(&dev->dev_mutex);
	if (dev->refcnt > 0) {
		ret = -EBUSY;
		goto out_unlock_dev;
	}

	/* Remove from device list */
	list_del(&dev->list);
	ubiblock_cleanup(dev);
	mutex_unlock(&dev->dev_mutex);
	mutex_unlock(&devices_mutex);

	kfree(dev);
	return 0;

out_unlock_dev:
	mutex_unlock(&dev->dev_mutex);
out_unlock:
	mutex_unlock(&devices_mutex);
	return ret;
}

static int ubiblock_resize(struct ubi_volume_info *vi)
{
	struct ubiblock *dev;
	u64 disk_capacity;
	int ret;

	/*
	 * Need to lock the device list until we stop using the device,
	 * otherwise the device struct might get released in
	 * 'ubiblock_remove()'.
	 */
	mutex_lock(&devices_mutex);
	dev = find_dev_nolock(vi->ubi_num, vi->vol_id);
	if (!dev) {
		mutex_unlock(&devices_mutex);
		return -ENODEV;
	}

	ret = calc_disk_capacity(vi, &disk_capacity);
	if (ret) {
		mutex_unlock(&devices_mutex);
		if (ret == -EFBIG) {
			dev_warn(disk_to_dev(dev->gd),
				 "the volume is too big (%d LEBs), cannot resize",
				 vi->size);
		}
		return ret;
	}

	mutex_lock(&dev->dev_mutex);

	if (get_capacity(dev->gd) != disk_capacity) {
		set_capacity(dev->gd, disk_capacity);
		dev_info(disk_to_dev(dev->gd), "resized to %lld bytes",
			 vi->used_bytes);
	}
	mutex_unlock(&dev->dev_mutex);
	mutex_unlock(&devices_mutex);
	return 0;
}

static int ubiblock_notify(struct notifier_block *nb,
			 unsigned long notification_type, void *ns_ptr)
{
	struct ubi_notification *nt = ns_ptr;

	switch (notification_type) {
	case UBI_VOLUME_ADDED:
		/*
		 * We want to enforce explicit block device creation for
		 * volumes, so when a volume is added we do nothing.
		 */
		break;
	case UBI_VOLUME_REMOVED:
		ubiblock_remove(&nt->vi);
		break;
	case UBI_VOLUME_RESIZED:
		ubiblock_resize(&nt->vi);
		break;
	case UBI_VOLUME_UPDATED:
		/*
		 * If the volume is static, a content update might mean the
		 * size (i.e. used_bytes) was also changed.
		 */
		if (nt->vi.vol_type == UBI_STATIC_VOLUME)
			ubiblock_resize(&nt->vi);
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block ubiblock_notifier = {
	.notifier_call = ubiblock_notify,
};

static struct ubi_volume_desc * __init
open_volume_desc(const char *name, int ubi_num, int vol_id)
{
	if (ubi_num == -1)
		/* No ubi num, name must be a vol device path */
		return ubi_open_volume_path(name, UBI_READONLY);
	else if (vol_id == -1)
		/* No vol_id, must be vol_name */
		return ubi_open_volume_nm(ubi_num, name, UBI_READONLY);
	else
		return ubi_open_volume(ubi_num, vol_id, UBI_READONLY);
}

static void __init ubiblock_create_from_param(void)
{
	int i, ret = 0;
	struct ubiblock_param *p;
	struct ubi_volume_desc *desc;
	struct ubi_volume_info vi;

	/*
	 * If there is an error creating one of the ubiblocks, continue on to
	 * create the following ubiblocks. This helps in a circumstance where
	 * the kernel command-line specifies multiple block devices and some
	 * may be broken, but we still want the working ones to come up.
	 */
	for (i = 0; i < ubiblock_devs; i++) {
		p = &ubiblock_param[i];

		desc = open_volume_desc(p->name, p->ubi_num, p->vol_id);
		if (IS_ERR(desc)) {
			pr_err(
			       "UBI: block: can't open volume on ubi%d_%d, err=%ld\n",
			       p->ubi_num, p->vol_id, PTR_ERR(desc));
			continue;
		}

		ubi_get_volume_info(desc, &vi);
		ubi_close_volume(desc);

		ret = ubiblock_create(&vi);
		if (ret) {
			pr_err(
			       "UBI: block: can't add '%s' volume on ubi%d_%d, err=%d\n",
			       vi.name, p->ubi_num, p->vol_id, ret);
			continue;
		}
	}
}

static void ubiblock_remove_all(void)
{
	struct ubiblock *next;
	struct ubiblock *dev;

	mutex_lock(&devices_mutex);
	list_for_each_entry_safe(dev, next, &ubiblock_devices, list) {
		/* The module is being forcefully removed */
		WARN_ON(dev->desc);
		/* Remove from device list */
		list_del(&dev->list);
		ubiblock_cleanup(dev);
		kfree(dev);
	}
	mutex_unlock(&devices_mutex);
}

int __init ubiblock_init(void)
{
	int ret;

	ubiblock_major = register_blkdev(0, "ubiblock");
	if (ubiblock_major < 0)
		return ubiblock_major;

	/*
	 * Attach block devices from 'block=' module param.
	 * Even if one block device in the param list fails to come up,
	 * still allow the module to load and leave any others up.
	 */
	ubiblock_create_from_param();

	/*
	 * Block devices are only created upon user requests, so we ignore
	 * existing volumes.
	 */
	ret = ubi_register_volume_notifier(&ubiblock_notifier, 1);
	if (ret)
		goto err_unreg;
	return 0;

err_unreg:
	unregister_blkdev(ubiblock_major, "ubiblock");
	ubiblock_remove_all();
	return ret;
}

void __exit ubiblock_exit(void)
{
	ubi_unregister_volume_notifier(&ubiblock_notifier);
	ubiblock_remove_all();
	unregister_blkdev(ubiblock_major, "ubiblock");
}
