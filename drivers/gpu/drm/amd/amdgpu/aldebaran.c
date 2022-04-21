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

#include "aldebaran.h"
#include "amdgpu_reset.h"
#include "amdgpu_amdkfd.h"
#include "amdgpu_dpm.h"
#include "amdgpu_job.h"
#include "amdgpu_ring.h"
#include "amdgpu_ras.h"
#include "amdgpu_psp.h"
#include "amdgpu_xgmi.h"

static bool aldebaran_is_mode2_default(struct amdgpu_reset_control *reset_ctl)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)reset_ctl->handle;

	if ((adev->ip_versions[MP1_HWIP][0] == IP_VERSION(13, 0, 2) &&
	     adev->gmc.xgmi.connected_to_cpu))
		return true;

	return false;
}

static struct amdgpu_reset_handler *
aldebaran_get_reset_handler(struct amdgpu_reset_control *reset_ctl,
			    struct amdgpu_reset_context *reset_context)
{
	struct amdgpu_reset_handler *handler;
	struct amdgpu_device *adev = (struct amdgpu_device *)reset_ctl->handle;

	if (reset_context->method != AMD_RESET_METHOD_NONE) {
		dev_dbg(adev->dev, "Getting reset handler for method %d\n",
			reset_context->method);
		list_for_each_entry(handler, &reset_ctl->reset_handlers,
				     handler_list) {
			if (handler->reset_method == reset_context->method)
				return handler;
		}
	}

	if (aldebaran_is_mode2_default(reset_ctl)) {
		list_for_each_entry(handler, &reset_ctl->reset_handlers,
				     handler_list) {
			if (handler->reset_method == AMD_RESET_METHOD_MODE2) {
				reset_context->method = AMD_RESET_METHOD_MODE2;
				return handler;
			}
		}
	}

	dev_dbg(adev->dev, "Reset handler not found!\n");

	return NULL;
}

static int aldebaran_mode2_suspend_ip(struct amdgpu_device *adev)
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

	return r;
}

static int
aldebaran_mode2_prepare_hwcontext(struct amdgpu_reset_control *reset_ctl,
				  struct amdgpu_reset_context *reset_context)
{
	int r = 0;
	struct amdgpu_device *adev = (struct amdgpu_device *)reset_ctl->handle;

	dev_dbg(adev->dev, "Aldebaran prepare hw context\n");
	/* Don't suspend on bare metal if we are not going to HW reset the ASIC */
	if (!amdgpu_sriov_vf(adev))
		r = aldebaran_mode2_suspend_ip(adev);

	return r;
}

static void aldebaran_async_reset(struct work_struct *work)
{
	struct amdgpu_reset_handler *handler;
	struct amdgpu_reset_control *reset_ctl =
		container_of(work, struct amdgpu_reset_control, reset_work);
	struct amdgpu_device *adev = (struct amdgpu_device *)reset_ctl->handle;

	list_for_each_entry(handler, &reset_ctl->reset_handlers,
			     handler_list) {
		if (handler->reset_method == reset_ctl->active_reset) {
			dev_dbg(adev->dev, "Resetting device\n");
			handler->do_reset(adev);
			break;
		}
	}
}

static int aldebaran_mode2_reset(struct amdgpu_device *adev)
{
	/* disable BM */
	pci_clear_master(adev->pdev);
	adev->asic_reset_res = amdgpu_dpm_mode2_reset(adev);
	return adev->asic_reset_res;
}

static int
aldebaran_mode2_perform_reset(struct amdgpu_reset_control *reset_ctl,
			      struct amdgpu_reset_context *reset_context)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)reset_ctl->handle;
	struct amdgpu_device *tmp_adev = NULL;
	struct list_head reset_device_list;
	int r = 0;

	dev_dbg(adev->dev, "aldebaran perform hw reset\n");
	if (adev->ip_versions[MP1_HWIP][0] == IP_VERSION(13, 0, 2) &&
	    reset_context->hive == NULL) {
		/* Wrong context, return error */
		return -EINVAL;
	}

	INIT_LIST_HEAD(&reset_device_list);
	if (reset_context->hive) {
		list_for_each_entry (tmp_adev,
				     &reset_context->hive->device_list,
				     gmc.xgmi.head)
			list_add_tail(&tmp_adev->reset_list,
				      &reset_device_list);
	} else {
		list_add_tail(&reset_context->reset_req_dev->reset_list,
			      &reset_device_list);
	}

	list_for_each_entry (tmp_adev, &reset_device_list, reset_list) {
		mutex_lock(&tmp_adev->reset_cntl->reset_lock);
		tmp_adev->reset_cntl->active_reset = AMD_RESET_METHOD_MODE2;
	}
	/*
	 * Mode2 reset doesn't need any sync between nodes in XGMI hive, instead launch
	 * them together so that they can be completed asynchronously on multiple nodes
	 */
	list_for_each_entry (tmp_adev, &reset_device_list, reset_list) {
		/* For XGMI run all resets in parallel to speed up the process */
		if (tmp_adev->gmc.xgmi.num_physical_nodes > 1) {
			if (!queue_work(system_unbound_wq,
					&tmp_adev->reset_cntl->reset_work))
				r = -EALREADY;
		} else
			r = aldebaran_mode2_reset(tmp_adev);
		if (r) {
			dev_err(tmp_adev->dev,
				"ASIC reset failed with error, %d for drm dev, %s",
				r, adev_to_drm(tmp_adev)->unique);
			break;
		}
	}

	/* For XGMI wait for all resets to complete before proceed */
	if (!r) {
		list_for_each_entry (tmp_adev, &reset_device_list, reset_list) {
			if (tmp_adev->gmc.xgmi.num_physical_nodes > 1) {
				flush_work(&tmp_adev->reset_cntl->reset_work);
				r = tmp_adev->asic_reset_res;
				if (r)
					break;
			}
		}
	}

	list_for_each_entry (tmp_adev, &reset_device_list, reset_list) {
		mutex_unlock(&tmp_adev->reset_cntl->reset_lock);
		tmp_adev->reset_cntl->active_reset = AMD_RESET_METHOD_NONE;
	}

	return r;
}

static int aldebaran_mode2_restore_ip(struct amdgpu_device *adev)
{
	struct amdgpu_firmware_info *ucode_list[AMDGPU_UCODE_ID_MAXIMUM];
	struct amdgpu_firmware_info *ucode;
	struct amdgpu_ip_block *cmn_block;
	int ucode_count = 0;
	int i, r;

	dev_dbg(adev->dev, "Reloading ucodes after reset\n");
	for (i = 0; i < adev->firmware.max_ucodes; i++) {
		ucode = &adev->firmware.ucode[i];
		if (!ucode->fw)
			continue;
		switch (ucode->ucode_id) {
		case AMDGPU_UCODE_ID_SDMA0:
		case AMDGPU_UCODE_ID_SDMA1:
		case AMDGPU_UCODE_ID_SDMA2:
		case AMDGPU_UCODE_ID_SDMA3:
		case AMDGPU_UCODE_ID_SDMA4:
		case AMDGPU_UCODE_ID_SDMA5:
		case AMDGPU_UCODE_ID_SDMA6:
		case AMDGPU_UCODE_ID_SDMA7:
		case AMDGPU_UCODE_ID_CP_MEC1:
		case AMDGPU_UCODE_ID_CP_MEC1_JT:
		case AMDGPU_UCODE_ID_RLC_RESTORE_LIST_CNTL:
		case AMDGPU_UCODE_ID_RLC_RESTORE_LIST_GPM_MEM:
		case AMDGPU_UCODE_ID_RLC_RESTORE_LIST_SRM_MEM:
		case AMDGPU_UCODE_ID_RLC_G:
			ucode_list[ucode_count++] = ucode;
			break;
		default:
			break;
		}
	}

	/* Reinit NBIF block */
	cmn_block =
		amdgpu_device_ip_get_ip_block(adev, AMD_IP_BLOCK_TYPE_COMMON);
	if (unlikely(!cmn_block)) {
		dev_err(adev->dev, "Failed to get BIF handle\n");
		return -EINVAL;
	}
	r = cmn_block->version->funcs->resume(adev);
	if (r)
		return r;

	/* Reinit GFXHUB */
	adev->gfxhub.funcs->init(adev);
	r = adev->gfxhub.funcs->gart_enable(adev);
	if (r) {
		dev_err(adev->dev, "GFXHUB gart reenable failed after reset\n");
		return r;
	}

	/* Reload GFX firmware */
	r = psp_load_fw_list(&adev->psp, ucode_list, ucode_count);
	if (r) {
		dev_err(adev->dev, "GFX ucode load failed after reset\n");
		return r;
	}

	/* Resume RLC, FW needs RLC alive to complete reset process */
	adev->gfx.rlc.funcs->resume(adev);

	/* Wait for FW reset event complete */
	r = amdgpu_dpm_wait_for_event(adev, SMU_EVENT_RESET_COMPLETE, 0);
	if (r) {
		dev_err(adev->dev,
			"Failed to get response from firmware after reset\n");
		return r;
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
			      AMD_IP_BLOCK_TYPE_SDMA ||
		      adev->ip_blocks[i].version->type ==
			      AMD_IP_BLOCK_TYPE_COMMON))
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

	amdgpu_ras_set_error_query_ready(adev, true);

	amdgpu_device_set_cg_state(adev, AMD_CG_STATE_GATE);
	amdgpu_device_set_pg_state(adev, AMD_PG_STATE_GATE);

	return r;
}

static int
aldebaran_mode2_restore_hwcontext(struct amdgpu_reset_control *reset_ctl,
				  struct amdgpu_reset_context *reset_context)
{
	struct amdgpu_device *tmp_adev = NULL;
	struct list_head reset_device_list;
	int r;

	if (reset_context->reset_req_dev->ip_versions[MP1_HWIP][0] ==
		    IP_VERSION(13, 0, 2) &&
	    reset_context->hive == NULL) {
		/* Wrong context, return error */
		return -EINVAL;
	}

	INIT_LIST_HEAD(&reset_device_list);
	if (reset_context->hive) {
		list_for_each_entry (tmp_adev,
				     &reset_context->hive->device_list,
				     gmc.xgmi.head)
			list_add_tail(&tmp_adev->reset_list,
				      &reset_device_list);
	} else {
		list_add_tail(&reset_context->reset_req_dev->reset_list,
			      &reset_device_list);
	}

	list_for_each_entry (tmp_adev, &reset_device_list, reset_list) {
		dev_info(tmp_adev->dev,
			 "GPU reset succeeded, trying to resume\n");
		r = aldebaran_mode2_restore_ip(tmp_adev);
		if (r)
			goto end;

		/*
		 * Add this ASIC as tracked as reset was already
		 * complete successfully.
		 */
		amdgpu_register_gpu_instance(tmp_adev);

		/* Resume RAS */
		amdgpu_ras_resume(tmp_adev);

		/* Update PSP FW topology after reset */
		if (reset_context->hive &&
		    tmp_adev->gmc.xgmi.num_physical_nodes > 1)
			r = amdgpu_xgmi_update_topology(reset_context->hive,
							tmp_adev);

		if (!r) {
			amdgpu_irq_gpu_reset_resume_helper(tmp_adev);

			r = amdgpu_ib_ring_tests(tmp_adev);
			if (r) {
				dev_err(tmp_adev->dev,
					"ib ring test failed (%d).\n", r);
				r = -EAGAIN;
				tmp_adev->asic_reset_res = r;
				goto end;
			}
		}
	}

end:
	return r;
}

static struct amdgpu_reset_handler aldebaran_mode2_handler = {
	.reset_method		= AMD_RESET_METHOD_MODE2,
	.prepare_env		= NULL,
	.prepare_hwcontext	= aldebaran_mode2_prepare_hwcontext,
	.perform_reset		= aldebaran_mode2_perform_reset,
	.restore_hwcontext	= aldebaran_mode2_restore_hwcontext,
	.restore_env		= NULL,
	.do_reset		= aldebaran_mode2_reset,
};

int aldebaran_reset_init(struct amdgpu_device *adev)
{
	struct amdgpu_reset_control *reset_ctl;

	reset_ctl = kzalloc(sizeof(*reset_ctl), GFP_KERNEL);
	if (!reset_ctl)
		return -ENOMEM;

	reset_ctl->handle = adev;
	reset_ctl->async_reset = aldebaran_async_reset;
	reset_ctl->active_reset = AMD_RESET_METHOD_NONE;
	reset_ctl->get_reset_handler = aldebaran_get_reset_handler;

	INIT_LIST_HEAD(&reset_ctl->reset_handlers);
	INIT_WORK(&reset_ctl->reset_work, reset_ctl->async_reset);
	/* Only mode2 is handled through reset control now */
	amdgpu_reset_add_handler(reset_ctl, &aldebaran_mode2_handler);

	adev->reset_cntl = reset_ctl;

	return 0;
}

int aldebaran_reset_fini(struct amdgpu_device *adev)
{
	kfree(adev->reset_cntl);
	adev->reset_cntl = NULL;
	return 0;
}
