/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2018 Intel Corporation. */

#ifndef I40E_TXRX_COMMON_
#define I40E_TXRX_COMMON_

int i40e_xmit_xdp_tx_ring(struct xdp_buff *xdp, struct i40e_ring *xdp_ring);
void i40e_clean_programming_status(struct i40e_ring *rx_ring, u64 qword0_raw,
				   u64 qword1);
void i40e_process_skb_fields(struct i40e_ring *rx_ring,
			     union i40e_rx_desc *rx_desc, struct sk_buff *skb);
void i40e_xdp_ring_update_tail(struct i40e_ring *xdp_ring);
void i40e_update_rx_stats(struct i40e_ring *rx_ring,
			  unsigned int total_rx_bytes,
			  unsigned int total_rx_packets);
void i40e_finalize_xdp_rx(struct i40e_ring *rx_ring, unsigned int xdp_res);
void i40e_release_rx_desc(struct i40e_ring *rx_ring, u32 val);

#define I40E_XDP_PASS		0
#define I40E_XDP_CONSUMED	BIT(0)
#define I40E_XDP_TX		BIT(1)
#define I40E_XDP_REDIR		BIT(2)

/**
 * build_ctob - Builds the Tx descriptor (cmd, offset and type) qword
 **/
static inline __le64 build_ctob(u32 td_cmd, u32 td_offset, unsigned int size,
				u32 td_tag)
{
	return cpu_to_le64(I40E_TX_DESC_DTYPE_DATA |
			   ((u64)td_cmd  << I40E_TXD_QW1_CMD_SHIFT) |
			   ((u64)td_offset << I40E_TXD_QW1_OFFSET_SHIFT) |
			   ((u64)size  << I40E_TXD_QW1_TX_BUF_SZ_SHIFT) |
			   ((u64)td_tag  << I40E_TXD_QW1_L2TAG1_SHIFT));
}

/**
 * i40e_update_tx_stats - Update the egress statistics for the Tx ring
 * @tx_ring: Tx ring to update
 * @total_packet: total packets sent
 * @total_bytes: total bytes sent
 **/
static inline void i40e_update_tx_stats(struct i40e_ring *tx_ring,
					unsigned int total_packets,
					unsigned int total_bytes)
{
	u64_stats_update_begin(&tx_ring->syncp);
	tx_ring->stats.bytes += total_bytes;
	tx_ring->stats.packets += total_packets;
	u64_stats_update_end(&tx_ring->syncp);
	tx_ring->q_vector->tx.total_bytes += total_bytes;
	tx_ring->q_vector->tx.total_packets += total_packets;
}

#define WB_STRIDE 4

/**
 * i40e_arm_wb - (Possibly) arms Tx write-back
 * @tx_ring: Tx ring to update
 * @vsi: the VSI
 * @budget: the NAPI budget left
 **/
static inline void i40e_arm_wb(struct i40e_ring *tx_ring,
			       struct i40e_vsi *vsi,
			       int budget)
{
	if (tx_ring->flags & I40E_TXR_FLAGS_WB_ON_ITR) {
		/* check to see if there are < 4 descriptors
		 * waiting to be written back, then kick the hardware to force
		 * them to be written back in case we stay in NAPI.
		 * In this mode on X722 we do not enable Interrupt.
		 */
		unsigned int j = i40e_get_tx_pending(tx_ring, false);

		if (budget &&
		    ((j / WB_STRIDE) == 0) && j > 0 &&
		    !test_bit(__I40E_VSI_DOWN, vsi->state) &&
		    (I40E_DESC_UNUSED(tx_ring) != tx_ring->count))
			tx_ring->arm_wb = true;
	}
}

/**
 * i40e_rx_is_programming_status - check for programming status descriptor
 * @qword1: qword1 representing status_error_len in CPU ordering
 *
 * The value of in the descriptor length field indicate if this
 * is a programming status descriptor for flow director or FCoE
 * by the value of I40E_RX_PROG_STATUS_DESC_LENGTH, otherwise
 * it is a packet descriptor.
 **/
static inline bool i40e_rx_is_programming_status(u64 qword1)
{
	/* The Rx filter programming status and SPH bit occupy the same
	 * spot in the descriptor. Since we don't support packet split we
	 * can just reuse the bit as an indication that this is a
	 * programming status descriptor.
	 */
	return qword1 & I40E_RXD_QW1_LENGTH_SPH_MASK;
}

/**
 * i40e_inc_ntc: Advance the next_to_clean index
 * @rx_ring: Rx ring
 **/
static inline void i40e_inc_ntc(struct i40e_ring *rx_ring)
{
	u32 ntc = rx_ring->next_to_clean + 1;

	ntc = (ntc < rx_ring->count) ? ntc : 0;
	rx_ring->next_to_clean = ntc;
	prefetch(I40E_RX_DESC(rx_ring, ntc));
}

void i40e_xsk_clean_rx_ring(struct i40e_ring *rx_ring);
void i40e_xsk_clean_tx_ring(struct i40e_ring *tx_ring);
bool i40e_xsk_any_rx_ring_enabled(struct i40e_vsi *vsi);

#endif /* I40E_TXRX_COMMON_ */
