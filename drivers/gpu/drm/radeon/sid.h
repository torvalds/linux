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

#define TAHITI_RB_BITMAP_WIDTH_PER_SH  2

#define TAHITI_GB_ADDR_CONFIG_GOLDEN        0x12011003
#define VERDE_GB_ADDR_CONFIG_GOLDEN         0x12010002
#define HAINAN_GB_ADDR_CONFIG_GOLDEN        0x02010001

#define SI_MAX_SH_GPRS           256
#define SI_MAX_TEMP_GPRS         16
#define SI_MAX_SH_THREADS        256
#define SI_MAX_SH_STACK_ENTRIES  4096
#define SI_MAX_FRC_EOV_CNT       16384
#define SI_MAX_BACKENDS          8
#define SI_MAX_BACKENDS_MASK     0xFF
#define SI_MAX_BACKENDS_PER_SE_MASK     0x0F
#define SI_MAX_SIMDS             12
#define SI_MAX_SIMDS_MASK        0x0FFF
#define SI_MAX_SIMDS_PER_SE_MASK        0x00FF
#define SI_MAX_PIPES             8
#define SI_MAX_PIPES_MASK        0xFF
#define SI_MAX_PIPES_PER_SIMD_MASK      0x3F
#define SI_MAX_LDS_NUM           0xFFFF
#define SI_MAX_TCC               16
#define SI_MAX_TCC_MASK          0xFFFF

/* SMC IND accessor regs */
#define SMC_IND_INDEX_0                              0x200
#define SMC_IND_DATA_0                               0x204

#define SMC_IND_ACCESS_CNTL                          0x228
#       define AUTO_INCREMENT_IND_0                  (1 << 0)
#define SMC_MESSAGE_0                                0x22c
#define SMC_RESP_0                                   0x230

/* CG IND registers are accessed via SMC indirect space + SMC_CG_IND_START */
#define SMC_CG_IND_START                    0xc0030000
#define SMC_CG_IND_END                      0xc0040000

#define	CG_CGTT_LOCAL_0				0x400
#define	CG_CGTT_LOCAL_1				0x401

/* SMC IND registers */
#define	SMC_SYSCON_RESET_CNTL				0x80000000
#       define RST_REG                                  (1 << 0)
#define	SMC_SYSCON_CLOCK_CNTL_0				0x80000004
#       define CK_DISABLE                               (1 << 0)
#       define CKEN                                     (1 << 24)

#define VGA_HDP_CONTROL  				0x328
#define		VGA_MEMORY_DISABLE				(1 << 4)

#define DCCG_DISP_SLOW_SELECT_REG                       0x4fc
#define		DCCG_DISP1_SLOW_SELECT(x)		((x) << 0)
#define		DCCG_DISP1_SLOW_SELECT_MASK		(7 << 0)
#define		DCCG_DISP1_SLOW_SELECT_SHIFT		0
#define		DCCG_DISP2_SLOW_SELECT(x)		((x) << 4)
#define		DCCG_DISP2_SLOW_SELECT_MASK		(7 << 4)
#define		DCCG_DISP2_SLOW_SELECT_SHIFT		4

#define	CG_SPLL_FUNC_CNTL				0x600
#define		SPLL_RESET				(1 << 0)
#define		SPLL_SLEEP				(1 << 1)
#define		SPLL_BYPASS_EN				(1 << 3)
#define		SPLL_REF_DIV(x)				((x) << 4)
#define		SPLL_REF_DIV_MASK			(0x3f << 4)
#define		SPLL_PDIV_A(x)				((x) << 20)
#define		SPLL_PDIV_A_MASK			(0x7f << 20)
#define		SPLL_PDIV_A_SHIFT			20
#define	CG_SPLL_FUNC_CNTL_2				0x604
#define		SCLK_MUX_SEL(x)				((x) << 0)
#define		SCLK_MUX_SEL_MASK			(0x1ff << 0)
#define	CG_SPLL_FUNC_CNTL_3				0x608
#define		SPLL_FB_DIV(x)				((x) << 0)
#define		SPLL_FB_DIV_MASK			(0x3ffffff << 0)
#define		SPLL_FB_DIV_SHIFT			0
#define		SPLL_DITHEN				(1 << 28)
#define	CG_SPLL_FUNC_CNTL_4				0x60c

#define	SPLL_CNTL_MODE					0x618
#	define SPLL_REFCLK_SEL(x)			((x) << 8)
#	define SPLL_REFCLK_SEL_MASK			0xFF00

#define	CG_SPLL_SPREAD_SPECTRUM				0x620
#define		SSEN					(1 << 0)
#define		CLK_S(x)				((x) << 4)
#define		CLK_S_MASK				(0xfff << 4)
#define		CLK_S_SHIFT				4
#define	CG_SPLL_SPREAD_SPECTRUM_2			0x624
#define		CLK_V(x)				((x) << 0)
#define		CLK_V_MASK				(0x3ffffff << 0)
#define		CLK_V_SHIFT				0

#define	CG_SPLL_AUTOSCALE_CNTL				0x62c
#       define AUTOSCALE_ON_SS_CLEAR                    (1 << 9)

/* discrete uvd clocks */
#define	CG_UPLL_FUNC_CNTL				0x634
#	define UPLL_RESET_MASK				0x00000001
#	define UPLL_SLEEP_MASK				0x00000002
#	define UPLL_BYPASS_EN_MASK			0x00000004
#	define UPLL_CTLREQ_MASK				0x00000008
#	define UPLL_VCO_MODE_MASK			0x00000600
#	define UPLL_REF_DIV_MASK			0x003F0000
#	define UPLL_CTLACK_MASK				0x40000000
#	define UPLL_CTLACK2_MASK			0x80000000
#define	CG_UPLL_FUNC_CNTL_2				0x638
#	define UPLL_PDIV_A(x)				((x) << 0)
#	define UPLL_PDIV_A_MASK				0x0000007F
#	define UPLL_PDIV_B(x)				((x) << 8)
#	define UPLL_PDIV_B_MASK				0x00007F00
#	define VCLK_SRC_SEL(x)				((x) << 20)
#	define VCLK_SRC_SEL_MASK			0x01F00000
#	define DCLK_SRC_SEL(x)				((x) << 25)
#	define DCLK_SRC_SEL_MASK			0x3E000000
#define	CG_UPLL_FUNC_CNTL_3				0x63C
#	define UPLL_FB_DIV(x)				((x) << 0)
#	define UPLL_FB_DIV_MASK				0x01FFFFFF
#define	CG_UPLL_FUNC_CNTL_4                             0x644
#	define UPLL_SPARE_ISPARE9			0x00020000
#define	CG_UPLL_FUNC_CNTL_5				0x648
#	define RESET_ANTI_MUX_MASK			0x00000200
#define	CG_UPLL_SPREAD_SPECTRUM				0x650
#	define SSEN_MASK				0x00000001

#define	MPLL_BYPASSCLK_SEL				0x65c
#	define MPLL_CLKOUT_SEL(x)			((x) << 8)
#	define MPLL_CLKOUT_SEL_MASK			0xFF00

#define CG_CLKPIN_CNTL                                    0x660
#       define XTALIN_DIVIDE                              (1 << 1)
#       define BCLK_AS_XCLK                               (1 << 2)
#define CG_CLKPIN_CNTL_2                                  0x664
#       define FORCE_BIF_REFCLK_EN                        (1 << 3)
#       define MUX_TCLK_TO_XCLK                           (1 << 8)

#define	THM_CLK_CNTL					0x66c
#	define CMON_CLK_SEL(x)				((x) << 0)
#	define CMON_CLK_SEL_MASK			0xFF
#	define TMON_CLK_SEL(x)				((x) << 8)
#	define TMON_CLK_SEL_MASK			0xFF00
#define	MISC_CLK_CNTL					0x670
#	define DEEP_SLEEP_CLK_SEL(x)			((x) << 0)
#	define DEEP_SLEEP_CLK_SEL_MASK			0xFF
#	define ZCLK_SEL(x)				((x) << 8)
#	define ZCLK_SEL_MASK				0xFF00

#define	CG_THERMAL_CTRL					0x700
#define 	DPM_EVENT_SRC(x)			((x) << 0)
#define 	DPM_EVENT_SRC_MASK			(7 << 0)
#define		DIG_THERM_DPM(x)			((x) << 14)
#define		DIG_THERM_DPM_MASK			0x003FC000
#define		DIG_THERM_DPM_SHIFT			14

#define	CG_THERMAL_INT					0x708
#define		DIG_THERM_INTH(x)			((x) << 8)
#define		DIG_THERM_INTH_MASK			0x0000FF00
#define		DIG_THERM_INTH_SHIFT			8
#define		DIG_THERM_INTL(x)			((x) << 16)
#define		DIG_THERM_INTL_MASK			0x00FF0000
#define		DIG_THERM_INTL_SHIFT			16
#define 	THERM_INT_MASK_HIGH			(1 << 24)
#define 	THERM_INT_MASK_LOW			(1 << 25)

#define	CG_MULT_THERMAL_STATUS					0x714
#define		ASIC_MAX_TEMP(x)				((x) << 0)
#define		ASIC_MAX_TEMP_MASK				0x000001ff
#define		ASIC_MAX_TEMP_SHIFT				0
#define		CTF_TEMP(x)					((x) << 9)
#define		CTF_TEMP_MASK					0x0003fe00
#define		CTF_TEMP_SHIFT					9

#define GENERAL_PWRMGT                                  0x780
#       define GLOBAL_PWRMGT_EN                         (1 << 0)
#       define STATIC_PM_EN                             (1 << 1)
#       define THERMAL_PROTECTION_DIS                   (1 << 2)
#       define THERMAL_PROTECTION_TYPE                  (1 << 3)
#       define SW_SMIO_INDEX(x)                         ((x) << 6)
#       define SW_SMIO_INDEX_MASK                       (1 << 6)
#       define SW_SMIO_INDEX_SHIFT                      6
#       define VOLT_PWRMGT_EN                           (1 << 10)
#       define DYN_SPREAD_SPECTRUM_EN                   (1 << 23)
#define CG_TPC                                            0x784
#define SCLK_PWRMGT_CNTL                                  0x788
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
#       define DYN_LIGHT_SLEEP_EN                         (1 << 14)

#define TARGET_AND_CURRENT_PROFILE_INDEX                  0x798
#       define CURRENT_STATE_INDEX_MASK                   (0xf << 4)
#       define CURRENT_STATE_INDEX_SHIFT                  4

#define CG_FTV                                            0x7bc

#define CG_FFCT_0                                         0x7c0
#       define UTC_0(x)                                   ((x) << 0)
#       define UTC_0_MASK                                 (0x3ff << 0)
#       define DTC_0(x)                                   ((x) << 10)
#       define DTC_0_MASK                                 (0x3ff << 10)

#define CG_BSP                                          0x7fc
#       define BSP(x)					((x) << 0)
#       define BSP_MASK					(0xffff << 0)
#       define BSU(x)					((x) << 16)
#       define BSU_MASK					(0xf << 16)
#define CG_AT                                           0x800
#       define CG_R(x)					((x) << 0)
#       define CG_R_MASK				(0xffff << 0)
#       define CG_L(x)					((x) << 16)
#       define CG_L_MASK				(0xffff << 16)

#define CG_GIT                                          0x804
#       define CG_GICST(x)                              ((x) << 0)
#       define CG_GICST_MASK                            (0xffff << 0)
#       define CG_GIPOT(x)                              ((x) << 16)
#       define CG_GIPOT_MASK                            (0xffff << 16)

#define CG_SSP                                            0x80c
#       define SST(x)                                     ((x) << 0)
#       define SST_MASK                                   (0xffff << 0)
#       define SSTU(x)                                    ((x) << 16)
#       define SSTU_MASK                                  (0xf << 16)

#define CG_DISPLAY_GAP_CNTL                               0x828
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

#define	CG_ULV_CONTROL					0x878
#define	CG_ULV_PARAMETER				0x87c

#define	SMC_SCRATCH0					0x884

#define	CG_CAC_CTRL					0x8b8
#	define CAC_WINDOW(x)				((x) << 0)
#	define CAC_WINDOW_MASK				0x00ffffff

#define DMIF_ADDR_CONFIG  				0xBD4

#define DMIF_ADDR_CALC  				0xC00

#define	PIPE0_DMIF_BUFFER_CONTROL			  0x0ca0
#       define DMIF_BUFFERS_ALLOCATED(x)                  ((x) << 0)
#       define DMIF_BUFFERS_ALLOCATED_COMPLETED           (1 << 4)

#define	SRBM_STATUS				        0xE50
#define		GRBM_RQ_PENDING 			(1 << 5)
#define		VMC_BUSY 				(1 << 8)
#define		MCB_BUSY 				(1 << 9)
#define		MCB_NON_DISPLAY_BUSY 			(1 << 10)
#define		MCC_BUSY 				(1 << 11)
#define		MCD_BUSY 				(1 << 12)
#define		SEM_BUSY 				(1 << 14)
#define		IH_BUSY 				(1 << 17)

#define	SRBM_SOFT_RESET				        0x0E60
#define		SOFT_RESET_BIF				(1 << 1)
#define		SOFT_RESET_DC				(1 << 5)
#define		SOFT_RESET_DMA1				(1 << 6)
#define		SOFT_RESET_GRBM				(1 << 8)
#define		SOFT_RESET_HDP				(1 << 9)
#define		SOFT_RESET_IH				(1 << 10)
#define		SOFT_RESET_MC				(1 << 11)
#define		SOFT_RESET_ROM				(1 << 14)
#define		SOFT_RESET_SEM				(1 << 15)
#define		SOFT_RESET_VMC				(1 << 17)
#define		SOFT_RESET_DMA				(1 << 20)
#define		SOFT_RESET_TST				(1 << 21)
#define		SOFT_RESET_REGBB			(1 << 22)
#define		SOFT_RESET_ORB				(1 << 23)

#define	CC_SYS_RB_BACKEND_DISABLE			0xe80
#define	GC_USER_SYS_RB_BACKEND_DISABLE			0xe84

#define	SRBM_STATUS2				        0x0EC4
#define		DMA_BUSY 				(1 << 5)
#define		DMA1_BUSY 				(1 << 6)

#define VM_L2_CNTL					0x1400
#define		ENABLE_L2_CACHE					(1 << 0)
#define		ENABLE_L2_FRAGMENT_PROCESSING			(1 << 1)
#define		L2_CACHE_PTE_ENDIAN_SWAP_MODE(x)		((x) << 2)
#define		L2_CACHE_PDE_ENDIAN_SWAP_MODE(x)		((x) << 4)
#define		ENABLE_L2_PTE_CACHE_LRU_UPDATE_BY_WRITE		(1 << 9)
#define		ENABLE_L2_PDE0_CACHE_LRU_UPDATE_BY_WRITE	(1 << 10)
#define		EFFECTIVE_L2_QUEUE_SIZE(x)			(((x) & 7) << 15)
#define		CONTEXT1_IDENTITY_ACCESS_MODE(x)		(((x) & 3) << 19)
#define VM_L2_CNTL2					0x1404
#define		INVALIDATE_ALL_L1_TLBS				(1 << 0)
#define		INVALIDATE_L2_CACHE				(1 << 1)
#define		INVALIDATE_CACHE_MODE(x)			((x) << 26)
#define			INVALIDATE_PTE_AND_PDE_CACHES		0
#define			INVALIDATE_ONLY_PTE_CACHES		1
#define			INVALIDATE_ONLY_PDE_CACHES		2
#define VM_L2_CNTL3					0x1408
#define		BANK_SELECT(x)					((x) << 0)
#define		L2_CACHE_UPDATE_MODE(x)				((x) << 6)
#define		L2_CACHE_BIGK_FRAGMENT_SIZE(x)			((x) << 15)
#define		L2_CACHE_BIGK_ASSOCIATIVITY			(1 << 20)
#define	VM_L2_STATUS					0x140C
#define		L2_BUSY						(1 << 0)
#define VM_CONTEXT0_CNTL				0x1410
#define		ENABLE_CONTEXT					(1 << 0)
#define		PAGE_TABLE_DEPTH(x)				(((x) & 3) << 1)
#define		RANGE_PROTECTION_FAULT_ENABLE_INTERRUPT		(1 << 3)
#define		RANGE_PROTECTION_FAULT_ENABLE_DEFAULT		(1 << 4)
#define		DUMMY_PAGE_PROTECTION_FAULT_ENABLE_INTERRUPT	(1 << 6)
#define		DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT	(1 << 7)
#define		PDE0_PROTECTION_FAULT_ENABLE_INTERRUPT		(1 << 9)
#define		PDE0_PROTECTION_FAULT_ENABLE_DEFAULT		(1 << 10)
#define		VALID_PROTECTION_FAULT_ENABLE_INTERRUPT		(1 << 12)
#define		VALID_PROTECTION_FAULT_ENABLE_DEFAULT		(1 << 13)
#define		READ_PROTECTION_FAULT_ENABLE_INTERRUPT		(1 << 15)
#define		READ_PROTECTION_FAULT_ENABLE_DEFAULT		(1 << 16)
#define		WRITE_PROTECTION_FAULT_ENABLE_INTERRUPT		(1 << 18)
#define		WRITE_PROTECTION_FAULT_ENABLE_DEFAULT		(1 << 19)
#define VM_CONTEXT1_CNTL				0x1414
#define VM_CONTEXT0_CNTL2				0x1430
#define VM_CONTEXT1_CNTL2				0x1434
#define	VM_CONTEXT8_PAGE_TABLE_BASE_ADDR		0x1438
#define	VM_CONTEXT9_PAGE_TABLE_BASE_ADDR		0x143c
#define	VM_CONTEXT10_PAGE_TABLE_BASE_ADDR		0x1440
#define	VM_CONTEXT11_PAGE_TABLE_BASE_ADDR		0x1444
#define	VM_CONTEXT12_PAGE_TABLE_BASE_ADDR		0x1448
#define	VM_CONTEXT13_PAGE_TABLE_BASE_ADDR		0x144c
#define	VM_CONTEXT14_PAGE_TABLE_BASE_ADDR		0x1450
#define	VM_CONTEXT15_PAGE_TABLE_BASE_ADDR		0x1454

#define	VM_CONTEXT1_PROTECTION_FAULT_ADDR		0x14FC
#define	VM_CONTEXT1_PROTECTION_FAULT_STATUS		0x14DC
#define		PROTECTIONS_MASK			(0xf << 0)
#define		PROTECTIONS_SHIFT			0
		/* bit 0: range
		 * bit 1: pde0
		 * bit 2: valid
		 * bit 3: read
		 * bit 4: write
		 */
#define		MEMORY_CLIENT_ID_MASK			(0xff << 12)
#define		MEMORY_CLIENT_ID_SHIFT			12
#define		MEMORY_CLIENT_RW_MASK			(1 << 24)
#define		MEMORY_CLIENT_RW_SHIFT			24
#define		FAULT_VMID_MASK				(0xf << 25)
#define		FAULT_VMID_SHIFT			25

#define VM_INVALIDATE_REQUEST				0x1478
#define VM_INVALIDATE_RESPONSE				0x147c

#define VM_CONTEXT0_PROTECTION_FAULT_DEFAULT_ADDR	0x1518
#define VM_CONTEXT1_PROTECTION_FAULT_DEFAULT_ADDR	0x151c

#define	VM_CONTEXT0_PAGE_TABLE_BASE_ADDR		0x153c
#define	VM_CONTEXT1_PAGE_TABLE_BASE_ADDR		0x1540
#define	VM_CONTEXT2_PAGE_TABLE_BASE_ADDR		0x1544
#define	VM_CONTEXT3_PAGE_TABLE_BASE_ADDR		0x1548
#define	VM_CONTEXT4_PAGE_TABLE_BASE_ADDR		0x154c
#define	VM_CONTEXT5_PAGE_TABLE_BASE_ADDR		0x1550
#define	VM_CONTEXT6_PAGE_TABLE_BASE_ADDR		0x1554
#define	VM_CONTEXT7_PAGE_TABLE_BASE_ADDR		0x1558
#define	VM_CONTEXT0_PAGE_TABLE_START_ADDR		0x155c
#define	VM_CONTEXT1_PAGE_TABLE_START_ADDR		0x1560

#define	VM_CONTEXT0_PAGE_TABLE_END_ADDR			0x157C
#define	VM_CONTEXT1_PAGE_TABLE_END_ADDR			0x1580

#define VM_L2_CG           				0x15c0
#define		MC_CG_ENABLE				(1 << 18)
#define		MC_LS_ENABLE				(1 << 19)

#define MC_SHARED_CHMAP						0x2004
#define		NOOFCHAN_SHIFT					12
#define		NOOFCHAN_MASK					0x0000f000
#define MC_SHARED_CHREMAP					0x2008

#define	MC_VM_FB_LOCATION				0x2024
#define	MC_VM_AGP_TOP					0x2028
#define	MC_VM_AGP_BOT					0x202C
#define	MC_VM_AGP_BASE					0x2030
#define	MC_VM_SYSTEM_APERTURE_LOW_ADDR			0x2034
#define	MC_VM_SYSTEM_APERTURE_HIGH_ADDR			0x2038
#define	MC_VM_SYSTEM_APERTURE_DEFAULT_ADDR		0x203C

#define	MC_VM_MX_L1_TLB_CNTL				0x2064
#define		ENABLE_L1_TLB					(1 << 0)
#define		ENABLE_L1_FRAGMENT_PROCESSING			(1 << 1)
#define		SYSTEM_ACCESS_MODE_PA_ONLY			(0 << 3)
#define		SYSTEM_ACCESS_MODE_USE_SYS_MAP			(1 << 3)
#define		SYSTEM_ACCESS_MODE_IN_SYS			(2 << 3)
#define		SYSTEM_ACCESS_MODE_NOT_IN_SYS			(3 << 3)
#define		SYSTEM_APERTURE_UNMAPPED_ACCESS_PASS_THRU	(0 << 5)
#define		ENABLE_ADVANCED_DRIVER_MODEL			(1 << 6)

#define MC_SHARED_BLACKOUT_CNTL           		0x20ac

#define MC_HUB_MISC_HUB_CG           			0x20b8
#define MC_HUB_MISC_VM_CG           			0x20bc

#define MC_HUB_MISC_SIP_CG           			0x20c0

#define MC_XPB_CLK_GAT           			0x2478

#define MC_CITF_MISC_RD_CG           			0x2648
#define MC_CITF_MISC_WR_CG           			0x264c
#define MC_CITF_MISC_VM_CG           			0x2650

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
#define		CHANSIZE_OVERRIDE				(1 << 11)
#define		NOOFGROUPS_SHIFT				12
#define		NOOFGROUPS_MASK					0x00001000

#define	MC_ARB_DRAM_TIMING				0x2774
#define	MC_ARB_DRAM_TIMING2				0x2778

#define MC_ARB_BURST_TIME                               0x2808
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

#define	MC_SEQ_TRAIN_WAKEUP_CNTL			0x28e8
#define		TRAIN_DONE_D0      			(1 << 30)
#define		TRAIN_DONE_D1      			(1 << 31)

#define MC_SEQ_SUP_CNTL           			0x28c8
#define		RUN_MASK      				(1 << 0)
#define MC_SEQ_SUP_PGM           			0x28cc
#define MC_PMG_AUTO_CMD           			0x28d0

#define MC_IO_PAD_CNTL_D0           			0x29d0
#define		MEM_FALL_OUT_CMD      			(1 << 8)

#define MC_SEQ_RAS_TIMING                               0x28a0
#define MC_SEQ_CAS_TIMING                               0x28a4
#define MC_SEQ_MISC_TIMING                              0x28a8
#define MC_SEQ_MISC_TIMING2                             0x28ac
#define MC_SEQ_PMG_TIMING                               0x28b0
#define MC_SEQ_RD_CTL_D0                                0x28b4
#define MC_SEQ_RD_CTL_D1                                0x28b8
#define MC_SEQ_WR_CTL_D0                                0x28bc
#define MC_SEQ_WR_CTL_D1                                0x28c0

#define MC_SEQ_MISC0           				0x2a00
#define 	MC_SEQ_MISC0_VEN_ID_SHIFT               8
#define 	MC_SEQ_MISC0_VEN_ID_MASK                0x00000f00
#define 	MC_SEQ_MISC0_VEN_ID_VALUE               3
#define 	MC_SEQ_MISC0_REV_ID_SHIFT               12
#define 	MC_SEQ_MISC0_REV_ID_MASK                0x0000f000
#define 	MC_SEQ_MISC0_REV_ID_VALUE               1
#define 	MC_SEQ_MISC0_GDDR5_SHIFT                28
#define 	MC_SEQ_MISC0_GDDR5_MASK                 0xf0000000
#define 	MC_SEQ_MISC0_GDDR5_VALUE                5
#define MC_SEQ_MISC1                                    0x2a04
#define MC_SEQ_RESERVE_M                                0x2a08
#define MC_PMG_CMD_EMRS                                 0x2a0c

#define MC_SEQ_IO_DEBUG_INDEX           		0x2a44
#define MC_SEQ_IO_DEBUG_DATA           			0x2a48

#define MC_SEQ_MISC5                                    0x2a54
#define MC_SEQ_MISC6                                    0x2a58

#define MC_SEQ_MISC7                                    0x2a64

#define MC_SEQ_RAS_TIMING_LP                            0x2a6c
#define MC_SEQ_CAS_TIMING_LP                            0x2a70
#define MC_SEQ_MISC_TIMING_LP                           0x2a74
#define MC_SEQ_MISC_TIMING2_LP                          0x2a78
#define MC_SEQ_WR_CTL_D0_LP                             0x2a7c
#define MC_SEQ_WR_CTL_D1_LP                             0x2a80
#define MC_SEQ_PMG_CMD_EMRS_LP                          0x2a84
#define MC_SEQ_PMG_CMD_MRS_LP                           0x2a88

#define MC_PMG_CMD_MRS                                  0x2aac

#define MC_SEQ_RD_CTL_D0_LP                             0x2b1c
#define MC_SEQ_RD_CTL_D1_LP                             0x2b20

#define MC_PMG_CMD_MRS1                                 0x2b44
#define MC_SEQ_PMG_CMD_MRS1_LP                          0x2b48
#define MC_SEQ_PMG_TIMING_LP                            0x2b4c

#define MC_SEQ_WR_CTL_2                                 0x2b54
#define MC_SEQ_WR_CTL_2_LP                              0x2b58
#define MC_PMG_CMD_MRS2                                 0x2b5c
#define MC_SEQ_PMG_CMD_MRS2_LP                          0x2b60

#define	MCLK_PWRMGT_CNTL				0x2ba0
#       define DLL_SPEED(x)				((x) << 0)
#       define DLL_SPEED_MASK				(0x1f << 0)
#       define DLL_READY                                (1 << 6)
#       define MC_INT_CNTL                              (1 << 7)
#       define MRDCK0_PDNB                              (1 << 8)
#       define MRDCK1_PDNB                              (1 << 9)
#       define MRDCK0_RESET                             (1 << 16)
#       define MRDCK1_RESET                             (1 << 17)
#       define DLL_READY_READ                           (1 << 24)
#define	DLL_CNTL					0x2ba4
#       define MRDCK0_BYPASS                            (1 << 24)
#       define MRDCK1_BYPASS                            (1 << 25)

#define	MPLL_FUNC_CNTL					0x2bb4
#define		BWCTRL(x)				((x) << 20)
#define		BWCTRL_MASK				(0xff << 20)
#define	MPLL_FUNC_CNTL_1				0x2bb8
#define		VCO_MODE(x)				((x) << 0)
#define		VCO_MODE_MASK				(3 << 0)
#define		CLKFRAC(x)				((x) << 4)
#define		CLKFRAC_MASK				(0xfff << 4)
#define		CLKF(x)					((x) << 16)
#define		CLKF_MASK				(0xfff << 16)
#define	MPLL_FUNC_CNTL_2				0x2bbc
#define	MPLL_AD_FUNC_CNTL				0x2bc0
#define		YCLK_POST_DIV(x)			((x) << 0)
#define		YCLK_POST_DIV_MASK			(7 << 0)
#define	MPLL_DQ_FUNC_CNTL				0x2bc4
#define		YCLK_SEL(x)				((x) << 4)
#define		YCLK_SEL_MASK				(1 << 4)

#define	MPLL_SS1					0x2bcc
#define		CLKV(x)					((x) << 0)
#define		CLKV_MASK				(0x3ffffff << 0)
#define	MPLL_SS2					0x2bd0
#define		CLKS(x)					((x) << 0)
#define		CLKS_MASK				(0xfff << 0)

#define	HDP_HOST_PATH_CNTL				0x2C00
#define 	CLOCK_GATING_DIS			(1 << 23)
#define	HDP_NONSURFACE_BASE				0x2C04
#define	HDP_NONSURFACE_INFO				0x2C08
#define	HDP_NONSURFACE_SIZE				0x2C0C

#define HDP_ADDR_CONFIG  				0x2F48
#define HDP_MISC_CNTL					0x2F4C
#define 	HDP_FLUSH_INVALIDATE_CACHE			(1 << 0)
#define HDP_MEM_POWER_LS				0x2F50
#define 	HDP_LS_ENABLE				(1 << 0)

#define ATC_MISC_CG           				0x3350

#define IH_RB_CNTL                                        0x3e00
#       define IH_RB_ENABLE                               (1 << 0)
#       define IH_IB_SIZE(x)                              ((x) << 1) /* log2 */
#       define IH_RB_FULL_DRAIN_ENABLE                    (1 << 6)
#       define IH_WPTR_WRITEBACK_ENABLE                   (1 << 8)
#       define IH_WPTR_WRITEBACK_TIMER(x)                 ((x) << 9) /* log2 */
#       define IH_WPTR_OVERFLOW_ENABLE                    (1 << 16)
#       define IH_WPTR_OVERFLOW_CLEAR                     (1 << 31)
#define IH_RB_BASE                                        0x3e04
#define IH_RB_RPTR                                        0x3e08
#define IH_RB_WPTR                                        0x3e0c
#       define RB_OVERFLOW                                (1 << 0)
#       define WPTR_OFFSET_MASK                           0x3fffc
#define IH_RB_WPTR_ADDR_HI                                0x3e10
#define IH_RB_WPTR_ADDR_LO                                0x3e14
#define IH_CNTL                                           0x3e18
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

#define	CONFIG_MEMSIZE					0x5428

#define INTERRUPT_CNTL                                    0x5468
#       define IH_DUMMY_RD_OVERRIDE                       (1 << 0)
#       define IH_DUMMY_RD_EN                             (1 << 1)
#       define IH_REQ_NONSNOOP_EN                         (1 << 3)
#       define GEN_IH_INT_EN                              (1 << 8)
#define INTERRUPT_CNTL2                                   0x546c

#define HDP_MEM_COHERENCY_FLUSH_CNTL			0x5480

#define	BIF_FB_EN						0x5490
#define		FB_READ_EN					(1 << 0)
#define		FB_WRITE_EN					(1 << 1)

#define HDP_REG_COHERENCY_FLUSH_CNTL			0x54A0

/* DCE6 ELD audio interface */
#define AZ_F0_CODEC_ENDPOINT_INDEX                       0x5E00
#       define AZ_ENDPOINT_REG_INDEX(x)                  (((x) & 0xff) << 0)
#       define AZ_ENDPOINT_REG_WRITE_EN                  (1 << 8)
#define AZ_F0_CODEC_ENDPOINT_DATA                        0x5E04

#define AZ_F0_CODEC_PIN_CONTROL_CHANNEL_SPEAKER          0x25
#define		SPEAKER_ALLOCATION(x)			(((x) & 0x7f) << 0)
#define		SPEAKER_ALLOCATION_MASK			(0x7f << 0)
#define		SPEAKER_ALLOCATION_SHIFT		0
#define		HDMI_CONNECTION				(1 << 16)
#define		DP_CONNECTION				(1 << 17)

#define AZ_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR0        0x28 /* LPCM */
#define AZ_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR1        0x29 /* AC3 */
#define AZ_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR2        0x2A /* MPEG1 */
#define AZ_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR3        0x2B /* MP3 */
#define AZ_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR4        0x2C /* MPEG2 */
#define AZ_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR5        0x2D /* AAC */
#define AZ_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR6        0x2E /* DTS */
#define AZ_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR7        0x2F /* ATRAC */
#define AZ_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR8        0x30 /* one bit audio - leave at 0 (default) */
#define AZ_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR9        0x31 /* Dolby Digital */
#define AZ_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR10       0x32 /* DTS-HD */
#define AZ_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR11       0x33 /* MAT-MLP */
#define AZ_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR12       0x34 /* DTS */
#define AZ_F0_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR13       0x35 /* WMA Pro */
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

#define AZ_F0_CODEC_PIN_CONTROL_RESPONSE_LIPSYNC         0x37
#       define VIDEO_LIPSYNC(x)                           (((x) & 0xff) << 0)
#       define AUDIO_LIPSYNC(x)                           (((x) & 0xff) << 8)
/* VIDEO_LIPSYNC, AUDIO_LIPSYNC
 * 0   = invalid
 * x   = legal delay value
 * 255 = sync not supported
 */
#define AZ_F0_CODEC_PIN_CONTROL_RESPONSE_HBR             0x38
#       define HBR_CAPABLE                                (1 << 0) /* enabled by default */

#define AZ_F0_CODEC_PIN_CONTROL_SINK_INFO0               0x3a
#       define MANUFACTURER_ID(x)                        (((x) & 0xffff) << 0)
#       define PRODUCT_ID(x)                             (((x) & 0xffff) << 16)
#define AZ_F0_CODEC_PIN_CONTROL_SINK_INFO1               0x3b
#       define SINK_DESCRIPTION_LEN(x)                   (((x) & 0xff) << 0)
#define AZ_F0_CODEC_PIN_CONTROL_SINK_INFO2               0x3c
#       define PORT_ID0(x)                               (((x) & 0xffffffff) << 0)
#define AZ_F0_CODEC_PIN_CONTROL_SINK_INFO3               0x3d
#       define PORT_ID1(x)                               (((x) & 0xffffffff) << 0)
#define AZ_F0_CODEC_PIN_CONTROL_SINK_INFO4               0x3e
#       define DESCRIPTION0(x)                           (((x) & 0xff) << 0)
#       define DESCRIPTION1(x)                           (((x) & 0xff) << 8)
#       define DESCRIPTION2(x)                           (((x) & 0xff) << 16)
#       define DESCRIPTION3(x)                           (((x) & 0xff) << 24)
#define AZ_F0_CODEC_PIN_CONTROL_SINK_INFO5               0x3f
#       define DESCRIPTION4(x)                           (((x) & 0xff) << 0)
#       define DESCRIPTION5(x)                           (((x) & 0xff) << 8)
#       define DESCRIPTION6(x)                           (((x) & 0xff) << 16)
#       define DESCRIPTION7(x)                           (((x) & 0xff) << 24)
#define AZ_F0_CODEC_PIN_CONTROL_SINK_INFO6               0x40
#       define DESCRIPTION8(x)                           (((x) & 0xff) << 0)
#       define DESCRIPTION9(x)                           (((x) & 0xff) << 8)
#       define DESCRIPTION10(x)                          (((x) & 0xff) << 16)
#       define DESCRIPTION11(x)                          (((x) & 0xff) << 24)
#define AZ_F0_CODEC_PIN_CONTROL_SINK_INFO7               0x41
#       define DESCRIPTION12(x)                          (((x) & 0xff) << 0)
#       define DESCRIPTION13(x)                          (((x) & 0xff) << 8)
#       define DESCRIPTION14(x)                          (((x) & 0xff) << 16)
#       define DESCRIPTION15(x)                          (((x) & 0xff) << 24)
#define AZ_F0_CODEC_PIN_CONTROL_SINK_INFO8               0x42
#       define DESCRIPTION16(x)                          (((x) & 0xff) << 0)
#       define DESCRIPTION17(x)                          (((x) & 0xff) << 8)

#define AZ_F0_CODEC_PIN_CONTROL_HOTPLUG_CONTROL          0x54
#       define AUDIO_ENABLED                             (1 << 31)

#define AZ_F0_CODEC_PIN_CONTROL_RESPONSE_CONFIGURATION_DEFAULT  0x56
#define		PORT_CONNECTIVITY_MASK				(3 << 30)
#define		PORT_CONNECTIVITY_SHIFT				30

#define	DC_LB_MEMORY_SPLIT					0x6b0c
#define		DC_LB_MEMORY_CONFIG(x)				((x) << 20)

#define	PRIORITY_A_CNT						0x6b18
#define		PRIORITY_MARK_MASK				0x7fff
#define		PRIORITY_OFF					(1 << 16)
#define		PRIORITY_ALWAYS_ON				(1 << 20)
#define	PRIORITY_B_CNT						0x6b1c

#define	DPG_PIPE_ARBITRATION_CONTROL3				0x6cc8
#       define LATENCY_WATERMARK_MASK(x)			((x) << 16)
#define	DPG_PIPE_LATENCY_CONTROL				0x6ccc
#       define LATENCY_LOW_WATERMARK(x)				((x) << 0)
#       define LATENCY_HIGH_WATERMARK(x)			((x) << 16)

/* 0x6bb8, 0x77b8, 0x103b8, 0x10fb8, 0x11bb8, 0x127b8 */
#define VLINE_STATUS                                    0x6bb8
#       define VLINE_OCCURRED                           (1 << 0)
#       define VLINE_ACK                                (1 << 4)
#       define VLINE_STAT                               (1 << 12)
#       define VLINE_INTERRUPT                          (1 << 16)
#       define VLINE_INTERRUPT_TYPE                     (1 << 17)
/* 0x6bbc, 0x77bc, 0x103bc, 0x10fbc, 0x11bbc, 0x127bc */
#define VBLANK_STATUS                                   0x6bbc
#       define VBLANK_OCCURRED                          (1 << 0)
#       define VBLANK_ACK                               (1 << 4)
#       define VBLANK_STAT                              (1 << 12)
#       define VBLANK_INTERRUPT                         (1 << 16)
#       define VBLANK_INTERRUPT_TYPE                    (1 << 17)

/* 0x6b40, 0x7740, 0x10340, 0x10f40, 0x11b40, 0x12740 */
#define INT_MASK                                        0x6b40
#       define VBLANK_INT_MASK                          (1 << 0)
#       define VLINE_INT_MASK                           (1 << 4)

#define DISP_INTERRUPT_STATUS                           0x60f4
#       define LB_D1_VLINE_INTERRUPT                    (1 << 2)
#       define LB_D1_VBLANK_INTERRUPT                   (1 << 3)
#       define DC_HPD1_INTERRUPT                        (1 << 17)
#       define DC_HPD1_RX_INTERRUPT                     (1 << 18)
#       define DACA_AUTODETECT_INTERRUPT                (1 << 22)
#       define DACB_AUTODETECT_INTERRUPT                (1 << 23)
#       define DC_I2C_SW_DONE_INTERRUPT                 (1 << 24)
#       define DC_I2C_HW_DONE_INTERRUPT                 (1 << 25)
#define DISP_INTERRUPT_STATUS_CONTINUE                  0x60f8
#       define LB_D2_VLINE_INTERRUPT                    (1 << 2)
#       define LB_D2_VBLANK_INTERRUPT                   (1 << 3)
#       define DC_HPD2_INTERRUPT                        (1 << 17)
#       define DC_HPD2_RX_INTERRUPT                     (1 << 18)
#       define DISP_TIMER_INTERRUPT                     (1 << 24)
#define DISP_INTERRUPT_STATUS_CONTINUE2                 0x60fc
#       define LB_D3_VLINE_INTERRUPT                    (1 << 2)
#       define LB_D3_VBLANK_INTERRUPT                   (1 << 3)
#       define DC_HPD3_INTERRUPT                        (1 << 17)
#       define DC_HPD3_RX_INTERRUPT                     (1 << 18)
#define DISP_INTERRUPT_STATUS_CONTINUE3                 0x6100
#       define LB_D4_VLINE_INTERRUPT                    (1 << 2)
#       define LB_D4_VBLANK_INTERRUPT                   (1 << 3)
#       define DC_HPD4_INTERRUPT                        (1 << 17)
#       define DC_HPD4_RX_INTERRUPT                     (1 << 18)
#define DISP_INTERRUPT_STATUS_CONTINUE4                 0x614c
#       define LB_D5_VLINE_INTERRUPT                    (1 << 2)
#       define LB_D5_VBLANK_INTERRUPT                   (1 << 3)
#       define DC_HPD5_INTERRUPT                        (1 << 17)
#       define DC_HPD5_RX_INTERRUPT                     (1 << 18)
#define DISP_INTERRUPT_STATUS_CONTINUE5                 0x6150
#       define LB_D6_VLINE_INTERRUPT                    (1 << 2)
#       define LB_D6_VBLANK_INTERRUPT                   (1 << 3)
#       define DC_HPD6_INTERRUPT                        (1 << 17)
#       define DC_HPD6_RX_INTERRUPT                     (1 << 18)

/* 0x6858, 0x7458, 0x10058, 0x10c58, 0x11858, 0x12458 */
#define GRPH_INT_STATUS                                 0x6858
#       define GRPH_PFLIP_INT_OCCURRED                  (1 << 0)
#       define GRPH_PFLIP_INT_CLEAR                     (1 << 8)
/* 0x685c, 0x745c, 0x1005c, 0x10c5c, 0x1185c, 0x1245c */
#define	GRPH_INT_CONTROL			        0x685c
#       define GRPH_PFLIP_INT_MASK                      (1 << 0)
#       define GRPH_PFLIP_INT_TYPE                      (1 << 8)

#define	DAC_AUTODETECT_INT_CONTROL			0x67c8

#define DC_HPD1_INT_STATUS                              0x601c
#define DC_HPD2_INT_STATUS                              0x6028
#define DC_HPD3_INT_STATUS                              0x6034
#define DC_HPD4_INT_STATUS                              0x6040
#define DC_HPD5_INT_STATUS                              0x604c
#define DC_HPD6_INT_STATUS                              0x6058
#       define DC_HPDx_INT_STATUS                       (1 << 0)
#       define DC_HPDx_SENSE                            (1 << 1)
#       define DC_HPDx_RX_INT_STATUS                    (1 << 8)

#define DC_HPD1_INT_CONTROL                             0x6020
#define DC_HPD2_INT_CONTROL                             0x602c
#define DC_HPD3_INT_CONTROL                             0x6038
#define DC_HPD4_INT_CONTROL                             0x6044
#define DC_HPD5_INT_CONTROL                             0x6050
#define DC_HPD6_INT_CONTROL                             0x605c
#       define DC_HPDx_INT_ACK                          (1 << 0)
#       define DC_HPDx_INT_POLARITY                     (1 << 8)
#       define DC_HPDx_INT_EN                           (1 << 16)
#       define DC_HPDx_RX_INT_ACK                       (1 << 20)
#       define DC_HPDx_RX_INT_EN                        (1 << 24)

#define DC_HPD1_CONTROL                                   0x6024
#define DC_HPD2_CONTROL                                   0x6030
#define DC_HPD3_CONTROL                                   0x603c
#define DC_HPD4_CONTROL                                   0x6048
#define DC_HPD5_CONTROL                                   0x6054
#define DC_HPD6_CONTROL                                   0x6060
#       define DC_HPDx_CONNECTION_TIMER(x)                ((x) << 0)
#       define DC_HPDx_RX_INT_TIMER(x)                    ((x) << 16)
#       define DC_HPDx_EN                                 (1 << 28)

#define DPG_PIPE_STUTTER_CONTROL                          0x6cd4
#       define STUTTER_ENABLE                             (1 << 0)

/* 0x6e98, 0x7a98, 0x10698, 0x11298, 0x11e98, 0x12a98 */
#define CRTC_STATUS_FRAME_COUNT                         0x6e98

#define AFMT_AUDIO_SRC_CONTROL                          0x713c
#define		AFMT_AUDIO_SRC_SELECT(x)		(((x) & 7) << 0)
/* AFMT_AUDIO_SRC_SELECT
 * 0 = stream0
 * 1 = stream1
 * 2 = stream2
 * 3 = stream3
 * 4 = stream4
 * 5 = stream5
 */

#define	GRBM_CNTL					0x8000
#define		GRBM_READ_TIMEOUT(x)				((x) << 0)

#define	GRBM_STATUS2					0x8008
#define		RLC_RQ_PENDING 					(1 << 0)
#define		RLC_BUSY 					(1 << 8)
#define		TC_BUSY 					(1 << 9)

#define	GRBM_STATUS					0x8010
#define		CMDFIFO_AVAIL_MASK				0x0000000F
#define		RING2_RQ_PENDING				(1 << 4)
#define		SRBM_RQ_PENDING					(1 << 5)
#define		RING1_RQ_PENDING				(1 << 6)
#define		CF_RQ_PENDING					(1 << 7)
#define		PF_RQ_PENDING					(1 << 8)
#define		GDS_DMA_RQ_PENDING				(1 << 9)
#define		GRBM_EE_BUSY					(1 << 10)
#define		DB_CLEAN					(1 << 12)
#define		CB_CLEAN					(1 << 13)
#define		TA_BUSY 					(1 << 14)
#define		GDS_BUSY 					(1 << 15)
#define		VGT_BUSY					(1 << 17)
#define		IA_BUSY_NO_DMA					(1 << 18)
#define		IA_BUSY						(1 << 19)
#define		SX_BUSY 					(1 << 20)
#define		SPI_BUSY					(1 << 22)
#define		BCI_BUSY					(1 << 23)
#define		SC_BUSY 					(1 << 24)
#define		PA_BUSY 					(1 << 25)
#define		DB_BUSY 					(1 << 26)
#define		CP_COHERENCY_BUSY      				(1 << 28)
#define		CP_BUSY 					(1 << 29)
#define		CB_BUSY 					(1 << 30)
#define		GUI_ACTIVE					(1 << 31)
#define	GRBM_STATUS_SE0					0x8014
#define	GRBM_STATUS_SE1					0x8018
#define		SE_DB_CLEAN					(1 << 1)
#define		SE_CB_CLEAN					(1 << 2)
#define		SE_BCI_BUSY					(1 << 22)
#define		SE_VGT_BUSY					(1 << 23)
#define		SE_PA_BUSY					(1 << 24)
#define		SE_TA_BUSY					(1 << 25)
#define		SE_SX_BUSY					(1 << 26)
#define		SE_SPI_BUSY					(1 << 27)
#define		SE_SC_BUSY					(1 << 29)
#define		SE_DB_BUSY					(1 << 30)
#define		SE_CB_BUSY					(1 << 31)

#define	GRBM_SOFT_RESET					0x8020
#define		SOFT_RESET_CP					(1 << 0)
#define		SOFT_RESET_CB					(1 << 1)
#define		SOFT_RESET_RLC					(1 << 2)
#define		SOFT_RESET_DB					(1 << 3)
#define		SOFT_RESET_GDS					(1 << 4)
#define		SOFT_RESET_PA					(1 << 5)
#define		SOFT_RESET_SC					(1 << 6)
#define		SOFT_RESET_BCI					(1 << 7)
#define		SOFT_RESET_SPI					(1 << 8)
#define		SOFT_RESET_SX					(1 << 10)
#define		SOFT_RESET_TC					(1 << 11)
#define		SOFT_RESET_TA					(1 << 12)
#define		SOFT_RESET_VGT					(1 << 14)
#define		SOFT_RESET_IA					(1 << 15)

#define GRBM_GFX_INDEX          			0x802C
#define		INSTANCE_INDEX(x)			((x) << 0)
#define		SH_INDEX(x)     			((x) << 8)
#define		SE_INDEX(x)     			((x) << 16)
#define		SH_BROADCAST_WRITES      		(1 << 29)
#define		INSTANCE_BROADCAST_WRITES      		(1 << 30)
#define		SE_BROADCAST_WRITES      		(1 << 31)

#define GRBM_INT_CNTL                                   0x8060
#       define RDERR_INT_ENABLE                         (1 << 0)
#       define GUI_IDLE_INT_ENABLE                      (1 << 19)

#define	CP_STRMOUT_CNTL					0x84FC
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

#define	CP_SEM_WAIT_TIMER				0x85BC

#define	CP_SEM_INCOMPLETE_TIMER_CNTL			0x85C8

#define CP_ME_CNTL					0x86D8
#define		CP_CE_HALT					(1 << 24)
#define		CP_PFP_HALT					(1 << 26)
#define		CP_ME_HALT					(1 << 28)

#define	CP_COHER_CNTL2					0x85E8

#define	CP_RB2_RPTR					0x86f8
#define	CP_RB1_RPTR					0x86fc
#define	CP_RB0_RPTR					0x8700
#define	CP_RB_WPTR_DELAY				0x8704

#define	CP_QUEUE_THRESHOLDS				0x8760
#define		ROQ_IB1_START(x)				((x) << 0)
#define		ROQ_IB2_START(x)				((x) << 8)
#define CP_MEQ_THRESHOLDS				0x8764
#define		MEQ1_START(x)				((x) << 0)
#define		MEQ2_START(x)				((x) << 8)

#define	CP_PERFMON_CNTL					0x87FC

#define	VGT_VTX_VECT_EJECT_REG				0x88B0

#define	VGT_CACHE_INVALIDATION				0x88C4
#define		CACHE_INVALIDATION(x)				((x) << 0)
#define			VC_ONLY						0
#define			TC_ONLY						1
#define			VC_AND_TC					2
#define		AUTO_INVLD_EN(x)				((x) << 6)
#define			NO_AUTO						0
#define			ES_AUTO						1
#define			GS_AUTO						2
#define			ES_AND_GS_AUTO					3
#define	VGT_ESGS_RING_SIZE				0x88C8
#define	VGT_GSVS_RING_SIZE				0x88CC

#define	VGT_GS_VERTEX_REUSE				0x88D4

#define	VGT_PRIMITIVE_TYPE				0x8958
#define	VGT_INDEX_TYPE					0x895C

#define	VGT_NUM_INDICES					0x8970
#define	VGT_NUM_INSTANCES				0x8974

#define	VGT_TF_RING_SIZE				0x8988

#define	VGT_HS_OFFCHIP_PARAM				0x89B0

#define	VGT_TF_MEMORY_BASE				0x89B8

#define CC_GC_SHADER_ARRAY_CONFIG			0x89bc
#define		INACTIVE_CUS_MASK			0xFFFF0000
#define		INACTIVE_CUS_SHIFT			16
#define GC_USER_SHADER_ARRAY_CONFIG			0x89c0

#define	PA_CL_ENHANCE					0x8A14
#define		CLIP_VTX_REORDER_ENA				(1 << 0)
#define		NUM_CLIP_SEQ(x)					((x) << 1)

#define	PA_SU_LINE_STIPPLE_VALUE			0x8A60

#define	PA_SC_LINE_STIPPLE_STATE			0x8B10

#define	PA_SC_FORCE_EOV_MAX_CNTS			0x8B24
#define		FORCE_EOV_MAX_CLK_CNT(x)			((x) << 0)
#define		FORCE_EOV_MAX_REZ_CNT(x)			((x) << 16)

#define	PA_SC_FIFO_SIZE					0x8BCC
#define		SC_FRONTEND_PRIM_FIFO_SIZE(x)			((x) << 0)
#define		SC_BACKEND_PRIM_FIFO_SIZE(x)			((x) << 6)
#define		SC_HIZ_TILE_FIFO_SIZE(x)			((x) << 15)
#define		SC_EARLYZ_TILE_FIFO_SIZE(x)			((x) << 23)

#define	PA_SC_ENHANCE					0x8BF0

#define	SQ_CONFIG					0x8C00

#define	SQC_CACHES					0x8C08

#define SQ_POWER_THROTTLE                               0x8e58
#define		MIN_POWER(x)				((x) << 0)
#define		MIN_POWER_MASK				(0x3fff << 0)
#define		MIN_POWER_SHIFT				0
#define		MAX_POWER(x)				((x) << 16)
#define		MAX_POWER_MASK				(0x3fff << 16)
#define		MAX_POWER_SHIFT				0
#define SQ_POWER_THROTTLE2                              0x8e5c
#define		MAX_POWER_DELTA(x)			((x) << 0)
#define		MAX_POWER_DELTA_MASK			(0x3fff << 0)
#define		MAX_POWER_DELTA_SHIFT			0
#define		STI_SIZE(x)				((x) << 16)
#define		STI_SIZE_MASK				(0x3ff << 16)
#define		STI_SIZE_SHIFT				16
#define		LTI_RATIO(x)				((x) << 27)
#define		LTI_RATIO_MASK				(0xf << 27)
#define		LTI_RATIO_SHIFT				27

#define	SX_DEBUG_1					0x9060

#define	SPI_STATIC_THREAD_MGMT_1			0x90E0
#define	SPI_STATIC_THREAD_MGMT_2			0x90E4
#define	SPI_STATIC_THREAD_MGMT_3			0x90E8
#define	SPI_PS_MAX_WAVE_ID				0x90EC

#define	SPI_CONFIG_CNTL					0x9100

#define	SPI_CONFIG_CNTL_1				0x913C
#define		VTX_DONE_DELAY(x)				((x) << 0)
#define		INTERP_ONE_PRIM_PER_ROW				(1 << 4)

#define	CGTS_TCC_DISABLE				0x9148
#define	CGTS_USER_TCC_DISABLE				0x914C
#define		TCC_DISABLE_MASK				0xFFFF0000
#define		TCC_DISABLE_SHIFT				16
#define	CGTS_SM_CTRL_REG				0x9150
#define		OVERRIDE				(1 << 21)
#define		LS_OVERRIDE				(1 << 22)

#define	SPI_LB_CU_MASK					0x9354

#define	TA_CNTL_AUX					0x9508

#define CC_RB_BACKEND_DISABLE				0x98F4
#define		BACKEND_DISABLE(x)     			((x) << 16)
#define GB_ADDR_CONFIG  				0x98F8
#define		NUM_PIPES(x)				((x) << 0)
#define		NUM_PIPES_MASK				0x00000007
#define		NUM_PIPES_SHIFT				0
#define		PIPE_INTERLEAVE_SIZE(x)			((x) << 4)
#define		PIPE_INTERLEAVE_SIZE_MASK		0x00000070
#define		PIPE_INTERLEAVE_SIZE_SHIFT		4
#define		NUM_SHADER_ENGINES(x)			((x) << 12)
#define		NUM_SHADER_ENGINES_MASK			0x00003000
#define		NUM_SHADER_ENGINES_SHIFT		12
#define		SHADER_ENGINE_TILE_SIZE(x)     		((x) << 16)
#define		SHADER_ENGINE_TILE_SIZE_MASK		0x00070000
#define		SHADER_ENGINE_TILE_SIZE_SHIFT		16
#define		NUM_GPUS(x)     			((x) << 20)
#define		NUM_GPUS_MASK				0x00700000
#define		NUM_GPUS_SHIFT				20
#define		MULTI_GPU_TILE_SIZE(x)     		((x) << 24)
#define		MULTI_GPU_TILE_SIZE_MASK		0x03000000
#define		MULTI_GPU_TILE_SIZE_SHIFT		24
#define		ROW_SIZE(x)             		((x) << 28)
#define		ROW_SIZE_MASK				0x30000000
#define		ROW_SIZE_SHIFT				28

#define	GB_TILE_MODE0					0x9910
#       define MICRO_TILE_MODE(x)				((x) << 0)
#              define	ADDR_SURF_DISPLAY_MICRO_TILING		0
#              define	ADDR_SURF_THIN_MICRO_TILING		1
#              define	ADDR_SURF_DEPTH_MICRO_TILING		2
#       define ARRAY_MODE(x)					((x) << 2)
#              define	ARRAY_LINEAR_GENERAL			0
#              define	ARRAY_LINEAR_ALIGNED			1
#              define	ARRAY_1D_TILED_THIN1			2
#              define	ARRAY_2D_TILED_THIN1			4
#       define PIPE_CONFIG(x)					((x) << 6)
#              define	ADDR_SURF_P2				0
#              define	ADDR_SURF_P4_8x16			4
#              define	ADDR_SURF_P4_16x16			5
#              define	ADDR_SURF_P4_16x32			6
#              define	ADDR_SURF_P4_32x32			7
#              define	ADDR_SURF_P8_16x16_8x16			8
#              define	ADDR_SURF_P8_16x32_8x16			9
#              define	ADDR_SURF_P8_32x32_8x16			10
#              define	ADDR_SURF_P8_16x32_16x16		11
#              define	ADDR_SURF_P8_32x32_16x16		12
#              define	ADDR_SURF_P8_32x32_16x32		13
#              define	ADDR_SURF_P8_32x64_32x32		14
#       define TILE_SPLIT(x)					((x) << 11)
#              define	ADDR_SURF_TILE_SPLIT_64B		0
#              define	ADDR_SURF_TILE_SPLIT_128B		1
#              define	ADDR_SURF_TILE_SPLIT_256B		2
#              define	ADDR_SURF_TILE_SPLIT_512B		3
#              define	ADDR_SURF_TILE_SPLIT_1KB		4
#              define	ADDR_SURF_TILE_SPLIT_2KB		5
#              define	ADDR_SURF_TILE_SPLIT_4KB		6
#       define BANK_WIDTH(x)					((x) << 14)
#              define	ADDR_SURF_BANK_WIDTH_1			0
#              define	ADDR_SURF_BANK_WIDTH_2			1
#              define	ADDR_SURF_BANK_WIDTH_4			2
#              define	ADDR_SURF_BANK_WIDTH_8			3
#       define BANK_HEIGHT(x)					((x) << 16)
#              define	ADDR_SURF_BANK_HEIGHT_1			0
#              define	ADDR_SURF_BANK_HEIGHT_2			1
#              define	ADDR_SURF_BANK_HEIGHT_4			2
#              define	ADDR_SURF_BANK_HEIGHT_8			3
#       define MACRO_TILE_ASPECT(x)				((x) << 18)
#              define	ADDR_SURF_MACRO_ASPECT_1		0
#              define	ADDR_SURF_MACRO_ASPECT_2		1
#              define	ADDR_SURF_MACRO_ASPECT_4		2
#              define	ADDR_SURF_MACRO_ASPECT_8		3
#       define NUM_BANKS(x)					((x) << 20)
#              define	ADDR_SURF_2_BANK			0
#              define	ADDR_SURF_4_BANK			1
#              define	ADDR_SURF_8_BANK			2
#              define	ADDR_SURF_16_BANK			3

#define	CB_PERFCOUNTER0_SELECT0				0x9a20
#define	CB_PERFCOUNTER0_SELECT1				0x9a24
#define	CB_PERFCOUNTER1_SELECT0				0x9a28
#define	CB_PERFCOUNTER1_SELECT1				0x9a2c
#define	CB_PERFCOUNTER2_SELECT0				0x9a30
#define	CB_PERFCOUNTER2_SELECT1				0x9a34
#define	CB_PERFCOUNTER3_SELECT0				0x9a38
#define	CB_PERFCOUNTER3_SELECT1				0x9a3c

#define	CB_CGTT_SCLK_CTRL				0x9a60

#define	GC_USER_RB_BACKEND_DISABLE			0x9B7C
#define		BACKEND_DISABLE_MASK			0x00FF0000
#define		BACKEND_DISABLE_SHIFT			16

#define	TCP_CHAN_STEER_LO				0xac0c
#define	TCP_CHAN_STEER_HI				0xac10

#define	CP_RB0_BASE					0xC100
#define	CP_RB0_CNTL					0xC104
#define		RB_BUFSZ(x)					((x) << 0)
#define		RB_BLKSZ(x)					((x) << 8)
#define		BUF_SWAP_32BIT					(2 << 16)
#define		RB_NO_UPDATE					(1 << 27)
#define		RB_RPTR_WR_ENA					(1 << 31)

#define	CP_RB0_RPTR_ADDR				0xC10C
#define	CP_RB0_RPTR_ADDR_HI				0xC110
#define	CP_RB0_WPTR					0xC114

#define	CP_PFP_UCODE_ADDR				0xC150
#define	CP_PFP_UCODE_DATA				0xC154
#define	CP_ME_RAM_RADDR					0xC158
#define	CP_ME_RAM_WADDR					0xC15C
#define	CP_ME_RAM_DATA					0xC160

#define	CP_CE_UCODE_ADDR				0xC168
#define	CP_CE_UCODE_DATA				0xC16C

#define	CP_RB1_BASE					0xC180
#define	CP_RB1_CNTL					0xC184
#define	CP_RB1_RPTR_ADDR				0xC188
#define	CP_RB1_RPTR_ADDR_HI				0xC18C
#define	CP_RB1_WPTR					0xC190
#define	CP_RB2_BASE					0xC194
#define	CP_RB2_CNTL					0xC198
#define	CP_RB2_RPTR_ADDR				0xC19C
#define	CP_RB2_RPTR_ADDR_HI				0xC1A0
#define	CP_RB2_WPTR					0xC1A4
#define CP_INT_CNTL_RING0                               0xC1A8
#define CP_INT_CNTL_RING1                               0xC1AC
#define CP_INT_CNTL_RING2                               0xC1B0
#       define CNTX_BUSY_INT_ENABLE                     (1 << 19)
#       define CNTX_EMPTY_INT_ENABLE                    (1 << 20)
#       define WAIT_MEM_SEM_INT_ENABLE                  (1 << 21)
#       define TIME_STAMP_INT_ENABLE                    (1 << 26)
#       define CP_RINGID2_INT_ENABLE                    (1 << 29)
#       define CP_RINGID1_INT_ENABLE                    (1 << 30)
#       define CP_RINGID0_INT_ENABLE                    (1 << 31)
#define CP_INT_STATUS_RING0                             0xC1B4
#define CP_INT_STATUS_RING1                             0xC1B8
#define CP_INT_STATUS_RING2                             0xC1BC
#       define WAIT_MEM_SEM_INT_STAT                    (1 << 21)
#       define TIME_STAMP_INT_STAT                      (1 << 26)
#       define CP_RINGID2_INT_STAT                      (1 << 29)
#       define CP_RINGID1_INT_STAT                      (1 << 30)
#       define CP_RINGID0_INT_STAT                      (1 << 31)

#define	CP_MEM_SLP_CNTL					0xC1E4
#       define CP_MEM_LS_EN                             (1 << 0)

#define	CP_DEBUG					0xC1FC

#define RLC_CNTL                                          0xC300
#       define RLC_ENABLE                                 (1 << 0)
#define RLC_RL_BASE                                       0xC304
#define RLC_RL_SIZE                                       0xC308
#define RLC_LB_CNTL                                       0xC30C
#       define LOAD_BALANCE_ENABLE                        (1 << 0)
#define RLC_SAVE_AND_RESTORE_BASE                         0xC310
#define RLC_LB_CNTR_MAX                                   0xC314
#define RLC_LB_CNTR_INIT                                  0xC318

#define RLC_CLEAR_STATE_RESTORE_BASE                      0xC320

#define RLC_UCODE_ADDR                                    0xC32C
#define RLC_UCODE_DATA                                    0xC330

#define RLC_GPU_CLOCK_COUNT_LSB                           0xC338
#define RLC_GPU_CLOCK_COUNT_MSB                           0xC33C
#define RLC_CAPTURE_GPU_CLOCK_COUNT                       0xC340
#define RLC_MC_CNTL                                       0xC344
#define RLC_UCODE_CNTL                                    0xC348
#define RLC_STAT                                          0xC34C
#       define RLC_BUSY_STATUS                            (1 << 0)
#       define GFX_POWER_STATUS                           (1 << 1)
#       define GFX_CLOCK_STATUS                           (1 << 2)
#       define GFX_LS_STATUS                              (1 << 3)

#define	RLC_PG_CNTL					0xC35C
#	define GFX_PG_ENABLE				(1 << 0)
#	define GFX_PG_SRC				(1 << 1)

#define	RLC_CGTT_MGCG_OVERRIDE				0xC400
#define	RLC_CGCG_CGLS_CTRL				0xC404
#	define CGCG_EN					(1 << 0)
#	define CGLS_EN					(1 << 1)

#define	RLC_TTOP_D					0xC414
#	define RLC_PUD(x)				((x) << 0)
#	define RLC_PUD_MASK				(0xff << 0)
#	define RLC_PDD(x)				((x) << 8)
#	define RLC_PDD_MASK				(0xff << 8)
#	define RLC_TTPD(x)				((x) << 16)
#	define RLC_TTPD_MASK				(0xff << 16)
#	define RLC_MSD(x)				((x) << 24)
#	define RLC_MSD_MASK				(0xff << 24)

#define RLC_LB_INIT_CU_MASK                               0xC41C

#define	RLC_PG_AO_CU_MASK				0xC42C
#define	RLC_MAX_PG_CU					0xC430
#	define MAX_PU_CU(x)				((x) << 0)
#	define MAX_PU_CU_MASK				(0xff << 0)
#define	RLC_AUTO_PG_CTRL				0xC434
#	define AUTO_PG_EN				(1 << 0)
#	define GRBM_REG_SGIT(x)				((x) << 3)
#	define GRBM_REG_SGIT_MASK			(0xffff << 3)
#	define PG_AFTER_GRBM_REG_ST(x)			((x) << 19)
#	define PG_AFTER_GRBM_REG_ST_MASK		(0x1fff << 19)

#define RLC_SERDES_WR_MASTER_MASK_0                       0xC454
#define RLC_SERDES_WR_MASTER_MASK_1                       0xC458
#define RLC_SERDES_WR_CTRL                                0xC45C

#define RLC_SERDES_MASTER_BUSY_0                          0xC464
#define RLC_SERDES_MASTER_BUSY_1                          0xC468

#define RLC_GCPM_GENERAL_3                                0xC478

#define	DB_RENDER_CONTROL				0x28000

#define DB_DEPTH_INFO                                   0x2803c

#define PA_SC_RASTER_CONFIG                             0x28350
#       define RASTER_CONFIG_RB_MAP_0                   0
#       define RASTER_CONFIG_RB_MAP_1                   1
#       define RASTER_CONFIG_RB_MAP_2                   2
#       define RASTER_CONFIG_RB_MAP_3                   3

#define VGT_EVENT_INITIATOR                             0x28a90
#       define SAMPLE_STREAMOUTSTATS1                   (1 << 0)
#       define SAMPLE_STREAMOUTSTATS2                   (2 << 0)
#       define SAMPLE_STREAMOUTSTATS3                   (3 << 0)
#       define CACHE_FLUSH_TS                           (4 << 0)
#       define CACHE_FLUSH                              (6 << 0)
#       define CS_PARTIAL_FLUSH                         (7 << 0)
#       define VGT_STREAMOUT_RESET                      (10 << 0)
#       define END_OF_PIPE_INCR_DE                      (11 << 0)
#       define END_OF_PIPE_IB_END                       (12 << 0)
#       define RST_PIX_CNT                              (13 << 0)
#       define VS_PARTIAL_FLUSH                         (15 << 0)
#       define PS_PARTIAL_FLUSH                         (16 << 0)
#       define CACHE_FLUSH_AND_INV_TS_EVENT             (20 << 0)
#       define ZPASS_DONE                               (21 << 0)
#       define CACHE_FLUSH_AND_INV_EVENT                (22 << 0)
#       define PERFCOUNTER_START                        (23 << 0)
#       define PERFCOUNTER_STOP                         (24 << 0)
#       define PIPELINESTAT_START                       (25 << 0)
#       define PIPELINESTAT_STOP                        (26 << 0)
#       define PERFCOUNTER_SAMPLE                       (27 << 0)
#       define SAMPLE_PIPELINESTAT                      (30 << 0)
#       define SAMPLE_STREAMOUTSTATS                    (32 << 0)
#       define RESET_VTX_CNT                            (33 << 0)
#       define VGT_FLUSH                                (36 << 0)
#       define BOTTOM_OF_PIPE_TS                        (40 << 0)
#       define DB_CACHE_FLUSH_AND_INV                   (42 << 0)
#       define FLUSH_AND_INV_DB_DATA_TS                 (43 << 0)
#       define FLUSH_AND_INV_DB_META                    (44 << 0)
#       define FLUSH_AND_INV_CB_DATA_TS                 (45 << 0)
#       define FLUSH_AND_INV_CB_META                    (46 << 0)
#       define CS_DONE                                  (47 << 0)
#       define PS_DONE                                  (48 << 0)
#       define FLUSH_AND_INV_CB_PIXEL_DATA              (49 << 0)
#       define THREAD_TRACE_START                       (51 << 0)
#       define THREAD_TRACE_STOP                        (52 << 0)
#       define THREAD_TRACE_FLUSH                       (54 << 0)
#       define THREAD_TRACE_FINISH                      (55 << 0)

/* PIF PHY0 registers idx/data 0x8/0xc */
#define PB0_PIF_CNTL                                      0x10
#       define LS2_EXIT_TIME(x)                           ((x) << 17)
#       define LS2_EXIT_TIME_MASK                         (0x7 << 17)
#       define LS2_EXIT_TIME_SHIFT                        17
#define PB0_PIF_PAIRING                                   0x11
#       define MULTI_PIF                                  (1 << 25)
#define PB0_PIF_PWRDOWN_0                                 0x12
#       define PLL_POWER_STATE_IN_TXS2_0(x)               ((x) << 7)
#       define PLL_POWER_STATE_IN_TXS2_0_MASK             (0x7 << 7)
#       define PLL_POWER_STATE_IN_TXS2_0_SHIFT            7
#       define PLL_POWER_STATE_IN_OFF_0(x)                ((x) << 10)
#       define PLL_POWER_STATE_IN_OFF_0_MASK              (0x7 << 10)
#       define PLL_POWER_STATE_IN_OFF_0_SHIFT             10
#       define PLL_RAMP_UP_TIME_0(x)                      ((x) << 24)
#       define PLL_RAMP_UP_TIME_0_MASK                    (0x7 << 24)
#       define PLL_RAMP_UP_TIME_0_SHIFT                   24
#define PB0_PIF_PWRDOWN_1                                 0x13
#       define PLL_POWER_STATE_IN_TXS2_1(x)               ((x) << 7)
#       define PLL_POWER_STATE_IN_TXS2_1_MASK             (0x7 << 7)
#       define PLL_POWER_STATE_IN_TXS2_1_SHIFT            7
#       define PLL_POWER_STATE_IN_OFF_1(x)                ((x) << 10)
#       define PLL_POWER_STATE_IN_OFF_1_MASK              (0x7 << 10)
#       define PLL_POWER_STATE_IN_OFF_1_SHIFT             10
#       define PLL_RAMP_UP_TIME_1(x)                      ((x) << 24)
#       define PLL_RAMP_UP_TIME_1_MASK                    (0x7 << 24)
#       define PLL_RAMP_UP_TIME_1_SHIFT                   24

#define PB0_PIF_PWRDOWN_2                                 0x17
#       define PLL_POWER_STATE_IN_TXS2_2(x)               ((x) << 7)
#       define PLL_POWER_STATE_IN_TXS2_2_MASK             (0x7 << 7)
#       define PLL_POWER_STATE_IN_TXS2_2_SHIFT            7
#       define PLL_POWER_STATE_IN_OFF_2(x)                ((x) << 10)
#       define PLL_POWER_STATE_IN_OFF_2_MASK              (0x7 << 10)
#       define PLL_POWER_STATE_IN_OFF_2_SHIFT             10
#       define PLL_RAMP_UP_TIME_2(x)                      ((x) << 24)
#       define PLL_RAMP_UP_TIME_2_MASK                    (0x7 << 24)
#       define PLL_RAMP_UP_TIME_2_SHIFT                   24
#define PB0_PIF_PWRDOWN_3                                 0x18
#       define PLL_POWER_STATE_IN_TXS2_3(x)               ((x) << 7)
#       define PLL_POWER_STATE_IN_TXS2_3_MASK             (0x7 << 7)
#       define PLL_POWER_STATE_IN_TXS2_3_SHIFT            7
#       define PLL_POWER_STATE_IN_OFF_3(x)                ((x) << 10)
#       define PLL_POWER_STATE_IN_OFF_3_MASK              (0x7 << 10)
#       define PLL_POWER_STATE_IN_OFF_3_SHIFT             10
#       define PLL_RAMP_UP_TIME_3(x)                      ((x) << 24)
#       define PLL_RAMP_UP_TIME_3_MASK                    (0x7 << 24)
#       define PLL_RAMP_UP_TIME_3_SHIFT                   24
/* PIF PHY1 registers idx/data 0x10/0x14 */
#define PB1_PIF_CNTL                                      0x10
#define PB1_PIF_PAIRING                                   0x11
#define PB1_PIF_PWRDOWN_0                                 0x12
#define PB1_PIF_PWRDOWN_1                                 0x13

#define PB1_PIF_PWRDOWN_2                                 0x17
#define PB1_PIF_PWRDOWN_3                                 0x18
/* PCIE registers idx/data 0x30/0x34 */
#define PCIE_CNTL2                                        0x1c /* PCIE */
#       define SLV_MEM_LS_EN                              (1 << 16)
#       define SLV_MEM_AGGRESSIVE_LS_EN                   (1 << 17)
#       define MST_MEM_LS_EN                              (1 << 18)
#       define REPLAY_MEM_LS_EN                           (1 << 19)
#define PCIE_LC_STATUS1                                   0x28 /* PCIE */
#       define LC_REVERSE_RCVR                            (1 << 0)
#       define LC_REVERSE_XMIT                            (1 << 1)
#       define LC_OPERATING_LINK_WIDTH_MASK               (0x7 << 2)
#       define LC_OPERATING_LINK_WIDTH_SHIFT              2
#       define LC_DETECTED_LINK_WIDTH_MASK                (0x7 << 5)
#       define LC_DETECTED_LINK_WIDTH_SHIFT               5

#define PCIE_P_CNTL                                       0x40 /* PCIE */
#       define P_IGNORE_EDB_ERR                           (1 << 6)

/* PCIE PORT registers idx/data 0x38/0x3c */
#define PCIE_LC_CNTL                                      0xa0
#       define LC_L0S_INACTIVITY(x)                       ((x) << 8)
#       define LC_L0S_INACTIVITY_MASK                     (0xf << 8)
#       define LC_L0S_INACTIVITY_SHIFT                    8
#       define LC_L1_INACTIVITY(x)                        ((x) << 12)
#       define LC_L1_INACTIVITY_MASK                      (0xf << 12)
#       define LC_L1_INACTIVITY_SHIFT                     12
#       define LC_PMI_TO_L1_DIS                           (1 << 16)
#       define LC_ASPM_TO_L1_DIS                          (1 << 24)
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
#       define LC_DYN_LANES_PWR_STATE(x)                  ((x) << 21)
#       define LC_DYN_LANES_PWR_STATE_MASK                (0x3 << 21)
#       define LC_DYN_LANES_PWR_STATE_SHIFT               21
#define PCIE_LC_N_FTS_CNTL                                0xa3 /* PCIE_P */
#       define LC_XMIT_N_FTS(x)                           ((x) << 0)
#       define LC_XMIT_N_FTS_MASK                         (0xff << 0)
#       define LC_XMIT_N_FTS_SHIFT                        0
#       define LC_XMIT_N_FTS_OVERRIDE_EN                  (1 << 8)
#       define LC_N_FTS_MASK                              (0xff << 24)
#define PCIE_LC_SPEED_CNTL                                0xa4 /* PCIE_P */
#       define LC_GEN2_EN_STRAP                           (1 << 0)
#       define LC_GEN3_EN_STRAP                           (1 << 1)
#       define LC_TARGET_LINK_SPEED_OVERRIDE_EN           (1 << 2)
#       define LC_TARGET_LINK_SPEED_OVERRIDE_MASK         (0x3 << 3)
#       define LC_TARGET_LINK_SPEED_OVERRIDE_SHIFT        3
#       define LC_FORCE_EN_SW_SPEED_CHANGE                (1 << 5)
#       define LC_FORCE_DIS_SW_SPEED_CHANGE               (1 << 6)
#       define LC_FORCE_EN_HW_SPEED_CHANGE                (1 << 7)
#       define LC_FORCE_DIS_HW_SPEED_CHANGE               (1 << 8)
#       define LC_INITIATE_LINK_SPEED_CHANGE              (1 << 9)
#       define LC_SPEED_CHANGE_ATTEMPTS_ALLOWED_MASK      (0x3 << 10)
#       define LC_SPEED_CHANGE_ATTEMPTS_ALLOWED_SHIFT     10
#       define LC_CURRENT_DATA_RATE_MASK                  (0x3 << 13) /* 0/1/2 = gen1/2/3 */
#       define LC_CURRENT_DATA_RATE_SHIFT                 13
#       define LC_CLR_FAILED_SPD_CHANGE_CNT               (1 << 16)
#       define LC_OTHER_SIDE_EVER_SENT_GEN2               (1 << 18)
#       define LC_OTHER_SIDE_SUPPORTS_GEN2                (1 << 19)
#       define LC_OTHER_SIDE_EVER_SENT_GEN3               (1 << 20)
#       define LC_OTHER_SIDE_SUPPORTS_GEN3                (1 << 21)

#define PCIE_LC_CNTL2                                     0xb1
#       define LC_ALLOW_PDWN_IN_L1                        (1 << 17)
#       define LC_ALLOW_PDWN_IN_L23                       (1 << 18)

#define PCIE_LC_CNTL3                                     0xb5 /* PCIE_P */
#       define LC_GO_TO_RECOVERY                          (1 << 30)
#define PCIE_LC_CNTL4                                     0xb6 /* PCIE_P */
#       define LC_REDO_EQ                                 (1 << 5)
#       define LC_SET_QUIESCE                             (1 << 13)

/*
 * UVD
 */
#define UVD_UDEC_ADDR_CONFIG				0xEF4C
#define UVD_UDEC_DB_ADDR_CONFIG				0xEF50
#define UVD_UDEC_DBW_ADDR_CONFIG			0xEF54
#define UVD_RBC_RB_RPTR					0xF690
#define UVD_RBC_RB_WPTR					0xF694

#define	UVD_CGC_CTRL					0xF4B0
#	define DCM					(1 << 0)
#	define CG_DT(x)					((x) << 2)
#	define CG_DT_MASK				(0xf << 2)
#	define CLK_OD(x)				((x) << 6)
#	define CLK_OD_MASK				(0x1f << 6)

 /* UVD CTX indirect */
#define	UVD_CGC_MEM_CTRL				0xC0
#define	UVD_CGC_CTRL2					0xC1
#	define DYN_OR_EN				(1 << 0)
#	define DYN_RR_EN				(1 << 1)
#	define G_DIV_ID(x)				((x) << 2)
#	define G_DIV_ID_MASK				(0x7 << 2)

/*
 * PM4
 */
#define PACKET0(reg, n)	((RADEON_PACKET_TYPE0 << 30) |			\
			 (((reg) >> 2) & 0xFFFF) |			\
			 ((n) & 0x3FFF) << 16)
#define CP_PACKET2			0x80000000
#define		PACKET2_PAD_SHIFT		0
#define		PACKET2_PAD_MASK		(0x3fffffff << 0)

#define PACKET2(v)	(CP_PACKET2 | REG_SET(PACKET2_PAD, (v)))

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
#define	PACKET3_INDIRECT_BUFFER				0x32
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
#define		PACKET3_SET_CONFIG_REG_START			0x00008000
#define		PACKET3_SET_CONFIG_REG_END			0x0000b000
#define	PACKET3_SET_CONTEXT_REG				0x69
#define		PACKET3_SET_CONTEXT_REG_START			0x00028000
#define		PACKET3_SET_CONTEXT_REG_END			0x00029000
#define	PACKET3_SET_CONTEXT_REG_INDIRECT		0x73
#define	PACKET3_SET_RESOURCE_INDIRECT			0x74
#define	PACKET3_SET_SH_REG				0x76
#define		PACKET3_SET_SH_REG_START			0x0000b000
#define		PACKET3_SET_SH_REG_END				0x0000c000
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
#define DMA1_REGISTER_OFFSET                              0x800 /* not a register */

#define DMA_RB_CNTL                                       0xd000
#       define DMA_RB_ENABLE                              (1 << 0)
#       define DMA_RB_SIZE(x)                             ((x) << 1) /* log2 */
#       define DMA_RB_SWAP_ENABLE                         (1 << 9) /* 8IN32 */
#       define DMA_RPTR_WRITEBACK_ENABLE                  (1 << 12)
#       define DMA_RPTR_WRITEBACK_SWAP_ENABLE             (1 << 13)  /* 8IN32 */
#       define DMA_RPTR_WRITEBACK_TIMER(x)                ((x) << 16) /* log2 */
#define DMA_RB_BASE                                       0xd004
#define DMA_RB_RPTR                                       0xd008
#define DMA_RB_WPTR                                       0xd00c

#define DMA_RB_RPTR_ADDR_HI                               0xd01c
#define DMA_RB_RPTR_ADDR_LO                               0xd020

#define DMA_IB_CNTL                                       0xd024
#       define DMA_IB_ENABLE                              (1 << 0)
#       define DMA_IB_SWAP_ENABLE                         (1 << 4)
#define DMA_IB_RPTR                                       0xd028
#define DMA_CNTL                                          0xd02c
#       define TRAP_ENABLE                                (1 << 0)
#       define SEM_INCOMPLETE_INT_ENABLE                  (1 << 1)
#       define SEM_WAIT_INT_ENABLE                        (1 << 2)
#       define DATA_SWAP_ENABLE                           (1 << 3)
#       define FENCE_SWAP_ENABLE                          (1 << 4)
#       define CTXEMPTY_INT_ENABLE                        (1 << 28)
#define DMA_STATUS_REG                                    0xd034
#       define DMA_IDLE                                   (1 << 0)
#define DMA_TILING_CONFIG  				  0xd0b8

#define	DMA_POWER_CNTL					0xd0bc
#       define MEM_POWER_OVERRIDE                       (1 << 8)
#define	DMA_CLK_CTRL					0xd0c0

#define	DMA_PG						0xd0d4
#	define PG_CNTL_ENABLE				(1 << 0)
#define	DMA_PGFSM_CONFIG				0xd0d8
#define	DMA_PGFSM_WRITE					0xd0dc

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
#define	DMA_PACKET_NOP					  0xf

#endif
