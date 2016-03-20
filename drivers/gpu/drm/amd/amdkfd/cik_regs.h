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

/* if PTR32, these are the bases for scratch and lds */
#define	PRIVATE_BASE(x)					((x) << 0) /* scratch */
#define	SHARED_BASE(x)					((x) << 16) /* LDS */
#define	PTR32						(1 << 0)
#define	ALIGNMENT_MODE(x)				((x) << 2)
#define	SH_MEM_ALIGNMENT_MODE_UNALIGNED			3
#define	DEFAULT_MTYPE(x)				((x) << 4)
#define	APE1_MTYPE(x)					((x) << 7)

/* valid for both DEFAULT_MTYPE and APE1_MTYPE */
#define	MTYPE_CACHED					0
#define	MTYPE_NONCACHED					3

#define	DEFAULT_CP_HQD_PERSISTENT_STATE			(0x33U << 8)
#define	PRELOAD_REQ					(1 << 0)

#define	MQD_CONTROL_PRIV_STATE_EN			(1U << 8)

#define	DEFAULT_MIN_IB_AVAIL_SIZE			(3U << 20)

#define	IB_ATC_EN					(1U << 23)

#define	QUANTUM_EN					1U
#define	QUANTUM_SCALE_1MS				(1U << 4)
#define	QUANTUM_DURATION(x)				((x) << 8)

#define	RPTR_BLOCK_SIZE(x)				((x) << 8)
#define	MIN_AVAIL_SIZE(x)				((x) << 20)
#define	DEFAULT_RPTR_BLOCK_SIZE				RPTR_BLOCK_SIZE(5)
#define	DEFAULT_MIN_AVAIL_SIZE				MIN_AVAIL_SIZE(3)

#define	PQ_ATC_EN					(1 << 23)
#define	NO_UPDATE_RPTR					(1 << 27)

#define	DOORBELL_OFFSET(x)				((x) << 2)
#define	DOORBELL_EN					(1 << 30)

#define	PRIV_STATE					(1 << 30)
#define	KMD_QUEUE					(1 << 31)

#define	AQL_ENABLE					1

#define GRBM_GFX_INDEX					0x30800

#define	ATC_VMID_PASID_MAPPING_VALID			(1U << 31)

#endif
