/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.gnu.org/licenses/gpl-2.0.html
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * Implementation of cl_page for OSC layer.
 *
 *   Author: Nikita Danilov <nikita.danilov@sun.com>
 *   Author: Jinshan Xiong <jinshan.xiong@intel.com>
 */

#define DEBUG_SUBSYSTEM S_OSC

#include <linux/math64.h>
#include "osc_cl_internal.h"

static void osc_lru_del(struct client_obd *cli, struct osc_page *opg);
static void osc_lru_use(struct client_obd *cli, struct osc_page *opg);
static int osc_lru_reserve(const struct lu_env *env, struct osc_object *obj,
			   struct osc_page *opg);

/** \addtogroup osc
 *  @{
 */

/*****************************************************************************
 *
 * Page operations.
 *
 */
static void osc_page_transfer_get(struct osc_page *opg, const char *label)
{
	struct cl_page *page = opg->ops_cl.cpl_page;

	LASSERT(!opg->ops_transfer_pinned);
	cl_page_get(page);
	lu_ref_add_atomic(&page->cp_reference, label, page);
	opg->ops_transfer_pinned = 1;
}

static void osc_page_transfer_put(const struct lu_env *env,
				  struct osc_page *opg)
{
	struct cl_page *page = opg->ops_cl.cpl_page;

	if (opg->ops_transfer_pinned) {
		opg->ops_transfer_pinned = 0;
		lu_ref_del(&page->cp_reference, "transfer", page);
		cl_page_put(env, page);
	}
}

/**
 * This is called once for every page when it is submitted for a transfer
 * either opportunistic (osc_page_cache_add()), or immediate
 * (osc_page_submit()).
 */
static void osc_page_transfer_add(const struct lu_env *env,
				  struct osc_page *opg, enum cl_req_type crt)
{
	struct osc_object *obj = cl2osc(opg->ops_cl.cpl_obj);

	osc_lru_use(osc_cli(obj), opg);
}

int osc_page_cache_add(const struct lu_env *env,
		       const struct cl_page_slice *slice, struct cl_io *io)
{
	struct osc_page *opg = cl2osc_page(slice);
	int result;

	osc_page_transfer_get(opg, "transfer\0cache");
	result = osc_queue_async_io(env, io, opg);
	if (result != 0)
		osc_page_transfer_put(env, opg);
	else
		osc_page_transfer_add(env, opg, CRT_WRITE);

	return result;
}

void osc_index2policy(union ldlm_policy_data *policy,
		      const struct cl_object *obj,
		      pgoff_t start, pgoff_t end)
{
	memset(policy, 0, sizeof(*policy));
	policy->l_extent.start = cl_offset(obj, start);
	policy->l_extent.end = cl_offset(obj, end + 1) - 1;
}

static const char *osc_list(struct list_head *head)
{
	return list_empty(head) ? "-" : "+";
}

static inline unsigned long osc_submit_duration(struct osc_page *opg)
{
	if (opg->ops_submit_time == 0)
		return 0;

	return (cfs_time_current() - opg->ops_submit_time);
}

static int osc_page_print(const struct lu_env *env,
			  const struct cl_page_slice *slice,
			  void *cookie, lu_printer_t printer)
{
	struct osc_page *opg = cl2osc_page(slice);
	struct osc_async_page *oap = &opg->ops_oap;
	struct osc_object *obj = cl2osc(slice->cpl_obj);
	struct client_obd *cli = &osc_export(obj)->exp_obd->u.cli;

	return (*printer)(env, cookie, LUSTRE_OSC_NAME "-page@%p %lu: 1< %#x %d %u %s %s > 2< %llu %u %u %#x %#x | %p %p %p > 3< %d %lu %d > 4< %d %d %d %lu %s | %s %s %s %s > 5< %s %s %s %s | %d %s | %d %s %s>\n",
			  opg, osc_index(opg),
			  /* 1 */
			  oap->oap_magic, oap->oap_cmd,
			  oap->oap_interrupted,
			  osc_list(&oap->oap_pending_item),
			  osc_list(&oap->oap_rpc_item),
			  /* 2 */
			  oap->oap_obj_off, oap->oap_page_off, oap->oap_count,
			  oap->oap_async_flags, oap->oap_brw_flags,
			  oap->oap_request, oap->oap_cli, obj,
			  /* 3 */
			  opg->ops_transfer_pinned,
			  osc_submit_duration(opg), opg->ops_srvlock,
			  /* 4 */
			  cli->cl_r_in_flight, cli->cl_w_in_flight,
			  cli->cl_max_rpcs_in_flight,
			  cli->cl_avail_grant,
			  osc_list(&cli->cl_cache_waiters),
			  osc_list(&cli->cl_loi_ready_list),
			  osc_list(&cli->cl_loi_hp_ready_list),
			  osc_list(&cli->cl_loi_write_list),
			  osc_list(&cli->cl_loi_read_list),
			  /* 5 */
			  osc_list(&obj->oo_ready_item),
			  osc_list(&obj->oo_hp_ready_item),
			  osc_list(&obj->oo_write_item),
			  osc_list(&obj->oo_read_item),
			  atomic_read(&obj->oo_nr_reads),
			  osc_list(&obj->oo_reading_exts),
			  atomic_read(&obj->oo_nr_writes),
			  osc_list(&obj->oo_hp_exts),
			  osc_list(&obj->oo_urgent_exts));
}

static void osc_page_delete(const struct lu_env *env,
			    const struct cl_page_slice *slice)
{
	struct osc_page *opg = cl2osc_page(slice);
	struct osc_object *obj = cl2osc(opg->ops_cl.cpl_obj);
	int rc;

	CDEBUG(D_TRACE, "%p\n", opg);
	osc_page_transfer_put(env, opg);
	rc = osc_teardown_async_page(env, obj, opg);
	if (rc) {
		CL_PAGE_DEBUG(D_ERROR, env, slice->cpl_page,
			      "Trying to teardown failed: %d\n", rc);
		LASSERT(0);
	}

	osc_lru_del(osc_cli(obj), opg);

	if (slice->cpl_page->cp_type == CPT_CACHEABLE) {
		void *value;

		spin_lock(&obj->oo_tree_lock);
		value = radix_tree_delete(&obj->oo_tree, osc_index(opg));
		if (value)
			--obj->oo_npages;
		spin_unlock(&obj->oo_tree_lock);

		LASSERT(ergo(value, value == opg));
	}
}

static void osc_page_clip(const struct lu_env *env,
			  const struct cl_page_slice *slice, int from, int to)
{
	struct osc_page *opg = cl2osc_page(slice);
	struct osc_async_page *oap = &opg->ops_oap;

	opg->ops_from = from;
	opg->ops_to = to;
	spin_lock(&oap->oap_lock);
	oap->oap_async_flags |= ASYNC_COUNT_STABLE;
	spin_unlock(&oap->oap_lock);
}

static int osc_page_cancel(const struct lu_env *env,
			   const struct cl_page_slice *slice)
{
	struct osc_page *opg = cl2osc_page(slice);
	int rc = 0;

	/* Check if the transferring against this page
	 * is completed, or not even queued.
	 */
	if (opg->ops_transfer_pinned)
		/* FIXME: may not be interrupted.. */
		rc = osc_cancel_async_page(env, opg);
	LASSERT(ergo(rc == 0, opg->ops_transfer_pinned == 0));
	return rc;
}

static int osc_page_flush(const struct lu_env *env,
			  const struct cl_page_slice *slice,
			  struct cl_io *io)
{
	struct osc_page *opg = cl2osc_page(slice);
	int rc;

	rc = osc_flush_async_page(env, io, opg);
	return rc;
}

static const struct cl_page_operations osc_page_ops = {
	.cpo_print	 = osc_page_print,
	.cpo_delete	= osc_page_delete,
	.cpo_clip	   = osc_page_clip,
	.cpo_cancel	 = osc_page_cancel,
	.cpo_flush	  = osc_page_flush
};

int osc_page_init(const struct lu_env *env, struct cl_object *obj,
		  struct cl_page *page, pgoff_t index)
{
	struct osc_object *osc = cl2osc(obj);
	struct osc_page *opg = cl_object_page_slice(obj, page);
	int result;

	opg->ops_from = 0;
	opg->ops_to = PAGE_SIZE;

	result = osc_prep_async_page(osc, opg, page->cp_vmpage,
				     cl_offset(obj, index));
	if (result == 0) {
		struct osc_io *oio = osc_env_io(env);

		opg->ops_srvlock = osc_io_srvlock(oio);
		cl_page_slice_add(page, &opg->ops_cl, obj, index,
				  &osc_page_ops);
	}
	INIT_LIST_HEAD(&opg->ops_lru);

	/* reserve an LRU space for this page */
	if (page->cp_type == CPT_CACHEABLE && result == 0) {
		result = osc_lru_reserve(env, osc, opg);
		if (result == 0) {
			spin_lock(&osc->oo_tree_lock);
			result = radix_tree_insert(&osc->oo_tree, index, opg);
			if (result == 0)
				++osc->oo_npages;
			spin_unlock(&osc->oo_tree_lock);
			LASSERT(result == 0);
		}
	}

	return result;
}

/**
 * Helper function called by osc_io_submit() for every page in an immediate
 * transfer (i.e., transferred synchronously).
 */
void osc_page_submit(const struct lu_env *env, struct osc_page *opg,
		     enum cl_req_type crt, int brw_flags)
{
	struct osc_async_page *oap = &opg->ops_oap;

	LASSERTF(oap->oap_magic == OAP_MAGIC, "Bad oap magic: oap %p, magic 0x%x\n",
		 oap, oap->oap_magic);
	LASSERT(oap->oap_async_flags & ASYNC_READY);
	LASSERT(oap->oap_async_flags & ASYNC_COUNT_STABLE);

	oap->oap_cmd = crt == CRT_WRITE ? OBD_BRW_WRITE : OBD_BRW_READ;
	oap->oap_page_off = opg->ops_from;
	oap->oap_count = opg->ops_to - opg->ops_from;
	oap->oap_brw_flags = brw_flags | OBD_BRW_SYNC;

	if (capable(CFS_CAP_SYS_RESOURCE)) {
		oap->oap_brw_flags |= OBD_BRW_NOQUOTA;
		oap->oap_cmd |= OBD_BRW_NOQUOTA;
	}

	opg->ops_submit_time = cfs_time_current();
	osc_page_transfer_get(opg, "transfer\0imm");
	osc_page_transfer_add(env, opg, crt);
}

/* --------------- LRU page management ------------------ */

/* OSC is a natural place to manage LRU pages as applications are specialized
 * to write OSC by OSC. Ideally, if one OSC is used more frequently it should
 * occupy more LRU slots. On the other hand, we should avoid using up all LRU
 * slots (client_obd::cl_lru_left) otherwise process has to be put into sleep
 * for free LRU slots - this will be very bad so the algorithm requires each
 * OSC to free slots voluntarily to maintain a reasonable number of free slots
 * at any time.
 */
static DECLARE_WAIT_QUEUE_HEAD(osc_lru_waitq);

/**
 * LRU pages are freed in batch mode. OSC should at least free this
 * number of pages to avoid running out of LRU slots.
 */
static inline int lru_shrink_min(struct client_obd *cli)
{
	return cli->cl_max_pages_per_rpc * 2;
}

/**
 * free this number at most otherwise it will take too long time to finish.
 */
static inline int lru_shrink_max(struct client_obd *cli)
{
	return cli->cl_max_pages_per_rpc * cli->cl_max_rpcs_in_flight;
}

/**
 * Check if we can free LRU slots from this OSC. If there exists LRU waiters,
 * we should free slots aggressively. In this way, slots are freed in a steady
 * step to maintain fairness among OSCs.
 *
 * Return how many LRU pages should be freed.
 */
static int osc_cache_too_much(struct client_obd *cli)
{
	struct cl_client_cache *cache = cli->cl_cache;
	long pages = atomic_long_read(&cli->cl_lru_in_list);
	unsigned long budget;

	budget = cache->ccc_lru_max / (atomic_read(&cache->ccc_users) - 2);

	/* if it's going to run out LRU slots, we should free some, but not
	 * too much to maintain fairness among OSCs.
	 */
	if (atomic_long_read(cli->cl_lru_left) < cache->ccc_lru_max >> 2) {
		if (pages >= budget)
			return lru_shrink_max(cli);
		else if (pages >= budget / 2)
			return lru_shrink_min(cli);
	} else {
		time64_t duration = ktime_get_real_seconds();

		/* knock out pages by duration of no IO activity */
		duration -= cli->cl_lru_last_used;
		duration >>= 6; /* approximately 1 minute */
		if (duration > 0 &&
		    pages >= div64_s64((s64)budget, duration))
			return lru_shrink_min(cli);
	}
	return 0;
}

int lru_queue_work(const struct lu_env *env, void *data)
{
	struct client_obd *cli = data;
	int count;

	CDEBUG(D_CACHE, "%s: run LRU work for client obd\n", cli_name(cli));

	count = osc_cache_too_much(cli);
	if (count > 0) {
		int rc = osc_lru_shrink(env, cli, count, false);

		CDEBUG(D_CACHE, "%s: shrank %d/%d pages from client obd\n",
		       cli_name(cli), rc, count);
		if (rc >= count) {
			CDEBUG(D_CACHE, "%s: queue again\n", cli_name(cli));
			ptlrpcd_queue_work(cli->cl_lru_work);
		}
	}

	return 0;
}

void osc_lru_add_batch(struct client_obd *cli, struct list_head *plist)
{
	LIST_HEAD(lru);
	struct osc_async_page *oap;
	long npages = 0;

	list_for_each_entry(oap, plist, oap_pending_item) {
		struct osc_page *opg = oap2osc_page(oap);

		if (!opg->ops_in_lru)
			continue;

		++npages;
		LASSERT(list_empty(&opg->ops_lru));
		list_add(&opg->ops_lru, &lru);
	}

	if (npages > 0) {
		spin_lock(&cli->cl_lru_list_lock);
		list_splice_tail(&lru, &cli->cl_lru_list);
		atomic_long_sub(npages, &cli->cl_lru_busy);
		atomic_long_add(npages, &cli->cl_lru_in_list);
		cli->cl_lru_last_used = ktime_get_real_seconds();
		spin_unlock(&cli->cl_lru_list_lock);

		if (waitqueue_active(&osc_lru_waitq))
			(void)ptlrpcd_queue_work(cli->cl_lru_work);
	}
}

static void __osc_lru_del(struct client_obd *cli, struct osc_page *opg)
{
	LASSERT(atomic_long_read(&cli->cl_lru_in_list) > 0);
	list_del_init(&opg->ops_lru);
	atomic_long_dec(&cli->cl_lru_in_list);
}

/**
 * Page is being destroyed. The page may be not in LRU list, if the transfer
 * has never finished(error occurred).
 */
static void osc_lru_del(struct client_obd *cli, struct osc_page *opg)
{
	if (opg->ops_in_lru) {
		spin_lock(&cli->cl_lru_list_lock);
		if (!list_empty(&opg->ops_lru)) {
			__osc_lru_del(cli, opg);
		} else {
			LASSERT(atomic_long_read(&cli->cl_lru_busy) > 0);
			atomic_long_dec(&cli->cl_lru_busy);
		}
		spin_unlock(&cli->cl_lru_list_lock);

		atomic_long_inc(cli->cl_lru_left);
		/* this is a great place to release more LRU pages if
		 * this osc occupies too many LRU pages and kernel is
		 * stealing one of them.
		 */
		if (osc_cache_too_much(cli)) {
			CDEBUG(D_CACHE, "%s: queue LRU work\n", cli_name(cli));
			(void)ptlrpcd_queue_work(cli->cl_lru_work);
		}
		wake_up(&osc_lru_waitq);
	} else {
		LASSERT(list_empty(&opg->ops_lru));
	}
}

/**
 * Delete page from LRUlist for redirty.
 */
static void osc_lru_use(struct client_obd *cli, struct osc_page *opg)
{
	/* If page is being transferred for the first time,
	 * ops_lru should be empty
	 */
	if (opg->ops_in_lru && !list_empty(&opg->ops_lru)) {
		spin_lock(&cli->cl_lru_list_lock);
		__osc_lru_del(cli, opg);
		spin_unlock(&cli->cl_lru_list_lock);
		atomic_long_inc(&cli->cl_lru_busy);
	}
}

static void discard_pagevec(const struct lu_env *env, struct cl_io *io,
			    struct cl_page **pvec, int max_index)
{
	int i;

	for (i = 0; i < max_index; i++) {
		struct cl_page *page = pvec[i];

		LASSERT(cl_page_is_owned(page, io));
		cl_page_delete(env, page);
		cl_page_discard(env, io, page);
		cl_page_disown(env, io, page);
		cl_page_put(env, page);

		pvec[i] = NULL;
	}
}

/**
 * Check if a cl_page can be released, i.e, it's not being used.
 *
 * If unstable account is turned on, bulk transfer may hold one refcount
 * for recovery so we need to check vmpage refcount as well; otherwise,
 * even we can destroy cl_page but the corresponding vmpage can't be reused.
 */
static inline bool lru_page_busy(struct client_obd *cli, struct cl_page *page)
{
	if (cl_page_in_use_noref(page))
		return true;

	if (cli->cl_cache->ccc_unstable_check) {
		struct page *vmpage = cl_page_vmpage(page);

		/* vmpage have two known users: cl_page and VM page cache */
		if (page_count(vmpage) - page_mapcount(vmpage) > 2)
			return true;
	}
	return false;
}

/**
 * Drop @target of pages from LRU at most.
 */
long osc_lru_shrink(const struct lu_env *env, struct client_obd *cli,
		    long target, bool force)
{
	struct cl_io *io;
	struct cl_object *clobj = NULL;
	struct cl_page **pvec;
	struct osc_page *opg;
	int maxscan = 0;
	long count = 0;
	int index = 0;
	int rc = 0;

	LASSERT(atomic_long_read(&cli->cl_lru_in_list) >= 0);
	if (atomic_long_read(&cli->cl_lru_in_list) == 0 || target <= 0)
		return 0;

	CDEBUG(D_CACHE, "%s: shrinkers: %d, force: %d\n",
	       cli_name(cli), atomic_read(&cli->cl_lru_shrinkers), force);
	if (!force) {
		if (atomic_read(&cli->cl_lru_shrinkers) > 0)
			return -EBUSY;

		if (atomic_inc_return(&cli->cl_lru_shrinkers) > 1) {
			atomic_dec(&cli->cl_lru_shrinkers);
			return -EBUSY;
		}
	} else {
		atomic_inc(&cli->cl_lru_shrinkers);
	}

	pvec = (struct cl_page **)osc_env_info(env)->oti_pvec;
	io = &osc_env_info(env)->oti_io;

	spin_lock(&cli->cl_lru_list_lock);
	if (force)
		cli->cl_lru_reclaim++;
	maxscan = min(target << 1, atomic_long_read(&cli->cl_lru_in_list));
	while (!list_empty(&cli->cl_lru_list)) {
		struct cl_page *page;
		bool will_free = false;

		if (!force && atomic_read(&cli->cl_lru_shrinkers) > 1)
			break;

		if (--maxscan < 0)
			break;

		opg = list_entry(cli->cl_lru_list.next, struct osc_page,
				 ops_lru);
		page = opg->ops_cl.cpl_page;
		if (lru_page_busy(cli, page)) {
			list_move_tail(&opg->ops_lru, &cli->cl_lru_list);
			continue;
		}

		LASSERT(page->cp_obj);
		if (clobj != page->cp_obj) {
			struct cl_object *tmp = page->cp_obj;

			cl_object_get(tmp);
			spin_unlock(&cli->cl_lru_list_lock);

			if (clobj) {
				discard_pagevec(env, io, pvec, index);
				index = 0;

				cl_io_fini(env, io);
				cl_object_put(env, clobj);
				clobj = NULL;
			}

			clobj = tmp;
			io->ci_obj = clobj;
			io->ci_ignore_layout = 1;
			rc = cl_io_init(env, io, CIT_MISC, clobj);

			spin_lock(&cli->cl_lru_list_lock);

			if (rc != 0)
				break;

			++maxscan;
			continue;
		}

		if (cl_page_own_try(env, io, page) == 0) {
			if (!lru_page_busy(cli, page)) {
				/* remove it from lru list earlier to avoid
				 * lock contention
				 */
				__osc_lru_del(cli, opg);
				opg->ops_in_lru = 0; /* will be discarded */

				cl_page_get(page);
				will_free = true;
			} else {
				cl_page_disown(env, io, page);
			}
		}

		if (!will_free) {
			list_move_tail(&opg->ops_lru, &cli->cl_lru_list);
			continue;
		}

		/* Don't discard and free the page with cl_lru_list held */
		pvec[index++] = page;
		if (unlikely(index == OTI_PVEC_SIZE)) {
			spin_unlock(&cli->cl_lru_list_lock);
			discard_pagevec(env, io, pvec, index);
			index = 0;

			spin_lock(&cli->cl_lru_list_lock);
		}

		if (++count >= target)
			break;
	}
	spin_unlock(&cli->cl_lru_list_lock);

	if (clobj) {
		discard_pagevec(env, io, pvec, index);

		cl_io_fini(env, io);
		cl_object_put(env, clobj);
	}

	atomic_dec(&cli->cl_lru_shrinkers);
	if (count > 0) {
		atomic_long_add(count, cli->cl_lru_left);
		wake_up_all(&osc_lru_waitq);
	}
	return count > 0 ? count : rc;
}

/**
 * Reclaim LRU pages by an IO thread. The caller wants to reclaim at least
 * \@npages of LRU slots. For performance consideration, it's better to drop
 * LRU pages in batch. Therefore, the actual number is adjusted at least
 * max_pages_per_rpc.
 */
long osc_lru_reclaim(struct client_obd *cli, unsigned long npages)
{
	struct lu_env *env;
	struct cl_client_cache *cache = cli->cl_cache;
	int max_scans;
	int refcheck;
	long rc = 0;

	LASSERT(cache);

	env = cl_env_get(&refcheck);
	if (IS_ERR(env))
		return 0;

	npages = max_t(int, npages, cli->cl_max_pages_per_rpc);
	CDEBUG(D_CACHE, "%s: start to reclaim %ld pages from LRU\n",
	       cli_name(cli), npages);
	rc = osc_lru_shrink(env, cli, npages, true);
	if (rc >= npages) {
		CDEBUG(D_CACHE, "%s: reclaimed %ld/%ld pages from LRU\n",
		       cli_name(cli), rc, npages);
		if (osc_cache_too_much(cli) > 0)
			ptlrpcd_queue_work(cli->cl_lru_work);
		goto out;
	} else if (rc > 0) {
		npages -= rc;
	}

	CDEBUG(D_CACHE, "%s: cli %p no free slots, pages: %ld/%ld, want: %ld\n",
	       cli_name(cli), cli, atomic_long_read(&cli->cl_lru_in_list),
	       atomic_long_read(&cli->cl_lru_busy), npages);

	/* Reclaim LRU slots from other client_obd as it can't free enough
	 * from its own. This should rarely happen.
	 */
	spin_lock(&cache->ccc_lru_lock);
	LASSERT(!list_empty(&cache->ccc_lru));

	cache->ccc_lru_shrinkers++;
	list_move_tail(&cli->cl_lru_osc, &cache->ccc_lru);

	max_scans = atomic_read(&cache->ccc_users) - 2;
	while (--max_scans > 0 && !list_empty(&cache->ccc_lru)) {
		cli = list_entry(cache->ccc_lru.next, struct client_obd,
				 cl_lru_osc);

		CDEBUG(D_CACHE, "%s: cli %p LRU pages: %ld, busy: %ld.\n",
		       cli_name(cli), cli,
		       atomic_long_read(&cli->cl_lru_in_list),
		       atomic_long_read(&cli->cl_lru_busy));

		list_move_tail(&cli->cl_lru_osc, &cache->ccc_lru);
		if (osc_cache_too_much(cli) > 0) {
			spin_unlock(&cache->ccc_lru_lock);

			rc = osc_lru_shrink(env, cli, npages, true);
			spin_lock(&cache->ccc_lru_lock);
			if (rc >= npages)
				break;
			if (rc > 0)
				npages -= rc;
		}
	}
	spin_unlock(&cache->ccc_lru_lock);

out:
	cl_env_put(env, &refcheck);
	CDEBUG(D_CACHE, "%s: cli %p freed %ld pages.\n",
	       cli_name(cli), cli, rc);
	return rc;
}

/**
 * osc_lru_reserve() is called to reserve an LRU slot for a cl_page.
 *
 * Usually the LRU slots are reserved in osc_io_iter_rw_init().
 * Only in the case that the LRU slots are in extreme shortage, it should
 * have reserved enough slots for an IO.
 */
static int osc_lru_reserve(const struct lu_env *env, struct osc_object *obj,
			   struct osc_page *opg)
{
	struct l_wait_info lwi = LWI_INTR(LWI_ON_SIGNAL_NOOP, NULL);
	struct osc_io *oio = osc_env_io(env);
	struct client_obd *cli = osc_cli(obj);
	int rc = 0;

	if (!cli->cl_cache) /* shall not be in LRU */
		return 0;

	if (oio->oi_lru_reserved > 0) {
		--oio->oi_lru_reserved;
		goto out;
	}

	LASSERT(atomic_long_read(cli->cl_lru_left) >= 0);
	while (!atomic_long_add_unless(cli->cl_lru_left, -1, 0)) {
		/* run out of LRU spaces, try to drop some by itself */
		rc = osc_lru_reclaim(cli, 1);
		if (rc < 0)
			break;
		if (rc > 0)
			continue;

		cond_resched();

		rc = l_wait_event(osc_lru_waitq,
				  atomic_long_read(cli->cl_lru_left) > 0,
				  &lwi);

		if (rc < 0)
			break;
	}

out:
	if (rc >= 0) {
		atomic_long_inc(&cli->cl_lru_busy);
		opg->ops_in_lru = 1;
		rc = 0;
	}

	return rc;
}

/**
 * Atomic operations are expensive. We accumulate the accounting for the
 * same page pgdat to get better performance.
 * In practice this can work pretty good because the pages in the same RPC
 * are likely from the same page zone.
 */
static inline void unstable_page_accounting(struct ptlrpc_bulk_desc *desc,
					    int factor)
{
	int page_count = desc->bd_iov_count;
	pg_data_t *last = NULL;
	int count = 0;
	int i;

	LASSERT(ptlrpc_is_bulk_desc_kiov(desc->bd_type));

	for (i = 0; i < page_count; i++) {
		pg_data_t *pgdat = page_pgdat(BD_GET_KIOV(desc, i).bv_page);

		if (likely(pgdat == last)) {
			++count;
			continue;
		}

		if (count > 0) {
			mod_node_page_state(pgdat, NR_UNSTABLE_NFS,
					    factor * count);
			count = 0;
		}
		last = pgdat;
		++count;
	}
	if (count > 0)
		mod_node_page_state(last, NR_UNSTABLE_NFS, factor * count);
}

static inline void add_unstable_page_accounting(struct ptlrpc_bulk_desc *desc)
{
	unstable_page_accounting(desc, 1);
}

static inline void dec_unstable_page_accounting(struct ptlrpc_bulk_desc *desc)
{
	unstable_page_accounting(desc, -1);
}

/**
 * Performs "unstable" page accounting. This function balances the
 * increment operations performed in osc_inc_unstable_pages. It is
 * registered as the RPC request callback, and is executed when the
 * bulk RPC is committed on the server. Thus at this point, the pages
 * involved in the bulk transfer are no longer considered unstable.
 *
 * If this function is called, the request should have been committed
 * or req:rq_unstable must have been set; it implies that the unstable
 * statistic have been added.
 */
void osc_dec_unstable_pages(struct ptlrpc_request *req)
{
	struct client_obd *cli = &req->rq_import->imp_obd->u.cli;
	struct ptlrpc_bulk_desc *desc = req->rq_bulk;
	int page_count = desc->bd_iov_count;
	long unstable_count;

	LASSERT(page_count >= 0);
	dec_unstable_page_accounting(desc);

	unstable_count = atomic_long_sub_return(page_count,
						&cli->cl_unstable_count);
	LASSERT(unstable_count >= 0);

	unstable_count = atomic_long_sub_return(page_count,
						&cli->cl_cache->ccc_unstable_nr);
	LASSERT(unstable_count >= 0);
	if (!unstable_count)
		wake_up_all(&cli->cl_cache->ccc_unstable_waitq);

	if (waitqueue_active(&osc_lru_waitq))
		(void)ptlrpcd_queue_work(cli->cl_lru_work);
}

/**
 * "unstable" page accounting. See: osc_dec_unstable_pages.
 */
void osc_inc_unstable_pages(struct ptlrpc_request *req)
{
	struct client_obd *cli  = &req->rq_import->imp_obd->u.cli;
	struct ptlrpc_bulk_desc *desc = req->rq_bulk;
	long page_count = desc->bd_iov_count;

	/* No unstable page tracking */
	if (!cli->cl_cache || !cli->cl_cache->ccc_unstable_check)
		return;

	add_unstable_page_accounting(desc);
	atomic_long_add(page_count, &cli->cl_unstable_count);
	atomic_long_add(page_count, &cli->cl_cache->ccc_unstable_nr);

	/*
	 * If the request has already been committed (i.e. brw_commit
	 * called via rq_commit_cb), we need to undo the unstable page
	 * increments we just performed because rq_commit_cb wont be
	 * called again.
	 */
	spin_lock(&req->rq_lock);
	if (unlikely(req->rq_committed)) {
		spin_unlock(&req->rq_lock);

		osc_dec_unstable_pages(req);
	} else {
		req->rq_unstable = 1;
		spin_unlock(&req->rq_lock);
	}
}

/**
 * Check if it piggybacks SOFT_SYNC flag to OST from this OSC.
 * This function will be called by every BRW RPC so it's critical
 * to make this function fast.
 */
bool osc_over_unstable_soft_limit(struct client_obd *cli)
{
	long unstable_nr, osc_unstable_count;

	/* Can't check cli->cl_unstable_count, therefore, no soft limit */
	if (!cli->cl_cache || !cli->cl_cache->ccc_unstable_check)
		return false;

	osc_unstable_count = atomic_long_read(&cli->cl_unstable_count);
	unstable_nr = atomic_long_read(&cli->cl_cache->ccc_unstable_nr);

	CDEBUG(D_CACHE,
	       "%s: cli: %p unstable pages: %lu, osc unstable pages: %lu\n",
	       cli_name(cli), cli, unstable_nr, osc_unstable_count);

	/*
	 * If the LRU slots are in shortage - 25% remaining AND this OSC
	 * has one full RPC window of unstable pages, it's a good chance
	 * to piggyback a SOFT_SYNC flag.
	 * Please notice that the OST won't take immediate response for the
	 * SOFT_SYNC request so active OSCs will have more chance to carry
	 * the flag, this is reasonable.
	 */
	return unstable_nr > cli->cl_cache->ccc_lru_max >> 2 &&
	       osc_unstable_count > cli->cl_max_pages_per_rpc *
				    cli->cl_max_rpcs_in_flight;
}

/** @} osc */
