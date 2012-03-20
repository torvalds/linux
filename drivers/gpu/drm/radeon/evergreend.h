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
#ifndef EVERGREEND_H
#define EVERGREEND_H

#define EVERGREEN_MAX_SH_GPRS           256
#define EVERGREEN_MAX_TEMP_GPRS         16
#define EVERGREEN_MAX_SH_THREADS        256
#define EVERGREEN_MAX_SH_STACK_ENTRIES  4096
#define EVERGREEN_MAX_FRC_EOV_CNT       16384
#define EVERGREEN_MAX_BACKENDS          8
#define EVERGREEN_MAX_BACKENDS_MASK     0xFF
#define EVERGREEN_MAX_SIMDS             16
#define EVERGREEN_MAX_SIMDS_MASK        0xFFFF
#define EVERGREEN_MAX_PIPES             8
#define EVERGREEN_MAX_PIPES_MASK        0xFF
#define EVERGREEN_MAX_LDS_NUM           0xFFFF

/* Registers */

#define RCU_IND_INDEX           			0x100
#define RCU_IND_DATA            			0x104

#define GRBM_GFX_INDEX          			0x802C
#define		INSTANCE_INDEX(x)			((x) << 0)
#define		SE_INDEX(x)     			((x) << 16)
#define		INSTANCE_BROADCAST_WRITES      		(1 << 30)
#define		SE_BROADCAST_WRITES      		(1 << 31)
#define RLC_GFX_INDEX           			0x3fC4
#define CC_GC_SHADER_PIPE_CONFIG			0x8950
#define		WRITE_DIS      				(1 << 0)
#define CC_RB_BACKEND_DISABLE				0x98F4
#define		BACKEND_DISABLE(x)     			((x) << 16)
#define GB_ADDR_CONFIG  				0x98F8
#define		NUM_PIPES(x)				((x) << 0)
#define		PIPE_INTERLEAVE_SIZE(x)			((x) << 4)
#define		BANK_INTERLEAVE_SIZE(x)			((x) << 8)
#define		NUM_SHADER_ENGINES(x)			((x) << 12)
#define		SHADER_ENGINE_TILE_SIZE(x)     		((x) << 16)
#define		NUM_GPUS(x)     			((x) << 20)
#define		MULTI_GPU_TILE_SIZE(x)     		((x) << 24)
#define		ROW_SIZE(x)             		((x) << 28)
#define GB_BACKEND_MAP  				0x98FC
#define DMIF_ADDR_CONFIG  				0xBD4
#define HDP_ADDR_CONFIG  				0x2F48
#define HDP_MISC_CNTL  					0x2F4C
#define		HDP_FLUSH_INVALIDATE_CACHE      	(1 << 0)

#define	CC_SYS_RB_BACKEND_DISABLE			0x3F88
#define	GC_USER_RB_BACKEND_DISABLE			0x9B7C

#define	CGTS_SYS_TCC_DISABLE				0x3F90
#define	CGTS_TCC_DISABLE				0x9148
#define	CGTS_USER_SYS_TCC_DISABLE			0x3F94
#define	CGTS_USER_TCC_DISABLE				0x914C

#define	CONFIG_MEMSIZE					0x5428

#define CP_ME_CNTL					0x86D8
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
#define	CP_SEM_WAIT_TIMER				0x85BC
#define	CP_SEM_INCOMPLETE_TIMER_CNTL			0x85C8
#define	CP_DEBUG					0xC1FC


#define	GC_USER_SHADER_PIPE_CONFIG			0x8954
#define		INACTIVE_QD_PIPES(x)				((x) << 8)
#define		INACTIVE_QD_PIPES_MASK				0x0000FF00
#define		INACTIVE_SIMDS(x)				((x) << 16)
#define		INACTIVE_SIMDS_MASK				0x00FF0000

#define	GRBM_CNTL					0x8000
#define		GRBM_READ_TIMEOUT(x)				((x) << 0)
#define	GRBM_SOFT_RESET					0x8020
#define		SOFT_RESET_CP					(1 << 0)
#define		SOFT_RESET_CB					(1 << 1)
#define		SOFT_RESET_DB					(1 << 3)
#define		SOFT_RESET_PA					(1 << 5)
#define		SOFT_RESET_SC					(1 << 6)
#define		SOFT_RESET_SPI					(1 << 8)
#define		SOFT_RESET_SH					(1 << 9)
#define		SOFT_RESET_SX					(1 << 10)
#define		SOFT_RESET_TC					(1 << 11)
#define		SOFT_RESET_TA					(1 << 12)
#define		SOFT_RESET_VC					(1 << 13)
#define		SOFT_RESET_VGT					(1 << 14)

#define	GRBM_STATUS					0x8010
#define		CMDFIFO_AVAIL_MASK				0x0000000F
#define		SRBM_RQ_PENDING					(1 << 5)
#define		CF_RQ_PENDING					(1 << 7)
#define		PF_RQ_PENDING					(1 << 8)
#define		GRBM_EE_BUSY					(1 << 10)
#define		SX_CLEAN					(1 << 11)
#define		DB_CLEAN					(1 << 12)
#define		CB_CLEAN					(1 << 13)
#define		TA_BUSY 					(1 << 14)
#define		VGT_BUSY_NO_DMA					(1 << 16)
#define		VGT_BUSY					(1 << 17)
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
#define		SE_TA_BUSY					(1 << 25)
#define		SE_SX_BUSY					(1 << 26)
#define		SE_SPI_BUSY					(1 << 27)
#define		SE_SH_BUSY					(1 << 28)
#define		SE_SC_BUSY					(1 << 29)
#define		SE_DB_BUSY					(1 << 30)
#define		SE_CB_BUSY					(1 << 31)
/* evergreen */
#define	CG_THERMAL_CTRL					0x72c
#define		TOFFSET_MASK			        0x00003FE0
#define		TOFFSET_SHIFT			        5
#define	CG_MULT_THERMAL_STATUS				0x740
#define		ASIC_T(x)			        ((x) << 16)
#define		ASIC_T_MASK			        0x07FF0000
#define		ASIC_T_SHIFT			        16
#define	CG_TS0_STATUS					0x760
#define		TS0_ADC_DOUT_MASK			0x000003FF
#define		TS0_ADC_DOUT_SHIFT			0
/* APU */
#define	CG_THERMAL_STATUS			        0x678

#define	HDP_HOST_PATH_CNTL				0x2C00
#define	HDP_NONSURFACE_BASE				0x2C04
#define	HDP_NONSURFACE_INFO				0x2C08
#define	HDP_NONSURFACE_SIZE				0x2C0C
#define HDP_MEM_COHERENCY_FLUSH_CNTL			0x5480
#define HDP_REG_COHERENCY_FLUSH_CNTL			0x54A0
#define	HDP_TILING_CONFIG				0x2F3C

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
#define	FUS_MC_ARB_RAMCFG				0x2768
#define	MC_VM_AGP_TOP					0x2028
#define	MC_VM_AGP_BOT					0x202C
#define	MC_VM_AGP_BASE					0x2030
#define	MC_VM_FB_LOCATION				0x2024
#define	MC_FUS_VM_FB_OFFSET				0x2898
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

#define	FUS_MC_VM_MD_L1_TLB0_CNTL			0x265C
#define	FUS_MC_VM_MD_L1_TLB1_CNTL			0x2660
#define	FUS_MC_VM_MD_L1_TLB2_CNTL			0x2664

#define	MC_VM_SYSTEM_APERTURE_DEFAULT_ADDR		0x203C
#define	MC_VM_SYSTEM_APERTURE_HIGH_ADDR			0x2038
#define	MC_VM_SYSTEM_APERTURE_LOW_ADDR			0x2034

#define	PA_CL_ENHANCE					0x8A14
#define		CLIP_VTX_REORDER_ENA				(1 << 0)
#define		NUM_CLIP_SEQ(x)					((x) << 1)
#define	PA_SC_ENHANCE					0x8BF0
#define PA_SC_AA_CONFIG					0x28C04
#define         MSAA_NUM_SAMPLES_SHIFT                  0
#define         MSAA_NUM_SAMPLES_MASK                   0x3
#define PA_SC_CLIPRECT_RULE				0x2820C
#define	PA_SC_EDGERULE					0x28230
#define	PA_SC_FIFO_SIZE					0x8BCC
#define		SC_PRIM_FIFO_SIZE(x)				((x) << 0)
#define		SC_HIZ_TILE_FIFO_SIZE(x)			((x) << 12)
#define		SC_EARLYZ_TILE_FIFO_SIZE(x)			((x) << 20)
#define	PA_SC_FORCE_EOV_MAX_CNTS			0x8B24
#define		FORCE_EOV_MAX_CLK_CNT(x)			((x) << 0)
#define		FORCE_EOV_MAX_REZ_CNT(x)			((x) << 16)
#define PA_SC_LINE_STIPPLE				0x28A0C
#define	PA_SU_LINE_STIPPLE_VALUE			0x8A60
#define	PA_SC_LINE_STIPPLE_STATE			0x8B10

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

#define	SPI_CONFIG_CNTL					0x9100
#define		GPR_WRITE_PRIORITY(x)				((x) << 0)
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
#define		CS_PRIO(x)					((x) << 18)
#define		LS_PRIO(x)					((x) << 20)
#define		HS_PRIO(x)					((x) << 22)
#define		PS_PRIO(x)					((x) << 24)
#define		VS_PRIO(x)					((x) << 26)
#define		GS_PRIO(x)					((x) << 28)
#define		ES_PRIO(x)					((x) << 30)
#define	SQ_GPR_RESOURCE_MGMT_1				0x8C04
#define		NUM_PS_GPRS(x)					((x) << 0)
#define		NUM_VS_GPRS(x)					((x) << 16)
#define		NUM_CLAUSE_TEMP_GPRS(x)				((x) << 28)
#define	SQ_GPR_RESOURCE_MGMT_2				0x8C08
#define		NUM_GS_GPRS(x)					((x) << 0)
#define		NUM_ES_GPRS(x)					((x) << 16)
#define	SQ_GPR_RESOURCE_MGMT_3				0x8C0C
#define		NUM_HS_GPRS(x)					((x) << 0)
#define		NUM_LS_GPRS(x)					((x) << 16)
#define	SQ_GLOBAL_GPR_RESOURCE_MGMT_1			0x8C10
#define	SQ_GLOBAL_GPR_RESOURCE_MGMT_2			0x8C14
#define	SQ_THREAD_RESOURCE_MGMT				0x8C18
#define		NUM_PS_THREADS(x)				((x) << 0)
#define		NUM_VS_THREADS(x)				((x) << 8)
#define		NUM_GS_THREADS(x)				((x) << 16)
#define		NUM_ES_THREADS(x)				((x) << 24)
#define	SQ_THREAD_RESOURCE_MGMT_2			0x8C1C
#define		NUM_HS_THREADS(x)				((x) << 0)
#define		NUM_LS_THREADS(x)				((x) << 8)
#define	SQ_STACK_RESOURCE_MGMT_1			0x8C20
#define		NUM_PS_STACK_ENTRIES(x)				((x) << 0)
#define		NUM_VS_STACK_ENTRIES(x)				((x) << 16)
#define	SQ_STACK_RESOURCE_MGMT_2			0x8C24
#define		NUM_GS_STACK_ENTRIES(x)				((x) << 0)
#define		NUM_ES_STACK_ENTRIES(x)				((x) << 16)
#define	SQ_STACK_RESOURCE_MGMT_3			0x8C28
#define		NUM_HS_STACK_ENTRIES(x)				((x) << 0)
#define		NUM_LS_STACK_ENTRIES(x)				((x) << 16)
#define	SQ_DYN_GPR_CNTL_PS_FLUSH_REQ    		0x8D8C
#define	SQ_DYN_GPR_SIMD_LOCK_EN    			0x8D94
#define	SQ_STATIC_THREAD_MGMT_1    			0x8E20
#define	SQ_STATIC_THREAD_MGMT_2    			0x8E24
#define	SQ_STATIC_THREAD_MGMT_3    			0x8E28
#define	SQ_LDS_RESOURCE_MGMT    			0x8E2C

#define	SQ_MS_FIFO_SIZES				0x8CF0
#define		CACHE_FIFO_SIZE(x)				((x) << 0)
#define		FETCH_FIFO_HIWATER(x)				((x) << 8)
#define		DONE_FIFO_HIWATER(x)				((x) << 16)
#define		ALU_UPDATE_FIFO_HIWATER(x)			((x) << 24)

#define	SX_DEBUG_1					0x9058
#define		ENABLE_NEW_SMX_ADDRESS				(1 << 16)
#define	SX_EXPORT_BUFFER_SIZES				0x900C
#define		COLOR_BUFFER_SIZE(x)				((x) << 0)
#define		POSITION_BUFFER_SIZE(x)				((x) << 8)
#define		SMX_BUFFER_SIZE(x)				((x) << 16)
#define	SX_MEMORY_EXPORT_BASE				0x9010
#define	SX_MISC						0x28350

#define CB_PERF_CTR0_SEL_0				0x9A20
#define CB_PERF_CTR0_SEL_1				0x9A24
#define CB_PERF_CTR1_SEL_0				0x9A28
#define CB_PERF_CTR1_SEL_1				0x9A2C
#define CB_PERF_CTR2_SEL_0				0x9A30
#define CB_PERF_CTR2_SEL_1				0x9A34
#define CB_PERF_CTR3_SEL_0				0x9A38
#define CB_PERF_CTR3_SEL_1				0x9A3C

#define	TA_CNTL_AUX					0x9508
#define		DISABLE_CUBE_WRAP				(1 << 0)
#define		DISABLE_CUBE_ANISO				(1 << 1)
#define		SYNC_GRADIENT					(1 << 24)
#define		SYNC_WALKER					(1 << 25)
#define		SYNC_ALIGNER					(1 << 26)

#define	TCP_CHAN_STEER_LO				0x960c
#define	TCP_CHAN_STEER_HI				0x9610

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
#define	VGT_OUT_DEALLOC_CNTL				0x28C5C
#define		DEALLOC_DIST_MASK				0x0000007F
#define	VGT_VERTEX_REUSE_BLOCK_CNTL			0x28C58
#define		VTX_REUSE_DEPTH_MASK				0x000000FF

#define VM_CONTEXT0_CNTL				0x1410
#define		ENABLE_CONTEXT					(1 << 0)
#define		PAGE_TABLE_DEPTH(x)				(((x) & 3) << 1)
#define		RANGE_PROTECTION_FAULT_ENABLE_DEFAULT		(1 << 4)
#define VM_CONTEXT1_CNTL				0x1414
#define	VM_CONTEXT0_PAGE_TABLE_BASE_ADDR		0x153C
#define	VM_CONTEXT0_PAGE_TABLE_END_ADDR			0x157C
#define	VM_CONTEXT0_PAGE_TABLE_START_ADDR		0x155C
#define VM_CONTEXT0_PROTECTION_FAULT_DEFAULT_ADDR	0x1518
#define VM_CONTEXT0_REQUEST_RESPONSE			0x1470
#define		REQUEST_TYPE(x)					(((x) & 0xf) << 0)
#define		RESPONSE_TYPE_MASK				0x000000F0
#define		RESPONSE_TYPE_SHIFT				4
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

#define	SRBM_STATUS				        0x0E50
#define	SRBM_SOFT_RESET				        0x0E60
#define		SRBM_SOFT_RESET_ALL_MASK    	       	0x00FEEFA6
#define		SOFT_RESET_BIF				(1 << 1)
#define		SOFT_RESET_CG				(1 << 2)
#define		SOFT_RESET_DC				(1 << 5)
#define		SOFT_RESET_GRBM				(1 << 8)
#define		SOFT_RESET_HDP				(1 << 9)
#define		SOFT_RESET_IH				(1 << 10)
#define		SOFT_RESET_MC				(1 << 11)
#define		SOFT_RESET_RLC				(1 << 13)
#define		SOFT_RESET_ROM				(1 << 14)
#define		SOFT_RESET_SEM				(1 << 15)
#define		SOFT_RESET_VMC				(1 << 17)
#define		SOFT_RESET_TST				(1 << 21)
#define		SOFT_RESET_REGBB		       	(1 << 22)
#define		SOFT_RESET_ORB				(1 << 23)

/* display watermarks */
#define	DC_LB_MEMORY_SPLIT				  0x6b0c
#define	PRIORITY_A_CNT			                  0x6b18
#define		PRIORITY_MARK_MASK			  0x7fff
#define		PRIORITY_OFF				  (1 << 16)
#define		PRIORITY_ALWAYS_ON			  (1 << 20)
#define	PRIORITY_B_CNT			                  0x6b1c
#define	PIPE0_ARBITRATION_CONTROL3			  0x0bf0
#       define LATENCY_WATERMARK_MASK(x)                  ((x) << 16)
#define	PIPE0_LATENCY_CONTROL			          0x0bf4
#       define LATENCY_LOW_WATERMARK(x)                   ((x) << 0)
#       define LATENCY_HIGH_WATERMARK(x)                  ((x) << 16)

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

#define CP_INT_CNTL                                     0xc124
#       define CNTX_BUSY_INT_ENABLE                     (1 << 19)
#       define CNTX_EMPTY_INT_ENABLE                    (1 << 20)
#       define SCRATCH_INT_ENABLE                       (1 << 25)
#       define TIME_STAMP_INT_ENABLE                    (1 << 26)
#       define IB2_INT_ENABLE                           (1 << 29)
#       define IB1_INT_ENABLE                           (1 << 30)
#       define RB_INT_ENABLE                            (1 << 31)
#define CP_INT_STATUS                                   0xc128
#       define SCRATCH_INT_STAT                         (1 << 25)
#       define TIME_STAMP_INT_STAT                      (1 << 26)
#       define IB2_INT_STAT                             (1 << 29)
#       define IB1_INT_STAT                             (1 << 30)
#       define RB_INT_STAT                              (1 << 31)

#define GRBM_INT_CNTL                                   0x8060
#       define RDERR_INT_ENABLE                         (1 << 0)
#       define GUI_IDLE_INT_ENABLE                      (1 << 19)

/* 0x6e98, 0x7a98, 0x10698, 0x11298, 0x11e98, 0x12a98 */
#define CRTC_STATUS_FRAME_COUNT                         0x6e98

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

#define	DACA_AUTODETECT_INT_CONTROL			0x66c8
#define	DACB_AUTODETECT_INT_CONTROL			0x67c8

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

/* PCIE link stuff */
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
#define	PACKET3_STRMOUT_BUFFER_UPDATE			0x34
#define	PACKET3_DRAW_INDEX_OFFSET_2			0x35
#define	PACKET3_DRAW_INDEX_MULTI_ELEMENT		0x36
#define	PACKET3_MEM_SEMAPHORE				0x39
#define	PACKET3_MPEG_INDEX				0x3A
#define	PACKET3_COPY_DW					0x3B
#define	PACKET3_WAIT_REG_MEM				0x3C
#define	PACKET3_MEM_WRITE				0x3D
#define	PACKET3_INDIRECT_BUFFER				0x32
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
#              define PACKET3_VC_ACTION_ENA        (1 << 24)
#              define PACKET3_CB_ACTION_ENA        (1 << 25)
#              define PACKET3_DB_ACTION_ENA        (1 << 26)
#              define PACKET3_SH_ACTION_ENA        (1 << 27)
#              define PACKET3_SX_ACTION_ENA        (1 << 28)
#define	PACKET3_ME_INITIALIZE				0x44
#define		PACKET3_ME_INITIALIZE_DEVICE_ID(x) ((x) << 16)
#define	PACKET3_COND_WRITE				0x45
#define	PACKET3_EVENT_WRITE				0x46
#define	PACKET3_EVENT_WRITE_EOP				0x47
#define	PACKET3_EVENT_WRITE_EOS				0x48
#define	PACKET3_PREAMBLE_CNTL				0x4A
#              define PACKET3_PREAMBLE_BEGIN_CLEAR_STATE     (2 << 28)
#              define PACKET3_PREAMBLE_END_CLEAR_STATE       (3 << 28)
#define	PACKET3_RB_OFFSET				0x4B
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

#define	SQ_RESOURCE_CONSTANT_WORD7_0				0x3001c
#define		S__SQ_CONSTANT_TYPE(x)			(((x) & 3) << 30)
#define		G__SQ_CONSTANT_TYPE(x)			(((x) >> 30) & 3)
#define			SQ_TEX_VTX_INVALID_TEXTURE			0x0
#define			SQ_TEX_VTX_INVALID_BUFFER			0x1
#define			SQ_TEX_VTX_VALID_TEXTURE			0x2
#define			SQ_TEX_VTX_VALID_BUFFER				0x3

#define VGT_VTX_VECT_EJECT_REG				0x88b0

#define SQ_CONST_MEM_BASE				0x8df8

#define SQ_ESGS_RING_BASE				0x8c40
#define SQ_ESGS_RING_SIZE				0x8c44
#define SQ_GSVS_RING_BASE				0x8c48
#define SQ_GSVS_RING_SIZE				0x8c4c
#define SQ_ESTMP_RING_BASE				0x8c50
#define SQ_ESTMP_RING_SIZE				0x8c54
#define SQ_GSTMP_RING_BASE				0x8c58
#define SQ_GSTMP_RING_SIZE				0x8c5c
#define SQ_VSTMP_RING_BASE				0x8c60
#define SQ_VSTMP_RING_SIZE				0x8c64
#define SQ_PSTMP_RING_BASE				0x8c68
#define SQ_PSTMP_RING_SIZE				0x8c6c
#define SQ_LSTMP_RING_BASE				0x8e10
#define SQ_LSTMP_RING_SIZE				0x8e14
#define SQ_HSTMP_RING_BASE				0x8e18
#define SQ_HSTMP_RING_SIZE				0x8e1c
#define VGT_TF_RING_SIZE				0x8988

#define SQ_ESGS_RING_ITEMSIZE				0x28900
#define SQ_GSVS_RING_ITEMSIZE				0x28904
#define SQ_ESTMP_RING_ITEMSIZE				0x28908
#define SQ_GSTMP_RING_ITEMSIZE				0x2890c
#define SQ_VSTMP_RING_ITEMSIZE				0x28910
#define SQ_PSTMP_RING_ITEMSIZE				0x28914
#define SQ_LSTMP_RING_ITEMSIZE				0x28830
#define SQ_HSTMP_RING_ITEMSIZE				0x28834

#define SQ_GS_VERT_ITEMSIZE				0x2891c
#define SQ_GS_VERT_ITEMSIZE_1				0x28920
#define SQ_GS_VERT_ITEMSIZE_2				0x28924
#define SQ_GS_VERT_ITEMSIZE_3				0x28928
#define SQ_GSVS_RING_OFFSET_1				0x2892c
#define SQ_GSVS_RING_OFFSET_2				0x28930
#define SQ_GSVS_RING_OFFSET_3				0x28934

#define SQ_ALU_CONST_BUFFER_SIZE_PS_0			0x28140
#define SQ_ALU_CONST_BUFFER_SIZE_HS_0			0x28f80

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
#define SQ_ALU_CONST_CACHE_HS_0				0x28f00
#define SQ_ALU_CONST_CACHE_HS_1				0x28f04
#define SQ_ALU_CONST_CACHE_HS_2				0x28f08
#define SQ_ALU_CONST_CACHE_HS_3				0x28f0c
#define SQ_ALU_CONST_CACHE_HS_4				0x28f10
#define SQ_ALU_CONST_CACHE_HS_5				0x28f14
#define SQ_ALU_CONST_CACHE_HS_6				0x28f18
#define SQ_ALU_CONST_CACHE_HS_7				0x28f1c
#define SQ_ALU_CONST_CACHE_HS_8				0x28f20
#define SQ_ALU_CONST_CACHE_HS_9				0x28f24
#define SQ_ALU_CONST_CACHE_HS_10			0x28f28
#define SQ_ALU_CONST_CACHE_HS_11			0x28f2c
#define SQ_ALU_CONST_CACHE_HS_12			0x28f30
#define SQ_ALU_CONST_CACHE_HS_13			0x28f34
#define SQ_ALU_CONST_CACHE_HS_14			0x28f38
#define SQ_ALU_CONST_CACHE_HS_15			0x28f3c
#define SQ_ALU_CONST_CACHE_LS_0				0x28f40
#define SQ_ALU_CONST_CACHE_LS_1				0x28f44
#define SQ_ALU_CONST_CACHE_LS_2				0x28f48
#define SQ_ALU_CONST_CACHE_LS_3				0x28f4c
#define SQ_ALU_CONST_CACHE_LS_4				0x28f50
#define SQ_ALU_CONST_CACHE_LS_5				0x28f54
#define SQ_ALU_CONST_CACHE_LS_6				0x28f58
#define SQ_ALU_CONST_CACHE_LS_7				0x28f5c
#define SQ_ALU_CONST_CACHE_LS_8				0x28f60
#define SQ_ALU_CONST_CACHE_LS_9				0x28f64
#define SQ_ALU_CONST_CACHE_LS_10			0x28f68
#define SQ_ALU_CONST_CACHE_LS_11			0x28f6c
#define SQ_ALU_CONST_CACHE_LS_12			0x28f70
#define SQ_ALU_CONST_CACHE_LS_13			0x28f74
#define SQ_ALU_CONST_CACHE_LS_14			0x28f78
#define SQ_ALU_CONST_CACHE_LS_15			0x28f7c

#define PA_SC_SCREEN_SCISSOR_TL                         0x28030
#define PA_SC_GENERIC_SCISSOR_TL                        0x28240
#define PA_SC_WINDOW_SCISSOR_TL                         0x28204

#define VGT_PRIMITIVE_TYPE                              0x8958
#define VGT_INDEX_TYPE                                  0x895C

#define VGT_NUM_INDICES                                 0x8970

#define VGT_COMPUTE_DIM_X                               0x8990
#define VGT_COMPUTE_DIM_Y                               0x8994
#define VGT_COMPUTE_DIM_Z                               0x8998
#define VGT_COMPUTE_START_X                             0x899C
#define VGT_COMPUTE_START_Y                             0x89A0
#define VGT_COMPUTE_START_Z                             0x89A4
#define VGT_COMPUTE_INDEX                               0x89A8
#define VGT_COMPUTE_THREAD_GROUP_SIZE                   0x89AC
#define VGT_HS_OFFCHIP_PARAM                            0x89B0

#define DB_DEBUG					0x9830
#define DB_DEBUG2					0x9834
#define DB_DEBUG3					0x9838
#define DB_DEBUG4					0x983C
#define DB_WATERMARKS					0x9854
#define DB_DEPTH_CONTROL				0x28800
#define DB_DEPTH_VIEW					0x28008
#define DB_HTILE_DATA_BASE				0x28014
#define DB_Z_INFO					0x28040
#       define Z_ARRAY_MODE(x)                          ((x) << 4)
#       define DB_TILE_SPLIT(x)                         (((x) & 0x7) << 8)
#       define DB_NUM_BANKS(x)                          (((x) & 0x3) << 12)
#       define DB_BANK_WIDTH(x)                         (((x) & 0x3) << 16)
#       define DB_BANK_HEIGHT(x)                        (((x) & 0x3) << 20)
#define DB_STENCIL_INFO					0x28044
#define DB_Z_READ_BASE					0x28048
#define DB_STENCIL_READ_BASE				0x2804c
#define DB_Z_WRITE_BASE					0x28050
#define DB_STENCIL_WRITE_BASE				0x28054
#define DB_DEPTH_SIZE					0x28058

#define SQ_PGM_START_PS					0x28840
#define SQ_PGM_START_VS					0x2885c
#define SQ_PGM_START_GS					0x28874
#define SQ_PGM_START_ES					0x2888c
#define SQ_PGM_START_FS					0x288a4
#define SQ_PGM_START_HS					0x288b8
#define SQ_PGM_START_LS					0x288d0

#define VGT_STRMOUT_CONFIG				0x28b94
#define VGT_STRMOUT_BUFFER_CONFIG			0x28b98

#define CB_TARGET_MASK					0x28238
#define CB_SHADER_MASK					0x2823c

#define GDS_ADDR_BASE					0x28720

#define	CB_IMMED0_BASE					0x28b9c
#define	CB_IMMED1_BASE					0x28ba0
#define	CB_IMMED2_BASE					0x28ba4
#define	CB_IMMED3_BASE					0x28ba8
#define	CB_IMMED4_BASE					0x28bac
#define	CB_IMMED5_BASE					0x28bb0
#define	CB_IMMED6_BASE					0x28bb4
#define	CB_IMMED7_BASE					0x28bb8
#define	CB_IMMED8_BASE					0x28bbc
#define	CB_IMMED9_BASE					0x28bc0
#define	CB_IMMED10_BASE					0x28bc4
#define	CB_IMMED11_BASE					0x28bc8

/* all 12 CB blocks have these regs */
#define	CB_COLOR0_BASE					0x28c60
#define	CB_COLOR0_PITCH					0x28c64
#define	CB_COLOR0_SLICE					0x28c68
#define	CB_COLOR0_VIEW					0x28c6c
#define	CB_COLOR0_INFO					0x28c70
#	define CB_FORMAT(x)				((x) << 2)
#       define CB_ARRAY_MODE(x)                         ((x) << 8)
#       define ARRAY_LINEAR_GENERAL                     0
#       define ARRAY_LINEAR_ALIGNED                     1
#       define ARRAY_1D_TILED_THIN1                     2
#       define ARRAY_2D_TILED_THIN1                     4
#	define CB_SOURCE_FORMAT(x)			((x) << 24)
#	define CB_SF_EXPORT_FULL			0
#	define CB_SF_EXPORT_NORM			1
#define	CB_COLOR0_ATTRIB				0x28c74
#       define CB_TILE_SPLIT(x)                         (((x) & 0x7) << 5)
#       define ADDR_SURF_TILE_SPLIT_64B                 0
#       define ADDR_SURF_TILE_SPLIT_128B                1
#       define ADDR_SURF_TILE_SPLIT_256B                2
#       define ADDR_SURF_TILE_SPLIT_512B                3
#       define ADDR_SURF_TILE_SPLIT_1KB                 4
#       define ADDR_SURF_TILE_SPLIT_2KB                 5
#       define ADDR_SURF_TILE_SPLIT_4KB                 6
#       define CB_NUM_BANKS(x)                          (((x) & 0x3) << 10)
#       define ADDR_SURF_2_BANK                         0
#       define ADDR_SURF_4_BANK                         1
#       define ADDR_SURF_8_BANK                         2
#       define ADDR_SURF_16_BANK                        3
#       define CB_BANK_WIDTH(x)                         (((x) & 0x3) << 13)
#       define ADDR_SURF_BANK_WIDTH_1                   0
#       define ADDR_SURF_BANK_WIDTH_2                   1
#       define ADDR_SURF_BANK_WIDTH_4                   2
#       define ADDR_SURF_BANK_WIDTH_8                   3
#       define CB_BANK_HEIGHT(x)                        (((x) & 0x3) << 16)
#       define ADDR_SURF_BANK_HEIGHT_1                  0
#       define ADDR_SURF_BANK_HEIGHT_2                  1
#       define ADDR_SURF_BANK_HEIGHT_4                  2
#       define ADDR_SURF_BANK_HEIGHT_8                  3
#define	CB_COLOR0_DIM					0x28c78
/* only CB0-7 blocks have these regs */
#define	CB_COLOR0_CMASK					0x28c7c
#define	CB_COLOR0_CMASK_SLICE				0x28c80
#define	CB_COLOR0_FMASK					0x28c84
#define	CB_COLOR0_FMASK_SLICE				0x28c88
#define	CB_COLOR0_CLEAR_WORD0				0x28c8c
#define	CB_COLOR0_CLEAR_WORD1				0x28c90
#define	CB_COLOR0_CLEAR_WORD2				0x28c94
#define	CB_COLOR0_CLEAR_WORD3				0x28c98

#define	CB_COLOR1_BASE					0x28c9c
#define	CB_COLOR2_BASE					0x28cd8
#define	CB_COLOR3_BASE					0x28d14
#define	CB_COLOR4_BASE					0x28d50
#define	CB_COLOR5_BASE					0x28d8c
#define	CB_COLOR6_BASE					0x28dc8
#define	CB_COLOR7_BASE					0x28e04
#define	CB_COLOR8_BASE					0x28e40
#define	CB_COLOR9_BASE					0x28e5c
#define	CB_COLOR10_BASE					0x28e78
#define	CB_COLOR11_BASE					0x28e94

#define	CB_COLOR1_PITCH					0x28ca0
#define	CB_COLOR2_PITCH					0x28cdc
#define	CB_COLOR3_PITCH					0x28d18
#define	CB_COLOR4_PITCH					0x28d54
#define	CB_COLOR5_PITCH					0x28d90
#define	CB_COLOR6_PITCH					0x28dcc
#define	CB_COLOR7_PITCH					0x28e08
#define	CB_COLOR8_PITCH					0x28e44
#define	CB_COLOR9_PITCH					0x28e60
#define	CB_COLOR10_PITCH				0x28e7c
#define	CB_COLOR11_PITCH				0x28e98

#define	CB_COLOR1_SLICE					0x28ca4
#define	CB_COLOR2_SLICE					0x28ce0
#define	CB_COLOR3_SLICE					0x28d1c
#define	CB_COLOR4_SLICE					0x28d58
#define	CB_COLOR5_SLICE					0x28d94
#define	CB_COLOR6_SLICE					0x28dd0
#define	CB_COLOR7_SLICE					0x28e0c
#define	CB_COLOR8_SLICE					0x28e48
#define	CB_COLOR9_SLICE					0x28e64
#define	CB_COLOR10_SLICE				0x28e80
#define	CB_COLOR11_SLICE				0x28e9c

#define	CB_COLOR1_VIEW					0x28ca8
#define	CB_COLOR2_VIEW					0x28ce4
#define	CB_COLOR3_VIEW					0x28d20
#define	CB_COLOR4_VIEW					0x28d5c
#define	CB_COLOR5_VIEW					0x28d98
#define	CB_COLOR6_VIEW					0x28dd4
#define	CB_COLOR7_VIEW					0x28e10
#define	CB_COLOR8_VIEW					0x28e4c
#define	CB_COLOR9_VIEW					0x28e68
#define	CB_COLOR10_VIEW					0x28e84
#define	CB_COLOR11_VIEW					0x28ea0

#define	CB_COLOR1_INFO					0x28cac
#define	CB_COLOR2_INFO					0x28ce8
#define	CB_COLOR3_INFO					0x28d24
#define	CB_COLOR4_INFO					0x28d60
#define	CB_COLOR5_INFO					0x28d9c
#define	CB_COLOR6_INFO					0x28dd8
#define	CB_COLOR7_INFO					0x28e14
#define	CB_COLOR8_INFO					0x28e50
#define	CB_COLOR9_INFO					0x28e6c
#define	CB_COLOR10_INFO					0x28e88
#define	CB_COLOR11_INFO					0x28ea4

#define	CB_COLOR1_ATTRIB				0x28cb0
#define	CB_COLOR2_ATTRIB				0x28cec
#define	CB_COLOR3_ATTRIB				0x28d28
#define	CB_COLOR4_ATTRIB				0x28d64
#define	CB_COLOR5_ATTRIB				0x28da0
#define	CB_COLOR6_ATTRIB				0x28ddc
#define	CB_COLOR7_ATTRIB				0x28e18
#define	CB_COLOR8_ATTRIB				0x28e54
#define	CB_COLOR9_ATTRIB				0x28e70
#define	CB_COLOR10_ATTRIB				0x28e8c
#define	CB_COLOR11_ATTRIB				0x28ea8

#define	CB_COLOR1_DIM					0x28cb4
#define	CB_COLOR2_DIM					0x28cf0
#define	CB_COLOR3_DIM					0x28d2c
#define	CB_COLOR4_DIM					0x28d68
#define	CB_COLOR5_DIM					0x28da4
#define	CB_COLOR6_DIM					0x28de0
#define	CB_COLOR7_DIM					0x28e1c
#define	CB_COLOR8_DIM					0x28e58
#define	CB_COLOR9_DIM					0x28e74
#define	CB_COLOR10_DIM					0x28e90
#define	CB_COLOR11_DIM					0x28eac

#define	CB_COLOR1_CMASK					0x28cb8
#define	CB_COLOR2_CMASK					0x28cf4
#define	CB_COLOR3_CMASK					0x28d30
#define	CB_COLOR4_CMASK					0x28d6c
#define	CB_COLOR5_CMASK					0x28da8
#define	CB_COLOR6_CMASK					0x28de4
#define	CB_COLOR7_CMASK					0x28e20

#define	CB_COLOR1_CMASK_SLICE				0x28cbc
#define	CB_COLOR2_CMASK_SLICE				0x28cf8
#define	CB_COLOR3_CMASK_SLICE				0x28d34
#define	CB_COLOR4_CMASK_SLICE				0x28d70
#define	CB_COLOR5_CMASK_SLICE				0x28dac
#define	CB_COLOR6_CMASK_SLICE				0x28de8
#define	CB_COLOR7_CMASK_SLICE				0x28e24

#define	CB_COLOR1_FMASK					0x28cc0
#define	CB_COLOR2_FMASK					0x28cfc
#define	CB_COLOR3_FMASK					0x28d38
#define	CB_COLOR4_FMASK					0x28d74
#define	CB_COLOR5_FMASK					0x28db0
#define	CB_COLOR6_FMASK					0x28dec
#define	CB_COLOR7_FMASK					0x28e28

#define	CB_COLOR1_FMASK_SLICE				0x28cc4
#define	CB_COLOR2_FMASK_SLICE				0x28d00
#define	CB_COLOR3_FMASK_SLICE				0x28d3c
#define	CB_COLOR4_FMASK_SLICE				0x28d78
#define	CB_COLOR5_FMASK_SLICE				0x28db4
#define	CB_COLOR6_FMASK_SLICE				0x28df0
#define	CB_COLOR7_FMASK_SLICE				0x28e2c

#define	CB_COLOR1_CLEAR_WORD0				0x28cc8
#define	CB_COLOR2_CLEAR_WORD0				0x28d04
#define	CB_COLOR3_CLEAR_WORD0				0x28d40
#define	CB_COLOR4_CLEAR_WORD0				0x28d7c
#define	CB_COLOR5_CLEAR_WORD0				0x28db8
#define	CB_COLOR6_CLEAR_WORD0				0x28df4
#define	CB_COLOR7_CLEAR_WORD0				0x28e30

#define	CB_COLOR1_CLEAR_WORD1				0x28ccc
#define	CB_COLOR2_CLEAR_WORD1				0x28d08
#define	CB_COLOR3_CLEAR_WORD1				0x28d44
#define	CB_COLOR4_CLEAR_WORD1				0x28d80
#define	CB_COLOR5_CLEAR_WORD1				0x28dbc
#define	CB_COLOR6_CLEAR_WORD1				0x28df8
#define	CB_COLOR7_CLEAR_WORD1				0x28e34

#define	CB_COLOR1_CLEAR_WORD2				0x28cd0
#define	CB_COLOR2_CLEAR_WORD2				0x28d0c
#define	CB_COLOR3_CLEAR_WORD2				0x28d48
#define	CB_COLOR4_CLEAR_WORD2				0x28d84
#define	CB_COLOR5_CLEAR_WORD2				0x28dc0
#define	CB_COLOR6_CLEAR_WORD2				0x28dfc
#define	CB_COLOR7_CLEAR_WORD2				0x28e38

#define	CB_COLOR1_CLEAR_WORD3				0x28cd4
#define	CB_COLOR2_CLEAR_WORD3				0x28d10
#define	CB_COLOR3_CLEAR_WORD3				0x28d4c
#define	CB_COLOR4_CLEAR_WORD3				0x28d88
#define	CB_COLOR5_CLEAR_WORD3				0x28dc4
#define	CB_COLOR6_CLEAR_WORD3				0x28e00
#define	CB_COLOR7_CLEAR_WORD3				0x28e3c

#define SQ_TEX_RESOURCE_WORD0_0                         0x30000
#	define TEX_DIM(x)				((x) << 0)
#	define SQ_TEX_DIM_1D				0
#	define SQ_TEX_DIM_2D				1
#	define SQ_TEX_DIM_3D				2
#	define SQ_TEX_DIM_CUBEMAP			3
#	define SQ_TEX_DIM_1D_ARRAY			4
#	define SQ_TEX_DIM_2D_ARRAY			5
#	define SQ_TEX_DIM_2D_MSAA			6
#	define SQ_TEX_DIM_2D_ARRAY_MSAA			7
#define SQ_TEX_RESOURCE_WORD1_0                         0x30004
#       define TEX_ARRAY_MODE(x)                        ((x) << 28)
#define SQ_TEX_RESOURCE_WORD2_0                         0x30008
#define SQ_TEX_RESOURCE_WORD3_0                         0x3000C
#define SQ_TEX_RESOURCE_WORD4_0                         0x30010
#	define TEX_DST_SEL_X(x)				((x) << 16)
#	define TEX_DST_SEL_Y(x)				((x) << 19)
#	define TEX_DST_SEL_Z(x)				((x) << 22)
#	define TEX_DST_SEL_W(x)				((x) << 25)
#	define SQ_SEL_X					0
#	define SQ_SEL_Y					1
#	define SQ_SEL_Z					2
#	define SQ_SEL_W					3
#	define SQ_SEL_0					4
#	define SQ_SEL_1					5
#define SQ_TEX_RESOURCE_WORD5_0                         0x30014
#define SQ_TEX_RESOURCE_WORD6_0                         0x30018
#       define TEX_TILE_SPLIT(x)                        (((x) & 0x7) << 29)
#define SQ_TEX_RESOURCE_WORD7_0                         0x3001c
#       define TEX_BANK_WIDTH(x)                        (((x) & 0x3) << 8)
#       define TEX_BANK_HEIGHT(x)                       (((x) & 0x3) << 10)
#       define TEX_NUM_BANKS(x)                         (((x) & 0x3) << 16)

#define SQ_VTX_CONSTANT_WORD0_0				0x30000
#define SQ_VTX_CONSTANT_WORD1_0				0x30004
#define SQ_VTX_CONSTANT_WORD2_0				0x30008
#	define SQ_VTXC_BASE_ADDR_HI(x)			((x) << 0)
#	define SQ_VTXC_STRIDE(x)			((x) << 8)
#	define SQ_VTXC_ENDIAN_SWAP(x)			((x) << 30)
#	define SQ_ENDIAN_NONE				0
#	define SQ_ENDIAN_8IN16				1
#	define SQ_ENDIAN_8IN32				2
#define SQ_VTX_CONSTANT_WORD3_0				0x3000C
#	define SQ_VTCX_SEL_X(x)				((x) << 3)
#	define SQ_VTCX_SEL_Y(x)				((x) << 6)
#	define SQ_VTCX_SEL_Z(x)				((x) << 9)
#	define SQ_VTCX_SEL_W(x)				((x) << 12)
#define SQ_VTX_CONSTANT_WORD4_0				0x30010
#define SQ_VTX_CONSTANT_WORD5_0                         0x30014
#define SQ_VTX_CONSTANT_WORD6_0                         0x30018
#define SQ_VTX_CONSTANT_WORD7_0                         0x3001c

#define TD_PS_BORDER_COLOR_INDEX                        0xA400
#define TD_PS_BORDER_COLOR_RED                          0xA404
#define TD_PS_BORDER_COLOR_GREEN                        0xA408
#define TD_PS_BORDER_COLOR_BLUE                         0xA40C
#define TD_PS_BORDER_COLOR_ALPHA                        0xA410
#define TD_VS_BORDER_COLOR_INDEX                        0xA414
#define TD_VS_BORDER_COLOR_RED                          0xA418
#define TD_VS_BORDER_COLOR_GREEN                        0xA41C
#define TD_VS_BORDER_COLOR_BLUE                         0xA420
#define TD_VS_BORDER_COLOR_ALPHA                        0xA424
#define TD_GS_BORDER_COLOR_INDEX                        0xA428
#define TD_GS_BORDER_COLOR_RED                          0xA42C
#define TD_GS_BORDER_COLOR_GREEN                        0xA430
#define TD_GS_BORDER_COLOR_BLUE                         0xA434
#define TD_GS_BORDER_COLOR_ALPHA                        0xA438
#define TD_HS_BORDER_COLOR_INDEX                        0xA43C
#define TD_HS_BORDER_COLOR_RED                          0xA440
#define TD_HS_BORDER_COLOR_GREEN                        0xA444
#define TD_HS_BORDER_COLOR_BLUE                         0xA448
#define TD_HS_BORDER_COLOR_ALPHA                        0xA44C
#define TD_LS_BORDER_COLOR_INDEX                        0xA450
#define TD_LS_BORDER_COLOR_RED                          0xA454
#define TD_LS_BORDER_COLOR_GREEN                        0xA458
#define TD_LS_BORDER_COLOR_BLUE                         0xA45C
#define TD_LS_BORDER_COLOR_ALPHA                        0xA460
#define TD_CS_BORDER_COLOR_INDEX                        0xA464
#define TD_CS_BORDER_COLOR_RED                          0xA468
#define TD_CS_BORDER_COLOR_GREEN                        0xA46C
#define TD_CS_BORDER_COLOR_BLUE                         0xA470
#define TD_CS_BORDER_COLOR_ALPHA                        0xA474

/* cayman 3D regs */
#define CAYMAN_VGT_OFFCHIP_LDS_BASE			0x89B4
#define CAYMAN_SQ_EX_ALLOC_TABLE_SLOTS			0x8E48
#define CAYMAN_DB_EQAA					0x28804
#define CAYMAN_DB_DEPTH_INFO				0x2803C
#define CAYMAN_PA_SC_AA_CONFIG				0x28BE0
#define         CAYMAN_MSAA_NUM_SAMPLES_SHIFT           0
#define         CAYMAN_MSAA_NUM_SAMPLES_MASK            0x7
#define CAYMAN_SX_SCATTER_EXPORT_BASE			0x28358
/* cayman packet3 addition */
#define	CAYMAN_PACKET3_DEALLOC_STATE			0x14

#endif
