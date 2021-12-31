/* SPDX-License-Identifier: GPL-2.0+ */
// Copyright (c) 2021-2021 Hisilicon Limited.

#ifndef __HCLGE_COMM_CMD_H
#define __HCLGE_COMM_CMD_H
#include <linux/types.h>

#include "hnae3.h"

#define HCLGE_DESC_DATA_LEN		6
struct hclge_desc {
	__le16 opcode;
	__le16 flag;
	__le16 retval;
	__le16 rsv;
	__le32 data[HCLGE_DESC_DATA_LEN];
};

struct hclge_comm_cmq_ring {
	dma_addr_t desc_dma_addr;
	struct hclge_desc *desc;
	struct pci_dev *pdev;
	u32 head;
	u32 tail;

	u16 buf_size;
	u16 desc_num;
	int next_to_use;
	int next_to_clean;
	u8 ring_type; /* cmq ring type */
	spinlock_t lock; /* Command queue lock */
};

enum hclge_comm_cmd_status {
	HCLGE_COMM_STATUS_SUCCESS	= 0,
	HCLGE_COMM_ERR_CSQ_FULL		= -1,
	HCLGE_COMM_ERR_CSQ_TIMEOUT	= -2,
	HCLGE_COMM_ERR_CSQ_ERROR	= -3,
};

struct hclge_comm_cmq {
	struct hclge_comm_cmq_ring csq;
	struct hclge_comm_cmq_ring crq;
	u16 tx_timeout;
	enum hclge_comm_cmd_status last_status;
};

struct hclge_comm_hw {
	void __iomem *io_base;
	void __iomem *mem_base;
	struct hclge_comm_cmq cmq;
	unsigned long comm_state;
};

#endif
