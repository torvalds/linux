/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 *   Copyright (C) 2009-2016 John Crispin <blogic@openwrt.org>
 *   Copyright (C) 2009-2016 Felix Fietkau <nbd@openwrt.org>
 *   Copyright (C) 2013-2016 Michael Lee <igvtee@gmail.com>
 */

#ifndef MTK_ETH_H
#define MTK_ETH_H

#include <linux/dma-mapping.h>
#include <linux/netdevice.h>
#include <linux/of_net.h>
#include <linux/u64_stats_sync.h>
#include <linux/refcount.h>
#include <linux/phylink.h>
#include <linux/rhashtable.h>
#include <linux/dim.h>
#include <linux/bitfield.h>
#include <net/page_pool/types.h>
#include <linux/bpf_trace.h>
#include "mtk_ppe.h"

#define MTK_MAX_DSA_PORTS	7
#define MTK_DSA_PORT_MASK	GENMASK(2, 0)

#define MTK_QDMA_NUM_QUEUES	16
#define MTK_QDMA_PAGE_SIZE	2048
#define MTK_MAX_RX_LENGTH	1536
#define MTK_MAX_RX_LENGTH_2K	2048
#define MTK_TX_DMA_BUF_LEN	0x3fff
#define MTK_TX_DMA_BUF_LEN_V2	0xffff
#define MTK_QDMA_RING_SIZE	2048
#define MTK_DMA_SIZE(x)		(SZ_##x)
#define MTK_FQ_DMA_HEAD		32
#define MTK_FQ_DMA_LENGTH	2048
#define MTK_RX_ETH_HLEN		(ETH_HLEN + ETH_FCS_LEN)
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
				 NETIF_F_SG | NETIF_F_TSO | \
				 NETIF_F_TSO6 | \
				 NETIF_F_IPV6_CSUM |\
				 NETIF_F_HW_TC)
#define MTK_HW_FEATURES_MT7628	(NETIF_F_SG | NETIF_F_RXCSUM)
#define NEXT_DESP_IDX(X, Y)	(((X) + 1) & ((Y) - 1))

#define MTK_PP_HEADROOM		XDP_PACKET_HEADROOM
#define MTK_PP_PAD		(MTK_PP_HEADROOM + \
				 SKB_DATA_ALIGN(sizeof(struct skb_shared_info)))
#define MTK_PP_MAX_BUF_SIZE	(PAGE_SIZE - MTK_PP_PAD)

#define MTK_QRX_OFFSET		0x10

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

/* Frame Engine Global Configuration */
#define MTK_FE_GLO_CFG(x)	(((x) == MTK_GMAC3_ID) ? 0x24 : 0x00)
#define MTK_FE_LINK_DOWN_P(x)	BIT(((x) + 8) % 16)

/* Frame Engine Global Reset Register */
#define MTK_RST_GL		0x04
#define RST_GL_PSE		BIT(0)

/* Frame Engine Interrupt Status Register */
#define MTK_INT_STATUS2		0x08
#define MTK_FE_INT_ENABLE	0x0c
#define MTK_FE_INT_FQ_EMPTY	BIT(8)
#define MTK_FE_INT_TSO_FAIL	BIT(12)
#define MTK_FE_INT_TSO_ILLEGAL	BIT(13)
#define MTK_FE_INT_TSO_ALIGN	BIT(14)
#define MTK_FE_INT_RFIFO_OV	BIT(18)
#define MTK_FE_INT_RFIFO_UF	BIT(19)
#define MTK_GDM1_AF		BIT(28)
#define MTK_GDM2_AF		BIT(29)

/* PDMA HW LRO Alter Flow Timer Register */
#define MTK_PDMA_LRO_ALT_REFRESH_TIMER	0x1c

/* Frame Engine Interrupt Grouping Register */
#define MTK_FE_INT_GRP		0x20

/* CDMP Ingress Control Register */
#define MTK_CDMQ_IG_CTRL	0x1400
#define MTK_CDMQ_STAG_EN	BIT(0)

/* CDMQ Exgress Control Register */
#define MTK_CDMQ_EG_CTRL	0x1404

/* CDMP Ingress Control Register */
#define MTK_CDMP_IG_CTRL	0x400
#define MTK_CDMP_STAG_EN	BIT(0)

/* CDMP Exgress Control Register */
#define MTK_CDMP_EG_CTRL	0x404

/* GDM Exgress Control Register */
#define MTK_GDMA_FWD_CFG(x)	({ typeof(x) _x = (x); (_x == MTK_GMAC3_ID) ?	\
				   0x540 : 0x500 + (_x * 0x1000); })
#define MTK_GDMA_SPECIAL_TAG	BIT(24)
#define MTK_GDMA_ICS_EN		BIT(22)
#define MTK_GDMA_TCS_EN		BIT(21)
#define MTK_GDMA_UCS_EN		BIT(20)
#define MTK_GDMA_STRP_CRC	BIT(16)
#define MTK_GDMA_TO_PDMA	0x0
#define MTK_GDMA_DROP_ALL       0x7777

/* GDM Egress Control Register */
#define MTK_GDMA_EG_CTRL(x)	({ typeof(x) _x = (x); (_x == MTK_GMAC3_ID) ?	\
				   0x544 : 0x504 + (_x * 0x1000); })
#define MTK_GDMA_XGDM_SEL	BIT(31)

/* Unicast Filter MAC Address Register - Low */
#define MTK_GDMA_MAC_ADRL(x)	({ typeof(x) _x = (x); (_x == MTK_GMAC3_ID) ?	\
				   0x548 : 0x508 + (_x * 0x1000); })

/* Unicast Filter MAC Address Register - High */
#define MTK_GDMA_MAC_ADRH(x)	({ typeof(x) _x = (x); (_x == MTK_GMAC3_ID) ?	\
				   0x54C : 0x50C + (_x * 0x1000); })

/* Internal SRAM offset */
#define MTK_ETH_SRAM_OFFSET	0x40000

/* FE global misc reg*/
#define MTK_FE_GLO_MISC         0x124

/* PSE Free Queue Flow Control  */
#define PSE_FQFC_CFG1		0x100
#define PSE_FQFC_CFG2		0x104
#define PSE_DROP_CFG		0x108
#define PSE_PPE0_DROP		0x110

/* PSE Input Queue Reservation Register*/
#define PSE_IQ_REV(x)		(0x140 + (((x) - 1) << 2))

/* PSE Output Queue Threshold Register*/
#define PSE_OQ_TH(x)		(0x160 + (((x) - 1) << 2))

/* GDM and CDM Threshold */
#define MTK_GDM2_THRES		0x1530
#define MTK_CDMW0_THRES		0x164c
#define MTK_CDMW1_THRES		0x1650
#define MTK_CDME0_THRES		0x1654
#define MTK_CDME1_THRES		0x1658
#define MTK_CDMM_THRES		0x165c

/* PDMA HW LRO Control Registers */
#define MTK_PDMA_LRO_CTRL_DW0	0x980
#define MTK_LRO_EN			BIT(0)
#define MTK_L3_CKS_UPD_EN		BIT(7)
#define MTK_L3_CKS_UPD_EN_V2		BIT(19)
#define MTK_LRO_ALT_PKT_CNT_MODE	BIT(21)
#define MTK_LRO_RING_RELINQUISH_REQ	(0x7 << 26)
#define MTK_LRO_RING_RELINQUISH_REQ_V2	(0xf << 24)
#define MTK_LRO_RING_RELINQUISH_DONE	(0x7 << 29)
#define MTK_LRO_RING_RELINQUISH_DONE_V2	(0xf << 28)

#define MTK_PDMA_LRO_CTRL_DW1	0x984
#define MTK_PDMA_LRO_CTRL_DW2	0x988
#define MTK_PDMA_LRO_CTRL_DW3	0x98c
#define MTK_ADMA_MODE		BIT(15)
#define MTK_LRO_MIN_RXD_SDL	(MTK_HW_LRO_SDL_REMAIN_ROOM << 16)

#define MTK_RX_DMA_LRO_EN	BIT(8)
#define MTK_MULTI_EN		BIT(10)
#define MTK_PDMA_SIZE_8DWORDS	(1 << 4)

/* PDMA Global Configuration Register */
#define MTK_PDMA_LRO_SDL	0x3000
#define MTK_RX_CFG_SDL_OFFSET	16

/* PDMA Reset Index Register */
#define MTK_PST_DRX_IDX0	BIT(16)
#define MTK_PST_DRX_IDX_CFG(x)	(MTK_PST_DRX_IDX0 << (x))

/* PDMA Delay Interrupt Register */
#define MTK_PDMA_DELAY_RX_MASK		GENMASK(15, 0)
#define MTK_PDMA_DELAY_RX_EN		BIT(15)
#define MTK_PDMA_DELAY_RX_PINT_SHIFT	8
#define MTK_PDMA_DELAY_RX_PTIME_SHIFT	0

#define MTK_PDMA_DELAY_TX_MASK		GENMASK(31, 16)
#define MTK_PDMA_DELAY_TX_EN		BIT(31)
#define MTK_PDMA_DELAY_TX_PINT_SHIFT	24
#define MTK_PDMA_DELAY_TX_PTIME_SHIFT	16

#define MTK_PDMA_DELAY_PINT_MASK	0x7f
#define MTK_PDMA_DELAY_PTIME_MASK	0xff

/* PDMA HW LRO Alter Flow Delta Register */
#define MTK_PDMA_LRO_ALT_SCORE_DELTA	0xa4c

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
#define MTK_QTX_OFFSET		0x10
#define QDMA_RES_THRES		4

/* QDMA Tx Queue Scheduler Configuration Registers */
#define MTK_QTX_SCH_TX_SEL		BIT(31)
#define MTK_QTX_SCH_TX_SEL_V2		GENMASK(31, 30)

#define MTK_QTX_SCH_LEAKY_BUCKET_EN	BIT(30)
#define MTK_QTX_SCH_LEAKY_BUCKET_SIZE	GENMASK(29, 28)
#define MTK_QTX_SCH_MIN_RATE_EN		BIT(27)
#define MTK_QTX_SCH_MIN_RATE_MAN	GENMASK(26, 20)
#define MTK_QTX_SCH_MIN_RATE_EXP	GENMASK(19, 16)
#define MTK_QTX_SCH_MAX_RATE_WEIGHT	GENMASK(15, 12)
#define MTK_QTX_SCH_MAX_RATE_EN		BIT(11)
#define MTK_QTX_SCH_MAX_RATE_MAN	GENMASK(10, 4)
#define MTK_QTX_SCH_MAX_RATE_EXP	GENMASK(3, 0)

/* QDMA TX Scheduler Rate Control Register */
#define MTK_QDMA_TX_SCH_MAX_WFQ		BIT(15)

/* QDMA Global Configuration Register */
#define MTK_RX_2B_OFFSET	BIT(31)
#define MTK_RX_BT_32DWORDS	(3 << 11)
#define MTK_NDP_CO_PRO		BIT(10)
#define MTK_TX_WB_DDONE		BIT(6)
#define MTK_TX_BT_32DWORDS	(3 << 4)
#define MTK_RX_DMA_BUSY		BIT(3)
#define MTK_TX_DMA_BUSY		BIT(1)
#define MTK_RX_DMA_EN		BIT(2)
#define MTK_TX_DMA_EN		BIT(0)
#define MTK_DMA_BUSY_TIMEOUT_US	1000000

/* QDMA V2 Global Configuration Register */
#define MTK_CHK_DDONE_EN	BIT(28)
#define MTK_DMAD_WR_WDONE	BIT(26)
#define MTK_WCOMP_EN		BIT(24)
#define MTK_RESV_BUF		(0x40 << 16)
#define MTK_MUTLI_CNT		(0x4 << 12)
#define MTK_LEAKY_BUCKET_EN	BIT(11)

/* QDMA Flow Control Register */
#define FC_THRES_DROP_MODE	BIT(20)
#define FC_THRES_DROP_EN	(7 << 16)
#define FC_THRES_MIN		0x4444

/* QDMA Interrupt Status Register */
#define MTK_RX_DONE_DLY		BIT(30)
#define MTK_TX_DONE_DLY		BIT(28)
#define MTK_RX_DONE_INT3	BIT(19)
#define MTK_RX_DONE_INT2	BIT(18)
#define MTK_RX_DONE_INT1	BIT(17)
#define MTK_RX_DONE_INT0	BIT(16)
#define MTK_TX_DONE_INT3	BIT(3)
#define MTK_TX_DONE_INT2	BIT(2)
#define MTK_TX_DONE_INT1	BIT(1)
#define MTK_TX_DONE_INT0	BIT(0)
#define MTK_RX_DONE_INT		MTK_RX_DONE_DLY
#define MTK_TX_DONE_INT		MTK_TX_DONE_DLY

#define MTK_RX_DONE_INT_V2	BIT(14)

#define MTK_CDM_TXFIFO_RDY	BIT(7)

/* QDMA Interrupt grouping registers */
#define MTK_RLS_DONE_INT	BIT(0)

/* QDMA TX NUM */
#define QID_BITS_V2(x)		(((x) & 0x3f) << 16)
#define MTK_QDMA_GMAC2_QID	8

#define MTK_TX_DMA_BUF_SHIFT	8

/* QDMA V2 descriptor txd6 */
#define TX_DMA_INS_VLAN_V2	BIT(16)
/* QDMA V2 descriptor txd5 */
#define TX_DMA_CHKSUM_V2	(0x7 << 28)
#define TX_DMA_TSO_V2		BIT(31)

#define TX_DMA_SPTAG_V3         BIT(27)

/* QDMA V2 descriptor txd4 */
#define TX_DMA_FPORT_SHIFT_V2	8
#define TX_DMA_FPORT_MASK_V2	0xf
#define TX_DMA_SWC_V2		BIT(30)

/* QDMA descriptor txd4 */
#define TX_DMA_CHKSUM		(0x7 << 29)
#define TX_DMA_TSO		BIT(28)
#define TX_DMA_FPORT_SHIFT	25
#define TX_DMA_FPORT_MASK	0x7
#define TX_DMA_INS_VLAN		BIT(16)

/* QDMA descriptor txd3 */
#define TX_DMA_OWNER_CPU	BIT(31)
#define TX_DMA_LS0		BIT(30)
#define TX_DMA_PLEN0(x)		(((x) & eth->soc->tx.dma_max_len) << eth->soc->tx.dma_len_offset)
#define TX_DMA_PLEN1(x)		((x) & eth->soc->tx.dma_max_len)
#define TX_DMA_SWC		BIT(14)
#define TX_DMA_PQID		GENMASK(3, 0)
#define TX_DMA_ADDR64_MASK	GENMASK(3, 0)
#if IS_ENABLED(CONFIG_64BIT)
# define TX_DMA_GET_ADDR64(x)	(((u64)FIELD_GET(TX_DMA_ADDR64_MASK, (x))) << 32)
# define TX_DMA_PREP_ADDR64(x)	FIELD_PREP(TX_DMA_ADDR64_MASK, ((x) >> 32))
#else
# define TX_DMA_GET_ADDR64(x)	(0)
# define TX_DMA_PREP_ADDR64(x)	(0)
#endif

/* PDMA on MT7628 */
#define TX_DMA_DONE		BIT(31)
#define TX_DMA_LS1		BIT(14)
#define TX_DMA_DESP2_DEF	(TX_DMA_LS0 | TX_DMA_DONE)

/* QDMA descriptor rxd2 */
#define RX_DMA_DONE		BIT(31)
#define RX_DMA_LSO		BIT(30)
#define RX_DMA_PREP_PLEN0(x)	(((x) & eth->soc->rx.dma_max_len) << eth->soc->rx.dma_len_offset)
#define RX_DMA_GET_PLEN0(x)	(((x) >> eth->soc->rx.dma_len_offset) & eth->soc->rx.dma_max_len)
#define RX_DMA_VTAG		BIT(15)
#define RX_DMA_ADDR64_MASK	GENMASK(3, 0)
#if IS_ENABLED(CONFIG_64BIT)
# define RX_DMA_GET_ADDR64(x)	(((u64)FIELD_GET(RX_DMA_ADDR64_MASK, (x))) << 32)
# define RX_DMA_PREP_ADDR64(x)	FIELD_PREP(RX_DMA_ADDR64_MASK, ((x) >> 32))
#else
# define RX_DMA_GET_ADDR64(x)	(0)
# define RX_DMA_PREP_ADDR64(x)	(0)
#endif

/* QDMA descriptor rxd3 */
#define RX_DMA_VID(x)		((x) & VLAN_VID_MASK)
#define RX_DMA_TCI(x)		((x) & (VLAN_PRIO_MASK | VLAN_VID_MASK))
#define RX_DMA_VPID(x)		(((x) >> 16) & 0xffff)

/* QDMA descriptor rxd4 */
#define MTK_RXD4_FOE_ENTRY	GENMASK(13, 0)
#define MTK_RXD4_PPE_CPU_REASON	GENMASK(18, 14)
#define MTK_RXD4_SRC_PORT	GENMASK(21, 19)
#define MTK_RXD4_ALG		GENMASK(31, 22)

/* QDMA descriptor rxd4 */
#define RX_DMA_L4_VALID		BIT(24)
#define RX_DMA_L4_VALID_PDMA	BIT(30)		/* when PDMA is used */
#define RX_DMA_SPECIAL_TAG	BIT(22)

/* PDMA descriptor rxd5 */
#define MTK_RXD5_FOE_ENTRY	GENMASK(14, 0)
#define MTK_RXD5_PPE_CPU_REASON	GENMASK(22, 18)
#define MTK_RXD5_SRC_PORT	GENMASK(29, 26)

#define RX_DMA_GET_SPORT(x)	(((x) >> 19) & 0x7)
#define RX_DMA_GET_SPORT_V2(x)	(((x) >> 26) & 0xf)

/* PDMA V2 descriptor rxd3 */
#define RX_DMA_VTAG_V2		BIT(0)
#define RX_DMA_L4_VALID_V2	BIT(2)

/* PHY Polling and SMI Master Control registers */
#define MTK_PPSC		0x10000
#define PPSC_MDC_CFG		GENMASK(29, 24)
#define PPSC_MDC_TURBO		BIT(20)
#define MDC_MAX_FREQ		25000000
#define MDC_MAX_DIVIDER		63

/* PHY Indirect Access Control registers */
#define MTK_PHY_IAC		0x10004
#define PHY_IAC_ACCESS		BIT(31)
#define PHY_IAC_REG_MASK	GENMASK(29, 25)
#define PHY_IAC_REG(x)		FIELD_PREP(PHY_IAC_REG_MASK, (x))
#define PHY_IAC_ADDR_MASK	GENMASK(24, 20)
#define PHY_IAC_ADDR(x)		FIELD_PREP(PHY_IAC_ADDR_MASK, (x))
#define PHY_IAC_CMD_MASK	GENMASK(19, 18)
#define PHY_IAC_CMD_C45_ADDR	FIELD_PREP(PHY_IAC_CMD_MASK, 0)
#define PHY_IAC_CMD_WRITE	FIELD_PREP(PHY_IAC_CMD_MASK, 1)
#define PHY_IAC_CMD_C22_READ	FIELD_PREP(PHY_IAC_CMD_MASK, 2)
#define PHY_IAC_CMD_C45_READ	FIELD_PREP(PHY_IAC_CMD_MASK, 3)
#define PHY_IAC_START_MASK	GENMASK(17, 16)
#define PHY_IAC_START_C45	FIELD_PREP(PHY_IAC_START_MASK, 0)
#define PHY_IAC_START_C22	FIELD_PREP(PHY_IAC_START_MASK, 1)
#define PHY_IAC_DATA_MASK	GENMASK(15, 0)
#define PHY_IAC_DATA(x)		FIELD_PREP(PHY_IAC_DATA_MASK, (x))
#define PHY_IAC_TIMEOUT		HZ

#define MTK_MAC_MISC		0x1000c
#define MTK_MAC_MISC_V3		0x10010
#define MTK_MUX_TO_ESW		BIT(0)
#define MISC_MDC_TURBO		BIT(4)

/* XMAC status registers */
#define MTK_XGMAC_STS(x)	(((x) == MTK_GMAC3_ID) ? 0x1001C : 0x1000C)
#define MTK_XGMAC_FORCE_LINK(x)	(((x) == MTK_GMAC2_ID) ? BIT(31) : BIT(15))
#define MTK_USXGMII_PCS_LINK	BIT(8)
#define MTK_XGMAC_RX_FC		BIT(5)
#define MTK_XGMAC_TX_FC		BIT(4)
#define MTK_USXGMII_PCS_MODE	GENMASK(3, 1)
#define MTK_XGMAC_LINK_STS	BIT(0)

/* GSW bridge registers */
#define MTK_GSW_CFG		(0x10080)
#define GSWTX_IPG_MASK		GENMASK(19, 16)
#define GSWTX_IPG_SHIFT		16
#define GSWRX_IPG_MASK		GENMASK(3, 0)
#define GSWRX_IPG_SHIFT		0
#define GSW_IPG_11		11

/* Mac control registers */
#define MTK_MAC_MCR(x)		(0x10100 + (x * 0x100))
#define MAC_MCR_MAX_RX_MASK	GENMASK(25, 24)
#define MAC_MCR_MAX_RX(_x)	(MAC_MCR_MAX_RX_MASK & ((_x) << 24))
#define MAC_MCR_MAX_RX_1518	0x0
#define MAC_MCR_MAX_RX_1536	0x1
#define MAC_MCR_MAX_RX_1552	0x2
#define MAC_MCR_MAX_RX_2048	0x3
#define MAC_MCR_IPG_CFG		(BIT(18) | BIT(16))
#define MAC_MCR_FORCE_MODE	BIT(15)
#define MAC_MCR_TX_EN		BIT(14)
#define MAC_MCR_RX_EN		BIT(13)
#define MAC_MCR_RX_FIFO_CLR_DIS	BIT(12)
#define MAC_MCR_BACKOFF_EN	BIT(9)
#define MAC_MCR_BACKPR_EN	BIT(8)
#define MAC_MCR_EEE1G		BIT(7)
#define MAC_MCR_EEE100M		BIT(6)
#define MAC_MCR_FORCE_RX_FC	BIT(5)
#define MAC_MCR_FORCE_TX_FC	BIT(4)
#define MAC_MCR_SPEED_1000	BIT(3)
#define MAC_MCR_SPEED_100	BIT(2)
#define MAC_MCR_FORCE_DPX	BIT(1)
#define MAC_MCR_FORCE_LINK	BIT(0)
#define MAC_MCR_FORCE_LINK_DOWN	(MAC_MCR_FORCE_MODE)

/* Mac EEE control registers */
#define MTK_MAC_EEECR(x)		(0x10104 + (x * 0x100))
#define MAC_EEE_WAKEUP_TIME_1000	GENMASK(31, 24)
#define MAC_EEE_WAKEUP_TIME_100		GENMASK(23, 16)
#define MAC_EEE_LPI_TXIDLE_THD		GENMASK(15, 8)
#define MAC_EEE_CKG_TXIDLE		BIT(3)
#define MAC_EEE_CKG_RXLPI		BIT(2)
#define MAC_EEE_LPI_MODE		BIT(0)

/* Mac status registers */
#define MTK_MAC_MSR(x)		(0x10108 + (x * 0x100))
#define MAC_MSR_EEE1G		BIT(7)
#define MAC_MSR_EEE100M		BIT(6)
#define MAC_MSR_RX_FC		BIT(5)
#define MAC_MSR_TX_FC		BIT(4)
#define MAC_MSR_SPEED_1000	BIT(3)
#define MAC_MSR_SPEED_100	BIT(2)
#define MAC_MSR_SPEED_MASK	(MAC_MSR_SPEED_1000 | MAC_MSR_SPEED_100)
#define MAC_MSR_DPX		BIT(1)
#define MAC_MSR_LINK		BIT(0)

/* TRGMII RXC control register */
#define TRGMII_RCK_CTRL		0x10300
#define DQSI0(x)		((x << 0) & GENMASK(6, 0))
#define DQSI1(x)		((x << 8) & GENMASK(14, 8))
#define RXCTL_DMWTLAT(x)	((x << 16) & GENMASK(18, 16))
#define RXC_RST			BIT(31)
#define RXC_DQSISEL		BIT(30)
#define RCK_CTRL_RGMII_1000	(RXC_DQSISEL | RXCTL_DMWTLAT(2) | DQSI1(16))
#define RCK_CTRL_RGMII_10_100	RXCTL_DMWTLAT(2)

#define NUM_TRGMII_CTRL		5

/* TRGMII RXC control register */
#define TRGMII_TCK_CTRL		0x10340
#define TXCTL_DMWTLAT(x)	((x << 16) & GENMASK(18, 16))
#define TXC_INV			BIT(30)
#define TCK_CTRL_RGMII_1000	TXCTL_DMWTLAT(2)
#define TCK_CTRL_RGMII_10_100	(TXC_INV | TXCTL_DMWTLAT(2))

/* TRGMII TX Drive Strength */
#define TRGMII_TD_ODT(i)	(0x10354 + 8 * (i))
#define  TD_DM_DRVP(x)		((x) & 0xf)
#define  TD_DM_DRVN(x)		(((x) & 0xf) << 4)

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
#define MT7621_ETH		7621

/* ethernet system control register */
#define ETHSYS_SYSCFG		0x10
#define SYSCFG_DRAM_TYPE_DDR2	BIT(4)

/* ethernet subsystem config register */
#define ETHSYS_SYSCFG0		0x14
#define SYSCFG0_GE_MASK		0x3
#define SYSCFG0_GE_MODE(x, y)	(x << (12 + (y * 2)))
#define SYSCFG0_SGMII_MASK     GENMASK(9, 7)
#define SYSCFG0_SGMII_GMAC1    ((2 << 8) & SYSCFG0_SGMII_MASK)
#define SYSCFG0_SGMII_GMAC2    ((3 << 8) & SYSCFG0_SGMII_MASK)
#define SYSCFG0_SGMII_GMAC1_V2 BIT(9)
#define SYSCFG0_SGMII_GMAC2_V2 BIT(8)


/* ethernet subsystem clock register */
#define ETHSYS_CLKCFG0		0x2c
#define ETHSYS_TRGMII_CLK_SEL362_5	BIT(11)
#define ETHSYS_TRGMII_MT7621_MASK	(BIT(5) | BIT(6))
#define ETHSYS_TRGMII_MT7621_APLL	BIT(6)
#define ETHSYS_TRGMII_MT7621_DDR_PLL	BIT(5)

/* ethernet reset control register */
#define ETHSYS_RSTCTRL			0x34
#define RSTCTRL_FE			BIT(6)
#define RSTCTRL_WDMA0			BIT(24)
#define RSTCTRL_WDMA1			BIT(25)
#define RSTCTRL_WDMA2			BIT(26)
#define RSTCTRL_PPE0			BIT(31)
#define RSTCTRL_PPE0_V2			BIT(30)
#define RSTCTRL_PPE1			BIT(31)
#define RSTCTRL_PPE0_V3			BIT(29)
#define RSTCTRL_PPE1_V3			BIT(30)
#define RSTCTRL_PPE2			BIT(31)
#define RSTCTRL_ETH			BIT(23)

/* ethernet reset check idle register */
#define ETHSYS_FE_RST_CHK_IDLE_EN	0x28

/* ethernet dma channel agent map */
#define ETHSYS_DMA_AG_MAP	0x408
#define ETHSYS_DMA_AG_MAP_PDMA	BIT(0)
#define ETHSYS_DMA_AG_MAP_QDMA	BIT(1)
#define ETHSYS_DMA_AG_MAP_PPE	BIT(2)

/* Infrasys subsystem config registers */
#define INFRA_MISC2            0x70c
#define CO_QPHY_SEL            BIT(0)
#define GEPHY_MAC_SEL          BIT(1)

/* Top misc registers */
#define USB_PHY_SWITCH_REG	0x218
#define QPHY_SEL_MASK		GENMASK(1, 0)
#define SGMII_QPHY_SEL		0x2

/* MT7628/88 specific stuff */
#define MT7628_PDMA_OFFSET	0x0800
#define MT7628_SDM_OFFSET	0x0c00

#define MT7628_TX_BASE_PTR0	(MT7628_PDMA_OFFSET + 0x00)
#define MT7628_TX_MAX_CNT0	(MT7628_PDMA_OFFSET + 0x04)
#define MT7628_TX_CTX_IDX0	(MT7628_PDMA_OFFSET + 0x08)
#define MT7628_TX_DTX_IDX0	(MT7628_PDMA_OFFSET + 0x0c)
#define MT7628_PST_DTX_IDX0	BIT(0)

#define MT7628_SDM_MAC_ADRL	(MT7628_SDM_OFFSET + 0x0c)
#define MT7628_SDM_MAC_ADRH	(MT7628_SDM_OFFSET + 0x10)

/* Counter / stat register */
#define MT7628_SDM_TPCNT	(MT7628_SDM_OFFSET + 0x100)
#define MT7628_SDM_TBCNT	(MT7628_SDM_OFFSET + 0x104)
#define MT7628_SDM_RPCNT	(MT7628_SDM_OFFSET + 0x108)
#define MT7628_SDM_RBCNT	(MT7628_SDM_OFFSET + 0x10c)
#define MT7628_SDM_CS_ERR	(MT7628_SDM_OFFSET + 0x110)

#define MTK_FE_CDM1_FSM		0x220
#define MTK_FE_CDM2_FSM		0x224
#define MTK_FE_CDM3_FSM		0x238
#define MTK_FE_CDM4_FSM		0x298
#define MTK_FE_CDM5_FSM		0x318
#define MTK_FE_CDM6_FSM		0x328
#define MTK_FE_GDM1_FSM		0x228
#define MTK_FE_GDM2_FSM		0x22C

#define MTK_MAC_FSM(x)		(0x1010C + ((x) * 0x100))

struct mtk_rx_dma {
	unsigned int rxd1;
	unsigned int rxd2;
	unsigned int rxd3;
	unsigned int rxd4;
} __packed __aligned(4);

struct mtk_rx_dma_v2 {
	unsigned int rxd1;
	unsigned int rxd2;
	unsigned int rxd3;
	unsigned int rxd4;
	unsigned int rxd5;
	unsigned int rxd6;
	unsigned int rxd7;
	unsigned int rxd8;
} __packed __aligned(4);

struct mtk_tx_dma {
	unsigned int txd1;
	unsigned int txd2;
	unsigned int txd3;
	unsigned int txd4;
} __packed __aligned(4);

struct mtk_tx_dma_v2 {
	unsigned int txd1;
	unsigned int txd2;
	unsigned int txd3;
	unsigned int txd4;
	unsigned int txd5;
	unsigned int txd6;
	unsigned int txd7;
	unsigned int txd8;
} __packed __aligned(4);

struct mtk_eth;
struct mtk_mac;

struct mtk_xdp_stats {
	u64 rx_xdp_redirect;
	u64 rx_xdp_pass;
	u64 rx_xdp_drop;
	u64 rx_xdp_tx;
	u64 rx_xdp_tx_errors;
	u64 tx_xdp_xmit;
	u64 tx_xdp_xmit_errors;
};

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

	struct mtk_xdp_stats	xdp_stats;

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
};

/* This enum allows us to identify how the clock is defined on the array of the
 * clock in the order
 */
enum mtk_clks_map {
	MTK_CLK_ETHIF,
	MTK_CLK_SGMIITOP,
	MTK_CLK_ESW,
	MTK_CLK_GP0,
	MTK_CLK_GP1,
	MTK_CLK_GP2,
	MTK_CLK_GP3,
	MTK_CLK_XGP1,
	MTK_CLK_XGP2,
	MTK_CLK_XGP3,
	MTK_CLK_CRYPTO,
	MTK_CLK_FE,
	MTK_CLK_TRGPLL,
	MTK_CLK_SGMII_TX_250M,
	MTK_CLK_SGMII_RX_250M,
	MTK_CLK_SGMII_CDR_REF,
	MTK_CLK_SGMII_CDR_FB,
	MTK_CLK_SGMII2_TX_250M,
	MTK_CLK_SGMII2_RX_250M,
	MTK_CLK_SGMII2_CDR_REF,
	MTK_CLK_SGMII2_CDR_FB,
	MTK_CLK_SGMII_CK,
	MTK_CLK_ETH2PLL,
	MTK_CLK_WOCPU0,
	MTK_CLK_WOCPU1,
	MTK_CLK_NETSYS0,
	MTK_CLK_NETSYS1,
	MTK_CLK_ETHWARP_WOCPU2,
	MTK_CLK_ETHWARP_WOCPU1,
	MTK_CLK_ETHWARP_WOCPU0,
	MTK_CLK_TOP_SGM_0_SEL,
	MTK_CLK_TOP_SGM_1_SEL,
	MTK_CLK_TOP_ETH_GMII_SEL,
	MTK_CLK_TOP_ETH_REFCK_50M_SEL,
	MTK_CLK_TOP_ETH_SYS_200M_SEL,
	MTK_CLK_TOP_ETH_SYS_SEL,
	MTK_CLK_TOP_ETH_XGMII_SEL,
	MTK_CLK_TOP_ETH_MII_SEL,
	MTK_CLK_TOP_NETSYS_SEL,
	MTK_CLK_TOP_NETSYS_500M_SEL,
	MTK_CLK_TOP_NETSYS_PAO_2X_SEL,
	MTK_CLK_TOP_NETSYS_SYNC_250M_SEL,
	MTK_CLK_TOP_NETSYS_PPEFB_250M_SEL,
	MTK_CLK_TOP_NETSYS_WARP_SEL,
	MTK_CLK_MAX
};

#define MT7623_CLKS_BITMAP	(BIT_ULL(MTK_CLK_ETHIF) | BIT_ULL(MTK_CLK_ESW) |  \
				 BIT_ULL(MTK_CLK_GP1) | BIT_ULL(MTK_CLK_GP2) | \
				 BIT_ULL(MTK_CLK_TRGPLL))
#define MT7622_CLKS_BITMAP	(BIT_ULL(MTK_CLK_ETHIF) | BIT_ULL(MTK_CLK_ESW) |  \
				 BIT_ULL(MTK_CLK_GP0) | BIT_ULL(MTK_CLK_GP1) | \
				 BIT_ULL(MTK_CLK_GP2) | \
				 BIT_ULL(MTK_CLK_SGMII_TX_250M) | \
				 BIT_ULL(MTK_CLK_SGMII_RX_250M) | \
				 BIT_ULL(MTK_CLK_SGMII_CDR_REF) | \
				 BIT_ULL(MTK_CLK_SGMII_CDR_FB) | \
				 BIT_ULL(MTK_CLK_SGMII_CK) | \
				 BIT_ULL(MTK_CLK_ETH2PLL))
#define MT7621_CLKS_BITMAP	(0)
#define MT7628_CLKS_BITMAP	(0)
#define MT7629_CLKS_BITMAP	(BIT_ULL(MTK_CLK_ETHIF) | BIT_ULL(MTK_CLK_ESW) |  \
				 BIT_ULL(MTK_CLK_GP0) | BIT_ULL(MTK_CLK_GP1) | \
				 BIT_ULL(MTK_CLK_GP2) | BIT_ULL(MTK_CLK_FE) | \
				 BIT_ULL(MTK_CLK_SGMII_TX_250M) | \
				 BIT_ULL(MTK_CLK_SGMII_RX_250M) | \
				 BIT_ULL(MTK_CLK_SGMII_CDR_REF) | \
				 BIT_ULL(MTK_CLK_SGMII_CDR_FB) | \
				 BIT_ULL(MTK_CLK_SGMII2_TX_250M) | \
				 BIT_ULL(MTK_CLK_SGMII2_RX_250M) | \
				 BIT_ULL(MTK_CLK_SGMII2_CDR_REF) | \
				 BIT_ULL(MTK_CLK_SGMII2_CDR_FB) | \
				 BIT_ULL(MTK_CLK_SGMII_CK) | \
				 BIT_ULL(MTK_CLK_ETH2PLL) | BIT_ULL(MTK_CLK_SGMIITOP))
#define MT7981_CLKS_BITMAP	(BIT_ULL(MTK_CLK_FE) | BIT_ULL(MTK_CLK_GP2) | \
				 BIT_ULL(MTK_CLK_GP1) | \
				 BIT_ULL(MTK_CLK_WOCPU0) | \
				 BIT_ULL(MTK_CLK_SGMII_TX_250M) | \
				 BIT_ULL(MTK_CLK_SGMII_RX_250M) | \
				 BIT_ULL(MTK_CLK_SGMII_CDR_REF) | \
				 BIT_ULL(MTK_CLK_SGMII_CDR_FB) | \
				 BIT_ULL(MTK_CLK_SGMII2_TX_250M) | \
				 BIT_ULL(MTK_CLK_SGMII2_RX_250M) | \
				 BIT_ULL(MTK_CLK_SGMII2_CDR_REF) | \
				 BIT_ULL(MTK_CLK_SGMII2_CDR_FB) | \
				 BIT_ULL(MTK_CLK_SGMII_CK))
#define MT7986_CLKS_BITMAP	(BIT_ULL(MTK_CLK_FE) | BIT_ULL(MTK_CLK_GP2) | \
				 BIT_ULL(MTK_CLK_GP1) | \
				 BIT_ULL(MTK_CLK_WOCPU1) | BIT_ULL(MTK_CLK_WOCPU0) | \
				 BIT_ULL(MTK_CLK_SGMII_TX_250M) | \
				 BIT_ULL(MTK_CLK_SGMII_RX_250M) | \
				 BIT_ULL(MTK_CLK_SGMII_CDR_REF) | \
				 BIT_ULL(MTK_CLK_SGMII_CDR_FB) | \
				 BIT_ULL(MTK_CLK_SGMII2_TX_250M) | \
				 BIT_ULL(MTK_CLK_SGMII2_RX_250M) | \
				 BIT_ULL(MTK_CLK_SGMII2_CDR_REF) | \
				 BIT_ULL(MTK_CLK_SGMII2_CDR_FB))
#define MT7988_CLKS_BITMAP	(BIT_ULL(MTK_CLK_FE) | BIT_ULL(MTK_CLK_ESW) | \
				 BIT_ULL(MTK_CLK_GP1) | BIT_ULL(MTK_CLK_GP2) | \
				 BIT_ULL(MTK_CLK_GP3) | BIT_ULL(MTK_CLK_XGP1) | \
				 BIT_ULL(MTK_CLK_XGP2) | BIT_ULL(MTK_CLK_XGP3) | \
				 BIT_ULL(MTK_CLK_CRYPTO) | \
				 BIT_ULL(MTK_CLK_ETHWARP_WOCPU2) | \
				 BIT_ULL(MTK_CLK_ETHWARP_WOCPU1) | \
				 BIT_ULL(MTK_CLK_ETHWARP_WOCPU0) | \
				 BIT_ULL(MTK_CLK_TOP_ETH_GMII_SEL) | \
				 BIT_ULL(MTK_CLK_TOP_ETH_REFCK_50M_SEL) | \
				 BIT_ULL(MTK_CLK_TOP_ETH_SYS_200M_SEL) | \
				 BIT_ULL(MTK_CLK_TOP_ETH_SYS_SEL) | \
				 BIT_ULL(MTK_CLK_TOP_ETH_XGMII_SEL) | \
				 BIT_ULL(MTK_CLK_TOP_ETH_MII_SEL) | \
				 BIT_ULL(MTK_CLK_TOP_NETSYS_SEL) | \
				 BIT_ULL(MTK_CLK_TOP_NETSYS_500M_SEL) | \
				 BIT_ULL(MTK_CLK_TOP_NETSYS_PAO_2X_SEL) | \
				 BIT_ULL(MTK_CLK_TOP_NETSYS_SYNC_250M_SEL) | \
				 BIT_ULL(MTK_CLK_TOP_NETSYS_PPEFB_250M_SEL) | \
				 BIT_ULL(MTK_CLK_TOP_NETSYS_WARP_SEL))

enum mtk_dev_state {
	MTK_HW_INIT,
	MTK_RESETTING
};

/* PSE Port Definition */
enum mtk_pse_port {
	PSE_ADMA_PORT = 0,
	PSE_GDM1_PORT,
	PSE_GDM2_PORT,
	PSE_PPE0_PORT,
	PSE_PPE1_PORT,
	PSE_QDMA_TX_PORT,
	PSE_QDMA_RX_PORT,
	PSE_DROP_PORT,
	PSE_WDMA0_PORT,
	PSE_WDMA1_PORT,
	PSE_TDMA_PORT,
	PSE_NONE_PORT,
	PSE_PPE2_PORT,
	PSE_WDMA2_PORT,
	PSE_EIP197_PORT,
	PSE_GDM3_PORT,
	PSE_PORT_MAX
};

/* GMAC Identifier */
enum mtk_gmac_id {
	MTK_GMAC1_ID = 0,
	MTK_GMAC2_ID,
	MTK_GMAC3_ID,
	MTK_GMAC_ID_MAX
};

enum mtk_tx_buf_type {
	MTK_TYPE_SKB,
	MTK_TYPE_XDP_TX,
	MTK_TYPE_XDP_NDO,
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
	enum mtk_tx_buf_type type;
	void *data;

	u16 mac_id;
	u16 flags;
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
 * @last_free_ptr:	Hardware pointer value of the last free descriptor
 * @thresh:		The threshold of minimum amount of free descriptors
 * @free_count:		QDMA uses a linked list. Track how many free descriptors
 *			are present
 */
struct mtk_tx_ring {
	void *dma;
	struct mtk_tx_buf *buf;
	dma_addr_t phys;
	struct mtk_tx_dma *next_free;
	struct mtk_tx_dma *last_free;
	u32 last_free_ptr;
	u16 thresh;
	atomic_t free_count;
	int dma_size;
	struct mtk_tx_dma *dma_pdma;	/* For MT7628/88 PDMA handling */
	dma_addr_t phys_pdma;
	int cpu_idx;
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
	void *dma;
	u8 **data;
	dma_addr_t phys;
	u16 frag_size;
	u16 buf_size;
	u16 dma_size;
	bool calc_idx_update;
	u16 calc_idx;
	u32 crx_idx_reg;
	/* page_pool */
	struct page_pool *page_pool;
	struct xdp_rxq_info xdp_q;
};

enum mkt_eth_capabilities {
	MTK_RGMII_BIT = 0,
	MTK_TRGMII_BIT,
	MTK_SGMII_BIT,
	MTK_ESW_BIT,
	MTK_GEPHY_BIT,
	MTK_MUX_BIT,
	MTK_INFRA_BIT,
	MTK_SHARED_SGMII_BIT,
	MTK_HWLRO_BIT,
	MTK_SHARED_INT_BIT,
	MTK_TRGMII_MT7621_CLK_BIT,
	MTK_QDMA_BIT,
	MTK_SOC_MT7628_BIT,
	MTK_RSTCTRL_PPE1_BIT,
	MTK_RSTCTRL_PPE2_BIT,
	MTK_U3_COPHY_V2_BIT,
	MTK_SRAM_BIT,
	MTK_36BIT_DMA_BIT,

	/* MUX BITS*/
	MTK_ETH_MUX_GDM1_TO_GMAC1_ESW_BIT,
	MTK_ETH_MUX_GMAC2_GMAC0_TO_GEPHY_BIT,
	MTK_ETH_MUX_U3_GMAC2_TO_QPHY_BIT,
	MTK_ETH_MUX_GMAC1_GMAC2_TO_SGMII_RGMII_BIT,
	MTK_ETH_MUX_GMAC12_TO_GEPHY_SGMII_BIT,

	/* PATH BITS */
	MTK_ETH_PATH_GMAC1_RGMII_BIT,
	MTK_ETH_PATH_GMAC1_TRGMII_BIT,
	MTK_ETH_PATH_GMAC1_SGMII_BIT,
	MTK_ETH_PATH_GMAC2_RGMII_BIT,
	MTK_ETH_PATH_GMAC2_SGMII_BIT,
	MTK_ETH_PATH_GMAC2_GEPHY_BIT,
	MTK_ETH_PATH_GDM1_ESW_BIT,
};

/* Supported hardware group on SoCs */
#define MTK_RGMII		BIT_ULL(MTK_RGMII_BIT)
#define MTK_TRGMII		BIT_ULL(MTK_TRGMII_BIT)
#define MTK_SGMII		BIT_ULL(MTK_SGMII_BIT)
#define MTK_ESW			BIT_ULL(MTK_ESW_BIT)
#define MTK_GEPHY		BIT_ULL(MTK_GEPHY_BIT)
#define MTK_MUX			BIT_ULL(MTK_MUX_BIT)
#define MTK_INFRA		BIT_ULL(MTK_INFRA_BIT)
#define MTK_SHARED_SGMII	BIT_ULL(MTK_SHARED_SGMII_BIT)
#define MTK_HWLRO		BIT_ULL(MTK_HWLRO_BIT)
#define MTK_SHARED_INT		BIT_ULL(MTK_SHARED_INT_BIT)
#define MTK_TRGMII_MT7621_CLK	BIT_ULL(MTK_TRGMII_MT7621_CLK_BIT)
#define MTK_QDMA		BIT_ULL(MTK_QDMA_BIT)
#define MTK_SOC_MT7628		BIT_ULL(MTK_SOC_MT7628_BIT)
#define MTK_RSTCTRL_PPE1	BIT_ULL(MTK_RSTCTRL_PPE1_BIT)
#define MTK_RSTCTRL_PPE2	BIT_ULL(MTK_RSTCTRL_PPE2_BIT)
#define MTK_U3_COPHY_V2		BIT_ULL(MTK_U3_COPHY_V2_BIT)
#define MTK_SRAM		BIT_ULL(MTK_SRAM_BIT)
#define MTK_36BIT_DMA	BIT_ULL(MTK_36BIT_DMA_BIT)

#define MTK_ETH_MUX_GDM1_TO_GMAC1_ESW		\
	BIT_ULL(MTK_ETH_MUX_GDM1_TO_GMAC1_ESW_BIT)
#define MTK_ETH_MUX_GMAC2_GMAC0_TO_GEPHY	\
	BIT_ULL(MTK_ETH_MUX_GMAC2_GMAC0_TO_GEPHY_BIT)
#define MTK_ETH_MUX_U3_GMAC2_TO_QPHY		\
	BIT_ULL(MTK_ETH_MUX_U3_GMAC2_TO_QPHY_BIT)
#define MTK_ETH_MUX_GMAC1_GMAC2_TO_SGMII_RGMII	\
	BIT_ULL(MTK_ETH_MUX_GMAC1_GMAC2_TO_SGMII_RGMII_BIT)
#define MTK_ETH_MUX_GMAC12_TO_GEPHY_SGMII	\
	BIT_ULL(MTK_ETH_MUX_GMAC12_TO_GEPHY_SGMII_BIT)

/* Supported path present on SoCs */
#define MTK_ETH_PATH_GMAC1_RGMII	BIT_ULL(MTK_ETH_PATH_GMAC1_RGMII_BIT)
#define MTK_ETH_PATH_GMAC1_TRGMII	BIT_ULL(MTK_ETH_PATH_GMAC1_TRGMII_BIT)
#define MTK_ETH_PATH_GMAC1_SGMII	BIT_ULL(MTK_ETH_PATH_GMAC1_SGMII_BIT)
#define MTK_ETH_PATH_GMAC2_RGMII	BIT_ULL(MTK_ETH_PATH_GMAC2_RGMII_BIT)
#define MTK_ETH_PATH_GMAC2_SGMII	BIT_ULL(MTK_ETH_PATH_GMAC2_SGMII_BIT)
#define MTK_ETH_PATH_GMAC2_GEPHY	BIT_ULL(MTK_ETH_PATH_GMAC2_GEPHY_BIT)
#define MTK_ETH_PATH_GDM1_ESW		BIT_ULL(MTK_ETH_PATH_GDM1_ESW_BIT)

#define MTK_GMAC1_RGMII		(MTK_ETH_PATH_GMAC1_RGMII | MTK_RGMII)
#define MTK_GMAC1_TRGMII	(MTK_ETH_PATH_GMAC1_TRGMII | MTK_TRGMII)
#define MTK_GMAC1_SGMII		(MTK_ETH_PATH_GMAC1_SGMII | MTK_SGMII)
#define MTK_GMAC2_RGMII		(MTK_ETH_PATH_GMAC2_RGMII | MTK_RGMII)
#define MTK_GMAC2_SGMII		(MTK_ETH_PATH_GMAC2_SGMII | MTK_SGMII)
#define MTK_GMAC2_GEPHY		(MTK_ETH_PATH_GMAC2_GEPHY | MTK_GEPHY)
#define MTK_GDM1_ESW		(MTK_ETH_PATH_GDM1_ESW | MTK_ESW)

/* MUXes present on SoCs */
/* 0: GDM1 -> GMAC1, 1: GDM1 -> ESW */
#define MTK_MUX_GDM1_TO_GMAC1_ESW (MTK_ETH_MUX_GDM1_TO_GMAC1_ESW | MTK_MUX)

/* 0: GMAC2 -> GEPHY, 1: GMAC0 -> GePHY */
#define MTK_MUX_GMAC2_GMAC0_TO_GEPHY    \
	(MTK_ETH_MUX_GMAC2_GMAC0_TO_GEPHY | MTK_MUX | MTK_INFRA)

/* 0: U3 -> QPHY, 1: GMAC2 -> QPHY */
#define MTK_MUX_U3_GMAC2_TO_QPHY        \
	(MTK_ETH_MUX_U3_GMAC2_TO_QPHY | MTK_MUX | MTK_INFRA)

/* 2: GMAC1 -> SGMII, 3: GMAC2 -> SGMII */
#define MTK_MUX_GMAC1_GMAC2_TO_SGMII_RGMII      \
	(MTK_ETH_MUX_GMAC1_GMAC2_TO_SGMII_RGMII | MTK_MUX | \
	MTK_SHARED_SGMII)

/* 0: GMACx -> GEPHY, 1: GMACx -> SGMII where x is 1 or 2 */
#define MTK_MUX_GMAC12_TO_GEPHY_SGMII   \
	(MTK_ETH_MUX_GMAC12_TO_GEPHY_SGMII | MTK_MUX)

#define MTK_HAS_CAPS(caps, _x)		(((caps) & (_x)) == (_x))

#define MT7621_CAPS  (MTK_GMAC1_RGMII | MTK_GMAC1_TRGMII | \
		      MTK_GMAC2_RGMII | MTK_SHARED_INT | \
		      MTK_TRGMII_MT7621_CLK | MTK_QDMA)

#define MT7622_CAPS  (MTK_GMAC1_RGMII | MTK_GMAC1_SGMII | MTK_GMAC2_RGMII | \
		      MTK_GMAC2_SGMII | MTK_GDM1_ESW | \
		      MTK_MUX_GDM1_TO_GMAC1_ESW | \
		      MTK_MUX_GMAC1_GMAC2_TO_SGMII_RGMII | MTK_QDMA)

#define MT7623_CAPS  (MTK_GMAC1_RGMII | MTK_GMAC1_TRGMII | MTK_GMAC2_RGMII | \
		      MTK_QDMA)

#define MT7628_CAPS  (MTK_SHARED_INT | MTK_SOC_MT7628)

#define MT7629_CAPS  (MTK_GMAC1_SGMII | MTK_GMAC2_SGMII | MTK_GMAC2_GEPHY | \
		      MTK_GDM1_ESW | MTK_MUX_GDM1_TO_GMAC1_ESW | \
		      MTK_MUX_GMAC2_GMAC0_TO_GEPHY | \
		      MTK_MUX_U3_GMAC2_TO_QPHY | \
		      MTK_MUX_GMAC12_TO_GEPHY_SGMII | MTK_QDMA)

#define MT7981_CAPS  (MTK_GMAC1_SGMII | MTK_GMAC2_SGMII | MTK_GMAC2_GEPHY | \
		      MTK_MUX_GMAC12_TO_GEPHY_SGMII | MTK_QDMA | \
		      MTK_MUX_U3_GMAC2_TO_QPHY | MTK_U3_COPHY_V2 | \
		      MTK_RSTCTRL_PPE1 | MTK_SRAM)

#define MT7986_CAPS  (MTK_GMAC1_SGMII | MTK_GMAC2_SGMII | \
		      MTK_MUX_GMAC12_TO_GEPHY_SGMII | MTK_QDMA | \
		      MTK_RSTCTRL_PPE1 | MTK_SRAM)

#define MT7988_CAPS  (MTK_36BIT_DMA | MTK_GDM1_ESW | MTK_QDMA | \
		      MTK_RSTCTRL_PPE1 | MTK_RSTCTRL_PPE2 | MTK_SRAM)

struct mtk_tx_dma_desc_info {
	dma_addr_t	addr;
	u32		size;
	u16		vlan_tci;
	u16		qid;
	u8		gso:1;
	u8		csum:1;
	u8		vlan:1;
	u8		first:1;
	u8		last:1;
};

struct mtk_reg_map {
	u32	tx_irq_mask;
	u32	tx_irq_status;
	struct {
		u32	rx_ptr;		/* rx base pointer */
		u32	rx_cnt_cfg;	/* rx max count configuration */
		u32	pcrx_ptr;	/* rx cpu pointer */
		u32	glo_cfg;	/* global configuration */
		u32	rst_idx;	/* reset index */
		u32	delay_irq;	/* delay interrupt */
		u32	irq_status;	/* interrupt status */
		u32	irq_mask;	/* interrupt mask */
		u32	adma_rx_dbg0;
		u32	int_grp;
	} pdma;
	struct {
		u32	qtx_cfg;	/* tx queue configuration */
		u32	qtx_sch;	/* tx queue scheduler configuration */
		u32	rx_ptr;		/* rx base pointer */
		u32	rx_cnt_cfg;	/* rx max count configuration */
		u32	qcrx_ptr;	/* rx cpu pointer */
		u32	glo_cfg;	/* global configuration */
		u32	rst_idx;	/* reset index */
		u32	delay_irq;	/* delay interrupt */
		u32	fc_th;		/* flow control */
		u32	int_grp;
		u32	hred;		/* interrupt mask */
		u32	ctx_ptr;	/* tx acquire cpu pointer */
		u32	dtx_ptr;	/* tx acquire dma pointer */
		u32	crx_ptr;	/* tx release cpu pointer */
		u32	drx_ptr;	/* tx release dma pointer */
		u32	fq_head;	/* fq head pointer */
		u32	fq_tail;	/* fq tail pointer */
		u32	fq_count;	/* fq free page count */
		u32	fq_blen;	/* fq free page buffer length */
		u32	tx_sch_rate;	/* tx scheduler rate control registers */
	} qdma;
	u32	gdm1_cnt;
	u32	gdma_to_ppe[3];
	u32	ppe_base;
	u32	wdma_base[3];
	u32	pse_iq_sta;
	u32	pse_oq_sta;
};

/* struct mtk_eth_data -	This is the structure holding all differences
 *				among various plaforms
 * @reg_map			Soc register map.
 * @ana_rgc3:                   The offset for register ANA_RGC3 related to
 *				sgmiisys syscon
 * @caps			Flags shown the extra capability for the SoC
 * @hw_features			Flags shown HW features
 * @required_clks		Flags shown the bitmap for required clocks on
 *				the target SoC
 * @required_pctl		A bool value to show whether the SoC requires
 *				the extra setup for those pins used by GMAC.
 * @hash_offset			Flow table hash offset.
 * @version			SoC version.
 * @foe_entry_size		Foe table entry size.
 * @has_accounting		Bool indicating support for accounting of
 *				offloaded flows.
 * @desc_size			Tx/Rx DMA descriptor size.
 * @irq_done_mask		Rx irq done register mask.
 * @dma_l4_valid		Rx DMA valid register mask.
 * @dma_max_len			Max DMA tx/rx buffer length.
 * @dma_len_offset		Tx/Rx DMA length field offset.
 */
struct mtk_soc_data {
	const struct mtk_reg_map *reg_map;
	u32             ana_rgc3;
	u64		caps;
	u64		required_clks;
	bool		required_pctl;
	u8		offload_version;
	u8		hash_offset;
	u8		version;
	u8		ppe_num;
	u16		foe_entry_size;
	netdev_features_t hw_features;
	bool		has_accounting;
	bool		disable_pll_modes;
	struct {
		u32	desc_size;
		u32	dma_max_len;
		u32	dma_len_offset;
		u32	dma_size;
		u32	fq_dma_size;
	} tx;
	struct {
		u32	desc_size;
		u32	irq_done_mask;
		u32	dma_l4_valid;
		u32	dma_max_len;
		u32	dma_len_offset;
		u32	dma_size;
	} rx;
};

#define MTK_DMA_MONITOR_TIMEOUT		msecs_to_jiffies(1000)

/* currently no SoC has more than 3 macs */
#define MTK_MAX_DEVS	3

/* struct mtk_eth -	This is the main datasructure for holding the state
 *			of the driver
 * @dev:		The device pointer
 * @dev:		The device pointer used for dma mapping/alloc
 * @base:		The mapped register i/o base
 * @page_lock:		Make sure that register operations are atomic
 * @tx_irq__lock:	Make sure that IRQ register operations are atomic
 * @rx_irq__lock:	Make sure that IRQ register operations are atomic
 * @dim_lock:		Make sure that Net DIM operations are atomic
 * @dummy_dev:		we run 2 netdevs on 1 physical DMA ring and need a
 *			dummy for NAPI to work
 * @netdev:		The netdev instances
 * @mac:		Each netdev is linked to a physical MAC
 * @irq:		The IRQ that we are using
 * @msg_enable:		Ethtool msg level
 * @ethsys:		The register map pointing at the range used to setup
 *			MII modes
 * @infra:              The register map pointing at the range used to setup
 *                      SGMII and GePHY path
 * @sgmii_pcs:		Pointers to mtk-pcs-lynxi phylink_pcs instances
 * @pctl:		The register map pointing at the range used to setup
 *			GMAC port drive/slew values
 * @dma_refcnt:		track how many netdevs are using the DMA engine
 * @tx_ring:		Pointer to the memory holding info about the TX ring
 * @rx_ring:		Pointer to the memory holding info about the RX ring
 * @rx_ring_qdma:	Pointer to the memory holding info about the QDMA RX ring
 * @tx_napi:		The TX NAPI struct
 * @rx_napi:		The RX NAPI struct
 * @rx_events:		Net DIM RX event counter
 * @rx_packets:		Net DIM RX packet counter
 * @rx_bytes:		Net DIM RX byte counter
 * @rx_dim:		Net DIM RX context
 * @tx_events:		Net DIM TX event counter
 * @tx_packets:		Net DIM TX packet counter
 * @tx_bytes:		Net DIM TX byte counter
 * @tx_dim:		Net DIM TX context
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
	struct device			*dma_dev;
	void __iomem			*base;
	void				*sram_base;
	spinlock_t			page_lock;
	spinlock_t			tx_irq_lock;
	spinlock_t			rx_irq_lock;
	struct net_device		*dummy_dev;
	struct net_device		*netdev[MTK_MAX_DEVS];
	struct mtk_mac			*mac[MTK_MAX_DEVS];
	int				irq[3];
	u32				msg_enable;
	unsigned long			sysclk;
	struct regmap			*ethsys;
	struct regmap			*infra;
	struct phylink_pcs		*sgmii_pcs[MTK_MAX_DEVS];
	struct regmap			*pctl;
	bool				hwlro;
	refcount_t			dma_refcnt;
	struct mtk_tx_ring		tx_ring;
	struct mtk_rx_ring		rx_ring[MTK_MAX_RX_RING_NUM];
	struct mtk_rx_ring		rx_ring_qdma;
	struct napi_struct		tx_napi;
	struct napi_struct		rx_napi;
	void				*scratch_ring;
	dma_addr_t			phy_scratch_ring;
	void				*scratch_head[MTK_FQ_DMA_HEAD];
	struct clk			*clks[MTK_CLK_MAX];

	struct mii_bus			*mii_bus;
	struct work_struct		pending_work;
	unsigned long			state;

	const struct mtk_soc_data	*soc;

	spinlock_t			dim_lock;

	u32				rx_events;
	u32				rx_packets;
	u32				rx_bytes;
	struct dim			rx_dim;

	u32				tx_events;
	u32				tx_packets;
	u32				tx_bytes;
	struct dim			tx_dim;

	int				ip_align;

	struct metadata_dst		*dsa_meta[MTK_MAX_DSA_PORTS];

	struct mtk_ppe			*ppe[3];
	struct rhashtable		flow_table;

	struct bpf_prog			__rcu *prog;

	struct {
		struct delayed_work monitor_work;
		u32 wdidx;
		u8 wdma_hang_count;
		u8 qdma_hang_count;
		u8 adma_hang_count;
	} reset;
};

/* struct mtk_mac -	the structure that holds the info about the MACs of the
 *			SoC
 * @id:			The number of the MAC
 * @interface:		Interface mode kept for detecting change in hw settings
 * @of_node:		Our devicetree node
 * @hw:			Backpointer to our main datastruture
 * @hw_stats:		Packet statistics counter
 */
struct mtk_mac {
	int				id;
	phy_interface_t			interface;
	u8				ppe_idx;
	int				speed;
	struct device_node		*of_node;
	struct phylink			*phylink;
	struct phylink_config		phylink_config;
	struct mtk_eth			*hw;
	struct mtk_hw_stats		*hw_stats;
	__be32				hwlro_ip[MTK_MAX_LRO_IP_CNT];
	int				hwlro_ip_cnt;
	unsigned int			syscfg0;
	struct notifier_block		device_notifier;
};

/* the struct describing the SoC. these are declared in the soc_xyz.c files */
extern const struct of_device_id of_mtk_match[];

static inline bool mtk_is_netsys_v1(struct mtk_eth *eth)
{
	return eth->soc->version == 1;
}

static inline bool mtk_is_netsys_v2_or_greater(struct mtk_eth *eth)
{
	return eth->soc->version > 1;
}

static inline bool mtk_is_netsys_v3_or_greater(struct mtk_eth *eth)
{
	return eth->soc->version > 2;
}

static inline struct mtk_foe_entry *
mtk_foe_get_entry(struct mtk_ppe *ppe, u16 hash)
{
	const struct mtk_soc_data *soc = ppe->eth->soc;

	return ppe->foe_table + hash * soc->foe_entry_size;
}

static inline u32 mtk_get_ib1_ts_mask(struct mtk_eth *eth)
{
	if (mtk_is_netsys_v2_or_greater(eth))
		return MTK_FOE_IB1_BIND_TIMESTAMP_V2;

	return MTK_FOE_IB1_BIND_TIMESTAMP;
}

static inline u32 mtk_get_ib1_ppoe_mask(struct mtk_eth *eth)
{
	if (mtk_is_netsys_v2_or_greater(eth))
		return MTK_FOE_IB1_BIND_PPPOE_V2;

	return MTK_FOE_IB1_BIND_PPPOE;
}

static inline u32 mtk_get_ib1_vlan_tag_mask(struct mtk_eth *eth)
{
	if (mtk_is_netsys_v2_or_greater(eth))
		return MTK_FOE_IB1_BIND_VLAN_TAG_V2;

	return MTK_FOE_IB1_BIND_VLAN_TAG;
}

static inline u32 mtk_get_ib1_vlan_layer_mask(struct mtk_eth *eth)
{
	if (mtk_is_netsys_v2_or_greater(eth))
		return MTK_FOE_IB1_BIND_VLAN_LAYER_V2;

	return MTK_FOE_IB1_BIND_VLAN_LAYER;
}

static inline u32 mtk_prep_ib1_vlan_layer(struct mtk_eth *eth, u32 val)
{
	if (mtk_is_netsys_v2_or_greater(eth))
		return FIELD_PREP(MTK_FOE_IB1_BIND_VLAN_LAYER_V2, val);

	return FIELD_PREP(MTK_FOE_IB1_BIND_VLAN_LAYER, val);
}

static inline u32 mtk_get_ib1_vlan_layer(struct mtk_eth *eth, u32 val)
{
	if (mtk_is_netsys_v2_or_greater(eth))
		return FIELD_GET(MTK_FOE_IB1_BIND_VLAN_LAYER_V2, val);

	return FIELD_GET(MTK_FOE_IB1_BIND_VLAN_LAYER, val);
}

static inline u32 mtk_get_ib1_pkt_type_mask(struct mtk_eth *eth)
{
	if (mtk_is_netsys_v2_or_greater(eth))
		return MTK_FOE_IB1_PACKET_TYPE_V2;

	return MTK_FOE_IB1_PACKET_TYPE;
}

static inline u32 mtk_get_ib1_pkt_type(struct mtk_eth *eth, u32 val)
{
	if (mtk_is_netsys_v2_or_greater(eth))
		return FIELD_GET(MTK_FOE_IB1_PACKET_TYPE_V2, val);

	return FIELD_GET(MTK_FOE_IB1_PACKET_TYPE, val);
}

static inline u32 mtk_get_ib2_multicast_mask(struct mtk_eth *eth)
{
	if (mtk_is_netsys_v2_or_greater(eth))
		return MTK_FOE_IB2_MULTICAST_V2;

	return MTK_FOE_IB2_MULTICAST;
}

/* read the hardware status register */
void mtk_stats_update_mac(struct mtk_mac *mac);

void mtk_w32(struct mtk_eth *eth, u32 val, unsigned reg);
u32 mtk_r32(struct mtk_eth *eth, unsigned reg);
u32 mtk_m32(struct mtk_eth *eth, u32 mask, u32 set, unsigned int reg);

int mtk_gmac_sgmii_path_setup(struct mtk_eth *eth, int mac_id);
int mtk_gmac_gephy_path_setup(struct mtk_eth *eth, int mac_id);
int mtk_gmac_rgmii_path_setup(struct mtk_eth *eth, int mac_id);

int mtk_eth_offload_init(struct mtk_eth *eth, u8 id);
int mtk_eth_setup_tc(struct net_device *dev, enum tc_setup_type type,
		     void *type_data);
int mtk_flow_offload_cmd(struct mtk_eth *eth, struct flow_cls_offload *cls,
			 int ppe_index);
void mtk_flow_offload_cleanup(struct mtk_eth *eth, struct list_head *list);
void mtk_eth_set_dma_device(struct mtk_eth *eth, struct device *dma_dev);


#endif /* MTK_ETH_H */
