/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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

#include "smu_v13_0_10.h"
#include "amdgpu_reset.h"
#include "amdgpu_dpm.h"
#include "amdgpu_job.h"
#include "amdgpu_ring.h"
#include "amdgpu_ras.h"
#include "amdgpu_psp.h"

static bool smu_v13_0_10_is_mode2_default(struct amdgpu_reset_control *reset_ctl)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)reset_ctl->handle;
	if (adev->pm.fw_version >= 0x00502005 && !amdgpu_sriov_vf(adev))
		return true;

	return false;
}

static struct amdgpu_reset_handler *
smu_v13_0_10_get_reset_handler(struct amdgpu_reset_control *reset_ctl,
			    struct amdgpu_reset_context *reset_context)
{
	struct amdgpu_reset_handler *handler;
	struct amdgpu_device *adev = (struct amdgpu_device *)reset_ctl->handle;
	int i;

	if (reset_context->method != AMD_RESET_METHOD_NONE) {
		for_each_handler(i, handler, reset_ctl) {
			if (handler->reset_method == reset_context->method)
				return handler;
		}
	}

	if (smu_v13_0_10_is_mode2_default(reset_ctl) &&
		amdgpu_asic_reset_method(adev) == AMD_RESET_METHOD_MODE2) {
		for_each_handler(i, handler, reset_ctl)	{
			if (handler->reset_method == AMD_RESET_METHOD_MODE2)
				return handler;
		}
	}

	return NULL;
}

static int smu_v13_0_10_mode2_suspend_ip(struct amdgpu_device *adev)
{
	int r, i;

	amdgpu_device_set_pg_state(adev, AMD_PG_STATE_UNGATE);
	amdgpu_device_set_cg_state(adev, AMD_CG_STATE_UNGATE);

	for (i = adev->num_ip_blocks - 1; i >= 0; i--) {
		if (!(adev->ip_blocks[i].version->type ==
			      AMD_IP_BLOCK_TYPE_GFX ||
		      adev->ip_blocks[i].version->type ==
			      AMD_IP_BLOCK_TYPE_SDMA ||
		      adev->ip_blocks[i].version->type ==
			      AMD_IP_BLOCK_TYPE_MES))
			continue;

		r = adev->ip_blocks[i].version->funcs->suspend(adev);

		if (r) {
			dev_err(adev->dev,
				"suspend of IP block <%s> failed %d\n",
				adev->ip_blocks[i].version->funcs->name, r);
			return r;
		}
		adev->ip_blocks[i].status.hw = false;
	}

	return r;
}

static int
smu_v13_0_10_mode2_prepare_hwcontext(struct amdgpu_reset_control *reset_ctl,
				  struct amdgpu_reset_context *reset_context)
{
	int r = 0;
	struct amdgpu_device *adev = (struct amdgpu_device *)reset_ctl->handle;

	if (!amdgpu_sriov_vf(adev))
		r = smu_v13_0_10_mode2_suspend_ip(adev);

	return r;
}

static int smu_v13_0_10_mode2_reset(struct amdgpu_device *adev)
{
	return amdgpu_dpm_mode2_reset(adev);
}

static void smu_v13_0_10_async_reset(struct work_struct *work)
{
	struct amdgpu_reset_handler *handler;
	struct amdgpu_reset_control *reset_ctl =
		container_of(work, struct amdgpu_reset_control, reset_work);
	struct amdgpu_device *adev = (struct amdgpu_device *)reset_ctl->handle;
	int i;

	for_each_handler(i, handler, reset_ctl)	{
		if (handler->reset_method == reset_ctl->active_reset) {
			dev_dbg(adev->dev, "Resetting device\n");
			handler->do_reset(adev);
			break;
		}
	}
}
static int
smu_v13_0_10_mode2_perform_reset(struct amdgpu_reset_control *reset_ctl,
			      struct amdgpu_reset_context *reset_context)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)reset_ctl->handle;
	int r;

	r = smu_v13_0_10_mode2_reset(adev);
	if (r) {
		dev_err(adev->dev,
			"ASIC reset failed with error, %d ", r);
	}
	return r;
}

static int smu_v13_0_10_mode2_restore_ip(struct amdgpu_device *adev)
{
	int i, r;
	struct psp_context *psp = &adev->psp;
	struct amdgpu_firmware_info *ucode;
	struct amdgpu_firmware_info *ucode_list[2];
	int ucode_count = 0;

	for (i = 0; i < adev->firmware.max_ucodes; i++) {
		ucode = &adev->firmware.ucode[i];

		switch (ucode->ucode_id) {
		case AMDGPU_UCODE_ID_IMU_I:
		case AMDGPU_UCODE_ID_IMU_D:
			ucode_list[ucode_count++] = ucode;
			break;
		default:
			break;
		}
	}

	r = psp_load_fw_list(psp, ucode_list, ucode_count);
	if (r) {
		dev_err(adev->dev, "IMU ucode load failed after mode2 reset\n");
		return r;
	}

	r = psp_rlc_autoload_start(psp);
	if (r) {
		DRM_ERROR("Failed to start rlc autoload after mode2 reset\n");
		return r;
	}

	amdgpu_dpm_enable_gfx_features(adev);

	for (i = 0; i < adev->num_ip_blocks; i++) {
		if (!(adev->ip_blocks[i].version->type ==
			      AMD_IP_BLOCK_TYPE_GFX ||
		      adev->ip_blocks[i].version->type ==
			      AMD_IP_BLOCK_TYPE_MES ||
		      adev->ip_blocks[i].version->type ==
			      AMD_IP_BLOCK_TYPE_SDMA))
			continue;
		r = adev->ip_blocks[i].version->funcs->resume(adev);
		if (r) {
			dev_err(adev->dev,
				"resume of IP block <%s> failed %d\n",
				adev->ip_blocks[i].version->funcs->name, r);
			return r;
		}

		adev->ip_blocks[i].status.hw = true;
	}

	for (i = 0; i < adev->num_ip_blocks; i++) {
		if (!(adev->ip_blocks[i].version->type ==
			      AMD_IP_BLOCK_TYPE_GFX ||
		      adev->ip_blocks[i].version->type ==
			      AMD_IP_BLOCK_TYPE_MES ||
		      adev->ip_blocks[i].version->type ==
			      AMD_IP_BLOCK_TYPE_SDMA))
			continue;

		if (adev->ip_blocks[i].version->funcs->late_init) {
			r = adev->ip_blocks[i].version->funcs->late_init(
				(void *)adev);
			if (r) {
				dev_err(adev->dev,
					"late_init of IP block <%s> failed %d after reset\n",
					adev->ip_blocks[i].version->funcs->name,
					r);
				return r;
			}
		}
		adev->ip_blocks[i].status.late_initialized = true;
	}

	amdgpu_device_set_cg_state(adev, AMD_CG_STATE_GATE);
	amdgpu_device_set_pg_state(adev, AMD_PG_STATE_GATE);

	return r;
}

static int
smu_v13_0_10_mode2_restore_hwcontext(struct amdgpu_reset_control *reset_ctl,
				  struct amdgpu_reset_context *reset_context)
{
	int r;
	struct amdgpu_device *tmp_adev = (struct amdgpu_device *)reset_ctl->handle;

	dev_info(tmp_adev->dev,
			"GPU reset succeeded, trying to resume\n");
	r = smu_v13_0_10_mode2_restore_ip(tmp_adev);
	if (r)
		goto end;

	amdgpu_register_gpu_instance(tmp_adev);

	/* Resume RAS */
	amdgpu_ras_resume(tmp_adev);

	amdgpu_irq_gpu_reset_resume_helper(tmp_adev);

	r = amdgpu_ib_ring_tests(tmp_adev);
	if (r) {
		dev_err(tmp_adev->dev,
			"ib ring test failed (%d).\n", r);
		r = -EAGAIN;
		goto end;
	}

end:
	if (r)
		return -EAGAIN;
	else
		return r;
}

static struct amdgpu_reset_handler smu_v13_0_10_mode2_handler = {
	.reset_method		= AMD_RESET_METHOD_MODE2,
	.prepare_env		= NULL,
	.prepare_hwcontext	= smu_v13_0_10_mode2_prepare_hwcontext,
	.perform_reset		= smu_v13_0_10_mode2_perform_reset,
	.restore_hwcontext	= smu_v13_0_10_mode2_restore_hwcontext,
	.restore_env		= NULL,
	.do_reset		= smu_v13_0_10_mode2_reset,
};

static struct amdgpu_reset_handler
	*smu_v13_0_10_rst_handlers[AMDGPU_RESET_MAX_HANDLERS] = {
		&smu_v13_0_10_mode2_handler,
	};

int smu_v13_0_10_reset_init(struct amdgpu_device *adev)
{
	struct amdgpu_reset_control *reset_ctl;

	reset_ctl = kzalloc(sizeof(*reset_ctl), GFP_KERNEL);
	if (!reset_ctl)
		return -ENOMEM;

	reset_ctl->handle = adev;
	reset_ctl->async_reset = smu_v13_0_10_async_reset;
	reset_ctl->active_reset = AMD_RESET_METHOD_NONE;
	reset_ctl->get_reset_handler = smu_v13_0_10_get_reset_handler;

	INIT_WORK(&reset_ctl->reset_work, reset_ctl->async_reset);
	/* Only mode2 is handled through reset control now */
	reset_ctl->reset_handlers = &smu_v13_0_10_rst_handlers;

	adev->reset_cntl = reset_ctl;

	return 0;
}

int smu_v13_0_10_reset_fini(struct amdgpu_device *adev)
{
	kfree(adev->reset_cntl);
	adev->reset_cntl = NULL;
	return 0;
}
