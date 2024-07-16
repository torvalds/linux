// SPDX-License-Identifier: GPL-2.0
/* bounce buffer handling for block devices
 *
 * - Split from highmem.c
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/mm.h>
#include <linux/export.h>
#include <linux/swap.h>
#include <linux/gfp.h>
#include <linux/bio.h>
#include <linux/pagemap.h>
#include <linux/mempool.h>
#include <linux/blkdev.h>
#include <linux/backing-dev.h>
#include <linux/init.h>
#include <linux/hash.h>
#include <linux/highmem.h>
#include <linux/printk.h>
#include <asm/tlbflush.h>

#include <trace/events/block.h>
#include "blk.h"
#include "blk-cgroup.h"

#define POOL_SIZE	64
#define ISA_POOL_SIZE	16

static struct bio_set bounce_bio_set, bounce_bio_split;
static mempool_t page_pool;

static void init_bounce_bioset(void)
{
	static bool bounce_bs_setup;
	int ret;

	if (bounce_bs_setup)
		return;

	ret = bioset_init(&bounce_bio_set, BIO_POOL_SIZE, 0, BIOSET_NEED_BVECS);
	BUG_ON(ret);
	if (bioset_integrity_create(&bounce_bio_set, BIO_POOL_SIZE))
		BUG_ON(1);

	ret = bioset_init(&bounce_bio_split, BIO_POOL_SIZE, 0, 0);
	BUG_ON(ret);
	bounce_bs_setup = true;
}

static __init int init_emergency_pool(void)
{
	int ret;

#ifndef CONFIG_MEMORY_HOTPLUG
	if (max_pfn <= max_low_pfn)
		return 0;
#endif

	ret = mempool_init_page_pool(&page_pool, POOL_SIZE, 0);
	BUG_ON(ret);
	pr_info("pool size: %d pages\n", POOL_SIZE);

	init_bounce_bioset();
	return 0;
}

__initcall(init_emergency_pool);

/*
 * Simple bounce buffer support for highmem pages. Depending on the
 * queue gfp mask set, *to may or may not be a highmem page. kmap it
 * always, it will do the Right Thing
 */
static void copy_to_high_bio_irq(struct bio *to, struct bio *from)
{
	struct bio_vec tovec, fromvec;
	struct bvec_iter iter;
	/*
	 * The bio of @from is created by bounce, so we can iterate
	 * its bvec from start to end, but the @from->bi_iter can't be
	 * trusted because it might be changed by splitting.
	 */
	struct bvec_iter from_iter = BVEC_ITER_ALL_INIT;

	bio_for_each_segment(tovec, to, iter) {
		fromvec = bio_iter_iovec(from, from_iter);
		if (tovec.bv_page != fromvec.bv_page) {
			/*
			 * fromvec->bv_offset and fromvec->bv_len might have
			 * been modified by the block layer, so use the original
			 * copy, bounce_copy_vec already uses tovec->bv_len
			 */
			memcpy_to_bvec(&tovec, page_address(fromvec.bv_page) +
				       tovec.bv_offset);
		}
		bio_advance_iter(from, &from_iter, tovec.bv_len);
	}
}

static void bounce_end_io(struct bio *bio)
{
	struct bio *bio_orig = bio->bi_private;
	struct bio_vec *bvec, orig_vec;
	struct bvec_iter orig_iter = bio_orig->bi_iter;
	struct bvec_iter_all iter_all;

	/*
	 * free up bounce indirect pages used
	 */
	bio_for_each_segment_all(bvec, bio, iter_all) {
		orig_vec = bio_iter_iovec(bio_orig, orig_iter);
		if (bvec->bv_page != orig_vec.bv_page) {
			dec_zone_page_state(bvec->bv_page, NR_BOUNCE);
			mempool_free(bvec->bv_page, &page_pool);
		}
		bio_advance_iter(bio_orig, &orig_iter, orig_vec.bv_len);
	}

	bio_orig->bi_status = bio->bi_status;
	bio_endio(bio_orig);
	bio_put(bio);
}

static void bounce_end_io_write(struct bio *bio)
{
	bounce_end_io(bio);
}

static void bounce_end_io_read(struct bio *bio)
{
	struct bio *bio_orig = bio->bi_private;

	if (!bio->bi_status)
		copy_to_high_bio_irq(bio_orig, bio);

	bounce_end_io(bio);
}

static struct bio *bounce_clone_bio(struct bio *bio_src)
{
	struct bvec_iter iter;
	struct bio_vec bv;
	struct bio *bio;

	/*
	 * Pre immutable biovecs, __bio_clone() used to just do a memcpy from
	 * bio_src->bi_io_vec to bio->bi_io_vec.
	 *
	 * We can't do that anymore, because:
	 *
	 *  - The point of cloning the biovec is to produce a bio with a biovec
	 *    the caller can modify: bi_idx and bi_bvec_done should be 0.
	 *
	 *  - The original bio could've had more than BIO_MAX_VECS biovecs; if
	 *    we tried to clone the whole thing bio_alloc_bioset() would fail.
	 *    But the clone should succeed as long as the number of biovecs we
	 *    actually need to allocate is fewer than BIO_MAX_VECS.
	 *
	 *  - Lastly, bi_vcnt should not be looked at or relied upon by code
	 *    that does not own the bio - reason being drivers don't use it for
	 *    iterating over the biovec anymore, so expecting it to be kept up
	 *    to date (i.e. for clones that share the parent biovec) is just
	 *    asking for trouble and would force extra work.
	 */
	bio = bio_alloc_bioset(bio_src->bi_bdev, bio_segments(bio_src),
			       bio_src->bi_opf, GFP_NOIO, &bounce_bio_set);
	if (bio_flagged(bio_src, BIO_REMAPPED))
		bio_set_flag(bio, BIO_REMAPPED);
	bio->bi_ioprio		= bio_src->bi_ioprio;
	bio->bi_iter.bi_sector	= bio_src->bi_iter.bi_sector;
	bio->bi_iter.bi_size	= bio_src->bi_iter.bi_size;

	switch (bio_op(bio)) {
	case REQ_OP_DISCARD:
	case REQ_OP_SECURE_ERASE:
	case REQ_OP_WRITE_ZEROES:
		break;
	default:
		bio_for_each_segment(bv, bio_src, iter)
			bio->bi_io_vec[bio->bi_vcnt++] = bv;
		break;
	}

	if (bio_crypt_clone(bio, bio_src, GFP_NOIO) < 0)
		goto err_put;

	if (bio_integrity(bio_src) &&
	    bio_integrity_clone(bio, bio_src, GFP_NOIO) < 0)
		goto err_put;

	bio_clone_blkg_association(bio, bio_src);

	return bio;

err_put:
	bio_put(bio);
	return NULL;
}

struct bio *__blk_queue_bounce(struct bio *bio_orig, struct request_queue *q)
{
	struct bio *bio;
	int rw = bio_data_dir(bio_orig);
	struct bio_vec *to, from;
	struct bvec_iter iter;
	unsigned i = 0, bytes = 0;
	bool bounce = false;
	int sectors;

	bio_for_each_segment(from, bio_orig, iter) {
		if (i++ < BIO_MAX_VECS)
			bytes += from.bv_len;
		if (PageHighMem(from.bv_page))
			bounce = true;
	}
	if (!bounce)
		return bio_orig;

	/*
	 * Individual bvecs might not be logical block aligned. Round down
	 * the split size so that each bio is properly block size aligned,
	 * even if we do not use the full hardware limits.
	 */
	sectors = ALIGN_DOWN(bytes, queue_logical_block_size(q)) >>
			SECTOR_SHIFT;
	if (sectors < bio_sectors(bio_orig)) {
		bio = bio_split(bio_orig, sectors, GFP_NOIO, &bounce_bio_split);
		bio_chain(bio, bio_orig);
		submit_bio_noacct(bio_orig);
		bio_orig = bio;
	}
	bio = bounce_clone_bio(bio_orig);

	/*
	 * Bvec table can't be updated by bio_for_each_segment_all(),
	 * so retrieve bvec from the table directly. This way is safe
	 * because the 'bio' is single-page bvec.
	 */
	for (i = 0, to = bio->bi_io_vec; i < bio->bi_vcnt; to++, i++) {
		struct page *bounce_page;

		if (!PageHighMem(to->bv_page))
			continue;

		bounce_page = mempool_alloc(&page_pool, GFP_NOIO);
		inc_zone_page_state(bounce_page, NR_BOUNCE);

		if (rw == WRITE) {
			flush_dcache_page(to->bv_page);
			memcpy_from_bvec(page_address(bounce_page), to);
		}
		to->bv_page = bounce_page;
	}

	trace_block_bio_bounce(bio_orig);

	bio->bi_flags |= (1 << BIO_BOUNCED);

	if (rw == READ)
		bio->bi_end_io = bounce_end_io_read;
	else
		bio->bi_end_io = bounce_end_io_write;

	bio->bi_private = bio_orig;
	return bio;
}
