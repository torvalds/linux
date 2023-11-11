/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Cache operations for the cache instruction.
 *
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#ifndef __ASM_CACHEOPS_H
#define __ASM_CACHEOPS_H

/*
 * Most cache ops are split into a 3 bit field identifying the cache, and a 2
 * bit field identifying the cache operation.
 */
#define CacheOp_Cache			0x07
#define CacheOp_Op			0x18

#define Cache_LEAF0			0x00
#define Cache_LEAF1			0x01
#define Cache_LEAF2			0x02
#define Cache_LEAF3			0x03
#define Cache_LEAF4			0x04
#define Cache_LEAF5			0x05

#define Index_Invalidate		0x08
#define Index_Writeback_Inv		0x08
#define Hit_Invalidate			0x10
#define Hit_Writeback_Inv		0x10
#define CacheOp_User_Defined		0x18

#define Index_Writeback_Inv_LEAF0	(Cache_LEAF0 | Index_Writeback_Inv)
#define Index_Writeback_Inv_LEAF1	(Cache_LEAF1 | Index_Writeback_Inv)
#define Index_Writeback_Inv_LEAF2	(Cache_LEAF2 | Index_Writeback_Inv)
#define Index_Writeback_Inv_LEAF3	(Cache_LEAF3 | Index_Writeback_Inv)
#define Index_Writeback_Inv_LEAF4	(Cache_LEAF4 | Index_Writeback_Inv)
#define Index_Writeback_Inv_LEAF5	(Cache_LEAF5 | Index_Writeback_Inv)
#define Hit_Writeback_Inv_LEAF0		(Cache_LEAF0 | Hit_Writeback_Inv)
#define Hit_Writeback_Inv_LEAF1		(Cache_LEAF1 | Hit_Writeback_Inv)
#define Hit_Writeback_Inv_LEAF2		(Cache_LEAF2 | Hit_Writeback_Inv)
#define Hit_Writeback_Inv_LEAF3		(Cache_LEAF3 | Hit_Writeback_Inv)
#define Hit_Writeback_Inv_LEAF4		(Cache_LEAF4 | Hit_Writeback_Inv)
#define Hit_Writeback_Inv_LEAF5		(Cache_LEAF5 | Hit_Writeback_Inv)

#endif	/* __ASM_CACHEOPS_H */
