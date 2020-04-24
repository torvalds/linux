// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2001 Jens Axboe <axboe@kernel.dk>
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
#include <linux/blk-cgroup.h>
#include <linux/highmem.h>
#include <linux/sched/sysctl.h>

#include <trace/events/block.h>
#include "blk.h"
#include "blk-rq-qos.h"

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
#define BV(x, n) { .nr_vecs = x, .name = "biovec-"#n }
static struct biovec_slab bvec_slabs[BVEC_POOL_NR] __read_mostly = {
	BV(1, 1), BV(4, 4), BV(16, 16), BV(64, 64), BV(128, 128), BV(BIO_MAX_PAGES, max),
};
#undef BV

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
	return bvec_slabs[--idx].nr_vecs;
}

void bvec_free(mempool_t *pool, struct bio_vec *bv, unsigned int idx)
{
	if (!idx)
		return;
	idx--;

	BIO_BUG_ON(idx >= BVEC_POOL_NR);

	if (idx == BVEC_POOL_MAX) {
		mempool_free(bv, pool);
	} else {
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
	if (*idx == BVEC_POOL_MAX) {
fallback:
		bvl = mempool_alloc(pool, gfp_mask);
	} else {
		struct biovec_slab *bvs = bvec_slabs + *idx;
		gfp_t __gfp_mask = gfp_mask & ~(__GFP_DIRECT_RECLAIM | __GFP_IO);

		/*
		 * Make this allocation restricted and don't dump info on
		 * allocation failures, since we'll fallback to the mempool
		 * in case of failure.
		 */
		__gfp_mask |= __GFP_NOMEMALLOC | __GFP_NORETRY | __GFP_NOWARN;

		/*
		 * Try a slab allocation. If this fails and __GFP_DIRECT_RECLAIM
		 * is set, retry with the 1-entry mempool
		 */
		bvl = kmem_cache_alloc(bvs->slab, __gfp_mask);
		if (unlikely(!bvl && (gfp_mask & __GFP_DIRECT_RECLAIM))) {
			*idx = BVEC_POOL_MAX;
			goto fallback;
		}
	}

	(*idx)++;
	return bvl;
}

void bio_uninit(struct bio *bio)
{
	bio_disassociate_blkg(bio);

	if (bio_integrity(bio))
		bio_integrity_free(bio);
}
EXPORT_SYMBOL(bio_uninit);

static void bio_free(struct bio *bio)
{
	struct bio_set *bs = bio->bi_pool;
	void *p;

	bio_uninit(bio);

	if (bs) {
		bvec_free(&bs->bvec_pool, bio->bi_io_vec, BVEC_POOL_IDX(bio));

		/*
		 * If we have front padding, adjust the bio pointer before freeing
		 */
		p = bio;
		p -= bs->front_pad;

		mempool_free(p, &bs->bio_pool);
	} else {
		/* Bio was allocated by bio_kmalloc() */
		kfree(bio);
	}
}

/*
 * Users of this function have their own bio allocation. Subsequently,
 * they must remember to pair any call to bio_init() with bio_uninit()
 * when IO has completed, or when the bio is released.
 */
void bio_init(struct bio *bio, struct bio_vec *table,
	      unsigned short max_vecs)
{
	memset(bio, 0, sizeof(*bio));
	atomic_set(&bio->__bi_remaining, 1);
	atomic_set(&bio->__bi_cnt, 1);

	bio->bi_io_vec = table;
	bio->bi_max_vecs = max_vecs;
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

	bio_uninit(bio);

	memset(bio, 0, BIO_RESET_BYTES);
	bio->bi_flags = flags;
	atomic_set(&bio->__bi_remaining, 1);
}
EXPORT_SYMBOL(bio_reset);

static struct bio *__bio_chain_endio(struct bio *bio)
{
	struct bio *parent = bio->bi_private;

	if (!parent->bi_status)
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
	bio_inc_remaining(parent);
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

/**
 * bio_alloc_bioset - allocate a bio for I/O
 * @gfp_mask:   the GFP_* mask given to the slab allocator
 * @nr_iovecs:	number of iovecs to pre-allocate
 * @bs:		the bio_set to allocate from.
 *
 * Description:
 *   If @bs is NULL, uses kmalloc() to allocate the bio; else the allocation is
 *   backed by the @bs's mempool.
 *
 *   When @bs is not NULL, if %__GFP_DIRECT_RECLAIM is set then bio_alloc will
 *   always be able to allocate a bio. This is due to the mempool guarantees.
 *   To make this work, callers must never allocate more than 1 bio at a time
 *   from this pool. Callers that need to allocate more than 1 bio must always
 *   submit the previously allocated bio for IO before attempting to allocate
 *   a new one. Failure to do so can cause deadlocks under memory pressure.
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
struct bio *bio_alloc_bioset(gfp_t gfp_mask, unsigned int nr_iovecs,
			     struct bio_set *bs)
{
	gfp_t saved_gfp = gfp_mask;
	unsigned front_pad;
	unsigned inline_vecs;
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
		if (WARN_ON_ONCE(!mempool_initialized(&bs->bvec_pool) &&
				 nr_iovecs > 0))
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
		 * without __GFP_DIRECT_RECLAIM; if that fails, we punt those
		 * bios we would be blocking to the rescuer workqueue before
		 * we retry with the original gfp_flags.
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

		front_pad = bs->front_pad;
		inline_vecs = BIO_INLINE_VECS;
	}

	if (unlikely(!p))
		return NULL;

	bio = p + front_pad;
	bio_init(bio, NULL, 0);

	if (nr_iovecs > inline_vecs) {
		unsigned long idx = 0;

		bvl = bvec_alloc(gfp_mask, nr_iovecs, &idx, &bs->bvec_pool);
		if (!bvl && gfp_mask != saved_gfp) {
			punt_bios_to_rescuer(bs);
			gfp_mask = saved_gfp;
			bvl = bvec_alloc(gfp_mask, nr_iovecs, &idx, &bs->bvec_pool);
		}

		if (unlikely(!bvl))
			goto err_free;

		bio->bi_flags |= idx << BVEC_POOL_OFFSET;
	} else if (nr_iovecs) {
		bvl = bio->bi_inline_vecs;
	}

	bio->bi_pool = bs;
	bio->bi_max_vecs = nr_iovecs;
	bio->bi_io_vec = bvl;
	return bio;

err_free:
	mempool_free(p, &bs->bio_pool);
	return NULL;
}
EXPORT_SYMBOL(bio_alloc_bioset);

void zero_fill_bio_iter(struct bio *bio, struct bvec_iter start)
{
	unsigned long flags;
	struct bio_vec bv;
	struct bvec_iter iter;

	__bio_for_each_segment(bv, bio, iter, start) {
		char *data = bvec_kmap_irq(&bv, &flags);
		memset(data, 0, bv.bv_len);
		flush_dcache_page(bv.bv_page);
		bvec_kunmap_irq(data, &flags);
	}
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
void bio_truncate(struct bio *bio, unsigned new_size)
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
			zero_user(bv.bv_page, offset, bv.bv_len - offset);
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
	sector_t maxsector;
	struct hd_struct *part;

	rcu_read_lock();
	part = __disk_get_part(bio->bi_disk, bio->bi_partno);
	if (part)
		maxsector = part_nr_sects_read(part);
	else
		maxsector = get_capacity(bio->bi_disk);
	rcu_read_unlock();

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
	if (!bio_flagged(bio, BIO_REFFED))
		bio_free(bio);
	else {
		BIO_BUG_ON(!atomic_read(&bio->__bi_cnt));

		/*
		 * last put frees it
		 */
		if (atomic_dec_and_test(&bio->__bi_cnt))
			bio_free(bio);
	}
}
EXPORT_SYMBOL(bio_put);

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
	BUG_ON(bio->bi_pool && BVEC_POOL_IDX(bio));

	/*
	 * most users will be overriding ->bi_disk with a new target,
	 * so we don't set nor calculate new physical/hw segment counts here
	 */
	bio->bi_disk = bio_src->bi_disk;
	bio->bi_partno = bio_src->bi_partno;
	bio_set_flag(bio, BIO_CLONED);
	if (bio_flagged(bio_src, BIO_THROTTLED))
		bio_set_flag(bio, BIO_THROTTLED);
	bio->bi_opf = bio_src->bi_opf;
	bio->bi_ioprio = bio_src->bi_ioprio;
	bio->bi_write_hint = bio_src->bi_write_hint;
	bio->bi_iter = bio_src->bi_iter;
	bio->bi_io_vec = bio_src->bi_io_vec;

	bio_clone_blkg_association(bio, bio_src);
	blkcg_bio_issue_init(bio);
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

const char *bio_devname(struct bio *bio, char *buf)
{
	return disk_name(bio->bi_disk, bio->bi_partno, buf);
}
EXPORT_SYMBOL(bio_devname);

static inline bool page_is_mergeable(const struct bio_vec *bv,
		struct page *page, unsigned int len, unsigned int off,
		bool *same_page)
{
	phys_addr_t vec_end_addr = page_to_phys(bv->bv_page) +
		bv->bv_offset + bv->bv_len - 1;
	phys_addr_t page_addr = page_to_phys(page);

	if (vec_end_addr + 1 != page_addr + off)
		return false;
	if (xen_domain() && !xen_biovec_phys_mergeable(bv, page))
		return false;

	*same_page = ((vec_end_addr & PAGE_MASK) == page_addr);
	if (!*same_page && pfn_to_page(PFN_DOWN(vec_end_addr)) + 1 != page)
		return false;
	return true;
}

static bool bio_try_merge_pc_page(struct request_queue *q, struct bio *bio,
		struct page *page, unsigned len, unsigned offset,
		bool *same_page)
{
	struct bio_vec *bv = &bio->bi_io_vec[bio->bi_vcnt - 1];
	unsigned long mask = queue_segment_boundary(q);
	phys_addr_t addr1 = page_to_phys(bv->bv_page) + bv->bv_offset;
	phys_addr_t addr2 = page_to_phys(page) + offset + len - 1;

	if ((addr1 | mask) != (addr2 | mask))
		return false;
	if (bv->bv_len + len > queue_max_segment_size(q))
		return false;
	return __bio_try_merge_page(bio, page, len, offset, same_page);
}

/**
 *	__bio_add_pc_page	- attempt to add page to passthrough bio
 *	@q: the target queue
 *	@bio: destination bio
 *	@page: page to add
 *	@len: vec entry length
 *	@offset: vec entry offset
 *	@same_page: return if the merge happen inside the same page
 *
 *	Attempt to add a page to the bio_vec maplist. This can fail for a
 *	number of reasons, such as the bio being full or target block device
 *	limitations. The target block device must allow bio's up to PAGE_SIZE,
 *	so it is always possible to add a single page to an empty bio.
 *
 *	This should only be used by passthrough bios.
 */
int __bio_add_pc_page(struct request_queue *q, struct bio *bio,
		struct page *page, unsigned int len, unsigned int offset,
		bool *same_page)
{
	struct bio_vec *bvec;

	/*
	 * cloned bio must not modify vec list
	 */
	if (unlikely(bio_flagged(bio, BIO_CLONED)))
		return 0;

	if (((bio->bi_iter.bi_size + len) >> 9) > queue_max_hw_sectors(q))
		return 0;

	if (bio->bi_vcnt > 0) {
		if (bio_try_merge_pc_page(q, bio, page, len, offset, same_page))
			return len;

		/*
		 * If the queue doesn't support SG gaps and adding this segment
		 * would create a gap, disallow it.
		 */
		bvec = &bio->bi_io_vec[bio->bi_vcnt - 1];
		if (bvec_gap_to_prev(q, bvec, offset))
			return 0;
	}

	if (bio_full(bio, len))
		return 0;

	if (bio->bi_vcnt >= queue_max_segments(q))
		return 0;

	bvec = &bio->bi_io_vec[bio->bi_vcnt];
	bvec->bv_page = page;
	bvec->bv_len = len;
	bvec->bv_offset = offset;
	bio->bi_vcnt++;
	bio->bi_iter.bi_size += len;
	return len;
}

int bio_add_pc_page(struct request_queue *q, struct bio *bio,
		struct page *page, unsigned int len, unsigned int offset)
{
	bool same_page = false;
	return __bio_add_pc_page(q, bio, page, len, offset, &same_page);
}
EXPORT_SYMBOL(bio_add_pc_page);

/**
 * __bio_try_merge_page - try appending data to an existing bvec.
 * @bio: destination bio
 * @page: start page to add
 * @len: length of the data to add
 * @off: offset of the data relative to @page
 * @same_page: return if the segment has been merged inside the same page
 *
 * Try to add the data at @page + @off to the last bvec of @bio.  This is a
 * a useful optimisation for file systems with a block size smaller than the
 * page size.
 *
 * Warn if (@len, @off) crosses pages in case that @same_page is true.
 *
 * Return %true on success or %false on failure.
 */
bool __bio_try_merge_page(struct bio *bio, struct page *page,
		unsigned int len, unsigned int off, bool *same_page)
{
	if (WARN_ON_ONCE(bio_flagged(bio, BIO_CLONED)))
		return false;

	if (bio->bi_vcnt > 0) {
		struct bio_vec *bv = &bio->bi_io_vec[bio->bi_vcnt - 1];

		if (page_is_mergeable(bv, page, len, off, same_page)) {
			if (bio->bi_iter.bi_size > UINT_MAX - len)
				return false;
			bv->bv_len += len;
			bio->bi_iter.bi_size += len;
			return true;
		}
	}
	return false;
}
EXPORT_SYMBOL_GPL(__bio_try_merge_page);

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
	struct bio_vec *bv = &bio->bi_io_vec[bio->bi_vcnt];

	WARN_ON_ONCE(bio_flagged(bio, BIO_CLONED));
	WARN_ON_ONCE(bio_full(bio, len));

	bv->bv_page = page;
	bv->bv_offset = off;
	bv->bv_len = len;

	bio->bi_iter.bi_size += len;
	bio->bi_vcnt++;

	if (!bio_flagged(bio, BIO_WORKINGSET) && unlikely(PageWorkingset(page)))
		bio_set_flag(bio, BIO_WORKINGSET);
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

	if (!__bio_try_merge_page(bio, page, len, offset, &same_page)) {
		if (bio_full(bio, len))
			return 0;
		__bio_add_page(bio, page, len, offset);
	}
	return len;
}
EXPORT_SYMBOL(bio_add_page);

void bio_release_pages(struct bio *bio, bool mark_dirty)
{
	struct bvec_iter_all iter_all;
	struct bio_vec *bvec;

	if (bio_flagged(bio, BIO_NO_PAGE_REF))
		return;

	bio_for_each_segment_all(bvec, bio, iter_all) {
		if (mark_dirty && !PageCompound(bvec->bv_page))
			set_page_dirty_lock(bvec->bv_page);
		put_page(bvec->bv_page);
	}
}

static int __bio_iov_bvec_add_pages(struct bio *bio, struct iov_iter *iter)
{
	const struct bio_vec *bv = iter->bvec;
	unsigned int len;
	size_t size;

	if (WARN_ON_ONCE(iter->iov_offset > bv->bv_len))
		return -EINVAL;

	len = min_t(size_t, bv->bv_len - iter->iov_offset, iter->count);
	size = bio_add_page(bio, bv->bv_page, len,
				bv->bv_offset + iter->iov_offset);
	if (unlikely(size != len))
		return -EINVAL;
	iov_iter_advance(iter, size);
	return 0;
}

#define PAGE_PTRS_PER_BVEC     (sizeof(struct bio_vec) / sizeof(struct page *))

/**
 * __bio_iov_iter_get_pages - pin user or kernel pages and add them to a bio
 * @bio: bio to add pages to
 * @iter: iov iterator describing the region to be mapped
 *
 * Pins pages from *iter and appends them to @bio's bvec array. The
 * pages will have to be released using put_page() when done.
 * For multi-segment *iter, this function only adds pages from the
 * the next non-empty segment of the iov iterator.
 */
static int __bio_iov_iter_get_pages(struct bio *bio, struct iov_iter *iter)
{
	unsigned short nr_pages = bio->bi_max_vecs - bio->bi_vcnt;
	unsigned short entries_left = bio->bi_max_vecs - bio->bi_vcnt;
	struct bio_vec *bv = bio->bi_io_vec + bio->bi_vcnt;
	struct page **pages = (struct page **)bv;
	bool same_page = false;
	ssize_t size, left;
	unsigned len, i;
	size_t offset;

	/*
	 * Move page array up in the allocated memory for the bio vecs as far as
	 * possible so that we can start filling biovecs from the beginning
	 * without overwriting the temporary page array.
	*/
	BUILD_BUG_ON(PAGE_PTRS_PER_BVEC < 2);
	pages += entries_left * (PAGE_PTRS_PER_BVEC - 1);

	size = iov_iter_get_pages(iter, pages, LONG_MAX, nr_pages, &offset);
	if (unlikely(size <= 0))
		return size ? size : -EFAULT;

	for (left = size, i = 0; left > 0; left -= len, i++) {
		struct page *page = pages[i];

		len = min_t(size_t, PAGE_SIZE - offset, left);

		if (__bio_try_merge_page(bio, page, len, offset, &same_page)) {
			if (same_page)
				put_page(page);
		} else {
			if (WARN_ON_ONCE(bio_full(bio, len)))
                                return -EINVAL;
			__bio_add_page(bio, page, len, offset);
		}
		offset = 0;
	}

	iov_iter_advance(iter, size);
	return 0;
}

/**
 * bio_iov_iter_get_pages - add user or kernel pages to a bio
 * @bio: bio to add pages to
 * @iter: iov iterator describing the region to be added
 *
 * This takes either an iterator pointing to user memory, or one pointing to
 * kernel pages (BVEC iterator). If we're adding user pages, we pin them and
 * map them into the kernel. On IO completion, the caller should put those
 * pages. If we're adding kernel pages, and the caller told us it's safe to
 * do so, we just have to add the pages to the bio directly. We don't grab an
 * extra reference to those pages (the user should already have that), and we
 * don't put the page on IO completion. The caller needs to check if the bio is
 * flagged BIO_NO_PAGE_REF on IO completion. If it isn't, then pages should be
 * released.
 *
 * The function tries, but does not guarantee, to pin as many pages as
 * fit into the bio, or are requested in *iter, whatever is smaller. If
 * MM encounters an error pinning the requested pages, it stops. Error
 * is returned only if 0 pages could be pinned.
 */
int bio_iov_iter_get_pages(struct bio *bio, struct iov_iter *iter)
{
	const bool is_bvec = iov_iter_is_bvec(iter);
	int ret;

	if (WARN_ON_ONCE(bio->bi_vcnt))
		return -EINVAL;

	do {
		if (is_bvec)
			ret = __bio_iov_bvec_add_pages(bio, iter);
		else
			ret = __bio_iov_iter_get_pages(bio, iter);
	} while (!ret && iov_iter_count(iter) && !bio_full(bio, 0));

	if (is_bvec)
		bio_set_flag(bio, BIO_NO_PAGE_REF);
	return bio->bi_vcnt ? 0 : ret;
}

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
	DECLARE_COMPLETION_ONSTACK_MAP(done, bio->bi_disk->lockdep_map);
	unsigned long hang_check;

	bio->bi_private = &done;
	bio->bi_end_io = submit_bio_wait_endio;
	bio->bi_opf |= REQ_SYNC;
	submit_bio(bio);

	/* Prevent hang_check timer from firing at us during very long I/O */
	hang_check = sysctl_hung_task_timeout_secs;
	if (hang_check)
		while (!wait_for_completion_io_timeout(&done,
					hang_check * (HZ/2)))
			;
	else
		wait_for_completion_io(&done);

	return blk_status_to_errno(bio->bi_status);
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

void bio_copy_data_iter(struct bio *dst, struct bvec_iter *dst_iter,
			struct bio *src, struct bvec_iter *src_iter)
{
	struct bio_vec src_bv, dst_bv;
	void *src_p, *dst_p;
	unsigned bytes;

	while (src_iter->bi_size && dst_iter->bi_size) {
		src_bv = bio_iter_iovec(src, *src_iter);
		dst_bv = bio_iter_iovec(dst, *dst_iter);

		bytes = min(src_bv.bv_len, dst_bv.bv_len);

		src_p = kmap_atomic(src_bv.bv_page);
		dst_p = kmap_atomic(dst_bv.bv_page);

		memcpy(dst_p + dst_bv.bv_offset,
		       src_p + src_bv.bv_offset,
		       bytes);

		kunmap_atomic(dst_p);
		kunmap_atomic(src_p);

		flush_dcache_page(dst_bv.bv_page);

		bio_advance_iter(src, src_iter, bytes);
		bio_advance_iter(dst, dst_iter, bytes);
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

/**
 * bio_list_copy_data - copy contents of data buffers from one chain of bios to
 * another
 * @src: source bio list
 * @dst: destination bio list
 *
 * Stops when it reaches the end of either the @src list or @dst list - that is,
 * copies min(src->bi_size, dst->bi_size) bytes (or the equivalent for lists of
 * bios).
 */
void bio_list_copy_data(struct bio *dst, struct bio *src)
{
	struct bvec_iter src_iter = src->bi_iter;
	struct bvec_iter dst_iter = dst->bi_iter;

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

		bio_copy_data_iter(dst, &dst_iter, src, &src_iter);
	}
}
EXPORT_SYMBOL(bio_list_copy_data);

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
	struct bvec_iter_all iter_all;

	bio_for_each_segment_all(bvec, bio, iter_all) {
		if (!PageCompound(bvec->bv_page))
			set_page_dirty_lock(bvec->bv_page);
	}
}

/*
 * bio_check_pages_dirty() will check that all the BIO's pages are still dirty.
 * If they are, then fine.  If, however, some pages are clean then they must
 * have been written out during the direct-IO read.  So we take another ref on
 * the BIO and re-dirty the pages in process context.
 *
 * It is expected that bio_check_pages_dirty() will wholly own the BIO from
 * here on.  It will run one put_page() against each page and will run one
 * bio_put() against the BIO.
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
	struct bio_vec *bvec;
	unsigned long flags;
	struct bvec_iter_all iter_all;

	bio_for_each_segment_all(bvec, bio, iter_all) {
		if (!PageDirty(bvec->bv_page) && !PageCompound(bvec->bv_page))
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

void update_io_ticks(struct hd_struct *part, unsigned long now, bool end)
{
	unsigned long stamp;
again:
	stamp = READ_ONCE(part->stamp);
	if (unlikely(stamp != now)) {
		if (likely(cmpxchg(&part->stamp, stamp, now) == stamp)) {
			__part_stat_add(part, io_ticks, end ? now - stamp : 1);
		}
	}
	if (part->partno) {
		part = &part_to_disk(part)->part0;
		goto again;
	}
}

void generic_start_io_acct(struct request_queue *q, int op,
			   unsigned long sectors, struct hd_struct *part)
{
	const int sgrp = op_stat_group(op);

	part_stat_lock();

	update_io_ticks(part, jiffies, false);
	part_stat_inc(part, ios[sgrp]);
	part_stat_add(part, sectors[sgrp], sectors);
	part_inc_in_flight(q, part, op_is_write(op));

	part_stat_unlock();
}
EXPORT_SYMBOL(generic_start_io_acct);

void generic_end_io_acct(struct request_queue *q, int req_op,
			 struct hd_struct *part, unsigned long start_time)
{
	unsigned long now = jiffies;
	unsigned long duration = now - start_time;
	const int sgrp = op_stat_group(req_op);

	part_stat_lock();

	update_io_ticks(part, now, true);
	part_stat_add(part, nsecs[sgrp], jiffies_to_nsecs(duration));
	part_dec_in_flight(q, part, op_is_write(req_op));

	part_stat_unlock();
}
EXPORT_SYMBOL(generic_end_io_acct);

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
 *   last time.  At this point the BLK_TA_COMPLETE tracing event will be
 *   generated if BIO_TRACE_COMPLETION is set.
 **/
void bio_endio(struct bio *bio)
{
again:
	if (!bio_remaining_done(bio))
		return;
	if (!bio_integrity_endio(bio))
		return;

	if (bio->bi_disk)
		rq_qos_done_bio(bio->bi_disk->queue, bio);

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

	if (bio->bi_disk && bio_flagged(bio, BIO_TRACE_COMPLETION)) {
		trace_block_bio_complete(bio->bi_disk->queue, bio,
					 blk_status_to_errno(bio->bi_status));
		bio_clear_flag(bio, BIO_TRACE_COMPLETION);
	}

	blk_throtl_bio_endio(bio);
	/* release cgroup info */
	bio_uninit(bio);
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

	BUG_ON(sectors <= 0);
	BUG_ON(sectors >= bio_sectors(bio));

	split = bio_clone_fast(bio, gfp, bs);
	if (!split)
		return NULL;

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
 */
void bio_trim(struct bio *bio, int offset, int size)
{
	/* 'bio' is a cloned bio which we need to trim to match
	 * the given offset and size.
	 */

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
	struct biovec_slab *bp = bvec_slabs + BVEC_POOL_MAX;

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
 *    for allocating iovecs.  This pool is not needed e.g. for bio_clone_fast().
 *    If %BIOSET_NEED_RESCUER is set, a workqueue is created which can be used to
 *    dispatch queued requests when the mempool runs out of space.
 *
 */
int bioset_init(struct bio_set *bs,
		unsigned int pool_size,
		unsigned int front_pad,
		int flags)
{
	unsigned int back_pad = BIO_INLINE_VECS * sizeof(struct bio_vec);

	bs->front_pad = front_pad;

	spin_lock_init(&bs->rescue_lock);
	bio_list_init(&bs->rescue_list);
	INIT_WORK(&bs->rescue_work, bio_alloc_rescue);

	bs->bio_slab = bio_find_or_create_slab(front_pad + back_pad);
	if (!bs->bio_slab)
		return -ENOMEM;

	if (mempool_init_slab_pool(&bs->bio_pool, pool_size, bs->bio_slab))
		goto bad;

	if ((flags & BIOSET_NEED_BVECS) &&
	    biovec_init_pool(&bs->bvec_pool, pool_size))
		goto bad;

	if (!(flags & BIOSET_NEED_RESCUER))
		return 0;

	bs->rescue_workqueue = alloc_workqueue("bioset", WQ_MEM_RECLAIM, 0);
	if (!bs->rescue_workqueue)
		goto bad;

	return 0;
bad:
	bioset_exit(bs);
	return -ENOMEM;
}
EXPORT_SYMBOL(bioset_init);

/*
 * Initialize and setup a new bio_set, based on the settings from
 * another bio_set.
 */
int bioset_init_from_src(struct bio_set *bs, struct bio_set *src)
{
	int flags;

	flags = 0;
	if (src->bvec_pool.min_nr)
		flags |= BIOSET_NEED_BVECS;
	if (src->rescue_workqueue)
		flags |= BIOSET_NEED_RESCUER;

	return bioset_init(bs, src->bio_pool.min_nr, src->front_pad, flags);
}
EXPORT_SYMBOL(bioset_init_from_src);

#ifdef CONFIG_BLK_CGROUP

/**
 * bio_disassociate_blkg - puts back the blkg reference if associated
 * @bio: target bio
 *
 * Helper to disassociate the blkg from @bio if a blkg is associated.
 */
void bio_disassociate_blkg(struct bio *bio)
{
	if (bio->bi_blkg) {
		blkg_put(bio->bi_blkg);
		bio->bi_blkg = NULL;
	}
}
EXPORT_SYMBOL_GPL(bio_disassociate_blkg);

/**
 * __bio_associate_blkg - associate a bio with the a blkg
 * @bio: target bio
 * @blkg: the blkg to associate
 *
 * This tries to associate @bio with the specified @blkg.  Association failure
 * is handled by walking up the blkg tree.  Therefore, the blkg associated can
 * be anything between @blkg and the root_blkg.  This situation only happens
 * when a cgroup is dying and then the remaining bios will spill to the closest
 * alive blkg.
 *
 * A reference will be taken on the @blkg and will be released when @bio is
 * freed.
 */
static void __bio_associate_blkg(struct bio *bio, struct blkcg_gq *blkg)
{
	bio_disassociate_blkg(bio);

	bio->bi_blkg = blkg_tryget_closest(blkg);
}

/**
 * bio_associate_blkg_from_css - associate a bio with a specified css
 * @bio: target bio
 * @css: target css
 *
 * Associate @bio with the blkg found by combining the css's blkg and the
 * request_queue of the @bio.  This falls back to the queue's root_blkg if
 * the association fails with the css.
 */
void bio_associate_blkg_from_css(struct bio *bio,
				 struct cgroup_subsys_state *css)
{
	struct request_queue *q = bio->bi_disk->queue;
	struct blkcg_gq *blkg;

	rcu_read_lock();

	if (!css || !css->parent)
		blkg = q->root_blkg;
	else
		blkg = blkg_lookup_create(css_to_blkcg(css), q);

	__bio_associate_blkg(bio, blkg);

	rcu_read_unlock();
}
EXPORT_SYMBOL_GPL(bio_associate_blkg_from_css);

#ifdef CONFIG_MEMCG
/**
 * bio_associate_blkg_from_page - associate a bio with the page's blkg
 * @bio: target bio
 * @page: the page to lookup the blkcg from
 *
 * Associate @bio with the blkg from @page's owning memcg and the respective
 * request_queue.  If cgroup_e_css returns %NULL, fall back to the queue's
 * root_blkg.
 */
void bio_associate_blkg_from_page(struct bio *bio, struct page *page)
{
	struct cgroup_subsys_state *css;

	if (!page->mem_cgroup)
		return;

	rcu_read_lock();

	css = cgroup_e_css(page->mem_cgroup->css.cgroup, &io_cgrp_subsys);
	bio_associate_blkg_from_css(bio, css);

	rcu_read_unlock();
}
#endif /* CONFIG_MEMCG */

/**
 * bio_associate_blkg - associate a bio with a blkg
 * @bio: target bio
 *
 * Associate @bio with the blkg found from the bio's css and request_queue.
 * If one is not found, bio_lookup_blkg() creates the blkg.  If a blkg is
 * already associated, the css is reused and association redone as the
 * request_queue may have changed.
 */
void bio_associate_blkg(struct bio *bio)
{
	struct cgroup_subsys_state *css;

	rcu_read_lock();

	if (bio->bi_blkg)
		css = &bio_blkcg(bio)->css;
	else
		css = blkcg_css();

	bio_associate_blkg_from_css(bio, css);

	rcu_read_unlock();
}
EXPORT_SYMBOL_GPL(bio_associate_blkg);

/**
 * bio_clone_blkg_association - clone blkg association from src to dst bio
 * @dst: destination bio
 * @src: source bio
 */
void bio_clone_blkg_association(struct bio *dst, struct bio *src)
{
	rcu_read_lock();

	if (src->bi_blkg)
		__bio_associate_blkg(dst, src->bi_blkg);

	rcu_read_unlock();
}
EXPORT_SYMBOL_GPL(bio_clone_blkg_association);
#endif /* CONFIG_BLK_CGROUP */

static void __init biovec_init_slabs(void)
{
	int i;

	for (i = 0; i < BVEC_POOL_NR; i++) {
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
	bio_slabs = kcalloc(bio_slab_max, sizeof(struct bio_slab),
			    GFP_KERNEL);

	BUILD_BUG_ON(BIO_FLAG_LAST > BVEC_POOL_OFFSET);

	if (!bio_slabs)
		panic("bio: can't allocate bios\n");

	bio_integrity_init();
	biovec_init_slabs();

	if (bioset_init(&fs_bio_set, BIO_POOL_SIZE, 0, BIOSET_NEED_BVECS))
		panic("bio: can't allocate bios\n");

	if (bioset_integrity_create(&fs_bio_set, BIO_POOL_SIZE))
		panic("bio: can't create integrity pool\n");

	return 0;
}
subsys_initcall(init_bio);
