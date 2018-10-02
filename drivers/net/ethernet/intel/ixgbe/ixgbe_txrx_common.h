/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2018 Intel Corporation. */

#ifndef _IXGBE_TXRX_COMMON_H_
#define _IXGBE_TXRX_COMMON_H_

#define IXGBE_XDP_PASS		0
#define IXGBE_XDP_CONSUMED	BIT(0)
#define IXGBE_XDP_TX		BIT(1)
#define IXGBE_XDP_REDIR		BIT(2)

int ixgbe_xmit_xdp_ring(struct ixgbe_adapter *adapter,
			struct xdp_frame *xdpf);
bool ixgbe_cleanup_headers(struct ixgbe_ring *rx_ring,
			   union ixgbe_adv_rx_desc *rx_desc,
			   struct sk_buff *skb);
void ixgbe_process_skb_fields(struct ixgbe_ring *rx_ring,
			      union ixgbe_adv_rx_desc *rx_desc,
			      struct sk_buff *skb);
void ixgbe_rx_skb(struct ixgbe_q_vector *q_vector,
		  struct sk_buff *skb);

void ixgbe_txrx_ring_disable(struct ixgbe_adapter *adapter, int ring);
void ixgbe_txrx_ring_enable(struct ixgbe_adapter *adapter, int ring);

#endif /* #define _IXGBE_TXRX_COMMON_H_ */
