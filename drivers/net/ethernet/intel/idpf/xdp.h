/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2025 Intel Corporation */

#ifndef _IDPF_XDP_H_
#define _IDPF_XDP_H_

#include <net/libeth/xdp.h>

#include "idpf_txrx.h"

int idpf_xdp_rxq_info_init_all(const struct idpf_vport *vport);
void idpf_xdp_rxq_info_deinit_all(const struct idpf_vport *vport);
void idpf_xdp_copy_prog_to_rqs(const struct idpf_vport *vport,
			       struct bpf_prog *xdp_prog);

int idpf_xdpsqs_get(const struct idpf_vport *vport);
void idpf_xdpsqs_put(const struct idpf_vport *vport);

bool idpf_xdp_tx_flush_bulk(struct libeth_xdp_tx_bulk *bq, u32 flags);

/**
 * idpf_xdp_tx_xmit - produce a single HW Tx descriptor out of XDP desc
 * @desc: XDP descriptor to pull the DMA address and length from
 * @i: descriptor index on the queue to fill
 * @sq: XDP queue to produce the HW Tx descriptor on
 * @priv: &xsk_tx_metadata_ops on XSk xmit or %NULL
 */
static inline void idpf_xdp_tx_xmit(struct libeth_xdp_tx_desc desc, u32 i,
				    const struct libeth_xdpsq *sq, u64 priv)
{
	struct idpf_flex_tx_desc *tx_desc = sq->descs;
	u32 cmd;

	cmd = FIELD_PREP(IDPF_FLEX_TXD_QW1_DTYPE_M,
			 IDPF_TX_DESC_DTYPE_FLEX_L2TAG1_L2TAG2);
	if (desc.flags & LIBETH_XDP_TX_LAST)
		cmd |= FIELD_PREP(IDPF_FLEX_TXD_QW1_CMD_M,
				  IDPF_TX_DESC_CMD_EOP);
	if (priv && (desc.flags & LIBETH_XDP_TX_CSUM))
		cmd |= FIELD_PREP(IDPF_FLEX_TXD_QW1_CMD_M,
				  IDPF_TX_FLEX_DESC_CMD_CS_EN);

	tx_desc = &tx_desc[i];
	tx_desc->buf_addr = cpu_to_le64(desc.addr);
#ifdef __LIBETH_WORD_ACCESS
	*(u64 *)&tx_desc->qw1 = ((u64)desc.len << 48) | cmd;
#else
	tx_desc->qw1.buf_size = cpu_to_le16(desc.len);
	tx_desc->qw1.cmd_dtype = cpu_to_le16(cmd);
#endif
}

static inline void idpf_xdpsq_set_rs(const struct idpf_tx_queue *xdpsq)
{
	u32 ntu, cmd;

	ntu = xdpsq->next_to_use;
	if (unlikely(!ntu))
		ntu = xdpsq->desc_count;

	cmd = FIELD_PREP(IDPF_FLEX_TXD_QW1_CMD_M, IDPF_TX_DESC_CMD_RS);
#ifdef __LIBETH_WORD_ACCESS
	*(u64 *)&xdpsq->flex_tx[ntu - 1].q.qw1 |= cmd;
#else
	xdpsq->flex_tx[ntu - 1].q.qw1.cmd_dtype |= cpu_to_le16(cmd);
#endif
}

static inline void idpf_xdpsq_update_tail(const struct idpf_tx_queue *xdpsq)
{
	dma_wmb();
	writel_relaxed(xdpsq->next_to_use, xdpsq->tail);
}

/**
 * idpf_xdp_tx_finalize - finalize sending over XDPSQ
 * @_xdpsq: XDP Tx queue
 * @sent: whether any frames were sent
 * @flush: whether to update RS bit and the tail register
 *
 * Set the RS bit ("end of batch"), bump the tail, and queue the cleanup timer.
 * To be called after a NAPI polling loop, at the end of .ndo_xdp_xmit() etc.
 */
static inline void idpf_xdp_tx_finalize(void *_xdpsq, bool sent, bool flush)
{
	struct idpf_tx_queue *xdpsq = _xdpsq;

	if ((!flush || unlikely(!sent)) &&
	    likely(xdpsq->desc_count - 1 != xdpsq->pending))
		return;

	libeth_xdpsq_lock(&xdpsq->xdp_lock);

	idpf_xdpsq_set_rs(xdpsq);
	idpf_xdpsq_update_tail(xdpsq);

	libeth_xdpsq_queue_timer(xdpsq->timer);

	libeth_xdpsq_unlock(&xdpsq->xdp_lock);
}

void idpf_xdp_set_features(const struct idpf_vport *vport);

int idpf_xdp(struct net_device *dev, struct netdev_bpf *xdp);
int idpf_xdp_xmit(struct net_device *dev, int n, struct xdp_frame **frames,
		  u32 flags);

#endif /* _IDPF_XDP_H_ */
