/*
 * Copyright (c) 2016-2017 Hisilicon Limited.
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

#ifndef _HNS_ROCE_HW_V2_H
#define _HNS_ROCE_HW_V2_H

#define HNS_ROCE_CMQ_TX_TIMEOUT			200

#define HNS_ROCE_CMD_FLAG_IN_VALID_SHIFT	0
#define HNS_ROCE_CMD_FLAG_OUT_VALID_SHIFT	1
#define HNS_ROCE_CMD_FLAG_NEXT_SHIFT		2
#define HNS_ROCE_CMD_FLAG_WR_OR_RD_SHIFT	3
#define HNS_ROCE_CMD_FLAG_NO_INTR_SHIFT		4
#define HNS_ROCE_CMD_FLAG_ERR_INTR_SHIFT	5

#define HNS_ROCE_CMD_FLAG_IN		BIT(HNS_ROCE_CMD_FLAG_IN_VALID_SHIFT)
#define HNS_ROCE_CMD_FLAG_OUT		BIT(HNS_ROCE_CMD_FLAG_OUT_VALID_SHIFT)
#define HNS_ROCE_CMD_FLAG_NEXT		BIT(HNS_ROCE_CMD_FLAG_NEXT_SHIFT)
#define HNS_ROCE_CMD_FLAG_WR		BIT(HNS_ROCE_CMD_FLAG_WR_OR_RD_SHIFT)
#define HNS_ROCE_CMD_FLAG_NO_INTR	BIT(HNS_ROCE_CMD_FLAG_NO_INTR_SHIFT)
#define HNS_ROCE_CMD_FLAG_ERR_INTR	BIT(HNS_ROCE_CMD_FLAG_ERR_INTR_SHIFT)

#define HNS_ROCE_CMQ_DESC_NUM_S		3
#define HNS_ROCE_CMQ_EN_B		16
#define HNS_ROCE_CMQ_ENABLE		BIT(HNS_ROCE_CMQ_EN_B)

/* CMQ command */
enum hns_roce_opcode_type {
	HNS_ROCE_OPC_QUERY_HW_VER			= 0x8000,
	HNS_ROCE_OPC_CFG_GLOBAL_PARAM			= 0x8001,
	HNS_ROCE_OPC_ALLOC_PF_RES			= 0x8004,
	HNS_ROCE_OPC_QUERY_PF_RES			= 0x8400,
	HNS_ROCE_OPC_ALLOC_VF_RES			= 0x8401,
	HNS_ROCE_OPC_CFG_BT_ATTR			= 0x8506,
};

enum {
	TYPE_CRQ,
	TYPE_CSQ,
};

enum hns_roce_cmd_return_status {
	CMD_EXEC_SUCCESS	= 0,
	CMD_NO_AUTH		= 1,
	CMD_NOT_EXEC		= 2,
	CMD_QUEUE_FULL		= 3,
};

struct hns_roce_cmq_desc {
	u16 opcode;
	u16 flag;
	u16 retval;
	u16 rsv;
	u32 data[6];
};

struct hns_roce_v2_cmq_ring {
	dma_addr_t desc_dma_addr;
	struct hns_roce_cmq_desc *desc;
	u32 head;
	u32 tail;

	u16 buf_size;
	u16 desc_num;
	int next_to_use;
	int next_to_clean;
	u8 flag;
	spinlock_t lock; /* command queue lock */
};

struct hns_roce_v2_cmq {
	struct hns_roce_v2_cmq_ring csq;
	struct hns_roce_v2_cmq_ring crq;
	u16 tx_timeout;
	u16 last_status;
};

struct hns_roce_v2_priv {
	struct hns_roce_v2_cmq cmq;
};

#endif
