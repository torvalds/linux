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
#define CB_COLOR0_INFO                                  0x280a0
#define CB_COLOR0_TILE                                  0x280c0
#define CB_COLOR0_FRAG                                  0x280e0
#define CB_COLOR0_MASK                                  0x28100

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
#define		RB_BUFSZ(x)					((x)<<0)
#define		RB_BLKSZ(x)					((x)<<8)
#define		RB_NO_UPDATE					(1<<27)
#define		RB_RPTR_WR_ENA					(1<<31)
#define		BUF_SWAP_32BIT					(2 << 16)
#define	CP_RB_RPTR					0x8700
#define	CP_RB_RPTR_ADDR					0xC10C
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

#define GRBM_CNTL                                       0x8000
#       define GRBM_READ_TIMEOUT(x)                     ((x) << 0)
#define	GRBM_STATUS					0x8010
#define		CMDFIFO_AVAIL_MASK				0x0000001F
#define		GUI_ACTIVE					(1<<31)
#define	GRBM_STATUS2					0x8014
#define	GRBM_SOFT_RESET					0x8020
#define		SOFT_RESET_CP					(1<<0)

#define	HDP_HOST_PATH_CNTL				0x2C00
#define	HDP_NONSURFACE_BASE				0x2C04
#define	HDP_NONSURFACE_INFO				0x2C08
#define	HDP_NONSURFACE_SIZE				0x2C0C
#define HDP_REG_COHERENCY_FLUSH_CNTL			0x54A0
#define	HDP_TILING_CONFIG				0x2F3C

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
#define	SQ_VTX_CONSTANT_WORD6_0				0x38018
#define		S__SQ_VTX_CONSTANT_TYPE(x)			(((x) & 3) << 30)
#define		G__SQ_VTX_CONSTANT_TYPE(x)			(((x) >> 30) & 3)
#define			SQ_TEX_VTX_INVALID_TEXTURE			0x0
#define			SQ_TEX_VTX_INVALID_BUFFER			0x1
#define			SQ_TEX_VTX_VALID_TEXTURE			0x2
#define			SQ_TEX_VTX_VALID_BUFFER				0x3


#define	SX_MISC						0x28350
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
#define	VGT_STRMOUT_EN					0x28AB0
#define	VGT_VERTEX_REUSE_BLOCK_CNTL			0x28C58
#define		VTX_REUSE_DEPTH_MASK				0x000000FF
#define VGT_EVENT_INITIATOR                             0x28a90
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
#define	PACKET3_MPEG_INDEX				0x3A
#define	PACKET3_WAIT_REG_MEM				0x3C
#define	PACKET3_MEM_WRITE				0x3D
#define	PACKET3_INDIRECT_BUFFER				0x32
#define	PACKET3_CP_INTERRUPT				0x40
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
#define	PACKET3_EVENT_WRITE_EOP				0x47
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

#endif
