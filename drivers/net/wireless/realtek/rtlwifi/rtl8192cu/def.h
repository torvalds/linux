/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#include "../rtl8192ce/def.h"

/*-------------------------------------------------------------------------
 *	Chip specific
 *-------------------------------------------------------------------------*/
#define NORMAL_CHIP			BIT(4)
#define CHIP_VENDOR_UMC			BIT(5)
#define CHIP_VENDOR_UMC_B_CUT		BIT(6)

#define IS_92C_1T2R(version)		\
	(((version) & CHIP_92C) && ((version) & CHIP_92C_1T2R))

#define IS_VENDOR_UMC(version)		\
	(((version) & CHIP_VENDOR_UMC) ? true : false)

#define CHIP_BONDING_92C_1T2R	0x1
#define CHIP_BONDING_IDENTIFIER(_value)	(((_value) >> 22) & 0x3)
