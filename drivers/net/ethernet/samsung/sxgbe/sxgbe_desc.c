/* 10G controller driver for Samsung SoCs
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Siva Reddy Kallam <siva.kallam@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/bitops.h>
#include <linux/export.h>
#include <linux/io.h>
#include <linux/netdevice.h>
#include <linux/phy.h>

#include "sxgbe_common.h"
#include "sxgbe_dma.h"
#include "sxgbe_desc.h"

/* DMA TX descriptor ring initialization */
static void sxgbe_init_tx_desc(struct sxgbe_tx_norm_desc *p)
{
	p->tdes23.tx_rd_des23.own_bit = 0;
}

static void sxgbe_tx_desc_enable_tse(struct sxgbe_tx_norm_desc *p, u8 is_tse,
				     u32 total_hdr_len, u32 tcp_hdr_len,
				     u32 tcp_payload_len)
{
	p->tdes23.tx_rd_des23.tse_bit = is_tse;
	p->tdes23.tx_rd_des23.buf1_size = total_hdr_len;
	p->tdes23.tx_rd_des23.tcp_hdr_len = tcp_hdr_len / 4;
	p->tdes23.tx_rd_des23.tx_pkt_len.tcp_payload_len  = tcp_payload_len;
}

/* Assign buffer lengths for descriptor */
static void sxgbe_prepare_tx_desc(struct sxgbe_tx_norm_desc *p, u8 is_fd,
				  int buf1_len, int pkt_len, int cksum)
{
	p->tdes23.tx_rd_des23.first_desc = is_fd;
	p->tdes23.tx_rd_des23.buf1_size = buf1_len;

	p->tdes23.tx_rd_des23.tx_pkt_len.pkt_len.total_pkt_len = pkt_len;

	if (cksum)
		p->tdes23.tx_rd_des23.cksum_ctl = cic_full;
}

/* Set VLAN control information */
static void sxgbe_tx_vlanctl_desc(struct sxgbe_tx_norm_desc *p, int vlan_ctl)
{
	p->tdes23.tx_rd_des23.vlan_tag_ctl = vlan_ctl;
}

/* Set the owner of Normal descriptor */
static void sxgbe_set_tx_owner(struct sxgbe_tx_norm_desc *p)
{
	p->tdes23.tx_rd_des23.own_bit = 1;
}

/* Get the owner of Normal descriptor */
static int sxgbe_get_tx_owner(struct sxgbe_tx_norm_desc *p)
{
	return p->tdes23.tx_rd_des23.own_bit;
}

/* Invoked by the xmit function to close the tx descriptor */
static void sxgbe_close_tx_desc(struct sxgbe_tx_norm_desc *p)
{
	p->tdes23.tx_rd_des23.last_desc = 1;
	p->tdes23.tx_rd_des23.int_on_com = 1;
}

/* Clean the tx descriptor as soon as the tx irq is received */
static void sxgbe_release_tx_desc(struct sxgbe_tx_norm_desc *p)
{
	memset(p, 0, sizeof(*p));
}

/* Clear interrupt on tx frame completion. When this bit is
 * set an interrupt happens as soon as the frame is transmitted
 */
static void sxgbe_clear_tx_ic(struct sxgbe_tx_norm_desc *p)
{
	p->tdes23.tx_rd_des23.int_on_com = 0;
}

/* Last tx segment reports the transmit status */
static int sxgbe_get_tx_ls(struct sxgbe_tx_norm_desc *p)
{
	return p->tdes23.tx_rd_des23.last_desc;
}

/* Get the buffer size from the descriptor */
static int sxgbe_get_tx_len(struct sxgbe_tx_norm_desc *p)
{
	return p->tdes23.tx_rd_des23.buf1_size;
}

/* Set tx timestamp enable bit */
static void sxgbe_tx_enable_tstamp(struct sxgbe_tx_norm_desc *p)
{
	p->tdes23.tx_rd_des23.timestmp_enable = 1;
}

/* get tx timestamp status */
static int sxgbe_get_tx_timestamp_status(struct sxgbe_tx_norm_desc *p)
{
	return p->tdes23.tx_rd_des23.timestmp_enable;
}

/* TX Context Descripto Specific */
static void sxgbe_tx_ctxt_desc_set_ctxt(struct sxgbe_tx_ctxt_desc *p)
{
	p->ctxt_bit = 1;
}

/* Set the owner of TX context descriptor */
static void sxgbe_tx_ctxt_desc_set_owner(struct sxgbe_tx_ctxt_desc *p)
{
	p->own_bit = 1;
}

/* Get the owner of TX context descriptor */
static int sxgbe_tx_ctxt_desc_get_owner(struct sxgbe_tx_ctxt_desc *p)
{
	return p->own_bit;
}

/* Set TX mss in TX context Descriptor */
static void sxgbe_tx_ctxt_desc_set_mss(struct sxgbe_tx_ctxt_desc *p, u16 mss)
{
	p->maxseg_size = mss;
}

/* Get TX mss from TX context Descriptor */
static int sxgbe_tx_ctxt_desc_get_mss(struct sxgbe_tx_ctxt_desc *p)
{
	return p->maxseg_size;
}

/* Set TX tcmssv in TX context Descriptor */
static void sxgbe_tx_ctxt_desc_set_tcmssv(struct sxgbe_tx_ctxt_desc *p)
{
	p->tcmssv = 1;
}

/* Reset TX ostc in TX context Descriptor */
static void sxgbe_tx_ctxt_desc_reset_ostc(struct sxgbe_tx_ctxt_desc *p)
{
	p->ostc = 0;
}

/* Set IVLAN information */
static void sxgbe_tx_ctxt_desc_set_ivlantag(struct sxgbe_tx_ctxt_desc *p,
					    int is_ivlanvalid, int ivlan_tag,
					    int ivlan_ctl)
{
	if (is_ivlanvalid) {
		p->ivlan_tag_valid = is_ivlanvalid;
		p->ivlan_tag = ivlan_tag;
		p->ivlan_tag_ctl = ivlan_ctl;
	}
}

/* Return IVLAN Tag */
static int sxgbe_tx_ctxt_desc_get_ivlantag(struct sxgbe_tx_ctxt_desc *p)
{
	return p->ivlan_tag;
}

/* Set VLAN Tag */
static void sxgbe_tx_ctxt_desc_set_vlantag(struct sxgbe_tx_ctxt_desc *p,
					   int is_vlanvalid, int vlan_tag)
{
	if (is_vlanvalid) {
		p->vltag_valid = is_vlanvalid;
		p->vlan_tag = vlan_tag;
	}
}

/* Return VLAN Tag */
static int sxgbe_tx_ctxt_desc_get_vlantag(struct sxgbe_tx_ctxt_desc *p)
{
	return p->vlan_tag;
}

/* Set Time stamp */
static void sxgbe_tx_ctxt_desc_set_tstamp(struct sxgbe_tx_ctxt_desc *p,
					  u8 ostc_enable, u64 tstamp)
{
	if (ostc_enable) {
		p->ostc = ostc_enable;
		p->tstamp_lo = (u32) tstamp;
		p->tstamp_hi = (u32) (tstamp>>32);
	}
}
/* Close TX context descriptor */
static void sxgbe_tx_ctxt_desc_close(struct sxgbe_tx_ctxt_desc *p)
{
	p->own_bit = 1;
}

/* WB status of context descriptor */
static int sxgbe_tx_ctxt_desc_get_cde(struct sxgbe_tx_ctxt_desc *p)
{
	return p->ctxt_desc_err;
}

/* DMA RX descriptor ring initialization */
static void sxgbe_init_rx_desc(struct sxgbe_rx_norm_desc *p, int disable_rx_ic,
			       int mode, int end)
{
	p->rdes23.rx_rd_des23.own_bit = 1;
	if (disable_rx_ic)
		p->rdes23.rx_rd_des23.int_on_com = disable_rx_ic;
}

/* Get RX own bit */
static int sxgbe_get_rx_owner(struct sxgbe_rx_norm_desc *p)
{
	return p->rdes23.rx_rd_des23.own_bit;
}

/* Set RX own bit */
static void sxgbe_set_rx_owner(struct sxgbe_rx_norm_desc *p)
{
	p->rdes23.rx_rd_des23.own_bit = 1;
}

/* Set Interrupt on completion bit */
static void sxgbe_set_rx_int_on_com(struct sxgbe_rx_norm_desc *p)
{
	p->rdes23.rx_rd_des23.int_on_com = 1;
}

/* Get the receive frame size */
static int sxgbe_get_rx_frame_len(struct sxgbe_rx_norm_desc *p)
{
	return p->rdes23.rx_wb_des23.pkt_len;
}

/* Return first Descriptor status */
static int sxgbe_get_rx_fd_status(struct sxgbe_rx_norm_desc *p)
{
	return p->rdes23.rx_wb_des23.first_desc;
}

/* Return Last Descriptor status */
static int sxgbe_get_rx_ld_status(struct sxgbe_rx_norm_desc *p)
{
	return p->rdes23.rx_wb_des23.last_desc;
}


/* Return the RX status looking at the WB fields */
static int sxgbe_rx_wbstatus(struct sxgbe_rx_norm_desc *p,
			     struct sxgbe_extra_stats *x, int *checksum)
{
	int status = 0;

	*checksum = CHECKSUM_UNNECESSARY;
	if (p->rdes23.rx_wb_des23.err_summary) {
		switch (p->rdes23.rx_wb_des23.err_l2_type) {
		case RX_GMII_ERR:
			status = -EINVAL;
			x->rx_code_gmii_err++;
			break;
		case RX_WATCHDOG_ERR:
			status = -EINVAL;
			x->rx_watchdog_err++;
			break;
		case RX_CRC_ERR:
			status = -EINVAL;
			x->rx_crc_err++;
			break;
		case RX_GAINT_ERR:
			status = -EINVAL;
			x->rx_gaint_pkt_err++;
			break;
		case RX_IP_HDR_ERR:
			*checksum = CHECKSUM_NONE;
			x->ip_hdr_err++;
			break;
		case RX_PAYLOAD_ERR:
			*checksum = CHECKSUM_NONE;
			x->ip_payload_err++;
			break;
		case RX_OVERFLOW_ERR:
			status = -EINVAL;
			x->overflow_error++;
			break;
		default:
			pr_err("Invalid Error type\n");
			break;
		}
	} else {
		switch (p->rdes23.rx_wb_des23.err_l2_type) {
		case RX_LEN_PKT:
			x->len_pkt++;
			break;
		case RX_MACCTL_PKT:
			x->mac_ctl_pkt++;
			break;
		case RX_DCBCTL_PKT:
			x->dcb_ctl_pkt++;
			break;
		case RX_ARP_PKT:
			x->arp_pkt++;
			break;
		case RX_OAM_PKT:
			x->oam_pkt++;
			break;
		case RX_UNTAG_PKT:
			x->untag_okt++;
			break;
		case RX_OTHER_PKT:
			x->other_pkt++;
			break;
		case RX_SVLAN_PKT:
			x->svlan_tag_pkt++;
			break;
		case RX_CVLAN_PKT:
			x->cvlan_tag_pkt++;
			break;
		case RX_DVLAN_OCVLAN_ICVLAN_PKT:
			x->dvlan_ocvlan_icvlan_pkt++;
			break;
		case RX_DVLAN_OSVLAN_ISVLAN_PKT:
			x->dvlan_osvlan_isvlan_pkt++;
			break;
		case RX_DVLAN_OSVLAN_ICVLAN_PKT:
			x->dvlan_osvlan_icvlan_pkt++;
			break;
		case RX_DVLAN_OCVLAN_ISVLAN_PKT:
			x->dvlan_ocvlan_icvlan_pkt++;
			break;
		default:
			pr_err("Invalid L2 Packet type\n");
			break;
		}
	}

	/* L3/L4 Pkt type */
	switch (p->rdes23.rx_wb_des23.layer34_pkt_type) {
	case RX_NOT_IP_PKT:
		x->not_ip_pkt++;
		break;
	case RX_IPV4_TCP_PKT:
		x->ip4_tcp_pkt++;
		break;
	case RX_IPV4_UDP_PKT:
		x->ip4_udp_pkt++;
		break;
	case RX_IPV4_ICMP_PKT:
		x->ip4_icmp_pkt++;
		break;
	case RX_IPV4_UNKNOWN_PKT:
		x->ip4_unknown_pkt++;
		break;
	case RX_IPV6_TCP_PKT:
		x->ip6_tcp_pkt++;
		break;
	case RX_IPV6_UDP_PKT:
		x->ip6_udp_pkt++;
		break;
	case RX_IPV6_ICMP_PKT:
		x->ip6_icmp_pkt++;
		break;
	case RX_IPV6_UNKNOWN_PKT:
		x->ip6_unknown_pkt++;
		break;
	default:
		pr_err("Invalid L3/L4 Packet type\n");
		break;
	}

	/* Filter */
	if (p->rdes23.rx_wb_des23.vlan_filter_match)
		x->vlan_filter_match++;

	if (p->rdes23.rx_wb_des23.sa_filter_fail) {
		status = -EINVAL;
		x->sa_filter_fail++;
	}
	if (p->rdes23.rx_wb_des23.da_filter_fail) {
		status = -EINVAL;
		x->da_filter_fail++;
	}
	if (p->rdes23.rx_wb_des23.hash_filter_pass)
		x->hash_filter_pass++;

	if (p->rdes23.rx_wb_des23.l3_filter_match)
		x->l3_filter_match++;

	if (p->rdes23.rx_wb_des23.l4_filter_match)
		x->l4_filter_match++;

	return status;
}

/* Get own bit of context descriptor */
static int sxgbe_get_rx_ctxt_owner(struct sxgbe_rx_ctxt_desc *p)
{
	return p->own_bit;
}

/* Set own bit for context descriptor */
static void sxgbe_set_ctxt_rx_owner(struct sxgbe_rx_ctxt_desc *p)
{
	p->own_bit = 1;
}


/* Return the reception status looking at Context control information */
static void sxgbe_rx_ctxt_wbstatus(struct sxgbe_rx_ctxt_desc *p,
				   struct sxgbe_extra_stats *x)
{
	if (p->tstamp_dropped)
		x->timestamp_dropped++;

	/* ptp */
	if (p->ptp_msgtype == RX_NO_PTP)
		x->rx_msg_type_no_ptp++;
	else if (p->ptp_msgtype == RX_PTP_SYNC)
		x->rx_ptp_type_sync++;
	else if (p->ptp_msgtype == RX_PTP_FOLLOW_UP)
		x->rx_ptp_type_follow_up++;
	else if (p->ptp_msgtype == RX_PTP_DELAY_REQ)
		x->rx_ptp_type_delay_req++;
	else if (p->ptp_msgtype == RX_PTP_DELAY_RESP)
		x->rx_ptp_type_delay_resp++;
	else if (p->ptp_msgtype == RX_PTP_PDELAY_REQ)
		x->rx_ptp_type_pdelay_req++;
	else if (p->ptp_msgtype == RX_PTP_PDELAY_RESP)
		x->rx_ptp_type_pdelay_resp++;
	else if (p->ptp_msgtype == RX_PTP_PDELAY_FOLLOW_UP)
		x->rx_ptp_type_pdelay_follow_up++;
	else if (p->ptp_msgtype == RX_PTP_ANNOUNCE)
		x->rx_ptp_announce++;
	else if (p->ptp_msgtype == RX_PTP_MGMT)
		x->rx_ptp_mgmt++;
	else if (p->ptp_msgtype == RX_PTP_SIGNAL)
		x->rx_ptp_signal++;
	else if (p->ptp_msgtype == RX_PTP_RESV_MSG)
		x->rx_ptp_resv_msg_type++;
}

/* Get rx timestamp status */
static int sxgbe_get_rx_ctxt_tstamp_status(struct sxgbe_rx_ctxt_desc *p)
{
	if ((p->tstamp_hi == 0xffffffff) && (p->tstamp_lo == 0xffffffff)) {
		pr_err("Time stamp corrupted\n");
		return 0;
	}

	return p->tstamp_available;
}


static u64 sxgbe_get_rx_timestamp(struct sxgbe_rx_ctxt_desc *p)
{
	u64 ns;

	ns = p->tstamp_lo;
	ns |= ((u64)p->tstamp_hi) << 32;

	return ns;
}

static const struct sxgbe_desc_ops desc_ops = {
	.init_tx_desc			= sxgbe_init_tx_desc,
	.tx_desc_enable_tse		= sxgbe_tx_desc_enable_tse,
	.prepare_tx_desc		= sxgbe_prepare_tx_desc,
	.tx_vlanctl_desc		= sxgbe_tx_vlanctl_desc,
	.set_tx_owner			= sxgbe_set_tx_owner,
	.get_tx_owner			= sxgbe_get_tx_owner,
	.close_tx_desc			= sxgbe_close_tx_desc,
	.release_tx_desc		= sxgbe_release_tx_desc,
	.clear_tx_ic			= sxgbe_clear_tx_ic,
	.get_tx_ls			= sxgbe_get_tx_ls,
	.get_tx_len			= sxgbe_get_tx_len,
	.tx_enable_tstamp		= sxgbe_tx_enable_tstamp,
	.get_tx_timestamp_status	= sxgbe_get_tx_timestamp_status,
	.tx_ctxt_desc_set_ctxt		= sxgbe_tx_ctxt_desc_set_ctxt,
	.tx_ctxt_desc_set_owner		= sxgbe_tx_ctxt_desc_set_owner,
	.get_tx_ctxt_owner		= sxgbe_tx_ctxt_desc_get_owner,
	.tx_ctxt_desc_set_mss		= sxgbe_tx_ctxt_desc_set_mss,
	.tx_ctxt_desc_get_mss		= sxgbe_tx_ctxt_desc_get_mss,
	.tx_ctxt_desc_set_tcmssv	= sxgbe_tx_ctxt_desc_set_tcmssv,
	.tx_ctxt_desc_reset_ostc	= sxgbe_tx_ctxt_desc_reset_ostc,
	.tx_ctxt_desc_set_ivlantag	= sxgbe_tx_ctxt_desc_set_ivlantag,
	.tx_ctxt_desc_get_ivlantag	= sxgbe_tx_ctxt_desc_get_ivlantag,
	.tx_ctxt_desc_set_vlantag	= sxgbe_tx_ctxt_desc_set_vlantag,
	.tx_ctxt_desc_get_vlantag	= sxgbe_tx_ctxt_desc_get_vlantag,
	.tx_ctxt_set_tstamp		= sxgbe_tx_ctxt_desc_set_tstamp,
	.close_tx_ctxt_desc		= sxgbe_tx_ctxt_desc_close,
	.get_tx_ctxt_cde		= sxgbe_tx_ctxt_desc_get_cde,
	.init_rx_desc			= sxgbe_init_rx_desc,
	.get_rx_owner			= sxgbe_get_rx_owner,
	.set_rx_owner			= sxgbe_set_rx_owner,
	.set_rx_int_on_com		= sxgbe_set_rx_int_on_com,
	.get_rx_frame_len		= sxgbe_get_rx_frame_len,
	.get_rx_fd_status		= sxgbe_get_rx_fd_status,
	.get_rx_ld_status		= sxgbe_get_rx_ld_status,
	.rx_wbstatus			= sxgbe_rx_wbstatus,
	.get_rx_ctxt_owner		= sxgbe_get_rx_ctxt_owner,
	.set_rx_ctxt_owner		= sxgbe_set_ctxt_rx_owner,
	.rx_ctxt_wbstatus		= sxgbe_rx_ctxt_wbstatus,
	.get_rx_ctxt_tstamp_status	= sxgbe_get_rx_ctxt_tstamp_status,
	.get_timestamp			= sxgbe_get_rx_timestamp,
};

const struct sxgbe_desc_ops *sxgbe_get_desc_ops(void)
{
	return &desc_ops;
}
