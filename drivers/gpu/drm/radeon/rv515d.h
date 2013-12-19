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
#ifndef __RV515D_H__
#define __RV515D_H__

/*
 * RV515 registers
 */
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


/*
 * PM4 packet
 */
#define CP_PACKET0			0x00000000
#define		PACKET0_BASE_INDEX_SHIFT	0
#define		PACKET0_BASE_INDEX_MASK		(0x1ffff << 0)
#define		PACKET0_COUNT_SHIFT		16
#define		PACKET0_COUNT_MASK		(0x3fff << 16)
#define CP_PACKET1			0x40000000
#define CP_PACKET2			0x80000000
#define		PACKET2_PAD_SHIFT		0
#define		PACKET2_PAD_MASK		(0x3fffffff << 0)
#define CP_PACKET3			0xC0000000
#define		PACKET3_IT_OPCODE_SHIFT		8
#define		PACKET3_IT_OPCODE_MASK		(0xff << 8)
#define		PACKET3_COUNT_SHIFT		16
#define		PACKET3_COUNT_MASK		(0x3fff << 16)
/* PACKET3 op code */
#define		PACKET3_NOP			0x10
#define		PACKET3_3D_DRAW_VBUF		0x28
#define		PACKET3_3D_DRAW_IMMD		0x29
#define		PACKET3_3D_DRAW_INDX		0x2A
#define		PACKET3_3D_LOAD_VBPNTR		0x2F
#define		PACKET3_INDX_BUFFER		0x33
#define		PACKET3_3D_DRAW_VBUF_2		0x34
#define		PACKET3_3D_DRAW_IMMD_2		0x35
#define		PACKET3_3D_DRAW_INDX_2		0x36
#define		PACKET3_BITBLT_MULTI		0x9B

#define PACKET0(reg, n)	(CP_PACKET0 |					\
			 REG_SET(PACKET0_BASE_INDEX, (reg) >> 2) |	\
			 REG_SET(PACKET0_COUNT, (n)))
#define PACKET2(v)	(CP_PACKET2 | REG_SET(PACKET2_PAD, (v)))
#define PACKET3(op, n)	(CP_PACKET3 |					\
			 REG_SET(PACKET3_IT_OPCODE, (op)) |		\
			 REG_SET(PACKET3_COUNT, (n)))

/* Registers */
#define R_0000F0_RBBM_SOFT_RESET                     0x0000F0
#define   S_0000F0_SOFT_RESET_CP(x)                    (((x) & 0x1) << 0)
#define   G_0000F0_SOFT_RESET_CP(x)                    (((x) >> 0) & 0x1)
#define   C_0000F0_SOFT_RESET_CP                       0xFFFFFFFE
#define   S_0000F0_SOFT_RESET_HI(x)                    (((x) & 0x1) << 1)
#define   G_0000F0_SOFT_RESET_HI(x)                    (((x) >> 1) & 0x1)
#define   C_0000F0_SOFT_RESET_HI                       0xFFFFFFFD
#define   S_0000F0_SOFT_RESET_VAP(x)                   (((x) & 0x1) << 2)
#define   G_0000F0_SOFT_RESET_VAP(x)                   (((x) >> 2) & 0x1)
#define   C_0000F0_SOFT_RESET_VAP                      0xFFFFFFFB
#define   S_0000F0_SOFT_RESET_RE(x)                    (((x) & 0x1) << 3)
#define   G_0000F0_SOFT_RESET_RE(x)                    (((x) >> 3) & 0x1)
#define   C_0000F0_SOFT_RESET_RE                       0xFFFFFFF7
#define   S_0000F0_SOFT_RESET_PP(x)                    (((x) & 0x1) << 4)
#define   G_0000F0_SOFT_RESET_PP(x)                    (((x) >> 4) & 0x1)
#define   C_0000F0_SOFT_RESET_PP                       0xFFFFFFEF
#define   S_0000F0_SOFT_RESET_E2(x)                    (((x) & 0x1) << 5)
#define   G_0000F0_SOFT_RESET_E2(x)                    (((x) >> 5) & 0x1)
#define   C_0000F0_SOFT_RESET_E2                       0xFFFFFFDF
#define   S_0000F0_SOFT_RESET_RB(x)                    (((x) & 0x1) << 6)
#define   G_0000F0_SOFT_RESET_RB(x)                    (((x) >> 6) & 0x1)
#define   C_0000F0_SOFT_RESET_RB                       0xFFFFFFBF
#define   S_0000F0_SOFT_RESET_HDP(x)                   (((x) & 0x1) << 7)
#define   G_0000F0_SOFT_RESET_HDP(x)                   (((x) >> 7) & 0x1)
#define   C_0000F0_SOFT_RESET_HDP                      0xFFFFFF7F
#define   S_0000F0_SOFT_RESET_MC(x)                    (((x) & 0x1) << 8)
#define   G_0000F0_SOFT_RESET_MC(x)                    (((x) >> 8) & 0x1)
#define   C_0000F0_SOFT_RESET_MC                       0xFFFFFEFF
#define   S_0000F0_SOFT_RESET_AIC(x)                   (((x) & 0x1) << 9)
#define   G_0000F0_SOFT_RESET_AIC(x)                   (((x) >> 9) & 0x1)
#define   C_0000F0_SOFT_RESET_AIC                      0xFFFFFDFF
#define   S_0000F0_SOFT_RESET_VIP(x)                   (((x) & 0x1) << 10)
#define   G_0000F0_SOFT_RESET_VIP(x)                   (((x) >> 10) & 0x1)
#define   C_0000F0_SOFT_RESET_VIP                      0xFFFFFBFF
#define   S_0000F0_SOFT_RESET_DISP(x)                  (((x) & 0x1) << 11)
#define   G_0000F0_SOFT_RESET_DISP(x)                  (((x) >> 11) & 0x1)
#define   C_0000F0_SOFT_RESET_DISP                     0xFFFFF7FF
#define   S_0000F0_SOFT_RESET_CG(x)                    (((x) & 0x1) << 12)
#define   G_0000F0_SOFT_RESET_CG(x)                    (((x) >> 12) & 0x1)
#define   C_0000F0_SOFT_RESET_CG                       0xFFFFEFFF
#define   S_0000F0_SOFT_RESET_GA(x)                    (((x) & 0x1) << 13)
#define   G_0000F0_SOFT_RESET_GA(x)                    (((x) >> 13) & 0x1)
#define   C_0000F0_SOFT_RESET_GA                       0xFFFFDFFF
#define   S_0000F0_SOFT_RESET_IDCT(x)                  (((x) & 0x1) << 14)
#define   G_0000F0_SOFT_RESET_IDCT(x)                  (((x) >> 14) & 0x1)
#define   C_0000F0_SOFT_RESET_IDCT                     0xFFFFBFFF
#define R_0000F8_CONFIG_MEMSIZE                      0x0000F8
#define   S_0000F8_CONFIG_MEMSIZE(x)                   (((x) & 0xFFFFFFFF) << 0)
#define   G_0000F8_CONFIG_MEMSIZE(x)                   (((x) >> 0) & 0xFFFFFFFF)
#define   C_0000F8_CONFIG_MEMSIZE                      0x00000000
#define R_000134_HDP_FB_LOCATION                     0x000134
#define   S_000134_HDP_FB_START(x)                     (((x) & 0xFFFF) << 0)
#define   G_000134_HDP_FB_START(x)                     (((x) >> 0) & 0xFFFF)
#define   C_000134_HDP_FB_START                        0xFFFF0000
#define R_000300_VGA_RENDER_CONTROL                  0x000300
#define   S_000300_VGA_BLINK_RATE(x)                   (((x) & 0x1F) << 0)
#define   G_000300_VGA_BLINK_RATE(x)                   (((x) >> 0) & 0x1F)
#define   C_000300_VGA_BLINK_RATE                      0xFFFFFFE0
#define   S_000300_VGA_BLINK_MODE(x)                   (((x) & 0x3) << 5)
#define   G_000300_VGA_BLINK_MODE(x)                   (((x) >> 5) & 0x3)
#define   C_000300_VGA_BLINK_MODE                      0xFFFFFF9F
#define   S_000300_VGA_CURSOR_BLINK_INVERT(x)          (((x) & 0x1) << 7)
#define   G_000300_VGA_CURSOR_BLINK_INVERT(x)          (((x) >> 7) & 0x1)
#define   C_000300_VGA_CURSOR_BLINK_INVERT             0xFFFFFF7F
#define   S_000300_VGA_EXTD_ADDR_COUNT_ENABLE(x)       (((x) & 0x1) << 8)
#define   G_000300_VGA_EXTD_ADDR_COUNT_ENABLE(x)       (((x) >> 8) & 0x1)
#define   C_000300_VGA_EXTD_ADDR_COUNT_ENABLE          0xFFFFFEFF
#define   S_000300_VGA_VSTATUS_CNTL(x)                 (((x) & 0x3) << 16)
#define   G_000300_VGA_VSTATUS_CNTL(x)                 (((x) >> 16) & 0x3)
#define   C_000300_VGA_VSTATUS_CNTL                    0xFFFCFFFF
#define   S_000300_VGA_LOCK_8DOT(x)                    (((x) & 0x1) << 24)
#define   G_000300_VGA_LOCK_8DOT(x)                    (((x) >> 24) & 0x1)
#define   C_000300_VGA_LOCK_8DOT                       0xFEFFFFFF
#define   S_000300_VGAREG_LINECMP_COMPATIBILITY_SEL(x) (((x) & 0x1) << 25)
#define   G_000300_VGAREG_LINECMP_COMPATIBILITY_SEL(x) (((x) >> 25) & 0x1)
#define   C_000300_VGAREG_LINECMP_COMPATIBILITY_SEL    0xFDFFFFFF
#define R_000310_VGA_MEMORY_BASE_ADDRESS             0x000310
#define   S_000310_VGA_MEMORY_BASE_ADDRESS(x)          (((x) & 0xFFFFFFFF) << 0)
#define   G_000310_VGA_MEMORY_BASE_ADDRESS(x)          (((x) >> 0) & 0xFFFFFFFF)
#define   C_000310_VGA_MEMORY_BASE_ADDRESS             0x00000000
#define R_000328_VGA_HDP_CONTROL                     0x000328
#define   S_000328_VGA_MEM_PAGE_SELECT_EN(x)           (((x) & 0x1) << 0)
#define   G_000328_VGA_MEM_PAGE_SELECT_EN(x)           (((x) >> 0) & 0x1)
#define   C_000328_VGA_MEM_PAGE_SELECT_EN              0xFFFFFFFE
#define   S_000328_VGA_RBBM_LOCK_DISABLE(x)            (((x) & 0x1) << 8)
#define   G_000328_VGA_RBBM_LOCK_DISABLE(x)            (((x) >> 8) & 0x1)
#define   C_000328_VGA_RBBM_LOCK_DISABLE               0xFFFFFEFF
#define   S_000328_VGA_SOFT_RESET(x)                   (((x) & 0x1) << 16)
#define   G_000328_VGA_SOFT_RESET(x)                   (((x) >> 16) & 0x1)
#define   C_000328_VGA_SOFT_RESET                      0xFFFEFFFF
#define   S_000328_VGA_TEST_RESET_CONTROL(x)           (((x) & 0x1) << 24)
#define   G_000328_VGA_TEST_RESET_CONTROL(x)           (((x) >> 24) & 0x1)
#define   C_000328_VGA_TEST_RESET_CONTROL              0xFEFFFFFF
#define R_000330_D1VGA_CONTROL                       0x000330
#define   S_000330_D1VGA_MODE_ENABLE(x)                (((x) & 0x1) << 0)
#define   G_000330_D1VGA_MODE_ENABLE(x)                (((x) >> 0) & 0x1)
#define   C_000330_D1VGA_MODE_ENABLE                   0xFFFFFFFE
#define   S_000330_D1VGA_TIMING_SELECT(x)              (((x) & 0x1) << 8)
#define   G_000330_D1VGA_TIMING_SELECT(x)              (((x) >> 8) & 0x1)
#define   C_000330_D1VGA_TIMING_SELECT                 0xFFFFFEFF
#define   S_000330_D1VGA_SYNC_POLARITY_SELECT(x)       (((x) & 0x1) << 9)
#define   G_000330_D1VGA_SYNC_POLARITY_SELECT(x)       (((x) >> 9) & 0x1)
#define   C_000330_D1VGA_SYNC_POLARITY_SELECT          0xFFFFFDFF
#define   S_000330_D1VGA_OVERSCAN_TIMING_SELECT(x)     (((x) & 0x1) << 10)
#define   G_000330_D1VGA_OVERSCAN_TIMING_SELECT(x)     (((x) >> 10) & 0x1)
#define   C_000330_D1VGA_OVERSCAN_TIMING_SELECT        0xFFFFFBFF
#define   S_000330_D1VGA_OVERSCAN_COLOR_EN(x)          (((x) & 0x1) << 16)
#define   G_000330_D1VGA_OVERSCAN_COLOR_EN(x)          (((x) >> 16) & 0x1)
#define   C_000330_D1VGA_OVERSCAN_COLOR_EN             0xFFFEFFFF
#define   S_000330_D1VGA_ROTATE(x)                     (((x) & 0x3) << 24)
#define   G_000330_D1VGA_ROTATE(x)                     (((x) >> 24) & 0x3)
#define   C_000330_D1VGA_ROTATE                        0xFCFFFFFF
#define R_000338_D2VGA_CONTROL                       0x000338
#define   S_000338_D2VGA_MODE_ENABLE(x)                (((x) & 0x1) << 0)
#define   G_000338_D2VGA_MODE_ENABLE(x)                (((x) >> 0) & 0x1)
#define   C_000338_D2VGA_MODE_ENABLE                   0xFFFFFFFE
#define   S_000338_D2VGA_TIMING_SELECT(x)              (((x) & 0x1) << 8)
#define   G_000338_D2VGA_TIMING_SELECT(x)              (((x) >> 8) & 0x1)
#define   C_000338_D2VGA_TIMING_SELECT                 0xFFFFFEFF
#define   S_000338_D2VGA_SYNC_POLARITY_SELECT(x)       (((x) & 0x1) << 9)
#define   G_000338_D2VGA_SYNC_POLARITY_SELECT(x)       (((x) >> 9) & 0x1)
#define   C_000338_D2VGA_SYNC_POLARITY_SELECT          0xFFFFFDFF
#define   S_000338_D2VGA_OVERSCAN_TIMING_SELECT(x)     (((x) & 0x1) << 10)
#define   G_000338_D2VGA_OVERSCAN_TIMING_SELECT(x)     (((x) >> 10) & 0x1)
#define   C_000338_D2VGA_OVERSCAN_TIMING_SELECT        0xFFFFFBFF
#define   S_000338_D2VGA_OVERSCAN_COLOR_EN(x)          (((x) & 0x1) << 16)
#define   G_000338_D2VGA_OVERSCAN_COLOR_EN(x)          (((x) >> 16) & 0x1)
#define   C_000338_D2VGA_OVERSCAN_COLOR_EN             0xFFFEFFFF
#define   S_000338_D2VGA_ROTATE(x)                     (((x) & 0x3) << 24)
#define   G_000338_D2VGA_ROTATE(x)                     (((x) >> 24) & 0x3)
#define   C_000338_D2VGA_ROTATE                        0xFCFFFFFF
#define R_0007C0_CP_STAT                             0x0007C0
#define   S_0007C0_MRU_BUSY(x)                         (((x) & 0x1) << 0)
#define   G_0007C0_MRU_BUSY(x)                         (((x) >> 0) & 0x1)
#define   C_0007C0_MRU_BUSY                            0xFFFFFFFE
#define   S_0007C0_MWU_BUSY(x)                         (((x) & 0x1) << 1)
#define   G_0007C0_MWU_BUSY(x)                         (((x) >> 1) & 0x1)
#define   C_0007C0_MWU_BUSY                            0xFFFFFFFD
#define   S_0007C0_RSIU_BUSY(x)                        (((x) & 0x1) << 2)
#define   G_0007C0_RSIU_BUSY(x)                        (((x) >> 2) & 0x1)
#define   C_0007C0_RSIU_BUSY                           0xFFFFFFFB
#define   S_0007C0_RCIU_BUSY(x)                        (((x) & 0x1) << 3)
#define   G_0007C0_RCIU_BUSY(x)                        (((x) >> 3) & 0x1)
#define   C_0007C0_RCIU_BUSY                           0xFFFFFFF7
#define   S_0007C0_CSF_PRIMARY_BUSY(x)                 (((x) & 0x1) << 9)
#define   G_0007C0_CSF_PRIMARY_BUSY(x)                 (((x) >> 9) & 0x1)
#define   C_0007C0_CSF_PRIMARY_BUSY                    0xFFFFFDFF
#define   S_0007C0_CSF_INDIRECT_BUSY(x)                (((x) & 0x1) << 10)
#define   G_0007C0_CSF_INDIRECT_BUSY(x)                (((x) >> 10) & 0x1)
#define   C_0007C0_CSF_INDIRECT_BUSY                   0xFFFFFBFF
#define   S_0007C0_CSQ_PRIMARY_BUSY(x)                 (((x) & 0x1) << 11)
#define   G_0007C0_CSQ_PRIMARY_BUSY(x)                 (((x) >> 11) & 0x1)
#define   C_0007C0_CSQ_PRIMARY_BUSY                    0xFFFFF7FF
#define   S_0007C0_CSQ_INDIRECT_BUSY(x)                (((x) & 0x1) << 12)
#define   G_0007C0_CSQ_INDIRECT_BUSY(x)                (((x) >> 12) & 0x1)
#define   C_0007C0_CSQ_INDIRECT_BUSY                   0xFFFFEFFF
#define   S_0007C0_CSI_BUSY(x)                         (((x) & 0x1) << 13)
#define   G_0007C0_CSI_BUSY(x)                         (((x) >> 13) & 0x1)
#define   C_0007C0_CSI_BUSY                            0xFFFFDFFF
#define   S_0007C0_CSF_INDIRECT2_BUSY(x)               (((x) & 0x1) << 14)
#define   G_0007C0_CSF_INDIRECT2_BUSY(x)               (((x) >> 14) & 0x1)
#define   C_0007C0_CSF_INDIRECT2_BUSY                  0xFFFFBFFF
#define   S_0007C0_CSQ_INDIRECT2_BUSY(x)               (((x) & 0x1) << 15)
#define   G_0007C0_CSQ_INDIRECT2_BUSY(x)               (((x) >> 15) & 0x1)
#define   C_0007C0_CSQ_INDIRECT2_BUSY                  0xFFFF7FFF
#define   S_0007C0_GUIDMA_BUSY(x)                      (((x) & 0x1) << 28)
#define   G_0007C0_GUIDMA_BUSY(x)                      (((x) >> 28) & 0x1)
#define   C_0007C0_GUIDMA_BUSY                         0xEFFFFFFF
#define   S_0007C0_VIDDMA_BUSY(x)                      (((x) & 0x1) << 29)
#define   G_0007C0_VIDDMA_BUSY(x)                      (((x) >> 29) & 0x1)
#define   C_0007C0_VIDDMA_BUSY                         0xDFFFFFFF
#define   S_0007C0_CMDSTRM_BUSY(x)                     (((x) & 0x1) << 30)
#define   G_0007C0_CMDSTRM_BUSY(x)                     (((x) >> 30) & 0x1)
#define   C_0007C0_CMDSTRM_BUSY                        0xBFFFFFFF
#define   S_0007C0_CP_BUSY(x)                          (((x) & 0x1) << 31)
#define   G_0007C0_CP_BUSY(x)                          (((x) >> 31) & 0x1)
#define   C_0007C0_CP_BUSY                             0x7FFFFFFF
#define R_000E40_RBBM_STATUS                         0x000E40
#define   S_000E40_CMDFIFO_AVAIL(x)                    (((x) & 0x7F) << 0)
#define   G_000E40_CMDFIFO_AVAIL(x)                    (((x) >> 0) & 0x7F)
#define   C_000E40_CMDFIFO_AVAIL                       0xFFFFFF80
#define   S_000E40_HIRQ_ON_RBB(x)                      (((x) & 0x1) << 8)
#define   G_000E40_HIRQ_ON_RBB(x)                      (((x) >> 8) & 0x1)
#define   C_000E40_HIRQ_ON_RBB                         0xFFFFFEFF
#define   S_000E40_CPRQ_ON_RBB(x)                      (((x) & 0x1) << 9)
#define   G_000E40_CPRQ_ON_RBB(x)                      (((x) >> 9) & 0x1)
#define   C_000E40_CPRQ_ON_RBB                         0xFFFFFDFF
#define   S_000E40_CFRQ_ON_RBB(x)                      (((x) & 0x1) << 10)
#define   G_000E40_CFRQ_ON_RBB(x)                      (((x) >> 10) & 0x1)
#define   C_000E40_CFRQ_ON_RBB                         0xFFFFFBFF
#define   S_000E40_HIRQ_IN_RTBUF(x)                    (((x) & 0x1) << 11)
#define   G_000E40_HIRQ_IN_RTBUF(x)                    (((x) >> 11) & 0x1)
#define   C_000E40_HIRQ_IN_RTBUF                       0xFFFFF7FF
#define   S_000E40_CPRQ_IN_RTBUF(x)                    (((x) & 0x1) << 12)
#define   G_000E40_CPRQ_IN_RTBUF(x)                    (((x) >> 12) & 0x1)
#define   C_000E40_CPRQ_IN_RTBUF                       0xFFFFEFFF
#define   S_000E40_CFRQ_IN_RTBUF(x)                    (((x) & 0x1) << 13)
#define   G_000E40_CFRQ_IN_RTBUF(x)                    (((x) >> 13) & 0x1)
#define   C_000E40_CFRQ_IN_RTBUF                       0xFFFFDFFF
#define   S_000E40_CF_PIPE_BUSY(x)                     (((x) & 0x1) << 14)
#define   G_000E40_CF_PIPE_BUSY(x)                     (((x) >> 14) & 0x1)
#define   C_000E40_CF_PIPE_BUSY                        0xFFFFBFFF
#define   S_000E40_ENG_EV_BUSY(x)                      (((x) & 0x1) << 15)
#define   G_000E40_ENG_EV_BUSY(x)                      (((x) >> 15) & 0x1)
#define   C_000E40_ENG_EV_BUSY                         0xFFFF7FFF
#define   S_000E40_CP_CMDSTRM_BUSY(x)                  (((x) & 0x1) << 16)
#define   G_000E40_CP_CMDSTRM_BUSY(x)                  (((x) >> 16) & 0x1)
#define   C_000E40_CP_CMDSTRM_BUSY                     0xFFFEFFFF
#define   S_000E40_E2_BUSY(x)                          (((x) & 0x1) << 17)
#define   G_000E40_E2_BUSY(x)                          (((x) >> 17) & 0x1)
#define   C_000E40_E2_BUSY                             0xFFFDFFFF
#define   S_000E40_RB2D_BUSY(x)                        (((x) & 0x1) << 18)
#define   G_000E40_RB2D_BUSY(x)                        (((x) >> 18) & 0x1)
#define   C_000E40_RB2D_BUSY                           0xFFFBFFFF
#define   S_000E40_RB3D_BUSY(x)                        (((x) & 0x1) << 19)
#define   G_000E40_RB3D_BUSY(x)                        (((x) >> 19) & 0x1)
#define   C_000E40_RB3D_BUSY                           0xFFF7FFFF
#define   S_000E40_VAP_BUSY(x)                         (((x) & 0x1) << 20)
#define   G_000E40_VAP_BUSY(x)                         (((x) >> 20) & 0x1)
#define   C_000E40_VAP_BUSY                            0xFFEFFFFF
#define   S_000E40_RE_BUSY(x)                          (((x) & 0x1) << 21)
#define   G_000E40_RE_BUSY(x)                          (((x) >> 21) & 0x1)
#define   C_000E40_RE_BUSY                             0xFFDFFFFF
#define   S_000E40_TAM_BUSY(x)                         (((x) & 0x1) << 22)
#define   G_000E40_TAM_BUSY(x)                         (((x) >> 22) & 0x1)
#define   C_000E40_TAM_BUSY                            0xFFBFFFFF
#define   S_000E40_TDM_BUSY(x)                         (((x) & 0x1) << 23)
#define   G_000E40_TDM_BUSY(x)                         (((x) >> 23) & 0x1)
#define   C_000E40_TDM_BUSY                            0xFF7FFFFF
#define   S_000E40_PB_BUSY(x)                          (((x) & 0x1) << 24)
#define   G_000E40_PB_BUSY(x)                          (((x) >> 24) & 0x1)
#define   C_000E40_PB_BUSY                             0xFEFFFFFF
#define   S_000E40_TIM_BUSY(x)                         (((x) & 0x1) << 25)
#define   G_000E40_TIM_BUSY(x)                         (((x) >> 25) & 0x1)
#define   C_000E40_TIM_BUSY                            0xFDFFFFFF
#define   S_000E40_GA_BUSY(x)                          (((x) & 0x1) << 26)
#define   G_000E40_GA_BUSY(x)                          (((x) >> 26) & 0x1)
#define   C_000E40_GA_BUSY                             0xFBFFFFFF
#define   S_000E40_CBA2D_BUSY(x)                       (((x) & 0x1) << 27)
#define   G_000E40_CBA2D_BUSY(x)                       (((x) >> 27) & 0x1)
#define   C_000E40_CBA2D_BUSY                          0xF7FFFFFF
#define   S_000E40_RBBM_HIBUSY(x)                      (((x) & 0x1) << 28)
#define   G_000E40_RBBM_HIBUSY(x)                      (((x) >> 28) & 0x1)
#define   C_000E40_RBBM_HIBUSY                         0xEFFFFFFF
#define   S_000E40_SKID_CFBUSY(x)                      (((x) & 0x1) << 29)
#define   G_000E40_SKID_CFBUSY(x)                      (((x) >> 29) & 0x1)
#define   C_000E40_SKID_CFBUSY                         0xDFFFFFFF
#define   S_000E40_VAP_VF_BUSY(x)                      (((x) & 0x1) << 30)
#define   G_000E40_VAP_VF_BUSY(x)                      (((x) >> 30) & 0x1)
#define   C_000E40_VAP_VF_BUSY                         0xBFFFFFFF
#define   S_000E40_GUI_ACTIVE(x)                       (((x) & 0x1) << 31)
#define   G_000E40_GUI_ACTIVE(x)                       (((x) >> 31) & 0x1)
#define   C_000E40_GUI_ACTIVE                          0x7FFFFFFF
#define R_006080_D1CRTC_CONTROL                      0x006080
#define   S_006080_D1CRTC_MASTER_EN(x)                 (((x) & 0x1) << 0)
#define   G_006080_D1CRTC_MASTER_EN(x)                 (((x) >> 0) & 0x1)
#define   C_006080_D1CRTC_MASTER_EN                    0xFFFFFFFE
#define   S_006080_D1CRTC_SYNC_RESET_SEL(x)            (((x) & 0x1) << 4)
#define   G_006080_D1CRTC_SYNC_RESET_SEL(x)            (((x) >> 4) & 0x1)
#define   C_006080_D1CRTC_SYNC_RESET_SEL               0xFFFFFFEF
#define   S_006080_D1CRTC_DISABLE_POINT_CNTL(x)        (((x) & 0x3) << 8)
#define   G_006080_D1CRTC_DISABLE_POINT_CNTL(x)        (((x) >> 8) & 0x3)
#define   C_006080_D1CRTC_DISABLE_POINT_CNTL           0xFFFFFCFF
#define   S_006080_D1CRTC_CURRENT_MASTER_EN_STATE(x)   (((x) & 0x1) << 16)
#define   G_006080_D1CRTC_CURRENT_MASTER_EN_STATE(x)   (((x) >> 16) & 0x1)
#define   C_006080_D1CRTC_CURRENT_MASTER_EN_STATE      0xFFFEFFFF
#define   S_006080_D1CRTC_DISP_READ_REQUEST_DISABLE(x) (((x) & 0x1) << 24)
#define   G_006080_D1CRTC_DISP_READ_REQUEST_DISABLE(x) (((x) >> 24) & 0x1)
#define   C_006080_D1CRTC_DISP_READ_REQUEST_DISABLE    0xFEFFFFFF
#define R_0060E8_D1CRTC_UPDATE_LOCK                  0x0060E8
#define   S_0060E8_D1CRTC_UPDATE_LOCK(x)               (((x) & 0x1) << 0)
#define   G_0060E8_D1CRTC_UPDATE_LOCK(x)               (((x) >> 0) & 0x1)
#define   C_0060E8_D1CRTC_UPDATE_LOCK                  0xFFFFFFFE
#define R_006110_D1GRPH_PRIMARY_SURFACE_ADDRESS      0x006110
#define   S_006110_D1GRPH_PRIMARY_SURFACE_ADDRESS(x)   (((x) & 0xFFFFFFFF) << 0)
#define   G_006110_D1GRPH_PRIMARY_SURFACE_ADDRESS(x)   (((x) >> 0) & 0xFFFFFFFF)
#define   C_006110_D1GRPH_PRIMARY_SURFACE_ADDRESS      0x00000000
#define R_006118_D1GRPH_SECONDARY_SURFACE_ADDRESS    0x006118
#define   S_006118_D1GRPH_SECONDARY_SURFACE_ADDRESS(x) (((x) & 0xFFFFFFFF) << 0)
#define   G_006118_D1GRPH_SECONDARY_SURFACE_ADDRESS(x) (((x) >> 0) & 0xFFFFFFFF)
#define   C_006118_D1GRPH_SECONDARY_SURFACE_ADDRESS    0x00000000
#define R_006880_D2CRTC_CONTROL                      0x006880
#define   S_006880_D2CRTC_MASTER_EN(x)                 (((x) & 0x1) << 0)
#define   G_006880_D2CRTC_MASTER_EN(x)                 (((x) >> 0) & 0x1)
#define   C_006880_D2CRTC_MASTER_EN                    0xFFFFFFFE
#define   S_006880_D2CRTC_SYNC_RESET_SEL(x)            (((x) & 0x1) << 4)
#define   G_006880_D2CRTC_SYNC_RESET_SEL(x)            (((x) >> 4) & 0x1)
#define   C_006880_D2CRTC_SYNC_RESET_SEL               0xFFFFFFEF
#define   S_006880_D2CRTC_DISABLE_POINT_CNTL(x)        (((x) & 0x3) << 8)
#define   G_006880_D2CRTC_DISABLE_POINT_CNTL(x)        (((x) >> 8) & 0x3)
#define   C_006880_D2CRTC_DISABLE_POINT_CNTL           0xFFFFFCFF
#define   S_006880_D2CRTC_CURRENT_MASTER_EN_STATE(x)   (((x) & 0x1) << 16)
#define   G_006880_D2CRTC_CURRENT_MASTER_EN_STATE(x)   (((x) >> 16) & 0x1)
#define   C_006880_D2CRTC_CURRENT_MASTER_EN_STATE      0xFFFEFFFF
#define   S_006880_D2CRTC_DISP_READ_REQUEST_DISABLE(x) (((x) & 0x1) << 24)
#define   G_006880_D2CRTC_DISP_READ_REQUEST_DISABLE(x) (((x) >> 24) & 0x1)
#define   C_006880_D2CRTC_DISP_READ_REQUEST_DISABLE    0xFEFFFFFF
#define R_0068E8_D2CRTC_UPDATE_LOCK                  0x0068E8
#define   S_0068E8_D2CRTC_UPDATE_LOCK(x)               (((x) & 0x1) << 0)
#define   G_0068E8_D2CRTC_UPDATE_LOCK(x)               (((x) >> 0) & 0x1)
#define   C_0068E8_D2CRTC_UPDATE_LOCK                  0xFFFFFFFE
#define R_006910_D2GRPH_PRIMARY_SURFACE_ADDRESS      0x006910
#define   S_006910_D2GRPH_PRIMARY_SURFACE_ADDRESS(x)   (((x) & 0xFFFFFFFF) << 0)
#define   G_006910_D2GRPH_PRIMARY_SURFACE_ADDRESS(x)   (((x) >> 0) & 0xFFFFFFFF)
#define   C_006910_D2GRPH_PRIMARY_SURFACE_ADDRESS      0x00000000
#define R_006918_D2GRPH_SECONDARY_SURFACE_ADDRESS    0x006918
#define   S_006918_D2GRPH_SECONDARY_SURFACE_ADDRESS(x) (((x) & 0xFFFFFFFF) << 0)
#define   G_006918_D2GRPH_SECONDARY_SURFACE_ADDRESS(x) (((x) >> 0) & 0xFFFFFFFF)
#define   C_006918_D2GRPH_SECONDARY_SURFACE_ADDRESS    0x00000000


#define R_000001_MC_FB_LOCATION                      0x000001
#define   S_000001_MC_FB_START(x)                      (((x) & 0xFFFF) << 0)
#define   G_000001_MC_FB_START(x)                      (((x) >> 0) & 0xFFFF)
#define   C_000001_MC_FB_START                         0xFFFF0000
#define   S_000001_MC_FB_TOP(x)                        (((x) & 0xFFFF) << 16)
#define   G_000001_MC_FB_TOP(x)                        (((x) >> 16) & 0xFFFF)
#define   C_000001_MC_FB_TOP                           0x0000FFFF
#define R_000002_MC_AGP_LOCATION                     0x000002
#define   S_000002_MC_AGP_START(x)                     (((x) & 0xFFFF) << 0)
#define   G_000002_MC_AGP_START(x)                     (((x) >> 0) & 0xFFFF)
#define   C_000002_MC_AGP_START                        0xFFFF0000
#define   S_000002_MC_AGP_TOP(x)                       (((x) & 0xFFFF) << 16)
#define   G_000002_MC_AGP_TOP(x)                       (((x) >> 16) & 0xFFFF)
#define   C_000002_MC_AGP_TOP                          0x0000FFFF
#define R_000003_MC_AGP_BASE                         0x000003
#define   S_000003_AGP_BASE_ADDR(x)                    (((x) & 0xFFFFFFFF) << 0)
#define   G_000003_AGP_BASE_ADDR(x)                    (((x) >> 0) & 0xFFFFFFFF)
#define   C_000003_AGP_BASE_ADDR                       0x00000000
#define R_000004_MC_AGP_BASE_2                       0x000004
#define   S_000004_AGP_BASE_ADDR_2(x)                  (((x) & 0xF) << 0)
#define   G_000004_AGP_BASE_ADDR_2(x)                  (((x) >> 0) & 0xF)
#define   C_000004_AGP_BASE_ADDR_2                     0xFFFFFFF0


#define R_00000F_CP_DYN_CNTL                         0x00000F
#define   S_00000F_CP_FORCEON(x)                       (((x) & 0x1) << 0)
#define   G_00000F_CP_FORCEON(x)                       (((x) >> 0) & 0x1)
#define   C_00000F_CP_FORCEON                          0xFFFFFFFE
#define   S_00000F_CP_MAX_DYN_STOP_LAT(x)              (((x) & 0x1) << 1)
#define   G_00000F_CP_MAX_DYN_STOP_LAT(x)              (((x) >> 1) & 0x1)
#define   C_00000F_CP_MAX_DYN_STOP_LAT                 0xFFFFFFFD
#define   S_00000F_CP_CLOCK_STATUS(x)                  (((x) & 0x1) << 2)
#define   G_00000F_CP_CLOCK_STATUS(x)                  (((x) >> 2) & 0x1)
#define   C_00000F_CP_CLOCK_STATUS                     0xFFFFFFFB
#define   S_00000F_CP_PROG_SHUTOFF(x)                  (((x) & 0x1) << 3)
#define   G_00000F_CP_PROG_SHUTOFF(x)                  (((x) >> 3) & 0x1)
#define   C_00000F_CP_PROG_SHUTOFF                     0xFFFFFFF7
#define   S_00000F_CP_PROG_DELAY_VALUE(x)              (((x) & 0xFF) << 4)
#define   G_00000F_CP_PROG_DELAY_VALUE(x)              (((x) >> 4) & 0xFF)
#define   C_00000F_CP_PROG_DELAY_VALUE                 0xFFFFF00F
#define   S_00000F_CP_LOWER_POWER_IDLE(x)              (((x) & 0xFF) << 12)
#define   G_00000F_CP_LOWER_POWER_IDLE(x)              (((x) >> 12) & 0xFF)
#define   C_00000F_CP_LOWER_POWER_IDLE                 0xFFF00FFF
#define   S_00000F_CP_LOWER_POWER_IGNORE(x)            (((x) & 0x1) << 20)
#define   G_00000F_CP_LOWER_POWER_IGNORE(x)            (((x) >> 20) & 0x1)
#define   C_00000F_CP_LOWER_POWER_IGNORE               0xFFEFFFFF
#define   S_00000F_CP_NORMAL_POWER_IGNORE(x)           (((x) & 0x1) << 21)
#define   G_00000F_CP_NORMAL_POWER_IGNORE(x)           (((x) >> 21) & 0x1)
#define   C_00000F_CP_NORMAL_POWER_IGNORE              0xFFDFFFFF
#define   S_00000F_SPARE(x)                            (((x) & 0x3) << 22)
#define   G_00000F_SPARE(x)                            (((x) >> 22) & 0x3)
#define   C_00000F_SPARE                               0xFF3FFFFF
#define   S_00000F_CP_NORMAL_POWER_BUSY(x)             (((x) & 0xFF) << 24)
#define   G_00000F_CP_NORMAL_POWER_BUSY(x)             (((x) >> 24) & 0xFF)
#define   C_00000F_CP_NORMAL_POWER_BUSY                0x00FFFFFF
#define R_000011_E2_DYN_CNTL                         0x000011
#define   S_000011_E2_FORCEON(x)                       (((x) & 0x1) << 0)
#define   G_000011_E2_FORCEON(x)                       (((x) >> 0) & 0x1)
#define   C_000011_E2_FORCEON                          0xFFFFFFFE
#define   S_000011_E2_MAX_DYN_STOP_LAT(x)              (((x) & 0x1) << 1)
#define   G_000011_E2_MAX_DYN_STOP_LAT(x)              (((x) >> 1) & 0x1)
#define   C_000011_E2_MAX_DYN_STOP_LAT                 0xFFFFFFFD
#define   S_000011_E2_CLOCK_STATUS(x)                  (((x) & 0x1) << 2)
#define   G_000011_E2_CLOCK_STATUS(x)                  (((x) >> 2) & 0x1)
#define   C_000011_E2_CLOCK_STATUS                     0xFFFFFFFB
#define   S_000011_E2_PROG_SHUTOFF(x)                  (((x) & 0x1) << 3)
#define   G_000011_E2_PROG_SHUTOFF(x)                  (((x) >> 3) & 0x1)
#define   C_000011_E2_PROG_SHUTOFF                     0xFFFFFFF7
#define   S_000011_E2_PROG_DELAY_VALUE(x)              (((x) & 0xFF) << 4)
#define   G_000011_E2_PROG_DELAY_VALUE(x)              (((x) >> 4) & 0xFF)
#define   C_000011_E2_PROG_DELAY_VALUE                 0xFFFFF00F
#define   S_000011_E2_LOWER_POWER_IDLE(x)              (((x) & 0xFF) << 12)
#define   G_000011_E2_LOWER_POWER_IDLE(x)              (((x) >> 12) & 0xFF)
#define   C_000011_E2_LOWER_POWER_IDLE                 0xFFF00FFF
#define   S_000011_E2_LOWER_POWER_IGNORE(x)            (((x) & 0x1) << 20)
#define   G_000011_E2_LOWER_POWER_IGNORE(x)            (((x) >> 20) & 0x1)
#define   C_000011_E2_LOWER_POWER_IGNORE               0xFFEFFFFF
#define   S_000011_E2_NORMAL_POWER_IGNORE(x)           (((x) & 0x1) << 21)
#define   G_000011_E2_NORMAL_POWER_IGNORE(x)           (((x) >> 21) & 0x1)
#define   C_000011_E2_NORMAL_POWER_IGNORE              0xFFDFFFFF
#define   S_000011_SPARE(x)                            (((x) & 0x3) << 22)
#define   G_000011_SPARE(x)                            (((x) >> 22) & 0x3)
#define   C_000011_SPARE                               0xFF3FFFFF
#define   S_000011_E2_NORMAL_POWER_BUSY(x)             (((x) & 0xFF) << 24)
#define   G_000011_E2_NORMAL_POWER_BUSY(x)             (((x) >> 24) & 0xFF)
#define   C_000011_E2_NORMAL_POWER_BUSY                0x00FFFFFF
#define R_000013_IDCT_DYN_CNTL                       0x000013
#define   S_000013_IDCT_FORCEON(x)                     (((x) & 0x1) << 0)
#define   G_000013_IDCT_FORCEON(x)                     (((x) >> 0) & 0x1)
#define   C_000013_IDCT_FORCEON                        0xFFFFFFFE
#define   S_000013_IDCT_MAX_DYN_STOP_LAT(x)            (((x) & 0x1) << 1)
#define   G_000013_IDCT_MAX_DYN_STOP_LAT(x)            (((x) >> 1) & 0x1)
#define   C_000013_IDCT_MAX_DYN_STOP_LAT               0xFFFFFFFD
#define   S_000013_IDCT_CLOCK_STATUS(x)                (((x) & 0x1) << 2)
#define   G_000013_IDCT_CLOCK_STATUS(x)                (((x) >> 2) & 0x1)
#define   C_000013_IDCT_CLOCK_STATUS                   0xFFFFFFFB
#define   S_000013_IDCT_PROG_SHUTOFF(x)                (((x) & 0x1) << 3)
#define   G_000013_IDCT_PROG_SHUTOFF(x)                (((x) >> 3) & 0x1)
#define   C_000013_IDCT_PROG_SHUTOFF                   0xFFFFFFF7
#define   S_000013_IDCT_PROG_DELAY_VALUE(x)            (((x) & 0xFF) << 4)
#define   G_000013_IDCT_PROG_DELAY_VALUE(x)            (((x) >> 4) & 0xFF)
#define   C_000013_IDCT_PROG_DELAY_VALUE               0xFFFFF00F
#define   S_000013_IDCT_LOWER_POWER_IDLE(x)            (((x) & 0xFF) << 12)
#define   G_000013_IDCT_LOWER_POWER_IDLE(x)            (((x) >> 12) & 0xFF)
#define   C_000013_IDCT_LOWER_POWER_IDLE               0xFFF00FFF
#define   S_000013_IDCT_LOWER_POWER_IGNORE(x)          (((x) & 0x1) << 20)
#define   G_000013_IDCT_LOWER_POWER_IGNORE(x)          (((x) >> 20) & 0x1)
#define   C_000013_IDCT_LOWER_POWER_IGNORE             0xFFEFFFFF
#define   S_000013_IDCT_NORMAL_POWER_IGNORE(x)         (((x) & 0x1) << 21)
#define   G_000013_IDCT_NORMAL_POWER_IGNORE(x)         (((x) >> 21) & 0x1)
#define   C_000013_IDCT_NORMAL_POWER_IGNORE            0xFFDFFFFF
#define   S_000013_SPARE(x)                            (((x) & 0x3) << 22)
#define   G_000013_SPARE(x)                            (((x) >> 22) & 0x3)
#define   C_000013_SPARE                               0xFF3FFFFF
#define   S_000013_IDCT_NORMAL_POWER_BUSY(x)           (((x) & 0xFF) << 24)
#define   G_000013_IDCT_NORMAL_POWER_BUSY(x)           (((x) >> 24) & 0xFF)
#define   C_000013_IDCT_NORMAL_POWER_BUSY              0x00FFFFFF

#endif
