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
	struct rrpc_block *rblk = a->rblk;
	unsigned int pg_offset;

	lockdep_assert_held(&rrpc->rev_lock);

	if (a->addr == ADDR_EMPTY || !rblk)
		return;

	spin_lock(&rblk->lock);

	div_u64_rem(a->addr, rrpc->dev->pgs_per_blk, &pg_offset);
	WARN_ON(test_and_set_bit(pg_offset, rblk->invalid_pages));
	rblk->nr_invalid_pages++;

	spin_unlock(&rblk->lock);

	rrpc->rev_trans_map[a->addr - rrpc->poffset].addr = ADDR_EMPTY;
}

static void rrpc_invalidate_range(struct rrpc *rrpc, sector_t slba,
								unsigned len)
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

	do {
		rqd = rrpc_inflight_laddr_acquire(rrpc, slba, len);
		schedule();
	} while (!rqd);

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
	return (rblk->next_page == rrpc->dev->pgs_per_blk);
}

static u64 block_to_addr(struct rrpc *rrpc, struct rrpc_block *rblk)
{
	struct nvm_block *blk = rblk->parent;

	return blk->id * rrpc->dev->pgs_per_blk;
}

static struct ppa_addr linear_to_generic_addr(struct nvm_dev *dev,
							struct ppa_addr r)
{
	struct ppa_addr l;
	int secs, pgs, blks, luns;
	sector_t ppa = r.ppa;

	l.ppa = 0;

	div_u64_rem(ppa, dev->sec_per_pg, &secs);
	l.g.sec = secs;

	sector_div(ppa, dev->sec_per_pg);
	div_u64_rem(ppa, dev->sec_per_blk, &pgs);
	l.g.pg = pgs;

	sector_div(ppa, dev->pgs_per_blk);
	div_u64_rem(ppa, dev->blks_per_lun, &blks);
	l.g.blk = blks;

	sector_div(ppa, dev->blks_per_lun);
	div_u64_rem(ppa, dev->luns_per_chnl, &luns);
	l.g.lun = luns;

	sector_div(ppa, dev->luns_per_chnl);
	l.g.ch = ppa;

	return l;
}

static struct ppa_addr rrpc_ppa_to_gaddr(struct nvm_dev *dev, u64 addr)
{
	struct ppa_addr paddr;

	paddr.ppa = addr;
	return linear_to_generic_addr(dev, paddr);
}

/* requires lun->lock taken */
static void rrpc_set_lun_cur(struct rrpc_lun *rlun, struct rrpc_block *rblk)
{
	struct rrpc *rrpc = rlun->rrpc;

	BUG_ON(!rblk);

	if (rlun->cur) {
		spin_lock(&rlun->cur->lock);
		WARN_ON(!block_is_full(rrpc, rlun->cur));
		spin_unlock(&rlun->cur->lock);
	}
	rlun->cur = rblk;
}

static struct rrpc_block *rrpc_get_blk(struct rrpc *rrpc, struct rrpc_lun *rlun,
							unsigned long flags)
{
	struct nvm_block *blk;
	struct rrpc_block *rblk;

	blk = nvm_get_blk(rrpc->dev, rlun->parent, flags);
	if (!blk)
		return NULL;

	rblk = &rlun->blocks[blk->id];
	blk->priv = rblk;

	bitmap_zero(rblk->invalid_pages, rrpc->dev->pgs_per_blk);
	rblk->next_page = 0;
	rblk->nr_invalid_pages = 0;
	atomic_set(&rblk->data_cmnt_size, 0);

	return rblk;
}

static void rrpc_put_blk(struct rrpc *rrpc, struct rrpc_block *rblk)
{
	nvm_put_blk(rrpc->dev, rblk->parent);
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

	if (bio->bi_error)
		pr_err("nvm: gc request failed (%u).\n", bio->bi_error);

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
	struct request_queue *q = rrpc->dev->q;
	struct rrpc_rev_addr *rev;
	struct nvm_rq *rqd;
	struct bio *bio;
	struct page *page;
	int slot;
	int nr_pgs_per_blk = rrpc->dev->pgs_per_blk;
	u64 phys_addr;
	DECLARE_COMPLETION_ONSTACK(wait);

	if (bitmap_full(rblk->invalid_pages, nr_pgs_per_blk))
		return 0;

	bio = bio_alloc(GFP_NOIO, 1);
	if (!bio) {
		pr_err("nvm: could not alloc bio to gc\n");
		return -ENOMEM;
	}

	page = mempool_alloc(rrpc->page_pool, GFP_NOIO);
	if (!page)
		return -ENOMEM;

	while ((slot = find_first_zero_bit(rblk->invalid_pages,
					    nr_pgs_per_blk)) < nr_pgs_per_blk) {

		/* Lock laddr */
		phys_addr = (rblk->parent->id * nr_pgs_per_blk) + slot;

try:
		spin_lock(&rrpc->rev_lock);
		/* Get logical address from physical to logical table */
		rev = &rrpc->rev_trans_map[phys_addr - rrpc->poffset];
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
		bio->bi_rw = READ;
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

		bio_reset(bio);
		reinit_completion(&wait);

		bio->bi_iter.bi_sector = rrpc_get_sector(rev->addr);
		bio->bi_rw = WRITE;
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

		bio_reset(bio);
	}

finished:
	mempool_free(page, rrpc->page_pool);
	bio_put(bio);

	if (!bitmap_full(rblk->invalid_pages, nr_pgs_per_blk)) {
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
	struct nvm_dev *dev = rrpc->dev;

	pr_debug("nvm: block '%lu' being reclaimed\n", rblk->parent->id);

	if (rrpc_move_valid_pages(rrpc, rblk))
		goto done;

	nvm_erase_blk(dev, rblk->parent);
	rrpc_put_blk(rrpc, rblk);
done:
	mempool_free(gcb, rrpc->gcb_pool);
}

/* the block with highest number of invalid pages, will be in the beginning
 * of the list
 */
static struct rrpc_block *rblock_max_invalid(struct rrpc_block *ra,
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
	struct rrpc_block *rblock, *max;

	BUG_ON(list_empty(prio_list));

	max = list_first_entry(prio_list, struct rrpc_block, prio);
	list_for_each_entry(rblock, prio_list, prio)
		max = rblock_max_invalid(max, rblock);

	return max;
}

static void rrpc_lun_gc(struct work_struct *work)
{
	struct rrpc_lun *rlun = container_of(work, struct rrpc_lun, ws_gc);
	struct rrpc *rrpc = rlun->rrpc;
	struct nvm_lun *lun = rlun->parent;
	struct rrpc_block_gc *gcb;
	unsigned int nr_blocks_need;

	nr_blocks_need = rrpc->dev->blks_per_lun / GC_LIMIT_INVERSE;

	if (nr_blocks_need < rrpc->nr_luns)
		nr_blocks_need = rrpc->nr_luns;

	spin_lock(&lun->lock);
	while (nr_blocks_need > lun->nr_free_blocks &&
					!list_empty(&rlun->prio_list)) {
		struct rrpc_block *rblock = block_prio_find_max(rlun);
		struct nvm_block *block = rblock->parent;

		if (!rblock->nr_invalid_pages)
			break;

		list_del_init(&rblock->prio);

		BUG_ON(!block_is_full(rrpc, rblock));

		pr_debug("rrpc: selected block '%lu' for GC\n", block->id);

		gcb = mempool_alloc(rrpc->gcb_pool, GFP_ATOMIC);
		if (!gcb)
			break;

		gcb->rrpc = rrpc;
		gcb->rblk = rblock;
		INIT_WORK(&gcb->ws_gc, rrpc_block_gc);

		queue_work(rrpc->kgc_wq, &gcb->ws_gc);

		nr_blocks_need--;
	}
	spin_unlock(&lun->lock);

	/* TODO: Hint that request queue can be started again */
}

static void rrpc_gc_queue(struct work_struct *work)
{
	struct rrpc_block_gc *gcb = container_of(work, struct rrpc_block_gc,
									ws_gc);
	struct rrpc *rrpc = gcb->rrpc;
	struct rrpc_block *rblk = gcb->rblk;
	struct nvm_lun *lun = rblk->parent->lun;
	struct rrpc_lun *rlun = &rrpc->luns[lun->id - rrpc->lun_offset];

	spin_lock(&rlun->lock);
	list_add_tail(&rblk->prio, &rlun->prio_list);
	spin_unlock(&rlun->lock);

	mempool_free(gcb, rrpc->gcb_pool);
	pr_debug("nvm: block '%lu' is full, allow GC (sched)\n",
							rblk->parent->id);
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
		if (rlun->parent->nr_free_blocks >
					max_free->parent->nr_free_blocks)
			max_free = rlun;
	}

	return max_free;
}

static struct rrpc_addr *rrpc_update_map(struct rrpc *rrpc, sector_t laddr,
					struct rrpc_block *rblk, u64 paddr)
{
	struct rrpc_addr *gp;
	struct rrpc_rev_addr *rev;

	BUG_ON(laddr >= rrpc->nr_pages);

	gp = &rrpc->trans_map[laddr];
	spin_lock(&rrpc->rev_lock);
	if (gp->rblk)
		rrpc_page_invalidate(rrpc, gp);

	gp->addr = paddr;
	gp->rblk = rblk;

	rev = &rrpc->rev_trans_map[gp->addr - rrpc->poffset];
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

	addr = block_to_addr(rrpc, rblk) + rblk->next_page;

	rblk->next_page++;
out:
	spin_unlock(&rblk->lock);
	return addr;
}

/* Simple round-robin Logical to physical address translation.
 *
 * Retrieve the mapping using the active append point. Then update the ap for
 * the next write to the disk.
 *
 * Returns rrpc_addr with the physical address and block. Remember to return to
 * rrpc->addr_cache when request is finished.
 */
static struct rrpc_addr *rrpc_map_page(struct rrpc *rrpc, sector_t laddr,
								int is_gc)
{
	struct rrpc_lun *rlun;
	struct rrpc_block *rblk;
	struct nvm_lun *lun;
	u64 paddr;

	rlun = rrpc_get_lun_rr(rrpc, is_gc);
	lun = rlun->parent;

	if (!is_gc && lun->nr_free_blocks < rrpc->nr_luns * 4)
		return NULL;

	spin_lock(&rlun->lock);

	rblk = rlun->cur;
retry:
	paddr = rrpc_alloc_addr(rrpc, rblk);

	if (paddr == ADDR_EMPTY) {
		rblk = rrpc_get_blk(rrpc, rlun, 0);
		if (rblk) {
			rrpc_set_lun_cur(rlun, rblk);
			goto retry;
		}

		if (is_gc) {
			/* retry from emergency gc block */
			paddr = rrpc_alloc_addr(rrpc, rlun->gc_cur);
			if (paddr == ADDR_EMPTY) {
				rblk = rrpc_get_blk(rrpc, rlun, 1);
				if (!rblk) {
					pr_err("rrpc: no more blocks");
					goto err;
				}

				rlun->gc_cur = rblk;
				paddr = rrpc_alloc_addr(rrpc, rlun->gc_cur);
			}
			rblk = rlun->gc_cur;
		}
	}

	spin_unlock(&rlun->lock);
	return rrpc_update_map(rrpc, laddr, rblk, paddr);
err:
	spin_unlock(&rlun->lock);
	return NULL;
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

static void rrpc_end_io_write(struct rrpc *rrpc, struct rrpc_rq *rrqd,
						sector_t laddr, uint8_t npages)
{
	struct rrpc_addr *p;
	struct rrpc_block *rblk;
	struct nvm_lun *lun;
	int cmnt_size, i;

	for (i = 0; i < npages; i++) {
		p = &rrpc->trans_map[laddr + i];
		rblk = p->rblk;
		lun = rblk->parent->lun;

		cmnt_size = atomic_inc_return(&rblk->data_cmnt_size);
		if (unlikely(cmnt_size == rrpc->dev->pgs_per_blk))
			rrpc_run_gc(rrpc, rblk);
	}
}

static int rrpc_end_io(struct nvm_rq *rqd, int error)
{
	struct rrpc *rrpc = container_of(rqd->ins, struct rrpc, instance);
	struct rrpc_rq *rrqd = nvm_rq_to_pdu(rqd);
	uint8_t npages = rqd->nr_pages;
	sector_t laddr = rrpc_get_laddr(rqd->bio) - npages;

	if (bio_data_dir(rqd->bio) == WRITE)
		rrpc_end_io_write(rrpc, rrqd, laddr, npages);

	bio_put(rqd->bio);

	if (rrqd->flags & NVM_IOTYPE_GC)
		return 0;

	rrpc_unlock_rq(rrpc, rqd);

	if (npages > 1)
		nvm_dev_dma_free(rrpc->dev, rqd->ppa_list, rqd->dma_ppa_list);
	if (rqd->metadata)
		nvm_dev_dma_free(rrpc->dev, rqd->metadata, rqd->dma_metadata);

	mempool_free(rqd, rrpc->rq_pool);

	return 0;
}

static int rrpc_read_ppalist_rq(struct rrpc *rrpc, struct bio *bio,
			struct nvm_rq *rqd, unsigned long flags, int npages)
{
	struct rrpc_inflight_rq *r = rrpc_get_inflight_rq(rqd);
	struct rrpc_addr *gp;
	sector_t laddr = rrpc_get_laddr(bio);
	int is_gc = flags & NVM_IOTYPE_GC;
	int i;

	if (!is_gc && rrpc_lock_rq(rrpc, bio, rqd)) {
		nvm_dev_dma_free(rrpc->dev, rqd->ppa_list, rqd->dma_ppa_list);
		return NVM_IO_REQUEUE;
	}

	for (i = 0; i < npages; i++) {
		/* We assume that mapping occurs at 4KB granularity */
		BUG_ON(!(laddr + i >= 0 && laddr + i < rrpc->nr_pages));
		gp = &rrpc->trans_map[laddr + i];

		if (gp->rblk) {
			rqd->ppa_list[i] = rrpc_ppa_to_gaddr(rrpc->dev,
								gp->addr);
		} else {
			BUG_ON(is_gc);
			rrpc_unlock_laddr(rrpc, r);
			nvm_dev_dma_free(rrpc->dev, rqd->ppa_list,
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
	struct rrpc_rq *rrqd = nvm_rq_to_pdu(rqd);
	int is_gc = flags & NVM_IOTYPE_GC;
	sector_t laddr = rrpc_get_laddr(bio);
	struct rrpc_addr *gp;

	if (!is_gc && rrpc_lock_rq(rrpc, bio, rqd))
		return NVM_IO_REQUEUE;

	BUG_ON(!(laddr >= 0 && laddr < rrpc->nr_pages));
	gp = &rrpc->trans_map[laddr];

	if (gp->rblk) {
		rqd->ppa_addr = rrpc_ppa_to_gaddr(rrpc->dev, gp->addr);
	} else {
		BUG_ON(is_gc);
		rrpc_unlock_rq(rrpc, rqd);
		return NVM_IO_DONE;
	}

	rqd->opcode = NVM_OP_HBREAD;
	rrqd->addr = gp;

	return NVM_IO_OK;
}

static int rrpc_write_ppalist_rq(struct rrpc *rrpc, struct bio *bio,
			struct nvm_rq *rqd, unsigned long flags, int npages)
{
	struct rrpc_inflight_rq *r = rrpc_get_inflight_rq(rqd);
	struct rrpc_addr *p;
	sector_t laddr = rrpc_get_laddr(bio);
	int is_gc = flags & NVM_IOTYPE_GC;
	int i;

	if (!is_gc && rrpc_lock_rq(rrpc, bio, rqd)) {
		nvm_dev_dma_free(rrpc->dev, rqd->ppa_list, rqd->dma_ppa_list);
		return NVM_IO_REQUEUE;
	}

	for (i = 0; i < npages; i++) {
		/* We assume that mapping occurs at 4KB granularity */
		p = rrpc_map_page(rrpc, laddr + i, is_gc);
		if (!p) {
			BUG_ON(is_gc);
			rrpc_unlock_laddr(rrpc, r);
			nvm_dev_dma_free(rrpc->dev, rqd->ppa_list,
							rqd->dma_ppa_list);
			rrpc_gc_kick(rrpc);
			return NVM_IO_REQUEUE;
		}

		rqd->ppa_list[i] = rrpc_ppa_to_gaddr(rrpc->dev,
								p->addr);
	}

	rqd->opcode = NVM_OP_HBWRITE;

	return NVM_IO_OK;
}

static int rrpc_write_rq(struct rrpc *rrpc, struct bio *bio,
				struct nvm_rq *rqd, unsigned long flags)
{
	struct rrpc_rq *rrqd = nvm_rq_to_pdu(rqd);
	struct rrpc_addr *p;
	int is_gc = flags & NVM_IOTYPE_GC;
	sector_t laddr = rrpc_get_laddr(bio);

	if (!is_gc && rrpc_lock_rq(rrpc, bio, rqd))
		return NVM_IO_REQUEUE;

	p = rrpc_map_page(rrpc, laddr, is_gc);
	if (!p) {
		BUG_ON(is_gc);
		rrpc_unlock_rq(rrpc, rqd);
		rrpc_gc_kick(rrpc);
		return NVM_IO_REQUEUE;
	}

	rqd->ppa_addr = rrpc_ppa_to_gaddr(rrpc->dev, p->addr);
	rqd->opcode = NVM_OP_HBWRITE;
	rrqd->addr = p;

	return NVM_IO_OK;
}

static int rrpc_setup_rq(struct rrpc *rrpc, struct bio *bio,
			struct nvm_rq *rqd, unsigned long flags, uint8_t npages)
{
	if (npages > 1) {
		rqd->ppa_list = nvm_dev_dma_alloc(rrpc->dev, GFP_KERNEL,
							&rqd->dma_ppa_list);
		if (!rqd->ppa_list) {
			pr_err("rrpc: not able to allocate ppa list\n");
			return NVM_IO_ERR;
		}

		if (bio_rw(bio) == WRITE)
			return rrpc_write_ppalist_rq(rrpc, bio, rqd, flags,
									npages);

		return rrpc_read_ppalist_rq(rrpc, bio, rqd, flags, npages);
	}

	if (bio_rw(bio) == WRITE)
		return rrpc_write_rq(rrpc, bio, rqd, flags);

	return rrpc_read_rq(rrpc, bio, rqd, flags);
}

static int rrpc_submit_io(struct rrpc *rrpc, struct bio *bio,
				struct nvm_rq *rqd, unsigned long flags)
{
	int err;
	struct rrpc_rq *rrq = nvm_rq_to_pdu(rqd);
	uint8_t nr_pages = rrpc_get_pages(bio);
	int bio_size = bio_sectors(bio) << 9;

	if (bio_size < rrpc->dev->sec_size)
		return NVM_IO_ERR;
	else if (bio_size > rrpc->dev->max_rq_size)
		return NVM_IO_ERR;

	err = rrpc_setup_rq(rrpc, bio, rqd, flags, nr_pages);
	if (err)
		return err;

	bio_get(bio);
	rqd->bio = bio;
	rqd->ins = &rrpc->instance;
	rqd->nr_pages = nr_pages;
	rrq->flags = flags;

	err = nvm_submit_io(rrpc->dev, rqd);
	if (err) {
		pr_err("rrpc: I/O submission failed: %d\n", err);
		bio_put(bio);
		if (!(flags & NVM_IOTYPE_GC)) {
			rrpc_unlock_rq(rrpc, rqd);
			if (rqd->nr_pages > 1)
				nvm_dev_dma_free(rrpc->dev,
			rqd->ppa_list, rqd->dma_ppa_list);
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

	if (bio->bi_rw & REQ_DISCARD) {
		rrpc_discard(rrpc, bio);
		return BLK_QC_T_NONE;
	}

	rqd = mempool_alloc(rrpc->rq_pool, GFP_KERNEL);
	if (!rqd) {
		pr_err_ratelimited("rrpc: not able to queue bio.");
		bio_io_error(bio);
		return BLK_QC_T_NONE;
	}
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
	struct rrpc_lun *rlun;
	int i;

	if (rrpc->krqd_wq)
		destroy_workqueue(rrpc->krqd_wq);

	if (rrpc->kgc_wq)
		destroy_workqueue(rrpc->kgc_wq);

	if (!rrpc->luns)
		return;

	for (i = 0; i < rrpc->nr_luns; i++) {
		rlun = &rrpc->luns[i];

		if (!rlun->blocks)
			break;
		vfree(rlun->blocks);
	}
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
	struct nvm_dev *dev = rrpc->dev;
	struct rrpc_addr *addr = rrpc->trans_map + slba;
	struct rrpc_rev_addr *raddr = rrpc->rev_trans_map;
	sector_t max_pages = dev->total_pages * (dev->sec_size >> 9);
	u64 elba = slba + nlb;
	u64 i;

	if (unlikely(elba > dev->total_pages)) {
		pr_err("nvm: L2P data from device is out of bounds!\n");
		return -EINVAL;
	}

	for (i = 0; i < nlb; i++) {
		u64 pba = le64_to_cpu(entries[i]);
		/* LNVM treats address-spaces as silos, LBA and PBA are
		 * equally large and zero-indexed.
		 */
		if (unlikely(pba >= max_pages && pba != U64_MAX)) {
			pr_err("nvm: L2P data entry is out of bounds!\n");
			return -EINVAL;
		}

		/* Address zero is a special one. The first page on a disk is
		 * protected. As it often holds internal device boot
		 * information.
		 */
		if (!pba)
			continue;

		addr[i].addr = pba;
		raddr[pba].addr = slba + i;
	}

	return 0;
}

static int rrpc_map_init(struct rrpc *rrpc)
{
	struct nvm_dev *dev = rrpc->dev;
	sector_t i;
	int ret;

	rrpc->trans_map = vzalloc(sizeof(struct rrpc_addr) * rrpc->nr_pages);
	if (!rrpc->trans_map)
		return -ENOMEM;

	rrpc->rev_trans_map = vmalloc(sizeof(struct rrpc_rev_addr)
							* rrpc->nr_pages);
	if (!rrpc->rev_trans_map)
		return -ENOMEM;

	for (i = 0; i < rrpc->nr_pages; i++) {
		struct rrpc_addr *p = &rrpc->trans_map[i];
		struct rrpc_rev_addr *r = &rrpc->rev_trans_map[i];

		p->addr = ADDR_EMPTY;
		r->addr = ADDR_EMPTY;
	}

	if (!dev->ops->get_l2p_tbl)
		return 0;

	/* Bring up the mapping table from device */
	ret = dev->ops->get_l2p_tbl(dev, 0, dev->total_pages,
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

	rrpc->gcb_pool = mempool_create_slab_pool(rrpc->dev->nr_luns,
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
	kfree(rrpc->luns);
}

static int rrpc_luns_init(struct rrpc *rrpc, int lun_begin, int lun_end)
{
	struct nvm_dev *dev = rrpc->dev;
	struct rrpc_lun *rlun;
	int i, j;

	spin_lock_init(&rrpc->rev_lock);

	rrpc->luns = kcalloc(rrpc->nr_luns, sizeof(struct rrpc_lun),
								GFP_KERNEL);
	if (!rrpc->luns)
		return -ENOMEM;

	/* 1:1 mapping */
	for (i = 0; i < rrpc->nr_luns; i++) {
		struct nvm_lun *lun = dev->mt->get_lun(dev, lun_begin + i);

		if (dev->pgs_per_blk >
				MAX_INVALID_PAGES_STORAGE * BITS_PER_LONG) {
			pr_err("rrpc: number of pages per block too high.");
			goto err;
		}

		rlun = &rrpc->luns[i];
		rlun->rrpc = rrpc;
		rlun->parent = lun;
		INIT_LIST_HEAD(&rlun->prio_list);
		INIT_WORK(&rlun->ws_gc, rrpc_lun_gc);
		spin_lock_init(&rlun->lock);

		rrpc->total_blocks += dev->blks_per_lun;
		rrpc->nr_pages += dev->sec_per_lun;

		rlun->blocks = vzalloc(sizeof(struct rrpc_block) *
						rrpc->dev->blks_per_lun);
		if (!rlun->blocks)
			goto err;

		for (j = 0; j < rrpc->dev->blks_per_lun; j++) {
			struct rrpc_block *rblk = &rlun->blocks[j];
			struct nvm_block *blk = &lun->blocks[j];

			rblk->parent = blk;
			INIT_LIST_HEAD(&rblk->prio);
			spin_lock_init(&rblk->lock);
		}
	}

	return 0;
err:
	return -ENOMEM;
}

static void rrpc_free(struct rrpc *rrpc)
{
	rrpc_gc_free(rrpc);
	rrpc_map_free(rrpc);
	rrpc_core_free(rrpc);
	rrpc_luns_free(rrpc);

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
	struct nvm_dev *dev = rrpc->dev;
	sector_t reserved, provisioned;

	/* cur, gc, and two emergency blocks for each lun */
	reserved = rrpc->nr_luns * dev->max_pages_per_blk * 4;
	provisioned = rrpc->nr_pages - reserved;

	if (reserved > rrpc->nr_pages) {
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
	struct nvm_dev *dev = rrpc->dev;
	int offset;
	struct rrpc_addr *laddr;
	u64 paddr, pladdr;

	for (offset = 0; offset < dev->pgs_per_blk; offset++) {
		paddr = block_to_addr(rrpc, rblk) + offset;

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
	struct rrpc_lun *rlun;
	struct rrpc_block *rblk;
	int lun_iter, blk_iter;

	for (lun_iter = 0; lun_iter < rrpc->nr_luns; lun_iter++) {
		rlun = &rrpc->luns[lun_iter];

		for (blk_iter = 0; blk_iter < rrpc->dev->blks_per_lun;
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

		rrpc_set_lun_cur(rlun, rblk);

		/* Emergency gc block */
		rblk = rrpc_get_blk(rrpc, rlun, 1);
		if (!rblk)
			goto err;
		rlun->gc_cur = rblk;
	}

	return 0;
err:
	rrpc_put_blks(rrpc);
	return -EINVAL;
}

static struct nvm_tgt_type tt_rrpc;

static void *rrpc_init(struct nvm_dev *dev, struct gendisk *tdisk,
						int lun_begin, int lun_end)
{
	struct request_queue *bqueue = dev->q;
	struct request_queue *tqueue = tdisk->queue;
	struct rrpc *rrpc;
	int ret;

	if (!(dev->identity.dom & NVM_RSP_L2P)) {
		pr_err("nvm: rrpc: device does not support l2p (%x)\n",
							dev->identity.dom);
		return ERR_PTR(-EINVAL);
	}

	rrpc = kzalloc(sizeof(struct rrpc), GFP_KERNEL);
	if (!rrpc)
		return ERR_PTR(-ENOMEM);

	rrpc->instance.tt = &tt_rrpc;
	rrpc->dev = dev;
	rrpc->disk = tdisk;

	bio_list_init(&rrpc->requeue_bios);
	spin_lock_init(&rrpc->bio_lock);
	INIT_WORK(&rrpc->ws_requeue, rrpc_requeue);

	rrpc->nr_luns = lun_end - lun_begin + 1;

	/* simple round-robin strategy */
	atomic_set(&rrpc->next_lun, -1);

	ret = rrpc_luns_init(rrpc, lun_begin, lun_end);
	if (ret) {
		pr_err("nvm: rrpc: could not initialize luns\n");
		goto err;
	}

	rrpc->poffset = dev->sec_per_lun * lun_begin;
	rrpc->lun_offset = lun_begin;

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
			rrpc->nr_luns, (unsigned long long)rrpc->nr_pages);

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
	.end_io		= rrpc_end_io,

	.init		= rrpc_init,
	.exit		= rrpc_exit,
};

static int __init rrpc_module_init(void)
{
	return nvm_register_target(&tt_rrpc);
}

static void rrpc_module_exit(void)
{
	nvm_unregister_target(&tt_rrpc);
}

module_init(rrpc_module_init);
module_exit(rrpc_module_exit);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Block-Device Target for Open-Channel SSDs");
