// SPDX-License-Identifier: GPL-2.0-only
/*
 * NVDIMM Block Window Driver
 * Copyright (c) 2014, Intel Corporation.
 */

#include <linux/blkdev.h>
#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/nd.h>
#include <linux/sizes.h>
#include "nd.h"

static u32 nsblk_meta_size(struct nd_namespace_blk *nsblk)
{
	return nsblk->lbasize - ((nsblk->lbasize >= 4096) ? 4096 : 512);
}

static u32 nsblk_internal_lbasize(struct nd_namespace_blk *nsblk)
{
	return roundup(nsblk->lbasize, INT_LBASIZE_ALIGNMENT);
}

static u32 nsblk_sector_size(struct nd_namespace_blk *nsblk)
{
	return nsblk->lbasize - nsblk_meta_size(nsblk);
}

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

static struct nd_blk_region *to_ndbr(struct nd_namespace_blk *nsblk)
{
	struct nd_region *nd_region;
	struct device *parent;

	parent = nsblk->common.dev.parent;
	nd_region = container_of(parent, struct nd_region, dev);
	return container_of(nd_region, struct nd_blk_region, nd_region);
}

#ifdef CONFIG_BLK_DEV_INTEGRITY
static int nd_blk_rw_integrity(struct nd_namespace_blk *nsblk,
		struct bio_integrity_payload *bip, u64 lba, int rw)
{
	struct nd_blk_region *ndbr = to_ndbr(nsblk);
	unsigned int len = nsblk_meta_size(nsblk);
	resource_size_t	dev_offset, ns_offset;
	u32 internal_lbasize, sector_size;
	int err = 0;

	internal_lbasize = nsblk_internal_lbasize(nsblk);
	sector_size = nsblk_sector_size(nsblk);
	ns_offset = lba * internal_lbasize + sector_size;
	dev_offset = to_dev_offset(nsblk, ns_offset, len);
	if (dev_offset == SIZE_MAX)
		return -EIO;

	while (len) {
		unsigned int cur_len;
		struct bio_vec bv;
		void *iobuf;

		bv = bvec_iter_bvec(bip->bip_vec, bip->bip_iter);
		/*
		 * The 'bv' obtained from bvec_iter_bvec has its .bv_len and
		 * .bv_offset already adjusted for iter->bi_bvec_done, and we
		 * can use those directly
		 */

		cur_len = min(len, bv.bv_len);
		iobuf = kmap_atomic(bv.bv_page);
		err = ndbr->do_io(ndbr, dev_offset, iobuf + bv.bv_offset,
				cur_len, rw);
		kunmap_atomic(iobuf);
		if (err)
			return err;

		len -= cur_len;
		dev_offset += cur_len;
		if (!bvec_iter_advance(bip->bip_vec, &bip->bip_iter, cur_len))
			return -EIO;
	}

	return err;
}

#else /* CONFIG_BLK_DEV_INTEGRITY */
static int nd_blk_rw_integrity(struct nd_namespace_blk *nsblk,
		struct bio_integrity_payload *bip, u64 lba, int rw)
{
	return 0;
}
#endif

static int nsblk_do_bvec(struct nd_namespace_blk *nsblk,
		struct bio_integrity_payload *bip, struct page *page,
		unsigned int len, unsigned int off, int rw, sector_t sector)
{
	struct nd_blk_region *ndbr = to_ndbr(nsblk);
	resource_size_t	dev_offset, ns_offset;
	u32 internal_lbasize, sector_size;
	int err = 0;
	void *iobuf;
	u64 lba;

	internal_lbasize = nsblk_internal_lbasize(nsblk);
	sector_size = nsblk_sector_size(nsblk);
	while (len) {
		unsigned int cur_len;

		/*
		 * If we don't have an integrity payload, we don't have to
		 * split the bvec into sectors, as this would cause unnecessary
		 * Block Window setup/move steps. the do_io routine is capable
		 * of handling len <= PAGE_SIZE.
		 */
		cur_len = bip ? min(len, sector_size) : len;

		lba = div_u64(sector << SECTOR_SHIFT, sector_size);
		ns_offset = lba * internal_lbasize;
		dev_offset = to_dev_offset(nsblk, ns_offset, cur_len);
		if (dev_offset == SIZE_MAX)
			return -EIO;

		iobuf = kmap_atomic(page);
		err = ndbr->do_io(ndbr, dev_offset, iobuf + off, cur_len, rw);
		kunmap_atomic(iobuf);
		if (err)
			return err;

		if (bip) {
			err = nd_blk_rw_integrity(nsblk, bip, lba, rw);
			if (err)
				return err;
		}
		len -= cur_len;
		off += cur_len;
		sector += sector_size >> SECTOR_SHIFT;
	}

	return err;
}

static void nd_blk_submit_bio(struct bio *bio)
{
	struct bio_integrity_payload *bip;
	struct nd_namespace_blk *nsblk = bio->bi_bdev->bd_disk->private_data;
	struct bvec_iter iter;
	unsigned long start;
	struct bio_vec bvec;
	int err = 0, rw;
	bool do_acct;

	if (!bio_integrity_prep(bio))
		return;

	bip = bio_integrity(bio);
	rw = bio_data_dir(bio);
	do_acct = blk_queue_io_stat(bio->bi_bdev->bd_disk->queue);
	if (do_acct)
		start = bio_start_io_acct(bio);
	bio_for_each_segment(bvec, bio, iter) {
		unsigned int len = bvec.bv_len;

		BUG_ON(len > PAGE_SIZE);
		err = nsblk_do_bvec(nsblk, bip, bvec.bv_page, len,
				bvec.bv_offset, rw, iter.bi_sector);
		if (err) {
			dev_dbg(&nsblk->common.dev,
					"io error in %s sector %lld, len %d,\n",
					(rw == READ) ? "READ" : "WRITE",
					(unsigned long long) iter.bi_sector, len);
			bio->bi_status = errno_to_blk_status(err);
			break;
		}
	}
	if (do_acct)
		bio_end_io_acct(bio, start);

	bio_endio(bio);
}

static int nsblk_rw_bytes(struct nd_namespace_common *ndns,
		resource_size_t offset, void *iobuf, size_t n, int rw,
		unsigned long flags)
{
	struct nd_namespace_blk *nsblk = to_nd_namespace_blk(&ndns->dev);
	struct nd_blk_region *ndbr = to_ndbr(nsblk);
	resource_size_t	dev_offset;

	dev_offset = to_dev_offset(nsblk, offset, n);

	if (unlikely(offset + n > nsblk->size)) {
		dev_WARN_ONCE(&ndns->dev, 1, "request out of range\n");
		return -EFAULT;
	}

	if (dev_offset == SIZE_MAX)
		return -EIO;

	return ndbr->do_io(ndbr, dev_offset, iobuf, n, rw);
}

static const struct block_device_operations nd_blk_fops = {
	.owner = THIS_MODULE,
	.submit_bio =  nd_blk_submit_bio,
};

static void nd_blk_release_disk(void *disk)
{
	del_gendisk(disk);
	blk_cleanup_disk(disk);
}

static int nsblk_attach_disk(struct nd_namespace_blk *nsblk)
{
	struct device *dev = &nsblk->common.dev;
	resource_size_t available_disk_size;
	struct gendisk *disk;
	u64 internal_nlba;
	int rc;

	internal_nlba = div_u64(nsblk->size, nsblk_internal_lbasize(nsblk));
	available_disk_size = internal_nlba * nsblk_sector_size(nsblk);

	disk = blk_alloc_disk(NUMA_NO_NODE);
	if (!disk)
		return -ENOMEM;

	disk->fops		= &nd_blk_fops;
	disk->private_data	= nsblk;
	nvdimm_namespace_disk_name(&nsblk->common, disk->disk_name);

	blk_queue_max_hw_sectors(disk->queue, UINT_MAX);
	blk_queue_logical_block_size(disk->queue, nsblk_sector_size(nsblk));
	blk_queue_flag_set(QUEUE_FLAG_NONROT, disk->queue);

	if (nsblk_meta_size(nsblk)) {
		rc = nd_integrity_init(disk, nsblk_meta_size(nsblk));

		if (rc)
			goto out_before_devm_err;
	}

	set_capacity(disk, available_disk_size >> SECTOR_SHIFT);
	rc = device_add_disk(dev, disk, NULL);
	if (rc)
		goto out_before_devm_err;

	/* nd_blk_release_disk() is called if this fails */
	if (devm_add_action_or_reset(dev, nd_blk_release_disk, disk))
		return -ENOMEM;

	nvdimm_check_and_set_ro(disk);
	return 0;

out_before_devm_err:
	blk_cleanup_disk(disk);
	return rc;
}

static int nd_blk_probe(struct device *dev)
{
	struct nd_namespace_common *ndns;
	struct nd_namespace_blk *nsblk;

	ndns = nvdimm_namespace_common_probe(dev);
	if (IS_ERR(ndns))
		return PTR_ERR(ndns);

	nsblk = to_nd_namespace_blk(&ndns->dev);
	nsblk->size = nvdimm_namespace_capacity(ndns);
	dev_set_drvdata(dev, nsblk);

	ndns->rw_bytes = nsblk_rw_bytes;
	if (is_nd_btt(dev))
		return nvdimm_namespace_attach_btt(ndns);
	else if (nd_btt_probe(dev, ndns) == 0) {
		/* we'll come back as btt-blk */
		return -ENXIO;
	} else
		return nsblk_attach_disk(nsblk);
}

static void nd_blk_remove(struct device *dev)
{
	if (is_nd_btt(dev))
		nvdimm_namespace_detach_btt(to_nd_btt(dev));
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
	return nd_driver_register(&nd_blk_driver);
}

static void __exit nd_blk_exit(void)
{
	driver_unregister(&nd_blk_driver.drv);
}

MODULE_AUTHOR("Ross Zwisler <ross.zwisler@linux.intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS_ND_DEVICE(ND_DEVICE_NAMESPACE_BLK);
module_init(nd_blk_init);
module_exit(nd_blk_exit);
