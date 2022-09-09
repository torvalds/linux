// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012 Linutronix GmbH
 * Copyright (c) 2014 sigma star gmbh
 * Author: Richard Weinberger <richard@nod.at>
 */

/**
 * update_fastmap_work_fn - calls ubi_update_fastmap from a work queue
 * @wrk: the work description object
 */
static void update_fastmap_work_fn(struct work_struct *wrk)
{
	struct ubi_device *ubi = container_of(wrk, struct ubi_device, fm_work);

	ubi_update_fastmap(ubi);
	spin_lock(&ubi->wl_lock);
	ubi->fm_work_scheduled = 0;
	spin_unlock(&ubi->wl_lock);
}

/**
 * find_anchor_wl_entry - find wear-leveling entry to used as anchor PEB.
 * @root: the RB-tree where to look for
 */
static struct ubi_wl_entry *find_anchor_wl_entry(struct rb_root *root)
{
	struct rb_node *p;
	struct ubi_wl_entry *e, *victim = NULL;
	int max_ec = UBI_MAX_ERASECOUNTER;

	ubi_rb_for_each_entry(p, e, root, u.rb) {
		if (e->pnum < UBI_FM_MAX_START && e->ec < max_ec) {
			victim = e;
			max_ec = e->ec;
		}
	}

	return victim;
}

static inline void return_unused_peb(struct ubi_device *ubi,
				     struct ubi_wl_entry *e)
{
	wl_tree_add(e, &ubi->free);
	ubi->free_count++;
}

/**
 * return_unused_pool_pebs - returns unused PEB to the free tree.
 * @ubi: UBI device description object
 * @pool: fastmap pool description object
 */
static void return_unused_pool_pebs(struct ubi_device *ubi,
				    struct ubi_fm_pool *pool)
{
	int i;
	struct ubi_wl_entry *e;

	for (i = pool->used; i < pool->size; i++) {
		e = ubi->lookuptbl[pool->pebs[i]];
		return_unused_peb(ubi, e);
	}
}

/**
 * ubi_wl_get_fm_peb - find a physical erase block with a given maximal number.
 * @ubi: UBI device description object
 * @anchor: This PEB will be used as anchor PEB by fastmap
 *
 * The function returns a physical erase block with a given maximal number
 * and removes it from the wl subsystem.
 * Must be called with wl_lock held!
 */
struct ubi_wl_entry *ubi_wl_get_fm_peb(struct ubi_device *ubi, int anchor)
{
	struct ubi_wl_entry *e = NULL;

	if (!ubi->free.rb_node || (ubi->free_count - ubi->beb_rsvd_pebs < 1))
		goto out;

	if (anchor)
		e = find_anchor_wl_entry(&ubi->free);
	else
		e = find_mean_wl_entry(ubi, &ubi->free);

	if (!e)
		goto out;

	self_check_in_wl_tree(ubi, e, &ubi->free);

	/* remove it from the free list,
	 * the wl subsystem does no longer know this erase block */
	rb_erase(&e->u.rb, &ubi->free);
	ubi->free_count--;
out:
	return e;
}

/*
 * has_enough_free_count - whether ubi has enough free pebs to fill fm pools
 * @ubi: UBI device description object
 * @is_wl_pool: whether UBI is filling wear leveling pool
 *
 * This helper function checks whether there are enough free pebs (deducted
 * by fastmap pebs) to fill fm_pool and fm_wl_pool, above rule works after
 * there is at least one of free pebs is filled into fm_wl_pool.
 * For wear leveling pool, UBI should also reserve free pebs for bad pebs
 * handling, because there maybe no enough free pebs for user volumes after
 * producing new bad pebs.
 */
static bool has_enough_free_count(struct ubi_device *ubi, bool is_wl_pool)
{
	int fm_used = 0;	// fastmap non anchor pebs.
	int beb_rsvd_pebs;

	if (!ubi->free.rb_node)
		return false;

	beb_rsvd_pebs = is_wl_pool ? ubi->beb_rsvd_pebs : 0;
	if (ubi->fm_wl_pool.size > 0 && !(ubi->ro_mode || ubi->fm_disabled))
		fm_used = ubi->fm_size / ubi->leb_size - 1;

	return ubi->free_count - beb_rsvd_pebs > fm_used;
}

/**
 * ubi_refill_pools - refills all fastmap PEB pools.
 * @ubi: UBI device description object
 */
void ubi_refill_pools(struct ubi_device *ubi)
{
	struct ubi_fm_pool *wl_pool = &ubi->fm_wl_pool;
	struct ubi_fm_pool *pool = &ubi->fm_pool;
	struct ubi_wl_entry *e;
	int enough;

	spin_lock(&ubi->wl_lock);

	return_unused_pool_pebs(ubi, wl_pool);
	return_unused_pool_pebs(ubi, pool);

	wl_pool->size = 0;
	pool->size = 0;

	if (ubi->fm_anchor) {
		wl_tree_add(ubi->fm_anchor, &ubi->free);
		ubi->free_count++;
	}

	/*
	 * All available PEBs are in ubi->free, now is the time to get
	 * the best anchor PEBs.
	 */
	ubi->fm_anchor = ubi_wl_get_fm_peb(ubi, 1);

	for (;;) {
		enough = 0;
		if (pool->size < pool->max_size) {
			if (!has_enough_free_count(ubi, false))
				break;

			e = wl_get_wle(ubi);
			if (!e)
				break;

			pool->pebs[pool->size] = e->pnum;
			pool->size++;
		} else
			enough++;

		if (wl_pool->size < wl_pool->max_size) {
			if (!has_enough_free_count(ubi, true))
				break;

			e = find_wl_entry(ubi, &ubi->free, WL_FREE_MAX_DIFF);
			self_check_in_wl_tree(ubi, e, &ubi->free);
			rb_erase(&e->u.rb, &ubi->free);
			ubi->free_count--;

			wl_pool->pebs[wl_pool->size] = e->pnum;
			wl_pool->size++;
		} else
			enough++;

		if (enough == 2)
			break;
	}

	wl_pool->used = 0;
	pool->used = 0;

	spin_unlock(&ubi->wl_lock);
}

/**
 * produce_free_peb - produce a free physical eraseblock.
 * @ubi: UBI device description object
 *
 * This function tries to make a free PEB by means of synchronous execution of
 * pending works. This may be needed if, for example the background thread is
 * disabled. Returns zero in case of success and a negative error code in case
 * of failure.
 */
static int produce_free_peb(struct ubi_device *ubi)
{
	int err;

	while (!ubi->free.rb_node && ubi->works_count) {
		dbg_wl("do one work synchronously");
		err = do_work(ubi);

		if (err)
			return err;
	}

	return 0;
}

/**
 * ubi_wl_get_peb - get a physical eraseblock.
 * @ubi: UBI device description object
 *
 * This function returns a physical eraseblock in case of success and a
 * negative error code in case of failure.
 * Returns with ubi->fm_eba_sem held in read mode!
 */
int ubi_wl_get_peb(struct ubi_device *ubi)
{
	int ret, attempts = 0;
	struct ubi_fm_pool *pool = &ubi->fm_pool;
	struct ubi_fm_pool *wl_pool = &ubi->fm_wl_pool;

again:
	down_read(&ubi->fm_eba_sem);
	spin_lock(&ubi->wl_lock);

	/* We check here also for the WL pool because at this point we can
	 * refill the WL pool synchronous. */
	if (pool->used == pool->size || wl_pool->used == wl_pool->size) {
		spin_unlock(&ubi->wl_lock);
		up_read(&ubi->fm_eba_sem);
		ret = ubi_update_fastmap(ubi);
		if (ret) {
			ubi_msg(ubi, "Unable to write a new fastmap: %i", ret);
			down_read(&ubi->fm_eba_sem);
			return -ENOSPC;
		}
		down_read(&ubi->fm_eba_sem);
		spin_lock(&ubi->wl_lock);
	}

	if (pool->used == pool->size) {
		spin_unlock(&ubi->wl_lock);
		attempts++;
		if (attempts == 10) {
			ubi_err(ubi, "Unable to get a free PEB from user WL pool");
			ret = -ENOSPC;
			goto out;
		}
		up_read(&ubi->fm_eba_sem);
		ret = produce_free_peb(ubi);
		if (ret < 0) {
			down_read(&ubi->fm_eba_sem);
			goto out;
		}
		goto again;
	}

	ubi_assert(pool->used < pool->size);
	ret = pool->pebs[pool->used++];
	prot_queue_add(ubi, ubi->lookuptbl[ret]);
	spin_unlock(&ubi->wl_lock);
out:
	return ret;
}

/**
 * next_peb_for_wl - returns next PEB to be used internally by the
 * WL sub-system.
 *
 * @ubi: UBI device description object
 */
static struct ubi_wl_entry *next_peb_for_wl(struct ubi_device *ubi)
{
	struct ubi_fm_pool *pool = &ubi->fm_wl_pool;
	int pnum;

	if (pool->used == pool->size)
		return NULL;

	pnum = pool->pebs[pool->used];
	return ubi->lookuptbl[pnum];
}

/**
 * need_wear_leveling - checks whether to trigger a wear leveling work.
 * UBI fetches free PEB from wl_pool, we check free PEBs from both 'wl_pool'
 * and 'ubi->free', because free PEB in 'ubi->free' tree maybe moved into
 * 'wl_pool' by ubi_refill_pools().
 *
 * @ubi: UBI device description object
 */
static bool need_wear_leveling(struct ubi_device *ubi)
{
	int ec;
	struct ubi_wl_entry *e;

	if (!ubi->used.rb_node)
		return false;

	e = next_peb_for_wl(ubi);
	if (!e) {
		if (!ubi->free.rb_node)
			return false;
		e = find_wl_entry(ubi, &ubi->free, WL_FREE_MAX_DIFF);
		ec = e->ec;
	} else {
		ec = e->ec;
		if (ubi->free.rb_node) {
			e = find_wl_entry(ubi, &ubi->free, WL_FREE_MAX_DIFF);
			ec = max(ec, e->ec);
		}
	}
	e = rb_entry(rb_first(&ubi->used), struct ubi_wl_entry, u.rb);

	return ec - e->ec >= UBI_WL_THRESHOLD;
}

/* get_peb_for_wl - returns a PEB to be used internally by the WL sub-system.
 *
 * @ubi: UBI device description object
 */
static struct ubi_wl_entry *get_peb_for_wl(struct ubi_device *ubi)
{
	struct ubi_fm_pool *pool = &ubi->fm_wl_pool;
	int pnum;

	ubi_assert(rwsem_is_locked(&ubi->fm_eba_sem));

	if (pool->used == pool->size) {
		/* We cannot update the fastmap here because this
		 * function is called in atomic context.
		 * Let's fail here and refill/update it as soon as possible. */
		if (!ubi->fm_work_scheduled) {
			ubi->fm_work_scheduled = 1;
			schedule_work(&ubi->fm_work);
		}
		return NULL;
	}

	pnum = pool->pebs[pool->used++];
	return ubi->lookuptbl[pnum];
}

/**
 * ubi_ensure_anchor_pebs - schedule wear-leveling to produce an anchor PEB.
 * @ubi: UBI device description object
 */
int ubi_ensure_anchor_pebs(struct ubi_device *ubi)
{
	struct ubi_work *wrk;
	struct ubi_wl_entry *anchor;

	spin_lock(&ubi->wl_lock);

	/* Do we already have an anchor? */
	if (ubi->fm_anchor) {
		spin_unlock(&ubi->wl_lock);
		return 0;
	}

	/* See if we can find an anchor PEB on the list of free PEBs */
	anchor = ubi_wl_get_fm_peb(ubi, 1);
	if (anchor) {
		ubi->fm_anchor = anchor;
		spin_unlock(&ubi->wl_lock);
		return 0;
	}

	ubi->fm_do_produce_anchor = 1;
	/* No luck, trigger wear leveling to produce a new anchor PEB. */
	if (ubi->wl_scheduled) {
		spin_unlock(&ubi->wl_lock);
		return 0;
	}
	ubi->wl_scheduled = 1;
	spin_unlock(&ubi->wl_lock);

	wrk = kmalloc(sizeof(struct ubi_work), GFP_NOFS);
	if (!wrk) {
		spin_lock(&ubi->wl_lock);
		ubi->wl_scheduled = 0;
		spin_unlock(&ubi->wl_lock);
		return -ENOMEM;
	}

	wrk->func = &wear_leveling_worker;
	__schedule_ubi_work(ubi, wrk);
	return 0;
}

/**
 * ubi_wl_put_fm_peb - returns a PEB used in a fastmap to the wear-leveling
 * sub-system.
 * see: ubi_wl_put_peb()
 *
 * @ubi: UBI device description object
 * @fm_e: physical eraseblock to return
 * @lnum: the last used logical eraseblock number for the PEB
 * @torture: if this physical eraseblock has to be tortured
 */
int ubi_wl_put_fm_peb(struct ubi_device *ubi, struct ubi_wl_entry *fm_e,
		      int lnum, int torture)
{
	struct ubi_wl_entry *e;
	int vol_id, pnum = fm_e->pnum;

	dbg_wl("PEB %d", pnum);

	ubi_assert(pnum >= 0);
	ubi_assert(pnum < ubi->peb_count);

	spin_lock(&ubi->wl_lock);
	e = ubi->lookuptbl[pnum];

	/* This can happen if we recovered from a fastmap the very
	 * first time and writing now a new one. In this case the wl system
	 * has never seen any PEB used by the original fastmap.
	 */
	if (!e) {
		e = fm_e;
		ubi_assert(e->ec >= 0);
		ubi->lookuptbl[pnum] = e;
	}

	spin_unlock(&ubi->wl_lock);

	vol_id = lnum ? UBI_FM_DATA_VOLUME_ID : UBI_FM_SB_VOLUME_ID;
	return schedule_erase(ubi, e, vol_id, lnum, torture, true);
}

/**
 * ubi_is_erase_work - checks whether a work is erase work.
 * @wrk: The work object to be checked
 */
int ubi_is_erase_work(struct ubi_work *wrk)
{
	return wrk->func == erase_worker;
}

static void ubi_fastmap_close(struct ubi_device *ubi)
{
	int i;

	return_unused_pool_pebs(ubi, &ubi->fm_pool);
	return_unused_pool_pebs(ubi, &ubi->fm_wl_pool);

	if (ubi->fm_anchor) {
		return_unused_peb(ubi, ubi->fm_anchor);
		ubi->fm_anchor = NULL;
	}

	if (ubi->fm) {
		for (i = 0; i < ubi->fm->used_blocks; i++)
			kfree(ubi->fm->e[i]);
	}
	kfree(ubi->fm);
}

/**
 * may_reserve_for_fm - tests whether a PEB shall be reserved for fastmap.
 * See find_mean_wl_entry()
 *
 * @ubi: UBI device description object
 * @e: physical eraseblock to return
 * @root: RB tree to test against.
 */
static struct ubi_wl_entry *may_reserve_for_fm(struct ubi_device *ubi,
					   struct ubi_wl_entry *e,
					   struct rb_root *root) {
	if (e && !ubi->fm_disabled && !ubi->fm &&
	    e->pnum < UBI_FM_MAX_START)
		e = rb_entry(rb_next(root->rb_node),
			     struct ubi_wl_entry, u.rb);

	return e;
}
