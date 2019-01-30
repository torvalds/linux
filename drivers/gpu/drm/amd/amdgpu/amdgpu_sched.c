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
#include <linux/pid.h>
#include <drm/amdgpu_drm.h>
#include "amdgpu.h"

#include "amdgpu_vm.h"

enum drm_sched_priority amdgpu_to_sched_priority(int amdgpu_priority)
{
	switch (amdgpu_priority) {
	case AMDGPU_CTX_PRIORITY_VERY_HIGH:
		return DRM_SCHED_PRIORITY_HIGH_HW;
	case AMDGPU_CTX_PRIORITY_HIGH:
		return DRM_SCHED_PRIORITY_HIGH_SW;
	case AMDGPU_CTX_PRIORITY_NORMAL:
		return DRM_SCHED_PRIORITY_NORMAL;
	case AMDGPU_CTX_PRIORITY_LOW:
	case AMDGPU_CTX_PRIORITY_VERY_LOW:
		return DRM_SCHED_PRIORITY_LOW;
	case AMDGPU_CTX_PRIORITY_UNSET:
		return DRM_SCHED_PRIORITY_UNSET;
	default:
		WARN(1, "Invalid context priority %d\n", amdgpu_priority);
		return DRM_SCHED_PRIORITY_INVALID;
	}
}

static int amdgpu_sched_process_priority_override(struct amdgpu_device *adev,
						  int fd,
						  enum drm_sched_priority priority)
{
	struct file *filp = fget(fd);
	struct amdgpu_fpriv *fpriv;
	struct amdgpu_ctx *ctx;
	uint32_t id;
	int r;

	if (!filp)
		return -EINVAL;

	r = amdgpu_file_to_fpriv(filp, &fpriv);
	if (r) {
		fput(filp);
		return r;
	}

	idr_for_each_entry(&fpriv->ctx_mgr.ctx_handles, ctx, id)
		amdgpu_ctx_priority_override(ctx, priority);

	fput(filp);

	return 0;
}

int amdgpu_sched_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *filp)
{
	union drm_amdgpu_sched *args = data;
	struct amdgpu_device *adev = dev->dev_private;
	enum drm_sched_priority priority;
	int r;

	priority = amdgpu_to_sched_priority(args->in.priority);
	if (args->in.flags || priority == DRM_SCHED_PRIORITY_INVALID)
		return -EINVAL;

	switch (args->in.op) {
	case AMDGPU_SCHED_OP_PROCESS_PRIORITY_OVERRIDE:
		r = amdgpu_sched_process_priority_override(adev,
							   args->in.fd,
							   priority);
		break;
	default:
		DRM_ERROR("Invalid sched op specified: %d\n", args->in.op);
		r = -EINVAL;
		break;
	}

	return r;
}
