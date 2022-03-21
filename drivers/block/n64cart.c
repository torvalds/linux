// SPDX-License-Identifier: GPL-2.0
/*
 * Support for the N64 cart.
 *
 * Copyright (c) 2021 Lauri Kasanen
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/bitops.h>
#include <linux/blkdev.h>
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>

enum {
	PI_DRAM_REG = 0,
	PI_CART_REG,
	PI_READ_REG,
	PI_WRITE_REG,
	PI_STATUS_REG,
};

#define PI_STATUS_DMA_BUSY	(1 << 0)
#define PI_STATUS_IO_BUSY	(1 << 1)

#define CART_DOMAIN		0x10000000
#define CART_MAX		0x1FFFFFFF

#define MIN_ALIGNMENT		8

static u32 __iomem *reg_base;

static unsigned int start;
module_param(start, uint, 0);
MODULE_PARM_DESC(start, "Start address of the cart block data");

static unsigned int size;
module_param(size, uint, 0);
MODULE_PARM_DESC(size, "Size of the cart block data, in bytes");

static void n64cart_write_reg(const u8 reg, const u32 value)
{
	writel(value, reg_base + reg);
}

static u32 n64cart_read_reg(const u8 reg)
{
	return readl(reg_base + reg);
}

static void n64cart_wait_dma(void)
{
	while (n64cart_read_reg(PI_STATUS_REG) &
		(PI_STATUS_DMA_BUSY | PI_STATUS_IO_BUSY))
		cpu_relax();
}

/*
 * Process a single bvec of a bio.
 */
static bool n64cart_do_bvec(struct device *dev, struct bio_vec *bv, u32 pos)
{
	dma_addr_t dma_addr;
	const u32 bstart = pos + start;

	/* Alignment check */
	WARN_ON_ONCE((bv->bv_offset & (MIN_ALIGNMENT - 1)) ||
		     (bv->bv_len & (MIN_ALIGNMENT - 1)));

	dma_addr = dma_map_bvec(dev, bv, DMA_FROM_DEVICE, 0);
	if (dma_mapping_error(dev, dma_addr))
		return false;

	n64cart_wait_dma();

	n64cart_write_reg(PI_DRAM_REG, dma_addr);
	n64cart_write_reg(PI_CART_REG, (bstart | CART_DOMAIN) & CART_MAX);
	n64cart_write_reg(PI_WRITE_REG, bv->bv_len - 1);

	n64cart_wait_dma();

	dma_unmap_page(dev, dma_addr, bv->bv_len, DMA_FROM_DEVICE);
	return true;
}

static blk_qc_t n64cart_submit_bio(struct bio *bio)
{
	struct bio_vec bvec;
	struct bvec_iter iter;
	struct device *dev = bio->bi_bdev->bd_disk->private_data;
	u32 pos = bio->bi_iter.bi_sector << SECTOR_SHIFT;

	bio_for_each_segment(bvec, bio, iter) {
		if (!n64cart_do_bvec(dev, &bvec, pos))
			goto io_error;
		pos += bvec.bv_len;
	}

	bio_endio(bio);
	return BLK_QC_T_NONE;
io_error:
	bio_io_error(bio);
	return BLK_QC_T_NONE;
}

static const struct block_device_operations n64cart_fops = {
	.owner		= THIS_MODULE,
	.submit_bio	= n64cart_submit_bio,
};

/*
 * The target device is embedded and RAM-constrained. We save RAM
 * by initializing in __init code that gets dropped late in boot.
 * For the same reason there is no module or unloading support.
 */
static int __init n64cart_probe(struct platform_device *pdev)
{
	struct gendisk *disk;

	if (!start || !size) {
		pr_err("start or size not specified\n");
		return -ENODEV;
	}

	if (size & 4095) {
		pr_err("size must be a multiple of 4K\n");
		return -ENODEV;
	}

	reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(reg_base))
		return PTR_ERR(reg_base);

	disk = blk_alloc_disk(NUMA_NO_NODE);
	if (!disk)
		return -ENOMEM;

	disk->first_minor = 0;
	disk->flags = GENHD_FL_NO_PART_SCAN;
	disk->fops = &n64cart_fops;
	disk->private_data = &pdev->dev;
	strcpy(disk->disk_name, "n64cart");

	set_capacity(disk, size >> SECTOR_SHIFT);
	set_disk_ro(disk, 1);

	blk_queue_flag_set(QUEUE_FLAG_NONROT, disk->queue);
	blk_queue_physical_block_size(disk->queue, 4096);
	blk_queue_logical_block_size(disk->queue, 4096);

	add_disk(disk);

	pr_info("n64cart: %u kb disk\n", size / 1024);

	return 0;
}

static struct platform_driver n64cart_driver = {
	.driver = {
		.name = "n64cart",
	},
};

static int __init n64cart_init(void)
{
	return platform_driver_probe(&n64cart_driver, n64cart_probe);
}

module_init(n64cart_init);

MODULE_AUTHOR("Lauri Kasanen <cand@gmx.com>");
MODULE_DESCRIPTION("Driver for the N64 cart");
MODULE_LICENSE("GPL");
