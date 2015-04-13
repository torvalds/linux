/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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
 */

#ifndef CIK_REGS_H
#define CIK_REGS_H

#define IH_VMID_0_LUT					0x3D40u

#define BIF_DOORBELL_CNTL				0x530Cu

#define	SRBM_GFX_CNTL					0xE44
#define	PIPEID(x)					((x) << 0)
#define	MEID(x)						((x) << 2)
#define	VMID(x)						((x) << 4)
#define	QUEUEID(x)					((x) << 8)

#define	SQ_CONFIG					0x8C00

#define	SH_MEM_BASES					0x8C28
/* if PTR32, these are the bases for scratch and lds */
#define	PRIVATE_BASE(x)					((x) << 0) /* scratch */
#define	SHARED_BASE(x)					((x) << 16) /* LDS */
#define	SH_MEM_APE1_BASE				0x8C2C
/* if PTR32, this is the base location of GPUVM */
#define	SH_MEM_APE1_LIMIT				0x8C30
/* if PTR32, this is the upper limit of GPUVM */
#define	SH_MEM_CONFIG					0x8C34
#define	PTR32						(1 << 0)
#define PRIVATE_ATC					(1 << 1)
#define	ALIGNMENT_MODE(x)				((x) << 2)
#define	SH_MEM_ALIGNMENT_MODE_DWORD			0
#define	SH_MEM_ALIGNMENT_MODE_DWORD_STRICT		1
#define	SH_MEM_ALIGNMENT_MODE_STRICT			2
#define	SH_MEM_ALIGNMENT_MODE_UNALIGNED			3
#define	DEFAULT_MTYPE(x)				((x) << 4)
#define	APE1_MTYPE(x)					((x) << 7)

/* valid for both DEFAULT_MTYPE and APE1_MTYPE */
#define	MTYPE_CACHED					0
#define	MTYPE_NONCACHED					3


#define SH_STATIC_MEM_CONFIG				0x9604u

#define	TC_CFG_L1_LOAD_POLICY0				0xAC68
#define	TC_CFG_L1_LOAD_POLICY1				0xAC6C
#define	TC_CFG_L1_STORE_POLICY				0xAC70
#define	TC_CFG_L2_LOAD_POLICY0				0xAC74
#define	TC_CFG_L2_LOAD_POLICY1				0xAC78
#define	TC_CFG_L2_STORE_POLICY0				0xAC7C
#define	TC_CFG_L2_STORE_POLICY1				0xAC80
#define	TC_CFG_L2_ATOMIC_POLICY				0xAC84
#define	TC_CFG_L1_VOLATILE				0xAC88
#define	TC_CFG_L2_VOLATILE				0xAC8C

#define CP_PQ_WPTR_POLL_CNTL				0xC20C
#define	WPTR_POLL_EN					(1 << 31)

#define CPC_INT_CNTL					0xC2D0
#define CP_ME1_PIPE0_INT_CNTL				0xC214
#define CP_ME1_PIPE1_INT_CNTL				0xC218
#define CP_ME1_PIPE2_INT_CNTL				0xC21C
#define CP_ME1_PIPE3_INT_CNTL				0xC220
#define CP_ME2_PIPE0_INT_CNTL				0xC224
#define CP_ME2_PIPE1_INT_CNTL				0xC228
#define CP_ME2_PIPE2_INT_CNTL				0xC22C
#define CP_ME2_PIPE3_INT_CNTL				0xC230
#define DEQUEUE_REQUEST_INT_ENABLE			(1 << 13)
#define WRM_POLL_TIMEOUT_INT_ENABLE			(1 << 17)
#define PRIV_REG_INT_ENABLE				(1 << 23)
#define TIME_STAMP_INT_ENABLE				(1 << 26)
#define GENERIC2_INT_ENABLE				(1 << 29)
#define GENERIC1_INT_ENABLE				(1 << 30)
#define GENERIC0_INT_ENABLE				(1 << 31)
#define CP_ME1_PIPE0_INT_STATUS				0xC214
#define CP_ME1_PIPE1_INT_STATUS				0xC218
#define CP_ME1_PIPE2_INT_STATUS				0xC21C
#define CP_ME1_PIPE3_INT_STATUS				0xC220
#define CP_ME2_PIPE0_INT_STATUS				0xC224
#define CP_ME2_PIPE1_INT_STATUS				0xC228
#define CP_ME2_PIPE2_INT_STATUS				0xC22C
#define CP_ME2_PIPE3_INT_STATUS				0xC230
#define DEQUEUE_REQUEST_INT_STATUS			(1 << 13)
#define WRM_POLL_TIMEOUT_INT_STATUS			(1 << 17)
#define PRIV_REG_INT_STATUS				(1 << 23)
#define TIME_STAMP_INT_STATUS				(1 << 26)
#define GENERIC2_INT_STATUS				(1 << 29)
#define GENERIC1_INT_STATUS				(1 << 30)
#define GENERIC0_INT_STATUS				(1 << 31)

#define CP_HPD_EOP_BASE_ADDR				0xC904
#define CP_HPD_EOP_BASE_ADDR_HI				0xC908
#define CP_HPD_EOP_VMID					0xC90C
#define CP_HPD_EOP_CONTROL				0xC910
#define	EOP_SIZE(x)					((x) << 0)
#define	EOP_SIZE_MASK					(0x3f << 0)
#define CP_MQD_BASE_ADDR				0xC914
#define CP_MQD_BASE_ADDR_HI				0xC918
#define CP_HQD_ACTIVE					0xC91C
#define CP_HQD_VMID					0xC920

#define CP_HQD_PERSISTENT_STATE				0xC924u
#define	DEFAULT_CP_HQD_PERSISTENT_STATE			(0x33U << 8)
#define	PRELOAD_REQ					(1 << 0)

#define CP_HQD_PIPE_PRIORITY				0xC928u
#define CP_HQD_QUEUE_PRIORITY				0xC92Cu
#define CP_HQD_QUANTUM					0xC930u
#define	QUANTUM_EN					1U
#define	QUANTUM_SCALE_1MS				(1U << 4)
#define	QUANTUM_DURATION(x)				((x) << 8)

#define CP_HQD_PQ_BASE					0xC934
#define CP_HQD_PQ_BASE_HI				0xC938
#define CP_HQD_PQ_RPTR					0xC93C
#define CP_HQD_PQ_RPTR_REPORT_ADDR			0xC940
#define CP_HQD_PQ_RPTR_REPORT_ADDR_HI			0xC944
#define CP_HQD_PQ_WPTR_POLL_ADDR			0xC948
#define CP_HQD_PQ_WPTR_POLL_ADDR_HI			0xC94C
#define CP_HQD_PQ_DOORBELL_CONTROL			0xC950
#define	DOORBELL_OFFSET(x)				((x) << 2)
#define	DOORBELL_OFFSET_MASK				(0x1fffff << 2)
#define	DOORBELL_SOURCE					(1 << 28)
#define	DOORBELL_SCHD_HIT				(1 << 29)
#define	DOORBELL_EN					(1 << 30)
#define	DOORBELL_HIT					(1 << 31)
#define CP_HQD_PQ_WPTR					0xC954
#define CP_HQD_PQ_CONTROL				0xC958
#define	QUEUE_SIZE(x)					((x) << 0)
#define	QUEUE_SIZE_MASK					(0x3f << 0)
#define	RPTR_BLOCK_SIZE(x)				((x) << 8)
#define	RPTR_BLOCK_SIZE_MASK				(0x3f << 8)
#define	MIN_AVAIL_SIZE(x)				((x) << 20)
#define	PQ_ATC_EN					(1 << 23)
#define	PQ_VOLATILE					(1 << 26)
#define	NO_UPDATE_RPTR					(1 << 27)
#define	UNORD_DISPATCH					(1 << 28)
#define	ROQ_PQ_IB_FLIP					(1 << 29)
#define	PRIV_STATE					(1 << 30)
#define	KMD_QUEUE					(1 << 31)

#define	DEFAULT_RPTR_BLOCK_SIZE				RPTR_BLOCK_SIZE(5)
#define	DEFAULT_MIN_AVAIL_SIZE				MIN_AVAIL_SIZE(3)

#define CP_HQD_IB_BASE_ADDR				0xC95Cu
#define CP_HQD_IB_BASE_ADDR_HI				0xC960u
#define CP_HQD_IB_RPTR					0xC964u
#define CP_HQD_IB_CONTROL				0xC968u
#define	IB_ATC_EN					(1U << 23)
#define	DEFAULT_MIN_IB_AVAIL_SIZE			(3U << 20)

#define	AQL_ENABLE					1

#define CP_HQD_DEQUEUE_REQUEST				0xC974
#define	DEQUEUE_REQUEST_DRAIN				1
#define DEQUEUE_REQUEST_RESET				2
#define		DEQUEUE_INT					(1U << 8)

#define CP_HQD_SEMA_CMD					0xC97Cu
#define CP_HQD_MSG_TYPE					0xC980u
#define CP_HQD_ATOMIC0_PREOP_LO				0xC984u
#define CP_HQD_ATOMIC0_PREOP_HI				0xC988u
#define CP_HQD_ATOMIC1_PREOP_LO				0xC98Cu
#define CP_HQD_ATOMIC1_PREOP_HI				0xC990u
#define CP_HQD_HQ_SCHEDULER0				0xC994u
#define CP_HQD_HQ_SCHEDULER1				0xC998u


#define CP_MQD_CONTROL					0xC99C
#define	MQD_VMID(x)					((x) << 0)
#define	MQD_VMID_MASK					(0xf << 0)
#define	MQD_CONTROL_PRIV_STATE_EN			(1U << 8)

#define	SDMA_RB_VMID(x)					(x << 24)
#define	SDMA_RB_ENABLE					(1 << 0)
#define	SDMA_RB_SIZE(x)					((x) << 1) /* log2 */
#define	SDMA_RPTR_WRITEBACK_ENABLE			(1 << 12)
#define	SDMA_RPTR_WRITEBACK_TIMER(x)			((x) << 16) /* log2 */
#define	SDMA_OFFSET(x)					(x << 0)
#define	SDMA_DB_ENABLE					(1 << 28)
#define	SDMA_ATC					(1 << 0)
#define	SDMA_VA_PTR32					(1 << 4)
#define	SDMA_VA_SHARED_BASE(x)				(x << 8)

#define GRBM_GFX_INDEX					0x30800
#define	INSTANCE_INDEX(x)				((x) << 0)
#define	SH_INDEX(x)					((x) << 8)
#define	SE_INDEX(x)					((x) << 16)
#define	SH_BROADCAST_WRITES				(1 << 29)
#define	INSTANCE_BROADCAST_WRITES			(1 << 30)
#define	SE_BROADCAST_WRITES				(1 << 31)

#define SQC_CACHES					0x30d20
#define SQC_POLICY					0x8C38u
#define SQC_VOLATILE					0x8C3Cu

#define CP_PERFMON_CNTL					0x36020

#define ATC_VMID0_PASID_MAPPING				0x339Cu
#define	ATC_VMID_PASID_MAPPING_UPDATE_STATUS		0x3398u
#define	ATC_VMID_PASID_MAPPING_VALID			(1U << 31)

#define ATC_VM_APERTURE0_CNTL				0x3310u
#define	ATS_ACCESS_MODE_NEVER				0
#define	ATS_ACCESS_MODE_ALWAYS				1

#define ATC_VM_APERTURE0_CNTL2				0x3318u
#define ATC_VM_APERTURE0_HIGH_ADDR			0x3308u
#define ATC_VM_APERTURE0_LOW_ADDR			0x3300u
#define ATC_VM_APERTURE1_CNTL				0x3314u
#define ATC_VM_APERTURE1_CNTL2				0x331Cu
#define ATC_VM_APERTURE1_HIGH_ADDR			0x330Cu
#define ATC_VM_APERTURE1_LOW_ADDR			0x3304u

#endif
