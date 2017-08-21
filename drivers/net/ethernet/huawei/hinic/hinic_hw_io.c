/*
 * Huawei HiNIC PCI Express Linux driver
 * Copyright(c) 2017 Huawei Technologies Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 */

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/slab.h>

#include "hinic_hw_if.h"
#include "hinic_hw_wqe.h"
#include "hinic_hw_wq.h"
#include "hinic_hw_qp.h"
#include "hinic_hw_io.h"

/**
 * init_qp - Initialize a Queue Pair
 * @func_to_io: func to io channel that holds the IO components
 * @qp: pointer to the qp to initialize
 * @q_id: the id of the qp
 * @sq_msix_entry: msix entry for sq
 * @rq_msix_entry: msix entry for rq
 *
 * Return 0 - Success, negative - Failure
 **/
static int init_qp(struct hinic_func_to_io *func_to_io,
		   struct hinic_qp *qp, int q_id,
		   struct msix_entry *sq_msix_entry,
		   struct msix_entry *rq_msix_entry)
{
	struct hinic_hwif *hwif = func_to_io->hwif;
	struct pci_dev *pdev = hwif->pdev;
	int err;

	qp->q_id = q_id;

	err = hinic_wq_allocate(&func_to_io->wqs, &func_to_io->sq_wq[q_id],
				HINIC_SQ_WQEBB_SIZE, HINIC_SQ_PAGE_SIZE,
				HINIC_SQ_DEPTH, HINIC_SQ_WQE_MAX_SIZE);
	if (err) {
		dev_err(&pdev->dev, "Failed to allocate WQ for SQ\n");
		return err;
	}

	err = hinic_wq_allocate(&func_to_io->wqs, &func_to_io->rq_wq[q_id],
				HINIC_RQ_WQEBB_SIZE, HINIC_RQ_PAGE_SIZE,
				HINIC_RQ_DEPTH, HINIC_RQ_WQE_SIZE);
	if (err) {
		dev_err(&pdev->dev, "Failed to allocate WQ for RQ\n");
		goto err_rq_alloc;
	}

	return 0;

err_rq_alloc:
	hinic_wq_free(&func_to_io->wqs, &func_to_io->sq_wq[q_id]);
	return err;
}

/**
 * destroy_qp - Clean the resources of a Queue Pair
 * @func_to_io: func to io channel that holds the IO components
 * @qp: pointer to the qp to clean
 **/
static void destroy_qp(struct hinic_func_to_io *func_to_io,
		       struct hinic_qp *qp)
{
	int q_id = qp->q_id;

	hinic_wq_free(&func_to_io->wqs, &func_to_io->rq_wq[q_id]);
	hinic_wq_free(&func_to_io->wqs, &func_to_io->sq_wq[q_id]);
}

/**
 * hinic_io_create_qps - Create Queue Pairs
 * @func_to_io: func to io channel that holds the IO components
 * @base_qpn: base qp number
 * @num_qps: number queue pairs to create
 * @sq_msix_entry: msix entries for sq
 * @rq_msix_entry: msix entries for rq
 *
 * Return 0 - Success, negative - Failure
 **/
int hinic_io_create_qps(struct hinic_func_to_io *func_to_io,
			u16 base_qpn, int num_qps,
			struct msix_entry *sq_msix_entries,
			struct msix_entry *rq_msix_entries)
{
	struct hinic_hwif *hwif = func_to_io->hwif;
	struct pci_dev *pdev = hwif->pdev;
	size_t qps_size, wq_size;
	int i, j, err;

	qps_size = num_qps * sizeof(*func_to_io->qps);
	func_to_io->qps = devm_kzalloc(&pdev->dev, qps_size, GFP_KERNEL);
	if (!func_to_io->qps)
		return -ENOMEM;

	wq_size = num_qps * sizeof(*func_to_io->sq_wq);
	func_to_io->sq_wq = devm_kzalloc(&pdev->dev, wq_size, GFP_KERNEL);
	if (!func_to_io->sq_wq) {
		err = -ENOMEM;
		goto err_sq_wq;
	}

	wq_size = num_qps * sizeof(*func_to_io->rq_wq);
	func_to_io->rq_wq = devm_kzalloc(&pdev->dev, wq_size, GFP_KERNEL);
	if (!func_to_io->rq_wq) {
		err = -ENOMEM;
		goto err_rq_wq;
	}

	for (i = 0; i < num_qps; i++) {
		err = init_qp(func_to_io, &func_to_io->qps[i], i,
			      &sq_msix_entries[i], &rq_msix_entries[i]);
		if (err) {
			dev_err(&pdev->dev, "Failed to create QP %d\n", i);
			goto err_init_qp;
		}
	}

	return 0;

err_init_qp:
	for (j = 0; j < i; j++)
		destroy_qp(func_to_io, &func_to_io->qps[j]);

	devm_kfree(&pdev->dev, func_to_io->rq_wq);

err_rq_wq:
	devm_kfree(&pdev->dev, func_to_io->sq_wq);

err_sq_wq:
	devm_kfree(&pdev->dev, func_to_io->qps);
	return err;
}

/**
 * hinic_io_destroy_qps - Destroy the IO Queue Pairs
 * @func_to_io: func to io channel that holds the IO components
 * @num_qps: number queue pairs to destroy
 **/
void hinic_io_destroy_qps(struct hinic_func_to_io *func_to_io, int num_qps)
{
	struct hinic_hwif *hwif = func_to_io->hwif;
	struct pci_dev *pdev = hwif->pdev;
	int i;

	for (i = 0; i < num_qps; i++)
		destroy_qp(func_to_io, &func_to_io->qps[i]);

	devm_kfree(&pdev->dev, func_to_io->rq_wq);
	devm_kfree(&pdev->dev, func_to_io->sq_wq);

	devm_kfree(&pdev->dev, func_to_io->qps);
}

/**
 * hinic_io_init - Initialize the IO components
 * @func_to_io: func to io channel that holds the IO components
 * @hwif: HW interface for accessing IO
 * @max_qps: maximum QPs in HW
 * @num_ceqs: number completion event queues
 * @ceq_msix_entries: msix entries for ceqs
 *
 * Return 0 - Success, negative - Failure
 **/
int hinic_io_init(struct hinic_func_to_io *func_to_io,
		  struct hinic_hwif *hwif, u16 max_qps, int num_ceqs,
		  struct msix_entry *ceq_msix_entries)
{
	struct pci_dev *pdev = hwif->pdev;
	int err;

	func_to_io->hwif = hwif;
	func_to_io->qps = NULL;
	func_to_io->max_qps = max_qps;

	err = hinic_wqs_alloc(&func_to_io->wqs, 2 * max_qps, hwif);
	if (err) {
		dev_err(&pdev->dev, "Failed to allocate WQS for IO\n");
		return err;
	}

	return 0;
}

/**
 * hinic_io_free - Free the IO components
 * @func_to_io: func to io channel that holds the IO components
 **/
void hinic_io_free(struct hinic_func_to_io *func_to_io)
{
	hinic_wqs_free(&func_to_io->wqs);
}
