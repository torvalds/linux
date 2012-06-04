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
#ifndef R600D_H
#define R600D_H

#define CP_PACKET2			0x80000000
#define		PACKET2_PAD_SHIFT		0
#define		PACKET2_PAD_MASK		(0x3fffffff << 0)

#define PACKET2(v)	(CP_PACKET2 | REG_SET(PACKET2_PAD, (v)))

#define R6XX_MAX_SH_GPRS			256
#define R6XX_MAX_TEMP_GPRS			16
#define R6XX_MAX_SH_THREADS			256
#define R6XX_MAX_SH_STACK_ENTRIES		4096
#define R6XX_MAX_BACKENDS			8
#define R6XX_MAX_BACKENDS_MASK			0xff
#define R6XX_MAX_SIMDS				8
#define R6XX_MAX_SIMDS_MASK			0xff
#define R6XX_MAX_PIPES				8
#define R6XX_MAX_PIPES_MASK			0xff

/* PTE flags */
#define PTE_VALID				(1 << 0)
#define PTE_SYSTEM				(1 << 1)
#define PTE_SNOOPED				(1 << 2)
#define PTE_READABLE				(1 << 5)
#define PTE_WRITEABLE				(1 << 6)

/* tiling bits */
#define     ARRAY_LINEAR_GENERAL              0x00000000
#define     ARRAY_LINEAR_ALIGNED              0x00000001
#define     ARRAY_1D_TILED_THIN1              0x00000002
#define     ARRAY_2D_TILED_THIN1              0x00000004

/* Registers */
#define	ARB_POP						0x2418
#define 	ENABLE_TC128					(1 << 30)
#define	ARB_GDEC_RD_CNTL				0x246C

#define	CC_GC_SHADER_PIPE_CONFIG			0x8950
#define	CC_RB_BACKEND_DISABLE				0x98F4
#define		BACKEND_DISABLE(x)				((x) << 16)

#define	CB_COLOR0_BASE					0x28040
#define	CB_COLOR1_BASE					0x28044
#define	CB_COLOR2_BASE					0x28048
#define	CB_COLOR3_BASE					0x2804C
#define	CB_COLOR4_BASE					0x28050
#define	CB_COLOR5_BASE					0x28054
#define	CB_COLOR6_BASE					0x28058
#define	CB_COLOR7_BASE					0x2805C
#define	CB_COLOR7_FRAG					0x280FC

#define CB_COLOR0_SIZE                                  0x28060
#define CB_COLOR0_VIEW                                  0x28080
#define R_028080_CB_COLOR0_VIEW                      0x028080
#define   S_028080_SLICE_START(x)                      (((x) & 0x7FF) << 0)
#define   G_028080_SLICE_START(x)                      (((x) >> 0) & 0x7FF)
#define   C_028080_SLICE_START                         0xFFFFF800
#define   S_028080_SLICE_MAX(x)                        (((x) & 0x7FF) << 13)
#define   G_028080_SLICE_MAX(x)                        (((x) >> 13) & 0x7FF)
#define   C_028080_SLICE_MAX                           0xFF001FFF
#define R_028084_CB_COLOR1_VIEW                      0x028084
#define R_028088_CB_COLOR2_VIEW                      0x028088
#define R_02808C_CB_COLOR3_VIEW                      0x02808C
#define R_028090_CB_COLOR4_VIEW                      0x028090
#define R_028094_CB_COLOR5_VIEW                      0x028094
#define R_028098_CB_COLOR6_VIEW                      0x028098
#define R_02809C_CB_COLOR7_VIEW                      0x02809C
#define CB_COLOR0_INFO                                  0x280a0
#	define CB_FORMAT(x)				((x) << 2)
#       define CB_ARRAY_MODE(x)                         ((x) << 8)
#	define CB_SOURCE_FORMAT(x)			((x) << 27)
#	define CB_SF_EXPORT_FULL			0
#	define CB_SF_EXPORT_NORM			1
#define CB_COLOR0_TILE                                  0x280c0
#define CB_COLOR0_FRAG                                  0x280e0
#define CB_COLOR0_MASK                                  0x28100

#define SQ_ALU_CONST_CACHE_PS_0				0x28940
#define SQ_ALU_CONST_CACHE_PS_1				0x28944
#define SQ_ALU_CONST_CACHE_PS_2				0x28948
#define SQ_ALU_CONST_CACHE_PS_3				0x2894c
#define SQ_ALU_CONST_CACHE_PS_4				0x28950
#define SQ_ALU_CONST_CACHE_PS_5				0x28954
#define SQ_ALU_CONST_CACHE_PS_6				0x28958
#define SQ_ALU_CONST_CACHE_PS_7				0x2895c
#define SQ_ALU_CONST_CACHE_PS_8				0x28960
#define SQ_ALU_CONST_CACHE_PS_9				0x28964
#define SQ_ALU_CONST_CACHE_PS_10			0x28968
#define SQ_ALU_CONST_CACHE_PS_11			0x2896c
#define SQ_ALU_CONST_CACHE_PS_12			0x28970
#define SQ_ALU_CONST_CACHE_PS_13			0x28974
#define SQ_ALU_CONST_CACHE_PS_14			0x28978
#define SQ_ALU_CONST_CACHE_PS_15			0x2897c
#define SQ_ALU_CONST_CACHE_VS_0				0x28980
#define SQ_ALU_CONST_CACHE_VS_1				0x28984
#define SQ_ALU_CONST_CACHE_VS_2				0x28988
#define SQ_ALU_CONST_CACHE_VS_3				0x2898c
#define SQ_ALU_CONST_CACHE_VS_4				0x28990
#define SQ_ALU_CONST_CACHE_VS_5				0x28994
#define SQ_ALU_CONST_CACHE_VS_6				0x28998
#define SQ_ALU_CONST_CACHE_VS_7				0x2899c
#define SQ_ALU_CONST_CACHE_VS_8				0x289a0
#define SQ_ALU_CONST_CACHE_VS_9				0x289a4
#define SQ_ALU_CONST_CACHE_VS_10			0x289a8
#define SQ_ALU_CONST_CACHE_VS_11			0x289ac
#define SQ_ALU_CONST_CACHE_VS_12			0x289b0
#define SQ_ALU_CONST_CACHE_VS_13			0x289b4
#define SQ_ALU_CONST_CACHE_VS_14			0x289b8
#define SQ_ALU_CONST_CACHE_VS_15			0x289bc
#define SQ_ALU_CONST_CACHE_GS_0				0x289c0
#define SQ_ALU_CONST_CACHE_GS_1				0x289c4
#define SQ_ALU_CONST_CACHE_GS_2				0x289c8
#define SQ_ALU_CONST_CACHE_GS_3				0x289cc
#define SQ_ALU_CONST_CACHE_GS_4				0x289d0
#define SQ_ALU_CONST_CACHE_GS_5				0x289d4
#define SQ_ALU_CONST_CACHE_GS_6				0x289d8
#define SQ_ALU_CONST_CACHE_GS_7				0x289dc
#define SQ_ALU_CONST_CACHE_GS_8				0x289e0
#define SQ_ALU_CONST_CACHE_GS_9				0x289e4
#define SQ_ALU_CONST_CACHE_GS_10			0x289e8
#define SQ_ALU_CONST_CACHE_GS_11			0x289ec
#define SQ_ALU_CONST_CACHE_GS_12			0x289f0
#define SQ_ALU_CONST_CACHE_GS_13			0x289f4
#define SQ_ALU_CONST_CACHE_GS_14			0x289f8
#define SQ_ALU_CONST_CACHE_GS_15			0x289fc

#define	CONFIG_MEMSIZE					0x5428
#define CONFIG_CNTL					0x5424
#define	CP_STAT						0x8680
#define	CP_COHER_BASE					0x85F8
#define	CP_DEBUG					0xC1FC
#define	R_0086D8_CP_ME_CNTL			0x86D8
#define		S_0086D8_CP_ME_HALT(x)			(((x) & 1)<<28)
#define		C_0086D8_CP_ME_HALT(x)			((x) & 0xEFFFFFFF)
#define	CP_ME_RAM_DATA					0xC160
#define	CP_ME_RAM_RADDR					0xC158
#define	CP_ME_RAM_WADDR					0xC15C
#define CP_MEQ_THRESHOLDS				0x8764
#define		MEQ_END(x)					((x) << 16)
#define		ROQ_END(x)					((x) << 24)
#define	CP_PERFMON_CNTL					0x87FC
#define	CP_PFP_UCODE_ADDR				0xC150
#define	CP_PFP_UCODE_DATA				0xC154
#define	CP_QUEUE_THRESHOLDS				0x8760
#define		ROQ_IB1_START(x)				((x) << 0)
#define		ROQ_IB2_START(x)				((x) << 8)
#define	CP_RB_BASE					0xC100
#define	CP_RB_CNTL					0xC104
#define		RB_BUFSZ(x)					((x) << 0)
#define		RB_BLKSZ(x)					((x) << 8)
#define		RB_NO_UPDATE					(1 << 27)
#define		RB_RPTR_WR_ENA					(1 << 31)
#define		BUF_SWAP_32BIT					(2 << 16)
#define	CP_RB_RPTR					0x8700
#define	CP_RB_RPTR_ADDR					0xC10C
#define		RB_RPTR_SWAP(x)					((x) << 0)
#define	CP_RB_RPTR_ADDR_HI				0xC110
#define	CP_RB_RPTR_WR					0xC108
#define	CP_RB_WPTR					0xC114
#define	CP_RB_WPTR_ADDR					0xC118
#define	CP_RB_WPTR_ADDR_HI				0xC11C
#define	CP_RB_WPTR_DELAY				0x8704
#define	CP_ROQ_IB1_STAT					0x8784
#define	CP_ROQ_IB2_STAT					0x8788
#define	CP_SEM_WAIT_TIMER				0x85BC

#define	DB_DEBUG					0x9830
#define		PREZ_MUST_WAIT_FOR_POSTZ_DONE			(1 << 31)
#define	DB_DEPTH_BASE					0x2800C
#define	DB_HTILE_DATA_BASE				0x28014
#define	DB_HTILE_SURFACE				0x28D24
#define   S_028D24_HTILE_WIDTH(x)                      (((x) & 0x1) << 0)
#define   G_028D24_HTILE_WIDTH(x)                      (((x) >> 0) & 0x1)
#define   C_028D24_HTILE_WIDTH                         0xFFFFFFFE
#define   S_028D24_HTILE_HEIGHT(x)                      (((x) & 0x1) << 1)
#define   G_028D24_HTILE_HEIGHT(x)                      (((x) >> 1) & 0x1)
#define   C_028D24_HTILE_HEIGHT                         0xFFFFFFFD
#define   G_028D24_LINEAR(x)                           (((x) >> 2) & 0x1)
#define	DB_WATERMARKS					0x9838
#define		DEPTH_FREE(x)					((x) << 0)
#define		DEPTH_FLUSH(x)					((x) << 5)
#define		DEPTH_PENDING_FREE(x)				((x) << 15)
#define		DEPTH_CACHELINE_FREE(x)				((x) << 20)

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

#define	GC_USER_SHADER_PIPE_CONFIG			0x8954
#define		INACTIVE_QD_PIPES(x)				((x) << 8)
#define		INACTIVE_QD_PIPES_MASK				0x0000FF00
#define		INACTIVE_SIMDS(x)				((x) << 16)
#define		INACTIVE_SIMDS_MASK				0x00FF0000

#define SQ_CONFIG                                         0x8c00
#       define VC_ENABLE                                  (1 << 0)
#       define EXPORT_SRC_C                               (1 << 1)
#       define DX9_CONSTS                                 (1 << 2)
#       define ALU_INST_PREFER_VECTOR                     (1 << 3)
#       define DX10_CLAMP                                 (1 << 4)
#       define CLAUSE_SEQ_PRIO(x)                         ((x) << 8)
#       define PS_PRIO(x)                                 ((x) << 24)
#       define VS_PRIO(x)                                 ((x) << 26)
#       define GS_PRIO(x)                                 ((x) << 28)
#       define ES_PRIO(x)                                 ((x) << 30)
#define SQ_GPR_RESOURCE_MGMT_1                            0x8c04
#       define NUM_PS_GPRS(x)                             ((x) << 0)
#       define NUM_VS_GPRS(x)                             ((x) << 16)
#       define NUM_CLAUSE_TEMP_GPRS(x)                    ((x) << 28)
#define SQ_GPR_RESOURCE_MGMT_2                            0x8c08
#       define NUM_GS_GPRS(x)                             ((x) << 0)
#       define NUM_ES_GPRS(x)                             ((x) << 16)
#define SQ_THREAD_RESOURCE_MGMT                           0x8c0c
#       define NUM_PS_THREADS(x)                          ((x) << 0)
#       define NUM_VS_THREADS(x)                          ((x) << 8)
#       define NUM_GS_THREADS(x)                          ((x) << 16)
#       define NUM_ES_THREADS(x)                          ((x) << 24)
#define SQ_STACK_RESOURCE_MGMT_1                          0x8c10
#       define NUM_PS_STACK_ENTRIES(x)                    ((x) << 0)
#       define NUM_VS_STACK_ENTRIES(x)                    ((x) << 16)
#define SQ_STACK_RESOURCE_MGMT_2                          0x8c14
#       define NUM_GS_STACK_ENTRIES(x)                    ((x) << 0)
#       define NUM_ES_STACK_ENTRIES(x)                    ((x) << 16)
#define SQ_ESGS_RING_BASE                               0x8c40
#define SQ_GSVS_RING_BASE                               0x8c48
#define SQ_ESTMP_RING_BASE                              0x8c50
#define SQ_GSTMP_RING_BASE                              0x8c58
#define SQ_VSTMP_RING_BASE                              0x8c60
#define SQ_PSTMP_RING_BASE                              0x8c68
#define SQ_FBUF_RING_BASE                               0x8c70
#define SQ_REDUC_RING_BASE                              0x8c78

#define GRBM_CNTL                                       0x8000
#       define GRBM_READ_TIMEOUT(x)                     ((x) << 0)
#define	GRBM_STATUS					0x8010
#define		CMDFIFO_AVAIL_MASK				0x0000001F
#define		GUI_ACTIVE					(1<<31)
#define	GRBM_STATUS2					0x8014
#define	GRBM_SOFT_RESET					0x8020
#define		SOFT_RESET_CP					(1<<0)

#define	CG_THERMAL_STATUS				0x7F4
#define		ASIC_T(x)			        ((x) << 0)
#define		ASIC_T_MASK			        0x1FF
#define		ASIC_T_SHIFT			        0

#define	HDP_HOST_PATH_CNTL				0x2C00
#define	HDP_NONSURFACE_BASE				0x2C04
#define	HDP_NONSURFACE_INFO				0x2C08
#define	HDP_NONSURFACE_SIZE				0x2C0C
#define HDP_REG_COHERENCY_FLUSH_CNTL			0x54A0
#define	HDP_TILING_CONFIG				0x2F3C
#define HDP_DEBUG1                                      0x2F34

#define MC_VM_AGP_TOP					0x2184
#define MC_VM_AGP_BOT					0x2188
#define	MC_VM_AGP_BASE					0x218C
#define MC_VM_FB_LOCATION				0x2180
#define MC_VM_L1_TLB_MCD_RD_A_CNTL			0x219C
#define 	ENABLE_L1_TLB					(1 << 0)
#define		ENABLE_L1_FRAGMENT_PROCESSING			(1 << 1)
#define		ENABLE_L1_STRICT_ORDERING			(1 << 2)
#define		SYSTEM_ACCESS_MODE_MASK				0x000000C0
#define		SYSTEM_ACCESS_MODE_SHIFT			6
#define		SYSTEM_ACCESS_MODE_PA_ONLY			(0 << 6)
#define		SYSTEM_ACCESS_MODE_USE_SYS_MAP			(1 << 6)
#define		SYSTEM_ACCESS_MODE_IN_SYS			(2 << 6)
#define		SYSTEM_ACCESS_MODE_NOT_IN_SYS			(3 << 6)
#define		SYSTEM_APERTURE_UNMAPPED_ACCESS_PASS_THRU	(0 << 8)
#define		SYSTEM_APERTURE_UNMAPPED_ACCESS_DEFAULT_PAGE	(1 << 8)
#define		ENABLE_SEMAPHORE_MODE				(1 << 10)
#define		ENABLE_WAIT_L2_QUERY				(1 << 11)
#define		EFFECTIVE_L1_TLB_SIZE(x)			(((x) & 7) << 12)
#define		EFFECTIVE_L1_TLB_SIZE_MASK			0x00007000
#define		EFFECTIVE_L1_TLB_SIZE_SHIFT			12
#define		EFFECTIVE_L1_QUEUE_SIZE(x)			(((x) & 7) << 15)
#define		EFFECTIVE_L1_QUEUE_SIZE_MASK			0x00038000
#define		EFFECTIVE_L1_QUEUE_SIZE_SHIFT			15
#define MC_VM_L1_TLB_MCD_RD_B_CNTL			0x21A0
#define MC_VM_L1_TLB_MCB_RD_GFX_CNTL			0x21FC
#define MC_VM_L1_TLB_MCB_RD_HDP_CNTL			0x2204
#define MC_VM_L1_TLB_MCB_RD_PDMA_CNTL			0x2208
#define MC_VM_L1_TLB_MCB_RD_SEM_CNTL			0x220C
#define	MC_VM_L1_TLB_MCB_RD_SYS_CNTL			0x2200
#define MC_VM_L1_TLB_MCD_WR_A_CNTL			0x21A4
#define MC_VM_L1_TLB_MCD_WR_B_CNTL			0x21A8
#define MC_VM_L1_TLB_MCB_WR_GFX_CNTL			0x2210
#define MC_VM_L1_TLB_MCB_WR_HDP_CNTL			0x2218
#define MC_VM_L1_TLB_MCB_WR_PDMA_CNTL			0x221C
#define MC_VM_L1_TLB_MCB_WR_SEM_CNTL			0x2220
#define MC_VM_L1_TLB_MCB_WR_SYS_CNTL			0x2214
#define MC_VM_SYSTEM_APERTURE_LOW_ADDR			0x2190
#define		LOGICAL_PAGE_NUMBER_MASK			0x000FFFFF
#define		LOGICAL_PAGE_NUMBER_SHIFT			0
#define MC_VM_SYSTEM_APERTURE_HIGH_ADDR			0x2194
#define MC_VM_SYSTEM_APERTURE_DEFAULT_ADDR		0x2198

#define	PA_CL_ENHANCE					0x8A14
#define		CLIP_VTX_REORDER_ENA				(1 << 0)
#define		NUM_CLIP_SEQ(x)					((x) << 1)
#define PA_SC_AA_CONFIG					0x28C04
#define	PA_SC_AA_SAMPLE_LOCS_2S				0x8B40
#define	PA_SC_AA_SAMPLE_LOCS_4S				0x8B44
#define	PA_SC_AA_SAMPLE_LOCS_8S_WD0			0x8B48
#define	PA_SC_AA_SAMPLE_LOCS_8S_WD1			0x8B4C
#define		S0_X(x)						((x) << 0)
#define		S0_Y(x)						((x) << 4)
#define		S1_X(x)						((x) << 8)
#define		S1_Y(x)						((x) << 12)
#define		S2_X(x)						((x) << 16)
#define		S2_Y(x)						((x) << 20)
#define		S3_X(x)						((x) << 24)
#define		S3_Y(x)						((x) << 28)
#define		S4_X(x)						((x) << 0)
#define		S4_Y(x)						((x) << 4)
#define		S5_X(x)						((x) << 8)
#define		S5_Y(x)						((x) << 12)
#define		S6_X(x)						((x) << 16)
#define		S6_Y(x)						((x) << 20)
#define		S7_X(x)						((x) << 24)
#define		S7_Y(x)						((x) << 28)
#define PA_SC_CLIPRECT_RULE				0x2820c
#define	PA_SC_ENHANCE					0x8BF0
#define		FORCE_EOV_MAX_CLK_CNT(x)			((x) << 0)
#define		FORCE_EOV_MAX_TILE_CNT(x)			((x) << 12)
#define PA_SC_LINE_STIPPLE				0x28A0C
#define	PA_SC_LINE_STIPPLE_STATE			0x8B10
#define PA_SC_MODE_CNTL					0x28A4C
#define	PA_SC_MULTI_CHIP_CNTL				0x8B20

#define PA_SC_SCREEN_SCISSOR_TL                         0x28030
#define PA_SC_GENERIC_SCISSOR_TL                        0x28240
#define PA_SC_WINDOW_SCISSOR_TL                         0x28204

#define	PCIE_PORT_INDEX					0x0038
#define	PCIE_PORT_DATA					0x003C

#define CHMAP						0x2004
#define		NOOFCHAN_SHIFT					12
#define		NOOFCHAN_MASK					0x00003000

#define RAMCFG						0x2408
#define		NOOFBANK_SHIFT					0
#define		NOOFBANK_MASK					0x00000001
#define		NOOFRANK_SHIFT					1
#define		NOOFRANK_MASK					0x00000002
#define		NOOFROWS_SHIFT					2
#define		NOOFROWS_MASK					0x0000001C
#define		NOOFCOLS_SHIFT					5
#define		NOOFCOLS_MASK					0x00000060
#define		CHANSIZE_SHIFT					7
#define		CHANSIZE_MASK					0x00000080
#define		BURSTLENGTH_SHIFT				8
#define		BURSTLENGTH_MASK				0x00000100
#define		CHANSIZE_OVERRIDE				(1 << 10)

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
#define	SPI_PS_IN_CONTROL_1				0x286D0
#define		GEN_INDEX_PIX					(1<<0)
#define		GEN_INDEX_PIX_ADDR(x)				((x)<<1)
#define		FRONT_FACE_ENA					(1<<8)
#define		FRONT_FACE_CHAN(x)				((x)<<9)
#define		FRONT_FACE_ALL_BITS				(1<<11)
#define		FRONT_FACE_ADDR(x)				((x)<<12)
#define		FOG_ADDR(x)					((x)<<17)
#define		FIXED_PT_POSITION_ENA				(1<<24)
#define		FIXED_PT_POSITION_ADDR(x)			((x)<<25)

#define	SQ_MS_FIFO_SIZES				0x8CF0
#define		CACHE_FIFO_SIZE(x)				((x) << 0)
#define		FETCH_FIFO_HIWATER(x)				((x) << 8)
#define		DONE_FIFO_HIWATER(x)				((x) << 16)
#define		ALU_UPDATE_FIFO_HIWATER(x)			((x) << 24)
#define	SQ_PGM_START_ES					0x28880
#define	SQ_PGM_START_FS					0x28894
#define	SQ_PGM_START_GS					0x2886C
#define	SQ_PGM_START_PS					0x28840
#define SQ_PGM_RESOURCES_PS                             0x28850
#define SQ_PGM_EXPORTS_PS                               0x28854
#define SQ_PGM_CF_OFFSET_PS                             0x288cc
#define	SQ_PGM_START_VS					0x28858
#define SQ_PGM_RESOURCES_VS                             0x28868
#define SQ_PGM_CF_OFFSET_VS                             0x288d0

#define SQ_VTX_CONSTANT_WORD0_0				0x30000
#define SQ_VTX_CONSTANT_WORD1_0				0x30004
#define SQ_VTX_CONSTANT_WORD2_0				0x30008
#	define SQ_VTXC_BASE_ADDR_HI(x)			((x) << 0)
#	define SQ_VTXC_STRIDE(x)			((x) << 8)
#	define SQ_VTXC_ENDIAN_SWAP(x)			((x) << 30)
#	define SQ_ENDIAN_NONE				0
#	define SQ_ENDIAN_8IN16				1
#	define SQ_ENDIAN_8IN32				2
#define SQ_VTX_CONSTANT_WORD3_0				0x3000c
#define	SQ_VTX_CONSTANT_WORD6_0				0x38018
#define		S__SQ_VTX_CONSTANT_TYPE(x)			(((x) & 3) << 30)
#define		G__SQ_VTX_CONSTANT_TYPE(x)			(((x) >> 30) & 3)
#define			SQ_TEX_VTX_INVALID_TEXTURE			0x0
#define			SQ_TEX_VTX_INVALID_BUFFER			0x1
#define			SQ_TEX_VTX_VALID_TEXTURE			0x2
#define			SQ_TEX_VTX_VALID_BUFFER				0x3


#define	SX_MISC						0x28350
#define	SX_MEMORY_EXPORT_BASE				0x9010
#define	SX_DEBUG_1					0x9054
#define		SMX_EVENT_RELEASE				(1 << 0)
#define		ENABLE_NEW_SMX_ADDRESS				(1 << 16)

#define	TA_CNTL_AUX					0x9508
#define		DISABLE_CUBE_WRAP				(1 << 0)
#define		DISABLE_CUBE_ANISO				(1 << 1)
#define		SYNC_GRADIENT					(1 << 24)
#define		SYNC_WALKER					(1 << 25)
#define		SYNC_ALIGNER					(1 << 26)
#define		BILINEAR_PRECISION_6_BIT			(0 << 31)
#define		BILINEAR_PRECISION_8_BIT			(1 << 31)

#define	TC_CNTL						0x9608
#define		TC_L2_SIZE(x)					((x)<<5)
#define		L2_DISABLE_LATE_HIT				(1<<9)


#define	VGT_CACHE_INVALIDATION				0x88C4
#define		CACHE_INVALIDATION(x)				((x)<<0)
#define			VC_ONLY						0
#define			TC_ONLY						1
#define			VC_AND_TC					2
#define	VGT_DMA_BASE					0x287E8
#define	VGT_DMA_BASE_HI					0x287E4
#define	VGT_ES_PER_GS					0x88CC
#define	VGT_GS_PER_ES					0x88C8
#define	VGT_GS_PER_VS					0x88E8
#define	VGT_GS_VERTEX_REUSE				0x88D4
#define VGT_PRIMITIVE_TYPE                              0x8958
#define	VGT_NUM_INSTANCES				0x8974
#define	VGT_OUT_DEALLOC_CNTL				0x28C5C
#define		DEALLOC_DIST_MASK				0x0000007F
#define	VGT_STRMOUT_BASE_OFFSET_0			0x28B10
#define	VGT_STRMOUT_BASE_OFFSET_1			0x28B14
#define	VGT_STRMOUT_BASE_OFFSET_2			0x28B18
#define	VGT_STRMOUT_BASE_OFFSET_3			0x28B1c
#define	VGT_STRMOUT_BASE_OFFSET_HI_0			0x28B44
#define	VGT_STRMOUT_BASE_OFFSET_HI_1			0x28B48
#define	VGT_STRMOUT_BASE_OFFSET_HI_2			0x28B4c
#define	VGT_STRMOUT_BASE_OFFSET_HI_3			0x28B50
#define	VGT_STRMOUT_BUFFER_BASE_0			0x28AD8
#define	VGT_STRMOUT_BUFFER_BASE_1			0x28AE8
#define	VGT_STRMOUT_BUFFER_BASE_2			0x28AF8
#define	VGT_STRMOUT_BUFFER_BASE_3			0x28B08
#define	VGT_STRMOUT_BUFFER_OFFSET_0			0x28ADC
#define	VGT_STRMOUT_BUFFER_OFFSET_1			0x28AEC
#define	VGT_STRMOUT_BUFFER_OFFSET_2			0x28AFC
#define	VGT_STRMOUT_BUFFER_OFFSET_3			0x28B0C
#define VGT_STRMOUT_BUFFER_SIZE_0			0x28AD0
#define VGT_STRMOUT_BUFFER_SIZE_1			0x28AE0
#define VGT_STRMOUT_BUFFER_SIZE_2			0x28AF0
#define VGT_STRMOUT_BUFFER_SIZE_3			0x28B00

#define	VGT_STRMOUT_EN					0x28AB0
#define	VGT_VERTEX_REUSE_BLOCK_CNTL			0x28C58
#define		VTX_REUSE_DEPTH_MASK				0x000000FF
#define VGT_EVENT_INITIATOR                             0x28a90
#       define CACHE_FLUSH_AND_INV_EVENT_TS                     (0x14 << 0)
#       define CACHE_FLUSH_AND_INV_EVENT                        (0x16 << 0)

#define VM_CONTEXT0_CNTL				0x1410
#define		ENABLE_CONTEXT					(1 << 0)
#define		PAGE_TABLE_DEPTH(x)				(((x) & 3) << 1)
#define		RANGE_PROTECTION_FAULT_ENABLE_DEFAULT		(1 << 4)
#define VM_CONTEXT0_INVALIDATION_LOW_ADDR		0x1490
#define VM_CONTEXT0_INVALIDATION_HIGH_ADDR		0x14B0
#define VM_CONTEXT0_PAGE_TABLE_BASE_ADDR		0x1574
#define VM_CONTEXT0_PAGE_TABLE_START_ADDR		0x1594
#define VM_CONTEXT0_PAGE_TABLE_END_ADDR			0x15B4
#define VM_CONTEXT0_PROTECTION_FAULT_DEFAULT_ADDR	0x1554
#define VM_CONTEXT0_REQUEST_RESPONSE			0x1470
#define		REQUEST_TYPE(x)					(((x) & 0xf) << 0)
#define		RESPONSE_TYPE_MASK				0x000000F0
#define		RESPONSE_TYPE_SHIFT				4
#define VM_L2_CNTL					0x1400
#define		ENABLE_L2_CACHE					(1 << 0)
#define		ENABLE_L2_FRAGMENT_PROCESSING			(1 << 1)
#define		ENABLE_L2_PTE_CACHE_LRU_UPDATE_BY_WRITE		(1 << 9)
#define		EFFECTIVE_L2_QUEUE_SIZE(x)			(((x) & 7) << 13)
#define VM_L2_CNTL2					0x1404
#define		INVALIDATE_ALL_L1_TLBS				(1 << 0)
#define		INVALIDATE_L2_CACHE				(1 << 1)
#define VM_L2_CNTL3					0x1408
#define		BANK_SELECT_0(x)				(((x) & 0x1f) << 0)
#define		BANK_SELECT_1(x)				(((x) & 0x1f) << 5)
#define		L2_CACHE_UPDATE_MODE(x)				(((x) & 3) << 10)
#define	VM_L2_STATUS					0x140C
#define		L2_BUSY						(1 << 0)

#define	WAIT_UNTIL					0x8040
#define         WAIT_2D_IDLE_bit                                (1 << 14)
#define         WAIT_3D_IDLE_bit                                (1 << 15)
#define         WAIT_2D_IDLECLEAN_bit                           (1 << 16)
#define         WAIT_3D_IDLECLEAN_bit                           (1 << 17)

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

#define RLC_CNTL                                          0x3f00
#       define RLC_ENABLE                                 (1 << 0)
#define RLC_HB_BASE                                       0x3f10
#define RLC_HB_CNTL                                       0x3f0c
#define RLC_HB_RPTR                                       0x3f20
#define RLC_HB_WPTR                                       0x3f1c
#define RLC_HB_WPTR_LSB_ADDR                              0x3f14
#define RLC_HB_WPTR_MSB_ADDR                              0x3f18
#define RLC_MC_CNTL                                       0x3f44
#define RLC_UCODE_CNTL                                    0x3f48
#define RLC_UCODE_ADDR                                    0x3f2c
#define RLC_UCODE_DATA                                    0x3f30

/* new for TN */
#define TN_RLC_SAVE_AND_RESTORE_BASE                      0x3f10
#define TN_RLC_CLEAR_STATE_RESTORE_BASE                   0x3f20

#define SRBM_SOFT_RESET                                   0xe60
#       define SOFT_RESET_RLC                             (1 << 13)

#define CP_INT_CNTL                                       0xc124
#       define CNTX_BUSY_INT_ENABLE                       (1 << 19)
#       define CNTX_EMPTY_INT_ENABLE                      (1 << 20)
#       define SCRATCH_INT_ENABLE                         (1 << 25)
#       define TIME_STAMP_INT_ENABLE                      (1 << 26)
#       define IB2_INT_ENABLE                             (1 << 29)
#       define IB1_INT_ENABLE                             (1 << 30)
#       define RB_INT_ENABLE                              (1 << 31)
#define CP_INT_STATUS                                     0xc128
#       define SCRATCH_INT_STAT                           (1 << 25)
#       define TIME_STAMP_INT_STAT                        (1 << 26)
#       define IB2_INT_STAT                               (1 << 29)
#       define IB1_INT_STAT                               (1 << 30)
#       define RB_INT_STAT                                (1 << 31)

#define GRBM_INT_CNTL                                     0x8060
#       define RDERR_INT_ENABLE                           (1 << 0)
#       define WAIT_COUNT_TIMEOUT_INT_ENABLE              (1 << 1)
#       define GUI_IDLE_INT_ENABLE                        (1 << 19)

#define INTERRUPT_CNTL                                    0x5468
#       define IH_DUMMY_RD_OVERRIDE                       (1 << 0)
#       define IH_DUMMY_RD_EN                             (1 << 1)
#       define IH_REQ_NONSNOOP_EN                         (1 << 3)
#       define GEN_IH_INT_EN                              (1 << 8)
#define INTERRUPT_CNTL2                                   0x546c

#define D1MODE_VBLANK_STATUS                              0x6534
#define D2MODE_VBLANK_STATUS                              0x6d34
#       define DxMODE_VBLANK_OCCURRED                     (1 << 0)
#       define DxMODE_VBLANK_ACK                          (1 << 4)
#       define DxMODE_VBLANK_STAT                         (1 << 12)
#       define DxMODE_VBLANK_INTERRUPT                    (1 << 16)
#       define DxMODE_VBLANK_INTERRUPT_TYPE               (1 << 17)
#define D1MODE_VLINE_STATUS                               0x653c
#define D2MODE_VLINE_STATUS                               0x6d3c
#       define DxMODE_VLINE_OCCURRED                      (1 << 0)
#       define DxMODE_VLINE_ACK                           (1 << 4)
#       define DxMODE_VLINE_STAT                          (1 << 12)
#       define DxMODE_VLINE_INTERRUPT                     (1 << 16)
#       define DxMODE_VLINE_INTERRUPT_TYPE                (1 << 17)
#define DxMODE_INT_MASK                                   0x6540
#       define D1MODE_VBLANK_INT_MASK                     (1 << 0)
#       define D1MODE_VLINE_INT_MASK                      (1 << 4)
#       define D2MODE_VBLANK_INT_MASK                     (1 << 8)
#       define D2MODE_VLINE_INT_MASK                      (1 << 12)
#define DCE3_DISP_INTERRUPT_STATUS                        0x7ddc
#       define DC_HPD1_INTERRUPT                          (1 << 18)
#       define DC_HPD2_INTERRUPT                          (1 << 19)
#define DISP_INTERRUPT_STATUS                             0x7edc
#       define LB_D1_VLINE_INTERRUPT                      (1 << 2)
#       define LB_D2_VLINE_INTERRUPT                      (1 << 3)
#       define LB_D1_VBLANK_INTERRUPT                     (1 << 4)
#       define LB_D2_VBLANK_INTERRUPT                     (1 << 5)
#       define DACA_AUTODETECT_INTERRUPT                  (1 << 16)
#       define DACB_AUTODETECT_INTERRUPT                  (1 << 17)
#       define DC_HOT_PLUG_DETECT1_INTERRUPT              (1 << 18)
#       define DC_HOT_PLUG_DETECT2_INTERRUPT              (1 << 19)
#       define DC_I2C_SW_DONE_INTERRUPT                   (1 << 20)
#       define DC_I2C_HW_DONE_INTERRUPT                   (1 << 21)
#define DISP_INTERRUPT_STATUS_CONTINUE                    0x7ee8
#define DCE3_DISP_INTERRUPT_STATUS_CONTINUE               0x7de8
#       define DC_HPD4_INTERRUPT                          (1 << 14)
#       define DC_HPD4_RX_INTERRUPT                       (1 << 15)
#       define DC_HPD3_INTERRUPT                          (1 << 28)
#       define DC_HPD1_RX_INTERRUPT                       (1 << 29)
#       define DC_HPD2_RX_INTERRUPT                       (1 << 30)
#define DCE3_DISP_INTERRUPT_STATUS_CONTINUE2              0x7dec
#       define DC_HPD3_RX_INTERRUPT                       (1 << 0)
#       define DIGA_DP_VID_STREAM_DISABLE_INTERRUPT       (1 << 1)
#       define DIGA_DP_STEER_FIFO_OVERFLOW_INTERRUPT      (1 << 2)
#       define DIGB_DP_VID_STREAM_DISABLE_INTERRUPT       (1 << 3)
#       define DIGB_DP_STEER_FIFO_OVERFLOW_INTERRUPT      (1 << 4)
#       define AUX1_SW_DONE_INTERRUPT                     (1 << 5)
#       define AUX1_LS_DONE_INTERRUPT                     (1 << 6)
#       define AUX2_SW_DONE_INTERRUPT                     (1 << 7)
#       define AUX2_LS_DONE_INTERRUPT                     (1 << 8)
#       define AUX3_SW_DONE_INTERRUPT                     (1 << 9)
#       define AUX3_LS_DONE_INTERRUPT                     (1 << 10)
#       define AUX4_SW_DONE_INTERRUPT                     (1 << 11)
#       define AUX4_LS_DONE_INTERRUPT                     (1 << 12)
#       define DIGA_DP_FAST_TRAINING_COMPLETE_INTERRUPT   (1 << 13)
#       define DIGB_DP_FAST_TRAINING_COMPLETE_INTERRUPT   (1 << 14)
/* DCE 3.2 */
#       define AUX5_SW_DONE_INTERRUPT                     (1 << 15)
#       define AUX5_LS_DONE_INTERRUPT                     (1 << 16)
#       define AUX6_SW_DONE_INTERRUPT                     (1 << 17)
#       define AUX6_LS_DONE_INTERRUPT                     (1 << 18)
#       define DC_HPD5_INTERRUPT                          (1 << 19)
#       define DC_HPD5_RX_INTERRUPT                       (1 << 20)
#       define DC_HPD6_INTERRUPT                          (1 << 21)
#       define DC_HPD6_RX_INTERRUPT                       (1 << 22)

#define DACA_AUTO_DETECT_CONTROL                          0x7828
#define DACB_AUTO_DETECT_CONTROL                          0x7a28
#define DCE3_DACA_AUTO_DETECT_CONTROL                     0x7028
#define DCE3_DACB_AUTO_DETECT_CONTROL                     0x7128
#       define DACx_AUTODETECT_MODE(x)                    ((x) << 0)
#       define DACx_AUTODETECT_MODE_NONE                  0
#       define DACx_AUTODETECT_MODE_CONNECT               1
#       define DACx_AUTODETECT_MODE_DISCONNECT            2
#       define DACx_AUTODETECT_FRAME_TIME_COUNTER(x)      ((x) << 8)
/* bit 18 = R/C, 17 = G/Y, 16 = B/Comp */
#       define DACx_AUTODETECT_CHECK_MASK(x)              ((x) << 16)

#define DCE3_DACA_AUTODETECT_INT_CONTROL                  0x7038
#define DCE3_DACB_AUTODETECT_INT_CONTROL                  0x7138
#define DACA_AUTODETECT_INT_CONTROL                       0x7838
#define DACB_AUTODETECT_INT_CONTROL                       0x7a38
#       define DACx_AUTODETECT_ACK                        (1 << 0)
#       define DACx_AUTODETECT_INT_ENABLE                 (1 << 16)

#define DC_HOT_PLUG_DETECT1_CONTROL                       0x7d00
#define DC_HOT_PLUG_DETECT2_CONTROL                       0x7d10
#define DC_HOT_PLUG_DETECT3_CONTROL                       0x7d24
#       define DC_HOT_PLUG_DETECTx_EN                     (1 << 0)

#define DC_HOT_PLUG_DETECT1_INT_STATUS                    0x7d04
#define DC_HOT_PLUG_DETECT2_INT_STATUS                    0x7d14
#define DC_HOT_PLUG_DETECT3_INT_STATUS                    0x7d28
#       define DC_HOT_PLUG_DETECTx_INT_STATUS             (1 << 0)
#       define DC_HOT_PLUG_DETECTx_SENSE                  (1 << 1)

/* DCE 3.0 */
#define DC_HPD1_INT_STATUS                                0x7d00
#define DC_HPD2_INT_STATUS                                0x7d0c
#define DC_HPD3_INT_STATUS                                0x7d18
#define DC_HPD4_INT_STATUS                                0x7d24
/* DCE 3.2 */
#define DC_HPD5_INT_STATUS                                0x7dc0
#define DC_HPD6_INT_STATUS                                0x7df4
#       define DC_HPDx_INT_STATUS                         (1 << 0)
#       define DC_HPDx_SENSE                              (1 << 1)
#       define DC_HPDx_RX_INT_STATUS                      (1 << 8)

#define DC_HOT_PLUG_DETECT1_INT_CONTROL                   0x7d08
#define DC_HOT_PLUG_DETECT2_INT_CONTROL                   0x7d18
#define DC_HOT_PLUG_DETECT3_INT_CONTROL                   0x7d2c
#       define DC_HOT_PLUG_DETECTx_INT_ACK                (1 << 0)
#       define DC_HOT_PLUG_DETECTx_INT_POLARITY           (1 << 8)
#       define DC_HOT_PLUG_DETECTx_INT_EN                 (1 << 16)
/* DCE 3.0 */
#define DC_HPD1_INT_CONTROL                               0x7d04
#define DC_HPD2_INT_CONTROL                               0x7d10
#define DC_HPD3_INT_CONTROL                               0x7d1c
#define DC_HPD4_INT_CONTROL                               0x7d28
/* DCE 3.2 */
#define DC_HPD5_INT_CONTROL                               0x7dc4
#define DC_HPD6_INT_CONTROL                               0x7df8
#       define DC_HPDx_INT_ACK                            (1 << 0)
#       define DC_HPDx_INT_POLARITY                       (1 << 8)
#       define DC_HPDx_INT_EN                             (1 << 16)
#       define DC_HPDx_RX_INT_ACK                         (1 << 20)
#       define DC_HPDx_RX_INT_EN                          (1 << 24)

/* DCE 3.0 */
#define DC_HPD1_CONTROL                                   0x7d08
#define DC_HPD2_CONTROL                                   0x7d14
#define DC_HPD3_CONTROL                                   0x7d20
#define DC_HPD4_CONTROL                                   0x7d2c
/* DCE 3.2 */
#define DC_HPD5_CONTROL                                   0x7dc8
#define DC_HPD6_CONTROL                                   0x7dfc
#       define DC_HPDx_CONNECTION_TIMER(x)                ((x) << 0)
#       define DC_HPDx_RX_INT_TIMER(x)                    ((x) << 16)
/* DCE 3.2 */
#       define DC_HPDx_EN                                 (1 << 28)

#define D1GRPH_INTERRUPT_STATUS                           0x6158
#define D2GRPH_INTERRUPT_STATUS                           0x6958
#       define DxGRPH_PFLIP_INT_OCCURRED                  (1 << 0)
#       define DxGRPH_PFLIP_INT_CLEAR                     (1 << 8)
#define D1GRPH_INTERRUPT_CONTROL                          0x615c
#define D2GRPH_INTERRUPT_CONTROL                          0x695c
#       define DxGRPH_PFLIP_INT_MASK                      (1 << 0)
#       define DxGRPH_PFLIP_INT_TYPE                      (1 << 8)

/* PCIE link stuff */
#define PCIE_LC_TRAINING_CNTL                             0xa1 /* PCIE_P */
#       define LC_POINT_7_PLUS_EN                         (1 << 6)
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
#       define LC_VOLTAGE_TIMER_SEL_MASK                  (0xf << 14)
#       define LC_CLR_FAILED_SPD_CHANGE_CNT               (1 << 21)
#       define LC_OTHER_SIDE_EVER_SENT_GEN2               (1 << 23)
#       define LC_OTHER_SIDE_SUPPORTS_GEN2                (1 << 24)
#define MM_CFGREGS_CNTL                                   0x544c
#       define MM_WR_TO_CFG_EN                            (1 << 3)
#define LINK_CNTL2                                        0x88 /* F0 */
#       define TARGET_LINK_SPEED_MASK                     (0xf << 0)
#       define SELECTABLE_DEEMPHASIS                      (1 << 6)

/* Audio clocks */
#define DCCG_AUDIO_DTO0_PHASE             0x0514
#define DCCG_AUDIO_DTO0_MODULE            0x0518
#define DCCG_AUDIO_DTO0_LOAD              0x051c
#       define DTO_LOAD                   (1 << 31)
#define DCCG_AUDIO_DTO0_CNTL              0x0520

#define DCCG_AUDIO_DTO1_PHASE             0x0524
#define DCCG_AUDIO_DTO1_MODULE            0x0528
#define DCCG_AUDIO_DTO1_LOAD              0x052c
#define DCCG_AUDIO_DTO1_CNTL              0x0530

#define DCCG_AUDIO_DTO_SELECT             0x0534

/* digital blocks */
#define TMDSA_CNTL                       0x7880
#       define TMDSA_HDMI_EN             (1 << 2)
#define LVTMA_CNTL                       0x7a80
#       define LVTMA_HDMI_EN             (1 << 2)
#define DDIA_CNTL                        0x7200
#       define DDIA_HDMI_EN              (1 << 2)
#define DIG0_CNTL                        0x75a0
#       define DIG_MODE(x)               (((x) & 7) << 8)
#       define DIG_MODE_DP               0
#       define DIG_MODE_LVDS             1
#       define DIG_MODE_TMDS_DVI         2
#       define DIG_MODE_TMDS_HDMI        3
#       define DIG_MODE_SDVO             4
#define DIG1_CNTL                        0x79a0

/* rs6xx/rs740 and r6xx share the same HDMI blocks, however, rs6xx has only one
 * instance of the blocks while r6xx has 2.  DCE 3.0 cards are slightly
 * different due to the new DIG blocks, but also have 2 instances.
 * DCE 3.0 HDMI blocks are part of each DIG encoder.
 */

/* rs6xx/rs740/r6xx/dce3 */
#define HDMI0_CONTROL                0x7400
/* rs6xx/rs740/r6xx */
#       define HDMI0_ENABLE          (1 << 0)
#       define HDMI0_STREAM(x)       (((x) & 3) << 2)
#       define HDMI0_STREAM_TMDSA    0
#       define HDMI0_STREAM_LVTMA    1
#       define HDMI0_STREAM_DVOA     2
#       define HDMI0_STREAM_DDIA     3
/* rs6xx/r6xx/dce3 */
#       define HDMI0_ERROR_ACK       (1 << 8)
#       define HDMI0_ERROR_MASK      (1 << 9)
#define HDMI0_STATUS                 0x7404
#       define HDMI0_ACTIVE_AVMUTE   (1 << 0)
#       define HDMI0_AUDIO_ENABLE    (1 << 4)
#       define HDMI0_AZ_FORMAT_WTRIG     (1 << 28)
#       define HDMI0_AZ_FORMAT_WTRIG_INT (1 << 29)
#define HDMI0_AUDIO_PACKET_CONTROL   0x7408
#       define HDMI0_AUDIO_SAMPLE_SEND  (1 << 0)
#       define HDMI0_AUDIO_DELAY_EN(x)  (((x) & 3) << 4)
#       define HDMI0_AUDIO_SEND_MAX_PACKETS  (1 << 8)
#       define HDMI0_AUDIO_TEST_EN         (1 << 12)
#       define HDMI0_AUDIO_PACKETS_PER_LINE(x)  (((x) & 0x1f) << 16)
#       define HDMI0_AUDIO_CHANNEL_SWAP    (1 << 24)
#       define HDMI0_60958_CS_UPDATE       (1 << 26)
#       define HDMI0_AZ_FORMAT_WTRIG_MASK  (1 << 28)
#       define HDMI0_AZ_FORMAT_WTRIG_ACK   (1 << 29)
#define HDMI0_AUDIO_CRC_CONTROL      0x740c
#       define HDMI0_AUDIO_CRC_EN    (1 << 0)
#define HDMI0_VBI_PACKET_CONTROL     0x7410
#       define HDMI0_NULL_SEND       (1 << 0)
#       define HDMI0_GC_SEND         (1 << 4)
#       define HDMI0_GC_CONT         (1 << 5) /* 0 - once; 1 - every frame */
#define HDMI0_INFOFRAME_CONTROL0     0x7414
#       define HDMI0_AVI_INFO_SEND   (1 << 0)
#       define HDMI0_AVI_INFO_CONT   (1 << 1)
#       define HDMI0_AUDIO_INFO_SEND (1 << 4)
#       define HDMI0_AUDIO_INFO_CONT (1 << 5)
#       define HDMI0_AUDIO_INFO_SOURCE (1 << 6) /* 0 - sound block; 1 - hmdi regs */
#       define HDMI0_AUDIO_INFO_UPDATE (1 << 7)
#       define HDMI0_MPEG_INFO_SEND  (1 << 8)
#       define HDMI0_MPEG_INFO_CONT  (1 << 9)
#       define HDMI0_MPEG_INFO_UPDATE  (1 << 10)
#define HDMI0_INFOFRAME_CONTROL1     0x7418
#       define HDMI0_AVI_INFO_LINE(x)  (((x) & 0x3f) << 0)
#       define HDMI0_AUDIO_INFO_LINE(x)  (((x) & 0x3f) << 8)
#       define HDMI0_MPEG_INFO_LINE(x)  (((x) & 0x3f) << 16)
#define HDMI0_GENERIC_PACKET_CONTROL 0x741c
#       define HDMI0_GENERIC0_SEND   (1 << 0)
#       define HDMI0_GENERIC0_CONT   (1 << 1)
#       define HDMI0_GENERIC0_UPDATE (1 << 2)
#       define HDMI0_GENERIC1_SEND   (1 << 4)
#       define HDMI0_GENERIC1_CONT   (1 << 5)
#       define HDMI0_GENERIC0_LINE(x)  (((x) & 0x3f) << 16)
#       define HDMI0_GENERIC1_LINE(x)  (((x) & 0x3f) << 24)
#define HDMI0_GC                     0x7428
#       define HDMI0_GC_AVMUTE       (1 << 0)
#define HDMI0_AVI_INFO0              0x7454
#       define HDMI0_AVI_INFO_CHECKSUM(x)  (((x) & 0xff) << 0)
#       define HDMI0_AVI_INFO_S(x)   (((x) & 3) << 8)
#       define HDMI0_AVI_INFO_B(x)   (((x) & 3) << 10)
#       define HDMI0_AVI_INFO_A(x)   (((x) & 1) << 12)
#       define HDMI0_AVI_INFO_Y(x)   (((x) & 3) << 13)
#       define HDMI0_AVI_INFO_Y_RGB       0
#       define HDMI0_AVI_INFO_Y_YCBCR422  1
#       define HDMI0_AVI_INFO_Y_YCBCR444  2
#       define HDMI0_AVI_INFO_Y_A_B_S(x)   (((x) & 0xff) << 8)
#       define HDMI0_AVI_INFO_R(x)   (((x) & 0xf) << 16)
#       define HDMI0_AVI_INFO_M(x)   (((x) & 0x3) << 20)
#       define HDMI0_AVI_INFO_C(x)   (((x) & 0x3) << 22)
#       define HDMI0_AVI_INFO_C_M_R(x)   (((x) & 0xff) << 16)
#       define HDMI0_AVI_INFO_SC(x)  (((x) & 0x3) << 24)
#       define HDMI0_AVI_INFO_ITC_EC_Q_SC(x)  (((x) & 0xff) << 24)
#define HDMI0_AVI_INFO1              0x7458
#       define HDMI0_AVI_INFO_VIC(x) (((x) & 0x7f) << 0) /* don't use avi infoframe v1 */
#       define HDMI0_AVI_INFO_PR(x)  (((x) & 0xf) << 8) /* don't use avi infoframe v1 */
#       define HDMI0_AVI_INFO_TOP(x) (((x) & 0xffff) << 16)
#define HDMI0_AVI_INFO2              0x745c
#       define HDMI0_AVI_INFO_BOTTOM(x)  (((x) & 0xffff) << 0)
#       define HDMI0_AVI_INFO_LEFT(x)    (((x) & 0xffff) << 16)
#define HDMI0_AVI_INFO3              0x7460
#       define HDMI0_AVI_INFO_RIGHT(x)    (((x) & 0xffff) << 0)
#       define HDMI0_AVI_INFO_VERSION(x)  (((x) & 3) << 24)
#define HDMI0_MPEG_INFO0             0x7464
#       define HDMI0_MPEG_INFO_CHECKSUM(x)  (((x) & 0xff) << 0)
#       define HDMI0_MPEG_INFO_MB0(x)  (((x) & 0xff) << 8)
#       define HDMI0_MPEG_INFO_MB1(x)  (((x) & 0xff) << 16)
#       define HDMI0_MPEG_INFO_MB2(x)  (((x) & 0xff) << 24)
#define HDMI0_MPEG_INFO1             0x7468
#       define HDMI0_MPEG_INFO_MB3(x)  (((x) & 0xff) << 0)
#       define HDMI0_MPEG_INFO_MF(x)   (((x) & 3) << 8)
#       define HDMI0_MPEG_INFO_FR(x)   (((x) & 1) << 12)
#define HDMI0_GENERIC0_HDR           0x746c
#define HDMI0_GENERIC0_0             0x7470
#define HDMI0_GENERIC0_1             0x7474
#define HDMI0_GENERIC0_2             0x7478
#define HDMI0_GENERIC0_3             0x747c
#define HDMI0_GENERIC0_4             0x7480
#define HDMI0_GENERIC0_5             0x7484
#define HDMI0_GENERIC0_6             0x7488
#define HDMI0_GENERIC1_HDR           0x748c
#define HDMI0_GENERIC1_0             0x7490
#define HDMI0_GENERIC1_1             0x7494
#define HDMI0_GENERIC1_2             0x7498
#define HDMI0_GENERIC1_3             0x749c
#define HDMI0_GENERIC1_4             0x74a0
#define HDMI0_GENERIC1_5             0x74a4
#define HDMI0_GENERIC1_6             0x74a8
#define HDMI0_ACR_32_0               0x74ac
#       define HDMI0_ACR_CTS_32(x)   (((x) & 0xfffff) << 12)
#define HDMI0_ACR_32_1               0x74b0
#       define HDMI0_ACR_N_32(x)   (((x) & 0xfffff) << 0)
#define HDMI0_ACR_44_0               0x74b4
#       define HDMI0_ACR_CTS_44(x)   (((x) & 0xfffff) << 12)
#define HDMI0_ACR_44_1               0x74b8
#       define HDMI0_ACR_N_44(x)   (((x) & 0xfffff) << 0)
#define HDMI0_ACR_48_0               0x74bc
#       define HDMI0_ACR_CTS_48(x)   (((x) & 0xfffff) << 12)
#define HDMI0_ACR_48_1               0x74c0
#       define HDMI0_ACR_N_48(x)   (((x) & 0xfffff) << 0)
#define HDMI0_ACR_STATUS_0           0x74c4
#define HDMI0_ACR_STATUS_1           0x74c8
#define HDMI0_AUDIO_INFO0            0x74cc
#       define HDMI0_AUDIO_INFO_CHECKSUM(x)  (((x) & 0xff) << 0)
#       define HDMI0_AUDIO_INFO_CC(x)  (((x) & 7) << 8)
#define HDMI0_AUDIO_INFO1            0x74d0
#       define HDMI0_AUDIO_INFO_CA(x)  (((x) & 0xff) << 0)
#       define HDMI0_AUDIO_INFO_LSV(x)  (((x) & 0xf) << 11)
#       define HDMI0_AUDIO_INFO_DM_INH(x)  (((x) & 1) << 15)
#       define HDMI0_AUDIO_INFO_DM_INH_LSV(x)  (((x) & 0xff) << 8)
#define HDMI0_60958_0                0x74d4
#       define HDMI0_60958_CS_A(x)   (((x) & 1) << 0)
#       define HDMI0_60958_CS_B(x)   (((x) & 1) << 1)
#       define HDMI0_60958_CS_C(x)   (((x) & 1) << 2)
#       define HDMI0_60958_CS_D(x)   (((x) & 3) << 3)
#       define HDMI0_60958_CS_MODE(x)   (((x) & 3) << 6)
#       define HDMI0_60958_CS_CATEGORY_CODE(x)      (((x) & 0xff) << 8)
#       define HDMI0_60958_CS_SOURCE_NUMBER(x)      (((x) & 0xf) << 16)
#       define HDMI0_60958_CS_CHANNEL_NUMBER_L(x)   (((x) & 0xf) << 20)
#       define HDMI0_60958_CS_SAMPLING_FREQUENCY(x) (((x) & 0xf) << 24)
#       define HDMI0_60958_CS_CLOCK_ACCURACY(x)     (((x) & 3) << 28)
#define HDMI0_60958_1                0x74d8
#       define HDMI0_60958_CS_WORD_LENGTH(x)        (((x) & 0xf) << 0)
#       define HDMI0_60958_CS_ORIGINAL_SAMPLING_FREQUENCY(x)   (((x) & 0xf) << 4)
#       define HDMI0_60958_CS_VALID_L(x)   (((x) & 1) << 16)
#       define HDMI0_60958_CS_VALID_R(x)   (((x) & 1) << 18)
#       define HDMI0_60958_CS_CHANNEL_NUMBER_R(x)   (((x) & 0xf) << 20)
#define HDMI0_ACR_PACKET_CONTROL     0x74dc
#       define HDMI0_ACR_SEND        (1 << 0)
#       define HDMI0_ACR_CONT        (1 << 1)
#       define HDMI0_ACR_SELECT(x)   (((x) & 3) << 4)
#       define HDMI0_ACR_HW          0
#       define HDMI0_ACR_32          1
#       define HDMI0_ACR_44          2
#       define HDMI0_ACR_48          3
#       define HDMI0_ACR_SOURCE      (1 << 8) /* 0 - hw; 1 - cts value */
#       define HDMI0_ACR_AUTO_SEND   (1 << 12)
#define HDMI0_RAMP_CONTROL0          0x74e0
#       define HDMI0_RAMP_MAX_COUNT(x)   (((x) & 0xffffff) << 0)
#define HDMI0_RAMP_CONTROL1          0x74e4
#       define HDMI0_RAMP_MIN_COUNT(x)   (((x) & 0xffffff) << 0)
#define HDMI0_RAMP_CONTROL2          0x74e8
#       define HDMI0_RAMP_INC_COUNT(x)   (((x) & 0xffffff) << 0)
#define HDMI0_RAMP_CONTROL3          0x74ec
#       define HDMI0_RAMP_DEC_COUNT(x)   (((x) & 0xffffff) << 0)
/* HDMI0_60958_2 is r7xx only */
#define HDMI0_60958_2                0x74f0
#       define HDMI0_60958_CS_CHANNEL_NUMBER_2(x)   (((x) & 0xf) << 0)
#       define HDMI0_60958_CS_CHANNEL_NUMBER_3(x)   (((x) & 0xf) << 4)
#       define HDMI0_60958_CS_CHANNEL_NUMBER_4(x)   (((x) & 0xf) << 8)
#       define HDMI0_60958_CS_CHANNEL_NUMBER_5(x)   (((x) & 0xf) << 12)
#       define HDMI0_60958_CS_CHANNEL_NUMBER_6(x)   (((x) & 0xf) << 16)
#       define HDMI0_60958_CS_CHANNEL_NUMBER_7(x)   (((x) & 0xf) << 20)
/* r6xx only; second instance starts at 0x7700 */
#define HDMI1_CONTROL                0x7700
#define HDMI1_STATUS                 0x7704
#define HDMI1_AUDIO_PACKET_CONTROL   0x7708
/* DCE3; second instance starts at 0x7800 NOT 0x7700 */
#define DCE3_HDMI1_CONTROL                0x7800
#define DCE3_HDMI1_STATUS                 0x7804
#define DCE3_HDMI1_AUDIO_PACKET_CONTROL   0x7808
/* DCE3.2 (for interrupts) */
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

/*
 * PM4
 */
#define	PACKET_TYPE0	0
#define	PACKET_TYPE1	1
#define	PACKET_TYPE2	2
#define	PACKET_TYPE3	3

#define CP_PACKET_GET_TYPE(h) (((h) >> 30) & 3)
#define CP_PACKET_GET_COUNT(h) (((h) >> 16) & 0x3FFF)
#define CP_PACKET0_GET_REG(h) (((h) & 0xFFFF) << 2)
#define CP_PACKET3_GET_OPCODE(h) (((h) >> 8) & 0xFF)
#define PACKET0(reg, n)	((PACKET_TYPE0 << 30) |				\
			 (((reg) >> 2) & 0xFFFF) |			\
			 ((n) & 0x3FFF) << 16)
#define PACKET3(op, n)	((PACKET_TYPE3 << 30) |				\
			 (((op) & 0xFF) << 8) |				\
			 ((n) & 0x3FFF) << 16)

/* Packet 3 types */
#define	PACKET3_NOP					0x10
#define	PACKET3_INDIRECT_BUFFER_END			0x17
#define	PACKET3_SET_PREDICATION				0x20
#define	PACKET3_REG_RMW					0x21
#define	PACKET3_COND_EXEC				0x22
#define	PACKET3_PRED_EXEC				0x23
#define	PACKET3_START_3D_CMDBUF				0x24
#define	PACKET3_DRAW_INDEX_2				0x27
#define	PACKET3_CONTEXT_CONTROL				0x28
#define	PACKET3_DRAW_INDEX_IMMD_BE			0x29
#define	PACKET3_INDEX_TYPE				0x2A
#define	PACKET3_DRAW_INDEX				0x2B
#define	PACKET3_DRAW_INDEX_AUTO				0x2D
#define	PACKET3_DRAW_INDEX_IMMD				0x2E
#define	PACKET3_NUM_INSTANCES				0x2F
#define	PACKET3_STRMOUT_BUFFER_UPDATE			0x34
#define	PACKET3_INDIRECT_BUFFER_MP			0x38
#define	PACKET3_MEM_SEMAPHORE				0x39
#              define PACKET3_SEM_WAIT_ON_SIGNAL    (0x1 << 12)
#              define PACKET3_SEM_SEL_SIGNAL	    (0x6 << 29)
#              define PACKET3_SEM_SEL_WAIT	    (0x7 << 29)
#define	PACKET3_MPEG_INDEX				0x3A
#define	PACKET3_COPY_DW					0x3B
#define	PACKET3_WAIT_REG_MEM				0x3C
#define	PACKET3_MEM_WRITE				0x3D
#define	PACKET3_INDIRECT_BUFFER				0x32
#define	PACKET3_SURFACE_SYNC				0x43
#              define PACKET3_CB0_DEST_BASE_ENA    (1 << 6)
#              define PACKET3_TC_ACTION_ENA        (1 << 23)
#              define PACKET3_VC_ACTION_ENA        (1 << 24)
#              define PACKET3_CB_ACTION_ENA        (1 << 25)
#              define PACKET3_DB_ACTION_ENA        (1 << 26)
#              define PACKET3_SH_ACTION_ENA        (1 << 27)
#              define PACKET3_SMX_ACTION_ENA       (1 << 28)
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
		 * 5 - TS events
		 */
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
#define	PACKET3_ONE_REG_WRITE				0x57
#define	PACKET3_SET_CONFIG_REG				0x68
#define		PACKET3_SET_CONFIG_REG_OFFSET			0x00008000
#define		PACKET3_SET_CONFIG_REG_END			0x0000ac00
#define	PACKET3_SET_CONTEXT_REG				0x69
#define		PACKET3_SET_CONTEXT_REG_OFFSET			0x00028000
#define		PACKET3_SET_CONTEXT_REG_END			0x00029000
#define	PACKET3_SET_ALU_CONST				0x6A
#define		PACKET3_SET_ALU_CONST_OFFSET			0x00030000
#define		PACKET3_SET_ALU_CONST_END			0x00032000
#define	PACKET3_SET_BOOL_CONST				0x6B
#define		PACKET3_SET_BOOL_CONST_OFFSET			0x0003e380
#define		PACKET3_SET_BOOL_CONST_END			0x00040000
#define	PACKET3_SET_LOOP_CONST				0x6C
#define		PACKET3_SET_LOOP_CONST_OFFSET			0x0003e200
#define		PACKET3_SET_LOOP_CONST_END			0x0003e380
#define	PACKET3_SET_RESOURCE				0x6D
#define		PACKET3_SET_RESOURCE_OFFSET			0x00038000
#define		PACKET3_SET_RESOURCE_END			0x0003c000
#define	PACKET3_SET_SAMPLER				0x6E
#define		PACKET3_SET_SAMPLER_OFFSET			0x0003c000
#define		PACKET3_SET_SAMPLER_END				0x0003cff0
#define	PACKET3_SET_CTL_CONST				0x6F
#define		PACKET3_SET_CTL_CONST_OFFSET			0x0003cff0
#define		PACKET3_SET_CTL_CONST_END			0x0003e200
#define	PACKET3_SURFACE_BASE_UPDATE			0x73


#define	R_008020_GRBM_SOFT_RESET		0x8020
#define		S_008020_SOFT_RESET_CP(x)		(((x) & 1) << 0)
#define		S_008020_SOFT_RESET_CB(x)		(((x) & 1) << 1)
#define		S_008020_SOFT_RESET_CR(x)		(((x) & 1) << 2)
#define		S_008020_SOFT_RESET_DB(x)		(((x) & 1) << 3)
#define		S_008020_SOFT_RESET_PA(x)		(((x) & 1) << 5)
#define		S_008020_SOFT_RESET_SC(x)		(((x) & 1) << 6)
#define		S_008020_SOFT_RESET_SMX(x)		(((x) & 1) << 7)
#define		S_008020_SOFT_RESET_SPI(x)		(((x) & 1) << 8)
#define		S_008020_SOFT_RESET_SH(x)		(((x) & 1) << 9)
#define		S_008020_SOFT_RESET_SX(x)		(((x) & 1) << 10)
#define		S_008020_SOFT_RESET_TC(x)		(((x) & 1) << 11)
#define		S_008020_SOFT_RESET_TA(x)		(((x) & 1) << 12)
#define		S_008020_SOFT_RESET_VC(x)		(((x) & 1) << 13)
#define		S_008020_SOFT_RESET_VGT(x)		(((x) & 1) << 14)
#define	R_008010_GRBM_STATUS			0x8010
#define		S_008010_CMDFIFO_AVAIL(x)		(((x) & 0x1F) << 0)
#define		S_008010_CP_RQ_PENDING(x)		(((x) & 1) << 6)
#define		S_008010_CF_RQ_PENDING(x)		(((x) & 1) << 7)
#define		S_008010_PF_RQ_PENDING(x)		(((x) & 1) << 8)
#define		S_008010_GRBM_EE_BUSY(x)		(((x) & 1) << 10)
#define		S_008010_VC_BUSY(x)			(((x) & 1) << 11)
#define		S_008010_DB03_CLEAN(x)			(((x) & 1) << 12)
#define		S_008010_CB03_CLEAN(x)			(((x) & 1) << 13)
#define		S_008010_VGT_BUSY_NO_DMA(x)		(((x) & 1) << 16)
#define		S_008010_VGT_BUSY(x)			(((x) & 1) << 17)
#define		S_008010_TA03_BUSY(x)			(((x) & 1) << 18)
#define		S_008010_TC_BUSY(x)			(((x) & 1) << 19)
#define		S_008010_SX_BUSY(x)			(((x) & 1) << 20)
#define		S_008010_SH_BUSY(x)			(((x) & 1) << 21)
#define		S_008010_SPI03_BUSY(x)			(((x) & 1) << 22)
#define		S_008010_SMX_BUSY(x)			(((x) & 1) << 23)
#define		S_008010_SC_BUSY(x)			(((x) & 1) << 24)
#define		S_008010_PA_BUSY(x)			(((x) & 1) << 25)
#define		S_008010_DB03_BUSY(x)			(((x) & 1) << 26)
#define		S_008010_CR_BUSY(x)			(((x) & 1) << 27)
#define		S_008010_CP_COHERENCY_BUSY(x)		(((x) & 1) << 28)
#define		S_008010_CP_BUSY(x)			(((x) & 1) << 29)
#define		S_008010_CB03_BUSY(x)			(((x) & 1) << 30)
#define		S_008010_GUI_ACTIVE(x)			(((x) & 1) << 31)
#define		G_008010_CMDFIFO_AVAIL(x)		(((x) >> 0) & 0x1F)
#define		G_008010_CP_RQ_PENDING(x)		(((x) >> 6) & 1)
#define		G_008010_CF_RQ_PENDING(x)		(((x) >> 7) & 1)
#define		G_008010_PF_RQ_PENDING(x)		(((x) >> 8) & 1)
#define		G_008010_GRBM_EE_BUSY(x)		(((x) >> 10) & 1)
#define		G_008010_VC_BUSY(x)			(((x) >> 11) & 1)
#define		G_008010_DB03_CLEAN(x)			(((x) >> 12) & 1)
#define		G_008010_CB03_CLEAN(x)			(((x) >> 13) & 1)
#define		G_008010_VGT_BUSY_NO_DMA(x)		(((x) >> 16) & 1)
#define		G_008010_VGT_BUSY(x)			(((x) >> 17) & 1)
#define		G_008010_TA03_BUSY(x)			(((x) >> 18) & 1)
#define		G_008010_TC_BUSY(x)			(((x) >> 19) & 1)
#define		G_008010_SX_BUSY(x)			(((x) >> 20) & 1)
#define		G_008010_SH_BUSY(x)			(((x) >> 21) & 1)
#define		G_008010_SPI03_BUSY(x)			(((x) >> 22) & 1)
#define		G_008010_SMX_BUSY(x)			(((x) >> 23) & 1)
#define		G_008010_SC_BUSY(x)			(((x) >> 24) & 1)
#define		G_008010_PA_BUSY(x)			(((x) >> 25) & 1)
#define		G_008010_DB03_BUSY(x)			(((x) >> 26) & 1)
#define		G_008010_CR_BUSY(x)			(((x) >> 27) & 1)
#define		G_008010_CP_COHERENCY_BUSY(x)		(((x) >> 28) & 1)
#define		G_008010_CP_BUSY(x)			(((x) >> 29) & 1)
#define		G_008010_CB03_BUSY(x)			(((x) >> 30) & 1)
#define		G_008010_GUI_ACTIVE(x)			(((x) >> 31) & 1)
#define	R_008014_GRBM_STATUS2			0x8014
#define		S_008014_CR_CLEAN(x)			(((x) & 1) << 0)
#define		S_008014_SMX_CLEAN(x)			(((x) & 1) << 1)
#define		S_008014_SPI0_BUSY(x)			(((x) & 1) << 8)
#define		S_008014_SPI1_BUSY(x)			(((x) & 1) << 9)
#define		S_008014_SPI2_BUSY(x)			(((x) & 1) << 10)
#define		S_008014_SPI3_BUSY(x)			(((x) & 1) << 11)
#define		S_008014_TA0_BUSY(x)			(((x) & 1) << 12)
#define		S_008014_TA1_BUSY(x)			(((x) & 1) << 13)
#define		S_008014_TA2_BUSY(x)			(((x) & 1) << 14)
#define		S_008014_TA3_BUSY(x)			(((x) & 1) << 15)
#define		S_008014_DB0_BUSY(x)			(((x) & 1) << 16)
#define		S_008014_DB1_BUSY(x)			(((x) & 1) << 17)
#define		S_008014_DB2_BUSY(x)			(((x) & 1) << 18)
#define		S_008014_DB3_BUSY(x)			(((x) & 1) << 19)
#define		S_008014_CB0_BUSY(x)			(((x) & 1) << 20)
#define		S_008014_CB1_BUSY(x)			(((x) & 1) << 21)
#define		S_008014_CB2_BUSY(x)			(((x) & 1) << 22)
#define		S_008014_CB3_BUSY(x)			(((x) & 1) << 23)
#define		G_008014_CR_CLEAN(x)			(((x) >> 0) & 1)
#define		G_008014_SMX_CLEAN(x)			(((x) >> 1) & 1)
#define		G_008014_SPI0_BUSY(x)			(((x) >> 8) & 1)
#define		G_008014_SPI1_BUSY(x)			(((x) >> 9) & 1)
#define		G_008014_SPI2_BUSY(x)			(((x) >> 10) & 1)
#define		G_008014_SPI3_BUSY(x)			(((x) >> 11) & 1)
#define		G_008014_TA0_BUSY(x)			(((x) >> 12) & 1)
#define		G_008014_TA1_BUSY(x)			(((x) >> 13) & 1)
#define		G_008014_TA2_BUSY(x)			(((x) >> 14) & 1)
#define		G_008014_TA3_BUSY(x)			(((x) >> 15) & 1)
#define		G_008014_DB0_BUSY(x)			(((x) >> 16) & 1)
#define		G_008014_DB1_BUSY(x)			(((x) >> 17) & 1)
#define		G_008014_DB2_BUSY(x)			(((x) >> 18) & 1)
#define		G_008014_DB3_BUSY(x)			(((x) >> 19) & 1)
#define		G_008014_CB0_BUSY(x)			(((x) >> 20) & 1)
#define		G_008014_CB1_BUSY(x)			(((x) >> 21) & 1)
#define		G_008014_CB2_BUSY(x)			(((x) >> 22) & 1)
#define		G_008014_CB3_BUSY(x)			(((x) >> 23) & 1)
#define	R_000E50_SRBM_STATUS				0x0E50
#define		G_000E50_RLC_RQ_PENDING(x)		(((x) >> 3) & 1)
#define		G_000E50_RCU_RQ_PENDING(x)		(((x) >> 4) & 1)
#define		G_000E50_GRBM_RQ_PENDING(x)		(((x) >> 5) & 1)
#define		G_000E50_HI_RQ_PENDING(x)		(((x) >> 6) & 1)
#define		G_000E50_IO_EXTERN_SIGNAL(x)		(((x) >> 7) & 1)
#define		G_000E50_VMC_BUSY(x)			(((x) >> 8) & 1)
#define		G_000E50_MCB_BUSY(x)			(((x) >> 9) & 1)
#define		G_000E50_MCDZ_BUSY(x)			(((x) >> 10) & 1)
#define		G_000E50_MCDY_BUSY(x)			(((x) >> 11) & 1)
#define		G_000E50_MCDX_BUSY(x)			(((x) >> 12) & 1)
#define		G_000E50_MCDW_BUSY(x)			(((x) >> 13) & 1)
#define		G_000E50_SEM_BUSY(x)			(((x) >> 14) & 1)
#define		G_000E50_RLC_BUSY(x)			(((x) >> 15) & 1)
#define		G_000E50_BIF_BUSY(x)			(((x) >> 29) & 1)
#define	R_000E60_SRBM_SOFT_RESET			0x0E60
#define		S_000E60_SOFT_RESET_BIF(x)		(((x) & 1) << 1)
#define		S_000E60_SOFT_RESET_CG(x)		(((x) & 1) << 2)
#define		S_000E60_SOFT_RESET_CMC(x)		(((x) & 1) << 3)
#define		S_000E60_SOFT_RESET_CSC(x)		(((x) & 1) << 4)
#define		S_000E60_SOFT_RESET_DC(x)		(((x) & 1) << 5)
#define		S_000E60_SOFT_RESET_GRBM(x)		(((x) & 1) << 8)
#define		S_000E60_SOFT_RESET_HDP(x)		(((x) & 1) << 9)
#define		S_000E60_SOFT_RESET_IH(x)		(((x) & 1) << 10)
#define		S_000E60_SOFT_RESET_MC(x)		(((x) & 1) << 11)
#define		S_000E60_SOFT_RESET_RLC(x)		(((x) & 1) << 13)
#define		S_000E60_SOFT_RESET_ROM(x)		(((x) & 1) << 14)
#define		S_000E60_SOFT_RESET_SEM(x)		(((x) & 1) << 15)
#define		S_000E60_SOFT_RESET_TSC(x)		(((x) & 1) << 16)
#define		S_000E60_SOFT_RESET_VMC(x)		(((x) & 1) << 17)

#define R_005480_HDP_MEM_COHERENCY_FLUSH_CNTL		0x5480

#define R_028C04_PA_SC_AA_CONFIG                     0x028C04
#define   S_028C04_MSAA_NUM_SAMPLES(x)                 (((x) & 0x3) << 0)
#define   G_028C04_MSAA_NUM_SAMPLES(x)                 (((x) >> 0) & 0x3)
#define   C_028C04_MSAA_NUM_SAMPLES                    0xFFFFFFFC
#define   S_028C04_AA_MASK_CENTROID_DTMN(x)            (((x) & 0x1) << 4)
#define   G_028C04_AA_MASK_CENTROID_DTMN(x)            (((x) >> 4) & 0x1)
#define   C_028C04_AA_MASK_CENTROID_DTMN               0xFFFFFFEF
#define   S_028C04_MAX_SAMPLE_DIST(x)                  (((x) & 0xF) << 13)
#define   G_028C04_MAX_SAMPLE_DIST(x)                  (((x) >> 13) & 0xF)
#define   C_028C04_MAX_SAMPLE_DIST                     0xFFFE1FFF
#define R_0280E0_CB_COLOR0_FRAG                      0x0280E0
#define   S_0280E0_BASE_256B(x)                        (((x) & 0xFFFFFFFF) << 0)
#define   G_0280E0_BASE_256B(x)                        (((x) >> 0) & 0xFFFFFFFF)
#define   C_0280E0_BASE_256B                           0x00000000
#define R_0280E4_CB_COLOR1_FRAG                      0x0280E4
#define R_0280E8_CB_COLOR2_FRAG                      0x0280E8
#define R_0280EC_CB_COLOR3_FRAG                      0x0280EC
#define R_0280F0_CB_COLOR4_FRAG                      0x0280F0
#define R_0280F4_CB_COLOR5_FRAG                      0x0280F4
#define R_0280F8_CB_COLOR6_FRAG                      0x0280F8
#define R_0280FC_CB_COLOR7_FRAG                      0x0280FC
#define R_0280C0_CB_COLOR0_TILE                      0x0280C0
#define   S_0280C0_BASE_256B(x)                        (((x) & 0xFFFFFFFF) << 0)
#define   G_0280C0_BASE_256B(x)                        (((x) >> 0) & 0xFFFFFFFF)
#define   C_0280C0_BASE_256B                           0x00000000
#define R_0280C4_CB_COLOR1_TILE                      0x0280C4
#define R_0280C8_CB_COLOR2_TILE                      0x0280C8
#define R_0280CC_CB_COLOR3_TILE                      0x0280CC
#define R_0280D0_CB_COLOR4_TILE                      0x0280D0
#define R_0280D4_CB_COLOR5_TILE                      0x0280D4
#define R_0280D8_CB_COLOR6_TILE                      0x0280D8
#define R_0280DC_CB_COLOR7_TILE                      0x0280DC
#define R_0280A0_CB_COLOR0_INFO                      0x0280A0
#define   S_0280A0_ENDIAN(x)                           (((x) & 0x3) << 0)
#define   G_0280A0_ENDIAN(x)                           (((x) >> 0) & 0x3)
#define   C_0280A0_ENDIAN                              0xFFFFFFFC
#define   S_0280A0_FORMAT(x)                           (((x) & 0x3F) << 2)
#define   G_0280A0_FORMAT(x)                           (((x) >> 2) & 0x3F)
#define   C_0280A0_FORMAT                              0xFFFFFF03
#define     V_0280A0_COLOR_INVALID                     0x00000000
#define     V_0280A0_COLOR_8                           0x00000001
#define     V_0280A0_COLOR_4_4                         0x00000002
#define     V_0280A0_COLOR_3_3_2                       0x00000003
#define     V_0280A0_COLOR_16                          0x00000005
#define     V_0280A0_COLOR_16_FLOAT                    0x00000006
#define     V_0280A0_COLOR_8_8                         0x00000007
#define     V_0280A0_COLOR_5_6_5                       0x00000008
#define     V_0280A0_COLOR_6_5_5                       0x00000009
#define     V_0280A0_COLOR_1_5_5_5                     0x0000000A
#define     V_0280A0_COLOR_4_4_4_4                     0x0000000B
#define     V_0280A0_COLOR_5_5_5_1                     0x0000000C
#define     V_0280A0_COLOR_32                          0x0000000D
#define     V_0280A0_COLOR_32_FLOAT                    0x0000000E
#define     V_0280A0_COLOR_16_16                       0x0000000F
#define     V_0280A0_COLOR_16_16_FLOAT                 0x00000010
#define     V_0280A0_COLOR_8_24                        0x00000011
#define     V_0280A0_COLOR_8_24_FLOAT                  0x00000012
#define     V_0280A0_COLOR_24_8                        0x00000013
#define     V_0280A0_COLOR_24_8_FLOAT                  0x00000014
#define     V_0280A0_COLOR_10_11_11                    0x00000015
#define     V_0280A0_COLOR_10_11_11_FLOAT              0x00000016
#define     V_0280A0_COLOR_11_11_10                    0x00000017
#define     V_0280A0_COLOR_11_11_10_FLOAT              0x00000018
#define     V_0280A0_COLOR_2_10_10_10                  0x00000019
#define     V_0280A0_COLOR_8_8_8_8                     0x0000001A
#define     V_0280A0_COLOR_10_10_10_2                  0x0000001B
#define     V_0280A0_COLOR_X24_8_32_FLOAT              0x0000001C
#define     V_0280A0_COLOR_32_32                       0x0000001D
#define     V_0280A0_COLOR_32_32_FLOAT                 0x0000001E
#define     V_0280A0_COLOR_16_16_16_16                 0x0000001F
#define     V_0280A0_COLOR_16_16_16_16_FLOAT           0x00000020
#define     V_0280A0_COLOR_32_32_32_32                 0x00000022
#define     V_0280A0_COLOR_32_32_32_32_FLOAT           0x00000023
#define   S_0280A0_ARRAY_MODE(x)                       (((x) & 0xF) << 8)
#define   G_0280A0_ARRAY_MODE(x)                       (((x) >> 8) & 0xF)
#define   C_0280A0_ARRAY_MODE                          0xFFFFF0FF
#define     V_0280A0_ARRAY_LINEAR_GENERAL              0x00000000
#define     V_0280A0_ARRAY_LINEAR_ALIGNED              0x00000001
#define     V_0280A0_ARRAY_1D_TILED_THIN1              0x00000002
#define     V_0280A0_ARRAY_2D_TILED_THIN1              0x00000004
#define   S_0280A0_NUMBER_TYPE(x)                      (((x) & 0x7) << 12)
#define   G_0280A0_NUMBER_TYPE(x)                      (((x) >> 12) & 0x7)
#define   C_0280A0_NUMBER_TYPE                         0xFFFF8FFF
#define   S_0280A0_READ_SIZE(x)                        (((x) & 0x1) << 15)
#define   G_0280A0_READ_SIZE(x)                        (((x) >> 15) & 0x1)
#define   C_0280A0_READ_SIZE                           0xFFFF7FFF
#define   S_0280A0_COMP_SWAP(x)                        (((x) & 0x3) << 16)
#define   G_0280A0_COMP_SWAP(x)                        (((x) >> 16) & 0x3)
#define   C_0280A0_COMP_SWAP                           0xFFFCFFFF
#define   S_0280A0_TILE_MODE(x)                        (((x) & 0x3) << 18)
#define   G_0280A0_TILE_MODE(x)                        (((x) >> 18) & 0x3)
#define   C_0280A0_TILE_MODE                           0xFFF3FFFF
#define   S_0280A0_BLEND_CLAMP(x)                      (((x) & 0x1) << 20)
#define   G_0280A0_BLEND_CLAMP(x)                      (((x) >> 20) & 0x1)
#define   C_0280A0_BLEND_CLAMP                         0xFFEFFFFF
#define   S_0280A0_CLEAR_COLOR(x)                      (((x) & 0x1) << 21)
#define   G_0280A0_CLEAR_COLOR(x)                      (((x) >> 21) & 0x1)
#define   C_0280A0_CLEAR_COLOR                         0xFFDFFFFF
#define   S_0280A0_BLEND_BYPASS(x)                     (((x) & 0x1) << 22)
#define   G_0280A0_BLEND_BYPASS(x)                     (((x) >> 22) & 0x1)
#define   C_0280A0_BLEND_BYPASS                        0xFFBFFFFF
#define   S_0280A0_BLEND_FLOAT32(x)                    (((x) & 0x1) << 23)
#define   G_0280A0_BLEND_FLOAT32(x)                    (((x) >> 23) & 0x1)
#define   C_0280A0_BLEND_FLOAT32                       0xFF7FFFFF
#define   S_0280A0_SIMPLE_FLOAT(x)                     (((x) & 0x1) << 24)
#define   G_0280A0_SIMPLE_FLOAT(x)                     (((x) >> 24) & 0x1)
#define   C_0280A0_SIMPLE_FLOAT                        0xFEFFFFFF
#define   S_0280A0_ROUND_MODE(x)                       (((x) & 0x1) << 25)
#define   G_0280A0_ROUND_MODE(x)                       (((x) >> 25) & 0x1)
#define   C_0280A0_ROUND_MODE                          0xFDFFFFFF
#define   S_0280A0_TILE_COMPACT(x)                     (((x) & 0x1) << 26)
#define   G_0280A0_TILE_COMPACT(x)                     (((x) >> 26) & 0x1)
#define   C_0280A0_TILE_COMPACT                        0xFBFFFFFF
#define   S_0280A0_SOURCE_FORMAT(x)                    (((x) & 0x1) << 27)
#define   G_0280A0_SOURCE_FORMAT(x)                    (((x) >> 27) & 0x1)
#define   C_0280A0_SOURCE_FORMAT                       0xF7FFFFFF
#define R_0280A4_CB_COLOR1_INFO                      0x0280A4
#define R_0280A8_CB_COLOR2_INFO                      0x0280A8
#define R_0280AC_CB_COLOR3_INFO                      0x0280AC
#define R_0280B0_CB_COLOR4_INFO                      0x0280B0
#define R_0280B4_CB_COLOR5_INFO                      0x0280B4
#define R_0280B8_CB_COLOR6_INFO                      0x0280B8
#define R_0280BC_CB_COLOR7_INFO                      0x0280BC
#define R_028060_CB_COLOR0_SIZE                      0x028060
#define   S_028060_PITCH_TILE_MAX(x)                   (((x) & 0x3FF) << 0)
#define   G_028060_PITCH_TILE_MAX(x)                   (((x) >> 0) & 0x3FF)
#define   C_028060_PITCH_TILE_MAX                      0xFFFFFC00
#define   S_028060_SLICE_TILE_MAX(x)                   (((x) & 0xFFFFF) << 10)
#define   G_028060_SLICE_TILE_MAX(x)                   (((x) >> 10) & 0xFFFFF)
#define   C_028060_SLICE_TILE_MAX                      0xC00003FF
#define R_028064_CB_COLOR1_SIZE                      0x028064
#define R_028068_CB_COLOR2_SIZE                      0x028068
#define R_02806C_CB_COLOR3_SIZE                      0x02806C
#define R_028070_CB_COLOR4_SIZE                      0x028070
#define R_028074_CB_COLOR5_SIZE                      0x028074
#define R_028078_CB_COLOR6_SIZE                      0x028078
#define R_02807C_CB_COLOR7_SIZE                      0x02807C
#define R_028238_CB_TARGET_MASK                      0x028238
#define   S_028238_TARGET0_ENABLE(x)                   (((x) & 0xF) << 0)
#define   G_028238_TARGET0_ENABLE(x)                   (((x) >> 0) & 0xF)
#define   C_028238_TARGET0_ENABLE                      0xFFFFFFF0
#define   S_028238_TARGET1_ENABLE(x)                   (((x) & 0xF) << 4)
#define   G_028238_TARGET1_ENABLE(x)                   (((x) >> 4) & 0xF)
#define   C_028238_TARGET1_ENABLE                      0xFFFFFF0F
#define   S_028238_TARGET2_ENABLE(x)                   (((x) & 0xF) << 8)
#define   G_028238_TARGET2_ENABLE(x)                   (((x) >> 8) & 0xF)
#define   C_028238_TARGET2_ENABLE                      0xFFFFF0FF
#define   S_028238_TARGET3_ENABLE(x)                   (((x) & 0xF) << 12)
#define   G_028238_TARGET3_ENABLE(x)                   (((x) >> 12) & 0xF)
#define   C_028238_TARGET3_ENABLE                      0xFFFF0FFF
#define   S_028238_TARGET4_ENABLE(x)                   (((x) & 0xF) << 16)
#define   G_028238_TARGET4_ENABLE(x)                   (((x) >> 16) & 0xF)
#define   C_028238_TARGET4_ENABLE                      0xFFF0FFFF
#define   S_028238_TARGET5_ENABLE(x)                   (((x) & 0xF) << 20)
#define   G_028238_TARGET5_ENABLE(x)                   (((x) >> 20) & 0xF)
#define   C_028238_TARGET5_ENABLE                      0xFF0FFFFF
#define   S_028238_TARGET6_ENABLE(x)                   (((x) & 0xF) << 24)
#define   G_028238_TARGET6_ENABLE(x)                   (((x) >> 24) & 0xF)
#define   C_028238_TARGET6_ENABLE                      0xF0FFFFFF
#define   S_028238_TARGET7_ENABLE(x)                   (((x) & 0xF) << 28)
#define   G_028238_TARGET7_ENABLE(x)                   (((x) >> 28) & 0xF)
#define   C_028238_TARGET7_ENABLE                      0x0FFFFFFF
#define R_02823C_CB_SHADER_MASK                      0x02823C
#define   S_02823C_OUTPUT0_ENABLE(x)                   (((x) & 0xF) << 0)
#define   G_02823C_OUTPUT0_ENABLE(x)                   (((x) >> 0) & 0xF)
#define   C_02823C_OUTPUT0_ENABLE                      0xFFFFFFF0
#define   S_02823C_OUTPUT1_ENABLE(x)                   (((x) & 0xF) << 4)
#define   G_02823C_OUTPUT1_ENABLE(x)                   (((x) >> 4) & 0xF)
#define   C_02823C_OUTPUT1_ENABLE                      0xFFFFFF0F
#define   S_02823C_OUTPUT2_ENABLE(x)                   (((x) & 0xF) << 8)
#define   G_02823C_OUTPUT2_ENABLE(x)                   (((x) >> 8) & 0xF)
#define   C_02823C_OUTPUT2_ENABLE                      0xFFFFF0FF
#define   S_02823C_OUTPUT3_ENABLE(x)                   (((x) & 0xF) << 12)
#define   G_02823C_OUTPUT3_ENABLE(x)                   (((x) >> 12) & 0xF)
#define   C_02823C_OUTPUT3_ENABLE                      0xFFFF0FFF
#define   S_02823C_OUTPUT4_ENABLE(x)                   (((x) & 0xF) << 16)
#define   G_02823C_OUTPUT4_ENABLE(x)                   (((x) >> 16) & 0xF)
#define   C_02823C_OUTPUT4_ENABLE                      0xFFF0FFFF
#define   S_02823C_OUTPUT5_ENABLE(x)                   (((x) & 0xF) << 20)
#define   G_02823C_OUTPUT5_ENABLE(x)                   (((x) >> 20) & 0xF)
#define   C_02823C_OUTPUT5_ENABLE                      0xFF0FFFFF
#define   S_02823C_OUTPUT6_ENABLE(x)                   (((x) & 0xF) << 24)
#define   G_02823C_OUTPUT6_ENABLE(x)                   (((x) >> 24) & 0xF)
#define   C_02823C_OUTPUT6_ENABLE                      0xF0FFFFFF
#define   S_02823C_OUTPUT7_ENABLE(x)                   (((x) & 0xF) << 28)
#define   G_02823C_OUTPUT7_ENABLE(x)                   (((x) >> 28) & 0xF)
#define   C_02823C_OUTPUT7_ENABLE                      0x0FFFFFFF
#define R_028AB0_VGT_STRMOUT_EN                      0x028AB0
#define   S_028AB0_STREAMOUT(x)                        (((x) & 0x1) << 0)
#define   G_028AB0_STREAMOUT(x)                        (((x) >> 0) & 0x1)
#define   C_028AB0_STREAMOUT                           0xFFFFFFFE
#define R_028B20_VGT_STRMOUT_BUFFER_EN               0x028B20
#define   S_028B20_BUFFER_0_EN(x)                      (((x) & 0x1) << 0)
#define   G_028B20_BUFFER_0_EN(x)                      (((x) >> 0) & 0x1)
#define   C_028B20_BUFFER_0_EN                         0xFFFFFFFE
#define   S_028B20_BUFFER_1_EN(x)                      (((x) & 0x1) << 1)
#define   G_028B20_BUFFER_1_EN(x)                      (((x) >> 1) & 0x1)
#define   C_028B20_BUFFER_1_EN                         0xFFFFFFFD
#define   S_028B20_BUFFER_2_EN(x)                      (((x) & 0x1) << 2)
#define   G_028B20_BUFFER_2_EN(x)                      (((x) >> 2) & 0x1)
#define   C_028B20_BUFFER_2_EN                         0xFFFFFFFB
#define   S_028B20_BUFFER_3_EN(x)                      (((x) & 0x1) << 3)
#define   G_028B20_BUFFER_3_EN(x)                      (((x) >> 3) & 0x1)
#define   C_028B20_BUFFER_3_EN                         0xFFFFFFF7
#define   S_028B20_SIZE(x)                             (((x) & 0xFFFFFFFF) << 0)
#define   G_028B20_SIZE(x)                             (((x) >> 0) & 0xFFFFFFFF)
#define   C_028B20_SIZE                                0x00000000
#define R_038000_SQ_TEX_RESOURCE_WORD0_0             0x038000
#define   S_038000_DIM(x)                              (((x) & 0x7) << 0)
#define   G_038000_DIM(x)                              (((x) >> 0) & 0x7)
#define   C_038000_DIM                                 0xFFFFFFF8
#define     V_038000_SQ_TEX_DIM_1D                     0x00000000
#define     V_038000_SQ_TEX_DIM_2D                     0x00000001
#define     V_038000_SQ_TEX_DIM_3D                     0x00000002
#define     V_038000_SQ_TEX_DIM_CUBEMAP                0x00000003
#define     V_038000_SQ_TEX_DIM_1D_ARRAY               0x00000004
#define     V_038000_SQ_TEX_DIM_2D_ARRAY               0x00000005
#define     V_038000_SQ_TEX_DIM_2D_MSAA                0x00000006
#define     V_038000_SQ_TEX_DIM_2D_ARRAY_MSAA          0x00000007
#define   S_038000_TILE_MODE(x)                        (((x) & 0xF) << 3)
#define   G_038000_TILE_MODE(x)                        (((x) >> 3) & 0xF)
#define   C_038000_TILE_MODE                           0xFFFFFF87
#define     V_038000_ARRAY_LINEAR_GENERAL              0x00000000
#define     V_038000_ARRAY_LINEAR_ALIGNED              0x00000001
#define     V_038000_ARRAY_1D_TILED_THIN1              0x00000002
#define     V_038000_ARRAY_2D_TILED_THIN1              0x00000004
#define   S_038000_TILE_TYPE(x)                        (((x) & 0x1) << 7)
#define   G_038000_TILE_TYPE(x)                        (((x) >> 7) & 0x1)
#define   C_038000_TILE_TYPE                           0xFFFFFF7F
#define   S_038000_PITCH(x)                            (((x) & 0x7FF) << 8)
#define   G_038000_PITCH(x)                            (((x) >> 8) & 0x7FF)
#define   C_038000_PITCH                               0xFFF800FF
#define   S_038000_TEX_WIDTH(x)                        (((x) & 0x1FFF) << 19)
#define   G_038000_TEX_WIDTH(x)                        (((x) >> 19) & 0x1FFF)
#define   C_038000_TEX_WIDTH                           0x0007FFFF
#define R_038004_SQ_TEX_RESOURCE_WORD1_0             0x038004
#define   S_038004_TEX_HEIGHT(x)                       (((x) & 0x1FFF) << 0)
#define   G_038004_TEX_HEIGHT(x)                       (((x) >> 0) & 0x1FFF)
#define   C_038004_TEX_HEIGHT                          0xFFFFE000
#define   S_038004_TEX_DEPTH(x)                        (((x) & 0x1FFF) << 13)
#define   G_038004_TEX_DEPTH(x)                        (((x) >> 13) & 0x1FFF)
#define   C_038004_TEX_DEPTH                           0xFC001FFF
#define   S_038004_DATA_FORMAT(x)                      (((x) & 0x3F) << 26)
#define   G_038004_DATA_FORMAT(x)                      (((x) >> 26) & 0x3F)
#define   C_038004_DATA_FORMAT                         0x03FFFFFF
#define     V_038004_COLOR_INVALID                     0x00000000
#define     V_038004_COLOR_8                           0x00000001
#define     V_038004_COLOR_4_4                         0x00000002
#define     V_038004_COLOR_3_3_2                       0x00000003
#define     V_038004_COLOR_16                          0x00000005
#define     V_038004_COLOR_16_FLOAT                    0x00000006
#define     V_038004_COLOR_8_8                         0x00000007
#define     V_038004_COLOR_5_6_5                       0x00000008
#define     V_038004_COLOR_6_5_5                       0x00000009
#define     V_038004_COLOR_1_5_5_5                     0x0000000A
#define     V_038004_COLOR_4_4_4_4                     0x0000000B
#define     V_038004_COLOR_5_5_5_1                     0x0000000C
#define     V_038004_COLOR_32                          0x0000000D
#define     V_038004_COLOR_32_FLOAT                    0x0000000E
#define     V_038004_COLOR_16_16                       0x0000000F
#define     V_038004_COLOR_16_16_FLOAT                 0x00000010
#define     V_038004_COLOR_8_24                        0x00000011
#define     V_038004_COLOR_8_24_FLOAT                  0x00000012
#define     V_038004_COLOR_24_8                        0x00000013
#define     V_038004_COLOR_24_8_FLOAT                  0x00000014
#define     V_038004_COLOR_10_11_11                    0x00000015
#define     V_038004_COLOR_10_11_11_FLOAT              0x00000016
#define     V_038004_COLOR_11_11_10                    0x00000017
#define     V_038004_COLOR_11_11_10_FLOAT              0x00000018
#define     V_038004_COLOR_2_10_10_10                  0x00000019
#define     V_038004_COLOR_8_8_8_8                     0x0000001A
#define     V_038004_COLOR_10_10_10_2                  0x0000001B
#define     V_038004_COLOR_X24_8_32_FLOAT              0x0000001C
#define     V_038004_COLOR_32_32                       0x0000001D
#define     V_038004_COLOR_32_32_FLOAT                 0x0000001E
#define     V_038004_COLOR_16_16_16_16                 0x0000001F
#define     V_038004_COLOR_16_16_16_16_FLOAT           0x00000020
#define     V_038004_COLOR_32_32_32_32                 0x00000022
#define     V_038004_COLOR_32_32_32_32_FLOAT           0x00000023
#define     V_038004_FMT_1                             0x00000025
#define     V_038004_FMT_GB_GR                         0x00000027
#define     V_038004_FMT_BG_RG                         0x00000028
#define     V_038004_FMT_32_AS_8                       0x00000029
#define     V_038004_FMT_32_AS_8_8                     0x0000002A
#define     V_038004_FMT_5_9_9_9_SHAREDEXP             0x0000002B
#define     V_038004_FMT_8_8_8                         0x0000002C
#define     V_038004_FMT_16_16_16                      0x0000002D
#define     V_038004_FMT_16_16_16_FLOAT                0x0000002E
#define     V_038004_FMT_32_32_32                      0x0000002F
#define     V_038004_FMT_32_32_32_FLOAT                0x00000030
#define     V_038004_FMT_BC1                           0x00000031
#define     V_038004_FMT_BC2                           0x00000032
#define     V_038004_FMT_BC3                           0x00000033
#define     V_038004_FMT_BC4                           0x00000034
#define     V_038004_FMT_BC5                           0x00000035
#define     V_038004_FMT_BC6                           0x00000036
#define     V_038004_FMT_BC7                           0x00000037
#define     V_038004_FMT_32_AS_32_32_32_32             0x00000038
#define R_038010_SQ_TEX_RESOURCE_WORD4_0             0x038010
#define   S_038010_FORMAT_COMP_X(x)                    (((x) & 0x3) << 0)
#define   G_038010_FORMAT_COMP_X(x)                    (((x) >> 0) & 0x3)
#define   C_038010_FORMAT_COMP_X                       0xFFFFFFFC
#define   S_038010_FORMAT_COMP_Y(x)                    (((x) & 0x3) << 2)
#define   G_038010_FORMAT_COMP_Y(x)                    (((x) >> 2) & 0x3)
#define   C_038010_FORMAT_COMP_Y                       0xFFFFFFF3
#define   S_038010_FORMAT_COMP_Z(x)                    (((x) & 0x3) << 4)
#define   G_038010_FORMAT_COMP_Z(x)                    (((x) >> 4) & 0x3)
#define   C_038010_FORMAT_COMP_Z                       0xFFFFFFCF
#define   S_038010_FORMAT_COMP_W(x)                    (((x) & 0x3) << 6)
#define   G_038010_FORMAT_COMP_W(x)                    (((x) >> 6) & 0x3)
#define   C_038010_FORMAT_COMP_W                       0xFFFFFF3F
#define   S_038010_NUM_FORMAT_ALL(x)                   (((x) & 0x3) << 8)
#define   G_038010_NUM_FORMAT_ALL(x)                   (((x) >> 8) & 0x3)
#define   C_038010_NUM_FORMAT_ALL                      0xFFFFFCFF
#define   S_038010_SRF_MODE_ALL(x)                     (((x) & 0x1) << 10)
#define   G_038010_SRF_MODE_ALL(x)                     (((x) >> 10) & 0x1)
#define   C_038010_SRF_MODE_ALL                        0xFFFFFBFF
#define   S_038010_FORCE_DEGAMMA(x)                    (((x) & 0x1) << 11)
#define   G_038010_FORCE_DEGAMMA(x)                    (((x) >> 11) & 0x1)
#define   C_038010_FORCE_DEGAMMA                       0xFFFFF7FF
#define   S_038010_ENDIAN_SWAP(x)                      (((x) & 0x3) << 12)
#define   G_038010_ENDIAN_SWAP(x)                      (((x) >> 12) & 0x3)
#define   C_038010_ENDIAN_SWAP                         0xFFFFCFFF
#define   S_038010_REQUEST_SIZE(x)                     (((x) & 0x3) << 14)
#define   G_038010_REQUEST_SIZE(x)                     (((x) >> 14) & 0x3)
#define   C_038010_REQUEST_SIZE                        0xFFFF3FFF
#define   S_038010_DST_SEL_X(x)                        (((x) & 0x7) << 16)
#define   G_038010_DST_SEL_X(x)                        (((x) >> 16) & 0x7)
#define   C_038010_DST_SEL_X                           0xFFF8FFFF
#define   S_038010_DST_SEL_Y(x)                        (((x) & 0x7) << 19)
#define   G_038010_DST_SEL_Y(x)                        (((x) >> 19) & 0x7)
#define   C_038010_DST_SEL_Y                           0xFFC7FFFF
#define   S_038010_DST_SEL_Z(x)                        (((x) & 0x7) << 22)
#define   G_038010_DST_SEL_Z(x)                        (((x) >> 22) & 0x7)
#define   C_038010_DST_SEL_Z                           0xFE3FFFFF
#define   S_038010_DST_SEL_W(x)                        (((x) & 0x7) << 25)
#define   G_038010_DST_SEL_W(x)                        (((x) >> 25) & 0x7)
#define   C_038010_DST_SEL_W                           0xF1FFFFFF
#	define SQ_SEL_X					0
#	define SQ_SEL_Y					1
#	define SQ_SEL_Z					2
#	define SQ_SEL_W					3
#	define SQ_SEL_0					4
#	define SQ_SEL_1					5
#define   S_038010_BASE_LEVEL(x)                       (((x) & 0xF) << 28)
#define   G_038010_BASE_LEVEL(x)                       (((x) >> 28) & 0xF)
#define   C_038010_BASE_LEVEL                          0x0FFFFFFF
#define R_038014_SQ_TEX_RESOURCE_WORD5_0             0x038014
#define   S_038014_LAST_LEVEL(x)                       (((x) & 0xF) << 0)
#define   G_038014_LAST_LEVEL(x)                       (((x) >> 0) & 0xF)
#define   C_038014_LAST_LEVEL                          0xFFFFFFF0
#define   S_038014_BASE_ARRAY(x)                       (((x) & 0x1FFF) << 4)
#define   G_038014_BASE_ARRAY(x)                       (((x) >> 4) & 0x1FFF)
#define   C_038014_BASE_ARRAY                          0xFFFE000F
#define   S_038014_LAST_ARRAY(x)                       (((x) & 0x1FFF) << 17)
#define   G_038014_LAST_ARRAY(x)                       (((x) >> 17) & 0x1FFF)
#define   C_038014_LAST_ARRAY                          0xC001FFFF
#define R_0288A8_SQ_ESGS_RING_ITEMSIZE               0x0288A8
#define   S_0288A8_ITEMSIZE(x)                         (((x) & 0x7FFF) << 0)
#define   G_0288A8_ITEMSIZE(x)                         (((x) >> 0) & 0x7FFF)
#define   C_0288A8_ITEMSIZE                            0xFFFF8000
#define R_008C44_SQ_ESGS_RING_SIZE                   0x008C44
#define   S_008C44_MEM_SIZE(x)                         (((x) & 0xFFFFFFFF) << 0)
#define   G_008C44_MEM_SIZE(x)                         (((x) >> 0) & 0xFFFFFFFF)
#define   C_008C44_MEM_SIZE                            0x00000000
#define R_0288B0_SQ_ESTMP_RING_ITEMSIZE              0x0288B0
#define   S_0288B0_ITEMSIZE(x)                         (((x) & 0x7FFF) << 0)
#define   G_0288B0_ITEMSIZE(x)                         (((x) >> 0) & 0x7FFF)
#define   C_0288B0_ITEMSIZE                            0xFFFF8000
#define R_008C54_SQ_ESTMP_RING_SIZE                  0x008C54
#define   S_008C54_MEM_SIZE(x)                         (((x) & 0xFFFFFFFF) << 0)
#define   G_008C54_MEM_SIZE(x)                         (((x) >> 0) & 0xFFFFFFFF)
#define   C_008C54_MEM_SIZE                            0x00000000
#define R_0288C0_SQ_FBUF_RING_ITEMSIZE               0x0288C0
#define   S_0288C0_ITEMSIZE(x)                         (((x) & 0x7FFF) << 0)
#define   G_0288C0_ITEMSIZE(x)                         (((x) >> 0) & 0x7FFF)
#define   C_0288C0_ITEMSIZE                            0xFFFF8000
#define R_008C74_SQ_FBUF_RING_SIZE                   0x008C74
#define   S_008C74_MEM_SIZE(x)                         (((x) & 0xFFFFFFFF) << 0)
#define   G_008C74_MEM_SIZE(x)                         (((x) >> 0) & 0xFFFFFFFF)
#define   C_008C74_MEM_SIZE                            0x00000000
#define R_0288B4_SQ_GSTMP_RING_ITEMSIZE              0x0288B4
#define   S_0288B4_ITEMSIZE(x)                         (((x) & 0x7FFF) << 0)
#define   G_0288B4_ITEMSIZE(x)                         (((x) >> 0) & 0x7FFF)
#define   C_0288B4_ITEMSIZE                            0xFFFF8000
#define R_008C5C_SQ_GSTMP_RING_SIZE                  0x008C5C
#define   S_008C5C_MEM_SIZE(x)                         (((x) & 0xFFFFFFFF) << 0)
#define   G_008C5C_MEM_SIZE(x)                         (((x) >> 0) & 0xFFFFFFFF)
#define   C_008C5C_MEM_SIZE                            0x00000000
#define R_0288AC_SQ_GSVS_RING_ITEMSIZE               0x0288AC
#define   S_0288AC_ITEMSIZE(x)                         (((x) & 0x7FFF) << 0)
#define   G_0288AC_ITEMSIZE(x)                         (((x) >> 0) & 0x7FFF)
#define   C_0288AC_ITEMSIZE                            0xFFFF8000
#define R_008C4C_SQ_GSVS_RING_SIZE                   0x008C4C
#define   S_008C4C_MEM_SIZE(x)                         (((x) & 0xFFFFFFFF) << 0)
#define   G_008C4C_MEM_SIZE(x)                         (((x) >> 0) & 0xFFFFFFFF)
#define   C_008C4C_MEM_SIZE                            0x00000000
#define R_0288BC_SQ_PSTMP_RING_ITEMSIZE              0x0288BC
#define   S_0288BC_ITEMSIZE(x)                         (((x) & 0x7FFF) << 0)
#define   G_0288BC_ITEMSIZE(x)                         (((x) >> 0) & 0x7FFF)
#define   C_0288BC_ITEMSIZE                            0xFFFF8000
#define R_008C6C_SQ_PSTMP_RING_SIZE                  0x008C6C
#define   S_008C6C_MEM_SIZE(x)                         (((x) & 0xFFFFFFFF) << 0)
#define   G_008C6C_MEM_SIZE(x)                         (((x) >> 0) & 0xFFFFFFFF)
#define   C_008C6C_MEM_SIZE                            0x00000000
#define R_0288C4_SQ_REDUC_RING_ITEMSIZE              0x0288C4
#define   S_0288C4_ITEMSIZE(x)                         (((x) & 0x7FFF) << 0)
#define   G_0288C4_ITEMSIZE(x)                         (((x) >> 0) & 0x7FFF)
#define   C_0288C4_ITEMSIZE                            0xFFFF8000
#define R_008C7C_SQ_REDUC_RING_SIZE                  0x008C7C
#define   S_008C7C_MEM_SIZE(x)                         (((x) & 0xFFFFFFFF) << 0)
#define   G_008C7C_MEM_SIZE(x)                         (((x) >> 0) & 0xFFFFFFFF)
#define   C_008C7C_MEM_SIZE                            0x00000000
#define R_0288B8_SQ_VSTMP_RING_ITEMSIZE              0x0288B8
#define   S_0288B8_ITEMSIZE(x)                         (((x) & 0x7FFF) << 0)
#define   G_0288B8_ITEMSIZE(x)                         (((x) >> 0) & 0x7FFF)
#define   C_0288B8_ITEMSIZE                            0xFFFF8000
#define R_008C64_SQ_VSTMP_RING_SIZE                  0x008C64
#define   S_008C64_MEM_SIZE(x)                         (((x) & 0xFFFFFFFF) << 0)
#define   G_008C64_MEM_SIZE(x)                         (((x) >> 0) & 0xFFFFFFFF)
#define   C_008C64_MEM_SIZE                            0x00000000
#define R_0288C8_SQ_GS_VERT_ITEMSIZE                 0x0288C8
#define   S_0288C8_ITEMSIZE(x)                         (((x) & 0x7FFF) << 0)
#define   G_0288C8_ITEMSIZE(x)                         (((x) >> 0) & 0x7FFF)
#define   C_0288C8_ITEMSIZE                            0xFFFF8000
#define R_028010_DB_DEPTH_INFO                       0x028010
#define   S_028010_FORMAT(x)                           (((x) & 0x7) << 0)
#define   G_028010_FORMAT(x)                           (((x) >> 0) & 0x7)
#define   C_028010_FORMAT                              0xFFFFFFF8
#define     V_028010_DEPTH_INVALID                     0x00000000
#define     V_028010_DEPTH_16                          0x00000001
#define     V_028010_DEPTH_X8_24                       0x00000002
#define     V_028010_DEPTH_8_24                        0x00000003
#define     V_028010_DEPTH_X8_24_FLOAT                 0x00000004
#define     V_028010_DEPTH_8_24_FLOAT                  0x00000005
#define     V_028010_DEPTH_32_FLOAT                    0x00000006
#define     V_028010_DEPTH_X24_8_32_FLOAT              0x00000007
#define   S_028010_READ_SIZE(x)                        (((x) & 0x1) << 3)
#define   G_028010_READ_SIZE(x)                        (((x) >> 3) & 0x1)
#define   C_028010_READ_SIZE                           0xFFFFFFF7
#define   S_028010_ARRAY_MODE(x)                       (((x) & 0xF) << 15)
#define   G_028010_ARRAY_MODE(x)                       (((x) >> 15) & 0xF)
#define   C_028010_ARRAY_MODE                          0xFFF87FFF
#define     V_028010_ARRAY_1D_TILED_THIN1              0x00000002
#define     V_028010_ARRAY_2D_TILED_THIN1              0x00000004
#define   S_028010_TILE_SURFACE_ENABLE(x)              (((x) & 0x1) << 25)
#define   G_028010_TILE_SURFACE_ENABLE(x)              (((x) >> 25) & 0x1)
#define   C_028010_TILE_SURFACE_ENABLE                 0xFDFFFFFF
#define   S_028010_TILE_COMPACT(x)                     (((x) & 0x1) << 26)
#define   G_028010_TILE_COMPACT(x)                     (((x) >> 26) & 0x1)
#define   C_028010_TILE_COMPACT                        0xFBFFFFFF
#define   S_028010_ZRANGE_PRECISION(x)                 (((x) & 0x1) << 31)
#define   G_028010_ZRANGE_PRECISION(x)                 (((x) >> 31) & 0x1)
#define   C_028010_ZRANGE_PRECISION                    0x7FFFFFFF
#define R_028000_DB_DEPTH_SIZE                       0x028000
#define   S_028000_PITCH_TILE_MAX(x)                   (((x) & 0x3FF) << 0)
#define   G_028000_PITCH_TILE_MAX(x)                   (((x) >> 0) & 0x3FF)
#define   C_028000_PITCH_TILE_MAX                      0xFFFFFC00
#define   S_028000_SLICE_TILE_MAX(x)                   (((x) & 0xFFFFF) << 10)
#define   G_028000_SLICE_TILE_MAX(x)                   (((x) >> 10) & 0xFFFFF)
#define   C_028000_SLICE_TILE_MAX                      0xC00003FF
#define R_028004_DB_DEPTH_VIEW                       0x028004
#define   S_028004_SLICE_START(x)                      (((x) & 0x7FF) << 0)
#define   G_028004_SLICE_START(x)                      (((x) >> 0) & 0x7FF)
#define   C_028004_SLICE_START                         0xFFFFF800
#define   S_028004_SLICE_MAX(x)                        (((x) & 0x7FF) << 13)
#define   G_028004_SLICE_MAX(x)                        (((x) >> 13) & 0x7FF)
#define   C_028004_SLICE_MAX                           0xFF001FFF
#define R_028800_DB_DEPTH_CONTROL                    0x028800
#define   S_028800_STENCIL_ENABLE(x)                   (((x) & 0x1) << 0)
#define   G_028800_STENCIL_ENABLE(x)                   (((x) >> 0) & 0x1)
#define   C_028800_STENCIL_ENABLE                      0xFFFFFFFE
#define   S_028800_Z_ENABLE(x)                         (((x) & 0x1) << 1)
#define   G_028800_Z_ENABLE(x)                         (((x) >> 1) & 0x1)
#define   C_028800_Z_ENABLE                            0xFFFFFFFD
#define   S_028800_Z_WRITE_ENABLE(x)                   (((x) & 0x1) << 2)
#define   G_028800_Z_WRITE_ENABLE(x)                   (((x) >> 2) & 0x1)
#define   C_028800_Z_WRITE_ENABLE                      0xFFFFFFFB
#define   S_028800_ZFUNC(x)                            (((x) & 0x7) << 4)
#define   G_028800_ZFUNC(x)                            (((x) >> 4) & 0x7)
#define   C_028800_ZFUNC                               0xFFFFFF8F
#define   S_028800_BACKFACE_ENABLE(x)                  (((x) & 0x1) << 7)
#define   G_028800_BACKFACE_ENABLE(x)                  (((x) >> 7) & 0x1)
#define   C_028800_BACKFACE_ENABLE                     0xFFFFFF7F
#define   S_028800_STENCILFUNC(x)                      (((x) & 0x7) << 8)
#define   G_028800_STENCILFUNC(x)                      (((x) >> 8) & 0x7)
#define   C_028800_STENCILFUNC                         0xFFFFF8FF
#define   S_028800_STENCILFAIL(x)                      (((x) & 0x7) << 11)
#define   G_028800_STENCILFAIL(x)                      (((x) >> 11) & 0x7)
#define   C_028800_STENCILFAIL                         0xFFFFC7FF
#define   S_028800_STENCILZPASS(x)                     (((x) & 0x7) << 14)
#define   G_028800_STENCILZPASS(x)                     (((x) >> 14) & 0x7)
#define   C_028800_STENCILZPASS                        0xFFFE3FFF
#define   S_028800_STENCILZFAIL(x)                     (((x) & 0x7) << 17)
#define   G_028800_STENCILZFAIL(x)                     (((x) >> 17) & 0x7)
#define   C_028800_STENCILZFAIL                        0xFFF1FFFF
#define   S_028800_STENCILFUNC_BF(x)                   (((x) & 0x7) << 20)
#define   G_028800_STENCILFUNC_BF(x)                   (((x) >> 20) & 0x7)
#define   C_028800_STENCILFUNC_BF                      0xFF8FFFFF
#define   S_028800_STENCILFAIL_BF(x)                   (((x) & 0x7) << 23)
#define   G_028800_STENCILFAIL_BF(x)                   (((x) >> 23) & 0x7)
#define   C_028800_STENCILFAIL_BF                      0xFC7FFFFF
#define   S_028800_STENCILZPASS_BF(x)                  (((x) & 0x7) << 26)
#define   G_028800_STENCILZPASS_BF(x)                  (((x) >> 26) & 0x7)
#define   C_028800_STENCILZPASS_BF                     0xE3FFFFFF
#define   S_028800_STENCILZFAIL_BF(x)                  (((x) & 0x7) << 29)
#define   G_028800_STENCILZFAIL_BF(x)                  (((x) >> 29) & 0x7)
#define   C_028800_STENCILZFAIL_BF                     0x1FFFFFFF

#endif
