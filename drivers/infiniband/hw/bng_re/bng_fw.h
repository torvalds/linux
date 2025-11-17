/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (c) 2025 Broadcom.

#ifndef __BNG_FW_H__
#define __BNG_FW_H__

/* CREQ */
#define BNG_FW_CREQE_MAX_CNT	(64 * 1024)
#define BNG_FW_CREQE_UNITS	16

/* CMDQ */
struct bng_fw_cmdqe {
	u8	data[16];
};

#define BNG_FW_CMDQE_MAX_CNT		8192
#define BNG_FW_CMDQE_UNITS		sizeof(struct bng_fw_cmdqe)
#define BNG_FW_CMDQE_BYTES(depth)	((depth) * BNG_FW_CMDQE_UNITS)

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

/* HWQ */
struct bng_re_cmdq_ctx {
	struct bng_re_hwq		hwq;
};

struct bng_re_creq_ctx {
	struct bng_re_hwq		hwq;
};

struct bng_re_crsqe {
	struct creq_qp_event	*resp;
	u32			req_size;
	/* Free slots at the time of submission */
	u32			free_slots;
	u8			opcode;
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
};

void bng_re_free_rcfw_channel(struct bng_re_rcfw *rcfw);
int bng_re_alloc_fw_channel(struct bng_re_res *res,
			    struct bng_re_rcfw *rcfw);
#endif
