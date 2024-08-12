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

#include "sienna_cichlid.h"
#include "amdgpu_reset.h"
#include "amdgpu_amdkfd.h"
#include "amdgpu_dpm.h"
#include "amdgpu_job.h"
#include "amdgpu_ring.h"
#include "amdgpu_ras.h"
#include "amdgpu_psp.h"
#include "amdgpu_xgmi.h"

static bool sienna_cichlid_is_mode2_default(struct amdgpu_reset_control *reset_ctl)
{
#if 0
	struct amdgpu_device *adev = (struct amdgpu_device *)reset_ctl->handle;

	if (amdgpu_ip_version(adev, MP1_HWIP, 0) == IP_VERSION(11, 0, 7) &&
	    adev->pm.fw_version >= 0x3a5500 && !amdgpu_sriov_vf(adev))
		return true;
#endif
	return amdgpu_reset_method == AMD_RESET_METHOD_MODE2;
}

static struct amdgpu_reset_handler *
sienna_cichlid_get_reset_handler(struct amdgpu_reset_control *reset_ctl,
			    struct amdgpu_reset_context *reset_context)
{
	struct amdgpu_reset_handler *handler;
	int i;

	if (reset_context->method != AMD_RESET_METHOD_NONE) {
		for_each_handler(i, handler, reset_ctl)	{
			if (handler->reset_method == reset_context->method)
				return handler;
		}
	}

	if (sienna_cichlid_is_mode2_default(reset_ctl)) {
		for_each_handler(i, handler, reset_ctl)	{
			if (handler->reset_method == AMD_RESET_METHOD_MODE2)
				return handler;
		}
	}

	return NULL;
}

static int sienna_cichlid_mode2_suspend_ip(struct amdgpu_device *adev)
{
	int r, i;

	amdgpu_device_set_pg_state(adev, AMD_PG_STATE_UNGATE);
	amdgpu_device_set_cg_state(adev, AMD_CG_STATE_UNGATE);

	for (i = adev->num_ip_blocks - 1; i >= 0; i--) {
		if (!(adev->ip_blocks[i].version->type ==
			      AMD_IP_BLOCK_TYPE_GFX ||
		      adev->ip_blocks[i].version->type ==
			      AMD_IP_BLOCK_TYPE_SDMA))
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

	return 0;
}

static int
sienna_cichlid_mode2_prepare_hwcontext(struct amdgpu_reset_control *reset_ctl,
				  struct amdgpu_reset_context *reset_context)
{
	int r = 0;
	struct amdgpu_device *adev = (struct amdgpu_device *)reset_ctl->handle;

	if (!amdgpu_sriov_vf(adev)) {
		if (adev->gfxhub.funcs->mode2_save_regs)
			adev->gfxhub.funcs->mode2_save_regs(adev);
		if (adev->gfxhub.funcs->halt)
			adev->gfxhub.funcs->halt(adev);
		r = sienna_cichlid_mode2_suspend_ip(adev);
	}

	return r;
}

static void sienna_cichlid_async_reset(struct work_struct *work)
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

static int sienna_cichlid_mode2_reset(struct amdgpu_device *adev)
{
	/* disable BM */
	pci_clear_master(adev->pdev);
	return amdgpu_dpm_mode2_reset(adev);
}

static int
sienna_cichlid_mode2_perform_reset(struct amdgpu_reset_control *reset_ctl,
			      struct amdgpu_reset_context *reset_context)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)reset_ctl->handle;
	int r;

	r = sienna_cichlid_mode2_reset(adev);
	if (r) {
		dev_err(adev->dev,
			"ASIC reset failed with error, %d ", r);
	}
	return r;
}

static int sienna_cichlid_mode2_restore_ip(struct amdgpu_device *adev)
{
	int i, r;
	struct psp_context *psp = &adev->psp;

	r = psp_rlc_autoload_start(psp);
	if (r) {
		dev_err(adev->dev, "Failed to start rlc autoload\n");
		return r;
	}

	/* Reinit GFXHUB */
	if (adev->gfxhub.funcs->mode2_restore_regs)
		adev->gfxhub.funcs->mode2_restore_regs(adev);
	adev->gfxhub.funcs->init(adev);
	r = adev->gfxhub.funcs->gart_enable(adev);
	if (r) {
		dev_err(adev->dev, "GFXHUB gart reenable failed after reset\n");
		return r;
	}

	for (i = 0; i < adev->num_ip_blocks; i++) {
		if (adev->ip_blocks[i].version->type == AMD_IP_BLOCK_TYPE_IH) {
			r = adev->ip_blocks[i].version->funcs->resume(adev);
			if (r) {
				dev_err(adev->dev,
					"resume of IP block <%s> failed %d\n",
					adev->ip_blocks[i].version->funcs->name, r);
				return r;
			}

			adev->ip_blocks[i].status.hw = true;
		}
	}

	for (i = 0; i < adev->num_ip_blocks; i++) {
		if (!(adev->ip_blocks[i].version->type ==
			      AMD_IP_BLOCK_TYPE_GFX ||
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
sienna_cichlid_mode2_restore_hwcontext(struct amdgpu_reset_control *reset_ctl,
				  struct amdgpu_reset_context *reset_context)
{
	int r;
	struct amdgpu_device *tmp_adev = (struct amdgpu_device *)reset_ctl->handle;

	dev_info(tmp_adev->dev,
			"GPU reset succeeded, trying to resume\n");
	r = sienna_cichlid_mode2_restore_ip(tmp_adev);
	if (r)
		goto end;

	/*
	* Add this ASIC as tracked as reset was already
	* complete successfully.
	*/
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

static struct amdgpu_reset_handler sienna_cichlid_mode2_handler = {
	.reset_method		= AMD_RESET_METHOD_MODE2,
	.prepare_env		= NULL,
	.prepare_hwcontext	= sienna_cichlid_mode2_prepare_hwcontext,
	.perform_reset		= sienna_cichlid_mode2_perform_reset,
	.restore_hwcontext	= sienna_cichlid_mode2_restore_hwcontext,
	.restore_env		= NULL,
	.do_reset		= sienna_cichlid_mode2_reset,
};

static struct amdgpu_reset_handler
	*sienna_cichlid_rst_handlers[AMDGPU_RESET_MAX_HANDLERS] = {
		&sienna_cichlid_mode2_handler,
	};

int sienna_cichlid_reset_init(struct amdgpu_device *adev)
{
	struct amdgpu_reset_control *reset_ctl;

	reset_ctl = kzalloc(sizeof(*reset_ctl), GFP_KERNEL);
	if (!reset_ctl)
		return -ENOMEM;

	reset_ctl->handle = adev;
	reset_ctl->async_reset = sienna_cichlid_async_reset;
	reset_ctl->active_reset = AMD_RESET_METHOD_NONE;
	reset_ctl->get_reset_handler = sienna_cichlid_get_reset_handler;

	INIT_WORK(&reset_ctl->reset_work, reset_ctl->async_reset);
	/* Only mode2 is handled through reset control now */
	reset_ctl->reset_handlers = &sienna_cichlid_rst_handlers;
	adev->reset_cntl = reset_ctl;

	return 0;
}

int sienna_cichlid_reset_fini(struct amdgpu_device *adev)
{
	kfree(adev->reset_cntl);
	adev->reset_cntl = NULL;
	return 0;
}
