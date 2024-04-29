/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2017-2020 Xilinx, Inc. All rights reserved.
 * Copyright (C) 2022, Advanced Micro Devices, Inc.
 */

#ifndef __DMA_XDMA_REGS_H
#define __DMA_XDMA_REGS_H

/* The length of register space exposed to host */
#define XDMA_REG_SPACE_LEN	65536

/*
 * maximum number of DMA channels for each direction:
 * Host to Card (H2C) or Card to Host (C2H)
 */
#define XDMA_MAX_CHANNELS	4

/*
 * macros to define the number of descriptor blocks can be used in one
 * DMA transfer request.
 * the DMA engine uses a linked list of descriptor blocks that specify the
 * source, destination, and length of the DMA transfers.
 */
#define XDMA_DESC_BLOCK_NUM		BIT(7)
#define XDMA_DESC_BLOCK_MASK		(XDMA_DESC_BLOCK_NUM - 1)

/* descriptor definitions */
#define XDMA_DESC_ADJACENT		32
#define XDMA_DESC_ADJACENT_MASK		(XDMA_DESC_ADJACENT - 1)
#define XDMA_DESC_ADJACENT_BITS		GENMASK(13, 8)
#define XDMA_DESC_MAGIC			0xad4bUL
#define XDMA_DESC_MAGIC_BITS		GENMASK(31, 16)
#define XDMA_DESC_FLAGS_BITS		GENMASK(7, 0)
#define XDMA_DESC_STOPPED		BIT(0)
#define XDMA_DESC_COMPLETED		BIT(1)
#define XDMA_DESC_BLEN_BITS		28
#define XDMA_DESC_BLEN_MAX		(BIT(XDMA_DESC_BLEN_BITS) - PAGE_SIZE)

/* macros to construct the descriptor control word */
#define XDMA_DESC_CONTROL(adjacent, flag)				\
	(FIELD_PREP(XDMA_DESC_MAGIC_BITS, XDMA_DESC_MAGIC) |		\
	 FIELD_PREP(XDMA_DESC_ADJACENT_BITS, (adjacent) - 1) |		\
	 FIELD_PREP(XDMA_DESC_FLAGS_BITS, (flag)))
#define XDMA_DESC_CONTROL_LAST						\
	XDMA_DESC_CONTROL(1, XDMA_DESC_STOPPED | XDMA_DESC_COMPLETED)
#define XDMA_DESC_CONTROL_CYCLIC					\
	XDMA_DESC_CONTROL(1, XDMA_DESC_COMPLETED)

/*
 * Descriptor for a single contiguous memory block transfer.
 *
 * Multiple descriptors are linked by means of the next pointer. An additional
 * extra adjacent number gives the amount of extra contiguous descriptors.
 *
 * The descriptors are in root complex memory, and the bytes in the 32-bit
 * words must be in little-endian byte ordering.
 */
struct xdma_hw_desc {
	__le32		control;
	__le32		bytes;
	__le64		src_addr;
	__le64		dst_addr;
	__le64		next_desc;
};

#define XDMA_DESC_SIZE			sizeof(struct xdma_hw_desc)
#define XDMA_DESC_BLOCK_SIZE		(XDMA_DESC_SIZE * XDMA_DESC_ADJACENT)
#define XDMA_DESC_BLOCK_ALIGN		32
#define XDMA_DESC_BLOCK_BOUNDARY	4096

/*
 * Channel registers
 */
#define XDMA_CHAN_IDENTIFIER		0x0
#define XDMA_CHAN_CONTROL		0x4
#define XDMA_CHAN_CONTROL_W1S		0x8
#define XDMA_CHAN_CONTROL_W1C		0xc
#define XDMA_CHAN_STATUS		0x40
#define XDMA_CHAN_STATUS_RC		0x44
#define XDMA_CHAN_COMPLETED_DESC	0x48
#define XDMA_CHAN_ALIGNMENTS		0x4c
#define XDMA_CHAN_INTR_ENABLE		0x90
#define XDMA_CHAN_INTR_ENABLE_W1S	0x94
#define XDMA_CHAN_INTR_ENABLE_W1C	0x9c

#define XDMA_CHAN_STRIDE	0x100
#define XDMA_CHAN_H2C_OFFSET	0x0
#define XDMA_CHAN_C2H_OFFSET	0x1000
#define XDMA_CHAN_H2C_TARGET	0x0
#define XDMA_CHAN_C2H_TARGET	0x1

/* macro to check if channel is available */
#define XDMA_CHAN_MAGIC		0x1fc0
#define XDMA_CHAN_CHECK_TARGET(id, target)		\
	(((u32)(id) >> 16) == XDMA_CHAN_MAGIC + (target))

/* bits of the channel control register */
#define CHAN_CTRL_RUN_STOP			BIT(0)
#define CHAN_CTRL_IE_DESC_STOPPED		BIT(1)
#define CHAN_CTRL_IE_DESC_COMPLETED		BIT(2)
#define CHAN_CTRL_IE_DESC_ALIGN_MISMATCH	BIT(3)
#define CHAN_CTRL_IE_MAGIC_STOPPED		BIT(4)
#define CHAN_CTRL_IE_IDLE_STOPPED		BIT(6)
#define CHAN_CTRL_IE_READ_ERROR			GENMASK(13, 9)
#define CHAN_CTRL_IE_WRITE_ERROR		GENMASK(18, 14)
#define CHAN_CTRL_IE_DESC_ERROR			GENMASK(23, 19)
#define CHAN_CTRL_NON_INCR_ADDR			BIT(25)
#define CHAN_CTRL_POLL_MODE_WB			BIT(26)

#define CHAN_CTRL_START	(CHAN_CTRL_RUN_STOP |				\
			 CHAN_CTRL_IE_DESC_STOPPED |			\
			 CHAN_CTRL_IE_DESC_COMPLETED |			\
			 CHAN_CTRL_IE_DESC_ALIGN_MISMATCH |		\
			 CHAN_CTRL_IE_MAGIC_STOPPED |			\
			 CHAN_CTRL_IE_READ_ERROR |			\
			 CHAN_CTRL_IE_WRITE_ERROR |			\
			 CHAN_CTRL_IE_DESC_ERROR)

/* bits of the channel status register */
#define XDMA_CHAN_STATUS_BUSY			BIT(0)

#define XDMA_CHAN_STATUS_MASK CHAN_CTRL_START

#define XDMA_CHAN_ERROR_MASK (CHAN_CTRL_IE_DESC_ALIGN_MISMATCH |	\
			      CHAN_CTRL_IE_MAGIC_STOPPED |		\
			      CHAN_CTRL_IE_READ_ERROR |			\
			      CHAN_CTRL_IE_WRITE_ERROR |		\
			      CHAN_CTRL_IE_DESC_ERROR)

/* bits of the channel interrupt enable mask */
#define CHAN_IM_DESC_ERROR			BIT(19)
#define CHAN_IM_READ_ERROR			BIT(9)
#define CHAN_IM_IDLE_STOPPED			BIT(6)
#define CHAN_IM_MAGIC_STOPPED			BIT(4)
#define CHAN_IM_DESC_COMPLETED			BIT(2)
#define CHAN_IM_DESC_STOPPED			BIT(1)

#define CHAN_IM_ALL	(CHAN_IM_DESC_ERROR | CHAN_IM_READ_ERROR |	\
			 CHAN_IM_IDLE_STOPPED | CHAN_IM_MAGIC_STOPPED | \
			 CHAN_IM_DESC_COMPLETED | CHAN_IM_DESC_STOPPED)

/*
 * Channel SGDMA registers
 */
#define XDMA_SGDMA_IDENTIFIER	0x4000
#define XDMA_SGDMA_DESC_LO	0x4080
#define XDMA_SGDMA_DESC_HI	0x4084
#define XDMA_SGDMA_DESC_ADJ	0x4088
#define XDMA_SGDMA_DESC_CREDIT	0x408c

/*
 * interrupt registers
 */
#define XDMA_IRQ_IDENTIFIER		0x2000
#define XDMA_IRQ_USER_INT_EN		0x2004
#define XDMA_IRQ_USER_INT_EN_W1S	0x2008
#define XDMA_IRQ_USER_INT_EN_W1C	0x200c
#define XDMA_IRQ_CHAN_INT_EN		0x2010
#define XDMA_IRQ_CHAN_INT_EN_W1S	0x2014
#define XDMA_IRQ_CHAN_INT_EN_W1C	0x2018
#define XDMA_IRQ_USER_INT_REQ		0x2040
#define XDMA_IRQ_CHAN_INT_REQ		0x2044
#define XDMA_IRQ_USER_INT_PEND		0x2048
#define XDMA_IRQ_CHAN_INT_PEND		0x204c
#define XDMA_IRQ_USER_VEC_NUM		0x2080
#define XDMA_IRQ_CHAN_VEC_NUM		0x20a0

#define XDMA_IRQ_VEC_SHIFT		8

#endif /* __DMA_XDMA_REGS_H */
