/*
 * Copyright 2010 Advanced Micro Devices, Inc.
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
#ifndef NI_H
#define NI_H

#define CAYMAN_MAX_SH_GPRS           256
#define CAYMAN_MAX_TEMP_GPRS         16
#define CAYMAN_MAX_SH_THREADS        256
#define CAYMAN_MAX_SH_STACK_ENTRIES  4096
#define CAYMAN_MAX_FRC_EOV_CNT       16384
#define CAYMAN_MAX_BACKENDS          8
#define CAYMAN_MAX_BACKENDS_MASK     0xFF
#define CAYMAN_MAX_BACKENDS_PER_SE_MASK 0xF
#define CAYMAN_MAX_SIMDS             16
#define CAYMAN_MAX_SIMDS_MASK        0xFFFF
#define CAYMAN_MAX_SIMDS_PER_SE_MASK 0xFFF
#define CAYMAN_MAX_PIPES             8
#define CAYMAN_MAX_PIPES_MASK        0xFF
#define CAYMAN_MAX_LDS_NUM           0xFFFF
#define CAYMAN_MAX_TCC               16
#define CAYMAN_MAX_TCC_MASK          0xFF

#define CAYMAN_GB_ADDR_CONFIG_GOLDEN       0x02011003
#define ARUBA_GB_ADDR_CONFIG_GOLDEN        0x12010001

#define DMIF_ADDR_CONFIG  				0xBD4
#define	SRBM_GFX_CNTL				        0x0E44
#define		RINGID(x)					(((x) & 0x3) << 0)
#define		VMID(x)						(((x) & 0x7) << 0)
#define	SRBM_STATUS				        0x0E50

#define VM_CONTEXT0_REQUEST_RESPONSE			0x1470
#define		REQUEST_TYPE(x)					(((x) & 0xf) << 0)
#define		RESPONSE_TYPE_MASK				0x000000F0
#define		RESPONSE_TYPE_SHIFT				4
#define VM_L2_CNTL					0x1400
#define		ENABLE_L2_CACHE					(1 << 0)
#define		ENABLE_L2_FRAGMENT_PROCESSING			(1 << 1)
#define		ENABLE_L2_PTE_CACHE_LRU_UPDATE_BY_WRITE		(1 << 9)
#define		ENABLE_L2_PDE0_CACHE_LRU_UPDATE_BY_WRITE	(1 << 10)
#define		EFFECTIVE_L2_QUEUE_SIZE(x)			(((x) & 7) << 14)
#define		CONTEXT1_IDENTITY_ACCESS_MODE(x)		(((x) & 3) << 18)
/* CONTEXT1_IDENTITY_ACCESS_MODE
 * 0 physical = logical
 * 1 logical via context1 page table
 * 2 inside identity aperture use translation, outside physical = logical
 * 3 inside identity aperture physical = logical, outside use translation
 */
#define VM_L2_CNTL2					0x1404
#define		INVALIDATE_ALL_L1_TLBS				(1 << 0)
#define		INVALIDATE_L2_CACHE				(1 << 1)
#define VM_L2_CNTL3					0x1408
#define		BANK_SELECT(x)					((x) << 0)
#define		CACHE_UPDATE_MODE(x)				((x) << 6)
#define		L2_CACHE_BIGK_ASSOCIATIVITY			(1 << 20)
#define		L2_CACHE_BIGK_FRAGMENT_SIZE(x)			((x) << 15)
#define	VM_L2_STATUS					0x140C
#define		L2_BUSY						(1 << 0)
#define VM_CONTEXT0_CNTL				0x1410
#define		ENABLE_CONTEXT					(1 << 0)
#define		PAGE_TABLE_DEPTH(x)				(((x) & 3) << 1)
#define		RANGE_PROTECTION_FAULT_ENABLE_DEFAULT		(1 << 4)
#define VM_CONTEXT1_CNTL				0x1414
#define VM_CONTEXT0_CNTL2				0x1430
#define VM_CONTEXT1_CNTL2				0x1434
#define VM_INVALIDATE_REQUEST				0x1478
#define VM_INVALIDATE_RESPONSE				0x147c
#define VM_CONTEXT0_PROTECTION_FAULT_DEFAULT_ADDR	0x1518
#define VM_CONTEXT1_PROTECTION_FAULT_DEFAULT_ADDR	0x151c
#define	VM_CONTEXT0_PAGE_TABLE_BASE_ADDR		0x153C
#define	VM_CONTEXT0_PAGE_TABLE_START_ADDR		0x155C
#define	VM_CONTEXT0_PAGE_TABLE_END_ADDR			0x157C

#define MC_SHARED_CHMAP						0x2004
#define		NOOFCHAN_SHIFT					12
#define		NOOFCHAN_MASK					0x00003000
#define MC_SHARED_CHREMAP					0x2008

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
#define	FUS_MC_VM_FB_OFFSET				0x2068

#define MC_SHARED_BLACKOUT_CNTL           		0x20ac
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
#define MC_SEQ_SUP_CNTL           			0x28c8
#define		RUN_MASK      				(1 << 0)
#define MC_SEQ_SUP_PGM           			0x28cc
#define MC_IO_PAD_CNTL_D0           			0x29d0
#define		MEM_FALL_OUT_CMD      			(1 << 8)
#define MC_SEQ_MISC0           				0x2a00
#define		MC_SEQ_MISC0_GDDR5_SHIFT      		28
#define		MC_SEQ_MISC0_GDDR5_MASK      		0xf0000000
#define		MC_SEQ_MISC0_GDDR5_VALUE      		5
#define MC_SEQ_IO_DEBUG_INDEX           		0x2a44
#define MC_SEQ_IO_DEBUG_DATA           			0x2a48

#define	HDP_HOST_PATH_CNTL				0x2C00
#define	HDP_NONSURFACE_BASE				0x2C04
#define	HDP_NONSURFACE_INFO				0x2C08
#define	HDP_NONSURFACE_SIZE				0x2C0C
#define HDP_ADDR_CONFIG  				0x2F48
#define HDP_MISC_CNTL					0x2F4C
#define 	HDP_FLUSH_INVALIDATE_CACHE			(1 << 0)

#define	CC_SYS_RB_BACKEND_DISABLE			0x3F88
#define	GC_USER_SYS_RB_BACKEND_DISABLE			0x3F8C
#define	CGTS_SYS_TCC_DISABLE				0x3F90
#define	CGTS_USER_SYS_TCC_DISABLE			0x3F94

#define RLC_GFX_INDEX           			0x3FC4

#define	CONFIG_MEMSIZE					0x5428

#define HDP_MEM_COHERENCY_FLUSH_CNTL			0x5480
#define HDP_REG_COHERENCY_FLUSH_CNTL			0x54A0

#define	GRBM_CNTL					0x8000
#define		GRBM_READ_TIMEOUT(x)				((x) << 0)
#define	GRBM_STATUS					0x8010
#define		CMDFIFO_AVAIL_MASK				0x0000000F
#define		RING2_RQ_PENDING				(1 << 4)
#define		SRBM_RQ_PENDING					(1 << 5)
#define		RING1_RQ_PENDING				(1 << 6)
#define		CF_RQ_PENDING					(1 << 7)
#define		PF_RQ_PENDING					(1 << 8)
#define		GDS_DMA_RQ_PENDING				(1 << 9)
#define		GRBM_EE_BUSY					(1 << 10)
#define		SX_CLEAN					(1 << 11)
#define		DB_CLEAN					(1 << 12)
#define		CB_CLEAN					(1 << 13)
#define		TA_BUSY 					(1 << 14)
#define		GDS_BUSY 					(1 << 15)
#define		VGT_BUSY_NO_DMA					(1 << 16)
#define		VGT_BUSY					(1 << 17)
#define		IA_BUSY_NO_DMA					(1 << 18)
#define		IA_BUSY						(1 << 19)
#define		SX_BUSY 					(1 << 20)
#define		SH_BUSY 					(1 << 21)
#define		SPI_BUSY					(1 << 22)
#define		SC_BUSY 					(1 << 24)
#define		PA_BUSY 					(1 << 25)
#define		DB_BUSY 					(1 << 26)
#define		CP_COHERENCY_BUSY      				(1 << 28)
#define		CP_BUSY 					(1 << 29)
#define		CB_BUSY 					(1 << 30)
#define		GUI_ACTIVE					(1 << 31)
#define	GRBM_STATUS_SE0					0x8014
#define	GRBM_STATUS_SE1					0x8018
#define		SE_SX_CLEAN					(1 << 0)
#define		SE_DB_CLEAN					(1 << 1)
#define		SE_CB_CLEAN					(1 << 2)
#define		SE_VGT_BUSY					(1 << 23)
#define		SE_PA_BUSY					(1 << 24)
#define		SE_TA_BUSY					(1 << 25)
#define		SE_SX_BUSY					(1 << 26)
#define		SE_SPI_BUSY					(1 << 27)
#define		SE_SH_BUSY					(1 << 28)
#define		SE_SC_BUSY					(1 << 29)
#define		SE_DB_BUSY					(1 << 30)
#define		SE_CB_BUSY					(1 << 31)
#define	GRBM_SOFT_RESET					0x8020
#define		SOFT_RESET_CP					(1 << 0)
#define		SOFT_RESET_CB					(1 << 1)
#define		SOFT_RESET_DB					(1 << 3)
#define		SOFT_RESET_GDS					(1 << 4)
#define		SOFT_RESET_PA					(1 << 5)
#define		SOFT_RESET_SC					(1 << 6)
#define		SOFT_RESET_SPI					(1 << 8)
#define		SOFT_RESET_SH					(1 << 9)
#define		SOFT_RESET_SX					(1 << 10)
#define		SOFT_RESET_TC					(1 << 11)
#define		SOFT_RESET_TA					(1 << 12)
#define		SOFT_RESET_VGT					(1 << 14)
#define		SOFT_RESET_IA					(1 << 15)

#define GRBM_GFX_INDEX          			0x802C
#define		INSTANCE_INDEX(x)			((x) << 0)
#define		SE_INDEX(x)     			((x) << 16)
#define		INSTANCE_BROADCAST_WRITES      		(1 << 30)
#define		SE_BROADCAST_WRITES      		(1 << 31)

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
#define	CP_COHER_CNTL2					0x85E8
#define	CP_STALLED_STAT1			0x8674
#define	CP_STALLED_STAT2			0x8678
#define	CP_BUSY_STAT				0x867C
#define	CP_STAT						0x8680
#define CP_ME_CNTL					0x86D8
#define		CP_ME_HALT					(1 << 28)
#define		CP_PFP_HALT					(1 << 26)
#define	CP_RB2_RPTR					0x86f8
#define	CP_RB1_RPTR					0x86fc
#define	CP_RB0_RPTR					0x8700
#define	CP_RB_WPTR_DELAY				0x8704
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

#define CC_GC_SHADER_PIPE_CONFIG			0x8950
#define	GC_USER_SHADER_PIPE_CONFIG			0x8954
#define		INACTIVE_QD_PIPES(x)				((x) << 8)
#define		INACTIVE_QD_PIPES_MASK				0x0000FF00
#define		INACTIVE_QD_PIPES_SHIFT				8
#define		INACTIVE_SIMDS(x)				((x) << 16)
#define		INACTIVE_SIMDS_MASK				0xFFFF0000
#define		INACTIVE_SIMDS_SHIFT				16

#define VGT_PRIMITIVE_TYPE                              0x8958
#define	VGT_NUM_INSTANCES				0x8974
#define VGT_TF_RING_SIZE				0x8988
#define VGT_OFFCHIP_LDS_BASE				0x89b4

#define	PA_SC_LINE_STIPPLE_STATE			0x8B10
#define	PA_CL_ENHANCE					0x8A14
#define		CLIP_VTX_REORDER_ENA				(1 << 0)
#define		NUM_CLIP_SEQ(x)					((x) << 1)
#define	PA_SC_FIFO_SIZE					0x8BCC
#define		SC_PRIM_FIFO_SIZE(x)				((x) << 0)
#define		SC_HIZ_TILE_FIFO_SIZE(x)			((x) << 12)
#define		SC_EARLYZ_TILE_FIFO_SIZE(x)			((x) << 20)
#define	PA_SC_FORCE_EOV_MAX_CNTS			0x8B24
#define		FORCE_EOV_MAX_CLK_CNT(x)			((x) << 0)
#define		FORCE_EOV_MAX_REZ_CNT(x)			((x) << 16)

#define	SQ_CONFIG					0x8C00
#define		VC_ENABLE					(1 << 0)
#define		EXPORT_SRC_C					(1 << 1)
#define		GFX_PRIO(x)					((x) << 2)
#define		CS1_PRIO(x)					((x) << 4)
#define		CS2_PRIO(x)					((x) << 6)
#define	SQ_GPR_RESOURCE_MGMT_1				0x8C04
#define		NUM_PS_GPRS(x)					((x) << 0)
#define		NUM_VS_GPRS(x)					((x) << 16)
#define		NUM_CLAUSE_TEMP_GPRS(x)				((x) << 28)
#define SQ_ESGS_RING_SIZE				0x8c44
#define SQ_GSVS_RING_SIZE				0x8c4c
#define SQ_ESTMP_RING_BASE				0x8c50
#define SQ_ESTMP_RING_SIZE				0x8c54
#define SQ_GSTMP_RING_BASE				0x8c58
#define SQ_GSTMP_RING_SIZE				0x8c5c
#define SQ_VSTMP_RING_BASE				0x8c60
#define SQ_VSTMP_RING_SIZE				0x8c64
#define SQ_PSTMP_RING_BASE				0x8c68
#define SQ_PSTMP_RING_SIZE				0x8c6c
#define	SQ_MS_FIFO_SIZES				0x8CF0
#define		CACHE_FIFO_SIZE(x)				((x) << 0)
#define		FETCH_FIFO_HIWATER(x)				((x) << 8)
#define		DONE_FIFO_HIWATER(x)				((x) << 16)
#define		ALU_UPDATE_FIFO_HIWATER(x)			((x) << 24)
#define SQ_LSTMP_RING_BASE				0x8e10
#define SQ_LSTMP_RING_SIZE				0x8e14
#define SQ_HSTMP_RING_BASE				0x8e18
#define SQ_HSTMP_RING_SIZE				0x8e1c
#define	SQ_DYN_GPR_CNTL_PS_FLUSH_REQ    		0x8D8C
#define		DYN_GPR_ENABLE					(1 << 8)
#define SQ_CONST_MEM_BASE				0x8df8

#define	SX_EXPORT_BUFFER_SIZES				0x900C
#define		COLOR_BUFFER_SIZE(x)				((x) << 0)
#define		POSITION_BUFFER_SIZE(x)				((x) << 8)
#define		SMX_BUFFER_SIZE(x)				((x) << 16)
#define	SX_DEBUG_1					0x9058
#define		ENABLE_NEW_SMX_ADDRESS				(1 << 16)

#define	SPI_CONFIG_CNTL					0x9100
#define		GPR_WRITE_PRIORITY(x)				((x) << 0)
#define	SPI_CONFIG_CNTL_1				0x913C
#define		VTX_DONE_DELAY(x)				((x) << 0)
#define		INTERP_ONE_PRIM_PER_ROW				(1 << 4)
#define		CRC_SIMD_ID_WADDR_DISABLE			(1 << 8)

#define	CGTS_TCC_DISABLE				0x9148
#define	CGTS_USER_TCC_DISABLE				0x914C
#define		TCC_DISABLE_MASK				0xFFFF0000
#define		TCC_DISABLE_SHIFT				16
#define	CGTS_SM_CTRL_REG				0x9150
#define		OVERRIDE				(1 << 21)

#define	TA_CNTL_AUX					0x9508
#define		DISABLE_CUBE_WRAP				(1 << 0)
#define		DISABLE_CUBE_ANISO				(1 << 1)

#define	TCP_CHAN_STEER_LO				0x960c
#define	TCP_CHAN_STEER_HI				0x9610

#define CC_RB_BACKEND_DISABLE				0x98F4
#define		BACKEND_DISABLE(x)     			((x) << 16)
#define GB_ADDR_CONFIG  				0x98F8
#define		NUM_PIPES(x)				((x) << 0)
#define		NUM_PIPES_MASK				0x00000007
#define		NUM_PIPES_SHIFT				0
#define		PIPE_INTERLEAVE_SIZE(x)			((x) << 4)
#define		PIPE_INTERLEAVE_SIZE_MASK		0x00000070
#define		PIPE_INTERLEAVE_SIZE_SHIFT		4
#define		BANK_INTERLEAVE_SIZE(x)			((x) << 8)
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
#define		NUM_LOWER_PIPES(x)			((x) << 30)
#define		NUM_LOWER_PIPES_MASK			0x40000000
#define		NUM_LOWER_PIPES_SHIFT			30
#define GB_BACKEND_MAP  				0x98FC

#define CB_PERF_CTR0_SEL_0				0x9A20
#define CB_PERF_CTR0_SEL_1				0x9A24
#define CB_PERF_CTR1_SEL_0				0x9A28
#define CB_PERF_CTR1_SEL_1				0x9A2C
#define CB_PERF_CTR2_SEL_0				0x9A30
#define CB_PERF_CTR2_SEL_1				0x9A34
#define CB_PERF_CTR3_SEL_0				0x9A38
#define CB_PERF_CTR3_SEL_1				0x9A3C

#define	GC_USER_RB_BACKEND_DISABLE			0x9B7C
#define		BACKEND_DISABLE_MASK			0x00FF0000
#define		BACKEND_DISABLE_SHIFT			16

#define	SMX_DC_CTL0					0xA020
#define		USE_HASH_FUNCTION				(1 << 0)
#define		NUMBER_OF_SETS(x)				((x) << 1)
#define		FLUSH_ALL_ON_EVENT				(1 << 10)
#define		STALL_ON_EVENT					(1 << 11)
#define	SMX_EVENT_CTL					0xA02C
#define		ES_FLUSH_CTL(x)					((x) << 0)
#define		GS_FLUSH_CTL(x)					((x) << 3)
#define		ACK_FLUSH_CTL(x)				((x) << 6)
#define		SYNC_FLUSH_CTL					(1 << 8)

#define	CP_RB0_BASE					0xC100
#define	CP_RB0_CNTL					0xC104
#define		RB_BUFSZ(x)					((x) << 0)
#define		RB_BLKSZ(x)					((x) << 8)
#define		RB_NO_UPDATE					(1 << 27)
#define		RB_RPTR_WR_ENA					(1 << 31)
#define		BUF_SWAP_32BIT					(2 << 16)
#define	CP_RB0_RPTR_ADDR				0xC10C
#define	CP_RB0_RPTR_ADDR_HI				0xC110
#define	CP_RB0_WPTR					0xC114

#define CP_INT_CNTL                                     0xC124
#       define CNTX_BUSY_INT_ENABLE                     (1 << 19)
#       define CNTX_EMPTY_INT_ENABLE                    (1 << 20)
#       define TIME_STAMP_INT_ENABLE                    (1 << 26)

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
#define	CP_PFP_UCODE_ADDR				0xC150
#define	CP_PFP_UCODE_DATA				0xC154
#define	CP_ME_RAM_RADDR					0xC158
#define	CP_ME_RAM_WADDR					0xC15C
#define	CP_ME_RAM_DATA					0xC160
#define	CP_DEBUG					0xC1FC

#define VGT_EVENT_INITIATOR                             0x28a90
#       define CACHE_FLUSH_AND_INV_EVENT_TS                     (0x14 << 0)
#       define CACHE_FLUSH_AND_INV_EVENT                        (0x16 << 0)

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
#define CP_PACKET2			0x80000000
#define		PACKET2_PAD_SHIFT		0
#define		PACKET2_PAD_MASK		(0x3fffffff << 0)

#define PACKET2(v)	(CP_PACKET2 | REG_SET(PACKET2_PAD, (v)))

#define PACKET3(op, n)	((PACKET_TYPE3 << 30) |				\
			 (((op) & 0xFF) << 8) |				\
			 ((n) & 0x3FFF) << 16)

/* Packet 3 types */
#define	PACKET3_NOP					0x10
#define	PACKET3_SET_BASE				0x11
#define	PACKET3_CLEAR_STATE				0x12
#define	PACKET3_INDEX_BUFFER_SIZE			0x13
#define	PACKET3_DEALLOC_STATE				0x14
#define	PACKET3_DISPATCH_DIRECT				0x15
#define	PACKET3_DISPATCH_INDIRECT			0x16
#define	PACKET3_INDIRECT_BUFFER_END			0x17
#define	PACKET3_MODE_CONTROL				0x18
#define	PACKET3_SET_PREDICATION				0x20
#define	PACKET3_REG_RMW					0x21
#define	PACKET3_COND_EXEC				0x22
#define	PACKET3_PRED_EXEC				0x23
#define	PACKET3_DRAW_INDIRECT				0x24
#define	PACKET3_DRAW_INDEX_INDIRECT			0x25
#define	PACKET3_INDEX_BASE				0x26
#define	PACKET3_DRAW_INDEX_2				0x27
#define	PACKET3_CONTEXT_CONTROL				0x28
#define	PACKET3_DRAW_INDEX_OFFSET			0x29
#define	PACKET3_INDEX_TYPE				0x2A
#define	PACKET3_DRAW_INDEX				0x2B
#define	PACKET3_DRAW_INDEX_AUTO				0x2D
#define	PACKET3_DRAW_INDEX_IMMD				0x2E
#define	PACKET3_NUM_INSTANCES				0x2F
#define	PACKET3_DRAW_INDEX_MULTI_AUTO			0x30
#define	PACKET3_INDIRECT_BUFFER				0x32
#define	PACKET3_STRMOUT_BUFFER_UPDATE			0x34
#define	PACKET3_DRAW_INDEX_OFFSET_2			0x35
#define	PACKET3_DRAW_INDEX_MULTI_ELEMENT		0x36
#define	PACKET3_WRITE_DATA				0x37
#define	PACKET3_MEM_SEMAPHORE				0x39
#define	PACKET3_MPEG_INDEX				0x3A
#define	PACKET3_WAIT_REG_MEM				0x3C
#define	PACKET3_MEM_WRITE				0x3D
#define	PACKET3_SURFACE_SYNC				0x43
#              define PACKET3_CB0_DEST_BASE_ENA    (1 << 6)
#              define PACKET3_CB1_DEST_BASE_ENA    (1 << 7)
#              define PACKET3_CB2_DEST_BASE_ENA    (1 << 8)
#              define PACKET3_CB3_DEST_BASE_ENA    (1 << 9)
#              define PACKET3_CB4_DEST_BASE_ENA    (1 << 10)
#              define PACKET3_CB5_DEST_BASE_ENA    (1 << 11)
#              define PACKET3_CB6_DEST_BASE_ENA    (1 << 12)
#              define PACKET3_CB7_DEST_BASE_ENA    (1 << 13)
#              define PACKET3_DB_DEST_BASE_ENA     (1 << 14)
#              define PACKET3_CB8_DEST_BASE_ENA    (1 << 15)
#              define PACKET3_CB9_DEST_BASE_ENA    (1 << 16)
#              define PACKET3_CB10_DEST_BASE_ENA   (1 << 17)
#              define PACKET3_CB11_DEST_BASE_ENA   (1 << 18)
#              define PACKET3_FULL_CACHE_ENA       (1 << 20)
#              define PACKET3_TC_ACTION_ENA        (1 << 23)
#              define PACKET3_CB_ACTION_ENA        (1 << 25)
#              define PACKET3_DB_ACTION_ENA        (1 << 26)
#              define PACKET3_SH_ACTION_ENA        (1 << 27)
#              define PACKET3_SX_ACTION_ENA        (1 << 28)
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
#define	PACKET3_EVENT_WRITE_EOS				0x48
#define	PACKET3_PREAMBLE_CNTL				0x4A
#              define PACKET3_PREAMBLE_BEGIN_CLEAR_STATE     (2 << 28)
#              define PACKET3_PREAMBLE_END_CLEAR_STATE       (3 << 28)
#define	PACKET3_ALU_PS_CONST_BUFFER_COPY		0x4C
#define	PACKET3_ALU_VS_CONST_BUFFER_COPY		0x4D
#define	PACKET3_ALU_PS_CONST_UPDATE		        0x4E
#define	PACKET3_ALU_VS_CONST_UPDATE		        0x4F
#define	PACKET3_ONE_REG_WRITE				0x57
#define	PACKET3_SET_CONFIG_REG				0x68
#define		PACKET3_SET_CONFIG_REG_START			0x00008000
#define		PACKET3_SET_CONFIG_REG_END			0x0000ac00
#define	PACKET3_SET_CONTEXT_REG				0x69
#define		PACKET3_SET_CONTEXT_REG_START			0x00028000
#define		PACKET3_SET_CONTEXT_REG_END			0x00029000
#define	PACKET3_SET_ALU_CONST				0x6A
/* alu const buffers only; no reg file */
#define	PACKET3_SET_BOOL_CONST				0x6B
#define		PACKET3_SET_BOOL_CONST_START			0x0003a500
#define		PACKET3_SET_BOOL_CONST_END			0x0003a518
#define	PACKET3_SET_LOOP_CONST				0x6C
#define		PACKET3_SET_LOOP_CONST_START			0x0003a200
#define		PACKET3_SET_LOOP_CONST_END			0x0003a500
#define	PACKET3_SET_RESOURCE				0x6D
#define		PACKET3_SET_RESOURCE_START			0x00030000
#define		PACKET3_SET_RESOURCE_END			0x00038000
#define	PACKET3_SET_SAMPLER				0x6E
#define		PACKET3_SET_SAMPLER_START			0x0003c000
#define		PACKET3_SET_SAMPLER_END				0x0003c600
#define	PACKET3_SET_CTL_CONST				0x6F
#define		PACKET3_SET_CTL_CONST_START			0x0003cff0
#define		PACKET3_SET_CTL_CONST_END			0x0003ff0c
#define	PACKET3_SET_RESOURCE_OFFSET			0x70
#define	PACKET3_SET_ALU_CONST_VS			0x71
#define	PACKET3_SET_ALU_CONST_DI			0x72
#define	PACKET3_SET_CONTEXT_REG_INDIRECT		0x73
#define	PACKET3_SET_RESOURCE_INDIRECT			0x74
#define	PACKET3_SET_APPEND_CNT			        0x75
#define	PACKET3_ME_WRITE				0x7A

#endif

