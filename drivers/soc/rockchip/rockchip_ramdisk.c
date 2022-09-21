// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Rockchip Electronics Co., Ltd
 *
 * Parts derived from drivers/block/brd.c, copyright
 * of their respective owners.
 */

#include <linux/backing-dev.h>
#include <linux/dax.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/pfn_t.h>
#include <linux/platform_device.h>
#include <linux/uio.h>

#define PAGE_SECTORS_SHIFT	(PAGE_SHIFT - SECTOR_SHIFT)
#define PAGE_SECTORS		(1 << PAGE_SECTORS_SHIFT)

struct rd_device {
	struct request_queue	*rd_queue;
	struct gendisk		*rd_disk;

	struct device		*dev;
	phys_addr_t		mem_addr;
	size_t			mem_size;
	size_t			mem_pages;
	void			*mem_kaddr;
	struct dax_device	*dax_dev;
};

static int rd_major;

/*
 * Look up and return a rd's page for a given sector.
 */
static struct page *rd_lookup_page(struct rd_device *rd, sector_t sector)
{
	pgoff_t idx;
	struct page *page;

	idx = sector >> PAGE_SECTORS_SHIFT; /* sector to page index */
	page = phys_to_page(rd->mem_addr + (idx << PAGE_SHIFT));
	BUG_ON(!page);

	return page;
}

/*
 * Copy n bytes from src to the rd starting at sector. Does not sleep.
 */
static void copy_to_rd(struct rd_device *rd, const void *src,
		       sector_t sector, size_t n)
{
	struct page *page;
	void *dst;
	unsigned int offset = (sector & (PAGE_SECTORS - 1)) << SECTOR_SHIFT;
	size_t copy;

	copy = min_t(size_t, n, PAGE_SIZE - offset);
	page = rd_lookup_page(rd, sector);
	BUG_ON(!page);

	dst = kmap_atomic(page);
	memcpy(dst + offset, src, copy);
	kunmap_atomic(dst);

	if (copy < n) {
		src += copy;
		sector += copy >> SECTOR_SHIFT;
		copy = n - copy;
		page = rd_lookup_page(rd, sector);
		BUG_ON(!page);

		dst = kmap_atomic(page);
		memcpy(dst, src, copy);
		kunmap_atomic(dst);
	}
}

/*
 * Copy n bytes to dst from the rd starting at sector. Does not sleep.
 */
static void copy_from_rd(void *dst, struct rd_device *rd,
			 sector_t sector, size_t n)
{
	struct page *page;
	void *src;
	unsigned int offset = (sector & (PAGE_SECTORS - 1)) << SECTOR_SHIFT;
	size_t copy;

	copy = min_t(size_t, n, PAGE_SIZE - offset);
	page = rd_lookup_page(rd, sector);
	if (page) {
		src = kmap_atomic(page);
		memcpy(dst, src + offset, copy);
		kunmap_atomic(src);
	} else {
		memset(dst, 0, copy);
	}

	if (copy < n) {
		dst += copy;
		sector += copy >> SECTOR_SHIFT;
		copy = n - copy;
		page = rd_lookup_page(rd, sector);
		if (page) {
			src = kmap_atomic(page);
			memcpy(dst, src, copy);
			kunmap_atomic(src);
		} else {
			memset(dst, 0, copy);
		}
	}
}

/*
 * Process a single bvec of a bio.
 */
static int rd_do_bvec(struct rd_device *rd, struct page *page,
		      unsigned int len, unsigned int off, unsigned int op,
		      sector_t sector)
{
	void *mem;

	mem = kmap_atomic(page);
	if (!op_is_write(op)) {
		copy_from_rd(mem + off, rd, sector, len);
		flush_dcache_page(page);
	} else {
		flush_dcache_page(page);
		copy_to_rd(rd, mem + off, sector, len);
	}
	kunmap_atomic(mem);

	return 0;
}

static blk_qc_t rd_submit_bio(struct bio *bio)
{
	struct rd_device *rd = bio->bi_disk->private_data;
	struct bio_vec bvec;
	sector_t sector;
	struct bvec_iter iter;

	sector = bio->bi_iter.bi_sector;
	if (bio_end_sector(bio) > get_capacity(bio->bi_disk))
		goto io_error;

	bio_for_each_segment(bvec, bio, iter) {
		unsigned int len = bvec.bv_len;
		int err;

		/* Don't support un-aligned buffer */
		WARN_ON_ONCE((bvec.bv_offset & (SECTOR_SIZE - 1)) ||
				(len & (SECTOR_SIZE - 1)));

		err = rd_do_bvec(rd, bvec.bv_page, len, bvec.bv_offset,
				 bio_op(bio), sector);
		if (err)
			goto io_error;
		sector += len >> SECTOR_SHIFT;
	}

	bio_endio(bio);
	return BLK_QC_T_NONE;
io_error:
	bio_io_error(bio);
	return BLK_QC_T_NONE;
}

static int rd_rw_page(struct block_device *bdev, sector_t sector,
		      struct page *page, unsigned int op)
{
	struct rd_device *rd = bdev->bd_disk->private_data;
	int err;

	if (PageTransHuge(page))
		return -ENOTSUPP;
	err = rd_do_bvec(rd, page, PAGE_SIZE, 0, op, sector);
	page_endio(page, op_is_write(op), err);
	return err;
}

static const struct block_device_operations rd_fops = {
	.owner =	THIS_MODULE,
	.submit_bio =	rd_submit_bio,
	.rw_page =	rd_rw_page,
};

static long rd_dax_direct_access(struct dax_device *dax_dev, pgoff_t pgoff,
				 long nr_pages, void **kaddr, pfn_t *pfn)
{
	struct rd_device *rd = dax_get_private(dax_dev);

	phys_addr_t offset = PFN_PHYS(pgoff);
	size_t max_nr_pages = rd->mem_pages - pgoff;

	if (kaddr)
		*kaddr = rd->mem_kaddr + offset;
	if (pfn)
		*pfn = phys_to_pfn_t(rd->mem_addr + offset, PFN_DEV | PFN_MAP);

	return nr_pages > max_nr_pages ? max_nr_pages : nr_pages;
}

static bool rd_dax_supported(struct dax_device *dax_dev,
			     struct block_device *bdev, int blocksize,
			     sector_t start, sector_t sectors)
{
	return true;
}

static size_t rd_dax_copy_from_iter(struct dax_device *dax_dev, pgoff_t pgoff,
				    void *addr, size_t bytes, struct iov_iter *i)
{
	return copy_from_iter(addr, bytes, i);
}

static size_t rd_dax_copy_to_iter(struct dax_device *dax_dev, pgoff_t pgoff,
				  void *addr, size_t bytes, struct iov_iter *i)
{
	return copy_to_iter(addr, bytes, i);
}

static int rd_dax_zero_page_range(struct dax_device *dax_dev, pgoff_t pgoff, size_t nr_pages)
{
	long rc;
	void *kaddr;

	rc = dax_direct_access(dax_dev, pgoff, nr_pages, &kaddr, NULL);
	if (rc < 0)
		return rc;
	memset(kaddr, 0, nr_pages << PAGE_SHIFT);

	return 0;
}

static const struct dax_operations rd_dax_ops = {
	.direct_access = rd_dax_direct_access,
	.dax_supported = rd_dax_supported,
	.copy_from_iter = rd_dax_copy_from_iter,
	.copy_to_iter = rd_dax_copy_to_iter,
	.zero_page_range = rd_dax_zero_page_range,
};

static int rd_init(struct rd_device *rd, int major, int minor)
{
	int ret;
	struct gendisk *disk;

	rd->rd_queue = blk_alloc_queue(NUMA_NO_NODE);
	if (!rd->rd_queue)
		return -ENOMEM;

	/* This is so fdisk will align partitions on 4k, because of
	 * direct_access API needing 4k alignment, returning a PFN
	 * (This is only a problem on very small devices <= 4M,
	 *  otherwise fdisk will align on 1M. Regardless this call
	 *  is harmless)
	 */
	blk_queue_physical_block_size(rd->rd_queue, PAGE_SIZE);
	disk = alloc_disk(1);
	if (!disk) {
		ret = -ENOMEM;
		goto out_free_queue;
	}
	disk->major		= major;
	disk->first_minor	= 0;
	disk->fops		= &rd_fops;
	disk->private_data	= rd;
	disk->flags		= GENHD_FL_EXT_DEVT;
	sprintf(disk->disk_name, "rd%d", minor);
	set_capacity(disk, rd->mem_size >> SECTOR_SHIFT);
	rd->rd_disk = disk;

	rd->mem_kaddr = phys_to_virt(rd->mem_addr);
	rd->mem_pages = PHYS_PFN(rd->mem_size);
	rd->dax_dev = alloc_dax(rd, disk->disk_name, &rd_dax_ops, DAXDEV_F_SYNC);
	if (IS_ERR(rd->dax_dev)) {
		ret = PTR_ERR(rd->dax_dev);
		dev_err(rd->dev, "alloc_dax failed %d\n", ret);
		rd->dax_dev = NULL;
		goto out_free_queue;
	}

	/* Tell the block layer that this is not a rotational device */
	blk_queue_flag_set(QUEUE_FLAG_NONROT, rd->rd_queue);
	blk_queue_flag_clear(QUEUE_FLAG_ADD_RANDOM, rd->rd_queue);
	if (rd->dax_dev)
		blk_queue_flag_set(QUEUE_FLAG_DAX, rd->rd_queue);

	rd->rd_disk->queue = rd->rd_queue;
	add_disk(rd->rd_disk);

	return 0;

out_free_queue:
	blk_cleanup_queue(rd->rd_queue);
	return ret;
}

static int rd_probe(struct platform_device *pdev)
{
	struct rd_device *rd;
	struct device *dev = &pdev->dev;
	struct device_node *node;
	struct resource reg;
	int ret;

	rd = devm_kzalloc(dev, sizeof(*rd), GFP_KERNEL);
	if (!rd)
		return -ENOMEM;

	rd->dev = dev;
	node = of_parse_phandle(dev->of_node, "memory-region", 0);
	if (!node) {
		dev_err(dev, "missing \"memory-region\" property\n");
		return -ENODEV;
	}

	ret = of_address_to_resource(node, 0, &reg);
	of_node_put(node);
	if (ret) {
		dev_err(dev, "missing \"reg\" property\n");
		return -ENODEV;
	}

	rd->mem_addr = reg.start;
	rd->mem_size = resource_size(&reg);

	ret = rd_init(rd, rd_major, 0);
	dev_info(dev, "0x%zx@%pa -> 0x%px dax:%d ret:%d\n",
		 rd->mem_size, &rd->mem_addr, rd->mem_kaddr, (bool)rd->dax_dev, ret);

	return ret;
}

static const struct of_device_id rd_dt_match[] = {
	{ .compatible = "rockchip,ramdisk" },
	{},
};

static struct platform_driver rd_driver = {
	.driver		= {
		.name	= "rd",
		.of_match_table = rd_dt_match,
	},
	.probe = rd_probe,
};

static int __init rd_driver_init(void)
{
	int ret;

	ret = register_blkdev(0, "rd");
	if (ret < 0)
		return ret;
	rd_major = ret;

	return platform_driver_register(&rd_driver);
}
subsys_initcall_sync(rd_driver_init);

MODULE_LICENSE("GPL");
