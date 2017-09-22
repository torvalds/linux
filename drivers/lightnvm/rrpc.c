/*
 * Copyright (C) 2015 IT University of Copenhagen
 * Initial release: Matias Bjorling <m@bjorling.me>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * Implementation of a Round-robin page-based Hybrid FTL for Open-channel SSDs.
 */

#include "rrpc.h"

static struct kmem_cache *rrpc_gcb_cache, *rrpc_rq_cache;
static DECLARE_RWSEM(rrpc_lock);

static int rrpc_submit_io(struct rrpc *rrpc, struct bio *bio,
				struct nvm_rq *rqd, unsigned long flags);

#define rrpc_for_each_lun(rrpc, rlun, i) \
		for ((i) = 0, rlun = &(rrpc)->luns[0]; \
			(i) < (rrpc)->nr_luns; (i)++, rlun = &(rrpc)->luns[(i)])

static void rrpc_page_invalidate(struct rrpc *rrpc, struct rrpc_addr *a)
{
	struct nvm_tgt_dev *dev = rrpc->dev;
	struct rrpc_block *rblk = a->rblk;
	unsigned int pg_offset;

	lockdep_assert_held(&rrpc->rev_lock);

	if (a->addr == ADDR_EMPTY || !rblk)
		return;

	spin_lock(&rblk->lock);

	div_u64_rem(a->addr, dev->geo.sec_per_blk, &pg_offset);
	WARN_ON(test_and_set_bit(pg_offset, rblk->invalid_pages));
	rblk->nr_invalid_pages++;

	spin_unlock(&rblk->lock);

	rrpc->rev_trans_map[a->addr].addr = ADDR_EMPTY;
}

static void rrpc_invalidate_range(struct rrpc *rrpc, sector_t slba,
							unsigned int len)
{
	sector_t i;

	spin_lock(&rrpc->rev_lock);
	for (i = slba; i < slba + len; i++) {
		struct rrpc_addr *gp = &rrpc->trans_map[i];

		rrpc_page_invalidate(rrpc, gp);
		gp->rblk = NULL;
	}
	spin_unlock(&rrpc->rev_lock);
}

static struct nvm_rq *rrpc_inflight_laddr_acquire(struct rrpc *rrpc,
					sector_t laddr, unsigned int pages)
{
	struct nvm_rq *rqd;
	struct rrpc_inflight_rq *inf;

	rqd = mempool_alloc(rrpc->rq_pool, GFP_ATOMIC);
	if (!rqd)
		return ERR_PTR(-ENOMEM);

	inf = rrpc_get_inflight_rq(rqd);
	if (rrpc_lock_laddr(rrpc, laddr, pages, inf)) {
		mempool_free(rqd, rrpc->rq_pool);
		return NULL;
	}

	return rqd;
}

static void rrpc_inflight_laddr_release(struct rrpc *rrpc, struct nvm_rq *rqd)
{
	struct rrpc_inflight_rq *inf = rrpc_get_inflight_rq(rqd);

	rrpc_unlock_laddr(rrpc, inf);

	mempool_free(rqd, rrpc->rq_pool);
}

static void rrpc_discard(struct rrpc *rrpc, struct bio *bio)
{
	sector_t slba = bio->bi_iter.bi_sector / NR_PHY_IN_LOG;
	sector_t len = bio->bi_iter.bi_size / RRPC_EXPOSED_PAGE_SIZE;
	struct nvm_rq *rqd;

	while (1) {
		rqd = rrpc_inflight_laddr_acquire(rrpc, slba, len);
		if (rqd)
			break;

		schedule();
	}

	if (IS_ERR(rqd)) {
		pr_err("rrpc: unable to acquire inflight IO\n");
		bio_io_error(bio);
		return;
	}

	rrpc_invalidate_range(rrpc, slba, len);
	rrpc_inflight_laddr_release(rrpc, rqd);
}

static int block_is_full(struct rrpc *rrpc, struct rrpc_block *rblk)
{
	struct nvm_tgt_dev *dev = rrpc->dev;

	return (rblk->next_page == dev->geo.sec_per_blk);
}

/* Calculate relative addr for the given block, considering instantiated LUNs */
static u64 block_to_rel_addr(struct rrpc *rrpc, struct rrpc_block *rblk)
{
	struct nvm_tgt_dev *dev = rrpc->dev;
	struct rrpc_lun *rlun = rblk->rlun;

	return rlun->id * dev->geo.sec_per_blk;
}

static struct ppa_addr rrpc_ppa_to_gaddr(struct nvm_tgt_dev *dev,
					 struct rrpc_addr *gp)
{
	struct rrpc_block *rblk = gp->rblk;
	struct rrpc_lun *rlun = rblk->rlun;
	u64 addr = gp->addr;
	struct ppa_addr paddr;

	paddr.ppa = addr;
	paddr = rrpc_linear_to_generic_addr(&dev->geo, paddr);
	paddr.g.ch = rlun->bppa.g.ch;
	paddr.g.lun = rlun->bppa.g.lun;
	paddr.g.blk = rblk->id;

	return paddr;
}

/* requires lun->lock taken */
static void rrpc_set_lun_cur(struct rrpc_lun *rlun, struct rrpc_block *new_rblk,
						struct rrpc_block **cur_rblk)
{
	struct rrpc *rrpc = rlun->rrpc;

	if (*cur_rblk) {
		spin_lock(&(*cur_rblk)->lock);
		WARN_ON(!block_is_full(rrpc, *cur_rblk));
		spin_unlock(&(*cur_rblk)->lock);
	}
	*cur_rblk = new_rblk;
}

static struct rrpc_block *__rrpc_get_blk(struct rrpc *rrpc,
							struct rrpc_lun *rlun)
{
	struct rrpc_block *rblk = NULL;

	if (list_empty(&rlun->free_list))
		goto out;

	rblk = list_first_entry(&rlun->free_list, struct rrpc_block, list);

	list_move_tail(&rblk->list, &rlun->used_list);
	rblk->state = NVM_BLK_ST_TGT;
	rlun->nr_free_blocks--;

out:
	return rblk;
}

static struct rrpc_block *rrpc_get_blk(struct rrpc *rrpc, struct rrpc_lun *rlun,
							unsigned long flags)
{
	struct nvm_tgt_dev *dev = rrpc->dev;
	struct rrpc_block *rblk;
	int is_gc = flags & NVM_IOTYPE_GC;

	spin_lock(&rlun->lock);
	if (!is_gc && rlun->nr_free_blocks < rlun->reserved_blocks) {
		pr_err("nvm: rrpc: cannot give block to non GC request\n");
		spin_unlock(&rlun->lock);
		return NULL;
	}

	rblk = __rrpc_get_blk(rrpc, rlun);
	if (!rblk) {
		pr_err("nvm: rrpc: cannot get new block\n");
		spin_unlock(&rlun->lock);
		return NULL;
	}
	spin_unlock(&rlun->lock);

	bitmap_zero(rblk->invalid_pages, dev->geo.sec_per_blk);
	rblk->next_page = 0;
	rblk->nr_invalid_pages = 0;
	atomic_set(&rblk->data_cmnt_size, 0);

	return rblk;
}

static void rrpc_put_blk(struct rrpc *rrpc, struct rrpc_block *rblk)
{
	struct rrpc_lun *rlun = rblk->rlun;

	spin_lock(&rlun->lock);
	if (rblk->state & NVM_BLK_ST_TGT) {
		list_move_tail(&rblk->list, &rlun->free_list);
		rlun->nr_free_blocks++;
		rblk->state = NVM_BLK_ST_FREE;
	} else if (rblk->state & NVM_BLK_ST_BAD) {
		list_move_tail(&rblk->list, &rlun->bb_list);
		rblk->state = NVM_BLK_ST_BAD;
	} else {
		WARN_ON_ONCE(1);
		pr_err("rrpc: erroneous type (ch:%d,lun:%d,blk%d-> %u)\n",
					rlun->bppa.g.ch, rlun->bppa.g.lun,
					rblk->id, rblk->state);
		list_move_tail(&rblk->list, &rlun->bb_list);
	}
	spin_unlock(&rlun->lock);
}

static void rrpc_put_blks(struct rrpc *rrpc)
{
	struct rrpc_lun *rlun;
	int i;

	for (i = 0; i < rrpc->nr_luns; i++) {
		rlun = &rrpc->luns[i];
		if (rlun->cur)
			rrpc_put_blk(rrpc, rlun->cur);
		if (rlun->gc_cur)
			rrpc_put_blk(rrpc, rlun->gc_cur);
	}
}

static struct rrpc_lun *get_next_lun(struct rrpc *rrpc)
{
	int next = atomic_inc_return(&rrpc->next_lun);

	return &rrpc->luns[next % rrpc->nr_luns];
}

static void rrpc_gc_kick(struct rrpc *rrpc)
{
	struct rrpc_lun *rlun;
	unsigned int i;

	for (i = 0; i < rrpc->nr_luns; i++) {
		rlun = &rrpc->luns[i];
		queue_work(rrpc->krqd_wq, &rlun->ws_gc);
	}
}

/*
 * timed GC every interval.
 */
static void rrpc_gc_timer(unsigned long data)
{
	struct rrpc *rrpc = (struct rrpc *)data;

	rrpc_gc_kick(rrpc);
	mod_timer(&rrpc->gc_timer, jiffies + msecs_to_jiffies(10));
}

static void rrpc_end_sync_bio(struct bio *bio)
{
	struct completion *waiting = bio->bi_private;

	if (bio->bi_status)
		pr_err("nvm: gc request failed (%u).\n", bio->bi_status);

	complete(waiting);
}

/*
 * rrpc_move_valid_pages -- migrate live data off the block
 * @rrpc: the 'rrpc' structure
 * @block: the block from which to migrate live pages
 *
 * Description:
 *   GC algorithms may call this function to migrate remaining live
 *   pages off the block prior to erasing it. This function blocks
 *   further execution until the operation is complete.
 */
static int rrpc_move_valid_pages(struct rrpc *rrpc, struct rrpc_block *rblk)
{
	struct nvm_tgt_dev *dev = rrpc->dev;
	struct request_queue *q = dev->q;
	struct rrpc_rev_addr *rev;
	struct nvm_rq *rqd;
	struct bio *bio;
	struct page *page;
	int slot;
	int nr_sec_per_blk = dev->geo.sec_per_blk;
	u64 phys_addr;
	DECLARE_COMPLETION_ONSTACK(wait);

	if (bitmap_full(rblk->invalid_pages, nr_sec_per_blk))
		return 0;

	bio = bio_alloc(GFP_NOIO, 1);
	if (!bio) {
		pr_err("nvm: could not alloc bio to gc\n");
		return -ENOMEM;
	}

	page = mempool_alloc(rrpc->page_pool, GFP_NOIO);

	while ((slot = find_first_zero_bit(rblk->invalid_pages,
					    nr_sec_per_blk)) < nr_sec_per_blk) {

		/* Lock laddr */
		phys_addr = rrpc_blk_to_ppa(rrpc, rblk) + slot;

try:
		spin_lock(&rrpc->rev_lock);
		/* Get logical address from physical to logical table */
		rev = &rrpc->rev_trans_map[phys_addr];
		/* already updated by previous regular write */
		if (rev->addr == ADDR_EMPTY) {
			spin_unlock(&rrpc->rev_lock);
			continue;
		}

		rqd = rrpc_inflight_laddr_acquire(rrpc, rev->addr, 1);
		if (IS_ERR_OR_NULL(rqd)) {
			spin_unlock(&rrpc->rev_lock);
			schedule();
			goto try;
		}

		spin_unlock(&rrpc->rev_lock);

		/* Perform read to do GC */
		bio->bi_iter.bi_sector = rrpc_get_sector(rev->addr);
		bio_set_op_attrs(bio,  REQ_OP_READ, 0);
		bio->bi_private = &wait;
		bio->bi_end_io = rrpc_end_sync_bio;

		/* TODO: may fail when EXP_PG_SIZE > PAGE_SIZE */
		bio_add_pc_page(q, bio, page, RRPC_EXPOSED_PAGE_SIZE, 0);

		if (rrpc_submit_io(rrpc, bio, rqd, NVM_IOTYPE_GC)) {
			pr_err("rrpc: gc read failed.\n");
			rrpc_inflight_laddr_release(rrpc, rqd);
			goto finished;
		}
		wait_for_completion_io(&wait);
		if (bio->bi_status) {
			rrpc_inflight_laddr_release(rrpc, rqd);
			goto finished;
		}

		bio_reset(bio);
		reinit_completion(&wait);

		bio->bi_iter.bi_sector = rrpc_get_sector(rev->addr);
		bio_set_op_attrs(bio, REQ_OP_WRITE, 0);
		bio->bi_private = &wait;
		bio->bi_end_io = rrpc_end_sync_bio;

		bio_add_pc_page(q, bio, page, RRPC_EXPOSED_PAGE_SIZE, 0);

		/* turn the command around and write the data back to a new
		 * address
		 */
		if (rrpc_submit_io(rrpc, bio, rqd, NVM_IOTYPE_GC)) {
			pr_err("rrpc: gc write failed.\n");
			rrpc_inflight_laddr_release(rrpc, rqd);
			goto finished;
		}
		wait_for_completion_io(&wait);

		rrpc_inflight_laddr_release(rrpc, rqd);
		if (bio->bi_status)
			goto finished;

		bio_reset(bio);
	}

finished:
	mempool_free(page, rrpc->page_pool);
	bio_put(bio);

	if (!bitmap_full(rblk->invalid_pages, nr_sec_per_blk)) {
		pr_err("nvm: failed to garbage collect block\n");
		return -EIO;
	}

	return 0;
}

static void rrpc_block_gc(struct work_struct *work)
{
	struct rrpc_block_gc *gcb = container_of(work, struct rrpc_block_gc,
									ws_gc);
	struct rrpc *rrpc = gcb->rrpc;
	struct rrpc_block *rblk = gcb->rblk;
	struct rrpc_lun *rlun = rblk->rlun;
	struct ppa_addr ppa;

	mempool_free(gcb, rrpc->gcb_pool);
	pr_debug("nvm: block 'ch:%d,lun:%d,blk:%d' being reclaimed\n",
			rlun->bppa.g.ch, rlun->bppa.g.lun,
			rblk->id);

	if (rrpc_move_valid_pages(rrpc, rblk))
		goto put_back;

	ppa.ppa = 0;
	ppa.g.ch = rlun->bppa.g.ch;
	ppa.g.lun = rlun->bppa.g.lun;
	ppa.g.blk = rblk->id;

	if (nvm_erase_sync(rrpc->dev, &ppa, 1))
		goto put_back;

	rrpc_put_blk(rrpc, rblk);

	return;

put_back:
	spin_lock(&rlun->lock);
	list_add_tail(&rblk->prio, &rlun->prio_list);
	spin_unlock(&rlun->lock);
}

/* the block with highest number of invalid pages, will be in the beginning
 * of the list
 */
static struct rrpc_block *rblk_max_invalid(struct rrpc_block *ra,
							struct rrpc_block *rb)
{
	if (ra->nr_invalid_pages == rb->nr_invalid_pages)
		return ra;

	return (ra->nr_invalid_pages < rb->nr_invalid_pages) ? rb : ra;
}

/* linearly find the block with highest number of invalid pages
 * requires lun->lock
 */
static struct rrpc_block *block_prio_find_max(struct rrpc_lun *rlun)
{
	struct list_head *prio_list = &rlun->prio_list;
	struct rrpc_block *rblk, *max;

	BUG_ON(list_empty(prio_list));

	max = list_first_entry(prio_list, struct rrpc_block, prio);
	list_for_each_entry(rblk, prio_list, prio)
		max = rblk_max_invalid(max, rblk);

	return max;
}

static void rrpc_lun_gc(struct work_struct *work)
{
	struct rrpc_lun *rlun = container_of(work, struct rrpc_lun, ws_gc);
	struct rrpc *rrpc = rlun->rrpc;
	struct nvm_tgt_dev *dev = rrpc->dev;
	struct rrpc_block_gc *gcb;
	unsigned int nr_blocks_need;

	nr_blocks_need = dev->geo.blks_per_lun / GC_LIMIT_INVERSE;

	if (nr_blocks_need < rrpc->nr_luns)
		nr_blocks_need = rrpc->nr_luns;

	spin_lock(&rlun->lock);
	while (nr_blocks_need > rlun->nr_free_blocks &&
					!list_empty(&rlun->prio_list)) {
		struct rrpc_block *rblk = block_prio_find_max(rlun);

		if (!rblk->nr_invalid_pages)
			break;

		gcb = mempool_alloc(rrpc->gcb_pool, GFP_ATOMIC);
		if (!gcb)
			break;

		list_del_init(&rblk->prio);

		WARN_ON(!block_is_full(rrpc, rblk));

		pr_debug("rrpc: selected block 'ch:%d,lun:%d,blk:%d' for GC\n",
					rlun->bppa.g.ch, rlun->bppa.g.lun,
					rblk->id);

		gcb->rrpc = rrpc;
		gcb->rblk = rblk;
		INIT_WORK(&gcb->ws_gc, rrpc_block_gc);

		queue_work(rrpc->kgc_wq, &gcb->ws_gc);

		nr_blocks_need--;
	}
	spin_unlock(&rlun->lock);

	/* TODO: Hint that request queue can be started again */
}

static void rrpc_gc_queue(struct work_struct *work)
{
	struct rrpc_block_gc *gcb = container_of(work, struct rrpc_block_gc,
									ws_gc);
	struct rrpc *rrpc = gcb->rrpc;
	struct rrpc_block *rblk = gcb->rblk;
	struct rrpc_lun *rlun = rblk->rlun;

	spin_lock(&rlun->lock);
	list_add_tail(&rblk->prio, &rlun->prio_list);
	spin_unlock(&rlun->lock);

	mempool_free(gcb, rrpc->gcb_pool);
	pr_debug("nvm: block 'ch:%d,lun:%d,blk:%d' full, allow GC (sched)\n",
					rlun->bppa.g.ch, rlun->bppa.g.lun,
					rblk->id);
}

static const struct block_device_operations rrpc_fops = {
	.owner		= THIS_MODULE,
};

static struct rrpc_lun *rrpc_get_lun_rr(struct rrpc *rrpc, int is_gc)
{
	unsigned int i;
	struct rrpc_lun *rlun, *max_free;

	if (!is_gc)
		return get_next_lun(rrpc);

	/* during GC, we don't care about RR, instead we want to make
	 * sure that we maintain evenness between the block luns.
	 */
	max_free = &rrpc->luns[0];
	/* prevent GC-ing lun from devouring pages of a lun with
	 * little free blocks. We don't take the lock as we only need an
	 * estimate.
	 */
	rrpc_for_each_lun(rrpc, rlun, i) {
		if (rlun->nr_free_blocks > max_free->nr_free_blocks)
			max_free = rlun;
	}

	return max_free;
}

static struct rrpc_addr *rrpc_update_map(struct rrpc *rrpc, sector_t laddr,
					struct rrpc_block *rblk, u64 paddr)
{
	struct rrpc_addr *gp;
	struct rrpc_rev_addr *rev;

	BUG_ON(laddr >= rrpc->nr_sects);

	gp = &rrpc->trans_map[laddr];
	spin_lock(&rrpc->rev_lock);
	if (gp->rblk)
		rrpc_page_invalidate(rrpc, gp);

	gp->addr = paddr;
	gp->rblk = rblk;

	rev = &rrpc->rev_trans_map[gp->addr];
	rev->addr = laddr;
	spin_unlock(&rrpc->rev_lock);

	return gp;
}

static u64 rrpc_alloc_addr(struct rrpc *rrpc, struct rrpc_block *rblk)
{
	u64 addr = ADDR_EMPTY;

	spin_lock(&rblk->lock);
	if (block_is_full(rrpc, rblk))
		goto out;

	addr = rblk->next_page;

	rblk->next_page++;
out:
	spin_unlock(&rblk->lock);
	return addr;
}

/* Map logical address to a physical page. The mapping implements a round robin
 * approach and allocates a page from the next lun available.
 *
 * Returns rrpc_addr with the physical address and block. Returns NULL if no
 * blocks in the next rlun are available.
 */
static struct ppa_addr rrpc_map_page(struct rrpc *rrpc, sector_t laddr,
								int is_gc)
{
	struct nvm_tgt_dev *tgt_dev = rrpc->dev;
	struct rrpc_lun *rlun;
	struct rrpc_block *rblk, **cur_rblk;
	struct rrpc_addr *p;
	struct ppa_addr ppa;
	u64 paddr;
	int gc_force = 0;

	ppa.ppa = ADDR_EMPTY;
	rlun = rrpc_get_lun_rr(rrpc, is_gc);

	if (!is_gc && rlun->nr_free_blocks < rrpc->nr_luns * 4)
		return ppa;

	/*
	 * page allocation steps:
	 * 1. Try to allocate new page from current rblk
	 * 2a. If succeed, proceed to map it in and return
	 * 2b. If fail, first try to allocate a new block from media manger,
	 *     and then retry step 1. Retry until the normal block pool is
	 *     exhausted.
	 * 3. If exhausted, and garbage collector is requesting the block,
	 *    go to the reserved block and retry step 1.
	 *    In the case that this fails as well, or it is not GC
	 *    requesting, report not able to retrieve a block and let the
	 *    caller handle further processing.
	 */

	spin_lock(&rlun->lock);
	cur_rblk = &rlun->cur;
	rblk = rlun->cur;
retry:
	paddr = rrpc_alloc_addr(rrpc, rblk);

	if (paddr != ADDR_EMPTY)
		goto done;

	if (!list_empty(&rlun->wblk_list)) {
new_blk:
		rblk = list_first_entry(&rlun->wblk_list, struct rrpc_block,
									prio);
		rrpc_set_lun_cur(rlun, rblk, cur_rblk);
		list_del(&rblk->prio);
		goto retry;
	}
	spin_unlock(&rlun->lock);

	rblk = rrpc_get_blk(rrpc, rlun, gc_force);
	if (rblk) {
		spin_lock(&rlun->lock);
		list_add_tail(&rblk->prio, &rlun->wblk_list);
		/*
		 * another thread might already have added a new block,
		 * Therefore, make sure that one is used, instead of the
		 * one just added.
		 */
		goto new_blk;
	}

	if (unlikely(is_gc) && !gc_force) {
		/* retry from emergency gc block */
		cur_rblk = &rlun->gc_cur;
		rblk = rlun->gc_cur;
		gc_force = 1;
		spin_lock(&rlun->lock);
		goto retry;
	}

	pr_err("rrpc: failed to allocate new block\n");
	return ppa;
done:
	spin_unlock(&rlun->lock);
	p = rrpc_update_map(rrpc, laddr, rblk, paddr);
	if (!p)
		return ppa;

	/* return global address */
	return rrpc_ppa_to_gaddr(tgt_dev, p);
}

static void rrpc_run_gc(struct rrpc *rrpc, struct rrpc_block *rblk)
{
	struct rrpc_block_gc *gcb;

	gcb = mempool_alloc(rrpc->gcb_pool, GFP_ATOMIC);
	if (!gcb) {
		pr_err("rrpc: unable to queue block for gc.");
		return;
	}

	gcb->rrpc = rrpc;
	gcb->rblk = rblk;

	INIT_WORK(&gcb->ws_gc, rrpc_gc_queue);
	queue_work(rrpc->kgc_wq, &gcb->ws_gc);
}

static struct rrpc_lun *rrpc_ppa_to_lun(struct rrpc *rrpc, struct ppa_addr p)
{
	struct rrpc_lun *rlun = NULL;
	int i;

	for (i = 0; i < rrpc->nr_luns; i++) {
		if (rrpc->luns[i].bppa.g.ch == p.g.ch &&
				rrpc->luns[i].bppa.g.lun == p.g.lun) {
			rlun = &rrpc->luns[i];
			break;
		}
	}

	return rlun;
}

static void __rrpc_mark_bad_block(struct rrpc *rrpc, struct ppa_addr ppa)
{
	struct nvm_tgt_dev *dev = rrpc->dev;
	struct rrpc_lun *rlun;
	struct rrpc_block *rblk;

	rlun = rrpc_ppa_to_lun(rrpc, ppa);
	rblk = &rlun->blocks[ppa.g.blk];
	rblk->state = NVM_BLK_ST_BAD;

	nvm_set_tgt_bb_tbl(dev, &ppa, 1, NVM_BLK_T_GRWN_BAD);
}

static void rrpc_mark_bad_block(struct rrpc *rrpc, struct nvm_rq *rqd)
{
	void *comp_bits = &rqd->ppa_status;
	struct ppa_addr ppa, prev_ppa;
	int nr_ppas = rqd->nr_ppas;
	int bit;

	if (rqd->nr_ppas == 1)
		__rrpc_mark_bad_block(rrpc, rqd->ppa_addr);

	ppa_set_empty(&prev_ppa);
	bit = -1;
	while ((bit = find_next_bit(comp_bits, nr_ppas, bit + 1)) < nr_ppas) {
		ppa = rqd->ppa_list[bit];
		if (ppa_cmp_blk(ppa, prev_ppa))
			continue;

		__rrpc_mark_bad_block(rrpc, ppa);
	}
}

static void rrpc_end_io_write(struct rrpc *rrpc, struct rrpc_rq *rrqd,
						sector_t laddr, uint8_t npages)
{
	struct nvm_tgt_dev *dev = rrpc->dev;
	struct rrpc_addr *p;
	struct rrpc_block *rblk;
	int cmnt_size, i;

	for (i = 0; i < npages; i++) {
		p = &rrpc->trans_map[laddr + i];
		rblk = p->rblk;

		cmnt_size = atomic_inc_return(&rblk->data_cmnt_size);
		if (unlikely(cmnt_size == dev->geo.sec_per_blk))
			rrpc_run_gc(rrpc, rblk);
	}
}

static void rrpc_end_io(struct nvm_rq *rqd)
{
	struct rrpc *rrpc = rqd->private;
	struct nvm_tgt_dev *dev = rrpc->dev;
	struct rrpc_rq *rrqd = nvm_rq_to_pdu(rqd);
	uint8_t npages = rqd->nr_ppas;
	sector_t laddr = rrpc_get_laddr(rqd->bio) - npages;

	if (bio_data_dir(rqd->bio) == WRITE) {
		if (rqd->error == NVM_RSP_ERR_FAILWRITE)
			rrpc_mark_bad_block(rrpc, rqd);

		rrpc_end_io_write(rrpc, rrqd, laddr, npages);
	}

	bio_put(rqd->bio);

	if (rrqd->flags & NVM_IOTYPE_GC)
		return;

	rrpc_unlock_rq(rrpc, rqd);

	if (npages > 1)
		nvm_dev_dma_free(dev->parent, rqd->ppa_list, rqd->dma_ppa_list);

	mempool_free(rqd, rrpc->rq_pool);
}

static int rrpc_read_ppalist_rq(struct rrpc *rrpc, struct bio *bio,
			struct nvm_rq *rqd, unsigned long flags, int npages)
{
	struct nvm_tgt_dev *dev = rrpc->dev;
	struct rrpc_inflight_rq *r = rrpc_get_inflight_rq(rqd);
	struct rrpc_addr *gp;
	sector_t laddr = rrpc_get_laddr(bio);
	int is_gc = flags & NVM_IOTYPE_GC;
	int i;

	if (!is_gc && rrpc_lock_rq(rrpc, bio, rqd)) {
		nvm_dev_dma_free(dev->parent, rqd->ppa_list, rqd->dma_ppa_list);
		return NVM_IO_REQUEUE;
	}

	for (i = 0; i < npages; i++) {
		/* We assume that mapping occurs at 4KB granularity */
		BUG_ON(!(laddr + i < rrpc->nr_sects));
		gp = &rrpc->trans_map[laddr + i];

		if (gp->rblk) {
			rqd->ppa_list[i] = rrpc_ppa_to_gaddr(dev, gp);
		} else {
			BUG_ON(is_gc);
			rrpc_unlock_laddr(rrpc, r);
			nvm_dev_dma_free(dev->parent, rqd->ppa_list,
							rqd->dma_ppa_list);
			return NVM_IO_DONE;
		}
	}

	rqd->opcode = NVM_OP_HBREAD;

	return NVM_IO_OK;
}

static int rrpc_read_rq(struct rrpc *rrpc, struct bio *bio, struct nvm_rq *rqd,
							unsigned long flags)
{
	int is_gc = flags & NVM_IOTYPE_GC;
	sector_t laddr = rrpc_get_laddr(bio);
	struct rrpc_addr *gp;

	if (!is_gc && rrpc_lock_rq(rrpc, bio, rqd))
		return NVM_IO_REQUEUE;

	BUG_ON(!(laddr < rrpc->nr_sects));
	gp = &rrpc->trans_map[laddr];

	if (gp->rblk) {
		rqd->ppa_addr = rrpc_ppa_to_gaddr(rrpc->dev, gp);
	} else {
		BUG_ON(is_gc);
		rrpc_unlock_rq(rrpc, rqd);
		return NVM_IO_DONE;
	}

	rqd->opcode = NVM_OP_HBREAD;

	return NVM_IO_OK;
}

static int rrpc_write_ppalist_rq(struct rrpc *rrpc, struct bio *bio,
			struct nvm_rq *rqd, unsigned long flags, int npages)
{
	struct nvm_tgt_dev *dev = rrpc->dev;
	struct rrpc_inflight_rq *r = rrpc_get_inflight_rq(rqd);
	struct ppa_addr p;
	sector_t laddr = rrpc_get_laddr(bio);
	int is_gc = flags & NVM_IOTYPE_GC;
	int i;

	if (!is_gc && rrpc_lock_rq(rrpc, bio, rqd)) {
		nvm_dev_dma_free(dev->parent, rqd->ppa_list, rqd->dma_ppa_list);
		return NVM_IO_REQUEUE;
	}

	for (i = 0; i < npages; i++) {
		/* We assume that mapping occurs at 4KB granularity */
		p = rrpc_map_page(rrpc, laddr + i, is_gc);
		if (p.ppa == ADDR_EMPTY) {
			BUG_ON(is_gc);
			rrpc_unlock_laddr(rrpc, r);
			nvm_dev_dma_free(dev->parent, rqd->ppa_list,
							rqd->dma_ppa_list);
			rrpc_gc_kick(rrpc);
			return NVM_IO_REQUEUE;
		}

		rqd->ppa_list[i] = p;
	}

	rqd->opcode = NVM_OP_HBWRITE;

	return NVM_IO_OK;
}

static int rrpc_write_rq(struct rrpc *rrpc, struct bio *bio,
				struct nvm_rq *rqd, unsigned long flags)
{
	struct ppa_addr p;
	int is_gc = flags & NVM_IOTYPE_GC;
	sector_t laddr = rrpc_get_laddr(bio);

	if (!is_gc && rrpc_lock_rq(rrpc, bio, rqd))
		return NVM_IO_REQUEUE;

	p = rrpc_map_page(rrpc, laddr, is_gc);
	if (p.ppa == ADDR_EMPTY) {
		BUG_ON(is_gc);
		rrpc_unlock_rq(rrpc, rqd);
		rrpc_gc_kick(rrpc);
		return NVM_IO_REQUEUE;
	}

	rqd->ppa_addr = p;
	rqd->opcode = NVM_OP_HBWRITE;

	return NVM_IO_OK;
}

static int rrpc_setup_rq(struct rrpc *rrpc, struct bio *bio,
			struct nvm_rq *rqd, unsigned long flags, uint8_t npages)
{
	struct nvm_tgt_dev *dev = rrpc->dev;

	if (npages > 1) {
		rqd->ppa_list = nvm_dev_dma_alloc(dev->parent, GFP_KERNEL,
							&rqd->dma_ppa_list);
		if (!rqd->ppa_list) {
			pr_err("rrpc: not able to allocate ppa list\n");
			return NVM_IO_ERR;
		}

		if (bio_op(bio) == REQ_OP_WRITE)
			return rrpc_write_ppalist_rq(rrpc, bio, rqd, flags,
									npages);

		return rrpc_read_ppalist_rq(rrpc, bio, rqd, flags, npages);
	}

	if (bio_op(bio) == REQ_OP_WRITE)
		return rrpc_write_rq(rrpc, bio, rqd, flags);

	return rrpc_read_rq(rrpc, bio, rqd, flags);
}

static int rrpc_submit_io(struct rrpc *rrpc, struct bio *bio,
				struct nvm_rq *rqd, unsigned long flags)
{
	struct nvm_tgt_dev *dev = rrpc->dev;
	struct rrpc_rq *rrq = nvm_rq_to_pdu(rqd);
	uint8_t nr_pages = rrpc_get_pages(bio);
	int bio_size = bio_sectors(bio) << 9;
	int err;

	if (bio_size < dev->geo.sec_size)
		return NVM_IO_ERR;
	else if (bio_size > dev->geo.max_rq_size)
		return NVM_IO_ERR;

	err = rrpc_setup_rq(rrpc, bio, rqd, flags, nr_pages);
	if (err)
		return err;

	bio_get(bio);
	rqd->bio = bio;
	rqd->private = rrpc;
	rqd->nr_ppas = nr_pages;
	rqd->end_io = rrpc_end_io;
	rrq->flags = flags;

	err = nvm_submit_io(dev, rqd);
	if (err) {
		pr_err("rrpc: I/O submission failed: %d\n", err);
		bio_put(bio);
		if (!(flags & NVM_IOTYPE_GC)) {
			rrpc_unlock_rq(rrpc, rqd);
			if (rqd->nr_ppas > 1)
				nvm_dev_dma_free(dev->parent, rqd->ppa_list,
							rqd->dma_ppa_list);
		}
		return NVM_IO_ERR;
	}

	return NVM_IO_OK;
}

static blk_qc_t rrpc_make_rq(struct request_queue *q, struct bio *bio)
{
	struct rrpc *rrpc = q->queuedata;
	struct nvm_rq *rqd;
	int err;

	blk_queue_split(q, &bio);

	if (bio_op(bio) == REQ_OP_DISCARD) {
		rrpc_discard(rrpc, bio);
		return BLK_QC_T_NONE;
	}

	rqd = mempool_alloc(rrpc->rq_pool, GFP_KERNEL);
	memset(rqd, 0, sizeof(struct nvm_rq));

	err = rrpc_submit_io(rrpc, bio, rqd, NVM_IOTYPE_NONE);
	switch (err) {
	case NVM_IO_OK:
		return BLK_QC_T_NONE;
	case NVM_IO_ERR:
		bio_io_error(bio);
		break;
	case NVM_IO_DONE:
		bio_endio(bio);
		break;
	case NVM_IO_REQUEUE:
		spin_lock(&rrpc->bio_lock);
		bio_list_add(&rrpc->requeue_bios, bio);
		spin_unlock(&rrpc->bio_lock);
		queue_work(rrpc->kgc_wq, &rrpc->ws_requeue);
		break;
	}

	mempool_free(rqd, rrpc->rq_pool);
	return BLK_QC_T_NONE;
}

static void rrpc_requeue(struct work_struct *work)
{
	struct rrpc *rrpc = container_of(work, struct rrpc, ws_requeue);
	struct bio_list bios;
	struct bio *bio;

	bio_list_init(&bios);

	spin_lock(&rrpc->bio_lock);
	bio_list_merge(&bios, &rrpc->requeue_bios);
	bio_list_init(&rrpc->requeue_bios);
	spin_unlock(&rrpc->bio_lock);

	while ((bio = bio_list_pop(&bios)))
		rrpc_make_rq(rrpc->disk->queue, bio);
}

static void rrpc_gc_free(struct rrpc *rrpc)
{
	if (rrpc->krqd_wq)
		destroy_workqueue(rrpc->krqd_wq);

	if (rrpc->kgc_wq)
		destroy_workqueue(rrpc->kgc_wq);
}

static int rrpc_gc_init(struct rrpc *rrpc)
{
	rrpc->krqd_wq = alloc_workqueue("rrpc-lun", WQ_MEM_RECLAIM|WQ_UNBOUND,
								rrpc->nr_luns);
	if (!rrpc->krqd_wq)
		return -ENOMEM;

	rrpc->kgc_wq = alloc_workqueue("rrpc-bg", WQ_MEM_RECLAIM, 1);
	if (!rrpc->kgc_wq)
		return -ENOMEM;

	setup_timer(&rrpc->gc_timer, rrpc_gc_timer, (unsigned long)rrpc);

	return 0;
}

static void rrpc_map_free(struct rrpc *rrpc)
{
	vfree(rrpc->rev_trans_map);
	vfree(rrpc->trans_map);
}

static int rrpc_l2p_update(u64 slba, u32 nlb, __le64 *entries, void *private)
{
	struct rrpc *rrpc = (struct rrpc *)private;
	struct nvm_tgt_dev *dev = rrpc->dev;
	struct rrpc_addr *addr = rrpc->trans_map + slba;
	struct rrpc_rev_addr *raddr = rrpc->rev_trans_map;
	struct rrpc_lun *rlun;
	struct rrpc_block *rblk;
	u64 i;

	for (i = 0; i < nlb; i++) {
		struct ppa_addr gaddr;
		u64 pba = le64_to_cpu(entries[i]);
		unsigned int mod;

		/* LNVM treats address-spaces as silos, LBA and PBA are
		 * equally large and zero-indexed.
		 */
		if (unlikely(pba >= dev->total_secs && pba != U64_MAX)) {
			pr_err("nvm: L2P data entry is out of bounds!\n");
			pr_err("nvm: Maybe loaded an old target L2P\n");
			return -EINVAL;
		}

		/* Address zero is a special one. The first page on a disk is
		 * protected. As it often holds internal device boot
		 * information.
		 */
		if (!pba)
			continue;

		div_u64_rem(pba, rrpc->nr_sects, &mod);

		gaddr = rrpc_recov_addr(dev, pba);
		rlun = rrpc_ppa_to_lun(rrpc, gaddr);
		if (!rlun) {
			pr_err("rrpc: l2p corruption on lba %llu\n",
							slba + i);
			return -EINVAL;
		}

		rblk = &rlun->blocks[gaddr.g.blk];
		if (!rblk->state) {
			/* at this point, we don't know anything about the
			 * block. It's up to the FTL on top to re-etablish the
			 * block state. The block is assumed to be open.
			 */
			list_move_tail(&rblk->list, &rlun->used_list);
			rblk->state = NVM_BLK_ST_TGT;
			rlun->nr_free_blocks--;
		}

		addr[i].addr = pba;
		addr[i].rblk = rblk;
		raddr[mod].addr = slba + i;
	}

	return 0;
}

static int rrpc_map_init(struct rrpc *rrpc)
{
	struct nvm_tgt_dev *dev = rrpc->dev;
	sector_t i;
	int ret;

	rrpc->trans_map = vzalloc(sizeof(struct rrpc_addr) * rrpc->nr_sects);
	if (!rrpc->trans_map)
		return -ENOMEM;

	rrpc->rev_trans_map = vmalloc(sizeof(struct rrpc_rev_addr)
							* rrpc->nr_sects);
	if (!rrpc->rev_trans_map)
		return -ENOMEM;

	for (i = 0; i < rrpc->nr_sects; i++) {
		struct rrpc_addr *p = &rrpc->trans_map[i];
		struct rrpc_rev_addr *r = &rrpc->rev_trans_map[i];

		p->addr = ADDR_EMPTY;
		r->addr = ADDR_EMPTY;
	}

	/* Bring up the mapping table from device */
	ret = nvm_get_l2p_tbl(dev, rrpc->soffset, rrpc->nr_sects,
							rrpc_l2p_update, rrpc);
	if (ret) {
		pr_err("nvm: rrpc: could not read L2P table.\n");
		return -EINVAL;
	}

	return 0;
}

/* Minimum pages needed within a lun */
#define PAGE_POOL_SIZE 16
#define ADDR_POOL_SIZE 64

static int rrpc_core_init(struct rrpc *rrpc)
{
	down_write(&rrpc_lock);
	if (!rrpc_gcb_cache) {
		rrpc_gcb_cache = kmem_cache_create("rrpc_gcb",
				sizeof(struct rrpc_block_gc), 0, 0, NULL);
		if (!rrpc_gcb_cache) {
			up_write(&rrpc_lock);
			return -ENOMEM;
		}

		rrpc_rq_cache = kmem_cache_create("rrpc_rq",
				sizeof(struct nvm_rq) + sizeof(struct rrpc_rq),
				0, 0, NULL);
		if (!rrpc_rq_cache) {
			kmem_cache_destroy(rrpc_gcb_cache);
			up_write(&rrpc_lock);
			return -ENOMEM;
		}
	}
	up_write(&rrpc_lock);

	rrpc->page_pool = mempool_create_page_pool(PAGE_POOL_SIZE, 0);
	if (!rrpc->page_pool)
		return -ENOMEM;

	rrpc->gcb_pool = mempool_create_slab_pool(rrpc->dev->geo.nr_luns,
								rrpc_gcb_cache);
	if (!rrpc->gcb_pool)
		return -ENOMEM;

	rrpc->rq_pool = mempool_create_slab_pool(64, rrpc_rq_cache);
	if (!rrpc->rq_pool)
		return -ENOMEM;

	spin_lock_init(&rrpc->inflights.lock);
	INIT_LIST_HEAD(&rrpc->inflights.reqs);

	return 0;
}

static void rrpc_core_free(struct rrpc *rrpc)
{
	mempool_destroy(rrpc->page_pool);
	mempool_destroy(rrpc->gcb_pool);
	mempool_destroy(rrpc->rq_pool);
}

static void rrpc_luns_free(struct rrpc *rrpc)
{
	struct rrpc_lun *rlun;
	int i;

	if (!rrpc->luns)
		return;

	for (i = 0; i < rrpc->nr_luns; i++) {
		rlun = &rrpc->luns[i];
		vfree(rlun->blocks);
	}

	kfree(rrpc->luns);
}

static int rrpc_bb_discovery(struct nvm_tgt_dev *dev, struct rrpc_lun *rlun)
{
	struct nvm_geo *geo = &dev->geo;
	struct rrpc_block *rblk;
	struct ppa_addr ppa;
	u8 *blks;
	int nr_blks;
	int i;
	int ret;

	if (!dev->parent->ops->get_bb_tbl)
		return 0;

	nr_blks = geo->blks_per_lun * geo->plane_mode;
	blks = kmalloc(nr_blks, GFP_KERNEL);
	if (!blks)
		return -ENOMEM;

	ppa.ppa = 0;
	ppa.g.ch = rlun->bppa.g.ch;
	ppa.g.lun = rlun->bppa.g.lun;

	ret = nvm_get_tgt_bb_tbl(dev, ppa, blks);
	if (ret) {
		pr_err("rrpc: could not get BB table\n");
		goto out;
	}

	nr_blks = nvm_bb_tbl_fold(dev->parent, blks, nr_blks);
	if (nr_blks < 0) {
		ret = nr_blks;
		goto out;
	}

	for (i = 0; i < nr_blks; i++) {
		if (blks[i] == NVM_BLK_T_FREE)
			continue;

		rblk = &rlun->blocks[i];
		list_move_tail(&rblk->list, &rlun->bb_list);
		rblk->state = NVM_BLK_ST_BAD;
		rlun->nr_free_blocks--;
	}

out:
	kfree(blks);
	return ret;
}

static void rrpc_set_lun_ppa(struct rrpc_lun *rlun, struct ppa_addr ppa)
{
	rlun->bppa.ppa = 0;
	rlun->bppa.g.ch = ppa.g.ch;
	rlun->bppa.g.lun = ppa.g.lun;
}

static int rrpc_luns_init(struct rrpc *rrpc, struct ppa_addr *luns)
{
	struct nvm_tgt_dev *dev = rrpc->dev;
	struct nvm_geo *geo = &dev->geo;
	struct rrpc_lun *rlun;
	int i, j, ret = -EINVAL;

	if (geo->sec_per_blk > MAX_INVALID_PAGES_STORAGE * BITS_PER_LONG) {
		pr_err("rrpc: number of pages per block too high.");
		return -EINVAL;
	}

	spin_lock_init(&rrpc->rev_lock);

	rrpc->luns = kcalloc(rrpc->nr_luns, sizeof(struct rrpc_lun),
								GFP_KERNEL);
	if (!rrpc->luns)
		return -ENOMEM;

	/* 1:1 mapping */
	for (i = 0; i < rrpc->nr_luns; i++) {
		rlun = &rrpc->luns[i];
		rlun->id = i;
		rrpc_set_lun_ppa(rlun, luns[i]);
		rlun->blocks = vzalloc(sizeof(struct rrpc_block) *
							geo->blks_per_lun);
		if (!rlun->blocks) {
			ret = -ENOMEM;
			goto err;
		}

		INIT_LIST_HEAD(&rlun->free_list);
		INIT_LIST_HEAD(&rlun->used_list);
		INIT_LIST_HEAD(&rlun->bb_list);

		for (j = 0; j < geo->blks_per_lun; j++) {
			struct rrpc_block *rblk = &rlun->blocks[j];

			rblk->id = j;
			rblk->rlun = rlun;
			rblk->state = NVM_BLK_T_FREE;
			INIT_LIST_HEAD(&rblk->prio);
			INIT_LIST_HEAD(&rblk->list);
			spin_lock_init(&rblk->lock);

			list_add_tail(&rblk->list, &rlun->free_list);
		}

		rlun->rrpc = rrpc;
		rlun->nr_free_blocks = geo->blks_per_lun;
		rlun->reserved_blocks = 2; /* for GC only */

		INIT_LIST_HEAD(&rlun->prio_list);
		INIT_LIST_HEAD(&rlun->wblk_list);

		INIT_WORK(&rlun->ws_gc, rrpc_lun_gc);
		spin_lock_init(&rlun->lock);

		if (rrpc_bb_discovery(dev, rlun))
			goto err;

	}

	return 0;
err:
	return ret;
}

/* returns 0 on success and stores the beginning address in *begin */
static int rrpc_area_init(struct rrpc *rrpc, sector_t *begin)
{
	struct nvm_tgt_dev *dev = rrpc->dev;
	sector_t size = rrpc->nr_sects * dev->geo.sec_size;
	int ret;

	size >>= 9;

	ret = nvm_get_area(dev, begin, size);
	if (!ret)
		*begin >>= (ilog2(dev->geo.sec_size) - 9);

	return ret;
}

static void rrpc_area_free(struct rrpc *rrpc)
{
	struct nvm_tgt_dev *dev = rrpc->dev;
	sector_t begin = rrpc->soffset << (ilog2(dev->geo.sec_size) - 9);

	nvm_put_area(dev, begin);
}

static void rrpc_free(struct rrpc *rrpc)
{
	rrpc_gc_free(rrpc);
	rrpc_map_free(rrpc);
	rrpc_core_free(rrpc);
	rrpc_luns_free(rrpc);
	rrpc_area_free(rrpc);

	kfree(rrpc);
}

static void rrpc_exit(void *private)
{
	struct rrpc *rrpc = private;

	del_timer(&rrpc->gc_timer);

	flush_workqueue(rrpc->krqd_wq);
	flush_workqueue(rrpc->kgc_wq);

	rrpc_free(rrpc);
}

static sector_t rrpc_capacity(void *private)
{
	struct rrpc *rrpc = private;
	struct nvm_tgt_dev *dev = rrpc->dev;
	sector_t reserved, provisioned;

	/* cur, gc, and two emergency blocks for each lun */
	reserved = rrpc->nr_luns * dev->geo.sec_per_blk * 4;
	provisioned = rrpc->nr_sects - reserved;

	if (reserved > rrpc->nr_sects) {
		pr_err("rrpc: not enough space available to expose storage.\n");
		return 0;
	}

	sector_div(provisioned, 10);
	return provisioned * 9 * NR_PHY_IN_LOG;
}

/*
 * Looks up the logical address from reverse trans map and check if its valid by
 * comparing the logical to physical address with the physical address.
 * Returns 0 on free, otherwise 1 if in use
 */
static void rrpc_block_map_update(struct rrpc *rrpc, struct rrpc_block *rblk)
{
	struct nvm_tgt_dev *dev = rrpc->dev;
	int offset;
	struct rrpc_addr *laddr;
	u64 bpaddr, paddr, pladdr;

	bpaddr = block_to_rel_addr(rrpc, rblk);
	for (offset = 0; offset < dev->geo.sec_per_blk; offset++) {
		paddr = bpaddr + offset;

		pladdr = rrpc->rev_trans_map[paddr].addr;
		if (pladdr == ADDR_EMPTY)
			continue;

		laddr = &rrpc->trans_map[pladdr];

		if (paddr == laddr->addr) {
			laddr->rblk = rblk;
		} else {
			set_bit(offset, rblk->invalid_pages);
			rblk->nr_invalid_pages++;
		}
	}
}

static int rrpc_blocks_init(struct rrpc *rrpc)
{
	struct nvm_tgt_dev *dev = rrpc->dev;
	struct rrpc_lun *rlun;
	struct rrpc_block *rblk;
	int lun_iter, blk_iter;

	for (lun_iter = 0; lun_iter < rrpc->nr_luns; lun_iter++) {
		rlun = &rrpc->luns[lun_iter];

		for (blk_iter = 0; blk_iter < dev->geo.blks_per_lun;
								blk_iter++) {
			rblk = &rlun->blocks[blk_iter];
			rrpc_block_map_update(rrpc, rblk);
		}
	}

	return 0;
}

static int rrpc_luns_configure(struct rrpc *rrpc)
{
	struct rrpc_lun *rlun;
	struct rrpc_block *rblk;
	int i;

	for (i = 0; i < rrpc->nr_luns; i++) {
		rlun = &rrpc->luns[i];

		rblk = rrpc_get_blk(rrpc, rlun, 0);
		if (!rblk)
			goto err;
		rrpc_set_lun_cur(rlun, rblk, &rlun->cur);

		/* Emergency gc block */
		rblk = rrpc_get_blk(rrpc, rlun, 1);
		if (!rblk)
			goto err;
		rrpc_set_lun_cur(rlun, rblk, &rlun->gc_cur);
	}

	return 0;
err:
	rrpc_put_blks(rrpc);
	return -EINVAL;
}

static struct nvm_tgt_type tt_rrpc;

static void *rrpc_init(struct nvm_tgt_dev *dev, struct gendisk *tdisk,
		       int flags)
{
	struct request_queue *bqueue = dev->q;
	struct request_queue *tqueue = tdisk->queue;
	struct nvm_geo *geo = &dev->geo;
	struct rrpc *rrpc;
	sector_t soffset;
	int ret;

	if (!(dev->identity.dom & NVM_RSP_L2P)) {
		pr_err("nvm: rrpc: device does not support l2p (%x)\n",
							dev->identity.dom);
		return ERR_PTR(-EINVAL);
	}

	rrpc = kzalloc(sizeof(struct rrpc), GFP_KERNEL);
	if (!rrpc)
		return ERR_PTR(-ENOMEM);

	rrpc->dev = dev;
	rrpc->disk = tdisk;

	bio_list_init(&rrpc->requeue_bios);
	spin_lock_init(&rrpc->bio_lock);
	INIT_WORK(&rrpc->ws_requeue, rrpc_requeue);

	rrpc->nr_luns = geo->nr_luns;
	rrpc->nr_sects = (unsigned long long)geo->sec_per_lun * rrpc->nr_luns;

	/* simple round-robin strategy */
	atomic_set(&rrpc->next_lun, -1);

	ret = rrpc_area_init(rrpc, &soffset);
	if (ret < 0) {
		pr_err("nvm: rrpc: could not initialize area\n");
		return ERR_PTR(ret);
	}
	rrpc->soffset = soffset;

	ret = rrpc_luns_init(rrpc, dev->luns);
	if (ret) {
		pr_err("nvm: rrpc: could not initialize luns\n");
		goto err;
	}

	ret = rrpc_core_init(rrpc);
	if (ret) {
		pr_err("nvm: rrpc: could not initialize core\n");
		goto err;
	}

	ret = rrpc_map_init(rrpc);
	if (ret) {
		pr_err("nvm: rrpc: could not initialize maps\n");
		goto err;
	}

	ret = rrpc_blocks_init(rrpc);
	if (ret) {
		pr_err("nvm: rrpc: could not initialize state for blocks\n");
		goto err;
	}

	ret = rrpc_luns_configure(rrpc);
	if (ret) {
		pr_err("nvm: rrpc: not enough blocks available in LUNs.\n");
		goto err;
	}

	ret = rrpc_gc_init(rrpc);
	if (ret) {
		pr_err("nvm: rrpc: could not initialize gc\n");
		goto err;
	}

	/* inherit the size from the underlying device */
	blk_queue_logical_block_size(tqueue, queue_physical_block_size(bqueue));
	blk_queue_max_hw_sectors(tqueue, queue_max_hw_sectors(bqueue));

	pr_info("nvm: rrpc initialized with %u luns and %llu pages.\n",
			rrpc->nr_luns, (unsigned long long)rrpc->nr_sects);

	mod_timer(&rrpc->gc_timer, jiffies + msecs_to_jiffies(10));

	return rrpc;
err:
	rrpc_free(rrpc);
	return ERR_PTR(ret);
}

/* round robin, page-based FTL, and cost-based GC */
static struct nvm_tgt_type tt_rrpc = {
	.name		= "rrpc",
	.version	= {1, 0, 0},

	.make_rq	= rrpc_make_rq,
	.capacity	= rrpc_capacity,

	.init		= rrpc_init,
	.exit		= rrpc_exit,
};

static int __init rrpc_module_init(void)
{
	return nvm_register_tgt_type(&tt_rrpc);
}

static void rrpc_module_exit(void)
{
	nvm_unregister_tgt_type(&tt_rrpc);
}

module_init(rrpc_module_init);
module_exit(rrpc_module_exit);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Block-Device Target for Open-Channel SSDs");
