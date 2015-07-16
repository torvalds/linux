/*
 * Copyright 2009 Advanced Micro Devices, Inc.
 * Copyright 2009 Red Hat Inc.
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
#ifndef RV770_H
#define RV770_H

#define R7XX_MAX_SH_GPRS           256
#define R7XX_MAX_TEMP_GPRS         16
#define R7XX_MAX_SH_THREADS        256
#define R7XX_MAX_SH_STACK_ENTRIES  4096
#define R7XX_MAX_BACKENDS          8
#define R7XX_MAX_BACKENDS_MASK     0xff
#define R7XX_MAX_SIMDS             16
#define R7XX_MAX_SIMDS_MASK        0xffff
#define R7XX_MAX_PIPES             8
#define R7XX_MAX_PIPES_MASK        0xff

/* discrete uvd clocks */
#define CG_UPLL_FUNC_CNTL				0x718
#	define UPLL_RESET_MASK				0x00000001
#	define UPLL_SLEEP_MASK				0x00000002
#	define UPLL_BYPASS_EN_MASK			0x00000004
#	define UPLL_CTLREQ_MASK				0x00000008
#	define UPLL_REF_DIV(x)				((x) << 16)
#	define UPLL_REF_DIV_MASK			0x003F0000
#	define UPLL_CTLACK_MASK				0x40000000
#	define UPLL_CTLACK2_MASK			0x80000000
#define CG_UPLL_FUNC_CNTL_2				0x71c
#	define UPLL_SW_HILEN(x)				((x) << 0)
#	define UPLL_SW_LOLEN(x)				((x) << 4)
#	define UPLL_SW_HILEN2(x)			((x) << 8)
#	define UPLL_SW_LOLEN2(x)			((x) << 12)
#	define UPLL_SW_MASK				0x0000FFFF
#	define VCLK_SRC_SEL(x)				((x) << 20)
#	define VCLK_SRC_SEL_MASK			0x01F00000
#	define DCLK_SRC_SEL(x)				((x) << 25)
#	define DCLK_SRC_SEL_MASK			0x3E000000
#define CG_UPLL_FUNC_CNTL_3				0x720
#	define UPLL_FB_DIV(x)				((x) << 0)
#	define UPLL_FB_DIV_MASK				0x01FFFFFF

/* pm registers */
#define	SMC_SRAM_ADDR					0x200
#define		SMC_SRAM_AUTO_INC_DIS				(1 << 16)
#define	SMC_SRAM_DATA					0x204
#define	SMC_IO						0x208
#define		SMC_RST_N					(1 << 0)
#define		SMC_STOP_MODE					(1 << 2)
#define		SMC_CLK_EN					(1 << 11)
#define	SMC_MSG						0x20c
#define		HOST_SMC_MSG(x)					((x) << 0)
#define		HOST_SMC_MSG_MASK				(0xff << 0)
#define		HOST_SMC_MSG_SHIFT				0
#define		HOST_SMC_RESP(x)				((x) << 8)
#define		HOST_SMC_RESP_MASK				(0xff << 8)
#define		HOST_SMC_RESP_SHIFT				8
#define		SMC_HOST_MSG(x)					((x) << 16)
#define		SMC_HOST_MSG_MASK				(0xff << 16)
#define		SMC_HOST_MSG_SHIFT				16
#define		SMC_HOST_RESP(x)				((x) << 24)
#define		SMC_HOST_RESP_MASK				(0xff << 24)
#define		SMC_HOST_RESP_SHIFT				24

#define	SMC_ISR_FFD8_FFDB				0x218

#define	CG_SPLL_FUNC_CNTL				0x600
#define		SPLL_RESET				(1 << 0)
#define		SPLL_SLEEP				(1 << 1)
#define		SPLL_DIVEN				(1 << 2)
#define		SPLL_BYPASS_EN				(1 << 3)
#define		SPLL_REF_DIV(x)				((x) << 4)
#define		SPLL_REF_DIV_MASK			(0x3f << 4)
#define		SPLL_HILEN(x)				((x) << 12)
#define		SPLL_HILEN_MASK				(0xf << 12)
#define		SPLL_LOLEN(x)				((x) << 16)
#define		SPLL_LOLEN_MASK				(0xf << 16)
#define	CG_SPLL_FUNC_CNTL_2				0x604
#define		SCLK_MUX_SEL(x)				((x) << 0)
#define		SCLK_MUX_SEL_MASK			(0x1ff << 0)
#define		SCLK_MUX_UPDATE				(1 << 26)
#define	CG_SPLL_FUNC_CNTL_3				0x608
#define		SPLL_FB_DIV(x)				((x) << 0)
#define		SPLL_FB_DIV_MASK			(0x3ffffff << 0)
#define		SPLL_DITHEN				(1 << 28)
#define	CG_SPLL_STATUS					0x60c
#define		SPLL_CHG_STATUS				(1 << 1)

#define	SPLL_CNTL_MODE					0x610
#define		SPLL_DIV_SYNC				(1 << 5)

#define MPLL_CNTL_MODE                                  0x61c
#       define MPLL_MCLK_SEL                            (1 << 11)
#       define RV730_MPLL_MCLK_SEL                      (1 << 25)

#define	MPLL_AD_FUNC_CNTL				0x624
#define		CLKF(x)					((x) << 0)
#define		CLKF_MASK				(0x7f << 0)
#define		CLKR(x)					((x) << 7)
#define		CLKR_MASK				(0x1f << 7)
#define		CLKFRAC(x)				((x) << 12)
#define		CLKFRAC_MASK				(0x1f << 12)
#define		YCLK_POST_DIV(x)			((x) << 17)
#define		YCLK_POST_DIV_MASK			(3 << 17)
#define		IBIAS(x)				((x) << 20)
#define		IBIAS_MASK				(0x3ff << 20)
#define		RESET					(1 << 30)
#define		PDNB					(1 << 31)
#define	MPLL_AD_FUNC_CNTL_2				0x628
#define		BYPASS					(1 << 19)
#define		BIAS_GEN_PDNB				(1 << 24)
#define		RESET_EN				(1 << 25)
#define		VCO_MODE				(1 << 29)
#define	MPLL_DQ_FUNC_CNTL				0x62c
#define	MPLL_DQ_FUNC_CNTL_2				0x630

#define GENERAL_PWRMGT                                  0x63c
#       define GLOBAL_PWRMGT_EN                         (1 << 0)
#       define STATIC_PM_EN                             (1 << 1)
#       define THERMAL_PROTECTION_DIS                   (1 << 2)
#       define THERMAL_PROTECTION_TYPE                  (1 << 3)
#       define ENABLE_GEN2PCIE                          (1 << 4)
#       define ENABLE_GEN2XSP                           (1 << 5)
#       define SW_SMIO_INDEX(x)                         ((x) << 6)
#       define SW_SMIO_INDEX_MASK                       (3 << 6)
#       define SW_SMIO_INDEX_SHIFT                      6
#       define LOW_VOLT_D2_ACPI                         (1 << 8)
#       define LOW_VOLT_D3_ACPI                         (1 << 9)
#       define VOLT_PWRMGT_EN                           (1 << 10)
#       define BACKBIAS_PAD_EN                          (1 << 18)
#       define BACKBIAS_VALUE                           (1 << 19)
#       define DYN_SPREAD_SPECTRUM_EN                   (1 << 23)
#       define AC_DC_SW                                 (1 << 24)

#define CG_TPC                                            0x640
#define SCLK_PWRMGT_CNTL                                  0x644
#       define SCLK_PWRMGT_OFF                            (1 << 0)
#       define SCLK_LOW_D1                                (1 << 1)
#       define FIR_RESET                                  (1 << 4)
#       define FIR_FORCE_TREND_SEL                        (1 << 5)
#       define FIR_TREND_MODE                             (1 << 6)
#       define DYN_GFX_CLK_OFF_EN                         (1 << 7)
#       define GFX_CLK_FORCE_ON                           (1 << 8)
#       define GFX_CLK_REQUEST_OFF                        (1 << 9)
#       define GFX_CLK_FORCE_OFF                          (1 << 10)
#       define GFX_CLK_OFF_ACPI_D1                        (1 << 11)
#       define GFX_CLK_OFF_ACPI_D2                        (1 << 12)
#       define GFX_CLK_OFF_ACPI_D3                        (1 << 13)
#define	MCLK_PWRMGT_CNTL				0x648
#       define DLL_SPEED(x)				((x) << 0)
#       define DLL_SPEED_MASK				(0x1f << 0)
#       define MPLL_PWRMGT_OFF                          (1 << 5)
#       define DLL_READY                                (1 << 6)
#       define MC_INT_CNTL                              (1 << 7)
#       define MRDCKA0_SLEEP                            (1 << 8)
#       define MRDCKA1_SLEEP                            (1 << 9)
#       define MRDCKB0_SLEEP                            (1 << 10)
#       define MRDCKB1_SLEEP                            (1 << 11)
#       define MRDCKC0_SLEEP                            (1 << 12)
#       define MRDCKC1_SLEEP                            (1 << 13)
#       define MRDCKD0_SLEEP                            (1 << 14)
#       define MRDCKD1_SLEEP                            (1 << 15)
#       define MRDCKA0_RESET                            (1 << 16)
#       define MRDCKA1_RESET                            (1 << 17)
#       define MRDCKB0_RESET                            (1 << 18)
#       define MRDCKB1_RESET                            (1 << 19)
#       define MRDCKC0_RESET                            (1 << 20)
#       define MRDCKC1_RESET                            (1 << 21)
#       define MRDCKD0_RESET                            (1 << 22)
#       define MRDCKD1_RESET                            (1 << 23)
#       define DLL_READY_READ                           (1 << 24)
#       define USE_DISPLAY_GAP                          (1 << 25)
#       define USE_DISPLAY_URGENT_NORMAL                (1 << 26)
#       define MPLL_TURNOFF_D2                          (1 << 28)
#define	DLL_CNTL					0x64c
#       define MRDCKA0_BYPASS                           (1 << 24)
#       define MRDCKA1_BYPASS                           (1 << 25)
#       define MRDCKB0_BYPASS                           (1 << 26)
#       define MRDCKB1_BYPASS                           (1 << 27)
#       define MRDCKC0_BYPASS                           (1 << 28)
#       define MRDCKC1_BYPASS                           (1 << 29)
#       define MRDCKD0_BYPASS                           (1 << 30)
#       define MRDCKD1_BYPASS                           (1 << 31)

#define MPLL_TIME                                         0x654
#       define MPLL_LOCK_TIME(x)			((x) << 0)
#       define MPLL_LOCK_TIME_MASK			(0xffff << 0)
#       define MPLL_RESET_TIME(x)			((x) << 16)
#       define MPLL_RESET_TIME_MASK			(0xffff << 16)

#define CG_CLKPIN_CNTL                                    0x660
#       define MUX_TCLK_TO_XCLK                           (1 << 8)
#       define XTALIN_DIVIDE                              (1 << 9)

#define TARGET_AND_CURRENT_PROFILE_INDEX                  0x66c
#       define CURRENT_PROFILE_INDEX_MASK                 (0xf << 4)
#       define CURRENT_PROFILE_INDEX_SHIFT                4

#define S0_VID_LOWER_SMIO_CNTL                            0x678
#define S1_VID_LOWER_SMIO_CNTL                            0x67c
#define S2_VID_LOWER_SMIO_CNTL                            0x680
#define S3_VID_LOWER_SMIO_CNTL                            0x684

#define CG_FTV                                            0x690
#define CG_FFCT_0                                         0x694
#       define UTC_0(x)                                   ((x) << 0)
#       define UTC_0_MASK                                 (0x3ff << 0)
#       define DTC_0(x)                                   ((x) << 10)
#       define DTC_0_MASK                                 (0x3ff << 10)

#define CG_BSP                                          0x6d0
#       define BSP(x)					((x) << 0)
#       define BSP_MASK					(0xffff << 0)
#       define BSU(x)					((x) << 16)
#       define BSU_MASK					(0xf << 16)
#define CG_AT                                           0x6d4
#       define CG_R(x)					((x) << 0)
#       define CG_R_MASK				(0xffff << 0)
#       define CG_L(x)					((x) << 16)
#       define CG_L_MASK				(0xffff << 16)
#define CG_GIT                                          0x6d8
#       define CG_GICST(x)                              ((x) << 0)
#       define CG_GICST_MASK                            (0xffff << 0)
#       define CG_GIPOT(x)                              ((x) << 16)
#       define CG_GIPOT_MASK                            (0xffff << 16)

#define CG_SSP                                            0x6e8
#       define SST(x)                                     ((x) << 0)
#       define SST_MASK                                   (0xffff << 0)
#       define SSTU(x)                                    ((x) << 16)
#       define SSTU_MASK                                  (0xf << 16)

#define CG_DISPLAY_GAP_CNTL                               0x714
#       define DISP1_GAP(x)                               ((x) << 0)
#       define DISP1_GAP_MASK                             (3 << 0)
#       define DISP2_GAP(x)                               ((x) << 2)
#       define DISP2_GAP_MASK                             (3 << 2)
#       define VBI_TIMER_COUNT(x)                         ((x) << 4)
#       define VBI_TIMER_COUNT_MASK                       (0x3fff << 4)
#       define VBI_TIMER_UNIT(x)                          ((x) << 20)
#       define VBI_TIMER_UNIT_MASK                        (7 << 20)
#       define DISP1_GAP_MCHG(x)                          ((x) << 24)
#       define DISP1_GAP_MCHG_MASK                        (3 << 24)
#       define DISP2_GAP_MCHG(x)                          ((x) << 26)
#       define DISP2_GAP_MCHG_MASK                        (3 << 26)

#define	CG_SPLL_SPREAD_SPECTRUM				0x790
#define		SSEN					(1 << 0)
#define		CLKS(x)					((x) << 4)
#define		CLKS_MASK				(0xfff << 4)
#define	CG_SPLL_SPREAD_SPECTRUM_2			0x794
#define		CLKV(x)					((x) << 0)
#define		CLKV_MASK				(0x3ffffff << 0)
#define	CG_MPLL_SPREAD_SPECTRUM				0x798
#define CG_UPLL_SPREAD_SPECTRUM				0x79c
#	define SSEN_MASK				0x00000001

#define CG_CGTT_LOCAL_0                                   0x7d0
#define CG_CGTT_LOCAL_1                                   0x7d4

#define BIOS_SCRATCH_4                                    0x1734

#define MC_SEQ_MISC0                                      0x2a00
#define         MC_SEQ_MISC0_GDDR5_SHIFT                  28
#define         MC_SEQ_MISC0_GDDR5_MASK                   0xf0000000
#define         MC_SEQ_MISC0_GDDR5_VALUE                  5

#define MC_ARB_SQM_RATIO                                  0x2770
#define		STATE0(x)				((x) << 0)
#define		STATE0_MASK				(0xff << 0)
#define		STATE1(x)				((x) << 8)
#define		STATE1_MASK				(0xff << 8)
#define		STATE2(x)				((x) << 16)
#define		STATE2_MASK				(0xff << 16)
#define		STATE3(x)				((x) << 24)
#define		STATE3_MASK				(0xff << 24)

#define	MC_ARB_RFSH_RATE				0x27b0
#define		POWERMODE0(x)				((x) << 0)
#define		POWERMODE0_MASK				(0xff << 0)
#define		POWERMODE1(x)				((x) << 8)
#define		POWERMODE1_MASK				(0xff << 8)
#define		POWERMODE2(x)				((x) << 16)
#define		POWERMODE2_MASK				(0xff << 16)
#define		POWERMODE3(x)				((x) << 24)
#define		POWERMODE3_MASK				(0xff << 24)

#define CGTS_SM_CTRL_REG                                  0x9150

/* Registers */
#define	CB_COLOR0_BASE					0x28040
#define	CB_COLOR1_BASE					0x28044
#define	CB_COLOR2_BASE					0x28048
#define	CB_COLOR3_BASE					0x2804C
#define	CB_COLOR4_BASE					0x28050
#define	CB_COLOR5_BASE					0x28054
#define	CB_COLOR6_BASE					0x28058
#define	CB_COLOR7_BASE					0x2805C
#define	CB_COLOR7_FRAG					0x280FC

#define	CC_GC_SHADER_PIPE_CONFIG			0x8950
#define	CC_RB_BACKEND_DISABLE				0x98F4
#define		BACKEND_DISABLE(x)				((x) << 16)
#define	CC_SYS_RB_BACKEND_DISABLE			0x3F88

#define	CGTS_SYS_TCC_DISABLE				0x3F90
#define	CGTS_TCC_DISABLE				0x9148
#define	CGTS_USER_SYS_TCC_DISABLE			0x3F94
#define	CGTS_USER_TCC_DISABLE				0x914C

#define	CONFIG_MEMSIZE					0x5428

#define	CP_ME_CNTL					0x86D8
#define		CP_ME_HALT					(1 << 28)
#define		CP_PFP_HALT					(1 << 26)
#define	CP_ME_RAM_DATA					0xC160
#define	CP_ME_RAM_RADDR					0xC158
#define	CP_ME_RAM_WADDR					0xC15C
#define CP_MEQ_THRESHOLDS				0x8764
#define		STQ_SPLIT(x)					((x) << 0)
#define	CP_PERFMON_CNTL					0x87FC
#define	CP_PFP_UCODE_ADDR				0xC150
#define	CP_PFP_UCODE_DATA				0xC154
#define	CP_QUEUE_THRESHOLDS				0x8760
#define		ROQ_IB1_START(x)				((x) << 0)
#define		ROQ_IB2_START(x)				((x) << 8)
#define	CP_RB_CNTL					0xC104
#define		RB_BUFSZ(x)					((x) << 0)
#define		RB_BLKSZ(x)					((x) << 8)
#define		RB_NO_UPDATE					(1 << 27)
#define		RB_RPTR_WR_ENA					(1 << 31)
#define		BUF_SWAP_32BIT					(2 << 16)
#define	CP_RB_RPTR					0x8700
#define	CP_RB_RPTR_ADDR					0xC10C
#define	CP_RB_RPTR_ADDR_HI				0xC110
#define	CP_RB_RPTR_WR					0xC108
#define	CP_RB_WPTR					0xC114
#define	CP_RB_WPTR_ADDR					0xC118
#define	CP_RB_WPTR_ADDR_HI				0xC11C
#define	CP_RB_WPTR_DELAY				0x8704
#define	CP_SEM_WAIT_TIMER				0x85BC

#define	DB_DEBUG3					0x98B0
#define		DB_CLK_OFF_DELAY(x)				((x) << 11)
#define DB_DEBUG4					0x9B8C
#define		DISABLE_TILE_COVERED_FOR_PS_ITER		(1 << 6)

#define	DCP_TILING_CONFIG				0x6CA0
#define		PIPE_TILING(x)					((x) << 1)
#define 	BANK_TILING(x)					((x) << 4)
#define		GROUP_SIZE(x)					((x) << 6)
#define		ROW_TILING(x)					((x) << 8)
#define		BANK_SWAPS(x)					((x) << 11)
#define		SAMPLE_SPLIT(x)					((x) << 14)
#define		BACKEND_MAP(x)					((x) << 16)

#define GB_TILING_CONFIG				0x98F0
#define     PIPE_TILING__SHIFT              1
#define     PIPE_TILING__MASK               0x0000000e

#define DMA_TILING_CONFIG                               0x3ec8
#define DMA_TILING_CONFIG2                              0xd0b8

/* RV730 only */
#define UVD_UDEC_TILING_CONFIG                          0xef40
#define UVD_UDEC_DB_TILING_CONFIG                       0xef44
#define UVD_UDEC_DBW_TILING_CONFIG                      0xef48

#define	GC_USER_SHADER_PIPE_CONFIG			0x8954
#define		INACTIVE_QD_PIPES(x)				((x) << 8)
#define		INACTIVE_QD_PIPES_MASK				0x0000FF00
#define		INACTIVE_QD_PIPES_SHIFT			    8
#define		INACTIVE_SIMDS(x)				((x) << 16)
#define		INACTIVE_SIMDS_MASK				0x00FF0000

#define	GRBM_CNTL					0x8000
#define		GRBM_READ_TIMEOUT(x)				((x) << 0)
#define	GRBM_SOFT_RESET					0x8020
#define		SOFT_RESET_CP					(1<<0)
#define	GRBM_STATUS					0x8010
#define		CMDFIFO_AVAIL_MASK				0x0000000F
#define		GUI_ACTIVE					(1<<31)
#define	GRBM_STATUS2					0x8014

#define	CG_THERMAL_CTRL					0x72C
#define 	DPM_EVENT_SRC(x)			((x) << 0)
#define 	DPM_EVENT_SRC_MASK			(7 << 0)
#define		DIG_THERM_DPM(x)			((x) << 14)
#define		DIG_THERM_DPM_MASK			0x003FC000
#define		DIG_THERM_DPM_SHIFT			14

#define	CG_THERMAL_INT					0x734
#define		DIG_THERM_INTH(x)			((x) << 8)
#define		DIG_THERM_INTH_MASK			0x0000FF00
#define		DIG_THERM_INTH_SHIFT			8
#define		DIG_THERM_INTL(x)			((x) << 16)
#define		DIG_THERM_INTL_MASK			0x00FF0000
#define		DIG_THERM_INTL_SHIFT			16
#define 	THERM_INT_MASK_HIGH			(1 << 24)
#define 	THERM_INT_MASK_LOW			(1 << 25)

#define	CG_MULT_THERMAL_STATUS				0x740
#define		ASIC_T(x)			        ((x) << 16)
#define		ASIC_T_MASK			        0x3FF0000
#define		ASIC_T_SHIFT			        16

#define	HDP_HOST_PATH_CNTL				0x2C00
#define	HDP_NONSURFACE_BASE				0x2C04
#define	HDP_NONSURFACE_INFO				0x2C08
#define	HDP_NONSURFACE_SIZE				0x2C0C
#define HDP_REG_COHERENCY_FLUSH_CNTL			0x54A0
#define	HDP_TILING_CONFIG				0x2F3C
#define HDP_DEBUG1                                      0x2F34

#define MC_SHARED_CHMAP						0x2004
#define		NOOFCHAN_SHIFT					12
#define		NOOFCHAN_MASK					0x00003000
#define MC_SHARED_CHREMAP					0x2008

#define	MC_ARB_RAMCFG					0x2760
#define		NOOFBANK_SHIFT					0
#define		NOOFBANK_MASK					0x00000003
#define		NOOFRANK_SHIFT					2
#define		NOOFRANK_MASK					0x00000004
#define		NOOFROWS_SHIFT					3
#define		NOOFROWS_MASK					0x00000038
#define		NOOFCOLS_SHIFT					6
#define		NOOFCOLS_MASK					0x000000C0
#define		CHANSIZE_SHIFT					8
#define		CHANSIZE_MASK					0x00000100
#define		BURSTLENGTH_SHIFT				9
#define		BURSTLENGTH_MASK				0x00000200
#define		CHANSIZE_OVERRIDE				(1 << 11)
#define	MC_VM_AGP_TOP					0x2028
#define	MC_VM_AGP_BOT					0x202C
#define	MC_VM_AGP_BASE					0x2030
#define	MC_VM_FB_LOCATION				0x2024
#define	MC_VM_MB_L1_TLB0_CNTL				0x2234
#define	MC_VM_MB_L1_TLB1_CNTL				0x2238
#define	MC_VM_MB_L1_TLB2_CNTL				0x223C
#define	MC_VM_MB_L1_TLB3_CNTL				0x2240
#define		ENABLE_L1_TLB					(1 << 0)
#define		ENABLE_L1_FRAGMENT_PROCESSING			(1 << 1)
#define		SYSTEM_ACCESS_MODE_PA_ONLY			(0 << 3)
#define		SYSTEM_ACCESS_MODE_USE_SYS_MAP			(1 << 3)
#define		SYSTEM_ACCESS_MODE_IN_SYS			(2 << 3)
#define		SYSTEM_ACCESS_MODE_NOT_IN_SYS			(3 << 3)
#define		SYSTEM_APERTURE_UNMAPPED_ACCESS_PASS_THRU	(0 << 5)
#define		EFFECTIVE_L1_TLB_SIZE(x)			((x)<<15)
#define		EFFECTIVE_L1_QUEUE_SIZE(x)			((x)<<18)
#define	MC_VM_MD_L1_TLB0_CNTL				0x2654
#define	MC_VM_MD_L1_TLB1_CNTL				0x2658
#define	MC_VM_MD_L1_TLB2_CNTL				0x265C
#define	MC_VM_MD_L1_TLB3_CNTL				0x2698
#define	MC_VM_SYSTEM_APERTURE_DEFAULT_ADDR		0x203C
#define	MC_VM_SYSTEM_APERTURE_HIGH_ADDR			0x2038
#define	MC_VM_SYSTEM_APERTURE_LOW_ADDR			0x2034

#define	PA_CL_ENHANCE					0x8A14
#define		CLIP_VTX_REORDER_ENA				(1 << 0)
#define		NUM_CLIP_SEQ(x)					((x) << 1)
#define PA_SC_AA_CONFIG					0x28C04
#define PA_SC_CLIPRECT_RULE				0x2820C
#define	PA_SC_EDGERULE					0x28230
#define	PA_SC_FIFO_SIZE					0x8BCC
#define		SC_PRIM_FIFO_SIZE(x)				((x) << 0)
#define		SC_HIZ_TILE_FIFO_SIZE(x)			((x) << 12)
#define	PA_SC_FORCE_EOV_MAX_CNTS			0x8B24
#define		FORCE_EOV_MAX_CLK_CNT(x)			((x)<<0)
#define		FORCE_EOV_MAX_REZ_CNT(x)			((x)<<16)
#define PA_SC_LINE_STIPPLE				0x28A0C
#define	PA_SC_LINE_STIPPLE_STATE			0x8B10
#define PA_SC_MODE_CNTL					0x28A4C
#define	PA_SC_MULTI_CHIP_CNTL				0x8B20
#define		SC_EARLYZ_TILE_FIFO_SIZE(x)			((x) << 20)

#define	SCRATCH_REG0					0x8500
#define	SCRATCH_REG1					0x8504
#define	SCRATCH_REG2					0x8508
#define	SCRATCH_REG3					0x850C
#define	SCRATCH_REG4					0x8510
#define	SCRATCH_REG5					0x8514
#define	SCRATCH_REG6					0x8518
#define	SCRATCH_REG7					0x851C
#define	SCRATCH_UMSK					0x8540
#define	SCRATCH_ADDR					0x8544

#define	SMX_SAR_CTL0					0xA008
#define	SMX_DC_CTL0					0xA020
#define		USE_HASH_FUNCTION				(1 << 0)
#define		CACHE_DEPTH(x)					((x) << 1)
#define		FLUSH_ALL_ON_EVENT				(1 << 10)
#define		STALL_ON_EVENT					(1 << 11)
#define	SMX_EVENT_CTL					0xA02C
#define		ES_FLUSH_CTL(x)					((x) << 0)
#define		GS_FLUSH_CTL(x)					((x) << 3)
#define		ACK_FLUSH_CTL(x)				((x) << 6)
#define		SYNC_FLUSH_CTL					(1 << 8)

#define	SPI_CONFIG_CNTL					0x9100
#define		GPR_WRITE_PRIORITY(x)				((x) << 0)
#define		DISABLE_INTERP_1				(1 << 5)
#define	SPI_CONFIG_CNTL_1				0x913C
#define		VTX_DONE_DELAY(x)				((x) << 0)
#define		INTERP_ONE_PRIM_PER_ROW				(1 << 4)
#define	SPI_INPUT_Z					0x286D8
#define	SPI_PS_IN_CONTROL_0				0x286CC
#define		NUM_INTERP(x)					((x)<<0)
#define		POSITION_ENA					(1<<8)
#define		POSITION_CENTROID				(1<<9)
#define		POSITION_ADDR(x)				((x)<<10)
#define		PARAM_GEN(x)					((x)<<15)
#define		PARAM_GEN_ADDR(x)				((x)<<19)
#define		BARYC_SAMPLE_CNTL(x)				((x)<<26)
#define		PERSP_GRADIENT_ENA				(1<<28)
#define		LINEAR_GRADIENT_ENA				(1<<29)
#define		POSITION_SAMPLE					(1<<30)
#define		BARYC_AT_SAMPLE_ENA				(1<<31)

#define	SQ_CONFIG					0x8C00
#define		VC_ENABLE					(1 << 0)
#define		EXPORT_SRC_C					(1 << 1)
#define		DX9_CONSTS					(1 << 2)
#define		ALU_INST_PREFER_VECTOR				(1 << 3)
#define		DX10_CLAMP					(1 << 4)
#define		CLAUSE_SEQ_PRIO(x)				((x) << 8)
#define		PS_PRIO(x)					((x) << 24)
#define		VS_PRIO(x)					((x) << 26)
#define		GS_PRIO(x)					((x) << 28)
#define	SQ_DYN_GPR_SIZE_SIMD_AB_0			0x8DB0
#define		SIMDA_RING0(x)					((x)<<0)
#define		SIMDA_RING1(x)					((x)<<8)
#define		SIMDB_RING0(x)					((x)<<16)
#define		SIMDB_RING1(x)					((x)<<24)
#define	SQ_DYN_GPR_SIZE_SIMD_AB_1			0x8DB4
#define	SQ_DYN_GPR_SIZE_SIMD_AB_2			0x8DB8
#define	SQ_DYN_GPR_SIZE_SIMD_AB_3			0x8DBC
#define	SQ_DYN_GPR_SIZE_SIMD_AB_4			0x8DC0
#define	SQ_DYN_GPR_SIZE_SIMD_AB_5			0x8DC4
#define	SQ_DYN_GPR_SIZE_SIMD_AB_6			0x8DC8
#define	SQ_DYN_GPR_SIZE_SIMD_AB_7			0x8DCC
#define		ES_PRIO(x)					((x) << 30)
#define	SQ_GPR_RESOURCE_MGMT_1				0x8C04
#define		NUM_PS_GPRS(x)					((x) << 0)
#define		NUM_VS_GPRS(x)					((x) << 16)
#define		DYN_GPR_ENABLE					(1 << 27)
#define		NUM_CLAUSE_TEMP_GPRS(x)				((x) << 28)
#define	SQ_GPR_RESOURCE_MGMT_2				0x8C08
#define		NUM_GS_GPRS(x)					((x) << 0)
#define		NUM_ES_GPRS(x)					((x) << 16)
#define	SQ_MS_FIFO_SIZES				0x8CF0
#define		CACHE_FIFO_SIZE(x)				((x) << 0)
#define		FETCH_FIFO_HIWATER(x)				((x) << 8)
#define		DONE_FIFO_HIWATER(x)				((x) << 16)
#define		ALU_UPDATE_FIFO_HIWATER(x)			((x) << 24)
#define	SQ_STACK_RESOURCE_MGMT_1			0x8C10
#define		NUM_PS_STACK_ENTRIES(x)				((x) << 0)
#define		NUM_VS_STACK_ENTRIES(x)				((x) << 16)
#define	SQ_STACK_RESOURCE_MGMT_2			0x8C14
#define		NUM_GS_STACK_ENTRIES(x)				((x) << 0)
#define		NUM_ES_STACK_ENTRIES(x)				((x) << 16)
#define	SQ_THREAD_RESOURCE_MGMT				0x8C0C
#define		NUM_PS_THREADS(x)				((x) << 0)
#define		NUM_VS_THREADS(x)				((x) << 8)
#define		NUM_GS_THREADS(x)				((x) << 16)
#define		NUM_ES_THREADS(x)				((x) << 24)

#define	SX_DEBUG_1					0x9058
#define		ENABLE_NEW_SMX_ADDRESS				(1 << 16)
#define	SX_EXPORT_BUFFER_SIZES				0x900C
#define		COLOR_BUFFER_SIZE(x)				((x) << 0)
#define		POSITION_BUFFER_SIZE(x)				((x) << 8)
#define		SMX_BUFFER_SIZE(x)				((x) << 16)
#define	SX_MISC						0x28350

#define	TA_CNTL_AUX					0x9508
#define		DISABLE_CUBE_WRAP				(1 << 0)
#define		DISABLE_CUBE_ANISO				(1 << 1)
#define		SYNC_GRADIENT					(1 << 24)
#define		SYNC_WALKER					(1 << 25)
#define		SYNC_ALIGNER					(1 << 26)
#define		BILINEAR_PRECISION_6_BIT			(0 << 31)
#define		BILINEAR_PRECISION_8_BIT			(1 << 31)

#define	TCP_CNTL					0x9610
#define	TCP_CHAN_STEER					0x9614

#define	VC_ENHANCE					0x9714

#define	VGT_CACHE_INVALIDATION				0x88C4
#define		CACHE_INVALIDATION(x)				((x)<<0)
#define			VC_ONLY						0
#define			TC_ONLY						1
#define			VC_AND_TC					2
#define		AUTO_INVLD_EN(x)				((x) << 6)
#define			NO_AUTO						0
#define			ES_AUTO						1
#define			GS_AUTO						2
#define			ES_AND_GS_AUTO					3
#define	VGT_ES_PER_GS					0x88CC
#define	VGT_GS_PER_ES					0x88C8
#define	VGT_GS_PER_VS					0x88E8
#define	VGT_GS_VERTEX_REUSE				0x88D4
#define	VGT_NUM_INSTANCES				0x8974
#define	VGT_OUT_DEALLOC_CNTL				0x28C5C
#define		DEALLOC_DIST_MASK				0x0000007F
#define	VGT_STRMOUT_EN					0x28AB0
#define	VGT_VERTEX_REUSE_BLOCK_CNTL			0x28C58
#define		VTX_REUSE_DEPTH_MASK				0x000000FF

#define VM_CONTEXT0_CNTL				0x1410
#define		ENABLE_CONTEXT					(1 << 0)
#define		PAGE_TABLE_DEPTH(x)				(((x) & 3) << 1)
#define		RANGE_PROTECTION_FAULT_ENABLE_DEFAULT		(1 << 4)
#define	VM_CONTEXT0_PAGE_TABLE_BASE_ADDR		0x153C
#define	VM_CONTEXT0_PAGE_TABLE_END_ADDR			0x157C
#define	VM_CONTEXT0_PAGE_TABLE_START_ADDR		0x155C
#define VM_CONTEXT0_PROTECTION_FAULT_DEFAULT_ADDR	0x1518
#define VM_L2_CNTL					0x1400
#define		ENABLE_L2_CACHE					(1 << 0)
#define		ENABLE_L2_FRAGMENT_PROCESSING			(1 << 1)
#define		ENABLE_L2_PTE_CACHE_LRU_UPDATE_BY_WRITE		(1 << 9)
#define		EFFECTIVE_L2_QUEUE_SIZE(x)			(((x) & 7) << 14)
#define VM_L2_CNTL2					0x1404
#define		INVALIDATE_ALL_L1_TLBS				(1 << 0)
#define		INVALIDATE_L2_CACHE				(1 << 1)
#define VM_L2_CNTL3					0x1408
#define		BANK_SELECT(x)					((x) << 0)
#define		CACHE_UPDATE_MODE(x)				((x) << 6)
#define	VM_L2_STATUS					0x140C
#define		L2_BUSY						(1 << 0)

#define	WAIT_UNTIL					0x8040

/* async DMA */
#define DMA_RB_RPTR                                       0xd008
#define DMA_RB_WPTR                                       0xd00c

/* async DMA packets */
#define DMA_PACKET(cmd, t, s, n)	((((cmd) & 0xF) << 28) |	\
					 (((t) & 0x1) << 23) |		\
					 (((s) & 0x1) << 22) |		\
					 (((n) & 0xFFFF) << 0))
/* async DMA Packet types */
#define	DMA_PACKET_WRITE				  0x2
#define	DMA_PACKET_COPY					  0x3
#define	DMA_PACKET_INDIRECT_BUFFER			  0x4
#define	DMA_PACKET_SEMAPHORE				  0x5
#define	DMA_PACKET_FENCE				  0x6
#define	DMA_PACKET_TRAP					  0x7
#define	DMA_PACKET_CONSTANT_FILL			  0xd
#define	DMA_PACKET_NOP					  0xf


#define	SRBM_STATUS				        0x0E50

/* DCE 3.2 HDMI */
#define HDMI_CONTROL                         0x7400
#       define HDMI_KEEPOUT_MODE             (1 << 0)
#       define HDMI_PACKET_GEN_VERSION       (1 << 4) /* 0 = r6xx compat */
#       define HDMI_ERROR_ACK                (1 << 8)
#       define HDMI_ERROR_MASK               (1 << 9)
#define HDMI_STATUS                          0x7404
#       define HDMI_ACTIVE_AVMUTE            (1 << 0)
#       define HDMI_AUDIO_PACKET_ERROR       (1 << 16)
#       define HDMI_VBI_PACKET_ERROR         (1 << 20)
#define HDMI_AUDIO_PACKET_CONTROL            0x7408
#       define HDMI_AUDIO_DELAY_EN(x)        (((x) & 3) << 4)
#       define HDMI_AUDIO_PACKETS_PER_LINE(x)  (((x) & 0x1f) << 16)
#define HDMI_ACR_PACKET_CONTROL              0x740c
#       define HDMI_ACR_SEND                 (1 << 0)
#       define HDMI_ACR_CONT                 (1 << 1)
#       define HDMI_ACR_SELECT(x)            (((x) & 3) << 4)
#       define HDMI_ACR_HW                   0
#       define HDMI_ACR_32                   1
#       define HDMI_ACR_44                   2
#       define HDMI_ACR_48                   3
#       define HDMI_ACR_SOURCE               (1 << 8) /* 0 - hw; 1 - cts value */
#       define HDMI_ACR_AUTO_SEND            (1 << 12)
#define HDMI_VBI_PACKET_CONTROL              0x7410
#       define HDMI_NULL_SEND                (1 << 0)
#       define HDMI_GC_SEND                  (1 << 4)
#       define HDMI_GC_CONT                  (1 << 5) /* 0 - once; 1 - every frame */
#define HDMI_INFOFRAME_CONTROL0              0x7414
#       define HDMI_AVI_INFO_SEND            (1 << 0)
#       define HDMI_AVI_INFO_CONT            (1 << 1)
#       define HDMI_AUDIO_INFO_SEND          (1 << 4)
#       define HDMI_AUDIO_INFO_CONT          (1 << 5)
#       define HDMI_MPEG_INFO_SEND           (1 << 8)
#       define HDMI_MPEG_INFO_CONT           (1 << 9)
#define HDMI_INFOFRAME_CONTROL1              0x7418
#       define HDMI_AVI_INFO_LINE(x)         (((x) & 0x3f) << 0)
#       define HDMI_AUDIO_INFO_LINE(x)       (((x) & 0x3f) << 8)
#       define HDMI_MPEG_INFO_LINE(x)        (((x) & 0x3f) << 16)
#define HDMI_GENERIC_PACKET_CONTROL          0x741c
#       define HDMI_GENERIC0_SEND            (1 << 0)
#       define HDMI_GENERIC0_CONT            (1 << 1)
#       define HDMI_GENERIC1_SEND            (1 << 4)
#       define HDMI_GENERIC1_CONT            (1 << 5)
#       define HDMI_GENERIC0_LINE(x)         (((x) & 0x3f) << 16)
#       define HDMI_GENERIC1_LINE(x)         (((x) & 0x3f) << 24)
#define HDMI_GC                              0x7428
#       define HDMI_GC_AVMUTE                (1 << 0)
#define AFMT_AUDIO_PACKET_CONTROL2           0x742c
#       define AFMT_AUDIO_LAYOUT_OVRD        (1 << 0)
#       define AFMT_AUDIO_LAYOUT_SELECT      (1 << 1)
#       define AFMT_60958_CS_SOURCE          (1 << 4)
#       define AFMT_AUDIO_CHANNEL_ENABLE(x)  (((x) & 0xff) << 8)
#       define AFMT_DP_AUDIO_STREAM_ID(x)    (((x) & 0xff) << 16)
#define AFMT_AVI_INFO0                       0x7454
#       define AFMT_AVI_INFO_CHECKSUM(x)     (((x) & 0xff) << 0)
#       define AFMT_AVI_INFO_S(x)            (((x) & 3) << 8)
#       define AFMT_AVI_INFO_B(x)            (((x) & 3) << 10)
#       define AFMT_AVI_INFO_A(x)            (((x) & 1) << 12)
#       define AFMT_AVI_INFO_Y(x)            (((x) & 3) << 13)
#       define AFMT_AVI_INFO_Y_RGB           0
#       define AFMT_AVI_INFO_Y_YCBCR422      1
#       define AFMT_AVI_INFO_Y_YCBCR444      2
#       define AFMT_AVI_INFO_Y_A_B_S(x)      (((x) & 0xff) << 8)
#       define AFMT_AVI_INFO_R(x)            (((x) & 0xf) << 16)
#       define AFMT_AVI_INFO_M(x)            (((x) & 0x3) << 20)
#       define AFMT_AVI_INFO_C(x)            (((x) & 0x3) << 22)
#       define AFMT_AVI_INFO_C_M_R(x)        (((x) & 0xff) << 16)
#       define AFMT_AVI_INFO_SC(x)           (((x) & 0x3) << 24)
#       define AFMT_AVI_INFO_Q(x)            (((x) & 0x3) << 26)
#       define AFMT_AVI_INFO_EC(x)           (((x) & 0x3) << 28)
#       define AFMT_AVI_INFO_ITC(x)          (((x) & 0x1) << 31)
#       define AFMT_AVI_INFO_ITC_EC_Q_SC(x)  (((x) & 0xff) << 24)
#define AFMT_AVI_INFO1                       0x7458
#       define AFMT_AVI_INFO_VIC(x)          (((x) & 0x7f) << 0) /* don't use avi infoframe v1 */
#       define AFMT_AVI_INFO_PR(x)           (((x) & 0xf) << 8) /* don't use avi infoframe v1 */
#       define AFMT_AVI_INFO_TOP(x)          (((x) & 0xffff) << 16)
#define AFMT_AVI_INFO2                       0x745c
#       define AFMT_AVI_INFO_BOTTOM(x)       (((x) & 0xffff) << 0)
#       define AFMT_AVI_INFO_LEFT(x)         (((x) & 0xffff) << 16)
#define AFMT_AVI_INFO3                       0x7460
#       define AFMT_AVI_INFO_RIGHT(x)        (((x) & 0xffff) << 0)
#       define AFMT_AVI_INFO_VERSION(x)      (((x) & 3) << 24)
#define AFMT_MPEG_INFO0                      0x7464
#       define AFMT_MPEG_INFO_CHECKSUM(x)    (((x) & 0xff) << 0)
#       define AFMT_MPEG_INFO_MB0(x)         (((x) & 0xff) << 8)
#       define AFMT_MPEG_INFO_MB1(x)         (((x) & 0xff) << 16)
#       define AFMT_MPEG_INFO_MB2(x)         (((x) & 0xff) << 24)
#define AFMT_MPEG_INFO1                      0x7468
#       define AFMT_MPEG_INFO_MB3(x)         (((x) & 0xff) << 0)
#       define AFMT_MPEG_INFO_MF(x)          (((x) & 3) << 8)
#       define AFMT_MPEG_INFO_FR(x)          (((x) & 1) << 12)
#define AFMT_GENERIC0_HDR                    0x746c
#define AFMT_GENERIC0_0                      0x7470
#define AFMT_GENERIC0_1                      0x7474
#define AFMT_GENERIC0_2                      0x7478
#define AFMT_GENERIC0_3                      0x747c
#define AFMT_GENERIC0_4                      0x7480
#define AFMT_GENERIC0_5                      0x7484
#define AFMT_GENERIC0_6                      0x7488
#define AFMT_GENERIC1_HDR                    0x748c
#define AFMT_GENERIC1_0                      0x7490
#define AFMT_GENERIC1_1                      0x7494
#define AFMT_GENERIC1_2                      0x7498
#define AFMT_GENERIC1_3                      0x749c
#define AFMT_GENERIC1_4                      0x74a0
#define AFMT_GENERIC1_5                      0x74a4
#define AFMT_GENERIC1_6                      0x74a8
#define HDMI_ACR_32_0                        0x74ac
#       define HDMI_ACR_CTS_32(x)            (((x) & 0xfffff) << 12)
#define HDMI_ACR_32_1                        0x74b0
#       define HDMI_ACR_N_32(x)              (((x) & 0xfffff) << 0)
#define HDMI_ACR_44_0                        0x74b4
#       define HDMI_ACR_CTS_44(x)            (((x) & 0xfffff) << 12)
#define HDMI_ACR_44_1                        0x74b8
#       define HDMI_ACR_N_44(x)              (((x) & 0xfffff) << 0)
#define HDMI_ACR_48_0                        0x74bc
#       define HDMI_ACR_CTS_48(x)            (((x) & 0xfffff) << 12)
#define HDMI_ACR_48_1                        0x74c0
#       define HDMI_ACR_N_48(x)              (((x) & 0xfffff) << 0)
#define HDMI_ACR_STATUS_0                    0x74c4
#define HDMI_ACR_STATUS_1                    0x74c8
#define AFMT_AUDIO_INFO0                     0x74cc
#       define AFMT_AUDIO_INFO_CHECKSUM(x)   (((x) & 0xff) << 0)
#       define AFMT_AUDIO_INFO_CC(x)         (((x) & 7) << 8)
#       define AFMT_AUDIO_INFO_CHECKSUM_OFFSET(x)   (((x) & 0xff) << 16)
#define AFMT_AUDIO_INFO1                     0x74d0
#       define AFMT_AUDIO_INFO_CA(x)         (((x) & 0xff) << 0)
#       define AFMT_AUDIO_INFO_LSV(x)        (((x) & 0xf) << 11)
#       define AFMT_AUDIO_INFO_DM_INH(x)     (((x) & 1) << 15)
#       define AFMT_AUDIO_INFO_DM_INH_LSV(x) (((x) & 0xff) << 8)
#define AFMT_60958_0                         0x74d4
#       define AFMT_60958_CS_A(x)            (((x) & 1) << 0)
#       define AFMT_60958_CS_B(x)            (((x) & 1) << 1)
#       define AFMT_60958_CS_C(x)            (((x) & 1) << 2)
#       define AFMT_60958_CS_D(x)            (((x) & 3) << 3)
#       define AFMT_60958_CS_MODE(x)         (((x) & 3) << 6)
#       define AFMT_60958_CS_CATEGORY_CODE(x)      (((x) & 0xff) << 8)
#       define AFMT_60958_CS_SOURCE_NUMBER(x)      (((x) & 0xf) << 16)
#       define AFMT_60958_CS_CHANNEL_NUMBER_L(x)   (((x) & 0xf) << 20)
#       define AFMT_60958_CS_SAMPLING_FREQUENCY(x) (((x) & 0xf) << 24)
#       define AFMT_60958_CS_CLOCK_ACCURACY(x)     (((x) & 3) << 28)
#define AFMT_60958_1                         0x74d8
#       define AFMT_60958_CS_WORD_LENGTH(x)  (((x) & 0xf) << 0)
#       define AFMT_60958_CS_ORIGINAL_SAMPLING_FREQUENCY(x)   (((x) & 0xf) << 4)
#       define AFMT_60958_CS_VALID_L(x)      (((x) & 1) << 16)
#       define AFMT_60958_CS_VALID_R(x)      (((x) & 1) << 18)
#       define AFMT_60958_CS_CHANNEL_NUMBER_R(x)   (((x) & 0xf) << 20)
#define AFMT_AUDIO_CRC_CONTROL               0x74dc
#       define AFMT_AUDIO_CRC_EN             (1 << 0)
#define AFMT_RAMP_CONTROL0                   0x74e0
#       define AFMT_RAMP_MAX_COUNT(x)        (((x) & 0xffffff) << 0)
#       define AFMT_RAMP_DATA_SIGN           (1 << 31)
#define AFMT_RAMP_CONTROL1                   0x74e4
#       define AFMT_RAMP_MIN_COUNT(x)        (((x) & 0xffffff) << 0)
#       define AFMT_AUDIO_TEST_CH_DISABLE(x) (((x) & 0xff) << 24)
#define AFMT_RAMP_CONTROL2                   0x74e8
#       define AFMT_RAMP_INC_COUNT(x)        (((x) & 0xffffff) << 0)
#define AFMT_RAMP_CONTROL3                   0x74ec
#       define AFMT_RAMP_DEC_COUNT(x)        (((x) & 0xffffff) << 0)
#define AFMT_60958_2                         0x74f0
#       define AFMT_60958_CS_CHANNEL_NUMBER_2(x)   (((x) & 0xf) << 0)
#       define AFMT_60958_CS_CHANNEL_NUMBER_3(x)   (((x) & 0xf) << 4)
#       define AFMT_60958_CS_CHANNEL_NUMBER_4(x)   (((x) & 0xf) << 8)
#       define AFMT_60958_CS_CHANNEL_NUMBER_5(x)   (((x) & 0xf) << 12)
#       define AFMT_60958_CS_CHANNEL_NUMBER_6(x)   (((x) & 0xf) << 16)
#       define AFMT_60958_CS_CHANNEL_NUMBER_7(x)   (((x) & 0xf) << 20)
#define AFMT_STATUS                          0x7600
#       define AFMT_AUDIO_ENABLE             (1 << 4)
#       define AFMT_AZ_FORMAT_WTRIG          (1 << 28)
#       define AFMT_AZ_FORMAT_WTRIG_INT      (1 << 29)
#       define AFMT_AZ_AUDIO_ENABLE_CHG      (1 << 30)
#define AFMT_AUDIO_PACKET_CONTROL            0x7604
#       define AFMT_AUDIO_SAMPLE_SEND        (1 << 0)
#       define AFMT_AUDIO_TEST_EN            (1 << 12)
#       define AFMT_AUDIO_CHANNEL_SWAP       (1 << 24)
#       define AFMT_60958_CS_UPDATE          (1 << 26)
#       define AFMT_AZ_AUDIO_ENABLE_CHG_MASK (1 << 27)
#       define AFMT_AZ_FORMAT_WTRIG_MASK     (1 << 28)
#       define AFMT_AZ_FORMAT_WTRIG_ACK      (1 << 29)
#       define AFMT_AZ_AUDIO_ENABLE_CHG_ACK  (1 << 30)
#define AFMT_VBI_PACKET_CONTROL              0x7608
#       define AFMT_GENERIC0_UPDATE          (1 << 2)
#define AFMT_INFOFRAME_CONTROL0              0x760c
#       define AFMT_AUDIO_INFO_SOURCE        (1 << 6) /* 0 - sound block; 1 - hdmi regs */
#       define AFMT_AUDIO_INFO_UPDATE        (1 << 7)
#       define AFMT_MPEG_INFO_UPDATE         (1 << 10)
#define AFMT_GENERIC0_7                      0x7610
/* second instance starts at 0x7800 */
#define HDMI_OFFSET0                      (0x7400 - 0x7400)
#define HDMI_OFFSET1                      (0x7800 - 0x7400)

/* DCE3.2 ELD audio interface */
#define AZ_F0_CODEC_PIN0_CONTROL_AUDIO_DESCRIPTOR0        0x71c8 /* LPCM */
#define AZ_F0_CODEC_PIN0_CONTROL_AUDIO_DESCRIPTOR1        0x71cc /* AC3 */
#define AZ_F0_CODEC_PIN0_CONTROL_AUDIO_DESCRIPTOR2        0x71d0 /* MPEG1 */
#define AZ_F0_CODEC_PIN0_CONTROL_AUDIO_DESCRIPTOR3        0x71d4 /* MP3 */
#define AZ_F0_CODEC_PIN0_CONTROL_AUDIO_DESCRIPTOR4        0x71d8 /* MPEG2 */
#define AZ_F0_CODEC_PIN0_CONTROL_AUDIO_DESCRIPTOR5        0x71dc /* AAC */
#define AZ_F0_CODEC_PIN0_CONTROL_AUDIO_DESCRIPTOR6        0x71e0 /* DTS */
#define AZ_F0_CODEC_PIN0_CONTROL_AUDIO_DESCRIPTOR7        0x71e4 /* ATRAC */
#define AZ_F0_CODEC_PIN0_CONTROL_AUDIO_DESCRIPTOR8        0x71e8 /* one bit audio - leave at 0 (default) */
#define AZ_F0_CODEC_PIN0_CONTROL_AUDIO_DESCRIPTOR9        0x71ec /* Dolby Digital */
#define AZ_F0_CODEC_PIN0_CONTROL_AUDIO_DESCRIPTOR10       0x71f0 /* DTS-HD */
#define AZ_F0_CODEC_PIN0_CONTROL_AUDIO_DESCRIPTOR11       0x71f4 /* MAT-MLP */
#define AZ_F0_CODEC_PIN0_CONTROL_AUDIO_DESCRIPTOR12       0x71f8 /* DTS */
#define AZ_F0_CODEC_PIN0_CONTROL_AUDIO_DESCRIPTOR13       0x71fc /* WMA Pro */
#       define MAX_CHANNELS(x)                            (((x) & 0x7) << 0)
/* max channels minus one.  7 = 8 channels */
#       define SUPPORTED_FREQUENCIES(x)                   (((x) & 0xff) << 8)
#       define DESCRIPTOR_BYTE_2(x)                       (((x) & 0xff) << 16)
#       define SUPPORTED_FREQUENCIES_STEREO(x)            (((x) & 0xff) << 24) /* LPCM only */
/* SUPPORTED_FREQUENCIES, SUPPORTED_FREQUENCIES_STEREO
 * bit0 = 32 kHz
 * bit1 = 44.1 kHz
 * bit2 = 48 kHz
 * bit3 = 88.2 kHz
 * bit4 = 96 kHz
 * bit5 = 176.4 kHz
 * bit6 = 192 kHz
 */

#define AZ_HOT_PLUG_CONTROL                               0x7300
#       define AZ_FORCE_CODEC_WAKE                        (1 << 0)
#       define PIN0_JACK_DETECTION_ENABLE                 (1 << 4)
#       define PIN1_JACK_DETECTION_ENABLE                 (1 << 5)
#       define PIN2_JACK_DETECTION_ENABLE                 (1 << 6)
#       define PIN3_JACK_DETECTION_ENABLE                 (1 << 7)
#       define PIN0_UNSOLICITED_RESPONSE_ENABLE           (1 << 8)
#       define PIN1_UNSOLICITED_RESPONSE_ENABLE           (1 << 9)
#       define PIN2_UNSOLICITED_RESPONSE_ENABLE           (1 << 10)
#       define PIN3_UNSOLICITED_RESPONSE_ENABLE           (1 << 11)
#       define CODEC_HOT_PLUG_ENABLE                      (1 << 12)
#       define PIN0_AUDIO_ENABLED                         (1 << 24)
#       define PIN1_AUDIO_ENABLED                         (1 << 25)
#       define PIN2_AUDIO_ENABLED                         (1 << 26)
#       define PIN3_AUDIO_ENABLED                         (1 << 27)
#       define AUDIO_ENABLED                              (1 << 31)


#define D1GRPH_PRIMARY_SURFACE_ADDRESS                    0x6110
#define D1GRPH_PRIMARY_SURFACE_ADDRESS_HIGH               0x6914
#define D2GRPH_PRIMARY_SURFACE_ADDRESS_HIGH               0x6114
#define D1GRPH_SECONDARY_SURFACE_ADDRESS                  0x6118
#define D1GRPH_SECONDARY_SURFACE_ADDRESS_HIGH             0x691c
#define D2GRPH_SECONDARY_SURFACE_ADDRESS_HIGH             0x611c

/* PCIE indirect regs */
#define PCIE_P_CNTL                                       0x40
#       define P_PLL_PWRDN_IN_L1L23                       (1 << 3)
#       define P_PLL_BUF_PDNB                             (1 << 4)
#       define P_PLL_PDNB                                 (1 << 9)
#       define P_ALLOW_PRX_FRONTEND_SHUTOFF               (1 << 12)
/* PCIE PORT regs */
#define PCIE_LC_CNTL                                      0xa0
#       define LC_L0S_INACTIVITY(x)                       ((x) << 8)
#       define LC_L0S_INACTIVITY_MASK                     (0xf << 8)
#       define LC_L0S_INACTIVITY_SHIFT                    8
#       define LC_L1_INACTIVITY(x)                        ((x) << 12)
#       define LC_L1_INACTIVITY_MASK                      (0xf << 12)
#       define LC_L1_INACTIVITY_SHIFT                     12
#       define LC_PMI_TO_L1_DIS                           (1 << 16)
#       define LC_ASPM_TO_L1_DIS                          (1 << 24)
#define PCIE_LC_TRAINING_CNTL                             0xa1 /* PCIE_P */
#define PCIE_LC_LINK_WIDTH_CNTL                           0xa2 /* PCIE_P */
#       define LC_LINK_WIDTH_SHIFT                        0
#       define LC_LINK_WIDTH_MASK                         0x7
#       define LC_LINK_WIDTH_X0                           0
#       define LC_LINK_WIDTH_X1                           1
#       define LC_LINK_WIDTH_X2                           2
#       define LC_LINK_WIDTH_X4                           3
#       define LC_LINK_WIDTH_X8                           4
#       define LC_LINK_WIDTH_X16                          6
#       define LC_LINK_WIDTH_RD_SHIFT                     4
#       define LC_LINK_WIDTH_RD_MASK                      0x70
#       define LC_RECONFIG_ARC_MISSING_ESCAPE             (1 << 7)
#       define LC_RECONFIG_NOW                            (1 << 8)
#       define LC_RENEGOTIATION_SUPPORT                   (1 << 9)
#       define LC_RENEGOTIATE_EN                          (1 << 10)
#       define LC_SHORT_RECONFIG_EN                       (1 << 11)
#       define LC_UPCONFIGURE_SUPPORT                     (1 << 12)
#       define LC_UPCONFIGURE_DIS                         (1 << 13)
#define PCIE_LC_SPEED_CNTL                                0xa4 /* PCIE_P */
#       define LC_GEN2_EN_STRAP                           (1 << 0)
#       define LC_TARGET_LINK_SPEED_OVERRIDE_EN           (1 << 1)
#       define LC_FORCE_EN_HW_SPEED_CHANGE                (1 << 5)
#       define LC_FORCE_DIS_HW_SPEED_CHANGE               (1 << 6)
#       define LC_SPEED_CHANGE_ATTEMPTS_ALLOWED_MASK      (0x3 << 8)
#       define LC_SPEED_CHANGE_ATTEMPTS_ALLOWED_SHIFT     3
#       define LC_CURRENT_DATA_RATE                       (1 << 11)
#       define LC_HW_VOLTAGE_IF_CONTROL(x)                ((x) << 12)
#       define LC_HW_VOLTAGE_IF_CONTROL_MASK              (3 << 12)
#       define LC_HW_VOLTAGE_IF_CONTROL_SHIFT             12
#       define LC_VOLTAGE_TIMER_SEL_MASK                  (0xf << 14)
#       define LC_CLR_FAILED_SPD_CHANGE_CNT               (1 << 21)
#       define LC_OTHER_SIDE_EVER_SENT_GEN2               (1 << 23)
#       define LC_OTHER_SIDE_SUPPORTS_GEN2                (1 << 24)
#define MM_CFGREGS_CNTL                                   0x544c
#       define MM_WR_TO_CFG_EN                            (1 << 3)
#define LINK_CNTL2                                        0x88 /* F0 */
#       define TARGET_LINK_SPEED_MASK                     (0xf << 0)
#       define SELECTABLE_DEEMPHASIS                      (1 << 6)

/*
 * PM4
 */
#define PACKET0(reg, n)	((RADEON_PACKET_TYPE0 << 30) |			\
			 (((reg) >> 2) & 0xFFFF) |			\
			 ((n) & 0x3FFF) << 16)
#define PACKET3(op, n)	((RADEON_PACKET_TYPE3 << 30) |			\
			 (((op) & 0xFF) << 8) |				\
			 ((n) & 0x3FFF) << 16)

/* UVD */
#define UVD_SEMA_ADDR_LOW				0xef00
#define UVD_SEMA_ADDR_HIGH				0xef04
#define UVD_SEMA_CMD					0xef08
#define UVD_GPCOM_VCPU_CMD				0xef0c
#define UVD_GPCOM_VCPU_DATA0				0xef10
#define UVD_GPCOM_VCPU_DATA1				0xef14

#define UVD_LMI_EXT40_ADDR				0xf498
#define UVD_VCPU_CHIP_ID				0xf4d4
#define UVD_VCPU_CACHE_OFFSET0				0xf4d8
#define UVD_VCPU_CACHE_SIZE0				0xf4dc
#define UVD_VCPU_CACHE_OFFSET1				0xf4e0
#define UVD_VCPU_CACHE_SIZE1				0xf4e4
#define UVD_VCPU_CACHE_OFFSET2				0xf4e8
#define UVD_VCPU_CACHE_SIZE2				0xf4ec
#define UVD_LMI_ADDR_EXT				0xf594

#define UVD_RBC_RB_RPTR					0xf690
#define UVD_RBC_RB_WPTR					0xf694

#define UVD_CONTEXT_ID					0xf6f4

#endif
