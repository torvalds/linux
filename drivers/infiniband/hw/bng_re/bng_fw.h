/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (c) 2025 Broadcom.

#ifndef __BNG_FW_H__
#define __BNG_FW_H__

#include "bng_tlv.h"

/* FW DB related */
#define BNG_FW_CMDQ_TRIG_VAL		1
#define BNG_FW_COMM_PCI_BAR_REGION	0
#define BNG_FW_COMM_CONS_PCI_BAR_REGION	2
#define BNG_FW_DBR_BASE_PAGE_SHIFT	12
#define BNG_FW_COMM_SIZE		0x104
#define BNG_FW_COMM_BASE_OFFSET		0x600
#define BNG_FW_COMM_TRIG_OFFSET		0x100
#define BNG_FW_PF_VF_COMM_PROD_OFFSET	0xc
#define BNG_FW_CREQ_DB_LEN		8

/* CREQ */
#define BNG_FW_CREQE_MAX_CNT		(64 * 1024)
#define BNG_FW_CREQE_UNITS		16
#define BNG_FW_CREQ_ENTRY_POLL_BUDGET	0x100
#define BNG_FW_CREQ_CMP_VALID(hdr, pass)			\
	(!!((hdr)->v & CREQ_BASE_V) ==				\
	   !((pass) & BNG_RE_FLAG_EPOCH_CONS_MASK))
#define BNG_FW_CREQ_ENTRY_POLL_BUDGET	0x100

/* CMDQ */
struct bng_fw_cmdqe {
	u8	data[16];
};

#define BNG_FW_CMDQE_MAX_CNT		8192
#define BNG_FW_CMDQE_UNITS		sizeof(struct bng_fw_cmdqe)
#define BNG_FW_CMDQE_BYTES(depth)	((depth) * BNG_FW_CMDQE_UNITS)

#define BNG_FW_MAX_COOKIE_VALUE		(BNG_FW_CMDQE_MAX_CNT - 1)
#define BNG_FW_CMD_IS_BLOCKING		0x8000

/* Crsq buf is 1024-Byte */
struct bng_re_crsbe {
	u8			data[1024];
};


static inline u32 bng_fw_cmdqe_npages(u32 depth)
{
	u32 npages;

	npages = BNG_FW_CMDQE_BYTES(depth) / PAGE_SIZE;
	if (BNG_FW_CMDQE_BYTES(depth) % PAGE_SIZE)
		npages++;
	return npages;
}

static inline u32 bng_fw_cmdqe_page_size(u32 depth)
{
	return (bng_fw_cmdqe_npages(depth) * PAGE_SIZE);
}
struct bng_re_cmdq_mbox {
	struct bng_re_reg_desc		reg;
	void __iomem			*prod;
	void __iomem			*db;
};

/* HWQ */
struct bng_re_cmdq_ctx {
	struct bng_re_hwq		hwq;
	struct bng_re_cmdq_mbox		cmdq_mbox;
	unsigned long			flags;
#define FIRMWARE_INITIALIZED_FLAG	(0)
#define FIRMWARE_STALL_DETECTED		(3)
#define FIRMWARE_FIRST_FLAG		(31)
	wait_queue_head_t		waitq;
	u32				seq_num;
};

struct bng_re_creq_db {
	struct bng_re_reg_desc	reg;
	struct bng_re_db_info	dbinfo;
};

struct bng_re_creq_stat {
	u64	creq_qp_event_processed;
	u64	creq_func_event_processed;
};

struct bng_re_creq_ctx {
	struct bng_re_hwq		hwq;
	struct bng_re_creq_db		creq_db;
	struct bng_re_creq_stat		stats;
	struct tasklet_struct		creq_tasklet;
	u16				ring_id;
	int				msix_vec;
	bool				irq_handler_avail;
	char				*irq_name;
};

struct bng_re_crsqe {
	struct creq_qp_event	*resp;
	u32			req_size;
	/* Free slots at the time of submission */
	u32			free_slots;
	u8			opcode;
	bool			is_waiter_alive;
	bool			is_in_used;
};

struct bng_re_rcfw_sbuf {
	void *sb;
	dma_addr_t dma_addr;
	u32 size;
};

/* RoCE FW Communication Channels */
struct bng_re_rcfw {
	struct pci_dev		*pdev;
	struct bng_re_res	*res;
	struct bng_re_cmdq_ctx	cmdq;
	struct bng_re_creq_ctx	creq;
	struct bng_re_crsqe	*crsqe_tbl;
	/* To synchronize the qp-handle hash table */
	spinlock_t		tbl_lock;
	u32			cmdq_depth;
	/* cached from chip cctx for quick reference in slow path */
	u16			max_timeout;
	atomic_t		rcfw_intr_enabled;
};

struct bng_re_cmdqmsg {
	struct cmdq_base	*req;
	struct creq_base	*resp;
	void			*sb;
	u32			req_sz;
	u32			res_sz;
	u8			block;
};

static inline void bng_re_rcfw_cmd_prep(struct cmdq_base *req,
					u8 opcode, u8 cmd_size)
{
	req->opcode = opcode;
	req->cmd_size = cmd_size;
}

static inline void bng_re_fill_cmdqmsg(struct bng_re_cmdqmsg *msg,
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

/* Get the number of command units required for the req. The
 * function returns correct value only if called before
 * setting using bng_re_set_cmd_slots
 */
static inline u32 bng_re_get_cmd_slots(struct cmdq_base *req)
{
	u32 cmd_units = 0;

	if (HAS_TLV_HEADER(req)) {
		struct roce_tlv *tlv_req = (struct roce_tlv *)req;

		cmd_units = tlv_req->total_size;
	} else {
		cmd_units = (req->cmd_size + BNG_FW_CMDQE_UNITS - 1) /
			    BNG_FW_CMDQE_UNITS;
	}

	return cmd_units;
}

static inline u32 bng_re_set_cmd_slots(struct cmdq_base *req)
{
	u32 cmd_byte = 0;

	if (HAS_TLV_HEADER(req)) {
		struct roce_tlv *tlv_req = (struct roce_tlv *)req;

		cmd_byte = tlv_req->total_size * BNG_FW_CMDQE_UNITS;
	} else {
		cmd_byte = req->cmd_size;
		req->cmd_size = (req->cmd_size + BNG_FW_CMDQE_UNITS - 1) /
				 BNG_FW_CMDQE_UNITS;
	}

	return cmd_byte;
}

void bng_re_free_rcfw_channel(struct bng_re_rcfw *rcfw);
int bng_re_alloc_fw_channel(struct bng_re_res *res,
			    struct bng_re_rcfw *rcfw);
int bng_re_enable_fw_channel(struct bng_re_rcfw *rcfw,
			     int msix_vector,
			     int cp_bar_reg_off);
void bng_re_disable_rcfw_channel(struct bng_re_rcfw *rcfw);
int bng_re_rcfw_start_irq(struct bng_re_rcfw *rcfw, int msix_vector,
			  bool need_init);
void bng_re_rcfw_stop_irq(struct bng_re_rcfw *rcfw, bool kill);
int bng_re_rcfw_send_message(struct bng_re_rcfw *rcfw,
			     struct bng_re_cmdqmsg *msg);
int bng_re_init_rcfw(struct bng_re_rcfw *rcfw,
		     struct bng_re_stats *stats_ctx);
int bng_re_deinit_rcfw(struct bng_re_rcfw *rcfw);
#endif
