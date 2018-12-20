/*
 * Broadcom NetXtreme-E RoCE driver.
 *
 * Copyright (c) 2016 - 2017, Broadcom. All rights reserved.  The term
 * Broadcom refers to Broadcom Limited and/or its subsidiaries.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Description: RDMA Controller HW interface (header)
 */

#ifndef __BNXT_QPLIB_RCFW_H__
#define __BNXT_QPLIB_RCFW_H__

#define RCFW_CMDQ_TRIG_VAL		1
#define RCFW_COMM_PCI_BAR_REGION	0
#define RCFW_COMM_CONS_PCI_BAR_REGION	2
#define RCFW_COMM_BASE_OFFSET		0x600
#define RCFW_PF_COMM_PROD_OFFSET	0xc
#define RCFW_VF_COMM_PROD_OFFSET	0xc
#define RCFW_COMM_TRIG_OFFSET		0x100
#define RCFW_COMM_SIZE			0x104

#define RCFW_DBR_PCI_BAR_REGION		2
#define RCFW_DBR_BASE_PAGE_SHIFT	12

#define RCFW_CMD_PREP(req, CMD, cmd_flags)				\
	do {								\
		memset(&(req), 0, sizeof((req)));			\
		(req).opcode = CMDQ_BASE_OPCODE_##CMD;			\
		(req).cmd_size = (sizeof((req)) +			\
				BNXT_QPLIB_CMDQE_UNITS - 1) /		\
				BNXT_QPLIB_CMDQE_UNITS;			\
		(req).flags = cpu_to_le16(cmd_flags);			\
	} while (0)

#define RCFW_CMD_WAIT_TIME_MS		20000 /* 20 Seconds timeout */

/* CMDQ elements */
#define BNXT_QPLIB_CMDQE_MAX_CNT	256
#define BNXT_QPLIB_CMDQE_UNITS		sizeof(struct bnxt_qplib_cmdqe)
#define BNXT_QPLIB_CMDQE_CNT_PER_PG	(PAGE_SIZE / BNXT_QPLIB_CMDQE_UNITS)

#define MAX_CMDQ_IDX			(BNXT_QPLIB_CMDQE_MAX_CNT - 1)
#define MAX_CMDQ_IDX_PER_PG		(BNXT_QPLIB_CMDQE_CNT_PER_PG - 1)

#define RCFW_MAX_OUTSTANDING_CMD	BNXT_QPLIB_CMDQE_MAX_CNT
#define RCFW_MAX_COOKIE_VALUE		0x7FFF
#define RCFW_CMD_IS_BLOCKING		0x8000
#define RCFW_BLOCKED_CMD_WAIT_COUNT	0x4E20

/* Cmdq contains a fix number of a 16-Byte slots */
struct bnxt_qplib_cmdqe {
	u8		data[16];
};

static inline u32 get_cmdq_pg(u32 val)
{
	return (val & ~MAX_CMDQ_IDX_PER_PG) / BNXT_QPLIB_CMDQE_CNT_PER_PG;
}

static inline u32 get_cmdq_idx(u32 val)
{
	return val & MAX_CMDQ_IDX_PER_PG;
}

/* Crsq buf is 1024-Byte */
struct bnxt_qplib_crsbe {
	u8			data[1024];
};

/* CREQ */
/* Allocate 1 per QP for async error notification for now */
#define BNXT_QPLIB_CREQE_MAX_CNT	(64 * 1024)
#define BNXT_QPLIB_CREQE_UNITS		16	/* 16-Bytes per prod unit */
#define BNXT_QPLIB_CREQE_CNT_PER_PG	(PAGE_SIZE / BNXT_QPLIB_CREQE_UNITS)

#define MAX_CREQ_IDX			(BNXT_QPLIB_CREQE_MAX_CNT - 1)
#define MAX_CREQ_IDX_PER_PG		(BNXT_QPLIB_CREQE_CNT_PER_PG - 1)

static inline u32 get_creq_pg(u32 val)
{
	return (val & ~MAX_CREQ_IDX_PER_PG) / BNXT_QPLIB_CREQE_CNT_PER_PG;
}

static inline u32 get_creq_idx(u32 val)
{
	return val & MAX_CREQ_IDX_PER_PG;
}

#define BNXT_QPLIB_CREQE_PER_PG	(PAGE_SIZE / sizeof(struct creq_base))

#define CREQ_CMP_VALID(hdr, raw_cons, cp_bit)			\
	(!!((hdr)->v & CREQ_BASE_V) ==				\
	   !((raw_cons) & (cp_bit)))

#define CREQ_DB_KEY_CP			(0x2 << CMPL_DOORBELL_KEY_SFT)
#define CREQ_DB_IDX_VALID		CMPL_DOORBELL_IDX_VALID
#define CREQ_DB_IRQ_DIS			CMPL_DOORBELL_MASK
#define CREQ_DB_CP_FLAGS_REARM		(CREQ_DB_KEY_CP |	\
					 CREQ_DB_IDX_VALID)
#define CREQ_DB_CP_FLAGS		(CREQ_DB_KEY_CP |	\
					 CREQ_DB_IDX_VALID |	\
					 CREQ_DB_IRQ_DIS)
#define CREQ_DB_REARM(db, raw_cons, cp_bit)			\
	writel(CREQ_DB_CP_FLAGS_REARM | ((raw_cons) & ((cp_bit) - 1)), db)
#define CREQ_DB(db, raw_cons, cp_bit)				\
	writel(CREQ_DB_CP_FLAGS | ((raw_cons) & ((cp_bit) - 1)), db)

#define CREQ_ENTRY_POLL_BUDGET		0x100

/* HWQ */

struct bnxt_qplib_crsq {
	struct creq_qp_event	*resp;
	u32			req_size;
};

struct bnxt_qplib_rcfw_sbuf {
	void *sb;
	dma_addr_t dma_addr;
	u32 size;
};

struct bnxt_qplib_qp_node {
	u32 qp_id;              /* QP id */
	void *qp_handle;        /* ptr to qplib_qp */
};

#define BNXT_QPLIB_OOS_COUNT_MASK 0xFFFFFFFF

/* RCFW Communication Channels */
struct bnxt_qplib_rcfw {
	struct pci_dev		*pdev;
	int			vector;
	struct tasklet_struct	worker;
	bool			requested;
	unsigned long		*cmdq_bitmap;
	u32			bmap_size;
	unsigned long		flags;
#define FIRMWARE_INITIALIZED_FLAG	0
#define FIRMWARE_FIRST_FLAG		31
#define FIRMWARE_TIMED_OUT		3
	wait_queue_head_t	waitq;
	int			(*aeq_handler)(struct bnxt_qplib_rcfw *,
					       void *, void *);
	u32			seq_num;

	/* Bar region info */
	void __iomem		*cmdq_bar_reg_iomem;
	u16			cmdq_bar_reg;
	u16			cmdq_bar_reg_prod_off;
	u16			cmdq_bar_reg_trig_off;
	u16			creq_ring_id;
	u16			creq_bar_reg;
	void __iomem		*creq_bar_reg_iomem;

	/* Cmd-Resp and Async Event notification queue */
	struct bnxt_qplib_hwq	creq;
	u64			creq_qp_event_processed;
	u64			creq_func_event_processed;

	/* Actual Cmd and Resp Queues */
	struct bnxt_qplib_hwq	cmdq;
	struct bnxt_qplib_crsq	*crsqe_tbl;
	int qp_tbl_size;
	struct bnxt_qplib_qp_node *qp_tbl;
	u64 oos_prev;
	u32 init_oos_stats;
};

void bnxt_qplib_free_rcfw_channel(struct bnxt_qplib_rcfw *rcfw);
int bnxt_qplib_alloc_rcfw_channel(struct pci_dev *pdev,
				  struct bnxt_qplib_rcfw *rcfw, int qp_tbl_sz);
void bnxt_qplib_rcfw_stop_irq(struct bnxt_qplib_rcfw *rcfw, bool kill);
void bnxt_qplib_disable_rcfw_channel(struct bnxt_qplib_rcfw *rcfw);
int bnxt_qplib_rcfw_start_irq(struct bnxt_qplib_rcfw *rcfw, int msix_vector,
			      bool need_init);
int bnxt_qplib_enable_rcfw_channel(struct pci_dev *pdev,
				   struct bnxt_qplib_rcfw *rcfw,
				   int msix_vector,
				   int cp_bar_reg_off, int virt_fn,
				   int (*aeq_handler)(struct bnxt_qplib_rcfw *,
						      void *aeqe, void *obj));

struct bnxt_qplib_rcfw_sbuf *bnxt_qplib_rcfw_alloc_sbuf(
				struct bnxt_qplib_rcfw *rcfw,
				u32 size);
void bnxt_qplib_rcfw_free_sbuf(struct bnxt_qplib_rcfw *rcfw,
			       struct bnxt_qplib_rcfw_sbuf *sbuf);
int bnxt_qplib_rcfw_send_message(struct bnxt_qplib_rcfw *rcfw,
				 struct cmdq_base *req, struct creq_base *resp,
				 void *sbuf, u8 is_block);

int bnxt_qplib_deinit_rcfw(struct bnxt_qplib_rcfw *rcfw);
int bnxt_qplib_init_rcfw(struct bnxt_qplib_rcfw *rcfw,
			 struct bnxt_qplib_ctx *ctx, int is_virtfn);
void bnxt_qplib_mark_qp_error(void *qp_handle);
#endif /* __BNXT_QPLIB_RCFW_H__ */
