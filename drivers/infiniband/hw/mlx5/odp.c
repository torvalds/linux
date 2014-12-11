/*
 * Copyright (c) 2014 Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "mlx5_ib.h"

struct workqueue_struct *mlx5_ib_page_fault_wq;

#define COPY_ODP_BIT_MLX_TO_IB(reg, ib_caps, field_name, bit_name) do {	\
	if (be32_to_cpu(reg.field_name) & MLX5_ODP_SUPPORT_##bit_name)	\
		ib_caps->field_name |= IB_ODP_SUPPORT_##bit_name;	\
} while (0)

int mlx5_ib_internal_query_odp_caps(struct mlx5_ib_dev *dev)
{
	int err;
	struct mlx5_odp_caps hw_caps;
	struct ib_odp_caps *caps = &dev->odp_caps;

	memset(caps, 0, sizeof(*caps));

	if (!(dev->mdev->caps.gen.flags & MLX5_DEV_CAP_FLAG_ON_DMND_PG))
		return 0;

	err = mlx5_query_odp_caps(dev->mdev, &hw_caps);
	if (err)
		goto out;

	/* At this point we would copy the capability bits that the driver
	 * supports from the hw_caps struct to the caps struct. However, no
	 * such capabilities are supported so far. */
out:
	return err;
}

static struct mlx5_ib_mr *mlx5_ib_odp_find_mr_lkey(struct mlx5_ib_dev *dev,
						   u32 key)
{
	u32 base_key = mlx5_base_mkey(key);
	struct mlx5_core_mr *mmr = __mlx5_mr_lookup(dev->mdev, base_key);

	if (!mmr || mmr->key != key)
		return NULL;

	return container_of(mmr, struct mlx5_ib_mr, mmr);
}

static void mlx5_ib_page_fault_resume(struct mlx5_ib_qp *qp,
				      struct mlx5_ib_pfault *pfault,
				      int error) {
	struct mlx5_ib_dev *dev = to_mdev(qp->ibqp.pd->device);
	int ret = mlx5_core_page_fault_resume(dev->mdev, qp->mqp.qpn,
					      pfault->mpfault.flags,
					      error);
	if (ret)
		pr_err("Failed to resolve the page fault on QP 0x%x\n",
		       qp->mqp.qpn);
}

void mlx5_ib_mr_pfault_handler(struct mlx5_ib_qp *qp,
			       struct mlx5_ib_pfault *pfault)
{
	u8 event_subtype = pfault->mpfault.event_subtype;

	switch (event_subtype) {
	default:
		pr_warn("Invalid page fault event subtype: 0x%x\n",
			event_subtype);
		mlx5_ib_page_fault_resume(qp, pfault, 1);
		break;
	}
}

static void mlx5_ib_qp_pfault_action(struct work_struct *work)
{
	struct mlx5_ib_pfault *pfault = container_of(work,
						     struct mlx5_ib_pfault,
						     work);
	enum mlx5_ib_pagefault_context context =
		mlx5_ib_get_pagefault_context(&pfault->mpfault);
	struct mlx5_ib_qp *qp = container_of(pfault, struct mlx5_ib_qp,
					     pagefaults[context]);
	mlx5_ib_mr_pfault_handler(qp, pfault);
}

void mlx5_ib_qp_disable_pagefaults(struct mlx5_ib_qp *qp)
{
	unsigned long flags;

	spin_lock_irqsave(&qp->disable_page_faults_lock, flags);
	qp->disable_page_faults = 1;
	spin_unlock_irqrestore(&qp->disable_page_faults_lock, flags);

	/*
	 * Note that at this point, we are guarenteed that no more
	 * work queue elements will be posted to the work queue with
	 * the QP we are closing.
	 */
	flush_workqueue(mlx5_ib_page_fault_wq);
}

void mlx5_ib_qp_enable_pagefaults(struct mlx5_ib_qp *qp)
{
	unsigned long flags;

	spin_lock_irqsave(&qp->disable_page_faults_lock, flags);
	qp->disable_page_faults = 0;
	spin_unlock_irqrestore(&qp->disable_page_faults_lock, flags);
}

static void mlx5_ib_pfault_handler(struct mlx5_core_qp *qp,
				   struct mlx5_pagefault *pfault)
{
	/*
	 * Note that we will only get one fault event per QP per context
	 * (responder/initiator, read/write), until we resolve the page fault
	 * with the mlx5_ib_page_fault_resume command. Since this function is
	 * called from within the work element, there is no risk of missing
	 * events.
	 */
	struct mlx5_ib_qp *mibqp = to_mibqp(qp);
	enum mlx5_ib_pagefault_context context =
		mlx5_ib_get_pagefault_context(pfault);
	struct mlx5_ib_pfault *qp_pfault = &mibqp->pagefaults[context];

	qp_pfault->mpfault = *pfault;

	/* No need to stop interrupts here since we are in an interrupt */
	spin_lock(&mibqp->disable_page_faults_lock);
	if (!mibqp->disable_page_faults)
		queue_work(mlx5_ib_page_fault_wq, &qp_pfault->work);
	spin_unlock(&mibqp->disable_page_faults_lock);
}

void mlx5_ib_odp_create_qp(struct mlx5_ib_qp *qp)
{
	int i;

	qp->disable_page_faults = 1;
	spin_lock_init(&qp->disable_page_faults_lock);

	qp->mqp.pfault_handler	= mlx5_ib_pfault_handler;

	for (i = 0; i < MLX5_IB_PAGEFAULT_CONTEXTS; ++i)
		INIT_WORK(&qp->pagefaults[i].work, mlx5_ib_qp_pfault_action);
}

int mlx5_ib_odp_init_one(struct mlx5_ib_dev *ibdev)
{
	int ret;

	ret = init_srcu_struct(&ibdev->mr_srcu);
	if (ret)
		return ret;

	return 0;
}

void mlx5_ib_odp_remove_one(struct mlx5_ib_dev *ibdev)
{
	cleanup_srcu_struct(&ibdev->mr_srcu);
}

int __init mlx5_ib_odp_init(void)
{
	mlx5_ib_page_fault_wq =
		create_singlethread_workqueue("mlx5_ib_page_faults");
	if (!mlx5_ib_page_fault_wq)
		return -ENOMEM;

	return 0;
}

void mlx5_ib_odp_cleanup(void)
{
	destroy_workqueue(mlx5_ib_page_fault_wq);
}
