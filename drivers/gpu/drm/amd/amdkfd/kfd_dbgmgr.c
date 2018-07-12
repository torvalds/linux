/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/log2.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/device.h>

#include "kfd_priv.h"
#include "cik_regs.h"
#include "kfd_pm4_headers.h"
#include "kfd_pm4_headers_diq.h"
#include "kfd_dbgmgr.h"
#include "kfd_dbgdev.h"
#include "kfd_device_queue_manager.h"

static DEFINE_MUTEX(kfd_dbgmgr_mutex);

struct mutex *kfd_get_dbgmgr_mutex(void)
{
	return &kfd_dbgmgr_mutex;
}


static void kfd_dbgmgr_uninitialize(struct kfd_dbgmgr *pmgr)
{
	kfree(pmgr->dbgdev);

	pmgr->dbgdev = NULL;
	pmgr->pasid = 0;
	pmgr->dev = NULL;
}

void kfd_dbgmgr_destroy(struct kfd_dbgmgr *pmgr)
{
	if (pmgr) {
		kfd_dbgmgr_uninitialize(pmgr);
		kfree(pmgr);
	}
}

bool kfd_dbgmgr_create(struct kfd_dbgmgr **ppmgr, struct kfd_dev *pdev)
{
	enum DBGDEV_TYPE type = DBGDEV_TYPE_DIQ;
	struct kfd_dbgmgr *new_buff;

	if (WARN_ON(!pdev->init_complete))
		return false;

	new_buff = kfd_alloc_struct(new_buff);
	if (!new_buff) {
		pr_err("Failed to allocate dbgmgr instance\n");
		return false;
	}

	new_buff->pasid = 0;
	new_buff->dev = pdev;
	new_buff->dbgdev = kfd_alloc_struct(new_buff->dbgdev);
	if (!new_buff->dbgdev) {
		pr_err("Failed to allocate dbgdev instance\n");
		kfree(new_buff);
		return false;
	}

	/* get actual type of DBGDevice cpsch or not */
	if (pdev->dqm->sched_policy == KFD_SCHED_POLICY_NO_HWS)
		type = DBGDEV_TYPE_NODIQ;

	kfd_dbgdev_init(new_buff->dbgdev, pdev, type);
	*ppmgr = new_buff;

	return true;
}

long kfd_dbgmgr_register(struct kfd_dbgmgr *pmgr, struct kfd_process *p)
{
	if (pmgr->pasid != 0) {
		pr_debug("H/W debugger is already active using pasid %d\n",
				pmgr->pasid);
		return -EBUSY;
	}

	/* remember pasid */
	pmgr->pasid = p->pasid;

	/* provide the pqm for diq generation */
	pmgr->dbgdev->pqm = &p->pqm;

	/* activate the actual registering */
	pmgr->dbgdev->dbgdev_register(pmgr->dbgdev);

	return 0;
}

long kfd_dbgmgr_unregister(struct kfd_dbgmgr *pmgr, struct kfd_process *p)
{
	/* Is the requests coming from the already registered process? */
	if (pmgr->pasid != p->pasid) {
		pr_debug("H/W debugger is not registered by calling pasid %d\n",
				p->pasid);
		return -EINVAL;
	}

	pmgr->dbgdev->dbgdev_unregister(pmgr->dbgdev);

	pmgr->pasid = 0;

	return 0;
}

long kfd_dbgmgr_wave_control(struct kfd_dbgmgr *pmgr,
				struct dbg_wave_control_info *wac_info)
{
	/* Is the requests coming from the already registered process? */
	if (pmgr->pasid != wac_info->process->pasid) {
		pr_debug("H/W debugger support was not registered for requester pasid %d\n",
				wac_info->process->pasid);
		return -EINVAL;
	}

	return (long) pmgr->dbgdev->dbgdev_wave_control(pmgr->dbgdev, wac_info);
}

long kfd_dbgmgr_address_watch(struct kfd_dbgmgr *pmgr,
				struct dbg_address_watch_info *adw_info)
{
	/* Is the requests coming from the already registered process? */
	if (pmgr->pasid != adw_info->process->pasid) {
		pr_debug("H/W debugger support was not registered for requester pasid %d\n",
				adw_info->process->pasid);
		return -EINVAL;
	}

	return (long) pmgr->dbgdev->dbgdev_address_watch(pmgr->dbgdev,
							adw_info);
}

