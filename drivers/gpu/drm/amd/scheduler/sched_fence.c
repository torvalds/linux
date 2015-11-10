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

struct amd_sched_fence *amd_sched_fence_create(struct amd_sched_entity *s_entity, void *owner)
{
	struct amd_sched_fence *fence = NULL;
	unsigned seq;

	fence = kzalloc(sizeof(struct amd_sched_fence), GFP_KERNEL);
	if (fence == NULL)
		return NULL;
	fence->owner = owner;
	fence->sched = s_entity->sched;
	spin_lock_init(&fence->lock);

	seq = atomic_inc_return(&s_entity->fence_seq);
	fence_init(&fence->base, &amd_sched_fence_ops, &fence->lock,
		   s_entity->fence_context, seq);

	return fence;
}

void amd_sched_fence_signal(struct amd_sched_fence *fence)
{
	int ret = fence_signal(&fence->base);
	if (!ret)
		FENCE_TRACE(&fence->base, "signaled from irq context\n");
	else
		FENCE_TRACE(&fence->base, "was already signaled\n");
}

static const char *amd_sched_fence_get_driver_name(struct fence *fence)
{
	return "amd_sched";
}

static const char *amd_sched_fence_get_timeline_name(struct fence *f)
{
	struct amd_sched_fence *fence = to_amd_sched_fence(f);
	return (const char *)fence->sched->name;
}

static bool amd_sched_fence_enable_signaling(struct fence *f)
{
	return true;
}

const struct fence_ops amd_sched_fence_ops = {
	.get_driver_name = amd_sched_fence_get_driver_name,
	.get_timeline_name = amd_sched_fence_get_timeline_name,
	.enable_signaling = amd_sched_fence_enable_signaling,
	.signaled = NULL,
	.wait = fence_default_wait,
	.release = NULL,
};
