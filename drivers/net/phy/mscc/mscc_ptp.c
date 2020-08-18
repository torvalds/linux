// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * Driver for Microsemi VSC85xx PHYs - timestamping and PHC support
 *
 * Authors: Quentin Schulz & Antoine Tenart
 * License: Dual MIT/GPL
 * Copyright (c) 2020 Microsemi Corporation
 */

#include <linux/gpio/consumer.h>
#include <linux/ip.h>
#include <linux/net_tstamp.h>
#include <linux/mii.h>
#include <linux/phy.h>
#include <linux/ptp_classify.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/udp.h>
#include <asm/unaligned.h>

#include "mscc.h"
#include "mscc_ptp.h"

/* Two PHYs share the same 1588 processor and it's to be entirely configured
 * through the base PHY of this processor.
 */
/* phydev->bus->mdio_lock should be locked when using this function */
static int phy_ts_base_write(struct phy_device *phydev, u32 regnum, u16 val)
{
	struct vsc8531_private *priv = phydev->priv;

	WARN_ON_ONCE(!mutex_is_locked(&phydev->mdio.bus->mdio_lock));
	return __mdiobus_write(phydev->mdio.bus, priv->ts_base_addr, regnum,
			       val);
}

/* phydev->bus->mdio_lock should be locked when using this function */
static int phy_ts_base_read(struct phy_device *phydev, u32 regnum)
{
	struct vsc8531_private *priv = phydev->priv;

	WARN_ON_ONCE(!mutex_is_locked(&phydev->mdio.bus->mdio_lock));
	return __mdiobus_read(phydev->mdio.bus, priv->ts_base_addr, regnum);
}

enum ts_blk_hw {
	INGRESS_ENGINE_0,
	EGRESS_ENGINE_0,
	INGRESS_ENGINE_1,
	EGRESS_ENGINE_1,
	INGRESS_ENGINE_2,
	EGRESS_ENGINE_2,
	PROCESSOR_0,
	PROCESSOR_1,
};

enum ts_blk {
	INGRESS,
	EGRESS,
	PROCESSOR,
};

static u32 vsc85xx_ts_read_csr(struct phy_device *phydev, enum ts_blk blk,
			       u16 addr)
{
	struct vsc8531_private *priv = phydev->priv;
	bool base_port = phydev->mdio.addr == priv->ts_base_addr;
	u32 val, cnt = 0;
	enum ts_blk_hw blk_hw;

	switch (blk) {
	case INGRESS:
		blk_hw = base_port ? INGRESS_ENGINE_0 : INGRESS_ENGINE_1;
		break;
	case EGRESS:
		blk_hw = base_port ? EGRESS_ENGINE_0 : EGRESS_ENGINE_1;
		break;
	case PROCESSOR:
	default:
		blk_hw = base_port ? PROCESSOR_0 : PROCESSOR_1;
		break;
	}

	phy_lock_mdio_bus(phydev);

	phy_ts_base_write(phydev, MSCC_EXT_PAGE_ACCESS, MSCC_PHY_PAGE_1588);

	phy_ts_base_write(phydev, MSCC_PHY_TS_BIU_ADDR_CNTL, BIU_ADDR_EXE |
			  BIU_ADDR_READ | BIU_BLK_ID(blk_hw) |
			  BIU_CSR_ADDR(addr));

	do {
		val = phy_ts_base_read(phydev, MSCC_PHY_TS_BIU_ADDR_CNTL);
	} while (!(val & BIU_ADDR_EXE) && cnt++ < BIU_ADDR_CNT_MAX);

	val = phy_ts_base_read(phydev, MSCC_PHY_TS_CSR_DATA_MSB);
	val <<= 16;
	val |= phy_ts_base_read(phydev, MSCC_PHY_TS_CSR_DATA_LSB);

	phy_ts_base_write(phydev, MSCC_EXT_PAGE_ACCESS, MSCC_PHY_PAGE_STANDARD);

	phy_unlock_mdio_bus(phydev);

	return val;
}

static void vsc85xx_ts_write_csr(struct phy_device *phydev, enum ts_blk blk,
				 u16 addr, u32 val)
{
	struct vsc8531_private *priv = phydev->priv;
	bool base_port = phydev->mdio.addr == priv->ts_base_addr;
	u32 reg, bypass, cnt = 0, lower = val & 0xffff, upper = val >> 16;
	bool cond = (addr == MSCC_PHY_PTP_LTC_CTRL ||
		     addr == MSCC_PHY_1588_INGR_VSC85XX_INT_MASK ||
		     addr == MSCC_PHY_1588_VSC85XX_INT_MASK ||
		     addr == MSCC_PHY_1588_INGR_VSC85XX_INT_STATUS ||
		     addr == MSCC_PHY_1588_VSC85XX_INT_STATUS) &&
		    blk == PROCESSOR;
	enum ts_blk_hw blk_hw;

	switch (blk) {
	case INGRESS:
		blk_hw = base_port ? INGRESS_ENGINE_0 : INGRESS_ENGINE_1;
		break;
	case EGRESS:
		blk_hw = base_port ? EGRESS_ENGINE_0 : EGRESS_ENGINE_1;
		break;
	case PROCESSOR:
	default:
		blk_hw = base_port ? PROCESSOR_0 : PROCESSOR_1;
		break;
	}

	phy_lock_mdio_bus(phydev);

	bypass = phy_ts_base_read(phydev, MSCC_PHY_BYPASS_CONTROL);

	phy_ts_base_write(phydev, MSCC_EXT_PAGE_ACCESS, MSCC_PHY_PAGE_1588);

	if (!cond || (cond && upper))
		phy_ts_base_write(phydev, MSCC_PHY_TS_CSR_DATA_MSB, upper);

	phy_ts_base_write(phydev, MSCC_PHY_TS_CSR_DATA_LSB, lower);

	phy_ts_base_write(phydev, MSCC_PHY_TS_BIU_ADDR_CNTL, BIU_ADDR_EXE |
			  BIU_ADDR_WRITE | BIU_BLK_ID(blk_hw) |
			  BIU_CSR_ADDR(addr));

	do {
		reg = phy_ts_base_read(phydev, MSCC_PHY_TS_BIU_ADDR_CNTL);
	} while (!(reg & BIU_ADDR_EXE) && cnt++ < BIU_ADDR_CNT_MAX);

	phy_ts_base_write(phydev, MSCC_EXT_PAGE_ACCESS, MSCC_PHY_PAGE_STANDARD);

	if (cond && upper)
		phy_ts_base_write(phydev, MSCC_PHY_BYPASS_CONTROL, bypass);

	phy_unlock_mdio_bus(phydev);
}

/* Pick bytes from PTP header */
#define PTP_HEADER_TRNSP_MSG		26
#define PTP_HEADER_DOMAIN_NUM		25
#define PTP_HEADER_BYTE_8_31(x)		(31 - (x))
#define MAC_ADDRESS_BYTE(x)		((x) + (35 - ETH_ALEN + 1))

static int vsc85xx_ts_fsb_init(struct phy_device *phydev)
{
	u8 sig_sel[16] = {};
	signed char i, pos = 0;

	/* Seq ID is 2B long and starts at 30th byte */
	for (i = 1; i >= 0; i--)
		sig_sel[pos++] = PTP_HEADER_BYTE_8_31(30 + i);

	/* DomainNum */
	sig_sel[pos++] = PTP_HEADER_DOMAIN_NUM;

	/* MsgType */
	sig_sel[pos++] = PTP_HEADER_TRNSP_MSG;

	/* MAC address is 6B long */
	for (i = ETH_ALEN - 1; i >= 0; i--)
		sig_sel[pos++] = MAC_ADDRESS_BYTE(i);

	/* Fill the last bytes of the signature to reach a 16B signature */
	for (; pos < ARRAY_SIZE(sig_sel); pos++)
		sig_sel[pos] = PTP_HEADER_TRNSP_MSG;

	for (i = 0; i <= 2; i++) {
		u32 val = 0;

		for (pos = i * 5 + 4; pos >= i * 5; pos--)
			val = (val << 6) | sig_sel[pos];

		vsc85xx_ts_write_csr(phydev, EGRESS, MSCC_PHY_ANA_FSB_REG(i),
				     val);
	}

	vsc85xx_ts_write_csr(phydev, EGRESS, MSCC_PHY_ANA_FSB_REG(3),
			     sig_sel[15]);

	return 0;
}

static const u32 vsc85xx_egr_latency[] = {
	/* Copper Egress */
	1272, /* 1000Mbps */
	12516, /* 100Mbps */
	125444, /* 10Mbps */
	/* Fiber Egress */
	1277, /* 1000Mbps */
	12537, /* 100Mbps */
};

static const u32 vsc85xx_egr_latency_macsec[] = {
	/* Copper Egress ON */
	3496, /* 1000Mbps */
	34760, /* 100Mbps */
	347844, /* 10Mbps */
	/* Fiber Egress ON */
	3502, /* 1000Mbps */
	34780, /* 100Mbps */
};

static const u32 vsc85xx_ingr_latency[] = {
	/* Copper Ingress */
	208, /* 1000Mbps */
	304, /* 100Mbps */
	2023, /* 10Mbps */
	/* Fiber Ingress */
	98, /* 1000Mbps */
	197, /* 100Mbps */
};

static const u32 vsc85xx_ingr_latency_macsec[] = {
	/* Copper Ingress */
	2408, /* 1000Mbps */
	22300, /* 100Mbps */
	222009, /* 10Mbps */
	/* Fiber Ingress */
	2299, /* 1000Mbps */
	22192, /* 100Mbps */
};

static void vsc85xx_ts_set_latencies(struct phy_device *phydev)
{
	u32 val, ingr_latency, egr_latency;
	u8 idx;

	/* No need to set latencies of packets if the PHY is not connected */
	if (!phydev->link)
		return;

	vsc85xx_ts_write_csr(phydev, PROCESSOR, MSCC_PHY_PTP_EGR_STALL_LATENCY,
			     STALL_EGR_LATENCY(phydev->speed));

	switch (phydev->speed) {
	case SPEED_100:
		idx = 1;
		break;
	case SPEED_1000:
		idx = 0;
		break;
	default:
		idx = 2;
		break;
	}

	ingr_latency = IS_ENABLED(CONFIG_MACSEC) ?
		vsc85xx_ingr_latency_macsec[idx] : vsc85xx_ingr_latency[idx];
	egr_latency = IS_ENABLED(CONFIG_MACSEC) ?
		vsc85xx_egr_latency_macsec[idx] : vsc85xx_egr_latency[idx];

	vsc85xx_ts_write_csr(phydev, PROCESSOR, MSCC_PHY_PTP_INGR_LOCAL_LATENCY,
			     PTP_INGR_LOCAL_LATENCY(ingr_latency));

	val = vsc85xx_ts_read_csr(phydev, PROCESSOR,
				  MSCC_PHY_PTP_INGR_TSP_CTRL);
	val |= PHY_PTP_INGR_TSP_CTRL_LOAD_DELAYS;
	vsc85xx_ts_write_csr(phydev, PROCESSOR, MSCC_PHY_PTP_INGR_TSP_CTRL,
			     val);

	vsc85xx_ts_write_csr(phydev, PROCESSOR, MSCC_PHY_PTP_EGR_LOCAL_LATENCY,
			     PTP_EGR_LOCAL_LATENCY(egr_latency));

	val = vsc85xx_ts_read_csr(phydev, PROCESSOR, MSCC_PHY_PTP_EGR_TSP_CTRL);
	val |= PHY_PTP_EGR_TSP_CTRL_LOAD_DELAYS;
	vsc85xx_ts_write_csr(phydev, PROCESSOR, MSCC_PHY_PTP_EGR_TSP_CTRL, val);
}

static int vsc85xx_ts_disable_flows(struct phy_device *phydev, enum ts_blk blk)
{
	u8 i;

	vsc85xx_ts_write_csr(phydev, blk, MSCC_ANA_IP1_NXT_PROT_NXT_COMP, 0);
	vsc85xx_ts_write_csr(phydev, blk, MSCC_ANA_IP1_NXT_PROT_UDP_CHKSUM,
			     IP1_NXT_PROT_UDP_CHKSUM_WIDTH(2));
	vsc85xx_ts_write_csr(phydev, blk, MSCC_ANA_IP2_NXT_PROT_NXT_COMP, 0);
	vsc85xx_ts_write_csr(phydev, blk, MSCC_ANA_IP2_NXT_PROT_UDP_CHKSUM,
			     IP2_NXT_PROT_UDP_CHKSUM_WIDTH(2));
	vsc85xx_ts_write_csr(phydev, blk, MSCC_PHY_ANA_MPLS_COMP_NXT_COMP, 0);
	vsc85xx_ts_write_csr(phydev, blk, MSCC_PHY_ANA_ETH1_NTX_PROT, 0);
	vsc85xx_ts_write_csr(phydev, blk, MSCC_PHY_ANA_ETH2_NTX_PROT, 0);

	for (i = 0; i < COMP_MAX_FLOWS; i++) {
		vsc85xx_ts_write_csr(phydev, blk, MSCC_ANA_IP1_FLOW_ENA(i),
				     IP1_FLOW_VALID_CH0 | IP1_FLOW_VALID_CH1);
		vsc85xx_ts_write_csr(phydev, blk, MSCC_ANA_IP2_FLOW_ENA(i),
				     IP2_FLOW_VALID_CH0 | IP2_FLOW_VALID_CH1);
		vsc85xx_ts_write_csr(phydev, blk, MSCC_ANA_ETH1_FLOW_ENA(i),
				     ETH1_FLOW_VALID_CH0 | ETH1_FLOW_VALID_CH1);
		vsc85xx_ts_write_csr(phydev, blk, MSCC_ANA_ETH2_FLOW_ENA(i),
				     ETH2_FLOW_VALID_CH0 | ETH2_FLOW_VALID_CH1);
		vsc85xx_ts_write_csr(phydev, blk, MSCC_ANA_MPLS_FLOW_CTRL(i),
				     MPLS_FLOW_VALID_CH0 | MPLS_FLOW_VALID_CH1);

		if (i >= PTP_COMP_MAX_FLOWS)
			continue;

		vsc85xx_ts_write_csr(phydev, blk, MSCC_ANA_PTP_FLOW_ENA(i), 0);
		vsc85xx_ts_write_csr(phydev, blk,
				     MSCC_ANA_PTP_FLOW_DOMAIN_RANGE(i), 0);
		vsc85xx_ts_write_csr(phydev, blk,
				     MSCC_ANA_PTP_FLOW_MASK_UPPER(i), 0);
		vsc85xx_ts_write_csr(phydev, blk,
				     MSCC_ANA_PTP_FLOW_MASK_LOWER(i), 0);
		vsc85xx_ts_write_csr(phydev, blk,
				     MSCC_ANA_PTP_FLOW_MATCH_UPPER(i), 0);
		vsc85xx_ts_write_csr(phydev, blk,
				     MSCC_ANA_PTP_FLOW_MATCH_LOWER(i), 0);
		vsc85xx_ts_write_csr(phydev, blk,
				     MSCC_ANA_PTP_FLOW_PTP_ACTION(i), 0);
		vsc85xx_ts_write_csr(phydev, blk,
				     MSCC_ANA_PTP_FLOW_PTP_ACTION2(i), 0);
		vsc85xx_ts_write_csr(phydev, blk,
				     MSCC_ANA_PTP_FLOW_PTP_0_FIELD(i), 0);
		vsc85xx_ts_write_csr(phydev, blk, MSCC_ANA_OAM_PTP_FLOW_ENA(i),
				     0);
	}

	return 0;
}

static int vsc85xx_ts_eth_cmp1_sig(struct phy_device *phydev)
{
	u32 val;

	val = vsc85xx_ts_read_csr(phydev, EGRESS, MSCC_PHY_ANA_ETH1_NTX_PROT);
	val &= ~ANA_ETH1_NTX_PROT_SIG_OFF_MASK;
	val |= ANA_ETH1_NTX_PROT_SIG_OFF(0);
	vsc85xx_ts_write_csr(phydev, EGRESS, MSCC_PHY_ANA_ETH1_NTX_PROT, val);

	val = vsc85xx_ts_read_csr(phydev, EGRESS, MSCC_PHY_ANA_FSB_CFG);
	val &= ~ANA_FSB_ADDR_FROM_BLOCK_SEL_MASK;
	val |= ANA_FSB_ADDR_FROM_ETH1;
	vsc85xx_ts_write_csr(phydev, EGRESS, MSCC_PHY_ANA_FSB_CFG, val);

	return 0;
}

static struct vsc85xx_ptphdr *get_ptp_header_l4(struct sk_buff *skb,
						struct iphdr *iphdr,
						struct udphdr *udphdr)
{
	if (iphdr->version != 4 || iphdr->protocol != IPPROTO_UDP)
		return NULL;

	return (struct vsc85xx_ptphdr *)(((unsigned char *)udphdr) + UDP_HLEN);
}

static struct vsc85xx_ptphdr *get_ptp_header_tx(struct sk_buff *skb)
{
	struct ethhdr *ethhdr = eth_hdr(skb);
	struct udphdr *udphdr;
	struct iphdr *iphdr;

	if (ethhdr->h_proto == htons(ETH_P_1588))
		return (struct vsc85xx_ptphdr *)(((unsigned char *)ethhdr) +
						 skb_mac_header_len(skb));

	if (ethhdr->h_proto != htons(ETH_P_IP))
		return NULL;

	iphdr = ip_hdr(skb);
	udphdr = udp_hdr(skb);

	return get_ptp_header_l4(skb, iphdr, udphdr);
}

static struct vsc85xx_ptphdr *get_ptp_header_rx(struct sk_buff *skb,
						enum hwtstamp_rx_filters rx_filter)
{
	struct udphdr *udphdr;
	struct iphdr *iphdr;

	if (rx_filter == HWTSTAMP_FILTER_PTP_V2_L2_EVENT)
		return (struct vsc85xx_ptphdr *)skb->data;

	iphdr = (struct iphdr *)skb->data;
	udphdr = (struct udphdr *)(skb->data + iphdr->ihl * 4);

	return get_ptp_header_l4(skb, iphdr, udphdr);
}

static int get_sig(struct sk_buff *skb, u8 *sig)
{
	struct vsc85xx_ptphdr *ptphdr = get_ptp_header_tx(skb);
	struct ethhdr *ethhdr = eth_hdr(skb);
	unsigned int i;

	if (!ptphdr)
		return -EOPNOTSUPP;

	sig[0] = (__force u16)ptphdr->seq_id >> 8;
	sig[1] = (__force u16)ptphdr->seq_id & GENMASK(7, 0);
	sig[2] = ptphdr->domain;
	sig[3] = ptphdr->tsmt & GENMASK(3, 0);

	memcpy(&sig[4], ethhdr->h_dest, ETH_ALEN);

	/* Fill the last bytes of the signature to reach a 16B signature */
	for (i = 10; i < 16; i++)
		sig[i] = ptphdr->tsmt & GENMASK(3, 0);

	return 0;
}

static void vsc85xx_dequeue_skb(struct vsc85xx_ptp *ptp)
{
	struct skb_shared_hwtstamps shhwtstamps;
	struct vsc85xx_ts_fifo fifo;
	struct sk_buff *skb;
	u8 skb_sig[16], *p;
	int i, len;
	u32 reg;

	memset(&fifo, 0, sizeof(fifo));
	p = (u8 *)&fifo;

	reg = vsc85xx_ts_read_csr(ptp->phydev, PROCESSOR,
				  MSCC_PHY_PTP_EGR_TS_FIFO(0));
	if (reg & PTP_EGR_TS_FIFO_EMPTY)
		return;

	*p++ = reg & 0xff;
	*p++ = (reg >> 8) & 0xff;

	/* Read the current FIFO item. Reading FIFO6 pops the next one. */
	for (i = 1; i < 7; i++) {
		reg = vsc85xx_ts_read_csr(ptp->phydev, PROCESSOR,
					  MSCC_PHY_PTP_EGR_TS_FIFO(i));
		*p++ = reg & 0xff;
		*p++ = (reg >> 8) & 0xff;
		*p++ = (reg >> 16) & 0xff;
		*p++ = (reg >> 24) & 0xff;
	}

	len = skb_queue_len(&ptp->tx_queue);
	if (len < 1)
		return;

	while (len--) {
		skb = __skb_dequeue(&ptp->tx_queue);
		if (!skb)
			return;

		/* Can't get the signature of the packet, won't ever
		 * be able to have one so let's dequeue the packet.
		 */
		if (get_sig(skb, skb_sig) < 0) {
			kfree_skb(skb);
			continue;
		}

		/* Check if we found the signature we were looking for. */
		if (!memcmp(skb_sig, fifo.sig, sizeof(fifo.sig))) {
			memset(&shhwtstamps, 0, sizeof(shhwtstamps));
			shhwtstamps.hwtstamp = ktime_set(fifo.secs, fifo.ns);
			skb_complete_tx_timestamp(skb, &shhwtstamps);

			return;
		}

		/* Valid signature but does not match the one of the
		 * packet in the FIFO right now, reschedule it for later
		 * packets.
		 */
		__skb_queue_tail(&ptp->tx_queue, skb);
	}
}

static void vsc85xx_get_tx_ts(struct vsc85xx_ptp *ptp)
{
	u32 reg;

	do {
		vsc85xx_dequeue_skb(ptp);

		/* If other timestamps are available in the FIFO, process them. */
		reg = vsc85xx_ts_read_csr(ptp->phydev, PROCESSOR,
					  MSCC_PHY_PTP_EGR_TS_FIFO_CTRL);
	} while (PTP_EGR_FIFO_LEVEL_LAST_READ(reg) > 1);
}

static int vsc85xx_ptp_cmp_init(struct phy_device *phydev, enum ts_blk blk)
{
	struct vsc8531_private *vsc8531 = phydev->priv;
	bool base = phydev->mdio.addr == vsc8531->ts_base_addr;
	enum vsc85xx_ptp_msg_type msgs[] = {
		PTP_MSG_TYPE_SYNC,
		PTP_MSG_TYPE_DELAY_REQ
	};
	u32 val;
	u8 i;

	for (i = 0; i < ARRAY_SIZE(msgs); i++) {
		vsc85xx_ts_write_csr(phydev, blk, MSCC_ANA_PTP_FLOW_ENA(i),
				     base ? PTP_FLOW_VALID_CH0 :
				     PTP_FLOW_VALID_CH1);

		val = vsc85xx_ts_read_csr(phydev, blk,
					  MSCC_ANA_PTP_FLOW_DOMAIN_RANGE(i));
		val &= ~PTP_FLOW_DOMAIN_RANGE_ENA;
		vsc85xx_ts_write_csr(phydev, blk,
				     MSCC_ANA_PTP_FLOW_DOMAIN_RANGE(i), val);

		vsc85xx_ts_write_csr(phydev, blk,
				     MSCC_ANA_PTP_FLOW_MATCH_UPPER(i),
				     msgs[i] << 24);

		vsc85xx_ts_write_csr(phydev, blk,
				     MSCC_ANA_PTP_FLOW_MASK_UPPER(i),
				     PTP_FLOW_MSG_TYPE_MASK);
	}

	return 0;
}

static int vsc85xx_eth_cmp1_init(struct phy_device *phydev, enum ts_blk blk)
{
	struct vsc8531_private *vsc8531 = phydev->priv;
	bool base = phydev->mdio.addr == vsc8531->ts_base_addr;
	u32 val;

	vsc85xx_ts_write_csr(phydev, blk, MSCC_PHY_ANA_ETH1_NXT_PROT_TAG, 0);
	vsc85xx_ts_write_csr(phydev, blk, MSCC_PHY_ANA_ETH1_NTX_PROT_VLAN_TPID,
			     ANA_ETH1_NTX_PROT_VLAN_TPID(ETH_P_8021AD));

	vsc85xx_ts_write_csr(phydev, blk, MSCC_ANA_ETH1_FLOW_ENA(0),
			     base ? ETH1_FLOW_VALID_CH0 : ETH1_FLOW_VALID_CH1);
	vsc85xx_ts_write_csr(phydev, blk, MSCC_ANA_ETH1_FLOW_MATCH_MODE(0),
			     ANA_ETH1_FLOW_MATCH_VLAN_TAG2);
	vsc85xx_ts_write_csr(phydev, blk, MSCC_ANA_ETH1_FLOW_ADDR_MATCH1(0), 0);
	vsc85xx_ts_write_csr(phydev, blk, MSCC_ANA_ETH1_FLOW_ADDR_MATCH2(0), 0);
	vsc85xx_ts_write_csr(phydev, blk,
			     MSCC_ANA_ETH1_FLOW_VLAN_RANGE_I_TAG(0), 0);
	vsc85xx_ts_write_csr(phydev, blk, MSCC_ANA_ETH1_FLOW_VLAN_TAG1(0), 0);
	vsc85xx_ts_write_csr(phydev, blk,
			     MSCC_ANA_ETH1_FLOW_VLAN_TAG2_I_TAG(0), 0);

	val = vsc85xx_ts_read_csr(phydev, blk,
				  MSCC_ANA_ETH1_FLOW_MATCH_MODE(0));
	val &= ~ANA_ETH1_FLOW_MATCH_VLAN_TAG_MASK;
	val |= ANA_ETH1_FLOW_MATCH_VLAN_VERIFY;
	vsc85xx_ts_write_csr(phydev, blk, MSCC_ANA_ETH1_FLOW_MATCH_MODE(0),
			     val);

	return 0;
}

static int vsc85xx_ip_cmp1_init(struct phy_device *phydev, enum ts_blk blk)
{
	struct vsc8531_private *vsc8531 = phydev->priv;
	bool base = phydev->mdio.addr == vsc8531->ts_base_addr;
	u32 val;

	vsc85xx_ts_write_csr(phydev, blk, MSCC_ANA_IP1_NXT_PROT_MATCH2_UPPER,
			     PTP_EV_PORT);
	/* Match on dest port only, ignore src */
	vsc85xx_ts_write_csr(phydev, blk, MSCC_ANA_IP1_NXT_PROT_MASK2_UPPER,
			     0xffff);
	vsc85xx_ts_write_csr(phydev, blk, MSCC_ANA_IP1_NXT_PROT_MATCH2_LOWER,
			     0);
	vsc85xx_ts_write_csr(phydev, blk, MSCC_ANA_IP1_NXT_PROT_MASK2_LOWER, 0);

	val = vsc85xx_ts_read_csr(phydev, blk, MSCC_ANA_IP1_FLOW_ENA(0));
	val &= ~IP1_FLOW_ENA_CHANNEL_MASK_MASK;
	val |= base ? IP1_FLOW_VALID_CH0 : IP1_FLOW_VALID_CH1;
	vsc85xx_ts_write_csr(phydev, blk, MSCC_ANA_IP1_FLOW_ENA(0), val);

	/* Match all IPs */
	vsc85xx_ts_write_csr(phydev, blk, MSCC_ANA_IP1_FLOW_MATCH_UPPER(0), 0);
	vsc85xx_ts_write_csr(phydev, blk, MSCC_ANA_IP1_FLOW_MASK_UPPER(0), 0);
	vsc85xx_ts_write_csr(phydev, blk, MSCC_ANA_IP1_FLOW_MATCH_UPPER_MID(0),
			     0);
	vsc85xx_ts_write_csr(phydev, blk, MSCC_ANA_IP1_FLOW_MASK_UPPER_MID(0),
			     0);
	vsc85xx_ts_write_csr(phydev, blk, MSCC_ANA_IP1_FLOW_MATCH_LOWER_MID(0),
			     0);
	vsc85xx_ts_write_csr(phydev, blk, MSCC_ANA_IP1_FLOW_MASK_LOWER_MID(0),
			     0);
	vsc85xx_ts_write_csr(phydev, blk, MSCC_ANA_IP1_FLOW_MATCH_LOWER(0), 0);
	vsc85xx_ts_write_csr(phydev, blk, MSCC_ANA_IP1_FLOW_MASK_LOWER(0), 0);

	vsc85xx_ts_write_csr(phydev, blk, MSCC_ANA_PTP_IP_CHKSUM_SEL, 0);

	return 0;
}

static int vsc85xx_adjfine(struct ptp_clock_info *info, long scaled_ppm)
{
	struct vsc85xx_ptp *ptp = container_of(info, struct vsc85xx_ptp, caps);
	struct phy_device *phydev = ptp->phydev;
	struct vsc8531_private *priv = phydev->priv;
	u64 adj = 0;
	u32 val;

	if (abs(scaled_ppm) < 66 || abs(scaled_ppm) > 65536UL * 1000000UL)
		return 0;

	adj = div64_u64(1000000ULL * 65536ULL, abs(scaled_ppm));
	if (adj > 1000000000L)
		adj = 1000000000L;

	val = PTP_AUTO_ADJ_NS_ROLLOVER(adj);
	val |= scaled_ppm > 0 ? PTP_AUTO_ADJ_ADD_1NS : PTP_AUTO_ADJ_SUB_1NS;

	mutex_lock(&priv->phc_lock);

	/* Update the ppb val in nano seconds to the auto adjust reg. */
	vsc85xx_ts_write_csr(phydev, PROCESSOR, MSCC_PHY_PTP_LTC_AUTO_ADJ,
			     val);

	/* The auto adjust update val is set to 0 after write operation. */
	val = vsc85xx_ts_read_csr(phydev, PROCESSOR, MSCC_PHY_PTP_LTC_CTRL);
	val |= PTP_LTC_CTRL_AUTO_ADJ_UPDATE;
	vsc85xx_ts_write_csr(phydev, PROCESSOR, MSCC_PHY_PTP_LTC_CTRL, val);

	mutex_unlock(&priv->phc_lock);

	return 0;
}

static int __vsc85xx_gettime(struct ptp_clock_info *info, struct timespec64 *ts)
{
	struct vsc85xx_ptp *ptp = container_of(info, struct vsc85xx_ptp, caps);
	struct phy_device *phydev = ptp->phydev;
	struct vsc85xx_shared_private *shared =
		(struct vsc85xx_shared_private *)phydev->shared->priv;
	struct vsc8531_private *priv = phydev->priv;
	u32 val;

	val = vsc85xx_ts_read_csr(phydev, PROCESSOR, MSCC_PHY_PTP_LTC_CTRL);
	val |= PTP_LTC_CTRL_SAVE_ENA;
	vsc85xx_ts_write_csr(phydev, PROCESSOR, MSCC_PHY_PTP_LTC_CTRL, val);

	/* Local Time Counter (LTC) is put in SAVE* regs on rising edge of
	 * LOAD_SAVE pin.
	 */
	mutex_lock(&shared->gpio_lock);
	gpiod_set_value(priv->load_save, 1);

	val = vsc85xx_ts_read_csr(phydev, PROCESSOR,
				  MSCC_PHY_PTP_LTC_SAVED_SEC_MSB);

	ts->tv_sec = ((time64_t)val) << 32;

	val = vsc85xx_ts_read_csr(phydev, PROCESSOR,
				  MSCC_PHY_PTP_LTC_SAVED_SEC_LSB);
	ts->tv_sec += val;

	ts->tv_nsec = vsc85xx_ts_read_csr(phydev, PROCESSOR,
					  MSCC_PHY_PTP_LTC_SAVED_NS);

	gpiod_set_value(priv->load_save, 0);
	mutex_unlock(&shared->gpio_lock);

	return 0;
}

static int vsc85xx_gettime(struct ptp_clock_info *info, struct timespec64 *ts)
{
	struct vsc85xx_ptp *ptp = container_of(info, struct vsc85xx_ptp, caps);
	struct phy_device *phydev = ptp->phydev;
	struct vsc8531_private *priv = phydev->priv;

	mutex_lock(&priv->phc_lock);
	__vsc85xx_gettime(info, ts);
	mutex_unlock(&priv->phc_lock);

	return 0;
}

static int __vsc85xx_settime(struct ptp_clock_info *info,
			     const struct timespec64 *ts)
{
	struct vsc85xx_ptp *ptp = container_of(info, struct vsc85xx_ptp, caps);
	struct phy_device *phydev = ptp->phydev;
	struct vsc85xx_shared_private *shared =
		(struct vsc85xx_shared_private *)phydev->shared->priv;
	struct vsc8531_private *priv = phydev->priv;
	u32 val;

	vsc85xx_ts_write_csr(phydev, PROCESSOR, MSCC_PHY_PTP_LTC_LOAD_SEC_MSB,
			     PTP_LTC_LOAD_SEC_MSB(ts->tv_sec));
	vsc85xx_ts_write_csr(phydev, PROCESSOR, MSCC_PHY_PTP_LTC_LOAD_SEC_LSB,
			     PTP_LTC_LOAD_SEC_LSB(ts->tv_sec));
	vsc85xx_ts_write_csr(phydev, PROCESSOR, MSCC_PHY_PTP_LTC_LOAD_NS,
			     PTP_LTC_LOAD_NS(ts->tv_nsec));

	val = vsc85xx_ts_read_csr(phydev, PROCESSOR, MSCC_PHY_PTP_LTC_CTRL);
	val |= PTP_LTC_CTRL_LOAD_ENA;
	vsc85xx_ts_write_csr(phydev, PROCESSOR, MSCC_PHY_PTP_LTC_CTRL, val);

	/* Local Time Counter (LTC) is set from LOAD* regs on rising edge of
	 * LOAD_SAVE pin.
	 */
	mutex_lock(&shared->gpio_lock);
	gpiod_set_value(priv->load_save, 1);

	val &= ~PTP_LTC_CTRL_LOAD_ENA;
	vsc85xx_ts_write_csr(phydev, PROCESSOR, MSCC_PHY_PTP_LTC_CTRL, val);

	gpiod_set_value(priv->load_save, 0);
	mutex_unlock(&shared->gpio_lock);

	return 0;
}

static int vsc85xx_settime(struct ptp_clock_info *info,
			   const struct timespec64 *ts)
{
	struct vsc85xx_ptp *ptp = container_of(info, struct vsc85xx_ptp, caps);
	struct phy_device *phydev = ptp->phydev;
	struct vsc8531_private *priv = phydev->priv;

	mutex_lock(&priv->phc_lock);
	__vsc85xx_settime(info, ts);
	mutex_unlock(&priv->phc_lock);

	return 0;
}

static int vsc85xx_adjtime(struct ptp_clock_info *info, s64 delta)
{
	struct vsc85xx_ptp *ptp = container_of(info, struct vsc85xx_ptp, caps);
	struct phy_device *phydev = ptp->phydev;
	struct vsc8531_private *priv = phydev->priv;
	u32 val;

	/* Can't recover that big of an offset. Let's set the time directly. */
	if (abs(delta) >= NSEC_PER_SEC) {
		struct timespec64 ts;
		u64 now;

		mutex_lock(&priv->phc_lock);

		__vsc85xx_gettime(info, &ts);
		now = ktime_to_ns(timespec64_to_ktime(ts));
		ts = ns_to_timespec64(now + delta);
		__vsc85xx_settime(info, &ts);

		mutex_unlock(&priv->phc_lock);

		return 0;
	}

	mutex_lock(&priv->phc_lock);

	val = PTP_LTC_OFFSET_VAL(abs(delta)) | PTP_LTC_OFFSET_ADJ;
	if (delta > 0)
		val |= PTP_LTC_OFFSET_ADD;
	vsc85xx_ts_write_csr(phydev, PROCESSOR, MSCC_PHY_PTP_LTC_OFFSET, val);

	mutex_unlock(&priv->phc_lock);

	return 0;
}

static int vsc85xx_eth1_next_comp(struct phy_device *phydev, enum ts_blk blk,
				  u32 next_comp, u32 etype)
{
	u32 val;

	val = vsc85xx_ts_read_csr(phydev, blk, MSCC_PHY_ANA_ETH1_NTX_PROT);
	val &= ~ANA_ETH1_NTX_PROT_COMPARATOR_MASK;
	val |= next_comp;
	vsc85xx_ts_write_csr(phydev, blk, MSCC_PHY_ANA_ETH1_NTX_PROT, val);

	val = ANA_ETH1_NXT_PROT_ETYPE_MATCH(etype) |
		ANA_ETH1_NXT_PROT_ETYPE_MATCH_ENA;
	vsc85xx_ts_write_csr(phydev, blk,
			     MSCC_PHY_ANA_ETH1_NXT_PROT_ETYPE_MATCH, val);

	return 0;
}

static int vsc85xx_ip1_next_comp(struct phy_device *phydev, enum ts_blk blk,
				 u32 next_comp, u32 header)
{
	vsc85xx_ts_write_csr(phydev, blk, MSCC_ANA_IP1_NXT_PROT_NXT_COMP,
			     ANA_IP1_NXT_PROT_NXT_COMP_BYTES_HDR(header) |
			     next_comp);

	return 0;
}

static int vsc85xx_ts_ptp_action_flow(struct phy_device *phydev, enum ts_blk blk, u8 flow, enum ptp_cmd cmd)
{
	u32 val;

	/* Check non-zero reserved field */
	val = PTP_FLOW_PTP_0_FIELD_PTP_FRAME | PTP_FLOW_PTP_0_FIELD_RSVRD_CHECK;
	vsc85xx_ts_write_csr(phydev, blk,
			     MSCC_ANA_PTP_FLOW_PTP_0_FIELD(flow), val);

	val = PTP_FLOW_PTP_ACTION_CORR_OFFSET(8) |
	      PTP_FLOW_PTP_ACTION_TIME_OFFSET(8) |
	      PTP_FLOW_PTP_ACTION_PTP_CMD(cmd == PTP_SAVE_IN_TS_FIFO ?
					  PTP_NOP : cmd);
	if (cmd == PTP_SAVE_IN_TS_FIFO)
		val |= PTP_FLOW_PTP_ACTION_SAVE_LOCAL_TIME;
	else if (cmd == PTP_WRITE_NS)
		val |= PTP_FLOW_PTP_ACTION_MOD_FRAME_STATUS_UPDATE |
		       PTP_FLOW_PTP_ACTION_MOD_FRAME_STATUS_BYTE_OFFSET(6);
	vsc85xx_ts_write_csr(phydev, blk, MSCC_ANA_PTP_FLOW_PTP_ACTION(flow),
			     val);

	if (cmd == PTP_WRITE_1588)
		/* Rewrite timestamp directly in frame */
		val = PTP_FLOW_PTP_ACTION2_REWRITE_OFFSET(34) |
		      PTP_FLOW_PTP_ACTION2_REWRITE_BYTES(10);
	else if (cmd == PTP_SAVE_IN_TS_FIFO)
		/* no rewrite */
		val = PTP_FLOW_PTP_ACTION2_REWRITE_OFFSET(0) |
		      PTP_FLOW_PTP_ACTION2_REWRITE_BYTES(0);
	else
		/* Write in reserved field */
		val = PTP_FLOW_PTP_ACTION2_REWRITE_OFFSET(16) |
		      PTP_FLOW_PTP_ACTION2_REWRITE_BYTES(4);
	vsc85xx_ts_write_csr(phydev, blk,
			     MSCC_ANA_PTP_FLOW_PTP_ACTION2(flow), val);

	return 0;
}

static int vsc85xx_ptp_conf(struct phy_device *phydev, enum ts_blk blk,
			    bool one_step, bool enable)
{
	enum vsc85xx_ptp_msg_type msgs[] = {
		PTP_MSG_TYPE_SYNC,
		PTP_MSG_TYPE_DELAY_REQ
	};
	u32 val;
	u8 i;

	for (i = 0; i < ARRAY_SIZE(msgs); i++) {
		if (blk == INGRESS)
			vsc85xx_ts_ptp_action_flow(phydev, blk, msgs[i],
						   PTP_WRITE_NS);
		else if (msgs[i] == PTP_MSG_TYPE_SYNC && one_step)
			/* no need to know Sync t when sending in one_step */
			vsc85xx_ts_ptp_action_flow(phydev, blk, msgs[i],
						   PTP_WRITE_1588);
		else
			vsc85xx_ts_ptp_action_flow(phydev, blk, msgs[i],
						   PTP_SAVE_IN_TS_FIFO);

		val = vsc85xx_ts_read_csr(phydev, blk,
					  MSCC_ANA_PTP_FLOW_ENA(i));
		val &= ~PTP_FLOW_ENA;
		if (enable)
			val |= PTP_FLOW_ENA;
		vsc85xx_ts_write_csr(phydev, blk, MSCC_ANA_PTP_FLOW_ENA(i),
				     val);
	}

	return 0;
}

static int vsc85xx_eth1_conf(struct phy_device *phydev, enum ts_blk blk,
			     bool enable)
{
	struct vsc8531_private *vsc8531 = phydev->priv;
	u32 val = ANA_ETH1_FLOW_ADDR_MATCH2_DEST;

	if (vsc8531->ptp->rx_filter == HWTSTAMP_FILTER_PTP_V2_L2_EVENT) {
		/* PTP over Ethernet multicast address for SYNC and DELAY msg */
		u8 ptp_multicast[6] = {0x01, 0x1b, 0x19, 0x00, 0x00, 0x00};

		val |= ANA_ETH1_FLOW_ADDR_MATCH2_FULL_ADDR |
		       get_unaligned_be16(&ptp_multicast[4]);
		vsc85xx_ts_write_csr(phydev, blk,
				     MSCC_ANA_ETH1_FLOW_ADDR_MATCH2(0), val);
		vsc85xx_ts_write_csr(phydev, blk,
				     MSCC_ANA_ETH1_FLOW_ADDR_MATCH1(0),
				     get_unaligned_be32(ptp_multicast));
	} else {
		val |= ANA_ETH1_FLOW_ADDR_MATCH2_ANY_MULTICAST;
		vsc85xx_ts_write_csr(phydev, blk,
				     MSCC_ANA_ETH1_FLOW_ADDR_MATCH2(0), val);
		vsc85xx_ts_write_csr(phydev, blk,
				     MSCC_ANA_ETH1_FLOW_ADDR_MATCH1(0), 0);
	}

	val = vsc85xx_ts_read_csr(phydev, blk, MSCC_ANA_ETH1_FLOW_ENA(0));
	val &= ~ETH1_FLOW_ENA;
	if (enable)
		val |= ETH1_FLOW_ENA;
	vsc85xx_ts_write_csr(phydev, blk, MSCC_ANA_ETH1_FLOW_ENA(0), val);

	return 0;
}

static int vsc85xx_ip1_conf(struct phy_device *phydev, enum ts_blk blk,
			    bool enable)
{
	u32 val;

	vsc85xx_ts_write_csr(phydev, blk, MSCC_ANA_IP1_NXT_PROT_IP1_MODE,
			     ANA_IP1_NXT_PROT_IPV4 |
			     ANA_IP1_NXT_PROT_FLOW_OFFSET_IPV4);

	/* Matching UDP protocol number */
	val = ANA_IP1_NXT_PROT_IP_MATCH1_PROT_MASK(0xff) |
	      ANA_IP1_NXT_PROT_IP_MATCH1_PROT_MATCH(IPPROTO_UDP) |
	      ANA_IP1_NXT_PROT_IP_MATCH1_PROT_OFF(9);
	vsc85xx_ts_write_csr(phydev, blk, MSCC_ANA_IP1_NXT_PROT_IP_MATCH1,
			     val);

	/* End of IP protocol, start of next protocol (UDP) */
	vsc85xx_ts_write_csr(phydev, blk, MSCC_ANA_IP1_NXT_PROT_OFFSET2,
			     ANA_IP1_NXT_PROT_OFFSET2(20));

	val = vsc85xx_ts_read_csr(phydev, blk,
				  MSCC_ANA_IP1_NXT_PROT_UDP_CHKSUM);
	val &= ~(IP1_NXT_PROT_UDP_CHKSUM_OFF_MASK |
		 IP1_NXT_PROT_UDP_CHKSUM_WIDTH_MASK);
	val |= IP1_NXT_PROT_UDP_CHKSUM_WIDTH(2);

	val &= ~(IP1_NXT_PROT_UDP_CHKSUM_UPDATE |
		 IP1_NXT_PROT_UDP_CHKSUM_CLEAR);
	/* UDP checksum offset in IPv4 packet
	 * according to: https://tools.ietf.org/html/rfc768
	 */
	val |= IP1_NXT_PROT_UDP_CHKSUM_OFF(26) | IP1_NXT_PROT_UDP_CHKSUM_CLEAR;
	vsc85xx_ts_write_csr(phydev, blk, MSCC_ANA_IP1_NXT_PROT_UDP_CHKSUM,
			     val);

	val = vsc85xx_ts_read_csr(phydev, blk, MSCC_ANA_IP1_FLOW_ENA(0));
	val &= ~(IP1_FLOW_MATCH_ADDR_MASK | IP1_FLOW_ENA);
	val |= IP1_FLOW_MATCH_DEST_SRC_ADDR;
	if (enable)
		val |= IP1_FLOW_ENA;
	vsc85xx_ts_write_csr(phydev, blk, MSCC_ANA_IP1_FLOW_ENA(0), val);

	return 0;
}

static int vsc85xx_ts_engine_init(struct phy_device *phydev, bool one_step)
{
	struct vsc8531_private *vsc8531 = phydev->priv;
	bool ptp_l4, base = phydev->mdio.addr == vsc8531->ts_base_addr;
	u8 eng_id = base ? 0 : 1;
	u32 val;

	ptp_l4 = vsc8531->ptp->rx_filter == HWTSTAMP_FILTER_PTP_V2_L4_EVENT;

	val = vsc85xx_ts_read_csr(phydev, PROCESSOR,
				  MSCC_PHY_PTP_ANALYZER_MODE);
	/* Disable INGRESS and EGRESS so engine eng_id can be reconfigured */
	val &= ~(PTP_ANALYZER_MODE_EGR_ENA(BIT(eng_id)) |
		 PTP_ANALYZER_MODE_INGR_ENA(BIT(eng_id)));
	vsc85xx_ts_write_csr(phydev, PROCESSOR, MSCC_PHY_PTP_ANALYZER_MODE,
			     val);

	if (vsc8531->ptp->rx_filter == HWTSTAMP_FILTER_PTP_V2_L2_EVENT) {
		vsc85xx_eth1_next_comp(phydev, INGRESS,
				       ANA_ETH1_NTX_PROT_PTP_OAM, ETH_P_1588);
		vsc85xx_eth1_next_comp(phydev, EGRESS,
				       ANA_ETH1_NTX_PROT_PTP_OAM, ETH_P_1588);
	} else {
		vsc85xx_eth1_next_comp(phydev, INGRESS,
				       ANA_ETH1_NTX_PROT_IP_UDP_ACH_1,
				       ETH_P_IP);
		vsc85xx_eth1_next_comp(phydev, EGRESS,
				       ANA_ETH1_NTX_PROT_IP_UDP_ACH_1,
				       ETH_P_IP);
		/* Header length of IPv[4/6] + UDP */
		vsc85xx_ip1_next_comp(phydev, INGRESS,
				      ANA_ETH1_NTX_PROT_PTP_OAM, 28);
		vsc85xx_ip1_next_comp(phydev, EGRESS,
				      ANA_ETH1_NTX_PROT_PTP_OAM, 28);
	}

	vsc85xx_eth1_conf(phydev, INGRESS,
			  vsc8531->ptp->rx_filter != HWTSTAMP_FILTER_NONE);
	vsc85xx_ip1_conf(phydev, INGRESS,
			 ptp_l4 && vsc8531->ptp->rx_filter != HWTSTAMP_FILTER_NONE);
	vsc85xx_ptp_conf(phydev, INGRESS, one_step,
			 vsc8531->ptp->rx_filter != HWTSTAMP_FILTER_NONE);

	vsc85xx_eth1_conf(phydev, EGRESS,
			  vsc8531->ptp->tx_type != HWTSTAMP_TX_OFF);
	vsc85xx_ip1_conf(phydev, EGRESS,
			 ptp_l4 && vsc8531->ptp->tx_type != HWTSTAMP_TX_OFF);
	vsc85xx_ptp_conf(phydev, EGRESS, one_step,
			 vsc8531->ptp->tx_type != HWTSTAMP_TX_OFF);

	val &= ~PTP_ANALYZER_MODE_EGR_ENA(BIT(eng_id));
	if (vsc8531->ptp->tx_type != HWTSTAMP_TX_OFF)
		val |= PTP_ANALYZER_MODE_EGR_ENA(BIT(eng_id));

	val &= ~PTP_ANALYZER_MODE_INGR_ENA(BIT(eng_id));
	if (vsc8531->ptp->rx_filter != HWTSTAMP_FILTER_NONE)
		val |= PTP_ANALYZER_MODE_INGR_ENA(BIT(eng_id));

	vsc85xx_ts_write_csr(phydev, PROCESSOR, MSCC_PHY_PTP_ANALYZER_MODE,
			     val);

	return 0;
}

void vsc85xx_link_change_notify(struct phy_device *phydev)
{
	struct vsc8531_private *priv = phydev->priv;

	mutex_lock(&priv->ts_lock);
	vsc85xx_ts_set_latencies(phydev);
	mutex_unlock(&priv->ts_lock);
}

static void vsc85xx_ts_reset_fifo(struct phy_device *phydev)
{
	u32 val;

	val = vsc85xx_ts_read_csr(phydev, PROCESSOR,
				  MSCC_PHY_PTP_EGR_TS_FIFO_CTRL);
	val |= PTP_EGR_TS_FIFO_RESET;
	vsc85xx_ts_write_csr(phydev, PROCESSOR, MSCC_PHY_PTP_EGR_TS_FIFO_CTRL,
			     val);

	val &= ~PTP_EGR_TS_FIFO_RESET;
	vsc85xx_ts_write_csr(phydev, PROCESSOR, MSCC_PHY_PTP_EGR_TS_FIFO_CTRL,
			     val);
}

static int vsc85xx_hwtstamp(struct mii_timestamper *mii_ts, struct ifreq *ifr)
{
	struct vsc8531_private *vsc8531 =
		container_of(mii_ts, struct vsc8531_private, mii_ts);
	struct phy_device *phydev = vsc8531->ptp->phydev;
	struct hwtstamp_config cfg;
	bool one_step = false;
	u32 val;

	if (copy_from_user(&cfg, ifr->ifr_data, sizeof(cfg)))
		return -EFAULT;

	if (cfg.flags)
		return -EINVAL;

	switch (cfg.tx_type) {
	case HWTSTAMP_TX_ONESTEP_SYNC:
		one_step = true;
		break;
	case HWTSTAMP_TX_ON:
		break;
	case HWTSTAMP_TX_OFF:
		break;
	default:
		return -ERANGE;
	}

	vsc8531->ptp->tx_type = cfg.tx_type;

	switch (cfg.rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		break;
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
		/* ETH->IP->UDP->PTP */
		break;
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
		/* ETH->PTP */
		break;
	default:
		return -ERANGE;
	}

	vsc8531->ptp->rx_filter = cfg.rx_filter;

	mutex_lock(&vsc8531->ts_lock);

	__skb_queue_purge(&vsc8531->ptp->tx_queue);
	__skb_queue_head_init(&vsc8531->ptp->tx_queue);

	/* Disable predictor while configuring the 1588 block */
	val = vsc85xx_ts_read_csr(phydev, PROCESSOR,
				  MSCC_PHY_PTP_INGR_PREDICTOR);
	val &= ~PTP_INGR_PREDICTOR_EN;
	vsc85xx_ts_write_csr(phydev, PROCESSOR, MSCC_PHY_PTP_INGR_PREDICTOR,
			     val);
	val = vsc85xx_ts_read_csr(phydev, PROCESSOR,
				  MSCC_PHY_PTP_EGR_PREDICTOR);
	val &= ~PTP_EGR_PREDICTOR_EN;
	vsc85xx_ts_write_csr(phydev, PROCESSOR, MSCC_PHY_PTP_EGR_PREDICTOR,
			     val);

	/* Bypass egress or ingress blocks if timestamping isn't used */
	val = vsc85xx_ts_read_csr(phydev, PROCESSOR, MSCC_PHY_PTP_IFACE_CTRL);
	val &= ~(PTP_IFACE_CTRL_EGR_BYPASS | PTP_IFACE_CTRL_INGR_BYPASS);
	if (vsc8531->ptp->tx_type == HWTSTAMP_TX_OFF)
		val |= PTP_IFACE_CTRL_EGR_BYPASS;
	if (vsc8531->ptp->rx_filter == HWTSTAMP_FILTER_NONE)
		val |= PTP_IFACE_CTRL_INGR_BYPASS;
	vsc85xx_ts_write_csr(phydev, PROCESSOR, MSCC_PHY_PTP_IFACE_CTRL, val);

	/* Resetting FIFO so that it's empty after reconfiguration */
	vsc85xx_ts_reset_fifo(phydev);

	vsc85xx_ts_engine_init(phydev, one_step);

	/* Re-enable predictors now */
	val = vsc85xx_ts_read_csr(phydev, PROCESSOR,
				  MSCC_PHY_PTP_INGR_PREDICTOR);
	val |= PTP_INGR_PREDICTOR_EN;
	vsc85xx_ts_write_csr(phydev, PROCESSOR, MSCC_PHY_PTP_INGR_PREDICTOR,
			     val);
	val = vsc85xx_ts_read_csr(phydev, PROCESSOR,
				  MSCC_PHY_PTP_EGR_PREDICTOR);
	val |= PTP_EGR_PREDICTOR_EN;
	vsc85xx_ts_write_csr(phydev, PROCESSOR, MSCC_PHY_PTP_EGR_PREDICTOR,
			     val);

	vsc8531->ptp->configured = 1;
	mutex_unlock(&vsc8531->ts_lock);

	return copy_to_user(ifr->ifr_data, &cfg, sizeof(cfg)) ? -EFAULT : 0;
}

static int vsc85xx_ts_info(struct mii_timestamper *mii_ts,
			   struct ethtool_ts_info *info)
{
	struct vsc8531_private *vsc8531 =
		container_of(mii_ts, struct vsc8531_private, mii_ts);

	info->phc_index = ptp_clock_index(vsc8531->ptp->ptp_clock);
	info->so_timestamping =
		SOF_TIMESTAMPING_TX_HARDWARE |
		SOF_TIMESTAMPING_RX_HARDWARE |
		SOF_TIMESTAMPING_RAW_HARDWARE;
	info->tx_types =
		(1 << HWTSTAMP_TX_OFF) |
		(1 << HWTSTAMP_TX_ON) |
		(1 << HWTSTAMP_TX_ONESTEP_SYNC);
	info->rx_filters =
		(1 << HWTSTAMP_FILTER_NONE) |
		(1 << HWTSTAMP_FILTER_PTP_V2_L2_EVENT) |
		(1 << HWTSTAMP_FILTER_PTP_V2_L4_EVENT);

	return 0;
}

static void vsc85xx_txtstamp(struct mii_timestamper *mii_ts,
			     struct sk_buff *skb, int type)
{
	struct vsc8531_private *vsc8531 =
		container_of(mii_ts, struct vsc8531_private, mii_ts);

	if (!vsc8531->ptp->configured)
		return;

	if (vsc8531->ptp->tx_type == HWTSTAMP_TX_OFF) {
		kfree_skb(skb);
		return;
	}

	skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;

	mutex_lock(&vsc8531->ts_lock);
	__skb_queue_tail(&vsc8531->ptp->tx_queue, skb);
	mutex_unlock(&vsc8531->ts_lock);
}

static bool vsc85xx_rxtstamp(struct mii_timestamper *mii_ts,
			     struct sk_buff *skb, int type)
{
	struct vsc8531_private *vsc8531 =
		container_of(mii_ts, struct vsc8531_private, mii_ts);
	struct skb_shared_hwtstamps *shhwtstamps = NULL;
	struct vsc85xx_ptphdr *ptphdr;
	struct timespec64 ts;
	unsigned long ns;

	if (!vsc8531->ptp->configured)
		return false;

	if (vsc8531->ptp->rx_filter == HWTSTAMP_FILTER_NONE ||
	    type == PTP_CLASS_NONE)
		return false;

	vsc85xx_gettime(&vsc8531->ptp->caps, &ts);

	ptphdr = get_ptp_header_rx(skb, vsc8531->ptp->rx_filter);
	if (!ptphdr)
		return false;

	shhwtstamps = skb_hwtstamps(skb);
	memset(shhwtstamps, 0, sizeof(struct skb_shared_hwtstamps));

	ns = ntohl(ptphdr->rsrvd2);

	/* nsec is in reserved field */
	if (ts.tv_nsec < ns)
		ts.tv_sec--;

	shhwtstamps->hwtstamp = ktime_set(ts.tv_sec, ns);
	netif_rx_ni(skb);

	return true;
}

static const struct ptp_clock_info vsc85xx_clk_caps = {
	.owner		= THIS_MODULE,
	.name		= "VSC85xx timer",
	.max_adj	= S32_MAX,
	.n_alarm	= 0,
	.n_pins		= 0,
	.n_ext_ts	= 0,
	.n_per_out	= 0,
	.pps		= 0,
	.adjtime        = &vsc85xx_adjtime,
	.adjfine	= &vsc85xx_adjfine,
	.gettime64	= &vsc85xx_gettime,
	.settime64	= &vsc85xx_settime,
};

static struct vsc8531_private *vsc8584_base_priv(struct phy_device *phydev)
{
	struct vsc8531_private *vsc8531 = phydev->priv;

	if (vsc8531->ts_base_addr != phydev->mdio.addr) {
		struct mdio_device *dev;

		dev = phydev->mdio.bus->mdio_map[vsc8531->ts_base_addr];
		phydev = container_of(dev, struct phy_device, mdio);

		return phydev->priv;
	}

	return vsc8531;
}

static bool vsc8584_is_1588_input_clk_configured(struct phy_device *phydev)
{
	struct vsc8531_private *vsc8531 = vsc8584_base_priv(phydev);

	return vsc8531->input_clk_init;
}

static void vsc8584_set_input_clk_configured(struct phy_device *phydev)
{
	struct vsc8531_private *vsc8531 = vsc8584_base_priv(phydev);

	vsc8531->input_clk_init = true;
}

static int __vsc8584_init_ptp(struct phy_device *phydev)
{
	struct vsc8531_private *vsc8531 = phydev->priv;
	u32 ltc_seq_e[] = { 0, 400000, 0, 0, 0 };
	u8  ltc_seq_a[] = { 8, 6, 5, 4, 2 };
	u32 val;

	if (!vsc8584_is_1588_input_clk_configured(phydev)) {
		phy_lock_mdio_bus(phydev);

		/* 1588_DIFF_INPUT_CLK configuration: Use an external clock for
		 * the LTC, as per 3.13.29 in the VSC8584 datasheet.
		 */
		phy_ts_base_write(phydev, MSCC_EXT_PAGE_ACCESS,
				  MSCC_PHY_PAGE_1588);
		phy_ts_base_write(phydev, 29, 0x7ae0);
		phy_ts_base_write(phydev, 30, 0xb71c);
		phy_ts_base_write(phydev, MSCC_EXT_PAGE_ACCESS,
				  MSCC_PHY_PAGE_STANDARD);

		phy_unlock_mdio_bus(phydev);

		vsc8584_set_input_clk_configured(phydev);
	}

	/* Disable predictor before configuring the 1588 block */
	val = vsc85xx_ts_read_csr(phydev, PROCESSOR,
				  MSCC_PHY_PTP_INGR_PREDICTOR);
	val &= ~PTP_INGR_PREDICTOR_EN;
	vsc85xx_ts_write_csr(phydev, PROCESSOR, MSCC_PHY_PTP_INGR_PREDICTOR,
			     val);
	val = vsc85xx_ts_read_csr(phydev, PROCESSOR,
				  MSCC_PHY_PTP_EGR_PREDICTOR);
	val &= ~PTP_EGR_PREDICTOR_EN;
	vsc85xx_ts_write_csr(phydev, PROCESSOR, MSCC_PHY_PTP_EGR_PREDICTOR,
			     val);

	/* By default, the internal clock of fixed rate 250MHz is used */
	val = vsc85xx_ts_read_csr(phydev, PROCESSOR, MSCC_PHY_PTP_LTC_CTRL);
	val &= ~PTP_LTC_CTRL_CLK_SEL_MASK;
	val |= PTP_LTC_CTRL_CLK_SEL_INTERNAL_250;
	vsc85xx_ts_write_csr(phydev, PROCESSOR, MSCC_PHY_PTP_LTC_CTRL, val);

	val = vsc85xx_ts_read_csr(phydev, PROCESSOR, MSCC_PHY_PTP_LTC_SEQUENCE);
	val &= ~PTP_LTC_SEQUENCE_A_MASK;
	val |= PTP_LTC_SEQUENCE_A(ltc_seq_a[PHC_CLK_250MHZ]);
	vsc85xx_ts_write_csr(phydev, PROCESSOR, MSCC_PHY_PTP_LTC_SEQUENCE, val);

	val = vsc85xx_ts_read_csr(phydev, PROCESSOR, MSCC_PHY_PTP_LTC_SEQ);
	val &= ~(PTP_LTC_SEQ_ERR_MASK | PTP_LTC_SEQ_ADD_SUB);
	if (ltc_seq_e[PHC_CLK_250MHZ])
		val |= PTP_LTC_SEQ_ADD_SUB;
	val |= PTP_LTC_SEQ_ERR(ltc_seq_e[PHC_CLK_250MHZ]);
	vsc85xx_ts_write_csr(phydev, PROCESSOR, MSCC_PHY_PTP_LTC_SEQ, val);

	vsc85xx_ts_write_csr(phydev, PROCESSOR, MSCC_PHY_PTP_LTC_1PPS_WIDTH_ADJ,
			     PPS_WIDTH_ADJ);

	vsc85xx_ts_write_csr(phydev, PROCESSOR, MSCC_PHY_PTP_INGR_DELAY_FIFO,
			     IS_ENABLED(CONFIG_MACSEC) ?
			     PTP_INGR_DELAY_FIFO_DEPTH_MACSEC :
			     PTP_INGR_DELAY_FIFO_DEPTH_DEFAULT);

	vsc85xx_ts_write_csr(phydev, PROCESSOR, MSCC_PHY_PTP_EGR_DELAY_FIFO,
			     IS_ENABLED(CONFIG_MACSEC) ?
			     PTP_EGR_DELAY_FIFO_DEPTH_MACSEC :
			     PTP_EGR_DELAY_FIFO_DEPTH_DEFAULT);

	/* Enable n-phase sampler for Viper Rev-B */
	val = vsc85xx_ts_read_csr(phydev, PROCESSOR,
				  MSCC_PHY_PTP_ACCUR_CFG_STATUS);
	val &= ~(PTP_ACCUR_PPS_OUT_BYPASS | PTP_ACCUR_PPS_IN_BYPASS |
		 PTP_ACCUR_EGR_SOF_BYPASS | PTP_ACCUR_INGR_SOF_BYPASS |
		 PTP_ACCUR_LOAD_SAVE_BYPASS);
	val |= PTP_ACCUR_PPS_OUT_CALIB_ERR | PTP_ACCUR_PPS_OUT_CALIB_DONE |
	       PTP_ACCUR_PPS_IN_CALIB_ERR | PTP_ACCUR_PPS_IN_CALIB_DONE |
	       PTP_ACCUR_EGR_SOF_CALIB_ERR | PTP_ACCUR_EGR_SOF_CALIB_DONE |
	       PTP_ACCUR_INGR_SOF_CALIB_ERR | PTP_ACCUR_INGR_SOF_CALIB_DONE |
	       PTP_ACCUR_LOAD_SAVE_CALIB_ERR | PTP_ACCUR_LOAD_SAVE_CALIB_DONE;
	vsc85xx_ts_write_csr(phydev, PROCESSOR, MSCC_PHY_PTP_ACCUR_CFG_STATUS,
			     val);

	val = vsc85xx_ts_read_csr(phydev, PROCESSOR,
				  MSCC_PHY_PTP_ACCUR_CFG_STATUS);
	val |= PTP_ACCUR_CALIB_TRIGG;
	vsc85xx_ts_write_csr(phydev, PROCESSOR, MSCC_PHY_PTP_ACCUR_CFG_STATUS,
			     val);

	val = vsc85xx_ts_read_csr(phydev, PROCESSOR,
				  MSCC_PHY_PTP_ACCUR_CFG_STATUS);
	val &= ~PTP_ACCUR_CALIB_TRIGG;
	val |= PTP_ACCUR_PPS_OUT_CALIB_ERR | PTP_ACCUR_PPS_OUT_CALIB_DONE |
	       PTP_ACCUR_PPS_IN_CALIB_ERR | PTP_ACCUR_PPS_IN_CALIB_DONE |
	       PTP_ACCUR_EGR_SOF_CALIB_ERR | PTP_ACCUR_EGR_SOF_CALIB_DONE |
	       PTP_ACCUR_INGR_SOF_CALIB_ERR | PTP_ACCUR_INGR_SOF_CALIB_DONE |
	       PTP_ACCUR_LOAD_SAVE_CALIB_ERR | PTP_ACCUR_LOAD_SAVE_CALIB_DONE;
	vsc85xx_ts_write_csr(phydev, PROCESSOR, MSCC_PHY_PTP_ACCUR_CFG_STATUS,
			     val);

	val = vsc85xx_ts_read_csr(phydev, PROCESSOR,
				  MSCC_PHY_PTP_ACCUR_CFG_STATUS);
	val |= PTP_ACCUR_CALIB_TRIGG;
	vsc85xx_ts_write_csr(phydev, PROCESSOR, MSCC_PHY_PTP_ACCUR_CFG_STATUS,
			     val);

	val = vsc85xx_ts_read_csr(phydev, PROCESSOR,
				  MSCC_PHY_PTP_ACCUR_CFG_STATUS);
	val &= ~PTP_ACCUR_CALIB_TRIGG;
	vsc85xx_ts_write_csr(phydev, PROCESSOR, MSCC_PHY_PTP_ACCUR_CFG_STATUS,
			     val);

	/* Do not access FIFO via SI */
	val = vsc85xx_ts_read_csr(phydev, PROCESSOR,
				  MSCC_PHY_PTP_TSTAMP_FIFO_SI);
	val &= ~PTP_TSTAMP_FIFO_SI_EN;
	vsc85xx_ts_write_csr(phydev, PROCESSOR, MSCC_PHY_PTP_TSTAMP_FIFO_SI,
			     val);

	val = vsc85xx_ts_read_csr(phydev, PROCESSOR,
				  MSCC_PHY_PTP_INGR_REWRITER_CTRL);
	val &= ~PTP_INGR_REWRITER_REDUCE_PREAMBLE;
	vsc85xx_ts_write_csr(phydev, PROCESSOR, MSCC_PHY_PTP_INGR_REWRITER_CTRL,
			     val);
	val = vsc85xx_ts_read_csr(phydev, PROCESSOR,
				  MSCC_PHY_PTP_EGR_REWRITER_CTRL);
	val &= ~PTP_EGR_REWRITER_REDUCE_PREAMBLE;
	vsc85xx_ts_write_csr(phydev, PROCESSOR, MSCC_PHY_PTP_EGR_REWRITER_CTRL,
			     val);

	/* Put the flag that indicates the frame has been modified to bit 7 */
	val = vsc85xx_ts_read_csr(phydev, PROCESSOR,
				  MSCC_PHY_PTP_INGR_REWRITER_CTRL);
	val |= PTP_INGR_REWRITER_FLAG_BIT_OFF(7) | PTP_INGR_REWRITER_FLAG_VAL;
	vsc85xx_ts_write_csr(phydev, PROCESSOR, MSCC_PHY_PTP_INGR_REWRITER_CTRL,
			     val);
	val = vsc85xx_ts_read_csr(phydev, PROCESSOR,
				  MSCC_PHY_PTP_EGR_REWRITER_CTRL);
	val |= PTP_EGR_REWRITER_FLAG_BIT_OFF(7);
	val &= ~PTP_EGR_REWRITER_FLAG_VAL;
	vsc85xx_ts_write_csr(phydev, PROCESSOR, MSCC_PHY_PTP_EGR_REWRITER_CTRL,
			     val);

	/* 30bit mode for RX timestamp, only the nanoseconds are kept in
	 * reserved field.
	 */
	val = vsc85xx_ts_read_csr(phydev, PROCESSOR,
				  MSCC_PHY_PTP_INGR_TSP_CTRL);
	val |= PHY_PTP_INGR_TSP_CTRL_FRACT_NS;
	vsc85xx_ts_write_csr(phydev, PROCESSOR, MSCC_PHY_PTP_INGR_TSP_CTRL,
			     val);

	val = vsc85xx_ts_read_csr(phydev, PROCESSOR, MSCC_PHY_PTP_EGR_TSP_CTRL);
	val |= PHY_PTP_EGR_TSP_CTRL_FRACT_NS;
	vsc85xx_ts_write_csr(phydev, PROCESSOR, MSCC_PHY_PTP_EGR_TSP_CTRL, val);

	val = vsc85xx_ts_read_csr(phydev, PROCESSOR,
				  MSCC_PHY_PTP_SERIAL_TOD_IFACE);
	val |= PTP_SERIAL_TOD_IFACE_LS_AUTO_CLR;
	vsc85xx_ts_write_csr(phydev, PROCESSOR, MSCC_PHY_PTP_SERIAL_TOD_IFACE,
			     val);

	vsc85xx_ts_fsb_init(phydev);

	/* Set the Egress timestamp FIFO configuration and status register */
	val = vsc85xx_ts_read_csr(phydev, PROCESSOR,
				  MSCC_PHY_PTP_EGR_TS_FIFO_CTRL);
	val &= ~(PTP_EGR_TS_FIFO_SIG_BYTES_MASK | PTP_EGR_TS_FIFO_THRESH_MASK);
	/* 16 bytes for the signature, 10 for the timestamp in the TS FIFO */
	val |= PTP_EGR_TS_FIFO_SIG_BYTES(16) | PTP_EGR_TS_FIFO_THRESH(7);
	vsc85xx_ts_write_csr(phydev, PROCESSOR, MSCC_PHY_PTP_EGR_TS_FIFO_CTRL,
			     val);

	vsc85xx_ts_reset_fifo(phydev);

	val = PTP_IFACE_CTRL_CLK_ENA;
	if (!IS_ENABLED(CONFIG_MACSEC))
		val |= PTP_IFACE_CTRL_GMII_PROT;
	vsc85xx_ts_write_csr(phydev, PROCESSOR, MSCC_PHY_PTP_IFACE_CTRL, val);

	vsc85xx_ts_set_latencies(phydev);

	val = vsc85xx_ts_read_csr(phydev, PROCESSOR, MSCC_PHY_PTP_VERSION_CODE);

	val = vsc85xx_ts_read_csr(phydev, PROCESSOR, MSCC_PHY_PTP_IFACE_CTRL);
	val |= PTP_IFACE_CTRL_EGR_BYPASS;
	vsc85xx_ts_write_csr(phydev, PROCESSOR, MSCC_PHY_PTP_IFACE_CTRL, val);

	vsc85xx_ts_disable_flows(phydev, EGRESS);
	vsc85xx_ts_disable_flows(phydev, INGRESS);

	val = vsc85xx_ts_read_csr(phydev, PROCESSOR,
				  MSCC_PHY_PTP_ANALYZER_MODE);
	/* Disable INGRESS and EGRESS so engine eng_id can be reconfigured */
	val &= ~(PTP_ANALYZER_MODE_EGR_ENA_MASK |
		 PTP_ANALYZER_MODE_INGR_ENA_MASK |
		 PTP_ANA_INGR_ENCAP_FLOW_MODE_MASK |
		 PTP_ANA_EGR_ENCAP_FLOW_MODE_MASK);
	/* Strict matching in flow (packets should match flows from the same
	 * index in all enabled comparators (except PTP)).
	 */
	val |= PTP_ANA_SPLIT_ENCAP_FLOW | PTP_ANA_INGR_ENCAP_FLOW_MODE(0x7) |
	       PTP_ANA_EGR_ENCAP_FLOW_MODE(0x7);
	vsc85xx_ts_write_csr(phydev, PROCESSOR, MSCC_PHY_PTP_ANALYZER_MODE,
			     val);

	/* Initialized for ingress and egress flows:
	 * - The Ethernet comparator.
	 * - The IP comparator.
	 * - The PTP comparator.
	 */
	vsc85xx_eth_cmp1_init(phydev, INGRESS);
	vsc85xx_ip_cmp1_init(phydev, INGRESS);
	vsc85xx_ptp_cmp_init(phydev, INGRESS);
	vsc85xx_eth_cmp1_init(phydev, EGRESS);
	vsc85xx_ip_cmp1_init(phydev, EGRESS);
	vsc85xx_ptp_cmp_init(phydev, EGRESS);

	vsc85xx_ts_eth_cmp1_sig(phydev);

	vsc8531->mii_ts.rxtstamp = vsc85xx_rxtstamp;
	vsc8531->mii_ts.txtstamp = vsc85xx_txtstamp;
	vsc8531->mii_ts.hwtstamp = vsc85xx_hwtstamp;
	vsc8531->mii_ts.ts_info  = vsc85xx_ts_info;
	phydev->mii_ts = &vsc8531->mii_ts;

	memcpy(&vsc8531->ptp->caps, &vsc85xx_clk_caps, sizeof(vsc85xx_clk_caps));

	vsc8531->ptp->ptp_clock = ptp_clock_register(&vsc8531->ptp->caps,
						     &phydev->mdio.dev);
	return PTR_ERR_OR_ZERO(vsc8531->ptp->ptp_clock);
}

void vsc8584_config_ts_intr(struct phy_device *phydev)
{
	struct vsc8531_private *priv = phydev->priv;

	mutex_lock(&priv->ts_lock);
	vsc85xx_ts_write_csr(phydev, PROCESSOR, MSCC_PHY_1588_VSC85XX_INT_MASK,
			     VSC85XX_1588_INT_MASK_MASK);
	mutex_unlock(&priv->ts_lock);
}

int vsc8584_ptp_init(struct phy_device *phydev)
{
	switch (phydev->phy_id & phydev->drv->phy_id_mask) {
	case PHY_ID_VSC8575:
	case PHY_ID_VSC8582:
	case PHY_ID_VSC8584:
		return __vsc8584_init_ptp(phydev);
	}

	return 0;
}

irqreturn_t vsc8584_handle_ts_interrupt(struct phy_device *phydev)
{
	struct vsc8531_private *priv = phydev->priv;
	int rc;

	mutex_lock(&priv->ts_lock);
	rc = vsc85xx_ts_read_csr(phydev, PROCESSOR,
				 MSCC_PHY_1588_VSC85XX_INT_STATUS);
	/* Ack the PTP interrupt */
	vsc85xx_ts_write_csr(phydev, PROCESSOR,
			     MSCC_PHY_1588_VSC85XX_INT_STATUS, rc);

	if (!(rc & VSC85XX_1588_INT_MASK_MASK)) {
		mutex_unlock(&priv->ts_lock);
		return IRQ_NONE;
	}

	if (rc & VSC85XX_1588_INT_FIFO_ADD) {
		vsc85xx_get_tx_ts(priv->ptp);
	} else if (rc & VSC85XX_1588_INT_FIFO_OVERFLOW) {
		__skb_queue_purge(&priv->ptp->tx_queue);
		vsc85xx_ts_reset_fifo(phydev);
	}

	mutex_unlock(&priv->ts_lock);
	return IRQ_HANDLED;
}

int vsc8584_ptp_probe(struct phy_device *phydev)
{
	struct vsc8531_private *vsc8531 = phydev->priv;

	vsc8531->ptp = devm_kzalloc(&phydev->mdio.dev, sizeof(*vsc8531->ptp),
				    GFP_KERNEL);
	if (!vsc8531->ptp)
		return -ENOMEM;

	mutex_init(&vsc8531->phc_lock);
	mutex_init(&vsc8531->ts_lock);

	/* Retrieve the shared load/save GPIO. Request it as non exclusive as
	 * the same GPIO can be requested by all the PHYs of the same package.
	 * This GPIO must be used with the gpio_lock taken (the lock is shared
	 * between all PHYs).
	 */
	vsc8531->load_save = devm_gpiod_get_optional(&phydev->mdio.dev, "load-save",
						     GPIOD_FLAGS_BIT_NONEXCLUSIVE |
						     GPIOD_OUT_LOW);
	if (IS_ERR(vsc8531->load_save)) {
		phydev_err(phydev, "Can't get load-save GPIO (%ld)\n",
			   PTR_ERR(vsc8531->load_save));
		return PTR_ERR(vsc8531->load_save);
	}

	vsc8531->ptp->phydev = phydev;

	return 0;
}

int vsc8584_ptp_probe_once(struct phy_device *phydev)
{
	struct vsc85xx_shared_private *shared =
		(struct vsc85xx_shared_private *)phydev->shared->priv;

	/* Initialize shared GPIO lock */
	mutex_init(&shared->gpio_lock);

	return 0;
}
