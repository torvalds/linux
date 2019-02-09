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

#include <linux/mii.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/dma-mapping.h>
#include <linux/phy.h>
#include <linux/ethtool.h>
#include <linux/version.h>
#include <linux/atomic.h>

/* these registers have different offsets depending on the SoC. we use a lookup
 * table for these
 */
enum mtk_reg {
	MTK_REG_PDMA_GLO_CFG = 0,
	MTK_REG_PDMA_RST_CFG,
	MTK_REG_DLY_INT_CFG,
	MTK_REG_TX_BASE_PTR0,
	MTK_REG_TX_MAX_CNT0,
	MTK_REG_TX_CTX_IDX0,
	MTK_REG_TX_DTX_IDX0,
	MTK_REG_RX_BASE_PTR0,
	MTK_REG_RX_MAX_CNT0,
	MTK_REG_RX_CALC_IDX0,
	MTK_REG_RX_DRX_IDX0,
	MTK_REG_MTK_INT_ENABLE,
	MTK_REG_MTK_INT_STATUS,
	MTK_REG_MTK_DMA_VID_BASE,
	MTK_REG_MTK_COUNTER_BASE,
	MTK_REG_MTK_RST_GL,
	MTK_REG_MTK_INT_STATUS2,
	MTK_REG_COUNT
};

/* delayed interrupt bits */
#define MTK_DELAY_EN_INT	0x80
#define MTK_DELAY_MAX_INT	0x04
#define MTK_DELAY_MAX_TOUT	0x04
#define MTK_DELAY_TIME		20
#define MTK_DELAY_CHAN		(((MTK_DELAY_EN_INT | MTK_DELAY_MAX_INT) << 8) \
				 | MTK_DELAY_MAX_TOUT)
#define MTK_DELAY_INIT		((MTK_DELAY_CHAN << 16) | MTK_DELAY_CHAN)
#define MTK_PSE_FQFC_CFG_INIT	0x80504000
#define MTK_PSE_FQFC_CFG_256Q	0xff908000

/* interrupt bits */
#define MTK_CNT_PPE_AF		BIT(31)
#define MTK_CNT_GDM_AF		BIT(29)
#define MTK_PSE_P2_FC		BIT(26)
#define MTK_PSE_BUF_DROP	BIT(24)
#define MTK_GDM_OTHER_DROP	BIT(23)
#define MTK_PSE_P1_FC		BIT(22)
#define MTK_PSE_P0_FC		BIT(21)
#define MTK_PSE_FQ_EMPTY	BIT(20)
#define MTK_GE1_STA_CHG		BIT(18)
#define MTK_TX_COHERENT		BIT(17)
#define MTK_RX_COHERENT		BIT(16)
#define MTK_TX_DONE_INT3	BIT(11)
#define MTK_TX_DONE_INT2	BIT(10)
#define MTK_TX_DONE_INT1	BIT(9)
#define MTK_TX_DONE_INT0	BIT(8)
#define MTK_RX_DONE_INT0	BIT(2)
#define MTK_TX_DLY_INT		BIT(1)
#define MTK_RX_DLY_INT		BIT(0)

#define MTK_RX_DONE_INT		MTK_RX_DONE_INT0
#define MTK_TX_DONE_INT		(MTK_TX_DONE_INT0 | MTK_TX_DONE_INT1 | \
				 MTK_TX_DONE_INT2 | MTK_TX_DONE_INT3)

#define RT5350_RX_DLY_INT	BIT(30)
#define RT5350_TX_DLY_INT	BIT(28)
#define RT5350_RX_DONE_INT1	BIT(17)
#define RT5350_RX_DONE_INT0	BIT(16)
#define RT5350_TX_DONE_INT3	BIT(3)
#define RT5350_TX_DONE_INT2	BIT(2)
#define RT5350_TX_DONE_INT1	BIT(1)
#define RT5350_TX_DONE_INT0	BIT(0)

#define RT5350_RX_DONE_INT	(RT5350_RX_DONE_INT0 | RT5350_RX_DONE_INT1)
#define RT5350_TX_DONE_INT	(RT5350_TX_DONE_INT0 | RT5350_TX_DONE_INT1 | \
				 RT5350_TX_DONE_INT2 | RT5350_TX_DONE_INT3)

/* registers */
#define MTK_GDMA_OFFSET		0x0020
#define MTK_PSE_OFFSET		0x0040
#define MTK_GDMA2_OFFSET	0x0060
#define MTK_CDMA_OFFSET		0x0080
#define MTK_DMA_VID0		0x00a8
#define MTK_PDMA_OFFSET		0x0100
#define MTK_PPE_OFFSET		0x0200
#define MTK_CMTABLE_OFFSET	0x0400
#define MTK_POLICYTABLE_OFFSET	0x1000

#define MT7621_GDMA_OFFSET	0x0500
#define MT7620_GDMA_OFFSET	0x0600

#define RT5350_PDMA_OFFSET	0x0800
#define RT5350_SDM_OFFSET	0x0c00

#define MTK_MDIO_ACCESS		0x00
#define MTK_MDIO_CFG		0x04
#define MTK_GLO_CFG		0x08
#define MTK_RST_GL		0x0C
#define MTK_INT_STATUS		0x10
#define MTK_INT_ENABLE		0x14
#define MTK_MDIO_CFG2		0x18
#define MTK_FOC_TS_T		0x1C

#define	MTK_GDMA1_FWD_CFG	(MTK_GDMA_OFFSET + 0x00)
#define MTK_GDMA1_SCH_CFG	(MTK_GDMA_OFFSET + 0x04)
#define MTK_GDMA1_SHPR_CFG	(MTK_GDMA_OFFSET + 0x08)
#define MTK_GDMA1_MAC_ADRL	(MTK_GDMA_OFFSET + 0x0C)
#define MTK_GDMA1_MAC_ADRH	(MTK_GDMA_OFFSET + 0x10)

#define	MTK_GDMA2_FWD_CFG	(MTK_GDMA2_OFFSET + 0x00)
#define MTK_GDMA2_SCH_CFG	(MTK_GDMA2_OFFSET + 0x04)
#define MTK_GDMA2_SHPR_CFG	(MTK_GDMA2_OFFSET + 0x08)
#define MTK_GDMA2_MAC_ADRL	(MTK_GDMA2_OFFSET + 0x0C)
#define MTK_GDMA2_MAC_ADRH	(MTK_GDMA2_OFFSET + 0x10)

#define MTK_PSE_FQ_CFG		(MTK_PSE_OFFSET + 0x00)
#define MTK_CDMA_FC_CFG		(MTK_PSE_OFFSET + 0x04)
#define MTK_GDMA1_FC_CFG	(MTK_PSE_OFFSET + 0x08)
#define MTK_GDMA2_FC_CFG	(MTK_PSE_OFFSET + 0x0C)

#define MTK_CDMA_CSG_CFG	(MTK_CDMA_OFFSET + 0x00)
#define MTK_CDMA_SCH_CFG	(MTK_CDMA_OFFSET + 0x04)

#define	MT7621_GDMA_FWD_CFG(x)	(MT7621_GDMA_OFFSET + (x * 0x1000))

/* FIXME this might be different for different SOCs */
#define	MT7620_GDMA1_FWD_CFG	(MT7621_GDMA_OFFSET + 0x00)

#define RT5350_TX_BASE_PTR0	(RT5350_PDMA_OFFSET + 0x00)
#define RT5350_TX_MAX_CNT0	(RT5350_PDMA_OFFSET + 0x04)
#define RT5350_TX_CTX_IDX0	(RT5350_PDMA_OFFSET + 0x08)
#define RT5350_TX_DTX_IDX0	(RT5350_PDMA_OFFSET + 0x0C)
#define RT5350_TX_BASE_PTR1	(RT5350_PDMA_OFFSET + 0x10)
#define RT5350_TX_MAX_CNT1	(RT5350_PDMA_OFFSET + 0x14)
#define RT5350_TX_CTX_IDX1	(RT5350_PDMA_OFFSET + 0x18)
#define RT5350_TX_DTX_IDX1	(RT5350_PDMA_OFFSET + 0x1C)
#define RT5350_TX_BASE_PTR2	(RT5350_PDMA_OFFSET + 0x20)
#define RT5350_TX_MAX_CNT2	(RT5350_PDMA_OFFSET + 0x24)
#define RT5350_TX_CTX_IDX2	(RT5350_PDMA_OFFSET + 0x28)
#define RT5350_TX_DTX_IDX2	(RT5350_PDMA_OFFSET + 0x2C)
#define RT5350_TX_BASE_PTR3	(RT5350_PDMA_OFFSET + 0x30)
#define RT5350_TX_MAX_CNT3	(RT5350_PDMA_OFFSET + 0x34)
#define RT5350_TX_CTX_IDX3	(RT5350_PDMA_OFFSET + 0x38)
#define RT5350_TX_DTX_IDX3	(RT5350_PDMA_OFFSET + 0x3C)
#define RT5350_RX_BASE_PTR0	(RT5350_PDMA_OFFSET + 0x100)
#define RT5350_RX_MAX_CNT0	(RT5350_PDMA_OFFSET + 0x104)
#define RT5350_RX_CALC_IDX0	(RT5350_PDMA_OFFSET + 0x108)
#define RT5350_RX_DRX_IDX0	(RT5350_PDMA_OFFSET + 0x10C)
#define RT5350_RX_BASE_PTR1	(RT5350_PDMA_OFFSET + 0x110)
#define RT5350_RX_MAX_CNT1	(RT5350_PDMA_OFFSET + 0x114)
#define RT5350_RX_CALC_IDX1	(RT5350_PDMA_OFFSET + 0x118)
#define RT5350_RX_DRX_IDX1	(RT5350_PDMA_OFFSET + 0x11C)
#define RT5350_PDMA_GLO_CFG	(RT5350_PDMA_OFFSET + 0x204)
#define RT5350_PDMA_RST_CFG	(RT5350_PDMA_OFFSET + 0x208)
#define RT5350_DLY_INT_CFG	(RT5350_PDMA_OFFSET + 0x20c)
#define RT5350_MTK_INT_STATUS	(RT5350_PDMA_OFFSET + 0x220)
#define RT5350_MTK_INT_ENABLE	(RT5350_PDMA_OFFSET + 0x228)
#define RT5350_PDMA_SCH_CFG	(RT5350_PDMA_OFFSET + 0x280)

#define MTK_PDMA_GLO_CFG	(MTK_PDMA_OFFSET + 0x00)
#define MTK_PDMA_RST_CFG	(MTK_PDMA_OFFSET + 0x04)
#define MTK_PDMA_SCH_CFG	(MTK_PDMA_OFFSET + 0x08)
#define MTK_DLY_INT_CFG		(MTK_PDMA_OFFSET + 0x0C)
#define MTK_TX_BASE_PTR0	(MTK_PDMA_OFFSET + 0x10)
#define MTK_TX_MAX_CNT0		(MTK_PDMA_OFFSET + 0x14)
#define MTK_TX_CTX_IDX0		(MTK_PDMA_OFFSET + 0x18)
#define MTK_TX_DTX_IDX0		(MTK_PDMA_OFFSET + 0x1C)
#define MTK_TX_BASE_PTR1	(MTK_PDMA_OFFSET + 0x20)
#define MTK_TX_MAX_CNT1		(MTK_PDMA_OFFSET + 0x24)
#define MTK_TX_CTX_IDX1		(MTK_PDMA_OFFSET + 0x28)
#define MTK_TX_DTX_IDX1		(MTK_PDMA_OFFSET + 0x2C)
#define MTK_RX_BASE_PTR0	(MTK_PDMA_OFFSET + 0x30)
#define MTK_RX_MAX_CNT0		(MTK_PDMA_OFFSET + 0x34)
#define MTK_RX_CALC_IDX0	(MTK_PDMA_OFFSET + 0x38)
#define MTK_RX_DRX_IDX0		(MTK_PDMA_OFFSET + 0x3C)
#define MTK_TX_BASE_PTR2	(MTK_PDMA_OFFSET + 0x40)
#define MTK_TX_MAX_CNT2		(MTK_PDMA_OFFSET + 0x44)
#define MTK_TX_CTX_IDX2		(MTK_PDMA_OFFSET + 0x48)
#define MTK_TX_DTX_IDX2		(MTK_PDMA_OFFSET + 0x4C)
#define MTK_TX_BASE_PTR3	(MTK_PDMA_OFFSET + 0x50)
#define MTK_TX_MAX_CNT3		(MTK_PDMA_OFFSET + 0x54)
#define MTK_TX_CTX_IDX3		(MTK_PDMA_OFFSET + 0x58)
#define MTK_TX_DTX_IDX3		(MTK_PDMA_OFFSET + 0x5C)
#define MTK_RX_BASE_PTR1	(MTK_PDMA_OFFSET + 0x60)
#define MTK_RX_MAX_CNT1		(MTK_PDMA_OFFSET + 0x64)
#define MTK_RX_CALC_IDX1	(MTK_PDMA_OFFSET + 0x68)
#define MTK_RX_DRX_IDX1		(MTK_PDMA_OFFSET + 0x6C)

/* Switch DMA configuration */
#define RT5350_SDM_CFG		(RT5350_SDM_OFFSET + 0x00)
#define RT5350_SDM_RRING	(RT5350_SDM_OFFSET + 0x04)
#define RT5350_SDM_TRING	(RT5350_SDM_OFFSET + 0x08)
#define RT5350_SDM_MAC_ADRL	(RT5350_SDM_OFFSET + 0x0C)
#define RT5350_SDM_MAC_ADRH	(RT5350_SDM_OFFSET + 0x10)
#define RT5350_SDM_TPCNT	(RT5350_SDM_OFFSET + 0x100)
#define RT5350_SDM_TBCNT	(RT5350_SDM_OFFSET + 0x104)
#define RT5350_SDM_RPCNT	(RT5350_SDM_OFFSET + 0x108)
#define RT5350_SDM_RBCNT	(RT5350_SDM_OFFSET + 0x10C)
#define RT5350_SDM_CS_ERR	(RT5350_SDM_OFFSET + 0x110)

#define RT5350_SDM_ICS_EN	BIT(16)
#define RT5350_SDM_TCS_EN	BIT(17)
#define RT5350_SDM_UCS_EN	BIT(18)

/* QDMA registers */
#define MTK_QTX_CFG(x)		(0x1800 + (x * 0x10))
#define MTK_QTX_SCH(x)		(0x1804 + (x * 0x10))
#define MTK_QRX_BASE_PTR0	0x1900
#define MTK_QRX_MAX_CNT0	0x1904
#define MTK_QRX_CRX_IDX0	0x1908
#define MTK_QRX_DRX_IDX0	0x190C
#define MTK_QDMA_GLO_CFG	0x1A04
#define MTK_QDMA_RST_IDX	0x1A08
#define MTK_QDMA_DELAY_INT	0x1A0C
#define MTK_QDMA_FC_THRES	0x1A10
#define MTK_QMTK_INT_STATUS	0x1A18
#define MTK_QMTK_INT_ENABLE	0x1A1C
#define MTK_QDMA_HRED2		0x1A44

#define MTK_QTX_CTX_PTR		0x1B00
#define MTK_QTX_DTX_PTR		0x1B04

#define MTK_QTX_CRX_PTR		0x1B10
#define MTK_QTX_DRX_PTR		0x1B14

#define MTK_QDMA_FQ_HEAD	0x1B20
#define MTK_QDMA_FQ_TAIL	0x1B24
#define MTK_QDMA_FQ_CNT		0x1B28
#define MTK_QDMA_FQ_BLEN	0x1B2C

#define QDMA_PAGE_SIZE		2048
#define QDMA_TX_OWNER_CPU	BIT(31)
#define QDMA_TX_SWC		BIT(14)
#define TX_QDMA_SDL(_x)		(((_x) & 0x3fff) << 16)
#define QDMA_RES_THRES		4

/* MDIO_CFG register bits */
#define MTK_MDIO_CFG_AUTO_POLL_EN	BIT(29)
#define MTK_MDIO_CFG_GP1_BP_EN		BIT(16)
#define MTK_MDIO_CFG_GP1_FRC_EN		BIT(15)
#define MTK_MDIO_CFG_GP1_SPEED_10	(0 << 13)
#define MTK_MDIO_CFG_GP1_SPEED_100	(1 << 13)
#define MTK_MDIO_CFG_GP1_SPEED_1000	(2 << 13)
#define MTK_MDIO_CFG_GP1_DUPLEX		BIT(12)
#define MTK_MDIO_CFG_GP1_FC_TX		BIT(11)
#define MTK_MDIO_CFG_GP1_FC_RX		BIT(10)
#define MTK_MDIO_CFG_GP1_LNK_DWN	BIT(9)
#define MTK_MDIO_CFG_GP1_AN_FAIL	BIT(8)
#define MTK_MDIO_CFG_MDC_CLK_DIV_1	(0 << 6)
#define MTK_MDIO_CFG_MDC_CLK_DIV_2	(1 << 6)
#define MTK_MDIO_CFG_MDC_CLK_DIV_4	(2 << 6)
#define MTK_MDIO_CFG_MDC_CLK_DIV_8	(3 << 6)
#define MTK_MDIO_CFG_TURBO_MII_FREQ	BIT(5)
#define MTK_MDIO_CFG_TURBO_MII_MODE	BIT(4)
#define MTK_MDIO_CFG_RX_CLK_SKEW_0	(0 << 2)
#define MTK_MDIO_CFG_RX_CLK_SKEW_200	(1 << 2)
#define MTK_MDIO_CFG_RX_CLK_SKEW_400	(2 << 2)
#define MTK_MDIO_CFG_RX_CLK_SKEW_INV	(3 << 2)
#define MTK_MDIO_CFG_TX_CLK_SKEW_0	0
#define MTK_MDIO_CFG_TX_CLK_SKEW_200	1
#define MTK_MDIO_CFG_TX_CLK_SKEW_400	2
#define MTK_MDIO_CFG_TX_CLK_SKEW_INV	3

/* uni-cast port */
#define MTK_GDM1_JMB_LEN_MASK	0xf
#define MTK_GDM1_JMB_LEN_SHIFT	28
#define MTK_GDM1_ICS_EN		BIT(22)
#define MTK_GDM1_TCS_EN		BIT(21)
#define MTK_GDM1_UCS_EN		BIT(20)
#define MTK_GDM1_JMB_EN		BIT(19)
#define MTK_GDM1_STRPCRC	BIT(16)
#define MTK_GDM1_UFRC_P_CPU	(0 << 12)
#define MTK_GDM1_UFRC_P_GDMA1	(1 << 12)
#define MTK_GDM1_UFRC_P_PPE	(6 << 12)

/* checksums */
#define MTK_ICS_GEN_EN		BIT(2)
#define MTK_UCS_GEN_EN		BIT(1)
#define MTK_TCS_GEN_EN		BIT(0)

/* dma mode */
#define MTK_PDMA		BIT(0)
#define MTK_QDMA		BIT(1)
#define MTK_PDMA_RX_QDMA_TX	(MTK_PDMA | MTK_QDMA)

/* dma ring */
#define MTK_PST_DRX_IDX0	BIT(16)
#define MTK_PST_DTX_IDX3	BIT(3)
#define MTK_PST_DTX_IDX2	BIT(2)
#define MTK_PST_DTX_IDX1	BIT(1)
#define MTK_PST_DTX_IDX0	BIT(0)

#define MTK_RX_2B_OFFSET	BIT(31)
#define MTK_TX_WB_DDONE		BIT(6)
#define MTK_RX_DMA_BUSY		BIT(3)
#define MTK_TX_DMA_BUSY		BIT(1)
#define MTK_RX_DMA_EN		BIT(2)
#define MTK_TX_DMA_EN		BIT(0)

#define MTK_PDMA_SIZE_4DWORDS	(0 << 4)
#define MTK_PDMA_SIZE_8DWORDS	(1 << 4)
#define MTK_PDMA_SIZE_16DWORDS	(2 << 4)

#define MTK_US_CYC_CNT_MASK	0xff
#define MTK_US_CYC_CNT_SHIFT	0x8
#define MTK_US_CYC_CNT_DIVISOR	1000000

/* PDMA descriptor rxd2 */
#define RX_DMA_DONE		BIT(31)
#define RX_DMA_LSO		BIT(30)
#define RX_DMA_PLEN0(_x)	(((_x) & 0x3fff) << 16)
#define RX_DMA_GET_PLEN0(_x)	(((_x) >> 16) & 0x3fff)
#define RX_DMA_TAG		BIT(15)

/* PDMA descriptor rxd3 */
#define RX_DMA_TPID(_x)		(((_x) >> 16) & 0xffff)
#define RX_DMA_VID(_x)		((_x) & 0xfff)

/* PDMA descriptor rxd4 */
#define RX_DMA_L4VALID		BIT(30)
#define RX_DMA_FPORT_SHIFT	19
#define RX_DMA_FPORT_MASK	0x7

struct mtk_rx_dma {
	unsigned int rxd1;
	unsigned int rxd2;
	unsigned int rxd3;
	unsigned int rxd4;
} __packed __aligned(4);

/* PDMA tx descriptor bits */
#define TX_DMA_BUF_LEN		0x3fff
#define TX_DMA_PLEN0_MASK	(TX_DMA_BUF_LEN << 16)
#define TX_DMA_PLEN0(_x)	(((_x) & TX_DMA_BUF_LEN) << 16)
#define TX_DMA_PLEN1(_x)	((_x) & TX_DMA_BUF_LEN)
#define TX_DMA_GET_PLEN0(_x)    (((_x) >> 16) & TX_DMA_BUF_LEN)
#define TX_DMA_GET_PLEN1(_x)    ((_x) & TX_DMA_BUF_LEN)
#define TX_DMA_LS1		BIT(14)
#define TX_DMA_LS0		BIT(30)
#define TX_DMA_DONE		BIT(31)
#define TX_DMA_FPORT_SHIFT	25
#define TX_DMA_FPORT_MASK	0x7
#define TX_DMA_INS_VLAN_MT7621	BIT(16)
#define TX_DMA_INS_VLAN		BIT(7)
#define TX_DMA_INS_PPPOE	BIT(12)
#define TX_DMA_TAG		BIT(15)
#define TX_DMA_TAG_MASK		BIT(15)
#define TX_DMA_QN(_x)		((_x) << 16)
#define TX_DMA_PN(_x)		((_x) << 24)
#define TX_DMA_QN_MASK		TX_DMA_QN(0x7)
#define TX_DMA_PN_MASK		TX_DMA_PN(0x7)
#define TX_DMA_UDF		BIT(20)
#define TX_DMA_CHKSUM		(0x7 << 29)
#define TX_DMA_TSO		BIT(28)
#define TX_DMA_DESP4_DEF	(TX_DMA_QN(3) | TX_DMA_PN(1))

/* frame engine counters */
#define MTK_PPE_AC_BCNT0	(MTK_CMTABLE_OFFSET + 0x00)
#define MTK_GDMA1_TX_GBCNT	(MTK_CMTABLE_OFFSET + 0x300)
#define MTK_GDMA2_TX_GBCNT	(MTK_GDMA1_TX_GBCNT + 0x40)

/* phy device flags */
#define MTK_PHY_FLAG_PORT	BIT(0)
#define MTK_PHY_FLAG_ATTACH	BIT(1)

struct mtk_tx_dma {
	unsigned int txd1;
	unsigned int txd2;
	unsigned int txd3;
	unsigned int txd4;
} __packed __aligned(4);

struct mtk_eth;
struct mtk_mac;

/* manage the attached phys */
struct mtk_phy {
	spinlock_t		lock;

	struct phy_device	*phy[8];
	struct device_node	*phy_node[8];
	const __be32		*phy_fixed[8];
	int			duplex[8];
	int			speed[8];
	int			tx_fc[8];
	int			rx_fc[8];
	int (*connect)(struct mtk_mac *mac);
	void (*disconnect)(struct mtk_mac *mac);
	void (*start)(struct mtk_mac *mac);
	void (*stop)(struct mtk_mac *mac);
};

/* struct mtk_soc_data - the structure that holds the SoC specific data
 * @reg_table:		Some of the legacy registers changed their location
 *			over time. Their offsets are stored in this table
 *
 * @init_data:		Some features depend on the silicon revision. This
 *			callback allows runtime modification of the content of
 *			this struct
 * @reset_fe:		This callback is used to trigger the reset of the frame
 *			engine
 * @set_mac:		This callback is used to set the unicast mac address
 *			filter
 * @fwd_config:		This callback is used to setup the forward config
 *			register of the MAC
 * @switch_init:	This callback is used to bring up the switch core
 * @port_init:		Some SoCs have ports that can be router to a switch port
 *			or an external PHY. This callback is used to setup these
 *			ports.
 * @has_carrier:	This callback allows driver to check if there is a cable
 *			attached.
 * @mdio_init:		This callbck is used to setup the MDIO bus if one is
 *			present
 * @mdio_cleanup:	This callback is used to cleanup the MDIO state.
 * @mdio_write:		This callback is used to write data to the MDIO bus.
 * @mdio_read:		This callback is used to write data to the MDIO bus.
 * @mdio_adjust_link:	This callback is used to apply the PHY settings.
 * @piac_offset:	the PIAC register has a different different base offset
 * @hw_features:	feature set depends on the SoC type
 * @dma_ring_size:	allow GBit SoCs to set bigger rings than FE SoCs
 * @napi_weight:	allow GBit SoCs to set bigger napi weight than FE SoCs
 * @dma_type:		SoCs is PDMA, QDMA or a mix of the 2
 * @pdma_glo_cfg:	the default DMA configuration
 * @rx_int:		the TX interrupt bits used by the SoC
 * @tx_int:		the TX interrupt bits used by the SoC
 * @status_int:		the Status interrupt bits used by the SoC
 * @checksum_bit:	the bits used to turn on HW checksumming
 * @txd4:		default value of the TXD4 descriptor
 * @mac_count:		the number of MACs that the SoC has
 * @new_stats:		there is a old and new way to read hardware stats
 *			registers
 * @jumbo_frame:	does the SoC support jumbo frames ?
 * @rx_2b_offset:	tell the rx dma to offset the data by 2 bytes
 * @rx_sg_dma:		scatter gather support
 * @padding_64b		enable 64 bit padding
 * @padding_bug:	rt2880 has a padding bug
 * @has_switch:		does the SoC have a built-in switch
 *
 * Although all of the supported SoCs share the same basic functionality, there
 * are several SoC specific functions and features that we need to support. This
 * struct holds the SoC specific data so that the common core can figure out
 * how to setup and use these differences.
 */
struct mtk_soc_data {
	const u16 *reg_table;

	void (*init_data)(struct mtk_soc_data *data, struct net_device *netdev);
	void (*reset_fe)(struct mtk_eth *eth);
	void (*set_mac)(struct mtk_mac *mac, unsigned char *macaddr);
	int (*fwd_config)(struct mtk_eth *eth);
	int (*switch_init)(struct mtk_eth *eth);
	void (*port_init)(struct mtk_eth *eth, struct mtk_mac *mac,
			  struct device_node *port);
	int (*has_carrier)(struct mtk_eth *eth);
	int (*mdio_init)(struct mtk_eth *eth);
	void (*mdio_cleanup)(struct mtk_eth *eth);
	int (*mdio_write)(struct mii_bus *bus, int phy_addr, int phy_reg,
			  u16 val);
	int (*mdio_read)(struct mii_bus *bus, int phy_addr, int phy_reg);
	void (*mdio_adjust_link)(struct mtk_eth *eth, int port);
	u32 piac_offset;
	netdev_features_t hw_features;
	u32 dma_ring_size;
	u32 napi_weight;
	u32 dma_type;
	u32 pdma_glo_cfg;
	u32 rx_int;
	u32 tx_int;
	u32 status_int;
	u32 checksum_bit;
	u32 txd4;
	u32 mac_count;

	u32 new_stats:1;
	u32 jumbo_frame:1;
	u32 rx_2b_offset:1;
	u32 rx_sg_dma:1;
	u32 padding_64b:1;
	u32 padding_bug:1;
	u32 has_switch:1;
};

#define MTK_STAT_OFFSET			0x40

/* struct mtk_hw_stats - the structure that holds the traffic statistics.
 * @stats_lock:		make sure that stats operations are atomic
 * @reg_offset:		the status register offset of the SoC
 * @syncp:		the refcount
 *
 * All of the supported SoCs have hardware counters for traffic statstics.
 * Whenever the status IRQ triggers we can read the latest stats from these
 * counters and store them in this struct.
 */
struct mtk_hw_stats {
	spinlock_t stats_lock;
	u32 reg_offset;
	struct u64_stats_sync syncp;

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
};

/* PDMA descriptor can point at 1-2 segments. This enum allows us to track how
 * memory was allocated so that it can be freed properly
 */
enum mtk_tx_flags {
	MTK_TX_FLAGS_SINGLE0	= 0x01,
	MTK_TX_FLAGS_PAGE0	= 0x02,
	MTK_TX_FLAGS_PAGE1	= 0x04,
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
 * @tx_dma:		The descriptor ring
 * @tx_buf:		The memory pointed at by the ring
 * @tx_phys:		The physical addr of tx_buf
 * @tx_next_free:	Pointer to the next free descriptor
 * @tx_last_free:	Pointer to the last free descriptor
 * @tx_thresh:		The threshold of minimum amount of free descriptors
 * @tx_map:		Callback to map a new packet into the ring
 * @tx_poll:		Callback for the housekeeping function
 * @tx_clean:		Callback for the cleanup function
 * @tx_ring_size:	How many descriptors are in the ring
 * @tx_free_idx:	The index of th next free descriptor
 * @tx_next_idx:	QDMA uses a linked list. This element points to the next
 *			free descriptor in the list
 * @tx_free_count:	QDMA uses a linked list. Track how many free descriptors
 *			are present
 */
struct mtk_tx_ring {
	struct mtk_tx_dma *tx_dma;
	struct mtk_tx_buf *tx_buf;
	dma_addr_t tx_phys;
	struct mtk_tx_dma *tx_next_free;
	struct mtk_tx_dma *tx_last_free;
	u16 tx_thresh;
	int (*tx_map)(struct sk_buff *skb, struct net_device *dev, int tx_num,
		      struct mtk_tx_ring *ring, bool gso);
	int (*tx_poll)(struct mtk_eth *eth, int budget, bool *tx_again);
	void (*tx_clean)(struct mtk_eth *eth);

	/* PDMA only */
	u16 tx_ring_size;
	u16 tx_free_idx;

	/* QDMA only */
	u16 tx_next_idx;
	atomic_t tx_free_count;
};

/* struct mtk_rx_ring -	This struct holds info describing a RX ring
 * @rx_dma:		The descriptor ring
 * @rx_data:		The memory pointed at by the ring
 * @trx_phys:		The physical addr of rx_buf
 * @rx_ring_size:	How many descriptors are in the ring
 * @rx_buf_size:	The size of each packet buffer
 * @rx_calc_idx:	The current head of ring
 */
struct mtk_rx_ring {
	struct mtk_rx_dma *rx_dma;
	u8 **rx_data;
	dma_addr_t rx_phys;
	u16 rx_ring_size;
	u16 frag_size;
	u16 rx_buf_size;
	u16 rx_calc_idx;
};

/* currently no SoC has more than 2 macs */
#define MTK_MAX_DEVS			2

/* struct mtk_eth -	This is the main datasructure for holding the state
 *			of the driver
 * @dev:		The device pointer
 * @base:		The mapped register i/o base
 * @page_lock:		Make sure that register operations are atomic
 * @soc:		pointer to our SoC specific data
 * @dummy_dev:		we run 2 netdevs on 1 physical DMA ring and need a
 *			dummy for NAPI to work
 * @netdev:		The netdev instances
 * @mac:		Each netdev is linked to a physical MAC
 * @switch_np:		The phandle for the switch
 * @irq:		The IRQ that we are using
 * @msg_enable:		Ethtool msg level
 * @ysclk:		The sysclk rate - neeed for calibration
 * @ethsys:		The register map pointing at the range used to setup
 *			MII modes
 * @dma_refcnt:		track how many netdevs are using the DMA engine
 * @tx_ring:		Pointer to the memore holding info about the TX ring
 * @rx_ring:		Pointer to the memore holding info about the RX ring
 * @rx_napi:		The NAPI struct
 * @scratch_ring:	Newer SoCs need memory for a second HW managed TX ring
 * @scratch_head:	The scratch memory that scratch_ring points to.
 * @phy:		Info about the attached PHYs
 * @mii_bus:		If there is a bus we need to create an instance for it
 * @link:		Track if the ports have a physical link
 * @sw_priv:		Pointer to the switches private data
 * @vlan_map:		RX VID tracking
 */

struct mtk_eth {
	struct device			*dev;
	void __iomem			*base;
	spinlock_t			page_lock;
	struct mtk_soc_data		*soc;
	struct net_device		dummy_dev;
	struct net_device		*netdev[MTK_MAX_DEVS];
	struct mtk_mac			*mac[MTK_MAX_DEVS];
	struct device_node		*switch_np;
	int				irq;
	u32				msg_enable;
	unsigned long			sysclk;
	struct regmap			*ethsys;
	atomic_t			dma_refcnt;
	struct mtk_tx_ring		tx_ring;
	struct mtk_rx_ring		rx_ring[2];
	struct napi_struct		rx_napi;
	struct mtk_tx_dma		*scratch_ring;
	void				*scratch_head;
	struct mtk_phy			*phy;
	struct mii_bus			*mii_bus;
	int				link[8];
	void				*sw_priv;
	unsigned long			vlan_map;
};

/* struct mtk_mac -	the structure that holds the info about the MACs of the
 *			SoC
 * @id:			The number of the MAC
 * @of_node:		Our devicetree node
 * @hw:			Backpointer to our main datastruture
 * @hw_stats:		Packet statistics counter
 * @phy_dev:		The attached PHY if available
 * @phy_flags:		The PHYs flags
 * @pending_work:	The workqueue used to reset the dma ring
 */
struct mtk_mac {
	int				id;
	struct device_node		*of_node;
	struct mtk_eth			*hw;
	struct mtk_hw_stats		*hw_stats;
	struct phy_device		*phy_dev;
	u32				phy_flags;
	struct work_struct		pending_work;
};

/* the struct describing the SoC. these are declared in the soc_xyz.c files */
extern const struct of_device_id of_mtk_match[];

/* read the hardware status register */
void mtk_stats_update_mac(struct mtk_mac *mac);

/* default checksum setup handler */
void mtk_reset(struct mtk_eth *eth, u32 reset_bits);

/* register i/o wrappers */
void mtk_w32(struct mtk_eth *eth, u32 val, unsigned int reg);
u32 mtk_r32(struct mtk_eth *eth, unsigned int reg);

/* default clock calibration handler */
int mtk_set_clock_cycle(struct mtk_eth *eth);

/* default checksum setup handler */
void mtk_csum_config(struct mtk_eth *eth);

/* default forward config handler */
void mtk_fwd_config(struct mtk_eth *eth);

#endif /* MTK_ETH_H */
