// SPDX-License-Identifier: GPL-2.0
/*
 * Common Block IO controller cgroup interface
 *
 * Based on ideas and code from CFQ, CFS and BFQ:
 * Copyright (C) 2003 Jens Axboe <axboe@kernel.dk>
 *
 * Copyright (C) 2008 Fabio Checconi <fabio@gandalf.sssup.it>
 *		      Paolo Valente <paolo.valente@unimore.it>
 *
 * Copyright (C) 2009 Vivek Goyal <vgoyal@redhat.com>
 * 	              Nauman Rafique <nauman@google.com>
 *
 * For policy-specific per-blkcg data:
 * Copyright (C) 2015 Paolo Valente <paolo.valente@unimore.it>
 *                    Arianna Avanzini <avanzini.arianna@gmail.com>
 */
#include <linux/ioprio.h>
#include <linux/kdev_t.h>
#include <linux/module.h>
#include <linux/sched/signal.h>
#include <linux/err.h>
#include <linux/blkdev.h>
#include <linux/backing-dev.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/atomic.h>
#include <linux/ctype.h>
#include <linux/resume_user_mode.h>
#include <linux/psi.h>
#include <linux/part_stat.h>
#include "blk.h"
#include "blk-cgroup.h"
#include "blk-ioprio.h"
#include "blk-throttle.h"
#include "blk-rq-qos.h"

/*
 * blkcg_pol_mutex protects blkcg_policy[] and policy [de]activation.
 * blkcg_pol_register_mutex nests outside of it and synchronizes entire
 * policy [un]register operations including cgroup file additions /
 * removals.  Putting cgroup file registration outside blkcg_pol_mutex
 * allows grabbing it from cgroup callbacks.
 */
static DEFINE_MUTEX(blkcg_pol_register_mutex);
static DEFINE_MUTEX(blkcg_pol_mutex);

struct blkcg blkcg_root;
EXPORT_SYMBOL_GPL(blkcg_root);

struct cgroup_subsys_state * const blkcg_root_css = &blkcg_root.css;
EXPORT_SYMBOL_GPL(blkcg_root_css);

static struct blkcg_policy *blkcg_policy[BLKCG_MAX_POLS];

static LIST_HEAD(all_blkcgs);		/* protected by blkcg_pol_mutex */

bool blkcg_debug_stats = false;
static struct workqueue_struct *blkcg_punt_bio_wq;

#define BLKG_DESTROY_BATCH_SIZE  64

/**
 * blkcg_css - find the current css
 *
 * Find the css associated with either the kthread or the current task.
 * This may return a dying css, so it is up to the caller to use tryget logic
 * to confirm it is alive and well.
 */
static struct cgroup_subsys_state *blkcg_css(void)
{
	struct cgroup_subsys_state *css;

	css = kthread_blkcg();
	if (css)
		return css;
	return task_css(current, io_cgrp_id);
}

static bool blkcg_policy_enabled(struct request_queue *q,
				 const struct blkcg_policy *pol)
{
	return pol && test_bit(pol->plid, q->blkcg_pols);
}

static void blkg_free_workfn(struct work_struct *work)
{
	struct blkcg_gq *blkg = container_of(work, struct blkcg_gq,
					     free_work);
	int i;

	for (i = 0; i < BLKCG_MAX_POLS; i++)
		if (blkg->pd[i])
			blkcg_policy[i]->pd_free_fn(blkg->pd[i]);

	if (blkg->q)
		blk_put_queue(blkg->q);
	free_percpu(blkg->iostat_cpu);
	percpu_ref_exit(&blkg->refcnt);
	kfree(blkg);
}

/**
 * blkg_free - free a blkg
 * @blkg: blkg to free
 *
 * Free @blkg which may be partially allocated.
 */
static void blkg_free(struct blkcg_gq *blkg)
{
	if (!blkg)
		return;

	/*
	 * Both ->pd_free_fn() and request queue's release handler may
	 * sleep, so free us by scheduling one work func
	 */
	INIT_WORK(&blkg->free_work, blkg_free_workfn);
	schedule_work(&blkg->free_work);
}

static void __blkg_release(struct rcu_head *rcu)
{
	struct blkcg_gq *blkg = container_of(rcu, struct blkcg_gq, rcu_head);

	WARN_ON(!bio_list_empty(&blkg->async_bios));

	/* release the blkcg and parent blkg refs this blkg has been holding */
	css_put(&blkg->blkcg->css);
	if (blkg->parent)
		blkg_put(blkg->parent);
	blkg_free(blkg);
}

/*
 * A group is RCU protected, but having an rcu lock does not mean that one
 * can access all the fields of blkg and assume these are valid.  For
 * example, don't try to follow throtl_data and request queue links.
 *
 * Having a reference to blkg under an rcu allows accesses to only values
 * local to groups like group stats and group rate limits.
 */
static void blkg_release(struct percpu_ref *ref)
{
	struct blkcg_gq *blkg = container_of(ref, struct blkcg_gq, refcnt);

	call_rcu(&blkg->rcu_head, __blkg_release);
}

static void blkg_async_bio_workfn(struct work_struct *work)
{
	struct blkcg_gq *blkg = container_of(work, struct blkcg_gq,
					     async_bio_work);
	struct bio_list bios = BIO_EMPTY_LIST;
	struct bio *bio;
	struct blk_plug plug;
	bool need_plug = false;

	/* as long as there are pending bios, @blkg can't go away */
	spin_lock_bh(&blkg->async_bio_lock);
	bio_list_merge(&bios, &blkg->async_bios);
	bio_list_init(&blkg->async_bios);
	spin_unlock_bh(&blkg->async_bio_lock);

	/* start plug only when bio_list contains at least 2 bios */
	if (bios.head && bios.head->bi_next) {
		need_plug = true;
		blk_start_plug(&plug);
	}
	while ((bio = bio_list_pop(&bios)))
		submit_bio(bio);
	if (need_plug)
		blk_finish_plug(&plug);
}

/**
 * bio_blkcg_css - return the blkcg CSS associated with a bio
 * @bio: target bio
 *
 * This returns the CSS for the blkcg associated with a bio, or %NULL if not
 * associated. Callers are expected to either handle %NULL or know association
 * has been done prior to calling this.
 */
struct cgroup_subsys_state *bio_blkcg_css(struct bio *bio)
{
	if (!bio || !bio->bi_blkg)
		return NULL;
	return &bio->bi_blkg->blkcg->css;
}
EXPORT_SYMBOL_GPL(bio_blkcg_css);

/**
 * blkcg_parent - get the parent of a blkcg
 * @blkcg: blkcg of interest
 *
 * Return the parent blkcg of @blkcg.  Can be called anytime.
 */
static inline struct blkcg *blkcg_parent(struct blkcg *blkcg)
{
	return css_to_blkcg(blkcg->css.parent);
}

/**
 * blkg_alloc - allocate a blkg
 * @blkcg: block cgroup the new blkg is associated with
 * @disk: gendisk the new blkg is associated with
 * @gfp_mask: allocation mask to use
 *
 * Allocate a new blkg assocating @blkcg and @q.
 */
static struct blkcg_gq *blkg_alloc(struct blkcg *blkcg, struct gendisk *disk,
				   gfp_t gfp_mask)
{
	struct blkcg_gq *blkg;
	int i, cpu;

	/* alloc and init base part */
	blkg = kzalloc_node(sizeof(*blkg), gfp_mask, disk->queue->node);
	if (!blkg)
		return NULL;

	if (percpu_ref_init(&blkg->refcnt, blkg_release, 0, gfp_mask))
		goto err_free;

	blkg->iostat_cpu = alloc_percpu_gfp(struct blkg_iostat_set, gfp_mask);
	if (!blkg->iostat_cpu)
		goto err_free;

	if (!blk_get_queue(disk->queue))
		goto err_free;

	blkg->q = disk->queue;
	INIT_LIST_HEAD(&blkg->q_node);
	spin_lock_init(&blkg->async_bio_lock);
	bio_list_init(&blkg->async_bios);
	INIT_WORK(&blkg->async_bio_work, blkg_async_bio_workfn);
	blkg->blkcg = blkcg;

	u64_stats_init(&blkg->iostat.sync);
	for_each_possible_cpu(cpu)
		u64_stats_init(&per_cpu_ptr(blkg->iostat_cpu, cpu)->sync);

	for (i = 0; i < BLKCG_MAX_POLS; i++) {
		struct blkcg_policy *pol = blkcg_policy[i];
		struct blkg_policy_data *pd;

		if (!blkcg_policy_enabled(disk->queue, pol))
			continue;

		/* alloc per-policy data and attach it to blkg */
		pd = pol->pd_alloc_fn(gfp_mask, disk->queue, blkcg);
		if (!pd)
			goto err_free;

		blkg->pd[i] = pd;
		pd->blkg = blkg;
		pd->plid = i;
	}

	return blkg;

err_free:
	blkg_free(blkg);
	return NULL;
}

/*
 * If @new_blkg is %NULL, this function tries to allocate a new one as
 * necessary using %GFP_NOWAIT.  @new_blkg is always consumed on return.
 */
static struct blkcg_gq *blkg_create(struct blkcg *blkcg, struct gendisk *disk,
				    struct blkcg_gq *new_blkg)
{
	struct blkcg_gq *blkg;
	int i, ret;

	lockdep_assert_held(&disk->queue->queue_lock);

	/* request_queue is dying, do not create/recreate a blkg */
	if (blk_queue_dying(disk->queue)) {
		ret = -ENODEV;
		goto err_free_blkg;
	}

	/* blkg holds a reference to blkcg */
	if (!css_tryget_online(&blkcg->css)) {
		ret = -ENODEV;
		goto err_free_blkg;
	}

	/* allocate */
	if (!new_blkg) {
		new_blkg = blkg_alloc(blkcg, disk, GFP_NOWAIT | __GFP_NOWARN);
		if (unlikely(!new_blkg)) {
			ret = -ENOMEM;
			goto err_put_css;
		}
	}
	blkg = new_blkg;

	/* link parent */
	if (blkcg_parent(blkcg)) {
		blkg->parent = blkg_lookup(blkcg_parent(blkcg), disk->queue);
		if (WARN_ON_ONCE(!blkg->parent)) {
			ret = -ENODEV;
			goto err_put_css;
		}
		blkg_get(blkg->parent);
	}

	/* invoke per-policy init */
	for (i = 0; i < BLKCG_MAX_POLS; i++) {
		struct blkcg_policy *pol = blkcg_policy[i];

		if (blkg->pd[i] && pol->pd_init_fn)
			pol->pd_init_fn(blkg->pd[i]);
	}

	/* insert */
	spin_lock(&blkcg->lock);
	ret = radix_tree_insert(&blkcg->blkg_tree, disk->queue->id, blkg);
	if (likely(!ret)) {
		hlist_add_head_rcu(&blkg->blkcg_node, &blkcg->blkg_list);
		list_add(&blkg->q_node, &disk->queue->blkg_list);

		for (i = 0; i < BLKCG_MAX_POLS; i++) {
			struct blkcg_policy *pol = blkcg_policy[i];

			if (blkg->pd[i] && pol->pd_online_fn)
				pol->pd_online_fn(blkg->pd[i]);
		}
	}
	blkg->online = true;
	spin_unlock(&blkcg->lock);

	if (!ret)
		return blkg;

	/* @blkg failed fully initialized, use the usual release path */
	blkg_put(blkg);
	return ERR_PTR(ret);

err_put_css:
	css_put(&blkcg->css);
err_free_blkg:
	blkg_free(new_blkg);
	return ERR_PTR(ret);
}

/**
 * blkg_lookup_create - lookup blkg, try to create one if not there
 * @blkcg: blkcg of interest
 * @disk: gendisk of interest
 *
 * Lookup blkg for the @blkcg - @disk pair.  If it doesn't exist, try to
 * create one.  blkg creation is performed recursively from blkcg_root such
 * that all non-root blkg's have access to the parent blkg.  This function
 * should be called under RCU read lock and takes @disk->queue->queue_lock.
 *
 * Returns the blkg or the closest blkg if blkg_create() fails as it walks
 * down from root.
 */
static struct blkcg_gq *blkg_lookup_create(struct blkcg *blkcg,
		struct gendisk *disk)
{
	struct request_queue *q = disk->queue;
	struct blkcg_gq *blkg;
	unsigned long flags;

	WARN_ON_ONCE(!rcu_read_lock_held());

	blkg = blkg_lookup(blkcg, q);
	if (blkg)
		return blkg;

	spin_lock_irqsave(&q->queue_lock, flags);
	blkg = blkg_lookup(blkcg, q);
	if (blkg) {
		if (blkcg != &blkcg_root &&
		    blkg != rcu_dereference(blkcg->blkg_hint))
			rcu_assign_pointer(blkcg->blkg_hint, blkg);
		goto found;
	}

	/*
	 * Create blkgs walking down from blkcg_root to @blkcg, so that all
	 * non-root blkgs have access to their parents.  Returns the closest
	 * blkg to the intended blkg should blkg_create() fail.
	 */
	while (true) {
		struct blkcg *pos = blkcg;
		struct blkcg *parent = blkcg_parent(blkcg);
		struct blkcg_gq *ret_blkg = q->root_blkg;

		while (parent) {
			blkg = blkg_lookup(parent, q);
			if (blkg) {
				/* remember closest blkg */
				ret_blkg = blkg;
				break;
			}
			pos = parent;
			parent = blkcg_parent(parent);
		}

		blkg = blkg_create(pos, disk, NULL);
		if (IS_ERR(blkg)) {
			blkg = ret_blkg;
			break;
		}
		if (pos == blkcg)
			break;
	}

found:
	spin_unlock_irqrestore(&q->queue_lock, flags);
	return blkg;
}

static void blkg_destroy(struct blkcg_gq *blkg)
{
	struct blkcg *blkcg = blkg->blkcg;
	int i;

	lockdep_assert_held(&blkg->q->queue_lock);
	lockdep_assert_held(&blkcg->lock);

	/* Something wrong if we are trying to remove same group twice */
	WARN_ON_ONCE(list_empty(&blkg->q_node));
	WARN_ON_ONCE(hlist_unhashed(&blkg->blkcg_node));

	for (i = 0; i < BLKCG_MAX_POLS; i++) {
		struct blkcg_policy *pol = blkcg_policy[i];

		if (blkg->pd[i] && pol->pd_offline_fn)
			pol->pd_offline_fn(blkg->pd[i]);
	}

	blkg->online = false;

	radix_tree_delete(&blkcg->blkg_tree, blkg->q->id);
	list_del_init(&blkg->q_node);
	hlist_del_init_rcu(&blkg->blkcg_node);

	/*
	 * Both setting lookup hint to and clearing it from @blkg are done
	 * under queue_lock.  If it's not pointing to @blkg now, it never
	 * will.  Hint assignment itself can race safely.
	 */
	if (rcu_access_pointer(blkcg->blkg_hint) == blkg)
		rcu_assign_pointer(blkcg->blkg_hint, NULL);

	/*
	 * Put the reference taken at the time of creation so that when all
	 * queues are gone, group can be destroyed.
	 */
	percpu_ref_kill(&blkg->refcnt);
}

static void blkg_destroy_all(struct gendisk *disk)
{
	struct request_queue *q = disk->queue;
	struct blkcg_gq *blkg, *n;
	int count = BLKG_DESTROY_BATCH_SIZE;

restart:
	spin_lock_irq(&q->queue_lock);
	list_for_each_entry_safe(blkg, n, &q->blkg_list, q_node) {
		struct blkcg *blkcg = blkg->blkcg;

		if (hlist_unhashed(&blkg->blkcg_node))
			continue;

		spin_lock(&blkcg->lock);
		blkg_destroy(blkg);
		spin_unlock(&blkcg->lock);

		/*
		 * in order to avoid holding the spin lock for too long, release
		 * it when a batch of blkgs are destroyed.
		 */
		if (!(--count)) {
			count = BLKG_DESTROY_BATCH_SIZE;
			spin_unlock_irq(&q->queue_lock);
			cond_resched();
			goto restart;
		}
	}

	q->root_blkg = NULL;
	spin_unlock_irq(&q->queue_lock);
}

static int blkcg_reset_stats(struct cgroup_subsys_state *css,
			     struct cftype *cftype, u64 val)
{
	struct blkcg *blkcg = css_to_blkcg(css);
	struct blkcg_gq *blkg;
	int i, cpu;

	mutex_lock(&blkcg_pol_mutex);
	spin_lock_irq(&blkcg->lock);

	/*
	 * Note that stat reset is racy - it doesn't synchronize against
	 * stat updates.  This is a debug feature which shouldn't exist
	 * anyway.  If you get hit by a race, retry.
	 */
	hlist_for_each_entry(blkg, &blkcg->blkg_list, blkcg_node) {
		for_each_possible_cpu(cpu) {
			struct blkg_iostat_set *bis =
				per_cpu_ptr(blkg->iostat_cpu, cpu);
			memset(bis, 0, sizeof(*bis));
		}
		memset(&blkg->iostat, 0, sizeof(blkg->iostat));

		for (i = 0; i < BLKCG_MAX_POLS; i++) {
			struct blkcg_policy *pol = blkcg_policy[i];

			if (blkg->pd[i] && pol->pd_reset_stats_fn)
				pol->pd_reset_stats_fn(blkg->pd[i]);
		}
	}

	spin_unlock_irq(&blkcg->lock);
	mutex_unlock(&blkcg_pol_mutex);
	return 0;
}

const char *blkg_dev_name(struct blkcg_gq *blkg)
{
	if (!blkg->q->disk || !blkg->q->disk->bdi->dev)
		return NULL;
	return bdi_dev_name(blkg->q->disk->bdi);
}

/**
 * blkcg_print_blkgs - helper for printing per-blkg data
 * @sf: seq_file to print to
 * @blkcg: blkcg of interest
 * @prfill: fill function to print out a blkg
 * @pol: policy in question
 * @data: data to be passed to @prfill
 * @show_total: to print out sum of prfill return values or not
 *
 * This function invokes @prfill on each blkg of @blkcg if pd for the
 * policy specified by @pol exists.  @prfill is invoked with @sf, the
 * policy data and @data and the matching queue lock held.  If @show_total
 * is %true, the sum of the return values from @prfill is printed with
 * "Total" label at the end.
 *
 * This is to be used to construct print functions for
 * cftype->read_seq_string method.
 */
void blkcg_print_blkgs(struct seq_file *sf, struct blkcg *blkcg,
		       u64 (*prfill)(struct seq_file *,
				     struct blkg_policy_data *, int),
		       const struct blkcg_policy *pol, int data,
		       bool show_total)
{
	struct blkcg_gq *blkg;
	u64 total = 0;

	rcu_read_lock();
	hlist_for_each_entry_rcu(blkg, &blkcg->blkg_list, blkcg_node) {
		spin_lock_irq(&blkg->q->queue_lock);
		if (blkcg_policy_enabled(blkg->q, pol))
			total += prfill(sf, blkg->pd[pol->plid], data);
		spin_unlock_irq(&blkg->q->queue_lock);
	}
	rcu_read_unlock();

	if (show_total)
		seq_printf(sf, "Total %llu\n", (unsigned long long)total);
}
EXPORT_SYMBOL_GPL(blkcg_print_blkgs);

/**
 * __blkg_prfill_u64 - prfill helper for a single u64 value
 * @sf: seq_file to print to
 * @pd: policy private data of interest
 * @v: value to print
 *
 * Print @v to @sf for the device assocaited with @pd.
 */
u64 __blkg_prfill_u64(struct seq_file *sf, struct blkg_policy_data *pd, u64 v)
{
	const char *dname = blkg_dev_name(pd->blkg);

	if (!dname)
		return 0;

	seq_printf(sf, "%s %llu\n", dname, (unsigned long long)v);
	return v;
}
EXPORT_SYMBOL_GPL(__blkg_prfill_u64);

/**
 * blkcg_conf_open_bdev - parse and open bdev for per-blkg config update
 * @inputp: input string pointer
 *
 * Parse the device node prefix part, MAJ:MIN, of per-blkg config update
 * from @input and get and return the matching bdev.  *@inputp is
 * updated to point past the device node prefix.  Returns an ERR_PTR()
 * value on error.
 *
 * Use this function iff blkg_conf_prep() can't be used for some reason.
 */
struct block_device *blkcg_conf_open_bdev(char **inputp)
{
	char *input = *inputp;
	unsigned int major, minor;
	struct block_device *bdev;
	int key_len;

	if (sscanf(input, "%u:%u%n", &major, &minor, &key_len) != 2)
		return ERR_PTR(-EINVAL);

	input += key_len;
	if (!isspace(*input))
		return ERR_PTR(-EINVAL);
	input = skip_spaces(input);

	bdev = blkdev_get_no_open(MKDEV(major, minor));
	if (!bdev)
		return ERR_PTR(-ENODEV);
	if (bdev_is_partition(bdev)) {
		blkdev_put_no_open(bdev);
		return ERR_PTR(-ENODEV);
	}

	*inputp = input;
	return bdev;
}

/**
 * blkg_conf_prep - parse and prepare for per-blkg config update
 * @blkcg: target block cgroup
 * @pol: target policy
 * @input: input string
 * @ctx: blkg_conf_ctx to be filled
 *
 * Parse per-blkg config update from @input and initialize @ctx with the
 * result.  @ctx->blkg points to the blkg to be updated and @ctx->body the
 * part of @input following MAJ:MIN.  This function returns with RCU read
 * lock and queue lock held and must be paired with blkg_conf_finish().
 */
int blkg_conf_prep(struct blkcg *blkcg, const struct blkcg_policy *pol,
		   char *input, struct blkg_conf_ctx *ctx)
	__acquires(rcu) __acquires(&bdev->bd_queue->queue_lock)
{
	struct block_device *bdev;
	struct gendisk *disk;
	struct request_queue *q;
	struct blkcg_gq *blkg;
	int ret;

	bdev = blkcg_conf_open_bdev(&input);
	if (IS_ERR(bdev))
		return PTR_ERR(bdev);
	disk = bdev->bd_disk;
	q = disk->queue;

	/*
	 * blkcg_deactivate_policy() requires queue to be frozen, we can grab
	 * q_usage_counter to prevent concurrent with blkcg_deactivate_policy().
	 */
	ret = blk_queue_enter(q, 0);
	if (ret)
		goto fail;

	rcu_read_lock();
	spin_lock_irq(&q->queue_lock);

	if (!blkcg_policy_enabled(q, pol)) {
		ret = -EOPNOTSUPP;
		goto fail_unlock;
	}

	blkg = blkg_lookup(blkcg, q);
	if (blkg)
		goto success;

	/*
	 * Create blkgs walking down from blkcg_root to @blkcg, so that all
	 * non-root blkgs have access to their parents.
	 */
	while (true) {
		struct blkcg *pos = blkcg;
		struct blkcg *parent;
		struct blkcg_gq *new_blkg;

		parent = blkcg_parent(blkcg);
		while (parent && !blkg_lookup(parent, q)) {
			pos = parent;
			parent = blkcg_parent(parent);
		}

		/* Drop locks to do new blkg allocation with GFP_KERNEL. */
		spin_unlock_irq(&q->queue_lock);
		rcu_read_unlock();

		new_blkg = blkg_alloc(pos, disk, GFP_KERNEL);
		if (unlikely(!new_blkg)) {
			ret = -ENOMEM;
			goto fail_exit_queue;
		}

		if (radix_tree_preload(GFP_KERNEL)) {
			blkg_free(new_blkg);
			ret = -ENOMEM;
			goto fail_exit_queue;
		}

		rcu_read_lock();
		spin_lock_irq(&q->queue_lock);

		if (!blkcg_policy_enabled(q, pol)) {
			blkg_free(new_blkg);
			ret = -EOPNOTSUPP;
			goto fail_preloaded;
		}

		blkg = blkg_lookup(pos, q);
		if (blkg) {
			blkg_free(new_blkg);
		} else {
			blkg = blkg_create(pos, disk, new_blkg);
			if (IS_ERR(blkg)) {
				ret = PTR_ERR(blkg);
				goto fail_preloaded;
			}
		}

		radix_tree_preload_end();

		if (pos == blkcg)
			goto success;
	}
success:
	blk_queue_exit(q);
	ctx->bdev = bdev;
	ctx->blkg = blkg;
	ctx->body = input;
	return 0;

fail_preloaded:
	radix_tree_preload_end();
fail_unlock:
	spin_unlock_irq(&q->queue_lock);
	rcu_read_unlock();
fail_exit_queue:
	blk_queue_exit(q);
fail:
	blkdev_put_no_open(bdev);
	/*
	 * If queue was bypassing, we should retry.  Do so after a
	 * short msleep().  It isn't strictly necessary but queue
	 * can be bypassing for some time and it's always nice to
	 * avoid busy looping.
	 */
	if (ret == -EBUSY) {
		msleep(10);
		ret = restart_syscall();
	}
	return ret;
}
EXPORT_SYMBOL_GPL(blkg_conf_prep);

/**
 * blkg_conf_finish - finish up per-blkg config update
 * @ctx: blkg_conf_ctx intiailized by blkg_conf_prep()
 *
 * Finish up after per-blkg config update.  This function must be paired
 * with blkg_conf_prep().
 */
void blkg_conf_finish(struct blkg_conf_ctx *ctx)
	__releases(&ctx->bdev->bd_queue->queue_lock) __releases(rcu)
{
	spin_unlock_irq(&bdev_get_queue(ctx->bdev)->queue_lock);
	rcu_read_unlock();
	blkdev_put_no_open(ctx->bdev);
}
EXPORT_SYMBOL_GPL(blkg_conf_finish);

static void blkg_iostat_set(struct blkg_iostat *dst, struct blkg_iostat *src)
{
	int i;

	for (i = 0; i < BLKG_IOSTAT_NR; i++) {
		dst->bytes[i] = src->bytes[i];
		dst->ios[i] = src->ios[i];
	}
}

static void blkg_iostat_add(struct blkg_iostat *dst, struct blkg_iostat *src)
{
	int i;

	for (i = 0; i < BLKG_IOSTAT_NR; i++) {
		dst->bytes[i] += src->bytes[i];
		dst->ios[i] += src->ios[i];
	}
}

static void blkg_iostat_sub(struct blkg_iostat *dst, struct blkg_iostat *src)
{
	int i;

	for (i = 0; i < BLKG_IOSTAT_NR; i++) {
		dst->bytes[i] -= src->bytes[i];
		dst->ios[i] -= src->ios[i];
	}
}

static void blkcg_iostat_update(struct blkcg_gq *blkg, struct blkg_iostat *cur,
				struct blkg_iostat *last)
{
	struct blkg_iostat delta;
	unsigned long flags;

	/* propagate percpu delta to global */
	flags = u64_stats_update_begin_irqsave(&blkg->iostat.sync);
	blkg_iostat_set(&delta, cur);
	blkg_iostat_sub(&delta, last);
	blkg_iostat_add(&blkg->iostat.cur, &delta);
	blkg_iostat_add(last, &delta);
	u64_stats_update_end_irqrestore(&blkg->iostat.sync, flags);
}

static void blkcg_rstat_flush(struct cgroup_subsys_state *css, int cpu)
{
	struct blkcg *blkcg = css_to_blkcg(css);
	struct blkcg_gq *blkg;

	/* Root-level stats are sourced from system-wide IO stats */
	if (!cgroup_parent(css->cgroup))
		return;

	rcu_read_lock();

	hlist_for_each_entry_rcu(blkg, &blkcg->blkg_list, blkcg_node) {
		struct blkcg_gq *parent = blkg->parent;
		struct blkg_iostat_set *bisc = per_cpu_ptr(blkg->iostat_cpu, cpu);
		struct blkg_iostat cur;
		unsigned int seq;

		/* fetch the current per-cpu values */
		do {
			seq = u64_stats_fetch_begin(&bisc->sync);
			blkg_iostat_set(&cur, &bisc->cur);
		} while (u64_stats_fetch_retry(&bisc->sync, seq));

		blkcg_iostat_update(blkg, &cur, &bisc->last);

		/* propagate global delta to parent (unless that's root) */
		if (parent && parent->parent)
			blkcg_iostat_update(parent, &blkg->iostat.cur,
					    &blkg->iostat.last);
	}

	rcu_read_unlock();
}

/*
 * We source root cgroup stats from the system-wide stats to avoid
 * tracking the same information twice and incurring overhead when no
 * cgroups are defined. For that reason, cgroup_rstat_flush in
 * blkcg_print_stat does not actually fill out the iostat in the root
 * cgroup's blkcg_gq.
 *
 * However, we would like to re-use the printing code between the root and
 * non-root cgroups to the extent possible. For that reason, we simulate
 * flushing the root cgroup's stats by explicitly filling in the iostat
 * with disk level statistics.
 */
static void blkcg_fill_root_iostats(void)
{
	struct class_dev_iter iter;
	struct device *dev;

	class_dev_iter_init(&iter, &block_class, NULL, &disk_type);
	while ((dev = class_dev_iter_next(&iter))) {
		struct block_device *bdev = dev_to_bdev(dev);
		struct blkcg_gq *blkg = bdev->bd_disk->queue->root_blkg;
		struct blkg_iostat tmp;
		int cpu;
		unsigned long flags;

		memset(&tmp, 0, sizeof(tmp));
		for_each_possible_cpu(cpu) {
			struct disk_stats *cpu_dkstats;

			cpu_dkstats = per_cpu_ptr(bdev->bd_stats, cpu);
			tmp.ios[BLKG_IOSTAT_READ] +=
				cpu_dkstats->ios[STAT_READ];
			tmp.ios[BLKG_IOSTAT_WRITE] +=
				cpu_dkstats->ios[STAT_WRITE];
			tmp.ios[BLKG_IOSTAT_DISCARD] +=
				cpu_dkstats->ios[STAT_DISCARD];
			// convert sectors to bytes
			tmp.bytes[BLKG_IOSTAT_READ] +=
				cpu_dkstats->sectors[STAT_READ] << 9;
			tmp.bytes[BLKG_IOSTAT_WRITE] +=
				cpu_dkstats->sectors[STAT_WRITE] << 9;
			tmp.bytes[BLKG_IOSTAT_DISCARD] +=
				cpu_dkstats->sectors[STAT_DISCARD] << 9;
		}

		flags = u64_stats_update_begin_irqsave(&blkg->iostat.sync);
		blkg_iostat_set(&blkg->iostat.cur, &tmp);
		u64_stats_update_end_irqrestore(&blkg->iostat.sync, flags);
	}
}

static void blkcg_print_one_stat(struct blkcg_gq *blkg, struct seq_file *s)
{
	struct blkg_iostat_set *bis = &blkg->iostat;
	u64 rbytes, wbytes, rios, wios, dbytes, dios;
	const char *dname;
	unsigned seq;
	int i;

	if (!blkg->online)
		return;

	dname = blkg_dev_name(blkg);
	if (!dname)
		return;

	seq_printf(s, "%s ", dname);

	do {
		seq = u64_stats_fetch_begin(&bis->sync);

		rbytes = bis->cur.bytes[BLKG_IOSTAT_READ];
		wbytes = bis->cur.bytes[BLKG_IOSTAT_WRITE];
		dbytes = bis->cur.bytes[BLKG_IOSTAT_DISCARD];
		rios = bis->cur.ios[BLKG_IOSTAT_READ];
		wios = bis->cur.ios[BLKG_IOSTAT_WRITE];
		dios = bis->cur.ios[BLKG_IOSTAT_DISCARD];
	} while (u64_stats_fetch_retry(&bis->sync, seq));

	if (rbytes || wbytes || rios || wios) {
		seq_printf(s, "rbytes=%llu wbytes=%llu rios=%llu wios=%llu dbytes=%llu dios=%llu",
			rbytes, wbytes, rios, wios,
			dbytes, dios);
	}

	if (blkcg_debug_stats && atomic_read(&blkg->use_delay)) {
		seq_printf(s, " use_delay=%d delay_nsec=%llu",
			atomic_read(&blkg->use_delay),
			atomic64_read(&blkg->delay_nsec));
	}

	for (i = 0; i < BLKCG_MAX_POLS; i++) {
		struct blkcg_policy *pol = blkcg_policy[i];

		if (!blkg->pd[i] || !pol->pd_stat_fn)
			continue;

		pol->pd_stat_fn(blkg->pd[i], s);
	}

	seq_puts(s, "\n");
}

static int blkcg_print_stat(struct seq_file *sf, void *v)
{
	struct blkcg *blkcg = css_to_blkcg(seq_css(sf));
	struct blkcg_gq *blkg;

	if (!seq_css(sf)->parent)
		blkcg_fill_root_iostats();
	else
		cgroup_rstat_flush(blkcg->css.cgroup);

	rcu_read_lock();
	hlist_for_each_entry_rcu(blkg, &blkcg->blkg_list, blkcg_node) {
		spin_lock_irq(&blkg->q->queue_lock);
		blkcg_print_one_stat(blkg, sf);
		spin_unlock_irq(&blkg->q->queue_lock);
	}
	rcu_read_unlock();
	return 0;
}

static struct cftype blkcg_files[] = {
	{
		.name = "stat",
		.seq_show = blkcg_print_stat,
	},
	{ }	/* terminate */
};

static struct cftype blkcg_legacy_files[] = {
	{
		.name = "reset_stats",
		.write_u64 = blkcg_reset_stats,
	},
	{ }	/* terminate */
};

#ifdef CONFIG_CGROUP_WRITEBACK
struct list_head *blkcg_get_cgwb_list(struct cgroup_subsys_state *css)
{
	return &css_to_blkcg(css)->cgwb_list;
}
#endif

/*
 * blkcg destruction is a three-stage process.
 *
 * 1. Destruction starts.  The blkcg_css_offline() callback is invoked
 *    which offlines writeback.  Here we tie the next stage of blkg destruction
 *    to the completion of writeback associated with the blkcg.  This lets us
 *    avoid punting potentially large amounts of outstanding writeback to root
 *    while maintaining any ongoing policies.  The next stage is triggered when
 *    the nr_cgwbs count goes to zero.
 *
 * 2. When the nr_cgwbs count goes to zero, blkcg_destroy_blkgs() is called
 *    and handles the destruction of blkgs.  Here the css reference held by
 *    the blkg is put back eventually allowing blkcg_css_free() to be called.
 *    This work may occur in cgwb_release_workfn() on the cgwb_release
 *    workqueue.  Any submitted ios that fail to get the blkg ref will be
 *    punted to the root_blkg.
 *
 * 3. Once the blkcg ref count goes to zero, blkcg_css_free() is called.
 *    This finally frees the blkcg.
 */

/**
 * blkcg_destroy_blkgs - responsible for shooting down blkgs
 * @blkcg: blkcg of interest
 *
 * blkgs should be removed while holding both q and blkcg locks.  As blkcg lock
 * is nested inside q lock, this function performs reverse double lock dancing.
 * Destroying the blkgs releases the reference held on the blkcg's css allowing
 * blkcg_css_free to eventually be called.
 *
 * This is the blkcg counterpart of ioc_release_fn().
 */
static void blkcg_destroy_blkgs(struct blkcg *blkcg)
{
	might_sleep();

	spin_lock_irq(&blkcg->lock);

	while (!hlist_empty(&blkcg->blkg_list)) {
		struct blkcg_gq *blkg = hlist_entry(blkcg->blkg_list.first,
						struct blkcg_gq, blkcg_node);
		struct request_queue *q = blkg->q;

		if (need_resched() || !spin_trylock(&q->queue_lock)) {
			/*
			 * Given that the system can accumulate a huge number
			 * of blkgs in pathological cases, check to see if we
			 * need to rescheduling to avoid softlockup.
			 */
			spin_unlock_irq(&blkcg->lock);
			cond_resched();
			spin_lock_irq(&blkcg->lock);
			continue;
		}

		blkg_destroy(blkg);
		spin_unlock(&q->queue_lock);
	}

	spin_unlock_irq(&blkcg->lock);
}

/**
 * blkcg_pin_online - pin online state
 * @blkcg_css: blkcg of interest
 *
 * While pinned, a blkcg is kept online.  This is primarily used to
 * impedance-match blkg and cgwb lifetimes so that blkg doesn't go offline
 * while an associated cgwb is still active.
 */
void blkcg_pin_online(struct cgroup_subsys_state *blkcg_css)
{
	refcount_inc(&css_to_blkcg(blkcg_css)->online_pin);
}

/**
 * blkcg_unpin_online - unpin online state
 * @blkcg_css: blkcg of interest
 *
 * This is primarily used to impedance-match blkg and cgwb lifetimes so
 * that blkg doesn't go offline while an associated cgwb is still active.
 * When this count goes to zero, all active cgwbs have finished so the
 * blkcg can continue destruction by calling blkcg_destroy_blkgs().
 */
void blkcg_unpin_online(struct cgroup_subsys_state *blkcg_css)
{
	struct blkcg *blkcg = css_to_blkcg(blkcg_css);

	do {
		if (!refcount_dec_and_test(&blkcg->online_pin))
			break;
		blkcg_destroy_blkgs(blkcg);
		blkcg = blkcg_parent(blkcg);
	} while (blkcg);
}

/**
 * blkcg_css_offline - cgroup css_offline callback
 * @css: css of interest
 *
 * This function is called when @css is about to go away.  Here the cgwbs are
 * offlined first and only once writeback associated with the blkcg has
 * finished do we start step 2 (see above).
 */
static void blkcg_css_offline(struct cgroup_subsys_state *css)
{
	/* this prevents anyone from attaching or migrating to this blkcg */
	wb_blkcg_offline(css);

	/* put the base online pin allowing step 2 to be triggered */
	blkcg_unpin_online(css);
}

static void blkcg_css_free(struct cgroup_subsys_state *css)
{
	struct blkcg *blkcg = css_to_blkcg(css);
	int i;

	mutex_lock(&blkcg_pol_mutex);

	list_del(&blkcg->all_blkcgs_node);

	for (i = 0; i < BLKCG_MAX_POLS; i++)
		if (blkcg->cpd[i])
			blkcg_policy[i]->cpd_free_fn(blkcg->cpd[i]);

	mutex_unlock(&blkcg_pol_mutex);

	kfree(blkcg);
}

static struct cgroup_subsys_state *
blkcg_css_alloc(struct cgroup_subsys_state *parent_css)
{
	struct blkcg *blkcg;
	struct cgroup_subsys_state *ret;
	int i;

	mutex_lock(&blkcg_pol_mutex);

	if (!parent_css) {
		blkcg = &blkcg_root;
	} else {
		blkcg = kzalloc(sizeof(*blkcg), GFP_KERNEL);
		if (!blkcg) {
			ret = ERR_PTR(-ENOMEM);
			goto unlock;
		}
	}

	for (i = 0; i < BLKCG_MAX_POLS ; i++) {
		struct blkcg_policy *pol = blkcg_policy[i];
		struct blkcg_policy_data *cpd;

		/*
		 * If the policy hasn't been attached yet, wait for it
		 * to be attached before doing anything else. Otherwise,
		 * check if the policy requires any specific per-cgroup
		 * data: if it does, allocate and initialize it.
		 */
		if (!pol || !pol->cpd_alloc_fn)
			continue;

		cpd = pol->cpd_alloc_fn(GFP_KERNEL);
		if (!cpd) {
			ret = ERR_PTR(-ENOMEM);
			goto free_pd_blkcg;
		}
		blkcg->cpd[i] = cpd;
		cpd->blkcg = blkcg;
		cpd->plid = i;
		if (pol->cpd_init_fn)
			pol->cpd_init_fn(cpd);
	}

	spin_lock_init(&blkcg->lock);
	refcount_set(&blkcg->online_pin, 1);
	INIT_RADIX_TREE(&blkcg->blkg_tree, GFP_NOWAIT | __GFP_NOWARN);
	INIT_HLIST_HEAD(&blkcg->blkg_list);
#ifdef CONFIG_CGROUP_WRITEBACK
	INIT_LIST_HEAD(&blkcg->cgwb_list);
#endif
	list_add_tail(&blkcg->all_blkcgs_node, &all_blkcgs);

	mutex_unlock(&blkcg_pol_mutex);
	return &blkcg->css;

free_pd_blkcg:
	for (i--; i >= 0; i--)
		if (blkcg->cpd[i])
			blkcg_policy[i]->cpd_free_fn(blkcg->cpd[i]);

	if (blkcg != &blkcg_root)
		kfree(blkcg);
unlock:
	mutex_unlock(&blkcg_pol_mutex);
	return ret;
}

static int blkcg_css_online(struct cgroup_subsys_state *css)
{
	struct blkcg *parent = blkcg_parent(css_to_blkcg(css));

	/*
	 * blkcg_pin_online() is used to delay blkcg offline so that blkgs
	 * don't go offline while cgwbs are still active on them.  Pin the
	 * parent so that offline always happens towards the root.
	 */
	if (parent)
		blkcg_pin_online(&parent->css);
	return 0;
}

int blkcg_init_disk(struct gendisk *disk)
{
	struct request_queue *q = disk->queue;
	struct blkcg_gq *new_blkg, *blkg;
	bool preloaded;
	int ret;

	INIT_LIST_HEAD(&q->blkg_list);

	new_blkg = blkg_alloc(&blkcg_root, disk, GFP_KERNEL);
	if (!new_blkg)
		return -ENOMEM;

	preloaded = !radix_tree_preload(GFP_KERNEL);

	/* Make sure the root blkg exists. */
	/* spin_lock_irq can serve as RCU read-side critical section. */
	spin_lock_irq(&q->queue_lock);
	blkg = blkg_create(&blkcg_root, disk, new_blkg);
	if (IS_ERR(blkg))
		goto err_unlock;
	q->root_blkg = blkg;
	spin_unlock_irq(&q->queue_lock);

	if (preloaded)
		radix_tree_preload_end();

	ret = blk_ioprio_init(disk);
	if (ret)
		goto err_destroy_all;

	ret = blk_throtl_init(disk);
	if (ret)
		goto err_ioprio_exit;

	ret = blk_iolatency_init(disk);
	if (ret)
		goto err_throtl_exit;

	return 0;

err_throtl_exit:
	blk_throtl_exit(disk);
err_ioprio_exit:
	blk_ioprio_exit(disk);
err_destroy_all:
	blkg_destroy_all(disk);
	return ret;
err_unlock:
	spin_unlock_irq(&q->queue_lock);
	if (preloaded)
		radix_tree_preload_end();
	return PTR_ERR(blkg);
}

void blkcg_exit_disk(struct gendisk *disk)
{
	blkg_destroy_all(disk);
	rq_qos_exit(disk->queue);
	blk_throtl_exit(disk);
}

static void blkcg_bind(struct cgroup_subsys_state *root_css)
{
	int i;

	mutex_lock(&blkcg_pol_mutex);

	for (i = 0; i < BLKCG_MAX_POLS; i++) {
		struct blkcg_policy *pol = blkcg_policy[i];
		struct blkcg *blkcg;

		if (!pol || !pol->cpd_bind_fn)
			continue;

		list_for_each_entry(blkcg, &all_blkcgs, all_blkcgs_node)
			if (blkcg->cpd[pol->plid])
				pol->cpd_bind_fn(blkcg->cpd[pol->plid]);
	}
	mutex_unlock(&blkcg_pol_mutex);
}

static void blkcg_exit(struct task_struct *tsk)
{
	if (tsk->throttle_queue)
		blk_put_queue(tsk->throttle_queue);
	tsk->throttle_queue = NULL;
}

struct cgroup_subsys io_cgrp_subsys = {
	.css_alloc = blkcg_css_alloc,
	.css_online = blkcg_css_online,
	.css_offline = blkcg_css_offline,
	.css_free = blkcg_css_free,
	.css_rstat_flush = blkcg_rstat_flush,
	.bind = blkcg_bind,
	.dfl_cftypes = blkcg_files,
	.legacy_cftypes = blkcg_legacy_files,
	.legacy_name = "blkio",
	.exit = blkcg_exit,
#ifdef CONFIG_MEMCG
	/*
	 * This ensures that, if available, memcg is automatically enabled
	 * together on the default hierarchy so that the owner cgroup can
	 * be retrieved from writeback pages.
	 */
	.depends_on = 1 << memory_cgrp_id,
#endif
};
EXPORT_SYMBOL_GPL(io_cgrp_subsys);

/**
 * blkcg_activate_policy - activate a blkcg policy on a request_queue
 * @q: request_queue of interest
 * @pol: blkcg policy to activate
 *
 * Activate @pol on @q.  Requires %GFP_KERNEL context.  @q goes through
 * bypass mode to populate its blkgs with policy_data for @pol.
 *
 * Activation happens with @q bypassed, so nobody would be accessing blkgs
 * from IO path.  Update of each blkg is protected by both queue and blkcg
 * locks so that holding either lock and testing blkcg_policy_enabled() is
 * always enough for dereferencing policy data.
 *
 * The caller is responsible for synchronizing [de]activations and policy
 * [un]registerations.  Returns 0 on success, -errno on failure.
 */
int blkcg_activate_policy(struct request_queue *q,
			  const struct blkcg_policy *pol)
{
	struct blkg_policy_data *pd_prealloc = NULL;
	struct blkcg_gq *blkg, *pinned_blkg = NULL;
	int ret;

	if (blkcg_policy_enabled(q, pol))
		return 0;

	if (queue_is_mq(q))
		blk_mq_freeze_queue(q);
retry:
	spin_lock_irq(&q->queue_lock);

	/* blkg_list is pushed at the head, reverse walk to allocate parents first */
	list_for_each_entry_reverse(blkg, &q->blkg_list, q_node) {
		struct blkg_policy_data *pd;

		if (blkg->pd[pol->plid])
			continue;

		/* If prealloc matches, use it; otherwise try GFP_NOWAIT */
		if (blkg == pinned_blkg) {
			pd = pd_prealloc;
			pd_prealloc = NULL;
		} else {
			pd = pol->pd_alloc_fn(GFP_NOWAIT | __GFP_NOWARN, q,
					      blkg->blkcg);
		}

		if (!pd) {
			/*
			 * GFP_NOWAIT failed.  Free the existing one and
			 * prealloc for @blkg w/ GFP_KERNEL.
			 */
			if (pinned_blkg)
				blkg_put(pinned_blkg);
			blkg_get(blkg);
			pinned_blkg = blkg;

			spin_unlock_irq(&q->queue_lock);

			if (pd_prealloc)
				pol->pd_free_fn(pd_prealloc);
			pd_prealloc = pol->pd_alloc_fn(GFP_KERNEL, q,
						       blkg->blkcg);
			if (pd_prealloc)
				goto retry;
			else
				goto enomem;
		}

		blkg->pd[pol->plid] = pd;
		pd->blkg = blkg;
		pd->plid = pol->plid;
	}

	/* all allocated, init in the same order */
	if (pol->pd_init_fn)
		list_for_each_entry_reverse(blkg, &q->blkg_list, q_node)
			pol->pd_init_fn(blkg->pd[pol->plid]);

	if (pol->pd_online_fn)
		list_for_each_entry_reverse(blkg, &q->blkg_list, q_node)
			pol->pd_online_fn(blkg->pd[pol->plid]);

	__set_bit(pol->plid, q->blkcg_pols);
	ret = 0;

	spin_unlock_irq(&q->queue_lock);
out:
	if (queue_is_mq(q))
		blk_mq_unfreeze_queue(q);
	if (pinned_blkg)
		blkg_put(pinned_blkg);
	if (pd_prealloc)
		pol->pd_free_fn(pd_prealloc);
	return ret;

enomem:
	/* alloc failed, nothing's initialized yet, free everything */
	spin_lock_irq(&q->queue_lock);
	list_for_each_entry(blkg, &q->blkg_list, q_node) {
		struct blkcg *blkcg = blkg->blkcg;

		spin_lock(&blkcg->lock);
		if (blkg->pd[pol->plid]) {
			pol->pd_free_fn(blkg->pd[pol->plid]);
			blkg->pd[pol->plid] = NULL;
		}
		spin_unlock(&blkcg->lock);
	}
	spin_unlock_irq(&q->queue_lock);
	ret = -ENOMEM;
	goto out;
}
EXPORT_SYMBOL_GPL(blkcg_activate_policy);

/**
 * blkcg_deactivate_policy - deactivate a blkcg policy on a request_queue
 * @q: request_queue of interest
 * @pol: blkcg policy to deactivate
 *
 * Deactivate @pol on @q.  Follows the same synchronization rules as
 * blkcg_activate_policy().
 */
void blkcg_deactivate_policy(struct request_queue *q,
			     const struct blkcg_policy *pol)
{
	struct blkcg_gq *blkg;

	if (!blkcg_policy_enabled(q, pol))
		return;

	if (queue_is_mq(q))
		blk_mq_freeze_queue(q);

	spin_lock_irq(&q->queue_lock);

	__clear_bit(pol->plid, q->blkcg_pols);

	list_for_each_entry(blkg, &q->blkg_list, q_node) {
		struct blkcg *blkcg = blkg->blkcg;

		spin_lock(&blkcg->lock);
		if (blkg->pd[pol->plid]) {
			if (pol->pd_offline_fn)
				pol->pd_offline_fn(blkg->pd[pol->plid]);
			pol->pd_free_fn(blkg->pd[pol->plid]);
			blkg->pd[pol->plid] = NULL;
		}
		spin_unlock(&blkcg->lock);
	}

	spin_unlock_irq(&q->queue_lock);

	if (queue_is_mq(q))
		blk_mq_unfreeze_queue(q);
}
EXPORT_SYMBOL_GPL(blkcg_deactivate_policy);

static void blkcg_free_all_cpd(struct blkcg_policy *pol)
{
	struct blkcg *blkcg;

	list_for_each_entry(blkcg, &all_blkcgs, all_blkcgs_node) {
		if (blkcg->cpd[pol->plid]) {
			pol->cpd_free_fn(blkcg->cpd[pol->plid]);
			blkcg->cpd[pol->plid] = NULL;
		}
	}
}

/**
 * blkcg_policy_register - register a blkcg policy
 * @pol: blkcg policy to register
 *
 * Register @pol with blkcg core.  Might sleep and @pol may be modified on
 * successful registration.  Returns 0 on success and -errno on failure.
 */
int blkcg_policy_register(struct blkcg_policy *pol)
{
	struct blkcg *blkcg;
	int i, ret;

	mutex_lock(&blkcg_pol_register_mutex);
	mutex_lock(&blkcg_pol_mutex);

	/* find an empty slot */
	ret = -ENOSPC;
	for (i = 0; i < BLKCG_MAX_POLS; i++)
		if (!blkcg_policy[i])
			break;
	if (i >= BLKCG_MAX_POLS) {
		pr_warn("blkcg_policy_register: BLKCG_MAX_POLS too small\n");
		goto err_unlock;
	}

	/* Make sure cpd/pd_alloc_fn and cpd/pd_free_fn in pairs */
	if ((!pol->cpd_alloc_fn ^ !pol->cpd_free_fn) ||
		(!pol->pd_alloc_fn ^ !pol->pd_free_fn))
		goto err_unlock;

	/* register @pol */
	pol->plid = i;
	blkcg_policy[pol->plid] = pol;

	/* allocate and install cpd's */
	if (pol->cpd_alloc_fn) {
		list_for_each_entry(blkcg, &all_blkcgs, all_blkcgs_node) {
			struct blkcg_policy_data *cpd;

			cpd = pol->cpd_alloc_fn(GFP_KERNEL);
			if (!cpd)
				goto err_free_cpds;

			blkcg->cpd[pol->plid] = cpd;
			cpd->blkcg = blkcg;
			cpd->plid = pol->plid;
			if (pol->cpd_init_fn)
				pol->cpd_init_fn(cpd);
		}
	}

	mutex_unlock(&blkcg_pol_mutex);

	/* everything is in place, add intf files for the new policy */
	if (pol->dfl_cftypes)
		WARN_ON(cgroup_add_dfl_cftypes(&io_cgrp_subsys,
					       pol->dfl_cftypes));
	if (pol->legacy_cftypes)
		WARN_ON(cgroup_add_legacy_cftypes(&io_cgrp_subsys,
						  pol->legacy_cftypes));
	mutex_unlock(&blkcg_pol_register_mutex);
	return 0;

err_free_cpds:
	if (pol->cpd_free_fn)
		blkcg_free_all_cpd(pol);

	blkcg_policy[pol->plid] = NULL;
err_unlock:
	mutex_unlock(&blkcg_pol_mutex);
	mutex_unlock(&blkcg_pol_register_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(blkcg_policy_register);

/**
 * blkcg_policy_unregister - unregister a blkcg policy
 * @pol: blkcg policy to unregister
 *
 * Undo blkcg_policy_register(@pol).  Might sleep.
 */
void blkcg_policy_unregister(struct blkcg_policy *pol)
{
	mutex_lock(&blkcg_pol_register_mutex);

	if (WARN_ON(blkcg_policy[pol->plid] != pol))
		goto out_unlock;

	/* kill the intf files first */
	if (pol->dfl_cftypes)
		cgroup_rm_cftypes(pol->dfl_cftypes);
	if (pol->legacy_cftypes)
		cgroup_rm_cftypes(pol->legacy_cftypes);

	/* remove cpds and unregister */
	mutex_lock(&blkcg_pol_mutex);

	if (pol->cpd_free_fn)
		blkcg_free_all_cpd(pol);

	blkcg_policy[pol->plid] = NULL;

	mutex_unlock(&blkcg_pol_mutex);
out_unlock:
	mutex_unlock(&blkcg_pol_register_mutex);
}
EXPORT_SYMBOL_GPL(blkcg_policy_unregister);

bool __blkcg_punt_bio_submit(struct bio *bio)
{
	struct blkcg_gq *blkg = bio->bi_blkg;

	/* consume the flag first */
	bio->bi_opf &= ~REQ_CGROUP_PUNT;

	/* never bounce for the root cgroup */
	if (!blkg->parent)
		return false;

	spin_lock_bh(&blkg->async_bio_lock);
	bio_list_add(&blkg->async_bios, bio);
	spin_unlock_bh(&blkg->async_bio_lock);

	queue_work(blkcg_punt_bio_wq, &blkg->async_bio_work);
	return true;
}

/*
 * Scale the accumulated delay based on how long it has been since we updated
 * the delay.  We only call this when we are adding delay, in case it's been a
 * while since we added delay, and when we are checking to see if we need to
 * delay a task, to account for any delays that may have occurred.
 */
static void blkcg_scale_delay(struct blkcg_gq *blkg, u64 now)
{
	u64 old = atomic64_read(&blkg->delay_start);

	/* negative use_delay means no scaling, see blkcg_set_delay() */
	if (atomic_read(&blkg->use_delay) < 0)
		return;

	/*
	 * We only want to scale down every second.  The idea here is that we
	 * want to delay people for min(delay_nsec, NSEC_PER_SEC) in a certain
	 * time window.  We only want to throttle tasks for recent delay that
	 * has occurred, in 1 second time windows since that's the maximum
	 * things can be throttled.  We save the current delay window in
	 * blkg->last_delay so we know what amount is still left to be charged
	 * to the blkg from this point onward.  blkg->last_use keeps track of
	 * the use_delay counter.  The idea is if we're unthrottling the blkg we
	 * are ok with whatever is happening now, and we can take away more of
	 * the accumulated delay as we've already throttled enough that
	 * everybody is happy with their IO latencies.
	 */
	if (time_before64(old + NSEC_PER_SEC, now) &&
	    atomic64_try_cmpxchg(&blkg->delay_start, &old, now)) {
		u64 cur = atomic64_read(&blkg->delay_nsec);
		u64 sub = min_t(u64, blkg->last_delay, now - old);
		int cur_use = atomic_read(&blkg->use_delay);

		/*
		 * We've been unthrottled, subtract a larger chunk of our
		 * accumulated delay.
		 */
		if (cur_use < blkg->last_use)
			sub = max_t(u64, sub, blkg->last_delay >> 1);

		/*
		 * This shouldn't happen, but handle it anyway.  Our delay_nsec
		 * should only ever be growing except here where we subtract out
		 * min(last_delay, 1 second), but lord knows bugs happen and I'd
		 * rather not end up with negative numbers.
		 */
		if (unlikely(cur < sub)) {
			atomic64_set(&blkg->delay_nsec, 0);
			blkg->last_delay = 0;
		} else {
			atomic64_sub(sub, &blkg->delay_nsec);
			blkg->last_delay = cur - sub;
		}
		blkg->last_use = cur_use;
	}
}

/*
 * This is called when we want to actually walk up the hierarchy and check to
 * see if we need to throttle, and then actually throttle if there is some
 * accumulated delay.  This should only be called upon return to user space so
 * we're not holding some lock that would induce a priority inversion.
 */
static void blkcg_maybe_throttle_blkg(struct blkcg_gq *blkg, bool use_memdelay)
{
	unsigned long pflags;
	bool clamp;
	u64 now = ktime_to_ns(ktime_get());
	u64 exp;
	u64 delay_nsec = 0;
	int tok;

	while (blkg->parent) {
		int use_delay = atomic_read(&blkg->use_delay);

		if (use_delay) {
			u64 this_delay;

			blkcg_scale_delay(blkg, now);
			this_delay = atomic64_read(&blkg->delay_nsec);
			if (this_delay > delay_nsec) {
				delay_nsec = this_delay;
				clamp = use_delay > 0;
			}
		}
		blkg = blkg->parent;
	}

	if (!delay_nsec)
		return;

	/*
	 * Let's not sleep for all eternity if we've amassed a huge delay.
	 * Swapping or metadata IO can accumulate 10's of seconds worth of
	 * delay, and we want userspace to be able to do _something_ so cap the
	 * delays at 0.25s. If there's 10's of seconds worth of delay then the
	 * tasks will be delayed for 0.25 second for every syscall. If
	 * blkcg_set_delay() was used as indicated by negative use_delay, the
	 * caller is responsible for regulating the range.
	 */
	if (clamp)
		delay_nsec = min_t(u64, delay_nsec, 250 * NSEC_PER_MSEC);

	if (use_memdelay)
		psi_memstall_enter(&pflags);

	exp = ktime_add_ns(now, delay_nsec);
	tok = io_schedule_prepare();
	do {
		__set_current_state(TASK_KILLABLE);
		if (!schedule_hrtimeout(&exp, HRTIMER_MODE_ABS))
			break;
	} while (!fatal_signal_pending(current));
	io_schedule_finish(tok);

	if (use_memdelay)
		psi_memstall_leave(&pflags);
}

/**
 * blkcg_maybe_throttle_current - throttle the current task if it has been marked
 *
 * This is only called if we've been marked with set_notify_resume().  Obviously
 * we can be set_notify_resume() for reasons other than blkcg throttling, so we
 * check to see if current->throttle_queue is set and if not this doesn't do
 * anything.  This should only ever be called by the resume code, it's not meant
 * to be called by people willy-nilly as it will actually do the work to
 * throttle the task if it is setup for throttling.
 */
void blkcg_maybe_throttle_current(void)
{
	struct request_queue *q = current->throttle_queue;
	struct blkcg *blkcg;
	struct blkcg_gq *blkg;
	bool use_memdelay = current->use_memdelay;

	if (!q)
		return;

	current->throttle_queue = NULL;
	current->use_memdelay = false;

	rcu_read_lock();
	blkcg = css_to_blkcg(blkcg_css());
	if (!blkcg)
		goto out;
	blkg = blkg_lookup(blkcg, q);
	if (!blkg)
		goto out;
	if (!blkg_tryget(blkg))
		goto out;
	rcu_read_unlock();

	blkcg_maybe_throttle_blkg(blkg, use_memdelay);
	blkg_put(blkg);
	blk_put_queue(q);
	return;
out:
	rcu_read_unlock();
	blk_put_queue(q);
}

/**
 * blkcg_schedule_throttle - this task needs to check for throttling
 * @gendisk: disk to throttle
 * @use_memdelay: do we charge this to memory delay for PSI
 *
 * This is called by the IO controller when we know there's delay accumulated
 * for the blkg for this task.  We do not pass the blkg because there are places
 * we call this that may not have that information, the swapping code for
 * instance will only have a block_device at that point.  This set's the
 * notify_resume for the task to check and see if it requires throttling before
 * returning to user space.
 *
 * We will only schedule once per syscall.  You can call this over and over
 * again and it will only do the check once upon return to user space, and only
 * throttle once.  If the task needs to be throttled again it'll need to be
 * re-set at the next time we see the task.
 */
void blkcg_schedule_throttle(struct gendisk *disk, bool use_memdelay)
{
	struct request_queue *q = disk->queue;

	if (unlikely(current->flags & PF_KTHREAD))
		return;

	if (current->throttle_queue != q) {
		if (!blk_get_queue(q))
			return;

		if (current->throttle_queue)
			blk_put_queue(current->throttle_queue);
		current->throttle_queue = q;
	}

	if (use_memdelay)
		current->use_memdelay = use_memdelay;
	set_notify_resume(current);
}

/**
 * blkcg_add_delay - add delay to this blkg
 * @blkg: blkg of interest
 * @now: the current time in nanoseconds
 * @delta: how many nanoseconds of delay to add
 *
 * Charge @delta to the blkg's current delay accumulation.  This is used to
 * throttle tasks if an IO controller thinks we need more throttling.
 */
void blkcg_add_delay(struct blkcg_gq *blkg, u64 now, u64 delta)
{
	if (WARN_ON_ONCE(atomic_read(&blkg->use_delay) < 0))
		return;
	blkcg_scale_delay(blkg, now);
	atomic64_add(delta, &blkg->delay_nsec);
}

/**
 * blkg_tryget_closest - try and get a blkg ref on the closet blkg
 * @bio: target bio
 * @css: target css
 *
 * As the failure mode here is to walk up the blkg tree, this ensure that the
 * blkg->parent pointers are always valid.  This returns the blkg that it ended
 * up taking a reference on or %NULL if no reference was taken.
 */
static inline struct blkcg_gq *blkg_tryget_closest(struct bio *bio,
		struct cgroup_subsys_state *css)
{
	struct blkcg_gq *blkg, *ret_blkg = NULL;

	rcu_read_lock();
	blkg = blkg_lookup_create(css_to_blkcg(css), bio->bi_bdev->bd_disk);
	while (blkg) {
		if (blkg_tryget(blkg)) {
			ret_blkg = blkg;
			break;
		}
		blkg = blkg->parent;
	}
	rcu_read_unlock();

	return ret_blkg;
}

/**
 * bio_associate_blkg_from_css - associate a bio with a specified css
 * @bio: target bio
 * @css: target css
 *
 * Associate @bio with the blkg found by combining the css's blkg and the
 * request_queue of the @bio.  An association failure is handled by walking up
 * the blkg tree.  Therefore, the blkg associated can be anything between @blkg
 * and q->root_blkg.  This situation only happens when a cgroup is dying and
 * then the remaining bios will spill to the closest alive blkg.
 *
 * A reference will be taken on the blkg and will be released when @bio is
 * freed.
 */
void bio_associate_blkg_from_css(struct bio *bio,
				 struct cgroup_subsys_state *css)
{
	if (bio->bi_blkg)
		blkg_put(bio->bi_blkg);

	if (css && css->parent) {
		bio->bi_blkg = blkg_tryget_closest(bio, css);
	} else {
		blkg_get(bdev_get_queue(bio->bi_bdev)->root_blkg);
		bio->bi_blkg = bdev_get_queue(bio->bi_bdev)->root_blkg;
	}
}
EXPORT_SYMBOL_GPL(bio_associate_blkg_from_css);

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
		css = bio_blkcg_css(bio);
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
	if (src->bi_blkg)
		bio_associate_blkg_from_css(dst, bio_blkcg_css(src));
}
EXPORT_SYMBOL_GPL(bio_clone_blkg_association);

static int blk_cgroup_io_type(struct bio *bio)
{
	if (op_is_discard(bio->bi_opf))
		return BLKG_IOSTAT_DISCARD;
	if (op_is_write(bio->bi_opf))
		return BLKG_IOSTAT_WRITE;
	return BLKG_IOSTAT_READ;
}

void blk_cgroup_bio_start(struct bio *bio)
{
	int rwd = blk_cgroup_io_type(bio), cpu;
	struct blkg_iostat_set *bis;
	unsigned long flags;

	cpu = get_cpu();
	bis = per_cpu_ptr(bio->bi_blkg->iostat_cpu, cpu);
	flags = u64_stats_update_begin_irqsave(&bis->sync);

	/*
	 * If the bio is flagged with BIO_CGROUP_ACCT it means this is a split
	 * bio and we would have already accounted for the size of the bio.
	 */
	if (!bio_flagged(bio, BIO_CGROUP_ACCT)) {
		bio_set_flag(bio, BIO_CGROUP_ACCT);
		bis->cur.bytes[rwd] += bio->bi_iter.bi_size;
	}
	bis->cur.ios[rwd]++;

	u64_stats_update_end_irqrestore(&bis->sync, flags);
	if (cgroup_subsys_on_dfl(io_cgrp_subsys))
		cgroup_rstat_updated(bio->bi_blkg->blkcg->css.cgroup, cpu);
	put_cpu();
}

bool blk_cgroup_congested(void)
{
	struct cgroup_subsys_state *css;
	bool ret = false;

	rcu_read_lock();
	for (css = blkcg_css(); css; css = css->parent) {
		if (atomic_read(&css->cgroup->congestion_count)) {
			ret = true;
			break;
		}
	}
	rcu_read_unlock();
	return ret;
}

static int __init blkcg_init(void)
{
	blkcg_punt_bio_wq = alloc_workqueue("blkcg_punt_bio",
					    WQ_MEM_RECLAIM | WQ_FREEZABLE |
					    WQ_UNBOUND | WQ_SYSFS, 0);
	if (!blkcg_punt_bio_wq)
		return -ENOMEM;
	return 0;
}
subsys_initcall(blkcg_init);

module_param(blkcg_debug_stats, bool, 0644);
MODULE_PARM_DESC(blkcg_debug_stats, "True if you want debug stats, false if not");
