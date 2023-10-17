/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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
 */

#include "amdgpu_reset.h"
#include "aldebaran.h"
#include "sienna_cichlid.h"
#include "smu_v13_0_10.h"

int amdgpu_reset_add_handler(struct amdgpu_reset_control *reset_ctl,
			     struct amdgpu_reset_handler *handler)
{
	/* TODO: Check if handler exists? */
	list_add_tail(&handler->handler_list, &reset_ctl->reset_handlers);
	return 0;
}

int amdgpu_reset_init(struct amdgpu_device *adev)
{
	int ret = 0;

	switch (adev->ip_versions[MP1_HWIP][0]) {
	case IP_VERSION(13, 0, 2):
	case IP_VERSION(13, 0, 6):
		ret = aldebaran_reset_init(adev);
		break;
	case IP_VERSION(11, 0, 7):
		ret = sienna_cichlid_reset_init(adev);
		break;
	case IP_VERSION(13, 0, 10):
		ret = smu_v13_0_10_reset_init(adev);
		break;
	default:
		break;
	}

	return ret;
}

int amdgpu_reset_fini(struct amdgpu_device *adev)
{
	int ret = 0;

	switch (adev->ip_versions[MP1_HWIP][0]) {
	case IP_VERSION(13, 0, 2):
	case IP_VERSION(13, 0, 6):
		ret = aldebaran_reset_fini(adev);
		break;
	case IP_VERSION(11, 0, 7):
		ret = sienna_cichlid_reset_fini(adev);
		break;
	case IP_VERSION(13, 0, 10):
		ret = smu_v13_0_10_reset_fini(adev);
		break;
	default:
		break;
	}

	return ret;
}

int amdgpu_reset_prepare_hwcontext(struct amdgpu_device *adev,
				   struct amdgpu_reset_context *reset_context)
{
	struct amdgpu_reset_handler *reset_handler = NULL;

	if (adev->reset_cntl && adev->reset_cntl->get_reset_handler)
		reset_handler = adev->reset_cntl->get_reset_handler(
			adev->reset_cntl, reset_context);
	if (!reset_handler)
		return -EOPNOTSUPP;

	return reset_handler->prepare_hwcontext(adev->reset_cntl,
						reset_context);
}

int amdgpu_reset_perform_reset(struct amdgpu_device *adev,
			       struct amdgpu_reset_context *reset_context)
{
	int ret;
	struct amdgpu_reset_handler *reset_handler = NULL;

	if (adev->reset_cntl)
		reset_handler = adev->reset_cntl->get_reset_handler(
			adev->reset_cntl, reset_context);
	if (!reset_handler)
		return -EOPNOTSUPP;

	ret = reset_handler->perform_reset(adev->reset_cntl, reset_context);
	if (ret)
		return ret;

	return reset_handler->restore_hwcontext(adev->reset_cntl,
						reset_context);
}


void amdgpu_reset_destroy_reset_domain(struct kref *ref)
{
	struct amdgpu_reset_domain *reset_domain = container_of(ref,
								struct amdgpu_reset_domain,
								refcount);
	if (reset_domain->wq)
		destroy_workqueue(reset_domain->wq);

	kvfree(reset_domain);
}

struct amdgpu_reset_domain *amdgpu_reset_create_reset_domain(enum amdgpu_reset_domain_type type,
							     char *wq_name)
{
	struct amdgpu_reset_domain *reset_domain;

	reset_domain = kvzalloc(sizeof(struct amdgpu_reset_domain), GFP_KERNEL);
	if (!reset_domain) {
		DRM_ERROR("Failed to allocate amdgpu_reset_domain!");
		return NULL;
	}

	reset_domain->type = type;
	kref_init(&reset_domain->refcount);

	reset_domain->wq = create_singlethread_workqueue(wq_name);
	if (!reset_domain->wq) {
		DRM_ERROR("Failed to allocate wq for amdgpu_reset_domain!");
		amdgpu_reset_put_reset_domain(reset_domain);
		return NULL;

	}

	atomic_set(&reset_domain->in_gpu_reset, 0);
	atomic_set(&reset_domain->reset_res, 0);
	init_rwsem(&reset_domain->sem);

	return reset_domain;
}

void amdgpu_device_lock_reset_domain(struct amdgpu_reset_domain *reset_domain)
{
	atomic_set(&reset_domain->in_gpu_reset, 1);
	down_write(&reset_domain->sem);
}


void amdgpu_device_unlock_reset_domain(struct amdgpu_reset_domain *reset_domain)
{
	atomic_set(&reset_domain->in_gpu_reset, 0);
	up_write(&reset_domain->sem);
}



