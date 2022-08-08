/*
 * ARAnyM block device driver
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/slab.h>

#include <asm/natfeat.h>

static long nfhd_id;

enum {
	/* emulation entry points */
	NFHD_READ_WRITE = 10,
	NFHD_GET_CAPACITY = 14,

	/* skip ACSI devices */
	NFHD_DEV_OFFSET = 8,
};

static inline s32 nfhd_read_write(u32 major, u32 minor, u32 rwflag, u32 recno,
				  u32 count, u32 buf)
{
	return nf_call(nfhd_id + NFHD_READ_WRITE, major, minor, rwflag, recno,
		       count, buf);
}

static inline s32 nfhd_get_capacity(u32 major, u32 minor, u32 *blocks,
				    u32 *blocksize)
{
	return nf_call(nfhd_id + NFHD_GET_CAPACITY, major, minor,
		       virt_to_phys(blocks), virt_to_phys(blocksize));
}

static LIST_HEAD(nfhd_list);

static int major_num;
module_param(major_num, int, 0);

struct nfhd_device {
	struct list_head list;
	int id;
	u32 blocks, bsize;
	int bshift;
	struct request_queue *queue;
	struct gendisk *disk;
};

static blk_qc_t nfhd_submit_bio(struct bio *bio)
{
	struct nfhd_device *dev = bio->bi_bdev->bd_disk->private_data;
	struct bio_vec bvec;
	struct bvec_iter iter;
	int dir, len, shift;
	sector_t sec = bio->bi_iter.bi_sector;

	dir = bio_data_dir(bio);
	shift = dev->bshift;
	bio_for_each_segment(bvec, bio, iter) {
		len = bvec.bv_len;
		len >>= 9;
		nfhd_read_write(dev->id, 0, dir, sec >> shift, len >> shift,
				page_to_phys(bvec.bv_page) + bvec.bv_offset);
		sec += len;
	}
	bio_endio(bio);
	return BLK_QC_T_NONE;
}

static int nfhd_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	struct nfhd_device *dev = bdev->bd_disk->private_data;

	geo->cylinders = dev->blocks >> (6 - dev->bshift);
	geo->heads = 4;
	geo->sectors = 16;

	return 0;
}

static const struct block_device_operations nfhd_ops = {
	.owner	= THIS_MODULE,
	.submit_bio = nfhd_submit_bio,
	.getgeo	= nfhd_getgeo,
};

static int __init nfhd_init_one(int id, u32 blocks, u32 bsize)
{
	struct nfhd_device *dev;
	int dev_id = id - NFHD_DEV_OFFSET;

	pr_info("nfhd%u: found device with %u blocks (%u bytes)\n", dev_id,
		blocks, bsize);

	if (bsize < 512 || (bsize & (bsize - 1))) {
		pr_warn("nfhd%u: invalid block size\n", dev_id);
		return -EINVAL;
	}

	dev = kmalloc(sizeof(struct nfhd_device), GFP_KERNEL);
	if (!dev)
		goto out;

	dev->id = id;
	dev->blocks = blocks;
	dev->bsize = bsize;
	dev->bshift = ffs(bsize) - 10;

	dev->queue = blk_alloc_queue(NUMA_NO_NODE);
	if (dev->queue == NULL)
		goto free_dev;

	blk_queue_logical_block_size(dev->queue, bsize);

	dev->disk = alloc_disk(16);
	if (!dev->disk)
		goto free_queue;

	dev->disk->major = major_num;
	dev->disk->first_minor = dev_id * 16;
	dev->disk->fops = &nfhd_ops;
	dev->disk->private_data = dev;
	sprintf(dev->disk->disk_name, "nfhd%u", dev_id);
	set_capacity(dev->disk, (sector_t)blocks * (bsize / 512));
	dev->disk->queue = dev->queue;

	add_disk(dev->disk);

	list_add_tail(&dev->list, &nfhd_list);

	return 0;

free_queue:
	blk_cleanup_queue(dev->queue);
free_dev:
	kfree(dev);
out:
	return -ENOMEM;
}

static int __init nfhd_init(void)
{
	u32 blocks, bsize;
	int ret;
	int i;

	nfhd_id = nf_get_id("XHDI");
	if (!nfhd_id)
		return -ENODEV;

	ret = register_blkdev(major_num, "nfhd");
	if (ret < 0) {
		pr_warn("nfhd: unable to get major number\n");
		return ret;
	}

	if (!major_num)
		major_num = ret;

	for (i = NFHD_DEV_OFFSET; i < 24; i++) {
		if (nfhd_get_capacity(i, 0, &blocks, &bsize))
			continue;
		nfhd_init_one(i, blocks, bsize);
	}

	return 0;
}

static void __exit nfhd_exit(void)
{
	struct nfhd_device *dev, *next;

	list_for_each_entry_safe(dev, next, &nfhd_list, list) {
		list_del(&dev->list);
		del_gendisk(dev->disk);
		put_disk(dev->disk);
		blk_cleanup_queue(dev->queue);
		kfree(dev);
	}
	unregister_blkdev(major_num, "nfhd");
}

module_init(nfhd_init);
module_exit(nfhd_exit);

MODULE_LICENSE("GPL");
