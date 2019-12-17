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
#include <linux/memblock.h>
#include <linux/printk.h>
#include <asm/tlbflush.h>

#include <trace/events/block.h>
#include "blk.h"

#define POOL_SIZE	64
#define ISA_POOL_SIZE	16

static struct bio_set bounce_bio_set, bounce_bio_split;
static mempool_t page_pool, isa_page_pool;

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

#if defined(CONFIG_HIGHMEM)
static __init int init_emergency_pool(void)
{
	int ret;
#if defined(CONFIG_HIGHMEM) && !defined(CONFIG_MEMORY_HOTPLUG)
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
#endif

#ifdef CONFIG_HIGHMEM
/*
 * highmem version, map in to vec
 */
static void bounce_copy_vec(struct bio_vec *to, unsigned char *vfrom)
{
	unsigned char *vto;

	vto = kmap_atomic(to->bv_page);
	memcpy(vto + to->bv_offset, vfrom, to->bv_len);
	kunmap_atomic(vto);
}

#else /* CONFIG_HIGHMEM */

#define bounce_copy_vec(to, vfrom)	\
	memcpy(page_address((to)->bv_page) + (to)->bv_offset, vfrom, (to)->bv_len)

#endif /* CONFIG_HIGHMEM */

/*
 * allocate pages in the DMA region for the ISA pool
 */
static void *mempool_alloc_pages_isa(gfp_t gfp_mask, void *data)
{
	return mempool_alloc_pages(gfp_mask | GFP_DMA, data);
}

static DEFINE_MUTEX(isa_mutex);

/*
 * gets called "every" time someone init's a queue with BLK_BOUNCE_ISA
 * as the max address, so check if the pool has already been created.
 */
int init_emergency_isa_pool(void)
{
	int ret;

	mutex_lock(&isa_mutex);

	if (mempool_initialized(&isa_page_pool)) {
		mutex_unlock(&isa_mutex);
		return 0;
	}

	ret = mempool_init(&isa_page_pool, ISA_POOL_SIZE, mempool_alloc_pages_isa,
			   mempool_free_pages, (void *) 0);
	BUG_ON(ret);

	pr_info("isa pool size: %d pages\n", ISA_POOL_SIZE);
	init_bounce_bioset();
	mutex_unlock(&isa_mutex);
	return 0;
}

/*
 * Simple bounce buffer support for highmem pages. Depending on the
 * queue gfp mask set, *to may or may not be a highmem page. kmap it
 * always, it will do the Right Thing
 */
static void copy_to_high_bio_irq(struct bio *to, struct bio *from)
{
	unsigned char *vfrom;
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
			vfrom = page_address(fromvec.bv_page) +
				tovec.bv_offset;

			bounce_copy_vec(&tovec, vfrom);
			flush_dcache_page(tovec.bv_page);
		}
		bio_advance_iter(from, &from_iter, tovec.bv_len);
	}
}

static void bounce_end_io(struct bio *bio, mempool_t *pool)
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
			mempool_free(bvec->bv_page, pool);
		}
		bio_advance_iter(bio_orig, &orig_iter, orig_vec.bv_len);
	}

	bio_orig->bi_status = bio->bi_status;
	bio_endio(bio_orig);
	bio_put(bio);
}

static void bounce_end_io_write(struct bio *bio)
{
	bounce_end_io(bio, &page_pool);
}

static void bounce_end_io_write_isa(struct bio *bio)
{

	bounce_end_io(bio, &isa_page_pool);
}

static void __bounce_end_io_read(struct bio *bio, mempool_t *pool)
{
	struct bio *bio_orig = bio->bi_private;

	if (!bio->bi_status)
		copy_to_high_bio_irq(bio_orig, bio);

	bounce_end_io(bio, pool);
}

static void bounce_end_io_read(struct bio *bio)
{
	__bounce_end_io_read(bio, &page_pool);
}

static void bounce_end_io_read_isa(struct bio *bio)
{
	__bounce_end_io_read(bio, &isa_page_pool);
}

static struct bio *bounce_clone_bio(struct bio *bio_src, gfp_t gfp_mask,
		struct bio_set *bs)
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
	 *  - The original bio could've had more than BIO_MAX_PAGES biovecs; if
	 *    we tried to clone the whole thing bio_alloc_bioset() would fail.
	 *    But the clone should succeed as long as the number of biovecs we
	 *    actually need to allocate is fewer than BIO_MAX_PAGES.
	 *
	 *  - Lastly, bi_vcnt should not be looked at or relied upon by code
	 *    that does not own the bio - reason being drivers don't use it for
	 *    iterating over the biovec anymore, so expecting it to be kept up
	 *    to date (i.e. for clones that share the parent biovec) is just
	 *    asking for trouble and would force extra work on
	 *    __bio_clone_fast() anyways.
	 */

	bio = bio_alloc_bioset(gfp_mask, bio_segments(bio_src), bs);
	if (!bio)
		return NULL;
	bio->bi_disk		= bio_src->bi_disk;
	bio->bi_opf		= bio_src->bi_opf;
	bio->bi_ioprio		= bio_src->bi_ioprio;
	bio->bi_write_hint	= bio_src->bi_write_hint;
	bio->bi_iter.bi_sector	= bio_src->bi_iter.bi_sector;
	bio->bi_iter.bi_size	= bio_src->bi_iter.bi_size;

	switch (bio_op(bio)) {
	case REQ_OP_DISCARD:
	case REQ_OP_SECURE_ERASE:
	case REQ_OP_WRITE_ZEROES:
		break;
	case REQ_OP_WRITE_SAME:
		bio->bi_io_vec[bio->bi_vcnt++] = bio_src->bi_io_vec[0];
		break;
	default:
		bio_for_each_segment(bv, bio_src, iter)
			bio->bi_io_vec[bio->bi_vcnt++] = bv;
		break;
	}

	bio_crypt_clone(bio, bio_src, gfp_mask);

	if (bio_integrity(bio_src) &&
	    bio_integrity_clone(bio, bio_src, gfp_mask) < 0) {
		bio_put(bio);
		return NULL;
	}

	bio_clone_blkg_association(bio, bio_src);
	blkcg_bio_issue_init(bio);

	return bio;
}

static void __blk_queue_bounce(struct request_queue *q, struct bio **bio_orig,
			       mempool_t *pool)
{
	struct bio *bio;
	int rw = bio_data_dir(*bio_orig);
	struct bio_vec *to, from;
	struct bvec_iter iter;
	unsigned i = 0;
	bool bounce = false;
	int sectors = 0;
	bool passthrough = bio_is_passthrough(*bio_orig);

	bio_for_each_segment(from, *bio_orig, iter) {
		if (i++ < BIO_MAX_PAGES)
			sectors += from.bv_len >> 9;
		if (page_to_pfn(from.bv_page) > q->limits.bounce_pfn)
			bounce = true;
	}
	if (!bounce)
		return;

	if (!passthrough && sectors < bio_sectors(*bio_orig)) {
		bio = bio_split(*bio_orig, sectors, GFP_NOIO, &bounce_bio_split);
		bio_chain(bio, *bio_orig);
		generic_make_request(*bio_orig);
		*bio_orig = bio;
	}
	bio = bounce_clone_bio(*bio_orig, GFP_NOIO, passthrough ? NULL :
			&bounce_bio_set);

	/*
	 * Bvec table can't be updated by bio_for_each_segment_all(),
	 * so retrieve bvec from the table directly. This way is safe
	 * because the 'bio' is single-page bvec.
	 */
	for (i = 0, to = bio->bi_io_vec; i < bio->bi_vcnt; to++, i++) {
		struct page *page = to->bv_page;

		if (page_to_pfn(page) <= q->limits.bounce_pfn)
			continue;

		to->bv_page = mempool_alloc(pool, q->bounce_gfp);
		inc_zone_page_state(to->bv_page, NR_BOUNCE);

		if (rw == WRITE) {
			char *vto, *vfrom;

			flush_dcache_page(page);

			vto = page_address(to->bv_page) + to->bv_offset;
			vfrom = kmap_atomic(page) + to->bv_offset;
			memcpy(vto, vfrom, to->bv_len);
			kunmap_atomic(vfrom);
		}
	}

	trace_block_bio_bounce(q, *bio_orig);

	bio->bi_flags |= (1 << BIO_BOUNCED);

	if (pool == &page_pool) {
		bio->bi_end_io = bounce_end_io_write;
		if (rw == READ)
			bio->bi_end_io = bounce_end_io_read;
	} else {
		bio->bi_end_io = bounce_end_io_write_isa;
		if (rw == READ)
			bio->bi_end_io = bounce_end_io_read_isa;
	}

	bio->bi_private = *bio_orig;
	*bio_orig = bio;
}

void blk_queue_bounce(struct request_queue *q, struct bio **bio_orig)
{
	mempool_t *pool;

	/*
	 * Data-less bio, nothing to bounce
	 */
	if (!bio_has_data(*bio_orig))
		return;

	/*
	 * for non-isa bounce case, just check if the bounce pfn is equal
	 * to or bigger than the highest pfn in the system -- in that case,
	 * don't waste time iterating over bio segments
	 */
	if (!(q->bounce_gfp & GFP_DMA)) {
		if (q->limits.bounce_pfn >= blk_max_pfn)
			return;
		pool = &page_pool;
	} else {
		BUG_ON(!mempool_initialized(&isa_page_pool));
		pool = &isa_page_pool;
	}

	/*
	 * slow path
	 */
	__blk_queue_bounce(q, bio_orig, pool);
}
