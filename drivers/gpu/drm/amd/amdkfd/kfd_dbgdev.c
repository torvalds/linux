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
#include <linux/mutex.h>
#include <linux/device.h>

#include "kfd_pm4_headers.h"
#include "kfd_pm4_headers_diq.h"
#include "kfd_kernel_queue.h"
#include "kfd_priv.h"
#include "kfd_pm4_opcodes.h"
#include "cik_regs.h"
#include "kfd_dbgmgr.h"
#include "kfd_dbgdev.h"
#include "kfd_device_queue_manager.h"
#include "../../radeon/cik_reg.h"

static void dbgdev_address_watch_disable_nodiq(struct kfd_dev *dev)
{
	BUG_ON(!dev || !dev->kfd2kgd);

	dev->kfd2kgd->address_watch_disable(dev->kgd);
}

static int dbgdev_register_nodiq(struct kfd_dbgdev *dbgdev)
{
	BUG_ON(!dbgdev);

	/*
	 * no action is needed in this case,
	 * just make sure diq will not be used
	 */

	dbgdev->kq = NULL;

	return 0;
}

static int dbgdev_register_diq(struct kfd_dbgdev *dbgdev)
{
	struct queue_properties properties;
	unsigned int qid;
	struct kernel_queue *kq = NULL;
	int status;

	BUG_ON(!dbgdev || !dbgdev->pqm || !dbgdev->dev);

	status = pqm_create_queue(dbgdev->pqm, dbgdev->dev, NULL,
				&properties, 0, KFD_QUEUE_TYPE_DIQ,
				&qid);

	if (status) {
		pr_err("amdkfd: Failed to create DIQ\n");
		return status;
	}

	pr_debug("DIQ Created with queue id: %d\n", qid);

	kq = pqm_get_kernel_queue(dbgdev->pqm, qid);

	if (kq == NULL) {
		pr_err("amdkfd: Error getting DIQ\n");
		pqm_destroy_queue(dbgdev->pqm, qid);
		return -EFAULT;
	}

	dbgdev->kq = kq;

	return status;
}

static int dbgdev_unregister_nodiq(struct kfd_dbgdev *dbgdev)
{
	BUG_ON(!dbgdev || !dbgdev->dev);

	/* disable watch address */
	dbgdev_address_watch_disable_nodiq(dbgdev->dev);
	return 0;
}

static int dbgdev_unregister_diq(struct kfd_dbgdev *dbgdev)
{
	/* todo - disable address watch */
	int status;

	BUG_ON(!dbgdev || !dbgdev->pqm || !dbgdev->kq);

	status = pqm_destroy_queue(dbgdev->pqm,
			dbgdev->kq->queue->properties.queue_id);
	dbgdev->kq = NULL;

	return status;
}

void kfd_dbgdev_init(struct kfd_dbgdev *pdbgdev, struct kfd_dev *pdev,
			enum DBGDEV_TYPE type)
{
	BUG_ON(!pdbgdev || !pdev);

	pdbgdev->dev = pdev;
	pdbgdev->kq = NULL;
	pdbgdev->type = type;
	pdbgdev->pqm = NULL;

	switch (type) {
	case DBGDEV_TYPE_NODIQ:
		pdbgdev->dbgdev_register = dbgdev_register_nodiq;
		pdbgdev->dbgdev_unregister = dbgdev_unregister_nodiq;
		break;
	case DBGDEV_TYPE_DIQ:
	default:
		pdbgdev->dbgdev_register = dbgdev_register_diq;
		pdbgdev->dbgdev_unregister = dbgdev_unregister_diq;
		break;
	}

}
