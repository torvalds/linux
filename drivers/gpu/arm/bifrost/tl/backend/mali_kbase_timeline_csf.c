// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2019-2021 ARM Limited. All rights reserved.
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

#include <tl/mali_kbase_tracepoints.h>
#include <tl/mali_kbase_timeline.h>
#include <tl/mali_kbase_timeline_priv.h>

#include <mali_kbase.h>

#define GPU_FEATURES_CROSS_STREAM_SYNC_MASK (1ull << 3ull)

void kbase_create_timeline_objects(struct kbase_device *kbdev)
{
	unsigned int as_nr;
	unsigned int slot_i;
	struct kbase_context *kctx;
	struct kbase_timeline *timeline = kbdev->timeline;
	struct kbase_tlstream *summary =
		&kbdev->timeline->streams[TL_STREAM_TYPE_OBJ_SUMMARY];
	u32 const kbdev_has_cross_stream_sync =
		(kbdev->gpu_props.props.raw_props.gpu_features &
		 GPU_FEATURES_CROSS_STREAM_SYNC_MASK) ?
			1 :
			0;
	u32 const arch_maj = (kbdev->gpu_props.props.raw_props.gpu_id &
			      GPU_ID2_ARCH_MAJOR) >>
			     GPU_ID2_ARCH_MAJOR_SHIFT;
	u32 const num_sb_entries = arch_maj >= 11 ? 16 : 8;
	u32 const supports_gpu_sleep =
#ifdef KBASE_PM_RUNTIME
		kbdev->pm.backend.gpu_sleep_supported;
#else
		false;
#endif /* KBASE_PM_RUNTIME */

	/* Summarize the Address Space objects. */
	for (as_nr = 0; as_nr < kbdev->nr_hw_address_spaces; as_nr++)
		__kbase_tlstream_tl_new_as(summary, &kbdev->as[as_nr], as_nr);

	/* Create Legacy GPU object to track in AOM for dumping */
	__kbase_tlstream_tl_new_gpu(summary,
			kbdev,
			kbdev->gpu_props.props.raw_props.gpu_id,
			kbdev->gpu_props.num_cores);


	for (as_nr = 0; as_nr < kbdev->nr_hw_address_spaces; as_nr++)
		__kbase_tlstream_tl_lifelink_as_gpu(summary,
				&kbdev->as[as_nr],
				kbdev);

	/* Trace the creation of a new kbase device and set its properties. */
	__kbase_tlstream_tl_kbase_new_device(summary, kbdev->gpu_props.props.raw_props.gpu_id,
					     kbdev->gpu_props.num_cores,
					     kbdev->csf.global_iface.group_num,
					     kbdev->nr_hw_address_spaces, num_sb_entries,
					     kbdev_has_cross_stream_sync, supports_gpu_sleep);

	/* Lock the context list, to ensure no changes to the list are made
	 * while we're summarizing the contexts and their contents.
	 */
	mutex_lock(&timeline->tl_kctx_list_lock);

	/* Hold the scheduler lock while we emit the current state
	 * We also need to continue holding the lock until after the first body
	 * stream tracepoints are emitted to ensure we don't change the
	 * scheduler until after then
	 */
	mutex_lock(&kbdev->csf.scheduler.lock);

	for (slot_i = 0; slot_i < kbdev->csf.global_iface.group_num; slot_i++) {

		struct kbase_queue_group *group =
			kbdev->csf.scheduler.csg_slots[slot_i].resident_group;

		if (group)
			__kbase_tlstream_tl_kbase_device_program_csg(
				summary,
				kbdev->gpu_props.props.raw_props.gpu_id,
				group->kctx->id, group->handle, slot_i, 0);
	}

	/* Reset body stream buffers while holding the kctx lock.
	 * As we are holding the lock, we can guarantee that no kctx creation or
	 * deletion tracepoints can be fired from outside of this function by
	 * some other thread.
	 */
	kbase_timeline_streams_body_reset(timeline);

	mutex_unlock(&kbdev->csf.scheduler.lock);

	/* For each context in the device... */
	list_for_each_entry(kctx, &timeline->tl_kctx_list, tl_kctx_list_node) {
		size_t i;
		struct kbase_tlstream *body =
			&timeline->streams[TL_STREAM_TYPE_OBJ];

		/* Lock the context's KCPU queues, to ensure no KCPU-queue
		 * related actions can occur in this context from now on.
		 */
		mutex_lock(&kctx->csf.kcpu_queues.lock);

		/* Acquire the MMU lock, to ensure we don't get a concurrent
		 * address space assignment while summarizing this context's
		 * address space.
		 */
		mutex_lock(&kbdev->mmu_hw_mutex);

		/* Trace the context itself into the body stream, not the
		 * summary stream.
		 * We place this in the body to ensure it is ordered after any
		 * other tracepoints related to the contents of the context that
		 * might have been fired before acquiring all of the per-context
		 * locks.
		 * This ensures that those tracepoints will not actually affect
		 * the object model state, as they reference a context that
		 * hasn't been traced yet. They may, however, cause benign
		 * errors to be emitted.
		 */
		__kbase_tlstream_tl_kbase_new_ctx(body, kctx->id,
				kbdev->gpu_props.props.raw_props.gpu_id);

		/* Also trace with the legacy AOM tracepoint for dumping */
		__kbase_tlstream_tl_new_ctx(body,
				kctx,
				kctx->id,
				(u32)(kctx->tgid));

		/* Trace the currently assigned address space */
		if (kctx->as_nr != KBASEP_AS_NR_INVALID)
			__kbase_tlstream_tl_kbase_ctx_assign_as(body, kctx->id,
				kctx->as_nr);


		/* Trace all KCPU queues in the context into the body stream.
		 * As we acquired the KCPU lock after resetting the body stream,
		 * it's possible that some KCPU-related events for this context
		 * occurred between that reset and now.
		 * These will cause errors to be emitted when parsing the
		 * timeline, but they will not affect the correctness of the
		 * object model.
		 */
		for (i = 0; i < KBASEP_MAX_KCPU_QUEUES; i++) {
			const struct kbase_kcpu_command_queue *kcpu_queue =
				kctx->csf.kcpu_queues.array[i];

			if (kcpu_queue)
				__kbase_tlstream_tl_kbase_new_kcpuqueue(
					body, kcpu_queue, kcpu_queue->id, kcpu_queue->kctx->id,
					kcpu_queue->num_pending_cmds);
		}

		mutex_unlock(&kbdev->mmu_hw_mutex);
		mutex_unlock(&kctx->csf.kcpu_queues.lock);

		/* Now that all per-context locks for this context have been
		 * released, any per-context tracepoints that are fired from
		 * any other threads will go into the body stream after
		 * everything that was just summarised into the body stream in
		 * this iteration of the loop, so will start to correctly update
		 * the object model state.
		 */
	}

	mutex_unlock(&timeline->tl_kctx_list_lock);

	/* Static object are placed into summary packet that needs to be
	 * transmitted first. Flush all streams to make it available to
	 * user space.
	 */
	kbase_timeline_streams_flush(timeline);
}
