// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024 AIROHA Inc
 * Author: Lorenzo Bianconi <lorenzo@kernel.org>
 */
#include <linux/etherdevice.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/tcp.h>
#include <linux/u64_stats_sync.h>
#include <net/dsa.h>
#include <net/page_pool/helpers.h>
#include <uapi/linux/ppp_defs.h>

#define AIROHA_MAX_NUM_GDM_PORTS	1
#define AIROHA_MAX_NUM_QDMA		2
#define AIROHA_MAX_NUM_RSTS		3
#define AIROHA_MAX_NUM_XSI_RSTS		5
#define AIROHA_MAX_MTU			2000
#define AIROHA_MAX_PACKET_SIZE		2048
#define AIROHA_NUM_TX_RING		32
#define AIROHA_NUM_RX_RING		32
#define AIROHA_FE_MC_MAX_VLAN_TABLE	64
#define AIROHA_FE_MC_MAX_VLAN_PORT	16
#define AIROHA_NUM_TX_IRQ		2
#define HW_DSCP_NUM			2048
#define IRQ_QUEUE_LEN(_n)		((_n) ? 1024 : 2048)
#define TX_DSCP_NUM			1024
#define RX_DSCP_NUM(_n)			\
	((_n) ==  2 ? 128 :		\
	 (_n) == 11 ? 128 :		\
	 (_n) == 15 ? 128 :		\
	 (_n) ==  0 ? 1024 : 16)

#define PSE_RSV_PAGES			128
#define PSE_QUEUE_RSV_PAGES		64

/* FE */
#define PSE_BASE			0x0100
#define CSR_IFC_BASE			0x0200
#define CDM1_BASE			0x0400
#define GDM1_BASE			0x0500
#define PPE1_BASE			0x0c00

#define CDM2_BASE			0x1400
#define GDM2_BASE			0x1500

#define GDM3_BASE			0x1100
#define GDM4_BASE			0x2500

#define GDM_BASE(_n)			\
	((_n) == 4 ? GDM4_BASE :	\
	 (_n) == 3 ? GDM3_BASE :	\
	 (_n) == 2 ? GDM2_BASE : GDM1_BASE)

#define REG_FE_DMA_GLO_CFG		0x0000
#define FE_DMA_GLO_L2_SPACE_MASK	GENMASK(7, 4)
#define FE_DMA_GLO_PG_SZ_MASK		BIT(3)

#define REG_FE_RST_GLO_CFG		0x0004
#define FE_RST_GDM4_MBI_ARB_MASK	BIT(3)
#define FE_RST_GDM3_MBI_ARB_MASK	BIT(2)
#define FE_RST_CORE_MASK		BIT(0)

#define REG_FE_LAN_MAC_H		0x0040
#define REG_FE_LAN_MAC_LMIN		0x0044
#define REG_FE_LAN_MAC_LMAX		0x0048

#define REG_FE_CDM1_OQ_MAP0		0x0050
#define REG_FE_CDM1_OQ_MAP1		0x0054
#define REG_FE_CDM1_OQ_MAP2		0x0058
#define REG_FE_CDM1_OQ_MAP3		0x005c

#define REG_FE_PCE_CFG			0x0070
#define PCE_DPI_EN_MASK			BIT(2)
#define PCE_KA_EN_MASK			BIT(1)
#define PCE_MC_EN_MASK			BIT(0)

#define REG_FE_PSE_QUEUE_CFG_WR		0x0080
#define PSE_CFG_PORT_ID_MASK		GENMASK(27, 24)
#define PSE_CFG_QUEUE_ID_MASK		GENMASK(20, 16)
#define PSE_CFG_WR_EN_MASK		BIT(8)
#define PSE_CFG_OQRSV_SEL_MASK		BIT(0)

#define REG_FE_PSE_QUEUE_CFG_VAL	0x0084
#define PSE_CFG_OQ_RSV_MASK		GENMASK(13, 0)

#define PSE_FQ_CFG			0x008c
#define PSE_FQ_LIMIT_MASK		GENMASK(14, 0)

#define REG_FE_PSE_BUF_SET		0x0090
#define PSE_SHARE_USED_LTHD_MASK	GENMASK(31, 16)
#define PSE_ALLRSV_MASK			GENMASK(14, 0)

#define REG_PSE_SHARE_USED_THD		0x0094
#define PSE_SHARE_USED_MTHD_MASK	GENMASK(31, 16)
#define PSE_SHARE_USED_HTHD_MASK	GENMASK(15, 0)

#define REG_GDM_MISC_CFG		0x0148
#define GDM2_RDM_ACK_WAIT_PREF_MASK	BIT(9)
#define GDM2_CHN_VLD_MODE_MASK		BIT(5)

#define REG_FE_CSR_IFC_CFG		CSR_IFC_BASE
#define FE_IFC_EN_MASK			BIT(0)

#define REG_FE_VIP_PORT_EN		0x01f0
#define REG_FE_IFC_PORT_EN		0x01f4

#define REG_PSE_IQ_REV1			(PSE_BASE + 0x08)
#define PSE_IQ_RES1_P2_MASK		GENMASK(23, 16)

#define REG_PSE_IQ_REV2			(PSE_BASE + 0x0c)
#define PSE_IQ_RES2_P5_MASK		GENMASK(15, 8)
#define PSE_IQ_RES2_P4_MASK		GENMASK(7, 0)

#define REG_FE_VIP_EN(_n)		(0x0300 + ((_n) << 3))
#define PATN_FCPU_EN_MASK		BIT(7)
#define PATN_SWP_EN_MASK		BIT(6)
#define PATN_DP_EN_MASK			BIT(5)
#define PATN_SP_EN_MASK			BIT(4)
#define PATN_TYPE_MASK			GENMASK(3, 1)
#define PATN_EN_MASK			BIT(0)

#define REG_FE_VIP_PATN(_n)		(0x0304 + ((_n) << 3))
#define PATN_DP_MASK			GENMASK(31, 16)
#define PATN_SP_MASK			GENMASK(15, 0)

#define REG_CDM1_VLAN_CTRL		CDM1_BASE
#define CDM1_VLAN_MASK			GENMASK(31, 16)

#define REG_CDM1_FWD_CFG		(CDM1_BASE + 0x08)
#define CDM1_VIP_QSEL_MASK		GENMASK(24, 20)

#define REG_CDM1_CRSN_QSEL(_n)		(CDM1_BASE + 0x10 + ((_n) << 2))
#define CDM1_CRSN_QSEL_REASON_MASK(_n)	\
	GENMASK(4 + (((_n) % 4) << 3),	(((_n) % 4) << 3))

#define REG_CDM2_FWD_CFG		(CDM2_BASE + 0x08)
#define CDM2_OAM_QSEL_MASK		GENMASK(31, 27)
#define CDM2_VIP_QSEL_MASK		GENMASK(24, 20)

#define REG_CDM2_CRSN_QSEL(_n)		(CDM2_BASE + 0x10 + ((_n) << 2))
#define CDM2_CRSN_QSEL_REASON_MASK(_n)	\
	GENMASK(4 + (((_n) % 4) << 3),	(((_n) % 4) << 3))

#define REG_GDM_FWD_CFG(_n)		GDM_BASE(_n)
#define GDM_DROP_CRC_ERR		BIT(23)
#define GDM_IP4_CKSUM			BIT(22)
#define GDM_TCP_CKSUM			BIT(21)
#define GDM_UDP_CKSUM			BIT(20)
#define GDM_UCFQ_MASK			GENMASK(15, 12)
#define GDM_BCFQ_MASK			GENMASK(11, 8)
#define GDM_MCFQ_MASK			GENMASK(7, 4)
#define GDM_OCFQ_MASK			GENMASK(3, 0)

#define REG_GDM_INGRESS_CFG(_n)		(GDM_BASE(_n) + 0x10)
#define GDM_INGRESS_FC_EN_MASK		BIT(1)
#define GDM_STAG_EN_MASK		BIT(0)

#define REG_GDM_LEN_CFG(_n)		(GDM_BASE(_n) + 0x14)
#define GDM_SHORT_LEN_MASK		GENMASK(13, 0)
#define GDM_LONG_LEN_MASK		GENMASK(29, 16)

#define REG_FE_CPORT_CFG		(GDM1_BASE + 0x40)
#define FE_CPORT_PAD			BIT(26)
#define FE_CPORT_PORT_XFC_MASK		BIT(25)
#define FE_CPORT_QUEUE_XFC_MASK		BIT(24)

#define REG_FE_GDM_MIB_CLEAR(_n)	(GDM_BASE(_n) + 0xf0)
#define FE_GDM_MIB_RX_CLEAR_MASK	BIT(1)
#define FE_GDM_MIB_TX_CLEAR_MASK	BIT(0)

#define REG_FE_GDM1_MIB_CFG		(GDM1_BASE + 0xf4)
#define FE_STRICT_RFC2819_MODE_MASK	BIT(31)
#define FE_GDM1_TX_MIB_SPLIT_EN_MASK	BIT(17)
#define FE_GDM1_RX_MIB_SPLIT_EN_MASK	BIT(16)
#define FE_TX_MIB_ID_MASK		GENMASK(15, 8)
#define FE_RX_MIB_ID_MASK		GENMASK(7, 0)

#define REG_FE_GDM_TX_OK_PKT_CNT_L(_n)		(GDM_BASE(_n) + 0x104)
#define REG_FE_GDM_TX_OK_BYTE_CNT_L(_n)		(GDM_BASE(_n) + 0x10c)
#define REG_FE_GDM_TX_ETH_PKT_CNT_L(_n)		(GDM_BASE(_n) + 0x110)
#define REG_FE_GDM_TX_ETH_BYTE_CNT_L(_n)	(GDM_BASE(_n) + 0x114)
#define REG_FE_GDM_TX_ETH_DROP_CNT(_n)		(GDM_BASE(_n) + 0x118)
#define REG_FE_GDM_TX_ETH_BC_CNT(_n)		(GDM_BASE(_n) + 0x11c)
#define REG_FE_GDM_TX_ETH_MC_CNT(_n)		(GDM_BASE(_n) + 0x120)
#define REG_FE_GDM_TX_ETH_RUNT_CNT(_n)		(GDM_BASE(_n) + 0x124)
#define REG_FE_GDM_TX_ETH_LONG_CNT(_n)		(GDM_BASE(_n) + 0x128)
#define REG_FE_GDM_TX_ETH_E64_CNT_L(_n)		(GDM_BASE(_n) + 0x12c)
#define REG_FE_GDM_TX_ETH_L64_CNT_L(_n)		(GDM_BASE(_n) + 0x130)
#define REG_FE_GDM_TX_ETH_L127_CNT_L(_n)	(GDM_BASE(_n) + 0x134)
#define REG_FE_GDM_TX_ETH_L255_CNT_L(_n)	(GDM_BASE(_n) + 0x138)
#define REG_FE_GDM_TX_ETH_L511_CNT_L(_n)	(GDM_BASE(_n) + 0x13c)
#define REG_FE_GDM_TX_ETH_L1023_CNT_L(_n)	(GDM_BASE(_n) + 0x140)

#define REG_FE_GDM_RX_OK_PKT_CNT_L(_n)		(GDM_BASE(_n) + 0x148)
#define REG_FE_GDM_RX_FC_DROP_CNT(_n)		(GDM_BASE(_n) + 0x14c)
#define REG_FE_GDM_RX_RC_DROP_CNT(_n)		(GDM_BASE(_n) + 0x150)
#define REG_FE_GDM_RX_OVERFLOW_DROP_CNT(_n)	(GDM_BASE(_n) + 0x154)
#define REG_FE_GDM_RX_ERROR_DROP_CNT(_n)	(GDM_BASE(_n) + 0x158)
#define REG_FE_GDM_RX_OK_BYTE_CNT_L(_n)		(GDM_BASE(_n) + 0x15c)
#define REG_FE_GDM_RX_ETH_PKT_CNT_L(_n)		(GDM_BASE(_n) + 0x160)
#define REG_FE_GDM_RX_ETH_BYTE_CNT_L(_n)	(GDM_BASE(_n) + 0x164)
#define REG_FE_GDM_RX_ETH_DROP_CNT(_n)		(GDM_BASE(_n) + 0x168)
#define REG_FE_GDM_RX_ETH_BC_CNT(_n)		(GDM_BASE(_n) + 0x16c)
#define REG_FE_GDM_RX_ETH_MC_CNT(_n)		(GDM_BASE(_n) + 0x170)
#define REG_FE_GDM_RX_ETH_CRC_ERR_CNT(_n)	(GDM_BASE(_n) + 0x174)
#define REG_FE_GDM_RX_ETH_FRAG_CNT(_n)		(GDM_BASE(_n) + 0x178)
#define REG_FE_GDM_RX_ETH_JABBER_CNT(_n)	(GDM_BASE(_n) + 0x17c)
#define REG_FE_GDM_RX_ETH_RUNT_CNT(_n)		(GDM_BASE(_n) + 0x180)
#define REG_FE_GDM_RX_ETH_LONG_CNT(_n)		(GDM_BASE(_n) + 0x184)
#define REG_FE_GDM_RX_ETH_E64_CNT_L(_n)		(GDM_BASE(_n) + 0x188)
#define REG_FE_GDM_RX_ETH_L64_CNT_L(_n)		(GDM_BASE(_n) + 0x18c)
#define REG_FE_GDM_RX_ETH_L127_CNT_L(_n)	(GDM_BASE(_n) + 0x190)
#define REG_FE_GDM_RX_ETH_L255_CNT_L(_n)	(GDM_BASE(_n) + 0x194)
#define REG_FE_GDM_RX_ETH_L511_CNT_L(_n)	(GDM_BASE(_n) + 0x198)
#define REG_FE_GDM_RX_ETH_L1023_CNT_L(_n)	(GDM_BASE(_n) + 0x19c)

#define REG_PPE1_TB_HASH_CFG		(PPE1_BASE + 0x250)
#define PPE1_SRAM_TABLE_EN_MASK		BIT(0)
#define PPE1_SRAM_HASH1_EN_MASK		BIT(8)
#define PPE1_DRAM_TABLE_EN_MASK		BIT(16)
#define PPE1_DRAM_HASH1_EN_MASK		BIT(24)

#define REG_FE_GDM_TX_OK_PKT_CNT_H(_n)		(GDM_BASE(_n) + 0x280)
#define REG_FE_GDM_TX_OK_BYTE_CNT_H(_n)		(GDM_BASE(_n) + 0x284)
#define REG_FE_GDM_TX_ETH_PKT_CNT_H(_n)		(GDM_BASE(_n) + 0x288)
#define REG_FE_GDM_TX_ETH_BYTE_CNT_H(_n)	(GDM_BASE(_n) + 0x28c)

#define REG_FE_GDM_RX_OK_PKT_CNT_H(_n)		(GDM_BASE(_n) + 0x290)
#define REG_FE_GDM_RX_OK_BYTE_CNT_H(_n)		(GDM_BASE(_n) + 0x294)
#define REG_FE_GDM_RX_ETH_PKT_CNT_H(_n)		(GDM_BASE(_n) + 0x298)
#define REG_FE_GDM_RX_ETH_BYTE_CNT_H(_n)	(GDM_BASE(_n) + 0x29c)
#define REG_FE_GDM_TX_ETH_E64_CNT_H(_n)		(GDM_BASE(_n) + 0x2b8)
#define REG_FE_GDM_TX_ETH_L64_CNT_H(_n)		(GDM_BASE(_n) + 0x2bc)
#define REG_FE_GDM_TX_ETH_L127_CNT_H(_n)	(GDM_BASE(_n) + 0x2c0)
#define REG_FE_GDM_TX_ETH_L255_CNT_H(_n)	(GDM_BASE(_n) + 0x2c4)
#define REG_FE_GDM_TX_ETH_L511_CNT_H(_n)	(GDM_BASE(_n) + 0x2c8)
#define REG_FE_GDM_TX_ETH_L1023_CNT_H(_n)	(GDM_BASE(_n) + 0x2cc)
#define REG_FE_GDM_RX_ETH_E64_CNT_H(_n)		(GDM_BASE(_n) + 0x2e8)
#define REG_FE_GDM_RX_ETH_L64_CNT_H(_n)		(GDM_BASE(_n) + 0x2ec)
#define REG_FE_GDM_RX_ETH_L127_CNT_H(_n)	(GDM_BASE(_n) + 0x2f0)
#define REG_FE_GDM_RX_ETH_L255_CNT_H(_n)	(GDM_BASE(_n) + 0x2f4)
#define REG_FE_GDM_RX_ETH_L511_CNT_H(_n)	(GDM_BASE(_n) + 0x2f8)
#define REG_FE_GDM_RX_ETH_L1023_CNT_H(_n)	(GDM_BASE(_n) + 0x2fc)

#define REG_GDM2_CHN_RLS		(GDM2_BASE + 0x20)
#define MBI_RX_AGE_SEL_MASK		GENMASK(26, 25)
#define MBI_TX_AGE_SEL_MASK		GENMASK(18, 17)

#define REG_GDM3_FWD_CFG		GDM3_BASE
#define GDM3_PAD_EN_MASK		BIT(28)

#define REG_GDM4_FWD_CFG		(GDM4_BASE + 0x100)
#define GDM4_PAD_EN_MASK		BIT(28)
#define GDM4_SPORT_OFFSET0_MASK		GENMASK(11, 8)

#define REG_GDM4_SRC_PORT_SET		(GDM4_BASE + 0x33c)
#define GDM4_SPORT_OFF2_MASK		GENMASK(19, 16)
#define GDM4_SPORT_OFF1_MASK		GENMASK(15, 12)
#define GDM4_SPORT_OFF0_MASK		GENMASK(11, 8)

#define REG_IP_FRAG_FP			0x2010
#define IP_ASSEMBLE_PORT_MASK		GENMASK(24, 21)
#define IP_ASSEMBLE_NBQ_MASK		GENMASK(20, 16)
#define IP_FRAGMENT_PORT_MASK		GENMASK(8, 5)
#define IP_FRAGMENT_NBQ_MASK		GENMASK(4, 0)

#define REG_MC_VLAN_EN			0x2100
#define MC_VLAN_EN_MASK			BIT(0)

#define REG_MC_VLAN_CFG			0x2104
#define MC_VLAN_CFG_CMD_DONE_MASK	BIT(31)
#define MC_VLAN_CFG_TABLE_ID_MASK	GENMASK(21, 16)
#define MC_VLAN_CFG_PORT_ID_MASK	GENMASK(11, 8)
#define MC_VLAN_CFG_TABLE_SEL_MASK	BIT(4)
#define MC_VLAN_CFG_RW_MASK		BIT(0)

#define REG_MC_VLAN_DATA		0x2108

#define REG_CDM5_RX_OQ1_DROP_CNT	0x29d4

/* QDMA */
#define REG_QDMA_GLOBAL_CFG			0x0004
#define GLOBAL_CFG_RX_2B_OFFSET_MASK		BIT(31)
#define GLOBAL_CFG_DMA_PREFERENCE_MASK		GENMASK(30, 29)
#define GLOBAL_CFG_CPU_TXR_RR_MASK		BIT(28)
#define GLOBAL_CFG_DSCP_BYTE_SWAP_MASK		BIT(27)
#define GLOBAL_CFG_PAYLOAD_BYTE_SWAP_MASK	BIT(26)
#define GLOBAL_CFG_MULTICAST_MODIFY_FP_MASK	BIT(25)
#define GLOBAL_CFG_OAM_MODIFY_MASK		BIT(24)
#define GLOBAL_CFG_RESET_MASK			BIT(23)
#define GLOBAL_CFG_RESET_DONE_MASK		BIT(22)
#define GLOBAL_CFG_MULTICAST_EN_MASK		BIT(21)
#define GLOBAL_CFG_IRQ1_EN_MASK			BIT(20)
#define GLOBAL_CFG_IRQ0_EN_MASK			BIT(19)
#define GLOBAL_CFG_LOOPCNT_EN_MASK		BIT(18)
#define GLOBAL_CFG_RD_BYPASS_WR_MASK		BIT(17)
#define GLOBAL_CFG_QDMA_LOOPBACK_MASK		BIT(16)
#define GLOBAL_CFG_LPBK_RXQ_SEL_MASK		GENMASK(13, 8)
#define GLOBAL_CFG_CHECK_DONE_MASK		BIT(7)
#define GLOBAL_CFG_TX_WB_DONE_MASK		BIT(6)
#define GLOBAL_CFG_MAX_ISSUE_NUM_MASK		GENMASK(5, 4)
#define GLOBAL_CFG_RX_DMA_BUSY_MASK		BIT(3)
#define GLOBAL_CFG_RX_DMA_EN_MASK		BIT(2)
#define GLOBAL_CFG_TX_DMA_BUSY_MASK		BIT(1)
#define GLOBAL_CFG_TX_DMA_EN_MASK		BIT(0)

#define REG_FWD_DSCP_BASE			0x0010
#define REG_FWD_BUF_BASE			0x0014

#define REG_HW_FWD_DSCP_CFG			0x0018
#define HW_FWD_DSCP_PAYLOAD_SIZE_MASK		GENMASK(29, 28)
#define HW_FWD_DSCP_SCATTER_LEN_MASK		GENMASK(17, 16)
#define HW_FWD_DSCP_MIN_SCATTER_LEN_MASK	GENMASK(15, 0)

#define REG_INT_STATUS(_n)		\
	(((_n) == 4) ? 0x0730 :		\
	 ((_n) == 3) ? 0x0724 :		\
	 ((_n) == 2) ? 0x0720 :		\
	 ((_n) == 1) ? 0x0024 : 0x0020)

#define REG_INT_ENABLE(_n)		\
	(((_n) == 4) ? 0x0750 :		\
	 ((_n) == 3) ? 0x0744 :		\
	 ((_n) == 2) ? 0x0740 :		\
	 ((_n) == 1) ? 0x002c : 0x0028)

/* QDMA_CSR_INT_ENABLE1 */
#define RX15_COHERENT_INT_MASK		BIT(31)
#define RX14_COHERENT_INT_MASK		BIT(30)
#define RX13_COHERENT_INT_MASK		BIT(29)
#define RX12_COHERENT_INT_MASK		BIT(28)
#define RX11_COHERENT_INT_MASK		BIT(27)
#define RX10_COHERENT_INT_MASK		BIT(26)
#define RX9_COHERENT_INT_MASK		BIT(25)
#define RX8_COHERENT_INT_MASK		BIT(24)
#define RX7_COHERENT_INT_MASK		BIT(23)
#define RX6_COHERENT_INT_MASK		BIT(22)
#define RX5_COHERENT_INT_MASK		BIT(21)
#define RX4_COHERENT_INT_MASK		BIT(20)
#define RX3_COHERENT_INT_MASK		BIT(19)
#define RX2_COHERENT_INT_MASK		BIT(18)
#define RX1_COHERENT_INT_MASK		BIT(17)
#define RX0_COHERENT_INT_MASK		BIT(16)
#define TX7_COHERENT_INT_MASK		BIT(15)
#define TX6_COHERENT_INT_MASK		BIT(14)
#define TX5_COHERENT_INT_MASK		BIT(13)
#define TX4_COHERENT_INT_MASK		BIT(12)
#define TX3_COHERENT_INT_MASK		BIT(11)
#define TX2_COHERENT_INT_MASK		BIT(10)
#define TX1_COHERENT_INT_MASK		BIT(9)
#define TX0_COHERENT_INT_MASK		BIT(8)
#define CNT_OVER_FLOW_INT_MASK		BIT(7)
#define IRQ1_FULL_INT_MASK		BIT(5)
#define IRQ1_INT_MASK			BIT(4)
#define HWFWD_DSCP_LOW_INT_MASK		BIT(3)
#define HWFWD_DSCP_EMPTY_INT_MASK	BIT(2)
#define IRQ0_FULL_INT_MASK		BIT(1)
#define IRQ0_INT_MASK			BIT(0)

#define TX_DONE_INT_MASK(_n)					\
	((_n) ? IRQ1_INT_MASK | IRQ1_FULL_INT_MASK		\
	      : IRQ0_INT_MASK | IRQ0_FULL_INT_MASK)

#define INT_TX_MASK						\
	(IRQ1_INT_MASK | IRQ1_FULL_INT_MASK |			\
	 IRQ0_INT_MASK | IRQ0_FULL_INT_MASK)

#define INT_IDX0_MASK						\
	(TX0_COHERENT_INT_MASK | TX1_COHERENT_INT_MASK |	\
	 TX2_COHERENT_INT_MASK | TX3_COHERENT_INT_MASK |	\
	 TX4_COHERENT_INT_MASK | TX5_COHERENT_INT_MASK |	\
	 TX6_COHERENT_INT_MASK | TX7_COHERENT_INT_MASK |	\
	 RX0_COHERENT_INT_MASK | RX1_COHERENT_INT_MASK |	\
	 RX2_COHERENT_INT_MASK | RX3_COHERENT_INT_MASK |	\
	 RX4_COHERENT_INT_MASK | RX7_COHERENT_INT_MASK |	\
	 RX8_COHERENT_INT_MASK | RX9_COHERENT_INT_MASK |	\
	 RX15_COHERENT_INT_MASK | INT_TX_MASK)

/* QDMA_CSR_INT_ENABLE2 */
#define RX15_NO_CPU_DSCP_INT_MASK	BIT(31)
#define RX14_NO_CPU_DSCP_INT_MASK	BIT(30)
#define RX13_NO_CPU_DSCP_INT_MASK	BIT(29)
#define RX12_NO_CPU_DSCP_INT_MASK	BIT(28)
#define RX11_NO_CPU_DSCP_INT_MASK	BIT(27)
#define RX10_NO_CPU_DSCP_INT_MASK	BIT(26)
#define RX9_NO_CPU_DSCP_INT_MASK	BIT(25)
#define RX8_NO_CPU_DSCP_INT_MASK	BIT(24)
#define RX7_NO_CPU_DSCP_INT_MASK	BIT(23)
#define RX6_NO_CPU_DSCP_INT_MASK	BIT(22)
#define RX5_NO_CPU_DSCP_INT_MASK	BIT(21)
#define RX4_NO_CPU_DSCP_INT_MASK	BIT(20)
#define RX3_NO_CPU_DSCP_INT_MASK	BIT(19)
#define RX2_NO_CPU_DSCP_INT_MASK	BIT(18)
#define RX1_NO_CPU_DSCP_INT_MASK	BIT(17)
#define RX0_NO_CPU_DSCP_INT_MASK	BIT(16)
#define RX15_DONE_INT_MASK		BIT(15)
#define RX14_DONE_INT_MASK		BIT(14)
#define RX13_DONE_INT_MASK		BIT(13)
#define RX12_DONE_INT_MASK		BIT(12)
#define RX11_DONE_INT_MASK		BIT(11)
#define RX10_DONE_INT_MASK		BIT(10)
#define RX9_DONE_INT_MASK		BIT(9)
#define RX8_DONE_INT_MASK		BIT(8)
#define RX7_DONE_INT_MASK		BIT(7)
#define RX6_DONE_INT_MASK		BIT(6)
#define RX5_DONE_INT_MASK		BIT(5)
#define RX4_DONE_INT_MASK		BIT(4)
#define RX3_DONE_INT_MASK		BIT(3)
#define RX2_DONE_INT_MASK		BIT(2)
#define RX1_DONE_INT_MASK		BIT(1)
#define RX0_DONE_INT_MASK		BIT(0)

#define RX_DONE_INT_MASK					\
	(RX0_DONE_INT_MASK | RX1_DONE_INT_MASK |		\
	 RX2_DONE_INT_MASK | RX3_DONE_INT_MASK |		\
	 RX4_DONE_INT_MASK | RX7_DONE_INT_MASK |		\
	 RX8_DONE_INT_MASK | RX9_DONE_INT_MASK |		\
	 RX15_DONE_INT_MASK)
#define INT_IDX1_MASK						\
	(RX_DONE_INT_MASK |					\
	 RX0_NO_CPU_DSCP_INT_MASK | RX1_NO_CPU_DSCP_INT_MASK |	\
	 RX2_NO_CPU_DSCP_INT_MASK | RX3_NO_CPU_DSCP_INT_MASK |	\
	 RX4_NO_CPU_DSCP_INT_MASK | RX7_NO_CPU_DSCP_INT_MASK |	\
	 RX8_NO_CPU_DSCP_INT_MASK | RX9_NO_CPU_DSCP_INT_MASK |	\
	 RX15_NO_CPU_DSCP_INT_MASK)

/* QDMA_CSR_INT_ENABLE5 */
#define TX31_COHERENT_INT_MASK		BIT(31)
#define TX30_COHERENT_INT_MASK		BIT(30)
#define TX29_COHERENT_INT_MASK		BIT(29)
#define TX28_COHERENT_INT_MASK		BIT(28)
#define TX27_COHERENT_INT_MASK		BIT(27)
#define TX26_COHERENT_INT_MASK		BIT(26)
#define TX25_COHERENT_INT_MASK		BIT(25)
#define TX24_COHERENT_INT_MASK		BIT(24)
#define TX23_COHERENT_INT_MASK		BIT(23)
#define TX22_COHERENT_INT_MASK		BIT(22)
#define TX21_COHERENT_INT_MASK		BIT(21)
#define TX20_COHERENT_INT_MASK		BIT(20)
#define TX19_COHERENT_INT_MASK		BIT(19)
#define TX18_COHERENT_INT_MASK		BIT(18)
#define TX17_COHERENT_INT_MASK		BIT(17)
#define TX16_COHERENT_INT_MASK		BIT(16)
#define TX15_COHERENT_INT_MASK		BIT(15)
#define TX14_COHERENT_INT_MASK		BIT(14)
#define TX13_COHERENT_INT_MASK		BIT(13)
#define TX12_COHERENT_INT_MASK		BIT(12)
#define TX11_COHERENT_INT_MASK		BIT(11)
#define TX10_COHERENT_INT_MASK		BIT(10)
#define TX9_COHERENT_INT_MASK		BIT(9)
#define TX8_COHERENT_INT_MASK		BIT(8)

#define INT_IDX4_MASK						\
	(TX8_COHERENT_INT_MASK | TX9_COHERENT_INT_MASK |	\
	 TX10_COHERENT_INT_MASK | TX11_COHERENT_INT_MASK |	\
	 TX12_COHERENT_INT_MASK | TX13_COHERENT_INT_MASK |	\
	 TX14_COHERENT_INT_MASK | TX15_COHERENT_INT_MASK |	\
	 TX16_COHERENT_INT_MASK | TX17_COHERENT_INT_MASK |	\
	 TX18_COHERENT_INT_MASK | TX19_COHERENT_INT_MASK |	\
	 TX20_COHERENT_INT_MASK | TX21_COHERENT_INT_MASK |	\
	 TX22_COHERENT_INT_MASK | TX23_COHERENT_INT_MASK |	\
	 TX24_COHERENT_INT_MASK | TX25_COHERENT_INT_MASK |	\
	 TX26_COHERENT_INT_MASK | TX27_COHERENT_INT_MASK |	\
	 TX28_COHERENT_INT_MASK | TX29_COHERENT_INT_MASK |	\
	 TX30_COHERENT_INT_MASK | TX31_COHERENT_INT_MASK)

#define REG_TX_IRQ_BASE(_n)		((_n) ? 0x0048 : 0x0050)

#define REG_TX_IRQ_CFG(_n)		((_n) ? 0x004c : 0x0054)
#define TX_IRQ_THR_MASK			GENMASK(27, 16)
#define TX_IRQ_DEPTH_MASK		GENMASK(11, 0)

#define REG_IRQ_CLEAR_LEN(_n)		((_n) ? 0x0064 : 0x0058)
#define IRQ_CLEAR_LEN_MASK		GENMASK(7, 0)

#define REG_IRQ_STATUS(_n)		((_n) ? 0x0068 : 0x005c)
#define IRQ_ENTRY_LEN_MASK		GENMASK(27, 16)
#define IRQ_HEAD_IDX_MASK		GENMASK(11, 0)

#define REG_TX_RING_BASE(_n)	\
	(((_n) < 8) ? 0x0100 + ((_n) << 5) : 0x0b00 + (((_n) - 8) << 5))

#define REG_TX_RING_BLOCKING(_n)	\
	(((_n) < 8) ? 0x0104 + ((_n) << 5) : 0x0b04 + (((_n) - 8) << 5))

#define TX_RING_IRQ_BLOCKING_MAP_MASK			BIT(6)
#define TX_RING_IRQ_BLOCKING_CFG_MASK			BIT(4)
#define TX_RING_IRQ_BLOCKING_TX_DROP_EN_MASK		BIT(2)
#define TX_RING_IRQ_BLOCKING_MAX_TH_TXRING_EN_MASK	BIT(1)
#define TX_RING_IRQ_BLOCKING_MIN_TH_TXRING_EN_MASK	BIT(0)

#define REG_TX_CPU_IDX(_n)	\
	(((_n) < 8) ? 0x0108 + ((_n) << 5) : 0x0b08 + (((_n) - 8) << 5))

#define TX_RING_CPU_IDX_MASK		GENMASK(15, 0)

#define REG_TX_DMA_IDX(_n)	\
	(((_n) < 8) ? 0x010c + ((_n) << 5) : 0x0b0c + (((_n) - 8) << 5))

#define TX_RING_DMA_IDX_MASK		GENMASK(15, 0)

#define IRQ_RING_IDX_MASK		GENMASK(20, 16)
#define IRQ_DESC_IDX_MASK		GENMASK(15, 0)

#define REG_RX_RING_BASE(_n)	\
	(((_n) < 16) ? 0x0200 + ((_n) << 5) : 0x0e00 + (((_n) - 16) << 5))

#define REG_RX_RING_SIZE(_n)	\
	(((_n) < 16) ? 0x0204 + ((_n) << 5) : 0x0e04 + (((_n) - 16) << 5))

#define RX_RING_THR_MASK		GENMASK(31, 16)
#define RX_RING_SIZE_MASK		GENMASK(15, 0)

#define REG_RX_CPU_IDX(_n)	\
	(((_n) < 16) ? 0x0208 + ((_n) << 5) : 0x0e08 + (((_n) - 16) << 5))

#define RX_RING_CPU_IDX_MASK		GENMASK(15, 0)

#define REG_RX_DMA_IDX(_n)	\
	(((_n) < 16) ? 0x020c + ((_n) << 5) : 0x0e0c + (((_n) - 16) << 5))

#define REG_RX_DELAY_INT_IDX(_n)	\
	(((_n) < 16) ? 0x0210 + ((_n) << 5) : 0x0e10 + (((_n) - 16) << 5))

#define RX_DELAY_INT_MASK		GENMASK(15, 0)

#define RX_RING_DMA_IDX_MASK		GENMASK(15, 0)

#define REG_INGRESS_TRTCM_CFG		0x0070
#define INGRESS_TRTCM_EN_MASK		BIT(31)
#define INGRESS_TRTCM_MODE_MASK		BIT(30)
#define INGRESS_SLOW_TICK_RATIO_MASK	GENMASK(29, 16)
#define INGRESS_FAST_TICK_MASK		GENMASK(15, 0)

#define REG_TXQ_DIS_CFG_BASE(_n)	((_n) ? 0x20a0 : 0x00a0)
#define REG_TXQ_DIS_CFG(_n, _m)		(REG_TXQ_DIS_CFG_BASE((_n)) + (_m) << 2)

#define REG_LMGR_INIT_CFG		0x1000
#define LMGR_INIT_START			BIT(31)
#define LMGR_SRAM_MODE_MASK		BIT(30)
#define HW_FWD_PKTSIZE_OVERHEAD_MASK	GENMASK(27, 20)
#define HW_FWD_DESC_NUM_MASK		GENMASK(16, 0)

#define REG_FWD_DSCP_LOW_THR		0x1004
#define FWD_DSCP_LOW_THR_MASK		GENMASK(17, 0)

#define REG_EGRESS_RATE_METER_CFG		0x100c
#define EGRESS_RATE_METER_EN_MASK		BIT(29)
#define EGRESS_RATE_METER_EQ_RATE_EN_MASK	BIT(17)
#define EGRESS_RATE_METER_WINDOW_SZ_MASK	GENMASK(16, 12)
#define EGRESS_RATE_METER_TIMESLICE_MASK	GENMASK(10, 0)

#define REG_EGRESS_TRTCM_CFG		0x1010
#define EGRESS_TRTCM_EN_MASK		BIT(31)
#define EGRESS_TRTCM_MODE_MASK		BIT(30)
#define EGRESS_SLOW_TICK_RATIO_MASK	GENMASK(29, 16)
#define EGRESS_FAST_TICK_MASK		GENMASK(15, 0)

#define REG_TXWRR_MODE_CFG		0x1020
#define TWRR_WEIGHT_SCALE_MASK		BIT(31)
#define TWRR_WEIGHT_BASE_MASK		BIT(3)

#define REG_PSE_BUF_USAGE_CFG		0x1028
#define PSE_BUF_ESTIMATE_EN_MASK	BIT(29)

#define REG_GLB_TRTCM_CFG		0x1080
#define GLB_TRTCM_EN_MASK		BIT(31)
#define GLB_TRTCM_MODE_MASK		BIT(30)
#define GLB_SLOW_TICK_RATIO_MASK	GENMASK(29, 16)
#define GLB_FAST_TICK_MASK		GENMASK(15, 0)

#define REG_TXQ_CNGST_CFG		0x10a0
#define TXQ_CNGST_DROP_EN		BIT(31)
#define TXQ_CNGST_DEI_DROP_EN		BIT(30)

#define REG_SLA_TRTCM_CFG		0x1150
#define SLA_TRTCM_EN_MASK		BIT(31)
#define SLA_TRTCM_MODE_MASK		BIT(30)
#define SLA_SLOW_TICK_RATIO_MASK	GENMASK(29, 16)
#define SLA_FAST_TICK_MASK		GENMASK(15, 0)

/* CTRL */
#define QDMA_DESC_DONE_MASK		BIT(31)
#define QDMA_DESC_DROP_MASK		BIT(30) /* tx: drop - rx: overflow */
#define QDMA_DESC_MORE_MASK		BIT(29) /* more SG elements */
#define QDMA_DESC_DEI_MASK		BIT(25)
#define QDMA_DESC_NO_DROP_MASK		BIT(24)
#define QDMA_DESC_LEN_MASK		GENMASK(15, 0)
/* DATA */
#define QDMA_DESC_NEXT_ID_MASK		GENMASK(15, 0)
/* TX MSG0 */
#define QDMA_ETH_TXMSG_MIC_IDX_MASK	BIT(30)
#define QDMA_ETH_TXMSG_SP_TAG_MASK	GENMASK(29, 14)
#define QDMA_ETH_TXMSG_ICO_MASK		BIT(13)
#define QDMA_ETH_TXMSG_UCO_MASK		BIT(12)
#define QDMA_ETH_TXMSG_TCO_MASK		BIT(11)
#define QDMA_ETH_TXMSG_TSO_MASK		BIT(10)
#define QDMA_ETH_TXMSG_FAST_MASK	BIT(9)
#define QDMA_ETH_TXMSG_OAM_MASK		BIT(8)
#define QDMA_ETH_TXMSG_CHAN_MASK	GENMASK(7, 3)
#define QDMA_ETH_TXMSG_QUEUE_MASK	GENMASK(2, 0)
/* TX MSG1 */
#define QDMA_ETH_TXMSG_NO_DROP		BIT(31)
#define QDMA_ETH_TXMSG_METER_MASK	GENMASK(30, 24)	/* 0x7f no meters */
#define QDMA_ETH_TXMSG_FPORT_MASK	GENMASK(23, 20)
#define QDMA_ETH_TXMSG_NBOQ_MASK	GENMASK(19, 15)
#define QDMA_ETH_TXMSG_HWF_MASK		BIT(14)
#define QDMA_ETH_TXMSG_HOP_MASK		BIT(13)
#define QDMA_ETH_TXMSG_PTP_MASK		BIT(12)
#define QDMA_ETH_TXMSG_ACNT_G1_MASK	GENMASK(10, 6)	/* 0x1f do not count */
#define QDMA_ETH_TXMSG_ACNT_G0_MASK	GENMASK(5, 0)	/* 0x3f do not count */

/* RX MSG1 */
#define QDMA_ETH_RXMSG_DEI_MASK		BIT(31)
#define QDMA_ETH_RXMSG_IP6_MASK		BIT(30)
#define QDMA_ETH_RXMSG_IP4_MASK		BIT(29)
#define QDMA_ETH_RXMSG_IP4F_MASK	BIT(28)
#define QDMA_ETH_RXMSG_L4_VALID_MASK	BIT(27)
#define QDMA_ETH_RXMSG_L4F_MASK		BIT(26)
#define QDMA_ETH_RXMSG_SPORT_MASK	GENMASK(25, 21)
#define QDMA_ETH_RXMSG_CRSN_MASK	GENMASK(20, 16)
#define QDMA_ETH_RXMSG_PPE_ENTRY_MASK	GENMASK(15, 0)

struct airoha_qdma_desc {
	__le32 rsv;
	__le32 ctrl;
	__le32 addr;
	__le32 data;
	__le32 msg0;
	__le32 msg1;
	__le32 msg2;
	__le32 msg3;
};

/* CTRL0 */
#define QDMA_FWD_DESC_CTX_MASK		BIT(31)
#define QDMA_FWD_DESC_RING_MASK		GENMASK(30, 28)
#define QDMA_FWD_DESC_IDX_MASK		GENMASK(27, 16)
#define QDMA_FWD_DESC_LEN_MASK		GENMASK(15, 0)
/* CTRL1 */
#define QDMA_FWD_DESC_FIRST_IDX_MASK	GENMASK(15, 0)
/* CTRL2 */
#define QDMA_FWD_DESC_MORE_PKT_NUM_MASK	GENMASK(2, 0)

struct airoha_qdma_fwd_desc {
	__le32 addr;
	__le32 ctrl0;
	__le32 ctrl1;
	__le32 ctrl2;
	__le32 msg0;
	__le32 msg1;
	__le32 rsv0;
	__le32 rsv1;
};

enum {
	QDMA_INT_REG_IDX0,
	QDMA_INT_REG_IDX1,
	QDMA_INT_REG_IDX2,
	QDMA_INT_REG_IDX3,
	QDMA_INT_REG_IDX4,
	QDMA_INT_REG_MAX
};

enum {
	XSI_PCIE0_PORT,
	XSI_PCIE1_PORT,
	XSI_USB_PORT,
	XSI_AE_PORT,
	XSI_ETH_PORT,
};

enum {
	XSI_PCIE0_VIP_PORT_MASK	= BIT(22),
	XSI_PCIE1_VIP_PORT_MASK	= BIT(23),
	XSI_USB_VIP_PORT_MASK	= BIT(25),
	XSI_ETH_VIP_PORT_MASK	= BIT(24),
};

enum {
	DEV_STATE_INITIALIZED,
};

enum {
	CDM_CRSN_QSEL_Q1 = 1,
	CDM_CRSN_QSEL_Q5 = 5,
	CDM_CRSN_QSEL_Q6 = 6,
	CDM_CRSN_QSEL_Q15 = 15,
};

enum {
	CRSN_08 = 0x8,
	CRSN_21 = 0x15, /* KA */
	CRSN_22 = 0x16, /* hit bind and force route to CPU */
	CRSN_24 = 0x18,
	CRSN_25 = 0x19,
};

enum {
	FE_PSE_PORT_CDM1,
	FE_PSE_PORT_GDM1,
	FE_PSE_PORT_GDM2,
	FE_PSE_PORT_GDM3,
	FE_PSE_PORT_PPE1,
	FE_PSE_PORT_CDM2,
	FE_PSE_PORT_CDM3,
	FE_PSE_PORT_CDM4,
	FE_PSE_PORT_PPE2,
	FE_PSE_PORT_GDM4,
	FE_PSE_PORT_CDM5,
	FE_PSE_PORT_DROP = 0xf,
};

struct airoha_queue_entry {
	union {
		void *buf;
		struct sk_buff *skb;
	};
	dma_addr_t dma_addr;
	u16 dma_len;
};

struct airoha_queue {
	struct airoha_qdma *qdma;

	/* protect concurrent queue accesses */
	spinlock_t lock;
	struct airoha_queue_entry *entry;
	struct airoha_qdma_desc *desc;
	u16 head;
	u16 tail;

	int queued;
	int ndesc;
	int free_thr;
	int buf_size;

	struct napi_struct napi;
	struct page_pool *page_pool;
};

struct airoha_tx_irq_queue {
	struct airoha_qdma *qdma;

	struct napi_struct napi;
	u32 *q;

	int size;
	int queued;
	u16 head;
};

struct airoha_hw_stats {
	/* protect concurrent hw_stats accesses */
	spinlock_t lock;
	struct u64_stats_sync syncp;

	/* get_stats64 */
	u64 rx_ok_pkts;
	u64 tx_ok_pkts;
	u64 rx_ok_bytes;
	u64 tx_ok_bytes;
	u64 rx_multicast;
	u64 rx_errors;
	u64 rx_drops;
	u64 tx_drops;
	u64 rx_crc_error;
	u64 rx_over_errors;
	/* ethtool stats */
	u64 tx_broadcast;
	u64 tx_multicast;
	u64 tx_len[7];
	u64 rx_broadcast;
	u64 rx_fragment;
	u64 rx_jabber;
	u64 rx_len[7];
};

struct airoha_qdma {
	struct airoha_eth *eth;
	void __iomem *regs;

	/* protect concurrent irqmask accesses */
	spinlock_t irq_lock;
	u32 irqmask[QDMA_INT_REG_MAX];
	int irq;

	struct airoha_tx_irq_queue q_tx_irq[AIROHA_NUM_TX_IRQ];

	struct airoha_queue q_tx[AIROHA_NUM_TX_RING];
	struct airoha_queue q_rx[AIROHA_NUM_RX_RING];

	/* descriptor and packet buffers for qdma hw forward */
	struct {
		void *desc;
		void *q;
	} hfwd;
};

struct airoha_gdm_port {
	struct airoha_qdma *qdma;
	struct net_device *dev;
	int id;

	struct airoha_hw_stats stats;
};

struct airoha_eth {
	struct device *dev;

	unsigned long state;
	void __iomem *fe_regs;

	struct reset_control_bulk_data rsts[AIROHA_MAX_NUM_RSTS];
	struct reset_control_bulk_data xsi_rsts[AIROHA_MAX_NUM_XSI_RSTS];

	struct net_device *napi_dev;

	struct airoha_qdma qdma[AIROHA_MAX_NUM_QDMA];
	struct airoha_gdm_port *ports[AIROHA_MAX_NUM_GDM_PORTS];
};

static u32 airoha_rr(void __iomem *base, u32 offset)
{
	return readl(base + offset);
}

static void airoha_wr(void __iomem *base, u32 offset, u32 val)
{
	writel(val, base + offset);
}

static u32 airoha_rmw(void __iomem *base, u32 offset, u32 mask, u32 val)
{
	val |= (airoha_rr(base, offset) & ~mask);
	airoha_wr(base, offset, val);

	return val;
}

#define airoha_fe_rr(eth, offset)				\
	airoha_rr((eth)->fe_regs, (offset))
#define airoha_fe_wr(eth, offset, val)				\
	airoha_wr((eth)->fe_regs, (offset), (val))
#define airoha_fe_rmw(eth, offset, mask, val)			\
	airoha_rmw((eth)->fe_regs, (offset), (mask), (val))
#define airoha_fe_set(eth, offset, val)				\
	airoha_rmw((eth)->fe_regs, (offset), 0, (val))
#define airoha_fe_clear(eth, offset, val)			\
	airoha_rmw((eth)->fe_regs, (offset), (val), 0)

#define airoha_qdma_rr(qdma, offset)				\
	airoha_rr((qdma)->regs, (offset))
#define airoha_qdma_wr(qdma, offset, val)			\
	airoha_wr((qdma)->regs, (offset), (val))
#define airoha_qdma_rmw(qdma, offset, mask, val)		\
	airoha_rmw((qdma)->regs, (offset), (mask), (val))
#define airoha_qdma_set(qdma, offset, val)			\
	airoha_rmw((qdma)->regs, (offset), 0, (val))
#define airoha_qdma_clear(qdma, offset, val)			\
	airoha_rmw((qdma)->regs, (offset), (val), 0)

static void airoha_qdma_set_irqmask(struct airoha_qdma *qdma, int index,
				    u32 clear, u32 set)
{
	unsigned long flags;

	if (WARN_ON_ONCE(index >= ARRAY_SIZE(qdma->irqmask)))
		return;

	spin_lock_irqsave(&qdma->irq_lock, flags);

	qdma->irqmask[index] &= ~clear;
	qdma->irqmask[index] |= set;
	airoha_qdma_wr(qdma, REG_INT_ENABLE(index), qdma->irqmask[index]);
	/* Read irq_enable register in order to guarantee the update above
	 * completes in the spinlock critical section.
	 */
	airoha_qdma_rr(qdma, REG_INT_ENABLE(index));

	spin_unlock_irqrestore(&qdma->irq_lock, flags);
}

static void airoha_qdma_irq_enable(struct airoha_qdma *qdma, int index,
				   u32 mask)
{
	airoha_qdma_set_irqmask(qdma, index, 0, mask);
}

static void airoha_qdma_irq_disable(struct airoha_qdma *qdma, int index,
				    u32 mask)
{
	airoha_qdma_set_irqmask(qdma, index, mask, 0);
}

static void airoha_set_macaddr(struct airoha_eth *eth, const u8 *addr)
{
	u32 val;

	val = (addr[0] << 16) | (addr[1] << 8) | addr[2];
	airoha_fe_wr(eth, REG_FE_LAN_MAC_H, val);

	val = (addr[3] << 16) | (addr[4] << 8) | addr[5];
	airoha_fe_wr(eth, REG_FE_LAN_MAC_LMIN, val);
	airoha_fe_wr(eth, REG_FE_LAN_MAC_LMAX, val);
}

static void airoha_set_gdm_port_fwd_cfg(struct airoha_eth *eth, u32 addr,
					u32 val)
{
	airoha_fe_rmw(eth, addr, GDM_OCFQ_MASK,
		      FIELD_PREP(GDM_OCFQ_MASK, val));
	airoha_fe_rmw(eth, addr, GDM_MCFQ_MASK,
		      FIELD_PREP(GDM_MCFQ_MASK, val));
	airoha_fe_rmw(eth, addr, GDM_BCFQ_MASK,
		      FIELD_PREP(GDM_BCFQ_MASK, val));
	airoha_fe_rmw(eth, addr, GDM_UCFQ_MASK,
		      FIELD_PREP(GDM_UCFQ_MASK, val));
}

static int airoha_set_gdm_port(struct airoha_eth *eth, int port, bool enable)
{
	u32 val = enable ? FE_PSE_PORT_PPE1 : FE_PSE_PORT_DROP;
	u32 vip_port, cfg_addr;

	switch (port) {
	case XSI_PCIE0_PORT:
		vip_port = XSI_PCIE0_VIP_PORT_MASK;
		cfg_addr = REG_GDM_FWD_CFG(3);
		break;
	case XSI_PCIE1_PORT:
		vip_port = XSI_PCIE1_VIP_PORT_MASK;
		cfg_addr = REG_GDM_FWD_CFG(3);
		break;
	case XSI_USB_PORT:
		vip_port = XSI_USB_VIP_PORT_MASK;
		cfg_addr = REG_GDM_FWD_CFG(4);
		break;
	case XSI_ETH_PORT:
		vip_port = XSI_ETH_VIP_PORT_MASK;
		cfg_addr = REG_GDM_FWD_CFG(4);
		break;
	default:
		return -EINVAL;
	}

	if (enable) {
		airoha_fe_set(eth, REG_FE_VIP_PORT_EN, vip_port);
		airoha_fe_set(eth, REG_FE_IFC_PORT_EN, vip_port);
	} else {
		airoha_fe_clear(eth, REG_FE_VIP_PORT_EN, vip_port);
		airoha_fe_clear(eth, REG_FE_IFC_PORT_EN, vip_port);
	}

	airoha_set_gdm_port_fwd_cfg(eth, cfg_addr, val);

	return 0;
}

static int airoha_set_gdm_ports(struct airoha_eth *eth, bool enable)
{
	const int port_list[] = {
		XSI_PCIE0_PORT,
		XSI_PCIE1_PORT,
		XSI_USB_PORT,
		XSI_ETH_PORT
	};
	int i, err;

	for (i = 0; i < ARRAY_SIZE(port_list); i++) {
		err = airoha_set_gdm_port(eth, port_list[i], enable);
		if (err)
			goto error;
	}

	return 0;

error:
	for (i--; i >= 0; i--)
		airoha_set_gdm_port(eth, port_list[i], false);

	return err;
}

static void airoha_fe_maccr_init(struct airoha_eth *eth)
{
	int p;

	for (p = 1; p <= ARRAY_SIZE(eth->ports); p++) {
		airoha_fe_set(eth, REG_GDM_FWD_CFG(p),
			      GDM_TCP_CKSUM | GDM_UDP_CKSUM | GDM_IP4_CKSUM |
			      GDM_DROP_CRC_ERR);
		airoha_set_gdm_port_fwd_cfg(eth, REG_GDM_FWD_CFG(p),
					    FE_PSE_PORT_CDM1);
		airoha_fe_rmw(eth, REG_GDM_LEN_CFG(p),
			      GDM_SHORT_LEN_MASK | GDM_LONG_LEN_MASK,
			      FIELD_PREP(GDM_SHORT_LEN_MASK, 60) |
			      FIELD_PREP(GDM_LONG_LEN_MASK, 4004));
	}

	airoha_fe_rmw(eth, REG_CDM1_VLAN_CTRL, CDM1_VLAN_MASK,
		      FIELD_PREP(CDM1_VLAN_MASK, 0x8100));

	airoha_fe_set(eth, REG_FE_CPORT_CFG, FE_CPORT_PAD);
}

static void airoha_fe_vip_setup(struct airoha_eth *eth)
{
	airoha_fe_wr(eth, REG_FE_VIP_PATN(3), ETH_P_PPP_DISC);
	airoha_fe_wr(eth, REG_FE_VIP_EN(3), PATN_FCPU_EN_MASK | PATN_EN_MASK);

	airoha_fe_wr(eth, REG_FE_VIP_PATN(4), PPP_LCP);
	airoha_fe_wr(eth, REG_FE_VIP_EN(4),
		     PATN_FCPU_EN_MASK | FIELD_PREP(PATN_TYPE_MASK, 1) |
		     PATN_EN_MASK);

	airoha_fe_wr(eth, REG_FE_VIP_PATN(6), PPP_IPCP);
	airoha_fe_wr(eth, REG_FE_VIP_EN(6),
		     PATN_FCPU_EN_MASK | FIELD_PREP(PATN_TYPE_MASK, 1) |
		     PATN_EN_MASK);

	airoha_fe_wr(eth, REG_FE_VIP_PATN(7), PPP_CHAP);
	airoha_fe_wr(eth, REG_FE_VIP_EN(7),
		     PATN_FCPU_EN_MASK | FIELD_PREP(PATN_TYPE_MASK, 1) |
		     PATN_EN_MASK);

	/* BOOTP (0x43) */
	airoha_fe_wr(eth, REG_FE_VIP_PATN(8), 0x43);
	airoha_fe_wr(eth, REG_FE_VIP_EN(8),
		     PATN_FCPU_EN_MASK | PATN_SP_EN_MASK |
		     FIELD_PREP(PATN_TYPE_MASK, 4) | PATN_EN_MASK);

	/* BOOTP (0x44) */
	airoha_fe_wr(eth, REG_FE_VIP_PATN(9), 0x44);
	airoha_fe_wr(eth, REG_FE_VIP_EN(9),
		     PATN_FCPU_EN_MASK | PATN_SP_EN_MASK |
		     FIELD_PREP(PATN_TYPE_MASK, 4) | PATN_EN_MASK);

	/* ISAKMP */
	airoha_fe_wr(eth, REG_FE_VIP_PATN(10), 0x1f401f4);
	airoha_fe_wr(eth, REG_FE_VIP_EN(10),
		     PATN_FCPU_EN_MASK | PATN_DP_EN_MASK | PATN_SP_EN_MASK |
		     FIELD_PREP(PATN_TYPE_MASK, 4) | PATN_EN_MASK);

	airoha_fe_wr(eth, REG_FE_VIP_PATN(11), PPP_IPV6CP);
	airoha_fe_wr(eth, REG_FE_VIP_EN(11),
		     PATN_FCPU_EN_MASK | FIELD_PREP(PATN_TYPE_MASK, 1) |
		     PATN_EN_MASK);

	/* DHCPv6 */
	airoha_fe_wr(eth, REG_FE_VIP_PATN(12), 0x2220223);
	airoha_fe_wr(eth, REG_FE_VIP_EN(12),
		     PATN_FCPU_EN_MASK | PATN_DP_EN_MASK | PATN_SP_EN_MASK |
		     FIELD_PREP(PATN_TYPE_MASK, 4) | PATN_EN_MASK);

	airoha_fe_wr(eth, REG_FE_VIP_PATN(19), PPP_PAP);
	airoha_fe_wr(eth, REG_FE_VIP_EN(19),
		     PATN_FCPU_EN_MASK | FIELD_PREP(PATN_TYPE_MASK, 1) |
		     PATN_EN_MASK);

	/* ETH->ETH_P_1905 (0x893a) */
	airoha_fe_wr(eth, REG_FE_VIP_PATN(20), 0x893a);
	airoha_fe_wr(eth, REG_FE_VIP_EN(20),
		     PATN_FCPU_EN_MASK | PATN_EN_MASK);

	airoha_fe_wr(eth, REG_FE_VIP_PATN(21), ETH_P_LLDP);
	airoha_fe_wr(eth, REG_FE_VIP_EN(21),
		     PATN_FCPU_EN_MASK | PATN_EN_MASK);
}

static u32 airoha_fe_get_pse_queue_rsv_pages(struct airoha_eth *eth,
					     u32 port, u32 queue)
{
	u32 val;

	airoha_fe_rmw(eth, REG_FE_PSE_QUEUE_CFG_WR,
		      PSE_CFG_PORT_ID_MASK | PSE_CFG_QUEUE_ID_MASK,
		      FIELD_PREP(PSE_CFG_PORT_ID_MASK, port) |
		      FIELD_PREP(PSE_CFG_QUEUE_ID_MASK, queue));
	val = airoha_fe_rr(eth, REG_FE_PSE_QUEUE_CFG_VAL);

	return FIELD_GET(PSE_CFG_OQ_RSV_MASK, val);
}

static void airoha_fe_set_pse_queue_rsv_pages(struct airoha_eth *eth,
					      u32 port, u32 queue, u32 val)
{
	airoha_fe_rmw(eth, REG_FE_PSE_QUEUE_CFG_VAL, PSE_CFG_OQ_RSV_MASK,
		      FIELD_PREP(PSE_CFG_OQ_RSV_MASK, val));
	airoha_fe_rmw(eth, REG_FE_PSE_QUEUE_CFG_WR,
		      PSE_CFG_PORT_ID_MASK | PSE_CFG_QUEUE_ID_MASK |
		      PSE_CFG_WR_EN_MASK | PSE_CFG_OQRSV_SEL_MASK,
		      FIELD_PREP(PSE_CFG_PORT_ID_MASK, port) |
		      FIELD_PREP(PSE_CFG_QUEUE_ID_MASK, queue) |
		      PSE_CFG_WR_EN_MASK | PSE_CFG_OQRSV_SEL_MASK);
}

static int airoha_fe_set_pse_oq_rsv(struct airoha_eth *eth,
				    u32 port, u32 queue, u32 val)
{
	u32 orig_val, tmp, all_rsv, fq_limit;

	airoha_fe_set_pse_queue_rsv_pages(eth, port, queue, val);

	/* modify all rsv */
	orig_val = airoha_fe_get_pse_queue_rsv_pages(eth, port, queue);
	tmp = airoha_fe_rr(eth, REG_FE_PSE_BUF_SET);
	all_rsv = FIELD_GET(PSE_ALLRSV_MASK, tmp);
	all_rsv += (val - orig_val);
	airoha_fe_rmw(eth, REG_FE_PSE_BUF_SET, PSE_ALLRSV_MASK,
		      FIELD_PREP(PSE_ALLRSV_MASK, all_rsv));

	/* modify hthd */
	tmp = airoha_fe_rr(eth, PSE_FQ_CFG);
	fq_limit = FIELD_GET(PSE_FQ_LIMIT_MASK, tmp);
	tmp = fq_limit - all_rsv - 0x20;
	airoha_fe_rmw(eth, REG_PSE_SHARE_USED_THD,
		      PSE_SHARE_USED_HTHD_MASK,
		      FIELD_PREP(PSE_SHARE_USED_HTHD_MASK, tmp));

	tmp = fq_limit - all_rsv - 0x100;
	airoha_fe_rmw(eth, REG_PSE_SHARE_USED_THD,
		      PSE_SHARE_USED_MTHD_MASK,
		      FIELD_PREP(PSE_SHARE_USED_MTHD_MASK, tmp));
	tmp = (3 * tmp) >> 2;
	airoha_fe_rmw(eth, REG_FE_PSE_BUF_SET,
		      PSE_SHARE_USED_LTHD_MASK,
		      FIELD_PREP(PSE_SHARE_USED_LTHD_MASK, tmp));

	return 0;
}

static void airoha_fe_pse_ports_init(struct airoha_eth *eth)
{
	const u32 pse_port_num_queues[] = {
		[FE_PSE_PORT_CDM1] = 6,
		[FE_PSE_PORT_GDM1] = 6,
		[FE_PSE_PORT_GDM2] = 32,
		[FE_PSE_PORT_GDM3] = 6,
		[FE_PSE_PORT_PPE1] = 4,
		[FE_PSE_PORT_CDM2] = 6,
		[FE_PSE_PORT_CDM3] = 8,
		[FE_PSE_PORT_CDM4] = 10,
		[FE_PSE_PORT_PPE2] = 4,
		[FE_PSE_PORT_GDM4] = 2,
		[FE_PSE_PORT_CDM5] = 2,
	};
	int q;

	/* hw misses PPE2 oq rsv */
	airoha_fe_set(eth, REG_FE_PSE_BUF_SET,
		      PSE_RSV_PAGES * pse_port_num_queues[FE_PSE_PORT_PPE2]);

	/* CMD1 */
	for (q = 0; q < pse_port_num_queues[FE_PSE_PORT_CDM1]; q++)
		airoha_fe_set_pse_oq_rsv(eth, FE_PSE_PORT_CDM1, q,
					 PSE_QUEUE_RSV_PAGES);
	/* GMD1 */
	for (q = 0; q < pse_port_num_queues[FE_PSE_PORT_GDM1]; q++)
		airoha_fe_set_pse_oq_rsv(eth, FE_PSE_PORT_GDM1, q,
					 PSE_QUEUE_RSV_PAGES);
	/* GMD2 */
	for (q = 6; q < pse_port_num_queues[FE_PSE_PORT_GDM2]; q++)
		airoha_fe_set_pse_oq_rsv(eth, FE_PSE_PORT_GDM2, q, 0);
	/* GMD3 */
	for (q = 0; q < pse_port_num_queues[FE_PSE_PORT_GDM3]; q++)
		airoha_fe_set_pse_oq_rsv(eth, FE_PSE_PORT_GDM3, q,
					 PSE_QUEUE_RSV_PAGES);
	/* PPE1 */
	for (q = 0; q < pse_port_num_queues[FE_PSE_PORT_PPE1]; q++) {
		if (q < pse_port_num_queues[FE_PSE_PORT_PPE1])
			airoha_fe_set_pse_oq_rsv(eth, FE_PSE_PORT_PPE1, q,
						 PSE_QUEUE_RSV_PAGES);
		else
			airoha_fe_set_pse_oq_rsv(eth, FE_PSE_PORT_PPE1, q, 0);
	}
	/* CDM2 */
	for (q = 0; q < pse_port_num_queues[FE_PSE_PORT_CDM2]; q++)
		airoha_fe_set_pse_oq_rsv(eth, FE_PSE_PORT_CDM2, q,
					 PSE_QUEUE_RSV_PAGES);
	/* CDM3 */
	for (q = 0; q < pse_port_num_queues[FE_PSE_PORT_CDM3] - 1; q++)
		airoha_fe_set_pse_oq_rsv(eth, FE_PSE_PORT_CDM3, q, 0);
	/* CDM4 */
	for (q = 4; q < pse_port_num_queues[FE_PSE_PORT_CDM4]; q++)
		airoha_fe_set_pse_oq_rsv(eth, FE_PSE_PORT_CDM4, q,
					 PSE_QUEUE_RSV_PAGES);
	/* PPE2 */
	for (q = 0; q < pse_port_num_queues[FE_PSE_PORT_PPE2]; q++) {
		if (q < pse_port_num_queues[FE_PSE_PORT_PPE2] / 2)
			airoha_fe_set_pse_oq_rsv(eth, FE_PSE_PORT_PPE2, q,
						 PSE_QUEUE_RSV_PAGES);
		else
			airoha_fe_set_pse_oq_rsv(eth, FE_PSE_PORT_PPE2, q, 0);
	}
	/* GMD4 */
	for (q = 0; q < pse_port_num_queues[FE_PSE_PORT_GDM4]; q++)
		airoha_fe_set_pse_oq_rsv(eth, FE_PSE_PORT_GDM4, q,
					 PSE_QUEUE_RSV_PAGES);
	/* CDM5 */
	for (q = 0; q < pse_port_num_queues[FE_PSE_PORT_CDM5]; q++)
		airoha_fe_set_pse_oq_rsv(eth, FE_PSE_PORT_CDM5, q,
					 PSE_QUEUE_RSV_PAGES);
}

static int airoha_fe_mc_vlan_clear(struct airoha_eth *eth)
{
	int i;

	for (i = 0; i < AIROHA_FE_MC_MAX_VLAN_TABLE; i++) {
		int err, j;
		u32 val;

		airoha_fe_wr(eth, REG_MC_VLAN_DATA, 0x0);

		val = FIELD_PREP(MC_VLAN_CFG_TABLE_ID_MASK, i) |
		      MC_VLAN_CFG_TABLE_SEL_MASK | MC_VLAN_CFG_RW_MASK;
		airoha_fe_wr(eth, REG_MC_VLAN_CFG, val);
		err = read_poll_timeout(airoha_fe_rr, val,
					val & MC_VLAN_CFG_CMD_DONE_MASK,
					USEC_PER_MSEC, 5 * USEC_PER_MSEC,
					false, eth, REG_MC_VLAN_CFG);
		if (err)
			return err;

		for (j = 0; j < AIROHA_FE_MC_MAX_VLAN_PORT; j++) {
			airoha_fe_wr(eth, REG_MC_VLAN_DATA, 0x0);

			val = FIELD_PREP(MC_VLAN_CFG_TABLE_ID_MASK, i) |
			      FIELD_PREP(MC_VLAN_CFG_PORT_ID_MASK, j) |
			      MC_VLAN_CFG_RW_MASK;
			airoha_fe_wr(eth, REG_MC_VLAN_CFG, val);
			err = read_poll_timeout(airoha_fe_rr, val,
						val & MC_VLAN_CFG_CMD_DONE_MASK,
						USEC_PER_MSEC,
						5 * USEC_PER_MSEC, false, eth,
						REG_MC_VLAN_CFG);
			if (err)
				return err;
		}
	}

	return 0;
}

static void airoha_fe_crsn_qsel_init(struct airoha_eth *eth)
{
	/* CDM1_CRSN_QSEL */
	airoha_fe_rmw(eth, REG_CDM1_CRSN_QSEL(CRSN_22 >> 2),
		      CDM1_CRSN_QSEL_REASON_MASK(CRSN_22),
		      FIELD_PREP(CDM1_CRSN_QSEL_REASON_MASK(CRSN_22),
				 CDM_CRSN_QSEL_Q1));
	airoha_fe_rmw(eth, REG_CDM1_CRSN_QSEL(CRSN_08 >> 2),
		      CDM1_CRSN_QSEL_REASON_MASK(CRSN_08),
		      FIELD_PREP(CDM1_CRSN_QSEL_REASON_MASK(CRSN_08),
				 CDM_CRSN_QSEL_Q1));
	airoha_fe_rmw(eth, REG_CDM1_CRSN_QSEL(CRSN_21 >> 2),
		      CDM1_CRSN_QSEL_REASON_MASK(CRSN_21),
		      FIELD_PREP(CDM1_CRSN_QSEL_REASON_MASK(CRSN_21),
				 CDM_CRSN_QSEL_Q1));
	airoha_fe_rmw(eth, REG_CDM1_CRSN_QSEL(CRSN_24 >> 2),
		      CDM1_CRSN_QSEL_REASON_MASK(CRSN_24),
		      FIELD_PREP(CDM1_CRSN_QSEL_REASON_MASK(CRSN_24),
				 CDM_CRSN_QSEL_Q6));
	airoha_fe_rmw(eth, REG_CDM1_CRSN_QSEL(CRSN_25 >> 2),
		      CDM1_CRSN_QSEL_REASON_MASK(CRSN_25),
		      FIELD_PREP(CDM1_CRSN_QSEL_REASON_MASK(CRSN_25),
				 CDM_CRSN_QSEL_Q1));
	/* CDM2_CRSN_QSEL */
	airoha_fe_rmw(eth, REG_CDM2_CRSN_QSEL(CRSN_08 >> 2),
		      CDM2_CRSN_QSEL_REASON_MASK(CRSN_08),
		      FIELD_PREP(CDM2_CRSN_QSEL_REASON_MASK(CRSN_08),
				 CDM_CRSN_QSEL_Q1));
	airoha_fe_rmw(eth, REG_CDM2_CRSN_QSEL(CRSN_21 >> 2),
		      CDM2_CRSN_QSEL_REASON_MASK(CRSN_21),
		      FIELD_PREP(CDM2_CRSN_QSEL_REASON_MASK(CRSN_21),
				 CDM_CRSN_QSEL_Q1));
	airoha_fe_rmw(eth, REG_CDM2_CRSN_QSEL(CRSN_22 >> 2),
		      CDM2_CRSN_QSEL_REASON_MASK(CRSN_22),
		      FIELD_PREP(CDM2_CRSN_QSEL_REASON_MASK(CRSN_22),
				 CDM_CRSN_QSEL_Q1));
	airoha_fe_rmw(eth, REG_CDM2_CRSN_QSEL(CRSN_24 >> 2),
		      CDM2_CRSN_QSEL_REASON_MASK(CRSN_24),
		      FIELD_PREP(CDM2_CRSN_QSEL_REASON_MASK(CRSN_24),
				 CDM_CRSN_QSEL_Q6));
	airoha_fe_rmw(eth, REG_CDM2_CRSN_QSEL(CRSN_25 >> 2),
		      CDM2_CRSN_QSEL_REASON_MASK(CRSN_25),
		      FIELD_PREP(CDM2_CRSN_QSEL_REASON_MASK(CRSN_25),
				 CDM_CRSN_QSEL_Q1));
}

static int airoha_fe_init(struct airoha_eth *eth)
{
	airoha_fe_maccr_init(eth);

	/* PSE IQ reserve */
	airoha_fe_rmw(eth, REG_PSE_IQ_REV1, PSE_IQ_RES1_P2_MASK,
		      FIELD_PREP(PSE_IQ_RES1_P2_MASK, 0x10));
	airoha_fe_rmw(eth, REG_PSE_IQ_REV2,
		      PSE_IQ_RES2_P5_MASK | PSE_IQ_RES2_P4_MASK,
		      FIELD_PREP(PSE_IQ_RES2_P5_MASK, 0x40) |
		      FIELD_PREP(PSE_IQ_RES2_P4_MASK, 0x34));

	/* enable FE copy engine for MC/KA/DPI */
	airoha_fe_wr(eth, REG_FE_PCE_CFG,
		     PCE_DPI_EN_MASK | PCE_KA_EN_MASK | PCE_MC_EN_MASK);
	/* set vip queue selection to ring 1 */
	airoha_fe_rmw(eth, REG_CDM1_FWD_CFG, CDM1_VIP_QSEL_MASK,
		      FIELD_PREP(CDM1_VIP_QSEL_MASK, 0x4));
	airoha_fe_rmw(eth, REG_CDM2_FWD_CFG, CDM2_VIP_QSEL_MASK,
		      FIELD_PREP(CDM2_VIP_QSEL_MASK, 0x4));
	/* set GDM4 source interface offset to 8 */
	airoha_fe_rmw(eth, REG_GDM4_SRC_PORT_SET,
		      GDM4_SPORT_OFF2_MASK |
		      GDM4_SPORT_OFF1_MASK |
		      GDM4_SPORT_OFF0_MASK,
		      FIELD_PREP(GDM4_SPORT_OFF2_MASK, 8) |
		      FIELD_PREP(GDM4_SPORT_OFF1_MASK, 8) |
		      FIELD_PREP(GDM4_SPORT_OFF0_MASK, 8));

	/* set PSE Page as 128B */
	airoha_fe_rmw(eth, REG_FE_DMA_GLO_CFG,
		      FE_DMA_GLO_L2_SPACE_MASK | FE_DMA_GLO_PG_SZ_MASK,
		      FIELD_PREP(FE_DMA_GLO_L2_SPACE_MASK, 2) |
		      FE_DMA_GLO_PG_SZ_MASK);
	airoha_fe_wr(eth, REG_FE_RST_GLO_CFG,
		     FE_RST_CORE_MASK | FE_RST_GDM3_MBI_ARB_MASK |
		     FE_RST_GDM4_MBI_ARB_MASK);
	usleep_range(1000, 2000);

	/* connect RxRing1 and RxRing15 to PSE Port0 OQ-1
	 * connect other rings to PSE Port0 OQ-0
	 */
	airoha_fe_wr(eth, REG_FE_CDM1_OQ_MAP0, BIT(4));
	airoha_fe_wr(eth, REG_FE_CDM1_OQ_MAP1, BIT(28));
	airoha_fe_wr(eth, REG_FE_CDM1_OQ_MAP2, BIT(4));
	airoha_fe_wr(eth, REG_FE_CDM1_OQ_MAP3, BIT(28));

	airoha_fe_vip_setup(eth);
	airoha_fe_pse_ports_init(eth);

	airoha_fe_set(eth, REG_GDM_MISC_CFG,
		      GDM2_RDM_ACK_WAIT_PREF_MASK |
		      GDM2_CHN_VLD_MODE_MASK);
	airoha_fe_rmw(eth, REG_CDM2_FWD_CFG, CDM2_OAM_QSEL_MASK, 15);

	/* init fragment and assemble Force Port */
	/* NPU Core-3, NPU Bridge Channel-3 */
	airoha_fe_rmw(eth, REG_IP_FRAG_FP,
		      IP_FRAGMENT_PORT_MASK | IP_FRAGMENT_NBQ_MASK,
		      FIELD_PREP(IP_FRAGMENT_PORT_MASK, 6) |
		      FIELD_PREP(IP_FRAGMENT_NBQ_MASK, 3));
	/* QDMA LAN, RX Ring-22 */
	airoha_fe_rmw(eth, REG_IP_FRAG_FP,
		      IP_ASSEMBLE_PORT_MASK | IP_ASSEMBLE_NBQ_MASK,
		      FIELD_PREP(IP_ASSEMBLE_PORT_MASK, 0) |
		      FIELD_PREP(IP_ASSEMBLE_NBQ_MASK, 22));

	airoha_fe_set(eth, REG_GDM3_FWD_CFG, GDM3_PAD_EN_MASK);
	airoha_fe_set(eth, REG_GDM4_FWD_CFG, GDM4_PAD_EN_MASK);

	airoha_fe_crsn_qsel_init(eth);

	airoha_fe_clear(eth, REG_FE_CPORT_CFG, FE_CPORT_QUEUE_XFC_MASK);
	airoha_fe_set(eth, REG_FE_CPORT_CFG, FE_CPORT_PORT_XFC_MASK);

	/* default aging mode for mbi unlock issue */
	airoha_fe_rmw(eth, REG_GDM2_CHN_RLS,
		      MBI_RX_AGE_SEL_MASK | MBI_TX_AGE_SEL_MASK,
		      FIELD_PREP(MBI_RX_AGE_SEL_MASK, 3) |
		      FIELD_PREP(MBI_TX_AGE_SEL_MASK, 3));

	/* disable IFC by default */
	airoha_fe_clear(eth, REG_FE_CSR_IFC_CFG, FE_IFC_EN_MASK);

	/* enable 1:N vlan action, init vlan table */
	airoha_fe_set(eth, REG_MC_VLAN_EN, MC_VLAN_EN_MASK);

	return airoha_fe_mc_vlan_clear(eth);
}

static int airoha_qdma_fill_rx_queue(struct airoha_queue *q)
{
	enum dma_data_direction dir = page_pool_get_dma_dir(q->page_pool);
	struct airoha_qdma *qdma = q->qdma;
	struct airoha_eth *eth = qdma->eth;
	int qid = q - &qdma->q_rx[0];
	int nframes = 0;

	while (q->queued < q->ndesc - 1) {
		struct airoha_queue_entry *e = &q->entry[q->head];
		struct airoha_qdma_desc *desc = &q->desc[q->head];
		struct page *page;
		int offset;
		u32 val;

		page = page_pool_dev_alloc_frag(q->page_pool, &offset,
						q->buf_size);
		if (!page)
			break;

		q->head = (q->head + 1) % q->ndesc;
		q->queued++;
		nframes++;

		e->buf = page_address(page) + offset;
		e->dma_addr = page_pool_get_dma_addr(page) + offset;
		e->dma_len = SKB_WITH_OVERHEAD(q->buf_size);

		dma_sync_single_for_device(eth->dev, e->dma_addr, e->dma_len,
					   dir);

		val = FIELD_PREP(QDMA_DESC_LEN_MASK, e->dma_len);
		WRITE_ONCE(desc->ctrl, cpu_to_le32(val));
		WRITE_ONCE(desc->addr, cpu_to_le32(e->dma_addr));
		val = FIELD_PREP(QDMA_DESC_NEXT_ID_MASK, q->head);
		WRITE_ONCE(desc->data, cpu_to_le32(val));
		WRITE_ONCE(desc->msg0, 0);
		WRITE_ONCE(desc->msg1, 0);
		WRITE_ONCE(desc->msg2, 0);
		WRITE_ONCE(desc->msg3, 0);

		airoha_qdma_rmw(qdma, REG_RX_CPU_IDX(qid),
				RX_RING_CPU_IDX_MASK,
				FIELD_PREP(RX_RING_CPU_IDX_MASK, q->head));
	}

	return nframes;
}

static int airoha_qdma_get_gdm_port(struct airoha_eth *eth,
				    struct airoha_qdma_desc *desc)
{
	u32 port, sport, msg1 = le32_to_cpu(desc->msg1);

	sport = FIELD_GET(QDMA_ETH_RXMSG_SPORT_MASK, msg1);
	switch (sport) {
	case 0x10 ... 0x13:
		port = 0;
		break;
	case 0x2 ... 0x4:
		port = sport - 1;
		break;
	default:
		return -EINVAL;
	}

	return port >= ARRAY_SIZE(eth->ports) ? -EINVAL : port;
}

static int airoha_qdma_rx_process(struct airoha_queue *q, int budget)
{
	enum dma_data_direction dir = page_pool_get_dma_dir(q->page_pool);
	struct airoha_qdma *qdma = q->qdma;
	struct airoha_eth *eth = qdma->eth;
	int qid = q - &qdma->q_rx[0];
	int done = 0;

	while (done < budget) {
		struct airoha_queue_entry *e = &q->entry[q->tail];
		struct airoha_qdma_desc *desc = &q->desc[q->tail];
		dma_addr_t dma_addr = le32_to_cpu(desc->addr);
		u32 desc_ctrl = le32_to_cpu(desc->ctrl);
		struct sk_buff *skb;
		int len, p;

		if (!(desc_ctrl & QDMA_DESC_DONE_MASK))
			break;

		if (!dma_addr)
			break;

		len = FIELD_GET(QDMA_DESC_LEN_MASK, desc_ctrl);
		if (!len)
			break;

		q->tail = (q->tail + 1) % q->ndesc;
		q->queued--;

		dma_sync_single_for_cpu(eth->dev, dma_addr,
					SKB_WITH_OVERHEAD(q->buf_size), dir);

		p = airoha_qdma_get_gdm_port(eth, desc);
		if (p < 0 || !eth->ports[p]) {
			page_pool_put_full_page(q->page_pool,
						virt_to_head_page(e->buf),
						true);
			continue;
		}

		skb = napi_build_skb(e->buf, q->buf_size);
		if (!skb) {
			page_pool_put_full_page(q->page_pool,
						virt_to_head_page(e->buf),
						true);
			break;
		}

		skb_reserve(skb, 2);
		__skb_put(skb, len);
		skb_mark_for_recycle(skb);
		skb->dev = eth->ports[p]->dev;
		skb->protocol = eth_type_trans(skb, skb->dev);
		skb->ip_summed = CHECKSUM_UNNECESSARY;
		skb_record_rx_queue(skb, qid);
		napi_gro_receive(&q->napi, skb);

		done++;
	}
	airoha_qdma_fill_rx_queue(q);

	return done;
}

static int airoha_qdma_rx_napi_poll(struct napi_struct *napi, int budget)
{
	struct airoha_queue *q = container_of(napi, struct airoha_queue, napi);
	int cur, done = 0;

	do {
		cur = airoha_qdma_rx_process(q, budget - done);
		done += cur;
	} while (cur && done < budget);

	if (done < budget && napi_complete(napi))
		airoha_qdma_irq_enable(q->qdma, QDMA_INT_REG_IDX1,
				       RX_DONE_INT_MASK);

	return done;
}

static int airoha_qdma_init_rx_queue(struct airoha_queue *q,
				     struct airoha_qdma *qdma, int ndesc)
{
	const struct page_pool_params pp_params = {
		.order = 0,
		.pool_size = 256,
		.flags = PP_FLAG_DMA_MAP | PP_FLAG_DMA_SYNC_DEV,
		.dma_dir = DMA_FROM_DEVICE,
		.max_len = PAGE_SIZE,
		.nid = NUMA_NO_NODE,
		.dev = qdma->eth->dev,
		.napi = &q->napi,
	};
	struct airoha_eth *eth = qdma->eth;
	int qid = q - &qdma->q_rx[0], thr;
	dma_addr_t dma_addr;

	q->buf_size = PAGE_SIZE / 2;
	q->ndesc = ndesc;
	q->qdma = qdma;

	q->entry = devm_kzalloc(eth->dev, q->ndesc * sizeof(*q->entry),
				GFP_KERNEL);
	if (!q->entry)
		return -ENOMEM;

	q->page_pool = page_pool_create(&pp_params);
	if (IS_ERR(q->page_pool)) {
		int err = PTR_ERR(q->page_pool);

		q->page_pool = NULL;
		return err;
	}

	q->desc = dmam_alloc_coherent(eth->dev, q->ndesc * sizeof(*q->desc),
				      &dma_addr, GFP_KERNEL);
	if (!q->desc)
		return -ENOMEM;

	netif_napi_add(eth->napi_dev, &q->napi, airoha_qdma_rx_napi_poll);

	airoha_qdma_wr(qdma, REG_RX_RING_BASE(qid), dma_addr);
	airoha_qdma_rmw(qdma, REG_RX_RING_SIZE(qid),
			RX_RING_SIZE_MASK,
			FIELD_PREP(RX_RING_SIZE_MASK, ndesc));

	thr = clamp(ndesc >> 3, 1, 32);
	airoha_qdma_rmw(qdma, REG_RX_RING_SIZE(qid), RX_RING_THR_MASK,
			FIELD_PREP(RX_RING_THR_MASK, thr));
	airoha_qdma_rmw(qdma, REG_RX_DMA_IDX(qid), RX_RING_DMA_IDX_MASK,
			FIELD_PREP(RX_RING_DMA_IDX_MASK, q->head));

	airoha_qdma_fill_rx_queue(q);

	return 0;
}

static void airoha_qdma_cleanup_rx_queue(struct airoha_queue *q)
{
	struct airoha_eth *eth = q->qdma->eth;

	while (q->queued) {
		struct airoha_queue_entry *e = &q->entry[q->tail];
		struct page *page = virt_to_head_page(e->buf);

		dma_sync_single_for_cpu(eth->dev, e->dma_addr, e->dma_len,
					page_pool_get_dma_dir(q->page_pool));
		page_pool_put_full_page(q->page_pool, page, false);
		q->tail = (q->tail + 1) % q->ndesc;
		q->queued--;
	}
}

static int airoha_qdma_init_rx(struct airoha_qdma *qdma)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(qdma->q_rx); i++) {
		int err;

		if (!(RX_DONE_INT_MASK & BIT(i))) {
			/* rx-queue not binded to irq */
			continue;
		}

		err = airoha_qdma_init_rx_queue(&qdma->q_rx[i], qdma,
						RX_DSCP_NUM(i));
		if (err)
			return err;
	}

	return 0;
}

static int airoha_qdma_tx_napi_poll(struct napi_struct *napi, int budget)
{
	struct airoha_tx_irq_queue *irq_q;
	struct airoha_qdma *qdma;
	struct airoha_eth *eth;
	int id, done = 0;

	irq_q = container_of(napi, struct airoha_tx_irq_queue, napi);
	qdma = irq_q->qdma;
	id = irq_q - &qdma->q_tx_irq[0];
	eth = qdma->eth;

	while (irq_q->queued > 0 && done < budget) {
		u32 qid, last, val = irq_q->q[irq_q->head];
		struct airoha_queue *q;

		if (val == 0xff)
			break;

		irq_q->q[irq_q->head] = 0xff; /* mark as done */
		irq_q->head = (irq_q->head + 1) % irq_q->size;
		irq_q->queued--;
		done++;

		last = FIELD_GET(IRQ_DESC_IDX_MASK, val);
		qid = FIELD_GET(IRQ_RING_IDX_MASK, val);

		if (qid >= ARRAY_SIZE(qdma->q_tx))
			continue;

		q = &qdma->q_tx[qid];
		if (!q->ndesc)
			continue;

		spin_lock_bh(&q->lock);

		while (q->queued > 0) {
			struct airoha_qdma_desc *desc = &q->desc[q->tail];
			struct airoha_queue_entry *e = &q->entry[q->tail];
			u32 desc_ctrl = le32_to_cpu(desc->ctrl);
			struct sk_buff *skb = e->skb;
			u16 index = q->tail;

			if (!(desc_ctrl & QDMA_DESC_DONE_MASK) &&
			    !(desc_ctrl & QDMA_DESC_DROP_MASK))
				break;

			q->tail = (q->tail + 1) % q->ndesc;
			q->queued--;

			dma_unmap_single(eth->dev, e->dma_addr, e->dma_len,
					 DMA_TO_DEVICE);

			WRITE_ONCE(desc->msg0, 0);
			WRITE_ONCE(desc->msg1, 0);

			if (skb) {
				struct netdev_queue *txq;

				txq = netdev_get_tx_queue(skb->dev, qid);
				if (netif_tx_queue_stopped(txq) &&
				    q->ndesc - q->queued >= q->free_thr)
					netif_tx_wake_queue(txq);

				dev_kfree_skb_any(skb);
				e->skb = NULL;
			}

			if (index == last)
				break;
		}

		spin_unlock_bh(&q->lock);
	}

	if (done) {
		int i, len = done >> 7;

		for (i = 0; i < len; i++)
			airoha_qdma_rmw(qdma, REG_IRQ_CLEAR_LEN(id),
					IRQ_CLEAR_LEN_MASK, 0x80);
		airoha_qdma_rmw(qdma, REG_IRQ_CLEAR_LEN(id),
				IRQ_CLEAR_LEN_MASK, (done & 0x7f));
	}

	if (done < budget && napi_complete(napi))
		airoha_qdma_irq_enable(qdma, QDMA_INT_REG_IDX0,
				       TX_DONE_INT_MASK(id));

	return done;
}

static int airoha_qdma_init_tx_queue(struct airoha_queue *q,
				     struct airoha_qdma *qdma, int size)
{
	struct airoha_eth *eth = qdma->eth;
	int i, qid = q - &qdma->q_tx[0];
	dma_addr_t dma_addr;

	spin_lock_init(&q->lock);
	q->ndesc = size;
	q->qdma = qdma;
	q->free_thr = 1 + MAX_SKB_FRAGS;

	q->entry = devm_kzalloc(eth->dev, q->ndesc * sizeof(*q->entry),
				GFP_KERNEL);
	if (!q->entry)
		return -ENOMEM;

	q->desc = dmam_alloc_coherent(eth->dev, q->ndesc * sizeof(*q->desc),
				      &dma_addr, GFP_KERNEL);
	if (!q->desc)
		return -ENOMEM;

	for (i = 0; i < q->ndesc; i++) {
		u32 val;

		val = FIELD_PREP(QDMA_DESC_DONE_MASK, 1);
		WRITE_ONCE(q->desc[i].ctrl, cpu_to_le32(val));
	}

	airoha_qdma_wr(qdma, REG_TX_RING_BASE(qid), dma_addr);
	airoha_qdma_rmw(qdma, REG_TX_CPU_IDX(qid), TX_RING_CPU_IDX_MASK,
			FIELD_PREP(TX_RING_CPU_IDX_MASK, q->head));
	airoha_qdma_rmw(qdma, REG_TX_DMA_IDX(qid), TX_RING_DMA_IDX_MASK,
			FIELD_PREP(TX_RING_DMA_IDX_MASK, q->head));

	return 0;
}

static int airoha_qdma_tx_irq_init(struct airoha_tx_irq_queue *irq_q,
				   struct airoha_qdma *qdma, int size)
{
	int id = irq_q - &qdma->q_tx_irq[0];
	struct airoha_eth *eth = qdma->eth;
	dma_addr_t dma_addr;

	netif_napi_add_tx(eth->napi_dev, &irq_q->napi,
			  airoha_qdma_tx_napi_poll);
	irq_q->q = dmam_alloc_coherent(eth->dev, size * sizeof(u32),
				       &dma_addr, GFP_KERNEL);
	if (!irq_q->q)
		return -ENOMEM;

	memset(irq_q->q, 0xff, size * sizeof(u32));
	irq_q->size = size;
	irq_q->qdma = qdma;

	airoha_qdma_wr(qdma, REG_TX_IRQ_BASE(id), dma_addr);
	airoha_qdma_rmw(qdma, REG_TX_IRQ_CFG(id), TX_IRQ_DEPTH_MASK,
			FIELD_PREP(TX_IRQ_DEPTH_MASK, size));
	airoha_qdma_rmw(qdma, REG_TX_IRQ_CFG(id), TX_IRQ_THR_MASK,
			FIELD_PREP(TX_IRQ_THR_MASK, 1));

	return 0;
}

static int airoha_qdma_init_tx(struct airoha_qdma *qdma)
{
	int i, err;

	for (i = 0; i < ARRAY_SIZE(qdma->q_tx_irq); i++) {
		err = airoha_qdma_tx_irq_init(&qdma->q_tx_irq[i], qdma,
					      IRQ_QUEUE_LEN(i));
		if (err)
			return err;
	}

	for (i = 0; i < ARRAY_SIZE(qdma->q_tx); i++) {
		err = airoha_qdma_init_tx_queue(&qdma->q_tx[i], qdma,
						TX_DSCP_NUM);
		if (err)
			return err;
	}

	return 0;
}

static void airoha_qdma_cleanup_tx_queue(struct airoha_queue *q)
{
	struct airoha_eth *eth = q->qdma->eth;

	spin_lock_bh(&q->lock);
	while (q->queued) {
		struct airoha_queue_entry *e = &q->entry[q->tail];

		dma_unmap_single(eth->dev, e->dma_addr, e->dma_len,
				 DMA_TO_DEVICE);
		dev_kfree_skb_any(e->skb);
		e->skb = NULL;

		q->tail = (q->tail + 1) % q->ndesc;
		q->queued--;
	}
	spin_unlock_bh(&q->lock);
}

static int airoha_qdma_init_hfwd_queues(struct airoha_qdma *qdma)
{
	struct airoha_eth *eth = qdma->eth;
	dma_addr_t dma_addr;
	u32 status;
	int size;

	size = HW_DSCP_NUM * sizeof(struct airoha_qdma_fwd_desc);
	qdma->hfwd.desc = dmam_alloc_coherent(eth->dev, size, &dma_addr,
					      GFP_KERNEL);
	if (!qdma->hfwd.desc)
		return -ENOMEM;

	airoha_qdma_wr(qdma, REG_FWD_DSCP_BASE, dma_addr);

	size = AIROHA_MAX_PACKET_SIZE * HW_DSCP_NUM;
	qdma->hfwd.q = dmam_alloc_coherent(eth->dev, size, &dma_addr,
					   GFP_KERNEL);
	if (!qdma->hfwd.q)
		return -ENOMEM;

	airoha_qdma_wr(qdma, REG_FWD_BUF_BASE, dma_addr);

	airoha_qdma_rmw(qdma, REG_HW_FWD_DSCP_CFG,
			HW_FWD_DSCP_PAYLOAD_SIZE_MASK,
			FIELD_PREP(HW_FWD_DSCP_PAYLOAD_SIZE_MASK, 0));
	airoha_qdma_rmw(qdma, REG_FWD_DSCP_LOW_THR, FWD_DSCP_LOW_THR_MASK,
			FIELD_PREP(FWD_DSCP_LOW_THR_MASK, 128));
	airoha_qdma_rmw(qdma, REG_LMGR_INIT_CFG,
			LMGR_INIT_START | LMGR_SRAM_MODE_MASK |
			HW_FWD_DESC_NUM_MASK,
			FIELD_PREP(HW_FWD_DESC_NUM_MASK, HW_DSCP_NUM) |
			LMGR_INIT_START);

	return read_poll_timeout(airoha_qdma_rr, status,
				 !(status & LMGR_INIT_START), USEC_PER_MSEC,
				 30 * USEC_PER_MSEC, true, qdma,
				 REG_LMGR_INIT_CFG);
}

static void airoha_qdma_init_qos(struct airoha_qdma *qdma)
{
	airoha_qdma_clear(qdma, REG_TXWRR_MODE_CFG, TWRR_WEIGHT_SCALE_MASK);
	airoha_qdma_set(qdma, REG_TXWRR_MODE_CFG, TWRR_WEIGHT_BASE_MASK);

	airoha_qdma_clear(qdma, REG_PSE_BUF_USAGE_CFG,
			  PSE_BUF_ESTIMATE_EN_MASK);

	airoha_qdma_set(qdma, REG_EGRESS_RATE_METER_CFG,
			EGRESS_RATE_METER_EN_MASK |
			EGRESS_RATE_METER_EQ_RATE_EN_MASK);
	/* 2047us x 31 = 63.457ms */
	airoha_qdma_rmw(qdma, REG_EGRESS_RATE_METER_CFG,
			EGRESS_RATE_METER_WINDOW_SZ_MASK,
			FIELD_PREP(EGRESS_RATE_METER_WINDOW_SZ_MASK, 0x1f));
	airoha_qdma_rmw(qdma, REG_EGRESS_RATE_METER_CFG,
			EGRESS_RATE_METER_TIMESLICE_MASK,
			FIELD_PREP(EGRESS_RATE_METER_TIMESLICE_MASK, 0x7ff));

	/* ratelimit init */
	airoha_qdma_set(qdma, REG_GLB_TRTCM_CFG, GLB_TRTCM_EN_MASK);
	/* fast-tick 25us */
	airoha_qdma_rmw(qdma, REG_GLB_TRTCM_CFG, GLB_FAST_TICK_MASK,
			FIELD_PREP(GLB_FAST_TICK_MASK, 25));
	airoha_qdma_rmw(qdma, REG_GLB_TRTCM_CFG, GLB_SLOW_TICK_RATIO_MASK,
			FIELD_PREP(GLB_SLOW_TICK_RATIO_MASK, 40));

	airoha_qdma_set(qdma, REG_EGRESS_TRTCM_CFG, EGRESS_TRTCM_EN_MASK);
	airoha_qdma_rmw(qdma, REG_EGRESS_TRTCM_CFG, EGRESS_FAST_TICK_MASK,
			FIELD_PREP(EGRESS_FAST_TICK_MASK, 25));
	airoha_qdma_rmw(qdma, REG_EGRESS_TRTCM_CFG,
			EGRESS_SLOW_TICK_RATIO_MASK,
			FIELD_PREP(EGRESS_SLOW_TICK_RATIO_MASK, 40));

	airoha_qdma_set(qdma, REG_INGRESS_TRTCM_CFG, INGRESS_TRTCM_EN_MASK);
	airoha_qdma_clear(qdma, REG_INGRESS_TRTCM_CFG,
			  INGRESS_TRTCM_MODE_MASK);
	airoha_qdma_rmw(qdma, REG_INGRESS_TRTCM_CFG, INGRESS_FAST_TICK_MASK,
			FIELD_PREP(INGRESS_FAST_TICK_MASK, 125));
	airoha_qdma_rmw(qdma, REG_INGRESS_TRTCM_CFG,
			INGRESS_SLOW_TICK_RATIO_MASK,
			FIELD_PREP(INGRESS_SLOW_TICK_RATIO_MASK, 8));

	airoha_qdma_set(qdma, REG_SLA_TRTCM_CFG, SLA_TRTCM_EN_MASK);
	airoha_qdma_rmw(qdma, REG_SLA_TRTCM_CFG, SLA_FAST_TICK_MASK,
			FIELD_PREP(SLA_FAST_TICK_MASK, 25));
	airoha_qdma_rmw(qdma, REG_SLA_TRTCM_CFG, SLA_SLOW_TICK_RATIO_MASK,
			FIELD_PREP(SLA_SLOW_TICK_RATIO_MASK, 40));
}

static int airoha_qdma_hw_init(struct airoha_qdma *qdma)
{
	int i;

	/* clear pending irqs */
	for (i = 0; i < ARRAY_SIZE(qdma->irqmask); i++)
		airoha_qdma_wr(qdma, REG_INT_STATUS(i), 0xffffffff);

	/* setup irqs */
	airoha_qdma_irq_enable(qdma, QDMA_INT_REG_IDX0, INT_IDX0_MASK);
	airoha_qdma_irq_enable(qdma, QDMA_INT_REG_IDX1, INT_IDX1_MASK);
	airoha_qdma_irq_enable(qdma, QDMA_INT_REG_IDX4, INT_IDX4_MASK);

	/* setup irq binding */
	for (i = 0; i < ARRAY_SIZE(qdma->q_tx); i++) {
		if (!qdma->q_tx[i].ndesc)
			continue;

		if (TX_RING_IRQ_BLOCKING_MAP_MASK & BIT(i))
			airoha_qdma_set(qdma, REG_TX_RING_BLOCKING(i),
					TX_RING_IRQ_BLOCKING_CFG_MASK);
		else
			airoha_qdma_clear(qdma, REG_TX_RING_BLOCKING(i),
					  TX_RING_IRQ_BLOCKING_CFG_MASK);
	}

	airoha_qdma_wr(qdma, REG_QDMA_GLOBAL_CFG,
		       GLOBAL_CFG_RX_2B_OFFSET_MASK |
		       FIELD_PREP(GLOBAL_CFG_DMA_PREFERENCE_MASK, 3) |
		       GLOBAL_CFG_CPU_TXR_RR_MASK |
		       GLOBAL_CFG_PAYLOAD_BYTE_SWAP_MASK |
		       GLOBAL_CFG_MULTICAST_MODIFY_FP_MASK |
		       GLOBAL_CFG_MULTICAST_EN_MASK |
		       GLOBAL_CFG_IRQ0_EN_MASK | GLOBAL_CFG_IRQ1_EN_MASK |
		       GLOBAL_CFG_TX_WB_DONE_MASK |
		       FIELD_PREP(GLOBAL_CFG_MAX_ISSUE_NUM_MASK, 2));

	airoha_qdma_init_qos(qdma);

	/* disable qdma rx delay interrupt */
	for (i = 0; i < ARRAY_SIZE(qdma->q_rx); i++) {
		if (!qdma->q_rx[i].ndesc)
			continue;

		airoha_qdma_clear(qdma, REG_RX_DELAY_INT_IDX(i),
				  RX_DELAY_INT_MASK);
	}

	airoha_qdma_set(qdma, REG_TXQ_CNGST_CFG,
			TXQ_CNGST_DROP_EN | TXQ_CNGST_DEI_DROP_EN);

	return 0;
}

static irqreturn_t airoha_irq_handler(int irq, void *dev_instance)
{
	struct airoha_qdma *qdma = dev_instance;
	u32 intr[ARRAY_SIZE(qdma->irqmask)];
	int i;

	for (i = 0; i < ARRAY_SIZE(qdma->irqmask); i++) {
		intr[i] = airoha_qdma_rr(qdma, REG_INT_STATUS(i));
		intr[i] &= qdma->irqmask[i];
		airoha_qdma_wr(qdma, REG_INT_STATUS(i), intr[i]);
	}

	if (!test_bit(DEV_STATE_INITIALIZED, &qdma->eth->state))
		return IRQ_NONE;

	if (intr[1] & RX_DONE_INT_MASK) {
		airoha_qdma_irq_disable(qdma, QDMA_INT_REG_IDX1,
					RX_DONE_INT_MASK);

		for (i = 0; i < ARRAY_SIZE(qdma->q_rx); i++) {
			if (!qdma->q_rx[i].ndesc)
				continue;

			if (intr[1] & BIT(i))
				napi_schedule(&qdma->q_rx[i].napi);
		}
	}

	if (intr[0] & INT_TX_MASK) {
		for (i = 0; i < ARRAY_SIZE(qdma->q_tx_irq); i++) {
			struct airoha_tx_irq_queue *irq_q = &qdma->q_tx_irq[i];
			u32 status, head;

			if (!(intr[0] & TX_DONE_INT_MASK(i)))
				continue;

			airoha_qdma_irq_disable(qdma, QDMA_INT_REG_IDX0,
						TX_DONE_INT_MASK(i));

			status = airoha_qdma_rr(qdma, REG_IRQ_STATUS(i));
			head = FIELD_GET(IRQ_HEAD_IDX_MASK, status);
			irq_q->head = head % irq_q->size;
			irq_q->queued = FIELD_GET(IRQ_ENTRY_LEN_MASK, status);

			napi_schedule(&qdma->q_tx_irq[i].napi);
		}
	}

	return IRQ_HANDLED;
}

static int airoha_qdma_init(struct platform_device *pdev,
			    struct airoha_eth *eth,
			    struct airoha_qdma *qdma)
{
	int err, id = qdma - &eth->qdma[0];
	const char *res;

	spin_lock_init(&qdma->irq_lock);
	qdma->eth = eth;

	res = devm_kasprintf(eth->dev, GFP_KERNEL, "qdma%d", id);
	if (!res)
		return -ENOMEM;

	qdma->regs = devm_platform_ioremap_resource_byname(pdev, res);
	if (IS_ERR(qdma->regs))
		return dev_err_probe(eth->dev, PTR_ERR(qdma->regs),
				     "failed to iomap qdma%d regs\n", id);

	qdma->irq = platform_get_irq(pdev, 4 * id);
	if (qdma->irq < 0)
		return qdma->irq;

	err = devm_request_irq(eth->dev, qdma->irq, airoha_irq_handler,
			       IRQF_SHARED, KBUILD_MODNAME, qdma);
	if (err)
		return err;

	err = airoha_qdma_init_rx(qdma);
	if (err)
		return err;

	err = airoha_qdma_init_tx(qdma);
	if (err)
		return err;

	err = airoha_qdma_init_hfwd_queues(qdma);
	if (err)
		return err;

	return airoha_qdma_hw_init(qdma);
}

static int airoha_hw_init(struct platform_device *pdev,
			  struct airoha_eth *eth)
{
	int err, i;

	/* disable xsi */
	err = reset_control_bulk_assert(ARRAY_SIZE(eth->xsi_rsts),
					eth->xsi_rsts);
	if (err)
		return err;

	err = reset_control_bulk_assert(ARRAY_SIZE(eth->rsts), eth->rsts);
	if (err)
		return err;

	msleep(20);
	err = reset_control_bulk_deassert(ARRAY_SIZE(eth->rsts), eth->rsts);
	if (err)
		return err;

	msleep(20);
	err = airoha_fe_init(eth);
	if (err)
		return err;

	for (i = 0; i < ARRAY_SIZE(eth->qdma); i++) {
		err = airoha_qdma_init(pdev, eth, &eth->qdma[i]);
		if (err)
			return err;
	}

	set_bit(DEV_STATE_INITIALIZED, &eth->state);

	return 0;
}

static void airoha_hw_cleanup(struct airoha_qdma *qdma)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(qdma->q_rx); i++) {
		if (!qdma->q_rx[i].ndesc)
			continue;

		napi_disable(&qdma->q_rx[i].napi);
		netif_napi_del(&qdma->q_rx[i].napi);
		airoha_qdma_cleanup_rx_queue(&qdma->q_rx[i]);
		if (qdma->q_rx[i].page_pool)
			page_pool_destroy(qdma->q_rx[i].page_pool);
	}

	for (i = 0; i < ARRAY_SIZE(qdma->q_tx_irq); i++) {
		napi_disable(&qdma->q_tx_irq[i].napi);
		netif_napi_del(&qdma->q_tx_irq[i].napi);
	}

	for (i = 0; i < ARRAY_SIZE(qdma->q_tx); i++) {
		if (!qdma->q_tx[i].ndesc)
			continue;

		airoha_qdma_cleanup_tx_queue(&qdma->q_tx[i]);
	}
}

static void airoha_qdma_start_napi(struct airoha_qdma *qdma)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(qdma->q_tx_irq); i++)
		napi_enable(&qdma->q_tx_irq[i].napi);

	for (i = 0; i < ARRAY_SIZE(qdma->q_rx); i++) {
		if (!qdma->q_rx[i].ndesc)
			continue;

		napi_enable(&qdma->q_rx[i].napi);
	}
}

static void airoha_update_hw_stats(struct airoha_gdm_port *port)
{
	struct airoha_eth *eth = port->qdma->eth;
	u32 val, i = 0;

	spin_lock(&port->stats.lock);
	u64_stats_update_begin(&port->stats.syncp);

	/* TX */
	val = airoha_fe_rr(eth, REG_FE_GDM_TX_OK_PKT_CNT_H(port->id));
	port->stats.tx_ok_pkts += ((u64)val << 32);
	val = airoha_fe_rr(eth, REG_FE_GDM_TX_OK_PKT_CNT_L(port->id));
	port->stats.tx_ok_pkts += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_TX_OK_BYTE_CNT_H(port->id));
	port->stats.tx_ok_bytes += ((u64)val << 32);
	val = airoha_fe_rr(eth, REG_FE_GDM_TX_OK_BYTE_CNT_L(port->id));
	port->stats.tx_ok_bytes += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_TX_ETH_DROP_CNT(port->id));
	port->stats.tx_drops += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_TX_ETH_BC_CNT(port->id));
	port->stats.tx_broadcast += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_TX_ETH_MC_CNT(port->id));
	port->stats.tx_multicast += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_TX_ETH_RUNT_CNT(port->id));
	port->stats.tx_len[i] += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_TX_ETH_E64_CNT_H(port->id));
	port->stats.tx_len[i] += ((u64)val << 32);
	val = airoha_fe_rr(eth, REG_FE_GDM_TX_ETH_E64_CNT_L(port->id));
	port->stats.tx_len[i++] += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_TX_ETH_L64_CNT_H(port->id));
	port->stats.tx_len[i] += ((u64)val << 32);
	val = airoha_fe_rr(eth, REG_FE_GDM_TX_ETH_L64_CNT_L(port->id));
	port->stats.tx_len[i++] += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_TX_ETH_L127_CNT_H(port->id));
	port->stats.tx_len[i] += ((u64)val << 32);
	val = airoha_fe_rr(eth, REG_FE_GDM_TX_ETH_L127_CNT_L(port->id));
	port->stats.tx_len[i++] += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_TX_ETH_L255_CNT_H(port->id));
	port->stats.tx_len[i] += ((u64)val << 32);
	val = airoha_fe_rr(eth, REG_FE_GDM_TX_ETH_L255_CNT_L(port->id));
	port->stats.tx_len[i++] += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_TX_ETH_L511_CNT_H(port->id));
	port->stats.tx_len[i] += ((u64)val << 32);
	val = airoha_fe_rr(eth, REG_FE_GDM_TX_ETH_L511_CNT_L(port->id));
	port->stats.tx_len[i++] += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_TX_ETH_L1023_CNT_H(port->id));
	port->stats.tx_len[i] += ((u64)val << 32);
	val = airoha_fe_rr(eth, REG_FE_GDM_TX_ETH_L1023_CNT_L(port->id));
	port->stats.tx_len[i++] += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_TX_ETH_LONG_CNT(port->id));
	port->stats.tx_len[i++] += val;

	/* RX */
	val = airoha_fe_rr(eth, REG_FE_GDM_RX_OK_PKT_CNT_H(port->id));
	port->stats.rx_ok_pkts += ((u64)val << 32);
	val = airoha_fe_rr(eth, REG_FE_GDM_RX_OK_PKT_CNT_L(port->id));
	port->stats.rx_ok_pkts += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_RX_OK_BYTE_CNT_H(port->id));
	port->stats.rx_ok_bytes += ((u64)val << 32);
	val = airoha_fe_rr(eth, REG_FE_GDM_RX_OK_BYTE_CNT_L(port->id));
	port->stats.rx_ok_bytes += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_RX_ETH_DROP_CNT(port->id));
	port->stats.rx_drops += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_RX_ETH_BC_CNT(port->id));
	port->stats.rx_broadcast += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_RX_ETH_MC_CNT(port->id));
	port->stats.rx_multicast += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_RX_ERROR_DROP_CNT(port->id));
	port->stats.rx_errors += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_RX_ETH_CRC_ERR_CNT(port->id));
	port->stats.rx_crc_error += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_RX_OVERFLOW_DROP_CNT(port->id));
	port->stats.rx_over_errors += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_RX_ETH_FRAG_CNT(port->id));
	port->stats.rx_fragment += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_RX_ETH_JABBER_CNT(port->id));
	port->stats.rx_jabber += val;

	i = 0;
	val = airoha_fe_rr(eth, REG_FE_GDM_RX_ETH_RUNT_CNT(port->id));
	port->stats.rx_len[i] += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_RX_ETH_E64_CNT_H(port->id));
	port->stats.rx_len[i] += ((u64)val << 32);
	val = airoha_fe_rr(eth, REG_FE_GDM_RX_ETH_E64_CNT_L(port->id));
	port->stats.rx_len[i++] += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_RX_ETH_L64_CNT_H(port->id));
	port->stats.rx_len[i] += ((u64)val << 32);
	val = airoha_fe_rr(eth, REG_FE_GDM_RX_ETH_L64_CNT_L(port->id));
	port->stats.rx_len[i++] += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_RX_ETH_L127_CNT_H(port->id));
	port->stats.rx_len[i] += ((u64)val << 32);
	val = airoha_fe_rr(eth, REG_FE_GDM_RX_ETH_L127_CNT_L(port->id));
	port->stats.rx_len[i++] += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_RX_ETH_L255_CNT_H(port->id));
	port->stats.rx_len[i] += ((u64)val << 32);
	val = airoha_fe_rr(eth, REG_FE_GDM_RX_ETH_L255_CNT_L(port->id));
	port->stats.rx_len[i++] += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_RX_ETH_L511_CNT_H(port->id));
	port->stats.rx_len[i] += ((u64)val << 32);
	val = airoha_fe_rr(eth, REG_FE_GDM_RX_ETH_L511_CNT_L(port->id));
	port->stats.rx_len[i++] += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_RX_ETH_L1023_CNT_H(port->id));
	port->stats.rx_len[i] += ((u64)val << 32);
	val = airoha_fe_rr(eth, REG_FE_GDM_RX_ETH_L1023_CNT_L(port->id));
	port->stats.rx_len[i++] += val;

	val = airoha_fe_rr(eth, REG_FE_GDM_RX_ETH_LONG_CNT(port->id));
	port->stats.rx_len[i++] += val;

	/* reset mib counters */
	airoha_fe_set(eth, REG_FE_GDM_MIB_CLEAR(port->id),
		      FE_GDM_MIB_RX_CLEAR_MASK | FE_GDM_MIB_TX_CLEAR_MASK);

	u64_stats_update_end(&port->stats.syncp);
	spin_unlock(&port->stats.lock);
}

static int airoha_dev_open(struct net_device *dev)
{
	struct airoha_gdm_port *port = netdev_priv(dev);
	struct airoha_qdma *qdma = port->qdma;
	int err;

	netif_tx_start_all_queues(dev);
	err = airoha_set_gdm_ports(qdma->eth, true);
	if (err)
		return err;

	if (netdev_uses_dsa(dev))
		airoha_fe_set(qdma->eth, REG_GDM_INGRESS_CFG(port->id),
			      GDM_STAG_EN_MASK);
	else
		airoha_fe_clear(qdma->eth, REG_GDM_INGRESS_CFG(port->id),
				GDM_STAG_EN_MASK);

	airoha_qdma_set(qdma, REG_QDMA_GLOBAL_CFG,
			GLOBAL_CFG_TX_DMA_EN_MASK |
			GLOBAL_CFG_RX_DMA_EN_MASK);

	return 0;
}

static int airoha_dev_stop(struct net_device *dev)
{
	struct airoha_gdm_port *port = netdev_priv(dev);
	struct airoha_qdma *qdma = port->qdma;
	int err;

	netif_tx_disable(dev);
	err = airoha_set_gdm_ports(qdma->eth, false);
	if (err)
		return err;

	airoha_qdma_clear(qdma, REG_QDMA_GLOBAL_CFG,
			  GLOBAL_CFG_TX_DMA_EN_MASK |
			  GLOBAL_CFG_RX_DMA_EN_MASK);

	return 0;
}

static int airoha_dev_set_macaddr(struct net_device *dev, void *p)
{
	struct airoha_gdm_port *port = netdev_priv(dev);
	int err;

	err = eth_mac_addr(dev, p);
	if (err)
		return err;

	airoha_set_macaddr(port->qdma->eth, dev->dev_addr);

	return 0;
}

static int airoha_dev_init(struct net_device *dev)
{
	struct airoha_gdm_port *port = netdev_priv(dev);

	airoha_set_macaddr(port->qdma->eth, dev->dev_addr);

	return 0;
}

static void airoha_dev_get_stats64(struct net_device *dev,
				   struct rtnl_link_stats64 *storage)
{
	struct airoha_gdm_port *port = netdev_priv(dev);
	unsigned int start;

	airoha_update_hw_stats(port);
	do {
		start = u64_stats_fetch_begin(&port->stats.syncp);
		storage->rx_packets = port->stats.rx_ok_pkts;
		storage->tx_packets = port->stats.tx_ok_pkts;
		storage->rx_bytes = port->stats.rx_ok_bytes;
		storage->tx_bytes = port->stats.tx_ok_bytes;
		storage->multicast = port->stats.rx_multicast;
		storage->rx_errors = port->stats.rx_errors;
		storage->rx_dropped = port->stats.rx_drops;
		storage->tx_dropped = port->stats.tx_drops;
		storage->rx_crc_errors = port->stats.rx_crc_error;
		storage->rx_over_errors = port->stats.rx_over_errors;
	} while (u64_stats_fetch_retry(&port->stats.syncp, start));
}

static netdev_tx_t airoha_dev_xmit(struct sk_buff *skb,
				   struct net_device *dev)
{
	struct skb_shared_info *sinfo = skb_shinfo(skb);
	struct airoha_gdm_port *port = netdev_priv(dev);
	u32 msg0 = 0, msg1, len = skb_headlen(skb);
	int i, qid = skb_get_queue_mapping(skb);
	struct airoha_qdma *qdma = port->qdma;
	u32 nr_frags = 1 + sinfo->nr_frags;
	struct netdev_queue *txq;
	struct airoha_queue *q;
	void *data = skb->data;
	u16 index;
	u8 fport;

	if (skb->ip_summed == CHECKSUM_PARTIAL)
		msg0 |= FIELD_PREP(QDMA_ETH_TXMSG_TCO_MASK, 1) |
			FIELD_PREP(QDMA_ETH_TXMSG_UCO_MASK, 1) |
			FIELD_PREP(QDMA_ETH_TXMSG_ICO_MASK, 1);

	/* TSO: fill MSS info in tcp checksum field */
	if (skb_is_gso(skb)) {
		if (skb_cow_head(skb, 0))
			goto error;

		if (sinfo->gso_type & (SKB_GSO_TCPV4 | SKB_GSO_TCPV6)) {
			__be16 csum = cpu_to_be16(sinfo->gso_size);

			tcp_hdr(skb)->check = (__force __sum16)csum;
			msg0 |= FIELD_PREP(QDMA_ETH_TXMSG_TSO_MASK, 1);
		}
	}

	fport = port->id == 4 ? FE_PSE_PORT_GDM4 : port->id;
	msg1 = FIELD_PREP(QDMA_ETH_TXMSG_FPORT_MASK, fport) |
	       FIELD_PREP(QDMA_ETH_TXMSG_METER_MASK, 0x7f);

	q = &qdma->q_tx[qid];
	if (WARN_ON_ONCE(!q->ndesc))
		goto error;

	spin_lock_bh(&q->lock);

	txq = netdev_get_tx_queue(dev, qid);
	if (q->queued + nr_frags > q->ndesc) {
		/* not enough space in the queue */
		netif_tx_stop_queue(txq);
		spin_unlock_bh(&q->lock);
		return NETDEV_TX_BUSY;
	}

	index = q->head;
	for (i = 0; i < nr_frags; i++) {
		struct airoha_qdma_desc *desc = &q->desc[index];
		struct airoha_queue_entry *e = &q->entry[index];
		skb_frag_t *frag = &sinfo->frags[i];
		dma_addr_t addr;
		u32 val;

		addr = dma_map_single(dev->dev.parent, data, len,
				      DMA_TO_DEVICE);
		if (unlikely(dma_mapping_error(dev->dev.parent, addr)))
			goto error_unmap;

		index = (index + 1) % q->ndesc;

		val = FIELD_PREP(QDMA_DESC_LEN_MASK, len);
		if (i < nr_frags - 1)
			val |= FIELD_PREP(QDMA_DESC_MORE_MASK, 1);
		WRITE_ONCE(desc->ctrl, cpu_to_le32(val));
		WRITE_ONCE(desc->addr, cpu_to_le32(addr));
		val = FIELD_PREP(QDMA_DESC_NEXT_ID_MASK, index);
		WRITE_ONCE(desc->data, cpu_to_le32(val));
		WRITE_ONCE(desc->msg0, cpu_to_le32(msg0));
		WRITE_ONCE(desc->msg1, cpu_to_le32(msg1));
		WRITE_ONCE(desc->msg2, cpu_to_le32(0xffff));

		e->skb = i ? NULL : skb;
		e->dma_addr = addr;
		e->dma_len = len;

		airoha_qdma_rmw(qdma, REG_TX_CPU_IDX(qid),
				TX_RING_CPU_IDX_MASK,
				FIELD_PREP(TX_RING_CPU_IDX_MASK, index));

		data = skb_frag_address(frag);
		len = skb_frag_size(frag);
	}

	q->head = index;
	q->queued += i;

	skb_tx_timestamp(skb);
	if (q->ndesc - q->queued < q->free_thr)
		netif_tx_stop_queue(txq);

	spin_unlock_bh(&q->lock);

	return NETDEV_TX_OK;

error_unmap:
	for (i--; i >= 0; i--) {
		index = (q->head + i) % q->ndesc;
		dma_unmap_single(dev->dev.parent, q->entry[index].dma_addr,
				 q->entry[index].dma_len, DMA_TO_DEVICE);
	}

	spin_unlock_bh(&q->lock);
error:
	dev_kfree_skb_any(skb);
	dev->stats.tx_dropped++;

	return NETDEV_TX_OK;
}

static void airoha_ethtool_get_drvinfo(struct net_device *dev,
				       struct ethtool_drvinfo *info)
{
	struct airoha_gdm_port *port = netdev_priv(dev);
	struct airoha_eth *eth = port->qdma->eth;

	strscpy(info->driver, eth->dev->driver->name, sizeof(info->driver));
	strscpy(info->bus_info, dev_name(eth->dev), sizeof(info->bus_info));
}

static void airoha_ethtool_get_mac_stats(struct net_device *dev,
					 struct ethtool_eth_mac_stats *stats)
{
	struct airoha_gdm_port *port = netdev_priv(dev);
	unsigned int start;

	airoha_update_hw_stats(port);
	do {
		start = u64_stats_fetch_begin(&port->stats.syncp);
		stats->MulticastFramesXmittedOK = port->stats.tx_multicast;
		stats->BroadcastFramesXmittedOK = port->stats.tx_broadcast;
		stats->BroadcastFramesReceivedOK = port->stats.rx_broadcast;
	} while (u64_stats_fetch_retry(&port->stats.syncp, start));
}

static const struct ethtool_rmon_hist_range airoha_ethtool_rmon_ranges[] = {
	{    0,    64 },
	{   65,   127 },
	{  128,   255 },
	{  256,   511 },
	{  512,  1023 },
	{ 1024,  1518 },
	{ 1519, 10239 },
	{},
};

static void
airoha_ethtool_get_rmon_stats(struct net_device *dev,
			      struct ethtool_rmon_stats *stats,
			      const struct ethtool_rmon_hist_range **ranges)
{
	struct airoha_gdm_port *port = netdev_priv(dev);
	struct airoha_hw_stats *hw_stats = &port->stats;
	unsigned int start;

	BUILD_BUG_ON(ARRAY_SIZE(airoha_ethtool_rmon_ranges) !=
		     ARRAY_SIZE(hw_stats->tx_len) + 1);
	BUILD_BUG_ON(ARRAY_SIZE(airoha_ethtool_rmon_ranges) !=
		     ARRAY_SIZE(hw_stats->rx_len) + 1);

	*ranges = airoha_ethtool_rmon_ranges;
	airoha_update_hw_stats(port);
	do {
		int i;

		start = u64_stats_fetch_begin(&port->stats.syncp);
		stats->fragments = hw_stats->rx_fragment;
		stats->jabbers = hw_stats->rx_jabber;
		for (i = 0; i < ARRAY_SIZE(airoha_ethtool_rmon_ranges) - 1;
		     i++) {
			stats->hist[i] = hw_stats->rx_len[i];
			stats->hist_tx[i] = hw_stats->tx_len[i];
		}
	} while (u64_stats_fetch_retry(&port->stats.syncp, start));
}

static const struct net_device_ops airoha_netdev_ops = {
	.ndo_init		= airoha_dev_init,
	.ndo_open		= airoha_dev_open,
	.ndo_stop		= airoha_dev_stop,
	.ndo_start_xmit		= airoha_dev_xmit,
	.ndo_get_stats64        = airoha_dev_get_stats64,
	.ndo_set_mac_address	= airoha_dev_set_macaddr,
};

static const struct ethtool_ops airoha_ethtool_ops = {
	.get_drvinfo		= airoha_ethtool_get_drvinfo,
	.get_eth_mac_stats      = airoha_ethtool_get_mac_stats,
	.get_rmon_stats		= airoha_ethtool_get_rmon_stats,
};

static int airoha_alloc_gdm_port(struct airoha_eth *eth, struct device_node *np)
{
	const __be32 *id_ptr = of_get_property(np, "reg", NULL);
	struct airoha_gdm_port *port;
	struct airoha_qdma *qdma;
	struct net_device *dev;
	int err, index;
	u32 id;

	if (!id_ptr) {
		dev_err(eth->dev, "missing gdm port id\n");
		return -EINVAL;
	}

	id = be32_to_cpup(id_ptr);
	index = id - 1;

	if (!id || id > ARRAY_SIZE(eth->ports)) {
		dev_err(eth->dev, "invalid gdm port id: %d\n", id);
		return -EINVAL;
	}

	if (eth->ports[index]) {
		dev_err(eth->dev, "duplicate gdm port id: %d\n", id);
		return -EINVAL;
	}

	dev = devm_alloc_etherdev_mqs(eth->dev, sizeof(*port),
				      AIROHA_NUM_TX_RING, AIROHA_NUM_RX_RING);
	if (!dev) {
		dev_err(eth->dev, "alloc_etherdev failed\n");
		return -ENOMEM;
	}

	qdma = &eth->qdma[index % AIROHA_MAX_NUM_QDMA];
	dev->netdev_ops = &airoha_netdev_ops;
	dev->ethtool_ops = &airoha_ethtool_ops;
	dev->max_mtu = AIROHA_MAX_MTU;
	dev->watchdog_timeo = 5 * HZ;
	dev->hw_features = NETIF_F_IP_CSUM | NETIF_F_RXCSUM |
			   NETIF_F_TSO6 | NETIF_F_IPV6_CSUM |
			   NETIF_F_SG | NETIF_F_TSO;
	dev->features |= dev->hw_features;
	dev->dev.of_node = np;
	dev->irq = qdma->irq;
	SET_NETDEV_DEV(dev, eth->dev);

	err = of_get_ethdev_address(np, dev);
	if (err) {
		if (err == -EPROBE_DEFER)
			return err;

		eth_hw_addr_random(dev);
		dev_info(eth->dev, "generated random MAC address %pM\n",
			 dev->dev_addr);
	}

	port = netdev_priv(dev);
	u64_stats_init(&port->stats.syncp);
	spin_lock_init(&port->stats.lock);
	port->qdma = qdma;
	port->dev = dev;
	port->id = id;
	eth->ports[index] = port;

	return register_netdev(dev);
}

static int airoha_probe(struct platform_device *pdev)
{
	struct device_node *np;
	struct airoha_eth *eth;
	int i, err;

	eth = devm_kzalloc(&pdev->dev, sizeof(*eth), GFP_KERNEL);
	if (!eth)
		return -ENOMEM;

	eth->dev = &pdev->dev;

	err = dma_set_mask_and_coherent(eth->dev, DMA_BIT_MASK(32));
	if (err) {
		dev_err(eth->dev, "failed configuring DMA mask\n");
		return err;
	}

	eth->fe_regs = devm_platform_ioremap_resource_byname(pdev, "fe");
	if (IS_ERR(eth->fe_regs))
		return dev_err_probe(eth->dev, PTR_ERR(eth->fe_regs),
				     "failed to iomap fe regs\n");

	eth->rsts[0].id = "fe";
	eth->rsts[1].id = "pdma";
	eth->rsts[2].id = "qdma";
	err = devm_reset_control_bulk_get_exclusive(eth->dev,
						    ARRAY_SIZE(eth->rsts),
						    eth->rsts);
	if (err) {
		dev_err(eth->dev, "failed to get bulk reset lines\n");
		return err;
	}

	eth->xsi_rsts[0].id = "xsi-mac";
	eth->xsi_rsts[1].id = "hsi0-mac";
	eth->xsi_rsts[2].id = "hsi1-mac";
	eth->xsi_rsts[3].id = "hsi-mac";
	eth->xsi_rsts[4].id = "xfp-mac";
	err = devm_reset_control_bulk_get_exclusive(eth->dev,
						    ARRAY_SIZE(eth->xsi_rsts),
						    eth->xsi_rsts);
	if (err) {
		dev_err(eth->dev, "failed to get bulk xsi reset lines\n");
		return err;
	}

	eth->napi_dev = alloc_netdev_dummy(0);
	if (!eth->napi_dev)
		return -ENOMEM;

	/* Enable threaded NAPI by default */
	eth->napi_dev->threaded = true;
	strscpy(eth->napi_dev->name, "qdma_eth", sizeof(eth->napi_dev->name));
	platform_set_drvdata(pdev, eth);

	err = airoha_hw_init(pdev, eth);
	if (err)
		goto error;

	for (i = 0; i < ARRAY_SIZE(eth->qdma); i++)
		airoha_qdma_start_napi(&eth->qdma[i]);

	for_each_child_of_node(pdev->dev.of_node, np) {
		if (!of_device_is_compatible(np, "airoha,eth-mac"))
			continue;

		if (!of_device_is_available(np))
			continue;

		err = airoha_alloc_gdm_port(eth, np);
		if (err) {
			of_node_put(np);
			goto error;
		}
	}

	return 0;

error:
	for (i = 0; i < ARRAY_SIZE(eth->qdma); i++)
		airoha_hw_cleanup(&eth->qdma[i]);

	for (i = 0; i < ARRAY_SIZE(eth->ports); i++) {
		struct airoha_gdm_port *port = eth->ports[i];

		if (port && port->dev->reg_state == NETREG_REGISTERED)
			unregister_netdev(port->dev);
	}
	free_netdev(eth->napi_dev);
	platform_set_drvdata(pdev, NULL);

	return err;
}

static void airoha_remove(struct platform_device *pdev)
{
	struct airoha_eth *eth = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < ARRAY_SIZE(eth->qdma); i++)
		airoha_hw_cleanup(&eth->qdma[i]);

	for (i = 0; i < ARRAY_SIZE(eth->ports); i++) {
		struct airoha_gdm_port *port = eth->ports[i];

		if (!port)
			continue;

		airoha_dev_stop(port->dev);
		unregister_netdev(port->dev);
	}
	free_netdev(eth->napi_dev);

	platform_set_drvdata(pdev, NULL);
}

static const struct of_device_id of_airoha_match[] = {
	{ .compatible = "airoha,en7581-eth" },
	{ /* sentinel */ }
};

static struct platform_driver airoha_driver = {
	.probe = airoha_probe,
	.remove_new = airoha_remove,
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = of_airoha_match,
	},
};
module_platform_driver(airoha_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Lorenzo Bianconi <lorenzo@kernel.org>");
MODULE_DESCRIPTION("Ethernet driver for Airoha SoC");
