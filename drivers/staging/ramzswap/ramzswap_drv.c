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

/*
 * Pages that compress to larger than this size are
 * forwarded to backing swap, if present or stored
 * uncompressed in memory otherwise.
 */
static unsigned int max_zpage_size;

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

/*
 * memlimit cannot be greater than backing disk size.
 */
static void ramzswap_set_memlimit(struct ramzswap *rzs, size_t totalram_bytes)
{
	int memlimit_valid = 1;

	if (!rzs->memlimit) {
		pr_info("Memory limit not set.\n");
		memlimit_valid = 0;
	}

	if (rzs->memlimit > rzs->disksize) {
		pr_info("Memory limit cannot be greater than "
			"disksize: limit=%zu, disksize=%zu\n",
			rzs->memlimit, rzs->disksize);
		memlimit_valid = 0;
	}

	if (!memlimit_valid) {
		size_t mempart, disksize;
		pr_info("Using default: smaller of (%u%% of RAM) and "
			"(backing disk size).\n",
			default_memlimit_perc_ram);
		mempart = default_memlimit_perc_ram * (totalram_bytes / 100);
		disksize = rzs->disksize;
		rzs->memlimit = mempart > disksize ? disksize : mempart;
	}

	if (rzs->memlimit > totalram_bytes / 2) {
		pr_info(
		"Its not advisable setting limit more than half of "
		"size of memory since we expect a 2:1 compression ratio. "
		"Limit represents amount of *compressed* data we can keep "
		"in memory!\n"
		"\tMemory Size: %zu kB\n"
		"\tLimit you selected: %zu kB\n"
		"Continuing anyway ...\n",
		totalram_bytes >> 10, rzs->memlimit >> 10
		);
	}

	rzs->memlimit &= PAGE_MASK;
	BUG_ON(!rzs->memlimit);
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
 * to indentify it as a swap partition. Prepare such a header
 * for ramzswap device (ramzswap0) so that swapon can identify
 * it as swap partition. In case backing swap device is provided,
 * copy its swap header.
 */
static int setup_swap_header(struct ramzswap *rzs, union swap_header *s)
{
	int ret = 0;
	struct page *page;
	struct address_space *mapping;
	union swap_header *backing_swap_header;

	/*
	 * There is no backing swap device. Create a swap header
	 * that is acceptable by swapon.
	 */
	if (!rzs->backing_swap) {
		s->info.version = 1;
		s->info.last_page = (rzs->disksize >> PAGE_SHIFT) - 1;
		s->info.nr_badpages = 0;
		memcpy(s->magic.magic, "SWAPSPACE2", 10);
		return 0;
	}

	/*
	 * We have a backing swap device. Copy its swap header
	 * to ramzswap device header. If this header contains
	 * invalid information (backing device not a swap
	 * partition, etc.), swapon will fail for ramzswap
	 * which is correct behavior - we don't want to swap
	 * over filesystem partition!
	 */

	/* Read the backing swap header (code from sys_swapon) */
	mapping = rzs->swap_file->f_mapping;
	if (!mapping->a_ops->readpage) {
		ret = -EINVAL;
		goto out;
	}

	page = read_mapping_page(mapping, 0, rzs->swap_file);
	if (IS_ERR(page)) {
		ret = PTR_ERR(page);
		goto out;
	}

	backing_swap_header = kmap(page);
	memcpy(s, backing_swap_header, sizeof(*s));
	if (s->info.nr_badpages) {
		pr_info("Cannot use backing swap with bad pages (%u)\n",
			s->info.nr_badpages);
		ret = -EINVAL;
	}
	/*
	 * ramzswap disksize equals number of usable pages in backing
	 * swap. Set last_page in swap header to match this disksize
	 * ('last_page' means 0-based index of last usable swap page).
	 */
	s->info.last_page = (rzs->disksize >> PAGE_SHIFT) - 1;
	kunmap(page);

out:
	return ret;
}

static void ramzswap_ioctl_get_stats(struct ramzswap *rzs,
			struct ramzswap_ioctl_stats *s)
{
	strncpy(s->backing_swap_name, rzs->backing_swap_name,
		MAX_SWAP_NAME_LEN - 1);
	s->backing_swap_name[MAX_SWAP_NAME_LEN - 1] = '\0';

	s->disksize = rzs->disksize;
	s->memlimit = rzs->memlimit;

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

	s->bdev_num_reads = rzs_stat64_read(rzs, &rs->bdev_num_reads);
	s->bdev_num_writes = rzs_stat64_read(rzs, &rs->bdev_num_writes);
	}
#endif /* CONFIG_RAMZSWAP_STATS */
}

static int add_backing_swap_extent(struct ramzswap *rzs,
				pgoff_t phy_pagenum,
				pgoff_t num_pages)
{
	unsigned int idx;
	struct list_head *head;
	struct page *curr_page, *new_page;
	unsigned int extents_per_page = PAGE_SIZE /
				sizeof(struct ramzswap_backing_extent);

	idx = rzs->num_extents % extents_per_page;
	if (!idx) {
		new_page = alloc_page(__GFP_ZERO);
		if (!new_page)
			return -ENOMEM;

		if (rzs->num_extents) {
			curr_page = virt_to_page(rzs->curr_extent);
			head = &curr_page->lru;
		} else {
			head = &rzs->backing_swap_extent_list;
		}

		list_add(&new_page->lru, head);
		rzs->curr_extent = page_address(new_page);
	}

	rzs->curr_extent->phy_pagenum = phy_pagenum;
	rzs->curr_extent->num_pages = num_pages;

	pr_debug("add_extent: idx=%u, phy_pgnum=%lu, num_pgs=%lu, "
		"pg_last=%lu, curr_ext=%p\n", idx, phy_pagenum, num_pages,
		phy_pagenum + num_pages - 1, rzs->curr_extent);

	if (idx != extents_per_page - 1)
		rzs->curr_extent++;

	return 0;
}

static int setup_backing_swap_extents(struct ramzswap *rzs,
				struct inode *inode, unsigned long *num_pages)
{
	int ret = 0;
	unsigned blkbits;
	unsigned blocks_per_page;
	pgoff_t contig_pages = 0, total_pages = 0;
	pgoff_t pagenum = 0, prev_pagenum = 0;
	sector_t probe_block = 0;
	sector_t last_block;

	blkbits = inode->i_blkbits;
	blocks_per_page = PAGE_SIZE >> blkbits;

	last_block = i_size_read(inode) >> blkbits;
	while (probe_block + blocks_per_page <= last_block) {
		unsigned block_in_page;
		sector_t first_block;

		first_block = bmap(inode, probe_block);
		if (first_block == 0)
			goto bad_bmap;

		/* It must be PAGE_SIZE aligned on-disk */
		if (first_block & (blocks_per_page - 1)) {
			probe_block++;
			goto probe_next;
		}

		/* All blocks within this page must be contiguous on disk */
		for (block_in_page = 1; block_in_page < blocks_per_page;
					block_in_page++) {
			sector_t block;

			block = bmap(inode, probe_block + block_in_page);
			if (block == 0)
				goto bad_bmap;
			if (block != first_block + block_in_page) {
				/* Discontiguity */
				probe_block++;
				goto probe_next;
			}
		}

		/*
		 * We found a PAGE_SIZE length, PAGE_SIZE aligned
		 * run of blocks.
		 */
		pagenum = first_block >> (PAGE_SHIFT - blkbits);

		if (total_pages && (pagenum != prev_pagenum + 1)) {
			ret = add_backing_swap_extent(rzs, prev_pagenum -
					(contig_pages - 1), contig_pages);
			if (ret < 0)
				goto out;
			rzs->num_extents++;
			contig_pages = 0;
		}
		total_pages++;
		contig_pages++;
		prev_pagenum = pagenum;
		probe_block += blocks_per_page;

probe_next:
		continue;
	}

	if (contig_pages) {
		pr_debug("adding last extent: pagenum=%lu, "
			"contig_pages=%lu\n", pagenum, contig_pages);
		ret = add_backing_swap_extent(rzs,
			prev_pagenum - (contig_pages - 1), contig_pages);
		if (ret < 0)
			goto out;
		rzs->num_extents++;
	}
	if (!rzs->num_extents) {
		pr_err("No swap extents found!\n");
		ret = -EINVAL;
	}

	if (!ret) {
		*num_pages = total_pages;
		pr_info("Found %lu extents containing %luk\n",
			rzs->num_extents, *num_pages << (PAGE_SHIFT - 10));
	}
	goto out;

bad_bmap:
	pr_err("Backing swapfile has holes\n");
	ret = -EINVAL;
out:
	while (ret && !list_empty(&rzs->backing_swap_extent_list)) {
		struct page *page;
		struct list_head *entry = rzs->backing_swap_extent_list.next;
		page = list_entry(entry, struct page, lru);
		list_del(entry);
		__free_page(page);
	}
	return ret;
}

static void map_backing_swap_extents(struct ramzswap *rzs)
{
	struct ramzswap_backing_extent *se;
	struct page *table_page, *se_page;
	unsigned long num_pages, num_table_pages, entry;
	unsigned long se_idx, span;
	unsigned entries_per_page = PAGE_SIZE / sizeof(*rzs->table);
	unsigned extents_per_page = PAGE_SIZE / sizeof(*se);

	/* True for block device */
	if (!rzs->num_extents)
		return;

	se_page = list_entry(rzs->backing_swap_extent_list.next,
					struct page, lru);
	se = page_address(se_page);
	span = se->num_pages;
	num_pages = rzs->disksize >> PAGE_SHIFT;
	num_table_pages = DIV_ROUND_UP(num_pages * sizeof(*rzs->table),
							PAGE_SIZE);

	entry = 0;
	se_idx = 0;
	while (num_table_pages--) {
		table_page = vmalloc_to_page(&rzs->table[entry]);
		while (span <= entry) {
			se_idx++;
			if (se_idx == rzs->num_extents)
				BUG();

			if (!(se_idx % extents_per_page)) {
				se_page = list_entry(se_page->lru.next,
						struct page, lru);
				se = page_address(se_page);
			} else
				se++;

			span += se->num_pages;
		}
		table_page->mapping = (struct address_space *)se;
		table_page->private = se->num_pages - (span - entry);
		pr_debug("map_table: entry=%lu, span=%lu, map=%p, priv=%lu\n",
			entry, span, table_page->mapping, table_page->private);
		entry += entries_per_page;
	}
}

/*
 * Check if value of backing_swap module param is sane.
 * Claim this device and set ramzswap size equal to
 * size of this block device.
 */
static int setup_backing_swap(struct ramzswap *rzs)
{
	int ret = 0;
	size_t disksize;
	unsigned long num_pages = 0;
	struct inode *inode;
	struct file *swap_file;
	struct address_space *mapping;
	struct block_device *bdev = NULL;

	if (!rzs->backing_swap_name[0]) {
		pr_debug("backing_swap param not given\n");
		goto out;
	}

	pr_info("Using backing swap device: %s\n", rzs->backing_swap_name);

	swap_file = filp_open(rzs->backing_swap_name,
				O_RDWR | O_LARGEFILE, 0);
	if (IS_ERR(swap_file)) {
		pr_err("Error opening backing device: %s\n",
			rzs->backing_swap_name);
		ret = -EINVAL;
		goto out;
	}

	mapping = swap_file->f_mapping;
	inode = mapping->host;

	if (S_ISBLK(inode->i_mode)) {
		bdev = I_BDEV(inode);
		ret = bd_claim(bdev, setup_backing_swap);
		if (ret < 0) {
			bdev = NULL;
			goto bad_param;
		}
		disksize = i_size_read(inode);
		/*
		 * Can happen if user gives an extended partition as
		 * backing swap or simply a bad disk.
		 */
		if (!disksize) {
			pr_err("Error reading backing swap size.\n");
			goto bad_param;
		}
	} else if (S_ISREG(inode->i_mode)) {
		bdev = inode->i_sb->s_bdev;
		if (IS_SWAPFILE(inode)) {
			ret = -EBUSY;
			goto bad_param;
		}
		ret = setup_backing_swap_extents(rzs, inode, &num_pages);
		if (ret < 0)
			goto bad_param;
		disksize = num_pages << PAGE_SHIFT;
	} else {
		goto bad_param;
	}

	rzs->swap_file = swap_file;
	rzs->backing_swap = bdev;
	rzs->disksize = disksize;

	return 0;

bad_param:
	if (bdev)
		bd_release(bdev);
	filp_close(swap_file, NULL);

out:
	rzs->backing_swap = NULL;
	return ret;
}

/*
 * Map logical page number 'pagenum' to physical page number
 * on backing swap device. For block device, this is a nop.
 */
static u32 map_backing_swap_page(struct ramzswap *rzs, u32 pagenum)
{
	u32 skip_pages, entries_per_page;
	size_t delta, se_offset, skipped;
	struct page *table_page, *se_page;
	struct ramzswap_backing_extent *se;

	if (!rzs->num_extents)
		return pagenum;

	entries_per_page = PAGE_SIZE / sizeof(*rzs->table);

	table_page = vmalloc_to_page(&rzs->table[pagenum]);
	se = (struct ramzswap_backing_extent *)table_page->mapping;
	se_page = virt_to_page(se);

	skip_pages = pagenum - (pagenum / entries_per_page * entries_per_page);
	se_offset = table_page->private + skip_pages;

	if (se_offset < se->num_pages)
		return se->phy_pagenum + se_offset;

	skipped = se->num_pages - table_page->private;
	do {
		struct ramzswap_backing_extent *se_base;
		u32 se_entries_per_page = PAGE_SIZE / sizeof(*se);

		/* Get next swap extent */
		se_base = (struct ramzswap_backing_extent *)
						page_address(se_page);
		if (se - se_base == se_entries_per_page - 1) {
			se_page = list_entry(se_page->lru.next,
						struct page, lru);
			se = page_address(se_page);
		} else {
			se++;
		}

		skipped += se->num_pages;
	} while (skipped < skip_pages);

	delta = skipped - skip_pages;
	se_offset = se->num_pages - delta;

	return se->phy_pagenum + se_offset;
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
 * Its either in backing swap device (if present) or
 * this is an attempt to read before any previous write
 * to this location - this happens due to readahead when
 * swap device is read from user-space (e.g. during swapon)
 */
static int handle_ramzswap_fault(struct ramzswap *rzs, struct bio *bio)
{
	/*
	 * Always forward such requests to backing swap
	 * device (if present)
	 */
	if (rzs->backing_swap) {
		u32 pagenum;
		rzs_stat64_dec(rzs, &rzs->stats.num_reads);
		rzs_stat64_inc(rzs, &rzs->stats.bdev_num_reads);
		bio->bi_bdev = rzs->backing_swap;

		/*
		 * In case backing swap is a file, find the right offset within
		 * the file corresponding to logical position 'index'. For block
		 * device, this is a nop.
		 */
		pagenum = bio->bi_sector >> SECTORS_PER_PAGE_SHIFT;
		bio->bi_sector = map_backing_swap_page(rzs, pagenum)
					<< SECTORS_PER_PAGE_SHIFT;
		return 1;
	}

	/*
	 * Its unlikely event in case backing dev is
	 * not present
	 */
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
	int ret, fwd_write_request = 0;
	u32 offset, index;
	size_t clen;
	struct zobj_header *zheader;
	struct page *page, *page_store;
	unsigned char *user_mem, *cmem, *src;

	rzs_stat64_inc(rzs, &rzs->stats.num_writes);

	page = bio->bi_io_vec[0].bv_page;
	index = bio->bi_sector >> SECTORS_PER_PAGE_SHIFT;

	src = rzs->compress_buffer;

	/*
	 * System swaps to same sector again when the stored page
	 * is no longer referenced by any process. So, its now safe
	 * to free the memory that was allocated for this page.
	 */
	if (rzs->table[index].page || rzs_test_flag(rzs, index, RZS_ZERO))
		ramzswap_free_page(rzs, index);

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

	if (rzs->backing_swap &&
		(rzs->stats.compr_size > rzs->memlimit - PAGE_SIZE)) {
		kunmap_atomic(user_mem, KM_USER0);
		mutex_unlock(&rzs->lock);
		fwd_write_request = 1;
		goto out;
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
	 * Page is incompressible. Forward it to backing swap
	 * if present. Otherwise, store it as-is (uncompressed)
	 * since we do not want to return too many swap write
	 * errors which has side effect of hanging the system.
	 */
	if (unlikely(clen > max_zpage_size)) {
		if (rzs->backing_swap) {
			mutex_unlock(&rzs->lock);
			fwd_write_request = 1;
			goto out;
		}

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
		if (rzs->backing_swap)
			fwd_write_request = 1;
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
	if (fwd_write_request) {
		rzs_stat64_inc(rzs, &rzs->stats.bdev_num_writes);
		bio->bi_bdev = rzs->backing_swap;
#if 0
		/*
		 * TODO: We currently have linear mapping of ramzswap and
		 * backing swap sectors. This is not desired since we want
		 * to optimize writes to backing swap to minimize disk seeks
		 * or have effective wear leveling (for SSDs). Also, a
		 * non-linear mapping is required to implement compressed
		 * on-disk swapping.
		 */
		 bio->bi_sector = get_backing_swap_page()
					<< SECTORS_PER_PAGE_SHIFT;
#endif
		/*
		 * In case backing swap is a file, find the right offset within
		 * the file corresponding to logical position 'index'. For block
		 * device, this is a nop.
		 */
		bio->bi_sector = map_backing_swap_page(rzs, index)
					<< SECTORS_PER_PAGE_SHIFT;
		return 1;
	}

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
	int is_backing_blkdev = 0;
	size_t index, num_pages;
	unsigned entries_per_page;
	unsigned long num_table_pages, entry = 0;

	/* Do not accept any new I/O request */
	rzs->init_done = 0;

	if (rzs->backing_swap && !rzs->num_extents)
		is_backing_blkdev = 1;

	num_pages = rzs->disksize >> PAGE_SHIFT;

	/* Free various per-device buffers */
	kfree(rzs->compress_workmem);
	free_pages((unsigned long)rzs->compress_buffer, 1);

	rzs->compress_workmem = NULL;
	rzs->compress_buffer = NULL;

	/* Free all pages that are still in this ramzswap device */
	for (index = 0; index < num_pages; index++) {
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

	entries_per_page = PAGE_SIZE / sizeof(*rzs->table);
	num_table_pages = DIV_ROUND_UP(num_pages * sizeof(*rzs->table),
					PAGE_SIZE);
	/*
	 * Set page->mapping to NULL for every table page.
	 * Otherwise, we will hit bad_page() during free.
	 */
	while (rzs->num_extents && num_table_pages--) {
		struct page *page;
		page = vmalloc_to_page(&rzs->table[entry]);
		page->mapping = NULL;
		entry += entries_per_page;
	}
	vfree(rzs->table);
	rzs->table = NULL;

	xv_destroy_pool(rzs->mem_pool);
	rzs->mem_pool = NULL;

	/* Free all swap extent pages */
	while (!list_empty(&rzs->backing_swap_extent_list)) {
		struct page *page;
		struct list_head *entry;
		entry = rzs->backing_swap_extent_list.next;
		page = list_entry(entry, struct page, lru);
		list_del(entry);
		__free_page(page);
	}
	INIT_LIST_HEAD(&rzs->backing_swap_extent_list);
	rzs->num_extents = 0;

	/* Close backing swap device, if present */
	if (rzs->backing_swap) {
		if (is_backing_blkdev)
			bd_release(rzs->backing_swap);
		filp_close(rzs->swap_file, NULL);
		rzs->backing_swap = NULL;
		memset(rzs->backing_swap_name, 0, MAX_SWAP_NAME_LEN);
	}

	/* Reset stats */
	memset(&rzs->stats, 0, sizeof(rzs->stats));

	rzs->disksize = 0;
	rzs->memlimit = 0;
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

	ret = setup_backing_swap(rzs);
	if (ret)
		goto fail;

	if (rzs->backing_swap)
		ramzswap_set_memlimit(rzs, totalram_pages << PAGE_SHIFT);
	else
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

	map_backing_swap_extents(rzs);

	page = alloc_page(__GFP_ZERO);
	if (!page) {
		pr_err("Error allocating swap header page\n");
		ret = -ENOMEM;
		goto fail;
	}
	rzs->table[0].page = page;
	rzs_set_flag(rzs, 0, RZS_UNCOMPRESSED);

	swap_header = kmap(page);
	ret = setup_swap_header(rzs, swap_header);
	kunmap(page);
	if (ret) {
		pr_err("Error setting swap header\n");
		goto fail;
	}

	set_capacity(rzs->disk, rzs->disksize >> SECTOR_SHIFT);

	/*
	 * We have ident mapping of sectors for ramzswap and
	 * and the backing swap device. So, this queue flag
	 * should be according to backing dev.
	 */
	if (!rzs->backing_swap ||
			blk_queue_nonrot(rzs->backing_swap->bd_disk->queue))
		queue_flag_set_unlocked(QUEUE_FLAG_NONROT, rzs->disk->queue);

	rzs->mem_pool = xv_create_pool();
	if (!rzs->mem_pool) {
		pr_err("Error creating memory pool\n");
		ret = -ENOMEM;
		goto fail;
	}

	/*
	 * Pages that compress to size greater than this are forwarded
	 * to physical swap disk (if backing dev is provided)
	 * TODO: make this configurable
	 */
	if (rzs->backing_swap)
		max_zpage_size = max_zpage_size_bdev;
	else
		max_zpage_size = max_zpage_size_nobdev;
	pr_debug("Max compressed page size: %u bytes\n", max_zpage_size);

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
	size_t disksize_kb, memlimit_kb;

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

	case RZSIO_SET_MEMLIMIT_KB:
		if (rzs->init_done) {
			/* TODO: allow changing memlimit */
			ret = -EBUSY;
			goto out;
		}
		if (copy_from_user(&memlimit_kb, (void *)arg,
						_IOC_SIZE(cmd))) {
			ret = -EFAULT;
			goto out;
		}
		rzs->memlimit = memlimit_kb << 10;
		pr_info("Memory limit set to %zu kB\n", memlimit_kb);
		break;

	case RZSIO_SET_BACKING_SWAP:
		if (rzs->init_done) {
			ret = -EBUSY;
			goto out;
		}

		if (copy_from_user(&rzs->backing_swap_name, (void *)arg,
						_IOC_SIZE(cmd))) {
			ret = -EFAULT;
			goto out;
		}
		rzs->backing_swap_name[MAX_SWAP_NAME_LEN - 1] = '\0';
		pr_info("Backing swap set to %s\n", rzs->backing_swap_name);
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

static struct block_device_operations ramzswap_devops = {
	.ioctl = ramzswap_ioctl,
	.owner = THIS_MODULE,
};

static int create_device(struct ramzswap *rzs, int device_id)
{
	int ret = 0;

	mutex_init(&rzs->lock);
	spin_lock_init(&rzs->stat64_lock);
	INIT_LIST_HEAD(&rzs->backing_swap_extent_list);

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

	/*
	 * Actual capacity set using RZSIO_SET_DISKSIZE_KB ioctl
	 * or set equal to backing swap device (if provided)
	 */
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
