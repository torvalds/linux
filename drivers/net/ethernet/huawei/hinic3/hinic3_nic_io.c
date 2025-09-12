// SPDX-License-Identifier: GPL-2.0
// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

#include "hinic3_hw_comm.h"
#include "hinic3_hw_intf.h"
#include "hinic3_hwdev.h"
#include "hinic3_hwif.h"
#include "hinic3_nic_cfg.h"
#include "hinic3_nic_dev.h"
#include "hinic3_nic_io.h"

#define HINIC3_CI_Q_ADDR_SIZE                (64)

#define HINIC3_CI_TABLE_SIZE(num_qps)  \
	(ALIGN((num_qps) * HINIC3_CI_Q_ADDR_SIZE, HINIC3_MIN_PAGE_SIZE))

#define HINIC3_CI_VADDR(base_addr, q_id)  \
	((u8 *)(base_addr) + (q_id) * HINIC3_CI_Q_ADDR_SIZE)

int hinic3_init_nic_io(struct hinic3_nic_dev *nic_dev)
{
	struct hinic3_hwdev *hwdev = nic_dev->hwdev;
	struct hinic3_nic_io *nic_io;
	int err;

	nic_io = kzalloc(sizeof(*nic_io), GFP_KERNEL);
	if (!nic_io)
		return -ENOMEM;

	nic_dev->nic_io = nic_io;

	err = hinic3_set_func_svc_used_state(hwdev, COMM_FUNC_SVC_T_NIC, 1);
	if (err) {
		dev_err(hwdev->dev, "Failed to set function svc used state\n");
		goto err_free_nicio;
	}

	err = hinic3_init_function_table(nic_dev);
	if (err) {
		dev_err(hwdev->dev, "Failed to init function table\n");
		goto err_clear_func_svc_used_state;
	}

	nic_io->rx_buf_len = nic_dev->rx_buf_len;

	err = hinic3_get_nic_feature_from_hw(nic_dev);
	if (err) {
		dev_err(hwdev->dev, "Failed to get nic features\n");
		goto err_clear_func_svc_used_state;
	}

	nic_io->feature_cap &= HINIC3_NIC_F_ALL_MASK;
	nic_io->feature_cap &= HINIC3_NIC_DRV_DEFAULT_FEATURE;
	dev_dbg(hwdev->dev, "nic features: 0x%llx\n\n", nic_io->feature_cap);

	return 0;

err_clear_func_svc_used_state:
	hinic3_set_func_svc_used_state(hwdev, COMM_FUNC_SVC_T_NIC, 0);
err_free_nicio:
	nic_dev->nic_io = NULL;
	kfree(nic_io);

	return err;
}

void hinic3_free_nic_io(struct hinic3_nic_dev *nic_dev)
{
	struct hinic3_nic_io *nic_io = nic_dev->nic_io;

	hinic3_set_func_svc_used_state(nic_dev->hwdev, COMM_FUNC_SVC_T_NIC, 0);
	nic_dev->nic_io = NULL;
	kfree(nic_io);
}

int hinic3_init_nicio_res(struct hinic3_nic_dev *nic_dev)
{
	struct hinic3_nic_io *nic_io = nic_dev->nic_io;
	struct hinic3_hwdev *hwdev = nic_dev->hwdev;
	void __iomem *db_base;
	int err;

	nic_io->max_qps = hinic3_func_max_qnum(hwdev);

	err = hinic3_alloc_db_addr(hwdev, &db_base, NULL);
	if (err) {
		dev_err(hwdev->dev, "Failed to allocate doorbell for sqs\n");
		return err;
	}
	nic_io->sqs_db_addr = db_base;

	err = hinic3_alloc_db_addr(hwdev, &db_base, NULL);
	if (err) {
		hinic3_free_db_addr(hwdev, nic_io->sqs_db_addr);
		dev_err(hwdev->dev, "Failed to allocate doorbell for rqs\n");
		return err;
	}
	nic_io->rqs_db_addr = db_base;

	nic_io->ci_vaddr_base =
		dma_alloc_coherent(hwdev->dev,
				   HINIC3_CI_TABLE_SIZE(nic_io->max_qps),
				   &nic_io->ci_dma_base,
				   GFP_KERNEL);
	if (!nic_io->ci_vaddr_base) {
		hinic3_free_db_addr(hwdev, nic_io->sqs_db_addr);
		hinic3_free_db_addr(hwdev, nic_io->rqs_db_addr);
		return -ENOMEM;
	}

	return 0;
}

void hinic3_free_nicio_res(struct hinic3_nic_dev *nic_dev)
{
	struct hinic3_nic_io *nic_io = nic_dev->nic_io;
	struct hinic3_hwdev *hwdev = nic_dev->hwdev;

	dma_free_coherent(hwdev->dev,
			  HINIC3_CI_TABLE_SIZE(nic_io->max_qps),
			  nic_io->ci_vaddr_base, nic_io->ci_dma_base);

	hinic3_free_db_addr(hwdev, nic_io->sqs_db_addr);
	hinic3_free_db_addr(hwdev, nic_io->rqs_db_addr);
}

static int hinic3_create_sq(struct hinic3_hwdev *hwdev,
			    struct hinic3_io_queue *sq,
			    u16 q_id, u32 sq_depth, u16 sq_msix_idx)
{
	int err;

	/* sq used & hardware request init 1 */
	sq->owner = 1;

	sq->q_id = q_id;
	sq->msix_entry_idx = sq_msix_idx;

	err = hinic3_wq_create(hwdev, &sq->wq, sq_depth,
			       BIT(HINIC3_SQ_WQEBB_SHIFT));
	if (err) {
		dev_err(hwdev->dev, "Failed to create tx queue %u wq\n",
			q_id);
		return err;
	}

	return 0;
}

static int hinic3_create_rq(struct hinic3_hwdev *hwdev,
			    struct hinic3_io_queue *rq,
			    u16 q_id, u32 rq_depth, u16 rq_msix_idx)
{
	int err;

	rq->q_id = q_id;
	rq->msix_entry_idx = rq_msix_idx;

	err = hinic3_wq_create(hwdev, &rq->wq, rq_depth,
			       BIT(HINIC3_RQ_WQEBB_SHIFT +
				   HINIC3_NORMAL_RQ_WQE));
	if (err) {
		dev_err(hwdev->dev, "Failed to create rx queue %u wq\n",
			q_id);
		return err;
	}

	return 0;
}

static int hinic3_create_qp(struct hinic3_hwdev *hwdev,
			    struct hinic3_io_queue *sq,
			    struct hinic3_io_queue *rq, u16 q_id, u32 sq_depth,
			    u32 rq_depth, u16 qp_msix_idx)
{
	int err;

	err = hinic3_create_sq(hwdev, sq, q_id, sq_depth, qp_msix_idx);
	if (err) {
		dev_err(hwdev->dev, "Failed to create sq, qid: %u\n",
			q_id);
		return err;
	}

	err = hinic3_create_rq(hwdev, rq, q_id, rq_depth, qp_msix_idx);
	if (err) {
		dev_err(hwdev->dev, "Failed to create rq, qid: %u\n",
			q_id);
		goto err_destroy_sq_wq;
	}

	return 0;

err_destroy_sq_wq:
	hinic3_wq_destroy(hwdev, &sq->wq);

	return err;
}

static void hinic3_destroy_qp(struct hinic3_hwdev *hwdev,
			      struct hinic3_io_queue *sq,
			      struct hinic3_io_queue *rq)
{
	hinic3_wq_destroy(hwdev, &sq->wq);
	hinic3_wq_destroy(hwdev, &rq->wq);
}

int hinic3_alloc_qps(struct hinic3_nic_dev *nic_dev,
		     struct hinic3_dyna_qp_params *qp_params)
{
	struct msix_entry *qps_msix_entries = nic_dev->qps_msix_entries;
	struct hinic3_nic_io *nic_io = nic_dev->nic_io;
	struct hinic3_hwdev *hwdev = nic_dev->hwdev;
	struct hinic3_io_queue *sqs;
	struct hinic3_io_queue *rqs;
	u16 q_id;
	int err;

	if (qp_params->num_qps > nic_io->max_qps || !qp_params->num_qps)
		return -EINVAL;

	sqs = kcalloc(qp_params->num_qps, sizeof(*sqs), GFP_KERNEL);
	if (!sqs) {
		err = -ENOMEM;
		goto err_out;
	}

	rqs = kcalloc(qp_params->num_qps, sizeof(*rqs), GFP_KERNEL);
	if (!rqs) {
		err = -ENOMEM;
		goto err_free_sqs;
	}

	for (q_id = 0; q_id < qp_params->num_qps; q_id++) {
		err = hinic3_create_qp(hwdev, &sqs[q_id], &rqs[q_id], q_id,
				       qp_params->sq_depth, qp_params->rq_depth,
				       qps_msix_entries[q_id].entry);
		if (err) {
			dev_err(hwdev->dev, "Failed to allocate qp %u, err: %d\n",
				q_id, err);
			goto err_destroy_qp;
		}
	}

	qp_params->sqs = sqs;
	qp_params->rqs = rqs;

	return 0;

err_destroy_qp:
	while (q_id > 0) {
		q_id--;
		hinic3_destroy_qp(hwdev, &sqs[q_id], &rqs[q_id]);
	}
	kfree(rqs);
err_free_sqs:
	kfree(sqs);
err_out:
	return err;
}

void hinic3_free_qps(struct hinic3_nic_dev *nic_dev,
		     struct hinic3_dyna_qp_params *qp_params)
{
	struct hinic3_hwdev *hwdev = nic_dev->hwdev;
	u16 q_id;

	for (q_id = 0; q_id < qp_params->num_qps; q_id++)
		hinic3_destroy_qp(hwdev, &qp_params->sqs[q_id],
				  &qp_params->rqs[q_id]);

	kfree(qp_params->sqs);
	kfree(qp_params->rqs);
}

void hinic3_init_qps(struct hinic3_nic_dev *nic_dev,
		     struct hinic3_dyna_qp_params *qp_params)
{
	struct hinic3_nic_io *nic_io = nic_dev->nic_io;
	struct hinic3_io_queue *sqs = qp_params->sqs;
	struct hinic3_io_queue *rqs = qp_params->rqs;
	u16 q_id;

	nic_io->num_qps = qp_params->num_qps;
	nic_io->sq = qp_params->sqs;
	nic_io->rq = qp_params->rqs;
	for (q_id = 0; q_id < nic_io->num_qps; q_id++) {
		sqs[q_id].cons_idx_addr =
			(u16 *)HINIC3_CI_VADDR(nic_io->ci_vaddr_base, q_id);
		/* clear ci value */
		WRITE_ONCE(*sqs[q_id].cons_idx_addr, 0);

		sqs[q_id].db_addr = nic_io->sqs_db_addr;
		rqs[q_id].db_addr = nic_io->rqs_db_addr;
	}
}

void hinic3_uninit_qps(struct hinic3_nic_dev *nic_dev,
		       struct hinic3_dyna_qp_params *qp_params)
{
	struct hinic3_nic_io *nic_io = nic_dev->nic_io;

	qp_params->sqs = nic_io->sq;
	qp_params->rqs = nic_io->rq;
	qp_params->num_qps = nic_io->num_qps;
}
