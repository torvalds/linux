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

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/spinlock.h>
#include <linux/sizes.h>
#include <linux/atomic.h>
#include <linux/log2.h>
#include <asm/byteorder.h>

#include "hinic_hw_if.h"
#include "hinic_hw_mgmt.h"
#include "hinic_hw_wq.h"
#include "hinic_hw_cmdq.h"
#include "hinic_hw_io.h"
#include "hinic_hw_dev.h"

#define CMDQ_DB_OFF                     SZ_2K

#define CMDQ_WQEBB_SIZE                 64
#define CMDQ_DEPTH                      SZ_4K

#define CMDQ_WQ_PAGE_SIZE               SZ_4K

#define WQE_LCMD_SIZE                   64
#define WQE_SCMD_SIZE                   64

#define CMDQ_PFN(addr, page_size)       ((addr) >> (ilog2(page_size)))

#define cmdq_to_cmdqs(cmdq)     container_of((cmdq) - (cmdq)->cmdq_type, \
					     struct hinic_cmdqs, cmdq[0])

#define cmdqs_to_func_to_io(cmdqs)      container_of(cmdqs, \
						     struct hinic_func_to_io, \
						     cmdqs)

enum cmdq_wqe_type {
	WQE_LCMD_TYPE = 0,
	WQE_SCMD_TYPE = 1,
};

/**
 * hinic_alloc_cmdq_buf - alloc buffer for sending command
 * @cmdqs: the cmdqs
 * @cmdq_buf: the buffer returned in this struct
 *
 * Return 0 - Success, negative - Failure
 **/
int hinic_alloc_cmdq_buf(struct hinic_cmdqs *cmdqs,
			 struct hinic_cmdq_buf *cmdq_buf)
{
	struct hinic_hwif *hwif = cmdqs->hwif;
	struct pci_dev *pdev = hwif->pdev;

	cmdq_buf->buf = pci_pool_alloc(cmdqs->cmdq_buf_pool, GFP_KERNEL,
				       &cmdq_buf->dma_addr);
	if (!cmdq_buf->buf) {
		dev_err(&pdev->dev, "Failed to allocate cmd from the pool\n");
		return -ENOMEM;
	}

	return 0;
}

/**
 * hinic_free_cmdq_buf - free buffer
 * @cmdqs: the cmdqs
 * @cmdq_buf: the buffer to free that is in this struct
 **/
void hinic_free_cmdq_buf(struct hinic_cmdqs *cmdqs,
			 struct hinic_cmdq_buf *cmdq_buf)
{
	pci_pool_free(cmdqs->cmdq_buf_pool, cmdq_buf->buf, cmdq_buf->dma_addr);
}

/**
 * hinic_cmdq_direct_resp - send command with direct data as resp
 * @cmdqs: the cmdqs
 * @mod: module on the card that will handle the command
 * @cmd: the command
 * @buf_in: the buffer for the command
 * @resp: the response to return
 *
 * Return 0 - Success, negative - Failure
 **/
int hinic_cmdq_direct_resp(struct hinic_cmdqs *cmdqs,
			   enum hinic_mod_type mod, u8 cmd,
			   struct hinic_cmdq_buf *buf_in, u64 *resp)
{
	/* should be implemented */
	return -EINVAL;
}

/**
 * cmdq_init_queue_ctxt - init the queue ctxt of a cmdq
 * @cmdq_ctxt: cmdq ctxt to initialize
 * @cmdq: the cmdq
 * @cmdq_pages: the memory of the queue
 **/
static void cmdq_init_queue_ctxt(struct hinic_cmdq_ctxt *cmdq_ctxt,
				 struct hinic_cmdq *cmdq,
				 struct hinic_cmdq_pages *cmdq_pages)
{
	struct hinic_cmdq_ctxt_info *ctxt_info = &cmdq_ctxt->ctxt_info;
	u64 wq_first_page_paddr, cmdq_first_block_paddr, pfn;
	struct hinic_cmdqs *cmdqs = cmdq_to_cmdqs(cmdq);
	struct hinic_wq *wq = cmdq->wq;

	/* The data in the HW is in Big Endian Format */
	wq_first_page_paddr = be64_to_cpu(*wq->block_vaddr);

	pfn = CMDQ_PFN(wq_first_page_paddr, wq->wq_page_size);

	ctxt_info->curr_wqe_page_pfn =
		HINIC_CMDQ_CTXT_PAGE_INFO_SET(pfn, CURR_WQE_PAGE_PFN)   |
		HINIC_CMDQ_CTXT_PAGE_INFO_SET(HINIC_CEQ_ID_CMDQ, EQ_ID) |
		HINIC_CMDQ_CTXT_PAGE_INFO_SET(1, CEQ_ARM)               |
		HINIC_CMDQ_CTXT_PAGE_INFO_SET(1, CEQ_EN)                |
		HINIC_CMDQ_CTXT_PAGE_INFO_SET(cmdq->wrapped, WRAPPED);

	/* block PFN - Read Modify Write */
	cmdq_first_block_paddr = cmdq_pages->page_paddr;

	pfn = CMDQ_PFN(cmdq_first_block_paddr, wq->wq_page_size);

	ctxt_info->wq_block_pfn =
		HINIC_CMDQ_CTXT_BLOCK_INFO_SET(pfn, WQ_BLOCK_PFN) |
		HINIC_CMDQ_CTXT_BLOCK_INFO_SET(atomic_read(&wq->cons_idx), CI);

	cmdq_ctxt->func_idx = HINIC_HWIF_FUNC_IDX(cmdqs->hwif);
	cmdq_ctxt->cmdq_type  = cmdq->cmdq_type;
}

/**
 * init_cmdq - initialize cmdq
 * @cmdq: the cmdq
 * @wq: the wq attaced to the cmdq
 * @q_type: the cmdq type of the cmdq
 * @db_area: doorbell area for the cmdq
 *
 * Return 0 - Success, negative - Failure
 **/
static int init_cmdq(struct hinic_cmdq *cmdq, struct hinic_wq *wq,
		     enum hinic_cmdq_type q_type, void __iomem *db_area)
{
	int err;

	cmdq->wq = wq;
	cmdq->cmdq_type = q_type;
	cmdq->wrapped = 1;

	spin_lock_init(&cmdq->cmdq_lock);

	cmdq->done = vzalloc(wq->q_depth * sizeof(*cmdq->done));
	if (!cmdq->done)
		return -ENOMEM;

	cmdq->errcode = vzalloc(wq->q_depth * sizeof(*cmdq->errcode));
	if (!cmdq->errcode) {
		err = -ENOMEM;
		goto err_errcode;
	}

	cmdq->db_base = db_area + CMDQ_DB_OFF;
	return 0;

err_errcode:
	vfree(cmdq->done);
	return err;
}

/**
 * free_cmdq - Free cmdq
 * @cmdq: the cmdq to free
 **/
static void free_cmdq(struct hinic_cmdq *cmdq)
{
	vfree(cmdq->errcode);
	vfree(cmdq->done);
}

/**
 * init_cmdqs_ctxt - write the cmdq ctxt to HW after init all cmdq
 * @hwdev: the NIC HW device
 * @cmdqs: cmdqs to write the ctxts for
 * &db_area: db_area for all the cmdqs
 *
 * Return 0 - Success, negative - Failure
 **/
static int init_cmdqs_ctxt(struct hinic_hwdev *hwdev,
			   struct hinic_cmdqs *cmdqs, void __iomem **db_area)
{
	struct hinic_hwif *hwif = hwdev->hwif;
	enum hinic_cmdq_type type, cmdq_type;
	struct hinic_cmdq_ctxt *cmdq_ctxts;
	struct pci_dev *pdev = hwif->pdev;
	struct hinic_pfhwdev *pfhwdev;
	size_t cmdq_ctxts_size;
	int err;

	if (!HINIC_IS_PF(hwif) && !HINIC_IS_PPF(hwif)) {
		dev_err(&pdev->dev, "Unsupported PCI function type\n");
		return -EINVAL;
	}

	cmdq_ctxts_size = HINIC_MAX_CMDQ_TYPES * sizeof(*cmdq_ctxts);
	cmdq_ctxts = devm_kzalloc(&pdev->dev, cmdq_ctxts_size, GFP_KERNEL);
	if (!cmdq_ctxts)
		return -ENOMEM;

	pfhwdev = container_of(hwdev, struct hinic_pfhwdev, hwdev);

	cmdq_type = HINIC_CMDQ_SYNC;
	for (; cmdq_type < HINIC_MAX_CMDQ_TYPES; cmdq_type++) {
		err = init_cmdq(&cmdqs->cmdq[cmdq_type],
				&cmdqs->saved_wqs[cmdq_type], cmdq_type,
				db_area[cmdq_type]);
		if (err) {
			dev_err(&pdev->dev, "Failed to initialize cmdq\n");
			goto err_init_cmdq;
		}

		cmdq_init_queue_ctxt(&cmdq_ctxts[cmdq_type],
				     &cmdqs->cmdq[cmdq_type],
				     &cmdqs->cmdq_pages);
	}

	/* Write the CMDQ ctxts */
	cmdq_type = HINIC_CMDQ_SYNC;
	for (; cmdq_type < HINIC_MAX_CMDQ_TYPES; cmdq_type++) {
		err = hinic_msg_to_mgmt(&pfhwdev->pf_to_mgmt, HINIC_MOD_COMM,
					HINIC_COMM_CMD_CMDQ_CTXT_SET,
					&cmdq_ctxts[cmdq_type],
					sizeof(cmdq_ctxts[cmdq_type]),
					NULL, NULL, HINIC_MGMT_MSG_SYNC);
		if (err) {
			dev_err(&pdev->dev, "Failed to set CMDQ CTXT type = %d\n",
				cmdq_type);
			goto err_write_cmdq_ctxt;
		}
	}

	devm_kfree(&pdev->dev, cmdq_ctxts);
	return 0;

err_write_cmdq_ctxt:
	cmdq_type = HINIC_MAX_CMDQ_TYPES;

err_init_cmdq:
	for (type = HINIC_CMDQ_SYNC; type < cmdq_type; type++)
		free_cmdq(&cmdqs->cmdq[type]);

	devm_kfree(&pdev->dev, cmdq_ctxts);
	return err;
}

/**
 * hinic_init_cmdqs - init all cmdqs
 * @cmdqs: cmdqs to init
 * @hwif: HW interface for accessing cmdqs
 * @db_area: doorbell areas for all the cmdqs
 *
 * Return 0 - Success, negative - Failure
 **/
int hinic_init_cmdqs(struct hinic_cmdqs *cmdqs, struct hinic_hwif *hwif,
		     void __iomem **db_area)
{
	struct hinic_func_to_io *func_to_io = cmdqs_to_func_to_io(cmdqs);
	struct pci_dev *pdev = hwif->pdev;
	struct hinic_hwdev *hwdev;
	size_t saved_wqs_size;
	u16 max_wqe_size;
	int err;

	cmdqs->hwif = hwif;
	cmdqs->cmdq_buf_pool = pci_pool_create("hinic_cmdq", pdev,
					       HINIC_CMDQ_BUF_SIZE,
					       HINIC_CMDQ_BUF_SIZE, 0);
	if (!cmdqs->cmdq_buf_pool)
		return -ENOMEM;

	saved_wqs_size = HINIC_MAX_CMDQ_TYPES * sizeof(struct hinic_wq);
	cmdqs->saved_wqs = devm_kzalloc(&pdev->dev, saved_wqs_size, GFP_KERNEL);
	if (!cmdqs->saved_wqs) {
		err = -ENOMEM;
		goto err_saved_wqs;
	}

	max_wqe_size = WQE_LCMD_SIZE;
	err = hinic_wqs_cmdq_alloc(&cmdqs->cmdq_pages, cmdqs->saved_wqs, hwif,
				   HINIC_MAX_CMDQ_TYPES, CMDQ_WQEBB_SIZE,
				   CMDQ_WQ_PAGE_SIZE, CMDQ_DEPTH, max_wqe_size);
	if (err) {
		dev_err(&pdev->dev, "Failed to allocate CMDQ wqs\n");
		goto err_cmdq_wqs;
	}

	hwdev = container_of(func_to_io, struct hinic_hwdev, func_to_io);
	err = init_cmdqs_ctxt(hwdev, cmdqs, db_area);
	if (err) {
		dev_err(&pdev->dev, "Failed to write cmdq ctxt\n");
		goto err_cmdq_ctxt;
	}

	return 0;

err_cmdq_ctxt:
	hinic_wqs_cmdq_free(&cmdqs->cmdq_pages, cmdqs->saved_wqs,
			    HINIC_MAX_CMDQ_TYPES);

err_cmdq_wqs:
	devm_kfree(&pdev->dev, cmdqs->saved_wqs);

err_saved_wqs:
	pci_pool_destroy(cmdqs->cmdq_buf_pool);
	return err;
}

/**
 * hinic_free_cmdqs - free all cmdqs
 * @cmdqs: cmdqs to free
 **/
void hinic_free_cmdqs(struct hinic_cmdqs *cmdqs)
{
	struct hinic_hwif *hwif = cmdqs->hwif;
	struct pci_dev *pdev = hwif->pdev;
	enum hinic_cmdq_type cmdq_type;

	cmdq_type = HINIC_CMDQ_SYNC;
	for (; cmdq_type < HINIC_MAX_CMDQ_TYPES; cmdq_type++)
		free_cmdq(&cmdqs->cmdq[cmdq_type]);

	hinic_wqs_cmdq_free(&cmdqs->cmdq_pages, cmdqs->saved_wqs,
			    HINIC_MAX_CMDQ_TYPES);

	devm_kfree(&pdev->dev, cmdqs->saved_wqs);

	pci_pool_destroy(cmdqs->cmdq_buf_pool);
}
