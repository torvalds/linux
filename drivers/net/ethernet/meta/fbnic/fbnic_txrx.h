/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#ifndef _FBNIC_TXRX_H_
#define _FBNIC_TXRX_H_

#include <linux/netdevice.h>
#include <linux/types.h>

struct fbnic_net;

#define FBNIC_MAX_TXQS			128u
#define FBNIC_MAX_RXQS			128u

#define FBNIC_TXQ_SIZE_DEFAULT		1024

#define FBNIC_RING_F_DISABLED		BIT(0)
#define FBNIC_RING_F_CTX		BIT(1)
#define FBNIC_RING_F_STATS		BIT(2)	/* Ring's stats may be used */

struct fbnic_ring {
	/* Pointer to buffer specific info */
	union {
		void **tx_buf;			/* TWQ */
		void *buffer;			/* Generic pointer */
	};

	u32 __iomem *doorbell;		/* Pointer to CSR space for ring */
	__le64 *desc;			/* Descriptor ring memory */
	u16 size_mask;			/* Size of ring in descriptors - 1 */
	u8 q_idx;			/* Logical netdev ring index */
	u8 flags;			/* Ring flags (FBNIC_RING_F_*) */

	u32 head, tail;			/* Head/Tail of ring */

	/* Slow path fields follow */
	dma_addr_t dma;			/* Phys addr of descriptor memory */
	size_t size;			/* Size of descriptor ring in memory */
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
int fbnic_alloc_resources(struct fbnic_net *fbn);
void fbnic_free_resources(struct fbnic_net *fbn);
void fbnic_napi_enable(struct fbnic_net *fbn);
void fbnic_napi_disable(struct fbnic_net *fbn);
void fbnic_enable(struct fbnic_net *fbn);
void fbnic_disable(struct fbnic_net *fbn);
void fbnic_flush(struct fbnic_net *fbn);

int fbnic_wait_all_queues_idle(struct fbnic_dev *fbd, bool may_fail);

#endif /* _FBNIC_TXRX_H_ */
