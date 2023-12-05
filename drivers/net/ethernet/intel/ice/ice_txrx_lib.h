/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019, Intel Corporation. */

#ifndef _ICE_TXRX_LIB_H_
#define _ICE_TXRX_LIB_H_
#include "ice.h"

/**
 * ice_set_rx_bufs_act - propagate Rx buffer action to frags
 * @xdp: XDP buffer representing frame (linear and frags part)
 * @rx_ring: Rx ring struct
 * act: action to store onto Rx buffers related to XDP buffer parts
 *
 * Set action that should be taken before putting Rx buffer from first frag
 * to one before last. Last one is handled by caller of this function as it
 * is the EOP frag that is currently being processed. This function is
 * supposed to be called only when XDP buffer contains frags.
 */
static inline void
ice_set_rx_bufs_act(struct xdp_buff *xdp, const struct ice_rx_ring *rx_ring,
		    const unsigned int act)
{
	const struct skb_shared_info *sinfo = xdp_get_shared_info_from_buff(xdp);
	u32 first = rx_ring->first_desc;
	u32 nr_frags = sinfo->nr_frags;
	u32 cnt = rx_ring->count;
	struct ice_rx_buf *buf;

	for (int i = 0; i < nr_frags; i++) {
		buf = &rx_ring->rx_buf[first];
		buf->act = act;

		if (++first == cnt)
			first = 0;
	}
}

/**
 * ice_test_staterr - tests bits in Rx descriptor status and error fields
 * @status_err_n: Rx descriptor status_error0 or status_error1 bits
 * @stat_err_bits: value to mask
 *
 * This function does some fast chicanery in order to return the
 * value of the mask which is really only used for boolean tests.
 * The status_error_len doesn't need to be shifted because it begins
 * at offset zero.
 */
static inline bool
ice_test_staterr(__le16 status_err_n, const u16 stat_err_bits)
{
	return !!(status_err_n & cpu_to_le16(stat_err_bits));
}

/**
 * ice_is_non_eop - process handling of non-EOP buffers
 * @rx_ring: Rx ring being processed
 * @rx_desc: Rx descriptor for current buffer
 *
 * If the buffer is an EOP buffer, this function exits returning false,
 * otherwise return true indicating that this is in fact a non-EOP buffer.
 */
static inline bool
ice_is_non_eop(const struct ice_rx_ring *rx_ring,
	       const union ice_32b_rx_flex_desc *rx_desc)
{
	/* if we are the last buffer then there is nothing else to do */
#define ICE_RXD_EOF BIT(ICE_RX_FLEX_DESC_STATUS0_EOF_S)
	if (likely(ice_test_staterr(rx_desc->wb.status_error0, ICE_RXD_EOF)))
		return false;

	rx_ring->ring_stats->rx_stats.non_eop_descs++;

	return true;
}

static inline __le64
ice_build_ctob(u64 td_cmd, u64 td_offset, unsigned int size, u64 td_tag)
{
	return cpu_to_le64(ICE_TX_DESC_DTYPE_DATA |
			   (td_cmd    << ICE_TXD_QW1_CMD_S) |
			   (td_offset << ICE_TXD_QW1_OFFSET_S) |
			   ((u64)size << ICE_TXD_QW1_TX_BUF_SZ_S) |
			   (td_tag    << ICE_TXD_QW1_L2TAG1_S));
}

/**
 * ice_get_vlan_tci - get VLAN TCI from Rx flex descriptor
 * @rx_desc: Rx 32b flex descriptor with RXDID=2
 *
 * The OS and current PF implementation only support stripping a single VLAN tag
 * at a time, so there should only ever be 0 or 1 tags in the l2tag* fields. If
 * one is found return the tag, else return 0 to mean no VLAN tag was found.
 */
static inline u16
ice_get_vlan_tci(const union ice_32b_rx_flex_desc *rx_desc)
{
	u16 stat_err_bits;

	stat_err_bits = BIT(ICE_RX_FLEX_DESC_STATUS0_L2TAG1P_S);
	if (ice_test_staterr(rx_desc->wb.status_error0, stat_err_bits))
		return le16_to_cpu(rx_desc->wb.l2tag1);

	stat_err_bits = BIT(ICE_RX_FLEX_DESC_STATUS1_L2TAG2P_S);
	if (ice_test_staterr(rx_desc->wb.status_error1, stat_err_bits))
		return le16_to_cpu(rx_desc->wb.l2tag2_2nd);

	return 0;
}

/**
 * ice_xdp_ring_update_tail - Updates the XDP Tx ring tail register
 * @xdp_ring: XDP Tx ring
 *
 * This function updates the XDP Tx ring tail register.
 */
static inline void ice_xdp_ring_update_tail(struct ice_tx_ring *xdp_ring)
{
	/* Force memory writes to complete before letting h/w
	 * know there are new descriptors to fetch.
	 */
	wmb();
	writel_relaxed(xdp_ring->next_to_use, xdp_ring->tail);
}

/**
 * ice_set_rs_bit - set RS bit on last produced descriptor (one behind current NTU)
 * @xdp_ring: XDP ring to produce the HW Tx descriptors on
 *
 * returns index of descriptor that had RS bit produced on
 */
static inline u32 ice_set_rs_bit(const struct ice_tx_ring *xdp_ring)
{
	u32 rs_idx = xdp_ring->next_to_use ? xdp_ring->next_to_use - 1 : xdp_ring->count - 1;
	struct ice_tx_desc *tx_desc;

	tx_desc = ICE_TX_DESC(xdp_ring, rs_idx);
	tx_desc->cmd_type_offset_bsz |=
		cpu_to_le64(ICE_TX_DESC_CMD_RS << ICE_TXD_QW1_CMD_S);

	return rs_idx;
}

void ice_finalize_xdp_rx(struct ice_tx_ring *xdp_ring, unsigned int xdp_res, u32 first_idx);
int ice_xmit_xdp_buff(struct xdp_buff *xdp, struct ice_tx_ring *xdp_ring);
int __ice_xmit_xdp_ring(struct xdp_buff *xdp, struct ice_tx_ring *xdp_ring,
			bool frame);
void ice_release_rx_desc(struct ice_rx_ring *rx_ring, u16 val);
void
ice_process_skb_fields(struct ice_rx_ring *rx_ring,
		       union ice_32b_rx_flex_desc *rx_desc,
		       struct sk_buff *skb);
void
ice_receive_skb(struct ice_rx_ring *rx_ring, struct sk_buff *skb, u16 vlan_tci);

static inline void
ice_xdp_meta_set_desc(struct xdp_buff *xdp,
		      union ice_32b_rx_flex_desc *eop_desc)
{
	struct ice_xdp_buff *xdp_ext = container_of(xdp, struct ice_xdp_buff,
						    xdp_buff);

	xdp_ext->eop_desc = eop_desc;
}
#endif /* !_ICE_TXRX_LIB_H_ */
