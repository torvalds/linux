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

#define	CG_MULT_THERMAL_STATUS					0x714
#define		ASIC_MAX_TEMP(x)				((x) << 0)
#define		ASIC_MAX_TEMP_MASK				0x000001ff
#define		ASIC_MAX_TEMP_SHIFT				0
#define		CTF_TEMP(x)					((x) << 9)
#define		CTF_TEMP_MASK					0x0003fe00
#define		CTF_TEMP_SHIFT					9

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

#define DMIF_ADDR_CONFIG  				0xBD4

#define	CC_SYS_RB_BACKEND_DISABLE			0xe80
#define	GC_USER_SYS_RB_BACKEND_DISABLE			0xe84

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

#define HDP_ADDR_CONFIG  				0x2F48
#define HDP_MISC_CNTL					0x2F4C
#define 	HDP_FLUSH_INVALIDATE_CACHE			(1 << 0)

#define	BIF_FB_EN						0x5490
#define		FB_READ_EN					(1 << 0)
#define		FB_WRITE_EN					(1 << 1)

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

#define	GRBM_CNTL					0x8000
#define		GRBM_READ_TIMEOUT(x)				((x) << 0)

#define	CP_QUEUE_THRESHOLDS				0x8760
#define		ROQ_IB1_START(x)				((x) << 0)
#define		ROQ_IB2_START(x)				((x) << 8)
#define CP_MEQ_THRESHOLDS				0x8764
#define		MEQ1_START(x)				((x) << 0)
#define		MEQ2_START(x)				((x) << 8)

#define	CP_PERFMON_CNTL					0x87FC

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

#define	VGT_NUM_INSTANCES				0x8974

#define CC_GC_SHADER_ARRAY_CONFIG			0x89bc
#define GC_USER_SHADER_ARRAY_CONFIG			0x89c0

#define	PA_CL_ENHANCE					0x8A14
#define		CLIP_VTX_REORDER_ENA				(1 << 0)
#define		NUM_CLIP_SEQ(x)					((x) << 1)

#define	PA_SC_LINE_STIPPLE_STATE			0x8B10

#define	PA_SC_FORCE_EOV_MAX_CNTS			0x8B24
#define		FORCE_EOV_MAX_CLK_CNT(x)			((x) << 0)
#define		FORCE_EOV_MAX_REZ_CNT(x)			((x) << 16)

#define	PA_SC_FIFO_SIZE					0x8BCC
#define		SC_FRONTEND_PRIM_FIFO_SIZE(x)			((x) << 0)
#define		SC_BACKEND_PRIM_FIFO_SIZE(x)			((x) << 6)
#define		SC_HIZ_TILE_FIFO_SIZE(x)			((x) << 15)
#define		SC_EARLYZ_TILE_FIFO_SIZE(x)			((x) << 23)

#define	SQ_CONFIG					0x8C00

#define	SX_DEBUG_1					0x9060

#define	SPI_CONFIG_CNTL_1				0x913C
#define		VTX_DONE_DELAY(x)				((x) << 0)
#define		INTERP_ONE_PRIM_PER_ROW				(1 << 4)

#define	CGTS_TCC_DISABLE				0x9148
#define	CGTS_USER_TCC_DISABLE				0x914C
#define		TCC_DISABLE_MASK				0xFFFF0000
#define		TCC_DISABLE_SHIFT				16

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

#define	GC_USER_RB_BACKEND_DISABLE			0x9B7C
#define		BACKEND_DISABLE_MASK			0x00FF0000
#define		BACKEND_DISABLE_SHIFT			16

#define	TCP_CHAN_STEER_LO				0xac0c
#define	TCP_CHAN_STEER_HI				0xac10


#endif
