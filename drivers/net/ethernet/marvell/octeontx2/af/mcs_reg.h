/* SPDX-License-Identifier: GPL-2.0 */
/* Marvell MCS driver
 *
 * Copyright (C) 2022 Marvell.
 */

#ifndef MCS_REG_H
#define MCS_REG_H

#include <linux/bits.h>

/* Registers */
#define MCSX_IP_MODE					0x900c8ull
#define MCSX_MCS_TOP_SLAVE_PORT_RESET(a) ({	\
	u64 offset;					\
							\
	offset = 0x408ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0xa28ull;			\
	offset += (a) * 0x8ull;				\
	offset; })


#define MCSX_MCS_TOP_SLAVE_CHANNEL_CFG(a) ({		\
	u64 offset;					\
							\
	offset = 0x808ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0xa68ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_MIL_GLOBAL	({				\
	u64 offset;					\
							\
	offset = 0x80000ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x60000ull;			\
	offset; })

#define MCSX_MIL_RX_LMACX_CFG(a) ({			\
	u64 offset;					\
							\
	offset = 0x900a8ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x700a8ull;			\
	offset += (a) * 0x800ull;			\
	offset; })

#define MCSX_HIL_GLOBAL ({				\
	u64 offset;					\
							\
	offset = 0xc0000ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0xa0000ull;			\
	offset; })

#define MCSX_LINK_LMACX_CFG(a) ({			\
	u64 offset;					\
							\
	offset = 0x90000ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x70000ull;			\
	offset += (a) * 0x800ull;			\
	offset; })

#define MCSX_MIL_RX_GBL_STATUS ({			\
	u64 offset;					\
							\
	offset = 0x800c8ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x600c8ull;			\
	offset; })

#define MCSX_MIL_IP_GBL_STATUS ({			\
	u64 offset;					\
							\
	offset = 0x800d0ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x600d0ull;			\
	offset; })

/* PAB */
#define MCSX_PAB_RX_SLAVE_PORT_CFGX(a) ({	\
	u64 offset;				\
						\
	offset = 0x1718ull;			\
	if (mcs->hw->mcs_blks > 1)		\
		offset = 0x280ull;		\
	offset += (a) * 0x40ull;		\
	offset; })

#define MCSX_PAB_TX_SLAVE_PORT_CFGX(a)			(0x2930ull + (a) * 0x40ull)

/* PEX registers */
#define MCSX_PEX_RX_SLAVE_VLAN_CFGX(a)          (0x3b58ull + (a) * 0x8ull)
#define MCSX_PEX_TX_SLAVE_VLAN_CFGX(a)          (0x46f8ull + (a) * 0x8ull)
#define MCSX_PEX_TX_SLAVE_CUSTOM_TAG_REL_MODE_SEL(a)	(0x788ull + (a) * 0x8ull)
#define MCSX_PEX_TX_SLAVE_PORT_CONFIG(a)		(0x4738ull + (a) * 0x8ull)
#define MCSX_PEX_RX_SLAVE_RULE_ETYPE_CFGX(a) ({	\
	u64 offset;					\
							\
	offset = 0x3fc0ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x558ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_PEX_RX_SLAVE_RULE_DAX(a) ({	\
	u64 offset;					\
							\
	offset = 0x4000ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x598ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_PEX_RX_SLAVE_RULE_DA_RANGE_MINX(a) ({	\
	u64 offset;					\
							\
	offset = 0x4040ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x5d8ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_PEX_RX_SLAVE_RULE_DA_RANGE_MAXX(a) ({	\
	u64 offset;					\
							\
	offset = 0x4048ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x5e0ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_PEX_RX_SLAVE_RULE_COMBO_MINX(a) ({	\
	u64 offset;					\
							\
	offset = 0x4080ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x648ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_PEX_RX_SLAVE_RULE_COMBO_MAXX(a) ({	\
	u64 offset;					\
							\
	offset = 0x4088ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x650ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_PEX_RX_SLAVE_RULE_COMBO_ETX(a) ({	\
	u64 offset;					\
							\
	offset = 0x4090ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x658ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_PEX_RX_SLAVE_RULE_MAC ({	\
	u64 offset;					\
							\
	offset = 0x40e0ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x6d8ull;			\
	offset; })

#define MCSX_PEX_RX_SLAVE_RULE_ENABLE ({	\
	u64 offset;					\
							\
	offset = 0x40e8ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x6e0ull;			\
	offset; })

#define MCSX_PEX_TX_SLAVE_RULE_ETYPE_CFGX(a) ({	\
	u64 offset;					\
							\
	offset = 0x4b60ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x7d8ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_PEX_TX_SLAVE_RULE_DAX(a) ({	\
	u64 offset;					\
							\
	offset = 0x4ba0ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x818ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_PEX_TX_SLAVE_RULE_DA_RANGE_MINX(a) ({	\
	u64 offset;					\
							\
	offset = 0x4be0ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x858ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_PEX_TX_SLAVE_RULE_DA_RANGE_MAXX(a) ({	\
	u64 offset;					\
							\
	offset = 0x4be8ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x860ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_PEX_TX_SLAVE_RULE_COMBO_MINX(a) ({	\
	u64 offset;					\
							\
	offset = 0x4c20ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x8c8ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_PEX_TX_SLAVE_RULE_COMBO_MAXX(a) ({	\
	u64 offset;					\
							\
	offset = 0x4c28ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x8d0ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_PEX_TX_SLAVE_RULE_COMBO_ETX(a) ({	\
	u64 offset;					\
							\
	offset = 0x4c30ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x8d8ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_PEX_TX_SLAVE_RULE_MAC ({	\
	u64 offset;					\
							\
	offset = 0x4c80ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x958ull;			\
	offset; })

#define MCSX_PEX_TX_SLAVE_RULE_ENABLE ({	\
	u64 offset;					\
							\
	offset = 0x4c88ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x960ull;			\
	offset; })

#define MCSX_PEX_RX_SLAVE_PEX_CONFIGURATION ({		\
	u64 offset;					\
							\
	offset = 0x3b50ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x4c0ull;			\
	offset; })

/* CNF10K-B */
#define MCSX_PEX_RX_SLAVE_CUSTOM_TAGX(a)        (0x4c8ull + (a) * 0x8ull)
#define MCSX_PEX_TX_SLAVE_CUSTOM_TAGX(a)        (0x748ull + (a) * 0x8ull)
#define MCSX_PEX_RX_SLAVE_ETYPE_ENABLE          0x6e8ull
#define MCSX_PEX_TX_SLAVE_ETYPE_ENABLE          0x968ull

/* BEE */
#define MCSX_BBE_RX_SLAVE_PADDING_CTL			0xe08ull
#define MCSX_BBE_TX_SLAVE_PADDING_CTL			0x12f8ull
#define MCSX_BBE_RX_SLAVE_CAL_ENTRY			0x180ull
#define MCSX_BBE_RX_SLAVE_CAL_LEN			0x188ull
#define MCSX_PAB_RX_SLAVE_FIFO_SKID_CFGX(a)		(0x290ull + (a) * 0x40ull)

#define MCSX_BBE_RX_SLAVE_BBE_INT ({	\
	u64 offset;			\
					\
	offset = 0xe00ull;		\
	if (mcs->hw->mcs_blks > 1)	\
		offset = 0x160ull;	\
	offset; })

#define MCSX_BBE_RX_SLAVE_BBE_INT_ENB ({	\
	u64 offset;			\
					\
	offset = 0xe08ull;		\
	if (mcs->hw->mcs_blks > 1)	\
		offset = 0x168ull;	\
	offset; })

#define MCSX_BBE_RX_SLAVE_BBE_INT_INTR_RW ({	\
	u64 offset;			\
					\
	offset = 0xe08ull;		\
	if (mcs->hw->mcs_blks > 1)	\
		offset = 0x178ull;	\
	offset; })

#define MCSX_BBE_TX_SLAVE_BBE_INT ({	\
	u64 offset;			\
					\
	offset = 0x1278ull;		\
	if (mcs->hw->mcs_blks > 1)	\
		offset = 0x1e0ull;	\
	offset; })

#define MCSX_BBE_TX_SLAVE_BBE_INT_INTR_RW ({	\
	u64 offset;			\
					\
	offset = 0x1278ull;		\
	if (mcs->hw->mcs_blks > 1)	\
		offset = 0x1f8ull;	\
	offset; })

#define MCSX_BBE_TX_SLAVE_BBE_INT_ENB ({	\
	u64 offset;			\
					\
	offset = 0x1280ull;		\
	if (mcs->hw->mcs_blks > 1)	\
		offset = 0x1e8ull;	\
	offset; })

#define MCSX_PAB_RX_SLAVE_PAB_INT ({	\
	u64 offset;			\
					\
	offset = 0x16f0ull;		\
	if (mcs->hw->mcs_blks > 1)	\
		offset = 0x260ull;	\
	offset; })

#define MCSX_PAB_RX_SLAVE_PAB_INT_ENB ({	\
	u64 offset;			\
					\
	offset = 0x16f8ull;		\
	if (mcs->hw->mcs_blks > 1)	\
		offset = 0x268ull;	\
	offset; })

#define MCSX_PAB_RX_SLAVE_PAB_INT_INTR_RW ({	\
	u64 offset;			\
					\
	offset = 0x16f8ull;		\
	if (mcs->hw->mcs_blks > 1)	\
		offset = 0x278ull;	\
	offset; })

#define MCSX_PAB_TX_SLAVE_PAB_INT ({	\
	u64 offset;			\
					\
	offset = 0x2908ull;		\
	if (mcs->hw->mcs_blks > 1)	\
		offset = 0x380ull;	\
	offset; })

#define MCSX_PAB_TX_SLAVE_PAB_INT_ENB ({	\
	u64 offset;			\
					\
	offset = 0x2910ull;		\
	if (mcs->hw->mcs_blks > 1)	\
		offset = 0x388ull;	\
	offset; })

#define MCSX_PAB_TX_SLAVE_PAB_INT_INTR_RW ({	\
	u64 offset;			\
					\
	offset = 0x16f8ull;		\
	if (mcs->hw->mcs_blks > 1)	\
		offset = 0x398ull;	\
	offset; })

/* CPM registers */
#define MCSX_CPM_RX_SLAVE_FLOWID_TCAM_DATAX(a, b) ({	\
	u64 offset;					\
							\
	offset = 0x30740ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x3bf8ull;			\
	offset += (a) * 0x8ull + (b) * 0x20ull;		\
	offset; })

#define MCSX_CPM_RX_SLAVE_FLOWID_TCAM_MASKX(a, b) ({	\
	u64 offset;					\
							\
	offset = 0x34740ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x43f8ull;			\
	offset += (a) * 0x8ull + (b) * 0x20ull;		\
	offset; })

#define MCSX_CPM_RX_SLAVE_FLOWID_TCAM_ENA_0 ({		\
	u64 offset;					\
							\
	offset = 0x30700ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x3bd8ull;			\
	offset; })

#define MCSX_CPM_RX_SLAVE_SC_CAMX(a, b)	({		\
	u64 offset;					\
							\
	offset = 0x38780ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x4c08ull;			\
	offset +=  (a) * 0x8ull + (b) * 0x10ull;	\
	offset; })

#define MCSX_CPM_RX_SLAVE_SC_CAM_ENA(a)	({		\
	u64 offset;					\
							\
	offset = 0x38740ull + (a) * 0x8ull;		\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x4bf8ull;			\
	offset; })

#define MCSX_CPM_RX_SLAVE_SECY_MAP_MEMX(a) ({		\
	u64 offset;					\
							\
	offset = 0x23ee0ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0xbd0ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_CPM_RX_SLAVE_SECY_PLCY_MEM_0X(a) ({	\
	u64 offset;					\
							\
	offset = (0x246e0ull + (a) * 0x10ull);		\
	if (mcs->hw->mcs_blks > 1)			\
		offset = (0xdd0ull + (a) * 0x8ull);	\
	offset; })

#define MCSX_CPM_RX_SLAVE_SA_KEY_LOCKOUTX(a) ({		\
	u64 offset;					\
							\
	offset = 0x23E90ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0xbb0ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_CPM_RX_SLAVE_SA_MAP_MEMX(a) ({		\
	u64 offset;					\
							\
	offset = 0x256e0ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0xfd0ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_CPM_RX_SLAVE_SA_PLCY_MEMX(a, b) ({		\
	u64 offset;					\
							\
	offset = 0x27700ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x17d8ull;			\
	offset +=  (a) * 0x8ull + (b) * 0x40ull;	\
	offset; })

#define MCSX_CPM_RX_SLAVE_SA_PN_TABLE_MEMX(a) ({	\
	u64 offset;					\
							\
	offset = 0x2f700ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x37d8;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_CPM_RX_SLAVE_XPN_THRESHOLD	({		\
	u64 offset;					\
							\
	offset = 0x23e40ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0xb90ull;			\
	offset; })

#define MCSX_CPM_RX_SLAVE_PN_THRESHOLD	({		\
	u64 offset;					\
							\
	offset = 0x23e48ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0xb98ull;			\
	offset; })

#define MCSX_CPM_RX_SLAVE_PN_THRESH_REACHEDX(a)	({	\
	u64 offset;					\
							\
	offset = 0x23e50ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0xba0ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_CPM_RX_SLAVE_FLOWID_TCAM_ENA_1		0x30708ull
#define MCSX_CPM_RX_SLAVE_SECY_PLCY_MEM_1X(a)		(0x246e8ull + (a) * 0x10ull)

/* TX registers */
#define MCSX_CPM_TX_SLAVE_FLOWID_TCAM_DATAX(a, b) ({	\
	u64 offset;					\
							\
	offset = 0x51d50ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0xa7c0ull;			\
	offset += (a) * 0x8ull + (b) * 0x20ull;		\
	offset; })

#define MCSX_CPM_TX_SLAVE_FLOWID_TCAM_MASKX(a, b) ({	\
	u64 offset;					\
							\
	offset = 0x55d50ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0xafc0ull;			\
	offset += (a) * 0x8ull + (b) * 0x20ull;		\
	offset; })

#define MCSX_CPM_TX_SLAVE_FLOWID_TCAM_ENA_0 ({		\
	u64 offset;					\
							\
	offset = 0x51d10ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0xa7a0ull;			\
	offset; })

#define MCSX_CPM_TX_SLAVE_SECY_MAP_MEM_0X(a) ({		\
	u64 offset;					\
							\
	offset = 0x3e508ull + (a) * 0x8ull;		\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x5550ull + (a) * 0x10ull;	\
	offset; })

#define MCSX_CPM_TX_SLAVE_SECY_PLCY_MEMX(a) ({	\
	u64 offset;					\
							\
	offset = 0x3ed08ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x5950ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_CPM_TX_SLAVE_SA_KEY_LOCKOUTX(a) ({		\
	u64 offset;					\
							\
	offset = 0x3e4c0ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x5538ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_CPM_TX_SLAVE_SA_MAP_MEM_0X(a) ({		\
	u64 offset;					\
							\
	offset = 0x3fd10ull + (a) * 0x10ull;		\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x6150ull + (a) * 0x8ull;	\
	offset; })

#define MCSX_CPM_TX_SLAVE_SA_PLCY_MEMX(a, b) ({		\
	u64 offset;					\
							\
	offset = 0x40d10ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x63a0ull;			\
	offset += (a) * 0x8ull + (b) * 0x80ull;		\
	offset; })

#define MCSX_CPM_TX_SLAVE_SA_PN_TABLE_MEMX(a) ({	\
	u64 offset;					\
							\
	offset = 0x50d10ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0xa3a0ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_CPM_TX_SLAVE_XPN_THRESHOLD ({		\
	u64 offset;					\
							\
	offset = 0x3e4b0ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x5528ull;			\
	offset; })

#define MCSX_CPM_TX_SLAVE_PN_THRESHOLD ({		\
	u64 offset;					\
							\
	offset = 0x3e4b8ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x5530ull;			\
	offset; })

#define MCSX_CPM_TX_SLAVE_SA_MAP_MEM_1X(a)		(0x3fd18ull + (a) * 0x10ull)
#define MCSX_CPM_TX_SLAVE_SECY_MAP_MEM_1X(a)		(0x5558ull + (a) * 0x10ull)
#define MCSX_CPM_TX_SLAVE_FLOWID_TCAM_ENA_1		0x51d18ull
#define MCSX_CPM_TX_SLAVE_TX_SA_ACTIVEX(a)		(0x5b50 + (a) * 0x8ull)
#define MCSX_CPM_TX_SLAVE_SA_INDEX0_VLDX(a)		(0x5d50 + (a) * 0x8ull)
#define MCSX_CPM_TX_SLAVE_SA_INDEX1_VLDX(a)		(0x5f50 + (a) * 0x8ull)
#define MCSX_CPM_TX_SLAVE_AUTO_REKEY_ENABLE_0		0x5500ull

/* CSE */
#define MCSX_CSE_RX_MEM_SLAVE_IFINCTLBCPKTSX(a) ({	\
	u64 offset;					\
							\
	offset = 0x9e80ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0xc218ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_CSE_RX_MEM_SLAVE_IFINCTLMCPKTSX(a) ({	\
	u64 offset;					\
							\
	offset = 0x9680ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0xc018ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_CSE_RX_MEM_SLAVE_IFINCTLOCTETSX(a) ({	\
	u64 offset;					\
							\
	offset = 0x6e80ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0xbc18ull;			\
	offset +=  (a) * 0x8ull;			\
	offset; })

#define MCSX_CSE_RX_MEM_SLAVE_IFINCTLUCPKTSX(a) ({	\
	u64 offset;					\
							\
	offset = 0x8e80ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0xbe18ull;			\
	offset +=  (a) * 0x8ull;			\
	offset; })

#define	MCSX_CSE_RX_MEM_SLAVE_IFINUNCTLBCPKTSX(a) ({	\
	u64 offset;					\
							\
	offset = 0x8680ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0xca18ull;			\
	offset +=  (a) * 0x8ull;			\
	offset; })

#define	MCSX_CSE_RX_MEM_SLAVE_IFINUNCTLMCPKTSX(a) ({	\
	u64 offset;					\
							\
	offset = 0x7e80ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0xc818ull;			\
	offset +=  (a) * 0x8ull;			\
	offset; })

#define	MCSX_CSE_RX_MEM_SLAVE_IFINUNCTLOCTETSX(a) ({	\
	u64 offset;					\
							\
	offset = 0x6680ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0xc418ull;			\
	offset +=  (a) * 0x8ull;			\
	offset; })

#define	MCSX_CSE_RX_MEM_SLAVE_IFINUNCTLUCPKTSX(a) ({	\
	u64 offset;					\
							\
	offset = 0x7680ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0xc618ull;			\
	offset +=  (a) * 0x8ull;			\
	offset; })

#define MCSX_CSE_RX_MEM_SLAVE_INOCTETSSECYDECRYPTEDX(a) ({ \
	u64 offset;					\
							\
	offset = 0x5e80ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0xdc18ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_CSE_RX_MEM_SLAVE_INOCTETSSECYVALIDATEX(a)({ \
	u64 offset;					\
							\
	offset = 0x5680ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0xda18ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_CSE_RX_MEM_SLAVE_INPKTSCTRLPORTDISABLEDX(a) ({ \
	u64 offset;					\
							\
	offset = 0xd680ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0xce18ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_CSE_RX_MEM_SLAVE_INPKTSFLOWIDTCAMHITX(a) ({ \
	u64 offset;					\
							\
	offset = 0x16a80ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0xec78ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_CSE_RX_MEM_SLAVE_INPKTSFLOWIDTCAMMISSX(a) ({ \
	u64 offset;					\
							\
	offset = 0x16680ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0xec38ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_CSE_RX_MEM_SLAVE_INPKTSPARSEERRX(a) ({	\
	u64 offset;					\
							\
	offset = 0x16880ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0xec18ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_CSE_RX_MEM_SLAVE_INPKTSSCCAMHITX(a) ({	\
	u64 offset;					\
							\
	offset = 0xfe80ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0xde18ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_CSE_RX_MEM_SLAVE_INPKTSSCINVALIDX(a) ({	\
	u64 offset;					\
							\
	offset = 0x10680ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0xe418ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_CSE_RX_MEM_SLAVE_INPKTSSCNOTVALIDX(a) ({	\
	u64 offset;					\
							\
	offset = 0x10e80ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0xe218ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_CSE_RX_MEM_SLAVE_INPKTSSECYBADTAGX(a) ({	\
	u64 offset;					\
							\
	offset = 0xae80ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0xd418ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_CSE_RX_MEM_SLAVE_INPKTSSECYNOSAX(a) ({	\
	u64 offset;					\
							\
	offset = 0xc680ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0xd618ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_CSE_RX_MEM_SLAVE_INPKTSSECYNOSAERRORX(a) ({ \
	u64 offset;					\
							\
	offset = 0xce80ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0xd818ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_CSE_RX_MEM_SLAVE_INPKTSSECYTAGGEDCTLX(a) ({ \
	u64 offset;					\
							\
	offset = 0xbe80ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0xcc18ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_CSE_RX_SLAVE_CTRL	({			\
	u64 offset;					\
							\
	offset = 0x52a0ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x9c0ull;			\
	offset; })

#define MCSX_CSE_RX_SLAVE_STATS_CLEAR	({		\
	u64 offset;					\
							\
	offset = 0x52b8ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x9d8ull;			\
	offset; })

#define MCSX_CSE_RX_MEM_SLAVE_INOCTETSSCDECRYPTEDX(a)	(0xe680ull + (a) * 0x8ull)
#define MCSX_CSE_RX_MEM_SLAVE_INOCTETSSCVALIDATEX(a)	(0xde80ull + (a) * 0x8ull)
#define MCSX_CSE_RX_MEM_SLAVE_INPKTSSECYUNTAGGEDORNOTAGX(a)	(0xa680ull + (a) * 0x8ull)
#define MCSX_CSE_RX_MEM_SLAVE_INPKTSSECYNOTAGX(a)	(0xd218 + (a) * 0x8ull)
#define MCSX_CSE_RX_MEM_SLAVE_INPKTSSECYUNTAGGEDX(a)	(0xd018ull + (a) * 0x8ull)
#define MCSX_CSE_RX_MEM_SLAVE_INPKTSSCUNCHECKEDOROKX(a)	(0xee80ull + (a) * 0x8ull)
#define MCSX_CSE_RX_MEM_SLAVE_INPKTSSECYCTLX(a)		(0xb680ull + (a) * 0x8ull)
#define MCSX_CSE_RX_MEM_SLAVE_INPKTSSCLATEORDELAYEDX(a) (0xf680ull + (a) * 0x8ull)
#define MCSX_CSE_RX_MEM_SLAVE_INPKTSSAINVALIDX(a)	(0x12680ull + (a) * 0x8ull)
#define MCSX_CSE_RX_MEM_SLAVE_INPKTSSANOTUSINGSAERRORX(a) (0x15680ull + (a) * 0x8ull)
#define MCSX_CSE_RX_MEM_SLAVE_INPKTSSANOTVALIDX(a)	(0x13680ull + (a) * 0x8ull)
#define MCSX_CSE_RX_MEM_SLAVE_INPKTSSAOKX(a)		(0x11680ull + (a) * 0x8ull)
#define MCSX_CSE_RX_MEM_SLAVE_INPKTSSAUNUSEDSAX(a)	(0x14680ull + (a) * 0x8ull)
#define MCSX_CSE_RX_MEM_SLAVE_INPKTSEARLYPREEMPTERRX(a) (0xec58ull + (a) * 0x8ull)
#define MCSX_CSE_RX_MEM_SLAVE_INPKTSSCOKX(a)		(0xea18ull + (a) * 0x8ull)
#define MCSX_CSE_RX_MEM_SLAVE_INPKTSSCDELAYEDX(a)	(0xe618ull + (a) * 0x8ull)

/* CSE TX */
#define MCSX_CSE_TX_MEM_SLAVE_IFOUTCOMMONOCTETSX(a)	(0x18440ull + (a) * 0x8ull)
#define MCSX_CSE_TX_MEM_SLAVE_IFOUTCTLBCPKTSX(a) ({	\
	u64 offset;					\
							\
	offset = 0x1c440ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0xf478ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_CSE_TX_MEM_SLAVE_IFOUTCTLMCPKTSX(a) ({	\
	u64 offset;					\
							\
	offset = 0x1bc40ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0xf278ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_CSE_TX_MEM_SLAVE_IFOUTCTLOCTETSX(a) ({	\
	u64 offset;					\
							\
	offset = 0x19440ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0xee78ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_CSE_TX_MEM_SLAVE_IFOUTCTLUCPKTSX(a) ({	\
	u64 offset;					\
							\
	offset = 0x1b440ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0xf078ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_CSE_TX_MEM_SLAVE_IFOUTUNCTLBCPKTSX(a) ({	\
	u64 offset;					\
							\
	offset = 0x1ac40ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0xfc78ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_CSE_TX_MEM_SLAVE_IFOUTUNCTLMCPKTSX(a) ({	\
	u64 offset;					\
							\
	offset = 0x1a440ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0xfa78ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_CSE_TX_MEM_SLAVE_IFOUTUNCTLOCTETSX(a) ({	\
	u64 offset;					\
							\
	offset = 0x18c40ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0xf678ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_CSE_TX_MEM_SLAVE_IFOUTUNCTLUCPKTSX(a) ({	\
	u64 offset;					\
							\
	offset = 0x19c40ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0xf878ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_CSE_TX_MEM_SLAVE_OUTOCTETSSECYENCRYPTEDX(a) ({	\
	u64 offset;					\
							\
	offset = 0x17c40ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x10878ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_CSE_TX_MEM_SLAVE_OUTOCTETSSECYPROTECTEDX(a) ({	\
	u64 offset;					\
							\
	offset = 0x17440ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x10678ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_CSE_TX_MEM_SLAVE_OUTPKTSCTRLPORTDISABLEDX(a) ({	\
	u64 offset;					\
							\
	offset = 0x1e440ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0xfe78ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_CSE_TX_MEM_SLAVE_OUTPKTSFLOWIDTCAMHITX(a) ({	\
	u64 offset;					\
							\
	offset = 0x23240ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x10ed8ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_CSE_TX_MEM_SLAVE_OUTPKTSFLOWIDTCAMMISSX(a) ({	\
	u64 offset;					\
							\
	offset = 0x22c40ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x10e98ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_CSE_TX_MEM_SLAVE_OUTPKTSPARSEERRX(a) ({	\
	u64 offset;					\
							\
	offset = 0x22e40ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x10e78ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_CSE_TX_MEM_SLAVE_OUTPKTSSCENCRYPTEDX(a) ({	\
	u64 offset;					\
							\
	offset = 0x20440ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x10c78ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_CSE_TX_MEM_SLAVE_OUTPKTSSCPROTECTEDX(a) ({	\
	u64 offset;					\
							\
	offset = 0x1fc40ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x10a78ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_CSE_TX_MEM_SLAVE_OUTPKTSSECTAGINSERTIONERRX(a) ({	\
	u64 offset;					\
							\
	offset = 0x23040ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x110d8ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_CSE_TX_MEM_SLAVE_OUTPKTSSECYNOACTIVESAX(a) ({	\
	u64 offset;					\
							\
	offset = 0x1dc40ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x10278ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_CSE_TX_MEM_SLAVE_OUTPKTSSECYTOOLONGX(a) ({	\
	u64 offset;					\
							\
	offset = 0x1d440ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x10478ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_CSE_TX_MEM_SLAVE_OUTPKTSSECYUNTAGGEDX(a) ({	\
	u64 offset;					\
							\
	offset = 0x1cc40ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0x10078ull;			\
	offset += (a) * 0x8ull;				\
	offset; })

#define MCSX_CSE_TX_SLAVE_CTRL	({	\
	u64 offset;					\
							\
	offset = 0x54a0ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0xa00ull;			\
	offset; })

#define MCSX_CSE_TX_SLAVE_STATS_CLEAR ({		\
	u64 offset;					\
							\
	offset = 0x54b8ull;				\
	if (mcs->hw->mcs_blks > 1)			\
		offset = 0xa18ull;			\
	offset; })

#define MCSX_CSE_TX_MEM_SLAVE_OUTOCTETSSCENCRYPTEDX(a)	(0x1f440ull + (a) * 0x8ull)
#define MCSX_CSE_TX_MEM_SLAVE_OUTOCTETSSCPROTECTEDX(a)	(0x1ec40ull + (a) * 0x8ull)
#define MCSX_CSE_TX_MEM_SLAVE_OUTPKTSEARLYPREEMPTERRX(a) (0x10eb8ull + (a) * 0x8ull)
#define MCSX_CSE_TX_MEM_SLAVE_OUTPKTSSAENCRYPTEDX(a)	(0x21c40ull + (a) * 0x8ull)
#define MCSX_CSE_TX_MEM_SLAVE_OUTPKTSSAPROTECTEDX(a)	(0x20c40ull + (a) * 0x8ull)

#define MCSX_IP_INT ({			\
	u64 offset;			\
					\
	offset = 0x80028ull;		\
	if (mcs->hw->mcs_blks > 1)	\
		offset = 0x60028ull;	\
	offset; })

#define MCSX_IP_INT_ENA_W1S ({		\
	u64 offset;			\
					\
	offset = 0x80040ull;		\
	if (mcs->hw->mcs_blks > 1)	\
		offset = 0x60040ull;	\
	offset; })

#define MCSX_IP_INT_ENA_W1C ({		\
	u64 offset;			\
					\
	offset = 0x80038ull;		\
	if (mcs->hw->mcs_blks > 1)	\
		offset = 0x60038ull;	\
	offset; })

#define MCSX_TOP_SLAVE_INT_SUM ({	\
	u64 offset;			\
					\
	offset = 0xc20ull;		\
	if (mcs->hw->mcs_blks > 1)	\
		offset = 0xab8ull;	\
	offset; })

#define MCSX_TOP_SLAVE_INT_SUM_ENB ({	\
	u64 offset;			\
					\
	offset = 0xc28ull;		\
	if (mcs->hw->mcs_blks > 1)	\
		offset = 0xac0ull;	\
	offset; })

#define MCSX_CPM_RX_SLAVE_RX_INT ({	\
	u64 offset;			\
					\
	offset = 0x23c00ull;		\
	if (mcs->hw->mcs_blks > 1)	\
		offset = 0x0ad8ull;	\
	offset; })

#define MCSX_CPM_RX_SLAVE_RX_INT_ENB ({	\
	u64 offset;			\
					\
	offset = 0x23c08ull;		\
	if (mcs->hw->mcs_blks > 1)	\
		offset = 0xae0ull;	\
	offset; })

#define MCSX_CPM_TX_SLAVE_TX_INT ({	\
	u64 offset;			\
					\
	offset = 0x3d490ull;		\
	if (mcs->hw->mcs_blks > 1)	\
		offset = 0x54a0ull;	\
	offset; })

#define MCSX_CPM_TX_SLAVE_TX_INT_ENB ({	\
	u64 offset;			\
					\
	offset = 0x3d498ull;		\
	if (mcs->hw->mcs_blks > 1)	\
		offset = 0x54a8ull;	\
	offset; })

#endif
