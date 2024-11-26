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

static int amdgpu_reset_xgmi_reset_on_init_suspend(struct amdgpu_device *adev)
{
	int i;

	for (i = adev->num_ip_blocks - 1; i >= 0; i--) {
		if (!adev->ip_blocks[i].status.valid)
			continue;
		if (!adev->ip_blocks[i].status.hw)
			continue;
		/* displays are handled in phase1 */
		if (adev->ip_blocks[i].version->type == AMD_IP_BLOCK_TYPE_DCE)
			continue;

		/* XXX handle errors */
		amdgpu_ip_block_suspend(&adev->ip_blocks[i]);
		adev->ip_blocks[i].status.hw = false;
	}

	/* VCN FW shared region is in frambuffer, there are some flags
	 * initialized in that region during sw_init. Make sure the region is
	 * backed up.
	 */
	amdgpu_vcn_save_vcpu_bo(adev);

	return 0;
}

static int amdgpu_reset_xgmi_reset_on_init_prep_hwctxt(
	struct amdgpu_reset_control *reset_ctl,
	struct amdgpu_reset_context *reset_context)
{
	struct list_head *reset_device_list = reset_context->reset_device_list;
	struct amdgpu_device *tmp_adev;
	int r;

	list_for_each_entry(tmp_adev, reset_device_list, reset_list) {
		amdgpu_unregister_gpu_instance(tmp_adev);
		r = amdgpu_reset_xgmi_reset_on_init_suspend(tmp_adev);
		if (r) {
			dev_err(tmp_adev->dev,
				"xgmi reset on init: prepare for reset failed");
			return r;
		}
	}

	return r;
}

static int amdgpu_reset_xgmi_reset_on_init_restore_hwctxt(
	struct amdgpu_reset_control *reset_ctl,
	struct amdgpu_reset_context *reset_context)
{
	struct list_head *reset_device_list = reset_context->reset_device_list;
	struct amdgpu_device *tmp_adev = NULL;
	int r;

	r = amdgpu_device_reinit_after_reset(reset_context);
	if (r)
		return r;
	list_for_each_entry(tmp_adev, reset_device_list, reset_list) {
		if (!tmp_adev->kfd.init_complete) {
			kgd2kfd_init_zone_device(tmp_adev);
			amdgpu_amdkfd_device_init(tmp_adev);
			amdgpu_amdkfd_drm_client_create(tmp_adev);
		}
	}

	return r;
}

static int amdgpu_reset_xgmi_reset_on_init_perform_reset(
	struct amdgpu_reset_control *reset_ctl,
	struct amdgpu_reset_context *reset_context)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)reset_ctl->handle;
	struct list_head *reset_device_list = reset_context->reset_device_list;
	struct amdgpu_device *tmp_adev = NULL;
	int r;

	dev_dbg(adev->dev, "xgmi roi - hw reset\n");

	list_for_each_entry(tmp_adev, reset_device_list, reset_list) {
		mutex_lock(&tmp_adev->reset_cntl->reset_lock);
		tmp_adev->reset_cntl->active_reset =
			amdgpu_asic_reset_method(adev);
	}
	r = 0;
	/* Mode1 reset needs to be triggered on all devices together */
	list_for_each_entry(tmp_adev, reset_device_list, reset_list) {
		/* For XGMI run all resets in parallel to speed up the process */
		if (!queue_work(system_unbound_wq, &tmp_adev->xgmi_reset_work))
			r = -EALREADY;
		if (r) {
			dev_err(tmp_adev->dev,
				"xgmi reset on init: reset failed with error, %d",
				r);
			break;
		}
	}

	/* For XGMI wait for all resets to complete before proceed */
	if (!r) {
		list_for_each_entry(tmp_adev, reset_device_list, reset_list) {
			flush_work(&tmp_adev->xgmi_reset_work);
			r = tmp_adev->asic_reset_res;
			if (r)
				break;
		}
	}

	list_for_each_entry(tmp_adev, reset_device_list, reset_list) {
		mutex_unlock(&tmp_adev->reset_cntl->reset_lock);
		tmp_adev->reset_cntl->active_reset = AMD_RESET_METHOD_NONE;
	}

	return r;
}

int amdgpu_reset_do_xgmi_reset_on_init(
	struct amdgpu_reset_context *reset_context)
{
	struct list_head *reset_device_list = reset_context->reset_device_list;
	struct amdgpu_device *adev;
	int r;

	if (!reset_device_list || list_empty(reset_device_list) ||
	    list_is_singular(reset_device_list))
		return -EINVAL;

	adev = list_first_entry(reset_device_list, struct amdgpu_device,
				reset_list);
	r = amdgpu_reset_prepare_hwcontext(adev, reset_context);
	if (r)
		return r;

	r = amdgpu_reset_perform_reset(adev, reset_context);

	return r;
}

struct amdgpu_reset_handler xgmi_reset_on_init_handler = {
	.reset_method = AMD_RESET_METHOD_ON_INIT,
	.prepare_env = NULL,
	.prepare_hwcontext = amdgpu_reset_xgmi_reset_on_init_prep_hwctxt,
	.perform_reset = amdgpu_reset_xgmi_reset_on_init_perform_reset,
	.restore_hwcontext = amdgpu_reset_xgmi_reset_on_init_restore_hwctxt,
	.restore_env = NULL,
	.do_reset = NULL,
};

int amdgpu_reset_init(struct amdgpu_device *adev)
{
	int ret = 0;

	switch (amdgpu_ip_version(adev, MP1_HWIP, 0)) {
	case IP_VERSION(13, 0, 2):
	case IP_VERSION(13, 0, 6):
	case IP_VERSION(13, 0, 14):
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

	switch (amdgpu_ip_version(adev, MP1_HWIP, 0)) {
	case IP_VERSION(13, 0, 2):
	case IP_VERSION(13, 0, 6):
	case IP_VERSION(13, 0, 14):
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

void amdgpu_reset_get_desc(struct amdgpu_reset_context *rst_ctxt, char *buf,
			   size_t len)
{
	if (!buf || !len)
		return;

	switch (rst_ctxt->src) {
	case AMDGPU_RESET_SRC_JOB:
		if (rst_ctxt->job) {
			snprintf(buf, len, "job hang on ring:%s",
				 rst_ctxt->job->base.sched->name);
		} else {
			strscpy(buf, "job hang", len);
		}
		break;
	case AMDGPU_RESET_SRC_RAS:
		strscpy(buf, "RAS error", len);
		break;
	case AMDGPU_RESET_SRC_MES:
		strscpy(buf, "MES hang", len);
		break;
	case AMDGPU_RESET_SRC_HWS:
		strscpy(buf, "HWS hang", len);
		break;
	case AMDGPU_RESET_SRC_USER:
		strscpy(buf, "user trigger", len);
		break;
	default:
		strscpy(buf, "unknown", len);
	}
}
