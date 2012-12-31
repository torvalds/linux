/*
 * BFQ: I/O context handling.
 *
 * Based on ideas and code from CFQ:
 * Copyright (C) 2003 Jens Axboe <axboe@kernel.dk>
 *
 * Copyright (C) 2008 Fabio Checconi <fabio@gandalf.sssup.it>
 *		      Paolo Valente <paolo.valente@unimore.it>
 */

/**
 * bfq_cic_free_rcu - deferred cic freeing.
 * @head: RCU head of the cic to free.
 *
 * Free the cic containing @head and, if it was the last one and
 * the module is exiting wake up anyone waiting for its deallocation
 * (see bfq_exit()).
 */
static void bfq_cic_free_rcu(struct rcu_head *head)
{
	struct cfq_io_context *cic;

	cic = container_of(head, struct cfq_io_context, rcu_head);

	kmem_cache_free(bfq_ioc_pool, cic);
	elv_ioc_count_dec(bfq_ioc_count);

	if (bfq_ioc_gone != NULL) {
		spin_lock(&bfq_ioc_gone_lock);
		if (bfq_ioc_gone != NULL &&
		    !elv_ioc_count_read(bfq_ioc_count)) {
			complete(bfq_ioc_gone);
			bfq_ioc_gone = NULL;
		}
		spin_unlock(&bfq_ioc_gone_lock);
	}
}

static void bfq_cic_free(struct cfq_io_context *cic)
{
	call_rcu(&cic->rcu_head, bfq_cic_free_rcu);
}

/**
 * cic_free_func - disconnect a cic ready to be freed.
 * @ioc: the io_context @cic belongs to.
 * @cic: the cic to be freed.
 *
 * Remove @cic from the @ioc radix tree hash and from its cic list,
 * deferring the deallocation of @cic to the end of the current RCU
 * grace period.  This assumes that __bfq_exit_single_io_context()
 * has already been called for @cic.
 */
static void cic_free_func(struct io_context *ioc, struct cfq_io_context *cic)
{
	unsigned long flags;
	unsigned long dead_key = (unsigned long) cic->key;

	BUG_ON(!(dead_key & CIC_DEAD_KEY));

	spin_lock_irqsave(&ioc->lock, flags);
	radix_tree_delete(&ioc->bfq_radix_root,
		dead_key >> CIC_DEAD_INDEX_SHIFT);
	hlist_del_init_rcu(&cic->cic_list);
	spin_unlock_irqrestore(&ioc->lock, flags);

	bfq_cic_free(cic);
}

static void bfq_free_io_context(struct io_context *ioc)
{
	/*
	 * ioc->refcount is zero here, or we are called from elv_unregister(),
	 * so no more cic's are allowed to be linked into this ioc.  So it
	 * should be ok to iterate over the known list, we will see all cic's
	 * since no new ones are added.
	 */
	call_for_each_cic(ioc, cic_free_func);
}

/**
 * __bfq_exit_single_io_context - deassociate @cic from any running task.
 * @bfqd: bfq_data on which @cic is valid.
 * @cic: the cic being exited.
 *
 * Whenever no more tasks are using @cic or @bfqd is deallocated we
 * need to invalidate its entry in the radix tree hash table and to
 * release the queues it refers to.
 *
 * Called under the queue lock.
 */
static void __bfq_exit_single_io_context(struct bfq_data *bfqd,
					 struct cfq_io_context *cic)
{
	struct io_context *ioc = cic->ioc;

	list_del_init(&cic->queue_list);

	/*
	 * Make sure dead mark is seen for dead queues
	 */
	smp_wmb();
	rcu_assign_pointer(cic->key, bfqd_dead_key(bfqd));

	/*
	 * No write-side locking as no task is using @ioc (they're exited
	 * or bfqd is being deallocated.
	 */
	rcu_read_lock();
	if (rcu_dereference(ioc->ioc_data) == cic) {
		rcu_read_unlock();
		spin_lock(&ioc->lock);
		rcu_assign_pointer(ioc->ioc_data, NULL);
		spin_unlock(&ioc->lock);
	} else
		rcu_read_unlock();

	if (cic->cfqq[BLK_RW_ASYNC] != NULL) {
		bfq_exit_bfqq(bfqd, cic->cfqq[BLK_RW_ASYNC]);
		cic->cfqq[BLK_RW_ASYNC] = NULL;
	}

	if (cic->cfqq[BLK_RW_SYNC] != NULL) {
		bfq_exit_bfqq(bfqd, cic->cfqq[BLK_RW_SYNC]);
		cic->cfqq[BLK_RW_SYNC] = NULL;
	}
}

/**
 * bfq_exit_single_io_context - deassociate @cic from @ioc (unlocked version).
 * @ioc: the io_context @cic belongs to.
 * @cic: the cic being exited.
 *
 * Take the queue lock and call __bfq_exit_single_io_context() to do the
 * rest of the work.  We take care of possible races with bfq_exit_queue()
 * using bfq_get_bfqd_locked() (and abusing a little bit the RCU mechanism).
 */
static void bfq_exit_single_io_context(struct io_context *ioc,
				       struct cfq_io_context *cic)
{
	struct bfq_data *bfqd;
	unsigned long uninitialized_var(flags);

	bfqd = bfq_get_bfqd_locked(&cic->key, &flags);
	if (bfqd != NULL) {
		__bfq_exit_single_io_context(bfqd, cic);
		bfq_put_bfqd_unlock(bfqd, &flags);
	}
}

/**
 * bfq_exit_io_context - deassociate @ioc from all cics it owns.
 * @ioc: the @ioc being exited.
 *
 * No more processes are using @ioc we need to clean up and put the
 * internal structures we have that belongs to that process.  Loop
 * through all its cics, locking their queues and exiting them.
 */
static void bfq_exit_io_context(struct io_context *ioc)
{
	call_for_each_cic(ioc, bfq_exit_single_io_context);
}

static struct cfq_io_context *bfq_alloc_io_context(struct bfq_data *bfqd,
						   gfp_t gfp_mask)
{
	struct cfq_io_context *cic;

	cic = kmem_cache_alloc_node(bfq_ioc_pool, gfp_mask | __GFP_ZERO,
							bfqd->queue->node);
	if (cic != NULL) {
		cic->last_end_request = jiffies;
		INIT_LIST_HEAD(&cic->queue_list);
		INIT_HLIST_NODE(&cic->cic_list);
		cic->dtor = bfq_free_io_context;
		cic->exit = bfq_exit_io_context;
		elv_ioc_count_inc(bfq_ioc_count);
	}

	return cic;
}

/**
 * bfq_drop_dead_cic - free an exited cic.
 * @bfqd: bfq data for the device in use.
 * @ioc: io_context owning @cic.
 * @cic: the @cic to free.
 *
 * We drop cfq io contexts lazily, so we may find a dead one.
 */
static void bfq_drop_dead_cic(struct bfq_data *bfqd, struct io_context *ioc,
			      struct cfq_io_context *cic)
{
	unsigned long flags;

	WARN_ON(!list_empty(&cic->queue_list));
	BUG_ON(cic->key != bfqd_dead_key(bfqd));

	spin_lock_irqsave(&ioc->lock, flags);

	BUG_ON(ioc->ioc_data == cic);

	/*
	 * With shared I/O contexts two lookups may race and drop the
	 * same cic more than one time: RCU guarantees that the storage
	 * will not be freed too early, here we make sure that we do
	 * not try to remove the cic from the hashing structures multiple
	 * times.
	 */
	if (!hlist_unhashed(&cic->cic_list)) {
		radix_tree_delete(&ioc->bfq_radix_root, bfqd->cic_index);
		hlist_del_init_rcu(&cic->cic_list);
		bfq_cic_free(cic);
	}

	spin_unlock_irqrestore(&ioc->lock, flags);
}

/**
 * bfq_cic_lookup - search into @ioc a cic associated to @bfqd.
 * @bfqd: the lookup key.
 * @ioc: the io_context of the process doing I/O.
 *
 * If @ioc already has a cic associated to @bfqd return it, return %NULL
 * otherwise.
 */
static struct cfq_io_context *bfq_cic_lookup(struct bfq_data *bfqd,
					     struct io_context *ioc)
{
	struct cfq_io_context *cic;
	unsigned long flags;
	void *k;

	if (unlikely(ioc == NULL))
		return NULL;

	rcu_read_lock();

	/* We maintain a last-hit cache, to avoid browsing over the tree. */
	cic = rcu_dereference(ioc->ioc_data);
	if (cic != NULL) {
		k = rcu_dereference(cic->key);
		if (k == bfqd)
			goto out;
	}

	do {
		cic = radix_tree_lookup(&ioc->bfq_radix_root,
					bfqd->cic_index);
		if (cic == NULL)
			goto out;

		k = rcu_dereference(cic->key);
		if (unlikely(k != bfqd)) {
			rcu_read_unlock();
			bfq_drop_dead_cic(bfqd, ioc, cic);
			rcu_read_lock();
			continue;
		}

		spin_lock_irqsave(&ioc->lock, flags);
		rcu_assign_pointer(ioc->ioc_data, cic);
		spin_unlock_irqrestore(&ioc->lock, flags);
		break;
	} while (1);

out:
	rcu_read_unlock();

	return cic;
}

/**
 * bfq_cic_link - add @cic to @ioc.
 * @bfqd: bfq_data @cic refers to.
 * @ioc: io_context @cic belongs to.
 * @cic: the cic to link.
 * @gfp_mask: the mask to use for radix tree preallocations.
 *
 * Add @cic to @ioc, using @bfqd as the search key.  This enables us to
 * lookup the process specific cfq io context when entered from the block
 * layer.  Also adds @cic to a per-bfqd list, used when this queue is
 * removed.
 */
static int bfq_cic_link(struct bfq_data *bfqd, struct io_context *ioc,
			struct cfq_io_context *cic, gfp_t gfp_mask)
{
	unsigned long flags;
	int ret;

	ret = radix_tree_preload(gfp_mask);
	if (ret == 0) {
		cic->ioc = ioc;

		/* No write-side locking, cic is not published yet. */
		rcu_assign_pointer(cic->key, bfqd);

		spin_lock_irqsave(&ioc->lock, flags);
		ret = radix_tree_insert(&ioc->bfq_radix_root,
					bfqd->cic_index, cic);
		if (ret == 0)
			hlist_add_head_rcu(&cic->cic_list, &ioc->bfq_cic_list);
		spin_unlock_irqrestore(&ioc->lock, flags);

		radix_tree_preload_end();

		if (ret == 0) {
			spin_lock_irqsave(bfqd->queue->queue_lock, flags);
			list_add(&cic->queue_list, &bfqd->cic_list);
			spin_unlock_irqrestore(bfqd->queue->queue_lock, flags);
		}
	}

	if (ret != 0)
		printk(KERN_ERR "bfq: cic link failed!\n");

	return ret;
}

/**
 * bfq_ioc_set_ioprio - signal a priority change to the cics belonging to @ioc.
 * @ioc: the io_context changing its priority.
 */
static inline void bfq_ioc_set_ioprio(struct io_context *ioc)
{
	call_for_each_cic(ioc, bfq_changed_ioprio);
}

/**
 * bfq_get_io_context - return the @cic associated to @bfqd in @ioc.
 * @bfqd: the search key.
 * @gfp_mask: the mask to use for cic allocation.
 *
 * Setup general io context and cfq io context.  There can be several cfq
 * io contexts per general io context, if this process is doing io to more
 * than one device managed by cfq.
 */
static struct cfq_io_context *bfq_get_io_context(struct bfq_data *bfqd,
						 gfp_t gfp_mask)
{
	struct io_context *ioc = NULL;
	struct cfq_io_context *cic;

	might_sleep_if(gfp_mask & __GFP_WAIT);

	ioc = get_io_context(gfp_mask, bfqd->queue->node);
	if (ioc == NULL)
		return NULL;

	/* Lookup for an existing cic. */
	cic = bfq_cic_lookup(bfqd, ioc);
	if (cic != NULL)
		goto out;

	/* Alloc one if needed. */
	cic = bfq_alloc_io_context(bfqd, gfp_mask);
	if (cic == NULL)
		goto err;

	/* Link it into the ioc's radix tree and cic list. */
	if (bfq_cic_link(bfqd, ioc, cic, gfp_mask) != 0)
		goto err_free;

out:
	/*
	 * test_and_clear_bit() implies a memory barrier, paired with
	 * the wmb() in fs/ioprio.c, so the value seen for ioprio is the
	 * new one.
	 */
	if (unlikely(test_and_clear_bit(IOC_BFQ_IOPRIO_CHANGED,
					ioc->ioprio_changed)))
		bfq_ioc_set_ioprio(ioc);

	return cic;
err_free:
	bfq_cic_free(cic);
err:
	put_io_context(ioc);
	return NULL;
}
