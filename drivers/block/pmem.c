/*
 * Persistent Memory Driver
 *
 * Copyright (c) 2014, Intel Corporation.
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

#define PMEM_MINORS		16

struct pmem_device {
	struct request_queue	*pmem_queue;
	struct gendisk		*pmem_disk;

	/* One contiguous memory region per device */
	phys_addr_t		phys_addr;
	void			*virt_addr;
	size_t			size;
};

static int pmem_major;
static atomic_t pmem_index;

static void pmem_do_bvec(struct pmem_device *pmem, struct page *page,
			unsigned int len, unsigned int off, int rw,
			sector_t sector)
{
	void *mem = kmap_atomic(page);
	size_t pmem_off = sector << 9;

	if (rw == READ) {
		memcpy(mem + off, pmem->virt_addr + pmem_off, len);
		flush_dcache_page(page);
	} else {
		flush_dcache_page(page);
		memcpy(pmem->virt_addr + pmem_off, mem + off, len);
	}

	kunmap_atomic(mem);
}

static void pmem_make_request(struct request_queue *q, struct bio *bio)
{
	struct block_device *bdev = bio->bi_bdev;
	struct pmem_device *pmem = bdev->bd_disk->private_data;
	int rw;
	struct bio_vec bvec;
	sector_t sector;
	struct bvec_iter iter;
	int err = 0;

	if (bio_end_sector(bio) > get_capacity(bdev->bd_disk)) {
		err = -EIO;
		goto out;
	}

	BUG_ON(bio->bi_rw & REQ_DISCARD);

	rw = bio_data_dir(bio);
	sector = bio->bi_iter.bi_sector;
	bio_for_each_segment(bvec, bio, iter) {
		pmem_do_bvec(pmem, bvec.bv_page, bvec.bv_len, bvec.bv_offset,
			     rw, sector);
		sector += bvec.bv_len >> 9;
	}

out:
	bio_endio(bio, err);
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

	*kaddr = pmem->virt_addr + offset;
	*pfn = (pmem->phys_addr + offset) >> PAGE_SHIFT;

	return pmem->size - offset;
}

static const struct block_device_operations pmem_fops = {
	.owner =		THIS_MODULE,
	.rw_page =		pmem_rw_page,
	.direct_access =	pmem_direct_access,
};

static struct pmem_device *pmem_alloc(struct device *dev, struct resource *res)
{
	struct pmem_device *pmem;
	struct gendisk *disk;
	int idx, err;

	err = -ENOMEM;
	pmem = kzalloc(sizeof(*pmem), GFP_KERNEL);
	if (!pmem)
		goto out;

	pmem->phys_addr = res->start;
	pmem->size = resource_size(res);

	err = -EINVAL;
	if (!request_mem_region(pmem->phys_addr, pmem->size, "pmem")) {
		dev_warn(dev, "could not reserve region [0x%pa:0x%zx]\n", &pmem->phys_addr, pmem->size);
		goto out_free_dev;
	}

	/*
	 * Map the memory as non-cachable, as we can't write back the contents
	 * of the CPU caches in case of a crash.
	 */
	err = -ENOMEM;
	pmem->virt_addr = ioremap_nocache(pmem->phys_addr, pmem->size);
	if (!pmem->virt_addr)
		goto out_release_region;

	pmem->pmem_queue = blk_alloc_queue(GFP_KERNEL);
	if (!pmem->pmem_queue)
		goto out_unmap;

	blk_queue_make_request(pmem->pmem_queue, pmem_make_request);
	blk_queue_max_hw_sectors(pmem->pmem_queue, 1024);
	blk_queue_bounce_limit(pmem->pmem_queue, BLK_BOUNCE_ANY);

	disk = alloc_disk(PMEM_MINORS);
	if (!disk)
		goto out_free_queue;

	idx = atomic_inc_return(&pmem_index) - 1;

	disk->major		= pmem_major;
	disk->first_minor	= PMEM_MINORS * idx;
	disk->fops		= &pmem_fops;
	disk->private_data	= pmem;
	disk->queue		= pmem->pmem_queue;
	disk->flags		= GENHD_FL_EXT_DEVT;
	sprintf(disk->disk_name, "pmem%d", idx);
	disk->driverfs_dev = dev;
	set_capacity(disk, pmem->size >> 9);
	pmem->pmem_disk = disk;

	add_disk(disk);

	return pmem;

out_free_queue:
	blk_cleanup_queue(pmem->pmem_queue);
out_unmap:
	iounmap(pmem->virt_addr);
out_release_region:
	release_mem_region(pmem->phys_addr, pmem->size);
out_free_dev:
	kfree(pmem);
out:
	return ERR_PTR(err);
}

static void pmem_free(struct pmem_device *pmem)
{
	del_gendisk(pmem->pmem_disk);
	put_disk(pmem->pmem_disk);
	blk_cleanup_queue(pmem->pmem_queue);
	iounmap(pmem->virt_addr);
	release_mem_region(pmem->phys_addr, pmem->size);
	kfree(pmem);
}

static int pmem_probe(struct platform_device *pdev)
{
	struct pmem_device *pmem;
	struct resource *res;

	if (WARN_ON(pdev->num_resources > 1))
		return -ENXIO;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENXIO;

	pmem = pmem_alloc(&pdev->dev, res);
	if (IS_ERR(pmem))
		return PTR_ERR(pmem);

	platform_set_drvdata(pdev, pmem);

	return 0;
}

static int pmem_remove(struct platform_device *pdev)
{
	struct pmem_device *pmem = platform_get_drvdata(pdev);

	pmem_free(pmem);
	return 0;
}

static struct platform_driver pmem_driver = {
	.probe		= pmem_probe,
	.remove		= pmem_remove,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "pmem",
	},
};

static int __init pmem_init(void)
{
	int error;

	pmem_major = register_blkdev(0, "pmem");
	if (pmem_major < 0)
		return pmem_major;

	error = platform_driver_register(&pmem_driver);
	if (error)
		unregister_blkdev(pmem_major, "pmem");
	return error;
}
module_init(pmem_init);

static void pmem_exit(void)
{
	platform_driver_unregister(&pmem_driver);
	unregister_blkdev(pmem_major, "pmem");
}
module_exit(pmem_exit);

MODULE_AUTHOR("Ross Zwisler <ross.zwisler@linux.intel.com>");
MODULE_LICENSE("GPL v2");
