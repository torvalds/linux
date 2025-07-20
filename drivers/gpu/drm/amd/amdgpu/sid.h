/*
 * Copyright 2011 Advanced Micro Devices, Inc.
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
 * Authors: Alex Deucher
 */
#ifndef SI_H
#define SI_H

#define SI_MAX_CTLACKS_ASSERTION_WAIT   100

/* CG IND registers are accessed via SMC indirect space + SMC_CG_IND_START */
#define SMC_CG_IND_START                    0xc0030000
#define SMC_CG_IND_END                      0xc0040000

/* SMC IND registers */
#define	SMC_SYSCON_RESET_CNTL				0x80000000
#       define RST_REG                                  (1 << 0)
#define	SMC_SYSCON_CLOCK_CNTL_0				0x80000004
#       define CK_DISABLE                               (1 << 0)
#       define CKEN                                     (1 << 24)

#define DCCG_DISP_SLOW_SELECT_REG                       0x13F
#define		DCCG_DISP1_SLOW_SELECT(x)		((x) << 0)
#define		DCCG_DISP1_SLOW_SELECT_MASK		(7 << 0)
#define		DCCG_DISP1_SLOW_SELECT_SHIFT		0
#define		DCCG_DISP2_SLOW_SELECT(x)		((x) << 4)
#define		DCCG_DISP2_SLOW_SELECT_MASK		(7 << 4)
#define		DCCG_DISP2_SLOW_SELECT_SHIFT		4

/* discrete uvd clocks */
#define	CG_UPLL_FUNC_CNTL				0x18d
#	define UPLL_RESET_MASK				0x00000001
#	define UPLL_SLEEP_MASK				0x00000002
#	define UPLL_BYPASS_EN_MASK			0x00000004
#	define UPLL_CTLREQ_MASK				0x00000008
#	define UPLL_VCO_MODE_MASK			0x00000600
#	define UPLL_REF_DIV_MASK			0x003F0000
#	define UPLL_CTLACK_MASK				0x40000000
#	define UPLL_CTLACK2_MASK			0x80000000
#define	CG_UPLL_FUNC_CNTL_2				0x18e
#	define UPLL_PDIV_A(x)				((x) << 0)
#	define UPLL_PDIV_A_MASK				0x0000007F
#	define UPLL_PDIV_B(x)				((x) << 8)
#	define UPLL_PDIV_B_MASK				0x00007F00
#	define VCLK_SRC_SEL(x)				((x) << 20)
#	define VCLK_SRC_SEL_MASK			0x01F00000
#	define DCLK_SRC_SEL(x)				((x) << 25)
#	define DCLK_SRC_SEL_MASK			0x3E000000
#define	CG_UPLL_FUNC_CNTL_3				0x18f
#	define UPLL_FB_DIV(x)				((x) << 0)
#	define UPLL_FB_DIV_MASK				0x01FFFFFF
#define	CG_UPLL_FUNC_CNTL_4                             0x191
#	define UPLL_SPARE_ISPARE9			0x00020000
#define	CG_UPLL_FUNC_CNTL_5				0x192
#	define RESET_ANTI_MUX_MASK			0x00000200
#define	CG_UPLL_SPREAD_SPECTRUM				0x194
#	define SSEN_MASK				0x00000001

#define VM_INVALIDATE_REQUEST				0x51E
#define VM_INVALIDATE_RESPONSE				0x51F

#define VM_L2_CG           				0x570
#define		MC_CG_ENABLE				(1 << 18)
#define		MC_LS_ENABLE				(1 << 19)

#define	MC_VM_FB_LOCATION				0x809
#define	MC_VM_AGP_TOP					0x80A
#define	MC_VM_AGP_BOT					0x80B
#define	MC_VM_AGP_BASE					0x80C
#define	MC_VM_SYSTEM_APERTURE_LOW_ADDR			0x80D
#define	MC_VM_SYSTEM_APERTURE_HIGH_ADDR			0x80E
#define	MC_VM_SYSTEM_APERTURE_DEFAULT_ADDR		0x80F

#define	MC_VM_MX_L1_TLB_CNTL				0x819
#define		ENABLE_L1_TLB					(1 << 0)
#define		ENABLE_L1_FRAGMENT_PROCESSING			(1 << 1)
#define		SYSTEM_ACCESS_MODE_PA_ONLY			(0 << 3)
#define		SYSTEM_ACCESS_MODE_USE_SYS_MAP			(1 << 3)
#define		SYSTEM_ACCESS_MODE_IN_SYS			(2 << 3)
#define		SYSTEM_ACCESS_MODE_NOT_IN_SYS			(3 << 3)
#define		SYSTEM_APERTURE_UNMAPPED_ACCESS_PASS_THRU	(0 << 5)
#define		ENABLE_ADVANCED_DRIVER_MODEL			(1 << 6)

#define MC_SHARED_BLACKOUT_CNTL           		0x82B

#define MC_HUB_MISC_HUB_CG           			0x82E
#define MC_HUB_MISC_VM_CG           			0x82F

#define MC_HUB_MISC_SIP_CG           			0x830

#define MC_XPB_CLK_GAT           			0x91E

#define MC_CITF_MISC_RD_CG           			0x992
#define MC_CITF_MISC_WR_CG           			0x993
#define MC_CITF_MISC_VM_CG           			0x994

#define	MC_ARB_DRAM_TIMING				0x9DD
#define	MC_ARB_DRAM_TIMING2				0x9DE

#define MC_ARB_BURST_TIME                               0xA02
#define		STATE0(x)				((x) << 0)
#define		STATE0_MASK				(0x1f << 0)
#define		STATE0_SHIFT				0
#define		STATE1(x)				((x) << 5)
#define		STATE1_MASK				(0x1f << 5)
#define		STATE1_SHIFT				5
#define		STATE2(x)				((x) << 10)
#define		STATE2_MASK				(0x1f << 10)
#define		STATE2_SHIFT				10
#define		STATE3(x)				((x) << 15)
#define		STATE3_MASK				(0x1f << 15)
#define		STATE3_SHIFT				15

#define	MC_SEQ_TRAIN_WAKEUP_CNTL			0xA3A
#define		TRAIN_DONE_D0      			(1 << 30)
#define		TRAIN_DONE_D1      			(1 << 31)

#define MC_SEQ_SUP_CNTL           			0xA32
#define		RUN_MASK      				(1 << 0)
#define MC_SEQ_SUP_PGM           			0xA33
#define MC_PMG_AUTO_CMD           			0xA34

#define MC_IO_PAD_CNTL_D0           			0xA74
#define		MEM_FALL_OUT_CMD      			(1 << 8)

#define MC_SEQ_RAS_TIMING                               0xA28
#define MC_SEQ_CAS_TIMING                               0xA29
#define MC_SEQ_MISC_TIMING                              0xA2A
#define MC_SEQ_MISC_TIMING2                             0xA2B
#define MC_SEQ_PMG_TIMING                               0xA2C
#define MC_SEQ_RD_CTL_D0                                0xA2D
#define MC_SEQ_RD_CTL_D1                                0xA2E
#define MC_SEQ_WR_CTL_D0                                0xA2F
#define MC_SEQ_WR_CTL_D1                                0xA30

#define MC_SEQ_MISC0           				0xA80
#define 	MC_SEQ_MISC0_VEN_ID_SHIFT               8
#define 	MC_SEQ_MISC0_VEN_ID_MASK                0x00000f00
#define 	MC_SEQ_MISC0_VEN_ID_VALUE               3
#define 	MC_SEQ_MISC0_REV_ID_SHIFT               12
#define 	MC_SEQ_MISC0_REV_ID_MASK                0x0000f000
#define 	MC_SEQ_MISC0_REV_ID_VALUE               1
#define 	MC_SEQ_MISC0_GDDR5_SHIFT                28
#define 	MC_SEQ_MISC0_GDDR5_MASK                 0xf0000000
#define 	MC_SEQ_MISC0_GDDR5_VALUE                5
#define MC_SEQ_MISC1                                    0xA81
#define MC_SEQ_RESERVE_M                                0xA82
#define MC_PMG_CMD_EMRS                                 0xA83

#define MC_SEQ_IO_DEBUG_INDEX           		0xA91
#define MC_SEQ_IO_DEBUG_DATA           			0xA92

#define MC_SEQ_MISC5                                    0xA95
#define MC_SEQ_MISC6                                    0xA96

#define MC_SEQ_MISC7                                    0xA99

#define MC_SEQ_RAS_TIMING_LP                            0xA9B
#define MC_SEQ_CAS_TIMING_LP                            0xA9C
#define MC_SEQ_MISC_TIMING_LP                           0xA9D
#define MC_SEQ_MISC_TIMING2_LP                          0xA9E
#define MC_SEQ_WR_CTL_D0_LP                             0xA9F
#define MC_SEQ_WR_CTL_D1_LP                             0xAA0
#define MC_SEQ_PMG_CMD_EMRS_LP                          0xAA1
#define MC_SEQ_PMG_CMD_MRS_LP                           0xAA2

#define MC_PMG_CMD_MRS                                  0xAAB

#define MC_SEQ_RD_CTL_D0_LP                             0xAC7
#define MC_SEQ_RD_CTL_D1_LP                             0xAC8

#define MC_PMG_CMD_MRS1                                 0xAD1
#define MC_SEQ_PMG_CMD_MRS1_LP                          0xAD2
#define MC_SEQ_PMG_TIMING_LP                            0xAD3

#define MC_SEQ_WR_CTL_2                                 0xAD5
#define MC_SEQ_WR_CTL_2_LP                              0xAD6
#define MC_PMG_CMD_MRS2                                 0xAD7
#define MC_SEQ_PMG_CMD_MRS2_LP                          0xAD8

#define	MCLK_PWRMGT_CNTL				0xAE8
#       define DLL_SPEED(x)				((x) << 0)
#       define DLL_SPEED_MASK				(0x1f << 0)
#       define DLL_READY                                (1 << 6)
#       define MC_INT_CNTL                              (1 << 7)
#       define MRDCK0_PDNB                              (1 << 8)
#       define MRDCK1_PDNB                              (1 << 9)
#       define MRDCK0_RESET                             (1 << 16)
#       define MRDCK1_RESET                             (1 << 17)
#       define DLL_READY_READ                           (1 << 24)
#define	DLL_CNTL					0xAE9
#       define MRDCK0_BYPASS                            (1 << 24)
#       define MRDCK1_BYPASS                            (1 << 25)

#define	MPLL_CNTL_MODE					0xAEC
#       define MPLL_MCLK_SEL                            (1 << 11)
#define	MPLL_FUNC_CNTL					0xAED
#define		BWCTRL(x)				((x) << 20)
#define		BWCTRL_MASK				(0xff << 20)
#define	MPLL_FUNC_CNTL_1				0xAEE
#define		VCO_MODE(x)				((x) << 0)
#define		VCO_MODE_MASK				(3 << 0)
#define		CLKFRAC(x)				((x) << 4)
#define		CLKFRAC_MASK				(0xfff << 4)
#define		CLKF(x)					((x) << 16)
#define		CLKF_MASK				(0xfff << 16)
#define	MPLL_FUNC_CNTL_2				0xAEF
#define	MPLL_AD_FUNC_CNTL				0xAF0
#define		YCLK_POST_DIV(x)			((x) << 0)
#define		YCLK_POST_DIV_MASK			(7 << 0)
#define	MPLL_DQ_FUNC_CNTL				0xAF1
#define		YCLK_SEL(x)				((x) << 4)
#define		YCLK_SEL_MASK				(1 << 4)

#define	MPLL_SS1					0xAF3
#define		CLKV(x)					((x) << 0)
#define		CLKV_MASK				(0x3ffffff << 0)
#define	MPLL_SS2					0xAF4
#define		CLKS(x)					((x) << 0)
#define		CLKS_MASK				(0xfff << 0)

#define ATC_MISC_CG           				0xCD4

#define IH_RB_CNTL                                        0xF80
#       define IH_RB_ENABLE                               (1 << 0)
#       define IH_IB_SIZE(x)                              ((x) << 1) /* log2 */
#       define IH_RB_FULL_DRAIN_ENABLE                    (1 << 6)
#       define IH_WPTR_WRITEBACK_ENABLE                   (1 << 8)
#       define IH_WPTR_WRITEBACK_TIMER(x)                 ((x) << 9) /* log2 */
#       define IH_WPTR_OVERFLOW_ENABLE                    (1 << 16)
#       define IH_WPTR_OVERFLOW_CLEAR                     (1 << 31)
#define IH_RB_BASE                                        0xF81
#define IH_RB_RPTR                                        0xF82
#define IH_RB_WPTR                                        0xF83
#       define RB_OVERFLOW                                (1 << 0)
#       define WPTR_OFFSET_MASK                           0x3fffc
#define IH_RB_WPTR_ADDR_HI                                0xF84
#define IH_RB_WPTR_ADDR_LO                                0xF85
#define IH_CNTL                                           0xF86
#       define ENABLE_INTR                                (1 << 0)
#       define IH_MC_SWAP(x)                              ((x) << 1)
#       define IH_MC_SWAP_NONE                            0
#       define IH_MC_SWAP_16BIT                           1
#       define IH_MC_SWAP_32BIT                           2
#       define IH_MC_SWAP_64BIT                           3
#       define RPTR_REARM                                 (1 << 4)
#       define MC_WRREQ_CREDIT(x)                         ((x) << 15)
#       define MC_WR_CLEAN_CNT(x)                         ((x) << 20)
#       define MC_VMID(x)                                 ((x) << 25)

#define INTERRUPT_CNTL                                    0x151A
#       define IH_DUMMY_RD_OVERRIDE                       (1 << 0)
#       define IH_DUMMY_RD_EN                             (1 << 1)
#       define IH_REQ_NONSNOOP_EN                         (1 << 3)
#       define GEN_IH_INT_EN                              (1 << 8)
#define INTERRUPT_CNTL2                                   0x151B

#define	VGT_VTX_VECT_EJECT_REG				0x222C
#define	VGT_ESGS_RING_SIZE				0x2232
#define	VGT_GSVS_RING_SIZE				0x2233
#define	VGT_GS_VERTEX_REUSE				0x2235
#define	VGT_PRIMITIVE_TYPE				0x2256
#define	VGT_INDEX_TYPE					0x2257
#define	VGT_NUM_INDICES					0x225C
#define	VGT_NUM_INSTANCES				0x225D
#define	VGT_TF_RING_SIZE				0x2262
#define	VGT_HS_OFFCHIP_PARAM				0x226C
#define	VGT_TF_MEMORY_BASE				0x226E

#define	PA_SC_ENHANCE					0x22FC

#define	TA_CNTL_AUX					0x2542

// #define PA_SC_RASTER_CONFIG                             0xA0D4
#	define RB_XSEL2(x)				((x) << 4)
#	define RB_XSEL2_MASK				(0x3 << 4)
#	define RB_XSEL					(1 << 6)
#	define RB_YSEL					(1 << 7)
#	define PKR_MAP(x)				((x) << 8)
#	define PKR_XSEL(x)				((x) << 10)
#	define PKR_XSEL_MASK				(0x3 << 10)
#	define PKR_YSEL(x)				((x) << 12)
#	define PKR_YSEL_MASK				(0x3 << 12)
#	define SC_MAP(x)				((x) << 16)
#	define SC_MAP_MASK				(0x3 << 16)
#	define SC_XSEL(x)				((x) << 18)
#	define SC_XSEL_MASK				(0x3 << 18)
#	define SC_YSEL(x)				((x) << 20)
#	define SC_YSEL_MASK				(0x3 << 20)
#	define SE_MAP(x)				((x) << 24)
#	define SE_XSEL(x)				((x) << 26)
#	define SE_XSEL_MASK				(0x3 << 26)
#	define SE_YSEL(x)				((x) << 28)
#	define SE_YSEL_MASK				(0x3 << 28)

/* PCIE PORT registers idx/data 0x38/0x3c */
// #define PCIE_LC_LINK_WIDTH_CNTL                           0xa2 /* PCIE_P */
#       define LC_LINK_WIDTH_X0                           0
#       define LC_LINK_WIDTH_X1                           1
#       define LC_LINK_WIDTH_X2                           2
#       define LC_LINK_WIDTH_X4                           3
#       define LC_LINK_WIDTH_X8                           4
#       define LC_LINK_WIDTH_X16                          6

/*
 * PM4
 */
#define PACKET_TYPE0    0
#define PACKET0(reg, n) ((PACKET_TYPE0 << 30) |				\
                         ((reg) & 0xFFFF) |				\
                         ((n) & 0x3FFF) << 16)
#define CP_PACKET2			0x80000000
#define		PACKET2_PAD_SHIFT		0
#define		PACKET2_PAD_MASK		(0x3fffffff << 0)

#define PACKET2(v)	(CP_PACKET2 | REG_SET(PACKET2_PAD, (v)))
#define RADEON_PACKET_TYPE3 3
#define PACKET3(op, n)	((RADEON_PACKET_TYPE3 << 30) |			\
			 (((op) & 0xFF) << 8) |				\
			 ((n) & 0x3FFF) << 16)

#define PACKET3_COMPUTE(op, n) (PACKET3(op, n) | 1 << 1)

/* Packet 3 types */
#define	PACKET3_NOP					0x10
#define	PACKET3_SET_BASE				0x11
#define		PACKET3_BASE_INDEX(x)                  ((x) << 0)
#define			GDS_PARTITION_BASE		2
#define			CE_PARTITION_BASE		3
#define	PACKET3_CLEAR_STATE				0x12
#define	PACKET3_INDEX_BUFFER_SIZE			0x13
#define	PACKET3_DISPATCH_DIRECT				0x15
#define	PACKET3_DISPATCH_INDIRECT			0x16
#define	PACKET3_ALLOC_GDS				0x1B
#define	PACKET3_WRITE_GDS_RAM				0x1C
#define	PACKET3_ATOMIC_GDS				0x1D
#define	PACKET3_ATOMIC					0x1E
#define	PACKET3_OCCLUSION_QUERY				0x1F
#define	PACKET3_SET_PREDICATION				0x20
#define	PACKET3_REG_RMW					0x21
#define	PACKET3_COND_EXEC				0x22
#define	PACKET3_PRED_EXEC				0x23
#define	PACKET3_DRAW_INDIRECT				0x24
#define	PACKET3_DRAW_INDEX_INDIRECT			0x25
#define	PACKET3_INDEX_BASE				0x26
#define	PACKET3_DRAW_INDEX_2				0x27
#define	PACKET3_CONTEXT_CONTROL				0x28
#define	PACKET3_INDEX_TYPE				0x2A
#define	PACKET3_DRAW_INDIRECT_MULTI			0x2C
#define	PACKET3_DRAW_INDEX_AUTO				0x2D
#define	PACKET3_DRAW_INDEX_IMMD				0x2E
#define	PACKET3_NUM_INSTANCES				0x2F
#define	PACKET3_DRAW_INDEX_MULTI_AUTO			0x30
#define	PACKET3_INDIRECT_BUFFER_CONST			0x31
#define	PACKET3_INDIRECT_BUFFER				0x3F
#define	PACKET3_STRMOUT_BUFFER_UPDATE			0x34
#define	PACKET3_DRAW_INDEX_OFFSET_2			0x35
#define	PACKET3_DRAW_INDEX_MULTI_ELEMENT		0x36
#define	PACKET3_WRITE_DATA				0x37
#define		WRITE_DATA_DST_SEL(x)                   ((x) << 8)
                /* 0 - register
		 * 1 - memory (sync - via GRBM)
		 * 2 - tc/l2
		 * 3 - gds
		 * 4 - reserved
		 * 5 - memory (async - direct)
		 */
#define		WR_ONE_ADDR                             (1 << 16)
#define		WR_CONFIRM                              (1 << 20)
#define		WRITE_DATA_ENGINE_SEL(x)                ((x) << 30)
                /* 0 - me
		 * 1 - pfp
		 * 2 - ce
		 */
#define	PACKET3_DRAW_INDEX_INDIRECT_MULTI		0x38
#define	PACKET3_MEM_SEMAPHORE				0x39
#define	PACKET3_MPEG_INDEX				0x3A
#define	PACKET3_COPY_DW					0x3B
#define	PACKET3_WAIT_REG_MEM				0x3C
#define		WAIT_REG_MEM_FUNCTION(x)                ((x) << 0)
                /* 0 - always
		 * 1 - <
		 * 2 - <=
		 * 3 - ==
		 * 4 - !=
		 * 5 - >=
		 * 6 - >
		 */
#define		WAIT_REG_MEM_MEM_SPACE(x)               ((x) << 4)
                /* 0 - reg
		 * 1 - mem
		 */
#define		WAIT_REG_MEM_ENGINE(x)                  ((x) << 8)
                /* 0 - me
		 * 1 - pfp
		 */
#define	PACKET3_MEM_WRITE				0x3D
#define	PACKET3_COPY_DATA				0x40
#define	PACKET3_CP_DMA					0x41
/* 1. header
 * 2. SRC_ADDR_LO or DATA [31:0]
 * 3. CP_SYNC [31] | SRC_SEL [30:29] | ENGINE [27] | DST_SEL [21:20] |
 *    SRC_ADDR_HI [7:0]
 * 4. DST_ADDR_LO [31:0]
 * 5. DST_ADDR_HI [7:0]
 * 6. COMMAND [30:21] | BYTE_COUNT [20:0]
 */
#              define PACKET3_CP_DMA_DST_SEL(x)    ((x) << 20)
                /* 0 - DST_ADDR
		 * 1 - GDS
		 */
#              define PACKET3_CP_DMA_ENGINE(x)     ((x) << 27)
                /* 0 - ME
		 * 1 - PFP
		 */
#              define PACKET3_CP_DMA_SRC_SEL(x)    ((x) << 29)
                /* 0 - SRC_ADDR
		 * 1 - GDS
		 * 2 - DATA
		 */
#              define PACKET3_CP_DMA_CP_SYNC       (1 << 31)
/* COMMAND */
#              define PACKET3_CP_DMA_DIS_WC        (1 << 21)
#              define PACKET3_CP_DMA_CMD_SRC_SWAP(x) ((x) << 22)
                /* 0 - none
		 * 1 - 8 in 16
		 * 2 - 8 in 32
		 * 3 - 8 in 64
		 */
#              define PACKET3_CP_DMA_CMD_DST_SWAP(x) ((x) << 24)
                /* 0 - none
		 * 1 - 8 in 16
		 * 2 - 8 in 32
		 * 3 - 8 in 64
		 */
#              define PACKET3_CP_DMA_CMD_SAS       (1 << 26)
                /* 0 - memory
		 * 1 - register
		 */
#              define PACKET3_CP_DMA_CMD_DAS       (1 << 27)
                /* 0 - memory
		 * 1 - register
		 */
#              define PACKET3_CP_DMA_CMD_SAIC      (1 << 28)
#              define PACKET3_CP_DMA_CMD_DAIC      (1 << 29)
#              define PACKET3_CP_DMA_CMD_RAW_WAIT  (1 << 30)
#define	PACKET3_PFP_SYNC_ME				0x42
#define	PACKET3_SURFACE_SYNC				0x43
#              define PACKET3_DEST_BASE_0_ENA      (1 << 0)
#              define PACKET3_DEST_BASE_1_ENA      (1 << 1)
#              define PACKET3_CB0_DEST_BASE_ENA    (1 << 6)
#              define PACKET3_CB1_DEST_BASE_ENA    (1 << 7)
#              define PACKET3_CB2_DEST_BASE_ENA    (1 << 8)
#              define PACKET3_CB3_DEST_BASE_ENA    (1 << 9)
#              define PACKET3_CB4_DEST_BASE_ENA    (1 << 10)
#              define PACKET3_CB5_DEST_BASE_ENA    (1 << 11)
#              define PACKET3_CB6_DEST_BASE_ENA    (1 << 12)
#              define PACKET3_CB7_DEST_BASE_ENA    (1 << 13)
#              define PACKET3_DB_DEST_BASE_ENA     (1 << 14)
#              define PACKET3_DEST_BASE_2_ENA      (1 << 19)
#              define PACKET3_DEST_BASE_3_ENA      (1 << 21)
#              define PACKET3_TCL1_ACTION_ENA      (1 << 22)
#              define PACKET3_TC_ACTION_ENA        (1 << 23)
#              define PACKET3_CB_ACTION_ENA        (1 << 25)
#              define PACKET3_DB_ACTION_ENA        (1 << 26)
#              define PACKET3_SH_KCACHE_ACTION_ENA (1 << 27)
#              define PACKET3_SH_ICACHE_ACTION_ENA (1 << 29)
#define	PACKET3_ME_INITIALIZE				0x44
#define		PACKET3_ME_INITIALIZE_DEVICE_ID(x) ((x) << 16)
#define	PACKET3_COND_WRITE				0x45
#define	PACKET3_EVENT_WRITE				0x46
#define		EVENT_TYPE(x)                           ((x) << 0)
#define		EVENT_INDEX(x)                          ((x) << 8)
                /* 0 - any non-TS event
		 * 1 - ZPASS_DONE
		 * 2 - SAMPLE_PIPELINESTAT
		 * 3 - SAMPLE_STREAMOUTSTAT*
		 * 4 - *S_PARTIAL_FLUSH
		 * 5 - EOP events
		 * 6 - EOS events
		 * 7 - CACHE_FLUSH, CACHE_FLUSH_AND_INV_EVENT
		 */
#define		INV_L2                                  (1 << 20)
                /* INV TC L2 cache when EVENT_INDEX = 7 */
#define	PACKET3_EVENT_WRITE_EOP				0x47
#define		DATA_SEL(x)                             ((x) << 29)
                /* 0 - discard
		 * 1 - send low 32bit data
		 * 2 - send 64bit data
		 * 3 - send 64bit counter value
		 */
#define		INT_SEL(x)                              ((x) << 24)
                /* 0 - none
		 * 1 - interrupt only (DATA_SEL = 0)
		 * 2 - interrupt when data write is confirmed
		 */
#define	PACKET3_EVENT_WRITE_EOS				0x48
#define	PACKET3_PREAMBLE_CNTL				0x4A
#              define PACKET3_PREAMBLE_BEGIN_CLEAR_STATE     (2 << 28)
#              define PACKET3_PREAMBLE_END_CLEAR_STATE       (3 << 28)
#define	PACKET3_ONE_REG_WRITE				0x57
#define	PACKET3_LOAD_CONFIG_REG				0x5F
#define	PACKET3_LOAD_CONTEXT_REG			0x60
#define	PACKET3_LOAD_SH_REG				0x61
#define	PACKET3_SET_CONFIG_REG				0x68
#define		PACKET3_SET_CONFIG_REG_START			0x00002000
#define		PACKET3_SET_CONFIG_REG_END			0x00002c00
#define	PACKET3_SET_CONTEXT_REG				0x69
#define		PACKET3_SET_CONTEXT_REG_START			0x000a000
#define		PACKET3_SET_CONTEXT_REG_END			0x000a400
#define	PACKET3_SET_CONTEXT_REG_INDIRECT		0x73
#define	PACKET3_SET_RESOURCE_INDIRECT			0x74
#define	PACKET3_SET_SH_REG				0x76
#define		PACKET3_SET_SH_REG_START			0x00002c00
#define		PACKET3_SET_SH_REG_END				0x00003000
#define	PACKET3_SET_SH_REG_OFFSET			0x77
#define	PACKET3_ME_WRITE				0x7A
#define	PACKET3_SCRATCH_RAM_WRITE			0x7D
#define	PACKET3_SCRATCH_RAM_READ			0x7E
#define	PACKET3_CE_WRITE				0x7F
#define	PACKET3_LOAD_CONST_RAM				0x80
#define	PACKET3_WRITE_CONST_RAM				0x81
#define	PACKET3_WRITE_CONST_RAM_OFFSET			0x82
#define	PACKET3_DUMP_CONST_RAM				0x83
#define	PACKET3_INCREMENT_CE_COUNTER			0x84
#define	PACKET3_INCREMENT_DE_COUNTER			0x85
#define	PACKET3_WAIT_ON_CE_COUNTER			0x86
#define	PACKET3_WAIT_ON_DE_COUNTER			0x87
#define	PACKET3_WAIT_ON_DE_COUNTER_DIFF			0x88
#define	PACKET3_SET_CE_DE_COUNTERS			0x89
#define	PACKET3_WAIT_ON_AVAIL_BUFFER			0x8A
#define	PACKET3_SWITCH_BUFFER				0x8B

/* ASYNC DMA - first instance at 0xd000, second at 0xd800 */
#define DMA0_REGISTER_OFFSET                              0x0 /* not a register */
#define DMA1_REGISTER_OFFSET                              0x200 /* not a register */
#define SDMA_MAX_INSTANCE 2

#define DMA_PACKET(cmd, b, t, s, n)	((((cmd) & 0xF) << 28) |	\
					 (((b) & 0x1) << 26) |		\
					 (((t) & 0x1) << 23) |		\
					 (((s) & 0x1) << 22) |		\
					 (((n) & 0xFFFFF) << 0))

#define DMA_IB_PACKET(cmd, vmid, n)	((((cmd) & 0xF) << 28) |	\
					 (((vmid) & 0xF) << 20) |	\
					 (((n) & 0xFFFFF) << 0))

#define DMA_PTE_PDE_PACKET(n)		((2 << 28) |			\
					 (1 << 26) |			\
					 (1 << 21) |			\
					 (((n) & 0xFFFFF) << 0))

/* async DMA Packet types */
#define	DMA_PACKET_WRITE				  0x2
#define	DMA_PACKET_COPY					  0x3
#define	DMA_PACKET_INDIRECT_BUFFER			  0x4
#define	DMA_PACKET_SEMAPHORE				  0x5
#define	DMA_PACKET_FENCE				  0x6
#define	DMA_PACKET_TRAP					  0x7
#define	DMA_PACKET_SRBM_WRITE				  0x9
#define	DMA_PACKET_CONSTANT_FILL			  0xd
#define	DMA_PACKET_POLL_REG_MEM				  0xe
#define	DMA_PACKET_NOP					  0xf

/* VCE */
#define VCE_STATUS					0x20004
#define VCE_VCPU_CNTL					0x20014
#define		VCE_CLK_EN				(1 << 0)
#define VCE_VCPU_CACHE_OFFSET0				0x20024
#define VCE_VCPU_CACHE_SIZE0				0x20028
#define VCE_VCPU_CACHE_OFFSET1				0x2002c
#define VCE_VCPU_CACHE_SIZE1				0x20030
#define VCE_VCPU_CACHE_OFFSET2				0x20034
#define VCE_VCPU_CACHE_SIZE2				0x20038
#define VCE_SOFT_RESET					0x20120
#define 	VCE_ECPU_SOFT_RESET			(1 << 0)
#define 	VCE_FME_SOFT_RESET			(1 << 2)
#define VCE_RB_BASE_LO2					0x2016c
#define VCE_RB_BASE_HI2					0x20170
#define VCE_RB_SIZE2					0x20174
#define VCE_RB_RPTR2					0x20178
#define VCE_RB_WPTR2					0x2017c
#define VCE_RB_BASE_LO					0x20180
#define VCE_RB_BASE_HI					0x20184
#define VCE_RB_SIZE					0x20188
#define VCE_RB_RPTR					0x2018c
#define VCE_RB_WPTR					0x20190
#define VCE_CLOCK_GATING_A				0x202f8
#define VCE_CLOCK_GATING_B				0x202fc
#define VCE_UENC_CLOCK_GATING				0x205bc
#define VCE_UENC_REG_CLOCK_GATING			0x205c0
#define VCE_FW_REG_STATUS				0x20e10
#	define VCE_FW_REG_STATUS_BUSY			(1 << 0)
#	define VCE_FW_REG_STATUS_PASS			(1 << 3)
#	define VCE_FW_REG_STATUS_DONE			(1 << 11)
#define VCE_LMI_FW_START_KEYSEL				0x20e18
#define VCE_LMI_FW_PERIODIC_CTRL			0x20e20
#define VCE_LMI_CTRL2					0x20e74
#define VCE_LMI_CTRL					0x20e98
#define VCE_LMI_VM_CTRL					0x20ea0
#define VCE_LMI_SWAP_CNTL				0x20eb4
#define VCE_LMI_SWAP_CNTL1				0x20eb8
#define VCE_LMI_CACHE_CTRL				0x20ef4

#define VCE_CMD_NO_OP					0x00000000
#define VCE_CMD_END					0x00000001
#define VCE_CMD_IB					0x00000002
#define VCE_CMD_FENCE					0x00000003
#define VCE_CMD_TRAP					0x00000004
#define VCE_CMD_IB_AUTO					0x00000005
#define VCE_CMD_SEMAPHORE				0x00000006


//#dce stupp
/* display controller offsets used for crtc/cur/lut/grph/viewport/etc. */
#define CRTC0_REGISTER_OFFSET                 (0x1b7c - 0x1b7c) //(0x6df0 - 0x6df0)/4
#define CRTC1_REGISTER_OFFSET                 (0x1e7c - 0x1b7c) //(0x79f0 - 0x6df0)/4
#define CRTC2_REGISTER_OFFSET                 (0x417c - 0x1b7c) //(0x105f0 - 0x6df0)/4
#define CRTC3_REGISTER_OFFSET                 (0x447c - 0x1b7c) //(0x111f0 - 0x6df0)/4
#define CRTC4_REGISTER_OFFSET                 (0x477c - 0x1b7c) //(0x11df0 - 0x6df0)/4
#define CRTC5_REGISTER_OFFSET                 (0x4a7c - 0x1b7c) //(0x129f0 - 0x6df0)/4

/* hpd instance offsets */
#define HPD0_REGISTER_OFFSET                 (0x1807 - 0x1807)
#define HPD1_REGISTER_OFFSET                 (0x180a - 0x1807)
#define HPD2_REGISTER_OFFSET                 (0x180d - 0x1807)
#define HPD3_REGISTER_OFFSET                 (0x1810 - 0x1807)
#define HPD4_REGISTER_OFFSET                 (0x1813 - 0x1807)
#define HPD5_REGISTER_OFFSET                 (0x1816 - 0x1807)

/* audio endpt instance offsets */
#define AUD0_REGISTER_OFFSET                 (0x1780 - 0x1780)
#define AUD1_REGISTER_OFFSET                 (0x1786 - 0x1780)
#define AUD2_REGISTER_OFFSET                 (0x178c - 0x1780)
#define AUD3_REGISTER_OFFSET                 (0x1792 - 0x1780)
#define AUD4_REGISTER_OFFSET                 (0x1798 - 0x1780)
#define AUD5_REGISTER_OFFSET                 (0x179d - 0x1780)
#define AUD6_REGISTER_OFFSET                 (0x17a4 - 0x1780)

#define CURSOR_WIDTH 64
#define CURSOR_HEIGHT 64


#define R600_ROM_CNTL                              0x580
#       define R600_SCK_OVERWRITE                  (1 << 1)
#       define R600_SCK_PRESCALE_CRYSTAL_CLK_SHIFT 28
#       define R600_SCK_PRESCALE_CRYSTAL_CLK_MASK  (0xf << 28)

#define GRPH_ARRAY_LINEAR_GENERAL      0
#define GRPH_ARRAY_LINEAR_ALIGNED      1
#define GRPH_ARRAY_1D_TILED_THIN1      2
#define GRPH_ARRAY_2D_TILED_THIN1      4

#define ES_AND_GS_AUTO       3
#define BUF_SWAP_32BIT       (2 << 16)

#define GRPH_DEPTH_8BPP                0
#define GRPH_DEPTH_16BPP               1
#define GRPH_DEPTH_32BPP               2

/* 8 BPP */
#define GRPH_FORMAT_INDEXED            0

/* 16 BPP */
#define GRPH_FORMAT_ARGB1555           0
#define GRPH_FORMAT_ARGB565            1
#define GRPH_FORMAT_ARGB4444           2
#define GRPH_FORMAT_AI88               3
#define GRPH_FORMAT_MONO16             4
#define GRPH_FORMAT_BGRA5551           5

/* 32 BPP */
#define GRPH_FORMAT_ARGB8888           0
#define GRPH_FORMAT_ARGB2101010        1
#define GRPH_FORMAT_32BPP_DIG          2
#define GRPH_FORMAT_8B_ARGB2101010     3
#define GRPH_FORMAT_BGRA1010102        4
#define GRPH_FORMAT_8B_BGRA1010102     5
#define GRPH_FORMAT_RGB111110          6
#define GRPH_FORMAT_BGR101111          7

#define GRPH_ENDIAN_NONE               0
#define GRPH_ENDIAN_8IN16              1
#define GRPH_ENDIAN_8IN32              2
#define GRPH_ENDIAN_8IN64              3
#define GRPH_RED_SEL_R                 0
#define GRPH_RED_SEL_G                 1
#define GRPH_RED_SEL_B                 2
#define GRPH_RED_SEL_A                 3

#define GRPH_GREEN_SEL_G               0
#define GRPH_GREEN_SEL_B               1
#define GRPH_GREEN_SEL_A               2
#define GRPH_GREEN_SEL_R               3

#define GRPH_BLUE_SEL_B                0
#define GRPH_BLUE_SEL_A                1
#define GRPH_BLUE_SEL_R                2
#define GRPH_BLUE_SEL_G                3

#define GRPH_ALPHA_SEL_A               0
#define GRPH_ALPHA_SEL_R               1
#define GRPH_ALPHA_SEL_G               2
#define GRPH_ALPHA_SEL_B               3

/* CUR_CONTROL */
	#define CURSOR_MONO                    0
	#define CURSOR_24_1                    1
	#define CURSOR_24_8_PRE_MULT           2
	#define CURSOR_24_8_UNPRE_MULT         3
	#define CURSOR_URGENT_ALWAYS           0
	#define CURSOR_URGENT_1_8              1
	#define CURSOR_URGENT_1_4              2
	#define CURSOR_URGENT_3_8              3
	#define CURSOR_URGENT_1_2              4

/* INPUT_CSC_CONTROL */
#       define INPUT_CSC_BYPASS                     0
#       define INPUT_CSC_PROG_COEFF                 1
#       define INPUT_CSC_PROG_SHARED_MATRIXA        2

/* OUTPUT_CSC_CONTROL */
#       define OUTPUT_CSC_BYPASS                    0
#       define OUTPUT_CSC_TV_RGB                    1
#       define OUTPUT_CSC_YCBCR_601                 2
#       define OUTPUT_CSC_YCBCR_709                 3
#       define OUTPUT_CSC_PROG_COEFF                4
#       define OUTPUT_CSC_PROG_SHARED_MATRIXB       5

/* DEGAMMA_CONTROL */
#       define DEGAMMA_BYPASS                       0
#       define DEGAMMA_SRGB_24                      1
#       define DEGAMMA_XVYCC_222                    2

/* GAMUT_REMAP_CONTROL */
#       define GAMUT_REMAP_BYPASS                   0
#       define GAMUT_REMAP_PROG_COEFF               1
#       define GAMUT_REMAP_PROG_SHARED_MATRIXA      2
#       define GAMUT_REMAP_PROG_SHARED_MATRIXB      3

/* REGAMMA_CONTROL */
#       define REGAMMA_BYPASS                       0
#       define REGAMMA_SRGB_24                      1
#       define REGAMMA_XVYCC_222                    2
#       define REGAMMA_PROG_A                       3
#       define REGAMMA_PROG_B                       4


/* INPUT_GAMMA_CONTROL */
#       define INPUT_GAMMA_USE_LUT                  0
#       define INPUT_GAMMA_BYPASS                   1
#       define INPUT_GAMMA_SRGB_24                  2
#       define INPUT_GAMMA_XVYCC_222                3

#define MC_SEQ_MISC0__MT__MASK	0xf0000000
#define MC_SEQ_MISC0__MT__GDDR1  0x10000000
#define MC_SEQ_MISC0__MT__DDR2   0x20000000
#define MC_SEQ_MISC0__MT__GDDR3  0x30000000
#define MC_SEQ_MISC0__MT__GDDR4  0x40000000
#define MC_SEQ_MISC0__MT__GDDR5  0x50000000
#define MC_SEQ_MISC0__MT__HBM    0x60000000
#define MC_SEQ_MISC0__MT__DDR3   0xB0000000

#define CP_INT_CNTL_RING__TIME_STAMP_INT_ENABLE_MASK 0x4000000
#define PACKET3_SEM_WAIT_ON_SIGNAL    (0x1 << 12)
#define PACKET3_SEM_SEL_SIGNAL	    (0x6 << 29)
#define PACKET3_SEM_SEL_WAIT	    (0x7 << 29)

#define AMDGPU_PCIE_INDEX	0xc
#define AMDGPU_PCIE_DATA	0xd

#define PCIE_BUS_CLK    10000
#define TCLK            (PCIE_BUS_CLK / 10)
#define	PCIE_PORT_INDEX					0xe
#define	PCIE_PORT_DATA					0xf
#define EVERGREEN_PIF_PHY0_INDEX                        0x8
#define EVERGREEN_PIF_PHY0_DATA                         0xc
#define EVERGREEN_PIF_PHY1_INDEX                        0x10
#define EVERGREEN_PIF_PHY1_DATA				0x14

/* Discrete VCE clocks */
#define CG_VCEPLL_FUNC_CNTL                             0xc0030600
#define    VCEPLL_RESET_MASK                            0x00000001
#define    VCEPLL_SLEEP_MASK                            0x00000002
#define    VCEPLL_BYPASS_EN_MASK                        0x00000004
#define    VCEPLL_CTLREQ_MASK                           0x00000008
#define    VCEPLL_VCO_MODE_MASK                         0x00000600
#define    VCEPLL_REF_DIV_MASK                          0x003F0000
#define    VCEPLL_CTLACK_MASK                           0x40000000
#define    VCEPLL_CTLACK2_MASK                          0x80000000

#define CG_VCEPLL_FUNC_CNTL_2                           0xc0030601
#define    VCEPLL_PDIV_A(x)                             ((x) << 0)
#define    VCEPLL_PDIV_A_MASK                           0x0000007F
#define    VCEPLL_PDIV_B(x)                             ((x) << 8)
#define    VCEPLL_PDIV_B_MASK                           0x00007F00
#define    EVCLK_SRC_SEL(x)                             ((x) << 20)
#define    EVCLK_SRC_SEL_MASK                           0x01F00000
#define    ECCLK_SRC_SEL(x)                             ((x) << 25)
#define    ECCLK_SRC_SEL_MASK                           0x3E000000

#define CG_VCEPLL_FUNC_CNTL_3                           0xc0030602
#define    VCEPLL_FB_DIV(x)                             ((x) << 0)
#define    VCEPLL_FB_DIV_MASK                           0x01FFFFFF

#define CG_VCEPLL_FUNC_CNTL_4                           0xc0030603

#define CG_VCEPLL_FUNC_CNTL_5                           0xc0030604
#define CG_VCEPLL_SPREAD_SPECTRUM                       0xc0030606
#define    VCEPLL_SSEN_MASK                             0x00000001


#endif
