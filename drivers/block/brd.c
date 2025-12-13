// SPDX-License-Identifier: GPL-2.0-only
/*
 * Ram backed block device driver.
 *
 * Copyright (C) 2007 Nick Piggin
 * Copyright (C) 2007 Novell Inc.
 *
 * Parts derived from drivers/block/rd.c, and drivers/block/loop.c, copyright
 * of their respective owners.
 */

#include <linux/init.h>
#include <linux/initrd.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/major.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/highmem.h>
#include <linux/mutex.h>
#include <linux/pagemap.h>
#include <linux/xarray.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/backing-dev.h>
#include <linux/debugfs.h>

#include <linux/uaccess.h>

/*
 * Each block ramdisk device has a xarray brd_pages of pages that stores
 * the pages containing the block device's contents.
 */
struct brd_device {
	int			brd_number;
	struct gendisk		*brd_disk;
	struct list_head	brd_list;

	/*
	 * Backing store of pages. This is the contents of the block device.
	 */
	struct xarray	        brd_pages;
	u64			brd_nr_pages;
};

/*
 * Look up and return a brd's page with reference grabbed for a given sector.
 */
static struct page *brd_lookup_page(struct brd_device *brd, sector_t sector)
{
	struct page *page;
	XA_STATE(xas, &brd->brd_pages, sector >> PAGE_SECTORS_SHIFT);

	rcu_read_lock();
repeat:
	page = xas_load(&xas);
	if (xas_retry(&xas, page)) {
		xas_reset(&xas);
		goto repeat;
	}

	if (!page)
		goto out;

	if (!get_page_unless_zero(page)) {
		xas_reset(&xas);
		goto repeat;
	}

	if (unlikely(page != xas_reload(&xas))) {
		put_page(page);
		xas_reset(&xas);
		goto repeat;
	}
out:
	rcu_read_unlock();

	return page;
}

/*
 * Insert a new page for a given sector, if one does not already exist.
 * The returned page will grab reference.
 */
static struct page *brd_insert_page(struct brd_device *brd, sector_t sector,
		blk_opf_t opf)
{
	gfp_t gfp = (opf & REQ_NOWAIT) ? GFP_NOWAIT : GFP_NOIO;
	struct page *page, *ret;

	page = alloc_page(gfp | __GFP_ZERO | __GFP_HIGHMEM);
	if (!page)
		return ERR_PTR(-ENOMEM);

	xa_lock(&brd->brd_pages);
	ret = __xa_cmpxchg(&brd->brd_pages, sector >> PAGE_SECTORS_SHIFT, NULL,
			page, gfp);
	if (!ret) {
		brd->brd_nr_pages++;
		get_page(page);
		xa_unlock(&brd->brd_pages);
		return page;
	}

	if (!xa_is_err(ret)) {
		get_page(ret);
		xa_unlock(&brd->brd_pages);
		put_page(page);
		return ret;
	}

	xa_unlock(&brd->brd_pages);
	put_page(page);
	return ERR_PTR(xa_err(ret));
}

/*
 * Free all backing store pages and xarray. This must only be called when
 * there are no other users of the device.
 */
static void brd_free_pages(struct brd_device *brd)
{
	struct page *page;
	pgoff_t idx;

	xa_for_each(&brd->brd_pages, idx, page) {
		put_page(page);
		cond_resched();
	}

	xa_destroy(&brd->brd_pages);
}

/*
 * Process a single segment.  The segment is capped to not cross page boundaries
 * in both the bio and the brd backing memory.
 */
static bool brd_rw_bvec(struct brd_device *brd, struct bio *bio)
{
	struct bio_vec bv = bio_iter_iovec(bio, bio->bi_iter);
	sector_t sector = bio->bi_iter.bi_sector;
	u32 offset = (sector & (PAGE_SECTORS - 1)) << SECTOR_SHIFT;
	blk_opf_t opf = bio->bi_opf;
	struct page *page;
	void *kaddr;

	bv.bv_len = min_t(u32, bv.bv_len, PAGE_SIZE - offset);

	page = brd_lookup_page(brd, sector);
	if (!page && op_is_write(opf)) {
		page = brd_insert_page(brd, sector, opf);
		if (IS_ERR(page))
			goto out_error;
	}

	kaddr = bvec_kmap_local(&bv);
	if (op_is_write(opf)) {
		memcpy_to_page(page, offset, kaddr, bv.bv_len);
	} else {
		if (page)
			memcpy_from_page(kaddr, page, offset, bv.bv_len);
		else
			memset(kaddr, 0, bv.bv_len);
	}
	kunmap_local(kaddr);

	bio_advance_iter_single(bio, &bio->bi_iter, bv.bv_len);
	if (page)
		put_page(page);
	return true;

out_error:
	if (PTR_ERR(page) == -ENOMEM && (opf & REQ_NOWAIT))
		bio_wouldblock_error(bio);
	else
		bio_io_error(bio);
	return false;
}

static void brd_do_discard(struct brd_device *brd, sector_t sector, u32 size)
{
	sector_t aligned_sector = round_up(sector, PAGE_SECTORS);
	sector_t aligned_end = round_down(
			sector + (size >> SECTOR_SHIFT), PAGE_SECTORS);
	struct page *page;

	if (aligned_end <= aligned_sector)
		return;

	xa_lock(&brd->brd_pages);
	while (aligned_sector < aligned_end && aligned_sector < rd_size * 2) {
		page = __xa_erase(&brd->brd_pages, aligned_sector >> PAGE_SECTORS_SHIFT);
		if (page) {
			put_page(page);
			brd->brd_nr_pages--;
		}
		aligned_sector += PAGE_SECTORS;
	}
	xa_unlock(&brd->brd_pages);
}

static void brd_submit_bio(struct bio *bio)
{
	struct brd_device *brd = bio->bi_bdev->bd_disk->private_data;

	if (unlikely(op_is_discard(bio->bi_opf))) {
		brd_do_discard(brd, bio->bi_iter.bi_sector,
				bio->bi_iter.bi_size);
		bio_endio(bio);
		return;
	}

	do {
		if (!brd_rw_bvec(brd, bio))
			return;
	} while (bio->bi_iter.bi_size);

	bio_endio(bio);
}

static const struct block_device_operations brd_fops = {
	.owner =		THIS_MODULE,
	.submit_bio =		brd_submit_bio,
};

/*
 * And now the modules code and kernel interface.
 */
static int rd_nr = CONFIG_BLK_DEV_RAM_COUNT;
module_param(rd_nr, int, 0444);
MODULE_PARM_DESC(rd_nr, "Maximum number of brd devices");

unsigned long rd_size = CONFIG_BLK_DEV_RAM_SIZE;
module_param(rd_size, ulong, 0444);
MODULE_PARM_DESC(rd_size, "Size of each RAM disk in kbytes.");

static int max_part = 1;
module_param(max_part, int, 0444);
MODULE_PARM_DESC(max_part, "Num Minors to reserve between devices");

MODULE_DESCRIPTION("Ram backed block device driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS_BLOCKDEV_MAJOR(RAMDISK_MAJOR);
MODULE_ALIAS("rd");

#ifndef MODULE
/* Legacy boot options - nonmodular */
static int __init ramdisk_size(char *str)
{
	rd_size = simple_strtol(str, NULL, 0);
	return 1;
}
__setup("ramdisk_size=", ramdisk_size);
#endif

/*
 * The device scheme is derived from loop.c. Keep them in synch where possible
 * (should share code eventually).
 */
static LIST_HEAD(brd_devices);
static DEFINE_MUTEX(brd_devices_mutex);
static struct dentry *brd_debugfs_dir;

static struct brd_device *brd_find_or_alloc_device(int i)
{
	struct brd_device *brd;

	mutex_lock(&brd_devices_mutex);
	list_for_each_entry(brd, &brd_devices, brd_list) {
		if (brd->brd_number == i) {
			mutex_unlock(&brd_devices_mutex);
			return ERR_PTR(-EEXIST);
		}
	}

	brd = kzalloc(sizeof(*brd), GFP_KERNEL);
	if (!brd) {
		mutex_unlock(&brd_devices_mutex);
		return ERR_PTR(-ENOMEM);
	}
	brd->brd_number	= i;
	list_add_tail(&brd->brd_list, &brd_devices);
	mutex_unlock(&brd_devices_mutex);
	return brd;
}

static void brd_free_device(struct brd_device *brd)
{
	mutex_lock(&brd_devices_mutex);
	list_del(&brd->brd_list);
	mutex_unlock(&brd_devices_mutex);
	kfree(brd);
}

static int brd_alloc(int i)
{
	struct brd_device *brd;
	struct gendisk *disk;
	char buf[DISK_NAME_LEN];
	int err = -ENOMEM;
	struct queue_limits lim = {
		/*
		 * This is so fdisk will align partitions on 4k, because of
		 * direct_access API needing 4k alignment, returning a PFN
		 * (This is only a problem on very small devices <= 4M,
		 *  otherwise fdisk will align on 1M. Regardless this call
		 *  is harmless)
		 */
		.physical_block_size	= PAGE_SIZE,
		.max_hw_discard_sectors	= UINT_MAX,
		.max_discard_segments	= 1,
		.discard_granularity	= PAGE_SIZE,
		.features		= BLK_FEAT_SYNCHRONOUS |
					  BLK_FEAT_NOWAIT,
	};

	brd = brd_find_or_alloc_device(i);
	if (IS_ERR(brd))
		return PTR_ERR(brd);

	xa_init(&brd->brd_pages);

	snprintf(buf, DISK_NAME_LEN, "ram%d", i);
	if (!IS_ERR_OR_NULL(brd_debugfs_dir))
		debugfs_create_u64(buf, 0444, brd_debugfs_dir,
				&brd->brd_nr_pages);

	disk = brd->brd_disk = blk_alloc_disk(&lim, NUMA_NO_NODE);
	if (IS_ERR(disk)) {
		err = PTR_ERR(disk);
		goto out_free_dev;
	}
	disk->major		= RAMDISK_MAJOR;
	disk->first_minor	= i * max_part;
	disk->minors		= max_part;
	disk->fops		= &brd_fops;
	disk->private_data	= brd;
	strscpy(disk->disk_name, buf, DISK_NAME_LEN);
	set_capacity(disk, rd_size * 2);
	
	err = add_disk(disk);
	if (err)
		goto out_cleanup_disk;

	return 0;

out_cleanup_disk:
	put_disk(disk);
out_free_dev:
	brd_free_device(brd);
	return err;
}

static void brd_probe(dev_t dev)
{
	brd_alloc(MINOR(dev) / max_part);
}

static void brd_cleanup(void)
{
	struct brd_device *brd, *next;

	debugfs_remove_recursive(brd_debugfs_dir);

	list_for_each_entry_safe(brd, next, &brd_devices, brd_list) {
		del_gendisk(brd->brd_disk);
		put_disk(brd->brd_disk);
		brd_free_pages(brd);
		brd_free_device(brd);
	}
}

static inline void brd_check_and_reset_par(void)
{
	if (unlikely(!max_part))
		max_part = 1;

	/*
	 * make sure 'max_part' can be divided exactly by (1U << MINORBITS),
	 * otherwise, it is possiable to get same dev_t when adding partitions.
	 */
	if ((1U << MINORBITS) % max_part != 0)
		max_part = 1UL << fls(max_part);

	if (max_part > DISK_MAX_PARTS) {
		pr_info("brd: max_part can't be larger than %d, reset max_part = %d.\n",
			DISK_MAX_PARTS, DISK_MAX_PARTS);
		max_part = DISK_MAX_PARTS;
	}
}

static int __init brd_init(void)
{
	int err, i;

	/*
	 * brd module now has a feature to instantiate underlying device
	 * structure on-demand, provided that there is an access dev node.
	 *
	 * (1) if rd_nr is specified, create that many upfront. else
	 *     it defaults to CONFIG_BLK_DEV_RAM_COUNT
	 * (2) User can further extend brd devices by create dev node themselves
	 *     and have kernel automatically instantiate actual device
	 *     on-demand. Example:
	 *		mknod /path/devnod_name b 1 X	# 1 is the rd major
	 *		fdisk -l /path/devnod_name
	 *	If (X / max_part) was not already created it will be created
	 *	dynamically.
	 */

	brd_check_and_reset_par();

	brd_debugfs_dir = debugfs_create_dir("ramdisk_pages", NULL);

	if (__register_blkdev(RAMDISK_MAJOR, "ramdisk", brd_probe)) {
		err = -EIO;
		goto out_free;
	}

	for (i = 0; i < rd_nr; i++)
		brd_alloc(i);

	pr_info("brd: module loaded\n");
	return 0;

out_free:
	brd_cleanup();

	pr_info("brd: module NOT loaded !!!\n");
	return err;
}

static void __exit brd_exit(void)
{

	unregister_blkdev(RAMDISK_MAJOR, "ramdisk");
	brd_cleanup();

	pr_info("brd: module unloaded\n");
}

module_init(brd_init);
module_exit(brd_exit);

