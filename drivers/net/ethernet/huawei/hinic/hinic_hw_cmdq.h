/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Huawei HiNIC PCI Express Linux driver
 * Copyright(c) 2017 Huawei Technologies Co., Ltd
 */

#ifndef HINIC_CMDQ_H
#define HINIC_CMDQ_H

#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/pci.h>

#include "hinic_hw_if.h"
#include "hinic_hw_wq.h"

#define HINIC_CMDQ_CTXT_CURR_WQE_PAGE_PFN_SHIFT         0
#define HINIC_CMDQ_CTXT_EQ_ID_SHIFT                     56
#define HINIC_CMDQ_CTXT_CEQ_ARM_SHIFT                   61
#define HINIC_CMDQ_CTXT_CEQ_EN_SHIFT                    62
#define HINIC_CMDQ_CTXT_WRAPPED_SHIFT                   63

#define HINIC_CMDQ_CTXT_CURR_WQE_PAGE_PFN_MASK          0xFFFFFFFFFFFFF
#define HINIC_CMDQ_CTXT_EQ_ID_MASK                      0x1F
#define HINIC_CMDQ_CTXT_CEQ_ARM_MASK                    0x1
#define HINIC_CMDQ_CTXT_CEQ_EN_MASK                     0x1
#define HINIC_CMDQ_CTXT_WRAPPED_MASK                    0x1

#define HINIC_CMDQ_CTXT_PAGE_INFO_SET(val, member)      \
			(((u64)(val) & HINIC_CMDQ_CTXT_##member##_MASK) \
			 << HINIC_CMDQ_CTXT_##member##_SHIFT)

#define HINIC_CMDQ_CTXT_PAGE_INFO_GET(val, member)	\
			(((u64)(val) >> HINIC_CMDQ_CTXT_##member##_SHIFT) \
			 & HINIC_CMDQ_CTXT_##member##_MASK)

#define HINIC_CMDQ_CTXT_PAGE_INFO_CLEAR(val, member)    \
			((val) & (~((u64)HINIC_CMDQ_CTXT_##member##_MASK \
			 << HINIC_CMDQ_CTXT_##member##_SHIFT)))

#define HINIC_CMDQ_CTXT_WQ_BLOCK_PFN_SHIFT              0
#define HINIC_CMDQ_CTXT_CI_SHIFT                        52

#define HINIC_CMDQ_CTXT_WQ_BLOCK_PFN_MASK               0xFFFFFFFFFFFFF
#define HINIC_CMDQ_CTXT_CI_MASK                         0xFFF

#define HINIC_CMDQ_CTXT_BLOCK_INFO_SET(val, member)     \
			(((u64)(val) & HINIC_CMDQ_CTXT_##member##_MASK) \
			 << HINIC_CMDQ_CTXT_##member##_SHIFT)

#define HINIC_CMDQ_CTXT_BLOCK_INFO_GET(val, member)	\
			(((u64)(val) >> HINIC_CMDQ_CTXT_##member##_SHIFT) \
			& HINIC_CMDQ_CTXT_##member##_MASK)

#define HINIC_CMDQ_CTXT_BLOCK_INFO_CLEAR(val, member)   \
			((val) & (~((u64)HINIC_CMDQ_CTXT_##member##_MASK \
			 << HINIC_CMDQ_CTXT_##member##_SHIFT)))

#define HINIC_SAVED_DATA_ARM_SHIFT                      31

#define HINIC_SAVED_DATA_ARM_MASK                       0x1

#define HINIC_SAVED_DATA_SET(val, member)               \
			(((u32)(val) & HINIC_SAVED_DATA_##member##_MASK) \
			 << HINIC_SAVED_DATA_##member##_SHIFT)

#define HINIC_SAVED_DATA_GET(val, member)               \
			(((val) >> HINIC_SAVED_DATA_##member##_SHIFT) \
			 & HINIC_SAVED_DATA_##member##_MASK)

#define HINIC_SAVED_DATA_CLEAR(val, member)             \
			((val) & (~(HINIC_SAVED_DATA_##member##_MASK \
			 << HINIC_SAVED_DATA_##member##_SHIFT)))

#define HINIC_CMDQ_DB_INFO_HI_PROD_IDX_SHIFT            0
#define HINIC_CMDQ_DB_INFO_PATH_SHIFT                   23
#define HINIC_CMDQ_DB_INFO_CMDQ_TYPE_SHIFT              24
#define HINIC_CMDQ_DB_INFO_DB_TYPE_SHIFT                27

#define HINIC_CMDQ_DB_INFO_HI_PROD_IDX_MASK             0xFF
#define HINIC_CMDQ_DB_INFO_PATH_MASK                    0x1
#define HINIC_CMDQ_DB_INFO_CMDQ_TYPE_MASK               0x7
#define HINIC_CMDQ_DB_INFO_DB_TYPE_MASK                 0x1F

#define HINIC_CMDQ_DB_INFO_SET(val, member)             \
			(((u32)(val) & HINIC_CMDQ_DB_INFO_##member##_MASK) \
			 << HINIC_CMDQ_DB_INFO_##member##_SHIFT)

#define HINIC_CMDQ_BUF_SIZE             2048

#define HINIC_CMDQ_BUF_HW_RSVD          8
#define HINIC_CMDQ_MAX_DATA_SIZE        (HINIC_CMDQ_BUF_SIZE - \
					 HINIC_CMDQ_BUF_HW_RSVD)

enum hinic_cmdq_type {
	HINIC_CMDQ_SYNC,

	HINIC_MAX_CMDQ_TYPES,
};

enum hinic_set_arm_qtype {
	HINIC_SET_ARM_CMDQ,
};

enum hinic_cmd_ack_type {
	HINIC_CMD_ACK_TYPE_CMDQ,
};

struct hinic_cmdq_buf {
	void            *buf;
	dma_addr_t      dma_addr;
	size_t          size;
};

struct hinic_cmdq_arm_bit {
	u32     q_type;
	u32     q_id;
};

struct hinic_cmdq_ctxt_info {
	u64     curr_wqe_page_pfn;
	u64     wq_block_pfn;
};

struct hinic_cmdq_ctxt {
	u8      status;
	u8      version;
	u8      rsvd0[6];

	u16     func_idx;
	u8      cmdq_type;
	u8      ppf_idx;

	u8      rsvd2[4];

	struct hinic_cmdq_ctxt_info ctxt_info;
};

struct hinic_cmdq {
	struct hinic_hwdev      *hwdev;

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

	struct dma_pool         *cmdq_buf_pool;

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
