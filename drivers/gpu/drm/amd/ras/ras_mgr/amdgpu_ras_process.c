// SPDX-License-Identifier: MIT
/*
 * Copyright (c) 2025 Advanced Micro Devices, Inc. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "amdgpu.h"
#include "amdgpu_reset.h"
#include "amdgpu_xgmi.h"
#include "ras_sys.h"
#include "amdgpu_ras_mgr.h"
#include "amdgpu_ras_process.h"

#define RAS_MGR_RETIRE_PAGE_INTERVAL  100
#define RAS_EVENT_PROCESS_TIMEOUT  1200

static void ras_process_retire_page_dwork(struct work_struct *work)
{
	struct amdgpu_ras_mgr *ras_mgr =
		container_of(work, struct amdgpu_ras_mgr, retire_page_dwork.work);
	struct amdgpu_device *adev = ras_mgr->adev;
	int ret;

	if (amdgpu_ras_is_rma(adev))
		return;

	/* If gpu reset is ongoing, delay retiring the bad pages */
	if (amdgpu_in_reset(adev) || amdgpu_ras_in_recovery(adev)) {
		schedule_delayed_work(&ras_mgr->retire_page_dwork,
			msecs_to_jiffies(RAS_MGR_RETIRE_PAGE_INTERVAL * 3));
		return;
	}

	ret = ras_umc_handle_bad_pages(ras_mgr->ras_core, NULL);
	if (!ret)
		schedule_delayed_work(&ras_mgr->retire_page_dwork,
			msecs_to_jiffies(RAS_MGR_RETIRE_PAGE_INTERVAL));
}

int amdgpu_ras_process_init(struct amdgpu_device *adev)
{
	struct amdgpu_ras_mgr *ras_mgr = amdgpu_ras_mgr_get_context(adev);

	ras_mgr->is_paused = false;
	init_completion(&ras_mgr->ras_event_done);

	INIT_DELAYED_WORK(&ras_mgr->retire_page_dwork, ras_process_retire_page_dwork);

	return 0;
}

int amdgpu_ras_process_fini(struct amdgpu_device *adev)
{
	struct amdgpu_ras_mgr *ras_mgr = amdgpu_ras_mgr_get_context(adev);

	ras_mgr->is_paused = false;
	/* Save all cached bad pages to eeprom */
	flush_delayed_work(&ras_mgr->retire_page_dwork);
	cancel_delayed_work_sync(&ras_mgr->retire_page_dwork);
	return 0;
}

int amdgpu_ras_process_handle_umc_interrupt(struct amdgpu_device *adev, void *data)
{
	struct amdgpu_ras_mgr *ras_mgr = amdgpu_ras_mgr_get_context(adev);

	if (!ras_mgr->ras_core)
		return -EINVAL;

	return ras_process_add_interrupt_req(ras_mgr->ras_core, NULL, true);
}

int amdgpu_ras_process_handle_unexpected_interrupt(struct amdgpu_device *adev, void *data)
{
	amdgpu_ras_set_fed(adev, true);
	return amdgpu_ras_mgr_reset_gpu(adev, AMDGPU_RAS_GPU_RESET_MODE1_RESET);
}

int amdgpu_ras_process_handle_consumption_interrupt(struct amdgpu_device *adev, void *data)
{
	struct amdgpu_ras_mgr *ras_mgr = amdgpu_ras_mgr_get_context(adev);
	struct ras_ih_info *ih_info = (struct ras_ih_info *)data;
	struct ras_event_req req;
	uint64_t seqno;

	if (!ih_info)
		return -EINVAL;

	memset(&req, 0, sizeof(req));
	req.block = ih_info->block;
	req.data = ih_info->data;
	req.pasid = ih_info->pasid;
	req.pasid_fn = ih_info->pasid_fn;
	req.reset = ih_info->reset;

	seqno = ras_core_get_seqno(ras_mgr->ras_core,
				RAS_SEQNO_TYPE_POISON_CONSUMPTION, false);

	/* When the ACA register cannot be read from FW, the poison
	 * consumption seqno in the fifo will not pop up, so it is
	 * necessary to check whether the seqno is the previous seqno.
	 */
	if (seqno == ras_mgr->last_poison_consumption_seqno) {
		/* Pop and discard the previous seqno */
		ras_core_get_seqno(ras_mgr->ras_core,
				RAS_SEQNO_TYPE_POISON_CONSUMPTION, true);
		seqno = ras_core_get_seqno(ras_mgr->ras_core,
					RAS_SEQNO_TYPE_POISON_CONSUMPTION, false);
	}
	ras_mgr->last_poison_consumption_seqno = seqno;
	req.seqno = seqno;

	return ras_process_add_interrupt_req(ras_mgr->ras_core, &req, false);
}

int amdgpu_ras_process_begin(struct amdgpu_device *adev)
{
	struct amdgpu_ras_mgr *ras_mgr = amdgpu_ras_mgr_get_context(adev);

	if (ras_mgr->is_paused)
		return -EAGAIN;

	reinit_completion(&ras_mgr->ras_event_done);
	return 0;
}

int amdgpu_ras_process_end(struct amdgpu_device *adev)
{
	struct amdgpu_ras_mgr *ras_mgr = amdgpu_ras_mgr_get_context(adev);

	complete(&ras_mgr->ras_event_done);
	return 0;
}

int amdgpu_ras_process_pre_reset(struct amdgpu_device *adev)
{
	struct amdgpu_ras_mgr *ras_mgr = amdgpu_ras_mgr_get_context(adev);
	long rc;

	if (!ras_mgr || !ras_mgr->ras_core)
		return -EINVAL;

	if (!ras_mgr->ras_core->is_initialized)
		return -EPERM;

	ras_mgr->is_paused = true;

	/* Wait for RAS event processing to complete */
	rc = wait_for_completion_interruptible_timeout(&ras_mgr->ras_event_done,
			msecs_to_jiffies(RAS_EVENT_PROCESS_TIMEOUT));
	if (rc <= 0)
		RAS_DEV_WARN(adev, "Waiting for ras process to complete %s\n",
			 rc ? "interrupted" : "timeout");

	flush_delayed_work(&ras_mgr->retire_page_dwork);
	return 0;
}

int amdgpu_ras_process_post_reset(struct amdgpu_device *adev)
{
	struct amdgpu_ras_mgr *ras_mgr = amdgpu_ras_mgr_get_context(adev);

	if (!ras_mgr || !ras_mgr->ras_core)
		return -EINVAL;

	if (!ras_mgr->ras_core->is_initialized)
		return -EPERM;

	ras_mgr->is_paused = false;

	schedule_delayed_work(&ras_mgr->retire_page_dwork, 0);
	return 0;
}
