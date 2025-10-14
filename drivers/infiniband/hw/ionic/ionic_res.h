/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018-2025, Advanced Micro Devices, Inc. */

#ifndef _IONIC_RES_H_
#define _IONIC_RES_H_

#include <linux/kernel.h>
#include <linux/idr.h>

/**
 * struct ionic_resid_bits - Number allocator based on IDA
 *
 * @inuse:      IDA handle
 * @inuse_size: Highest ID limit for IDA
 */
struct ionic_resid_bits {
	struct ida inuse;
	unsigned int inuse_size;
};

/**
 * ionic_resid_init() - Initialize a resid allocator
 * @resid:  Uninitialized resid allocator
 * @size:   Capacity of the allocator
 *
 * Return: Zero on success, or negative error number
 */
static inline void ionic_resid_init(struct ionic_resid_bits *resid,
				    unsigned int size)
{
	resid->inuse_size = size;
	ida_init(&resid->inuse);
}

/**
 * ionic_resid_destroy() - Destroy a resid allocator
 * @resid:  Resid allocator
 */
static inline void ionic_resid_destroy(struct ionic_resid_bits *resid)
{
	ida_destroy(&resid->inuse);
}

/**
 * ionic_resid_get_shared() - Allocate an available shared resource id
 * @resid:   Resid allocator
 * @min:     Smallest valid resource id
 * @size:    One after largest valid resource id
 *
 * Return: Resource id, or negative error number
 */
static inline int ionic_resid_get_shared(struct ionic_resid_bits *resid,
					 unsigned int min,
					 unsigned int size)
{
	return ida_alloc_range(&resid->inuse, min, size - 1, GFP_KERNEL);
}

/**
 * ionic_resid_get() - Allocate an available resource id
 * @resid: Resid allocator
 *
 * Return: Resource id, or negative error number
 */
static inline int ionic_resid_get(struct ionic_resid_bits *resid)
{
	return ionic_resid_get_shared(resid, 0, resid->inuse_size);
}

/**
 * ionic_resid_put() - Free a resource id
 * @resid:  Resid allocator
 * @id:     Resource id
 */
static inline void ionic_resid_put(struct ionic_resid_bits *resid, int id)
{
	ida_free(&resid->inuse, id);
}

/**
 * ionic_bitid_to_qid() - Transform a resource bit index into a queue id
 * @bitid:           Bit index
 * @qgrp_shift:      Log2 number of queues per queue group
 * @half_qid_shift:  Log2 of half the total number of queues
 *
 * Return: Queue id
 *
 * Udma-constrained queues (QPs and CQs) are associated with their udma by
 * queue group. Even queue groups are associated with udma0, and odd queue
 * groups with udma1.
 *
 * For allocating queue ids, we want to arrange the bits into two halves,
 * with the even queue groups of udma0 in the lower half of the bitset,
 * and the odd queue groups of udma1 in the upper half of the bitset.
 * Then, one or two calls of find_next_zero_bit can examine all the bits
 * for queues of an entire udma.
 *
 * For example, assuming eight queue groups with qgrp qids per group:
 *
 * bitid 0*qgrp..1*qgrp-1 : qid 0*qgrp..1*qgrp-1
 * bitid 1*qgrp..2*qgrp-1 : qid 2*qgrp..3*qgrp-1
 * bitid 2*qgrp..3*qgrp-1 : qid 4*qgrp..5*qgrp-1
 * bitid 3*qgrp..4*qgrp-1 : qid 6*qgrp..7*qgrp-1
 * bitid 4*qgrp..5*qgrp-1 : qid 1*qgrp..2*qgrp-1
 * bitid 5*qgrp..6*qgrp-1 : qid 3*qgrp..4*qgrp-1
 * bitid 6*qgrp..7*qgrp-1 : qid 5*qgrp..6*qgrp-1
 * bitid 7*qgrp..8*qgrp-1 : qid 7*qgrp..8*qgrp-1
 *
 * There are three important ranges of bits in the qid.  There is the udma
 * bit "U" at qgrp_shift, which is the least significant bit of the group
 * index, and determines which udma a queue is associated with.
 * The bits of lesser significance we can call the idx bits "I", which are
 * the index of the queue within the group.  The bits of greater significance
 * we can call the grp bits "G", which are other bits of the group index that
 * do not determine the udma.  Those bits are just rearranged in the bit index
 * in the bitset.  A bitid has the udma bit in the most significant place,
 * then the grp bits, then the idx bits.
 *
 * bitid: 00000000000000 U GGG IIIIII
 * qid:   00000000000000 GGG U IIIIII
 *
 * Transforming from bit index to qid, or from qid to bit index, can be
 * accomplished by rearranging the bits by masking and shifting.
 */
static inline u32 ionic_bitid_to_qid(u32 bitid, u8 qgrp_shift,
				     u8 half_qid_shift)
{
	u32 udma_bit =
		(bitid & BIT(half_qid_shift)) >> (half_qid_shift - qgrp_shift);
	u32 grp_bits = (bitid & GENMASK(half_qid_shift - 1, qgrp_shift)) << 1;
	u32 idx_bits = bitid & (BIT(qgrp_shift) - 1);

	return grp_bits | udma_bit | idx_bits;
}

/**
 * ionic_qid_to_bitid() - Transform a queue id into a resource bit index
 * @qid:            queue index
 * @qgrp_shift:     Log2 number of queues per queue group
 * @half_qid_shift: Log2 of half the total number of queues
 *
 * Return: Resource bit index
 *
 * This is the inverse of ionic_bitid_to_qid().
 */
static inline u32 ionic_qid_to_bitid(u32 qid, u8 qgrp_shift, u8 half_qid_shift)
{
	u32 udma_bit = (qid & BIT(qgrp_shift)) << (half_qid_shift - qgrp_shift);
	u32 grp_bits = (qid & GENMASK(half_qid_shift, qgrp_shift + 1)) >> 1;
	u32 idx_bits = qid & (BIT(qgrp_shift) - 1);

	return udma_bit | grp_bits | idx_bits;
}
#endif /* _IONIC_RES_H_ */
