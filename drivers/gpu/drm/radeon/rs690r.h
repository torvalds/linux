/*
 * Copyright 2008 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 * Copyright 2009 Jerome Glisse.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Dave Airlie
 *          Alex Deucher
 *          Jerome Glisse
 */
#ifndef RS690R_H
#define RS690R_H

/* RS690/RS740 registers */
#define MC_INDEX			0x0078
#	define MC_INDEX_MASK			0x1FF
#	define MC_INDEX_WR_EN			(1 << 9)
#	define MC_INDEX_WR_ACK			0x7F
#define MC_DATA				0x007C
#define HDP_FB_LOCATION			0x0134
#define DC_LB_MEMORY_SPLIT		0x6520
#define		DC_LB_MEMORY_SPLIT_MASK			0x00000003
#define		DC_LB_MEMORY_SPLIT_SHIFT		0
#define		DC_LB_MEMORY_SPLIT_D1HALF_D2HALF	0
#define		DC_LB_MEMORY_SPLIT_D1_3Q_D2_1Q		1
#define		DC_LB_MEMORY_SPLIT_D1_ONLY		2
#define		DC_LB_MEMORY_SPLIT_D1_1Q_D2_3Q		3
#define		DC_LB_MEMORY_SPLIT_SHIFT_MODE		(1 << 2)
#define		DC_LB_DISP1_END_ADR_SHIFT		4
#define		DC_LB_DISP1_END_ADR_MASK		0x00007FF0
#define D1MODE_PRIORITY_A_CNT		0x6548
#define		MODE_PRIORITY_MARK_MASK			0x00007FFF
#define		MODE_PRIORITY_OFF			(1 << 16)
#define		MODE_PRIORITY_ALWAYS_ON			(1 << 20)
#define		MODE_PRIORITY_FORCE_MASK		(1 << 24)
#define D1MODE_PRIORITY_B_CNT		0x654C
#define LB_MAX_REQ_OUTSTANDING		0x6D58
#define		LB_D1_MAX_REQ_OUTSTANDING_MASK		0x0000000F
#define		LB_D1_MAX_REQ_OUTSTANDING_SHIFT		0
#define		LB_D2_MAX_REQ_OUTSTANDING_MASK		0x000F0000
#define		LB_D2_MAX_REQ_OUTSTANDING_SHIFT		16
#define DCP_CONTROL			0x6C9C
#define D2MODE_PRIORITY_A_CNT		0x6D48
#define D2MODE_PRIORITY_B_CNT		0x6D4C

/* MC indirect registers */
#define MC_STATUS_IDLE				(1 << 0)
#define MC_MISC_CNTL			0x18
#define		DISABLE_GTW			(1 << 1)
#define		GART_INDEX_REG_EN		(1 << 12)
#define		BLOCK_GFX_D3_EN			(1 << 14)
#define GART_FEATURE_ID			0x2B
#define		HANG_EN				(1 << 11)
#define		TLB_ENABLE			(1 << 18)
#define		P2P_ENABLE			(1 << 19)
#define		GTW_LAC_EN			(1 << 25)
#define		LEVEL2_GART			(0 << 30)
#define		LEVEL1_GART			(1 << 30)
#define		PDC_EN				(1 << 31)
#define GART_BASE			0x2C
#define GART_CACHE_CNTRL		0x2E
#	define GART_CACHE_INVALIDATE		(1 << 0)
#define MC_STATUS			0x90
#define MCCFG_FB_LOCATION		0x100
#define		MC_FB_START_MASK		0x0000FFFF
#define		MC_FB_START_SHIFT		0
#define		MC_FB_TOP_MASK			0xFFFF0000
#define		MC_FB_TOP_SHIFT			16
#define MCCFG_AGP_LOCATION		0x101
#define		MC_AGP_START_MASK		0x0000FFFF
#define		MC_AGP_START_SHIFT		0
#define		MC_AGP_TOP_MASK			0xFFFF0000
#define		MC_AGP_TOP_SHIFT		16
#define MCCFG_AGP_BASE			0x102
#define MCCFG_AGP_BASE_2		0x103
#define MC_INIT_MISC_LAT_TIMER		0x104
#define		MC_DISP0R_INIT_LAT_SHIFT	8
#define		MC_DISP0R_INIT_LAT_MASK		0x00000F00
#define		MC_DISP1R_INIT_LAT_SHIFT	12
#define		MC_DISP1R_INIT_LAT_MASK		0x0000F000

#endif
