/* SPDX-License-Identifier: (GPL-2.0 OR Linux-OpenIB) OR BSD-2-Clause */
/* Copyright (c) 2018-2019 Pensando Systems, Inc.  All rights reserved. */

#ifndef IONIC_REGS_H
#define IONIC_REGS_H

#include <linux/io.h>

/** struct ionic_intr - interrupt control register set.
 * @coal_init:			coalesce timer initial value.
 * @mask:			interrupt mask value.
 * @credits:			interrupt credit count and return.
 * @mask_assert:		interrupt mask value on assert.
 * @coal:			coalesce timer time remaining.
 */
struct ionic_intr {
	u32 coal_init;
	u32 mask;
	u32 credits;
	u32 mask_assert;
	u32 coal;
	u32 rsvd[3];
};

#define IONIC_INTR_CTRL_REGS_MAX	2048
#define IONIC_INTR_CTRL_COAL_MAX	0x3F

/** enum ionic_intr_mask_vals - valid values for mask and mask_assert.
 * @IONIC_INTR_MASK_CLEAR:	unmask interrupt.
 * @IONIC_INTR_MASK_SET:	mask interrupt.
 */
enum ionic_intr_mask_vals {
	IONIC_INTR_MASK_CLEAR		= 0,
	IONIC_INTR_MASK_SET		= 1,
};

/** enum ionic_intr_credits_bits - bitwise composition of credits values.
 * @IONIC_INTR_CRED_COUNT:	bit mask of credit count, no shift needed.
 * @IONIC_INTR_CRED_COUNT_SIGNED: bit mask of credit count, including sign bit.
 * @IONIC_INTR_CRED_UNMASK:	unmask the interrupt.
 * @IONIC_INTR_CRED_RESET_COALESCE: reset the coalesce timer.
 * @IONIC_INTR_CRED_REARM:	unmask the and reset the timer.
 */
enum ionic_intr_credits_bits {
	IONIC_INTR_CRED_COUNT		= 0x7fffu,
	IONIC_INTR_CRED_COUNT_SIGNED	= 0xffffu,
	IONIC_INTR_CRED_UNMASK		= 0x10000u,
	IONIC_INTR_CRED_RESET_COALESCE	= 0x20000u,
	IONIC_INTR_CRED_REARM		= (IONIC_INTR_CRED_UNMASK |
					   IONIC_INTR_CRED_RESET_COALESCE),
};

static inline void ionic_intr_coal_init(struct ionic_intr __iomem *intr_ctrl,
					int intr_idx, u32 coal)
{
	iowrite32(coal, &intr_ctrl[intr_idx].coal_init);
}

static inline void ionic_intr_mask(struct ionic_intr __iomem *intr_ctrl,
				   int intr_idx, u32 mask)
{
	iowrite32(mask, &intr_ctrl[intr_idx].mask);
}

static inline void ionic_intr_credits(struct ionic_intr __iomem *intr_ctrl,
				      int intr_idx, u32 cred, u32 flags)
{
	if (WARN_ON_ONCE(cred > IONIC_INTR_CRED_COUNT)) {
		cred = ioread32(&intr_ctrl[intr_idx].credits);
		cred &= IONIC_INTR_CRED_COUNT_SIGNED;
	}

	iowrite32(cred | flags, &intr_ctrl[intr_idx].credits);
}

static inline void ionic_intr_clean(struct ionic_intr __iomem *intr_ctrl,
				    int intr_idx)
{
	u32 cred;

	cred = ioread32(&intr_ctrl[intr_idx].credits);
	cred &= IONIC_INTR_CRED_COUNT_SIGNED;
	cred |= IONIC_INTR_CRED_RESET_COALESCE;
	iowrite32(cred, &intr_ctrl[intr_idx].credits);
}

static inline void ionic_intr_mask_assert(struct ionic_intr __iomem *intr_ctrl,
					  int intr_idx, u32 mask)
{
	iowrite32(mask, &intr_ctrl[intr_idx].mask_assert);
}

/** enum ionic_dbell_bits - bitwise composition of dbell values.
 *
 * @IONIC_DBELL_QID_MASK:	unshifted mask of valid queue id bits.
 * @IONIC_DBELL_QID_SHIFT:	queue id shift amount in dbell value.
 * @IONIC_DBELL_QID:		macro to build QID component of dbell value.
 *
 * @IONIC_DBELL_RING_MASK:	unshifted mask of valid ring bits.
 * @IONIC_DBELL_RING_SHIFT:	ring shift amount in dbell value.
 * @IONIC_DBELL_RING:		macro to build ring component of dbell value.
 *
 * @IONIC_DBELL_RING_0:		ring zero dbell component value.
 * @IONIC_DBELL_RING_1:		ring one dbell component value.
 * @IONIC_DBELL_RING_2:		ring two dbell component value.
 * @IONIC_DBELL_RING_3:		ring three dbell component value.
 *
 * @IONIC_DBELL_INDEX_MASK:	bit mask of valid index bits, no shift needed.
 */
enum ionic_dbell_bits {
	IONIC_DBELL_QID_MASK		= 0xffffff,
	IONIC_DBELL_QID_SHIFT		= 24,

#define IONIC_DBELL_QID(n) \
	(((u64)(n) & IONIC_DBELL_QID_MASK) << IONIC_DBELL_QID_SHIFT)

	IONIC_DBELL_RING_MASK		= 0x7,
	IONIC_DBELL_RING_SHIFT		= 16,

#define IONIC_DBELL_RING(n) \
	(((u64)(n) & IONIC_DBELL_RING_MASK) << IONIC_DBELL_RING_SHIFT)

	IONIC_DBELL_RING_0		= 0,
	IONIC_DBELL_RING_1		= IONIC_DBELL_RING(1),
	IONIC_DBELL_RING_2		= IONIC_DBELL_RING(2),
	IONIC_DBELL_RING_3		= IONIC_DBELL_RING(3),

	IONIC_DBELL_INDEX_MASK		= 0xffff,
};

static inline void ionic_dbell_ring(u64 __iomem *db_page, int qtype, u64 val)
{
	writeq(val, &db_page[qtype]);
}

#endif /* IONIC_REGS_H */
