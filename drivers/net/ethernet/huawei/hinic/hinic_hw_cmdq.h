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

#ifndef HINIC_CMDQ_H
#define HINIC_CMDQ_H

#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/pci.h>

#include "hinic_hw_if.h"
#include "hinic_hw_wq.h"

#define HINIC_CMDQ_BUF_SIZE             2048

enum hinic_cmdq_type {
	HINIC_CMDQ_SYNC,

	HINIC_MAX_CMDQ_TYPES,
};

struct hinic_cmdq_buf {
	void            *buf;
	dma_addr_t      dma_addr;
	size_t          size;
};

struct hinic_cmdq {
	struct hinic_wq         *wq;

	enum hinic_cmdq_type    cmdq_type;
	int                     wrapped;

	/* Lock for keeping the doorbell order */
	spinlock_t              cmdq_lock;

	struct completion       **done;
	int                     **errcode;

	/* doorbell area */
	void __iomem            *db_base;
};

struct hinic_cmdqs {
	struct hinic_hwif       *hwif;

	struct pci_pool         *cmdq_buf_pool;

	struct hinic_wq         *saved_wqs;

	struct hinic_cmdq_pages cmdq_pages;

	struct hinic_cmdq       cmdq[HINIC_MAX_CMDQ_TYPES];
};

int hinic_alloc_cmdq_buf(struct hinic_cmdqs *cmdqs,
			 struct hinic_cmdq_buf *cmdq_buf);

void hinic_free_cmdq_buf(struct hinic_cmdqs *cmdqs,
			 struct hinic_cmdq_buf *cmdq_buf);

int hinic_cmdq_direct_resp(struct hinic_cmdqs *cmdqs,
			   enum hinic_mod_type mod, u8 cmd,
			   struct hinic_cmdq_buf *buf_in, u64 *out_param);

int hinic_init_cmdqs(struct hinic_cmdqs *cmdqs, struct hinic_hwif *hwif,
		     void __iomem **db_area);

void hinic_free_cmdqs(struct hinic_cmdqs *cmdqs);

#endif
