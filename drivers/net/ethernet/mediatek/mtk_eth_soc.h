/*   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   Copyright (C) 2009-2016 John Crispin <blogic@openwrt.org>
 *   Copyright (C) 2009-2016 Felix Fietkau <nbd@openwrt.org>
 *   Copyright (C) 2013-2016 Michael Lee <igvtee@gmail.com>
 */

#ifndef MTK_ETH_H
#define MTK_ETH_H

#include <linux/refcount.h>

#define MTK_QDMA_PAGE_SIZE	2048
#define	MTK_MAX_RX_LENGTH	1536
#define MTK_TX_DMA_BUF_LEN	0x3fff
#define MTK_DMA_SIZE		256
#define MTK_NAPI_WEIGHT		64
#define MTK_MAC_COUNT		2
#define MTK_RX_ETH_HLEN		(VLAN_ETH_HLEN + VLAN_HLEN + ETH_FCS_LEN)
#define MTK_RX_HLEN		(NET_SKB_PAD + MTK_RX_ETH_HLEN + NET_IP_ALIGN)
#define MTK_DMA_DUMMY_DESC	0xffffffff
#define MTK_DEFAULT_MSG_ENABLE	(NETIF_MSG_DRV | \
				 NETIF_MSG_PROBE | \
				 NETIF_MSG_LINK | \
				 NETIF_MSG_TIMER | \
				 NETIF_MSG_IFDOWN | \
				 NETIF_MSG_IFUP | \
				 NETIF_MSG_RX_ERR | \
				 NETIF_MSG_TX_ERR)
#define MTK_HW_FEATURES		(NETIF_F_IP_CSUM | \
				 NETIF_F_RXCSUM | \
				 NETIF_F_HW_VLAN_CTAG_TX | \
				 NETIF_F_HW_VLAN_CTAG_RX | \
				 NETIF_F_SG | NETIF_F_TSO | \
				 NETIF_F_TSO6 | \
				 NETIF_F_IPV6_CSUM)
#define NEXT_RX_DESP_IDX(X, Y)	(((X) + 1) & ((Y) - 1))

#define MTK_MAX_RX_RING_NUM	4
#define MTK_HW_LRO_DMA_SIZE	8

#define	MTK_MAX_LRO_RX_LENGTH		(4096 * 3)
#define	MTK_MAX_LRO_IP_CNT		2
#define	MTK_HW_LRO_TIMER_UNIT		1	/* 20 us */
#define	MTK_HW_LRO_REFRESH_TIME		50000	/* 1 sec. */
#define	MTK_HW_LRO_AGG_TIME		10	/* 200us */
#define	MTK_HW_LRO_AGE_TIME		50	/* 1ms */
#define	MTK_HW_LRO_MAX_AGG_CNT		64
#define	MTK_HW_LRO_BW_THRE		3000
#define	MTK_HW_LRO_REPLACE_DELTA	1000
#define	MTK_HW_LRO_SDL_REMAIN_ROOM	1522

/* Frame Engine Global Reset Register */
#define MTK_RST_GL		0x04
#define RST_GL_PSE		BIT(0)

/* Frame Engine Interrupt Status Register */
#define MTK_INT_STATUS2		0x08
#define MTK_GDM1_AF		BIT(28)
#define MTK_GDM2_AF		BIT(29)

/* PDMA HW LRO Alter Flow Timer Register */
#define MTK_PDMA_LRO_ALT_REFRESH_TIMER	0x1c

/* Frame Engine Interrupt Grouping Register */
#define MTK_FE_INT_GRP		0x20

/* CDMP Ingress Control Register */
#define MTK_CDMQ_IG_CTRL	0x1400
#define MTK_CDMQ_STAG_EN	BIT(0)

/* CDMP Exgress Control Register */
#define MTK_CDMP_EG_CTRL	0x404

/* GDM Exgress Control Register */
#define MTK_GDMA_FWD_CFG(x)	(0x500 + (x * 0x1000))
#define MTK_GDMA_ICS_EN		BIT(22)
#define MTK_GDMA_TCS_EN		BIT(21)
#define MTK_GDMA_UCS_EN		BIT(20)

/* Unicast Filter MAC Address Register - Low */
#define MTK_GDMA_MAC_ADRL(x)	(0x508 + (x * 0x1000))

/* Unicast Filter MAC Address Register - High */
#define MTK_GDMA_MAC_ADRH(x)	(0x50C + (x * 0x1000))

/* PDMA RX Base Pointer Register */
#define MTK_PRX_BASE_PTR0	0x900
#define MTK_PRX_BASE_PTR_CFG(x)	(MTK_PRX_BASE_PTR0 + (x * 0x10))

/* PDMA RX Maximum Count Register */
#define MTK_PRX_MAX_CNT0	0x904
#define MTK_PRX_MAX_CNT_CFG(x)	(MTK_PRX_MAX_CNT0 + (x * 0x10))

/* PDMA RX CPU Pointer Register */
#define MTK_PRX_CRX_IDX0	0x908
#define MTK_PRX_CRX_IDX_CFG(x)	(MTK_PRX_CRX_IDX0 + (x * 0x10))

/* PDMA HW LRO Control Registers */
#define MTK_PDMA_LRO_CTRL_DW0	0x980
#define MTK_LRO_EN			BIT(0)
#define MTK_L3_CKS_UPD_EN		BIT(7)
#define MTK_LRO_ALT_PKT_CNT_MODE	BIT(21)
#define MTK_LRO_RING_RELINQUISH_REQ	(0x7 << 26)
#define MTK_LRO_RING_RELINQUISH_DONE	(0x7 << 29)

#define MTK_PDMA_LRO_CTRL_DW1	0x984
#define MTK_PDMA_LRO_CTRL_DW2	0x988
#define MTK_PDMA_LRO_CTRL_DW3	0x98c
#define MTK_ADMA_MODE		BIT(15)
#define MTK_LRO_MIN_RXD_SDL	(MTK_HW_LRO_SDL_REMAIN_ROOM << 16)

/* PDMA Global Configuration Register */
#define MTK_PDMA_GLO_CFG	0xa04
#define MTK_MULTI_EN		BIT(10)

/* PDMA Reset Index Register */
#define MTK_PDMA_RST_IDX	0xa08
#define MTK_PST_DRX_IDX0	BIT(16)
#define MTK_PST_DRX_IDX_CFG(x)	(MTK_PST_DRX_IDX0 << (x))

/* PDMA Delay Interrupt Register */
#define MTK_PDMA_DELAY_INT		0xa0c
#define MTK_PDMA_DELAY_RX_EN		BIT(15)
#define MTK_PDMA_DELAY_RX_PINT		4
#define MTK_PDMA_DELAY_RX_PINT_SHIFT	8
#define MTK_PDMA_DELAY_RX_PTIME		4
#define MTK_PDMA_DELAY_RX_DELAY		\
	(MTK_PDMA_DELAY_RX_EN | MTK_PDMA_DELAY_RX_PTIME | \
	(MTK_PDMA_DELAY_RX_PINT << MTK_PDMA_DELAY_RX_PINT_SHIFT))

/* PDMA Interrupt Status Register */
#define MTK_PDMA_INT_STATUS	0xa20

/* PDMA Interrupt Mask Register */
#define MTK_PDMA_INT_MASK	0xa28

/* PDMA HW LRO Alter Flow Delta Register */
#define MTK_PDMA_LRO_ALT_SCORE_DELTA	0xa4c

/* PDMA Interrupt grouping registers */
#define MTK_PDMA_INT_GRP1	0xa50
#define MTK_PDMA_INT_GRP2	0xa54

/* PDMA HW LRO IP Setting Registers */
#define MTK_LRO_RX_RING0_DIP_DW0	0xb04
#define MTK_LRO_DIP_DW0_CFG(x)		(MTK_LRO_RX_RING0_DIP_DW0 + (x * 0x40))
#define MTK_RING_MYIP_VLD		BIT(9)

/* PDMA HW LRO Ring Control Registers */
#define MTK_LRO_RX_RING0_CTRL_DW1	0xb28
#define MTK_LRO_RX_RING0_CTRL_DW2	0xb2c
#define MTK_LRO_RX_RING0_CTRL_DW3	0xb30
#define MTK_LRO_CTRL_DW1_CFG(x)		(MTK_LRO_RX_RING0_CTRL_DW1 + (x * 0x40))
#define MTK_LRO_CTRL_DW2_CFG(x)		(MTK_LRO_RX_RING0_CTRL_DW2 + (x * 0x40))
#define MTK_LRO_CTRL_DW3_CFG(x)		(MTK_LRO_RX_RING0_CTRL_DW3 + (x * 0x40))
#define MTK_RING_AGE_TIME_L		((MTK_HW_LRO_AGE_TIME & 0x3ff) << 22)
#define MTK_RING_AGE_TIME_H		((MTK_HW_LRO_AGE_TIME >> 10) & 0x3f)
#define MTK_RING_AUTO_LERAN_MODE	(3 << 6)
#define MTK_RING_VLD			BIT(8)
#define MTK_RING_MAX_AGG_TIME		((MTK_HW_LRO_AGG_TIME & 0xffff) << 10)
#define MTK_RING_MAX_AGG_CNT_L		((MTK_HW_LRO_MAX_AGG_CNT & 0x3f) << 26)
#define MTK_RING_MAX_AGG_CNT_H		((MTK_HW_LRO_MAX_AGG_CNT >> 6) & 0x3)

/* QDMA TX Queue Configuration Registers */
#define MTK_QTX_CFG(x)		(0x1800 + (x * 0x10))
#define QDMA_RES_THRES		4

/* QDMA TX Queue Scheduler Registers */
#define MTK_QTX_SCH(x)		(0x1804 + (x * 0x10))

/* QDMA RX Base Pointer Register */
#define MTK_QRX_BASE_PTR0	0x1900

/* QDMA RX Maximum Count Register */
#define MTK_QRX_MAX_CNT0	0x1904

/* QDMA RX CPU Pointer Register */
#define MTK_QRX_CRX_IDX0	0x1908

/* QDMA RX DMA Pointer Register */
#define MTK_QRX_DRX_IDX0	0x190C

/* QDMA Global Configuration Register */
#define MTK_QDMA_GLO_CFG	0x1A04
#define MTK_RX_2B_OFFSET	BIT(31)
#define MTK_RX_BT_32DWORDS	(3 << 11)
#define MTK_NDP_CO_PRO		BIT(10)
#define MTK_TX_WB_DDONE		BIT(6)
#define MTK_DMA_SIZE_16DWORDS	(2 << 4)
#define MTK_RX_DMA_BUSY		BIT(3)
#define MTK_TX_DMA_BUSY		BIT(1)
#define MTK_RX_DMA_EN		BIT(2)
#define MTK_TX_DMA_EN		BIT(0)
#define MTK_DMA_BUSY_TIMEOUT	HZ

/* QDMA Reset Index Register */
#define MTK_QDMA_RST_IDX	0x1A08

/* QDMA Delay Interrupt Register */
#define MTK_QDMA_DELAY_INT	0x1A0C

/* QDMA Flow Control Register */
#define MTK_QDMA_FC_THRES	0x1A10
#define FC_THRES_DROP_MODE	BIT(20)
#define FC_THRES_DROP_EN	(7 << 16)
#define FC_THRES_MIN		0x4444

/* QDMA Interrupt Status Register */
#define MTK_QMTK_INT_STATUS	0x1A18
#define MTK_RX_DONE_DLY		BIT(30)
#define MTK_RX_DONE_INT3	BIT(19)
#define MTK_RX_DONE_INT2	BIT(18)
#define MTK_RX_DONE_INT1	BIT(17)
#define MTK_RX_DONE_INT0	BIT(16)
#define MTK_TX_DONE_INT3	BIT(3)
#define MTK_TX_DONE_INT2	BIT(2)
#define MTK_TX_DONE_INT1	BIT(1)
#define MTK_TX_DONE_INT0	BIT(0)
#define MTK_RX_DONE_INT		MTK_RX_DONE_DLY
#define MTK_TX_DONE_INT		(MTK_TX_DONE_INT0 | MTK_TX_DONE_INT1 | \
				 MTK_TX_DONE_INT2 | MTK_TX_DONE_INT3)

/* QDMA Interrupt grouping registers */
#define MTK_QDMA_INT_GRP1	0x1a20
#define MTK_QDMA_INT_GRP2	0x1a24
#define MTK_RLS_DONE_INT	BIT(0)

/* QDMA Interrupt Status Register */
#define MTK_QDMA_INT_MASK	0x1A1C

/* QDMA Interrupt Mask Register */
#define MTK_QDMA_HRED2		0x1A44

/* QDMA TX Forward CPU Pointer Register */
#define MTK_QTX_CTX_PTR		0x1B00

/* QDMA TX Forward DMA Pointer Register */
#define MTK_QTX_DTX_PTR		0x1B04

/* QDMA TX Release CPU Pointer Register */
#define MTK_QTX_CRX_PTR		0x1B10

/* QDMA TX Release DMA Pointer Register */
#define MTK_QTX_DRX_PTR		0x1B14

/* QDMA FQ Head Pointer Register */
#define MTK_QDMA_FQ_HEAD	0x1B20

/* QDMA FQ Head Pointer Register */
#define MTK_QDMA_FQ_TAIL	0x1B24

/* QDMA FQ Free Page Counter Register */
#define MTK_QDMA_FQ_CNT		0x1B28

/* QDMA FQ Free Page Buffer Length Register */
#define MTK_QDMA_FQ_BLEN	0x1B2C

/* GMA1 Received Good Byte Count Register */
#define MTK_GDM1_TX_GBCNT	0x2400
#define MTK_STAT_OFFSET		0x40

/* QDMA descriptor txd4 */
#define TX_DMA_CHKSUM		(0x7 << 29)
#define TX_DMA_TSO		BIT(28)
#define TX_DMA_FPORT_SHIFT	25
#define TX_DMA_FPORT_MASK	0x7
#define TX_DMA_INS_VLAN		BIT(16)

/* QDMA descriptor txd3 */
#define TX_DMA_OWNER_CPU	BIT(31)
#define TX_DMA_LS0		BIT(30)
#define TX_DMA_PLEN0(_x)	(((_x) & MTK_TX_DMA_BUF_LEN) << 16)
#define TX_DMA_SWC		BIT(14)
#define TX_DMA_SDL(_x)		(((_x) & 0x3fff) << 16)

/* QDMA descriptor rxd2 */
#define RX_DMA_DONE		BIT(31)
#define RX_DMA_PLEN0(_x)	(((_x) & 0x3fff) << 16)
#define RX_DMA_GET_PLEN0(_x)	(((_x) >> 16) & 0x3fff)

/* QDMA descriptor rxd3 */
#define RX_DMA_VID(_x)		((_x) & 0xfff)

/* QDMA descriptor rxd4 */
#define RX_DMA_L4_VALID		BIT(24)
#define RX_DMA_FPORT_SHIFT	19
#define RX_DMA_FPORT_MASK	0x7

/* PHY Indirect Access Control registers */
#define MTK_PHY_IAC		0x10004
#define PHY_IAC_ACCESS		BIT(31)
#define PHY_IAC_READ		BIT(19)
#define PHY_IAC_WRITE		BIT(18)
#define PHY_IAC_START		BIT(16)
#define PHY_IAC_ADDR_SHIFT	20
#define PHY_IAC_REG_SHIFT	25
#define PHY_IAC_TIMEOUT		HZ

#define MTK_MAC_MISC		0x1000c
#define MTK_MUX_TO_ESW		BIT(0)

/* Mac control registers */
#define MTK_MAC_MCR(x)		(0x10100 + (x * 0x100))
#define MAC_MCR_MAX_RX_1536	BIT(24)
#define MAC_MCR_IPG_CFG		(BIT(18) | BIT(16))
#define MAC_MCR_FORCE_MODE	BIT(15)
#define MAC_MCR_TX_EN		BIT(14)
#define MAC_MCR_RX_EN		BIT(13)
#define MAC_MCR_BACKOFF_EN	BIT(9)
#define MAC_MCR_BACKPR_EN	BIT(8)
#define MAC_MCR_FORCE_RX_FC	BIT(5)
#define MAC_MCR_FORCE_TX_FC	BIT(4)
#define MAC_MCR_SPEED_1000	BIT(3)
#define MAC_MCR_SPEED_100	BIT(2)
#define MAC_MCR_FORCE_DPX	BIT(1)
#define MAC_MCR_FORCE_LINK	BIT(0)
#define MAC_MCR_FIXED_LINK	(MAC_MCR_MAX_RX_1536 | MAC_MCR_IPG_CFG | \
				 MAC_MCR_FORCE_MODE | MAC_MCR_TX_EN | \
				 MAC_MCR_RX_EN | MAC_MCR_BACKOFF_EN | \
				 MAC_MCR_BACKPR_EN | MAC_MCR_FORCE_RX_FC | \
				 MAC_MCR_FORCE_TX_FC | MAC_MCR_SPEED_1000 | \
				 MAC_MCR_FORCE_DPX | MAC_MCR_FORCE_LINK)

/* TRGMII RXC control register */
#define TRGMII_RCK_CTRL		0x10300
#define DQSI0(x)		((x << 0) & GENMASK(6, 0))
#define DQSI1(x)		((x << 8) & GENMASK(14, 8))
#define RXCTL_DMWTLAT(x)	((x << 16) & GENMASK(18, 16))
#define RXC_DQSISEL		BIT(30)
#define RCK_CTRL_RGMII_1000	(RXC_DQSISEL | RXCTL_DMWTLAT(2) | DQSI1(16))
#define RCK_CTRL_RGMII_10_100	RXCTL_DMWTLAT(2)

/* TRGMII RXC control register */
#define TRGMII_TCK_CTRL		0x10340
#define TXCTL_DMWTLAT(x)	((x << 16) & GENMASK(18, 16))
#define TXC_INV			BIT(30)
#define TCK_CTRL_RGMII_1000	TXCTL_DMWTLAT(2)
#define TCK_CTRL_RGMII_10_100	(TXC_INV | TXCTL_DMWTLAT(2))

/* TRGMII Interface mode register */
#define INTF_MODE		0x10390
#define TRGMII_INTF_DIS		BIT(0)
#define TRGMII_MODE		BIT(1)
#define TRGMII_CENTRAL_ALIGNED	BIT(2)
#define INTF_MODE_RGMII_1000    (TRGMII_MODE | TRGMII_CENTRAL_ALIGNED)
#define INTF_MODE_RGMII_10_100  0

/* GPIO port control registers for GMAC 2*/
#define GPIO_OD33_CTRL8		0x4c0
#define GPIO_BIAS_CTRL		0xed0
#define GPIO_DRV_SEL10		0xf00

/* ethernet subsystem chip id register */
#define ETHSYS_CHIPID0_3	0x0
#define ETHSYS_CHIPID4_7	0x4
#define MT7623_ETH		7623
#define MT7622_ETH		7622

/* ethernet subsystem config register */
#define ETHSYS_SYSCFG0		0x14
#define SYSCFG0_GE_MASK		0x3
#define SYSCFG0_GE_MODE(x, y)	(x << (12 + (y * 2)))
#define SYSCFG0_SGMII_MASK	(3 << 8)
#define SYSCFG0_SGMII_GMAC1	((2 << 8) & GENMASK(9, 8))
#define SYSCFG0_SGMII_GMAC2	((3 << 8) & GENMASK(9, 8))

/* ethernet subsystem clock register */
#define ETHSYS_CLKCFG0		0x2c
#define ETHSYS_TRGMII_CLK_SEL362_5	BIT(11)

/* ethernet reset control register */
#define ETHSYS_RSTCTRL		0x34
#define RSTCTRL_FE		BIT(6)
#define RSTCTRL_PPE		BIT(31)

/* SGMII subsystem config registers */
/* Register to auto-negotiation restart */
#define SGMSYS_PCS_CONTROL_1	0x0
#define SGMII_AN_RESTART	BIT(9)

/* Register to programmable link timer, the unit in 2 * 8ns */
#define SGMSYS_PCS_LINK_TIMER	0x18
#define SGMII_LINK_TIMER_DEFAULT	(0x186a0 & GENMASK(19, 0))

/* Register to control remote fault */
#define SGMSYS_SGMII_MODE	0x20
#define SGMII_REMOTE_FAULT_DIS	BIT(8)

/* Register to power up QPHY */
#define SGMSYS_QPHY_PWR_STATE_CTRL 0xe8
#define	SGMII_PHYA_PWD		BIT(4)

struct mtk_rx_dma {
	unsigned int rxd1;
	unsigned int rxd2;
	unsigned int rxd3;
	unsigned int rxd4;
} __packed __aligned(4);

struct mtk_tx_dma {
	unsigned int txd1;
	unsigned int txd2;
	unsigned int txd3;
	unsigned int txd4;
} __packed __aligned(4);

struct mtk_eth;
struct mtk_mac;

/* struct mtk_hw_stats - the structure that holds the traffic statistics.
 * @stats_lock:		make sure that stats operations are atomic
 * @reg_offset:		the status register offset of the SoC
 * @syncp:		the refcount
 *
 * All of the supported SoCs have hardware counters for traffic statistics.
 * Whenever the status IRQ triggers we can read the latest stats from these
 * counters and store them in this struct.
 */
struct mtk_hw_stats {
	u64 tx_bytes;
	u64 tx_packets;
	u64 tx_skip;
	u64 tx_collisions;
	u64 rx_bytes;
	u64 rx_packets;
	u64 rx_overflow;
	u64 rx_fcs_errors;
	u64 rx_short_errors;
	u64 rx_long_errors;
	u64 rx_checksum_errors;
	u64 rx_flow_control_packets;

	spinlock_t		stats_lock;
	u32			reg_offset;
	struct u64_stats_sync	syncp;
};

enum mtk_tx_flags {
	/* PDMA descriptor can point at 1-2 segments. This enum allows us to
	 * track how memory was allocated so that it can be freed properly.
	 */
	MTK_TX_FLAGS_SINGLE0	= 0x01,
	MTK_TX_FLAGS_PAGE0	= 0x02,

	/* MTK_TX_FLAGS_FPORTx allows tracking which port the transmitted
	 * SKB out instead of looking up through hardware TX descriptor.
	 */
	MTK_TX_FLAGS_FPORT0	= 0x04,
	MTK_TX_FLAGS_FPORT1	= 0x08,
};

/* This enum allows us to identify how the clock is defined on the array of the
 * clock in the order
 */
enum mtk_clks_map {
	MTK_CLK_ETHIF,
	MTK_CLK_ESW,
	MTK_CLK_GP0,
	MTK_CLK_GP1,
	MTK_CLK_GP2,
	MTK_CLK_TRGPLL,
	MTK_CLK_SGMII_TX_250M,
	MTK_CLK_SGMII_RX_250M,
	MTK_CLK_SGMII_CDR_REF,
	MTK_CLK_SGMII_CDR_FB,
	MTK_CLK_SGMII_CK,
	MTK_CLK_ETH2PLL,
	MTK_CLK_MAX
};

#define MT7623_CLKS_BITMAP	(BIT(MTK_CLK_ETHIF) | BIT(MTK_CLK_ESW) |  \
				 BIT(MTK_CLK_GP1) | BIT(MTK_CLK_GP2) | \
				 BIT(MTK_CLK_TRGPLL))
#define MT7622_CLKS_BITMAP	(BIT(MTK_CLK_ETHIF) | BIT(MTK_CLK_ESW) |  \
				 BIT(MTK_CLK_GP0) | BIT(MTK_CLK_GP1) | \
				 BIT(MTK_CLK_GP2) | \
				 BIT(MTK_CLK_SGMII_TX_250M) | \
				 BIT(MTK_CLK_SGMII_RX_250M) | \
				 BIT(MTK_CLK_SGMII_CDR_REF) | \
				 BIT(MTK_CLK_SGMII_CDR_FB) | \
				 BIT(MTK_CLK_SGMII_CK) | \
				 BIT(MTK_CLK_ETH2PLL))
enum mtk_dev_state {
	MTK_HW_INIT,
	MTK_RESETTING
};

/* struct mtk_tx_buf -	This struct holds the pointers to the memory pointed at
 *			by the TX descriptor	s
 * @skb:		The SKB pointer of the packet being sent
 * @dma_addr0:		The base addr of the first segment
 * @dma_len0:		The length of the first segment
 * @dma_addr1:		The base addr of the second segment
 * @dma_len1:		The length of the second segment
 */
struct mtk_tx_buf {
	struct sk_buff *skb;
	u32 flags;
	DEFINE_DMA_UNMAP_ADDR(dma_addr0);
	DEFINE_DMA_UNMAP_LEN(dma_len0);
	DEFINE_DMA_UNMAP_ADDR(dma_addr1);
	DEFINE_DMA_UNMAP_LEN(dma_len1);
};

/* struct mtk_tx_ring -	This struct holds info describing a TX ring
 * @dma:		The descriptor ring
 * @buf:		The memory pointed at by the ring
 * @phys:		The physical addr of tx_buf
 * @next_free:		Pointer to the next free descriptor
 * @last_free:		Pointer to the last free descriptor
 * @thresh:		The threshold of minimum amount of free descriptors
 * @free_count:		QDMA uses a linked list. Track how many free descriptors
 *			are present
 */
struct mtk_tx_ring {
	struct mtk_tx_dma *dma;
	struct mtk_tx_buf *buf;
	dma_addr_t phys;
	struct mtk_tx_dma *next_free;
	struct mtk_tx_dma *last_free;
	u16 thresh;
	atomic_t free_count;
};

/* PDMA rx ring mode */
enum mtk_rx_flags {
	MTK_RX_FLAGS_NORMAL = 0,
	MTK_RX_FLAGS_HWLRO,
	MTK_RX_FLAGS_QDMA,
};

/* struct mtk_rx_ring -	This struct holds info describing a RX ring
 * @dma:		The descriptor ring
 * @data:		The memory pointed at by the ring
 * @phys:		The physical addr of rx_buf
 * @frag_size:		How big can each fragment be
 * @buf_size:		The size of each packet buffer
 * @calc_idx:		The current head of ring
 */
struct mtk_rx_ring {
	struct mtk_rx_dma *dma;
	u8 **data;
	dma_addr_t phys;
	u16 frag_size;
	u16 buf_size;
	u16 dma_size;
	bool calc_idx_update;
	u16 calc_idx;
	u32 crx_idx_reg;
};

#define MTK_TRGMII			BIT(0)
#define MTK_GMAC1_TRGMII		(BIT(1) | MTK_TRGMII)
#define MTK_ESW				BIT(4)
#define MTK_GMAC1_ESW			(BIT(5) | MTK_ESW)
#define MTK_SGMII			BIT(8)
#define MTK_GMAC1_SGMII			(BIT(9) | MTK_SGMII)
#define MTK_GMAC2_SGMII			(BIT(10) | MTK_SGMII)
#define MTK_DUAL_GMAC_SHARED_SGMII	(BIT(11) | MTK_GMAC1_SGMII | \
					 MTK_GMAC2_SGMII)
#define MTK_HAS_CAPS(caps, _x)		(((caps) & (_x)) == (_x))

/* struct mtk_eth_data -	This is the structure holding all differences
 *				among various plaforms
 * @caps			Flags shown the extra capability for the SoC
 * @required_clks		Flags shown the bitmap for required clocks on
 *				the target SoC
 * @required_pctl		A bool value to show whether the SoC requires
 *				the extra setup for those pins used by GMAC.
 */
struct mtk_soc_data {
	u32		caps;
	u32		required_clks;
	bool		required_pctl;
};

/* currently no SoC has more than 2 macs */
#define MTK_MAX_DEVS			2

/* struct mtk_eth -	This is the main datasructure for holding the state
 *			of the driver
 * @dev:		The device pointer
 * @base:		The mapped register i/o base
 * @page_lock:		Make sure that register operations are atomic
 * @tx_irq__lock:	Make sure that IRQ register operations are atomic
 * @rx_irq__lock:	Make sure that IRQ register operations are atomic
 * @dummy_dev:		we run 2 netdevs on 1 physical DMA ring and need a
 *			dummy for NAPI to work
 * @netdev:		The netdev instances
 * @mac:		Each netdev is linked to a physical MAC
 * @irq:		The IRQ that we are using
 * @msg_enable:		Ethtool msg level
 * @ethsys:		The register map pointing at the range used to setup
 *			MII modes
 * @sgmiisys:		The register map pointing at the range used to setup
 *			SGMII modes
 * @pctl:		The register map pointing at the range used to setup
 *			GMAC port drive/slew values
 * @dma_refcnt:		track how many netdevs are using the DMA engine
 * @tx_ring:		Pointer to the memory holding info about the TX ring
 * @rx_ring:		Pointer to the memory holding info about the RX ring
 * @rx_ring_qdma:	Pointer to the memory holding info about the QDMA RX ring
 * @tx_napi:		The TX NAPI struct
 * @rx_napi:		The RX NAPI struct
 * @scratch_ring:	Newer SoCs need memory for a second HW managed TX ring
 * @phy_scratch_ring:	physical address of scratch_ring
 * @scratch_head:	The scratch memory that scratch_ring points to.
 * @clks:		clock array for all clocks required
 * @mii_bus:		If there is a bus we need to create an instance for it
 * @pending_work:	The workqueue used to reset the dma ring
 * @state:		Initialization and runtime state of the device
 * @soc:		Holding specific data among vaious SoCs
 */

struct mtk_eth {
	struct device			*dev;
	void __iomem			*base;
	spinlock_t			page_lock;
	spinlock_t			tx_irq_lock;
	spinlock_t			rx_irq_lock;
	struct net_device		dummy_dev;
	struct net_device		*netdev[MTK_MAX_DEVS];
	struct mtk_mac			*mac[MTK_MAX_DEVS];
	int				irq[3];
	u32				msg_enable;
	unsigned long			sysclk;
	struct regmap			*ethsys;
	struct regmap			*sgmiisys;
	struct regmap			*pctl;
	u32				chip_id;
	bool				hwlro;
	refcount_t			dma_refcnt;
	struct mtk_tx_ring		tx_ring;
	struct mtk_rx_ring		rx_ring[MTK_MAX_RX_RING_NUM];
	struct mtk_rx_ring		rx_ring_qdma;
	struct napi_struct		tx_napi;
	struct napi_struct		rx_napi;
	struct mtk_tx_dma		*scratch_ring;
	dma_addr_t			phy_scratch_ring;
	void				*scratch_head;
	struct clk			*clks[MTK_CLK_MAX];

	struct mii_bus			*mii_bus;
	struct work_struct		pending_work;
	unsigned long			state;

	const struct mtk_soc_data	*soc;
};

/* struct mtk_mac -	the structure that holds the info about the MACs of the
 *			SoC
 * @id:			The number of the MAC
 * @ge_mode:            Interface mode kept for setup restoring
 * @of_node:		Our devicetree node
 * @hw:			Backpointer to our main datastruture
 * @hw_stats:		Packet statistics counter
 * @trgmii		Indicate if the MAC uses TRGMII connected to internal
			switch
 */
struct mtk_mac {
	int				id;
	int				ge_mode;
	struct device_node		*of_node;
	struct mtk_eth			*hw;
	struct mtk_hw_stats		*hw_stats;
	__be32				hwlro_ip[MTK_MAX_LRO_IP_CNT];
	int				hwlro_ip_cnt;
	bool				trgmii;
};

/* the struct describing the SoC. these are declared in the soc_xyz.c files */
extern const struct of_device_id of_mtk_match[];

/* read the hardware status register */
void mtk_stats_update_mac(struct mtk_mac *mac);

void mtk_w32(struct mtk_eth *eth, u32 val, unsigned reg);
u32 mtk_r32(struct mtk_eth *eth, unsigned reg);

#endif /* MTK_ETH_H */
