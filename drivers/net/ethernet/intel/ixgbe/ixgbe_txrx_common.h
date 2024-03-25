/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2018 Intel Corporation. */

#ifndef _IXGBE_TXRX_COMMON_H_
#define _IXGBE_TXRX_COMMON_H_

#define IXGBE_XDP_PASS		0
#define IXGBE_XDP_CONSUMED	BIT(0)
#define IXGBE_XDP_TX		BIT(1)
#define IXGBE_XDP_REDIR		BIT(2)
#define IXGBE_XDP_EXIT		BIT(3)

#define IXGBE_TXD_CMD (IXGBE_TXD_CMD_EOP | \
		       IXGBE_TXD_CMD_RS)

int ixgbe_xmit_xdp_ring(struct ixgbe_ring *ring,
			struct xdp_frame *xdpf);
bool ixgbe_cleanup_headers(struct ixgbe_ring *rx_ring,
			   union ixgbe_adv_rx_desc *rx_desc,
			   struct sk_buff *skb);
void ixgbe_process_skb_fields(struct ixgbe_ring *rx_ring,
			      union ixgbe_adv_rx_desc *rx_desc,
			      struct sk_buff *skb);
void ixgbe_rx_skb(struct ixgbe_q_vector *q_vector,
		  struct sk_buff *skb);
void ixgbe_xdp_ring_update_tail(struct ixgbe_ring *ring);
void ixgbe_xdp_ring_update_tail_locked(struct ixgbe_ring *ring);
void ixgbe_irq_rearm_queues(struct ixgbe_adapter *adapter, u64 qmask);

void ixgbe_txrx_ring_disable(struct ixgbe_adapter *adapter, int ring);
void ixgbe_txrx_ring_enable(struct ixgbe_adapter *adapter, int ring);

struct xsk_buff_pool *ixgbe_xsk_pool(struct ixgbe_adapter *adapter,
				     struct ixgbe_ring *ring);
int ixgbe_xsk_pool_setup(struct ixgbe_adapter *adapter,
			 struct xsk_buff_pool *pool,
			 u16 qid);

bool ixgbe_alloc_rx_buffers_zc(struct ixgbe_ring *rx_ring, u16 cleaned_count);
int ixgbe_clean_rx_irq_zc(struct ixgbe_q_vector *q_vector,
			  struct ixgbe_ring *rx_ring,
			  const int budget);
void ixgbe_xsk_clean_rx_ring(struct ixgbe_ring *rx_ring);
bool ixgbe_clean_xdp_tx_irq(struct ixgbe_q_vector *q_vector,
			    struct ixgbe_ring *tx_ring, int napi_budget);
int ixgbe_xsk_wakeup(struct net_device *dev, u32 queue_id, u32 flags);
void ixgbe_xsk_clean_tx_ring(struct ixgbe_ring *tx_ring);

void ixgbe_update_tx_ring_stats(struct ixgbe_ring *tx_ring,
				struct ixgbe_q_vector *q_vector, u64 pkts,
				u64 bytes);
void ixgbe_update_rx_ring_stats(struct ixgbe_ring *rx_ring,
				struct ixgbe_q_vector *q_vector, u64 pkts,
				u64 bytes);

#endif /* #define _IXGBE_TXRX_COMMON_H_ */
