/*******************************************************************************
  This contains the functions to handle the enhanced descriptors.

  Copyright (C) 2007-2014  STMicroelectronics Ltd

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
*******************************************************************************/

#include <linux/stmmac.h>
#include "common.h"
#include "descs_com.h"

static int enh_desc_get_tx_status(void *data, struct stmmac_extra_stats *x,
				  struct dma_desc *p, void __iomem *ioaddr)
{
	struct net_device_stats *stats = (struct net_device_stats *)data;
	unsigned int tdes0 = le32_to_cpu(p->des0);
	int ret = tx_done;

	/* Get tx owner first */
	if (unlikely(tdes0 & ETDES0_OWN))
		return tx_dma_own;

	/* Verify tx error by looking at the last segment. */
	if (likely(!(tdes0 & ETDES0_LAST_SEGMENT)))
		return tx_not_ls;

	if (unlikely(tdes0 & ETDES0_ERROR_SUMMARY)) {
		if (unlikely(tdes0 & ETDES0_JABBER_TIMEOUT))
			x->tx_jabber++;

		if (unlikely(tdes0 & ETDES0_FRAME_FLUSHED)) {
			x->tx_frame_flushed++;
			dwmac_dma_flush_tx_fifo(ioaddr);
		}

		if (unlikely(tdes0 & ETDES0_LOSS_CARRIER)) {
			x->tx_losscarrier++;
			stats->tx_carrier_errors++;
		}
		if (unlikely(tdes0 & ETDES0_NO_CARRIER)) {
			x->tx_carrier++;
			stats->tx_carrier_errors++;
		}
		if (unlikely((tdes0 & ETDES0_LATE_COLLISION) ||
			     (tdes0 & ETDES0_EXCESSIVE_COLLISIONS)))
			stats->collisions +=
				(tdes0 & ETDES0_COLLISION_COUNT_MASK) >> 3;

		if (unlikely(tdes0 & ETDES0_EXCESSIVE_DEFERRAL))
			x->tx_deferred++;

		if (unlikely(tdes0 & ETDES0_UNDERFLOW_ERROR)) {
			dwmac_dma_flush_tx_fifo(ioaddr);
			x->tx_underflow++;
		}

		if (unlikely(tdes0 & ETDES0_IP_HEADER_ERROR))
			x->tx_ip_header_error++;

		if (unlikely(tdes0 & ETDES0_PAYLOAD_ERROR)) {
			x->tx_payload_error++;
			dwmac_dma_flush_tx_fifo(ioaddr);
		}

		ret = tx_err;
	}

	if (unlikely(tdes0 & ETDES0_DEFERRED))
		x->tx_deferred++;

#ifdef STMMAC_VLAN_TAG_USED
	if (tdes0 & ETDES0_VLAN_FRAME)
		x->tx_vlan++;
#endif

	return ret;
}

static int enh_desc_get_tx_len(struct dma_desc *p)
{
	return (le32_to_cpu(p->des1) & ETDES1_BUFFER1_SIZE_MASK);
}

static int enh_desc_coe_rdes0(int ipc_err, int type, int payload_err)
{
	int ret = good_frame;
	u32 status = (type << 2 | ipc_err << 1 | payload_err) & 0x7;

	/* bits 5 7 0 | Frame status
	 * ----------------------------------------------------------
	 *      0 0 0 | IEEE 802.3 Type frame (length < 1536 octects)
	 *      1 0 0 | IPv4/6 No CSUM errorS.
	 *      1 0 1 | IPv4/6 CSUM PAYLOAD error
	 *      1 1 0 | IPv4/6 CSUM IP HR error
	 *      1 1 1 | IPv4/6 IP PAYLOAD AND HEADER errorS
	 *      0 0 1 | IPv4/6 unsupported IP PAYLOAD
	 *      0 1 1 | COE bypassed.. no IPv4/6 frame
	 *      0 1 0 | Reserved.
	 */
	if (status == 0x0)
		ret = llc_snap;
	else if (status == 0x4)
		ret = good_frame;
	else if (status == 0x5)
		ret = csum_none;
	else if (status == 0x6)
		ret = csum_none;
	else if (status == 0x7)
		ret = csum_none;
	else if (status == 0x1)
		ret = discard_frame;
	else if (status == 0x3)
		ret = discard_frame;
	return ret;
}

static void enh_desc_get_ext_status(void *data, struct stmmac_extra_stats *x,
				    struct dma_extended_desc *p)
{
	unsigned int rdes0 = le32_to_cpu(p->basic.des0);
	unsigned int rdes4 = le32_to_cpu(p->des4);

	if (unlikely(rdes0 & ERDES0_RX_MAC_ADDR)) {
		int message_type = (rdes4 & ERDES4_MSG_TYPE_MASK) >> 8;

		if (rdes4 & ERDES4_IP_HDR_ERR)
			x->ip_hdr_err++;
		if (rdes4 & ERDES4_IP_PAYLOAD_ERR)
			x->ip_payload_err++;
		if (rdes4 & ERDES4_IP_CSUM_BYPASSED)
			x->ip_csum_bypassed++;
		if (rdes4 & ERDES4_IPV4_PKT_RCVD)
			x->ipv4_pkt_rcvd++;
		if (rdes4 & ERDES4_IPV6_PKT_RCVD)
			x->ipv6_pkt_rcvd++;

		if (message_type == RDES_EXT_NO_PTP)
			x->no_ptp_rx_msg_type_ext++;
		else if (message_type == RDES_EXT_SYNC)
			x->ptp_rx_msg_type_sync++;
		else if (message_type == RDES_EXT_FOLLOW_UP)
			x->ptp_rx_msg_type_follow_up++;
		else if (message_type == RDES_EXT_DELAY_REQ)
			x->ptp_rx_msg_type_delay_req++;
		else if (message_type == RDES_EXT_DELAY_RESP)
			x->ptp_rx_msg_type_delay_resp++;
		else if (message_type == RDES_EXT_PDELAY_REQ)
			x->ptp_rx_msg_type_pdelay_req++;
		else if (message_type == RDES_EXT_PDELAY_RESP)
			x->ptp_rx_msg_type_pdelay_resp++;
		else if (message_type == RDES_EXT_PDELAY_FOLLOW_UP)
			x->ptp_rx_msg_type_pdelay_follow_up++;
		else if (message_type == RDES_PTP_ANNOUNCE)
			x->ptp_rx_msg_type_announce++;
		else if (message_type == RDES_PTP_MANAGEMENT)
			x->ptp_rx_msg_type_management++;
		else if (message_type == RDES_PTP_PKT_RESERVED_TYPE)
			x->ptp_rx_msg_pkt_reserved_type++;

		if (rdes4 & ERDES4_PTP_FRAME_TYPE)
			x->ptp_frame_type++;
		if (rdes4 & ERDES4_PTP_VER)
			x->ptp_ver++;
		if (rdes4 & ERDES4_TIMESTAMP_DROPPED)
			x->timestamp_dropped++;
		if (rdes4 & ERDES4_AV_PKT_RCVD)
			x->av_pkt_rcvd++;
		if (rdes4 & ERDES4_AV_TAGGED_PKT_RCVD)
			x->av_tagged_pkt_rcvd++;
		if ((rdes4 & ERDES4_VLAN_TAG_PRI_VAL_MASK) >> 18)
			x->vlan_tag_priority_val++;
		if (rdes4 & ERDES4_L3_FILTER_MATCH)
			x->l3_filter_match++;
		if (rdes4 & ERDES4_L4_FILTER_MATCH)
			x->l4_filter_match++;
		if ((rdes4 & ERDES4_L3_L4_FILT_NO_MATCH_MASK) >> 26)
			x->l3_l4_filter_no_match++;
	}
}

static int enh_desc_get_rx_status(void *data, struct stmmac_extra_stats *x,
				  struct dma_desc *p)
{
	struct net_device_stats *stats = (struct net_device_stats *)data;
	unsigned int rdes0 = le32_to_cpu(p->des0);
	int ret = good_frame;

	if (unlikely(rdes0 & RDES0_OWN))
		return dma_own;

	if (unlikely(rdes0 & RDES0_ERROR_SUMMARY)) {
		if (unlikely(rdes0 & RDES0_DESCRIPTOR_ERROR)) {
			x->rx_desc++;
			stats->rx_length_errors++;
		}
		if (unlikely(rdes0 & RDES0_OVERFLOW_ERROR))
			x->rx_gmac_overflow++;

		if (unlikely(rdes0 & RDES0_IPC_CSUM_ERROR))
			pr_err("\tIPC Csum Error/Giant frame\n");

		if (unlikely(rdes0 & RDES0_COLLISION))
			stats->collisions++;
		if (unlikely(rdes0 & RDES0_RECEIVE_WATCHDOG))
			x->rx_watchdog++;

		if (unlikely(rdes0 & RDES0_MII_ERROR))	/* GMII */
			x->rx_mii++;

		if (unlikely(rdes0 & RDES0_CRC_ERROR)) {
			x->rx_crc_errors++;
			stats->rx_crc_errors++;
		}
		ret = discard_frame;
	}

	/* After a payload csum error, the ES bit is set.
	 * It doesn't match with the information reported into the databook.
	 * At any rate, we need to understand if the CSUM hw computation is ok
	 * and report this info to the upper layers. */
	ret = enh_desc_coe_rdes0(!!(rdes0 & RDES0_IPC_CSUM_ERROR),
				 !!(rdes0 & RDES0_FRAME_TYPE),
				 !!(rdes0 & ERDES0_RX_MAC_ADDR));

	if (unlikely(rdes0 & RDES0_DRIBBLING))
		x->dribbling_bit++;

	if (unlikely(rdes0 & RDES0_SA_FILTER_FAIL)) {
		x->sa_rx_filter_fail++;
		ret = discard_frame;
	}
	if (unlikely(rdes0 & RDES0_DA_FILTER_FAIL)) {
		x->da_rx_filter_fail++;
		ret = discard_frame;
	}
	if (unlikely(rdes0 & RDES0_LENGTH_ERROR)) {
		x->rx_length++;
		ret = discard_frame;
	}
#ifdef STMMAC_VLAN_TAG_USED
	if (rdes0 & RDES0_VLAN_TAG)
		x->rx_vlan++;
#endif

	return ret;
}

static void enh_desc_init_rx_desc(struct dma_desc *p, int disable_rx_ic,
				  int mode, int end)
{
	p->des0 |= cpu_to_le32(RDES0_OWN);
	p->des1 |= cpu_to_le32((BUF_SIZE_8KiB - 1) & ERDES1_BUFFER1_SIZE_MASK);

	if (mode == STMMAC_CHAIN_MODE)
		ehn_desc_rx_set_on_chain(p);
	else
		ehn_desc_rx_set_on_ring(p, end);

	if (disable_rx_ic)
		p->des1 |= cpu_to_le32(ERDES1_DISABLE_IC);
}

static void enh_desc_init_tx_desc(struct dma_desc *p, int mode, int end)
{
	p->des0 &= cpu_to_le32(~ETDES0_OWN);
	if (mode == STMMAC_CHAIN_MODE)
		enh_desc_end_tx_desc_on_chain(p);
	else
		enh_desc_end_tx_desc_on_ring(p, end);
}

static int enh_desc_get_tx_owner(struct dma_desc *p)
{
	return (le32_to_cpu(p->des0) & ETDES0_OWN) >> 31;
}

static void enh_desc_set_tx_owner(struct dma_desc *p)
{
	p->des0 |= cpu_to_le32(ETDES0_OWN);
}

static void enh_desc_set_rx_owner(struct dma_desc *p)
{
	p->des0 |= cpu_to_le32(RDES0_OWN);
}

static int enh_desc_get_tx_ls(struct dma_desc *p)
{
	return (le32_to_cpu(p->des0) & ETDES0_LAST_SEGMENT) >> 29;
}

static void enh_desc_release_tx_desc(struct dma_desc *p, int mode)
{
	int ter = (le32_to_cpu(p->des0) & ETDES0_END_RING) >> 21;

	memset(p, 0, offsetof(struct dma_desc, des2));
	if (mode == STMMAC_CHAIN_MODE)
		enh_desc_end_tx_desc_on_chain(p);
	else
		enh_desc_end_tx_desc_on_ring(p, ter);
}

static void enh_desc_prepare_tx_desc(struct dma_desc *p, int is_fs, int len,
				     bool csum_flag, int mode, bool tx_own,
				     bool ls, unsigned int tot_pkt_len)
{
	unsigned int tdes0 = le32_to_cpu(p->des0);

	if (mode == STMMAC_CHAIN_MODE)
		enh_set_tx_desc_len_on_chain(p, len);
	else
		enh_set_tx_desc_len_on_ring(p, len);

	if (is_fs)
		tdes0 |= ETDES0_FIRST_SEGMENT;
	else
		tdes0 &= ~ETDES0_FIRST_SEGMENT;

	if (likely(csum_flag))
		tdes0 |= (TX_CIC_FULL << ETDES0_CHECKSUM_INSERTION_SHIFT);
	else
		tdes0 &= ~(TX_CIC_FULL << ETDES0_CHECKSUM_INSERTION_SHIFT);

	if (ls)
		tdes0 |= ETDES0_LAST_SEGMENT;

	/* Finally set the OWN bit. Later the DMA will start! */
	if (tx_own)
		tdes0 |= ETDES0_OWN;

	if (is_fs & tx_own)
		/* When the own bit, for the first frame, has to be set, all
		 * descriptors for the same frame has to be set before, to
		 * avoid race condition.
		 */
		dma_wmb();

	p->des0 = cpu_to_le32(tdes0);
}

static void enh_desc_set_tx_ic(struct dma_desc *p)
{
	p->des0 |= cpu_to_le32(ETDES0_INTERRUPT);
}

static int enh_desc_get_rx_frame_len(struct dma_desc *p, int rx_coe_type)
{
	unsigned int csum = 0;
	/* The type-1 checksum offload engines append the checksum at
	 * the end of frame and the two bytes of checksum are added in
	 * the length.
	 * Adjust for that in the framelen for type-1 checksum offload
	 * engines.
	 */
	if (rx_coe_type == STMMAC_RX_COE_TYPE1)
		csum = 2;

	return (((le32_to_cpu(p->des0) & RDES0_FRAME_LEN_MASK)
				>> RDES0_FRAME_LEN_SHIFT) - csum);
}

static void enh_desc_enable_tx_timestamp(struct dma_desc *p)
{
	p->des0 |= cpu_to_le32(ETDES0_TIME_STAMP_ENABLE);
}

static int enh_desc_get_tx_timestamp_status(struct dma_desc *p)
{
	return (le32_to_cpu(p->des0) & ETDES0_TIME_STAMP_STATUS) >> 17;
}

static u64 enh_desc_get_timestamp(void *desc, u32 ats)
{
	u64 ns;

	if (ats) {
		struct dma_extended_desc *p = (struct dma_extended_desc *)desc;
		ns = le32_to_cpu(p->des6);
		/* convert high/sec time stamp value to nanosecond */
		ns += le32_to_cpu(p->des7) * 1000000000ULL;
	} else {
		struct dma_desc *p = (struct dma_desc *)desc;
		ns = le32_to_cpu(p->des2);
		ns += le32_to_cpu(p->des3) * 1000000000ULL;
	}

	return ns;
}

static int enh_desc_get_rx_timestamp_status(void *desc, void *next_desc,
					    u32 ats)
{
	if (ats) {
		struct dma_extended_desc *p = (struct dma_extended_desc *)desc;
		return (le32_to_cpu(p->basic.des0) & RDES0_IPC_CSUM_ERROR) >> 7;
	} else {
		struct dma_desc *p = (struct dma_desc *)desc;
		if ((le32_to_cpu(p->des2) == 0xffffffff) &&
		    (le32_to_cpu(p->des3) == 0xffffffff))
			/* timestamp is corrupted, hence don't store it */
			return 0;
		else
			return 1;
	}
}

static void enh_desc_display_ring(void *head, unsigned int size, bool rx)
{
	struct dma_extended_desc *ep = (struct dma_extended_desc *)head;
	int i;

	pr_info("Extended %s descriptor ring:\n", rx ? "RX" : "TX");

	for (i = 0; i < size; i++) {
		u64 x;

		x = *(u64 *)ep;
		pr_info("%d [0x%x]: 0x%x 0x%x 0x%x 0x%x\n",
			i, (unsigned int)virt_to_phys(ep),
			(unsigned int)x, (unsigned int)(x >> 32),
			ep->basic.des2, ep->basic.des3);
		ep++;
	}
	pr_info("\n");
}

const struct stmmac_desc_ops enh_desc_ops = {
	.tx_status = enh_desc_get_tx_status,
	.rx_status = enh_desc_get_rx_status,
	.get_tx_len = enh_desc_get_tx_len,
	.init_rx_desc = enh_desc_init_rx_desc,
	.init_tx_desc = enh_desc_init_tx_desc,
	.get_tx_owner = enh_desc_get_tx_owner,
	.release_tx_desc = enh_desc_release_tx_desc,
	.prepare_tx_desc = enh_desc_prepare_tx_desc,
	.set_tx_ic = enh_desc_set_tx_ic,
	.get_tx_ls = enh_desc_get_tx_ls,
	.set_tx_owner = enh_desc_set_tx_owner,
	.set_rx_owner = enh_desc_set_rx_owner,
	.get_rx_frame_len = enh_desc_get_rx_frame_len,
	.rx_extended_status = enh_desc_get_ext_status,
	.enable_tx_timestamp = enh_desc_enable_tx_timestamp,
	.get_tx_timestamp_status = enh_desc_get_tx_timestamp_status,
	.get_timestamp = enh_desc_get_timestamp,
	.get_rx_timestamp_status = enh_desc_get_rx_timestamp_status,
	.display_ring = enh_desc_display_ring,
};
