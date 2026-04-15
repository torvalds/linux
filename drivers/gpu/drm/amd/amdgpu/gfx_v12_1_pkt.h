/*
 * Copyright 2025 Advanced Micro Devices, Inc.
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

#ifndef GFX_V12_1_PKT_H
#define GFX_V12_1_PKT_H

/**
 * PM4 definitions
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
#define	PACKET3_CLEAR_STATE				0x12
#define	PACKET3_INDEX_BUFFER_SIZE			0x13
#define	PACKET3_DISPATCH_DIRECT				0x15
#define	PACKET3_DISPATCH_INDIRECT			0x16
#define	PACKET3_ATOMIC_MEM				0x1E
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
#define	PACKET3_DRAW_INDIRECT_MULTI			0x2C
#define	PACKET3_DRAW_INDEX_AUTO				0x2D
#define	PACKET3_NUM_INSTANCES				0x2F
#define	PACKET3_DRAW_INDEX_MULTI_AUTO			0x30
#define	PACKET3_DRAW_INDEX_OFFSET_2			0x35
#define	PACKET3_WRITE_DATA				0x37
#define		WRITE_DATA_DST_SEL(x)                   (((x) & 0xf) << 8)
		/* 0 - register
		 * 1 - reserved
		 * 2 - tc_l2
		 * 3 - reserved
		 * 4 - reserved
		 * 5 - memory (same as tc_l2)
         * 6 - memory_mapped_adc_persistent_state
		 */
#define		WRITE_DATA_SCOPE(x)                     (((x) & 0x3) << 12)
#define		WRITE_DATA_MODE(x)                      (((x) & 0x3) << 14)
        /* 0 - local xcd
         * 1 - remote/local aid
         * 2 - remote xcd
         * 3 - remote mid
         */
#define		WRITE_DATA_ADDR_INCR                    (1 << 16)
#define		WRITE_DATA_MID_DIE_ID(x)                (((x) & 0x3) << 18)
#define		WR_CONFIRM                              (1 << 20)
#define		WRITE_DATA_XCD_DIE_ID(x)                (((x) & 0xf) << 21)
#define		WRITE_DATA_TEMPORAL(x)                  (((x) & 0x3) << 25)
		/* 0 - rt
		 * 1 - nt
         * 2 - ht
         * 3 - lu
		 */
#define		WRITE_DATA_COOP_DISABLE                 (1 << 27)
#define	PACKET3_DRAW_INDEX_INDIRECT_MULTI		0x38
#define	PACKET3_WAIT_REG_MEM				0x3C
#define		WAIT_REG_MEM_FUNCTION(x)                (((x) & 0x7) << 0)
		/* 0 - always
		 * 1 - <
		 * 2 - <=
		 * 3 - ==
		 * 4 - !=
		 * 5 - >=
		 * 6 - >
		 */
#define		WAIT_REG_MEM_MEM_SPACE(x)               (((x) & 0x3) << 4)
		/* 0 - reg
		 * 1 - mem
		 */
#define		WAIT_REG_MEM_OPERATION(x)               (((x) & 0x3) << 6)
		/* 0 - wait_reg_mem
		 * 1 - wr_wait_wr_reg
		 */
#define		WAIT_REG_MEM_MODE(x)                    (((x) & 0x3) << 10)
        /* 0 - local xcd
         * 1 - remote/local aid
         * 2 - remote xcd
         * 3 - remote mid
         */
#define		WAIT_REG_MEM_MID_DIE_ID(x)              (((x) & 0x3) << 12)
#define		WAIT_REG_MEM_XCD_DIE_ID(x)              (((x) & 0xf) << 14)
#define		WAIT_REG_MEM_MES_INTR_PIPE(x)           (((x) & 0x3) << 22)
#define		WAIT_REG_MEM_MES_ACTION(x)              (((x) & 0x1) << 24)
#define		WAIT_REG_MEM_TEMPORAL(x)                (((x) & 0x3) << 25)
		/* 0 - rt
		 * 1 - nt
         * 2 - ht
         * 3 - lu
		 */
#define	PACKET3_INDIRECT_BUFFER				0x3F
#define		INDIRECT_BUFFER_VALID                   (1 << 23)
#define		INDIRECT_BUFFER_TEMPORAL(x)             (x) << 28)
		/* 0 - rt
		 * 1 - nt
		 * 2 - ht
         * 3 - lu
		 */
#define	PACKET3_COND_INDIRECT_BUFFER			0x3F
#define	PACKET3_COPY_DATA				0x40
#define		COPY_DATA_SRC_SEL(x)                    (((x) & 0xf) << 0)
#define		COPY_DATA_DST_SEL(x)                    (((x) & 0xf) << 8)
#define		COPY_DATA_SRC_SCOPE(x)                  (((x) & 0x3) << 4)
#define		COPY_DATA_DST_SCOPE(x)                  (((x) & 0x3) << 27)
#define		COPY_DATA_MODE(x)                       (((x) & 0x3) << 6)
        /* 0 - local xcd
         * 1 - remote/local aid
         * 2 - remote xcd
         * 3 - remote mid
         */
#define		COPY_DATA_SRC_TEMPORAL(x)               (((x) & 0x3) << 13)
#define		COPY_DATA_DST_TEMPORAL(x)               (((x) & 0x3) << 25)
		/* 0 - rt
		 * 1 - nt
         * 2 - ht
         * 3 - lu
		 */
#define		COPY_DATA_COUNT_SEL                     (1 << 16)
#define		COPY_DATA_SRC_DST_REMOTE_MODE(x)        (((x)) & 0x1 << 16)
        /* 0 - src remote
         * 1 - dst remote
         */
#define		COPY_DATA_MID_DIE_ID(x)                 (((x) & 0x3) << 18)
#define		COPY_DATA_XCD_DIE_ID(x)                 (((x) & 0xf) << 21)
#define		COPY_DATA_PQ_EXE_STATUS                 (1 << 27)
#define	PACKET3_PFP_SYNC_ME				0x42
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
#define	PACKET3_RELEASE_MEM				0x49
#define		PACKET3_RELEASE_MEM_EVENT_TYPE(x)	    ((x) << 0)
#define		PACKET3_RELEASE_MEM_EVENT_INDEX(x)	    ((x) << 8)
#define		PACKET3_RELEASE_MEM_GCR_GL2_SCOPE(x)	((x) << 12)
#define		PACKET3_RELEASE_MEM_GCR_GLV_INV		    (1 << 14)
#define		PACKET3_RELEASE_MEM_GCR_GL2_US		    (1 << 16)
#define		PACKET3_RELEASE_MEM_GCR_GL2_RANGE(x)	((x) << 17)
#define		PACKET3_RELEASE_MEM_GCR_GL2_DISCARD	    (1 << 19)
#define		PACKET3_RELEASE_MEM_GCR_GL2_INV		    (1 << 20)
#define		PACKET3_RELEASE_MEM_GCR_GL2_WB		    (1 << 21)
#define		PACKET3_RELEASE_MEM_GCR_SEQ(x)		    ((x) << 22)
#define		PACKET3_RELEASE_MEM_GCR_GLV_WB		    (1 << 24)
#define		PACKET3_RELEASE_MEM_TEMPORAL(x)	        ((x) << 25)
		/* 0 - temporal__release_mem__rt
		 * 1 - temporal__release_mem__nt
		 * 2 - temporal__release_mem__ht
		 * 3 - temporal__release_mem__lu
		 */
#define		PACKET3_RELEASE_MEM_PQ_EXE_STATUS		(1 << 28)
#define		PACKET3_RELEASE_MEM_GCR_GLK_INV		    (1 << 30)

#define		PACKET3_RELEASE_MEM_DST_SEL(x)		((x) << 16)
		/* 0 - memory controller
		 * 1 - TC/L2
         * 2 - register
		 */
#define		PACKET3_RELEASE_MEM_MES_INTR_PIPE(x)    ((x) << 20)
#define		PACKET3_RELEASE_MEM_MES_ACTION_ID(x)    ((x) << 22)
#define		PACKET3_RELEASE_MEM_INT_SEL(x)		((x) << 24)
		/* 0 - none
		 * 1 - interrupt only (DATA_SEL = 0)
		 * 2 - interrupt when data write is confirmed
		 */
#define		PACKET3_RELEASE_MEM_ADD_DOOREBLL_OFFSET(x)		(1 << 28)
#define		PACKET3_RELEASE_MEM_DATA_SEL(x)		((x) << 29)
		/* 0 - discard
		 * 1 - send low 32bit data
		 * 2 - send 64bit data
		 * 3 - send 64bit GPU counter value
		 * 4 - send 64bit sys counter value
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
#              define PACKET3_DMA_DATA_SRC_TEMPORAL(x)  ((x) << 13)
		/* 0 - rt
		 * 1 - nt
         * 2 - ht
         * 3 - lu
		 */
#              define PACKET3_DMA_DATA_SRC_SCOPE(x)     ((x) << 15)
#              define PACKET3_DMA_DATA_DST_SEL(x)       ((x) << 20)
		/* 0 - DST_ADDR using DAS
		 * 1 - GDS
		 * 3 - DST_ADDR using L2
		 */
#              define PACKET3_DMA_DATA_DST_TEMPORAL(x)  ((x) << 25)
		/* 0 - LRU
		 * 1 - Stream
		 */
#              define PACKET3_DMA_DATA_DST_SCOPE(x)     ((x) << 27)
#              define PACKET3_DMA_DATA_SRC_SEL(x)       ((x) << 29)
		/* 0 - SRC_ADDR using SAS
		 * 1 - GDS
		 * 2 - DATA
		 * 3 - SRC_ADDR using L2
		 */
/* COMMAND */
#              define PACKET3_DMA_DATA_CMD_SAS     (1 << 26)
		/* 0 - memory
		 * 1 - register
		 */
#              define PACKET3_DMA_DATA_CMD_DAS     (1 << 27)
		/* 0 - memory
		 * 1 - register
		 */
#              define PACKET3_DMA_DATA_CMD_SAIC     (1 << 28)
#              define PACKET3_DMA_DATA_CMD_DAIC     (1 << 29)
#              define PACKET3_DMA_DATA_CMD_RAW_WAIT (1 << 30)
#              define PACKET3_DMA_DATA_CMD_DIS_WC   (1 << 30)
#define	PACKET3_CONTEXT_REG_RMW				0x51
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
#define 	PACKET3_ACQUIRE_MEM_GCR_CNTL_GL2_SCOPE(x) ((x) << 4)
        /*
         * 0:Device scope
         * 1:System scope
         * 2:Force INV/WB all
         * 3:Reserved
         */
#define 	PACKET3_ACQUIRE_MEM_GCR_CNTL_GLV_WB(x) ((x) << 6)
#define 	PACKET3_ACQUIRE_MEM_GCR_CNTL_GLK_INV(x) ((x) << 7)
#define 	PACKET3_ACQUIRE_MEM_GCR_CNTL_GLV_INV(x) ((x) << 8)
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
#define	PACKET3_GEN_PDEPTE				0x5B
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
#define	PACKET3_SET_SH_REG				0x76
#define		PACKET3_SET_SH_REG_START			0x00002c00
#define		PACKET3_SET_SH_REG_END				0x00003000
#define	PACKET3_SET_SH_REG_OFFSET			0x77
#define	PACKET3_SET_QUEUE_REG				0x78
#define	PACKET3_SET_UCONFIG_REG				0x79
#define		PACKET3_SET_UCONFIG_REG_START			0x0000c000
#define		PACKET3_SET_UCONFIG_REG_END			0x0000c400
#define	PACKET3_SET_UCONFIG_REG_INDEX			0x7A
#define	PACKET3_DISPATCH_DRAW_PREAMBLE			0x8C
#define	PACKET3_DISPATCH_DRAW				0x8D
#define	PACKET3_INDEX_ATTRIBUTES_INDIRECT		0x91
#define	PACKET3_WAIT_REG_MEM64				0x93
#define	PACKET3_HDP_FLUSH				0x95
#define	PACKET3_INVALIDATE_TLBS				0x98
#define PACKET3_INVALIDATE_TLBS_DST_SEL(x)     ((x) << 0)
#define PACKET3_INVALIDATE_TLBS_ALL_HUB(x)     ((x) << 4)
#define PACKET3_INVALIDATE_TLBS_PASID(x)       ((x) << 5)
#define PACKET3_INVALIDATE_TLBS_FLUSH_TYPE(x)  ((x) << 29)

#define	PACKET3_DMA_DATA_FILL_MULTI			0x9A
#define	PACKET3_SET_SH_REG_INDEX			0x9B
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
#              define PACKET3_MAP_QUEUES_QUEUE_GROUP(x)     ((x) << 24)
#              define PACKET3_MAP_QUEUES_ENGINE_SEL(x)      ((x) << 26)
#              define PACKET3_MAP_QUEUES_NUM_QUEUES(x)      ((x) << 29)
/* CONTROL2 */
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
#              define PACKET3_QUERY_STATUS_ENG_SEL(x)          ((x) << 28)

#endif
