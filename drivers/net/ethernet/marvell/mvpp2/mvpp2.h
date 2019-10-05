/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Definitions for Marvell PPv2 network controller for Armada 375 SoC.
 *
 * Copyright (C) 2014 Marvell
 *
 * Marcin Wojtas <mw@semihalf.com>
 */
#ifndef _MVPP2_H_
#define _MVPP2_H_

#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/phy.h>
#include <linux/phylink.h>
#include <net/flow_offload.h>

/* Fifo Registers */
#define MVPP2_RX_DATA_FIFO_SIZE_REG(port)	(0x00 + 4 * (port))
#define MVPP2_RX_ATTR_FIFO_SIZE_REG(port)	(0x20 + 4 * (port))
#define MVPP2_RX_MIN_PKT_SIZE_REG		0x60
#define MVPP2_RX_FIFO_INIT_REG			0x64
#define MVPP22_TX_FIFO_THRESH_REG(port)		(0x8840 + 4 * (port))
#define MVPP22_TX_FIFO_SIZE_REG(port)		(0x8860 + 4 * (port))

/* RX DMA Top Registers */
#define MVPP2_RX_CTRL_REG(port)			(0x140 + 4 * (port))
#define     MVPP2_RX_LOW_LATENCY_PKT_SIZE(s)	(((s) & 0xfff) << 16)
#define     MVPP2_RX_USE_PSEUDO_FOR_CSUM_MASK	BIT(31)
#define MVPP2_POOL_BUF_SIZE_REG(pool)		(0x180 + 4 * (pool))
#define     MVPP2_POOL_BUF_SIZE_OFFSET		5
#define MVPP2_RXQ_CONFIG_REG(rxq)		(0x800 + 4 * (rxq))
#define     MVPP2_SNOOP_PKT_SIZE_MASK		0x1ff
#define     MVPP2_SNOOP_BUF_HDR_MASK		BIT(9)
#define     MVPP2_RXQ_POOL_SHORT_OFFS		20
#define     MVPP21_RXQ_POOL_SHORT_MASK		0x700000
#define     MVPP22_RXQ_POOL_SHORT_MASK		0xf00000
#define     MVPP2_RXQ_POOL_LONG_OFFS		24
#define     MVPP21_RXQ_POOL_LONG_MASK		0x7000000
#define     MVPP22_RXQ_POOL_LONG_MASK		0xf000000
#define     MVPP2_RXQ_PACKET_OFFSET_OFFS	28
#define     MVPP2_RXQ_PACKET_OFFSET_MASK	0x70000000
#define     MVPP2_RXQ_DISABLE_MASK		BIT(31)

/* Top Registers */
#define MVPP2_MH_REG(port)			(0x5040 + 4 * (port))
#define MVPP2_DSA_EXTENDED			BIT(5)

/* Parser Registers */
#define MVPP2_PRS_INIT_LOOKUP_REG		0x1000
#define     MVPP2_PRS_PORT_LU_MAX		0xf
#define     MVPP2_PRS_PORT_LU_MASK(port)	(0xff << ((port) * 4))
#define     MVPP2_PRS_PORT_LU_VAL(port, val)	((val) << ((port) * 4))
#define MVPP2_PRS_INIT_OFFS_REG(port)		(0x1004 + ((port) & 4))
#define     MVPP2_PRS_INIT_OFF_MASK(port)	(0x3f << (((port) % 4) * 8))
#define     MVPP2_PRS_INIT_OFF_VAL(port, val)	((val) << (((port) % 4) * 8))
#define MVPP2_PRS_MAX_LOOP_REG(port)		(0x100c + ((port) & 4))
#define     MVPP2_PRS_MAX_LOOP_MASK(port)	(0xff << (((port) % 4) * 8))
#define     MVPP2_PRS_MAX_LOOP_VAL(port, val)	((val) << (((port) % 4) * 8))
#define MVPP2_PRS_TCAM_IDX_REG			0x1100
#define MVPP2_PRS_TCAM_DATA_REG(idx)		(0x1104 + (idx) * 4)
#define     MVPP2_PRS_TCAM_INV_MASK		BIT(31)
#define MVPP2_PRS_SRAM_IDX_REG			0x1200
#define MVPP2_PRS_SRAM_DATA_REG(idx)		(0x1204 + (idx) * 4)
#define MVPP2_PRS_TCAM_CTRL_REG			0x1230
#define     MVPP2_PRS_TCAM_EN_MASK		BIT(0)
#define MVPP2_PRS_TCAM_HIT_IDX_REG		0x1240
#define MVPP2_PRS_TCAM_HIT_CNT_REG		0x1244
#define     MVPP2_PRS_TCAM_HIT_CNT_MASK		GENMASK(15, 0)

/* RSS Registers */
#define MVPP22_RSS_INDEX			0x1500
#define     MVPP22_RSS_INDEX_TABLE_ENTRY(idx)	(idx)
#define     MVPP22_RSS_INDEX_TABLE(idx)		((idx) << 8)
#define     MVPP22_RSS_INDEX_QUEUE(idx)		((idx) << 16)
#define MVPP22_RXQ2RSS_TABLE			0x1504
#define     MVPP22_RSS_TABLE_POINTER(p)		(p)
#define MVPP22_RSS_TABLE_ENTRY			0x1508
#define MVPP22_RSS_WIDTH			0x150c

/* Classifier Registers */
#define MVPP2_CLS_MODE_REG			0x1800
#define     MVPP2_CLS_MODE_ACTIVE_MASK		BIT(0)
#define MVPP2_CLS_PORT_WAY_REG			0x1810
#define     MVPP2_CLS_PORT_WAY_MASK(port)	(1 << (port))
#define MVPP2_CLS_LKP_INDEX_REG			0x1814
#define     MVPP2_CLS_LKP_INDEX_WAY_OFFS	6
#define MVPP2_CLS_LKP_TBL_REG			0x1818
#define     MVPP2_CLS_LKP_TBL_RXQ_MASK		0xff
#define     MVPP2_CLS_LKP_FLOW_PTR(flow)	((flow) << 16)
#define     MVPP2_CLS_LKP_TBL_LOOKUP_EN_MASK	BIT(25)
#define MVPP2_CLS_FLOW_INDEX_REG		0x1820
#define MVPP2_CLS_FLOW_TBL0_REG			0x1824
#define     MVPP2_CLS_FLOW_TBL0_LAST		BIT(0)
#define     MVPP2_CLS_FLOW_TBL0_ENG_MASK	0x7
#define     MVPP2_CLS_FLOW_TBL0_OFFS		1
#define     MVPP2_CLS_FLOW_TBL0_ENG(x)		((x) << 1)
#define     MVPP2_CLS_FLOW_TBL0_PORT_ID_MASK	0xff
#define     MVPP2_CLS_FLOW_TBL0_PORT_ID(port)	((port) << 4)
#define     MVPP2_CLS_FLOW_TBL0_PORT_ID_SEL	BIT(23)
#define MVPP2_CLS_FLOW_TBL1_REG			0x1828
#define     MVPP2_CLS_FLOW_TBL1_N_FIELDS_MASK	0x7
#define     MVPP2_CLS_FLOW_TBL1_N_FIELDS(x)	(x)
#define     MVPP2_CLS_FLOW_TBL1_LU_TYPE(lu)	(((lu) & 0x3f) << 3)
#define     MVPP2_CLS_FLOW_TBL1_PRIO_MASK	0x3f
#define     MVPP2_CLS_FLOW_TBL1_PRIO(x)		((x) << 9)
#define     MVPP2_CLS_FLOW_TBL1_SEQ_MASK	0x7
#define     MVPP2_CLS_FLOW_TBL1_SEQ(x)		((x) << 15)
#define MVPP2_CLS_FLOW_TBL2_REG			0x182c
#define     MVPP2_CLS_FLOW_TBL2_FLD_MASK	0x3f
#define     MVPP2_CLS_FLOW_TBL2_FLD_OFFS(n)	((n) * 6)
#define     MVPP2_CLS_FLOW_TBL2_FLD(n, x)	((x) << ((n) * 6))
#define MVPP2_CLS_OVERSIZE_RXQ_LOW_REG(port)	(0x1980 + ((port) * 4))
#define     MVPP2_CLS_OVERSIZE_RXQ_LOW_BITS	3
#define     MVPP2_CLS_OVERSIZE_RXQ_LOW_MASK	0x7
#define MVPP2_CLS_SWFWD_P2HQ_REG(port)		(0x19b0 + ((port) * 4))
#define MVPP2_CLS_SWFWD_PCTRL_REG		0x19d0
#define     MVPP2_CLS_SWFWD_PCTRL_MASK(port)	(1 << (port))

/* Classifier C2 engine Registers */
#define MVPP22_CLS_C2_TCAM_IDX			0x1b00
#define MVPP22_CLS_C2_TCAM_DATA0		0x1b10
#define MVPP22_CLS_C2_TCAM_DATA1		0x1b14
#define MVPP22_CLS_C2_TCAM_DATA2		0x1b18
#define MVPP22_CLS_C2_TCAM_DATA3		0x1b1c
#define MVPP22_CLS_C2_TCAM_DATA4		0x1b20
#define     MVPP22_CLS_C2_LU_TYPE(lu)		((lu) & 0x3f)
#define     MVPP22_CLS_C2_PORT_ID(port)		((port) << 8)
#define     MVPP22_CLS_C2_PORT_MASK		(0xff << 8)
#define MVPP22_CLS_C2_TCAM_INV			0x1b24
#define     MVPP22_CLS_C2_TCAM_INV_BIT		BIT(31)
#define MVPP22_CLS_C2_HIT_CTR			0x1b50
#define MVPP22_CLS_C2_ACT			0x1b60
#define     MVPP22_CLS_C2_ACT_RSS_EN(act)	(((act) & 0x3) << 19)
#define     MVPP22_CLS_C2_ACT_FWD(act)		(((act) & 0x7) << 13)
#define     MVPP22_CLS_C2_ACT_QHIGH(act)	(((act) & 0x3) << 11)
#define     MVPP22_CLS_C2_ACT_QLOW(act)		(((act) & 0x3) << 9)
#define     MVPP22_CLS_C2_ACT_COLOR(act)	((act) & 0x7)
#define MVPP22_CLS_C2_ATTR0			0x1b64
#define     MVPP22_CLS_C2_ATTR0_QHIGH(qh)	(((qh) & 0x1f) << 24)
#define     MVPP22_CLS_C2_ATTR0_QHIGH_MASK	0x1f
#define     MVPP22_CLS_C2_ATTR0_QHIGH_OFFS	24
#define     MVPP22_CLS_C2_ATTR0_QLOW(ql)	(((ql) & 0x7) << 21)
#define     MVPP22_CLS_C2_ATTR0_QLOW_MASK	0x7
#define     MVPP22_CLS_C2_ATTR0_QLOW_OFFS	21
#define MVPP22_CLS_C2_ATTR1			0x1b68
#define MVPP22_CLS_C2_ATTR2			0x1b6c
#define     MVPP22_CLS_C2_ATTR2_RSS_EN		BIT(30)
#define MVPP22_CLS_C2_ATTR3			0x1b70
#define MVPP22_CLS_C2_TCAM_CTRL			0x1b90
#define     MVPP22_CLS_C2_TCAM_BYPASS_FIFO	BIT(0)

/* Descriptor Manager Top Registers */
#define MVPP2_RXQ_NUM_REG			0x2040
#define MVPP2_RXQ_DESC_ADDR_REG			0x2044
#define     MVPP22_DESC_ADDR_OFFS		8
#define MVPP2_RXQ_DESC_SIZE_REG			0x2048
#define     MVPP2_RXQ_DESC_SIZE_MASK		0x3ff0
#define MVPP2_RXQ_STATUS_UPDATE_REG(rxq)	(0x3000 + 4 * (rxq))
#define     MVPP2_RXQ_NUM_PROCESSED_OFFSET	0
#define     MVPP2_RXQ_NUM_NEW_OFFSET		16
#define MVPP2_RXQ_STATUS_REG(rxq)		(0x3400 + 4 * (rxq))
#define     MVPP2_RXQ_OCCUPIED_MASK		0x3fff
#define     MVPP2_RXQ_NON_OCCUPIED_OFFSET	16
#define     MVPP2_RXQ_NON_OCCUPIED_MASK		0x3fff0000
#define MVPP2_RXQ_THRESH_REG			0x204c
#define     MVPP2_OCCUPIED_THRESH_OFFSET	0
#define     MVPP2_OCCUPIED_THRESH_MASK		0x3fff
#define MVPP2_RXQ_INDEX_REG			0x2050
#define MVPP2_TXQ_NUM_REG			0x2080
#define MVPP2_TXQ_DESC_ADDR_REG			0x2084
#define MVPP2_TXQ_DESC_SIZE_REG			0x2088
#define     MVPP2_TXQ_DESC_SIZE_MASK		0x3ff0
#define MVPP2_TXQ_THRESH_REG			0x2094
#define	    MVPP2_TXQ_THRESH_OFFSET		16
#define	    MVPP2_TXQ_THRESH_MASK		0x3fff
#define MVPP2_AGGR_TXQ_UPDATE_REG		0x2090
#define MVPP2_TXQ_INDEX_REG			0x2098
#define MVPP2_TXQ_PREF_BUF_REG			0x209c
#define     MVPP2_PREF_BUF_PTR(desc)		((desc) & 0xfff)
#define     MVPP2_PREF_BUF_SIZE_4		(BIT(12) | BIT(13))
#define     MVPP2_PREF_BUF_SIZE_16		(BIT(12) | BIT(14))
#define     MVPP2_PREF_BUF_THRESH(val)		((val) << 17)
#define     MVPP2_TXQ_DRAIN_EN_MASK		BIT(31)
#define MVPP2_TXQ_PENDING_REG			0x20a0
#define     MVPP2_TXQ_PENDING_MASK		0x3fff
#define MVPP2_TXQ_INT_STATUS_REG		0x20a4
#define MVPP2_TXQ_SENT_REG(txq)			(0x3c00 + 4 * (txq))
#define     MVPP2_TRANSMITTED_COUNT_OFFSET	16
#define     MVPP2_TRANSMITTED_COUNT_MASK	0x3fff0000
#define MVPP2_TXQ_RSVD_REQ_REG			0x20b0
#define     MVPP2_TXQ_RSVD_REQ_Q_OFFSET		16
#define MVPP2_TXQ_RSVD_RSLT_REG			0x20b4
#define     MVPP2_TXQ_RSVD_RSLT_MASK		0x3fff
#define MVPP2_TXQ_RSVD_CLR_REG			0x20b8
#define     MVPP2_TXQ_RSVD_CLR_OFFSET		16
#define MVPP2_AGGR_TXQ_DESC_ADDR_REG(cpu)	(0x2100 + 4 * (cpu))
#define     MVPP22_AGGR_TXQ_DESC_ADDR_OFFS	8
#define MVPP2_AGGR_TXQ_DESC_SIZE_REG(cpu)	(0x2140 + 4 * (cpu))
#define     MVPP2_AGGR_TXQ_DESC_SIZE_MASK	0x3ff0
#define MVPP2_AGGR_TXQ_STATUS_REG(cpu)		(0x2180 + 4 * (cpu))
#define     MVPP2_AGGR_TXQ_PENDING_MASK		0x3fff
#define MVPP2_AGGR_TXQ_INDEX_REG(cpu)		(0x21c0 + 4 * (cpu))

/* MBUS bridge registers */
#define MVPP2_WIN_BASE(w)			(0x4000 + ((w) << 2))
#define MVPP2_WIN_SIZE(w)			(0x4020 + ((w) << 2))
#define MVPP2_WIN_REMAP(w)			(0x4040 + ((w) << 2))
#define MVPP2_BASE_ADDR_ENABLE			0x4060

/* AXI Bridge Registers */
#define MVPP22_AXI_BM_WR_ATTR_REG		0x4100
#define MVPP22_AXI_BM_RD_ATTR_REG		0x4104
#define MVPP22_AXI_AGGRQ_DESCR_RD_ATTR_REG	0x4110
#define MVPP22_AXI_TXQ_DESCR_WR_ATTR_REG	0x4114
#define MVPP22_AXI_TXQ_DESCR_RD_ATTR_REG	0x4118
#define MVPP22_AXI_RXQ_DESCR_WR_ATTR_REG	0x411c
#define MVPP22_AXI_RX_DATA_WR_ATTR_REG		0x4120
#define MVPP22_AXI_TX_DATA_RD_ATTR_REG		0x4130
#define MVPP22_AXI_RD_NORMAL_CODE_REG		0x4150
#define MVPP22_AXI_RD_SNOOP_CODE_REG		0x4154
#define MVPP22_AXI_WR_NORMAL_CODE_REG		0x4160
#define MVPP22_AXI_WR_SNOOP_CODE_REG		0x4164

/* Values for AXI Bridge registers */
#define MVPP22_AXI_ATTR_CACHE_OFFS		0
#define MVPP22_AXI_ATTR_DOMAIN_OFFS		12

#define MVPP22_AXI_CODE_CACHE_OFFS		0
#define MVPP22_AXI_CODE_DOMAIN_OFFS		4

#define MVPP22_AXI_CODE_CACHE_NON_CACHE		0x3
#define MVPP22_AXI_CODE_CACHE_WR_CACHE		0x7
#define MVPP22_AXI_CODE_CACHE_RD_CACHE		0xb

#define MVPP22_AXI_CODE_DOMAIN_OUTER_DOM	2
#define MVPP22_AXI_CODE_DOMAIN_SYSTEM		3

/* Interrupt Cause and Mask registers */
#define MVPP2_ISR_TX_THRESHOLD_REG(port)	(0x5140 + 4 * (port))
#define     MVPP2_MAX_ISR_TX_THRESHOLD		0xfffff0

#define MVPP2_ISR_RX_THRESHOLD_REG(rxq)		(0x5200 + 4 * (rxq))
#define     MVPP2_MAX_ISR_RX_THRESHOLD		0xfffff0
#define MVPP21_ISR_RXQ_GROUP_REG(port)		(0x5400 + 4 * (port))

#define MVPP22_ISR_RXQ_GROUP_INDEX_REG		0x5400
#define MVPP22_ISR_RXQ_GROUP_INDEX_SUBGROUP_MASK 0xf
#define MVPP22_ISR_RXQ_GROUP_INDEX_GROUP_MASK	0x380
#define MVPP22_ISR_RXQ_GROUP_INDEX_GROUP_OFFSET	7

#define MVPP22_ISR_RXQ_GROUP_INDEX_SUBGROUP_MASK 0xf
#define MVPP22_ISR_RXQ_GROUP_INDEX_GROUP_MASK	0x380

#define MVPP22_ISR_RXQ_SUB_GROUP_CONFIG_REG	0x5404
#define MVPP22_ISR_RXQ_SUB_GROUP_STARTQ_MASK	0x1f
#define MVPP22_ISR_RXQ_SUB_GROUP_SIZE_MASK	0xf00
#define MVPP22_ISR_RXQ_SUB_GROUP_SIZE_OFFSET	8

#define MVPP2_ISR_ENABLE_REG(port)		(0x5420 + 4 * (port))
#define     MVPP2_ISR_ENABLE_INTERRUPT(mask)	((mask) & 0xffff)
#define     MVPP2_ISR_DISABLE_INTERRUPT(mask)	(((mask) << 16) & 0xffff0000)
#define MVPP2_ISR_RX_TX_CAUSE_REG(port)		(0x5480 + 4 * (port))
#define     MVPP2_CAUSE_RXQ_OCCUP_DESC_ALL_MASK(version) \
					((version) == MVPP21 ? 0xffff : 0xff)
#define     MVPP2_CAUSE_TXQ_OCCUP_DESC_ALL_MASK	0xff0000
#define     MVPP2_CAUSE_TXQ_OCCUP_DESC_ALL_OFFSET	16
#define     MVPP2_CAUSE_RX_FIFO_OVERRUN_MASK	BIT(24)
#define     MVPP2_CAUSE_FCS_ERR_MASK		BIT(25)
#define     MVPP2_CAUSE_TX_FIFO_UNDERRUN_MASK	BIT(26)
#define     MVPP2_CAUSE_TX_EXCEPTION_SUM_MASK	BIT(29)
#define     MVPP2_CAUSE_RX_EXCEPTION_SUM_MASK	BIT(30)
#define     MVPP2_CAUSE_MISC_SUM_MASK		BIT(31)
#define MVPP2_ISR_RX_TX_MASK_REG(port)		(0x54a0 + 4 * (port))
#define MVPP2_ISR_PON_RX_TX_MASK_REG		0x54bc
#define     MVPP2_PON_CAUSE_RXQ_OCCUP_DESC_ALL_MASK	0xffff
#define     MVPP2_PON_CAUSE_TXP_OCCUP_DESC_ALL_MASK	0x3fc00000
#define     MVPP2_PON_CAUSE_MISC_SUM_MASK		BIT(31)
#define MVPP2_ISR_MISC_CAUSE_REG		0x55b0

/* Buffer Manager registers */
#define MVPP2_BM_POOL_BASE_REG(pool)		(0x6000 + ((pool) * 4))
#define     MVPP2_BM_POOL_BASE_ADDR_MASK	0xfffff80
#define MVPP2_BM_POOL_SIZE_REG(pool)		(0x6040 + ((pool) * 4))
#define     MVPP2_BM_POOL_SIZE_MASK		0xfff0
#define MVPP2_BM_POOL_READ_PTR_REG(pool)	(0x6080 + ((pool) * 4))
#define     MVPP2_BM_POOL_GET_READ_PTR_MASK	0xfff0
#define MVPP2_BM_POOL_PTRS_NUM_REG(pool)	(0x60c0 + ((pool) * 4))
#define     MVPP2_BM_POOL_PTRS_NUM_MASK		0xfff0
#define MVPP2_BM_BPPI_READ_PTR_REG(pool)	(0x6100 + ((pool) * 4))
#define MVPP2_BM_BPPI_PTRS_NUM_REG(pool)	(0x6140 + ((pool) * 4))
#define     MVPP2_BM_BPPI_PTR_NUM_MASK		0x7ff
#define MVPP22_BM_POOL_PTRS_NUM_MASK		0xfff8
#define     MVPP2_BM_BPPI_PREFETCH_FULL_MASK	BIT(16)
#define MVPP2_BM_POOL_CTRL_REG(pool)		(0x6200 + ((pool) * 4))
#define     MVPP2_BM_START_MASK			BIT(0)
#define     MVPP2_BM_STOP_MASK			BIT(1)
#define     MVPP2_BM_STATE_MASK			BIT(4)
#define     MVPP2_BM_LOW_THRESH_OFFS		8
#define     MVPP2_BM_LOW_THRESH_MASK		0x7f00
#define     MVPP2_BM_LOW_THRESH_VALUE(val)	((val) << \
						MVPP2_BM_LOW_THRESH_OFFS)
#define     MVPP2_BM_HIGH_THRESH_OFFS		16
#define     MVPP2_BM_HIGH_THRESH_MASK		0x7f0000
#define     MVPP2_BM_HIGH_THRESH_VALUE(val)	((val) << \
						MVPP2_BM_HIGH_THRESH_OFFS)
#define MVPP2_BM_INTR_CAUSE_REG(pool)		(0x6240 + ((pool) * 4))
#define     MVPP2_BM_RELEASED_DELAY_MASK	BIT(0)
#define     MVPP2_BM_ALLOC_FAILED_MASK		BIT(1)
#define     MVPP2_BM_BPPE_EMPTY_MASK		BIT(2)
#define     MVPP2_BM_BPPE_FULL_MASK		BIT(3)
#define     MVPP2_BM_AVAILABLE_BP_LOW_MASK	BIT(4)
#define MVPP2_BM_INTR_MASK_REG(pool)		(0x6280 + ((pool) * 4))
#define MVPP2_BM_PHY_ALLOC_REG(pool)		(0x6400 + ((pool) * 4))
#define     MVPP2_BM_PHY_ALLOC_GRNTD_MASK	BIT(0)
#define MVPP2_BM_VIRT_ALLOC_REG			0x6440
#define MVPP22_BM_ADDR_HIGH_ALLOC		0x6444
#define     MVPP22_BM_ADDR_HIGH_PHYS_MASK	0xff
#define     MVPP22_BM_ADDR_HIGH_VIRT_MASK	0xff00
#define     MVPP22_BM_ADDR_HIGH_VIRT_SHIFT	8
#define MVPP2_BM_PHY_RLS_REG(pool)		(0x6480 + ((pool) * 4))
#define     MVPP2_BM_PHY_RLS_MC_BUFF_MASK	BIT(0)
#define     MVPP2_BM_PHY_RLS_PRIO_EN_MASK	BIT(1)
#define     MVPP2_BM_PHY_RLS_GRNTD_MASK		BIT(2)
#define MVPP2_BM_VIRT_RLS_REG			0x64c0
#define MVPP22_BM_ADDR_HIGH_RLS_REG		0x64c4
#define     MVPP22_BM_ADDR_HIGH_PHYS_RLS_MASK	0xff
#define     MVPP22_BM_ADDR_HIGH_VIRT_RLS_MASK	0xff00
#define     MVPP22_BM_ADDR_HIGH_VIRT_RLS_SHIFT	8

/* Packet Processor per-port counters */
#define MVPP2_OVERRUN_ETH_DROP			0x7000
#define MVPP2_CLS_ETH_DROP			0x7020

/* Hit counters registers */
#define MVPP2_CTRS_IDX				0x7040
#define     MVPP22_CTRS_TX_CTR(port, txq)	((txq) | ((port) << 3) | BIT(7))
#define MVPP2_TX_DESC_ENQ_CTR			0x7100
#define MVPP2_TX_DESC_ENQ_TO_DDR_CTR		0x7104
#define MVPP2_TX_BUFF_ENQ_TO_DDR_CTR		0x7108
#define MVPP2_TX_DESC_ENQ_HW_FWD_CTR		0x710c
#define MVPP2_RX_DESC_ENQ_CTR			0x7120
#define MVPP2_TX_PKTS_DEQ_CTR			0x7130
#define MVPP2_TX_PKTS_FULL_QUEUE_DROP_CTR	0x7200
#define MVPP2_TX_PKTS_EARLY_DROP_CTR		0x7204
#define MVPP2_TX_PKTS_BM_DROP_CTR		0x7208
#define MVPP2_TX_PKTS_BM_MC_DROP_CTR		0x720c
#define MVPP2_RX_PKTS_FULL_QUEUE_DROP_CTR	0x7220
#define MVPP2_RX_PKTS_EARLY_DROP_CTR		0x7224
#define MVPP2_RX_PKTS_BM_DROP_CTR		0x7228
#define MVPP2_CLS_DEC_TBL_HIT_CTR		0x7700
#define MVPP2_CLS_FLOW_TBL_HIT_CTR		0x7704

/* TX Scheduler registers */
#define MVPP2_TXP_SCHED_PORT_INDEX_REG		0x8000
#define MVPP2_TXP_SCHED_Q_CMD_REG		0x8004
#define     MVPP2_TXP_SCHED_ENQ_MASK		0xff
#define     MVPP2_TXP_SCHED_DISQ_OFFSET		8
#define MVPP2_TXP_SCHED_CMD_1_REG		0x8010
#define MVPP2_TXP_SCHED_FIXED_PRIO_REG		0x8014
#define MVPP2_TXP_SCHED_PERIOD_REG		0x8018
#define MVPP2_TXP_SCHED_MTU_REG			0x801c
#define     MVPP2_TXP_MTU_MAX			0x7FFFF
#define MVPP2_TXP_SCHED_REFILL_REG		0x8020
#define     MVPP2_TXP_REFILL_TOKENS_ALL_MASK	0x7ffff
#define     MVPP2_TXP_REFILL_PERIOD_ALL_MASK	0x3ff00000
#define     MVPP2_TXP_REFILL_PERIOD_MASK(v)	((v) << 20)
#define MVPP2_TXP_SCHED_TOKEN_SIZE_REG		0x8024
#define     MVPP2_TXP_TOKEN_SIZE_MAX		0xffffffff
#define MVPP2_TXQ_SCHED_REFILL_REG(q)		(0x8040 + ((q) << 2))
#define     MVPP2_TXQ_REFILL_TOKENS_ALL_MASK	0x7ffff
#define     MVPP2_TXQ_REFILL_PERIOD_ALL_MASK	0x3ff00000
#define     MVPP2_TXQ_REFILL_PERIOD_MASK(v)	((v) << 20)
#define MVPP2_TXQ_SCHED_TOKEN_SIZE_REG(q)	(0x8060 + ((q) << 2))
#define     MVPP2_TXQ_TOKEN_SIZE_MAX		0x7fffffff
#define MVPP2_TXQ_SCHED_TOKEN_CNTR_REG(q)	(0x8080 + ((q) << 2))
#define     MVPP2_TXQ_TOKEN_CNTR_MAX		0xffffffff

/* TX general registers */
#define MVPP2_TX_SNOOP_REG			0x8800
#define MVPP2_TX_PORT_FLUSH_REG			0x8810
#define     MVPP2_TX_PORT_FLUSH_MASK(port)	(1 << (port))

/* LMS registers */
#define MVPP2_SRC_ADDR_MIDDLE			0x24
#define MVPP2_SRC_ADDR_HIGH			0x28
#define MVPP2_PHY_AN_CFG0_REG			0x34
#define     MVPP2_PHY_AN_STOP_SMI0_MASK		BIT(7)
#define MVPP2_MNG_EXTENDED_GLOBAL_CTRL_REG	0x305c
#define     MVPP2_EXT_GLOBAL_CTRL_DEFAULT	0x27

/* Per-port registers */
#define MVPP2_GMAC_CTRL_0_REG			0x0
#define     MVPP2_GMAC_PORT_EN_MASK		BIT(0)
#define     MVPP2_GMAC_PORT_TYPE_MASK		BIT(1)
#define     MVPP2_GMAC_MAX_RX_SIZE_OFFS		2
#define     MVPP2_GMAC_MAX_RX_SIZE_MASK		0x7ffc
#define     MVPP2_GMAC_MIB_CNTR_EN_MASK		BIT(15)
#define MVPP2_GMAC_CTRL_1_REG			0x4
#define     MVPP2_GMAC_PERIODIC_XON_EN_MASK	BIT(1)
#define     MVPP2_GMAC_GMII_LB_EN_MASK		BIT(5)
#define     MVPP2_GMAC_PCS_LB_EN_BIT		6
#define     MVPP2_GMAC_PCS_LB_EN_MASK		BIT(6)
#define     MVPP2_GMAC_SA_LOW_OFFS		7
#define MVPP2_GMAC_CTRL_2_REG			0x8
#define     MVPP2_GMAC_INBAND_AN_MASK		BIT(0)
#define     MVPP2_GMAC_FLOW_CTRL_MASK		GENMASK(2, 1)
#define     MVPP2_GMAC_PCS_ENABLE_MASK		BIT(3)
#define     MVPP2_GMAC_INTERNAL_CLK_MASK	BIT(4)
#define     MVPP2_GMAC_DISABLE_PADDING		BIT(5)
#define     MVPP2_GMAC_PORT_RESET_MASK		BIT(6)
#define MVPP2_GMAC_AUTONEG_CONFIG		0xc
#define     MVPP2_GMAC_FORCE_LINK_DOWN		BIT(0)
#define     MVPP2_GMAC_FORCE_LINK_PASS		BIT(1)
#define     MVPP2_GMAC_IN_BAND_AUTONEG		BIT(2)
#define     MVPP2_GMAC_IN_BAND_AUTONEG_BYPASS	BIT(3)
#define     MVPP2_GMAC_IN_BAND_RESTART_AN	BIT(4)
#define     MVPP2_GMAC_CONFIG_MII_SPEED		BIT(5)
#define     MVPP2_GMAC_CONFIG_GMII_SPEED	BIT(6)
#define     MVPP2_GMAC_AN_SPEED_EN		BIT(7)
#define     MVPP2_GMAC_FC_ADV_EN		BIT(9)
#define     MVPP2_GMAC_FC_ADV_ASM_EN		BIT(10)
#define     MVPP2_GMAC_FLOW_CTRL_AUTONEG	BIT(11)
#define     MVPP2_GMAC_CONFIG_FULL_DUPLEX	BIT(12)
#define     MVPP2_GMAC_AN_DUPLEX_EN		BIT(13)
#define MVPP2_GMAC_STATUS0			0x10
#define     MVPP2_GMAC_STATUS0_LINK_UP		BIT(0)
#define     MVPP2_GMAC_STATUS0_GMII_SPEED	BIT(1)
#define     MVPP2_GMAC_STATUS0_MII_SPEED	BIT(2)
#define     MVPP2_GMAC_STATUS0_FULL_DUPLEX	BIT(3)
#define     MVPP2_GMAC_STATUS0_RX_PAUSE		BIT(4)
#define     MVPP2_GMAC_STATUS0_TX_PAUSE		BIT(5)
#define     MVPP2_GMAC_STATUS0_AN_COMPLETE	BIT(11)
#define MVPP2_GMAC_PORT_FIFO_CFG_1_REG		0x1c
#define     MVPP2_GMAC_TX_FIFO_MIN_TH_OFFS	6
#define     MVPP2_GMAC_TX_FIFO_MIN_TH_ALL_MASK	0x1fc0
#define     MVPP2_GMAC_TX_FIFO_MIN_TH_MASK(v)	(((v) << 6) & \
					MVPP2_GMAC_TX_FIFO_MIN_TH_ALL_MASK)
#define MVPP22_GMAC_INT_STAT			0x20
#define     MVPP22_GMAC_INT_STAT_LINK		BIT(1)
#define MVPP22_GMAC_INT_MASK			0x24
#define     MVPP22_GMAC_INT_MASK_LINK_STAT	BIT(1)
#define MVPP22_GMAC_CTRL_4_REG			0x90
#define     MVPP22_CTRL4_EXT_PIN_GMII_SEL	BIT(0)
#define     MVPP22_CTRL4_RX_FC_EN		BIT(3)
#define     MVPP22_CTRL4_TX_FC_EN		BIT(4)
#define     MVPP22_CTRL4_DP_CLK_SEL		BIT(5)
#define     MVPP22_CTRL4_SYNC_BYPASS_DIS	BIT(6)
#define     MVPP22_CTRL4_QSGMII_BYPASS_ACTIVE	BIT(7)
#define MVPP22_GMAC_INT_SUM_MASK		0xa4
#define     MVPP22_GMAC_INT_SUM_MASK_LINK_STAT	BIT(1)

/* Per-port XGMAC registers. PPv2.2 only, only for GOP port 0,
 * relative to port->base.
 */
#define MVPP22_XLG_CTRL0_REG			0x100
#define     MVPP22_XLG_CTRL0_PORT_EN		BIT(0)
#define     MVPP22_XLG_CTRL0_MAC_RESET_DIS	BIT(1)
#define     MVPP22_XLG_CTRL0_FORCE_LINK_DOWN	BIT(2)
#define     MVPP22_XLG_CTRL0_FORCE_LINK_PASS	BIT(3)
#define     MVPP22_XLG_CTRL0_RX_FLOW_CTRL_EN	BIT(7)
#define     MVPP22_XLG_CTRL0_TX_FLOW_CTRL_EN	BIT(8)
#define     MVPP22_XLG_CTRL0_MIB_CNT_DIS	BIT(14)
#define MVPP22_XLG_CTRL1_REG			0x104
#define     MVPP22_XLG_CTRL1_FRAMESIZELIMIT_OFFS	0
#define     MVPP22_XLG_CTRL1_FRAMESIZELIMIT_MASK	0x1fff
#define MVPP22_XLG_STATUS			0x10c
#define     MVPP22_XLG_STATUS_LINK_UP		BIT(0)
#define MVPP22_XLG_INT_STAT			0x114
#define     MVPP22_XLG_INT_STAT_LINK		BIT(1)
#define MVPP22_XLG_INT_MASK			0x118
#define     MVPP22_XLG_INT_MASK_LINK		BIT(1)
#define MVPP22_XLG_CTRL3_REG			0x11c
#define     MVPP22_XLG_CTRL3_MACMODESELECT_MASK	(7 << 13)
#define     MVPP22_XLG_CTRL3_MACMODESELECT_GMAC	(0 << 13)
#define     MVPP22_XLG_CTRL3_MACMODESELECT_10G	(1 << 13)
#define MVPP22_XLG_EXT_INT_MASK			0x15c
#define     MVPP22_XLG_EXT_INT_MASK_XLG		BIT(1)
#define     MVPP22_XLG_EXT_INT_MASK_GIG		BIT(2)
#define MVPP22_XLG_CTRL4_REG			0x184
#define     MVPP22_XLG_CTRL4_FWD_FC		BIT(5)
#define     MVPP22_XLG_CTRL4_FWD_PFC		BIT(6)
#define     MVPP22_XLG_CTRL4_MACMODSELECT_GMAC	BIT(12)
#define     MVPP22_XLG_CTRL4_EN_IDLE_CHECK	BIT(14)

/* SMI registers. PPv2.2 only, relative to priv->iface_base. */
#define MVPP22_SMI_MISC_CFG_REG			0x1204
#define     MVPP22_SMI_POLLING_EN		BIT(10)

#define MVPP22_GMAC_BASE(port)		(0x7000 + (port) * 0x1000 + 0xe00)

#define MVPP2_CAUSE_TXQ_SENT_DESC_ALL_MASK	0xff

/* Descriptor ring Macros */
#define MVPP2_QUEUE_NEXT_DESC(q, index) \
	(((index) < (q)->last_desc) ? ((index) + 1) : 0)

/* XPCS registers. PPv2.2 only */
#define MVPP22_MPCS_BASE(port)			(0x7000 + (port) * 0x1000)
#define MVPP22_MPCS_CTRL			0x14
#define     MVPP22_MPCS_CTRL_FWD_ERR_CONN	BIT(10)
#define MVPP22_MPCS_CLK_RESET			0x14c
#define     MAC_CLK_RESET_SD_TX			BIT(0)
#define     MAC_CLK_RESET_SD_RX			BIT(1)
#define     MAC_CLK_RESET_MAC			BIT(2)
#define     MVPP22_MPCS_CLK_RESET_DIV_RATIO(n)	((n) << 4)
#define     MVPP22_MPCS_CLK_RESET_DIV_SET	BIT(11)

/* XPCS registers. PPv2.2 only */
#define MVPP22_XPCS_BASE(port)			(0x7400 + (port) * 0x1000)
#define MVPP22_XPCS_CFG0			0x0
#define     MVPP22_XPCS_CFG0_RESET_DIS		BIT(0)
#define     MVPP22_XPCS_CFG0_PCS_MODE(n)	((n) << 3)
#define     MVPP22_XPCS_CFG0_ACTIVE_LANE(n)	((n) << 5)

/* System controller registers. Accessed through a regmap. */
#define GENCONF_SOFT_RESET1				0x1108
#define     GENCONF_SOFT_RESET1_GOP			BIT(6)
#define GENCONF_PORT_CTRL0				0x1110
#define     GENCONF_PORT_CTRL0_BUS_WIDTH_SELECT		BIT(1)
#define     GENCONF_PORT_CTRL0_RX_DATA_SAMPLE		BIT(29)
#define     GENCONF_PORT_CTRL0_CLK_DIV_PHASE_CLR	BIT(31)
#define GENCONF_PORT_CTRL1				0x1114
#define     GENCONF_PORT_CTRL1_EN(p)			BIT(p)
#define     GENCONF_PORT_CTRL1_RESET(p)			(BIT(p) << 28)
#define GENCONF_CTRL0					0x1120
#define     GENCONF_CTRL0_PORT0_RGMII			BIT(0)
#define     GENCONF_CTRL0_PORT1_RGMII_MII		BIT(1)
#define     GENCONF_CTRL0_PORT1_RGMII			BIT(2)

/* Various constants */

/* Coalescing */
#define MVPP2_TXDONE_COAL_PKTS_THRESH	64
#define MVPP2_TXDONE_HRTIMER_PERIOD_NS	1000000UL
#define MVPP2_TXDONE_COAL_USEC		1000
#define MVPP2_RX_COAL_PKTS		32
#define MVPP2_RX_COAL_USEC		64

/* The two bytes Marvell header. Either contains a special value used
 * by Marvell switches when a specific hardware mode is enabled (not
 * supported by this driver) or is filled automatically by zeroes on
 * the RX side. Those two bytes being at the front of the Ethernet
 * header, they allow to have the IP header aligned on a 4 bytes
 * boundary automatically: the hardware skips those two bytes on its
 * own.
 */
#define MVPP2_MH_SIZE			2
#define MVPP2_ETH_TYPE_LEN		2
#define MVPP2_PPPOE_HDR_SIZE		8
#define MVPP2_VLAN_TAG_LEN		4
#define MVPP2_VLAN_TAG_EDSA_LEN		8

/* Lbtd 802.3 type */
#define MVPP2_IP_LBDT_TYPE		0xfffa

#define MVPP2_TX_CSUM_MAX_SIZE		9800

/* Timeout constants */
#define MVPP2_TX_DISABLE_TIMEOUT_MSEC	1000
#define MVPP2_TX_PENDING_TIMEOUT_MSEC	1000

#define MVPP2_TX_MTU_MAX		0x7ffff

/* Maximum number of T-CONTs of PON port */
#define MVPP2_MAX_TCONT			16

/* Maximum number of supported ports */
#define MVPP2_MAX_PORTS			4

/* Maximum number of TXQs used by single port */
#define MVPP2_MAX_TXQ			8

/* MVPP2_MAX_TSO_SEGS is the maximum number of fragments to allow in the GSO
 * skb. As we need a maxium of two descriptors per fragments (1 header, 1 data),
 * multiply this value by two to count the maximum number of skb descs needed.
 */
#define MVPP2_MAX_TSO_SEGS		300
#define MVPP2_MAX_SKB_DESCS		(MVPP2_MAX_TSO_SEGS * 2 + MAX_SKB_FRAGS)

/* Max number of RXQs per port */
#define MVPP2_PORT_MAX_RXQ		32

/* Max number of Rx descriptors */
#define MVPP2_MAX_RXD_MAX		1024
#define MVPP2_MAX_RXD_DFLT		128

/* Max number of Tx descriptors */
#define MVPP2_MAX_TXD_MAX		2048
#define MVPP2_MAX_TXD_DFLT		1024

/* Amount of Tx descriptors that can be reserved at once by CPU */
#define MVPP2_CPU_DESC_CHUNK		64

/* Max number of Tx descriptors in each aggregated queue */
#define MVPP2_AGGR_TXQ_SIZE		256

/* Descriptor aligned size */
#define MVPP2_DESC_ALIGNED_SIZE		32

/* Descriptor alignment mask */
#define MVPP2_TX_DESC_ALIGN		(MVPP2_DESC_ALIGNED_SIZE - 1)

/* RX FIFO constants */
#define MVPP2_RX_FIFO_PORT_DATA_SIZE_32KB	0x8000
#define MVPP2_RX_FIFO_PORT_DATA_SIZE_8KB	0x2000
#define MVPP2_RX_FIFO_PORT_DATA_SIZE_4KB	0x1000
#define MVPP2_RX_FIFO_PORT_ATTR_SIZE_32KB	0x200
#define MVPP2_RX_FIFO_PORT_ATTR_SIZE_8KB	0x80
#define MVPP2_RX_FIFO_PORT_ATTR_SIZE_4KB	0x40
#define MVPP2_RX_FIFO_PORT_MIN_PKT		0x80

/* TX FIFO constants */
#define MVPP22_TX_FIFO_DATA_SIZE_10KB		0xa
#define MVPP22_TX_FIFO_DATA_SIZE_3KB		0x3
#define MVPP2_TX_FIFO_THRESHOLD_MIN		256
#define MVPP2_TX_FIFO_THRESHOLD_10KB	\
	(MVPP22_TX_FIFO_DATA_SIZE_10KB * 1024 - MVPP2_TX_FIFO_THRESHOLD_MIN)
#define MVPP2_TX_FIFO_THRESHOLD_3KB	\
	(MVPP22_TX_FIFO_DATA_SIZE_3KB * 1024 - MVPP2_TX_FIFO_THRESHOLD_MIN)

/* RX buffer constants */
#define MVPP2_SKB_SHINFO_SIZE \
	SKB_DATA_ALIGN(sizeof(struct skb_shared_info))

#define MVPP2_RX_PKT_SIZE(mtu) \
	ALIGN((mtu) + MVPP2_MH_SIZE + MVPP2_VLAN_TAG_LEN + \
	      ETH_HLEN + ETH_FCS_LEN, cache_line_size())

#define MVPP2_RX_BUF_SIZE(pkt_size)	((pkt_size) + NET_SKB_PAD)
#define MVPP2_RX_TOTAL_SIZE(buf_size)	((buf_size) + MVPP2_SKB_SHINFO_SIZE)
#define MVPP2_RX_MAX_PKT_SIZE(total_size) \
	((total_size) - NET_SKB_PAD - MVPP2_SKB_SHINFO_SIZE)

#define MVPP2_BIT_TO_BYTE(bit)		((bit) / 8)
#define MVPP2_BIT_TO_WORD(bit)		((bit) / 32)
#define MVPP2_BIT_IN_WORD(bit)		((bit) % 32)

#define MVPP2_N_PRS_FLOWS		52
#define MVPP2_N_RFS_ENTRIES_PER_FLOW	4

/* There are 7 supported high-level flows */
#define MVPP2_N_RFS_RULES		(MVPP2_N_RFS_ENTRIES_PER_FLOW * 7)

/* RSS constants */
#define MVPP22_N_RSS_TABLES		8
#define MVPP22_RSS_TABLE_ENTRIES	32

/* IPv6 max L3 address size */
#define MVPP2_MAX_L3_ADDR_SIZE		16

/* Port flags */
#define MVPP2_F_LOOPBACK		BIT(0)
#define MVPP2_F_DT_COMPAT		BIT(1)

/* Marvell tag types */
enum mvpp2_tag_type {
	MVPP2_TAG_TYPE_NONE = 0,
	MVPP2_TAG_TYPE_MH   = 1,
	MVPP2_TAG_TYPE_DSA  = 2,
	MVPP2_TAG_TYPE_EDSA = 3,
	MVPP2_TAG_TYPE_VLAN = 4,
	MVPP2_TAG_TYPE_LAST = 5
};

/* L2 cast enum */
enum mvpp2_prs_l2_cast {
	MVPP2_PRS_L2_UNI_CAST,
	MVPP2_PRS_L2_MULTI_CAST,
};

/* L3 cast enum */
enum mvpp2_prs_l3_cast {
	MVPP2_PRS_L3_UNI_CAST,
	MVPP2_PRS_L3_MULTI_CAST,
	MVPP2_PRS_L3_BROAD_CAST
};

/* BM constants */
#define MVPP2_BM_JUMBO_BUF_NUM		512
#define MVPP2_BM_LONG_BUF_NUM		1024
#define MVPP2_BM_SHORT_BUF_NUM		2048
#define MVPP2_BM_POOL_SIZE_MAX		(16*1024 - MVPP2_BM_POOL_PTR_ALIGN/4)
#define MVPP2_BM_POOL_PTR_ALIGN		128
#define MVPP2_BM_MAX_POOLS		8

/* BM cookie (32 bits) definition */
#define MVPP2_BM_COOKIE_POOL_OFFS	8
#define MVPP2_BM_COOKIE_CPU_OFFS	24

#define MVPP2_BM_SHORT_FRAME_SIZE		512
#define MVPP2_BM_LONG_FRAME_SIZE		2048
#define MVPP2_BM_JUMBO_FRAME_SIZE		10240
/* BM short pool packet size
 * These value assure that for SWF the total number
 * of bytes allocated for each buffer will be 512
 */
#define MVPP2_BM_SHORT_PKT_SIZE	MVPP2_RX_MAX_PKT_SIZE(MVPP2_BM_SHORT_FRAME_SIZE)
#define MVPP2_BM_LONG_PKT_SIZE	MVPP2_RX_MAX_PKT_SIZE(MVPP2_BM_LONG_FRAME_SIZE)
#define MVPP2_BM_JUMBO_PKT_SIZE	MVPP2_RX_MAX_PKT_SIZE(MVPP2_BM_JUMBO_FRAME_SIZE)

#define MVPP21_ADDR_SPACE_SZ		0
#define MVPP22_ADDR_SPACE_SZ		SZ_64K

#define MVPP2_MAX_THREADS		9
#define MVPP2_MAX_QVECS			MVPP2_MAX_THREADS

/* GMAC MIB Counters register definitions */
#define MVPP21_MIB_COUNTERS_OFFSET		0x1000
#define MVPP21_MIB_COUNTERS_PORT_SZ		0x400
#define MVPP22_MIB_COUNTERS_OFFSET		0x0
#define MVPP22_MIB_COUNTERS_PORT_SZ		0x100

#define MVPP2_MIB_GOOD_OCTETS_RCVD		0x0
#define MVPP2_MIB_BAD_OCTETS_RCVD		0x8
#define MVPP2_MIB_CRC_ERRORS_SENT		0xc
#define MVPP2_MIB_UNICAST_FRAMES_RCVD		0x10
#define MVPP2_MIB_BROADCAST_FRAMES_RCVD		0x18
#define MVPP2_MIB_MULTICAST_FRAMES_RCVD		0x1c
#define MVPP2_MIB_FRAMES_64_OCTETS		0x20
#define MVPP2_MIB_FRAMES_65_TO_127_OCTETS	0x24
#define MVPP2_MIB_FRAMES_128_TO_255_OCTETS	0x28
#define MVPP2_MIB_FRAMES_256_TO_511_OCTETS	0x2c
#define MVPP2_MIB_FRAMES_512_TO_1023_OCTETS	0x30
#define MVPP2_MIB_FRAMES_1024_TO_MAX_OCTETS	0x34
#define MVPP2_MIB_GOOD_OCTETS_SENT		0x38
#define MVPP2_MIB_UNICAST_FRAMES_SENT		0x40
#define MVPP2_MIB_MULTICAST_FRAMES_SENT		0x48
#define MVPP2_MIB_BROADCAST_FRAMES_SENT		0x4c
#define MVPP2_MIB_FC_SENT			0x54
#define MVPP2_MIB_FC_RCVD			0x58
#define MVPP2_MIB_RX_FIFO_OVERRUN		0x5c
#define MVPP2_MIB_UNDERSIZE_RCVD		0x60
#define MVPP2_MIB_FRAGMENTS_RCVD		0x64
#define MVPP2_MIB_OVERSIZE_RCVD			0x68
#define MVPP2_MIB_JABBER_RCVD			0x6c
#define MVPP2_MIB_MAC_RCV_ERROR			0x70
#define MVPP2_MIB_BAD_CRC_EVENT			0x74
#define MVPP2_MIB_COLLISION			0x78
#define MVPP2_MIB_LATE_COLLISION		0x7c

#define MVPP2_MIB_COUNTERS_STATS_DELAY		(1 * HZ)

#define MVPP2_DESC_DMA_MASK	DMA_BIT_MASK(40)

/* Definitions */
struct mvpp2_dbgfs_entries;

struct mvpp2_rss_table {
	u32 indir[MVPP22_RSS_TABLE_ENTRIES];
};

/* Shared Packet Processor resources */
struct mvpp2 {
	/* Shared registers' base addresses */
	void __iomem *lms_base;
	void __iomem *iface_base;

	/* On PPv2.2, each "software thread" can access the base
	 * register through a separate address space, each 64 KB apart
	 * from each other. Typically, such address spaces will be
	 * used per CPU.
	 */
	void __iomem *swth_base[MVPP2_MAX_THREADS];

	/* On PPv2.2, some port control registers are located into the system
	 * controller space. These registers are accessible through a regmap.
	 */
	struct regmap *sysctrl_base;

	/* Common clocks */
	struct clk *pp_clk;
	struct clk *gop_clk;
	struct clk *mg_clk;
	struct clk *mg_core_clk;
	struct clk *axi_clk;

	/* List of pointers to port structures */
	int port_count;
	struct mvpp2_port *port_list[MVPP2_MAX_PORTS];

	/* Number of Tx threads used */
	unsigned int nthreads;
	/* Map of threads needing locking */
	unsigned long lock_map;

	/* Aggregated TXQs */
	struct mvpp2_tx_queue *aggr_txqs;

	/* Are we using page_pool with per-cpu pools? */
	int percpu_pools;

	/* BM pools */
	struct mvpp2_bm_pool *bm_pools;

	/* PRS shadow table */
	struct mvpp2_prs_shadow *prs_shadow;
	/* PRS auxiliary table for double vlan entries control */
	bool *prs_double_vlans;

	/* Tclk value */
	u32 tclk;

	/* HW version */
	enum { MVPP21, MVPP22 } hw_version;

	/* Maximum number of RXQs per port */
	unsigned int max_port_rxqs;

	/* Workqueue to gather hardware statistics */
	char queue_name[30];
	struct workqueue_struct *stats_queue;

	/* Debugfs root entry */
	struct dentry *dbgfs_dir;

	/* Debugfs entries private data */
	struct mvpp2_dbgfs_entries *dbgfs_entries;

	/* RSS Indirection tables */
	struct mvpp2_rss_table *rss_tables[MVPP22_N_RSS_TABLES];
};

struct mvpp2_pcpu_stats {
	struct	u64_stats_sync syncp;
	u64	rx_packets;
	u64	rx_bytes;
	u64	tx_packets;
	u64	tx_bytes;
};

/* Per-CPU port control */
struct mvpp2_port_pcpu {
	struct hrtimer tx_done_timer;
	struct net_device *dev;
	bool timer_scheduled;
};

struct mvpp2_queue_vector {
	int irq;
	struct napi_struct napi;
	enum { MVPP2_QUEUE_VECTOR_SHARED, MVPP2_QUEUE_VECTOR_PRIVATE } type;
	int sw_thread_id;
	u16 sw_thread_mask;
	int first_rxq;
	int nrxqs;
	u32 pending_cause_rx;
	struct mvpp2_port *port;
	struct cpumask *mask;
};

/* Internal represention of a Flow Steering rule */
struct mvpp2_rfs_rule {
	/* Rule location inside the flow*/
	int loc;

	/* Flow type, such as TCP_V4_FLOW, IP6_FLOW, etc. */
	int flow_type;

	/* Index of the C2 TCAM entry handling this rule */
	int c2_index;

	/* Header fields that needs to be extracted to match this flow */
	u16 hek_fields;

	/* CLS engine : only c2 is supported for now. */
	u8 engine;

	/* TCAM key and mask for C2-based steering. These fields should be
	 * encapsulated in a union should we add more engines.
	 */
	u64 c2_tcam;
	u64 c2_tcam_mask;

	struct flow_rule *flow;
};

struct mvpp2_ethtool_fs {
	struct mvpp2_rfs_rule rule;
	struct ethtool_rxnfc rxnfc;
};

struct mvpp2_port {
	u8 id;

	/* Index of the port from the "group of ports" complex point
	 * of view. This is specific to PPv2.2.
	 */
	int gop_id;

	int link_irq;

	struct mvpp2 *priv;

	/* Firmware node associated to the port */
	struct fwnode_handle *fwnode;

	/* Is a PHY always connected to the port */
	bool has_phy;

	/* Per-port registers' base address */
	void __iomem *base;
	void __iomem *stats_base;

	struct mvpp2_rx_queue **rxqs;
	unsigned int nrxqs;
	struct mvpp2_tx_queue **txqs;
	unsigned int ntxqs;
	struct net_device *dev;

	int pkt_size;

	/* Per-CPU port control */
	struct mvpp2_port_pcpu __percpu *pcpu;

	/* Protect the BM refills and the Tx paths when a thread is used on more
	 * than a single CPU.
	 */
	spinlock_t bm_lock[MVPP2_MAX_THREADS];
	spinlock_t tx_lock[MVPP2_MAX_THREADS];

	/* Flags */
	unsigned long flags;

	u16 tx_ring_size;
	u16 rx_ring_size;
	struct mvpp2_pcpu_stats __percpu *stats;
	u64 *ethtool_stats;

	/* Per-port work and its lock to gather hardware statistics */
	struct mutex gather_stats_lock;
	struct delayed_work stats_work;

	struct device_node *of_node;

	phy_interface_t phy_interface;
	struct phylink *phylink;
	struct phylink_config phylink_config;
	struct phy *comphy;

	struct mvpp2_bm_pool *pool_long;
	struct mvpp2_bm_pool *pool_short;

	/* Index of first port's physical RXQ */
	u8 first_rxq;

	struct mvpp2_queue_vector qvecs[MVPP2_MAX_QVECS];
	unsigned int nqvecs;
	bool has_tx_irqs;

	u32 tx_time_coal;

	/* List of steering rules active on that port */
	struct mvpp2_ethtool_fs *rfs_rules[MVPP2_N_RFS_ENTRIES_PER_FLOW];
	int n_rfs_rules;

	/* Each port has its own view of the rss contexts, so that it can number
	 * them from 0
	 */
	int rss_ctx[MVPP22_N_RSS_TABLES];
};

/* The mvpp2_tx_desc and mvpp2_rx_desc structures describe the
 * layout of the transmit and reception DMA descriptors, and their
 * layout is therefore defined by the hardware design
 */

#define MVPP2_TXD_L3_OFF_SHIFT		0
#define MVPP2_TXD_IP_HLEN_SHIFT		8
#define MVPP2_TXD_L4_CSUM_FRAG		BIT(13)
#define MVPP2_TXD_L4_CSUM_NOT		BIT(14)
#define MVPP2_TXD_IP_CSUM_DISABLE	BIT(15)
#define MVPP2_TXD_PADDING_DISABLE	BIT(23)
#define MVPP2_TXD_L4_UDP		BIT(24)
#define MVPP2_TXD_L3_IP6		BIT(26)
#define MVPP2_TXD_L_DESC		BIT(28)
#define MVPP2_TXD_F_DESC		BIT(29)

#define MVPP2_RXD_ERR_SUMMARY		BIT(15)
#define MVPP2_RXD_ERR_CODE_MASK		(BIT(13) | BIT(14))
#define MVPP2_RXD_ERR_CRC		0x0
#define MVPP2_RXD_ERR_OVERRUN		BIT(13)
#define MVPP2_RXD_ERR_RESOURCE		(BIT(13) | BIT(14))
#define MVPP2_RXD_BM_POOL_ID_OFFS	16
#define MVPP2_RXD_BM_POOL_ID_MASK	(BIT(16) | BIT(17) | BIT(18))
#define MVPP2_RXD_HWF_SYNC		BIT(21)
#define MVPP2_RXD_L4_CSUM_OK		BIT(22)
#define MVPP2_RXD_IP4_HEADER_ERR	BIT(24)
#define MVPP2_RXD_L4_TCP		BIT(25)
#define MVPP2_RXD_L4_UDP		BIT(26)
#define MVPP2_RXD_L3_IP4		BIT(28)
#define MVPP2_RXD_L3_IP6		BIT(30)
#define MVPP2_RXD_BUF_HDR		BIT(31)

/* HW TX descriptor for PPv2.1 */
struct mvpp21_tx_desc {
	__le32 command;		/* Options used by HW for packet transmitting.*/
	u8  packet_offset;	/* the offset from the buffer beginning	*/
	u8  phys_txq;		/* destination queue ID			*/
	__le16 data_size;	/* data size of transmitted packet in bytes */
	__le32 buf_dma_addr;	/* physical addr of transmitted buffer	*/
	__le32 buf_cookie;	/* cookie for access to TX buffer in tx path */
	__le32 reserved1[3];	/* hw_cmd (for future use, BM, PON, PNC) */
	__le32 reserved2;	/* reserved (for future use)		*/
};

/* HW RX descriptor for PPv2.1 */
struct mvpp21_rx_desc {
	__le32 status;		/* info about received packet		*/
	__le16 reserved1;	/* parser_info (for future use, PnC)	*/
	__le16 data_size;	/* size of received packet in bytes	*/
	__le32 buf_dma_addr;	/* physical address of the buffer	*/
	__le32 buf_cookie;	/* cookie for access to RX buffer in rx path */
	__le16 reserved2;	/* gem_port_id (for future use, PON)	*/
	__le16 reserved3;	/* csum_l4 (for future use, PnC)	*/
	u8  reserved4;		/* bm_qset (for future use, BM)		*/
	u8  reserved5;
	__le16 reserved6;	/* classify_info (for future use, PnC)	*/
	__le32 reserved7;	/* flow_id (for future use, PnC) */
	__le32 reserved8;
};

/* HW TX descriptor for PPv2.2 */
struct mvpp22_tx_desc {
	__le32 command;
	u8  packet_offset;
	u8  phys_txq;
	__le16 data_size;
	__le64 reserved1;
	__le64 buf_dma_addr_ptp;
	__le64 buf_cookie_misc;
};

/* HW RX descriptor for PPv2.2 */
struct mvpp22_rx_desc {
	__le32 status;
	__le16 reserved1;
	__le16 data_size;
	__le32 reserved2;
	__le32 reserved3;
	__le64 buf_dma_addr_key_hash;
	__le64 buf_cookie_misc;
};

/* Opaque type used by the driver to manipulate the HW TX and RX
 * descriptors
 */
struct mvpp2_tx_desc {
	union {
		struct mvpp21_tx_desc pp21;
		struct mvpp22_tx_desc pp22;
	};
};

struct mvpp2_rx_desc {
	union {
		struct mvpp21_rx_desc pp21;
		struct mvpp22_rx_desc pp22;
	};
};

struct mvpp2_txq_pcpu_buf {
	/* Transmitted SKB */
	struct sk_buff *skb;

	/* Physical address of transmitted buffer */
	dma_addr_t dma;

	/* Size transmitted */
	size_t size;
};

/* Per-CPU Tx queue control */
struct mvpp2_txq_pcpu {
	unsigned int thread;

	/* Number of Tx DMA descriptors in the descriptor ring */
	int size;

	/* Number of currently used Tx DMA descriptor in the
	 * descriptor ring
	 */
	int count;

	int wake_threshold;
	int stop_threshold;

	/* Number of Tx DMA descriptors reserved for each CPU */
	int reserved_num;

	/* Infos about transmitted buffers */
	struct mvpp2_txq_pcpu_buf *buffs;

	/* Index of last TX DMA descriptor that was inserted */
	int txq_put_index;

	/* Index of the TX DMA descriptor to be cleaned up */
	int txq_get_index;

	/* DMA buffer for TSO headers */
	char *tso_headers;
	dma_addr_t tso_headers_dma;
};

struct mvpp2_tx_queue {
	/* Physical number of this Tx queue */
	u8 id;

	/* Logical number of this Tx queue */
	u8 log_id;

	/* Number of Tx DMA descriptors in the descriptor ring */
	int size;

	/* Number of currently used Tx DMA descriptor in the descriptor ring */
	int count;

	/* Per-CPU control of physical Tx queues */
	struct mvpp2_txq_pcpu __percpu *pcpu;

	u32 done_pkts_coal;

	/* Virtual address of thex Tx DMA descriptors array */
	struct mvpp2_tx_desc *descs;

	/* DMA address of the Tx DMA descriptors array */
	dma_addr_t descs_dma;

	/* Index of the last Tx DMA descriptor */
	int last_desc;

	/* Index of the next Tx DMA descriptor to process */
	int next_desc_to_proc;
};

struct mvpp2_rx_queue {
	/* RX queue number, in the range 0-31 for physical RXQs */
	u8 id;

	/* Num of rx descriptors in the rx descriptor ring */
	int size;

	u32 pkts_coal;
	u32 time_coal;

	/* Virtual address of the RX DMA descriptors array */
	struct mvpp2_rx_desc *descs;

	/* DMA address of the RX DMA descriptors array */
	dma_addr_t descs_dma;

	/* Index of the last RX DMA descriptor */
	int last_desc;

	/* Index of the next RX DMA descriptor to process */
	int next_desc_to_proc;

	/* ID of port to which physical RXQ is mapped */
	int port;

	/* Port's logic RXQ number to which physical RXQ is mapped */
	int logic_rxq;
};

struct mvpp2_bm_pool {
	/* Pool number in the range 0-7 */
	int id;

	/* Buffer Pointers Pool External (BPPE) size */
	int size;
	/* BPPE size in bytes */
	int size_bytes;
	/* Number of buffers for this pool */
	int buf_num;
	/* Pool buffer size */
	int buf_size;
	/* Packet size */
	int pkt_size;
	int frag_size;

	/* BPPE virtual base address */
	u32 *virt_addr;
	/* BPPE DMA base address */
	dma_addr_t dma_addr;

	/* Ports using BM pool */
	u32 port_map;
};

#define IS_TSO_HEADER(txq_pcpu, addr) \
	((addr) >= (txq_pcpu)->tso_headers_dma && \
	 (addr) < (txq_pcpu)->tso_headers_dma + \
	 (txq_pcpu)->size * TSO_HEADER_SIZE)

#define MVPP2_DRIVER_NAME "mvpp2"
#define MVPP2_DRIVER_VERSION "1.0"

void mvpp2_write(struct mvpp2 *priv, u32 offset, u32 data);
u32 mvpp2_read(struct mvpp2 *priv, u32 offset);

void mvpp2_dbgfs_init(struct mvpp2 *priv, const char *name);

void mvpp2_dbgfs_cleanup(struct mvpp2 *priv);

#endif
