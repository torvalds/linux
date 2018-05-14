/* Copyright (c) 2013-2016, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/* Qualcomm Technologies, Inc. EMAC Ethernet Controller MAC layer support
 */

#include <linux/tcp.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/crc32.h>
#include <linux/if_vlan.h>
#include <linux/jiffies.h>
#include <linux/phy.h>
#include <linux/of.h>
#include <net/ip6_checksum.h>
#include "emac.h"
#include "emac-sgmii.h"

/* EMAC_MAC_CTRL */
#define SINGLE_PAUSE_MODE       	0x10000000
#define DEBUG_MODE                      0x08000000
#define BROAD_EN                        0x04000000
#define MULTI_ALL                       0x02000000
#define RX_CHKSUM_EN                    0x01000000
#define HUGE                            0x00800000
#define SPEED(x)			(((x) & 0x3) << 20)
#define SPEED_MASK			SPEED(0x3)
#define SIMR                            0x00080000
#define TPAUSE                          0x00010000
#define PROM_MODE                       0x00008000
#define VLAN_STRIP                      0x00004000
#define PRLEN_BMSK                      0x00003c00
#define PRLEN_SHFT                      10
#define HUGEN                           0x00000200
#define FLCHK                           0x00000100
#define PCRCE                           0x00000080
#define CRCE                            0x00000040
#define FULLD                           0x00000020
#define MAC_LP_EN                       0x00000010
#define RXFC                            0x00000008
#define TXFC                            0x00000004
#define RXEN                            0x00000002
#define TXEN                            0x00000001

/* EMAC_DESC_CTRL_3 */
#define RFD_RING_SIZE_BMSK                                       0xfff

/* EMAC_DESC_CTRL_4 */
#define RX_BUFFER_SIZE_BMSK                                     0xffff

/* EMAC_DESC_CTRL_6 */
#define RRD_RING_SIZE_BMSK                                       0xfff

/* EMAC_DESC_CTRL_9 */
#define TPD_RING_SIZE_BMSK                                      0xffff

/* EMAC_TXQ_CTRL_0 */
#define NUM_TXF_BURST_PREF_BMSK                             0xffff0000
#define NUM_TXF_BURST_PREF_SHFT                                     16
#define LS_8023_SP                                                0x80
#define TXQ_MODE                                                  0x40
#define TXQ_EN                                                    0x20
#define IP_OP_SP                                                  0x10
#define NUM_TPD_BURST_PREF_BMSK                                    0xf
#define NUM_TPD_BURST_PREF_SHFT                                      0

/* EMAC_TXQ_CTRL_1 */
#define JUMBO_TASK_OFFLOAD_THRESHOLD_BMSK                        0x7ff

/* EMAC_TXQ_CTRL_2 */
#define TXF_HWM_BMSK                                         0xfff0000
#define TXF_LWM_BMSK                                             0xfff

/* EMAC_RXQ_CTRL_0 */
#define RXQ_EN                                                 BIT(31)
#define CUT_THRU_EN                                            BIT(30)
#define RSS_HASH_EN                                            BIT(29)
#define NUM_RFD_BURST_PREF_BMSK                              0x3f00000
#define NUM_RFD_BURST_PREF_SHFT                                     20
#define IDT_TABLE_SIZE_BMSK                                    0x1ff00
#define IDT_TABLE_SIZE_SHFT                                          8
#define SP_IPV6                                                   0x80

/* EMAC_RXQ_CTRL_1 */
#define JUMBO_1KAH_BMSK                                         0xf000
#define JUMBO_1KAH_SHFT                                             12
#define RFD_PREF_LOW_TH                                           0x10
#define RFD_PREF_LOW_THRESHOLD_BMSK                              0xfc0
#define RFD_PREF_LOW_THRESHOLD_SHFT                                  6
#define RFD_PREF_UP_TH                                            0x10
#define RFD_PREF_UP_THRESHOLD_BMSK                                0x3f
#define RFD_PREF_UP_THRESHOLD_SHFT                                   0

/* EMAC_RXQ_CTRL_2 */
#define RXF_DOF_THRESFHOLD                                       0x1a0
#define RXF_DOF_THRESHOLD_BMSK                               0xfff0000
#define RXF_DOF_THRESHOLD_SHFT                                      16
#define RXF_UOF_THRESFHOLD                                        0xbe
#define RXF_UOF_THRESHOLD_BMSK                                   0xfff
#define RXF_UOF_THRESHOLD_SHFT                                       0

/* EMAC_RXQ_CTRL_3 */
#define RXD_TIMER_BMSK                                      0xffff0000
#define RXD_THRESHOLD_BMSK                                       0xfff
#define RXD_THRESHOLD_SHFT                                           0

/* EMAC_DMA_CTRL */
#define DMAW_DLY_CNT_BMSK                                      0xf0000
#define DMAW_DLY_CNT_SHFT                                           16
#define DMAR_DLY_CNT_BMSK                                       0xf800
#define DMAR_DLY_CNT_SHFT                                           11
#define DMAR_REQ_PRI                                             0x400
#define REGWRBLEN_BMSK                                           0x380
#define REGWRBLEN_SHFT                                               7
#define REGRDBLEN_BMSK                                            0x70
#define REGRDBLEN_SHFT                                               4
#define OUT_ORDER_MODE                                             0x4
#define ENH_ORDER_MODE                                             0x2
#define IN_ORDER_MODE                                              0x1

/* EMAC_MAILBOX_13 */
#define RFD3_PROC_IDX_BMSK                                   0xfff0000
#define RFD3_PROC_IDX_SHFT                                          16
#define RFD3_PROD_IDX_BMSK                                       0xfff
#define RFD3_PROD_IDX_SHFT                                           0

/* EMAC_MAILBOX_2 */
#define NTPD_CONS_IDX_BMSK                                  0xffff0000
#define NTPD_CONS_IDX_SHFT                                          16

/* EMAC_MAILBOX_3 */
#define RFD0_CONS_IDX_BMSK                                       0xfff
#define RFD0_CONS_IDX_SHFT                                           0

/* EMAC_MAILBOX_11 */
#define H3TPD_PROD_IDX_BMSK                                 0xffff0000
#define H3TPD_PROD_IDX_SHFT                                         16

/* EMAC_AXI_MAST_CTRL */
#define DATA_BYTE_SWAP                                             0x8
#define MAX_BOUND                                                  0x2
#define MAX_BTYPE                                                  0x1

/* EMAC_MAILBOX_12 */
#define H3TPD_CONS_IDX_BMSK                                 0xffff0000
#define H3TPD_CONS_IDX_SHFT                                         16

/* EMAC_MAILBOX_9 */
#define H2TPD_PROD_IDX_BMSK                                     0xffff
#define H2TPD_PROD_IDX_SHFT                                          0

/* EMAC_MAILBOX_10 */
#define H1TPD_CONS_IDX_BMSK                                 0xffff0000
#define H1TPD_CONS_IDX_SHFT                                         16
#define H2TPD_CONS_IDX_BMSK                                     0xffff
#define H2TPD_CONS_IDX_SHFT                                          0

/* EMAC_ATHR_HEADER_CTRL */
#define HEADER_CNT_EN                                              0x2
#define HEADER_ENABLE                                              0x1

/* EMAC_MAILBOX_0 */
#define RFD0_PROC_IDX_BMSK                                   0xfff0000
#define RFD0_PROC_IDX_SHFT                                          16
#define RFD0_PROD_IDX_BMSK                                       0xfff
#define RFD0_PROD_IDX_SHFT                                           0

/* EMAC_MAILBOX_5 */
#define RFD1_PROC_IDX_BMSK                                   0xfff0000
#define RFD1_PROC_IDX_SHFT                                          16
#define RFD1_PROD_IDX_BMSK                                       0xfff
#define RFD1_PROD_IDX_SHFT                                           0

/* EMAC_MISC_CTRL */
#define RX_UNCPL_INT_EN                                            0x1

/* EMAC_MAILBOX_7 */
#define RFD2_CONS_IDX_BMSK                                   0xfff0000
#define RFD2_CONS_IDX_SHFT                                          16
#define RFD1_CONS_IDX_BMSK                                       0xfff
#define RFD1_CONS_IDX_SHFT                                           0

/* EMAC_MAILBOX_8 */
#define RFD3_CONS_IDX_BMSK                                       0xfff
#define RFD3_CONS_IDX_SHFT                                           0

/* EMAC_MAILBOX_15 */
#define NTPD_PROD_IDX_BMSK                                      0xffff
#define NTPD_PROD_IDX_SHFT                                           0

/* EMAC_MAILBOX_16 */
#define H1TPD_PROD_IDX_BMSK                                     0xffff
#define H1TPD_PROD_IDX_SHFT                                          0

#define RXQ0_RSS_HSTYP_IPV6_TCP_EN                                0x20
#define RXQ0_RSS_HSTYP_IPV6_EN                                    0x10
#define RXQ0_RSS_HSTYP_IPV4_TCP_EN                                 0x8
#define RXQ0_RSS_HSTYP_IPV4_EN                                     0x4

/* EMAC_EMAC_WRAPPER_TX_TS_INX */
#define EMAC_WRAPPER_TX_TS_EMPTY                               BIT(31)
#define EMAC_WRAPPER_TX_TS_INX_BMSK                             0xffff

struct emac_skb_cb {
	u32           tpd_idx;
	unsigned long jiffies;
};

#define EMAC_SKB_CB(skb)	((struct emac_skb_cb *)(skb)->cb)
#define EMAC_RSS_IDT_SIZE	256
#define JUMBO_1KAH		0x4
#define RXD_TH			0x100
#define EMAC_TPD_LAST_FRAGMENT	0x80000000
#define EMAC_TPD_TSTAMP_SAVE	0x80000000

/* EMAC Errors in emac_rrd.word[3] */
#define EMAC_RRD_L4F		BIT(14)
#define EMAC_RRD_IPF		BIT(15)
#define EMAC_RRD_CRC		BIT(21)
#define EMAC_RRD_FAE		BIT(22)
#define EMAC_RRD_TRN		BIT(23)
#define EMAC_RRD_RNT		BIT(24)
#define EMAC_RRD_INC		BIT(25)
#define EMAC_RRD_FOV		BIT(29)
#define EMAC_RRD_LEN		BIT(30)

/* Error bits that will result in a received frame being discarded */
#define EMAC_RRD_ERROR (EMAC_RRD_IPF | EMAC_RRD_CRC | EMAC_RRD_FAE | \
			EMAC_RRD_TRN | EMAC_RRD_RNT | EMAC_RRD_INC | \
			EMAC_RRD_FOV | EMAC_RRD_LEN)
#define EMAC_RRD_STATS_DW_IDX 3

#define EMAC_RRD(RXQ, SIZE, IDX)	((RXQ)->rrd.v_addr + (SIZE * (IDX)))
#define EMAC_RFD(RXQ, SIZE, IDX)	((RXQ)->rfd.v_addr + (SIZE * (IDX)))
#define EMAC_TPD(TXQ, SIZE, IDX)	((TXQ)->tpd.v_addr + (SIZE * (IDX)))

#define GET_RFD_BUFFER(RXQ, IDX)	(&((RXQ)->rfd.rfbuff[(IDX)]))
#define GET_TPD_BUFFER(RTQ, IDX)	(&((RTQ)->tpd.tpbuff[(IDX)]))

#define EMAC_TX_POLL_HWTXTSTAMP_THRESHOLD	8

#define ISR_RX_PKT      (\
	RX_PKT_INT0     |\
	RX_PKT_INT1     |\
	RX_PKT_INT2     |\
	RX_PKT_INT3)

void emac_mac_multicast_addr_set(struct emac_adapter *adpt, u8 *addr)
{
	u32 crc32, bit, reg, mta;

	/* Calculate the CRC of the MAC address */
	crc32 = ether_crc(ETH_ALEN, addr);

	/* The HASH Table is an array of 2 32-bit registers. It is
	 * treated like an array of 64 bits (BitArray[hash_value]).
	 * Use the upper 6 bits of the above CRC as the hash value.
	 */
	reg = (crc32 >> 31) & 0x1;
	bit = (crc32 >> 26) & 0x1F;

	mta = readl(adpt->base + EMAC_HASH_TAB_REG0 + (reg << 2));
	mta |= BIT(bit);
	writel(mta, adpt->base + EMAC_HASH_TAB_REG0 + (reg << 2));
}

void emac_mac_multicast_addr_clear(struct emac_adapter *adpt)
{
	writel(0, adpt->base + EMAC_HASH_TAB_REG0);
	writel(0, adpt->base + EMAC_HASH_TAB_REG1);
}

/* definitions for RSS */
#define EMAC_RSS_KEY(_i, _type) \
		(EMAC_RSS_KEY0 + ((_i) * sizeof(_type)))
#define EMAC_RSS_TBL(_i, _type) \
		(EMAC_IDT_TABLE0 + ((_i) * sizeof(_type)))

/* Config MAC modes */
void emac_mac_mode_config(struct emac_adapter *adpt)
{
	struct net_device *netdev = adpt->netdev;
	u32 mac;

	mac = readl(adpt->base + EMAC_MAC_CTRL);
	mac &= ~(VLAN_STRIP | PROM_MODE | MULTI_ALL | MAC_LP_EN);

	if (netdev->features & NETIF_F_HW_VLAN_CTAG_RX)
		mac |= VLAN_STRIP;

	if (netdev->flags & IFF_PROMISC)
		mac |= PROM_MODE;

	if (netdev->flags & IFF_ALLMULTI)
		mac |= MULTI_ALL;

	writel(mac, adpt->base + EMAC_MAC_CTRL);
}

/* Config descriptor rings */
static void emac_mac_dma_rings_config(struct emac_adapter *adpt)
{
	/* TPD (Transmit Packet Descriptor) */
	writel(upper_32_bits(adpt->tx_q.tpd.dma_addr),
	       adpt->base + EMAC_DESC_CTRL_1);

	writel(lower_32_bits(adpt->tx_q.tpd.dma_addr),
	       adpt->base + EMAC_DESC_CTRL_8);

	writel(adpt->tx_q.tpd.count & TPD_RING_SIZE_BMSK,
	       adpt->base + EMAC_DESC_CTRL_9);

	/* RFD (Receive Free Descriptor) & RRD (Receive Return Descriptor) */
	writel(upper_32_bits(adpt->rx_q.rfd.dma_addr),
	       adpt->base + EMAC_DESC_CTRL_0);

	writel(lower_32_bits(adpt->rx_q.rfd.dma_addr),
	       adpt->base + EMAC_DESC_CTRL_2);
	writel(lower_32_bits(adpt->rx_q.rrd.dma_addr),
	       adpt->base + EMAC_DESC_CTRL_5);

	writel(adpt->rx_q.rfd.count & RFD_RING_SIZE_BMSK,
	       adpt->base + EMAC_DESC_CTRL_3);
	writel(adpt->rx_q.rrd.count & RRD_RING_SIZE_BMSK,
	       adpt->base + EMAC_DESC_CTRL_6);

	writel(adpt->rxbuf_size & RX_BUFFER_SIZE_BMSK,
	       adpt->base + EMAC_DESC_CTRL_4);

	writel(0, adpt->base + EMAC_DESC_CTRL_11);

	/* Load all of the base addresses above and ensure that triggering HW to
	 * read ring pointers is flushed
	 */
	writel(1, adpt->base + EMAC_INTER_SRAM_PART9);
}

/* Config transmit parameters */
static void emac_mac_tx_config(struct emac_adapter *adpt)
{
	u32 val;

	writel((EMAC_MAX_TX_OFFLOAD_THRESH >> 3) &
	       JUMBO_TASK_OFFLOAD_THRESHOLD_BMSK, adpt->base + EMAC_TXQ_CTRL_1);

	val = (adpt->tpd_burst << NUM_TPD_BURST_PREF_SHFT) &
	       NUM_TPD_BURST_PREF_BMSK;

	val |= TXQ_MODE | LS_8023_SP;
	val |= (0x0100 << NUM_TXF_BURST_PREF_SHFT) &
		NUM_TXF_BURST_PREF_BMSK;

	writel(val, adpt->base + EMAC_TXQ_CTRL_0);
	emac_reg_update32(adpt->base + EMAC_TXQ_CTRL_2,
			  (TXF_HWM_BMSK | TXF_LWM_BMSK), 0);
}

/* Config receive parameters */
static void emac_mac_rx_config(struct emac_adapter *adpt)
{
	u32 val;

	val = (adpt->rfd_burst << NUM_RFD_BURST_PREF_SHFT) &
	       NUM_RFD_BURST_PREF_BMSK;
	val |= (SP_IPV6 | CUT_THRU_EN);

	writel(val, adpt->base + EMAC_RXQ_CTRL_0);

	val = readl(adpt->base + EMAC_RXQ_CTRL_1);
	val &= ~(JUMBO_1KAH_BMSK | RFD_PREF_LOW_THRESHOLD_BMSK |
		 RFD_PREF_UP_THRESHOLD_BMSK);
	val |= (JUMBO_1KAH << JUMBO_1KAH_SHFT) |
		(RFD_PREF_LOW_TH << RFD_PREF_LOW_THRESHOLD_SHFT) |
		(RFD_PREF_UP_TH  << RFD_PREF_UP_THRESHOLD_SHFT);
	writel(val, adpt->base + EMAC_RXQ_CTRL_1);

	val = readl(adpt->base + EMAC_RXQ_CTRL_2);
	val &= ~(RXF_DOF_THRESHOLD_BMSK | RXF_UOF_THRESHOLD_BMSK);
	val |= (RXF_DOF_THRESFHOLD  << RXF_DOF_THRESHOLD_SHFT) |
		(RXF_UOF_THRESFHOLD << RXF_UOF_THRESHOLD_SHFT);
	writel(val, adpt->base + EMAC_RXQ_CTRL_2);

	val = readl(adpt->base + EMAC_RXQ_CTRL_3);
	val &= ~(RXD_TIMER_BMSK | RXD_THRESHOLD_BMSK);
	val |= RXD_TH << RXD_THRESHOLD_SHFT;
	writel(val, adpt->base + EMAC_RXQ_CTRL_3);
}

/* Config dma */
static void emac_mac_dma_config(struct emac_adapter *adpt)
{
	u32 dma_ctrl = DMAR_REQ_PRI;

	switch (adpt->dma_order) {
	case emac_dma_ord_in:
		dma_ctrl |= IN_ORDER_MODE;
		break;
	case emac_dma_ord_enh:
		dma_ctrl |= ENH_ORDER_MODE;
		break;
	case emac_dma_ord_out:
		dma_ctrl |= OUT_ORDER_MODE;
		break;
	default:
		break;
	}

	dma_ctrl |= (((u32)adpt->dmar_block) << REGRDBLEN_SHFT) &
						REGRDBLEN_BMSK;
	dma_ctrl |= (((u32)adpt->dmaw_block) << REGWRBLEN_SHFT) &
						REGWRBLEN_BMSK;
	dma_ctrl |= (((u32)adpt->dmar_dly_cnt) << DMAR_DLY_CNT_SHFT) &
						DMAR_DLY_CNT_BMSK;
	dma_ctrl |= (((u32)adpt->dmaw_dly_cnt) << DMAW_DLY_CNT_SHFT) &
						DMAW_DLY_CNT_BMSK;

	/* config DMA and ensure that configuration is flushed to HW */
	writel(dma_ctrl, adpt->base + EMAC_DMA_CTRL);
}

/* set MAC address */
static void emac_set_mac_address(struct emac_adapter *adpt, u8 *addr)
{
	u32 sta;

	/* for example: 00-A0-C6-11-22-33
	 * 0<-->C6112233, 1<-->00A0.
	 */

	/* low 32bit word */
	sta = (((u32)addr[2]) << 24) | (((u32)addr[3]) << 16) |
	      (((u32)addr[4]) << 8)  | (((u32)addr[5]));
	writel(sta, adpt->base + EMAC_MAC_STA_ADDR0);

	/* hight 32bit word */
	sta = (((u32)addr[0]) << 8) | (u32)addr[1];
	writel(sta, adpt->base + EMAC_MAC_STA_ADDR1);
}

static void emac_mac_config(struct emac_adapter *adpt)
{
	struct net_device *netdev = adpt->netdev;
	unsigned int max_frame;
	u32 val;

	emac_set_mac_address(adpt, netdev->dev_addr);

	max_frame = netdev->mtu + ETH_HLEN + ETH_FCS_LEN + VLAN_HLEN;
	adpt->rxbuf_size = netdev->mtu > EMAC_DEF_RX_BUF_SIZE ?
		ALIGN(max_frame, 8) : EMAC_DEF_RX_BUF_SIZE;

	emac_mac_dma_rings_config(adpt);

	writel(netdev->mtu + ETH_HLEN + VLAN_HLEN + ETH_FCS_LEN,
	       adpt->base + EMAC_MAX_FRAM_LEN_CTRL);

	emac_mac_tx_config(adpt);
	emac_mac_rx_config(adpt);
	emac_mac_dma_config(adpt);

	val = readl(adpt->base + EMAC_AXI_MAST_CTRL);
	val &= ~(DATA_BYTE_SWAP | MAX_BOUND);
	val |= MAX_BTYPE;
	writel(val, adpt->base + EMAC_AXI_MAST_CTRL);
	writel(0, adpt->base + EMAC_CLK_GATE_CTRL);
	writel(RX_UNCPL_INT_EN, adpt->base + EMAC_MISC_CTRL);
}

void emac_mac_reset(struct emac_adapter *adpt)
{
	emac_mac_stop(adpt);

	emac_reg_update32(adpt->base + EMAC_DMA_MAS_CTRL, 0, SOFT_RST);
	usleep_range(100, 150); /* reset may take up to 100usec */

	/* interrupt clear-on-read */
	emac_reg_update32(adpt->base + EMAC_DMA_MAS_CTRL, 0, INT_RD_CLR_EN);
}

static void emac_mac_start(struct emac_adapter *adpt)
{
	struct phy_device *phydev = adpt->phydev;
	u32 mac, csr1;

	/* enable tx queue */
	emac_reg_update32(adpt->base + EMAC_TXQ_CTRL_0, 0, TXQ_EN);

	/* enable rx queue */
	emac_reg_update32(adpt->base + EMAC_RXQ_CTRL_0, 0, RXQ_EN);

	/* enable mac control */
	mac = readl(adpt->base + EMAC_MAC_CTRL);
	csr1 = readl(adpt->csr + EMAC_EMAC_WRAPPER_CSR1);

	mac |= TXEN | RXEN;     /* enable RX/TX */

	/* Configure MAC flow control. If set to automatic, then match
	 * whatever the PHY does. Otherwise, enable or disable it, depending
	 * on what the user configured via ethtool.
	 */
	mac &= ~(RXFC | TXFC);

	if (adpt->automatic) {
		/* If it's set to automatic, then update our local values */
		adpt->rx_flow_control = phydev->pause;
		adpt->tx_flow_control = phydev->pause != phydev->asym_pause;
	}
	mac |= adpt->rx_flow_control ? RXFC : 0;
	mac |= adpt->tx_flow_control ? TXFC : 0;

	/* setup link speed */
	mac &= ~SPEED_MASK;
	if (phydev->speed == SPEED_1000) {
		mac |= SPEED(2);
		csr1 |= FREQ_MODE;
	} else {
		mac |= SPEED(1);
		csr1 &= ~FREQ_MODE;
	}

	if (phydev->duplex == DUPLEX_FULL)
		mac |= FULLD;
	else
		mac &= ~FULLD;

	/* other parameters */
	mac |= (CRCE | PCRCE);
	mac |= ((adpt->preamble << PRLEN_SHFT) & PRLEN_BMSK);
	mac |= BROAD_EN;
	mac |= FLCHK;
	mac &= ~RX_CHKSUM_EN;
	mac &= ~(HUGEN | VLAN_STRIP | TPAUSE | SIMR | HUGE | MULTI_ALL |
		 DEBUG_MODE | SINGLE_PAUSE_MODE);

	/* Enable single-pause-frame mode if requested.
	 *
	 * If enabled, the EMAC will send a single pause frame when the RX
	 * queue is full.  This normally leads to packet loss because
	 * the pause frame disables the remote MAC only for 33ms (the quanta),
	 * and then the remote MAC continues sending packets even though
	 * the RX queue is still full.
	 *
	 * If disabled, the EMAC sends a pause frame every 31ms until the RX
	 * queue is no longer full.  Normally, this is the preferred
	 * method of operation.  However, when the system is hung (e.g.
	 * cores are halted), the EMAC interrupt handler is never called
	 * and so the RX queue fills up quickly and stays full.  The resuling
	 * non-stop "flood" of pause frames sometimes has the effect of
	 * disabling nearby switches.  In some cases, other nearby switches
	 * are also affected, shutting down the entire network.
	 *
	 * The user can enable or disable single-pause-frame mode
	 * via ethtool.
	 */
	mac |= adpt->single_pause_mode ? SINGLE_PAUSE_MODE : 0;

	writel_relaxed(csr1, adpt->csr + EMAC_EMAC_WRAPPER_CSR1);

	writel_relaxed(mac, adpt->base + EMAC_MAC_CTRL);

	/* enable interrupt read clear, low power sleep mode and
	 * the irq moderators
	 */

	writel_relaxed(adpt->irq_mod, adpt->base + EMAC_IRQ_MOD_TIM_INIT);
	writel_relaxed(INT_RD_CLR_EN | LPW_MODE | IRQ_MODERATOR_EN |
			IRQ_MODERATOR2_EN, adpt->base + EMAC_DMA_MAS_CTRL);

	emac_mac_mode_config(adpt);

	emac_reg_update32(adpt->base + EMAC_ATHR_HEADER_CTRL,
			  (HEADER_ENABLE | HEADER_CNT_EN), 0);
}

void emac_mac_stop(struct emac_adapter *adpt)
{
	emac_reg_update32(adpt->base + EMAC_RXQ_CTRL_0, RXQ_EN, 0);
	emac_reg_update32(adpt->base + EMAC_TXQ_CTRL_0, TXQ_EN, 0);
	emac_reg_update32(adpt->base + EMAC_MAC_CTRL, TXEN | RXEN, 0);
	usleep_range(1000, 1050); /* stopping mac may take upto 1msec */
}

/* Free all descriptors of given transmit queue */
static void emac_tx_q_descs_free(struct emac_adapter *adpt)
{
	struct emac_tx_queue *tx_q = &adpt->tx_q;
	unsigned int i;
	size_t size;

	/* ring already cleared, nothing to do */
	if (!tx_q->tpd.tpbuff)
		return;

	for (i = 0; i < tx_q->tpd.count; i++) {
		struct emac_buffer *tpbuf = GET_TPD_BUFFER(tx_q, i);

		if (tpbuf->dma_addr) {
			dma_unmap_single(adpt->netdev->dev.parent,
					 tpbuf->dma_addr, tpbuf->length,
					 DMA_TO_DEVICE);
			tpbuf->dma_addr = 0;
		}
		if (tpbuf->skb) {
			dev_kfree_skb_any(tpbuf->skb);
			tpbuf->skb = NULL;
		}
	}

	size = sizeof(struct emac_buffer) * tx_q->tpd.count;
	memset(tx_q->tpd.tpbuff, 0, size);

	/* clear the descriptor ring */
	memset(tx_q->tpd.v_addr, 0, tx_q->tpd.size);

	tx_q->tpd.consume_idx = 0;
	tx_q->tpd.produce_idx = 0;
}

/* Free all descriptors of given receive queue */
static void emac_rx_q_free_descs(struct emac_adapter *adpt)
{
	struct device *dev = adpt->netdev->dev.parent;
	struct emac_rx_queue *rx_q = &adpt->rx_q;
	unsigned int i;
	size_t size;

	/* ring already cleared, nothing to do */
	if (!rx_q->rfd.rfbuff)
		return;

	for (i = 0; i < rx_q->rfd.count; i++) {
		struct emac_buffer *rfbuf = GET_RFD_BUFFER(rx_q, i);

		if (rfbuf->dma_addr) {
			dma_unmap_single(dev, rfbuf->dma_addr, rfbuf->length,
					 DMA_FROM_DEVICE);
			rfbuf->dma_addr = 0;
		}
		if (rfbuf->skb) {
			dev_kfree_skb(rfbuf->skb);
			rfbuf->skb = NULL;
		}
	}

	size =  sizeof(struct emac_buffer) * rx_q->rfd.count;
	memset(rx_q->rfd.rfbuff, 0, size);

	/* clear the descriptor rings */
	memset(rx_q->rrd.v_addr, 0, rx_q->rrd.size);
	rx_q->rrd.produce_idx = 0;
	rx_q->rrd.consume_idx = 0;

	memset(rx_q->rfd.v_addr, 0, rx_q->rfd.size);
	rx_q->rfd.produce_idx = 0;
	rx_q->rfd.consume_idx = 0;
}

/* Free all buffers associated with given transmit queue */
static void emac_tx_q_bufs_free(struct emac_adapter *adpt)
{
	struct emac_tx_queue *tx_q = &adpt->tx_q;

	emac_tx_q_descs_free(adpt);

	kfree(tx_q->tpd.tpbuff);
	tx_q->tpd.tpbuff = NULL;
	tx_q->tpd.v_addr = NULL;
	tx_q->tpd.dma_addr = 0;
	tx_q->tpd.size = 0;
}

/* Allocate TX descriptor ring for the given transmit queue */
static int emac_tx_q_desc_alloc(struct emac_adapter *adpt,
				struct emac_tx_queue *tx_q)
{
	struct emac_ring_header *ring_header = &adpt->ring_header;
	size_t size;

	size = sizeof(struct emac_buffer) * tx_q->tpd.count;
	tx_q->tpd.tpbuff = kzalloc(size, GFP_KERNEL);
	if (!tx_q->tpd.tpbuff)
		return -ENOMEM;

	tx_q->tpd.size = tx_q->tpd.count * (adpt->tpd_size * 4);
	tx_q->tpd.dma_addr = ring_header->dma_addr + ring_header->used;
	tx_q->tpd.v_addr = ring_header->v_addr + ring_header->used;
	ring_header->used += ALIGN(tx_q->tpd.size, 8);
	tx_q->tpd.produce_idx = 0;
	tx_q->tpd.consume_idx = 0;

	return 0;
}

/* Free all buffers associated with given transmit queue */
static void emac_rx_q_bufs_free(struct emac_adapter *adpt)
{
	struct emac_rx_queue *rx_q = &adpt->rx_q;

	emac_rx_q_free_descs(adpt);

	kfree(rx_q->rfd.rfbuff);
	rx_q->rfd.rfbuff   = NULL;

	rx_q->rfd.v_addr   = NULL;
	rx_q->rfd.dma_addr = 0;
	rx_q->rfd.size     = 0;

	rx_q->rrd.v_addr   = NULL;
	rx_q->rrd.dma_addr = 0;
	rx_q->rrd.size     = 0;
}

/* Allocate RX descriptor rings for the given receive queue */
static int emac_rx_descs_alloc(struct emac_adapter *adpt)
{
	struct emac_ring_header *ring_header = &adpt->ring_header;
	struct emac_rx_queue *rx_q = &adpt->rx_q;
	size_t size;

	size = sizeof(struct emac_buffer) * rx_q->rfd.count;
	rx_q->rfd.rfbuff = kzalloc(size, GFP_KERNEL);
	if (!rx_q->rfd.rfbuff)
		return -ENOMEM;

	rx_q->rrd.size = rx_q->rrd.count * (adpt->rrd_size * 4);
	rx_q->rfd.size = rx_q->rfd.count * (adpt->rfd_size * 4);

	rx_q->rrd.dma_addr = ring_header->dma_addr + ring_header->used;
	rx_q->rrd.v_addr   = ring_header->v_addr + ring_header->used;
	ring_header->used += ALIGN(rx_q->rrd.size, 8);

	rx_q->rfd.dma_addr = ring_header->dma_addr + ring_header->used;
	rx_q->rfd.v_addr   = ring_header->v_addr + ring_header->used;
	ring_header->used += ALIGN(rx_q->rfd.size, 8);

	rx_q->rrd.produce_idx = 0;
	rx_q->rrd.consume_idx = 0;

	rx_q->rfd.produce_idx = 0;
	rx_q->rfd.consume_idx = 0;

	return 0;
}

/* Allocate all TX and RX descriptor rings */
int emac_mac_rx_tx_rings_alloc_all(struct emac_adapter *adpt)
{
	struct emac_ring_header *ring_header = &adpt->ring_header;
	struct device *dev = adpt->netdev->dev.parent;
	unsigned int num_tx_descs = adpt->tx_desc_cnt;
	unsigned int num_rx_descs = adpt->rx_desc_cnt;
	int ret;

	adpt->tx_q.tpd.count = adpt->tx_desc_cnt;

	adpt->rx_q.rrd.count = adpt->rx_desc_cnt;
	adpt->rx_q.rfd.count = adpt->rx_desc_cnt;

	/* Ring DMA buffer. Each ring may need up to 8 bytes for alignment,
	 * hence the additional padding bytes are allocated.
	 */
	ring_header->size = num_tx_descs * (adpt->tpd_size * 4) +
			    num_rx_descs * (adpt->rfd_size * 4) +
			    num_rx_descs * (adpt->rrd_size * 4) +
			    8 + 2 * 8; /* 8 byte per one Tx and two Rx rings */

	ring_header->used = 0;
	ring_header->v_addr = dma_zalloc_coherent(dev, ring_header->size,
						 &ring_header->dma_addr,
						 GFP_KERNEL);
	if (!ring_header->v_addr)
		return -ENOMEM;

	ring_header->used = ALIGN(ring_header->dma_addr, 8) -
							ring_header->dma_addr;

	ret = emac_tx_q_desc_alloc(adpt, &adpt->tx_q);
	if (ret) {
		netdev_err(adpt->netdev, "error: Tx Queue alloc failed\n");
		goto err_alloc_tx;
	}

	ret = emac_rx_descs_alloc(adpt);
	if (ret) {
		netdev_err(adpt->netdev, "error: Rx Queue alloc failed\n");
		goto err_alloc_rx;
	}

	return 0;

err_alloc_rx:
	emac_tx_q_bufs_free(adpt);
err_alloc_tx:
	dma_free_coherent(dev, ring_header->size,
			  ring_header->v_addr, ring_header->dma_addr);

	ring_header->v_addr   = NULL;
	ring_header->dma_addr = 0;
	ring_header->size     = 0;
	ring_header->used     = 0;

	return ret;
}

/* Free all TX and RX descriptor rings */
void emac_mac_rx_tx_rings_free_all(struct emac_adapter *adpt)
{
	struct emac_ring_header *ring_header = &adpt->ring_header;
	struct device *dev = adpt->netdev->dev.parent;

	emac_tx_q_bufs_free(adpt);
	emac_rx_q_bufs_free(adpt);

	dma_free_coherent(dev, ring_header->size,
			  ring_header->v_addr, ring_header->dma_addr);

	ring_header->v_addr   = NULL;
	ring_header->dma_addr = 0;
	ring_header->size     = 0;
	ring_header->used     = 0;
}

/* Initialize descriptor rings */
static void emac_mac_rx_tx_ring_reset_all(struct emac_adapter *adpt)
{
	unsigned int i;

	adpt->tx_q.tpd.produce_idx = 0;
	adpt->tx_q.tpd.consume_idx = 0;
	for (i = 0; i < adpt->tx_q.tpd.count; i++)
		adpt->tx_q.tpd.tpbuff[i].dma_addr = 0;

	adpt->rx_q.rrd.produce_idx = 0;
	adpt->rx_q.rrd.consume_idx = 0;
	adpt->rx_q.rfd.produce_idx = 0;
	adpt->rx_q.rfd.consume_idx = 0;
	for (i = 0; i < adpt->rx_q.rfd.count; i++)
		adpt->rx_q.rfd.rfbuff[i].dma_addr = 0;
}

/* Produce new receive free descriptor */
static void emac_mac_rx_rfd_create(struct emac_adapter *adpt,
				   struct emac_rx_queue *rx_q,
				   dma_addr_t addr)
{
	u32 *hw_rfd = EMAC_RFD(rx_q, adpt->rfd_size, rx_q->rfd.produce_idx);

	*(hw_rfd++) = lower_32_bits(addr);
	*hw_rfd = upper_32_bits(addr);

	if (++rx_q->rfd.produce_idx == rx_q->rfd.count)
		rx_q->rfd.produce_idx = 0;
}

/* Fill up receive queue's RFD with preallocated receive buffers */
static void emac_mac_rx_descs_refill(struct emac_adapter *adpt,
				    struct emac_rx_queue *rx_q)
{
	struct emac_buffer *curr_rxbuf;
	struct emac_buffer *next_rxbuf;
	unsigned int count = 0;
	u32 next_produce_idx;

	next_produce_idx = rx_q->rfd.produce_idx + 1;
	if (next_produce_idx == rx_q->rfd.count)
		next_produce_idx = 0;

	curr_rxbuf = GET_RFD_BUFFER(rx_q, rx_q->rfd.produce_idx);
	next_rxbuf = GET_RFD_BUFFER(rx_q, next_produce_idx);

	/* this always has a blank rx_buffer*/
	while (!next_rxbuf->dma_addr) {
		struct sk_buff *skb;
		int ret;

		skb = netdev_alloc_skb_ip_align(adpt->netdev, adpt->rxbuf_size);
		if (!skb)
			break;

		curr_rxbuf->dma_addr =
			dma_map_single(adpt->netdev->dev.parent, skb->data,
				       adpt->rxbuf_size, DMA_FROM_DEVICE);

		ret = dma_mapping_error(adpt->netdev->dev.parent,
					curr_rxbuf->dma_addr);
		if (ret) {
			dev_kfree_skb(skb);
			break;
		}
		curr_rxbuf->skb = skb;
		curr_rxbuf->length = adpt->rxbuf_size;

		emac_mac_rx_rfd_create(adpt, rx_q, curr_rxbuf->dma_addr);
		next_produce_idx = rx_q->rfd.produce_idx + 1;
		if (next_produce_idx == rx_q->rfd.count)
			next_produce_idx = 0;

		curr_rxbuf = GET_RFD_BUFFER(rx_q, rx_q->rfd.produce_idx);
		next_rxbuf = GET_RFD_BUFFER(rx_q, next_produce_idx);
		count++;
	}

	if (count) {
		u32 prod_idx = (rx_q->rfd.produce_idx << rx_q->produce_shift) &
				rx_q->produce_mask;
		emac_reg_update32(adpt->base + rx_q->produce_reg,
				  rx_q->produce_mask, prod_idx);
	}
}

static void emac_adjust_link(struct net_device *netdev)
{
	struct emac_adapter *adpt = netdev_priv(netdev);
	struct emac_sgmii *sgmii = &adpt->phy;
	struct phy_device *phydev = netdev->phydev;

	if (phydev->link) {
		emac_mac_start(adpt);
		sgmii->link_up(adpt);
	} else {
		sgmii->link_down(adpt);
		emac_mac_stop(adpt);
	}

	phy_print_status(phydev);
}

/* Bringup the interface/HW */
int emac_mac_up(struct emac_adapter *adpt)
{
	struct net_device *netdev = adpt->netdev;
	int ret;

	emac_mac_rx_tx_ring_reset_all(adpt);
	emac_mac_config(adpt);
	emac_mac_rx_descs_refill(adpt, &adpt->rx_q);

	adpt->phydev->irq = PHY_POLL;
	ret = phy_connect_direct(netdev, adpt->phydev, emac_adjust_link,
				 PHY_INTERFACE_MODE_SGMII);
	if (ret) {
		netdev_err(adpt->netdev, "could not connect phy\n");
		return ret;
	}

	phy_attached_print(adpt->phydev, NULL);

	/* enable mac irq */
	writel((u32)~DIS_INT, adpt->base + EMAC_INT_STATUS);
	writel(adpt->irq.mask, adpt->base + EMAC_INT_MASK);

	phy_start(adpt->phydev);

	napi_enable(&adpt->rx_q.napi);
	netif_start_queue(netdev);

	return 0;
}

/* Bring down the interface/HW */
void emac_mac_down(struct emac_adapter *adpt)
{
	struct net_device *netdev = adpt->netdev;

	netif_stop_queue(netdev);
	napi_disable(&adpt->rx_q.napi);

	phy_stop(adpt->phydev);

	/* Interrupts must be disabled before the PHY is disconnected, to
	 * avoid a race condition where adjust_link is null when we get
	 * an interrupt.
	 */
	writel(DIS_INT, adpt->base + EMAC_INT_STATUS);
	writel(0, adpt->base + EMAC_INT_MASK);
	synchronize_irq(adpt->irq.irq);

	phy_disconnect(adpt->phydev);

	emac_mac_reset(adpt);

	emac_tx_q_descs_free(adpt);
	netdev_reset_queue(adpt->netdev);
	emac_rx_q_free_descs(adpt);
}

/* Consume next received packet descriptor */
static bool emac_rx_process_rrd(struct emac_adapter *adpt,
				struct emac_rx_queue *rx_q,
				struct emac_rrd *rrd)
{
	u32 *hw_rrd = EMAC_RRD(rx_q, adpt->rrd_size, rx_q->rrd.consume_idx);

	rrd->word[3] = *(hw_rrd + 3);

	if (!RRD_UPDT(rrd))
		return false;

	rrd->word[4] = 0;
	rrd->word[5] = 0;

	rrd->word[0] = *(hw_rrd++);
	rrd->word[1] = *(hw_rrd++);
	rrd->word[2] = *(hw_rrd++);

	if (unlikely(RRD_NOR(rrd) != 1)) {
		netdev_err(adpt->netdev,
			   "error: multi-RFD not support yet! nor:%lu\n",
			   RRD_NOR(rrd));
	}

	/* mark rrd as processed */
	RRD_UPDT_SET(rrd, 0);
	*hw_rrd = rrd->word[3];

	if (++rx_q->rrd.consume_idx == rx_q->rrd.count)
		rx_q->rrd.consume_idx = 0;

	return true;
}

/* Produce new transmit descriptor */
static void emac_tx_tpd_create(struct emac_adapter *adpt,
			       struct emac_tx_queue *tx_q, struct emac_tpd *tpd)
{
	u32 *hw_tpd;

	tx_q->tpd.last_produce_idx = tx_q->tpd.produce_idx;
	hw_tpd = EMAC_TPD(tx_q, adpt->tpd_size, tx_q->tpd.produce_idx);

	if (++tx_q->tpd.produce_idx == tx_q->tpd.count)
		tx_q->tpd.produce_idx = 0;

	*(hw_tpd++) = tpd->word[0];
	*(hw_tpd++) = tpd->word[1];
	*(hw_tpd++) = tpd->word[2];
	*hw_tpd = tpd->word[3];
}

/* Mark the last transmit descriptor as such (for the transmit packet) */
static void emac_tx_tpd_mark_last(struct emac_adapter *adpt,
				  struct emac_tx_queue *tx_q)
{
	u32 *hw_tpd =
		EMAC_TPD(tx_q, adpt->tpd_size, tx_q->tpd.last_produce_idx);
	u32 tmp_tpd;

	tmp_tpd = *(hw_tpd + 1);
	tmp_tpd |= EMAC_TPD_LAST_FRAGMENT;
	*(hw_tpd + 1) = tmp_tpd;
}

static void emac_rx_rfd_clean(struct emac_rx_queue *rx_q, struct emac_rrd *rrd)
{
	struct emac_buffer *rfbuf = rx_q->rfd.rfbuff;
	u32 consume_idx = RRD_SI(rrd);
	unsigned int i;

	for (i = 0; i < RRD_NOR(rrd); i++) {
		rfbuf[consume_idx].skb = NULL;
		if (++consume_idx == rx_q->rfd.count)
			consume_idx = 0;
	}

	rx_q->rfd.consume_idx = consume_idx;
	rx_q->rfd.process_idx = consume_idx;
}

/* Push the received skb to upper layers */
static void emac_receive_skb(struct emac_rx_queue *rx_q,
			     struct sk_buff *skb,
			     u16 vlan_tag, bool vlan_flag)
{
	if (vlan_flag) {
		u16 vlan;

		EMAC_TAG_TO_VLAN(vlan_tag, vlan);
		__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q), vlan);
	}

	napi_gro_receive(&rx_q->napi, skb);
}

/* Process receive event */
void emac_mac_rx_process(struct emac_adapter *adpt, struct emac_rx_queue *rx_q,
			 int *num_pkts, int max_pkts)
{
	u32 proc_idx, hw_consume_idx, num_consume_pkts;
	struct net_device *netdev  = adpt->netdev;
	struct emac_buffer *rfbuf;
	unsigned int count = 0;
	struct emac_rrd rrd;
	struct sk_buff *skb;
	u32 reg;

	reg = readl_relaxed(adpt->base + rx_q->consume_reg);

	hw_consume_idx = (reg & rx_q->consume_mask) >> rx_q->consume_shift;
	num_consume_pkts = (hw_consume_idx >= rx_q->rrd.consume_idx) ?
		(hw_consume_idx -  rx_q->rrd.consume_idx) :
		(hw_consume_idx + rx_q->rrd.count - rx_q->rrd.consume_idx);

	do {
		if (!num_consume_pkts)
			break;

		if (!emac_rx_process_rrd(adpt, rx_q, &rrd))
			break;

		if (likely(RRD_NOR(&rrd) == 1)) {
			/* good receive */
			rfbuf = GET_RFD_BUFFER(rx_q, RRD_SI(&rrd));
			dma_unmap_single(adpt->netdev->dev.parent,
					 rfbuf->dma_addr, rfbuf->length,
					 DMA_FROM_DEVICE);
			rfbuf->dma_addr = 0;
			skb = rfbuf->skb;
		} else {
			netdev_err(adpt->netdev,
				   "error: multi-RFD not support yet!\n");
			break;
		}
		emac_rx_rfd_clean(rx_q, &rrd);
		num_consume_pkts--;
		count++;

		/* Due to a HW issue in L4 check sum detection (UDP/TCP frags
		 * with DF set are marked as error), drop packets based on the
		 * error mask rather than the summary bit (ignoring L4F errors)
		 */
		if (rrd.word[EMAC_RRD_STATS_DW_IDX] & EMAC_RRD_ERROR) {
			netif_dbg(adpt, rx_status, adpt->netdev,
				  "Drop error packet[RRD: 0x%x:0x%x:0x%x:0x%x]\n",
				  rrd.word[0], rrd.word[1],
				  rrd.word[2], rrd.word[3]);

			dev_kfree_skb(skb);
			continue;
		}

		skb_put(skb, RRD_PKT_SIZE(&rrd) - ETH_FCS_LEN);
		skb->dev = netdev;
		skb->protocol = eth_type_trans(skb, skb->dev);
		if (netdev->features & NETIF_F_RXCSUM)
			skb->ip_summed = RRD_L4F(&rrd) ?
					  CHECKSUM_NONE : CHECKSUM_UNNECESSARY;
		else
			skb_checksum_none_assert(skb);

		emac_receive_skb(rx_q, skb, (u16)RRD_CVALN_TAG(&rrd),
				 (bool)RRD_CVTAG(&rrd));

		(*num_pkts)++;
	} while (*num_pkts < max_pkts);

	if (count) {
		proc_idx = (rx_q->rfd.process_idx << rx_q->process_shft) &
				rx_q->process_mask;
		emac_reg_update32(adpt->base + rx_q->process_reg,
				  rx_q->process_mask, proc_idx);
		emac_mac_rx_descs_refill(adpt, rx_q);
	}
}

/* get the number of free transmit descriptors */
static unsigned int emac_tpd_num_free_descs(struct emac_tx_queue *tx_q)
{
	u32 produce_idx = tx_q->tpd.produce_idx;
	u32 consume_idx = tx_q->tpd.consume_idx;

	return (consume_idx > produce_idx) ?
		(consume_idx - produce_idx - 1) :
		(tx_q->tpd.count + consume_idx - produce_idx - 1);
}

/* Process transmit event */
void emac_mac_tx_process(struct emac_adapter *adpt, struct emac_tx_queue *tx_q)
{
	u32 reg = readl_relaxed(adpt->base + tx_q->consume_reg);
	u32 hw_consume_idx, pkts_compl = 0, bytes_compl = 0;
	struct emac_buffer *tpbuf;

	hw_consume_idx = (reg & tx_q->consume_mask) >> tx_q->consume_shift;

	while (tx_q->tpd.consume_idx != hw_consume_idx) {
		tpbuf = GET_TPD_BUFFER(tx_q, tx_q->tpd.consume_idx);
		if (tpbuf->dma_addr) {
			dma_unmap_page(adpt->netdev->dev.parent,
				       tpbuf->dma_addr, tpbuf->length,
				       DMA_TO_DEVICE);
			tpbuf->dma_addr = 0;
		}

		if (tpbuf->skb) {
			pkts_compl++;
			bytes_compl += tpbuf->skb->len;
			dev_kfree_skb_irq(tpbuf->skb);
			tpbuf->skb = NULL;
		}

		if (++tx_q->tpd.consume_idx == tx_q->tpd.count)
			tx_q->tpd.consume_idx = 0;
	}

	netdev_completed_queue(adpt->netdev, pkts_compl, bytes_compl);

	if (netif_queue_stopped(adpt->netdev))
		if (emac_tpd_num_free_descs(tx_q) > (MAX_SKB_FRAGS + 1))
			netif_wake_queue(adpt->netdev);
}

/* Initialize all queue data structures */
void emac_mac_rx_tx_ring_init_all(struct platform_device *pdev,
				  struct emac_adapter *adpt)
{
	adpt->rx_q.netdev = adpt->netdev;

	adpt->rx_q.produce_reg  = EMAC_MAILBOX_0;
	adpt->rx_q.produce_mask = RFD0_PROD_IDX_BMSK;
	adpt->rx_q.produce_shift = RFD0_PROD_IDX_SHFT;

	adpt->rx_q.process_reg  = EMAC_MAILBOX_0;
	adpt->rx_q.process_mask = RFD0_PROC_IDX_BMSK;
	adpt->rx_q.process_shft = RFD0_PROC_IDX_SHFT;

	adpt->rx_q.consume_reg  = EMAC_MAILBOX_3;
	adpt->rx_q.consume_mask = RFD0_CONS_IDX_BMSK;
	adpt->rx_q.consume_shift = RFD0_CONS_IDX_SHFT;

	adpt->rx_q.irq          = &adpt->irq;
	adpt->rx_q.intr         = adpt->irq.mask & ISR_RX_PKT;

	adpt->tx_q.produce_reg  = EMAC_MAILBOX_15;
	adpt->tx_q.produce_mask = NTPD_PROD_IDX_BMSK;
	adpt->tx_q.produce_shift = NTPD_PROD_IDX_SHFT;

	adpt->tx_q.consume_reg  = EMAC_MAILBOX_2;
	adpt->tx_q.consume_mask = NTPD_CONS_IDX_BMSK;
	adpt->tx_q.consume_shift = NTPD_CONS_IDX_SHFT;
}

/* Fill up transmit descriptors with TSO and Checksum offload information */
static int emac_tso_csum(struct emac_adapter *adpt,
			 struct emac_tx_queue *tx_q,
			 struct sk_buff *skb,
			 struct emac_tpd *tpd)
{
	unsigned int hdr_len;
	int ret;

	if (skb_is_gso(skb)) {
		if (skb_header_cloned(skb)) {
			ret = pskb_expand_head(skb, 0, 0, GFP_ATOMIC);
			if (unlikely(ret))
				return ret;
		}

		if (skb->protocol == htons(ETH_P_IP)) {
			u32 pkt_len = ((unsigned char *)ip_hdr(skb) - skb->data)
				       + ntohs(ip_hdr(skb)->tot_len);
			if (skb->len > pkt_len)
				pskb_trim(skb, pkt_len);
		}

		hdr_len = skb_transport_offset(skb) + tcp_hdrlen(skb);
		if (unlikely(skb->len == hdr_len)) {
			/* we only need to do csum */
			netif_warn(adpt, tx_err, adpt->netdev,
				   "tso not needed for packet with 0 data\n");
			goto do_csum;
		}

		if (skb_shinfo(skb)->gso_type & SKB_GSO_TCPV4) {
			ip_hdr(skb)->check = 0;
			tcp_hdr(skb)->check =
				~csum_tcpudp_magic(ip_hdr(skb)->saddr,
						   ip_hdr(skb)->daddr,
						   0, IPPROTO_TCP, 0);
			TPD_IPV4_SET(tpd, 1);
		}

		if (skb_shinfo(skb)->gso_type & SKB_GSO_TCPV6) {
			/* ipv6 tso need an extra tpd */
			struct emac_tpd extra_tpd;

			memset(tpd, 0, sizeof(*tpd));
			memset(&extra_tpd, 0, sizeof(extra_tpd));

			ipv6_hdr(skb)->payload_len = 0;
			tcp_hdr(skb)->check =
				~csum_ipv6_magic(&ipv6_hdr(skb)->saddr,
						 &ipv6_hdr(skb)->daddr,
						 0, IPPROTO_TCP, 0);
			TPD_PKT_LEN_SET(&extra_tpd, skb->len);
			TPD_LSO_SET(&extra_tpd, 1);
			TPD_LSOV_SET(&extra_tpd, 1);
			emac_tx_tpd_create(adpt, tx_q, &extra_tpd);
			TPD_LSOV_SET(tpd, 1);
		}

		TPD_LSO_SET(tpd, 1);
		TPD_TCPHDR_OFFSET_SET(tpd, skb_transport_offset(skb));
		TPD_MSS_SET(tpd, skb_shinfo(skb)->gso_size);
		return 0;
	}

do_csum:
	if (likely(skb->ip_summed == CHECKSUM_PARTIAL)) {
		unsigned int css, cso;

		cso = skb_transport_offset(skb);
		if (unlikely(cso & 0x1)) {
			netdev_err(adpt->netdev,
				   "error: payload offset should be even\n");
			return -EINVAL;
		}
		css = cso + skb->csum_offset;

		TPD_PAYLOAD_OFFSET_SET(tpd, cso >> 1);
		TPD_CXSUM_OFFSET_SET(tpd, css >> 1);
		TPD_CSX_SET(tpd, 1);
	}

	return 0;
}

/* Fill up transmit descriptors */
static void emac_tx_fill_tpd(struct emac_adapter *adpt,
			     struct emac_tx_queue *tx_q, struct sk_buff *skb,
			     struct emac_tpd *tpd)
{
	unsigned int nr_frags = skb_shinfo(skb)->nr_frags;
	unsigned int first = tx_q->tpd.produce_idx;
	unsigned int len = skb_headlen(skb);
	struct emac_buffer *tpbuf = NULL;
	unsigned int mapped_len = 0;
	unsigned int i;
	int count = 0;
	int ret;

	/* if Large Segment Offload is (in TCP Segmentation Offload struct) */
	if (TPD_LSO(tpd)) {
		mapped_len = skb_transport_offset(skb) + tcp_hdrlen(skb);

		tpbuf = GET_TPD_BUFFER(tx_q, tx_q->tpd.produce_idx);
		tpbuf->length = mapped_len;
		tpbuf->dma_addr = dma_map_page(adpt->netdev->dev.parent,
					       virt_to_page(skb->data),
					       offset_in_page(skb->data),
					       tpbuf->length,
					       DMA_TO_DEVICE);
		ret = dma_mapping_error(adpt->netdev->dev.parent,
					tpbuf->dma_addr);
		if (ret)
			goto error;

		TPD_BUFFER_ADDR_L_SET(tpd, lower_32_bits(tpbuf->dma_addr));
		TPD_BUFFER_ADDR_H_SET(tpd, upper_32_bits(tpbuf->dma_addr));
		TPD_BUF_LEN_SET(tpd, tpbuf->length);
		emac_tx_tpd_create(adpt, tx_q, tpd);
		count++;
	}

	if (mapped_len < len) {
		tpbuf = GET_TPD_BUFFER(tx_q, tx_q->tpd.produce_idx);
		tpbuf->length = len - mapped_len;
		tpbuf->dma_addr = dma_map_page(adpt->netdev->dev.parent,
					       virt_to_page(skb->data +
							    mapped_len),
					       offset_in_page(skb->data +
							      mapped_len),
					       tpbuf->length, DMA_TO_DEVICE);
		ret = dma_mapping_error(adpt->netdev->dev.parent,
					tpbuf->dma_addr);
		if (ret)
			goto error;

		TPD_BUFFER_ADDR_L_SET(tpd, lower_32_bits(tpbuf->dma_addr));
		TPD_BUFFER_ADDR_H_SET(tpd, upper_32_bits(tpbuf->dma_addr));
		TPD_BUF_LEN_SET(tpd, tpbuf->length);
		emac_tx_tpd_create(adpt, tx_q, tpd);
		count++;
	}

	for (i = 0; i < nr_frags; i++) {
		struct skb_frag_struct *frag;

		frag = &skb_shinfo(skb)->frags[i];

		tpbuf = GET_TPD_BUFFER(tx_q, tx_q->tpd.produce_idx);
		tpbuf->length = frag->size;
		tpbuf->dma_addr = dma_map_page(adpt->netdev->dev.parent,
					       frag->page.p, frag->page_offset,
					       tpbuf->length, DMA_TO_DEVICE);
		ret = dma_mapping_error(adpt->netdev->dev.parent,
					tpbuf->dma_addr);
		if (ret)
			goto error;

		TPD_BUFFER_ADDR_L_SET(tpd, lower_32_bits(tpbuf->dma_addr));
		TPD_BUFFER_ADDR_H_SET(tpd, upper_32_bits(tpbuf->dma_addr));
		TPD_BUF_LEN_SET(tpd, tpbuf->length);
		emac_tx_tpd_create(adpt, tx_q, tpd);
		count++;
	}

	/* The last tpd */
	wmb();
	emac_tx_tpd_mark_last(adpt, tx_q);

	/* The last buffer info contain the skb address,
	 * so it will be freed after unmap
	 */
	tpbuf->skb = skb;

	return;

error:
	/* One of the memory mappings failed, so undo everything */
	tx_q->tpd.produce_idx = first;

	while (count--) {
		tpbuf = GET_TPD_BUFFER(tx_q, first);
		dma_unmap_page(adpt->netdev->dev.parent, tpbuf->dma_addr,
			       tpbuf->length, DMA_TO_DEVICE);
		tpbuf->dma_addr = 0;
		tpbuf->length = 0;

		if (++first == tx_q->tpd.count)
			first = 0;
	}

	dev_kfree_skb(skb);
}

/* Transmit the packet using specified transmit queue */
int emac_mac_tx_buf_send(struct emac_adapter *adpt, struct emac_tx_queue *tx_q,
			 struct sk_buff *skb)
{
	struct emac_tpd tpd;
	u32 prod_idx;

	memset(&tpd, 0, sizeof(tpd));

	if (emac_tso_csum(adpt, tx_q, skb, &tpd) != 0) {
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	if (skb_vlan_tag_present(skb)) {
		u16 tag;

		EMAC_VLAN_TO_TAG(skb_vlan_tag_get(skb), tag);
		TPD_CVLAN_TAG_SET(&tpd, tag);
		TPD_INSTC_SET(&tpd, 1);
	}

	if (skb_network_offset(skb) != ETH_HLEN)
		TPD_TYP_SET(&tpd, 1);

	emac_tx_fill_tpd(adpt, tx_q, skb, &tpd);

	netdev_sent_queue(adpt->netdev, skb->len);

	/* Make sure the are enough free descriptors to hold one
	 * maximum-sized SKB.  We need one desc for each fragment,
	 * one for the checksum (emac_tso_csum), one for TSO, and
	 * and one for the SKB header.
	 */
	if (emac_tpd_num_free_descs(tx_q) < (MAX_SKB_FRAGS + 3))
		netif_stop_queue(adpt->netdev);

	/* update produce idx */
	prod_idx = (tx_q->tpd.produce_idx << tx_q->produce_shift) &
		    tx_q->produce_mask;
	emac_reg_update32(adpt->base + tx_q->produce_reg,
			  tx_q->produce_mask, prod_idx);

	return NETDEV_TX_OK;
}
