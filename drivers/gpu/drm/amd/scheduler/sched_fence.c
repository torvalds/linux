/*
 * Copyright 2015 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 *
 */
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <drm/drmP.h>
#include "gpu_scheduler.h"

static void amd_sched_fence_wait_cb(struct fence *f, struct fence_cb *cb)
{
	struct amd_sched_fence *fence =
		container_of(cb, struct amd_sched_fence, cb);
	list_del_init(&fence->list);
	fence_put(&fence->base);
}

struct amd_sched_fence *amd_sched_fence_create(
	struct amd_sched_entity *s_entity)
{
	struct amd_sched_fence *fence = NULL;
	fence = kzalloc(sizeof(struct amd_sched_fence), GFP_KERNEL);
	if (fence == NULL)
		return NULL;
	fence->v_seq = atomic64_inc_return(&s_entity->last_queued_v_seq);
	fence->entity = s_entity;
	spin_lock_init(&fence->lock);
	fence_init(&fence->base, &amd_sched_fence_ops,
		&fence->lock,
		s_entity->fence_context,
		fence->v_seq);
	fence_get(&fence->base);
	list_add_tail(&fence->list, &s_entity->fence_list);
	if (fence_add_callback(&fence->base,&fence->cb,
			       amd_sched_fence_wait_cb)) {
		fence_put(&fence->base);
		kfree(fence);
		return NULL;
	}
	return fence;
}

bool amd_sched_check_ts(struct amd_sched_entity *s_entity, uint64_t v_seq)
{
	return atomic64_read(&s_entity->last_signaled_v_seq) >= v_seq ? true : false;
}

void amd_sched_fence_signal(struct amd_sched_fence *fence)
{
	if (amd_sched_check_ts(fence->entity, fence->v_seq)) {
		int ret = fence_signal(&fence->base);
		if (!ret)
			FENCE_TRACE(&fence->base, "signaled from irq context\n");
		else
			FENCE_TRACE(&fence->base, "was already signaled\n");
	} else
		WARN(true, "fence process dismattch with job!\n");
}

static const char *amd_sched_fence_get_driver_name(struct fence *fence)
{
	return "amd_sched";
}

static const char *amd_sched_fence_get_timeline_name(struct fence *f)
{
	struct amd_sched_fence *fence = to_amd_sched_fence(f);
	return (const char *)fence->entity->name;
}

static bool amd_sched_fence_enable_signaling(struct fence *f)
{
	struct amd_sched_fence *fence = to_amd_sched_fence(f);

	return !amd_sched_check_ts(fence->entity, fence->v_seq);
}

static bool amd_sched_fence_is_signaled(struct fence *f)
{
	struct amd_sched_fence *fence = to_amd_sched_fence(f);

	return amd_sched_check_ts(fence->entity, fence->v_seq);
}

const struct fence_ops amd_sched_fence_ops = {
	.get_driver_name = amd_sched_fence_get_driver_name,
	.get_timeline_name = amd_sched_fence_get_timeline_name,
	.enable_signaling = amd_sched_fence_enable_signaling,
	.signaled = amd_sched_fence_is_signaled,
	.wait = fence_default_wait,
	.release = NULL,
};
