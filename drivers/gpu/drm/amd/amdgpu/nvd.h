/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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
 */

#ifndef NVD_H
#define NVD_H

/**
 * Navi's PM4 definitions
 */
#define	PACKET_TYPE0	0
#define	PACKET_TYPE1	1
#define	PACKET_TYPE2	2
#define	PACKET_TYPE3	3

#define CP_PACKET_GET_TYPE(h) (((h) >> 30) & 3)
#define CP_PACKET_GET_COUNT(h) (((h) >> 16) & 0x3FFF)
#define CP_PACKET0_GET_REG(h) ((h) & 0xFFFF)
#define CP_PACKET3_GET_OPCODE(h) (((h) >> 8) & 0xFF)
#define PACKET0(reg, n)	((PACKET_TYPE0 << 30) |				\
			 ((reg) & 0xFFFF) |			\
			 ((n) & 0x3FFF) << 16)
#define CP_PACKET2			0x80000000
#define		PACKET2_PAD_SHIFT		0
#define		PACKET2_PAD_MASK		(0x3fffffff << 0)

#define PACKET2(v)	(CP_PACKET2 | REG_SET(PACKET2_PAD, (v)))

#define PACKET3(op, n)	((PACKET_TYPE3 << 30) |				\
			 (((op) & 0xFF) << 8) |				\
			 ((n) & 0x3FFF) << 16)

#define PACKET3_COMPUTE(op, n) (PACKET3(op, n) | 1 << 1)

/* Packet 3 types */
#define	PACKET3_NOP					0x10
#define	PACKET3_SET_BASE				0x11
#define		PACKET3_BASE_INDEX(x)                  ((x) << 0)
#define			CE_PARTITION_BASE		3
#define	PACKET3_CLEAR_STATE				0x12
#define	PACKET3_INDEX_BUFFER_SIZE			0x13
#define	PACKET3_DISPATCH_DIRECT				0x15
#define	PACKET3_DISPATCH_INDIRECT			0x16
#define	PACKET3_INDIRECT_BUFFER_END			0x17
#define	PACKET3_INDIRECT_BUFFER_CNST_END		0x19
#define	PACKET3_ATOMIC_GDS				0x1D
#define	PACKET3_ATOMIC_MEM				0x1E
#define 	PACKET3_ATOMIC_MEM__ATOMIC(x) ((((unsigned)(x)) & 0x7F) << 0)
#define 	PACKET3_ATOMIC_MEM__COMMAND(x) ((((unsigned)(x)) & 0xF) << 8)
#define 	PACKET3_ATOMIC_MEM__CACHE_POLICY(x) ((((unsigned)(x)) & 0x3) << 25)
#define 	PACKET3_ATOMIC_MEM__ADDR_LO(x) (((unsigned)(x)))
#define 	PACKET3_ATOMIC_MEM__ADDR_HI(x) (((unsigned)(x)))
#define 	PACKET3_ATOMIC_MEM__SRC_DATA_LO(x) (((unsigned)(x)))
#define 	PACKET3_ATOMIC_MEM__SRC_DATA_HI(x) (((unsigned)(x)))
#define 	PACKET3_ATOMIC_MEM__CMP_DATA_LO(x) (((unsigned)(x)))
#define 	PACKET3_ATOMIC_MEM__CMP_DATA_HI(x) (((unsigned)(x)))
#define 	PACKET3_ATOMIC_MEM__LOOP_INTERVAL(x) ((((unsigned)(x)) & 0x1FFF) << 0)
#define 	PACKET3_ATOMIC_MEM__COMMAND__SINGLE_PASS_ATOMIC 0
#define 	PACKET3_ATOMIC_MEM__COMMAND__LOOP_UNTIL_COMPARE_SATISFIED 1
#define 	PACKET3_ATOMIC_MEM__COMMAND__WAIT_FOR_WRITE_CONFIRMATION 2
#define 	PACKET3_ATOMIC_MEM__COMMAND__SEND_AND_CONTINUE 3
#define 	PACKET3_ATOMIC_MEM__CACHE_POLICY__LRU 0
#define 	PACKET3_ATOMIC_MEM__CACHE_POLICY__STREAM 1
#define 	PACKET3_ATOMIC_MEM__CACHE_POLICY__NOA 2
#define 	PACKET3_ATOMIC_MEM__CACHE_POLICY__BYPASS 3
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
#define	PACKET3_NUM_INSTANCES				0x2F
#define	PACKET3_DRAW_INDEX_MULTI_AUTO			0x30
#define	PACKET3_INDIRECT_BUFFER_PRIV			0x32
#define	PACKET3_INDIRECT_BUFFER_CNST			0x33
#define	PACKET3_COND_INDIRECT_BUFFER_CNST		0x33
#define	PACKET3_STRMOUT_BUFFER_UPDATE			0x34
#define	PACKET3_DRAW_INDEX_OFFSET_2			0x35
#define	PACKET3_DRAW_PREAMBLE				0x36
#define	PACKET3_WRITE_DATA				0x37
#define		WRITE_DATA_DST_SEL(x)                   ((x) << 8)
		/* 0 - register
		 * 1 - memory (sync - via GRBM)
		 * 2 - gl2
		 * 3 - gds
		 * 4 - reserved
		 * 5 - memory (async - direct)
		 */
#define		WR_ONE_ADDR                             (1 << 16)
#define		WR_CONFIRM                              (1 << 20)
#define		WRITE_DATA_CACHE_POLICY(x)              ((x) << 25)
		/* 0 - LRU
		 * 1 - Stream
		 */
#define		WRITE_DATA_ENGINE_SEL(x)                ((x) << 30)
		/* 0 - me
		 * 1 - pfp
		 * 2 - ce
		 */
#define 	PACKET3_WRITE_DATA__DST_SEL(x) ((((unsigned)(x)) & 0xF) << 8)
#define 	PACKET3_WRITE_DATA__ADDR_INCR(x) ((((unsigned)(x)) & 0x1) << 16)
#define 	PACKET3_WRITE_DATA__WR_CONFIRM(x) ((((unsigned)(x)) & 0x1) << 20)
#define 	PACKET3_WRITE_DATA__CACHE_POLICY(x) ((((unsigned)(x)) & 0x3) << 25)
#define 	PACKET3_WRITE_DATA__DST_MMREG_ADDR(x) ((((unsigned)(x)) & 0x3FFFF) << 0)
#define 	PACKET3_WRITE_DATA__DST_GDS_ADDR(x) ((((unsigned)(x)) & 0xFFFF) << 0)
#define 	PACKET3_WRITE_DATA__DST_MEM_ADDR_LO(x) ((((unsigned)(x)) & 0x3FFFFFFF) << 2)
#define 	PACKET3_WRITE_DATA__DST_MEM_ADDR_HI(x) ((unsigned)(x))
#define 	PACKET3_WRITE_DATA__MODE(x) ((((unsigned)(x)) & 0x1) << 21)
#define 	PACKET3_WRITE_DATA__AID_ID(x) ((((unsigned)(x)) & 0x3) << 22)
#define 	PACKET3_WRITE_DATA__TEMPORAL(x) ((((unsigned)(x)) & 0x3) << 24)
#define 	PACKET3_WRITE_DATA__DST_MMREG_ADDR_LO(x) ((unsigned)(x))
#define 	PACKET3_WRITE_DATA__DST_MMREG_ADDR_HI(x) ((((unsigned)(x)) & 0xFF) << 0)
#define 	PACKET3_WRITE_DATA__DST_SEL__MEM_MAPPED_REGISTER 0
#define 	PACKET3_WRITE_DATA__DST_SEL__TC_L2 2
#define 	PACKET3_WRITE_DATA__DST_SEL__GDS 3
#define 	PACKET3_WRITE_DATA__DST_SEL__MEMORY 5
#define 	PACKET3_WRITE_DATA__DST_SEL__MEMORY_MAPPED_ADC_PERSISTENT_STATE 6
#define 	PACKET3_WRITE_DATA__ADDR_INCR__INCREMENT_ADDRESS 0
#define 	PACKET3_WRITE_DATA__ADDR_INCR__DO_NOT_INCREMENT_ADDRESS 1
#define 	PACKET3_WRITE_DATA__WR_CONFIRM__DO_NOT_WAIT_FOR_WRITE_CONFIRMATION 0
#define 	PACKET3_WRITE_DATA__WR_CONFIRM__WAIT_FOR_WRITE_CONFIRMATION 1
#define 	PACKET3_WRITE_DATA__MODE__PF_VF_DISABLED 0
#define 	PACKET3_WRITE_DATA__MODE__PF_VF_ENABLED 1
#define 	PACKET3_WRITE_DATA__TEMPORAL__RT 0
#define 	PACKET3_WRITE_DATA__TEMPORAL__NT 1
#define 	PACKET3_WRITE_DATA__TEMPORAL__HT 2
#define 	PACKET3_WRITE_DATA__TEMPORAL__LU 3
#define 	PACKET3_WRITE_DATA__CACHE_POLICY__LRU 0
#define 	PACKET3_WRITE_DATA__CACHE_POLICY__STREAM 1
#define 	PACKET3_WRITE_DATA__CACHE_POLICY__NOA 2
#define 	PACKET3_WRITE_DATA__CACHE_POLICY__BYPASS 3
#define	PACKET3_DRAW_INDEX_INDIRECT_MULTI		0x38
#define	PACKET3_MEM_SEMAPHORE				0x39
#              define PACKET3_SEM_USE_MAILBOX       (0x1 << 16)
#              define PACKET3_SEM_SEL_SIGNAL_TYPE   (0x1 << 20) /* 0 = increment, 1 = write 1 */
#              define PACKET3_SEM_SEL_SIGNAL	    (0x6 << 29)
#              define PACKET3_SEM_SEL_WAIT	    (0x7 << 29)
#define	PACKET3_DRAW_INDEX_MULTI_INST			0x3A
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
#define		WAIT_REG_MEM_OPERATION(x)               ((x) << 6)
		/* 0 - wait_reg_mem
		 * 1 - wr_wait_wr_reg
		 */
#define		WAIT_REG_MEM_ENGINE(x)                  ((x) << 8)
		/* 0 - me
		 * 1 - pfp
		 */
#define		PACKET3_WAIT_REG_MEM__FUNCTION(x) ((((unsigned)(x)) & 0x7) << 0)
#define		PACKET3_WAIT_REG_MEM__MEM_SPACE(x) ((((unsigned)(x)) & 0x3) << 4)
#define		PACKET3_WAIT_REG_MEM__OPERATION(x) ((((unsigned)(x)) & 0x3) << 6)
#define		PACKET3_WAIT_REG_MEM__MES_INTR_PIPE(x) ((((unsigned)(x)) & 0x3) << 22)
#define		PACKET3_WAIT_REG_MEM__MES_ACTION(x) ((((unsigned)(x)) & 0x1) << 24)
#define		PACKET3_WAIT_REG_MEM__CACHE_POLICY(x) ((((unsigned)(x)) & 0x3) << 25)
#define		PACKET3_WAIT_REG_MEM__TEMPORAL(x) ((((unsigned)(x)) & 0x3) << 25)
#define		PACKET3_WAIT_REG_MEM__MEM_POLL_ADDR_LO(x) ((((unsigned)(x)) & 0x3FFFFFFF) << 2)
#define		PACKET3_WAIT_REG_MEM__REG_POLL_ADDR(x) ((((unsigned)(x)) & 0X3FFFF) << 0)
#define		PACKET3_WAIT_REG_MEM__REG_WRITE_ADDR1(x) ((((unsigned)(x)) & 0X3FFFF) << 0)
#define		PACKET3_WAIT_REG_MEM__MEM_POLL_ADDR_HI(x) ((unsigned)(x))
#define		PACKET3_WAIT_REG_MEM__REG_WRITE_ADDR2(x) ((((unsigned)(x)) & 0x3FFFF) << 0)
#define		PACKET3_WAIT_REG_MEM__REFERENCE(x) ((unsigned)(x))
#define		PACKET3_WAIT_REG_MEM__MASK(x) ((unsigned)(x))
#define		PACKET3_WAIT_REG_MEM__POLL_INTERVAL(x) ((((unsigned)(x)) & 0xFFFF) << 0)
#define		PACKET3_WAIT_REG_MEM__OPTIMIZE_ACE_OFFLOAD_MODE(x) ((((unsigned)(x)) & 0x1) << 31)
#define 	PACKET3_WAIT_REG_MEM__FUNCTION__ALWAYS_PASS 0
#define 	PACKET3_WAIT_REG_MEM__FUNCTION__LESS_THAN_REF_VALUE 1
#define 	PACKET3_WAIT_REG_MEM__FUNCTION__LESS_THAN_EQUAL_TO_THE_REF_VALUE 2
#define 	PACKET3_WAIT_REG_MEM__FUNCTION__EQUAL_TO_THE_REFERENCE_VALUE 3
#define 	PACKET3_WAIT_REG_MEM__FUNCTION__NOT_EQUAL_REFERENCE_VALUE 4
#define 	PACKET3_WAIT_REG_MEM__FUNCTION__GREATER_THAN_OR_EQUAL_REFERENCE_VALUE 5
#define 	PACKET3_WAIT_REG_MEM__FUNCTION__GREATER_THAN_REFERENCE_VALUE 6
#define 	PACKET3_WAIT_REG_MEM__MEM_SPACE__REGISTER_SPACE 0
#define 	PACKET3_WAIT_REG_MEM__MEM_SPACE__MEMORY_SPACE 1
#define 	PACKET3_WAIT_REG_MEM__OPERATION__WAIT_REG_MEM 0
#define 	PACKET3_WAIT_REG_MEM__OPERATION__WR_WAIT_WR_REG 1
#define 	PACKET3_WAIT_REG_MEM__OPERATION__WAIT_MEM_PREEMPTABLE 3
#define 	PACKET3_WAIT_REG_MEM__CACHE_POLICY__LRU 0
#define 	PACKET3_WAIT_REG_MEM__CACHE_POLICY__STREAM 1
#define 	PACKET3_WAIT_REG_MEM__CACHE_POLICY__NOA 2
#define 	PACKET3_WAIT_REG_MEM__CACHE_POLICY__BYPASS 3
#define 	PACKET3_WAIT_REG_MEM__TEMPORAL__RT 0
#define 	PACKET3_WAIT_REG_MEM__TEMPORAL__NT 1
#define 	PACKET3_WAIT_REG_MEM__TEMPORAL__HT 2
#define 	PACKET3_WAIT_REG_MEM__TEMPORAL__LU 3
#define	PACKET3_INDIRECT_BUFFER				0x3F
#define		INDIRECT_BUFFER_VALID                   (1 << 23)
#define		INDIRECT_BUFFER_CACHE_POLICY(x)         ((x) << 28)
		/* 0 - LRU
		 * 1 - Stream
		 * 2 - Bypass
		 */
#define		INDIRECT_BUFFER_PRE_ENB(x)		((x) << 21)
#define		INDIRECT_BUFFER_PRE_RESUME(x)           ((x) << 30)
#define 	PACKET3_INDIRECT_BUFFER__IB_BASE_LO(x) ((((unsigned)(x)) & 0x3FFFFFFF) << 2)
#define 	PACKET3_INDIRECT_BUFFER__IB_BASE_HI(x) ((unsigned)(x))
#define 	PACKET3_INDIRECT_BUFFER__IB_SIZE(x) ((((unsigned)(x)) & 0xFFFFF) << 0)
#define 	PACKET3_INDIRECT_BUFFER__CHAIN(x) ((((unsigned)(x)) & 0x1) << 20)
#define 	PACKET3_INDIRECT_BUFFER__OFFLOAD_POLLING(x) ((((unsigned)(x)) & 0x1) << 21)
#define 	PACKET3_INDIRECT_BUFFER__VALID(x) ((((unsigned)(x)) & 0x1) << 23)
#define 	PACKET3_INDIRECT_BUFFER__VMID(x) ((((unsigned)(x)) & 0xF) << 24)
#define 	PACKET3_INDIRECT_BUFFER__CACHE_POLICY(x) ((((unsigned)(x)) & 0x3) << 28)
#define 	PACKET3_INDIRECT_BUFFER__TEMPORAL(x) ((((unsigned)(x)) & 0x3) << 28)
#define 	PACKET3_INDIRECT_BUFFER__PRIV(x) ((((unsigned)(x)) & 0x1) << 31)
#define 	PACKET3_INDIRECT_BUFFER__TEMPORAL__RT 0
#define 	PACKET3_INDIRECT_BUFFER__TEMPORAL__NT 1
#define 	PACKET3_INDIRECT_BUFFER__TEMPORAL__HT 2
#define 	PACKET3_INDIRECT_BUFFER__TEMPORAL__LU 3
#define 	PACKET3_INDIRECT_BUFFER__CACHE_POLICY__LRU 0
#define 	PACKET3_INDIRECT_BUFFER__CACHE_POLICY__STREAM 1
#define 	PACKET3_INDIRECT_BUFFER__CACHE_POLICY__NOA 2
#define 	PACKET3_INDIRECT_BUFFER__CACHE_POLICY__BYPASS 3
#define	PACKET3_COND_INDIRECT_BUFFER			0x3F
#define	PACKET3_COPY_DATA				0x40
#define 	PACKET3_COPY_DATA__SRC_SEL(x) ((((unsigned)(x)) & 0xF) << 0)
#define 	PACKET3_COPY_DATA__DST_SEL(x) ((((unsigned)(x)) & 0xF) << 8)
#define 	PACKET3_COPY_DATA__SRC_CACHE_POLICY(x) ((((unsigned)(x)) & 0x3) << 13)
#define 	PACKET3_COPY_DATA__SRC_TEMPORAL(x) ((((unsigned)(x)) & 0x3) << 13)
#define 	PACKET3_COPY_DATA__COUNT_SEL(x) ((((unsigned)(x)) & 0x1) << 16)
#define 	PACKET3_COPY_DATA__WR_CONFIRM(x) ((((unsigned)(x)) & 0x1) << 20)
#define 	PACKET3_COPY_DATA__DST_CACHE_POLICY(x) ((((unsigned)(x)) & 0x3) << 25)
#define 	PACKET3_COPY_DATA__PQ_EXE_STATUS(x) ((((unsigned)(x)) & 0x1) << 29)
#define 	PACKET3_COPY_DATA__SRC_REG_OFFSET(x) ((((unsigned)(x)) & 0x3FFFF) << 0)
#define 	PACKET3_COPY_DATA__SRC_32B_ADDR_LO(x) ((((unsigned)(x)) & 0x3FFFFFFF) << 2)
#define 	PACKET3_COPY_DATA__SRC_64B_ADDR_LO(x) ((((unsigned)(x)) & 0x1FFFFFFF) << 3)
#define 	PACKET3_COPY_DATA__SRC_GDS_ADDR_LO(x) ((((unsigned)(x)) & 0xFFFF) << 0)
#define 	PACKET3_COPY_DATA__IMM_DATA(x) ((unsigned)(x))
#define 	PACKET3_COPY_DATA__SRC_MEMTC_ADDR_HI(x) ((unsigned)(x))
#define 	PACKET3_COPY_DATA__SRC_IMM_DATA(x) ((unsigned)(x))
#define 	PACKET3_COPY_DATA__DST_REG_OFFSET(x) ((((unsigned)(x)) & 0x3FFFF) << 0)
#define 	PACKET3_COPY_DATA__DST_32B_ADDR_LO(x) ((((unsigned)(x)) & 0x3FFFFFFF) << 2)
#define 	PACKET3_COPY_DATA__DST_64B_ADDR_LO(x) ((((unsigned)(x)) & 0x1FFFFFFF) << 3)
#define 	PACKET3_COPY_DATA__DST_GDS_ADDR_LO(x) ((((unsigned)(x)) & 0xFFFF) << 0)
#define 	PACKET3_COPY_DATA__DST_ADDR_HI(x) ((unsigned)(x))
#define 	PACKET3_COPY_DATA__MODE(x) ((((unsigned)(x)) & 0x1) << 21)
#define 	PACKET3_COPY_DATA__AID_ID(x) ((((unsigned)(x)) & 0x3) << 23)
#define 	PACKET3_COPY_DATA__DST_TEMPORAL(x) ((((unsigned)(x)) & 0x3) << 25)
#define 	PACKET3_COPY_DATA__SRC_REG_OFFSET_LO(x) ((unsigned)(x))
#define 	PACKET3_COPY_DATA__SRC_REG_OFFSET_HI(x) ((((unsigned)(x)) & 0xFF) << 0)
#define 	PACKET3_COPY_DATA__DST_REG_OFFSET_LO(x) ((unsigned)(x))
#define 	PACKET3_COPY_DATA__DST_REG_OFFSET_HI(x) ((((unsigned)(x)) & 0xFF) << 0)
#define 	PACKET3_COPY_DATA__SRC_SEL__MEM_MAPPED_REGISTER 0
#define 	PACKET3_COPY_DATA__SRC_SEL__TC_L2_OBSOLETE 1
#define 	PACKET3_COPY_DATA__SRC_SEL__TC_L2 2
#define 	PACKET3_COPY_DATA__SRC_SEL__GDS 3
#define 	PACKET3_COPY_DATA__SRC_SEL__PERFCOUNTERS 4
#define 	PACKET3_COPY_DATA__SRC_SEL__IMMEDIATE_DATA 5
#define 	PACKET3_COPY_DATA__SRC_SEL__ATOMIC_RETURN_DATA 6
#define 	PACKET3_COPY_DATA__SRC_SEL__GDS_ATOMIC_RETURN_DATA0 7
#define 	PACKET3_COPY_DATA__SRC_SEL__GDS_ATOMIC_RETURN_DATA1 8
#define 	PACKET3_COPY_DATA__SRC_SEL__GPU_CLOCK_COUNT 9
#define 	PACKET3_COPY_DATA__SRC_SEL__SYSTEM_CLOCK_COUNT 10
#define 	PACKET3_COPY_DATA__DST_SEL__MEM_MAPPED_REGISTER 0
#define 	PACKET3_COPY_DATA__DST_SEL__TC_L2 2
#define 	PACKET3_COPY_DATA__DST_SEL__GDS 3
#define 	PACKET3_COPY_DATA__DST_SEL__PERFCOUNTERS 4
#define 	PACKET3_COPY_DATA__DST_SEL__TC_L2_OBSOLETE 5
#define 	PACKET3_COPY_DATA__DST_SEL__MEM_MAPPED_REG_DC 6
#define 	PACKET3_COPY_DATA__SRC_TEMPORAL__RT 0
#define 	PACKET3_COPY_DATA__SRC_TEMPORAL__NT 1
#define 	PACKET3_COPY_DATA__SRC_TEMPORAL__HT 2
#define 	PACKET3_COPY_DATA__SRC_TEMPORAL__LU 3
#define 	PACKET3_COPY_DATA__SRC_CACHE_POLICY__LRU 0
#define 	PACKET3_COPY_DATA__SRC_CACHE_POLICY__STREAM 1
#define 	PACKET3_COPY_DATA__SRC_CACHE_POLICY__NOA 2
#define 	PACKET3_COPY_DATA__SRC_CACHE_POLICY__BYPASS 3
#define 	PACKET3_COPY_DATA__COUNT_SEL__32_BITS_OF_DATA 0
#define 	PACKET3_COPY_DATA__COUNT_SEL__64_BITS_OF_DATA 1
#define 	PACKET3_COPY_DATA__WR_CONFIRM__DO_NOT_WAIT_FOR_CONFIRMATION 0
#define 	PACKET3_COPY_DATA__WR_CONFIRM__WAIT_FOR_CONFIRMATION 1
#define 	PACKET3_COPY_DATA__MODE__PF_VF_DISABLED 0
#define 	PACKET3_COPY_DATA__MODE__PF_VF_ENABLED 1
#define 	PACKET3_COPY_DATA__DST_TEMPORAL__RT 0
#define 	PACKET3_COPY_DATA__DST_TEMPORAL__NT 1
#define 	PACKET3_COPY_DATA__DST_TEMPORAL__HT 2
#define 	PACKET3_COPY_DATA__DST_TEMPORAL__LU 3
#define 	PACKET3_COPY_DATA__DST_CACHE_POLICY__LRU 0
#define 	PACKET3_COPY_DATA__DST_CACHE_POLICY__STREAM 1
#define 	PACKET3_COPY_DATA__DST_CACHE_POLICY__NOA 2
#define 	PACKET3_COPY_DATA__DST_CACHE_POLICY__BYPASS 3
#define 	PACKET3_COPY_DATA__PQ_EXE_STATUS__DEFAULT 0
#define 	PACKET3_COPY_DATA__PQ_EXE_STATUS__PHASE_UPDATE 1
#define	PACKET3_CP_DMA					0x41
#define	PACKET3_PFP_SYNC_ME				0x42
#define	PACKET3_SURFACE_SYNC				0x43
#define	PACKET3_ME_INITIALIZE				0x44
#define	PACKET3_COND_WRITE				0x45
#define	PACKET3_EVENT_WRITE				0x46
#define		EVENT_TYPE(x)                           ((x) << 0)
#define		EVENT_INDEX(x)                          ((x) << 8)
		/* 0 - any non-TS event
		 * 1 - ZPASS_DONE, PIXEL_PIPE_STAT_*
		 * 2 - SAMPLE_PIPELINESTAT
		 * 3 - SAMPLE_STREAMOUTSTAT*
		 * 4 - *S_PARTIAL_FLUSH
		 */
#define		PACKET3_EVENT_WRITE__EVENT_TYPE(x) ((((unsigned)(x)) & 0x3F) << 0)
#define		PACKET3_EVENT_WRITE__EVENT_INDEX(x) ((((unsigned)(x)) & 0xF) << 8)
#define		PACKET3_EVENT_WRITE__SAMP_PLST_CNTR_MODE(x) ((((unsigned)(x)) & 0x3) << 29)
#define		PACKET3_EVENT_WRITE__OFFLOAD_ENABLE(x) ((((unsigned)(x)) & 0x1) << 0)
#define 	PACKET3_EVENT_WRITE__ADDRESS_LO(x) ((((unsigned)(x)) & 0x1FFFFFFF) << 3)
#define 	PACKET3_EVENT_WRITE__ADDRESS_HI(x) ((unsigned)(x))
#define 	PACKET3_EVENT_WRITE__EVENT_INDEX__OTHER 0
#define 	PACKET3_EVENT_WRITE__EVENT_INDEX__SAMPLE_PIPELINESTAT 2
#define 	PACKET3_EVENT_WRITE__EVENT_INDEX__CS_PARTIAL_FLUSH 4
#define 	PACKET3_EVENT_WRITE__EVENT_INDEX__SAMPLE_STREAMOUTSTATS 8
#define 	PACKET3_EVENT_WRITE__EVENT_INDEX__SAMPLE_STREAMOUTSTATS1 9
#define 	PACKET3_EVENT_WRITE__EVENT_INDEX__SAMPLE_STREAMOUTSTATS2 10
#define 	PACKET3_EVENT_WRITE__EVENT_INDEX__SAMPLE_STREAMOUTSTATS3 11
#define 	PACKET3_EVENT_WRITE__SAMP_PLST_CNTR_MODE__LEGACY_MODE 0
#define 	PACKET3_EVENT_WRITE__SAMP_PLST_CNTR_MODE__MIXED_MODE1 1
#define 	PACKET3_EVENT_WRITE__SAMP_PLST_CNTR_MODE__NEW_MODE 2
#define 	PACKET3_EVENT_WRITE__SAMP_PLST_CNTR_MODE__MIXED_MODE3 3
#define	PACKET3_EVENT_WRITE_EOP				0x47
#define	PACKET3_EVENT_WRITE_EOS				0x48
#define	PACKET3_RELEASE_MEM				0x49
#define		PACKET3_RELEASE_MEM_EVENT_TYPE(x)	((x) << 0)
#define		PACKET3_RELEASE_MEM_EVENT_INDEX(x)	((x) << 8)
#define		PACKET3_RELEASE_MEM_GCR_GLM_WB		(1 << 12)
#define		PACKET3_RELEASE_MEM_GCR_GLM_INV		(1 << 13)
#define		PACKET3_RELEASE_MEM_GCR_GLV_INV		(1 << 14)
#define		PACKET3_RELEASE_MEM_GCR_GL1_INV		(1 << 15)
#define		PACKET3_RELEASE_MEM_GCR_GL2_US		(1 << 16)
#define		PACKET3_RELEASE_MEM_GCR_GL2_RANGE	(1 << 17)
#define		PACKET3_RELEASE_MEM_GCR_GL2_DISCARD	(1 << 19)
#define		PACKET3_RELEASE_MEM_GCR_GL2_INV		(1 << 20)
#define		PACKET3_RELEASE_MEM_GCR_GL2_WB		(1 << 21)
#define		PACKET3_RELEASE_MEM_GCR_SEQ		(1 << 22)
#define		PACKET3_RELEASE_MEM_CACHE_POLICY(x)	((x) << 25)
		/* 0 - cache_policy__me_release_mem__lru
		 * 1 - cache_policy__me_release_mem__stream
		 * 2 - cache_policy__me_release_mem__noa
		 * 3 - cache_policy__me_release_mem__bypass
		 */
#define		PACKET3_RELEASE_MEM_EXECUTE		(1 << 28)

#define		PACKET3_RELEASE_MEM_DATA_SEL(x)		((x) << 29)
		/* 0 - discard
		 * 1 - send low 32bit data
		 * 2 - send 64bit data
		 * 3 - send 64bit GPU counter value
		 * 4 - send 64bit sys counter value
		 */
#define		PACKET3_RELEASE_MEM_INT_SEL(x)		((x) << 24)
		/* 0 - none
		 * 1 - interrupt only (DATA_SEL = 0)
		 * 2 - interrupt when data write is confirmed
		 */
#define		PACKET3_RELEASE_MEM_DST_SEL(x)		((x) << 16)
		/* 0 - MC
		 * 1 - TC/L2
		 */



#define	PACKET3_PREAMBLE_CNTL				0x4A
#              define PACKET3_PREAMBLE_BEGIN_CLEAR_STATE     (2 << 28)
#              define PACKET3_PREAMBLE_END_CLEAR_STATE       (3 << 28)
#define	PACKET3_DMA_DATA				0x50
/* 1. header
 * 2. CONTROL
 * 3. SRC_ADDR_LO or DATA [31:0]
 * 4. SRC_ADDR_HI [31:0]
 * 5. DST_ADDR_LO [31:0]
 * 6. DST_ADDR_HI [7:0]
 * 7. COMMAND [31:26] | BYTE_COUNT [25:0]
 */
/* CONTROL */
#              define PACKET3_DMA_DATA_ENGINE(x)     ((x) << 0)
		/* 0 - ME
		 * 1 - PFP
		 */
#              define PACKET3_DMA_DATA_SRC_CACHE_POLICY(x) ((x) << 13)
		/* 0 - LRU
		 * 1 - Stream
		 */
#              define PACKET3_DMA_DATA_DST_SEL(x)  ((x) << 20)
		/* 0 - DST_ADDR using DAS
		 * 1 - GDS
		 * 3 - DST_ADDR using L2
		 */
#              define PACKET3_DMA_DATA_DST_CACHE_POLICY(x) ((x) << 25)
		/* 0 - LRU
		 * 1 - Stream
		 */
#              define PACKET3_DMA_DATA_SRC_SEL(x)  ((x) << 29)
		/* 0 - SRC_ADDR using SAS
		 * 1 - GDS
		 * 2 - DATA
		 * 3 - SRC_ADDR using L2
		 */
#              define PACKET3_DMA_DATA_CP_SYNC     (1 << 31)
/* COMMAND */
#              define PACKET3_DMA_DATA_CMD_SAS     (1 << 26)
		/* 0 - memory
		 * 1 - register
		 */
#              define PACKET3_DMA_DATA_CMD_DAS     (1 << 27)
		/* 0 - memory
		 * 1 - register
		 */
#              define PACKET3_DMA_DATA_CMD_SAIC    (1 << 28)
#              define PACKET3_DMA_DATA_CMD_DAIC    (1 << 29)
#              define PACKET3_DMA_DATA_CMD_RAW_WAIT  (1 << 30)
#define	PACKET3_CONTEXT_REG_RMW				0x51
#define	PACKET3_GFX_CNTX_UPDATE				0x52
#define	PACKET3_BLK_CNTX_UPDATE				0x53
#define	PACKET3_INCR_UPDT_STATE				0x55
#define	PACKET3_ACQUIRE_MEM				0x58
/* 1.  HEADER
 * 2.  COHER_CNTL [30:0]
 * 2.1 ENGINE_SEL [31:31]
 * 2.  COHER_SIZE [31:0]
 * 3.  COHER_SIZE_HI [7:0]
 * 4.  COHER_BASE_LO [31:0]
 * 5.  COHER_BASE_HI [23:0]
 * 7.  POLL_INTERVAL [15:0]
 * 8.  GCR_CNTL [18:0]
 */
#define 	PACKET3_ACQUIRE_MEM_GCR_CNTL_GLI_INV(x) ((x) << 0)
		/*
		 * 0:NOP
		 * 1:ALL
		 * 2:RANGE
		 * 3:FIRST_LAST
		 */
#define 	PACKET3_ACQUIRE_MEM_GCR_CNTL_GL1_RANGE(x) ((x) << 2)
		/*
		 * 0:ALL
		 * 1:reserved
		 * 2:RANGE
		 * 3:FIRST_LAST
		 */
#define 	PACKET3_ACQUIRE_MEM_GCR_CNTL_GLM_WB(x) ((x) << 4)
#define 	PACKET3_ACQUIRE_MEM_GCR_CNTL_GLM_INV(x) ((x) << 5)
#define 	PACKET3_ACQUIRE_MEM_GCR_CNTL_GLK_WB(x) ((x) << 6)
#define 	PACKET3_ACQUIRE_MEM_GCR_CNTL_GLK_INV(x) ((x) << 7)
#define 	PACKET3_ACQUIRE_MEM_GCR_CNTL_GLV_INV(x) ((x) << 8)
#define 	PACKET3_ACQUIRE_MEM_GCR_CNTL_GL1_INV(x) ((x) << 9)
#define 	PACKET3_ACQUIRE_MEM_GCR_CNTL_GL2_US(x) ((x) << 10)
#define 	PACKET3_ACQUIRE_MEM_GCR_CNTL_GL2_RANGE(x) ((x) << 11)
		/*
		 * 0:ALL
		 * 1:VOL
		 * 2:RANGE
		 * 3:FIRST_LAST
		 */
#define 	PACKET3_ACQUIRE_MEM_GCR_CNTL_GL2_DISCARD(x)  ((x) << 13)
#define 	PACKET3_ACQUIRE_MEM_GCR_CNTL_GL2_INV(x) ((x) << 14)
#define 	PACKET3_ACQUIRE_MEM_GCR_CNTL_GL2_WB(x) ((x) << 15)
#define 	PACKET3_ACQUIRE_MEM_GCR_CNTL_SEQ(x) ((x) << 16)
		/*
		 * 0: PARALLEL
		 * 1: FORWARD
		 * 2: REVERSE
		 */
#define 	PACKET3_ACQUIRE_MEM_GCR_RANGE_IS_PA  (1 << 18)
#define 	PACKET3_ACQUIRE_MEM__COHER_SIZE(x) ((unsigned)(x))
#define 	PACKET3_ACQUIRE_MEM__COHER_SIZE_HI(x) ((((unsigned)(x)) & 0xFF) << 0)
#define 	PACKET3_ACQUIRE_MEM__COHER_BASE_LO(x) ((unsigned)(x))
#define 	PACKET3_ACQUIRE_MEM__COHER_BASE_HI(x) ((((unsigned)(x)) & 0xFFFFFF) << 0)
#define 	PACKET3_ACQUIRE_MEM__POLL_INTERVAL(x) ((((unsigned)(x)) & 0xFFFF) << 0)
#define 	PACKET3_ACQUIRE_MEM__GCR_CNTL(x) ((((unsigned)(x)) & 0x7FFFF) << 0)
#define	PACKET3_REWIND					0x59
#define	PACKET3_INTERRUPT				0x5A
#define	PACKET3_GEN_PDEPTE				0x5B
#define	PACKET3_INDIRECT_BUFFER_PASID			0x5C
#define	PACKET3_PRIME_UTCL2				0x5D
#define	PACKET3_LOAD_UCONFIG_REG			0x5E
#define	PACKET3_LOAD_SH_REG				0x5F
#define	PACKET3_LOAD_CONFIG_REG				0x60
#define	PACKET3_LOAD_CONTEXT_REG			0x61
#define	PACKET3_LOAD_COMPUTE_STATE			0x62
#define	PACKET3_LOAD_SH_REG_INDEX			0x63
#define	PACKET3_SET_CONFIG_REG				0x68
#define		PACKET3_SET_CONFIG_REG_START			0x00002000
#define		PACKET3_SET_CONFIG_REG_END			0x00002c00
#define	PACKET3_SET_CONTEXT_REG				0x69
#define		PACKET3_SET_CONTEXT_REG_START			0x0000a000
#define		PACKET3_SET_CONTEXT_REG_END			0x0000a400
#define	PACKET3_SET_CONTEXT_REG_INDEX			0x6A
#define	PACKET3_SET_VGPR_REG_DI_MULTI			0x71
#define	PACKET3_SET_SH_REG_DI				0x72
#define	PACKET3_SET_CONTEXT_REG_INDIRECT		0x73
#define	PACKET3_SET_SH_REG_DI_MULTI			0x74
#define	PACKET3_GFX_PIPE_LOCK				0x75
#define	PACKET3_SET_SH_REG				0x76
#define		PACKET3_SET_SH_REG_START			0x00002c00
#define		PACKET3_SET_SH_REG_END				0x00003000
#define 	PACKET3_SET_SH_REG__REG_OFFSET(x) ((((unsigned)(x)) & 0xFFFF) << 0)
#define 	PACKET3_SET_SH_REG__VMID_SHIFT(x) ((((unsigned)(x)) & 0x1F) << 23)
#define 	PACKET3_SET_SH_REG__INDEX(x) ((((unsigned)(x)) & 0xF) << 28)
#define 	PACKET3_SET_SH_REG__INDEX__DEFAULT 0
#define 	PACKET3_SET_SH_REG__INDEX__INSERT_VMID 1
#define	PACKET3_SET_SH_REG_OFFSET			0x77
#define	PACKET3_SET_QUEUE_REG				0x78
#define	PACKET3_SET_UCONFIG_REG				0x79
#define		PACKET3_SET_UCONFIG_REG_START			0x0000c000
#define		PACKET3_SET_UCONFIG_REG_END			0x0000c400
#define 	PACKET3_SET_UCONFIG_REG__REG_OFFSET(x) ((((unsigned)(x)) & 0xFFFF) << 0)
#define	PACKET3_SET_UCONFIG_REG_INDEX			0x7A
#define	PACKET3_FORWARD_HEADER				0x7C
#define	PACKET3_SCRATCH_RAM_WRITE			0x7D
#define	PACKET3_SCRATCH_RAM_READ			0x7E
#define	PACKET3_LOAD_CONST_RAM				0x80
#define	PACKET3_WRITE_CONST_RAM				0x81
#define	PACKET3_DUMP_CONST_RAM				0x83
#define	PACKET3_INCREMENT_CE_COUNTER			0x84
#define	PACKET3_INCREMENT_DE_COUNTER			0x85
#define	PACKET3_WAIT_ON_CE_COUNTER			0x86
#define	PACKET3_WAIT_ON_DE_COUNTER_DIFF			0x88
#define	PACKET3_SWITCH_BUFFER				0x8B
#define	PACKET3_DISPATCH_DRAW_PREAMBLE			0x8C
#define	PACKET3_DISPATCH_DRAW_PREAMBLE_ACE		0x8C
#define	PACKET3_DISPATCH_DRAW				0x8D
#define	PACKET3_DISPATCH_DRAW_ACE			0x8D
#define	PACKET3_GET_LOD_STATS				0x8E
#define	PACKET3_DRAW_MULTI_PREAMBLE			0x8F
#define	PACKET3_FRAME_CONTROL				0x90
#			define FRAME_TMZ	(1 << 0)
#			define FRAME_CMD(x) ((x) << 28)
			/*
			 * x=0: tmz_begin
			 * x=1: tmz_end
			 */
#define	PACKET3_INDEX_ATTRIBUTES_INDIRECT		0x91
#define	PACKET3_WAIT_REG_MEM64				0x93
#define	PACKET3_COND_PREEMPT				0x94
#define	PACKET3_HDP_FLUSH				0x95
#define	PACKET3_COPY_DATA_RB				0x96
#define	PACKET3_INVALIDATE_TLBS				0x98
#              define PACKET3_INVALIDATE_TLBS_DST_SEL(x)     ((x) << 0)
#              define PACKET3_INVALIDATE_TLBS_ALL_HUB(x)     ((x) << 4)
#              define PACKET3_INVALIDATE_TLBS_PASID(x)       ((x) << 5)
#              define PACKET3_INVALIDATE_TLBS_FLUSH_TYPE(x)  ((x) << 29)
#define	PACKET3_AQL_PACKET				0x99
#define	PACKET3_DMA_DATA_FILL_MULTI			0x9A
#define	PACKET3_SET_SH_REG_INDEX			0x9B
#define	PACKET3_DRAW_INDIRECT_COUNT_MULTI		0x9C
#define	PACKET3_DRAW_INDEX_INDIRECT_COUNT_MULTI		0x9D
#define	PACKET3_DUMP_CONST_RAM_OFFSET			0x9E
#define	PACKET3_LOAD_CONTEXT_REG_INDEX			0x9F
#define	PACKET3_SET_RESOURCES				0xA0
/* 1. header
 * 2. CONTROL
 * 3. QUEUE_MASK_LO [31:0]
 * 4. QUEUE_MASK_HI [31:0]
 * 5. GWS_MASK_LO [31:0]
 * 6. GWS_MASK_HI [31:0]
 * 7. OAC_MASK [15:0]
 * 8. GDS_HEAP_SIZE [16:11] | GDS_HEAP_BASE [5:0]
 */
#              define PACKET3_SET_RESOURCES_VMID_MASK(x)     ((x) << 0)
#              define PACKET3_SET_RESOURCES_UNMAP_LATENTY(x) ((x) << 16)
#              define PACKET3_SET_RESOURCES_QUEUE_TYPE(x)    ((x) << 29)
#define PACKET3_MAP_PROCESS				0xA1
#define PACKET3_MAP_QUEUES				0xA2
/* 1. header
 * 2. CONTROL
 * 3. CONTROL2
 * 4. MQD_ADDR_LO [31:0]
 * 5. MQD_ADDR_HI [31:0]
 * 6. WPTR_ADDR_LO [31:0]
 * 7. WPTR_ADDR_HI [31:0]
 */
/* CONTROL */
#              define PACKET3_MAP_QUEUES_QUEUE_SEL(x)       ((x) << 4)
#              define PACKET3_MAP_QUEUES_VMID(x)            ((x) << 8)
#              define PACKET3_MAP_QUEUES_QUEUE(x)           ((x) << 13)
#              define PACKET3_MAP_QUEUES_PIPE(x)            ((x) << 16)
#              define PACKET3_MAP_QUEUES_ME(x)              ((x) << 18)
#              define PACKET3_MAP_QUEUES_QUEUE_TYPE(x)      ((x) << 21)
#              define PACKET3_MAP_QUEUES_ALLOC_FORMAT(x)    ((x) << 24)
#              define PACKET3_MAP_QUEUES_ENGINE_SEL(x)      ((x) << 26)
#              define PACKET3_MAP_QUEUES_NUM_QUEUES(x)      ((x) << 29)
/* CONTROL2 */
#              define PACKET3_MAP_QUEUES_CHECK_DISABLE(x)   ((x) << 1)
#              define PACKET3_MAP_QUEUES_DOORBELL_OFFSET(x) ((x) << 2)
#define	PACKET3_UNMAP_QUEUES				0xA3
/* 1. header
 * 2. CONTROL
 * 3. CONTROL2
 * 4. CONTROL3
 * 5. CONTROL4
 * 6. CONTROL5
 */
/* CONTROL */
#              define PACKET3_UNMAP_QUEUES_ACTION(x)           ((x) << 0)
		/* 0 - PREEMPT_QUEUES
		 * 1 - RESET_QUEUES
		 * 2 - DISABLE_PROCESS_QUEUES
		 * 3 - PREEMPT_QUEUES_NO_UNMAP
		 */
#              define PACKET3_UNMAP_QUEUES_QUEUE_SEL(x)        ((x) << 4)
#              define PACKET3_UNMAP_QUEUES_ENGINE_SEL(x)       ((x) << 26)
#              define PACKET3_UNMAP_QUEUES_NUM_QUEUES(x)       ((x) << 29)
/* CONTROL2a */
#              define PACKET3_UNMAP_QUEUES_PASID(x)            ((x) << 0)
/* CONTROL2b */
#              define PACKET3_UNMAP_QUEUES_DOORBELL_OFFSET0(x) ((x) << 2)
/* CONTROL3a */
#              define PACKET3_UNMAP_QUEUES_DOORBELL_OFFSET1(x) ((x) << 2)
/* CONTROL3b */
#              define PACKET3_UNMAP_QUEUES_RB_WPTR(x)          ((x) << 0)
/* CONTROL4 */
#              define PACKET3_UNMAP_QUEUES_DOORBELL_OFFSET2(x) ((x) << 2)
/* CONTROL5 */
#              define PACKET3_UNMAP_QUEUES_DOORBELL_OFFSET3(x) ((x) << 2)
#define	PACKET3_QUERY_STATUS				0xA4
/* 1. header
 * 2. CONTROL
 * 3. CONTROL2
 * 4. ADDR_LO [31:0]
 * 5. ADDR_HI [31:0]
 * 6. DATA_LO [31:0]
 * 7. DATA_HI [31:0]
 */
/* CONTROL */
#              define PACKET3_QUERY_STATUS_CONTEXT_ID(x)       ((x) << 0)
#              define PACKET3_QUERY_STATUS_INTERRUPT_SEL(x)    ((x) << 28)
#              define PACKET3_QUERY_STATUS_COMMAND(x)          ((x) << 30)
/* CONTROL2a */
#              define PACKET3_QUERY_STATUS_PASID(x)            ((x) << 0)
/* CONTROL2b */
#              define PACKET3_QUERY_STATUS_DOORBELL_OFFSET(x)  ((x) << 2)
#              define PACKET3_QUERY_STATUS_ENG_SEL(x)          ((x) << 25)
#define	PACKET3_RUN_LIST				0xA5
#define	PACKET3_MAP_PROCESS_VM				0xA6

#define PACKET3_RUN_CLEANER_SHADER                      0xD2
/* 1. header
 * 2. RESERVED [31:0]
 */

/* GFX11 */
#define	PACKET3_SET_Q_PREEMPTION_MODE			0xF0
#              define PACKET3_SET_Q_PREEMPTION_MODE_IB_VMID(x)  ((x) << 0)
#              define PACKET3_SET_Q_PREEMPTION_MODE_INIT_SHADOW_MEM    (1 << 0)

#endif
