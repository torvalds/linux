// SPDX-License-Identifier: GPL-2.0-only
/*
 * Huawei HiNIC PCI Express Linux driver
 * Copyright(c) 2017 Huawei Technologies Co., Ltd
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/semaphore.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/err.h>

#include "hinic_hw_dev.h"
#include "hinic_hw_if.h"
#include "hinic_hw_eqs.h"
#include "hinic_hw_wqe.h"
#include "hinic_hw_wq.h"
#include "hinic_hw_cmdq.h"
#include "hinic_hw_qp_ctxt.h"
#include "hinic_hw_qp.h"
#include "hinic_hw_io.h"

#define CI_Q_ADDR_SIZE                  sizeof(u32)

#define CI_ADDR(base_addr, q_id)        ((base_addr) + \
					 (q_id) * CI_Q_ADDR_SIZE)

#define CI_TABLE_SIZE(num_qps)          ((num_qps) * CI_Q_ADDR_SIZE)

#define DB_IDX(db, db_base)             \
	(((unsigned long)(db) - (unsigned long)(db_base)) / HINIC_DB_PAGE_SIZE)

#define HINIC_PAGE_SIZE_HW(pg_size)	((u8)ilog2((u32)((pg_size) >> 12)))

enum io_cmd {
	IO_CMD_MODIFY_QUEUE_CTXT = 0,
	IO_CMD_CLEAN_QUEUE_CTXT,
};

static void init_db_area_idx(struct hinic_free_db_area *free_db_area)
{
	int i;

	for (i = 0; i < HINIC_DB_MAX_AREAS; i++)
		free_db_area->db_idx[i] = i;

	free_db_area->alloc_pos = 0;
	free_db_area->return_pos = HINIC_DB_MAX_AREAS;

	free_db_area->num_free = HINIC_DB_MAX_AREAS;

	sema_init(&free_db_area->idx_lock, 1);
}

static void __iomem *get_db_area(struct hinic_func_to_io *func_to_io)
{
	struct hinic_free_db_area *free_db_area = &func_to_io->free_db_area;
	int pos, idx;

	down(&free_db_area->idx_lock);

	free_db_area->num_free--;

	if (free_db_area->num_free < 0) {
		free_db_area->num_free++;
		up(&free_db_area->idx_lock);
		return ERR_PTR(-ENOMEM);
	}

	pos = free_db_area->alloc_pos++;
	pos &= HINIC_DB_MAX_AREAS - 1;

	idx = free_db_area->db_idx[pos];

	free_db_area->db_idx[pos] = -1;

	up(&free_db_area->idx_lock);

	return func_to_io->db_base + idx * HINIC_DB_PAGE_SIZE;
}

static void return_db_area(struct hinic_func_to_io *func_to_io,
			   void __iomem *db_base)
{
	struct hinic_free_db_area *free_db_area = &func_to_io->free_db_area;
	int pos, idx = DB_IDX(db_base, func_to_io->db_base);

	down(&free_db_area->idx_lock);

	pos = free_db_area->return_pos++;
	pos &= HINIC_DB_MAX_AREAS - 1;

	free_db_area->db_idx[pos] = idx;

	free_db_area->num_free++;

	up(&free_db_area->idx_lock);
}

static int write_sq_ctxts(struct hinic_func_to_io *func_to_io, u16 base_qpn,
			  u16 num_sqs)
{
	struct hinic_hwif *hwif = func_to_io->hwif;
	struct hinic_sq_ctxt_block *sq_ctxt_block;
	struct pci_dev *pdev = hwif->pdev;
	struct hinic_cmdq_buf cmdq_buf;
	struct hinic_sq_ctxt *sq_ctxt;
	struct hinic_qp *qp;
	u64 out_param;
	int err, i;

	err = hinic_alloc_cmdq_buf(&func_to_io->cmdqs, &cmdq_buf);
	if (err) {
		dev_err(&pdev->dev, "Failed to allocate cmdq buf\n");
		return err;
	}

	sq_ctxt_block = cmdq_buf.buf;
	sq_ctxt = sq_ctxt_block->sq_ctxt;

	hinic_qp_prepare_header(&sq_ctxt_block->hdr, HINIC_QP_CTXT_TYPE_SQ,
				num_sqs, func_to_io->max_qps);
	for (i = 0; i < num_sqs; i++) {
		qp = &func_to_io->qps[i];

		hinic_sq_prepare_ctxt(&sq_ctxt[i], &qp->sq,
				      base_qpn + qp->q_id);
	}

	cmdq_buf.size = HINIC_SQ_CTXT_SIZE(num_sqs);

	err = hinic_cmdq_direct_resp(&func_to_io->cmdqs, HINIC_MOD_L2NIC,
				     IO_CMD_MODIFY_QUEUE_CTXT, &cmdq_buf,
				     &out_param);
	if ((err) || (out_param != 0)) {
		dev_err(&pdev->dev, "Failed to set SQ ctxts\n");
		err = -EFAULT;
	}

	hinic_free_cmdq_buf(&func_to_io->cmdqs, &cmdq_buf);
	return err;
}

static int write_rq_ctxts(struct hinic_func_to_io *func_to_io, u16 base_qpn,
			  u16 num_rqs)
{
	struct hinic_hwif *hwif = func_to_io->hwif;
	struct hinic_rq_ctxt_block *rq_ctxt_block;
	struct pci_dev *pdev = hwif->pdev;
	struct hinic_cmdq_buf cmdq_buf;
	struct hinic_rq_ctxt *rq_ctxt;
	struct hinic_qp *qp;
	u64 out_param;
	int err, i;

	err = hinic_alloc_cmdq_buf(&func_to_io->cmdqs, &cmdq_buf);
	if (err) {
		dev_err(&pdev->dev, "Failed to allocate cmdq buf\n");
		return err;
	}

	rq_ctxt_block = cmdq_buf.buf;
	rq_ctxt = rq_ctxt_block->rq_ctxt;

	hinic_qp_prepare_header(&rq_ctxt_block->hdr, HINIC_QP_CTXT_TYPE_RQ,
				num_rqs, func_to_io->max_qps);
	for (i = 0; i < num_rqs; i++) {
		qp = &func_to_io->qps[i];

		hinic_rq_prepare_ctxt(&rq_ctxt[i], &qp->rq,
				      base_qpn + qp->q_id);
	}

	cmdq_buf.size = HINIC_RQ_CTXT_SIZE(num_rqs);

	err = hinic_cmdq_direct_resp(&func_to_io->cmdqs, HINIC_MOD_L2NIC,
				     IO_CMD_MODIFY_QUEUE_CTXT, &cmdq_buf,
				     &out_param);
	if ((err) || (out_param != 0)) {
		dev_err(&pdev->dev, "Failed to set RQ ctxts\n");
		err = -EFAULT;
	}

	hinic_free_cmdq_buf(&func_to_io->cmdqs, &cmdq_buf);
	return err;
}

/**
 * write_qp_ctxts - write the qp ctxt to HW
 * @func_to_io: func to io channel that holds the IO components
 * @base_qpn: first qp number
 * @num_qps: number of qps to write
 *
 * Return 0 - Success, negative - Failure
 **/
static int write_qp_ctxts(struct hinic_func_to_io *func_to_io, u16 base_qpn,
			  u16 num_qps)
{
	return (write_sq_ctxts(func_to_io, base_qpn, num_qps) ||
		write_rq_ctxts(func_to_io, base_qpn, num_qps));
}

static int hinic_clean_queue_offload_ctxt(struct hinic_func_to_io *func_to_io,
					  enum hinic_qp_ctxt_type ctxt_type)
{
	struct hinic_hwif *hwif = func_to_io->hwif;
	struct hinic_clean_queue_ctxt *ctxt_block;
	struct pci_dev *pdev = hwif->pdev;
	struct hinic_cmdq_buf cmdq_buf;
	u64 out_param = 0;
	int err;

	err = hinic_alloc_cmdq_buf(&func_to_io->cmdqs, &cmdq_buf);
	if (err) {
		dev_err(&pdev->dev, "Failed to allocate cmdq buf\n");
		return err;
	}

	ctxt_block = cmdq_buf.buf;
	ctxt_block->cmdq_hdr.num_queues = func_to_io->max_qps;
	ctxt_block->cmdq_hdr.queue_type = ctxt_type;
	ctxt_block->cmdq_hdr.addr_offset = 0;

	/* TSO/LRO ctxt size: 0x0:0B; 0x1:160B; 0x2:200B; 0x3:240B */
	ctxt_block->ctxt_size = 0x3;

	hinic_cpu_to_be32(ctxt_block, sizeof(*ctxt_block));

	cmdq_buf.size = sizeof(*ctxt_block);

	err = hinic_cmdq_direct_resp(&func_to_io->cmdqs, HINIC_MOD_L2NIC,
				     IO_CMD_CLEAN_QUEUE_CTXT,
				     &cmdq_buf, &out_param);

	if (err || out_param) {
		dev_err(&pdev->dev, "Failed to clean offload ctxts, err: %d, out_param: 0x%llx\n",
			err, out_param);

		err = -EFAULT;
	}

	hinic_free_cmdq_buf(&func_to_io->cmdqs, &cmdq_buf);

	return err;
}

static int hinic_clean_qp_offload_ctxt(struct hinic_func_to_io *func_to_io)
{
	/* clean LRO/TSO context space */
	return (hinic_clean_queue_offload_ctxt(func_to_io,
					       HINIC_QP_CTXT_TYPE_SQ) ||
		hinic_clean_queue_offload_ctxt(func_to_io,
					       HINIC_QP_CTXT_TYPE_RQ));
}

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
	void __iomem *db_base;
	int err;

	qp->q_id = q_id;

	err = hinic_wq_allocate(&func_to_io->wqs, &func_to_io->sq_wq[q_id],
				HINIC_SQ_WQEBB_SIZE, HINIC_SQ_PAGE_SIZE,
				func_to_io->sq_depth, HINIC_SQ_WQE_MAX_SIZE);
	if (err) {
		dev_err(&pdev->dev, "Failed to allocate WQ for SQ\n");
		return err;
	}

	err = hinic_wq_allocate(&func_to_io->wqs, &func_to_io->rq_wq[q_id],
				HINIC_RQ_WQEBB_SIZE, HINIC_RQ_PAGE_SIZE,
				func_to_io->rq_depth, HINIC_RQ_WQE_SIZE);
	if (err) {
		dev_err(&pdev->dev, "Failed to allocate WQ for RQ\n");
		goto err_rq_alloc;
	}

	db_base = get_db_area(func_to_io);
	if (IS_ERR(db_base)) {
		dev_err(&pdev->dev, "Failed to get DB area for SQ\n");
		err = PTR_ERR(db_base);
		goto err_get_db;
	}

	func_to_io->sq_db[q_id] = db_base;

	qp->sq.qid = q_id;
	err = hinic_init_sq(&qp->sq, hwif, &func_to_io->sq_wq[q_id],
			    sq_msix_entry,
			    CI_ADDR(func_to_io->ci_addr_base, q_id),
			    CI_ADDR(func_to_io->ci_dma_base, q_id), db_base);
	if (err) {
		dev_err(&pdev->dev, "Failed to init SQ\n");
		goto err_sq_init;
	}

	qp->rq.qid = q_id;
	err = hinic_init_rq(&qp->rq, hwif, &func_to_io->rq_wq[q_id],
			    rq_msix_entry);
	if (err) {
		dev_err(&pdev->dev, "Failed to init RQ\n");
		goto err_rq_init;
	}

	return 0;

err_rq_init:
	hinic_clean_sq(&qp->sq);

err_sq_init:
	return_db_area(func_to_io, db_base);

err_get_db:
	hinic_wq_free(&func_to_io->wqs, &func_to_io->rq_wq[q_id]);

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

	hinic_clean_rq(&qp->rq);
	hinic_clean_sq(&qp->sq);

	return_db_area(func_to_io, func_to_io->sq_db[q_id]);

	hinic_wq_free(&func_to_io->wqs, &func_to_io->rq_wq[q_id]);
	hinic_wq_free(&func_to_io->wqs, &func_to_io->sq_wq[q_id]);
}

/**
 * hinic_io_create_qps - Create Queue Pairs
 * @func_to_io: func to io channel that holds the IO components
 * @base_qpn: base qp number
 * @num_qps: number queue pairs to create
 * @sq_msix_entries: msix entries for sq
 * @rq_msix_entries: msix entries for rq
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
	size_t qps_size, wq_size, db_size;
	void *ci_addr_base;
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

	db_size = num_qps * sizeof(*func_to_io->sq_db);
	func_to_io->sq_db = devm_kzalloc(&pdev->dev, db_size, GFP_KERNEL);
	if (!func_to_io->sq_db) {
		err = -ENOMEM;
		goto err_sq_db;
	}

	ci_addr_base = dma_alloc_coherent(&pdev->dev, CI_TABLE_SIZE(num_qps),
					  &func_to_io->ci_dma_base,
					  GFP_KERNEL);
	if (!ci_addr_base) {
		dev_err(&pdev->dev, "Failed to allocate CI area\n");
		err = -ENOMEM;
		goto err_ci_base;
	}

	func_to_io->ci_addr_base = ci_addr_base;

	for (i = 0; i < num_qps; i++) {
		err = init_qp(func_to_io, &func_to_io->qps[i], i,
			      &sq_msix_entries[i], &rq_msix_entries[i]);
		if (err) {
			dev_err(&pdev->dev, "Failed to create QP %d\n", i);
			goto err_init_qp;
		}
	}

	err = write_qp_ctxts(func_to_io, base_qpn, num_qps);
	if (err) {
		dev_err(&pdev->dev, "Failed to init QP ctxts\n");
		goto err_write_qp_ctxts;
	}

	err = hinic_clean_qp_offload_ctxt(func_to_io);
	if (err) {
		dev_err(&pdev->dev, "Failed to clean QP contexts space\n");
		goto err_write_qp_ctxts;
	}

	return 0;

err_write_qp_ctxts:
err_init_qp:
	for (j = 0; j < i; j++)
		destroy_qp(func_to_io, &func_to_io->qps[j]);

	dma_free_coherent(&pdev->dev, CI_TABLE_SIZE(num_qps),
			  func_to_io->ci_addr_base, func_to_io->ci_dma_base);

err_ci_base:
	devm_kfree(&pdev->dev, func_to_io->sq_db);

err_sq_db:
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
	size_t ci_table_size;
	int i;

	ci_table_size = CI_TABLE_SIZE(num_qps);

	for (i = 0; i < num_qps; i++)
		destroy_qp(func_to_io, &func_to_io->qps[i]);

	dma_free_coherent(&pdev->dev, ci_table_size, func_to_io->ci_addr_base,
			  func_to_io->ci_dma_base);

	devm_kfree(&pdev->dev, func_to_io->sq_db);

	devm_kfree(&pdev->dev, func_to_io->rq_wq);
	devm_kfree(&pdev->dev, func_to_io->sq_wq);

	devm_kfree(&pdev->dev, func_to_io->qps);
}

int hinic_set_wq_page_size(struct hinic_hwdev *hwdev, u16 func_idx,
			   u32 page_size)
{
	struct hinic_wq_page_size page_size_info = {0};
	u16 out_size = sizeof(page_size_info);
	struct hinic_pfhwdev *pfhwdev;
	int err;

	pfhwdev = container_of(hwdev, struct hinic_pfhwdev, hwdev);

	page_size_info.func_idx = func_idx;
	page_size_info.ppf_idx = HINIC_HWIF_PPF_IDX(hwdev->hwif);
	page_size_info.page_size = HINIC_PAGE_SIZE_HW(page_size);

	err = hinic_msg_to_mgmt(&pfhwdev->pf_to_mgmt, HINIC_MOD_COMM,
				HINIC_COMM_CMD_PAGESIZE_SET, &page_size_info,
				sizeof(page_size_info), &page_size_info,
				&out_size, HINIC_MGMT_MSG_SYNC);
	if (err || !out_size || page_size_info.status) {
		dev_err(&hwdev->hwif->pdev->dev, "Failed to set wq page size, err: %d, status: 0x%x, out_size: 0x%0x\n",
			err, page_size_info.status, out_size);
		return -EFAULT;
	}

	return 0;
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
	enum hinic_cmdq_type cmdq, type;
	void __iomem *db_area;
	int err;

	func_to_io->hwif = hwif;
	func_to_io->qps = NULL;
	func_to_io->max_qps = max_qps;
	func_to_io->ceqs.hwdev = func_to_io->hwdev;

	err = hinic_ceqs_init(&func_to_io->ceqs, hwif, num_ceqs,
			      HINIC_DEFAULT_CEQ_LEN, HINIC_EQ_PAGE_SIZE,
			      ceq_msix_entries);
	if (err) {
		dev_err(&pdev->dev, "Failed to init CEQs\n");
		return err;
	}

	err = hinic_wqs_alloc(&func_to_io->wqs, 2 * max_qps, hwif);
	if (err) {
		dev_err(&pdev->dev, "Failed to allocate WQS for IO\n");
		goto err_wqs_alloc;
	}

	func_to_io->db_base = pci_ioremap_bar(pdev, HINIC_PCI_DB_BAR);
	if (!func_to_io->db_base) {
		dev_err(&pdev->dev, "Failed to remap IO DB area\n");
		err = -ENOMEM;
		goto err_db_ioremap;
	}

	init_db_area_idx(&func_to_io->free_db_area);

	for (cmdq = HINIC_CMDQ_SYNC; cmdq < HINIC_MAX_CMDQ_TYPES; cmdq++) {
		db_area = get_db_area(func_to_io);
		if (IS_ERR(db_area)) {
			dev_err(&pdev->dev, "Failed to get cmdq db area\n");
			err = PTR_ERR(db_area);
			goto err_db_area;
		}

		func_to_io->cmdq_db_area[cmdq] = db_area;
	}

	err = hinic_set_wq_page_size(func_to_io->hwdev,
				     HINIC_HWIF_FUNC_IDX(hwif),
				     HINIC_DEFAULT_WQ_PAGE_SIZE);
	if (err) {
		dev_err(&func_to_io->hwif->pdev->dev, "Failed to set wq page size\n");
		goto init_wq_pg_size_err;
	}

	err = hinic_init_cmdqs(&func_to_io->cmdqs, hwif,
			       func_to_io->cmdq_db_area);
	if (err) {
		dev_err(&pdev->dev, "Failed to initialize cmdqs\n");
		goto err_init_cmdqs;
	}

	return 0;

err_init_cmdqs:
	if (!HINIC_IS_VF(func_to_io->hwif))
		hinic_set_wq_page_size(func_to_io->hwdev,
				       HINIC_HWIF_FUNC_IDX(hwif),
				       HINIC_HW_WQ_PAGE_SIZE);
init_wq_pg_size_err:
err_db_area:
	for (type = HINIC_CMDQ_SYNC; type < cmdq; type++)
		return_db_area(func_to_io, func_to_io->cmdq_db_area[type]);

	iounmap(func_to_io->db_base);

err_db_ioremap:
	hinic_wqs_free(&func_to_io->wqs);

err_wqs_alloc:
	hinic_ceqs_free(&func_to_io->ceqs);
	return err;
}

/**
 * hinic_io_free - Free the IO components
 * @func_to_io: func to io channel that holds the IO components
 **/
void hinic_io_free(struct hinic_func_to_io *func_to_io)
{
	enum hinic_cmdq_type cmdq;

	hinic_free_cmdqs(&func_to_io->cmdqs);

	if (!HINIC_IS_VF(func_to_io->hwif))
		hinic_set_wq_page_size(func_to_io->hwdev,
				       HINIC_HWIF_FUNC_IDX(func_to_io->hwif),
				       HINIC_HW_WQ_PAGE_SIZE);

	for (cmdq = HINIC_CMDQ_SYNC; cmdq < HINIC_MAX_CMDQ_TYPES; cmdq++)
		return_db_area(func_to_io, func_to_io->cmdq_db_area[cmdq]);

	iounmap(func_to_io->db_base);
	hinic_wqs_free(&func_to_io->wqs);
	hinic_ceqs_free(&func_to_io->ceqs);
}
