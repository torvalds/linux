/*
 *
 * (C) COPYRIGHT 2019 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
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
 * SPDX-License-Identifier: GPL-2.0
 *
 */

#include "../mali_kbase_tracepoints.h"
#include "../mali_kbase_timeline.h"
#include "../mali_kbase_timeline_priv.h"

#include <mali_kbase.h>

void kbase_create_timeline_objects(struct kbase_device *kbdev)
{
	unsigned int lpu_id;
	unsigned int as_nr;
	struct kbase_context *kctx;
	struct kbase_timeline *timeline = kbdev->timeline;
	struct kbase_tlstream *summary =
		&timeline->streams[TL_STREAM_TYPE_OBJ_SUMMARY];

	/* Summarize the LPU objects. */
	for (lpu_id = 0; lpu_id < kbdev->gpu_props.num_job_slots; lpu_id++) {
		u32 *lpu =
			&kbdev->gpu_props.props.raw_props.js_features[lpu_id];
		__kbase_tlstream_tl_new_lpu(summary, lpu, lpu_id, *lpu);
	}

	/* Summarize the Address Space objects. */
	for (as_nr = 0; as_nr < kbdev->nr_hw_address_spaces; as_nr++)
		__kbase_tlstream_tl_new_as(summary, &kbdev->as[as_nr], as_nr);

	/* Create GPU object and make it retain all LPUs and address spaces. */
	__kbase_tlstream_tl_new_gpu(summary,
			kbdev,
			kbdev->gpu_props.props.raw_props.gpu_id,
			kbdev->gpu_props.num_cores);

	for (lpu_id = 0; lpu_id < kbdev->gpu_props.num_job_slots; lpu_id++) {
		void *lpu =
			&kbdev->gpu_props.props.raw_props.js_features[lpu_id];
		__kbase_tlstream_tl_lifelink_lpu_gpu(summary, lpu, kbdev);
	}

	for (as_nr = 0; as_nr < kbdev->nr_hw_address_spaces; as_nr++)
		__kbase_tlstream_tl_lifelink_as_gpu(summary,
				&kbdev->as[as_nr],
				kbdev);

	/* Lock the context list, to ensure no changes to the list are made
	 * while we're summarizing the contexts and their contents.
	 */
	mutex_lock(&kbdev->kctx_list_lock);

	/* For each context in the device... */
	list_for_each_entry(kctx, &kbdev->kctx_list, kctx_list_link) {
		/* Summarize the context itself */
		__kbase_tlstream_tl_new_ctx(summary,
				kctx,
				kctx->id,
				(u32)(kctx->tgid));
	};

	/* Reset body stream buffers while holding the kctx lock.
	 * This ensures we can't fire both summary and normal tracepoints for
	 * the same objects.
	 * If we weren't holding the lock, it's possible that the summarized
	 * objects could have been created, destroyed, or used after we
	 * constructed the summary stream tracepoints, but before we reset
	 * the body stream, resulting in losing those object event tracepoints.
	 */
	kbase_timeline_streams_body_reset(timeline);

	mutex_unlock(&kbdev->kctx_list_lock);

	/* Static object are placed into summary packet that needs to be
	 * transmitted first. Flush all streams to make it available to
	 * user space.
	 */
	kbase_timeline_streams_flush(timeline);
}