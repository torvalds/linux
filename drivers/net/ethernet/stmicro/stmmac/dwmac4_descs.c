// SPDX-License-Identifier: GPL-2.0-only
/*
 * This contains the functions to handle the descriptors for DesignWare databook
 * 4.xx.
 *
 * Copyright (C) 2015  STMicroelectronics Ltd
 *
 * Author: Alexandre Torgue <alexandre.torgue@st.com>
 */

#include <linux/stmmac.h>
#include "common.h"
#include "dwmac4.h"
#include "dwmac4_descs.h"

static int dwmac4_wrback_get_tx_status(void *data, struct stmmac_extra_stats *x,
				       struct dma_desc *p,
				       void __iomem *ioaddr)
{
	struct net_device_stats *stats = (struct net_device_stats *)data;
	unsigned int tdes3;
	int ret = tx_done;

	tdes3 = le32_to_cpu(p->des3);

	/* Get tx owner first */
	if (unlikely(tdes3 & TDES3_OWN))
		return tx_dma_own;

	/* Verify tx error by looking at the last segment. */
	if (likely(!(tdes3 & TDES3_LAST_DESCRIPTOR)))
		return tx_not_ls;

	if (unlikely(tdes3 & TDES3_ERROR_SUMMARY)) {
		ret = tx_err;

		if (unlikely(tdes3 & TDES3_JABBER_TIMEOUT))
			x->tx_jabber++;
		if (unlikely(tdes3 & TDES3_PACKET_FLUSHED))
			x->tx_frame_flushed++;
		if (unlikely(tdes3 & TDES3_LOSS_CARRIER)) {
			x->tx_losscarrier++;
			stats->tx_carrier_errors++;
		}
		if (unlikely(tdes3 & TDES3_NO_CARRIER)) {
			x->tx_carrier++;
			stats->tx_carrier_errors++;
		}
		if (unlikely((tdes3 & TDES3_LATE_COLLISION) ||
			     (tdes3 & TDES3_EXCESSIVE_COLLISION)))
			stats->collisions +=
			    (tdes3 & TDES3_COLLISION_COUNT_MASK)
			    >> TDES3_COLLISION_COUNT_SHIFT;

		if (unlikely(tdes3 & TDES3_EXCESSIVE_DEFERRAL))
			x->tx_deferred++;

		if (unlikely(tdes3 & TDES3_UNDERFLOW_ERROR)) {
			x->tx_underflow++;
			ret |= tx_err_bump_tc;
		}

		if (unlikely(tdes3 & TDES3_IP_HDR_ERROR))
			x->tx_ip_header_error++;

		if (unlikely(tdes3 & TDES3_PAYLOAD_ERROR))
			x->tx_payload_error++;
	}

	if (unlikely(tdes3 & TDES3_DEFERRED))
		x->tx_deferred++;

	return ret;
}

static int dwmac4_wrback_get_rx_status(void *data, struct stmmac_extra_stats *x,
				       struct dma_desc *p)
{
	struct net_device_stats *stats = (struct net_device_stats *)data;
	unsigned int rdes1 = le32_to_cpu(p->des1);
	unsigned int rdes2 = le32_to_cpu(p->des2);
	unsigned int rdes3 = le32_to_cpu(p->des3);
	int message_type;
	int ret = good_frame;

	if (unlikely(rdes3 & RDES3_OWN))
		return dma_own;

	if (unlikely(rdes3 & RDES3_CONTEXT_DESCRIPTOR))
		return discard_frame;
	if (likely(!(rdes3 & RDES3_LAST_DESCRIPTOR)))
		return rx_not_ls;

	if (unlikely(rdes3 & RDES3_ERROR_SUMMARY)) {
		if (unlikely(rdes3 & RDES3_GIANT_PACKET))
			stats->rx_length_errors++;
		if (unlikely(rdes3 & RDES3_OVERFLOW_ERROR))
			x->rx_gmac_overflow++;

		if (unlikely(rdes3 & RDES3_RECEIVE_WATCHDOG))
			x->rx_watchdog++;

		if (unlikely(rdes3 & RDES3_RECEIVE_ERROR))
			x->rx_mii++;

		if (unlikely(rdes3 & RDES3_CRC_ERROR)) {
			x->rx_crc_errors++;
			stats->rx_crc_errors++;
		}

		if (unlikely(rdes3 & RDES3_DRIBBLE_ERROR))
			x->dribbling_bit++;

		ret = discard_frame;
	}

	message_type = (rdes1 & ERDES4_MSG_TYPE_MASK) >> 8;

	if (rdes1 & RDES1_IP_HDR_ERROR)
		x->ip_hdr_err++;
	if (rdes1 & RDES1_IP_CSUM_BYPASSED)
		x->ip_csum_bypassed++;
	if (rdes1 & RDES1_IPV4_HEADER)
		x->ipv4_pkt_rcvd++;
	if (rdes1 & RDES1_IPV6_HEADER)
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

	if (rdes1 & RDES1_PTP_PACKET_TYPE)
		x->ptp_frame_type++;
	if (rdes1 & RDES1_PTP_VER)
		x->ptp_ver++;
	if (rdes1 & RDES1_TIMESTAMP_DROPPED)
		x->timestamp_dropped++;

	if (unlikely(rdes2 & RDES2_SA_FILTER_FAIL)) {
		x->sa_rx_filter_fail++;
		ret = discard_frame;
	}
	if (unlikely(rdes2 & RDES2_DA_FILTER_FAIL)) {
		x->da_rx_filter_fail++;
		ret = discard_frame;
	}

	if (rdes2 & RDES2_L3_FILTER_MATCH)
		x->l3_filter_match++;
	if (rdes2 & RDES2_L4_FILTER_MATCH)
		x->l4_filter_match++;
	if ((rdes2 & RDES2_L3_L4_FILT_NB_MATCH_MASK)
	    >> RDES2_L3_L4_FILT_NB_MATCH_SHIFT)
		x->l3_l4_filter_no_match++;

	return ret;
}

static int dwmac4_rd_get_tx_len(struct dma_desc *p)
{
	return (le32_to_cpu(p->des2) & TDES2_BUFFER1_SIZE_MASK);
}

static int dwmac4_get_tx_owner(struct dma_desc *p)
{
	return (le32_to_cpu(p->des3) & TDES3_OWN) >> TDES3_OWN_SHIFT;
}

static void dwmac4_set_tx_owner(struct dma_desc *p)
{
	p->des3 |= cpu_to_le32(TDES3_OWN);
}

static void dwmac4_set_rx_owner(struct dma_desc *p, int disable_rx_ic)
{
	p->des3 |= cpu_to_le32(RDES3_OWN | RDES3_BUFFER1_VALID_ADDR);

	if (!disable_rx_ic)
		p->des3 |= cpu_to_le32(RDES3_INT_ON_COMPLETION_EN);
}

static int dwmac4_get_tx_ls(struct dma_desc *p)
{
	return (le32_to_cpu(p->des3) & TDES3_LAST_DESCRIPTOR)
		>> TDES3_LAST_DESCRIPTOR_SHIFT;
}

static int dwmac4_wrback_get_rx_frame_len(struct dma_desc *p, int rx_coe)
{
	return (le32_to_cpu(p->des3) & RDES3_PACKET_SIZE_MASK);
}

static void dwmac4_rd_enable_tx_timestamp(struct dma_desc *p)
{
	p->des2 |= cpu_to_le32(TDES2_TIMESTAMP_ENABLE);
}

static int dwmac4_wrback_get_tx_timestamp_status(struct dma_desc *p)
{
	/* Context type from W/B descriptor must be zero */
	if (le32_to_cpu(p->des3) & TDES3_CONTEXT_TYPE)
		return 0;

	/* Tx Timestamp Status is 1 so des0 and des1'll have valid values */
	if (le32_to_cpu(p->des3) & TDES3_TIMESTAMP_STATUS)
		return 1;

	return 0;
}

static inline void dwmac4_get_timestamp(void *desc, u32 ats, u64 *ts)
{
	struct dma_desc *p = (struct dma_desc *)desc;
	u64 ns;

	ns = le32_to_cpu(p->des0);
	/* convert high/sec time stamp value to nanosecond */
	ns += le32_to_cpu(p->des1) * 1000000000ULL;

	*ts = ns;
}

static int dwmac4_rx_check_timestamp(void *desc)
{
	struct dma_desc *p = (struct dma_desc *)desc;
	unsigned int rdes0 = le32_to_cpu(p->des0);
	unsigned int rdes1 = le32_to_cpu(p->des1);
	unsigned int rdes3 = le32_to_cpu(p->des3);
	u32 own, ctxt;
	int ret = 1;

	own = rdes3 & RDES3_OWN;
	ctxt = ((rdes3 & RDES3_CONTEXT_DESCRIPTOR)
		>> RDES3_CONTEXT_DESCRIPTOR_SHIFT);

	if (likely(!own && ctxt)) {
		if ((rdes0 == 0xffffffff) && (rdes1 == 0xffffffff))
			/* Corrupted value */
			ret = -EINVAL;
		else
			/* A valid Timestamp is ready to be read */
			ret = 0;
	}

	/* Timestamp not ready */
	return ret;
}

static int dwmac4_wrback_get_rx_timestamp_status(void *desc, void *next_desc,
						 u32 ats)
{
	struct dma_desc *p = (struct dma_desc *)desc;
	int ret = -EINVAL;

	/* Get the status from normal w/b descriptor */
	if (likely(le32_to_cpu(p->des3) & RDES3_RDES1_VALID)) {
		if (likely(le32_to_cpu(p->des1) & RDES1_TIMESTAMP_AVAILABLE)) {
			int i = 0;

			/* Check if timestamp is OK from context descriptor */
			do {
				ret = dwmac4_rx_check_timestamp(next_desc);
				if (ret < 0)
					goto exit;
				i++;

			} while ((ret == 1) && (i < 10));

			if (i == 10)
				ret = -EBUSY;
		}
	}
exit:
	if (likely(ret == 0))
		return 1;

	return 0;
}

static void dwmac4_rd_init_rx_desc(struct dma_desc *p, int disable_rx_ic,
				   int mode, int end, int bfsize)
{
	dwmac4_set_rx_owner(p, disable_rx_ic);
}

static void dwmac4_rd_init_tx_desc(struct dma_desc *p, int mode, int end)
{
	p->des0 = 0;
	p->des1 = 0;
	p->des2 = 0;
	p->des3 = 0;
}

static void dwmac4_rd_prepare_tx_desc(struct dma_desc *p, int is_fs, int len,
				      bool csum_flag, int mode, bool tx_own,
				      bool ls, unsigned int tot_pkt_len)
{
	unsigned int tdes3 = le32_to_cpu(p->des3);

	p->des2 |= cpu_to_le32(len & TDES2_BUFFER1_SIZE_MASK);

	tdes3 |= tot_pkt_len & TDES3_PACKET_SIZE_MASK;
	if (is_fs)
		tdes3 |= TDES3_FIRST_DESCRIPTOR;
	else
		tdes3 &= ~TDES3_FIRST_DESCRIPTOR;

	if (likely(csum_flag))
		tdes3 |= (TX_CIC_FULL << TDES3_CHECKSUM_INSERTION_SHIFT);
	else
		tdes3 &= ~(TX_CIC_FULL << TDES3_CHECKSUM_INSERTION_SHIFT);

	if (ls)
		tdes3 |= TDES3_LAST_DESCRIPTOR;
	else
		tdes3 &= ~TDES3_LAST_DESCRIPTOR;

	/* Finally set the OWN bit. Later the DMA will start! */
	if (tx_own)
		tdes3 |= TDES3_OWN;

	if (is_fs && tx_own)
		/* When the own bit, for the first frame, has to be set, all
		 * descriptors for the same frame has to be set before, to
		 * avoid race condition.
		 */
		dma_wmb();

	p->des3 = cpu_to_le32(tdes3);
}

static void dwmac4_rd_prepare_tso_tx_desc(struct dma_desc *p, int is_fs,
					  int len1, int len2, bool tx_own,
					  bool ls, unsigned int tcphdrlen,
					  unsigned int tcppayloadlen)
{
	unsigned int tdes3 = le32_to_cpu(p->des3);

	if (len1)
		p->des2 |= cpu_to_le32((len1 & TDES2_BUFFER1_SIZE_MASK));

	if (len2)
		p->des2 |= cpu_to_le32((len2 << TDES2_BUFFER2_SIZE_MASK_SHIFT)
			    & TDES2_BUFFER2_SIZE_MASK);

	if (is_fs) {
		tdes3 |= TDES3_FIRST_DESCRIPTOR |
			 TDES3_TCP_SEGMENTATION_ENABLE |
			 ((tcphdrlen << TDES3_HDR_LEN_SHIFT) &
			  TDES3_SLOT_NUMBER_MASK) |
			 ((tcppayloadlen & TDES3_TCP_PKT_PAYLOAD_MASK));
	} else {
		tdes3 &= ~TDES3_FIRST_DESCRIPTOR;
	}

	if (ls)
		tdes3 |= TDES3_LAST_DESCRIPTOR;
	else
		tdes3 &= ~TDES3_LAST_DESCRIPTOR;

	/* Finally set the OWN bit. Later the DMA will start! */
	if (tx_own)
		tdes3 |= TDES3_OWN;

	if (is_fs && tx_own)
		/* When the own bit, for the first frame, has to be set, all
		 * descriptors for the same frame has to be set before, to
		 * avoid race condition.
		 */
		dma_wmb();

	p->des3 = cpu_to_le32(tdes3);
}

static void dwmac4_release_tx_desc(struct dma_desc *p, int mode)
{
	p->des0 = 0;
	p->des1 = 0;
	p->des2 = 0;
	p->des3 = 0;
}

static void dwmac4_rd_set_tx_ic(struct dma_desc *p)
{
	p->des2 |= cpu_to_le32(TDES2_INTERRUPT_ON_COMPLETION);
}

static void dwmac4_display_ring(void *head, unsigned int size, bool rx,
				dma_addr_t dma_rx_phy, unsigned int desc_size)
{
	dma_addr_t dma_addr;
	int i;

	pr_info("%s descriptor ring:\n", rx ? "RX" : "TX");

	if (desc_size == sizeof(struct dma_desc)) {
		struct dma_desc *p = (struct dma_desc *)head;

		for (i = 0; i < size; i++) {
			dma_addr = dma_rx_phy + i * sizeof(*p);
			pr_info("%03d [%pad]: 0x%x 0x%x 0x%x 0x%x\n",
				i, &dma_addr,
				le32_to_cpu(p->des0), le32_to_cpu(p->des1),
				le32_to_cpu(p->des2), le32_to_cpu(p->des3));
			p++;
		}
	} else if (desc_size == sizeof(struct dma_extended_desc)) {
		struct dma_extended_desc *extp = (struct dma_extended_desc *)head;

		for (i = 0; i < size; i++) {
			dma_addr = dma_rx_phy + i * sizeof(*extp);
			pr_info("%03d [%pad]: 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n",
				i, &dma_addr,
				le32_to_cpu(extp->basic.des0), le32_to_cpu(extp->basic.des1),
				le32_to_cpu(extp->basic.des2), le32_to_cpu(extp->basic.des3),
				le32_to_cpu(extp->des4), le32_to_cpu(extp->des5),
				le32_to_cpu(extp->des6), le32_to_cpu(extp->des7));
			extp++;
		}
	} else if (desc_size == sizeof(struct dma_edesc)) {
		struct dma_edesc *ep = (struct dma_edesc *)head;

		for (i = 0; i < size; i++) {
			dma_addr = dma_rx_phy + i * sizeof(*ep);
			pr_info("%03d [%pad]: 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n",
				i, &dma_addr,
				le32_to_cpu(ep->des4), le32_to_cpu(ep->des5),
				le32_to_cpu(ep->des6), le32_to_cpu(ep->des7),
				le32_to_cpu(ep->basic.des0), le32_to_cpu(ep->basic.des1),
				le32_to_cpu(ep->basic.des2), le32_to_cpu(ep->basic.des3));
			ep++;
		}
	} else {
		pr_err("unsupported descriptor!");
	}
}

static void dwmac4_set_mss_ctxt(struct dma_desc *p, unsigned int mss)
{
	p->des0 = 0;
	p->des1 = 0;
	p->des2 = cpu_to_le32(mss);
	p->des3 = cpu_to_le32(TDES3_CONTEXT_TYPE | TDES3_CTXT_TCMSSV);
}

static void dwmac4_get_addr(struct dma_desc *p, unsigned int *addr)
{
	*addr = le32_to_cpu(p->des0);
}

static void dwmac4_set_addr(struct dma_desc *p, dma_addr_t addr)
{
	p->des0 = cpu_to_le32(lower_32_bits(addr));
	p->des1 = cpu_to_le32(upper_32_bits(addr));
}

static void dwmac4_clear(struct dma_desc *p)
{
	p->des0 = 0;
	p->des1 = 0;
	p->des2 = 0;
	p->des3 = 0;
}

static void dwmac4_set_sarc(struct dma_desc *p, u32 sarc_type)
{
	sarc_type <<= TDES3_SA_INSERT_CTRL_SHIFT;

	p->des3 |= cpu_to_le32(sarc_type & TDES3_SA_INSERT_CTRL_MASK);
}

static int set_16kib_bfsize(int mtu)
{
	int ret = 0;

	if (unlikely(mtu >= BUF_SIZE_8KiB))
		ret = BUF_SIZE_16KiB;
	return ret;
}

static void dwmac4_set_vlan_tag(struct dma_desc *p, u16 tag, u16 inner_tag,
				u32 inner_type)
{
	p->des0 = 0;
	p->des1 = 0;
	p->des2 = 0;
	p->des3 = 0;

	/* Inner VLAN */
	if (inner_type) {
		u32 des = inner_tag << TDES2_IVT_SHIFT;

		des &= TDES2_IVT_MASK;
		p->des2 = cpu_to_le32(des);

		des = inner_type << TDES3_IVTIR_SHIFT;
		des &= TDES3_IVTIR_MASK;
		p->des3 = cpu_to_le32(des | TDES3_IVLTV);
	}

	/* Outer VLAN */
	p->des3 |= cpu_to_le32(tag & TDES3_VLAN_TAG);
	p->des3 |= cpu_to_le32(TDES3_VLTV);

	p->des3 |= cpu_to_le32(TDES3_CONTEXT_TYPE);
}

static void dwmac4_set_vlan(struct dma_desc *p, u32 type)
{
	type <<= TDES2_VLAN_TAG_SHIFT;
	p->des2 |= cpu_to_le32(type & TDES2_VLAN_TAG_MASK);
}

static void dwmac4_get_rx_header_len(struct dma_desc *p, unsigned int *len)
{
	*len = le32_to_cpu(p->des2) & RDES2_HL;
}

static void dwmac4_set_sec_addr(struct dma_desc *p, dma_addr_t addr, bool buf2_valid)
{
	p->des2 = cpu_to_le32(lower_32_bits(addr));
	p->des3 = cpu_to_le32(upper_32_bits(addr));

	if (buf2_valid)
		p->des3 |= cpu_to_le32(RDES3_BUFFER2_VALID_ADDR);
	else
		p->des3 &= cpu_to_le32(~RDES3_BUFFER2_VALID_ADDR);
}

static void dwmac4_set_tbs(struct dma_edesc *p, u32 sec, u32 nsec)
{
	p->des4 = cpu_to_le32((sec & TDES4_LT) | TDES4_LTV);
	p->des5 = cpu_to_le32(nsec & TDES5_LT);
	p->des6 = 0;
	p->des7 = 0;
}

const struct stmmac_desc_ops dwmac4_desc_ops = {
	.tx_status = dwmac4_wrback_get_tx_status,
	.rx_status = dwmac4_wrback_get_rx_status,
	.get_tx_len = dwmac4_rd_get_tx_len,
	.get_tx_owner = dwmac4_get_tx_owner,
	.set_tx_owner = dwmac4_set_tx_owner,
	.set_rx_owner = dwmac4_set_rx_owner,
	.get_tx_ls = dwmac4_get_tx_ls,
	.get_rx_frame_len = dwmac4_wrback_get_rx_frame_len,
	.enable_tx_timestamp = dwmac4_rd_enable_tx_timestamp,
	.get_tx_timestamp_status = dwmac4_wrback_get_tx_timestamp_status,
	.get_rx_timestamp_status = dwmac4_wrback_get_rx_timestamp_status,
	.get_timestamp = dwmac4_get_timestamp,
	.set_tx_ic = dwmac4_rd_set_tx_ic,
	.prepare_tx_desc = dwmac4_rd_prepare_tx_desc,
	.prepare_tso_tx_desc = dwmac4_rd_prepare_tso_tx_desc,
	.release_tx_desc = dwmac4_release_tx_desc,
	.init_rx_desc = dwmac4_rd_init_rx_desc,
	.init_tx_desc = dwmac4_rd_init_tx_desc,
	.display_ring = dwmac4_display_ring,
	.set_mss = dwmac4_set_mss_ctxt,
	.get_addr = dwmac4_get_addr,
	.set_addr = dwmac4_set_addr,
	.clear = dwmac4_clear,
	.set_sarc = dwmac4_set_sarc,
	.set_vlan_tag = dwmac4_set_vlan_tag,
	.set_vlan = dwmac4_set_vlan,
	.get_rx_header_len = dwmac4_get_rx_header_len,
	.set_sec_addr = dwmac4_set_sec_addr,
	.set_tbs = dwmac4_set_tbs,
};

const struct stmmac_mode_ops dwmac4_ring_mode_ops = {
	.set_16kib_bfsize = set_16kib_bfsize,
};
