/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018, Intel Corporation. */

#ifndef _ICE_TXRX_H_
#define _ICE_TXRX_H_

#define ICE_DFLT_IRQ_WORK	256

/* this enum matches hardware bits and is meant to be used by DYN_CTLN
 * registers and QINT registers or more generally anywhere in the manual
 * mentioning ITR_INDX, ITR_NONE cannot be used as an index 'n' into any
 * register but instead is a special value meaning "don't update" ITR0/1/2.
 */
enum ice_dyn_idx_t {
	ICE_IDX_ITR0 = 0,
	ICE_IDX_ITR1 = 1,
	ICE_IDX_ITR2 = 2,
	ICE_ITR_NONE = 3	/* ITR_NONE must not be used as an index */
};

/* indices into GLINT_ITR registers */
#define ICE_RX_ITR	ICE_IDX_ITR0
#define ICE_ITR_DYNAMIC	0x8000  /* use top bit as a flag */
#define ICE_ITR_8K	0x003E

/* apply ITR HW granularity translation to program the HW registers */
#define ITR_TO_REG(val, itr_gran) (((val) & ~ICE_ITR_DYNAMIC) >> (itr_gran))

/* descriptor ring, associated with a VSI */
struct ice_ring {
	struct ice_ring *next;		/* pointer to next ring in q_vector */
	struct device *dev;		/* Used for DMA mapping */
	struct net_device *netdev;	/* netdev ring maps to */
	struct ice_vsi *vsi;		/* Backreference to associated VSI */
	struct ice_q_vector *q_vector;	/* Backreference to associated vector */
	u16 q_index;			/* Queue number of ring */
	u16 count;			/* Number of descriptors */
	u16 reg_idx;			/* HW register index of the ring */
	bool ring_active;		/* is ring online or not */
	struct rcu_head rcu;		/* to avoid race on free */
} ____cacheline_internodealigned_in_smp;

struct ice_ring_container {
	/* array of pointers to rings */
	struct ice_ring *ring;
	unsigned int total_bytes;	/* total bytes processed this int */
	unsigned int total_pkts;	/* total packets processed this int */
	u16 itr;
};

/* iterator for handling rings in ring container */
#define ice_for_each_ring(pos, head) \
	for (pos = (head).ring; pos; pos = pos->next)

#endif /* _ICE_TXRX_H_ */
