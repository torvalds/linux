/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Cache operations for the cache instruction.
 *
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#ifndef __ASM_CACHEOPS_H
#define __ASM_CACHEOPS_H

/*
 * Most cache ops are split into a 2 bit field identifying the cache, and a 3
 * bit field identifying the cache operation.
 */
#define CacheOp_Cache			0x03
#define CacheOp_Op			0x1c

#define Cache_I				0x00
#define Cache_D				0x01
#define Cache_V				0x02
#define Cache_S				0x03

#define Index_Invalidate		0x08
#define Index_Writeback_Inv		0x08
#define Hit_Invalidate			0x10
#define Hit_Writeback_Inv		0x10
#define CacheOp_User_Defined		0x18

#define Index_Invalidate_I		(Cache_I | Index_Invalidate)
#define Index_Writeback_Inv_D		(Cache_D | Index_Writeback_Inv)
#define Index_Writeback_Inv_V		(Cache_V | Index_Writeback_Inv)
#define Index_Writeback_Inv_S		(Cache_S | Index_Writeback_Inv)
#define Hit_Invalidate_I		(Cache_I | Hit_Invalidate)
#define Hit_Writeback_Inv_D		(Cache_D | Hit_Writeback_Inv)
#define Hit_Writeback_Inv_V		(Cache_V | Hit_Writeback_Inv)
#define Hit_Writeback_Inv_S		(Cache_S | Hit_Writeback_Inv)

#endif	/* __ASM_CACHEOPS_H */
