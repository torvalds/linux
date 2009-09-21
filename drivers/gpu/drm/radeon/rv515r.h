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
#ifndef RV515R_H
#define RV515R_H

/* RV515 registers */
#define PCIE_INDEX			0x0030
#define PCIE_DATA			0x0034
#define	MC_IND_INDEX			0x0070
#define		MC_IND_WR_EN				(1 << 24)
#define	MC_IND_DATA			0x0074
#define	RBBM_SOFT_RESET			0x00F0
#define	CONFIG_MEMSIZE			0x00F8
#define HDP_FB_LOCATION			0x0134
#define	CP_CSQ_CNTL			0x0740
#define	CP_CSQ_MODE			0x0744
#define	CP_CSQ_ADDR			0x07F0
#define	CP_CSQ_DATA			0x07F4
#define	CP_CSQ_STAT			0x07F8
#define	CP_CSQ2_STAT			0x07FC
#define	RBBM_STATUS			0x0E40
#define	DST_PIPE_CONFIG			0x170C
#define	WAIT_UNTIL			0x1720
#define		WAIT_2D_IDLE				(1 << 14)
#define		WAIT_3D_IDLE				(1 << 15)
#define		WAIT_2D_IDLECLEAN			(1 << 16)
#define		WAIT_3D_IDLECLEAN			(1 << 17)
#define	ISYNC_CNTL			0x1724
#define		ISYNC_ANY2D_IDLE3D			(1 << 0)
#define		ISYNC_ANY3D_IDLE2D			(1 << 1)
#define		ISYNC_TRIG2D_IDLE3D			(1 << 2)
#define		ISYNC_TRIG3D_IDLE2D			(1 << 3)
#define		ISYNC_WAIT_IDLEGUI			(1 << 4)
#define		ISYNC_CPSCRATCH_IDLEGUI			(1 << 5)
#define	VAP_INDEX_OFFSET		0x208C
#define	VAP_PVS_STATE_FLUSH_REG		0x2284
#define	GB_ENABLE			0x4008
#define	GB_MSPOS0			0x4010
#define		MS_X0_SHIFT				0
#define		MS_Y0_SHIFT				4
#define		MS_X1_SHIFT				8
#define		MS_Y1_SHIFT				12
#define		MS_X2_SHIFT				16
#define		MS_Y2_SHIFT				20
#define		MSBD0_Y_SHIFT				24
#define		MSBD0_X_SHIFT				28
#define	GB_MSPOS1			0x4014
#define		MS_X3_SHIFT				0
#define		MS_Y3_SHIFT				4
#define		MS_X4_SHIFT				8
#define		MS_Y4_SHIFT				12
#define		MS_X5_SHIFT				16
#define		MS_Y5_SHIFT				20
#define		MSBD1_SHIFT				24
#define GB_TILE_CONFIG			0x4018
#define		ENABLE_TILING				(1 << 0)
#define		PIPE_COUNT_MASK				0x0000000E
#define		PIPE_COUNT_SHIFT			1
#define		TILE_SIZE_8				(0 << 4)
#define		TILE_SIZE_16				(1 << 4)
#define		TILE_SIZE_32				(2 << 4)
#define		SUBPIXEL_1_12				(0 << 16)
#define		SUBPIXEL_1_16				(1 << 16)
#define	GB_SELECT			0x401C
#define	GB_AA_CONFIG			0x4020
#define	GB_PIPE_SELECT			0x402C
#define	GA_ENHANCE			0x4274
#define		GA_DEADLOCK_CNTL			(1 << 0)
#define		GA_FASTSYNC_CNTL			(1 << 1)
#define	GA_POLY_MODE			0x4288
#define		FRONT_PTYPE_POINT			(0 << 4)
#define		FRONT_PTYPE_LINE			(1 << 4)
#define		FRONT_PTYPE_TRIANGE			(2 << 4)
#define		BACK_PTYPE_POINT			(0 << 7)
#define		BACK_PTYPE_LINE				(1 << 7)
#define		BACK_PTYPE_TRIANGE			(2 << 7)
#define	GA_ROUND_MODE			0x428C
#define		GEOMETRY_ROUND_TRUNC			(0 << 0)
#define		GEOMETRY_ROUND_NEAREST			(1 << 0)
#define		COLOR_ROUND_TRUNC			(0 << 2)
#define		COLOR_ROUND_NEAREST			(1 << 2)
#define	SU_REG_DEST			0x42C8
#define	RB3D_DSTCACHE_CTLSTAT		0x4E4C
#define		RB3D_DC_FLUSH				(2 << 0)
#define		RB3D_DC_FREE				(2 << 2)
#define		RB3D_DC_FINISH				(1 << 4)
#define ZB_ZCACHE_CTLSTAT		0x4F18
#define		ZC_FLUSH				(1 << 0)
#define		ZC_FREE					(1 << 1)
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
#define D2MODE_PRIORITY_A_CNT		0x6D48
#define D2MODE_PRIORITY_B_CNT		0x6D4C

/* ix[MC] registers */
#define MC_FB_LOCATION			0x01
#define		MC_FB_START_MASK			0x0000FFFF
#define		MC_FB_START_SHIFT			0
#define		MC_FB_TOP_MASK				0xFFFF0000
#define		MC_FB_TOP_SHIFT				16
#define MC_AGP_LOCATION			0x02
#define		MC_AGP_START_MASK			0x0000FFFF
#define		MC_AGP_START_SHIFT			0
#define		MC_AGP_TOP_MASK				0xFFFF0000
#define		MC_AGP_TOP_SHIFT			16
#define MC_AGP_BASE			0x03
#define MC_AGP_BASE_2			0x04
#define	MC_CNTL				0x5
#define		MEM_NUM_CHANNELS_MASK			0x00000003
#define	MC_STATUS			0x08
#define		MC_STATUS_IDLE				(1 << 4)
#define	MC_MISC_LAT_TIMER		0x09
#define		MC_CPR_INIT_LAT_MASK			0x0000000F
#define		MC_VF_INIT_LAT_MASK			0x000000F0
#define		MC_DISP0R_INIT_LAT_MASK			0x00000F00
#define		MC_DISP0R_INIT_LAT_SHIFT		8
#define		MC_DISP1R_INIT_LAT_MASK			0x0000F000
#define		MC_DISP1R_INIT_LAT_SHIFT		12
#define		MC_FIXED_INIT_LAT_MASK			0x000F0000
#define		MC_E2R_INIT_LAT_MASK			0x00F00000
#define		SAME_PAGE_PRIO_MASK			0x0F000000
#define		MC_GLOBW_INIT_LAT_MASK			0xF0000000


#endif

