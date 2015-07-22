/*
 * Copyright (C) 2001 Jens Axboe <axboe@kernel.dk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public Licens
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-
 *
 */
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/uio.h>
#include <linux/iocontext.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/mempool.h>
#include <linux/workqueue.h>
#include <linux/cgroup.h>

#include <trace/events/block.h>

/*
 * Test patch to inline a certain number of bi_io_vec's inside the bio
 * itself, to shrink a bio data allocation from two mempool calls to one
 */
#define BIO_INLINE_VECS		4

/*
 * if you change this list, also change bvec_alloc or things will
 * break badly! cannot be bigger than what you can fit into an
 * unsigned short
 */
#define BV(x) { .nr_vecs = x, .name = "biovec-"__stringify(x) }
static struct biovec_slab bvec_slabs[BIOVEC_NR_POOLS] __read_mostly = {
	BV(1), BV(4), BV(16), BV(64), BV(128), BV(BIO_MAX_PAGES),
};
#undef BV

/*
 * fs_bio_set is the bio_set containing bio and iovec memory pools used by
 * IO code that does not need private memory pools.
 */
struct bio_set *fs_bio_set;
EXPORT_SYMBOL(fs_bio_set);

/*
 * Our slab pool management
 */
struct bio_slab {
	struct kmem_cache *slab;
	unsigned int slab_ref;
	unsigned int slab_size;
	char name[8];
};
static DEFINE_MUTEX(bio_slab_lock);
static struct bio_slab *bio_slabs;
static unsigned int bio_slab_nr, bio_slab_max;

static struct kmem_cache *bio_find_or_create_slab(unsigned int extra_size)
{
	unsigned int sz = sizeof(struct bio) + extra_size;
	struct kmem_cache *slab = NULL;
	struct bio_slab *bslab, *new_bio_slabs;
	unsigned int new_bio_slab_max;
	unsigned int i, entry = -1;

	mutex_lock(&bio_slab_lock);

	i = 0;
	while (i < bio_slab_nr) {
		bslab = &bio_slabs[i];

		if (!bslab->slab && entry == -1)
			entry = i;
		else if (bslab->slab_size == sz) {
			slab = bslab->slab;
			bslab->slab_ref++;
			break;
		}
		i++;
	}

	if (slab)
		goto out_unlock;

	if (bio_slab_nr == bio_slab_max && entry == -1) {
		new_bio_slab_max = bio_slab_max << 1;
		new_bio_slabs = krealloc(bio_slabs,
					 new_bio_slab_max * sizeof(struct bio_slab),
					 GFP_KERNEL);
		if (!new_bio_slabs)
			goto out_unlock;
		bio_slab_max = new_bio_slab_max;
		bio_slabs = new_bio_slabs;
	}
	if (entry == -1)
		entry = bio_slab_nr++;

	bslab = &bio_slabs[entry];

	snprintf(bslab->name, sizeof(bslab->name), "bio-%d", entry);
	slab = kmem_cache_create(bslab->name, sz, ARCH_KMALLOC_MINALIGN,
				 SLAB_HWCACHE_ALIGN, NULL);
	if (!slab)
		goto out_unlock;

	bslab->slab = slab;
	bslab->slab_ref = 1;
	bslab->slab_size = sz;
out_unlock:
	mutex_unlock(&bio_slab_lock);
	return slab;
}

static void bio_put_slab(struct bio_set *bs)
{
	struct bio_slab *bslab = NULL;
	unsigned int i;

	mutex_lock(&bio_slab_lock);

	for (i = 0; i < bio_slab_nr; i++) {
		if (bs->bio_slab == bio_slabs[i].slab) {
			bslab = &bio_slabs[i];
			break;
		}
	}

	if (WARN(!bslab, KERN_ERR "bio: unable to find slab!\n"))
		goto out;

	WARN_ON(!bslab->slab_ref);

	if (--bslab->slab_ref)
		goto out;

	kmem_cache_destroy(bslab->slab);
	bslab->slab = NULL;

out:
	mutex_unlock(&bio_slab_lock);
}

unsigned int bvec_nr_vecs(unsigned short idx)
{
	return bvec_slabs[idx].nr_vecs;
}

void bvec_free(mempool_t *pool, struct bio_vec *bv, unsigned int idx)
{
	BIO_BUG_ON(idx >= BIOVEC_NR_POOLS);

	if (idx == BIOVEC_MAX_IDX)
		mempool_free(bv, pool);
	else {
		struct biovec_slab *bvs = bvec_slabs + idx;

		kmem_cache_free(bvs->slab, bv);
	}
}

struct bio_vec *bvec_alloc(gfp_t gfp_mask, int nr, unsigned long *idx,
			   mempool_t *pool)
{
	struct bio_vec *bvl;

	/*
	 * see comment near bvec_array define!
	 */
	switch (nr) {
	case 1:
		*idx = 0;
		break;
	case 2 ... 4:
		*idx = 1;
		break;
	case 5 ... 16:
		*idx = 2;
		break;
	case 17 ... 64:
		*idx = 3;
		break;
	case 65 ... 128:
		*idx = 4;
		break;
	case 129 ... BIO_MAX_PAGES:
		*idx = 5;
		break;
	default:
		return NULL;
	}

	/*
	 * idx now points to the pool we want to allocate from. only the
	 * 1-vec entry pool is mempool backed.
	 */
	if (*idx == BIOVEC_MAX_IDX) {
fallback:
		bvl = mempool_alloc(pool, gfp_mask);
	} else {
		struct biovec_slab *bvs = bvec_slabs + *idx;
		gfp_t __gfp_mask = gfp_mask & ~(__GFP_WAIT | __GFP_IO);

		/*
		 * Make this allocation restricted and don't dump info on
		 * allocation failures, since we'll fallback to the mempool
		 * in case of failure.
		 */
		__gfp_mask |= __GFP_NOMEMALLOC | __GFP_NORETRY | __GFP_NOWARN;

		/*
		 * Try a slab allocation. If this fails and __GFP_WAIT
		 * is set, retry with the 1-entry mempool
		 */
		bvl = kmem_cache_alloc(bvs->slab, __gfp_mask);
		if (unlikely(!bvl && (gfp_mask & __GFP_WAIT))) {
			*idx = BIOVEC_MAX_IDX;
			goto fallback;
		}
	}

	return bvl;
}

static void __bio_free(struct bio *bio)
{
	bio_disassociate_task(bio);

	if (bio_integrity(bio))
		bio_integrity_free(bio);
}

static void bio_free(struct bio *bio)
{
	struct bio_set *bs = bio->bi_pool;
	void *p;

	__bio_free(bio);

	if (bs) {
		if (bio_flagged(bio, BIO_OWNS_VEC))
			bvec_free(bs->bvec_pool, bio->bi_io_vec, BIO_POOL_IDX(bio));

		/*
		 * If we have front padding, adjust the bio pointer before freeing
		 */
		p = bio;
		p -= bs->front_pad;

		mempool_free(p, bs->bio_pool);
	} else {
		/* Bio was allocated by bio_kmalloc() */
		kfree(bio);
	}
}

void bio_init(struct bio *bio)
{
	memset(bio, 0, sizeof(*bio));
	bio->bi_flags = 1 << BIO_UPTODATE;
	atomic_set(&bio->bi_remaining, 1);
	atomic_set(&bio->bi_cnt, 1);
}
EXPORT_SYMBOL(bio_init);

/**
 * bio_reset - reinitialize a bio
 * @bio:	bio to reset
 *
 * Description:
 *   After calling bio_reset(), @bio will be in the same state as a freshly
 *   allocated bio returned bio bio_alloc_bioset() - the only fields that are
 *   preserved are the ones that are initialized by bio_alloc_bioset(). See
 *   comment in struct bio.
 */
void bio_reset(struct bio *bio)
{
	unsigned long flags = bio->bi_flags & (~0UL << BIO_RESET_BITS);

	__bio_free(bio);

	memset(bio, 0, BIO_RESET_BYTES);
	bio->bi_flags = flags|(1 << BIO_UPTODATE);
	atomic_set(&bio->bi_remaining, 1);
}
EXPORT_SYMBOL(bio_reset);

static void bio_chain_endio(struct bio *bio, int error)
{
	bio_endio(bio->bi_private, error);
	bio_put(bio);
}

/**
 * bio_chain - chain bio completions
 * @bio: the target bio
 * @parent: the @bio's parent bio
 *
 * The caller won't have a bi_end_io called when @bio completes - instead,
 * @parent's bi_end_io won't be called until both @parent and @bio have
 * completed; the chained bio will also be freed when it completes.
 *
 * The caller must not set bi_private or bi_end_io in @bio.
 */
void bio_chain(struct bio *bio, struct bio *parent)
{
	BUG_ON(bio->bi_private || bio->bi_end_io);

	bio->bi_private = parent;
	bio->bi_end_io	= bio_chain_endio;
	atomic_inc(&parent->bi_remaining);
}
EXPORT_SYMBOL(bio_chain);

static void bio_alloc_rescue(struct work_struct *work)
{
	struct bio_set *bs = container_of(work, struct bio_set, rescue_work);
	struct bio *bio;

	while (1) {
		spin_lock(&bs->rescue_lock);
		bio = bio_list_pop(&bs->rescue_list);
		spin_unlock(&bs->rescue_lock);

		if (!bio)
			break;

		generic_make_request(bio);
	}
}

static void punt_bios_to_rescuer(struct bio_set *bs)
{
	struct bio_list punt, nopunt;
	struct bio *bio;

	/*
	 * In order to guarantee forward progress we must punt only bios that
	 * were allocated from this bio_set; otherwise, if there was a bio on
	 * there for a stacking driver higher up in the stack, processing it
	 * could require allocating bios from this bio_set, and doing that from
	 * our own rescuer would be bad.
	 *
	 * Since bio lists are singly linked, pop them all instead of trying to
	 * remove from the middle of the list:
	 */

	bio_list_init(&punt);
	bio_list_init(&nopunt);

	while ((bio = bio_list_pop(current->bio_list)))
		bio_list_add(bio->bi_pool == bs ? &punt : &nopunt, bio);

	*current->bio_list = nopunt;

	spin_lock(&bs->rescue_lock);
	bio_list_merge(&bs->rescue_list, &punt);
	spin_unlock(&bs->rescue_lock);

	queue_work(bs->rescue_workqueue, &bs->rescue_work);
}

/**
 * bio_alloc_bioset - allocate a bio for I/O
 * @gfp_mask:   the GFP_ mask given to the slab allocator
 * @nr_iovecs:	number of iovecs to pre-allocate
 * @bs:		the bio_set to allocate from.
 *
 * Description:
 *   If @bs is NULL, uses kmalloc() to allocate the bio; else the allocation is
 *   backed by the @bs's mempool.
 *
 *   When @bs is not NULL, if %__GFP_WAIT is set then bio_alloc will always be
 *   able to allocate a bio. This is due to the mempool guarantees. To make this
 *   work, callers must never allocate more than 1 bio at a time from this pool.
 *   Callers that need to allocate more than 1 bio must always submit the
 *   previously allocated bio for IO before attempting to allocate a new one.
 *   Failure to do so can cause deadlocks under memory pressure.
 *
 *   Note that when running under generic_make_request() (i.e. any block
 *   driver), bios are not submitted until after you return - see the code in
 *   generic_make_request() that converts recursion into iteration, to prevent
 *   stack overflows.
 *
 *   This would normally mean allocating multiple bios under
 *   generic_make_request() would be susceptible to deadlocks, but we have
 *   deadlock avoidance code that resubmits any blocked bios from a rescuer
 *   thread.
 *
 *   However, we do not guarantee forward progress for allocations from other
 *   mempools. Doing multiple allocations from the same mempool under
 *   generic_make_request() should be avoided - instead, use bio_set's front_pad
 *   for per bio allocations.
 *
 *   RETURNS:
 *   Pointer to new bio on success, NULL on failure.
 */
struct bio *bio_alloc_bioset(gfp_t gfp_mask, int nr_iovecs, struct bio_set *bs)
{
	gfp_t saved_gfp = gfp_mask;
	unsigned front_pad;
	unsigned inline_vecs;
	unsigned long idx = BIO_POOL_NONE;
	struct bio_vec *bvl = NULL;
	struct bio *bio;
	void *p;

	if (!bs) {
		if (nr_iovecs > UIO_MAXIOV)
			return NULL;

		p = kmalloc(sizeof(struct bio) +
			    nr_iovecs * sizeof(struct bio_vec),
			    gfp_mask);
		front_pad = 0;
		inline_vecs = nr_iovecs;
	} else {
		/* should not use nobvec bioset for nr_iovecs > 0 */
		if (WARN_ON_ONCE(!bs->bvec_pool && nr_iovecs > 0))
			return NULL;
		/*
		 * generic_make_request() converts recursion to iteration; this
		 * means if we're running beneath it, any bios we allocate and
		 * submit will not be submitted (and thus freed) until after we
		 * return.
		 *
		 * This exposes us to a potential deadlock if we allocate
		 * multiple bios from the same bio_set() while running
		 * underneath generic_make_request(). If we were to allocate
		 * multiple bios (say a stacking block driver that was splitting
		 * bios), we would deadlock if we exhausted the mempool's
		 * reserve.
		 *
		 * We solve this, and guarantee forward progress, with a rescuer
		 * workqueue per bio_set. If we go to allocate and there are
		 * bios on current->bio_list, we first try the allocation
		 * without __GFP_WAIT; if that fails, we punt those bios we
		 * would be blocking to the rescuer workqueue before we retry
		 * with the original gfp_flags.
		 */

		if (current->bio_list && !bio_list_empty(current->bio_list))
			gfp_mask &= ~__GFP_WAIT;

		p = mempool_alloc(bs->bio_pool, gfp_mask);
		if (!p && gfp_mask != saved_gfp) {
			punt_bios_to_rescuer(bs);
			gfp_mask = saved_gfp;
			p = mempool_alloc(bs->bio_pool, gfp_mask);
		}

		front_pad = bs->front_pad;
		inline_vecs = BIO_INLINE_VECS;
	}

	if (unlikely(!p))
		return NULL;

	bio = p + front_pad;
	bio_init(bio);

	if (nr_iovecs > inline_vecs) {
		bvl = bvec_alloc(gfp_mask, nr_iovecs, &idx, bs->bvec_pool);
		if (!bvl && gfp_mask != saved_gfp) {
			punt_bios_to_rescuer(bs);
			gfp_mask = saved_gfp;
			bvl = bvec_alloc(gfp_mask, nr_iovecs, &idx, bs->bvec_pool);
		}

		if (unlikely(!bvl))
			goto err_free;

		bio->bi_flags |= 1 << BIO_OWNS_VEC;
	} else if (nr_iovecs) {
		bvl = bio->bi_inline_vecs;
	}

	bio->bi_pool = bs;
	bio->bi_flags |= idx << BIO_POOL_OFFSET;
	bio->bi_max_vecs = nr_iovecs;
	bio->bi_io_vec = bvl;
	return bio;

err_free:
	mempool_free(p, bs->bio_pool);
	return NULL;
}
EXPORT_SYMBOL(bio_alloc_bioset);

void zero_fill_bio(struct bio *bio)
{
	unsigned long flags;
	struct bio_vec bv;
	struct bvec_iter iter;

	bio_for_each_segment(bv, bio, iter) {
		char *data = bvec_kmap_irq(&bv, &flags);
		memset(data, 0, bv.bv_len);
		flush_dcache_page(bv.bv_page);
		bvec_kunmap_irq(data, &flags);
	}
}
EXPORT_SYMBOL(zero_fill_bio);

/**
 * bio_put - release a reference to a bio
 * @bio:   bio to release reference to
 *
 * Description:
 *   Put a reference to a &struct bio, either one you have gotten with
 *   bio_alloc, bio_get or bio_clone. The last put of a bio will free it.
 **/
void bio_put(struct bio *bio)
{
	BIO_BUG_ON(!atomic_read(&bio->bi_cnt));

	/*
	 * last put frees it
	 */
	if (atomic_dec_and_test(&bio->bi_cnt))
		bio_free(bio);
}
EXPORT_SYMBOL(bio_put);

inline int bio_phys_segments(struct request_queue *q, struct bio *bio)
{
	if (unlikely(!bio_flagged(bio, BIO_SEG_VALID)))
		blk_recount_segments(q, bio);

	return bio->bi_phys_segments;
}
EXPORT_SYMBOL(bio_phys_segments);

/**
 * 	__bio_clone_fast - clone a bio that shares the original bio's biovec
 * 	@bio: destination bio
 * 	@bio_src: bio to clone
 *
 *	Clone a &bio. Caller will own the returned bio, but not
 *	the actual data it points to. Reference count of returned
 * 	bio will be one.
 *
 * 	Caller must ensure that @bio_src is not freed before @bio.
 */
void __bio_clone_fast(struct bio *bio, struct bio *bio_src)
{
	BUG_ON(bio->bi_pool && BIO_POOL_IDX(bio) != BIO_POOL_NONE);

	/*
	 * most users will be overriding ->bi_bdev with a new target,
	 * so we don't set nor calculate new physical/hw segment counts here
	 */
	bio->bi_bdev = bio_src->bi_bdev;
	bio->bi_flags |= 1 << BIO_CLONED;
	bio->bi_rw = bio_src->bi_rw;
	bio->bi_iter = bio_src->bi_iter;
	bio->bi_io_vec = bio_src->bi_io_vec;
}
EXPORT_SYMBOL(__bio_clone_fast);

/**
 *	bio_clone_fast - clone a bio that shares the original bio's biovec
 *	@bio: bio to clone
 *	@gfp_mask: allocation priority
 *	@bs: bio_set to allocate from
 *
 * 	Like __bio_clone_fast, only also allocates the returned bio
 */
struct bio *bio_clone_fast(struct bio *bio, gfp_t gfp_mask, struct bio_set *bs)
{
	struct bio *b;

	b = bio_alloc_bioset(gfp_mask, 0, bs);
	if (!b)
		return NULL;

	__bio_clone_fast(b, bio);

	if (bio_integrity(bio)) {
		int ret;

		ret = bio_integrity_clone(b, bio, gfp_mask);

		if (ret < 0) {
			bio_put(b);
			return NULL;
		}
	}

	return b;
}
EXPORT_SYMBOL(bio_clone_fast);

/**
 * 	bio_clone_bioset - clone a bio
 * 	@bio_src: bio to clone
 *	@gfp_mask: allocation priority
 *	@bs: bio_set to allocate from
 *
 *	Clone bio. Caller will own the returned bio, but not the actual data it
 *	points to. Reference count of returned bio will be one.
 */
struct bio *bio_clone_bioset(struct bio *bio_src, gfp_t gfp_mask,
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

	bio->bi_bdev		= bio_src->bi_bdev;
	bio->bi_rw		= bio_src->bi_rw;
	bio->bi_iter.bi_sector	= bio_src->bi_iter.bi_sector;
	bio->bi_iter.bi_size	= bio_src->bi_iter.bi_size;

	if (bio->bi_rw & REQ_DISCARD)
		goto integrity_clone;

	if (bio->bi_rw & REQ_WRITE_SAME) {
		bio->bi_io_vec[bio->bi_vcnt++] = bio_src->bi_io_vec[0];
		goto integrity_clone;
	}

	bio_for_each_segment(bv, bio_src, iter)
		bio->bi_io_vec[bio->bi_vcnt++] = bv;

integrity_clone:
	if (bio_integrity(bio_src)) {
		int ret;

		ret = bio_integrity_clone(bio, bio_src, gfp_mask);
		if (ret < 0) {
			bio_put(bio);
			return NULL;
		}
	}

	return bio;
}
EXPORT_SYMBOL(bio_clone_bioset);

/**
 *	bio_get_nr_vecs		- return approx number of vecs
 *	@bdev:  I/O target
 *
 *	Return the approximate number of pages we can send to this target.
 *	There's no guarantee that you will be able to fit this number of pages
 *	into a bio, it does not account for dynamic restrictions that vary
 *	on offset.
 */
int bio_get_nr_vecs(struct block_device *bdev)
{
	struct request_queue *q = bdev_get_queue(bdev);
	int nr_pages;

	nr_pages = min_t(unsigned,
		     queue_max_segments(q),
		     queue_max_sectors(q) / (PAGE_SIZE >> 9) + 1);

	return min_t(unsigned, nr_pages, BIO_MAX_PAGES);

}
EXPORT_SYMBOL(bio_get_nr_vecs);

static int __bio_add_page(struct request_queue *q, struct bio *bio, struct page
			  *page, unsigned int len, unsigned int offset,
			  unsigned int max_sectors)
{
	int retried_segments = 0;
	struct bio_vec *bvec;

	/*
	 * cloned bio must not modify vec list
	 */
	if (unlikely(bio_flagged(bio, BIO_CLONED)))
		return 0;

	if (((bio->bi_iter.bi_size + len) >> 9) > max_sectors)
		return 0;

	/*
	 * For filesystems with a blocksize smaller than the pagesize
	 * we will often be called with the same page as last time and
	 * a consecutive offset.  Optimize this special case.
	 */
	if (bio->bi_vcnt > 0) {
		struct bio_vec *prev = &bio->bi_io_vec[bio->bi_vcnt - 1];

		if (page == prev->bv_page &&
		    offset == prev->bv_offset + prev->bv_len) {
			unsigned int prev_bv_len = prev->bv_len;
			prev->bv_len += len;

			if (q->merge_bvec_fn) {
				struct bvec_merge_data bvm = {
					/* prev_bvec is already charged in
					   bi_size, discharge it in order to
					   simulate merging updated prev_bvec
					   as new bvec. */
					.bi_bdev = bio->bi_bdev,
					.bi_sector = bio->bi_iter.bi_sector,
					.bi_size = bio->bi_iter.bi_size -
						prev_bv_len,
					.bi_rw = bio->bi_rw,
				};

				if (q->merge_bvec_fn(q, &bvm, prev) < prev->bv_len) {
					prev->bv_len -= len;
					return 0;
				}
			}

			bio->bi_iter.bi_size += len;
			goto done;
		}

		/*
		 * If the queue doesn't support SG gaps and adding this
		 * offset would create a gap, disallow it.
		 */
		if (q->queue_flags & (1 << QUEUE_FLAG_SG_GAPS) &&
		    bvec_gap_to_prev(prev, offset))
			return 0;
	}

	if (bio->bi_vcnt >= bio->bi_max_vecs)
		return 0;

	/*
	 * setup the new entry, we might clear it again later if we
	 * cannot add the page
	 */
	bvec = &bio->bi_io_vec[bio->bi_vcnt];
	bvec->bv_page = page;
	bvec->bv_len = len;
	bvec->bv_offset = offset;
	bio->bi_vcnt++;
	bio->bi_phys_segments++;
	bio->bi_iter.bi_size += len;

	/*
	 * Perform a recount if the number of segments is greater
	 * than queue_max_segments(q).
	 */

	while (bio->bi_phys_segments > queue_max_segments(q)) {

		if (retried_segments)
			goto failed;

		retried_segments = 1;
		blk_recount_segments(q, bio);
	}

	/*
	 * if queue has other restrictions (eg varying max sector size
	 * depending on offset), it can specify a merge_bvec_fn in the
	 * queue to get further control
	 */
	if (q->merge_bvec_fn) {
		struct bvec_merge_data bvm = {
			.bi_bdev = bio->bi_bdev,
			.bi_sector = bio->bi_iter.bi_sector,
			.bi_size = bio->bi_iter.bi_size - len,
			.bi_rw = bio->bi_rw,
		};

		/*
		 * merge_bvec_fn() returns number of bytes it can accept
		 * at this offset
		 */
		if (q->merge_bvec_fn(q, &bvm, bvec) < bvec->bv_len)
			goto failed;
	}

	/* If we may be able to merge these biovecs, force a recount */
	if (bio->bi_vcnt > 1 && (BIOVEC_PHYS_MERGEABLE(bvec-1, bvec)))
		bio->bi_flags &= ~(1 << BIO_SEG_VALID);

 done:
	return len;

 failed:
	bvec->bv_page = NULL;
	bvec->bv_len = 0;
	bvec->bv_offset = 0;
	bio->bi_vcnt--;
	bio->bi_iter.bi_size -= len;
	blk_recount_segments(q, bio);
	return 0;
}

/**
 *	bio_add_pc_page	-	attempt to add page to bio
 *	@q: the target queue
 *	@bio: destination bio
 *	@page: page to add
 *	@len: vec entry length
 *	@offset: vec entry offset
 *
 *	Attempt to add a page to the bio_vec maplist. This can fail for a
 *	number of reasons, such as the bio being full or target block device
 *	limitations. The target block device must allow bio's up to PAGE_SIZE,
 *	so it is always possible to add a single page to an empty bio.
 *
 *	This should only be used by REQ_PC bios.
 */
int bio_add_pc_page(struct request_queue *q, struct bio *bio, struct page *page,
		    unsigned int len, unsigned int offset)
{
	return __bio_add_page(q, bio, page, len, offset,
			      queue_max_hw_sectors(q));
}
EXPORT_SYMBOL(bio_add_pc_page);

/**
 *	bio_add_page	-	attempt to add page to bio
 *	@bio: destination bio
 *	@page: page to add
 *	@len: vec entry length
 *	@offset: vec entry offset
 *
 *	Attempt to add a page to the bio_vec maplist. This can fail for a
 *	number of reasons, such as the bio being full or target block device
 *	limitations. The target block device must allow bio's up to PAGE_SIZE,
 *	so it is always possible to add a single page to an empty bio.
 */
int bio_add_page(struct bio *bio, struct page *page, unsigned int len,
		 unsigned int offset)
{
	struct request_queue *q = bdev_get_queue(bio->bi_bdev);
	unsigned int max_sectors;

	max_sectors = blk_max_size_offset(q, bio->bi_iter.bi_sector);
	if ((max_sectors < (len >> 9)) && !bio->bi_iter.bi_size)
		max_sectors = len >> 9;

	return __bio_add_page(q, bio, page, len, offset, max_sectors);
}
EXPORT_SYMBOL(bio_add_page);

struct submit_bio_ret {
	struct completion event;
	int error;
};

static void submit_bio_wait_endio(struct bio *bio, int error)
{
	struct submit_bio_ret *ret = bio->bi_private;

	ret->error = error;
	complete(&ret->event);
}

/**
 * submit_bio_wait - submit a bio, and wait until it completes
 * @rw: whether to %READ or %WRITE, or maybe to %READA (read ahead)
 * @bio: The &struct bio which describes the I/O
 *
 * Simple wrapper around submit_bio(). Returns 0 on success, or the error from
 * bio_endio() on failure.
 */
int submit_bio_wait(int rw, struct bio *bio)
{
	struct submit_bio_ret ret;

	rw |= REQ_SYNC;
	init_completion(&ret.event);
	bio->bi_private = &ret;
	bio->bi_end_io = submit_bio_wait_endio;
	submit_bio(rw, bio);
	wait_for_completion(&ret.event);

	return ret.error;
}
EXPORT_SYMBOL(submit_bio_wait);

/**
 * bio_advance - increment/complete a bio by some number of bytes
 * @bio:	bio to advance
 * @bytes:	number of bytes to complete
 *
 * This updates bi_sector, bi_size and bi_idx; if the number of bytes to
 * complete doesn't align with a bvec boundary, then bv_len and bv_offset will
 * be updated on the last bvec as well.
 *
 * @bio will then represent the remaining, uncompleted portion of the io.
 */
void bio_advance(struct bio *bio, unsigned bytes)
{
	if (bio_integrity(bio))
		bio_integrity_advance(bio, bytes);

	bio_advance_iter(bio, &bio->bi_iter, bytes);
}
EXPORT_SYMBOL(bio_advance);

/**
 * bio_alloc_pages - allocates a single page for each bvec in a bio
 * @bio: bio to allocate pages for
 * @gfp_mask: flags for allocation
 *
 * Allocates pages up to @bio->bi_vcnt.
 *
 * Returns 0 on success, -ENOMEM on failure. On failure, any allocated pages are
 * freed.
 */
int bio_alloc_pages(struct bio *bio, gfp_t gfp_mask)
{
	int i;
	struct bio_vec *bv;

	bio_for_each_segment_all(bv, bio, i) {
		bv->bv_page = alloc_page(gfp_mask);
		if (!bv->bv_page) {
			while (--bv >= bio->bi_io_vec)
				__free_page(bv->bv_page);
			return -ENOMEM;
		}
	}

	return 0;
}
EXPORT_SYMBOL(bio_alloc_pages);

/**
 * bio_copy_data - copy contents of data buffers from one chain of bios to
 * another
 * @src: source bio list
 * @dst: destination bio list
 *
 * If @src and @dst are single bios, bi_next must be NULL - otherwise, treats
 * @src and @dst as linked lists of bios.
 *
 * Stops when it reaches the end of either @src or @dst - that is, copies
 * min(src->bi_size, dst->bi_size) bytes (or the equivalent for lists of bios).
 */
void bio_copy_data(struct bio *dst, struct bio *src)
{
	struct bvec_iter src_iter, dst_iter;
	struct bio_vec src_bv, dst_bv;
	void *src_p, *dst_p;
	unsigned bytes;

	src_iter = src->bi_iter;
	dst_iter = dst->bi_iter;

	while (1) {
		if (!src_iter.bi_size) {
			src = src->bi_next;
			if (!src)
				break;

			src_iter = src->bi_iter;
		}

		if (!dst_iter.bi_size) {
			dst = dst->bi_next;
			if (!dst)
				break;

			dst_iter = dst->bi_iter;
		}

		src_bv = bio_iter_iovec(src, src_iter);
		dst_bv = bio_iter_iovec(dst, dst_iter);

		bytes = min(src_bv.bv_len, dst_bv.bv_len);

		src_p = kmap_atomic(src_bv.bv_page);
		dst_p = kmap_atomic(dst_bv.bv_page);

		memcpy(dst_p + dst_bv.bv_offset,
		       src_p + src_bv.bv_offset,
		       bytes);

		kunmap_atomic(dst_p);
		kunmap_atomic(src_p);

		bio_advance_iter(src, &src_iter, bytes);
		bio_advance_iter(dst, &dst_iter, bytes);
	}
}
EXPORT_SYMBOL(bio_copy_data);

struct bio_map_data {
	int is_our_pages;
	struct iov_iter iter;
	struct iovec iov[];
};

static struct bio_map_data *bio_alloc_map_data(unsigned int iov_count,
					       gfp_t gfp_mask)
{
	if (iov_count > UIO_MAXIOV)
		return NULL;

	return kmalloc(sizeof(struct bio_map_data) +
		       sizeof(struct iovec) * iov_count, gfp_mask);
}

/**
 * bio_copy_from_iter - copy all pages from iov_iter to bio
 * @bio: The &struct bio which describes the I/O as destination
 * @iter: iov_iter as source
 *
 * Copy all pages from iov_iter to bio.
 * Returns 0 on success, or error on failure.
 */
static int bio_copy_from_iter(struct bio *bio, struct iov_iter iter)
{
	int i;
	struct bio_vec *bvec;

	bio_for_each_segment_all(bvec, bio, i) {
		ssize_t ret;

		ret = copy_page_from_iter(bvec->bv_page,
					  bvec->bv_offset,
					  bvec->bv_len,
					  &iter);

		if (!iov_iter_count(&iter))
			break;

		if (ret < bvec->bv_len)
			return -EFAULT;
	}

	return 0;
}

/**
 * bio_copy_to_iter - copy all pages from bio to iov_iter
 * @bio: The &struct bio which describes the I/O as source
 * @iter: iov_iter as destination
 *
 * Copy all pages from bio to iov_iter.
 * Returns 0 on success, or error on failure.
 */
static int bio_copy_to_iter(struct bio *bio, struct iov_iter iter)
{
	int i;
	struct bio_vec *bvec;

	bio_for_each_segment_all(bvec, bio, i) {
		ssize_t ret;

		ret = copy_page_to_iter(bvec->bv_page,
					bvec->bv_offset,
					bvec->bv_len,
					&iter);

		if (!iov_iter_count(&iter))
			break;

		if (ret < bvec->bv_len)
			return -EFAULT;
	}

	return 0;
}

static void bio_free_pages(struct bio *bio)
{
	struct bio_vec *bvec;
	int i;

	bio_for_each_segment_all(bvec, bio, i)
		__free_page(bvec->bv_page);
}

/**
 *	bio_uncopy_user	-	finish previously mapped bio
 *	@bio: bio being terminated
 *
 *	Free pages allocated from bio_copy_user_iov() and write back data
 *	to user space in case of a read.
 */
int bio_uncopy_user(struct bio *bio)
{
	struct bio_map_data *bmd = bio->bi_private;
	int ret = 0;

	if (!bio_flagged(bio, BIO_NULL_MAPPED)) {
		/*
		 * if we're in a workqueue, the request is orphaned, so
		 * don't copy into a random user address space, just free.
		 */
		if (current->mm && bio_data_dir(bio) == READ)
			ret = bio_copy_to_iter(bio, bmd->iter);
		if (bmd->is_our_pages)
			bio_free_pages(bio);
	}
	kfree(bmd);
	bio_put(bio);
	return ret;
}
EXPORT_SYMBOL(bio_uncopy_user);

/**
 *	bio_copy_user_iov	-	copy user data to bio
 *	@q:		destination block queue
 *	@map_data:	pointer to the rq_map_data holding pages (if necessary)
 *	@iter:		iovec iterator
 *	@gfp_mask:	memory allocation flags
 *
 *	Prepares and returns a bio for indirect user io, bouncing data
 *	to/from kernel pages as necessary. Must be paired with
 *	call bio_uncopy_user() on io completion.
 */
struct bio *bio_copy_user_iov(struct request_queue *q,
			      struct rq_map_data *map_data,
			      const struct iov_iter *iter,
			      gfp_t gfp_mask)
{
	struct bio_map_data *bmd;
	struct page *page;
	struct bio *bio;
	int i, ret;
	int nr_pages = 0;
	unsigned int len = iter->count;
	unsigned int offset = map_data ? map_data->offset & ~PAGE_MASK : 0;

	for (i = 0; i < iter->nr_segs; i++) {
		unsigned long uaddr;
		unsigned long end;
		unsigned long start;

		uaddr = (unsigned long) iter->iov[i].iov_base;
		end = (uaddr + iter->iov[i].iov_len + PAGE_SIZE - 1)
			>> PAGE_SHIFT;
		start = uaddr >> PAGE_SHIFT;

		/*
		 * Overflow, abort
		 */
		if (end < start)
			return ERR_PTR(-EINVAL);

		nr_pages += end - start;
	}

	if (offset)
		nr_pages++;

	bmd = bio_alloc_map_data(iter->nr_segs, gfp_mask);
	if (!bmd)
		return ERR_PTR(-ENOMEM);

	/*
	 * We need to do a deep copy of the iov_iter including the iovecs.
	 * The caller provided iov might point to an on-stack or otherwise
	 * shortlived one.
	 */
	bmd->is_our_pages = map_data ? 0 : 1;
	memcpy(bmd->iov, iter->iov, sizeof(struct iovec) * iter->nr_segs);
	iov_iter_init(&bmd->iter, iter->type, bmd->iov,
			iter->nr_segs, iter->count);

	ret = -ENOMEM;
	bio = bio_kmalloc(gfp_mask, nr_pages);
	if (!bio)
		goto out_bmd;

	if (iter->type & WRITE)
		bio->bi_rw |= REQ_WRITE;

	ret = 0;

	if (map_data) {
		nr_pages = 1 << map_data->page_order;
		i = map_data->offset / PAGE_SIZE;
	}
	while (len) {
		unsigned int bytes = PAGE_SIZE;

		bytes -= offset;

		if (bytes > len)
			bytes = len;

		if (map_data) {
			if (i == map_data->nr_entries * nr_pages) {
				ret = -ENOMEM;
				break;
			}

			page = map_data->pages[i / nr_pages];
			page += (i % nr_pages);

			i++;
		} else {
			page = alloc_page(q->bounce_gfp | gfp_mask);
			if (!page) {
				ret = -ENOMEM;
				break;
			}
		}

		if (bio_add_pc_page(q, bio, page, bytes, offset) < bytes)
			break;

		len -= bytes;
		offset = 0;
	}

	if (ret)
		goto cleanup;

	/*
	 * success
	 */
	if (((iter->type & WRITE) && (!map_data || !map_data->null_mapped)) ||
	    (map_data && map_data->from_user)) {
		ret = bio_copy_from_iter(bio, *iter);
		if (ret)
			goto cleanup;
	}

	bio->bi_private = bmd;
	return bio;
cleanup:
	if (!map_data)
		bio_free_pages(bio);
	bio_put(bio);
out_bmd:
	kfree(bmd);
	return ERR_PTR(ret);
}

/**
 *	bio_map_user_iov - map user iovec into bio
 *	@q:		the struct request_queue for the bio
 *	@iter:		iovec iterator
 *	@gfp_mask:	memory allocation flags
 *
 *	Map the user space address into a bio suitable for io to a block
 *	device. Returns an error pointer in case of error.
 */
struct bio *bio_map_user_iov(struct request_queue *q,
			     const struct iov_iter *iter,
			     gfp_t gfp_mask)
{
	int j;
	int nr_pages = 0;
	struct page **pages;
	struct bio *bio;
	int cur_page = 0;
	int ret, offset;
	struct iov_iter i;
	struct iovec iov;

	iov_for_each(iov, i, *iter) {
		unsigned long uaddr = (unsigned long) iov.iov_base;
		unsigned long len = iov.iov_len;
		unsigned long end = (uaddr + len + PAGE_SIZE - 1) >> PAGE_SHIFT;
		unsigned long start = uaddr >> PAGE_SHIFT;

		/*
		 * Overflow, abort
		 */
		if (end < start)
			return ERR_PTR(-EINVAL);

		nr_pages += end - start;
		/*
		 * buffer must be aligned to at least hardsector size for now
		 */
		if (uaddr & queue_dma_alignment(q))
			return ERR_PTR(-EINVAL);
	}

	if (!nr_pages)
		return ERR_PTR(-EINVAL);

	bio = bio_kmalloc(gfp_mask, nr_pages);
	if (!bio)
		return ERR_PTR(-ENOMEM);

	ret = -ENOMEM;
	pages = kcalloc(nr_pages, sizeof(struct page *), gfp_mask);
	if (!pages)
		goto out;

	iov_for_each(iov, i, *iter) {
		unsigned long uaddr = (unsigned long) iov.iov_base;
		unsigned long len = iov.iov_len;
		unsigned long end = (uaddr + len + PAGE_SIZE - 1) >> PAGE_SHIFT;
		unsigned long start = uaddr >> PAGE_SHIFT;
		const int local_nr_pages = end - start;
		const int page_limit = cur_page + local_nr_pages;

		ret = get_user_pages_fast(uaddr, local_nr_pages,
				(iter->type & WRITE) != WRITE,
				&pages[cur_page]);
		if (ret < local_nr_pages) {
			ret = -EFAULT;
			goto out_unmap;
		}

		offset = uaddr & ~PAGE_MASK;
		for (j = cur_page; j < page_limit; j++) {
			unsigned int bytes = PAGE_SIZE - offset;

			if (len <= 0)
				break;
			
			if (bytes > len)
				bytes = len;

			/*
			 * sorry...
			 */
			if (bio_add_pc_page(q, bio, pages[j], bytes, offset) <
					    bytes)
				break;

			len -= bytes;
			offset = 0;
		}

		cur_page = j;
		/*
		 * release the pages we didn't map into the bio, if any
		 */
		while (j < page_limit)
			page_cache_release(pages[j++]);
	}

	kfree(pages);

	/*
	 * set data direction, and check if mapped pages need bouncing
	 */
	if (iter->type & WRITE)
		bio->bi_rw |= REQ_WRITE;

	bio->bi_flags |= (1 << BIO_USER_MAPPED);

	/*
	 * subtle -- if __bio_map_user() ended up bouncing a bio,
	 * it would normally disappear when its bi_end_io is run.
	 * however, we need it for the unmap, so grab an extra
	 * reference to it
	 */
	bio_get(bio);
	return bio;

 out_unmap:
	for (j = 0; j < nr_pages; j++) {
		if (!pages[j])
			break;
		page_cache_release(pages[j]);
	}
 out:
	kfree(pages);
	bio_put(bio);
	return ERR_PTR(ret);
}

static void __bio_unmap_user(struct bio *bio)
{
	struct bio_vec *bvec;
	int i;

	/*
	 * make sure we dirty pages we wrote to
	 */
	bio_for_each_segment_all(bvec, bio, i) {
		if (bio_data_dir(bio) == READ)
			set_page_dirty_lock(bvec->bv_page);

		page_cache_release(bvec->bv_page);
	}

	bio_put(bio);
}

/**
 *	bio_unmap_user	-	unmap a bio
 *	@bio:		the bio being unmapped
 *
 *	Unmap a bio previously mapped by bio_map_user(). Must be called with
 *	a process context.
 *
 *	bio_unmap_user() may sleep.
 */
void bio_unmap_user(struct bio *bio)
{
	__bio_unmap_user(bio);
	bio_put(bio);
}
EXPORT_SYMBOL(bio_unmap_user);

static void bio_map_kern_endio(struct bio *bio, int err)
{
	bio_put(bio);
}

/**
 *	bio_map_kern	-	map kernel address into bio
 *	@q: the struct request_queue for the bio
 *	@data: pointer to buffer to map
 *	@len: length in bytes
 *	@gfp_mask: allocation flags for bio allocation
 *
 *	Map the kernel address into a bio suitable for io to a block
 *	device. Returns an error pointer in case of error.
 */
struct bio *bio_map_kern(struct request_queue *q, void *data, unsigned int len,
			 gfp_t gfp_mask)
{
	unsigned long kaddr = (unsigned long)data;
	unsigned long end = (kaddr + len + PAGE_SIZE - 1) >> PAGE_SHIFT;
	unsigned long start = kaddr >> PAGE_SHIFT;
	const int nr_pages = end - start;
	int offset, i;
	struct bio *bio;

	bio = bio_kmalloc(gfp_mask, nr_pages);
	if (!bio)
		return ERR_PTR(-ENOMEM);

	offset = offset_in_page(kaddr);
	for (i = 0; i < nr_pages; i++) {
		unsigned int bytes = PAGE_SIZE - offset;

		if (len <= 0)
			break;

		if (bytes > len)
			bytes = len;

		if (bio_add_pc_page(q, bio, virt_to_page(data), bytes,
				    offset) < bytes) {
			/* we don't support partial mappings */
			bio_put(bio);
			return ERR_PTR(-EINVAL);
		}

		data += bytes;
		len -= bytes;
		offset = 0;
	}

	bio->bi_end_io = bio_map_kern_endio;
	return bio;
}
EXPORT_SYMBOL(bio_map_kern);

static void bio_copy_kern_endio(struct bio *bio, int err)
{
	bio_free_pages(bio);
	bio_put(bio);
}

static void bio_copy_kern_endio_read(struct bio *bio, int err)
{
	char *p = bio->bi_private;
	struct bio_vec *bvec;
	int i;

	bio_for_each_segment_all(bvec, bio, i) {
		memcpy(p, page_address(bvec->bv_page), bvec->bv_len);
		p += bvec->bv_len;
	}

	bio_copy_kern_endio(bio, err);
}

/**
 *	bio_copy_kern	-	copy kernel address into bio
 *	@q: the struct request_queue for the bio
 *	@data: pointer to buffer to copy
 *	@len: length in bytes
 *	@gfp_mask: allocation flags for bio and page allocation
 *	@reading: data direction is READ
 *
 *	copy the kernel address into a bio suitable for io to a block
 *	device. Returns an error pointer in case of error.
 */
struct bio *bio_copy_kern(struct request_queue *q, void *data, unsigned int len,
			  gfp_t gfp_mask, int reading)
{
	unsigned long kaddr = (unsigned long)data;
	unsigned long end = (kaddr + len + PAGE_SIZE - 1) >> PAGE_SHIFT;
	unsigned long start = kaddr >> PAGE_SHIFT;
	struct bio *bio;
	void *p = data;
	int nr_pages = 0;

	/*
	 * Overflow, abort
	 */
	if (end < start)
		return ERR_PTR(-EINVAL);

	nr_pages = end - start;
	bio = bio_kmalloc(gfp_mask, nr_pages);
	if (!bio)
		return ERR_PTR(-ENOMEM);

	while (len) {
		struct page *page;
		unsigned int bytes = PAGE_SIZE;

		if (bytes > len)
			bytes = len;

		page = alloc_page(q->bounce_gfp | gfp_mask);
		if (!page)
			goto cleanup;

		if (!reading)
			memcpy(page_address(page), p, bytes);

		if (bio_add_pc_page(q, bio, page, bytes, 0) < bytes)
			break;

		len -= bytes;
		p += bytes;
	}

	if (reading) {
		bio->bi_end_io = bio_copy_kern_endio_read;
		bio->bi_private = data;
	} else {
		bio->bi_end_io = bio_copy_kern_endio;
		bio->bi_rw |= REQ_WRITE;
	}

	return bio;

cleanup:
	bio_free_pages(bio);
	bio_put(bio);
	return ERR_PTR(-ENOMEM);
}
EXPORT_SYMBOL(bio_copy_kern);

/*
 * bio_set_pages_dirty() and bio_check_pages_dirty() are support functions
 * for performing direct-IO in BIOs.
 *
 * The problem is that we cannot run set_page_dirty() from interrupt context
 * because the required locks are not interrupt-safe.  So what we can do is to
 * mark the pages dirty _before_ performing IO.  And in interrupt context,
 * check that the pages are still dirty.   If so, fine.  If not, redirty them
 * in process context.
 *
 * We special-case compound pages here: normally this means reads into hugetlb
 * pages.  The logic in here doesn't really work right for compound pages
 * because the VM does not uniformly chase down the head page in all cases.
 * But dirtiness of compound pages is pretty meaningless anyway: the VM doesn't
 * handle them at all.  So we skip compound pages here at an early stage.
 *
 * Note that this code is very hard to test under normal circumstances because
 * direct-io pins the pages with get_user_pages().  This makes
 * is_page_cache_freeable return false, and the VM will not clean the pages.
 * But other code (eg, flusher threads) could clean the pages if they are mapped
 * pagecache.
 *
 * Simply disabling the call to bio_set_pages_dirty() is a good way to test the
 * deferred bio dirtying paths.
 */

/*
 * bio_set_pages_dirty() will mark all the bio's pages as dirty.
 */
void bio_set_pages_dirty(struct bio *bio)
{
	struct bio_vec *bvec;
	int i;

	bio_for_each_segment_all(bvec, bio, i) {
		struct page *page = bvec->bv_page;

		if (page && !PageCompound(page))
			set_page_dirty_lock(page);
	}
}

static void bio_release_pages(struct bio *bio)
{
	struct bio_vec *bvec;
	int i;

	bio_for_each_segment_all(bvec, bio, i) {
		struct page *page = bvec->bv_page;

		if (page)
			put_page(page);
	}
}

/*
 * bio_check_pages_dirty() will check that all the BIO's pages are still dirty.
 * If they are, then fine.  If, however, some pages are clean then they must
 * have been written out during the direct-IO read.  So we take another ref on
 * the BIO and the offending pages and re-dirty the pages in process context.
 *
 * It is expected that bio_check_pages_dirty() will wholly own the BIO from
 * here on.  It will run one page_cache_release() against each page and will
 * run one bio_put() against the BIO.
 */

static void bio_dirty_fn(struct work_struct *work);

static DECLARE_WORK(bio_dirty_work, bio_dirty_fn);
static DEFINE_SPINLOCK(bio_dirty_lock);
static struct bio *bio_dirty_list;

/*
 * This runs in process context
 */
static void bio_dirty_fn(struct work_struct *work)
{
	unsigned long flags;
	struct bio *bio;

	spin_lock_irqsave(&bio_dirty_lock, flags);
	bio = bio_dirty_list;
	bio_dirty_list = NULL;
	spin_unlock_irqrestore(&bio_dirty_lock, flags);

	while (bio) {
		struct bio *next = bio->bi_private;

		bio_set_pages_dirty(bio);
		bio_release_pages(bio);
		bio_put(bio);
		bio = next;
	}
}

void bio_check_pages_dirty(struct bio *bio)
{
	struct bio_vec *bvec;
	int nr_clean_pages = 0;
	int i;

	bio_for_each_segment_all(bvec, bio, i) {
		struct page *page = bvec->bv_page;

		if (PageDirty(page) || PageCompound(page)) {
			page_cache_release(page);
			bvec->bv_page = NULL;
		} else {
			nr_clean_pages++;
		}
	}

	if (nr_clean_pages) {
		unsigned long flags;

		spin_lock_irqsave(&bio_dirty_lock, flags);
		bio->bi_private = bio_dirty_list;
		bio_dirty_list = bio;
		spin_unlock_irqrestore(&bio_dirty_lock, flags);
		schedule_work(&bio_dirty_work);
	} else {
		bio_put(bio);
	}
}

void generic_start_io_acct(int rw, unsigned long sectors,
			   struct hd_struct *part)
{
	int cpu = part_stat_lock();

	part_round_stats(cpu, part);
	part_stat_inc(cpu, part, ios[rw]);
	part_stat_add(cpu, part, sectors[rw], sectors);
	part_inc_in_flight(part, rw);

	part_stat_unlock();
}
EXPORT_SYMBOL(generic_start_io_acct);

void generic_end_io_acct(int rw, struct hd_struct *part,
			 unsigned long start_time)
{
	unsigned long duration = jiffies - start_time;
	int cpu = part_stat_lock();

	part_stat_add(cpu, part, ticks[rw], duration);
	part_round_stats(cpu, part);
	part_dec_in_flight(part, rw);

	part_stat_unlock();
}
EXPORT_SYMBOL(generic_end_io_acct);

#if ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE
void bio_flush_dcache_pages(struct bio *bi)
{
	struct bio_vec bvec;
	struct bvec_iter iter;

	bio_for_each_segment(bvec, bi, iter)
		flush_dcache_page(bvec.bv_page);
}
EXPORT_SYMBOL(bio_flush_dcache_pages);
#endif

/**
 * bio_endio - end I/O on a bio
 * @bio:	bio
 * @error:	error, if any
 *
 * Description:
 *   bio_endio() will end I/O on the whole bio. bio_endio() is the
 *   preferred way to end I/O on a bio, it takes care of clearing
 *   BIO_UPTODATE on error. @error is 0 on success, and and one of the
 *   established -Exxxx (-EIO, for instance) error values in case
 *   something went wrong. No one should call bi_end_io() directly on a
 *   bio unless they own it and thus know that it has an end_io
 *   function.
 **/
void bio_endio(struct bio *bio, int error)
{
	while (bio) {
		BUG_ON(atomic_read(&bio->bi_remaining) <= 0);

		if (error)
			clear_bit(BIO_UPTODATE, &bio->bi_flags);
		else if (!test_bit(BIO_UPTODATE, &bio->bi_flags))
			error = -EIO;

		if (!atomic_dec_and_test(&bio->bi_remaining))
			return;

		/*
		 * Need to have a real endio function for chained bios,
		 * otherwise various corner cases will break (like stacking
		 * block devices that save/restore bi_end_io) - however, we want
		 * to avoid unbounded recursion and blowing the stack. Tail call
		 * optimization would handle this, but compiling with frame
		 * pointers also disables gcc's sibling call optimization.
		 */
		if (bio->bi_end_io == bio_chain_endio) {
			struct bio *parent = bio->bi_private;
			bio_put(bio);
			bio = parent;
		} else {
			if (bio->bi_end_io)
				bio->bi_end_io(bio, error);
			bio = NULL;
		}
	}
}
EXPORT_SYMBOL(bio_endio);

/**
 * bio_endio_nodec - end I/O on a bio, without decrementing bi_remaining
 * @bio:	bio
 * @error:	error, if any
 *
 * For code that has saved and restored bi_end_io; thing hard before using this
 * function, probably you should've cloned the entire bio.
 **/
void bio_endio_nodec(struct bio *bio, int error)
{
	atomic_inc(&bio->bi_remaining);
	bio_endio(bio, error);
}
EXPORT_SYMBOL(bio_endio_nodec);

/**
 * bio_split - split a bio
 * @bio:	bio to split
 * @sectors:	number of sectors to split from the front of @bio
 * @gfp:	gfp mask
 * @bs:		bio set to allocate from
 *
 * Allocates and returns a new bio which represents @sectors from the start of
 * @bio, and updates @bio to represent the remaining sectors.
 *
 * Unless this is a discard request the newly allocated bio will point
 * to @bio's bi_io_vec; it is the caller's responsibility to ensure that
 * @bio is not freed before the split.
 */
struct bio *bio_split(struct bio *bio, int sectors,
		      gfp_t gfp, struct bio_set *bs)
{
	struct bio *split = NULL;

	BUG_ON(sectors <= 0);
	BUG_ON(sectors >= bio_sectors(bio));

	/*
	 * Discards need a mutable bio_vec to accommodate the payload
	 * required by the DSM TRIM and UNMAP commands.
	 */
	if (bio->bi_rw & REQ_DISCARD)
		split = bio_clone_bioset(bio, gfp, bs);
	else
		split = bio_clone_fast(bio, gfp, bs);

	if (!split)
		return NULL;

	split->bi_iter.bi_size = sectors << 9;

	if (bio_integrity(split))
		bio_integrity_trim(split, 0, sectors);

	bio_advance(bio, split->bi_iter.bi_size);

	return split;
}
EXPORT_SYMBOL(bio_split);

/**
 * bio_trim - trim a bio
 * @bio:	bio to trim
 * @offset:	number of sectors to trim from the front of @bio
 * @size:	size we want to trim @bio to, in sectors
 */
void bio_trim(struct bio *bio, int offset, int size)
{
	/* 'bio' is a cloned bio which we need to trim to match
	 * the given offset and size.
	 */

	size <<= 9;
	if (offset == 0 && size == bio->bi_iter.bi_size)
		return;

	clear_bit(BIO_SEG_VALID, &bio->bi_flags);

	bio_advance(bio, offset << 9);

	bio->bi_iter.bi_size = size;
}
EXPORT_SYMBOL_GPL(bio_trim);

/*
 * create memory pools for biovec's in a bio_set.
 * use the global biovec slabs created for general use.
 */
mempool_t *biovec_create_pool(int pool_entries)
{
	struct biovec_slab *bp = bvec_slabs + BIOVEC_MAX_IDX;

	return mempool_create_slab_pool(pool_entries, bp->slab);
}

void bioset_free(struct bio_set *bs)
{
	if (bs->rescue_workqueue)
		destroy_workqueue(bs->rescue_workqueue);

	if (bs->bio_pool)
		mempool_destroy(bs->bio_pool);

	if (bs->bvec_pool)
		mempool_destroy(bs->bvec_pool);

	bioset_integrity_free(bs);
	bio_put_slab(bs);

	kfree(bs);
}
EXPORT_SYMBOL(bioset_free);

static struct bio_set *__bioset_create(unsigned int pool_size,
				       unsigned int front_pad,
				       bool create_bvec_pool)
{
	unsigned int back_pad = BIO_INLINE_VECS * sizeof(struct bio_vec);
	struct bio_set *bs;

	bs = kzalloc(sizeof(*bs), GFP_KERNEL);
	if (!bs)
		return NULL;

	bs->front_pad = front_pad;

	spin_lock_init(&bs->rescue_lock);
	bio_list_init(&bs->rescue_list);
	INIT_WORK(&bs->rescue_work, bio_alloc_rescue);

	bs->bio_slab = bio_find_or_create_slab(front_pad + back_pad);
	if (!bs->bio_slab) {
		kfree(bs);
		return NULL;
	}

	bs->bio_pool = mempool_create_slab_pool(pool_size, bs->bio_slab);
	if (!bs->bio_pool)
		goto bad;

	if (create_bvec_pool) {
		bs->bvec_pool = biovec_create_pool(pool_size);
		if (!bs->bvec_pool)
			goto bad;
	}

	bs->rescue_workqueue = alloc_workqueue("bioset", WQ_MEM_RECLAIM, 0);
	if (!bs->rescue_workqueue)
		goto bad;

	return bs;
bad:
	bioset_free(bs);
	return NULL;
}

/**
 * bioset_create  - Create a bio_set
 * @pool_size:	Number of bio and bio_vecs to cache in the mempool
 * @front_pad:	Number of bytes to allocate in front of the returned bio
 *
 * Description:
 *    Set up a bio_set to be used with @bio_alloc_bioset. Allows the caller
 *    to ask for a number of bytes to be allocated in front of the bio.
 *    Front pad allocation is useful for embedding the bio inside
 *    another structure, to avoid allocating extra data to go with the bio.
 *    Note that the bio must be embedded at the END of that structure always,
 *    or things will break badly.
 */
struct bio_set *bioset_create(unsigned int pool_size, unsigned int front_pad)
{
	return __bioset_create(pool_size, front_pad, true);
}
EXPORT_SYMBOL(bioset_create);

/**
 * bioset_create_nobvec  - Create a bio_set without bio_vec mempool
 * @pool_size:	Number of bio to cache in the mempool
 * @front_pad:	Number of bytes to allocate in front of the returned bio
 *
 * Description:
 *    Same functionality as bioset_create() except that mempool is not
 *    created for bio_vecs. Saving some memory for bio_clone_fast() users.
 */
struct bio_set *bioset_create_nobvec(unsigned int pool_size, unsigned int front_pad)
{
	return __bioset_create(pool_size, front_pad, false);
}
EXPORT_SYMBOL(bioset_create_nobvec);

#ifdef CONFIG_BLK_CGROUP
/**
 * bio_associate_current - associate a bio with %current
 * @bio: target bio
 *
 * Associate @bio with %current if it hasn't been associated yet.  Block
 * layer will treat @bio as if it were issued by %current no matter which
 * task actually issues it.
 *
 * This function takes an extra reference of @task's io_context and blkcg
 * which will be put when @bio is released.  The caller must own @bio,
 * ensure %current->io_context exists, and is responsible for synchronizing
 * calls to this function.
 */
int bio_associate_current(struct bio *bio)
{
	struct io_context *ioc;
	struct cgroup_subsys_state *css;

	if (bio->bi_ioc)
		return -EBUSY;

	ioc = current->io_context;
	if (!ioc)
		return -ENOENT;

	/* acquire active ref on @ioc and associate */
	get_io_context_active(ioc);
	bio->bi_ioc = ioc;

	/* associate blkcg if exists */
	rcu_read_lock();
	css = task_css(current, blkio_cgrp_id);
	if (css && css_tryget_online(css))
		bio->bi_css = css;
	rcu_read_unlock();

	return 0;
}

/**
 * bio_disassociate_task - undo bio_associate_current()
 * @bio: target bio
 */
void bio_disassociate_task(struct bio *bio)
{
	if (bio->bi_ioc) {
		put_io_context(bio->bi_ioc);
		bio->bi_ioc = NULL;
	}
	if (bio->bi_css) {
		css_put(bio->bi_css);
		bio->bi_css = NULL;
	}
}

#endif /* CONFIG_BLK_CGROUP */

static void __init biovec_init_slabs(void)
{
	int i;

	for (i = 0; i < BIOVEC_NR_POOLS; i++) {
		int size;
		struct biovec_slab *bvs = bvec_slabs + i;

		if (bvs->nr_vecs <= BIO_INLINE_VECS) {
			bvs->slab = NULL;
			continue;
		}

		size = bvs->nr_vecs * sizeof(struct bio_vec);
		bvs->slab = kmem_cache_create(bvs->name, size, 0,
                                SLAB_HWCACHE_ALIGN|SLAB_PANIC, NULL);
	}
}

static int __init init_bio(void)
{
	bio_slab_max = 2;
	bio_slab_nr = 0;
	bio_slabs = kzalloc(bio_slab_max * sizeof(struct bio_slab), GFP_KERNEL);
	if (!bio_slabs)
		panic("bio: can't allocate bios\n");

	bio_integrity_init();
	biovec_init_slabs();

	fs_bio_set = bioset_create(BIO_POOL_SIZE, 0);
	if (!fs_bio_set)
		panic("bio: can't allocate bios\n");

	if (bioset_integrity_create(fs_bio_set, BIO_POOL_SIZE))
		panic("bio: can't create integrity pool\n");

	return 0;
}
subsys_initcall(init_bio);
