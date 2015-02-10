/*
 * Copyright (c) 2014 Ezequiel Garcia
 * Copyright (c) 2011 Free Electrons
 *
 * Driver parameter handling strongly based on drivers/mtd/ubi/build.c
 *   Copyright (c) International Business Machines Corp., 2006
 *   Copyright (c) Nokia Corporation, 2007
 *   Authors: Artem Bityutskiy, Frank Haverkamp
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See
 * the GNU General Public License for more details.
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
#include <linux/vmalloc.h>
#include <linux/mtd/ubi.h>
#include <linux/workqueue.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
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
	struct work_struct work;

	struct mutex dev_mutex;
	spinlock_t queue_lock;
	struct list_head list;
};

/* Linked list of all ubiblock instances */
static LIST_HEAD(ubiblock_devices);
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

static struct kernel_param_ops ubiblock_param_ops = {
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

static int ubiblock_read_to_buf(struct ubiblock *dev, char *buffer,
				int leb, int offset, int len)
{
	int ret;

	ret = ubi_read(dev->desc, leb, buffer, offset, len);
	if (ret) {
		dev_err(disk_to_dev(dev->gd), "%d while reading from LEB %d (offset %d, length %d)",
			ret, leb, offset, len);
		return ret;
	}
	return 0;
}

static int ubiblock_read(struct ubiblock *dev, char *buffer,
			 sector_t sec, int len)
{
	int ret, leb, offset;
	int bytes_left = len;
	int to_read = len;
	u64 pos = sec << 9;

	/* Get LEB:offset address to read from */
	offset = do_div(pos, dev->leb_size);
	leb = pos;

	while (bytes_left) {
		/*
		 * We can only read one LEB at a time. Therefore if the read
		 * length is larger than one LEB size, we split the operation.
		 */
		if (offset + to_read > dev->leb_size)
			to_read = dev->leb_size - offset;

		ret = ubiblock_read_to_buf(dev, buffer, leb, offset, to_read);
		if (ret)
			return ret;

		buffer += to_read;
		bytes_left -= to_read;
		to_read = bytes_left;
		leb += 1;
		offset = 0;
	}
	return 0;
}

static int do_ubiblock_request(struct ubiblock *dev, struct request *req)
{
	int len, ret;
	sector_t sec;

	if (req->cmd_type != REQ_TYPE_FS)
		return -EIO;

	if (blk_rq_pos(req) + blk_rq_cur_sectors(req) >
	    get_capacity(req->rq_disk))
		return -EIO;

	if (rq_data_dir(req) != READ)
		return -ENOSYS; /* Write not implemented */

	sec = blk_rq_pos(req);
	len = blk_rq_cur_bytes(req);

	/*
	 * Let's prevent the device from being removed while we're doing I/O
	 * work. Notice that this means we serialize all the I/O operations,
	 * but it's probably of no impact given the NAND core serializes
	 * flash access anyway.
	 */
	mutex_lock(&dev->dev_mutex);
	ret = ubiblock_read(dev, bio_data(req->bio), sec, len);
	mutex_unlock(&dev->dev_mutex);

	return ret;
}

static void ubiblock_do_work(struct work_struct *work)
{
	struct ubiblock *dev =
		container_of(work, struct ubiblock, work);
	struct request_queue *rq = dev->rq;
	struct request *req;
	int res;

	spin_lock_irq(rq->queue_lock);

	req = blk_fetch_request(rq);
	while (req) {

		spin_unlock_irq(rq->queue_lock);
		res = do_ubiblock_request(dev, req);
		spin_lock_irq(rq->queue_lock);

		/*
		 * If we're done with this request,
		 * we need to fetch a new one
		 */
		if (!__blk_end_request_cur(req, res))
			req = blk_fetch_request(rq);
	}

	spin_unlock_irq(rq->queue_lock);
}

static void ubiblock_request(struct request_queue *rq)
{
	struct ubiblock *dev;
	struct request *req;

	dev = rq->queuedata;

	if (!dev)
		while ((req = blk_fetch_request(rq)) != NULL)
			__blk_end_request_all(req, -ENODEV);
	else
		queue_work(dev->wq, &dev->work);
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
		ret = -EPERM;
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

int ubiblock_create(struct ubi_volume_info *vi)
{
	struct ubiblock *dev;
	struct gendisk *gd;
	u64 disk_capacity = vi->used_bytes >> 9;
	int ret;

	if ((sector_t)disk_capacity != disk_capacity)
		return -EFBIG;
	/* Check that the volume isn't already handled */
	mutex_lock(&devices_mutex);
	if (find_dev_nolock(vi->ubi_num, vi->vol_id)) {
		mutex_unlock(&devices_mutex);
		return -EEXIST;
	}
	mutex_unlock(&devices_mutex);

	dev = kzalloc(sizeof(struct ubiblock), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	mutex_init(&dev->dev_mutex);

	dev->ubi_num = vi->ubi_num;
	dev->vol_id = vi->vol_id;
	dev->leb_size = vi->usable_leb_size;

	/* Initialize the gendisk of this ubiblock device */
	gd = alloc_disk(1);
	if (!gd) {
		pr_err("UBI: block: alloc_disk failed");
		ret = -ENODEV;
		goto out_free_dev;
	}

	gd->fops = &ubiblock_ops;
	gd->major = ubiblock_major;
	gd->first_minor = dev->ubi_num * UBI_MAX_VOLUMES + dev->vol_id;
	gd->private_data = dev;
	sprintf(gd->disk_name, "ubiblock%d_%d", dev->ubi_num, dev->vol_id);
	set_capacity(gd, disk_capacity);
	dev->gd = gd;

	spin_lock_init(&dev->queue_lock);
	dev->rq = blk_init_queue(ubiblock_request, &dev->queue_lock);
	if (!dev->rq) {
		dev_err(disk_to_dev(gd), "blk_init_queue failed");
		ret = -ENODEV;
		goto out_put_disk;
	}

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
	INIT_WORK(&dev->work, ubiblock_do_work);

	mutex_lock(&devices_mutex);
	list_add_tail(&dev->list, &ubiblock_devices);
	mutex_unlock(&devices_mutex);

	/* Must be the last step: anyone can call file ops from now on */
	add_disk(dev->gd);
	dev_info(disk_to_dev(dev->gd), "created from ubi%d:%d(%s)",
		 dev->ubi_num, dev->vol_id, vi->name);
	return 0;

out_free_queue:
	blk_cleanup_queue(dev->rq);
out_put_disk:
	put_disk(dev->gd);
out_free_dev:
	kfree(dev);

	return ret;
}

static void ubiblock_cleanup(struct ubiblock *dev)
{
	del_gendisk(dev->gd);
	blk_cleanup_queue(dev->rq);
	dev_info(disk_to_dev(dev->gd), "released");
	put_disk(dev->gd);
}

int ubiblock_remove(struct ubi_volume_info *vi)
{
	struct ubiblock *dev;

	mutex_lock(&devices_mutex);
	dev = find_dev_nolock(vi->ubi_num, vi->vol_id);
	if (!dev) {
		mutex_unlock(&devices_mutex);
		return -ENODEV;
	}

	/* Found a device, let's lock it so we can check if it's busy */
	mutex_lock(&dev->dev_mutex);
	if (dev->refcnt > 0) {
		mutex_unlock(&dev->dev_mutex);
		mutex_unlock(&devices_mutex);
		return -EBUSY;
	}

	/* Remove from device list */
	list_del(&dev->list);
	mutex_unlock(&devices_mutex);

	/* Flush pending work and stop this workqueue */
	destroy_workqueue(dev->wq);

	ubiblock_cleanup(dev);
	mutex_unlock(&dev->dev_mutex);
	kfree(dev);
	return 0;
}

static int ubiblock_resize(struct ubi_volume_info *vi)
{
	struct ubiblock *dev;
	u64 disk_capacity = vi->used_bytes >> 9;

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
	if ((sector_t)disk_capacity != disk_capacity) {
		mutex_unlock(&devices_mutex);
		dev_warn(disk_to_dev(dev->gd), "the volume is too big (%d LEBs), cannot resize",
			 vi->size);
		return -EFBIG;
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

static int __init ubiblock_create_from_param(void)
{
	int i, ret;
	struct ubiblock_param *p;
	struct ubi_volume_desc *desc;
	struct ubi_volume_info vi;

	for (i = 0; i < ubiblock_devs; i++) {
		p = &ubiblock_param[i];

		desc = open_volume_desc(p->name, p->ubi_num, p->vol_id);
		if (IS_ERR(desc)) {
			pr_err("UBI: block: can't open volume, err=%ld\n",
			       PTR_ERR(desc));
			ret = PTR_ERR(desc);
			break;
		}

		ubi_get_volume_info(desc, &vi);
		ubi_close_volume(desc);

		ret = ubiblock_create(&vi);
		if (ret) {
			pr_err("UBI: block: can't add '%s' volume, err=%d\n",
			       vi.name, ret);
			break;
		}
	}
	return ret;
}

static void ubiblock_remove_all(void)
{
	struct ubiblock *next;
	struct ubiblock *dev;

	list_for_each_entry_safe(dev, next, &ubiblock_devices, list) {
		/* Flush pending work and stop workqueue */
		destroy_workqueue(dev->wq);
		/* The module is being forcefully removed */
		WARN_ON(dev->desc);
		/* Remove from device list */
		list_del(&dev->list);
		ubiblock_cleanup(dev);
		kfree(dev);
	}
}

int __init ubiblock_init(void)
{
	int ret;

	ubiblock_major = register_blkdev(0, "ubiblock");
	if (ubiblock_major < 0)
		return ubiblock_major;

	/* Attach block devices from 'block=' module param */
	ret = ubiblock_create_from_param();
	if (ret)
		goto err_remove;

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
err_remove:
	ubiblock_remove_all();
	return ret;
}

void __exit ubiblock_exit(void)
{
	ubi_unregister_volume_notifier(&ubiblock_notifier);
	ubiblock_remove_all();
	unregister_blkdev(ubiblock_major, "ubiblock");
}
