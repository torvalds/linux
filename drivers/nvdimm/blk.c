/*
 * NVDIMM Block Window Driver
 * Copyright (c) 2014, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/blkdev.h>
#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/nd.h>
#include <linux/sizes.h>
#include "nd.h"

struct nd_blk_device {
	struct request_queue *queue;
	struct gendisk *disk;
	struct nd_namespace_blk *nsblk;
	struct nd_blk_region *ndbr;
	size_t disk_size;
};

static int nd_blk_major;

static resource_size_t to_dev_offset(struct nd_namespace_blk *nsblk,
				resource_size_t ns_offset, unsigned int len)
{
	int i;

	for (i = 0; i < nsblk->num_resources; i++) {
		if (ns_offset < resource_size(nsblk->res[i])) {
			if (ns_offset + len > resource_size(nsblk->res[i])) {
				dev_WARN_ONCE(&nsblk->common.dev, 1,
					"illegal request\n");
				return SIZE_MAX;
			}
			return nsblk->res[i]->start + ns_offset;
		}
		ns_offset -= resource_size(nsblk->res[i]);
	}

	dev_WARN_ONCE(&nsblk->common.dev, 1, "request out of range\n");
	return SIZE_MAX;
}

static void nd_blk_make_request(struct request_queue *q, struct bio *bio)
{
	struct block_device *bdev = bio->bi_bdev;
	struct gendisk *disk = bdev->bd_disk;
	struct nd_namespace_blk *nsblk;
	struct nd_blk_device *blk_dev;
	struct nd_blk_region *ndbr;
	struct bvec_iter iter;
	struct bio_vec bvec;
	int err = 0, rw;

	blk_dev = disk->private_data;
	nsblk = blk_dev->nsblk;
	ndbr = blk_dev->ndbr;
	rw = bio_data_dir(bio);
	bio_for_each_segment(bvec, bio, iter) {
		unsigned int len = bvec.bv_len;
		resource_size_t	dev_offset;
		void *iobuf;

		BUG_ON(len > PAGE_SIZE);

		dev_offset = to_dev_offset(nsblk,
				iter.bi_sector << SECTOR_SHIFT, len);
		if (dev_offset == SIZE_MAX) {
			err = -EIO;
			goto out;
		}

		iobuf = kmap_atomic(bvec.bv_page);
		err = ndbr->do_io(ndbr, dev_offset, iobuf + bvec.bv_offset,
				len, rw);
		kunmap_atomic(iobuf);
		if (err)
			goto out;
	}

 out:
	bio_endio(bio, err);
}

static int nd_blk_rw_bytes(struct nd_namespace_common *ndns,
		resource_size_t offset, void *iobuf, size_t n, int rw)
{
	struct nd_blk_device *blk_dev = dev_get_drvdata(ndns->claim);
	struct nd_namespace_blk *nsblk = blk_dev->nsblk;
	struct nd_blk_region *ndbr = blk_dev->ndbr;
	resource_size_t	dev_offset;

	dev_offset = to_dev_offset(nsblk, offset, n);

	if (unlikely(offset + n > blk_dev->disk_size)) {
		dev_WARN_ONCE(&ndns->dev, 1, "request out of range\n");
		return -EFAULT;
	}

	if (dev_offset == SIZE_MAX)
		return -EIO;

	return ndbr->do_io(ndbr, dev_offset, iobuf, n, rw);
}

static const struct block_device_operations nd_blk_fops = {
	.owner = THIS_MODULE,
};

static int nd_blk_attach_disk(struct nd_namespace_common *ndns,
		struct nd_blk_device *blk_dev)
{
	struct nd_namespace_blk *nsblk = to_nd_namespace_blk(&ndns->dev);
	struct gendisk *disk;

	blk_dev->queue = blk_alloc_queue(GFP_KERNEL);
	if (!blk_dev->queue)
		return -ENOMEM;

	blk_queue_make_request(blk_dev->queue, nd_blk_make_request);
	blk_queue_max_hw_sectors(blk_dev->queue, UINT_MAX);
	blk_queue_bounce_limit(blk_dev->queue, BLK_BOUNCE_ANY);
	blk_queue_logical_block_size(blk_dev->queue, nsblk->lbasize);
	queue_flag_set_unlocked(QUEUE_FLAG_NONROT, blk_dev->queue);

	disk = blk_dev->disk = alloc_disk(0);
	if (!disk) {
		blk_cleanup_queue(blk_dev->queue);
		return -ENOMEM;
	}

	disk->driverfs_dev	= &ndns->dev;
	disk->major		= nd_blk_major;
	disk->first_minor	= 0;
	disk->fops		= &nd_blk_fops;
	disk->private_data	= blk_dev;
	disk->queue		= blk_dev->queue;
	disk->flags		= GENHD_FL_EXT_DEVT;
	nvdimm_namespace_disk_name(ndns, disk->disk_name);
	set_capacity(disk, blk_dev->disk_size >> SECTOR_SHIFT);
	add_disk(disk);

	return 0;
}

static int nd_blk_probe(struct device *dev)
{
	struct nd_namespace_common *ndns;
	struct nd_blk_device *blk_dev;
	int rc;

	ndns = nvdimm_namespace_common_probe(dev);
	if (IS_ERR(ndns))
		return PTR_ERR(ndns);

	blk_dev = kzalloc(sizeof(*blk_dev), GFP_KERNEL);
	if (!blk_dev)
		return -ENOMEM;

	blk_dev->disk_size = nvdimm_namespace_capacity(ndns);
	blk_dev->ndbr = to_nd_blk_region(dev->parent);
	blk_dev->nsblk = to_nd_namespace_blk(&ndns->dev);
	dev_set_drvdata(dev, blk_dev);

	ndns->rw_bytes = nd_blk_rw_bytes;
	if (is_nd_btt(dev))
		rc = nvdimm_namespace_attach_btt(ndns);
	else if (nd_btt_probe(ndns, blk_dev) == 0) {
		/* we'll come back as btt-blk */
		rc = -ENXIO;
	} else
		rc = nd_blk_attach_disk(ndns, blk_dev);
	if (rc)
		kfree(blk_dev);
	return rc;
}

static void nd_blk_detach_disk(struct nd_blk_device *blk_dev)
{
	del_gendisk(blk_dev->disk);
	put_disk(blk_dev->disk);
	blk_cleanup_queue(blk_dev->queue);
}

static int nd_blk_remove(struct device *dev)
{
	struct nd_blk_device *blk_dev = dev_get_drvdata(dev);

	if (is_nd_btt(dev))
		nvdimm_namespace_detach_btt(to_nd_btt(dev)->ndns);
	else
		nd_blk_detach_disk(blk_dev);
	kfree(blk_dev);

	return 0;
}

static struct nd_device_driver nd_blk_driver = {
	.probe = nd_blk_probe,
	.remove = nd_blk_remove,
	.drv = {
		.name = "nd_blk",
	},
	.type = ND_DRIVER_NAMESPACE_BLK,
};

static int __init nd_blk_init(void)
{
	int rc;

	rc = register_blkdev(0, "nd_blk");
	if (rc < 0)
		return rc;

	nd_blk_major = rc;
	rc = nd_driver_register(&nd_blk_driver);

	if (rc < 0)
		unregister_blkdev(nd_blk_major, "nd_blk");

	return rc;
}

static void __exit nd_blk_exit(void)
{
	driver_unregister(&nd_blk_driver.drv);
	unregister_blkdev(nd_blk_major, "nd_blk");
}

MODULE_AUTHOR("Ross Zwisler <ross.zwisler@linux.intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS_ND_DEVICE(ND_DEVICE_NAMESPACE_BLK);
module_init(nd_blk_init);
module_exit(nd_blk_exit);
