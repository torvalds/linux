/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#ifndef _FBNIC_TXRX_H_
#define _FBNIC_TXRX_H_

#include <linux/netdevice.h>
#include <linux/types.h>

struct fbnic_net;

#define FBNIC_MAX_TXQS			128u
#define FBNIC_MAX_RXQS			128u

#define FBNIC_RING_F_DISABLED		BIT(0)
#define FBNIC_RING_F_CTX		BIT(1)
#define FBNIC_RING_F_STATS		BIT(2)	/* Ring's stats may be used */

struct fbnic_ring {
	u32 __iomem *doorbell;		/* Pointer to CSR space for ring */
	u16 size_mask;			/* Size of ring in descriptors - 1 */
	u8 q_idx;			/* Logical netdev ring index */
	u8 flags;			/* Ring flags (FBNIC_RING_F_*) */

	u32 head, tail;			/* Head/Tail of ring */
};

struct fbnic_q_triad {
	struct fbnic_ring sub0, sub1, cmpl;
};

struct fbnic_napi_vector {
	struct napi_struct napi;
	struct device *dev;		/* Device for DMA unmapping */
	struct fbnic_dev *fbd;
	char name[IFNAMSIZ + 9];

	u16 v_idx;
	u8 txt_count;
	u8 rxt_count;

	struct list_head napis;

	struct fbnic_q_triad qt[];
};

#define FBNIC_MAX_TXQS			128u
#define FBNIC_MAX_RXQS			128u

netdev_tx_t fbnic_xmit_frame(struct sk_buff *skb, struct net_device *dev);

int fbnic_alloc_napi_vectors(struct fbnic_net *fbn);
void fbnic_free_napi_vectors(struct fbnic_net *fbn);

#endif /* _FBNIC_TXRX_H_ */
