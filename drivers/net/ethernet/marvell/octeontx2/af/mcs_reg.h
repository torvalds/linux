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

#endif
