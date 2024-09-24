/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * DMA header for AMD Queue-based DMA Subsystem
 *
 * Copyright (C) 2023-2024, Advanced Micro Devices, Inc.
 */

#ifndef __QDMA_H
#define __QDMA_H

#include <linux/bitfield.h>
#include <linux/dmaengine.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "../../virt-dma.h"

#define DISABLE					0
#define ENABLE					1

#define QDMA_MIN_IRQ				3
#define QDMA_INTR_NAME_MAX_LEN			30
#define QDMA_INTR_PREFIX			"amd-qdma"

#define QDMA_IDENTIFIER				0x1FD3
#define QDMA_DEFAULT_RING_SIZE			(BIT(10) + 1)
#define QDMA_DEFAULT_RING_ID			0
#define QDMA_POLL_INTRVL_US			10		/* 10us */
#define QDMA_POLL_TIMEOUT_US			(500 * 1000)	/* 500ms */
#define QDMA_DMAP_REG_STRIDE			16
#define QDMA_CTXT_REGMAP_LEN			8		/* 8 regs */
#define QDMA_MM_DESC_SIZE			32		/* Bytes */
#define QDMA_MM_DESC_LEN_BITS			28
#define QDMA_MM_DESC_MAX_LEN			(BIT(QDMA_MM_DESC_LEN_BITS) - 1)
#define QDMA_MIN_DMA_ALLOC_SIZE			4096
#define QDMA_INTR_RING_SIZE			BIT(13)
#define QDMA_INTR_RING_IDX_MASK			GENMASK(9, 0)
#define QDMA_INTR_RING_BASE(_addr)		((_addr) >> 12)

#define QDMA_IDENTIFIER_REGOFF			0x0
#define QDMA_IDENTIFIER_MASK			GENMASK(31, 16)
#define QDMA_QUEUE_ARM_BIT			BIT(16)

#define qdma_err(qdev, fmt, args...)					\
	dev_err(&(qdev)->pdev->dev, fmt, ##args)

#define qdma_dbg(qdev, fmt, args...)					\
	dev_dbg(&(qdev)->pdev->dev, fmt, ##args)

#define qdma_info(qdev, fmt, args...)					\
	dev_info(&(qdev)->pdev->dev, fmt, ##args)

enum qdma_reg_fields {
	QDMA_REGF_IRQ_ENABLE,
	QDMA_REGF_WBK_ENABLE,
	QDMA_REGF_WBI_CHECK,
	QDMA_REGF_IRQ_ARM,
	QDMA_REGF_IRQ_VEC,
	QDMA_REGF_IRQ_AGG,
	QDMA_REGF_WBI_INTVL_ENABLE,
	QDMA_REGF_MRKR_DISABLE,
	QDMA_REGF_QUEUE_ENABLE,
	QDMA_REGF_QUEUE_MODE,
	QDMA_REGF_DESC_BASE,
	QDMA_REGF_DESC_SIZE,
	QDMA_REGF_RING_ID,
	QDMA_REGF_CMD_INDX,
	QDMA_REGF_CMD_CMD,
	QDMA_REGF_CMD_TYPE,
	QDMA_REGF_CMD_BUSY,
	QDMA_REGF_QUEUE_COUNT,
	QDMA_REGF_QUEUE_MAX,
	QDMA_REGF_QUEUE_BASE,
	QDMA_REGF_FUNCTION_ID,
	QDMA_REGF_INTR_AGG_BASE,
	QDMA_REGF_INTR_VECTOR,
	QDMA_REGF_INTR_SIZE,
	QDMA_REGF_INTR_VALID,
	QDMA_REGF_INTR_COLOR,
	QDMA_REGF_INTR_FUNCTION_ID,
	QDMA_REGF_ERR_INT_FUNC,
	QDMA_REGF_ERR_INT_VEC,
	QDMA_REGF_ERR_INT_ARM,
	QDMA_REGF_MAX
};

enum qdma_regs {
	QDMA_REGO_CTXT_DATA,
	QDMA_REGO_CTXT_CMD,
	QDMA_REGO_CTXT_MASK,
	QDMA_REGO_MM_H2C_CTRL,
	QDMA_REGO_MM_C2H_CTRL,
	QDMA_REGO_QUEUE_COUNT,
	QDMA_REGO_RING_SIZE,
	QDMA_REGO_H2C_PIDX,
	QDMA_REGO_C2H_PIDX,
	QDMA_REGO_INTR_CIDX,
	QDMA_REGO_FUNC_ID,
	QDMA_REGO_ERR_INT,
	QDMA_REGO_ERR_STAT,
	QDMA_REGO_MAX
};

struct qdma_reg_field {
	u16 lsb; /* Least significant bit of field */
	u16 msb; /* Most significant bit of field */
};

struct qdma_reg {
	u32 off;
	u32 count;
};

#define QDMA_REGF(_msb, _lsb) {						\
	.lsb = (_lsb),							\
	.msb = (_msb),							\
}

#define QDMA_REGO(_off, _count) {					\
	.off = (_off),							\
	.count = (_count),						\
}

enum qdma_desc_size {
	QDMA_DESC_SIZE_8B,
	QDMA_DESC_SIZE_16B,
	QDMA_DESC_SIZE_32B,
	QDMA_DESC_SIZE_64B,
};

enum qdma_queue_op_mode {
	QDMA_QUEUE_OP_STREAM,
	QDMA_QUEUE_OP_MM,
};

enum qdma_ctxt_type {
	QDMA_CTXT_DESC_SW_C2H,
	QDMA_CTXT_DESC_SW_H2C,
	QDMA_CTXT_DESC_HW_C2H,
	QDMA_CTXT_DESC_HW_H2C,
	QDMA_CTXT_DESC_CR_C2H,
	QDMA_CTXT_DESC_CR_H2C,
	QDMA_CTXT_WRB,
	QDMA_CTXT_PFTCH,
	QDMA_CTXT_INTR_COAL,
	QDMA_CTXT_RSVD,
	QDMA_CTXT_HOST_PROFILE,
	QDMA_CTXT_TIMER,
	QDMA_CTXT_FMAP,
	QDMA_CTXT_FNC_STS,
};

enum qdma_ctxt_cmd {
	QDMA_CTXT_CLEAR,
	QDMA_CTXT_WRITE,
	QDMA_CTXT_READ,
	QDMA_CTXT_INVALIDATE,
	QDMA_CTXT_MAX
};

struct qdma_ctxt_sw_desc {
	u64				desc_base;
	u16				vec;
};

struct qdma_ctxt_intr {
	u64				agg_base;
	u16				vec;
	u32				size;
	bool				valid;
	bool				color;
};

struct qdma_ctxt_fmap {
	u16				qbase;
	u16				qmax;
};

struct qdma_device;

struct qdma_mm_desc {
	__le64			src_addr;
	__le32			len;
	__le32			reserved1;
	__le64			dst_addr;
	__le64			reserved2;
} __packed;

struct qdma_mm_vdesc {
	struct virt_dma_desc		vdesc;
	struct qdma_queue		*queue;
	struct scatterlist		*sgl;
	u64				sg_off;
	u32				sg_len;
	u64				dev_addr;
	u32				pidx;
	u32				pending_descs;
	struct dma_slave_config		cfg;
};

#define QDMA_VDESC_QUEUED(vdesc)	(!(vdesc)->sg_len)

struct qdma_queue {
	struct qdma_device		*qdev;
	struct virt_dma_chan		vchan;
	enum dma_transfer_direction	dir;
	struct dma_slave_config		cfg;
	struct qdma_mm_desc		*desc_base;
	struct qdma_mm_vdesc		*submitted_vdesc;
	struct qdma_mm_vdesc		*issued_vdesc;
	dma_addr_t			dma_desc_base;
	u32				pidx_reg;
	u32				cidx_reg;
	u32				ring_size;
	u32				idx_mask;
	u16				qid;
	u32				pidx;
	u32				cidx;
};

struct qdma_intr_ring {
	struct qdma_device		*qdev;
	__le64				*base;
	dma_addr_t			dev_base;
	char				msix_name[QDMA_INTR_NAME_MAX_LEN];
	u32				msix_vector;
	u16				msix_id;
	u32				ring_size;
	u16				ridx;
	u16				cidx;
	u8				color;
};

#define QDMA_INTR_MASK_PIDX		GENMASK_ULL(15, 0)
#define QDMA_INTR_MASK_CIDX		GENMASK_ULL(31, 16)
#define QDMA_INTR_MASK_DESC_COLOR	GENMASK_ULL(32, 32)
#define QDMA_INTR_MASK_STATE		GENMASK_ULL(34, 33)
#define QDMA_INTR_MASK_ERROR		GENMASK_ULL(36, 35)
#define QDMA_INTR_MASK_TYPE		GENMASK_ULL(38, 38)
#define QDMA_INTR_MASK_QID		GENMASK_ULL(62, 39)
#define QDMA_INTR_MASK_COLOR		GENMASK_ULL(63, 63)

struct qdma_device {
	struct platform_device		*pdev;
	struct dma_device		dma_dev;
	struct regmap			*regmap;
	struct mutex			ctxt_lock; /* protect ctxt registers */
	const struct qdma_reg_field	*rfields;
	const struct qdma_reg		*roffs;
	struct qdma_queue		*h2c_queues;
	struct qdma_queue		*c2h_queues;
	struct qdma_intr_ring		*qintr_rings;
	u32				qintr_ring_num;
	u32				qintr_ring_idx;
	u32				chan_num;
	u32				queue_irq_start;
	u32				queue_irq_num;
	u32				err_irq_idx;
	u32				fid;
};

extern const struct qdma_reg qdma_regos_default[QDMA_REGO_MAX];
extern const struct qdma_reg_field qdma_regfs_default[QDMA_REGF_MAX];

#endif	/* __QDMA_H */
