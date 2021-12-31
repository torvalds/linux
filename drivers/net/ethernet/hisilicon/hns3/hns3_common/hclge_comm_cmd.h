/* SPDX-License-Identifier: GPL-2.0+ */
// Copyright (c) 2021-2021 Hisilicon Limited.

#ifndef __HCLGE_COMM_CMD_H
#define __HCLGE_COMM_CMD_H
#include <linux/types.h>

#include "hnae3.h"

#define HCLGE_COMM_CMD_FLAG_NO_INTR		BIT(4)

#define HCLGE_COMM_SEND_SYNC(flag) \
	((flag) & HCLGE_COMM_CMD_FLAG_NO_INTR)

#define HCLGE_COMM_NIC_CSQ_TAIL_REG		0x27010
#define HCLGE_COMM_NIC_CSQ_HEAD_REG		0x27014

enum hclge_comm_cmd_return_status {
	HCLGE_COMM_CMD_EXEC_SUCCESS	= 0,
	HCLGE_COMM_CMD_NO_AUTH		= 1,
	HCLGE_COMM_CMD_NOT_SUPPORTED	= 2,
	HCLGE_COMM_CMD_QUEUE_FULL	= 3,
	HCLGE_COMM_CMD_NEXT_ERR		= 4,
	HCLGE_COMM_CMD_UNEXE_ERR	= 5,
	HCLGE_COMM_CMD_PARA_ERR		= 6,
	HCLGE_COMM_CMD_RESULT_ERR	= 7,
	HCLGE_COMM_CMD_TIMEOUT		= 8,
	HCLGE_COMM_CMD_HILINK_ERR	= 9,
	HCLGE_COMM_CMD_QUEUE_ILLEGAL	= 10,
	HCLGE_COMM_CMD_INVALID		= 11,
};

enum hclge_comm_special_cmd {
	HCLGE_COMM_OPC_STATS_64_BIT		= 0x0030,
	HCLGE_COMM_OPC_STATS_32_BIT		= 0x0031,
	HCLGE_COMM_OPC_STATS_MAC		= 0x0032,
	HCLGE_COMM_OPC_STATS_MAC_ALL		= 0x0034,
	HCLGE_COMM_OPC_QUERY_32_BIT_REG		= 0x0041,
	HCLGE_COMM_OPC_QUERY_64_BIT_REG		= 0x0042,
	HCLGE_COMM_QUERY_CLEAR_MPF_RAS_INT	= 0x1511,
	HCLGE_COMM_QUERY_CLEAR_PF_RAS_INT	= 0x1512,
	HCLGE_COMM_QUERY_CLEAR_ALL_MPF_MSIX_INT	= 0x1514,
	HCLGE_COMM_QUERY_CLEAR_ALL_PF_MSIX_INT	= 0x1515,
	HCLGE_COMM_QUERY_ALL_ERR_INFO		= 0x1517,
};

enum hclge_comm_cmd_state {
	HCLGE_COMM_STATE_CMD_DISABLE,
};

struct hclge_comm_errcode {
	u32 imp_errcode;
	int common_errno;
};

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

static inline void hclge_comm_write_reg(void __iomem *base, u32 reg, u32 value)
{
	writel(value, base + reg);
}

static inline u32 hclge_comm_read_reg(u8 __iomem *base, u32 reg)
{
	u8 __iomem *reg_addr = READ_ONCE(base);

	return readl(reg_addr + reg);
}

#define hclge_comm_write_dev(a, reg, value) \
	hclge_comm_write_reg((a)->io_base, reg, value)
#define hclge_comm_read_dev(a, reg) \
	hclge_comm_read_reg((a)->io_base, reg)

int hclge_comm_cmd_send(struct hclge_comm_hw *hw, struct hclge_desc *desc,
			int num, bool is_pf);

#endif
