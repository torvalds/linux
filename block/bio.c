// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2001 Jens Axboe <axboe@kernel.dk>
 */
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/bio-integrity.h>
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
#include <linux/highmem.h>
#include <linux/blk-crypto.h>
#include <linux/xarray.h>

#include <trace/events/block.h>
#include "blk.h"
#include "blk-rq-qos.h"
#include "blk-cgroup.h"

#define ALLOC_CACHE_THRESHOLD	16
#define ALLOC_CACHE_MAX		256

struct bio_alloc_cache {
	struct bio		*free_list;
	struct bio		*free_list_irq;
	unsigned int		nr;
	unsigned int		nr_irq;
};

static struct biovec_slab {
	int nr_vecs;
	char *name;
	struct kmem_cache *slab;
} bvec_slabs[] __read_mostly = {
	{ .nr_vecs = 16, .name = "biovec-16" },
	{ .nr_vecs = 64, .name = "biovec-64" },
	{ .nr_vecs = 128, .name = "biovec-128" },
	{ .nr_vecs = BIO_MAX_VECS, .name = "biovec-max" },
};

static struct biovec_slab *biovec_slab(unsigned short nr_vecs)
{
	switch (nr_vecs) {
	/* smaller bios use inline vecs */
	case 5 ... 16:
		return &bvec_slabs[0];
	case 17 ... 64:
		return &bvec_slabs[1];
	case 65 ... 128:
		return &bvec_slabs[2];
	case 129 ... BIO_MAX_VECS:
		return &bvec_slabs[3];
	default:
		BUG();
		return NULL;
	}
}

/*
 * fs_bio_set is the bio_set containing bio and iovec memory pools used by
 * IO code that does not need private memory pools.
 */
struct bio_set fs_bio_set;
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
static DEFINE_XARRAY(bio_slabs);

static struct bio_slab *create_bio_slab(unsigned int size)
{
	struct bio_slab *bslab = kzalloc(sizeof(*bslab), GFP_KERNEL);

	if (!bslab)
		return NULL;

	snprintf(bslab->name, sizeof(bslab->name), "bio-%d", size);
	bslab->slab = kmem_cache_create(bslab->name, size,
			ARCH_KMALLOC_MINALIGN,
			SLAB_HWCACHE_ALIGN | SLAB_TYPESAFE_BY_RCU, NULL);
	if (!bslab->slab)
		goto fail_alloc_slab;

	bslab->slab_ref = 1;
	bslab->slab_size = size;

	if (!xa_err(xa_store(&bio_slabs, size, bslab, GFP_KERNEL)))
		return bslab;

	kmem_cache_destroy(bslab->slab);

fail_alloc_slab:
	kfree(bslab);
	return NULL;
}

static inline unsigned int bs_bio_slab_size(struct bio_set *bs)
{
	return bs->front_pad + sizeof(struct bio) + bs->back_pad;
}

static struct kmem_cache *bio_find_or_create_slab(struct bio_set *bs)
{
	unsigned int size = bs_bio_slab_size(bs);
	struct bio_slab *bslab;

	mutex_lock(&bio_slab_lock);
	bslab = xa_load(&bio_slabs, size);
	if (bslab)
		bslab->slab_ref++;
	else
		bslab = create_bio_slab(size);
	mutex_unlock(&bio_slab_lock);

	if (bslab)
		return bslab->slab;
	return NULL;
}

static void bio_put_slab(struct bio_set *bs)
{
	struct bio_slab *bslab = NULL;
	unsigned int slab_size = bs_bio_slab_size(bs);

	mutex_lock(&bio_slab_lock);

	bslab = xa_load(&bio_slabs, slab_size);
	if (WARN(!bslab, KERN_ERR "bio: unable to find slab!\n"))
		goto out;

	WARN_ON_ONCE(bslab->slab != bs->bio_slab);

	WARN_ON(!bslab->slab_ref);

	if (--bslab->slab_ref)
		goto out;

	xa_erase(&bio_slabs, slab_size);

	kmem_cache_destroy(bslab->slab);
	kfree(bslab);

out:
	mutex_unlock(&bio_slab_lock);
}

void bvec_free(mempool_t *pool, struct bio_vec *bv, unsigned short nr_vecs)
{
	BUG_ON(nr_vecs > BIO_MAX_VECS);

	if (nr_vecs == BIO_MAX_VECS)
		mempool_free(bv, pool);
	else if (nr_vecs > BIO_INLINE_VECS)
		kmem_cache_free(biovec_slab(nr_vecs)->slab, bv);
}

/*
 * Make the first allocation restricted and don't dump info on allocation
 * failures, since we'll fall back to the mempool in case of failure.
 */
static inline gfp_t bvec_alloc_gfp(gfp_t gfp)
{
	return (gfp & ~(__GFP_DIRECT_RECLAIM | __GFP_IO)) |
		__GFP_NOMEMALLOC | __GFP_NORETRY | __GFP_NOWARN;
}

struct bio_vec *bvec_alloc(mempool_t *pool, unsigned short *nr_vecs,
		gfp_t gfp_mask)
{
	struct biovec_slab *bvs = biovec_slab(*nr_vecs);

	if (WARN_ON_ONCE(!bvs))
		return NULL;

	/*
	 * Upgrade the nr_vecs request to take full advantage of the allocation.
	 * We also rely on this in the bvec_free path.
	 */
	*nr_vecs = bvs->nr_vecs;

	/*
	 * Try a slab allocation first for all smaller allocations.  If that
	 * fails and __GFP_DIRECT_RECLAIM is set retry with the mempool.
	 * The mempool is sized to handle up to BIO_MAX_VECS entries.
	 */
	if (*nr_vecs < BIO_MAX_VECS) {
		struct bio_vec *bvl;

		bvl = kmem_cache_alloc(bvs->slab, bvec_alloc_gfp(gfp_mask));
		if (likely(bvl) || !(gfp_mask & __GFP_DIRECT_RECLAIM))
			return bvl;
		*nr_vecs = BIO_MAX_VECS;
	}

	return mempool_alloc(pool, gfp_mask);
}

void bio_uninit(struct bio *bio)
{
#ifdef CONFIG_BLK_CGROUP
	if (bio->bi_blkg) {
		blkg_put(bio->bi_blkg);
		bio->bi_blkg = NULL;
	}
#endif
	if (bio_integrity(bio))
		bio_integrity_free(bio);

	bio_crypt_free_ctx(bio);
}
EXPORT_SYMBOL(bio_uninit);

static void bio_free(struct bio *bio)
{
	struct bio_set *bs = bio->bi_pool;
	void *p = bio;

	WARN_ON_ONCE(!bs);

	bio_uninit(bio);
	bvec_free(&bs->bvec_pool, bio->bi_io_vec, bio->bi_max_vecs);
	mempool_free(p - bs->front_pad, &bs->bio_pool);
}

/*
 * Users of this function have their own bio allocation. Subsequently,
 * they must remember to pair any call to bio_init() with bio_uninit()
 * when IO has completed, or when the bio is released.
 */
void bio_init(struct bio *bio, struct block_device *bdev, struct bio_vec *table,
	      unsigned short max_vecs, blk_opf_t opf)
{
	bio->bi_next = NULL;
	bio->bi_bdev = bdev;
	bio->bi_opf = opf;
	bio->bi_flags = 0;
	bio->bi_ioprio = 0;
	bio->bi_write_hint = 0;
	bio->bi_status = 0;
	bio->bi_iter.bi_sector = 0;
	bio->bi_iter.bi_size = 0;
	bio->bi_iter.bi_idx = 0;
	bio->bi_iter.bi_bvec_done = 0;
	bio->bi_end_io = NULL;
	bio->bi_private = NULL;
#ifdef CONFIG_BLK_CGROUP
	bio->bi_blkg = NULL;
	bio->bi_issue.value = 0;
	if (bdev)
		bio_associate_blkg(bio);
#ifdef CONFIG_BLK_CGROUP_IOCOST
	bio->bi_iocost_cost = 0;
#endif
#endif
#ifdef CONFIG_BLK_INLINE_ENCRYPTION
	bio->bi_crypt_context = NULL;
#endif
#ifdef CONFIG_BLK_DEV_INTEGRITY
	bio->bi_integrity = NULL;
#endif
	bio->bi_vcnt = 0;

	atomic_set(&bio->__bi_remaining, 1);
	atomic_set(&bio->__bi_cnt, 1);
	bio->bi_cookie = BLK_QC_T_NONE;

	bio->bi_max_vecs = max_vecs;
	bio->bi_io_vec = table;
	bio->bi_pool = NULL;
}
EXPORT_SYMBOL(bio_init);

/**
 * bio_reset - reinitialize a bio
 * @bio:	bio to reset
 * @bdev:	block device to use the bio for
 * @opf:	operation and flags for bio
 *
 * Description:
 *   After calling bio_reset(), @bio will be in the same state as a freshly
 *   allocated bio returned bio bio_alloc_bioset() - the only fields that are
 *   preserved are the ones that are initialized by bio_alloc_bioset(). See
 *   comment in struct bio.
 */
void bio_reset(struct bio *bio, struct block_device *bdev, blk_opf_t opf)
{
	bio_uninit(bio);
	memset(bio, 0, BIO_RESET_BYTES);
	atomic_set(&bio->__bi_remaining, 1);
	bio->bi_bdev = bdev;
	if (bio->bi_bdev)
		bio_associate_blkg(bio);
	bio->bi_opf = opf;
}
EXPORT_SYMBOL(bio_reset);

static struct bio *__bio_chain_endio(struct bio *bio)
{
	struct bio *parent = bio->bi_private;

	if (bio->bi_status && !parent->bi_status)
		parent->bi_status = bio->bi_status;
	bio_put(bio);
	return parent;
}

static void bio_chain_endio(struct bio *bio)
{
	bio_endio(__bio_chain_endio(bio));
}

/**
 * bio_chain - chain bio completions
 * @bio: the target bio
 * @parent: the parent bio of @bio
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
	bio_inc_remaining(parent);
}
EXPORT_SYMBOL(bio_chain);

/**
 * bio_chain_and_submit - submit a bio after chaining it to another one
 * @prev: bio to chain and submit
 * @new: bio to chain to
 *
 * If @prev is non-NULL, chain it to @new and submit it.
 *
 * Return: @new.
 */
struct bio *bio_chain_and_submit(struct bio *prev, struct bio *new)
{
	if (prev) {
		bio_chain(prev, new);
		submit_bio(prev);
	}
	return new;
}

struct bio *blk_next_bio(struct bio *bio, struct block_device *bdev,
		unsigned int nr_pages, blk_opf_t opf, gfp_t gfp)
{
	return bio_chain_and_submit(bio, bio_alloc(bdev, nr_pages, opf, gfp));
}
EXPORT_SYMBOL_GPL(blk_next_bio);

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

		submit_bio_noacct(bio);
	}
}

static void punt_bios_to_rescuer(struct bio_set *bs)
{
	struct bio_list punt, nopunt;
	struct bio *bio;

	if (WARN_ON_ONCE(!bs->rescue_workqueue))
		return;
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

	while ((bio = bio_list_pop(&current->bio_list[0])))
		bio_list_add(bio->bi_pool == bs ? &punt : &nopunt, bio);
	current->bio_list[0] = nopunt;

	bio_list_init(&nopunt);
	while ((bio = bio_list_pop(&current->bio_list[1])))
		bio_list_add(bio->bi_pool == bs ? &punt : &nopunt, bio);
	current->bio_list[1] = nopunt;

	spin_lock(&bs->rescue_lock);
	bio_list_merge(&bs->rescue_list, &punt);
	spin_unlock(&bs->rescue_lock);

	queue_work(bs->rescue_workqueue, &bs->rescue_work);
}

static void bio_alloc_irq_cache_splice(struct bio_alloc_cache *cache)
{
	unsigned long flags;

	/* cache->free_list must be empty */
	if (WARN_ON_ONCE(cache->free_list))
		return;

	local_irq_save(flags);
	cache->free_list = cache->free_list_irq;
	cache->free_list_irq = NULL;
	cache->nr += cache->nr_irq;
	cache->nr_irq = 0;
	local_irq_restore(flags);
}

static struct bio *bio_alloc_percpu_cache(struct block_device *bdev,
		unsigned short nr_vecs, blk_opf_t opf, gfp_t gfp,
		struct bio_set *bs)
{
	struct bio_alloc_cache *cache;
	struct bio *bio;

	cache = per_cpu_ptr(bs->cache, get_cpu());
	if (!cache->free_list) {
		if (READ_ONCE(cache->nr_irq) >= ALLOC_CACHE_THRESHOLD)
			bio_alloc_irq_cache_splice(cache);
		if (!cache->free_list) {
			put_cpu();
			return NULL;
		}
	}
	bio = cache->free_list;
	cache->free_list = bio->bi_next;
	cache->nr--;
	put_cpu();

	bio_init(bio, bdev, nr_vecs ? bio->bi_inline_vecs : NULL, nr_vecs, opf);
	bio->bi_pool = bs;
	return bio;
}

/**
 * bio_alloc_bioset - allocate a bio for I/O
 * @bdev:	block device to allocate the bio for (can be %NULL)
 * @nr_vecs:	number of bvecs to pre-allocate
 * @opf:	operation and flags for bio
 * @gfp_mask:   the GFP_* mask given to the slab allocator
 * @bs:		the bio_set to allocate from.
 *
 * Allocate a bio from the mempools in @bs.
 *
 * If %__GFP_DIRECT_RECLAIM is set then bio_alloc will always be able to
 * allocate a bio.  This is due to the mempool guarantees.  To make this work,
 * callers must never allocate more than 1 bio at a time from the general pool.
 * Callers that need to allocate more than 1 bio must always submit the
 * previously allocated bio for IO before attempting to allocate a new one.
 * Failure to do so can cause deadlocks under memory pressure.
 *
 * Note that when running under submit_bio_noacct() (i.e. any block driver),
 * bios are not submitted until after you return - see the code in
 * submit_bio_noacct() that converts recursion into iteration, to prevent
 * stack overflows.
 *
 * This would normally mean allocating multiple bios under submit_bio_noacct()
 * would be susceptible to deadlocks, but we have
 * deadlock avoidance code that resubmits any blocked bios from a rescuer
 * thread.
 *
 * However, we do not guarantee forward progress for allocations from other
 * mempools. Doing multiple allocations from the same mempool under
 * submit_bio_noacct() should be avoided - instead, use bio_set's front_pad
 * for per bio allocations.
 *
 * Returns: Pointer to new bio on success, NULL on failure.
 */
struct bio *bio_alloc_bioset(struct block_device *bdev, unsigned short nr_vecs,
			     blk_opf_t opf, gfp_t gfp_mask,
			     struct bio_set *bs)
{
	gfp_t saved_gfp = gfp_mask;
	struct bio *bio;
	void *p;

	/* should not use nobvec bioset for nr_vecs > 0 */
	if (WARN_ON_ONCE(!mempool_initialized(&bs->bvec_pool) && nr_vecs > 0))
		return NULL;

	if (opf & REQ_ALLOC_CACHE) {
		if (bs->cache && nr_vecs <= BIO_INLINE_VECS) {
			bio = bio_alloc_percpu_cache(bdev, nr_vecs, opf,
						     gfp_mask, bs);
			if (bio)
				return bio;
			/*
			 * No cached bio available, bio returned below marked with
			 * REQ_ALLOC_CACHE to particpate in per-cpu alloc cache.
			 */
		} else {
			opf &= ~REQ_ALLOC_CACHE;
		}
	}

	/*
	 * submit_bio_noacct() converts recursion to iteration; this means if
	 * we're running beneath it, any bios we allocate and submit will not be
	 * submitted (and thus freed) until after we return.
	 *
	 * This exposes us to a potential deadlock if we allocate multiple bios
	 * from the same bio_set() while running underneath submit_bio_noacct().
	 * If we were to allocate multiple bios (say a stacking block driver
	 * that was splitting bios), we would deadlock if we exhausted the
	 * mempool's reserve.
	 *
	 * We solve this, and guarantee forward progress, with a rescuer
	 * workqueue per bio_set. If we go to allocate and there are bios on
	 * current->bio_list, we first try the allocation without
	 * __GFP_DIRECT_RECLAIM; if that fails, we punt those bios we would be
	 * blocking to the rescuer workqueue before we retry with the original
	 * gfp_flags.
	 */
	if (current->bio_list &&
	    (!bio_list_empty(&current->bio_list[0]) ||
	     !bio_list_empty(&current->bio_list[1])) &&
	    bs->rescue_workqueue)
		gfp_mask &= ~__GFP_DIRECT_RECLAIM;

	p = mempool_alloc(&bs->bio_pool, gfp_mask);
	if (!p && gfp_mask != saved_gfp) {
		punt_bios_to_rescuer(bs);
		gfp_mask = saved_gfp;
		p = mempool_alloc(&bs->bio_pool, gfp_mask);
	}
	if (unlikely(!p))
		return NULL;
	if (!mempool_is_saturated(&bs->bio_pool))
		opf &= ~REQ_ALLOC_CACHE;

	bio = p + bs->front_pad;
	if (nr_vecs > BIO_INLINE_VECS) {
		struct bio_vec *bvl = NULL;

		bvl = bvec_alloc(&bs->bvec_pool, &nr_vecs, gfp_mask);
		if (!bvl && gfp_mask != saved_gfp) {
			punt_bios_to_rescuer(bs);
			gfp_mask = saved_gfp;
			bvl = bvec_alloc(&bs->bvec_pool, &nr_vecs, gfp_mask);
		}
		if (unlikely(!bvl))
			goto err_free;

		bio_init(bio, bdev, bvl, nr_vecs, opf);
	} else if (nr_vecs) {
		bio_init(bio, bdev, bio->bi_inline_vecs, BIO_INLINE_VECS, opf);
	} else {
		bio_init(bio, bdev, NULL, 0, opf);
	}

	bio->bi_pool = bs;
	return bio;

err_free:
	mempool_free(p, &bs->bio_pool);
	return NULL;
}
EXPORT_SYMBOL(bio_alloc_bioset);

/**
 * bio_kmalloc - kmalloc a bio
 * @nr_vecs:	number of bio_vecs to allocate
 * @gfp_mask:   the GFP_* mask given to the slab allocator
 *
 * Use kmalloc to allocate a bio (including bvecs).  The bio must be initialized
 * using bio_init() before use.  To free a bio returned from this function use
 * kfree() after calling bio_uninit().  A bio returned from this function can
 * be reused by calling bio_uninit() before calling bio_init() again.
 *
 * Note that unlike bio_alloc() or bio_alloc_bioset() allocations from this
 * function are not backed by a mempool can fail.  Do not use this function
 * for allocations in the file system I/O path.
 *
 * Returns: Pointer to new bio on success, NULL on failure.
 */
struct bio *bio_kmalloc(unsigned short nr_vecs, gfp_t gfp_mask)
{
	struct bio *bio;

	if (nr_vecs > UIO_MAXIOV)
		return NULL;
	return kmalloc(struct_size(bio, bi_inline_vecs, nr_vecs), gfp_mask);
}
EXPORT_SYMBOL(bio_kmalloc);

void zero_fill_bio_iter(struct bio *bio, struct bvec_iter start)
{
	struct bio_vec bv;
	struct bvec_iter iter;

	__bio_for_each_segment(bv, bio, iter, start)
		memzero_bvec(&bv);
}
EXPORT_SYMBOL(zero_fill_bio_iter);

/**
 * bio_truncate - truncate the bio to small size of @new_size
 * @bio:	the bio to be truncated
 * @new_size:	new size for truncating the bio
 *
 * Description:
 *   Truncate the bio to new size of @new_size. If bio_op(bio) is
 *   REQ_OP_READ, zero the truncated part. This function should only
 *   be used for handling corner cases, such as bio eod.
 */
static void bio_truncate(struct bio *bio, unsigned new_size)
{
	struct bio_vec bv;
	struct bvec_iter iter;
	unsigned int done = 0;
	bool truncated = false;

	if (new_size >= bio->bi_iter.bi_size)
		return;

	if (bio_op(bio) != REQ_OP_READ)
		goto exit;

	bio_for_each_segment(bv, bio, iter) {
		if (done + bv.bv_len > new_size) {
			unsigned offset;

			if (!truncated)
				offset = new_size - done;
			else
				offset = 0;
			zero_user(bv.bv_page, bv.bv_offset + offset,
				  bv.bv_len - offset);
			truncated = true;
		}
		done += bv.bv_len;
	}

 exit:
	/*
	 * Don't touch bvec table here and make it really immutable, since
	 * fs bio user has to retrieve all pages via bio_for_each_segment_all
	 * in its .end_bio() callback.
	 *
	 * It is enough to truncate bio by updating .bi_size since we can make
	 * correct bvec with the updated .bi_size for drivers.
	 */
	bio->bi_iter.bi_size = new_size;
}

/**
 * guard_bio_eod - truncate a BIO to fit the block device
 * @bio:	bio to truncate
 *
 * This allows us to do IO even on the odd last sectors of a device, even if the
 * block size is some multiple of the physical sector size.
 *
 * We'll just truncate the bio to the size of the device, and clear the end of
 * the buffer head manually.  Truly out-of-range accesses will turn into actual
 * I/O errors, this only handles the "we need to be able to do I/O at the final
 * sector" case.
 */
void guard_bio_eod(struct bio *bio)
{
	sector_t maxsector = bdev_nr_sectors(bio->bi_bdev);

	if (!maxsector)
		return;

	/*
	 * If the *whole* IO is past the end of the device,
	 * let it through, and the IO layer will turn it into
	 * an EIO.
	 */
	if (unlikely(bio->bi_iter.bi_sector >= maxsector))
		return;

	maxsector -= bio->bi_iter.bi_sector;
	if (likely((bio->bi_iter.bi_size >> 9) <= maxsector))
		return;

	bio_truncate(bio, maxsector << 9);
}

static int __bio_alloc_cache_prune(struct bio_alloc_cache *cache,
				   unsigned int nr)
{
	unsigned int i = 0;
	struct bio *bio;

	while ((bio = cache->free_list) != NULL) {
		cache->free_list = bio->bi_next;
		cache->nr--;
		bio_free(bio);
		if (++i == nr)
			break;
	}
	return i;
}

static void bio_alloc_cache_prune(struct bio_alloc_cache *cache,
				  unsigned int nr)
{
	nr -= __bio_alloc_cache_prune(cache, nr);
	if (!READ_ONCE(cache->free_list)) {
		bio_alloc_irq_cache_splice(cache);
		__bio_alloc_cache_prune(cache, nr);
	}
}

static int bio_cpu_dead(unsigned int cpu, struct hlist_node *node)
{
	struct bio_set *bs;

	bs = hlist_entry_safe(node, struct bio_set, cpuhp_dead);
	if (bs->cache) {
		struct bio_alloc_cache *cache = per_cpu_ptr(bs->cache, cpu);

		bio_alloc_cache_prune(cache, -1U);
	}
	return 0;
}

static void bio_alloc_cache_destroy(struct bio_set *bs)
{
	int cpu;

	if (!bs->cache)
		return;

	cpuhp_state_remove_instance_nocalls(CPUHP_BIO_DEAD, &bs->cpuhp_dead);
	for_each_possible_cpu(cpu) {
		struct bio_alloc_cache *cache;

		cache = per_cpu_ptr(bs->cache, cpu);
		bio_alloc_cache_prune(cache, -1U);
	}
	free_percpu(bs->cache);
	bs->cache = NULL;
}

static inline void bio_put_percpu_cache(struct bio *bio)
{
	struct bio_alloc_cache *cache;

	cache = per_cpu_ptr(bio->bi_pool->cache, get_cpu());
	if (READ_ONCE(cache->nr_irq) + cache->nr > ALLOC_CACHE_MAX)
		goto out_free;

	if (in_task()) {
		bio_uninit(bio);
		bio->bi_next = cache->free_list;
		/* Not necessary but helps not to iopoll already freed bios */
		bio->bi_bdev = NULL;
		cache->free_list = bio;
		cache->nr++;
	} else if (in_hardirq()) {
		lockdep_assert_irqs_disabled();

		bio_uninit(bio);
		bio->bi_next = cache->free_list_irq;
		cache->free_list_irq = bio;
		cache->nr_irq++;
	} else {
		goto out_free;
	}
	put_cpu();
	return;
out_free:
	put_cpu();
	bio_free(bio);
}

/**
 * bio_put - release a reference to a bio
 * @bio:   bio to release reference to
 *
 * Description:
 *   Put a reference to a &struct bio, either one you have gotten with
 *   bio_alloc, bio_get or bio_clone_*. The last put of a bio will free it.
 **/
void bio_put(struct bio *bio)
{
	if (unlikely(bio_flagged(bio, BIO_REFFED))) {
		BUG_ON(!atomic_read(&bio->__bi_cnt));
		if (!atomic_dec_and_test(&bio->__bi_cnt))
			return;
	}
	if (bio->bi_opf & REQ_ALLOC_CACHE)
		bio_put_percpu_cache(bio);
	else
		bio_free(bio);
}
EXPORT_SYMBOL(bio_put);

static int __bio_clone(struct bio *bio, struct bio *bio_src, gfp_t gfp)
{
	bio_set_flag(bio, BIO_CLONED);
	bio->bi_ioprio = bio_src->bi_ioprio;
	bio->bi_write_hint = bio_src->bi_write_hint;
	bio->bi_iter = bio_src->bi_iter;

	if (bio->bi_bdev) {
		if (bio->bi_bdev == bio_src->bi_bdev &&
		    bio_flagged(bio_src, BIO_REMAPPED))
			bio_set_flag(bio, BIO_REMAPPED);
		bio_clone_blkg_association(bio, bio_src);
	}

	if (bio_crypt_clone(bio, bio_src, gfp) < 0)
		return -ENOMEM;
	if (bio_integrity(bio_src) &&
	    bio_integrity_clone(bio, bio_src, gfp) < 0)
		return -ENOMEM;
	return 0;
}

/**
 * bio_alloc_clone - clone a bio that shares the original bio's biovec
 * @bdev: block_device to clone onto
 * @bio_src: bio to clone from
 * @gfp: allocation priority
 * @bs: bio_set to allocate from
 *
 * Allocate a new bio that is a clone of @bio_src. The caller owns the returned
 * bio, but not the actual data it points to.
 *
 * The caller must ensure that the return bio is not freed before @bio_src.
 */
struct bio *bio_alloc_clone(struct block_device *bdev, struct bio *bio_src,
		gfp_t gfp, struct bio_set *bs)
{
	struct bio *bio;

	bio = bio_alloc_bioset(bdev, 0, bio_src->bi_opf, gfp, bs);
	if (!bio)
		return NULL;

	if (__bio_clone(bio, bio_src, gfp) < 0) {
		bio_put(bio);
		return NULL;
	}
	bio->bi_io_vec = bio_src->bi_io_vec;

	return bio;
}
EXPORT_SYMBOL(bio_alloc_clone);

/**
 * bio_init_clone - clone a bio that shares the original bio's biovec
 * @bdev: block_device to clone onto
 * @bio: bio to clone into
 * @bio_src: bio to clone from
 * @gfp: allocation priority
 *
 * Initialize a new bio in caller provided memory that is a clone of @bio_src.
 * The caller owns the returned bio, but not the actual data it points to.
 *
 * The caller must ensure that @bio_src is not freed before @bio.
 */
int bio_init_clone(struct block_device *bdev, struct bio *bio,
		struct bio *bio_src, gfp_t gfp)
{
	int ret;

	bio_init(bio, bdev, bio_src->bi_io_vec, 0, bio_src->bi_opf);
	ret = __bio_clone(bio, bio_src, gfp);
	if (ret)
		bio_uninit(bio);
	return ret;
}
EXPORT_SYMBOL(bio_init_clone);

/**
 * bio_full - check if the bio is full
 * @bio:	bio to check
 * @len:	length of one segment to be added
 *
 * Return true if @bio is full and one segment with @len bytes can't be
 * added to the bio, otherwise return false
 */
static inline bool bio_full(struct bio *bio, unsigned len)
{
	if (bio->bi_vcnt >= bio->bi_max_vecs)
		return true;
	if (bio->bi_iter.bi_size > UINT_MAX - len)
		return true;
	return false;
}

static bool bvec_try_merge_page(struct bio_vec *bv, struct page *page,
		unsigned int len, unsigned int off, bool *same_page)
{
	size_t bv_end = bv->bv_offset + bv->bv_len;
	phys_addr_t vec_end_addr = page_to_phys(bv->bv_page) + bv_end - 1;
	phys_addr_t page_addr = page_to_phys(page);

	if (vec_end_addr + 1 != page_addr + off)
		return false;
	if (xen_domain() && !xen_biovec_phys_mergeable(bv, page))
		return false;
	if (!zone_device_pages_have_same_pgmap(bv->bv_page, page))
		return false;

	*same_page = ((vec_end_addr & PAGE_MASK) == ((page_addr + off) &
		     PAGE_MASK));
	if (!*same_page) {
		if (IS_ENABLED(CONFIG_KMSAN))
			return false;
		if (bv->bv_page + bv_end / PAGE_SIZE != page + off / PAGE_SIZE)
			return false;
	}

	bv->bv_len += len;
	return true;
}

/*
 * Try to merge a page into a segment, while obeying the hardware segment
 * size limit.
 *
 * This is kept around for the integrity metadata, which is still tries
 * to build the initial bio to the hardware limit and doesn't have proper
 * helpers to split.  Hopefully this will go away soon.
 */
bool bvec_try_merge_hw_page(struct request_queue *q, struct bio_vec *bv,
		struct page *page, unsigned len, unsigned offset,
		bool *same_page)
{
	unsigned long mask = queue_segment_boundary(q);
	phys_addr_t addr1 = bvec_phys(bv);
	phys_addr_t addr2 = page_to_phys(page) + offset + len - 1;

	if ((addr1 | mask) != (addr2 | mask))
		return false;
	if (len > queue_max_segment_size(q) - bv->bv_len)
		return false;
	return bvec_try_merge_page(bv, page, len, offset, same_page);
}

/**
 * __bio_add_page - add page(s) to a bio in a new segment
 * @bio: destination bio
 * @page: start page to add
 * @len: length of the data to add, may cross pages
 * @off: offset of the data relative to @page, may cross pages
 *
 * Add the data at @page + @off to @bio as a new bvec.  The caller must ensure
 * that @bio has space for another bvec.
 */
void __bio_add_page(struct bio *bio, struct page *page,
		unsigned int len, unsigned int off)
{
	WARN_ON_ONCE(bio_flagged(bio, BIO_CLONED));
	WARN_ON_ONCE(bio_full(bio, len));

	bvec_set_page(&bio->bi_io_vec[bio->bi_vcnt], page, len, off);
	bio->bi_iter.bi_size += len;
	bio->bi_vcnt++;
}
EXPORT_SYMBOL_GPL(__bio_add_page);

/**
 *	bio_add_page	-	attempt to add page(s) to bio
 *	@bio: destination bio
 *	@page: start page to add
 *	@len: vec entry length, may cross pages
 *	@offset: vec entry offset relative to @page, may cross pages
 *
 *	Attempt to add page(s) to the bio_vec maplist. This will only fail
 *	if either bio->bi_vcnt == bio->bi_max_vecs or it's a cloned bio.
 */
int bio_add_page(struct bio *bio, struct page *page,
		 unsigned int len, unsigned int offset)
{
	bool same_page = false;

	if (WARN_ON_ONCE(bio_flagged(bio, BIO_CLONED)))
		return 0;
	if (bio->bi_iter.bi_size > UINT_MAX - len)
		return 0;

	if (bio->bi_vcnt > 0 &&
	    bvec_try_merge_page(&bio->bi_io_vec[bio->bi_vcnt - 1],
				page, len, offset, &same_page)) {
		bio->bi_iter.bi_size += len;
		return len;
	}

	if (bio->bi_vcnt >= bio->bi_max_vecs)
		return 0;
	__bio_add_page(bio, page, len, offset);
	return len;
}
EXPORT_SYMBOL(bio_add_page);

void bio_add_folio_nofail(struct bio *bio, struct folio *folio, size_t len,
			  size_t off)
{
	WARN_ON_ONCE(len > UINT_MAX);
	WARN_ON_ONCE(off > UINT_MAX);
	__bio_add_page(bio, &folio->page, len, off);
}
EXPORT_SYMBOL_GPL(bio_add_folio_nofail);

/**
 * bio_add_folio - Attempt to add part of a folio to a bio.
 * @bio: BIO to add to.
 * @folio: Folio to add.
 * @len: How many bytes from the folio to add.
 * @off: First byte in this folio to add.
 *
 * Filesystems that use folios can call this function instead of calling
 * bio_add_page() for each page in the folio.  If @off is bigger than
 * PAGE_SIZE, this function can create a bio_vec that starts in a page
 * after the bv_page.  BIOs do not support folios that are 4GiB or larger.
 *
 * Return: Whether the addition was successful.
 */
bool bio_add_folio(struct bio *bio, struct folio *folio, size_t len,
		   size_t off)
{
	if (len > UINT_MAX || off > UINT_MAX)
		return false;
	return bio_add_page(bio, &folio->page, len, off) > 0;
}
EXPORT_SYMBOL(bio_add_folio);

void __bio_release_pages(struct bio *bio, bool mark_dirty)
{
	struct folio_iter fi;

	bio_for_each_folio_all(fi, bio) {
		size_t nr_pages;

		if (mark_dirty) {
			folio_lock(fi.folio);
			folio_mark_dirty(fi.folio);
			folio_unlock(fi.folio);
		}
		nr_pages = (fi.offset + fi.length - 1) / PAGE_SIZE -
			   fi.offset / PAGE_SIZE + 1;
		unpin_user_folio(fi.folio, nr_pages);
	}
}
EXPORT_SYMBOL_GPL(__bio_release_pages);

void bio_iov_bvec_set(struct bio *bio, const struct iov_iter *iter)
{
	WARN_ON_ONCE(bio->bi_max_vecs);

	bio->bi_vcnt = iter->nr_segs;
	bio->bi_io_vec = (struct bio_vec *)iter->bvec;
	bio->bi_iter.bi_bvec_done = iter->iov_offset;
	bio->bi_iter.bi_size = iov_iter_count(iter);
	bio_set_flag(bio, BIO_CLONED);
}

static int bio_iov_add_folio(struct bio *bio, struct folio *folio, size_t len,
			     size_t offset)
{
	bool same_page = false;

	if (WARN_ON_ONCE(bio->bi_iter.bi_size > UINT_MAX - len))
		return -EIO;

	if (bio->bi_vcnt > 0 &&
	    bvec_try_merge_page(&bio->bi_io_vec[bio->bi_vcnt - 1],
				folio_page(folio, 0), len, offset,
				&same_page)) {
		bio->bi_iter.bi_size += len;
		if (same_page && bio_flagged(bio, BIO_PAGE_PINNED))
			unpin_user_folio(folio, 1);
		return 0;
	}
	bio_add_folio_nofail(bio, folio, len, offset);
	return 0;
}

static unsigned int get_contig_folio_len(unsigned int *num_pages,
					 struct page **pages, unsigned int i,
					 struct folio *folio, size_t left,
					 size_t offset)
{
	size_t bytes = left;
	size_t contig_sz = min_t(size_t, PAGE_SIZE - offset, bytes);
	unsigned int j;

	/*
	 * We might COW a single page in the middle of
	 * a large folio, so we have to check that all
	 * pages belong to the same folio.
	 */
	bytes -= contig_sz;
	for (j = i + 1; j < i + *num_pages; j++) {
		size_t next = min_t(size_t, PAGE_SIZE, bytes);

		if (page_folio(pages[j]) != folio ||
		    pages[j] != pages[j - 1] + 1) {
			break;
		}
		contig_sz += next;
		bytes -= next;
	}
	*num_pages = j - i;

	return contig_sz;
}

#define PAGE_PTRS_PER_BVEC     (sizeof(struct bio_vec) / sizeof(struct page *))

/**
 * __bio_iov_iter_get_pages - pin user or kernel pages and add them to a bio
 * @bio: bio to add pages to
 * @iter: iov iterator describing the region to be mapped
 *
 * Extracts pages from *iter and appends them to @bio's bvec array.  The pages
 * will have to be cleaned up in the way indicated by the BIO_PAGE_PINNED flag.
 * For a multi-segment *iter, this function only adds pages from the next
 * non-empty segment of the iov iterator.
 */
static int __bio_iov_iter_get_pages(struct bio *bio, struct iov_iter *iter)
{
	iov_iter_extraction_t extraction_flags = 0;
	unsigned short nr_pages = bio->bi_max_vecs - bio->bi_vcnt;
	unsigned short entries_left = bio->bi_max_vecs - bio->bi_vcnt;
	struct bio_vec *bv = bio->bi_io_vec + bio->bi_vcnt;
	struct page **pages = (struct page **)bv;
	ssize_t size;
	unsigned int num_pages, i = 0;
	size_t offset, folio_offset, left, len;
	int ret = 0;

	/*
	 * Move page array up in the allocated memory for the bio vecs as far as
	 * possible so that we can start filling biovecs from the beginning
	 * without overwriting the temporary page array.
	 */
	BUILD_BUG_ON(PAGE_PTRS_PER_BVEC < 2);
	pages += entries_left * (PAGE_PTRS_PER_BVEC - 1);

	if (bio->bi_bdev && blk_queue_pci_p2pdma(bio->bi_bdev->bd_disk->queue))
		extraction_flags |= ITER_ALLOW_P2PDMA;

	/*
	 * Each segment in the iov is required to be a block size multiple.
	 * However, we may not be able to get the entire segment if it spans
	 * more pages than bi_max_vecs allows, so we have to ALIGN_DOWN the
	 * result to ensure the bio's total size is correct. The remainder of
	 * the iov data will be picked up in the next bio iteration.
	 */
	size = iov_iter_extract_pages(iter, &pages,
				      UINT_MAX - bio->bi_iter.bi_size,
				      nr_pages, extraction_flags, &offset);
	if (unlikely(size <= 0))
		return size ? size : -EFAULT;

	nr_pages = DIV_ROUND_UP(offset + size, PAGE_SIZE);

	if (bio->bi_bdev) {
		size_t trim = size & (bdev_logical_block_size(bio->bi_bdev) - 1);
		iov_iter_revert(iter, trim);
		size -= trim;
	}

	if (unlikely(!size)) {
		ret = -EFAULT;
		goto out;
	}

	for (left = size, i = 0; left > 0; left -= len, i += num_pages) {
		struct page *page = pages[i];
		struct folio *folio = page_folio(page);

		folio_offset = ((size_t)folio_page_idx(folio, page) <<
			       PAGE_SHIFT) + offset;

		len = min(folio_size(folio) - folio_offset, left);

		num_pages = DIV_ROUND_UP(offset + len, PAGE_SIZE);

		if (num_pages > 1)
			len = get_contig_folio_len(&num_pages, pages, i,
						   folio, left, offset);

		bio_iov_add_folio(bio, folio, len, folio_offset);
		offset = 0;
	}

	iov_iter_revert(iter, left);
out:
	while (i < nr_pages)
		bio_release_page(bio, pages[i++]);

	return ret;
}

/**
 * bio_iov_iter_get_pages - add user or kernel pages to a bio
 * @bio: bio to add pages to
 * @iter: iov iterator describing the region to be added
 *
 * This takes either an iterator pointing to user memory, or one pointing to
 * kernel pages (BVEC iterator). If we're adding user pages, we pin them and
 * map them into the kernel. On IO completion, the caller should put those
 * pages. For bvec based iterators bio_iov_iter_get_pages() uses the provided
 * bvecs rather than copying them. Hence anyone issuing kiocb based IO needs
 * to ensure the bvecs and pages stay referenced until the submitted I/O is
 * completed by a call to ->ki_complete() or returns with an error other than
 * -EIOCBQUEUED. The caller needs to check if the bio is flagged BIO_NO_PAGE_REF
 * on IO completion. If it isn't, then pages should be released.
 *
 * The function tries, but does not guarantee, to pin as many pages as
 * fit into the bio, or are requested in @iter, whatever is smaller. If
 * MM encounters an error pinning the requested pages, it stops. Error
 * is returned only if 0 pages could be pinned.
 */
int bio_iov_iter_get_pages(struct bio *bio, struct iov_iter *iter)
{
	int ret = 0;

	if (WARN_ON_ONCE(bio_flagged(bio, BIO_CLONED)))
		return -EIO;

	if (iov_iter_is_bvec(iter)) {
		bio_iov_bvec_set(bio, iter);
		iov_iter_advance(iter, bio->bi_iter.bi_size);
		return 0;
	}

	if (iov_iter_extract_will_pin(iter))
		bio_set_flag(bio, BIO_PAGE_PINNED);
	do {
		ret = __bio_iov_iter_get_pages(bio, iter);
	} while (!ret && iov_iter_count(iter) && !bio_full(bio, 0));

	return bio->bi_vcnt ? 0 : ret;
}
EXPORT_SYMBOL_GPL(bio_iov_iter_get_pages);

static void submit_bio_wait_endio(struct bio *bio)
{
	complete(bio->bi_private);
}

/**
 * submit_bio_wait - submit a bio, and wait until it completes
 * @bio: The &struct bio which describes the I/O
 *
 * Simple wrapper around submit_bio(). Returns 0 on success, or the error from
 * bio_endio() on failure.
 *
 * WARNING: Unlike to how submit_bio() is usually used, this function does not
 * result in bio reference to be consumed. The caller must drop the reference
 * on his own.
 */
int submit_bio_wait(struct bio *bio)
{
	DECLARE_COMPLETION_ONSTACK_MAP(done,
			bio->bi_bdev->bd_disk->lockdep_map);

	bio->bi_private = &done;
	bio->bi_end_io = submit_bio_wait_endio;
	bio->bi_opf |= REQ_SYNC;
	submit_bio(bio);
	blk_wait_io(&done);

	return blk_status_to_errno(bio->bi_status);
}
EXPORT_SYMBOL(submit_bio_wait);

static void bio_wait_end_io(struct bio *bio)
{
	complete(bio->bi_private);
	bio_put(bio);
}

/*
 * bio_await_chain - ends @bio and waits for every chained bio to complete
 */
void bio_await_chain(struct bio *bio)
{
	DECLARE_COMPLETION_ONSTACK_MAP(done,
			bio->bi_bdev->bd_disk->lockdep_map);

	bio->bi_private = &done;
	bio->bi_end_io = bio_wait_end_io;
	bio_endio(bio);
	blk_wait_io(&done);
}

void __bio_advance(struct bio *bio, unsigned bytes)
{
	if (bio_integrity(bio))
		bio_integrity_advance(bio, bytes);

	bio_crypt_advance(bio, bytes);
	bio_advance_iter(bio, &bio->bi_iter, bytes);
}
EXPORT_SYMBOL(__bio_advance);

void bio_copy_data_iter(struct bio *dst, struct bvec_iter *dst_iter,
			struct bio *src, struct bvec_iter *src_iter)
{
	while (src_iter->bi_size && dst_iter->bi_size) {
		struct bio_vec src_bv = bio_iter_iovec(src, *src_iter);
		struct bio_vec dst_bv = bio_iter_iovec(dst, *dst_iter);
		unsigned int bytes = min(src_bv.bv_len, dst_bv.bv_len);
		void *src_buf = bvec_kmap_local(&src_bv);
		void *dst_buf = bvec_kmap_local(&dst_bv);

		memcpy(dst_buf, src_buf, bytes);

		kunmap_local(dst_buf);
		kunmap_local(src_buf);

		bio_advance_iter_single(src, src_iter, bytes);
		bio_advance_iter_single(dst, dst_iter, bytes);
	}
}
EXPORT_SYMBOL(bio_copy_data_iter);

/**
 * bio_copy_data - copy contents of data buffers from one bio to another
 * @src: source bio
 * @dst: destination bio
 *
 * Stops when it reaches the end of either @src or @dst - that is, copies
 * min(src->bi_size, dst->bi_size) bytes (or the equivalent for lists of bios).
 */
void bio_copy_data(struct bio *dst, struct bio *src)
{
	struct bvec_iter src_iter = src->bi_iter;
	struct bvec_iter dst_iter = dst->bi_iter;

	bio_copy_data_iter(dst, &dst_iter, src, &src_iter);
}
EXPORT_SYMBOL(bio_copy_data);

void bio_free_pages(struct bio *bio)
{
	struct bio_vec *bvec;
	struct bvec_iter_all iter_all;

	bio_for_each_segment_all(bvec, bio, iter_all)
		__free_page(bvec->bv_page);
}
EXPORT_SYMBOL(bio_free_pages);

/*
 * bio_set_pages_dirty() and bio_check_pages_dirty() are support functions
 * for performing direct-IO in BIOs.
 *
 * The problem is that we cannot run folio_mark_dirty() from interrupt context
 * because the required locks are not interrupt-safe.  So what we can do is to
 * mark the pages dirty _before_ performing IO.  And in interrupt context,
 * check that the pages are still dirty.   If so, fine.  If not, redirty them
 * in process context.
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
	struct folio_iter fi;

	bio_for_each_folio_all(fi, bio) {
		folio_lock(fi.folio);
		folio_mark_dirty(fi.folio);
		folio_unlock(fi.folio);
	}
}
EXPORT_SYMBOL_GPL(bio_set_pages_dirty);

/*
 * bio_check_pages_dirty() will check that all the BIO's pages are still dirty.
 * If they are, then fine.  If, however, some pages are clean then they must
 * have been written out during the direct-IO read.  So we take another ref on
 * the BIO and re-dirty the pages in process context.
 *
 * It is expected that bio_check_pages_dirty() will wholly own the BIO from
 * here on.  It will unpin each page and will run one bio_put() against the
 * BIO.
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
	struct bio *bio, *next;

	spin_lock_irq(&bio_dirty_lock);
	next = bio_dirty_list;
	bio_dirty_list = NULL;
	spin_unlock_irq(&bio_dirty_lock);

	while ((bio = next) != NULL) {
		next = bio->bi_private;

		bio_release_pages(bio, true);
		bio_put(bio);
	}
}

void bio_check_pages_dirty(struct bio *bio)
{
	struct folio_iter fi;
	unsigned long flags;

	bio_for_each_folio_all(fi, bio) {
		if (!folio_test_dirty(fi.folio))
			goto defer;
	}

	bio_release_pages(bio, false);
	bio_put(bio);
	return;
defer:
	spin_lock_irqsave(&bio_dirty_lock, flags);
	bio->bi_private = bio_dirty_list;
	bio_dirty_list = bio;
	spin_unlock_irqrestore(&bio_dirty_lock, flags);
	schedule_work(&bio_dirty_work);
}
EXPORT_SYMBOL_GPL(bio_check_pages_dirty);

static inline bool bio_remaining_done(struct bio *bio)
{
	/*
	 * If we're not chaining, then ->__bi_remaining is always 1 and
	 * we always end io on the first invocation.
	 */
	if (!bio_flagged(bio, BIO_CHAIN))
		return true;

	BUG_ON(atomic_read(&bio->__bi_remaining) <= 0);

	if (atomic_dec_and_test(&bio->__bi_remaining)) {
		bio_clear_flag(bio, BIO_CHAIN);
		return true;
	}

	return false;
}

/**
 * bio_endio - end I/O on a bio
 * @bio:	bio
 *
 * Description:
 *   bio_endio() will end I/O on the whole bio. bio_endio() is the preferred
 *   way to end I/O on a bio. No one should call bi_end_io() directly on a
 *   bio unless they own it and thus know that it has an end_io function.
 *
 *   bio_endio() can be called several times on a bio that has been chained
 *   using bio_chain().  The ->bi_end_io() function will only be called the
 *   last time.
 **/
void bio_endio(struct bio *bio)
{
again:
	if (!bio_remaining_done(bio))
		return;
	if (!bio_integrity_endio(bio))
		return;

	blk_zone_bio_endio(bio);

	rq_qos_done_bio(bio);

	if (bio->bi_bdev && bio_flagged(bio, BIO_TRACE_COMPLETION)) {
		trace_block_bio_complete(bdev_get_queue(bio->bi_bdev), bio);
		bio_clear_flag(bio, BIO_TRACE_COMPLETION);
	}

	/*
	 * Need to have a real endio function for chained bios, otherwise
	 * various corner cases will break (like stacking block devices that
	 * save/restore bi_end_io) - however, we want to avoid unbounded
	 * recursion and blowing the stack. Tail call optimization would
	 * handle this, but compiling with frame pointers also disables
	 * gcc's sibling call optimization.
	 */
	if (bio->bi_end_io == bio_chain_endio) {
		bio = __bio_chain_endio(bio);
		goto again;
	}

#ifdef CONFIG_BLK_CGROUP
	/*
	 * Release cgroup info.  We shouldn't have to do this here, but quite
	 * a few callers of bio_init fail to call bio_uninit, so we cover up
	 * for that here at least for now.
	 */
	if (bio->bi_blkg) {
		blkg_put(bio->bi_blkg);
		bio->bi_blkg = NULL;
	}
#endif

	if (bio->bi_end_io)
		bio->bi_end_io(bio);
}
EXPORT_SYMBOL(bio_endio);

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
 * to @bio's bi_io_vec. It is the caller's responsibility to ensure that
 * neither @bio nor @bs are freed before the split bio.
 */
struct bio *bio_split(struct bio *bio, int sectors,
		      gfp_t gfp, struct bio_set *bs)
{
	struct bio *split;

	if (WARN_ON_ONCE(sectors <= 0))
		return ERR_PTR(-EINVAL);
	if (WARN_ON_ONCE(sectors >= bio_sectors(bio)))
		return ERR_PTR(-EINVAL);

	/* Zone append commands cannot be split */
	if (WARN_ON_ONCE(bio_op(bio) == REQ_OP_ZONE_APPEND))
		return ERR_PTR(-EINVAL);

	/* atomic writes cannot be split */
	if (bio->bi_opf & REQ_ATOMIC)
		return ERR_PTR(-EINVAL);

	split = bio_alloc_clone(bio->bi_bdev, bio, gfp, bs);
	if (!split)
		return ERR_PTR(-ENOMEM);

	split->bi_iter.bi_size = sectors << 9;

	if (bio_integrity(split))
		bio_integrity_trim(split);

	bio_advance(bio, split->bi_iter.bi_size);

	if (bio_flagged(bio, BIO_TRACE_COMPLETION))
		bio_set_flag(split, BIO_TRACE_COMPLETION);

	return split;
}
EXPORT_SYMBOL(bio_split);

/**
 * bio_trim - trim a bio
 * @bio:	bio to trim
 * @offset:	number of sectors to trim from the front of @bio
 * @size:	size we want to trim @bio to, in sectors
 *
 * This function is typically used for bios that are cloned and submitted
 * to the underlying device in parts.
 */
void bio_trim(struct bio *bio, sector_t offset, sector_t size)
{
	/* We should never trim an atomic write */
	if (WARN_ON_ONCE(bio->bi_opf & REQ_ATOMIC && size))
		return;

	if (WARN_ON_ONCE(offset > BIO_MAX_SECTORS || size > BIO_MAX_SECTORS ||
			 offset + size > bio_sectors(bio)))
		return;

	size <<= 9;
	if (offset == 0 && size == bio->bi_iter.bi_size)
		return;

	bio_advance(bio, offset << 9);
	bio->bi_iter.bi_size = size;

	if (bio_integrity(bio))
		bio_integrity_trim(bio);
}
EXPORT_SYMBOL_GPL(bio_trim);

/*
 * create memory pools for biovec's in a bio_set.
 * use the global biovec slabs created for general use.
 */
int biovec_init_pool(mempool_t *pool, int pool_entries)
{
	struct biovec_slab *bp = bvec_slabs + ARRAY_SIZE(bvec_slabs) - 1;

	return mempool_init_slab_pool(pool, pool_entries, bp->slab);
}

/*
 * bioset_exit - exit a bioset initialized with bioset_init()
 *
 * May be called on a zeroed but uninitialized bioset (i.e. allocated with
 * kzalloc()).
 */
void bioset_exit(struct bio_set *bs)
{
	bio_alloc_cache_destroy(bs);
	if (bs->rescue_workqueue)
		destroy_workqueue(bs->rescue_workqueue);
	bs->rescue_workqueue = NULL;

	mempool_exit(&bs->bio_pool);
	mempool_exit(&bs->bvec_pool);

	bioset_integrity_free(bs);
	if (bs->bio_slab)
		bio_put_slab(bs);
	bs->bio_slab = NULL;
}
EXPORT_SYMBOL(bioset_exit);

/**
 * bioset_init - Initialize a bio_set
 * @bs:		pool to initialize
 * @pool_size:	Number of bio and bio_vecs to cache in the mempool
 * @front_pad:	Number of bytes to allocate in front of the returned bio
 * @flags:	Flags to modify behavior, currently %BIOSET_NEED_BVECS
 *              and %BIOSET_NEED_RESCUER
 *
 * Description:
 *    Set up a bio_set to be used with @bio_alloc_bioset. Allows the caller
 *    to ask for a number of bytes to be allocated in front of the bio.
 *    Front pad allocation is useful for embedding the bio inside
 *    another structure, to avoid allocating extra data to go with the bio.
 *    Note that the bio must be embedded at the END of that structure always,
 *    or things will break badly.
 *    If %BIOSET_NEED_BVECS is set in @flags, a separate pool will be allocated
 *    for allocating iovecs.  This pool is not needed e.g. for bio_init_clone().
 *    If %BIOSET_NEED_RESCUER is set, a workqueue is created which can be used
 *    to dispatch queued requests when the mempool runs out of space.
 *
 */
int bioset_init(struct bio_set *bs,
		unsigned int pool_size,
		unsigned int front_pad,
		int flags)
{
	bs->front_pad = front_pad;
	if (flags & BIOSET_NEED_BVECS)
		bs->back_pad = BIO_INLINE_VECS * sizeof(struct bio_vec);
	else
		bs->back_pad = 0;

	spin_lock_init(&bs->rescue_lock);
	bio_list_init(&bs->rescue_list);
	INIT_WORK(&bs->rescue_work, bio_alloc_rescue);

	bs->bio_slab = bio_find_or_create_slab(bs);
	if (!bs->bio_slab)
		return -ENOMEM;

	if (mempool_init_slab_pool(&bs->bio_pool, pool_size, bs->bio_slab))
		goto bad;

	if ((flags & BIOSET_NEED_BVECS) &&
	    biovec_init_pool(&bs->bvec_pool, pool_size))
		goto bad;

	if (flags & BIOSET_NEED_RESCUER) {
		bs->rescue_workqueue = alloc_workqueue("bioset",
							WQ_MEM_RECLAIM, 0);
		if (!bs->rescue_workqueue)
			goto bad;
	}
	if (flags & BIOSET_PERCPU_CACHE) {
		bs->cache = alloc_percpu(struct bio_alloc_cache);
		if (!bs->cache)
			goto bad;
		cpuhp_state_add_instance_nocalls(CPUHP_BIO_DEAD, &bs->cpuhp_dead);
	}

	return 0;
bad:
	bioset_exit(bs);
	return -ENOMEM;
}
EXPORT_SYMBOL(bioset_init);

static int __init init_bio(void)
{
	int i;

	BUILD_BUG_ON(BIO_FLAG_LAST > 8 * sizeof_field(struct bio, bi_flags));

	bio_integrity_init();

	for (i = 0; i < ARRAY_SIZE(bvec_slabs); i++) {
		struct biovec_slab *bvs = bvec_slabs + i;

		bvs->slab = kmem_cache_create(bvs->name,
				bvs->nr_vecs * sizeof(struct bio_vec), 0,
				SLAB_HWCACHE_ALIGN | SLAB_PANIC, NULL);
	}

	cpuhp_setup_state_multi(CPUHP_BIO_DEAD, "block/bio:dead", NULL,
					bio_cpu_dead);

	if (bioset_init(&fs_bio_set, BIO_POOL_SIZE, 0,
			BIOSET_NEED_BVECS | BIOSET_PERCPU_CACHE))
		panic("bio: can't allocate bios\n");

	if (bioset_integrity_create(&fs_bio_set, BIO_POOL_SIZE))
		panic("bio: can't create integrity pool\n");

	return 0;
}
subsys_initcall(init_bio);
