/*
 * Persistent Memory Driver
 *
 * Copyright (c) 2014-2015, Intel Corporation.
 * Copyright (c) 2015, Christoph Hellwig <hch@lst.de>.
 * Copyright (c) 2015, Boaz Harrosh <boaz@plexistor.com>.
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

#include <asm/cacheflush.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/pmem.h>
#include <linux/nd.h>
#include "nd.h"

struct pmem_device {
	struct request_queue	*pmem_queue;
	struct gendisk		*pmem_disk;

	/* One contiguous memory region per device */
	phys_addr_t		phys_addr;
	void __pmem		*virt_addr;
	size_t			size;
};

static int pmem_major;

static void pmem_do_bvec(struct pmem_device *pmem, struct page *page,
			unsigned int len, unsigned int off, int rw,
			sector_t sector)
{
	void *mem = kmap_atomic(page);
	size_t pmem_off = sector << 9;
	void __pmem *pmem_addr = pmem->virt_addr + pmem_off;

	if (rw == READ) {
		memcpy_from_pmem(mem + off, pmem_addr, len);
		flush_dcache_page(page);
	} else {
		flush_dcache_page(page);
		memcpy_to_pmem(pmem_addr, mem + off, len);
	}

	kunmap_atomic(mem);
}

static void pmem_make_request(struct request_queue *q, struct bio *bio)
{
	bool do_acct;
	unsigned long start;
	struct bio_vec bvec;
	struct bvec_iter iter;
	struct block_device *bdev = bio->bi_bdev;
	struct pmem_device *pmem = bdev->bd_disk->private_data;

	do_acct = nd_iostat_start(bio, &start);
	bio_for_each_segment(bvec, bio, iter)
		pmem_do_bvec(pmem, bvec.bv_page, bvec.bv_len, bvec.bv_offset,
				bio_data_dir(bio), iter.bi_sector);
	if (do_acct)
		nd_iostat_end(bio, start);

	if (bio_data_dir(bio))
		wmb_pmem();

	bio_endio(bio, 0);
}

static int pmem_rw_page(struct block_device *bdev, sector_t sector,
		       struct page *page, int rw)
{
	struct pmem_device *pmem = bdev->bd_disk->private_data;

	pmem_do_bvec(pmem, page, PAGE_CACHE_SIZE, 0, rw, sector);
	page_endio(page, rw & WRITE, 0);

	return 0;
}

static long pmem_direct_access(struct block_device *bdev, sector_t sector,
			      void **kaddr, unsigned long *pfn, long size)
{
	struct pmem_device *pmem = bdev->bd_disk->private_data;
	size_t offset = sector << 9;

	if (!pmem)
		return -ENODEV;

	/* FIXME convert DAX to comprehend that this mapping has a lifetime */
	*kaddr = (void __force *) pmem->virt_addr + offset;
	*pfn = (pmem->phys_addr + offset) >> PAGE_SHIFT;

	return pmem->size - offset;
}

static const struct block_device_operations pmem_fops = {
	.owner =		THIS_MODULE,
	.rw_page =		pmem_rw_page,
	.direct_access =	pmem_direct_access,
	.revalidate_disk =	nvdimm_revalidate_disk,
};

static struct pmem_device *pmem_alloc(struct device *dev,
		struct resource *res, int id)
{
	struct pmem_device *pmem;

	pmem = kzalloc(sizeof(*pmem), GFP_KERNEL);
	if (!pmem)
		return ERR_PTR(-ENOMEM);

	pmem->phys_addr = res->start;
	pmem->size = resource_size(res);
	if (!arch_has_pmem_api())
		dev_warn(dev, "unable to guarantee persistence of writes\n");

	if (!request_mem_region(pmem->phys_addr, pmem->size, dev_name(dev))) {
		dev_warn(dev, "could not reserve region [0x%pa:0x%zx]\n",
				&pmem->phys_addr, pmem->size);
		kfree(pmem);
		return ERR_PTR(-EBUSY);
	}

	pmem->virt_addr = memremap_pmem(pmem->phys_addr, pmem->size);
	if (!pmem->virt_addr) {
		release_mem_region(pmem->phys_addr, pmem->size);
		kfree(pmem);
		return ERR_PTR(-ENXIO);
	}

	return pmem;
}

static void pmem_detach_disk(struct pmem_device *pmem)
{
	del_gendisk(pmem->pmem_disk);
	put_disk(pmem->pmem_disk);
	blk_cleanup_queue(pmem->pmem_queue);
}

static int pmem_attach_disk(struct nd_namespace_common *ndns,
		struct pmem_device *pmem)
{
	struct gendisk *disk;

	pmem->pmem_queue = blk_alloc_queue(GFP_KERNEL);
	if (!pmem->pmem_queue)
		return -ENOMEM;

	blk_queue_make_request(pmem->pmem_queue, pmem_make_request);
	blk_queue_max_hw_sectors(pmem->pmem_queue, UINT_MAX);
	blk_queue_bounce_limit(pmem->pmem_queue, BLK_BOUNCE_ANY);
	queue_flag_set_unlocked(QUEUE_FLAG_NONROT, pmem->pmem_queue);

	disk = alloc_disk(0);
	if (!disk) {
		blk_cleanup_queue(pmem->pmem_queue);
		return -ENOMEM;
	}

	disk->major		= pmem_major;
	disk->first_minor	= 0;
	disk->fops		= &pmem_fops;
	disk->private_data	= pmem;
	disk->queue		= pmem->pmem_queue;
	disk->flags		= GENHD_FL_EXT_DEVT;
	nvdimm_namespace_disk_name(ndns, disk->disk_name);
	disk->driverfs_dev = &ndns->dev;
	set_capacity(disk, pmem->size >> 9);
	pmem->pmem_disk = disk;

	add_disk(disk);
	revalidate_disk(disk);

	return 0;
}

static int pmem_rw_bytes(struct nd_namespace_common *ndns,
		resource_size_t offset, void *buf, size_t size, int rw)
{
	struct pmem_device *pmem = dev_get_drvdata(ndns->claim);

	if (unlikely(offset + size > pmem->size)) {
		dev_WARN_ONCE(&ndns->dev, 1, "request out of range\n");
		return -EFAULT;
	}

	if (rw == READ)
		memcpy_from_pmem(buf, pmem->virt_addr + offset, size);
	else {
		memcpy_to_pmem(pmem->virt_addr + offset, buf, size);
		wmb_pmem();
	}

	return 0;
}

static void pmem_free(struct pmem_device *pmem)
{
	memunmap_pmem(pmem->virt_addr);
	release_mem_region(pmem->phys_addr, pmem->size);
	kfree(pmem);
}

static int nd_pmem_probe(struct device *dev)
{
	struct nd_region *nd_region = to_nd_region(dev->parent);
	struct nd_namespace_common *ndns;
	struct nd_namespace_io *nsio;
	struct pmem_device *pmem;
	int rc;

	ndns = nvdimm_namespace_common_probe(dev);
	if (IS_ERR(ndns))
		return PTR_ERR(ndns);

	nsio = to_nd_namespace_io(&ndns->dev);
	pmem = pmem_alloc(dev, &nsio->res, nd_region->id);
	if (IS_ERR(pmem))
		return PTR_ERR(pmem);

	dev_set_drvdata(dev, pmem);
	ndns->rw_bytes = pmem_rw_bytes;
	if (is_nd_btt(dev))
		rc = nvdimm_namespace_attach_btt(ndns);
	else if (nd_btt_probe(ndns, pmem) == 0) {
		/* we'll come back as btt-pmem */
		rc = -ENXIO;
	} else
		rc = pmem_attach_disk(ndns, pmem);
	if (rc)
		pmem_free(pmem);
	return rc;
}

static int nd_pmem_remove(struct device *dev)
{
	struct pmem_device *pmem = dev_get_drvdata(dev);

	if (is_nd_btt(dev))
		nvdimm_namespace_detach_btt(to_nd_btt(dev)->ndns);
	else
		pmem_detach_disk(pmem);
	pmem_free(pmem);

	return 0;
}

MODULE_ALIAS("pmem");
MODULE_ALIAS_ND_DEVICE(ND_DEVICE_NAMESPACE_IO);
MODULE_ALIAS_ND_DEVICE(ND_DEVICE_NAMESPACE_PMEM);
static struct nd_device_driver nd_pmem_driver = {
	.probe = nd_pmem_probe,
	.remove = nd_pmem_remove,
	.drv = {
		.name = "nd_pmem",
	},
	.type = ND_DRIVER_NAMESPACE_IO | ND_DRIVER_NAMESPACE_PMEM,
};

static int __init pmem_init(void)
{
	int error;

	pmem_major = register_blkdev(0, "pmem");
	if (pmem_major < 0)
		return pmem_major;

	error = nd_driver_register(&nd_pmem_driver);
	if (error) {
		unregister_blkdev(pmem_major, "pmem");
		return error;
	}

	return 0;
}
module_init(pmem_init);

static void pmem_exit(void)
{
	driver_unregister(&nd_pmem_driver.drv);
	unregister_blkdev(pmem_major, "pmem");
}
module_exit(pmem_exit);

MODULE_AUTHOR("Ross Zwisler <ross.zwisler@linux.intel.com>");
MODULE_LICENSE("GPL v2");
