// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2022 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#include <mali_kbase.h>
#include "mali_kbase_csf.h"
#include "mali_kbase_csf_tiler_heap.h"
#include "mali_kbase_csf_tiler_heap_reclaim.h"

/* Tiler heap shrinker seek value, needs to be higher than jit and memory pools */
#define HEAP_SHRINKER_SEEKS (DEFAULT_SEEKS + 2)

/* Tiler heap shrinker batch value */
#define HEAP_SHRINKER_BATCH (512)

/* Tiler heap reclaim scan (free) method size for limiting a scan run length */
#define HEAP_RECLAIM_SCAN_BATCH_SIZE (HEAP_SHRINKER_BATCH << 7)

static u8 get_kctx_highest_csg_priority(struct kbase_context *kctx)
{
	u8 prio;

	for (prio = KBASE_QUEUE_GROUP_PRIORITY_REALTIME; prio < KBASE_QUEUE_GROUP_PRIORITY_LOW;
	     prio++)
		if (!list_empty(&kctx->csf.sched.runnable_groups[prio]))
			break;

	if (prio != KBASE_QUEUE_GROUP_PRIORITY_REALTIME && kctx->csf.sched.num_idle_wait_grps) {
		struct kbase_queue_group *group;

		list_for_each_entry(group, &kctx->csf.sched.idle_wait_groups, link) {
			if (group->priority < prio)
				prio = group->priority;
		}
	}

	return prio;
}

static void detach_ctx_from_heap_reclaim_mgr(struct kbase_context *kctx)
{
	struct kbase_csf_scheduler *const scheduler = &kctx->kbdev->csf.scheduler;
	struct kbase_csf_ctx_heap_reclaim_info *info = &kctx->csf.sched.heap_info;

	lockdep_assert_held(&scheduler->lock);

	if (!list_empty(&info->mgr_link)) {
		u32 remaining = (info->nr_est_unused_pages > info->nr_freed_pages) ?
					info->nr_est_unused_pages - info->nr_freed_pages :
					0;

		list_del_init(&info->mgr_link);
		if (remaining)
			WARN_ON(atomic_sub_return(remaining, &scheduler->reclaim_mgr.unused_pages) <
				0);

		dev_dbg(kctx->kbdev->dev,
			"Reclaim_mgr_detach: ctx_%d_%d, est_pages=0%u, freed_pages=%u", kctx->tgid,
			kctx->id, info->nr_est_unused_pages, info->nr_freed_pages);
	}
}

static void attach_ctx_to_heap_reclaim_mgr(struct kbase_context *kctx)
{
	struct kbase_csf_ctx_heap_reclaim_info *const info = &kctx->csf.sched.heap_info;
	struct kbase_csf_scheduler *const scheduler = &kctx->kbdev->csf.scheduler;
	u8 const prio = get_kctx_highest_csg_priority(kctx);

	lockdep_assert_held(&scheduler->lock);

	if (WARN_ON(!list_empty(&info->mgr_link)))
		list_del_init(&info->mgr_link);

	/* Count the pages that could be freed */
	info->nr_est_unused_pages = kbase_csf_tiler_heap_count_kctx_unused_pages(kctx);
	/* Initialize the scan operation tracking pages */
	info->nr_freed_pages = 0;

	list_add_tail(&info->mgr_link, &scheduler->reclaim_mgr.ctx_lists[prio]);
	/* Accumulate the estimated pages to the manager total field */
	atomic_add(info->nr_est_unused_pages, &scheduler->reclaim_mgr.unused_pages);

	dev_dbg(kctx->kbdev->dev, "Reclaim_mgr_attach: ctx_%d_%d, est_count_pages=%u", kctx->tgid,
		kctx->id, info->nr_est_unused_pages);
}

void kbase_csf_tiler_heap_reclaim_sched_notify_grp_active(struct kbase_queue_group *group)
{
	struct kbase_context *kctx = group->kctx;
	struct kbase_csf_ctx_heap_reclaim_info *info = &kctx->csf.sched.heap_info;

	lockdep_assert_held(&kctx->kbdev->csf.scheduler.lock);

	info->on_slot_grps++;
	/* If the kctx has an on-slot change from 0 => 1, detach it from reclaim_mgr */
	if (info->on_slot_grps == 1) {
		dev_dbg(kctx->kbdev->dev, "CSG_%d_%d_%d on-slot, remove kctx from reclaim manager",
			group->kctx->tgid, group->kctx->id, group->handle);

		detach_ctx_from_heap_reclaim_mgr(kctx);
	}
}

void kbase_csf_tiler_heap_reclaim_sched_notify_grp_evict(struct kbase_queue_group *group)
{
	struct kbase_context *kctx = group->kctx;
	struct kbase_csf_ctx_heap_reclaim_info *const info = &kctx->csf.sched.heap_info;
	struct kbase_csf_scheduler *const scheduler = &kctx->kbdev->csf.scheduler;
	const u32 num_groups = kctx->kbdev->csf.global_iface.group_num;
	u32 on_slot_grps = 0;
	u32 i;

	lockdep_assert_held(&scheduler->lock);

	/* Group eviction from the scheduler is a bit more complex, but fairly less
	 * frequent in operations. Taking the opportunity to actually count the
	 * on-slot CSGs from the given kctx, for robustness and clearer code logic.
	 */
	for_each_set_bit(i, scheduler->csg_inuse_bitmap, num_groups) {
		struct kbase_csf_csg_slot *csg_slot = &scheduler->csg_slots[i];
		struct kbase_queue_group *grp = csg_slot->resident_group;

		if (unlikely(!grp))
			continue;

		if (grp->kctx == kctx)
			on_slot_grps++;
	}

	info->on_slot_grps = on_slot_grps;

	/* If the kctx has no other CSGs on-slot, handle the heap reclaim related actions */
	if (!info->on_slot_grps) {
		if (kctx->csf.sched.num_runnable_grps || kctx->csf.sched.num_idle_wait_grps) {
			/* The kctx has other operational CSGs, attach it if not yet done */
			if (list_empty(&info->mgr_link)) {
				dev_dbg(kctx->kbdev->dev,
					"CSG_%d_%d_%d evict, add kctx to reclaim manager",
					group->kctx->tgid, group->kctx->id, group->handle);

				attach_ctx_to_heap_reclaim_mgr(kctx);
			}
		} else {
			/* The kctx is a zombie after the group eviction, drop it out */
			dev_dbg(kctx->kbdev->dev,
				"CSG_%d_%d_%d evict leading to zombie kctx, dettach from reclaim manager",
				group->kctx->tgid, group->kctx->id, group->handle);

			detach_ctx_from_heap_reclaim_mgr(kctx);
		}
	}
}

void kbase_csf_tiler_heap_reclaim_sched_notify_grp_suspend(struct kbase_queue_group *group)
{
	struct kbase_context *kctx = group->kctx;
	struct kbase_csf_ctx_heap_reclaim_info *info = &kctx->csf.sched.heap_info;

	lockdep_assert_held(&kctx->kbdev->csf.scheduler.lock);

	if (!WARN_ON(info->on_slot_grps == 0))
		info->on_slot_grps--;
	/* If the kctx has no CSGs on-slot, attach it to scheduler's reclaim manager */
	if (info->on_slot_grps == 0) {
		dev_dbg(kctx->kbdev->dev, "CSG_%d_%d_%d off-slot, add kctx to reclaim manager",
			group->kctx->tgid, group->kctx->id, group->handle);

		attach_ctx_to_heap_reclaim_mgr(kctx);
	}
}

static unsigned long reclaim_unused_heap_pages(struct kbase_device *kbdev)
{
	struct kbase_csf_scheduler *const scheduler = &kbdev->csf.scheduler;
	struct kbase_csf_sched_heap_reclaim_mgr *const mgr = &scheduler->reclaim_mgr;
	unsigned long total_freed_pages = 0;
	int prio;

	lockdep_assert_held(&kbdev->csf.scheduler.lock);

	for (prio = KBASE_QUEUE_GROUP_PRIORITY_LOW;
	     total_freed_pages < HEAP_RECLAIM_SCAN_BATCH_SIZE &&
	     prio >= KBASE_QUEUE_GROUP_PRIORITY_REALTIME;
	     prio--) {
		struct kbase_csf_ctx_heap_reclaim_info *info, *tmp;
		u32 cnt_ctxs = 0;

		list_for_each_entry_safe(info, tmp, &scheduler->reclaim_mgr.ctx_lists[prio],
					 mgr_link) {
			struct kbase_context *kctx =
				container_of(info, struct kbase_context, csf.sched.heap_info);
			u32 freed_pages = kbase_csf_tiler_heap_scan_kctx_unused_pages(
				kctx, info->nr_est_unused_pages);

			if (freed_pages) {
				/* Remove the freed pages from the manager retained estimate. The
				 * accumulated removals from the kctx should not exceed the kctx
				 * initially notified contribution amount:
				 *   info->nr_est_unused_pages.
				 */
				u32 rm_cnt = MIN(info->nr_est_unused_pages - info->nr_freed_pages,
						 freed_pages);

				WARN_ON(atomic_sub_return(rm_cnt, &mgr->unused_pages) < 0);

				/* tracking the freed pages, before a potential detach call */
				info->nr_freed_pages += freed_pages;
				total_freed_pages += freed_pages;

				schedule_work(&kctx->jit_work);
			}

			/* If the kctx can't offer anymore, drop it from the reclaim manger,
			 * otherwise leave it remaining in. If the kctx changes its state (i.e.
			 * some CSGs becoming on-slot), the scheduler will pull it out.
			 */
			if (info->nr_freed_pages >= info->nr_est_unused_pages || freed_pages == 0)
				detach_ctx_from_heap_reclaim_mgr(kctx);

			cnt_ctxs++;

			/* Enough has been freed, break to avoid holding the lock too long */
			if (total_freed_pages >= HEAP_RECLAIM_SCAN_BATCH_SIZE)
				break;
		}

		dev_dbg(kbdev->dev, "Reclaim free heap pages: %lu (cnt_ctxs: %u, prio: %d)",
			total_freed_pages, cnt_ctxs, prio);
	}

	dev_dbg(kbdev->dev, "Reclaim free total heap pages: %lu (across all CSG priority)",
		total_freed_pages);

	return total_freed_pages;
}

static unsigned long kbase_csf_tiler_heap_reclaim_count_free_pages(struct kbase_device *kbdev,
								   struct shrink_control *sc)
{
	struct kbase_csf_sched_heap_reclaim_mgr *mgr = &kbdev->csf.scheduler.reclaim_mgr;
	unsigned long page_cnt = atomic_read(&mgr->unused_pages);

	dev_dbg(kbdev->dev, "Reclaim count unused pages (estimate): %lu", page_cnt);

	return page_cnt;
}

static unsigned long kbase_csf_tiler_heap_reclaim_scan_free_pages(struct kbase_device *kbdev,
								  struct shrink_control *sc)
{
	struct kbase_csf_sched_heap_reclaim_mgr *mgr = &kbdev->csf.scheduler.reclaim_mgr;
	unsigned long freed = 0;
	unsigned long avail = 0;

	/* If Scheduler is busy in action, return 0 */
	if (!mutex_trylock(&kbdev->csf.scheduler.lock)) {
		struct kbase_csf_scheduler *const scheduler = &kbdev->csf.scheduler;

		/* Wait for roughly 2-ms */
		wait_event_timeout(kbdev->csf.event_wait, (scheduler->state != SCHED_BUSY),
				   msecs_to_jiffies(2));
		if (!mutex_trylock(&kbdev->csf.scheduler.lock)) {
			dev_dbg(kbdev->dev, "Tiler heap reclaim scan see device busy (freed: 0)");
			return 0;
		}
	}

	avail = atomic_read(&mgr->unused_pages);
	if (avail)
		freed = reclaim_unused_heap_pages(kbdev);

	mutex_unlock(&kbdev->csf.scheduler.lock);

#if (KERNEL_VERSION(4, 14, 0) <= LINUX_VERSION_CODE)
	if (freed > sc->nr_to_scan)
		sc->nr_scanned = freed;
#endif /* (KERNEL_VERSION(4, 14, 0) <= LINUX_VERSION_CODE) */

	dev_info(kbdev->dev, "Tiler heap reclaim scan freed pages: %lu (unused: %lu)", freed,
		 avail);

	/* On estimate suggesting available, yet actual free failed, return STOP */
	if (avail && !freed)
		return SHRINK_STOP;
	else
		return freed;
}

static unsigned long kbase_csf_tiler_heap_reclaim_count_objects(struct shrinker *s,
								struct shrink_control *sc)
{
	struct kbase_device *kbdev =
		container_of(s, struct kbase_device, csf.scheduler.reclaim_mgr.heap_reclaim);

	return kbase_csf_tiler_heap_reclaim_count_free_pages(kbdev, sc);
}

static unsigned long kbase_csf_tiler_heap_reclaim_scan_objects(struct shrinker *s,
							       struct shrink_control *sc)
{
	struct kbase_device *kbdev =
		container_of(s, struct kbase_device, csf.scheduler.reclaim_mgr.heap_reclaim);

	return kbase_csf_tiler_heap_reclaim_scan_free_pages(kbdev, sc);
}

void kbase_csf_tiler_heap_reclaim_ctx_init(struct kbase_context *kctx)
{
	/* Per-kctx heap_info object initialization */
	memset(&kctx->csf.sched.heap_info, 0, sizeof(struct kbase_csf_ctx_heap_reclaim_info));
	INIT_LIST_HEAD(&kctx->csf.sched.heap_info.mgr_link);
}

void kbase_csf_tiler_heap_reclaim_mgr_init(struct kbase_device *kbdev)
{
	struct kbase_csf_scheduler *scheduler = &kbdev->csf.scheduler;
	struct shrinker *reclaim = &scheduler->reclaim_mgr.heap_reclaim;
	u8 prio;

	for (prio = KBASE_QUEUE_GROUP_PRIORITY_REALTIME; prio < KBASE_QUEUE_GROUP_PRIORITY_COUNT;
	     prio++)
		INIT_LIST_HEAD(&scheduler->reclaim_mgr.ctx_lists[prio]);

	atomic_set(&scheduler->reclaim_mgr.unused_pages, 0);

	reclaim->count_objects = kbase_csf_tiler_heap_reclaim_count_objects;
	reclaim->scan_objects = kbase_csf_tiler_heap_reclaim_scan_objects;
	reclaim->seeks = HEAP_SHRINKER_SEEKS;
	reclaim->batch = HEAP_SHRINKER_BATCH;

#if !defined(CONFIG_MALI_VECTOR_DUMP)
	register_shrinker(reclaim);
#endif
}

void kbase_csf_tiler_heap_reclaim_mgr_term(struct kbase_device *kbdev)
{
	struct kbase_csf_scheduler *scheduler = &kbdev->csf.scheduler;
	u8 prio;

#if !defined(CONFIG_MALI_VECTOR_DUMP)
	unregister_shrinker(&scheduler->reclaim_mgr.heap_reclaim);
#endif

	for (prio = KBASE_QUEUE_GROUP_PRIORITY_REALTIME; prio < KBASE_QUEUE_GROUP_PRIORITY_COUNT;
	     prio++)
		WARN_ON(!list_empty(&scheduler->reclaim_mgr.ctx_lists[prio]));

	WARN_ON(atomic_read(&scheduler->reclaim_mgr.unused_pages));
}
