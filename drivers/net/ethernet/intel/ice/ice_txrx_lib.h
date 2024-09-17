/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019, Intel Corporation. */

#ifndef _ICE_TXRX_LIB_H_
#define _ICE_TXRX_LIB_H_
#include "ice.h"

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
 * ice_get_vlan_tag_from_rx_desc - get VLAN from Rx flex descriptor
 * @rx_desc: Rx 32b flex descriptor with RXDID=2
 *
 * The OS and current PF implementation only support stripping a single VLAN tag
 * at a time, so there should only ever be 0 or 1 tags in the l2tag* fields. If
 * one is found return the tag, else return 0 to mean no VLAN tag was found.
 */
static inline u16
ice_get_vlan_tag_from_rx_desc(union ice_32b_rx_flex_desc *rx_desc)
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

void ice_finalize_xdp_rx(struct ice_tx_ring *xdp_ring, unsigned int xdp_res);
int ice_xmit_xdp_buff(struct xdp_buff *xdp, struct ice_tx_ring *xdp_ring);
int ice_xmit_xdp_ring(void *data, u16 size, struct ice_tx_ring *xdp_ring);
void ice_release_rx_desc(struct ice_rx_ring *rx_ring, u16 val);
void
ice_process_skb_fields(struct ice_rx_ring *rx_ring,
		       union ice_32b_rx_flex_desc *rx_desc,
		       struct sk_buff *skb, u16 ptype);
void
ice_receive_skb(struct ice_rx_ring *rx_ring, struct sk_buff *skb, u16 vlan_tag);
#endif /* !_ICE_TXRX_LIB_H_ */
