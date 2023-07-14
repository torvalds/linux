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

#include "qplib_tlv.h"

#define RCFW_CMDQ_TRIG_VAL		1
#define RCFW_COMM_PCI_BAR_REGION	0
#define RCFW_COMM_CONS_PCI_BAR_REGION	2
#define RCFW_COMM_BASE_OFFSET		0x600
#define RCFW_PF_VF_COMM_PROD_OFFSET	0xc
#define RCFW_COMM_TRIG_OFFSET		0x100
#define RCFW_COMM_SIZE			0x104

#define RCFW_DBR_PCI_BAR_REGION		2
#define RCFW_DBR_BASE_PAGE_SHIFT	12
#define RCFW_FW_STALL_MAX_TIMEOUT	40

/* Cmdq contains a fix number of a 16-Byte slots */
struct bnxt_qplib_cmdqe {
	u8		data[16];
};

#define BNXT_QPLIB_CMDQE_UNITS		sizeof(struct bnxt_qplib_cmdqe)

static inline void bnxt_qplib_rcfw_cmd_prep(struct cmdq_base *req,
					    u8 opcode, u8 cmd_size)
{
	req->opcode = opcode;
	req->cmd_size = cmd_size;
}

/* Shadow queue depth for non blocking command */
#define RCFW_CMD_NON_BLOCKING_SHADOW_QD	64
#define RCFW_CMD_WAIT_TIME_MS		20000 /* 20 Seconds timeout */

/* CMDQ elements */
#define BNXT_QPLIB_CMDQE_MAX_CNT	8192
#define BNXT_QPLIB_CMDQE_BYTES(depth)	((depth) * BNXT_QPLIB_CMDQE_UNITS)

static inline u32 bnxt_qplib_cmdqe_npages(u32 depth)
{
	u32 npages;

	npages = BNXT_QPLIB_CMDQE_BYTES(depth) / PAGE_SIZE;
	if (BNXT_QPLIB_CMDQE_BYTES(depth) % PAGE_SIZE)
		npages++;
	return npages;
}

static inline u32 bnxt_qplib_cmdqe_page_size(u32 depth)
{
	return (bnxt_qplib_cmdqe_npages(depth) * PAGE_SIZE);
}

/* Get the number of command units required for the req. The
 * function returns correct value only if called before
 * setting using bnxt_qplib_set_cmd_slots
 */
static inline u32 bnxt_qplib_get_cmd_slots(struct cmdq_base *req)
{
	u32 cmd_units = 0;

	if (HAS_TLV_HEADER(req)) {
		struct roce_tlv *tlv_req = (struct roce_tlv *)req;

		cmd_units = tlv_req->total_size;
	} else {
		cmd_units = (req->cmd_size + BNXT_QPLIB_CMDQE_UNITS - 1) /
			    BNXT_QPLIB_CMDQE_UNITS;
	}

	return cmd_units;
}

static inline u32 bnxt_qplib_set_cmd_slots(struct cmdq_base *req)
{
	u32 cmd_byte = 0;

	if (HAS_TLV_HEADER(req)) {
		struct roce_tlv *tlv_req = (struct roce_tlv *)req;

		cmd_byte = tlv_req->total_size * BNXT_QPLIB_CMDQE_UNITS;
	} else {
		cmd_byte = req->cmd_size;
		req->cmd_size = (req->cmd_size + BNXT_QPLIB_CMDQE_UNITS - 1) /
				 BNXT_QPLIB_CMDQE_UNITS;
	}

	return cmd_byte;
}

#define RCFW_MAX_COOKIE_VALUE		(BNXT_QPLIB_CMDQE_MAX_CNT - 1)
#define RCFW_CMD_IS_BLOCKING		0x8000

#define HWRM_VERSION_DEV_ATTR_MAX_DPI  0x1000A0000000DULL

/* Crsq buf is 1024-Byte */
struct bnxt_qplib_crsbe {
	u8			data[1024];
};

/* CREQ */
/* Allocate 1 per QP for async error notification for now */
#define BNXT_QPLIB_CREQE_MAX_CNT	(64 * 1024)
#define BNXT_QPLIB_CREQE_UNITS		16	/* 16-Bytes per prod unit */
#define CREQ_CMP_VALID(hdr, raw_cons, cp_bit)			\
	(!!((hdr)->v & CREQ_BASE_V) ==				\
	   !((raw_cons) & (cp_bit)))
#define CREQ_ENTRY_POLL_BUDGET		0x100

/* HWQ */
typedef int (*aeq_handler_t)(struct bnxt_qplib_rcfw *, void *, void *);

struct bnxt_qplib_crsqe {
	struct creq_qp_event	*resp;
	u32			req_size;
	/* Free slots at the time of submission */
	u32			free_slots;
	u8			opcode;
	bool			is_waiter_alive;
	bool			is_internal_cmd;
	bool			is_in_used;
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

#define FIRMWARE_INITIALIZED_FLAG	(0)
#define FIRMWARE_FIRST_FLAG		(31)
#define FIRMWARE_STALL_DETECTED		(3)
#define ERR_DEVICE_DETACHED             (4)

struct bnxt_qplib_cmdq_mbox {
	struct bnxt_qplib_reg_desc	reg;
	void __iomem			*prod;
	void __iomem			*db;
};

struct bnxt_qplib_cmdq_ctx {
	struct bnxt_qplib_hwq		hwq;
	struct bnxt_qplib_cmdq_mbox	cmdq_mbox;
	wait_queue_head_t		waitq;
	unsigned long			flags;
	unsigned long			last_seen;
	u32				seq_num;
};

struct bnxt_qplib_creq_db {
	struct bnxt_qplib_reg_desc	reg;
	struct bnxt_qplib_db_info	dbinfo;
};

struct bnxt_qplib_creq_stat {
	u64	creq_qp_event_processed;
	u64	creq_func_event_processed;
};

struct bnxt_qplib_creq_ctx {
	struct bnxt_qplib_hwq		hwq;
	struct bnxt_qplib_creq_db	creq_db;
	struct bnxt_qplib_creq_stat	stats;
	struct tasklet_struct		creq_tasklet;
	aeq_handler_t			aeq_handler;
	u16				ring_id;
	int				msix_vec;
	bool				requested; /*irq handler installed */
	char				*irq_name;
};

/* RCFW Communication Channels */
struct bnxt_qplib_rcfw {
	struct pci_dev		*pdev;
	struct bnxt_qplib_res	*res;
	struct bnxt_qplib_cmdq_ctx	cmdq;
	struct bnxt_qplib_creq_ctx	creq;
	struct bnxt_qplib_crsqe		*crsqe_tbl;
	int qp_tbl_size;
	struct bnxt_qplib_qp_node *qp_tbl;
	u64 oos_prev;
	u32 init_oos_stats;
	u32 cmdq_depth;
	atomic_t rcfw_intr_enabled;
	struct semaphore rcfw_inflight;
	atomic_t timeout_send;
	/* cached from chip cctx for quick reference in slow path */
	u16 max_timeout;
};

struct bnxt_qplib_cmdqmsg {
	struct cmdq_base	*req;
	struct creq_base	*resp;
	void			*sb;
	u32			req_sz;
	u32			res_sz;
	u8			block;
};

static inline void bnxt_qplib_fill_cmdqmsg(struct bnxt_qplib_cmdqmsg *msg,
					   void *req, void *resp, void *sb,
					   u32 req_sz, u32 res_sz, u8 block)
{
	msg->req = req;
	msg->resp = resp;
	msg->sb = sb;
	msg->req_sz = req_sz;
	msg->res_sz = res_sz;
	msg->block = block;
}

void bnxt_qplib_free_rcfw_channel(struct bnxt_qplib_rcfw *rcfw);
int bnxt_qplib_alloc_rcfw_channel(struct bnxt_qplib_res *res,
				  struct bnxt_qplib_rcfw *rcfw,
				  struct bnxt_qplib_ctx *ctx,
				  int qp_tbl_sz);
void bnxt_qplib_rcfw_stop_irq(struct bnxt_qplib_rcfw *rcfw, bool kill);
void bnxt_qplib_disable_rcfw_channel(struct bnxt_qplib_rcfw *rcfw);
int bnxt_qplib_rcfw_start_irq(struct bnxt_qplib_rcfw *rcfw, int msix_vector,
			      bool need_init);
int bnxt_qplib_enable_rcfw_channel(struct bnxt_qplib_rcfw *rcfw,
				   int msix_vector,
				   int cp_bar_reg_off,
				   aeq_handler_t aeq_handler);

struct bnxt_qplib_rcfw_sbuf *bnxt_qplib_rcfw_alloc_sbuf(
				struct bnxt_qplib_rcfw *rcfw,
				u32 size);
void bnxt_qplib_rcfw_free_sbuf(struct bnxt_qplib_rcfw *rcfw,
			       struct bnxt_qplib_rcfw_sbuf *sbuf);
int bnxt_qplib_rcfw_send_message(struct bnxt_qplib_rcfw *rcfw,
				 struct bnxt_qplib_cmdqmsg *msg);

int bnxt_qplib_deinit_rcfw(struct bnxt_qplib_rcfw *rcfw);
int bnxt_qplib_init_rcfw(struct bnxt_qplib_rcfw *rcfw,
			 struct bnxt_qplib_ctx *ctx, int is_virtfn);
void bnxt_qplib_mark_qp_error(void *qp_handle);
static inline u32 map_qp_id_to_tbl_indx(u32 qid, struct bnxt_qplib_rcfw *rcfw)
{
	/* Last index of the qp_tbl is for QP1 ie. qp_tbl_size - 1*/
	return (qid == 1) ? rcfw->qp_tbl_size - 1 : qid % rcfw->qp_tbl_size - 2;
}
#endif /* __BNXT_QPLIB_RCFW_H__ */
