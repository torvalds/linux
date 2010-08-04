/*
 * Compressed RAM based swap device
 *
 * Copyright (C) 2008, 2009, 2010  Nitin Gupta
 *
 * This code is released using a dual license strategy: BSD/GPL
 * You can choose the licence that better fits your requirements.
 *
 * Released under the terms of 3-clause BSD License
 * Released under the terms of GNU General Public License Version 2.0
 *
 * Project home: http://compcache.googlecode.com
 */

#define KMSG_COMPONENT "ramzswap"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/device.h>
#include <linux/genhd.h>
#include <linux/highmem.h>
#include <linux/slab.h>
#include <linux/lzo.h>
#include <linux/string.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/vmalloc.h>

#include "ramzswap_drv.h"

/* Globals */
static int ramzswap_major;
static struct ramzswap *devices;

/* Module params (documentation at end) */
static unsigned int num_devices;

static int rzs_test_flag(struct ramzswap *rzs, u32 index,
			enum rzs_pageflags flag)
{
	return rzs->table[index].flags & BIT(flag);
}

static void rzs_set_flag(struct ramzswap *rzs, u32 index,
			enum rzs_pageflags flag)
{
	rzs->table[index].flags |= BIT(flag);
}

static void rzs_clear_flag(struct ramzswap *rzs, u32 index,
			enum rzs_pageflags flag)
{
	rzs->table[index].flags &= ~BIT(flag);
}

static int page_zero_filled(void *ptr)
{
	unsigned int pos;
	unsigned long *page;

	page = (unsigned long *)ptr;

	for (pos = 0; pos != PAGE_SIZE / sizeof(*page); pos++) {
		if (page[pos])
			return 0;
	}

	return 1;
}

static void ramzswap_set_disksize(struct ramzswap *rzs, size_t totalram_bytes)
{
	if (!rzs->disksize) {
		pr_info(
		"disk size not provided. You can use disksize_kb module "
		"param to specify size.\nUsing default: (%u%% of RAM).\n",
		default_disksize_perc_ram
		);
		rzs->disksize = default_disksize_perc_ram *
					(totalram_bytes / 100);
	}

	if (rzs->disksize > 2 * (totalram_bytes)) {
		pr_info(
		"There is little point creating a ramzswap of greater than "
		"twice the size of memory since we expect a 2:1 compression "
		"ratio. Note that ramzswap uses about 0.1%% of the size of "
		"the swap device when not in use so a huge ramzswap is "
		"wasteful.\n"
		"\tMemory Size: %zu kB\n"
		"\tSize you selected: %zu kB\n"
		"Continuing anyway ...\n",
		totalram_bytes >> 10, rzs->disksize
		);
	}

	rzs->disksize &= PAGE_MASK;
}

/*
 * Swap header (1st page of swap device) contains information
 * about a swap file/partition. Prepare such a header for the
 * given ramzswap device so that swapon can identify it as a
 * swap partition.
 */
static void setup_swap_header(struct ramzswap *rzs, union swap_header *s)
{
	s->info.version = 1;
	s->info.last_page = (rzs->disksize >> PAGE_SHIFT) - 1;
	s->info.nr_badpages = 0;
	memcpy(s->magic.magic, "SWAPSPACE2", 10);
}

static void ramzswap_ioctl_get_stats(struct ramzswap *rzs,
			struct ramzswap_ioctl_stats *s)
{
	s->disksize = rzs->disksize;

#if defined(CONFIG_RAMZSWAP_STATS)
	{
	struct ramzswap_stats *rs = &rzs->stats;
	size_t succ_writes, mem_used;
	unsigned int good_compress_perc = 0, no_compress_perc = 0;

	mem_used = xv_get_total_size_bytes(rzs->mem_pool)
			+ (rs->pages_expand << PAGE_SHIFT);
	succ_writes = rzs_stat64_read(rzs, &rs->num_writes) -
			rzs_stat64_read(rzs, &rs->failed_writes);

	if (succ_writes && rs->pages_stored) {
		good_compress_perc = rs->good_compress * 100
					/ rs->pages_stored;
		no_compress_perc = rs->pages_expand * 100
					/ rs->pages_stored;
	}

	s->num_reads = rzs_stat64_read(rzs, &rs->num_reads);
	s->num_writes = rzs_stat64_read(rzs, &rs->num_writes);
	s->failed_reads = rzs_stat64_read(rzs, &rs->failed_reads);
	s->failed_writes = rzs_stat64_read(rzs, &rs->failed_writes);
	s->invalid_io = rzs_stat64_read(rzs, &rs->invalid_io);
	s->notify_free = rzs_stat64_read(rzs, &rs->notify_free);
	s->pages_zero = rs->pages_zero;

	s->good_compress_pct = good_compress_perc;
	s->pages_expand_pct = no_compress_perc;

	s->pages_stored = rs->pages_stored;
	s->pages_used = mem_used >> PAGE_SHIFT;
	s->orig_data_size = rs->pages_stored << PAGE_SHIFT;
	s->compr_data_size = rs->compr_size;
	s->mem_used_total = mem_used;
	}
#endif /* CONFIG_RAMZSWAP_STATS */
}

static void ramzswap_free_page(struct ramzswap *rzs, size_t index)
{
	u32 clen;
	void *obj;

	struct page *page = rzs->table[index].page;
	u32 offset = rzs->table[index].offset;

	if (unlikely(!page)) {
		/*
		 * No memory is allocated for zero filled pages.
		 * Simply clear zero page flag.
		 */
		if (rzs_test_flag(rzs, index, RZS_ZERO)) {
			rzs_clear_flag(rzs, index, RZS_ZERO);
			rzs_stat_dec(&rzs->stats.pages_zero);
		}
		return;
	}

	if (unlikely(rzs_test_flag(rzs, index, RZS_UNCOMPRESSED))) {
		clen = PAGE_SIZE;
		__free_page(page);
		rzs_clear_flag(rzs, index, RZS_UNCOMPRESSED);
		rzs_stat_dec(&rzs->stats.pages_expand);
		goto out;
	}

	obj = kmap_atomic(page, KM_USER0) + offset;
	clen = xv_get_object_size(obj) - sizeof(struct zobj_header);
	kunmap_atomic(obj, KM_USER0);

	xv_free(rzs->mem_pool, page, offset);
	if (clen <= PAGE_SIZE / 2)
		rzs_stat_dec(&rzs->stats.good_compress);

out:
	rzs->stats.compr_size -= clen;
	rzs_stat_dec(&rzs->stats.pages_stored);

	rzs->table[index].page = NULL;
	rzs->table[index].offset = 0;
}

static int handle_zero_page(struct bio *bio)
{
	void *user_mem;
	struct page *page = bio->bi_io_vec[0].bv_page;

	user_mem = kmap_atomic(page, KM_USER0);
	memset(user_mem, 0, PAGE_SIZE);
	kunmap_atomic(user_mem, KM_USER0);

	flush_dcache_page(page);

	set_bit(BIO_UPTODATE, &bio->bi_flags);
	bio_endio(bio, 0);
	return 0;
}

static int handle_uncompressed_page(struct ramzswap *rzs, struct bio *bio)
{
	u32 index;
	struct page *page;
	unsigned char *user_mem, *cmem;

	page = bio->bi_io_vec[0].bv_page;
	index = bio->bi_sector >> SECTORS_PER_PAGE_SHIFT;

	user_mem = kmap_atomic(page, KM_USER0);
	cmem = kmap_atomic(rzs->table[index].page, KM_USER1) +
			rzs->table[index].offset;

	memcpy(user_mem, cmem, PAGE_SIZE);
	kunmap_atomic(user_mem, KM_USER0);
	kunmap_atomic(cmem, KM_USER1);

	flush_dcache_page(page);

	set_bit(BIO_UPTODATE, &bio->bi_flags);
	bio_endio(bio, 0);
	return 0;
}

/*
 * Called when request page is not present in ramzswap.
 * This is an attempt to read before any previous write
 * to this location - this happens due to readahead when
 * swap device is read from user-space (e.g. during swapon)
 */
static int handle_ramzswap_fault(struct ramzswap *rzs, struct bio *bio)
{
	pr_debug("Read before write on swap device: "
		"sector=%lu, size=%u, offset=%u\n",
		(ulong)(bio->bi_sector), bio->bi_size,
		bio->bi_io_vec[0].bv_offset);

	/* Do nothing. Just return success */
	set_bit(BIO_UPTODATE, &bio->bi_flags);
	bio_endio(bio, 0);
	return 0;
}

static int ramzswap_read(struct ramzswap *rzs, struct bio *bio)
{
	int ret;
	u32 index;
	size_t clen;
	struct page *page;
	struct zobj_header *zheader;
	unsigned char *user_mem, *cmem;

	rzs_stat64_inc(rzs, &rzs->stats.num_reads);

	page = bio->bi_io_vec[0].bv_page;
	index = bio->bi_sector >> SECTORS_PER_PAGE_SHIFT;

	if (rzs_test_flag(rzs, index, RZS_ZERO))
		return handle_zero_page(bio);

	/* Requested page is not present in compressed area */
	if (!rzs->table[index].page)
		return handle_ramzswap_fault(rzs, bio);

	/* Page is stored uncompressed since it's incompressible */
	if (unlikely(rzs_test_flag(rzs, index, RZS_UNCOMPRESSED)))
		return handle_uncompressed_page(rzs, bio);

	user_mem = kmap_atomic(page, KM_USER0);
	clen = PAGE_SIZE;

	cmem = kmap_atomic(rzs->table[index].page, KM_USER1) +
			rzs->table[index].offset;

	ret = lzo1x_decompress_safe(
		cmem + sizeof(*zheader),
		xv_get_object_size(cmem) - sizeof(*zheader),
		user_mem, &clen);

	kunmap_atomic(user_mem, KM_USER0);
	kunmap_atomic(cmem, KM_USER1);

	/* should NEVER happen */
	if (unlikely(ret != LZO_E_OK)) {
		pr_err("Decompression failed! err=%d, page=%u\n",
			ret, index);
		rzs_stat64_inc(rzs, &rzs->stats.failed_reads);
		goto out;
	}

	flush_dcache_page(page);

	set_bit(BIO_UPTODATE, &bio->bi_flags);
	bio_endio(bio, 0);
	return 0;

out:
	bio_io_error(bio);
	return 0;
}

static int ramzswap_write(struct ramzswap *rzs, struct bio *bio)
{
	int ret;
	u32 offset, index;
	size_t clen;
	struct zobj_header *zheader;
	struct page *page, *page_store;
	unsigned char *user_mem, *cmem, *src;

	rzs_stat64_inc(rzs, &rzs->stats.num_writes);

	page = bio->bi_io_vec[0].bv_page;
	index = bio->bi_sector >> SECTORS_PER_PAGE_SHIFT;

	src = rzs->compress_buffer;

	mutex_lock(&rzs->lock);

	user_mem = kmap_atomic(page, KM_USER0);
	if (page_zero_filled(user_mem)) {
		kunmap_atomic(user_mem, KM_USER0);
		mutex_unlock(&rzs->lock);
		rzs_stat_inc(&rzs->stats.pages_zero);
		rzs_set_flag(rzs, index, RZS_ZERO);

		set_bit(BIO_UPTODATE, &bio->bi_flags);
		bio_endio(bio, 0);
		return 0;
	}

	ret = lzo1x_1_compress(user_mem, PAGE_SIZE, src, &clen,
				rzs->compress_workmem);

	kunmap_atomic(user_mem, KM_USER0);

	if (unlikely(ret != LZO_E_OK)) {
		mutex_unlock(&rzs->lock);
		pr_err("Compression failed! err=%d\n", ret);
		rzs_stat64_inc(rzs, &rzs->stats.failed_writes);
		goto out;
	}

	/*
	 * Page is incompressible. Store it as-is (uncompressed)
	 * since we do not want to return too many swap write
	 * errors which has side effect of hanging the system.
	 */
	if (unlikely(clen > max_zpage_size)) {
		clen = PAGE_SIZE;
		page_store = alloc_page(GFP_NOIO | __GFP_HIGHMEM);
		if (unlikely(!page_store)) {
			mutex_unlock(&rzs->lock);
			pr_info("Error allocating memory for incompressible "
				"page: %u\n", index);
			rzs_stat64_inc(rzs, &rzs->stats.failed_writes);
			goto out;
		}

		offset = 0;
		rzs_set_flag(rzs, index, RZS_UNCOMPRESSED);
		rzs_stat_inc(&rzs->stats.pages_expand);
		rzs->table[index].page = page_store;
		src = kmap_atomic(page, KM_USER0);
		goto memstore;
	}

	if (xv_malloc(rzs->mem_pool, clen + sizeof(*zheader),
			&rzs->table[index].page, &offset,
			GFP_NOIO | __GFP_HIGHMEM)) {
		mutex_unlock(&rzs->lock);
		pr_info("Error allocating memory for compressed "
			"page: %u, size=%zu\n", index, clen);
		rzs_stat64_inc(rzs, &rzs->stats.failed_writes);
		goto out;
	}

memstore:
	rzs->table[index].offset = offset;

	cmem = kmap_atomic(rzs->table[index].page, KM_USER1) +
			rzs->table[index].offset;

#if 0
	/* Back-reference needed for memory defragmentation */
	if (!rzs_test_flag(rzs, index, RZS_UNCOMPRESSED)) {
		zheader = (struct zobj_header *)cmem;
		zheader->table_idx = index;
		cmem += sizeof(*zheader);
	}
#endif

	memcpy(cmem, src, clen);

	kunmap_atomic(cmem, KM_USER1);
	if (unlikely(rzs_test_flag(rzs, index, RZS_UNCOMPRESSED)))
		kunmap_atomic(src, KM_USER0);

	/* Update stats */
	rzs->stats.compr_size += clen;
	rzs_stat_inc(&rzs->stats.pages_stored);
	if (clen <= PAGE_SIZE / 2)
		rzs_stat_inc(&rzs->stats.good_compress);

	mutex_unlock(&rzs->lock);

	set_bit(BIO_UPTODATE, &bio->bi_flags);
	bio_endio(bio, 0);
	return 0;

out:
	bio_io_error(bio);
	return 0;
}

/*
 * Check if request is within bounds and page aligned.
 */
static inline int valid_swap_request(struct ramzswap *rzs, struct bio *bio)
{
	if (unlikely(
		(bio->bi_sector >= (rzs->disksize >> SECTOR_SHIFT)) ||
		(bio->bi_sector & (SECTORS_PER_PAGE - 1)) ||
		(bio->bi_vcnt != 1) ||
		(bio->bi_size != PAGE_SIZE) ||
		(bio->bi_io_vec[0].bv_offset != 0))) {

		return 0;
	}

	/* swap request is valid */
	return 1;
}

/*
 * Handler function for all ramzswap I/O requests.
 */
static int ramzswap_make_request(struct request_queue *queue, struct bio *bio)
{
	int ret = 0;
	struct ramzswap *rzs = queue->queuedata;

	if (unlikely(!rzs->init_done)) {
		bio_io_error(bio);
		return 0;
	}

	if (!valid_swap_request(rzs, bio)) {
		rzs_stat64_inc(rzs, &rzs->stats.invalid_io);
		bio_io_error(bio);
		return 0;
	}

	switch (bio_data_dir(bio)) {
	case READ:
		ret = ramzswap_read(rzs, bio);
		break;

	case WRITE:
		ret = ramzswap_write(rzs, bio);
		break;
	}

	return ret;
}

static void reset_device(struct ramzswap *rzs)
{
	size_t index;

	/* Do not accept any new I/O request */
	rzs->init_done = 0;

	/* Free various per-device buffers */
	kfree(rzs->compress_workmem);
	free_pages((unsigned long)rzs->compress_buffer, 1);

	rzs->compress_workmem = NULL;
	rzs->compress_buffer = NULL;

	/* Free all pages that are still in this ramzswap device */
	for (index = 0; index < rzs->disksize >> PAGE_SHIFT; index++) {
		struct page *page;
		u16 offset;

		page = rzs->table[index].page;
		offset = rzs->table[index].offset;

		if (!page)
			continue;

		if (unlikely(rzs_test_flag(rzs, index, RZS_UNCOMPRESSED)))
			__free_page(page);
		else
			xv_free(rzs->mem_pool, page, offset);
	}

	vfree(rzs->table);
	rzs->table = NULL;

	xv_destroy_pool(rzs->mem_pool);
	rzs->mem_pool = NULL;

	/* Reset stats */
	memset(&rzs->stats, 0, sizeof(rzs->stats));

	rzs->disksize = 0;
}

static int ramzswap_ioctl_init_device(struct ramzswap *rzs)
{
	int ret;
	size_t num_pages;
	struct page *page;
	union swap_header *swap_header;

	if (rzs->init_done) {
		pr_info("Device already initialized!\n");
		return -EBUSY;
	}

	ramzswap_set_disksize(rzs, totalram_pages << PAGE_SHIFT);

	rzs->compress_workmem = kzalloc(LZO1X_MEM_COMPRESS, GFP_KERNEL);
	if (!rzs->compress_workmem) {
		pr_err("Error allocating compressor working memory!\n");
		ret = -ENOMEM;
		goto fail;
	}

	rzs->compress_buffer = (void *)__get_free_pages(__GFP_ZERO, 1);
	if (!rzs->compress_buffer) {
		pr_err("Error allocating compressor buffer space\n");
		ret = -ENOMEM;
		goto fail;
	}

	num_pages = rzs->disksize >> PAGE_SHIFT;
	rzs->table = vmalloc(num_pages * sizeof(*rzs->table));
	if (!rzs->table) {
		pr_err("Error allocating ramzswap address table\n");
		/* To prevent accessing table entries during cleanup */
		rzs->disksize = 0;
		ret = -ENOMEM;
		goto fail;
	}
	memset(rzs->table, 0, num_pages * sizeof(*rzs->table));

	page = alloc_page(__GFP_ZERO);
	if (!page) {
		pr_err("Error allocating swap header page\n");
		ret = -ENOMEM;
		goto fail;
	}
	rzs->table[0].page = page;
	rzs_set_flag(rzs, 0, RZS_UNCOMPRESSED);

	swap_header = kmap(page);
	setup_swap_header(rzs, swap_header);
	kunmap(page);

	set_capacity(rzs->disk, rzs->disksize >> SECTOR_SHIFT);

	/* ramzswap devices sort of resembles non-rotational disks */
	queue_flag_set_unlocked(QUEUE_FLAG_NONROT, rzs->disk->queue);

	rzs->mem_pool = xv_create_pool();
	if (!rzs->mem_pool) {
		pr_err("Error creating memory pool\n");
		ret = -ENOMEM;
		goto fail;
	}

	rzs->init_done = 1;

	pr_debug("Initialization done!\n");
	return 0;

fail:
	reset_device(rzs);

	pr_err("Initialization failed: err=%d\n", ret);
	return ret;
}

static int ramzswap_ioctl_reset_device(struct ramzswap *rzs)
{
	if (rzs->init_done)
		reset_device(rzs);

	return 0;
}

static int ramzswap_ioctl(struct block_device *bdev, fmode_t mode,
			unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	size_t disksize_kb;

	struct ramzswap *rzs = bdev->bd_disk->private_data;

	switch (cmd) {
	case RZSIO_SET_DISKSIZE_KB:
		if (rzs->init_done) {
			ret = -EBUSY;
			goto out;
		}
		if (copy_from_user(&disksize_kb, (void *)arg,
						_IOC_SIZE(cmd))) {
			ret = -EFAULT;
			goto out;
		}
		rzs->disksize = disksize_kb << 10;
		pr_info("Disk size set to %zu kB\n", disksize_kb);
		break;

	case RZSIO_GET_STATS:
	{
		struct ramzswap_ioctl_stats *stats;
		if (!rzs->init_done) {
			ret = -ENOTTY;
			goto out;
		}
		stats = kzalloc(sizeof(*stats), GFP_KERNEL);
		if (!stats) {
			ret = -ENOMEM;
			goto out;
		}
		ramzswap_ioctl_get_stats(rzs, stats);
		if (copy_to_user((void *)arg, stats, sizeof(*stats))) {
			kfree(stats);
			ret = -EFAULT;
			goto out;
		}
		kfree(stats);
		break;
	}
	case RZSIO_INIT:
		ret = ramzswap_ioctl_init_device(rzs);
		break;

	case RZSIO_RESET:
		/* Do not reset an active device! */
		if (bdev->bd_holders) {
			ret = -EBUSY;
			goto out;
		}

		/* Make sure all pending I/O is finished */
		if (bdev)
			fsync_bdev(bdev);

		ret = ramzswap_ioctl_reset_device(rzs);
		break;

	default:
		pr_info("Invalid ioctl %u\n", cmd);
		ret = -ENOTTY;
	}

out:
	return ret;
}

void ramzswap_slot_free_notify(struct block_device *bdev, unsigned long index)
{
	struct ramzswap *rzs;

	rzs = bdev->bd_disk->private_data;
	ramzswap_free_page(rzs, index);
	rzs_stat64_inc(rzs, &rzs->stats.notify_free);

	return;
}

static struct block_device_operations ramzswap_devops = {
	.ioctl = ramzswap_ioctl,
	.swap_slot_free_notify = ramzswap_slot_free_notify,
	.owner = THIS_MODULE
};

static int create_device(struct ramzswap *rzs, int device_id)
{
	int ret = 0;

	mutex_init(&rzs->lock);
	spin_lock_init(&rzs->stat64_lock);

	rzs->queue = blk_alloc_queue(GFP_KERNEL);
	if (!rzs->queue) {
		pr_err("Error allocating disk queue for device %d\n",
			device_id);
		ret = -ENOMEM;
		goto out;
	}

	blk_queue_make_request(rzs->queue, ramzswap_make_request);
	rzs->queue->queuedata = rzs;

	 /* gendisk structure */
	rzs->disk = alloc_disk(1);
	if (!rzs->disk) {
		blk_cleanup_queue(rzs->queue);
		pr_warning("Error allocating disk structure for device %d\n",
			device_id);
		ret = -ENOMEM;
		goto out;
	}

	rzs->disk->major = ramzswap_major;
	rzs->disk->first_minor = device_id;
	rzs->disk->fops = &ramzswap_devops;
	rzs->disk->queue = rzs->queue;
	rzs->disk->private_data = rzs;
	snprintf(rzs->disk->disk_name, 16, "ramzswap%d", device_id);

	/* Actual capacity set using RZSIO_SET_DISKSIZE_KB ioctl */
	set_capacity(rzs->disk, 0);

	blk_queue_physical_block_size(rzs->disk->queue, PAGE_SIZE);
	blk_queue_logical_block_size(rzs->disk->queue, PAGE_SIZE);

	add_disk(rzs->disk);

	rzs->init_done = 0;

out:
	return ret;
}

static void destroy_device(struct ramzswap *rzs)
{
	if (rzs->disk) {
		del_gendisk(rzs->disk);
		put_disk(rzs->disk);
	}

	if (rzs->queue)
		blk_cleanup_queue(rzs->queue);
}

static int __init ramzswap_init(void)
{
	int ret, dev_id;

	if (num_devices > max_num_devices) {
		pr_warning("Invalid value for num_devices: %u\n",
				num_devices);
		ret = -EINVAL;
		goto out;
	}

	ramzswap_major = register_blkdev(0, "ramzswap");
	if (ramzswap_major <= 0) {
		pr_warning("Unable to get major number\n");
		ret = -EBUSY;
		goto out;
	}

	if (!num_devices) {
		pr_info("num_devices not specified. Using default: 1\n");
		num_devices = 1;
	}

	/* Allocate the device array and initialize each one */
	pr_info("Creating %u devices ...\n", num_devices);
	devices = kzalloc(num_devices * sizeof(struct ramzswap), GFP_KERNEL);
	if (!devices) {
		ret = -ENOMEM;
		goto unregister;
	}

	for (dev_id = 0; dev_id < num_devices; dev_id++) {
		ret = create_device(&devices[dev_id], dev_id);
		if (ret)
			goto free_devices;
	}

	return 0;

free_devices:
	while (dev_id)
		destroy_device(&devices[--dev_id]);
unregister:
	unregister_blkdev(ramzswap_major, "ramzswap");
out:
	return ret;
}

static void __exit ramzswap_exit(void)
{
	int i;
	struct ramzswap *rzs;

	for (i = 0; i < num_devices; i++) {
		rzs = &devices[i];

		destroy_device(rzs);
		if (rzs->init_done)
			reset_device(rzs);
	}

	unregister_blkdev(ramzswap_major, "ramzswap");

	kfree(devices);
	pr_debug("Cleanup done!\n");
}

module_param(num_devices, uint, 0);
MODULE_PARM_DESC(num_devices, "Number of ramzswap devices");

module_init(ramzswap_init);
module_exit(ramzswap_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Nitin Gupta <ngupta@vflare.org>");
MODULE_DESCRIPTION("Compressed RAM Based Swap Device");
