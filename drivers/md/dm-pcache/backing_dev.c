// SPDX-License-Identifier: GPL-2.0-or-later
#include <linux/blkdev.h>

#include "../dm-core.h"
#include "pcache_internal.h"
#include "cache_dev.h"
#include "backing_dev.h"
#include "cache.h"
#include "dm_pcache.h"

static struct kmem_cache *backing_req_cache;
static struct kmem_cache *backing_bvec_cache;

static void backing_dev_exit(struct pcache_backing_dev *backing_dev)
{
	mempool_exit(&backing_dev->req_pool);
	mempool_exit(&backing_dev->bvec_pool);
}

static void req_submit_fn(struct work_struct *work);
static void req_complete_fn(struct work_struct *work);
static int backing_dev_init(struct dm_pcache *pcache)
{
	struct pcache_backing_dev *backing_dev = &pcache->backing_dev;
	int ret;

	ret = mempool_init_slab_pool(&backing_dev->req_pool, 128, backing_req_cache);
	if (ret)
		goto err;

	ret = mempool_init_slab_pool(&backing_dev->bvec_pool, 128, backing_bvec_cache);
	if (ret)
		goto req_pool_exit;

	INIT_LIST_HEAD(&backing_dev->submit_list);
	INIT_LIST_HEAD(&backing_dev->complete_list);
	spin_lock_init(&backing_dev->submit_lock);
	spin_lock_init(&backing_dev->complete_lock);
	INIT_WORK(&backing_dev->req_submit_work, req_submit_fn);
	INIT_WORK(&backing_dev->req_complete_work, req_complete_fn);
	atomic_set(&backing_dev->inflight_reqs, 0);
	init_waitqueue_head(&backing_dev->inflight_wq);

	return 0;

req_pool_exit:
	mempool_exit(&backing_dev->req_pool);
err:
	return ret;
}

int backing_dev_start(struct dm_pcache *pcache)
{
	struct pcache_backing_dev *backing_dev = &pcache->backing_dev;
	int ret;

	ret = backing_dev_init(pcache);
	if (ret)
		return ret;

	backing_dev->dev_size = bdev_nr_sectors(backing_dev->dm_dev->bdev);

	return 0;
}

void backing_dev_stop(struct dm_pcache *pcache)
{
	struct pcache_backing_dev *backing_dev = &pcache->backing_dev;

	/*
	 * There should not be any new request comming, just wait
	 * inflight requests done.
	 */
	wait_event(backing_dev->inflight_wq,
			atomic_read(&backing_dev->inflight_reqs) == 0);

	flush_work(&backing_dev->req_submit_work);
	flush_work(&backing_dev->req_complete_work);

	backing_dev_exit(backing_dev);
}

/* pcache_backing_dev_req functions */
void backing_dev_req_end(struct pcache_backing_dev_req *backing_req)
{
	struct pcache_backing_dev *backing_dev = backing_req->backing_dev;

	if (backing_req->end_req)
		backing_req->end_req(backing_req, backing_req->ret);

	switch (backing_req->type) {
	case BACKING_DEV_REQ_TYPE_REQ:
		if (backing_req->req.upper_req)
			pcache_req_put(backing_req->req.upper_req, backing_req->ret);
		break;
	case BACKING_DEV_REQ_TYPE_KMEM:
		if (backing_req->kmem.bvecs != backing_req->kmem.inline_bvecs)
			mempool_free(backing_req->kmem.bvecs, &backing_dev->bvec_pool);
		break;
	default:
		BUG();
	}

	mempool_free(backing_req, &backing_dev->req_pool);

	if (atomic_dec_and_test(&backing_dev->inflight_reqs))
		wake_up(&backing_dev->inflight_wq);
}

static void req_complete_fn(struct work_struct *work)
{
	struct pcache_backing_dev *backing_dev = container_of(work, struct pcache_backing_dev, req_complete_work);
	struct pcache_backing_dev_req *backing_req;
	LIST_HEAD(tmp_list);

	spin_lock_irq(&backing_dev->complete_lock);
	list_splice_init(&backing_dev->complete_list, &tmp_list);
	spin_unlock_irq(&backing_dev->complete_lock);

	while (!list_empty(&tmp_list)) {
		backing_req = list_first_entry(&tmp_list,
					    struct pcache_backing_dev_req, node);
		list_del_init(&backing_req->node);
		backing_dev_req_end(backing_req);
	}
}

static void backing_dev_bio_end(struct bio *bio)
{
	struct pcache_backing_dev_req *backing_req = bio->bi_private;
	struct pcache_backing_dev *backing_dev = backing_req->backing_dev;
	unsigned long flags;

	backing_req->ret = blk_status_to_errno(bio->bi_status);

	spin_lock_irqsave(&backing_dev->complete_lock, flags);
	list_move_tail(&backing_req->node, &backing_dev->complete_list);
	queue_work(BACKING_DEV_TO_PCACHE(backing_dev)->task_wq, &backing_dev->req_complete_work);
	spin_unlock_irqrestore(&backing_dev->complete_lock, flags);
}

static void req_submit_fn(struct work_struct *work)
{
	struct pcache_backing_dev *backing_dev = container_of(work, struct pcache_backing_dev, req_submit_work);
	struct pcache_backing_dev_req *backing_req;
	LIST_HEAD(tmp_list);

	spin_lock(&backing_dev->submit_lock);
	list_splice_init(&backing_dev->submit_list, &tmp_list);
	spin_unlock(&backing_dev->submit_lock);

	while (!list_empty(&tmp_list)) {
		backing_req = list_first_entry(&tmp_list,
					    struct pcache_backing_dev_req, node);
		list_del_init(&backing_req->node);
		submit_bio_noacct(&backing_req->bio);
	}
}

void backing_dev_req_submit(struct pcache_backing_dev_req *backing_req, bool direct)
{
	struct pcache_backing_dev *backing_dev = backing_req->backing_dev;

	if (direct) {
		submit_bio_noacct(&backing_req->bio);
		return;
	}

	spin_lock(&backing_dev->submit_lock);
	list_add_tail(&backing_req->node, &backing_dev->submit_list);
	queue_work(BACKING_DEV_TO_PCACHE(backing_dev)->task_wq, &backing_dev->req_submit_work);
	spin_unlock(&backing_dev->submit_lock);
}

static void bio_map(struct bio *bio, void *base, size_t size)
{
	struct page *page;
	unsigned int offset;
	unsigned int len;

	if (!is_vmalloc_addr(base)) {
		page = virt_to_page(base);
		offset = offset_in_page(base);

		BUG_ON(!bio_add_page(bio, page, size, offset));
		return;
	}

	flush_kernel_vmap_range(base, size);
	while (size) {
		page = vmalloc_to_page(base);
		offset = offset_in_page(base);
		len = min_t(size_t, PAGE_SIZE - offset, size);

		BUG_ON(!bio_add_page(bio, page, len, offset));
		size -= len;
		base += len;
	}
}

static struct pcache_backing_dev_req *req_type_req_alloc(struct pcache_backing_dev *backing_dev,
							struct pcache_backing_dev_req_opts *opts)
{
	struct pcache_request *pcache_req = opts->req.upper_req;
	struct pcache_backing_dev_req *backing_req;
	struct bio *orig = pcache_req->bio;

	backing_req = mempool_alloc(&backing_dev->req_pool, opts->gfp_mask);
	if (!backing_req)
		return NULL;

	memset(backing_req, 0, sizeof(struct pcache_backing_dev_req));

	bio_init_clone(backing_dev->dm_dev->bdev, &backing_req->bio, orig, opts->gfp_mask);

	backing_req->type = BACKING_DEV_REQ_TYPE_REQ;
	backing_req->backing_dev = backing_dev;
	atomic_inc(&backing_dev->inflight_reqs);

	return backing_req;
}

static struct pcache_backing_dev_req *kmem_type_req_alloc(struct pcache_backing_dev *backing_dev,
						struct pcache_backing_dev_req_opts *opts)
{
	struct pcache_backing_dev_req *backing_req;
	u32 n_vecs = bio_add_max_vecs(opts->kmem.data, opts->kmem.len);

	backing_req = mempool_alloc(&backing_dev->req_pool, opts->gfp_mask);
	if (!backing_req)
		return NULL;

	memset(backing_req, 0, sizeof(struct pcache_backing_dev_req));

	if (n_vecs > BACKING_DEV_REQ_INLINE_BVECS) {
		backing_req->kmem.bvecs = mempool_alloc(&backing_dev->bvec_pool, opts->gfp_mask);
		if (!backing_req->kmem.bvecs)
			goto free_backing_req;
	} else {
		backing_req->kmem.bvecs = backing_req->kmem.inline_bvecs;
	}

	backing_req->kmem.n_vecs = n_vecs;
	backing_req->type = BACKING_DEV_REQ_TYPE_KMEM;
	backing_req->backing_dev = backing_dev;
	atomic_inc(&backing_dev->inflight_reqs);

	return backing_req;

free_backing_req:
	mempool_free(backing_req, &backing_dev->req_pool);
	return NULL;
}

struct pcache_backing_dev_req *backing_dev_req_alloc(struct pcache_backing_dev *backing_dev,
						struct pcache_backing_dev_req_opts *opts)
{
	if (opts->type == BACKING_DEV_REQ_TYPE_REQ)
		return req_type_req_alloc(backing_dev, opts);

	if (opts->type == BACKING_DEV_REQ_TYPE_KMEM)
		return kmem_type_req_alloc(backing_dev, opts);

	BUG();
}

static void req_type_req_init(struct pcache_backing_dev_req *backing_req,
			struct pcache_backing_dev_req_opts *opts)
{
	struct pcache_request *pcache_req = opts->req.upper_req;
	struct bio *clone;
	u32 off = opts->req.req_off;
	u32 len = opts->req.len;

	clone = &backing_req->bio;
	BUG_ON(off & SECTOR_MASK);
	BUG_ON(len & SECTOR_MASK);
	bio_trim(clone, off >> SECTOR_SHIFT, len >> SECTOR_SHIFT);

	clone->bi_iter.bi_sector = (pcache_req->off + off) >> SECTOR_SHIFT;
	clone->bi_private = backing_req;
	clone->bi_end_io = backing_dev_bio_end;

	INIT_LIST_HEAD(&backing_req->node);
	backing_req->end_req     = opts->end_fn;

	pcache_req_get(pcache_req);
	backing_req->req.upper_req	= pcache_req;
	backing_req->req.bio_off	= off;
}

static void kmem_type_req_init(struct pcache_backing_dev_req *backing_req,
			struct pcache_backing_dev_req_opts *opts)
{
	struct pcache_backing_dev *backing_dev = backing_req->backing_dev;
	struct bio *backing_bio;

	bio_init(&backing_req->bio, backing_dev->dm_dev->bdev, backing_req->kmem.bvecs,
			backing_req->kmem.n_vecs, opts->kmem.opf);

	backing_bio = &backing_req->bio;
	bio_map(backing_bio, opts->kmem.data, opts->kmem.len);

	backing_bio->bi_iter.bi_sector = (opts->kmem.backing_off) >> SECTOR_SHIFT;
	backing_bio->bi_private = backing_req;
	backing_bio->bi_end_io = backing_dev_bio_end;

	INIT_LIST_HEAD(&backing_req->node);
	backing_req->end_req	= opts->end_fn;
	backing_req->priv_data	= opts->priv_data;
}

void backing_dev_req_init(struct pcache_backing_dev_req *backing_req,
			struct pcache_backing_dev_req_opts *opts)
{
	if (opts->type == BACKING_DEV_REQ_TYPE_REQ)
		return req_type_req_init(backing_req, opts);

	if (opts->type == BACKING_DEV_REQ_TYPE_KMEM)
		return kmem_type_req_init(backing_req, opts);

	BUG();
}

struct pcache_backing_dev_req *backing_dev_req_create(struct pcache_backing_dev *backing_dev,
						struct pcache_backing_dev_req_opts *opts)
{
	struct pcache_backing_dev_req *backing_req;

	backing_req = backing_dev_req_alloc(backing_dev, opts);
	if (!backing_req)
		return NULL;

	backing_dev_req_init(backing_req, opts);

	return backing_req;
}

void backing_dev_flush(struct pcache_backing_dev *backing_dev)
{
	blkdev_issue_flush(backing_dev->dm_dev->bdev);
}

int pcache_backing_init(void)
{
	u32 max_bvecs = (PCACHE_CACHE_SUBTREE_SIZE >> PAGE_SHIFT) + 1;
	int ret;

	backing_req_cache = KMEM_CACHE(pcache_backing_dev_req, 0);
	if (!backing_req_cache) {
		ret = -ENOMEM;
		goto err;
	}

	backing_bvec_cache = kmem_cache_create("pcache-bvec-slab",
					max_bvecs * sizeof(struct bio_vec),
					0, 0, NULL);
	if (!backing_bvec_cache) {
		ret = -ENOMEM;
		goto destroy_req_cache;
	}

	return 0;
destroy_req_cache:
	kmem_cache_destroy(backing_req_cache);
err:
	return ret;
}

void pcache_backing_exit(void)
{
	kmem_cache_destroy(backing_bvec_cache);
	kmem_cache_destroy(backing_req_cache);
}
