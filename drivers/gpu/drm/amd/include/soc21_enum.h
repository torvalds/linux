/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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
#if !defined (_soc21_ENUM_HEADER)
#define _soc21_ENUM_HEADER

#ifndef _DRIVER_BUILD
#ifndef GL_ZERO
#define GL__ZERO                      BLEND_ZERO
#define GL__ONE                       BLEND_ONE
#define GL__SRC_COLOR                 BLEND_SRC_COLOR
#define GL__ONE_MINUS_SRC_COLOR       BLEND_ONE_MINUS_SRC_COLOR
#define GL__DST_COLOR                 BLEND_DST_COLOR
#define GL__ONE_MINUS_DST_COLOR       BLEND_ONE_MINUS_DST_COLOR
#define GL__SRC_ALPHA                 BLEND_SRC_ALPHA
#define GL__ONE_MINUS_SRC_ALPHA       BLEND_ONE_MINUS_SRC_ALPHA
#define GL__DST_ALPHA                 BLEND_DST_ALPHA
#define GL__ONE_MINUS_DST_ALPHA       BLEND_ONE_MINUS_DST_ALPHA
#define GL__SRC_ALPHA_SATURATE        BLEND_SRC_ALPHA_SATURATE
#define GL__CONSTANT_COLOR            BLEND_CONSTANT_COLOR
#define GL__ONE_MINUS_CONSTANT_COLOR  BLEND_ONE_MINUS_CONSTANT_COLOR
#define GL__CONSTANT_ALPHA            BLEND_CONSTANT_ALPHA
#define GL__ONE_MINUS_CONSTANT_ALPHA  BLEND_ONE_MINUS_CONSTANT_ALPHA
#endif
#endif

/*******************************************************
 * Chip Enums
 *******************************************************/

/*
 * DSM_DATA_SEL enum
 */

typedef enum DSM_DATA_SEL {
DSM_DATA_SEL_DISABLE                     = 0x00000000,
DSM_DATA_SEL_0                           = 0x00000001,
DSM_DATA_SEL_1                           = 0x00000002,
DSM_DATA_SEL_BOTH                        = 0x00000003,
} DSM_DATA_SEL;

/*
 * DSM_ENABLE_ERROR_INJECT enum
 */

typedef enum DSM_ENABLE_ERROR_INJECT {
DSM_ENABLE_ERROR_INJECT_FED_IN           = 0x00000000,
DSM_ENABLE_ERROR_INJECT_SINGLE           = 0x00000001,
DSM_ENABLE_ERROR_INJECT_UNCORRECTABLE    = 0x00000002,
DSM_ENABLE_ERROR_INJECT_UNCORRECTABLE_LIMITED = 0x00000003,
} DSM_ENABLE_ERROR_INJECT;

/*
 * DSM_SELECT_INJECT_DELAY enum
 */

typedef enum DSM_SELECT_INJECT_DELAY {
DSM_SELECT_INJECT_DELAY_NO_DELAY         = 0x00000000,
DSM_SELECT_INJECT_DELAY_DELAY_ERROR      = 0x00000001,
} DSM_SELECT_INJECT_DELAY;

/*
 * DSM_SINGLE_WRITE enum
 */

typedef enum DSM_SINGLE_WRITE {
DSM_SINGLE_WRITE_DIS                     = 0x00000000,
DSM_SINGLE_WRITE_EN                      = 0x00000001,
} DSM_SINGLE_WRITE;

/*
 * ENUM_NUM_SIMD_PER_CU enum
 */

typedef enum ENUM_NUM_SIMD_PER_CU {
NUM_SIMD_PER_CU                          = 0x00000004,
} ENUM_NUM_SIMD_PER_CU;

/*
 * GATCL1RequestType enum
 */

typedef enum GATCL1RequestType {
GATCL1_TYPE_NORMAL                       = 0x00000000,
GATCL1_TYPE_SHOOTDOWN                    = 0x00000001,
GATCL1_TYPE_BYPASS                       = 0x00000002,
} GATCL1RequestType;

/*
 * GL0V_CACHE_POLICIES enum
 */

typedef enum GL0V_CACHE_POLICIES {
GL0V_CACHE_POLICY_MISS_LRU               = 0x00000000,
GL0V_CACHE_POLICY_MISS_EVICT             = 0x00000001,
GL0V_CACHE_POLICY_HIT_LRU                = 0x00000002,
GL0V_CACHE_POLICY_HIT_EVICT              = 0x00000003,
} GL0V_CACHE_POLICIES;

/*
 * GL1_CACHE_POLICIES enum
 */

typedef enum GL1_CACHE_POLICIES {
GL1_CACHE_POLICY_MISS_LRU                = 0x00000000,
GL1_CACHE_POLICY_MISS_EVICT              = 0x00000001,
GL1_CACHE_POLICY_HIT_LRU                 = 0x00000002,
GL1_CACHE_POLICY_HIT_EVICT               = 0x00000003,
} GL1_CACHE_POLICIES;

/*
 * GL1_CACHE_STORE_POLICIES enum
 */

typedef enum GL1_CACHE_STORE_POLICIES {
GL1_CACHE_STORE_POLICY_BYPASS            = 0x00000000,
} GL1_CACHE_STORE_POLICIES;

/*
 * GL2_CACHE_POLICIES enum
 */

typedef enum GL2_CACHE_POLICIES {
GL2_CACHE_POLICY_LRU                     = 0x00000000,
GL2_CACHE_POLICY_STREAM                  = 0x00000001,
GL2_CACHE_POLICY_NOA                     = 0x00000002,
GL2_CACHE_POLICY_BYPASS                  = 0x00000003,
} GL2_CACHE_POLICIES;

/*
 * Hdp_SurfaceEndian enum
 */

typedef enum Hdp_SurfaceEndian {
HDP_ENDIAN_NONE                          = 0x00000000,
HDP_ENDIAN_8IN16                         = 0x00000001,
HDP_ENDIAN_8IN32                         = 0x00000002,
HDP_ENDIAN_8IN64                         = 0x00000003,
} Hdp_SurfaceEndian;

/*
 * MTYPE enum
 */

typedef enum MTYPE {
MTYPE_C_RW_US                            = 0x00000000,
MTYPE_RESERVED_1                         = 0x00000001,
MTYPE_C_RO_S                             = 0x00000002,
MTYPE_UC                                 = 0x00000003,
MTYPE_C_RW_S                             = 0x00000004,
MTYPE_RESERVED_5                         = 0x00000005,
MTYPE_C_RO_US                            = 0x00000006,
MTYPE_RESERVED_7                         = 0x00000007,
} MTYPE;

/*
 * PERFMON_COUNTER_MODE enum
 */

typedef enum PERFMON_COUNTER_MODE {
PERFMON_COUNTER_MODE_ACCUM               = 0x00000000,
PERFMON_COUNTER_MODE_ACTIVE_CYCLES       = 0x00000001,
PERFMON_COUNTER_MODE_MAX                 = 0x00000002,
PERFMON_COUNTER_MODE_DIRTY               = 0x00000003,
PERFMON_COUNTER_MODE_SAMPLE              = 0x00000004,
PERFMON_COUNTER_MODE_CYCLES_SINCE_FIRST_EVENT = 0x00000005,
PERFMON_COUNTER_MODE_CYCLES_SINCE_LAST_EVENT = 0x00000006,
PERFMON_COUNTER_MODE_CYCLES_GE_HI        = 0x00000007,
PERFMON_COUNTER_MODE_CYCLES_EQ_HI        = 0x00000008,
PERFMON_COUNTER_MODE_INACTIVE_CYCLES     = 0x00000009,
PERFMON_COUNTER_MODE_RESERVED            = 0x0000000f,
} PERFMON_COUNTER_MODE;

/*
 * PERFMON_SPM_MODE enum
 */

typedef enum PERFMON_SPM_MODE {
PERFMON_SPM_MODE_OFF                     = 0x00000000,
PERFMON_SPM_MODE_16BIT_CLAMP             = 0x00000001,
PERFMON_SPM_MODE_16BIT_NO_CLAMP          = 0x00000002,
PERFMON_SPM_MODE_32BIT_CLAMP             = 0x00000003,
PERFMON_SPM_MODE_32BIT_NO_CLAMP          = 0x00000004,
PERFMON_SPM_MODE_RESERVED_5              = 0x00000005,
PERFMON_SPM_MODE_RESERVED_6              = 0x00000006,
PERFMON_SPM_MODE_RESERVED_7              = 0x00000007,
PERFMON_SPM_MODE_TEST_MODE_0             = 0x00000008,
PERFMON_SPM_MODE_TEST_MODE_1             = 0x00000009,
PERFMON_SPM_MODE_TEST_MODE_2             = 0x0000000a,
} PERFMON_SPM_MODE;

/*
 * RMI_CID enum
 */

typedef enum RMI_CID {
RMI_CID_CC                               = 0x00000000,
RMI_CID_FC                               = 0x00000001,
RMI_CID_CM                               = 0x00000002,
RMI_CID_DC                               = 0x00000003,
RMI_CID_Z                                = 0x00000004,
RMI_CID_S                                = 0x00000005,
RMI_CID_TILE                             = 0x00000006,
RMI_CID_ZPCPSD                           = 0x00000007,
} RMI_CID;

/*
 * ReadPolicy enum
 */

typedef enum ReadPolicy {
CACHE_LRU_RD                             = 0x00000000,
CACHE_STREAM_RD                          = 0x00000001,
CACHE_NOA                                = 0x00000002,
RESERVED_RDPOLICY                        = 0x00000003,
} ReadPolicy;

/*
 * SDMA_PERFMON_SEL enum
 */

typedef enum SDMA_PERFMON_SEL {
SDMA_PERFMON_SEL_CYCLE                   = 0x00000000,
SDMA_PERFMON_SEL_IDLE                    = 0x00000001,
SDMA_PERFMON_SEL_REG_IDLE                = 0x00000002,
SDMA_PERFMON_SEL_RB_EMPTY                = 0x00000003,
SDMA_PERFMON_SEL_RB_FULL                 = 0x00000004,
SDMA_PERFMON_SEL_RB_WPTR_WRAP            = 0x00000005,
SDMA_PERFMON_SEL_RB_RPTR_WRAP            = 0x00000006,
SDMA_PERFMON_SEL_RB_WPTR_POLL_READ       = 0x00000007,
SDMA_PERFMON_SEL_RB_RPTR_WB              = 0x00000008,
SDMA_PERFMON_SEL_RB_CMD_IDLE             = 0x00000009,
SDMA_PERFMON_SEL_RB_CMD_FULL             = 0x0000000a,
SDMA_PERFMON_SEL_IB_CMD_IDLE             = 0x0000000b,
SDMA_PERFMON_SEL_IB_CMD_FULL             = 0x0000000c,
SDMA_PERFMON_SEL_EX_IDLE                 = 0x0000000d,
SDMA_PERFMON_SEL_SRBM_REG_SEND           = 0x0000000e,
SDMA_PERFMON_SEL_EX_IDLE_POLL_TIMER_EXPIRE = 0x0000000f,
SDMA_PERFMON_SEL_WR_BA_RTR               = 0x00000010,
SDMA_PERFMON_SEL_MC_WR_IDLE              = 0x00000011,
SDMA_PERFMON_SEL_MC_WR_COUNT             = 0x00000012,
SDMA_PERFMON_SEL_RD_BA_RTR               = 0x00000013,
SDMA_PERFMON_SEL_MC_RD_IDLE              = 0x00000014,
SDMA_PERFMON_SEL_MC_RD_COUNT             = 0x00000015,
SDMA_PERFMON_SEL_MC_RD_RET_STALL         = 0x00000016,
SDMA_PERFMON_SEL_MC_RD_NO_POLL_IDLE      = 0x00000017,
SDMA_PERFMON_SEL_SEM_IDLE                = 0x0000001a,
SDMA_PERFMON_SEL_SEM_REQ_STALL           = 0x0000001b,
SDMA_PERFMON_SEL_SEM_REQ_COUNT           = 0x0000001c,
SDMA_PERFMON_SEL_SEM_RESP_INCOMPLETE     = 0x0000001d,
SDMA_PERFMON_SEL_SEM_RESP_FAIL           = 0x0000001e,
SDMA_PERFMON_SEL_SEM_RESP_PASS           = 0x0000001f,
SDMA_PERFMON_SEL_INT_IDLE                = 0x00000020,
SDMA_PERFMON_SEL_INT_REQ_STALL           = 0x00000021,
SDMA_PERFMON_SEL_INT_REQ_COUNT           = 0x00000022,
SDMA_PERFMON_SEL_INT_RESP_ACCEPTED       = 0x00000023,
SDMA_PERFMON_SEL_INT_RESP_RETRY          = 0x00000024,
SDMA_PERFMON_SEL_NUM_PACKET              = 0x00000025,
SDMA_PERFMON_SEL_CE_WREQ_IDLE            = 0x00000027,
SDMA_PERFMON_SEL_CE_WR_IDLE              = 0x00000028,
SDMA_PERFMON_SEL_CE_SPLIT_IDLE           = 0x00000029,
SDMA_PERFMON_SEL_CE_RREQ_IDLE            = 0x0000002a,
SDMA_PERFMON_SEL_CE_OUT_IDLE             = 0x0000002b,
SDMA_PERFMON_SEL_CE_IN_IDLE              = 0x0000002c,
SDMA_PERFMON_SEL_CE_DST_IDLE             = 0x0000002d,
SDMA_PERFMON_SEL_CE_AFIFO_FULL           = 0x00000030,
SDMA_PERFMON_SEL_CE_INFO_FULL            = 0x00000033,
SDMA_PERFMON_SEL_CE_INFO1_FULL           = 0x00000034,
SDMA_PERFMON_SEL_CE_RD_STALL             = 0x00000035,
SDMA_PERFMON_SEL_CE_WR_STALL             = 0x00000036,
SDMA_PERFMON_SEL_GFX_SELECT              = 0x00000037,
SDMA_PERFMON_SEL_RLC0_SELECT             = 0x00000038,
SDMA_PERFMON_SEL_RLC1_SELECT             = 0x00000039,
SDMA_PERFMON_SEL_PAGE_SELECT             = 0x0000003a,
SDMA_PERFMON_SEL_CTX_CHANGE              = 0x0000003b,
SDMA_PERFMON_SEL_CTX_CHANGE_EXPIRED      = 0x0000003c,
SDMA_PERFMON_SEL_CTX_CHANGE_EXCEPTION    = 0x0000003d,
SDMA_PERFMON_SEL_DOORBELL                = 0x0000003e,
SDMA_PERFMON_SEL_F32_L1_WR_VLD           = 0x0000003f,
SDMA_PERFMON_SEL_CE_L1_WR_VLD            = 0x00000040,
SDMA_PERFMON_SEL_CPF_SDMA_INVREQ         = 0x00000041,
SDMA_PERFMON_SEL_SDMA_CPF_INVACK         = 0x00000042,
SDMA_PERFMON_SEL_UTCL2_SDMA_INVREQ       = 0x00000043,
SDMA_PERFMON_SEL_SDMA_UTCL2_INVACK       = 0x00000044,
SDMA_PERFMON_SEL_UTCL2_SDMA_INVREQ_ALL   = 0x00000045,
SDMA_PERFMON_SEL_SDMA_UTCL2_INVACK_ALL   = 0x00000046,
SDMA_PERFMON_SEL_UTCL2_RET_XNACK         = 0x00000047,
SDMA_PERFMON_SEL_UTCL2_RET_ACK           = 0x00000048,
SDMA_PERFMON_SEL_UTCL2_FREE              = 0x00000049,
SDMA_PERFMON_SEL_SDMA_UTCL2_SEND         = 0x0000004a,
SDMA_PERFMON_SEL_DMA_L1_WR_SEND          = 0x0000004b,
SDMA_PERFMON_SEL_DMA_L1_RD_SEND          = 0x0000004c,
SDMA_PERFMON_SEL_DMA_MC_WR_SEND          = 0x0000004d,
SDMA_PERFMON_SEL_DMA_MC_RD_SEND          = 0x0000004e,
SDMA_PERFMON_SEL_GPUVM_INV_HIGH          = 0x0000004f,
SDMA_PERFMON_SEL_GPUVM_INV_LOW           = 0x00000050,
SDMA_PERFMON_SEL_L1_WRL2_IDLE            = 0x00000051,
SDMA_PERFMON_SEL_L1_RDL2_IDLE            = 0x00000052,
SDMA_PERFMON_SEL_L1_WRMC_IDLE            = 0x00000053,
SDMA_PERFMON_SEL_L1_RDMC_IDLE            = 0x00000054,
SDMA_PERFMON_SEL_L1_WR_INV_IDLE          = 0x00000055,
SDMA_PERFMON_SEL_L1_RD_INV_IDLE          = 0x00000056,
SDMA_PERFMON_SEL_META_L2_REQ_SEND        = 0x00000057,
SDMA_PERFMON_SEL_L2_META_RET_VLD         = 0x00000058,
SDMA_PERFMON_SEL_SDMA_UTCL2_RD_SEND      = 0x00000059,
SDMA_PERFMON_SEL_UTCL2_SDMA_RD_RTN       = 0x0000005a,
SDMA_PERFMON_SEL_SDMA_UTCL2_WR_SEND      = 0x0000005b,
SDMA_PERFMON_SEL_UTCL2_SDMA_WR_RTN       = 0x0000005c,
SDMA_PERFMON_SEL_META_REQ_SEND           = 0x0000005d,
SDMA_PERFMON_SEL_META_RTN_VLD            = 0x0000005e,
SDMA_PERFMON_SEL_TLBI_SEND               = 0x0000005f,
SDMA_PERFMON_SEL_TLBI_RTN                = 0x00000060,
SDMA_PERFMON_SEL_GCR_SEND                = 0x00000061,
SDMA_PERFMON_SEL_GCR_RTN                 = 0x00000062,
SDMA_PERFMON_SEL_UTCL1_TAG_DELAY_COUNTER = 0x00000063,
SDMA_PERFMON_SEL_MMHUB_TAG_DELAY_COUNTER = 0x00000064,
} SDMA_PERFMON_SEL;

/*
 * SDMA_PERF_SEL enum
 */

typedef enum SDMA_PERF_SEL {
SDMA_PERF_SEL_CYCLE                      = 0x00000000,
SDMA_PERF_SEL_IDLE                       = 0x00000001,
SDMA_PERF_SEL_REG_IDLE                   = 0x00000002,
SDMA_PERF_SEL_RB_EMPTY                   = 0x00000003,
SDMA_PERF_SEL_RB_FULL                    = 0x00000004,
SDMA_PERF_SEL_RB_WPTR_WRAP               = 0x00000005,
SDMA_PERF_SEL_RB_RPTR_WRAP               = 0x00000006,
SDMA_PERF_SEL_RB_WPTR_POLL_READ          = 0x00000007,
SDMA_PERF_SEL_RB_RPTR_WB                 = 0x00000008,
SDMA_PERF_SEL_RB_CMD_IDLE                = 0x00000009,
SDMA_PERF_SEL_RB_CMD_FULL                = 0x0000000a,
SDMA_PERF_SEL_IB_CMD_IDLE                = 0x0000000b,
SDMA_PERF_SEL_IB_CMD_FULL                = 0x0000000c,
SDMA_PERF_SEL_EX_IDLE                    = 0x0000000d,
SDMA_PERF_SEL_SRBM_REG_SEND              = 0x0000000e,
SDMA_PERF_SEL_EX_IDLE_POLL_TIMER_EXPIRE  = 0x0000000f,
SDMA_PERF_SEL_MC_WR_IDLE                 = 0x00000010,
SDMA_PERF_SEL_MC_WR_COUNT                = 0x00000011,
SDMA_PERF_SEL_MC_RD_IDLE                 = 0x00000012,
SDMA_PERF_SEL_MC_RD_COUNT                = 0x00000013,
SDMA_PERF_SEL_MC_RD_RET_STALL            = 0x00000014,
SDMA_PERF_SEL_MC_RD_NO_POLL_IDLE         = 0x00000015,
SDMA_PERF_SEL_SEM_IDLE                   = 0x00000018,
SDMA_PERF_SEL_SEM_REQ_STALL              = 0x00000019,
SDMA_PERF_SEL_SEM_REQ_COUNT              = 0x0000001a,
SDMA_PERF_SEL_SEM_RESP_INCOMPLETE        = 0x0000001b,
SDMA_PERF_SEL_SEM_RESP_FAIL              = 0x0000001c,
SDMA_PERF_SEL_SEM_RESP_PASS              = 0x0000001d,
SDMA_PERF_SEL_INT_IDLE                   = 0x0000001e,
SDMA_PERF_SEL_INT_REQ_STALL              = 0x0000001f,
SDMA_PERF_SEL_INT_REQ_COUNT              = 0x00000020,
SDMA_PERF_SEL_INT_RESP_ACCEPTED          = 0x00000021,
SDMA_PERF_SEL_INT_RESP_RETRY             = 0x00000022,
SDMA_PERF_SEL_NUM_PACKET                 = 0x00000023,
SDMA_PERF_SEL_CE_WREQ_IDLE               = 0x00000025,
SDMA_PERF_SEL_CE_WR_IDLE                 = 0x00000026,
SDMA_PERF_SEL_CE_SPLIT_IDLE              = 0x00000027,
SDMA_PERF_SEL_CE_RREQ_IDLE               = 0x00000028,
SDMA_PERF_SEL_CE_OUT_IDLE                = 0x00000029,
SDMA_PERF_SEL_CE_IN_IDLE                 = 0x0000002a,
SDMA_PERF_SEL_CE_DST_IDLE                = 0x0000002b,
SDMA_PERF_SEL_CE_AFIFO_FULL              = 0x0000002e,
SDMA_PERF_SEL_CE_INFO_FULL               = 0x00000031,
SDMA_PERF_SEL_CE_INFO1_FULL              = 0x00000032,
SDMA_PERF_SEL_CE_RD_STALL                = 0x00000033,
SDMA_PERF_SEL_CE_WR_STALL                = 0x00000034,
SDMA_PERF_SEL_GFX_SELECT                 = 0x00000035,
SDMA_PERF_SEL_RLC0_SELECT                = 0x00000036,
SDMA_PERF_SEL_RLC1_SELECT                = 0x00000037,
SDMA_PERF_SEL_PAGE_SELECT                = 0x00000038,
SDMA_PERF_SEL_CTX_CHANGE                 = 0x00000039,
SDMA_PERF_SEL_CTX_CHANGE_EXPIRED         = 0x0000003a,
SDMA_PERF_SEL_CTX_CHANGE_EXCEPTION       = 0x0000003b,
SDMA_PERF_SEL_DOORBELL                   = 0x0000003c,
SDMA_PERF_SEL_RD_BA_RTR                  = 0x0000003d,
SDMA_PERF_SEL_WR_BA_RTR                  = 0x0000003e,
SDMA_PERF_SEL_F32_L1_WR_VLD              = 0x0000003f,
SDMA_PERF_SEL_CE_L1_WR_VLD               = 0x00000040,
SDMA_PERF_SEL_CPF_SDMA_INVREQ            = 0x00000041,
SDMA_PERF_SEL_SDMA_CPF_INVACK            = 0x00000042,
SDMA_PERF_SEL_UTCL2_SDMA_INVREQ          = 0x00000043,
SDMA_PERF_SEL_SDMA_UTCL2_INVACK          = 0x00000044,
SDMA_PERF_SEL_UTCL2_SDMA_INVREQ_ALL      = 0x00000045,
SDMA_PERF_SEL_SDMA_UTCL2_INVACK_ALL      = 0x00000046,
SDMA_PERF_SEL_UTCL2_RET_XNACK            = 0x00000047,
SDMA_PERF_SEL_UTCL2_RET_ACK              = 0x00000048,
SDMA_PERF_SEL_UTCL2_FREE                 = 0x00000049,
SDMA_PERF_SEL_SDMA_UTCL2_SEND            = 0x0000004a,
SDMA_PERF_SEL_DMA_L1_WR_SEND             = 0x0000004b,
SDMA_PERF_SEL_DMA_L1_RD_SEND             = 0x0000004c,
SDMA_PERF_SEL_DMA_MC_WR_SEND             = 0x0000004d,
SDMA_PERF_SEL_DMA_MC_RD_SEND             = 0x0000004e,
SDMA_PERF_SEL_GPUVM_INV_HIGH             = 0x0000004f,
SDMA_PERF_SEL_GPUVM_INV_LOW              = 0x00000050,
SDMA_PERF_SEL_L1_WRL2_IDLE               = 0x00000051,
SDMA_PERF_SEL_L1_RDL2_IDLE               = 0x00000052,
SDMA_PERF_SEL_L1_WRMC_IDLE               = 0x00000053,
SDMA_PERF_SEL_L1_RDMC_IDLE               = 0x00000054,
SDMA_PERF_SEL_L1_WR_INV_IDLE             = 0x00000055,
SDMA_PERF_SEL_L1_RD_INV_IDLE             = 0x00000056,
SDMA_PERF_SEL_META_L2_REQ_SEND           = 0x00000057,
SDMA_PERF_SEL_L2_META_RET_VLD            = 0x00000058,
SDMA_PERF_SEL_SDMA_UTCL2_RD_SEND         = 0x00000059,
SDMA_PERF_SEL_UTCL2_SDMA_RD_RTN          = 0x0000005a,
SDMA_PERF_SEL_SDMA_UTCL2_WR_SEND         = 0x0000005b,
SDMA_PERF_SEL_UTCL2_SDMA_WR_RTN          = 0x0000005c,
SDMA_PERF_SEL_META_REQ_SEND              = 0x0000005d,
SDMA_PERF_SEL_META_RTN_VLD               = 0x0000005e,
SDMA_PERF_SEL_TLBI_SEND                  = 0x0000005f,
SDMA_PERF_SEL_TLBI_RTN                   = 0x00000060,
SDMA_PERF_SEL_GCR_SEND                   = 0x00000061,
SDMA_PERF_SEL_GCR_RTN                    = 0x00000062,
SDMA_PERF_SEL_CGCG_FENCE                 = 0x00000063,
SDMA_PERF_SEL_CE_CH_WR_REQ               = 0x00000064,
SDMA_PERF_SEL_CE_CH_WR_RET               = 0x00000065,
SDMA_PERF_SEL_F32_CH_WR_REQ              = 0x00000066,
SDMA_PERF_SEL_F32_CH_WR_RET              = 0x00000067,
SDMA_PERF_SEL_CE_OR_F32_CH_RD_REQ        = 0x00000068,
SDMA_PERF_SEL_CE_OR_F32_CH_RD_RET        = 0x00000069,
SDMA_PERF_SEL_RB_CH_RD_REQ               = 0x0000006a,
SDMA_PERF_SEL_RB_CH_RD_RET               = 0x0000006b,
SDMA_PERF_SEL_IB_CH_RD_REQ               = 0x0000006c,
SDMA_PERF_SEL_IB_CH_RD_RET               = 0x0000006d,
SDMA_PERF_SEL_WPTR_CH_RD_REQ             = 0x0000006e,
SDMA_PERF_SEL_WPTR_CH_RD_RET             = 0x0000006f,
SDMA_PERF_SEL_UTCL1_UTCL2_REQ            = 0x00000070,
SDMA_PERF_SEL_UTCL1_UTCL2_RET            = 0x00000071,
SDMA_PERF_SEL_CMD_OP_MATCH               = 0x00000072,
SDMA_PERF_SEL_CMD_OP_START               = 0x00000073,
SDMA_PERF_SEL_CMD_OP_END                 = 0x00000074,
SDMA_PERF_SEL_CE_BUSY                    = 0x00000075,
SDMA_PERF_SEL_CE_BUSY_START              = 0x00000076,
SDMA_PERF_SEL_CE_BUSY_END                = 0x00000077,
SDMA_PERF_SEL_F32_PERFCNT_TRIGGER        = 0x00000078,
SDMA_PERF_SEL_F32_PERFCNT_TRIGGER_START  = 0x00000079,
SDMA_PERF_SEL_F32_PERFCNT_TRIGGER_END    = 0x0000007a,
SDMA_PERF_SEL_CE_CH_WRREQ_SEND           = 0x0000007b,
SDMA_PERF_SEL_CH_CE_WRRET_VALID          = 0x0000007c,
SDMA_PERF_SEL_CE_CH_RDREQ_SEND           = 0x0000007d,
SDMA_PERF_SEL_CH_CE_RDRET_VALID          = 0x0000007e,
} SDMA_PERF_SEL;

/*
 * TCC_CACHE_POLICIES enum
 */

typedef enum TCC_CACHE_POLICIES {
TCC_CACHE_POLICY_LRU                     = 0x00000000,
TCC_CACHE_POLICY_STREAM                  = 0x00000001,
} TCC_CACHE_POLICIES;

/*
 * TCC_MTYPE enum
 */

typedef enum TCC_MTYPE {
MTYPE_NC                                 = 0x00000000,
MTYPE_WC                                 = 0x00000001,
MTYPE_CC                                 = 0x00000002,
} TCC_MTYPE;

/*
 * UTCL0FaultType enum
 */

typedef enum UTCL0FaultType {
UTCL0_XNACK_SUCCESS                      = 0x00000000,
UTCL0_XNACK_RETRY                        = 0x00000001,
UTCL0_XNACK_PRT                          = 0x00000002,
UTCL0_XNACK_NO_RETRY                     = 0x00000003,
} UTCL0FaultType;

/*
 * UTCL0RequestType enum
 */

typedef enum UTCL0RequestType {
UTCL0_TYPE_NORMAL                        = 0x00000000,
UTCL0_TYPE_SHOOTDOWN                     = 0x00000001,
UTCL0_TYPE_BYPASS                        = 0x00000002,
} UTCL0RequestType;

/*
 * UTCL1FaultType enum
 */

typedef enum UTCL1FaultType {
UTCL1_XNACK_SUCCESS                      = 0x00000000,
UTCL1_XNACK_RETRY                        = 0x00000001,
UTCL1_XNACK_PRT                          = 0x00000002,
UTCL1_XNACK_NO_RETRY                     = 0x00000003,
} UTCL1FaultType;

/*
 * UTCL1RequestType enum
 */

typedef enum UTCL1RequestType {
UTCL1_TYPE_NORMAL                        = 0x00000000,
UTCL1_TYPE_SHOOTDOWN                     = 0x00000001,
UTCL1_TYPE_BYPASS                        = 0x00000002,
} UTCL1RequestType;

/*
 * VMEMCMD_RETURN_ORDER enum
 */

typedef enum VMEMCMD_RETURN_ORDER {
VMEMCMD_RETURN_OUT_OF_ORDER              = 0x00000000,
VMEMCMD_RETURN_IN_ORDER                  = 0x00000001,
VMEMCMD_RETURN_IN_ORDER_READ             = 0x00000002,
} VMEMCMD_RETURN_ORDER;

/*
 * WritePolicy enum
 */

typedef enum WritePolicy {
CACHE_LRU_WR                             = 0x00000000,
CACHE_STREAM                             = 0x00000001,
CACHE_NOA_WR                             = 0x00000002,
CACHE_BYPASS                             = 0x00000003,
} WritePolicy;

/*******************************************************
 * CNVC_CFG Enums
 *******************************************************/

/*
 * CNVC_BYPASS enum
 */

typedef enum CNVC_BYPASS {
CNVC_BYPASS_DISABLE                      = 0x00000000,
CNVC_BYPASS_EN                           = 0x00000001,
} CNVC_BYPASS;

/*
 * CNVC_COEF_FORMAT_ENUM enum
 */

typedef enum CNVC_COEF_FORMAT_ENUM {
CNVC_FIX_S2_13                           = 0x00000000,
CNVC_FIX_S3_12                           = 0x00000001,
} CNVC_COEF_FORMAT_ENUM;

/*
 * CNVC_ENABLE enum
 */

typedef enum CNVC_ENABLE {
CNVC_DIS                                 = 0x00000000,
CNVC_EN                                  = 0x00000001,
} CNVC_ENABLE;

/*
 * CNVC_PENDING enum
 */

typedef enum CNVC_PENDING {
CNVC_NOT_PENDING                         = 0x00000000,
CNVC_YES_PENDING                         = 0x00000001,
} CNVC_PENDING;

/*
 * COLOR_KEYER_MODE enum
 */

typedef enum COLOR_KEYER_MODE {
FORCE_00                                 = 0x00000000,
FORCE_FF                                 = 0x00000001,
RANGE_00                                 = 0x00000002,
RANGE_FF                                 = 0x00000003,
} COLOR_KEYER_MODE;

/*
 * DENORM_TRUNCATE enum
 */

typedef enum DENORM_TRUNCATE {
CNVC_ROUND                               = 0x00000000,
CNVC_TRUNCATE                            = 0x00000001,
} DENORM_TRUNCATE;

/*
 * FORMAT_CROSSBAR enum
 */

typedef enum FORMAT_CROSSBAR {
FORMAT_CROSSBAR_R                        = 0x00000000,
FORMAT_CROSSBAR_G                        = 0x00000001,
FORMAT_CROSSBAR_B                        = 0x00000002,
} FORMAT_CROSSBAR;

/*
 * PIX_EXPAND_MODE enum
 */

typedef enum PIX_EXPAND_MODE {
PIX_DYNAMIC_EXPANSION                    = 0x00000000,
PIX_ZERO_EXPANSION                       = 0x00000001,
} PIX_EXPAND_MODE;

/*
 * PRE_CSC_MODE_ENUM enum
 */

typedef enum PRE_CSC_MODE_ENUM {
PRE_CSC_BYPASS                           = 0x00000000,
PRE_CSC_SET_A                            = 0x00000001,
PRE_CSC_SET_B                            = 0x00000002,
} PRE_CSC_MODE_ENUM;

/*
 * PRE_DEGAM_MODE enum
 */

typedef enum PRE_DEGAM_MODE {
PRE_DEGAM_BYPASS                         = 0x00000000,
PRE_DEGAM_ENABLE                         = 0x00000001,
} PRE_DEGAM_MODE;

/*
 * PRE_DEGAM_SELECT enum
 */

typedef enum PRE_DEGAM_SELECT {
PRE_DEGAM_SRGB                           = 0x00000000,
PRE_DEGAM_GAMMA_22                       = 0x00000001,
PRE_DEGAM_GAMMA_24                       = 0x00000002,
PRE_DEGAM_GAMMA_26                       = 0x00000003,
PRE_DEGAM_BT2020                         = 0x00000004,
PRE_DEGAM_BT2100PQ                       = 0x00000005,
PRE_DEGAM_BT2100HLG                      = 0x00000006,
} PRE_DEGAM_SELECT;

/*
 * SURFACE_PIXEL_FORMAT enum
 */

typedef enum SURFACE_PIXEL_FORMAT {
ARGB1555                                 = 0x00000001,
RGBA5551                                 = 0x00000002,
RGB565                                   = 0x00000003,
BGR565                                   = 0x00000004,
ARGB4444                                 = 0x00000005,
RGBA4444                                 = 0x00000006,
ARGB8888                                 = 0x00000008,
RGBA8888                                 = 0x00000009,
ARGB2101010                              = 0x0000000a,
RGBA1010102                              = 0x0000000b,
AYCrCb8888                               = 0x0000000c,
YCrCbA8888                               = 0x0000000d,
ACrYCb8888                               = 0x0000000e,
CrYCbA8888                               = 0x0000000f,
ARGB16161616_10MSB                       = 0x00000010,
RGBA16161616_10MSB                       = 0x00000011,
ARGB16161616_10LSB                       = 0x00000012,
RGBA16161616_10LSB                       = 0x00000013,
ARGB16161616_12MSB                       = 0x00000014,
RGBA16161616_12MSB                       = 0x00000015,
ARGB16161616_12LSB                       = 0x00000016,
RGBA16161616_12LSB                       = 0x00000017,
ARGB16161616_FLOAT                       = 0x00000018,
RGBA16161616_FLOAT                       = 0x00000019,
ARGB16161616_UNORM                       = 0x0000001a,
RGBA16161616_UNORM                       = 0x0000001b,
ARGB16161616_SNORM                       = 0x0000001c,
RGBA16161616_SNORM                       = 0x0000001d,
AYCrCb16161616_10MSB                     = 0x00000020,
AYCrCb16161616_10LSB                     = 0x00000021,
YCrCbA16161616_10MSB                     = 0x00000022,
YCrCbA16161616_10LSB                     = 0x00000023,
ACrYCb16161616_10MSB                     = 0x00000024,
ACrYCb16161616_10LSB                     = 0x00000025,
CrYCbA16161616_10MSB                     = 0x00000026,
CrYCbA16161616_10LSB                     = 0x00000027,
AYCrCb16161616_12MSB                     = 0x00000028,
AYCrCb16161616_12LSB                     = 0x00000029,
YCrCbA16161616_12MSB                     = 0x0000002a,
YCrCbA16161616_12LSB                     = 0x0000002b,
ACrYCb16161616_12MSB                     = 0x0000002c,
ACrYCb16161616_12LSB                     = 0x0000002d,
CrYCbA16161616_12MSB                     = 0x0000002e,
CrYCbA16161616_12LSB                     = 0x0000002f,
Y8_CrCb88_420_PLANAR                     = 0x00000040,
Y8_CbCr88_420_PLANAR                     = 0x00000041,
Y10_CrCb1010_420_PLANAR                  = 0x00000042,
Y10_CbCr1010_420_PLANAR                  = 0x00000043,
Y12_CrCb1212_420_PLANAR                  = 0x00000044,
Y12_CbCr1212_420_PLANAR                  = 0x00000045,
YCrYCb8888_422_PACKED                    = 0x00000048,
YCbYCr8888_422_PACKED                    = 0x00000049,
CrYCbY8888_422_PACKED                    = 0x0000004a,
CbYCrY8888_422_PACKED                    = 0x0000004b,
YCrYCb10101010_422_PACKED                = 0x0000004c,
YCbYCr10101010_422_PACKED                = 0x0000004d,
CrYCbY10101010_422_PACKED                = 0x0000004e,
CbYCrY10101010_422_PACKED                = 0x0000004f,
YCrYCb12121212_422_PACKED                = 0x00000050,
YCbYCr12121212_422_PACKED                = 0x00000051,
CrYCbY12121212_422_PACKED                = 0x00000052,
CbYCrY12121212_422_PACKED                = 0x00000053,
RGB111110_FIX                            = 0x00000070,
BGR101111_FIX                            = 0x00000071,
ACrYCb2101010                            = 0x00000072,
CrYCbA1010102                            = 0x00000073,
RGBE                                     = 0x00000074,
RGB111110_FLOAT                          = 0x00000076,
BGR101111_FLOAT                          = 0x00000077,
MONO_8                                   = 0x00000078,
MONO_10MSB                               = 0x00000079,
MONO_10LSB                               = 0x0000007a,
MONO_12MSB                               = 0x0000007b,
MONO_12LSB                               = 0x0000007c,
MONO_16                                  = 0x0000007d,
} SURFACE_PIXEL_FORMAT;

/*
 * XNORM enum
 */

typedef enum XNORM {
XNORM_A                                  = 0x00000000,
XNORM_B                                  = 0x00000001,
} XNORM;

/*******************************************************
 * CNVC_CUR Enums
 *******************************************************/

/*
 * CUR_ENABLE enum
 */

typedef enum CUR_ENABLE {
CUR_DIS                                  = 0x00000000,
CUR_EN                                   = 0x00000001,
} CUR_ENABLE;

/*
 * CUR_EXPAND_MODE enum
 */

typedef enum CUR_EXPAND_MODE {
CUR_DYNAMIC_EXPANSION                    = 0x00000000,
CUR_ZERO_EXPANSION                       = 0x00000001,
} CUR_EXPAND_MODE;

/*
 * CUR_INV_CLAMP enum
 */

typedef enum CUR_INV_CLAMP {
CUR_CLAMP_DIS                            = 0x00000000,
CUR_CLAMP_EN                             = 0x00000001,
} CUR_INV_CLAMP;

/*
 * CUR_MODE enum
 */

typedef enum CUR_MODE {
MONO_2BIT                                = 0x00000000,
COLOR_24BIT_1BIT_AND                     = 0x00000001,
COLOR_24BIT_8BIT_ALPHA_PREMULT           = 0x00000002,
COLOR_24BIT_8BIT_ALPHA_UNPREMULT         = 0x00000003,
COLOR_64BIT_FP_PREMULT                   = 0x00000004,
COLOR_64BIT_FP_UNPREMULT                 = 0x00000005,
} CUR_MODE;

/*
 * CUR_PENDING enum
 */

typedef enum CUR_PENDING {
CUR_NOT_PENDING                          = 0x00000000,
CUR_YES_PENDING                          = 0x00000001,
} CUR_PENDING;

/*
 * CUR_ROM_EN enum
 */

typedef enum CUR_ROM_EN {
CUR_FP_NO_ROM                            = 0x00000000,
CUR_FP_USE_ROM                           = 0x00000001,
} CUR_ROM_EN;

/*******************************************************
 * DSCL Enums
 *******************************************************/

/*
 * COEF_RAM_SELECT_RD enum
 */

typedef enum COEF_RAM_SELECT_RD {
COEF_RAM_SELECT_BACK                     = 0x00000000,
COEF_RAM_SELECT_CURRENT                  = 0x00000001,
} COEF_RAM_SELECT_RD;

/*
 * DSCL_MODE_SEL enum
 */

typedef enum DSCL_MODE_SEL {
DSCL_MODE_SCALING_444_BYPASS             = 0x00000000,
DSCL_MODE_SCALING_444_RGB_ENABLE         = 0x00000001,
DSCL_MODE_SCALING_444_YCBCR_ENABLE       = 0x00000002,
DSCL_MODE_SCALING_YCBCR_ENABLE           = 0x00000003,
DSCL_MODE_LUMA_SCALING_BYPASS            = 0x00000004,
DSCL_MODE_CHROMA_SCALING_BYPASS          = 0x00000005,
DSCL_MODE_DSCL_BYPASS                    = 0x00000006,
} DSCL_MODE_SEL;

/*
 * LB_ALPHA_EN enum
 */

typedef enum LB_ALPHA_EN {
LB_ALPHA_DISABLE                         = 0x00000000,
LB_ALPHA_ENABLE                          = 0x00000001,
} LB_ALPHA_EN;

/*
 * LB_INTERLEAVE_EN enum
 */

typedef enum LB_INTERLEAVE_EN {
LB_INTERLEAVE_DISABLE                    = 0x00000000,
LB_INTERLEAVE_ENABLE                     = 0x00000001,
} LB_INTERLEAVE_EN;

/*
 * LB_MEMORY_CONFIG enum
 */

typedef enum LB_MEMORY_CONFIG {
LB_MEMORY_CONFIG_0                       = 0x00000000,
LB_MEMORY_CONFIG_1                       = 0x00000001,
LB_MEMORY_CONFIG_2                       = 0x00000002,
LB_MEMORY_CONFIG_3                       = 0x00000003,
} LB_MEMORY_CONFIG;

/*
 * OBUF_BYPASS_SEL enum
 */

typedef enum OBUF_BYPASS_SEL {
OBUF_BYPASS_DIS                          = 0x00000000,
OBUF_BYPASS_EN                           = 0x00000001,
} OBUF_BYPASS_SEL;

/*
 * OBUF_IS_HALF_RECOUT_WIDTH_SEL enum
 */

typedef enum OBUF_IS_HALF_RECOUT_WIDTH_SEL {
OBUF_FULL_RECOUT                         = 0x00000000,
OBUF_HALF_RECOUT                         = 0x00000001,
} OBUF_IS_HALF_RECOUT_WIDTH_SEL;

/*
 * OBUF_USE_FULL_BUFFER_SEL enum
 */

typedef enum OBUF_USE_FULL_BUFFER_SEL {
OBUF_RECOUT                              = 0x00000000,
OBUF_FULL                                = 0x00000001,
} OBUF_USE_FULL_BUFFER_SEL;

/*
 * SCL_2TAP_HARDCODE enum
 */

typedef enum SCL_2TAP_HARDCODE {
SCL_COEF_2TAP_HARDCODE_OFF               = 0x00000000,
SCL_COEF_2TAP_HARDCODE_ON                = 0x00000001,
} SCL_2TAP_HARDCODE;

/*
 * SCL_ALPHA_COEF enum
 */

typedef enum SCL_ALPHA_COEF {
SCL_ALPHA_COEF_FIRST                     = 0x00000000,
SCL_ALPHA_COEF_SECOND                    = 0x00000001,
} SCL_ALPHA_COEF;

/*
 * SCL_AUTOCAL_MODE enum
 */

typedef enum SCL_AUTOCAL_MODE {
AUTOCAL_MODE_OFF                         = 0x00000000,
AUTOCAL_MODE_AUTOSCALE                   = 0x00000001,
AUTOCAL_MODE_AUTOCENTER                  = 0x00000002,
AUTOCAL_MODE_AUTOREPLICATE               = 0x00000003,
} SCL_AUTOCAL_MODE;

/*
 * SCL_BOUNDARY enum
 */

typedef enum SCL_BOUNDARY {
SCL_BOUNDARY_EDGE                        = 0x00000000,
SCL_BOUNDARY_BLACK                       = 0x00000001,
} SCL_BOUNDARY;

/*
 * SCL_CHROMA_COEF enum
 */

typedef enum SCL_CHROMA_COEF {
SCL_CHROMA_COEF_FIRST                    = 0x00000000,
SCL_CHROMA_COEF_SECOND                   = 0x00000001,
} SCL_CHROMA_COEF;

/*
 * SCL_COEF_FILTER_TYPE_SEL enum
 */

typedef enum SCL_COEF_FILTER_TYPE_SEL {
SCL_COEF_LUMA_VERT_FILTER                = 0x00000000,
SCL_COEF_LUMA_HORZ_FILTER                = 0x00000001,
SCL_COEF_CHROMA_VERT_FILTER              = 0x00000002,
SCL_COEF_CHROMA_HORZ_FILTER              = 0x00000003,
} SCL_COEF_FILTER_TYPE_SEL;

/*
 * SCL_COEF_RAM_SEL enum
 */

typedef enum SCL_COEF_RAM_SEL {
SCL_COEF_RAM_SEL_0                       = 0x00000000,
SCL_COEF_RAM_SEL_1                       = 0x00000001,
} SCL_COEF_RAM_SEL;

/*
 * SCL_SHARP_EN enum
 */

typedef enum SCL_SHARP_EN {
SCL_SHARP_DISABLE                        = 0x00000000,
SCL_SHARP_ENABLE                         = 0x00000001,
} SCL_SHARP_EN;

/*******************************************************
 * CM Enums
 *******************************************************/

/*
 * CMC_3DLUT_30BIT_ENUM enum
 */

typedef enum CMC_3DLUT_30BIT_ENUM {
CMC_3DLUT_36BIT                          = 0x00000000,
CMC_3DLUT_30BIT                          = 0x00000001,
} CMC_3DLUT_30BIT_ENUM;

/*
 * CMC_3DLUT_RAM_SEL enum
 */

typedef enum CMC_3DLUT_RAM_SEL {
CMC_RAM0_ACCESS                          = 0x00000000,
CMC_RAM1_ACCESS                          = 0x00000001,
CMC_RAM2_ACCESS                          = 0x00000002,
CMC_RAM3_ACCESS                          = 0x00000003,
} CMC_3DLUT_RAM_SEL;

/*
 * CMC_3DLUT_SIZE_ENUM enum
 */

typedef enum CMC_3DLUT_SIZE_ENUM {
CMC_3DLUT_17CUBE                         = 0x00000000,
CMC_3DLUT_9CUBE                          = 0x00000001,
} CMC_3DLUT_SIZE_ENUM;

/*
 * CMC_LUT_2_CONFIG_ENUM enum
 */

typedef enum CMC_LUT_2_CONFIG_ENUM {
CMC_LUT_2CFG_NO_MEMORY                   = 0x00000000,
CMC_LUT_2CFG_MEMORY_A                    = 0x00000001,
CMC_LUT_2CFG_MEMORY_B                    = 0x00000002,
} CMC_LUT_2_CONFIG_ENUM;

/*
 * CMC_LUT_2_MODE_ENUM enum
 */

typedef enum CMC_LUT_2_MODE_ENUM {
CMC_LUT_2_MODE_BYPASS                    = 0x00000000,
CMC_LUT_2_MODE_RAMA_LUT                  = 0x00000001,
CMC_LUT_2_MODE_RAMB_LUT                  = 0x00000002,
} CMC_LUT_2_MODE_ENUM;

/*
 * CMC_LUT_NUM_SEG enum
 */

typedef enum CMC_LUT_NUM_SEG {
CMC_SEGMENTS_1                           = 0x00000000,
CMC_SEGMENTS_2                           = 0x00000001,
CMC_SEGMENTS_4                           = 0x00000002,
CMC_SEGMENTS_8                           = 0x00000003,
CMC_SEGMENTS_16                          = 0x00000004,
CMC_SEGMENTS_32                          = 0x00000005,
CMC_SEGMENTS_64                          = 0x00000006,
CMC_SEGMENTS_128                         = 0x00000007,
} CMC_LUT_NUM_SEG;

/*
 * CMC_LUT_RAM_SEL enum
 */

typedef enum CMC_LUT_RAM_SEL {
CMC_RAMA_ACCESS                          = 0x00000000,
CMC_RAMB_ACCESS                          = 0x00000001,
} CMC_LUT_RAM_SEL;

/*
 * CM_BYPASS enum
 */

typedef enum CM_BYPASS {
NON_BYPASS                               = 0x00000000,
BYPASS_EN                                = 0x00000001,
} CM_BYPASS;

/*
 * CM_COEF_FORMAT_ENUM enum
 */

typedef enum CM_COEF_FORMAT_ENUM {
FIX_S2_13                                = 0x00000000,
FIX_S3_12                                = 0x00000001,
} CM_COEF_FORMAT_ENUM;

/*
 * CM_DATA_SIGNED enum
 */

typedef enum CM_DATA_SIGNED {
UNSIGNED                                 = 0x00000000,
SIGNED                                   = 0x00000001,
} CM_DATA_SIGNED;

/*
 * CM_EN enum
 */

typedef enum CM_EN {
CM_DISABLE                               = 0x00000000,
CM_ENABLE                                = 0x00000001,
} CM_EN;

/*
 * CM_GAMMA_LUT_MODE_ENUM enum
 */

typedef enum CM_GAMMA_LUT_MODE_ENUM {
BYPASS                                   = 0x00000000,
RESERVED_1                               = 0x00000001,
RAM_LUT                                  = 0x00000002,
RESERVED_3                               = 0x00000003,
} CM_GAMMA_LUT_MODE_ENUM;

/*
 * CM_GAMMA_LUT_PWL_DISABLE_ENUM enum
 */

typedef enum CM_GAMMA_LUT_PWL_DISABLE_ENUM {
ENABLE_PWL                               = 0x00000000,
DISABLE_PWL                              = 0x00000001,
} CM_GAMMA_LUT_PWL_DISABLE_ENUM;

/*
 * CM_GAMMA_LUT_SEL_ENUM enum
 */

typedef enum CM_GAMMA_LUT_SEL_ENUM {
RAMA                                     = 0x00000000,
RAMB                                     = 0x00000001,
} CM_GAMMA_LUT_SEL_ENUM;

/*
 * CM_GAMUT_REMAP_MODE_ENUM enum
 */

typedef enum CM_GAMUT_REMAP_MODE_ENUM {
BYPASS_GAMUT                             = 0x00000000,
GAMUT_COEF                               = 0x00000001,
GAMUT_COEF_B                             = 0x00000002,
} CM_GAMUT_REMAP_MODE_ENUM;

/*
 * CM_LUT_2_CONFIG_ENUM enum
 */

typedef enum CM_LUT_2_CONFIG_ENUM {
LUT_2CFG_NO_MEMORY                       = 0x00000000,
LUT_2CFG_MEMORY_A                        = 0x00000001,
LUT_2CFG_MEMORY_B                        = 0x00000002,
} CM_LUT_2_CONFIG_ENUM;

/*
 * CM_LUT_2_MODE_ENUM enum
 */

typedef enum CM_LUT_2_MODE_ENUM {
LUT_2_MODE_BYPASS                        = 0x00000000,
LUT_2_MODE_RAMA_LUT                      = 0x00000001,
LUT_2_MODE_RAMB_LUT                      = 0x00000002,
} CM_LUT_2_MODE_ENUM;

/*
 * CM_LUT_4_CONFIG_ENUM enum
 */

typedef enum CM_LUT_4_CONFIG_ENUM {
LUT_4CFG_NO_MEMORY                       = 0x00000000,
LUT_4CFG_ROM_A                           = 0x00000001,
LUT_4CFG_ROM_B                           = 0x00000002,
LUT_4CFG_MEMORY_A                        = 0x00000003,
LUT_4CFG_MEMORY_B                        = 0x00000004,
} CM_LUT_4_CONFIG_ENUM;

/*
 * CM_LUT_4_MODE_ENUM enum
 */

typedef enum CM_LUT_4_MODE_ENUM {
LUT_4_MODE_BYPASS                        = 0x00000000,
LUT_4_MODE_ROMA_LUT                      = 0x00000001,
LUT_4_MODE_ROMB_LUT                      = 0x00000002,
LUT_4_MODE_RAMA_LUT                      = 0x00000003,
LUT_4_MODE_RAMB_LUT                      = 0x00000004,
} CM_LUT_4_MODE_ENUM;

/*
 * CM_LUT_CONFIG_MODE enum
 */

typedef enum CM_LUT_CONFIG_MODE {
DIFFERENT_RGB                            = 0x00000000,
ALL_USE_R                                = 0x00000001,
} CM_LUT_CONFIG_MODE;

/*
 * CM_LUT_NUM_SEG enum
 */

typedef enum CM_LUT_NUM_SEG {
SEGMENTS_1                               = 0x00000000,
SEGMENTS_2                               = 0x00000001,
SEGMENTS_4                               = 0x00000002,
SEGMENTS_8                               = 0x00000003,
SEGMENTS_16                              = 0x00000004,
SEGMENTS_32                              = 0x00000005,
SEGMENTS_64                              = 0x00000006,
SEGMENTS_128                             = 0x00000007,
} CM_LUT_NUM_SEG;

/*
 * CM_LUT_RAM_SEL enum
 */

typedef enum CM_LUT_RAM_SEL {
RAMA_ACCESS                              = 0x00000000,
RAMB_ACCESS                              = 0x00000001,
} CM_LUT_RAM_SEL;

/*
 * CM_LUT_READ_COLOR_SEL enum
 */

typedef enum CM_LUT_READ_COLOR_SEL {
BLUE_LUT                                 = 0x00000000,
GREEN_LUT                                = 0x00000001,
RED_LUT                                  = 0x00000002,
} CM_LUT_READ_COLOR_SEL;

/*
 * CM_LUT_READ_DBG enum
 */

typedef enum CM_LUT_READ_DBG {
DISABLE_DEBUG                            = 0x00000000,
ENABLE_DEBUG                             = 0x00000001,
} CM_LUT_READ_DBG;

/*
 * CM_PENDING enum
 */

typedef enum CM_PENDING {
CM_NOT_PENDING                           = 0x00000000,
CM_YES_PENDING                           = 0x00000001,
} CM_PENDING;

/*
 * CM_POST_CSC_MODE_ENUM enum
 */

typedef enum CM_POST_CSC_MODE_ENUM {
BYPASS_POST_CSC                          = 0x00000000,
COEF_POST_CSC                            = 0x00000001,
COEF_POST_CSC_B                          = 0x00000002,
} CM_POST_CSC_MODE_ENUM;

/*
 * CM_WRITE_BASE_ONLY enum
 */

typedef enum CM_WRITE_BASE_ONLY {
WRITE_BOTH                               = 0x00000000,
WRITE_BASE_ONLY                          = 0x00000001,
} CM_WRITE_BASE_ONLY;

/*******************************************************
 * DPP_TOP Enums
 *******************************************************/

/*
 * CRC_CUR_SEL enum
 */

typedef enum CRC_CUR_SEL {
CRC_CUR_0                                = 0x00000000,
CRC_CUR_1                                = 0x00000001,
} CRC_CUR_SEL;

/*
 * CRC_INTERLACE_SEL enum
 */

typedef enum CRC_INTERLACE_SEL {
CRC_INTERLACE_0                          = 0x00000000,
CRC_INTERLACE_1                          = 0x00000001,
CRC_INTERLACE_2                          = 0x00000002,
CRC_INTERLACE_3                          = 0x00000003,
} CRC_INTERLACE_SEL;

/*
 * CRC_IN_CUR_SEL enum
 */

typedef enum CRC_IN_CUR_SEL {
CRC_IN_CUR_0                             = 0x00000000,
CRC_IN_CUR_1                             = 0x00000001,
CRC_IN_CUR_2                             = 0x00000002,
CRC_IN_CUR_3                             = 0x00000003,
} CRC_IN_CUR_SEL;

/*
 * CRC_IN_PIX_SEL enum
 */

typedef enum CRC_IN_PIX_SEL {
CRC_IN_PIX_0                             = 0x00000000,
CRC_IN_PIX_1                             = 0x00000001,
CRC_IN_PIX_2                             = 0x00000002,
CRC_IN_PIX_3                             = 0x00000003,
CRC_IN_PIX_4                             = 0x00000004,
CRC_IN_PIX_5                             = 0x00000005,
CRC_IN_PIX_6                             = 0x00000006,
CRC_IN_PIX_7                             = 0x00000007,
} CRC_IN_PIX_SEL;

/*
 * CRC_SRC_SEL enum
 */

typedef enum CRC_SRC_SEL {
CRC_SRC_0                                = 0x00000000,
CRC_SRC_1                                = 0x00000001,
CRC_SRC_2                                = 0x00000002,
CRC_SRC_3                                = 0x00000003,
} CRC_SRC_SEL;

/*
 * CRC_STEREO_SEL enum
 */

typedef enum CRC_STEREO_SEL {
CRC_STEREO_0                             = 0x00000000,
CRC_STEREO_1                             = 0x00000001,
CRC_STEREO_2                             = 0x00000002,
CRC_STEREO_3                             = 0x00000003,
} CRC_STEREO_SEL;

/*
 * TEST_CLK_SEL enum
 */

typedef enum TEST_CLK_SEL {
TEST_CLK_SEL_0                           = 0x00000000,
TEST_CLK_SEL_1                           = 0x00000001,
TEST_CLK_SEL_2                           = 0x00000002,
TEST_CLK_SEL_3                           = 0x00000003,
TEST_CLK_SEL_4                           = 0x00000004,
TEST_CLK_SEL_5                           = 0x00000005,
TEST_CLK_SEL_6                           = 0x00000006,
TEST_CLK_SEL_7                           = 0x00000007,
} TEST_CLK_SEL;

/*******************************************************
 * DC_PERFMON Enums
 *******************************************************/

/*
 * PERFCOUNTER_ACTIVE enum
 */

typedef enum PERFCOUNTER_ACTIVE {
PERFCOUNTER_IS_IDLE                      = 0x00000000,
PERFCOUNTER_IS_ACTIVE                    = 0x00000001,
} PERFCOUNTER_ACTIVE;

/*
 * PERFCOUNTER_CNT0_STATE enum
 */

typedef enum PERFCOUNTER_CNT0_STATE {
PERFCOUNTER_CNT0_STATE_RESET             = 0x00000000,
PERFCOUNTER_CNT0_STATE_START             = 0x00000001,
PERFCOUNTER_CNT0_STATE_FREEZE            = 0x00000002,
PERFCOUNTER_CNT0_STATE_HW                = 0x00000003,
} PERFCOUNTER_CNT0_STATE;

/*
 * PERFCOUNTER_CNT1_STATE enum
 */

typedef enum PERFCOUNTER_CNT1_STATE {
PERFCOUNTER_CNT1_STATE_RESET             = 0x00000000,
PERFCOUNTER_CNT1_STATE_START             = 0x00000001,
PERFCOUNTER_CNT1_STATE_FREEZE            = 0x00000002,
PERFCOUNTER_CNT1_STATE_HW                = 0x00000003,
} PERFCOUNTER_CNT1_STATE;

/*
 * PERFCOUNTER_CNT2_STATE enum
 */

typedef enum PERFCOUNTER_CNT2_STATE {
PERFCOUNTER_CNT2_STATE_RESET             = 0x00000000,
PERFCOUNTER_CNT2_STATE_START             = 0x00000001,
PERFCOUNTER_CNT2_STATE_FREEZE            = 0x00000002,
PERFCOUNTER_CNT2_STATE_HW                = 0x00000003,
} PERFCOUNTER_CNT2_STATE;

/*
 * PERFCOUNTER_CNT3_STATE enum
 */

typedef enum PERFCOUNTER_CNT3_STATE {
PERFCOUNTER_CNT3_STATE_RESET             = 0x00000000,
PERFCOUNTER_CNT3_STATE_START             = 0x00000001,
PERFCOUNTER_CNT3_STATE_FREEZE            = 0x00000002,
PERFCOUNTER_CNT3_STATE_HW                = 0x00000003,
} PERFCOUNTER_CNT3_STATE;

/*
 * PERFCOUNTER_CNT4_STATE enum
 */

typedef enum PERFCOUNTER_CNT4_STATE {
PERFCOUNTER_CNT4_STATE_RESET             = 0x00000000,
PERFCOUNTER_CNT4_STATE_START             = 0x00000001,
PERFCOUNTER_CNT4_STATE_FREEZE            = 0x00000002,
PERFCOUNTER_CNT4_STATE_HW                = 0x00000003,
} PERFCOUNTER_CNT4_STATE;

/*
 * PERFCOUNTER_CNT5_STATE enum
 */

typedef enum PERFCOUNTER_CNT5_STATE {
PERFCOUNTER_CNT5_STATE_RESET             = 0x00000000,
PERFCOUNTER_CNT5_STATE_START             = 0x00000001,
PERFCOUNTER_CNT5_STATE_FREEZE            = 0x00000002,
PERFCOUNTER_CNT5_STATE_HW                = 0x00000003,
} PERFCOUNTER_CNT5_STATE;

/*
 * PERFCOUNTER_CNT6_STATE enum
 */

typedef enum PERFCOUNTER_CNT6_STATE {
PERFCOUNTER_CNT6_STATE_RESET             = 0x00000000,
PERFCOUNTER_CNT6_STATE_START             = 0x00000001,
PERFCOUNTER_CNT6_STATE_FREEZE            = 0x00000002,
PERFCOUNTER_CNT6_STATE_HW                = 0x00000003,
} PERFCOUNTER_CNT6_STATE;

/*
 * PERFCOUNTER_CNT7_STATE enum
 */

typedef enum PERFCOUNTER_CNT7_STATE {
PERFCOUNTER_CNT7_STATE_RESET             = 0x00000000,
PERFCOUNTER_CNT7_STATE_START             = 0x00000001,
PERFCOUNTER_CNT7_STATE_FREEZE            = 0x00000002,
PERFCOUNTER_CNT7_STATE_HW                = 0x00000003,
} PERFCOUNTER_CNT7_STATE;

/*
 * PERFCOUNTER_CNTL_SEL enum
 */

typedef enum PERFCOUNTER_CNTL_SEL {
PERFCOUNTER_CNTL_SEL_0                   = 0x00000000,
PERFCOUNTER_CNTL_SEL_1                   = 0x00000001,
PERFCOUNTER_CNTL_SEL_2                   = 0x00000002,
PERFCOUNTER_CNTL_SEL_3                   = 0x00000003,
PERFCOUNTER_CNTL_SEL_4                   = 0x00000004,
PERFCOUNTER_CNTL_SEL_5                   = 0x00000005,
PERFCOUNTER_CNTL_SEL_6                   = 0x00000006,
PERFCOUNTER_CNTL_SEL_7                   = 0x00000007,
} PERFCOUNTER_CNTL_SEL;

/*
 * PERFCOUNTER_CNTOFF_START_DIS enum
 */

typedef enum PERFCOUNTER_CNTOFF_START_DIS {
PERFCOUNTER_CNTOFF_START_ENABLE          = 0x00000000,
PERFCOUNTER_CNTOFF_START_DISABLE         = 0x00000001,
} PERFCOUNTER_CNTOFF_START_DIS;

/*
 * PERFCOUNTER_COUNTED_VALUE_TYPE enum
 */

typedef enum PERFCOUNTER_COUNTED_VALUE_TYPE {
PERFCOUNTER_COUNTED_VALUE_TYPE_ACC       = 0x00000000,
PERFCOUNTER_COUNTED_VALUE_TYPE_MAX       = 0x00000001,
PERFCOUNTER_COUNTED_VALUE_TYPE_MIN       = 0x00000002,
} PERFCOUNTER_COUNTED_VALUE_TYPE;

/*
 * PERFCOUNTER_CVALUE_SEL enum
 */

typedef enum PERFCOUNTER_CVALUE_SEL {
PERFCOUNTER_CVALUE_SEL_47_0              = 0x00000000,
PERFCOUNTER_CVALUE_SEL_15_0              = 0x00000001,
PERFCOUNTER_CVALUE_SEL_31_16             = 0x00000002,
PERFCOUNTER_CVALUE_SEL_47_32             = 0x00000003,
PERFCOUNTER_CVALUE_SEL_11_0              = 0x00000004,
PERFCOUNTER_CVALUE_SEL_23_12             = 0x00000005,
PERFCOUNTER_CVALUE_SEL_35_24             = 0x00000006,
PERFCOUNTER_CVALUE_SEL_47_36             = 0x00000007,
} PERFCOUNTER_CVALUE_SEL;

/*
 * PERFCOUNTER_HW_CNTL_SEL enum
 */

typedef enum PERFCOUNTER_HW_CNTL_SEL {
PERFCOUNTER_HW_CNTL_SEL_RUNEN            = 0x00000000,
PERFCOUNTER_HW_CNTL_SEL_CNTOFF           = 0x00000001,
} PERFCOUNTER_HW_CNTL_SEL;

/*
 * PERFCOUNTER_HW_STOP1_SEL enum
 */

typedef enum PERFCOUNTER_HW_STOP1_SEL {
PERFCOUNTER_HW_STOP1_0                   = 0x00000000,
PERFCOUNTER_HW_STOP1_1                   = 0x00000001,
} PERFCOUNTER_HW_STOP1_SEL;

/*
 * PERFCOUNTER_HW_STOP2_SEL enum
 */

typedef enum PERFCOUNTER_HW_STOP2_SEL {
PERFCOUNTER_HW_STOP2_0                   = 0x00000000,
PERFCOUNTER_HW_STOP2_1                   = 0x00000001,
} PERFCOUNTER_HW_STOP2_SEL;

/*
 * PERFCOUNTER_INC_MODE enum
 */

typedef enum PERFCOUNTER_INC_MODE {
PERFCOUNTER_INC_MODE_MULTI_BIT           = 0x00000000,
PERFCOUNTER_INC_MODE_BOTH_EDGE           = 0x00000001,
PERFCOUNTER_INC_MODE_LSB                 = 0x00000002,
PERFCOUNTER_INC_MODE_POS_EDGE            = 0x00000003,
PERFCOUNTER_INC_MODE_NEG_EDGE            = 0x00000004,
} PERFCOUNTER_INC_MODE;

/*
 * PERFCOUNTER_INT_EN enum
 */

typedef enum PERFCOUNTER_INT_EN {
PERFCOUNTER_INT_DISABLE                  = 0x00000000,
PERFCOUNTER_INT_ENABLE                   = 0x00000001,
} PERFCOUNTER_INT_EN;

/*
 * PERFCOUNTER_INT_TYPE enum
 */

typedef enum PERFCOUNTER_INT_TYPE {
PERFCOUNTER_INT_TYPE_LEVEL               = 0x00000000,
PERFCOUNTER_INT_TYPE_PULSE               = 0x00000001,
} PERFCOUNTER_INT_TYPE;

/*
 * PERFCOUNTER_OFF_MASK enum
 */

typedef enum PERFCOUNTER_OFF_MASK {
PERFCOUNTER_OFF_MASK_DISABLE             = 0x00000000,
PERFCOUNTER_OFF_MASK_ENABLE              = 0x00000001,
} PERFCOUNTER_OFF_MASK;

/*
 * PERFCOUNTER_RESTART_EN enum
 */

typedef enum PERFCOUNTER_RESTART_EN {
PERFCOUNTER_RESTART_DISABLE              = 0x00000000,
PERFCOUNTER_RESTART_ENABLE               = 0x00000001,
} PERFCOUNTER_RESTART_EN;

/*
 * PERFCOUNTER_RUNEN_MODE enum
 */

typedef enum PERFCOUNTER_RUNEN_MODE {
PERFCOUNTER_RUNEN_MODE_LEVEL             = 0x00000000,
PERFCOUNTER_RUNEN_MODE_EDGE              = 0x00000001,
} PERFCOUNTER_RUNEN_MODE;

/*
 * PERFCOUNTER_STATE_SEL0 enum
 */

typedef enum PERFCOUNTER_STATE_SEL0 {
PERFCOUNTER_STATE_SEL0_GLOBAL            = 0x00000000,
PERFCOUNTER_STATE_SEL0_LOCAL             = 0x00000001,
} PERFCOUNTER_STATE_SEL0;

/*
 * PERFCOUNTER_STATE_SEL1 enum
 */

typedef enum PERFCOUNTER_STATE_SEL1 {
PERFCOUNTER_STATE_SEL1_GLOBAL            = 0x00000000,
PERFCOUNTER_STATE_SEL1_LOCAL             = 0x00000001,
} PERFCOUNTER_STATE_SEL1;

/*
 * PERFCOUNTER_STATE_SEL2 enum
 */

typedef enum PERFCOUNTER_STATE_SEL2 {
PERFCOUNTER_STATE_SEL2_GLOBAL            = 0x00000000,
PERFCOUNTER_STATE_SEL2_LOCAL             = 0x00000001,
} PERFCOUNTER_STATE_SEL2;

/*
 * PERFCOUNTER_STATE_SEL3 enum
 */

typedef enum PERFCOUNTER_STATE_SEL3 {
PERFCOUNTER_STATE_SEL3_GLOBAL            = 0x00000000,
PERFCOUNTER_STATE_SEL3_LOCAL             = 0x00000001,
} PERFCOUNTER_STATE_SEL3;

/*
 * PERFCOUNTER_STATE_SEL4 enum
 */

typedef enum PERFCOUNTER_STATE_SEL4 {
PERFCOUNTER_STATE_SEL4_GLOBAL            = 0x00000000,
PERFCOUNTER_STATE_SEL4_LOCAL             = 0x00000001,
} PERFCOUNTER_STATE_SEL4;

/*
 * PERFCOUNTER_STATE_SEL5 enum
 */

typedef enum PERFCOUNTER_STATE_SEL5 {
PERFCOUNTER_STATE_SEL5_GLOBAL            = 0x00000000,
PERFCOUNTER_STATE_SEL5_LOCAL             = 0x00000001,
} PERFCOUNTER_STATE_SEL5;

/*
 * PERFCOUNTER_STATE_SEL6 enum
 */

typedef enum PERFCOUNTER_STATE_SEL6 {
PERFCOUNTER_STATE_SEL6_GLOBAL            = 0x00000000,
PERFCOUNTER_STATE_SEL6_LOCAL             = 0x00000001,
} PERFCOUNTER_STATE_SEL6;

/*
 * PERFCOUNTER_STATE_SEL7 enum
 */

typedef enum PERFCOUNTER_STATE_SEL7 {
PERFCOUNTER_STATE_SEL7_GLOBAL            = 0x00000000,
PERFCOUNTER_STATE_SEL7_LOCAL             = 0x00000001,
} PERFCOUNTER_STATE_SEL7;

/*
 * PERFMON_CNTOFF_AND_OR enum
 */

typedef enum PERFMON_CNTOFF_AND_OR {
PERFMON_CNTOFF_OR                        = 0x00000000,
PERFMON_CNTOFF_AND                       = 0x00000001,
} PERFMON_CNTOFF_AND_OR;

/*
 * PERFMON_CNTOFF_INT_EN enum
 */

typedef enum PERFMON_CNTOFF_INT_EN {
PERFMON_CNTOFF_INT_DISABLE               = 0x00000000,
PERFMON_CNTOFF_INT_ENABLE                = 0x00000001,
} PERFMON_CNTOFF_INT_EN;

/*
 * PERFMON_CNTOFF_INT_TYPE enum
 */

typedef enum PERFMON_CNTOFF_INT_TYPE {
PERFMON_CNTOFF_INT_TYPE_LEVEL            = 0x00000000,
PERFMON_CNTOFF_INT_TYPE_PULSE            = 0x00000001,
} PERFMON_CNTOFF_INT_TYPE;

/*
 * PERFMON_STATE enum
 */

typedef enum PERFMON_STATE {
PERFMON_STATE_RESET                      = 0x00000000,
PERFMON_STATE_START                      = 0x00000001,
PERFMON_STATE_FREEZE                     = 0x00000002,
PERFMON_STATE_HW                         = 0x00000003,
} PERFMON_STATE;

/*******************************************************
 * HUBP Enums
 *******************************************************/

/*
 * BIGK_FRAGMENT_SIZE enum
 */

typedef enum BIGK_FRAGMENT_SIZE {
VM_PG_SIZE_4KB                           = 0x00000000,
VM_PG_SIZE_8KB                           = 0x00000001,
VM_PG_SIZE_16KB                          = 0x00000002,
VM_PG_SIZE_32KB                          = 0x00000003,
VM_PG_SIZE_64KB                          = 0x00000004,
VM_PG_SIZE_128KB                         = 0x00000005,
VM_PG_SIZE_256KB                         = 0x00000006,
VM_PG_SIZE_512KB                         = 0x00000007,
VM_PG_SIZE_1024KB                        = 0x00000008,
VM_PG_SIZE_2048KB                        = 0x00000009,
} BIGK_FRAGMENT_SIZE;

/*
 * CHUNK_SIZE enum
 */

typedef enum CHUNK_SIZE {
CHUNK_SIZE_1KB                           = 0x00000000,
CHUNK_SIZE_2KB                           = 0x00000001,
CHUNK_SIZE_4KB                           = 0x00000002,
CHUNK_SIZE_8KB                           = 0x00000003,
CHUNK_SIZE_16KB                          = 0x00000004,
CHUNK_SIZE_32KB                          = 0x00000005,
CHUNK_SIZE_64KB                          = 0x00000006,
} CHUNK_SIZE;

/*
 * COMPAT_LEVEL enum
 */

typedef enum COMPAT_LEVEL {
ADDR_GEN_ZERO                            = 0x00000000,
ADDR_GEN_ONE                             = 0x00000001,
ADDR_GEN_TWO                             = 0x00000002,
ADDR_RESERVED                            = 0x00000003,
} COMPAT_LEVEL;

/*
 * DPTE_GROUP_SIZE enum
 */

typedef enum DPTE_GROUP_SIZE {
DPTE_GROUP_SIZE_64B                      = 0x00000000,
DPTE_GROUP_SIZE_128B                     = 0x00000001,
DPTE_GROUP_SIZE_256B                     = 0x00000002,
DPTE_GROUP_SIZE_512B                     = 0x00000003,
DPTE_GROUP_SIZE_1024B                    = 0x00000004,
DPTE_GROUP_SIZE_2048B                    = 0x00000005,
} DPTE_GROUP_SIZE;

/*
 * FORCE_ONE_ROW_FOR_FRAME enum
 */

typedef enum FORCE_ONE_ROW_FOR_FRAME {
FORCE_ONE_ROW_FOR_FRAME_0                = 0x00000000,
FORCE_ONE_ROW_FOR_FRAME_1                = 0x00000001,
} FORCE_ONE_ROW_FOR_FRAME;

/*
 * HUBP_BLANK_EN enum
 */

typedef enum HUBP_BLANK_EN {
HUBP_BLANK_SW_DEASSERT                   = 0x00000000,
HUBP_BLANK_SW_ASSERT                     = 0x00000001,
} HUBP_BLANK_EN;

/*
 * HUBP_IN_BLANK enum
 */

typedef enum HUBP_IN_BLANK {
HUBP_IN_ACTIVE                           = 0x00000000,
HUBP_IN_VBLANK                           = 0x00000001,
} HUBP_IN_BLANK;

/*
 * HUBP_MEASURE_WIN_MODE_DCFCLK enum
 */

typedef enum HUBP_MEASURE_WIN_MODE_DCFCLK {
HUBP_MEASURE_WIN_MODE_DCFCLK_0           = 0x00000000,
HUBP_MEASURE_WIN_MODE_DCFCLK_1           = 0x00000001,
HUBP_MEASURE_WIN_MODE_DCFCLK_2           = 0x00000002,
HUBP_MEASURE_WIN_MODE_DCFCLK_3           = 0x00000003,
} HUBP_MEASURE_WIN_MODE_DCFCLK;

/*
 * HUBP_NO_OUTSTANDING_REQ enum
 */

typedef enum HUBP_NO_OUTSTANDING_REQ {
OUTSTANDING_REQ                          = 0x00000000,
NO_OUTSTANDING_REQ                       = 0x00000001,
} HUBP_NO_OUTSTANDING_REQ;

/*
 * HUBP_SOFT_RESET enum
 */

typedef enum HUBP_SOFT_RESET {
HUBP_SOFT_RESET_ON                       = 0x00000000,
HUBP_SOFT_RESET_OFF                      = 0x00000001,
} HUBP_SOFT_RESET;

/*
 * HUBP_TTU_DISABLE enum
 */

typedef enum HUBP_TTU_DISABLE {
HUBP_TTU_ENABLED                         = 0x00000000,
HUBP_TTU_DISABLED                        = 0x00000001,
} HUBP_TTU_DISABLE;

/*
 * HUBP_VREADY_AT_OR_AFTER_VSYNC enum
 */

typedef enum HUBP_VREADY_AT_OR_AFTER_VSYNC {
VREADY_BEFORE_VSYNC                      = 0x00000000,
VREADY_AT_OR_AFTER_VSYNC                 = 0x00000001,
} HUBP_VREADY_AT_OR_AFTER_VSYNC;

/*
 * HUBP_VTG_SEL enum
 */

typedef enum HUBP_VTG_SEL {
VTG_SEL_0                                = 0x00000000,
VTG_SEL_1                                = 0x00000001,
VTG_SEL_2                                = 0x00000002,
VTG_SEL_3                                = 0x00000003,
VTG_SEL_4                                = 0x00000004,
VTG_SEL_5                                = 0x00000005,
} HUBP_VTG_SEL;

/*
 * H_MIRROR_EN enum
 */

typedef enum H_MIRROR_EN {
HW_MIRRORING_DISABLE                     = 0x00000000,
HW_MIRRORING_ENABLE                      = 0x00000001,
} H_MIRROR_EN;

/*
 * LEGACY_PIPE_INTERLEAVE enum
 */

typedef enum LEGACY_PIPE_INTERLEAVE {
LEGACY_PIPE_INTERLEAVE_256B              = 0x00000000,
LEGACY_PIPE_INTERLEAVE_512B              = 0x00000001,
} LEGACY_PIPE_INTERLEAVE;

/*
 * META_CHUNK_SIZE enum
 */

typedef enum META_CHUNK_SIZE {
META_CHUNK_SIZE_1KB                      = 0x00000000,
META_CHUNK_SIZE_2KB                      = 0x00000001,
META_CHUNK_SIZE_4KB                      = 0x00000002,
META_CHUNK_SIZE_8KB                      = 0x00000003,
} META_CHUNK_SIZE;

/*
 * META_LINEAR enum
 */

typedef enum META_LINEAR {
META_SURF_TILED                          = 0x00000000,
META_SURF_LINEAR                         = 0x00000001,
} META_LINEAR;

/*
 * MIN_CHUNK_SIZE enum
 */

typedef enum MIN_CHUNK_SIZE {
NO_MIN_CHUNK_SIZE                        = 0x00000000,
MIN_CHUNK_SIZE_256B                      = 0x00000001,
MIN_CHUNK_SIZE_512B                      = 0x00000002,
MIN_CHUNK_SIZE_1024B                     = 0x00000003,
} MIN_CHUNK_SIZE;

/*
 * MIN_META_CHUNK_SIZE enum
 */

typedef enum MIN_META_CHUNK_SIZE {
NO_MIN_META_CHUNK_SIZE                   = 0x00000000,
MIN_META_CHUNK_SIZE_64B                  = 0x00000001,
MIN_META_CHUNK_SIZE_128B                 = 0x00000002,
MIN_META_CHUNK_SIZE_256B                 = 0x00000003,
} MIN_META_CHUNK_SIZE;

/*
 * PIPE_ALIGNED enum
 */

typedef enum PIPE_ALIGNED {
PIPE_UNALIGNED_SURF                      = 0x00000000,
PIPE_ALIGNED_SURF                        = 0x00000001,
} PIPE_ALIGNED;

/*
 * PTE_BUFFER_MODE enum
 */

typedef enum PTE_BUFFER_MODE {
PTE_BUFFER_MODE_0                        = 0x00000000,
PTE_BUFFER_MODE_1                        = 0x00000001,
} PTE_BUFFER_MODE;

/*
 * PTE_ROW_HEIGHT_LINEAR enum
 */

typedef enum PTE_ROW_HEIGHT_LINEAR {
PTE_ROW_HEIGHT_LINEAR_8L                 = 0x00000000,
PTE_ROW_HEIGHT_LINEAR_16L                = 0x00000001,
PTE_ROW_HEIGHT_LINEAR_32L                = 0x00000002,
PTE_ROW_HEIGHT_LINEAR_64L                = 0x00000003,
PTE_ROW_HEIGHT_LINEAR_128L               = 0x00000004,
PTE_ROW_HEIGHT_LINEAR_256L               = 0x00000005,
PTE_ROW_HEIGHT_LINEAR_512L               = 0x00000006,
PTE_ROW_HEIGHT_LINEAR_1024L              = 0x00000007,
} PTE_ROW_HEIGHT_LINEAR;

/*
 * ROTATION_ANGLE enum
 */

typedef enum ROTATION_ANGLE {
ROTATE_0_DEGREES                         = 0x00000000,
ROTATE_90_DEGREES                        = 0x00000001,
ROTATE_180_DEGREES                       = 0x00000002,
ROTATE_270_DEGREES                       = 0x00000003,
} ROTATION_ANGLE;

/*
 * SWATH_HEIGHT enum
 */

typedef enum SWATH_HEIGHT {
SWATH_HEIGHT_1L                          = 0x00000000,
SWATH_HEIGHT_2L                          = 0x00000001,
SWATH_HEIGHT_4L                          = 0x00000002,
SWATH_HEIGHT_8L                          = 0x00000003,
SWATH_HEIGHT_16L                         = 0x00000004,
} SWATH_HEIGHT;

/*
 * USE_MALL_FOR_CURSOR enum
 */

typedef enum USE_MALL_FOR_CURSOR {
USE_MALL_FOR_CURSOR_0                    = 0x00000000,
USE_MALL_FOR_CURSOR_1                    = 0x00000001,
} USE_MALL_FOR_CURSOR;

/*
 * USE_MALL_FOR_PSTATE_CHANGE enum
 */

typedef enum USE_MALL_FOR_PSTATE_CHANGE {
USE_MALL_FOR_PSTATE_CHANGE_0             = 0x00000000,
USE_MALL_FOR_PSTATE_CHANGE_1             = 0x00000001,
} USE_MALL_FOR_PSTATE_CHANGE;

/*
 * USE_MALL_FOR_STATIC_SCREEN enum
 */

typedef enum USE_MALL_FOR_STATIC_SCREEN {
USE_MALL_FOR_STATIC_SCREEN_0             = 0x00000000,
USE_MALL_FOR_STATIC_SCREEN_1             = 0x00000001,
} USE_MALL_FOR_STATIC_SCREEN;

/*
 * VMPG_SIZE enum
 */

typedef enum VMPG_SIZE {
VMPG_SIZE_4KB                            = 0x00000000,
VMPG_SIZE_64KB                           = 0x00000001,
} VMPG_SIZE;

/*
 * VM_GROUP_SIZE enum
 */

typedef enum VM_GROUP_SIZE {
VM_GROUP_SIZE_64B                        = 0x00000000,
VM_GROUP_SIZE_128B                       = 0x00000001,
VM_GROUP_SIZE_256B                       = 0x00000002,
VM_GROUP_SIZE_512B                       = 0x00000003,
VM_GROUP_SIZE_1024B                      = 0x00000004,
VM_GROUP_SIZE_2048B                      = 0x00000005,
} VM_GROUP_SIZE;

/*******************************************************
 * HUBPREQ Enums
 *******************************************************/

/*
 * DFQ_MIN_FREE_ENTRIES enum
 */

typedef enum DFQ_MIN_FREE_ENTRIES {
DFQ_MIN_FREE_ENTRIES_0                   = 0x00000000,
DFQ_MIN_FREE_ENTRIES_1                   = 0x00000001,
DFQ_MIN_FREE_ENTRIES_2                   = 0x00000002,
DFQ_MIN_FREE_ENTRIES_3                   = 0x00000003,
DFQ_MIN_FREE_ENTRIES_4                   = 0x00000004,
DFQ_MIN_FREE_ENTRIES_5                   = 0x00000005,
DFQ_MIN_FREE_ENTRIES_6                   = 0x00000006,
DFQ_MIN_FREE_ENTRIES_7                   = 0x00000007,
} DFQ_MIN_FREE_ENTRIES;

/*
 * DFQ_NUM_ENTRIES enum
 */

typedef enum DFQ_NUM_ENTRIES {
DFQ_NUM_ENTRIES_0                        = 0x00000000,
DFQ_NUM_ENTRIES_1                        = 0x00000001,
DFQ_NUM_ENTRIES_2                        = 0x00000002,
DFQ_NUM_ENTRIES_3                        = 0x00000003,
DFQ_NUM_ENTRIES_4                        = 0x00000004,
DFQ_NUM_ENTRIES_5                        = 0x00000005,
DFQ_NUM_ENTRIES_6                        = 0x00000006,
DFQ_NUM_ENTRIES_7                        = 0x00000007,
DFQ_NUM_ENTRIES_8                        = 0x00000008,
} DFQ_NUM_ENTRIES;

/*
 * DFQ_SIZE enum
 */

typedef enum DFQ_SIZE {
DFQ_SIZE_0                               = 0x00000000,
DFQ_SIZE_1                               = 0x00000001,
DFQ_SIZE_2                               = 0x00000002,
DFQ_SIZE_3                               = 0x00000003,
DFQ_SIZE_4                               = 0x00000004,
DFQ_SIZE_5                               = 0x00000005,
DFQ_SIZE_6                               = 0x00000006,
DFQ_SIZE_7                               = 0x00000007,
} DFQ_SIZE;

/*
 * DMDATA_VM_DONE enum
 */

typedef enum DMDATA_VM_DONE {
DMDATA_VM_IS_NOT_DONE                    = 0x00000000,
DMDATA_VM_IS_DONE                        = 0x00000001,
} DMDATA_VM_DONE;

/*
 * EXPANSION_MODE enum
 */

typedef enum EXPANSION_MODE {
EXPANSION_MODE_ZERO                      = 0x00000000,
EXPANSION_MODE_CONSERVATIVE              = 0x00000001,
EXPANSION_MODE_OPTIMAL                   = 0x00000002,
} EXPANSION_MODE;

/*
 * FLIP_RATE enum
 */

typedef enum FLIP_RATE {
FLIP_RATE_0                              = 0x00000000,
FLIP_RATE_1                              = 0x00000001,
FLIP_RATE_2                              = 0x00000002,
FLIP_RATE_3                              = 0x00000003,
FLIP_RATE_4                              = 0x00000004,
FLIP_RATE_5                              = 0x00000005,
FLIP_RATE_6                              = 0x00000006,
FLIP_RATE_7                              = 0x00000007,
} FLIP_RATE;

/*
 * INT_MASK enum
 */

typedef enum INT_MASK {
INT_DISABLED                             = 0x00000000,
INT_ENABLED                              = 0x00000001,
} INT_MASK;

/*
 * PIPE_IN_FLUSH_URGENT enum
 */

typedef enum PIPE_IN_FLUSH_URGENT {
PIPE_IN_FLUSH_URGENT_ENABLE              = 0x00000000,
PIPE_IN_FLUSH_URGENT_DISABLE             = 0x00000001,
} PIPE_IN_FLUSH_URGENT;

/*
 * PRQ_MRQ_FLUSH_URGENT enum
 */

typedef enum PRQ_MRQ_FLUSH_URGENT {
PRQ_MRQ_FLUSH_URGENT_ENABLE              = 0x00000000,
PRQ_MRQ_FLUSH_URGENT_DISABLE             = 0x00000001,
} PRQ_MRQ_FLUSH_URGENT;

/*
 * ROW_TTU_MODE enum
 */

typedef enum ROW_TTU_MODE {
END_OF_ROW_MODE                          = 0x00000000,
WATERMARK_MODE                           = 0x00000001,
} ROW_TTU_MODE;

/*
 * SURFACE_DCC enum
 */

typedef enum SURFACE_DCC {
SURFACE_IS_NOT_DCC                       = 0x00000000,
SURFACE_IS_DCC                           = 0x00000001,
} SURFACE_DCC;

/*
 * SURFACE_DCC_IND_128B enum
 */

typedef enum SURFACE_DCC_IND_128B {
SURFACE_DCC_IS_NOT_IND_128B              = 0x00000000,
SURFACE_DCC_IS_IND_128B                  = 0x00000001,
} SURFACE_DCC_IND_128B;

/*
 * SURFACE_DCC_IND_64B enum
 */

typedef enum SURFACE_DCC_IND_64B {
SURFACE_DCC_IS_NOT_IND_64B               = 0x00000000,
SURFACE_DCC_IS_IND_64B                   = 0x00000001,
} SURFACE_DCC_IND_64B;

/*
 * SURFACE_DCC_IND_BLK enum
 */

typedef enum SURFACE_DCC_IND_BLK {
SURFACE_DCC_BLOCK_IS_UNCONSTRAINED       = 0x00000000,
SURFACE_DCC_BLOCK_IS_IND_64B             = 0x00000001,
SURFACE_DCC_BLOCK_IS_IND_128B            = 0x00000002,
SURFACE_DCC_BLOCK_IS_IND_64B_NO_128BCL   = 0x00000003,
} SURFACE_DCC_IND_BLK;

/*
 * SURFACE_FLIP_AWAY_INT_TYPE enum
 */

typedef enum SURFACE_FLIP_AWAY_INT_TYPE {
SURFACE_FLIP_AWAY_INT_LEVEL              = 0x00000000,
SURFACE_FLIP_AWAY_INT_PULSE              = 0x00000001,
} SURFACE_FLIP_AWAY_INT_TYPE;

/*
 * SURFACE_FLIP_EXEC_DEBUG_MODE enum
 */

typedef enum SURFACE_FLIP_EXEC_DEBUG_MODE {
SURFACE_FLIP_EXEC_NORMAL_MODE            = 0x00000000,
SURFACE_FLIP_EXEC_DEBUG_MODE_ENABLE      = 0x00000001,
} SURFACE_FLIP_EXEC_DEBUG_MODE;

/*
 * SURFACE_FLIP_INT_TYPE enum
 */

typedef enum SURFACE_FLIP_INT_TYPE {
SURFACE_FLIP_INT_LEVEL                   = 0x00000000,
SURFACE_FLIP_INT_PULSE                   = 0x00000001,
} SURFACE_FLIP_INT_TYPE;

/*
 * SURFACE_FLIP_IN_STEREOSYNC enum
 */

typedef enum SURFACE_FLIP_IN_STEREOSYNC {
SURFACE_FLIP_NOT_IN_STEREOSYNC_MODE      = 0x00000000,
SURFACE_FLIP_IN_STEREOSYNC_MODE          = 0x00000001,
} SURFACE_FLIP_IN_STEREOSYNC;

/*
 * SURFACE_FLIP_MODE_FOR_STEREOSYNC enum
 */

typedef enum SURFACE_FLIP_MODE_FOR_STEREOSYNC {
FLIP_ANY_FRAME                           = 0x00000000,
FLIP_LEFT_EYE                            = 0x00000001,
FLIP_RIGHT_EYE                           = 0x00000002,
SURFACE_FLIP_MODE_FOR_STEREOSYNC_RESERVED = 0x00000003,
} SURFACE_FLIP_MODE_FOR_STEREOSYNC;

/*
 * SURFACE_FLIP_STEREO_SELECT_DISABLE enum
 */

typedef enum SURFACE_FLIP_STEREO_SELECT_DISABLE {
SURFACE_FLIP_STEREO_SELECT_ENABLED       = 0x00000000,
SURFACE_FLIP_STEREO_SELECT_DISABLED      = 0x00000001,
} SURFACE_FLIP_STEREO_SELECT_DISABLE;

/*
 * SURFACE_FLIP_STEREO_SELECT_POLARITY enum
 */

typedef enum SURFACE_FLIP_STEREO_SELECT_POLARITY {
SURFACE_FLIP_STEREO_SELECT_POLARITY_NOT_INVERT = 0x00000000,
SURFACE_FLIP_STEREO_SELECT_POLARITY_INVERT = 0x00000001,
} SURFACE_FLIP_STEREO_SELECT_POLARITY;

/*
 * SURFACE_FLIP_TYPE enum
 */

typedef enum SURFACE_FLIP_TYPE {
SURFACE_V_FLIP                           = 0x00000000,
SURFACE_I_FLIP                           = 0x00000001,
} SURFACE_FLIP_TYPE;

/*
 * SURFACE_FLIP_VUPDATE_SKIP_NUM enum
 */

typedef enum SURFACE_FLIP_VUPDATE_SKIP_NUM {
SURFACE_FLIP_VUPDATE_SKIP_NUM_0          = 0x00000000,
SURFACE_FLIP_VUPDATE_SKIP_NUM_1          = 0x00000001,
SURFACE_FLIP_VUPDATE_SKIP_NUM_2          = 0x00000002,
SURFACE_FLIP_VUPDATE_SKIP_NUM_3          = 0x00000003,
SURFACE_FLIP_VUPDATE_SKIP_NUM_4          = 0x00000004,
SURFACE_FLIP_VUPDATE_SKIP_NUM_5          = 0x00000005,
SURFACE_FLIP_VUPDATE_SKIP_NUM_6          = 0x00000006,
SURFACE_FLIP_VUPDATE_SKIP_NUM_7          = 0x00000007,
SURFACE_FLIP_VUPDATE_SKIP_NUM_8          = 0x00000008,
SURFACE_FLIP_VUPDATE_SKIP_NUM_9          = 0x00000009,
SURFACE_FLIP_VUPDATE_SKIP_NUM_10         = 0x0000000a,
SURFACE_FLIP_VUPDATE_SKIP_NUM_11         = 0x0000000b,
SURFACE_FLIP_VUPDATE_SKIP_NUM_12         = 0x0000000c,
SURFACE_FLIP_VUPDATE_SKIP_NUM_13         = 0x0000000d,
SURFACE_FLIP_VUPDATE_SKIP_NUM_14         = 0x0000000e,
SURFACE_FLIP_VUPDATE_SKIP_NUM_15         = 0x0000000f,
} SURFACE_FLIP_VUPDATE_SKIP_NUM;

/*
 * SURFACE_INUSE_RAED_NO_LATCH enum
 */

typedef enum SURFACE_INUSE_RAED_NO_LATCH {
SURFACE_INUSE_IS_LATCHED                 = 0x00000000,
SURFACE_INUSE_IS_NOT_LATCHED             = 0x00000001,
} SURFACE_INUSE_RAED_NO_LATCH;

/*
 * SURFACE_TMZ enum
 */

typedef enum SURFACE_TMZ {
SURFACE_IS_NOT_TMZ                       = 0x00000000,
SURFACE_IS_TMZ                           = 0x00000001,
} SURFACE_TMZ;

/*
 * SURFACE_UPDATE_LOCK enum
 */

typedef enum SURFACE_UPDATE_LOCK {
SURFACE_UPDATE_IS_UNLOCKED               = 0x00000000,
SURFACE_UPDATE_IS_LOCKED                 = 0x00000001,
} SURFACE_UPDATE_LOCK;

/*******************************************************
 * HUBPRET Enums
 *******************************************************/

/*
 * CROSSBAR_FOR_ALPHA enum
 */

typedef enum CROSSBAR_FOR_ALPHA {
ALPHA_DATA_ONTO_ALPHA_PORT               = 0x00000000,
Y_G_DATA_ONTO_ALPHA_PORT                 = 0x00000001,
CB_B_DATA_ONTO_ALPHA_PORT                = 0x00000002,
CR_R_DATA_ONTO_ALPHA_PORT                = 0x00000003,
} CROSSBAR_FOR_ALPHA;

/*
 * CROSSBAR_FOR_CB_B enum
 */

typedef enum CROSSBAR_FOR_CB_B {
ALPHA_DATA_ONTO_CB_B_PORT                = 0x00000000,
Y_G_DATA_ONTO_CB_B_PORT                  = 0x00000001,
CB_B_DATA_ONTO_CB_B_PORT                 = 0x00000002,
CR_R_DATA_ONTO_CB_B_PORT                 = 0x00000003,
} CROSSBAR_FOR_CB_B;

/*
 * CROSSBAR_FOR_CR_R enum
 */

typedef enum CROSSBAR_FOR_CR_R {
ALPHA_DATA_ONTO_CR_R_PORT                = 0x00000000,
Y_G_DATA_ONTO_CR_R_PORT                  = 0x00000001,
CB_B_DATA_ONTO_CR_R_PORT                 = 0x00000002,
CR_R_DATA_ONTO_CR_R_PORT                 = 0x00000003,
} CROSSBAR_FOR_CR_R;

/*
 * CROSSBAR_FOR_Y_G enum
 */

typedef enum CROSSBAR_FOR_Y_G {
ALPHA_DATA_ONTO_Y_G_PORT                 = 0x00000000,
Y_G_DATA_ONTO_Y_G_PORT                   = 0x00000001,
CB_B_DATA_ONTO_Y_G_PORT                  = 0x00000002,
CR_R_DATA_ONTO_Y_G_PORT                  = 0x00000003,
} CROSSBAR_FOR_Y_G;

/*
 * DETILE_BUFFER_PACKER_ENABLE enum
 */

typedef enum DETILE_BUFFER_PACKER_ENABLE {
DETILE_BUFFER_PACKER_IS_DISABLE          = 0x00000000,
DETILE_BUFFER_PACKER_IS_ENABLE           = 0x00000001,
} DETILE_BUFFER_PACKER_ENABLE;

/*
 * MEM_PWR_DIS_MODE enum
 */

typedef enum MEM_PWR_DIS_MODE {
MEM_POWER_DIS_MODE_ENABLE                = 0x00000000,
MEM_POWER_DIS_MODE_DISABLE               = 0x00000001,
} MEM_PWR_DIS_MODE;

/*
 * MEM_PWR_FORCE_MODE enum
 */

typedef enum MEM_PWR_FORCE_MODE {
MEM_POWER_FORCE_MODE_OFF                 = 0x00000000,
MEM_POWER_FORCE_MODE_LIGHT_SLEEP         = 0x00000001,
MEM_POWER_FORCE_MODE_DEEP_SLEEP          = 0x00000002,
MEM_POWER_FORCE_MODE_SHUT_DOWN           = 0x00000003,
} MEM_PWR_FORCE_MODE;

/*
 * MEM_PWR_STATUS enum
 */

typedef enum MEM_PWR_STATUS {
MEM_POWER_STATUS_ON                      = 0x00000000,
MEM_POWER_STATUS_LIGHT_SLEEP             = 0x00000001,
MEM_POWER_STATUS_DEEP_SLEEP              = 0x00000002,
MEM_POWER_STATUS_SHUT_DOWN               = 0x00000003,
} MEM_PWR_STATUS;

/*
 * PIPE_INT_MASK_MODE enum
 */

typedef enum PIPE_INT_MASK_MODE {
PIPE_INT_MASK_MODE_DISABLE               = 0x00000000,
PIPE_INT_MASK_MODE_ENABLE                = 0x00000001,
} PIPE_INT_MASK_MODE;

/*
 * PIPE_INT_TYPE_MODE enum
 */

typedef enum PIPE_INT_TYPE_MODE {
PIPE_INT_TYPE_MODE_DISABLE               = 0x00000000,
PIPE_INT_TYPE_MODE_ENABLE                = 0x00000001,
} PIPE_INT_TYPE_MODE;

/*
 * PIXCDC_MEM_PWR_LIGHT_SLEEP_MODE enum
 */

typedef enum PIXCDC_MEM_PWR_LIGHT_SLEEP_MODE {
PIXCDC_MEM_POWER_LIGHT_SLEEP_MODE_OFF    = 0x00000000,
PIXCDC_MEM_POWER_LIGHT_SLEEP_MODE_1      = 0x00000001,
} PIXCDC_MEM_PWR_LIGHT_SLEEP_MODE;

/*******************************************************
 * CURSOR Enums
 *******************************************************/

/*
 * CROB_MEM_PWR_LIGHT_SLEEP_MODE enum
 */

typedef enum CROB_MEM_PWR_LIGHT_SLEEP_MODE {
CROB_MEM_POWER_LIGHT_SLEEP_MODE_OFF      = 0x00000000,
CROB_MEM_POWER_LIGHT_SLEEP_MODE_1        = 0x00000001,
CROB_MEM_POWER_LIGHT_SLEEP_MODE_2        = 0x00000002,
} CROB_MEM_PWR_LIGHT_SLEEP_MODE;

/*
 * CURSOR_2X_MAGNIFY enum
 */

typedef enum CURSOR_2X_MAGNIFY {
CURSOR_2X_MAGNIFY_IS_DISABLE             = 0x00000000,
CURSOR_2X_MAGNIFY_IS_ENABLE              = 0x00000001,
} CURSOR_2X_MAGNIFY;

/*
 * CURSOR_ENABLE enum
 */

typedef enum CURSOR_ENABLE {
CURSOR_IS_DISABLE                        = 0x00000000,
CURSOR_IS_ENABLE                         = 0x00000001,
} CURSOR_ENABLE;

/*
 * CURSOR_LINES_PER_CHUNK enum
 */

typedef enum CURSOR_LINES_PER_CHUNK {
CURSOR_LINE_PER_CHUNK_1                  = 0x00000000,
CURSOR_LINE_PER_CHUNK_2                  = 0x00000001,
CURSOR_LINE_PER_CHUNK_4                  = 0x00000002,
CURSOR_LINE_PER_CHUNK_8                  = 0x00000003,
CURSOR_LINE_PER_CHUNK_16                 = 0x00000004,
} CURSOR_LINES_PER_CHUNK;

/*
 * CURSOR_MODE enum
 */

typedef enum CURSOR_MODE {
CURSOR_MONO_2BIT                         = 0x00000000,
CURSOR_COLOR_24BIT_1BIT_AND              = 0x00000001,
CURSOR_COLOR_24BIT_8BIT_ALPHA_PREMULT    = 0x00000002,
CURSOR_COLOR_24BIT_8BIT_ALPHA_UNPREMULT  = 0x00000003,
CURSOR_COLOR_64BIT_FP_PREMULT            = 0x00000004,
CURSOR_COLOR_64BIT_FP_UNPREMULT          = 0x00000005,
} CURSOR_MODE;

/*
 * CURSOR_PERFMON_LATENCY_MEASURE_EN enum
 */

typedef enum CURSOR_PERFMON_LATENCY_MEASURE_EN {
CURSOR_PERFMON_LATENCY_MEASURE_IS_DISABLED = 0x00000000,
CURSOR_PERFMON_LATENCY_MEASURE_IS_ENABLED = 0x00000001,
} CURSOR_PERFMON_LATENCY_MEASURE_EN;

/*
 * CURSOR_PERFMON_LATENCY_MEASURE_SEL enum
 */

typedef enum CURSOR_PERFMON_LATENCY_MEASURE_SEL {
CURSOR_PERFMON_LATENCY_MEASURE_MC_LATENCY = 0x00000000,
CURSOR_PERFMON_LATENCY_MEASURE_CROB_LATENCY = 0x00000001,
} CURSOR_PERFMON_LATENCY_MEASURE_SEL;

/*
 * CURSOR_PITCH enum
 */

typedef enum CURSOR_PITCH {
CURSOR_PITCH_64_PIXELS                   = 0x00000000,
CURSOR_PITCH_128_PIXELS                  = 0x00000001,
CURSOR_PITCH_256_PIXELS                  = 0x00000002,
} CURSOR_PITCH;

/*
 * CURSOR_REQ_MODE enum
 */

typedef enum CURSOR_REQ_MODE {
CURSOR_REQUEST_NORMALLY                  = 0x00000000,
CURSOR_REQUEST_EARLY                     = 0x00000001,
} CURSOR_REQ_MODE;

/*
 * CURSOR_SNOOP enum
 */

typedef enum CURSOR_SNOOP {
CURSOR_IS_NOT_SNOOP                      = 0x00000000,
CURSOR_IS_SNOOP                          = 0x00000001,
} CURSOR_SNOOP;

/*
 * CURSOR_STEREO_EN enum
 */

typedef enum CURSOR_STEREO_EN {
CURSOR_STEREO_IS_DISABLED                = 0x00000000,
CURSOR_STEREO_IS_ENABLED                 = 0x00000001,
} CURSOR_STEREO_EN;

/*
 * CURSOR_SURFACE_TMZ enum
 */

typedef enum CURSOR_SURFACE_TMZ {
CURSOR_SURFACE_IS_NOT_TMZ                = 0x00000000,
CURSOR_SURFACE_IS_TMZ                    = 0x00000001,
} CURSOR_SURFACE_TMZ;

/*
 * CURSOR_SYSTEM enum
 */

typedef enum CURSOR_SYSTEM {
CURSOR_IN_SYSTEM_PHYSICAL_ADDRESS        = 0x00000000,
CURSOR_IN_GUEST_PHYSICAL_ADDRESS         = 0x00000001,
} CURSOR_SYSTEM;

/*
 * CURSOR_XY_POSITION_ROTATION_AND_MIRRORING_BYPASS enum
 */

typedef enum CURSOR_XY_POSITION_ROTATION_AND_MIRRORING_BYPASS {
CURSOR_XY_POSITION_ROTATION_AND_MIRRORING_BYPASS_0 = 0x00000000,
CURSOR_XY_POSITION_ROTATION_AND_MIRRORING_BYPASS_1 = 0x00000001,
} CURSOR_XY_POSITION_ROTATION_AND_MIRRORING_BYPASS;

/*
 * DMDATA_DONE enum
 */

typedef enum DMDATA_DONE {
DMDATA_NOT_SENT_TO_DIG                   = 0x00000000,
DMDATA_SENT_TO_DIG                       = 0x00000001,
} DMDATA_DONE;

/*
 * DMDATA_MODE enum
 */

typedef enum DMDATA_MODE {
DMDATA_SOFTWARE_UPDATE_MODE              = 0x00000000,
DMDATA_HARDWARE_UPDATE_MODE              = 0x00000001,
} DMDATA_MODE;

/*
 * DMDATA_QOS_MODE enum
 */

typedef enum DMDATA_QOS_MODE {
DMDATA_QOS_LEVEL_FROM_TTU                = 0x00000000,
DMDATA_QOS_LEVEL_FROM_SOFTWARE           = 0x00000001,
} DMDATA_QOS_MODE;

/*
 * DMDATA_REPEAT enum
 */

typedef enum DMDATA_REPEAT {
DMDATA_USE_FOR_CURRENT_FRAME_ONLY        = 0x00000000,
DMDATA_USE_FOR_CURRENT_AND_FUTURE_FRAMES = 0x00000001,
} DMDATA_REPEAT;

/*
 * DMDATA_UNDERFLOW enum
 */

typedef enum DMDATA_UNDERFLOW {
DMDATA_NOT_UNDERFLOW                     = 0x00000000,
DMDATA_UNDERFLOWED                       = 0x00000001,
} DMDATA_UNDERFLOW;

/*
 * DMDATA_UNDERFLOW_CLEAR enum
 */

typedef enum DMDATA_UNDERFLOW_CLEAR {
DMDATA_DONT_CLEAR                        = 0x00000000,
DMDATA_CLEAR_UNDERFLOW_STATUS            = 0x00000001,
} DMDATA_UNDERFLOW_CLEAR;

/*
 * DMDATA_UPDATED enum
 */

typedef enum DMDATA_UPDATED {
DMDATA_NOT_UPDATED                       = 0x00000000,
DMDATA_WAS_UPDATED                       = 0x00000001,
} DMDATA_UPDATED;

/*******************************************************
 * HUBBUB_SDPIF Enums
 *******************************************************/

/*
 * RESPONSE_STATUS enum
 */

typedef enum RESPONSE_STATUS {
OKAY                                     = 0x00000000,
EXOKAY                                   = 0x00000001,
SLVERR                                   = 0x00000002,
DECERR                                   = 0x00000003,
EARLY                                    = 0x00000004,
OKAY_NODATA                              = 0x00000005,
PROTVIOL                                 = 0x00000006,
TRANSERR                                 = 0x00000007,
CMPTO                                    = 0x00000008,
CRS                                      = 0x0000000c,
} RESPONSE_STATUS;

/*******************************************************
 * HUBBUB_RET_PATH Enums
 *******************************************************/

/*
 * DCHUBBUB_DET_MEM_PWR_LIGHT_SLEEP_MODE enum
 */

typedef enum DCHUBBUB_DET_MEM_PWR_LIGHT_SLEEP_MODE {
DCHUBBUB_DET_MEM_POWER_LIGHT_SLEEP_MODE_OFF = 0x00000000,
DCHUBBUB_DET_MEM_POWER_LIGHT_SLEEP_MODE_1 = 0x00000001,
DCHUBBUB_DET_MEM_POWER_LIGHT_SLEEP_MODE_2 = 0x00000002,
} DCHUBBUB_DET_MEM_PWR_LIGHT_SLEEP_MODE;

/*
 * DCHUBBUB_MEM_PWR_DIS_MODE enum
 */

typedef enum DCHUBBUB_MEM_PWR_DIS_MODE {
DCHUBBUB_MEM_POWER_DIS_MODE_ENABLE       = 0x00000000,
DCHUBBUB_MEM_POWER_DIS_MODE_DISABLE      = 0x00000001,
} DCHUBBUB_MEM_PWR_DIS_MODE;

/*
 * DCHUBBUB_MEM_PWR_MODE enum
 */

typedef enum DCHUBBUB_MEM_PWR_MODE {
DCHUBBUB_MEM_POWER_MODE_OFF              = 0x00000000,
DCHUBBUB_MEM_POWER_MODE_LIGHT_SLEEP      = 0x00000001,
DCHUBBUB_MEM_POWER_MODE_DEEP_SLEEP       = 0x00000002,
DCHUBBUB_MEM_POWER_MODE_SHUT_DOWN        = 0x00000003,
} DCHUBBUB_MEM_PWR_MODE;

/*******************************************************
 * MPC_CFG Enums
 *******************************************************/

/*
 * MPC_CFG_ADR_CFG_CUR_VUPDATE_LOCK_SET enum
 */

typedef enum MPC_CFG_ADR_CFG_CUR_VUPDATE_LOCK_SET {
MPC_CFG_ADR_CFG_CUR_VUPDATE_LOCK_SET_FALSE = 0x00000000,
MPC_CFG_ADR_CFG_CUR_VUPDATE_LOCK_SET_TRUE = 0x00000001,
} MPC_CFG_ADR_CFG_CUR_VUPDATE_LOCK_SET;

/*
 * MPC_CFG_ADR_CFG_VUPDATE_LOCK_SET enum
 */

typedef enum MPC_CFG_ADR_CFG_VUPDATE_LOCK_SET {
MPC_CFG_ADR_CFG_VUPDATE_LOCK_SET_FALSE   = 0x00000000,
MPC_CFG_ADR_CFG_VUPDATE_LOCK_SET_TRUE    = 0x00000001,
} MPC_CFG_ADR_CFG_VUPDATE_LOCK_SET;

/*
 * MPC_CFG_ADR_VUPDATE_LOCK_SET enum
 */

typedef enum MPC_CFG_ADR_VUPDATE_LOCK_SET {
MPC_CFG_ADR_VUPDATE_LOCK_SET_FALSE       = 0x00000000,
MPC_CFG_ADR_VUPDATE_LOCK_SET_TRUE        = 0x00000001,
} MPC_CFG_ADR_VUPDATE_LOCK_SET;

/*
 * MPC_CFG_CFG_VUPDATE_LOCK_SET enum
 */

typedef enum MPC_CFG_CFG_VUPDATE_LOCK_SET {
MPC_CFG_CFG_VUPDATE_LOCK_SET_FALSE       = 0x00000000,
MPC_CFG_CFG_VUPDATE_LOCK_SET_TRUE        = 0x00000001,
} MPC_CFG_CFG_VUPDATE_LOCK_SET;

/*
 * MPC_CFG_CUR_VUPDATE_LOCK_SET enum
 */

typedef enum MPC_CFG_CUR_VUPDATE_LOCK_SET {
MPC_CFG_CUR_VUPDATE_LOCK_SET_FALSE       = 0x00000000,
MPC_CFG_CUR_VUPDATE_LOCK_SET_TRUE        = 0x00000001,
} MPC_CFG_CUR_VUPDATE_LOCK_SET;

/*
 * MPC_CFG_MPC_TEST_CLK_SEL enum
 */

typedef enum MPC_CFG_MPC_TEST_CLK_SEL {
MPC_CFG_MPC_TEST_CLK_SEL_0               = 0x00000000,
MPC_CFG_MPC_TEST_CLK_SEL_1               = 0x00000001,
MPC_CFG_MPC_TEST_CLK_SEL_2               = 0x00000002,
MPC_CFG_MPC_TEST_CLK_SEL_3               = 0x00000003,
} MPC_CFG_MPC_TEST_CLK_SEL;

/*
 * MPC_CFG_TEST_DEBUG_INDEX_MPC_CFG_TEST_DEBUG_WRITE_EN enum
 */

typedef enum MPC_CFG_TEST_DEBUG_INDEX_MPC_CFG_TEST_DEBUG_WRITE_EN {
MPC_CFG_TEST_DEBUG_INDEX_MPC_CFG_TEST_DEBUG_WRITE_EN_FALSE = 0x00000000,
MPC_CFG_TEST_DEBUG_INDEX_MPC_CFG_TEST_DEBUG_WRITE_EN_TRUE = 0x00000001,
} MPC_CFG_TEST_DEBUG_INDEX_MPC_CFG_TEST_DEBUG_WRITE_EN;

/*
 * MPC_CRC_CALC_INTERLACE_MODE enum
 */

typedef enum MPC_CRC_CALC_INTERLACE_MODE {
MPC_CRC_INTERLACE_MODE_TOP               = 0x00000000,
MPC_CRC_INTERLACE_MODE_BOTTOM            = 0x00000001,
MPC_CRC_INTERLACE_MODE_BOTH_RESET_BOTTOM = 0x00000002,
MPC_CRC_INTERLACE_MODE_BOTH_RESET_EACH   = 0x00000003,
} MPC_CRC_CALC_INTERLACE_MODE;

/*
 * MPC_CRC_CALC_MODE enum
 */

typedef enum MPC_CRC_CALC_MODE {
MPC_CRC_ONE_SHOT_MODE                    = 0x00000000,
MPC_CRC_CONTINUOUS_MODE                  = 0x00000001,
} MPC_CRC_CALC_MODE;

/*
 * MPC_CRC_CALC_STEREO_MODE enum
 */

typedef enum MPC_CRC_CALC_STEREO_MODE {
MPC_CRC_STEREO_MODE_LEFT                 = 0x00000000,
MPC_CRC_STEREO_MODE_RIGHT                = 0x00000001,
MPC_CRC_STEREO_MODE_BOTH_RESET_RIGHT     = 0x00000002,
MPC_CRC_STEREO_MODE_BOTH_RESET_EACH      = 0x00000003,
} MPC_CRC_CALC_STEREO_MODE;

/*
 * MPC_CRC_SOURCE_SELECT enum
 */

typedef enum MPC_CRC_SOURCE_SELECT {
MPC_CRC_SOURCE_SEL_DPP                   = 0x00000000,
MPC_CRC_SOURCE_SEL_OPP                   = 0x00000001,
MPC_CRC_SOURCE_SEL_DWB                   = 0x00000002,
MPC_CRC_SOURCE_SEL_OTHER                 = 0x00000003,
} MPC_CRC_SOURCE_SELECT;

/*
 * MPC_DEBUG_BUS1_DATA_SELECT enum
 */

typedef enum MPC_DEBUG_BUS1_DATA_SELECT {
MPC_DEBUG_BUS1_DATA_SELECT_MPC_CFG       = 0x00000000,
MPC_DEBUG_BUS1_DATA_SELECT_MPC_CONT      = 0x00000001,
MPC_DEBUG_BUS1_DATA_SELECT_MPC_RSV1      = 0x00000002,
MPC_DEBUG_BUS1_DATA_SELECT_MPC_RSV       = 0x00000003,
} MPC_DEBUG_BUS1_DATA_SELECT;

/*
 * MPC_DEBUG_BUS2_DATA_SELECT enum
 */

typedef enum MPC_DEBUG_BUS2_DATA_SELECT {
MPC_DEBUG_BUS2_DATA_SELECT_MPCC          = 0x00000000,
MPC_DEBUG_BUS2_DATA_SELECT_MPCC_CONT     = 0x00000001,
MPC_DEBUG_BUS2_DATA_SELECT_MPCC_MCM      = 0x00000002,
MPC_DEBUG_BUS2_DATA_SELECT_RES           = 0x00000003,
} MPC_DEBUG_BUS2_DATA_SELECT;

/*
 * MPC_DEBUG_BUS_DIRECT_OUT_DATA_SELECT enum
 */

typedef enum MPC_DEBUG_BUS_DIRECT_OUT_DATA_SELECT {
MPC_DEBUG_BUS_DIRECT_OUT_DATA_SELECT_MPC_DEBUG_ID = 0x00000000,
MPC_DEBUG_BUS_DIRECT_OUT_DATA_SELECT_MPCC_DEBUG_ID = 0x00000001,
MPC_DEBUG_BUS_DIRECT_OUT_DATA_SELECT_MPCC_OGAM_DEBUG_ID = 0x00000002,
MPC_DEBUG_BUS_DIRECT_OUT_DATA_SELECT_MPC_OCSC_DEBUG_ID = 0x00000003,
MPC_DEBUG_BUS_DIRECT_OUT_DATA_SELECT_SFR_DEBUG_DATA = 0x00000004,
MPC_DEBUG_BUS_DIRECT_OUT_DATA_SELECT_SFT_DEBUG_DATA = 0x00000005,
MPC_DEBUG_BUS_DIRECT_OUT_DATA_SELECT_RSV1 = 0x00000006,
MPC_DEBUG_BUS_DIRECT_OUT_DATA_SELECT_MPCC_MCM_DEBUG_ID = 0x00000007,
} MPC_DEBUG_BUS_DIRECT_OUT_DATA_SELECT;

/*
 * MPC_DEBUG_BUS_MPCC_BYTE_SELECT enum
 */

typedef enum MPC_DEBUG_BUS_MPCC_BYTE_SELECT {
MPC_DEBUG_BUS_MPCC_BYTE0                 = 0x00000000,
MPC_DEBUG_BUS_MPCC_BYTE1                 = 0x00000001,
MPC_DEBUG_BUS_MPCC_BYTE2                 = 0x00000002,
MPC_DEBUG_BUS_MPCC_BYTE3                 = 0x00000003,
} MPC_DEBUG_BUS_MPCC_BYTE_SELECT;

/*******************************************************
 * MPC_OCSC Enums
 *******************************************************/

/*
 * MPC_OCSC_COEF_FORMAT enum
 */

typedef enum MPC_OCSC_COEF_FORMAT {
MPC_OCSC_COEF_FORMAT_S2_13               = 0x00000000,
MPC_OCSC_COEF_FORMAT_S3_12               = 0x00000001,
} MPC_OCSC_COEF_FORMAT;

/*
 * MPC_OCSC_TEST_DEBUG_INDEX_MPC_OCSC_TEST_DEBUG_WRITE_EN enum
 */

typedef enum MPC_OCSC_TEST_DEBUG_INDEX_MPC_OCSC_TEST_DEBUG_WRITE_EN {
MPC_OCSC_TEST_DEBUG_INDEX_MPC_OCSC_TEST_DEBUG_WRITE_EN_FALSE = 0x00000000,
MPC_OCSC_TEST_DEBUG_INDEX_MPC_OCSC_TEST_DEBUG_WRITE_EN_TRUE = 0x00000001,
} MPC_OCSC_TEST_DEBUG_INDEX_MPC_OCSC_TEST_DEBUG_WRITE_EN;

/*
 * MPC_OUT_CSC_MODE enum
 */

typedef enum MPC_OUT_CSC_MODE {
MPC_OUT_CSC_MODE_0                       = 0x00000000,
MPC_OUT_CSC_MODE_1                       = 0x00000001,
MPC_OUT_CSC_MODE_2                       = 0x00000002,
MPC_OUT_CSC_MODE_RSV                     = 0x00000003,
} MPC_OUT_CSC_MODE;

/*
 * MPC_OUT_DENORM_CONTROL_MPC_OUT_DENORM_MODE enum
 */

typedef enum MPC_OUT_DENORM_CONTROL_MPC_OUT_DENORM_MODE {
MPC_OUT_DENORM_CONTROL_MPC_OUT_DENORM_BYPASS = 0x00000000,
MPC_OUT_DENORM_CONTROL_MPC_OUT_DENORM_6BITS = 0x00000001,
MPC_OUT_DENORM_CONTROL_MPC_OUT_DENORM_8BITS = 0x00000002,
MPC_OUT_DENORM_CONTROL_MPC_OUT_DENORM_9BITS = 0x00000003,
MPC_OUT_DENORM_CONTROL_MPC_OUT_DENORM_10BITS = 0x00000004,
MPC_OUT_DENORM_CONTROL_MPC_OUT_DENORM_11BITS = 0x00000005,
MPC_OUT_DENORM_CONTROL_MPC_OUT_DENORM_12BITS = 0x00000006,
MPC_OUT_DENORM_CONTROL_MPC_OUT_DENORM_PASSTHROUGH = 0x00000007,
} MPC_OUT_DENORM_CONTROL_MPC_OUT_DENORM_MODE;

/*
 * MPC_OUT_RATE_CONTROL_DISABLE_SET enum
 */

typedef enum MPC_OUT_RATE_CONTROL_DISABLE_SET {
MPC_OUT_RATE_CONTROL_SET_ENABLE          = 0x00000000,
MPC_OUT_RATE_CONTROL_SET_DISABLE         = 0x00000001,
} MPC_OUT_RATE_CONTROL_DISABLE_SET;

/*******************************************************
 * MPCC Enums
 *******************************************************/

/*
 * MPCC_BG_COLOR_BPC enum
 */

typedef enum MPCC_BG_COLOR_BPC {
MPCC_BG_COLOR_BPC_8bit                   = 0x00000000,
MPCC_BG_COLOR_BPC_9bit                   = 0x00000001,
MPCC_BG_COLOR_BPC_10bit                  = 0x00000002,
MPCC_BG_COLOR_BPC_11bit                  = 0x00000003,
MPCC_BG_COLOR_BPC_12bit                  = 0x00000004,
} MPCC_BG_COLOR_BPC;

/*
 * MPCC_CONTROL_MPCC_ACTIVE_OVERLAP_ONLY enum
 */

typedef enum MPCC_CONTROL_MPCC_ACTIVE_OVERLAP_ONLY {
MPCC_CONTROL_MPCC_ACTIVE_OVERLAP_ONLY_FALSE = 0x00000000,
MPCC_CONTROL_MPCC_ACTIVE_OVERLAP_ONLY_TRUE = 0x00000001,
} MPCC_CONTROL_MPCC_ACTIVE_OVERLAP_ONLY;

/*
 * MPCC_CONTROL_MPCC_ALPHA_BLND_MODE enum
 */

typedef enum MPCC_CONTROL_MPCC_ALPHA_BLND_MODE {
MPCC_CONTROL_MPCC_ALPHA_BLND_MODE_PER_PIXEL_ALPHA = 0x00000000,
MPCC_CONTROL_MPCC_ALPHA_BLND_MODE_PER_PIXEL_ALPHA_COMBINED_GLOBAL_GAIN = 0x00000001,
MPCC_CONTROL_MPCC_ALPHA_BLND_MODE_GLOBAL_ALPHA = 0x00000002,
MPCC_CONTROL_MPCC_ALPHA_BLND_MODE_UNUSED = 0x00000003,
} MPCC_CONTROL_MPCC_ALPHA_BLND_MODE;

/*
 * MPCC_CONTROL_MPCC_ALPHA_MULTIPLIED_MODE enum
 */

typedef enum MPCC_CONTROL_MPCC_ALPHA_MULTIPLIED_MODE {
MPCC_CONTROL_MPCC_ALPHA_MULTIPLIED_MODE_FALSE = 0x00000000,
MPCC_CONTROL_MPCC_ALPHA_MULTIPLIED_MODE_TRUE = 0x00000001,
} MPCC_CONTROL_MPCC_ALPHA_MULTIPLIED_MODE;

/*
 * MPCC_CONTROL_MPCC_BOT_GAIN_MODE enum
 */

typedef enum MPCC_CONTROL_MPCC_BOT_GAIN_MODE {
MPCC_CONTROL_MPCC_BOT_GAIN_MODE_0        = 0x00000000,
MPCC_CONTROL_MPCC_BOT_GAIN_MODE_1        = 0x00000001,
} MPCC_CONTROL_MPCC_BOT_GAIN_MODE;

/*
 * MPCC_CONTROL_MPCC_MODE enum
 */

typedef enum MPCC_CONTROL_MPCC_MODE {
MPCC_CONTROL_MPCC_MODE_BYPASS            = 0x00000000,
MPCC_CONTROL_MPCC_MODE_TOP_LAYER_PASSTHROUGH = 0x00000001,
MPCC_CONTROL_MPCC_MODE_TOP_LAYER_ONLY    = 0x00000002,
MPCC_CONTROL_MPCC_MODE_TOP_BOT_BLENDING  = 0x00000003,
} MPCC_CONTROL_MPCC_MODE;

/*
 * MPCC_SM_CONTROL_MPCC_SM_EN enum
 */

typedef enum MPCC_SM_CONTROL_MPCC_SM_EN {
MPCC_SM_CONTROL_MPCC_SM_EN_FALSE         = 0x00000000,
MPCC_SM_CONTROL_MPCC_SM_EN_TRUE          = 0x00000001,
} MPCC_SM_CONTROL_MPCC_SM_EN;

/*
 * MPCC_SM_CONTROL_MPCC_SM_FIELD_ALT enum
 */

typedef enum MPCC_SM_CONTROL_MPCC_SM_FIELD_ALT {
MPCC_SM_CONTROL_MPCC_SM_FIELD_ALT_FALSE  = 0x00000000,
MPCC_SM_CONTROL_MPCC_SM_FIELD_ALT_TRUE   = 0x00000001,
} MPCC_SM_CONTROL_MPCC_SM_FIELD_ALT;

/*
 * MPCC_SM_CONTROL_MPCC_SM_FORCE_NEXT_FRAME_POL enum
 */

typedef enum MPCC_SM_CONTROL_MPCC_SM_FORCE_NEXT_FRAME_POL {
MPCC_SM_CONTROL_MPCC_SM_FORCE_NEXT_FRAME_POL_NO_FORCE = 0x00000000,
MPCC_SM_CONTROL_MPCC_SM_FORCE_NEXT_FRAME_POL_RESERVED = 0x00000001,
MPCC_SM_CONTROL_MPCC_SM_FORCE_NEXT_FRAME_POL_FORCE_LOW = 0x00000002,
MPCC_SM_CONTROL_MPCC_SM_FORCE_NEXT_FRAME_POL_FORCE_HIGH = 0x00000003,
} MPCC_SM_CONTROL_MPCC_SM_FORCE_NEXT_FRAME_POL;

/*
 * MPCC_SM_CONTROL_MPCC_SM_FORCE_NEXT_TOP_POL enum
 */

typedef enum MPCC_SM_CONTROL_MPCC_SM_FORCE_NEXT_TOP_POL {
MPCC_SM_CONTROL_MPCC_SM_FORCE_NEXT_TOP_POL_NO_FORCE = 0x00000000,
MPCC_SM_CONTROL_MPCC_SM_FORCE_NEXT_TOP_POL_RESERVED = 0x00000001,
MPCC_SM_CONTROL_MPCC_SM_FORCE_NEXT_TOP_POL_FORCE_LOW = 0x00000002,
MPCC_SM_CONTROL_MPCC_SM_FORCE_NEXT_TOP_POL_FORCE_HIGH = 0x00000003,
} MPCC_SM_CONTROL_MPCC_SM_FORCE_NEXT_TOP_POL;

/*
 * MPCC_SM_CONTROL_MPCC_SM_FRAME_ALT enum
 */

typedef enum MPCC_SM_CONTROL_MPCC_SM_FRAME_ALT {
MPCC_SM_CONTROL_MPCC_SM_FRAME_ALT_FALSE  = 0x00000000,
MPCC_SM_CONTROL_MPCC_SM_FRAME_ALT_TRUE   = 0x00000001,
} MPCC_SM_CONTROL_MPCC_SM_FRAME_ALT;

/*
 * MPCC_SM_CONTROL_MPCC_SM_MODE enum
 */

typedef enum MPCC_SM_CONTROL_MPCC_SM_MODE {
MPCC_SM_CONTROL_MPCC_SM_MODE_SINGLE_PLANE = 0x00000000,
MPCC_SM_CONTROL_MPCC_SM_MODE_ROW_SUBSAMPLING = 0x00000002,
MPCC_SM_CONTROL_MPCC_SM_MODE_COLUMN_SUBSAMPLING = 0x00000004,
MPCC_SM_CONTROL_MPCC_SM_MODE_CHECKERBOARD_SUBSAMPLING = 0x00000006,
} MPCC_SM_CONTROL_MPCC_SM_MODE;

/*
 * MPCC_TEST_DEBUG_INDEX_MPCC_TEST_DEBUG_WRITE_EN enum
 */

typedef enum MPCC_TEST_DEBUG_INDEX_MPCC_TEST_DEBUG_WRITE_EN {
MPCC_TEST_DEBUG_INDEX_MPCC_TEST_DEBUG_WRITE_EN_FALSE = 0x00000000,
MPCC_TEST_DEBUG_INDEX_MPCC_TEST_DEBUG_WRITE_EN_TRUE = 0x00000001,
} MPCC_TEST_DEBUG_INDEX_MPCC_TEST_DEBUG_WRITE_EN;

/*******************************************************
 * MPCC_OGAM Enums
 *******************************************************/

/*
 * MPCC_GAMUT_REMAP_COEF_FORMAT_ENUM enum
 */

typedef enum MPCC_GAMUT_REMAP_COEF_FORMAT_ENUM {
MPCC_GAMUT_REMAP_COEF_FORMAT_S2_13       = 0x00000000,
MPCC_GAMUT_REMAP_COEF_FORMAT_S3_12       = 0x00000001,
} MPCC_GAMUT_REMAP_COEF_FORMAT_ENUM;

/*
 * MPCC_GAMUT_REMAP_MODE_ENUM enum
 */

typedef enum MPCC_GAMUT_REMAP_MODE_ENUM {
MPCC_GAMUT_REMAP_MODE_0                  = 0x00000000,
MPCC_GAMUT_REMAP_MODE_1                  = 0x00000001,
MPCC_GAMUT_REMAP_MODE_2                  = 0x00000002,
MPCC_GAMUT_REMAP_MODE_RSV                = 0x00000003,
} MPCC_GAMUT_REMAP_MODE_ENUM;

/*
 * MPCC_OGAM_LUT_2_CONFIG_ENUM enum
 */

typedef enum MPCC_OGAM_LUT_2_CONFIG_ENUM {
MPCC_OGAM_LUT_2CFG_NO_MEMORY             = 0x00000000,
MPCC_OGAM_LUT_2CFG_MEMORY_A              = 0x00000001,
MPCC_OGAM_LUT_2CFG_MEMORY_B              = 0x00000002,
} MPCC_OGAM_LUT_2_CONFIG_ENUM;

/*
 * MPCC_OGAM_LUT_CONFIG_MODE enum
 */

typedef enum MPCC_OGAM_LUT_CONFIG_MODE {
MPCC_OGAM_DIFFERENT_RGB                  = 0x00000000,
MPCC_OGAM_ALL_USE_R                      = 0x00000001,
} MPCC_OGAM_LUT_CONFIG_MODE;

/*
 * MPCC_OGAM_LUT_PWL_DISABLE_ENUM enum
 */

typedef enum MPCC_OGAM_LUT_PWL_DISABLE_ENUM {
MPCC_OGAM_ENABLE_PWL                     = 0x00000000,
MPCC_OGAM_DISABLE_PWL                    = 0x00000001,
} MPCC_OGAM_LUT_PWL_DISABLE_ENUM;

/*
 * MPCC_OGAM_LUT_RAM_CONTROL_MPCC_OGAM_LUT_RAM_SEL enum
 */

typedef enum MPCC_OGAM_LUT_RAM_CONTROL_MPCC_OGAM_LUT_RAM_SEL {
MPCC_OGAM_LUT_RAM_CONTROL_MPCC_OGAM_LUT_RAM_SEL_RAMA = 0x00000000,
MPCC_OGAM_LUT_RAM_CONTROL_MPCC_OGAM_LUT_RAM_SEL_RAMB = 0x00000001,
} MPCC_OGAM_LUT_RAM_CONTROL_MPCC_OGAM_LUT_RAM_SEL;

/*
 * MPCC_OGAM_LUT_RAM_SEL enum
 */

typedef enum MPCC_OGAM_LUT_RAM_SEL {
MPCC_OGAM_RAMA_ACCESS                    = 0x00000000,
MPCC_OGAM_RAMB_ACCESS                    = 0x00000001,
} MPCC_OGAM_LUT_RAM_SEL;

/*
 * MPCC_OGAM_LUT_READ_COLOR_SEL enum
 */

typedef enum MPCC_OGAM_LUT_READ_COLOR_SEL {
MPCC_OGAM_BLUE_LUT                       = 0x00000000,
MPCC_OGAM_GREEN_LUT                      = 0x00000001,
MPCC_OGAM_RED_LUT                        = 0x00000002,
} MPCC_OGAM_LUT_READ_COLOR_SEL;

/*
 * MPCC_OGAM_LUT_READ_DBG enum
 */

typedef enum MPCC_OGAM_LUT_READ_DBG {
MPCC_OGAM_DISABLE_DEBUG                  = 0x00000000,
MPCC_OGAM_ENABLE_DEBUG                   = 0x00000001,
} MPCC_OGAM_LUT_READ_DBG;

/*
 * MPCC_OGAM_LUT_SEL_ENUM enum
 */

typedef enum MPCC_OGAM_LUT_SEL_ENUM {
MPCC_OGAM_RAMA                           = 0x00000000,
MPCC_OGAM_RAMB                           = 0x00000001,
} MPCC_OGAM_LUT_SEL_ENUM;

/*
 * MPCC_OGAM_MODE_MPCC_OGAM_MODE_ENUM enum
 */

typedef enum MPCC_OGAM_MODE_MPCC_OGAM_MODE_ENUM {
MPCC_OGAM_MODE_0                         = 0x00000000,
MPCC_OGAM_MODE_RSV1                      = 0x00000001,
MPCC_OGAM_MODE_2                         = 0x00000002,
MPCC_OGAM_MODE_RSV                       = 0x00000003,
} MPCC_OGAM_MODE_MPCC_OGAM_MODE_ENUM;

/*
 * MPCC_OGAM_NUM_SEG enum
 */

typedef enum MPCC_OGAM_NUM_SEG {
MPCC_OGAM_SEGMENTS_1                     = 0x00000000,
MPCC_OGAM_SEGMENTS_2                     = 0x00000001,
MPCC_OGAM_SEGMENTS_4                     = 0x00000002,
MPCC_OGAM_SEGMENTS_8                     = 0x00000003,
MPCC_OGAM_SEGMENTS_16                    = 0x00000004,
MPCC_OGAM_SEGMENTS_32                    = 0x00000005,
MPCC_OGAM_SEGMENTS_64                    = 0x00000006,
MPCC_OGAM_SEGMENTS_128                   = 0x00000007,
} MPCC_OGAM_NUM_SEG;

/*
 * MPCC_OGAM_TEST_DEBUG_INDEX_MPCC_OGAM_TEST_DEBUG_WRITE_EN enum
 */

typedef enum MPCC_OGAM_TEST_DEBUG_INDEX_MPCC_OGAM_TEST_DEBUG_WRITE_EN {
MPCC_OGAM_TEST_DEBUG_INDEX_MPCC_OGAM_TEST_DEBUG_WRITE_EN_FALSE = 0x00000000,
MPCC_OGAM_TEST_DEBUG_INDEX_MPCC_OGAM_TEST_DEBUG_WRITE_EN_TRUE = 0x00000001,
} MPCC_OGAM_TEST_DEBUG_INDEX_MPCC_OGAM_TEST_DEBUG_WRITE_EN;

/*******************************************************
 * MPCC_MCM Enums
 *******************************************************/

/*
 * MPCC_MCM_3DLUT_30BIT_ENUM enum
 */

typedef enum MPCC_MCM_3DLUT_30BIT_ENUM {
MPCC_MCM_3DLUT_36BIT                     = 0x00000000,
MPCC_MCM_3DLUT_30BIT                     = 0x00000001,
} MPCC_MCM_3DLUT_30BIT_ENUM;

/*
 * MPCC_MCM_3DLUT_RAM_SEL enum
 */

typedef enum MPCC_MCM_3DLUT_RAM_SEL {
MPCC_MCM_RAM0_ACCESS                     = 0x00000000,
MPCC_MCM_RAM1_ACCESS                     = 0x00000001,
MPCC_MCM_RAM2_ACCESS                     = 0x00000002,
MPCC_MCM_RAM3_ACCESS                     = 0x00000003,
} MPCC_MCM_3DLUT_RAM_SEL;

/*
 * MPCC_MCM_3DLUT_SIZE_ENUM enum
 */

typedef enum MPCC_MCM_3DLUT_SIZE_ENUM {
MPCC_MCM_3DLUT_17CUBE                    = 0x00000000,
MPCC_MCM_3DLUT_9CUBE                     = 0x00000001,
} MPCC_MCM_3DLUT_SIZE_ENUM;

/*
 * MPCC_MCM_GAMMA_LUT_MODE_ENUM enum
 */

typedef enum MPCC_MCM_GAMMA_LUT_MODE_ENUM {
MPCC_MCM_GAMMA_LUT_BYPASS                = 0x00000000,
MPCC_MCM_GAMMA_LUT_RESERVED_1            = 0x00000001,
MPCC_MCM_GAMMA_LUT_RAM_LUT               = 0x00000002,
MPCC_MCM_GAMMA_LUT_RESERVED_3            = 0x00000003,
} MPCC_MCM_GAMMA_LUT_MODE_ENUM;

/*
 * MPCC_MCM_GAMMA_LUT_PWL_DISABLE_ENUM enum
 */

typedef enum MPCC_MCM_GAMMA_LUT_PWL_DISABLE_ENUM {
MPCC_MCM_GAMMA_LUT_ENABLE_PWL            = 0x00000000,
MPCC_MCM_GAMMA_LUT_DISABLE_PWL           = 0x00000001,
} MPCC_MCM_GAMMA_LUT_PWL_DISABLE_ENUM;

/*
 * MPCC_MCM_GAMMA_LUT_SEL_ENUM enum
 */

typedef enum MPCC_MCM_GAMMA_LUT_SEL_ENUM {
MPCC_MCM_GAMMA_LUT_RAMA                  = 0x00000000,
MPCC_MCM_GAMMA_LUT_RAMB                  = 0x00000001,
} MPCC_MCM_GAMMA_LUT_SEL_ENUM;

/*
 * MPCC_MCM_LUT_2_MODE_ENUM enum
 */

typedef enum MPCC_MCM_LUT_2_MODE_ENUM {
MPCC_MCM_LUT_2_MODE_BYPASS               = 0x00000000,
MPCC_MCM_LUT_2_MODE_RAMA_LUT             = 0x00000001,
MPCC_MCM_LUT_2_MODE_RAMB_LUT             = 0x00000002,
} MPCC_MCM_LUT_2_MODE_ENUM;

/*
 * MPCC_MCM_LUT_CONFIG_MODE enum
 */

typedef enum MPCC_MCM_LUT_CONFIG_MODE {
MPCC_MCM_LUT_DIFFERENT_RGB               = 0x00000000,
MPCC_MCM_LUT_ALL_USE_R                   = 0x00000001,
} MPCC_MCM_LUT_CONFIG_MODE;

/*
 * MPCC_MCM_LUT_NUM_SEG enum
 */

typedef enum MPCC_MCM_LUT_NUM_SEG {
MPCC_MCM_LUT_SEGMENTS_1                  = 0x00000000,
MPCC_MCM_LUT_SEGMENTS_2                  = 0x00000001,
MPCC_MCM_LUT_SEGMENTS_4                  = 0x00000002,
MPCC_MCM_LUT_SEGMENTS_8                  = 0x00000003,
MPCC_MCM_LUT_SEGMENTS_16                 = 0x00000004,
MPCC_MCM_LUT_SEGMENTS_32                 = 0x00000005,
MPCC_MCM_LUT_SEGMENTS_64                 = 0x00000006,
MPCC_MCM_LUT_SEGMENTS_128                = 0x00000007,
} MPCC_MCM_LUT_NUM_SEG;

/*
 * MPCC_MCM_LUT_RAM_SEL enum
 */

typedef enum MPCC_MCM_LUT_RAM_SEL {
MPCC_MCM_LUT_RAMA_ACCESS                 = 0x00000000,
MPCC_MCM_LUT_RAMB_ACCESS                 = 0x00000001,
} MPCC_MCM_LUT_RAM_SEL;

/*
 * MPCC_MCM_LUT_READ_COLOR_SEL enum
 */

typedef enum MPCC_MCM_LUT_READ_COLOR_SEL {
MPCC_MCM_LUT_BLUE_LUT                    = 0x00000000,
MPCC_MCM_LUT_GREEN_LUT                   = 0x00000001,
MPCC_MCM_LUT_RED_LUT                     = 0x00000002,
} MPCC_MCM_LUT_READ_COLOR_SEL;

/*
 * MPCC_MCM_LUT_READ_DBG enum
 */

typedef enum MPCC_MCM_LUT_READ_DBG {
MPCC_MCM_LUT_DISABLE_DEBUG               = 0x00000000,
MPCC_MCM_LUT_ENABLE_DEBUG                = 0x00000001,
} MPCC_MCM_LUT_READ_DBG;

/*
 * MPCC_MCM_MEM_PWR_FORCE_ENUM enum
 */

typedef enum MPCC_MCM_MEM_PWR_FORCE_ENUM {
MPCC_MCM_MEM_PWR_FORCE_DIS               = 0x00000000,
MPCC_MCM_MEM_PWR_FORCE_LS                = 0x00000001,
MPCC_MCM_MEM_PWR_FORCE_DS                = 0x00000002,
MPCC_MCM_MEM_PWR_FORCE_SD                = 0x00000003,
} MPCC_MCM_MEM_PWR_FORCE_ENUM;

/*
 * MPCC_MCM_MEM_PWR_STATE_ENUM enum
 */

typedef enum MPCC_MCM_MEM_PWR_STATE_ENUM {
MPCC_MCM_MEM_PWR_STATE_ON                = 0x00000000,
MPCC_MCM_MEM_PWR_STATE_LS                = 0x00000001,
MPCC_MCM_MEM_PWR_STATE_DS                = 0x00000002,
MPCC_MCM_MEM_PWR_STATE_SD                = 0x00000003,
} MPCC_MCM_MEM_PWR_STATE_ENUM;

/*******************************************************
 * ABM Enums
 *******************************************************/

/*******************************************************
 * DPG Enums
 *******************************************************/

/*
 * ENUM_DPG_BIT_DEPTH enum
 */

typedef enum ENUM_DPG_BIT_DEPTH {
ENUM_DPG_BIT_DEPTH_6BPC                  = 0x00000000,
ENUM_DPG_BIT_DEPTH_8BPC                  = 0x00000001,
ENUM_DPG_BIT_DEPTH_10BPC                 = 0x00000002,
ENUM_DPG_BIT_DEPTH_12BPC                 = 0x00000003,
} ENUM_DPG_BIT_DEPTH;

/*
 * ENUM_DPG_DYNAMIC_RANGE enum
 */

typedef enum ENUM_DPG_DYNAMIC_RANGE {
ENUM_DPG_DYNAMIC_RANGE_VESA              = 0x00000000,
ENUM_DPG_DYNAMIC_RANGE_CEA               = 0x00000001,
} ENUM_DPG_DYNAMIC_RANGE;

/*
 * ENUM_DPG_EN enum
 */

typedef enum ENUM_DPG_EN {
ENUM_DPG_DISABLE                         = 0x00000000,
ENUM_DPG_ENABLE                          = 0x00000001,
} ENUM_DPG_EN;

/*
 * ENUM_DPG_FIELD_POLARITY enum
 */

typedef enum ENUM_DPG_FIELD_POLARITY {
ENUM_DPG_FIELD_POLARITY_TOP_EVEN_BOTTOM_ODD = 0x00000000,
ENUM_DPG_FIELD_POLARITY_TOP_ODD_BOTTOM_EVEN = 0x00000001,
} ENUM_DPG_FIELD_POLARITY;

/*
 * ENUM_DPG_MODE enum
 */

typedef enum ENUM_DPG_MODE {
ENUM_DPG_MODE_RGB_COLOUR_BLOCK           = 0x00000000,
ENUM_DPG_MODE_YCBCR_601_COLOUR_BLOCK     = 0x00000001,
ENUM_DPG_MODE_YCBCR_709_COLOUR_BLOCK     = 0x00000002,
ENUM_DPG_MODE_VERTICAL_BAR               = 0x00000003,
ENUM_DPG_MODE_HORIZONTAL_BAR             = 0x00000004,
ENUM_DPG_MODE_RGB_SINGLE_RAMP            = 0x00000005,
ENUM_DPG_MODE_RGB_DUAL_RAMP              = 0x00000006,
ENUM_DPG_MODE_RGB_XR_BIAS                = 0x00000007,
} ENUM_DPG_MODE;

/*******************************************************
 * FMT Enums
 *******************************************************/

/*
 * FMTMEM_PWR_DIS_CTRL enum
 */

typedef enum FMTMEM_PWR_DIS_CTRL {
FMTMEM_ENABLE_MEM_PWR_CTRL               = 0x00000000,
FMTMEM_DISABLE_MEM_PWR_CTRL              = 0x00000001,
} FMTMEM_PWR_DIS_CTRL;

/*
 * FMTMEM_PWR_FORCE_CTRL enum
 */

typedef enum FMTMEM_PWR_FORCE_CTRL {
FMTMEM_NO_FORCE_REQUEST                  = 0x00000000,
FMTMEM_FORCE_LIGHT_SLEEP_REQUEST         = 0x00000001,
FMTMEM_FORCE_DEEP_SLEEP_REQUEST          = 0x00000002,
FMTMEM_FORCE_SHUT_DOWN_REQUEST           = 0x00000003,
} FMTMEM_PWR_FORCE_CTRL;

/*
 * FMT_BIT_DEPTH_CONTROL_25FRC_SEL enum
 */

typedef enum FMT_BIT_DEPTH_CONTROL_25FRC_SEL {
FMT_BIT_DEPTH_CONTROL_25FRC_SEL_Ei       = 0x00000000,
FMT_BIT_DEPTH_CONTROL_25FRC_SEL_Fi       = 0x00000001,
FMT_BIT_DEPTH_CONTROL_25FRC_SEL_Gi       = 0x00000002,
FMT_BIT_DEPTH_CONTROL_25FRC_SEL_RESERVED = 0x00000003,
} FMT_BIT_DEPTH_CONTROL_25FRC_SEL;

/*
 * FMT_BIT_DEPTH_CONTROL_50FRC_SEL enum
 */

typedef enum FMT_BIT_DEPTH_CONTROL_50FRC_SEL {
FMT_BIT_DEPTH_CONTROL_50FRC_SEL_A        = 0x00000000,
FMT_BIT_DEPTH_CONTROL_50FRC_SEL_B        = 0x00000001,
FMT_BIT_DEPTH_CONTROL_50FRC_SEL_C        = 0x00000002,
FMT_BIT_DEPTH_CONTROL_50FRC_SEL_D        = 0x00000003,
} FMT_BIT_DEPTH_CONTROL_50FRC_SEL;

/*
 * FMT_BIT_DEPTH_CONTROL_75FRC_SEL enum
 */

typedef enum FMT_BIT_DEPTH_CONTROL_75FRC_SEL {
FMT_BIT_DEPTH_CONTROL_75FRC_SEL_E        = 0x00000000,
FMT_BIT_DEPTH_CONTROL_75FRC_SEL_F        = 0x00000001,
FMT_BIT_DEPTH_CONTROL_75FRC_SEL_G        = 0x00000002,
FMT_BIT_DEPTH_CONTROL_75FRC_SEL_RESERVED = 0x00000003,
} FMT_BIT_DEPTH_CONTROL_75FRC_SEL;

/*
 * FMT_BIT_DEPTH_CONTROL_SPATIAL_DITHER_DEPTH enum
 */

typedef enum FMT_BIT_DEPTH_CONTROL_SPATIAL_DITHER_DEPTH {
FMT_BIT_DEPTH_CONTROL_SPATIAL_DITHER_DEPTH_18BPP = 0x00000000,
FMT_BIT_DEPTH_CONTROL_SPATIAL_DITHER_DEPTH_24BPP = 0x00000001,
FMT_BIT_DEPTH_CONTROL_SPATIAL_DITHER_DEPTH_30BPP = 0x00000002,
} FMT_BIT_DEPTH_CONTROL_SPATIAL_DITHER_DEPTH;

/*
 * FMT_BIT_DEPTH_CONTROL_TEMPORAL_DITHER_DEPTH enum
 */

typedef enum FMT_BIT_DEPTH_CONTROL_TEMPORAL_DITHER_DEPTH {
FMT_BIT_DEPTH_CONTROL_TEMPORAL_DITHER_DEPTH_18BPP = 0x00000000,
FMT_BIT_DEPTH_CONTROL_TEMPORAL_DITHER_DEPTH_24BPP = 0x00000001,
FMT_BIT_DEPTH_CONTROL_TEMPORAL_DITHER_DEPTH_30BPP = 0x00000002,
} FMT_BIT_DEPTH_CONTROL_TEMPORAL_DITHER_DEPTH;

/*
 * FMT_BIT_DEPTH_CONTROL_TEMPORAL_LEVEL enum
 */

typedef enum FMT_BIT_DEPTH_CONTROL_TEMPORAL_LEVEL {
FMT_BIT_DEPTH_CONTROL_TEMPORAL_LEVEL_GREY_LEVEL2 = 0x00000000,
FMT_BIT_DEPTH_CONTROL_TEMPORAL_LEVEL_GREY_LEVEL4 = 0x00000001,
} FMT_BIT_DEPTH_CONTROL_TEMPORAL_LEVEL;

/*
 * FMT_BIT_DEPTH_CONTROL_TRUNCATE_DEPTH enum
 */

typedef enum FMT_BIT_DEPTH_CONTROL_TRUNCATE_DEPTH {
FMT_BIT_DEPTH_CONTROL_TRUNCATE_DEPTH_18BPP = 0x00000000,
FMT_BIT_DEPTH_CONTROL_TRUNCATE_DEPTH_24BPP = 0x00000001,
FMT_BIT_DEPTH_CONTROL_TRUNCATE_DEPTH_30BPP = 0x00000002,
} FMT_BIT_DEPTH_CONTROL_TRUNCATE_DEPTH;

/*
 * FMT_BIT_DEPTH_CONTROL_TRUNCATE_MODE enum
 */

typedef enum FMT_BIT_DEPTH_CONTROL_TRUNCATE_MODE {
FMT_BIT_DEPTH_CONTROL_TRUNCATE_MODE_TRUNCATION = 0x00000000,
FMT_BIT_DEPTH_CONTROL_TRUNCATE_MODE_ROUNDING = 0x00000001,
} FMT_BIT_DEPTH_CONTROL_TRUNCATE_MODE;

/*
 * FMT_CLAMP_CNTL_COLOR_FORMAT enum
 */

typedef enum FMT_CLAMP_CNTL_COLOR_FORMAT {
FMT_CLAMP_CNTL_COLOR_FORMAT_6BPC         = 0x00000000,
FMT_CLAMP_CNTL_COLOR_FORMAT_8BPC         = 0x00000001,
FMT_CLAMP_CNTL_COLOR_FORMAT_10BPC        = 0x00000002,
FMT_CLAMP_CNTL_COLOR_FORMAT_12BPC        = 0x00000003,
FMT_CLAMP_CNTL_COLOR_FORMAT_RESERVED1    = 0x00000004,
FMT_CLAMP_CNTL_COLOR_FORMAT_RESERVED2    = 0x00000005,
FMT_CLAMP_CNTL_COLOR_FORMAT_RESERVED3    = 0x00000006,
FMT_CLAMP_CNTL_COLOR_FORMAT_PROGRAMMABLE = 0x00000007,
} FMT_CLAMP_CNTL_COLOR_FORMAT;

/*
 * FMT_CONTROL_CBCR_BIT_REDUCTION_BYPASS enum
 */

typedef enum FMT_CONTROL_CBCR_BIT_REDUCTION_BYPASS {
FMT_CONTROL_CBCR_BIT_REDUCTION_BYPASS_DISABLE = 0x00000000,
FMT_CONTROL_CBCR_BIT_REDUCTION_BYPASS_ENABLE = 0x00000001,
} FMT_CONTROL_CBCR_BIT_REDUCTION_BYPASS;

/*
 * FMT_CONTROL_PIXEL_ENCODING enum
 */

typedef enum FMT_CONTROL_PIXEL_ENCODING {
FMT_CONTROL_PIXEL_ENCODING_RGB444_OR_YCBCR444 = 0x00000000,
FMT_CONTROL_PIXEL_ENCODING_YCBCR422      = 0x00000001,
FMT_CONTROL_PIXEL_ENCODING_YCBCR420      = 0x00000002,
FMT_CONTROL_PIXEL_ENCODING_RESERVED      = 0x00000003,
} FMT_CONTROL_PIXEL_ENCODING;

/*
 * FMT_CONTROL_SUBSAMPLING_MODE enum
 */

typedef enum FMT_CONTROL_SUBSAMPLING_MODE {
FMT_CONTROL_SUBSAMPLING_MODE_DROP        = 0x00000000,
FMT_CONTROL_SUBSAMPLING_MODE_AVERAGE     = 0x00000001,
FMT_CONTROL_SUBSAMPLING_MOME_3_TAP       = 0x00000002,
FMT_CONTROL_SUBSAMPLING_MOME_RESERVED    = 0x00000003,
} FMT_CONTROL_SUBSAMPLING_MODE;

/*
 * FMT_CONTROL_SUBSAMPLING_ORDER enum
 */

typedef enum FMT_CONTROL_SUBSAMPLING_ORDER {
FMT_CONTROL_SUBSAMPLING_ORDER_CB_BEFORE_CR = 0x00000000,
FMT_CONTROL_SUBSAMPLING_ORDER_CR_BEFORE_CB = 0x00000001,
} FMT_CONTROL_SUBSAMPLING_ORDER;

/*
 * FMT_DEBUG_CNTL_COLOR_SELECT enum
 */

typedef enum FMT_DEBUG_CNTL_COLOR_SELECT {
FMT_DEBUG_CNTL_COLOR_SELECT_BLUE         = 0x00000000,
FMT_DEBUG_CNTL_COLOR_SELECT_GREEN        = 0x00000001,
FMT_DEBUG_CNTL_COLOR_SELECT_RED1         = 0x00000002,
FMT_DEBUG_CNTL_COLOR_SELECT_RED2         = 0x00000003,
} FMT_DEBUG_CNTL_COLOR_SELECT;

/*
 * FMT_DYNAMIC_EXP_MODE enum
 */

typedef enum FMT_DYNAMIC_EXP_MODE {
FMT_DYNAMIC_EXP_MODE_10to12              = 0x00000000,
FMT_DYNAMIC_EXP_MODE_8to12               = 0x00000001,
} FMT_DYNAMIC_EXP_MODE;

/*
 * FMT_FRAME_RANDOM_ENABLE_CONTROL enum
 */

typedef enum FMT_FRAME_RANDOM_ENABLE_CONTROL {
FMT_FRAME_RANDOM_ENABLE_RESET_EACH_FRAME = 0x00000000,
FMT_FRAME_RANDOM_ENABLE_RESET_ONCE       = 0x00000001,
} FMT_FRAME_RANDOM_ENABLE_CONTROL;

/*
 * FMT_POWER_STATE_ENUM enum
 */

typedef enum FMT_POWER_STATE_ENUM {
FMT_POWER_STATE_ENUM_ON                  = 0x00000000,
FMT_POWER_STATE_ENUM_LS                  = 0x00000001,
FMT_POWER_STATE_ENUM_DS                  = 0x00000002,
FMT_POWER_STATE_ENUM_SD                  = 0x00000003,
} FMT_POWER_STATE_ENUM;

/*
 * FMT_RGB_RANDOM_ENABLE_CONTROL enum
 */

typedef enum FMT_RGB_RANDOM_ENABLE_CONTROL {
FMT_RGB_RANDOM_ENABLE_CONTROL_DISABLE    = 0x00000000,
FMT_RGB_RANDOM_ENABLE_CONTROL_ENABLE     = 0x00000001,
} FMT_RGB_RANDOM_ENABLE_CONTROL;

/*
 * FMT_SPATIAL_DITHER_FRAME_COUNTER_BIT_SWAP_CONTROL enum
 */

typedef enum FMT_SPATIAL_DITHER_FRAME_COUNTER_BIT_SWAP_CONTROL {
FMT_SPATIAL_DITHER_FRAME_COUNTER_BIT_SWAP_NO_SWAP = 0x00000000,
FMT_SPATIAL_DITHER_FRAME_COUNTER_BIT_SWAP_1 = 0x00000001,
FMT_SPATIAL_DITHER_FRAME_COUNTER_BIT_SWAP_2 = 0x00000002,
FMT_SPATIAL_DITHER_FRAME_COUNTER_BIT_SWAP_RESERVED = 0x00000003,
} FMT_SPATIAL_DITHER_FRAME_COUNTER_BIT_SWAP_CONTROL;

/*
 * FMT_SPATIAL_DITHER_MODE enum
 */

typedef enum FMT_SPATIAL_DITHER_MODE {
FMT_SPATIAL_DITHER_MODE_0                = 0x00000000,
FMT_SPATIAL_DITHER_MODE_1                = 0x00000001,
FMT_SPATIAL_DITHER_MODE_2                = 0x00000002,
FMT_SPATIAL_DITHER_MODE_3                = 0x00000003,
} FMT_SPATIAL_DITHER_MODE;

/*
 * FMT_STEREOSYNC_OVERRIDE_CONTROL enum
 */

typedef enum FMT_STEREOSYNC_OVERRIDE_CONTROL {
FMT_STEREOSYNC_OVERRIDE_CONTROL_0        = 0x00000000,
FMT_STEREOSYNC_OVERRIDE_CONTROL_1        = 0x00000001,
} FMT_STEREOSYNC_OVERRIDE_CONTROL;

/*
 * FMT_TEMPORAL_DITHER_PATTERN_CONTROL_RGB1_BGR0 enum
 */

typedef enum FMT_TEMPORAL_DITHER_PATTERN_CONTROL_RGB1_BGR0 {
FMT_TEMPORAL_DITHER_PATTERN_CONTROL_RGB1_BGR0_BGR = 0x00000000,
FMT_TEMPORAL_DITHER_PATTERN_CONTROL_RGB1_BGR0_RGB = 0x00000001,
} FMT_TEMPORAL_DITHER_PATTERN_CONTROL_RGB1_BGR0;

/*******************************************************
 * OPPBUF Enums
 *******************************************************/

/*
 * OPPBUF_DISPLAY_SEGMENTATION enum
 */

typedef enum OPPBUF_DISPLAY_SEGMENTATION {
OPPBUF_DISPLAY_SEGMENTATION_1_SEGMENT    = 0x00000000,
OPPBUF_DISPLAY_SEGMENTATION_2_SEGMENT    = 0x00000001,
OPPBUF_DISPLAY_SEGMENTATION_4_SEGMENT    = 0x00000002,
OPPBUF_DISPLAY_SEGMENTATION_4_SEGMENT_SPLIT_LEFT = 0x00000003,
OPPBUF_DISPLAY_SEGMENTATION_4_SEGMENT_SPLIT_RIGHT = 0x00000004,
} OPPBUF_DISPLAY_SEGMENTATION;

/*******************************************************
 * OPP_PIPE Enums
 *******************************************************/

/*
 * OPP_PIPE_CLOCK_ENABLE_CONTROL enum
 */

typedef enum OPP_PIPE_CLOCK_ENABLE_CONTROL {
OPP_PIPE_CLOCK_DISABLE                   = 0x00000000,
OPP_PIPE_CLOCK_ENABLE                    = 0x00000001,
} OPP_PIPE_CLOCK_ENABLE_CONTROL;

/*
 * OPP_PIPE_DIGTIAL_BYPASS_CONTROL enum
 */

typedef enum OPP_PIPE_DIGTIAL_BYPASS_CONTROL {
OPP_PIPE_DIGTIAL_BYPASS_DISABLE          = 0x00000000,
OPP_PIPE_DIGTIAL_BYPASS_ENABLE           = 0x00000001,
} OPP_PIPE_DIGTIAL_BYPASS_CONTROL;

/*******************************************************
 * OPP_PIPE_CRC Enums
 *******************************************************/

/*
 * OPP_PIPE_CRC_CONT_EN enum
 */

typedef enum OPP_PIPE_CRC_CONT_EN {
OPP_PIPE_CRC_MODE_ONE_SHOT               = 0x00000000,
OPP_PIPE_CRC_MODE_CONTINUOUS             = 0x00000001,
} OPP_PIPE_CRC_CONT_EN;

/*
 * OPP_PIPE_CRC_EN enum
 */

typedef enum OPP_PIPE_CRC_EN {
OPP_PIPE_CRC_DISABLE                     = 0x00000000,
OPP_PIPE_CRC_ENABLE                      = 0x00000001,
} OPP_PIPE_CRC_EN;

/*
 * OPP_PIPE_CRC_INTERLACE_EN enum
 */

typedef enum OPP_PIPE_CRC_INTERLACE_EN {
OPP_PIPE_CRC_INTERLACE_EN_INTERPRET_AS_PROGRESSIVE = 0x00000000,
OPP_PIPE_CRC_INTERLACE_EN_INTERPRET_AS_INTERLACED = 0x00000001,
} OPP_PIPE_CRC_INTERLACE_EN;

/*
 * OPP_PIPE_CRC_INTERLACE_MODE enum
 */

typedef enum OPP_PIPE_CRC_INTERLACE_MODE {
OPP_PIPE_CRC_INTERLACE_MODE_TOP          = 0x00000000,
OPP_PIPE_CRC_INTERLACE_MODE_BOTTOM       = 0x00000001,
OPP_PIPE_CRC_INTERLACE_MODE_BOTH_RESET_AFTER_BOTTOM_FIELD = 0x00000002,
OPP_PIPE_CRC_INTERLACE_MODE_BOTH_RESET_AFTER_EACH_FIELD = 0x00000003,
} OPP_PIPE_CRC_INTERLACE_MODE;

/*
 * OPP_PIPE_CRC_ONE_SHOT_PENDING enum
 */

typedef enum OPP_PIPE_CRC_ONE_SHOT_PENDING {
OPP_PIPE_CRC_ONE_SHOT_PENDING_NOT_PENDING = 0x00000000,
OPP_PIPE_CRC_ONE_SHOT_PENDING_PENDING    = 0x00000001,
} OPP_PIPE_CRC_ONE_SHOT_PENDING;

/*
 * OPP_PIPE_CRC_PIXEL_SELECT enum
 */

typedef enum OPP_PIPE_CRC_PIXEL_SELECT {
OPP_PIPE_CRC_PIXEL_SELECT_ALL_PIXELS     = 0x00000000,
OPP_PIPE_CRC_PIXEL_SELECT_RESERVED       = 0x00000001,
OPP_PIPE_CRC_PIXEL_SELECT_EVEN_PIXELS    = 0x00000002,
OPP_PIPE_CRC_PIXEL_SELECT_ODD_PIXELS     = 0x00000003,
} OPP_PIPE_CRC_PIXEL_SELECT;

/*
 * OPP_PIPE_CRC_SOURCE_SELECT enum
 */

typedef enum OPP_PIPE_CRC_SOURCE_SELECT {
OPP_PIPE_CRC_SOURCE_SELECT_FMT           = 0x00000000,
OPP_PIPE_CRC_SOURCE_SELECT_SFT           = 0x00000001,
} OPP_PIPE_CRC_SOURCE_SELECT;

/*
 * OPP_PIPE_CRC_STEREO_EN enum
 */

typedef enum OPP_PIPE_CRC_STEREO_EN {
OPP_PIPE_CRC_STEREO_EN_INTERPRET_AS_NON_STEREO = 0x00000000,
OPP_PIPE_CRC_STEREO_EN_INTERPRET_AS_STEREO = 0x00000001,
} OPP_PIPE_CRC_STEREO_EN;

/*
 * OPP_PIPE_CRC_STEREO_MODE enum
 */

typedef enum OPP_PIPE_CRC_STEREO_MODE {
OPP_PIPE_CRC_STEREO_MODE_LEFT            = 0x00000000,
OPP_PIPE_CRC_STEREO_MODE_RIGHT           = 0x00000001,
OPP_PIPE_CRC_STEREO_MODE_BOTH_RESET_AFTER_RIGHT_EYE = 0x00000002,
OPP_PIPE_CRC_STEREO_MODE_BOTH_RESET_AFTER_EACH_EYE = 0x00000003,
} OPP_PIPE_CRC_STEREO_MODE;

/*******************************************************
 * OPP_TOP Enums
 *******************************************************/

/*
 * OPP_ABM_DEBUG_BUS_SELECT_CONTROL enum
 */

typedef enum OPP_ABM_DEBUG_BUS_SELECT_CONTROL {
DEBUG_BUS_SELECT_ABM0                    = 0x00000000,
DEBUG_BUS_SELECT_ABM1                    = 0x00000001,
DEBUG_BUS_SELECT_ABM2                    = 0x00000002,
DEBUG_BUS_SELECT_ABM3                    = 0x00000003,
DEBUG_BUS_SELECT_ABM_RESERVED0           = 0x00000004,
DEBUG_BUS_SELECT_ABM_RESERVED1           = 0x00000005,
} OPP_ABM_DEBUG_BUS_SELECT_CONTROL;

/*
 * OPP_DPG_DEBUG_BUS_SELECT_CONTROL enum
 */

typedef enum OPP_DPG_DEBUG_BUS_SELECT_CONTROL {
DEBUG_BUS_SELECT_DPG0                    = 0x00000000,
DEBUG_BUS_SELECT_DPG1                    = 0x00000001,
DEBUG_BUS_SELECT_DPG2                    = 0x00000002,
DEBUG_BUS_SELECT_DPG3                    = 0x00000003,
DEBUG_BUS_SELECT_DPG_RESERVED0           = 0x00000004,
DEBUG_BUS_SELECT_DPG_RESERVED1           = 0x00000005,
} OPP_DPG_DEBUG_BUS_SELECT_CONTROL;

/*
 * OPP_FMT_DEBUG_BUS_SELECT_CONTROL enum
 */

typedef enum OPP_FMT_DEBUG_BUS_SELECT_CONTROL {
DEBUG_BUS_SELECT_FMT0                    = 0x00000000,
DEBUG_BUS_SELECT_FMT1                    = 0x00000001,
DEBUG_BUS_SELECT_FMT2                    = 0x00000002,
DEBUG_BUS_SELECT_FMT3                    = 0x00000003,
DEBUG_BUS_SELECT_FMT_RESERVED0           = 0x00000004,
DEBUG_BUS_SELECT_FMT_RESERVED1           = 0x00000005,
} OPP_FMT_DEBUG_BUS_SELECT_CONTROL;

/*
 * OPP_OPPBUF_DEBUG_BUS_SELECT_CONTROL enum
 */

typedef enum OPP_OPPBUF_DEBUG_BUS_SELECT_CONTROL {
DEBUG_BUS_SELECT_OPPBUF0                 = 0x00000000,
DEBUG_BUS_SELECT_OPPBUF1                 = 0x00000001,
DEBUG_BUS_SELECT_OPPBUF2                 = 0x00000002,
DEBUG_BUS_SELECT_OPPBUF3                 = 0x00000003,
DEBUG_BUS_SELECT_OPPBUF_RESERVED0        = 0x00000004,
DEBUG_BUS_SELECT_OPPBUF_RESERVED1        = 0x00000005,
} OPP_OPPBUF_DEBUG_BUS_SELECT_CONTROL;

/*
 * OPP_OPP_PIPE_DEBUG_BUS_SELECT_CONTROL enum
 */

typedef enum OPP_OPP_PIPE_DEBUG_BUS_SELECT_CONTROL {
DEBUG_BUS_SELECT_OPP_PIPE0               = 0x00000000,
DEBUG_BUS_SELECT_OPP_PIPE1               = 0x00000001,
DEBUG_BUS_SELECT_OPP_PIPE2               = 0x00000002,
DEBUG_BUS_SELECT_OPP_PIPE3               = 0x00000003,
DEBUG_BUS_SELECT_OPP_PIPE_RESERVED0      = 0x00000004,
DEBUG_BUS_SELECT_OPP_PIPE_RESERVED1      = 0x00000005,
} OPP_OPP_PIPE_DEBUG_BUS_SELECT_CONTROL;

/*
 * OPP_TEST_CLK_SEL_CONTROL enum
 */

typedef enum OPP_TEST_CLK_SEL_CONTROL {
OPP_TEST_CLK_SEL_DISPCLK_P               = 0x00000000,
OPP_TEST_CLK_SEL_DISPCLK_R               = 0x00000001,
OPP_TEST_CLK_SEL_DISPCLK_ABM0            = 0x00000002,
OPP_TEST_CLK_SEL_DISPCLK_ABM1            = 0x00000003,
OPP_TEST_CLK_SEL_DISPCLK_ABM2            = 0x00000004,
OPP_TEST_CLK_SEL_DISPCLK_ABM3            = 0x00000005,
OPP_TEST_CLK_SEL_RESERVED0               = 0x00000006,
OPP_TEST_CLK_SEL_RESERVED1               = 0x00000007,
OPP_TEST_CLK_SEL_DISPCLK_OPP0            = 0x00000008,
OPP_TEST_CLK_SEL_DISPCLK_OPP1            = 0x00000009,
OPP_TEST_CLK_SEL_DISPCLK_OPP2            = 0x0000000a,
OPP_TEST_CLK_SEL_DISPCLK_OPP3            = 0x0000000b,
OPP_TEST_CLK_SEL_RESERVED2               = 0x0000000c,
OPP_TEST_CLK_SEL_RESERVED3               = 0x0000000d,
} OPP_TEST_CLK_SEL_CONTROL;

/*
 * OPP_TOP_CLOCK_ENABLE_STATUS enum
 */

typedef enum OPP_TOP_CLOCK_ENABLE_STATUS {
OPP_TOP_CLOCK_DISABLED_STATUS            = 0x00000000,
OPP_TOP_CLOCK_ENABLED_STATUS             = 0x00000001,
} OPP_TOP_CLOCK_ENABLE_STATUS;

/*
 * OPP_TOP_CLOCK_GATING_CONTROL enum
 */

typedef enum OPP_TOP_CLOCK_GATING_CONTROL {
OPP_TOP_CLOCK_GATING_ENABLED             = 0x00000000,
OPP_TOP_CLOCK_GATING_DISABLED            = 0x00000001,
} OPP_TOP_CLOCK_GATING_CONTROL;

/*******************************************************
 * DSCRM Enums
 *******************************************************/

/*
 * ENUM_DSCRM_EN enum
 */

typedef enum ENUM_DSCRM_EN {
ENUM_DSCRM_DISABLE                       = 0x00000000,
ENUM_DSCRM_ENABLE                        = 0x00000001,
} ENUM_DSCRM_EN;

/*******************************************************
 * OTG Enums
 *******************************************************/

/*
 * MASTER_UPDATE_LOCK_MASTER_UPDATE_LOCK enum
 */

typedef enum MASTER_UPDATE_LOCK_MASTER_UPDATE_LOCK {
MASTER_UPDATE_LOCK_MASTER_UPDATE_LOCK_FALSE = 0x00000000,
MASTER_UPDATE_LOCK_MASTER_UPDATE_LOCK_TRUE = 0x00000001,
} MASTER_UPDATE_LOCK_MASTER_UPDATE_LOCK;

/*
 * MASTER_UPDATE_LOCK_SEL enum
 */

typedef enum MASTER_UPDATE_LOCK_SEL {
MASTER_UPDATE_LOCK_SEL_0                 = 0x00000000,
MASTER_UPDATE_LOCK_SEL_1                 = 0x00000001,
MASTER_UPDATE_LOCK_SEL_2                 = 0x00000002,
MASTER_UPDATE_LOCK_SEL_3                 = 0x00000003,
MASTER_UPDATE_LOCK_SEL_RESERVED4         = 0x00000004,
MASTER_UPDATE_LOCK_SEL_RESERVED5         = 0x00000005,
} MASTER_UPDATE_LOCK_SEL;

/*
 * MASTER_UPDATE_MODE_MASTER_UPDATE_INTERLACED_MODE enum
 */

typedef enum MASTER_UPDATE_MODE_MASTER_UPDATE_INTERLACED_MODE {
MASTER_UPDATE_MODE_MASTER_UPDATE_INTERLACED_MODE_BOTH = 0x00000000,
MASTER_UPDATE_MODE_MASTER_UPDATE_INTERLACED_MODE_TOP = 0x00000001,
MASTER_UPDATE_MODE_MASTER_UPDATE_INTERLACED_MODE_BOTTOM = 0x00000002,
MASTER_UPDATE_MODE_MASTER_UPDATE_INTERLACED_MODE_RESERVED = 0x00000003,
} MASTER_UPDATE_MODE_MASTER_UPDATE_INTERLACED_MODE;

/*
 * OTG_3D_STRUCTURE_CONTROL_OTG_3D_STRUCTURE_EN enum
 */

typedef enum OTG_3D_STRUCTURE_CONTROL_OTG_3D_STRUCTURE_EN {
OTG_3D_STRUCTURE_CONTROL_OTG_3D_STRUCTURE_EN_FALSE = 0x00000000,
OTG_3D_STRUCTURE_CONTROL_OTG_3D_STRUCTURE_EN_TRUE = 0x00000001,
} OTG_3D_STRUCTURE_CONTROL_OTG_3D_STRUCTURE_EN;

/*
 * OTG_3D_STRUCTURE_CONTROL_OTG_3D_STRUCTURE_EN_DB enum
 */

typedef enum OTG_3D_STRUCTURE_CONTROL_OTG_3D_STRUCTURE_EN_DB {
OTG_3D_STRUCTURE_CONTROL_OTG_3D_STRUCTURE_EN_DB_FALSE = 0x00000000,
OTG_3D_STRUCTURE_CONTROL_OTG_3D_STRUCTURE_EN_DB_TRUE = 0x00000001,
} OTG_3D_STRUCTURE_CONTROL_OTG_3D_STRUCTURE_EN_DB;

/*
 * OTG_3D_STRUCTURE_CONTROL_OTG_3D_STRUCTURE_STEREO_SEL_OVR enum
 */

typedef enum OTG_3D_STRUCTURE_CONTROL_OTG_3D_STRUCTURE_STEREO_SEL_OVR {
OTG_3D_STRUCTURE_CONTROL_OTG_3D_STRUCTURE_STEREO_SEL_OVR_FALSE = 0x00000000,
OTG_3D_STRUCTURE_CONTROL_OTG_3D_STRUCTURE_STEREO_SEL_OVR_TRUE = 0x00000001,
} OTG_3D_STRUCTURE_CONTROL_OTG_3D_STRUCTURE_STEREO_SEL_OVR;

/*
 * OTG_3D_STRUCTURE_CONTROL_OTG_3D_STRUCTURE_V_UPDATE_MODE enum
 */

typedef enum OTG_3D_STRUCTURE_CONTROL_OTG_3D_STRUCTURE_V_UPDATE_MODE {
OTG_3D_STRUCTURE_CONTROL_OTG_3D_STRUCTURE_V_UPDATE_MODE_BLOCK_BOTH = 0x00000000,
OTG_3D_STRUCTURE_CONTROL_OTG_3D_STRUCTURE_V_UPDATE_MODE_BLOCK_INTERLACE = 0x00000001,
OTG_3D_STRUCTURE_CONTROL_OTG_3D_STRUCTURE_V_UPDATE_MODE_BLOCK_PROGRASSIVE = 0x00000002,
OTG_3D_STRUCTURE_CONTROL_OTG_3D_STRUCTURE_V_UPDATE_MODE_RESERVED = 0x00000003,
} OTG_3D_STRUCTURE_CONTROL_OTG_3D_STRUCTURE_V_UPDATE_MODE;

/*
 * OTG_CONTROL_OTG_DISABLE_POINT_CNTL enum
 */

typedef enum OTG_CONTROL_OTG_DISABLE_POINT_CNTL {
OTG_CONTROL_OTG_DISABLE_POINT_CNTL_DISABLE = 0x00000000,
OTG_CONTROL_OTG_DISABLE_POINT_CNTL_DISABLE_CURRENT = 0x00000001,
OTG_CONTROL_OTG_DISABLE_POINT_CNTL_DISABLE_VUPDATE = 0x00000002,
OTG_CONTROL_OTG_DISABLE_POINT_CNTL_DISABLE_FIRST = 0x00000003,
} OTG_CONTROL_OTG_DISABLE_POINT_CNTL;

/*
 * OTG_CONTROL_OTG_FIELD_NUMBER_CNTL enum
 */

typedef enum OTG_CONTROL_OTG_FIELD_NUMBER_CNTL {
OTG_CONTROL_OTG_FIELD_NUMBER_CNTL_NORMAL = 0x00000000,
OTG_CONTROL_OTG_FIELD_NUMBER_CNTL_DP     = 0x00000001,
} OTG_CONTROL_OTG_FIELD_NUMBER_CNTL;

/*
 * OTG_CONTROL_OTG_FIELD_NUMBER_POLARITY enum
 */

typedef enum OTG_CONTROL_OTG_FIELD_NUMBER_POLARITY {
OTG_CONTROL_OTG_FIELD_NUMBER_POLARITY_FALSE = 0x00000000,
OTG_CONTROL_OTG_FIELD_NUMBER_POLARITY_TRUE = 0x00000001,
} OTG_CONTROL_OTG_FIELD_NUMBER_POLARITY;

/*
 * OTG_CONTROL_OTG_MASTER_EN enum
 */

typedef enum OTG_CONTROL_OTG_MASTER_EN {
OTG_CONTROL_OTG_MASTER_EN_FALSE          = 0x00000000,
OTG_CONTROL_OTG_MASTER_EN_TRUE           = 0x00000001,
} OTG_CONTROL_OTG_MASTER_EN;

/*
 * OTG_CONTROL_OTG_OUT_MUX enum
 */

typedef enum OTG_CONTROL_OTG_OUT_MUX {
OTG_CONTROL_OTG_OUT_MUX_0                = 0x00000000,
OTG_CONTROL_OTG_OUT_MUX_1                = 0x00000001,
OTG_CONTROL_OTG_OUT_MUX_2                = 0x00000002,
} OTG_CONTROL_OTG_OUT_MUX;

/*
 * OTG_CONTROL_OTG_START_POINT_CNTL enum
 */

typedef enum OTG_CONTROL_OTG_START_POINT_CNTL {
OTG_CONTROL_OTG_START_POINT_CNTL_NORMAL  = 0x00000000,
OTG_CONTROL_OTG_START_POINT_CNTL_DP      = 0x00000001,
} OTG_CONTROL_OTG_START_POINT_CNTL;

/*
 * OTG_COUNT_CONTROL_OTG_HORZ_COUNT_BY2_EN enum
 */

typedef enum OTG_COUNT_CONTROL_OTG_HORZ_COUNT_BY2_EN {
OTG_COUNT_CONTROL_OTG_HORZ_COUNT_BY2_EN_FALSE = 0x00000000,
OTG_COUNT_CONTROL_OTG_HORZ_COUNT_BY2_EN_TRUE = 0x00000001,
} OTG_COUNT_CONTROL_OTG_HORZ_COUNT_BY2_EN;

/*
 * OTG_CRC_CNTL_OTG_CRC1_EN enum
 */

typedef enum OTG_CRC_CNTL_OTG_CRC1_EN {
OTG_CRC_CNTL_OTG_CRC1_EN_FALSE           = 0x00000000,
OTG_CRC_CNTL_OTG_CRC1_EN_TRUE            = 0x00000001,
} OTG_CRC_CNTL_OTG_CRC1_EN;

/*
 * OTG_CRC_CNTL_OTG_CRC_CONT_EN enum
 */

typedef enum OTG_CRC_CNTL_OTG_CRC_CONT_EN {
OTG_CRC_CNTL_OTG_CRC_CONT_EN_FALSE       = 0x00000000,
OTG_CRC_CNTL_OTG_CRC_CONT_EN_TRUE        = 0x00000001,
} OTG_CRC_CNTL_OTG_CRC_CONT_EN;

/*
 * OTG_CRC_CNTL_OTG_CRC_CONT_MODE enum
 */

typedef enum OTG_CRC_CNTL_OTG_CRC_CONT_MODE {
OTG_CRC_CNTL_OTG_CRC_CONT_MODE_RESET     = 0x00000000,
OTG_CRC_CNTL_OTG_CRC_CONT_MODE_NORESET   = 0x00000001,
} OTG_CRC_CNTL_OTG_CRC_CONT_MODE;

/*
 * OTG_CRC_CNTL_OTG_CRC_EN enum
 */

typedef enum OTG_CRC_CNTL_OTG_CRC_EN {
OTG_CRC_CNTL_OTG_CRC_EN_FALSE            = 0x00000000,
OTG_CRC_CNTL_OTG_CRC_EN_TRUE             = 0x00000001,
} OTG_CRC_CNTL_OTG_CRC_EN;

/*
 * OTG_CRC_CNTL_OTG_CRC_INTERLACE_MODE enum
 */

typedef enum OTG_CRC_CNTL_OTG_CRC_INTERLACE_MODE {
OTG_CRC_CNTL_OTG_CRC_INTERLACE_MODE_TOP  = 0x00000000,
OTG_CRC_CNTL_OTG_CRC_INTERLACE_MODE_BOTTOM = 0x00000001,
OTG_CRC_CNTL_OTG_CRC_INTERLACE_MODE_BOTH_BOTTOM = 0x00000002,
OTG_CRC_CNTL_OTG_CRC_INTERLACE_MODE_BOTH_FIELD = 0x00000003,
} OTG_CRC_CNTL_OTG_CRC_INTERLACE_MODE;

/*
 * OTG_CRC_CNTL_OTG_CRC_STEREO_MODE enum
 */

typedef enum OTG_CRC_CNTL_OTG_CRC_STEREO_MODE {
OTG_CRC_CNTL_OTG_CRC_STEREO_MODE_LEFT    = 0x00000000,
OTG_CRC_CNTL_OTG_CRC_STEREO_MODE_RIGHT   = 0x00000001,
OTG_CRC_CNTL_OTG_CRC_STEREO_MODE_BOTH_EYES = 0x00000002,
OTG_CRC_CNTL_OTG_CRC_STEREO_MODE_BOTH_FIELDS = 0x00000003,
} OTG_CRC_CNTL_OTG_CRC_STEREO_MODE;

/*
 * OTG_CRC_CNTL_OTG_CRC_USE_NEW_AND_REPEATED_PIXELS enum
 */

typedef enum OTG_CRC_CNTL_OTG_CRC_USE_NEW_AND_REPEATED_PIXELS {
OTG_CRC_CNTL_OTG_CRC_USE_NEW_AND_REPEATED_PIXELS_FALSE = 0x00000000,
OTG_CRC_CNTL_OTG_CRC_USE_NEW_AND_REPEATED_PIXELS_TRUE = 0x00000001,
} OTG_CRC_CNTL_OTG_CRC_USE_NEW_AND_REPEATED_PIXELS;

/*
 * OTG_CRC_CNTL_OTG_OTG_CRC0_SELECT enum
 */

typedef enum OTG_CRC_CNTL_OTG_OTG_CRC0_SELECT {
OTG_CRC_CNTL_OTG_OTG_CRC0_SELECT_UAB     = 0x00000000,
OTG_CRC_CNTL_OTG_OTG_CRC0_SELECT_UA_B    = 0x00000001,
OTG_CRC_CNTL_OTG_OTG_CRC0_SELECT_U_AB    = 0x00000002,
OTG_CRC_CNTL_OTG_OTG_CRC0_SELECT_U_A_B   = 0x00000003,
OTG_CRC_CNTL_OTG_OTG_CRC0_SELECT_IAB     = 0x00000004,
OTG_CRC_CNTL_OTG_OTG_CRC0_SELECT_IA_B    = 0x00000005,
OTG_CRC_CNTL_OTG_OTG_CRC0_SELECT_I_AB    = 0x00000006,
OTG_CRC_CNTL_OTG_OTG_CRC0_SELECT_I_A_B   = 0x00000007,
} OTG_CRC_CNTL_OTG_OTG_CRC0_SELECT;

/*
 * OTG_CRC_CNTL_OTG_OTG_CRC1_SELECT enum
 */

typedef enum OTG_CRC_CNTL_OTG_OTG_CRC1_SELECT {
OTG_CRC_CNTL_OTG_OTG_CRC1_SELECT_UAB     = 0x00000000,
OTG_CRC_CNTL_OTG_OTG_CRC1_SELECT_UA_B    = 0x00000001,
OTG_CRC_CNTL_OTG_OTG_CRC1_SELECT_U_AB    = 0x00000002,
OTG_CRC_CNTL_OTG_OTG_CRC1_SELECT_U_A_B   = 0x00000003,
OTG_CRC_CNTL_OTG_OTG_CRC1_SELECT_IAB     = 0x00000004,
OTG_CRC_CNTL_OTG_OTG_CRC1_SELECT_IA_B    = 0x00000005,
OTG_CRC_CNTL_OTG_OTG_CRC1_SELECT_I_AB    = 0x00000006,
OTG_CRC_CNTL_OTG_OTG_CRC1_SELECT_I_A_B   = 0x00000007,
} OTG_CRC_CNTL_OTG_OTG_CRC1_SELECT;

/*
 * OTG_DIG_UPDATE_VCOUNT_MODE enum
 */

typedef enum OTG_DIG_UPDATE_VCOUNT_MODE {
OTG_DIG_UPDATE_VCOUNT_0                  = 0x00000000,
OTG_DIG_UPDATE_VCOUNT_1                  = 0x00000001,
} OTG_DIG_UPDATE_VCOUNT_MODE;

/*
 * OTG_DOUBLE_BUFFER_CONTROL_OTG_DRR_TIMING_DBUF_UPDATE_MODE enum
 */

typedef enum OTG_DOUBLE_BUFFER_CONTROL_OTG_DRR_TIMING_DBUF_UPDATE_MODE {
OTG_DOUBLE_BUFFER_CONTROL_OTG_DRR_TIMING_DBUF_UPDATE_MODE_0 = 0x00000000,
OTG_DOUBLE_BUFFER_CONTROL_OTG_DRR_TIMING_DBUF_UPDATE_MODE_1 = 0x00000001,
OTG_DOUBLE_BUFFER_CONTROL_OTG_DRR_TIMING_DBUF_UPDATE_MODE_2 = 0x00000002,
OTG_DOUBLE_BUFFER_CONTROL_OTG_DRR_TIMING_DBUF_UPDATE_MODE_3 = 0x00000003,
} OTG_DOUBLE_BUFFER_CONTROL_OTG_DRR_TIMING_DBUF_UPDATE_MODE;

/*
 * OTG_DOUBLE_BUFFER_CONTROL_OTG_UPDATE_INSTANTLY enum
 */

typedef enum OTG_DOUBLE_BUFFER_CONTROL_OTG_UPDATE_INSTANTLY {
OTG_DOUBLE_BUFFER_CONTROL_OTG_UPDATE_INSTANTLY_FALSE = 0x00000000,
OTG_DOUBLE_BUFFER_CONTROL_OTG_UPDATE_INSTANTLY_TRUE = 0x00000001,
} OTG_DOUBLE_BUFFER_CONTROL_OTG_UPDATE_INSTANTLY;

/*
 * OTG_DRR_CONTROL_OTG_DRR_AVERAGE_FRAME enum
 */

typedef enum OTG_DRR_CONTROL_OTG_DRR_AVERAGE_FRAME {
OTG_DRR_CONTROL_OTG_DRR_AVERAGE_FRAME_1FRAME = 0x00000000,
OTG_DRR_CONTROL_OTG_DRR_AVERAGE_FRAME_2FRAME = 0x00000001,
OTG_DRR_CONTROL_OTG_DRR_AVERAGE_FRAME_4FRAME = 0x00000002,
OTG_DRR_CONTROL_OTG_DRR_AVERAGE_FRAME_8FRAME = 0x00000003,
} OTG_DRR_CONTROL_OTG_DRR_AVERAGE_FRAME;

/*
 * OTG_DTMTEST_CNTL_OTG_DTMTEST_OTG_EN enum
 */

typedef enum OTG_DTMTEST_CNTL_OTG_DTMTEST_OTG_EN {
OTG_DTMTEST_CNTL_OTG_DTMTEST_OTG_EN_FALSE = 0x00000000,
OTG_DTMTEST_CNTL_OTG_DTMTEST_OTG_EN_TRUE = 0x00000001,
} OTG_DTMTEST_CNTL_OTG_DTMTEST_OTG_EN;

/*
 * OTG_FLOW_CONTROL_OTG_FLOW_CONTROL_GRANULARITY enum
 */

typedef enum OTG_FLOW_CONTROL_OTG_FLOW_CONTROL_GRANULARITY {
OTG_FLOW_CONTROL_OTG_FLOW_CONTROL_GRANULARITY_FALSE = 0x00000000,
OTG_FLOW_CONTROL_OTG_FLOW_CONTROL_GRANULARITY_TRUE = 0x00000001,
} OTG_FLOW_CONTROL_OTG_FLOW_CONTROL_GRANULARITY;

/*
 * OTG_FLOW_CONTROL_OTG_FLOW_CONTROL_POLARITY enum
 */

typedef enum OTG_FLOW_CONTROL_OTG_FLOW_CONTROL_POLARITY {
OTG_FLOW_CONTROL_OTG_FLOW_CONTROL_POLARITY_FALSE = 0x00000000,
OTG_FLOW_CONTROL_OTG_FLOW_CONTROL_POLARITY_TRUE = 0x00000001,
} OTG_FLOW_CONTROL_OTG_FLOW_CONTROL_POLARITY;

/*
 * OTG_FLOW_CONTROL_OTG_FLOW_CONTROL_SOURCE_SELECT enum
 */

typedef enum OTG_FLOW_CONTROL_OTG_FLOW_CONTROL_SOURCE_SELECT {
OTG_FLOW_CONTROL_OTG_FLOW_CONTROL_SOURCE_SELECT_LOGIC0 = 0x00000000,
OTG_FLOW_CONTROL_OTG_FLOW_CONTROL_SOURCE_SELECT_LOGIC1 = 0x00000001,
OTG_FLOW_CONTROL_OTG_FLOW_CONTROL_SOURCE_SELECT_GENERICA = 0x00000002,
OTG_FLOW_CONTROL_OTG_FLOW_CONTROL_SOURCE_SELECT_GENERICB = 0x00000003,
OTG_FLOW_CONTROL_OTG_FLOW_CONTROL_SOURCE_SELECT_GENERICC = 0x00000004,
OTG_FLOW_CONTROL_OTG_FLOW_CONTROL_SOURCE_SELECT_GENERICD = 0x00000005,
OTG_FLOW_CONTROL_OTG_FLOW_CONTROL_SOURCE_SELECT_GENERICE = 0x00000006,
OTG_FLOW_CONTROL_OTG_FLOW_CONTROL_SOURCE_SELECT_GENERICF = 0x00000007,
OTG_FLOW_CONTROL_OTG_FLOW_CONTROL_SOURCE_SELECT_HPD1 = 0x00000008,
OTG_FLOW_CONTROL_OTG_FLOW_CONTROL_SOURCE_SELECT_HPD2 = 0x00000009,
OTG_FLOW_CONTROL_OTG_FLOW_CONTROL_SOURCE_SELECT_DDC1DATA = 0x0000000a,
OTG_FLOW_CONTROL_OTG_FLOW_CONTROL_SOURCE_SELECT_DDC1CLK = 0x0000000b,
OTG_FLOW_CONTROL_OTG_FLOW_CONTROL_SOURCE_SELECT_DDC2DATA = 0x0000000c,
OTG_FLOW_CONTROL_OTG_FLOW_CONTROL_SOURCE_SELECT_DDC2CLK = 0x0000000d,
OTG_FLOW_CONTROL_OTG_FLOW_CONTROL_SOURCE_SELECT_MANUAL_FLOW_CONTROL = 0x0000000e,
OTG_FLOW_CONTROL_OTG_FLOW_CONTROL_SOURCE_SELECT_RESERVED = 0x0000000f,
OTG_FLOW_CONTROL_OTG_FLOW_CONTROL_SOURCE_SELECT_GENLK_CLK = 0x00000010,
OTG_FLOW_CONTROL_OTG_FLOW_CONTROL_SOURCE_SELECT_GENLK_VSYNC = 0x00000011,
OTG_FLOW_CONTROL_OTG_FLOW_CONTROL_SOURCE_SELECT_SWAPLOCKA = 0x00000012,
OTG_FLOW_CONTROL_OTG_FLOW_CONTROL_SOURCE_SELECT_SWAPLOCKB = 0x00000013,
} OTG_FLOW_CONTROL_OTG_FLOW_CONTROL_SOURCE_SELECT;

/*
 * OTG_FORCE_COUNT_NOW_CNTL_OTG_FORCE_COUNT_NOW_CHECK enum
 */

typedef enum OTG_FORCE_COUNT_NOW_CNTL_OTG_FORCE_COUNT_NOW_CHECK {
OTG_FORCE_COUNT_NOW_CNTL_OTG_FORCE_COUNT_NOW_CHECK_FALSE = 0x00000000,
OTG_FORCE_COUNT_NOW_CNTL_OTG_FORCE_COUNT_NOW_CHECK_TRUE = 0x00000001,
} OTG_FORCE_COUNT_NOW_CNTL_OTG_FORCE_COUNT_NOW_CHECK;

/*
 * OTG_FORCE_COUNT_NOW_CNTL_OTG_FORCE_COUNT_NOW_CLEAR enum
 */

typedef enum OTG_FORCE_COUNT_NOW_CNTL_OTG_FORCE_COUNT_NOW_CLEAR {
OTG_FORCE_COUNT_NOW_CNTL_OTG_FORCE_COUNT_NOW_CLEAR_FALSE = 0x00000000,
OTG_FORCE_COUNT_NOW_CNTL_OTG_FORCE_COUNT_NOW_CLEAR_TRUE = 0x00000001,
} OTG_FORCE_COUNT_NOW_CNTL_OTG_FORCE_COUNT_NOW_CLEAR;

/*
 * OTG_FORCE_COUNT_NOW_CNTL_OTG_FORCE_COUNT_NOW_MODE enum
 */

typedef enum OTG_FORCE_COUNT_NOW_CNTL_OTG_FORCE_COUNT_NOW_MODE {
OTG_FORCE_COUNT_NOW_CNTL_OTG_FORCE_COUNT_NOW_MODE_DISABLE = 0x00000000,
OTG_FORCE_COUNT_NOW_CNTL_OTG_FORCE_COUNT_NOW_MODE_HCOUNT = 0x00000001,
OTG_FORCE_COUNT_NOW_CNTL_OTG_FORCE_COUNT_NOW_MODE_HCOUNT_VCOUNT = 0x00000002,
OTG_FORCE_COUNT_NOW_CNTL_OTG_FORCE_COUNT_NOW_MODE_RESERVED = 0x00000003,
} OTG_FORCE_COUNT_NOW_CNTL_OTG_FORCE_COUNT_NOW_MODE;

/*
 * OTG_FORCE_COUNT_NOW_CNTL_OTG_FORCE_COUNT_NOW_TRIG_SEL enum
 */

typedef enum OTG_FORCE_COUNT_NOW_CNTL_OTG_FORCE_COUNT_NOW_TRIG_SEL {
OTG_FORCE_COUNT_NOW_CNTL_OTG_FORCE_COUNT_NOW_TRIG_SEL_FALSE = 0x00000000,
OTG_FORCE_COUNT_NOW_CNTL_OTG_FORCE_COUNT_NOW_TRIG_SEL_TRUE = 0x00000001,
} OTG_FORCE_COUNT_NOW_CNTL_OTG_FORCE_COUNT_NOW_TRIG_SEL;

/*
 * OTG_GLOBAL_CONTROL2_MANUAL_FLOW_CONTROL_SEL enum
 */

typedef enum OTG_GLOBAL_CONTROL2_MANUAL_FLOW_CONTROL_SEL {
OTG_GLOBAL_CONTROL2_MANUAL_FLOW_CONTROL_SEL_OTG0 = 0x00000000,
OTG_GLOBAL_CONTROL2_MANUAL_FLOW_CONTROL_SEL_OTG1 = 0x00000001,
OTG_GLOBAL_CONTROL2_MANUAL_FLOW_CONTROL_SEL_OTG2 = 0x00000002,
OTG_GLOBAL_CONTROL2_MANUAL_FLOW_CONTROL_SEL_OTG3 = 0x00000003,
OTG_GLOBAL_CONTROL2_MANUAL_FLOW_CONTROL_SEL_RESERVED4 = 0x00000004,
OTG_GLOBAL_CONTROL2_MANUAL_FLOW_CONTROL_SEL_RESERVED5 = 0x00000005,
} OTG_GLOBAL_CONTROL2_MANUAL_FLOW_CONTROL_SEL;

/*
 * OTG_GLOBAL_CONTROL3_DIG_UPDATE_EYE_SEL enum
 */

typedef enum OTG_GLOBAL_CONTROL3_DIG_UPDATE_EYE_SEL {
DIG_UPDATE_EYE_SEL_BOTH                  = 0x00000000,
DIG_UPDATE_EYE_SEL_LEFT                  = 0x00000001,
DIG_UPDATE_EYE_SEL_RIGHT                 = 0x00000002,
} OTG_GLOBAL_CONTROL3_DIG_UPDATE_EYE_SEL;

/*
 * OTG_GLOBAL_CONTROL3_DIG_UPDATE_FIELD_SEL enum
 */

typedef enum OTG_GLOBAL_CONTROL3_DIG_UPDATE_FIELD_SEL {
DIG_UPDATE_FIELD_SEL_BOTH                = 0x00000000,
DIG_UPDATE_FIELD_SEL_TOP                 = 0x00000001,
DIG_UPDATE_FIELD_SEL_BOTTOM              = 0x00000002,
DIG_UPDATE_FIELD_SEL_RESERVED            = 0x00000003,
} OTG_GLOBAL_CONTROL3_DIG_UPDATE_FIELD_SEL;

/*
 * OTG_GLOBAL_CONTROL3_MASTER_UPDATE_LOCK_DB_FIELD enum
 */

typedef enum OTG_GLOBAL_CONTROL3_MASTER_UPDATE_LOCK_DB_FIELD {
MASTER_UPDATE_LOCK_DB_FIELD_BOTH         = 0x00000000,
MASTER_UPDATE_LOCK_DB_FIELD_TOP          = 0x00000001,
MASTER_UPDATE_LOCK_DB_FIELD_BOTTOM       = 0x00000002,
MASTER_UPDATE_LOCK_DB_FIELD_RESERVED     = 0x00000003,
} OTG_GLOBAL_CONTROL3_MASTER_UPDATE_LOCK_DB_FIELD;

/*
 * OTG_GLOBAL_CONTROL3_MASTER_UPDATE_LOCK_DB_STEREO_SEL enum
 */

typedef enum OTG_GLOBAL_CONTROL3_MASTER_UPDATE_LOCK_DB_STEREO_SEL {
MASTER_UPDATE_LOCK_DB_STEREO_SEL_BOTH    = 0x00000000,
MASTER_UPDATE_LOCK_DB_STEREO_SEL_LEFT    = 0x00000001,
MASTER_UPDATE_LOCK_DB_STEREO_SEL_RIGHT   = 0x00000002,
MASTER_UPDATE_LOCK_DB_STEREO_SEL_RESERVED = 0x00000003,
} OTG_GLOBAL_CONTROL3_MASTER_UPDATE_LOCK_DB_STEREO_SEL;

/*
 * OTG_GLOBAL_UPDATE_LOCK_EN enum
 */

typedef enum OTG_GLOBAL_UPDATE_LOCK_EN {
OTG_GLOBAL_UPDATE_LOCK_DISABLE           = 0x00000000,
OTG_GLOBAL_UPDATE_LOCK_ENABLE            = 0x00000001,
} OTG_GLOBAL_UPDATE_LOCK_EN;

/*
 * OTG_GSL_MASTER_MODE enum
 */

typedef enum OTG_GSL_MASTER_MODE {
OTG_GSL_MASTER_MODE_0                    = 0x00000000,
OTG_GSL_MASTER_MODE_1                    = 0x00000001,
OTG_GSL_MASTER_MODE_2                    = 0x00000002,
OTG_GSL_MASTER_MODE_3                    = 0x00000003,
} OTG_GSL_MASTER_MODE;

/*
 * OTG_HORZ_REPETITION_COUNT enum
 */

typedef enum OTG_HORZ_REPETITION_COUNT {
OTG_HORZ_REPETITION_COUNT_0              = 0x00000000,
OTG_HORZ_REPETITION_COUNT_1              = 0x00000001,
OTG_HORZ_REPETITION_COUNT_2              = 0x00000002,
OTG_HORZ_REPETITION_COUNT_3              = 0x00000003,
OTG_HORZ_REPETITION_COUNT_4              = 0x00000004,
OTG_HORZ_REPETITION_COUNT_5              = 0x00000005,
OTG_HORZ_REPETITION_COUNT_6              = 0x00000006,
OTG_HORZ_REPETITION_COUNT_7              = 0x00000007,
OTG_HORZ_REPETITION_COUNT_8              = 0x00000008,
OTG_HORZ_REPETITION_COUNT_9              = 0x00000009,
OTG_HORZ_REPETITION_COUNT_10             = 0x0000000a,
OTG_HORZ_REPETITION_COUNT_11             = 0x0000000b,
OTG_HORZ_REPETITION_COUNT_12             = 0x0000000c,
OTG_HORZ_REPETITION_COUNT_13             = 0x0000000d,
OTG_HORZ_REPETITION_COUNT_14             = 0x0000000e,
OTG_HORZ_REPETITION_COUNT_15             = 0x0000000f,
} OTG_HORZ_REPETITION_COUNT;

/*
 * OTG_H_SYNC_A_POL enum
 */

typedef enum OTG_H_SYNC_A_POL {
OTG_H_SYNC_A_POL_HIGH                    = 0x00000000,
OTG_H_SYNC_A_POL_LOW                     = 0x00000001,
} OTG_H_SYNC_A_POL;

/*
 * OTG_H_TIMING_DIV_MODE enum
 */

typedef enum OTG_H_TIMING_DIV_MODE {
OTG_H_TIMING_DIV_MODE_NO_DIV             = 0x00000000,
OTG_H_TIMING_DIV_MODE_DIV_BY2            = 0x00000001,
OTG_H_TIMING_DIV_MODE_RESERVED           = 0x00000002,
OTG_H_TIMING_DIV_MODE_DIV_BY4            = 0x00000003,
} OTG_H_TIMING_DIV_MODE;

/*
 * OTG_H_TIMING_DIV_MODE_MANUAL enum
 */

typedef enum OTG_H_TIMING_DIV_MODE_MANUAL {
OTG_H_TIMING_DIV_MODE_AUTO               = 0x00000000,
OTG_H_TIMING_DIV_MODE_NOAUTO             = 0x00000001,
} OTG_H_TIMING_DIV_MODE_MANUAL;

/*
 * OTG_INTERLACE_CONTROL_OTG_INTERLACE_ENABLE enum
 */

typedef enum OTG_INTERLACE_CONTROL_OTG_INTERLACE_ENABLE {
OTG_INTERLACE_CONTROL_OTG_INTERLACE_ENABLE_FALSE = 0x00000000,
OTG_INTERLACE_CONTROL_OTG_INTERLACE_ENABLE_TRUE = 0x00000001,
} OTG_INTERLACE_CONTROL_OTG_INTERLACE_ENABLE;

/*
 * OTG_INTERLACE_CONTROL_OTG_INTERLACE_FORCE_NEXT_FIELD enum
 */

typedef enum OTG_INTERLACE_CONTROL_OTG_INTERLACE_FORCE_NEXT_FIELD {
OTG_INTERLACE_CONTROL_OTG_INTERLACE_FORCE_NEXT_FIELD_NOT = 0x00000000,
OTG_INTERLACE_CONTROL_OTG_INTERLACE_FORCE_NEXT_FIELD_BOTTOM = 0x00000001,
OTG_INTERLACE_CONTROL_OTG_INTERLACE_FORCE_NEXT_FIELD_TOP = 0x00000002,
OTG_INTERLACE_CONTROL_OTG_INTERLACE_FORCE_NEXT_FIELD_NOT2 = 0x00000003,
} OTG_INTERLACE_CONTROL_OTG_INTERLACE_FORCE_NEXT_FIELD;

/*
 * OTG_INTERRUPT_CONTROL_OTG_FORCE_COUNT_NOW_INT_MSK enum
 */

typedef enum OTG_INTERRUPT_CONTROL_OTG_FORCE_COUNT_NOW_INT_MSK {
OTG_INTERRUPT_CONTROL_OTG_FORCE_COUNT_NOW_INT_MSK_FALSE = 0x00000000,
OTG_INTERRUPT_CONTROL_OTG_FORCE_COUNT_NOW_INT_MSK_TRUE = 0x00000001,
} OTG_INTERRUPT_CONTROL_OTG_FORCE_COUNT_NOW_INT_MSK;

/*
 * OTG_INTERRUPT_CONTROL_OTG_FORCE_COUNT_NOW_INT_TYPE enum
 */

typedef enum OTG_INTERRUPT_CONTROL_OTG_FORCE_COUNT_NOW_INT_TYPE {
OTG_INTERRUPT_CONTROL_OTG_FORCE_COUNT_NOW_INT_TYPE_FALSE = 0x00000000,
OTG_INTERRUPT_CONTROL_OTG_FORCE_COUNT_NOW_INT_TYPE_TRUE = 0x00000001,
} OTG_INTERRUPT_CONTROL_OTG_FORCE_COUNT_NOW_INT_TYPE;

/*
 * OTG_INTERRUPT_CONTROL_OTG_FORCE_VSYNC_NEXT_LINE_INT_MSK enum
 */

typedef enum OTG_INTERRUPT_CONTROL_OTG_FORCE_VSYNC_NEXT_LINE_INT_MSK {
OTG_INTERRUPT_CONTROL_OTG_FORCE_VSYNC_NEXT_LINE_INT_MSK_FALSE = 0x00000000,
OTG_INTERRUPT_CONTROL_OTG_FORCE_VSYNC_NEXT_LINE_INT_MSK_TRUE = 0x00000001,
} OTG_INTERRUPT_CONTROL_OTG_FORCE_VSYNC_NEXT_LINE_INT_MSK;

/*
 * OTG_INTERRUPT_CONTROL_OTG_FORCE_VSYNC_NEXT_LINE_INT_TYPE enum
 */

typedef enum OTG_INTERRUPT_CONTROL_OTG_FORCE_VSYNC_NEXT_LINE_INT_TYPE {
OTG_INTERRUPT_CONTROL_OTG_FORCE_VSYNC_NEXT_LINE_INT_TYPE_FALSE = 0x00000000,
OTG_INTERRUPT_CONTROL_OTG_FORCE_VSYNC_NEXT_LINE_INT_TYPE_TRUE = 0x00000001,
} OTG_INTERRUPT_CONTROL_OTG_FORCE_VSYNC_NEXT_LINE_INT_TYPE;

/*
 * OTG_INTERRUPT_CONTROL_OTG_GSL_VSYNC_GAP_INT_MSK enum
 */

typedef enum OTG_INTERRUPT_CONTROL_OTG_GSL_VSYNC_GAP_INT_MSK {
OTG_INTERRUPT_CONTROL_OTG_GSL_VSYNC_GAP_INT_MSK_FALSE = 0x00000000,
OTG_INTERRUPT_CONTROL_OTG_GSL_VSYNC_GAP_INT_MSK_TRUE = 0x00000001,
} OTG_INTERRUPT_CONTROL_OTG_GSL_VSYNC_GAP_INT_MSK;

/*
 * OTG_INTERRUPT_CONTROL_OTG_GSL_VSYNC_GAP_INT_TYPE enum
 */

typedef enum OTG_INTERRUPT_CONTROL_OTG_GSL_VSYNC_GAP_INT_TYPE {
OTG_INTERRUPT_CONTROL_OTG_GSL_VSYNC_GAP_INT_TYPE_FALSE = 0x00000000,
OTG_INTERRUPT_CONTROL_OTG_GSL_VSYNC_GAP_INT_TYPE_TRUE = 0x00000001,
} OTG_INTERRUPT_CONTROL_OTG_GSL_VSYNC_GAP_INT_TYPE;

/*
 * OTG_INTERRUPT_CONTROL_OTG_SNAPSHOT_INT_MSK enum
 */

typedef enum OTG_INTERRUPT_CONTROL_OTG_SNAPSHOT_INT_MSK {
OTG_INTERRUPT_CONTROL_OTG_SNAPSHOT_INT_MSK_FALSE = 0x00000000,
OTG_INTERRUPT_CONTROL_OTG_SNAPSHOT_INT_MSK_TRUE = 0x00000001,
} OTG_INTERRUPT_CONTROL_OTG_SNAPSHOT_INT_MSK;

/*
 * OTG_INTERRUPT_CONTROL_OTG_SNAPSHOT_INT_TYPE enum
 */

typedef enum OTG_INTERRUPT_CONTROL_OTG_SNAPSHOT_INT_TYPE {
OTG_INTERRUPT_CONTROL_OTG_SNAPSHOT_INT_TYPE_FALSE = 0x00000000,
OTG_INTERRUPT_CONTROL_OTG_SNAPSHOT_INT_TYPE_TRUE = 0x00000001,
} OTG_INTERRUPT_CONTROL_OTG_SNAPSHOT_INT_TYPE;

/*
 * OTG_INTERRUPT_CONTROL_OTG_TRIGA_INT_MSK enum
 */

typedef enum OTG_INTERRUPT_CONTROL_OTG_TRIGA_INT_MSK {
OTG_INTERRUPT_CONTROL_OTG_TRIGA_INT_MSK_FALSE = 0x00000000,
OTG_INTERRUPT_CONTROL_OTG_TRIGA_INT_MSK_TRUE = 0x00000001,
} OTG_INTERRUPT_CONTROL_OTG_TRIGA_INT_MSK;

/*
 * OTG_INTERRUPT_CONTROL_OTG_TRIGA_INT_TYPE enum
 */

typedef enum OTG_INTERRUPT_CONTROL_OTG_TRIGA_INT_TYPE {
OTG_INTERRUPT_CONTROL_OTG_TRIGA_INT_TYPE_FALSE = 0x00000000,
OTG_INTERRUPT_CONTROL_OTG_TRIGA_INT_TYPE_TRUE = 0x00000001,
} OTG_INTERRUPT_CONTROL_OTG_TRIGA_INT_TYPE;

/*
 * OTG_INTERRUPT_CONTROL_OTG_TRIGB_INT_MSK enum
 */

typedef enum OTG_INTERRUPT_CONTROL_OTG_TRIGB_INT_MSK {
OTG_INTERRUPT_CONTROL_OTG_TRIGB_INT_MSK_FALSE = 0x00000000,
OTG_INTERRUPT_CONTROL_OTG_TRIGB_INT_MSK_TRUE = 0x00000001,
} OTG_INTERRUPT_CONTROL_OTG_TRIGB_INT_MSK;

/*
 * OTG_INTERRUPT_CONTROL_OTG_TRIGB_INT_TYPE enum
 */

typedef enum OTG_INTERRUPT_CONTROL_OTG_TRIGB_INT_TYPE {
OTG_INTERRUPT_CONTROL_OTG_TRIGB_INT_TYPE_FALSE = 0x00000000,
OTG_INTERRUPT_CONTROL_OTG_TRIGB_INT_TYPE_TRUE = 0x00000001,
} OTG_INTERRUPT_CONTROL_OTG_TRIGB_INT_TYPE;

/*
 * OTG_INTERRUPT_CONTROL_OTG_VSYNC_NOM_INT_MSK enum
 */

typedef enum OTG_INTERRUPT_CONTROL_OTG_VSYNC_NOM_INT_MSK {
OTG_INTERRUPT_CONTROL_OTG_VSYNC_NOM_INT_MSK_FALSE = 0x00000000,
OTG_INTERRUPT_CONTROL_OTG_VSYNC_NOM_INT_MSK_TRUE = 0x00000001,
} OTG_INTERRUPT_CONTROL_OTG_VSYNC_NOM_INT_MSK;

/*
 * OTG_INTERRUPT_CONTROL_OTG_VSYNC_NOM_INT_TYPE enum
 */

typedef enum OTG_INTERRUPT_CONTROL_OTG_VSYNC_NOM_INT_TYPE {
OTG_INTERRUPT_CONTROL_OTG_VSYNC_NOM_INT_TYPE_FALSE = 0x00000000,
OTG_INTERRUPT_CONTROL_OTG_VSYNC_NOM_INT_TYPE_TRUE = 0x00000001,
} OTG_INTERRUPT_CONTROL_OTG_VSYNC_NOM_INT_TYPE;

/*
 * OTG_MANUAL_FORCE_VSYNC_NEXT_LINE_OTG_MANUAL_FORCE_VSYNC_NEXT_LINE enum
 */

typedef enum OTG_MANUAL_FORCE_VSYNC_NEXT_LINE_OTG_MANUAL_FORCE_VSYNC_NEXT_LINE {
OTG_MANUAL_FORCE_VSYNC_NEXT_LINE_OTG_MANUAL_FORCE_VSYNC_NEXT_LINE_FALSE = 0x00000000,
OTG_MANUAL_FORCE_VSYNC_NEXT_LINE_OTG_MANUAL_FORCE_VSYNC_NEXT_LINE_TRUE = 0x00000001,
} OTG_MANUAL_FORCE_VSYNC_NEXT_LINE_OTG_MANUAL_FORCE_VSYNC_NEXT_LINE;

/*
 * OTG_MASTER_UPDATE_LOCK_DB_EN enum
 */

typedef enum OTG_MASTER_UPDATE_LOCK_DB_EN {
OTG_MASTER_UPDATE_LOCK_DISABLE           = 0x00000000,
OTG_MASTER_UPDATE_LOCK_ENABLE            = 0x00000001,
} OTG_MASTER_UPDATE_LOCK_DB_EN;

/*
 * OTG_MASTER_UPDATE_LOCK_GSL_EN enum
 */

typedef enum OTG_MASTER_UPDATE_LOCK_GSL_EN {
OTG_MASTER_UPDATE_LOCK_GSL_EN_FALSE      = 0x00000000,
OTG_MASTER_UPDATE_LOCK_GSL_EN_TRUE       = 0x00000001,
} OTG_MASTER_UPDATE_LOCK_GSL_EN;

/*
 * OTG_MASTER_UPDATE_LOCK_VCOUNT_MODE enum
 */

typedef enum OTG_MASTER_UPDATE_LOCK_VCOUNT_MODE {
OTG_MASTER_UPDATE_LOCK_VCOUNT_0          = 0x00000000,
OTG_MASTER_UPDATE_LOCK_VCOUNT_1          = 0x00000001,
} OTG_MASTER_UPDATE_LOCK_VCOUNT_MODE;

/*
 * OTG_SNAPSHOT_CONTROL_OTG_AUTO_SNAPSHOT_TRIG_SEL enum
 */

typedef enum OTG_SNAPSHOT_CONTROL_OTG_AUTO_SNAPSHOT_TRIG_SEL {
OTG_SNAPSHOT_CONTROL_OTG_AUTO_SNAPSHOT_TRIG_SEL_DISABLE = 0x00000000,
OTG_SNAPSHOT_CONTROL_OTG_AUTO_SNAPSHOT_TRIG_SEL_TRIGGERA = 0x00000001,
OTG_SNAPSHOT_CONTROL_OTG_AUTO_SNAPSHOT_TRIG_SEL_TRIGGERB = 0x00000002,
OTG_SNAPSHOT_CONTROL_OTG_AUTO_SNAPSHOT_TRIG_SEL_RESERVED = 0x00000003,
} OTG_SNAPSHOT_CONTROL_OTG_AUTO_SNAPSHOT_TRIG_SEL;

/*
 * OTG_SNAPSHOT_STATUS_OTG_SNAPSHOT_CLEAR enum
 */

typedef enum OTG_SNAPSHOT_STATUS_OTG_SNAPSHOT_CLEAR {
OTG_SNAPSHOT_STATUS_OTG_SNAPSHOT_CLEAR_FALSE = 0x00000000,
OTG_SNAPSHOT_STATUS_OTG_SNAPSHOT_CLEAR_TRUE = 0x00000001,
} OTG_SNAPSHOT_STATUS_OTG_SNAPSHOT_CLEAR;

/*
 * OTG_STATIC_SCREEN_CONTROL_OTG_CPU_SS_INT_CLEAR enum
 */

typedef enum OTG_STATIC_SCREEN_CONTROL_OTG_CPU_SS_INT_CLEAR {
OTG_STATIC_SCREEN_CONTROL_OTG_CPU_SS_INT_CLEAR_FALSE = 0x00000000,
OTG_STATIC_SCREEN_CONTROL_OTG_CPU_SS_INT_CLEAR_TRUE = 0x00000001,
} OTG_STATIC_SCREEN_CONTROL_OTG_CPU_SS_INT_CLEAR;

/*
 * OTG_STATIC_SCREEN_CONTROL_OTG_CPU_SS_INT_ENABLE enum
 */

typedef enum OTG_STATIC_SCREEN_CONTROL_OTG_CPU_SS_INT_ENABLE {
OTG_STATIC_SCREEN_CONTROL_OTG_CPU_SS_INT_ENABLE_FALSE = 0x00000000,
OTG_STATIC_SCREEN_CONTROL_OTG_CPU_SS_INT_ENABLE_TRUE = 0x00000001,
} OTG_STATIC_SCREEN_CONTROL_OTG_CPU_SS_INT_ENABLE;

/*
 * OTG_STATIC_SCREEN_CONTROL_OTG_CPU_SS_INT_TYPE enum
 */

typedef enum OTG_STATIC_SCREEN_CONTROL_OTG_CPU_SS_INT_TYPE {
OTG_STATIC_SCREEN_CONTROL_OTG_CPU_SS_INT_TYPE_FALSE = 0x00000000,
OTG_STATIC_SCREEN_CONTROL_OTG_CPU_SS_INT_TYPE_TRUE = 0x00000001,
} OTG_STATIC_SCREEN_CONTROL_OTG_CPU_SS_INT_TYPE;

/*
 * OTG_STATIC_SCREEN_CONTROL_OTG_STATIC_SCREEN_OVERRIDE enum
 */

typedef enum OTG_STATIC_SCREEN_CONTROL_OTG_STATIC_SCREEN_OVERRIDE {
OTG_STATIC_SCREEN_CONTROL_OTG_STATIC_SCREEN_OVERRIDE_FALSE = 0x00000000,
OTG_STATIC_SCREEN_CONTROL_OTG_STATIC_SCREEN_OVERRIDE_TRUE = 0x00000001,
} OTG_STATIC_SCREEN_CONTROL_OTG_STATIC_SCREEN_OVERRIDE;

/*
 * OTG_STATIC_SCREEN_CONTROL_OTG_STATIC_SCREEN_OVERRIDE_VALUE enum
 */

typedef enum OTG_STATIC_SCREEN_CONTROL_OTG_STATIC_SCREEN_OVERRIDE_VALUE {
OTG_STATIC_SCREEN_CONTROL_OTG_STATIC_SCREEN_OVERRIDE_VALUE_OFF = 0x00000000,
OTG_STATIC_SCREEN_CONTROL_OTG_STATIC_SCREEN_OVERRIDE_VALUE_ON = 0x00000001,
} OTG_STATIC_SCREEN_CONTROL_OTG_STATIC_SCREEN_OVERRIDE_VALUE;

/*
 * OTG_STEREO_CONTROL_OTG_FIELD_NUM_SEL enum
 */

typedef enum OTG_STEREO_CONTROL_OTG_FIELD_NUM_SEL {
OTG_STEREO_CONTROL_OTG_FIELD_NUM_SEL_FALSE = 0x00000000,
OTG_STEREO_CONTROL_OTG_FIELD_NUM_SEL_TRUE = 0x00000001,
} OTG_STEREO_CONTROL_OTG_FIELD_NUM_SEL;

/*
 * OTG_STEREO_CONTROL_OTG_STEREO_EN enum
 */

typedef enum OTG_STEREO_CONTROL_OTG_STEREO_EN {
OTG_STEREO_CONTROL_OTG_STEREO_EN_FALSE   = 0x00000000,
OTG_STEREO_CONTROL_OTG_STEREO_EN_TRUE    = 0x00000001,
} OTG_STEREO_CONTROL_OTG_STEREO_EN;

/*
 * OTG_STEREO_CONTROL_OTG_STEREO_EYE_FLAG_POLARITY enum
 */

typedef enum OTG_STEREO_CONTROL_OTG_STEREO_EYE_FLAG_POLARITY {
OTG_STEREO_CONTROL_OTG_STEREO_EYE_FLAG_POLARITY_FALSE = 0x00000000,
OTG_STEREO_CONTROL_OTG_STEREO_EYE_FLAG_POLARITY_TRUE = 0x00000001,
} OTG_STEREO_CONTROL_OTG_STEREO_EYE_FLAG_POLARITY;

/*
 * OTG_STEREO_CONTROL_OTG_STEREO_SYNC_OUTPUT_POLARITY enum
 */

typedef enum OTG_STEREO_CONTROL_OTG_STEREO_SYNC_OUTPUT_POLARITY {
OTG_STEREO_CONTROL_OTG_STEREO_SYNC_OUTPUT_POLARITY_FALSE = 0x00000000,
OTG_STEREO_CONTROL_OTG_STEREO_SYNC_OUTPUT_POLARITY_TRUE = 0x00000001,
} OTG_STEREO_CONTROL_OTG_STEREO_SYNC_OUTPUT_POLARITY;

/*
 * OTG_STEREO_FORCE_NEXT_EYE_OTG_STEREO_FORCE_NEXT_EYE enum
 */

typedef enum OTG_STEREO_FORCE_NEXT_EYE_OTG_STEREO_FORCE_NEXT_EYE {
OTG_STEREO_FORCE_NEXT_EYE_OTG_STEREO_FORCE_NEXT_EYE_NO = 0x00000000,
OTG_STEREO_FORCE_NEXT_EYE_OTG_STEREO_FORCE_NEXT_EYE_RIGHT = 0x00000001,
OTG_STEREO_FORCE_NEXT_EYE_OTG_STEREO_FORCE_NEXT_EYE_LEFT = 0x00000002,
OTG_STEREO_FORCE_NEXT_EYE_OTG_STEREO_FORCE_NEXT_EYE_RESERVED = 0x00000003,
} OTG_STEREO_FORCE_NEXT_EYE_OTG_STEREO_FORCE_NEXT_EYE;

/*
 * OTG_TRIGA_CNTL_OTG_TRIGA_CLEAR enum
 */

typedef enum OTG_TRIGA_CNTL_OTG_TRIGA_CLEAR {
OTG_TRIGA_CNTL_OTG_TRIGA_CLEAR_FALSE     = 0x00000000,
OTG_TRIGA_CNTL_OTG_TRIGA_CLEAR_TRUE      = 0x00000001,
} OTG_TRIGA_CNTL_OTG_TRIGA_CLEAR;

/*
 * OTG_TRIGA_CNTL_OTG_TRIGA_POLARITY_SELECT enum
 */

typedef enum OTG_TRIGA_CNTL_OTG_TRIGA_POLARITY_SELECT {
OTG_TRIGA_CNTL_OTG_TRIGA_POLARITY_SELECT_LOGIC0 = 0x00000000,
OTG_TRIGA_CNTL_OTG_TRIGA_POLARITY_SELECT_INTERLACE = 0x00000001,
OTG_TRIGA_CNTL_OTG_TRIGA_POLARITY_SELECT_GENERICA = 0x00000002,
OTG_TRIGA_CNTL_OTG_TRIGA_POLARITY_SELECT_GENERICB = 0x00000003,
OTG_TRIGA_CNTL_OTG_TRIGA_POLARITY_SELECT_HSYNCA = 0x00000004,
OTG_TRIGA_CNTL_OTG_TRIGA_POLARITY_SELECT_LOGIC1 = 0x00000005,
OTG_TRIGA_CNTL_OTG_TRIGA_POLARITY_SELECT_GENERICC = 0x00000006,
OTG_TRIGA_CNTL_OTG_TRIGA_POLARITY_SELECT_GENERICD = 0x00000007,
} OTG_TRIGA_CNTL_OTG_TRIGA_POLARITY_SELECT;

/*
 * OTG_TRIGA_CNTL_OTG_TRIGA_RESYNC_BYPASS_EN enum
 */

typedef enum OTG_TRIGA_CNTL_OTG_TRIGA_RESYNC_BYPASS_EN {
OTG_TRIGA_CNTL_OTG_TRIGA_RESYNC_BYPASS_EN_FALSE = 0x00000000,
OTG_TRIGA_CNTL_OTG_TRIGA_RESYNC_BYPASS_EN_TRUE = 0x00000001,
} OTG_TRIGA_CNTL_OTG_TRIGA_RESYNC_BYPASS_EN;

/*
 * OTG_TRIGA_CNTL_OTG_TRIGA_SOURCE_PIPE_SELECT enum
 */

typedef enum OTG_TRIGA_CNTL_OTG_TRIGA_SOURCE_PIPE_SELECT {
OTG_TRIGA_CNTL_OTG_TRIGA_SOURCE_PIPE_SELECT_OTG0 = 0x00000000,
OTG_TRIGA_CNTL_OTG_TRIGA_SOURCE_PIPE_SELECT_OTG1 = 0x00000001,
OTG_TRIGA_CNTL_OTG_TRIGA_SOURCE_PIPE_SELECT_OTG2 = 0x00000002,
OTG_TRIGA_CNTL_OTG_TRIGA_SOURCE_PIPE_SELECT_OTG3 = 0x00000003,
OTG_TRIGA_CNTL_OTG_TRIGA_SOURCE_PIPE_SELECT_RESERVED4 = 0x00000004,
OTG_TRIGA_CNTL_OTG_TRIGA_SOURCE_PIPE_SELECT_RESERVED5 = 0x00000005,
} OTG_TRIGA_CNTL_OTG_TRIGA_SOURCE_PIPE_SELECT;

/*
 * OTG_TRIGA_CNTL_OTG_TRIGA_SOURCE_SELECT enum
 */

typedef enum OTG_TRIGA_CNTL_OTG_TRIGA_SOURCE_SELECT {
OTG_TRIGA_CNTL_OTG_TRIGA_SOURCE_SELECT_LOGIC0 = 0x00000000,
OTG_TRIGA_CNTL_OTG_TRIGA_SOURCE_SELECT_GENERICA_PIN = 0x00000001,
OTG_TRIGA_CNTL_OTG_TRIGA_SOURCE_SELECT_GENERICB_PIN = 0x00000002,
OTG_TRIGA_CNTL_OTG_TRIGA_SOURCE_SELECT_GENERICC_PIN = 0x00000003,
OTG_TRIGA_CNTL_OTG_TRIGA_SOURCE_SELECT_GENERICD_PIN = 0x00000004,
OTG_TRIGA_CNTL_OTG_TRIGA_SOURCE_SELECT_GENERICE_PIN = 0x00000005,
OTG_TRIGA_CNTL_OTG_TRIGA_SOURCE_SELECT_GENERICF_PIN = 0x00000006,
OTG_TRIGA_CNTL_OTG_TRIGA_SOURCE_SELECT_SWAPLOCKA_PIN = 0x00000007,
OTG_TRIGA_CNTL_OTG_TRIGA_SOURCE_SELECT_SWAPLOCKB_PIN = 0x00000008,
OTG_TRIGA_CNTL_OTG_TRIGA_SOURCE_SELECT_GENLK_CLK_PIN = 0x00000009,
OTG_TRIGA_CNTL_OTG_TRIGA_SOURCE_SELECT_GENLK_VSYNC_PIN = 0x0000000a,
OTG_TRIGA_CNTL_OTG_TRIGA_SOURCE_SELECT_HPD1 = 0x0000000b,
OTG_TRIGA_CNTL_OTG_TRIGA_SOURCE_SELECT_HPD2 = 0x0000000c,
OTG_TRIGA_CNTL_OTG_TRIGA_SOURCE_SELECT_BLON_Y_PIN = 0x0000000d,
OTG_TRIGA_CNTL_OTG_TRIGA_SOURCE_SELECT_RESERVED14 = 0x0000000e,
OTG_TRIGA_CNTL_OTG_TRIGA_SOURCE_SELECT_UPDATE_LOCK = 0x0000000f,
OTG_TRIGA_CNTL_OTG_TRIGA_SOURCE_SELECT_GSL_ALLOW_FLIP = 0x00000010,
OTG_TRIGA_CNTL_OTG_TRIGA_SOURCE_SELECT_UPDATE_PENDING = 0x00000011,
OTG_TRIGA_CNTL_OTG_TRIGA_SOURCE_SELECT_OTG_SOF = 0x00000012,
OTG_TRIGA_CNTL_OTG_TRIGA_SOURCE_SELECT_HSYNC = 0x00000013,
OTG_TRIGA_CNTL_OTG_TRIGA_SOURCE_SELECT_VSYNC = 0x00000014,
OTG_TRIGA_CNTL_OTG_TRIGA_SOURCE_SELECT_OTG_TRIG_MANUAL_CONTROL = 0x00000015,
OTG_TRIGA_CNTL_OTG_TRIGA_SOURCE_SELECT_MANUAL_FLOW_CONTROL = 0x00000016,
OTG_TRIGA_CNTL_OTG_TRIGA_SOURCE_SELECT_LOGIC1 = 0x00000017,
OTG_TRIGA_CNTL_OTG_TRIGA_SOURCE_SELECT_FLIP_PENDING = 0x00000018,
} OTG_TRIGA_CNTL_OTG_TRIGA_SOURCE_SELECT;

/*
 * OTG_TRIGA_FALLING_EDGE_DETECT_CNTL enum
 */

typedef enum OTG_TRIGA_FALLING_EDGE_DETECT_CNTL {
OTG_TRIGA_FALLING_EDGE_DETECT_CNTL_0     = 0x00000000,
OTG_TRIGA_FALLING_EDGE_DETECT_CNTL_1     = 0x00000001,
OTG_TRIGA_FALLING_EDGE_DETECT_CNTL_2     = 0x00000002,
OTG_TRIGA_FALLING_EDGE_DETECT_CNTL_3     = 0x00000003,
} OTG_TRIGA_FALLING_EDGE_DETECT_CNTL;

/*
 * OTG_TRIGA_FREQUENCY_SELECT enum
 */

typedef enum OTG_TRIGA_FREQUENCY_SELECT {
OTG_TRIGA_FREQUENCY_SELECT_0             = 0x00000000,
OTG_TRIGA_FREQUENCY_SELECT_1             = 0x00000001,
OTG_TRIGA_FREQUENCY_SELECT_2             = 0x00000002,
OTG_TRIGA_FREQUENCY_SELECT_3             = 0x00000003,
} OTG_TRIGA_FREQUENCY_SELECT;

/*
 * OTG_TRIGA_RISING_EDGE_DETECT_CNTL enum
 */

typedef enum OTG_TRIGA_RISING_EDGE_DETECT_CNTL {
OTG_TRIGA_RISING_EDGE_DETECT_CNTL_0      = 0x00000000,
OTG_TRIGA_RISING_EDGE_DETECT_CNTL_1      = 0x00000001,
OTG_TRIGA_RISING_EDGE_DETECT_CNTL_2      = 0x00000002,
OTG_TRIGA_RISING_EDGE_DETECT_CNTL_3      = 0x00000003,
} OTG_TRIGA_RISING_EDGE_DETECT_CNTL;

/*
 * OTG_TRIGB_CNTL_OTG_TRIGB_CLEAR enum
 */

typedef enum OTG_TRIGB_CNTL_OTG_TRIGB_CLEAR {
OTG_TRIGB_CNTL_OTG_TRIGB_CLEAR_FALSE     = 0x00000000,
OTG_TRIGB_CNTL_OTG_TRIGB_CLEAR_TRUE      = 0x00000001,
} OTG_TRIGB_CNTL_OTG_TRIGB_CLEAR;

/*
 * OTG_TRIGB_CNTL_OTG_TRIGB_POLARITY_SELECT enum
 */

typedef enum OTG_TRIGB_CNTL_OTG_TRIGB_POLARITY_SELECT {
OTG_TRIGB_CNTL_OTG_TRIGB_POLARITY_SELECT_LOGIC0 = 0x00000000,
OTG_TRIGB_CNTL_OTG_TRIGB_POLARITY_SELECT_INTERLACE = 0x00000001,
OTG_TRIGB_CNTL_OTG_TRIGB_POLARITY_SELECT_GENERICA = 0x00000002,
OTG_TRIGB_CNTL_OTG_TRIGB_POLARITY_SELECT_GENERICB = 0x00000003,
OTG_TRIGB_CNTL_OTG_TRIGB_POLARITY_SELECT_HSYNCA = 0x00000004,
OTG_TRIGB_CNTL_OTG_TRIGB_POLARITY_SELECT_LOGIC1 = 0x00000005,
OTG_TRIGB_CNTL_OTG_TRIGB_POLARITY_SELECT_GENERICC = 0x00000006,
OTG_TRIGB_CNTL_OTG_TRIGB_POLARITY_SELECT_GENERICD = 0x00000007,
} OTG_TRIGB_CNTL_OTG_TRIGB_POLARITY_SELECT;

/*
 * OTG_TRIGB_CNTL_OTG_TRIGB_RESYNC_BYPASS_EN enum
 */

typedef enum OTG_TRIGB_CNTL_OTG_TRIGB_RESYNC_BYPASS_EN {
OTG_TRIGB_CNTL_OTG_TRIGB_RESYNC_BYPASS_EN_FALSE = 0x00000000,
OTG_TRIGB_CNTL_OTG_TRIGB_RESYNC_BYPASS_EN_TRUE = 0x00000001,
} OTG_TRIGB_CNTL_OTG_TRIGB_RESYNC_BYPASS_EN;

/*
 * OTG_TRIGB_CNTL_OTG_TRIGB_SOURCE_PIPE_SELECT enum
 */

typedef enum OTG_TRIGB_CNTL_OTG_TRIGB_SOURCE_PIPE_SELECT {
OTG_TRIGB_CNTL_OTG_TRIGB_SOURCE_PIPE_SELECT_OTG0 = 0x00000000,
OTG_TRIGB_CNTL_OTG_TRIGB_SOURCE_PIPE_SELECT_OTG1 = 0x00000001,
OTG_TRIGB_CNTL_OTG_TRIGB_SOURCE_PIPE_SELECT_OTG2 = 0x00000002,
OTG_TRIGB_CNTL_OTG_TRIGB_SOURCE_PIPE_SELECT_OTG3 = 0x00000003,
OTG_TRIGB_CNTL_OTG_TRIGB_SOURCE_PIPE_SELECT_RESERVED4 = 0x00000004,
OTG_TRIGB_CNTL_OTG_TRIGB_SOURCE_PIPE_SELECT_RESERVED5 = 0x00000005,
} OTG_TRIGB_CNTL_OTG_TRIGB_SOURCE_PIPE_SELECT;

/*
 * OTG_TRIGB_CNTL_OTG_TRIGB_SOURCE_SELECT enum
 */

typedef enum OTG_TRIGB_CNTL_OTG_TRIGB_SOURCE_SELECT {
OTG_TRIGB_CNTL_OTG_TRIGB_SOURCE_SELECT_LOGIC0 = 0x00000000,
OTG_TRIGB_CNTL_OTG_TRIGB_SOURCE_SELECT_GENERICA_PIN = 0x00000001,
OTG_TRIGB_CNTL_OTG_TRIGB_SOURCE_SELECT_GENERICB_PIN = 0x00000002,
OTG_TRIGB_CNTL_OTG_TRIGB_SOURCE_SELECT_GENERICC_PIN = 0x00000003,
OTG_TRIGB_CNTL_OTG_TRIGB_SOURCE_SELECT_GENERICD_PIN = 0x00000004,
OTG_TRIGB_CNTL_OTG_TRIGB_SOURCE_SELECT_GENERICE_PIN = 0x00000005,
OTG_TRIGB_CNTL_OTG_TRIGB_SOURCE_SELECT_GENERICF_PIN = 0x00000006,
OTG_TRIGB_CNTL_OTG_TRIGB_SOURCE_SELECT_SWAPLOCKA_PIN = 0x00000007,
OTG_TRIGB_CNTL_OTG_TRIGB_SOURCE_SELECT_SWAPLOCKB_PIN = 0x00000008,
OTG_TRIGB_CNTL_OTG_TRIGB_SOURCE_SELECT_GENLK_CLK_PIN = 0x00000009,
OTG_TRIGB_CNTL_OTG_TRIGB_SOURCE_SELECT_GENLK_VSYNC_PIN = 0x0000000a,
OTG_TRIGB_CNTL_OTG_TRIGB_SOURCE_SELECT_HPD1 = 0x0000000b,
OTG_TRIGB_CNTL_OTG_TRIGB_SOURCE_SELECT_HPD2 = 0x0000000c,
OTG_TRIGB_CNTL_OTG_TRIGB_SOURCE_SELECT_BLON_Y_PIN = 0x0000000d,
OTG_TRIGB_CNTL_OTG_TRIGB_SOURCE_SELECT_RESERVED14 = 0x0000000e,
OTG_TRIGB_CNTL_OTG_TRIGB_SOURCE_SELECT_UPDATE_LOCK = 0x0000000f,
OTG_TRIGB_CNTL_OTG_TRIGB_SOURCE_SELECT_GSL_ALLOW_FLIP = 0x00000010,
OTG_TRIGB_CNTL_OTG_TRIGB_SOURCE_SELECT_UPDATE_PENDING = 0x00000011,
OTG_TRIGB_CNTL_OTG_TRIGB_SOURCE_SELECT_OTG_SOF = 0x00000012,
OTG_TRIGB_CNTL_OTG_TRIGB_SOURCE_SELECT_HSYNC = 0x00000013,
OTG_TRIGB_CNTL_OTG_TRIGB_SOURCE_SELECT_VSYNC = 0x00000014,
OTG_TRIGB_CNTL_OTG_TRIGB_SOURCE_SELECT_OTG_TRIG_MANUAL_CONTROL = 0x00000015,
OTG_TRIGB_CNTL_OTG_TRIGB_SOURCE_SELECT_MANUAL_FLOW_CONTROL = 0x00000016,
OTG_TRIGB_CNTL_OTG_TRIGB_SOURCE_SELECT_LOGIC1 = 0x00000017,
OTG_TRIGB_CNTL_OTG_TRIGB_SOURCE_SELECT_FLIP_PENDING = 0x00000018,
} OTG_TRIGB_CNTL_OTG_TRIGB_SOURCE_SELECT;

/*
 * OTG_TRIGB_FALLING_EDGE_DETECT_CNTL enum
 */

typedef enum OTG_TRIGB_FALLING_EDGE_DETECT_CNTL {
OTG_TRIGB_FALLING_EDGE_DETECT_CNTL_0     = 0x00000000,
OTG_TRIGB_FALLING_EDGE_DETECT_CNTL_1     = 0x00000001,
OTG_TRIGB_FALLING_EDGE_DETECT_CNTL_2     = 0x00000002,
OTG_TRIGB_FALLING_EDGE_DETECT_CNTL_3     = 0x00000003,
} OTG_TRIGB_FALLING_EDGE_DETECT_CNTL;

/*
 * OTG_TRIGB_FREQUENCY_SELECT enum
 */

typedef enum OTG_TRIGB_FREQUENCY_SELECT {
OTG_TRIGB_FREQUENCY_SELECT_0             = 0x00000000,
OTG_TRIGB_FREQUENCY_SELECT_1             = 0x00000001,
OTG_TRIGB_FREQUENCY_SELECT_2             = 0x00000002,
OTG_TRIGB_FREQUENCY_SELECT_3             = 0x00000003,
} OTG_TRIGB_FREQUENCY_SELECT;

/*
 * OTG_TRIGB_RISING_EDGE_DETECT_CNTL enum
 */

typedef enum OTG_TRIGB_RISING_EDGE_DETECT_CNTL {
OTG_TRIGB_RISING_EDGE_DETECT_CNTL_0      = 0x00000000,
OTG_TRIGB_RISING_EDGE_DETECT_CNTL_1      = 0x00000001,
OTG_TRIGB_RISING_EDGE_DETECT_CNTL_2      = 0x00000002,
OTG_TRIGB_RISING_EDGE_DETECT_CNTL_3      = 0x00000003,
} OTG_TRIGB_RISING_EDGE_DETECT_CNTL;

/*
 * OTG_UPDATE_LOCK_OTG_UPDATE_LOCK enum
 */

typedef enum OTG_UPDATE_LOCK_OTG_UPDATE_LOCK {
OTG_UPDATE_LOCK_OTG_UPDATE_LOCK_FALSE    = 0x00000000,
OTG_UPDATE_LOCK_OTG_UPDATE_LOCK_TRUE     = 0x00000001,
} OTG_UPDATE_LOCK_OTG_UPDATE_LOCK;

/*
 * OTG_VERTICAL_INTERRUPT0_CONTROL_OTG_VERTICAL_INTERRUPT0_CLEAR enum
 */

typedef enum OTG_VERTICAL_INTERRUPT0_CONTROL_OTG_VERTICAL_INTERRUPT0_CLEAR {
OTG_VERTICAL_INTERRUPT0_CONTROL_OTG_VERTICAL_INTERRUPT0_CLEAR_FALSE = 0x00000000,
OTG_VERTICAL_INTERRUPT0_CONTROL_OTG_VERTICAL_INTERRUPT0_CLEAR_TRUE = 0x00000001,
} OTG_VERTICAL_INTERRUPT0_CONTROL_OTG_VERTICAL_INTERRUPT0_CLEAR;

/*
 * OTG_VERTICAL_INTERRUPT0_CONTROL_OTG_VERTICAL_INTERRUPT0_INT_ENABLE enum
 */

typedef enum OTG_VERTICAL_INTERRUPT0_CONTROL_OTG_VERTICAL_INTERRUPT0_INT_ENABLE {
OTG_VERTICAL_INTERRUPT0_CONTROL_OTG_VERTICAL_INTERRUPT0_INT_ENABLE_FALSE = 0x00000000,
OTG_VERTICAL_INTERRUPT0_CONTROL_OTG_VERTICAL_INTERRUPT0_INT_ENABLE_TRUE = 0x00000001,
} OTG_VERTICAL_INTERRUPT0_CONTROL_OTG_VERTICAL_INTERRUPT0_INT_ENABLE;

/*
 * OTG_VERTICAL_INTERRUPT0_CONTROL_OTG_VERTICAL_INTERRUPT0_INT_TYPE enum
 */

typedef enum OTG_VERTICAL_INTERRUPT0_CONTROL_OTG_VERTICAL_INTERRUPT0_INT_TYPE {
OTG_VERTICAL_INTERRUPT0_CONTROL_OTG_VERTICAL_INTERRUPT0_INT_TYPE_FALSE = 0x00000000,
OTG_VERTICAL_INTERRUPT0_CONTROL_OTG_VERTICAL_INTERRUPT0_INT_TYPE_TRUE = 0x00000001,
} OTG_VERTICAL_INTERRUPT0_CONTROL_OTG_VERTICAL_INTERRUPT0_INT_TYPE;

/*
 * OTG_VERTICAL_INTERRUPT0_CONTROL_OTG_VERTICAL_INTERRUPT0_OUTPUT_POLARITY enum
 */

typedef enum OTG_VERTICAL_INTERRUPT0_CONTROL_OTG_VERTICAL_INTERRUPT0_OUTPUT_POLARITY {
OTG_VERTICAL_INTERRUPT0_CONTROL_OTG_VERTICAL_INTERRUPT0_OUTPUT_POLARITY_FALSE = 0x00000000,
OTG_VERTICAL_INTERRUPT0_CONTROL_OTG_VERTICAL_INTERRUPT0_OUTPUT_POLARITY_TRUE = 0x00000001,
} OTG_VERTICAL_INTERRUPT0_CONTROL_OTG_VERTICAL_INTERRUPT0_OUTPUT_POLARITY;

/*
 * OTG_VERTICAL_INTERRUPT1_CONTROL_OTG_VERTICAL_INTERRUPT1_CLEAR enum
 */

typedef enum OTG_VERTICAL_INTERRUPT1_CONTROL_OTG_VERTICAL_INTERRUPT1_CLEAR {
OTG_VERTICAL_INTERRUPT1_CONTROL_OTG_VERTICAL_INTERRUPT1_CLEAR_CLEAR_FALSE = 0x00000000,
OTG_VERTICAL_INTERRUPT1_CONTROL_OTG_VERTICAL_INTERRUPT1_CLEAR_TRUE = 0x00000001,
} OTG_VERTICAL_INTERRUPT1_CONTROL_OTG_VERTICAL_INTERRUPT1_CLEAR;

/*
 * OTG_VERTICAL_INTERRUPT1_CONTROL_OTG_VERTICAL_INTERRUPT1_INT_ENABLE enum
 */

typedef enum OTG_VERTICAL_INTERRUPT1_CONTROL_OTG_VERTICAL_INTERRUPT1_INT_ENABLE {
OTG_VERTICAL_INTERRUPT1_CONTROL_OTG_VERTICAL_INTERRUPT1_INT_ENABLE_FALSE = 0x00000000,
OTG_VERTICAL_INTERRUPT1_CONTROL_OTG_VERTICAL_INTERRUPT1_INT_ENABLE_TRUE = 0x00000001,
} OTG_VERTICAL_INTERRUPT1_CONTROL_OTG_VERTICAL_INTERRUPT1_INT_ENABLE;

/*
 * OTG_VERTICAL_INTERRUPT1_CONTROL_OTG_VERTICAL_INTERRUPT1_INT_TYPE enum
 */

typedef enum OTG_VERTICAL_INTERRUPT1_CONTROL_OTG_VERTICAL_INTERRUPT1_INT_TYPE {
OTG_VERTICAL_INTERRUPT1_CONTROL_OTG_VERTICAL_INTERRUPT1_INT_TYPE_FALSE = 0x00000000,
OTG_VERTICAL_INTERRUPT1_CONTROL_OTG_VERTICAL_INTERRUPT1_INT_TYPE_TRUE = 0x00000001,
} OTG_VERTICAL_INTERRUPT1_CONTROL_OTG_VERTICAL_INTERRUPT1_INT_TYPE;

/*
 * OTG_VERTICAL_INTERRUPT2_CONTROL_OTG_VERTICAL_INTERRUPT2_CLEAR enum
 */

typedef enum OTG_VERTICAL_INTERRUPT2_CONTROL_OTG_VERTICAL_INTERRUPT2_CLEAR {
OTG_VERTICAL_INTERRUPT2_CONTROL_OTG_VERTICAL_INTERRUPT2_CLEAR_CLEAR_FALSE = 0x00000000,
OTG_VERTICAL_INTERRUPT2_CONTROL_OTG_VERTICAL_INTERRUPT2_CLEAR_TRUE = 0x00000001,
} OTG_VERTICAL_INTERRUPT2_CONTROL_OTG_VERTICAL_INTERRUPT2_CLEAR;

/*
 * OTG_VERTICAL_INTERRUPT2_CONTROL_OTG_VERTICAL_INTERRUPT2_INT_ENABLE enum
 */

typedef enum OTG_VERTICAL_INTERRUPT2_CONTROL_OTG_VERTICAL_INTERRUPT2_INT_ENABLE {
OTG_VERTICAL_INTERRUPT2_CONTROL_OTG_VERTICAL_INTERRUPT2_INT_ENABLE_FALSE = 0x00000000,
OTG_VERTICAL_INTERRUPT2_CONTROL_OTG_VERTICAL_INTERRUPT2_INT_ENABLE_TRUE = 0x00000001,
} OTG_VERTICAL_INTERRUPT2_CONTROL_OTG_VERTICAL_INTERRUPT2_INT_ENABLE;

/*
 * OTG_VERTICAL_INTERRUPT2_CONTROL_OTG_VERTICAL_INTERRUPT2_INT_TYPE enum
 */

typedef enum OTG_VERTICAL_INTERRUPT2_CONTROL_OTG_VERTICAL_INTERRUPT2_INT_TYPE {
OTG_VERTICAL_INTERRUPT2_CONTROL_OTG_VERTICAL_INTERRUPT2_INT_TYPE_FALSE = 0x00000000,
OTG_VERTICAL_INTERRUPT2_CONTROL_OTG_VERTICAL_INTERRUPT2_INT_TYPE_TRUE = 0x00000001,
} OTG_VERTICAL_INTERRUPT2_CONTROL_OTG_VERTICAL_INTERRUPT2_INT_TYPE;

/*
 * OTG_VERT_SYNC_CONTROL_OTG_AUTO_FORCE_VSYNC_MODE enum
 */

typedef enum OTG_VERT_SYNC_CONTROL_OTG_AUTO_FORCE_VSYNC_MODE {
OTG_VERT_SYNC_CONTROL_OTG_AUTO_FORCE_VSYNC_MODE_DISABLE = 0x00000000,
OTG_VERT_SYNC_CONTROL_OTG_AUTO_FORCE_VSYNC_MODE_TRIGGERA = 0x00000001,
OTG_VERT_SYNC_CONTROL_OTG_AUTO_FORCE_VSYNC_MODE_TRIGGERB = 0x00000002,
OTG_VERT_SYNC_CONTROL_OTG_AUTO_FORCE_VSYNC_MODE_RESERVED = 0x00000003,
} OTG_VERT_SYNC_CONTROL_OTG_AUTO_FORCE_VSYNC_MODE;

/*
 * OTG_VERT_SYNC_CONTROL_OTG_FORCE_VSYNC_NEXT_LINE_CLEAR enum
 */

typedef enum OTG_VERT_SYNC_CONTROL_OTG_FORCE_VSYNC_NEXT_LINE_CLEAR {
OTG_VERT_SYNC_CONTROL_OTG_FORCE_VSYNC_NEXT_LINE_CLEAR_FALSE = 0x00000000,
OTG_VERT_SYNC_CONTROL_OTG_FORCE_VSYNC_NEXT_LINE_CLEAR_TRUE = 0x00000001,
} OTG_VERT_SYNC_CONTROL_OTG_FORCE_VSYNC_NEXT_LINE_CLEAR;

/*
 * OTG_VSYNC_NOM_INT_STATUS_OTG_VSYNC_NOM_INT_CLEAR enum
 */

typedef enum OTG_VSYNC_NOM_INT_STATUS_OTG_VSYNC_NOM_INT_CLEAR {
OTG_VSYNC_NOM_INT_STATUS_OTG_VSYNC_NOM_INT_CLEAR_FALSE = 0x00000000,
OTG_VSYNC_NOM_INT_STATUS_OTG_VSYNC_NOM_INT_CLEAR_TRUE = 0x00000001,
} OTG_VSYNC_NOM_INT_STATUS_OTG_VSYNC_NOM_INT_CLEAR;

/*
 * OTG_VUPDATE_BLOCK_DISABLE enum
 */

typedef enum OTG_VUPDATE_BLOCK_DISABLE {
OTG_VUPDATE_BLOCK_DISABLE_OFF            = 0x00000000,
OTG_VUPDATE_BLOCK_DISABLE_ON             = 0x00000001,
} OTG_VUPDATE_BLOCK_DISABLE;

/*
 * OTG_V_SYNC_A_POL enum
 */

typedef enum OTG_V_SYNC_A_POL {
OTG_V_SYNC_A_POL_HIGH                    = 0x00000000,
OTG_V_SYNC_A_POL_LOW                     = 0x00000001,
} OTG_V_SYNC_A_POL;

/*
 * OTG_V_SYNC_MODE enum
 */

typedef enum OTG_V_SYNC_MODE {
OTG_V_SYNC_MODE_HSYNC                    = 0x00000000,
OTG_V_SYNC_MODE_HBLANK                   = 0x00000001,
} OTG_V_SYNC_MODE;

/*
 * OTG_V_TOTAL_CONTROL_OTG_DRR_EVENT_ACTIVE_PERIOD enum
 */

typedef enum OTG_V_TOTAL_CONTROL_OTG_DRR_EVENT_ACTIVE_PERIOD {
OTG_V_TOTAL_CONTROL_OTG_DRR_EVENT_ACTIVE_PERIOD_0 = 0x00000000,
OTG_V_TOTAL_CONTROL_OTG_DRR_EVENT_ACTIVE_PERIOD_1 = 0x00000001,
} OTG_V_TOTAL_CONTROL_OTG_DRR_EVENT_ACTIVE_PERIOD;

/*
 * OTG_V_TOTAL_CONTROL_OTG_FORCE_LOCK_ON_EVENT enum
 */

typedef enum OTG_V_TOTAL_CONTROL_OTG_FORCE_LOCK_ON_EVENT {
OTG_V_TOTAL_CONTROL_OTG_FORCE_LOCK_ON_EVENT_DISABLE = 0x00000000,
OTG_V_TOTAL_CONTROL_OTG_FORCE_LOCK_ON_EVENT_ENABLE = 0x00000001,
} OTG_V_TOTAL_CONTROL_OTG_FORCE_LOCK_ON_EVENT;

/*
 * OTG_V_TOTAL_CONTROL_OTG_FORCE_LOCK_TO_MASTER_VSYNC enum
 */

typedef enum OTG_V_TOTAL_CONTROL_OTG_FORCE_LOCK_TO_MASTER_VSYNC {
OTG_V_TOTAL_CONTROL_OTG_FORCE_LOCK_TO_MASTER_VSYNC_DISABLE = 0x00000000,
OTG_V_TOTAL_CONTROL_OTG_FORCE_LOCK_TO_MASTER_VSYNC_ENABLE = 0x00000001,
} OTG_V_TOTAL_CONTROL_OTG_FORCE_LOCK_TO_MASTER_VSYNC;

/*
 * OTG_V_TOTAL_CONTROL_OTG_V_TOTAL_MAX_SEL enum
 */

typedef enum OTG_V_TOTAL_CONTROL_OTG_V_TOTAL_MAX_SEL {
OTG_V_TOTAL_CONTROL_OTG_V_TOTAL_MAX_SEL_FALSE = 0x00000000,
OTG_V_TOTAL_CONTROL_OTG_V_TOTAL_MAX_SEL_TRUE = 0x00000001,
} OTG_V_TOTAL_CONTROL_OTG_V_TOTAL_MAX_SEL;

/*
 * OTG_V_TOTAL_CONTROL_OTG_V_TOTAL_MIN_SEL enum
 */

typedef enum OTG_V_TOTAL_CONTROL_OTG_V_TOTAL_MIN_SEL {
OTG_V_TOTAL_CONTROL_OTG_V_TOTAL_MIN_SEL_FALSE = 0x00000000,
OTG_V_TOTAL_CONTROL_OTG_V_TOTAL_MIN_SEL_TRUE = 0x00000001,
} OTG_V_TOTAL_CONTROL_OTG_V_TOTAL_MIN_SEL;

/*
 * OTG_V_TOTAL_INT_STATUS_OTG_SET_V_TOTAL_MIN_EVENT_OCCURRED_ACK enum
 */

typedef enum OTG_V_TOTAL_INT_STATUS_OTG_SET_V_TOTAL_MIN_EVENT_OCCURRED_ACK {
OTG_V_TOTAL_INT_STATUS_OTG_SET_V_TOTAL_MIN_EVENT_OCCURRED_ACK_FALSE = 0x00000000,
OTG_V_TOTAL_INT_STATUS_OTG_SET_V_TOTAL_MIN_EVENT_OCCURRED_ACK_TRUE = 0x00000001,
} OTG_V_TOTAL_INT_STATUS_OTG_SET_V_TOTAL_MIN_EVENT_OCCURRED_ACK;

/*******************************************************
 * OPTC_MISC Enums
 *******************************************************/

/*
 * OPTC_GSL_SOURCE_SELECT_GSL_TIMING_SYNC_SEL enum
 */

typedef enum OPTC_GSL_SOURCE_SELECT_GSL_TIMING_SYNC_SEL {
OPTC_GSL_SOURCE_SELECT_GSL_TIMING_SYNC_SEL_OTG0 = 0x00000000,
OPTC_GSL_SOURCE_SELECT_GSL_TIMING_SYNC_SEL_OTG1 = 0x00000001,
OPTC_GSL_SOURCE_SELECT_GSL_TIMING_SYNC_SEL_OTG2 = 0x00000002,
OPTC_GSL_SOURCE_SELECT_GSL_TIMING_SYNC_SEL_OTG3 = 0x00000003,
OPTC_GSL_SOURCE_SELECT_GSL_TIMING_SYNC_SEL_RESERVED4 = 0x00000004,
OPTC_GSL_SOURCE_SELECT_GSL_TIMING_SYNC_SEL_RESERVED5 = 0x00000005,
} OPTC_GSL_SOURCE_SELECT_GSL_TIMING_SYNC_SEL;

/*******************************************************
 * DMCUB Enums
 *******************************************************/

/*
 * DC_DMCUB_INT_TYPE enum
 */

typedef enum DC_DMCUB_INT_TYPE {
INT_LEVEL                                = 0x00000000,
INT_PULSE                                = 0x00000001,
} DC_DMCUB_INT_TYPE;

/*
 * DC_DMCUB_TIMER_WINDOW enum
 */

typedef enum DC_DMCUB_TIMER_WINDOW {
BITS_31_0                                = 0x00000000,
BITS_32_1                                = 0x00000001,
BITS_33_2                                = 0x00000002,
BITS_34_3                                = 0x00000003,
BITS_35_4                                = 0x00000004,
BITS_36_5                                = 0x00000005,
BITS_37_6                                = 0x00000006,
BITS_38_7                                = 0x00000007,
} DC_DMCUB_TIMER_WINDOW;

/*******************************************************
 * RBBMIF Enums
 *******************************************************/

/*
 * INVALID_REG_ACCESS_TYPE enum
 */

typedef enum INVALID_REG_ACCESS_TYPE {
REG_UNALLOCATED_ADDR_WRITE               = 0x00000000,
REG_UNALLOCATED_ADDR_READ                = 0x00000001,
REG_VIRTUAL_WRITE                        = 0x00000002,
REG_VIRTUAL_READ                         = 0x00000003,
REG_SECURE_VIOLATE_WRITE                 = 0x00000004,
REG_SECURE_VIOLATE_READ                  = 0x00000005,
} INVALID_REG_ACCESS_TYPE;

/*******************************************************
 * IHC Enums
 *******************************************************/

/*
 * DMU_DC_GPU_TIMER_READ_SELECT enum
 */

typedef enum DMU_DC_GPU_TIMER_READ_SELECT {
DMU_GPU_TIMER_READ_SELECT_LOWER_D1_V_UPDATE_0 = 0x00000000,
DMU_GPU_TIMER_READ_SELECT_UPPER_D1_V_UPDATE_1 = 0x00000001,
DMU_GPU_TIMER_READ_SELECT_LOWER_D2_V_UPDATE_2 = 0x00000002,
DMU_GPU_TIMER_READ_SELECT_UPPER_D2_V_UPDATE_3 = 0x00000003,
DMU_GPU_TIMER_READ_SELECT_LOWER_D3_V_UPDATE_4 = 0x00000004,
DMU_GPU_TIMER_READ_SELECT_UPPER_D3_V_UPDATE_5 = 0x00000005,
DMU_GPU_TIMER_READ_SELECT_LOWER_D4_V_UPDATE_6 = 0x00000006,
DMU_GPU_TIMER_READ_SELECT_UPPER_D4_V_UPDATE_7 = 0x00000007,
RESERVED_8                               = 0x00000008,
RESERVED_9                               = 0x00000009,
RESERVED_10                              = 0x0000000a,
RESERVED_11                              = 0x0000000b,
DMU_GPU_TIMER_READ_SELECT_LOWER_D1_V_STARTUP_12 = 0x0000000c,
DMU_GPU_TIMER_READ_SELECT_UPPER_D1_V_STARTUP_13 = 0x0000000d,
DMU_GPU_TIMER_READ_SELECT_LOWER_D2_V_STARTUP_14 = 0x0000000e,
DMU_GPU_TIMER_READ_SELECT_UPPER_D2_V_STARTUP_15 = 0x0000000f,
DMU_GPU_TIMER_READ_SELECT_LOWER_D3_V_STARTUP_16 = 0x00000010,
DMU_GPU_TIMER_READ_SELECT_UPPER_D3_V_STARTUP_17 = 0x00000011,
DMU_GPU_TIMER_READ_SELECT_LOWER_D4_V_STARTUP_18 = 0x00000012,
DMU_GPU_TIMER_READ_SELECT_UPPER_D4_V_STARTUP_19 = 0x00000013,
RESERVED_20                              = 0x00000014,
RESERVED_21                              = 0x00000015,
RESERVED_22                              = 0x00000016,
RESERVED_23                              = 0x00000017,
DMU_GPU_TIMER_READ_SELECT_LOWER_D1_VSYNC_NOM_24 = 0x00000018,
DMU_GPU_TIMER_READ_SELECT_UPPER_D1_VSYNC_NOM_25 = 0x00000019,
DMU_GPU_TIMER_READ_SELECT_LOWER_D2_VSYNC_NOM_26 = 0x0000001a,
DMU_GPU_TIMER_READ_SELECT_UPPER_D2_VSYNC_NOM_27 = 0x0000001b,
DMU_GPU_TIMER_READ_SELECT_LOWER_D3_VSYNC_NOM_28 = 0x0000001c,
DMU_GPU_TIMER_READ_SELECT_UPPER_D3_VSYNC_NOM_29 = 0x0000001d,
DMU_GPU_TIMER_READ_SELECT_LOWER_D4_VSYNC_NOM_30 = 0x0000001e,
DMU_GPU_TIMER_READ_SELECT_UPPER_D4_VSYNC_NOM_31 = 0x0000001f,
RESERVED_32                              = 0x00000020,
RESERVED_33                              = 0x00000021,
RESERVED_34                              = 0x00000022,
RESERVED_35                              = 0x00000023,
DMU_GPU_TIMER_READ_SELECT_LOWER_D1_VREADY_36 = 0x00000024,
DMU_GPU_TIMER_READ_SELECT_UPPER_D1_VREADY_37 = 0x00000025,
DMU_GPU_TIMER_READ_SELECT_LOWER_D2_VREADY_38 = 0x00000026,
DMU_GPU_TIMER_READ_SELECT_UPPER_D2_VREADY_39 = 0x00000027,
DMU_GPU_TIMER_READ_SELECT_LOWER_D3_VREADY_40 = 0x00000028,
DMU_GPU_TIMER_READ_SELECT_UPPER_D3_VREADY_41 = 0x00000029,
DMU_GPU_TIMER_READ_SELECT_LOWER_D4_VREADY_42 = 0x0000002a,
DMU_GPU_TIMER_READ_SELECT_UPPER_D4_VREADY_43 = 0x0000002b,
RESERVED_44                              = 0x0000002c,
RESERVED_45                              = 0x0000002d,
RESERVED_46                              = 0x0000002e,
RESERVED_47                              = 0x0000002f,
DMU_GPU_TIMER_READ_SELECT_LOWER_D1_FLIP_48 = 0x00000030,
DMU_GPU_TIMER_READ_SELECT_UPPER_D1_FLIP_49 = 0x00000031,
DMU_GPU_TIMER_READ_SELECT_LOWER_D2_FLIP_50 = 0x00000032,
DMU_GPU_TIMER_READ_SELECT_UPPER_D2_FLIP_51 = 0x00000033,
DMU_GPU_TIMER_READ_SELECT_LOWER_D3_FLIP_52 = 0x00000034,
DMU_GPU_TIMER_READ_SELECT_UPPER_D3_FLIP_53 = 0x00000035,
DMU_GPU_TIMER_READ_SELECT_LOWER_D4_FLIP_54 = 0x00000036,
DMU_GPU_TIMER_READ_SELECT_UPPER_D4_FLIP_55 = 0x00000037,
RESERVED_56                              = 0x00000038,
RESERVED_57                              = 0x00000039,
RESERVED_58                              = 0x0000003a,
RESERVED_59                              = 0x0000003b,
RESERVED_60                              = 0x0000003c,
RESERVED_61                              = 0x0000003d,
RESERVED_62                              = 0x0000003e,
RESERVED_63                              = 0x0000003f,
DMU_GPU_TIMER_READ_SELECT_LOWER_D1_V_UPDATE_NO_LOCK_64 = 0x00000040,
DMU_GPU_TIMER_READ_SELECT_UPPER_D1_V_UPDATE_NO_LOCK_65 = 0x00000041,
DMU_GPU_TIMER_READ_SELECT_LOWER_D2_V_UPDATE_NO_LOCK_66 = 0x00000042,
DMU_GPU_TIMER_READ_SELECT_UPPER_D2_V_UPDATE_NO_LOCK_67 = 0x00000043,
DMU_GPU_TIMER_READ_SELECT_LOWER_D3_V_UPDATE_NO_LOCK_68 = 0x00000044,
DMU_GPU_TIMER_READ_SELECT_UPPER_D3_V_UPDATE_NO_LOCK_69 = 0x00000045,
DMU_GPU_TIMER_READ_SELECT_LOWER_D4_V_UPDATE_NO_LOCK_70 = 0x00000046,
DMU_GPU_TIMER_READ_SELECT_UPPER_D4_V_UPDATE_NO_LOCK_71 = 0x00000047,
RESERVED_72                              = 0x00000048,
RESERVED_73                              = 0x00000049,
RESERVED_74                              = 0x0000004a,
RESERVED_75                              = 0x0000004b,
DMU_GPU_TIMER_READ_SELECT_LOWER_D1_FLIP_AWAY_76 = 0x0000004c,
DMU_GPU_TIMER_READ_SELECT_UPPER_D1_FLIP_AWAY_77 = 0x0000004d,
DMU_GPU_TIMER_READ_SELECT_LOWER_D2_FLIP_AWAY_78 = 0x0000004e,
DMU_GPU_TIMER_READ_SELECT_UPPER_D2_FLIP_AWAY_79 = 0x0000004f,
DMU_GPU_TIMER_READ_SELECT_LOWER_D3_FLIP_AWAY_80 = 0x00000050,
DMU_GPU_TIMER_READ_SELECT_UPPER_D3_FLIP_AWAY_81 = 0x00000051,
DMU_GPU_TIMER_READ_SELECT_LOWER_D4_FLIP_AWAY_82 = 0x00000052,
DMU_GPU_TIMER_READ_SELECT_UPPER_D4_FLIP_AWAY_83 = 0x00000053,
RESERVED_84                              = 0x00000054,
RESERVED_85                              = 0x00000055,
RESERVED_86                              = 0x00000056,
RESERVED_87                              = 0x00000057,
RESERVED_88                              = 0x00000058,
RESERVED_89                              = 0x00000059,
RESERVED_90                              = 0x0000005a,
RESERVED_91                              = 0x0000005b,
} DMU_DC_GPU_TIMER_READ_SELECT;

/*
 * DMU_DC_GPU_TIMER_START_POSITION enum
 */

typedef enum DMU_DC_GPU_TIMER_START_POSITION {
DMU_GPU_TIMER_START_0_END_27             = 0x00000000,
DMU_GPU_TIMER_START_1_END_28             = 0x00000001,
DMU_GPU_TIMER_START_2_END_29             = 0x00000002,
DMU_GPU_TIMER_START_3_END_30             = 0x00000003,
DMU_GPU_TIMER_START_4_END_31             = 0x00000004,
DMU_GPU_TIMER_START_6_END_33             = 0x00000005,
DMU_GPU_TIMER_START_8_END_35             = 0x00000006,
DMU_GPU_TIMER_START_10_END_37            = 0x00000007,
} DMU_DC_GPU_TIMER_START_POSITION;

/*
 * IHC_INTERRUPT_DEST enum
 */

typedef enum IHC_INTERRUPT_DEST {
INTERRUPT_SENT_TO_IH                     = 0x00000000,
INTERRUPT_SENT_TO_DMCUB                  = 0x00000001,
} IHC_INTERRUPT_DEST;

/*
 * IHC_INTERRUPT_LINE_STATUS enum
 */

typedef enum IHC_INTERRUPT_LINE_STATUS {
INTERRUPT_LINE_NOT_ASSERTED              = 0x00000000,
INTERRUPT_LINE_ASSERTED                  = 0x00000001,
} IHC_INTERRUPT_LINE_STATUS;

/*******************************************************
 * DMU_MISC Enums
 *******************************************************/

/*
 * DC_SMU_INTERRUPT_ENABLE enum
 */

typedef enum DC_SMU_INTERRUPT_ENABLE {
DISABLE_THE_INTERRUPT                    = 0x00000000,
ENABLE_THE_INTERRUPT                     = 0x00000001,
} DC_SMU_INTERRUPT_ENABLE;

/*
 * DMU_CLOCK_ON enum
 */

typedef enum DMU_CLOCK_ON {
DMU_CLOCK_STATUS_ON                      = 0x00000000,
DMU_CLOCK_STATUS_OFF                     = 0x00000001,
} DMU_CLOCK_ON;

/*
 * SMU_INTR enum
 */

typedef enum SMU_INTR {
SMU_MSG_INTR_NOOP                        = 0x00000000,
SET_SMU_MSG_INTR                         = 0x00000001,
} SMU_INTR;

/*******************************************************
 * DCCG Enums
 *******************************************************/

/*
 * ALLOW_SR_ON_TRANS_REQ enum
 */

typedef enum ALLOW_SR_ON_TRANS_REQ {
ALLOW_SR_ON_TRANS_REQ_ENABLE             = 0x00000000,
ALLOW_SR_ON_TRANS_REQ_DISABLE            = 0x00000001,
} ALLOW_SR_ON_TRANS_REQ;

/*
 * AMCLOCK_ENABLE enum
 */

typedef enum AMCLOCK_ENABLE {
ENABLE_AMCLK0                            = 0x00000000,
ENABLE_AMCLK1                            = 0x00000001,
} AMCLOCK_ENABLE;

/*
 * CLEAR_SMU_INTR enum
 */

typedef enum CLEAR_SMU_INTR {
SMU_INTR_STATUS_NOOP                     = 0x00000000,
SMU_INTR_STATUS_CLEAR                    = 0x00000001,
} CLEAR_SMU_INTR;

/*
 * CLOCK_BRANCH_SOFT_RESET enum
 */

typedef enum CLOCK_BRANCH_SOFT_RESET {
CLOCK_BRANCH_SOFT_RESET_NOOP             = 0x00000000,
CLOCK_BRANCH_SOFT_RESET_FORCE            = 0x00000001,
} CLOCK_BRANCH_SOFT_RESET;

/*
 * DCCG_AUDIO_DTO0_SOURCE_SEL enum
 */

typedef enum DCCG_AUDIO_DTO0_SOURCE_SEL {
DCCG_AUDIO_DTO0_SOURCE_SEL_OTG0          = 0x00000000,
DCCG_AUDIO_DTO0_SOURCE_SEL_OTG1          = 0x00000001,
DCCG_AUDIO_DTO0_SOURCE_SEL_OTG2          = 0x00000002,
DCCG_AUDIO_DTO0_SOURCE_SEL_OTG3          = 0x00000003,
DCCG_AUDIO_DTO0_SOURCE_SEL_RESERVED      = 0x00000004,
} DCCG_AUDIO_DTO0_SOURCE_SEL;

/*
 * DCCG_AUDIO_DTO2_SOURCE_SEL enum
 */

typedef enum DCCG_AUDIO_DTO2_SOURCE_SEL {
DCCG_AUDIO_DTO2_SOURCE_SEL_AMCLK0        = 0x00000000,
DCCG_AUDIO_DTO2_SOURCE_SEL_AMCLK0_DIV2   = 0x00000001,
} DCCG_AUDIO_DTO2_SOURCE_SEL;

/*
 * DCCG_AUDIO_DTO_SEL enum
 */

typedef enum DCCG_AUDIO_DTO_SEL {
DCCG_AUDIO_DTO_SEL_AUDIO_DTO0            = 0x00000000,
DCCG_AUDIO_DTO_SEL_AUDIO_DTO1            = 0x00000001,
DCCG_AUDIO_DTO_SEL_NO_AUDIO_DTO          = 0x00000002,
DCCG_AUDIO_DTO_SEL_AUDIO_DTO_DTBCLK      = 0x00000003,
} DCCG_AUDIO_DTO_SEL;

/*
 * DCCG_AUDIO_DTO_USE_512FBR_DTO enum
 */

typedef enum DCCG_AUDIO_DTO_USE_512FBR_DTO {
DCCG_AUDIO_DTO_USE_128FBR_FOR_DP         = 0x00000000,
DCCG_AUDIO_DTO_USE_512FBR_FOR_DP         = 0x00000001,
} DCCG_AUDIO_DTO_USE_512FBR_DTO;

/*
 * DCCG_DBG_BLOCK_SEL enum
 */

typedef enum DCCG_DBG_BLOCK_SEL {
DCCG_DBG_BLOCK_SEL_DCCG                  = 0x00000000,
DCCG_DBG_BLOCK_SEL_PMON                  = 0x00000001,
DCCG_DBG_BLOCK_SEL_PMON2                 = 0x00000002,
} DCCG_DBG_BLOCK_SEL;

/*
 * DCCG_DBG_EN enum
 */

typedef enum DCCG_DBG_EN {
DCCG_DBG_EN_DISABLE                      = 0x00000000,
DCCG_DBG_EN_ENABLE                       = 0x00000001,
} DCCG_DBG_EN;

/*
 * DCCG_DEEP_COLOR_CNTL enum
 */

typedef enum DCCG_DEEP_COLOR_CNTL {
DCCG_DEEP_COLOR_DTO_DISABLE              = 0x00000000,
DCCG_DEEP_COLOR_DTO_5_4_RATIO            = 0x00000001,
DCCG_DEEP_COLOR_DTO_3_2_RATIO            = 0x00000002,
DCCG_DEEP_COLOR_DTO_2_1_RATIO            = 0x00000003,
} DCCG_DEEP_COLOR_CNTL;

/*
 * DCCG_FIFO_ERRDET_OVR_EN enum
 */

typedef enum DCCG_FIFO_ERRDET_OVR_EN {
DCCG_FIFO_ERRDET_OVR_DISABLE             = 0x00000000,
DCCG_FIFO_ERRDET_OVR_ENABLE              = 0x00000001,
} DCCG_FIFO_ERRDET_OVR_EN;

/*
 * DCCG_FIFO_ERRDET_RESET enum
 */

typedef enum DCCG_FIFO_ERRDET_RESET {
DCCG_FIFO_ERRDET_RESET_NOOP              = 0x00000000,
DCCG_FIFO_ERRDET_RESET_FORCE             = 0x00000001,
} DCCG_FIFO_ERRDET_RESET;

/*
 * DCCG_FIFO_ERRDET_STATE enum
 */

typedef enum DCCG_FIFO_ERRDET_STATE {
DCCG_FIFO_ERRDET_STATE_CALIBRATION       = 0x00000000,
DCCG_FIFO_ERRDET_STATE_DETECTION         = 0x00000001,
} DCCG_FIFO_ERRDET_STATE;

/*
 * DCCG_PERF_MODE_HSYNC enum
 */

typedef enum DCCG_PERF_MODE_HSYNC {
DCCG_PERF_MODE_HSYNC_NOOP                = 0x00000000,
DCCG_PERF_MODE_HSYNC_START               = 0x00000001,
} DCCG_PERF_MODE_HSYNC;

/*
 * DCCG_PERF_MODE_VSYNC enum
 */

typedef enum DCCG_PERF_MODE_VSYNC {
DCCG_PERF_MODE_VSYNC_NOOP                = 0x00000000,
DCCG_PERF_MODE_VSYNC_START               = 0x00000001,
} DCCG_PERF_MODE_VSYNC;

/*
 * DCCG_PERF_OTG_SELECT enum
 */

typedef enum DCCG_PERF_OTG_SELECT {
DCCG_PERF_SEL_OTG0                       = 0x00000000,
DCCG_PERF_SEL_OTG1                       = 0x00000001,
DCCG_PERF_SEL_OTG2                       = 0x00000002,
DCCG_PERF_SEL_OTG3                       = 0x00000003,
DCCG_PERF_SEL_RESERVED                   = 0x00000004,
} DCCG_PERF_OTG_SELECT;

/*
 * DCCG_PERF_RUN enum
 */

typedef enum DCCG_PERF_RUN {
DCCG_PERF_RUN_NOOP                       = 0x00000000,
DCCG_PERF_RUN_START                      = 0x00000001,
} DCCG_PERF_RUN;

/*
 * DC_MEM_GLOBAL_PWR_REQ_DIS enum
 */

typedef enum DC_MEM_GLOBAL_PWR_REQ_DIS {
DC_MEM_GLOBAL_PWR_REQ_ENABLE             = 0x00000000,
DC_MEM_GLOBAL_PWR_REQ_DISABLE            = 0x00000001,
} DC_MEM_GLOBAL_PWR_REQ_DIS;

/*
 * DIO_FIFO_ERROR enum
 */

typedef enum DIO_FIFO_ERROR {
DIO_FIFO_ERROR_00                        = 0x00000000,
DIO_FIFO_ERROR_01                        = 0x00000001,
DIO_FIFO_ERROR_10                        = 0x00000002,
DIO_FIFO_ERROR_11                        = 0x00000003,
} DIO_FIFO_ERROR;

/*
 * DISABLE_CLOCK_GATING enum
 */

typedef enum DISABLE_CLOCK_GATING {
CLOCK_GATING_ENABLED                     = 0x00000000,
CLOCK_GATING_DISABLED                    = 0x00000001,
} DISABLE_CLOCK_GATING;

/*
 * DISABLE_CLOCK_GATING_IN_DCO enum
 */

typedef enum DISABLE_CLOCK_GATING_IN_DCO {
CLOCK_GATING_ENABLED_IN_DCO              = 0x00000000,
CLOCK_GATING_DISABLED_IN_DCO             = 0x00000001,
} DISABLE_CLOCK_GATING_IN_DCO;

/*
 * DISPCLK_CHG_FWD_CORR_DISABLE enum
 */

typedef enum DISPCLK_CHG_FWD_CORR_DISABLE {
DISPCLK_CHG_FWD_CORR_ENABLE_AT_BEGINNING = 0x00000000,
DISPCLK_CHG_FWD_CORR_DISABLE_AT_BEGINNING = 0x00000001,
} DISPCLK_CHG_FWD_CORR_DISABLE;

/*
 * DISPCLK_FREQ_RAMP_DONE enum
 */

typedef enum DISPCLK_FREQ_RAMP_DONE {
DISPCLK_FREQ_RAMP_IN_PROGRESS            = 0x00000000,
DISPCLK_FREQ_RAMP_COMPLETED              = 0x00000001,
} DISPCLK_FREQ_RAMP_DONE;

/*
 * DPREFCLK_SRC_SEL enum
 */

typedef enum DPREFCLK_SRC_SEL {
DPREFCLK_SRC_SEL_CK                      = 0x00000000,
DPREFCLK_SRC_SEL_P0PLL                   = 0x00000001,
DPREFCLK_SRC_SEL_P1PLL                   = 0x00000002,
DPREFCLK_SRC_SEL_P2PLL                   = 0x00000003,
} DPREFCLK_SRC_SEL;

/*
 * DP_DTO_DS_DISABLE enum
 */

typedef enum DP_DTO_DS_DISABLE {
DP_DTO_DESPREAD_DISABLE                  = 0x00000000,
DP_DTO_DESPREAD_ENABLE                   = 0x00000001,
} DP_DTO_DS_DISABLE;

/*
 * DS_HW_CAL_ENABLE enum
 */

typedef enum DS_HW_CAL_ENABLE {
DS_HW_CAL_DIS                            = 0x00000000,
DS_HW_CAL_EN                             = 0x00000001,
} DS_HW_CAL_ENABLE;

/*
 * DS_JITTER_COUNT_SRC_SEL enum
 */

typedef enum DS_JITTER_COUNT_SRC_SEL {
DS_JITTER_COUNT_SRC_SEL0                 = 0x00000000,
DS_JITTER_COUNT_SRC_SEL1                 = 0x00000001,
} DS_JITTER_COUNT_SRC_SEL;

/*
 * DS_REF_SRC enum
 */

typedef enum DS_REF_SRC {
DS_REF_IS_XTALIN                         = 0x00000000,
DS_REF_IS_EXT_GENLOCK                    = 0x00000001,
DS_REF_IS_PCIE                           = 0x00000002,
} DS_REF_SRC;

/*
 * DVOACLKC_IN_PHASE enum
 */

typedef enum DVOACLKC_IN_PHASE {
DVOACLKC_IN_OPPOSITE_PHASE_WITH_PCLK_DVO = 0x00000000,
DVOACLKC_IN_PHASE_WITH_PCLK_DVO          = 0x00000001,
} DVOACLKC_IN_PHASE;

/*
 * DVOACLKC_MVP_IN_PHASE enum
 */

typedef enum DVOACLKC_MVP_IN_PHASE {
DVOACLKC_MVP_IN_OPPOSITE_PHASE_WITH_PCLK_DVO = 0x00000000,
DVOACLKC_MVP_IN_PHASE_WITH_PCLK_DVO      = 0x00000001,
} DVOACLKC_MVP_IN_PHASE;

/*
 * DVOACLKC_MVP_SKEW_PHASE_OVERRIDE enum
 */

typedef enum DVOACLKC_MVP_SKEW_PHASE_OVERRIDE {
DVOACLKC_MVP_SKEW_PHASE_OVERRIDE_DISABLE = 0x00000000,
DVOACLKC_MVP_SKEW_PHASE_OVERRIDE_ENABLE  = 0x00000001,
} DVOACLKC_MVP_SKEW_PHASE_OVERRIDE;

/*
 * DVOACLKD_IN_PHASE enum
 */

typedef enum DVOACLKD_IN_PHASE {
DVOACLKD_IN_OPPOSITE_PHASE_WITH_PCLK_DVO = 0x00000000,
DVOACLKD_IN_PHASE_WITH_PCLK_DVO          = 0x00000001,
} DVOACLKD_IN_PHASE;

/*
 * DVOACLK_COARSE_SKEW_CNTL enum
 */

typedef enum DVOACLK_COARSE_SKEW_CNTL {
DVOACLK_COARSE_SKEW_CNTL_NO_ADJUSTMENT   = 0x00000000,
DVOACLK_COARSE_SKEW_CNTL_DELAY_1_STEP    = 0x00000001,
DVOACLK_COARSE_SKEW_CNTL_DELAY_2_STEPS   = 0x00000002,
DVOACLK_COARSE_SKEW_CNTL_DELAY_3_STEPS   = 0x00000003,
DVOACLK_COARSE_SKEW_CNTL_DELAY_4_STEPS   = 0x00000004,
DVOACLK_COARSE_SKEW_CNTL_DELAY_5_STEPS   = 0x00000005,
DVOACLK_COARSE_SKEW_CNTL_DELAY_6_STEPS   = 0x00000006,
DVOACLK_COARSE_SKEW_CNTL_DELAY_7_STEPS   = 0x00000007,
DVOACLK_COARSE_SKEW_CNTL_DELAY_8_STEPS   = 0x00000008,
DVOACLK_COARSE_SKEW_CNTL_DELAY_9_STEPS   = 0x00000009,
DVOACLK_COARSE_SKEW_CNTL_DELAY_10_STEPS  = 0x0000000a,
DVOACLK_COARSE_SKEW_CNTL_DELAY_11_STEPS  = 0x0000000b,
DVOACLK_COARSE_SKEW_CNTL_DELAY_12_STEPS  = 0x0000000c,
DVOACLK_COARSE_SKEW_CNTL_DELAY_13_STEPS  = 0x0000000d,
DVOACLK_COARSE_SKEW_CNTL_DELAY_14_STEPS  = 0x0000000e,
DVOACLK_COARSE_SKEW_CNTL_DELAY_15_STEPS  = 0x0000000f,
DVOACLK_COARSE_SKEW_CNTL_EARLY_1_STEP    = 0x00000010,
DVOACLK_COARSE_SKEW_CNTL_EARLY_2_STEPS   = 0x00000011,
DVOACLK_COARSE_SKEW_CNTL_EARLY_3_STEPS   = 0x00000012,
DVOACLK_COARSE_SKEW_CNTL_EARLY_4_STEPS   = 0x00000013,
DVOACLK_COARSE_SKEW_CNTL_EARLY_5_STEPS   = 0x00000014,
DVOACLK_COARSE_SKEW_CNTL_EARLY_6_STEPS   = 0x00000015,
DVOACLK_COARSE_SKEW_CNTL_EARLY_7_STEPS   = 0x00000016,
DVOACLK_COARSE_SKEW_CNTL_EARLY_8_STEPS   = 0x00000017,
DVOACLK_COARSE_SKEW_CNTL_EARLY_9_STEPS   = 0x00000018,
DVOACLK_COARSE_SKEW_CNTL_EARLY_10_STEPS  = 0x00000019,
DVOACLK_COARSE_SKEW_CNTL_EARLY_11_STEPS  = 0x0000001a,
DVOACLK_COARSE_SKEW_CNTL_EARLY_12_STEPS  = 0x0000001b,
DVOACLK_COARSE_SKEW_CNTL_EARLY_13_STEPS  = 0x0000001c,
DVOACLK_COARSE_SKEW_CNTL_EARLY_14_STEPS  = 0x0000001d,
DVOACLK_COARSE_SKEW_CNTL_EARLY_15_STEPS  = 0x0000001e,
} DVOACLK_COARSE_SKEW_CNTL;

/*
 * DVOACLK_FINE_SKEW_CNTL enum
 */

typedef enum DVOACLK_FINE_SKEW_CNTL {
DVOACLK_FINE_SKEW_CNTL_NO_ADJUSTMENT     = 0x00000000,
DVOACLK_FINE_SKEW_CNTL_DELAY_1_STEP      = 0x00000001,
DVOACLK_FINE_SKEW_CNTL_DELAY_2_STEPS     = 0x00000002,
DVOACLK_FINE_SKEW_CNTL_DELAY_3_STEPS     = 0x00000003,
DVOACLK_FINE_SKEW_CNTL_EARLY_1_STEP      = 0x00000004,
DVOACLK_FINE_SKEW_CNTL_EARLY_2_STEPS     = 0x00000005,
DVOACLK_FINE_SKEW_CNTL_EARLY_3_STEPS     = 0x00000006,
DVOACLK_FINE_SKEW_CNTL_EARLY_4_STEPS     = 0x00000007,
} DVOACLK_FINE_SKEW_CNTL;

/*
 * DVO_ENABLE_RST enum
 */

typedef enum DVO_ENABLE_RST {
DVO_ENABLE_RST_DISABLE                   = 0x00000000,
DVO_ENABLE_RST_ENABLE                    = 0x00000001,
} DVO_ENABLE_RST;

/*
 * ENABLE enum
 */

typedef enum ENABLE {
DISABLE_THE_FEATURE                      = 0x00000000,
ENABLE_THE_FEATURE                       = 0x00000001,
} ENABLE;

/*
 * ENABLE_CLOCK enum
 */

typedef enum ENABLE_CLOCK {
ENABLE_THE_REFCLK                        = 0x00000000,
ENABLE_THE_FUNC_CLOCK                    = 0x00000001,
} ENABLE_CLOCK;

/*
 * FORCE_DISABLE_CLOCK enum
 */

typedef enum FORCE_DISABLE_CLOCK {
NOT_FORCE_THE_CLOCK_DISABLED             = 0x00000000,
FORCE_THE_CLOCK_DISABLED                 = 0x00000001,
} FORCE_DISABLE_CLOCK;

/*
 * HDMICHARCLK_SRC_SEL enum
 */

typedef enum HDMICHARCLK_SRC_SEL {
HDMICHARCLK_SRC_SEL_UNIPHYA              = 0x00000000,
HDMICHARCLK_SRC_SEL_UNIPHYB              = 0x00000001,
HDMICHARCLK_SRC_SEL_UNIPHYC              = 0x00000002,
HDMICHARCLK_SRC_SEL_UNIPHYD              = 0x00000003,
HDMICHARCLK_SRC_SEL_UNIPHYE              = 0x00000004,
HDMICHARCLK_SRC_SEL_SRC_RESERVED         = 0x00000005,
} HDMICHARCLK_SRC_SEL;

/*
 * HDMISTREAMCLK_DTO_FORCE_DIS enum
 */

typedef enum HDMISTREAMCLK_DTO_FORCE_DIS {
DTO_FORCE_NO_BYPASS                      = 0x00000000,
DTO_FORCE_BYPASS                         = 0x00000001,
} HDMISTREAMCLK_DTO_FORCE_DIS;

/*
 * HDMISTREAMCLK_SRC_SEL enum
 */

typedef enum HDMISTREAMCLK_SRC_SEL {
SEL_REFCLK0                              = 0x00000000,
SEL_DTBCLK0                              = 0x00000001,
SEL_DTBCLK1                              = 0x00000002,
} HDMISTREAMCLK_SRC_SEL;

/*
 * JITTER_REMOVE_DISABLE enum
 */

typedef enum JITTER_REMOVE_DISABLE {
ENABLE_JITTER_REMOVAL                    = 0x00000000,
DISABLE_JITTER_REMOVAL                   = 0x00000001,
} JITTER_REMOVE_DISABLE;

/*
 * MICROSECOND_TIME_BASE_CLOCK_SOURCE_SEL enum
 */

typedef enum MICROSECOND_TIME_BASE_CLOCK_SOURCE_SEL {
MICROSECOND_TIME_BASE_CLOCK_IS_XTALIN    = 0x00000000,
MICROSECOND_TIME_BASE_CLOCK_IS_DCCGREFCLK = 0x00000001,
} MICROSECOND_TIME_BASE_CLOCK_SOURCE_SEL;

/*
 * MILLISECOND_TIME_BASE_CLOCK_SOURCE_SEL enum
 */

typedef enum MILLISECOND_TIME_BASE_CLOCK_SOURCE_SEL {
MILLISECOND_TIME_BASE_CLOCK_IS_XTALIN    = 0x00000000,
MILLISECOND_TIME_BASE_CLOCK_IS_DCCGREFCLK = 0x00000001,
} MILLISECOND_TIME_BASE_CLOCK_SOURCE_SEL;

/*
 * OTG_ADD_PIXEL enum
 */

typedef enum OTG_ADD_PIXEL {
OTG_ADD_PIXEL_NOOP                       = 0x00000000,
OTG_ADD_PIXEL_FORCE                      = 0x00000001,
} OTG_ADD_PIXEL;

/*
 * OTG_DROP_PIXEL enum
 */

typedef enum OTG_DROP_PIXEL {
OTG_DROP_PIXEL_NOOP                      = 0x00000000,
OTG_DROP_PIXEL_FORCE                     = 0x00000001,
} OTG_DROP_PIXEL;

/*
 * PHYSYMCLK_FORCE_EN enum
 */

typedef enum PHYSYMCLK_FORCE_EN {
PHYSYMCLK_FORCE_EN_DISABLE               = 0x00000000,
PHYSYMCLK_FORCE_EN_ENABLE                = 0x00000001,
} PHYSYMCLK_FORCE_EN;

/*
 * PHYSYMCLK_FORCE_SRC_SEL enum
 */

typedef enum PHYSYMCLK_FORCE_SRC_SEL {
PHYSYMCLK_FORCE_SRC_SYMCLK               = 0x00000000,
PHYSYMCLK_FORCE_SRC_PHYD18CLK            = 0x00000001,
PHYSYMCLK_FORCE_SRC_PHYD32CLK            = 0x00000002,
} PHYSYMCLK_FORCE_SRC_SEL;

/*
 * PIPE_PHYPLL_PIXEL_RATE_SOURCE enum
 */

typedef enum PIPE_PHYPLL_PIXEL_RATE_SOURCE {
PIPE_PHYPLL_PIXEL_RATE_SOURCE_UNIPHYA    = 0x00000000,
PIPE_PHYPLL_PIXEL_RATE_SOURCE_UNIPHYB    = 0x00000001,
PIPE_PHYPLL_PIXEL_RATE_SOURCE_UNIPHYC    = 0x00000002,
PIPE_PHYPLL_PIXEL_RATE_SOURCE_UNIPHYD    = 0x00000003,
PIPE_PHYPLL_PIXEL_RATE_SOURCE_RESERVED   = 0x00000004,
} PIPE_PHYPLL_PIXEL_RATE_SOURCE;

/*
 * PIPE_PIXEL_RATE_PLL_SOURCE enum
 */

typedef enum PIPE_PIXEL_RATE_PLL_SOURCE {
PIPE_PIXEL_RATE_PLL_SOURCE_PHYPLL        = 0x00000000,
PIPE_PIXEL_RATE_PLL_SOURCE_DISPPLL       = 0x00000001,
} PIPE_PIXEL_RATE_PLL_SOURCE;

/*
 * PIPE_PIXEL_RATE_SOURCE enum
 */

typedef enum PIPE_PIXEL_RATE_SOURCE {
PIPE_PIXEL_RATE_SOURCE_P0PLL             = 0x00000000,
PIPE_PIXEL_RATE_SOURCE_P1PLL             = 0x00000001,
PIPE_PIXEL_RATE_SOURCE_P2PLL             = 0x00000002,
} PIPE_PIXEL_RATE_SOURCE;

/*
 * PLL_CFG_IF_SOFT_RESET enum
 */

typedef enum PLL_CFG_IF_SOFT_RESET {
PLL_CFG_IF_SOFT_RESET_NOOP               = 0x00000000,
PLL_CFG_IF_SOFT_RESET_FORCE              = 0x00000001,
} PLL_CFG_IF_SOFT_RESET;

/*
 * SYMCLK_FE_FORCE_EN enum
 */

typedef enum SYMCLK_FE_FORCE_EN {
SYMCLK_FE_FORCE_EN_DISABLE               = 0x00000000,
SYMCLK_FE_FORCE_EN_ENABLE                = 0x00000001,
} SYMCLK_FE_FORCE_EN;

/*
 * SYMCLK_FE_FORCE_SRC enum
 */

typedef enum SYMCLK_FE_FORCE_SRC {
SYMCLK_FE_FORCE_SRC_UNIPHYA              = 0x00000000,
SYMCLK_FE_FORCE_SRC_UNIPHYB              = 0x00000001,
SYMCLK_FE_FORCE_SRC_UNIPHYC              = 0x00000002,
SYMCLK_FE_FORCE_SRC_UNIPHYD              = 0x00000003,
SYMCLK_FE_FORCE_SRC_RESERVED             = 0x00000004,
} SYMCLK_FE_FORCE_SRC;

/*
 * TEST_CLK_DIV_SEL enum
 */

typedef enum TEST_CLK_DIV_SEL {
NO_DIV                                   = 0x00000000,
DIV_2                                    = 0x00000001,
DIV_4                                    = 0x00000002,
DIV_8                                    = 0x00000003,
} TEST_CLK_DIV_SEL;

/*
 * VSYNC_CNT_LATCH_MASK enum
 */

typedef enum VSYNC_CNT_LATCH_MASK {
VSYNC_CNT_LATCH_MASK_0                   = 0x00000000,
VSYNC_CNT_LATCH_MASK_1                   = 0x00000001,
} VSYNC_CNT_LATCH_MASK;

/*
 * VSYNC_CNT_RESET_SEL enum
 */

typedef enum VSYNC_CNT_RESET_SEL {
VSYNC_CNT_RESET_SEL_0                    = 0x00000000,
VSYNC_CNT_RESET_SEL_1                    = 0x00000001,
} VSYNC_CNT_RESET_SEL;

/*
 * XTAL_REF_CLOCK_SOURCE_SEL enum
 */

typedef enum XTAL_REF_CLOCK_SOURCE_SEL {
XTAL_REF_CLOCK_SOURCE_SEL_XTALIN         = 0x00000000,
XTAL_REF_CLOCK_SOURCE_SEL_DCCGREFCLK     = 0x00000001,
} XTAL_REF_CLOCK_SOURCE_SEL;

/*
 * XTAL_REF_SEL enum
 */

typedef enum XTAL_REF_SEL {
XTAL_REF_SEL_1X                          = 0x00000000,
XTAL_REF_SEL_2X                          = 0x00000001,
} XTAL_REF_SEL;

/*******************************************************
 * HPD Enums
 *******************************************************/

/*
 * HPD_INT_CONTROL_ACK enum
 */

typedef enum HPD_INT_CONTROL_ACK {
HPD_INT_CONTROL_ACK_0                    = 0x00000000,
HPD_INT_CONTROL_ACK_1                    = 0x00000001,
} HPD_INT_CONTROL_ACK;

/*
 * HPD_INT_CONTROL_POLARITY enum
 */

typedef enum HPD_INT_CONTROL_POLARITY {
HPD_INT_CONTROL_GEN_INT_ON_DISCON        = 0x00000000,
HPD_INT_CONTROL_GEN_INT_ON_CON           = 0x00000001,
} HPD_INT_CONTROL_POLARITY;

/*
 * HPD_INT_CONTROL_RX_INT_ACK enum
 */

typedef enum HPD_INT_CONTROL_RX_INT_ACK {
HPD_INT_CONTROL_RX_INT_ACK_0             = 0x00000000,
HPD_INT_CONTROL_RX_INT_ACK_1             = 0x00000001,
} HPD_INT_CONTROL_RX_INT_ACK;

/*******************************************************
 * DP Enums
 *******************************************************/

/*
 * DPHY_8B10B_CUR_DISP enum
 */

typedef enum DPHY_8B10B_CUR_DISP {
DPHY_8B10B_CUR_DISP_ZERO                 = 0x00000000,
DPHY_8B10B_CUR_DISP_ONE                  = 0x00000001,
} DPHY_8B10B_CUR_DISP;

/*
 * DPHY_8B10B_RESET enum
 */

typedef enum DPHY_8B10B_RESET {
DPHY_8B10B_NOT_RESET                     = 0x00000000,
DPHY_8B10B_RESETET                       = 0x00000001,
} DPHY_8B10B_RESET;

/*
 * DPHY_ALT_SCRAMBLER_RESET_EN enum
 */

typedef enum DPHY_ALT_SCRAMBLER_RESET_EN {
DPHY_ALT_SCRAMBLER_REGULAR_RESET_VALUE   = 0x00000000,
DPHY_ALT_SCRAMBLER_INTERNAL_RESET_SOLUTION = 0x00000001,
} DPHY_ALT_SCRAMBLER_RESET_EN;

/*
 * DPHY_ALT_SCRAMBLER_RESET_SEL enum
 */

typedef enum DPHY_ALT_SCRAMBLER_RESET_SEL {
DPHY_ALT_SCRAMBLER_RESET_SEL_EDP_RESET_VALUE = 0x00000000,
DPHY_ALT_SCRAMBLER_RESET_SEL_CUSTOM_RESET_VALUE = 0x00000001,
} DPHY_ALT_SCRAMBLER_RESET_SEL;

/*
 * DPHY_ATEST_SEL_LANE0 enum
 */

typedef enum DPHY_ATEST_SEL_LANE0 {
DPHY_ATEST_LANE0_PRBS_PATTERN            = 0x00000000,
DPHY_ATEST_LANE0_REG_PATTERN             = 0x00000001,
} DPHY_ATEST_SEL_LANE0;

/*
 * DPHY_ATEST_SEL_LANE1 enum
 */

typedef enum DPHY_ATEST_SEL_LANE1 {
DPHY_ATEST_LANE1_PRBS_PATTERN            = 0x00000000,
DPHY_ATEST_LANE1_REG_PATTERN             = 0x00000001,
} DPHY_ATEST_SEL_LANE1;

/*
 * DPHY_ATEST_SEL_LANE2 enum
 */

typedef enum DPHY_ATEST_SEL_LANE2 {
DPHY_ATEST_LANE2_PRBS_PATTERN            = 0x00000000,
DPHY_ATEST_LANE2_REG_PATTERN             = 0x00000001,
} DPHY_ATEST_SEL_LANE2;

/*
 * DPHY_ATEST_SEL_LANE3 enum
 */

typedef enum DPHY_ATEST_SEL_LANE3 {
DPHY_ATEST_LANE3_PRBS_PATTERN            = 0x00000000,
DPHY_ATEST_LANE3_REG_PATTERN             = 0x00000001,
} DPHY_ATEST_SEL_LANE3;

/*
 * DPHY_BYPASS enum
 */

typedef enum DPHY_BYPASS {
DPHY_8B10B_OUTPUT                        = 0x00000000,
DPHY_DBG_OUTPUT                          = 0x00000001,
} DPHY_BYPASS;

/*
 * DPHY_CRC_CONT_EN enum
 */

typedef enum DPHY_CRC_CONT_EN {
DPHY_CRC_ONE_SHOT                        = 0x00000000,
DPHY_CRC_CONTINUOUS                      = 0x00000001,
} DPHY_CRC_CONT_EN;

/*
 * DPHY_CRC_EN enum
 */

typedef enum DPHY_CRC_EN {
DPHY_CRC_DISABLED                        = 0x00000000,
DPHY_CRC_ENABLED                         = 0x00000001,
} DPHY_CRC_EN;

/*
 * DPHY_CRC_FIELD enum
 */

typedef enum DPHY_CRC_FIELD {
DPHY_CRC_START_FROM_TOP_FIELD            = 0x00000000,
DPHY_CRC_START_FROM_BOTTOM_FIELD         = 0x00000001,
} DPHY_CRC_FIELD;

/*
 * DPHY_CRC_MST_PHASE_ERROR_ACK enum
 */

typedef enum DPHY_CRC_MST_PHASE_ERROR_ACK {
DPHY_CRC_MST_PHASE_ERROR_NO_ACK          = 0x00000000,
DPHY_CRC_MST_PHASE_ERROR_ACKED           = 0x00000001,
} DPHY_CRC_MST_PHASE_ERROR_ACK;

/*
 * DPHY_CRC_SEL enum
 */

typedef enum DPHY_CRC_SEL {
DPHY_CRC_LANE0_SELECTED                  = 0x00000000,
DPHY_CRC_LANE1_SELECTED                  = 0x00000001,
DPHY_CRC_LANE2_SELECTED                  = 0x00000002,
DPHY_CRC_LANE3_SELECTED                  = 0x00000003,
} DPHY_CRC_SEL;

/*
 * DPHY_FEC_ENABLE enum
 */

typedef enum DPHY_FEC_ENABLE {
DPHY_FEC_DISABLED                        = 0x00000000,
DPHY_FEC_ENABLED                         = 0x00000001,
} DPHY_FEC_ENABLE;

/*
 * DPHY_FEC_READY enum
 */

typedef enum DPHY_FEC_READY {
DPHY_FEC_READY_EN                        = 0x00000000,
DPHY_FEC_READY_DIS                       = 0x00000001,
} DPHY_FEC_READY;

/*
 * DPHY_LOAD_BS_COUNT_START enum
 */

typedef enum DPHY_LOAD_BS_COUNT_START {
DPHY_LOAD_BS_COUNT_STARTED               = 0x00000000,
DPHY_LOAD_BS_COUNT_NOT_STARTED           = 0x00000001,
} DPHY_LOAD_BS_COUNT_START;

/*
 * DPHY_PRBS_EN enum
 */

typedef enum DPHY_PRBS_EN {
DPHY_PRBS_DISABLE                        = 0x00000000,
DPHY_PRBS_ENABLE                         = 0x00000001,
} DPHY_PRBS_EN;

/*
 * DPHY_PRBS_SEL enum
 */

typedef enum DPHY_PRBS_SEL {
DPHY_PRBS7_SELECTED                      = 0x00000000,
DPHY_PRBS23_SELECTED                     = 0x00000001,
DPHY_PRBS11_SELECTED                     = 0x00000002,
} DPHY_PRBS_SEL;

/*
 * DPHY_RX_FAST_TRAINING_CAPABLE enum
 */

typedef enum DPHY_RX_FAST_TRAINING_CAPABLE {
DPHY_FAST_TRAINING_NOT_CAPABLE_0         = 0x00000000,
DPHY_FAST_TRAINING_CAPABLE               = 0x00000001,
} DPHY_RX_FAST_TRAINING_CAPABLE;

/*
 * DPHY_SCRAMBLER_ADVANCE enum
 */

typedef enum DPHY_SCRAMBLER_ADVANCE {
DPHY_DPHY_SCRAMBLER_ADVANCE_ON_DATA_SYMBOL_ONLY = 0x00000000,
DPHY_SCRAMBLER_ADVANCE_ON_BOTH_DATA_AND_CTRL = 0x00000001,
} DPHY_SCRAMBLER_ADVANCE;

/*
 * DPHY_SCRAMBLER_DIS enum
 */

typedef enum DPHY_SCRAMBLER_DIS {
DPHY_SCR_ENABLED                         = 0x00000000,
DPHY_SCR_DISABLED                        = 0x00000001,
} DPHY_SCRAMBLER_DIS;

/*
 * DPHY_SCRAMBLER_KCODE enum
 */

typedef enum DPHY_SCRAMBLER_KCODE {
DPHY_SCRAMBLER_KCODE_DISABLED            = 0x00000000,
DPHY_SCRAMBLER_KCODE_ENABLED             = 0x00000001,
} DPHY_SCRAMBLER_KCODE;

/*
 * DPHY_SCRAMBLER_SEL enum
 */

typedef enum DPHY_SCRAMBLER_SEL {
DPHY_SCRAMBLER_SEL_LANE_DATA             = 0x00000000,
DPHY_SCRAMBLER_SEL_DBG_DATA              = 0x00000001,
} DPHY_SCRAMBLER_SEL;

/*
 * DPHY_SKEW_BYPASS enum
 */

typedef enum DPHY_SKEW_BYPASS {
DPHY_WITH_SKEW                           = 0x00000000,
DPHY_NO_SKEW                             = 0x00000001,
} DPHY_SKEW_BYPASS;

/*
 * DPHY_STREAM_RESET_DURING_FAST_TRAINING_ENUM enum
 */

typedef enum DPHY_STREAM_RESET_DURING_FAST_TRAINING_ENUM {
DPHY_STREAM_RESET_DURING_FAST_TRAINING_RESET = 0x00000000,
DPHY_STREAM_RESET_DURING_FAST_TRAINING_NOT_RESET = 0x00000001,
} DPHY_STREAM_RESET_DURING_FAST_TRAINING_ENUM;

/*
 * DPHY_SW_FAST_TRAINING_START enum
 */

typedef enum DPHY_SW_FAST_TRAINING_START {
DPHY_SW_FAST_TRAINING_NOT_STARTED        = 0x00000000,
DPHY_SW_FAST_TRAINING_STARTED            = 0x00000001,
} DPHY_SW_FAST_TRAINING_START;

/*
 * DPHY_TRAINING_PATTERN_SEL enum
 */

typedef enum DPHY_TRAINING_PATTERN_SEL {
DPHY_TRAINING_PATTERN_1                  = 0x00000000,
DPHY_TRAINING_PATTERN_2                  = 0x00000001,
DPHY_TRAINING_PATTERN_3                  = 0x00000002,
DPHY_TRAINING_PATTERN_4                  = 0x00000003,
} DPHY_TRAINING_PATTERN_SEL;

/*
 * DP_COMPONENT_DEPTH enum
 */

typedef enum DP_COMPONENT_DEPTH {
DP_COMPONENT_DEPTH_6BPC                  = 0x00000000,
DP_COMPONENT_DEPTH_8BPC                  = 0x00000001,
DP_COMPONENT_DEPTH_10BPC                 = 0x00000002,
DP_COMPONENT_DEPTH_12BPC                 = 0x00000003,
DP_COMPONENT_DEPTH_16BPC                 = 0x00000004,
} DP_COMPONENT_DEPTH;

/*
 * DP_CP_ENCRYPTION_TYPE enum
 */

typedef enum DP_CP_ENCRYPTION_TYPE {
DP_CP_ENCRYPTION_TYPE_0                  = 0x00000000,
DP_CP_ENCRYPTION_TYPE_1                  = 0x00000001,
} DP_CP_ENCRYPTION_TYPE;

/*
 * DP_DPHY_8B10B_EXT_DISP enum
 */

typedef enum DP_DPHY_8B10B_EXT_DISP {
DP_DPHY_8B10B_EXT_DISP_ZERO              = 0x00000000,
DP_DPHY_8B10B_EXT_DISP_ONE               = 0x00000001,
} DP_DPHY_8B10B_EXT_DISP;

/*
 * DP_DPHY_FAST_TRAINING_COMPLETE_ACK enum
 */

typedef enum DP_DPHY_FAST_TRAINING_COMPLETE_ACK {
DP_DPHY_FAST_TRAINING_COMPLETE_NOT_ACKED = 0x00000000,
DP_DPHY_FAST_TRAINING_COMPLETE_ACKED     = 0x00000001,
} DP_DPHY_FAST_TRAINING_COMPLETE_ACK;

/*
 * DP_DPHY_FAST_TRAINING_COMPLETE_MASK enum
 */

typedef enum DP_DPHY_FAST_TRAINING_COMPLETE_MASK {
DP_DPHY_FAST_TRAINING_COMPLETE_MASKED    = 0x00000000,
DP_DPHY_FAST_TRAINING_COMPLETE_NOT_MASKED = 0x00000001,
} DP_DPHY_FAST_TRAINING_COMPLETE_MASK;

/*
 * DP_DPHY_FAST_TRAINING_VBLANK_EDGE_DETECT_EN enum
 */

typedef enum DP_DPHY_FAST_TRAINING_VBLANK_EDGE_DETECT_EN {
DP_DPHY_FAST_TRAINING_VBLANK_EDGE_DETECT_DISABLED = 0x00000000,
DP_DPHY_FAST_TRAINING_VBLANK_EDGE_DETECT_ENABLED = 0x00000001,
} DP_DPHY_FAST_TRAINING_VBLANK_EDGE_DETECT_EN;

/*
 * DP_DPHY_HBR2_PATTERN_CONTROL_MODE enum
 */

typedef enum DP_DPHY_HBR2_PATTERN_CONTROL_MODE {
DP_DPHY_HBR2_PASS_THROUGH                = 0x00000000,
DP_DPHY_HBR2_PATTERN_1                   = 0x00000001,
DP_DPHY_HBR2_PATTERN_2_NEG               = 0x00000002,
DP_DPHY_HBR2_PATTERN_3                   = 0x00000003,
DP_DPHY_HBR2_PATTERN_2_POS               = 0x00000006,
} DP_DPHY_HBR2_PATTERN_CONTROL_MODE;

/*
 * DP_DSC_MODE enum
 */

typedef enum DP_DSC_MODE {
DP_DSC_DISABLE                           = 0x00000000,
DP_DSC_444_SIMPLE_422                    = 0x00000001,
DP_DSC_NATIVE_422_420                    = 0x00000002,
} DP_DSC_MODE;

/*
 * DP_EMBEDDED_PANEL_MODE enum
 */

typedef enum DP_EMBEDDED_PANEL_MODE {
DP_EXTERNAL_PANEL                        = 0x00000000,
DP_EMBEDDED_PANEL                        = 0x00000001,
} DP_EMBEDDED_PANEL_MODE;

/*
 * DP_LINK_TRAINING_COMPLETE enum
 */

typedef enum DP_LINK_TRAINING_COMPLETE {
DP_LINK_TRAINING_NOT_COMPLETE            = 0x00000000,
DP_LINK_TRAINING_ALREADY_COMPLETE        = 0x00000001,
} DP_LINK_TRAINING_COMPLETE;

/*
 * DP_LINK_TRAINING_SWITCH_MODE enum
 */

typedef enum DP_LINK_TRAINING_SWITCH_MODE {
DP_LINK_TRAINING_SWITCH_TO_IDLE          = 0x00000000,
DP_LINK_TRAINING_SWITCH_TO_VIDEO         = 0x00000001,
} DP_LINK_TRAINING_SWITCH_MODE;

/*
 * DP_ML_PHY_SEQ_MODE enum
 */

typedef enum DP_ML_PHY_SEQ_MODE {
DP_ML_PHY_SEQ_LINE_NUM                   = 0x00000000,
DP_ML_PHY_SEQ_IMMEDIATE                  = 0x00000001,
} DP_ML_PHY_SEQ_MODE;

/*
 * DP_MSA_V_TIMING_OVERRIDE_EN enum
 */

typedef enum DP_MSA_V_TIMING_OVERRIDE_EN {
MSA_V_TIMING_OVERRIDE_DISABLED           = 0x00000000,
MSA_V_TIMING_OVERRIDE_ENABLED            = 0x00000001,
} DP_MSA_V_TIMING_OVERRIDE_EN;

/*
 * DP_MSE_BLANK_CODE enum
 */

typedef enum DP_MSE_BLANK_CODE {
DP_MSE_BLANK_CODE_SF_FILLED              = 0x00000000,
DP_MSE_BLANK_CODE_ZERO_FILLED            = 0x00000001,
} DP_MSE_BLANK_CODE;

/*
 * DP_MSE_LINK_LINE enum
 */

typedef enum DP_MSE_LINK_LINE {
DP_MSE_LINK_LINE_32_MTP_LONG             = 0x00000000,
DP_MSE_LINK_LINE_64_MTP_LONG             = 0x00000001,
DP_MSE_LINK_LINE_128_MTP_LONG            = 0x00000002,
DP_MSE_LINK_LINE_256_MTP_LONG            = 0x00000003,
} DP_MSE_LINK_LINE;

/*
 * DP_MSE_SAT_ENCRYPT0 enum
 */

typedef enum DP_MSE_SAT_ENCRYPT0 {
DP_MSE_SAT_ENCRYPT0_DISABLED             = 0x00000000,
DP_MSE_SAT_ENCRYPT0_ENABLED              = 0x00000001,
} DP_MSE_SAT_ENCRYPT0;

/*
 * DP_MSE_SAT_ENCRYPT1 enum
 */

typedef enum DP_MSE_SAT_ENCRYPT1 {
DP_MSE_SAT_ENCRYPT1_DISABLED             = 0x00000000,
DP_MSE_SAT_ENCRYPT1_ENABLED              = 0x00000001,
} DP_MSE_SAT_ENCRYPT1;

/*
 * DP_MSE_SAT_ENCRYPT2 enum
 */

typedef enum DP_MSE_SAT_ENCRYPT2 {
DP_MSE_SAT_ENCRYPT2_DISABLED             = 0x00000000,
DP_MSE_SAT_ENCRYPT2_ENABLED              = 0x00000001,
} DP_MSE_SAT_ENCRYPT2;

/*
 * DP_MSE_SAT_ENCRYPT3 enum
 */

typedef enum DP_MSE_SAT_ENCRYPT3 {
DP_MSE_SAT_ENCRYPT3_DISABLED             = 0x00000000,
DP_MSE_SAT_ENCRYPT3_ENABLED              = 0x00000001,
} DP_MSE_SAT_ENCRYPT3;

/*
 * DP_MSE_SAT_ENCRYPT4 enum
 */

typedef enum DP_MSE_SAT_ENCRYPT4 {
DP_MSE_SAT_ENCRYPT4_DISABLED             = 0x00000000,
DP_MSE_SAT_ENCRYPT4_ENABLED              = 0x00000001,
} DP_MSE_SAT_ENCRYPT4;

/*
 * DP_MSE_SAT_ENCRYPT5 enum
 */

typedef enum DP_MSE_SAT_ENCRYPT5 {
DP_MSE_SAT_ENCRYPT5_DISABLED             = 0x00000000,
DP_MSE_SAT_ENCRYPT5_ENABLED              = 0x00000001,
} DP_MSE_SAT_ENCRYPT5;

/*
 * DP_MSE_SAT_UPDATE_ACT enum
 */

typedef enum DP_MSE_SAT_UPDATE_ACT {
DP_MSE_SAT_UPDATE_NO_ACTION              = 0x00000000,
DP_MSE_SAT_UPDATE_WITH_TRIGGER           = 0x00000001,
DP_MSE_SAT_UPDATE_WITHOUT_TRIGGER        = 0x00000002,
} DP_MSE_SAT_UPDATE_ACT;

/*
 * DP_MSE_TIMESTAMP_MODE enum
 */

typedef enum DP_MSE_TIMESTAMP_MODE {
DP_MSE_TIMESTAMP_CALC_BASED_ON_LINK_RATE = 0x00000000,
DP_MSE_TIMESTAMP_CALC_BASED_ON_VC_RATE   = 0x00000001,
} DP_MSE_TIMESTAMP_MODE;

/*
 * DP_MSE_ZERO_ENCODER enum
 */

typedef enum DP_MSE_ZERO_ENCODER {
DP_MSE_NOT_ZERO_FE_ENCODER               = 0x00000000,
DP_MSE_ZERO_FE_ENCODER                   = 0x00000001,
} DP_MSE_ZERO_ENCODER;

/*
 * DP_MSO_NUM_OF_SST_LINKS enum
 */

typedef enum DP_MSO_NUM_OF_SST_LINKS {
DP_MSO_ONE_SSTLINK                       = 0x00000000,
DP_MSO_TWO_SSTLINK                       = 0x00000001,
DP_MSO_FOUR_SSTLINK                      = 0x00000002,
} DP_MSO_NUM_OF_SST_LINKS;

/*
 * DP_PIXEL_ENCODING enum
 */

typedef enum DP_PIXEL_ENCODING {
DP_PIXEL_ENCODING_RGB444                 = 0x00000000,
DP_PIXEL_ENCODING_YCBCR422               = 0x00000001,
DP_PIXEL_ENCODING_YCBCR444               = 0x00000002,
DP_PIXEL_ENCODING_RGB_WIDE_GAMUT         = 0x00000003,
DP_PIXEL_ENCODING_Y_ONLY                 = 0x00000004,
DP_PIXEL_ENCODING_YCBCR420               = 0x00000005,
} DP_PIXEL_ENCODING;

/*
 * DP_PIXEL_PER_CYCLE_PROCESSING_NUM enum
 */

typedef enum DP_PIXEL_PER_CYCLE_PROCESSING_NUM {
DP_ONE_PIXEL_PER_CYCLE                   = 0x00000000,
DP_TWO_PIXEL_PER_CYCLE                   = 0x00000001,
} DP_PIXEL_PER_CYCLE_PROCESSING_NUM;

/*
 * DP_SEC_ASP_CHANNEL_COUNT_OVERRIDE enum
 */

typedef enum DP_SEC_ASP_CHANNEL_COUNT_OVERRIDE {
DP_SEC_ASP_CHANNEL_COUNT_FROM_AZ         = 0x00000000,
DP_SEC_ASP_CHANNEL_COUNT_OVERRIDE_ENABLED = 0x00000001,
} DP_SEC_ASP_CHANNEL_COUNT_OVERRIDE;

/*
 * DP_SEC_ASP_PRIORITY enum
 */

typedef enum DP_SEC_ASP_PRIORITY {
DP_SEC_ASP_LOW_PRIORITY                  = 0x00000000,
DP_SEC_ASP_HIGH_PRIORITY                 = 0x00000001,
} DP_SEC_ASP_PRIORITY;

/*
 * DP_SEC_AUDIO_MUTE enum
 */

typedef enum DP_SEC_AUDIO_MUTE {
DP_SEC_AUDIO_MUTE_HW_CTRL                = 0x00000000,
DP_SEC_AUDIO_MUTE_SW_CTRL                = 0x00000001,
} DP_SEC_AUDIO_MUTE;

/*
 * DP_SEC_COLLISION_ACK enum
 */

typedef enum DP_SEC_COLLISION_ACK {
DP_SEC_COLLISION_ACK_NO_EFFECT           = 0x00000000,
DP_SEC_COLLISION_ACK_CLR_FLAG            = 0x00000001,
} DP_SEC_COLLISION_ACK;

/*
 * DP_SEC_GSP0_PRIORITY enum
 */

typedef enum DP_SEC_GSP0_PRIORITY {
SEC_GSP0_PRIORITY_LOW                    = 0x00000000,
SEC_GSP0_PRIORITY_HIGH                   = 0x00000001,
} DP_SEC_GSP0_PRIORITY;

/*
 * DP_SEC_GSP_SEND enum
 */

typedef enum DP_SEC_GSP_SEND {
NOT_SENT                                 = 0x00000000,
FORCE_SENT                               = 0x00000001,
} DP_SEC_GSP_SEND;

/*
 * DP_SEC_GSP_SEND_ANY_LINE enum
 */

typedef enum DP_SEC_GSP_SEND_ANY_LINE {
SEND_AT_LINK_NUMBER                      = 0x00000000,
SEND_AT_EARLIEST_TIME                    = 0x00000001,
} DP_SEC_GSP_SEND_ANY_LINE;

/*
 * DP_SEC_GSP_SEND_PPS enum
 */

typedef enum DP_SEC_GSP_SEND_PPS {
SEND_NORMAL_PACKET                       = 0x00000000,
SEND_PPS_PACKET                          = 0x00000001,
} DP_SEC_GSP_SEND_PPS;

/*
 * DP_SEC_LINE_REFERENCE enum
 */

typedef enum DP_SEC_LINE_REFERENCE {
REFER_TO_DP_SOF                          = 0x00000000,
REFER_TO_OTG_SOF                         = 0x00000001,
} DP_SEC_LINE_REFERENCE;

/*
 * DP_SEC_TIMESTAMP_MODE enum
 */

typedef enum DP_SEC_TIMESTAMP_MODE {
DP_SEC_TIMESTAMP_PROGRAMMABLE_MODE       = 0x00000000,
DP_SEC_TIMESTAMP_AUTO_CALC_MODE          = 0x00000001,
} DP_SEC_TIMESTAMP_MODE;

/*
 * DP_STEER_OVERFLOW_ACK enum
 */

typedef enum DP_STEER_OVERFLOW_ACK {
DP_STEER_OVERFLOW_ACK_NO_EFFECT          = 0x00000000,
DP_STEER_OVERFLOW_ACK_CLR_INTERRUPT      = 0x00000001,
} DP_STEER_OVERFLOW_ACK;

/*
 * DP_STEER_OVERFLOW_MASK enum
 */

typedef enum DP_STEER_OVERFLOW_MASK {
DP_STEER_OVERFLOW_MASKED                 = 0x00000000,
DP_STEER_OVERFLOW_UNMASK                 = 0x00000001,
} DP_STEER_OVERFLOW_MASK;

/*
 * DP_SYNC_POLARITY enum
 */

typedef enum DP_SYNC_POLARITY {
DP_SYNC_POLARITY_ACTIVE_HIGH             = 0x00000000,
DP_SYNC_POLARITY_ACTIVE_LOW              = 0x00000001,
} DP_SYNC_POLARITY;

/*
 * DP_TU_OVERFLOW_ACK enum
 */

typedef enum DP_TU_OVERFLOW_ACK {
DP_TU_OVERFLOW_ACK_NO_EFFECT             = 0x00000000,
DP_TU_OVERFLOW_ACK_CLR_INTERRUPT         = 0x00000001,
} DP_TU_OVERFLOW_ACK;

/*
 * DP_UDI_LANES enum
 */

typedef enum DP_UDI_LANES {
DP_UDI_1_LANE                            = 0x00000000,
DP_UDI_2_LANES                           = 0x00000001,
DP_UDI_LANES_RESERVED                    = 0x00000002,
DP_UDI_4_LANES                           = 0x00000003,
} DP_UDI_LANES;

/*
 * DP_VID_ENHANCED_FRAME_MODE enum
 */

typedef enum DP_VID_ENHANCED_FRAME_MODE {
VID_NORMAL_FRAME_MODE                    = 0x00000000,
VID_ENHANCED_MODE                        = 0x00000001,
} DP_VID_ENHANCED_FRAME_MODE;

/*
 * DP_VID_M_N_DOUBLE_BUFFER_MODE enum
 */

typedef enum DP_VID_M_N_DOUBLE_BUFFER_MODE {
DP_VID_M_N_DOUBLE_BUFFER_AFTER_VID_M_UPDATE = 0x00000000,
DP_VID_M_N_DOUBLE_BUFFER_AT_FRAME_START  = 0x00000001,
} DP_VID_M_N_DOUBLE_BUFFER_MODE;

/*
 * DP_VID_M_N_GEN_EN enum
 */

typedef enum DP_VID_M_N_GEN_EN {
DP_VID_M_N_PROGRAMMED_VIA_REG            = 0x00000000,
DP_VID_M_N_CALC_AUTO                     = 0x00000001,
} DP_VID_M_N_GEN_EN;

/*
 * DP_VID_N_MUL enum
 */

typedef enum DP_VID_N_MUL {
DP_VID_M_1X_INPUT_PIXEL_RATE             = 0x00000000,
DP_VID_M_2X_INPUT_PIXEL_RATE             = 0x00000001,
DP_VID_M_4X_INPUT_PIXEL_RATE             = 0x00000002,
DP_VID_M_8X_INPUT_PIXEL_RATE             = 0x00000003,
} DP_VID_N_MUL;

/*
 * DP_VID_STREAM_DISABLE_ACK enum
 */

typedef enum DP_VID_STREAM_DISABLE_ACK {
ID_STREAM_DISABLE_NO_ACK                 = 0x00000000,
ID_STREAM_DISABLE_ACKED                  = 0x00000001,
} DP_VID_STREAM_DISABLE_ACK;

/*
 * DP_VID_STREAM_DISABLE_MASK enum
 */

typedef enum DP_VID_STREAM_DISABLE_MASK {
VID_STREAM_DISABLE_MASKED                = 0x00000000,
VID_STREAM_DISABLE_UNMASK                = 0x00000001,
} DP_VID_STREAM_DISABLE_MASK;

/*
 * DP_VID_STREAM_DIS_DEFER enum
 */

typedef enum DP_VID_STREAM_DIS_DEFER {
DP_VID_STREAM_DIS_NO_DEFER               = 0x00000000,
DP_VID_STREAM_DIS_DEFER_TO_HBLANK        = 0x00000001,
DP_VID_STREAM_DIS_DEFER_TO_VBLANK        = 0x00000002,
} DP_VID_STREAM_DIS_DEFER;

/*
 * DP_VID_VBID_FIELD_POL enum
 */

typedef enum DP_VID_VBID_FIELD_POL {
DP_VID_VBID_FIELD_POL_NORMAL             = 0x00000000,
DP_VID_VBID_FIELD_POL_INV                = 0x00000001,
} DP_VID_VBID_FIELD_POL;

/*
 * FEC_ACTIVE_STATUS enum
 */

typedef enum FEC_ACTIVE_STATUS {
DPHY_FEC_NOT_ACTIVE                      = 0x00000000,
DPHY_FEC_ACTIVE                          = 0x00000001,
} FEC_ACTIVE_STATUS;

/*******************************************************
 * DIG Enums
 *******************************************************/

/*
 * DIG_BE_CNTL_HPD_SELECT enum
 */

typedef enum DIG_BE_CNTL_HPD_SELECT {
DIG_BE_CNTL_HPD1                         = 0x00000000,
DIG_BE_CNTL_HPD2                         = 0x00000001,
DIG_BE_CNTL_HPD3                         = 0x00000002,
DIG_BE_CNTL_HPD4                         = 0x00000003,
DIG_BE_CNTL_HPD5                         = 0x00000004,
DIG_BE_CNTL_NO_HPD                       = 0x00000005,
} DIG_BE_CNTL_HPD_SELECT;

/*
 * DIG_BE_CNTL_MODE enum
 */

typedef enum DIG_BE_CNTL_MODE {
DIG_BE_DP_SST_MODE                       = 0x00000000,
DIG_BE_RESERVED1                         = 0x00000001,
DIG_BE_TMDS_DVI_MODE                     = 0x00000002,
DIG_BE_TMDS_HDMI_MODE                    = 0x00000003,
DIG_BE_RESERVED4                         = 0x00000004,
DIG_BE_DP_MST_MODE                       = 0x00000005,
DIG_BE_RESERVED2                         = 0x00000006,
DIG_BE_RESERVED3                         = 0x00000007,
} DIG_BE_CNTL_MODE;

/*
 * DIG_DIGITAL_BYPASS_ENABLE enum
 */

typedef enum DIG_DIGITAL_BYPASS_ENABLE {
DIG_DIGITAL_BYPASS_OFF                   = 0x00000000,
DIG_DIGITAL_BYPASS_ON                    = 0x00000001,
} DIG_DIGITAL_BYPASS_ENABLE;

/*
 * DIG_DIGITAL_BYPASS_SEL enum
 */

typedef enum DIG_DIGITAL_BYPASS_SEL {
DIG_DIGITAL_BYPASS_SEL_BYPASS            = 0x00000000,
DIG_DIGITAL_BYPASS_SEL_36BPP             = 0x00000001,
DIG_DIGITAL_BYPASS_SEL_48BPP_LSB         = 0x00000002,
DIG_DIGITAL_BYPASS_SEL_48BPP_MSB         = 0x00000003,
DIG_DIGITAL_BYPASS_SEL_10BPP_LSB         = 0x00000004,
DIG_DIGITAL_BYPASS_SEL_12BPC_LSB         = 0x00000005,
DIG_DIGITAL_BYPASS_SEL_ALPHA             = 0x00000006,
} DIG_DIGITAL_BYPASS_SEL;

/*
 * DIG_FE_CNTL_SOURCE_SELECT enum
 */

typedef enum DIG_FE_CNTL_SOURCE_SELECT {
DIG_FE_SOURCE_FROM_OTG0                  = 0x00000000,
DIG_FE_SOURCE_FROM_OTG1                  = 0x00000001,
DIG_FE_SOURCE_FROM_OTG2                  = 0x00000002,
DIG_FE_SOURCE_FROM_OTG3                  = 0x00000003,
DIG_FE_SOURCE_RESERVED                   = 0x00000004,
} DIG_FE_CNTL_SOURCE_SELECT;

/*
 * DIG_FE_CNTL_STEREOSYNC_SELECT enum
 */

typedef enum DIG_FE_CNTL_STEREOSYNC_SELECT {
DIG_FE_STEREOSYNC_FROM_OTG0              = 0x00000000,
DIG_FE_STEREOSYNC_FROM_OTG1              = 0x00000001,
DIG_FE_STEREOSYNC_FROM_OTG2              = 0x00000002,
DIG_FE_STEREOSYNC_FROM_OTG3              = 0x00000003,
DIG_FE_STEREOSYNC_RESERVED               = 0x00000004,
} DIG_FE_CNTL_STEREOSYNC_SELECT;

/*
 * DIG_FIFO_CTRL_FORCE_RECOMP_MINMAX enum
 */

typedef enum DIG_FIFO_CTRL_FORCE_RECOMP_MINMAX {
DIG_FIFO_NOT_FORCE_RECOMP_MINMAX         = 0x00000000,
DIG_FIFO_FORCE_RECOMP_MINMAX             = 0x00000001,
} DIG_FIFO_CTRL_FORCE_RECOMP_MINMAX;

/*
 * DIG_FIFO_CTRL_USE_OVERWRITE_LEVEL enum
 */

typedef enum DIG_FIFO_CTRL_USE_OVERWRITE_LEVEL {
DIG_FIFO_USE_OVERWRITE_LEVEL             = 0x00000000,
DIG_FIFO_USE_CAL_AVERAGE_LEVEL           = 0x00000001,
} DIG_FIFO_CTRL_USE_OVERWRITE_LEVEL;

/*
 * DIG_FIFO_FORCE_RECAL_AVERAGE enum
 */

typedef enum DIG_FIFO_FORCE_RECAL_AVERAGE {
DIG_FIFO_NOT_FORCE_RECAL_AVERAGE         = 0x00000000,
DIG_FIFO_FORCE_RECAL_AVERAGE_LEVEL       = 0x00000001,
} DIG_FIFO_FORCE_RECAL_AVERAGE;

/*
 * DIG_FIFO_OUTPUT_PROCESSING_MODE enum
 */

typedef enum DIG_FIFO_OUTPUT_PROCESSING_MODE {
DIG_FIFO_1_PIX_PER_CYCLE                 = 0x00000000,
DIG_FIFO_2_PIX_PER_CYCLE                 = 0x00000001,
} DIG_FIFO_OUTPUT_PROCESSING_MODE;

/*
 * DIG_FIFO_OVERFLOW_UNDERFLOW_ERROR enum
 */

typedef enum DIG_FIFO_OVERFLOW_UNDERFLOW_ERROR {
DIG_FIFO_NO_ERROR_OCCURRED               = 0x00000000,
DIG_FIFO_UNDERFLOW_OCCURRED              = 0x00000001,
DIG_FIFO_OVERFLOW_OCCURRED               = 0x00000002,
} DIG_FIFO_OVERFLOW_UNDERFLOW_ERROR;

/*
 * DIG_FIFO_READ_CLOCK_SRC enum
 */

typedef enum DIG_FIFO_READ_CLOCK_SRC {
DIG_FIFO_READ_CLOCK_SRC_FROM_DCCG        = 0x00000000,
DIG_FIFO_READ_CLOCK_SRC_FROM_DISPLAY_PIPE = 0x00000001,
} DIG_FIFO_READ_CLOCK_SRC;

/*
 * DIG_INPUT_PIXEL_SEL enum
 */

typedef enum DIG_INPUT_PIXEL_SEL {
DIG_ALL_PIXEL                            = 0x00000000,
DIG_EVEN_PIXEL_ONLY                      = 0x00000001,
DIG_ODD_PIXEL_ONLY                       = 0x00000002,
} DIG_INPUT_PIXEL_SEL;

/*
 * DIG_OUTPUT_CRC_CNTL_LINK_SEL enum
 */

typedef enum DIG_OUTPUT_CRC_CNTL_LINK_SEL {
DIG_OUTPUT_CRC_ON_LINK0                  = 0x00000000,
DIG_OUTPUT_CRC_ON_LINK1                  = 0x00000001,
} DIG_OUTPUT_CRC_CNTL_LINK_SEL;

/*
 * DIG_OUTPUT_CRC_DATA_SEL enum
 */

typedef enum DIG_OUTPUT_CRC_DATA_SEL {
DIG_OUTPUT_CRC_FOR_FULLFRAME             = 0x00000000,
DIG_OUTPUT_CRC_FOR_ACTIVEONLY            = 0x00000001,
DIG_OUTPUT_CRC_FOR_VBI                   = 0x00000002,
DIG_OUTPUT_CRC_FOR_AUDIO                 = 0x00000003,
} DIG_OUTPUT_CRC_DATA_SEL;

/*
 * DIG_RANDOM_PATTERN_SEED_RAN_PAT enum
 */

typedef enum DIG_RANDOM_PATTERN_SEED_RAN_PAT {
DIG_RANDOM_PATTERN_SEED_RAN_PAT_ALL_PIXELS = 0x00000000,
DIG_RANDOM_PATTERN_SEED_RAN_PAT_DE_HIGH  = 0x00000001,
} DIG_RANDOM_PATTERN_SEED_RAN_PAT;

/*
 * DIG_SL_PIXEL_GROUPING enum
 */

typedef enum DIG_SL_PIXEL_GROUPING {
DIG_SINGLETON_PIXELS                     = 0x00000000,
DIG_PAIR_PIXELS                          = 0x00000001,
} DIG_SL_PIXEL_GROUPING;

/*
 * DIG_TEST_PATTERN_EXTERNAL_RESET_EN enum
 */

typedef enum DIG_TEST_PATTERN_EXTERNAL_RESET_EN {
DIG_TEST_PATTERN_EXTERNAL_RESET_ENABLE   = 0x00000000,
DIG_TEST_PATTERN_EXTERNAL_RESET_BY_EXT_SIG = 0x00000001,
} DIG_TEST_PATTERN_EXTERNAL_RESET_EN;

/*
 * DIG_TEST_PATTERN_HALF_CLOCK_PATTERN_SEL enum
 */

typedef enum DIG_TEST_PATTERN_HALF_CLOCK_PATTERN_SEL {
DIG_10BIT_TEST_PATTERN                   = 0x00000000,
DIG_ALTERNATING_TEST_PATTERN             = 0x00000001,
} DIG_TEST_PATTERN_HALF_CLOCK_PATTERN_SEL;

/*
 * DIG_TEST_PATTERN_RANDOM_PATTERN_OUT_EN enum
 */

typedef enum DIG_TEST_PATTERN_RANDOM_PATTERN_OUT_EN {
DIG_TEST_PATTERN_NORMAL                  = 0x00000000,
DIG_TEST_PATTERN_RANDOM                  = 0x00000001,
} DIG_TEST_PATTERN_RANDOM_PATTERN_OUT_EN;

/*
 * DIG_TEST_PATTERN_RANDOM_PATTERN_RESET enum
 */

typedef enum DIG_TEST_PATTERN_RANDOM_PATTERN_RESET {
DIG_RANDOM_PATTERN_ENABLED               = 0x00000000,
DIG_RANDOM_PATTERN_RESETED               = 0x00000001,
} DIG_TEST_PATTERN_RANDOM_PATTERN_RESET;

/*
 * DIG_TEST_PATTERN_TEST_PATTERN_OUT_EN enum
 */

typedef enum DIG_TEST_PATTERN_TEST_PATTERN_OUT_EN {
DIG_IN_NORMAL_OPERATION                  = 0x00000000,
DIG_IN_DEBUG_MODE                        = 0x00000001,
} DIG_TEST_PATTERN_TEST_PATTERN_OUT_EN;

/*
 * DOLBY_VISION_ENABLE enum
 */

typedef enum DOLBY_VISION_ENABLE {
DOLBY_VISION_DISABLED                    = 0x00000000,
DOLBY_VISION_ENABLED                     = 0x00000001,
} DOLBY_VISION_ENABLE;

/*
 * HDMI_ACP_SEND enum
 */

typedef enum HDMI_ACP_SEND {
HDMI_ACP_NOT_SEND                        = 0x00000000,
HDMI_ACP_PKT_SEND                        = 0x00000001,
} HDMI_ACP_SEND;

/*
 * HDMI_ACR_AUDIO_PRIORITY enum
 */

typedef enum HDMI_ACR_AUDIO_PRIORITY {
HDMI_ACR_PKT_HIGH_PRIORITY_THAN_AUDIO_SAMPLE = 0x00000000,
HDMI_AUDIO_SAMPLE_HIGH_PRIORITY_THAN_ACR_PKT = 0x00000001,
} HDMI_ACR_AUDIO_PRIORITY;

/*
 * HDMI_ACR_CONT enum
 */

typedef enum HDMI_ACR_CONT {
HDMI_ACR_CONT_DISABLE                    = 0x00000000,
HDMI_ACR_CONT_ENABLE                     = 0x00000001,
} HDMI_ACR_CONT;

/*
 * HDMI_ACR_N_MULTIPLE enum
 */

typedef enum HDMI_ACR_N_MULTIPLE {
HDMI_ACR_0_MULTIPLE_RESERVED             = 0x00000000,
HDMI_ACR_1_MULTIPLE                      = 0x00000001,
HDMI_ACR_2_MULTIPLE                      = 0x00000002,
HDMI_ACR_3_MULTIPLE_RESERVED             = 0x00000003,
HDMI_ACR_4_MULTIPLE                      = 0x00000004,
HDMI_ACR_5_MULTIPLE_RESERVED             = 0x00000005,
HDMI_ACR_6_MULTIPLE_RESERVED             = 0x00000006,
HDMI_ACR_7_MULTIPLE_RESERVED             = 0x00000007,
} HDMI_ACR_N_MULTIPLE;

/*
 * HDMI_ACR_SELECT enum
 */

typedef enum HDMI_ACR_SELECT {
HDMI_ACR_SELECT_HW                       = 0x00000000,
HDMI_ACR_SELECT_32K                      = 0x00000001,
HDMI_ACR_SELECT_44K                      = 0x00000002,
HDMI_ACR_SELECT_48K                      = 0x00000003,
} HDMI_ACR_SELECT;

/*
 * HDMI_ACR_SEND enum
 */

typedef enum HDMI_ACR_SEND {
HDMI_ACR_NOT_SEND                        = 0x00000000,
HDMI_ACR_PKT_SEND                        = 0x00000001,
} HDMI_ACR_SEND;

/*
 * HDMI_ACR_SOURCE enum
 */

typedef enum HDMI_ACR_SOURCE {
HDMI_ACR_SOURCE_HW                       = 0x00000000,
HDMI_ACR_SOURCE_SW                       = 0x00000001,
} HDMI_ACR_SOURCE;

/*
 * HDMI_AUDIO_DELAY_EN enum
 */

typedef enum HDMI_AUDIO_DELAY_EN {
HDMI_AUDIO_DELAY_DISABLE                 = 0x00000000,
HDMI_AUDIO_DELAY_58CLK                   = 0x00000001,
HDMI_AUDIO_DELAY_56CLK                   = 0x00000002,
HDMI_AUDIO_DELAY_RESERVED                = 0x00000003,
} HDMI_AUDIO_DELAY_EN;

/*
 * HDMI_AUDIO_INFO_CONT enum
 */

typedef enum HDMI_AUDIO_INFO_CONT {
HDMI_AUDIO_INFO_CONT_DISABLE             = 0x00000000,
HDMI_AUDIO_INFO_CONT_ENABLE              = 0x00000001,
} HDMI_AUDIO_INFO_CONT;

/*
 * HDMI_AUDIO_INFO_SEND enum
 */

typedef enum HDMI_AUDIO_INFO_SEND {
HDMI_AUDIO_INFO_NOT_SEND                 = 0x00000000,
HDMI_AUDIO_INFO_PKT_SEND                 = 0x00000001,
} HDMI_AUDIO_INFO_SEND;

/*
 * HDMI_CLOCK_CHANNEL_RATE enum
 */

typedef enum HDMI_CLOCK_CHANNEL_RATE {
HDMI_CLOCK_CHANNEL_FREQ_EQUAL_TO_CHAR_RATE = 0x00000000,
HDMI_CLOCK_CHANNEL_FREQ_QUARTER_TO_CHAR_RATE = 0x00000001,
} HDMI_CLOCK_CHANNEL_RATE;

/*
 * HDMI_DATA_SCRAMBLE_EN enum
 */

typedef enum HDMI_DATA_SCRAMBLE_EN {
HDMI_DATA_SCRAMBLE_DISABLE               = 0x00000000,
HDMI_DATA_SCRAMBLE_ENABLE                = 0x00000001,
} HDMI_DATA_SCRAMBLE_EN;

/*
 * HDMI_DEEP_COLOR_DEPTH enum
 */

typedef enum HDMI_DEEP_COLOR_DEPTH {
HDMI_DEEP_COLOR_DEPTH_24BPP              = 0x00000000,
HDMI_DEEP_COLOR_DEPTH_30BPP              = 0x00000001,
HDMI_DEEP_COLOR_DEPTH_36BPP              = 0x00000002,
HDMI_DEEP_COLOR_DEPTH_48BPP              = 0x00000003,
} HDMI_DEEP_COLOR_DEPTH;

/*
 * HDMI_DEFAULT_PAHSE enum
 */

typedef enum HDMI_DEFAULT_PAHSE {
HDMI_DEFAULT_PHASE_IS_0                  = 0x00000000,
HDMI_DEFAULT_PHASE_IS_1                  = 0x00000001,
} HDMI_DEFAULT_PAHSE;

/*
 * HDMI_ERROR_ACK enum
 */

typedef enum HDMI_ERROR_ACK {
HDMI_ERROR_ACK_INT                       = 0x00000000,
HDMI_ERROR_NOT_ACK                       = 0x00000001,
} HDMI_ERROR_ACK;

/*
 * HDMI_ERROR_MASK enum
 */

typedef enum HDMI_ERROR_MASK {
HDMI_ERROR_MASK_INT                      = 0x00000000,
HDMI_ERROR_NOT_MASK                      = 0x00000001,
} HDMI_ERROR_MASK;

/*
 * HDMI_GC_AVMUTE enum
 */

typedef enum HDMI_GC_AVMUTE {
HDMI_GC_AVMUTE_SET                       = 0x00000000,
HDMI_GC_AVMUTE_UNSET                     = 0x00000001,
} HDMI_GC_AVMUTE;

/*
 * HDMI_GC_AVMUTE_CONT enum
 */

typedef enum HDMI_GC_AVMUTE_CONT {
HDMI_GC_AVMUTE_CONT_DISABLE              = 0x00000000,
HDMI_GC_AVMUTE_CONT_ENABLE               = 0x00000001,
} HDMI_GC_AVMUTE_CONT;

/*
 * HDMI_GC_CONT enum
 */

typedef enum HDMI_GC_CONT {
HDMI_GC_CONT_DISABLE                     = 0x00000000,
HDMI_GC_CONT_ENABLE                      = 0x00000001,
} HDMI_GC_CONT;

/*
 * HDMI_GC_SEND enum
 */

typedef enum HDMI_GC_SEND {
HDMI_GC_NOT_SEND                         = 0x00000000,
HDMI_GC_PKT_SEND                         = 0x00000001,
} HDMI_GC_SEND;

/*
 * HDMI_GENERIC_CONT enum
 */

typedef enum HDMI_GENERIC_CONT {
HDMI_GENERIC_CONT_DISABLE                = 0x00000000,
HDMI_GENERIC_CONT_ENABLE                 = 0x00000001,
} HDMI_GENERIC_CONT;

/*
 * HDMI_GENERIC_SEND enum
 */

typedef enum HDMI_GENERIC_SEND {
HDMI_GENERIC_NOT_SEND                    = 0x00000000,
HDMI_GENERIC_PKT_SEND                    = 0x00000001,
} HDMI_GENERIC_SEND;

/*
 * HDMI_ISRC_CONT enum
 */

typedef enum HDMI_ISRC_CONT {
HDMI_ISRC_CONT_DISABLE                   = 0x00000000,
HDMI_ISRC_CONT_ENABLE                    = 0x00000001,
} HDMI_ISRC_CONT;

/*
 * HDMI_ISRC_SEND enum
 */

typedef enum HDMI_ISRC_SEND {
HDMI_ISRC_NOT_SEND                       = 0x00000000,
HDMI_ISRC_PKT_SEND                       = 0x00000001,
} HDMI_ISRC_SEND;

/*
 * HDMI_KEEPOUT_MODE enum
 */

typedef enum HDMI_KEEPOUT_MODE {
HDMI_KEEPOUT_0_650PIX_AFTER_VSYNC        = 0x00000000,
HDMI_KEEPOUT_509_650PIX_AFTER_VSYNC      = 0x00000001,
} HDMI_KEEPOUT_MODE;

/*
 * HDMI_METADATA_ENABLE enum
 */

typedef enum HDMI_METADATA_ENABLE {
HDMI_METADATA_NOT_SEND                   = 0x00000000,
HDMI_METADATA_PKT_SEND                   = 0x00000001,
} HDMI_METADATA_ENABLE;

/*
 * HDMI_MPEG_INFO_CONT enum
 */

typedef enum HDMI_MPEG_INFO_CONT {
HDMI_MPEG_INFO_CONT_DISABLE              = 0x00000000,
HDMI_MPEG_INFO_CONT_ENABLE               = 0x00000001,
} HDMI_MPEG_INFO_CONT;

/*
 * HDMI_MPEG_INFO_SEND enum
 */

typedef enum HDMI_MPEG_INFO_SEND {
HDMI_MPEG_INFO_NOT_SEND                  = 0x00000000,
HDMI_MPEG_INFO_PKT_SEND                  = 0x00000001,
} HDMI_MPEG_INFO_SEND;

/*
 * HDMI_NO_EXTRA_NULL_PACKET_FILLED enum
 */

typedef enum HDMI_NO_EXTRA_NULL_PACKET_FILLED {
HDMI_EXTRA_NULL_PACKET_FILLED_ENABLE     = 0x00000000,
HDMI_EXTRA_NULL_PACKET_FILLED_DISABLE    = 0x00000001,
} HDMI_NO_EXTRA_NULL_PACKET_FILLED;

/*
 * HDMI_NULL_SEND enum
 */

typedef enum HDMI_NULL_SEND {
HDMI_NULL_NOT_SEND                       = 0x00000000,
HDMI_NULL_PKT_SEND                       = 0x00000001,
} HDMI_NULL_SEND;

/*
 * HDMI_PACKET_GEN_VERSION enum
 */

typedef enum HDMI_PACKET_GEN_VERSION {
HDMI_PACKET_GEN_VERSION_OLD              = 0x00000000,
HDMI_PACKET_GEN_VERSION_NEW              = 0x00000001,
} HDMI_PACKET_GEN_VERSION;

/*
 * HDMI_PACKET_LINE_REFERENCE enum
 */

typedef enum HDMI_PACKET_LINE_REFERENCE {
HDMI_PKT_LINE_REF_VSYNC                  = 0x00000000,
HDMI_PKT_LINE_REF_OTGSOF                 = 0x00000001,
} HDMI_PACKET_LINE_REFERENCE;

/*
 * HDMI_PACKING_PHASE_OVERRIDE enum
 */

typedef enum HDMI_PACKING_PHASE_OVERRIDE {
HDMI_PACKING_PHASE_SET_BY_HW             = 0x00000000,
HDMI_PACKING_PHASE_SET_BY_SW             = 0x00000001,
} HDMI_PACKING_PHASE_OVERRIDE;

/*
 * LVTMA_RANDOM_PATTERN_SEED_RAN_PAT enum
 */

typedef enum LVTMA_RANDOM_PATTERN_SEED_RAN_PAT {
LVTMA_RANDOM_PATTERN_SEED_ALL_PIXELS     = 0x00000000,
LVTMA_RANDOM_PATTERN_SEED_ONLY_DE_HIGH   = 0x00000001,
} LVTMA_RANDOM_PATTERN_SEED_RAN_PAT;

/*
 * TMDS_COLOR_FORMAT enum
 */

typedef enum TMDS_COLOR_FORMAT {
TMDS_COLOR_FORMAT__24BPP__TWIN30BPP_MSB__DUAL48BPP = 0x00000000,
TMDS_COLOR_FORMAT_TWIN30BPP_LSB          = 0x00000001,
TMDS_COLOR_FORMAT_DUAL30BPP              = 0x00000002,
TMDS_COLOR_FORMAT_RESERVED               = 0x00000003,
} TMDS_COLOR_FORMAT;

/*
 * TMDS_CTL0_DATA_INVERT enum
 */

typedef enum TMDS_CTL0_DATA_INVERT {
TMDS_CTL0_DATA_NORMAL                    = 0x00000000,
TMDS_CTL0_DATA_INVERT_EN                 = 0x00000001,
} TMDS_CTL0_DATA_INVERT;

/*
 * TMDS_CTL0_DATA_MODULATION enum
 */

typedef enum TMDS_CTL0_DATA_MODULATION {
TMDS_CTL0_DATA_MODULATION_DISABLE        = 0x00000000,
TMDS_CTL0_DATA_MODULATION_BIT0           = 0x00000001,
TMDS_CTL0_DATA_MODULATION_BIT1           = 0x00000002,
TMDS_CTL0_DATA_MODULATION_BIT2           = 0x00000003,
} TMDS_CTL0_DATA_MODULATION;

/*
 * TMDS_CTL0_DATA_SEL enum
 */

typedef enum TMDS_CTL0_DATA_SEL {
TMDS_CTL0_DATA_SEL0_RESERVED             = 0x00000000,
TMDS_CTL0_DATA_SEL1_DISPLAY_ENABLE       = 0x00000001,
TMDS_CTL0_DATA_SEL2_VSYNC                = 0x00000002,
TMDS_CTL0_DATA_SEL3_RESERVED             = 0x00000003,
TMDS_CTL0_DATA_SEL4_HSYNC                = 0x00000004,
TMDS_CTL0_DATA_SEL5_SEL7_RESERVED        = 0x00000005,
TMDS_CTL0_DATA_SEL8_RANDOM_DATA          = 0x00000006,
TMDS_CTL0_DATA_SEL9_SEL15_RANDOM_DATA    = 0x00000007,
} TMDS_CTL0_DATA_SEL;

/*
 * TMDS_CTL0_PATTERN_OUT_EN enum
 */

typedef enum TMDS_CTL0_PATTERN_OUT_EN {
TMDS_CTL0_PATTERN_OUT_DISABLE            = 0x00000000,
TMDS_CTL0_PATTERN_OUT_ENABLE             = 0x00000001,
} TMDS_CTL0_PATTERN_OUT_EN;

/*
 * TMDS_CTL1_DATA_INVERT enum
 */

typedef enum TMDS_CTL1_DATA_INVERT {
TMDS_CTL1_DATA_NORMAL                    = 0x00000000,
TMDS_CTL1_DATA_INVERT_EN                 = 0x00000001,
} TMDS_CTL1_DATA_INVERT;

/*
 * TMDS_CTL1_DATA_MODULATION enum
 */

typedef enum TMDS_CTL1_DATA_MODULATION {
TMDS_CTL1_DATA_MODULATION_DISABLE        = 0x00000000,
TMDS_CTL1_DATA_MODULATION_BIT0           = 0x00000001,
TMDS_CTL1_DATA_MODULATION_BIT1           = 0x00000002,
TMDS_CTL1_DATA_MODULATION_BIT2           = 0x00000003,
} TMDS_CTL1_DATA_MODULATION;

/*
 * TMDS_CTL1_DATA_SEL enum
 */

typedef enum TMDS_CTL1_DATA_SEL {
TMDS_CTL1_DATA_SEL0_RESERVED             = 0x00000000,
TMDS_CTL1_DATA_SEL1_DISPLAY_ENABLE       = 0x00000001,
TMDS_CTL1_DATA_SEL2_VSYNC                = 0x00000002,
TMDS_CTL1_DATA_SEL3_RESERVED             = 0x00000003,
TMDS_CTL1_DATA_SEL4_HSYNC                = 0x00000004,
TMDS_CTL1_DATA_SEL5_SEL7_RESERVED        = 0x00000005,
TMDS_CTL1_DATA_SEL8_BLANK_TIME           = 0x00000006,
TMDS_CTL1_DATA_SEL9_SEL15_RESERVED       = 0x00000007,
} TMDS_CTL1_DATA_SEL;

/*
 * TMDS_CTL1_PATTERN_OUT_EN enum
 */

typedef enum TMDS_CTL1_PATTERN_OUT_EN {
TMDS_CTL1_PATTERN_OUT_DISABLE            = 0x00000000,
TMDS_CTL1_PATTERN_OUT_ENABLE             = 0x00000001,
} TMDS_CTL1_PATTERN_OUT_EN;

/*
 * TMDS_CTL2_DATA_INVERT enum
 */

typedef enum TMDS_CTL2_DATA_INVERT {
TMDS_CTL2_DATA_NORMAL                    = 0x00000000,
TMDS_CTL2_DATA_INVERT_EN                 = 0x00000001,
} TMDS_CTL2_DATA_INVERT;

/*
 * TMDS_CTL2_DATA_MODULATION enum
 */

typedef enum TMDS_CTL2_DATA_MODULATION {
TMDS_CTL2_DATA_MODULATION_DISABLE        = 0x00000000,
TMDS_CTL2_DATA_MODULATION_BIT0           = 0x00000001,
TMDS_CTL2_DATA_MODULATION_BIT1           = 0x00000002,
TMDS_CTL2_DATA_MODULATION_BIT2           = 0x00000003,
} TMDS_CTL2_DATA_MODULATION;

/*
 * TMDS_CTL2_DATA_SEL enum
 */

typedef enum TMDS_CTL2_DATA_SEL {
TMDS_CTL2_DATA_SEL0_RESERVED             = 0x00000000,
TMDS_CTL2_DATA_SEL1_DISPLAY_ENABLE       = 0x00000001,
TMDS_CTL2_DATA_SEL2_VSYNC                = 0x00000002,
TMDS_CTL2_DATA_SEL3_RESERVED             = 0x00000003,
TMDS_CTL2_DATA_SEL4_HSYNC                = 0x00000004,
TMDS_CTL2_DATA_SEL5_SEL7_RESERVED        = 0x00000005,
TMDS_CTL2_DATA_SEL8_BLANK_TIME           = 0x00000006,
TMDS_CTL2_DATA_SEL9_SEL15_RESERVED       = 0x00000007,
} TMDS_CTL2_DATA_SEL;

/*
 * TMDS_CTL2_PATTERN_OUT_EN enum
 */

typedef enum TMDS_CTL2_PATTERN_OUT_EN {
TMDS_CTL2_PATTERN_OUT_DISABLE            = 0x00000000,
TMDS_CTL2_PATTERN_OUT_ENABLE             = 0x00000001,
} TMDS_CTL2_PATTERN_OUT_EN;

/*
 * TMDS_CTL3_DATA_INVERT enum
 */

typedef enum TMDS_CTL3_DATA_INVERT {
TMDS_CTL3_DATA_NORMAL                    = 0x00000000,
TMDS_CTL3_DATA_INVERT_EN                 = 0x00000001,
} TMDS_CTL3_DATA_INVERT;

/*
 * TMDS_CTL3_DATA_MODULATION enum
 */

typedef enum TMDS_CTL3_DATA_MODULATION {
TMDS_CTL3_DATA_MODULATION_DISABLE        = 0x00000000,
TMDS_CTL3_DATA_MODULATION_BIT0           = 0x00000001,
TMDS_CTL3_DATA_MODULATION_BIT1           = 0x00000002,
TMDS_CTL3_DATA_MODULATION_BIT2           = 0x00000003,
} TMDS_CTL3_DATA_MODULATION;

/*
 * TMDS_CTL3_DATA_SEL enum
 */

typedef enum TMDS_CTL3_DATA_SEL {
TMDS_CTL3_DATA_SEL0_RESERVED             = 0x00000000,
TMDS_CTL3_DATA_SEL1_DISPLAY_ENABLE       = 0x00000001,
TMDS_CTL3_DATA_SEL2_VSYNC                = 0x00000002,
TMDS_CTL3_DATA_SEL3_RESERVED             = 0x00000003,
TMDS_CTL3_DATA_SEL4_HSYNC                = 0x00000004,
TMDS_CTL3_DATA_SEL5_SEL7_RESERVED        = 0x00000005,
TMDS_CTL3_DATA_SEL8_BLANK_TIME           = 0x00000006,
TMDS_CTL3_DATA_SEL9_SEL15_RESERVED       = 0x00000007,
} TMDS_CTL3_DATA_SEL;

/*
 * TMDS_CTL3_PATTERN_OUT_EN enum
 */

typedef enum TMDS_CTL3_PATTERN_OUT_EN {
TMDS_CTL3_PATTERN_OUT_DISABLE            = 0x00000000,
TMDS_CTL3_PATTERN_OUT_ENABLE             = 0x00000001,
} TMDS_CTL3_PATTERN_OUT_EN;

/*
 * TMDS_DATA_SYNCHRONIZATION_DSINTSEL enum
 */

typedef enum TMDS_DATA_SYNCHRONIZATION_DSINTSEL {
TMDS_DATA_SYNCHRONIZATION_DSINTSEL_PCLK_TMDS = 0x00000000,
TMDS_DATA_SYNCHRONIZATION_DSINTSEL_TMDS_PLL = 0x00000001,
} TMDS_DATA_SYNCHRONIZATION_DSINTSEL;

/*
 * TMDS_PIXEL_ENCODING enum
 */

typedef enum TMDS_PIXEL_ENCODING {
TMDS_PIXEL_ENCODING_444_OR_420           = 0x00000000,
TMDS_PIXEL_ENCODING_422                  = 0x00000001,
} TMDS_PIXEL_ENCODING;

/*
 * TMDS_REG_TEST_OUTPUTA_CNTLA enum
 */

typedef enum TMDS_REG_TEST_OUTPUTA_CNTLA {
TMDS_REG_TEST_OUTPUTA_CNTLA_OTDATA0      = 0x00000000,
TMDS_REG_TEST_OUTPUTA_CNTLA_OTDATA1      = 0x00000001,
TMDS_REG_TEST_OUTPUTA_CNTLA_OTDATA2      = 0x00000002,
TMDS_REG_TEST_OUTPUTA_CNTLA_NA           = 0x00000003,
} TMDS_REG_TEST_OUTPUTA_CNTLA;

/*
 * TMDS_REG_TEST_OUTPUTB_CNTLB enum
 */

typedef enum TMDS_REG_TEST_OUTPUTB_CNTLB {
TMDS_REG_TEST_OUTPUTB_CNTLB_OTDATB0      = 0x00000000,
TMDS_REG_TEST_OUTPUTB_CNTLB_OTDATB1      = 0x00000001,
TMDS_REG_TEST_OUTPUTB_CNTLB_OTDATB2      = 0x00000002,
TMDS_REG_TEST_OUTPUTB_CNTLB_NA           = 0x00000003,
} TMDS_REG_TEST_OUTPUTB_CNTLB;

/*
 * TMDS_STEREOSYNC_CTL_SEL_REG enum
 */

typedef enum TMDS_STEREOSYNC_CTL_SEL_REG {
TMDS_STEREOSYNC_CTL0                     = 0x00000000,
TMDS_STEREOSYNC_CTL1                     = 0x00000001,
TMDS_STEREOSYNC_CTL2                     = 0x00000002,
TMDS_STEREOSYNC_CTL3                     = 0x00000003,
} TMDS_STEREOSYNC_CTL_SEL_REG;

/*
 * TMDS_SYNC_PHASE enum
 */

typedef enum TMDS_SYNC_PHASE {
TMDS_NOT_SYNC_PHASE_ON_FRAME_START       = 0x00000000,
TMDS_SYNC_PHASE_ON_FRAME_START           = 0x00000001,
} TMDS_SYNC_PHASE;

/*
 * TMDS_TRANSMITTER_CONTROL_BYPASS_PLLA enum
 */

typedef enum TMDS_TRANSMITTER_CONTROL_BYPASS_PLLA {
TMDS_TRANSMITTER_BYPASS_PLLA_COHERENT    = 0x00000000,
TMDS_TRANSMITTER_BYPASS_PLLA_INCOHERENT  = 0x00000001,
} TMDS_TRANSMITTER_CONTROL_BYPASS_PLLA;

/*
 * TMDS_TRANSMITTER_CONTROL_BYPASS_PLLB enum
 */

typedef enum TMDS_TRANSMITTER_CONTROL_BYPASS_PLLB {
TMDS_TRANSMITTER_BYPASS_PLLB_COHERENT    = 0x00000000,
TMDS_TRANSMITTER_BYPASS_PLLB_INCOHERENT  = 0x00000001,
} TMDS_TRANSMITTER_CONTROL_BYPASS_PLLB;

/*
 * TMDS_TRANSMITTER_CONTROL_IDSCKSELA enum
 */

typedef enum TMDS_TRANSMITTER_CONTROL_IDSCKSELA {
TMDS_TRANSMITTER_IDSCKSELA_USE_IPIXCLK   = 0x00000000,
TMDS_TRANSMITTER_IDSCKSELA_USE_IDCLK     = 0x00000001,
} TMDS_TRANSMITTER_CONTROL_IDSCKSELA;

/*
 * TMDS_TRANSMITTER_CONTROL_IDSCKSELB enum
 */

typedef enum TMDS_TRANSMITTER_CONTROL_IDSCKSELB {
TMDS_TRANSMITTER_IDSCKSELB_USE_IPIXCLK   = 0x00000000,
TMDS_TRANSMITTER_IDSCKSELB_USE_IDCLK     = 0x00000001,
} TMDS_TRANSMITTER_CONTROL_IDSCKSELB;

/*
 * TMDS_TRANSMITTER_CONTROL_PLLSEL_OVERWRITE_EN enum
 */

typedef enum TMDS_TRANSMITTER_CONTROL_PLLSEL_OVERWRITE_EN {
TMDS_TRANSMITTER_PLLSEL_BY_HW            = 0x00000000,
TMDS_TRANSMITTER_PLLSEL_OVERWRITE_BY_SW  = 0x00000001,
} TMDS_TRANSMITTER_CONTROL_PLLSEL_OVERWRITE_EN;

/*
 * TMDS_TRANSMITTER_CONTROL_PLL_ENABLE_HPD_MASK enum
 */

typedef enum TMDS_TRANSMITTER_CONTROL_PLL_ENABLE_HPD_MASK {
TMDS_TRANSMITTER_HPD_NOT_OVERRIDE_PLL_ENABLE = 0x00000000,
TMDS_TRANSMITTER_HPD_OVERRIDE_PLL_ENABLE_ON_DISCON = 0x00000001,
TMDS_TRANSMITTER_HPD_OVERRIDE_PLL_ENABLE_ON_CON = 0x00000002,
TMDS_TRANSMITTER_HPD_OVERRIDE_PLL_ENABLE = 0x00000003,
} TMDS_TRANSMITTER_CONTROL_PLL_ENABLE_HPD_MASK;

/*
 * TMDS_TRANSMITTER_CONTROL_PLL_PWRUP_SEQ_EN enum
 */

typedef enum TMDS_TRANSMITTER_CONTROL_PLL_PWRUP_SEQ_EN {
TMDS_TRANSMITTER_PLL_PWRUP_SEQ_DISABLE   = 0x00000000,
TMDS_TRANSMITTER_PLL_PWRUP_SEQ_ENABLE    = 0x00000001,
} TMDS_TRANSMITTER_CONTROL_PLL_PWRUP_SEQ_EN;

/*
 * TMDS_TRANSMITTER_CONTROL_PLL_RESET_HPD_MASK enum
 */

typedef enum TMDS_TRANSMITTER_CONTROL_PLL_RESET_HPD_MASK {
TMDS_TRANSMITTER_PLL_NOT_RST_ON_HPD      = 0x00000000,
TMDS_TRANSMITTER_PLL_RST_ON_HPD          = 0x00000001,
} TMDS_TRANSMITTER_CONTROL_PLL_RESET_HPD_MASK;

/*
 * TMDS_TRANSMITTER_CONTROL_TDCLK_FROM_PADS enum
 */

typedef enum TMDS_TRANSMITTER_CONTROL_TDCLK_FROM_PADS {
TMDS_TRANSMITTER_TDCLK_FROM_TMDS_TDCLK   = 0x00000000,
TMDS_TRANSMITTER_TDCLK_FROM_PADS         = 0x00000001,
} TMDS_TRANSMITTER_CONTROL_TDCLK_FROM_PADS;

/*
 * TMDS_TRANSMITTER_CONTROL_TMCLK_FROM_PADS enum
 */

typedef enum TMDS_TRANSMITTER_CONTROL_TMCLK_FROM_PADS {
TMDS_TRANSMITTER_TMCLK_FROM_TMDS_TMCLK   = 0x00000000,
TMDS_TRANSMITTER_TMCLK_FROM_PADS         = 0x00000001,
} TMDS_TRANSMITTER_CONTROL_TMCLK_FROM_PADS;

/*
 * TMDS_TRANSMITTER_ENABLE_HPD_MASK enum
 */

typedef enum TMDS_TRANSMITTER_ENABLE_HPD_MASK {
TMDS_TRANSMITTER_HPD_MASK_NOT_OVERRIDE   = 0x00000000,
TMDS_TRANSMITTER_HPD_MASK_OVERRIDE       = 0x00000001,
} TMDS_TRANSMITTER_ENABLE_HPD_MASK;

/*
 * TMDS_TRANSMITTER_ENABLE_LNKCEN_HPD_MASK enum
 */

typedef enum TMDS_TRANSMITTER_ENABLE_LNKCEN_HPD_MASK {
TMDS_TRANSMITTER_LNKCEN_HPD_MASK_NOT_OVERRIDE = 0x00000000,
TMDS_TRANSMITTER_LNKCEN_HPD_MASK_OVERRIDE = 0x00000001,
} TMDS_TRANSMITTER_ENABLE_LNKCEN_HPD_MASK;

/*
 * TMDS_TRANSMITTER_ENABLE_LNKDEN_HPD_MASK enum
 */

typedef enum TMDS_TRANSMITTER_ENABLE_LNKDEN_HPD_MASK {
TMDS_TRANSMITTER_LNKDEN_HPD_MASK_NOT_OVERRIDE = 0x00000000,
TMDS_TRANSMITTER_LNKDEN_HPD_MASK_OVERRIDE = 0x00000001,
} TMDS_TRANSMITTER_ENABLE_LNKDEN_HPD_MASK;

/*******************************************************
 * DP_AUX Enums
 *******************************************************/

/*
 * DP_AUX_ARB_CONTROL_ARB_PRIORITY enum
 */

typedef enum DP_AUX_ARB_CONTROL_ARB_PRIORITY {
DP_AUX_ARB_CONTROL_ARB_PRIORITY__GTC_LS_SW = 0x00000000,
DP_AUX_ARB_CONTROL_ARB_PRIORITY__LS_GTC_SW = 0x00000001,
DP_AUX_ARB_CONTROL_ARB_PRIORITY__SW_LS_GTC = 0x00000002,
DP_AUX_ARB_CONTROL_ARB_PRIORITY__SW_GTC_LS = 0x00000003,
} DP_AUX_ARB_CONTROL_ARB_PRIORITY;

/*
 * DP_AUX_ARB_CONTROL_DONE_USING_AUX_REG enum
 */

typedef enum DP_AUX_ARB_CONTROL_DONE_USING_AUX_REG {
DP_AUX_ARB_CONTROL__DONE_NOT_USING_AUX_REG = 0x00000000,
DP_AUX_ARB_CONTROL__DONE_USING_AUX_REG   = 0x00000001,
} DP_AUX_ARB_CONTROL_DONE_USING_AUX_REG;

/*
 * DP_AUX_ARB_CONTROL_USE_AUX_REG_REQ enum
 */

typedef enum DP_AUX_ARB_CONTROL_USE_AUX_REG_REQ {
DP_AUX_ARB_CONTROL__NOT_USE_AUX_REG_REQ  = 0x00000000,
DP_AUX_ARB_CONTROL__USE_AUX_REG_REQ      = 0x00000001,
} DP_AUX_ARB_CONTROL_USE_AUX_REG_REQ;

/*
 * DP_AUX_ARB_STATUS enum
 */

typedef enum DP_AUX_ARB_STATUS {
DP_AUX_IDLE                              = 0x00000000,
DP_AUX_IN_USE_LS                         = 0x00000001,
DP_AUX_IN_USE_GTC                        = 0x00000002,
DP_AUX_IN_USE_SW                         = 0x00000003,
DP_AUX_IN_USE_PHYWAKE                    = 0x00000004,
} DP_AUX_ARB_STATUS;

/*
 * DP_AUX_CONTROL_HPD_SEL enum
 */

typedef enum DP_AUX_CONTROL_HPD_SEL {
DP_AUX_CONTROL_HPD1_SELECTED             = 0x00000000,
DP_AUX_CONTROL_HPD2_SELECTED             = 0x00000001,
DP_AUX_CONTROL_HPD3_SELECTED             = 0x00000002,
DP_AUX_CONTROL_HPD4_SELECTED             = 0x00000003,
DP_AUX_CONTROL_HPD5_SELECTED             = 0x00000004,
DP_AUX_CONTROL_NO_HPD_SELECTED           = 0x00000005,
} DP_AUX_CONTROL_HPD_SEL;

/*
 * DP_AUX_CONTROL_TEST_MODE enum
 */

typedef enum DP_AUX_CONTROL_TEST_MODE {
DP_AUX_CONTROL_TEST_MODE_DISABLE         = 0x00000000,
DP_AUX_CONTROL_TEST_MODE_ENABLE          = 0x00000001,
} DP_AUX_CONTROL_TEST_MODE;

/*
 * DP_AUX_DEFINITE_ERR_REACHED_ACK enum
 */

typedef enum DP_AUX_DEFINITE_ERR_REACHED_ACK {
ALPHA_DP_AUX_DEFINITE_ERR_REACHED_NOT_ACK = 0x00000000,
ALPHA_DP_AUX_DEFINITE_ERR_REACHED_ACK    = 0x00000001,
} DP_AUX_DEFINITE_ERR_REACHED_ACK;

/*
 * DP_AUX_DPHY_RX_CONTROL_ALLOW_BELOW_THRESHOLD_PHASE_DETECT enum
 */

typedef enum DP_AUX_DPHY_RX_CONTROL_ALLOW_BELOW_THRESHOLD_PHASE_DETECT {
DP_AUX_DPHY_RX_CONTROL__NOT_ALLOW_BELOW_THRESHOLD_PHASE_DETECT = 0x00000000,
DP_AUX_DPHY_RX_CONTROL__ALLOW_BELOW_THRESHOLD_PHASE_DETECT = 0x00000001,
} DP_AUX_DPHY_RX_CONTROL_ALLOW_BELOW_THRESHOLD_PHASE_DETECT;

/*
 * DP_AUX_DPHY_RX_CONTROL_ALLOW_BELOW_THRESHOLD_START enum
 */

typedef enum DP_AUX_DPHY_RX_CONTROL_ALLOW_BELOW_THRESHOLD_START {
DP_AUX_DPHY_RX_CONTROL__NOT_ALLOW_BELOW_THRESHOLD_START = 0x00000000,
DP_AUX_DPHY_RX_CONTROL__ALLOW_BELOW_THRESHOLD_START = 0x00000001,
} DP_AUX_DPHY_RX_CONTROL_ALLOW_BELOW_THRESHOLD_START;

/*
 * DP_AUX_DPHY_RX_CONTROL_ALLOW_BELOW_THRESHOLD_STOP enum
 */

typedef enum DP_AUX_DPHY_RX_CONTROL_ALLOW_BELOW_THRESHOLD_STOP {
DP_AUX_DPHY_RX_CONTROL__NOT_ALLOW_BELOW_THRESHOLD_STOP = 0x00000000,
DP_AUX_DPHY_RX_CONTROL__ALLOW_BELOW_THRESHOLD_STOP = 0x00000001,
} DP_AUX_DPHY_RX_CONTROL_ALLOW_BELOW_THRESHOLD_STOP;

/*
 * DP_AUX_DPHY_RX_CONTROL_HALF_SYM_DETECT_LEN enum
 */

typedef enum DP_AUX_DPHY_RX_CONTROL_HALF_SYM_DETECT_LEN {
DP_AUX_DPHY_RX_CONTROL_HALF_SYM_DETECT_LEN__6_EDGES = 0x00000000,
DP_AUX_DPHY_RX_CONTROL_HALF_SYM_DETECT_LEN__10_EDGES = 0x00000001,
DP_AUX_DPHY_RX_CONTROL_HALF_SYM_DETECT_LEN__18_EDGES = 0x00000002,
DP_AUX_DPHY_RX_CONTROL_HALF_SYM_DETECT_LEN__RESERVED = 0x00000003,
} DP_AUX_DPHY_RX_CONTROL_HALF_SYM_DETECT_LEN;

/*
 * DP_AUX_DPHY_RX_CONTROL_PHASE_DETECT_LEN enum
 */

typedef enum DP_AUX_DPHY_RX_CONTROL_PHASE_DETECT_LEN {
DP_AUX_DPHY_RX_CONTROL_PHASE_DETECT_LEN__2_HALF_SYMBOLS = 0x00000000,
DP_AUX_DPHY_RX_CONTROL_PHASE_DETECT_LEN__4_HALF_SYMBOLS = 0x00000001,
DP_AUX_DPHY_RX_CONTROL_PHASE_DETECT_LEN__6_HALF_SYMBOLS = 0x00000002,
DP_AUX_DPHY_RX_CONTROL_PHASE_DETECT_LEN__8_HALF_SYMBOLS = 0x00000003,
} DP_AUX_DPHY_RX_CONTROL_PHASE_DETECT_LEN;

/*
 * DP_AUX_DPHY_RX_CONTROL_RECEIVE_WINDOW enum
 */

typedef enum DP_AUX_DPHY_RX_CONTROL_RECEIVE_WINDOW {
DP_AUX_DPHY_RX_CONTROL_RECEIVE_WINDOW__1TO2_PERIOD = 0x00000000,
DP_AUX_DPHY_RX_CONTROL_RECEIVE_WINDOW__1TO4_PERIOD = 0x00000001,
DP_AUX_DPHY_RX_CONTROL_RECEIVE_WINDOW__1TO8_PERIOD = 0x00000002,
DP_AUX_DPHY_RX_CONTROL_RECEIVE_WINDOW__1TO16_PERIOD = 0x00000003,
DP_AUX_DPHY_RX_CONTROL_RECEIVE_WINDOW__1TO32_PERIOD = 0x00000004,
DP_AUX_DPHY_RX_CONTROL_RECEIVE_WINDOW__1TO64_PERIOD = 0x00000005,
DP_AUX_DPHY_RX_CONTROL_RECEIVE_WINDOW__1TO128_PERIOD = 0x00000006,
DP_AUX_DPHY_RX_CONTROL_RECEIVE_WINDOW__1TO256_PERIOD = 0x00000007,
} DP_AUX_DPHY_RX_CONTROL_RECEIVE_WINDOW;

/*
 * DP_AUX_DPHY_RX_CONTROL_START_WINDOW enum
 */

typedef enum DP_AUX_DPHY_RX_CONTROL_START_WINDOW {
DP_AUX_DPHY_RX_CONTROL_START_WINDOW__1TO2_PERIOD = 0x00000000,
DP_AUX_DPHY_RX_CONTROL_START_WINDOW__1TO4_PERIOD = 0x00000001,
DP_AUX_DPHY_RX_CONTROL_START_WINDOW__1TO8_PERIOD = 0x00000002,
DP_AUX_DPHY_RX_CONTROL_START_WINDOW__1TO16_PERIOD = 0x00000003,
DP_AUX_DPHY_RX_CONTROL_START_WINDOW__1TO32_PERIOD = 0x00000004,
DP_AUX_DPHY_RX_CONTROL_START_WINDOW__1TO64_PERIOD = 0x00000005,
DP_AUX_DPHY_RX_CONTROL_START_WINDOW__1TO128_PERIOD = 0x00000006,
DP_AUX_DPHY_RX_CONTROL_START_WINDOW__1TO256_PERIOD = 0x00000007,
} DP_AUX_DPHY_RX_CONTROL_START_WINDOW;

/*
 * DP_AUX_DPHY_RX_DETECTION_THRESHOLD enum
 */

typedef enum DP_AUX_DPHY_RX_DETECTION_THRESHOLD {
DP_AUX_DPHY_RX_DETECTION_THRESHOLD__1to2 = 0x00000000,
DP_AUX_DPHY_RX_DETECTION_THRESHOLD__3to4 = 0x00000001,
DP_AUX_DPHY_RX_DETECTION_THRESHOLD__7to8 = 0x00000002,
DP_AUX_DPHY_RX_DETECTION_THRESHOLD__15to16 = 0x00000003,
DP_AUX_DPHY_RX_DETECTION_THRESHOLD__31to32 = 0x00000004,
DP_AUX_DPHY_RX_DETECTION_THRESHOLD__63to64 = 0x00000005,
DP_AUX_DPHY_RX_DETECTION_THRESHOLD__127to128 = 0x00000006,
DP_AUX_DPHY_RX_DETECTION_THRESHOLD__255to256 = 0x00000007,
} DP_AUX_DPHY_RX_DETECTION_THRESHOLD;

/*
 * DP_AUX_DPHY_TX_CONTROL_MODE_DET_CHECK_DELAY enum
 */

typedef enum DP_AUX_DPHY_TX_CONTROL_MODE_DET_CHECK_DELAY {
DP_AUX_DPHY_TX_CONTROL_MODE_DET_CHECK_DELAY__0 = 0x00000000,
DP_AUX_DPHY_TX_CONTROL_MODE_DET_CHECK_DELAY__16US = 0x00000001,
DP_AUX_DPHY_TX_CONTROL_MODE_DET_CHECK_DELAY__32US = 0x00000002,
DP_AUX_DPHY_TX_CONTROL_MODE_DET_CHECK_DELAY__64US = 0x00000003,
DP_AUX_DPHY_TX_CONTROL_MODE_DET_CHECK_DELAY__128US = 0x00000004,
DP_AUX_DPHY_TX_CONTROL_MODE_DET_CHECK_DELAY__256US = 0x00000005,
} DP_AUX_DPHY_TX_CONTROL_MODE_DET_CHECK_DELAY;

/*
 * DP_AUX_DPHY_TX_REF_CONTROL_TX_RATE enum
 */

typedef enum DP_AUX_DPHY_TX_REF_CONTROL_TX_RATE {
DP_AUX_DPHY_TX_REF_CONTROL_TX_RATE__1MHZ = 0x00000000,
DP_AUX_DPHY_TX_REF_CONTROL_TX_RATE__2MHZ = 0x00000001,
DP_AUX_DPHY_TX_REF_CONTROL_TX_RATE__4MHZ = 0x00000002,
DP_AUX_DPHY_TX_REF_CONTROL_TX_RATE__8MHZ = 0x00000003,
} DP_AUX_DPHY_TX_REF_CONTROL_TX_RATE;

/*
 * DP_AUX_DPHY_TX_REF_CONTROL_TX_REF_SEL enum
 */

typedef enum DP_AUX_DPHY_TX_REF_CONTROL_TX_REF_SEL {
DP_AUX_DPHY_TX_REF_CONTROL_TX_REF_SEL__DIVIDED_SYM_CLK = 0x00000000,
DP_AUX_DPHY_TX_REF_CONTROL_TX_REF_SEL__FROM_DCCG_MICROSECOND_REF = 0x00000001,
} DP_AUX_DPHY_TX_REF_CONTROL_TX_REF_SEL;

/*
 * DP_AUX_ERR_OCCURRED_ACK enum
 */

typedef enum DP_AUX_ERR_OCCURRED_ACK {
DP_AUX_ERR_OCCURRED__NOT_ACK             = 0x00000000,
DP_AUX_ERR_OCCURRED__ACK                 = 0x00000001,
} DP_AUX_ERR_OCCURRED_ACK;

/*
 * DP_AUX_GTC_SYNC_CONTROL_GTC_SYNC_BLOCK_REQ enum
 */

typedef enum DP_AUX_GTC_SYNC_CONTROL_GTC_SYNC_BLOCK_REQ {
DP_AUX_GTC_SYNC_CONTROL_GTC_SYNC_ALLOW_REQ_FROM_OTHER_AUX = 0x00000000,
DP_AUX_GTC_SYNC_CONTROL_GTC_SYNC_BLOCK_REQ_FROM_OTHER_AUX = 0x00000001,
} DP_AUX_GTC_SYNC_CONTROL_GTC_SYNC_BLOCK_REQ;

/*
 * DP_AUX_GTC_SYNC_CONTROL_INTERVAL_RESET_WINDOW enum
 */

typedef enum DP_AUX_GTC_SYNC_CONTROL_INTERVAL_RESET_WINDOW {
DP_AUX_GTC_SYNC_CONTROL_INTERVAL_RESET_WINDOW__300US = 0x00000000,
DP_AUX_GTC_SYNC_CONTROL_INTERVAL_RESET_WINDOW__400US = 0x00000001,
DP_AUX_GTC_SYNC_CONTROL_INTERVAL_RESET_WINDOW__500US = 0x00000002,
DP_AUX_GTC_SYNC_CONTROL_INTERVAL_RESET_WINDOW__600US = 0x00000003,
} DP_AUX_GTC_SYNC_CONTROL_INTERVAL_RESET_WINDOW;

/*
 * DP_AUX_GTC_SYNC_CONTROL_OFFSET_CALC_MAX_ATTEMPT enum
 */

typedef enum DP_AUX_GTC_SYNC_CONTROL_OFFSET_CALC_MAX_ATTEMPT {
DP_AUX_GTC_SYNC_CONTROL_OFFSET_CALC_MAX_ATTEMPT__4_ATTAMPS = 0x00000000,
DP_AUX_GTC_SYNC_CONTROL_OFFSET_CALC_MAX_ATTEMPT__8_ATTAMPS = 0x00000001,
DP_AUX_GTC_SYNC_CONTROL_OFFSET_CALC_MAX_ATTEMPT__16_ATTAMPS = 0x00000002,
DP_AUX_GTC_SYNC_CONTROL_OFFSET_CALC_MAX_ATTEMPT__RESERVED = 0x00000003,
} DP_AUX_GTC_SYNC_CONTROL_OFFSET_CALC_MAX_ATTEMPT;

/*
 * DP_AUX_GTC_SYNC_ERROR_CONTROL_LOCK_ACQ_TIMEOUT_LEN enum
 */

typedef enum DP_AUX_GTC_SYNC_ERROR_CONTROL_LOCK_ACQ_TIMEOUT_LEN {
DP_AUX_GTC_SYNC_ERROR_CONTROL_LOCK_ACQ_TIMEOUT_LEN__0 = 0x00000000,
DP_AUX_GTC_SYNC_ERROR_CONTROL_LOCK_ACQ_TIMEOUT_LEN__64 = 0x00000001,
DP_AUX_GTC_SYNC_ERROR_CONTROL_LOCK_ACQ_TIMEOUT_LEN__128 = 0x00000002,
DP_AUX_GTC_SYNC_ERROR_CONTROL_LOCK_ACQ_TIMEOUT_LEN__256 = 0x00000003,
} DP_AUX_GTC_SYNC_ERROR_CONTROL_LOCK_ACQ_TIMEOUT_LEN;

/*
 * DP_AUX_INT_ACK enum
 */

typedef enum DP_AUX_INT_ACK {
DP_AUX_INT__NOT_ACK                      = 0x00000000,
DP_AUX_INT__ACK                          = 0x00000001,
} DP_AUX_INT_ACK;

/*
 * DP_AUX_LS_UPDATE_ACK enum
 */

typedef enum DP_AUX_LS_UPDATE_ACK {
DP_AUX_INT_LS_UPDATE_NOT_ACK             = 0x00000000,
DP_AUX_INT_LS_UPDATE_ACK                 = 0x00000001,
} DP_AUX_LS_UPDATE_ACK;

/*
 * DP_AUX_PHY_WAKE_PRIORITY enum
 */

typedef enum DP_AUX_PHY_WAKE_PRIORITY {
DP_AUX_PHY_WAKE_HIGH_PRIORITY            = 0x00000000,
DP_AUX_PHY_WAKE_LOW_PRIORITY             = 0x00000001,
} DP_AUX_PHY_WAKE_PRIORITY;

/*
 * DP_AUX_POTENTIAL_ERR_REACHED_ACK enum
 */

typedef enum DP_AUX_POTENTIAL_ERR_REACHED_ACK {
DP_AUX_POTENTIAL_ERR_REACHED__NOT_ACK    = 0x00000000,
DP_AUX_POTENTIAL_ERR_REACHED__ACK        = 0x00000001,
} DP_AUX_POTENTIAL_ERR_REACHED_ACK;

/*
 * DP_AUX_RESET enum
 */

typedef enum DP_AUX_RESET {
DP_AUX_RESET_DEASSERTED                  = 0x00000000,
DP_AUX_RESET_ASSERTED                    = 0x00000001,
} DP_AUX_RESET;

/*
 * DP_AUX_RESET_DONE enum
 */

typedef enum DP_AUX_RESET_DONE {
DP_AUX_RESET_SEQUENCE_NOT_DONE           = 0x00000000,
DP_AUX_RESET_SEQUENCE_DONE               = 0x00000001,
} DP_AUX_RESET_DONE;

/*
 * DP_AUX_RX_TIMEOUT_LEN_MUL enum
 */

typedef enum DP_AUX_RX_TIMEOUT_LEN_MUL {
DP_AUX_RX_TIMEOUT_LEN_NO_MUL             = 0x00000000,
DP_AUX_RX_TIMEOUT_LEN_MUL_2              = 0x00000001,
DP_AUX_RX_TIMEOUT_LEN_MUL_4              = 0x00000002,
DP_AUX_RX_TIMEOUT_LEN_MUL_8              = 0x00000003,
} DP_AUX_RX_TIMEOUT_LEN_MUL;

/*
 * DP_AUX_SW_CONTROL_LS_READ_TRIG enum
 */

typedef enum DP_AUX_SW_CONTROL_LS_READ_TRIG {
DP_AUX_SW_CONTROL_LS_READ__NOT_TRIG      = 0x00000000,
DP_AUX_SW_CONTROL_LS_READ__TRIG          = 0x00000001,
} DP_AUX_SW_CONTROL_LS_READ_TRIG;

/*
 * DP_AUX_SW_CONTROL_SW_GO enum
 */

typedef enum DP_AUX_SW_CONTROL_SW_GO {
DP_AUX_SW_CONTROL_SW__NOT_GO             = 0x00000000,
DP_AUX_SW_CONTROL_SW__GO                 = 0x00000001,
} DP_AUX_SW_CONTROL_SW_GO;

/*
 * DP_AUX_TX_PRECHARGE_LEN_MUL enum
 */

typedef enum DP_AUX_TX_PRECHARGE_LEN_MUL {
DP_AUX_TX_PRECHARGE_LEN_NO_MUL           = 0x00000000,
DP_AUX_TX_PRECHARGE_LEN_MUL_2            = 0x00000001,
DP_AUX_TX_PRECHARGE_LEN_MUL_4            = 0x00000002,
DP_AUX_TX_PRECHARGE_LEN_MUL_8            = 0x00000003,
} DP_AUX_TX_PRECHARGE_LEN_MUL;

/*******************************************************
 * DOUT_I2C Enums
 *******************************************************/

/*
 * DOUT_I2C_ACK enum
 */

typedef enum DOUT_I2C_ACK {
DOUT_I2C_NO_ACK                          = 0x00000000,
DOUT_I2C_ACK_TO_CLEAN                    = 0x00000001,
} DOUT_I2C_ACK;

/*
 * DOUT_I2C_ARBITRATION_ABORT_XFER enum
 */

typedef enum DOUT_I2C_ARBITRATION_ABORT_XFER {
DOUT_I2C_ARBITRATION_NOT_ABORT_CURRENT_TRANSFER = 0x00000000,
DOUT_I2C_ARBITRATION_ABORT_CURRENT_TRANSFER = 0x00000001,
} DOUT_I2C_ARBITRATION_ABORT_XFER;

/*
 * DOUT_I2C_ARBITRATION_DONE_USING_I2C_REG enum
 */

typedef enum DOUT_I2C_ARBITRATION_DONE_USING_I2C_REG {
DOUT_I2C_ARBITRATION_DONE__NOT_USING_I2C_REG = 0x00000000,
DOUT_I2C_ARBITRATION_DONE__USING_I2C_REG = 0x00000001,
} DOUT_I2C_ARBITRATION_DONE_USING_I2C_REG;

/*
 * DOUT_I2C_ARBITRATION_NO_QUEUED_SW_GO enum
 */

typedef enum DOUT_I2C_ARBITRATION_NO_QUEUED_SW_GO {
DOUT_I2C_ARBITRATION_SW_QUEUE_ENABLED    = 0x00000000,
DOUT_I2C_ARBITRATION_SW_QUEUE_DISABLED   = 0x00000001,
} DOUT_I2C_ARBITRATION_NO_QUEUED_SW_GO;

/*
 * DOUT_I2C_ARBITRATION_SW_PRIORITY enum
 */

typedef enum DOUT_I2C_ARBITRATION_SW_PRIORITY {
DOUT_I2C_ARBITRATION_SW_PRIORITY_NORMAL  = 0x00000000,
DOUT_I2C_ARBITRATION_SW_PRIORITY_HIGH    = 0x00000001,
DOUT_I2C_ARBITRATION_SW_PRIORITY_0_RESERVED = 0x00000002,
DOUT_I2C_ARBITRATION_SW_PRIORITY_1_RESERVED = 0x00000003,
} DOUT_I2C_ARBITRATION_SW_PRIORITY;

/*
 * DOUT_I2C_ARBITRATION_USE_I2C_REG_REQ enum
 */

typedef enum DOUT_I2C_ARBITRATION_USE_I2C_REG_REQ {
DOUT_I2C_ARBITRATION__NOT_USE_I2C_REG_REQ = 0x00000000,
DOUT_I2C_ARBITRATION__USE_I2C_REG_REQ    = 0x00000001,
} DOUT_I2C_ARBITRATION_USE_I2C_REG_REQ;

/*
 * DOUT_I2C_CONTROL_DBG_REF_SEL enum
 */

typedef enum DOUT_I2C_CONTROL_DBG_REF_SEL {
DOUT_I2C_CONTROL_NORMAL_DEBUG            = 0x00000000,
DOUT_I2C_CONTROL_FAST_REFERENCE_DEBUG    = 0x00000001,
} DOUT_I2C_CONTROL_DBG_REF_SEL;

/*
 * DOUT_I2C_CONTROL_DDC_SELECT enum
 */

typedef enum DOUT_I2C_CONTROL_DDC_SELECT {
DOUT_I2C_CONTROL_SELECT_DDC1             = 0x00000000,
DOUT_I2C_CONTROL_SELECT_DDC2             = 0x00000001,
DOUT_I2C_CONTROL_SELECT_DDC3             = 0x00000002,
DOUT_I2C_CONTROL_SELECT_DDC4             = 0x00000003,
DOUT_I2C_CONTROL_SELECT_DDC5             = 0x00000004,
DOUT_I2C_CONTROL_SELECT_DDCVGA           = 0x00000005,
} DOUT_I2C_CONTROL_DDC_SELECT;

/*
 * DOUT_I2C_CONTROL_GO enum
 */

typedef enum DOUT_I2C_CONTROL_GO {
DOUT_I2C_CONTROL_STOP_TRANSFER           = 0x00000000,
DOUT_I2C_CONTROL_START_TRANSFER          = 0x00000001,
} DOUT_I2C_CONTROL_GO;

/*
 * DOUT_I2C_CONTROL_SEND_RESET enum
 */

typedef enum DOUT_I2C_CONTROL_SEND_RESET {
DOUT_I2C_CONTROL__NOT_SEND_RESET         = 0x00000000,
DOUT_I2C_CONTROL__SEND_RESET             = 0x00000001,
} DOUT_I2C_CONTROL_SEND_RESET;

/*
 * DOUT_I2C_CONTROL_SEND_RESET_LENGTH enum
 */

typedef enum DOUT_I2C_CONTROL_SEND_RESET_LENGTH {
DOUT_I2C_CONTROL__SEND_RESET_LENGTH_9    = 0x00000000,
DOUT_I2C_CONTROL__SEND_RESET_LENGTH_10   = 0x00000001,
} DOUT_I2C_CONTROL_SEND_RESET_LENGTH;

/*
 * DOUT_I2C_CONTROL_SOFT_RESET enum
 */

typedef enum DOUT_I2C_CONTROL_SOFT_RESET {
DOUT_I2C_CONTROL_NOT_RESET_I2C_CONTROLLER = 0x00000000,
DOUT_I2C_CONTROL_RESET_I2C_CONTROLLER    = 0x00000001,
} DOUT_I2C_CONTROL_SOFT_RESET;

/*
 * DOUT_I2C_CONTROL_SW_STATUS_RESET enum
 */

typedef enum DOUT_I2C_CONTROL_SW_STATUS_RESET {
DOUT_I2C_CONTROL_NOT_RESET_SW_STATUS     = 0x00000000,
DOUT_I2C_CONTROL_RESET_SW_STATUS         = 0x00000001,
} DOUT_I2C_CONTROL_SW_STATUS_RESET;

/*
 * DOUT_I2C_CONTROL_TRANSACTION_COUNT enum
 */

typedef enum DOUT_I2C_CONTROL_TRANSACTION_COUNT {
DOUT_I2C_CONTROL_TRANS0                  = 0x00000000,
DOUT_I2C_CONTROL_TRANS0_TRANS1           = 0x00000001,
DOUT_I2C_CONTROL_TRANS0_TRANS1_TRANS2    = 0x00000002,
DOUT_I2C_CONTROL_TRANS0_TRANS1_TRANS2_TRANS3 = 0x00000003,
} DOUT_I2C_CONTROL_TRANSACTION_COUNT;

/*
 * DOUT_I2C_DATA_INDEX_WRITE enum
 */

typedef enum DOUT_I2C_DATA_INDEX_WRITE {
DOUT_I2C_DATA__NOT_INDEX_WRITE           = 0x00000000,
DOUT_I2C_DATA__INDEX_WRITE               = 0x00000001,
} DOUT_I2C_DATA_INDEX_WRITE;

/*
 * DOUT_I2C_DDC_SETUP_CLK_DRIVE_EN enum
 */

typedef enum DOUT_I2C_DDC_SETUP_CLK_DRIVE_EN {
DOUT_I2C_DDC_SETUP_CLK_DRIVE_BY_EXTERNAL_RESISTOR = 0x00000000,
DOUT_I2C_DDC_SETUP_I2C_PAD_DRIVE_SCL     = 0x00000001,
} DOUT_I2C_DDC_SETUP_CLK_DRIVE_EN;

/*
 * DOUT_I2C_DDC_SETUP_DATA_DRIVE_EN enum
 */

typedef enum DOUT_I2C_DDC_SETUP_DATA_DRIVE_EN {
DOUT_I2C_DDC_SETUP_DATA_DRIVE_BY_EXTERNAL_RESISTOR = 0x00000000,
DOUT_I2C_DDC_SETUP_I2C_PAD_DRIVE_SDA     = 0x00000001,
} DOUT_I2C_DDC_SETUP_DATA_DRIVE_EN;

/*
 * DOUT_I2C_DDC_SETUP_DATA_DRIVE_SEL enum
 */

typedef enum DOUT_I2C_DDC_SETUP_DATA_DRIVE_SEL {
DOUT_I2C_DDC_SETUP_DATA_DRIVE_FOR_10MCLKS = 0x00000000,
DOUT_I2C_DDC_SETUP_DATA_DRIVE_FOR_20MCLKS = 0x00000001,
} DOUT_I2C_DDC_SETUP_DATA_DRIVE_SEL;

/*
 * DOUT_I2C_DDC_SETUP_EDID_DETECT_MODE enum
 */

typedef enum DOUT_I2C_DDC_SETUP_EDID_DETECT_MODE {
DOUT_I2C_DDC_SETUP_EDID_DETECT_CONNECT   = 0x00000000,
DOUT_I2C_DDC_SETUP_EDID_DETECT_DISCONNECT = 0x00000001,
} DOUT_I2C_DDC_SETUP_EDID_DETECT_MODE;

/*
 * DOUT_I2C_DDC_SPEED_THRESHOLD enum
 */

typedef enum DOUT_I2C_DDC_SPEED_THRESHOLD {
DOUT_I2C_DDC_SPEED_THRESHOLD_BIG_THAN_ZERO = 0x00000000,
DOUT_I2C_DDC_SPEED_THRESHOLD_QUATER_OF_TOTAL_SAMPLE = 0x00000001,
DOUT_I2C_DDC_SPEED_THRESHOLD_HALF_OF_TOTAL_SAMPLE = 0x00000002,
DOUT_I2C_DDC_SPEED_THRESHOLD_THREE_QUATERS_OF_TOTAL_SAMPLE = 0x00000003,
} DOUT_I2C_DDC_SPEED_THRESHOLD;

/*
 * DOUT_I2C_EDID_DETECT_CTRL_SEND_RESET enum
 */

typedef enum DOUT_I2C_EDID_DETECT_CTRL_SEND_RESET {
DOUT_I2C_EDID_NOT_SEND_RESET_BEFORE_EDID_READ_TRACTION = 0x00000000,
DOUT_I2C_EDID_SEND_RESET_BEFORE_EDID_READ_TRACTION = 0x00000001,
} DOUT_I2C_EDID_DETECT_CTRL_SEND_RESET;

/*
 * DOUT_I2C_READ_REQUEST_INTERRUPT_TYPE enum
 */

typedef enum DOUT_I2C_READ_REQUEST_INTERRUPT_TYPE {
DOUT_I2C_READ_REQUEST_INTERRUPT_TYPE__LEVEL = 0x00000000,
DOUT_I2C_READ_REQUEST_INTERRUPT_TYPE__PULSE = 0x00000001,
} DOUT_I2C_READ_REQUEST_INTERRUPT_TYPE;

/*
 * DOUT_I2C_TRANSACTION_STOP_ON_NACK enum
 */

typedef enum DOUT_I2C_TRANSACTION_STOP_ON_NACK {
DOUT_I2C_TRANSACTION_STOP_CURRENT_TRANS  = 0x00000000,
DOUT_I2C_TRANSACTION_STOP_ALL_TRANS      = 0x00000001,
} DOUT_I2C_TRANSACTION_STOP_ON_NACK;

/*******************************************************
 * DIO_MISC Enums
 *******************************************************/

/*
 * CLOCK_GATING_EN enum
 */

typedef enum CLOCK_GATING_EN {
CLOCK_GATING_ENABLE                      = 0x00000000,
CLOCK_GATING_DISABLE                     = 0x00000001,
} CLOCK_GATING_EN;

/*
 * DAC_MUX_SELECT enum
 */

typedef enum DAC_MUX_SELECT {
DAC_MUX_SELECT_DACA                      = 0x00000000,
DAC_MUX_SELECT_DACB                      = 0x00000001,
} DAC_MUX_SELECT;

/*
 * DIOMEM_PWR_DIS_CTRL enum
 */

typedef enum DIOMEM_PWR_DIS_CTRL {
DIOMEM_ENABLE_MEM_PWR_CTRL               = 0x00000000,
DIOMEM_DISABLE_MEM_PWR_CTRL              = 0x00000001,
} DIOMEM_PWR_DIS_CTRL;

/*
 * DIOMEM_PWR_FORCE_CTRL enum
 */

typedef enum DIOMEM_PWR_FORCE_CTRL {
DIOMEM_NO_FORCE_REQUEST                  = 0x00000000,
DIOMEM_FORCE_LIGHT_SLEEP_REQUEST         = 0x00000001,
DIOMEM_FORCE_DEEP_SLEEP_REQUEST          = 0x00000002,
DIOMEM_FORCE_SHUT_DOWN_REQUEST           = 0x00000003,
} DIOMEM_PWR_FORCE_CTRL;

/*
 * DIOMEM_PWR_FORCE_CTRL2 enum
 */

typedef enum DIOMEM_PWR_FORCE_CTRL2 {
DIOMEM_NO_FORCE_REQ                      = 0x00000000,
DIOMEM_FORCE_LIGHT_SLEEP_REQ             = 0x00000001,
} DIOMEM_PWR_FORCE_CTRL2;

/*
 * DIOMEM_PWR_SEL_CTRL enum
 */

typedef enum DIOMEM_PWR_SEL_CTRL {
DIOMEM_DYNAMIC_SHUT_DOWN_ENABLE          = 0x00000000,
DIOMEM_DYNAMIC_DEEP_SLEEP_ENABLE         = 0x00000001,
DIOMEM_DYNAMIC_LIGHT_SLEEP_ENABLE        = 0x00000002,
} DIOMEM_PWR_SEL_CTRL;

/*
 * DIOMEM_PWR_SEL_CTRL2 enum
 */

typedef enum DIOMEM_PWR_SEL_CTRL2 {
DIOMEM_DYNAMIC_DEEP_SLEEP_EN             = 0x00000000,
DIOMEM_DYNAMIC_LIGHT_SLEEP_EN            = 0x00000001,
} DIOMEM_PWR_SEL_CTRL2;

/*
 * DIO_DBG_BLOCK_SEL enum
 */

typedef enum DIO_DBG_BLOCK_SEL {
DIO_DBG_BLOCK_SEL_DIO                    = 0x00000000,
DIO_DBG_BLOCK_SEL_DIGFE_A                = 0x0000000b,
DIO_DBG_BLOCK_SEL_DIGFE_B                = 0x0000000c,
DIO_DBG_BLOCK_SEL_DIGFE_C                = 0x0000000d,
DIO_DBG_BLOCK_SEL_DIGFE_D                = 0x0000000e,
DIO_DBG_BLOCK_SEL_DIGFE_E                = 0x0000000f,
DIO_DBG_BLOCK_SEL_DIGA                   = 0x00000012,
DIO_DBG_BLOCK_SEL_DIGB                   = 0x00000013,
DIO_DBG_BLOCK_SEL_DIGC                   = 0x00000014,
DIO_DBG_BLOCK_SEL_DIGD                   = 0x00000015,
DIO_DBG_BLOCK_SEL_DIGE                   = 0x00000016,
DIO_DBG_BLOCK_SEL_DPFE_A                 = 0x00000019,
DIO_DBG_BLOCK_SEL_DPFE_B                 = 0x0000001a,
DIO_DBG_BLOCK_SEL_DPFE_C                 = 0x0000001b,
DIO_DBG_BLOCK_SEL_DPFE_D                 = 0x0000001c,
DIO_DBG_BLOCK_SEL_DPFE_E                 = 0x0000001d,
DIO_DBG_BLOCK_SEL_DPA                    = 0x00000020,
DIO_DBG_BLOCK_SEL_DPB                    = 0x00000021,
DIO_DBG_BLOCK_SEL_DPC                    = 0x00000022,
DIO_DBG_BLOCK_SEL_DPD                    = 0x00000023,
DIO_DBG_BLOCK_SEL_DPE                    = 0x00000024,
DIO_DBG_BLOCK_SEL_AUX0                   = 0x00000027,
DIO_DBG_BLOCK_SEL_AUX1                   = 0x00000028,
DIO_DBG_BLOCK_SEL_AUX2                   = 0x00000029,
DIO_DBG_BLOCK_SEL_AUX3                   = 0x0000002a,
DIO_DBG_BLOCK_SEL_AUX4                   = 0x0000002b,
DIO_DBG_BLOCK_SEL_PERFMON_DIO            = 0x0000002d,
DIO_DBG_BLOCK_SEL_RESERVED               = 0x0000002e,
} DIO_DBG_BLOCK_SEL;

/*
 * DIO_HDMI_RXSTATUS_TIMER_CONTROL_DIO_HDMI_RXSTATUS_TIMER_TYPE enum
 */

typedef enum DIO_HDMI_RXSTATUS_TIMER_CONTROL_DIO_HDMI_RXSTATUS_TIMER_TYPE {
DIO_HDMI_RXSTATUS_TIMER_TYPE_LEVEL       = 0x00000000,
DIO_HDMI_RXSTATUS_TIMER_TYPE_PULSE       = 0x00000001,
} DIO_HDMI_RXSTATUS_TIMER_CONTROL_DIO_HDMI_RXSTATUS_TIMER_TYPE;

/*
 * DX_PROTECTION_DX_PIPE_ENC_REQUIRED_TYPE enum
 */

typedef enum DX_PROTECTION_DX_PIPE_ENC_REQUIRED_TYPE {
DX_PROTECTION_DX_PIPE_ENC_REQUIRED_TYPE_0 = 0x00000000,
DX_PROTECTION_DX_PIPE_ENC_REQUIRED_TYPE_1 = 0x00000001,
} DX_PROTECTION_DX_PIPE_ENC_REQUIRED_TYPE;

/*
 * ENUM_DIO_DCN_ACTIVE_STATUS enum
 */

typedef enum ENUM_DIO_DCN_ACTIVE_STATUS {
ENUM_DCN_NOT_ACTIVE                      = 0x00000000,
ENUM_DCN_ACTIVE                          = 0x00000001,
} ENUM_DIO_DCN_ACTIVE_STATUS;

/*
 * GENERIC_STEREOSYNC_SEL enum
 */

typedef enum GENERIC_STEREOSYNC_SEL {
GENERIC_STEREOSYNC_SEL_D1                = 0x00000000,
GENERIC_STEREOSYNC_SEL_D2                = 0x00000001,
GENERIC_STEREOSYNC_SEL_D3                = 0x00000002,
GENERIC_STEREOSYNC_SEL_D4                = 0x00000003,
GENERIC_STEREOSYNC_SEL_RESERVED          = 0x00000004,
} GENERIC_STEREOSYNC_SEL;

/*
 * PM_ASSERT_RESET enum
 */

typedef enum PM_ASSERT_RESET {
PM_ASSERT_RESET_0                        = 0x00000000,
PM_ASSERT_RESET_1                        = 0x00000001,
} PM_ASSERT_RESET;

/*
 * SOFT_RESET enum
 */

typedef enum SOFT_RESET {
SOFT_RESET_0                             = 0x00000000,
SOFT_RESET_1                             = 0x00000001,
} SOFT_RESET;

/*
 * TMDS_MUX_SELECT enum
 */

typedef enum TMDS_MUX_SELECT {
TMDS_MUX_SELECT_B                        = 0x00000000,
TMDS_MUX_SELECT_G                        = 0x00000001,
TMDS_MUX_SELECT_R                        = 0x00000002,
TMDS_MUX_SELECT_RESERVED                 = 0x00000003,
} TMDS_MUX_SELECT;

/*******************************************************
 * DME Enums
 *******************************************************/

/*
 * DME_MEM_POWER_STATE_ENUM enum
 */

typedef enum DME_MEM_POWER_STATE_ENUM {
DME_MEM_POWER_STATE_ENUM_ON              = 0x00000000,
DME_MEM_POWER_STATE_ENUM_LS              = 0x00000001,
DME_MEM_POWER_STATE_ENUM_DS              = 0x00000002,
DME_MEM_POWER_STATE_ENUM_SD              = 0x00000003,
} DME_MEM_POWER_STATE_ENUM;

/*
 * DME_MEM_PWR_DIS_CTRL enum
 */

typedef enum DME_MEM_PWR_DIS_CTRL {
DME_MEM_ENABLE_MEM_PWR_CTRL              = 0x00000000,
DME_MEM_DISABLE_MEM_PWR_CTRL             = 0x00000001,
} DME_MEM_PWR_DIS_CTRL;

/*
 * DME_MEM_PWR_FORCE_CTRL enum
 */

typedef enum DME_MEM_PWR_FORCE_CTRL {
DME_MEM_NO_FORCE_REQUEST                 = 0x00000000,
DME_MEM_FORCE_LIGHT_SLEEP_REQUEST        = 0x00000001,
DME_MEM_FORCE_DEEP_SLEEP_REQUEST         = 0x00000002,
DME_MEM_FORCE_SHUT_DOWN_REQUEST          = 0x00000003,
} DME_MEM_PWR_FORCE_CTRL;

/*
 * METADATA_HUBP_SEL enum
 */

typedef enum METADATA_HUBP_SEL {
METADATA_HUBP_SEL_0                      = 0x00000000,
METADATA_HUBP_SEL_1                      = 0x00000001,
METADATA_HUBP_SEL_2                      = 0x00000002,
METADATA_HUBP_SEL_3                      = 0x00000003,
METADATA_HUBP_SEL_RESERVED               = 0x00000004,
} METADATA_HUBP_SEL;

/*
 * METADATA_STREAM_TYPE_SEL enum
 */

typedef enum METADATA_STREAM_TYPE_SEL {
METADATA_STREAM_DP                       = 0x00000000,
METADATA_STREAM_DVE                      = 0x00000001,
} METADATA_STREAM_TYPE_SEL;

/*******************************************************
 * VPG Enums
 *******************************************************/

/*
 * VPG_MEM_PWR_DIS_CTRL enum
 */

typedef enum VPG_MEM_PWR_DIS_CTRL {
VPG_MEM_ENABLE_MEM_PWR_CTRL              = 0x00000000,
VPG_MEM_DISABLE_MEM_PWR_CTRL             = 0x00000001,
} VPG_MEM_PWR_DIS_CTRL;

/*
 * VPG_MEM_PWR_FORCE_CTRL enum
 */

typedef enum VPG_MEM_PWR_FORCE_CTRL {
VPG_MEM_NO_FORCE_REQ                     = 0x00000000,
VPG_MEM_FORCE_LIGHT_SLEEP_REQ            = 0x00000001,
} VPG_MEM_PWR_FORCE_CTRL;

/*******************************************************
 * AFMT Enums
 *******************************************************/

/*
 * AFMT_ACP_TYPE enum
 */

typedef enum AFMT_ACP_TYPE {
ACP_TYPE_GENERIC_AUDIO                   = 0x00000000,
ACP_TYPE_ICE60958_AUDIO                  = 0x00000001,
ACP_TYPE_DVD_AUDIO                       = 0x00000002,
ACP_TYPE_SUPER_AUDIO_CD                  = 0x00000003,
} AFMT_ACP_TYPE;

/*
 * AFMT_AUDIO_CRC_CONTROL_CH_SEL enum
 */

typedef enum AFMT_AUDIO_CRC_CONTROL_CH_SEL {
AFMT_AUDIO_CRC_CH0_SIG                   = 0x00000000,
AFMT_AUDIO_CRC_CH1_SIG                   = 0x00000001,
AFMT_AUDIO_CRC_CH2_SIG                   = 0x00000002,
AFMT_AUDIO_CRC_CH3_SIG                   = 0x00000003,
AFMT_AUDIO_CRC_CH4_SIG                   = 0x00000004,
AFMT_AUDIO_CRC_CH5_SIG                   = 0x00000005,
AFMT_AUDIO_CRC_CH6_SIG                   = 0x00000006,
AFMT_AUDIO_CRC_CH7_SIG                   = 0x00000007,
AFMT_AUDIO_CRC_RESERVED_8                = 0x00000008,
AFMT_AUDIO_CRC_RESERVED_9                = 0x00000009,
AFMT_AUDIO_CRC_RESERVED_10               = 0x0000000a,
AFMT_AUDIO_CRC_RESERVED_11               = 0x0000000b,
AFMT_AUDIO_CRC_RESERVED_12               = 0x0000000c,
AFMT_AUDIO_CRC_RESERVED_13               = 0x0000000d,
AFMT_AUDIO_CRC_RESERVED_14               = 0x0000000e,
AFMT_AUDIO_CRC_AUDIO_SAMPLE_COUNT        = 0x0000000f,
} AFMT_AUDIO_CRC_CONTROL_CH_SEL;

/*
 * AFMT_AUDIO_CRC_CONTROL_CONT enum
 */

typedef enum AFMT_AUDIO_CRC_CONTROL_CONT {
AFMT_AUDIO_CRC_ONESHOT                   = 0x00000000,
AFMT_AUDIO_CRC_AUTO_RESTART              = 0x00000001,
} AFMT_AUDIO_CRC_CONTROL_CONT;

/*
 * AFMT_AUDIO_CRC_CONTROL_SOURCE enum
 */

typedef enum AFMT_AUDIO_CRC_CONTROL_SOURCE {
AFMT_AUDIO_CRC_SOURCE_FROM_FIFO_INPUT    = 0x00000000,
AFMT_AUDIO_CRC_SOURCE_FROM_FIFO_OUTPUT   = 0x00000001,
} AFMT_AUDIO_CRC_CONTROL_SOURCE;

/*
 * AFMT_AUDIO_PACKET_CONTROL2_AUDIO_LAYOUT_OVRD enum
 */

typedef enum AFMT_AUDIO_PACKET_CONTROL2_AUDIO_LAYOUT_OVRD {
AFMT_AUDIO_LAYOUT_DETERMINED_BY_AZ_AUDIO_CHANNEL_STATUS = 0x00000000,
AFMT_AUDIO_LAYOUT_OVRD_BY_REGISTER       = 0x00000001,
} AFMT_AUDIO_PACKET_CONTROL2_AUDIO_LAYOUT_OVRD;

/*
 * AFMT_AUDIO_PACKET_CONTROL_AUDIO_SAMPLE_SEND enum
 */

typedef enum AFMT_AUDIO_PACKET_CONTROL_AUDIO_SAMPLE_SEND {
AFMT_AUDIO_PACKET_SENT_DISABLED          = 0x00000000,
AFMT_AUDIO_PACKET_SENT_ENABLED           = 0x00000001,
} AFMT_AUDIO_PACKET_CONTROL_AUDIO_SAMPLE_SEND;

/*
 * AFMT_AUDIO_PACKET_CONTROL_RESET_FIFO_WHEN_AUDIO_DIS enum
 */

typedef enum AFMT_AUDIO_PACKET_CONTROL_RESET_FIFO_WHEN_AUDIO_DIS {
AFMT_NOT_RESET_AUDIO_FIFO_WHEN_AUDIO_DISABLED_RESERVED = 0x00000000,
AFMT_RESET_AUDIO_FIFO_WHEN_AUDIO_DISABLED = 0x00000001,
} AFMT_AUDIO_PACKET_CONTROL_RESET_FIFO_WHEN_AUDIO_DIS;

/*
 * AFMT_AUDIO_SRC_CONTROL_SELECT enum
 */

typedef enum AFMT_AUDIO_SRC_CONTROL_SELECT {
AFMT_AUDIO_SRC_FROM_AZ_STREAM0           = 0x00000000,
AFMT_AUDIO_SRC_FROM_AZ_STREAM1           = 0x00000001,
AFMT_AUDIO_SRC_FROM_AZ_STREAM2           = 0x00000002,
AFMT_AUDIO_SRC_FROM_AZ_STREAM3           = 0x00000003,
AFMT_AUDIO_SRC_FROM_AZ_STREAM4           = 0x00000004,
AFMT_AUDIO_SRC_FROM_AZ_STREAM5           = 0x00000005,
} AFMT_AUDIO_SRC_CONTROL_SELECT;

/*
 * AFMT_HDMI_AUDIO_SEND_MAX_PACKETS enum
 */

typedef enum AFMT_HDMI_AUDIO_SEND_MAX_PACKETS {
HDMI_NOT_SEND_MAX_AUDIO_PACKETS          = 0x00000000,
HDMI_SEND_MAX_AUDIO_PACKETS              = 0x00000001,
} AFMT_HDMI_AUDIO_SEND_MAX_PACKETS;

/*
 * AFMT_INFOFRAME_CONTROL0_AUDIO_INFO_SOURCE enum
 */

typedef enum AFMT_INFOFRAME_CONTROL0_AUDIO_INFO_SOURCE {
AFMT_INFOFRAME_SOURCE_FROM_AZALIA_BLOCK  = 0x00000000,
AFMT_INFOFRAME_SOURCE_FROM_AFMT_REGISTERS = 0x00000001,
} AFMT_INFOFRAME_CONTROL0_AUDIO_INFO_SOURCE;

/*
 * AFMT_INTERRUPT_STATUS_CHG_MASK enum
 */

typedef enum AFMT_INTERRUPT_STATUS_CHG_MASK {
AFMT_INTERRUPT_DISABLE                   = 0x00000000,
AFMT_INTERRUPT_ENABLE                    = 0x00000001,
} AFMT_INTERRUPT_STATUS_CHG_MASK;

/*
 * AFMT_MEM_PWR_DIS_CTRL enum
 */

typedef enum AFMT_MEM_PWR_DIS_CTRL {
AFMT_MEM_ENABLE_MEM_PWR_CTRL             = 0x00000000,
AFMT_MEM_DISABLE_MEM_PWR_CTRL            = 0x00000001,
} AFMT_MEM_PWR_DIS_CTRL;

/*
 * AFMT_MEM_PWR_FORCE_CTRL enum
 */

typedef enum AFMT_MEM_PWR_FORCE_CTRL {
AFMT_MEM_NO_FORCE_REQUEST                = 0x00000000,
AFMT_MEM_FORCE_LIGHT_SLEEP_REQUEST       = 0x00000001,
AFMT_MEM_FORCE_DEEP_SLEEP_REQUEST        = 0x00000002,
AFMT_MEM_FORCE_SHUT_DOWN_REQUEST         = 0x00000003,
} AFMT_MEM_PWR_FORCE_CTRL;

/*
 * AFMT_RAMP_CONTROL0_SIGN enum
 */

typedef enum AFMT_RAMP_CONTROL0_SIGN {
AFMT_RAMP_SIGNED                         = 0x00000000,
AFMT_RAMP_UNSIGNED                       = 0x00000001,
} AFMT_RAMP_CONTROL0_SIGN;

/*
 * AFMT_VBI_PACKET_CONTROL_ACP_SOURCE enum
 */

typedef enum AFMT_VBI_PACKET_CONTROL_ACP_SOURCE {
AFMT_ACP_SOURCE_FROM_AZALIA              = 0x00000000,
AFMT_ACP_SOURCE_FROM_AFMT_REGISTERS      = 0x00000001,
} AFMT_VBI_PACKET_CONTROL_ACP_SOURCE;

/*
 * AUDIO_LAYOUT_SELECT enum
 */

typedef enum AUDIO_LAYOUT_SELECT {
AUDIO_LAYOUT_0                           = 0x00000000,
AUDIO_LAYOUT_1                           = 0x00000001,
} AUDIO_LAYOUT_SELECT;

/*******************************************************
 * HPO_TOP Enums
 *******************************************************/

/*
 * HPO_TOP_CLOCK_GATING_DISABLE enum
 */

typedef enum HPO_TOP_CLOCK_GATING_DISABLE {
HPO_TOP_CLOCK_GATING_EN                  = 0x00000000,
HPO_TOP_CLOCK_GATING_DIS                 = 0x00000001,
} HPO_TOP_CLOCK_GATING_DISABLE;

/*
 * HPO_TOP_TEST_CLK_SEL enum
 */

typedef enum HPO_TOP_TEST_CLK_SEL {
HPO_TOP_PERMANENT_DISPCLK                = 0x00000000,
HPO_TOP_REGISTER_GATED_DISPCLK           = 0x00000001,
HPO_TOP_PERMANENT_SOCCLK                 = 0x00000002,
HPO_TOP_TEST_CLOCK_RESERVED              = 0x00000003,
HPO_TOP_PERMANENT_HDMISTREAMCLK0         = 0x00000004,
HPO_TOP_FEATURE_GATED_HDMISTREAMCLK0     = 0x00000005,
HPO_TOP_REGISTER_GATED_HDMISTREAMCLK0    = 0x00000006,
HPO_TOP_FEATURE_GATED_DISPCLK_IN_HDMISTREAMENC0 = 0x00000007,
HPO_TOP_FEATURE_GATED_SOCCLK_IN_HDMISTREAMENC0 = 0x00000008,
HPO_TOP_PERMANENT_HDMICHARCLK0           = 0x00000009,
HPO_TOP_FEATURE_GATED_HDMICHARCLK0       = 0x0000000a,
HPO_TOP_REGISTER_GATED_HDMICHARCLK0      = 0x0000000b,
} HPO_TOP_TEST_CLK_SEL;

/*******************************************************
 * DP_STREAM_MAPPER Enums
 *******************************************************/

/*
 * DP_STREAM_MAPPER_DP_STREAM_LINK_TARGET enum
 */

typedef enum DP_STREAM_MAPPER_DP_STREAM_LINK_TARGET {
DP_STREAM_MAPPER_LINK0                   = 0x00000000,
DP_STREAM_MAPPER_LINK1                   = 0x00000001,
DP_STREAM_MAPPER_RESERVED                = 0x00000002,
} DP_STREAM_MAPPER_DP_STREAM_LINK_TARGET;

/*******************************************************
 * HDMI_STREAM_ENC Enums
 *******************************************************/

/*
 * HDMI_STREAM_ENC_DB_DISABLE_CONTROL enum
 */

typedef enum HDMI_STREAM_ENC_DB_DISABLE_CONTROL {
HDMI_STREAM_ENC_DB_ENABLE                = 0x00000000,
HDMI_STREAM_ENC_DB_DISABLE               = 0x00000001,
} HDMI_STREAM_ENC_DB_DISABLE_CONTROL;

/*
 * HDMI_STREAM_ENC_DSC_MODE enum
 */

typedef enum HDMI_STREAM_ENC_DSC_MODE {
STREAM_DSC_DISABLE                       = 0x00000000,
STREAM_DSC_444_RGB                       = 0x00000001,
STREAM_DSC_NATIVE_422_420                = 0x00000002,
} HDMI_STREAM_ENC_DSC_MODE;

/*
 * HDMI_STREAM_ENC_ENABLE_CONTROL enum
 */

typedef enum HDMI_STREAM_ENC_ENABLE_CONTROL {
HDMI_STREAM_ENC_DISABLE                  = 0x00000000,
HDMI_STREAM_ENC_ENABLE                   = 0x00000001,
} HDMI_STREAM_ENC_ENABLE_CONTROL;

/*
 * HDMI_STREAM_ENC_ODM_COMBINE_MODE enum
 */

typedef enum HDMI_STREAM_ENC_ODM_COMBINE_MODE {
STREAM_ODM_COMBINE_1_SEGMENT             = 0x00000000,
STREAM_ODM_COMBINE_2_SEGMENT             = 0x00000001,
STREAM_ODM_COMBINE_RESERVED              = 0x00000002,
STREAM_ODM_COMBINE_4_SEGMENT             = 0x00000003,
} HDMI_STREAM_ENC_ODM_COMBINE_MODE;

/*
 * HDMI_STREAM_ENC_OVERFLOW_UNDERFLOW_ERROR enum
 */

typedef enum HDMI_STREAM_ENC_OVERFLOW_UNDERFLOW_ERROR {
HDMI_STREAM_ENC_NO_ERROR_OCCURRED        = 0x00000000,
HDMI_STREAM_ENC_UNDERFLOW_OCCURRED       = 0x00000001,
HDMI_STREAM_ENC_OVERFLOW_OCCURRED        = 0x00000002,
} HDMI_STREAM_ENC_OVERFLOW_UNDERFLOW_ERROR;

/*
 * HDMI_STREAM_ENC_OVERWRITE_LEVEL_SELECT enum
 */

typedef enum HDMI_STREAM_ENC_OVERWRITE_LEVEL_SELECT {
HDMI_STREAM_ENC_HARDWARE                 = 0x00000000,
HDMI_STREAM_ENC_PROGRAMMABLE             = 0x00000001,
} HDMI_STREAM_ENC_OVERWRITE_LEVEL_SELECT;

/*
 * HDMI_STREAM_ENC_PIXEL_ENCODING enum
 */

typedef enum HDMI_STREAM_ENC_PIXEL_ENCODING {
STREAM_PIXEL_ENCODING_444_RGB            = 0x00000000,
STREAM_PIXEL_ENCODING_422                = 0x00000001,
STREAM_PIXEL_ENCODING_420                = 0x00000002,
} HDMI_STREAM_ENC_PIXEL_ENCODING;

/*
 * HDMI_STREAM_ENC_READ_CLOCK_CONTROL enum
 */

typedef enum HDMI_STREAM_ENC_READ_CLOCK_CONTROL {
HDMI_STREAM_ENC_DCCG                     = 0x00000000,
HDMI_STREAM_ENC_DISPLAY_PIPE             = 0x00000001,
} HDMI_STREAM_ENC_READ_CLOCK_CONTROL;

/*
 * HDMI_STREAM_ENC_RESET_CONTROL enum
 */

typedef enum HDMI_STREAM_ENC_RESET_CONTROL {
HDMI_STREAM_ENC_NOT_RESET                = 0x00000000,
HDMI_STREAM_ENC_RESET                    = 0x00000001,
} HDMI_STREAM_ENC_RESET_CONTROL;

/*
 * HDMI_STREAM_ENC_STREAM_ACTIVE enum
 */

typedef enum HDMI_STREAM_ENC_STREAM_ACTIVE {
HDMI_STREAM_ENC_VIDEO_STREAM_NOT_ACTIVE  = 0x00000000,
HDMI_STREAM_ENC_VIDEO_STREAM_ACTIVE      = 0x00000001,
} HDMI_STREAM_ENC_STREAM_ACTIVE;

/*******************************************************
 * HDMI_TB_ENC Enums
 *******************************************************/

/*
 * BORROWBUFFER_MEM_POWER_STATE_ENUM enum
 */

typedef enum BORROWBUFFER_MEM_POWER_STATE_ENUM {
BORROWBUFFER_MEM_POWER_STATE_ENUM_ON     = 0x00000000,
BORROWBUFFER_MEM_POWER_STATE_ENUM_LS     = 0x00000001,
BORROWBUFFER_MEM_POWER_STATE_ENUM_DS     = 0x00000002,
BORROWBUFFER_MEM_POWER_STATE_ENUM_SD     = 0x00000003,
} BORROWBUFFER_MEM_POWER_STATE_ENUM;

/*
 * HDMI_BORROW_MODE enum
 */

typedef enum HDMI_BORROW_MODE {
TB_BORROW_MODE_NONE                      = 0x00000000,
TB_BORROW_MODE_ACTIVE                    = 0x00000001,
TB_BORROW_MODE_BLANK                     = 0x00000002,
TB_BORROW_MODE_RESERVED                  = 0x00000003,
} HDMI_BORROW_MODE;

/*
 * HDMI_TB_ENC_ACP_SEND enum
 */

typedef enum HDMI_TB_ENC_ACP_SEND {
TB_ACP_NOT_SEND                          = 0x00000000,
TB_ACP_PKT_SEND                          = 0x00000001,
} HDMI_TB_ENC_ACP_SEND;

/*
 * HDMI_TB_ENC_ACR_AUDIO_PRIORITY enum
 */

typedef enum HDMI_TB_ENC_ACR_AUDIO_PRIORITY {
TB_ACR_PKT_HIGH_PRIORITY_THAN_AUDIO_SAMPLE = 0x00000000,
TB_AUDIO_SAMPLE_HIGH_PRIORITY_THAN_ACR_PKT = 0x00000001,
} HDMI_TB_ENC_ACR_AUDIO_PRIORITY;

/*
 * HDMI_TB_ENC_ACR_CONT enum
 */

typedef enum HDMI_TB_ENC_ACR_CONT {
TB_ACR_CONT_DISABLE                      = 0x00000000,
TB_ACR_CONT_ENABLE                       = 0x00000001,
} HDMI_TB_ENC_ACR_CONT;

/*
 * HDMI_TB_ENC_ACR_N_MULTIPLE enum
 */

typedef enum HDMI_TB_ENC_ACR_N_MULTIPLE {
TB_ACR_0_MULTIPLE_RESERVED               = 0x00000000,
TB_ACR_1_MULTIPLE                        = 0x00000001,
TB_ACR_2_MULTIPLE                        = 0x00000002,
TB_ACR_3_MULTIPLE_RESERVED               = 0x00000003,
TB_ACR_4_MULTIPLE                        = 0x00000004,
TB_ACR_5_MULTIPLE_RESERVED               = 0x00000005,
TB_ACR_6_MULTIPLE_RESERVED               = 0x00000006,
TB_ACR_7_MULTIPLE_RESERVED               = 0x00000007,
} HDMI_TB_ENC_ACR_N_MULTIPLE;

/*
 * HDMI_TB_ENC_ACR_SELECT enum
 */

typedef enum HDMI_TB_ENC_ACR_SELECT {
TB_ACR_SELECT_HW                         = 0x00000000,
TB_ACR_SELECT_32K                        = 0x00000001,
TB_ACR_SELECT_44K                        = 0x00000002,
TB_ACR_SELECT_48K                        = 0x00000003,
} HDMI_TB_ENC_ACR_SELECT;

/*
 * HDMI_TB_ENC_ACR_SEND enum
 */

typedef enum HDMI_TB_ENC_ACR_SEND {
TB_ACR_NOT_SEND                          = 0x00000000,
TB_ACR_PKT_SEND                          = 0x00000001,
} HDMI_TB_ENC_ACR_SEND;

/*
 * HDMI_TB_ENC_ACR_SOURCE enum
 */

typedef enum HDMI_TB_ENC_ACR_SOURCE {
TB_ACR_SOURCE_HW                         = 0x00000000,
TB_ACR_SOURCE_SW                         = 0x00000001,
} HDMI_TB_ENC_ACR_SOURCE;

/*
 * HDMI_TB_ENC_AUDIO_INFO_CONT enum
 */

typedef enum HDMI_TB_ENC_AUDIO_INFO_CONT {
TB_AUDIO_INFO_CONT_DISABLE               = 0x00000000,
TB_AUDIO_INFO_CONT_ENABLE                = 0x00000001,
} HDMI_TB_ENC_AUDIO_INFO_CONT;

/*
 * HDMI_TB_ENC_AUDIO_INFO_SEND enum
 */

typedef enum HDMI_TB_ENC_AUDIO_INFO_SEND {
TB_AUDIO_INFO_NOT_SEND                   = 0x00000000,
TB_AUDIO_INFO_PKT_SEND                   = 0x00000001,
} HDMI_TB_ENC_AUDIO_INFO_SEND;

/*
 * HDMI_TB_ENC_CRC_SRC_SEL enum
 */

typedef enum HDMI_TB_ENC_CRC_SRC_SEL {
TB_CRC_TB_ENC_INPUT                      = 0x00000000,
TB_CRC_DSC_PACKER                        = 0x00000001,
TB_CRC_DEEP_COLOR_PACKER                 = 0x00000002,
TB_CRC_ENCRYPTOR_INPUT                   = 0x00000003,
} HDMI_TB_ENC_CRC_SRC_SEL;

/*
 * HDMI_TB_ENC_CRC_TYPE enum
 */

typedef enum HDMI_TB_ENC_CRC_TYPE {
TB_CRC_ALL_TRIBYTES                      = 0x00000000,
TB_CRC_ACTIVE_TRIBYTES                   = 0x00000001,
TB_CRC_DATAISLAND_TRIBYTES               = 0x00000002,
TB_CRC_ACTIVE_AND_DATAISLAND_TRIBYTES    = 0x00000003,
} HDMI_TB_ENC_CRC_TYPE;

/*
 * HDMI_TB_ENC_DEEP_COLOR_DEPTH enum
 */

typedef enum HDMI_TB_ENC_DEEP_COLOR_DEPTH {
TB_DEEP_COLOR_DEPTH_24BPP                = 0x00000000,
TB_DEEP_COLOR_DEPTH_30BPP                = 0x00000001,
TB_DEEP_COLOR_DEPTH_36BPP                = 0x00000002,
TB_DEEP_COLOR_DEPTH_RESERVED             = 0x00000003,
} HDMI_TB_ENC_DEEP_COLOR_DEPTH;

/*
 * HDMI_TB_ENC_DEFAULT_PAHSE enum
 */

typedef enum HDMI_TB_ENC_DEFAULT_PAHSE {
TB_DEFAULT_PHASE_IS_0                    = 0x00000000,
TB_DEFAULT_PHASE_IS_1                    = 0x00000001,
} HDMI_TB_ENC_DEFAULT_PAHSE;

/*
 * HDMI_TB_ENC_DSC_MODE enum
 */

typedef enum HDMI_TB_ENC_DSC_MODE {
TB_DSC_DISABLE                           = 0x00000000,
TB_DSC_444_RGB                           = 0x00000001,
TB_DSC_NATIVE_422_420                    = 0x00000002,
} HDMI_TB_ENC_DSC_MODE;

/*
 * HDMI_TB_ENC_ENABLE enum
 */

typedef enum HDMI_TB_ENC_ENABLE {
TB_DISABLE                               = 0x00000000,
TB_ENABLE                                = 0x00000001,
} HDMI_TB_ENC_ENABLE;

/*
 * HDMI_TB_ENC_GC_AVMUTE enum
 */

typedef enum HDMI_TB_ENC_GC_AVMUTE {
TB_GC_AVMUTE_SET                         = 0x00000000,
TB_GC_AVMUTE_UNSET                       = 0x00000001,
} HDMI_TB_ENC_GC_AVMUTE;

/*
 * HDMI_TB_ENC_GC_AVMUTE_CONT enum
 */

typedef enum HDMI_TB_ENC_GC_AVMUTE_CONT {
TB_GC_AVMUTE_CONT_DISABLE                = 0x00000000,
TB_GC_AVMUTE_CONT_ENABLE                 = 0x00000001,
} HDMI_TB_ENC_GC_AVMUTE_CONT;

/*
 * HDMI_TB_ENC_GC_CONT enum
 */

typedef enum HDMI_TB_ENC_GC_CONT {
TB_GC_CONT_DISABLE                       = 0x00000000,
TB_GC_CONT_ENABLE                        = 0x00000001,
} HDMI_TB_ENC_GC_CONT;

/*
 * HDMI_TB_ENC_GC_SEND enum
 */

typedef enum HDMI_TB_ENC_GC_SEND {
TB_GC_NOT_SEND                           = 0x00000000,
TB_GC_PKT_SEND                           = 0x00000001,
} HDMI_TB_ENC_GC_SEND;

/*
 * HDMI_TB_ENC_GENERIC_CONT enum
 */

typedef enum HDMI_TB_ENC_GENERIC_CONT {
TB_GENERIC_CONT_DISABLE                  = 0x00000000,
TB_GENERIC_CONT_ENABLE                   = 0x00000001,
} HDMI_TB_ENC_GENERIC_CONT;

/*
 * HDMI_TB_ENC_GENERIC_LOCK_EN enum
 */

typedef enum HDMI_TB_ENC_GENERIC_LOCK_EN {
HDMI_TB_ENC_GENERIC_LOCK_DISABLE         = 0x00000000,
HDMI_TB_ENC_GENERIC_LOCK_ENABLE          = 0x00000001,
} HDMI_TB_ENC_GENERIC_LOCK_EN;

/*
 * HDMI_TB_ENC_GENERIC_SEND enum
 */

typedef enum HDMI_TB_ENC_GENERIC_SEND {
TB_GENERIC_NOT_SEND                      = 0x00000000,
TB_GENERIC_PKT_SEND                      = 0x00000001,
} HDMI_TB_ENC_GENERIC_SEND;

/*
 * HDMI_TB_ENC_ISRC_CONT enum
 */

typedef enum HDMI_TB_ENC_ISRC_CONT {
TB_ISRC_CONT_DISABLE                     = 0x00000000,
TB_ISRC_CONT_ENABLE                      = 0x00000001,
} HDMI_TB_ENC_ISRC_CONT;

/*
 * HDMI_TB_ENC_ISRC_SEND enum
 */

typedef enum HDMI_TB_ENC_ISRC_SEND {
TB_ISRC_NOT_SEND                         = 0x00000000,
TB_ISRC_PKT_SEND                         = 0x00000001,
} HDMI_TB_ENC_ISRC_SEND;

/*
 * HDMI_TB_ENC_METADATA_ENABLE enum
 */

typedef enum HDMI_TB_ENC_METADATA_ENABLE {
TB_METADATA_NOT_SEND                     = 0x00000000,
TB_METADATA_PKT_SEND                     = 0x00000001,
} HDMI_TB_ENC_METADATA_ENABLE;

/*
 * HDMI_TB_ENC_PACKET_LINE_REFERENCE enum
 */

typedef enum HDMI_TB_ENC_PACKET_LINE_REFERENCE {
TB_PKT_LINE_REF_END_OF_ACTIVE            = 0x00000000,
TB_PKT_LINE_REF_OTGSOF                   = 0x00000001,
} HDMI_TB_ENC_PACKET_LINE_REFERENCE;

/*
 * HDMI_TB_ENC_PIXEL_ENCODING enum
 */

typedef enum HDMI_TB_ENC_PIXEL_ENCODING {
TB_PIXEL_ENCODING_444_RGB                = 0x00000000,
TB_PIXEL_ENCODING_422                    = 0x00000001,
TB_PIXEL_ENCODING_420                    = 0x00000002,
} HDMI_TB_ENC_PIXEL_ENCODING;

/*
 * HDMI_TB_ENC_RESET enum
 */

typedef enum HDMI_TB_ENC_RESET {
TB_NOT_RESET                             = 0x00000000,
TB_RESET                                 = 0x00000001,
} HDMI_TB_ENC_RESET;

/*
 * HDMI_TB_ENC_SYNC_PHASE enum
 */

typedef enum HDMI_TB_ENC_SYNC_PHASE {
TB_NOT_SYNC_PHASE_ON_FRAME_START         = 0x00000000,
TB_SYNC_PHASE_ON_FRAME_START             = 0x00000001,
} HDMI_TB_ENC_SYNC_PHASE;

/*
 * INPUT_FIFO_ERROR_TYPE enum
 */

typedef enum INPUT_FIFO_ERROR_TYPE {
TB_NO_ERROR_OCCURRED                     = 0x00000000,
TB_OVERFLOW_OCCURRED                     = 0x00000001,
} INPUT_FIFO_ERROR_TYPE;

/*******************************************************
 * DP_STREAM_ENC Enums
 *******************************************************/

/*
 * DP_STREAM_ENC_OVERFLOW_UNDERFLOW_ERROR enum
 */

typedef enum DP_STREAM_ENC_OVERFLOW_UNDERFLOW_ERROR {
DP_STREAM_ENC_NO_ERROR_OCCURRED          = 0x00000000,
DP_STREAM_ENC_UNDERFLOW_OCCURRED         = 0x00000001,
DP_STREAM_ENC_OVERFLOW_OCCURRED          = 0x00000002,
} DP_STREAM_ENC_OVERFLOW_UNDERFLOW_ERROR;

/*
 * DP_STREAM_ENC_OVERWRITE_LEVEL_SELECT enum
 */

typedef enum DP_STREAM_ENC_OVERWRITE_LEVEL_SELECT {
DP_STREAM_ENC_HARDWARE                   = 0x00000000,
DP_STREAM_ENC_PROGRAMMABLE               = 0x00000001,
} DP_STREAM_ENC_OVERWRITE_LEVEL_SELECT;

/*
 * DP_STREAM_ENC_READ_CLOCK_CONTROL enum
 */

typedef enum DP_STREAM_ENC_READ_CLOCK_CONTROL {
DP_STREAM_ENC_DCCG                       = 0x00000000,
DP_STREAM_ENC_DISPLAY_PIPE               = 0x00000001,
} DP_STREAM_ENC_READ_CLOCK_CONTROL;

/*
 * DP_STREAM_ENC_RESET_CONTROL enum
 */

typedef enum DP_STREAM_ENC_RESET_CONTROL {
DP_STREAM_ENC_NOT_RESET                  = 0x00000000,
DP_STREAM_ENC_RESET                      = 0x00000001,
} DP_STREAM_ENC_RESET_CONTROL;

/*
 * DP_STREAM_ENC_STREAM_ACTIVE enum
 */

typedef enum DP_STREAM_ENC_STREAM_ACTIVE {
DP_STREAM_ENC_VIDEO_STREAM_NOT_ACTIVE    = 0x00000000,
DP_STREAM_ENC_VIDEO_STREAM_ACTIVE        = 0x00000001,
} DP_STREAM_ENC_STREAM_ACTIVE;

/*******************************************************
 * DP_SYM32_ENC Enums
 *******************************************************/

/*
 * ENUM_DP_SYM32_ENC_AUDIO_MUTE enum
 */

typedef enum ENUM_DP_SYM32_ENC_AUDIO_MUTE {
DP_SYM32_ENC_SDP_AUDIO_MUTE_NOT_FORCED   = 0x00000000,
DP_SYM32_ENC_SDP_AUDIO_MUTE_FORCED       = 0x00000001,
} ENUM_DP_SYM32_ENC_AUDIO_MUTE;

/*
 * ENUM_DP_SYM32_ENC_CONTINUOUS_MODE enum
 */

typedef enum ENUM_DP_SYM32_ENC_CONTINUOUS_MODE {
DP_SYM32_ENC_ONE_SHOT_MODE               = 0x00000000,
DP_SYM32_ENC_CONTINUOUS_MODE             = 0x00000001,
} ENUM_DP_SYM32_ENC_CONTINUOUS_MODE;

/*
 * ENUM_DP_SYM32_ENC_CRC_VALID enum
 */

typedef enum ENUM_DP_SYM32_ENC_CRC_VALID {
DP_SYM32_ENC_CRC_NOT_VALID               = 0x00000000,
DP_SYM32_ENC_CRC_VALID                   = 0x00000001,
} ENUM_DP_SYM32_ENC_CRC_VALID;

/*
 * ENUM_DP_SYM32_ENC_DP_COMPONENT_DEPTH enum
 */

typedef enum ENUM_DP_SYM32_ENC_DP_COMPONENT_DEPTH {
DP_SYM32_ENC_COMPONENT_DEPTH_6BPC        = 0x00000000,
DP_SYM32_ENC_COMPONENT_DEPTH_8BPC        = 0x00000001,
DP_SYM32_ENC_COMPONENT_DEPTH_10BPC       = 0x00000002,
DP_SYM32_ENC_COMPONENT_DEPTH_12BPC       = 0x00000003,
} ENUM_DP_SYM32_ENC_DP_COMPONENT_DEPTH;

/*
 * ENUM_DP_SYM32_ENC_ENABLE enum
 */

typedef enum ENUM_DP_SYM32_ENC_ENABLE {
DP_SYM32_ENC_DISABLE                     = 0x00000000,
DP_SYM32_ENC_ENABLE                      = 0x00000001,
} ENUM_DP_SYM32_ENC_ENABLE;

/*
 * ENUM_DP_SYM32_ENC_GSP_DEADLINE_MISSED enum
 */

typedef enum ENUM_DP_SYM32_ENC_GSP_DEADLINE_MISSED {
DP_SYM32_ENC_GSP_DEADLINE_NOT_MISSED     = 0x00000000,
DP_SYM32_ENC_GSP_DEADLINE_MISSED         = 0x00000001,
} ENUM_DP_SYM32_ENC_GSP_DEADLINE_MISSED;

/*
 * ENUM_DP_SYM32_ENC_GSP_ONE_SHOT_TRIGGER_POSITION enum
 */

typedef enum ENUM_DP_SYM32_ENC_GSP_ONE_SHOT_TRIGGER_POSITION {
DP_SYM32_ENC_GSP_SEND_AT_LINE_NUMBER     = 0x00000000,
DP_SYM32_ENC_GSP_SEND_AT_EARLIEST_TIME   = 0x00000001,
} ENUM_DP_SYM32_ENC_GSP_ONE_SHOT_TRIGGER_POSITION;

/*
 * ENUM_DP_SYM32_ENC_GSP_PAYLOAD_SIZE enum
 */

typedef enum ENUM_DP_SYM32_ENC_GSP_PAYLOAD_SIZE {
DP_SYM32_ENC_GSP_PAYLOAD_SIZE_32         = 0x00000000,
DP_SYM32_ENC_GSP_PAYLOAD_SIZE_RESERVED0  = 0x00000001,
DP_SYM32_ENC_GSP_PAYLOAD_SIZE_RESERVED1  = 0x00000002,
DP_SYM32_ENC_GSP_PAYLOAD_SIZE_128        = 0x00000003,
} ENUM_DP_SYM32_ENC_GSP_PAYLOAD_SIZE;

/*
 * ENUM_DP_SYM32_ENC_GSP_TRIGGER_PENDING enum
 */

typedef enum ENUM_DP_SYM32_ENC_GSP_TRIGGER_PENDING {
DP_SYM32_ENC_GSP_TRIGGER_NOT_PENDING     = 0x00000000,
DP_SYM32_ENC_GSP_TRIGGER_PENDING         = 0x00000001,
} ENUM_DP_SYM32_ENC_GSP_TRIGGER_PENDING;

/*
 * ENUM_DP_SYM32_ENC_MEM_PWR_FORCE_ENUM enum
 */

typedef enum ENUM_DP_SYM32_ENC_MEM_PWR_FORCE_ENUM {
DP_SYM32_ENC_MEM_PWR_NO_FORCE_REQUEST    = 0x00000000,
DP_SYM32_ENC_MEM_PWR_FORCE_LIGHT_SLEEP_REQUEST = 0x00000001,
DP_SYM32_ENC_MEM_PWR_FORCE_DEEP_SLEEP_REQUEST = 0x00000002,
DP_SYM32_ENC_MEM_PWR_FORCE_SHUT_DOWN_REQUEST = 0x00000003,
} ENUM_DP_SYM32_ENC_MEM_PWR_FORCE_ENUM;

/*
 * ENUM_DP_SYM32_ENC_OVERFLOW_STATUS enum
 */

typedef enum ENUM_DP_SYM32_ENC_OVERFLOW_STATUS {
DP_SYM32_ENC_NO_OVERFLOW_OCCURRED        = 0x00000000,
DP_SYM32_ENC_OVERFLOW_OCCURRED           = 0x00000001,
} ENUM_DP_SYM32_ENC_OVERFLOW_STATUS;

/*
 * ENUM_DP_SYM32_ENC_PENDING enum
 */

typedef enum ENUM_DP_SYM32_ENC_PENDING {
DP_SYM32_ENC_NOT_PENDING                 = 0x00000000,
DP_SYM32_ENC_PENDING                     = 0x00000001,
} ENUM_DP_SYM32_ENC_PENDING;

/*
 * ENUM_DP_SYM32_ENC_PIXEL_ENCODING enum
 */

typedef enum ENUM_DP_SYM32_ENC_PIXEL_ENCODING {
DP_SYM32_ENC_PIXEL_ENCODING_RGB_YCBCR444 = 0x00000000,
DP_SYM32_ENC_PIXEL_ENCODING_YCBCR422     = 0x00000001,
DP_SYM32_ENC_PIXEL_ENCODING_YCBCR420     = 0x00000002,
DP_SYM32_ENC_PIXEL_ENCODING_Y_ONLY       = 0x00000003,
} ENUM_DP_SYM32_ENC_PIXEL_ENCODING;

/*
 * ENUM_DP_SYM32_ENC_PIXEL_ENCODING_TYPE enum
 */

typedef enum ENUM_DP_SYM32_ENC_PIXEL_ENCODING_TYPE {
DP_SYM32_ENC_UNCOMPRESSED_FORMAT         = 0x00000000,
DP_SYM32_ENC_COMPRESSED_FORMAT           = 0x00000001,
} ENUM_DP_SYM32_ENC_PIXEL_ENCODING_TYPE;

/*
 * ENUM_DP_SYM32_ENC_POWER_STATE_ENUM enum
 */

typedef enum ENUM_DP_SYM32_ENC_POWER_STATE_ENUM {
DP_SYM32_ENC_POWER_STATE_ENUM_ON         = 0x00000000,
DP_SYM32_ENC_POWER_STATE_ENUM_LS         = 0x00000001,
DP_SYM32_ENC_POWER_STATE_ENUM_DS         = 0x00000002,
DP_SYM32_ENC_POWER_STATE_ENUM_SD         = 0x00000003,
} ENUM_DP_SYM32_ENC_POWER_STATE_ENUM;

/*
 * ENUM_DP_SYM32_ENC_RESET enum
 */

typedef enum ENUM_DP_SYM32_ENC_RESET {
DP_SYM32_ENC_NOT_RESET                   = 0x00000000,
DP_SYM32_ENC_RESET                       = 0x00000001,
} ENUM_DP_SYM32_ENC_RESET;

/*
 * ENUM_DP_SYM32_ENC_SDP_PRIORITY enum
 */

typedef enum ENUM_DP_SYM32_ENC_SDP_PRIORITY {
DP_SYM32_ENC_SDP_LOW_PRIORITY            = 0x00000000,
DP_SYM32_ENC_SDP_HIGH_PRIORITY           = 0x00000001,
} ENUM_DP_SYM32_ENC_SDP_PRIORITY;

/*
 * ENUM_DP_SYM32_ENC_SOF_REFERENCE enum
 */

typedef enum ENUM_DP_SYM32_ENC_SOF_REFERENCE {
DP_SYM32_ENC_DP_SOF                      = 0x00000000,
DP_SYM32_ENC_OTG_SOF                     = 0x00000001,
} ENUM_DP_SYM32_ENC_SOF_REFERENCE;

/*
 * ENUM_DP_SYM32_ENC_VID_STREAM_DEFER enum
 */

typedef enum ENUM_DP_SYM32_ENC_VID_STREAM_DEFER {
DP_SYM32_ENC_VID_STREAM_NO_DEFER         = 0x00000000,
DP_SYM32_ENC_VID_STREAM_DEFER_TO_HBLANK  = 0x00000001,
DP_SYM32_ENC_VID_STREAM_DEFER_TO_VBLANK  = 0x00000002,
} ENUM_DP_SYM32_ENC_VID_STREAM_DEFER;

/*******************************************************
 * DP_DPHY_SYM32 Enums
 *******************************************************/

/*
 * ENUM_DP_DPHY_SYM32_CRC_END_EVENT enum
 */

typedef enum ENUM_DP_DPHY_SYM32_CRC_END_EVENT {
DP_DPHY_SYM32_CRC_END_LLCP               = 0x00000000,
DP_DPHY_SYM32_CRC_END_PS_ONLY            = 0x00000001,
DP_DPHY_SYM32_CRC_END_PS_LT_SR           = 0x00000002,
DP_DPHY_SYM32_CRC_END_PS_ANY             = 0x00000003,
} ENUM_DP_DPHY_SYM32_CRC_END_EVENT;

/*
 * ENUM_DP_DPHY_SYM32_CRC_START_EVENT enum
 */

typedef enum ENUM_DP_DPHY_SYM32_CRC_START_EVENT {
DP_DPHY_SYM32_CRC_START_LLCP             = 0x00000000,
DP_DPHY_SYM32_CRC_START_PS_ONLY          = 0x00000001,
DP_DPHY_SYM32_CRC_START_PS_LT_SR         = 0x00000002,
DP_DPHY_SYM32_CRC_START_PS_POST_LT_SR    = 0x00000003,
DP_DPHY_SYM32_CRC_START_TP_START         = 0x00000004,
} ENUM_DP_DPHY_SYM32_CRC_START_EVENT;

/*
 * ENUM_DP_DPHY_SYM32_CRC_TAP_SOURCE enum
 */

typedef enum ENUM_DP_DPHY_SYM32_CRC_TAP_SOURCE {
DP_DPHY_SYM32_CRC_TAP_SOURCE_SCHEDULER   = 0x00000000,
DP_DPHY_SYM32_CRC_TAP_SOURCE_SYMBOL_HANDLER = 0x00000001,
DP_DPHY_SYM32_CRC_TAP_SOURCE_TP_GEN_MUX  = 0x00000002,
} ENUM_DP_DPHY_SYM32_CRC_TAP_SOURCE;

/*
 * ENUM_DP_DPHY_SYM32_CRC_USE_NUM_SYMBOLS enum
 */

typedef enum ENUM_DP_DPHY_SYM32_CRC_USE_NUM_SYMBOLS {
DP_DPHY_SYM32_CRC_USE_END_EVENT          = 0x00000000,
DP_DPHY_SYM32_CRC_USE_NUM_SYMBOLS        = 0x00000001,
} ENUM_DP_DPHY_SYM32_CRC_USE_NUM_SYMBOLS;

/*
 * ENUM_DP_DPHY_SYM32_ENABLE enum
 */

typedef enum ENUM_DP_DPHY_SYM32_ENABLE {
DP_DPHY_SYM32_DISABLE                    = 0x00000000,
DP_DPHY_SYM32_ENABLE                     = 0x00000001,
} ENUM_DP_DPHY_SYM32_ENABLE;

/*
 * ENUM_DP_DPHY_SYM32_ENCRYPT_TYPE enum
 */

typedef enum ENUM_DP_DPHY_SYM32_ENCRYPT_TYPE {
DP_DPHY_SYM32_ENCRYPT_TYPE0              = 0x00000000,
DP_DPHY_SYM32_ENCRYPT_TYPE1              = 0x00000001,
} ENUM_DP_DPHY_SYM32_ENCRYPT_TYPE;

/*
 * ENUM_DP_DPHY_SYM32_MODE enum
 */

typedef enum ENUM_DP_DPHY_SYM32_MODE {
DP_DPHY_SYM32_LT_TPS1                    = 0x00000000,
DP_DPHY_SYM32_LT_TPS2                    = 0x00000001,
DP_DPHY_SYM32_ACTIVE                     = 0x00000002,
DP_DPHY_SYM32_TEST                       = 0x00000003,
} ENUM_DP_DPHY_SYM32_MODE;

/*
 * ENUM_DP_DPHY_SYM32_NUM_LANES enum
 */

typedef enum ENUM_DP_DPHY_SYM32_NUM_LANES {
DP_DPHY_SYM32_1LANE                      = 0x00000000,
DP_DPHY_SYM32_2LANE                      = 0x00000001,
DP_DPHY_SYM32_RESERVED                   = 0x00000002,
DP_DPHY_SYM32_4LANE                      = 0x00000003,
} ENUM_DP_DPHY_SYM32_NUM_LANES;

/*
 * ENUM_DP_DPHY_SYM32_RATE_UPDATE_PENDING enum
 */

typedef enum ENUM_DP_DPHY_SYM32_RATE_UPDATE_PENDING {
DP_DPHY_SYM32_NO_RATE_UPDATE_PENDING     = 0x00000000,
DP_DPHY_SYM32_RATE_UPDATE_PENDING        = 0x00000001,
} ENUM_DP_DPHY_SYM32_RATE_UPDATE_PENDING;

/*
 * ENUM_DP_DPHY_SYM32_RESET enum
 */

typedef enum ENUM_DP_DPHY_SYM32_RESET {
DP_DPHY_SYM32_NOT_RESET                  = 0x00000000,
DP_DPHY_SYM32_RESET                      = 0x00000001,
} ENUM_DP_DPHY_SYM32_RESET;

/*
 * ENUM_DP_DPHY_SYM32_RESET_STATUS enum
 */

typedef enum ENUM_DP_DPHY_SYM32_RESET_STATUS {
DP_DPHY_SYM32_RESET_STATUS_DEASSERTED    = 0x00000000,
DP_DPHY_SYM32_RESET_STATUS_ASSERTED      = 0x00000001,
} ENUM_DP_DPHY_SYM32_RESET_STATUS;

/*
 * ENUM_DP_DPHY_SYM32_SAT_UPDATE enum
 */

typedef enum ENUM_DP_DPHY_SYM32_SAT_UPDATE {
DP_DPHY_SYM32_SAT_NO_UPDATE              = 0x00000000,
DP_DPHY_SYM32_SAT_TRIGGER_UPDATE         = 0x00000001,
DP_DPHY_SYM32_SAT_NOTRIGGER_UPDATE       = 0x00000002,
} ENUM_DP_DPHY_SYM32_SAT_UPDATE;

/*
 * ENUM_DP_DPHY_SYM32_SAT_UPDATE_PENDING enum
 */

typedef enum ENUM_DP_DPHY_SYM32_SAT_UPDATE_PENDING {
DP_DPHY_SYM32_SAT_NO_UPDATE_PENDING      = 0x00000000,
DP_DPHY_SYM32_SAT_TRIGGER_UPDATE_PENDING = 0x00000001,
DP_DPHY_SYM32_SAT_NOTRIGGER_UPDATE_PENDING = 0x00000002,
} ENUM_DP_DPHY_SYM32_SAT_UPDATE_PENDING;

/*
 * ENUM_DP_DPHY_SYM32_STATUS enum
 */

typedef enum ENUM_DP_DPHY_SYM32_STATUS {
DP_DPHY_SYM32_STATUS_IDLE                = 0x00000000,
DP_DPHY_SYM32_STATUS_ENABLED             = 0x00000001,
} ENUM_DP_DPHY_SYM32_STATUS;

/*
 * ENUM_DP_DPHY_SYM32_STREAM_OVR_ENABLE enum
 */

typedef enum ENUM_DP_DPHY_SYM32_STREAM_OVR_ENABLE {
DP_DPHY_SYM32_STREAM_OVR_NONE            = 0x00000000,
DP_DPHY_SYM32_STREAM_OVR_REPLACE         = 0x00000001,
DP_DPHY_SYM32_STREAM_OVR_ALWAYS          = 0x00000002,
} ENUM_DP_DPHY_SYM32_STREAM_OVR_ENABLE;

/*
 * ENUM_DP_DPHY_SYM32_STREAM_OVR_TYPE enum
 */

typedef enum ENUM_DP_DPHY_SYM32_STREAM_OVR_TYPE {
DP_DPHY_SYM32_STREAM_OVR_TYPE_DATA       = 0x00000000,
DP_DPHY_SYM32_STREAM_OVR_TYPE_CONTROL    = 0x00000001,
} ENUM_DP_DPHY_SYM32_STREAM_OVR_TYPE;

/*
 * ENUM_DP_DPHY_SYM32_TP_PRBS_SEL enum
 */

typedef enum ENUM_DP_DPHY_SYM32_TP_PRBS_SEL {
DP_DPHY_SYM32_TP_PRBS_SEL_PRBS7          = 0x00000000,
DP_DPHY_SYM32_TP_PRBS_SEL_PRBS9          = 0x00000001,
DP_DPHY_SYM32_TP_PRBS_SEL_PRBS11         = 0x00000002,
DP_DPHY_SYM32_TP_PRBS_SEL_PRBS15         = 0x00000003,
DP_DPHY_SYM32_TP_PRBS_SEL_PRBS23         = 0x00000004,
DP_DPHY_SYM32_TP_PRBS_SEL_PRBS31         = 0x00000005,
} ENUM_DP_DPHY_SYM32_TP_PRBS_SEL;

/*
 * ENUM_DP_DPHY_SYM32_TP_SELECT enum
 */

typedef enum ENUM_DP_DPHY_SYM32_TP_SELECT {
DP_DPHY_SYM32_TP_SELECT_TPS1             = 0x00000000,
DP_DPHY_SYM32_TP_SELECT_TPS2             = 0x00000001,
DP_DPHY_SYM32_TP_SELECT_PRBS             = 0x00000002,
DP_DPHY_SYM32_TP_SELECT_CUSTOM           = 0x00000003,
DP_DPHY_SYM32_TP_SELECT_SQUARE           = 0x00000004,
} ENUM_DP_DPHY_SYM32_TP_SELECT;

/*******************************************************
 * APG Enums
 *******************************************************/

/*
 * APG_AUDIO_CRC_CONTROL_CH_SEL enum
 */

typedef enum APG_AUDIO_CRC_CONTROL_CH_SEL {
APG_AUDIO_CRC_CH0_SIG                    = 0x00000000,
APG_AUDIO_CRC_CH1_SIG                    = 0x00000001,
APG_AUDIO_CRC_CH2_SIG                    = 0x00000002,
APG_AUDIO_CRC_CH3_SIG                    = 0x00000003,
APG_AUDIO_CRC_CH4_SIG                    = 0x00000004,
APG_AUDIO_CRC_CH5_SIG                    = 0x00000005,
APG_AUDIO_CRC_CH6_SIG                    = 0x00000006,
APG_AUDIO_CRC_CH7_SIG                    = 0x00000007,
APG_AUDIO_CRC_RESERVED_8                 = 0x00000008,
APG_AUDIO_CRC_RESERVED_9                 = 0x00000009,
APG_AUDIO_CRC_RESERVED_10                = 0x0000000a,
APG_AUDIO_CRC_RESERVED_11                = 0x0000000b,
APG_AUDIO_CRC_RESERVED_12                = 0x0000000c,
APG_AUDIO_CRC_RESERVED_13                = 0x0000000d,
APG_AUDIO_CRC_RESERVED_14                = 0x0000000e,
APG_AUDIO_CRC_RESERVED_15                = 0x0000000f,
} APG_AUDIO_CRC_CONTROL_CH_SEL;

/*
 * APG_AUDIO_CRC_CONTROL_CONT enum
 */

typedef enum APG_AUDIO_CRC_CONTROL_CONT {
APG_AUDIO_CRC_ONESHOT                    = 0x00000000,
APG_AUDIO_CRC_CONTINUOUS                 = 0x00000001,
} APG_AUDIO_CRC_CONTROL_CONT;

/*
 * APG_DBG_ACP_TYPE enum
 */

typedef enum APG_DBG_ACP_TYPE {
APG_ACP_TYPE_GENERIC_AUDIO               = 0x00000000,
APG_ACP_TYPE_ICE60958_AUDIO              = 0x00000001,
APG_ACP_TYPE_DVD_AUDIO                   = 0x00000002,
APG_ACP_TYPE_SUPER_AUDIO_CD              = 0x00000003,
} APG_DBG_ACP_TYPE;

/*
 * APG_DBG_AUDIO_DTO_BASE enum
 */

typedef enum APG_DBG_AUDIO_DTO_BASE {
BASE_RATE_48KHZ                          = 0x00000000,
BASE_RATE_44P1KHZ                        = 0x00000001,
} APG_DBG_AUDIO_DTO_BASE;

/*
 * APG_DBG_AUDIO_DTO_DIV enum
 */

typedef enum APG_DBG_AUDIO_DTO_DIV {
DIVISOR_BY1                              = 0x00000000,
DIVISOR_BY2_RESERVED                     = 0x00000001,
DIVISOR_BY3                              = 0x00000002,
DIVISOR_BY4_RESERVED                     = 0x00000003,
DIVISOR_BY5_RESERVED                     = 0x00000004,
DIVISOR_BY6_RESERVED                     = 0x00000005,
DIVISOR_BY7_RESERVED                     = 0x00000006,
DIVISOR_BY8_RESERVED                     = 0x00000007,
} APG_DBG_AUDIO_DTO_DIV;

/*
 * APG_DBG_AUDIO_DTO_MULTI enum
 */

typedef enum APG_DBG_AUDIO_DTO_MULTI {
MULTIPLE_BY1                             = 0x00000000,
MULTIPLE_BY2                             = 0x00000001,
MULTIPLE_BY3_RESERVED                    = 0x00000002,
MULTIPLE_BY4                             = 0x00000003,
MULTIPLE_RESERVED                        = 0x00000004,
} APG_DBG_AUDIO_DTO_MULTI;

/*
 * APG_DBG_MUX_SEL enum
 */

typedef enum APG_DBG_MUX_SEL {
APG_FUNCTIONAL_MODE                      = 0x00000000,
APG_DEBUG_AUDIO_MODE                     = 0x00000001,
} APG_DBG_MUX_SEL;

/*
 * APG_DP_ASP_CHANNEL_COUNT_OVERRIDE enum
 */

typedef enum APG_DP_ASP_CHANNEL_COUNT_OVERRIDE {
APG_DP_ASP_CHANNEL_COUNT_FROM_AZ         = 0x00000000,
APG_DP_ASP_CHANNEL_COUNT_OVERRIDE_ENABLED = 0x00000001,
} APG_DP_ASP_CHANNEL_COUNT_OVERRIDE;

/*
 * APG_MEM_POWER_STATE enum
 */

typedef enum APG_MEM_POWER_STATE {
APG_MEM_POWER_STATE_ON                   = 0x00000000,
APG_MEM_POWER_STATE_LS                   = 0x00000001,
APG_MEM_POWER_STATE_DS                   = 0x00000002,
APG_MEM_POWER_STATE_SD                   = 0x00000003,
} APG_MEM_POWER_STATE;

/*
 * APG_MEM_PWR_DIS_CTRL enum
 */

typedef enum APG_MEM_PWR_DIS_CTRL {
APG_MEM_ENABLE_MEM_PWR_CTRL              = 0x00000000,
APG_MEM_DISABLE_MEM_PWR_CTRL             = 0x00000001,
} APG_MEM_PWR_DIS_CTRL;

/*
 * APG_MEM_PWR_FORCE_CTRL enum
 */

typedef enum APG_MEM_PWR_FORCE_CTRL {
APG_MEM_NO_FORCE_REQUEST                 = 0x00000000,
APG_MEM_FORCE_LIGHT_SLEEP_REQUEST        = 0x00000001,
APG_MEM_FORCE_DEEP_SLEEP_REQUEST         = 0x00000002,
APG_MEM_FORCE_SHUT_DOWN_REQUEST          = 0x00000003,
} APG_MEM_PWR_FORCE_CTRL;

/*
 * APG_PACKET_CONTROL_ACP_SOURCE enum
 */

typedef enum APG_PACKET_CONTROL_ACP_SOURCE {
APG_ACP_SOURCE_NO_OVERRIDE               = 0x00000000,
APG_ACP_OVERRIDE                         = 0x00000001,
} APG_PACKET_CONTROL_ACP_SOURCE;

/*
 * APG_PACKET_CONTROL_AUDIO_INFO_SOURCE enum
 */

typedef enum APG_PACKET_CONTROL_AUDIO_INFO_SOURCE {
APG_INFOFRAME_SOURCE_NO_OVERRIDE         = 0x00000000,
APG_INFOFRAME_SOURCE_FROM_APG_REGISTERS  = 0x00000001,
} APG_PACKET_CONTROL_AUDIO_INFO_SOURCE;

/*
 * APG_RAMP_CONTROL_SIGN enum
 */

typedef enum APG_RAMP_CONTROL_SIGN {
APG_RAMP_SIGNED                          = 0x00000000,
APG_RAMP_UNSIGNED                        = 0x00000001,
} APG_RAMP_CONTROL_SIGN;

/*******************************************************
 * DCIO Enums
 *******************************************************/

/*
 * DCIO_BL_PWM_GRP1_FRAME_START_DISP_SEL enum
 */

typedef enum DCIO_BL_PWM_GRP1_FRAME_START_DISP_SEL {
DCIO_BL_PWM_GRP1_FRAME_START_DISP_SEL_CONTROLLER1 = 0x00000000,
DCIO_BL_PWM_GRP1_FRAME_START_DISP_SEL_CONTROLLER2 = 0x00000001,
DCIO_BL_PWM_GRP1_FRAME_START_DISP_SEL_CONTROLLER3 = 0x00000002,
DCIO_BL_PWM_GRP1_FRAME_START_DISP_SEL_CONTROLLER4 = 0x00000003,
DCIO_BL_PWM_GRP1_FRAME_START_DISP_SEL_CONTROLLER5 = 0x00000004,
DCIO_BL_PWM_GRP1_FRAME_START_DISP_SEL_CONTROLLER6 = 0x00000005,
} DCIO_BL_PWM_GRP1_FRAME_START_DISP_SEL;

/*
 * DCIO_CLOCK_CNTL_DCIO_TEST_CLK_SEL enum
 */

typedef enum DCIO_CLOCK_CNTL_DCIO_TEST_CLK_SEL {
DCIO_TEST_CLK_SEL_DISPCLK                = 0x00000000,
DCIO_TEST_CLK_SEL_GATED_DISPCLK          = 0x00000001,
DCIO_TEST_CLK_SEL_SOCCLK                 = 0x00000002,
} DCIO_CLOCK_CNTL_DCIO_TEST_CLK_SEL;

/*
 * DCIO_CLOCK_CNTL_DISPCLK_R_DCIO_GATE_DIS enum
 */

typedef enum DCIO_CLOCK_CNTL_DISPCLK_R_DCIO_GATE_DIS {
DCIO_DISPCLK_R_DCIO_GATE_DISABLE         = 0x00000000,
DCIO_DISPCLK_R_DCIO_GATE_ENABLE          = 0x00000001,
} DCIO_CLOCK_CNTL_DISPCLK_R_DCIO_GATE_DIS;

/*
 * DCIO_DBG_ASYNC_4BIT_SEL enum
 */

typedef enum DCIO_DBG_ASYNC_4BIT_SEL {
DCIO_DBG_ASYNC_4BIT_SEL_3TO0             = 0x00000000,
DCIO_DBG_ASYNC_4BIT_SEL_7TO4             = 0x00000001,
DCIO_DBG_ASYNC_4BIT_SEL_11TO8            = 0x00000002,
DCIO_DBG_ASYNC_4BIT_SEL_15TO12           = 0x00000003,
DCIO_DBG_ASYNC_4BIT_SEL_19TO16           = 0x00000004,
DCIO_DBG_ASYNC_4BIT_SEL_23TO20           = 0x00000005,
DCIO_DBG_ASYNC_4BIT_SEL_27TO24           = 0x00000006,
DCIO_DBG_ASYNC_4BIT_SEL_31TO28           = 0x00000007,
} DCIO_DBG_ASYNC_4BIT_SEL;

/*
 * DCIO_DBG_ASYNC_BLOCK_SEL enum
 */

typedef enum DCIO_DBG_ASYNC_BLOCK_SEL {
DCIO_DBG_ASYNC_BLOCK_SEL_OVERRIDE        = 0x00000000,
DCIO_DBG_ASYNC_BLOCK_SEL_DCCG            = 0x00000001,
DCIO_DBG_ASYNC_BLOCK_SEL_DCIO            = 0x00000002,
DCIO_DBG_ASYNC_BLOCK_SEL_DIO             = 0x00000003,
} DCIO_DBG_ASYNC_BLOCK_SEL;

/*
 * DCIO_DCRXPHY_SOFT_RESET enum
 */

typedef enum DCIO_DCRXPHY_SOFT_RESET {
DCIO_DCRXPHY_SOFT_RESET_DEASSERT         = 0x00000000,
DCIO_DCRXPHY_SOFT_RESET_ASSERT           = 0x00000001,
} DCIO_DCRXPHY_SOFT_RESET;

/*
 * DCIO_DC_GENERICA_SEL enum
 */

typedef enum DCIO_DC_GENERICA_SEL {
DCIO_GENERICA_SEL_STEREOSYNC             = 0x00000001,
DCIO_GENERICA_SEL_GENERICA_DCCG          = 0x0000000a,
DCIO_GENERICA_SEL_SYNCEN                 = 0x0000000b,
} DCIO_DC_GENERICA_SEL;

/*
 * DCIO_DC_GENERICB_SEL enum
 */

typedef enum DCIO_DC_GENERICB_SEL {
DCIO_GENERICB_SEL_STEREOSYNC             = 0x00000001,
DCIO_GENERICB_SEL_GENERICB_DCCG          = 0x0000000a,
DCIO_GENERICB_SEL_SYNCEN                 = 0x0000000b,
} DCIO_DC_GENERICB_SEL;

/*
 * DCIO_DC_GENERIC_UNIPHY_FBDIV_CLK_DIV2_SEL enum
 */

typedef enum DCIO_DC_GENERIC_UNIPHY_FBDIV_CLK_DIV2_SEL {
DCIO_UNIPHYA_TEST_FBDIV_CLK_DIV2         = 0x00000000,
DCIO_UNIPHYB_TEST_FBDIV_CLK_DIV2         = 0x00000001,
DCIO_UNIPHYC_TEST_FBDIV_CLK_DIV2         = 0x00000002,
DCIO_UNIPHYD_TEST_FBDIV_CLK_DIV2         = 0x00000003,
DCIO_UNIPHYE_TEST_FBDIV_CLK_DIV2         = 0x00000004,
DCIO_UNIPHYF_TEST_FBDIV_CLK_DIV2         = 0x00000005,
DCIO_UNIPHYG_TEST_FBDIV_CLK_DIV2         = 0x00000006,
} DCIO_DC_GENERIC_UNIPHY_FBDIV_CLK_DIV2_SEL;

/*
 * DCIO_DC_GENERIC_UNIPHY_FBDIV_CLK_SEL enum
 */

typedef enum DCIO_DC_GENERIC_UNIPHY_FBDIV_CLK_SEL {
DCIO_UNIPHYA_FBDIV_CLK                   = 0x00000000,
DCIO_UNIPHYB_FBDIV_CLK                   = 0x00000001,
DCIO_UNIPHYC_FBDIV_CLK                   = 0x00000002,
DCIO_UNIPHYD_FBDIV_CLK                   = 0x00000003,
DCIO_UNIPHYE_FBDIV_CLK                   = 0x00000004,
DCIO_UNIPHYF_FBDIV_CLK                   = 0x00000005,
DCIO_UNIPHYG_FBDIV_CLK                   = 0x00000006,
} DCIO_DC_GENERIC_UNIPHY_FBDIV_CLK_SEL;

/*
 * DCIO_DC_GENERIC_UNIPHY_FBDIV_SSC_CLK_SEL enum
 */

typedef enum DCIO_DC_GENERIC_UNIPHY_FBDIV_SSC_CLK_SEL {
DCIO_UNIPHYA_FBDIV_SSC_CLK               = 0x00000000,
DCIO_UNIPHYB_FBDIV_SSC_CLK               = 0x00000001,
DCIO_UNIPHYC_FBDIV_SSC_CLK               = 0x00000002,
DCIO_UNIPHYD_FBDIV_SSC_CLK               = 0x00000003,
DCIO_UNIPHYE_FBDIV_SSC_CLK               = 0x00000004,
DCIO_UNIPHYF_FBDIV_SSC_CLK               = 0x00000005,
DCIO_UNIPHYG_FBDIV_SSC_CLK               = 0x00000006,
} DCIO_DC_GENERIC_UNIPHY_FBDIV_SSC_CLK_SEL;

/*
 * DCIO_DC_GENERIC_UNIPHY_REFDIV_CLK_SEL enum
 */

typedef enum DCIO_DC_GENERIC_UNIPHY_REFDIV_CLK_SEL {
DCIO_UNIPHYA_TEST_REFDIV_CLK             = 0x00000000,
DCIO_UNIPHYB_TEST_REFDIV_CLK             = 0x00000001,
DCIO_UNIPHYC_TEST_REFDIV_CLK             = 0x00000002,
DCIO_UNIPHYD_TEST_REFDIV_CLK             = 0x00000003,
DCIO_UNIPHYE_TEST_REFDIV_CLK             = 0x00000004,
DCIO_UNIPHYF_TEST_REFDIV_CLK             = 0x00000005,
DCIO_UNIPHYG_TEST_REFDIV_CLK             = 0x00000006,
} DCIO_DC_GENERIC_UNIPHY_REFDIV_CLK_SEL;

/*
 * DCIO_DC_GPIO_DEBUG_DPRX_LOOPBACK_ENABLE enum
 */

typedef enum DCIO_DC_GPIO_DEBUG_DPRX_LOOPBACK_ENABLE {
DCIO_DPRX_LOOPBACK_ENABLE_NORMAL         = 0x00000000,
DCIO_DPRX_LOOPBACK_ENABLE_LOOP           = 0x00000001,
} DCIO_DC_GPIO_DEBUG_DPRX_LOOPBACK_ENABLE;

/*
 * DCIO_DC_GPU_TIMER_READ_SELECT enum
 */

typedef enum DCIO_DC_GPU_TIMER_READ_SELECT {
DCIO_GPU_TIMER_READ_SELECT_LOWER_D1_V_UPDATE = 0x00000000,
DCIO_GPU_TIMER_READ_SELECT_UPPER_D1_V_UPDATE = 0x00000001,
DCIO_GPU_TIMER_READ_SELECT_LOWER_D1_P_FLIP = 0x00000002,
DCIO_GPU_TIMER_READ_SELECT_UPPER_D1_P_FLIP = 0x00000003,
DCIO_GPU_TIMER_READ_SELECT_LOWER_D1_VSYNC_NOM = 0x00000004,
DCIO_GPU_TIMER_READ_SELECT_UPPER_D1_VSYNC_NOM = 0x00000005,
} DCIO_DC_GPU_TIMER_READ_SELECT;

/*
 * DCIO_DC_GPU_TIMER_START_POSITION enum
 */

typedef enum DCIO_DC_GPU_TIMER_START_POSITION {
DCIO_GPU_TIMER_START_0_END_27            = 0x00000000,
DCIO_GPU_TIMER_START_1_END_28            = 0x00000001,
DCIO_GPU_TIMER_START_2_END_29            = 0x00000002,
DCIO_GPU_TIMER_START_3_END_30            = 0x00000003,
DCIO_GPU_TIMER_START_4_END_31            = 0x00000004,
DCIO_GPU_TIMER_START_6_END_33            = 0x00000005,
DCIO_GPU_TIMER_START_8_END_35            = 0x00000006,
DCIO_GPU_TIMER_START_10_END_37           = 0x00000007,
} DCIO_DC_GPU_TIMER_START_POSITION;

/*
 * DCIO_DC_REF_CLK_CNTL_GENLK_CLK_OUTPUT_SEL enum
 */

typedef enum DCIO_DC_REF_CLK_CNTL_GENLK_CLK_OUTPUT_SEL {
DCIO_GENLK_CLK_OUTPUT_SEL_DISABLE        = 0x00000000,
DCIO_GENLK_CLK_OUTPUT_SEL_PPLL1          = 0x00000001,
DCIO_GENLK_CLK_OUTPUT_SEL_PPLL2          = 0x00000002,
DCIO_GENLK_CLK_OUTPUT_SEL_RESERVED_VALUE3 = 0x00000003,
} DCIO_DC_REF_CLK_CNTL_GENLK_CLK_OUTPUT_SEL;

/*
 * DCIO_DC_REF_CLK_CNTL_HSYNCA_OUTPUT_SEL enum
 */

typedef enum DCIO_DC_REF_CLK_CNTL_HSYNCA_OUTPUT_SEL {
DCIO_HSYNCA_OUTPUT_SEL_DISABLE           = 0x00000000,
DCIO_HSYNCA_OUTPUT_SEL_PPLL1             = 0x00000001,
DCIO_HSYNCA_OUTPUT_SEL_PPLL2             = 0x00000002,
DCIO_HSYNCA_OUTPUT_SEL_RESERVED          = 0x00000003,
} DCIO_DC_REF_CLK_CNTL_HSYNCA_OUTPUT_SEL;

/*
 * DCIO_DIO_EXT_VSYNC_MASK enum
 */

typedef enum DCIO_DIO_EXT_VSYNC_MASK {
DCIO_EXT_VSYNC_MASK_NONE                 = 0x00000000,
DCIO_EXT_VSYNC_MASK_PIPE0                = 0x00000001,
DCIO_EXT_VSYNC_MASK_PIPE1                = 0x00000002,
DCIO_EXT_VSYNC_MASK_PIPE2                = 0x00000003,
DCIO_EXT_VSYNC_MASK_PIPE3                = 0x00000004,
DCIO_EXT_VSYNC_MASK_PIPE4                = 0x00000005,
DCIO_EXT_VSYNC_MASK_PIPE5                = 0x00000006,
DCIO_EXT_VSYNC_MASK_NONE_DUPLICATE       = 0x00000007,
} DCIO_DIO_EXT_VSYNC_MASK;

/*
 * DCIO_DIO_OTG_EXT_VSYNC_MUX enum
 */

typedef enum DCIO_DIO_OTG_EXT_VSYNC_MUX {
DCIO_EXT_VSYNC_MUX_SWAPLOCKB             = 0x00000000,
DCIO_EXT_VSYNC_MUX_OTG0                  = 0x00000001,
DCIO_EXT_VSYNC_MUX_OTG1                  = 0x00000002,
DCIO_EXT_VSYNC_MUX_OTG2                  = 0x00000003,
DCIO_EXT_VSYNC_MUX_OTG3                  = 0x00000004,
DCIO_EXT_VSYNC_MUX_OTG4                  = 0x00000005,
DCIO_EXT_VSYNC_MUX_OTG5                  = 0x00000006,
DCIO_EXT_VSYNC_MUX_GENERICB              = 0x00000007,
} DCIO_DIO_OTG_EXT_VSYNC_MUX;

/*
 * DCIO_DPCS_INTERRUPT_MASK enum
 */

typedef enum DCIO_DPCS_INTERRUPT_MASK {
DCIO_DPCS_INTERRUPT_DISABLE              = 0x00000000,
DCIO_DPCS_INTERRUPT_ENABLE               = 0x00000001,
} DCIO_DPCS_INTERRUPT_MASK;

/*
 * DCIO_DPCS_INTERRUPT_TYPE enum
 */

typedef enum DCIO_DPCS_INTERRUPT_TYPE {
DCIO_DPCS_INTERRUPT_TYPE_LEVEL_BASED     = 0x00000000,
DCIO_DPCS_INTERRUPT_TYPE_PULSE_BASED     = 0x00000001,
} DCIO_DPCS_INTERRUPT_TYPE;

/*
 * DCIO_DSYNC_SOFT_RESET enum
 */

typedef enum DCIO_DSYNC_SOFT_RESET {
DCIO_DSYNC_SOFT_RESET_DEASSERT           = 0x00000000,
DCIO_DSYNC_SOFT_RESET_ASSERT             = 0x00000001,
} DCIO_DSYNC_SOFT_RESET;

/*
 * DCIO_GENLK_CLK_GSL_MASK enum
 */

typedef enum DCIO_GENLK_CLK_GSL_MASK {
DCIO_GENLK_CLK_GSL_MASK_NO               = 0x00000000,
DCIO_GENLK_CLK_GSL_MASK_TIMING           = 0x00000001,
DCIO_GENLK_CLK_GSL_MASK_STEREO           = 0x00000002,
} DCIO_GENLK_CLK_GSL_MASK;

/*
 * DCIO_GENLK_VSYNC_GSL_MASK enum
 */

typedef enum DCIO_GENLK_VSYNC_GSL_MASK {
DCIO_GENLK_VSYNC_GSL_MASK_NO             = 0x00000000,
DCIO_GENLK_VSYNC_GSL_MASK_TIMING         = 0x00000001,
DCIO_GENLK_VSYNC_GSL_MASK_STEREO         = 0x00000002,
} DCIO_GENLK_VSYNC_GSL_MASK;

/*
 * DCIO_GSL_SEL enum
 */

typedef enum DCIO_GSL_SEL {
DCIO_GSL_SEL_GROUP_0                     = 0x00000000,
DCIO_GSL_SEL_GROUP_1                     = 0x00000001,
DCIO_GSL_SEL_GROUP_2                     = 0x00000002,
} DCIO_GSL_SEL;

/*
 * DCIO_PHY_HPO_ENC_SRC_SEL enum
 */

typedef enum DCIO_PHY_HPO_ENC_SRC_SEL {
HPO_SRC0                                 = 0x00000000,
HPO_SRC_RESERVED                         = 0x00000001,
} DCIO_PHY_HPO_ENC_SRC_SEL;

/*
 * DCIO_SWAPLOCK_A_GSL_MASK enum
 */

typedef enum DCIO_SWAPLOCK_A_GSL_MASK {
DCIO_SWAPLOCK_A_GSL_MASK_NO              = 0x00000000,
DCIO_SWAPLOCK_A_GSL_MASK_TIMING          = 0x00000001,
DCIO_SWAPLOCK_A_GSL_MASK_STEREO          = 0x00000002,
} DCIO_SWAPLOCK_A_GSL_MASK;

/*
 * DCIO_SWAPLOCK_B_GSL_MASK enum
 */

typedef enum DCIO_SWAPLOCK_B_GSL_MASK {
DCIO_SWAPLOCK_B_GSL_MASK_NO              = 0x00000000,
DCIO_SWAPLOCK_B_GSL_MASK_TIMING          = 0x00000001,
DCIO_SWAPLOCK_B_GSL_MASK_STEREO          = 0x00000002,
} DCIO_SWAPLOCK_B_GSL_MASK;

/*
 * DCIO_UNIPHY_CHANNEL_XBAR_SOURCE enum
 */

typedef enum DCIO_UNIPHY_CHANNEL_XBAR_SOURCE {
DCIO_UNIPHY_CHANNEL_XBAR_SOURCE_CH0      = 0x00000000,
DCIO_UNIPHY_CHANNEL_XBAR_SOURCE_CH1      = 0x00000001,
DCIO_UNIPHY_CHANNEL_XBAR_SOURCE_CH2      = 0x00000002,
DCIO_UNIPHY_CHANNEL_XBAR_SOURCE_CH3      = 0x00000003,
} DCIO_UNIPHY_CHANNEL_XBAR_SOURCE;

/*
 * DCIO_UNIPHY_IMPCAL_SEL enum
 */

typedef enum DCIO_UNIPHY_IMPCAL_SEL {
DCIO_UNIPHY_IMPCAL_SEL_TEMPERATURE       = 0x00000000,
DCIO_UNIPHY_IMPCAL_SEL_BINARY            = 0x00000001,
} DCIO_UNIPHY_IMPCAL_SEL;

/*
 * DCIO_UNIPHY_LINK_CNTL_CHANNEL_INVERT enum
 */

typedef enum DCIO_UNIPHY_LINK_CNTL_CHANNEL_INVERT {
DCIO_UNIPHY_CHANNEL_NO_INVERSION         = 0x00000000,
DCIO_UNIPHY_CHANNEL_INVERTED             = 0x00000001,
} DCIO_UNIPHY_LINK_CNTL_CHANNEL_INVERT;

/*
 * DCIO_UNIPHY_LINK_CNTL_ENABLE_HPD_MASK enum
 */

typedef enum DCIO_UNIPHY_LINK_CNTL_ENABLE_HPD_MASK {
DCIO_UNIPHY_LINK_ENABLE_HPD_MASK_DISALLOW = 0x00000000,
DCIO_UNIPHY_LINK_ENABLE_HPD_MASK_ALLOW   = 0x00000001,
DCIO_UNIPHY_LINK_ENABLE_HPD_MASK_ALLOW_DEBOUNCED = 0x00000002,
DCIO_UNIPHY_LINK_ENABLE_HPD_MASK_ALLOW_TOGGLE_FILTERED = 0x00000003,
} DCIO_UNIPHY_LINK_CNTL_ENABLE_HPD_MASK;

/*******************************************************
 * DCIO_CHIP Enums
 *******************************************************/

/*
 * DCIOCHIP_AUX_ALL_PWR_OK enum
 */

typedef enum DCIOCHIP_AUX_ALL_PWR_OK {
DCIOCHIP_AUX_ALL_PWR_OK_0                = 0x00000000,
DCIOCHIP_AUX_ALL_PWR_OK_1                = 0x00000001,
} DCIOCHIP_AUX_ALL_PWR_OK;

/*
 * DCIOCHIP_AUX_CSEL0P9 enum
 */

typedef enum DCIOCHIP_AUX_CSEL0P9 {
DCIOCHIP_AUX_CSEL_DEC1P0                 = 0x00000000,
DCIOCHIP_AUX_CSEL_DEC0P9                 = 0x00000001,
} DCIOCHIP_AUX_CSEL0P9;

/*
 * DCIOCHIP_AUX_CSEL1P1 enum
 */

typedef enum DCIOCHIP_AUX_CSEL1P1 {
DCIOCHIP_AUX_CSEL_INC1P0                 = 0x00000000,
DCIOCHIP_AUX_CSEL_INC1P1                 = 0x00000001,
} DCIOCHIP_AUX_CSEL1P1;

/*
 * DCIOCHIP_AUX_FALLSLEWSEL enum
 */

typedef enum DCIOCHIP_AUX_FALLSLEWSEL {
DCIOCHIP_AUX_FALLSLEWSEL_LOW             = 0x00000000,
DCIOCHIP_AUX_FALLSLEWSEL_HIGH0           = 0x00000001,
DCIOCHIP_AUX_FALLSLEWSEL_HIGH1           = 0x00000002,
DCIOCHIP_AUX_FALLSLEWSEL_ULTRAHIGH       = 0x00000003,
} DCIOCHIP_AUX_FALLSLEWSEL;

/*
 * DCIOCHIP_AUX_HYS_TUNE enum
 */

typedef enum DCIOCHIP_AUX_HYS_TUNE {
DCIOCHIP_AUX_HYS_TUNE_0                  = 0x00000000,
DCIOCHIP_AUX_HYS_TUNE_1                  = 0x00000001,
DCIOCHIP_AUX_HYS_TUNE_2                  = 0x00000002,
DCIOCHIP_AUX_HYS_TUNE_3                  = 0x00000003,
} DCIOCHIP_AUX_HYS_TUNE;

/*
 * DCIOCHIP_AUX_RECEIVER_SEL enum
 */

typedef enum DCIOCHIP_AUX_RECEIVER_SEL {
DCIOCHIP_AUX_RECEIVER_SEL_0              = 0x00000000,
DCIOCHIP_AUX_RECEIVER_SEL_1              = 0x00000001,
DCIOCHIP_AUX_RECEIVER_SEL_2              = 0x00000002,
DCIOCHIP_AUX_RECEIVER_SEL_3              = 0x00000003,
} DCIOCHIP_AUX_RECEIVER_SEL;

/*
 * DCIOCHIP_AUX_RSEL0P9 enum
 */

typedef enum DCIOCHIP_AUX_RSEL0P9 {
DCIOCHIP_AUX_RSEL_DEC1P0                 = 0x00000000,
DCIOCHIP_AUX_RSEL_DEC0P9                 = 0x00000001,
} DCIOCHIP_AUX_RSEL0P9;

/*
 * DCIOCHIP_AUX_RSEL1P1 enum
 */

typedef enum DCIOCHIP_AUX_RSEL1P1 {
DCIOCHIP_AUX_RSEL_INC1P0                 = 0x00000000,
DCIOCHIP_AUX_RSEL_INC1P1                 = 0x00000001,
} DCIOCHIP_AUX_RSEL1P1;

/*
 * DCIOCHIP_AUX_SPIKESEL enum
 */

typedef enum DCIOCHIP_AUX_SPIKESEL {
DCIOCHIP_AUX_SPIKESEL_50NS               = 0x00000000,
DCIOCHIP_AUX_SPIKESEL_10NS               = 0x00000001,
} DCIOCHIP_AUX_SPIKESEL;

/*
 * DCIOCHIP_AUX_VOD_TUNE enum
 */

typedef enum DCIOCHIP_AUX_VOD_TUNE {
DCIOCHIP_AUX_VOD_TUNE_0                  = 0x00000000,
DCIOCHIP_AUX_VOD_TUNE_1                  = 0x00000001,
DCIOCHIP_AUX_VOD_TUNE_2                  = 0x00000002,
DCIOCHIP_AUX_VOD_TUNE_3                  = 0x00000003,
} DCIOCHIP_AUX_VOD_TUNE;

/*
 * DCIOCHIP_GPIO_MASK_EN enum
 */

typedef enum DCIOCHIP_GPIO_MASK_EN {
DCIOCHIP_GPIO_MASK_EN_HARDWARE           = 0x00000000,
DCIOCHIP_GPIO_MASK_EN_SOFTWARE           = 0x00000001,
} DCIOCHIP_GPIO_MASK_EN;

/*
 * DCIOCHIP_HPD_SEL enum
 */

typedef enum DCIOCHIP_HPD_SEL {
DCIOCHIP_HPD_SEL_ASYNC                   = 0x00000000,
DCIOCHIP_HPD_SEL_CLOCKED                 = 0x00000001,
} DCIOCHIP_HPD_SEL;

/*
 * DCIOCHIP_I2C_COMPSEL enum
 */

typedef enum DCIOCHIP_I2C_COMPSEL {
DCIOCHIP_I2C_REC_SCHMIT                  = 0x00000000,
DCIOCHIP_I2C_REC_COMPARATOR              = 0x00000001,
} DCIOCHIP_I2C_COMPSEL;

/*
 * DCIOCHIP_I2C_FALLSLEWSEL enum
 */

typedef enum DCIOCHIP_I2C_FALLSLEWSEL {
DCIOCHIP_I2C_FALLSLEWSEL_00              = 0x00000000,
DCIOCHIP_I2C_FALLSLEWSEL_01              = 0x00000001,
DCIOCHIP_I2C_FALLSLEWSEL_10              = 0x00000002,
DCIOCHIP_I2C_FALLSLEWSEL_11              = 0x00000003,
} DCIOCHIP_I2C_FALLSLEWSEL;

/*
 * DCIOCHIP_I2C_RECEIVER_SEL enum
 */

typedef enum DCIOCHIP_I2C_RECEIVER_SEL {
DCIOCHIP_I2C_RECEIVER_SEL_0              = 0x00000000,
DCIOCHIP_I2C_RECEIVER_SEL_1              = 0x00000001,
DCIOCHIP_I2C_RECEIVER_SEL_2              = 0x00000002,
DCIOCHIP_I2C_RECEIVER_SEL_3              = 0x00000003,
} DCIOCHIP_I2C_RECEIVER_SEL;

/*
 * DCIOCHIP_I2C_VPH_1V2_EN enum
 */

typedef enum DCIOCHIP_I2C_VPH_1V2_EN {
DCIOCHIP_I2C_VPH_1V2_EN_0                = 0x00000000,
DCIOCHIP_I2C_VPH_1V2_EN_1                = 0x00000001,
} DCIOCHIP_I2C_VPH_1V2_EN;

/*
 * DCIOCHIP_INVERT enum
 */

typedef enum DCIOCHIP_INVERT {
DCIOCHIP_POL_NON_INVERT                  = 0x00000000,
DCIOCHIP_POL_INVERT                      = 0x00000001,
} DCIOCHIP_INVERT;

/*
 * DCIOCHIP_MASK enum
 */

typedef enum DCIOCHIP_MASK {
DCIOCHIP_MASK_DISABLE                    = 0x00000000,
DCIOCHIP_MASK_ENABLE                     = 0x00000001,
} DCIOCHIP_MASK;

/*
 * DCIOCHIP_PAD_MODE enum
 */

typedef enum DCIOCHIP_PAD_MODE {
DCIOCHIP_PAD_MODE_DDC                    = 0x00000000,
DCIOCHIP_PAD_MODE_DP                     = 0x00000001,
} DCIOCHIP_PAD_MODE;

/*
 * DCIOCHIP_PD_EN enum
 */

typedef enum DCIOCHIP_PD_EN {
DCIOCHIP_PD_EN_NOTALLOW                  = 0x00000000,
DCIOCHIP_PD_EN_ALLOW                     = 0x00000001,
} DCIOCHIP_PD_EN;

/*
 * DCIOCHIP_REF_27_SRC_SEL enum
 */

typedef enum DCIOCHIP_REF_27_SRC_SEL {
DCIOCHIP_REF_27_SRC_SEL_XTAL_DIVIDER     = 0x00000000,
DCIOCHIP_REF_27_SRC_SEL_DISP_CLKIN2_DIVIDER = 0x00000001,
DCIOCHIP_REF_27_SRC_SEL_XTAL_BYPASS      = 0x00000002,
DCIOCHIP_REF_27_SRC_SEL_DISP_CLKIN2_BYPASS = 0x00000003,
} DCIOCHIP_REF_27_SRC_SEL;

/*******************************************************
 * PWRSEQ Enums
 *******************************************************/

/*
 * PWRSEQ_BL_PWM_CNTL2_BL_PWM_OVERRIDE_BL_OUT_ENABLE enum
 */

typedef enum PWRSEQ_BL_PWM_CNTL2_BL_PWM_OVERRIDE_BL_OUT_ENABLE {
PWRSEQ_BL_PWM_OVERRIDE_BL_OUT_DISABLE    = 0x00000000,
PWRSEQ_BL_PWM_OVERRIDE_BL_OUT_ENABLE     = 0x00000001,
} PWRSEQ_BL_PWM_CNTL2_BL_PWM_OVERRIDE_BL_OUT_ENABLE;

/*
 * PWRSEQ_BL_PWM_CNTL2_BL_PWM_OVERRIDE_PANEL_PWRSEQ_EN enum
 */

typedef enum PWRSEQ_BL_PWM_CNTL2_BL_PWM_OVERRIDE_PANEL_PWRSEQ_EN {
PWRSEQ_BL_PWM_OVERRIDE_PANEL_PWRSEQ_EN_NORMAL = 0x00000000,
PWRSEQ_BL_PWM_OVERRIDE_PANEL_PWRSEQ_EN_PWM = 0x00000001,
} PWRSEQ_BL_PWM_CNTL2_BL_PWM_OVERRIDE_PANEL_PWRSEQ_EN;

/*
 * PWRSEQ_BL_PWM_CNTL2_DBG_BL_PWM_INPUT_REFCLK_SELECT enum
 */

typedef enum PWRSEQ_BL_PWM_CNTL2_DBG_BL_PWM_INPUT_REFCLK_SELECT {
PWRSEQ_DBG_BL_PWM_INPUT_REFCLK_SELECT_NORMAL = 0x00000000,
PWRSEQ_DBG_BL_PWM_INPUT_REFCLK_SELECT_DEBUG1 = 0x00000001,
PWRSEQ_DBG_BL_PWM_INPUT_REFCLK_SELECT_DEBUG2 = 0x00000002,
PWRSEQ_DBG_BL_PWM_INPUT_REFCLK_SELECT_DEBUG3 = 0x00000003,
} PWRSEQ_BL_PWM_CNTL2_DBG_BL_PWM_INPUT_REFCLK_SELECT;

/*
 * PWRSEQ_BL_PWM_CNTL_BL_PWM_EN enum
 */

typedef enum PWRSEQ_BL_PWM_CNTL_BL_PWM_EN {
PWRSEQ_BL_PWM_DISABLE                    = 0x00000000,
PWRSEQ_BL_PWM_ENABLE                     = 0x00000001,
} PWRSEQ_BL_PWM_CNTL_BL_PWM_EN;

/*
 * PWRSEQ_BL_PWM_CNTL_BL_PWM_FRACTIONAL_EN enum
 */

typedef enum PWRSEQ_BL_PWM_CNTL_BL_PWM_FRACTIONAL_EN {
PWRSEQ_BL_PWM_FRACTIONAL_DISABLE         = 0x00000000,
PWRSEQ_BL_PWM_FRACTIONAL_ENABLE          = 0x00000001,
} PWRSEQ_BL_PWM_CNTL_BL_PWM_FRACTIONAL_EN;

/*
 * PWRSEQ_BL_PWM_GRP1_IGNORE_MASTER_LOCK_EN enum
 */

typedef enum PWRSEQ_BL_PWM_GRP1_IGNORE_MASTER_LOCK_EN {
PWRSEQ_BL_PWM_GRP1_IGNORE_MASTER_LOCK_ENABLE = 0x00000000,
PWRSEQ_BL_PWM_GRP1_IGNORE_MASTER_LOCK_DISABLE = 0x00000001,
} PWRSEQ_BL_PWM_GRP1_IGNORE_MASTER_LOCK_EN;

/*
 * PWRSEQ_BL_PWM_GRP1_READBACK_DB_REG_VALUE_EN enum
 */

typedef enum PWRSEQ_BL_PWM_GRP1_READBACK_DB_REG_VALUE_EN {
PWRSEQ_BL_PWM_GRP1_READBACK_DB_REG_VALUE_EN_BL_PWM = 0x00000000,
PWRSEQ_BL_PWM_GRP1_READBACK_DB_REG_VALUE_EN_BL1_PWM = 0x00000001,
} PWRSEQ_BL_PWM_GRP1_READBACK_DB_REG_VALUE_EN;

/*
 * PWRSEQ_BL_PWM_GRP1_REG_LOCK enum
 */

typedef enum PWRSEQ_BL_PWM_GRP1_REG_LOCK {
PWRSEQ_BL_PWM_GRP1_REG_LOCK_DISABLE      = 0x00000000,
PWRSEQ_BL_PWM_GRP1_REG_LOCK_ENABLE       = 0x00000001,
} PWRSEQ_BL_PWM_GRP1_REG_LOCK;

/*
 * PWRSEQ_BL_PWM_GRP1_UPDATE_AT_FRAME_START enum
 */

typedef enum PWRSEQ_BL_PWM_GRP1_UPDATE_AT_FRAME_START {
PWRSEQ_BL_PWM_GRP1_UPDATE_AT_FRAME_START_DISABLE = 0x00000000,
PWRSEQ_BL_PWM_GRP1_UPDATE_AT_FRAME_START_ENABLE = 0x00000001,
} PWRSEQ_BL_PWM_GRP1_UPDATE_AT_FRAME_START;

/*
 * PWRSEQ_GPIO_MASK_EN enum
 */

typedef enum PWRSEQ_GPIO_MASK_EN {
PWRSEQ_GPIO_MASK_EN_HARDWARE             = 0x00000000,
PWRSEQ_GPIO_MASK_EN_SOFTWARE             = 0x00000001,
} PWRSEQ_GPIO_MASK_EN;

/*
 * PWRSEQ_PANEL_PWRSEQ_CNTL_PANEL_BLON enum
 */

typedef enum PWRSEQ_PANEL_PWRSEQ_CNTL_PANEL_BLON {
PWRSEQ_PANEL_BLON_OFF                    = 0x00000000,
PWRSEQ_PANEL_BLON_ON                     = 0x00000001,
} PWRSEQ_PANEL_PWRSEQ_CNTL_PANEL_BLON;

/*
 * PWRSEQ_PANEL_PWRSEQ_CNTL_PANEL_BLON_POL enum
 */

typedef enum PWRSEQ_PANEL_PWRSEQ_CNTL_PANEL_BLON_POL {
PWRSEQ_PANEL_BLON_POL_NON_INVERT         = 0x00000000,
PWRSEQ_PANEL_BLON_POL_INVERT             = 0x00000001,
} PWRSEQ_PANEL_PWRSEQ_CNTL_PANEL_BLON_POL;

/*
 * PWRSEQ_PANEL_PWRSEQ_CNTL_PANEL_DIGON enum
 */

typedef enum PWRSEQ_PANEL_PWRSEQ_CNTL_PANEL_DIGON {
PWRSEQ_PANEL_DIGON_OFF                   = 0x00000000,
PWRSEQ_PANEL_DIGON_ON                    = 0x00000001,
} PWRSEQ_PANEL_PWRSEQ_CNTL_PANEL_DIGON;

/*
 * PWRSEQ_PANEL_PWRSEQ_CNTL_PANEL_DIGON_POL enum
 */

typedef enum PWRSEQ_PANEL_PWRSEQ_CNTL_PANEL_DIGON_POL {
PWRSEQ_PANEL_DIGON_POL_NON_INVERT        = 0x00000000,
PWRSEQ_PANEL_DIGON_POL_INVERT            = 0x00000001,
} PWRSEQ_PANEL_PWRSEQ_CNTL_PANEL_DIGON_POL;

/*
 * PWRSEQ_PANEL_PWRSEQ_CNTL_PANEL_SYNCEN_POL enum
 */

typedef enum PWRSEQ_PANEL_PWRSEQ_CNTL_PANEL_SYNCEN_POL {
PWRSEQ_PANEL_SYNCEN_POL_NON_INVERT       = 0x00000000,
PWRSEQ_PANEL_SYNCEN_POL_INVERT           = 0x00000001,
} PWRSEQ_PANEL_PWRSEQ_CNTL_PANEL_SYNCEN_POL;

/*
 * PWRSEQ_PANEL_PWRSEQ_CNTL_TARGET_STATE enum
 */

typedef enum PWRSEQ_PANEL_PWRSEQ_CNTL_TARGET_STATE {
PWRSEQ_PANEL_PWRSEQ_TARGET_STATE_LCD_OFF = 0x00000000,
PWRSEQ_PANEL_PWRSEQ_TARGET_STATE_LCD_ON  = 0x00000001,
} PWRSEQ_PANEL_PWRSEQ_CNTL_TARGET_STATE;

/*
 * PWRSEQ_PANEL_PWRSEQ_DELAY2_PANEL_VARY_BL_OVERRIDE_EN enum
 */

typedef enum PWRSEQ_PANEL_PWRSEQ_DELAY2_PANEL_VARY_BL_OVERRIDE_EN {
PWRSEQ_PANEL_VARY_BL_OVERRIDE_EN_BLON    = 0x00000000,
PWRSEQ_PANEL_VARY_BL_OVERRIDE_EN_SEPARATE = 0x00000001,
} PWRSEQ_PANEL_PWRSEQ_DELAY2_PANEL_VARY_BL_OVERRIDE_EN;

/*******************************************************
 * AZCONTROLLER Enums
 *******************************************************/

/*
 * AZ_CORB_SIZE enum
 */

typedef enum AZ_CORB_SIZE {
AZ_CORB_SIZE_2ENTRIES_RESERVED           = 0x00000000,
AZ_CORB_SIZE_16ENTRIES_RESERVED          = 0x00000001,
AZ_CORB_SIZE_256ENTRIES                  = 0x00000002,
AZ_CORB_SIZE_RESERVED                    = 0x00000003,
} AZ_CORB_SIZE;

/*
 * AZ_GLOBAL_CAPABILITIES enum
 */

typedef enum AZ_GLOBAL_CAPABILITIES {
AZ_GLOBAL_CAPABILITIES_SIXTY_FOUR_BIT_ADDRESS_NOT_SUPPORTED = 0x00000000,
AZ_GLOBAL_CAPABILITIES_SIXTY_FOUR_BIT_ADDRESS_SUPPORTED = 0x00000001,
} AZ_GLOBAL_CAPABILITIES;

/*
 * AZ_RIRB_SIZE enum
 */

typedef enum AZ_RIRB_SIZE {
AZ_RIRB_SIZE_2ENTRIES_RESERVED           = 0x00000000,
AZ_RIRB_SIZE_16ENTRIES_RESERVED          = 0x00000001,
AZ_RIRB_SIZE_256ENTRIES                  = 0x00000002,
AZ_RIRB_SIZE_UNDEFINED                   = 0x00000003,
} AZ_RIRB_SIZE;

/*
 * AZ_RIRB_WRITE_POINTER_RESET enum
 */

typedef enum AZ_RIRB_WRITE_POINTER_RESET {
AZ_RIRB_WRITE_POINTER_NOT_RESET          = 0x00000000,
AZ_RIRB_WRITE_POINTER_DO_RESET           = 0x00000001,
} AZ_RIRB_WRITE_POINTER_RESET;

/*
 * AZ_STATE_CHANGE_STATUS enum
 */

typedef enum AZ_STATE_CHANGE_STATUS {
AZ_STATE_CHANGE_STATUS_CODEC_NOT_PRESENT = 0x00000000,
AZ_STATE_CHANGE_STATUS_CODEC_PRESENT     = 0x00000001,
} AZ_STATE_CHANGE_STATUS;

/*
 * CORB_READ_POINTER_RESET enum
 */

typedef enum CORB_READ_POINTER_RESET {
CORB_READ_POINTER_RESET_CORB_DMA_IS_NOT_RESET = 0x00000000,
CORB_READ_POINTER_RESET_CORB_DMA_IS_RESET = 0x00000001,
} CORB_READ_POINTER_RESET;

/*
 * DMA_POSITION_LOWER_BASE_ADDRESS_BUFFER_ENABLE enum
 */

typedef enum DMA_POSITION_LOWER_BASE_ADDRESS_BUFFER_ENABLE {
DMA_POSITION_LOWER_BASE_ADDRESS_BUFFER_ENABLE_DMA_DISABLE = 0x00000000,
DMA_POSITION_LOWER_BASE_ADDRESS_BUFFER_ENABLE_DMA_ENABLE = 0x00000001,
} DMA_POSITION_LOWER_BASE_ADDRESS_BUFFER_ENABLE;

/*
 * GENERIC_AZ_CONTROLLER_REGISTER_ENABLE_CONTROL enum
 */

typedef enum GENERIC_AZ_CONTROLLER_REGISTER_ENABLE_CONTROL {
GENERIC_AZ_CONTROLLER_REGISTER_DISABLE   = 0x00000000,
GENERIC_AZ_CONTROLLER_REGISTER_ENABLE    = 0x00000001,
} GENERIC_AZ_CONTROLLER_REGISTER_ENABLE_CONTROL;

/*
 * GENERIC_AZ_CONTROLLER_REGISTER_ENABLE_CONTROL_RESERVED enum
 */

typedef enum GENERIC_AZ_CONTROLLER_REGISTER_ENABLE_CONTROL_RESERVED {
GENERIC_AZ_CONTROLLER_REGISTER_DISABLE_RESERVED = 0x00000000,
GENERIC_AZ_CONTROLLER_REGISTER_ENABLE_RESERVED = 0x00000001,
} GENERIC_AZ_CONTROLLER_REGISTER_ENABLE_CONTROL_RESERVED;

/*
 * GENERIC_AZ_CONTROLLER_REGISTER_STATUS enum
 */

typedef enum GENERIC_AZ_CONTROLLER_REGISTER_STATUS {
GENERIC_AZ_CONTROLLER_REGISTER_STATUS_NOT_SET = 0x00000000,
GENERIC_AZ_CONTROLLER_REGISTER_STATUS_SET = 0x00000001,
} GENERIC_AZ_CONTROLLER_REGISTER_STATUS;

/*
 * GENERIC_AZ_CONTROLLER_REGISTER_STATUS_RESERVED enum
 */

typedef enum GENERIC_AZ_CONTROLLER_REGISTER_STATUS_RESERVED {
GENERIC_AZ_CONTROLLER_REGISTER_STATUS_NOT_SET_RESERVED = 0x00000000,
GENERIC_AZ_CONTROLLER_REGISTER_STATUS_SET_RESERVED = 0x00000001,
} GENERIC_AZ_CONTROLLER_REGISTER_STATUS_RESERVED;

/*
 * GLOBAL_CONTROL_ACCEPT_UNSOLICITED_RESPONSE enum
 */

typedef enum GLOBAL_CONTROL_ACCEPT_UNSOLICITED_RESPONSE {
ACCEPT_UNSOLICITED_RESPONSE_NOT_ENABLE   = 0x00000000,
ACCEPT_UNSOLICITED_RESPONSE_ENABLE       = 0x00000001,
} GLOBAL_CONTROL_ACCEPT_UNSOLICITED_RESPONSE;

/*
 * GLOBAL_CONTROL_CONTROLLER_RESET enum
 */

typedef enum GLOBAL_CONTROL_CONTROLLER_RESET {
CONTROLLER_RESET_AZ_CONTROLLER_IN_RESET  = 0x00000000,
CONTROLLER_RESET_AZ_CONTROLLER_NOT_IN_RESET = 0x00000001,
} GLOBAL_CONTROL_CONTROLLER_RESET;

/*
 * GLOBAL_CONTROL_FLUSH_CONTROL enum
 */

typedef enum GLOBAL_CONTROL_FLUSH_CONTROL {
FLUSH_CONTROL_FLUSH_NOT_STARTED          = 0x00000000,
FLUSH_CONTROL_FLUSH_STARTED              = 0x00000001,
} GLOBAL_CONTROL_FLUSH_CONTROL;

/*
 * GLOBAL_STATUS_FLUSH_STATUS enum
 */

typedef enum GLOBAL_STATUS_FLUSH_STATUS {
GLOBAL_STATUS_FLUSH_STATUS_FLUSH_NOT_ENDED = 0x00000000,
GLOBAL_STATUS_FLUSH_STATUS_FLUSH_ENDED   = 0x00000001,
} GLOBAL_STATUS_FLUSH_STATUS;

/*
 * IMMEDIATE_COMMAND_STATUS_IMMEDIATE_COMMAND_BUSY enum
 */

typedef enum IMMEDIATE_COMMAND_STATUS_IMMEDIATE_COMMAND_BUSY {
IMMEDIATE_COMMAND_STATUS_IMMEDIATE_COMMAND_NOT_BUSY = 0x00000000,
IMMEDIATE_COMMAND_STATUS_IMMEDIATE_COMMAND_IS_BUSY = 0x00000001,
} IMMEDIATE_COMMAND_STATUS_IMMEDIATE_COMMAND_BUSY;

/*
 * IMMEDIATE_COMMAND_STATUS_IMMEDIATE_RESULT_VALID enum
 */

typedef enum IMMEDIATE_COMMAND_STATUS_IMMEDIATE_RESULT_VALID {
IMMEDIATE_COMMAND_STATUS_IMMEDIATE_RESULT_VALID_NO_IMMEDIATE_RESPONSE_VALID = 0x00000000,
IMMEDIATE_COMMAND_STATUS_IMMEDIATE_RESULT_VALID_IMMEDIATE_RESPONSE_VALID = 0x00000001,
} IMMEDIATE_COMMAND_STATUS_IMMEDIATE_RESULT_VALID;

/*
 * RIRB_CONTROL_RESPONSE_INTERRUPT_CONTROL enum
 */

typedef enum RIRB_CONTROL_RESPONSE_INTERRUPT_CONTROL {
RIRB_CONTROL_RESPONSE_INTERRUPT_CONTROL_INTERRUPT_DISABLED = 0x00000000,
RIRB_CONTROL_RESPONSE_INTERRUPT_CONTROL_INTERRUPT_ENABLED = 0x00000001,
} RIRB_CONTROL_RESPONSE_INTERRUPT_CONTROL;

/*
 * RIRB_CONTROL_RESPONSE_OVERRUN_INTERRUPT_CONTROL enum
 */

typedef enum RIRB_CONTROL_RESPONSE_OVERRUN_INTERRUPT_CONTROL {
RIRB_CONTROL_RESPONSE_OVERRUN_INTERRUPT_CONTROL_INTERRUPT_DISABLED = 0x00000000,
RIRB_CONTROL_RESPONSE_OVERRUN_INTERRUPT_CONTROL_INTERRUPT_ENABLED = 0x00000001,
} RIRB_CONTROL_RESPONSE_OVERRUN_INTERRUPT_CONTROL;

/*
 * STREAM_0_SYNCHRONIZATION enum
 */

typedef enum STREAM_0_SYNCHRONIZATION {
STREAM_0_SYNCHRONIZATION_STEAM_NOT_STOPPED = 0x00000000,
STREAM_0_SYNCHRONIZATION_STEAM_STOPPED   = 0x00000001,
} STREAM_0_SYNCHRONIZATION;

/*
 * STREAM_10_SYNCHRONIZATION enum
 */

typedef enum STREAM_10_SYNCHRONIZATION {
STREAM_10_SYNCHRONIZATION_STEAM_NOT_STOPPED_RESERVED = 0x00000000,
STREAM_10_SYNCHRONIZATION_STEAM_STOPPED_RESERVED = 0x00000001,
} STREAM_10_SYNCHRONIZATION;

/*
 * STREAM_11_SYNCHRONIZATION enum
 */

typedef enum STREAM_11_SYNCHRONIZATION {
STREAM_11_SYNCHRONIZATION_STEAM_NOT_STOPPED_RESERVED = 0x00000000,
STREAM_11_SYNCHRONIZATION_STEAM_STOPPED_RESERVED = 0x00000001,
} STREAM_11_SYNCHRONIZATION;

/*
 * STREAM_12_SYNCHRONIZATION enum
 */

typedef enum STREAM_12_SYNCHRONIZATION {
STREAM_12_SYNCHRONIZATION_STEAM_NOT_STOPPED_RESERVED = 0x00000000,
STREAM_12_SYNCHRONIZATION_STEAM_STOPPED_RESERVED = 0x00000001,
} STREAM_12_SYNCHRONIZATION;

/*
 * STREAM_13_SYNCHRONIZATION enum
 */

typedef enum STREAM_13_SYNCHRONIZATION {
STREAM_13_SYNCHRONIZATION_STEAM_NOT_STOPPED_RESERVED = 0x00000000,
STREAM_13_SYNCHRONIZATION_STEAM_STOPPED_RESERVED = 0x00000001,
} STREAM_13_SYNCHRONIZATION;

/*
 * STREAM_14_SYNCHRONIZATION enum
 */

typedef enum STREAM_14_SYNCHRONIZATION {
STREAM_14_SYNCHRONIZATION_STEAM_NOT_STOPPED_RESERVED = 0x00000000,
STREAM_14_SYNCHRONIZATION_STEAM_STOPPED_RESERVED = 0x00000001,
} STREAM_14_SYNCHRONIZATION;

/*
 * STREAM_15_SYNCHRONIZATION enum
 */

typedef enum STREAM_15_SYNCHRONIZATION {
STREAM_15_SYNCHRONIZATION_STEAM_NOT_STOPPED_RESERVED = 0x00000000,
STREAM_15_SYNCHRONIZATION_STEAM_STOPPED_RESERVED = 0x00000001,
} STREAM_15_SYNCHRONIZATION;

/*
 * STREAM_1_SYNCHRONIZATION enum
 */

typedef enum STREAM_1_SYNCHRONIZATION {
STREAM_1_SYNCHRONIZATION_STEAM_NOT_STOPPED = 0x00000000,
STREAM_1_SYNCHRONIZATION_STEAM_STOPPED   = 0x00000001,
} STREAM_1_SYNCHRONIZATION;

/*
 * STREAM_2_SYNCHRONIZATION enum
 */

typedef enum STREAM_2_SYNCHRONIZATION {
STREAM_2_SYNCHRONIZATION_STEAM_NOT_STOPPED = 0x00000000,
STREAM_2_SYNCHRONIZATION_STEAM_STOPPED   = 0x00000001,
} STREAM_2_SYNCHRONIZATION;

/*
 * STREAM_3_SYNCHRONIZATION enum
 */

typedef enum STREAM_3_SYNCHRONIZATION {
STREAM_3_SYNCHRONIZATION_STEAM_NOT_STOPPED = 0x00000000,
STREAM_3_SYNCHRONIZATION_STEAM_STOPPED   = 0x00000001,
} STREAM_3_SYNCHRONIZATION;

/*
 * STREAM_4_SYNCHRONIZATION enum
 */

typedef enum STREAM_4_SYNCHRONIZATION {
STREAM_4_SYNCHRONIZATION_STEAM_NOT_STOPPED_RESERVED = 0x00000000,
STREAM_4_SYNCHRONIZATION_STEAM_STOPPED_RESERVED = 0x00000001,
} STREAM_4_SYNCHRONIZATION;

/*
 * STREAM_5_SYNCHRONIZATION enum
 */

typedef enum STREAM_5_SYNCHRONIZATION {
STREAM_5_SYNCHRONIZATION_STEAM_NOT_STOPPED_RESERVED = 0x00000000,
STREAM_5_SYNCHRONIZATION_STEAM_STOPPED_RESERVED = 0x00000001,
} STREAM_5_SYNCHRONIZATION;

/*
 * STREAM_6_SYNCHRONIZATION enum
 */

typedef enum STREAM_6_SYNCHRONIZATION {
STREAM_6_SYNCHRONIZATION_STEAM_NOT_STOPPED_RESERVED = 0x00000000,
STREAM_6_SYNCHRONIZATION_STEAM_STOPPED_RESERVED = 0x00000001,
} STREAM_6_SYNCHRONIZATION;

/*
 * STREAM_7_SYNCHRONIZATION enum
 */

typedef enum STREAM_7_SYNCHRONIZATION {
STREAM_7_SYNCHRONIZATION_STEAM_NOT_STOPPED_RESERVED = 0x00000000,
STREAM_7_SYNCHRONIZATION_STEAM_STOPPED_RESERVED = 0x00000001,
} STREAM_7_SYNCHRONIZATION;

/*
 * STREAM_8_SYNCHRONIZATION enum
 */

typedef enum STREAM_8_SYNCHRONIZATION {
STREAM_8_SYNCHRONIZATION_STEAM_NOT_STOPPED_RESERVED = 0x00000000,
STREAM_8_SYNCHRONIZATION_STEAM_STOPPED_RESERVED = 0x00000001,
} STREAM_8_SYNCHRONIZATION;

/*
 * STREAM_9_SYNCHRONIZATION enum
 */

typedef enum STREAM_9_SYNCHRONIZATION {
STREAM_9_SYNCHRONIZATION_STEAM_NOT_STOPPED_RESERVED = 0x00000000,
STREAM_9_SYNCHRONIZATION_STEAM_STOPPED_RESERVED = 0x00000001,
} STREAM_9_SYNCHRONIZATION;

/*******************************************************
 * AZENDPOINT Enums
 *******************************************************/

/*
 * AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_BITS_PER_SAMPLE enum
 */

typedef enum AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_BITS_PER_SAMPLE {
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_BITS_PER_SAMPLE_8_RESERVED = 0x00000000,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_BITS_PER_SAMPLE_16 = 0x00000001,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_BITS_PER_SAMPLE_20 = 0x00000002,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_BITS_PER_SAMPLE_24 = 0x00000003,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_BITS_PER_SAMPLE_32_RESERVED = 0x00000004,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_BITS_PER_SAMPLE_RESERVED = 0x00000005,
} AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_BITS_PER_SAMPLE;

/*
 * AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_NUMBER_OF_CHANNELS enum
 */

typedef enum AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_NUMBER_OF_CHANNELS {
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_NUMBER_OF_CHANNELS_1 = 0x00000000,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_NUMBER_OF_CHANNELS_2 = 0x00000001,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_NUMBER_OF_CHANNELS_3 = 0x00000002,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_NUMBER_OF_CHANNELS_4 = 0x00000003,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_NUMBER_OF_CHANNELS_5 = 0x00000004,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_NUMBER_OF_CHANNELS_6 = 0x00000005,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_NUMBER_OF_CHANNELS_7 = 0x00000006,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_NUMBER_OF_CHANNELS_8 = 0x00000007,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_NUMBER_OF_CHANNELS_RESERVED = 0x00000008,
} AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_NUMBER_OF_CHANNELS;

/*
 * AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_DIVISOR enum
 */

typedef enum AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_DIVISOR {
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_DIVISOR_BY1 = 0x00000000,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_DIVISOR_BY2_RESERVED = 0x00000001,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_DIVISOR_BY3 = 0x00000002,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_DIVISOR_BY4_RESERVED = 0x00000003,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_DIVISOR_BY5_RESERVED = 0x00000004,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_DIVISOR_BY6_RESERVED = 0x00000005,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_DIVISOR_BY7_RESERVED = 0x00000006,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_DIVISOR_BY8_RESERVED = 0x00000007,
} AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_DIVISOR;

/*
 * AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_MULTIPLE enum
 */

typedef enum AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_MULTIPLE {
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_MULTIPLE_BY1 = 0x00000000,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_MULTIPLE_BY2 = 0x00000001,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_MULTIPLE_BY3_RESERVED = 0x00000002,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_MULTIPLE_BY4 = 0x00000003,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_MULTIPLE_RESERVED = 0x00000004,
} AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_MULTIPLE;

/*
 * AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_RATE enum
 */

typedef enum AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_RATE {
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_RATE_48KHZ = 0x00000000,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_RATE_44P1KHZ = 0x00000001,
} AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_RATE;

/*
 * AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_STREAM_TYPE enum
 */

typedef enum AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_STREAM_TYPE {
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_STREAM_TYPE_PCM = 0x00000000,
AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_STREAM_TYPE_NOT_PCM = 0x00000001,
} AZALIA_F2_CODEC_CONVERTER_CONTROL_CONVERTER_FORMAT_STREAM_TYPE;

/*
 * AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_3_KEEPALIVE enum
 */

typedef enum AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_3_KEEPALIVE {
AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_3_KEEPALIVE_SILENT_STREAM_NOT_ENABLE = 0x00000000,
AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_3_KEEPALIVE_SILENT_STREAM_ENABLE = 0x00000001,
} AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_3_KEEPALIVE;

/*
 * AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_COPY enum
 */

typedef enum AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_COPY {
AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_COPY_BIT_C_IS_SET = 0x00000000,
AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_COPY_BIT_C_NOT_SET = 0x00000001,
} AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_COPY;

/*
 * AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_DIGEN enum
 */

typedef enum AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_DIGEN {
AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_DIGEN_DIGITAL_TRANSMISSION_DISABLED = 0x00000000,
AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_DIGEN_DIGITAL_TRANSMISSION_ENABLED = 0x00000001,
} AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_DIGEN;

/*
 * AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_L enum
 */

typedef enum AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_L {
AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_L_BIT7_NOT_SET = 0x00000000,
AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_L_BIT7_IS_SET = 0x00000001,
} AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_L;

/*
 * AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_NON_AUDIO enum
 */

typedef enum AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_NON_AUDIO {
AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_NON_AUDIO_BIT_B_NOT_SET = 0x00000000,
AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_NON_AUDIO_BIT_B_IS_SET = 0x00000001,
} AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_NON_AUDIO;

/*
 * AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_PRE enum
 */

typedef enum AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_PRE {
AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_PRE_LSB_OF_D_NOT_SET = 0x00000000,
AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_PRE_LSB_OF_D_IS_SET = 0x00000001,
} AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_PRE;

/*
 * AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_PRO enum
 */

typedef enum AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_PRO {
AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_PRO_BIT_A_NOT_SET = 0x00000000,
AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_PRO_BIT_A_IS_SET = 0x00000001,
} AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_PRO;

/*
 * AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_V enum
 */

typedef enum AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_V {
AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_V_BIT28_IS_ZERO = 0x00000000,
AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_V_BIT28_IS_ONE = 0x00000001,
} AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_V;

/*
 * AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_VCFG enum
 */

typedef enum AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_VCFG {
AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_VALIDITY_CFG_NOT_ON = 0x00000000,
AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_VALIDITY_CFG_ON = 0x00000001,
} AZALIA_F2_CODEC_CONVERTER_CONTROL_DIGITAL_CONVERTER_VCFG;

/*
 * AZALIA_F2_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR_FORMAT_CODE enum
 */

typedef enum AZALIA_F2_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR_FORMAT_CODE {
AZALIA_F2_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR_FORMAT_CODE_0 = 0x00000000,
AZALIA_F2_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR_FORMAT_CODE_1 = 0x00000001,
AZALIA_F2_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR_FORMAT_CODE_2 = 0x00000002,
AZALIA_F2_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR_FORMAT_CODE_3 = 0x00000003,
AZALIA_F2_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR_FORMAT_CODE_4 = 0x00000004,
AZALIA_F2_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR_FORMAT_CODE_5 = 0x00000005,
AZALIA_F2_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR_FORMAT_CODE_6 = 0x00000006,
AZALIA_F2_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR_FORMAT_CODE_7 = 0x00000007,
AZALIA_F2_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR_FORMAT_CODE_8 = 0x00000008,
AZALIA_F2_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR_FORMAT_CODE_9 = 0x00000009,
AZALIA_F2_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR_FORMAT_CODE_10 = 0x0000000a,
AZALIA_F2_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR_FORMAT_CODE_11 = 0x0000000b,
AZALIA_F2_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR_FORMAT_CODE_12 = 0x0000000c,
AZALIA_F2_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR_FORMAT_CODE_13 = 0x0000000d,
AZALIA_F2_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR_FORMAT_CODE_14 = 0x0000000e,
AZALIA_F2_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR_FORMAT_CODE_15 = 0x0000000f,
} AZALIA_F2_CODEC_PIN_CONTROL_AUDIO_DESCRIPTOR_FORMAT_CODE;

/*
 * AZALIA_F2_CODEC_PIN_CONTROL_DOWN_MIX_INFO_DOWN_MIX_INHIBIT enum
 */

typedef enum AZALIA_F2_CODEC_PIN_CONTROL_DOWN_MIX_INFO_DOWN_MIX_INHIBIT {
AZALIA_F2_CODEC_PIN_CONTROL_DOWN_MIX_NO_INFO_OR_PERMITTED = 0x00000000,
AZALIA_F2_CODEC_PIN_CONTROL_DOWN_MIX_FORBIDDEN = 0x00000001,
} AZALIA_F2_CODEC_PIN_CONTROL_DOWN_MIX_INFO_DOWN_MIX_INHIBIT;

/*
 * AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL01_ENABLE_MULTICHANNEL01_MUTE enum
 */

typedef enum AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL01_ENABLE_MULTICHANNEL01_MUTE {
AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL01_ENABLE_MULTICHANNEL01_NOT_MUTED = 0x00000000,
AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL01_ENABLE_MULTICHANNEL01_MUTED = 0x00000001,
} AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL01_ENABLE_MULTICHANNEL01_MUTE;

/*
 * AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL1_ENABLE_MULTICHANNEL1_MUTE enum
 */

typedef enum AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL1_ENABLE_MULTICHANNEL1_MUTE {
AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL1_ENABLE_MULTICHANNEL1_NOT_MUTED = 0x00000000,
AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL1_ENABLE_MULTICHANNEL1_MUTED = 0x00000001,
} AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL1_ENABLE_MULTICHANNEL1_MUTE;

/*
 * AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL23_ENABLE_MULTICHANNEL23_MUTE enum
 */

typedef enum AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL23_ENABLE_MULTICHANNEL23_MUTE {
AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL23_ENABLE_MULTICHANNEL23_NOT_MUTED = 0x00000000,
AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL23_ENABLE_MULTICHANNEL23_MUTED = 0x00000001,
} AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL23_ENABLE_MULTICHANNEL23_MUTE;

/*
 * AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL3_ENABLE_MULTICHANNEL3_MUTE enum
 */

typedef enum AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL3_ENABLE_MULTICHANNEL3_MUTE {
AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL3_ENABLE_MULTICHANNEL3_NOT_MUTED = 0x00000000,
AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL3_ENABLE_MULTICHANNEL3_MUTED = 0x00000001,
} AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL3_ENABLE_MULTICHANNEL3_MUTE;

/*
 * AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL45_ENABLE_MULTICHANNEL45_MUTE enum
 */

typedef enum AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL45_ENABLE_MULTICHANNEL45_MUTE {
AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL45_ENABLE_MULTICHANNEL45_NOT_MUTED = 0x00000000,
AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL45_ENABLE_MULTICHANNEL45_MUTED = 0x00000001,
} AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL45_ENABLE_MULTICHANNEL45_MUTE;

/*
 * AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL5_ENABLE_MULTICHANNEL5_MUTE enum
 */

typedef enum AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL5_ENABLE_MULTICHANNEL5_MUTE {
AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL5_ENABLE_MULTICHANNEL5_NOT_MUTED = 0x00000000,
AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL5_ENABLE_MULTICHANNEL5_MUTED = 0x00000001,
} AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL5_ENABLE_MULTICHANNEL5_MUTE;

/*
 * AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL67_ENABLE_MULTICHANNEL67_MUTE enum
 */

typedef enum AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL67_ENABLE_MULTICHANNEL67_MUTE {
AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL67_ENABLE_MULTICHANNEL67_NOT_MUTED = 0x00000000,
AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL67_ENABLE_MULTICHANNEL67_MUTED = 0x00000001,
} AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL67_ENABLE_MULTICHANNEL67_MUTE;

/*
 * AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL7_ENABLE_MULTICHANNEL7_MUTE enum
 */

typedef enum AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL7_ENABLE_MULTICHANNEL7_MUTE {
AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL7_ENABLE_MULTICHANNEL7_NOT_MUTED = 0x00000000,
AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL7_ENABLE_MULTICHANNEL7_MUTED = 0x00000001,
} AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL7_ENABLE_MULTICHANNEL7_MUTE;

/*
 * AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL_MODE_MULTICHANNEL_MODE enum
 */

typedef enum AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL_MODE_MULTICHANNEL_MODE {
AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL_MODE_MULTICHANNEL_PAIR_MODE = 0x00000000,
AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL_MODE_MULTICHANNEL_SINGLE_MODE = 0x00000001,
} AZALIA_F2_CODEC_PIN_CONTROL_MULTICHANNEL_MODE_MULTICHANNEL_MODE;

/*
 * AZALIA_F2_CODEC_PIN_CONTROL_UNSOLICITED_RESPONSE_ENABLE enum
 */

typedef enum AZALIA_F2_CODEC_PIN_CONTROL_UNSOLICITED_RESPONSE_ENABLE {
AZALIA_F2_CODEC_PIN_CONTROL_UNSOLICITED_RESPONSE_DISABLED = 0x00000000,
AZALIA_F2_CODEC_PIN_CONTROL_UNSOLICITED_RESPONSE_ENABLED = 0x00000001,
} AZALIA_F2_CODEC_PIN_CONTROL_UNSOLICITED_RESPONSE_ENABLE;

/*
 * AZALIA_F2_CODEC_PIN_CONTROL_WIDGET_CONTROL_OUT_ENABLE enum
 */

typedef enum AZALIA_F2_CODEC_PIN_CONTROL_WIDGET_CONTROL_OUT_ENABLE {
AZALIA_F2_CODEC_PIN_CONTROL_WIDGET_CONTROL_OUT_ENABLE_PIN_SHUT_OFF = 0x00000000,
AZALIA_F2_CODEC_PIN_CONTROL_WIDGET_CONTROL_OUT_ENABLE_PIN_DRIVEN = 0x00000001,
} AZALIA_F2_CODEC_PIN_CONTROL_WIDGET_CONTROL_OUT_ENABLE;

/*******************************************************
 * AZF0CONTROLLER Enums
 *******************************************************/

/*
 * AZALIA_SOFT_RESET_REFCLK_SOFT_RESET enum
 */

typedef enum AZALIA_SOFT_RESET_REFCLK_SOFT_RESET {
AZALIA_SOFT_RESET_REFCLK_SOFT_RESET_NOT_RESET = 0x00000000,
AZALIA_SOFT_RESET_REFCLK_SOFT_RESET_RESET_REFCLK_LOGIC = 0x00000001,
} AZALIA_SOFT_RESET_REFCLK_SOFT_RESET;

/*
 * MEM_PWR_DIS_CTRL enum
 */

typedef enum MEM_PWR_DIS_CTRL {
ENABLE_MEM_PWR_CTRL                      = 0x00000000,
DISABLE_MEM_PWR_CTRL                     = 0x00000001,
} MEM_PWR_DIS_CTRL;

/*
 * MEM_PWR_FORCE_CTRL enum
 */

typedef enum MEM_PWR_FORCE_CTRL {
NO_FORCE_REQUEST                         = 0x00000000,
FORCE_LIGHT_SLEEP_REQUEST                = 0x00000001,
FORCE_DEEP_SLEEP_REQUEST                 = 0x00000002,
FORCE_SHUT_DOWN_REQUEST                  = 0x00000003,
} MEM_PWR_FORCE_CTRL;

/*
 * MEM_PWR_FORCE_CTRL2 enum
 */

typedef enum MEM_PWR_FORCE_CTRL2 {
NO_FORCE_REQ                             = 0x00000000,
FORCE_LIGHT_SLEEP_REQ                    = 0x00000001,
} MEM_PWR_FORCE_CTRL2;

/*
 * MEM_PWR_SEL_CTRL enum
 */

typedef enum MEM_PWR_SEL_CTRL {
DYNAMIC_SHUT_DOWN_ENABLE                 = 0x00000000,
DYNAMIC_DEEP_SLEEP_ENABLE                = 0x00000001,
DYNAMIC_LIGHT_SLEEP_ENABLE               = 0x00000002,
} MEM_PWR_SEL_CTRL;

/*
 * MEM_PWR_SEL_CTRL2 enum
 */

typedef enum MEM_PWR_SEL_CTRL2 {
DYNAMIC_DEEP_SLEEP_EN                    = 0x00000000,
DYNAMIC_LIGHT_SLEEP_EN                   = 0x00000001,
} MEM_PWR_SEL_CTRL2;

/*******************************************************
 * AZF0ROOT Enums
 *******************************************************/

/*
 * CC_RCU_DC_AUDIO_INPUT_PORT_CONNECTIVITY_INPUT_PORT_CONNECTIVITY enum
 */

typedef enum CC_RCU_DC_AUDIO_INPUT_PORT_CONNECTIVITY_INPUT_PORT_CONNECTIVITY {
CC_RCU_DC_AUDIO_INPUT_PORT_CONNECTIVITY_INPUT_PORT_CONNECTIVITY_ALL = 0x00000000,
CC_RCU_DC_AUDIO_INPUT_PORT_CONNECTIVITY_INPUT_PORT_CONNECTIVITY_6 = 0x00000001,
CC_RCU_DC_AUDIO_INPUT_PORT_CONNECTIVITY_INPUT_PORT_CONNECTIVITY_5 = 0x00000002,
CC_RCU_DC_AUDIO_INPUT_PORT_CONNECTIVITY_INPUT_PORT_CONNECTIVITY_4 = 0x00000003,
CC_RCU_DC_AUDIO_INPUT_PORT_CONNECTIVITY_INPUT_PORT_CONNECTIVITY_3 = 0x00000004,
CC_RCU_DC_AUDIO_INPUT_PORT_CONNECTIVITY_INPUT_PORT_CONNECTIVITY_2 = 0x00000005,
CC_RCU_DC_AUDIO_INPUT_PORT_CONNECTIVITY_INPUT_PORT_CONNECTIVITY_1 = 0x00000006,
CC_RCU_DC_AUDIO_INPUT_PORT_CONNECTIVITY_INPUT_PORT_CONNECTIVITY_0 = 0x00000007,
} CC_RCU_DC_AUDIO_INPUT_PORT_CONNECTIVITY_INPUT_PORT_CONNECTIVITY;

/*
 * CC_RCU_DC_AUDIO_PORT_CONNECTIVITY_PORT_CONNECTIVITY enum
 */

typedef enum CC_RCU_DC_AUDIO_PORT_CONNECTIVITY_PORT_CONNECTIVITY {
CC_RCU_DC_AUDIO_PORT_CONNECTIVITY_PORT_CONNECTIVITY_ALL = 0x00000000,
CC_RCU_DC_AUDIO_PORT_CONNECTIVITY_PORT_CONNECTIVITY_6 = 0x00000001,
CC_RCU_DC_AUDIO_PORT_CONNECTIVITY_PORT_CONNECTIVITY_5 = 0x00000002,
CC_RCU_DC_AUDIO_PORT_CONNECTIVITY_PORT_CONNECTIVITY_4 = 0x00000003,
CC_RCU_DC_AUDIO_PORT_CONNECTIVITY_PORT_CONNECTIVITY_3 = 0x00000004,
CC_RCU_DC_AUDIO_PORT_CONNECTIVITY_PORT_CONNECTIVITY_2 = 0x00000005,
CC_RCU_DC_AUDIO_PORT_CONNECTIVITY_PORT_CONNECTIVITY_1 = 0x00000006,
CC_RCU_DC_AUDIO_PORT_CONNECTIVITY_PORT_CONNECTIVITY_0 = 0x00000007,
} CC_RCU_DC_AUDIO_PORT_CONNECTIVITY_PORT_CONNECTIVITY;

/*******************************************************
 * AZINPUTENDPOINT Enums
 *******************************************************/

/*
 * AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_BITS_PER_SAMPLE enum
 */

typedef enum AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_BITS_PER_SAMPLE {
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_BITS_PER_SAMPLE_8_RESERVED = 0x00000000,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_BITS_PER_SAMPLE_16 = 0x00000001,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_BITS_PER_SAMPLE_20 = 0x00000002,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_BITS_PER_SAMPLE_24 = 0x00000003,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_BITS_PER_SAMPLE_32_RESERVED = 0x00000004,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_BITS_PER_SAMPLE_RESERVED = 0x00000005,
} AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_BITS_PER_SAMPLE;

/*
 * AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_NUMBER_OF_CHANNELS enum
 */

typedef enum AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_NUMBER_OF_CHANNELS {
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_NUMBER_OF_CHANNELS_1 = 0x00000000,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_NUMBER_OF_CHANNELS_2 = 0x00000001,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_NUMBER_OF_CHANNELS_3 = 0x00000002,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_NUMBER_OF_CHANNELS_4 = 0x00000003,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_NUMBER_OF_CHANNELS_5 = 0x00000004,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_NUMBER_OF_CHANNELS_6 = 0x00000005,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_NUMBER_OF_CHANNELS_7 = 0x00000006,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_NUMBER_OF_CHANNELS_8 = 0x00000007,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_NUMBER_OF_CHANNELS_RESERVED = 0x00000008,
} AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_NUMBER_OF_CHANNELS;

/*
 * AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_DIVISOR enum
 */

typedef enum AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_DIVISOR {
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_DIVISOR_BY1 = 0x00000000,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_DIVISOR_BY2_RESERVED = 0x00000001,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_DIVISOR_BY3 = 0x00000002,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_DIVISOR_BY4_RESERVED = 0x00000003,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_DIVISOR_BY5_RESERVED = 0x00000004,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_DIVISOR_BY6_RESERVED = 0x00000005,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_DIVISOR_BY7_RESERVED = 0x00000006,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_DIVISOR_BY8_RESERVED = 0x00000007,
} AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_DIVISOR;

/*
 * AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_MULTIPLE enum
 */

typedef enum AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_MULTIPLE {
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_MULTIPLE_BY1 = 0x00000000,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_MULTIPLE_BY2 = 0x00000001,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_MULTIPLE_BY3_RESERVED = 0x00000002,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_MULTIPLE_BY4 = 0x00000003,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_MULTIPLE_RESERVED = 0x00000004,
} AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_MULTIPLE;

/*
 * AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_RATE enum
 */

typedef enum AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_RATE {
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_RATE_48KHZ = 0x00000000,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_RATE_44P1KHZ = 0x00000001,
} AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_SAMPLE_BASE_RATE;

/*
 * AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_STREAM_TYPE enum
 */

typedef enum AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_STREAM_TYPE {
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_STREAM_TYPE_PCM = 0x00000000,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_STREAM_TYPE_NOT_PCM = 0x00000001,
} AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_CONVERTER_FORMAT_STREAM_TYPE;

/*
 * AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_DIGITAL_CONVERTER_DIGEN enum
 */

typedef enum AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_DIGITAL_CONVERTER_DIGEN {
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_DIGITAL_CONVERTER_DIGEN_DIGITAL_TRANSMISSION_DISABLED = 0x00000000,
AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_DIGITAL_CONVERTER_DIGEN_DIGITAL_TRANSMISSION_ENABLED = 0x00000001,
} AZALIA_F2_CODEC_INPUT_CONVERTER_CONTROL_DIGITAL_CONVERTER_DIGEN;

/*
 * AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL0_ENABLE_MULTICHANNEL0_MUTE enum
 */

typedef enum AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL0_ENABLE_MULTICHANNEL0_MUTE {
AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL0_ENABLE_MULTICHANNEL0_NOT_MUTED = 0x00000000,
AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL0_ENABLE_MULTICHANNEL0_MUTED = 0x00000001,
} AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL0_ENABLE_MULTICHANNEL0_MUTE;

/*
 * AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL1_ENABLE_MULTICHANNEL1_MUTE enum
 */

typedef enum AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL1_ENABLE_MULTICHANNEL1_MUTE {
AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL1_ENABLE_MULTICHANNEL1_NOT_MUTED = 0x00000000,
AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL1_ENABLE_MULTICHANNEL1_MUTED = 0x00000001,
} AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL1_ENABLE_MULTICHANNEL1_MUTE;

/*
 * AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL2_ENABLE_MULTICHANNEL2_MUTE enum
 */

typedef enum AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL2_ENABLE_MULTICHANNEL2_MUTE {
AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL2_ENABLE_MULTICHANNEL2_NOT_MUTED = 0x00000000,
AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL2_ENABLE_MULTICHANNEL2_MUTED = 0x00000001,
} AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL2_ENABLE_MULTICHANNEL2_MUTE;

/*
 * AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL3_ENABLE_MULTICHANNEL3_MUTE enum
 */

typedef enum AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL3_ENABLE_MULTICHANNEL3_MUTE {
AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL3_ENABLE_MULTICHANNEL3_NOT_MUTED = 0x00000000,
AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL3_ENABLE_MULTICHANNEL3_MUTED = 0x00000001,
} AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL3_ENABLE_MULTICHANNEL3_MUTE;

/*
 * AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL4_ENABLE_MULTICHANNEL4_MUTE enum
 */

typedef enum AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL4_ENABLE_MULTICHANNEL4_MUTE {
AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL4_ENABLE_MULTICHANNEL4_NOT_MUTED = 0x00000000,
AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL4_ENABLE_MULTICHANNEL4_MUTED = 0x00000001,
} AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL4_ENABLE_MULTICHANNEL4_MUTE;

/*
 * AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL5_ENABLE_MULTICHANNEL5_MUTE enum
 */

typedef enum AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL5_ENABLE_MULTICHANNEL5_MUTE {
AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL5_ENABLE_MULTICHANNEL5_NOT_MUTED = 0x00000000,
AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL5_ENABLE_MULTICHANNEL5_MUTED = 0x00000001,
} AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL5_ENABLE_MULTICHANNEL5_MUTE;

/*
 * AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL6_ENABLE_MULTICHANNEL6_MUTE enum
 */

typedef enum AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL6_ENABLE_MULTICHANNEL6_MUTE {
AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL6_ENABLE_MULTICHANNEL6_NOT_MUTED = 0x00000000,
AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL6_ENABLE_MULTICHANNEL6_MUTED = 0x00000001,
} AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL6_ENABLE_MULTICHANNEL6_MUTE;

/*
 * AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL7_ENABLE_MULTICHANNEL7_MUTE enum
 */

typedef enum AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL7_ENABLE_MULTICHANNEL7_MUTE {
AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL7_ENABLE_MULTICHANNEL7_NOT_MUTED = 0x00000000,
AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL7_ENABLE_MULTICHANNEL7_MUTED = 0x00000001,
} AZALIA_F2_CODEC_INPUT_PIN_CONTROL_MULTICHANNEL7_ENABLE_MULTICHANNEL7_MUTE;

/*
 * AZALIA_F2_CODEC_INPUT_PIN_CONTROL_UNSOLICITED_RESPONSE_ENABLE enum
 */

typedef enum AZALIA_F2_CODEC_INPUT_PIN_CONTROL_UNSOLICITED_RESPONSE_ENABLE {
AZALIA_F2_CODEC_INPUT_PIN_CONTROL_UNSOLICITED_RESPONSE_DISABLED = 0x00000000,
AZALIA_F2_CODEC_INPUT_PIN_CONTROL_UNSOLICITED_RESPONSE_ENABLED = 0x00000001,
} AZALIA_F2_CODEC_INPUT_PIN_CONTROL_UNSOLICITED_RESPONSE_ENABLE;

/*
 * AZALIA_F2_CODEC_INPUT_PIN_CONTROL_WIDGET_CONTROL_IN_ENABLE enum
 */

typedef enum AZALIA_F2_CODEC_INPUT_PIN_CONTROL_WIDGET_CONTROL_IN_ENABLE {
AZALIA_F2_CODEC_INPUT_PIN_CONTROL_WIDGET_CONTROL_IN_ENABLE_PIN_SHUT_OFF = 0x00000000,
AZALIA_F2_CODEC_INPUT_PIN_CONTROL_WIDGET_CONTROL_IN_ENABLE_PIN_DRIVEN = 0x00000001,
} AZALIA_F2_CODEC_INPUT_PIN_CONTROL_WIDGET_CONTROL_IN_ENABLE;

/*******************************************************
 * AZROOT Enums
 *******************************************************/

/*
 * AZALIA_F2_CODEC_FUNCTION_CONTROL_RESET_CODEC_RESET enum
 */

typedef enum AZALIA_F2_CODEC_FUNCTION_CONTROL_RESET_CODEC_RESET {
AZALIA_F2_CODEC_FUNCTION_CONTROL_RESET_CODEC_NOT_RESET = 0x00000000,
AZALIA_F2_CODEC_FUNCTION_CONTROL_RESET_CODEC_DO_RESET = 0x00000001,
} AZALIA_F2_CODEC_FUNCTION_CONTROL_RESET_CODEC_RESET;

/*******************************************************
 * AZF0STREAM Enums
 *******************************************************/

/*
 * AZ_LATENCY_COUNTER_CONTROL enum
 */

typedef enum AZ_LATENCY_COUNTER_CONTROL {
AZ_LATENCY_COUNTER_NO_RESET              = 0x00000000,
AZ_LATENCY_COUNTER_RESET_DONE            = 0x00000001,
} AZ_LATENCY_COUNTER_CONTROL;

/*******************************************************
 * AZSTREAM Enums
 *******************************************************/

/*
 * OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_BUFFER_COMPLETION_INTERRUPT_STATUS enum
 */

typedef enum OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_BUFFER_COMPLETION_INTERRUPT_STATUS {
OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_BUFFER_COMPLETION_INTERRUPT_STATUS_NOT_SET = 0x00000000,
OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_BUFFER_COMPLETION_INTERRUPT_STATUS_SET = 0x00000001,
} OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_BUFFER_COMPLETION_INTERRUPT_STATUS;

/*
 * OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_DESCRIPTOR_ERROR enum
 */

typedef enum OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_DESCRIPTOR_ERROR {
OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_DESCRIPTOR_ERROR_STATUS_NOT_SET = 0x00000000,
OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_DESCRIPTOR_ERROR_STATUS_SET = 0x00000001,
} OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_DESCRIPTOR_ERROR;

/*
 * OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_DESCRIPTOR_ERROR_INTERRUPT_ENABLE enum
 */

typedef enum OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_DESCRIPTOR_ERROR_INTERRUPT_ENABLE {
OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_DESCRIPTOR_ERROR_INTERRUPT_DISABLED = 0x00000000,
OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_DESCRIPTOR_ERROR_INTERRUPT_ENABLED = 0x00000001,
} OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_DESCRIPTOR_ERROR_INTERRUPT_ENABLE;

/*
 * OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_FIFO_ERROR enum
 */

typedef enum OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_FIFO_ERROR {
OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_FIFO_ERROR_STATUS_NOT_SET = 0x00000000,
OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_FIFO_ERROR_STATUS_SET = 0x00000001,
} OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_FIFO_ERROR;

/*
 * OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_FIFO_ERROR_INTERRUPT_ENABLE enum
 */

typedef enum OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_FIFO_ERROR_INTERRUPT_ENABLE {
OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_FIFO_ERROR_INTERRUPT_DISABLED = 0x00000000,
OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_FIFO_ERROR_INTERRUPT_ENABLED = 0x00000001,
} OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_FIFO_ERROR_INTERRUPT_ENABLE;

/*
 * OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_INTERRUPT_ON_COMPLETION_ENABLE enum
 */

typedef enum OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_INTERRUPT_ON_COMPLETION_ENABLE {
OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_INTERRUPT_ON_COMPLETION_ENABLE_INTERRUPT_DISABLED = 0x00000000,
OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_INTERRUPT_ON_COMPLETION_ENABLE_INTERRUPT_ENABLED = 0x00000001,
} OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_INTERRUPT_ON_COMPLETION_ENABLE;

/*
 * OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_STREAM_RESET enum
 */

typedef enum OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_STREAM_RESET {
OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_STREAM_NOT_RESET = 0x00000000,
OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_STREAM_IS_RESET = 0x00000001,
} OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_STREAM_RESET;

/*
 * OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_STREAM_RUN enum
 */

typedef enum OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_STREAM_RUN {
OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_STREAM_NOT_RUN = 0x00000000,
OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_STREAM_DO_RUN = 0x00000001,
} OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_STREAM_RUN;

/*
 * OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_TRAFFIC_PRIORITY enum
 */

typedef enum OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_TRAFFIC_PRIORITY {
OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_NO_TRAFFIC_PRIORITY = 0x00000000,
OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_YES_TRAFFIC_PRIORITY = 0x00000001,
} OUTPUT_STREAM_DESCRIPTOR_CONTROL_AND_STATUS_TRAFFIC_PRIORITY;

/*
 * OUTPUT_STREAM_DESCRIPTOR_FORMAT_BITS_PER_SAMPLE enum
 */

typedef enum OUTPUT_STREAM_DESCRIPTOR_FORMAT_BITS_PER_SAMPLE {
OUTPUT_STREAM_DESCRIPTOR_FORMAT_BITS_PER_SAMPLE_8_RESERVED = 0x00000000,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_BITS_PER_SAMPLE_16 = 0x00000001,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_BITS_PER_SAMPLE_20 = 0x00000002,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_BITS_PER_SAMPLE_24 = 0x00000003,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_BITS_PER_SAMPLE_32_RESERVED = 0x00000004,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_BITS_PER_SAMPLE_RESERVED = 0x00000005,
} OUTPUT_STREAM_DESCRIPTOR_FORMAT_BITS_PER_SAMPLE;

/*
 * OUTPUT_STREAM_DESCRIPTOR_FORMAT_NUMBER_OF_CHANNELS enum
 */

typedef enum OUTPUT_STREAM_DESCRIPTOR_FORMAT_NUMBER_OF_CHANNELS {
OUTPUT_STREAM_DESCRIPTOR_FORMAT_NUMBER_OF_CHANNELS_1 = 0x00000000,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_NUMBER_OF_CHANNELS_2 = 0x00000001,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_NUMBER_OF_CHANNELS_3 = 0x00000002,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_NUMBER_OF_CHANNELS_4 = 0x00000003,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_NUMBER_OF_CHANNELS_5 = 0x00000004,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_NUMBER_OF_CHANNELS_6 = 0x00000005,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_NUMBER_OF_CHANNELS_7 = 0x00000006,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_NUMBER_OF_CHANNELS_8 = 0x00000007,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_NUMBER_OF_CHANNELS_9_RESERVED = 0x00000008,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_NUMBER_OF_CHANNELS_10_RESERVED = 0x00000009,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_NUMBER_OF_CHANNELS_11_RESERVED = 0x0000000a,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_NUMBER_OF_CHANNELS_12_RESERVED = 0x0000000b,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_NUMBER_OF_CHANNELS_13_RESERVED = 0x0000000c,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_NUMBER_OF_CHANNELS_14_RESERVED = 0x0000000d,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_NUMBER_OF_CHANNELS_15_RESERVED = 0x0000000e,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_NUMBER_OF_CHANNELS_16_RESERVED = 0x0000000f,
} OUTPUT_STREAM_DESCRIPTOR_FORMAT_NUMBER_OF_CHANNELS;

/*
 * OUTPUT_STREAM_DESCRIPTOR_FORMAT_SAMPLE_BASE_DIVISOR enum
 */

typedef enum OUTPUT_STREAM_DESCRIPTOR_FORMAT_SAMPLE_BASE_DIVISOR {
OUTPUT_STREAM_DESCRIPTOR_FORMAT_SAMPLE_BASE_DIVISOR_BY1 = 0x00000000,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_SAMPLE_BASE_DIVISOR_BY2_RESERVED = 0x00000001,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_SAMPLE_BASE_DIVISOR_BY3 = 0x00000002,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_SAMPLE_BASE_DIVISOR_BY4_RESERVED = 0x00000003,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_SAMPLE_BASE_DIVISOR_BY5_RESERVED = 0x00000004,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_SAMPLE_BASE_DIVISOR_BY6_RESERVED = 0x00000005,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_SAMPLE_BASE_DIVISOR_BY7_RESERVED = 0x00000006,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_SAMPLE_BASE_DIVISOR_BY8_RESERVED = 0x00000007,
} OUTPUT_STREAM_DESCRIPTOR_FORMAT_SAMPLE_BASE_DIVISOR;

/*
 * OUTPUT_STREAM_DESCRIPTOR_FORMAT_SAMPLE_BASE_MULTIPLE enum
 */

typedef enum OUTPUT_STREAM_DESCRIPTOR_FORMAT_SAMPLE_BASE_MULTIPLE {
OUTPUT_STREAM_DESCRIPTOR_FORMAT_SAMPLE_BASE_MULTIPLE_BY1 = 0x00000000,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_SAMPLE_BASE_MULTIPLE_BY2 = 0x00000001,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_SAMPLE_BASE_MULTIPLE_BY3_RESERVED = 0x00000002,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_SAMPLE_BASE_MULTIPLE_BY4 = 0x00000003,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_SAMPLE_BASE_MULTIPLE_RESERVED = 0x00000004,
} OUTPUT_STREAM_DESCRIPTOR_FORMAT_SAMPLE_BASE_MULTIPLE;

/*
 * OUTPUT_STREAM_DESCRIPTOR_FORMAT_SAMPLE_BASE_RATE enum
 */

typedef enum OUTPUT_STREAM_DESCRIPTOR_FORMAT_SAMPLE_BASE_RATE {
OUTPUT_STREAM_DESCRIPTOR_FORMAT_SAMPLE_BASE_RATE_48KHZ = 0x00000000,
OUTPUT_STREAM_DESCRIPTOR_FORMAT_SAMPLE_BASE_RATE_44P1KHZ = 0x00000001,
} OUTPUT_STREAM_DESCRIPTOR_FORMAT_SAMPLE_BASE_RATE;

/*******************************************************
 * AZF0ENDPOINT Enums
 *******************************************************/

/*
 * AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_AMPLIFIER_PARAMETER_OVERRIDE enum
 */

typedef enum AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_AMPLIFIER_PARAMETER_OVERRIDE {
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_AMPLIFIER_PARAMETER = 0x00000000,
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_AMPLIFIER_PARAMETER_OVERRIDE = 0x00000001,
} AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_AMPLIFIER_PARAMETER_OVERRIDE;

/*
 * AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_AUDIO_CHANNEL_CAPABILITIES enum
 */

typedef enum AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_AUDIO_CHANNEL_CAPABILITIES {
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_AUDIO_CHANNEL_CAPABILITIES_MONOPHONIC = 0x00000000,
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_AUDIO_CHANNEL_CAPABILITIES_STEREO = 0x00000001,
} AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_AUDIO_CHANNEL_CAPABILITIES;

/*
 * AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_CONNECTION_LIST enum
 */

typedef enum AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_CONNECTION_LIST {
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_CONNECTION_LIST = 0x00000000,
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_CONNECTION_LIST = 0x00000001,
} AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_CONNECTION_LIST;

/*
 * AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_DIGITAL enum
 */

typedef enum AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_DIGITAL {
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_IS_ANALOG = 0x00000000,
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_IS_DIGITAL = 0x00000001,
} AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_DIGITAL;

/*
 * AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_FORMAT_OVERRIDE enum
 */

typedef enum AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_FORMAT_OVERRIDE {
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_FORMAT_OVERRIDE = 0x00000000,
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_SUPPORT_FORMAT_OVERRIDE = 0x00000001,
} AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_FORMAT_OVERRIDE;

/*
 * AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_INPUT_AMPLIFIER_PRESENT enum
 */

typedef enum AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_INPUT_AMPLIFIER_PRESENT {
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_INPUT_AMPLIFIER = 0x00000000,
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_INPUT_AMPLIFIER = 0x00000001,
} AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_INPUT_AMPLIFIER_PRESENT;

/*
 * AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_LR_SWAP enum
 */

typedef enum AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_LR_SWAP {
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_LR_SWAP_CAPABILITY = 0x00000000,
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_LR_SWAP_CAPABILITY = 0x00000001,
} AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_LR_SWAP;

/*
 * AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_OUTPUT_AMPLIFIER_PRESENT enum
 */

typedef enum AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_OUTPUT_AMPLIFIER_PRESENT {
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_OUTPUT_AMPLIFIER = 0x00000000,
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_OUTPUT_AMPLIFIER = 0x00000001,
} AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_OUTPUT_AMPLIFIER_PRESENT;

/*
 * AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_POWER_CONTROL enum
 */

typedef enum AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_POWER_CONTROL {
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_POWER_CONTROL_CAPABILITY = 0x00000000,
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_POWER_CONTROL_CAPABILITY = 0x00000001,
} AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_POWER_CONTROL;

/*
 * AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_PROCESSING_WIDGET enum
 */

typedef enum AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_PROCESSING_WIDGET {
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_PROCESSING_WIDGET_NO_PROCESSING_CAPABILITIES = 0x00000000,
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_PROCESSING_WIDGET_HAVE_PROCESSING_CAPABILITIES = 0x00000001,
} AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_PROCESSING_WIDGET;

/*
 * AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_STRIPE enum
 */

typedef enum AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_STRIPE {
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_SUPPORT_STRIPING = 0x00000000,
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_SUPPORT_STRIPING = 0x00000001,
} AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_STRIPE;

/*
 * AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE enum
 */

typedef enum AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE {
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_OUTPUT_CONVERTER_RESERVED = 0x00000000,
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_INPUT_CONVERTER_RESERVED = 0x00000001,
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_MIXER_RESERVED = 0x00000002,
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_SELECTOR_RESERVED = 0x00000003,
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_PIN_RESERVED = 0x00000004,
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_POWER_WIDGET_RESERVED = 0x00000005,
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_VOLUME_KNOB_RESERVED = 0x00000006,
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_BEEP_GENERATOR_RESERVED = 0x00000007,
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_RESERVED_RESERVED = 0x00000008,
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_VENDOR_DEFINED_RESERVED = 0x00000009,
} AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE;

/*
 * AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_UNSOLICITED_RESPONSE_CAPABILITY enum
 */

typedef enum AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_UNSOLICITED_RESPONSE_CAPABILITY {
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_UNSOLICITED_RESPONSE_CAPABILITY = 0x00000000,
AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_UNSOLICITED_RESPONSE_CAPABILITY = 0x00000001,
} AZALIA_F0_CODEC_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_UNSOLICITED_RESPONSE_CAPABILITY;

/*
 * AZALIA_F0_CODEC_PIN_CONTROL_MULTICHANNEL_MODE_MULTICHANNEL_MODE enum
 */

typedef enum AZALIA_F0_CODEC_PIN_CONTROL_MULTICHANNEL_MODE_MULTICHANNEL_MODE {
AZALIA_F0_CODEC_PIN_CONTROL_MULTICHANNEL_MODE_MULTICHANNEL_PAIR_MODE = 0x00000000,
AZALIA_F0_CODEC_PIN_CONTROL_MULTICHANNEL_MODE_MULTICHANNEL_SINGLE_MODE = 0x00000001,
} AZALIA_F0_CODEC_PIN_CONTROL_MULTICHANNEL_MODE_MULTICHANNEL_MODE;

/*
 * AZALIA_F0_CODEC_PIN_CONTROL_RESPONSE_HBR_HBR_CAPABLE enum
 */

typedef enum AZALIA_F0_CODEC_PIN_CONTROL_RESPONSE_HBR_HBR_CAPABLE {
AZALIA_F0_CODEC_PIN_CONTROL_RESPONSE_HBR_NO_HBR_CAPABLILITY = 0x00000000,
AZALIA_F0_CODEC_PIN_CONTROL_RESPONSE_HBR_HAVE_HBR_CAPABLILITY = 0x00000001,
} AZALIA_F0_CODEC_PIN_CONTROL_RESPONSE_HBR_HBR_CAPABLE;

/*
 * AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_AMPLIFIER_PARAMETER_OVERRIDE enum
 */

typedef enum AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_AMPLIFIER_PARAMETER_OVERRIDE {
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_AMPLIFIER_PARAMETER = 0x00000000,
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_AMPLIFIER_PARAMETER_OVERRIDE = 0x00000001,
} AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_AMPLIFIER_PARAMETER_OVERRIDE;

/*
 * AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_CONNECTION_LIST enum
 */

typedef enum AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_CONNECTION_LIST {
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_CONNECTION_LIST = 0x00000000,
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_CONNECTION_LIST = 0x00000001,
} AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_CONNECTION_LIST;

/*
 * AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_DIGITAL enum
 */

typedef enum AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_DIGITAL {
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_IS_ANALOG = 0x00000000,
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_IS_DIGITAL = 0x00000001,
} AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_DIGITAL;

/*
 * AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_INPUT_AMPLIFIER_PRESENT enum
 */

typedef enum AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_INPUT_AMPLIFIER_PRESENT {
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_INPUT_AMPLIFIER_PRESENT = 0x00000000,
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_INPUT_AMPLIFIER = 0x00000001,
} AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_INPUT_AMPLIFIER_PRESENT;

/*
 * AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_LR_SWAP enum
 */

typedef enum AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_LR_SWAP {
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_LR_SWAP_CAPABILITY = 0x00000000,
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_LR_SWAP_CAPABILITY = 0x00000001,
} AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_LR_SWAP;

/*
 * AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_OUTPUT_AMPLIFIER_PRESENT enum
 */

typedef enum AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_OUTPUT_AMPLIFIER_PRESENT {
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_OUTPUT_AMPLIFIER = 0x00000000,
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_OUTPUT_AMPLIFIER = 0x00000001,
} AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_OUTPUT_AMPLIFIER_PRESENT;

/*
 * AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_POWER_CONTROL enum
 */

typedef enum AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_POWER_CONTROL {
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_POWER_CONTROL_CAPABILITY = 0x00000000,
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_POWER_CONTROL_CAPABILITY = 0x00000001,
} AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_POWER_CONTROL;

/*
 * AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_PROCESSING_WIDGET enum
 */

typedef enum AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_PROCESSING_WIDGET {
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_PROCESSING_WIDGET_NO_PROCESSING_CAPABILITIES = 0x00000000,
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_PROCESSING_WIDGET_HAVE_PROCESSING_CAPABILITIES = 0x00000001,
} AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_PROCESSING_WIDGET;

/*
 * AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_STRIPE enum
 */

typedef enum AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_STRIPE {
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_SUPPORT_STRIPING = 0x00000000,
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_SUPPORT_STRIPING = 0x00000001,
} AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_STRIPE;

/*
 * AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE enum
 */

typedef enum AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE {
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_OUTPUT_CONVERTER_RESERVED = 0x00000000,
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_INPUT_CONVERTER_RESERVED = 0x00000001,
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_MIXER_RESERVED = 0x00000002,
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_SELECTOR_RESERVED = 0x00000003,
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_PIN_RESERVED = 0x00000004,
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_POWER_WIDGET_RESERVED = 0x00000005,
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_VOLUME_KNOB_RESERVED = 0x00000006,
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_BEEP_GENERATOR_RESERVED = 0x00000007,
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_RESERVED_RESERVED = 0x00000008,
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_VENDOR_DEFINED_RESERVED = 0x00000009,
} AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE;

/*
 * AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_UNSOLICITED_RESPONSE_CAPABILITY enum
 */

typedef enum AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_UNSOLICITED_RESPONSE_CAPABILITY {
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_UNSOLICITED_RESPONSE_CAPABILITY = 0x00000000,
AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_UNSOLICITED_RESPONSE_CAPABILITY = 0x00000001,
} AZALIA_F0_CODEC_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_UNSOLICITED_RESPONSE_CAPABILITY;

/*
 * AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_BALANCED_I_O_PINS enum
 */

typedef enum AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_BALANCED_I_O_PINS {
AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_I_O_PINS_ARE_NOT_BALANCED = 0x00000000,
AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_I_O_PINS_ARE_BALANCED = 0x00000001,
} AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_BALANCED_I_O_PINS;

/*
 * AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_EAPD_CAPABLE enum
 */

typedef enum AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_EAPD_CAPABLE {
AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_NO_EAPD_PIN = 0x00000000,
AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_HAVE_EAPD_PIN = 0x00000001,
} AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_EAPD_CAPABLE;

/*
 * AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_HEADPHONE_DRIVE_CAPABLE enum
 */

typedef enum AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_HEADPHONE_DRIVE_CAPABLE {
AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_NO_HEADPHONE_DRIVE_CAPABILITY = 0x00000000,
AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_HAVE_HEADPHONE_DRIVE_CAPABILITY = 0x00000001,
} AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_HEADPHONE_DRIVE_CAPABLE;

/*
 * AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_IMPEDANCE_SENSE_CAPABLE enum
 */

typedef enum AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_IMPEDANCE_SENSE_CAPABLE {
AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_NO_IMPEDANCE_SENSE_CAPABILITY = 0x00000000,
AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_HAVE_IMPEDANCE_SENSE_CAPABILITY = 0x00000001,
} AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_IMPEDANCE_SENSE_CAPABLE;

/*
 * AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_INPUT_CAPABLE enum
 */

typedef enum AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_INPUT_CAPABLE {
AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_NO_INPUT_PIN = 0x00000000,
AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_HAVE_INPUT_PIN = 0x00000001,
} AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_INPUT_CAPABLE;

/*
 * AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_JACK_DETECTION_CAPABILITY enum
 */

typedef enum AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_JACK_DETECTION_CAPABILITY {
AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_NO_JACK_DETECTION_CAPABILITY = 0x00000000,
AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_HAVE_JACK_DETECTION_CAPABILITY = 0x00000001,
} AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_JACK_DETECTION_CAPABILITY;

/*
 * AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_OUTPUT_CAPABLE enum
 */

typedef enum AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_OUTPUT_CAPABLE {
AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_NO_OUTPUT_PIN = 0x00000000,
AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_HAVE_OUTPUT_PIN = 0x00000001,
} AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_OUTPUT_CAPABLE;

/*
 * AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_TRIGGER_REQUIRED enum
 */

typedef enum AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_TRIGGER_REQUIRED {
AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_NO_TRIGGER_REQUIRED_FOR_IMPEDANCE_MEASUREMENT = 0x00000000,
AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_TRIGGER_REQUIRED_FOR_IMPEDANCE_MEASUREMENT = 0x00000001,
} AZALIA_F0_CODEC_PIN_PARAMETER_CAPABILITIES_TRIGGER_REQUIRED;

/*******************************************************
 * AZF0INPUTENDPOINT Enums
 *******************************************************/

/*
 * AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_AMPLIFIER_PARAMETER_OVERRIDE enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_AMPLIFIER_PARAMETER_OVERRIDE {
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_AMPLIFIER_PARAMETER = 0x00000000,
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_AMPLIFIER_PARAMETER = 0x00000001,
} AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_AMPLIFIER_PARAMETER_OVERRIDE;

/*
 * AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_AUDIO_CHANNEL_CAPABILITIES enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_AUDIO_CHANNEL_CAPABILITIES {
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_AUDIO_CHANNEL_CAPABILITIES_MONOPHONIC = 0x00000000,
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_AUDIO_CHANNEL_CAPABILITIES_STEREO = 0x00000001,
} AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_AUDIO_CHANNEL_CAPABILITIES;

/*
 * AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_CONNECTION_LIST enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_CONNECTION_LIST {
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_CONNECTION_LIST = 0x00000000,
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_CONNECTION_LIST = 0x00000001,
} AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_CONNECTION_LIST;

/*
 * AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_DIGITAL enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_DIGITAL {
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_CODEC_CONVERTER0_IS_ANALOG = 0x00000000,
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_CODEC_CONVERTER0_IS_DIGITAL = 0x00000001,
} AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_DIGITAL;

/*
 * AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_FORMAT_OVERRIDE enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_FORMAT_OVERRIDE {
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_FORMAT_OVERRIDE = 0x00000000,
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_FORMAT_OVERRIDE = 0x00000001,
} AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_FORMAT_OVERRIDE;

/*
 * AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_INPUT_AMPLIFIER_PRESENT enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_INPUT_AMPLIFIER_PRESENT {
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_INPUT_AMPLIFIER = 0x00000000,
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_INPUT_AMPLIFIER = 0x00000001,
} AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_INPUT_AMPLIFIER_PRESENT;

/*
 * AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_LR_SWAP enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_LR_SWAP {
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_LR_SWAP_CAPABILITY = 0x00000000,
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_LR_SWAP_CAPABILITY = 0x00000001,
} AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_LR_SWAP;

/*
 * AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_OUTPUT_AMPLIFIER_PRESENT enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_OUTPUT_AMPLIFIER_PRESENT {
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_OUTPUT_AMPLIFIER = 0x00000000,
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_OUTPUT_AMPLIFIER = 0x00000001,
} AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_OUTPUT_AMPLIFIER_PRESENT;

/*
 * AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_POWER_CONTROL enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_POWER_CONTROL {
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_POWER_CONTROL_CAPABILITY = 0x00000000,
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_POWER_CONTROL_CAPABILITY = 0x00000001,
} AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_POWER_CONTROL;

/*
 * AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_PROCESSING_WIDGET enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_PROCESSING_WIDGET {
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_PROCESSING_WIDGET_CODEC_CONVERTER0_HAVE_NO_PROCESSING_CAPABILITIES = 0x00000000,
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_PROCESSING_WIDGET_CODEC_CONVERTER0_HAVE_PROCESSING_CAPABILITIES = 0x00000001,
} AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_PROCESSING_WIDGET;

/*
 * AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_STRIPE enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_STRIPE {
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NOT_SUPPORT_STRIPING = 0x00000000,
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_SUPPORT_STRIPING = 0x00000001,
} AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_STRIPE;

/*
 * AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE {
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_OUTPUT_CONVERTER_RESERVED = 0x00000000,
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_INPUT_CONVERTER_RESERVED = 0x00000001,
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_MIXER_RESERVED = 0x00000002,
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_SELECTOR_RESERVED = 0x00000003,
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_PIN_RESERVED = 0x00000004,
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_POWER_WIDGET_RESERVED = 0x00000005,
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_VOLUME_KNOB_RESERVED = 0x00000006,
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_BEEP_GENERATOR_RESERVED = 0x00000007,
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_RESERVED = 0x00000008,
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_VENDOR_DEFINED_RESERVED = 0x00000009,
} AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE;

/*
 * AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_UNSOLICITED_RESPONSE_CAPABILITY enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_UNSOLICITED_RESPONSE_CAPABILITY {
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_UNSOLICITED_RESPONSE_CAPABILITY = 0x00000000,
AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_UNSOLICITED_RESPONSE_CAPABILITY = 0x00000001,
} AZALIA_F0_CODEC_INPUT_CONVERTER_PARAMETER_AUDIO_WIDGET_CAPABILITIES_UNSOLICITED_RESPONSE_CAPABILITY;

/*
 * AZALIA_F0_CODEC_INPUT_PIN_CONTROL_RESPONSE_HBR_HBR_CAPABLE enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_PIN_CONTROL_RESPONSE_HBR_HBR_CAPABLE {
AZALIA_F0_CODEC_INPUT_PIN_CONTROL_RESPONSE_HBR_NO_HBR_CAPABILITY = 0x00000000,
AZALIA_F0_CODEC_INPUT_PIN_CONTROL_RESPONSE_HBR_HAVE_HBR_CAPABILITY = 0x00000001,
} AZALIA_F0_CODEC_INPUT_PIN_CONTROL_RESPONSE_HBR_HBR_CAPABLE;

/*
 * AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_AMPLIFIER_PARAMETER_OVERRIDE enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_AMPLIFIER_PARAMETER_OVERRIDE {
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_AMPLIFIER_PARAMETER = 0x00000000,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_AMPLIFIER_PARAMETER_OVERRIDE = 0x00000001,
} AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_AMPLIFIER_PARAMETER_OVERRIDE;

/*
 * AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_CONNECTION_LIST enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_CONNECTION_LIST {
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_CONNECTION_LIST = 0x00000000,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_CONNECTION_LIST = 0x00000001,
} AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_CONNECTION_LIST;

/*
 * AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_DIGITAL enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_DIGITAL {
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_IS_ANALOG = 0x00000000,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_IS_DIGITAL = 0x00000001,
} AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_DIGITAL;

/*
 * AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_INPUT_AMPLIFIER_PRESENT enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_INPUT_AMPLIFIER_PRESENT {
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_INPUT_AMPLIFIER = 0x00000000,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_INPUT_AMPLIFIER = 0x00000001,
} AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_INPUT_AMPLIFIER_PRESENT;

/*
 * AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_LR_SWAP enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_LR_SWAP {
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_LR_SWAP = 0x00000000,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_LR_SWAP = 0x00000001,
} AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_LR_SWAP;

/*
 * AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_OUTPUT_AMPLIFIER_PRESENT enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_OUTPUT_AMPLIFIER_PRESENT {
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_OUTPUT_AMPLIFIER = 0x00000000,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_OUTPUT_AMPLIFIER = 0x00000001,
} AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_OUTPUT_AMPLIFIER_PRESENT;

/*
 * AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_POWER_CONTROL enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_POWER_CONTROL {
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_POWER_CONTROL_CAPABILITY = 0x00000000,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_POWER_CONTROL_CAPABILITY = 0x00000001,
} AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_POWER_CONTROL;

/*
 * AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_PROCESSING_WIDGET enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_PROCESSING_WIDGET {
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_PROCESSING_WIDGET_NO_PROCESING_CAPABILITIES = 0x00000000,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_PROCESSING_WIDGET_HAVE_PROCESING_CAPABILITIES = 0x00000001,
} AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_PROCESSING_WIDGET;

/*
 * AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_STRIPE enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_STRIPE {
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_SUPPORT_STRIPING = 0x00000000,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_SUPPORT_STRIPING = 0x00000001,
} AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_STRIPE;

/*
 * AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE {
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_OUTPUT_CONVERTER_RESERVED = 0x00000000,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_INPUT_CONVERTER_RESERVED = 0x00000001,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_MIXER_RESERVED = 0x00000002,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_SELECTOR_RESERVED = 0x00000003,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_PIN_RESERVED = 0x00000004,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_POWER_WIDGET_RESERVED = 0x00000005,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_VOLUME_KNOB_RESERVED = 0x00000006,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_BEEP_GENERATOR_RESERVED = 0x00000007,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_RESERVED = 0x00000008,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE_VENDOR_DEFINED_RESERVED = 0x00000009,
} AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_TYPE;

/*
 * AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_UNSOLICITED_RESPONSE_CAPABILITY enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_UNSOLICITED_RESPONSE_CAPABILITY {
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_NO_UNSOLICITED_RESPONSE_CAPABILITY = 0x00000000,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_HAVE_UNSOLICITED_RESPONSE_CAPABILITY = 0x00000001,
} AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_AUDIO_WIDGET_CAPABILITIES_UNSOLICITED_RESPONSE_CAPABILITY;

/*
 * AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_BALANCED_I_O_PINS enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_BALANCED_I_O_PINS {
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_I_O_PINS_NOT_BALANCED = 0x00000000,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_I_O_PINS_ARE_BALANCED = 0x00000001,
} AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_BALANCED_I_O_PINS;

/*
 * AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_DP enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_DP {
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_DP_NOT_ENABLED = 0x00000000,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_DP_ENABLED = 0x00000001,
} AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_DP;

/*
 * AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_EAPD_CAPABLE enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_EAPD_CAPABLE {
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_EAPD_CAPABLE_NO_EAPD_PIN = 0x00000000,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_EAPD_CAPABLE_HAVE_EAPD_PIN = 0x00000001,
} AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_EAPD_CAPABLE;

/*
 * AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_HDMI enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_HDMI {
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_HDMI_NOT_ENABLED = 0x00000000,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_HDMI_ENABLED = 0x00000001,
} AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_HDMI;

/*
 * AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_HEADPHONE_DRIVE_CAPABLE enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_HEADPHONE_DRIVE_CAPABLE {
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_NO_HEADPHONE_DRIVE_CAPABILITY = 0x00000000,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_HAVE_HEADPHONE_DRIVE_CAPABILITY = 0x00000001,
} AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_HEADPHONE_DRIVE_CAPABLE;

/*
 * AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_IMPEDANCE_SENSE_CAPABLE enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_IMPEDANCE_SENSE_CAPABLE {
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_NO_IMPEDANCE_SENSE_CAPABILITY = 0x00000000,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_HAVE_IMPEDANCE_SENSE_CAPABILITY = 0x00000001,
} AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_IMPEDANCE_SENSE_CAPABLE;

/*
 * AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_INPUT_CAPABLE enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_INPUT_CAPABLE {
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_NO_INPUT_PIN = 0x00000000,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_HAVE_INPUT_PIN = 0x00000001,
} AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_INPUT_CAPABLE;

/*
 * AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_JACK_DETECTION_CAPABILITY enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_JACK_DETECTION_CAPABILITY {
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_NO_JACK_PRESENCE_DETECTION_CAPABILITY = 0x00000000,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_HAVE_JACK_PRESENCE_DETECTION_CAPABILITY = 0x00000001,
} AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_JACK_DETECTION_CAPABILITY;

/*
 * AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_OUTPUT_CAPABLE enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_OUTPUT_CAPABLE {
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_NO_OUTPUT_PIN = 0x00000000,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_HAVE_OUTPUT_PIN = 0x00000001,
} AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_OUTPUT_CAPABLE;

/*
 * AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_TRIGGER_REQUIRED enum
 */

typedef enum AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_TRIGGER_REQUIRED {
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_NO_TRIGGER_REQUIRED_FOR_IMPEDANCE_MEASUREMENT = 0x00000000,
AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_TRIGGER_REQUIRED_FOR_IMPEDANCE_MEASUREMENT = 0x00000001,
} AZALIA_F0_CODEC_INPUT_PIN_PARAMETER_CAPABILITIES_TRIGGER_REQUIRED;

/*******************************************************
 * DSCC Enums
 *******************************************************/

/*
 * DSCC_BITS_PER_COMPONENT_ENUM enum
 */

typedef enum DSCC_BITS_PER_COMPONENT_ENUM {
DSCC_BITS_PER_COMPONENT_ENUM_BITS_PER_COMPONENT_8_BIT = 0x00000008,
DSCC_BITS_PER_COMPONENT_ENUM_BITS_PER_COMPONENT_10_BIT = 0x0000000a,
DSCC_BITS_PER_COMPONENT_ENUM_BITS_PER_COMPONENT_12_BIT = 0x0000000c,
} DSCC_BITS_PER_COMPONENT_ENUM;

/*
 * DSCC_DSC_VERSION_MAJOR_ENUM enum
 */

typedef enum DSCC_DSC_VERSION_MAJOR_ENUM {
DSCC_DSC_VERSION_MAJOR_ENUM_DSC_1_X_MAJOR_VERSION = 0x00000001,
} DSCC_DSC_VERSION_MAJOR_ENUM;

/*
 * DSCC_DSC_VERSION_MINOR_ENUM enum
 */

typedef enum DSCC_DSC_VERSION_MINOR_ENUM {
DSCC_DSC_VERSION_MINOR_ENUM_DSC_X_1_MINOR_VERSION = 0x00000001,
DSCC_DSC_VERSION_MINOR_ENUM_DSC_X_2_MINOR_VERSION = 0x00000002,
} DSCC_DSC_VERSION_MINOR_ENUM;

/*
 * DSCC_ENABLE_ENUM enum
 */

typedef enum DSCC_ENABLE_ENUM {
DSCC_ENABLE_ENUM_DISABLED                = 0x00000000,
DSCC_ENABLE_ENUM_ENABLED                 = 0x00000001,
} DSCC_ENABLE_ENUM;

/*
 * DSCC_ICH_RESET_ENUM enum
 */

typedef enum DSCC_ICH_RESET_ENUM {
DSCC_ICH_RESET_ENUM_SLICE0_ICH_RESET     = 0x00000001,
DSCC_ICH_RESET_ENUM_SLICE1_ICH_RESET     = 0x00000002,
DSCC_ICH_RESET_ENUM_SLICE2_ICH_RESET     = 0x00000004,
DSCC_ICH_RESET_ENUM_SLICE3_ICH_RESET     = 0x00000008,
} DSCC_ICH_RESET_ENUM;

/*
 * DSCC_LINEBUF_DEPTH_ENUM enum
 */

typedef enum DSCC_LINEBUF_DEPTH_ENUM {
DSCC_LINEBUF_DEPTH_ENUM_LINEBUF_DEPTH_8_BIT = 0x00000008,
DSCC_LINEBUF_DEPTH_ENUM_LINEBUF_DEPTH_9_BIT = 0x00000009,
DSCC_LINEBUF_DEPTH_ENUM_LINEBUF_DEPTH_10_BIT = 0x0000000a,
DSCC_LINEBUF_DEPTH_ENUM_LINEBUF_DEPTH_11_BIT = 0x0000000b,
DSCC_LINEBUF_DEPTH_ENUM_LINEBUF_DEPTH_12_BIT = 0x0000000c,
DSCC_LINEBUF_DEPTH_ENUM_LINEBUF_DEPTH_13_BIT = 0x0000000d,
} DSCC_LINEBUF_DEPTH_ENUM;

/*
 * DSCC_MEM_PWR_DIS_ENUM enum
 */

typedef enum DSCC_MEM_PWR_DIS_ENUM {
DSCC_MEM_PWR_DIS_ENUM_REQUEST_EN         = 0x00000000,
DSCC_MEM_PWR_DIS_ENUM_REQUEST_DIS        = 0x00000001,
} DSCC_MEM_PWR_DIS_ENUM;

/*
 * DSCC_MEM_PWR_FORCE_ENUM enum
 */

typedef enum DSCC_MEM_PWR_FORCE_ENUM {
DSCC_MEM_PWR_FORCE_ENUM_NO_FORCE_REQUEST = 0x00000000,
DSCC_MEM_PWR_FORCE_ENUM_FORCE_LIGHT_SLEEP_REQUEST = 0x00000001,
DSCC_MEM_PWR_FORCE_ENUM_FORCE_DEEP_SLEEP_REQUEST = 0x00000002,
DSCC_MEM_PWR_FORCE_ENUM_FORCE_SHUT_DOWN_REQUEST = 0x00000003,
} DSCC_MEM_PWR_FORCE_ENUM;

/*
 * POWER_STATE_ENUM enum
 */

typedef enum POWER_STATE_ENUM {
POWER_STATE_ENUM_ON                      = 0x00000000,
POWER_STATE_ENUM_LS                      = 0x00000001,
POWER_STATE_ENUM_DS                      = 0x00000002,
POWER_STATE_ENUM_SD                      = 0x00000003,
} POWER_STATE_ENUM;

/*******************************************************
 * DSCCIF Enums
 *******************************************************/

/*
 * DSCCIF_BITS_PER_COMPONENT_ENUM enum
 */

typedef enum DSCCIF_BITS_PER_COMPONENT_ENUM {
DSCCIF_BITS_PER_COMPONENT_ENUM_BITS_PER_COMPONENT_8_BIT = 0x00000008,
DSCCIF_BITS_PER_COMPONENT_ENUM_BITS_PER_COMPONENT_10_BIT = 0x0000000a,
DSCCIF_BITS_PER_COMPONENT_ENUM_BITS_PER_COMPONENT_12_BIT = 0x0000000c,
} DSCCIF_BITS_PER_COMPONENT_ENUM;

/*
 * DSCCIF_ENABLE_ENUM enum
 */

typedef enum DSCCIF_ENABLE_ENUM {
DSCCIF_ENABLE_ENUM_DISABLED              = 0x00000000,
DSCCIF_ENABLE_ENUM_ENABLED               = 0x00000001,
} DSCCIF_ENABLE_ENUM;

/*
 * DSCCIF_INPUT_PIXEL_FORMAT_ENUM enum
 */

typedef enum DSCCIF_INPUT_PIXEL_FORMAT_ENUM {
DSCCIF_INPUT_PIXEL_FORMAT_ENUM_RGB       = 0x00000000,
DSCCIF_INPUT_PIXEL_FORMAT_ENUM_YCBCR_444 = 0x00000001,
DSCCIF_INPUT_PIXEL_FORMAT_ENUM_SIMPLE_YCBCR_422 = 0x00000002,
DSCCIF_INPUT_PIXEL_FORMAT_ENUM_NATIVE_YCBCR_422 = 0x00000003,
DSCCIF_INPUT_PIXEL_FORMAT_ENUM_NATIVE_YCBCR_420 = 0x00000004,
} DSCCIF_INPUT_PIXEL_FORMAT_ENUM;

/*******************************************************
 * DSC_TOP Enums
 *******************************************************/

/*
 * CLOCK_GATING_DISABLE_ENUM enum
 */

typedef enum CLOCK_GATING_DISABLE_ENUM {
CLOCK_GATING_DISABLE_ENUM_ENABLED        = 0x00000000,
CLOCK_GATING_DISABLE_ENUM_DISABLED       = 0x00000001,
} CLOCK_GATING_DISABLE_ENUM;

/*
 * ENABLE_ENUM enum
 */

typedef enum ENABLE_ENUM {
ENABLE_ENUM_DISABLED                     = 0x00000000,
ENABLE_ENUM_ENABLED                      = 0x00000001,
} ENABLE_ENUM;

/*
 * TEST_CLOCK_MUX_SELECT_ENUM enum
 */

typedef enum TEST_CLOCK_MUX_SELECT_ENUM {
TEST_CLOCK_MUX_SELECT_DISPCLK_P          = 0x00000000,
TEST_CLOCK_MUX_SELECT_DISPCLK_G          = 0x00000001,
TEST_CLOCK_MUX_SELECT_DISPCLK_R          = 0x00000002,
TEST_CLOCK_MUX_SELECT_DSCCLK_P           = 0x00000003,
TEST_CLOCK_MUX_SELECT_DSCCLK_G           = 0x00000004,
TEST_CLOCK_MUX_SELECT_DSCCLK_R           = 0x00000005,
} TEST_CLOCK_MUX_SELECT_ENUM;

/*******************************************************
 * DWB_TOP Enums
 *******************************************************/

/*
 * DWB_CRC_CONT_EN_ENUM enum
 */

typedef enum DWB_CRC_CONT_EN_ENUM {
DWB_CRC_CONT_EN_ONE_SHOT                 = 0x00000000,
DWB_CRC_CONT_EN_CONT                     = 0x00000001,
} DWB_CRC_CONT_EN_ENUM;

/*
 * DWB_CRC_SRC_SEL_ENUM enum
 */

typedef enum DWB_CRC_SRC_SEL_ENUM {
DWB_CRC_SRC_SEL_DWB_IN                   = 0x00000000,
DWB_CRC_SRC_SEL_OGAM_OUT                 = 0x00000001,
DWB_CRC_SRC_SEL_DWB_OUT                  = 0x00000002,
} DWB_CRC_SRC_SEL_ENUM;

/*
 * DWB_DATA_OVERFLOW_INT_TYPE_ENUM enum
 */

typedef enum DWB_DATA_OVERFLOW_INT_TYPE_ENUM {
DWB_DATA_OVERFLOW_INT_TYPE_0             = 0x00000000,
DWB_DATA_OVERFLOW_INT_TYPE_1             = 0x00000001,
} DWB_DATA_OVERFLOW_INT_TYPE_ENUM;

/*
 * DWB_DATA_OVERFLOW_TYPE_ENUM enum
 */

typedef enum DWB_DATA_OVERFLOW_TYPE_ENUM {
DWB_DATA_OVERFLOW_TYPE_NO_OVERFLOW       = 0x00000000,
DWB_DATA_OVERFLOW_TYPE_BUFFER            = 0x00000001,
DWB_DATA_OVERFLOW_TYPE_VUPDATE           = 0x00000002,
DWB_DATA_OVERFLOW_TYPE_VREADY            = 0x00000003,
} DWB_DATA_OVERFLOW_TYPE_ENUM;

/*
 * DWB_DEBUG_SEL_ENUM enum
 */

typedef enum DWB_DEBUG_SEL_ENUM {
DWB_DEBUG_SEL_FC                         = 0x00000000,
DWB_DEBUG_SEL_RESERVED                   = 0x00000001,
DWB_DEBUG_SEL_DWBCP                      = 0x00000002,
DWB_DEBUG_SEL_PERFMON                    = 0x00000003,
} DWB_DEBUG_SEL_ENUM;

/*
 * DWB_MEM_PWR_FORCE_ENUM enum
 */

typedef enum DWB_MEM_PWR_FORCE_ENUM {
DWB_MEM_PWR_FORCE_DIS                    = 0x00000000,
DWB_MEM_PWR_FORCE_LS                     = 0x00000001,
DWB_MEM_PWR_FORCE_DS                     = 0x00000002,
DWB_MEM_PWR_FORCE_SD                     = 0x00000003,
} DWB_MEM_PWR_FORCE_ENUM;

/*
 * DWB_MEM_PWR_STATE_ENUM enum
 */

typedef enum DWB_MEM_PWR_STATE_ENUM {
DWB_MEM_PWR_STATE_ON                     = 0x00000000,
DWB_MEM_PWR_STATE_LS                     = 0x00000001,
DWB_MEM_PWR_STATE_DS                     = 0x00000002,
DWB_MEM_PWR_STATE_SD                     = 0x00000003,
} DWB_MEM_PWR_STATE_ENUM;

/*
 * DWB_TEST_CLK_SEL_ENUM enum
 */

typedef enum DWB_TEST_CLK_SEL_ENUM {
DWB_TEST_CLK_SEL_R                       = 0x00000000,
DWB_TEST_CLK_SEL_G                       = 0x00000001,
DWB_TEST_CLK_SEL_P                       = 0x00000002,
} DWB_TEST_CLK_SEL_ENUM;

/*
 * FC_EYE_SELECTION_ENUM enum
 */

typedef enum FC_EYE_SELECTION_ENUM {
FC_EYE_SELECTION_STEREO_DIS              = 0x00000000,
FC_EYE_SELECTION_LEFT_EYE                = 0x00000001,
FC_EYE_SELECTION_RIGHT_EYE               = 0x00000002,
} FC_EYE_SELECTION_ENUM;

/*
 * FC_FRAME_CAPTURE_RATE_ENUM enum
 */

typedef enum FC_FRAME_CAPTURE_RATE_ENUM {
FC_FRAME_CAPTURE_RATE_FULL               = 0x00000000,
FC_FRAME_CAPTURE_RATE_HALF               = 0x00000001,
FC_FRAME_CAPTURE_RATE_THIRD              = 0x00000002,
FC_FRAME_CAPTURE_RATE_QUARTER            = 0x00000003,
} FC_FRAME_CAPTURE_RATE_ENUM;

/*
 * FC_STEREO_EYE_POLARITY_ENUM enum
 */

typedef enum FC_STEREO_EYE_POLARITY_ENUM {
FC_STEREO_EYE_POLARITY_LEFT              = 0x00000000,
FC_STEREO_EYE_POLARITY_RIGHT             = 0x00000001,
} FC_STEREO_EYE_POLARITY_ENUM;

/*******************************************************
 * DWBCP Enums
 *******************************************************/

/*
 * DWB_GAMUT_REMAP_COEF_FORMAT_ENUM enum
 */

typedef enum DWB_GAMUT_REMAP_COEF_FORMAT_ENUM {
DWB_GAMUT_REMAP_COEF_FORMAT_S2_13        = 0x00000000,
DWB_GAMUT_REMAP_COEF_FORMAT_S3_12        = 0x00000001,
} DWB_GAMUT_REMAP_COEF_FORMAT_ENUM;

/*
 * DWB_GAMUT_REMAP_MODE_ENUM enum
 */

typedef enum DWB_GAMUT_REMAP_MODE_ENUM {
DWB_GAMUT_REMAP_MODE_BYPASS              = 0x00000000,
DWB_GAMUT_REMAP_MODE_COEF_A              = 0x00000001,
DWB_GAMUT_REMAP_MODE_COEF_B              = 0x00000002,
DWB_GAMUT_REMAP_MODE_RESERVED            = 0x00000003,
} DWB_GAMUT_REMAP_MODE_ENUM;

/*
 * DWB_LUT_NUM_SEG enum
 */

typedef enum DWB_LUT_NUM_SEG {
DWB_SEGMENTS_1                           = 0x00000000,
DWB_SEGMENTS_2                           = 0x00000001,
DWB_SEGMENTS_4                           = 0x00000002,
DWB_SEGMENTS_8                           = 0x00000003,
DWB_SEGMENTS_16                          = 0x00000004,
DWB_SEGMENTS_32                          = 0x00000005,
DWB_SEGMENTS_64                          = 0x00000006,
DWB_SEGMENTS_128                         = 0x00000007,
} DWB_LUT_NUM_SEG;

/*
 * DWB_OGAM_LUT_CONFIG_MODE_ENUM enum
 */

typedef enum DWB_OGAM_LUT_CONFIG_MODE_ENUM {
DWB_OGAM_LUT_CONFIG_MODE_DIFF            = 0x00000000,
DWB_OGAM_LUT_CONFIG_MODE_SAME            = 0x00000001,
} DWB_OGAM_LUT_CONFIG_MODE_ENUM;

/*
 * DWB_OGAM_LUT_HOST_SEL_ENUM enum
 */

typedef enum DWB_OGAM_LUT_HOST_SEL_ENUM {
DWB_OGAM_LUT_HOST_SEL_RAMA               = 0x00000000,
DWB_OGAM_LUT_HOST_SEL_RAMB               = 0x00000001,
} DWB_OGAM_LUT_HOST_SEL_ENUM;

/*
 * DWB_OGAM_LUT_READ_COLOR_SEL_ENUM enum
 */

typedef enum DWB_OGAM_LUT_READ_COLOR_SEL_ENUM {
DWB_OGAM_LUT_READ_COLOR_SEL_B            = 0x00000000,
DWB_OGAM_LUT_READ_COLOR_SEL_G            = 0x00000001,
DWB_OGAM_LUT_READ_COLOR_SEL_R            = 0x00000002,
DWB_OGAM_LUT_READ_COLOR_SEL_RESERVED     = 0x00000003,
} DWB_OGAM_LUT_READ_COLOR_SEL_ENUM;

/*
 * DWB_OGAM_LUT_READ_DBG_ENUM enum
 */

typedef enum DWB_OGAM_LUT_READ_DBG_ENUM {
DWB_OGAM_LUT_READ_DBG_DISABLE            = 0x00000000,
DWB_OGAM_LUT_READ_DBG_ENABLE             = 0x00000001,
} DWB_OGAM_LUT_READ_DBG_ENUM;

/*
 * DWB_OGAM_MODE_ENUM enum
 */

typedef enum DWB_OGAM_MODE_ENUM {
DWB_OGAM_MODE_BYPASS                     = 0x00000000,
DWB_OGAM_MODE_RESERVED                   = 0x00000001,
DWB_OGAM_MODE_RAM_LUT_ENABLED            = 0x00000002,
} DWB_OGAM_MODE_ENUM;

/*
 * DWB_OGAM_PWL_DISABLE_ENUM enum
 */

typedef enum DWB_OGAM_PWL_DISABLE_ENUM {
DWB_OGAM_PWL_DISABLE_FALSE               = 0x00000000,
DWB_OGAM_PWL_DISABLE_TRUE                = 0x00000001,
} DWB_OGAM_PWL_DISABLE_ENUM;

/*
 * DWB_OGAM_SELECT_ENUM enum
 */

typedef enum DWB_OGAM_SELECT_ENUM {
DWB_OGAM_SELECT_A                        = 0x00000000,
DWB_OGAM_SELECT_B                        = 0x00000001,
} DWB_OGAM_SELECT_ENUM;

/*******************************************************
 * RDPCSPIPE Enums
 *******************************************************/

/*
 * RDPCSPIPE_CLOCK_CNTL_LANE_CLK_EN enum
 */

typedef enum RDPCSPIPE_CLOCK_CNTL_LANE_CLK_EN {
RDPCSPIPE_EXT_PCLK_EN_DISABLE            = 0x00000000,
RDPCSPIPE_EXT_PCLK_EN_ENABLE             = 0x00000001,
} RDPCSPIPE_CLOCK_CNTL_LANE_CLK_EN;

/*
 * RDPCSPIPE_CLOCK_CNTL_RDPCS_APBCLK_EN enum
 */

typedef enum RDPCSPIPE_CLOCK_CNTL_RDPCS_APBCLK_EN {
RDPCSPIPE_APBCLK_DISABLE                 = 0x00000000,
RDPCSPIPE_APBCLK_ENABLE                  = 0x00000001,
} RDPCSPIPE_CLOCK_CNTL_RDPCS_APBCLK_EN;

/*
 * RDPCSPIPE_CLOCK_CNTL_RDPCS_PIPE_CLK_CLOCK_ON enum
 */

typedef enum RDPCSPIPE_CLOCK_CNTL_RDPCS_PIPE_CLK_CLOCK_ON {
RDPCS_PIPE_CLK_CLOCK_OFF                 = 0x00000000,
RDPCS_PIPE_CLK_CLOCK_ON                  = 0x00000001,
} RDPCSPIPE_CLOCK_CNTL_RDPCS_PIPE_CLK_CLOCK_ON;

/*
 * RDPCSPIPE_CLOCK_CNTL_RDPCS_PIPE_CLK_EN enum
 */

typedef enum RDPCSPIPE_CLOCK_CNTL_RDPCS_PIPE_CLK_EN {
RDPCS_PIPE_CLK_DISABLE                   = 0x00000000,
RDPCS_PIPE_CLK_ENABLE                    = 0x00000001,
} RDPCSPIPE_CLOCK_CNTL_RDPCS_PIPE_CLK_EN;

/*
 * RDPCSPIPE_CLOCK_CNTL_RDPCS_PIPE_CLK_GATE_DIS enum
 */

typedef enum RDPCSPIPE_CLOCK_CNTL_RDPCS_PIPE_CLK_GATE_DIS {
RDPCS_PIPE_CLK_GATE_ENABLE               = 0x00000000,
RDPCS_PIPE_CLK_GATE_DISABLE              = 0x00000001,
} RDPCSPIPE_CLOCK_CNTL_RDPCS_PIPE_CLK_GATE_DIS;

/*
 * RDPCSPIPE_CLOCK_CNTL_RDPCS_PIPE_PHYD32CLK_CLOCK_ON enum
 */

typedef enum RDPCSPIPE_CLOCK_CNTL_RDPCS_PIPE_PHYD32CLK_CLOCK_ON {
RDPCS_PIPE_PHYD32CLK_CLOCK_OFF           = 0x00000000,
RDPCS_PIPE_PHYD32CLK_CLOCK_ON            = 0x00000001,
} RDPCSPIPE_CLOCK_CNTL_RDPCS_PIPE_PHYD32CLK_CLOCK_ON;

/*
 * RDPCSPIPE_CLOCK_CNTL_RDPCS_SRAMCLK_CLOCK_ON enum
 */

typedef enum RDPCSPIPE_CLOCK_CNTL_RDPCS_SRAMCLK_CLOCK_ON {
RDPCSPIPE_SYMCLK_SRAMCLK_CLOCK_OFF       = 0x00000000,
RDPCSPIPE_SYMCLK_SRAMCLK_CLOCK_ON        = 0x00000001,
} RDPCSPIPE_CLOCK_CNTL_RDPCS_SRAMCLK_CLOCK_ON;

/*
 * RDPCSPIPE_CLOCK_CNTL_RDPCS_SRAMCLK_EN enum
 */

typedef enum RDPCSPIPE_CLOCK_CNTL_RDPCS_SRAMCLK_EN {
RDPCSPIPE_SRAMCLK_DISABLE                = 0x00000000,
RDPCSPIPE_SRAMCLK_ENABLE                 = 0x00000001,
} RDPCSPIPE_CLOCK_CNTL_RDPCS_SRAMCLK_EN;

/*
 * RDPCSPIPE_CLOCK_CNTL_RDPCS_SRAMCLK_GATE_DIS enum
 */

typedef enum RDPCSPIPE_CLOCK_CNTL_RDPCS_SRAMCLK_GATE_DIS {
RDPCSPIPE_SRAMCLK_GATE_ENABLE            = 0x00000000,
RDPCSPIPE_SRAMCLK_GATE_DISABLE           = 0x00000001,
} RDPCSPIPE_CLOCK_CNTL_RDPCS_SRAMCLK_GATE_DIS;

/*
 * RDPCSPIPE_CLOCK_CNTL_RDPCS_SRAMCLK_PASS enum
 */

typedef enum RDPCSPIPE_CLOCK_CNTL_RDPCS_SRAMCLK_PASS {
RDPCSPIPE_SRAMCLK_NOT_PASS               = 0x00000000,
RDPCSPIPE_SRAMCLK_PASS                   = 0x00000001,
} RDPCSPIPE_CLOCK_CNTL_RDPCS_SRAMCLK_PASS;

/*
 * RDPCSPIPE_CNTL_RDPCS_PIPE_FIFO_EN enum
 */

typedef enum RDPCSPIPE_CNTL_RDPCS_PIPE_FIFO_EN {
RDPCS_PIPE_FIFO_DISABLE                  = 0x00000000,
RDPCS_PIPE_FIFO_ENABLE                   = 0x00000001,
} RDPCSPIPE_CNTL_RDPCS_PIPE_FIFO_EN;

/*
 * RDPCSPIPE_CNTL_RDPCS_PIPE_FIFO_LANE_EN enum
 */

typedef enum RDPCSPIPE_CNTL_RDPCS_PIPE_FIFO_LANE_EN {
RDPCS_PIPE_FIFO_LANE_DISABLE             = 0x00000000,
RDPCS_PIPE_FIFO_LANE_ENABLE              = 0x00000001,
} RDPCSPIPE_CNTL_RDPCS_PIPE_FIFO_LANE_EN;

/*
 * RDPCSPIPE_CNTL_RDPCS_PIPE_SOFT_RESET enum
 */

typedef enum RDPCSPIPE_CNTL_RDPCS_PIPE_SOFT_RESET {
RDPCS_PIPE_SOFT_RESET_DISABLE            = 0x00000000,
RDPCS_PIPE_SOFT_RESET_ENABLE             = 0x00000001,
} RDPCSPIPE_CNTL_RDPCS_PIPE_SOFT_RESET;

/*
 * RDPCSPIPE_CNTL_RDPCS_SRAM_SOFT_RESET enum
 */

typedef enum RDPCSPIPE_CNTL_RDPCS_SRAM_SOFT_RESET {
RDPCSPIPE_SRAM_SRAM_RESET_DISABLE        = 0x00000000,
RDPCSPIPE_SRAM_SRAM_RESET_ENABLE         = 0x00000001,
} RDPCSPIPE_CNTL_RDPCS_SRAM_SOFT_RESET;

/*
 * RDPCSPIPE_DBG_APB_COUNT_EXPIRE_MASK enum
 */

typedef enum RDPCSPIPE_DBG_APB_COUNT_EXPIRE_MASK {
RDPCSPIPE_DBG_APB_COUNT_EXPIRE_MASK_DISABLE = 0x00000000,
RDPCSPIPE_DBG_APB_COUNT_EXPIRE_MASK_ENABLE = 0x00000001,
} RDPCSPIPE_DBG_APB_COUNT_EXPIRE_MASK;

/*
 * RDPCSPIPE_DBG_OCLA_SEL enum
 */

typedef enum RDPCSPIPE_DBG_OCLA_SEL {
RDPCSPIPE_DBG_OCLA_SEL_MON_OUT_7_0       = 0x00000000,
RDPCSPIPE_DBG_OCLA_SEL_MON_OUT_15_8      = 0x00000001,
RDPCSPIPE_DBG_OCLA_SEL_MON_OUT_23_16     = 0x00000002,
RDPCSPIPE_DBG_OCLA_SEL_MON_OUT_31_24     = 0x00000003,
RDPCSPIPE_DBG_OCLA_SEL_MON_OUT_39_32     = 0x00000004,
RDPCSPIPE_DBG_OCLA_SEL_MON_OUT_47_40     = 0x00000005,
RDPCSPIPE_DBG_OCLA_SEL_MON_OUT_55_48     = 0x00000006,
RDPCSPIPE_DBG_OCLA_SEL_MON_OUT_63_56     = 0x00000007,
} RDPCSPIPE_DBG_OCLA_SEL;

/*
 * RDPCSPIPE_ENC_TYPE enum
 */

typedef enum RDPCSPIPE_ENC_TYPE {
HDMI_TMDS_OR_DP_8B10B                    = 0x00000000,
HDMI_FRL                                 = 0x00000001,
DP_128B132B                              = 0x00000002,
} RDPCSPIPE_ENC_TYPE;

/*
 * RDPCSPIPE_FIFO_EMPTY enum
 */

typedef enum RDPCSPIPE_FIFO_EMPTY {
RDPCSPIPE_FIFO_NOT_EMPTY                 = 0x00000000,
RDPCSPIPE_FIFO_IS_EMPTY                  = 0x00000001,
} RDPCSPIPE_FIFO_EMPTY;

/*
 * RDPCSPIPE_FIFO_FULL enum
 */

typedef enum RDPCSPIPE_FIFO_FULL {
RDPCSPIPE_FIFO_NOT_FULL                  = 0x00000000,
RDPCSPIPE_FIFO_IS_FULL                   = 0x00000001,
} RDPCSPIPE_FIFO_FULL;

/*
 * RDPCSPIPE_INTERRUPT_CONTROL_RDPCS_APB_PSLVERR_MASK enum
 */

typedef enum RDPCSPIPE_INTERRUPT_CONTROL_RDPCS_APB_PSLVERR_MASK {
RDPCSPIPE_APB_PSLVERR_MASK_DISABLE       = 0x00000000,
RDPCSPIPE_APB_PSLVERR_MASK_ENABLE        = 0x00000001,
} RDPCSPIPE_INTERRUPT_CONTROL_RDPCS_APB_PSLVERR_MASK;

/*
 * RDPCSPIPE_INTERRUPT_CONTROL_RDPCS_DPALT_4LANE_TOGGLE enum
 */

typedef enum RDPCSPIPE_INTERRUPT_CONTROL_RDPCS_DPALT_4LANE_TOGGLE {
RDPCSPIPE_DPALT_4LANE_TOGGLE_2LANE       = 0x00000000,
RDPCSPIPE_DPALT_4LANE_TOGGLE_4LANE       = 0x00000001,
} RDPCSPIPE_INTERRUPT_CONTROL_RDPCS_DPALT_4LANE_TOGGLE;

/*
 * RDPCSPIPE_INTERRUPT_CONTROL_RDPCS_DPALT_4LANE_TOGGLE_MASK enum
 */

typedef enum RDPCSPIPE_INTERRUPT_CONTROL_RDPCS_DPALT_4LANE_TOGGLE_MASK {
RDPCSPIPE_DPALT_4LANE_TOGGLE_MASK_DISABLE = 0x00000000,
RDPCSPIPE_DPALT_4LANE_TOGGLE_MASK_ENABLE = 0x00000001,
} RDPCSPIPE_INTERRUPT_CONTROL_RDPCS_DPALT_4LANE_TOGGLE_MASK;

/*
 * RDPCSPIPE_INTERRUPT_CONTROL_RDPCS_DPALT_DISABLE_TOGGLE enum
 */

typedef enum RDPCSPIPE_INTERRUPT_CONTROL_RDPCS_DPALT_DISABLE_TOGGLE {
RDPCSPIPE_DPALT_DISABLE_TOGGLE_ENABLE    = 0x00000000,
RDPCSPIPE_DPALT_DISABLE_TOGGLE_DISABLE   = 0x00000001,
} RDPCSPIPE_INTERRUPT_CONTROL_RDPCS_DPALT_DISABLE_TOGGLE;

/*
 * RDPCSPIPE_INTERRUPT_CONTROL_RDPCS_DPALT_DISABLE_TOGGLE_MASK enum
 */

typedef enum RDPCSPIPE_INTERRUPT_CONTROL_RDPCS_DPALT_DISABLE_TOGGLE_MASK {
RDPCSPIPE_DPALT_DISABLE_TOGGLE_MASK_DISABLE = 0x00000000,
RDPCSPIPE_DPALT_DISABLE_TOGGLE_MASK_ENABLE = 0x00000001,
} RDPCSPIPE_INTERRUPT_CONTROL_RDPCS_DPALT_DISABLE_TOGGLE_MASK;

/*
 * RDPCSPIPE_INTERRUPT_CONTROL_RDPCS_PIPE_FIFO_ERROR_MASK enum
 */

typedef enum RDPCSPIPE_INTERRUPT_CONTROL_RDPCS_PIPE_FIFO_ERROR_MASK {
RDPCSPIPE_PIPE_FIFO_ERROR_MASK_DISABLE   = 0x00000000,
RDPCSPIPE_PIPE_FIFO_ERROR_MASK_ENABLE    = 0x00000001,
} RDPCSPIPE_INTERRUPT_CONTROL_RDPCS_PIPE_FIFO_ERROR_MASK;

/*
 * RDPCSPIPE_INTERRUPT_CONTROL_RDPCS_REG_FIFO_ERROR_MASK enum
 */

typedef enum RDPCSPIPE_INTERRUPT_CONTROL_RDPCS_REG_FIFO_ERROR_MASK {
RDPCSPIPE_REG_FIFO_ERROR_MASK_DISABLE    = 0x00000000,
RDPCSPIPE_REG_FIFO_ERROR_MASK_ENABLE     = 0x00000001,
} RDPCSPIPE_INTERRUPT_CONTROL_RDPCS_REG_FIFO_ERROR_MASK;

/*
 * RDPCSPIPE_MSG_BUS_COUNT_EXPIRE_MASK enum
 */

typedef enum RDPCSPIPE_MSG_BUS_COUNT_EXPIRE_MASK {
RDPCSPIPE_MSG_BUS_COUNT_EXPIRE_MASK_DISABLE = 0x00000000,
RDPCSPIPE_MSG_BUS_COUNT_EXPIRE_MASK_ENABLE = 0x00000001,
} RDPCSPIPE_MSG_BUS_COUNT_EXPIRE_MASK;

/*
 * RDPCSPIPE_PACK_MODE enum
 */

typedef enum RDPCSPIPE_PACK_MODE {
TIGHT_PACK                               = 0x00000000,
LOOSE_PACK                               = 0x00000001,
} RDPCSPIPE_PACK_MODE;

/*
 * RDPCSPIPE_PHY_CNTL0_RDPCS_PHY_CR_MUX_SEL enum
 */

typedef enum RDPCSPIPE_PHY_CNTL0_RDPCS_PHY_CR_MUX_SEL {
RDPCSPIPE_PHY_CR_MUX_SEL_FOR_USB         = 0x00000000,
RDPCSPIPE_PHY_CR_MUX_SEL_FOR_DC          = 0x00000001,
} RDPCSPIPE_PHY_CNTL0_RDPCS_PHY_CR_MUX_SEL;

/*
 * RDPCSPIPE_PHY_CNTL0_RDPCS_PHY_CR_PARA_SEL enum
 */

typedef enum RDPCSPIPE_PHY_CNTL0_RDPCS_PHY_CR_PARA_SEL {
RDPCSPIPE_PHY_CR_PARA_SEL_JTAG           = 0x00000000,
RDPCSPIPE_PHY_CR_PARA_SEL_CR             = 0x00000001,
} RDPCSPIPE_PHY_CNTL0_RDPCS_PHY_CR_PARA_SEL;

/*
 * RDPCSPIPE_PHY_CNTL0_RDPCS_PHY_REF_RANGE enum
 */

typedef enum RDPCSPIPE_PHY_CNTL0_RDPCS_PHY_REF_RANGE {
RDPCSPIPE_PHY_REF_RANGE_0                = 0x00000000,
RDPCSPIPE_PHY_REF_RANGE_1                = 0x00000001,
RDPCSPIPE_PHY_REF_RANGE_2                = 0x00000002,
RDPCSPIPE_PHY_REF_RANGE_3                = 0x00000003,
RDPCSPIPE_PHY_REF_RANGE_4                = 0x00000004,
RDPCSPIPE_PHY_REF_RANGE_5                = 0x00000005,
RDPCSPIPE_PHY_REF_RANGE_6                = 0x00000006,
RDPCSPIPE_PHY_REF_RANGE_7                = 0x00000007,
} RDPCSPIPE_PHY_CNTL0_RDPCS_PHY_REF_RANGE;

/*
 * RDPCSPIPE_PHY_CNTL0_RDPCS_SRAM_EXT_LD_DONE enum
 */

typedef enum RDPCSPIPE_PHY_CNTL0_RDPCS_SRAM_EXT_LD_DONE {
RDPCSPIPE_SRAM_EXT_LD_NOT_DONE           = 0x00000000,
RDPCSPIPE_SRAM_EXT_LD_DONE               = 0x00000001,
} RDPCSPIPE_PHY_CNTL0_RDPCS_SRAM_EXT_LD_DONE;

/*
 * RDPCSPIPE_PHY_CNTL0_RDPCS_SRAM_INIT_DONE enum
 */

typedef enum RDPCSPIPE_PHY_CNTL0_RDPCS_SRAM_INIT_DONE {
RDPCSPIPE_SRAM_INIT_NOT_DONE             = 0x00000000,
RDPCSPIPE_SRAM_INIT_DONE                 = 0x00000001,
} RDPCSPIPE_PHY_CNTL0_RDPCS_SRAM_INIT_DONE;

/*
 * RDPCSPIPE_PHY_CNTL11_RDPCS_PHY_DP_REF_CLK_MPLLB_DIV enum
 */

typedef enum RDPCSPIPE_PHY_CNTL11_RDPCS_PHY_DP_REF_CLK_MPLLB_DIV {
RDPCSPIPE_PHY_DP_REF_CLK_MPLLB_DIV1      = 0x00000000,
RDPCSPIPE_PHY_DP_REF_CLK_MPLLB_DIV2      = 0x00000001,
RDPCSPIPE_PHY_DP_REF_CLK_MPLLB_DIV3      = 0x00000002,
RDPCSPIPE_PHY_DP_REF_CLK_MPLLB_DIV8      = 0x00000003,
RDPCSPIPE_PHY_DP_REF_CLK_MPLLB_DIV16     = 0x00000004,
} RDPCSPIPE_PHY_CNTL11_RDPCS_PHY_DP_REF_CLK_MPLLB_DIV;

/*
 * RDPCSPIPE_PHY_CNTL11_RDPCS_PHY_HDMI_MPLLB_HDMI_PIXEL_CLK_DIV enum
 */

typedef enum RDPCSPIPE_PHY_CNTL11_RDPCS_PHY_HDMI_MPLLB_HDMI_PIXEL_CLK_DIV {
RDPCSPIPE_PHY_HDMI_MPLLB_HDMI_PIXEL_CLK_DIV_0 = 0x00000000,
RDPCSPIPE_PHY_HDMI_MPLLB_HDMI_PIXEL_CLK_DIV_1 = 0x00000001,
RDPCSPIPE_PHY_HDMI_MPLLB_HDMI_PIXEL_CLK_DIV_2 = 0x00000002,
RDPCSPIPE_PHY_HDMI_MPLLB_HDMI_PIXEL_CLK_DIV_3 = 0x00000003,
} RDPCSPIPE_PHY_CNTL11_RDPCS_PHY_HDMI_MPLLB_HDMI_PIXEL_CLK_DIV;

/*
 * RDPCSPIPE_PHY_CNTL12_RDPCS_PHY_DP_MPLLB_TX_CLK_DIV enum
 */

typedef enum RDPCSPIPE_PHY_CNTL12_RDPCS_PHY_DP_MPLLB_TX_CLK_DIV {
RDPCSPIPE_PHY_DP_MPLLB_TX_CLK_DIV        = 0x00000000,
RDPCSPIPE_PHY_DP_MPLLB_TX_CLK_DIV2       = 0x00000001,
RDPCSPIPE_PHY_DP_MPLLB_TX_CLK_DIV4       = 0x00000002,
RDPCSPIPE_PHY_DP_MPLLB_TX_CLK_DIV8       = 0x00000003,
RDPCSPIPE_PHY_DP_MPLLB_TX_CLK_DIV3       = 0x00000004,
RDPCSPIPE_PHY_DP_MPLLB_TX_CLK_DIV5       = 0x00000005,
RDPCSPIPE_PHY_DP_MPLLB_TX_CLK_DIV6       = 0x00000006,
RDPCSPIPE_PHY_DP_MPLLB_TX_CLK_DIV10      = 0x00000007,
} RDPCSPIPE_PHY_CNTL12_RDPCS_PHY_DP_MPLLB_TX_CLK_DIV;

/*
 * RDPCSPIPE_PHY_CNTL4_RDPCS_PHY_DP_TX_TERM_CTRL enum
 */

typedef enum RDPCSPIPE_PHY_CNTL4_RDPCS_PHY_DP_TX_TERM_CTRL {
RDPCSPIPE_PHY_DP_TX_TERM_CTRL_54         = 0x00000000,
RDPCSPIPE_PHY_DP_TX_TERM_CTRL_52         = 0x00000001,
RDPCSPIPE_PHY_DP_TX_TERM_CTRL_50         = 0x00000002,
RDPCSPIPE_PHY_DP_TX_TERM_CTRL_48         = 0x00000003,
RDPCSPIPE_PHY_DP_TX_TERM_CTRL_46         = 0x00000004,
RDPCSPIPE_PHY_DP_TX_TERM_CTRL_44         = 0x00000005,
RDPCSPIPE_PHY_DP_TX_TERM_CTRL_42         = 0x00000006,
RDPCSPIPE_PHY_DP_TX_TERM_CTRL_40         = 0x00000007,
} RDPCSPIPE_PHY_CNTL4_RDPCS_PHY_DP_TX_TERM_CTRL;

/*
 * RDPCSPIPE_PHY_CNTL_RDPCS_PHY_DP_TX_DETRX_RESULT enum
 */

typedef enum RDPCSPIPE_PHY_CNTL_RDPCS_PHY_DP_TX_DETRX_RESULT {
RDPCSPIPE_PHY_DP_TX_DETRX_RESULT_NO_DETECT = 0x00000000,
RDPCSPIPE_PHY_DP_TX_DETRX_RESULT_DETECT  = 0x00000001,
} RDPCSPIPE_PHY_CNTL_RDPCS_PHY_DP_TX_DETRX_RESULT;

/*
 * RDPCSPIPE_PHY_CNTL_RDPCS_PHY_DP_TX_RATE enum
 */

typedef enum RDPCSPIPE_PHY_CNTL_RDPCS_PHY_DP_TX_RATE {
RDPCSPIPE_PHY_DP_TX_RATE                 = 0x00000000,
RDPCSPIPE_PHY_DP_TX_RATE_DIV2            = 0x00000001,
RDPCSPIPE_PHY_DP_TX_RATE_DIV4            = 0x00000002,
} RDPCSPIPE_PHY_CNTL_RDPCS_PHY_DP_TX_RATE;

/*
 * RDPCSPIPE_PHY_CNTL_RDPCS_PHY_DP_TX_WIDTH enum
 */

typedef enum RDPCSPIPE_PHY_CNTL_RDPCS_PHY_DP_TX_WIDTH {
RDPCSPIPE_PHY_DP_TX_WIDTH_8              = 0x00000000,
RDPCSPIPE_PHY_DP_TX_WIDTH_10             = 0x00000001,
RDPCSPIPE_PHY_DP_TX_WIDTH_16             = 0x00000002,
RDPCSPIPE_PHY_DP_TX_WIDTH_20             = 0x00000003,
} RDPCSPIPE_PHY_CNTL_RDPCS_PHY_DP_TX_WIDTH;

/*
 * RDPCSPIPE_PHY_CNTL_RRDPCS_PHY_DP_TX_PSTATE enum
 */

typedef enum RDPCSPIPE_PHY_CNTL_RRDPCS_PHY_DP_TX_PSTATE {
RRDPCSPIPE_PHY_DP_TX_PSTATE_POWER_UP     = 0x00000000,
RRDPCSPIPE_PHY_DP_TX_PSTATE_HOLD         = 0x00000001,
RRDPCSPIPE_PHY_DP_TX_PSTATE_HOLD_OFF     = 0x00000002,
RRDPCSPIPE_PHY_DP_TX_PSTATE_POWER_DOWN   = 0x00000003,
} RDPCSPIPE_PHY_CNTL_RRDPCS_PHY_DP_TX_PSTATE;

/*
 * RDPCSPIPE_PHY_IF_WIDTH enum
 */

typedef enum RDPCSPIPE_PHY_IF_WIDTH {
PHY_IF_WIDTH_10BIT                       = 0x00000000,
PHY_IF_WIDTH_20BIT                       = 0x00000001,
PHY_IF_WIDTH_40BIT                       = 0x00000002,
PHY_IF_WIDTH_80BIT                       = 0x00000003,
} RDPCSPIPE_PHY_IF_WIDTH;

/*
 * RDPCSPIPE_PHY_RATE enum
 */

typedef enum RDPCSPIPE_PHY_RATE {
PHY_DP_RATE_1P62                         = 0x00000000,
PHY_DP_RATE_2P7                          = 0x00000001,
PHY_DP_RATE_5P4                          = 0x00000002,
PHY_DP_RATE_8P1                          = 0x00000003,
PHY_DP_RATE_2P16                         = 0x00000004,
PHY_DP_RATE_2P43                         = 0x00000005,
PHY_DP_RATE_3P24                         = 0x00000006,
PHY_DP_RATE_4P32                         = 0x00000007,
PHY_DP_RATE_10P                          = 0x00000008,
PHY_DP_RATE_13P5                         = 0x00000009,
PHY_DP_RATE_20P                          = 0x0000000a,
PHY_CUSTOM_RATE                          = 0x0000000f,
} RDPCSPIPE_PHY_RATE;

/*
 * RDPCSPIPE_PHY_REF_ALT_CLK_EN enum
 */

typedef enum RDPCSPIPE_PHY_REF_ALT_CLK_EN {
RDPCSPIPE_PHY_REF_ALT_CLK_DISABLE        = 0x00000000,
RDPCSPIPE_PHY_REF_ALT_CLK_ENABLE         = 0x00000001,
} RDPCSPIPE_PHY_REF_ALT_CLK_EN;

/*
 * RDPCSPIPE_TEST_CLK_SEL enum
 */

typedef enum RDPCSPIPE_TEST_CLK_SEL {
RDPCSPIPE_TEST_CLK_SEL_NONE              = 0x00000000,
RDPCSPIPE_TEST_CLK_SEL_CFGCLK            = 0x00000001,
RDPCSPIPE_TEST_CLK_SEL_SYMCLK_DIV2_LDPCS = 0x00000002,
RDPCSPIPE_TEST_CLK_SEL_SYMCLK_DIV2_RDPCS = 0x00000003,
RDPCSPIPE_TEST_CLK_SEL_SYMCLK_DIV2_LDPCS_DIV4 = 0x00000004,
RDPCSPIPE_TEST_CLK_SEL_SYMCLK_DIV2_RDPCS_DIV4 = 0x00000005,
RDPCSPIPE_TEST_CLK_SEL_SRAMCLK           = 0x00000006,
RDPCSPIPE_TEST_CLK_SEL_EXT_CR_CLK        = 0x00000007,
RDPCSPIPE_TEST_CLK_SEL_DP_TX0_WORD_CLK   = 0x00000008,
RDPCSPIPE_TEST_CLK_SEL_DP_TX1_WORD_CLK   = 0x00000009,
RDPCSPIPE_TEST_CLK_SEL_DP_TX2_WORD_CLK   = 0x0000000a,
RDPCSPIPE_TEST_CLK_SEL_DP_TX3_WORD_CLK   = 0x0000000b,
RDPCSPIPE_TEST_CLK_SEL_DP_MPLLB_DIV_CLK  = 0x0000000c,
RDPCSPIPE_TEST_CLK_SEL_HDMI_MPLLB_HDMI_PIXEL_CLK = 0x0000000d,
RDPCSPIPE_TEST_CLK_SEL_PHY_REF_DIG_CLK   = 0x0000000e,
RDPCSPIPE_TEST_CLK_SEL_REF_DIG_FR_clk    = 0x0000000f,
RDPCSPIPE_TEST_CLK_SEL_dtb_out0          = 0x00000010,
RDPCSPIPE_TEST_CLK_SEL_dtb_out1          = 0x00000011,
} RDPCSPIPE_TEST_CLK_SEL;

/*
 * RDPCS_PIPE_CNTL_TX_LANE_PACK_FROM_MSB enum
 */

typedef enum RDPCS_PIPE_CNTL_TX_LANE_PACK_FROM_MSB {
RDPCSPIPE_LANE_PACK_FROM_MSB_DISABLE     = 0x00000000,
RDPCSPIPE_LANE_PACK_FROM_MSB_ENABLE      = 0x00000001,
} RDPCS_PIPE_CNTL_TX_LANE_PACK_FROM_MSB;

/*
 * RDPCS_PIPE_SRAM_CNTL_RDPCS_MEM_PWR_FORCE enum
 */

typedef enum RDPCS_PIPE_SRAM_CNTL_RDPCS_MEM_PWR_FORCE {
RDPCSPIPE_MEM_PWR_NO_FORCE               = 0x00000000,
RDPCSPIPE_MEM_PWR_LIGHT_SLEEP            = 0x00000001,
RDPCSPIPE_MEM_PWR_DEEP_SLEEP             = 0x00000002,
RDPCSPIPE_MEM_PWR_SHUT_DOWN              = 0x00000003,
} RDPCS_PIPE_SRAM_CNTL_RDPCS_MEM_PWR_FORCE;

/*
 * RDPCS_PIPE_SRAM_CNTL_RDPCS_MEM_PWR_PWR_STATE enum
 */

typedef enum RDPCS_PIPE_SRAM_CNTL_RDPCS_MEM_PWR_PWR_STATE {
RDPCSPIPE_MEM_PWR_PWR_STATE_ON           = 0x00000000,
RDPCSPIPE_MEM_PWR_PWR_STATE_LIGHT_SLEEP  = 0x00000001,
RDPCSPIPE_MEM_PWR_PWR_STATE_DEEP_SLEEP   = 0x00000002,
RDPCSPIPE_MEM_PWR_PWR_STATE_SHUT_DOWN    = 0x00000003,
} RDPCS_PIPE_SRAM_CNTL_RDPCS_MEM_PWR_PWR_STATE;

/*
 * RPDCSPIPE_CNTL_TX_LANE_BIT_ORDER_REVERSE_BEFORE_PACK enum
 */

typedef enum RPDCSPIPE_CNTL_TX_LANE_BIT_ORDER_REVERSE_BEFORE_PACK {
RDPCSPIPE_LANE_BIT_ORDER_REVERSE_DISABLE = 0x00000000,
RDPCSPIPE_LANE_BIT_ORDER_REVERSE_ENABLE  = 0x00000001,
} RPDCSPIPE_CNTL_TX_LANE_BIT_ORDER_REVERSE_BEFORE_PACK;

/*******************************************************
 * GDS Enums
 *******************************************************/

/*
 * GDS_PERFCOUNT_SELECT enum
 */

typedef enum GDS_PERFCOUNT_SELECT {
GDS_PERF_SEL_WR_COMP                     = 0x00000000,
GDS_PERF_SEL_WBUF_WR                     = 0x00000001,
GDS_PERF_SEL_SE0_NORET                   = 0x00000002,
GDS_PERF_SEL_SE0_RET                     = 0x00000003,
GDS_PERF_SEL_SE0_ORD_CNT                 = 0x00000004,
GDS_PERF_SEL_SE0_2COMP_REQ               = 0x00000005,
GDS_PERF_SEL_SE0_ORD_WAVE_VALID          = 0x00000006,
GDS_PERF_SEL_SE0_GDS_STALL_BY_ORD        = 0x00000007,
GDS_PERF_SEL_SE0_GDS_WR_OP               = 0x00000008,
GDS_PERF_SEL_SE0_GDS_RD_OP               = 0x00000009,
GDS_PERF_SEL_SE0_GDS_ATOM_OP             = 0x0000000a,
GDS_PERF_SEL_SE0_GDS_REL_OP              = 0x0000000b,
GDS_PERF_SEL_SE0_GDS_CMPXCH_OP           = 0x0000000c,
GDS_PERF_SEL_SE0_GDS_BYTE_OP             = 0x0000000d,
GDS_PERF_SEL_SE0_GDS_SHORT_OP            = 0x0000000e,
GDS_PERF_SEL_SE1_NORET                   = 0x0000000f,
GDS_PERF_SEL_SE1_RET                     = 0x00000010,
GDS_PERF_SEL_SE1_ORD_CNT                 = 0x00000011,
GDS_PERF_SEL_SE1_2COMP_REQ               = 0x00000012,
GDS_PERF_SEL_SE1_ORD_WAVE_VALID          = 0x00000013,
GDS_PERF_SEL_SE1_GDS_STALL_BY_ORD        = 0x00000014,
GDS_PERF_SEL_SE1_GDS_WR_OP               = 0x00000015,
GDS_PERF_SEL_SE1_GDS_RD_OP               = 0x00000016,
GDS_PERF_SEL_SE1_GDS_ATOM_OP             = 0x00000017,
GDS_PERF_SEL_SE1_GDS_REL_OP              = 0x00000018,
GDS_PERF_SEL_SE1_GDS_CMPXCH_OP           = 0x00000019,
GDS_PERF_SEL_SE1_GDS_BYTE_OP             = 0x0000001a,
GDS_PERF_SEL_SE1_GDS_SHORT_OP            = 0x0000001b,
GDS_PERF_SEL_SE2_NORET                   = 0x0000001c,
GDS_PERF_SEL_SE2_RET                     = 0x0000001d,
GDS_PERF_SEL_SE2_ORD_CNT                 = 0x0000001e,
GDS_PERF_SEL_SE2_2COMP_REQ               = 0x0000001f,
GDS_PERF_SEL_SE2_ORD_WAVE_VALID          = 0x00000020,
GDS_PERF_SEL_SE2_GDS_STALL_BY_ORD        = 0x00000021,
GDS_PERF_SEL_SE2_GDS_WR_OP               = 0x00000022,
GDS_PERF_SEL_SE2_GDS_RD_OP               = 0x00000023,
GDS_PERF_SEL_SE2_GDS_ATOM_OP             = 0x00000024,
GDS_PERF_SEL_SE2_GDS_REL_OP              = 0x00000025,
GDS_PERF_SEL_SE2_GDS_CMPXCH_OP           = 0x00000026,
GDS_PERF_SEL_SE2_GDS_BYTE_OP             = 0x00000027,
GDS_PERF_SEL_SE2_GDS_SHORT_OP            = 0x00000028,
GDS_PERF_SEL_SE3_NORET                   = 0x00000029,
GDS_PERF_SEL_SE3_RET                     = 0x0000002a,
GDS_PERF_SEL_SE3_ORD_CNT                 = 0x0000002b,
GDS_PERF_SEL_SE3_2COMP_REQ               = 0x0000002c,
GDS_PERF_SEL_SE3_ORD_WAVE_VALID          = 0x0000002d,
GDS_PERF_SEL_SE3_GDS_STALL_BY_ORD        = 0x0000002e,
GDS_PERF_SEL_SE3_GDS_WR_OP               = 0x0000002f,
GDS_PERF_SEL_SE3_GDS_RD_OP               = 0x00000030,
GDS_PERF_SEL_SE3_GDS_ATOM_OP             = 0x00000031,
GDS_PERF_SEL_SE3_GDS_REL_OP              = 0x00000032,
GDS_PERF_SEL_SE3_GDS_CMPXCH_OP           = 0x00000033,
GDS_PERF_SEL_SE3_GDS_BYTE_OP             = 0x00000034,
GDS_PERF_SEL_SE3_GDS_SHORT_OP            = 0x00000035,
GDS_PERF_SEL_SE4_NORET                   = 0x00000036,
GDS_PERF_SEL_SE4_RET                     = 0x00000037,
GDS_PERF_SEL_SE4_ORD_CNT                 = 0x00000038,
GDS_PERF_SEL_SE4_2COMP_REQ               = 0x00000039,
GDS_PERF_SEL_SE4_ORD_WAVE_VALID          = 0x0000003a,
GDS_PERF_SEL_SE4_GDS_STALL_BY_ORD        = 0x0000003b,
GDS_PERF_SEL_SE4_GDS_WR_OP               = 0x0000003c,
GDS_PERF_SEL_SE4_GDS_RD_OP               = 0x0000003d,
GDS_PERF_SEL_SE4_GDS_ATOM_OP             = 0x0000003e,
GDS_PERF_SEL_SE4_GDS_REL_OP              = 0x0000003f,
GDS_PERF_SEL_SE4_GDS_CMPXCH_OP           = 0x00000040,
GDS_PERF_SEL_SE4_GDS_BYTE_OP             = 0x00000041,
GDS_PERF_SEL_SE4_GDS_SHORT_OP            = 0x00000042,
GDS_PERF_SEL_SE5_NORET                   = 0x00000043,
GDS_PERF_SEL_SE5_RET                     = 0x00000044,
GDS_PERF_SEL_SE5_ORD_CNT                 = 0x00000045,
GDS_PERF_SEL_SE5_2COMP_REQ               = 0x00000046,
GDS_PERF_SEL_SE5_ORD_WAVE_VALID          = 0x00000047,
GDS_PERF_SEL_SE5_GDS_STALL_BY_ORD        = 0x00000048,
GDS_PERF_SEL_SE5_GDS_WR_OP               = 0x00000049,
GDS_PERF_SEL_SE5_GDS_RD_OP               = 0x0000004a,
GDS_PERF_SEL_SE5_GDS_ATOM_OP             = 0x0000004b,
GDS_PERF_SEL_SE5_GDS_REL_OP              = 0x0000004c,
GDS_PERF_SEL_SE5_GDS_CMPXCH_OP           = 0x0000004d,
GDS_PERF_SEL_SE5_GDS_BYTE_OP             = 0x0000004e,
GDS_PERF_SEL_SE5_GDS_SHORT_OP            = 0x0000004f,
GDS_PERF_SEL_SE6_NORET                   = 0x00000050,
GDS_PERF_SEL_SE6_RET                     = 0x00000051,
GDS_PERF_SEL_SE6_ORD_CNT                 = 0x00000052,
GDS_PERF_SEL_SE6_2COMP_REQ               = 0x00000053,
GDS_PERF_SEL_SE6_ORD_WAVE_VALID          = 0x00000054,
GDS_PERF_SEL_SE6_GDS_STALL_BY_ORD        = 0x00000055,
GDS_PERF_SEL_SE6_GDS_WR_OP               = 0x00000056,
GDS_PERF_SEL_SE6_GDS_RD_OP               = 0x00000057,
GDS_PERF_SEL_SE6_GDS_ATOM_OP             = 0x00000058,
GDS_PERF_SEL_SE6_GDS_REL_OP              = 0x00000059,
GDS_PERF_SEL_SE6_GDS_CMPXCH_OP           = 0x0000005a,
GDS_PERF_SEL_SE6_GDS_BYTE_OP             = 0x0000005b,
GDS_PERF_SEL_SE6_GDS_SHORT_OP            = 0x0000005c,
GDS_PERF_SEL_SE7_NORET                   = 0x0000005d,
GDS_PERF_SEL_SE7_RET                     = 0x0000005e,
GDS_PERF_SEL_SE7_ORD_CNT                 = 0x0000005f,
GDS_PERF_SEL_SE7_2COMP_REQ               = 0x00000060,
GDS_PERF_SEL_SE7_ORD_WAVE_VALID          = 0x00000061,
GDS_PERF_SEL_SE7_GDS_STALL_BY_ORD        = 0x00000062,
GDS_PERF_SEL_SE7_GDS_WR_OP               = 0x00000063,
GDS_PERF_SEL_SE7_GDS_RD_OP               = 0x00000064,
GDS_PERF_SEL_SE7_GDS_ATOM_OP             = 0x00000065,
GDS_PERF_SEL_SE7_GDS_REL_OP              = 0x00000066,
GDS_PERF_SEL_SE7_GDS_CMPXCH_OP           = 0x00000067,
GDS_PERF_SEL_SE7_GDS_BYTE_OP             = 0x00000068,
GDS_PERF_SEL_SE7_GDS_SHORT_OP            = 0x00000069,
GDS_PERF_SEL_GWS_RELEASED                = 0x0000006a,
GDS_PERF_SEL_GWS_BYPASS                  = 0x0000006b,
} GDS_PERFCOUNT_SELECT;

/*******************************************************
 * CB Enums
 *******************************************************/

/*
 * BlendOp enum
 */

typedef enum BlendOp {
BLEND_ZERO                               = 0x00000000,
BLEND_ONE                                = 0x00000001,
BLEND_SRC_COLOR                          = 0x00000002,
BLEND_ONE_MINUS_SRC_COLOR                = 0x00000003,
BLEND_SRC_ALPHA                          = 0x00000004,
BLEND_ONE_MINUS_SRC_ALPHA                = 0x00000005,
BLEND_DST_ALPHA                          = 0x00000006,
BLEND_ONE_MINUS_DST_ALPHA                = 0x00000007,
BLEND_DST_COLOR                          = 0x00000008,
BLEND_ONE_MINUS_DST_COLOR                = 0x00000009,
BLEND_SRC_ALPHA_SATURATE                 = 0x0000000a,
BLEND_CONSTANT_COLOR                     = 0x0000000b,
BLEND_ONE_MINUS_CONSTANT_COLOR           = 0x0000000c,
BLEND_SRC1_COLOR                         = 0x0000000d,
BLEND_INV_SRC1_COLOR                     = 0x0000000e,
BLEND_SRC1_ALPHA                         = 0x0000000f,
BLEND_INV_SRC1_ALPHA                     = 0x00000010,
BLEND_CONSTANT_ALPHA                     = 0x00000011,
BLEND_ONE_MINUS_CONSTANT_ALPHA           = 0x00000012,
} BlendOp;

/*
 * BlendOpt enum
 */

typedef enum BlendOpt {
FORCE_OPT_AUTO                           = 0x00000000,
FORCE_OPT_DISABLE                        = 0x00000001,
FORCE_OPT_ENABLE_IF_SRC_A_0              = 0x00000002,
FORCE_OPT_ENABLE_IF_SRC_RGB_0            = 0x00000003,
FORCE_OPT_ENABLE_IF_SRC_ARGB_0           = 0x00000004,
FORCE_OPT_ENABLE_IF_SRC_A_1              = 0x00000005,
FORCE_OPT_ENABLE_IF_SRC_RGB_1            = 0x00000006,
FORCE_OPT_ENABLE_IF_SRC_ARGB_1           = 0x00000007,
} BlendOpt;

/*
 * CBMode enum
 */

typedef enum CBMode {
CB_DISABLE                               = 0x00000000,
CB_NORMAL                                = 0x00000001,
CB_ELIMINATE_FAST_CLEAR                  = 0x00000002,
CB_DCC_DECOMPRESS                        = 0x00000003,
CB_RESERVED                              = 0x00000004,
} CBMode;

/*
 * CBPerfClearFilterSel enum
 */

typedef enum CBPerfClearFilterSel {
CB_PERF_CLEAR_FILTER_SEL_NONCLEAR        = 0x00000000,
CB_PERF_CLEAR_FILTER_SEL_CLEAR           = 0x00000001,
} CBPerfClearFilterSel;

/*
 * CBPerfOpFilterSel enum
 */

typedef enum CBPerfOpFilterSel {
CB_PERF_OP_FILTER_SEL_WRITE_ONLY         = 0x00000000,
CB_PERF_OP_FILTER_SEL_NEEDS_DESTINATION  = 0x00000001,
CB_PERF_OP_FILTER_SEL_RESOLVE            = 0x00000002,
CB_PERF_OP_FILTER_SEL_DECOMPRESS         = 0x00000003,
CB_PERF_OP_FILTER_SEL_FMASK_DECOMPRESS   = 0x00000004,
CB_PERF_OP_FILTER_SEL_ELIMINATE_FAST_CLEAR = 0x00000005,
} CBPerfOpFilterSel;

/*
 * CBPerfSel enum
 */

typedef enum CBPerfSel {
CB_PERF_SEL_NONE                         = 0x00000000,
CB_PERF_SEL_DRAWN_PIXEL                  = 0x00000001,
CB_PERF_SEL_DRAWN_QUAD                   = 0x00000002,
CB_PERF_SEL_DRAWN_QUAD_FRAGMENT          = 0x00000003,
CB_PERF_SEL_DRAWN_TILE                   = 0x00000004,
CB_PERF_SEL_FILTER_DRAWN_PIXEL           = 0x00000005,
CB_PERF_SEL_FILTER_DRAWN_QUAD            = 0x00000006,
CB_PERF_SEL_FILTER_DRAWN_QUAD_FRAGMENT   = 0x00000007,
CB_PERF_SEL_FILTER_DRAWN_TILE            = 0x00000008,
CB_PERF_SEL_CC_DCC_DECOMPRESS_TID_IN     = 0x00000009,
CB_PERF_SEL_CC_DCC_DECOMPRESS_TID_OUT    = 0x0000000a,
CB_PERF_SEL_CC_DCC_COMPRESS_TID_IN       = 0x0000000b,
CB_PERF_SEL_CC_DCC_COMPRESS_TID_OUT      = 0x0000000c,
CB_PERF_SEL_CC_MC_WRITE_REQUEST          = 0x0000000d,
CB_PERF_SEL_CC_MC_WRITE_REQUESTS_IN_FLIGHT = 0x0000000e,
CB_PERF_SEL_CC_MC_READ_REQUEST           = 0x0000000f,
CB_PERF_SEL_CC_MC_READ_REQUESTS_IN_FLIGHT = 0x00000010,
CB_PERF_SEL_DB_CB_EXPORT_VALID_READY     = 0x00000011,
CB_PERF_SEL_DB_CB_EXPORT_VALID_READYB    = 0x00000012,
CB_PERF_SEL_DB_CB_EXPORT_VALIDB_READY    = 0x00000013,
CB_PERF_SEL_DB_CB_EXPORT_VALIDB_READYB   = 0x00000014,
CB_PERF_SEL_RESERVED_21                  = 0x00000015,
CB_PERF_SEL_RESERVED_22                  = 0x00000016,
CB_PERF_SEL_RESERVED_23                  = 0x00000017,
CB_PERF_SEL_RESERVED_24                  = 0x00000018,
CB_PERF_SEL_RESERVED_25                  = 0x00000019,
CB_PERF_SEL_RESERVED_26                  = 0x0000001a,
CB_PERF_SEL_RESERVED_27                  = 0x0000001b,
CB_PERF_SEL_RESERVED_28                  = 0x0000001c,
CB_PERF_SEL_RESERVED_29                  = 0x0000001d,
CB_PERF_SEL_CB_RMI_WRREQ_VALID_READY     = 0x0000001e,
CB_PERF_SEL_CB_RMI_WRREQ_VALID_READYB    = 0x0000001f,
CB_PERF_SEL_CB_RMI_WRREQ_VALIDB_READY    = 0x00000020,
CB_PERF_SEL_CB_RMI_WRREQ_VALIDB_READYB   = 0x00000021,
CB_PERF_SEL_CB_RMI_RDREQ_VALID_READY     = 0x00000022,
CB_PERF_SEL_CB_RMI_RDREQ_VALID_READYB    = 0x00000023,
CB_PERF_SEL_CB_RMI_RDREQ_VALIDB_READY    = 0x00000024,
CB_PERF_SEL_CB_RMI_RDREQ_VALIDB_READYB   = 0x00000025,
CB_PERF_SEL_RESERVED_38                  = 0x00000026,
CB_PERF_SEL_RESERVED_39                  = 0x00000027,
CB_PERF_SEL_RESERVED_40                  = 0x00000028,
CB_PERF_SEL_RESERVED_41                  = 0x00000029,
CB_PERF_SEL_RESERVED_42                  = 0x0000002a,
CB_PERF_SEL_RESERVED_43                  = 0x0000002b,
CB_PERF_SEL_RESERVED_44                  = 0x0000002c,
CB_PERF_SEL_RESERVED_45                  = 0x0000002d,
CB_PERF_SEL_RESERVED_46                  = 0x0000002e,
CB_PERF_SEL_RESERVED_47                  = 0x0000002f,
CB_PERF_SEL_RESERVED_48                  = 0x00000030,
CB_PERF_SEL_RESERVED_49                  = 0x00000031,
CB_PERF_SEL_STATIC_CLOCK_EN              = 0x00000032,
CB_PERF_SEL_PERFMON_CLOCK_EN             = 0x00000033,
CB_PERF_SEL_BLEND_CLOCK_EN               = 0x00000034,
CB_PERF_SEL_COLOR_STORE_CLOCK_EN         = 0x00000035,
CB_PERF_SEL_BACKEND_READ_CLOCK_EN        = 0x00000036,
CB_PERF_SEL_GRBM_CLOCK_EN                = 0x00000037,
CB_PERF_SEL_MEMARB_CLOCK_EN              = 0x00000038,
CB_PERF_SEL_BACKEND_EVICT_PIPE_CLOCK_EN  = 0x00000039,
CB_PERF_SEL_BACKEND_FRAGOP_CLOCK_EN      = 0x0000003a,
CB_PERF_SEL_BACKEND_SRC_FIFO_CLOCK_EN    = 0x0000003b,
CB_PERF_SEL_BACKEND_CACHE_CTL_CLOCK_EN   = 0x0000003c,
CB_PERF_SEL_FRONTEND_INPUT_CLOCK_EN      = 0x0000003d,
CB_PERF_SEL_FRONTEND_ADDR_CLOCK_EN       = 0x0000003e,
CB_PERF_SEL_FRONTEND_FDCC_CLOCK_EN       = 0x0000003f,
CB_PERF_SEL_FRONTEND_SAMPLE_MASK_TRACKER_CLOCK_EN = 0x00000040,
CB_PERF_SEL_RESERVED_65                  = 0x00000041,
CB_PERF_SEL_RESERVED_66                  = 0x00000042,
CB_PERF_SEL_RESERVED_67                  = 0x00000043,
CB_PERF_SEL_RESERVED_68                  = 0x00000044,
CB_PERF_SEL_RESERVED_69                  = 0x00000045,
CB_PERF_SEL_RESERVED_70                  = 0x00000046,
CB_PERF_SEL_RESERVED_71                  = 0x00000047,
CB_PERF_SEL_RESERVED_72                  = 0x00000048,
CB_PERF_SEL_RESERVED_73                  = 0x00000049,
CB_PERF_SEL_RESERVED_74                  = 0x0000004a,
CB_PERF_SEL_RESERVED_75                  = 0x0000004b,
CB_PERF_SEL_RESERVED_76                  = 0x0000004c,
CB_PERF_SEL_RESERVED_77                  = 0x0000004d,
CB_PERF_SEL_RESERVED_78                  = 0x0000004e,
CB_PERF_SEL_RESERVED_79                  = 0x0000004f,
CB_PERF_SEL_RESERVED_80                  = 0x00000050,
CB_PERF_SEL_RESERVED_81                  = 0x00000051,
CB_PERF_SEL_RESERVED_82                  = 0x00000052,
CB_PERF_SEL_RESERVED_83                  = 0x00000053,
CB_PERF_SEL_RESERVED_84                  = 0x00000054,
CB_PERF_SEL_RESERVED_85                  = 0x00000055,
CB_PERF_SEL_RESERVED_86                  = 0x00000056,
CB_PERF_SEL_RESERVED_87                  = 0x00000057,
CB_PERF_SEL_RESERVED_88                  = 0x00000058,
CB_PERF_SEL_RESERVED_89                  = 0x00000059,
CB_PERF_SEL_RESERVED_90                  = 0x0000005a,
CB_PERF_SEL_RESERVED_91                  = 0x0000005b,
CB_PERF_SEL_RESERVED_92                  = 0x0000005c,
CB_PERF_SEL_RESERVED_93                  = 0x0000005d,
CB_PERF_SEL_RESERVED_94                  = 0x0000005e,
CB_PERF_SEL_RESERVED_95                  = 0x0000005f,
CB_PERF_SEL_RESERVED_96                  = 0x00000060,
CB_PERF_SEL_RESERVED_97                  = 0x00000061,
CB_PERF_SEL_RESERVED_98                  = 0x00000062,
CB_PERF_SEL_RESERVED_99                  = 0x00000063,
CB_PERF_SEL_CC_TAG_HIT                   = 0x00000064,
CB_PERF_SEL_CC_CACHE_TAG_MISS            = 0x00000065,
CB_PERF_SEL_CC_CACHE_SECTOR_MISS         = 0x00000066,
CB_PERF_SEL_CC_CACHE_SECTOR_HIT          = 0x00000067,
CB_PERF_SEL_CC_CACHE_REEVICTION_STALL    = 0x00000068,
CB_PERF_SEL_CC_CACHE_EVICT_NONZERO_INFLIGHT_STALL = 0x00000069,
CB_PERF_SEL_CC_CACHE_REPLACE_PENDING_EVICT_STALL = 0x0000006a,
CB_PERF_SEL_CC_CACHE_INFLIGHT_COUNTER_MAXIMUM_STALL = 0x0000006b,
CB_PERF_SEL_CC_CACHE_READ_OUTPUT_STALL   = 0x0000006c,
CB_PERF_SEL_CC_CACHE_WRITE_OUTPUT_STALL  = 0x0000006d,
CB_PERF_SEL_CC_CACHE_ACK_OUTPUT_STALL    = 0x0000006e,
CB_PERF_SEL_CC_CACHE_STALL               = 0x0000006f,
CB_PERF_SEL_CC_CACHE_FLUSH               = 0x00000070,
CB_PERF_SEL_CC_CACHE_TAGS_FLUSHED        = 0x00000071,
CB_PERF_SEL_CC_CACHE_WA_TO_RMW_CONVERSION = 0x00000072,
CB_PERF_SEL_CC_CACHE_SECTORS_FLUSHED     = 0x00000073,
CB_PERF_SEL_CC_CACHE_DIRTY_SECTORS_FLUSHED = 0x00000074,
CB_PERF_SEL_CC_CACHE_READS_SAVED_DUE_TO_DCC = 0x00000075,
CB_PERF_SEL_RESERVED_118                 = 0x00000076,
CB_PERF_SEL_RESERVED_119                 = 0x00000077,
CB_PERF_SEL_RESERVED_120                 = 0x00000078,
CB_PERF_SEL_RESERVED_121                 = 0x00000079,
CB_PERF_SEL_RESERVED_122                 = 0x0000007a,
CB_PERF_SEL_RESERVED_123                 = 0x0000007b,
CB_PERF_SEL_RESERVED_124                 = 0x0000007c,
CB_PERF_SEL_RESERVED_125                 = 0x0000007d,
CB_PERF_SEL_RESERVED_126                 = 0x0000007e,
CB_PERF_SEL_RESERVED_127                 = 0x0000007f,
CB_PERF_SEL_RESERVED_128                 = 0x00000080,
CB_PERF_SEL_RESERVED_129                 = 0x00000081,
CB_PERF_SEL_RESERVED_130                 = 0x00000082,
CB_PERF_SEL_RESERVED_131                 = 0x00000083,
CB_PERF_SEL_RESERVED_132                 = 0x00000084,
CB_PERF_SEL_RESERVED_133                 = 0x00000085,
CB_PERF_SEL_RESERVED_134                 = 0x00000086,
CB_PERF_SEL_RESERVED_135                 = 0x00000087,
CB_PERF_SEL_RESERVED_136                 = 0x00000088,
CB_PERF_SEL_RESERVED_137                 = 0x00000089,
CB_PERF_SEL_RESERVED_138                 = 0x0000008a,
CB_PERF_SEL_RESERVED_139                 = 0x0000008b,
CB_PERF_SEL_RESERVED_140                 = 0x0000008c,
CB_PERF_SEL_RESERVED_141                 = 0x0000008d,
CB_PERF_SEL_RESERVED_142                 = 0x0000008e,
CB_PERF_SEL_RESERVED_143                 = 0x0000008f,
CB_PERF_SEL_RESERVED_144                 = 0x00000090,
CB_PERF_SEL_RESERVED_145                 = 0x00000091,
CB_PERF_SEL_RESERVED_146                 = 0x00000092,
CB_PERF_SEL_RESERVED_147                 = 0x00000093,
CB_PERF_SEL_RESERVED_148                 = 0x00000094,
CB_PERF_SEL_RESERVED_149                 = 0x00000095,
CB_PERF_SEL_DCC_CACHE_PERF_HIT           = 0x00000096,
CB_PERF_SEL_DCC_CACHE_TAG_MISS           = 0x00000097,
CB_PERF_SEL_DCC_CACHE_SECTOR_MISS        = 0x00000098,
CB_PERF_SEL_DCC_CACHE_REEVICTION_STALL   = 0x00000099,
CB_PERF_SEL_DCC_CACHE_EVICT_NONZERO_INFLIGHT_STALL = 0x0000009a,
CB_PERF_SEL_DCC_CACHE_REPLACE_PENDING_EVICT_STALL = 0x0000009b,
CB_PERF_SEL_DCC_CACHE_INFLIGHT_COUNTER_MAXIMUM_STALL = 0x0000009c,
CB_PERF_SEL_DCC_CACHE_READ_OUTPUT_STALL  = 0x0000009d,
CB_PERF_SEL_DCC_CACHE_WRITE_OUTPUT_STALL = 0x0000009e,
CB_PERF_SEL_DCC_CACHE_ACK_OUTPUT_STALL   = 0x0000009f,
CB_PERF_SEL_DCC_CACHE_STALL              = 0x000000a0,
CB_PERF_SEL_DCC_CACHE_FLUSH              = 0x000000a1,
CB_PERF_SEL_DCC_CACHE_SECTORS_FLUSHED    = 0x000000a2,
CB_PERF_SEL_DCC_CACHE_DIRTY_SECTORS_FLUSHED = 0x000000a3,
CB_PERF_SEL_DCC_CACHE_TAGS_FLUSHED       = 0x000000a4,
CB_PERF_SEL_RESERVED_165                 = 0x000000a5,
CB_PERF_SEL_RESERVED_166                 = 0x000000a6,
CB_PERF_SEL_RESERVED_167                 = 0x000000a7,
CB_PERF_SEL_RESERVED_168                 = 0x000000a8,
CB_PERF_SEL_RESERVED_169                 = 0x000000a9,
CB_PERF_SEL_RESERVED_170                 = 0x000000aa,
CB_PERF_SEL_RESERVED_171                 = 0x000000ab,
CB_PERF_SEL_RESERVED_172                 = 0x000000ac,
CB_PERF_SEL_RESERVED_173                 = 0x000000ad,
CB_PERF_SEL_RESERVED_174                 = 0x000000ae,
CB_PERF_SEL_RESERVED_175                 = 0x000000af,
CB_PERF_SEL_RESERVED_176                 = 0x000000b0,
CB_PERF_SEL_RESERVED_177                 = 0x000000b1,
CB_PERF_SEL_RESERVED_178                 = 0x000000b2,
CB_PERF_SEL_RESERVED_179                 = 0x000000b3,
CB_PERF_SEL_RESERVED_180                 = 0x000000b4,
CB_PERF_SEL_RESERVED_181                 = 0x000000b5,
CB_PERF_SEL_RESERVED_182                 = 0x000000b6,
CB_PERF_SEL_RESERVED_183                 = 0x000000b7,
CB_PERF_SEL_RESERVED_184                 = 0x000000b8,
CB_PERF_SEL_RESERVED_185                 = 0x000000b9,
CB_PERF_SEL_RESERVED_186                 = 0x000000ba,
CB_PERF_SEL_RESERVED_187                 = 0x000000bb,
CB_PERF_SEL_RESERVED_188                 = 0x000000bc,
CB_PERF_SEL_RESERVED_189                 = 0x000000bd,
CB_PERF_SEL_RESERVED_190                 = 0x000000be,
CB_PERF_SEL_RESERVED_191                 = 0x000000bf,
CB_PERF_SEL_RESERVED_192                 = 0x000000c0,
CB_PERF_SEL_RESERVED_193                 = 0x000000c1,
CB_PERF_SEL_RESERVED_194                 = 0x000000c2,
CB_PERF_SEL_RESERVED_195                 = 0x000000c3,
CB_PERF_SEL_RESERVED_196                 = 0x000000c4,
CB_PERF_SEL_RESERVED_197                 = 0x000000c5,
CB_PERF_SEL_RESERVED_198                 = 0x000000c6,
CB_PERF_SEL_RESERVED_199                 = 0x000000c7,
CB_PERF_SEL_BLEND_QUAD_DST_READ_COULD_HAVE_BEEN_OPTIMIZED = 0x000000c8,
CB_PERF_SEL_BLEND_QUAD_BLENDING_COULD_HAVE_BEEN_BYPASSED = 0x000000c9,
CB_PERF_SEL_BLEND_QUAD_COULD_HAVE_BEEN_DISCARDED = 0x000000ca,
CB_PERF_SEL_BLEND_OPT_PIXELS_RESULT_EQ_DEST = 0x000000cb,
CB_PERF_SEL_BLEND_STALL_AT_OUTPUT        = 0x000000cc,
CB_PERF_SEL_RESERVED_205                 = 0x000000cd,
CB_PERF_SEL_RESERVED_206                 = 0x000000ce,
CB_PERF_SEL_RESERVED_207                 = 0x000000cf,
CB_PERF_SEL_RESERVED_208                 = 0x000000d0,
CB_PERF_SEL_RESERVED_209                 = 0x000000d1,
CB_PERF_SEL_RESERVED_210                 = 0x000000d2,
CB_PERF_SEL_RESERVED_211                 = 0x000000d3,
CB_PERF_SEL_RESERVED_212                 = 0x000000d4,
CB_PERF_SEL_RESERVED_213                 = 0x000000d5,
CB_PERF_SEL_RESERVED_214                 = 0x000000d6,
CB_PERF_SEL_RESERVED_215                 = 0x000000d7,
CB_PERF_SEL_RESERVED_216                 = 0x000000d8,
CB_PERF_SEL_RESERVED_217                 = 0x000000d9,
CB_PERF_SEL_RESERVED_218                 = 0x000000da,
CB_PERF_SEL_RESERVED_219                 = 0x000000db,
CB_PERF_SEL_RESERVED_220                 = 0x000000dc,
CB_PERF_SEL_RESERVED_221                 = 0x000000dd,
CB_PERF_SEL_RESERVED_222                 = 0x000000de,
CB_PERF_SEL_RESERVED_223                 = 0x000000df,
CB_PERF_SEL_RESERVED_224                 = 0x000000e0,
CB_PERF_SEL_RESERVED_225                 = 0x000000e1,
CB_PERF_SEL_RESERVED_226                 = 0x000000e2,
CB_PERF_SEL_RESERVED_227                 = 0x000000e3,
CB_PERF_SEL_RESERVED_228                 = 0x000000e4,
CB_PERF_SEL_RESERVED_229                 = 0x000000e5,
CB_PERF_SEL_RESERVED_230                 = 0x000000e6,
CB_PERF_SEL_RESERVED_231                 = 0x000000e7,
CB_PERF_SEL_RESERVED_232                 = 0x000000e8,
CB_PERF_SEL_RESERVED_233                 = 0x000000e9,
CB_PERF_SEL_RESERVED_234                 = 0x000000ea,
CB_PERF_SEL_RESERVED_235                 = 0x000000eb,
CB_PERF_SEL_RESERVED_236                 = 0x000000ec,
CB_PERF_SEL_RESERVED_237                 = 0x000000ed,
CB_PERF_SEL_RESERVED_238                 = 0x000000ee,
CB_PERF_SEL_RESERVED_239                 = 0x000000ef,
CB_PERF_SEL_RESERVED_240                 = 0x000000f0,
CB_PERF_SEL_RESERVED_241                 = 0x000000f1,
CB_PERF_SEL_RESERVED_242                 = 0x000000f2,
CB_PERF_SEL_RESERVED_243                 = 0x000000f3,
CB_PERF_SEL_RESERVED_244                 = 0x000000f4,
CB_PERF_SEL_RESERVED_245                 = 0x000000f5,
CB_PERF_SEL_RESERVED_246                 = 0x000000f6,
CB_PERF_SEL_RESERVED_247                 = 0x000000f7,
CB_PERF_SEL_RESERVED_248                 = 0x000000f8,
CB_PERF_SEL_RESERVED_249                 = 0x000000f9,
CB_PERF_SEL_EVENT                        = 0x000000fa,
CB_PERF_SEL_EVENT_CACHE_FLUSH_TS         = 0x000000fb,
CB_PERF_SEL_EVENT_CONTEXT_DONE           = 0x000000fc,
CB_PERF_SEL_EVENT_CACHE_FLUSH            = 0x000000fd,
CB_PERF_SEL_EVENT_CACHE_FLUSH_AND_INV_TS_EVENT = 0x000000fe,
CB_PERF_SEL_EVENT_CACHE_FLUSH_AND_INV_EVENT = 0x000000ff,
CB_PERF_SEL_EVENT_FLUSH_AND_INV_CB_DATA_TS = 0x00000100,
CB_PERF_SEL_EVENT_FLUSH_AND_INV_CB_META  = 0x00000101,
CB_PERF_SEL_CC_SURFACE_SYNC              = 0x00000102,
CB_PERF_SEL_RESERVED_259                 = 0x00000103,
CB_PERF_SEL_RESERVED_260                 = 0x00000104,
CB_PERF_SEL_RESERVED_261                 = 0x00000105,
CB_PERF_SEL_RESERVED_262                 = 0x00000106,
CB_PERF_SEL_RESERVED_263                 = 0x00000107,
CB_PERF_SEL_RESERVED_264                 = 0x00000108,
CB_PERF_SEL_RESERVED_265                 = 0x00000109,
CB_PERF_SEL_RESERVED_266                 = 0x0000010a,
CB_PERF_SEL_RESERVED_267                 = 0x0000010b,
CB_PERF_SEL_RESERVED_268                 = 0x0000010c,
CB_PERF_SEL_RESERVED_269                 = 0x0000010d,
CB_PERF_SEL_RESERVED_270                 = 0x0000010e,
CB_PERF_SEL_RESERVED_271                 = 0x0000010f,
CB_PERF_SEL_RESERVED_272                 = 0x00000110,
CB_PERF_SEL_RESERVED_273                 = 0x00000111,
CB_PERF_SEL_RESERVED_274                 = 0x00000112,
CB_PERF_SEL_RESERVED_275                 = 0x00000113,
CB_PERF_SEL_RESERVED_276                 = 0x00000114,
CB_PERF_SEL_RESERVED_277                 = 0x00000115,
CB_PERF_SEL_RESERVED_278                 = 0x00000116,
CB_PERF_SEL_RESERVED_279                 = 0x00000117,
CB_PERF_SEL_RESERVED_280                 = 0x00000118,
CB_PERF_SEL_RESERVED_281                 = 0x00000119,
CB_PERF_SEL_RESERVED_282                 = 0x0000011a,
CB_PERF_SEL_RESERVED_283                 = 0x0000011b,
CB_PERF_SEL_RESERVED_284                 = 0x0000011c,
CB_PERF_SEL_RESERVED_285                 = 0x0000011d,
CB_PERF_SEL_RESERVED_286                 = 0x0000011e,
CB_PERF_SEL_RESERVED_287                 = 0x0000011f,
CB_PERF_SEL_RESERVED_288                 = 0x00000120,
CB_PERF_SEL_RESERVED_289                 = 0x00000121,
CB_PERF_SEL_RESERVED_290                 = 0x00000122,
CB_PERF_SEL_RESERVED_291                 = 0x00000123,
CB_PERF_SEL_RESERVED_292                 = 0x00000124,
CB_PERF_SEL_RESERVED_293                 = 0x00000125,
CB_PERF_SEL_RESERVED_294                 = 0x00000126,
CB_PERF_SEL_RESERVED_295                 = 0x00000127,
CB_PERF_SEL_RESERVED_296                 = 0x00000128,
CB_PERF_SEL_RESERVED_297                 = 0x00000129,
CB_PERF_SEL_RESERVED_298                 = 0x0000012a,
CB_PERF_SEL_RESERVED_299                 = 0x0000012b,
CB_PERF_SEL_NACK_CC_READ                 = 0x0000012c,
CB_PERF_SEL_NACK_CC_WRITE                = 0x0000012d,
CB_PERF_SEL_EXPORT_32_ABGR_QUAD_FRAGMENT = 0x0000012e,
CB_PERF_SEL_RESERVED_303                 = 0x0000012f,
CB_PERF_SEL_RESERVED_304                 = 0x00000130,
CB_PERF_SEL_RESERVED_305                 = 0x00000131,
CB_PERF_SEL_RESERVED_306                 = 0x00000132,
CB_PERF_SEL_RESERVED_307                 = 0x00000133,
CB_PERF_SEL_RESERVED_308                 = 0x00000134,
CB_PERF_SEL_RESERVED_309                 = 0x00000135,
CB_PERF_SEL_RESERVED_310                 = 0x00000136,
CB_PERF_SEL_RESERVED_311                 = 0x00000137,
CB_PERF_SEL_RESERVED_312                 = 0x00000138,
CB_PERF_SEL_RESERVED_313                 = 0x00000139,
CB_PERF_SEL_RESERVED_314                 = 0x0000013a,
CB_PERF_SEL_RESERVED_315                 = 0x0000013b,
CB_PERF_SEL_RESERVED_316                 = 0x0000013c,
CB_PERF_SEL_RESERVED_317                 = 0x0000013d,
CB_PERF_SEL_RESERVED_318                 = 0x0000013e,
CB_PERF_SEL_RESERVED_319                 = 0x0000013f,
CB_PERF_SEL_RESERVED_320                 = 0x00000140,
CB_PERF_SEL_RESERVED_321                 = 0x00000141,
CB_PERF_SEL_RESERVED_322                 = 0x00000142,
CB_PERF_SEL_RESERVED_323                 = 0x00000143,
CB_PERF_SEL_RESERVED_324                 = 0x00000144,
CB_PERF_SEL_RESERVED_325                 = 0x00000145,
CB_PERF_SEL_RESERVED_326                 = 0x00000146,
CB_PERF_SEL_RESERVED_327                 = 0x00000147,
CB_PERF_SEL_RESERVED_328                 = 0x00000148,
CB_PERF_SEL_RESERVED_329                 = 0x00000149,
CB_PERF_SEL_RESERVED_330                 = 0x0000014a,
CB_PERF_SEL_RESERVED_331                 = 0x0000014b,
CB_PERF_SEL_RESERVED_332                 = 0x0000014c,
CB_PERF_SEL_RESERVED_333                 = 0x0000014d,
CB_PERF_SEL_RESERVED_334                 = 0x0000014e,
CB_PERF_SEL_RESERVED_335                 = 0x0000014f,
CB_PERF_SEL_RESERVED_336                 = 0x00000150,
CB_PERF_SEL_RESERVED_337                 = 0x00000151,
CB_PERF_SEL_RESERVED_338                 = 0x00000152,
CB_PERF_SEL_RESERVED_339                 = 0x00000153,
CB_PERF_SEL_RESERVED_340                 = 0x00000154,
CB_PERF_SEL_RESERVED_341                 = 0x00000155,
CB_PERF_SEL_RESERVED_342                 = 0x00000156,
CB_PERF_SEL_RESERVED_343                 = 0x00000157,
CB_PERF_SEL_RESERVED_344                 = 0x00000158,
CB_PERF_SEL_RESERVED_345                 = 0x00000159,
CB_PERF_SEL_RESERVED_346                 = 0x0000015a,
CB_PERF_SEL_RESERVED_347                 = 0x0000015b,
CB_PERF_SEL_RESERVED_348                 = 0x0000015c,
CB_PERF_SEL_RESERVED_349                 = 0x0000015d,
CB_PERF_SEL_RESERVED_350                 = 0x0000015e,
CB_PERF_SEL_RESERVED_351                 = 0x0000015f,
CB_PERF_SEL_RESERVED_352                 = 0x00000160,
CB_PERF_SEL_RESERVED_353                 = 0x00000161,
CB_PERF_SEL_RESERVED_354                 = 0x00000162,
CB_PERF_SEL_RESERVED_355                 = 0x00000163,
CB_PERF_SEL_RESERVED_356                 = 0x00000164,
CB_PERF_SEL_RESERVED_357                 = 0x00000165,
CB_PERF_SEL_RESERVED_358                 = 0x00000166,
CB_PERF_SEL_RESERVED_359                 = 0x00000167,
CB_PERF_SEL_RESERVED_360                 = 0x00000168,
CB_PERF_SEL_RESERVED_361                 = 0x00000169,
CB_PERF_SEL_RESERVED_362                 = 0x0000016a,
CB_PERF_SEL_RESERVED_363                 = 0x0000016b,
CB_PERF_SEL_RESERVED_364                 = 0x0000016c,
CB_PERF_SEL_RESERVED_365                 = 0x0000016d,
CB_PERF_SEL_RESERVED_366                 = 0x0000016e,
CB_PERF_SEL_RESERVED_367                 = 0x0000016f,
CB_PERF_SEL_RESERVED_368                 = 0x00000170,
CB_PERF_SEL_RESERVED_369                 = 0x00000171,
CB_PERF_SEL_RESERVED_370                 = 0x00000172,
CB_PERF_SEL_RESERVED_371                 = 0x00000173,
CB_PERF_SEL_RESERVED_372                 = 0x00000174,
CB_PERF_SEL_RESERVED_373                 = 0x00000175,
CB_PERF_SEL_RESERVED_374                 = 0x00000176,
CB_PERF_SEL_RESERVED_375                 = 0x00000177,
CB_PERF_SEL_RESERVED_376                 = 0x00000178,
CB_PERF_SEL_RESERVED_377                 = 0x00000179,
CB_PERF_SEL_RESERVED_378                 = 0x0000017a,
CB_PERF_SEL_RESERVED_379                 = 0x0000017b,
CB_PERF_SEL_RESERVED_380                 = 0x0000017c,
CB_PERF_SEL_RESERVED_381                 = 0x0000017d,
CB_PERF_SEL_RESERVED_382                 = 0x0000017e,
CB_PERF_SEL_RESERVED_383                 = 0x0000017f,
CB_PERF_SEL_RESERVED_384                 = 0x00000180,
CB_PERF_SEL_RESERVED_385                 = 0x00000181,
CB_PERF_SEL_RESERVED_386                 = 0x00000182,
CB_PERF_SEL_RESERVED_387                 = 0x00000183,
CB_PERF_SEL_RESERVED_388                 = 0x00000184,
CB_PERF_SEL_RESERVED_389                 = 0x00000185,
CB_PERF_SEL_RESERVED_390                 = 0x00000186,
CB_PERF_SEL_RESERVED_391                 = 0x00000187,
CB_PERF_SEL_RESERVED_392                 = 0x00000188,
CB_PERF_SEL_RESERVED_393                 = 0x00000189,
CB_PERF_SEL_RESERVED_394                 = 0x0000018a,
CB_PERF_SEL_RESERVED_395                 = 0x0000018b,
CB_PERF_SEL_RESERVED_396                 = 0x0000018c,
CB_PERF_SEL_RESERVED_397                 = 0x0000018d,
CB_PERF_SEL_RESERVED_398                 = 0x0000018e,
CB_PERF_SEL_RESERVED_399                 = 0x0000018f,
CB_PERF_SEL_RESERVED_400                 = 0x00000190,
CB_PERF_SEL_RESERVED_401                 = 0x00000191,
CB_PERF_SEL_RESERVED_402                 = 0x00000192,
CB_PERF_SEL_RESERVED_403                 = 0x00000193,
CB_PERF_SEL_RESERVED_404                 = 0x00000194,
CB_PERF_SEL_RESERVED_405                 = 0x00000195,
CB_PERF_SEL_RESERVED_406                 = 0x00000196,
CB_PERF_SEL_RESERVED_407                 = 0x00000197,
CB_PERF_SEL_RESERVED_408                 = 0x00000198,
CB_PERF_SEL_RESERVED_409                 = 0x00000199,
CB_PERF_SEL_RESERVED_410                 = 0x0000019a,
CB_PERF_SEL_RESERVED_411                 = 0x0000019b,
CB_PERF_SEL_RESERVED_412                 = 0x0000019c,
CB_PERF_SEL_RESERVED_413                 = 0x0000019d,
CB_PERF_SEL_RESERVED_414                 = 0x0000019e,
CB_PERF_SEL_RESERVED_415                 = 0x0000019f,
CB_PERF_SEL_RESERVED_416                 = 0x000001a0,
CB_PERF_SEL_RESERVED_417                 = 0x000001a1,
CB_PERF_SEL_RESERVED_418                 = 0x000001a2,
CB_PERF_SEL_RESERVED_419                 = 0x000001a3,
CB_PERF_SEL_RESERVED_420                 = 0x000001a4,
CB_PERF_SEL_RESERVED_421                 = 0x000001a5,
CB_PERF_SEL_RESERVED_422                 = 0x000001a6,
CB_PERF_SEL_RESERVED_423                 = 0x000001a7,
CB_PERF_SEL_RESERVED_424                 = 0x000001a8,
CB_PERF_SEL_RESERVED_425                 = 0x000001a9,
CB_PERF_SEL_RESERVED_426                 = 0x000001aa,
CB_PERF_SEL_RESERVED_427                 = 0x000001ab,
CB_PERF_SEL_RESERVED_428                 = 0x000001ac,
CB_PERF_SEL_RESERVED_429                 = 0x000001ad,
CB_PERF_SEL_RESERVED_430                 = 0x000001ae,
CB_PERF_SEL_RESERVED_431                 = 0x000001af,
CB_PERF_SEL_RESERVED_432                 = 0x000001b0,
CB_PERF_SEL_RESERVED_433                 = 0x000001b1,
CB_PERF_SEL_RESERVED_434                 = 0x000001b2,
CB_PERF_SEL_RESERVED_435                 = 0x000001b3,
CB_PERF_SEL_RESERVED_436                 = 0x000001b4,
CB_PERF_SEL_RESERVED_437                 = 0x000001b5,
CB_PERF_SEL_RESERVED_438                 = 0x000001b6,
CB_PERF_SEL_RESERVED_439                 = 0x000001b7,
CB_PERF_SEL_RESERVED_440                 = 0x000001b8,
CB_PERF_SEL_RESERVED_441                 = 0x000001b9,
CB_PERF_SEL_RESERVED_442                 = 0x000001ba,
CB_PERF_SEL_RESERVED_443                 = 0x000001bb,
CB_PERF_SEL_RESERVED_444                 = 0x000001bc,
CB_PERF_SEL_RESERVED_445                 = 0x000001bd,
CB_PERF_SEL_RESERVED_446                 = 0x000001be,
CB_PERF_SEL_RESERVED_447                 = 0x000001bf,
CB_PERF_SEL_RESERVED_448                 = 0x000001c0,
CB_PERF_SEL_RESERVED_449                 = 0x000001c1,
CB_PERF_SEL_RESERVED_450                 = 0x000001c2,
CB_PERF_SEL_RESERVED_451                 = 0x000001c3,
CB_PERF_SEL_RESERVED_452                 = 0x000001c4,
CB_PERF_SEL_RESERVED_453                 = 0x000001c5,
CB_PERF_SEL_RESERVED_454                 = 0x000001c6,
CB_PERF_SEL_RESERVED_455                 = 0x000001c7,
CB_PERF_SEL_RESERVED_456                 = 0x000001c8,
CB_PERF_SEL_RESERVED_457                 = 0x000001c9,
CB_PERF_SEL_RESERVED_458                 = 0x000001ca,
CB_PERF_SEL_RESERVED_459                 = 0x000001cb,
CB_PERF_SEL_RESERVED_460                 = 0x000001cc,
CB_PERF_SEL_RESERVED_461                 = 0x000001cd,
CB_PERF_SEL_RESERVED_462                 = 0x000001ce,
CB_PERF_SEL_RESERVED_463                 = 0x000001cf,
CB_PERF_SEL_RESERVED_464                 = 0x000001d0,
CB_PERF_SEL_RESERVED_465                 = 0x000001d1,
} CBPerfSel;

/*
 * CBRamList enum
 */

typedef enum CBRamList {
CB_DCG_CCC_CAS_TAG_ARRAY                 = 0x00000000,
CB_DCG_CCC_CAS_FRAG_PTR                  = 0x00000001,
CB_DCG_CCC_CAS_COLOR_PTR                 = 0x00000002,
CB_DCG_CCC_CAS_SURF_PARAM                = 0x00000003,
CB_DCG_CCC_CAS_KEYID                     = 0x00000004,
CB_DCG_BACKEND_RDLAT_FIFO                = 0x00000005,
CB_DCG_FRONTEND_RDLAT_FIFO               = 0x00000006,
CB_DCG_SRC_FIFO                          = 0x00000007,
CB_DCG_COLOR_STORE                       = 0x00000008,
CB_DCG_COLOR_STORE_DIRTY_BYTE            = 0x00000009,
CB_DCG_FMASK_CACHE_STORE                 = 0x0000000a,
CB_DCG_READ_SKID_FIFO                    = 0x0000000b,
CB_DCG_QUAD_PTR_FIFO                     = 0x0000000c,
CB_DCG_OUTPUT_FIFO                       = 0x0000000d,
CB_DCG_DCC_CACHE                         = 0x0000000e,
CB_DCG_DCC_DIRTY_BITS                    = 0x0000000f,
CB_DCG_QBLOCK_ALLOC                      = 0x00000010,
} CBRamList;

/*
 * CmaskCode enum
 */

typedef enum CmaskCode {
CMASK_CLR00_F0                           = 0x00000000,
CMASK_CLR00_F1                           = 0x00000001,
CMASK_CLR00_F2                           = 0x00000002,
CMASK_CLR00_FX                           = 0x00000003,
CMASK_CLR01_F0                           = 0x00000004,
CMASK_CLR01_F1                           = 0x00000005,
CMASK_CLR01_F2                           = 0x00000006,
CMASK_CLR01_FX                           = 0x00000007,
CMASK_CLR10_F0                           = 0x00000008,
CMASK_CLR10_F1                           = 0x00000009,
CMASK_CLR10_F2                           = 0x0000000a,
CMASK_CLR10_FX                           = 0x0000000b,
CMASK_CLR11_F0                           = 0x0000000c,
CMASK_CLR11_F1                           = 0x0000000d,
CMASK_CLR11_F2                           = 0x0000000e,
CMASK_CLR11_FX                           = 0x0000000f,
} CmaskCode;

/*
 * CombFunc enum
 */

typedef enum CombFunc {
COMB_DST_PLUS_SRC                        = 0x00000000,
COMB_SRC_MINUS_DST                       = 0x00000001,
COMB_MIN_DST_SRC                         = 0x00000002,
COMB_MAX_DST_SRC                         = 0x00000003,
COMB_DST_MINUS_SRC                       = 0x00000004,
} CombFunc;

/*
 * MemArbMode enum
 */

typedef enum MemArbMode {
MEM_ARB_MODE_FIXED                       = 0x00000000,
MEM_ARB_MODE_AGE                         = 0x00000001,
MEM_ARB_MODE_WEIGHT                      = 0x00000002,
MEM_ARB_MODE_BOTH                        = 0x00000003,
} MemArbMode;

/*
 * SourceFormat enum
 */

typedef enum SourceFormat {
EXPORT_4C_32BPC                          = 0x00000000,
EXPORT_4C_16BPC                          = 0x00000001,
EXPORT_2C_32BPC_GR                       = 0x00000002,
EXPORT_2C_32BPC_AR                       = 0x00000003,
} SourceFormat;

/*******************************************************
 * SC Enums
 *******************************************************/

/*
 * BinEventCntl enum
 */

typedef enum BinEventCntl {
BINNER_BREAK_BATCH                       = 0x00000000,
BINNER_PIPELINE                          = 0x00000001,
BINNER_DROP                              = 0x00000002,
BINNER_PIPELINE_BREAK                    = 0x00000003,
} BinEventCntl;

/*
 * BinMapMode enum
 */

typedef enum BinMapMode {
BIN_MAP_MODE_NONE                        = 0x00000000,
BIN_MAP_MODE_RTA_INDEX                   = 0x00000001,
BIN_MAP_MODE_POPS                        = 0x00000002,
} BinMapMode;

/*
 * BinSizeExtend enum
 */

typedef enum BinSizeExtend {
BIN_SIZE_32_PIXELS                       = 0x00000000,
BIN_SIZE_64_PIXELS                       = 0x00000001,
BIN_SIZE_128_PIXELS                      = 0x00000002,
BIN_SIZE_256_PIXELS                      = 0x00000003,
BIN_SIZE_512_PIXELS                      = 0x00000004,
} BinSizeExtend;

/*
 * BinningMode enum
 */

typedef enum BinningMode {
BINNING_ALLOWED                          = 0x00000000,
FORCE_BINNING_ON                         = 0x00000001,
DISABLE_BINNING_USE_NEW_SC               = 0x00000002,
DISABLE_BINNING_USE_LEGACY_SC            = 0x00000003,
} BinningMode;

/*
 * CovToShaderSel enum
 */

typedef enum CovToShaderSel {
INPUT_COVERAGE                           = 0x00000000,
INPUT_INNER_COVERAGE                     = 0x00000001,
INPUT_DEPTH_COVERAGE                     = 0x00000002,
RAW                                      = 0x00000003,
} CovToShaderSel;

/*
 * PkrMap enum
 */

typedef enum PkrMap {
RASTER_CONFIG_PKR_MAP_0                  = 0x00000000,
RASTER_CONFIG_PKR_MAP_1                  = 0x00000001,
RASTER_CONFIG_PKR_MAP_2                  = 0x00000002,
RASTER_CONFIG_PKR_MAP_3                  = 0x00000003,
} PkrMap;

/*
 * PkrXsel enum
 */

typedef enum PkrXsel {
RASTER_CONFIG_PKR_XSEL_0                 = 0x00000000,
RASTER_CONFIG_PKR_XSEL_1                 = 0x00000001,
RASTER_CONFIG_PKR_XSEL_2                 = 0x00000002,
RASTER_CONFIG_PKR_XSEL_3                 = 0x00000003,
} PkrXsel;

/*
 * PkrXsel2 enum
 */

typedef enum PkrXsel2 {
RASTER_CONFIG_PKR_XSEL2_0                = 0x00000000,
RASTER_CONFIG_PKR_XSEL2_1                = 0x00000001,
RASTER_CONFIG_PKR_XSEL2_2                = 0x00000002,
RASTER_CONFIG_PKR_XSEL2_3                = 0x00000003,
} PkrXsel2;

/*
 * PkrYsel enum
 */

typedef enum PkrYsel {
RASTER_CONFIG_PKR_YSEL_0                 = 0x00000000,
RASTER_CONFIG_PKR_YSEL_1                 = 0x00000001,
RASTER_CONFIG_PKR_YSEL_2                 = 0x00000002,
RASTER_CONFIG_PKR_YSEL_3                 = 0x00000003,
} PkrYsel;

/*
 * RbMap enum
 */

typedef enum RbMap {
RASTER_CONFIG_RB_MAP_0                   = 0x00000000,
RASTER_CONFIG_RB_MAP_1                   = 0x00000001,
RASTER_CONFIG_RB_MAP_2                   = 0x00000002,
RASTER_CONFIG_RB_MAP_3                   = 0x00000003,
} RbMap;

/*
 * RbXsel enum
 */

typedef enum RbXsel {
RASTER_CONFIG_RB_XSEL_0                  = 0x00000000,
RASTER_CONFIG_RB_XSEL_1                  = 0x00000001,
} RbXsel;

/*
 * RbXsel2 enum
 */

typedef enum RbXsel2 {
RASTER_CONFIG_RB_XSEL2_0                 = 0x00000000,
RASTER_CONFIG_RB_XSEL2_1                 = 0x00000001,
RASTER_CONFIG_RB_XSEL2_2                 = 0x00000002,
RASTER_CONFIG_RB_XSEL2_3                 = 0x00000003,
} RbXsel2;

/*
 * RbYsel enum
 */

typedef enum RbYsel {
RASTER_CONFIG_RB_YSEL_0                  = 0x00000000,
RASTER_CONFIG_RB_YSEL_1                  = 0x00000001,
} RbYsel;

/*
 * SC_PERFCNT_SEL enum
 */

typedef enum SC_PERFCNT_SEL {
SC_SRPS_WINDOW_VALID                     = 0x00000000,
SC_PSSW_WINDOW_VALID                     = 0x00000001,
SC_TPQZ_WINDOW_VALID                     = 0x00000002,
SC_QZQP_WINDOW_VALID                     = 0x00000003,
SC_TRPK_WINDOW_VALID                     = 0x00000004,
SC_SRPS_WINDOW_VALID_BUSY                = 0x00000005,
SC_PSSW_WINDOW_VALID_BUSY                = 0x00000006,
SC_TPQZ_WINDOW_VALID_BUSY                = 0x00000007,
SC_QZQP_WINDOW_VALID_BUSY                = 0x00000008,
SC_TRPK_WINDOW_VALID_BUSY                = 0x00000009,
SC_STARVED_BY_PA                         = 0x0000000a,
SC_STALLED_BY_PRIMFIFO                   = 0x0000000b,
SC_STALLED_BY_DB_TILE                    = 0x0000000c,
SC_STARVED_BY_DB_TILE                    = 0x0000000d,
SC_STALLED_BY_TILEORDERFIFO              = 0x0000000e,
SC_STALLED_BY_TILEFIFO                   = 0x0000000f,
SC_STALLED_BY_DB_QUAD                    = 0x00000010,
SC_STARVED_BY_DB_QUAD                    = 0x00000011,
SC_STALLED_BY_QUADFIFO                   = 0x00000012,
SC_STALLED_BY_BCI                        = 0x00000013,
SC_STALLED_BY_SPI                        = 0x00000014,
SC_SCISSOR_DISCARD                       = 0x00000015,
SC_BB_DISCARD                            = 0x00000016,
SC_SUPERTILE_COUNT                       = 0x00000017,
SC_SUPERTILE_PER_PRIM_H0                 = 0x00000018,
SC_SUPERTILE_PER_PRIM_H1                 = 0x00000019,
SC_SUPERTILE_PER_PRIM_H2                 = 0x0000001a,
SC_SUPERTILE_PER_PRIM_H3                 = 0x0000001b,
SC_SUPERTILE_PER_PRIM_H4                 = 0x0000001c,
SC_SUPERTILE_PER_PRIM_H5                 = 0x0000001d,
SC_SUPERTILE_PER_PRIM_H6                 = 0x0000001e,
SC_SUPERTILE_PER_PRIM_H7                 = 0x0000001f,
SC_SUPERTILE_PER_PRIM_H8                 = 0x00000020,
SC_SUPERTILE_PER_PRIM_H9                 = 0x00000021,
SC_SUPERTILE_PER_PRIM_H10                = 0x00000022,
SC_SUPERTILE_PER_PRIM_H11                = 0x00000023,
SC_SUPERTILE_PER_PRIM_H12                = 0x00000024,
SC_SUPERTILE_PER_PRIM_H13                = 0x00000025,
SC_SUPERTILE_PER_PRIM_H14                = 0x00000026,
SC_SUPERTILE_PER_PRIM_H15                = 0x00000027,
SC_SUPERTILE_PER_PRIM_H16                = 0x00000028,
SC_TILE_PER_PRIM_H0                      = 0x00000029,
SC_TILE_PER_PRIM_H1                      = 0x0000002a,
SC_TILE_PER_PRIM_H2                      = 0x0000002b,
SC_TILE_PER_PRIM_H3                      = 0x0000002c,
SC_TILE_PER_PRIM_H4                      = 0x0000002d,
SC_TILE_PER_PRIM_H5                      = 0x0000002e,
SC_TILE_PER_PRIM_H6                      = 0x0000002f,
SC_TILE_PER_PRIM_H7                      = 0x00000030,
SC_TILE_PER_PRIM_H8                      = 0x00000031,
SC_TILE_PER_PRIM_H9                      = 0x00000032,
SC_TILE_PER_PRIM_H10                     = 0x00000033,
SC_TILE_PER_PRIM_H11                     = 0x00000034,
SC_TILE_PER_PRIM_H12                     = 0x00000035,
SC_TILE_PER_PRIM_H13                     = 0x00000036,
SC_TILE_PER_PRIM_H14                     = 0x00000037,
SC_TILE_PER_PRIM_H15                     = 0x00000038,
SC_TILE_PER_PRIM_H16                     = 0x00000039,
SC_TILE_PER_SUPERTILE_H0                 = 0x0000003a,
SC_TILE_PER_SUPERTILE_H1                 = 0x0000003b,
SC_TILE_PER_SUPERTILE_H2                 = 0x0000003c,
SC_TILE_PER_SUPERTILE_H3                 = 0x0000003d,
SC_TILE_PER_SUPERTILE_H4                 = 0x0000003e,
SC_TILE_PER_SUPERTILE_H5                 = 0x0000003f,
SC_TILE_PER_SUPERTILE_H6                 = 0x00000040,
SC_TILE_PER_SUPERTILE_H7                 = 0x00000041,
SC_TILE_PER_SUPERTILE_H8                 = 0x00000042,
SC_TILE_PER_SUPERTILE_H9                 = 0x00000043,
SC_TILE_PER_SUPERTILE_H10                = 0x00000044,
SC_TILE_PER_SUPERTILE_H11                = 0x00000045,
SC_TILE_PER_SUPERTILE_H12                = 0x00000046,
SC_TILE_PER_SUPERTILE_H13                = 0x00000047,
SC_TILE_PER_SUPERTILE_H14                = 0x00000048,
SC_TILE_PER_SUPERTILE_H15                = 0x00000049,
SC_TILE_PER_SUPERTILE_H16                = 0x0000004a,
SC_TILE_PICKED_H1                        = 0x0000004b,
SC_TILE_PICKED_H2                        = 0x0000004c,
SC_TILE_PICKED_H3                        = 0x0000004d,
SC_TILE_PICKED_H4                        = 0x0000004e,
SC_QZ0_TILE_COUNT                        = 0x0000004f,
SC_QZ1_TILE_COUNT                        = 0x00000050,
SC_QZ2_TILE_COUNT                        = 0x00000051,
SC_QZ3_TILE_COUNT                        = 0x00000052,
SC_QZ0_TILE_COVERED_COUNT                = 0x00000053,
SC_QZ1_TILE_COVERED_COUNT                = 0x00000054,
SC_QZ2_TILE_COVERED_COUNT                = 0x00000055,
SC_QZ3_TILE_COVERED_COUNT                = 0x00000056,
SC_QZ0_TILE_NOT_COVERED_COUNT            = 0x00000057,
SC_QZ1_TILE_NOT_COVERED_COUNT            = 0x00000058,
SC_QZ2_TILE_NOT_COVERED_COUNT            = 0x00000059,
SC_QZ3_TILE_NOT_COVERED_COUNT            = 0x0000005a,
SC_QZ0_QUAD_PER_TILE_H0                  = 0x0000005b,
SC_QZ0_QUAD_PER_TILE_H1                  = 0x0000005c,
SC_QZ0_QUAD_PER_TILE_H2                  = 0x0000005d,
SC_QZ0_QUAD_PER_TILE_H3                  = 0x0000005e,
SC_QZ0_QUAD_PER_TILE_H4                  = 0x0000005f,
SC_QZ0_QUAD_PER_TILE_H5                  = 0x00000060,
SC_QZ0_QUAD_PER_TILE_H6                  = 0x00000061,
SC_QZ0_QUAD_PER_TILE_H7                  = 0x00000062,
SC_QZ0_QUAD_PER_TILE_H8                  = 0x00000063,
SC_QZ0_QUAD_PER_TILE_H9                  = 0x00000064,
SC_QZ0_QUAD_PER_TILE_H10                 = 0x00000065,
SC_QZ0_QUAD_PER_TILE_H11                 = 0x00000066,
SC_QZ0_QUAD_PER_TILE_H12                 = 0x00000067,
SC_QZ0_QUAD_PER_TILE_H13                 = 0x00000068,
SC_QZ0_QUAD_PER_TILE_H14                 = 0x00000069,
SC_QZ0_QUAD_PER_TILE_H15                 = 0x0000006a,
SC_QZ0_QUAD_PER_TILE_H16                 = 0x0000006b,
SC_QZ1_QUAD_PER_TILE_H0                  = 0x0000006c,
SC_QZ1_QUAD_PER_TILE_H1                  = 0x0000006d,
SC_QZ1_QUAD_PER_TILE_H2                  = 0x0000006e,
SC_QZ1_QUAD_PER_TILE_H3                  = 0x0000006f,
SC_QZ1_QUAD_PER_TILE_H4                  = 0x00000070,
SC_QZ1_QUAD_PER_TILE_H5                  = 0x00000071,
SC_QZ1_QUAD_PER_TILE_H6                  = 0x00000072,
SC_QZ1_QUAD_PER_TILE_H7                  = 0x00000073,
SC_QZ1_QUAD_PER_TILE_H8                  = 0x00000074,
SC_QZ1_QUAD_PER_TILE_H9                  = 0x00000075,
SC_QZ1_QUAD_PER_TILE_H10                 = 0x00000076,
SC_QZ1_QUAD_PER_TILE_H11                 = 0x00000077,
SC_QZ1_QUAD_PER_TILE_H12                 = 0x00000078,
SC_QZ1_QUAD_PER_TILE_H13                 = 0x00000079,
SC_QZ1_QUAD_PER_TILE_H14                 = 0x0000007a,
SC_QZ1_QUAD_PER_TILE_H15                 = 0x0000007b,
SC_QZ1_QUAD_PER_TILE_H16                 = 0x0000007c,
SC_QZ2_QUAD_PER_TILE_H0                  = 0x0000007d,
SC_QZ2_QUAD_PER_TILE_H1                  = 0x0000007e,
SC_QZ2_QUAD_PER_TILE_H2                  = 0x0000007f,
SC_QZ2_QUAD_PER_TILE_H3                  = 0x00000080,
SC_QZ2_QUAD_PER_TILE_H4                  = 0x00000081,
SC_QZ2_QUAD_PER_TILE_H5                  = 0x00000082,
SC_QZ2_QUAD_PER_TILE_H6                  = 0x00000083,
SC_QZ2_QUAD_PER_TILE_H7                  = 0x00000084,
SC_QZ2_QUAD_PER_TILE_H8                  = 0x00000085,
SC_QZ2_QUAD_PER_TILE_H9                  = 0x00000086,
SC_QZ2_QUAD_PER_TILE_H10                 = 0x00000087,
SC_QZ2_QUAD_PER_TILE_H11                 = 0x00000088,
SC_QZ2_QUAD_PER_TILE_H12                 = 0x00000089,
SC_QZ2_QUAD_PER_TILE_H13                 = 0x0000008a,
SC_QZ2_QUAD_PER_TILE_H14                 = 0x0000008b,
SC_QZ2_QUAD_PER_TILE_H15                 = 0x0000008c,
SC_QZ2_QUAD_PER_TILE_H16                 = 0x0000008d,
SC_QZ3_QUAD_PER_TILE_H0                  = 0x0000008e,
SC_QZ3_QUAD_PER_TILE_H1                  = 0x0000008f,
SC_QZ3_QUAD_PER_TILE_H2                  = 0x00000090,
SC_QZ3_QUAD_PER_TILE_H3                  = 0x00000091,
SC_QZ3_QUAD_PER_TILE_H4                  = 0x00000092,
SC_QZ3_QUAD_PER_TILE_H5                  = 0x00000093,
SC_QZ3_QUAD_PER_TILE_H6                  = 0x00000094,
SC_QZ3_QUAD_PER_TILE_H7                  = 0x00000095,
SC_QZ3_QUAD_PER_TILE_H8                  = 0x00000096,
SC_QZ3_QUAD_PER_TILE_H9                  = 0x00000097,
SC_QZ3_QUAD_PER_TILE_H10                 = 0x00000098,
SC_QZ3_QUAD_PER_TILE_H11                 = 0x00000099,
SC_QZ3_QUAD_PER_TILE_H12                 = 0x0000009a,
SC_QZ3_QUAD_PER_TILE_H13                 = 0x0000009b,
SC_QZ3_QUAD_PER_TILE_H14                 = 0x0000009c,
SC_QZ3_QUAD_PER_TILE_H15                 = 0x0000009d,
SC_QZ3_QUAD_PER_TILE_H16                 = 0x0000009e,
SC_QZ0_QUAD_COUNT                        = 0x0000009f,
SC_QZ1_QUAD_COUNT                        = 0x000000a0,
SC_QZ2_QUAD_COUNT                        = 0x000000a1,
SC_QZ3_QUAD_COUNT                        = 0x000000a2,
SC_P0_HIZ_TILE_COUNT                     = 0x000000a3,
SC_P1_HIZ_TILE_COUNT                     = 0x000000a4,
SC_P2_HIZ_TILE_COUNT                     = 0x000000a5,
SC_P3_HIZ_TILE_COUNT                     = 0x000000a6,
SC_P0_HIZ_QUAD_PER_TILE_H0               = 0x000000a7,
SC_P0_HIZ_QUAD_PER_TILE_H1               = 0x000000a8,
SC_P0_HIZ_QUAD_PER_TILE_H2               = 0x000000a9,
SC_P0_HIZ_QUAD_PER_TILE_H3               = 0x000000aa,
SC_P0_HIZ_QUAD_PER_TILE_H4               = 0x000000ab,
SC_P0_HIZ_QUAD_PER_TILE_H5               = 0x000000ac,
SC_P0_HIZ_QUAD_PER_TILE_H6               = 0x000000ad,
SC_P0_HIZ_QUAD_PER_TILE_H7               = 0x000000ae,
SC_P0_HIZ_QUAD_PER_TILE_H8               = 0x000000af,
SC_P0_HIZ_QUAD_PER_TILE_H9               = 0x000000b0,
SC_P0_HIZ_QUAD_PER_TILE_H10              = 0x000000b1,
SC_P0_HIZ_QUAD_PER_TILE_H11              = 0x000000b2,
SC_P0_HIZ_QUAD_PER_TILE_H12              = 0x000000b3,
SC_P0_HIZ_QUAD_PER_TILE_H13              = 0x000000b4,
SC_P0_HIZ_QUAD_PER_TILE_H14              = 0x000000b5,
SC_P0_HIZ_QUAD_PER_TILE_H15              = 0x000000b6,
SC_P0_HIZ_QUAD_PER_TILE_H16              = 0x000000b7,
SC_P1_HIZ_QUAD_PER_TILE_H0               = 0x000000b8,
SC_P1_HIZ_QUAD_PER_TILE_H1               = 0x000000b9,
SC_P1_HIZ_QUAD_PER_TILE_H2               = 0x000000ba,
SC_P1_HIZ_QUAD_PER_TILE_H3               = 0x000000bb,
SC_P1_HIZ_QUAD_PER_TILE_H4               = 0x000000bc,
SC_P1_HIZ_QUAD_PER_TILE_H5               = 0x000000bd,
SC_P1_HIZ_QUAD_PER_TILE_H6               = 0x000000be,
SC_P1_HIZ_QUAD_PER_TILE_H7               = 0x000000bf,
SC_P1_HIZ_QUAD_PER_TILE_H8               = 0x000000c0,
SC_P1_HIZ_QUAD_PER_TILE_H9               = 0x000000c1,
SC_P1_HIZ_QUAD_PER_TILE_H10              = 0x000000c2,
SC_P1_HIZ_QUAD_PER_TILE_H11              = 0x000000c3,
SC_P1_HIZ_QUAD_PER_TILE_H12              = 0x000000c4,
SC_P1_HIZ_QUAD_PER_TILE_H13              = 0x000000c5,
SC_P1_HIZ_QUAD_PER_TILE_H14              = 0x000000c6,
SC_P1_HIZ_QUAD_PER_TILE_H15              = 0x000000c7,
SC_P1_HIZ_QUAD_PER_TILE_H16              = 0x000000c8,
SC_P2_HIZ_QUAD_PER_TILE_H0               = 0x000000c9,
SC_P2_HIZ_QUAD_PER_TILE_H1               = 0x000000ca,
SC_P2_HIZ_QUAD_PER_TILE_H2               = 0x000000cb,
SC_P2_HIZ_QUAD_PER_TILE_H3               = 0x000000cc,
SC_P2_HIZ_QUAD_PER_TILE_H4               = 0x000000cd,
SC_P2_HIZ_QUAD_PER_TILE_H5               = 0x000000ce,
SC_P2_HIZ_QUAD_PER_TILE_H6               = 0x000000cf,
SC_P2_HIZ_QUAD_PER_TILE_H7               = 0x000000d0,
SC_P2_HIZ_QUAD_PER_TILE_H8               = 0x000000d1,
SC_P2_HIZ_QUAD_PER_TILE_H9               = 0x000000d2,
SC_P2_HIZ_QUAD_PER_TILE_H10              = 0x000000d3,
SC_P2_HIZ_QUAD_PER_TILE_H11              = 0x000000d4,
SC_P2_HIZ_QUAD_PER_TILE_H12              = 0x000000d5,
SC_P2_HIZ_QUAD_PER_TILE_H13              = 0x000000d6,
SC_P2_HIZ_QUAD_PER_TILE_H14              = 0x000000d7,
SC_P2_HIZ_QUAD_PER_TILE_H15              = 0x000000d8,
SC_P2_HIZ_QUAD_PER_TILE_H16              = 0x000000d9,
SC_P3_HIZ_QUAD_PER_TILE_H0               = 0x000000da,
SC_P3_HIZ_QUAD_PER_TILE_H1               = 0x000000db,
SC_P3_HIZ_QUAD_PER_TILE_H2               = 0x000000dc,
SC_P3_HIZ_QUAD_PER_TILE_H3               = 0x000000dd,
SC_P3_HIZ_QUAD_PER_TILE_H4               = 0x000000de,
SC_P3_HIZ_QUAD_PER_TILE_H5               = 0x000000df,
SC_P3_HIZ_QUAD_PER_TILE_H6               = 0x000000e0,
SC_P3_HIZ_QUAD_PER_TILE_H7               = 0x000000e1,
SC_P3_HIZ_QUAD_PER_TILE_H8               = 0x000000e2,
SC_P3_HIZ_QUAD_PER_TILE_H9               = 0x000000e3,
SC_P3_HIZ_QUAD_PER_TILE_H10              = 0x000000e4,
SC_P3_HIZ_QUAD_PER_TILE_H11              = 0x000000e5,
SC_P3_HIZ_QUAD_PER_TILE_H12              = 0x000000e6,
SC_P3_HIZ_QUAD_PER_TILE_H13              = 0x000000e7,
SC_P3_HIZ_QUAD_PER_TILE_H14              = 0x000000e8,
SC_P3_HIZ_QUAD_PER_TILE_H15              = 0x000000e9,
SC_P3_HIZ_QUAD_PER_TILE_H16              = 0x000000ea,
SC_P0_HIZ_QUAD_COUNT                     = 0x000000eb,
SC_P1_HIZ_QUAD_COUNT                     = 0x000000ec,
SC_P2_HIZ_QUAD_COUNT                     = 0x000000ed,
SC_P3_HIZ_QUAD_COUNT                     = 0x000000ee,
SC_P0_DETAIL_QUAD_COUNT                  = 0x000000ef,
SC_P1_DETAIL_QUAD_COUNT                  = 0x000000f0,
SC_P2_DETAIL_QUAD_COUNT                  = 0x000000f1,
SC_P3_DETAIL_QUAD_COUNT                  = 0x000000f2,
SC_P0_DETAIL_QUAD_WITH_1_PIX             = 0x000000f3,
SC_P0_DETAIL_QUAD_WITH_2_PIX             = 0x000000f4,
SC_P0_DETAIL_QUAD_WITH_3_PIX             = 0x000000f5,
SC_P0_DETAIL_QUAD_WITH_4_PIX             = 0x000000f6,
SC_P1_DETAIL_QUAD_WITH_1_PIX             = 0x000000f7,
SC_P1_DETAIL_QUAD_WITH_2_PIX             = 0x000000f8,
SC_P1_DETAIL_QUAD_WITH_3_PIX             = 0x000000f9,
SC_P1_DETAIL_QUAD_WITH_4_PIX             = 0x000000fa,
SC_P2_DETAIL_QUAD_WITH_1_PIX             = 0x000000fb,
SC_P2_DETAIL_QUAD_WITH_2_PIX             = 0x000000fc,
SC_P2_DETAIL_QUAD_WITH_3_PIX             = 0x000000fd,
SC_P2_DETAIL_QUAD_WITH_4_PIX             = 0x000000fe,
SC_P3_DETAIL_QUAD_WITH_1_PIX             = 0x000000ff,
SC_P3_DETAIL_QUAD_WITH_2_PIX             = 0x00000100,
SC_P3_DETAIL_QUAD_WITH_3_PIX             = 0x00000101,
SC_P3_DETAIL_QUAD_WITH_4_PIX             = 0x00000102,
SC_EARLYZ_QUAD_COUNT                     = 0x00000103,
SC_EARLYZ_QUAD_WITH_1_PIX                = 0x00000104,
SC_EARLYZ_QUAD_WITH_2_PIX                = 0x00000105,
SC_EARLYZ_QUAD_WITH_3_PIX                = 0x00000106,
SC_EARLYZ_QUAD_WITH_4_PIX                = 0x00000107,
SC_PKR_QUAD_PER_ROW_H1                   = 0x00000108,
SC_PKR_QUAD_PER_ROW_H2                   = 0x00000109,
SC_PKR_4X2_QUAD_SPLIT                    = 0x0000010a,
SC_PKR_4X2_FILL_QUAD                     = 0x0000010b,
SC_PKR_END_OF_VECTOR                     = 0x0000010c,
SC_PKR_CONTROL_XFER                      = 0x0000010d,
SC_PKR_DBHANG_FORCE_EOV                  = 0x0000010e,
SC_REG_SCLK_BUSY                         = 0x0000010f,
SC_GRP0_DYN_SCLK_BUSY                    = 0x00000110,
SC_GRP1_DYN_SCLK_BUSY                    = 0x00000111,
SC_GRP2_DYN_SCLK_BUSY                    = 0x00000112,
SC_GRP3_DYN_SCLK_BUSY                    = 0x00000113,
SC_GRP4_DYN_SCLK_BUSY                    = 0x00000114,
SC_PA0_SC_DATA_FIFO_RD                   = 0x00000115,
SC_PA0_SC_DATA_FIFO_WE                   = 0x00000116,
SC_PA1_SC_DATA_FIFO_RD                   = 0x00000117,
SC_PA1_SC_DATA_FIFO_WE                   = 0x00000118,
SC_PS_ARB_XFC_ALL_EVENT_OR_PRIM_CYCLES   = 0x00000119,
SC_PS_ARB_XFC_ONLY_PRIM_CYCLES           = 0x0000011a,
SC_PS_ARB_XFC_ONLY_ONE_INC_PER_PRIM      = 0x0000011b,
SC_PS_ARB_STALLED_FROM_BELOW             = 0x0000011c,
SC_PS_ARB_STARVED_FROM_ABOVE             = 0x0000011d,
SC_PS_ARB_SC_BUSY                        = 0x0000011e,
SC_PS_ARB_PA_SC_BUSY                     = 0x0000011f,
SC_PA2_SC_DATA_FIFO_RD                   = 0x00000120,
SC_PA2_SC_DATA_FIFO_WE                   = 0x00000121,
SC_PA3_SC_DATA_FIFO_RD                   = 0x00000122,
SC_PA3_SC_DATA_FIFO_WE                   = 0x00000123,
SC_PA_SC_DEALLOC_0_0_WE                  = 0x00000124,
SC_PA_SC_DEALLOC_0_1_WE                  = 0x00000125,
SC_PA_SC_DEALLOC_1_0_WE                  = 0x00000126,
SC_PA_SC_DEALLOC_1_1_WE                  = 0x00000127,
SC_PA_SC_DEALLOC_2_0_WE                  = 0x00000128,
SC_PA_SC_DEALLOC_2_1_WE                  = 0x00000129,
SC_PA_SC_DEALLOC_3_0_WE                  = 0x0000012a,
SC_PA_SC_DEALLOC_3_1_WE                  = 0x0000012b,
SC_PA0_SC_EOP_WE                         = 0x0000012c,
SC_PA0_SC_EOPG_WE                        = 0x0000012d,
SC_PA0_SC_EVENT_WE                       = 0x0000012e,
SC_PA1_SC_EOP_WE                         = 0x0000012f,
SC_PA1_SC_EOPG_WE                        = 0x00000130,
SC_PA1_SC_EVENT_WE                       = 0x00000131,
SC_PA2_SC_EOP_WE                         = 0x00000132,
SC_PA2_SC_EOPG_WE                        = 0x00000133,
SC_PA2_SC_EVENT_WE                       = 0x00000134,
SC_PA3_SC_EOP_WE                         = 0x00000135,
SC_PA3_SC_EOPG_WE                        = 0x00000136,
SC_PA3_SC_EVENT_WE                       = 0x00000137,
SC_PS_ARB_OOO_THRESHOLD_SWITCH_TO_DESIRED_FIFO = 0x00000138,
SC_PS_ARB_OOO_FIFO_EMPTY_SWITCH          = 0x00000139,
SC_PS_ARB_NULL_PRIM_BUBBLE_POP           = 0x0000013a,
SC_PS_ARB_EOP_POP_SYNC_POP               = 0x0000013b,
SC_PS_ARB_EVENT_SYNC_POP                 = 0x0000013c,
SC_PS_ENG_MULTICYCLE_BUBBLE              = 0x0000013d,
SC_PA0_SC_FPOV_WE                        = 0x0000013e,
SC_PA1_SC_FPOV_WE                        = 0x0000013f,
SC_PA2_SC_FPOV_WE                        = 0x00000140,
SC_PA3_SC_FPOV_WE                        = 0x00000141,
SC_PA0_SC_LPOV_WE                        = 0x00000142,
SC_PA1_SC_LPOV_WE                        = 0x00000143,
SC_PA2_SC_LPOV_WE                        = 0x00000144,
SC_PA3_SC_LPOV_WE                        = 0x00000145,
SC_SPI_DEALLOC_0_0                       = 0x00000146,
SC_SPI_DEALLOC_0_1                       = 0x00000147,
SC_SPI_DEALLOC_0_2                       = 0x00000148,
SC_SPI_DEALLOC_1_0                       = 0x00000149,
SC_SPI_DEALLOC_1_1                       = 0x0000014a,
SC_SPI_DEALLOC_1_2                       = 0x0000014b,
SC_SPI_DEALLOC_2_0                       = 0x0000014c,
SC_SPI_DEALLOC_2_1                       = 0x0000014d,
SC_SPI_DEALLOC_2_2                       = 0x0000014e,
SC_SPI_DEALLOC_3_0                       = 0x0000014f,
SC_SPI_DEALLOC_3_1                       = 0x00000150,
SC_SPI_DEALLOC_3_2                       = 0x00000151,
SC_SPI_FPOV_0                            = 0x00000152,
SC_SPI_FPOV_1                            = 0x00000153,
SC_SPI_FPOV_2                            = 0x00000154,
SC_SPI_FPOV_3                            = 0x00000155,
SC_SPI_EVENT                             = 0x00000156,
SC_PS_TS_EVENT_FIFO_PUSH                 = 0x00000157,
SC_PS_TS_EVENT_FIFO_POP                  = 0x00000158,
SC_PS_CTX_DONE_FIFO_PUSH                 = 0x00000159,
SC_PS_CTX_DONE_FIFO_POP                  = 0x0000015a,
SC_MULTICYCLE_BUBBLE_FREEZE              = 0x0000015b,
SC_EOP_SYNC_WINDOW                       = 0x0000015c,
SC_PA0_SC_NULL_WE                        = 0x0000015d,
SC_PA0_SC_NULL_DEALLOC_WE                = 0x0000015e,
SC_PA0_SC_DATA_FIFO_EOPG_RD              = 0x0000015f,
SC_PA0_SC_DATA_FIFO_EOP_RD               = 0x00000160,
SC_PA0_SC_DEALLOC_0_RD                   = 0x00000161,
SC_PA0_SC_DEALLOC_1_RD                   = 0x00000162,
SC_PA1_SC_DATA_FIFO_EOPG_RD              = 0x00000163,
SC_PA1_SC_DATA_FIFO_EOP_RD               = 0x00000164,
SC_PA1_SC_DEALLOC_0_RD                   = 0x00000165,
SC_PA1_SC_DEALLOC_1_RD                   = 0x00000166,
SC_PA1_SC_NULL_WE                        = 0x00000167,
SC_PA1_SC_NULL_DEALLOC_WE                = 0x00000168,
SC_PA2_SC_DATA_FIFO_EOPG_RD              = 0x00000169,
SC_PA2_SC_DATA_FIFO_EOP_RD               = 0x0000016a,
SC_PA2_SC_DEALLOC_0_RD                   = 0x0000016b,
SC_PA2_SC_DEALLOC_1_RD                   = 0x0000016c,
SC_PA2_SC_NULL_WE                        = 0x0000016d,
SC_PA2_SC_NULL_DEALLOC_WE                = 0x0000016e,
SC_PA3_SC_DATA_FIFO_EOPG_RD              = 0x0000016f,
SC_PA3_SC_DATA_FIFO_EOP_RD               = 0x00000170,
SC_PA3_SC_DEALLOC_0_RD                   = 0x00000171,
SC_PA3_SC_DEALLOC_1_RD                   = 0x00000172,
SC_PA3_SC_NULL_WE                        = 0x00000173,
SC_PA3_SC_NULL_DEALLOC_WE                = 0x00000174,
SC_PS_PA0_SC_FIFO_EMPTY                  = 0x00000175,
SC_PS_PA0_SC_FIFO_FULL                   = 0x00000176,
SC_RESERVED_0                            = 0x00000177,
SC_PS_PA1_SC_FIFO_EMPTY                  = 0x00000178,
SC_PS_PA1_SC_FIFO_FULL                   = 0x00000179,
SC_RESERVED_1                            = 0x0000017a,
SC_PS_PA2_SC_FIFO_EMPTY                  = 0x0000017b,
SC_PS_PA2_SC_FIFO_FULL                   = 0x0000017c,
SC_RESERVED_2                            = 0x0000017d,
SC_PS_PA3_SC_FIFO_EMPTY                  = 0x0000017e,
SC_PS_PA3_SC_FIFO_FULL                   = 0x0000017f,
SC_RESERVED_3                            = 0x00000180,
SC_BUSY_PROCESSING_MULTICYCLE_PRIM       = 0x00000181,
SC_BUSY_CNT_NOT_ZERO                     = 0x00000182,
SC_BM_BUSY                               = 0x00000183,
SC_BACKEND_BUSY                          = 0x00000184,
SC_SCF_SCB_INTERFACE_BUSY                = 0x00000185,
SC_SCB_BUSY                              = 0x00000186,
SC_STARVED_BY_PA_WITH_UNSELECTED_PA_NOT_EMPTY = 0x00000187,
SC_STARVED_BY_PA_WITH_UNSELECTED_PA_FULL = 0x00000188,
SC_PBB_BIN_HIST_NUM_PRIMS                = 0x00000189,
SC_PBB_BATCH_HIST_NUM_PRIMS              = 0x0000018a,
SC_PBB_BIN_HIST_NUM_CONTEXTS             = 0x0000018b,
SC_PBB_BATCH_HIST_NUM_CONTEXTS           = 0x0000018c,
SC_PBB_BIN_HIST_NUM_PERSISTENT_STATES    = 0x0000018d,
SC_PBB_BATCH_HIST_NUM_PERSISTENT_STATES  = 0x0000018e,
SC_PBB_BATCH_HIST_NUM_PS_WAVE_BREAKS     = 0x0000018f,
SC_PBB_BATCH_HIST_NUM_TRIV_REJECTED_PRIMS = 0x00000190,
SC_PBB_BATCH_HIST_NUM_ROWS_PER_PRIM      = 0x00000191,
SC_PBB_BATCH_HIST_NUM_COLUMNS_PER_ROW    = 0x00000192,
SC_PBB_BUSY                              = 0x00000193,
SC_PBB_BUSY_AND_NO_SENDS                 = 0x00000194,
SC_PBB_STALLS_PA_DUE_TO_NO_TILES         = 0x00000195,
SC_PBB_NUM_BINS                          = 0x00000196,
SC_PBB_END_OF_BIN                        = 0x00000197,
SC_PBB_END_OF_BATCH                      = 0x00000198,
SC_PBB_PRIMBIN_PROCESSED                 = 0x00000199,
SC_PBB_PRIM_ADDED_TO_BATCH               = 0x0000019a,
SC_PBB_NONBINNED_PRIM                    = 0x0000019b,
SC_PBB_TOTAL_REAL_PRIMS_OUT_OF_PBB       = 0x0000019c,
SC_PBB_TOTAL_NULL_PRIMS_OUT_OF_PBB       = 0x0000019d,
SC_PBB_IDLE_CLK_DUE_TO_ROW_TO_COLUMN_TRANSITION = 0x0000019e,
SC_PBB_IDLE_CLK_DUE_TO_FALSE_POSITIVE_ON_ROW = 0x0000019f,
SC_PBB_IDLE_CLK_DUE_TO_FALSE_POSITIVE_ON_COLUMN = 0x000001a0,
SC_PBB_BATCH_BREAK_DUE_TO_PERSISTENT_STATE = 0x000001a1,
SC_PBB_BATCH_BREAK_DUE_TO_CONTEXT_STATE  = 0x000001a2,
SC_PBB_BATCH_BREAK_DUE_TO_PRIM           = 0x000001a3,
SC_PBB_BATCH_BREAK_DUE_TO_PC_STORAGE     = 0x000001a4,
SC_PBB_BATCH_BREAK_DUE_TO_EVENT          = 0x000001a5,
SC_PBB_BATCH_BREAK_DUE_TO_FPOV_LIMIT     = 0x000001a6,
SC_POPS_INTRA_WAVE_OVERLAPS              = 0x000001a7,
SC_POPS_FORCE_EOV                        = 0x000001a8,
SC_PKR_QUAD_OVLP_NOT_FOUND_IN_WAVE_TABLE_AND_WAVES_SINCE_OVLP_SET_TO_MAX = 0x000001a9,
SC_PKR_QUAD_OVLP_NOT_FOUND_IN_WAVE_TABLE_AND_NO_CHANGE_TO_WAVES_SINCE_OVLP = 0x000001aa,
SC_PKR_QUAD_OVLP_FOUND_IN_WAVE_TABLE     = 0x000001ab,
SC_FULL_FULL_QUAD                        = 0x000001ac,
SC_FULL_HALF_QUAD                        = 0x000001ad,
SC_FULL_QTR_QUAD                         = 0x000001ae,
SC_HALF_FULL_QUAD                        = 0x000001af,
SC_HALF_HALF_QUAD                        = 0x000001b0,
SC_HALF_QTR_QUAD                         = 0x000001b1,
SC_QTR_FULL_QUAD                         = 0x000001b2,
SC_QTR_HALF_QUAD                         = 0x000001b3,
SC_QTR_QTR_QUAD                          = 0x000001b4,
SC_GRP5_DYN_SCLK_BUSY                    = 0x000001b5,
SC_GRP6_DYN_SCLK_BUSY                    = 0x000001b6,
SC_GRP7_DYN_SCLK_BUSY                    = 0x000001b7,
SC_GRP8_DYN_SCLK_BUSY                    = 0x000001b8,
SC_GRP9_DYN_SCLK_BUSY                    = 0x000001b9,
SC_PS_TO_BE_SCLK_GATE_STALL              = 0x000001ba,
SC_PA_TO_PBB_SCLK_GATE_STALL_STALL       = 0x000001bb,
SC_PK_BUSY                               = 0x000001bc,
SC_PK_MAX_DEALLOC_FORCE_EOV              = 0x000001bd,
SC_PK_DEALLOC_WAVE_BREAK                 = 0x000001be,
SC_SPI_SEND                              = 0x000001bf,
SC_SPI_CREDIT_AT_ZERO_WITH_PENDING_SEND  = 0x000001c0,
SC_SPI_CREDIT_AT_MAX                     = 0x000001c1,
SC_SPI_CREDIT_AT_MAX_NO_PENDING_SEND     = 0x000001c2,
SC_BCI_SEND                              = 0x000001c3,
SC_BCI_CREDIT_AT_ZERO_WITH_PENDING_SEND  = 0x000001c4,
SC_BCI_CREDIT_AT_MAX                     = 0x000001c5,
SC_BCI_CREDIT_AT_MAX_NO_PENDING_SEND     = 0x000001c6,
SC_SPIBC_FULL_FREEZE                     = 0x000001c7,
SC_PW_BM_PASS_EMPTY_PRIM                 = 0x000001c8,
SC_SUPERTILE_COUNT_EXCLUDE_PASS_EMPTY_PRIM = 0x000001c9,
SC_SUPERTILE_PER_PRIM_EXCLUDE_PASS_EMPTY_PRIM_H0 = 0x000001ca,
SC_SUPERTILE_PER_PRIM_EXCLUDE_PASS_EMPTY_PRIM_H1 = 0x000001cb,
SC_SUPERTILE_PER_PRIM_EXCLUDE_PASS_EMPTY_PRIM_H2 = 0x000001cc,
SC_SUPERTILE_PER_PRIM_EXCLUDE_PASS_EMPTY_PRIM_H3 = 0x000001cd,
SC_SUPERTILE_PER_PRIM_EXCLUDE_PASS_EMPTY_PRIM_H4 = 0x000001ce,
SC_SUPERTILE_PER_PRIM_EXCLUDE_PASS_EMPTY_PRIM_H5 = 0x000001cf,
SC_SUPERTILE_PER_PRIM_EXCLUDE_PASS_EMPTY_PRIM_H6 = 0x000001d0,
SC_SUPERTILE_PER_PRIM_EXCLUDE_PASS_EMPTY_PRIM_H7 = 0x000001d1,
SC_SUPERTILE_PER_PRIM_EXCLUDE_PASS_EMPTY_PRIM_H8 = 0x000001d2,
SC_SUPERTILE_PER_PRIM_EXCLUDE_PASS_EMPTY_PRIM_H9 = 0x000001d3,
SC_SUPERTILE_PER_PRIM_EXCLUDE_PASS_EMPTY_PRIM_H10 = 0x000001d4,
SC_SUPERTILE_PER_PRIM_EXCLUDE_PASS_EMPTY_PRIM_H11 = 0x000001d5,
SC_SUPERTILE_PER_PRIM_EXCLUDE_PASS_EMPTY_PRIM_H12 = 0x000001d6,
SC_SUPERTILE_PER_PRIM_EXCLUDE_PASS_EMPTY_PRIM_H13 = 0x000001d7,
SC_SUPERTILE_PER_PRIM_EXCLUDE_PASS_EMPTY_PRIM_H14 = 0x000001d8,
SC_SUPERTILE_PER_PRIM_EXCLUDE_PASS_EMPTY_PRIM_H15 = 0x000001d9,
SC_SUPERTILE_PER_PRIM_EXCLUDE_PASS_EMPTY_PRIM_H16 = 0x000001da,
SC_DB0_TILE_INTERFACE_BUSY               = 0x000001db,
SC_DB0_TILE_INTERFACE_SEND               = 0x000001dc,
SC_DB0_TILE_INTERFACE_SEND_EVENT         = 0x000001dd,
SC_DB0_TILE_INTERFACE_SEND_SOP_ONLY_EVENT = 0x000001de,
SC_DB0_TILE_INTERFACE_SEND_SOP           = 0x000001df,
SC_DB0_TILE_INTERFACE_CREDIT_AT_ZERO_WITH_PENDING_SEND = 0x000001e0,
SC_DB0_TILE_INTERFACE_CREDIT_AT_MAX      = 0x000001e1,
SC_DB0_TILE_INTERFACE_CREDIT_AT_MAX_WITH_NO_PENDING_SEND = 0x000001e2,
SC_DB1_TILE_INTERFACE_BUSY               = 0x000001e3,
SC_DB1_TILE_INTERFACE_SEND               = 0x000001e4,
SC_DB1_TILE_INTERFACE_SEND_EVENT         = 0x000001e5,
SC_DB1_TILE_INTERFACE_SEND_SOP_ONLY_EVENT = 0x000001e6,
SC_DB1_TILE_INTERFACE_SEND_SOP           = 0x000001e7,
SC_DB1_TILE_INTERFACE_CREDIT_AT_ZERO_WITH_PENDING_SEND = 0x000001e8,
SC_DB1_TILE_INTERFACE_CREDIT_AT_MAX      = 0x000001e9,
SC_DB1_TILE_INTERFACE_CREDIT_AT_MAX_WITH_NO_PENDING_SEND = 0x000001ea,
SC_BACKEND_PRIM_FIFO_FULL                = 0x000001eb,
SC_PBB_BATCH_BREAK_DUE_TO_TIMEOUT_COUNTER = 0x000001ec,
SC_PBB_BATCH_BREAK_DUE_TO_NONBINNED_BATCH = 0x000001ed,
SC_PBB_BATCH_BREAK_DUE_TO_DEBUG_DATA_PER_DRAW_DISPATCH = 0x000001ee,
SC_PBB_BATCH_BREAK_DUE_TO_OVERRIDE_REGISTER_PERSISTENT = 0x000001ef,
SC_PBB_BATCH_BREAK_DUE_TO_OVERRIDE_REGISTER_CONTEXT = 0x000001f0,
SC_PBB_BATCH_BREAK_DUE_TO_OVERRIDE_REGISTER_FPOV = 0x000001f1,
SC_PBB_BATCH_BREAK_DUE_TO_NEW_SC_MODE    = 0x000001f2,
SC_PBB_BATCH_BREAK_DUE_TO_BINNING_MODE_CHANGE = 0x000001f3,
SC_PBB_BATCH_BREAK_DUE_TO_PIPELINE_EVENT_COUNT = 0x000001f4,
SC_PBB_BATCH_BREAK_DUE_TO_PIPE_RESET     = 0x000001f5,
SC_PBB_BATCH_BREAK_DUE_TO_GFX_PIPE_CHANGE = 0x000001f6,
SC_STALLED_BY_DB0_TILEFIFO               = 0x000001f7,
SC_DB0_QUAD_INTF_SEND                    = 0x000001f8,
SC_DB0_QUAD_INTF_BUSY                    = 0x000001f9,
SC_DB0_QUAD_INTF_STALLED_BY_DB           = 0x000001fa,
SC_DB0_QUAD_INTF_CREDIT_AT_MAX           = 0x000001fb,
SC_DB0_QUAD_INTF_IDLE                    = 0x000001fc,
SC_DB1_QUAD_INTF_SEND                    = 0x000001fd,
SC_STALLED_BY_DB1_TILEFIFO               = 0x000001fe,
SC_DB1_QUAD_INTF_BUSY                    = 0x000001ff,
SC_DB1_QUAD_INTF_STALLED_BY_DB           = 0x00000200,
SC_DB1_QUAD_INTF_CREDIT_AT_MAX           = 0x00000201,
SC_DB1_QUAD_INTF_IDLE                    = 0x00000202,
SC_PKR_WAVE_BREAK_OUTSIDE_REGION         = 0x00000203,
SC_PKR_WAVE_BREAK_FULL_TILE              = 0x00000204,
SC_FSR_WALKED                            = 0x00000205,
SC_PBB_EMPTY_INPUT_CYCLE_WHEN_BATCH_OPEN = 0x00000206,
SC_PBB_BATCH_BREAK_DUE_TO_NULL_PRIM_BREAK_BATCH_LIMIT = 0x00000207,
SC_DB0_WE_STALLED_BY_RSLT_FIFO_FULL      = 0x00000208,
SC_DB0_WE_TILE_MASK_RETURN_FIFO_FULL_WITH_WE_RSLT_FIFO_STALL = 0x00000209,
SC_DB0_TILE_MASK_FIFO_FULL               = 0x0000020a,
SC_DB1_WE_STALLED_BY_RSLT_FIFO_FULL      = 0x0000020b,
SC_DB1_WE_TILE_MASK_RETURN_FIFO_FULL_WITH_WE_RSLT_FIFO_STALL = 0x0000020c,
SC_DB1_TILE_MASK_FIFO_FULL               = 0x0000020d,
SC_PS_PM_PBB_TO_PSE_FIFO_WE_STALL_BY_PFF_PW_FULL = 0x0000020e,
SC_PS_PM_PBB_TO_PSE_FIFO_WE_STALL_BY_ZFF_PW_FULL = 0x0000020f,
SC_PS_PM_PBB_TO_PSE_FIFO_WE_STALL_BY_PBB_TO_PSE_FIFO_FULL = 0x00000210,
SC_PS_PM_PFF_PW_FULL                     = 0x00000211,
SC_PS_PM_ZFF_PW_FULL                     = 0x00000212,
SC_PS_PM_PBB_TO_PSE_FIFO_FULL            = 0x00000213,
SC_PK_PM_QD1_FD_CONFLICT_WAVE_BRK_1H     = 0x00000214,
SC_PK_PM_QD1_FORCE_PARTIAL_FOR_DEALLOC_WAVE_BRK_1H = 0x00000215,
SC_PK_PM_QD1_AVOID_DEALLOC_ADD_WAVE_BRK_1H = 0x00000216,
SC_PK_PM_4X2_SPLIT_WAVE_BRK_1H           = 0x00000217,
SC_PK_PM_PKR_FILL_4X2_WAVE_BRK_1H        = 0x00000218,
SC_PK_PM_SPLIT_OR_FILL_4X2_WAVE_BRK_1H   = 0x00000219,
SC_PK_PM_END_OF_VECTOR_WAVE_BRK_1H       = 0x0000021a,
SC_PK_PM_LAST_AND_DEALLOC_WAVE_BRK_1H    = 0x0000021b,
SC_PK_PM_CTL_ONLY_CMD_WAVE_BRK_1H        = 0x0000021c,
SC_PK_PM_AVOID_DEALLOC_ADD_WAVE_BRK_1H   = 0x0000021d,
SC_PK_PM_FD_CONFLICT_WAVE_BRK_1H         = 0x0000021e,
SC_PK_PM_FORCE_PARTIAL_FOR_DEALLOC_WAVE_BRK_1H = 0x0000021f,
SC_PK_PM_AE_CONFLICT_WAVE_BRK_1H         = 0x00000220,
SC_PK_PM_EOP_OR_LAD_WAVE_BRK_1H          = 0x00000221,
SC_PK_PM_FULL_TILE_WAVE_BRK_1H           = 0x00000222,
SC_PK_PM_POPS_FORCE_EOV_WAVE_BRK_1H      = 0x00000223,
SC_PK_PM_MAX_DEALLOC_FORCE_EOV_WAVE_BRK_1H = 0x00000224,
SC_PK_PM_WAVE_BREAK_OUTSIDE_REGION_WAVE_BRK_1H = 0x00000225,
SC_PK_PM_MAX_CLK_CNT_FORCE_EOV_WAVE_BRK_1H = 0x00000226,
SC_PK_PM_MAX_REZ_CNT_FORCE_EOV_WAVE_BRK_1H = 0x00000227,
SC_PK_PM_VRS_RATE_X_00_Y_00_QUAD         = 0x00000228,
SC_PK_PM_VRS_RATE_X_00_Y_01_QUAD         = 0x00000229,
SC_PK_PM_VRS_RATE_X_00_Y_10_QUAD         = 0x0000022a,
SC_PK_PM_VRS_RATE_X_00_Y_11_QUAD         = 0x0000022b,
SC_PK_PM_VRS_RATE_X_01_Y_00_QUAD         = 0x0000022c,
SC_PK_PM_VRS_RATE_X_01_Y_01_QUAD         = 0x0000022d,
SC_PK_PM_VRS_RATE_X_01_Y_10_QUAD         = 0x0000022e,
SC_PK_PM_VRS_RATE_X_01_Y_11_QUAD         = 0x0000022f,
SC_PK_PM_VRS_RATE_X_10_Y_00_QUAD         = 0x00000230,
SC_PK_PM_VRS_RATE_X_10_Y_01_QUAD         = 0x00000231,
SC_PK_PM_VRS_RATE_X_10_Y_10_QUAD         = 0x00000232,
SC_PK_PM_VRS_RATE_X_10_Y_11_QUAD         = 0x00000233,
SC_PK_PM_VRS_RATE_X_11_Y_00_QUAD         = 0x00000234,
SC_PK_PM_VRS_RATE_X_11_Y_01_QUAD         = 0x00000235,
SC_PK_PM_VRS_RATE_X_11_Y_10_QUAD         = 0x00000236,
SC_PK_PM_VRS_RATE_X_11_Y_11_QUAD         = 0x00000237,
SC_PBB_BATCH_BREAK_DUE_TO_PIPELINE_MODE_CHANGE = 0x00000238,
SC_PBB_RESERVED                          = 0x00000239,
SC_BM_BE0_STALLED                        = 0x0000023a,
SC_BM_BE1_STALLED                        = 0x0000023b,
SC_BM_BE2_STALLED                        = 0x0000023c,
SC_BM_BE3_STALLED                        = 0x0000023d,
SC_BM_MULTI_ACCUM_1_BE_STALLED           = 0x0000023e,
SC_BM_MULTI_ACCUM_2_BE_STALLED           = 0x0000023f,
SC_BM_MULTI_ACCUM_3_BE_STALLED           = 0x00000240,
SC_BM_MULTI_ACCUM_4_BE_STALLED           = 0x00000241,
} SC_PERFCNT_SEL;

/*
 * ScMap enum
 */

typedef enum ScMap {
RASTER_CONFIG_SC_MAP_0                   = 0x00000000,
RASTER_CONFIG_SC_MAP_1                   = 0x00000001,
RASTER_CONFIG_SC_MAP_2                   = 0x00000002,
RASTER_CONFIG_SC_MAP_3                   = 0x00000003,
} ScMap;

/*
 * ScUncertaintyRegionMode enum
 */

typedef enum ScUncertaintyRegionMode {
SC_HALF_LSB                              = 0x00000000,
SC_LSB_ONE_SIDED                         = 0x00000001,
SC_LSB_TWO_SIDED                         = 0x00000002,
} ScUncertaintyRegionMode;

/*
 * ScUncertaintyRegionMult enum
 */

typedef enum ScUncertaintyRegionMult {
SC_UR_1X                                 = 0x00000000,
SC_UR_2X                                 = 0x00000001,
SC_UR_4X                                 = 0x00000002,
SC_UR_8X                                 = 0x00000003,
} ScUncertaintyRegionMult;

/*
 * ScXsel enum
 */

typedef enum ScXsel {
RASTER_CONFIG_SC_XSEL_8_WIDE_TILE        = 0x00000000,
RASTER_CONFIG_SC_XSEL_16_WIDE_TILE       = 0x00000001,
RASTER_CONFIG_SC_XSEL_32_WIDE_TILE       = 0x00000002,
RASTER_CONFIG_SC_XSEL_64_WIDE_TILE       = 0x00000003,
} ScXsel;

/*
 * ScYsel enum
 */

typedef enum ScYsel {
RASTER_CONFIG_SC_YSEL_8_WIDE_TILE        = 0x00000000,
RASTER_CONFIG_SC_YSEL_16_WIDE_TILE       = 0x00000001,
RASTER_CONFIG_SC_YSEL_32_WIDE_TILE       = 0x00000002,
RASTER_CONFIG_SC_YSEL_64_WIDE_TILE       = 0x00000003,
} ScYsel;

/*
 * SeMap enum
 */

typedef enum SeMap {
RASTER_CONFIG_SE_MAP_0                   = 0x00000000,
RASTER_CONFIG_SE_MAP_1                   = 0x00000001,
RASTER_CONFIG_SE_MAP_2                   = 0x00000002,
RASTER_CONFIG_SE_MAP_3                   = 0x00000003,
} SeMap;

/*
 * SePairMap enum
 */

typedef enum SePairMap {
RASTER_CONFIG_SE_PAIR_MAP_0              = 0x00000000,
RASTER_CONFIG_SE_PAIR_MAP_1              = 0x00000001,
RASTER_CONFIG_SE_PAIR_MAP_2              = 0x00000002,
RASTER_CONFIG_SE_PAIR_MAP_3              = 0x00000003,
} SePairMap;

/*
 * SePairXsel enum
 */

typedef enum SePairXsel {
RASTER_CONFIG_SE_PAIR_XSEL_8_WIDE_TILE   = 0x00000000,
RASTER_CONFIG_SE_PAIR_XSEL_16_WIDE_TILE  = 0x00000001,
RASTER_CONFIG_SE_PAIR_XSEL_32_WIDE_TILE  = 0x00000002,
RASTER_CONFIG_SE_PAIR_XSEL_64_WIDE_TILE  = 0x00000003,
} SePairXsel;

/*
 * SePairYsel enum
 */

typedef enum SePairYsel {
RASTER_CONFIG_SE_PAIR_YSEL_8_WIDE_TILE   = 0x00000000,
RASTER_CONFIG_SE_PAIR_YSEL_16_WIDE_TILE  = 0x00000001,
RASTER_CONFIG_SE_PAIR_YSEL_32_WIDE_TILE  = 0x00000002,
RASTER_CONFIG_SE_PAIR_YSEL_64_WIDE_TILE  = 0x00000003,
} SePairYsel;

/*
 * SeXsel enum
 */

typedef enum SeXsel {
RASTER_CONFIG_SE_XSEL_8_WIDE_TILE        = 0x00000000,
RASTER_CONFIG_SE_XSEL_16_WIDE_TILE       = 0x00000001,
RASTER_CONFIG_SE_XSEL_32_WIDE_TILE       = 0x00000002,
RASTER_CONFIG_SE_XSEL_64_WIDE_TILE       = 0x00000003,
} SeXsel;

/*
 * SeYsel enum
 */

typedef enum SeYsel {
RASTER_CONFIG_SE_YSEL_8_WIDE_TILE        = 0x00000000,
RASTER_CONFIG_SE_YSEL_16_WIDE_TILE       = 0x00000001,
RASTER_CONFIG_SE_YSEL_32_WIDE_TILE       = 0x00000002,
RASTER_CONFIG_SE_YSEL_64_WIDE_TILE       = 0x00000003,
} SeYsel;

/*
 * VRSCombinerModeSC enum
 */

typedef enum VRSCombinerModeSC {
SC_VRS_COMB_MODE_PASSTHRU                = 0x00000000,
SC_VRS_COMB_MODE_OVERRIDE                = 0x00000001,
SC_VRS_COMB_MODE_MIN                     = 0x00000002,
SC_VRS_COMB_MODE_MAX                     = 0x00000003,
SC_VRS_COMB_MODE_SATURATE                = 0x00000004,
} VRSCombinerModeSC;

/*
 * VRSrate enum
 */

typedef enum VRSrate {
VRS_SHADING_RATE_1X1                     = 0x00000000,
VRS_SHADING_RATE_1X2                     = 0x00000001,
VRS_SHADING_RATE_UNDEFINED0              = 0x00000002,
VRS_SHADING_RATE_UNDEFINED1              = 0x00000003,
VRS_SHADING_RATE_2X1                     = 0x00000004,
VRS_SHADING_RATE_2X2                     = 0x00000005,
VRS_SHADING_RATE_2X4                     = 0x00000006,
VRS_SHADING_RATE_UNDEFINED2              = 0x00000007,
VRS_SHADING_RATE_UNDEFINED3              = 0x00000008,
VRS_SHADING_RATE_4X2                     = 0x00000009,
VRS_SHADING_RATE_4X4                     = 0x0000000a,
VRS_SHADING_RATE_UNDEFINED4              = 0x0000000b,
VRS_SHADING_RATE_16X_SSAA                = 0x0000000c,
VRS_SHADING_RATE_8X_SSAA                 = 0x0000000d,
VRS_SHADING_RATE_4X_SSAA                 = 0x0000000e,
VRS_SHADING_RATE_2X_SSAA                 = 0x0000000f,
} VRSrate;

/*******************************************************
 * TC Enums
 *******************************************************/

/*
 * TC_EA_CID enum
 */

typedef enum TC_EA_CID {
TC_EA_CID_RT                             = 0x00000000,
TC_EA_CID_FMASK                          = 0x00000001,
TC_EA_CID_DCC                            = 0x00000002,
TC_EA_CID_TCPMETA                        = 0x00000003,
TC_EA_CID_Z                              = 0x00000004,
TC_EA_CID_STENCIL                        = 0x00000005,
TC_EA_CID_HTILE                          = 0x00000006,
TC_EA_CID_MISC                           = 0x00000007,
TC_EA_CID_TCP                            = 0x00000008,
TC_EA_CID_SQC                            = 0x00000009,
TC_EA_CID_CPF                            = 0x0000000a,
TC_EA_CID_CPG                            = 0x0000000b,
TC_EA_CID_IA                             = 0x0000000c,
TC_EA_CID_WD                             = 0x0000000d,
TC_EA_CID_PA                             = 0x0000000e,
TC_EA_CID_UTCL2_TPI                      = 0x0000000f,
} TC_EA_CID;

/*
 * TC_NACKS enum
 */

typedef enum TC_NACKS {
TC_NACK_NO_FAULT                         = 0x00000000,
TC_NACK_PAGE_FAULT                       = 0x00000001,
TC_NACK_PROTECTION_FAULT                 = 0x00000002,
TC_NACK_DATA_ERROR                       = 0x00000003,
} TC_NACKS;

/*
 * TC_OP enum
 */

typedef enum TC_OP {
TC_OP_READ                               = 0x00000000,
TC_OP_ATOMIC_FCMPSWAP_RTN_32             = 0x00000001,
TC_OP_ATOMIC_FMIN_RTN_32                 = 0x00000002,
TC_OP_ATOMIC_FMAX_RTN_32                 = 0x00000003,
TC_OP_RESERVED_FOP_RTN_32_0              = 0x00000004,
TC_OP_RESERVED_FADD_RTN_32               = 0x00000005,
TC_OP_RESERVED_FOP_RTN_32_2              = 0x00000006,
TC_OP_ATOMIC_SWAP_RTN_32                 = 0x00000007,
TC_OP_ATOMIC_CMPSWAP_RTN_32              = 0x00000008,
TC_OP_ATOMIC_FCMPSWAP_FLUSH_DENORM_RTN_32 = 0x00000009,
TC_OP_ATOMIC_FMIN_FLUSH_DENORM_RTN_32    = 0x0000000a,
TC_OP_ATOMIC_FMAX_FLUSH_DENORM_RTN_32    = 0x0000000b,
TC_OP_PROBE_FILTER                       = 0x0000000c,
TC_OP_ATOMIC_FADD_FLUSH_DENORM_RTN_32    = 0x0000000d,
TC_OP_RESERVED_FOP_FLUSH_DENORM_RTN_32_2 = 0x0000000e,
TC_OP_ATOMIC_ADD_RTN_32                  = 0x0000000f,
TC_OP_ATOMIC_SUB_RTN_32                  = 0x00000010,
TC_OP_ATOMIC_SMIN_RTN_32                 = 0x00000011,
TC_OP_ATOMIC_UMIN_RTN_32                 = 0x00000012,
TC_OP_ATOMIC_SMAX_RTN_32                 = 0x00000013,
TC_OP_ATOMIC_UMAX_RTN_32                 = 0x00000014,
TC_OP_ATOMIC_AND_RTN_32                  = 0x00000015,
TC_OP_ATOMIC_OR_RTN_32                   = 0x00000016,
TC_OP_ATOMIC_XOR_RTN_32                  = 0x00000017,
TC_OP_ATOMIC_INC_RTN_32                  = 0x00000018,
TC_OP_ATOMIC_DEC_RTN_32                  = 0x00000019,
TC_OP_WBINVL1_VOL                        = 0x0000001a,
TC_OP_WBINVL1_SD                         = 0x0000001b,
TC_OP_RESERVED_NON_FLOAT_RTN_32_0        = 0x0000001c,
TC_OP_RESERVED_NON_FLOAT_RTN_32_1        = 0x0000001d,
TC_OP_RESERVED_NON_FLOAT_RTN_32_2        = 0x0000001e,
TC_OP_RESERVED_NON_FLOAT_RTN_32_3        = 0x0000001f,
TC_OP_WRITE                              = 0x00000020,
TC_OP_ATOMIC_FCMPSWAP_RTN_64             = 0x00000021,
TC_OP_ATOMIC_FMIN_RTN_64                 = 0x00000022,
TC_OP_ATOMIC_FMAX_RTN_64                 = 0x00000023,
TC_OP_RESERVED_FOP_RTN_64_0              = 0x00000024,
TC_OP_RESERVED_FOP_RTN_64_1              = 0x00000025,
TC_OP_RESERVED_FOP_RTN_64_2              = 0x00000026,
TC_OP_ATOMIC_SWAP_RTN_64                 = 0x00000027,
TC_OP_ATOMIC_CMPSWAP_RTN_64              = 0x00000028,
TC_OP_ATOMIC_FCMPSWAP_FLUSH_DENORM_RTN_64 = 0x00000029,
TC_OP_ATOMIC_FMIN_FLUSH_DENORM_RTN_64    = 0x0000002a,
TC_OP_ATOMIC_FMAX_FLUSH_DENORM_RTN_64    = 0x0000002b,
TC_OP_WBINVL2_SD                         = 0x0000002c,
TC_OP_RESERVED_FOP_FLUSH_DENORM_RTN_64_0 = 0x0000002d,
TC_OP_RESERVED_FOP_FLUSH_DENORM_RTN_64_1 = 0x0000002e,
TC_OP_ATOMIC_ADD_RTN_64                  = 0x0000002f,
TC_OP_ATOMIC_SUB_RTN_64                  = 0x00000030,
TC_OP_ATOMIC_SMIN_RTN_64                 = 0x00000031,
TC_OP_ATOMIC_UMIN_RTN_64                 = 0x00000032,
TC_OP_ATOMIC_SMAX_RTN_64                 = 0x00000033,
TC_OP_ATOMIC_UMAX_RTN_64                 = 0x00000034,
TC_OP_ATOMIC_AND_RTN_64                  = 0x00000035,
TC_OP_ATOMIC_OR_RTN_64                   = 0x00000036,
TC_OP_ATOMIC_XOR_RTN_64                  = 0x00000037,
TC_OP_ATOMIC_INC_RTN_64                  = 0x00000038,
TC_OP_ATOMIC_DEC_RTN_64                  = 0x00000039,
TC_OP_WBL2_NC                            = 0x0000003a,
TC_OP_WBL2_WC                            = 0x0000003b,
TC_OP_RESERVED_NON_FLOAT_RTN_64_1        = 0x0000003c,
TC_OP_RESERVED_NON_FLOAT_RTN_64_2        = 0x0000003d,
TC_OP_RESERVED_NON_FLOAT_RTN_64_3        = 0x0000003e,
TC_OP_RESERVED_NON_FLOAT_RTN_64_4        = 0x0000003f,
TC_OP_WBINVL1                            = 0x00000040,
TC_OP_ATOMIC_FCMPSWAP_32                 = 0x00000041,
TC_OP_ATOMIC_FMIN_32                     = 0x00000042,
TC_OP_ATOMIC_FMAX_32                     = 0x00000043,
TC_OP_RESERVED_FOP_32_0                  = 0x00000044,
TC_OP_RESERVED_FADD_32                   = 0x00000045,
TC_OP_RESERVED_FOP_32_2                  = 0x00000046,
TC_OP_ATOMIC_SWAP_32                     = 0x00000047,
TC_OP_ATOMIC_CMPSWAP_32                  = 0x00000048,
TC_OP_ATOMIC_FCMPSWAP_FLUSH_DENORM_32    = 0x00000049,
TC_OP_ATOMIC_FMIN_FLUSH_DENORM_32        = 0x0000004a,
TC_OP_ATOMIC_FMAX_FLUSH_DENORM_32        = 0x0000004b,
TC_OP_INV_METADATA                       = 0x0000004c,
TC_OP_ATOMIC_FADD_FLUSH_DENORM_32        = 0x0000004d,
TC_OP_RESERVED_FOP_FLUSH_DENORM_32_2     = 0x0000004e,
TC_OP_ATOMIC_ADD_32                      = 0x0000004f,
TC_OP_ATOMIC_SUB_32                      = 0x00000050,
TC_OP_ATOMIC_SMIN_32                     = 0x00000051,
TC_OP_ATOMIC_UMIN_32                     = 0x00000052,
TC_OP_ATOMIC_SMAX_32                     = 0x00000053,
TC_OP_ATOMIC_UMAX_32                     = 0x00000054,
TC_OP_ATOMIC_AND_32                      = 0x00000055,
TC_OP_ATOMIC_OR_32                       = 0x00000056,
TC_OP_ATOMIC_XOR_32                      = 0x00000057,
TC_OP_ATOMIC_INC_32                      = 0x00000058,
TC_OP_ATOMIC_DEC_32                      = 0x00000059,
TC_OP_INVL2_NC                           = 0x0000005a,
TC_OP_NOP_RTN0                           = 0x0000005b,
TC_OP_RESERVED_NON_FLOAT_32_1            = 0x0000005c,
TC_OP_RESERVED_NON_FLOAT_32_2            = 0x0000005d,
TC_OP_RESERVED_NON_FLOAT_32_3            = 0x0000005e,
TC_OP_RESERVED_NON_FLOAT_32_4            = 0x0000005f,
TC_OP_WBINVL2                            = 0x00000060,
TC_OP_ATOMIC_FCMPSWAP_64                 = 0x00000061,
TC_OP_ATOMIC_FMIN_64                     = 0x00000062,
TC_OP_ATOMIC_FMAX_64                     = 0x00000063,
TC_OP_RESERVED_FOP_64_0                  = 0x00000064,
TC_OP_RESERVED_FOP_64_1                  = 0x00000065,
TC_OP_RESERVED_FOP_64_2                  = 0x00000066,
TC_OP_ATOMIC_SWAP_64                     = 0x00000067,
TC_OP_ATOMIC_CMPSWAP_64                  = 0x00000068,
TC_OP_ATOMIC_FCMPSWAP_FLUSH_DENORM_64    = 0x00000069,
TC_OP_ATOMIC_FMIN_FLUSH_DENORM_64        = 0x0000006a,
TC_OP_ATOMIC_FMAX_FLUSH_DENORM_64        = 0x0000006b,
TC_OP_RESERVED_FOP_FLUSH_DENORM_64_0     = 0x0000006c,
TC_OP_RESERVED_FOP_FLUSH_DENORM_64_1     = 0x0000006d,
TC_OP_RESERVED_FOP_FLUSH_DENORM_64_2     = 0x0000006e,
TC_OP_ATOMIC_ADD_64                      = 0x0000006f,
TC_OP_ATOMIC_SUB_64                      = 0x00000070,
TC_OP_ATOMIC_SMIN_64                     = 0x00000071,
TC_OP_ATOMIC_UMIN_64                     = 0x00000072,
TC_OP_ATOMIC_SMAX_64                     = 0x00000073,
TC_OP_ATOMIC_UMAX_64                     = 0x00000074,
TC_OP_ATOMIC_AND_64                      = 0x00000075,
TC_OP_ATOMIC_OR_64                       = 0x00000076,
TC_OP_ATOMIC_XOR_64                      = 0x00000077,
TC_OP_ATOMIC_INC_64                      = 0x00000078,
TC_OP_ATOMIC_DEC_64                      = 0x00000079,
TC_OP_WBINVL2_NC                         = 0x0000007a,
TC_OP_NOP_ACK                            = 0x0000007b,
TC_OP_RESERVED_NON_FLOAT_64_1            = 0x0000007c,
TC_OP_RESERVED_NON_FLOAT_64_2            = 0x0000007d,
TC_OP_RESERVED_NON_FLOAT_64_3            = 0x0000007e,
TC_OP_RESERVED_NON_FLOAT_64_4            = 0x0000007f,
} TC_OP;

/*
 * TC_OP_MASKS enum
 */

typedef enum TC_OP_MASKS {
TC_OP_MASK_FLUSH_DENROM                  = 0x00000008,
TC_OP_MASK_64                            = 0x00000020,
TC_OP_MASK_NO_RTN                        = 0x00000040,
} TC_OP_MASKS;

/*******************************************************
 * GL2 Enums
 *******************************************************/

/*
 * GL2_EA_CID enum
 */

typedef enum GL2_EA_CID {
GL2_EA_CID_CLIENT                        = 0x00000000,
GL2_EA_CID_SDMA                          = 0x00000001,
GL2_EA_CID_RLC                           = 0x00000002,
GL2_EA_CID_SQC                           = 0x00000003,
GL2_EA_CID_CP                            = 0x00000004,
GL2_EA_CID_CPDMA                         = 0x00000005,
GL2_EA_CID_UTCL2                         = 0x00000006,
GL2_EA_CID_RT                            = 0x00000007,
GL2_EA_CID_FMASK                         = 0x00000008,
GL2_EA_CID_DCC                           = 0x00000009,
GL2_EA_CID_Z_STENCIL                     = 0x0000000a,
GL2_EA_CID_ZPCPSD                        = 0x0000000b,
GL2_EA_CID_HTILE                         = 0x0000000c,
GL2_EA_CID_MES                           = 0x0000000d,
GL2_EA_CID_TCPMETA                       = 0x0000000f,
} GL2_EA_CID;

/*
 * GL2_NACKS enum
 */

typedef enum GL2_NACKS {
GL2_NACK_NO_FAULT                        = 0x00000000,
GL2_NACK_PAGE_FAULT                      = 0x00000001,
GL2_NACK_PROTECTION_FAULT                = 0x00000002,
GL2_NACK_DATA_ERROR                      = 0x00000003,
} GL2_NACKS;

/*
 * GL2_OP enum
 */

typedef enum GL2_OP {
GL2_OP_READ                              = 0x00000000,
GL2_OP_ATOMIC_FCMPSWAP_RTN_32            = 0x00000001,
GL2_OP_ATOMIC_FMIN_RTN_32                = 0x00000002,
GL2_OP_ATOMIC_FMAX_RTN_32                = 0x00000003,
GL2_OP_ATOMIC_SWAP_RTN_32                = 0x00000007,
GL2_OP_ATOMIC_CMPSWAP_RTN_32             = 0x00000008,
GL2_OP_ATOMIC_FCMPSWAP_FLUSH_DENORM_RTN_32 = 0x00000009,
GL2_OP_ATOMIC_FMIN_FLUSH_DENORM_RTN_32   = 0x0000000a,
GL2_OP_ATOMIC_FMAX_FLUSH_DENORM_RTN_32   = 0x0000000b,
GL2_OP_PROBE_FILTER                      = 0x0000000c,
GL2_OP_ATOMIC_FADD_FLUSH_DENORM_RTN_32   = 0x0000000d,
GL2_OP_RESERVED_FOP_FLUSH_DENORM_RTN_32_2 = 0x0000000e,
GL2_OP_ATOMIC_ADD_RTN_32                 = 0x0000000f,
GL2_OP_ATOMIC_SUB_RTN_32                 = 0x00000010,
GL2_OP_ATOMIC_SMIN_RTN_32                = 0x00000011,
GL2_OP_ATOMIC_UMIN_RTN_32                = 0x00000012,
GL2_OP_ATOMIC_SMAX_RTN_32                = 0x00000013,
GL2_OP_ATOMIC_UMAX_RTN_32                = 0x00000014,
GL2_OP_ATOMIC_AND_RTN_32                 = 0x00000015,
GL2_OP_ATOMIC_OR_RTN_32                  = 0x00000016,
GL2_OP_ATOMIC_XOR_RTN_32                 = 0x00000017,
GL2_OP_ATOMIC_INC_RTN_32                 = 0x00000018,
GL2_OP_ATOMIC_DEC_RTN_32                 = 0x00000019,
GL2_OP_ATOMIC_CLAMP_SUB_RTN_32           = 0x0000001a,
GL2_OP_WRITE                             = 0x00000020,
GL2_OP_ATOMIC_FCMPSWAP_RTN_64            = 0x00000021,
GL2_OP_ATOMIC_FMIN_RTN_64                = 0x00000022,
GL2_OP_ATOMIC_FMAX_RTN_64                = 0x00000023,
GL2_OP_ATOMIC_SWAP_RTN_64                = 0x00000027,
GL2_OP_ATOMIC_CMPSWAP_RTN_64             = 0x00000028,
GL2_OP_ATOMIC_FCMPSWAP_FLUSH_DENORM_RTN_64 = 0x00000029,
GL2_OP_ATOMIC_FMIN_FLUSH_DENORM_RTN_64   = 0x0000002a,
GL2_OP_ATOMIC_FMAX_FLUSH_DENORM_RTN_64   = 0x0000002b,
GL2_OP_ATOMIC_ADD_RTN_64                 = 0x0000002f,
GL2_OP_ATOMIC_SUB_RTN_64                 = 0x00000030,
GL2_OP_ATOMIC_SMIN_RTN_64                = 0x00000031,
GL2_OP_ATOMIC_UMIN_RTN_64                = 0x00000032,
GL2_OP_ATOMIC_SMAX_RTN_64                = 0x00000033,
GL2_OP_ATOMIC_UMAX_RTN_64                = 0x00000034,
GL2_OP_ATOMIC_AND_RTN_64                 = 0x00000035,
GL2_OP_ATOMIC_OR_RTN_64                  = 0x00000036,
GL2_OP_ATOMIC_XOR_RTN_64                 = 0x00000037,
GL2_OP_ATOMIC_INC_RTN_64                 = 0x00000038,
GL2_OP_ATOMIC_DEC_RTN_64                 = 0x00000039,
GL2_OP_GL1_INV                           = 0x00000040,
GL2_OP_ATOMIC_FCMPSWAP_32                = 0x00000041,
GL2_OP_ATOMIC_FMIN_32                    = 0x00000042,
GL2_OP_ATOMIC_FMAX_32                    = 0x00000043,
GL2_OP_ATOMIC_SWAP_32                    = 0x00000047,
GL2_OP_ATOMIC_CMPSWAP_32                 = 0x00000048,
GL2_OP_ATOMIC_FCMPSWAP_FLUSH_DENORM_32   = 0x00000049,
GL2_OP_ATOMIC_FMIN_FLUSH_DENORM_32       = 0x0000004a,
GL2_OP_ATOMIC_FMAX_FLUSH_DENORM_32       = 0x0000004b,
GL2_OP_ATOMIC_UMIN_8                     = 0x0000004c,
GL2_OP_ATOMIC_FADD_FLUSH_DENORM_32       = 0x0000004d,
GL2_OP_ATOMIC_ADD_32                     = 0x0000004f,
GL2_OP_ATOMIC_SUB_32                     = 0x00000050,
GL2_OP_ATOMIC_SMIN_32                    = 0x00000051,
GL2_OP_ATOMIC_UMIN_32                    = 0x00000052,
GL2_OP_ATOMIC_SMAX_32                    = 0x00000053,
GL2_OP_ATOMIC_UMAX_32                    = 0x00000054,
GL2_OP_ATOMIC_AND_32                     = 0x00000055,
GL2_OP_ATOMIC_OR_32                      = 0x00000056,
GL2_OP_ATOMIC_XOR_32                     = 0x00000057,
GL2_OP_ATOMIC_INC_32                     = 0x00000058,
GL2_OP_ATOMIC_DEC_32                     = 0x00000059,
GL2_OP_NOP_RTN0                          = 0x0000005b,
GL2_OP_ATOMIC_FCMPSWAP_64                = 0x00000061,
GL2_OP_ATOMIC_FMIN_64                    = 0x00000062,
GL2_OP_ATOMIC_FMAX_64                    = 0x00000063,
GL2_OP_ATOMIC_SWAP_64                    = 0x00000067,
GL2_OP_ATOMIC_CMPSWAP_64                 = 0x00000068,
GL2_OP_ATOMIC_FCMPSWAP_FLUSH_DENORM_64   = 0x00000069,
GL2_OP_ATOMIC_FMIN_FLUSH_DENORM_64       = 0x0000006a,
GL2_OP_ATOMIC_FMAX_FLUSH_DENORM_64       = 0x0000006b,
GL2_OP_ATOMIC_ADD_64                     = 0x0000006f,
GL2_OP_ATOMIC_SUB_64                     = 0x00000070,
GL2_OP_ATOMIC_SMIN_64                    = 0x00000071,
GL2_OP_ATOMIC_UMIN_64                    = 0x00000072,
GL2_OP_ATOMIC_SMAX_64                    = 0x00000073,
GL2_OP_ATOMIC_UMAX_64                    = 0x00000074,
GL2_OP_ATOMIC_AND_64                     = 0x00000075,
GL2_OP_ATOMIC_OR_64                      = 0x00000076,
GL2_OP_ATOMIC_XOR_64                     = 0x00000077,
GL2_OP_ATOMIC_INC_64                     = 0x00000078,
GL2_OP_ATOMIC_DEC_64                     = 0x00000079,
GL2_OP_ATOMIC_UMAX_8                     = 0x0000007a,
GL2_OP_NOP_ACK                           = 0x0000007b,
} GL2_OP;

/*
 * GL2_OP_MASKS enum
 */

typedef enum GL2_OP_MASKS {
GL2_OP_MASK_FLUSH_DENROM                 = 0x00000008,
GL2_OP_MASK_64                           = 0x00000020,
GL2_OP_MASK_NO_RTN                       = 0x00000040,
} GL2_OP_MASKS;

/*******************************************************
 * RLC Enums
 *******************************************************/

/*
 * RLC_DOORBELL_MODE enum
 */

typedef enum RLC_DOORBELL_MODE {
RLC_DOORBELL_MODE_DISABLE                = 0x00000000,
RLC_DOORBELL_MODE_ENABLE                 = 0x00000001,
RLC_DOORBELL_MODE_ENABLE_PF              = 0x00000002,
RLC_DOORBELL_MODE_ENABLE_PF_VF           = 0x00000003,
} RLC_DOORBELL_MODE;

/*
 * RLC_PERFCOUNTER_SEL enum
 */

typedef enum RLC_PERFCOUNTER_SEL {
RLC_PERF_SEL_POWER_FEATURE_0             = 0x00000000,
RLC_PERF_SEL_POWER_FEATURE_1             = 0x00000001,
RLC_PERF_SEL_CP_INTERRUPT                = 0x00000002,
RLC_PERF_SEL_GRBM_INTERRUPT              = 0x00000003,
RLC_PERF_SEL_SPM_INTERRUPT               = 0x00000004,
RLC_PERF_SEL_IH_INTERRUPT                = 0x00000005,
RLC_PERF_SEL_SERDES_COMMAND_WRITE        = 0x00000006,
} RLC_PERFCOUNTER_SEL;

/*
 * RLC_PERFMON_STATE enum
 */

typedef enum RLC_PERFMON_STATE {
RLC_PERFMON_STATE_RESET                  = 0x00000000,
RLC_PERFMON_STATE_ENABLE                 = 0x00000001,
RLC_PERFMON_STATE_DISABLE                = 0x00000002,
RLC_PERFMON_STATE_RESERVED_3             = 0x00000003,
RLC_PERFMON_STATE_RESERVED_4             = 0x00000004,
RLC_PERFMON_STATE_RESERVED_5             = 0x00000005,
RLC_PERFMON_STATE_RESERVED_6             = 0x00000006,
RLC_PERFMON_STATE_ROLLOVER               = 0x00000007,
} RLC_PERFMON_STATE;

/*
 * RSPM_CMD enum
 */

typedef enum RSPM_CMD {
RSPM_CMD_INVALID                         = 0x00000000,
RSPM_CMD_IDLE                            = 0x00000001,
RSPM_CMD_CALIBRATE                       = 0x00000002,
RSPM_CMD_SPM_RESET                       = 0x00000003,
RSPM_CMD_SPM_START                       = 0x00000004,
RSPM_CMD_SPM_STOP                        = 0x00000005,
RSPM_CMD_PERF_RESET                      = 0x00000006,
RSPM_CMD_PERF_SAMPLE                     = 0x00000007,
RSPM_CMD_PROF_START                      = 0x00000008,
RSPM_CMD_PROF_STOP                       = 0x00000009,
RSPM_CMD_FORCE_SAMPLE                    = 0x0000000a,
} RSPM_CMD;

/*******************************************************
 * SPI Enums
 *******************************************************/

/*
 * CLKGATE_BASE_MODE enum
 */

typedef enum CLKGATE_BASE_MODE {
MULT_8                                   = 0x00000000,
MULT_16                                  = 0x00000001,
} CLKGATE_BASE_MODE;

/*
 * CLKGATE_SM_MODE enum
 */

typedef enum CLKGATE_SM_MODE {
ON_SEQ                                   = 0x00000000,
OFF_SEQ                                  = 0x00000001,
PROG_SEQ                                 = 0x00000002,
READ_SEQ                                 = 0x00000003,
SM_MODE_RESERVED                         = 0x00000004,
} CLKGATE_SM_MODE;

/*
 * SPI_FOG_MODE enum
 */

typedef enum SPI_FOG_MODE {
SPI_FOG_NONE                             = 0x00000000,
SPI_FOG_EXP                              = 0x00000001,
SPI_FOG_EXP2                             = 0x00000002,
SPI_FOG_LINEAR                           = 0x00000003,
} SPI_FOG_MODE;

/*
 * SPI_LB_WAVES_SELECT enum
 */

typedef enum SPI_LB_WAVES_SELECT {
HS_GS                                    = 0x00000000,
PS                                       = 0x00000001,
CS_NA                                    = 0x00000002,
SPI_LB_WAVES_RSVD                        = 0x00000003,
} SPI_LB_WAVES_SELECT;

/*
 * SPI_PERFCNT_SEL enum
 */

typedef enum SPI_PERFCNT_SEL {
SPI_PERF_GS_WINDOW_VALID                 = 0x00000001,
SPI_PERF_GS_BUSY                         = 0x00000002,
SPI_PERF_GS_CRAWLER_STALL                = 0x00000003,
SPI_PERF_GS_EVENT_WAVE                   = 0x00000004,
SPI_PERF_GS_WAVE                         = 0x00000005,
SPI_PERF_GS_PERS_UPD_FULL0               = 0x00000006,
SPI_PERF_GS_PERS_UPD_FULL1               = 0x00000007,
SPI_PERF_GS_FIRST_SUBGRP                 = 0x00000008,
SPI_PERF_GS_HS_DEALLOC                   = 0x00000009,
SPI_PERF_GS_NGG_SE_LATE_ALLOC_LIMIT      = 0x0000000a,
SPI_PERF_GS_POS0_STALL                   = 0x0000000b,
SPI_PERF_GS_POS1_STALL                   = 0x0000000c,
SPI_PERF_GS_INDX0_STALL                  = 0x0000000d,
SPI_PERF_GS_INDX1_STALL                  = 0x0000000e,
SPI_PERF_GS_PWS_STALL                    = 0x0000000f,
SPI_PERF_HS_WINDOW_VALID                 = 0x00000015,
SPI_PERF_HS_BUSY                         = 0x00000016,
SPI_PERF_HS_CRAWLER_STALL                = 0x00000017,
SPI_PERF_HS_FIRST_WAVE                   = 0x00000018,
SPI_PERF_HS_OFFCHIP_LDS_STALL            = 0x00000019,
SPI_PERF_HS_EVENT_WAVE                   = 0x0000001a,
SPI_PERF_HS_WAVE                         = 0x0000001b,
SPI_PERF_HS_PERS_UPD_FULL0               = 0x0000001c,
SPI_PERF_HS_PERS_UPD_FULL1               = 0x0000001d,
SPI_PERF_HS_PWS_STALL                    = 0x0000001e,
SPI_PERF_CSGN_WINDOW_VALID               = 0x00000025,
SPI_PERF_CSGN_BUSY                       = 0x00000026,
SPI_PERF_CSGN_NUM_THREADGROUPS           = 0x00000027,
SPI_PERF_CSGN_CRAWLER_STALL              = 0x00000028,
SPI_PERF_CSGN_EVENT_WAVE                 = 0x00000029,
SPI_PERF_CSGN_WAVE                       = 0x0000002a,
SPI_PERF_CSGN_PWS_STALL                  = 0x0000002b,
SPI_PERF_CSN_WINDOW_VALID                = 0x0000002c,
SPI_PERF_CSN_BUSY                        = 0x0000002d,
SPI_PERF_CSN_NUM_THREADGROUPS            = 0x0000002e,
SPI_PERF_CSN_CRAWLER_STALL               = 0x0000002f,
SPI_PERF_CSN_EVENT_WAVE                  = 0x00000030,
SPI_PERF_CSN_WAVE                        = 0x00000031,
SPI_PERF_PS0_WINDOW_VALID                = 0x00000035,
SPI_PERF_PS1_WINDOW_VALID                = 0x00000036,
SPI_PERF_PS2_WINDOW_VALID                = 0x00000037,
SPI_PERF_PS3_WINDOW_VALID                = 0x00000038,
SPI_PERF_PS0_BUSY                        = 0x00000039,
SPI_PERF_PS1_BUSY                        = 0x0000003a,
SPI_PERF_PS2_BUSY                        = 0x0000003b,
SPI_PERF_PS3_BUSY                        = 0x0000003c,
SPI_PERF_PS0_ACTIVE                      = 0x0000003d,
SPI_PERF_PS1_ACTIVE                      = 0x0000003e,
SPI_PERF_PS2_ACTIVE                      = 0x0000003f,
SPI_PERF_PS3_ACTIVE                      = 0x00000040,
SPI_PERF_PS0_DEALLOC                     = 0x00000041,
SPI_PERF_PS1_DEALLOC                     = 0x00000042,
SPI_PERF_PS2_DEALLOC                     = 0x00000043,
SPI_PERF_PS3_DEALLOC                     = 0x00000044,
SPI_PERF_PS0_EVENT_WAVE                  = 0x00000045,
SPI_PERF_PS1_EVENT_WAVE                  = 0x00000046,
SPI_PERF_PS2_EVENT_WAVE                  = 0x00000047,
SPI_PERF_PS3_EVENT_WAVE                  = 0x00000048,
SPI_PERF_PS0_WAVE                        = 0x00000049,
SPI_PERF_PS1_WAVE                        = 0x0000004a,
SPI_PERF_PS2_WAVE                        = 0x0000004b,
SPI_PERF_PS3_WAVE                        = 0x0000004c,
SPI_PERF_PS0_OPT_WAVE                    = 0x0000004d,
SPI_PERF_PS1_OPT_WAVE                    = 0x0000004e,
SPI_PERF_PS2_OPT_WAVE                    = 0x0000004f,
SPI_PERF_PS3_OPT_WAVE                    = 0x00000050,
SPI_PERF_PS0_PRIM_BIN0                   = 0x00000051,
SPI_PERF_PS1_PRIM_BIN0                   = 0x00000052,
SPI_PERF_PS2_PRIM_BIN0                   = 0x00000053,
SPI_PERF_PS3_PRIM_BIN0                   = 0x00000054,
SPI_PERF_PS0_PRIM_BIN1                   = 0x00000055,
SPI_PERF_PS1_PRIM_BIN1                   = 0x00000056,
SPI_PERF_PS2_PRIM_BIN1                   = 0x00000057,
SPI_PERF_PS3_PRIM_BIN1                   = 0x00000058,
SPI_PERF_PS0_CRAWLER_STALL               = 0x00000059,
SPI_PERF_PS1_CRAWLER_STALL               = 0x0000005a,
SPI_PERF_PS2_CRAWLER_STALL               = 0x0000005b,
SPI_PERF_PS3_CRAWLER_STALL               = 0x0000005c,
SPI_PERF_PS_PERS_UPD_FULL0               = 0x0000005d,
SPI_PERF_PS_PERS_UPD_FULL1               = 0x0000005e,
SPI_PERF_PS0_2_WAVE_GROUPS               = 0x0000005f,
SPI_PERF_PS1_2_WAVE_GROUPS               = 0x00000060,
SPI_PERF_PS2_2_WAVE_GROUPS               = 0x00000061,
SPI_PERF_PS3_2_WAVE_GROUPS               = 0x00000062,
SPI_PERF_PS0_WAVE_GROUP_CLOCK_DELAY      = 0x00000063,
SPI_PERF_PS1_WAVE_GROUP_CLOCK_DELAY      = 0x00000064,
SPI_PERF_PS2_WAVE_GROUP_CLOCK_DELAY      = 0x00000065,
SPI_PERF_PS3_WAVE_GROUP_CLOCK_DELAY      = 0x00000066,
SPI_PERF_PS0_WAVE_GROUP_TIMEOUTS         = 0x00000067,
SPI_PERF_PS1_WAVE_GROUP_TIMEOUTS         = 0x00000068,
SPI_PERF_PS2_WAVE_GROUP_TIMEOUTS         = 0x00000069,
SPI_PERF_PS3_WAVE_GROUP_TIMEOUTS         = 0x0000006a,
SPI_PERF_PS_PWS_STALL                    = 0x0000006b,
SPI_PERF_RA_PIPE_REQ_BIN2                = 0x0000008d,
SPI_PERF_RA_TASK_REQ_BIN3                = 0x0000008e,
SPI_PERF_RA_WR_CTL_FULL                  = 0x0000008f,
SPI_PERF_RA_REQ_NO_ALLOC                 = 0x00000090,
SPI_PERF_RA_REQ_NO_ALLOC_PS              = 0x00000091,
SPI_PERF_RA_REQ_NO_ALLOC_GS              = 0x00000092,
SPI_PERF_RA_REQ_NO_ALLOC_HS              = 0x00000093,
SPI_PERF_RA_REQ_NO_ALLOC_CSG             = 0x00000094,
SPI_PERF_RA_REQ_NO_ALLOC_CSN             = 0x00000095,
SPI_PERF_RA_RES_STALL_PS                 = 0x00000096,
SPI_PERF_RA_RES_STALL_GS                 = 0x00000097,
SPI_PERF_RA_RES_STALL_HS                 = 0x00000098,
SPI_PERF_RA_RES_STALL_CSG                = 0x00000099,
SPI_PERF_RA_RES_STALL_CSN                = 0x0000009a,
SPI_PERF_RA_TMP_STALL_PS                 = 0x0000009b,
SPI_PERF_RA_TMP_STALL_GS                 = 0x0000009c,
SPI_PERF_RA_TMP_STALL_HS                 = 0x0000009d,
SPI_PERF_RA_TMP_STALL_CSG                = 0x0000009e,
SPI_PERF_RA_TMP_STALL_CSN                = 0x0000009f,
SPI_PERF_RA_WAVE_SIMD_FULL_PS            = 0x000000a0,
SPI_PERF_RA_WAVE_SIMD_FULL_GS            = 0x000000a1,
SPI_PERF_RA_WAVE_SIMD_FULL_HS            = 0x000000a2,
SPI_PERF_RA_WAVE_SIMD_FULL_CSG           = 0x000000a3,
SPI_PERF_RA_WAVE_SIMD_FULL_CSN           = 0x000000a4,
SPI_PERF_RA_VGPR_SIMD_FULL_PS            = 0x000000a5,
SPI_PERF_RA_VGPR_SIMD_FULL_GS            = 0x000000a6,
SPI_PERF_RA_VGPR_SIMD_FULL_HS            = 0x000000a7,
SPI_PERF_RA_VGPR_SIMD_FULL_CSG           = 0x000000a8,
SPI_PERF_RA_VGPR_SIMD_FULL_CSN           = 0x000000a9,
SPI_PERF_RA_LDS_CU_FULL_PS               = 0x000000aa,
SPI_PERF_RA_LDS_CU_FULL_HS               = 0x000000ab,
SPI_PERF_RA_LDS_CU_FULL_GS               = 0x000000ac,
SPI_PERF_RA_LDS_CU_FULL_CSG              = 0x000000ad,
SPI_PERF_RA_LDS_CU_FULL_CSN              = 0x000000ae,
SPI_PERF_RA_BAR_CU_FULL_HS               = 0x000000af,
SPI_PERF_RA_BAR_CU_FULL_CSG              = 0x000000b0,
SPI_PERF_RA_BAR_CU_FULL_CSN              = 0x000000b1,
SPI_PERF_RA_BULKY_CU_FULL_CSG            = 0x000000b2,
SPI_PERF_RA_BULKY_CU_FULL_CSN            = 0x000000b3,
SPI_PERF_RA_TGLIM_CU_FULL_CSG            = 0x000000b4,
SPI_PERF_RA_TGLIM_CU_FULL_CSN            = 0x000000b5,
SPI_PERF_RA_WVLIM_STALL_PS               = 0x000000b6,
SPI_PERF_RA_WVLIM_STALL_GS               = 0x000000b7,
SPI_PERF_RA_WVLIM_STALL_HS               = 0x000000b8,
SPI_PERF_RA_WVLIM_STALL_CSG              = 0x000000b9,
SPI_PERF_RA_WVLIM_STALL_CSN              = 0x000000ba,
SPI_PERF_RA_GS_LOCK                      = 0x000000bb,
SPI_PERF_RA_HS_LOCK                      = 0x000000bc,
SPI_PERF_RA_CSG_LOCK                     = 0x000000bd,
SPI_PERF_RA_CSN_LOCK                     = 0x000000be,
SPI_PERF_RA_RSV_UPD                      = 0x000000bf,
SPI_PERF_RA_PRE_ALLOC_STALL              = 0x000000c0,
SPI_PERF_RA_GFX_UNDER_TUNNEL             = 0x000000c1,
SPI_PERF_RA_CSC_UNDER_TUNNEL             = 0x000000c2,
SPI_PERF_RA_WVALLOC_STALL                = 0x000000c3,
SPI_PERF_RA_ACCUM0_SIMD_FULL_PS          = 0x000000c4,
SPI_PERF_RA_ACCUM1_SIMD_FULL_PS          = 0x000000c5,
SPI_PERF_RA_ACCUM2_SIMD_FULL_PS          = 0x000000c6,
SPI_PERF_RA_ACCUM3_SIMD_FULL_PS          = 0x000000c7,
SPI_PERF_RA_ACCUM0_SIMD_FULL_GS          = 0x000000c8,
SPI_PERF_RA_ACCUM1_SIMD_FULL_GS          = 0x000000c9,
SPI_PERF_RA_ACCUM2_SIMD_FULL_GS          = 0x000000ca,
SPI_PERF_RA_ACCUM3_SIMD_FULL_GS          = 0x000000cb,
SPI_PERF_RA_ACCUM0_SIMD_FULL_HS          = 0x000000cc,
SPI_PERF_RA_ACCUM1_SIMD_FULL_HS          = 0x000000cd,
SPI_PERF_RA_ACCUM2_SIMD_FULL_HS          = 0x000000ce,
SPI_PERF_RA_ACCUM3_SIMD_FULL_HS          = 0x000000cf,
SPI_PERF_RA_ACCUM0_SIMD_FULL_CSG         = 0x000000d0,
SPI_PERF_RA_ACCUM1_SIMD_FULL_CSG         = 0x000000d1,
SPI_PERF_RA_ACCUM2_SIMD_FULL_CSG         = 0x000000d2,
SPI_PERF_RA_ACCUM3_SIMD_FULL_CSG         = 0x000000d3,
SPI_PERF_RA_ACCUM0_SIMD_FULL_CSN         = 0x000000d4,
SPI_PERF_RA_ACCUM1_SIMD_FULL_CSN         = 0x000000d5,
SPI_PERF_RA_ACCUM2_SIMD_FULL_CSN         = 0x000000d6,
SPI_PERF_RA_ACCUM3_SIMD_FULL_CSN         = 0x000000d7,
SPI_PERF_EXP_ARB_COL_CNT                 = 0x000000d8,
SPI_PERF_EXP_ARB_POS_CNT                 = 0x000000d9,
SPI_PERF_EXP_ARB_GDS_CNT                 = 0x000000da,
SPI_PERF_EXP_ARB_IDX_CNT                 = 0x000000db,
SPI_PERF_EXP_WITH_CONFLICT               = 0x000000dc,
SPI_PERF_EXP_WITH_CONFLICT_CLEAR         = 0x000000dd,
SPI_PERF_GS_EXP_DONE                     = 0x000000de,
SPI_PERF_PS_EXP_DONE                     = 0x000000df,
SPI_PERF_PS_EXP_ARB_CONFLICT             = 0x000000e0,
SPI_PERF_PS_EXP_ALLOC                    = 0x000000e1,
SPI_PERF_PS0_WAVEID_STARVED              = 0x000000e2,
SPI_PERF_PS1_WAVEID_STARVED              = 0x000000e3,
SPI_PERF_PS2_WAVEID_STARVED              = 0x000000e4,
SPI_PERF_PS3_WAVEID_STARVED              = 0x000000e5,
SPI_PERF_PS0_EXP_ALLOC_WITH_CONFLICT     = 0x000000e6,
SPI_PERF_PS1_EXP_ALLOC_WITH_CONFLICT     = 0x000000e7,
SPI_PERF_PS2_EXP_ALLOC_WITH_CONFLICT     = 0x000000e8,
SPI_PERF_PS3_EXP_ALLOC_WITH_CONFLICT     = 0x000000e9,
SPI_PERF_NUM_PS_COL_SA0SQ0_EXPORTS       = 0x000000ea,
SPI_PERF_NUM_PS_COL_SA0SQ1_EXPORTS       = 0x000000eb,
SPI_PERF_NUM_PS_COL_SA1SQ0_EXPORTS       = 0x000000ec,
SPI_PERF_NUM_PS_COL_SA1SQ1_EXPORTS       = 0x000000ed,
SPI_PERF_NUM_POS_SA0SQ0_EXPORTS          = 0x000000ee,
SPI_PERF_NUM_POS_SA0SQ1_EXPORTS          = 0x000000ef,
SPI_PERF_NUM_POS_SA1SQ0_EXPORTS          = 0x000000f0,
SPI_PERF_NUM_POS_SA1SQ1_EXPORTS          = 0x000000f1,
SPI_PERF_NUM_GDS_SA0SQ0_EXPORTS          = 0x000000f2,
SPI_PERF_NUM_GDS_SA0SQ1_EXPORTS          = 0x000000f3,
SPI_PERF_NUM_GDS_SA1SQ0_EXPORTS          = 0x000000f4,
SPI_PERF_NUM_GDS_SA1SQ1_EXPORTS          = 0x000000f5,
SPI_PERF_NUM_EXPGRANT_EXPORTS            = 0x000000f6,
SPI_PERF_PIX_ALLOC_PEND_CNT              = 0x000000fd,
SPI_PERF_EXPORT_SCB0_STALL               = 0x000000fe,
SPI_PERF_EXPORT_SCB1_STALL               = 0x000000ff,
SPI_PERF_EXPORT_SCB2_STALL               = 0x00000100,
SPI_PERF_EXPORT_SCB3_STALL               = 0x00000101,
SPI_PERF_EXPORT_DB0_STALL                = 0x00000102,
SPI_PERF_EXPORT_DB1_STALL                = 0x00000103,
SPI_PERF_EXPORT_DB2_STALL                = 0x00000104,
SPI_PERF_EXPORT_DB3_STALL                = 0x00000105,
SPI_PERF_EXPORT_DB4_STALL                = 0x00000106,
SPI_PERF_EXPORT_DB5_STALL                = 0x00000107,
SPI_PERF_EXPORT_DB6_STALL                = 0x00000108,
SPI_PERF_EXPORT_DB7_STALL                = 0x00000109,
SPI_PERF_GS_NGG_SE_SEND_GS_ALLOC         = 0x0000010a,
SPI_PERF_GS_NGG_STALL_MSG_VAL            = 0x0000010b,
SPI_PERF_SWC_PS_WR                       = 0x0000010c,
SPI_PERF_SWC_GS_WR                       = 0x0000010d,
SPI_PERF_SWC_HS_WR                       = 0x0000010e,
SPI_PERF_SWC_CSGN_WR                     = 0x0000010f,
SPI_PERF_SWC_CSN_WR                      = 0x00000110,
SPI_PERF_VWC_PS_WR                       = 0x00000111,
SPI_PERF_VWC_ES_WR                       = 0x00000112,
SPI_PERF_VWC_GS_WR                       = 0x00000113,
SPI_PERF_VWC_LS_WR                       = 0x00000114,
SPI_PERF_VWC_HS_WR                       = 0x00000115,
SPI_PERF_VWC_CSGN_WR                     = 0x00000116,
SPI_PERF_VWC_CSN_WR                      = 0x00000117,
SPI_PERF_EXP_THROT_UPSTEP                = 0x00000118,
SPI_PERF_EXP_THROT_DOWNSTEP              = 0x00000119,
SPI_PERF_EXP_THROT_CAUSALITY_DETECTED    = 0x0000011a,
SPI_PERF_BUSY                            = 0x0000011b,
} SPI_PERFCNT_SEL;

/*
 * SPI_PNT_SPRITE_OVERRIDE enum
 */

typedef enum SPI_PNT_SPRITE_OVERRIDE {
SPI_PNT_SPRITE_SEL_0                     = 0x00000000,
SPI_PNT_SPRITE_SEL_1                     = 0x00000001,
SPI_PNT_SPRITE_SEL_S                     = 0x00000002,
SPI_PNT_SPRITE_SEL_T                     = 0x00000003,
SPI_PNT_SPRITE_SEL_NONE                  = 0x00000004,
} SPI_PNT_SPRITE_OVERRIDE;

/*
 * SPI_PS_LDS_GROUP_SIZE enum
 */

typedef enum SPI_PS_LDS_GROUP_SIZE {
SPI_PS_LDS_GROUP_1                       = 0x00000000,
SPI_PS_LDS_GROUP_2                       = 0x00000001,
SPI_PS_LDS_GROUP_4                       = 0x00000002,
} SPI_PS_LDS_GROUP_SIZE;

/*
 * SPI_SAMPLE_CNTL enum
 */

typedef enum SPI_SAMPLE_CNTL {
CENTROIDS_ONLY                           = 0x00000000,
CENTERS_ONLY                             = 0x00000001,
CENTROIDS_AND_CENTERS                    = 0x00000002,
UNDEF                                    = 0x00000003,
} SPI_SAMPLE_CNTL;

/*
 * SPI_SHADER_EX_FORMAT enum
 */

typedef enum SPI_SHADER_EX_FORMAT {
SPI_SHADER_ZERO                          = 0x00000000,
SPI_SHADER_32_R                          = 0x00000001,
SPI_SHADER_32_GR                         = 0x00000002,
SPI_SHADER_32_AR                         = 0x00000003,
SPI_SHADER_FP16_ABGR                     = 0x00000004,
SPI_SHADER_UNORM16_ABGR                  = 0x00000005,
SPI_SHADER_SNORM16_ABGR                  = 0x00000006,
SPI_SHADER_UINT16_ABGR                   = 0x00000007,
SPI_SHADER_SINT16_ABGR                   = 0x00000008,
SPI_SHADER_32_ABGR                       = 0x00000009,
} SPI_SHADER_EX_FORMAT;

/*
 * SPI_SHADER_FORMAT enum
 */

typedef enum SPI_SHADER_FORMAT {
SPI_SHADER_NONE                          = 0x00000000,
SPI_SHADER_1COMP                         = 0x00000001,
SPI_SHADER_2COMP                         = 0x00000002,
SPI_SHADER_4COMPRESS                     = 0x00000003,
SPI_SHADER_4COMP                         = 0x00000004,
} SPI_SHADER_FORMAT;

/*******************************************************
 * SQ Enums
 *******************************************************/

/*
 * SH_MEM_ADDRESS_MODE enum
 */

typedef enum SH_MEM_ADDRESS_MODE {
SH_MEM_ADDRESS_MODE_64                   = 0x00000000,
SH_MEM_ADDRESS_MODE_32                   = 0x00000001,
} SH_MEM_ADDRESS_MODE;

/*
 * SH_MEM_ALIGNMENT_MODE enum
 */

typedef enum SH_MEM_ALIGNMENT_MODE {
SH_MEM_ALIGNMENT_MODE_DWORD              = 0x00000000,
SH_MEM_ALIGNMENT_MODE_DWORD_STRICT       = 0x00000001,
SH_MEM_ALIGNMENT_MODE_STRICT             = 0x00000002,
SH_MEM_ALIGNMENT_MODE_UNALIGNED          = 0x00000003,
} SH_MEM_ALIGNMENT_MODE;

/*
 * SQG_PERF_SEL enum
 */

typedef enum SQG_PERF_SEL {
SQG_PERF_SEL_NONE                        = 0x00000000,
SQG_PERF_SEL_MSG_BUS_BUSY                = 0x00000001,
SQG_PERF_SEL_EXP_REQ0_BUS_BUSY           = 0x00000002,
SQG_PERF_SEL_EXP_REQ1_BUS_BUSY           = 0x00000003,
SQG_PERF_SEL_EXP_BUS0_BUSY               = 0x00000004,
SQG_PERF_SEL_EXP_BUS1_BUSY               = 0x00000005,
SQG_PERF_SEL_TTRACE_REQS                 = 0x00000006,
SQG_PERF_SEL_TTRACE_INFLIGHT_REQS        = 0x00000007,
SQG_PERF_SEL_TTRACE_STALL                = 0x00000008,
SQG_PERF_SEL_TTRACE_LOST_PACKETS         = 0x00000009,
SQG_PERF_SEL_WAVES_INITIAL_PREFETCH      = 0x0000000a,
SQG_PERF_SEL_EVENTS                      = 0x0000000b,
SQG_PERF_SEL_WAVES_RESTORED              = 0x0000000c,
SQG_PERF_SEL_WAVES_SAVED                 = 0x0000000d,
SQG_PERF_SEL_ACCUM_PREV                  = 0x0000000e,
SQG_PERF_SEL_CYCLES                      = 0x0000000f,
SQG_PERF_SEL_BUSY_CYCLES                 = 0x00000010,
SQG_PERF_SEL_WAVE_CYCLES                 = 0x00000011,
SQG_PERF_SEL_MSG                         = 0x00000012,
SQG_PERF_SEL_MSG_INTERRUPT               = 0x00000013,
SQG_PERF_SEL_WAVES                       = 0x00000014,
SQG_PERF_SEL_WAVES_32                    = 0x00000015,
SQG_PERF_SEL_WAVES_64                    = 0x00000016,
SQG_PERF_SEL_LEVEL_WAVES                 = 0x00000017,
SQG_PERF_SEL_ITEMS                       = 0x00000018,
SQG_PERF_SEL_WAVE32_ITEMS                = 0x00000019,
SQG_PERF_SEL_WAVE64_ITEMS                = 0x0000001a,
SQG_PERF_SEL_PS_QUADS                    = 0x0000001b,
SQG_PERF_SEL_WAVES_EQ_64                 = 0x0000001c,
SQG_PERF_SEL_WAVES_EQ_32                 = 0x0000001d,
SQG_PERF_SEL_WAVES_LT_64                 = 0x0000001e,
SQG_PERF_SEL_WAVES_LT_48                 = 0x0000001f,
SQG_PERF_SEL_WAVES_LT_32                 = 0x00000020,
SQG_PERF_SEL_WAVES_LT_16                 = 0x00000021,
SQG_PERF_SEL_DUMMY_LAST                  = 0x00000022,
} SQG_PERF_SEL;

/*
 * SQ_CAC_POWER_SEL enum
 */

typedef enum SQ_CAC_POWER_SEL {
SQ_CAC_POWER_VALU                        = 0x00000000,
SQ_CAC_POWER_VALU0                       = 0x00000001,
SQ_CAC_POWER_VALU1                       = 0x00000002,
SQ_CAC_POWER_VALU2                       = 0x00000003,
SQ_CAC_POWER_GPR_RD                      = 0x00000004,
SQ_CAC_POWER_GPR_WR                      = 0x00000005,
SQ_CAC_POWER_LDS_BUSY                    = 0x00000006,
SQ_CAC_POWER_ALU_BUSY                    = 0x00000007,
SQ_CAC_POWER_TEX_BUSY                    = 0x00000008,
} SQ_CAC_POWER_SEL;

/*
 * SQ_EDC_INFO_SOURCE enum
 */

typedef enum SQ_EDC_INFO_SOURCE {
SQ_EDC_INFO_SOURCE_INVALID               = 0x00000000,
SQ_EDC_INFO_SOURCE_INST                  = 0x00000001,
SQ_EDC_INFO_SOURCE_SGPR                  = 0x00000002,
SQ_EDC_INFO_SOURCE_VGPR                  = 0x00000003,
SQ_EDC_INFO_SOURCE_LDS                   = 0x00000004,
SQ_EDC_INFO_SOURCE_GDS                   = 0x00000005,
SQ_EDC_INFO_SOURCE_TA                    = 0x00000006,
} SQ_EDC_INFO_SOURCE;

/*
 * SQ_IBUF_ST enum
 */

typedef enum SQ_IBUF_ST {
SQ_IBUF_IB_IDLE                          = 0x00000000,
SQ_IBUF_IB_INI_WAIT_GNT                  = 0x00000001,
SQ_IBUF_IB_INI_WAIT_DRET                 = 0x00000002,
SQ_IBUF_IB_LE_4DW                        = 0x00000003,
SQ_IBUF_IB_WAIT_DRET                     = 0x00000004,
SQ_IBUF_IB_EMPTY_WAIT_DRET               = 0x00000005,
SQ_IBUF_IB_DRET                          = 0x00000006,
SQ_IBUF_IB_EMPTY_WAIT_GNT                = 0x00000007,
} SQ_IBUF_ST;

/*
 * SQ_IMG_FILTER_TYPE enum
 */

typedef enum SQ_IMG_FILTER_TYPE {
SQ_IMG_FILTER_MODE_BLEND                 = 0x00000000,
SQ_IMG_FILTER_MODE_MIN                   = 0x00000001,
SQ_IMG_FILTER_MODE_MAX                   = 0x00000002,
} SQ_IMG_FILTER_TYPE;

/*
 * SQ_IND_CMD_CMD enum
 */

typedef enum SQ_IND_CMD_CMD {
SQ_IND_CMD_CMD_NULL                      = 0x00000000,
SQ_IND_CMD_CMD_SETHALT                   = 0x00000001,
SQ_IND_CMD_CMD_SAVECTX                   = 0x00000002,
SQ_IND_CMD_CMD_KILL                      = 0x00000003,
SQ_IND_CMD_CMD_TRAP_AFTER_INST           = 0x00000004,
SQ_IND_CMD_CMD_TRAP                      = 0x00000005,
SQ_IND_CMD_CMD_SET_SPI_PRIO              = 0x00000006,
SQ_IND_CMD_CMD_SETFATALHALT              = 0x00000007,
SQ_IND_CMD_CMD_SINGLE_STEP               = 0x00000008,
} SQ_IND_CMD_CMD;

/*
 * SQ_IND_CMD_MODE enum
 */

typedef enum SQ_IND_CMD_MODE {
SQ_IND_CMD_MODE_SINGLE                   = 0x00000000,
SQ_IND_CMD_MODE_BROADCAST                = 0x00000001,
SQ_IND_CMD_MODE_BROADCAST_QUEUE          = 0x00000002,
SQ_IND_CMD_MODE_BROADCAST_PIPE           = 0x00000003,
SQ_IND_CMD_MODE_BROADCAST_ME             = 0x00000004,
} SQ_IND_CMD_MODE;

/*
 * SQ_INST_STR_ST enum
 */

typedef enum SQ_INST_STR_ST {
SQ_INST_STR_IB_WAVE_NORML                = 0x00000000,
SQ_INST_STR_IB_WAVE2ID_NORMAL_INST_AV    = 0x00000001,
SQ_INST_STR_IB_WAVE_INTERNAL_INST_AV     = 0x00000002,
SQ_INST_STR_IB_WAVE_INST_SKIP_AV         = 0x00000003,
SQ_INST_STR_IB_WAVE_NOP_SLEEP_WAIT       = 0x00000004,
SQ_INST_STR_IB_WAVE_PC_FROM_SGPR_MSG_WAIT = 0x00000005,
} SQ_INST_STR_ST;

/*
 * SQ_INST_TYPE enum
 */

typedef enum SQ_INST_TYPE {
SQ_INST_TYPE_VALU                        = 0x00000000,
SQ_INST_TYPE_SCALAR                      = 0x00000001,
SQ_INST_TYPE_TEX                         = 0x00000002,
SQ_INST_TYPE_LDS                         = 0x00000003,
SQ_INST_TYPE_LDS_DIRECT                  = 0x00000004,
SQ_INST_TYPE_EXP                         = 0x00000005,
SQ_INST_TYPE_MSG                         = 0x00000006,
SQ_INST_TYPE_BARRIER                     = 0x00000007,
SQ_INST_TYPE_BRANCH_NOT_TAKEN            = 0x00000008,
SQ_INST_TYPE_BRANCH_TAKEN                = 0x00000009,
SQ_INST_TYPE_JUMP                        = 0x0000000a,
SQ_INST_TYPE_OTHER                       = 0x0000000b,
SQ_INST_TYPE_NONE                        = 0x0000000c,
} SQ_INST_TYPE;

/*
 * SQ_LLC_CTL enum
 */

typedef enum SQ_LLC_CTL {
SQ_LLC_0                                 = 0x00000000,
SQ_LLC_1                                 = 0x00000001,
SQ_LLC_RSVD_2                            = 0x00000002,
SQ_LLC_BYPASS                            = 0x00000003,
} SQ_LLC_CTL;

/*
 * SQ_NO_INST_ISSUE enum
 */

typedef enum SQ_NO_INST_ISSUE {
SQ_NO_INST_ISSUE_NO_INSTS                = 0x00000000,
SQ_NO_INST_ISSUE_ALU_DEP                 = 0x00000001,
SQ_NO_INST_ISSUE_S_WAITCNT               = 0x00000002,
SQ_NO_INST_ISSUE_NO_ARB_WIN              = 0x00000003,
SQ_NO_INST_ISSUE_SLEEP_WAIT              = 0x00000004,
SQ_NO_INST_ISSUE_BARRIER_WAIT            = 0x00000005,
SQ_NO_INST_ISSUE_OTHER                   = 0x00000006,
} SQ_NO_INST_ISSUE;

/*
 * SQ_OOB_SELECT enum
 */

typedef enum SQ_OOB_SELECT {
SQ_OOB_INDEX_AND_OFFSET                  = 0x00000000,
SQ_OOB_INDEX_ONLY                        = 0x00000001,
SQ_OOB_NUM_RECORDS_0                     = 0x00000002,
SQ_OOB_COMPLETE                          = 0x00000003,
} SQ_OOB_SELECT;

/*
 * SQ_PERF_SEL enum
 */

typedef enum SQ_PERF_SEL {
SQ_PERF_SEL_NONE                         = 0x00000000,
SQ_PERF_SEL_ACCUM_PREV                   = 0x00000001,
SQ_PERF_SEL_CYCLES                       = 0x00000002,
SQ_PERF_SEL_BUSY_CYCLES                  = 0x00000003,
SQ_PERF_SEL_WAVES                        = 0x00000004,
SQ_PERF_SEL_WAVES_32                     = 0x00000005,
SQ_PERF_SEL_WAVES_64                     = 0x00000006,
SQ_PERF_SEL_LEVEL_WAVES                  = 0x00000007,
SQ_PERF_SEL_ITEMS                        = 0x00000008,
SQ_PERF_SEL_WAVE32_ITEMS                 = 0x00000009,
SQ_PERF_SEL_WAVE64_ITEMS                 = 0x0000000a,
SQ_PERF_SEL_PS_QUADS                     = 0x0000000b,
SQ_PERF_SEL_EVENTS                       = 0x0000000c,
SQ_PERF_SEL_WAVES_EQ_32                  = 0x0000000d,
SQ_PERF_SEL_WAVES_EQ_64                  = 0x0000000e,
SQ_PERF_SEL_WAVES_LT_64                  = 0x0000000f,
SQ_PERF_SEL_WAVES_LT_48                  = 0x00000010,
SQ_PERF_SEL_WAVES_LT_32                  = 0x00000011,
SQ_PERF_SEL_WAVES_LT_16                  = 0x00000012,
SQ_PERF_SEL_WAVES_RESTORED               = 0x00000013,
SQ_PERF_SEL_WAVES_SAVED                  = 0x00000014,
SQ_PERF_SEL_MSG                          = 0x00000015,
SQ_PERF_SEL_MSG_INTERRUPT                = 0x00000016,
SQ_PERF_SEL_WAVES_INITIAL_PREFETCH       = 0x00000017,
SQ_PERF_SEL_WAVE_CYCLES                  = 0x00000018,
SQ_PERF_SEL_WAVE_READY                   = 0x00000019,
SQ_PERF_SEL_WAIT_INST_ANY                = 0x0000001a,
SQ_PERF_SEL_WAIT_INST_VALU               = 0x0000001b,
SQ_PERF_SEL_WAIT_INST_SCA                = 0x0000001c,
SQ_PERF_SEL_WAIT_INST_LDS                = 0x0000001d,
SQ_PERF_SEL_WAIT_INST_TEX                = 0x0000001e,
SQ_PERF_SEL_WAIT_INST_FLAT               = 0x0000001f,
SQ_PERF_SEL_WAIT_INST_VMEM               = 0x00000020,
SQ_PERF_SEL_WAIT_INST_EXP_GDS            = 0x00000021,
SQ_PERF_SEL_WAIT_INST_BR_MSG             = 0x00000022,
SQ_PERF_SEL_WAIT_ANY                     = 0x00000023,
SQ_PERF_SEL_WAIT_CNT_ANY                 = 0x00000024,
SQ_PERF_SEL_WAIT_CNT_VMVS                = 0x00000025,
SQ_PERF_SEL_WAIT_CNT_LGKM                = 0x00000026,
SQ_PERF_SEL_WAIT_CNT_EXP                 = 0x00000027,
SQ_PERF_SEL_WAIT_TTRACE                  = 0x00000028,
SQ_PERF_SEL_WAIT_IFETCH                  = 0x00000029,
SQ_PERF_SEL_WAIT_BARRIER                 = 0x0000002a,
SQ_PERF_SEL_WAIT_EXP_ALLOC               = 0x0000002b,
SQ_PERF_SEL_WAIT_SLEEP                   = 0x0000002c,
SQ_PERF_SEL_WAIT_DELAY_ALU               = 0x0000002d,
SQ_PERF_SEL_WAIT_DEPCTR                  = 0x0000002e,
SQ_PERF_SEL_WAIT_OTHER                   = 0x0000002f,
SQ_PERF_SEL_INSTS_ALL                    = 0x00000030,
SQ_PERF_SEL_INSTS_BRANCH                 = 0x00000031,
SQ_PERF_SEL_INSTS_CBRANCH_NOT_TAKEN      = 0x00000032,
SQ_PERF_SEL_INSTS_CBRANCH_TAKEN          = 0x00000033,
SQ_PERF_SEL_INSTS_CBRANCH_TAKEN_HIT_IS   = 0x00000034,
SQ_PERF_SEL_INSTS_EXP_GDS                = 0x00000035,
SQ_PERF_SEL_INSTS_GDS                    = 0x00000036,
SQ_PERF_SEL_INSTS_EXP                    = 0x00000037,
SQ_PERF_SEL_INSTS_FLAT                   = 0x00000038,
SQ_PERF_SEL_INSTS_LDS                    = 0x00000039,
SQ_PERF_SEL_INSTS_SALU                   = 0x0000003a,
SQ_PERF_SEL_INSTS_SMEM                   = 0x0000003b,
SQ_PERF_SEL_INSTS_SMEM_NORM              = 0x0000003c,
SQ_PERF_SEL_INSTS_SENDMSG                = 0x0000003d,
SQ_PERF_SEL_INSTS_VALU                   = 0x0000003e,
SQ_PERF_SEL_INSTS_VALU_TRANS32           = 0x0000003f,
SQ_PERF_SEL_INSTS_VALU_NO_COEXEC         = 0x00000040,
SQ_PERF_SEL_INSTS_TEX                    = 0x00000041,
SQ_PERF_SEL_INSTS_TEX_LOAD               = 0x00000042,
SQ_PERF_SEL_INSTS_TEX_STORE              = 0x00000043,
SQ_PERF_SEL_INSTS_DELAY_ALU              = 0x00000044,
SQ_PERF_SEL_INSTS_INTERNAL               = 0x00000045,
SQ_PERF_SEL_INSTS_WAVE32                 = 0x00000046,
SQ_PERF_SEL_INSTS_WAVE32_FLAT            = 0x00000047,
SQ_PERF_SEL_INSTS_WAVE32_LDS             = 0x00000048,
SQ_PERF_SEL_INSTS_WAVE32_VALU            = 0x00000049,
SQ_PERF_SEL_WAVE32_INSTS_EXP_GDS         = 0x0000004a,
SQ_PERF_SEL_INSTS_WAVE32_VALU_TRANS32    = 0x0000004b,
SQ_PERF_SEL_INSTS_WAVE32_VALU_NO_COEXEC  = 0x0000004c,
SQ_PERF_SEL_INSTS_WAVE32_TEX             = 0x0000004d,
SQ_PERF_SEL_INSTS_WAVE32_TEX_LOAD        = 0x0000004e,
SQ_PERF_SEL_INSTS_WAVE32_TEX_STORE       = 0x0000004f,
SQ_PERF_SEL_ITEM_CYCLES_VALU             = 0x00000050,
SQ_PERF_SEL_VALU_READWRITELANE_CYCLES    = 0x00000051,
SQ_PERF_SEL_WAVE32_INSTS                 = 0x00000052,
SQ_PERF_SEL_WAVE64_INSTS                 = 0x00000053,
SQ_PERF_SEL_INSTS_VALU_EXEC_SKIPPED      = 0x00000054,
SQ_PERF_SEL_WAVE64_HALF_SKIP             = 0x00000055,
SQ_PERF_SEL_INST_LEVEL_EXP               = 0x00000056,
SQ_PERF_SEL_INST_LEVEL_GDS               = 0x00000057,
SQ_PERF_SEL_INST_LEVEL_LDS               = 0x00000058,
SQ_PERF_SEL_INST_LEVEL_SMEM              = 0x00000059,
SQ_PERF_SEL_INST_LEVEL_TEX_LOAD          = 0x0000005a,
SQ_PERF_SEL_INST_LEVEL_TEX_STORE         = 0x0000005b,
SQ_PERF_SEL_IFETCH_REQS                  = 0x0000005c,
SQ_PERF_SEL_IFETCH_LEVEL                 = 0x0000005d,
SQ_PERF_SEL_LDS_DIRECT_CMD_FIFO_FULL_STALL = 0x0000005e,
SQ_PERF_SEL_VALU_SGATHER_STALL           = 0x0000005f,
SQ_PERF_SEL_VALU_FWD_BUFFER_FULL_STALL   = 0x00000060,
SQ_PERF_SEL_VALU_SGPR_RD_FIFO_FULL_STALL = 0x00000061,
SQ_PERF_SEL_VALU_SGATHER_FULL_STALL      = 0x00000062,
SQ_PERF_SEL_SALU_SGATHER_STALL           = 0x00000063,
SQ_PERF_SEL_SALU_SGPR_RD_FIFO_FULL_STALL = 0x00000064,
SQ_PERF_SEL_SALU_GATHER_FULL_STALL       = 0x00000065,
SQ_PERF_SEL_SMEM_DCACHE_FIFO_FULL_STALL  = 0x00000066,
SQ_PERF_SEL_INST_CYCLES_VALU             = 0x00000067,
SQ_PERF_SEL_INST_CYCLES_VALU_TRANS32     = 0x00000068,
SQ_PERF_SEL_INST_CYCLES_VALU_NO_COEXEC   = 0x00000069,
SQ_PERF_SEL_INST_CYCLES_VMEM             = 0x0000006a,
SQ_PERF_SEL_INST_CYCLES_VMEM_LOAD        = 0x0000006b,
SQ_PERF_SEL_INST_CYCLES_VMEM_STORE       = 0x0000006c,
SQ_PERF_SEL_INST_CYCLES_LDS              = 0x0000006d,
SQ_PERF_SEL_INST_CYCLES_TEX              = 0x0000006e,
SQ_PERF_SEL_INST_CYCLES_FLAT             = 0x0000006f,
SQ_PERF_SEL_INST_CYCLES_EXP_GDS          = 0x00000070,
SQ_PERF_SEL_INST_CYCLES_EXP              = 0x00000071,
SQ_PERF_SEL_INST_CYCLES_GDS              = 0x00000072,
SQ_PERF_SEL_VALU_STARVE                  = 0x00000073,
SQ_PERF_SEL_VMEM_ARB_FIFO_FULL           = 0x00000074,
SQ_PERF_SEL_MSG_FIFO_FULL_STALL          = 0x00000075,
SQ_PERF_SEL_EXP_REQ_FIFO_FULL            = 0x00000076,
SQ_PERF_SEL_VMEM_BUS_ACTIVE              = 0x00000077,
SQ_PERF_SEL_VMEM_BUS_STALL               = 0x00000078,
SQ_PERF_SEL_VMEM_BUS_STALL_TA_ADDR_FIFO_FULL = 0x00000079,
SQ_PERF_SEL_VMEM_BUS_STALL_TA_CMD_FIFO_FULL = 0x0000007a,
SQ_PERF_SEL_VMEM_BUS_STALL_LDS_ADDR_FIFO_FULL = 0x0000007b,
SQ_PERF_SEL_VMEM_BUS_STALL_LDS_CMD_FIFO_FULL = 0x0000007c,
SQ_PERF_SEL_VMEM_STARVE_TA_ADDR_EMPTY    = 0x0000007d,
SQ_PERF_SEL_VMEM_STARVE_LDS_ADDR_EMPTY   = 0x0000007e,
SQ_PERF_SEL_SALU_PIPE_STALL              = 0x0000007f,
SQ_PERF_SEL_SMEM_DCACHE_RETURN_CYCLES    = 0x00000080,
SQ_PERF_SEL_MSG_BUS_BUSY                 = 0x00000081,
SQ_PERF_SEL_EXP_REQ_BUS_STALL            = 0x00000082,
SQ_PERF_SEL_EXP_REQ0_BUS_BUSY            = 0x00000083,
SQ_PERF_SEL_EXP_REQ1_BUS_BUSY            = 0x00000084,
SQ_PERF_SEL_EXP_BUS0_BUSY                = 0x00000085,
SQ_PERF_SEL_EXP_BUS1_BUSY                = 0x00000086,
SQ_PERF_SEL_INST_CACHE_REQ_STALL         = 0x00000087,
SQ_PERF_SEL_USER0                        = 0x00000088,
SQ_PERF_SEL_USER1                        = 0x00000089,
SQ_PERF_SEL_USER2                        = 0x0000008a,
SQ_PERF_SEL_USER3                        = 0x0000008b,
SQ_PERF_SEL_USER4                        = 0x0000008c,
SQ_PERF_SEL_USER5                        = 0x0000008d,
SQ_PERF_SEL_USER6                        = 0x0000008e,
SQ_PERF_SEL_USER7                        = 0x0000008f,
SQ_PERF_SEL_USER8                        = 0x00000090,
SQ_PERF_SEL_USER9                        = 0x00000091,
SQ_PERF_SEL_USER10                       = 0x00000092,
SQ_PERF_SEL_USER11                       = 0x00000093,
SQ_PERF_SEL_USER12                       = 0x00000094,
SQ_PERF_SEL_USER13                       = 0x00000095,
SQ_PERF_SEL_USER14                       = 0x00000096,
SQ_PERF_SEL_USER15                       = 0x00000097,
SQ_PERF_SEL_USER_LEVEL0                  = 0x00000098,
SQ_PERF_SEL_USER_LEVEL1                  = 0x00000099,
SQ_PERF_SEL_USER_LEVEL2                  = 0x0000009a,
SQ_PERF_SEL_USER_LEVEL3                  = 0x0000009b,
SQ_PERF_SEL_USER_LEVEL4                  = 0x0000009c,
SQ_PERF_SEL_USER_LEVEL5                  = 0x0000009d,
SQ_PERF_SEL_USER_LEVEL6                  = 0x0000009e,
SQ_PERF_SEL_USER_LEVEL7                  = 0x0000009f,
SQ_PERF_SEL_USER_LEVEL8                  = 0x000000a0,
SQ_PERF_SEL_USER_LEVEL9                  = 0x000000a1,
SQ_PERF_SEL_USER_LEVEL10                 = 0x000000a2,
SQ_PERF_SEL_USER_LEVEL11                 = 0x000000a3,
SQ_PERF_SEL_USER_LEVEL12                 = 0x000000a4,
SQ_PERF_SEL_USER_LEVEL13                 = 0x000000a5,
SQ_PERF_SEL_USER_LEVEL14                 = 0x000000a6,
SQ_PERF_SEL_USER_LEVEL15                 = 0x000000a7,
SQ_PERF_SEL_VALU_RETURN_SDST             = 0x000000a8,
SQ_PERF_SEL_VMEM_VGPR_READ_STALLED_BY_EXPORT = 0x000000a9,
SQ_PERF_SEL_INSTS_VALU_TRANS             = 0x000000aa,
SQ_PERF_SEL_INSTS_LDS_DIRECT_LOAD        = 0x000000ab,
SQ_PERF_SEL_INSTS_LDS_PARAM_LOAD         = 0x000000ac,
SQ_PERF_SEL_INSTS_WAVE32_LDS_PARAM_LOAD  = 0x000000ad,
SQ_PERF_SEL_INSTS_VALU_ONE_CYCLE_WAVE64  = 0x000000ae,
SQ_PERF_SEL_INSTS_VALU_VINTERP           = 0x000000af,
SQ_PERF_SEL_INSTS_VALU_WAVE32_VINTERP    = 0x000000b0,
SQ_PERF_SEL_OVERFLOW_PREV                = 0x000000b1,
SQ_PERF_SEL_INSTS_DUAL_VALU_WAVE32       = 0x000000b2,
SQ_PERF_SEL_INSTS_VALU_1_PASS            = 0x000000b3,
SQ_PERF_SEL_INSTS_VALU_2_PASS            = 0x000000b4,
SQ_PERF_SEL_INSTS_VALU_4_PASS            = 0x000000b5,
SQ_PERF_SEL_INSTS_VALU_DP                = 0x000000b6,
SQ_PERF_SEL_SP_CONST_CYCLES              = 0x000000b7,
SQ_PERF_SEL_SP_CONST_STALL_CYCLES        = 0x000000b8,
SQ_PERF_SEL_ITEMS_VALU                   = 0x000000b9,
SQ_PERF_SEL_ITEMS_MAX_VALU               = 0x000000ba,
SQ_PERF_SEL_ITEM_CYCLES_VMEM             = 0x000000bb,
SQ_PERF_SEL_DUMMY_END                    = 0x000000bc,
SQ_PERF_SEL_DUMMY_LAST                   = 0x000000ff,
SQC_PERF_SEL_LDS_BANK_CONFLICT           = 0x00000100,
SQC_PERF_SEL_LDS_ADDR_CONFLICT           = 0x00000101,
SQC_PERF_SEL_LDS_UNALIGNED_STALL         = 0x00000102,
SQC_PERF_SEL_LDS_MEM_VIOLATIONS          = 0x00000103,
SQC_PERF_SEL_LDS_ATOMIC_RETURN           = 0x00000104,
SQC_PERF_SEL_LDS_IDX_ACTIVE              = 0x00000105,
SQC_PERF_SEL_LDS_ADDR_STALL              = 0x00000106,
SQC_PERF_SEL_LDS_ADDR_ACTIVE             = 0x00000107,
SQC_PERF_SEL_LDS_PC_LDS_WRITE_STALL_TD   = 0x00000108,
SQC_PERF_SEL_LDS_SPI_VGPR_WRITE_STALL_TD = 0x00000109,
SQC_PERF_SEL_LDS_LDS_VGPR_WRITE_STALL    = 0x0000010a,
SQC_PERF_SEL_LDS_FP_ADD_CYCLES           = 0x0000010b,
SQC_PERF_SEL_ICACHE_BUSY_CYCLES          = 0x0000010c,
SQC_PERF_SEL_ICACHE_REQ                  = 0x0000010d,
SQC_PERF_SEL_ICACHE_HITS                 = 0x0000010e,
SQC_PERF_SEL_ICACHE_MISSES               = 0x0000010f,
SQC_PERF_SEL_ICACHE_MISSES_DUPLICATE     = 0x00000110,
SQC_PERF_SEL_ICACHE_INVAL_INST           = 0x00000111,
SQC_PERF_SEL_ICACHE_INVAL_ASYNC          = 0x00000112,
SQC_PERF_SEL_ICACHE_INFLIGHT_LEVEL       = 0x00000113,
SQC_PERF_SEL_DCACHE_INFLIGHT_LEVEL       = 0x00000114,
SQC_PERF_SEL_TC_INFLIGHT_LEVEL           = 0x00000115,
SQC_PERF_SEL_ICACHE_TC_INFLIGHT_LEVEL    = 0x00000116,
SQC_PERF_SEL_DCACHE_TC_INFLIGHT_LEVEL    = 0x00000117,
SQC_PERF_SEL_ICACHE_INPUT_VALID_READYB   = 0x00000118,
SQC_PERF_SEL_DCACHE_INPUT_VALID_READYB   = 0x00000119,
SQC_PERF_SEL_TC_REQ                      = 0x0000011a,
SQC_PERF_SEL_TC_INST_REQ                 = 0x0000011b,
SQC_PERF_SEL_TC_DATA_READ_REQ            = 0x0000011c,
SQC_PERF_SEL_TC_STALL                    = 0x0000011d,
SQC_PERF_SEL_TC_STARVE                   = 0x0000011e,
SQC_PERF_SEL_ICACHE_INPUT_STALL_ARB_NO_GRANT = 0x0000011f,
SQC_PERF_SEL_ICACHE_INPUT_STALL_BANK_READYB = 0x00000120,
SQC_PERF_SEL_ICACHE_CACHE_STALLED        = 0x00000121,
SQC_PERF_SEL_ICACHE_CACHE_STALL_INFLIGHT_MAX = 0x00000122,
SQC_PERF_SEL_ICACHE_STALL_OUTXBAR_ARB_NO_GRANT = 0x00000123,
SQC_PERF_SEL_DCACHE_BUSY_CYCLES          = 0x00000124,
SQC_PERF_SEL_DCACHE_REQ                  = 0x00000125,
SQC_PERF_SEL_DCACHE_HITS                 = 0x00000126,
SQC_PERF_SEL_DCACHE_MISSES               = 0x00000127,
SQC_PERF_SEL_DCACHE_MISSES_DUPLICATE     = 0x00000128,
SQC_PERF_SEL_DCACHE_INVAL_INST           = 0x00000129,
SQC_PERF_SEL_DCACHE_INVAL_ASYNC          = 0x0000012a,
SQC_PERF_SEL_DCACHE_HIT_LRU_READ         = 0x0000012b,
SQC_PERF_SEL_DCACHE_INPUT_STALL_ARB_NO_GRANT = 0x0000012c,
SQC_PERF_SEL_DCACHE_INPUT_STALL_BANK_READYB = 0x0000012d,
SQC_PERF_SEL_DCACHE_CACHE_STALLED        = 0x0000012e,
SQC_PERF_SEL_DCACHE_CACHE_STALL_INFLIGHT_MAX = 0x0000012f,
SQC_PERF_SEL_DCACHE_CACHE_STALL_OUTPUT   = 0x00000130,
SQC_PERF_SEL_DCACHE_STALL_OUTXBAR_ARB_NO_GRANT = 0x00000131,
SQC_PERF_SEL_DCACHE_REQ_READ_1           = 0x00000132,
SQC_PERF_SEL_DCACHE_REQ_READ_2           = 0x00000133,
SQC_PERF_SEL_DCACHE_REQ_READ_4           = 0x00000134,
SQC_PERF_SEL_DCACHE_REQ_READ_8           = 0x00000135,
SQC_PERF_SEL_DCACHE_REQ_READ_16          = 0x00000136,
SQC_PERF_SEL_DCACHE_REQ_ATC_PROBE        = 0x00000137,
SQC_PERF_SEL_SQ_DCACHE_REQS              = 0x00000138,
SQC_PERF_SEL_DCACHE_FLAT_REQ             = 0x00000139,
SQC_PERF_SEL_TD_VGPR_BUSY                = 0x0000013a,
SQC_PERF_SEL_LDS_VGPR_BUSY               = 0x0000013b,
SQC_PERF_SEL_LDS_TD_VGPR_CONF_STALL      = 0x0000013c,
SQC_PERF_SEL_ICACHE_GCR                  = 0x0000013d,
SQC_PERF_SEL_ICACHE_GCR_HITS             = 0x0000013e,
SQC_PERF_SEL_DCACHE_GCR                  = 0x0000013f,
SQC_PERF_SEL_DCACHE_GCR_HITS             = 0x00000140,
SQC_PERF_SEL_ICACHE_GCR_INVALIDATE       = 0x00000141,
SQC_PERF_SEL_DCACHE_GCR_INVALIDATE       = 0x00000142,
SQC_PERF_SEL_DCACHE_SPI_RETURN_STALL     = 0x00000143,
SQC_PERF_SEL_DUMMY_LAST                  = 0x00000144,
SP_PERF_SEL_DST_BUF_ALLOC_STALL          = 0x000001c0,
SP_PERF_SEL_DST_BUF_WB_CONF_W_TD_LDS     = 0x000001c1,
SP_PERF_SEL_DST_BUF_WB_CONF_W_SPI        = 0x000001c2,
SP_PERF_SEL_DST_BUF_EVEN_DIRTY           = 0x000001c3,
SP_PERF_SEL_DST_BUF_ODD_DIRTY            = 0x000001c4,
SP_PERF_SEL_SRC_CACHE_HIT_B0             = 0x000001c5,
SP_PERF_SEL_SRC_CACHE_HIT_B1             = 0x000001c6,
SP_PERF_SEL_SRC_CACHE_HIT_B2             = 0x000001c7,
SP_PERF_SEL_SRC_CACHE_HIT_B3             = 0x000001c8,
SP_PERF_SEL_SRC_CACHE_PROBE_B0           = 0x000001c9,
SP_PERF_SEL_SRC_CACHE_PROBE_B1           = 0x000001ca,
SP_PERF_SEL_SRC_CACHE_PROBE_B2           = 0x000001cb,
SP_PERF_SEL_SRC_CACHE_PROBE_B3           = 0x000001cc,
SP_PERF_SEL_SRC_CACHE_VGPR_RD_B0         = 0x000001cd,
SP_PERF_SEL_SRC_CACHE_VGPR_RD_B1         = 0x000001ce,
SP_PERF_SEL_SRC_CACHE_VGPR_RD_B2         = 0x000001cf,
SP_PERF_SEL_SRC_CACHE_VGPR_RD_B3         = 0x000001d0,
SP_PERF_SEL_SRC_CACHE_RECYCLE_HIT_B0     = 0x000001d1,
SP_PERF_SEL_SRC_CACHE_RECYCLE_HIT_B1     = 0x000001d2,
SP_PERF_SEL_SRC_CACHE_RECYCLE_HIT_B2     = 0x000001d3,
SP_PERF_SEL_SRC_CACHE_RECYCLE_HIT_B3     = 0x000001d4,
SP_PERF_SEL_SRC_CACHE_PROBE_SUCCESS_B0   = 0x000001d5,
SP_PERF_SEL_SRC_CACHE_PROBE_SUCCESS_B1   = 0x000001d6,
SP_PERF_SEL_SRC_CACHE_PROBE_SUCCESS_B2   = 0x000001d7,
SP_PERF_SEL_SRC_CACHE_PROBE_SUCCESS_B3   = 0x000001d8,
SP_PERF_SEL_VALU_PENDING_QUEUE_STALL     = 0x000001d9,
SP_PERF_SEL_VALU_OPERAND                 = 0x000001da,
SP_PERF_SEL_VALU_VGPR_OPERAND            = 0x000001db,
SP_PERF_SEL_VALU_OPERAND_FROM_DST_BUF    = 0x000001dc,
SP_PERF_SEL_VALU_EXEC_MASK_CHANGE        = 0x000001dd,
SP_PERF_SEL_VALU_COEXEC_WITH_TRANS       = 0x000001de,
SP_PERF_SEL_VALU_SGPR_FWD_BUF_FULL       = 0x000001df,
SP_PERF_SEL_VALU_STALL                   = 0x000001e0,
SP_PERF_SEL_VALU_STALL_VGPR_NOT_READY    = 0x000001e1,
SP_PERF_SEL_VALU_STALL_SGPR_NOT_READY    = 0x000001e2,
SP_PERF_SEL_VALU_STALL_VDST_FWD          = 0x000001e3,
SP_PERF_SEL_VALU_STALL_SDST_FWD          = 0x000001e4,
SP_PERF_SEL_VALU_STALL_DST_STALL         = 0x000001e5,
SP_PERF_SEL_VALU_FAST_OP_STALL_VGPR_NOT_READY = 0x000001e6,
SP_PERF_SEL_VGPR_VMEM_RD                 = 0x000001e7,
SP_PERF_SEL_VGPR_EXP_RD                  = 0x000001e8,
SP_PERF_SEL_VGPR_SPI_WR                  = 0x000001e9,
SP_PERF_SEL_VGPR_TDLDS_DATA_WR           = 0x000001ea,
SP_PERF_SEL_VGPR_WR                      = 0x000001eb,
SP_PERF_SEL_VGPR_RD                      = 0x000001ec,
SP_PERF_SEL_DUMMY_LAST                   = 0x000001ed,
SQ_PERF_SEL_NONE2                        = 0x000001ff,
} SQ_PERF_SEL;

/*
 * SQ_ROUND_MODE enum
 */

typedef enum SQ_ROUND_MODE {
SQ_ROUND_NEAREST_EVEN                    = 0x00000000,
SQ_ROUND_PLUS_INFINITY                   = 0x00000001,
SQ_ROUND_MINUS_INFINITY                  = 0x00000002,
SQ_ROUND_TO_ZERO                         = 0x00000003,
} SQ_ROUND_MODE;

/*
 * SQ_RSRC_BUF_TYPE enum
 */

typedef enum SQ_RSRC_BUF_TYPE {
SQ_RSRC_BUF                              = 0x00000000,
SQ_RSRC_BUF_RSVD_1                       = 0x00000001,
SQ_RSRC_BUF_RSVD_2                       = 0x00000002,
SQ_RSRC_BUF_RSVD_3                       = 0x00000003,
} SQ_RSRC_BUF_TYPE;

/*
 * SQ_RSRC_FLAT_TYPE enum
 */

typedef enum SQ_RSRC_FLAT_TYPE {
SQ_RSRC_FLAT_RSVD_0                      = 0x00000000,
SQ_RSRC_FLAT                             = 0x00000001,
SQ_RSRC_FLAT_RSVD_2                      = 0x00000002,
SQ_RSRC_FLAT_RSVD_3                      = 0x00000003,
} SQ_RSRC_FLAT_TYPE;

/*
 * SQ_RSRC_IMG_TYPE enum
 */

typedef enum SQ_RSRC_IMG_TYPE {
SQ_RSRC_IMG_RSVD_0                       = 0x00000000,
SQ_RSRC_IMG_RSVD_1                       = 0x00000001,
SQ_RSRC_IMG_RSVD_2                       = 0x00000002,
SQ_RSRC_IMG_RSVD_3                       = 0x00000003,
SQ_RSRC_IMG_RSVD_4                       = 0x00000004,
SQ_RSRC_IMG_RSVD_5                       = 0x00000005,
SQ_RSRC_IMG_RSVD_6                       = 0x00000006,
SQ_RSRC_IMG_RSVD_7                       = 0x00000007,
SQ_RSRC_IMG_1D                           = 0x00000008,
SQ_RSRC_IMG_2D                           = 0x00000009,
SQ_RSRC_IMG_3D                           = 0x0000000a,
SQ_RSRC_IMG_CUBE                         = 0x0000000b,
SQ_RSRC_IMG_1D_ARRAY                     = 0x0000000c,
SQ_RSRC_IMG_2D_ARRAY                     = 0x0000000d,
SQ_RSRC_IMG_2D_MSAA                      = 0x0000000e,
SQ_RSRC_IMG_2D_MSAA_ARRAY                = 0x0000000f,
} SQ_RSRC_IMG_TYPE;

/*
 * SQ_SEL_XYZW01 enum
 */

typedef enum SQ_SEL_XYZW01 {
SQ_SEL_0                                 = 0x00000000,
SQ_SEL_1                                 = 0x00000001,
SQ_SEL_N_BC_1                            = 0x00000002,
SQ_SEL_RESERVED_1                        = 0x00000003,
SQ_SEL_X                                 = 0x00000004,
SQ_SEL_Y                                 = 0x00000005,
SQ_SEL_Z                                 = 0x00000006,
SQ_SEL_W                                 = 0x00000007,
} SQ_SEL_XYZW01;

/*
 * SQ_TEX_ANISO_RATIO enum
 */

typedef enum SQ_TEX_ANISO_RATIO {
SQ_TEX_ANISO_RATIO_1                     = 0x00000000,
SQ_TEX_ANISO_RATIO_2                     = 0x00000001,
SQ_TEX_ANISO_RATIO_4                     = 0x00000002,
SQ_TEX_ANISO_RATIO_8                     = 0x00000003,
SQ_TEX_ANISO_RATIO_16                    = 0x00000004,
} SQ_TEX_ANISO_RATIO;

/*
 * SQ_TEX_BORDER_COLOR enum
 */

typedef enum SQ_TEX_BORDER_COLOR {
SQ_TEX_BORDER_COLOR_TRANS_BLACK          = 0x00000000,
SQ_TEX_BORDER_COLOR_OPAQUE_BLACK         = 0x00000001,
SQ_TEX_BORDER_COLOR_OPAQUE_WHITE         = 0x00000002,
SQ_TEX_BORDER_COLOR_REGISTER             = 0x00000003,
} SQ_TEX_BORDER_COLOR;

/*
 * SQ_TEX_CLAMP enum
 */

typedef enum SQ_TEX_CLAMP {
SQ_TEX_WRAP                              = 0x00000000,
SQ_TEX_MIRROR                            = 0x00000001,
SQ_TEX_CLAMP_LAST_TEXEL                  = 0x00000002,
SQ_TEX_MIRROR_ONCE_LAST_TEXEL            = 0x00000003,
SQ_TEX_CLAMP_HALF_BORDER                 = 0x00000004,
SQ_TEX_MIRROR_ONCE_HALF_BORDER           = 0x00000005,
SQ_TEX_CLAMP_BORDER                      = 0x00000006,
SQ_TEX_MIRROR_ONCE_BORDER                = 0x00000007,
} SQ_TEX_CLAMP;

/*
 * SQ_TEX_DEPTH_COMPARE enum
 */

typedef enum SQ_TEX_DEPTH_COMPARE {
SQ_TEX_DEPTH_COMPARE_NEVER               = 0x00000000,
SQ_TEX_DEPTH_COMPARE_LESS                = 0x00000001,
SQ_TEX_DEPTH_COMPARE_EQUAL               = 0x00000002,
SQ_TEX_DEPTH_COMPARE_LESSEQUAL           = 0x00000003,
SQ_TEX_DEPTH_COMPARE_GREATER             = 0x00000004,
SQ_TEX_DEPTH_COMPARE_NOTEQUAL            = 0x00000005,
SQ_TEX_DEPTH_COMPARE_GREATEREQUAL        = 0x00000006,
SQ_TEX_DEPTH_COMPARE_ALWAYS              = 0x00000007,
} SQ_TEX_DEPTH_COMPARE;

/*
 * SQ_TEX_MIP_FILTER enum
 */

typedef enum SQ_TEX_MIP_FILTER {
SQ_TEX_MIP_FILTER_NONE                   = 0x00000000,
SQ_TEX_MIP_FILTER_POINT                  = 0x00000001,
SQ_TEX_MIP_FILTER_LINEAR                 = 0x00000002,
SQ_TEX_MIP_FILTER_POINT_ANISO_ADJ        = 0x00000003,
} SQ_TEX_MIP_FILTER;

/*
 * SQ_TEX_XY_FILTER enum
 */

typedef enum SQ_TEX_XY_FILTER {
SQ_TEX_XY_FILTER_POINT                   = 0x00000000,
SQ_TEX_XY_FILTER_BILINEAR                = 0x00000001,
SQ_TEX_XY_FILTER_ANISO_POINT             = 0x00000002,
SQ_TEX_XY_FILTER_ANISO_BILINEAR          = 0x00000003,
} SQ_TEX_XY_FILTER;

/*
 * SQ_TEX_Z_FILTER enum
 */

typedef enum SQ_TEX_Z_FILTER {
SQ_TEX_Z_FILTER_NONE                     = 0x00000000,
SQ_TEX_Z_FILTER_POINT                    = 0x00000001,
SQ_TEX_Z_FILTER_LINEAR                   = 0x00000002,
} SQ_TEX_Z_FILTER;

/*
 * SQ_TT_MODE enum
 */

typedef enum SQ_TT_MODE {
SQ_TT_MODE_OFF                           = 0x00000000,
SQ_TT_MODE_ON                            = 0x00000001,
SQ_TT_MODE_GLOBAL                        = 0x00000002,
SQ_TT_MODE_DETAIL                        = 0x00000003,
} SQ_TT_MODE;

/*
 * SQ_TT_RT_FREQ enum
 */

typedef enum SQ_TT_RT_FREQ {
SQ_TT_RT_FREQ_NEVER                      = 0x00000000,
SQ_TT_RT_FREQ_1024_CLK                   = 0x00000001,
SQ_TT_RT_FREQ_4096_CLK                   = 0x00000002,
} SQ_TT_RT_FREQ;

/*
 * SQ_TT_TOKEN_MASK_INST_EXCLUDE enum
 */

typedef enum SQ_TT_TOKEN_MASK_INST_EXCLUDE {
SQ_TT_INST_EXCLUDE_VMEM_OTHER_SIMD_BIT   = 0x00000001,
SQ_TT_INST_EXCLUDE_EXPGNT234_BIT         = 0x00000002,
} SQ_TT_TOKEN_MASK_INST_EXCLUDE;

/*
 * SQ_TT_TOKEN_MASK_INST_EXCLUDE_SHIFT enum
 */

typedef enum SQ_TT_TOKEN_MASK_INST_EXCLUDE_SHIFT {
SQ_TT_INST_EXCLUDE_VMEM_OTHER_SIMD_SHIFT = 0x00000000,
SQ_TT_INST_EXCLUDE_EXPGNT234_SHIFT       = 0x00000001,
} SQ_TT_TOKEN_MASK_INST_EXCLUDE_SHIFT;

/*
 * SQ_TT_TOKEN_MASK_REG_EXCLUDE enum
 */

typedef enum SQ_TT_TOKEN_MASK_REG_EXCLUDE {
SQ_TT_REG_EXCLUDE_USER_DATA_BIT          = 0x00000001,
SQ_TT_REG_EXCLUDE_CP_ME_MC_RADDR_BIT     = 0x00000002,
SQ_TT_REG_EXCLUDE_GRBM_COMPUTE_EXCLUDE_BIT = 0x00000004,
} SQ_TT_TOKEN_MASK_REG_EXCLUDE;

/*
 * SQ_TT_TOKEN_MASK_REG_EXCLUDE_SHIFT enum
 */

typedef enum SQ_TT_TOKEN_MASK_REG_EXCLUDE_SHIFT {
SQ_TT_REG_EXCLUDE_USER_DATA_SHIFT        = 0x00000000,
SQ_TT_REG_EXCLUDE_CP_ME_MC_RADDR_SHIFT   = 0x00000001,
SQ_TT_REG_EXCLUDE_GRBM_COMPUTE_EXCLUDE_SHIFT = 0x00000002,
} SQ_TT_TOKEN_MASK_REG_EXCLUDE_SHIFT;

/*
 * SQ_TT_TOKEN_MASK_REG_INCLUDE enum
 */

typedef enum SQ_TT_TOKEN_MASK_REG_INCLUDE {
SQ_TT_TOKEN_MASK_SQDEC_BIT               = 0x00000001,
SQ_TT_TOKEN_MASK_SHDEC_BIT               = 0x00000002,
SQ_TT_TOKEN_MASK_GFXUDEC_BIT             = 0x00000004,
SQ_TT_TOKEN_MASK_COMP_BIT                = 0x00000008,
SQ_TT_TOKEN_MASK_CONTEXT_BIT             = 0x00000010,
SQ_TT_TOKEN_MASK_CONFIG_BIT              = 0x00000020,
SQ_TT_TOKEN_MASK_ALL_BIT                 = 0x00000040,
SQ_TT_TOKEN_MASK_RSVD_BIT                = 0x00000080,
} SQ_TT_TOKEN_MASK_REG_INCLUDE;

/*
 * SQ_TT_TOKEN_MASK_REG_INCLUDE_SHIFT enum
 */

typedef enum SQ_TT_TOKEN_MASK_REG_INCLUDE_SHIFT {
SQ_TT_TOKEN_MASK_SQDEC_SHIFT             = 0x00000000,
SQ_TT_TOKEN_MASK_SHDEC_SHIFT             = 0x00000001,
SQ_TT_TOKEN_MASK_GFXUDEC_SHIFT           = 0x00000002,
SQ_TT_TOKEN_MASK_COMP_SHIFT              = 0x00000003,
SQ_TT_TOKEN_MASK_CONTEXT_SHIFT           = 0x00000004,
SQ_TT_TOKEN_MASK_CONFIG_SHIFT            = 0x00000005,
SQ_TT_TOKEN_MASK_ALL_SHIFT               = 0x00000006,
SQ_TT_TOKEN_MASK_RSVD_SHIFT              = 0x00000007,
} SQ_TT_TOKEN_MASK_REG_INCLUDE_SHIFT;

/*
 * SQ_TT_TOKEN_MASK_TOKEN_EXCLUDE_SHIFT enum
 */

typedef enum SQ_TT_TOKEN_MASK_TOKEN_EXCLUDE_SHIFT {
SQ_TT_TOKEN_EXCLUDE_VMEMEXEC_SHIFT       = 0x00000000,
SQ_TT_TOKEN_EXCLUDE_ALUEXEC_SHIFT        = 0x00000001,
SQ_TT_TOKEN_EXCLUDE_VALUINST_SHIFT       = 0x00000002,
SQ_TT_TOKEN_EXCLUDE_WAVERDY_SHIFT        = 0x00000003,
SQ_TT_TOKEN_EXCLUDE_WAVESTARTEND_SHIFT   = 0x00000004,
SQ_TT_TOKEN_EXCLUDE_IMMEDIATE_SHIFT      = 0x00000005,
SQ_TT_TOKEN_EXCLUDE_REG_SHIFT            = 0x00000006,
SQ_TT_TOKEN_EXCLUDE_EVENT_SHIFT          = 0x00000007,
SQ_TT_TOKEN_EXCLUDE_INST_SHIFT           = 0x00000008,
SQ_TT_TOKEN_EXCLUDE_UTILCTR_SHIFT        = 0x00000009,
SQ_TT_TOKEN_EXCLUDE_WAVEALLOC_SHIFT      = 0x0000000a,
SQ_TT_TOKEN_EXCLUDE_PERF_SHIFT           = 0x0000000b,
} SQ_TT_TOKEN_MASK_TOKEN_EXCLUDE_SHIFT;

/*
 * SQ_TT_UTIL_TIMER enum
 */

typedef enum SQ_TT_UTIL_TIMER {
SQ_TT_UTIL_TIMER_100_CLK                 = 0x00000000,
SQ_TT_UTIL_TIMER_250_CLK                 = 0x00000001,
} SQ_TT_UTIL_TIMER;

/*
 * SQ_TT_WAVESTART_MODE enum
 */

typedef enum SQ_TT_WAVESTART_MODE {
SQ_TT_WAVESTART_MODE_SHORT               = 0x00000000,
SQ_TT_WAVESTART_MODE_ALLOC               = 0x00000001,
SQ_TT_WAVESTART_MODE_PBB_ID              = 0x00000002,
} SQ_TT_WAVESTART_MODE;

/*
 * SQ_TT_WTYPE_INCLUDE enum
 */

typedef enum SQ_TT_WTYPE_INCLUDE {
SQ_TT_WTYPE_INCLUDE_PS_BIT               = 0x00000001,
SQ_TT_WTYPE_INCLUDE_RSVD0_BIT            = 0x00000002,
SQ_TT_WTYPE_INCLUDE_GS_BIT               = 0x00000004,
SQ_TT_WTYPE_INCLUDE_RSVD1_BIT            = 0x00000008,
SQ_TT_WTYPE_INCLUDE_HS_BIT               = 0x00000010,
SQ_TT_WTYPE_INCLUDE_RSVD2_BIT            = 0x00000020,
SQ_TT_WTYPE_INCLUDE_CS_BIT               = 0x00000040,
} SQ_TT_WTYPE_INCLUDE;

/*
 * SQ_TT_WTYPE_INCLUDE_SHIFT enum
 */

typedef enum SQ_TT_WTYPE_INCLUDE_SHIFT {
SQ_TT_WTYPE_INCLUDE_PS_SHIFT             = 0x00000000,
SQ_TT_WTYPE_INCLUDE_RSVD0_SHIFT          = 0x00000001,
SQ_TT_WTYPE_INCLUDE_GS_SHIFT             = 0x00000002,
SQ_TT_WTYPE_INCLUDE_RSVD1_SHIFT          = 0x00000003,
SQ_TT_WTYPE_INCLUDE_HS_SHIFT             = 0x00000004,
SQ_TT_WTYPE_INCLUDE_RSVD2_SHIFT          = 0x00000005,
SQ_TT_WTYPE_INCLUDE_CS_SHIFT             = 0x00000006,
} SQ_TT_WTYPE_INCLUDE_SHIFT;

/*
 * SQ_WATCH_MODES enum
 */

typedef enum SQ_WATCH_MODES {
SQ_WATCH_MODE_READ                       = 0x00000000,
SQ_WATCH_MODE_NONREAD                    = 0x00000001,
SQ_WATCH_MODE_ATOMIC                     = 0x00000002,
SQ_WATCH_MODE_ALL                        = 0x00000003,
} SQ_WATCH_MODES;

/*
 * SQ_WAVE_FWD_PROG_INTERVAL enum
 */

typedef enum SQ_WAVE_FWD_PROG_INTERVAL {
SQ_WAVE_FWD_PROG_INTERVAL_NEVER          = 0x00000000,
SQ_WAVE_FWD_PROG_INTERVAL_256            = 0x00000001,
SQ_WAVE_FWD_PROG_INTERVAL_1024           = 0x00000002,
SQ_WAVE_FWD_PROG_INTERVAL_4096           = 0x00000003,
} SQ_WAVE_FWD_PROG_INTERVAL;

/*
 * SQ_WAVE_IB_ECC_ST enum
 */

typedef enum SQ_WAVE_IB_ECC_ST {
SQ_WAVE_IB_ECC_CLEAN                     = 0x00000000,
SQ_WAVE_IB_ECC_ERR_CONTINUE              = 0x00000001,
SQ_WAVE_IB_ECC_ERR_HALT                  = 0x00000002,
SQ_WAVE_IB_ECC_WITH_ERR_MSG              = 0x00000003,
} SQ_WAVE_IB_ECC_ST;

/*
 * SQ_WAVE_SCHED_MODES enum
 */

typedef enum SQ_WAVE_SCHED_MODES {
SQ_WAVE_SCHED_MODE_NORMAL                = 0x00000000,
SQ_WAVE_SCHED_MODE_EXPERT                = 0x00000001,
SQ_WAVE_SCHED_MODE_DISABLE_VA_VDST       = 0x00000002,
} SQ_WAVE_SCHED_MODES;

/*
 * SQ_WAVE_TYPE enum
 */

typedef enum SQ_WAVE_TYPE {
SQ_WAVE_TYPE_PS                          = 0x00000000,
SQ_WAVE_TYPE_RSVD0                       = 0x00000001,
SQ_WAVE_TYPE_GS                          = 0x00000002,
SQ_WAVE_TYPE_RSVD1                       = 0x00000003,
SQ_WAVE_TYPE_HS                          = 0x00000004,
SQ_WAVE_TYPE_RSVD2                       = 0x00000005,
SQ_WAVE_TYPE_CS                          = 0x00000006,
SQ_WAVE_TYPE_PS1                         = 0x00000007,
SQ_WAVE_TYPE_PS2                         = 0x00000008,
SQ_WAVE_TYPE_PS3                         = 0x00000009,
} SQ_WAVE_TYPE;

/*
 * SQ_WAVE_TYPE value
 */

#define SQ_WAVE_TYPE_PS0               0x00000000

/*
 * SQIND_PARTITIONS value
 */

#define SQIND_GLOBAL_REGS_OFFSET       0x00000000
#define SQIND_GLOBAL_REGS_SIZE         0x00000008
#define SQIND_LOCAL_REGS_OFFSET        0x00000008
#define SQIND_LOCAL_REGS_SIZE          0x00000008
#define SQIND_WAVE_HWREGS_OFFSET       0x00000100
#define SQIND_WAVE_HWREGS_SIZE         0x00000100
#define SQIND_WAVE_SGPRS_OFFSET        0x00000200
#define SQIND_WAVE_SGPRS_SIZE          0x00000200
#define SQIND_WAVE_VGPRS_OFFSET        0x00000400
#define SQIND_WAVE_VGPRS_SIZE          0x00000400

/*
 * SQ_GFXDEC value
 */

#define SQ_GFXDEC_BEGIN                0x0000a000
#define SQ_GFXDEC_END                  0x0000c000
#define SQ_GFXDEC_STATE_ID_SHIFT       0x0000000a

/*
 * SQDEC value
 */

#define SQDEC_BEGIN                    0x00002300
#define SQDEC_END                      0x000023ff

/*
 * SQPERFSDEC value
 */

#define SQPERFSDEC_BEGIN               0x0000d9c0
#define SQPERFSDEC_END                 0x0000da40

/*
 * SQPERFDDEC value
 */

#define SQPERFDDEC_BEGIN               0x0000d1c0
#define SQPERFDDEC_END                 0x0000d240

/*
 * SQGFXUDEC value
 */

#define SQGFXUDEC_BEGIN                0x0000c330
#define SQGFXUDEC_END                  0x0000c380

/*
 * SQPWRDEC value
 */

#define SQPWRDEC_BEGIN                 0x0000f08c
#define SQPWRDEC_END                   0x0000f094

/*
 * SQ_DISPATCHER value
 */

#define SQ_DISPATCHER_GFX_MIN          0x00000010
#define SQ_DISPATCHER_GFX_CNT_PER_RING 0x00000008

/*
 * SQ_MAX value
 */

#define SQ_MAX_PGM_SGPRS               0x00000068
#define SQ_MAX_PGM_VGPRS               0x00000100

/*
 * SQ_EXCP_BITS value
 */

#define SQ_EX_MODE_EXCP_VALU_BASE      0x00000000
#define SQ_EX_MODE_EXCP_VALU_SIZE      0x00000007
#define SQ_EX_MODE_EXCP_INVALID        0x00000000
#define SQ_EX_MODE_EXCP_INPUT_DENORM   0x00000001
#define SQ_EX_MODE_EXCP_DIV0           0x00000002
#define SQ_EX_MODE_EXCP_OVERFLOW       0x00000003
#define SQ_EX_MODE_EXCP_UNDERFLOW      0x00000004
#define SQ_EX_MODE_EXCP_INEXACT        0x00000005
#define SQ_EX_MODE_EXCP_INT_DIV0       0x00000006
#define SQ_EX_MODE_EXCP_ADDR_WATCH0    0x00000007
#define SQ_EX_MODE_EXCP_MEM_VIOL       0x00000008

/*
 * SQ_EXCP_HI_BITS value
 */

#define SQ_EX_MODE_EXCP_HI_ADDR_WATCH1 0x00000000
#define SQ_EX_MODE_EXCP_HI_ADDR_WATCH2 0x00000001
#define SQ_EX_MODE_EXCP_HI_ADDR_WATCH3 0x00000002

/*
 * HW_INSERTED_INST_ID value
 */

#define INST_ID_PRIV_START             0x80000000
#define INST_ID_ECC_INTERRUPT_MSG      0xfffffff0
#define INST_ID_TTRACE_NEW_PC_MSG      0xfffffff1
#define INST_ID_HW_TRAP                0xfffffff2
#define INST_ID_KILL_SEQ               0xfffffff3
#define INST_ID_SPI_WREXEC             0xfffffff4
#define INST_ID_HW_TRAP_GET_TBA        0xfffffff5
#define INST_ID_HOST_REG_TRAP_MSG      0xfffffffe

/*
 * SIMM16_WAITCNT_PARTITIONS value
 */

#define SIMM16_WAITCNT_EXP_CNT_START   0x00000000
#define SIMM16_WAITCNT_EXP_CNT_SIZE    0x00000003
#define SIMM16_WAITCNT_LGKM_CNT_START  0x00000004
#define SIMM16_WAITCNT_LGKM_CNT_SIZE   0x00000006
#define SIMM16_WAITCNT_VM_CNT_START    0x0000000a
#define SIMM16_WAITCNT_VM_CNT_SIZE     0x00000006
#define SIMM16_WAITCNT_DEPCTR_SA_SDST_START 0x00000000
#define SIMM16_WAITCNT_DEPCTR_SA_SDST_SIZE 0x00000001
#define SIMM16_WAITCNT_DEPCTR_VA_VCC_START 0x00000001
#define SIMM16_WAITCNT_DEPCTR_VA_VCC_SIZE 0x00000001
#define SIMM16_WAITCNT_DEPCTR_VM_VSRC_START 0x00000002
#define SIMM16_WAITCNT_DEPCTR_VM_VSRC_SIZE 0x00000003
#define SIMM16_WAITCNT_DEPCTR_HOLD_CNT_START 0x00000006
#define SIMM16_WAITCNT_DEPCTR_HOLD_CNT_SIZE 0x00000001
#define SIMM16_WAITCNT_DEPCTR_VA_SSRC_START 0x00000007
#define SIMM16_WAITCNT_DEPCTR_VA_SSRC_SIZE 0x00000001
#define SIMM16_WAITCNT_DEPCTR_VA_SDST_START 0x00000008
#define SIMM16_WAITCNT_DEPCTR_VA_SDST_SIZE 0x00000003
#define SIMM16_WAITCNT_DEPCTR_VA_VDST_START 0x0000000b
#define SIMM16_WAITCNT_DEPCTR_VA_VDST_SIZE 0x00000005

/*
 * SIMM16_WAIT_EVENT_PARTITIONS value
 */

#define SIMM16_WAIT_EVENT_EXP_RDY_START 0x00000000
#define SIMM16_WAIT_EVENT_EXP_RDY_SIZE 0x00000001

/*
 * SQ_WAVE_IB_DEP_COUNTER_SIZES value
 */

#define SQ_WAVE_IB_DEP_SA_SDST_SIZE    0x00000004
#define SQ_WAVE_IB_DEP_SA_EXEC_SIZE    0x00000002
#define SQ_WAVE_IB_DEP_SA_M0_SIZE      0x00000001
#define SQ_WAVE_IB_DEP_VM_VSRC_SIZE    0x00000004
#define SQ_WAVE_IB_DEP_HOLD_CNT_SIZE   0x00000001
#define SQ_WAVE_IB_DEP_VA_SSRC_SIZE    0x00000003
#define SQ_WAVE_IB_DEP_VA_SDST_SIZE    0x00000004
#define SQ_WAVE_IB_DEP_VA_VCC_SIZE     0x00000003
#define SQ_WAVE_IB_DEP_VA_EXEC_SIZE    0x00000002
#define SQ_WAVE_IB_DEP_VA_VDST_SIZE    0x00000005
#define SQ_WAVE_IB_DEP_LDS_DIR_SIZE    0x00000003

/*
 * SQ_EDC_FUE_CNTL_BITS value
 */

#define SQ_EDC_FUE_CNTL_SIMD0          0x00000000
#define SQ_EDC_FUE_CNTL_SIMD1          0x00000001
#define SQ_EDC_FUE_CNTL_SIMD2          0x00000002
#define SQ_EDC_FUE_CNTL_SIMD3          0x00000003
#define SQ_EDC_FUE_CNTL_SQ             0x00000004
#define SQ_EDC_FUE_CNTL_LDS            0x00000005
#define SQ_EDC_FUE_CNTL_TD             0x00000006
#define SQ_EDC_FUE_CNTL_TA             0x00000007
#define SQ_EDC_FUE_CNTL_TCP            0x00000008

/*******************************************************
 * COMP Enums
 *******************************************************/

/*
 * CSCNTL_TYPE enum
 */

typedef enum CSCNTL_TYPE {
CSCNTL_TYPE_TG                           = 0x00000000,
CSCNTL_TYPE_STATE                        = 0x00000001,
CSCNTL_TYPE_EVENT                        = 0x00000002,
CSCNTL_TYPE_PRIVATE                      = 0x00000003,
} CSCNTL_TYPE;

/*
 * CSDATA_TYPE enum
 */

typedef enum CSDATA_TYPE {
CSDATA_TYPE_TG                           = 0x00000000,
CSDATA_TYPE_STATE                        = 0x00000001,
CSDATA_TYPE_EVENT                        = 0x00000002,
CSDATA_TYPE_PRIVATE                      = 0x00000003,
} CSDATA_TYPE;

/*
 * CSDATA_TYPE_WIDTH value
 */

#define CSDATA_TYPE_WIDTH              0x00000002

/*
 * CSDATA_ADDR_WIDTH value
 */

#define CSDATA_ADDR_WIDTH              0x00000007

/*
 * CSDATA_DATA_WIDTH value
 */

#define CSDATA_DATA_WIDTH              0x00000020

/*
 * CSCNTL_TYPE_WIDTH value
 */

#define CSCNTL_TYPE_WIDTH              0x00000002

/*
 * CSCNTL_ADDR_WIDTH value
 */

#define CSCNTL_ADDR_WIDTH              0x00000007

/*
 * CSCNTL_DATA_WIDTH value
 */

#define CSCNTL_DATA_WIDTH              0x00000020

/*******************************************************
 * GE Enums
 *******************************************************/

/*
 * GE1_PERFCOUNT_SELECT enum
 */

typedef enum GE1_PERFCOUNT_SELECT {
ge1_assembler_busy                       = 0x00000000,
ge1_assembler_stalled                    = 0x00000001,
ge1_dma_busy                             = 0x00000002,
ge1_dma_lat_bin_0                        = 0x00000003,
ge1_dma_lat_bin_1                        = 0x00000004,
ge1_dma_lat_bin_2                        = 0x00000005,
ge1_dma_lat_bin_3                        = 0x00000006,
ge1_dma_lat_bin_4                        = 0x00000007,
ge1_dma_lat_bin_5                        = 0x00000008,
ge1_dma_lat_bin_6                        = 0x00000009,
ge1_dma_lat_bin_7                        = 0x0000000a,
ge1_dma_return_cl0                       = 0x0000000b,
ge1_dma_return_cl1                       = 0x0000000c,
ge1_dma_utcl1_consecutive_retry_event    = 0x0000000d,
ge1_dma_utcl1_request_event              = 0x0000000e,
ge1_dma_utcl1_retry_event                = 0x0000000f,
ge1_dma_utcl1_stall_event                = 0x00000010,
ge1_dma_utcl1_stall_utcl2_event          = 0x00000011,
ge1_dma_utcl1_translation_hit_event      = 0x00000012,
ge1_dma_utcl1_translation_miss_event     = 0x00000013,
ge1_assembler_dma_starved                = 0x00000014,
ge1_rbiu_di_fifo_stalled_p0              = 0x00000015,
ge1_rbiu_di_fifo_starved_p0              = 0x00000016,
ge1_rbiu_dr_fifo_stalled_p0              = 0x00000017,
ge1_rbiu_dr_fifo_starved_p0              = 0x00000018,
ge1_sclk_reg_vld                         = 0x00000019,
ge1_stat_busy                            = 0x0000001a,
ge1_stat_no_dma_busy                     = 0x0000001b,
ge1_pipe0_to_pipe1                       = 0x0000001c,
ge1_pipe1_to_pipe0                       = 0x0000001d,
ge1_dma_return_size_cl0                  = 0x0000001e,
ge1_dma_return_size_cl1                  = 0x0000001f,
ge1_small_draws_one_instance             = 0x00000020,
ge1_sclk_input_vld                       = 0x00000021,
ge1_prim_group_limit_hit                 = 0x00000022,
ge1_unopt_multi_instance_draws           = 0x00000023,
ge1_rbiu_di_fifo_stalled_p1              = 0x00000024,
ge1_rbiu_di_fifo_starved_p1              = 0x00000025,
ge1_rbiu_dr_fifo_stalled_p1              = 0x00000026,
ge1_rbiu_dr_fifo_starved_p1              = 0x00000027,
} GE1_PERFCOUNT_SELECT;

/*
 * GE2_DIST_PERFCOUNT_SELECT enum
 */

typedef enum GE2_DIST_PERFCOUNT_SELECT {
ge_dist_hs_done                          = 0x00000000,
ge_dist_hs_done_latency_se0              = 0x00000001,
ge_dist_hs_done_latency_se1              = 0x00000002,
ge_dist_hs_done_latency_se2              = 0x00000003,
ge_dist_hs_done_latency_se3              = 0x00000004,
ge_dist_hs_done_latency_se4              = 0x00000005,
ge_dist_hs_done_latency_se5              = 0x00000006,
ge_dist_hs_done_latency_se6              = 0x00000007,
ge_dist_hs_done_latency_se7              = 0x00000008,
ge_dist_inside_tf_bin_0                  = 0x00000009,
ge_dist_inside_tf_bin_1                  = 0x0000000a,
ge_dist_inside_tf_bin_2                  = 0x0000000b,
ge_dist_inside_tf_bin_3                  = 0x0000000c,
ge_dist_inside_tf_bin_4                  = 0x0000000d,
ge_dist_inside_tf_bin_5                  = 0x0000000e,
ge_dist_inside_tf_bin_6                  = 0x0000000f,
ge_dist_inside_tf_bin_7                  = 0x00000010,
ge_dist_inside_tf_bin_8                  = 0x00000011,
ge_dist_null_patch                       = 0x00000012,
ge_dist_sclk_core_vld                    = 0x00000013,
ge_dist_sclk_wd_te11_vld                 = 0x00000014,
ge_dist_tfreq_lat_bin_0                  = 0x00000015,
ge_dist_tfreq_lat_bin_1                  = 0x00000016,
ge_dist_tfreq_lat_bin_2                  = 0x00000017,
ge_dist_tfreq_lat_bin_3                  = 0x00000018,
ge_dist_tfreq_lat_bin_4                  = 0x00000019,
ge_dist_tfreq_lat_bin_5                  = 0x0000001a,
ge_dist_tfreq_lat_bin_6                  = 0x0000001b,
ge_dist_tfreq_lat_bin_7                  = 0x0000001c,
ge_dist_tfreq_utcl1_consecutive_retry_event = 0x0000001d,
ge_dist_tfreq_utcl1_request_event        = 0x0000001e,
ge_dist_tfreq_utcl1_retry_event          = 0x0000001f,
ge_dist_tfreq_utcl1_stall_event          = 0x00000020,
ge_dist_tfreq_utcl1_stall_utcl2_event    = 0x00000021,
ge_dist_tfreq_utcl1_translation_hit_event = 0x00000022,
ge_dist_tfreq_utcl1_translation_miss_event = 0x00000023,
ge_dist_vs_pc_stall                      = 0x00000024,
ge_dist_pc_feorder_fifo_full             = 0x00000025,
ge_dist_pc_ge_manager_busy               = 0x00000026,
ge_dist_pc_req_stall_se0                 = 0x00000027,
ge_dist_pc_req_stall_se1                 = 0x00000028,
ge_dist_pc_req_stall_se2                 = 0x00000029,
ge_dist_pc_req_stall_se3                 = 0x0000002a,
ge_dist_pc_req_stall_se4                 = 0x0000002b,
ge_dist_pc_req_stall_se5                 = 0x0000002c,
ge_dist_pc_req_stall_se6                 = 0x0000002d,
ge_dist_pc_req_stall_se7                 = 0x0000002e,
ge_dist_pc_space_zero                    = 0x0000002f,
ge_dist_sclk_input_vld                   = 0x00000030,
ge_dist_reserved                         = 0x00000031,
ge_dist_wd_te11_busy                     = 0x00000032,
ge_dist_te11_starved                     = 0x00000033,
ge_dist_switch_mode_stall                = 0x00000034,
ge_all_tf_eq                             = 0x00000035,
ge_all_tf2                               = 0x00000036,
ge_all_tf3                               = 0x00000037,
ge_all_tf4                               = 0x00000038,
ge_all_tf5                               = 0x00000039,
ge_all_tf6                               = 0x0000003a,
ge_se0_te11_starved_on_hs_done           = 0x0000003b,
ge_se1_te11_starved_on_hs_done           = 0x0000003c,
ge_se2_te11_starved_on_hs_done           = 0x0000003d,
ge_se3_te11_starved_on_hs_done           = 0x0000003e,
ge_se4_te11_starved_on_hs_done           = 0x0000003f,
ge_se5_te11_starved_on_hs_done           = 0x00000040,
ge_se6_te11_starved_on_hs_done           = 0x00000041,
ge_se7_te11_starved_on_hs_done           = 0x00000042,
ge_dist_op_fifo_full_starve              = 0x00000043,
ge_dist_hs_done_se0                      = 0x00000044,
ge_dist_hs_done_se1                      = 0x00000045,
ge_dist_hs_done_se2                      = 0x00000046,
ge_dist_hs_done_se3                      = 0x00000047,
ge_dist_hs_done_se4                      = 0x00000048,
ge_dist_hs_done_se5                      = 0x00000049,
ge_dist_hs_done_se6                      = 0x0000004a,
ge_dist_hs_done_se7                      = 0x0000004b,
ge_dist_hs_done_latency                  = 0x0000004c,
ge_dist_distributer_busy                 = 0x0000004d,
ge_tf_ret_data_stalling_hs_done          = 0x0000004e,
ge_num_of_no_dist_patches                = 0x0000004f,
ge_num_of_donut_dist_patches             = 0x00000050,
ge_num_of_patch_dist_patches             = 0x00000051,
ge_num_of_se_switches_due_to_patch_accum = 0x00000052,
ge_num_of_se_switches_due_to_donut       = 0x00000053,
ge_num_of_se_switches_due_to_trap        = 0x00000054,
ge_num_of_hs_alloc_events                = 0x00000055,
ge_agm_gcr_req                           = 0x00000056,
ge_agm_gcr_tag_stall                     = 0x00000057,
ge_agm_gcr_crd_stall                     = 0x00000058,
ge_agm_gcr_stall                         = 0x00000059,
ge_agm_gcr_latency                       = 0x0000005a,
ge_distclk_vld                           = 0x0000005b,
} GE2_DIST_PERFCOUNT_SELECT;

/*
 * GE2_SE_PERFCOUNT_SELECT enum
 */

typedef enum GE2_SE_PERFCOUNT_SELECT {
ge_se_ds_prims                           = 0x00000000,
ge_se_es_thread_groups                   = 0x00000001,
ge_se_esvert_stalled_gsprim              = 0x00000002,
ge_se_hs_tfm_stall                       = 0x00000003,
ge_se_hs_tgs_active_high_water_mark      = 0x00000004,
ge_se_hs_thread_groups                   = 0x00000005,
ge_se_reused_es_indices                  = 0x00000006,
ge_se_sclk_ngg_vld                       = 0x00000007,
ge_se_sclk_te11_vld                      = 0x00000008,
ge_se_spi_esvert_eov                     = 0x00000009,
ge_se_spi_esvert_stalled                 = 0x0000000a,
ge_se_spi_esvert_starved_busy            = 0x0000000b,
ge_se_spi_esvert_valid                   = 0x0000000c,
ge_se_spi_gsprim_cont                    = 0x0000000d,
ge_se_spi_gsprim_eov                     = 0x0000000e,
ge_se_spi_gsprim_stalled                 = 0x0000000f,
ge_se_spi_gsprim_starved_busy            = 0x00000010,
ge_se_spi_gsprim_valid                   = 0x00000011,
ge_se_spi_gssubgrp_is_event              = 0x00000012,
ge_se_spi_gssubgrp_send                  = 0x00000013,
ge_se_spi_hsvert_eov                     = 0x00000014,
ge_se_spi_hsvert_stalled                 = 0x00000015,
ge_se_spi_hsvert_starved_busy            = 0x00000016,
ge_se_spi_hsvert_valid                   = 0x00000017,
ge_se_spi_hswave_is_event                = 0x00000018,
ge_se_spi_hswave_send                    = 0x00000019,
ge_se_spi_lsvert_eov                     = 0x0000001a,
ge_se_spi_lsvert_stalled                 = 0x0000001b,
ge_se_spi_lsvert_starved_busy            = 0x0000001c,
ge_se_spi_lsvert_valid                   = 0x0000001d,
ge_se_spi_hsvert_fifo_full_stall         = 0x0000001e,
ge_se_spi_tgrp_fifo_stall                = 0x0000001f,
ge_spi_hsgrp_spi_stall                   = 0x00000020,
ge_se_spi_gssubgrp_event_window_active   = 0x00000021,
ge_se_hs_input_stall                     = 0x00000022,
ge_se_sending_vert_or_prim               = 0x00000023,
ge_se_sclk_input_vld                     = 0x00000024,
ge_spi_lswave_fifo_full_stall            = 0x00000025,
ge_spi_hswave_fifo_full_stall            = 0x00000026,
ge_hs_tif_stall                          = 0x00000027,
ge_csb_spi_bp                            = 0x00000028,
ge_ngg_starving_for_pc_grant             = 0x00000029,
ge_pa0_csb_eop                           = 0x0000002a,
ge_pa1_csb_eop                           = 0x0000002b,
ge_ngg_starved_idle                      = 0x0000002c,
ge_gsprim_send                           = 0x0000002d,
ge_esvert_send                           = 0x0000002e,
ge_ngg_starved_after_work                = 0x0000002f,
ge_ngg_subgrp_fifo_stall                 = 0x00000030,
ge_ngg_ord_id_req_stall                  = 0x00000031,
ge_ngg_indx_bus_stall                    = 0x00000032,
ge_hs_stall_tfmm_fifo_full               = 0x00000033,
ge_gs_issue_rtr_stalled                  = 0x00000034,
ge_gsprim_stalled_esvert                 = 0x00000035,
ge_gsthread_stalled                      = 0x00000036,
ge_te11_stall_prim_funnel                = 0x00000037,
ge_te11_stall_vert_funnel                = 0x00000038,
ge_ngg_attr_grp_alloc                    = 0x00000039,
ge_ngg_attr_discard_alloc                = 0x0000003a,
ge_ngg_pc_space_not_avail                = 0x0000003b,
ge_ngg_agm_req_stall                     = 0x0000003c,
ge_ngg_spi_esvert_partial_eov            = 0x0000003d,
ge_ngg_spi_gsprim_partial_eov            = 0x0000003e,
ge_spi_gsgrp_valid                       = 0x0000003f,
ge_ngg_attr_grp_latency                  = 0x00000040,
ge_ngg_reuse_prim_limit_hit              = 0x00000041,
ge_ngg_reuse_vert_limit_hit              = 0x00000042,
ge_te11_con_stall                        = 0x00000043,
ge_te11_compactor_starved                = 0x00000044,
ge_ngg_stall_tess_off_tess_on            = 0x00000045,
ge_ngg_stall_tess_on_tess_off            = 0x00000046,
} GE2_SE_PERFCOUNT_SELECT;

/*
 * VGT_DETECT_ONE enum
 */

typedef enum VGT_DETECT_ONE {
ENABLE_TF1_OPT                           = 0x00000000,
DISABLE_TF1_OPT                          = 0x00000001,
} VGT_DETECT_ONE;

/*
 * VGT_DETECT_ZERO enum
 */

typedef enum VGT_DETECT_ZERO {
ENABLE_TF0_OPT                           = 0x00000000,
DISABLE_TF0_OPT                          = 0x00000001,
} VGT_DETECT_ZERO;

/*
 * VGT_DIST_MODE enum
 */

typedef enum VGT_DIST_MODE {
NO_DIST                                  = 0x00000000,
PATCHES                                  = 0x00000001,
DONUTS                                   = 0x00000002,
TRAPEZOIDS                               = 0x00000003,
} VGT_DIST_MODE;

/*
 * VGT_DI_INDEX_SIZE enum
 */

typedef enum VGT_DI_INDEX_SIZE {
DI_INDEX_SIZE_16_BIT                     = 0x00000000,
DI_INDEX_SIZE_32_BIT                     = 0x00000001,
DI_INDEX_SIZE_8_BIT                      = 0x00000002,
} VGT_DI_INDEX_SIZE;

/*
 * VGT_DI_MAJOR_MODE_SELECT enum
 */

typedef enum VGT_DI_MAJOR_MODE_SELECT {
DI_MAJOR_MODE_0                          = 0x00000000,
DI_MAJOR_MODE_1                          = 0x00000001,
} VGT_DI_MAJOR_MODE_SELECT;

/*
 * VGT_DI_PRIM_TYPE enum
 */

typedef enum VGT_DI_PRIM_TYPE {
DI_PT_NONE                               = 0x00000000,
DI_PT_POINTLIST                          = 0x00000001,
DI_PT_LINELIST                           = 0x00000002,
DI_PT_LINESTRIP                          = 0x00000003,
DI_PT_TRILIST                            = 0x00000004,
DI_PT_TRIFAN                             = 0x00000005,
DI_PT_TRISTRIP                           = 0x00000006,
DI_PT_2D_RECTANGLE                       = 0x00000007,
DI_PT_UNUSED_1                           = 0x00000008,
DI_PT_PATCH                              = 0x00000009,
DI_PT_LINELIST_ADJ                       = 0x0000000a,
DI_PT_LINESTRIP_ADJ                      = 0x0000000b,
DI_PT_TRILIST_ADJ                        = 0x0000000c,
DI_PT_TRISTRIP_ADJ                       = 0x0000000d,
DI_PT_UNUSED_3                           = 0x0000000e,
DI_PT_UNUSED_4                           = 0x0000000f,
DI_PT_UNUSED_5                           = 0x00000010,
DI_PT_RECTLIST                           = 0x00000011,
DI_PT_LINELOOP                           = 0x00000012,
DI_PT_QUADLIST                           = 0x00000013,
DI_PT_QUADSTRIP                          = 0x00000014,
DI_PT_POLYGON                            = 0x00000015,
} VGT_DI_PRIM_TYPE;

/*
 * VGT_DI_SOURCE_SELECT enum
 */

typedef enum VGT_DI_SOURCE_SELECT {
DI_SRC_SEL_DMA                           = 0x00000000,
DI_SRC_SEL_IMMEDIATE                     = 0x00000001,
DI_SRC_SEL_AUTO_INDEX                    = 0x00000002,
DI_SRC_SEL_RESERVED                      = 0x00000003,
} VGT_DI_SOURCE_SELECT;

/*
 * VGT_DMA_BUF_TYPE enum
 */

typedef enum VGT_DMA_BUF_TYPE {
VGT_DMA_BUF_MEM                          = 0x00000000,
VGT_DMA_BUF_RING                         = 0x00000001,
VGT_DMA_BUF_SETUP                        = 0x00000002,
VGT_DMA_PTR_UPDATE                       = 0x00000003,
} VGT_DMA_BUF_TYPE;

/*
 * VGT_DMA_SWAP_MODE enum
 */

typedef enum VGT_DMA_SWAP_MODE {
VGT_DMA_SWAP_NONE                        = 0x00000000,
VGT_DMA_SWAP_16_BIT                      = 0x00000001,
VGT_DMA_SWAP_32_BIT                      = 0x00000002,
VGT_DMA_SWAP_WORD                        = 0x00000003,
} VGT_DMA_SWAP_MODE;

/*
 * VGT_EVENT_TYPE enum
 */

typedef enum VGT_EVENT_TYPE {
Reserved_0x00                            = 0x00000000,
SAMPLE_STREAMOUTSTATS1                   = 0x00000001,
SAMPLE_STREAMOUTSTATS2                   = 0x00000002,
SAMPLE_STREAMOUTSTATS3                   = 0x00000003,
CACHE_FLUSH_TS                           = 0x00000004,
CONTEXT_DONE                             = 0x00000005,
CACHE_FLUSH                              = 0x00000006,
CS_PARTIAL_FLUSH                         = 0x00000007,
VGT_STREAMOUT_SYNC                       = 0x00000008,
Reserved_0x09                            = 0x00000009,
VGT_STREAMOUT_RESET                      = 0x0000000a,
END_OF_PIPE_INCR_DE                      = 0x0000000b,
END_OF_PIPE_IB_END                       = 0x0000000c,
RST_PIX_CNT                              = 0x0000000d,
BREAK_BATCH                              = 0x0000000e,
VS_PARTIAL_FLUSH                         = 0x0000000f,
PS_PARTIAL_FLUSH                         = 0x00000010,
FLUSH_HS_OUTPUT                          = 0x00000011,
FLUSH_DFSM                               = 0x00000012,
RESET_TO_LOWEST_VGT                      = 0x00000013,
CACHE_FLUSH_AND_INV_TS_EVENT             = 0x00000014,
WAIT_SYNC                                = 0x00000015,
CACHE_FLUSH_AND_INV_EVENT                = 0x00000016,
PERFCOUNTER_START                        = 0x00000017,
PERFCOUNTER_STOP                         = 0x00000018,
PIPELINESTAT_START                       = 0x00000019,
PIPELINESTAT_STOP                        = 0x0000001a,
PERFCOUNTER_SAMPLE                       = 0x0000001b,
FLUSH_ES_OUTPUT                          = 0x0000001c,
BIN_CONF_OVERRIDE_CHECK                  = 0x0000001d,
SAMPLE_PIPELINESTAT                      = 0x0000001e,
SO_VGTSTREAMOUT_FLUSH                    = 0x0000001f,
SAMPLE_STREAMOUTSTATS                    = 0x00000020,
RESET_VTX_CNT                            = 0x00000021,
BLOCK_CONTEXT_DONE                       = 0x00000022,
CS_CONTEXT_DONE                          = 0x00000023,
VGT_FLUSH                                = 0x00000024,
TGID_ROLLOVER                            = 0x00000025,
SQ_NON_EVENT                             = 0x00000026,
SC_SEND_DB_VPZ                           = 0x00000027,
BOTTOM_OF_PIPE_TS                        = 0x00000028,
FLUSH_SX_TS                              = 0x00000029,
DB_CACHE_FLUSH_AND_INV                   = 0x0000002a,
FLUSH_AND_INV_DB_DATA_TS                 = 0x0000002b,
FLUSH_AND_INV_DB_META                    = 0x0000002c,
FLUSH_AND_INV_CB_DATA_TS                 = 0x0000002d,
FLUSH_AND_INV_CB_META                    = 0x0000002e,
CS_DONE                                  = 0x0000002f,
PS_DONE                                  = 0x00000030,
FLUSH_AND_INV_CB_PIXEL_DATA              = 0x00000031,
SX_CB_RAT_ACK_REQUEST                    = 0x00000032,
THREAD_TRACE_START                       = 0x00000033,
THREAD_TRACE_STOP                        = 0x00000034,
THREAD_TRACE_MARKER                      = 0x00000035,
THREAD_TRACE_DRAW                        = 0x00000036,
THREAD_TRACE_FINISH                      = 0x00000037,
PIXEL_PIPE_STAT_CONTROL                  = 0x00000038,
PIXEL_PIPE_STAT_DUMP                     = 0x00000039,
PIXEL_PIPE_STAT_RESET                    = 0x0000003a,
CONTEXT_SUSPEND                          = 0x0000003b,
OFFCHIP_HS_DEALLOC                       = 0x0000003c,
ENABLE_NGG_PIPELINE                      = 0x0000003d,
ENABLE_LEGACY_PIPELINE                   = 0x0000003e,
DRAW_DONE                                = 0x0000003f,
} VGT_EVENT_TYPE;

/*
 * VGT_GROUP_CONV_SEL enum
 */

typedef enum VGT_GROUP_CONV_SEL {
VGT_GRP_INDEX_16                         = 0x00000000,
VGT_GRP_INDEX_32                         = 0x00000001,
VGT_GRP_UINT_16                          = 0x00000002,
VGT_GRP_UINT_32                          = 0x00000003,
VGT_GRP_SINT_16                          = 0x00000004,
VGT_GRP_SINT_32                          = 0x00000005,
VGT_GRP_FLOAT_32                         = 0x00000006,
VGT_GRP_AUTO_PRIM                        = 0x00000007,
VGT_GRP_FIX_1_23_TO_FLOAT                = 0x00000008,
} VGT_GROUP_CONV_SEL;

/*
 * VGT_GS_MODE_TYPE enum
 */

typedef enum VGT_GS_MODE_TYPE {
GS_OFF                                   = 0x00000000,
GS_SCENARIO_A                            = 0x00000001,
GS_SCENARIO_B                            = 0x00000002,
GS_SCENARIO_G                            = 0x00000003,
GS_SCENARIO_C                            = 0x00000004,
SPRITE_EN                                = 0x00000005,
} VGT_GS_MODE_TYPE;

/*
 * VGT_GS_OUTPRIM_TYPE enum
 */

typedef enum VGT_GS_OUTPRIM_TYPE {
POINTLIST                                = 0x00000000,
LINESTRIP                                = 0x00000001,
TRISTRIP                                 = 0x00000002,
RECT_2D                                  = 0x00000003,
RECTLIST                                 = 0x00000004,
} VGT_GS_OUTPRIM_TYPE;

/*
 * VGT_INDEX_TYPE_MODE enum
 */

typedef enum VGT_INDEX_TYPE_MODE {
VGT_INDEX_16                             = 0x00000000,
VGT_INDEX_32                             = 0x00000001,
VGT_INDEX_8                              = 0x00000002,
} VGT_INDEX_TYPE_MODE;

/*
 * VGT_OUTPATH_SELECT enum
 */

typedef enum VGT_OUTPATH_SELECT {
VGT_OUTPATH_VTX_REUSE                    = 0x00000000,
VGT_OUTPATH_GS_BLOCK                     = 0x00000001,
VGT_OUTPATH_HS_BLOCK                     = 0x00000002,
VGT_OUTPATH_PRIM_GEN                     = 0x00000003,
VGT_OUTPATH_TE_PRIM_GEN                  = 0x00000004,
VGT_OUTPATH_TE_GS_BLOCK                  = 0x00000005,
VGT_OUTPATH_TE_OUTPUT                    = 0x00000006,
} VGT_OUTPATH_SELECT;

/*
 * VGT_OUT_PRIM_TYPE enum
 */

typedef enum VGT_OUT_PRIM_TYPE {
VGT_OUT_POINT                            = 0x00000000,
VGT_OUT_LINE                             = 0x00000001,
VGT_OUT_TRI                              = 0x00000002,
VGT_OUT_RECT_V0                          = 0x00000003,
VGT_OUT_RECT_V1                          = 0x00000004,
VGT_OUT_RECT_V2                          = 0x00000005,
VGT_OUT_RECT_V3                          = 0x00000006,
VGT_OUT_2D_RECT                          = 0x00000007,
VGT_TE_QUAD                              = 0x00000008,
VGT_TE_PRIM_INDEX_LINE                   = 0x00000009,
VGT_TE_PRIM_INDEX_TRI                    = 0x0000000a,
VGT_TE_PRIM_INDEX_QUAD                   = 0x0000000b,
VGT_OUT_LINE_ADJ                         = 0x0000000c,
VGT_OUT_TRI_ADJ                          = 0x0000000d,
VGT_OUT_PATCH                            = 0x0000000e,
} VGT_OUT_PRIM_TYPE;

/*
 * VGT_RDREQ_POLICY enum
 */

typedef enum VGT_RDREQ_POLICY {
VGT_POLICY_LRU                           = 0x00000000,
VGT_POLICY_STREAM                        = 0x00000001,
VGT_POLICY_BYPASS                        = 0x00000002,
} VGT_RDREQ_POLICY;

/*
 * VGT_STAGES_ES_EN enum
 */

typedef enum VGT_STAGES_ES_EN {
ES_STAGE_OFF                             = 0x00000000,
ES_STAGE_DS                              = 0x00000001,
ES_STAGE_REAL                            = 0x00000002,
RESERVED_ES                              = 0x00000003,
} VGT_STAGES_ES_EN;

/*
 * VGT_STAGES_GS_EN enum
 */

typedef enum VGT_STAGES_GS_EN {
GS_STAGE_OFF                             = 0x00000000,
GS_STAGE_ON                              = 0x00000001,
} VGT_STAGES_GS_EN;

/*
 * VGT_STAGES_HS_EN enum
 */

typedef enum VGT_STAGES_HS_EN {
HS_STAGE_OFF                             = 0x00000000,
HS_STAGE_ON                              = 0x00000001,
} VGT_STAGES_HS_EN;

/*
 * VGT_STAGES_LS_EN enum
 */

typedef enum VGT_STAGES_LS_EN {
LS_STAGE_OFF                             = 0x00000000,
LS_STAGE_ON                              = 0x00000001,
CS_STAGE_ON                              = 0x00000002,
RESERVED_LS                              = 0x00000003,
} VGT_STAGES_LS_EN;

/*
 * VGT_STAGES_VS_EN enum
 */

typedef enum VGT_STAGES_VS_EN {
VS_STAGE_REAL                            = 0x00000000,
VS_STAGE_DS                              = 0x00000001,
VS_STAGE_COPY_SHADER                     = 0x00000002,
RESERVED_VS                              = 0x00000003,
} VGT_STAGES_VS_EN;

/*
 * VGT_TESS_PARTITION enum
 */

typedef enum VGT_TESS_PARTITION {
PART_INTEGER                             = 0x00000000,
PART_POW2                                = 0x00000001,
PART_FRAC_ODD                            = 0x00000002,
PART_FRAC_EVEN                           = 0x00000003,
} VGT_TESS_PARTITION;

/*
 * VGT_TESS_TOPOLOGY enum
 */

typedef enum VGT_TESS_TOPOLOGY {
OUTPUT_POINT                             = 0x00000000,
OUTPUT_LINE                              = 0x00000001,
OUTPUT_TRIANGLE_CW                       = 0x00000002,
OUTPUT_TRIANGLE_CCW                      = 0x00000003,
} VGT_TESS_TOPOLOGY;

/*
 * VGT_TESS_TYPE enum
 */

typedef enum VGT_TESS_TYPE {
TESS_ISOLINE                             = 0x00000000,
TESS_TRIANGLE                            = 0x00000001,
TESS_QUAD                                = 0x00000002,
} VGT_TESS_TYPE;

/*
 * WD_IA_DRAW_REG_XFER enum
 */

typedef enum WD_IA_DRAW_REG_XFER {
WD_IA_DRAW_REG_XFER_IA_MULTI_VGT_PARAM   = 0x00000000,
WD_IA_DRAW_REG_XFER_VGT_MULTI_PRIM_IB_RESET_EN = 0x00000001,
WD_IA_DRAW_REG_XFER_VGT_INSTANCE_BASE_ID = 0x00000002,
WD_IA_DRAW_REG_XFER_GE_CNTL              = 0x00000003,
WD_IA_DRAW_REG_XFER_GE_USER_VGPR_EN      = 0x00000004,
WD_IA_DRAW_REG_XFER_FL_MS_WG_DIM         = 0x00000005,
WD_IA_DRAW_REG_XFER_FL_MS_WG_DIM_1       = 0x00000006,
WD_IA_DRAW_REG_XFER_FL_MS_TG_SIZE        = 0x00000007,
WD_IA_DRAW_REG_XFER_FL_MS_EXP_ALLOC      = 0x00000008,
} WD_IA_DRAW_REG_XFER;

/*
 * WD_IA_DRAW_SOURCE enum
 */

typedef enum WD_IA_DRAW_SOURCE {
WD_IA_DRAW_SOURCE_DMA                    = 0x00000000,
WD_IA_DRAW_SOURCE_IMMD                   = 0x00000001,
WD_IA_DRAW_SOURCE_AUTO                   = 0x00000002,
WD_IA_DRAW_SOURCE_OPAQ                   = 0x00000003,
} WD_IA_DRAW_SOURCE;

/*
 * WD_IA_DRAW_TYPE enum
 */

typedef enum WD_IA_DRAW_TYPE {
WD_IA_DRAW_TYPE_DI_MM0                   = 0x00000000,
WD_IA_DRAW_TYPE_REG_XFER                 = 0x00000001,
WD_IA_DRAW_TYPE_EVENT_INIT               = 0x00000002,
WD_IA_DRAW_TYPE_EVENT_ADDR               = 0x00000003,
WD_IA_DRAW_TYPE_MIN_INDX                 = 0x00000004,
WD_IA_DRAW_TYPE_MAX_INDX                 = 0x00000005,
WD_IA_DRAW_TYPE_INDX_OFF                 = 0x00000006,
WD_IA_DRAW_TYPE_IMM_DATA                 = 0x00000007,
} WD_IA_DRAW_TYPE;

/*
 * GS_THREADID_SIZE value
 */

#define GSTHREADID_SIZE                0x00000002

/*******************************************************
 * GB Enums
 *******************************************************/

/*
 * GB_EDC_DED_MODE enum
 */

typedef enum GB_EDC_DED_MODE {
GB_EDC_DED_MODE_LOG                      = 0x00000000,
GB_EDC_DED_MODE_HALT                     = 0x00000001,
GB_EDC_DED_MODE_INT_HALT                 = 0x00000002,
} GB_EDC_DED_MODE;

/*
 * VALUE_GB_TILING_CONFIG_TABLE_SIZE value
 */

#define GB_TILING_CONFIG_TABLE_SIZE    0x00000020

/*
 * VALUE_GB_TILING_CONFIG_MACROTABLE_SIZE value
 */

#define GB_TILING_CONFIG_MACROTABLE_SIZE 0x00000010

/*******************************************************
 * GLX Enums
 *******************************************************/

/*
 * CHA_PERF_SEL enum
 */

typedef enum CHA_PERF_SEL {
CHA_PERF_SEL_BUSY                        = 0x00000000,
CHA_PERF_SEL_STALL_CHC0                  = 0x00000001,
CHA_PERF_SEL_STALL_CHC1                  = 0x00000002,
CHA_PERF_SEL_STALL_CHC2                  = 0x00000003,
CHA_PERF_SEL_STALL_CHC3                  = 0x00000004,
CHA_PERF_SEL_STALL_CHC4                  = 0x00000005,
CHA_PERF_SEL_STALL_CHC5                  = 0x00000006,
CHA_PERF_SEL_REQUEST_CHC0                = 0x00000007,
CHA_PERF_SEL_REQUEST_CHC1                = 0x00000008,
CHA_PERF_SEL_REQUEST_CHC2                = 0x00000009,
CHA_PERF_SEL_REQUEST_CHC3                = 0x0000000a,
CHA_PERF_SEL_REQUEST_CHC4                = 0x0000000b,
CHA_PERF_SEL_MEM_32B_WDS_CHC0            = 0x0000000c,
CHA_PERF_SEL_MEM_32B_WDS_CHC1            = 0x0000000d,
CHA_PERF_SEL_MEM_32B_WDS_CHC2            = 0x0000000e,
CHA_PERF_SEL_MEM_32B_WDS_CHC3            = 0x0000000f,
CHA_PERF_SEL_MEM_32B_WDS_CHC4            = 0x00000010,
CHA_PERF_SEL_IO_32B_WDS_CHC0             = 0x00000011,
CHA_PERF_SEL_IO_32B_WDS_CHC1             = 0x00000012,
CHA_PERF_SEL_IO_32B_WDS_CHC2             = 0x00000013,
CHA_PERF_SEL_IO_32B_WDS_CHC3             = 0x00000014,
CHA_PERF_SEL_IO_32B_WDS_CHC4             = 0x00000015,
CHA_PERF_SEL_MEM_BURST_COUNT_CHC0        = 0x00000016,
CHA_PERF_SEL_MEM_BURST_COUNT_CHC1        = 0x00000017,
CHA_PERF_SEL_MEM_BURST_COUNT_CHC2        = 0x00000018,
CHA_PERF_SEL_MEM_BURST_COUNT_CHC3        = 0x00000019,
CHA_PERF_SEL_MEM_BURST_COUNT_CHC4        = 0x0000001a,
CHA_PERF_SEL_IO_BURST_COUNT_CHC0         = 0x0000001b,
CHA_PERF_SEL_IO_BURST_COUNT_CHC1         = 0x0000001c,
CHA_PERF_SEL_IO_BURST_COUNT_CHC2         = 0x0000001d,
CHA_PERF_SEL_IO_BURST_COUNT_CHC3         = 0x0000001e,
CHA_PERF_SEL_IO_BURST_COUNT_CHC4         = 0x0000001f,
CHA_PERF_SEL_ARB_REQUESTS                = 0x00000020,
CHA_PERF_SEL_REQ_INFLIGHT_LEVEL          = 0x00000021,
CHA_PERF_SEL_STALL_RET_CONFLICT_CHC0     = 0x00000022,
CHA_PERF_SEL_STALL_RET_CONFLICT_CHC1     = 0x00000023,
CHA_PERF_SEL_STALL_RET_CONFLICT_CHC2     = 0x00000024,
CHA_PERF_SEL_STALL_RET_CONFLICT_CHC3     = 0x00000025,
CHA_PERF_SEL_STALL_RET_CONFLICT_CHC4     = 0x00000026,
CHA_PERF_SEL_CYCLE                       = 0x00000027,
} CHA_PERF_SEL;

/*
 * CHCG_PERF_SEL enum
 */

typedef enum CHCG_PERF_SEL {
CHCG_PERF_SEL_CYCLE                      = 0x00000000,
CHCG_PERF_SEL_BUSY                       = 0x00000001,
CHCG_PERF_SEL_STARVE                     = 0x00000002,
CHCG_PERF_SEL_ARB_RET_LEVEL              = 0x00000003,
CHCG_PERF_SEL_GL2_REQ_READ_LATENCY       = 0x00000004,
CHCG_PERF_SEL_GL2_REQ_WRITE_LATENCY      = 0x00000005,
CHCG_PERF_SEL_REQ                        = 0x00000006,
CHCG_PERF_SEL_REQ_ATOMIC_WITH_RET        = 0x00000007,
CHCG_PERF_SEL_REQ_ATOMIC_WITHOUT_RET     = 0x00000008,
CHCG_PERF_SEL_REQ_NOP_ACK                = 0x00000009,
CHCG_PERF_SEL_REQ_NOP_RTN0               = 0x0000000a,
CHCG_PERF_SEL_REQ_READ                   = 0x0000000b,
CHCG_PERF_SEL_REQ_READ_128B              = 0x0000000c,
CHCG_PERF_SEL_REQ_READ_32B               = 0x0000000d,
CHCG_PERF_SEL_REQ_READ_64B               = 0x0000000e,
CHCG_PERF_SEL_REQ_WRITE                  = 0x0000000f,
CHCG_PERF_SEL_REQ_WRITE_32B              = 0x00000010,
CHCG_PERF_SEL_REQ_WRITE_64B              = 0x00000011,
CHCG_PERF_SEL_STALL_GUS_GL1              = 0x00000012,
CHCG_PERF_SEL_STALL_BUFFER_FULL          = 0x00000013,
CHCG_PERF_SEL_REQ_CLIENT0                = 0x00000014,
CHCG_PERF_SEL_REQ_CLIENT1                = 0x00000015,
CHCG_PERF_SEL_REQ_CLIENT2                = 0x00000016,
CHCG_PERF_SEL_REQ_CLIENT3                = 0x00000017,
CHCG_PERF_SEL_REQ_CLIENT4                = 0x00000018,
CHCG_PERF_SEL_REQ_CLIENT5                = 0x00000019,
CHCG_PERF_SEL_REQ_CLIENT6                = 0x0000001a,
CHCG_PERF_SEL_REQ_CLIENT7                = 0x0000001b,
CHCG_PERF_SEL_REQ_CLIENT8                = 0x0000001c,
CHCG_PERF_SEL_REQ_CLIENT9                = 0x0000001d,
CHCG_PERF_SEL_REQ_CLIENT10               = 0x0000001e,
CHCG_PERF_SEL_REQ_CLIENT11               = 0x0000001f,
CHCG_PERF_SEL_REQ_CLIENT12               = 0x00000020,
CHCG_PERF_SEL_REQ_CLIENT13               = 0x00000021,
CHCG_PERF_SEL_REQ_CLIENT14               = 0x00000022,
CHCG_PERF_SEL_REQ_CLIENT15               = 0x00000023,
CHCG_PERF_SEL_REQ_CLIENT16               = 0x00000024,
CHCG_PERF_SEL_REQ_CLIENT17               = 0x00000025,
CHCG_PERF_SEL_REQ_CLIENT18               = 0x00000026,
CHCG_PERF_SEL_REQ_CLIENT19               = 0x00000027,
CHCG_PERF_SEL_REQ_CLIENT20               = 0x00000028,
CHCG_PERF_SEL_REQ_CLIENT21               = 0x00000029,
CHCG_PERF_SEL_REQ_CLIENT22               = 0x0000002a,
CHCG_PERF_SEL_REQ_CLIENT23               = 0x0000002b,
} CHCG_PERF_SEL;

/*
 * CHC_PERF_SEL enum
 */

typedef enum CHC_PERF_SEL {
CHC_PERF_SEL_CYCLE                       = 0x00000000,
CHC_PERF_SEL_BUSY                        = 0x00000001,
CHC_PERF_SEL_STARVE                      = 0x00000002,
CHC_PERF_SEL_ARB_RET_LEVEL               = 0x00000003,
CHC_PERF_SEL_GL2_REQ_READ_LATENCY        = 0x00000004,
CHC_PERF_SEL_GL2_REQ_WRITE_LATENCY       = 0x00000005,
CHC_PERF_SEL_REQ                         = 0x00000006,
CHC_PERF_SEL_REQ_ATOMIC_WITH_RET         = 0x00000007,
CHC_PERF_SEL_REQ_ATOMIC_WITHOUT_RET      = 0x00000008,
CHC_PERF_SEL_REQ_NOP_ACK                 = 0x00000009,
CHC_PERF_SEL_REQ_NOP_RTN0                = 0x0000000a,
CHC_PERF_SEL_REQ_READ                    = 0x0000000b,
CHC_PERF_SEL_REQ_READ_128B               = 0x0000000c,
CHC_PERF_SEL_REQ_READ_32B                = 0x0000000d,
CHC_PERF_SEL_REQ_READ_64B                = 0x0000000e,
CHC_PERF_SEL_REQ_WRITE                   = 0x0000000f,
CHC_PERF_SEL_REQ_WRITE_32B               = 0x00000010,
CHC_PERF_SEL_REQ_WRITE_64B               = 0x00000011,
CHC_PERF_SEL_STALL_GL2_GL1               = 0x00000012,
CHC_PERF_SEL_STALL_BUFFER_FULL           = 0x00000013,
CHC_PERF_SEL_REQ_CLIENT0                 = 0x00000014,
CHC_PERF_SEL_REQ_CLIENT1                 = 0x00000015,
CHC_PERF_SEL_REQ_CLIENT2                 = 0x00000016,
CHC_PERF_SEL_REQ_CLIENT3                 = 0x00000017,
CHC_PERF_SEL_REQ_CLIENT4                 = 0x00000018,
CHC_PERF_SEL_REQ_CLIENT5                 = 0x00000019,
CHC_PERF_SEL_REQ_CLIENT6                 = 0x0000001a,
CHC_PERF_SEL_REQ_CLIENT7                 = 0x0000001b,
CHC_PERF_SEL_REQ_CLIENT8                 = 0x0000001c,
CHC_PERF_SEL_REQ_CLIENT9                 = 0x0000001d,
CHC_PERF_SEL_REQ_CLIENT10                = 0x0000001e,
CHC_PERF_SEL_REQ_CLIENT11                = 0x0000001f,
CHC_PERF_SEL_REQ_CLIENT12                = 0x00000020,
CHC_PERF_SEL_REQ_CLIENT13                = 0x00000021,
CHC_PERF_SEL_REQ_CLIENT14                = 0x00000022,
CHC_PERF_SEL_REQ_CLIENT15                = 0x00000023,
CHC_PERF_SEL_REQ_CLIENT16                = 0x00000024,
CHC_PERF_SEL_REQ_CLIENT17                = 0x00000025,
CHC_PERF_SEL_REQ_CLIENT18                = 0x00000026,
CHC_PERF_SEL_REQ_CLIENT19                = 0x00000027,
CHC_PERF_SEL_REQ_CLIENT20                = 0x00000028,
CHC_PERF_SEL_REQ_CLIENT21                = 0x00000029,
CHC_PERF_SEL_REQ_CLIENT22                = 0x0000002a,
CHC_PERF_SEL_REQ_CLIENT23                = 0x0000002b,
} CHC_PERF_SEL;

/*
 * GL1A_PERF_SEL enum
 */

typedef enum GL1A_PERF_SEL {
GL1A_PERF_SEL_BUSY                       = 0x00000000,
GL1A_PERF_SEL_STALL_GL1C0                = 0x00000001,
GL1A_PERF_SEL_STALL_GL1C1                = 0x00000002,
GL1A_PERF_SEL_STALL_GL1C2                = 0x00000003,
GL1A_PERF_SEL_STALL_GL1C3                = 0x00000004,
GL1A_PERF_SEL_REQUEST_GL1C0              = 0x00000005,
GL1A_PERF_SEL_REQUEST_GL1C1              = 0x00000006,
GL1A_PERF_SEL_REQUEST_GL1C2              = 0x00000007,
GL1A_PERF_SEL_REQUEST_GL1C3              = 0x00000008,
GL1A_PERF_SEL_WDS_32B_GL1C0              = 0x00000009,
GL1A_PERF_SEL_WDS_32B_GL1C1              = 0x0000000a,
GL1A_PERF_SEL_WDS_32B_GL1C2              = 0x0000000b,
GL1A_PERF_SEL_WDS_32B_GL1C3              = 0x0000000c,
GL1A_PERF_SEL_BURST_COUNT_GL1C0          = 0x0000000d,
GL1A_PERF_SEL_BURST_COUNT_GL1C1          = 0x0000000e,
GL1A_PERF_SEL_BURST_COUNT_GL1C2          = 0x0000000f,
GL1A_PERF_SEL_BURST_COUNT_GL1C3          = 0x00000010,
GL1A_PERF_SEL_ARB_REQUESTS               = 0x00000011,
GL1A_PERF_SEL_REQ_INFLIGHT_LEVEL         = 0x00000012,
GL1A_PERF_SEL_STALL_RET_CONFLICT_GL1C0   = 0x00000013,
GL1A_PERF_SEL_STALL_RET_CONFLICT_GL1C1   = 0x00000014,
GL1A_PERF_SEL_STALL_RET_CONFLICT_GL1C2   = 0x00000015,
GL1A_PERF_SEL_STALL_RET_CONFLICT_GL1C3   = 0x00000016,
GL1A_PERF_SEL_CYCLE                      = 0x00000017,
} GL1A_PERF_SEL;

/*
 * GL1C_PERF_SEL enum
 */

typedef enum GL1C_PERF_SEL {
GL1C_PERF_SEL_CYCLE                      = 0x00000000,
GL1C_PERF_SEL_BUSY                       = 0x00000001,
GL1C_PERF_SEL_STARVE                     = 0x00000002,
GL1C_PERF_SEL_ARB_RET_LEVEL              = 0x00000003,
GL1C_PERF_SEL_GL2_REQ_READ               = 0x00000004,
GL1C_PERF_SEL_GL2_REQ_READ_128B          = 0x00000005,
GL1C_PERF_SEL_GL2_REQ_READ_32B           = 0x00000006,
GL1C_PERF_SEL_GL2_REQ_READ_64B           = 0x00000007,
GL1C_PERF_SEL_GL2_REQ_READ_LATENCY       = 0x00000008,
GL1C_PERF_SEL_GL2_REQ_WRITE              = 0x00000009,
GL1C_PERF_SEL_GL2_REQ_WRITE_32B          = 0x0000000a,
GL1C_PERF_SEL_GL2_REQ_WRITE_64B          = 0x0000000b,
GL1C_PERF_SEL_GL2_REQ_WRITE_LATENCY      = 0x0000000c,
GL1C_PERF_SEL_GL2_REQ_PREFETCH           = 0x0000000d,
GL1C_PERF_SEL_REQ                        = 0x0000000e,
GL1C_PERF_SEL_REQ_ATOMIC_WITH_RET        = 0x0000000f,
GL1C_PERF_SEL_REQ_ATOMIC_WITHOUT_RET     = 0x00000010,
GL1C_PERF_SEL_REQ_SHADER_INV             = 0x00000011,
GL1C_PERF_SEL_REQ_MISS                   = 0x00000012,
GL1C_PERF_SEL_REQ_NOP_ACK                = 0x00000013,
GL1C_PERF_SEL_REQ_NOP_RTN0               = 0x00000014,
GL1C_PERF_SEL_REQ_READ                   = 0x00000015,
GL1C_PERF_SEL_REQ_READ_128B              = 0x00000016,
GL1C_PERF_SEL_REQ_READ_32B               = 0x00000017,
GL1C_PERF_SEL_REQ_READ_64B               = 0x00000018,
GL1C_PERF_SEL_REQ_READ_POLICY_HIT_EVICT  = 0x00000019,
GL1C_PERF_SEL_REQ_READ_POLICY_HIT_LRU    = 0x0000001a,
GL1C_PERF_SEL_REQ_READ_POLICY_MISS_EVICT = 0x0000001b,
GL1C_PERF_SEL_REQ_WRITE                  = 0x0000001c,
GL1C_PERF_SEL_REQ_WRITE_32B              = 0x0000001d,
GL1C_PERF_SEL_REQ_WRITE_64B              = 0x0000001e,
GL1C_PERF_SEL_STALL_GL2_GL1              = 0x0000001f,
GL1C_PERF_SEL_STALL_LFIFO_FULL           = 0x00000020,
GL1C_PERF_SEL_STALL_NO_AVAILABLE_ACK_ALLOC = 0x00000021,
GL1C_PERF_SEL_STALL_NOTHING_REPLACEABLE  = 0x00000022,
GL1C_PERF_SEL_STALL_GCR_INV              = 0x00000023,
GL1C_PERF_SEL_STALL_VM                   = 0x00000024,
GL1C_PERF_SEL_REQ_CLIENT0                = 0x00000025,
GL1C_PERF_SEL_REQ_CLIENT1                = 0x00000026,
GL1C_PERF_SEL_REQ_CLIENT2                = 0x00000027,
GL1C_PERF_SEL_REQ_CLIENT3                = 0x00000028,
GL1C_PERF_SEL_REQ_CLIENT4                = 0x00000029,
GL1C_PERF_SEL_REQ_CLIENT5                = 0x0000002a,
GL1C_PERF_SEL_REQ_CLIENT6                = 0x0000002b,
GL1C_PERF_SEL_REQ_CLIENT7                = 0x0000002c,
GL1C_PERF_SEL_REQ_CLIENT8                = 0x0000002d,
GL1C_PERF_SEL_REQ_CLIENT9                = 0x0000002e,
GL1C_PERF_SEL_REQ_CLIENT10               = 0x0000002f,
GL1C_PERF_SEL_REQ_CLIENT11               = 0x00000030,
GL1C_PERF_SEL_REQ_CLIENT12               = 0x00000031,
GL1C_PERF_SEL_REQ_CLIENT13               = 0x00000032,
GL1C_PERF_SEL_REQ_CLIENT14               = 0x00000033,
GL1C_PERF_SEL_REQ_CLIENT15               = 0x00000034,
GL1C_PERF_SEL_REQ_CLIENT16               = 0x00000035,
GL1C_PERF_SEL_REQ_CLIENT17               = 0x00000036,
GL1C_PERF_SEL_REQ_CLIENT18               = 0x00000037,
GL1C_PERF_SEL_REQ_CLIENT19               = 0x00000038,
GL1C_PERF_SEL_REQ_CLIENT20               = 0x00000039,
GL1C_PERF_SEL_REQ_CLIENT21               = 0x0000003a,
GL1C_PERF_SEL_REQ_CLIENT22               = 0x0000003b,
GL1C_PERF_SEL_REQ_CLIENT23               = 0x0000003c,
GL1C_PERF_SEL_REQ_CLIENT24               = 0x0000003d,
GL1C_PERF_SEL_REQ_CLIENT25               = 0x0000003e,
GL1C_PERF_SEL_REQ_CLIENT26               = 0x0000003f,
GL1C_PERF_SEL_REQ_CLIENT27               = 0x00000040,
GL1C_PERF_SEL_UTCL0_REQUEST              = 0x00000041,
GL1C_PERF_SEL_UTCL0_TRANSLATION_HIT      = 0x00000042,
GL1C_PERF_SEL_UTCL0_TRANSLATION_MISS     = 0x00000043,
GL1C_PERF_SEL_UTCL0_PERMISSION_MISS      = 0x00000044,
GL1C_PERF_SEL_UTCL0_MISS_UNDER_MISS      = 0x00000045,
GL1C_PERF_SEL_UTCL0_LFIFO_FULL           = 0x00000046,
GL1C_PERF_SEL_UTCL0_STALL_INFLIGHT_MAX   = 0x00000047,
GL1C_PERF_SEL_UTCL0_STALL_LFIFO_NOT_RES  = 0x00000048,
GL1C_PERF_SEL_UTCL0_STALL_LRU_INFLIGHT   = 0x00000049,
GL1C_PERF_SEL_UTCL0_STALL_MISSFIFO_FULL  = 0x0000004a,
GL1C_PERF_SEL_UTCL0_STALL_MULTI_MISS     = 0x0000004b,
GL1C_PERF_SEL_UTCL0_STALL_UTCL1_REQ_OUT_OF_CREDITS = 0x0000004c,
GL1C_PERF_SEL_UTCL0_UTCL1_PERM_FAULT     = 0x0000004d,
GL1C_PERF_SEL_CLIENT_UTCL0_INFLIGHT      = 0x0000004e,
GL1C_PERF_SEL_UTCL0_UTCL1_INFLIGHT       = 0x0000004f,
GL1C_PERF_SEL_UTCL0_INTERNAL_RETRY_REQ   = 0x00000050,
GL1C_PERF_SEL_UTCL0_UTCL1_XNACK_RETRY_FAULT = 0x00000051,
GL1C_PERF_SEL_UTCL0_UTCL1_XNACK_PRT_FAULT = 0x00000052,
GL1C_PERF_SEL_UTCL0_UTCL1_XNACK_NO_RETRY_FAULT = 0x00000053,
} GL1C_PERF_SEL;

/*******************************************************
 * GL1H Enums
 *******************************************************/

/*
 * GL1H_REQ_PERF_SEL enum
 */

typedef enum GL1H_REQ_PERF_SEL {
GL1H_REQ_PERF_SEL_BUSY                   = 0x00000000,
GL1H_REQ_PERF_SEL_STALL_GL1_0            = 0x00000001,
GL1H_REQ_PERF_SEL_STALL_GL1_1            = 0x00000002,
GL1H_REQ_PERF_SEL_REQUEST_GL1_0          = 0x00000003,
GL1H_REQ_PERF_SEL_REQUEST_GL1_1          = 0x00000004,
GL1H_REQ_PERF_SEL_WDS_32B_GL1_0          = 0x00000005,
GL1H_REQ_PERF_SEL_WDS_32B_GL1_1          = 0x00000006,
GL1H_REQ_PERF_SEL_BURST_COUNT_GL1_0      = 0x00000007,
GL1H_REQ_PERF_SEL_BURST_COUNT_GL1_1      = 0x00000008,
GL1H_REQ_PERF_SEL_ARB_REQUESTS           = 0x00000009,
GL1H_REQ_PERF_SEL_REQ_INFLIGHT_LEVEL     = 0x0000000a,
GL1H_REQ_PERF_SEL_CYCLE                  = 0x0000000b,
} GL1H_REQ_PERF_SEL;

/*******************************************************
 * TA Enums
 *******************************************************/

/*
 * TA_PERFCOUNT_SEL enum
 */

typedef enum TA_PERFCOUNT_SEL {
TA_PERF_SEL_NULL                         = 0x00000000,
TA_PERF_SEL_image_sampler_has_offset_instructions = 0x00000001,
TA_PERF_SEL_image_sampler_has_bias_instructions = 0x00000002,
TA_PERF_SEL_image_sampler_has_reference_instructions = 0x00000003,
TA_PERF_SEL_image_sampler_has_ds_instructions = 0x00000004,
TA_PERF_SEL_image_sampler_has_dt_instructions = 0x00000005,
TA_PERF_SEL_image_sampler_has_dr_instructions = 0x00000006,
TA_PERF_SEL_gradient_busy                = 0x00000007,
TA_PERF_SEL_gradient_fifo_busy           = 0x00000008,
TA_PERF_SEL_lod_busy                     = 0x00000009,
TA_PERF_SEL_lod_fifo_busy                = 0x0000000a,
TA_PERF_SEL_addresser_busy               = 0x0000000b,
TA_PERF_SEL_addresser_fifo_busy          = 0x0000000c,
TA_PERF_SEL_aligner_busy                 = 0x0000000d,
TA_PERF_SEL_write_path_busy              = 0x0000000e,
TA_PERF_SEL_ta_busy                      = 0x0000000f,
TA_PERF_SEL_image_sampler_1_input_vgpr_instructions = 0x00000010,
TA_PERF_SEL_image_sampler_2_input_vgpr_instructions = 0x00000011,
TA_PERF_SEL_image_sampler_3_input_vgpr_instructions = 0x00000012,
TA_PERF_SEL_image_sampler_4_input_vgpr_instructions = 0x00000013,
TA_PERF_SEL_image_sampler_5_input_vgpr_instructions = 0x00000014,
TA_PERF_SEL_image_sampler_6_input_vgpr_instructions = 0x00000015,
TA_PERF_SEL_image_sampler_7_input_vgpr_instructions = 0x00000016,
TA_PERF_SEL_image_sampler_8_input_vgpr_instructions = 0x00000017,
TA_PERF_SEL_image_sampler_9_input_vgpr_instructions = 0x00000018,
TA_PERF_SEL_image_sampler_10_input_vgpr_instructions = 0x00000019,
TA_PERF_SEL_image_sampler_11_input_vgpr_instructions = 0x0000001a,
TA_PERF_SEL_image_sampler_12_input_vgpr_instructions = 0x0000001b,
TA_PERF_SEL_image_sampler_has_t_instructions = 0x0000001c,
TA_PERF_SEL_image_sampler_has_r_instructions = 0x0000001d,
TA_PERF_SEL_image_sampler_has_q_instructions = 0x0000001e,
TA_PERF_SEL_total_wavefronts             = 0x00000020,
TA_PERF_SEL_gradient_cycles              = 0x00000021,
TA_PERF_SEL_walker_cycles                = 0x00000022,
TA_PERF_SEL_aligner_cycles               = 0x00000023,
TA_PERF_SEL_image_wavefronts             = 0x00000024,
TA_PERF_SEL_image_read_wavefronts        = 0x00000025,
TA_PERF_SEL_image_store_wavefronts       = 0x00000026,
TA_PERF_SEL_image_atomic_wavefronts      = 0x00000027,
TA_PERF_SEL_image_sampler_total_cycles   = 0x00000028,
TA_PERF_SEL_image_nosampler_total_cycles = 0x00000029,
TA_PERF_SEL_flat_total_cycles            = 0x0000002a,
TA_PERF_SEL_bvh_total_cycles             = 0x0000002b,
TA_PERF_SEL_buffer_wavefronts            = 0x0000002c,
TA_PERF_SEL_buffer_load_wavefronts       = 0x0000002d,
TA_PERF_SEL_buffer_store_wavefronts      = 0x0000002e,
TA_PERF_SEL_buffer_atomic_wavefronts     = 0x0000002f,
TA_PERF_SEL_buffer_total_cycles          = 0x00000031,
TA_PERF_SEL_buffer_1_address_input_vgpr_instructions = 0x00000032,
TA_PERF_SEL_buffer_2_address_input_vgpr_instructions = 0x00000033,
TA_PERF_SEL_buffer_has_index_instructions = 0x00000034,
TA_PERF_SEL_buffer_has_offset_instructions = 0x00000035,
TA_PERF_SEL_addr_stalled_by_tc_cycles    = 0x00000036,
TA_PERF_SEL_addr_stalled_by_td_cycles    = 0x00000037,
TA_PERF_SEL_image_sampler_wavefronts     = 0x00000038,
TA_PERF_SEL_addresser_stalled_by_aligner_only_cycles = 0x00000039,
TA_PERF_SEL_addresser_stalled_cycles     = 0x0000003a,
TA_PERF_SEL_aniso_stalled_by_addresser_only_cycles = 0x0000003b,
TA_PERF_SEL_aniso_stalled_cycles         = 0x0000003c,
TA_PERF_SEL_deriv_stalled_by_aniso_only_cycles = 0x0000003d,
TA_PERF_SEL_deriv_stalled_cycles         = 0x0000003e,
TA_PERF_SEL_aniso_gt1_cycle_quads        = 0x0000003f,
TA_PERF_SEL_color_1_cycle_quads          = 0x00000040,
TA_PERF_SEL_color_2_cycle_quads          = 0x00000041,
TA_PERF_SEL_color_3_cycle_quads          = 0x00000042,
TA_PERF_SEL_mip_1_cycle_quads            = 0x00000044,
TA_PERF_SEL_mip_2_cycle_quads            = 0x00000045,
TA_PERF_SEL_vol_1_cycle_quads            = 0x00000046,
TA_PERF_SEL_vol_2_cycle_quads            = 0x00000047,
TA_PERF_SEL_sampler_op_quads             = 0x00000048,
TA_PERF_SEL_mipmap_lod_0_samples         = 0x00000049,
TA_PERF_SEL_mipmap_lod_1_samples         = 0x0000004a,
TA_PERF_SEL_mipmap_lod_2_samples         = 0x0000004b,
TA_PERF_SEL_mipmap_lod_3_samples         = 0x0000004c,
TA_PERF_SEL_mipmap_lod_4_samples         = 0x0000004d,
TA_PERF_SEL_mipmap_lod_5_samples         = 0x0000004e,
TA_PERF_SEL_mipmap_lod_6_samples         = 0x0000004f,
TA_PERF_SEL_mipmap_lod_7_samples         = 0x00000050,
TA_PERF_SEL_mipmap_lod_8_samples         = 0x00000051,
TA_PERF_SEL_mipmap_lod_9_samples         = 0x00000052,
TA_PERF_SEL_mipmap_lod_10_samples        = 0x00000053,
TA_PERF_SEL_mipmap_lod_11_samples        = 0x00000054,
TA_PERF_SEL_mipmap_lod_12_samples        = 0x00000055,
TA_PERF_SEL_mipmap_lod_13_samples        = 0x00000056,
TA_PERF_SEL_mipmap_lod_14_samples        = 0x00000057,
TA_PERF_SEL_mipmap_invalid_samples       = 0x00000058,
TA_PERF_SEL_aniso_1_cycle_quads          = 0x00000059,
TA_PERF_SEL_aniso_2_cycle_quads          = 0x0000005a,
TA_PERF_SEL_aniso_4_cycle_quads          = 0x0000005b,
TA_PERF_SEL_aniso_6_cycle_quads          = 0x0000005c,
TA_PERF_SEL_aniso_8_cycle_quads          = 0x0000005d,
TA_PERF_SEL_aniso_10_cycle_quads         = 0x0000005e,
TA_PERF_SEL_aniso_12_cycle_quads         = 0x0000005f,
TA_PERF_SEL_aniso_14_cycle_quads         = 0x00000060,
TA_PERF_SEL_aniso_16_cycle_quads         = 0x00000061,
TA_PERF_SEL_store_write_data_input_cycles = 0x00000062,
TA_PERF_SEL_store_write_data_output_cycles = 0x00000063,
TA_PERF_SEL_flat_wavefronts              = 0x00000064,
TA_PERF_SEL_flat_load_wavefronts         = 0x00000065,
TA_PERF_SEL_flat_store_wavefronts        = 0x00000066,
TA_PERF_SEL_flat_atomic_wavefronts       = 0x00000067,
TA_PERF_SEL_flat_1_address_input_vgpr_instructions = 0x00000068,
TA_PERF_SEL_register_clk_valid_cycles    = 0x00000069,
TA_PERF_SEL_non_harvestable_clk_enabled_cycles = 0x0000006a,
TA_PERF_SEL_harvestable_clk_enabled_cycles = 0x0000006b,
TA_PERF_SEL_harvestable_register_clk_enabled_cycles = 0x0000006c,
TA_PERF_SEL_boundary_non_harvestable_clk_enabled_cycles = 0x0000006d,
TA_PERF_SEL_boundary_harvestable_clk_enabled_cycles = 0x0000006e,
TA_PERF_SEL_store_2_write_data_vgpr_instructions = 0x00000072,
TA_PERF_SEL_store_3_write_data_vgpr_instructions = 0x00000073,
TA_PERF_SEL_store_4_write_data_vgpr_instructions = 0x00000074,
TA_PERF_SEL_store_has_x_instructions     = 0x00000075,
TA_PERF_SEL_store_has_y_instructions     = 0x00000076,
TA_PERF_SEL_store_has_z_instructions     = 0x00000077,
TA_PERF_SEL_store_has_w_instructions     = 0x00000078,
TA_PERF_SEL_image_nosampler_has_t_instructions = 0x00000079,
TA_PERF_SEL_image_nosampler_has_r_instructions = 0x0000007a,
TA_PERF_SEL_image_nosampler_has_q_instructions = 0x0000007b,
TA_PERF_SEL_image_nosampler_1_address_input_vgpr_instructions = 0x0000007c,
TA_PERF_SEL_image_nosampler_2_address_input_vgpr_instructions = 0x0000007d,
TA_PERF_SEL_image_nosampler_3_address_input_vgpr_instructions = 0x0000007e,
TA_PERF_SEL_image_nosampler_4_address_input_vgpr_instructions = 0x0000007f,
TA_PERF_SEL_in_busy                      = 0x00000080,
TA_PERF_SEL_in_fifos_busy                = 0x00000081,
TA_PERF_SEL_in_cfifo_busy                = 0x00000082,
TA_PERF_SEL_in_qfifo_busy                = 0x00000083,
TA_PERF_SEL_in_wfifo_busy                = 0x00000084,
TA_PERF_SEL_in_rfifo_busy                = 0x00000085,
TA_PERF_SEL_bf_busy                      = 0x00000086,
TA_PERF_SEL_ns_busy                      = 0x00000087,
TA_PERF_SEL_smp_busy_ns_idle             = 0x00000088,
TA_PERF_SEL_smp_idle_ns_busy             = 0x00000089,
TA_PERF_SEL_vmemcmd_cycles               = 0x00000090,
TA_PERF_SEL_vmemreq_cycles               = 0x00000091,
TA_PERF_SEL_in_waiting_on_req_cycles     = 0x00000092,
TA_PERF_SEL_in_addr_cycles               = 0x00000096,
TA_PERF_SEL_in_data_cycles               = 0x00000097,
TA_PERF_SEL_latency_ram_weights_written_cycles = 0x0000009a,
TA_PERF_SEL_latency_ram_ws_required_quads = 0x0000009b,
TA_PERF_SEL_latency_ram_whv_required_quads = 0x0000009c,
TA_PERF_SEL_latency_ram_ws_required_instructions = 0x0000009d,
TA_PERF_SEL_latency_ram_whv_required_instructions = 0x0000009e,
TA_PERF_SEL_latency_ram_ref_required_instructions = 0x0000009f,
TA_PERF_SEL_point_sampled_quads          = 0x000000a0,
TA_PERF_SEL_atomic_2_write_data_vgpr_instructions = 0x000000a2,
TA_PERF_SEL_atomic_4_write_data_vgpr_instructions = 0x000000a3,
TA_PERF_SEL_atomic_write_data_input_cycles = 0x000000a4,
TA_PERF_SEL_atomic_write_data_output_cycles = 0x000000a5,
TA_PERF_SEL_num_unlit_nodes_ta_opt       = 0x000000ad,
TA_PERF_SEL_num_nodes_invalidated_due_to_bad_input = 0x000000ae,
TA_PERF_SEL_num_nodes_invalidated_due_to_oob = 0x000000af,
TA_PERF_SEL_num_of_bvh_valid_first_tri   = 0x000000b0,
TA_PERF_SEL_num_of_bvh_valid_second_tri  = 0x000000b1,
TA_PERF_SEL_num_of_bvh_valid_third_tri   = 0x000000b2,
TA_PERF_SEL_num_of_bvh_valid_fourth_tri  = 0x000000b3,
TA_PERF_SEL_num_of_bvh_valid_fp16_box    = 0x000000b4,
TA_PERF_SEL_num_of_bvh_valid_fp32_box    = 0x000000b5,
TA_PERF_SEL_num_of_bvh_invalidated_first_tri = 0x000000b6,
TA_PERF_SEL_num_of_bvh_invalidated_second_tri = 0x000000b7,
TA_PERF_SEL_num_of_bvh_invalidated_third_tri = 0x000000b8,
TA_PERF_SEL_num_of_bvh_invalidated_fourth_tri = 0x000000b9,
TA_PERF_SEL_num_of_bvh_invalidated_fp16_box = 0x000000ba,
TA_PERF_SEL_num_of_bvh_invalidated_fp32_box = 0x000000bb,
TA_PERF_SEL_image_bvh_8_input_vgpr_instructions = 0x000000bc,
TA_PERF_SEL_image_bvh_9_input_vgpr_instructions = 0x000000bd,
TA_PERF_SEL_image_bvh_11_input_vgpr_instructions = 0x000000be,
TA_PERF_SEL_image_bvh_12_input_vgpr_instructions = 0x000000bf,
TA_PERF_SEL_image_sampler_1_op_burst     = 0x000000c0,
TA_PERF_SEL_image_sampler_2to3_op_burst  = 0x000000c1,
TA_PERF_SEL_image_sampler_4to7_op_burst  = 0x000000c2,
TA_PERF_SEL_image_sampler_ge8_op_burst   = 0x000000c3,
TA_PERF_SEL_image_linked_1_op_burst      = 0x000000c4,
TA_PERF_SEL_image_linked_2to3_op_burst   = 0x000000c5,
TA_PERF_SEL_image_linked_4to7_op_burst   = 0x000000c6,
TA_PERF_SEL_image_linked_ge8_op_burst    = 0x000000c7,
TA_PERF_SEL_image_bvh_1_op_burst         = 0x000000c8,
TA_PERF_SEL_image_bvh_2to3_op_burst      = 0x000000c9,
TA_PERF_SEL_image_bvh_4to7_op_burst      = 0x000000ca,
TA_PERF_SEL_image_bvh_ge8_op_burst       = 0x000000cb,
TA_PERF_SEL_image_nosampler_1_op_burst   = 0x000000cc,
TA_PERF_SEL_image_nosampler_2to3_op_burst = 0x000000cd,
TA_PERF_SEL_image_nosampler_4to31_op_burst = 0x000000ce,
TA_PERF_SEL_image_nosampler_ge32_op_burst = 0x000000cf,
TA_PERF_SEL_buffer_flat_1_op_burst       = 0x000000d0,
TA_PERF_SEL_buffer_flat_2to3_op_burst    = 0x000000d1,
TA_PERF_SEL_buffer_flat_4to31_op_burst   = 0x000000d2,
TA_PERF_SEL_buffer_flat_ge32_op_burst    = 0x000000d3,
TA_PERF_SEL_write_1_op_burst             = 0x000000d4,
TA_PERF_SEL_write_2to3_op_burst          = 0x000000d5,
TA_PERF_SEL_write_4to31_op_burst         = 0x000000d6,
TA_PERF_SEL_write_ge32_op_burst          = 0x000000d7,
TA_PERF_SEL_ibubble_1_cycle_burst        = 0x000000d8,
TA_PERF_SEL_ibubble_2to3_cycle_burst     = 0x000000d9,
TA_PERF_SEL_ibubble_4to15_cycle_burst    = 0x000000da,
TA_PERF_SEL_ibubble_16to31_cycle_burst   = 0x000000db,
TA_PERF_SEL_ibubble_32to63_cycle_burst   = 0x000000dc,
TA_PERF_SEL_ibubble_ge64_cycle_burst     = 0x000000dd,
TA_PERF_SEL_sampler_clk_valid_cycles     = 0x000000e0,
TA_PERF_SEL_nonsampler_clk_valid_cycles  = 0x000000e1,
TA_PERF_SEL_buffer_flat_clk_valid_cycles = 0x000000e2,
TA_PERF_SEL_write_data_clk_valid_cycles  = 0x000000e3,
TA_PERF_SEL_gradient_clk_valid_cycles    = 0x000000e4,
TA_PERF_SEL_lod_aniso_clk_valid_cycles   = 0x000000e5,
TA_PERF_SEL_sampler_addressing_clk_valid_cycles = 0x000000e6,
TA_PERF_SEL_sync_sampler_sstate_fifo_clk_valid_cycles = 0x000000e7,
TA_PERF_SEL_sync_sampler_cstate_fifo_clk_valid_cycles = 0x000000e8,
TA_PERF_SEL_sync_nonsampler_fifo_clk_valid_cycles = 0x000000e9,
TA_PERF_SEL_aligner_clk_valid_cycles     = 0x000000ea,
TA_PERF_SEL_tcreq_clk_valid_cycles       = 0x000000eb,
} TA_PERFCOUNT_SEL;

/*
 * TEX_BC_SWIZZLE enum
 */

typedef enum TEX_BC_SWIZZLE {
TEX_BC_Swizzle_XYZW                      = 0x00000000,
TEX_BC_Swizzle_XWYZ                      = 0x00000001,
TEX_BC_Swizzle_WZYX                      = 0x00000002,
TEX_BC_Swizzle_WXYZ                      = 0x00000003,
TEX_BC_Swizzle_ZYXW                      = 0x00000004,
TEX_BC_Swizzle_YXWZ                      = 0x00000005,
} TEX_BC_SWIZZLE;

/*
 * TEX_BORDER_COLOR_TYPE enum
 */

typedef enum TEX_BORDER_COLOR_TYPE {
TEX_BorderColor_TransparentBlack         = 0x00000000,
TEX_BorderColor_OpaqueBlack              = 0x00000001,
TEX_BorderColor_OpaqueWhite              = 0x00000002,
TEX_BorderColor_Register                 = 0x00000003,
} TEX_BORDER_COLOR_TYPE;

/*
 * TEX_CHROMA_KEY enum
 */

typedef enum TEX_CHROMA_KEY {
TEX_ChromaKey_Disabled                   = 0x00000000,
TEX_ChromaKey_Kill                       = 0x00000001,
TEX_ChromaKey_Blend                      = 0x00000002,
TEX_ChromaKey_RESERVED_3                 = 0x00000003,
} TEX_CHROMA_KEY;

/*
 * TEX_CLAMP enum
 */

typedef enum TEX_CLAMP {
TEX_Clamp_Repeat                         = 0x00000000,
TEX_Clamp_Mirror                         = 0x00000001,
TEX_Clamp_ClampToLast                    = 0x00000002,
TEX_Clamp_MirrorOnceToLast               = 0x00000003,
TEX_Clamp_ClampHalfToBorder              = 0x00000004,
TEX_Clamp_MirrorOnceHalfToBorder         = 0x00000005,
TEX_Clamp_ClampToBorder                  = 0x00000006,
TEX_Clamp_MirrorOnceToBorder             = 0x00000007,
} TEX_CLAMP;

/*
 * TEX_COORD_TYPE enum
 */

typedef enum TEX_COORD_TYPE {
TEX_CoordType_Unnormalized               = 0x00000000,
TEX_CoordType_Normalized                 = 0x00000001,
} TEX_COORD_TYPE;

/*
 * TEX_DEPTH_COMPARE_FUNCTION enum
 */

typedef enum TEX_DEPTH_COMPARE_FUNCTION {
TEX_DepthCompareFunction_Never           = 0x00000000,
TEX_DepthCompareFunction_Less            = 0x00000001,
TEX_DepthCompareFunction_Equal           = 0x00000002,
TEX_DepthCompareFunction_LessEqual       = 0x00000003,
TEX_DepthCompareFunction_Greater         = 0x00000004,
TEX_DepthCompareFunction_NotEqual        = 0x00000005,
TEX_DepthCompareFunction_GreaterEqual    = 0x00000006,
TEX_DepthCompareFunction_Always          = 0x00000007,
} TEX_DEPTH_COMPARE_FUNCTION;

/*
 * TEX_FORMAT_COMP enum
 */

typedef enum TEX_FORMAT_COMP {
TEX_FormatComp_Unsigned                  = 0x00000000,
TEX_FormatComp_Signed                    = 0x00000001,
TEX_FormatComp_UnsignedBiased            = 0x00000002,
TEX_FormatComp_RESERVED_3                = 0x00000003,
} TEX_FORMAT_COMP;

/*
 * TEX_MAX_ANISO_RATIO enum
 */

typedef enum TEX_MAX_ANISO_RATIO {
TEX_MaxAnisoRatio_1to1                   = 0x00000000,
TEX_MaxAnisoRatio_2to1                   = 0x00000001,
TEX_MaxAnisoRatio_4to1                   = 0x00000002,
TEX_MaxAnisoRatio_8to1                   = 0x00000003,
TEX_MaxAnisoRatio_16to1                  = 0x00000004,
TEX_MaxAnisoRatio_RESERVED_5             = 0x00000005,
TEX_MaxAnisoRatio_RESERVED_6             = 0x00000006,
TEX_MaxAnisoRatio_RESERVED_7             = 0x00000007,
} TEX_MAX_ANISO_RATIO;

/*
 * TEX_MIP_FILTER enum
 */

typedef enum TEX_MIP_FILTER {
TEX_MipFilter_None                       = 0x00000000,
TEX_MipFilter_Point                      = 0x00000001,
TEX_MipFilter_Linear                     = 0x00000002,
TEX_MipFilter_Point_Aniso_Adj            = 0x00000003,
} TEX_MIP_FILTER;

/*
 * TEX_REQUEST_SIZE enum
 */

typedef enum TEX_REQUEST_SIZE {
TEX_RequestSize_32B                      = 0x00000000,
TEX_RequestSize_64B                      = 0x00000001,
TEX_RequestSize_128B                     = 0x00000002,
TEX_RequestSize_2X64B                    = 0x00000003,
} TEX_REQUEST_SIZE;

/*
 * TEX_SAMPLER_TYPE enum
 */

typedef enum TEX_SAMPLER_TYPE {
TEX_SamplerType_Invalid                  = 0x00000000,
TEX_SamplerType_Valid                    = 0x00000001,
} TEX_SAMPLER_TYPE;

/*
 * TEX_XY_FILTER enum
 */

typedef enum TEX_XY_FILTER {
TEX_XYFilter_Point                       = 0x00000000,
TEX_XYFilter_Linear                      = 0x00000001,
TEX_XYFilter_AnisoPoint                  = 0x00000002,
TEX_XYFilter_AnisoLinear                 = 0x00000003,
} TEX_XY_FILTER;

/*
 * TEX_Z_FILTER enum
 */

typedef enum TEX_Z_FILTER {
TEX_ZFilter_None                         = 0x00000000,
TEX_ZFilter_Point                        = 0x00000001,
TEX_ZFilter_Linear                       = 0x00000002,
TEX_ZFilter_RESERVED_3                   = 0x00000003,
} TEX_Z_FILTER;

/*
 * TVX_TYPE enum
 */

typedef enum TVX_TYPE {
TVX_Type_InvalidTextureResource          = 0x00000000,
TVX_Type_InvalidVertexBuffer             = 0x00000001,
TVX_Type_ValidTextureResource            = 0x00000002,
TVX_Type_ValidVertexBuffer               = 0x00000003,
} TVX_TYPE;

/*******************************************************
 * TCP Enums
 *******************************************************/

/*
 * TA_TC_ADDR_MODES enum
 */

typedef enum TA_TC_ADDR_MODES {
TA_TC_ADDR_MODE_DEFAULT                  = 0x00000000,
TA_TC_ADDR_MODE_COMP0                    = 0x00000001,
TA_TC_ADDR_MODE_COMP1                    = 0x00000002,
TA_TC_ADDR_MODE_COMP2                    = 0x00000003,
TA_TC_ADDR_MODE_COMP3                    = 0x00000004,
TA_TC_ADDR_MODE_UNALIGNED                = 0x00000005,
TA_TC_ADDR_MODE_BORDER_COLOR             = 0x00000006,
} TA_TC_ADDR_MODES;

/*
 * TA_TC_REQ_MODES enum
 */

typedef enum TA_TC_REQ_MODES {
TA_TC_REQ_MODE_BORDER                    = 0x00000000,
TA_TC_REQ_MODE_TEX2                      = 0x00000001,
TA_TC_REQ_MODE_TEX1                      = 0x00000002,
TA_TC_REQ_MODE_TEX0                      = 0x00000003,
TA_TC_REQ_MODE_NORMAL                    = 0x00000004,
TA_TC_REQ_MODE_DWORD                     = 0x00000005,
TA_TC_REQ_MODE_BYTE                      = 0x00000006,
TA_TC_REQ_MODE_BYTE_NV                   = 0x00000007,
} TA_TC_REQ_MODES;

/*
 * TCP_CACHE_POLICIES enum
 */

typedef enum TCP_CACHE_POLICIES {
TCP_CACHE_POLICY_MISS_LRU                = 0x00000000,
TCP_CACHE_POLICY_MISS_EVICT              = 0x00000001,
TCP_CACHE_POLICY_HIT_LRU                 = 0x00000002,
TCP_CACHE_POLICY_HIT_EVICT               = 0x00000003,
} TCP_CACHE_POLICIES;

/*
 * TCP_CACHE_STORE_POLICIES enum
 */

typedef enum TCP_CACHE_STORE_POLICIES {
TCP_CACHE_STORE_POLICY_WT_LRU            = 0x00000000,
TCP_CACHE_STORE_POLICY_WT_EVICT          = 0x00000001,
} TCP_CACHE_STORE_POLICIES;

/*
 * TCP_DSM_DATA_SEL enum
 */

typedef enum TCP_DSM_DATA_SEL {
TCP_DSM_DISABLE                          = 0x00000000,
TCP_DSM_SEL0                             = 0x00000001,
TCP_DSM_SEL1                             = 0x00000002,
TCP_DSM_SEL_BOTH                         = 0x00000003,
} TCP_DSM_DATA_SEL;

/*
 * TCP_DSM_INJECT_SEL enum
 */

typedef enum TCP_DSM_INJECT_SEL {
TCP_DSM_INJECT_SEL0                      = 0x00000000,
TCP_DSM_INJECT_SEL1                      = 0x00000001,
TCP_DSM_INJECT_SEL2                      = 0x00000002,
TCP_DSM_INJECT_SEL3                      = 0x00000003,
} TCP_DSM_INJECT_SEL;

/*
 * TCP_DSM_SINGLE_WRITE enum
 */

typedef enum TCP_DSM_SINGLE_WRITE {
TCP_DSM_SINGLE_WRITE_DIS                 = 0x00000000,
TCP_DSM_SINGLE_WRITE_EN                  = 0x00000001,
} TCP_DSM_SINGLE_WRITE;

/*
 * TCP_OPCODE_TYPE enum
 */

typedef enum TCP_OPCODE_TYPE {
TCP_OPCODE_READ                          = 0x00000000,
TCP_OPCODE_WRITE                         = 0x00000001,
TCP_OPCODE_ATOMIC                        = 0x00000002,
TCP_OPCODE_INV                           = 0x00000003,
TCP_OPCODE_ATOMIC_CMPSWAP                = 0x00000004,
TCP_OPCODE_SAMPLER                       = 0x00000005,
TCP_OPCODE_LOAD                          = 0x00000006,
TCP_OPCODE_GATHERH                       = 0x00000007,
} TCP_OPCODE_TYPE;

/*
 * TCP_PERFCOUNT_SELECT enum
 */

typedef enum TCP_PERFCOUNT_SELECT {
TCP_PERF_SEL_GATE_EN1                    = 0x00000000,
TCP_PERF_SEL_GATE_EN2                    = 0x00000001,
TCP_PERF_SEL_TA_REQ                      = 0x00000002,
TCP_PERF_SEL_TA_REQ_STATE_READ           = 0x00000003,
TCP_PERF_SEL_TA_REQ_READ                 = 0x00000004,
TCP_PERF_SEL_TA_REQ_WRITE                = 0x00000005,
TCP_PERF_SEL_TA_REQ_ATOMIC_WITH_RET      = 0x00000006,
TCP_PERF_SEL_TA_REQ_ATOMIC_WITHOUT_RET   = 0x00000007,
TCP_PERF_SEL_TA_REQ_GL0_INV              = 0x00000008,
TCP_PERF_SEL_REQ                         = 0x00000009,
TCP_PERF_SEL_REQ_READ                    = 0x0000000a,
TCP_PERF_SEL_REQ_READ_HIT_EVICT          = 0x0000000b,
TCP_PERF_SEL_REQ_READ_HIT_LRU            = 0x0000000c,
TCP_PERF_SEL_REQ_READ_MISS_EVICT         = 0x0000000d,
TCP_PERF_SEL_REQ_WRITE                   = 0x0000000e,
TCP_PERF_SEL_REQ_WRITE_MISS_EVICT        = 0x0000000f,
TCP_PERF_SEL_REQ_WRITE_MISS_LRU          = 0x00000010,
TCP_PERF_SEL_REQ_NON_READ                = 0x00000011,
TCP_PERF_SEL_REQ_MISS                    = 0x00000012,
TCP_PERF_SEL_REQ_TAGBANK0_SET0           = 0x00000013,
TCP_PERF_SEL_REQ_TAGBANK0_SET1           = 0x00000014,
TCP_PERF_SEL_REQ_TAGBANK1_SET0           = 0x00000015,
TCP_PERF_SEL_REQ_TAGBANK1_SET1           = 0x00000016,
TCP_PERF_SEL_REQ_TAGBANK2_SET0           = 0x00000017,
TCP_PERF_SEL_REQ_TAGBANK2_SET1           = 0x00000018,
TCP_PERF_SEL_REQ_TAGBANK3_SET0           = 0x00000019,
TCP_PERF_SEL_REQ_TAGBANK3_SET1           = 0x0000001a,
TCP_PERF_SEL_REQ_MISS_TAGBANK0           = 0x0000001b,
TCP_PERF_SEL_REQ_MISS_TAGBANK1           = 0x0000001c,
TCP_PERF_SEL_REQ_MISS_TAGBANK2           = 0x0000001d,
TCP_PERF_SEL_REQ_MISS_TAGBANK3           = 0x0000001e,
TCP_PERF_SEL_GL1_REQ_READ                = 0x0000001f,
TCP_PERF_SEL_GL1_REQ_READ_128B           = 0x00000020,
TCP_PERF_SEL_GL1_REQ_READ_64B            = 0x00000021,
TCP_PERF_SEL_GL1_REQ_WRITE               = 0x00000022,
TCP_PERF_SEL_GL1_REQ_ATOMIC_WITH_RET     = 0x00000023,
TCP_PERF_SEL_GL1_REQ_ATOMIC_WITHOUT_RET  = 0x00000024,
TCP_PERF_SEL_GL1_READ_LATENCY            = 0x00000025,
TCP_PERF_SEL_GL1_WRITE_LATENCY           = 0x00000026,
TCP_PERF_SEL_TCP_LATENCY                 = 0x00000027,
TCP_PERF_SEL_TCP_TA_REQ_STALL            = 0x00000028,
TCP_PERF_SEL_TA_TCP_REQ_STARVE           = 0x00000029,
TCP_PERF_SEL_DATA_FIFO_STALL             = 0x0000002a,
TCP_PERF_SEL_LOD_STALL                   = 0x0000002b,
TCP_PERF_SEL_POWER_STALL                 = 0x0000002c,
TCP_PERF_SEL_ALLOC_STALL                 = 0x0000002d,
TCP_PERF_SEL_UNORDERED_MTYPE_STALL       = 0x0000002e,
TCP_PERF_SEL_READ_TAGCONFLICT_STALL      = 0x0000002f,
TCP_PERF_SEL_WRITE_TAGCONFLICT_STALL     = 0x00000030,
TCP_PERF_SEL_ATOMIC_TAGCONFLICT_STALL    = 0x00000031,
TCP_PERF_SEL_LFIFO_STALL                 = 0x00000032,
TCP_PERF_SEL_MEM_REQ_FIFO_STALL          = 0x00000033,
TCP_PERF_SEL_GL1_TCP_BACK_PRESSURE       = 0x00000034,
TCP_PERF_SEL_GL1_TCP_RDRET_STALL         = 0x00000035,
TCP_PERF_SEL_GL1_GRANT_READ_STALL        = 0x00000036,
TCP_PERF_SEL_GL1_PENDING_STALL           = 0x00000037,
TCP_PERF_SEL_OFIFO_INCOMPLETE_STALL      = 0x00000038,
TCP_PERF_SEL_OFIFO_AGE_ORDER_STALL       = 0x00000039,
TCP_PERF_SEL_TD_DATA_CYCLE_STALL         = 0x0000003a,
TCP_PERF_SEL_COMP_TEX_LOAD_STALL         = 0x0000003b,
TCP_PERF_SEL_READ_DATACONFLICT_STALL     = 0x0000003c,
TCP_PERF_SEL_WRITE_DATACONFLICT_STALL    = 0x0000003d,
TCP_PERF_SEL_TD_TCP_STALL                = 0x0000003e,
} TCP_PERFCOUNT_SELECT;

/*
 * TCP_WATCH_MODES enum
 */

typedef enum TCP_WATCH_MODES {
TCP_WATCH_MODE_READ                      = 0x00000000,
TCP_WATCH_MODE_NONREAD                   = 0x00000001,
TCP_WATCH_MODE_ATOMIC                    = 0x00000002,
TCP_WATCH_MODE_ALL                       = 0x00000003,
} TCP_WATCH_MODES;

/*******************************************************
 * TD Enums
 *******************************************************/

/*
 * TD_PERFCOUNT_SEL enum
 */

typedef enum TD_PERFCOUNT_SEL {
TD_PERF_SEL_none                         = 0x00000000,
TD_PERF_SEL_td_busy                      = 0x00000001,
TD_PERF_SEL_input_busy                   = 0x00000002,
TD_PERF_SEL_sampler_lerp_busy            = 0x00000003,
TD_PERF_SEL_sampler_out_busy             = 0x00000004,
TD_PERF_SEL_nofilter_busy                = 0x00000005,
TD_PERF_SEL_ray_tracing_bvh4_busy        = 0x00000006,
TD_PERF_SEL_sampler_core_sclk_en         = 0x00000007,
TD_PERF_SEL_sampler_preformatter_sclk_en = 0x00000008,
TD_PERF_SEL_sampler_bilerp_sclk_en       = 0x00000009,
TD_PERF_SEL_sampler_bypass_sclk_en       = 0x0000000a,
TD_PERF_SEL_sampler_minmax_sclk_en       = 0x0000000b,
TD_PERF_SEL_sampler_accum_sclk_en        = 0x0000000c,
TD_PERF_SEL_sampler_format_flt_sclk_en   = 0x0000000d,
TD_PERF_SEL_sampler_format_fxdpt_sclk_en = 0x0000000e,
TD_PERF_SEL_sampler_out_sclk_en          = 0x0000000f,
TD_PERF_SEL_nofilter_sclk_en             = 0x00000010,
TD_PERF_SEL_nofilter_d32_sclk_en         = 0x00000011,
TD_PERF_SEL_nofilter_d16_sclk_en         = 0x00000012,
TD_PERF_SEL_ray_tracing_bvh4_sclk_en     = 0x00000016,
TD_PERF_SEL_ray_tracing_bvh4_ip_sclk_en  = 0x00000017,
TD_PERF_SEL_ray_tracing_bvh4_box_sclk_en = 0x00000018,
TD_PERF_SEL_ray_tracing_bvh4_tri_sclk_en = 0x00000019,
TD_PERF_SEL_sampler_sclk_on_nofilter_sclk_off = 0x0000001a,
TD_PERF_SEL_nofilter_sclk_on_sampler_sclk_off = 0x0000001b,
TD_PERF_SEL_all_pipes_sclk_on_at_same_time = 0x0000001c,
TD_PERF_SEL_sampler_and_nofilter_sclk_on_bvh4_sclk_off = 0x0000001d,
TD_PERF_SEL_sampler_and_bvh4_sclk_on_nofilter_sclk_off = 0x0000001e,
TD_PERF_SEL_nofilter_and_bvh4_sclk_on_sampler_sclk_off = 0x0000001f,
TD_PERF_SEL_core_state_ram_max_cnt       = 0x00000020,
TD_PERF_SEL_core_state_rams_read         = 0x00000021,
TD_PERF_SEL_weight_data_rams_read        = 0x00000022,
TD_PERF_SEL_reference_data_rams_read     = 0x00000023,
TD_PERF_SEL_tc_td_ram_fifo_full          = 0x00000024,
TD_PERF_SEL_tc_td_ram_fifo_max_cnt       = 0x00000025,
TD_PERF_SEL_tc_td_data_fifo_full         = 0x00000026,
TD_PERF_SEL_input_state_fifo_full        = 0x00000027,
TD_PERF_SEL_ta_data_stall                = 0x00000028,
TD_PERF_SEL_tc_data_stall                = 0x00000029,
TD_PERF_SEL_tc_ram_stall                 = 0x0000002a,
TD_PERF_SEL_lds_stall                    = 0x0000002b,
TD_PERF_SEL_sampler_pkr_full             = 0x0000002c,
TD_PERF_SEL_sampler_pkr_full_due_to_arb  = 0x0000002d,
TD_PERF_SEL_nofilter_pkr_full            = 0x0000002e,
TD_PERF_SEL_nofilter_pkr_full_due_to_arb = 0x0000002f,
TD_PERF_SEL_ray_tracing_bvh4_pkr_full    = 0x00000030,
TD_PERF_SEL_ray_tracing_bvh4_pkr_full_due_to_arb = 0x00000031,
TD_PERF_SEL_gather4_instr                = 0x00000032,
TD_PERF_SEL_gather4h_instr               = 0x00000033,
TD_PERF_SEL_sample_instr                 = 0x00000036,
TD_PERF_SEL_sample_c_instr               = 0x00000037,
TD_PERF_SEL_load_instr                   = 0x00000038,
TD_PERF_SEL_ldfptr_instr                 = 0x00000039,
TD_PERF_SEL_write_ack_instr              = 0x0000003a,
TD_PERF_SEL_d16_en_instr                 = 0x0000003b,
TD_PERF_SEL_bypassLerp_instr             = 0x0000003c,
TD_PERF_SEL_min_max_filter_instr         = 0x0000003d,
TD_PERF_SEL_one_comp_return_instr        = 0x0000003e,
TD_PERF_SEL_two_comp_return_instr        = 0x0000003f,
TD_PERF_SEL_three_comp_return_instr      = 0x00000040,
TD_PERF_SEL_four_comp_return_instr       = 0x00000041,
TD_PERF_SEL_user_defined_border          = 0x00000042,
TD_PERF_SEL_white_border                 = 0x00000043,
TD_PERF_SEL_opaque_black_border          = 0x00000044,
TD_PERF_SEL_lod_warn_from_ta             = 0x00000045,
TD_PERF_SEL_instruction_dest_is_lds      = 0x00000046,
TD_PERF_SEL_td_cycling_of_nofilter_instr_2cycles = 0x00000047,
TD_PERF_SEL_td_cycling_of_nofilter_instr_4cycles = 0x00000048,
TD_PERF_SEL_tc_cycling_of_nofilter_instr_2cycles = 0x00000049,
TD_PERF_SEL_tc_cycling_of_nofilter_instr_4cycles = 0x0000004a,
TD_PERF_SEL_out_of_order_instr           = 0x0000004b,
TD_PERF_SEL_total_num_instr              = 0x0000004c,
TD_PERF_SEL_total_num_instr_with_perf_wdw = 0x0000004d,
TD_PERF_SEL_total_num_sampler_instr      = 0x0000004e,
TD_PERF_SEL_total_num_sampler_instr_with_perf_wdw = 0x0000004f,
TD_PERF_SEL_total_num_nofilter_instr     = 0x00000050,
TD_PERF_SEL_total_num_nofilter_instr_with_perf_wdw = 0x00000051,
TD_PERF_SEL_total_num_ray_tracing_bvh4_instr = 0x00000052,
TD_PERF_SEL_total_num_ray_tracing_bvh4_instr_with_perf_wdw = 0x00000053,
TD_PERF_SEL_mixmode_instr                = 0x00000054,
TD_PERF_SEL_mixmode_resource             = 0x00000055,
TD_PERF_SEL_status_packet                = 0x00000056,
TD_PERF_SEL_address_cmd_poison           = 0x00000057,
TD_PERF_SEL_data_poison                  = 0x00000058,
TD_PERF_SEL_done_scoreboard_max_stored_cnt = 0x00000059,
TD_PERF_SEL_done_scoreboard_max_waiting_cnt = 0x0000005a,
TD_PERF_SEL_done_scoreboard_not_empty    = 0x0000005b,
TD_PERF_SEL_done_scoreboard_is_full      = 0x0000005c,
TD_PERF_SEL_done_scoreboard_bp_due_to_ooo = 0x0000005d,
TD_PERF_SEL_done_scoreboard_bp_due_to_lds = 0x0000005e,
TD_PERF_SEL_nofilter_formatters_turned_on = 0x0000005f,
TD_PERF_SEL_nofilter_insert_extra_comps  = 0x00000060,
TD_PERF_SEL_nofilter_popcount_dmask_gt_num_comp_of_fmt = 0x00000061,
TD_PERF_SEL_nofilter_popcount_dmask_lt_num_comp_of_fmt = 0x00000062,
TD_PERF_SEL_msaa_load_instr              = 0x00000063,
TD_PERF_SEL_blend_prt_with_prt_default_0 = 0x00000064,
TD_PERF_SEL_blend_prt_with_prt_default_1 = 0x00000065,
TD_PERF_SEL_resmap_instr                 = 0x00000066,
TD_PERF_SEL_prt_ack_instr                = 0x00000067,
TD_PERF_SEL_resmap_with_volume_filtering = 0x00000068,
TD_PERF_SEL_resmap_with_aniso_filtering  = 0x00000069,
TD_PERF_SEL_resmap_with_no_more_filtering = 0x0000006a,
TD_PERF_SEL_resmap_with_cubemap_corner   = 0x0000006b,
TD_PERF_SEL_ray_tracing_bvh4_threads_per_instruction_is_0 = 0x0000006c,
TD_PERF_SEL_ray_tracing_bvh4_threads_per_instruction_is_1 = 0x0000006d,
TD_PERF_SEL_ray_tracing_bvh4_threads_per_instruction_is_2 = 0x0000006e,
TD_PERF_SEL_ray_tracing_bvh4_threads_per_instruction_is_3to4 = 0x0000006f,
TD_PERF_SEL_ray_tracing_bvh4_threads_per_instruction_is_5to8 = 0x00000070,
TD_PERF_SEL_ray_tracing_bvh4_threads_per_instruction_is_9to16 = 0x00000071,
TD_PERF_SEL_ray_tracing_bvh4_threads_per_instruction_is_17to31 = 0x00000072,
TD_PERF_SEL_ray_tracing_bvh4_threads_per_instruction_is_32 = 0x00000073,
TD_PERF_SEL_ray_tracing_bvh4_fp16_box_node = 0x00000074,
TD_PERF_SEL_ray_tracing_bvh4_fp32_box_node = 0x00000075,
TD_PERF_SEL_ray_tracing_bvh4_tri_node    = 0x00000076,
TD_PERF_SEL_ray_tracing_bvh4_dropped_box_node = 0x00000077,
TD_PERF_SEL_ray_tracing_bvh4_dropped_tri_node = 0x00000078,
TD_PERF_SEL_ray_tracing_bvh4_invalid_box_node = 0x00000079,
TD_PERF_SEL_ray_tracing_bvh4_invalid_tri_node = 0x0000007a,
TD_PERF_SEL_ray_tracing_bvh4_box_sort_en = 0x0000007b,
TD_PERF_SEL_ray_tracing_bvh4_box_grow_val_nonzero = 0x0000007c,
TD_PERF_SEL_ray_tracing_bvh4_num_box_with_inf_or_nan_vtx = 0x0000007d,
TD_PERF_SEL_ray_tracing_bvh4_num_tri_with_inf_or_nan_vtx = 0x0000007e,
TD_PERF_SEL_ray_tracing_bvh4_num_box_that_squashed_a_nan = 0x0000007f,
TD_PERF_SEL_ray_tracing_bvh4_num_box_misses = 0x00000080,
TD_PERF_SEL_ray_tracing_bvh4_num_tri_misses = 0x00000081,
TD_PERF_SEL_ray_tracing_bvh4_num_tri_tie_breakers = 0x00000082,
TD_PERF_SEL_burst_bin_preempting_nofilter_1 = 0x00000083,
TD_PERF_SEL_burst_bin_preempting_nofilter_2to4 = 0x00000084,
TD_PERF_SEL_burst_bin_preempting_nofilter_5to7 = 0x00000085,
TD_PERF_SEL_burst_bin_preempting_nofilter_8to16 = 0x00000086,
TD_PERF_SEL_burst_bin_preempting_nofilter_gt16 = 0x00000087,
TD_PERF_SEL_burst_bin_sampler_1          = 0x00000088,
TD_PERF_SEL_burst_bin_sampler_2to8       = 0x00000089,
TD_PERF_SEL_burst_bin_sampler_9to16      = 0x0000008a,
TD_PERF_SEL_burst_bin_sampler_gt16       = 0x0000008b,
TD_PERF_SEL_burst_bin_gather_1           = 0x0000008c,
TD_PERF_SEL_burst_bin_gather_2to8        = 0x0000008d,
TD_PERF_SEL_burst_bin_gather_9to16       = 0x0000008e,
TD_PERF_SEL_burst_bin_gather_gt16        = 0x0000008f,
TD_PERF_SEL_burst_bin_nofilter_1         = 0x00000090,
TD_PERF_SEL_burst_bin_nofilter_2to4      = 0x00000091,
TD_PERF_SEL_burst_bin_nofilter_5to7      = 0x00000092,
TD_PERF_SEL_burst_bin_nofilter_8to16     = 0x00000093,
TD_PERF_SEL_burst_bin_nofilter_gt16      = 0x00000094,
TD_PERF_SEL_burst_bin_bvh4_1             = 0x00000095,
TD_PERF_SEL_burst_bin_bvh4_2to8          = 0x00000096,
TD_PERF_SEL_burst_bin_bvh4_9to16         = 0x00000097,
TD_PERF_SEL_burst_bin_bvh4_gt16          = 0x00000098,
TD_PERF_SEL_burst_bin_bvh4_box_nodes_1   = 0x00000099,
TD_PERF_SEL_burst_bin_bvh4_box_nodes_2to4 = 0x0000009a,
TD_PERF_SEL_burst_bin_bvh4_box_nodes_5to7 = 0x0000009b,
TD_PERF_SEL_burst_bin_bvh4_box_nodes_8to16 = 0x0000009c,
TD_PERF_SEL_burst_bin_bvh4_box_nodes_gt16 = 0x0000009d,
TD_PERF_SEL_burst_bin_bvh4_tri_nodes_1   = 0x0000009e,
TD_PERF_SEL_burst_bin_bvh4_tri_nodes_2to8 = 0x0000009f,
TD_PERF_SEL_burst_bin_bvh4_tri_nodes_9to16 = 0x000000a0,
TD_PERF_SEL_burst_bin_bvh4_tri_nodes_gt16 = 0x000000a1,
TD_PERF_SEL_burst_bin_bvh4_dropped_nodes_1 = 0x000000a2,
TD_PERF_SEL_burst_bin_bvh4_dropped_nodes_2to8 = 0x000000a3,
TD_PERF_SEL_burst_bin_bvh4_dropped_nodes_9to16 = 0x000000a4,
TD_PERF_SEL_burst_bin_bvh4_dropped_nodes_gt16 = 0x000000a5,
TD_PERF_SEL_burst_bin_bvh4_invalid_nodes_1 = 0x000000a6,
TD_PERF_SEL_burst_bin_bvh4_invalid_nodes_2to8 = 0x000000a7,
TD_PERF_SEL_burst_bin_bvh4_invalid_nodes_9to16 = 0x000000a8,
TD_PERF_SEL_burst_bin_bvh4_invalid_nodes_gt16 = 0x000000a9,
TD_PERF_SEL_bubble_bin_ta_waiting_for_tc_data_0 = 0x000000aa,
TD_PERF_SEL_bubble_bin_ta_waiting_for_tc_data_1 = 0x000000ab,
TD_PERF_SEL_bubble_bin_ta_waiting_for_tc_data_2to31 = 0x000000ac,
TD_PERF_SEL_bubble_bin_ta_waiting_for_tc_data_32to127 = 0x000000ad,
TD_PERF_SEL_bubble_bin_ta_waiting_for_tc_data_128to511 = 0x000000ae,
TD_PERF_SEL_bubble_bin_ta_waiting_for_tc_data_gt511 = 0x000000af,
TD_PERF_SEL_bubble_bin_lds_stall_1to3    = 0x000000b0,
TD_PERF_SEL_bubble_bin_lds_stall_4to7    = 0x000000b1,
TD_PERF_SEL_bubble_bin_lds_stall_8to15   = 0x000000b2,
TD_PERF_SEL_bubble_bin_lds_stall_gt15    = 0x000000b3,
TD_PERF_SEL_preempting_nofilter_max_cnt  = 0x000000b4,
TD_PERF_SEL_sampler_lerp0_active         = 0x000000b5,
TD_PERF_SEL_sampler_lerp1_active         = 0x000000b6,
TD_PERF_SEL_sampler_lerp2_active         = 0x000000b7,
TD_PERF_SEL_sampler_lerp3_active         = 0x000000b8,
TD_PERF_SEL_nofilter_total_num_comps_to_lds = 0x000000b9,
TD_PERF_SEL_nofilter_byte_cycling_4cycles = 0x000000ba,
TD_PERF_SEL_nofilter_byte_cycling_8cycles = 0x000000bb,
TD_PERF_SEL_nofilter_byte_cycling_16cycles = 0x000000bc,
TD_PERF_SEL_nofilter_dword_cycling_2cycles = 0x000000bd,
TD_PERF_SEL_nofilter_dword_cycling_4cycles = 0x000000be,
TD_PERF_SEL_input_bp_due_to_done_scoreboard_full = 0x000000bf,
TD_PERF_SEL_ray_tracing_bvh4_instr_invld_thread_cnt = 0x000000c0,
} TD_PERFCOUNT_SEL;

/*******************************************************
 * GL2C Enums
 *******************************************************/

/*
 * GL2A_PERF_SEL enum
 */

typedef enum GL2A_PERF_SEL {
GL2A_PERF_SEL_NONE                       = 0x00000000,
GL2A_PERF_SEL_CYCLE                      = 0x00000001,
GL2A_PERF_SEL_BUSY                       = 0x00000002,
GL2A_PERF_SEL_REQ_GL2C0                  = 0x00000003,
GL2A_PERF_SEL_REQ_GL2C1                  = 0x00000004,
GL2A_PERF_SEL_REQ_GL2C2                  = 0x00000005,
GL2A_PERF_SEL_REQ_GL2C3                  = 0x00000006,
GL2A_PERF_SEL_REQ_GL2C4                  = 0x00000007,
GL2A_PERF_SEL_REQ_GL2C5                  = 0x00000008,
GL2A_PERF_SEL_REQ_GL2C6                  = 0x00000009,
GL2A_PERF_SEL_REQ_GL2C7                  = 0x0000000a,
GL2A_PERF_SEL_REQ_HI_PRIO_GL2C0          = 0x0000000b,
GL2A_PERF_SEL_REQ_HI_PRIO_GL2C1          = 0x0000000c,
GL2A_PERF_SEL_REQ_HI_PRIO_GL2C2          = 0x0000000d,
GL2A_PERF_SEL_REQ_HI_PRIO_GL2C3          = 0x0000000e,
GL2A_PERF_SEL_REQ_HI_PRIO_GL2C4          = 0x0000000f,
GL2A_PERF_SEL_REQ_HI_PRIO_GL2C5          = 0x00000010,
GL2A_PERF_SEL_REQ_HI_PRIO_GL2C6          = 0x00000011,
GL2A_PERF_SEL_REQ_HI_PRIO_GL2C7          = 0x00000012,
GL2A_PERF_SEL_REQ_BURST_GL2C0            = 0x00000013,
GL2A_PERF_SEL_REQ_BURST_GL2C1            = 0x00000014,
GL2A_PERF_SEL_REQ_BURST_GL2C2            = 0x00000015,
GL2A_PERF_SEL_REQ_BURST_GL2C3            = 0x00000016,
GL2A_PERF_SEL_REQ_BURST_GL2C4            = 0x00000017,
GL2A_PERF_SEL_REQ_BURST_GL2C5            = 0x00000018,
GL2A_PERF_SEL_REQ_BURST_GL2C6            = 0x00000019,
GL2A_PERF_SEL_REQ_BURST_GL2C7            = 0x0000001a,
GL2A_PERF_SEL_REQ_STALL_GL2C0            = 0x0000001b,
GL2A_PERF_SEL_REQ_STALL_GL2C1            = 0x0000001c,
GL2A_PERF_SEL_REQ_STALL_GL2C2            = 0x0000001d,
GL2A_PERF_SEL_REQ_STALL_GL2C3            = 0x0000001e,
GL2A_PERF_SEL_REQ_STALL_GL2C4            = 0x0000001f,
GL2A_PERF_SEL_REQ_STALL_GL2C5            = 0x00000020,
GL2A_PERF_SEL_REQ_STALL_GL2C6            = 0x00000021,
GL2A_PERF_SEL_REQ_STALL_GL2C7            = 0x00000022,
GL2A_PERF_SEL_RTN_STALL_GL2C0            = 0x00000023,
GL2A_PERF_SEL_RTN_STALL_GL2C1            = 0x00000024,
GL2A_PERF_SEL_RTN_STALL_GL2C2            = 0x00000025,
GL2A_PERF_SEL_RTN_STALL_GL2C3            = 0x00000026,
GL2A_PERF_SEL_RTN_STALL_GL2C4            = 0x00000027,
GL2A_PERF_SEL_RTN_STALL_GL2C5            = 0x00000028,
GL2A_PERF_SEL_RTN_STALL_GL2C6            = 0x00000029,
GL2A_PERF_SEL_RTN_STALL_GL2C7            = 0x0000002a,
GL2A_PERF_SEL_RTN_CLIENT0                = 0x0000002b,
GL2A_PERF_SEL_RTN_CLIENT1                = 0x0000002c,
GL2A_PERF_SEL_RTN_CLIENT2                = 0x0000002d,
GL2A_PERF_SEL_RTN_CLIENT3                = 0x0000002e,
GL2A_PERF_SEL_RTN_CLIENT4                = 0x0000002f,
GL2A_PERF_SEL_RTN_CLIENT5                = 0x00000030,
GL2A_PERF_SEL_RTN_CLIENT6                = 0x00000031,
GL2A_PERF_SEL_RTN_CLIENT7                = 0x00000032,
GL2A_PERF_SEL_RTN_CLIENT8                = 0x00000033,
GL2A_PERF_SEL_RTN_CLIENT9                = 0x00000034,
GL2A_PERF_SEL_RTN_CLIENT10               = 0x00000035,
GL2A_PERF_SEL_RTN_CLIENT11               = 0x00000036,
GL2A_PERF_SEL_RTN_CLIENT12               = 0x00000037,
GL2A_PERF_SEL_RTN_CLIENT13               = 0x00000038,
GL2A_PERF_SEL_RTN_CLIENT14               = 0x00000039,
GL2A_PERF_SEL_RTN_CLIENT15               = 0x0000003a,
GL2A_PERF_SEL_RTN_ARB_COLLISION_CLIENT0  = 0x0000003b,
GL2A_PERF_SEL_RTN_ARB_COLLISION_CLIENT1  = 0x0000003c,
GL2A_PERF_SEL_RTN_ARB_COLLISION_CLIENT2  = 0x0000003d,
GL2A_PERF_SEL_RTN_ARB_COLLISION_CLIENT3  = 0x0000003e,
GL2A_PERF_SEL_RTN_ARB_COLLISION_CLIENT4  = 0x0000003f,
GL2A_PERF_SEL_RTN_ARB_COLLISION_CLIENT5  = 0x00000040,
GL2A_PERF_SEL_RTN_ARB_COLLISION_CLIENT6  = 0x00000041,
GL2A_PERF_SEL_RTN_ARB_COLLISION_CLIENT7  = 0x00000042,
GL2A_PERF_SEL_RTN_ARB_COLLISION_CLIENT8  = 0x00000043,
GL2A_PERF_SEL_RTN_ARB_COLLISION_CLIENT9  = 0x00000044,
GL2A_PERF_SEL_RTN_ARB_COLLISION_CLIENT10 = 0x00000045,
GL2A_PERF_SEL_RTN_ARB_COLLISION_CLIENT11 = 0x00000046,
GL2A_PERF_SEL_RTN_ARB_COLLISION_CLIENT12 = 0x00000047,
GL2A_PERF_SEL_RTN_ARB_COLLISION_CLIENT13 = 0x00000048,
GL2A_PERF_SEL_RTN_ARB_COLLISION_CLIENT14 = 0x00000049,
GL2A_PERF_SEL_RTN_ARB_COLLISION_CLIENT15 = 0x0000004a,
GL2A_PERF_SEL_REQ_BURST_CLIENT0          = 0x0000004b,
GL2A_PERF_SEL_REQ_BURST_CLIENT1          = 0x0000004c,
GL2A_PERF_SEL_REQ_BURST_CLIENT2          = 0x0000004d,
GL2A_PERF_SEL_REQ_BURST_CLIENT3          = 0x0000004e,
GL2A_PERF_SEL_REQ_BURST_CLIENT4          = 0x0000004f,
GL2A_PERF_SEL_REQ_BURST_CLIENT5          = 0x00000050,
GL2A_PERF_SEL_REQ_BURST_CLIENT6          = 0x00000051,
GL2A_PERF_SEL_REQ_BURST_CLIENT7          = 0x00000052,
GL2A_PERF_SEL_REQ_BURST_CLIENT8          = 0x00000053,
GL2A_PERF_SEL_REQ_BURST_CLIENT9          = 0x00000054,
GL2A_PERF_SEL_REQ_BURST_CLIENT10         = 0x00000055,
GL2A_PERF_SEL_REQ_BURST_CLIENT11         = 0x00000056,
GL2A_PERF_SEL_REQ_BURST_CLIENT12         = 0x00000057,
GL2A_PERF_SEL_REQ_BURST_CLIENT13         = 0x00000058,
GL2A_PERF_SEL_REQ_BURST_CLIENT14         = 0x00000059,
GL2A_PERF_SEL_REQ_BURST_CLIENT15         = 0x0000005a,
GL2A_PERF_SEL_RTN_CREDIT_STALL_CLIENT0   = 0x0000005b,
GL2A_PERF_SEL_RTN_CREDIT_STALL_CLIENT1   = 0x0000005c,
GL2A_PERF_SEL_RTN_CREDIT_STALL_CLIENT2   = 0x0000005d,
GL2A_PERF_SEL_RTN_CREDIT_STALL_CLIENT3   = 0x0000005e,
GL2A_PERF_SEL_RTN_CREDIT_STALL_CLIENT4   = 0x0000005f,
GL2A_PERF_SEL_RTN_CREDIT_STALL_CLIENT5   = 0x00000060,
GL2A_PERF_SEL_RTN_CREDIT_STALL_CLIENT6   = 0x00000061,
GL2A_PERF_SEL_RTN_CREDIT_STALL_CLIENT7   = 0x00000062,
GL2A_PERF_SEL_RTN_CREDIT_STALL_CLIENT8   = 0x00000063,
GL2A_PERF_SEL_RTN_CREDIT_STALL_CLIENT9   = 0x00000064,
GL2A_PERF_SEL_RTN_CREDIT_STALL_CLIENT10  = 0x00000065,
GL2A_PERF_SEL_RTN_CREDIT_STALL_CLIENT11  = 0x00000067,
GL2A_PERF_SEL_RTN_CREDIT_STALL_CLIENT12  = 0x00000068,
GL2A_PERF_SEL_RTN_CREDIT_STALL_CLIENT13  = 0x00000069,
GL2A_PERF_SEL_RTN_CREDIT_STALL_CLIENT14  = 0x0000006a,
GL2A_PERF_SEL_RTN_CREDIT_STALL_CLIENT15  = 0x0000006b,
} GL2A_PERF_SEL;

/*
 * GL2C_PERF_SEL enum
 */

typedef enum GL2C_PERF_SEL {
GL2C_PERF_SEL_NONE                       = 0x00000000,
GL2C_PERF_SEL_CYCLE                      = 0x00000001,
GL2C_PERF_SEL_BUSY                       = 0x00000002,
GL2C_PERF_SEL_REQ                        = 0x00000003,
GL2C_PERF_SEL_VOL_REQ                    = 0x00000004,
GL2C_PERF_SEL_HIGH_PRIORITY_REQ          = 0x00000005,
GL2C_PERF_SEL_READ                       = 0x00000006,
GL2C_PERF_SEL_WRITE                      = 0x00000007,
GL2C_PERF_SEL_ATOMIC                     = 0x00000008,
GL2C_PERF_SEL_NOP_ACK                    = 0x00000009,
GL2C_PERF_SEL_NOP_RTN0                   = 0x0000000a,
GL2C_PERF_SEL_PROBE                      = 0x0000000b,
GL2C_PERF_SEL_PROBE_ALL                  = 0x0000000c,
GL2C_PERF_SEL_INTERNAL_PROBE             = 0x0000000d,
GL2C_PERF_SEL_COMPRESSED_READ_REQ        = 0x0000000e,
GL2C_PERF_SEL_METADATA_READ_REQ          = 0x0000000f,
GL2C_PERF_SEL_CLIENT0_REQ                = 0x00000010,
GL2C_PERF_SEL_CLIENT1_REQ                = 0x00000011,
GL2C_PERF_SEL_CLIENT2_REQ                = 0x00000012,
GL2C_PERF_SEL_CLIENT3_REQ                = 0x00000013,
GL2C_PERF_SEL_CLIENT4_REQ                = 0x00000014,
GL2C_PERF_SEL_CLIENT5_REQ                = 0x00000015,
GL2C_PERF_SEL_CLIENT6_REQ                = 0x00000016,
GL2C_PERF_SEL_CLIENT7_REQ                = 0x00000017,
GL2C_PERF_SEL_CLIENT8_REQ                = 0x00000018,
GL2C_PERF_SEL_CLIENT9_REQ                = 0x00000019,
GL2C_PERF_SEL_CLIENT10_REQ               = 0x0000001a,
GL2C_PERF_SEL_CLIENT11_REQ               = 0x0000001b,
GL2C_PERF_SEL_CLIENT12_REQ               = 0x0000001c,
GL2C_PERF_SEL_CLIENT13_REQ               = 0x0000001d,
GL2C_PERF_SEL_CLIENT14_REQ               = 0x0000001e,
GL2C_PERF_SEL_CLIENT15_REQ               = 0x0000001f,
GL2C_PERF_SEL_C_RW_S_REQ                 = 0x00000020,
GL2C_PERF_SEL_C_RW_US_REQ                = 0x00000021,
GL2C_PERF_SEL_C_RO_S_REQ                 = 0x00000022,
GL2C_PERF_SEL_C_RO_US_REQ                = 0x00000023,
GL2C_PERF_SEL_UC_REQ                     = 0x00000024,
GL2C_PERF_SEL_LRU_REQ                    = 0x00000025,
GL2C_PERF_SEL_STREAM_REQ                 = 0x00000026,
GL2C_PERF_SEL_BYPASS_REQ                 = 0x00000027,
GL2C_PERF_SEL_NOA_REQ                    = 0x00000028,
GL2C_PERF_SEL_SHARED_REQ                 = 0x00000029,
GL2C_PERF_SEL_HIT                        = 0x0000002a,
GL2C_PERF_SEL_MISS                       = 0x0000002b,
GL2C_PERF_SEL_FULL_HIT                   = 0x0000002c,
GL2C_PERF_SEL_PARTIAL_32B_HIT            = 0x0000002d,
GL2C_PERF_SEL_PARTIAL_64B_HIT            = 0x0000002e,
GL2C_PERF_SEL_PARTIAL_96B_HIT            = 0x0000002f,
GL2C_PERF_SEL_DEWRITE_ALLOCATE_HIT       = 0x00000030,
GL2C_PERF_SEL_FULLY_WRITTEN_HIT          = 0x00000031,
GL2C_PERF_SEL_UNCACHED_WRITE             = 0x00000032,
GL2C_PERF_SEL_WRITEBACK                  = 0x00000033,
GL2C_PERF_SEL_NORMAL_WRITEBACK           = 0x00000034,
GL2C_PERF_SEL_EVICT                      = 0x00000035,
GL2C_PERF_SEL_NORMAL_EVICT               = 0x00000036,
GL2C_PERF_SEL_PROBE_EVICT                = 0x00000037,
GL2C_PERF_SEL_REQ_TO_MISS_QUEUE          = 0x00000038,
GL2C_PERF_SEL_HIT_PASS_MISS_IN_CLIENT0   = 0x00000039,
GL2C_PERF_SEL_HIT_PASS_MISS_IN_CLIENT1   = 0x0000003a,
GL2C_PERF_SEL_HIT_PASS_MISS_IN_CLIENT2   = 0x0000003b,
GL2C_PERF_SEL_HIT_PASS_MISS_IN_CLIENT3   = 0x0000003c,
GL2C_PERF_SEL_HIT_PASS_MISS_IN_CLIENT4   = 0x0000003d,
GL2C_PERF_SEL_HIT_PASS_MISS_IN_CLIENT5   = 0x0000003e,
GL2C_PERF_SEL_HIT_PASS_MISS_IN_CLIENT6   = 0x0000003f,
GL2C_PERF_SEL_HIT_PASS_MISS_IN_CLIENT7   = 0x00000040,
GL2C_PERF_SEL_HIT_PASS_MISS_IN_CLIENT8   = 0x00000041,
GL2C_PERF_SEL_HIT_PASS_MISS_IN_CLIENT9   = 0x00000042,
GL2C_PERF_SEL_HIT_PASS_MISS_IN_CLIENT10  = 0x00000043,
GL2C_PERF_SEL_HIT_PASS_MISS_IN_CLIENT11  = 0x00000044,
GL2C_PERF_SEL_HIT_PASS_MISS_IN_CLIENT12  = 0x00000045,
GL2C_PERF_SEL_HIT_PASS_MISS_IN_CLIENT13  = 0x00000046,
GL2C_PERF_SEL_HIT_PASS_MISS_IN_CLIENT14  = 0x00000047,
GL2C_PERF_SEL_HIT_PASS_MISS_IN_CLIENT15  = 0x00000048,
GL2C_PERF_SEL_READ_32_REQ                = 0x00000049,
GL2C_PERF_SEL_READ_64_REQ                = 0x0000004a,
GL2C_PERF_SEL_READ_128_REQ               = 0x0000004b,
GL2C_PERF_SEL_WRITE_32_REQ               = 0x0000004c,
GL2C_PERF_SEL_WRITE_64_REQ               = 0x0000004d,
GL2C_PERF_SEL_COMPRESSED_READ_0_REQ      = 0x0000004e,
GL2C_PERF_SEL_COMPRESSED_READ_32_REQ     = 0x0000004f,
GL2C_PERF_SEL_COMPRESSED_READ_64_REQ     = 0x00000050,
GL2C_PERF_SEL_COMPRESSED_READ_96_REQ     = 0x00000051,
GL2C_PERF_SEL_COMPRESSED_READ_128_REQ    = 0x00000052,
GL2C_PERF_SEL_MC_WRREQ                   = 0x00000053,
GL2C_PERF_SEL_EA_WRREQ_SNOOP             = 0x00000054,
GL2C_PERF_SEL_EA_WRREQ_64B               = 0x00000055,
GL2C_PERF_SEL_EA_WRREQ_PROBE_COMMAND     = 0x00000056,
GL2C_PERF_SEL_EA_WR_UNCACHED_32B         = 0x00000057,
GL2C_PERF_SEL_MC_WRREQ_STALL             = 0x00000058,
GL2C_PERF_SEL_EA_WRREQ_IO_CREDIT_STALL   = 0x00000059,
GL2C_PERF_SEL_EA_WRREQ_GMI_CREDIT_STALL  = 0x0000005a,
GL2C_PERF_SEL_EA_WRREQ_DRAM_CREDIT_STALL = 0x0000005b,
GL2C_PERF_SEL_TOO_MANY_EA_WRREQS_STALL   = 0x0000005c,
GL2C_PERF_SEL_MC_WRREQ_LEVEL             = 0x0000005d,
GL2C_PERF_SEL_EA_ATOMIC                  = 0x0000005e,
GL2C_PERF_SEL_EA_ATOMIC_LEVEL            = 0x0000005f,
GL2C_PERF_SEL_MC_RDREQ                   = 0x00000060,
GL2C_PERF_SEL_EA_RDREQ_SNOOP             = 0x00000061,
GL2C_PERF_SEL_EA_RDREQ_SPLIT             = 0x00000062,
GL2C_PERF_SEL_EA_RDREQ_32B               = 0x00000063,
GL2C_PERF_SEL_EA_RDREQ_64B               = 0x00000064,
GL2C_PERF_SEL_EA_RDREQ_96B               = 0x00000065,
GL2C_PERF_SEL_EA_RDREQ_128B              = 0x00000066,
GL2C_PERF_SEL_EA_RD_UNCACHED_32B         = 0x00000067,
GL2C_PERF_SEL_EA_RD_MDC_32B              = 0x00000068,
GL2C_PERF_SEL_EA_RD_COMPRESSED_32B       = 0x00000069,
GL2C_PERF_SEL_EA_RDREQ_IO_CREDIT_STALL   = 0x0000006a,
GL2C_PERF_SEL_EA_RDREQ_GMI_CREDIT_STALL  = 0x0000006b,
GL2C_PERF_SEL_EA_RDREQ_DRAM_CREDIT_STALL = 0x0000006c,
GL2C_PERF_SEL_MC_RDREQ_LEVEL             = 0x0000006d,
GL2C_PERF_SEL_EA_RDREQ_DRAM              = 0x0000006e,
GL2C_PERF_SEL_EA_WRREQ_DRAM              = 0x0000006f,
GL2C_PERF_SEL_EA_RDREQ_DRAM_32B          = 0x00000070,
GL2C_PERF_SEL_EA_WRREQ_DRAM_32B          = 0x00000071,
GL2C_PERF_SEL_ONION_READ                 = 0x00000072,
GL2C_PERF_SEL_ONION_WRITE                = 0x00000073,
GL2C_PERF_SEL_IO_READ                    = 0x00000074,
GL2C_PERF_SEL_IO_WRITE                   = 0x00000075,
GL2C_PERF_SEL_GARLIC_READ                = 0x00000076,
GL2C_PERF_SEL_GARLIC_WRITE               = 0x00000077,
GL2C_PERF_SEL_EA_OUTSTANDING             = 0x00000078,
GL2C_PERF_SEL_LATENCY_FIFO_FULL          = 0x00000079,
GL2C_PERF_SEL_SRC_FIFO_FULL              = 0x0000007a,
GL2C_PERF_SEL_TAG_STALL                  = 0x0000007b,
GL2C_PERF_SEL_TAG_WRITEBACK_FIFO_FULL_STALL = 0x0000007c,
GL2C_PERF_SEL_TAG_MISS_NOTHING_REPLACEABLE_STALL = 0x0000007d,
GL2C_PERF_SEL_TAG_UNCACHED_WRITE_ATOMIC_FIFO_FULL_STALL = 0x0000007e,
GL2C_PERF_SEL_TAG_NO_UNCACHED_WRITE_ATOMIC_ENTRIES_STALL = 0x0000007f,
GL2C_PERF_SEL_TAG_PROBE_STALL            = 0x00000080,
GL2C_PERF_SEL_TAG_PROBE_FILTER_STALL     = 0x00000081,
GL2C_PERF_SEL_TAG_PROBE_FIFO_FULL_STALL  = 0x00000082,
GL2C_PERF_SEL_TAG_READ_DST_STALL         = 0x00000083,
GL2C_PERF_SEL_READ_RETURN_TIMEOUT        = 0x00000084,
GL2C_PERF_SEL_WRITEBACK_READ_TIMEOUT     = 0x00000085,
GL2C_PERF_SEL_READ_RETURN_FULL_BUBBLE    = 0x00000086,
GL2C_PERF_SEL_BUBBLE                     = 0x00000087,
GL2C_PERF_SEL_IB_REQ                     = 0x00000088,
GL2C_PERF_SEL_IB_STALL                   = 0x00000089,
GL2C_PERF_SEL_IB_TAG_STALL               = 0x0000008a,
GL2C_PERF_SEL_IB_CM_STALL                = 0x0000008b,
GL2C_PERF_SEL_RETURN_ACK                 = 0x0000008c,
GL2C_PERF_SEL_RETURN_DATA                = 0x0000008d,
GL2C_PERF_SEL_EA_RDRET_NACK              = 0x0000008e,
GL2C_PERF_SEL_EA_WRRET_NACK              = 0x0000008f,
GL2C_PERF_SEL_GL2A_LEVEL                 = 0x00000090,
GL2C_PERF_SEL_PROBE_FILTER_DISABLE_TRANSITION = 0x00000091,
GL2C_PERF_SEL_PROBE_FILTER_DISABLED      = 0x00000092,
GL2C_PERF_SEL_ALL_TC_OP_WB_OR_INV_START  = 0x00000093,
GL2C_PERF_SEL_ALL_TC_OP_WB_OR_INV_VOL_START = 0x00000094,
GL2C_PERF_SEL_GCR_INV                    = 0x00000095,
GL2C_PERF_SEL_GCR_WB                     = 0x00000096,
GL2C_PERF_SEL_GCR_DISCARD                = 0x00000097,
GL2C_PERF_SEL_GCR_RANGE                  = 0x00000098,
GL2C_PERF_SEL_GCR_ALL                    = 0x00000099,
GL2C_PERF_SEL_GCR_VOL                    = 0x0000009a,
GL2C_PERF_SEL_GCR_UNSHARED               = 0x0000009b,
GL2C_PERF_SEL_GCR_MDC_INV                = 0x0000009c,
GL2C_PERF_SEL_GCR_GL2_INV_ALL            = 0x0000009d,
GL2C_PERF_SEL_GCR_GL2_WB_ALL             = 0x0000009e,
GL2C_PERF_SEL_GCR_MDC_INV_ALL            = 0x0000009f,
GL2C_PERF_SEL_GCR_GL2_INV_RANGE          = 0x000000a0,
GL2C_PERF_SEL_GCR_GL2_WB_RANGE           = 0x000000a1,
GL2C_PERF_SEL_GCR_GL2_WB_INV_RANGE       = 0x000000a2,
GL2C_PERF_SEL_GCR_MDC_INV_RANGE          = 0x000000a3,
GL2C_PERF_SEL_ALL_GCR_INV_EVICT          = 0x000000a4,
GL2C_PERF_SEL_ALL_GCR_INV_VOL_EVICT      = 0x000000a5,
GL2C_PERF_SEL_ALL_GCR_WB_OR_INV_CYCLE    = 0x000000a6,
GL2C_PERF_SEL_ALL_GCR_WB_OR_INV_VOL_CYCLE = 0x000000a7,
GL2C_PERF_SEL_ALL_GCR_WB_WRITEBACK       = 0x000000a8,
GL2C_PERF_SEL_GCR_INVL2_VOL_CYCLE        = 0x000000a9,
GL2C_PERF_SEL_GCR_INVL2_VOL_EVICT        = 0x000000aa,
GL2C_PERF_SEL_GCR_INVL2_VOL_START        = 0x000000ab,
GL2C_PERF_SEL_GCR_WBL2_VOL_CYCLE         = 0x000000ac,
GL2C_PERF_SEL_GCR_WBL2_VOL_START         = 0x000000ad,
GL2C_PERF_SEL_GCR_WBINVL2_CYCLE          = 0x000000ae,
GL2C_PERF_SEL_GCR_WBINVL2_EVICT          = 0x000000af,
GL2C_PERF_SEL_GCR_WBINVL2_START          = 0x000000b0,
GL2C_PERF_SEL_MDC_INV_METADATA           = 0x000000b1,
GL2C_PERF_SEL_MDC_REQ                    = 0x000000b2,
GL2C_PERF_SEL_MDC_LEVEL                  = 0x000000b3,
GL2C_PERF_SEL_MDC_TAG_HIT                = 0x000000b4,
GL2C_PERF_SEL_MDC_SECTOR_HIT             = 0x000000b5,
GL2C_PERF_SEL_MDC_SECTOR_MISS            = 0x000000b6,
GL2C_PERF_SEL_MDC_TAG_STALL              = 0x000000b7,
GL2C_PERF_SEL_MDC_TAG_REPLACEMENT_LINE_IN_USE_STALL = 0x000000b8,
GL2C_PERF_SEL_MDC_TAG_DESECTORIZATION_FIFO_FULL_STALL = 0x000000b9,
GL2C_PERF_SEL_MDC_TAG_WAITING_FOR_INVALIDATE_COMPLETION_STALL = 0x000000ba,
GL2C_PERF_SEL_CM_CHANNEL0_REQ            = 0x000000bb,
GL2C_PERF_SEL_CM_CHANNEL1_REQ            = 0x000000bc,
GL2C_PERF_SEL_CM_CHANNEL2_REQ            = 0x000000bd,
GL2C_PERF_SEL_CM_CHANNEL3_REQ            = 0x000000be,
GL2C_PERF_SEL_CM_CHANNEL4_REQ            = 0x000000bf,
GL2C_PERF_SEL_CM_CHANNEL5_REQ            = 0x000000c0,
GL2C_PERF_SEL_CM_CHANNEL6_REQ            = 0x000000c1,
GL2C_PERF_SEL_CM_CHANNEL7_REQ            = 0x000000c2,
GL2C_PERF_SEL_CM_CHANNEL8_REQ            = 0x000000c3,
GL2C_PERF_SEL_CM_CHANNEL9_REQ            = 0x000000c4,
GL2C_PERF_SEL_CM_CHANNEL10_REQ           = 0x000000c5,
GL2C_PERF_SEL_CM_CHANNEL11_REQ           = 0x000000c6,
GL2C_PERF_SEL_CM_CHANNEL12_REQ           = 0x000000c7,
GL2C_PERF_SEL_CM_CHANNEL13_REQ           = 0x000000c8,
GL2C_PERF_SEL_CM_CHANNEL14_REQ           = 0x000000c9,
GL2C_PERF_SEL_CM_CHANNEL15_REQ           = 0x000000ca,
GL2C_PERF_SEL_CM_CHANNEL16_REQ           = 0x000000cb,
GL2C_PERF_SEL_CM_CHANNEL17_REQ           = 0x000000cc,
GL2C_PERF_SEL_CM_CHANNEL18_REQ           = 0x000000cd,
GL2C_PERF_SEL_CM_CHANNEL19_REQ           = 0x000000ce,
GL2C_PERF_SEL_CM_CHANNEL20_REQ           = 0x000000cf,
GL2C_PERF_SEL_CM_CHANNEL21_REQ           = 0x000000d0,
GL2C_PERF_SEL_CM_CHANNEL22_REQ           = 0x000000d1,
GL2C_PERF_SEL_CM_CHANNEL23_REQ           = 0x000000d2,
GL2C_PERF_SEL_CM_CHANNEL24_REQ           = 0x000000d3,
GL2C_PERF_SEL_CM_CHANNEL25_REQ           = 0x000000d4,
GL2C_PERF_SEL_CM_CHANNEL26_REQ           = 0x000000d5,
GL2C_PERF_SEL_CM_CHANNEL27_REQ           = 0x000000d6,
GL2C_PERF_SEL_CM_CHANNEL28_REQ           = 0x000000d7,
GL2C_PERF_SEL_CM_CHANNEL29_REQ           = 0x000000d8,
GL2C_PERF_SEL_CM_CHANNEL30_REQ           = 0x000000d9,
GL2C_PERF_SEL_CM_CHANNEL31_REQ           = 0x000000da,
GL2C_PERF_SEL_CM_COMP_ATOMIC_COLOR_REQ   = 0x000000db,
GL2C_PERF_SEL_CM_COMP_ATOMIC_DEPTH16_REQ = 0x000000dc,
GL2C_PERF_SEL_CM_COMP_ATOMIC_DEPTH32_REQ = 0x000000dd,
GL2C_PERF_SEL_CM_COMP_ATOMIC_STENCIL_REQ = 0x000000de,
GL2C_PERF_SEL_CM_COMP_WRITE_COLOR_REQ    = 0x000000df,
GL2C_PERF_SEL_CM_COMP_WRITE_DEPTH16_REQ  = 0x000000e0,
GL2C_PERF_SEL_CM_COMP_WRITE_DEPTH32_REQ  = 0x000000e1,
GL2C_PERF_SEL_CM_COMP_WRITE_STENCIL_REQ  = 0x000000e2,
GL2C_PERF_SEL_CM_COMP_READ_REQ           = 0x000000e3,
GL2C_PERF_SEL_CM_READ_BACK_REQ           = 0x000000e4,
GL2C_PERF_SEL_CM_METADATA_WR_REQ         = 0x000000e5,
GL2C_PERF_SEL_CM_WR_ACK_REQ              = 0x000000e6,
GL2C_PERF_SEL_CM_NO_ACK_REQ              = 0x000000e7,
GL2C_PERF_SEL_CM_NOOP_REQ                = 0x000000e8,
GL2C_PERF_SEL_CM_COMP_COLOR_EN_REQ       = 0x000000e9,
GL2C_PERF_SEL_CM_COMP_COLOR_DIS_REQ      = 0x000000ea,
GL2C_PERF_SEL_CM_COMP_STENCIL_REQ        = 0x000000eb,
GL2C_PERF_SEL_CM_COMP_DEPTH16_REQ        = 0x000000ec,
GL2C_PERF_SEL_CM_COMP_DEPTH32_REQ        = 0x000000ed,
GL2C_PERF_SEL_CM_COMP_RB_SKIP_REQ        = 0x000000ee,
GL2C_PERF_SEL_CM_COLOR_32B_WR_REQ        = 0x000000ef,
GL2C_PERF_SEL_CM_COLOR_64B_WR_REQ        = 0x000000f0,
GL2C_PERF_SEL_CM_FULL_WRITE_REQ          = 0x000000f1,
GL2C_PERF_SEL_CM_RVF_FULL                = 0x000000f2,
GL2C_PERF_SEL_CM_SDR_FULL                = 0x000000f3,
GL2C_PERF_SEL_CM_MERGE_BUF_FULL          = 0x000000f4,
GL2C_PERF_SEL_CM_DCC_STALL               = 0x000000f5,
GL2C_PERF_SEL_CM_DCC_IN_XFC              = 0x000000f6,
GL2C_PERF_SEL_CM_DCC_OUT_XFC             = 0x000000f7,
GL2C_PERF_SEL_CM_DCC_OUT_1x1             = 0x000000f8,
GL2C_PERF_SEL_CM_DCC_OUT_1x2             = 0x000000f9,
GL2C_PERF_SEL_CM_DCC_OUT_2x1             = 0x000000fa,
GL2C_PERF_SEL_CM_DCC_OUT_2x2             = 0x000000fb,
GL2C_PERF_SEL_CM_DCC_OUT_UNCOMP          = 0x000000fc,
GL2C_PERF_SEL_CM_DCC_OUT_CONST           = 0x000000fd,
GL2C_PERF_SEL_HIT_PASS_MISS_IN_CLIENT16  = 0x000000fe,
GL2C_PERF_SEL_HIT_PASS_MISS_IN_CLIENT17  = 0x000000ff,
GL2C_PERF_SEL_HIT_PASS_MISS_IN_CLIENT18  = 0x00000100,
GL2C_PERF_SEL_HIT_PASS_MISS_IN_CLIENT19  = 0x00000101,
} GL2C_PERF_SEL;

/*******************************************************
 * GRBM Enums
 *******************************************************/

/*
 * GRBM_PERF_SEL enum
 */

typedef enum GRBM_PERF_SEL {
GRBM_PERF_SEL_COUNT                      = 0x00000000,
GRBM_PERF_SEL_USER_DEFINED               = 0x00000001,
GRBM_PERF_SEL_GUI_ACTIVE                 = 0x00000002,
GRBM_PERF_SEL_CP_BUSY                    = 0x00000003,
GRBM_PERF_SEL_CP_COHER_BUSY              = 0x00000004,
GRBM_PERF_SEL_CP_DMA_BUSY                = 0x00000005,
GRBM_PERF_SEL_CB_BUSY                    = 0x00000006,
GRBM_PERF_SEL_DB_BUSY                    = 0x00000007,
GRBM_PERF_SEL_PA_BUSY                    = 0x00000008,
GRBM_PERF_SEL_SC_BUSY                    = 0x00000009,
GRBM_PERF_SEL_SPI_BUSY                   = 0x0000000b,
GRBM_PERF_SEL_SX_BUSY                    = 0x0000000c,
GRBM_PERF_SEL_TA_BUSY                    = 0x0000000d,
GRBM_PERF_SEL_CB_CLEAN                   = 0x0000000e,
GRBM_PERF_SEL_DB_CLEAN                   = 0x0000000f,
GRBM_PERF_SEL_GDS_BUSY                   = 0x00000019,
GRBM_PERF_SEL_BCI_BUSY                   = 0x0000001a,
GRBM_PERF_SEL_RLC_BUSY                   = 0x0000001b,
GRBM_PERF_SEL_TCP_BUSY                   = 0x0000001c,
GRBM_PERF_SEL_CPG_BUSY                   = 0x0000001d,
GRBM_PERF_SEL_CPC_BUSY                   = 0x0000001e,
GRBM_PERF_SEL_CPF_BUSY                   = 0x0000001f,
GRBM_PERF_SEL_GE_BUSY                    = 0x00000020,
GRBM_PERF_SEL_GE_NO_DMA_BUSY             = 0x00000021,
GRBM_PERF_SEL_UTCL2_BUSY                 = 0x00000022,
GRBM_PERF_SEL_EA_BUSY                    = 0x00000023,
GRBM_PERF_SEL_RMI_BUSY                   = 0x00000024,
GRBM_PERF_SEL_CPAXI_BUSY                 = 0x00000025,
GRBM_PERF_SEL_UTCL1_BUSY                 = 0x00000027,
GRBM_PERF_SEL_GL2CC_BUSY                 = 0x00000028,
GRBM_PERF_SEL_SDMA_BUSY                  = 0x00000029,
GRBM_PERF_SEL_CH_BUSY                    = 0x0000002a,
GRBM_PERF_SEL_PH_BUSY                    = 0x0000002b,
GRBM_PERF_SEL_PMM_BUSY                   = 0x0000002c,
GRBM_PERF_SEL_GUS_BUSY                   = 0x0000002d,
GRBM_PERF_SEL_GL1CC_BUSY                 = 0x0000002e,
GRBM_PERF_SEL_ANY_ACTIVE_F_BUSY          = 0x0000002f,
GRBM_PERF_SEL_GL1H_BUSY                  = 0x00000030,
GRBM_PERF_SEL_PC_BUSY                    = 0x00000031,
} GRBM_PERF_SEL;

/*
 * GRBM_SE0_PERF_SEL enum
 */

typedef enum GRBM_SE0_PERF_SEL {
GRBM_SE0_PERF_SEL_COUNT                  = 0x00000000,
GRBM_SE0_PERF_SEL_USER_DEFINED           = 0x00000001,
GRBM_SE0_PERF_SEL_CB_BUSY                = 0x00000002,
GRBM_SE0_PERF_SEL_DB_BUSY                = 0x00000003,
GRBM_SE0_PERF_SEL_SC_BUSY                = 0x00000004,
GRBM_SE0_PERF_SEL_SPI_BUSY               = 0x00000006,
GRBM_SE0_PERF_SEL_SX_BUSY                = 0x00000007,
GRBM_SE0_PERF_SEL_TA_BUSY                = 0x00000008,
GRBM_SE0_PERF_SEL_CB_CLEAN               = 0x00000009,
GRBM_SE0_PERF_SEL_DB_CLEAN               = 0x0000000a,
GRBM_SE0_PERF_SEL_PA_BUSY                = 0x0000000c,
GRBM_SE0_PERF_SEL_BCI_BUSY               = 0x0000000e,
GRBM_SE0_PERF_SEL_RMI_BUSY               = 0x0000000f,
GRBM_SE0_PERF_SEL_UTCL1_BUSY             = 0x00000010,
GRBM_SE0_PERF_SEL_TCP_BUSY               = 0x00000011,
GRBM_SE0_PERF_SEL_GL1CC_BUSY             = 0x00000012,
GRBM_SE0_PERF_SEL_GL1H_BUSY              = 0x00000013,
GRBM_SE0_PERF_SEL_PC_BUSY                = 0x00000014,
} GRBM_SE0_PERF_SEL;

/*
 * GRBM_SE1_PERF_SEL enum
 */

typedef enum GRBM_SE1_PERF_SEL {
GRBM_SE1_PERF_SEL_COUNT                  = 0x00000000,
GRBM_SE1_PERF_SEL_USER_DEFINED           = 0x00000001,
GRBM_SE1_PERF_SEL_CB_BUSY                = 0x00000002,
GRBM_SE1_PERF_SEL_DB_BUSY                = 0x00000003,
GRBM_SE1_PERF_SEL_SC_BUSY                = 0x00000004,
GRBM_SE1_PERF_SEL_SPI_BUSY               = 0x00000006,
GRBM_SE1_PERF_SEL_SX_BUSY                = 0x00000007,
GRBM_SE1_PERF_SEL_TA_BUSY                = 0x00000008,
GRBM_SE1_PERF_SEL_CB_CLEAN               = 0x00000009,
GRBM_SE1_PERF_SEL_DB_CLEAN               = 0x0000000a,
GRBM_SE1_PERF_SEL_PA_BUSY                = 0x0000000c,
GRBM_SE1_PERF_SEL_BCI_BUSY               = 0x0000000e,
GRBM_SE1_PERF_SEL_RMI_BUSY               = 0x0000000f,
GRBM_SE1_PERF_SEL_UTCL1_BUSY             = 0x00000010,
GRBM_SE1_PERF_SEL_TCP_BUSY               = 0x00000011,
GRBM_SE1_PERF_SEL_GL1CC_BUSY             = 0x00000012,
GRBM_SE1_PERF_SEL_GL1H_BUSY              = 0x00000013,
GRBM_SE1_PERF_SEL_PC_BUSY                = 0x00000014,
} GRBM_SE1_PERF_SEL;

/*
 * GRBM_SE2_PERF_SEL enum
 */

typedef enum GRBM_SE2_PERF_SEL {
GRBM_SE2_PERF_SEL_COUNT                  = 0x00000000,
GRBM_SE2_PERF_SEL_USER_DEFINED           = 0x00000001,
GRBM_SE2_PERF_SEL_CB_BUSY                = 0x00000002,
GRBM_SE2_PERF_SEL_DB_BUSY                = 0x00000003,
GRBM_SE2_PERF_SEL_SC_BUSY                = 0x00000004,
GRBM_SE2_PERF_SEL_SPI_BUSY               = 0x00000006,
GRBM_SE2_PERF_SEL_SX_BUSY                = 0x00000007,
GRBM_SE2_PERF_SEL_TA_BUSY                = 0x00000008,
GRBM_SE2_PERF_SEL_CB_CLEAN               = 0x00000009,
GRBM_SE2_PERF_SEL_DB_CLEAN               = 0x0000000a,
GRBM_SE2_PERF_SEL_PA_BUSY                = 0x0000000c,
GRBM_SE2_PERF_SEL_BCI_BUSY               = 0x0000000e,
GRBM_SE2_PERF_SEL_RMI_BUSY               = 0x0000000f,
GRBM_SE2_PERF_SEL_UTCL1_BUSY             = 0x00000010,
GRBM_SE2_PERF_SEL_TCP_BUSY               = 0x00000011,
GRBM_SE2_PERF_SEL_GL1CC_BUSY             = 0x00000012,
GRBM_SE2_PERF_SEL_GL1H_BUSY              = 0x00000013,
GRBM_SE2_PERF_SEL_PC_BUSY                = 0x00000014,
} GRBM_SE2_PERF_SEL;

/*
 * GRBM_SE3_PERF_SEL enum
 */

typedef enum GRBM_SE3_PERF_SEL {
GRBM_SE3_PERF_SEL_COUNT                  = 0x00000000,
GRBM_SE3_PERF_SEL_USER_DEFINED           = 0x00000001,
GRBM_SE3_PERF_SEL_CB_BUSY                = 0x00000002,
GRBM_SE3_PERF_SEL_DB_BUSY                = 0x00000003,
GRBM_SE3_PERF_SEL_SC_BUSY                = 0x00000004,
GRBM_SE3_PERF_SEL_SPI_BUSY               = 0x00000006,
GRBM_SE3_PERF_SEL_SX_BUSY                = 0x00000007,
GRBM_SE3_PERF_SEL_TA_BUSY                = 0x00000008,
GRBM_SE3_PERF_SEL_CB_CLEAN               = 0x00000009,
GRBM_SE3_PERF_SEL_DB_CLEAN               = 0x0000000a,
GRBM_SE3_PERF_SEL_PA_BUSY                = 0x0000000c,
GRBM_SE3_PERF_SEL_BCI_BUSY               = 0x0000000e,
GRBM_SE3_PERF_SEL_RMI_BUSY               = 0x0000000f,
GRBM_SE3_PERF_SEL_UTCL1_BUSY             = 0x00000010,
GRBM_SE3_PERF_SEL_TCP_BUSY               = 0x00000011,
GRBM_SE3_PERF_SEL_GL1CC_BUSY             = 0x00000012,
GRBM_SE3_PERF_SEL_GL1H_BUSY              = 0x00000013,
GRBM_SE3_PERF_SEL_PC_BUSY                = 0x00000014,
} GRBM_SE3_PERF_SEL;

/*
 * GRBM_SE4_PERF_SEL enum
 */

typedef enum GRBM_SE4_PERF_SEL {
GRBM_SE4_PERF_SEL_COUNT                  = 0x00000000,
GRBM_SE4_PERF_SEL_USER_DEFINED           = 0x00000001,
GRBM_SE4_PERF_SEL_CB_BUSY                = 0x00000002,
GRBM_SE4_PERF_SEL_DB_BUSY                = 0x00000003,
GRBM_SE4_PERF_SEL_SC_BUSY                = 0x00000004,
GRBM_SE4_PERF_SEL_SPI_BUSY               = 0x00000006,
GRBM_SE4_PERF_SEL_SX_BUSY                = 0x00000007,
GRBM_SE4_PERF_SEL_TA_BUSY                = 0x00000008,
GRBM_SE4_PERF_SEL_CB_CLEAN               = 0x00000009,
GRBM_SE4_PERF_SEL_DB_CLEAN               = 0x0000000a,
GRBM_SE4_PERF_SEL_PA_BUSY                = 0x0000000c,
GRBM_SE4_PERF_SEL_BCI_BUSY               = 0x0000000e,
GRBM_SE4_PERF_SEL_RMI_BUSY               = 0x0000000f,
GRBM_SE4_PERF_SEL_UTCL1_BUSY             = 0x00000010,
GRBM_SE4_PERF_SEL_TCP_BUSY               = 0x00000011,
GRBM_SE4_PERF_SEL_GL1CC_BUSY             = 0x00000012,
GRBM_SE4_PERF_SEL_GL1H_BUSY              = 0x00000013,
GRBM_SE4_PERF_SEL_PC_BUSY                = 0x00000014,
} GRBM_SE4_PERF_SEL;

/*
 * GRBM_SE5_PERF_SEL enum
 */

typedef enum GRBM_SE5_PERF_SEL {
GRBM_SE5_PERF_SEL_COUNT                  = 0x00000000,
GRBM_SE5_PERF_SEL_USER_DEFINED           = 0x00000001,
GRBM_SE5_PERF_SEL_CB_BUSY                = 0x00000002,
GRBM_SE5_PERF_SEL_DB_BUSY                = 0x00000003,
GRBM_SE5_PERF_SEL_SC_BUSY                = 0x00000004,
GRBM_SE5_PERF_SEL_SPI_BUSY               = 0x00000006,
GRBM_SE5_PERF_SEL_SX_BUSY                = 0x00000007,
GRBM_SE5_PERF_SEL_TA_BUSY                = 0x00000008,
GRBM_SE5_PERF_SEL_CB_CLEAN               = 0x00000009,
GRBM_SE5_PERF_SEL_DB_CLEAN               = 0x0000000a,
GRBM_SE5_PERF_SEL_PA_BUSY                = 0x0000000c,
GRBM_SE5_PERF_SEL_BCI_BUSY               = 0x0000000e,
GRBM_SE5_PERF_SEL_RMI_BUSY               = 0x0000000f,
GRBM_SE5_PERF_SEL_UTCL1_BUSY             = 0x00000010,
GRBM_SE5_PERF_SEL_TCP_BUSY               = 0x00000011,
GRBM_SE5_PERF_SEL_GL1CC_BUSY             = 0x00000012,
GRBM_SE5_PERF_SEL_GL1H_BUSY              = 0x00000013,
GRBM_SE5_PERF_SEL_PC_BUSY                = 0x00000014,
} GRBM_SE5_PERF_SEL;

/*
 * GRBM_SE6_PERF_SEL enum
 */

typedef enum GRBM_SE6_PERF_SEL {
GRBM_SE6_PERF_SEL_COUNT                  = 0x00000000,
GRBM_SE6_PERF_SEL_USER_DEFINED           = 0x00000001,
GRBM_SE6_PERF_SEL_CB_BUSY                = 0x00000002,
GRBM_SE6_PERF_SEL_DB_BUSY                = 0x00000003,
GRBM_SE6_PERF_SEL_SC_BUSY                = 0x00000004,
GRBM_SE6_PERF_SEL_SPI_BUSY               = 0x00000006,
GRBM_SE6_PERF_SEL_SX_BUSY                = 0x00000007,
GRBM_SE6_PERF_SEL_TA_BUSY                = 0x00000008,
GRBM_SE6_PERF_SEL_CB_CLEAN               = 0x00000009,
GRBM_SE6_PERF_SEL_DB_CLEAN               = 0x0000000a,
GRBM_SE6_PERF_SEL_PA_BUSY                = 0x0000000c,
GRBM_SE6_PERF_SEL_BCI_BUSY               = 0x0000000e,
GRBM_SE6_PERF_SEL_RMI_BUSY               = 0x0000000f,
GRBM_SE6_PERF_SEL_UTCL1_BUSY             = 0x00000010,
GRBM_SE6_PERF_SEL_TCP_BUSY               = 0x00000011,
GRBM_SE6_PERF_SEL_GL1CC_BUSY             = 0x00000012,
GRBM_SE6_PERF_SEL_GL1H_BUSY              = 0x00000013,
GRBM_SE6_PERF_SEL_PC_BUSY                = 0x00000014,
} GRBM_SE6_PERF_SEL;

/*
 * GRBM_SE7_PERF_SEL enum
 */

typedef enum GRBM_SE7_PERF_SEL {
GRBM_SE7_PERF_SEL_COUNT                  = 0x00000000,
GRBM_SE7_PERF_SEL_USER_DEFINED           = 0x00000001,
GRBM_SE7_PERF_SEL_CB_BUSY                = 0x00000002,
GRBM_SE7_PERF_SEL_DB_BUSY                = 0x00000003,
GRBM_SE7_PERF_SEL_SC_BUSY                = 0x00000004,
GRBM_SE7_PERF_SEL_SPI_BUSY               = 0x00000006,
GRBM_SE7_PERF_SEL_SX_BUSY                = 0x00000007,
GRBM_SE7_PERF_SEL_TA_BUSY                = 0x00000008,
GRBM_SE7_PERF_SEL_CB_CLEAN               = 0x00000009,
GRBM_SE7_PERF_SEL_DB_CLEAN               = 0x0000000a,
GRBM_SE7_PERF_SEL_PA_BUSY                = 0x0000000c,
GRBM_SE7_PERF_SEL_BCI_BUSY               = 0x0000000e,
GRBM_SE7_PERF_SEL_RMI_BUSY               = 0x0000000f,
GRBM_SE7_PERF_SEL_UTCL1_BUSY             = 0x00000010,
GRBM_SE7_PERF_SEL_TCP_BUSY               = 0x00000011,
GRBM_SE7_PERF_SEL_GL1CC_BUSY             = 0x00000012,
GRBM_SE7_PERF_SEL_GL1H_BUSY              = 0x00000013,
GRBM_SE7_PERF_SEL_PC_BUSY                = 0x00000014,
} GRBM_SE7_PERF_SEL;

/*
 * PIPE_COMPAT_LEVEL enum
 */

typedef enum PIPE_COMPAT_LEVEL {
GEN_ZERO                                 = 0x00000000,
GEN_ONE                                  = 0x00000001,
GEN_TWO                                  = 0x00000002,
GEN_RESERVED                             = 0x00000003,
} PIPE_COMPAT_LEVEL;

/*******************************************************
 * CP Enums
 *******************************************************/

/*
 * CPC_LATENCY_STATS_SEL enum
 */

typedef enum CPC_LATENCY_STATS_SEL {
CPC_LATENCY_STATS_SEL_XACK_MAX           = 0x00000000,
CPC_LATENCY_STATS_SEL_XACK_MIN           = 0x00000001,
CPC_LATENCY_STATS_SEL_XACK_LAST          = 0x00000002,
CPC_LATENCY_STATS_SEL_XNACK_MAX          = 0x00000003,
CPC_LATENCY_STATS_SEL_XNACK_MIN          = 0x00000004,
CPC_LATENCY_STATS_SEL_XNACK_LAST         = 0x00000005,
CPC_LATENCY_STATS_SEL_INVAL_MAX          = 0x00000006,
CPC_LATENCY_STATS_SEL_INVAL_MIN          = 0x00000007,
CPC_LATENCY_STATS_SEL_INVAL_LAST         = 0x00000008,
} CPC_LATENCY_STATS_SEL;

/*
 * CPC_PERFCOUNT_SEL enum
 */

typedef enum CPC_PERFCOUNT_SEL {
CPC_PERF_SEL_ALWAYS_COUNT                = 0x00000000,
CPC_PERF_SEL_RCIU_STALL_WAIT_ON_FREE     = 0x00000001,
CPC_PERF_SEL_RCIU_STALL_PRIV_VIOLATION   = 0x00000002,
CPC_PERF_SEL_TCIU_STALL_WAIT_ON_FREE     = 0x00000005,
CPC_PERF_SEL_ME1_STALL_WAIT_ON_RCIU_READY = 0x00000006,
CPC_PERF_SEL_ME1_STALL_WAIT_ON_RCIU_READY_PERF = 0x00000007,
CPC_PERF_SEL_ME1_STALL_WAIT_ON_RCIU_READ = 0x00000008,
CPC_PERF_SEL_ME1_STALL_WAIT_ON_GUS_READ  = 0x00000009,
CPC_PERF_SEL_ME1_STALL_WAIT_ON_GUS_WRITE = 0x0000000a,
CPC_PERF_SEL_ME1_STALL_ON_DATA_FROM_ROQ  = 0x0000000b,
CPC_PERF_SEL_ME1_STALL_ON_DATA_FROM_ROQ_PERF = 0x0000000c,
CPC_PERF_SEL_ME1_BUSY_FOR_PACKET_DECODE  = 0x0000000d,
CPC_PERF_SEL_ME2_STALL_WAIT_ON_RCIU_READY = 0x0000000e,
CPC_PERF_SEL_ME2_STALL_WAIT_ON_RCIU_READY_PERF = 0x0000000f,
CPC_PERF_SEL_ME2_STALL_WAIT_ON_RCIU_READ = 0x00000010,
CPC_PERF_SEL_ME2_STALL_WAIT_ON_GUS_READ  = 0x00000011,
CPC_PERF_SEL_ME2_STALL_WAIT_ON_GUS_WRITE = 0x00000012,
CPC_PERF_SEL_ME2_STALL_ON_DATA_FROM_ROQ  = 0x00000013,
CPC_PERF_SEL_ME2_STALL_ON_DATA_FROM_ROQ_PERF = 0x00000014,
CPC_PERF_SEL_ME2_BUSY_FOR_PACKET_DECODE  = 0x00000015,
CPC_PERF_SEL_UTCL2IU_STALL_WAIT_ON_FREE  = 0x00000016,
CPC_PERF_SEL_UTCL2IU_STALL_WAIT_ON_TAGS  = 0x00000017,
CPC_PERF_SEL_UTCL1_STALL_ON_TRANSLATION  = 0x00000018,
CPC_PERF_SEL_CPC_STAT_BUSY               = 0x00000019,
CPC_PERF_SEL_CPC_STAT_IDLE               = 0x0000001a,
CPC_PERF_SEL_CPC_STAT_STALL              = 0x0000001b,
CPC_PERF_SEL_CPC_TCIU_BUSY               = 0x0000001c,
CPC_PERF_SEL_CPC_TCIU_IDLE               = 0x0000001d,
CPC_PERF_SEL_CPC_UTCL2IU_BUSY            = 0x0000001e,
CPC_PERF_SEL_CPC_UTCL2IU_IDLE            = 0x0000001f,
CPC_PERF_SEL_CPC_UTCL2IU_STALL           = 0x00000020,
CPC_PERF_SEL_ME1_DC0_SPI_BUSY            = 0x00000021,
CPC_PERF_SEL_ME2_DC1_SPI_BUSY            = 0x00000022,
CPC_PERF_SEL_CPC_GCRIU_BUSY              = 0x00000023,
CPC_PERF_SEL_CPC_GCRIU_IDLE              = 0x00000024,
CPC_PERF_SEL_CPC_GCRIU_STALL             = 0x00000025,
CPC_PERF_SEL_GCRIU_STALL_WAIT_ON_FREE    = 0x00000026,
CPC_PERF_SEL_ME1_STALL_WAIT_ON_TCIU_READ = 0x00000027,
CPC_PERF_SEL_ME2_STALL_WAIT_ON_TCIU_READ = 0x00000028,
CPC_PERF_SEL_CPC_UTCL2IU_XACK            = 0x00000029,
CPC_PERF_SEL_CPC_UTCL2IU_XNACK           = 0x0000002a,
CPC_PERF_SEL_MEC_INSTR_CACHE_HIT         = 0x0000002b,
CPC_PERF_SEL_MEC_INSTR_CACHE_MISS        = 0x0000002c,
CPC_PERF_SEL_MES_THREAD0                 = 0x0000002d,
CPC_PERF_SEL_MES_THREAD1                 = 0x0000002e,
} CPC_PERFCOUNT_SEL;

/*
 * CPF_LATENCY_STATS_SEL enum
 */

typedef enum CPF_LATENCY_STATS_SEL {
CPF_LATENCY_STATS_SEL_XACK_MAX           = 0x00000000,
CPF_LATENCY_STATS_SEL_XACK_MIN           = 0x00000001,
CPF_LATENCY_STATS_SEL_XACK_LAST          = 0x00000002,
CPF_LATENCY_STATS_SEL_XNACK_MAX          = 0x00000003,
CPF_LATENCY_STATS_SEL_XNACK_MIN          = 0x00000004,
CPF_LATENCY_STATS_SEL_XNACK_LAST         = 0x00000005,
CPF_LATENCY_STATS_SEL_READ_MAX           = 0x00000006,
CPF_LATENCY_STATS_SEL_READ_MIN           = 0x00000007,
CPF_LATENCY_STATS_SEL_READ_LAST          = 0x00000008,
CPF_LATENCY_STATS_SEL_INVAL_MAX          = 0x00000009,
CPF_LATENCY_STATS_SEL_INVAL_MIN          = 0x0000000a,
CPF_LATENCY_STATS_SEL_INVAL_LAST         = 0x0000000b,
} CPF_LATENCY_STATS_SEL;

/*
 * CPF_PERFCOUNTWINDOW_SEL enum
 */

typedef enum CPF_PERFCOUNTWINDOW_SEL {
CPF_PERFWINDOW_SEL_CSF                   = 0x00000000,
CPF_PERFWINDOW_SEL_HQD1                  = 0x00000001,
CPF_PERFWINDOW_SEL_HQD2                  = 0x00000002,
CPF_PERFWINDOW_SEL_RDMA                  = 0x00000003,
CPF_PERFWINDOW_SEL_RWPP                  = 0x00000004,
} CPF_PERFCOUNTWINDOW_SEL;

/*
 * CPF_PERFCOUNT_SEL enum
 */

typedef enum CPF_PERFCOUNT_SEL {
CPF_PERF_SEL_ALWAYS_COUNT                = 0x00000000,
CPF_PERF_SEL_TCIU_STALLED_WAITING_ON_FREE = 0x00000002,
CPF_PERF_SEL_TCIU_STALLED_WAITING_ON_TAGS = 0x00000003,
CPF_PERF_SEL_CSF_BUSY_FOR_FETCHING_RING  = 0x00000004,
CPF_PERF_SEL_CSF_BUSY_FOR_FETCHING_IB1   = 0x00000005,
CPF_PERF_SEL_CSF_BUSY_FOR_FETCHING_IB2   = 0x00000006,
CPF_PERF_SEL_CSF_BUSY_FOR_FETCHING_STATE = 0x00000007,
CPF_PERF_SEL_CSF_STATE_FIFO_NOT_RTR      = 0x0000000a,
CPF_PERF_SEL_CSF_FETCHING_CMD_BUFFERS    = 0x0000000b,
CPF_PERF_SEL_GRBM_DWORDS_SENT            = 0x0000000c,
CPF_PERF_SEL_DYNAMIC_CLOCK_VALID         = 0x0000000d,
CPF_PERF_SEL_REGISTER_CLOCK_VALID        = 0x0000000e,
CPF_PERF_SEL_GUS_WRITE_REQUEST_SENT      = 0x0000000f,
CPF_PERF_SEL_GUS_READ_REQUEST_SENT       = 0x00000010,
CPF_PERF_SEL_UTCL2IU_STALL_WAIT_ON_FREE  = 0x00000011,
CPF_PERF_SEL_UTCL2IU_STALL_WAIT_ON_TAGS  = 0x00000012,
CPF_PERF_SEL_GFX_UTCL1_STALL_ON_TRANSLATION = 0x00000013,
CPF_PERF_SEL_CMP_UTCL1_STALL_ON_TRANSLATION = 0x00000014,
CPF_PERF_SEL_RCIU_STALL_WAIT_ON_FREE     = 0x00000015,
CPF_PERF_SEL_TCIU_WRITE_REQUEST_SENT     = 0x00000016,
CPF_PERF_SEL_TCIU_READ_REQUEST_SENT      = 0x00000017,
CPF_PERF_SEL_CPF_STAT_BUSY               = 0x00000018,
CPF_PERF_SEL_CPF_STAT_IDLE               = 0x00000019,
CPF_PERF_SEL_CPF_STAT_STALL              = 0x0000001a,
CPF_PERF_SEL_CPF_TCIU_BUSY               = 0x0000001b,
CPF_PERF_SEL_CPF_TCIU_IDLE               = 0x0000001c,
CPF_PERF_SEL_CPF_TCIU_STALL              = 0x0000001d,
CPF_PERF_SEL_CPF_UTCL2IU_BUSY            = 0x0000001e,
CPF_PERF_SEL_CPF_UTCL2IU_IDLE            = 0x0000001f,
CPF_PERF_SEL_CPF_UTCL2IU_STALL           = 0x00000020,
CPF_PERF_SEL_CPF_GCRIU_BUSY              = 0x00000021,
CPF_PERF_SEL_CPF_GCRIU_IDLE              = 0x00000022,
CPF_PERF_SEL_CPF_GCRIU_STALL             = 0x00000023,
CPF_PERF_SEL_GCRIU_STALL_WAIT_ON_FREE    = 0x00000024,
CPF_PERF_SEL_CSF_BUSY_FOR_FETCHING_DB    = 0x00000025,
CPF_PERF_SEL_CPF_UTCL2IU_XACK            = 0x00000026,
CPF_PERF_SEL_CPF_UTCL2IU_XNACK           = 0x00000027,
CPF_PERF_SEL_CP_SDMA_MNGR_DMA_REQ        = 0x00000028,
CPF_PERF_SEL_CP_SDMA_MNGR_DMA_DONE       = 0x00000029,
CPF_PERF_SEL_CP_SDMA_MNGR_LATENCY        = 0x0000002a,
CPF_PERF_SEL_CP_SDMA_MNGR_SDMABUSY       = 0x0000002b,
} CPF_PERFCOUNT_SEL;

/*
 * CPF_SCRATCH_REG_ATOMIC_OP enum
 */

typedef enum CPF_SCRATCH_REG_ATOMIC_OP {
CPF_SCRATCH_REG_ATOMIC_ADD               = 0x00000000,
CPF_SCRATCH_REG_ATOMIC_SUB               = 0x00000001,
CPF_SCRATCH_REG_ATOMIC_OR                = 0x00000002,
CPF_SCRATCH_REG_ATOMIC_AND               = 0x00000003,
CPF_SCRATCH_REG_ATOMIC_NOT               = 0x00000004,
CPF_SCRATCH_REG_ATOMIC_MIN               = 0x00000005,
CPF_SCRATCH_REG_ATOMIC_MAX               = 0x00000006,
CPF_SCRATCH_REG_ATOMIC_CMPSWAP           = 0x00000007,
} CPF_SCRATCH_REG_ATOMIC_OP;

/*
 * CPG_LATENCY_STATS_SEL enum
 */

typedef enum CPG_LATENCY_STATS_SEL {
CPG_LATENCY_STATS_SEL_XACK_MAX           = 0x00000000,
CPG_LATENCY_STATS_SEL_XACK_MIN           = 0x00000001,
CPG_LATENCY_STATS_SEL_XACK_LAST          = 0x00000002,
CPG_LATENCY_STATS_SEL_XNACK_MAX          = 0x00000003,
CPG_LATENCY_STATS_SEL_XNACK_MIN          = 0x00000004,
CPG_LATENCY_STATS_SEL_XNACK_LAST         = 0x00000005,
CPG_LATENCY_STATS_SEL_WRITE_MAX          = 0x00000006,
CPG_LATENCY_STATS_SEL_WRITE_MIN          = 0x00000007,
CPG_LATENCY_STATS_SEL_WRITE_LAST         = 0x00000008,
CPG_LATENCY_STATS_SEL_READ_MAX           = 0x00000009,
CPG_LATENCY_STATS_SEL_READ_MIN           = 0x0000000a,
CPG_LATENCY_STATS_SEL_READ_LAST          = 0x0000000b,
CPG_LATENCY_STATS_SEL_ATOMIC_MAX         = 0x0000000c,
CPG_LATENCY_STATS_SEL_ATOMIC_MIN         = 0x0000000d,
CPG_LATENCY_STATS_SEL_ATOMIC_LAST        = 0x0000000e,
CPG_LATENCY_STATS_SEL_INVAL_MAX          = 0x0000000f,
CPG_LATENCY_STATS_SEL_INVAL_MIN          = 0x00000010,
CPG_LATENCY_STATS_SEL_INVAL_LAST         = 0x00000011,
} CPG_LATENCY_STATS_SEL;

/*
 * CPG_PERFCOUNTWINDOW_SEL enum
 */

typedef enum CPG_PERFCOUNTWINDOW_SEL {
CPG_PERFWINDOW_SEL_PFP                   = 0x00000000,
CPG_PERFWINDOW_SEL_ME                    = 0x00000001,
CPG_PERFWINDOW_SEL_CE                    = 0x00000002,
CPG_PERFWINDOW_SEL_MES                   = 0x00000003,
CPG_PERFWINDOW_SEL_MEC1                  = 0x00000004,
CPG_PERFWINDOW_SEL_MEC2                  = 0x00000005,
CPG_PERFWINDOW_SEL_DFY                   = 0x00000006,
CPG_PERFWINDOW_SEL_DMA                   = 0x00000007,
CPG_PERFWINDOW_SEL_SHADOW                = 0x00000008,
CPG_PERFWINDOW_SEL_RB                    = 0x00000009,
CPG_PERFWINDOW_SEL_CEDMA                 = 0x0000000a,
CPG_PERFWINDOW_SEL_PRT_HDR_RPTR          = 0x0000000b,
CPG_PERFWINDOW_SEL_PRT_SMP_RPTR          = 0x0000000c,
CPG_PERFWINDOW_SEL_PQ1                   = 0x0000000d,
CPG_PERFWINDOW_SEL_PQ2                   = 0x0000000e,
CPG_PERFWINDOW_SEL_PQ3                   = 0x0000000f,
CPG_PERFWINDOW_SEL_MEMWR                 = 0x00000010,
CPG_PERFWINDOW_SEL_MEMRD                 = 0x00000011,
CPG_PERFWINDOW_SEL_VGT0                  = 0x00000012,
CPG_PERFWINDOW_SEL_VGT1                  = 0x00000013,
CPG_PERFWINDOW_SEL_APPEND                = 0x00000014,
CPG_PERFWINDOW_SEL_QURD                  = 0x00000015,
CPG_PERFWINDOW_SEL_DDID                  = 0x00000016,
CPG_PERFWINDOW_SEL_SR                    = 0x00000017,
CPG_PERFWINDOW_SEL_QU_EOP                = 0x00000018,
CPG_PERFWINDOW_SEL_QU_STRM               = 0x00000019,
CPG_PERFWINDOW_SEL_QU_PIPE               = 0x0000001a,
CPG_PERFWINDOW_SEL_RESERVED1             = 0x0000001b,
CPG_PERFWINDOW_SEL_CPC_IC                = 0x0000001c,
CPG_PERFWINDOW_SEL_RESERVED2             = 0x0000001d,
CPG_PERFWINDOW_SEL_CPG_IC                = 0x0000001e,
} CPG_PERFCOUNTWINDOW_SEL;

/*
 * CPG_PERFCOUNT_SEL enum
 */

typedef enum CPG_PERFCOUNT_SEL {
CPG_PERF_SEL_ALWAYS_COUNT                = 0x00000000,
CPG_PERF_SEL_RBIU_FIFO_FULL              = 0x00000001,
CPG_PERF_SEL_CP_GRBM_DWORDS_SENT         = 0x00000004,
CPG_PERF_SEL_ME_PARSER_BUSY              = 0x00000005,
CPG_PERF_SEL_COUNT_TYPE0_PACKETS         = 0x00000006,
CPG_PERF_SEL_COUNT_TYPE3_PACKETS         = 0x00000007,
CPG_PERF_SEL_CP_GRBM_OUT_OF_CREDITS      = 0x00000009,
CPG_PERF_SEL_CP_PFP_GRBM_OUT_OF_CREDITS  = 0x0000000a,
CPG_PERF_SEL_CP_GDS_GRBM_OUT_OF_CREDITS  = 0x0000000b,
CPG_PERF_SEL_RCIU_STALLED_ON_ME_READ     = 0x0000000c,
CPG_PERF_SEL_RCIU_STALLED_ON_DMA_READ    = 0x0000000d,
CPG_PERF_SEL_SSU_STALLED_ON_ACTIVE_CNTX  = 0x0000000e,
CPG_PERF_SEL_SSU_STALLED_ON_CLEAN_SIGNALS = 0x0000000f,
CPG_PERF_SEL_QU_STALLED_ON_EOP_DONE_PULSE = 0x00000010,
CPG_PERF_SEL_QU_STALLED_ON_EOP_DONE_WR_CONFIRM = 0x00000011,
CPG_PERF_SEL_PFP_STALLED_ON_CSF_READY    = 0x00000012,
CPG_PERF_SEL_PFP_STALLED_ON_MEQ_READY    = 0x00000013,
CPG_PERF_SEL_PFP_STALLED_ON_RCIU_READY   = 0x00000014,
CPG_PERF_SEL_PFP_STALLED_FOR_DATA_FROM_ROQ = 0x00000015,
CPG_PERF_SEL_ME_STALLED_FOR_DATA_FROM_PFP = 0x00000016,
CPG_PERF_SEL_ME_STALLED_FOR_DATA_FROM_STQ = 0x00000017,
CPG_PERF_SEL_ME_STALLED_ON_NO_AVAIL_GFX_CNTX = 0x00000018,
CPG_PERF_SEL_ME_STALLED_WRITING_TO_RCIU  = 0x00000019,
CPG_PERF_SEL_ME_STALLED_WRITING_CONSTANTS = 0x0000001a,
CPG_PERF_SEL_ME_STALLED_ON_PARTIAL_FLUSH = 0x0000001b,
CPG_PERF_SEL_ME_WAIT_ON_CE_COUNTER       = 0x0000001c,
CPG_PERF_SEL_ME_WAIT_ON_AVAIL_BUFFER     = 0x0000001d,
CPG_PERF_SEL_LOAD_STALLED_ON_SET_COHERENCY = 0x0000001f,
CPG_PERF_SEL_DYNAMIC_CLK_VALID           = 0x00000020,
CPG_PERF_SEL_REGISTER_CLK_VALID          = 0x00000021,
CPG_PERF_SEL_GUS_WRITE_REQUEST_SENT      = 0x00000022,
CPG_PERF_SEL_GUS_READ_REQUEST_SENT       = 0x00000023,
CPG_PERF_SEL_CE_STALL_RAM_DUMP           = 0x00000024,
CPG_PERF_SEL_CE_STALL_RAM_WRITE          = 0x00000025,
CPG_PERF_SEL_CE_STALL_ON_INC_FIFO        = 0x00000026,
CPG_PERF_SEL_CE_STALL_ON_WR_RAM_FIFO     = 0x00000027,
CPG_PERF_SEL_CE_STALL_ON_DATA_FROM_ROQ   = 0x00000029,
CPG_PERF_SEL_CE_STALL_ON_CE_BUFFER_FLAG  = 0x0000002a,
CPG_PERF_SEL_CE_STALL_ON_DE_COUNTER      = 0x0000002b,
CPG_PERF_SEL_TCIU_STALL_WAIT_ON_FREE     = 0x0000002c,
CPG_PERF_SEL_TCIU_STALL_WAIT_ON_TAGS     = 0x0000002d,
CPG_PERF_SEL_UTCL2IU_STALL_WAIT_ON_FREE  = 0x0000002e,
CPG_PERF_SEL_UTCL2IU_STALL_WAIT_ON_TAGS  = 0x0000002f,
CPG_PERF_SEL_UTCL1_STALL_ON_TRANSLATION  = 0x00000030,
CPG_PERF_SEL_TCIU_WRITE_REQUEST_SENT     = 0x00000031,
CPG_PERF_SEL_TCIU_READ_REQUEST_SENT      = 0x00000032,
CPG_PERF_SEL_CPG_STAT_BUSY               = 0x00000033,
CPG_PERF_SEL_CPG_STAT_IDLE               = 0x00000034,
CPG_PERF_SEL_CPG_STAT_STALL              = 0x00000035,
CPG_PERF_SEL_CPG_TCIU_BUSY               = 0x00000036,
CPG_PERF_SEL_CPG_TCIU_IDLE               = 0x00000037,
CPG_PERF_SEL_CPG_TCIU_STALL              = 0x00000038,
CPG_PERF_SEL_CPG_UTCL2IU_BUSY            = 0x00000039,
CPG_PERF_SEL_CPG_UTCL2IU_IDLE            = 0x0000003a,
CPG_PERF_SEL_CPG_UTCL2IU_STALL           = 0x0000003b,
CPG_PERF_SEL_CPG_GCRIU_BUSY              = 0x0000003c,
CPG_PERF_SEL_CPG_GCRIU_IDLE              = 0x0000003d,
CPG_PERF_SEL_CPG_GCRIU_STALL             = 0x0000003e,
CPG_PERF_SEL_GCRIU_STALL_WAIT_ON_FREE    = 0x0000003f,
CPG_PERF_SEL_ALL_GFX_PIPES_BUSY          = 0x00000040,
CPG_PERF_SEL_CPG_UTCL2IU_XACK            = 0x00000041,
CPG_PERF_SEL_CPG_UTCL2IU_XNACK           = 0x00000042,
CPG_PERF_SEL_PFP_STALLED_ON_MEQ_DDID_READY = 0x00000043,
CPG_PERF_SEL_PFP_INSTR_CACHE_HIT         = 0x00000044,
CPG_PERF_SEL_PFP_INSTR_CACHE_MISS        = 0x00000045,
CPG_PERF_SEL_CE_INSTR_CACHE_HIT          = 0x00000046,
CPG_PERF_SEL_CE_INSTR_CACHE_MISS         = 0x00000047,
CPG_PERF_SEL_ME_INSTR_CACHE_HIT          = 0x00000048,
CPG_PERF_SEL_ME_INSTR_CACHE_MISS         = 0x00000049,
CPG_PERF_SEL_PFP_PACKET_FILTER_HIT_IB1   = 0x0000004a,
CPG_PERF_SEL_PFP_PACKET_FILTER_MISS_IB1  = 0x0000004b,
CPG_PERF_SEL_PFP_PACKET_FILTER_HIT_IB2   = 0x0000004c,
CPG_PERF_SEL_PFP_PACKET_FILTER_MISS_IB2  = 0x0000004d,
CPG_PERF_SEL_DMA_BUSY                    = 0x0000004e,
CPG_PERF_SEL_DMA_STARVED                 = 0x0000004f,
CPG_PERF_SEL_DMA_STALLED                 = 0x00000050,
CPG_PERF_SEL_DMA_FETCHER_STALLED_ON_ROQ_FULL = 0x00000051,
CPG_PERF_SEL_PFP_PWS_STALLED0            = 0x00000052,
CPG_PERF_SEL_ME_PWS_STALLED0             = 0x00000053,
CPG_PERF_SEL_PFP_PWS_STALLED1            = 0x00000054,
CPG_PERF_SEL_ME_PWS_STALLED1             = 0x00000055,
} CPG_PERFCOUNT_SEL;

/*
 * CP_ALPHA_TAG_RAM_SEL enum
 */

typedef enum CP_ALPHA_TAG_RAM_SEL {
CPG_TAG_RAM                              = 0x00000000,
CPC_TAG_RAM                              = 0x00000001,
CPF_TAG_RAM                              = 0x00000002,
RSV_TAG_RAM                              = 0x00000003,
} CP_ALPHA_TAG_RAM_SEL;

/*
 * CP_DDID_CNTL_MODE enum
 */

typedef enum CP_DDID_CNTL_MODE {
STALL                                    = 0x00000000,
OVERRUN                                  = 0x00000001,
} CP_DDID_CNTL_MODE;

/*
 * CP_DDID_CNTL_SIZE enum
 */

typedef enum CP_DDID_CNTL_SIZE {
SIZE_8K                                  = 0x00000000,
SIZE_16K                                 = 0x00000001,
} CP_DDID_CNTL_SIZE;

/*
 * CP_DDID_CNTL_VMID_SEL enum
 */

typedef enum CP_DDID_CNTL_VMID_SEL {
DDID_VMID_PIPE                           = 0x00000000,
DDID_VMID_CNTL                           = 0x00000001,
} CP_DDID_CNTL_VMID_SEL;

/*
 * CP_ME_ID enum
 */

typedef enum CP_ME_ID {
ME_ID0                                   = 0x00000000,
ME_ID1                                   = 0x00000001,
ME_ID2                                   = 0x00000002,
ME_ID3                                   = 0x00000003,
} CP_ME_ID;

/*
 * CP_PERFMON_ENABLE_MODE enum
 */

typedef enum CP_PERFMON_ENABLE_MODE {
CP_PERFMON_ENABLE_MODE_ALWAYS_COUNT      = 0x00000000,
CP_PERFMON_ENABLE_MODE_RESERVED_1        = 0x00000001,
CP_PERFMON_ENABLE_MODE_COUNT_CONTEXT_TRUE = 0x00000002,
CP_PERFMON_ENABLE_MODE_COUNT_CONTEXT_FALSE = 0x00000003,
} CP_PERFMON_ENABLE_MODE;

/*
 * CP_PERFMON_STATE enum
 */

typedef enum CP_PERFMON_STATE {
CP_PERFMON_STATE_DISABLE_AND_RESET       = 0x00000000,
CP_PERFMON_STATE_START_COUNTING          = 0x00000001,
CP_PERFMON_STATE_STOP_COUNTING           = 0x00000002,
CP_PERFMON_STATE_RESERVED_3              = 0x00000003,
CP_PERFMON_STATE_DISABLE_AND_RESET_PHANTOM = 0x00000004,
CP_PERFMON_STATE_COUNT_AND_DUMP_PHANTOM  = 0x00000005,
} CP_PERFMON_STATE;

/*
 * CP_PIPE_ID enum
 */

typedef enum CP_PIPE_ID {
PIPE_ID0                                 = 0x00000000,
PIPE_ID1                                 = 0x00000001,
PIPE_ID2                                 = 0x00000002,
PIPE_ID3                                 = 0x00000003,
} CP_PIPE_ID;

/*
 * CP_RING_ID enum
 */

typedef enum CP_RING_ID {
RINGID0                                  = 0x00000000,
RINGID1                                  = 0x00000001,
RINGID2                                  = 0x00000002,
RINGID3                                  = 0x00000003,
} CP_RING_ID;

/*
 * SPM_PERFMON_STATE enum
 */

typedef enum SPM_PERFMON_STATE {
STRM_PERFMON_STATE_DISABLE_AND_RESET     = 0x00000000,
STRM_PERFMON_STATE_START_COUNTING        = 0x00000001,
STRM_PERFMON_STATE_STOP_COUNTING         = 0x00000002,
STRM_PERFMON_STATE_RESERVED_3            = 0x00000003,
STRM_PERFMON_STATE_DISABLE_AND_RESET_PHANTOM = 0x00000004,
STRM_PERFMON_STATE_COUNT_AND_DUMP_PHANTOM = 0x00000005,
} SPM_PERFMON_STATE;

/*
 * SEM_RESPONSE value
 */

#define SEM_ECC_ERROR                  0x00000000
#define SEM_TRANS_ERROR                0x00000001
#define SEM_RESP_FAILED                0x00000002
#define SEM_RESP_PASSED                0x00000003

/*
 * IQ_RETRY_TYPE value
 */

#define IQ_QUEUE_SLEEP                 0x00000000
#define IQ_OFFLOAD_RETRY               0x00000001
#define IQ_SCH_WAVE_MSG                0x00000002
#define IQ_SEM_REARM                   0x00000003
#define IQ_DEQUEUE_RETRY               0x00000004

/*
 * IQ_INTR_TYPE value
 */

#define IQ_INTR_TYPE_PQ                0x00000000
#define IQ_INTR_TYPE_IB                0x00000001
#define IQ_INTR_TYPE_MQD               0x00000002

/*
 * VMID_SIZE value
 */

#define VMID_SZ                        0x00000004

/*
 * SRCID_SECURE value
 */

#define SRCID_RLC                      0x00000000
#define SRCID_RLCV                     0x00000006
#define SRCID_SECURE_CP                0x00000007
#define SRCID_NONSECURE_CP             0x00000001
#define SRCID_SECURE_CP_RCIU           0x00000007
#define SRCID_NONSECURE_CP_RCIU        0x00000001

/*
 * CONFIG_SPACE value
 */

#define CONFIG_SPACE_START             0x00002000
#define CONFIG_SPACE_END               0x00009fff

/*
 * CONFIG_SPACE1 value
 */

#define CONFIG_SPACE1_START            0x00002000
#define CONFIG_SPACE1_END              0x00002bff

/*
 * CONFIG_SPACE2 value
 */

#define CONFIG_SPACE2_START            0x00003000
#define CONFIG_SPACE2_END              0x00009fff

/*
 * UCONFIG_SPACE value
 */

#define UCONFIG_SPACE_START            0x0000c000
#define UCONFIG_SPACE_END              0x0000ffff

/*
 * PERSISTENT_SPACE value
 */

#define PERSISTENT_SPACE_START         0x00002c00
#define PERSISTENT_SPACE_END           0x00002fff

/*
 * CONTEXT_SPACE value
 */

#define CONTEXT_SPACE_START            0x0000a000
#define CONTEXT_SPACE_END              0x0000a3ff

/*******************************************************
 * SX Enums
 *******************************************************/

/*
 * SX_BLEND_OPT enum
 */

typedef enum SX_BLEND_OPT {
BLEND_OPT_PRESERVE_NONE_IGNORE_ALL       = 0x00000000,
BLEND_OPT_PRESERVE_ALL_IGNORE_NONE       = 0x00000001,
BLEND_OPT_PRESERVE_C1_IGNORE_C0          = 0x00000002,
BLEND_OPT_PRESERVE_C0_IGNORE_C1          = 0x00000003,
BLEND_OPT_PRESERVE_A1_IGNORE_A0          = 0x00000004,
BLEND_OPT_PRESERVE_A0_IGNORE_A1          = 0x00000005,
BLEND_OPT_PRESERVE_NONE_IGNORE_A0        = 0x00000006,
BLEND_OPT_PRESERVE_NONE_IGNORE_NONE      = 0x00000007,
} SX_BLEND_OPT;

/*
 * SX_DOWNCONVERT_FORMAT enum
 */

typedef enum SX_DOWNCONVERT_FORMAT {
SX_RT_EXPORT_NO_CONVERSION               = 0x00000000,
SX_RT_EXPORT_32_R                        = 0x00000001,
SX_RT_EXPORT_32_A                        = 0x00000002,
SX_RT_EXPORT_10_11_11                    = 0x00000003,
SX_RT_EXPORT_2_10_10_10                  = 0x00000004,
SX_RT_EXPORT_8_8_8_8                     = 0x00000005,
SX_RT_EXPORT_5_6_5                       = 0x00000006,
SX_RT_EXPORT_1_5_5_5                     = 0x00000007,
SX_RT_EXPORT_4_4_4_4                     = 0x00000008,
SX_RT_EXPORT_16_16_GR                    = 0x00000009,
SX_RT_EXPORT_16_16_AR                    = 0x0000000a,
SX_RT_EXPORT_9_9_9_E5                    = 0x0000000b,
SX_RT_EXPORT_2_10_10_10_7E3              = 0x0000000c,
SX_RT_EXPORT_2_10_10_10_6E4              = 0x0000000d,
} SX_DOWNCONVERT_FORMAT;

/*
 * SX_OPT_COMB_FCN enum
 */

typedef enum SX_OPT_COMB_FCN {
OPT_COMB_NONE                            = 0x00000000,
OPT_COMB_ADD                             = 0x00000001,
OPT_COMB_SUBTRACT                        = 0x00000002,
OPT_COMB_MIN                             = 0x00000003,
OPT_COMB_MAX                             = 0x00000004,
OPT_COMB_REVSUBTRACT                     = 0x00000005,
OPT_COMB_BLEND_DISABLED                  = 0x00000006,
OPT_COMB_SAFE_ADD                        = 0x00000007,
} SX_OPT_COMB_FCN;

/*
 * SX_PERFCOUNTER_VALS enum
 */

typedef enum SX_PERFCOUNTER_VALS {
SX_PERF_SEL_PA_IDLE_CYCLES               = 0x00000000,
SX_PERF_SEL_PA_REQ                       = 0x00000001,
SX_PERF_SEL_PA_POS                       = 0x00000002,
SX_PERF_SEL_CLOCK                        = 0x00000003,
SX_PERF_SEL_GATE_EN1                     = 0x00000004,
SX_PERF_SEL_GATE_EN2                     = 0x00000005,
SX_PERF_SEL_GATE_EN3                     = 0x00000006,
SX_PERF_SEL_GATE_EN4                     = 0x00000007,
SX_PERF_SEL_SH_POS_STARVE                = 0x00000008,
SX_PERF_SEL_SH_COLOR_STARVE              = 0x00000009,
SX_PERF_SEL_SH_POS_STALL                 = 0x0000000a,
SX_PERF_SEL_SH_COLOR_STALL               = 0x0000000b,
SX_PERF_SEL_DB0_PIXELS                   = 0x0000000c,
SX_PERF_SEL_DB0_HALF_QUADS               = 0x0000000d,
SX_PERF_SEL_DB0_PIXEL_STALL              = 0x0000000e,
SX_PERF_SEL_DB0_PIXEL_IDLE               = 0x0000000f,
SX_PERF_SEL_DB0_PRED_PIXELS              = 0x00000010,
SX_PERF_SEL_DB1_PIXELS                   = 0x00000011,
SX_PERF_SEL_DB1_HALF_QUADS               = 0x00000012,
SX_PERF_SEL_DB1_PIXEL_STALL              = 0x00000013,
SX_PERF_SEL_DB1_PIXEL_IDLE               = 0x00000014,
SX_PERF_SEL_DB1_PRED_PIXELS              = 0x00000015,
SX_PERF_SEL_DB2_PIXELS                   = 0x00000016,
SX_PERF_SEL_DB2_HALF_QUADS               = 0x00000017,
SX_PERF_SEL_DB2_PIXEL_STALL              = 0x00000018,
SX_PERF_SEL_DB2_PIXEL_IDLE               = 0x00000019,
SX_PERF_SEL_DB2_PRED_PIXELS              = 0x0000001a,
SX_PERF_SEL_DB3_PIXELS                   = 0x0000001b,
SX_PERF_SEL_DB3_HALF_QUADS               = 0x0000001c,
SX_PERF_SEL_DB3_PIXEL_STALL              = 0x0000001d,
SX_PERF_SEL_DB3_PIXEL_IDLE               = 0x0000001e,
SX_PERF_SEL_DB3_PRED_PIXELS              = 0x0000001f,
SX_PERF_SEL_COL_BUSY                     = 0x00000020,
SX_PERF_SEL_POS_BUSY                     = 0x00000021,
SX_PERF_SEL_DB0_MRT_BLEND_BYPASS         = 0x00000022,
SX_PERF_SEL_DB0_MRT_DONT_RD_DEST         = 0x00000023,
SX_PERF_SEL_DB0_MRT_DISCARD_SRC          = 0x00000024,
SX_PERF_SEL_DB0_MRT_SINGLE_QUADS         = 0x00000025,
SX_PERF_SEL_DB0_MRT_DOUBLE_QUADS         = 0x00000026,
SX_PERF_SEL_DB1_MRT_BLEND_BYPASS         = 0x00000027,
SX_PERF_SEL_DB1_MRT_DONT_RD_DEST         = 0x00000028,
SX_PERF_SEL_DB1_MRT_DISCARD_SRC          = 0x00000029,
SX_PERF_SEL_DB1_MRT_SINGLE_QUADS         = 0x0000002a,
SX_PERF_SEL_DB1_MRT_DOUBLE_QUADS         = 0x0000002b,
SX_PERF_SEL_DB2_MRT_BLEND_BYPASS         = 0x0000002c,
SX_PERF_SEL_DB2_MRT_DONT_RD_DEST         = 0x0000002d,
SX_PERF_SEL_DB2_MRT_DISCARD_SRC          = 0x0000002e,
SX_PERF_SEL_DB2_MRT_SINGLE_QUADS         = 0x0000002f,
SX_PERF_SEL_DB2_MRT_DOUBLE_QUADS         = 0x00000030,
SX_PERF_SEL_DB3_MRT_BLEND_BYPASS         = 0x00000031,
SX_PERF_SEL_DB3_MRT_DONT_RD_DEST         = 0x00000032,
SX_PERF_SEL_DB3_MRT_DISCARD_SRC          = 0x00000033,
SX_PERF_SEL_DB3_MRT_SINGLE_QUADS         = 0x00000034,
SX_PERF_SEL_DB3_MRT_DOUBLE_QUADS         = 0x00000035,
SX_PERF_SEL_PA_REQ_LATENCY               = 0x00000036,
SX_PERF_SEL_POS_SCBD_STALL               = 0x00000037,
SX_PERF_SEL_CLOCK_DROP_STALL             = 0x00000038,
SX_PERF_SEL_GATE_EN5                     = 0x00000039,
SX_PERF_SEL_GATE_EN6                     = 0x0000003a,
SX_PERF_SEL_DB0_SIZE                     = 0x0000003b,
SX_PERF_SEL_DB1_SIZE                     = 0x0000003c,
SX_PERF_SEL_DB2_SIZE                     = 0x0000003d,
SX_PERF_SEL_DB3_SIZE                     = 0x0000003e,
SX_PERF_SEL_IDX_STALL_CYCLES             = 0x0000003f,
SX_PERF_SEL_IDX_IDLE_CYCLES              = 0x00000040,
SX_PERF_SEL_IDX_REQ                      = 0x00000041,
SX_PERF_SEL_IDX_RET                      = 0x00000042,
SX_PERF_SEL_IDX_REQ_LATENCY              = 0x00000043,
SX_PERF_SEL_IDX_SCBD_STALL               = 0x00000044,
SX_PERF_SEL_GATE_EN7                     = 0x00000045,
SX_PERF_SEL_GATE_EN8                     = 0x00000046,
SX_PERF_SEL_SH_IDX_STARVE                = 0x00000047,
SX_PERF_SEL_IDX_BUSY                     = 0x00000048,
SX_PERF_SEL_PA_POS_BANK_CONF             = 0x00000049,
SX_PERF_SEL_DB0_END_OF_WAVE              = 0x0000004a,
SX_PERF_SEL_DB0_4X2_DISCARD              = 0x0000004b,
SX_PERF_SEL_DB1_END_OF_WAVE              = 0x0000004c,
SX_PERF_SEL_DB1_4X2_DISCARD              = 0x0000004d,
SX_PERF_SEL_DB2_END_OF_WAVE              = 0x0000004e,
SX_PERF_SEL_DB2_4X2_DISCARD              = 0x0000004f,
SX_PERF_SEL_DB3_END_OF_WAVE              = 0x00000050,
SX_PERF_SEL_DB3_4X2_DISCARD              = 0x00000051,
} SX_PERFCOUNTER_VALS;

/*******************************************************
 * DB Enums
 *******************************************************/

/*
 * CompareFrag enum
 */

typedef enum CompareFrag {
FRAG_NEVER                               = 0x00000000,
FRAG_LESS                                = 0x00000001,
FRAG_EQUAL                               = 0x00000002,
FRAG_LEQUAL                              = 0x00000003,
FRAG_GREATER                             = 0x00000004,
FRAG_NOTEQUAL                            = 0x00000005,
FRAG_GEQUAL                              = 0x00000006,
FRAG_ALWAYS                              = 0x00000007,
} CompareFrag;

/*
 * ConservativeZExport enum
 */

typedef enum ConservativeZExport {
EXPORT_ANY_Z                             = 0x00000000,
EXPORT_LESS_THAN_Z                       = 0x00000001,
EXPORT_GREATER_THAN_Z                    = 0x00000002,
EXPORT_RESERVED                          = 0x00000003,
} ConservativeZExport;

/*
 * DFSMFlushEvents enum
 */

typedef enum DFSMFlushEvents {
DB_FLUSH_AND_INV_DB_DATA_TS              = 0x00000000,
DB_FLUSH_AND_INV_DB_META                 = 0x00000001,
DB_CACHE_FLUSH                           = 0x00000002,
DB_CACHE_FLUSH_TS                        = 0x00000003,
DB_CACHE_FLUSH_AND_INV_EVENT             = 0x00000004,
DB_CACHE_FLUSH_AND_INV_TS_EVENT          = 0x00000005,
DB_VPORT_CHANGED_EVENT                   = 0x00000006,
DB_CONTEXT_DONE_EVENT                    = 0x00000007,
DB_BREAK_BATCH_EVENT                     = 0x00000008,
DB_INVOKE_CHANGE_EVENT                   = 0x00000009,
DB_CONTEXT_SUSPEND_EVENT                 = 0x0000000a,
} DFSMFlushEvents;

/*
 * DbMemArbWatermarks enum
 */

typedef enum DbMemArbWatermarks {
TRANSFERRED_64_BYTES                     = 0x00000000,
TRANSFERRED_128_BYTES                    = 0x00000001,
TRANSFERRED_256_BYTES                    = 0x00000002,
TRANSFERRED_512_BYTES                    = 0x00000003,
TRANSFERRED_1024_BYTES                   = 0x00000004,
TRANSFERRED_2048_BYTES                   = 0x00000005,
TRANSFERRED_4096_BYTES                   = 0x00000006,
TRANSFERRED_8192_BYTES                   = 0x00000007,
} DbMemArbWatermarks;

/*
 * DbPRTFaultBehavior enum
 */

typedef enum DbPRTFaultBehavior {
FAULT_ZERO                               = 0x00000000,
FAULT_ONE                                = 0x00000001,
FAULT_FAIL                               = 0x00000002,
FAULT_PASS                               = 0x00000003,
} DbPRTFaultBehavior;

/*
 * DbPSLControl enum
 */

typedef enum DbPSLControl {
PSLC_AUTO                                = 0x00000000,
PSLC_ON_HANG_ONLY                        = 0x00000001,
PSLC_ASAP                                = 0x00000002,
PSLC_COUNTDOWN                           = 0x00000003,
} DbPSLControl;

/*
 * ForceControl enum
 */

typedef enum ForceControl {
FORCE_OFF                                = 0x00000000,
FORCE_ENABLE                             = 0x00000001,
FORCE_DISABLE                            = 0x00000002,
FORCE_RESERVED                           = 0x00000003,
} ForceControl;

/*
 * OreoMode enum
 */

typedef enum OreoMode {
OMODE_BLEND                              = 0x00000000,
OMODE_O_THEN_B                           = 0x00000001,
OMODE_P_THEN_O_THEN_B                    = 0x00000002,
OMODE_RESERVED_3                         = 0x00000003,
} OreoMode;

/*
 * PerfCounter_Vals enum
 */

typedef enum PerfCounter_Vals {
DB_PERF_SEL_SC_DB_tile_sends             = 0x00000000,
DB_PERF_SEL_SC_DB_tile_busy              = 0x00000001,
DB_PERF_SEL_SC_DB_tile_stalls            = 0x00000002,
DB_PERF_SEL_SC_DB_tile_events            = 0x00000003,
DB_PERF_SEL_SC_DB_tile_tiles             = 0x00000004,
DB_PERF_SEL_SC_DB_tile_covered           = 0x00000005,
DB_PERF_SEL_hiz_tc_read_starved          = 0x00000006,
DB_PERF_SEL_hiz_tc_write_stall           = 0x00000007,
DB_PERF_SEL_hiz_tile_culled              = 0x00000008,
DB_PERF_SEL_his_tile_culled              = 0x00000009,
DB_PERF_SEL_DB_SC_tile_sends             = 0x0000000a,
DB_PERF_SEL_DB_SC_tile_busy              = 0x0000000b,
DB_PERF_SEL_DB_SC_tile_stalls            = 0x0000000c,
DB_PERF_SEL_DB_SC_tile_df_stalls         = 0x0000000d,
DB_PERF_SEL_DB_SC_tile_tiles             = 0x0000000e,
DB_PERF_SEL_DB_SC_tile_culled            = 0x0000000f,
DB_PERF_SEL_DB_SC_tile_hier_kill         = 0x00000010,
DB_PERF_SEL_DB_SC_tile_fast_ops          = 0x00000011,
DB_PERF_SEL_DB_SC_tile_no_ops            = 0x00000012,
DB_PERF_SEL_DB_SC_tile_tile_rate         = 0x00000013,
DB_PERF_SEL_DB_SC_tile_ssaa_kill         = 0x00000014,
DB_PERF_SEL_DB_SC_tile_fast_z_ops        = 0x00000015,
DB_PERF_SEL_DB_SC_tile_fast_stencil_ops  = 0x00000016,
DB_PERF_SEL_SC_DB_quad_sends             = 0x00000017,
DB_PERF_SEL_SC_DB_quad_busy              = 0x00000018,
DB_PERF_SEL_SC_DB_quad_squads            = 0x00000019,
DB_PERF_SEL_SC_DB_quad_tiles             = 0x0000001a,
DB_PERF_SEL_SC_DB_quad_pixels            = 0x0000001b,
DB_PERF_SEL_SC_DB_quad_killed_tiles      = 0x0000001c,
DB_PERF_SEL_DB_SC_quad_sends             = 0x0000001d,
DB_PERF_SEL_DB_SC_quad_busy              = 0x0000001e,
DB_PERF_SEL_DB_SC_quad_stalls            = 0x0000001f,
DB_PERF_SEL_DB_SC_quad_tiles             = 0x00000020,
DB_PERF_SEL_DB_SC_quad_lit_quad          = 0x00000021,
DB_PERF_SEL_DB_CB_tile_sends             = 0x00000022,
DB_PERF_SEL_DB_CB_tile_busy              = 0x00000023,
DB_PERF_SEL_DB_CB_tile_stalls            = 0x00000024,
DB_PERF_SEL_SX_DB_quad_sends             = 0x00000025,
DB_PERF_SEL_SX_DB_quad_busy              = 0x00000026,
DB_PERF_SEL_SX_DB_quad_stalls            = 0x00000027,
DB_PERF_SEL_SX_DB_quad_quads             = 0x00000028,
DB_PERF_SEL_SX_DB_quad_pixels            = 0x00000029,
DB_PERF_SEL_SX_DB_quad_exports           = 0x0000002a,
DB_PERF_SEL_SH_quads_outstanding_sum     = 0x0000002b,
DB_PERF_SEL_DB_CB_lquad_sends            = 0x0000002c,
DB_PERF_SEL_DB_CB_lquad_busy             = 0x0000002d,
DB_PERF_SEL_DB_CB_lquad_stalls           = 0x0000002e,
DB_PERF_SEL_DB_CB_lquad_quads            = 0x0000002f,
DB_PERF_SEL_tile_rd_sends                = 0x00000030,
DB_PERF_SEL_mi_tile_rd_outstanding_sum   = 0x00000031,
DB_PERF_SEL_quad_rd_sends                = 0x00000032,
DB_PERF_SEL_quad_rd_busy                 = 0x00000033,
DB_PERF_SEL_quad_rd_mi_stall             = 0x00000034,
DB_PERF_SEL_quad_rd_rw_collision         = 0x00000035,
DB_PERF_SEL_quad_rd_tag_stall            = 0x00000036,
DB_PERF_SEL_quad_rd_32byte_reqs          = 0x00000037,
DB_PERF_SEL_quad_rd_panic                = 0x00000038,
DB_PERF_SEL_mi_quad_rd_outstanding_sum   = 0x00000039,
DB_PERF_SEL_quad_rdret_sends             = 0x0000003a,
DB_PERF_SEL_quad_rdret_busy              = 0x0000003b,
DB_PERF_SEL_tile_wr_sends                = 0x0000003c,
DB_PERF_SEL_tile_wr_acks                 = 0x0000003d,
DB_PERF_SEL_mi_tile_wr_outstanding_sum   = 0x0000003e,
DB_PERF_SEL_quad_wr_sends                = 0x0000003f,
DB_PERF_SEL_quad_wr_busy                 = 0x00000040,
DB_PERF_SEL_quad_wr_mi_stall             = 0x00000041,
DB_PERF_SEL_quad_wr_coherency_stall      = 0x00000042,
DB_PERF_SEL_quad_wr_acks                 = 0x00000043,
DB_PERF_SEL_mi_quad_wr_outstanding_sum   = 0x00000044,
DB_PERF_SEL_Tile_Cache_misses            = 0x00000045,
DB_PERF_SEL_Tile_Cache_hits              = 0x00000046,
DB_PERF_SEL_Tile_Cache_flushes           = 0x00000047,
DB_PERF_SEL_Tile_Cache_surface_stall     = 0x00000048,
DB_PERF_SEL_Tile_Cache_starves           = 0x00000049,
DB_PERF_SEL_Tile_Cache_mem_return_starve = 0x0000004a,
DB_PERF_SEL_tcp_dispatcher_reads         = 0x0000004b,
DB_PERF_SEL_tcp_prefetcher_reads         = 0x0000004c,
DB_PERF_SEL_tcp_preloader_reads          = 0x0000004d,
DB_PERF_SEL_tcp_dispatcher_flushes       = 0x0000004e,
DB_PERF_SEL_tcp_prefetcher_flushes       = 0x0000004f,
DB_PERF_SEL_tcp_preloader_flushes        = 0x00000050,
DB_PERF_SEL_Depth_Tile_Cache_sends       = 0x00000051,
DB_PERF_SEL_Depth_Tile_Cache_busy        = 0x00000052,
DB_PERF_SEL_Depth_Tile_Cache_starves     = 0x00000053,
DB_PERF_SEL_Depth_Tile_Cache_dtile_locked = 0x00000054,
DB_PERF_SEL_Depth_Tile_Cache_alloc_stall = 0x00000055,
DB_PERF_SEL_Depth_Tile_Cache_misses      = 0x00000056,
DB_PERF_SEL_Depth_Tile_Cache_hits        = 0x00000057,
DB_PERF_SEL_Depth_Tile_Cache_flushes     = 0x00000058,
DB_PERF_SEL_Depth_Tile_Cache_noop_tile   = 0x00000059,
DB_PERF_SEL_Depth_Tile_Cache_detailed_noop = 0x0000005a,
DB_PERF_SEL_Depth_Tile_Cache_event       = 0x0000005b,
DB_PERF_SEL_Depth_Tile_Cache_tile_frees  = 0x0000005c,
DB_PERF_SEL_Depth_Tile_Cache_data_frees  = 0x0000005d,
DB_PERF_SEL_Depth_Tile_Cache_mem_return_starve = 0x0000005e,
DB_PERF_SEL_Stencil_Cache_misses         = 0x0000005f,
DB_PERF_SEL_Stencil_Cache_hits           = 0x00000060,
DB_PERF_SEL_Stencil_Cache_flushes        = 0x00000061,
DB_PERF_SEL_Stencil_Cache_starves        = 0x00000062,
DB_PERF_SEL_Stencil_Cache_frees          = 0x00000063,
DB_PERF_SEL_Z_Cache_separate_Z_misses    = 0x00000064,
DB_PERF_SEL_Z_Cache_separate_Z_hits      = 0x00000065,
DB_PERF_SEL_Z_Cache_separate_Z_flushes   = 0x00000066,
DB_PERF_SEL_Z_Cache_separate_Z_starves   = 0x00000067,
DB_PERF_SEL_Z_Cache_pmask_misses         = 0x00000068,
DB_PERF_SEL_Z_Cache_pmask_hits           = 0x00000069,
DB_PERF_SEL_Z_Cache_pmask_flushes        = 0x0000006a,
DB_PERF_SEL_Z_Cache_pmask_starves        = 0x0000006b,
DB_PERF_SEL_Z_Cache_frees                = 0x0000006c,
DB_PERF_SEL_Plane_Cache_misses           = 0x0000006d,
DB_PERF_SEL_Plane_Cache_hits             = 0x0000006e,
DB_PERF_SEL_Plane_Cache_flushes          = 0x0000006f,
DB_PERF_SEL_Plane_Cache_starves          = 0x00000070,
DB_PERF_SEL_Plane_Cache_frees            = 0x00000071,
DB_PERF_SEL_flush_expanded_stencil       = 0x00000072,
DB_PERF_SEL_flush_compressed_stencil     = 0x00000073,
DB_PERF_SEL_flush_single_stencil         = 0x00000074,
DB_PERF_SEL_planes_flushed               = 0x00000075,
DB_PERF_SEL_flush_1plane                 = 0x00000076,
DB_PERF_SEL_flush_2plane                 = 0x00000077,
DB_PERF_SEL_flush_3plane                 = 0x00000078,
DB_PERF_SEL_flush_4plane                 = 0x00000079,
DB_PERF_SEL_flush_5plane                 = 0x0000007a,
DB_PERF_SEL_flush_6plane                 = 0x0000007b,
DB_PERF_SEL_flush_7plane                 = 0x0000007c,
DB_PERF_SEL_flush_8plane                 = 0x0000007d,
DB_PERF_SEL_flush_9plane                 = 0x0000007e,
DB_PERF_SEL_flush_10plane                = 0x0000007f,
DB_PERF_SEL_flush_11plane                = 0x00000080,
DB_PERF_SEL_flush_12plane                = 0x00000081,
DB_PERF_SEL_flush_13plane                = 0x00000082,
DB_PERF_SEL_flush_14plane                = 0x00000083,
DB_PERF_SEL_flush_15plane                = 0x00000084,
DB_PERF_SEL_flush_16plane                = 0x00000085,
DB_PERF_SEL_flush_expanded_z             = 0x00000086,
DB_PERF_SEL_earlyZ_waiting_for_postZ_done = 0x00000087,
DB_PERF_SEL_reZ_waiting_for_postZ_done   = 0x00000088,
DB_PERF_SEL_dk_tile_sends                = 0x00000089,
DB_PERF_SEL_dk_tile_busy                 = 0x0000008a,
DB_PERF_SEL_dk_tile_quad_starves         = 0x0000008b,
DB_PERF_SEL_dk_tile_stalls               = 0x0000008c,
DB_PERF_SEL_dk_squad_sends               = 0x0000008d,
DB_PERF_SEL_dk_squad_busy                = 0x0000008e,
DB_PERF_SEL_dk_squad_stalls              = 0x0000008f,
DB_PERF_SEL_Op_Pipe_Busy                 = 0x00000090,
DB_PERF_SEL_Op_Pipe_MC_Read_stall        = 0x00000091,
DB_PERF_SEL_qc_busy                      = 0x00000092,
DB_PERF_SEL_qc_xfc                       = 0x00000093,
DB_PERF_SEL_qc_conflicts                 = 0x00000094,
DB_PERF_SEL_qc_full_stall                = 0x00000095,
DB_PERF_SEL_qc_in_preZ_tile_stalls_postZ = 0x00000096,
DB_PERF_SEL_qc_in_postZ_tile_stalls_preZ = 0x00000097,
DB_PERF_SEL_tsc_insert_summarize_stall   = 0x00000098,
DB_PERF_SEL_tl_busy                      = 0x00000099,
DB_PERF_SEL_tl_dtc_read_starved          = 0x0000009a,
DB_PERF_SEL_tl_z_fetch_stall             = 0x0000009b,
DB_PERF_SEL_tl_stencil_stall             = 0x0000009c,
DB_PERF_SEL_tl_z_decompress_stall        = 0x0000009d,
DB_PERF_SEL_tl_stencil_locked_stall      = 0x0000009e,
DB_PERF_SEL_tl_events                    = 0x0000009f,
DB_PERF_SEL_tl_summarize_squads          = 0x000000a0,
DB_PERF_SEL_tl_flush_expand_squads       = 0x000000a1,
DB_PERF_SEL_tl_expand_squads             = 0x000000a2,
DB_PERF_SEL_tl_preZ_squads               = 0x000000a3,
DB_PERF_SEL_tl_postZ_squads              = 0x000000a4,
DB_PERF_SEL_tl_preZ_noop_squads          = 0x000000a5,
DB_PERF_SEL_tl_postZ_noop_squads         = 0x000000a6,
DB_PERF_SEL_tl_tile_ops                  = 0x000000a7,
DB_PERF_SEL_tl_in_xfc                    = 0x000000a8,
DB_PERF_SEL_tl_in_single_stencil_expand_stall = 0x000000a9,
DB_PERF_SEL_tl_in_fast_z_stall           = 0x000000aa,
DB_PERF_SEL_tl_out_xfc                   = 0x000000ab,
DB_PERF_SEL_tl_out_squads                = 0x000000ac,
DB_PERF_SEL_zf_plane_multicycle          = 0x000000ad,
DB_PERF_SEL_PostZ_Samples_passing_Z      = 0x000000ae,
DB_PERF_SEL_PostZ_Samples_failing_Z      = 0x000000af,
DB_PERF_SEL_PostZ_Samples_failing_S      = 0x000000b0,
DB_PERF_SEL_PreZ_Samples_passing_Z       = 0x000000b1,
DB_PERF_SEL_PreZ_Samples_failing_Z       = 0x000000b2,
DB_PERF_SEL_PreZ_Samples_failing_S       = 0x000000b3,
DB_PERF_SEL_ts_tc_update_stall           = 0x000000b4,
DB_PERF_SEL_sc_kick_start                = 0x000000b5,
DB_PERF_SEL_sc_kick_end                  = 0x000000b6,
DB_PERF_SEL_clock_reg_active             = 0x000000b7,
DB_PERF_SEL_clock_main_active            = 0x000000b8,
DB_PERF_SEL_clock_mem_export_active      = 0x000000b9,
DB_PERF_SEL_esr_ps_out_busy              = 0x000000ba,
DB_PERF_SEL_esr_ps_lqf_busy              = 0x000000bb,
DB_PERF_SEL_esr_ps_lqf_stall             = 0x000000bc,
DB_PERF_SEL_etr_out_send                 = 0x000000bd,
DB_PERF_SEL_etr_out_busy                 = 0x000000be,
DB_PERF_SEL_etr_out_ltile_probe_fifo_full_stall = 0x000000bf,
DB_PERF_SEL_etr_out_cb_tile_stall        = 0x000000c0,
DB_PERF_SEL_etr_out_esr_stall            = 0x000000c1,
DB_PERF_SEL_esr_ps_vic_busy              = 0x000000c2,
DB_PERF_SEL_esr_ps_vic_stall             = 0x000000c3,
DB_PERF_SEL_esr_eot_fwd_busy             = 0x000000c4,
DB_PERF_SEL_esr_eot_fwd_holding_squad    = 0x000000c5,
DB_PERF_SEL_esr_eot_fwd_forward          = 0x000000c6,
DB_PERF_SEL_esr_sqq_zi_busy              = 0x000000c7,
DB_PERF_SEL_esr_sqq_zi_stall             = 0x000000c8,
DB_PERF_SEL_postzl_sq_pt_busy            = 0x000000c9,
DB_PERF_SEL_postzl_sq_pt_stall           = 0x000000ca,
DB_PERF_SEL_postzl_se_busy               = 0x000000cb,
DB_PERF_SEL_postzl_se_stall              = 0x000000cc,
DB_PERF_SEL_postzl_partial_launch        = 0x000000cd,
DB_PERF_SEL_postzl_full_launch           = 0x000000ce,
DB_PERF_SEL_postzl_partial_waiting       = 0x000000cf,
DB_PERF_SEL_postzl_tile_mem_stall        = 0x000000d0,
DB_PERF_SEL_postzl_tile_init_stall       = 0x000000d1,
DB_PERF_SEL_prezl_tile_mem_stall         = 0x000000d2,
DB_PERF_SEL_prezl_tile_init_stall        = 0x000000d3,
DB_PERF_SEL_dtt_sm_clash_stall           = 0x000000d4,
DB_PERF_SEL_dtt_sm_slot_stall            = 0x000000d5,
DB_PERF_SEL_dtt_sm_miss_stall            = 0x000000d6,
DB_PERF_SEL_mi_rdreq_busy                = 0x000000d7,
DB_PERF_SEL_mi_rdreq_stall               = 0x000000d8,
DB_PERF_SEL_mi_wrreq_busy                = 0x000000d9,
DB_PERF_SEL_mi_wrreq_stall               = 0x000000da,
DB_PERF_SEL_recomp_tile_to_1zplane_no_fastop = 0x000000db,
DB_PERF_SEL_dkg_tile_rate_tile           = 0x000000dc,
DB_PERF_SEL_prezl_src_in_sends           = 0x000000dd,
DB_PERF_SEL_prezl_src_in_stall           = 0x000000de,
DB_PERF_SEL_prezl_src_in_squads          = 0x000000df,
DB_PERF_SEL_prezl_src_in_squads_unrolled = 0x000000e0,
DB_PERF_SEL_prezl_src_in_tile_rate       = 0x000000e1,
DB_PERF_SEL_prezl_src_in_tile_rate_unrolled = 0x000000e2,
DB_PERF_SEL_prezl_src_out_stall          = 0x000000e3,
DB_PERF_SEL_postzl_src_in_sends          = 0x000000e4,
DB_PERF_SEL_postzl_src_in_stall          = 0x000000e5,
DB_PERF_SEL_postzl_src_in_squads         = 0x000000e6,
DB_PERF_SEL_postzl_src_in_squads_unrolled = 0x000000e7,
DB_PERF_SEL_postzl_src_in_tile_rate      = 0x000000e8,
DB_PERF_SEL_postzl_src_in_tile_rate_unrolled = 0x000000e9,
DB_PERF_SEL_postzl_src_out_stall         = 0x000000ea,
DB_PERF_SEL_esr_ps_src_in_sends          = 0x000000eb,
DB_PERF_SEL_esr_ps_src_in_stall          = 0x000000ec,
DB_PERF_SEL_esr_ps_src_in_squads         = 0x000000ed,
DB_PERF_SEL_esr_ps_src_in_squads_unrolled = 0x000000ee,
DB_PERF_SEL_esr_ps_src_in_tile_rate      = 0x000000ef,
DB_PERF_SEL_esr_ps_src_in_tile_rate_unrolled = 0x000000f0,
DB_PERF_SEL_esr_ps_src_in_tile_rate_unrolled_to_pixel_rate = 0x000000f1,
DB_PERF_SEL_esr_ps_src_out_stall         = 0x000000f2,
DB_PERF_SEL_depth_bounds_tile_culled     = 0x000000f3,
DB_PERF_SEL_PreZ_Samples_failing_DB      = 0x000000f4,
DB_PERF_SEL_PostZ_Samples_failing_DB     = 0x000000f5,
DB_PERF_SEL_flush_compressed             = 0x000000f6,
DB_PERF_SEL_flush_plane_le4              = 0x000000f7,
DB_PERF_SEL_tiles_z_fully_summarized     = 0x000000f8,
DB_PERF_SEL_tiles_stencil_fully_summarized = 0x000000f9,
DB_PERF_SEL_tiles_z_clear_on_expclear    = 0x000000fa,
DB_PERF_SEL_tiles_s_clear_on_expclear    = 0x000000fb,
DB_PERF_SEL_tiles_decomp_on_expclear     = 0x000000fc,
DB_PERF_SEL_tiles_compressed_to_decompressed = 0x000000fd,
DB_PERF_SEL_Op_Pipe_Prez_Busy            = 0x000000fe,
DB_PERF_SEL_Op_Pipe_Postz_Busy           = 0x000000ff,
DB_PERF_SEL_di_dt_stall                  = 0x00000100,
Spare_257                                = 0x00000101,
DB_PERF_SEL_DB_SC_s_tile_rate            = 0x00000102,
DB_PERF_SEL_DB_SC_c_tile_rate            = 0x00000103,
DB_PERF_SEL_DB_SC_z_tile_rate            = 0x00000104,
DB_PERF_SEL_DB_CB_lquad_export_quads     = 0x00000105,
DB_PERF_SEL_DB_CB_lquad_double_format    = 0x00000106,
DB_PERF_SEL_DB_CB_lquad_fast_format      = 0x00000107,
DB_PERF_SEL_DB_CB_lquad_slow_format      = 0x00000108,
DB_PERF_SEL_CB_DB_rdreq_sends            = 0x00000109,
DB_PERF_SEL_CB_DB_rdreq_prt_sends        = 0x0000010a,
DB_PERF_SEL_CB_DB_wrreq_sends            = 0x0000010b,
DB_PERF_SEL_CB_DB_wrreq_prt_sends        = 0x0000010c,
DB_PERF_SEL_DB_CB_rdret_ack              = 0x0000010d,
DB_PERF_SEL_DB_CB_rdret_nack             = 0x0000010e,
DB_PERF_SEL_DB_CB_wrret_ack              = 0x0000010f,
DB_PERF_SEL_DB_CB_wrret_nack             = 0x00000110,
DB_PERF_SEL_MI_tile_req_wrack_counter_stall = 0x00000111,
DB_PERF_SEL_MI_quad_req_wrack_counter_stall = 0x00000112,
DB_PERF_SEL_MI_zpc_req_wrack_counter_stall = 0x00000113,
DB_PERF_SEL_MI_psd_req_wrack_counter_stall = 0x00000114,
DB_PERF_SEL_unmapped_z_tile_culled       = 0x00000115,
DB_PERF_SEL_DB_CB_tile_is_event_FLUSH_AND_INV_DB_DATA_TS = 0x00000116,
DB_PERF_SEL_DB_CB_tile_is_event_FLUSH_AND_INV_CB_PIXEL_DATA = 0x00000117,
DB_PERF_SEL_DB_CB_tile_is_event_BOTTOM_OF_PIPE_TS = 0x00000118,
DB_PERF_SEL_DB_CB_tile_waiting_for_perfcounter_stop_event = 0x00000119,
DB_PERF_SEL_DB_CB_lquad_fmt_32bpp_8pix   = 0x0000011a,
DB_PERF_SEL_DB_CB_lquad_fmt_16_16_unsigned_8pix = 0x0000011b,
DB_PERF_SEL_DB_CB_lquad_fmt_16_16_signed_8pix = 0x0000011c,
DB_PERF_SEL_DB_CB_lquad_fmt_16_16_float_8pix = 0x0000011d,
DB_PERF_SEL_DB_CB_lquad_num_pixels_need_blending = 0x0000011e,
DB_PERF_SEL_DB_CB_context_dones          = 0x0000011f,
DB_PERF_SEL_DB_CB_eop_dones              = 0x00000120,
DB_PERF_SEL_SX_DB_quad_all_pixels_killed = 0x00000121,
DB_PERF_SEL_SX_DB_quad_all_pixels_enabled = 0x00000122,
DB_PERF_SEL_SX_DB_quad_need_blending_and_dst_read = 0x00000123,
DB_PERF_SEL_SC_DB_tile_backface          = 0x00000124,
DB_PERF_SEL_SC_DB_quad_quads             = 0x00000125,
DB_PERF_SEL_DB_SC_quad_quads_with_1_pixel = 0x00000126,
DB_PERF_SEL_DB_SC_quad_quads_with_2_pixels = 0x00000127,
DB_PERF_SEL_DB_SC_quad_quads_with_3_pixels = 0x00000128,
DB_PERF_SEL_DB_SC_quad_quads_with_4_pixels = 0x00000129,
DB_PERF_SEL_DB_SC_quad_double_quad       = 0x0000012a,
DB_PERF_SEL_SX_DB_quad_export_quads      = 0x0000012b,
DB_PERF_SEL_SX_DB_quad_double_format     = 0x0000012c,
DB_PERF_SEL_SX_DB_quad_fast_format       = 0x0000012d,
DB_PERF_SEL_SX_DB_quad_slow_format       = 0x0000012e,
DB_PERF_SEL_quad_rd_sends_unc            = 0x0000012f,
DB_PERF_SEL_quad_rd_mi_stall_unc         = 0x00000130,
DB_PERF_SEL_SC_DB_tile_tiles_pipe0       = 0x00000131,
DB_PERF_SEL_SC_DB_tile_tiles_pipe1       = 0x00000132,
DB_PERF_SEL_SC_DB_quad_quads_pipe0       = 0x00000133,
DB_PERF_SEL_SC_DB_quad_quads_pipe1       = 0x00000134,
DB_PERF_SEL_PERF_fg_lob_fwdr_timeout_hits = 0x00000135,
DB_PERF_SEL_noz_waiting_for_postz_done   = 0x00000136,
DB_PERF_SEL_DB_CB_lquad_quads_vrs_rate_1x1 = 0x00000137,
DB_PERF_SEL_DB_CB_lquad_quads_vrs_rate_2x1 = 0x00000138,
DB_PERF_SEL_DB_CB_lquad_quads_vrs_rate_1x2 = 0x00000139,
DB_PERF_SEL_DB_CB_lquad_quads_vrs_rate_2x2 = 0x0000013a,
DB_PERF_SEL_RMI_rd_tile_32byte_req       = 0x0000013b,
DB_PERF_SEL_RMI_rd_z_32byte_req          = 0x0000013c,
DB_PERF_SEL_RMI_rd_s_32byte_req          = 0x0000013d,
DB_PERF_SEL_RMI_wr_tile_32byte_req       = 0x0000013e,
DB_PERF_SEL_RMI_wr_z_32byte_req          = 0x0000013f,
DB_PERF_SEL_RMI_wr_s_32byte_req          = 0x00000140,
DB_PERF_SEL_RMI_wr_psdzpc_32byte_req     = 0x00000141,
DB_PERF_SEL_RMI_rd_tile_32byte_ret       = 0x00000142,
DB_PERF_SEL_RMI_rd_z_32byte_ret          = 0x00000143,
DB_PERF_SEL_RMI_rd_s_32byte_ret          = 0x00000144,
DB_PERF_SEL_RMI_wr_tile_32byte_ack       = 0x00000145,
DB_PERF_SEL_RMI_wr_z_32byte_ack          = 0x00000146,
DB_PERF_SEL_RMI_wr_s_32byte_ack          = 0x00000147,
DB_PERF_SEL_RMI_wr_psdzpc_32byte_ack     = 0x00000148,
DB_PERF_SEL_esr_vic_sqq_busy             = 0x00000149,
DB_PERF_SEL_esr_vic_sqq_stall            = 0x0000014a,
DB_PERF_SEL_esr_psi_vic_tile_rate        = 0x0000014b,
DB_PERF_SEL_esr_vic_footprint_match_2x2  = 0x0000014c,
DB_PERF_SEL_esr_vic_footprint_match_2x1  = 0x0000014d,
DB_PERF_SEL_esr_vic_footprint_match_1x2  = 0x0000014e,
DB_PERF_SEL_DB_SC_quad_num_null_2x2_coarse_pixels = 0x0000014f,
DB_PERF_SEL_DB_SC_quad_num_null_2x1_coarse_pixels = 0x00000150,
DB_PERF_SEL_DB_SC_quad_num_null_1x2_coarse_pixels = 0x00000151,
DB_PERF_SEL_hi_z_s_checker_force_coarse_vrs_1x1 = 0x00000152,
DB_PERF_SEL_hi_z_s_checker_force_ssaa_vrs_1x1 = 0x00000153,
DB_PERF_SEL_esr_ps_woc_1squadIn_2squadOut = 0x00000154,
DB_PERF_SEL_esr_ps_woc_2squadIn_1squadOut = 0x00000155,
DB_PERF_SEL_prez_ps_invoked_pixel_cnt    = 0x00000156,
DB_PERF_SEL_postz_ps_invoked_pixel_cnt   = 0x00000157,
DB_PERF_SEL_ts_events_pws_enable         = 0x00000158,
DB_PERF_SEL_ps_events_pws_enable         = 0x00000159,
DB_PERF_SEL_cs_events_pws_enable         = 0x0000015a,
DB_PERF_SEL_DB_SC_quad_noz_tiles         = 0x0000015b,
DB_PERF_SEL_DB_SC_quad_lit_noz_quad      = 0x0000015c,
} PerfCounter_Vals;

/*
 * PixelPipeCounterId enum
 */

typedef enum PixelPipeCounterId {
PIXEL_PIPE_OCCLUSION_COUNT_0             = 0x00000000,
PIXEL_PIPE_OCCLUSION_COUNT_1             = 0x00000001,
PIXEL_PIPE_OCCLUSION_COUNT_2             = 0x00000002,
PIXEL_PIPE_OCCLUSION_COUNT_3             = 0x00000003,
PIXEL_PIPE_SCREEN_MIN_EXTENTS_0          = 0x00000004,
PIXEL_PIPE_SCREEN_MAX_EXTENTS_0          = 0x00000005,
PIXEL_PIPE_SCREEN_MIN_EXTENTS_1          = 0x00000006,
PIXEL_PIPE_SCREEN_MAX_EXTENTS_1          = 0x00000007,
} PixelPipeCounterId;

/*
 * PixelPipeStride enum
 */

typedef enum PixelPipeStride {
PIXEL_PIPE_STRIDE_32_BITS                = 0x00000000,
PIXEL_PIPE_STRIDE_64_BITS                = 0x00000001,
PIXEL_PIPE_STRIDE_128_BITS               = 0x00000002,
PIXEL_PIPE_STRIDE_256_BITS               = 0x00000003,
} PixelPipeStride;

/*
 * RingCounterControl enum
 */

typedef enum RingCounterControl {
COUNTER_RING_SPLIT                       = 0x00000000,
COUNTER_RING_0                           = 0x00000001,
COUNTER_RING_1                           = 0x00000002,
} RingCounterControl;

/*
 * StencilOp enum
 */

typedef enum StencilOp {
STENCIL_KEEP                             = 0x00000000,
STENCIL_ZERO                             = 0x00000001,
STENCIL_ONES                             = 0x00000002,
STENCIL_REPLACE_TEST                     = 0x00000003,
STENCIL_REPLACE_OP                       = 0x00000004,
STENCIL_ADD_CLAMP                        = 0x00000005,
STENCIL_SUB_CLAMP                        = 0x00000006,
STENCIL_INVERT                           = 0x00000007,
STENCIL_ADD_WRAP                         = 0x00000008,
STENCIL_SUB_WRAP                         = 0x00000009,
STENCIL_AND                              = 0x0000000a,
STENCIL_OR                               = 0x0000000b,
STENCIL_XOR                              = 0x0000000c,
STENCIL_NAND                             = 0x0000000d,
STENCIL_NOR                              = 0x0000000e,
STENCIL_XNOR                             = 0x0000000f,
} StencilOp;

/*
 * ZLimitSumm enum
 */

typedef enum ZLimitSumm {
FORCE_SUMM_OFF                           = 0x00000000,
FORCE_SUMM_MINZ                          = 0x00000001,
FORCE_SUMM_MAXZ                          = 0x00000002,
FORCE_SUMM_BOTH                          = 0x00000003,
} ZLimitSumm;

/*
 * ZModeForce enum
 */

typedef enum ZModeForce {
NO_FORCE                                 = 0x00000000,
FORCE_EARLY_Z                            = 0x00000001,
FORCE_LATE_Z                             = 0x00000002,
FORCE_RE_Z                               = 0x00000003,
} ZModeForce;

/*
 * ZOrder enum
 */

typedef enum ZOrder {
LATE_Z                                   = 0x00000000,
EARLY_Z_THEN_LATE_Z                      = 0x00000001,
RE_Z                                     = 0x00000002,
EARLY_Z_THEN_RE_Z                        = 0x00000003,
} ZOrder;

/*
 * ZSamplePosition enum
 */

typedef enum ZSamplePosition {
Z_SAMPLE_CENTER                          = 0x00000000,
Z_SAMPLE_CENTROID                        = 0x00000001,
} ZSamplePosition;

/*
 * ZpassControl enum
 */

typedef enum ZpassControl {
ZPASS_DISABLE                            = 0x00000000,
ZPASS_SAMPLES                            = 0x00000001,
ZPASS_PIXELS                             = 0x00000002,
} ZpassControl;

/*******************************************************
 * PA Enums
 *******************************************************/

/*
 * SU_PERFCNT_SEL enum
 */

typedef enum SU_PERFCNT_SEL {
PERF_PAPC_PASX_REQ                       = 0x00000000,
PERF_PAPC_PASX_DISABLE_PIPE              = 0x00000001,
PERF_PAPC_PASX_FIRST_VECTOR              = 0x00000002,
PERF_PAPC_PASX_SECOND_VECTOR             = 0x00000003,
PERF_PAPC_PASX_FIRST_DEAD                = 0x00000004,
PERF_PAPC_PASX_SECOND_DEAD               = 0x00000005,
PERF_PAPC_PASX_VTX_KILL_DISCARD          = 0x00000006,
PERF_PAPC_PASX_VTX_NAN_DISCARD           = 0x00000007,
PERF_PAPC_PA_INPUT_PRIM                  = 0x00000008,
PERF_PAPC_PA_INPUT_NULL_PRIM             = 0x00000009,
PERF_PAPC_PA_INPUT_EVENT_FLAG            = 0x0000000a,
PERF_PAPC_PA_INPUT_FIRST_PRIM_SLOT       = 0x0000000b,
PERF_PAPC_PA_INPUT_END_OF_PACKET         = 0x0000000c,
PERF_PAPC_PA_INPUT_EXTENDED_EVENT        = 0x0000000d,
PERF_PAPC_CLPR_CULL_PRIM                 = 0x0000000e,
PERF_PAPC_CLPR_VVUCP_CULL_PRIM           = 0x0000000f,
PERF_PAPC_CLPR_VV_CULL_PRIM              = 0x00000010,
PERF_PAPC_CLPR_UCP_CULL_PRIM             = 0x00000011,
PERF_PAPC_CLPR_VTX_KILL_CULL_PRIM        = 0x00000012,
PERF_PAPC_CLPR_VTX_NAN_CULL_PRIM         = 0x00000013,
PERF_PAPC_CLPR_CULL_TO_NULL_PRIM         = 0x00000014,
PERF_PAPC_CLPR_VVUCP_CLIP_PRIM           = 0x00000015,
PERF_PAPC_CLPR_VV_CLIP_PRIM              = 0x00000016,
PERF_PAPC_CLPR_UCP_CLIP_PRIM             = 0x00000017,
PERF_PAPC_CLPR_POINT_CLIP_CANDIDATE      = 0x00000018,
PERF_PAPC_CLPR_CLIP_PLANE_CNT_1          = 0x00000019,
PERF_PAPC_CLPR_CLIP_PLANE_CNT_2          = 0x0000001a,
PERF_PAPC_CLPR_CLIP_PLANE_CNT_3          = 0x0000001b,
PERF_PAPC_CLPR_CLIP_PLANE_CNT_4          = 0x0000001c,
PERF_PAPC_CLPR_CLIP_PLANE_CNT_5_8        = 0x0000001d,
PERF_PAPC_CLPR_CLIP_PLANE_CNT_9_12       = 0x0000001e,
PERF_PAPC_CLPR_CLIP_PLANE_NEAR           = 0x0000001f,
PERF_PAPC_CLPR_CLIP_PLANE_FAR            = 0x00000020,
PERF_PAPC_CLPR_CLIP_PLANE_LEFT           = 0x00000021,
PERF_PAPC_CLPR_CLIP_PLANE_RIGHT          = 0x00000022,
PERF_PAPC_CLPR_CLIP_PLANE_TOP            = 0x00000023,
PERF_PAPC_CLPR_CLIP_PLANE_BOTTOM         = 0x00000024,
PERF_PAPC_CLPR_GSC_KILL_CULL_PRIM        = 0x00000025,
PERF_PAPC_CLPR_RASTER_KILL_CULL_PRIM     = 0x00000026,
PERF_PAPC_CLSM_NULL_PRIM                 = 0x00000027,
PERF_PAPC_CLSM_TOTALLY_VISIBLE_PRIM      = 0x00000028,
PERF_PAPC_CLSM_CULL_TO_NULL_PRIM         = 0x00000029,
PERF_PAPC_CLSM_OUT_PRIM_CNT_1            = 0x0000002a,
PERF_PAPC_CLSM_OUT_PRIM_CNT_2            = 0x0000002b,
PERF_PAPC_CLSM_OUT_PRIM_CNT_3            = 0x0000002c,
PERF_PAPC_CLSM_OUT_PRIM_CNT_4            = 0x0000002d,
PERF_PAPC_CLSM_OUT_PRIM_CNT_5_8          = 0x0000002e,
PERF_PAPC_CLSM_OUT_PRIM_CNT_9_13         = 0x0000002f,
PERF_PAPC_CLIPGA_VTE_KILL_PRIM           = 0x00000030,
PERF_PAPC_SU_INPUT_PRIM                  = 0x00000031,
PERF_PAPC_SU_INPUT_CLIP_PRIM             = 0x00000032,
PERF_PAPC_SU_INPUT_NULL_PRIM             = 0x00000033,
PERF_PAPC_SU_INPUT_PRIM_DUAL             = 0x00000034,
PERF_PAPC_SU_INPUT_CLIP_PRIM_DUAL        = 0x00000035,
PERF_PAPC_SU_ZERO_AREA_CULL_PRIM         = 0x00000036,
PERF_PAPC_SU_BACK_FACE_CULL_PRIM         = 0x00000037,
PERF_PAPC_SU_FRONT_FACE_CULL_PRIM        = 0x00000038,
PERF_PAPC_SU_POLYMODE_FACE_CULL          = 0x00000039,
PERF_PAPC_SU_POLYMODE_BACK_CULL          = 0x0000003a,
PERF_PAPC_SU_POLYMODE_FRONT_CULL         = 0x0000003b,
PERF_PAPC_SU_POLYMODE_INVALID_FILL       = 0x0000003c,
PERF_PAPC_SU_OUTPUT_PRIM                 = 0x0000003d,
PERF_PAPC_SU_OUTPUT_CLIP_PRIM            = 0x0000003e,
PERF_PAPC_SU_OUTPUT_NULL_PRIM            = 0x0000003f,
PERF_PAPC_SU_OUTPUT_EVENT_FLAG           = 0x00000040,
PERF_PAPC_SU_OUTPUT_FIRST_PRIM_SLOT      = 0x00000041,
PERF_PAPC_SU_OUTPUT_END_OF_PACKET        = 0x00000042,
PERF_PAPC_SU_OUTPUT_POLYMODE_FACE        = 0x00000043,
PERF_PAPC_SU_OUTPUT_POLYMODE_BACK        = 0x00000044,
PERF_PAPC_SU_OUTPUT_POLYMODE_FRONT       = 0x00000045,
PERF_PAPC_SU_OUT_CLIP_POLYMODE_FACE      = 0x00000046,
PERF_PAPC_SU_OUT_CLIP_POLYMODE_BACK      = 0x00000047,
PERF_PAPC_SU_OUT_CLIP_POLYMODE_FRONT     = 0x00000048,
PERF_PAPC_SU_OUTPUT_PRIM_DUAL            = 0x00000049,
PERF_PAPC_SU_OUTPUT_CLIP_PRIM_DUAL       = 0x0000004a,
PERF_PAPC_SU_OUTPUT_POLYMODE_DUAL        = 0x0000004b,
PERF_PAPC_SU_OUTPUT_CLIP_POLYMODE_DUAL   = 0x0000004c,
PERF_PAPC_PASX_REQ_IDLE                  = 0x0000004d,
PERF_PAPC_PASX_REQ_BUSY                  = 0x0000004e,
PERF_PAPC_PASX_REQ_STALLED               = 0x0000004f,
PERF_PAPC_PASX_REC_IDLE                  = 0x00000050,
PERF_PAPC_PASX_REC_BUSY                  = 0x00000051,
PERF_PAPC_PASX_REC_STARVED_SX            = 0x00000052,
PERF_PAPC_PASX_REC_STALLED               = 0x00000053,
PERF_PAPC_PASX_REC_STALLED_POS_MEM       = 0x00000054,
PERF_PAPC_PASX_REC_STALLED_CCGSM_IN      = 0x00000055,
PERF_PAPC_CCGSM_IDLE                     = 0x00000056,
PERF_PAPC_CCGSM_BUSY                     = 0x00000057,
PERF_PAPC_CCGSM_STALLED                  = 0x00000058,
PERF_PAPC_CLPRIM_IDLE                    = 0x00000059,
PERF_PAPC_CLPRIM_BUSY                    = 0x0000005a,
PERF_PAPC_CLPRIM_STALLED                 = 0x0000005b,
PERF_PAPC_CLPRIM_STARVED_CCGSM           = 0x0000005c,
PERF_PAPC_CLIPSM_IDLE                    = 0x0000005d,
PERF_PAPC_CLIPSM_BUSY                    = 0x0000005e,
PERF_PAPC_CLIPSM_WAIT_CLIP_VERT_ENGH     = 0x0000005f,
PERF_PAPC_CLIPSM_WAIT_HIGH_PRI_SEQ       = 0x00000060,
PERF_PAPC_CLIPSM_WAIT_CLIPGA             = 0x00000061,
PERF_PAPC_CLIPSM_WAIT_AVAIL_VTE_CLIP     = 0x00000062,
PERF_PAPC_CLIPSM_WAIT_CLIP_OUTSM         = 0x00000063,
PERF_PAPC_CLIPGA_IDLE                    = 0x00000064,
PERF_PAPC_CLIPGA_BUSY                    = 0x00000065,
PERF_PAPC_CLIPGA_STARVED_VTE_CLIP        = 0x00000066,
PERF_PAPC_CLIPGA_STALLED                 = 0x00000067,
PERF_PAPC_CLIP_IDLE                      = 0x00000068,
PERF_PAPC_CLIP_BUSY                      = 0x00000069,
PERF_PAPC_SU_IDLE                        = 0x0000006a,
PERF_PAPC_SU_BUSY                        = 0x0000006b,
PERF_PAPC_SU_STARVED_CLIP                = 0x0000006c,
PERF_PAPC_SU_STALLED_SC                  = 0x0000006d,
PERF_PAPC_CL_DYN_SCLK_VLD                = 0x0000006e,
PERF_PAPC_SU_DYN_SCLK_VLD                = 0x0000006f,
PERF_PAPC_PA_REG_SCLK_VLD                = 0x00000070,
PERF_PAPC_SU_MULTI_GPU_PRIM_FILTER_CULL  = 0x00000071,
PERF_PAPC_PASX_SE0_REQ                   = 0x00000072,
PERF_PAPC_PASX_SE1_REQ                   = 0x00000073,
PERF_PAPC_PASX_SE0_FIRST_VECTOR          = 0x00000074,
PERF_PAPC_PASX_SE0_SECOND_VECTOR         = 0x00000075,
PERF_PAPC_PASX_SE1_FIRST_VECTOR          = 0x00000076,
PERF_PAPC_PASX_SE1_SECOND_VECTOR         = 0x00000077,
PERF_PAPC_SU_SE0_PRIM_FILTER_CULL        = 0x00000078,
PERF_PAPC_SU_SE1_PRIM_FILTER_CULL        = 0x00000079,
PERF_PAPC_SU_SE01_PRIM_FILTER_CULL       = 0x0000007a,
PERF_PAPC_SU_SE0_OUTPUT_PRIM             = 0x0000007b,
PERF_PAPC_SU_SE1_OUTPUT_PRIM             = 0x0000007c,
PERF_PAPC_SU_SE01_OUTPUT_PRIM            = 0x0000007d,
PERF_PAPC_SU_SE0_OUTPUT_NULL_PRIM        = 0x0000007e,
PERF_PAPC_SU_SE1_OUTPUT_NULL_PRIM        = 0x0000007f,
PERF_PAPC_SU_SE01_OUTPUT_NULL_PRIM       = 0x00000080,
PERF_PAPC_SU_SE0_OUTPUT_FIRST_PRIM_SLOT  = 0x00000081,
PERF_PAPC_SU_SE1_OUTPUT_FIRST_PRIM_SLOT  = 0x00000082,
PERF_PAPC_SU_SE0_STALLED_SC              = 0x00000083,
PERF_PAPC_SU_SE1_STALLED_SC              = 0x00000084,
PERF_PAPC_SU_SE01_STALLED_SC             = 0x00000085,
PERF_PAPC_CLSM_CLIPPING_PRIM             = 0x00000086,
PERF_PAPC_SU_CULLED_PRIM                 = 0x00000087,
PERF_PAPC_SU_OUTPUT_EOPG                 = 0x00000088,
PERF_PAPC_SU_SE2_PRIM_FILTER_CULL        = 0x00000089,
PERF_PAPC_SU_SE3_PRIM_FILTER_CULL        = 0x0000008a,
PERF_PAPC_SU_SE2_OUTPUT_PRIM             = 0x0000008b,
PERF_PAPC_SU_SE3_OUTPUT_PRIM             = 0x0000008c,
PERF_PAPC_SU_SE2_OUTPUT_NULL_PRIM        = 0x0000008d,
PERF_PAPC_SU_SE3_OUTPUT_NULL_PRIM        = 0x0000008e,
PERF_PAPC_SU_SE0_OUTPUT_END_OF_PACKET    = 0x0000008f,
PERF_PAPC_SU_SE1_OUTPUT_END_OF_PACKET    = 0x00000090,
PERF_PAPC_SU_SE2_OUTPUT_END_OF_PACKET    = 0x00000091,
PERF_PAPC_SU_SE3_OUTPUT_END_OF_PACKET    = 0x00000092,
PERF_PAPC_SU_SE0_OUTPUT_EOPG             = 0x00000093,
PERF_PAPC_SU_SE1_OUTPUT_EOPG             = 0x00000094,
PERF_PAPC_SU_SE2_OUTPUT_EOPG             = 0x00000095,
PERF_PAPC_SU_SE3_OUTPUT_EOPG             = 0x00000096,
PERF_PAPC_SU_SE2_STALLED_SC              = 0x00000097,
PERF_PAPC_SU_SE3_STALLED_SC              = 0x00000098,
PERF_SU_SMALL_PRIM_FILTER_CULL_CNT       = 0x00000099,
PERF_SMALL_PRIM_CULL_PRIM_1X1            = 0x0000009a,
PERF_SMALL_PRIM_CULL_PRIM_2X1            = 0x0000009b,
PERF_SMALL_PRIM_CULL_PRIM_1X2            = 0x0000009c,
PERF_SMALL_PRIM_CULL_PRIM_2X2            = 0x0000009d,
PERF_SMALL_PRIM_CULL_PRIM_3X1            = 0x0000009e,
PERF_SMALL_PRIM_CULL_PRIM_1X3            = 0x0000009f,
PERF_SMALL_PRIM_CULL_PRIM_3X2            = 0x000000a0,
PERF_SMALL_PRIM_CULL_PRIM_2X3            = 0x000000a1,
PERF_SMALL_PRIM_CULL_PRIM_NX1            = 0x000000a2,
PERF_SMALL_PRIM_CULL_PRIM_1XN            = 0x000000a3,
PERF_SMALL_PRIM_CULL_PRIM_NX2            = 0x000000a4,
PERF_SMALL_PRIM_CULL_PRIM_2XN            = 0x000000a5,
PERF_SMALL_PRIM_CULL_PRIM_FULL_RES_EVENT = 0x000000a6,
PERF_SMALL_PRIM_CULL_PRIM_HALF_RES_EVENT = 0x000000a7,
PERF_SMALL_PRIM_CULL_PRIM_QUARTER_RES_EVENT = 0x000000a8,
PERF_SC0_QUALIFIED_SEND_BUSY_EVENT       = 0x000000aa,
PERF_SC0_QUALIFIED_SEND_NOT_BUSY_EVENT   = 0x000000ab,
PERF_SC1_QUALIFIED_SEND_BUSY_EVENT       = 0x000000ac,
PERF_SC1_QUALIFIED_SEND_NOT_BUSY_EVENT   = 0x000000ad,
PERF_SC2_QUALIFIED_SEND_BUSY_EVENT       = 0x000000ae,
PERF_SC2_QUALIFIED_SEND_NOT_BUSY_EVENT   = 0x000000af,
PERF_SC3_QUALIFIED_SEND_BUSY_EVENT       = 0x000000b0,
PERF_SC3_QUALIFIED_SEND_NOT_BUSY_EVENT   = 0x000000b1,
PERF_PA_VERTEX_FIFO_FULL                 = 0x000000b3,
PERF_PA_PRIMIC_TO_CLPRIM_FIFO_FULL       = 0x000000b4,
PERF_PA_FETCH_TO_PRIMIC_P_FIFO_FULL      = 0x000000b6,
PERF_PA_FETCH_TO_SXIF_FIFO_FULL          = 0x000000b7,
PERF_PA_PIPE0_SWITCHED_GEN               = 0x000000b9,
PERF_PA_PIPE1_SWITCHED_GEN               = 0x000000ba,
PERF_ENGG_CSB_MACHINE_IS_STARVED         = 0x000000bc,
PERF_ENGG_CSB_MACHINE_STALLED_BY_CSB_MEMORY = 0x000000bd,
PERF_ENGG_CSB_MACHINE_STALLED_BY_SPI     = 0x000000be,
PERF_ENGG_CSB_GE_INPUT_FIFO_FULL         = 0x000000bf,
PERF_ENGG_CSB_SPI_INPUT_FIFO_FULL        = 0x000000c0,
PERF_ENGG_CSB_PAYLOAD_INPUT_FIFO_FULL    = 0x000000c1,
PERF_ENGG_CSB_GE_INPUT_FIFO_POP_BIT      = 0x000000c2,
PERF_ENGG_CSB_PRIM_COUNT_EQ0             = 0x000000c3,
PERF_ENGG_CSB_NULL_SUBGROUP              = 0x000000c4,
PERF_ENGG_CSB_GE_SENDING_SUBGROUP        = 0x000000c5,
PERF_ENGG_CSB_GE_MEMORY_FULL             = 0x000000c6,
PERF_ENGG_CSB_GE_MEMORY_EMPTY            = 0x000000c7,
PERF_ENGG_CSB_SPI_MEMORY_FULL            = 0x000000c8,
PERF_ENGG_CSB_SPI_MEMORY_EMPTY           = 0x000000c9,
PERF_ENGG_CSB_DELAY_BIN00                = 0x000000ca,
PERF_ENGG_CSB_DELAY_BIN01                = 0x000000cb,
PERF_ENGG_CSB_DELAY_BIN02                = 0x000000cc,
PERF_ENGG_CSB_DELAY_BIN03                = 0x000000cd,
PERF_ENGG_CSB_DELAY_BIN04                = 0x000000ce,
PERF_ENGG_CSB_DELAY_BIN05                = 0x000000cf,
PERF_ENGG_CSB_DELAY_BIN06                = 0x000000d0,
PERF_ENGG_CSB_DELAY_BIN07                = 0x000000d1,
PERF_ENGG_CSB_DELAY_BIN08                = 0x000000d2,
PERF_ENGG_CSB_DELAY_BIN09                = 0x000000d3,
PERF_ENGG_CSB_DELAY_BIN10                = 0x000000d4,
PERF_ENGG_CSB_DELAY_BIN11                = 0x000000d5,
PERF_ENGG_CSB_DELAY_BIN12                = 0x000000d6,
PERF_ENGG_CSB_DELAY_BIN13                = 0x000000d7,
PERF_ENGG_CSB_DELAY_BIN14                = 0x000000d8,
PERF_ENGG_CSB_DELAY_BIN15                = 0x000000d9,
PERF_ENGG_CSB_SPI_DELAY_BIN00            = 0x000000da,
PERF_ENGG_CSB_SPI_DELAY_BIN01            = 0x000000db,
PERF_ENGG_CSB_SPI_DELAY_BIN02            = 0x000000dc,
PERF_ENGG_CSB_SPI_DELAY_BIN03            = 0x000000dd,
PERF_ENGG_CSB_SPI_DELAY_BIN04            = 0x000000de,
PERF_ENGG_CSB_SPI_DELAY_BIN05            = 0x000000df,
PERF_ENGG_CSB_SPI_DELAY_BIN06            = 0x000000e0,
PERF_ENGG_CSB_SPI_DELAY_BIN07            = 0x000000e1,
PERF_ENGG_CSB_SPI_DELAY_BIN08            = 0x000000e2,
PERF_ENGG_CSB_SPI_DELAY_BIN09            = 0x000000e3,
PERF_ENGG_CSB_SPI_DELAY_BIN10            = 0x000000e4,
PERF_ENGG_INDEX_REQ_NULL_REQUEST         = 0x000000e5,
PERF_ENGG_INDEX_REQ_0_NEW_VERTS_THIS_PRIM = 0x000000e6,
PERF_ENGG_INDEX_REQ_1_NEW_VERTS_THIS_PRIM = 0x000000e7,
PERF_ENGG_INDEX_REQ_2_NEW_VERTS_THIS_PRIM = 0x000000e8,
PERF_ENGG_INDEX_REQ_3_NEW_VERTS_THIS_PRIM = 0x000000e9,
PERF_ENGG_INDEX_REQ_STARVED              = 0x000000ea,
PERF_ENGG_INDEX_REQ_IDLE_AND_STALLED_BY_REQ2RTN_FIFO_FULL = 0x000000eb,
PERF_ENGG_INDEX_REQ_BUSY_AND_STALLED_BY_REQ2RTN_FIFO_FULL = 0x000000ec,
PERF_ENGG_INDEX_REQ_STALLED_BY_SX_CREDITS = 0x000000ed,
PERF_ENGG_INDEX_RET_REQ2RTN_FIFO_FULL    = 0x000000ee,
PERF_ENGG_INDEX_RET_REQ2RTN_FIFO_EMPTY   = 0x000000ef,
PERF_ENGG_INDEX_RET_SX_RECEIVE_FIFO_FULL = 0x000000f0,
PERF_ENGG_INDEX_RET_SXRX_STARVED_BY_CSB  = 0x000000f1,
PERF_ENGG_INDEX_RET_SXRX_STARVED_BY_PRIMS = 0x000000f2,
PERF_ENGG_INDEX_RET_SXRX_STALLED_BY_PRIM_INDICES_CSB_FIFO = 0x000000f3,
PERF_ENGG_INDEX_RET_SXRX_STALLED_BY_PRIM_INDICES_FIFO = 0x000000f4,
PERF_ENGG_INDEX_RET_SXRX_READING_EVENT   = 0x000000f5,
PERF_ENGG_INDEX_RET_SXRX_READING_NULL_SUBGROUP = 0x000000f6,
PERF_ENGG_INDEX_RET_SXRX_READING_SUBGROUP_PRIMCOUNT_EQ0 = 0x000000f7,
PERF_ENGG_INDEX_RET_SXRX_READING_QDWORD_0_VALID_PRIMS_NOPL = 0x000000f8,
PERF_ENGG_INDEX_RET_SXRX_READING_QDWORD_1_VALID_PRIMS_NOPL = 0x000000f9,
PERF_ENGG_INDEX_RET_SXRX_READING_QDWORD_2_VALID_PRIMS_NOPL = 0x000000fa,
PERF_ENGG_INDEX_RET_SXRX_READING_QDWORD_3_VALID_PRIMS_NOPL = 0x000000fb,
PERF_ENGG_INDEX_RET_SXRX_READING_QDWORD_4_VALID_PRIMS_NOPL = 0x000000fc,
PERF_ENGG_INDEX_RET_SXRX_READING_QDWORD_0_VALID_PRIMS_PL = 0x000000fd,
PERF_ENGG_INDEX_RET_SXRX_READING_QDWORD_1_VALID_PRIMS_PL = 0x000000fe,
PERF_ENGG_INDEX_RET_SXRX_READING_QDWORD_2_VALID_PRIMS_PL = 0x000000ff,
PERF_ENGG_INDEX_RET_SXRX_READING_QDWORD_3_VALID_PRIMS_PL = 0x00000100,
PERF_ENGG_INDEX_RET_SXRX_READING_QDWORD_4_VALID_PRIMS_PL = 0x00000101,
PERF_ENGG_INDEX_RET_SXRX_READING_QDWORD_0_NULL_PRIMS = 0x00000102,
PERF_ENGG_INDEX_RET_SXRX_READING_QDWORD_1_NULL_PRIMS = 0x00000103,
PERF_ENGG_INDEX_RET_SXRX_READING_QDWORD_2_NULL_PRIMS = 0x00000104,
PERF_ENGG_INDEX_RET_SXRX_READING_QDWORD_3_NULL_PRIMS = 0x00000105,
PERF_ENGG_INDEX_RET_SXRX_READING_QDWORD_4_NULL_PRIMS = 0x00000106,
PERF_ENGG_INDEX_PRIM_IF_STALLED_BY_FULL_FETCH_TO_PRIMIC_P_FIFO = 0x00000107,
PERF_ENGG_INDEX_PRIM_IF_STALLED_BY_FULL_FETCH_TO_PRIMIC_S_FIFO = 0x00000108,
PERF_ENGG_INDEX_PRIM_IF_STARVED_BY_NO_CSB = 0x00000109,
PERF_ENGG_INDEX_PRIM_IF_STARVED_BY_NO_PRIM = 0x0000010a,
PERF_ENGG_INDEX_PRIM_IF_FETCH_TO_PRIMIC_P_FIFO_WRITE = 0x0000010b,
PERF_ENGG_INDEX_PRIM_IF_FETCH_TO_PRIMIC_P_FIFO_NO_WRITE = 0x0000010c,
PERF_ENGG_POS_REQ_STARVED                = 0x0000010d,
PERF_ENGG_INDEX_RET_SXRX_NULL_DROPPER_STALLED_BY_FULL_PRIM_FIFO = 0x0000010e,
PERF_ENGG_BUSY                           = 0x0000010f,
PERF_CLIPSM_CULL_PRIMS_CNT               = 0x00000110,
PERF_PH_SEND_1_SC                        = 0x00000111,
PERF_PH_SEND_2_SC                        = 0x00000112,
PERF_PH_SEND_3_SC                        = 0x00000113,
PERF_PH_SEND_4_SC                        = 0x00000114,
PERF_OUTPUT_PRIM_1_SC                    = 0x00000115,
PERF_OUTPUT_PRIM_2_SC                    = 0x00000116,
PERF_OUTPUT_PRIM_3_SC                    = 0x00000117,
PERF_OUTPUT_PRIM_4_SC                    = 0x00000118,
} SU_PERFCNT_SEL;

/*******************************************************
 * PH Enums
 *******************************************************/

/*
 * PH_PERFCNT_SEL enum
 */

typedef enum PH_PERFCNT_SEL {
PH_PERF_SEL_SC0_SRPS_WINDOW_VALID        = 0x00000000,
PH_PERF_SEL_SC0_ARB_XFC_ALL_EVENT_OR_PRIM_CYCLES = 0x00000001,
PH_PERF_SEL_SC0_ARB_XFC_ONLY_PRIM_CYCLES = 0x00000002,
PH_PERF_SEL_SC0_ARB_XFC_ONLY_ONE_INC_PER_PRIM = 0x00000003,
PH_PERF_SEL_SC0_ARB_STALLED_FROM_BELOW   = 0x00000004,
PH_PERF_SEL_SC0_ARB_STARVED_FROM_ABOVE   = 0x00000005,
PH_PERF_SEL_SC0_ARB_STARVED_FROM_ABOVE_WITH_UNSELECTED_FIFO_NOT_EMPTY = 0x00000006,
PH_PERF_SEL_SC0_ARB_STARVED_FROM_ABOVE_WITH_UNSELECTED_FIFO_FULL = 0x00000007,
PH_PERF_SEL_SC0_ARB_BUSY                 = 0x00000008,
PH_PERF_SEL_SC0_ARB_PA_BUSY_SOP          = 0x00000009,
PH_PERF_SEL_SC0_ARB_EOP_POP_SYNC_POP     = 0x0000000a,
PH_PERF_SEL_SC0_ARB_EVENT_SYNC_POP       = 0x0000000b,
PH_PERF_SEL_SC0_PS_ENG_MULTICYCLE_BUBBLE = 0x0000000c,
PH_PERF_SEL_SC0_EOP_SYNC_WINDOW          = 0x0000000d,
PH_PERF_SEL_SC0_BUSY_PROCESSING_MULTICYCLE_PRIM = 0x0000000e,
PH_PERF_SEL_SC0_BUSY_CNT_NOT_ZERO        = 0x0000000f,
PH_PERF_SEL_SC0_SEND                     = 0x00000010,
PH_PERF_SEL_SC0_CREDIT_AT_ZERO_WITH_PENDING_SEND = 0x00000011,
PH_PERF_SEL_SC0_CREDIT_AT_MAX            = 0x00000012,
PH_PERF_SEL_SC0_CREDIT_AT_MAX_NO_PENDING_SEND = 0x00000013,
PH_PERF_SEL_SC0_GFX_PIPE0_TO_1_TRANSITION = 0x00000014,
PH_PERF_SEL_SC0_GFX_PIPE1_TO_0_TRANSITION = 0x00000015,
PH_PERF_SEL_SC0_GFX_PIPE_PRIM_PROVOKED_TRANSITION = 0x00000016,
PH_PERF_SEL_SC0_GFX_PIPE_EVENT_PROVOKED_TRANSITION = 0x00000017,
PH_PERF_SEL_SC0_PA0_DATA_FIFO_RD         = 0x00000018,
PH_PERF_SEL_SC0_PA0_DATA_FIFO_WE         = 0x00000019,
PH_PERF_SEL_SC0_PA0_FIFO_EMPTY           = 0x0000001a,
PH_PERF_SEL_SC0_PA0_FIFO_FULL            = 0x0000001b,
PH_PERF_SEL_SC0_PA0_NULL_WE              = 0x0000001c,
PH_PERF_SEL_SC0_PA0_EVENT_WE             = 0x0000001d,
PH_PERF_SEL_SC0_PA0_FPOV_WE              = 0x0000001e,
PH_PERF_SEL_SC0_PA0_LPOV_WE              = 0x0000001f,
PH_PERF_SEL_SC0_PA0_EOP_WE               = 0x00000020,
PH_PERF_SEL_SC0_PA0_DATA_FIFO_EOP_RD     = 0x00000021,
PH_PERF_SEL_SC0_PA0_EOPG_WE              = 0x00000022,
PH_PERF_SEL_SC0_PA0_DEALLOC_4_0_RD       = 0x00000023,
PH_PERF_SEL_SC0_PA1_DATA_FIFO_RD         = 0x00000024,
PH_PERF_SEL_SC0_PA1_DATA_FIFO_WE         = 0x00000025,
PH_PERF_SEL_SC0_PA1_FIFO_EMPTY           = 0x00000026,
PH_PERF_SEL_SC0_PA1_FIFO_FULL            = 0x00000027,
PH_PERF_SEL_SC0_PA1_NULL_WE              = 0x00000028,
PH_PERF_SEL_SC0_PA1_EVENT_WE             = 0x00000029,
PH_PERF_SEL_SC0_PA1_FPOV_WE              = 0x0000002a,
PH_PERF_SEL_SC0_PA1_LPOV_WE              = 0x0000002b,
PH_PERF_SEL_SC0_PA1_EOP_WE               = 0x0000002c,
PH_PERF_SEL_SC0_PA1_DATA_FIFO_EOP_RD     = 0x0000002d,
PH_PERF_SEL_SC0_PA1_EOPG_WE              = 0x0000002e,
PH_PERF_SEL_SC0_PA1_DEALLOC_4_0_RD       = 0x0000002f,
PH_PERF_SEL_SC0_PA2_DATA_FIFO_RD         = 0x00000030,
PH_PERF_SEL_SC0_PA2_DATA_FIFO_WE         = 0x00000031,
PH_PERF_SEL_SC0_PA2_FIFO_EMPTY           = 0x00000032,
PH_PERF_SEL_SC0_PA2_FIFO_FULL            = 0x00000033,
PH_PERF_SEL_SC0_PA2_NULL_WE              = 0x00000034,
PH_PERF_SEL_SC0_PA2_EVENT_WE             = 0x00000035,
PH_PERF_SEL_SC0_PA2_FPOV_WE              = 0x00000036,
PH_PERF_SEL_SC0_PA2_LPOV_WE              = 0x00000037,
PH_PERF_SEL_SC0_PA2_EOP_WE               = 0x00000038,
PH_PERF_SEL_SC0_PA2_DATA_FIFO_EOP_RD     = 0x00000039,
PH_PERF_SEL_SC0_PA2_EOPG_WE              = 0x0000003a,
PH_PERF_SEL_SC0_PA2_DEALLOC_4_0_RD       = 0x0000003b,
PH_PERF_SEL_SC0_PA3_DATA_FIFO_RD         = 0x0000003c,
PH_PERF_SEL_SC0_PA3_DATA_FIFO_WE         = 0x0000003d,
PH_PERF_SEL_SC0_PA3_FIFO_EMPTY           = 0x0000003e,
PH_PERF_SEL_SC0_PA3_FIFO_FULL            = 0x0000003f,
PH_PERF_SEL_SC0_PA3_NULL_WE              = 0x00000040,
PH_PERF_SEL_SC0_PA3_EVENT_WE             = 0x00000041,
PH_PERF_SEL_SC0_PA3_FPOV_WE              = 0x00000042,
PH_PERF_SEL_SC0_PA3_LPOV_WE              = 0x00000043,
PH_PERF_SEL_SC0_PA3_EOP_WE               = 0x00000044,
PH_PERF_SEL_SC0_PA3_DATA_FIFO_EOP_RD     = 0x00000045,
PH_PERF_SEL_SC0_PA3_EOPG_WE              = 0x00000046,
PH_PERF_SEL_SC0_PA3_DEALLOC_4_0_RD       = 0x00000047,
PH_PERF_SEL_SC0_PA4_DATA_FIFO_RD         = 0x00000048,
PH_PERF_SEL_SC0_PA4_DATA_FIFO_WE         = 0x00000049,
PH_PERF_SEL_SC0_PA4_FIFO_EMPTY           = 0x0000004a,
PH_PERF_SEL_SC0_PA4_FIFO_FULL            = 0x0000004b,
PH_PERF_SEL_SC0_PA4_NULL_WE              = 0x0000004c,
PH_PERF_SEL_SC0_PA4_EVENT_WE             = 0x0000004d,
PH_PERF_SEL_SC0_PA4_FPOV_WE              = 0x0000004e,
PH_PERF_SEL_SC0_PA4_LPOV_WE              = 0x0000004f,
PH_PERF_SEL_SC0_PA4_EOP_WE               = 0x00000050,
PH_PERF_SEL_SC0_PA4_DATA_FIFO_EOP_RD     = 0x00000051,
PH_PERF_SEL_SC0_PA4_EOPG_WE              = 0x00000052,
PH_PERF_SEL_SC0_PA4_DEALLOC_4_0_RD       = 0x00000053,
PH_PERF_SEL_SC0_PA5_DATA_FIFO_RD         = 0x00000054,
PH_PERF_SEL_SC0_PA5_DATA_FIFO_WE         = 0x00000055,
PH_PERF_SEL_SC0_PA5_FIFO_EMPTY           = 0x00000056,
PH_PERF_SEL_SC0_PA5_FIFO_FULL            = 0x00000057,
PH_PERF_SEL_SC0_PA5_NULL_WE              = 0x00000058,
PH_PERF_SEL_SC0_PA5_EVENT_WE             = 0x00000059,
PH_PERF_SEL_SC0_PA5_FPOV_WE              = 0x0000005a,
PH_PERF_SEL_SC0_PA5_LPOV_WE              = 0x0000005b,
PH_PERF_SEL_SC0_PA5_EOP_WE               = 0x0000005c,
PH_PERF_SEL_SC0_PA5_DATA_FIFO_EOP_RD     = 0x0000005d,
PH_PERF_SEL_SC0_PA5_EOPG_WE              = 0x0000005e,
PH_PERF_SEL_SC0_PA5_DEALLOC_4_0_RD       = 0x0000005f,
PH_PERF_SEL_SC0_PA6_DATA_FIFO_RD         = 0x00000060,
PH_PERF_SEL_SC0_PA6_DATA_FIFO_WE         = 0x00000061,
PH_PERF_SEL_SC0_PA6_FIFO_EMPTY           = 0x00000062,
PH_PERF_SEL_SC0_PA6_FIFO_FULL            = 0x00000063,
PH_PERF_SEL_SC0_PA6_NULL_WE              = 0x00000064,
PH_PERF_SEL_SC0_PA6_EVENT_WE             = 0x00000065,
PH_PERF_SEL_SC0_PA6_FPOV_WE              = 0x00000066,
PH_PERF_SEL_SC0_PA6_LPOV_WE              = 0x00000067,
PH_PERF_SEL_SC0_PA6_EOP_WE               = 0x00000068,
PH_PERF_SEL_SC0_PA6_DATA_FIFO_EOP_RD     = 0x00000069,
PH_PERF_SEL_SC0_PA6_EOPG_WE              = 0x0000006a,
PH_PERF_SEL_SC0_PA6_DEALLOC_4_0_RD       = 0x0000006b,
PH_PERF_SEL_SC0_PA7_DATA_FIFO_RD         = 0x0000006c,
PH_PERF_SEL_SC0_PA7_DATA_FIFO_WE         = 0x0000006d,
PH_PERF_SEL_SC0_PA7_FIFO_EMPTY           = 0x0000006e,
PH_PERF_SEL_SC0_PA7_FIFO_FULL            = 0x0000006f,
PH_PERF_SEL_SC0_PA7_NULL_WE              = 0x00000070,
PH_PERF_SEL_SC0_PA7_EVENT_WE             = 0x00000071,
PH_PERF_SEL_SC0_PA7_FPOV_WE              = 0x00000072,
PH_PERF_SEL_SC0_PA7_LPOV_WE              = 0x00000073,
PH_PERF_SEL_SC0_PA7_EOP_WE               = 0x00000074,
PH_PERF_SEL_SC0_PA7_DATA_FIFO_EOP_RD     = 0x00000075,
PH_PERF_SEL_SC0_PA7_EOPG_WE              = 0x00000076,
PH_PERF_SEL_SC0_PA7_DEALLOC_4_0_RD       = 0x00000077,
PH_PERF_SEL_SC1_SRPS_WINDOW_VALID        = 0x00000078,
PH_PERF_SEL_SC1_ARB_XFC_ALL_EVENT_OR_PRIM_CYCLES = 0x00000079,
PH_PERF_SEL_SC1_ARB_XFC_ONLY_PRIM_CYCLES = 0x0000007a,
PH_PERF_SEL_SC1_ARB_XFC_ONLY_ONE_INC_PER_PRIM = 0x0000007b,
PH_PERF_SEL_SC1_ARB_STALLED_FROM_BELOW   = 0x0000007c,
PH_PERF_SEL_SC1_ARB_STARVED_FROM_ABOVE   = 0x0000007d,
PH_PERF_SEL_SC1_ARB_STARVED_FROM_ABOVE_WITH_UNSELECTED_FIFO_NOT_EMPTY = 0x0000007e,
PH_PERF_SEL_SC1_ARB_STARVED_FROM_ABOVE_WITH_UNSELECTED_FIFO_FULL = 0x0000007f,
PH_PERF_SEL_SC1_ARB_BUSY                 = 0x00000080,
PH_PERF_SEL_SC1_ARB_PA_BUSY_SOP          = 0x00000081,
PH_PERF_SEL_SC1_ARB_EOP_POP_SYNC_POP     = 0x00000082,
PH_PERF_SEL_SC1_ARB_EVENT_SYNC_POP       = 0x00000083,
PH_PERF_SEL_SC1_PS_ENG_MULTICYCLE_BUBBLE = 0x00000084,
PH_PERF_SEL_SC1_EOP_SYNC_WINDOW          = 0x00000085,
PH_PERF_SEL_SC1_BUSY_PROCESSING_MULTICYCLE_PRIM = 0x00000086,
PH_PERF_SEL_SC1_BUSY_CNT_NOT_ZERO        = 0x00000087,
PH_PERF_SEL_SC1_SEND                     = 0x00000088,
PH_PERF_SEL_SC1_CREDIT_AT_ZERO_WITH_PENDING_SEND = 0x00000089,
PH_PERF_SEL_SC1_CREDIT_AT_MAX            = 0x0000008a,
PH_PERF_SEL_SC1_CREDIT_AT_MAX_NO_PENDING_SEND = 0x0000008b,
PH_PERF_SEL_SC1_GFX_PIPE0_TO_1_TRANSITION = 0x0000008c,
PH_PERF_SEL_SC1_GFX_PIPE1_TO_0_TRANSITION = 0x0000008d,
PH_PERF_SEL_SC1_GFX_PIPE_EOP_PRIM_PROVOKED_TRANSITION = 0x0000008e,
PH_PERF_SEL_SC1_GFX_PIPE_EVENT_PROVOKED_TRANSITION = 0x0000008f,
PH_PERF_SEL_SC1_PA0_DATA_FIFO_RD         = 0x00000090,
PH_PERF_SEL_SC1_PA0_DATA_FIFO_WE         = 0x00000091,
PH_PERF_SEL_SC1_PA0_FIFO_EMPTY           = 0x00000092,
PH_PERF_SEL_SC1_PA0_FIFO_FULL            = 0x00000093,
PH_PERF_SEL_SC1_PA0_NULL_WE              = 0x00000094,
PH_PERF_SEL_SC1_PA0_EVENT_WE             = 0x00000095,
PH_PERF_SEL_SC1_PA0_FPOV_WE              = 0x00000096,
PH_PERF_SEL_SC1_PA0_LPOV_WE              = 0x00000097,
PH_PERF_SEL_SC1_PA0_EOP_WE               = 0x00000098,
PH_PERF_SEL_SC1_PA0_DATA_FIFO_EOP_RD     = 0x00000099,
PH_PERF_SEL_SC1_PA0_EOPG_WE              = 0x0000009a,
PH_PERF_SEL_SC1_PA0_DEALLOC_4_0_RD       = 0x0000009b,
PH_PERF_SEL_SC1_PA1_DATA_FIFO_RD         = 0x0000009c,
PH_PERF_SEL_SC1_PA1_DATA_FIFO_WE         = 0x0000009d,
PH_PERF_SEL_SC1_PA1_FIFO_EMPTY           = 0x0000009e,
PH_PERF_SEL_SC1_PA1_FIFO_FULL            = 0x0000009f,
PH_PERF_SEL_SC1_PA1_NULL_WE              = 0x000000a0,
PH_PERF_SEL_SC1_PA1_EVENT_WE             = 0x000000a1,
PH_PERF_SEL_SC1_PA1_FPOV_WE              = 0x000000a2,
PH_PERF_SEL_SC1_PA1_LPOV_WE              = 0x000000a3,
PH_PERF_SEL_SC1_PA1_EOP_WE               = 0x000000a4,
PH_PERF_SEL_SC1_PA1_DATA_FIFO_EOP_RD     = 0x000000a5,
PH_PERF_SEL_SC1_PA1_EOPG_WE              = 0x000000a6,
PH_PERF_SEL_SC1_PA1_DEALLOC_4_0_RD       = 0x000000a7,
PH_PERF_SEL_SC1_PA2_DATA_FIFO_RD         = 0x000000a8,
PH_PERF_SEL_SC1_PA2_DATA_FIFO_WE         = 0x000000a9,
PH_PERF_SEL_SC1_PA2_FIFO_EMPTY           = 0x000000aa,
PH_PERF_SEL_SC1_PA2_FIFO_FULL            = 0x000000ab,
PH_PERF_SEL_SC1_PA2_NULL_WE              = 0x000000ac,
PH_PERF_SEL_SC1_PA2_EVENT_WE             = 0x000000ad,
PH_PERF_SEL_SC1_PA2_FPOV_WE              = 0x000000ae,
PH_PERF_SEL_SC1_PA2_LPOV_WE              = 0x000000af,
PH_PERF_SEL_SC1_PA2_EOP_WE               = 0x000000b0,
PH_PERF_SEL_SC1_PA2_DATA_FIFO_EOP_RD     = 0x000000b1,
PH_PERF_SEL_SC1_PA2_EOPG_WE              = 0x000000b2,
PH_PERF_SEL_SC1_PA2_DEALLOC_4_0_RD       = 0x000000b3,
PH_PERF_SEL_SC1_PA3_DATA_FIFO_RD         = 0x000000b4,
PH_PERF_SEL_SC1_PA3_DATA_FIFO_WE         = 0x000000b5,
PH_PERF_SEL_SC1_PA3_FIFO_EMPTY           = 0x000000b6,
PH_PERF_SEL_SC1_PA3_FIFO_FULL            = 0x000000b7,
PH_PERF_SEL_SC1_PA3_NULL_WE              = 0x000000b8,
PH_PERF_SEL_SC1_PA3_EVENT_WE             = 0x000000b9,
PH_PERF_SEL_SC1_PA3_FPOV_WE              = 0x000000ba,
PH_PERF_SEL_SC1_PA3_LPOV_WE              = 0x000000bb,
PH_PERF_SEL_SC1_PA3_EOP_WE               = 0x000000bc,
PH_PERF_SEL_SC1_PA3_DATA_FIFO_EOP_RD     = 0x000000bd,
PH_PERF_SEL_SC1_PA3_EOPG_WE              = 0x000000be,
PH_PERF_SEL_SC1_PA3_DEALLOC_4_0_RD       = 0x000000bf,
PH_PERF_SEL_SC1_PA4_DATA_FIFO_RD         = 0x000000c0,
PH_PERF_SEL_SC1_PA4_DATA_FIFO_WE         = 0x000000c1,
PH_PERF_SEL_SC1_PA4_FIFO_EMPTY           = 0x000000c2,
PH_PERF_SEL_SC1_PA4_FIFO_FULL            = 0x000000c3,
PH_PERF_SEL_SC1_PA4_NULL_WE              = 0x000000c4,
PH_PERF_SEL_SC1_PA4_EVENT_WE             = 0x000000c5,
PH_PERF_SEL_SC1_PA4_FPOV_WE              = 0x000000c6,
PH_PERF_SEL_SC1_PA4_LPOV_WE              = 0x000000c7,
PH_PERF_SEL_SC1_PA4_EOP_WE               = 0x000000c8,
PH_PERF_SEL_SC1_PA4_DATA_FIFO_EOP_RD     = 0x000000c9,
PH_PERF_SEL_SC1_PA4_EOPG_WE              = 0x000000ca,
PH_PERF_SEL_SC1_PA4_DEALLOC_4_0_RD       = 0x000000cb,
PH_PERF_SEL_SC1_PA5_DATA_FIFO_RD         = 0x000000cc,
PH_PERF_SEL_SC1_PA5_DATA_FIFO_WE         = 0x000000cd,
PH_PERF_SEL_SC1_PA5_FIFO_EMPTY           = 0x000000ce,
PH_PERF_SEL_SC1_PA5_FIFO_FULL            = 0x000000cf,
PH_PERF_SEL_SC1_PA5_NULL_WE              = 0x000000d0,
PH_PERF_SEL_SC1_PA5_EVENT_WE             = 0x000000d1,
PH_PERF_SEL_SC1_PA5_FPOV_WE              = 0x000000d2,
PH_PERF_SEL_SC1_PA5_LPOV_WE              = 0x000000d3,
PH_PERF_SEL_SC1_PA5_EOP_WE               = 0x000000d4,
PH_PERF_SEL_SC1_PA5_DATA_FIFO_EOP_RD     = 0x000000d5,
PH_PERF_SEL_SC1_PA5_EOPG_WE              = 0x000000d6,
PH_PERF_SEL_SC1_PA5_DEALLOC_4_0_RD       = 0x000000d7,
PH_PERF_SEL_SC1_PA6_DATA_FIFO_RD         = 0x000000d8,
PH_PERF_SEL_SC1_PA6_DATA_FIFO_WE         = 0x000000d9,
PH_PERF_SEL_SC1_PA6_FIFO_EMPTY           = 0x000000da,
PH_PERF_SEL_SC1_PA6_FIFO_FULL            = 0x000000db,
PH_PERF_SEL_SC1_PA6_NULL_WE              = 0x000000dc,
PH_PERF_SEL_SC1_PA6_EVENT_WE             = 0x000000dd,
PH_PERF_SEL_SC1_PA6_FPOV_WE              = 0x000000de,
PH_PERF_SEL_SC1_PA6_LPOV_WE              = 0x000000df,
PH_PERF_SEL_SC1_PA6_EOP_WE               = 0x000000e0,
PH_PERF_SEL_SC1_PA6_DATA_FIFO_EOP_RD     = 0x000000e1,
PH_PERF_SEL_SC1_PA6_EOPG_WE              = 0x000000e2,
PH_PERF_SEL_SC1_PA6_DEALLOC_4_0_RD       = 0x000000e3,
PH_PERF_SEL_SC1_PA7_DATA_FIFO_RD         = 0x000000e4,
PH_PERF_SEL_SC1_PA7_DATA_FIFO_WE         = 0x000000e5,
PH_PERF_SEL_SC1_PA7_FIFO_EMPTY           = 0x000000e6,
PH_PERF_SEL_SC1_PA7_FIFO_FULL            = 0x000000e7,
PH_PERF_SEL_SC1_PA7_NULL_WE              = 0x000000e8,
PH_PERF_SEL_SC1_PA7_EVENT_WE             = 0x000000e9,
PH_PERF_SEL_SC1_PA7_FPOV_WE              = 0x000000ea,
PH_PERF_SEL_SC1_PA7_LPOV_WE              = 0x000000eb,
PH_PERF_SEL_SC1_PA7_EOP_WE               = 0x000000ec,
PH_PERF_SEL_SC1_PA7_DATA_FIFO_EOP_RD     = 0x000000ed,
PH_PERF_SEL_SC1_PA7_EOPG_WE              = 0x000000ee,
PH_PERF_SEL_SC1_PA7_DEALLOC_4_0_RD       = 0x000000ef,
PH_PERF_SEL_SC2_SRPS_WINDOW_VALID        = 0x000000f0,
PH_PERF_SEL_SC2_ARB_XFC_ALL_EVENT_OR_PRIM_CYCLES = 0x000000f1,
PH_PERF_SEL_SC2_ARB_XFC_ONLY_PRIM_CYCLES = 0x000000f2,
PH_PERF_SEL_SC2_ARB_XFC_ONLY_ONE_INC_PER_PRIM = 0x000000f3,
PH_PERF_SEL_SC2_ARB_STALLED_FROM_BELOW   = 0x000000f4,
PH_PERF_SEL_SC2_ARB_STARVED_FROM_ABOVE   = 0x000000f5,
PH_PERF_SEL_SC2_ARB_STARVED_FROM_ABOVE_WITH_UNSELECTED_FIFO_NOT_EMPTY = 0x000000f6,
PH_PERF_SEL_SC2_ARB_STARVED_FROM_ABOVE_WITH_UNSELECTED_FIFO_FULL = 0x000000f7,
PH_PERF_SEL_SC2_ARB_BUSY                 = 0x000000f8,
PH_PERF_SEL_SC2_ARB_PA_BUSY_SOP          = 0x000000f9,
PH_PERF_SEL_SC2_ARB_EOP_POP_SYNC_POP     = 0x000000fa,
PH_PERF_SEL_SC2_ARB_EVENT_SYNC_POP       = 0x000000fb,
PH_PERF_SEL_SC2_PS_ENG_MULTICYCLE_BUBBLE = 0x000000fc,
PH_PERF_SEL_SC2_EOP_SYNC_WINDOW          = 0x000000fd,
PH_PERF_SEL_SC2_BUSY_PROCESSING_MULTICYCLE_PRIM = 0x000000fe,
PH_PERF_SEL_SC2_BUSY_CNT_NOT_ZERO        = 0x000000ff,
PH_PERF_SEL_SC2_SEND                     = 0x00000100,
PH_PERF_SEL_SC2_CREDIT_AT_ZERO_WITH_PENDING_SEND = 0x00000101,
PH_PERF_SEL_SC2_CREDIT_AT_MAX            = 0x00000102,
PH_PERF_SEL_SC2_CREDIT_AT_MAX_NO_PENDING_SEND = 0x00000103,
PH_PERF_SEL_SC2_GFX_PIPE0_TO_1_TRANSITION = 0x00000104,
PH_PERF_SEL_SC2_GFX_PIPE1_TO_0_TRANSITION = 0x00000105,
PH_PERF_SEL_SC2_GFX_PIPE_EOP_PRIM_PROVOKED_TRANSITION = 0x00000106,
PH_PERF_SEL_SC2_GFX_PIPE_EVENT_PROVOKED_TRANSITION = 0x00000107,
PH_PERF_SEL_SC2_PA0_DATA_FIFO_RD         = 0x00000108,
PH_PERF_SEL_SC2_PA0_DATA_FIFO_WE         = 0x00000109,
PH_PERF_SEL_SC2_PA0_FIFO_EMPTY           = 0x0000010a,
PH_PERF_SEL_SC2_PA0_FIFO_FULL            = 0x0000010b,
PH_PERF_SEL_SC2_PA0_NULL_WE              = 0x0000010c,
PH_PERF_SEL_SC2_PA0_EVENT_WE             = 0x0000010d,
PH_PERF_SEL_SC2_PA0_FPOV_WE              = 0x0000010e,
PH_PERF_SEL_SC2_PA0_LPOV_WE              = 0x0000010f,
PH_PERF_SEL_SC2_PA0_EOP_WE               = 0x00000110,
PH_PERF_SEL_SC2_PA0_DATA_FIFO_EOP_RD     = 0x00000111,
PH_PERF_SEL_SC2_PA0_EOPG_WE              = 0x00000112,
PH_PERF_SEL_SC2_PA0_DEALLOC_4_0_RD       = 0x00000113,
PH_PERF_SEL_SC2_PA1_DATA_FIFO_RD         = 0x00000114,
PH_PERF_SEL_SC2_PA1_DATA_FIFO_WE         = 0x00000115,
PH_PERF_SEL_SC2_PA1_FIFO_EMPTY           = 0x00000116,
PH_PERF_SEL_SC2_PA1_FIFO_FULL            = 0x00000117,
PH_PERF_SEL_SC2_PA1_NULL_WE              = 0x00000118,
PH_PERF_SEL_SC2_PA1_EVENT_WE             = 0x00000119,
PH_PERF_SEL_SC2_PA1_FPOV_WE              = 0x0000011a,
PH_PERF_SEL_SC2_PA1_LPOV_WE              = 0x0000011b,
PH_PERF_SEL_SC2_PA1_EOP_WE               = 0x0000011c,
PH_PERF_SEL_SC2_PA1_DATA_FIFO_EOP_RD     = 0x0000011d,
PH_PERF_SEL_SC2_PA1_EOPG_WE              = 0x0000011e,
PH_PERF_SEL_SC2_PA1_DEALLOC_4_0_RD       = 0x0000011f,
PH_PERF_SEL_SC2_PA2_DATA_FIFO_RD         = 0x00000120,
PH_PERF_SEL_SC2_PA2_DATA_FIFO_WE         = 0x00000121,
PH_PERF_SEL_SC2_PA2_FIFO_EMPTY           = 0x00000122,
PH_PERF_SEL_SC2_PA2_FIFO_FULL            = 0x00000123,
PH_PERF_SEL_SC2_PA2_NULL_WE              = 0x00000124,
PH_PERF_SEL_SC2_PA2_EVENT_WE             = 0x00000125,
PH_PERF_SEL_SC2_PA2_FPOV_WE              = 0x00000126,
PH_PERF_SEL_SC2_PA2_LPOV_WE              = 0x00000127,
PH_PERF_SEL_SC2_PA2_EOP_WE               = 0x00000128,
PH_PERF_SEL_SC2_PA2_DATA_FIFO_EOP_RD     = 0x00000129,
PH_PERF_SEL_SC2_PA2_EOPG_WE              = 0x0000012a,
PH_PERF_SEL_SC2_PA2_DEALLOC_4_0_RD       = 0x0000012b,
PH_PERF_SEL_SC2_PA3_DATA_FIFO_RD         = 0x0000012c,
PH_PERF_SEL_SC2_PA3_DATA_FIFO_WE         = 0x0000012d,
PH_PERF_SEL_SC2_PA3_FIFO_EMPTY           = 0x0000012e,
PH_PERF_SEL_SC2_PA3_FIFO_FULL            = 0x0000012f,
PH_PERF_SEL_SC2_PA3_NULL_WE              = 0x00000130,
PH_PERF_SEL_SC2_PA3_EVENT_WE             = 0x00000131,
PH_PERF_SEL_SC2_PA3_FPOV_WE              = 0x00000132,
PH_PERF_SEL_SC2_PA3_LPOV_WE              = 0x00000133,
PH_PERF_SEL_SC2_PA3_EOP_WE               = 0x00000134,
PH_PERF_SEL_SC2_PA3_DATA_FIFO_EOP_RD     = 0x00000135,
PH_PERF_SEL_SC2_PA3_EOPG_WE              = 0x00000136,
PH_PERF_SEL_SC2_PA3_DEALLOC_4_0_RD       = 0x00000137,
PH_PERF_SEL_SC2_PA4_DATA_FIFO_RD         = 0x00000138,
PH_PERF_SEL_SC2_PA4_DATA_FIFO_WE         = 0x00000139,
PH_PERF_SEL_SC2_PA4_FIFO_EMPTY           = 0x0000013a,
PH_PERF_SEL_SC2_PA4_FIFO_FULL            = 0x0000013b,
PH_PERF_SEL_SC2_PA4_NULL_WE              = 0x0000013c,
PH_PERF_SEL_SC2_PA4_EVENT_WE             = 0x0000013d,
PH_PERF_SEL_SC2_PA4_FPOV_WE              = 0x0000013e,
PH_PERF_SEL_SC2_PA4_LPOV_WE              = 0x0000013f,
PH_PERF_SEL_SC2_PA4_EOP_WE               = 0x00000140,
PH_PERF_SEL_SC2_PA4_DATA_FIFO_EOP_RD     = 0x00000141,
PH_PERF_SEL_SC2_PA4_EOPG_WE              = 0x00000142,
PH_PERF_SEL_SC2_PA4_DEALLOC_4_0_RD       = 0x00000143,
PH_PERF_SEL_SC2_PA5_DATA_FIFO_RD         = 0x00000144,
PH_PERF_SEL_SC2_PA5_DATA_FIFO_WE         = 0x00000145,
PH_PERF_SEL_SC2_PA5_FIFO_EMPTY           = 0x00000146,
PH_PERF_SEL_SC2_PA5_FIFO_FULL            = 0x00000147,
PH_PERF_SEL_SC2_PA5_NULL_WE              = 0x00000148,
PH_PERF_SEL_SC2_PA5_EVENT_WE             = 0x00000149,
PH_PERF_SEL_SC2_PA5_FPOV_WE              = 0x0000014a,
PH_PERF_SEL_SC2_PA5_LPOV_WE              = 0x0000014b,
PH_PERF_SEL_SC2_PA5_EOP_WE               = 0x0000014c,
PH_PERF_SEL_SC2_PA5_DATA_FIFO_EOP_RD     = 0x0000014d,
PH_PERF_SEL_SC2_PA5_EOPG_WE              = 0x0000014e,
PH_PERF_SEL_SC2_PA5_DEALLOC_4_0_RD       = 0x0000014f,
PH_PERF_SEL_SC2_PA6_DATA_FIFO_RD         = 0x00000150,
PH_PERF_SEL_SC2_PA6_DATA_FIFO_WE         = 0x00000151,
PH_PERF_SEL_SC2_PA6_FIFO_EMPTY           = 0x00000152,
PH_PERF_SEL_SC2_PA6_FIFO_FULL            = 0x00000153,
PH_PERF_SEL_SC2_PA6_NULL_WE              = 0x00000154,
PH_PERF_SEL_SC2_PA6_EVENT_WE             = 0x00000155,
PH_PERF_SEL_SC2_PA6_FPOV_WE              = 0x00000156,
PH_PERF_SEL_SC2_PA6_LPOV_WE              = 0x00000157,
PH_PERF_SEL_SC2_PA6_EOP_WE               = 0x00000158,
PH_PERF_SEL_SC2_PA6_DATA_FIFO_EOP_RD     = 0x00000159,
PH_PERF_SEL_SC2_PA6_EOPG_WE              = 0x0000015a,
PH_PERF_SEL_SC2_PA6_DEALLOC_4_0_RD       = 0x0000015b,
PH_PERF_SEL_SC2_PA7_DATA_FIFO_RD         = 0x0000015c,
PH_PERF_SEL_SC2_PA7_DATA_FIFO_WE         = 0x0000015d,
PH_PERF_SEL_SC2_PA7_FIFO_EMPTY           = 0x0000015e,
PH_PERF_SEL_SC2_PA7_FIFO_FULL            = 0x0000015f,
PH_PERF_SEL_SC2_PA7_NULL_WE              = 0x00000160,
PH_PERF_SEL_SC2_PA7_EVENT_WE             = 0x00000161,
PH_PERF_SEL_SC2_PA7_FPOV_WE              = 0x00000162,
PH_PERF_SEL_SC2_PA7_LPOV_WE              = 0x00000163,
PH_PERF_SEL_SC2_PA7_EOP_WE               = 0x00000164,
PH_PERF_SEL_SC2_PA7_DATA_FIFO_EOP_RD     = 0x00000165,
PH_PERF_SEL_SC2_PA7_EOPG_WE              = 0x00000166,
PH_PERF_SEL_SC2_PA7_DEALLOC_4_0_RD       = 0x00000167,
PH_PERF_SEL_SC3_SRPS_WINDOW_VALID        = 0x00000168,
PH_PERF_SEL_SC3_ARB_XFC_ALL_EVENT_OR_PRIM_CYCLES = 0x00000169,
PH_PERF_SEL_SC3_ARB_XFC_ONLY_PRIM_CYCLES = 0x0000016a,
PH_PERF_SEL_SC3_ARB_XFC_ONLY_ONE_INC_PER_PRIM = 0x0000016b,
PH_PERF_SEL_SC3_ARB_STALLED_FROM_BELOW   = 0x0000016c,
PH_PERF_SEL_SC3_ARB_STARVED_FROM_ABOVE   = 0x0000016d,
PH_PERF_SEL_SC3_ARB_STARVED_FROM_ABOVE_WITH_UNSELECTED_FIFO_NOT_EMPTY = 0x0000016e,
PH_PERF_SEL_SC3_ARB_STARVED_FROM_ABOVE_WITH_UNSELECTED_FIFO_FULL = 0x0000016f,
PH_PERF_SEL_SC3_ARB_BUSY                 = 0x00000170,
PH_PERF_SEL_SC3_ARB_PA_BUSY_SOP          = 0x00000171,
PH_PERF_SEL_SC3_ARB_EOP_POP_SYNC_POP     = 0x00000172,
PH_PERF_SEL_SC3_ARB_EVENT_SYNC_POP       = 0x00000173,
PH_PERF_SEL_SC3_PS_ENG_MULTICYCLE_BUBBLE = 0x00000174,
PH_PERF_SEL_SC3_EOP_SYNC_WINDOW          = 0x00000175,
PH_PERF_SEL_SC3_BUSY_PROCESSING_MULTICYCLE_PRIM = 0x00000176,
PH_PERF_SEL_SC3_BUSY_CNT_NOT_ZERO        = 0x00000177,
PH_PERF_SEL_SC3_SEND                     = 0x00000178,
PH_PERF_SEL_SC3_CREDIT_AT_ZERO_WITH_PENDING_SEND = 0x00000179,
PH_PERF_SEL_SC3_CREDIT_AT_MAX            = 0x0000017a,
PH_PERF_SEL_SC3_CREDIT_AT_MAX_NO_PENDING_SEND = 0x0000017b,
PH_PERF_SEL_SC3_GFX_PIPE0_TO_1_TRANSITION = 0x0000017c,
PH_PERF_SEL_SC3_GFX_PIPE1_TO_0_TRANSITION = 0x0000017d,
PH_PERF_SEL_SC3_GFX_PIPE_EOP_PRIM_PROVOKED_TRANSITION = 0x0000017e,
PH_PERF_SEL_SC3_GFX_PIPE_EVENT_PROVOKED_TRANSITION = 0x0000017f,
PH_PERF_SEL_SC3_PA0_DATA_FIFO_RD         = 0x00000180,
PH_PERF_SEL_SC3_PA0_DATA_FIFO_WE         = 0x00000181,
PH_PERF_SEL_SC3_PA0_FIFO_EMPTY           = 0x00000182,
PH_PERF_SEL_SC3_PA0_FIFO_FULL            = 0x00000183,
PH_PERF_SEL_SC3_PA0_NULL_WE              = 0x00000184,
PH_PERF_SEL_SC3_PA0_EVENT_WE             = 0x00000185,
PH_PERF_SEL_SC3_PA0_FPOV_WE              = 0x00000186,
PH_PERF_SEL_SC3_PA0_LPOV_WE              = 0x00000187,
PH_PERF_SEL_SC3_PA0_EOP_WE               = 0x00000188,
PH_PERF_SEL_SC3_PA0_DATA_FIFO_EOP_RD     = 0x00000189,
PH_PERF_SEL_SC3_PA0_EOPG_WE              = 0x0000018a,
PH_PERF_SEL_SC3_PA0_DEALLOC_4_0_RD       = 0x0000018b,
PH_PERF_SEL_SC3_PA1_DATA_FIFO_RD         = 0x0000018c,
PH_PERF_SEL_SC3_PA1_DATA_FIFO_WE         = 0x0000018d,
PH_PERF_SEL_SC3_PA1_FIFO_EMPTY           = 0x0000018e,
PH_PERF_SEL_SC3_PA1_FIFO_FULL            = 0x0000018f,
PH_PERF_SEL_SC3_PA1_NULL_WE              = 0x00000190,
PH_PERF_SEL_SC3_PA1_EVENT_WE             = 0x00000191,
PH_PERF_SEL_SC3_PA1_FPOV_WE              = 0x00000192,
PH_PERF_SEL_SC3_PA1_LPOV_WE              = 0x00000193,
PH_PERF_SEL_SC3_PA1_EOP_WE               = 0x00000194,
PH_PERF_SEL_SC3_PA1_DATA_FIFO_EOP_RD     = 0x00000195,
PH_PERF_SEL_SC3_PA1_EOPG_WE              = 0x00000196,
PH_PERF_SEL_SC3_PA1_DEALLOC_4_0_RD       = 0x00000197,
PH_PERF_SEL_SC3_PA2_DATA_FIFO_RD         = 0x00000198,
PH_PERF_SEL_SC3_PA2_DATA_FIFO_WE         = 0x00000199,
PH_PERF_SEL_SC3_PA2_FIFO_EMPTY           = 0x0000019a,
PH_PERF_SEL_SC3_PA2_FIFO_FULL            = 0x0000019b,
PH_PERF_SEL_SC3_PA2_NULL_WE              = 0x0000019c,
PH_PERF_SEL_SC3_PA2_EVENT_WE             = 0x0000019d,
PH_PERF_SEL_SC3_PA2_FPOV_WE              = 0x0000019e,
PH_PERF_SEL_SC3_PA2_LPOV_WE              = 0x0000019f,
PH_PERF_SEL_SC3_PA2_EOP_WE               = 0x000001a0,
PH_PERF_SEL_SC3_PA2_DATA_FIFO_EOP_RD     = 0x000001a1,
PH_PERF_SEL_SC3_PA2_EOPG_WE              = 0x000001a2,
PH_PERF_SEL_SC3_PA2_DEALLOC_4_0_RD       = 0x000001a3,
PH_PERF_SEL_SC3_PA3_DATA_FIFO_RD         = 0x000001a4,
PH_PERF_SEL_SC3_PA3_DATA_FIFO_WE         = 0x000001a5,
PH_PERF_SEL_SC3_PA3_FIFO_EMPTY           = 0x000001a6,
PH_PERF_SEL_SC3_PA3_FIFO_FULL            = 0x000001a7,
PH_PERF_SEL_SC3_PA3_NULL_WE              = 0x000001a8,
PH_PERF_SEL_SC3_PA3_EVENT_WE             = 0x000001a9,
PH_PERF_SEL_SC3_PA3_FPOV_WE              = 0x000001aa,
PH_PERF_SEL_SC3_PA3_LPOV_WE              = 0x000001ab,
PH_PERF_SEL_SC3_PA3_EOP_WE               = 0x000001ac,
PH_PERF_SEL_SC3_PA3_DATA_FIFO_EOP_RD     = 0x000001ad,
PH_PERF_SEL_SC3_PA3_EOPG_WE              = 0x000001ae,
PH_PERF_SEL_SC3_PA3_DEALLOC_4_0_RD       = 0x000001af,
PH_PERF_SEL_SC3_PA4_DATA_FIFO_RD         = 0x000001b0,
PH_PERF_SEL_SC3_PA4_DATA_FIFO_WE         = 0x000001b1,
PH_PERF_SEL_SC3_PA4_FIFO_EMPTY           = 0x000001b2,
PH_PERF_SEL_SC3_PA4_FIFO_FULL            = 0x000001b3,
PH_PERF_SEL_SC3_PA4_NULL_WE              = 0x000001b4,
PH_PERF_SEL_SC3_PA4_EVENT_WE             = 0x000001b5,
PH_PERF_SEL_SC3_PA4_FPOV_WE              = 0x000001b6,
PH_PERF_SEL_SC3_PA4_LPOV_WE              = 0x000001b7,
PH_PERF_SEL_SC3_PA4_EOP_WE               = 0x000001b8,
PH_PERF_SEL_SC3_PA4_DATA_FIFO_EOP_RD     = 0x000001b9,
PH_PERF_SEL_SC3_PA4_EOPG_WE              = 0x000001ba,
PH_PERF_SEL_SC3_PA4_DEALLOC_4_0_RD       = 0x000001bb,
PH_PERF_SEL_SC3_PA5_DATA_FIFO_RD         = 0x000001bc,
PH_PERF_SEL_SC3_PA5_DATA_FIFO_WE         = 0x000001bd,
PH_PERF_SEL_SC3_PA5_FIFO_EMPTY           = 0x000001be,
PH_PERF_SEL_SC3_PA5_FIFO_FULL            = 0x000001bf,
PH_PERF_SEL_SC3_PA5_NULL_WE              = 0x000001c0,
PH_PERF_SEL_SC3_PA5_EVENT_WE             = 0x000001c1,
PH_PERF_SEL_SC3_PA5_FPOV_WE              = 0x000001c2,
PH_PERF_SEL_SC3_PA5_LPOV_WE              = 0x000001c3,
PH_PERF_SEL_SC3_PA5_EOP_WE               = 0x000001c4,
PH_PERF_SEL_SC3_PA5_DATA_FIFO_EOP_RD     = 0x000001c5,
PH_PERF_SEL_SC3_PA5_EOPG_WE              = 0x000001c6,
PH_PERF_SEL_SC3_PA5_DEALLOC_4_0_RD       = 0x000001c7,
PH_PERF_SEL_SC3_PA6_DATA_FIFO_RD         = 0x000001c8,
PH_PERF_SEL_SC3_PA6_DATA_FIFO_WE         = 0x000001c9,
PH_PERF_SEL_SC3_PA6_FIFO_EMPTY           = 0x000001ca,
PH_PERF_SEL_SC3_PA6_FIFO_FULL            = 0x000001cb,
PH_PERF_SEL_SC3_PA6_NULL_WE              = 0x000001cc,
PH_PERF_SEL_SC3_PA6_EVENT_WE             = 0x000001cd,
PH_PERF_SEL_SC3_PA6_FPOV_WE              = 0x000001ce,
PH_PERF_SEL_SC3_PA6_LPOV_WE              = 0x000001cf,
PH_PERF_SEL_SC3_PA6_EOP_WE               = 0x000001d0,
PH_PERF_SEL_SC3_PA6_DATA_FIFO_EOP_RD     = 0x000001d1,
PH_PERF_SEL_SC3_PA6_EOPG_WE              = 0x000001d2,
PH_PERF_SEL_SC3_PA6_DEALLOC_4_0_RD       = 0x000001d3,
PH_PERF_SEL_SC3_PA7_DATA_FIFO_RD         = 0x000001d4,
PH_PERF_SEL_SC3_PA7_DATA_FIFO_WE         = 0x000001d5,
PH_PERF_SEL_SC3_PA7_FIFO_EMPTY           = 0x000001d6,
PH_PERF_SEL_SC3_PA7_FIFO_FULL            = 0x000001d7,
PH_PERF_SEL_SC3_PA7_NULL_WE              = 0x000001d8,
PH_PERF_SEL_SC3_PA7_EVENT_WE             = 0x000001d9,
PH_PERF_SEL_SC3_PA7_FPOV_WE              = 0x000001da,
PH_PERF_SEL_SC3_PA7_LPOV_WE              = 0x000001db,
PH_PERF_SEL_SC3_PA7_EOP_WE               = 0x000001dc,
PH_PERF_SEL_SC3_PA7_DATA_FIFO_EOP_RD     = 0x000001dd,
PH_PERF_SEL_SC3_PA7_EOPG_WE              = 0x000001de,
PH_PERF_SEL_SC3_PA7_DEALLOC_4_0_RD       = 0x000001df,
PH_PERF_SEL_SC4_SRPS_WINDOW_VALID        = 0x000001e0,
PH_PERF_SEL_SC4_ARB_XFC_ALL_EVENT_OR_PRIM_CYCLES = 0x000001e1,
PH_PERF_SEL_SC4_ARB_XFC_ONLY_PRIM_CYCLES = 0x000001e2,
PH_PERF_SEL_SC4_ARB_XFC_ONLY_ONE_INC_PER_PRIM = 0x000001e3,
PH_PERF_SEL_SC4_ARB_STALLED_FROM_BELOW   = 0x000001e4,
PH_PERF_SEL_SC4_ARB_STARVED_FROM_ABOVE   = 0x000001e5,
PH_PERF_SEL_SC4_ARB_STARVED_FROM_ABOVE_WITH_UNSELECTED_FIFO_NOT_EMPTY = 0x000001e6,
PH_PERF_SEL_SC4_ARB_STARVED_FROM_ABOVE_WITH_UNSELECTED_FIFO_FULL = 0x000001e7,
PH_PERF_SEL_SC4_ARB_BUSY                 = 0x000001e8,
PH_PERF_SEL_SC4_ARB_PA_BUSY_SOP          = 0x000001e9,
PH_PERF_SEL_SC4_ARB_EOP_POP_SYNC_POP     = 0x000001ea,
PH_PERF_SEL_SC4_ARB_EVENT_SYNC_POP       = 0x000001eb,
PH_PERF_SEL_SC4_PS_ENG_MULTICYCLE_BUBBLE = 0x000001ec,
PH_PERF_SEL_SC4_EOP_SYNC_WINDOW          = 0x000001ed,
PH_PERF_SEL_SC4_BUSY_PROCESSING_MULTICYCLE_PRIM = 0x000001ee,
PH_PERF_SEL_SC4_BUSY_CNT_NOT_ZERO        = 0x000001ef,
PH_PERF_SEL_SC4_SEND                     = 0x000001f0,
PH_PERF_SEL_SC4_CREDIT_AT_ZERO_WITH_PENDING_SEND = 0x000001f1,
PH_PERF_SEL_SC4_CREDIT_AT_MAX            = 0x000001f2,
PH_PERF_SEL_SC4_CREDIT_AT_MAX_NO_PENDING_SEND = 0x000001f3,
PH_PERF_SEL_SC4_GFX_PIPE0_TO_1_TRANSITION = 0x000001f4,
PH_PERF_SEL_SC4_GFX_PIPE1_TO_0_TRANSITION = 0x000001f5,
PH_PERF_SEL_SC4_GFX_PIPE_EOP_PRIM_PROVOKED_TRANSITION = 0x000001f6,
PH_PERF_SEL_SC4_GFX_PIPE_EVENT_PROVOKED_TRANSITION = 0x000001f7,
PH_PERF_SEL_SC4_PA0_DATA_FIFO_RD         = 0x000001f8,
PH_PERF_SEL_SC4_PA0_DATA_FIFO_WE         = 0x000001f9,
PH_PERF_SEL_SC4_PA0_FIFO_EMPTY           = 0x000001fa,
PH_PERF_SEL_SC4_PA0_FIFO_FULL            = 0x000001fb,
PH_PERF_SEL_SC4_PA0_NULL_WE              = 0x000001fc,
PH_PERF_SEL_SC4_PA0_EVENT_WE             = 0x000001fd,
PH_PERF_SEL_SC4_PA0_FPOV_WE              = 0x000001fe,
PH_PERF_SEL_SC4_PA0_LPOV_WE              = 0x000001ff,
PH_PERF_SEL_SC4_PA0_EOP_WE               = 0x00000200,
PH_PERF_SEL_SC4_PA0_DATA_FIFO_EOP_RD     = 0x00000201,
PH_PERF_SEL_SC4_PA0_EOPG_WE              = 0x00000202,
PH_PERF_SEL_SC4_PA0_DEALLOC_4_0_RD       = 0x00000203,
PH_PERF_SEL_SC4_PA1_DATA_FIFO_RD         = 0x00000204,
PH_PERF_SEL_SC4_PA1_DATA_FIFO_WE         = 0x00000205,
PH_PERF_SEL_SC4_PA1_FIFO_EMPTY           = 0x00000206,
PH_PERF_SEL_SC4_PA1_FIFO_FULL            = 0x00000207,
PH_PERF_SEL_SC4_PA1_NULL_WE              = 0x00000208,
PH_PERF_SEL_SC4_PA1_EVENT_WE             = 0x00000209,
PH_PERF_SEL_SC4_PA1_FPOV_WE              = 0x0000020a,
PH_PERF_SEL_SC4_PA1_LPOV_WE              = 0x0000020b,
PH_PERF_SEL_SC4_PA1_EOP_WE               = 0x0000020c,
PH_PERF_SEL_SC4_PA1_DATA_FIFO_EOP_RD     = 0x0000020d,
PH_PERF_SEL_SC4_PA1_EOPG_WE              = 0x0000020e,
PH_PERF_SEL_SC4_PA1_DEALLOC_4_0_RD       = 0x0000020f,
PH_PERF_SEL_SC4_PA2_DATA_FIFO_RD         = 0x00000210,
PH_PERF_SEL_SC4_PA2_DATA_FIFO_WE         = 0x00000211,
PH_PERF_SEL_SC4_PA2_FIFO_EMPTY           = 0x00000212,
PH_PERF_SEL_SC4_PA2_FIFO_FULL            = 0x00000213,
PH_PERF_SEL_SC4_PA2_NULL_WE              = 0x00000214,
PH_PERF_SEL_SC4_PA2_EVENT_WE             = 0x00000215,
PH_PERF_SEL_SC4_PA2_FPOV_WE              = 0x00000216,
PH_PERF_SEL_SC4_PA2_LPOV_WE              = 0x00000217,
PH_PERF_SEL_SC4_PA2_EOP_WE               = 0x00000218,
PH_PERF_SEL_SC4_PA2_DATA_FIFO_EOP_RD     = 0x00000219,
PH_PERF_SEL_SC4_PA2_EOPG_WE              = 0x0000021a,
PH_PERF_SEL_SC4_PA2_DEALLOC_4_0_RD       = 0x0000021b,
PH_PERF_SEL_SC4_PA3_DATA_FIFO_RD         = 0x0000021c,
PH_PERF_SEL_SC4_PA3_DATA_FIFO_WE         = 0x0000021d,
PH_PERF_SEL_SC4_PA3_FIFO_EMPTY           = 0x0000021e,
PH_PERF_SEL_SC4_PA3_FIFO_FULL            = 0x0000021f,
PH_PERF_SEL_SC4_PA3_NULL_WE              = 0x00000220,
PH_PERF_SEL_SC4_PA3_EVENT_WE             = 0x00000221,
PH_PERF_SEL_SC4_PA3_FPOV_WE              = 0x00000222,
PH_PERF_SEL_SC4_PA3_LPOV_WE              = 0x00000223,
PH_PERF_SEL_SC4_PA3_EOP_WE               = 0x00000224,
PH_PERF_SEL_SC4_PA3_DATA_FIFO_EOP_RD     = 0x00000225,
PH_PERF_SEL_SC4_PA3_EOPG_WE              = 0x00000226,
PH_PERF_SEL_SC4_PA3_DEALLOC_4_0_RD       = 0x00000227,
PH_PERF_SEL_SC4_PA4_DATA_FIFO_RD         = 0x00000228,
PH_PERF_SEL_SC4_PA4_DATA_FIFO_WE         = 0x00000229,
PH_PERF_SEL_SC4_PA4_FIFO_EMPTY           = 0x0000022a,
PH_PERF_SEL_SC4_PA4_FIFO_FULL            = 0x0000022b,
PH_PERF_SEL_SC4_PA4_NULL_WE              = 0x0000022c,
PH_PERF_SEL_SC4_PA4_EVENT_WE             = 0x0000022d,
PH_PERF_SEL_SC4_PA4_FPOV_WE              = 0x0000022e,
PH_PERF_SEL_SC4_PA4_LPOV_WE              = 0x0000022f,
PH_PERF_SEL_SC4_PA4_EOP_WE               = 0x00000230,
PH_PERF_SEL_SC4_PA4_DATA_FIFO_EOP_RD     = 0x00000231,
PH_PERF_SEL_SC4_PA4_EOPG_WE              = 0x00000232,
PH_PERF_SEL_SC4_PA4_DEALLOC_4_0_RD       = 0x00000233,
PH_PERF_SEL_SC4_PA5_DATA_FIFO_RD         = 0x00000234,
PH_PERF_SEL_SC4_PA5_DATA_FIFO_WE         = 0x00000235,
PH_PERF_SEL_SC4_PA5_FIFO_EMPTY           = 0x00000236,
PH_PERF_SEL_SC4_PA5_FIFO_FULL            = 0x00000237,
PH_PERF_SEL_SC4_PA5_NULL_WE              = 0x00000238,
PH_PERF_SEL_SC4_PA5_EVENT_WE             = 0x00000239,
PH_PERF_SEL_SC4_PA5_FPOV_WE              = 0x0000023a,
PH_PERF_SEL_SC4_PA5_LPOV_WE              = 0x0000023b,
PH_PERF_SEL_SC4_PA5_EOP_WE               = 0x0000023c,
PH_PERF_SEL_SC4_PA5_DATA_FIFO_EOP_RD     = 0x0000023d,
PH_PERF_SEL_SC4_PA5_EOPG_WE              = 0x0000023e,
PH_PERF_SEL_SC4_PA5_DEALLOC_4_0_RD       = 0x0000023f,
PH_PERF_SEL_SC4_PA6_DATA_FIFO_RD         = 0x00000240,
PH_PERF_SEL_SC4_PA6_DATA_FIFO_WE         = 0x00000241,
PH_PERF_SEL_SC4_PA6_FIFO_EMPTY           = 0x00000242,
PH_PERF_SEL_SC4_PA6_FIFO_FULL            = 0x00000243,
PH_PERF_SEL_SC4_PA6_NULL_WE              = 0x00000244,
PH_PERF_SEL_SC4_PA6_EVENT_WE             = 0x00000245,
PH_PERF_SEL_SC4_PA6_FPOV_WE              = 0x00000246,
PH_PERF_SEL_SC4_PA6_LPOV_WE              = 0x00000247,
PH_PERF_SEL_SC4_PA6_EOP_WE               = 0x00000248,
PH_PERF_SEL_SC4_PA6_DATA_FIFO_EOP_RD     = 0x00000249,
PH_PERF_SEL_SC4_PA6_EOPG_WE              = 0x0000024a,
PH_PERF_SEL_SC4_PA6_DEALLOC_4_0_RD       = 0x0000024b,
PH_PERF_SEL_SC4_PA7_DATA_FIFO_RD         = 0x0000024c,
PH_PERF_SEL_SC4_PA7_DATA_FIFO_WE         = 0x0000024d,
PH_PERF_SEL_SC4_PA7_FIFO_EMPTY           = 0x0000024e,
PH_PERF_SEL_SC4_PA7_FIFO_FULL            = 0x0000024f,
PH_PERF_SEL_SC4_PA7_NULL_WE              = 0x00000250,
PH_PERF_SEL_SC4_PA7_EVENT_WE             = 0x00000251,
PH_PERF_SEL_SC4_PA7_FPOV_WE              = 0x00000252,
PH_PERF_SEL_SC4_PA7_LPOV_WE              = 0x00000253,
PH_PERF_SEL_SC4_PA7_EOP_WE               = 0x00000254,
PH_PERF_SEL_SC4_PA7_DATA_FIFO_EOP_RD     = 0x00000255,
PH_PERF_SEL_SC4_PA7_EOPG_WE              = 0x00000256,
PH_PERF_SEL_SC4_PA7_DEALLOC_4_0_RD       = 0x00000257,
PH_PERF_SEL_SC5_SRPS_WINDOW_VALID        = 0x00000258,
PH_PERF_SEL_SC5_ARB_XFC_ALL_EVENT_OR_PRIM_CYCLES = 0x00000259,
PH_PERF_SEL_SC5_ARB_XFC_ONLY_PRIM_CYCLES = 0x0000025a,
PH_PERF_SEL_SC5_ARB_XFC_ONLY_ONE_INC_PER_PRIM = 0x0000025b,
PH_PERF_SEL_SC5_ARB_STALLED_FROM_BELOW   = 0x0000025c,
PH_PERF_SEL_SC5_ARB_STARVED_FROM_ABOVE   = 0x0000025d,
PH_PERF_SEL_SC5_ARB_STARVED_FROM_ABOVE_WITH_UNSELECTED_FIFO_NOT_EMPTY = 0x0000025e,
PH_PERF_SEL_SC5_ARB_STARVED_FROM_ABOVE_WITH_UNSELECTED_FIFO_FULL = 0x0000025f,
PH_PERF_SEL_SC5_ARB_BUSY                 = 0x00000260,
PH_PERF_SEL_SC5_ARB_PA_BUSY_SOP          = 0x00000261,
PH_PERF_SEL_SC5_ARB_EOP_POP_SYNC_POP     = 0x00000262,
PH_PERF_SEL_SC5_ARB_EVENT_SYNC_POP       = 0x00000263,
PH_PERF_SEL_SC5_PS_ENG_MULTICYCLE_BUBBLE = 0x00000264,
PH_PERF_SEL_SC5_EOP_SYNC_WINDOW          = 0x00000265,
PH_PERF_SEL_SC5_BUSY_PROCESSING_MULTICYCLE_PRIM = 0x00000266,
PH_PERF_SEL_SC5_BUSY_CNT_NOT_ZERO        = 0x00000267,
PH_PERF_SEL_SC5_SEND                     = 0x00000268,
PH_PERF_SEL_SC5_CREDIT_AT_ZERO_WITH_PENDING_SEND = 0x00000269,
PH_PERF_SEL_SC5_CREDIT_AT_MAX            = 0x0000026a,
PH_PERF_SEL_SC5_CREDIT_AT_MAX_NO_PENDING_SEND = 0x0000026b,
PH_PERF_SEL_SC5_GFX_PIPE0_TO_1_TRANSITION = 0x0000026c,
PH_PERF_SEL_SC5_GFX_PIPE1_TO_0_TRANSITION = 0x0000026d,
PH_PERF_SEL_SC5_GFX_PIPE_EOP_PRIM_PROVOKED_TRANSITION = 0x0000026e,
PH_PERF_SEL_SC5_GFX_PIPE_EVENT_PROVOKED_TRANSITION = 0x0000026f,
PH_PERF_SEL_SC5_PA0_DATA_FIFO_RD         = 0x00000270,
PH_PERF_SEL_SC5_PA0_DATA_FIFO_WE         = 0x00000271,
PH_PERF_SEL_SC5_PA0_FIFO_EMPTY           = 0x00000272,
PH_PERF_SEL_SC5_PA0_FIFO_FULL            = 0x00000273,
PH_PERF_SEL_SC5_PA0_NULL_WE              = 0x00000274,
PH_PERF_SEL_SC5_PA0_EVENT_WE             = 0x00000275,
PH_PERF_SEL_SC5_PA0_FPOV_WE              = 0x00000276,
PH_PERF_SEL_SC5_PA0_LPOV_WE              = 0x00000277,
PH_PERF_SEL_SC5_PA0_EOP_WE               = 0x00000278,
PH_PERF_SEL_SC5_PA0_DATA_FIFO_EOP_RD     = 0x00000279,
PH_PERF_SEL_SC5_PA0_EOPG_WE              = 0x0000027a,
PH_PERF_SEL_SC5_PA0_DEALLOC_4_0_RD       = 0x0000027b,
PH_PERF_SEL_SC5_PA1_DATA_FIFO_RD         = 0x0000027c,
PH_PERF_SEL_SC5_PA1_DATA_FIFO_WE         = 0x0000027d,
PH_PERF_SEL_SC5_PA1_FIFO_EMPTY           = 0x0000027e,
PH_PERF_SEL_SC5_PA1_FIFO_FULL            = 0x0000027f,
PH_PERF_SEL_SC5_PA1_NULL_WE              = 0x00000280,
PH_PERF_SEL_SC5_PA1_EVENT_WE             = 0x00000281,
PH_PERF_SEL_SC5_PA1_FPOV_WE              = 0x00000282,
PH_PERF_SEL_SC5_PA1_LPOV_WE              = 0x00000283,
PH_PERF_SEL_SC5_PA1_EOP_WE               = 0x00000284,
PH_PERF_SEL_SC5_PA1_DATA_FIFO_EOP_RD     = 0x00000285,
PH_PERF_SEL_SC5_PA1_EOPG_WE              = 0x00000286,
PH_PERF_SEL_SC5_PA1_DEALLOC_4_0_RD       = 0x00000287,
PH_PERF_SEL_SC5_PA2_DATA_FIFO_RD         = 0x00000288,
PH_PERF_SEL_SC5_PA2_DATA_FIFO_WE         = 0x00000289,
PH_PERF_SEL_SC5_PA2_FIFO_EMPTY           = 0x0000028a,
PH_PERF_SEL_SC5_PA2_FIFO_FULL            = 0x0000028b,
PH_PERF_SEL_SC5_PA2_NULL_WE              = 0x0000028c,
PH_PERF_SEL_SC5_PA2_EVENT_WE             = 0x0000028d,
PH_PERF_SEL_SC5_PA2_FPOV_WE              = 0x0000028e,
PH_PERF_SEL_SC5_PA2_LPOV_WE              = 0x0000028f,
PH_PERF_SEL_SC5_PA2_EOP_WE               = 0x00000290,
PH_PERF_SEL_SC5_PA2_DATA_FIFO_EOP_RD     = 0x00000291,
PH_PERF_SEL_SC5_PA2_EOPG_WE              = 0x00000292,
PH_PERF_SEL_SC5_PA2_DEALLOC_4_0_RD       = 0x00000293,
PH_PERF_SEL_SC5_PA3_DATA_FIFO_RD         = 0x00000294,
PH_PERF_SEL_SC5_PA3_DATA_FIFO_WE         = 0x00000295,
PH_PERF_SEL_SC5_PA3_FIFO_EMPTY           = 0x00000296,
PH_PERF_SEL_SC5_PA3_FIFO_FULL            = 0x00000297,
PH_PERF_SEL_SC5_PA3_NULL_WE              = 0x00000298,
PH_PERF_SEL_SC5_PA3_EVENT_WE             = 0x00000299,
PH_PERF_SEL_SC5_PA3_FPOV_WE              = 0x0000029a,
PH_PERF_SEL_SC5_PA3_LPOV_WE              = 0x0000029b,
PH_PERF_SEL_SC5_PA3_EOP_WE               = 0x0000029c,
PH_PERF_SEL_SC5_PA3_DATA_FIFO_EOP_RD     = 0x0000029d,
PH_PERF_SEL_SC5_PA3_EOPG_WE              = 0x0000029e,
PH_PERF_SEL_SC5_PA3_DEALLOC_4_0_RD       = 0x0000029f,
PH_PERF_SEL_SC5_PA4_DATA_FIFO_RD         = 0x000002a0,
PH_PERF_SEL_SC5_PA4_DATA_FIFO_WE         = 0x000002a1,
PH_PERF_SEL_SC5_PA4_FIFO_EMPTY           = 0x000002a2,
PH_PERF_SEL_SC5_PA4_FIFO_FULL            = 0x000002a3,
PH_PERF_SEL_SC5_PA4_NULL_WE              = 0x000002a4,
PH_PERF_SEL_SC5_PA4_EVENT_WE             = 0x000002a5,
PH_PERF_SEL_SC5_PA4_FPOV_WE              = 0x000002a6,
PH_PERF_SEL_SC5_PA4_LPOV_WE              = 0x000002a7,
PH_PERF_SEL_SC5_PA4_EOP_WE               = 0x000002a8,
PH_PERF_SEL_SC5_PA4_DATA_FIFO_EOP_RD     = 0x000002a9,
PH_PERF_SEL_SC5_PA4_EOPG_WE              = 0x000002aa,
PH_PERF_SEL_SC5_PA4_DEALLOC_4_0_RD       = 0x000002ab,
PH_PERF_SEL_SC5_PA5_DATA_FIFO_RD         = 0x000002ac,
PH_PERF_SEL_SC5_PA5_DATA_FIFO_WE         = 0x000002ad,
PH_PERF_SEL_SC5_PA5_FIFO_EMPTY           = 0x000002ae,
PH_PERF_SEL_SC5_PA5_FIFO_FULL            = 0x000002af,
PH_PERF_SEL_SC5_PA5_NULL_WE              = 0x000002b0,
PH_PERF_SEL_SC5_PA5_EVENT_WE             = 0x000002b1,
PH_PERF_SEL_SC5_PA5_FPOV_WE              = 0x000002b2,
PH_PERF_SEL_SC5_PA5_LPOV_WE              = 0x000002b3,
PH_PERF_SEL_SC5_PA5_EOP_WE               = 0x000002b4,
PH_PERF_SEL_SC5_PA5_DATA_FIFO_EOP_RD     = 0x000002b5,
PH_PERF_SEL_SC5_PA5_EOPG_WE              = 0x000002b6,
PH_PERF_SEL_SC5_PA5_DEALLOC_4_0_RD       = 0x000002b7,
PH_PERF_SEL_SC5_PA6_DATA_FIFO_RD         = 0x000002b8,
PH_PERF_SEL_SC5_PA6_DATA_FIFO_WE         = 0x000002b9,
PH_PERF_SEL_SC5_PA6_FIFO_EMPTY           = 0x000002ba,
PH_PERF_SEL_SC5_PA6_FIFO_FULL            = 0x000002bb,
PH_PERF_SEL_SC5_PA6_NULL_WE              = 0x000002bc,
PH_PERF_SEL_SC5_PA6_EVENT_WE             = 0x000002bd,
PH_PERF_SEL_SC5_PA6_FPOV_WE              = 0x000002be,
PH_PERF_SEL_SC5_PA6_LPOV_WE              = 0x000002bf,
PH_PERF_SEL_SC5_PA6_EOP_WE               = 0x000002c0,
PH_PERF_SEL_SC5_PA6_DATA_FIFO_EOP_RD     = 0x000002c1,
PH_PERF_SEL_SC5_PA6_EOPG_WE              = 0x000002c2,
PH_PERF_SEL_SC5_PA6_DEALLOC_4_0_RD       = 0x000002c3,
PH_PERF_SEL_SC5_PA7_DATA_FIFO_RD         = 0x000002c4,
PH_PERF_SEL_SC5_PA7_DATA_FIFO_WE         = 0x000002c5,
PH_PERF_SEL_SC5_PA7_FIFO_EMPTY           = 0x000002c6,
PH_PERF_SEL_SC5_PA7_FIFO_FULL            = 0x000002c7,
PH_PERF_SEL_SC5_PA7_NULL_WE              = 0x000002c8,
PH_PERF_SEL_SC5_PA7_EVENT_WE             = 0x000002c9,
PH_PERF_SEL_SC5_PA7_FPOV_WE              = 0x000002ca,
PH_PERF_SEL_SC5_PA7_LPOV_WE              = 0x000002cb,
PH_PERF_SEL_SC5_PA7_EOP_WE               = 0x000002cc,
PH_PERF_SEL_SC5_PA7_DATA_FIFO_EOP_RD     = 0x000002cd,
PH_PERF_SEL_SC5_PA7_EOPG_WE              = 0x000002ce,
PH_PERF_SEL_SC5_PA7_DEALLOC_4_0_RD       = 0x000002cf,
PH_PERF_SEL_SC6_SRPS_WINDOW_VALID        = 0x000002d0,
PH_PERF_SEL_SC6_ARB_XFC_ALL_EVENT_OR_PRIM_CYCLES = 0x000002d1,
PH_PERF_SEL_SC6_ARB_XFC_ONLY_PRIM_CYCLES = 0x000002d2,
PH_PERF_SEL_SC6_ARB_XFC_ONLY_ONE_INC_PER_PRIM = 0x000002d3,
PH_PERF_SEL_SC6_ARB_STALLED_FROM_BELOW   = 0x000002d4,
PH_PERF_SEL_SC6_ARB_STARVED_FROM_ABOVE   = 0x000002d5,
PH_PERF_SEL_SC6_ARB_STARVED_FROM_ABOVE_WITH_UNSELECTED_FIFO_NOT_EMPTY = 0x000002d6,
PH_PERF_SEL_SC6_ARB_STARVED_FROM_ABOVE_WITH_UNSELECTED_FIFO_FULL = 0x000002d7,
PH_PERF_SEL_SC6_ARB_BUSY                 = 0x000002d8,
PH_PERF_SEL_SC6_ARB_PA_BUSY_SOP          = 0x000002d9,
PH_PERF_SEL_SC6_ARB_EOP_POP_SYNC_POP     = 0x000002da,
PH_PERF_SEL_SC6_ARB_EVENT_SYNC_POP       = 0x000002db,
PH_PERF_SEL_SC6_PS_ENG_MULTICYCLE_BUBBLE = 0x000002dc,
PH_PERF_SEL_SC6_EOP_SYNC_WINDOW          = 0x000002dd,
PH_PERF_SEL_SC6_BUSY_PROCESSING_MULTICYCLE_PRIM = 0x000002de,
PH_PERF_SEL_SC6_BUSY_CNT_NOT_ZERO        = 0x000002df,
PH_PERF_SEL_SC6_SEND                     = 0x000002e0,
PH_PERF_SEL_SC6_CREDIT_AT_ZERO_WITH_PENDING_SEND = 0x000002e1,
PH_PERF_SEL_SC6_CREDIT_AT_MAX            = 0x000002e2,
PH_PERF_SEL_SC6_CREDIT_AT_MAX_NO_PENDING_SEND = 0x000002e3,
PH_PERF_SEL_SC6_GFX_PIPE0_TO_1_TRANSITION = 0x000002e4,
PH_PERF_SEL_SC6_GFX_PIPE1_TO_0_TRANSITION = 0x000002e5,
PH_PERF_SEL_SC6_GFX_PIPE_EOP_PRIM_PROVOKED_TRANSITION = 0x000002e6,
PH_PERF_SEL_SC6_GFX_PIPE_EVENT_PROVOKED_TRANSITION = 0x000002e7,
PH_PERF_SEL_SC6_PA0_DATA_FIFO_RD         = 0x000002e8,
PH_PERF_SEL_SC6_PA0_DATA_FIFO_WE         = 0x000002e9,
PH_PERF_SEL_SC6_PA0_FIFO_EMPTY           = 0x000002ea,
PH_PERF_SEL_SC6_PA0_FIFO_FULL            = 0x000002eb,
PH_PERF_SEL_SC6_PA0_NULL_WE              = 0x000002ec,
PH_PERF_SEL_SC6_PA0_EVENT_WE             = 0x000002ed,
PH_PERF_SEL_SC6_PA0_FPOV_WE              = 0x000002ee,
PH_PERF_SEL_SC6_PA0_LPOV_WE              = 0x000002ef,
PH_PERF_SEL_SC6_PA0_EOP_WE               = 0x000002f0,
PH_PERF_SEL_SC6_PA0_DATA_FIFO_EOP_RD     = 0x000002f1,
PH_PERF_SEL_SC6_PA0_EOPG_WE              = 0x000002f2,
PH_PERF_SEL_SC6_PA0_DEALLOC_4_0_RD       = 0x000002f3,
PH_PERF_SEL_SC6_PA1_DATA_FIFO_RD         = 0x000002f4,
PH_PERF_SEL_SC6_PA1_DATA_FIFO_WE         = 0x000002f5,
PH_PERF_SEL_SC6_PA1_FIFO_EMPTY           = 0x000002f6,
PH_PERF_SEL_SC6_PA1_FIFO_FULL            = 0x000002f7,
PH_PERF_SEL_SC6_PA1_NULL_WE              = 0x000002f8,
PH_PERF_SEL_SC6_PA1_EVENT_WE             = 0x000002f9,
PH_PERF_SEL_SC6_PA1_FPOV_WE              = 0x000002fa,
PH_PERF_SEL_SC6_PA1_LPOV_WE              = 0x000002fb,
PH_PERF_SEL_SC6_PA1_EOP_WE               = 0x000002fc,
PH_PERF_SEL_SC6_PA1_DATA_FIFO_EOP_RD     = 0x000002fd,
PH_PERF_SEL_SC6_PA1_EOPG_WE              = 0x000002fe,
PH_PERF_SEL_SC6_PA1_DEALLOC_4_0_RD       = 0x000002ff,
PH_PERF_SEL_SC6_PA2_DATA_FIFO_RD         = 0x00000300,
PH_PERF_SEL_SC6_PA2_DATA_FIFO_WE         = 0x00000301,
PH_PERF_SEL_SC6_PA2_FIFO_EMPTY           = 0x00000302,
PH_PERF_SEL_SC6_PA2_FIFO_FULL            = 0x00000303,
PH_PERF_SEL_SC6_PA2_NULL_WE              = 0x00000304,
PH_PERF_SEL_SC6_PA2_EVENT_WE             = 0x00000305,
PH_PERF_SEL_SC6_PA2_FPOV_WE              = 0x00000306,
PH_PERF_SEL_SC6_PA2_LPOV_WE              = 0x00000307,
PH_PERF_SEL_SC6_PA2_EOP_WE               = 0x00000308,
PH_PERF_SEL_SC6_PA2_DATA_FIFO_EOP_RD     = 0x00000309,
PH_PERF_SEL_SC6_PA2_EOPG_WE              = 0x0000030a,
PH_PERF_SEL_SC6_PA2_DEALLOC_4_0_RD       = 0x0000030b,
PH_PERF_SEL_SC6_PA3_DATA_FIFO_RD         = 0x0000030c,
PH_PERF_SEL_SC6_PA3_DATA_FIFO_WE         = 0x0000030d,
PH_PERF_SEL_SC6_PA3_FIFO_EMPTY           = 0x0000030e,
PH_PERF_SEL_SC6_PA3_FIFO_FULL            = 0x0000030f,
PH_PERF_SEL_SC6_PA3_NULL_WE              = 0x00000310,
PH_PERF_SEL_SC6_PA3_EVENT_WE             = 0x00000311,
PH_PERF_SEL_SC6_PA3_FPOV_WE              = 0x00000312,
PH_PERF_SEL_SC6_PA3_LPOV_WE              = 0x00000313,
PH_PERF_SEL_SC6_PA3_EOP_WE               = 0x00000314,
PH_PERF_SEL_SC6_PA3_DATA_FIFO_EOP_RD     = 0x00000315,
PH_PERF_SEL_SC6_PA3_EOPG_WE              = 0x00000316,
PH_PERF_SEL_SC6_PA3_DEALLOC_4_0_RD       = 0x00000317,
PH_PERF_SEL_SC6_PA4_DATA_FIFO_RD         = 0x00000318,
PH_PERF_SEL_SC6_PA4_DATA_FIFO_WE         = 0x00000319,
PH_PERF_SEL_SC6_PA4_FIFO_EMPTY           = 0x0000031a,
PH_PERF_SEL_SC6_PA4_FIFO_FULL            = 0x0000031b,
PH_PERF_SEL_SC6_PA4_NULL_WE              = 0x0000031c,
PH_PERF_SEL_SC6_PA4_EVENT_WE             = 0x0000031d,
PH_PERF_SEL_SC6_PA4_FPOV_WE              = 0x0000031e,
PH_PERF_SEL_SC6_PA4_LPOV_WE              = 0x0000031f,
PH_PERF_SEL_SC6_PA4_EOP_WE               = 0x00000320,
PH_PERF_SEL_SC6_PA4_DATA_FIFO_EOP_RD     = 0x00000321,
PH_PERF_SEL_SC6_PA4_EOPG_WE              = 0x00000322,
PH_PERF_SEL_SC6_PA4_DEALLOC_4_0_RD       = 0x00000323,
PH_PERF_SEL_SC6_PA5_DATA_FIFO_RD         = 0x00000324,
PH_PERF_SEL_SC6_PA5_DATA_FIFO_WE         = 0x00000325,
PH_PERF_SEL_SC6_PA5_FIFO_EMPTY           = 0x00000326,
PH_PERF_SEL_SC6_PA5_FIFO_FULL            = 0x00000327,
PH_PERF_SEL_SC6_PA5_NULL_WE              = 0x00000328,
PH_PERF_SEL_SC6_PA5_EVENT_WE             = 0x00000329,
PH_PERF_SEL_SC6_PA5_FPOV_WE              = 0x0000032a,
PH_PERF_SEL_SC6_PA5_LPOV_WE              = 0x0000032b,
PH_PERF_SEL_SC6_PA5_EOP_WE               = 0x0000032c,
PH_PERF_SEL_SC6_PA5_DATA_FIFO_EOP_RD     = 0x0000032d,
PH_PERF_SEL_SC6_PA5_EOPG_WE              = 0x0000032e,
PH_PERF_SEL_SC6_PA5_DEALLOC_4_0_RD       = 0x0000032f,
PH_PERF_SEL_SC6_PA6_DATA_FIFO_RD         = 0x00000330,
PH_PERF_SEL_SC6_PA6_DATA_FIFO_WE         = 0x00000331,
PH_PERF_SEL_SC6_PA6_FIFO_EMPTY           = 0x00000332,
PH_PERF_SEL_SC6_PA6_FIFO_FULL            = 0x00000333,
PH_PERF_SEL_SC6_PA6_NULL_WE              = 0x00000334,
PH_PERF_SEL_SC6_PA6_EVENT_WE             = 0x00000335,
PH_PERF_SEL_SC6_PA6_FPOV_WE              = 0x00000336,
PH_PERF_SEL_SC6_PA6_LPOV_WE              = 0x00000337,
PH_PERF_SEL_SC6_PA6_EOP_WE               = 0x00000338,
PH_PERF_SEL_SC6_PA6_DATA_FIFO_EOP_RD     = 0x00000339,
PH_PERF_SEL_SC6_PA6_EOPG_WE              = 0x0000033a,
PH_PERF_SEL_SC6_PA6_DEALLOC_4_0_RD       = 0x0000033b,
PH_PERF_SEL_SC6_PA7_DATA_FIFO_RD         = 0x0000033c,
PH_PERF_SEL_SC6_PA7_DATA_FIFO_WE         = 0x0000033d,
PH_PERF_SEL_SC6_PA7_FIFO_EMPTY           = 0x0000033e,
PH_PERF_SEL_SC6_PA7_FIFO_FULL            = 0x0000033f,
PH_PERF_SEL_SC6_PA7_NULL_WE              = 0x00000340,
PH_PERF_SEL_SC6_PA7_EVENT_WE             = 0x00000341,
PH_PERF_SEL_SC6_PA7_FPOV_WE              = 0x00000342,
PH_PERF_SEL_SC6_PA7_LPOV_WE              = 0x00000343,
PH_PERF_SEL_SC6_PA7_EOP_WE               = 0x00000344,
PH_PERF_SEL_SC6_PA7_DATA_FIFO_EOP_RD     = 0x00000345,
PH_PERF_SEL_SC6_PA7_EOPG_WE              = 0x00000346,
PH_PERF_SEL_SC6_PA7_DEALLOC_4_0_RD       = 0x00000347,
PH_PERF_SEL_SC7_SRPS_WINDOW_VALID        = 0x00000348,
PH_PERF_SEL_SC7_ARB_XFC_ALL_EVENT_OR_PRIM_CYCLES = 0x00000349,
PH_PERF_SEL_SC7_ARB_XFC_ONLY_PRIM_CYCLES = 0x0000034a,
PH_PERF_SEL_SC7_ARB_XFC_ONLY_ONE_INC_PER_PRIM = 0x0000034b,
PH_PERF_SEL_SC7_ARB_STALLED_FROM_BELOW   = 0x0000034c,
PH_PERF_SEL_SC7_ARB_STARVED_FROM_ABOVE   = 0x0000034d,
PH_PERF_SEL_SC7_ARB_STARVED_FROM_ABOVE_WITH_UNSELECTED_FIFO_NOT_EMPTY = 0x0000034e,
PH_PERF_SEL_SC7_ARB_STARVED_FROM_ABOVE_WITH_UNSELECTED_FIFO_FULL = 0x0000034f,
PH_PERF_SEL_SC7_ARB_BUSY                 = 0x00000350,
PH_PERF_SEL_SC7_ARB_PA_BUSY_SOP          = 0x00000351,
PH_PERF_SEL_SC7_ARB_EOP_POP_SYNC_POP     = 0x00000352,
PH_PERF_SEL_SC7_ARB_EVENT_SYNC_POP       = 0x00000353,
PH_PERF_SEL_SC7_PS_ENG_MULTICYCLE_BUBBLE = 0x00000354,
PH_PERF_SEL_SC7_EOP_SYNC_WINDOW          = 0x00000355,
PH_PERF_SEL_SC7_BUSY_PROCESSING_MULTICYCLE_PRIM = 0x00000356,
PH_PERF_SEL_SC7_BUSY_CNT_NOT_ZERO        = 0x00000357,
PH_PERF_SEL_SC7_SEND                     = 0x00000358,
PH_PERF_SEL_SC7_CREDIT_AT_ZERO_WITH_PENDING_SEND = 0x00000359,
PH_PERF_SEL_SC7_CREDIT_AT_MAX            = 0x0000035a,
PH_PERF_SEL_SC7_CREDIT_AT_MAX_NO_PENDING_SEND = 0x0000035b,
PH_PERF_SEL_SC7_GFX_PIPE0_TO_1_TRANSITION = 0x0000035c,
PH_PERF_SEL_SC7_GFX_PIPE1_TO_0_TRANSITION = 0x0000035d,
PH_PERF_SEL_SC7_GFX_PIPE_EOP_PRIM_PROVOKED_TRANSITION = 0x0000035e,
PH_PERF_SEL_SC7_GFX_PIPE_EVENT_PROVOKED_TRANSITION = 0x0000035f,
PH_PERF_SEL_SC7_PA0_DATA_FIFO_RD         = 0x00000360,
PH_PERF_SEL_SC7_PA0_DATA_FIFO_WE         = 0x00000361,
PH_PERF_SEL_SC7_PA0_FIFO_EMPTY           = 0x00000362,
PH_PERF_SEL_SC7_PA0_FIFO_FULL            = 0x00000363,
PH_PERF_SEL_SC7_PA0_NULL_WE              = 0x00000364,
PH_PERF_SEL_SC7_PA0_EVENT_WE             = 0x00000365,
PH_PERF_SEL_SC7_PA0_FPOV_WE              = 0x00000366,
PH_PERF_SEL_SC7_PA0_LPOV_WE              = 0x00000367,
PH_PERF_SEL_SC7_PA0_EOP_WE               = 0x00000368,
PH_PERF_SEL_SC7_PA0_DATA_FIFO_EOP_RD     = 0x00000369,
PH_PERF_SEL_SC7_PA0_EOPG_WE              = 0x0000036a,
PH_PERF_SEL_SC7_PA0_DEALLOC_4_0_RD       = 0x0000036b,
PH_PERF_SEL_SC7_PA1_DATA_FIFO_RD         = 0x0000036c,
PH_PERF_SEL_SC7_PA1_DATA_FIFO_WE         = 0x0000036d,
PH_PERF_SEL_SC7_PA1_FIFO_EMPTY           = 0x0000036e,
PH_PERF_SEL_SC7_PA1_FIFO_FULL            = 0x0000036f,
PH_PERF_SEL_SC7_PA1_NULL_WE              = 0x00000370,
PH_PERF_SEL_SC7_PA1_EVENT_WE             = 0x00000371,
PH_PERF_SEL_SC7_PA1_FPOV_WE              = 0x00000372,
PH_PERF_SEL_SC7_PA1_LPOV_WE              = 0x00000373,
PH_PERF_SEL_SC7_PA1_EOP_WE               = 0x00000374,
PH_PERF_SEL_SC7_PA1_DATA_FIFO_EOP_RD     = 0x00000375,
PH_PERF_SEL_SC7_PA1_EOPG_WE              = 0x00000376,
PH_PERF_SEL_SC7_PA1_DEALLOC_4_0_RD       = 0x00000377,
PH_PERF_SEL_SC7_PA2_DATA_FIFO_RD         = 0x00000378,
PH_PERF_SEL_SC7_PA2_DATA_FIFO_WE         = 0x00000379,
PH_PERF_SEL_SC7_PA2_FIFO_EMPTY           = 0x0000037a,
PH_PERF_SEL_SC7_PA2_FIFO_FULL            = 0x0000037b,
PH_PERF_SEL_SC7_PA2_NULL_WE              = 0x0000037c,
PH_PERF_SEL_SC7_PA2_EVENT_WE             = 0x0000037d,
PH_PERF_SEL_SC7_PA2_FPOV_WE              = 0x0000037e,
PH_PERF_SEL_SC7_PA2_LPOV_WE              = 0x0000037f,
PH_PERF_SEL_SC7_PA2_EOP_WE               = 0x00000380,
PH_PERF_SEL_SC7_PA2_DATA_FIFO_EOP_RD     = 0x00000381,
PH_PERF_SEL_SC7_PA2_EOPG_WE              = 0x00000382,
PH_PERF_SEL_SC7_PA2_DEALLOC_4_0_RD       = 0x00000383,
PH_PERF_SEL_SC7_PA3_DATA_FIFO_RD         = 0x00000384,
PH_PERF_SEL_SC7_PA3_DATA_FIFO_WE         = 0x00000385,
PH_PERF_SEL_SC7_PA3_FIFO_EMPTY           = 0x00000386,
PH_PERF_SEL_SC7_PA3_FIFO_FULL            = 0x00000387,
PH_PERF_SEL_SC7_PA3_NULL_WE              = 0x00000388,
PH_PERF_SEL_SC7_PA3_EVENT_WE             = 0x00000389,
PH_PERF_SEL_SC7_PA3_FPOV_WE              = 0x0000038a,
PH_PERF_SEL_SC7_PA3_LPOV_WE              = 0x0000038b,
PH_PERF_SEL_SC7_PA3_EOP_WE               = 0x0000038c,
PH_PERF_SEL_SC7_PA3_DATA_FIFO_EOP_RD     = 0x0000038d,
PH_PERF_SEL_SC7_PA3_EOPG_WE              = 0x0000038e,
PH_PERF_SEL_SC7_PA3_DEALLOC_4_0_RD       = 0x0000038f,
PH_PERF_SEL_SC7_PA4_DATA_FIFO_RD         = 0x00000390,
PH_PERF_SEL_SC7_PA4_DATA_FIFO_WE         = 0x00000391,
PH_PERF_SEL_SC7_PA4_FIFO_EMPTY           = 0x00000392,
PH_PERF_SEL_SC7_PA4_FIFO_FULL            = 0x00000393,
PH_PERF_SEL_SC7_PA4_NULL_WE              = 0x00000394,
PH_PERF_SEL_SC7_PA4_EVENT_WE             = 0x00000395,
PH_PERF_SEL_SC7_PA4_FPOV_WE              = 0x00000396,
PH_PERF_SEL_SC7_PA4_LPOV_WE              = 0x00000397,
PH_PERF_SEL_SC7_PA4_EOP_WE               = 0x00000398,
PH_PERF_SEL_SC7_PA4_DATA_FIFO_EOP_RD     = 0x00000399,
PH_PERF_SEL_SC7_PA4_EOPG_WE              = 0x0000039a,
PH_PERF_SEL_SC7_PA4_DEALLOC_4_0_RD       = 0x0000039b,
PH_PERF_SEL_SC7_PA5_DATA_FIFO_RD         = 0x0000039c,
PH_PERF_SEL_SC7_PA5_DATA_FIFO_WE         = 0x0000039d,
PH_PERF_SEL_SC7_PA5_FIFO_EMPTY           = 0x0000039e,
PH_PERF_SEL_SC7_PA5_FIFO_FULL            = 0x0000039f,
PH_PERF_SEL_SC7_PA5_NULL_WE              = 0x000003a0,
PH_PERF_SEL_SC7_PA5_EVENT_WE             = 0x000003a1,
PH_PERF_SEL_SC7_PA5_FPOV_WE              = 0x000003a2,
PH_PERF_SEL_SC7_PA5_LPOV_WE              = 0x000003a3,
PH_PERF_SEL_SC7_PA5_EOP_WE               = 0x000003a4,
PH_PERF_SEL_SC7_PA5_DATA_FIFO_EOP_RD     = 0x000003a5,
PH_PERF_SEL_SC7_PA5_EOPG_WE              = 0x000003a6,
PH_PERF_SEL_SC7_PA5_DEALLOC_4_0_RD       = 0x000003a7,
PH_PERF_SEL_SC7_PA6_DATA_FIFO_RD         = 0x000003a8,
PH_PERF_SEL_SC7_PA6_DATA_FIFO_WE         = 0x000003a9,
PH_PERF_SEL_SC7_PA6_FIFO_EMPTY           = 0x000003aa,
PH_PERF_SEL_SC7_PA6_FIFO_FULL            = 0x000003ab,
PH_PERF_SEL_SC7_PA6_NULL_WE              = 0x000003ac,
PH_PERF_SEL_SC7_PA6_EVENT_WE             = 0x000003ad,
PH_PERF_SEL_SC7_PA6_FPOV_WE              = 0x000003ae,
PH_PERF_SEL_SC7_PA6_LPOV_WE              = 0x000003af,
PH_PERF_SEL_SC7_PA6_EOP_WE               = 0x000003b0,
PH_PERF_SEL_SC7_PA6_DATA_FIFO_EOP_RD     = 0x000003b1,
PH_PERF_SEL_SC7_PA6_EOPG_WE              = 0x000003b2,
PH_PERF_SEL_SC7_PA6_DEALLOC_4_0_RD       = 0x000003b3,
PH_PERF_SEL_SC7_PA7_DATA_FIFO_RD         = 0x000003b4,
PH_PERF_SEL_SC7_PA7_DATA_FIFO_WE         = 0x000003b5,
PH_PERF_SEL_SC7_PA7_FIFO_EMPTY           = 0x000003b6,
PH_PERF_SEL_SC7_PA7_FIFO_FULL            = 0x000003b7,
PH_PERF_SEL_SC7_PA7_NULL_WE              = 0x000003b8,
PH_PERF_SEL_SC7_PA7_EVENT_WE             = 0x000003b9,
PH_PERF_SEL_SC7_PA7_FPOV_WE              = 0x000003ba,
PH_PERF_SEL_SC7_PA7_LPOV_WE              = 0x000003bb,
PH_PERF_SEL_SC7_PA7_EOP_WE               = 0x000003bc,
PH_PERF_SEL_SC7_PA7_DATA_FIFO_EOP_RD     = 0x000003bd,
PH_PERF_SEL_SC7_PA7_EOPG_WE              = 0x000003be,
PH_PERF_SEL_SC7_PA7_DEALLOC_4_0_RD       = 0x000003bf,
PH_PERF_SEL_1_SC_ARB_STALLED_FROM_BELOW  = 0x000003c0,
PH_PERF_SEL_2_SC_ARB_STALLED_FROM_BELOW  = 0x000003c1,
PH_PERF_SEL_3_SC_ARB_STALLED_FROM_BELOW  = 0x000003c2,
PH_PERF_SEL_4_SC_ARB_STALLED_FROM_BELOW  = 0x000003c3,
PH_PERF_SEL_5_SC_ARB_STALLED_FROM_BELOW  = 0x000003c4,
PH_PERF_SEL_6_SC_ARB_STALLED_FROM_BELOW  = 0x000003c5,
PH_PERF_SEL_7_SC_ARB_STALLED_FROM_BELOW  = 0x000003c6,
PH_PERF_SEL_8_SC_ARB_STALLED_FROM_BELOW  = 0x000003c7,
PH_PERF_SEL_1_SC_ARB_STARVED_FROM_ABOVE  = 0x000003c8,
PH_PERF_SEL_2_SC_ARB_STARVED_FROM_ABOVE  = 0x000003c9,
PH_PERF_SEL_3_SC_ARB_STARVED_FROM_ABOVE  = 0x000003ca,
PH_PERF_SEL_4_SC_ARB_STARVED_FROM_ABOVE  = 0x000003cb,
PH_PERF_SEL_5_SC_ARB_STARVED_FROM_ABOVE  = 0x000003cc,
PH_PERF_SEL_6_SC_ARB_STARVED_FROM_ABOVE  = 0x000003cd,
PH_PERF_SEL_7_SC_ARB_STARVED_FROM_ABOVE  = 0x000003ce,
PH_PERF_SEL_8_SC_ARB_STARVED_FROM_ABOVE  = 0x000003cf,
PH_PERF_SEL_1_SC_ARB_STARVED_FROM_ABOVE_WITH_UNSELECTED_FIFO_NOT_EMPTY = 0x000003d0,
PH_PERF_SEL_2_SC_ARB_STARVED_FROM_ABOVE_WITH_UNSELECTED_FIFO_NOT_EMPTY = 0x000003d1,
PH_PERF_SEL_3_SC_ARB_STARVED_FROM_ABOVE_WITH_UNSELECTED_FIFO_NOT_EMPTY = 0x000003d2,
PH_PERF_SEL_4_SC_ARB_STARVED_FROM_ABOVE_WITH_UNSELECTED_FIFO_NOT_EMPTY = 0x000003d3,
PH_PERF_SEL_5_SC_ARB_STARVED_FROM_ABOVE_WITH_UNSELECTED_FIFO_NOT_EMPTY = 0x000003d4,
PH_PERF_SEL_6_SC_ARB_STARVED_FROM_ABOVE_WITH_UNSELECTED_FIFO_NOT_EMPTY = 0x000003d5,
PH_PERF_SEL_7_SC_ARB_STARVED_FROM_ABOVE_WITH_UNSELECTED_FIFO_NOT_EMPTY = 0x000003d6,
PH_PERF_SEL_8_SC_ARB_STARVED_FROM_ABOVE_WITH_UNSELECTED_FIFO_NOT_EMPTY = 0x000003d7,
PH_PERF_SEL_1_SC_ARB_STARVED_FROM_ABOVE_WITH_UNSELECTED_FIFO_FULL = 0x000003d8,
PH_PERF_SEL_2_SC_ARB_STARVED_FROM_ABOVE_WITH_UNSELECTED_FIFO_FULL = 0x000003d9,
PH_PERF_SEL_3_SC_ARB_STARVED_FROM_ABOVE_WITH_UNSELECTED_FIFO_FULL = 0x000003da,
PH_PERF_SEL_4_SC_ARB_STARVED_FROM_ABOVE_WITH_UNSELECTED_FIFO_FULL = 0x000003db,
PH_PERF_SEL_5_SC_ARB_STARVED_FROM_ABOVE_WITH_UNSELECTED_FIFO_FULL = 0x000003dc,
PH_PERF_SEL_6_SC_ARB_STARVED_FROM_ABOVE_WITH_UNSELECTED_FIFO_FULL = 0x000003dd,
PH_PERF_SEL_7_SC_ARB_STARVED_FROM_ABOVE_WITH_UNSELECTED_FIFO_FULL = 0x000003de,
PH_PERF_SEL_8_SC_ARB_STARVED_FROM_ABOVE_WITH_UNSELECTED_FIFO_FULL = 0x000003df,
PH_PERF_SC0_FIFO_STATUS_0                = 0x000003e0,
PH_PERF_SC0_FIFO_STATUS_1                = 0x000003e1,
PH_PERF_SC0_FIFO_STATUS_2                = 0x000003e2,
PH_PERF_SC0_FIFO_STATUS_3                = 0x000003e3,
PH_PERF_SC1_FIFO_STATUS_0                = 0x000003e4,
PH_PERF_SC1_FIFO_STATUS_1                = 0x000003e5,
PH_PERF_SC1_FIFO_STATUS_2                = 0x000003e6,
PH_PERF_SC1_FIFO_STATUS_3                = 0x000003e7,
PH_PERF_SC2_FIFO_STATUS_0                = 0x000003e8,
PH_PERF_SC2_FIFO_STATUS_1                = 0x000003e9,
PH_PERF_SC2_FIFO_STATUS_2                = 0x000003ea,
PH_PERF_SC2_FIFO_STATUS_3                = 0x000003eb,
PH_PERF_SC3_FIFO_STATUS_0                = 0x000003ec,
PH_PERF_SC3_FIFO_STATUS_1                = 0x000003ed,
PH_PERF_SC3_FIFO_STATUS_2                = 0x000003ee,
PH_PERF_SC3_FIFO_STATUS_3                = 0x000003ef,
PH_PERF_SC4_FIFO_STATUS_0                = 0x000003f0,
PH_PERF_SC4_FIFO_STATUS_1                = 0x000003f1,
PH_PERF_SC4_FIFO_STATUS_2                = 0x000003f2,
PH_PERF_SC4_FIFO_STATUS_3                = 0x000003f3,
PH_PERF_SC5_FIFO_STATUS_0                = 0x000003f4,
PH_PERF_SC5_FIFO_STATUS_1                = 0x000003f5,
PH_PERF_SC5_FIFO_STATUS_2                = 0x000003f6,
PH_PERF_SC5_FIFO_STATUS_3                = 0x000003f7,
PH_PERF_SC6_FIFO_STATUS_0                = 0x000003f8,
PH_PERF_SC6_FIFO_STATUS_1                = 0x000003f9,
PH_PERF_SC6_FIFO_STATUS_2                = 0x000003fa,
PH_PERF_SC6_FIFO_STATUS_3                = 0x000003fb,
PH_PERF_SC7_FIFO_STATUS_0                = 0x000003fc,
PH_PERF_SC7_FIFO_STATUS_1                = 0x000003fd,
PH_PERF_SC7_FIFO_STATUS_2                = 0x000003fe,
PH_PERF_SC7_FIFO_STATUS_3                = 0x000003ff,
} PH_PERFCNT_SEL;

/*
 * PhSPIstatusMode enum
 */

typedef enum PhSPIstatusMode {
PH_SPI_MODE_LARGEST_PA_PH_FIFO_COUNT     = 0x00000000,
PH_SPI_MODE_ARBITER_SELECTED_PA_PH_FIFO_COUNT = 0x00000001,
PH_SPI_MODE_DISABLED                     = 0x00000002,
} PhSPIstatusMode;

/*******************************************************
 * RMI Enums
 *******************************************************/

/*
 * RMIPerfSel enum
 */

typedef enum RMIPerfSel {
RMI_PERF_SEL_RB_RMI_WRREQ_ALL_CID        = 0x00000000,
RMI_PERF_SEL_RB_RMI_RDREQ_ALL_CID        = 0x00000001,
} RMIPerfSel;

/*******************************************************
 * PMM Enums
 *******************************************************/

/*
 * GCRPerfSel enum
 */

typedef enum GCRPerfSel {
GCR_PERF_SEL_NONE                        = 0x00000000,
GCR_PERF_SEL_SDMA0_ALL_REQ               = 0x00000001,
GCR_PERF_SEL_SDMA0_GL2_RANGE_REQ         = 0x00000002,
GCR_PERF_SEL_SDMA0_GL2_RANGE_LT16K_REQ   = 0x00000003,
GCR_PERF_SEL_SDMA0_GL2_RANGE_16K_REQ     = 0x00000004,
GCR_PERF_SEL_SDMA0_GL2_RANGE_GT16K_REQ   = 0x00000005,
GCR_PERF_SEL_SDMA0_GL2_ALL_REQ           = 0x00000006,
GCR_PERF_SEL_SDMA0_GL1_RANGE_REQ         = 0x00000007,
GCR_PERF_SEL_SDMA0_GL1_RANGE_LT16K_REQ   = 0x00000008,
GCR_PERF_SEL_SDMA0_GL1_RANGE_16K_REQ     = 0x00000009,
GCR_PERF_SEL_SDMA0_GL1_RANGE_GT16K_REQ   = 0x0000000a,
GCR_PERF_SEL_SDMA0_GL1_ALL_REQ           = 0x0000000b,
GCR_PERF_SEL_SDMA0_METADATA_REQ          = 0x0000000c,
GCR_PERF_SEL_SDMA0_SQC_DATA_REQ          = 0x0000000d,
GCR_PERF_SEL_SDMA0_SQC_INST_REQ          = 0x0000000e,
GCR_PERF_SEL_SDMA0_TCP_REQ               = 0x0000000f,
GCR_PERF_SEL_SDMA0_TCP_TLB_SHOOTDOWN_REQ = 0x00000010,
GCR_PERF_SEL_SDMA1_ALL_REQ               = 0x00000011,
GCR_PERF_SEL_SDMA1_GL2_RANGE_REQ         = 0x00000012,
GCR_PERF_SEL_SDMA1_GL2_RANGE_LT16K_REQ   = 0x00000013,
GCR_PERF_SEL_SDMA1_GL2_RANGE_16K_REQ     = 0x00000014,
GCR_PERF_SEL_SDMA1_GL2_RANGE_GT16K_REQ   = 0x00000015,
GCR_PERF_SEL_SDMA1_GL2_ALL_REQ           = 0x00000016,
GCR_PERF_SEL_SDMA1_GL1_RANGE_REQ         = 0x00000017,
GCR_PERF_SEL_SDMA1_GL1_RANGE_LT16K_REQ   = 0x00000018,
GCR_PERF_SEL_SDMA1_GL1_RANGE_16K_REQ     = 0x00000019,
GCR_PERF_SEL_SDMA1_GL1_RANGE_GT16K_REQ   = 0x0000001a,
GCR_PERF_SEL_SDMA1_GL1_ALL_REQ           = 0x0000001b,
GCR_PERF_SEL_SDMA1_METADATA_REQ          = 0x0000001c,
GCR_PERF_SEL_SDMA1_SQC_DATA_REQ          = 0x0000001d,
GCR_PERF_SEL_SDMA1_SQC_INST_REQ          = 0x0000001e,
GCR_PERF_SEL_SDMA1_TCP_REQ               = 0x0000001f,
GCR_PERF_SEL_SDMA1_TCP_TLB_SHOOTDOWN_REQ = 0x00000020,
GCR_PERF_SEL_CPC_ALL_REQ                 = 0x00000021,
GCR_PERF_SEL_CPC_GL2_RANGE_REQ           = 0x00000022,
GCR_PERF_SEL_CPC_GL2_RANGE_LT16K_REQ     = 0x00000023,
GCR_PERF_SEL_CPC_GL2_RANGE_16K_REQ       = 0x00000024,
GCR_PERF_SEL_CPC_GL2_RANGE_GT16K_REQ     = 0x00000025,
GCR_PERF_SEL_CPC_GL2_ALL_REQ             = 0x00000026,
GCR_PERF_SEL_CPC_GL1_RANGE_REQ           = 0x00000027,
GCR_PERF_SEL_CPC_GL1_RANGE_LT16K_REQ     = 0x00000028,
GCR_PERF_SEL_CPC_GL1_RANGE_16K_REQ       = 0x00000029,
GCR_PERF_SEL_CPC_GL1_RANGE_GT16K_REQ     = 0x0000002a,
GCR_PERF_SEL_CPC_GL1_ALL_REQ             = 0x0000002b,
GCR_PERF_SEL_CPC_METADATA_REQ            = 0x0000002c,
GCR_PERF_SEL_CPC_SQC_DATA_REQ            = 0x0000002d,
GCR_PERF_SEL_CPC_SQC_INST_REQ            = 0x0000002e,
GCR_PERF_SEL_CPC_TCP_REQ                 = 0x0000002f,
GCR_PERF_SEL_CPC_TCP_TLB_SHOOTDOWN_REQ   = 0x00000030,
GCR_PERF_SEL_CPG_ALL_REQ                 = 0x00000031,
GCR_PERF_SEL_CPG_GL2_RANGE_REQ           = 0x00000032,
GCR_PERF_SEL_CPG_GL2_RANGE_LT16K_REQ     = 0x00000033,
GCR_PERF_SEL_CPG_GL2_RANGE_16K_REQ       = 0x00000034,
GCR_PERF_SEL_CPG_GL2_RANGE_GT16K_REQ     = 0x00000035,
GCR_PERF_SEL_CPG_GL2_ALL_REQ             = 0x00000036,
GCR_PERF_SEL_CPG_GL1_RANGE_REQ           = 0x00000037,
GCR_PERF_SEL_CPG_GL1_RANGE_LT16K_REQ     = 0x00000038,
GCR_PERF_SEL_CPG_GL1_RANGE_16K_REQ       = 0x00000039,
GCR_PERF_SEL_CPG_GL1_RANGE_GT16K_REQ     = 0x0000003a,
GCR_PERF_SEL_CPG_GL1_ALL_REQ             = 0x0000003b,
GCR_PERF_SEL_CPG_METADATA_REQ            = 0x0000003c,
GCR_PERF_SEL_CPG_SQC_DATA_REQ            = 0x0000003d,
GCR_PERF_SEL_CPG_SQC_INST_REQ            = 0x0000003e,
GCR_PERF_SEL_CPG_TCP_REQ                 = 0x0000003f,
GCR_PERF_SEL_CPG_TCP_TLB_SHOOTDOWN_REQ   = 0x00000040,
GCR_PERF_SEL_CPF_ALL_REQ                 = 0x00000041,
GCR_PERF_SEL_CPF_GL2_RANGE_REQ           = 0x00000042,
GCR_PERF_SEL_CPF_GL2_RANGE_LT16K_REQ     = 0x00000043,
GCR_PERF_SEL_CPF_GL2_RANGE_16K_REQ       = 0x00000044,
GCR_PERF_SEL_CPF_GL2_RANGE_GT16K_REQ     = 0x00000045,
GCR_PERF_SEL_CPF_GL2_ALL_REQ             = 0x00000046,
GCR_PERF_SEL_CPF_GL1_RANGE_REQ           = 0x00000047,
GCR_PERF_SEL_CPF_GL1_RANGE_LT16K_REQ     = 0x00000048,
GCR_PERF_SEL_CPF_GL1_RANGE_16K_REQ       = 0x00000049,
GCR_PERF_SEL_CPF_GL1_RANGE_GT16K_REQ     = 0x0000004a,
GCR_PERF_SEL_CPF_GL1_ALL_REQ             = 0x0000004b,
GCR_PERF_SEL_CPF_METADATA_REQ            = 0x0000004c,
GCR_PERF_SEL_CPF_SQC_DATA_REQ            = 0x0000004d,
GCR_PERF_SEL_CPF_SQC_INST_REQ            = 0x0000004e,
GCR_PERF_SEL_CPF_TCP_REQ                 = 0x0000004f,
GCR_PERF_SEL_CPF_TCP_TLB_SHOOTDOWN_REQ   = 0x00000050,
GCR_PERF_SEL_VIRT_REQ                    = 0x00000051,
GCR_PERF_SEL_PHY_REQ                     = 0x00000052,
GCR_PERF_SEL_TLB_SHOOTDOWN_HEAVY_REQ     = 0x00000053,
GCR_PERF_SEL_TLB_SHOOTDOWN_LIGHT_REQ     = 0x00000054,
GCR_PERF_SEL_ALL_REQ                     = 0x00000055,
GCR_PERF_SEL_CLK_FOR_PHY_OUTSTANDING_REQ = 0x00000056,
GCR_PERF_SEL_CLK_FOR_VIRT_OUTSTANDING_REQ = 0x00000057,
GCR_PERF_SEL_CLK_FOR_ALL_OUTSTANDING_REQ = 0x00000058,
GCR_PERF_SEL_UTCL2_REQ                   = 0x00000059,
GCR_PERF_SEL_UTCL2_RET                   = 0x0000005a,
GCR_PERF_SEL_UTCL2_OUT_OF_CREDIT_EVENT   = 0x0000005b,
GCR_PERF_SEL_UTCL2_INFLIGHT_REQ          = 0x0000005c,
GCR_PERF_SEL_UTCL2_FILTERED_RET          = 0x0000005d,
GCR_PERF_SEL_RLC_ALL_REQ                 = 0x0000005e,
GCR_PERF_SEL_RLC_GL2_RANGE_REQ           = 0x0000005f,
GCR_PERF_SEL_RLC_GL2_RANGE_LT16K_REQ     = 0x00000060,
GCR_PERF_SEL_RLC_GL2_RANGE_16K_REQ       = 0x00000061,
GCR_PERF_SEL_RLC_GL2_RANGE_GT16K_REQ     = 0x00000062,
GCR_PERF_SEL_RLC_GL2_ALL_REQ             = 0x00000063,
GCR_PERF_SEL_RLC_GL1_RANGE_REQ           = 0x00000064,
GCR_PERF_SEL_RLC_GL1_RANGE_LT16K_REQ     = 0x00000065,
GCR_PERF_SEL_RLC_GL1_RANGE_16K_REQ       = 0x00000066,
GCR_PERF_SEL_RLC_GL1_RANGE_GT16K_REQ     = 0x00000067,
GCR_PERF_SEL_RLC_GL1_ALL_REQ             = 0x00000068,
GCR_PERF_SEL_RLC_METADATA_REQ            = 0x00000069,
GCR_PERF_SEL_RLC_SQC_DATA_REQ            = 0x0000006a,
GCR_PERF_SEL_RLC_SQC_INST_REQ            = 0x0000006b,
GCR_PERF_SEL_RLC_TCP_REQ                 = 0x0000006c,
GCR_PERF_SEL_RLC_TCP_TLB_SHOOTDOWN_REQ   = 0x0000006d,
GCR_PERF_SEL_PM_ALL_REQ                  = 0x0000006e,
GCR_PERF_SEL_PM_GL2_RANGE_REQ            = 0x0000006f,
GCR_PERF_SEL_PM_GL2_RANGE_LT16K_REQ      = 0x00000070,
GCR_PERF_SEL_PM_GL2_RANGE_16K_REQ        = 0x00000071,
GCR_PERF_SEL_PM_GL2_RANGE_GT16K_REQ      = 0x00000072,
GCR_PERF_SEL_PM_GL2_ALL_REQ              = 0x00000073,
GCR_PERF_SEL_PM_GL1_RANGE_REQ            = 0x00000074,
GCR_PERF_SEL_PM_GL1_RANGE_LT16K_REQ      = 0x00000075,
GCR_PERF_SEL_PM_GL1_RANGE_16K_REQ        = 0x00000076,
GCR_PERF_SEL_PM_GL1_RANGE_GT16K_REQ      = 0x00000077,
GCR_PERF_SEL_PM_GL1_ALL_REQ              = 0x00000078,
GCR_PERF_SEL_PM_METADATA_REQ             = 0x00000079,
GCR_PERF_SEL_PM_SQC_DATA_REQ             = 0x0000007a,
GCR_PERF_SEL_PM_SQC_INST_REQ             = 0x0000007b,
GCR_PERF_SEL_PM_TCP_REQ                  = 0x0000007c,
GCR_PERF_SEL_PM_TCP_TLB_SHOOTDOWN_REQ    = 0x0000007d,
GCR_PERF_SEL_PIO_ALL_REQ                 = 0x0000007e,
GCR_PERF_SEL_PIO_GL2_RANGE_REQ           = 0x0000007f,
GCR_PERF_SEL_PIO_GL2_RANGE_LT16K_REQ     = 0x00000080,
GCR_PERF_SEL_PIO_GL2_RANGE_16K_REQ       = 0x00000081,
GCR_PERF_SEL_PIO_GL2_RANGE_GT16K_REQ     = 0x00000082,
GCR_PERF_SEL_PIO_GL2_ALL_REQ             = 0x00000083,
GCR_PERF_SEL_PIO_GL1_RANGE_REQ           = 0x00000084,
GCR_PERF_SEL_PIO_GL1_RANGE_LT16K_REQ     = 0x00000085,
GCR_PERF_SEL_PIO_GL1_RANGE_16K_REQ       = 0x00000086,
GCR_PERF_SEL_PIO_GL1_RANGE_GT16K_REQ     = 0x00000087,
GCR_PERF_SEL_PIO_GL1_ALL_REQ             = 0x00000088,
GCR_PERF_SEL_PIO_METADATA_REQ            = 0x00000089,
GCR_PERF_SEL_PIO_SQC_DATA_REQ            = 0x0000008a,
GCR_PERF_SEL_PIO_SQC_INST_REQ            = 0x0000008b,
GCR_PERF_SEL_PIO_TCP_REQ                 = 0x0000008c,
GCR_PERF_SEL_PIO_TCP_TLB_SHOOTDOWN_REQ   = 0x0000008d,
} GCRPerfSel;

/*******************************************************
 * UTCL1 Enums
 *******************************************************/

/*
 * UTCL1PerfSel enum
 */

typedef enum UTCL1PerfSel {
UTCL1_PERF_SEL_NONE                      = 0x00000000,
UTCL1_PERF_SEL_REQS                      = 0x00000001,
UTCL1_PERF_SEL_HITS                      = 0x00000002,
UTCL1_PERF_SEL_MISSES                    = 0x00000003,
UTCL1_PERF_SEL_MH_RECENT_BUF_HIT         = 0x00000004,
UTCL1_PERF_SEL_MH_DUPLICATE_DETECT       = 0x00000005,
UTCL1_PERF_SEL_UTCL2_REQS                = 0x00000006,
UTCL1_PERF_SEL_UTCL2_RET_XNACK_RETRY     = 0x00000007,
UTCL1_PERF_SEL_UTCL2_RET_FAULT           = 0x00000008,
UTCL1_PERF_SEL_STALL_UTCL2_CREDITS       = 0x00000009,
UTCL1_PERF_SEL_STALL_MH_FULL             = 0x0000000a,
UTCL1_PERF_SEL_UTCL2_REQS_OUTSTANDING_ACCUM = 0x0000000b,
UTCL1_PERF_SEL_UTCL2_RET_CNT             = 0x0000000c,
UTCL1_PERF_SEL_RTNS                      = 0x0000000d,
UTCL1_PERF_SEL_XLAT_REQ_BUSY             = 0x0000000e,
UTCL1_PERF_SEL_BYPASS_REQS               = 0x0000000f,
UTCL1_PERF_SEL_HIT_INV_FILTER_REQS       = 0x00000010,
UTCL1_PERF_SEL_UTCL2_RET_PERM_FAULT      = 0x00000011,
UTCL1_PERF_SEL_UTCL2_RET_PRT_FAULT       = 0x00000012,
UTCL1_PERF_SEL_CP_INVREQS                = 0x00000013,
UTCL1_PERF_SEL_UTCL2_UTCL1_INVREQS       = 0x00000014,
UTCL1_PERF_SEL_RANGE_INVREQS             = 0x00000015,
UTCL1_PERF_SEL_INV_ALL_VMID_INVREQS      = 0x00000016,
} UTCL1PerfSel;

/*******************************************************
 * IH Enums
 *******************************************************/

/*
 * IH_CLIENT_TYPE enum
 */

typedef enum IH_CLIENT_TYPE {
IH_GFX_VMID_CLIENT                       = 0x00000000,
IH_MM_VMID_CLIENT                        = 0x00000001,
IH_MULTI_VMID_CLIENT                     = 0x00000002,
IH_CLIENT_TYPE_RESERVED                  = 0x00000003,
} IH_CLIENT_TYPE;

/*
 * IH_INTERFACE_TYPE enum
 */

typedef enum IH_INTERFACE_TYPE {
IH_LEGACY_INTERFACE                      = 0x00000000,
IH_REGISTER_WRITE_INTERFACE              = 0x00000001,
} IH_INTERFACE_TYPE;

/*
 * IH_PERF_SEL enum
 */

typedef enum IH_PERF_SEL {
IH_PERF_SEL_CYCLE                        = 0x00000000,
IH_PERF_SEL_IDLE                         = 0x00000001,
IH_PERF_SEL_INPUT_IDLE                   = 0x00000002,
IH_PERF_SEL_BUFFER_IDLE                  = 0x00000003,
IH_PERF_SEL_RB0_FULL                     = 0x00000004,
IH_PERF_SEL_RB0_OVERFLOW                 = 0x00000005,
IH_PERF_SEL_RB0_WPTR_WRITEBACK           = 0x00000006,
IH_PERF_SEL_RB0_WPTR_WRAP                = 0x00000007,
IH_PERF_SEL_RB0_RPTR_WRAP                = 0x00000008,
IH_PERF_SEL_MC_WR_IDLE                   = 0x00000009,
IH_PERF_SEL_MC_WR_COUNT                  = 0x0000000a,
IH_PERF_SEL_MC_WR_STALL                  = 0x0000000b,
IH_PERF_SEL_MC_WR_CLEAN_PENDING          = 0x0000000c,
IH_PERF_SEL_MC_WR_CLEAN_STALL            = 0x0000000d,
IH_PERF_SEL_BIF_LINE0_RISING             = 0x0000000e,
IH_PERF_SEL_BIF_LINE0_FALLING            = 0x0000000f,
IH_PERF_SEL_RB1_FULL                     = 0x00000010,
IH_PERF_SEL_RB1_OVERFLOW                 = 0x00000011,
IH_PERF_SEL_COOKIE_REC_ERROR             = 0x00000012,
IH_PERF_SEL_RB1_WPTR_WRAP                = 0x00000013,
IH_PERF_SEL_RB1_RPTR_WRAP                = 0x00000014,
IH_PERF_SEL_RB2_FULL                     = 0x00000015,
IH_PERF_SEL_RB2_OVERFLOW                 = 0x00000016,
IH_PERF_SEL_CLIENT_CREDIT_ERROR          = 0x00000017,
IH_PERF_SEL_RB2_WPTR_WRAP                = 0x00000018,
IH_PERF_SEL_RB2_RPTR_WRAP                = 0x00000019,
IH_PERF_SEL_STORM_CLIENT_INT_DROP        = 0x0000001a,
IH_PERF_SEL_SELF_IV_VALID                = 0x0000001b,
IH_PERF_SEL_BUFFER_FIFO_FULL             = 0x0000001c,
IH_PERF_SEL_RB0_FULL_VF0                 = 0x0000001d,
IH_PERF_SEL_RB0_FULL_VF1                 = 0x0000001e,
IH_PERF_SEL_RB0_FULL_VF2                 = 0x0000001f,
IH_PERF_SEL_RB0_FULL_VF3                 = 0x00000020,
IH_PERF_SEL_RB0_FULL_VF4                 = 0x00000021,
IH_PERF_SEL_RB0_FULL_VF5                 = 0x00000022,
IH_PERF_SEL_RB0_FULL_VF6                 = 0x00000023,
IH_PERF_SEL_RB0_FULL_VF7                 = 0x00000024,
IH_PERF_SEL_RB0_FULL_VF8                 = 0x00000025,
IH_PERF_SEL_RB0_FULL_VF9                 = 0x00000026,
IH_PERF_SEL_RB0_FULL_VF10                = 0x00000027,
IH_PERF_SEL_RB0_FULL_VF11                = 0x00000028,
IH_PERF_SEL_RB0_FULL_VF12                = 0x00000029,
IH_PERF_SEL_RB0_FULL_VF13                = 0x0000002a,
IH_PERF_SEL_RB0_FULL_VF14                = 0x0000002b,
IH_PERF_SEL_RB0_FULL_VF15                = 0x0000002c,
IH_PERF_SEL_RB0_OVERFLOW_VF0             = 0x0000002d,
IH_PERF_SEL_RB0_OVERFLOW_VF1             = 0x0000002e,
IH_PERF_SEL_RB0_OVERFLOW_VF2             = 0x0000002f,
IH_PERF_SEL_RB0_OVERFLOW_VF3             = 0x00000030,
IH_PERF_SEL_RB0_OVERFLOW_VF4             = 0x00000031,
IH_PERF_SEL_RB0_OVERFLOW_VF5             = 0x00000032,
IH_PERF_SEL_RB0_OVERFLOW_VF6             = 0x00000033,
IH_PERF_SEL_RB0_OVERFLOW_VF7             = 0x00000034,
IH_PERF_SEL_RB0_OVERFLOW_VF8             = 0x00000035,
IH_PERF_SEL_RB0_OVERFLOW_VF9             = 0x00000036,
IH_PERF_SEL_RB0_OVERFLOW_VF10            = 0x00000037,
IH_PERF_SEL_RB0_OVERFLOW_VF11            = 0x00000038,
IH_PERF_SEL_RB0_OVERFLOW_VF12            = 0x00000039,
IH_PERF_SEL_RB0_OVERFLOW_VF13            = 0x0000003a,
IH_PERF_SEL_RB0_OVERFLOW_VF14            = 0x0000003b,
IH_PERF_SEL_RB0_OVERFLOW_VF15            = 0x0000003c,
IH_PERF_SEL_RB0_WPTR_WRITEBACK_VF0       = 0x0000003d,
IH_PERF_SEL_RB0_WPTR_WRITEBACK_VF1       = 0x0000003e,
IH_PERF_SEL_RB0_WPTR_WRITEBACK_VF2       = 0x0000003f,
IH_PERF_SEL_RB0_WPTR_WRITEBACK_VF3       = 0x00000040,
IH_PERF_SEL_RB0_WPTR_WRITEBACK_VF4       = 0x00000041,
IH_PERF_SEL_RB0_WPTR_WRITEBACK_VF5       = 0x00000042,
IH_PERF_SEL_RB0_WPTR_WRITEBACK_VF6       = 0x00000043,
IH_PERF_SEL_RB0_WPTR_WRITEBACK_VF7       = 0x00000044,
IH_PERF_SEL_RB0_WPTR_WRITEBACK_VF8       = 0x00000045,
IH_PERF_SEL_RB0_WPTR_WRITEBACK_VF9       = 0x00000046,
IH_PERF_SEL_RB0_WPTR_WRITEBACK_VF10      = 0x00000047,
IH_PERF_SEL_RB0_WPTR_WRITEBACK_VF11      = 0x00000048,
IH_PERF_SEL_RB0_WPTR_WRITEBACK_VF12      = 0x00000049,
IH_PERF_SEL_RB0_WPTR_WRITEBACK_VF13      = 0x0000004a,
IH_PERF_SEL_RB0_WPTR_WRITEBACK_VF14      = 0x0000004b,
IH_PERF_SEL_RB0_WPTR_WRITEBACK_VF15      = 0x0000004c,
IH_PERF_SEL_RB0_WPTR_WRAP_VF0            = 0x0000004d,
IH_PERF_SEL_RB0_WPTR_WRAP_VF1            = 0x0000004e,
IH_PERF_SEL_RB0_WPTR_WRAP_VF2            = 0x0000004f,
IH_PERF_SEL_RB0_WPTR_WRAP_VF3            = 0x00000050,
IH_PERF_SEL_RB0_WPTR_WRAP_VF4            = 0x00000051,
IH_PERF_SEL_RB0_WPTR_WRAP_VF5            = 0x00000052,
IH_PERF_SEL_RB0_WPTR_WRAP_VF6            = 0x00000053,
IH_PERF_SEL_RB0_WPTR_WRAP_VF7            = 0x00000054,
IH_PERF_SEL_RB0_WPTR_WRAP_VF8            = 0x00000055,
IH_PERF_SEL_RB0_WPTR_WRAP_VF9            = 0x00000056,
IH_PERF_SEL_RB0_WPTR_WRAP_VF10           = 0x00000057,
IH_PERF_SEL_RB0_WPTR_WRAP_VF11           = 0x00000058,
IH_PERF_SEL_RB0_WPTR_WRAP_VF12           = 0x00000059,
IH_PERF_SEL_RB0_WPTR_WRAP_VF13           = 0x0000005a,
IH_PERF_SEL_RB0_WPTR_WRAP_VF14           = 0x0000005b,
IH_PERF_SEL_RB0_WPTR_WRAP_VF15           = 0x0000005c,
IH_PERF_SEL_RB0_RPTR_WRAP_VF0            = 0x0000005d,
IH_PERF_SEL_RB0_RPTR_WRAP_VF1            = 0x0000005e,
IH_PERF_SEL_RB0_RPTR_WRAP_VF2            = 0x0000005f,
IH_PERF_SEL_RB0_RPTR_WRAP_VF3            = 0x00000060,
IH_PERF_SEL_RB0_RPTR_WRAP_VF4            = 0x00000061,
IH_PERF_SEL_RB0_RPTR_WRAP_VF5            = 0x00000062,
IH_PERF_SEL_RB0_RPTR_WRAP_VF6            = 0x00000063,
IH_PERF_SEL_RB0_RPTR_WRAP_VF7            = 0x00000064,
IH_PERF_SEL_RB0_RPTR_WRAP_VF8            = 0x00000065,
IH_PERF_SEL_RB0_RPTR_WRAP_VF9            = 0x00000066,
IH_PERF_SEL_RB0_RPTR_WRAP_VF10           = 0x00000067,
IH_PERF_SEL_RB0_RPTR_WRAP_VF11           = 0x00000068,
IH_PERF_SEL_RB0_RPTR_WRAP_VF12           = 0x00000069,
IH_PERF_SEL_RB0_RPTR_WRAP_VF13           = 0x0000006a,
IH_PERF_SEL_RB0_RPTR_WRAP_VF14           = 0x0000006b,
IH_PERF_SEL_RB0_RPTR_WRAP_VF15           = 0x0000006c,
IH_PERF_SEL_BIF_LINE0_RISING_VF0         = 0x0000006d,
IH_PERF_SEL_BIF_LINE0_RISING_VF1         = 0x0000006e,
IH_PERF_SEL_BIF_LINE0_RISING_VF2         = 0x0000006f,
IH_PERF_SEL_BIF_LINE0_RISING_VF3         = 0x00000070,
IH_PERF_SEL_BIF_LINE0_RISING_VF4         = 0x00000071,
IH_PERF_SEL_BIF_LINE0_RISING_VF5         = 0x00000072,
IH_PERF_SEL_BIF_LINE0_RISING_VF6         = 0x00000073,
IH_PERF_SEL_BIF_LINE0_RISING_VF7         = 0x00000074,
IH_PERF_SEL_BIF_LINE0_RISING_VF8         = 0x00000075,
IH_PERF_SEL_BIF_LINE0_RISING_VF9         = 0x00000076,
IH_PERF_SEL_BIF_LINE0_RISING_VF10        = 0x00000077,
IH_PERF_SEL_BIF_LINE0_RISING_VF11        = 0x00000078,
IH_PERF_SEL_BIF_LINE0_RISING_VF12        = 0x00000079,
IH_PERF_SEL_BIF_LINE0_RISING_VF13        = 0x0000007a,
IH_PERF_SEL_BIF_LINE0_RISING_VF14        = 0x0000007b,
IH_PERF_SEL_BIF_LINE0_RISING_VF15        = 0x0000007c,
IH_PERF_SEL_BIF_LINE0_FALLING_VF0        = 0x0000007d,
IH_PERF_SEL_BIF_LINE0_FALLING_VF1        = 0x0000007e,
IH_PERF_SEL_BIF_LINE0_FALLING_VF2        = 0x0000007f,
IH_PERF_SEL_BIF_LINE0_FALLING_VF3        = 0x00000080,
IH_PERF_SEL_BIF_LINE0_FALLING_VF4        = 0x00000081,
IH_PERF_SEL_BIF_LINE0_FALLING_VF5        = 0x00000082,
IH_PERF_SEL_BIF_LINE0_FALLING_VF6        = 0x00000083,
IH_PERF_SEL_BIF_LINE0_FALLING_VF7        = 0x00000084,
IH_PERF_SEL_BIF_LINE0_FALLING_VF8        = 0x00000085,
IH_PERF_SEL_BIF_LINE0_FALLING_VF9        = 0x00000086,
IH_PERF_SEL_BIF_LINE0_FALLING_VF10       = 0x00000087,
IH_PERF_SEL_BIF_LINE0_FALLING_VF11       = 0x00000088,
IH_PERF_SEL_BIF_LINE0_FALLING_VF12       = 0x00000089,
IH_PERF_SEL_BIF_LINE0_FALLING_VF13       = 0x0000008a,
IH_PERF_SEL_BIF_LINE0_FALLING_VF14       = 0x0000008b,
IH_PERF_SEL_BIF_LINE0_FALLING_VF15       = 0x0000008c,
IH_PERF_SEL_CLIENT0_INT                  = 0x0000008d,
IH_PERF_SEL_CLIENT1_INT                  = 0x0000008e,
IH_PERF_SEL_CLIENT2_INT                  = 0x0000008f,
IH_PERF_SEL_CLIENT3_INT                  = 0x00000090,
IH_PERF_SEL_CLIENT4_INT                  = 0x00000091,
IH_PERF_SEL_CLIENT5_INT                  = 0x00000092,
IH_PERF_SEL_CLIENT6_INT                  = 0x00000093,
IH_PERF_SEL_CLIENT7_INT                  = 0x00000094,
IH_PERF_SEL_CLIENT8_INT                  = 0x00000095,
IH_PERF_SEL_CLIENT9_INT                  = 0x00000096,
IH_PERF_SEL_CLIENT10_INT                 = 0x00000097,
IH_PERF_SEL_CLIENT11_INT                 = 0x00000098,
IH_PERF_SEL_CLIENT12_INT                 = 0x00000099,
IH_PERF_SEL_CLIENT13_INT                 = 0x0000009a,
IH_PERF_SEL_CLIENT14_INT                 = 0x0000009b,
IH_PERF_SEL_CLIENT15_INT                 = 0x0000009c,
IH_PERF_SEL_CLIENT16_INT                 = 0x0000009d,
IH_PERF_SEL_CLIENT17_INT                 = 0x0000009e,
IH_PERF_SEL_CLIENT18_INT                 = 0x0000009f,
IH_PERF_SEL_CLIENT19_INT                 = 0x000000a0,
IH_PERF_SEL_CLIENT20_INT                 = 0x000000a1,
IH_PERF_SEL_CLIENT21_INT                 = 0x000000a2,
IH_PERF_SEL_CLIENT22_INT                 = 0x000000a3,
IH_PERF_SEL_CLIENT23_INT                 = 0x000000a4,
IH_PERF_SEL_CLIENT24_INT                 = 0x000000a5,
IH_PERF_SEL_CLIENT25_INT                 = 0x000000a6,
IH_PERF_SEL_CLIENT26_INT                 = 0x000000a7,
IH_PERF_SEL_CLIENT27_INT                 = 0x000000a8,
IH_PERF_SEL_CLIENT28_INT                 = 0x000000a9,
IH_PERF_SEL_CLIENT29_INT                 = 0x000000aa,
IH_PERF_SEL_CLIENT30_INT                 = 0x000000ab,
IH_PERF_SEL_CLIENT31_INT                 = 0x000000ac,
IH_PERF_SEL_RB1_FULL_VF0                 = 0x000000ad,
IH_PERF_SEL_RB1_FULL_VF1                 = 0x000000ae,
IH_PERF_SEL_RB1_FULL_VF2                 = 0x000000af,
IH_PERF_SEL_RB1_FULL_VF3                 = 0x000000b0,
IH_PERF_SEL_RB1_FULL_VF4                 = 0x000000b1,
IH_PERF_SEL_RB1_FULL_VF5                 = 0x000000b2,
IH_PERF_SEL_RB1_FULL_VF6                 = 0x000000b3,
IH_PERF_SEL_RB1_FULL_VF7                 = 0x000000b4,
IH_PERF_SEL_RB1_FULL_VF8                 = 0x000000b5,
IH_PERF_SEL_RB1_FULL_VF9                 = 0x000000b6,
IH_PERF_SEL_RB1_FULL_VF10                = 0x000000b7,
IH_PERF_SEL_RB1_FULL_VF11                = 0x000000b8,
IH_PERF_SEL_RB1_FULL_VF12                = 0x000000b9,
IH_PERF_SEL_RB1_FULL_VF13                = 0x000000ba,
IH_PERF_SEL_RB1_FULL_VF14                = 0x000000bb,
IH_PERF_SEL_RB1_FULL_VF15                = 0x000000bc,
IH_PERF_SEL_RB1_OVERFLOW_VF0             = 0x000000bd,
IH_PERF_SEL_RB1_OVERFLOW_VF1             = 0x000000be,
IH_PERF_SEL_RB1_OVERFLOW_VF2             = 0x000000bf,
IH_PERF_SEL_RB1_OVERFLOW_VF3             = 0x000000c0,
IH_PERF_SEL_RB1_OVERFLOW_VF4             = 0x000000c1,
IH_PERF_SEL_RB1_OVERFLOW_VF5             = 0x000000c2,
IH_PERF_SEL_RB1_OVERFLOW_VF6             = 0x000000c3,
IH_PERF_SEL_RB1_OVERFLOW_VF7             = 0x000000c4,
IH_PERF_SEL_RB1_OVERFLOW_VF8             = 0x000000c5,
IH_PERF_SEL_RB1_OVERFLOW_VF9             = 0x000000c6,
IH_PERF_SEL_RB1_OVERFLOW_VF10            = 0x000000c7,
IH_PERF_SEL_RB1_OVERFLOW_VF11            = 0x000000c8,
IH_PERF_SEL_RB1_OVERFLOW_VF12            = 0x000000c9,
IH_PERF_SEL_RB1_OVERFLOW_VF13            = 0x000000ca,
IH_PERF_SEL_RB1_OVERFLOW_VF14            = 0x000000cb,
IH_PERF_SEL_RB1_OVERFLOW_VF15            = 0x000000cc,
IH_PERF_SEL_RB1_WPTR_WRAP_VF0            = 0x000000cd,
IH_PERF_SEL_RB1_WPTR_WRAP_VF1            = 0x000000ce,
IH_PERF_SEL_RB1_WPTR_WRAP_VF2            = 0x000000cf,
IH_PERF_SEL_RB1_WPTR_WRAP_VF3            = 0x000000d0,
IH_PERF_SEL_RB1_WPTR_WRAP_VF4            = 0x000000d1,
IH_PERF_SEL_RB1_WPTR_WRAP_VF5            = 0x000000d2,
IH_PERF_SEL_RB1_WPTR_WRAP_VF6            = 0x000000d3,
IH_PERF_SEL_RB1_WPTR_WRAP_VF7            = 0x000000d4,
IH_PERF_SEL_RB1_WPTR_WRAP_VF8            = 0x000000d5,
IH_PERF_SEL_RB1_WPTR_WRAP_VF9            = 0x000000d6,
IH_PERF_SEL_RB1_WPTR_WRAP_VF10           = 0x000000d7,
IH_PERF_SEL_RB1_WPTR_WRAP_VF11           = 0x000000d8,
IH_PERF_SEL_RB1_WPTR_WRAP_VF12           = 0x000000d9,
IH_PERF_SEL_RB1_WPTR_WRAP_VF13           = 0x000000da,
IH_PERF_SEL_RB1_WPTR_WRAP_VF14           = 0x000000db,
IH_PERF_SEL_RB1_WPTR_WRAP_VF15           = 0x000000dc,
IH_PERF_SEL_RB1_RPTR_WRAP_VF0            = 0x000000dd,
IH_PERF_SEL_RB1_RPTR_WRAP_VF1            = 0x000000de,
IH_PERF_SEL_RB1_RPTR_WRAP_VF2            = 0x000000df,
IH_PERF_SEL_RB1_RPTR_WRAP_VF3            = 0x000000e0,
IH_PERF_SEL_RB1_RPTR_WRAP_VF4            = 0x000000e1,
IH_PERF_SEL_RB1_RPTR_WRAP_VF5            = 0x000000e2,
IH_PERF_SEL_RB1_RPTR_WRAP_VF6            = 0x000000e3,
IH_PERF_SEL_RB1_RPTR_WRAP_VF7            = 0x000000e4,
IH_PERF_SEL_RB1_RPTR_WRAP_VF8            = 0x000000e5,
IH_PERF_SEL_RB1_RPTR_WRAP_VF9            = 0x000000e6,
IH_PERF_SEL_RB1_RPTR_WRAP_VF10           = 0x000000e7,
IH_PERF_SEL_RB1_RPTR_WRAP_VF11           = 0x000000e8,
IH_PERF_SEL_RB1_RPTR_WRAP_VF12           = 0x000000e9,
IH_PERF_SEL_RB1_RPTR_WRAP_VF13           = 0x000000ea,
IH_PERF_SEL_RB1_RPTR_WRAP_VF14           = 0x000000eb,
IH_PERF_SEL_RB1_RPTR_WRAP_VF15           = 0x000000ec,
IH_PERF_SEL_RB2_FULL_VF0                 = 0x000000ed,
IH_PERF_SEL_RB2_FULL_VF1                 = 0x000000ee,
IH_PERF_SEL_RB2_FULL_VF2                 = 0x000000ef,
IH_PERF_SEL_RB2_FULL_VF3                 = 0x000000f0,
IH_PERF_SEL_RB2_FULL_VF4                 = 0x000000f1,
IH_PERF_SEL_RB2_FULL_VF5                 = 0x000000f2,
IH_PERF_SEL_RB2_FULL_VF6                 = 0x000000f3,
IH_PERF_SEL_RB2_FULL_VF7                 = 0x000000f4,
IH_PERF_SEL_RB2_FULL_VF8                 = 0x000000f5,
IH_PERF_SEL_RB2_FULL_VF9                 = 0x000000f6,
IH_PERF_SEL_RB2_FULL_VF10                = 0x000000f7,
IH_PERF_SEL_RB2_FULL_VF11                = 0x000000f8,
IH_PERF_SEL_RB2_FULL_VF12                = 0x000000f9,
IH_PERF_SEL_RB2_FULL_VF13                = 0x000000fa,
IH_PERF_SEL_RB2_FULL_VF14                = 0x000000fb,
IH_PERF_SEL_RB2_FULL_VF15                = 0x000000fc,
IH_PERF_SEL_RB2_OVERFLOW_VF0             = 0x000000fd,
IH_PERF_SEL_RB2_OVERFLOW_VF1             = 0x000000fe,
IH_PERF_SEL_RB2_OVERFLOW_VF2             = 0x000000ff,
IH_PERF_SEL_RB2_OVERFLOW_VF3             = 0x00000100,
IH_PERF_SEL_RB2_OVERFLOW_VF4             = 0x00000101,
IH_PERF_SEL_RB2_OVERFLOW_VF5             = 0x00000102,
IH_PERF_SEL_RB2_OVERFLOW_VF6             = 0x00000103,
IH_PERF_SEL_RB2_OVERFLOW_VF7             = 0x00000104,
IH_PERF_SEL_RB2_OVERFLOW_VF8             = 0x00000105,
IH_PERF_SEL_RB2_OVERFLOW_VF9             = 0x00000106,
IH_PERF_SEL_RB2_OVERFLOW_VF10            = 0x00000107,
IH_PERF_SEL_RB2_OVERFLOW_VF11            = 0x00000108,
IH_PERF_SEL_RB2_OVERFLOW_VF12            = 0x00000109,
IH_PERF_SEL_RB2_OVERFLOW_VF13            = 0x0000010a,
IH_PERF_SEL_RB2_OVERFLOW_VF14            = 0x0000010b,
IH_PERF_SEL_RB2_OVERFLOW_VF15            = 0x0000010c,
IH_PERF_SEL_RB2_WPTR_WRAP_VF0            = 0x0000010d,
IH_PERF_SEL_RB2_WPTR_WRAP_VF1            = 0x0000010e,
IH_PERF_SEL_RB2_WPTR_WRAP_VF2            = 0x0000010f,
IH_PERF_SEL_RB2_WPTR_WRAP_VF3            = 0x00000110,
IH_PERF_SEL_RB2_WPTR_WRAP_VF4            = 0x00000111,
IH_PERF_SEL_RB2_WPTR_WRAP_VF5            = 0x00000112,
IH_PERF_SEL_RB2_WPTR_WRAP_VF6            = 0x00000113,
IH_PERF_SEL_RB2_WPTR_WRAP_VF7            = 0x00000114,
IH_PERF_SEL_RB2_WPTR_WRAP_VF8            = 0x00000115,
IH_PERF_SEL_RB2_WPTR_WRAP_VF9            = 0x00000116,
IH_PERF_SEL_RB2_WPTR_WRAP_VF10           = 0x00000117,
IH_PERF_SEL_RB2_WPTR_WRAP_VF11           = 0x00000118,
IH_PERF_SEL_RB2_WPTR_WRAP_VF12           = 0x00000119,
IH_PERF_SEL_RB2_WPTR_WRAP_VF13           = 0x0000011a,
IH_PERF_SEL_RB2_WPTR_WRAP_VF14           = 0x0000011b,
IH_PERF_SEL_RB2_WPTR_WRAP_VF15           = 0x0000011c,
IH_PERF_SEL_RB2_RPTR_WRAP_VF0            = 0x0000011d,
IH_PERF_SEL_RB2_RPTR_WRAP_VF1            = 0x0000011e,
IH_PERF_SEL_RB2_RPTR_WRAP_VF2            = 0x0000011f,
IH_PERF_SEL_RB2_RPTR_WRAP_VF3            = 0x00000120,
IH_PERF_SEL_RB2_RPTR_WRAP_VF4            = 0x00000121,
IH_PERF_SEL_RB2_RPTR_WRAP_VF5            = 0x00000122,
IH_PERF_SEL_RB2_RPTR_WRAP_VF6            = 0x00000123,
IH_PERF_SEL_RB2_RPTR_WRAP_VF7            = 0x00000124,
IH_PERF_SEL_RB2_RPTR_WRAP_VF8            = 0x00000125,
IH_PERF_SEL_RB2_RPTR_WRAP_VF9            = 0x00000126,
IH_PERF_SEL_RB2_RPTR_WRAP_VF10           = 0x00000127,
IH_PERF_SEL_RB2_RPTR_WRAP_VF11           = 0x00000128,
IH_PERF_SEL_RB2_RPTR_WRAP_VF12           = 0x00000129,
IH_PERF_SEL_RB2_RPTR_WRAP_VF13           = 0x0000012a,
IH_PERF_SEL_RB2_RPTR_WRAP_VF14           = 0x0000012b,
IH_PERF_SEL_RB2_RPTR_WRAP_VF15           = 0x0000012c,
IH_PERF_SEL_RB0_FULL_DRAIN_DROP          = 0x0000012d,
IH_PERF_SEL_RB0_FULL_DRAIN_DROP_VF0      = 0x0000012e,
IH_PERF_SEL_RB0_FULL_DRAIN_DROP_VF1      = 0x0000012f,
IH_PERF_SEL_RB0_FULL_DRAIN_DROP_VF2      = 0x00000130,
IH_PERF_SEL_RB0_FULL_DRAIN_DROP_VF3      = 0x00000131,
IH_PERF_SEL_RB0_FULL_DRAIN_DROP_VF4      = 0x00000132,
IH_PERF_SEL_RB0_FULL_DRAIN_DROP_VF5      = 0x00000133,
IH_PERF_SEL_RB0_FULL_DRAIN_DROP_VF6      = 0x00000134,
IH_PERF_SEL_RB0_FULL_DRAIN_DROP_VF7      = 0x00000135,
IH_PERF_SEL_RB0_FULL_DRAIN_DROP_VF8      = 0x00000136,
IH_PERF_SEL_RB0_FULL_DRAIN_DROP_VF9      = 0x00000137,
IH_PERF_SEL_RB0_FULL_DRAIN_DROP_VF10     = 0x00000138,
IH_PERF_SEL_RB0_FULL_DRAIN_DROP_VF11     = 0x00000139,
IH_PERF_SEL_RB0_FULL_DRAIN_DROP_VF12     = 0x0000013a,
IH_PERF_SEL_RB0_FULL_DRAIN_DROP_VF13     = 0x0000013b,
IH_PERF_SEL_RB0_FULL_DRAIN_DROP_VF14     = 0x0000013c,
IH_PERF_SEL_RB0_FULL_DRAIN_DROP_VF15     = 0x0000013d,
IH_PERF_SEL_RB1_FULL_DRAIN_DROP          = 0x0000013e,
IH_PERF_SEL_RB1_FULL_DRAIN_DROP_VF0      = 0x0000013f,
IH_PERF_SEL_RB1_FULL_DRAIN_DROP_VF1      = 0x00000140,
IH_PERF_SEL_RB1_FULL_DRAIN_DROP_VF2      = 0x00000141,
IH_PERF_SEL_RB1_FULL_DRAIN_DROP_VF3      = 0x00000142,
IH_PERF_SEL_RB1_FULL_DRAIN_DROP_VF4      = 0x00000143,
IH_PERF_SEL_RB1_FULL_DRAIN_DROP_VF5      = 0x00000144,
IH_PERF_SEL_RB1_FULL_DRAIN_DROP_VF6      = 0x00000145,
IH_PERF_SEL_RB1_FULL_DRAIN_DROP_VF7      = 0x00000146,
IH_PERF_SEL_RB1_FULL_DRAIN_DROP_VF8      = 0x00000147,
IH_PERF_SEL_RB1_FULL_DRAIN_DROP_VF9      = 0x00000148,
IH_PERF_SEL_RB1_FULL_DRAIN_DROP_VF10     = 0x00000149,
IH_PERF_SEL_RB1_FULL_DRAIN_DROP_VF11     = 0x0000014a,
IH_PERF_SEL_RB1_FULL_DRAIN_DROP_VF12     = 0x0000014b,
IH_PERF_SEL_RB1_FULL_DRAIN_DROP_VF13     = 0x0000014c,
IH_PERF_SEL_RB1_FULL_DRAIN_DROP_VF14     = 0x0000014d,
IH_PERF_SEL_RB1_FULL_DRAIN_DROP_VF15     = 0x0000014e,
IH_PERF_SEL_RB2_FULL_DRAIN_DROP          = 0x0000014f,
IH_PERF_SEL_RB2_FULL_DRAIN_DROP_VF0      = 0x00000150,
IH_PERF_SEL_RB2_FULL_DRAIN_DROP_VF1      = 0x00000151,
IH_PERF_SEL_RB2_FULL_DRAIN_DROP_VF2      = 0x00000152,
IH_PERF_SEL_RB2_FULL_DRAIN_DROP_VF3      = 0x00000153,
IH_PERF_SEL_RB2_FULL_DRAIN_DROP_VF4      = 0x00000154,
IH_PERF_SEL_RB2_FULL_DRAIN_DROP_VF5      = 0x00000155,
IH_PERF_SEL_RB2_FULL_DRAIN_DROP_VF6      = 0x00000156,
IH_PERF_SEL_RB2_FULL_DRAIN_DROP_VF7      = 0x00000157,
IH_PERF_SEL_RB2_FULL_DRAIN_DROP_VF8      = 0x00000158,
IH_PERF_SEL_RB2_FULL_DRAIN_DROP_VF9      = 0x00000159,
IH_PERF_SEL_RB2_FULL_DRAIN_DROP_VF10     = 0x0000015a,
IH_PERF_SEL_RB2_FULL_DRAIN_DROP_VF11     = 0x0000015b,
IH_PERF_SEL_RB2_FULL_DRAIN_DROP_VF12     = 0x0000015c,
IH_PERF_SEL_RB2_FULL_DRAIN_DROP_VF13     = 0x0000015d,
IH_PERF_SEL_RB2_FULL_DRAIN_DROP_VF14     = 0x0000015e,
IH_PERF_SEL_RB2_FULL_DRAIN_DROP_VF15     = 0x0000015f,
IH_PERF_SEL_RB0_LOAD_RPTR                = 0x00000160,
IH_PERF_SEL_RB0_LOAD_RPTR_VF0            = 0x00000161,
IH_PERF_SEL_RB0_LOAD_RPTR_VF1            = 0x00000162,
IH_PERF_SEL_RB0_LOAD_RPTR_VF2            = 0x00000163,
IH_PERF_SEL_RB0_LOAD_RPTR_VF3            = 0x00000164,
IH_PERF_SEL_RB0_LOAD_RPTR_VF4            = 0x00000165,
IH_PERF_SEL_RB0_LOAD_RPTR_VF5            = 0x00000166,
IH_PERF_SEL_RB0_LOAD_RPTR_VF6            = 0x00000167,
IH_PERF_SEL_RB0_LOAD_RPTR_VF7            = 0x00000168,
IH_PERF_SEL_RB0_LOAD_RPTR_VF8            = 0x00000169,
IH_PERF_SEL_RB0_LOAD_RPTR_VF9            = 0x0000016a,
IH_PERF_SEL_RB0_LOAD_RPTR_VF10           = 0x0000016b,
IH_PERF_SEL_RB0_LOAD_RPTR_VF11           = 0x0000016c,
IH_PERF_SEL_RB0_LOAD_RPTR_VF12           = 0x0000016d,
IH_PERF_SEL_RB0_LOAD_RPTR_VF13           = 0x0000016e,
IH_PERF_SEL_RB0_LOAD_RPTR_VF14           = 0x0000016f,
IH_PERF_SEL_RB0_LOAD_RPTR_VF15           = 0x00000170,
IH_PERF_SEL_RB1_LOAD_RPTR                = 0x00000171,
IH_PERF_SEL_RB1_LOAD_RPTR_VF0            = 0x00000172,
IH_PERF_SEL_RB1_LOAD_RPTR_VF1            = 0x00000173,
IH_PERF_SEL_RB1_LOAD_RPTR_VF2            = 0x00000174,
IH_PERF_SEL_RB1_LOAD_RPTR_VF3            = 0x00000175,
IH_PERF_SEL_RB1_LOAD_RPTR_VF4            = 0x00000176,
IH_PERF_SEL_RB1_LOAD_RPTR_VF5            = 0x00000177,
IH_PERF_SEL_RB1_LOAD_RPTR_VF6            = 0x00000178,
IH_PERF_SEL_RB1_LOAD_RPTR_VF7            = 0x00000179,
IH_PERF_SEL_RB1_LOAD_RPTR_VF8            = 0x0000017a,
IH_PERF_SEL_RB1_LOAD_RPTR_VF9            = 0x0000017b,
IH_PERF_SEL_RB1_LOAD_RPTR_VF10           = 0x0000017c,
IH_PERF_SEL_RB1_LOAD_RPTR_VF11           = 0x0000017d,
IH_PERF_SEL_RB1_LOAD_RPTR_VF12           = 0x0000017e,
IH_PERF_SEL_RB1_LOAD_RPTR_VF13           = 0x0000017f,
IH_PERF_SEL_RB1_LOAD_RPTR_VF14           = 0x00000180,
IH_PERF_SEL_RB1_LOAD_RPTR_VF15           = 0x00000181,
IH_PERF_SEL_RB2_LOAD_RPTR                = 0x00000182,
IH_PERF_SEL_RB2_LOAD_RPTR_VF0            = 0x00000183,
IH_PERF_SEL_RB2_LOAD_RPTR_VF1            = 0x00000184,
IH_PERF_SEL_RB2_LOAD_RPTR_VF2            = 0x00000185,
IH_PERF_SEL_RB2_LOAD_RPTR_VF3            = 0x00000186,
IH_PERF_SEL_RB2_LOAD_RPTR_VF4            = 0x00000187,
IH_PERF_SEL_RB2_LOAD_RPTR_VF5            = 0x00000188,
IH_PERF_SEL_RB2_LOAD_RPTR_VF6            = 0x00000189,
IH_PERF_SEL_RB2_LOAD_RPTR_VF7            = 0x0000018a,
IH_PERF_SEL_RB2_LOAD_RPTR_VF8            = 0x0000018b,
IH_PERF_SEL_RB2_LOAD_RPTR_VF9            = 0x0000018c,
IH_PERF_SEL_RB2_LOAD_RPTR_VF10           = 0x0000018d,
IH_PERF_SEL_RB2_LOAD_RPTR_VF11           = 0x0000018e,
IH_PERF_SEL_RB2_LOAD_RPTR_VF12           = 0x0000018f,
IH_PERF_SEL_RB2_LOAD_RPTR_VF13           = 0x00000190,
IH_PERF_SEL_RB2_LOAD_RPTR_VF14           = 0x00000191,
IH_PERF_SEL_RB2_LOAD_RPTR_VF15           = 0x00000192,
} IH_PERF_SEL;

/*
 * IH_RING_ID enum
 */

typedef enum IH_RING_ID {
IH_RING_ID_INTERRUPT                     = 0x00000000,
IH_RING_ID_REQUEST                       = 0x00000001,
IH_RING_ID_TRANSLATION                   = 0x00000002,
IH_RING_ID_RESERVED                      = 0x00000003,
} IH_RING_ID;

/*
 * IH_VF_RB_SELECT enum
 */

typedef enum IH_VF_RB_SELECT {
IH_VF_RB_SELECT_CLIENT_FCN_ID            = 0x00000000,
IH_VF_RB_SELECT_IH_FCN_ID                = 0x00000001,
IH_VF_RB_SELECT_PF                       = 0x00000002,
IH_VF_RB_SELECT_RESERVED                 = 0x00000003,
} IH_VF_RB_SELECT;

/*******************************************************
 * SEM Enums
 *******************************************************/

/*
 * SEM_PERF_SEL enum
 */

typedef enum SEM_PERF_SEL {
SEM_PERF_SEL_CYCLE                       = 0x00000000,
SEM_PERF_SEL_IDLE                        = 0x00000001,
SEM_PERF_SEL_SDMA0_REQ_SIGNAL            = 0x00000002,
SEM_PERF_SEL_SDMA1_REQ_SIGNAL            = 0x00000003,
SEM_PERF_SEL_SDMA2_REQ_SIGNAL            = 0x00000004,
SEM_PERF_SEL_SDMA3_REQ_SIGNAL            = 0x00000005,
SEM_PERF_SEL_UVD_REQ_SIGNAL              = 0x00000006,
SEM_PERF_SEL_UVD1_REQ_SIGNAL             = 0x00000007,
SEM_PERF_SEL_VCE0_REQ_SIGNAL             = 0x00000008,
SEM_PERF_SEL_ACP_REQ_SIGNAL              = 0x00000009,
SEM_PERF_SEL_ISP_REQ_SIGNAL              = 0x0000000a,
SEM_PERF_SEL_VCE1_REQ_SIGNAL             = 0x0000000b,
SEM_PERF_SEL_VP8_REQ_SIGNAL              = 0x0000000c,
SEM_PERF_SEL_CPG_E0_REQ_SIGNAL           = 0x0000000d,
SEM_PERF_SEL_CPG_E1_REQ_SIGNAL           = 0x0000000e,
SEM_PERF_SEL_CPC1_IMME_E0_REQ_SIGNAL     = 0x0000000f,
SEM_PERF_SEL_CPC1_IMME_E1_REQ_SIGNAL     = 0x00000010,
SEM_PERF_SEL_CPC1_IMME_E2_REQ_SIGNAL     = 0x00000011,
SEM_PERF_SEL_CPC1_IMME_E3_REQ_SIGNAL     = 0x00000012,
SEM_PERF_SEL_CPC2_IMME_E0_REQ_SIGNAL     = 0x00000013,
SEM_PERF_SEL_CPC2_IMME_E1_REQ_SIGNAL     = 0x00000014,
SEM_PERF_SEL_CPC2_IMME_E2_REQ_SIGNAL     = 0x00000015,
SEM_PERF_SEL_CPC2_IMME_E3_REQ_SIGNAL     = 0x00000016,
SEM_PERF_SEL_SDMA0_REQ_WAIT              = 0x00000017,
SEM_PERF_SEL_SDMA1_REQ_WAIT              = 0x00000018,
SEM_PERF_SEL_SDMA2_REQ_WAIT              = 0x00000019,
SEM_PERF_SEL_SDMA3_REQ_WAIT              = 0x0000001a,
SEM_PERF_SEL_UVD_REQ_WAIT                = 0x0000001b,
SEM_PERF_SEL_UVD1_REQ_WAIT               = 0x0000001c,
SEM_PERF_SEL_VCE0_REQ_WAIT               = 0x0000001d,
SEM_PERF_SEL_ACP_REQ_WAIT                = 0x0000001e,
SEM_PERF_SEL_ISP_REQ_WAIT                = 0x0000001f,
SEM_PERF_SEL_VCE1_REQ_WAIT               = 0x00000020,
SEM_PERF_SEL_VP8_REQ_WAIT                = 0x00000021,
SEM_PERF_SEL_CPG_E0_REQ_WAIT             = 0x00000022,
SEM_PERF_SEL_CPG_E1_REQ_WAIT             = 0x00000023,
SEM_PERF_SEL_CPC1_IMME_E0_REQ_WAIT       = 0x00000024,
SEM_PERF_SEL_CPC1_IMME_E1_REQ_WAIT       = 0x00000025,
SEM_PERF_SEL_CPC1_IMME_E2_REQ_WAIT       = 0x00000026,
SEM_PERF_SEL_CPC1_IMME_E3_REQ_WAIT       = 0x00000027,
SEM_PERF_SEL_CPC2_IMME_E0_REQ_WAIT       = 0x00000028,
SEM_PERF_SEL_CPC2_IMME_E1_REQ_WAIT       = 0x00000029,
SEM_PERF_SEL_CPC2_IMME_E2_REQ_WAIT       = 0x0000002a,
SEM_PERF_SEL_CPC2_IMME_E3_REQ_WAIT       = 0x0000002b,
SEM_PERF_SEL_CPC1_OFFL_E0_REQ_WAIT       = 0x0000002c,
SEM_PERF_SEL_CPC1_OFFL_E1_REQ_WAIT       = 0x0000002d,
SEM_PERF_SEL_CPC1_OFFL_E2_REQ_WAIT       = 0x0000002e,
SEM_PERF_SEL_CPC1_OFFL_E3_REQ_WAIT       = 0x0000002f,
SEM_PERF_SEL_CPC1_OFFL_E4_REQ_WAIT       = 0x00000030,
SEM_PERF_SEL_CPC1_OFFL_E5_REQ_WAIT       = 0x00000031,
SEM_PERF_SEL_CPC1_OFFL_E6_REQ_WAIT       = 0x00000032,
SEM_PERF_SEL_CPC1_OFFL_E7_REQ_WAIT       = 0x00000033,
SEM_PERF_SEL_CPC1_OFFL_E8_REQ_WAIT       = 0x00000034,
SEM_PERF_SEL_CPC1_OFFL_E9_REQ_WAIT       = 0x00000035,
SEM_PERF_SEL_CPC1_OFFL_E10_REQ_WAIT      = 0x00000036,
SEM_PERF_SEL_CPC1_OFFL_E11_REQ_WAIT      = 0x00000037,
SEM_PERF_SEL_CPC1_OFFL_E12_REQ_WAIT      = 0x00000038,
SEM_PERF_SEL_CPC1_OFFL_E13_REQ_WAIT      = 0x00000039,
SEM_PERF_SEL_CPC1_OFFL_E14_REQ_WAIT      = 0x0000003a,
SEM_PERF_SEL_CPC1_OFFL_E15_REQ_WAIT      = 0x0000003b,
SEM_PERF_SEL_CPC1_OFFL_E16_REQ_WAIT      = 0x0000003c,
SEM_PERF_SEL_CPC1_OFFL_E17_REQ_WAIT      = 0x0000003d,
SEM_PERF_SEL_CPC1_OFFL_E18_REQ_WAIT      = 0x0000003e,
SEM_PERF_SEL_CPC1_OFFL_E19_REQ_WAIT      = 0x0000003f,
SEM_PERF_SEL_CPC1_OFFL_E20_REQ_WAIT      = 0x00000040,
SEM_PERF_SEL_CPC1_OFFL_E21_REQ_WAIT      = 0x00000041,
SEM_PERF_SEL_CPC1_OFFL_E22_REQ_WAIT      = 0x00000042,
SEM_PERF_SEL_CPC1_OFFL_E23_REQ_WAIT      = 0x00000043,
SEM_PERF_SEL_CPC1_OFFL_E24_REQ_WAIT      = 0x00000044,
SEM_PERF_SEL_CPC1_OFFL_E25_REQ_WAIT      = 0x00000045,
SEM_PERF_SEL_CPC1_OFFL_E26_REQ_WAIT      = 0x00000046,
SEM_PERF_SEL_CPC1_OFFL_E27_REQ_WAIT      = 0x00000047,
SEM_PERF_SEL_CPC1_OFFL_E28_REQ_WAIT      = 0x00000048,
SEM_PERF_SEL_CPC1_OFFL_E29_REQ_WAIT      = 0x00000049,
SEM_PERF_SEL_CPC1_OFFL_E30_REQ_WAIT      = 0x0000004a,
SEM_PERF_SEL_CPC1_OFFL_E31_REQ_WAIT      = 0x0000004b,
SEM_PERF_SEL_CPC2_OFFL_E0_REQ_WAIT       = 0x0000004c,
SEM_PERF_SEL_CPC2_OFFL_E1_REQ_WAIT       = 0x0000004d,
SEM_PERF_SEL_CPC2_OFFL_E2_REQ_WAIT       = 0x0000004e,
SEM_PERF_SEL_CPC2_OFFL_E3_REQ_WAIT       = 0x0000004f,
SEM_PERF_SEL_CPC2_OFFL_E4_REQ_WAIT       = 0x00000050,
SEM_PERF_SEL_CPC2_OFFL_E5_REQ_WAIT       = 0x00000051,
SEM_PERF_SEL_CPC2_OFFL_E6_REQ_WAIT       = 0x00000052,
SEM_PERF_SEL_CPC2_OFFL_E7_REQ_WAIT       = 0x00000053,
SEM_PERF_SEL_CPC2_OFFL_E8_REQ_WAIT       = 0x00000054,
SEM_PERF_SEL_CPC2_OFFL_E9_REQ_WAIT       = 0x00000055,
SEM_PERF_SEL_CPC2_OFFL_E10_REQ_WAIT      = 0x00000056,
SEM_PERF_SEL_CPC2_OFFL_E11_REQ_WAIT      = 0x00000057,
SEM_PERF_SEL_CPC2_OFFL_E12_REQ_WAIT      = 0x00000058,
SEM_PERF_SEL_CPC2_OFFL_E13_REQ_WAIT      = 0x00000059,
SEM_PERF_SEL_CPC2_OFFL_E14_REQ_WAIT      = 0x0000005a,
SEM_PERF_SEL_CPC2_OFFL_E15_REQ_WAIT      = 0x0000005b,
SEM_PERF_SEL_CPC2_OFFL_E16_REQ_WAIT      = 0x0000005c,
SEM_PERF_SEL_CPC2_OFFL_E17_REQ_WAIT      = 0x0000005d,
SEM_PERF_SEL_CPC2_OFFL_E18_REQ_WAIT      = 0x0000005e,
SEM_PERF_SEL_CPC2_OFFL_E19_REQ_WAIT      = 0x0000005f,
SEM_PERF_SEL_CPC2_OFFL_E20_REQ_WAIT      = 0x00000060,
SEM_PERF_SEL_CPC2_OFFL_E21_REQ_WAIT      = 0x00000061,
SEM_PERF_SEL_CPC2_OFFL_E22_REQ_WAIT      = 0x00000062,
SEM_PERF_SEL_CPC2_OFFL_E23_REQ_WAIT      = 0x00000063,
SEM_PERF_SEL_CPC2_OFFL_E24_REQ_WAIT      = 0x00000064,
SEM_PERF_SEL_CPC2_OFFL_E25_REQ_WAIT      = 0x00000065,
SEM_PERF_SEL_CPC2_OFFL_E26_REQ_WAIT      = 0x00000066,
SEM_PERF_SEL_CPC2_OFFL_E27_REQ_WAIT      = 0x00000067,
SEM_PERF_SEL_CPC2_OFFL_E28_REQ_WAIT      = 0x00000068,
SEM_PERF_SEL_CPC2_OFFL_E29_REQ_WAIT      = 0x00000069,
SEM_PERF_SEL_CPC2_OFFL_E30_REQ_WAIT      = 0x0000006a,
SEM_PERF_SEL_CPC2_OFFL_E31_REQ_WAIT      = 0x0000006b,
SEM_PERF_SEL_CPC1_OFFL_E0_POLL_WAIT      = 0x0000006c,
SEM_PERF_SEL_CPC1_OFFL_E1_POLL_WAIT      = 0x0000006d,
SEM_PERF_SEL_CPC1_OFFL_E2_POLL_WAIT      = 0x0000006e,
SEM_PERF_SEL_CPC1_OFFL_E3_POLL_WAIT      = 0x0000006f,
SEM_PERF_SEL_CPC1_OFFL_E4_POLL_WAIT      = 0x00000070,
SEM_PERF_SEL_CPC1_OFFL_E5_POLL_WAIT      = 0x00000071,
SEM_PERF_SEL_CPC1_OFFL_E6_POLL_WAIT      = 0x00000072,
SEM_PERF_SEL_CPC1_OFFL_E7_POLL_WAIT      = 0x00000073,
SEM_PERF_SEL_CPC1_OFFL_E8_POLL_WAIT      = 0x00000074,
SEM_PERF_SEL_CPC1_OFFL_E9_POLL_WAIT      = 0x00000075,
SEM_PERF_SEL_CPC1_OFFL_E10_POLL_WAIT     = 0x00000076,
SEM_PERF_SEL_CPC1_OFFL_E11_POLL_WAIT     = 0x00000077,
SEM_PERF_SEL_CPC1_OFFL_E12_POLL_WAIT     = 0x00000078,
SEM_PERF_SEL_CPC1_OFFL_E13_POLL_WAIT     = 0x00000079,
SEM_PERF_SEL_CPC1_OFFL_E14_POLL_WAIT     = 0x0000007a,
SEM_PERF_SEL_CPC1_OFFL_E15_POLL_WAIT     = 0x0000007b,
SEM_PERF_SEL_CPC1_OFFL_E16_POLL_WAIT     = 0x0000007c,
SEM_PERF_SEL_CPC1_OFFL_E17_POLL_WAIT     = 0x0000007d,
SEM_PERF_SEL_CPC1_OFFL_E18_POLL_WAIT     = 0x0000007e,
SEM_PERF_SEL_CPC1_OFFL_E19_POLL_WAIT     = 0x0000007f,
SEM_PERF_SEL_CPC1_OFFL_E20_POLL_WAIT     = 0x00000080,
SEM_PERF_SEL_CPC1_OFFL_E21_POLL_WAIT     = 0x00000081,
SEM_PERF_SEL_CPC1_OFFL_E22_POLL_WAIT     = 0x00000082,
SEM_PERF_SEL_CPC1_OFFL_E23_POLL_WAIT     = 0x00000083,
SEM_PERF_SEL_CPC1_OFFL_E24_POLL_WAIT     = 0x00000084,
SEM_PERF_SEL_CPC1_OFFL_E25_POLL_WAIT     = 0x00000085,
SEM_PERF_SEL_CPC1_OFFL_E26_POLL_WAIT     = 0x00000086,
SEM_PERF_SEL_CPC1_OFFL_E27_POLL_WAIT     = 0x00000087,
SEM_PERF_SEL_CPC1_OFFL_E28_POLL_WAIT     = 0x00000088,
SEM_PERF_SEL_CPC1_OFFL_E29_POLL_WAIT     = 0x00000089,
SEM_PERF_SEL_CPC1_OFFL_E30_POLL_WAIT     = 0x0000008a,
SEM_PERF_SEL_CPC1_OFFL_E31_POLL_WAIT     = 0x0000008b,
SEM_PERF_SEL_CPC2_OFFL_E0_POLL_WAIT      = 0x0000008c,
SEM_PERF_SEL_CPC2_OFFL_E1_POLL_WAIT      = 0x0000008d,
SEM_PERF_SEL_CPC2_OFFL_E2_POLL_WAIT      = 0x0000008e,
SEM_PERF_SEL_CPC2_OFFL_E3_POLL_WAIT      = 0x0000008f,
SEM_PERF_SEL_CPC2_OFFL_E4_POLL_WAIT      = 0x00000090,
SEM_PERF_SEL_CPC2_OFFL_E5_POLL_WAIT      = 0x00000091,
SEM_PERF_SEL_CPC2_OFFL_E6_POLL_WAIT      = 0x00000092,
SEM_PERF_SEL_CPC2_OFFL_E7_POLL_WAIT      = 0x00000093,
SEM_PERF_SEL_CPC2_OFFL_E8_POLL_WAIT      = 0x00000094,
SEM_PERF_SEL_CPC2_OFFL_E9_POLL_WAIT      = 0x00000095,
SEM_PERF_SEL_CPC2_OFFL_E10_POLL_WAIT     = 0x00000096,
SEM_PERF_SEL_CPC2_OFFL_E11_POLL_WAIT     = 0x00000097,
SEM_PERF_SEL_CPC2_OFFL_E12_POLL_WAIT     = 0x00000098,
SEM_PERF_SEL_CPC2_OFFL_E13_POLL_WAIT     = 0x00000099,
SEM_PERF_SEL_CPC2_OFFL_E14_POLL_WAIT     = 0x0000009a,
SEM_PERF_SEL_CPC2_OFFL_E15_POLL_WAIT     = 0x0000009b,
SEM_PERF_SEL_CPC2_OFFL_E16_POLL_WAIT     = 0x0000009c,
SEM_PERF_SEL_CPC2_OFFL_E17_POLL_WAIT     = 0x0000009d,
SEM_PERF_SEL_CPC2_OFFL_E18_POLL_WAIT     = 0x0000009e,
SEM_PERF_SEL_CPC2_OFFL_E19_POLL_WAIT     = 0x0000009f,
SEM_PERF_SEL_CPC2_OFFL_E20_POLL_WAIT     = 0x000000a0,
SEM_PERF_SEL_CPC2_OFFL_E21_POLL_WAIT     = 0x000000a1,
SEM_PERF_SEL_CPC2_OFFL_E22_POLL_WAIT     = 0x000000a2,
SEM_PERF_SEL_CPC2_OFFL_E23_POLL_WAIT     = 0x000000a3,
SEM_PERF_SEL_CPC2_OFFL_E24_POLL_WAIT     = 0x000000a4,
SEM_PERF_SEL_CPC2_OFFL_E25_POLL_WAIT     = 0x000000a5,
SEM_PERF_SEL_CPC2_OFFL_E26_POLL_WAIT     = 0x000000a6,
SEM_PERF_SEL_CPC2_OFFL_E27_POLL_WAIT     = 0x000000a7,
SEM_PERF_SEL_CPC2_OFFL_E28_POLL_WAIT     = 0x000000a8,
SEM_PERF_SEL_CPC2_OFFL_E29_POLL_WAIT     = 0x000000a9,
SEM_PERF_SEL_CPC2_OFFL_E30_POLL_WAIT     = 0x000000aa,
SEM_PERF_SEL_CPC2_OFFL_E31_POLL_WAIT     = 0x000000ab,
SEM_PERF_SEL_MC_RD_REQ                   = 0x000000ac,
SEM_PERF_SEL_MC_RD_RET                   = 0x000000ad,
SEM_PERF_SEL_MC_WR_REQ                   = 0x000000ae,
SEM_PERF_SEL_MC_WR_RET                   = 0x000000af,
SEM_PERF_SEL_ATC_REQ                     = 0x000000b0,
SEM_PERF_SEL_ATC_RET                     = 0x000000b1,
SEM_PERF_SEL_ATC_XNACK                   = 0x000000b2,
SEM_PERF_SEL_ATC_INVALIDATION            = 0x000000b3,
SEM_PERF_SEL_ATC_VM_INVALIDATION         = 0x000000b4,
} SEM_PERF_SEL;

/*******************************************************
 * LSDMA Enums
 *******************************************************/

/*
 * LSDMA_PERF_SEL enum
 */

typedef enum LSDMA_PERF_SEL {
LSDMA_PERF_SEL_CYCLE                     = 0x00000000,
LSDMA_PERF_SEL_IDLE                      = 0x00000001,
LSDMA_PERF_SEL_REG_IDLE                  = 0x00000002,
LSDMA_PERF_SEL_RB_EMPTY                  = 0x00000003,
LSDMA_PERF_SEL_RB_FULL                   = 0x00000004,
LSDMA_PERF_SEL_RB_WPTR_WRAP              = 0x00000005,
LSDMA_PERF_SEL_RB_RPTR_WRAP              = 0x00000006,
LSDMA_PERF_SEL_RB_WPTR_POLL_READ         = 0x00000007,
LSDMA_PERF_SEL_RB_RPTR_WB                = 0x00000008,
LSDMA_PERF_SEL_RB_CMD_IDLE               = 0x00000009,
LSDMA_PERF_SEL_RB_CMD_FULL               = 0x0000000a,
LSDMA_PERF_SEL_IB_CMD_IDLE               = 0x0000000b,
LSDMA_PERF_SEL_IB_CMD_FULL               = 0x0000000c,
LSDMA_PERF_SEL_EX_IDLE                   = 0x0000000d,
LSDMA_PERF_SEL_SRBM_REG_SEND             = 0x0000000e,
LSDMA_PERF_SEL_EX_IDLE_POLL_TIMER_EXPIRE = 0x0000000f,
LSDMA_PERF_SEL_MC_WR_IDLE                = 0x00000010,
LSDMA_PERF_SEL_MC_WR_COUNT               = 0x00000011,
LSDMA_PERF_SEL_MC_RD_IDLE                = 0x00000012,
LSDMA_PERF_SEL_MC_RD_COUNT               = 0x00000013,
LSDMA_PERF_SEL_MC_RD_RET_STALL           = 0x00000014,
LSDMA_PERF_SEL_MC_RD_NO_POLL_IDLE        = 0x00000015,
LSDMA_PERF_SEL_SEM_IDLE                  = 0x00000018,
LSDMA_PERF_SEL_SEM_REQ_STALL             = 0x00000019,
LSDMA_PERF_SEL_SEM_REQ_COUNT             = 0x0000001a,
LSDMA_PERF_SEL_SEM_RESP_INCOMPLETE       = 0x0000001b,
LSDMA_PERF_SEL_SEM_RESP_FAIL             = 0x0000001c,
LSDMA_PERF_SEL_SEM_RESP_PASS             = 0x0000001d,
LSDMA_PERF_SEL_INT_IDLE                  = 0x0000001e,
LSDMA_PERF_SEL_INT_REQ_STALL             = 0x0000001f,
LSDMA_PERF_SEL_INT_REQ_COUNT             = 0x00000020,
LSDMA_PERF_SEL_INT_RESP_ACCEPTED         = 0x00000021,
LSDMA_PERF_SEL_INT_RESP_RETRY            = 0x00000022,
LSDMA_PERF_SEL_NUM_PACKET                = 0x00000023,
LSDMA_PERF_SEL_CE_WREQ_IDLE              = 0x00000025,
LSDMA_PERF_SEL_CE_WR_IDLE                = 0x00000026,
LSDMA_PERF_SEL_CE_SPLIT_IDLE             = 0x00000027,
LSDMA_PERF_SEL_CE_RREQ_IDLE              = 0x00000028,
LSDMA_PERF_SEL_CE_OUT_IDLE               = 0x00000029,
LSDMA_PERF_SEL_CE_IN_IDLE                = 0x0000002a,
LSDMA_PERF_SEL_CE_DST_IDLE               = 0x0000002b,
LSDMA_PERF_SEL_CE_AFIFO_FULL             = 0x0000002e,
LSDMA_PERF_SEL_CE_INFO_FULL              = 0x00000031,
LSDMA_PERF_SEL_CE_INFO1_FULL             = 0x00000032,
LSDMA_PERF_SEL_CE_RD_STALL               = 0x00000033,
LSDMA_PERF_SEL_CE_WR_STALL               = 0x00000034,
LSDMA_PERF_SEL_GFX_SELECT                = 0x00000035,
LSDMA_PERF_SEL_RLC0_SELECT               = 0x00000036,
LSDMA_PERF_SEL_RLC1_SELECT               = 0x00000037,
LSDMA_PERF_SEL_PAGE_SELECT               = 0x00000038,
LSDMA_PERF_SEL_CTX_CHANGE                = 0x00000039,
LSDMA_PERF_SEL_CTX_CHANGE_EXPIRED        = 0x0000003a,
LSDMA_PERF_SEL_CTX_CHANGE_EXCEPTION      = 0x0000003b,
LSDMA_PERF_SEL_DOORBELL                  = 0x0000003c,
LSDMA_PERF_SEL_RD_BA_RTR                 = 0x0000003d,
LSDMA_PERF_SEL_WR_BA_RTR                 = 0x0000003e,
LSDMA_PERF_SEL_F32_L1_WR_VLD             = 0x0000003f,
LSDMA_PERF_SEL_CE_L1_WR_VLD              = 0x00000040,
LSDMA_PERF_SEL_CE_L1_STALL               = 0x00000041,
LSDMA_PERF_SEL_SDMA_INVACK_NFLUSH        = 0x00000042,
LSDMA_PERF_SEL_SDMA_INVACK_FLUSH         = 0x00000043,
LSDMA_PERF_SEL_ATCL2_INVREQ_NFLUSH       = 0x00000044,
LSDMA_PERF_SEL_ATCL2_INVREQ_FLUSH        = 0x00000045,
LSDMA_PERF_SEL_ATCL2_RET_XNACK           = 0x00000046,
LSDMA_PERF_SEL_ATCL2_RET_ACK             = 0x00000047,
LSDMA_PERF_SEL_ATCL2_FREE                = 0x00000048,
LSDMA_PERF_SEL_SDMA_ATCL2_SEND           = 0x00000049,
LSDMA_PERF_SEL_DMA_L1_WR_SEND            = 0x0000004a,
LSDMA_PERF_SEL_DMA_L1_RD_SEND            = 0x0000004b,
LSDMA_PERF_SEL_DMA_MC_WR_SEND            = 0x0000004c,
LSDMA_PERF_SEL_DMA_MC_RD_SEND            = 0x0000004d,
LSDMA_PERF_SEL_L1_WR_FIFO_IDLE           = 0x0000004e,
LSDMA_PERF_SEL_L1_RD_FIFO_IDLE           = 0x0000004f,
LSDMA_PERF_SEL_L1_WRL2_IDLE              = 0x00000050,
LSDMA_PERF_SEL_L1_RDL2_IDLE              = 0x00000051,
LSDMA_PERF_SEL_L1_WRMC_IDLE              = 0x00000052,
LSDMA_PERF_SEL_L1_RDMC_IDLE              = 0x00000053,
LSDMA_PERF_SEL_L1_WR_INV_IDLE            = 0x00000054,
LSDMA_PERF_SEL_L1_RD_INV_IDLE            = 0x00000055,
LSDMA_PERF_SEL_L1_WR_INV_EN              = 0x00000056,
LSDMA_PERF_SEL_L1_RD_INV_EN              = 0x00000057,
LSDMA_PERF_SEL_L1_WR_WAIT_INVADR         = 0x00000058,
LSDMA_PERF_SEL_L1_RD_WAIT_INVADR         = 0x00000059,
LSDMA_PERF_SEL_IS_INVREQ_ADDR_WR         = 0x0000005a,
LSDMA_PERF_SEL_IS_INVREQ_ADDR_RD         = 0x0000005b,
LSDMA_PERF_SEL_L1_WR_XNACK_TIMEOUT       = 0x0000005c,
LSDMA_PERF_SEL_L1_RD_XNACK_TIMEOUT       = 0x0000005d,
LSDMA_PERF_SEL_L1_INV_MIDDLE             = 0x0000005e,
LSDMA_PERF_SEL_CE_OR_F32_MMHUB_WR_REQ    = 0x0000005f,
LSDMA_PERF_SEL_CE_OR_F32_MMHUB_WR_RET    = 0x00000060,
LSDMA_PERF_SEL_ATOMIC_MMHUB_WR_REQ       = 0x00000061,
LSDMA_PERF_SEL_ATOMIC_MMHUB_WR_RET       = 0x00000062,
LSDMA_PERF_SEL_CE_OR_F32_MMHUB_RD_REQ    = 0x00000063,
LSDMA_PERF_SEL_CE_OR_F32_MMHUB_RD_RET    = 0x00000064,
LSDMA_PERF_SEL_RB_MMHUB_RD_REQ           = 0x00000065,
LSDMA_PERF_SEL_RB_MMHUB_RD_RET           = 0x00000066,
LSDMA_PERF_SEL_IB_MMHUB_RD_REQ           = 0x00000067,
LSDMA_PERF_SEL_IB_MMHUB_RD_RET           = 0x00000068,
LSDMA_PERF_SEL_WPTR_MMHUB_RD_REQ         = 0x00000069,
LSDMA_PERF_SEL_WPTR_MMHUB_RD_RET         = 0x0000006a,
LSDMA_PERF_SEL_UTCL1_UTCL2_REQ           = 0x0000006b,
LSDMA_PERF_SEL_UTCL1_UTCL2_RET           = 0x0000006c,
LSDMA_PERF_SEL_CMD_OP_MATCH              = 0x0000006d,
LSDMA_PERF_SEL_CMD_OP_START              = 0x0000006e,
LSDMA_PERF_SEL_CMD_OP_END                = 0x0000006f,
LSDMA_PERF_SEL_CE_BUSY                   = 0x00000070,
LSDMA_PERF_SEL_CE_BUSY_START             = 0x00000071,
LSDMA_PERF_SEL_CE_BUSY_END               = 0x00000072,
LSDMA_PERF_SEL_F32_PERFCNT_TRIGGER       = 0x00000073,
LSDMA_PERF_SEL_F32_PERFCNT_TRIGGER_START = 0x00000074,
LSDMA_PERF_SEL_F32_PERFCNT_TRIGGER_END   = 0x00000075,
LSDMA_PERF_SEL_CE_MMHUB_WRREQ_SEND       = 0x00000076,
LSDMA_PERF_SEL_MMHUB_CE_WRRET_VALID      = 0x00000077,
LSDMA_PERF_SEL_CE_MMHUB_RDREQ_SEND       = 0x00000078,
LSDMA_PERF_SEL_MMHUB_CE_RDRET_VALID      = 0x00000079,
LSDMA_PERF_SEL_DRAM_ECC                  = 0x0000007a,
LSDMA_PERF_SEL_NACK_GEN_ERR              = 0x0000007b,
} LSDMA_PERF_SEL;

/*******************************************************
 * SMUIO_ROM Enums
 *******************************************************/

/*
 * ROM_SIGNATURE value
 */

#define ROM_SIGNATURE                  0x0000aa55

/*******************************************************
 * UVD_EFC Enums
 *******************************************************/

/*
 * EFC_SURFACE_PIXEL_FORMAT enum
 */

typedef enum EFC_SURFACE_PIXEL_FORMAT {
EFC_ARGB1555                             = 0x00000001,
EFC_RGBA5551                             = 0x00000002,
EFC_RGB565                               = 0x00000003,
EFC_BGR565                               = 0x00000004,
EFC_ARGB4444                             = 0x00000005,
EFC_RGBA4444                             = 0x00000006,
EFC_ARGB8888                             = 0x00000008,
EFC_RGBA8888                             = 0x00000009,
EFC_ARGB2101010                          = 0x0000000a,
EFC_RGBA1010102                          = 0x0000000b,
EFC_AYCrCb8888                           = 0x0000000c,
EFC_YCrCbA8888                           = 0x0000000d,
EFC_ACrYCb8888                           = 0x0000000e,
EFC_CrYCbA8888                           = 0x0000000f,
EFC_ARGB16161616_10MSB                   = 0x00000010,
EFC_RGBA16161616_10MSB                   = 0x00000011,
EFC_ARGB16161616_10LSB                   = 0x00000012,
EFC_RGBA16161616_10LSB                   = 0x00000013,
EFC_ARGB16161616_12MSB                   = 0x00000014,
EFC_RGBA16161616_12MSB                   = 0x00000015,
EFC_ARGB16161616_12LSB                   = 0x00000016,
EFC_RGBA16161616_12LSB                   = 0x00000017,
EFC_ARGB16161616_FLOAT                   = 0x00000018,
EFC_RGBA16161616_FLOAT                   = 0x00000019,
EFC_ARGB16161616_UNORM                   = 0x0000001a,
EFC_RGBA16161616_UNORM                   = 0x0000001b,
EFC_ARGB16161616_SNORM                   = 0x0000001c,
EFC_RGBA16161616_SNORM                   = 0x0000001d,
EFC_AYCrCb16161616_10MSB                 = 0x00000020,
EFC_AYCrCb16161616_10LSB                 = 0x00000021,
EFC_YCrCbA16161616_10MSB                 = 0x00000022,
EFC_YCrCbA16161616_10LSB                 = 0x00000023,
EFC_ACrYCb16161616_10MSB                 = 0x00000024,
EFC_ACrYCb16161616_10LSB                 = 0x00000025,
EFC_CrYCbA16161616_10MSB                 = 0x00000026,
EFC_CrYCbA16161616_10LSB                 = 0x00000027,
EFC_AYCrCb16161616_12MSB                 = 0x00000028,
EFC_AYCrCb16161616_12LSB                 = 0x00000029,
EFC_YCrCbA16161616_12MSB                 = 0x0000002a,
EFC_YCrCbA16161616_12LSB                 = 0x0000002b,
EFC_ACrYCb16161616_12MSB                 = 0x0000002c,
EFC_ACrYCb16161616_12LSB                 = 0x0000002d,
EFC_CrYCbA16161616_12MSB                 = 0x0000002e,
EFC_CrYCbA16161616_12LSB                 = 0x0000002f,
EFC_Y8_CrCb88_420_PLANAR                 = 0x00000040,
EFC_Y8_CbCr88_420_PLANAR                 = 0x00000041,
EFC_Y10_CrCb1010_420_PLANAR              = 0x00000042,
EFC_Y10_CbCr1010_420_PLANAR              = 0x00000043,
EFC_Y12_CrCb1212_420_PLANAR              = 0x00000044,
EFC_Y12_CbCr1212_420_PLANAR              = 0x00000045,
EFC_YCrYCb8888_422_PACKED                = 0x00000048,
EFC_YCbYCr8888_422_PACKED                = 0x00000049,
EFC_CrYCbY8888_422_PACKED                = 0x0000004a,
EFC_CbYCrY8888_422_PACKED                = 0x0000004b,
EFC_YCrYCb10101010_422_PACKED            = 0x0000004c,
EFC_YCbYCr10101010_422_PACKED            = 0x0000004d,
EFC_CrYCbY10101010_422_PACKED            = 0x0000004e,
EFC_CbYCrY10101010_422_PACKED            = 0x0000004f,
EFC_YCrYCb12121212_422_PACKED            = 0x00000050,
EFC_YCbYCr12121212_422_PACKED            = 0x00000051,
EFC_CrYCbY12121212_422_PACKED            = 0x00000052,
EFC_CbYCrY12121212_422_PACKED            = 0x00000053,
EFC_RGB111110_FIX                        = 0x00000070,
EFC_BGR101111_FIX                        = 0x00000071,
EFC_ACrYCb2101010                        = 0x00000072,
EFC_CrYCbA1010102                        = 0x00000073,
EFC_RGB111110_FLOAT                      = 0x00000076,
EFC_BGR101111_FLOAT                      = 0x00000077,
EFC_MONO_8                               = 0x00000078,
EFC_MONO_10MSB                           = 0x00000079,
EFC_MONO_10LSB                           = 0x0000007a,
EFC_MONO_12MSB                           = 0x0000007b,
EFC_MONO_12LSB                           = 0x0000007c,
EFC_MONO_16                              = 0x0000007d,
} EFC_SURFACE_PIXEL_FORMAT;

#endif /*_soc21_ENUM_HEADER*/
