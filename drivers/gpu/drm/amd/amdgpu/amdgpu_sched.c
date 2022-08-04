/*
 * Copyright 2017 Valve Corporation
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
 * Authors: Andres Rodriguez <andresx7@gmail.com>
 */

#include <linux/fdtable.h>
#include <linux/file.h>
#include <linux/pid.h>

#include <drm/amdgpu_drm.h>

#include "amdgpu.h"
#include "amdgpu_sched.h"
#include "amdgpu_vm.h"

int amdgpu_to_sched_priority(int amdgpu_priority,
			     enum drm_sched_priority *prio)
{
	switch (amdgpu_priority) {
	case AMDGPU_CTX_PRIORITY_VERY_HIGH:
		*prio = DRM_SCHED_PRIORITY_HIGH;
		break;
	case AMDGPU_CTX_PRIORITY_HIGH:
		*prio = DRM_SCHED_PRIORITY_HIGH;
		break;
	case AMDGPU_CTX_PRIORITY_NORMAL:
		*prio = DRM_SCHED_PRIORITY_NORMAL;
		break;
	case AMDGPU_CTX_PRIORITY_LOW:
	case AMDGPU_CTX_PRIORITY_VERY_LOW:
		*prio = DRM_SCHED_PRIORITY_MIN;
		break;
	case AMDGPU_CTX_PRIORITY_UNSET:
		*prio = DRM_SCHED_PRIORITY_UNSET;
		break;
	default:
		WARN(1, "Invalid context priority %d\n", amdgpu_priority);
		return -EINVAL;
	}

	return 0;
}

static int amdgpu_sched_process_priority_override(struct amdgpu_device *adev,
						  int fd,
						  enum drm_sched_priority priority)
{
	struct fd f = fdget(fd);
	struct amdgpu_fpriv *fpriv;
	struct amdgpu_ctx *ctx;
	uint32_t id;
	int r;

	if (!f.file)
		return -EINVAL;

	r = amdgpu_file_to_fpriv(f.file, &fpriv);
	if (r) {
		fdput(f);
		return r;
	}

	idr_for_each_entry(&fpriv->ctx_mgr.ctx_handles, ctx, id)
		amdgpu_ctx_priority_override(ctx, priority);

	fdput(f);
	return 0;
}

static int amdgpu_sched_context_priority_override(struct amdgpu_device *adev,
						  int fd,
						  unsigned ctx_id,
						  enum drm_sched_priority priority)
{
	struct fd f = fdget(fd);
	struct amdgpu_fpriv *fpriv;
	struct amdgpu_ctx *ctx;
	int r;

	if (!f.file)
		return -EINVAL;

	r = amdgpu_file_to_fpriv(f.file, &fpriv);
	if (r) {
		fdput(f);
		return r;
	}

	ctx = amdgpu_ctx_get(fpriv, ctx_id);

	if (!ctx) {
		fdput(f);
		return -EINVAL;
	}

	amdgpu_ctx_priority_override(ctx, priority);
	amdgpu_ctx_put(ctx);
	fdput(f);

	return 0;
}

int amdgpu_sched_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *filp)
{
	union drm_amdgpu_sched *args = data;
	struct amdgpu_device *adev = drm_to_adev(dev);
	enum drm_sched_priority priority;
	int r;

	/* First check the op, then the op's argument.
	 */
	switch (args->in.op) {
	case AMDGPU_SCHED_OP_PROCESS_PRIORITY_OVERRIDE:
	case AMDGPU_SCHED_OP_CONTEXT_PRIORITY_OVERRIDE:
		break;
	default:
		DRM_ERROR("Invalid sched op specified: %d\n", args->in.op);
		return -EINVAL;
	}

	r = amdgpu_to_sched_priority(args->in.priority, &priority);
	if (r)
		return r;

	switch (args->in.op) {
	case AMDGPU_SCHED_OP_PROCESS_PRIORITY_OVERRIDE:
		r = amdgpu_sched_process_priority_override(adev,
							   args->in.fd,
							   priority);
		break;
	case AMDGPU_SCHED_OP_CONTEXT_PRIORITY_OVERRIDE:
		r = amdgpu_sched_context_priority_override(adev,
							   args->in.fd,
							   args->in.ctx_id,
							   priority);
		break;
	default:
		/* Impossible.
		 */
		r = -EINVAL;
		break;
	}

	return r;
}
