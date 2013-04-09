/*
 * Copyright 2012 Advanced Micro Devices, Inc.
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
#ifndef CIK_H
#define CIK_H

#define BONAIRE_GB_ADDR_CONFIG_GOLDEN        0x12010001

#define CIK_RB_BITMAP_WIDTH_PER_SH  2

#define DMIF_ADDR_CALC  				0xC00

#define	SRBM_STATUS2				        0xE4C
#define	SRBM_STATUS				        0xE50

#define MC_SHARED_CHMAP						0x2004
#define		NOOFCHAN_SHIFT					12
#define		NOOFCHAN_MASK					0x0000f000
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
#define		NOOFGROUPS_SHIFT				12
#define		NOOFGROUPS_MASK					0x00001000

#define	HDP_HOST_PATH_CNTL				0x2C00
#define	HDP_NONSURFACE_BASE				0x2C04
#define	HDP_NONSURFACE_INFO				0x2C08
#define	HDP_NONSURFACE_SIZE				0x2C0C

#define HDP_ADDR_CONFIG  				0x2F48
#define HDP_MISC_CNTL					0x2F4C
#define 	HDP_FLUSH_INVALIDATE_CACHE			(1 << 0)

#define	BIF_FB_EN						0x5490
#define		FB_READ_EN					(1 << 0)
#define		FB_WRITE_EN					(1 << 1)

#define	GRBM_CNTL					0x8000
#define		GRBM_READ_TIMEOUT(x)				((x) << 0)

#define	GRBM_STATUS2					0x8008
#define		ME0PIPE1_CMDFIFO_AVAIL_MASK			0x0000000F
#define		ME0PIPE1_CF_RQ_PENDING				(1 << 4)
#define		ME0PIPE1_PF_RQ_PENDING				(1 << 5)
#define		ME1PIPE0_RQ_PENDING				(1 << 6)
#define		ME1PIPE1_RQ_PENDING				(1 << 7)
#define		ME1PIPE2_RQ_PENDING				(1 << 8)
#define		ME1PIPE3_RQ_PENDING				(1 << 9)
#define		ME2PIPE0_RQ_PENDING				(1 << 10)
#define		ME2PIPE1_RQ_PENDING				(1 << 11)
#define		ME2PIPE2_RQ_PENDING				(1 << 12)
#define		ME2PIPE3_RQ_PENDING				(1 << 13)
#define		RLC_RQ_PENDING 					(1 << 14)
#define		RLC_BUSY 					(1 << 24)
#define		TC_BUSY 					(1 << 25)
#define		CPF_BUSY 					(1 << 28)
#define		CPC_BUSY 					(1 << 29)
#define		CPG_BUSY 					(1 << 30)

#define	GRBM_STATUS					0x8010
#define		ME0PIPE0_CMDFIFO_AVAIL_MASK			0x0000000F
#define		SRBM_RQ_PENDING					(1 << 5)
#define		ME0PIPE0_CF_RQ_PENDING				(1 << 7)
#define		ME0PIPE0_PF_RQ_PENDING				(1 << 8)
#define		GDS_DMA_RQ_PENDING				(1 << 9)
#define		DB_CLEAN					(1 << 12)
#define		CB_CLEAN					(1 << 13)
#define		TA_BUSY 					(1 << 14)
#define		GDS_BUSY 					(1 << 15)
#define		WD_BUSY_NO_DMA 					(1 << 16)
#define		VGT_BUSY					(1 << 17)
#define		IA_BUSY_NO_DMA					(1 << 18)
#define		IA_BUSY						(1 << 19)
#define		SX_BUSY 					(1 << 20)
#define		WD_BUSY 					(1 << 21)
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
#define	GRBM_STATUS_SE2					0x8038
#define	GRBM_STATUS_SE3					0x803C
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
#define		SOFT_RESET_CP					(1 << 0)  /* All CP blocks */
#define		SOFT_RESET_RLC					(1 << 2)  /* RLC */
#define		SOFT_RESET_GFX					(1 << 16) /* GFX */
#define		SOFT_RESET_CPF					(1 << 17) /* CP fetcher shared by gfx and compute */
#define		SOFT_RESET_CPC					(1 << 18) /* CP Compute (MEC1/2) */
#define		SOFT_RESET_CPG					(1 << 19) /* CP GFX (PFP, ME, CE) */

#define CP_MEC_CNTL					0x8234
#define		MEC_ME2_HALT					(1 << 28)
#define		MEC_ME1_HALT					(1 << 30)

#define CP_ME_CNTL					0x86D8
#define		CP_CE_HALT					(1 << 24)
#define		CP_PFP_HALT					(1 << 26)
#define		CP_ME_HALT					(1 << 28)

#define CP_MEQ_THRESHOLDS				0x8764
#define		MEQ1_START(x)				((x) << 0)
#define		MEQ2_START(x)				((x) << 8)

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

#define	VGT_GS_VERTEX_REUSE				0x88D4

#define CC_GC_SHADER_ARRAY_CONFIG			0x89bc
#define		INACTIVE_CUS_MASK			0xFFFF0000
#define		INACTIVE_CUS_SHIFT			16
#define GC_USER_SHADER_ARRAY_CONFIG			0x89c0

#define	PA_CL_ENHANCE					0x8A14
#define		CLIP_VTX_REORDER_ENA				(1 << 0)
#define		NUM_CLIP_SEQ(x)					((x) << 1)

#define	PA_SC_FORCE_EOV_MAX_CNTS			0x8B24
#define		FORCE_EOV_MAX_CLK_CNT(x)			((x) << 0)
#define		FORCE_EOV_MAX_REZ_CNT(x)			((x) << 16)

#define	PA_SC_FIFO_SIZE					0x8BCC
#define		SC_FRONTEND_PRIM_FIFO_SIZE(x)			((x) << 0)
#define		SC_BACKEND_PRIM_FIFO_SIZE(x)			((x) << 6)
#define		SC_HIZ_TILE_FIFO_SIZE(x)			((x) << 15)
#define		SC_EARLYZ_TILE_FIFO_SIZE(x)			((x) << 23)

#define	PA_SC_ENHANCE					0x8BF0
#define		ENABLE_PA_SC_OUT_OF_ORDER			(1 << 0)
#define		DISABLE_PA_SC_GUIDANCE				(1 << 13)

#define	SQ_CONFIG					0x8C00

#define	SX_DEBUG_1					0x9060

#define	SPI_CONFIG_CNTL					0x9100

#define	SPI_CONFIG_CNTL_1				0x913C
#define		VTX_DONE_DELAY(x)				((x) << 0)
#define		INTERP_ONE_PRIM_PER_ROW				(1 << 4)

#define	TA_CNTL_AUX					0x9508

#define DB_DEBUG					0x9830
#define DB_DEBUG2					0x9834
#define DB_DEBUG3					0x9838

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
#define		ROW_SIZE(x)             		((x) << 28)
#define		ROW_SIZE_MASK				0x30000000
#define		ROW_SIZE_SHIFT				28

#define	GB_TILE_MODE0					0x9910
#       define ARRAY_MODE(x)					((x) << 2)
#              define	ARRAY_LINEAR_GENERAL			0
#              define	ARRAY_LINEAR_ALIGNED			1
#              define	ARRAY_1D_TILED_THIN1			2
#              define	ARRAY_2D_TILED_THIN1			4
#              define	ARRAY_PRT_TILED_THIN1			5
#              define	ARRAY_PRT_2D_TILED_THIN1		6
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
#       define MICRO_TILE_MODE_NEW(x)				((x) << 22)
#              define	ADDR_SURF_DISPLAY_MICRO_TILING		0
#              define	ADDR_SURF_THIN_MICRO_TILING		1
#              define	ADDR_SURF_DEPTH_MICRO_TILING		2
#              define	ADDR_SURF_ROTATED_MICRO_TILING		3
#       define SAMPLE_SPLIT(x)					((x) << 25)
#              define	ADDR_SURF_SAMPLE_SPLIT_1		0
#              define	ADDR_SURF_SAMPLE_SPLIT_2		1
#              define	ADDR_SURF_SAMPLE_SPLIT_4		2
#              define	ADDR_SURF_SAMPLE_SPLIT_8		3

#define	GB_MACROTILE_MODE0					0x9990
#       define BANK_WIDTH(x)					((x) << 0)
#              define	ADDR_SURF_BANK_WIDTH_1			0
#              define	ADDR_SURF_BANK_WIDTH_2			1
#              define	ADDR_SURF_BANK_WIDTH_4			2
#              define	ADDR_SURF_BANK_WIDTH_8			3
#       define BANK_HEIGHT(x)					((x) << 2)
#              define	ADDR_SURF_BANK_HEIGHT_1			0
#              define	ADDR_SURF_BANK_HEIGHT_2			1
#              define	ADDR_SURF_BANK_HEIGHT_4			2
#              define	ADDR_SURF_BANK_HEIGHT_8			3
#       define MACRO_TILE_ASPECT(x)				((x) << 4)
#              define	ADDR_SURF_MACRO_ASPECT_1		0
#              define	ADDR_SURF_MACRO_ASPECT_2		1
#              define	ADDR_SURF_MACRO_ASPECT_4		2
#              define	ADDR_SURF_MACRO_ASPECT_8		3
#       define NUM_BANKS(x)					((x) << 6)
#              define	ADDR_SURF_2_BANK			0
#              define	ADDR_SURF_4_BANK			1
#              define	ADDR_SURF_8_BANK			2
#              define	ADDR_SURF_16_BANK			3

#define	CB_HW_CONTROL					0x9A10

#define	GC_USER_RB_BACKEND_DISABLE			0x9B7C
#define		BACKEND_DISABLE_MASK			0x00FF0000
#define		BACKEND_DISABLE_SHIFT			16

#define	TCP_CHAN_STEER_LO				0xac0c
#define	TCP_CHAN_STEER_HI				0xac10

#define PA_SC_RASTER_CONFIG                             0x28350
#       define RASTER_CONFIG_RB_MAP_0                   0
#       define RASTER_CONFIG_RB_MAP_1                   1
#       define RASTER_CONFIG_RB_MAP_2                   2
#       define RASTER_CONFIG_RB_MAP_3                   3

#define GRBM_GFX_INDEX          			0x30800
#define		INSTANCE_INDEX(x)			((x) << 0)
#define		SH_INDEX(x)     			((x) << 8)
#define		SE_INDEX(x)     			((x) << 16)
#define		SH_BROADCAST_WRITES      		(1 << 29)
#define		INSTANCE_BROADCAST_WRITES      		(1 << 30)
#define		SE_BROADCAST_WRITES      		(1 << 31)

#define	VGT_ESGS_RING_SIZE				0x30900
#define	VGT_GSVS_RING_SIZE				0x30904
#define	VGT_PRIMITIVE_TYPE				0x30908
#define	VGT_INDEX_TYPE					0x3090C

#define	VGT_NUM_INDICES					0x30930
#define	VGT_NUM_INSTANCES				0x30934
#define	VGT_TF_RING_SIZE				0x30938
#define	VGT_HS_OFFCHIP_PARAM				0x3093C
#define	VGT_TF_MEMORY_BASE				0x30940

#define	PA_SU_LINE_STIPPLE_VALUE			0x30a00
#define	PA_SC_LINE_STIPPLE_STATE			0x30a04

#define	SQC_CACHES					0x30d20

#define	CP_PERFMON_CNTL					0x36020

#define	CGTS_TCC_DISABLE				0x3c00c
#define	CGTS_USER_TCC_DISABLE				0x3c010
#define		TCC_DISABLE_MASK				0xFFFF0000
#define		TCC_DISABLE_SHIFT				16

#endif
