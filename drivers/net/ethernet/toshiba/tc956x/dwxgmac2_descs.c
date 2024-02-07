/*
 * TC956X ethernet driver.
 *
 * dwxgmac2_descs.c
 *
 * Copyright (C) 2018 Synopsys, Inc. and/or its affiliates.
 * Copyright (C) 2021 Toshiba Electronic Devices & Storage Corporation
 *
 * This file has been derived from the STMicro and Synopsys Linux driver,
 * and developed or modified for TC956X.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/*! History:
 *  20 Jan 2021 : Initial Version
 *  VERSION     : 00-01
 *
 *  15 Mar 2021 : Base lined
 *  VERSION     : 01-00
 *  15 Jul 2021 : 1. USXGMII/XFI/SGMII/RGMII interface supported without module parameter
 *  VERSION     : 01-00-02
 *  20 Jul 2021 : 1. Debug prints removed
 *  VERSION     : 01-00-03
 */

#include "tc956xmac_inc.h"
#include "tc956xmac.h"
#include "common.h"
#include "dwxgmac2.h"

static int dwxgmac2_get_tx_status(struct tc956xmac_priv *priv, void *data,
					struct tc956xmac_extra_stats *x,
					struct dma_desc *p, void __iomem *ioaddr)
{
	unsigned int tdes3 = le32_to_cpu(p->des3);
	int ret = tx_done;

	if (unlikely(tdes3 & XGMAC_TDES3_OWN))
		return tx_dma_own;
	if (likely(!(tdes3 & XGMAC_TDES3_LD)))
		return tx_not_ls;
#ifdef TC956X_SRIOV_VF
	priv->sw_stats.tx_frame_count_good_bad++;
#endif
	return ret;
}

static int dwxgmac2_get_rx_status(struct tc956xmac_priv *priv, void *data,
					struct tc956xmac_extra_stats *x,
					struct dma_desc *p)
{
	unsigned int rdes3 = le32_to_cpu(p->des3);
#ifdef TC956X_SRIOV_VF
	u32 rdes2 = le32_to_cpu(p->des2), l34t_type = 0;
	u32 error_summary = 0, etlt = 0, tnp = 0;
#endif

	if (unlikely(rdes3 & XGMAC_RDES3_OWN))
		return dma_own;
	if (unlikely(rdes3 & XGMAC_RDES3_CTXT))
		return discard_frame;
	if (likely(!(rdes3 & XGMAC_RDES3_LD)))
		return rx_not_ls;
	if (unlikely((rdes3 & XGMAC_RDES3_ES) && (rdes3 & XGMAC_RDES3_LD)))
		return discard_frame;

#ifdef TC956X_SRIOV_VF
	l34t_type = (rdes3 & XGMAC_RDES3_L34T) >> XGMAC_RDES3_L34T_SHIFT;
	error_summary = (rdes3 & XGMAC_RDES3_ES) >> XGMAC_RDES3_ES_SHIFT;
	etlt = (rdes3 & XGMAC_RDES3_ETLT) >> XGMAC_RDES3_ETLT_SHIFT;
	tnp = (rdes2 & XGMAC_RDES2_TNP) >> XGMAC_RDES2_TNP_SHIFT;


	if (!(rdes3 & XGMAC_RDES3_OWN)) {
		priv->sw_stats.rx_frame_count_good_bad++;
		if (l34t_type == XGMAC_L34T_NON_IP) {
			/* Non IP Packet */
			priv->sw_stats.rx_non_ip_pkt_count++;
			if (rdes2 & XGMAC_RDES2_AVTDP)
				priv->sw_stats.rx_av_tagged_datapacket_count++;
			else if (rdes2 & XGMAC_RDES2_AVTCP)
				priv->sw_stats.rx_av_tagged_controlpacket_count++;
		} else {
			/* IP Packet */
			priv->sw_stats.rx_nonav_packet_count++;
			priv->sw_stats.rx_header_good_octets +=
				le32_to_cpu(p->des2) & XGMAC_RDES2_HL;
			if (l34t_type == XGMAC_L34T_IPV4_TCP)
				priv->sw_stats.rx_ipv4_tcp_pkt_count++;
			if (l34t_type == XGMAC_L34T_IPV4_UDP)
				priv->sw_stats.rx_ipv4_udp_pkt_count++;
			if (l34t_type == XGMAC_L34T_IPV4_ICMP)
				priv->sw_stats.rx_ipv4_icmp_pkt_count++;
			if (l34t_type == XGMAC_L34T_IPV4_IGMP)
				priv->sw_stats.rx_ipv4_igmp_pkt_count++;
			if (l34t_type == XGMAC_L34T_IPV4_UNKNOWN)
				priv->sw_stats.rx_ipv4_unkown_pkt_count++;
			if (l34t_type == XGMAC_L34T_IPV6_TCP)
				priv->sw_stats.rx_ipv6_tcp_pkt_count++;
			if (l34t_type == XGMAC_L34T_IPV6_UDP)
				priv->sw_stats.rx_ipv6_udp_pkt_count++;
			if (l34t_type == XGMAC_L34T_IPV6_ICMP)
				priv->sw_stats.rx_ipv6_icmp_pkt_count++;
			if (l34t_type == XGMAC_L34T_IPV6_UNKNOWN)
				priv->sw_stats.rx_ipv6_unkown_pkt_count++;
		}
		if (error_summary) {
			priv->sw_stats.rx_fame_count_bad++;
			if (etlt == XGMAC_ET_WD_TIMEOUT)
				priv->sw_stats.rx_err_wd_timeout_count++;
			if (etlt == XGMAC_ET_INV_GMII)
				priv->sw_stats.rx_err_gmii_inv_count++;
			if (etlt == XGMAC_ET_CRC)
				priv->sw_stats.rx_err_crc_count++;
			if (etlt == XGMAC_ET_GIANT_PKT)
				priv->sw_stats.rx_err_giant_count++;
			if (etlt == XGMAC_ET_IP_HEADER)
				priv->sw_stats.rx_err_giant_count++;
			if (etlt == XGMAC_ET_L4_CSUM)
				priv->sw_stats.rx_err_checksum_count++;
			if (etlt == XGMAC_ET_OVERFLOW)
				priv->sw_stats.rx_err_overflow_count++;
			if (etlt == XGMAC_ET_BUS)
				priv->sw_stats.rx_err_bus_count++;
			if (etlt == XGMAC_ET_LENGTH)
				priv->sw_stats.rx_err_pkt_len_count++;
			if (etlt == XGMAC_ET_GOOD_RUNT)
				priv->sw_stats.rx_err_runt_pkt_count++;
			if (etlt == XGMAC_ET_DRIBBLE)
				priv->sw_stats.rx_err_dribble_count++;
			if (tnp) {
				priv->sw_stats.rx_tunnel_packet_count++;
				if (etlt == XGMAC_ET_T_OUTER_IP_HEADER)
					priv->sw_stats.rx_err_t_out_ip_header_count++;
				if (etlt == XGMAC_ET_T_OUTER_HEADER_PAYLOAD_L4_CSUM)
					priv->sw_stats.rx_err_t_out_ip_pl_l4_csum_count++;
				if (etlt == XGMAC_ET_T_INNER_IP_HEADER)
					priv->sw_stats.rx_err_t_in_ip_header_count++;
				if (etlt == XGMAC_ET_T_INNER_L4_PAYLOAD)
					priv->sw_stats.rx_err_t_in_ip_pl_l4_csum_count++;
				if (etlt == XGMAC_ET_T_INV_VXLAN_HEADER)
					priv->sw_stats.rx_err_t_invalid_vlan_header++;
			}
		} else {
			priv->sw_stats.rx_frame_count_good++;
			priv->sw_stats.rx_packet_good_octets +=
						(rdes3 & XGMAC_RDES3_PL);
			priv->sw_stats.rx_header_good_octets +=
				le32_to_cpu(p->des2) & XGMAC_RDES2_HL;
			if (etlt == XGMAC_LT_LENGTH)
				priv->sw_stats.rx_l2_len_pkt_count++;
			if (etlt == XGMAC_LT_MAC_CONTROL)
				priv->sw_stats.rx_l2_mac_control_pkt_count++;
			if (etlt == XGMAC_LT_DCB_CONTROL)
				priv->sw_stats.rx_l2_dcb_control_pkt_count++;
			if (etlt == XGMAC_LT_ARP_REQ)
				priv->sw_stats.rx_l2_arp_pkt_count++;
			if (etlt == XGMAC_LT_OAM)
				priv->sw_stats.rx_l2_oam_type_pkt_count++;
			if (etlt == XGMAC_LT_MAC_RX_ETH_TYPE_MATCH)
				priv->sw_stats.rx_l2_untg_typ_match_pkt_count++;
			if (etlt == XGMAC_LT_OTH_TYPE)
				priv->sw_stats.rx_l2_other_type_pkt_count++;
			if (etlt == XGMAC_LT_SVLAN)
				priv->sw_stats.rx_l2_single_svlan_pkt_count++;
			if (etlt == XGMAC_LT_CVLAN)
				priv->sw_stats.rx_l2_single_cvlan_pkt_count++;
			if (etlt == XGMAC_LT_D_CVLAN_CVLAN)
				priv->sw_stats.rx_l2_d_cvlan_cvlan_pkt_count++;
			if (etlt == XGMAC_LT_D_SVLAN_SVLAN)
				priv->sw_stats.rx_l2_d_svlan_svlan_pkt_count++;
			if (etlt == XGMAC_LT_D_SVLAN_CVLAN)
				priv->sw_stats.rx_l2_d_svlan_cvlan_pkt_count++;
			if (etlt == XGMAC_LT_D_CVLAN_SVLAN)
				priv->sw_stats.rx_l2_d_cvlan_svlan_pkt_count++;
			if (etlt == XGMAC_LT_UNTAG_AV_CONTROL)
				priv->sw_stats.rx_l2_untg_av_control_pkt_count++;
		}
	}
#endif /* #ifdef TC956X_SRIOV_VF */
	return good_frame;
}

#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
static int dwxgmac2_get_tx_len(struct tc956xmac_priv *priv, struct dma_desc *p)
{
	return (le32_to_cpu(p->des2) & XGMAC_TDES2_B1L);
}

static int dwxgmac2_get_tx_owner(struct tc956xmac_priv *priv, struct dma_desc *p)
{
	return (le32_to_cpu(p->des3) & XGMAC_TDES3_OWN) > 0;
}
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */

static void dwxgmac2_set_tx_owner(struct tc956xmac_priv *priv, struct dma_desc *p)
{
	p->des3 |= cpu_to_le32(XGMAC_TDES3_OWN);
}

static void dwxgmac2_set_rx_owner(struct tc956xmac_priv *priv,
					struct dma_desc *p, int disable_rx_ic)
{
	p->des3 |= cpu_to_le32(XGMAC_RDES3_OWN);

	if (!disable_rx_ic)
		p->des3 |= cpu_to_le32(XGMAC_RDES3_IOC);
}
#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
static int dwxgmac2_get_tx_ls(struct tc956xmac_priv *priv, struct dma_desc *p)
{
	return (le32_to_cpu(p->des3) & XGMAC_RDES3_LD) > 0;
}
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */

static int dwxgmac2_get_rx_frame_len(struct tc956xmac_priv *priv,
					      struct dma_desc *p, int rx_coe)
{
	return (le32_to_cpu(p->des3) & XGMAC_RDES3_PL);
}

static void dwxgmac2_enable_tx_timestamp(struct tc956xmac_priv *priv,
						   struct dma_desc *p)
{
	p->des2 |= cpu_to_le32(XGMAC_TDES2_TTSE);
}

static int dwxgmac2_get_tx_timestamp_status(struct tc956xmac_priv *priv,
							struct dma_desc *p)
{
	return 0; /* Not supported */
}

static inline void dwxgmac2_get_timestamp(struct tc956xmac_priv *priv,
						   void *desc, u32 ats, u64 *ts)
{
	struct dma_desc *p = (struct dma_desc *)desc;
	u64 ns = 0;

	ns += le32_to_cpu(p->des1) * 1000000000ULL;
	ns += le32_to_cpu(p->des0);

	*ts = ns;

	netdev_dbg(priv->dev, "%s: timestamp in ns = 0x%llx", __func__, ns);
}

static int dwxgmac2_rx_check_timestamp(struct tc956xmac_priv *priv, void *desc)
{
	struct dma_desc *p = (struct dma_desc *)desc;
	unsigned int rdes3 = le32_to_cpu(p->des3);
	bool desc_valid, ts_valid;

	dma_rmb();

	desc_valid = !(rdes3 & XGMAC_RDES3_OWN) && (rdes3 & XGMAC_RDES3_CTXT);
	ts_valid = !(rdes3 & XGMAC_RDES3_TSD) && (rdes3 & XGMAC_RDES3_TSA);

	if (likely(desc_valid && ts_valid)) {
		if ((le32_to_cpu(p->des0) == 0xffffffff) && (le32_to_cpu(p->des1) == 0xffffffff))
			return -EINVAL;

		netdev_dbg(priv->dev, "%s: Rx timestamp Low = 0x%x", __func__,
				le32_to_cpu(p->des0));

		netdev_dbg(priv->dev, "%s: Rx timestamp High = 0x%x", __func__,
				le32_to_cpu(p->des1));
		return 0;
	}

	/* Timestamp not ready */
	return 1;
}

static int dwxgmac2_get_rx_timestamp_status(struct tc956xmac_priv *priv,
						void *desc, void *next_desc,
						u32 ats)
{
	struct dma_desc *p = (struct dma_desc *)desc;
	unsigned int rdes3 = le32_to_cpu(p->des3);
	int ret = -EBUSY;
	unsigned int i = 0;
#ifdef TC956X_SRIOV_VF
	u32 message_type;
#endif
	if (likely(rdes3 & XGMAC_RDES3_CDA)) {
		do {
			ret = dwxgmac2_rx_check_timestamp(priv, next_desc);
			if (ret < 0)
				goto exit;

			i++;

			if (ret == 1)
				udelay(1);

		} while ((ret == 1) && (i < 25));

	if (i == 25)
		ret = -EBUSY;
	}

#ifdef TC956X_SRIOV_VF
	/* Read frame type to SW MMC counters from PMT bits in RDES3 */
	if ((rdes3 & XGMAC_RDES3_OWN) && (rdes3 & XGMAC_RDES3_CTXT)) {

		message_type = (rdes3 &  XGMAC_RDES3_PMT);
		if (message_type == RDES_PMT_NO_PTP)
			priv->sw_stats.rx_ptp_no_msg++;
		else if (message_type == RDES_PMT_SYNC)
			priv->sw_stats.rx_ptp_msg_type_sync++;
		else if (message_type == RDES_PMT_FOLLOW_UP)
			priv->sw_stats.rx_ptp_msg_type_follow_up++;
		else if (message_type == RDES_PMT_DELAY_REQ)
			priv->sw_stats.rx_ptp_msg_type_delay_req++;
		else if (message_type == RDES_PMT_DELAY_RESP)
			priv->sw_stats.rx_ptp_msg_type_delay_resp++;
		else if (message_type == RDES_PMT_PDELAY_REQ)
			priv->sw_stats.rx_ptp_msg_type_pdelay_req++;
		else if (message_type == RDES_PMT_PDELAY_RESP)
			priv->sw_stats.rx_ptp_msg_type_pdelay_resp++;
		else if (message_type == RDES_PMT_PDELAY_FOLLOW_UP)
			priv->sw_stats.rx_ptp_msg_type_pdelay_follow_up++;
		else if (message_type == RDES_PMT_PTP_ANNOUNCE)
			priv->sw_stats.rx_ptp_msg_type_announce++;
		else if (message_type == RDES_PMT_PTP_MANAGEMENT)
			priv->sw_stats.rx_ptp_msg_type_management++;
		else if (message_type == RDES_PMT_PTP_SIGNALING)
			priv->sw_stats.rx_ptp_msg_pkt_signaling++;
		else if (message_type == RDES_PMT_PTP_PKT_RESERVED_TYPE)
			priv->sw_stats.rx_ptp_msg_pkt_reserved_type++;
	}
#endif
exit:
	return !ret;
}

static void dwxgmac2_init_rx_desc(struct tc956xmac_priv *priv,
					struct dma_desc *p, int disable_rx_ic,
					int mode, int end, int bfsize)
{
	dwxgmac2_set_rx_owner(priv, p, disable_rx_ic);
}

static void dwxgmac2_init_tx_desc(struct tc956xmac_priv *priv,
					struct dma_desc *p, int mode, int end)
{
	p->des0 = 0;
	p->des1 = 0;
	p->des2 = 0;
	p->des3 = 0;
}

/**
 * dwxgmac2_prepare_tx_desc - Fill descriptors based on the provided input
 * parameters.
 *
 * @p: pointer to dma_desc descriptor
 * @is_fs: 1 - first descriptor, 0 - not first descriptor
 * @len: buffer length
 * @csum_flag: checksum flag  1 - Enable HW checksum computaion, 0 - disable
 * @crc_pad: CRC padding configuration
 * @mode: descriptor mode
 * @tx_own: descriptor own bit setting
 * @ls: 1 - last descriptor, 0 - not last descriptor
 * @tot_pkt_len: total packet length
 */
static void dwxgmac2_prepare_tx_desc(struct tc956xmac_priv *priv,
				     struct dma_desc *p, int is_fs, int len,
				     bool csum_flag, u32 crc_pad, int mode,
				     bool tx_own, bool ls,
				     unsigned int tot_pkt_len)
{
	unsigned int tdes3 = le32_to_cpu(p->des3);

	p->des2 |= cpu_to_le32(len & XGMAC_TDES2_B1L);

	tdes3 |= tot_pkt_len & XGMAC_TDES3_FL;
	if (is_fs)
		tdes3 |= XGMAC_TDES3_FD;
	else
		tdes3 &= ~XGMAC_TDES3_FD;

	if (csum_flag)
		tdes3 |= 0x3 << XGMAC_TDES3_CIC_SHIFT;
	else
		tdes3 &= ~XGMAC_TDES3_CIC;

	if (crc_pad)
		tdes3 |= crc_pad << XGMAC_TDES3_CPC_SHIFT;
	else
		tdes3 &= ~XGMAC_TDES3_CPC;

	if (ls)
		tdes3 |= XGMAC_TDES3_LD;
	else
		tdes3 &= ~XGMAC_TDES3_LD;

	/* Finally set the OWN bit. Later the DMA will start! */
	if (tx_own)
		tdes3 |= XGMAC_TDES3_OWN;

	if (is_fs && tx_own)
		/* When the own bit, for the first frame, has to be set, all
		 * descriptors for the same frame has to be set before, to
		 * avoid race condition.
		 */
		dma_wmb();

	p->des3 = cpu_to_le32(tdes3);
}

static void dwxgmac2_prepare_tso_tx_desc(struct tc956xmac_priv *priv,
					 struct dma_desc *p, int is_fs,
					 int len1, int len2, bool tx_own,
					 bool ls, unsigned int tcphdrlen,
					 unsigned int tcppayloadlen)
{
	unsigned int tdes3 = le32_to_cpu(p->des3);

	if (len1)
		p->des2 |= cpu_to_le32(len1 & XGMAC_TDES2_B1L);
	if (len2)
		p->des2 |= cpu_to_le32((len2 << XGMAC_TDES2_B2L_SHIFT) &
				XGMAC_TDES2_B2L);
	if (is_fs) {
		tdes3 |= XGMAC_TDES3_FD | XGMAC_TDES3_TSE;
		tdes3 |= (tcphdrlen << XGMAC_TDES3_THL_SHIFT) &
			XGMAC_TDES3_THL;
		tdes3 |= tcppayloadlen & XGMAC_TDES3_TPL;
	} else {
		tdes3 &= ~XGMAC_TDES3_FD;
	}

	if (ls)
		tdes3 |= XGMAC_TDES3_LD;
	else
		tdes3 &= ~XGMAC_TDES3_LD;

	/* Finally set the OWN bit. Later the DMA will start! */
	if (tx_own)
		tdes3 |= XGMAC_TDES3_OWN;

	if (is_fs && tx_own)
		/* When the own bit, for the first frame, has to be set, all
		 * descriptors for the same frame has to be set before, to
		 * avoid race condition.
		 */
		dma_wmb();

	p->des3 = cpu_to_le32(tdes3);
}

static void dwxgmac2_release_tx_desc(struct tc956xmac_priv *priv,
					      struct dma_desc *p, int mode)
{
	p->des0 = 0;
	p->des1 = 0;
	p->des2 = 0;
	p->des3 = 0;
}

static void dwxgmac2_set_tx_ic(struct tc956xmac_priv *priv, struct dma_desc *p)
{
	p->des2 |= cpu_to_le32(XGMAC_TDES2_IOC);
}

static void dwxgmac2_set_mss(struct tc956xmac_priv *priv,
					struct dma_desc *p, unsigned int mss)
{
	p->des0 = 0;
	p->des1 = 0;
	p->des2 = cpu_to_le32(mss);
	p->des3 = cpu_to_le32(XGMAC_TDES3_CTXT | XGMAC_TDES3_TCMSSV);
}

#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
static void dwxgmac2_get_addr(struct tc956xmac_priv *priv,
				struct dma_desc *p, unsigned int *addr)
{
	*addr = le32_to_cpu(p->des0);
}
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */

static void dwxgmac2_set_addr(struct tc956xmac_priv *priv,
				struct dma_desc *p, dma_addr_t addr)
{
	p->des0 = cpu_to_le32(lower_32_bits(addr));
#ifdef TC956X
	/* Set the mask for physical address access  */
	p->des1 = cpu_to_le32(TC956X_HOST_PHYSICAL_ADRS_MASK |
		  (upper_32_bits(addr) & 0xF));
#else
	p->des1 = cpu_to_le32(upper_32_bits(addr));
#endif

}

static void dwxgmac2_clear(struct tc956xmac_priv *priv, struct dma_desc *p)
{
	p->des0 = 0;
	p->des1 = 0;
	p->des2 = 0;
	p->des3 = 0;
}

static int dwxgmac2_get_rx_hash(struct tc956xmac_priv *priv,
				struct dma_desc *p, u32 *hash,
				enum pkt_hash_types *type)
{
	unsigned int rdes3 = le32_to_cpu(p->des3);
	u32 ptype;

	if (rdes3 & XGMAC_RDES3_RSV) {
		ptype = (rdes3 & XGMAC_RDES3_L34T) >> XGMAC_RDES3_L34T_SHIFT;

		switch (ptype) {
		case XGMAC_L34T_IP4TCP:
		case XGMAC_L34T_IP4UDP:
		case XGMAC_L34T_IP6TCP:
		case XGMAC_L34T_IP6UDP:
			*type = PKT_HASH_TYPE_L4;
			break;
		default:
			*type = PKT_HASH_TYPE_L3;
			break;
		}

		*hash = le32_to_cpu(p->des1);
		return 0;
	}

	return -EINVAL;
}

static int dwxgmac2_get_rx_header_len(struct tc956xmac_priv *priv,
						struct dma_desc *p, unsigned int *len)
{
	if (le32_to_cpu(p->des3) & XGMAC_RDES3_L34T)
		*len = le32_to_cpu(p->des2) & XGMAC_RDES2_HL;
	return 0;
}

static void dwxgmac2_set_sec_addr(struct tc956xmac_priv *priv,
					struct dma_desc *p, dma_addr_t addr)
{
	p->des2 = cpu_to_le32(lower_32_bits(addr));
	p->des3 = cpu_to_le32(upper_32_bits(addr));
}

static void dwxgmac2_set_sarc(struct tc956xmac_priv *priv,
					struct dma_desc *p, u32 sarc_type)
{
	sarc_type <<= XGMAC_TDES3_SAIC_SHIFT;

	p->des3 |= cpu_to_le32(sarc_type & XGMAC_TDES3_SAIC);
}

static void dwxgmac2_set_vlan_tag(struct tc956xmac_priv *priv,
					struct dma_desc *p, u16 tag, u16 inner_tag,
					u32 inner_type)
{
	p->des0 = 0;
	p->des1 = 0;
	p->des2 = 0;
	p->des3 = 0;

	/* Inner VLAN */
	if (inner_type) {
		u32 des = inner_tag << XGMAC_TDES2_IVT_SHIFT;

		des &= XGMAC_TDES2_IVT;
		p->des2 = cpu_to_le32(des);

		des = inner_type << XGMAC_TDES3_IVTIR_SHIFT;
		des &= XGMAC_TDES3_IVTIR;
		p->des3 = cpu_to_le32(des | XGMAC_TDES3_IVLTV);
	}

	/* Outer VLAN */
	p->des3 |= cpu_to_le32(tag & XGMAC_TDES3_VT);
	p->des3 |= cpu_to_le32(XGMAC_TDES3_VLTV);

	p->des3 |= cpu_to_le32(XGMAC_TDES3_CTXT);
}

static void dwxgmac2_set_vlan(struct tc956xmac_priv *priv, struct dma_desc *p,
					u32 type)
{
	type <<= XGMAC_TDES2_VTIR_SHIFT;
	p->des2 |= cpu_to_le32(type & XGMAC_TDES2_VTIR);
}

static void dwxgmac2_set_tbs(struct tc956xmac_priv *priv, struct dma_edesc *p,
				u32 sec, u32 nsec, bool lt_valid)
{
	p->des4 = 0;
	p->des5 = 0;
	if (lt_valid) {
		p->des4 = cpu_to_le32((sec & XGMAC_TDES0_LT) | XGMAC_TDES0_LTV);
		p->des5 = cpu_to_le32(nsec & XGMAC_TDES1_LT);
	}
	p->des6 = 0;
	p->des7 = 0;
}

static void dwxgmac2_set_ostc(struct tc956xmac_priv *priv,
				struct dma_desc *p, u32 ttsh, u32 ttsl)
{
	p->des2 = 0;

	/* Set the timestamp for the DMA to use for doing one-step correction */
	p->des0 = cpu_to_le32(ttsl);
	p->des1 = cpu_to_le32(ttsh);

	/* One-Step Timestamp Correction Enable */
	p->des3 |= cpu_to_le32(XGMAC_TDES3_OSTC | XGMAC_TDES3_TCMSSV);

	/* Set Context Type for Context descriptor */
	p->des3 |= cpu_to_le32(XGMAC_TDES3_CTXT);
}

const struct tc956xmac_desc_ops dwxgmac210_desc_ops = {
	.tx_status = dwxgmac2_get_tx_status,
	.rx_status = dwxgmac2_get_rx_status,
#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
	.get_tx_len = dwxgmac2_get_tx_len,
	.get_tx_owner = dwxgmac2_get_tx_owner,
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */
	.set_tx_owner = dwxgmac2_set_tx_owner,
	.set_rx_owner = dwxgmac2_set_rx_owner,
#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
	.get_tx_ls = dwxgmac2_get_tx_ls,
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */
	.get_rx_frame_len = dwxgmac2_get_rx_frame_len,
	.enable_tx_timestamp = dwxgmac2_enable_tx_timestamp,
	.get_tx_timestamp_status = dwxgmac2_get_tx_timestamp_status,
	.get_rx_timestamp_status = dwxgmac2_get_rx_timestamp_status,
	.get_timestamp = dwxgmac2_get_timestamp,
	.set_tx_ic = dwxgmac2_set_tx_ic,
	.prepare_tx_desc = dwxgmac2_prepare_tx_desc,
	.prepare_tso_tx_desc = dwxgmac2_prepare_tso_tx_desc,
	.release_tx_desc = dwxgmac2_release_tx_desc,
	.init_rx_desc = dwxgmac2_init_rx_desc,
	.init_tx_desc = dwxgmac2_init_tx_desc,
	.set_mss = dwxgmac2_set_mss,
#ifdef TC956X_UNSUPPORTED_UNTESTED_FEATURE
	.get_addr = dwxgmac2_get_addr,
#endif /* TC956X_UNSUPPORTED_UNTESTED_FEATURE */
	.set_addr = dwxgmac2_set_addr,
	.clear = dwxgmac2_clear,
	.get_rx_hash = dwxgmac2_get_rx_hash,
	.get_rx_header_len = dwxgmac2_get_rx_header_len,
	.set_sec_addr = dwxgmac2_set_sec_addr,
	.set_sarc = dwxgmac2_set_sarc,
	.set_vlan_tag = dwxgmac2_set_vlan_tag,
	.set_vlan = dwxgmac2_set_vlan,
	.set_tbs = dwxgmac2_set_tbs,
	.set_ostc = dwxgmac2_set_ostc,
};
