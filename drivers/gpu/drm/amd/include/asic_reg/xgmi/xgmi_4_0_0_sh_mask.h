/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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

#ifndef _xgmi_4_0_0_SH_MASK_HEADER
#define _xgmi_4_0_0_SH_MASK_HEADER

//PCS_GOPX16_PCS_ERROR_STATUS
#define	XGMI0_PCS_GOPX16_PCS_ERROR_STATUS__DataLossErr__SHIFT								0x0
#define	XGMI0_PCS_GOPX16_PCS_ERROR_STATUS__TrainingErr__SHIFT								0x1
#define	XGMI0_PCS_GOPX16_PCS_ERROR_STATUS__CRCErr__SHIFT								0x5
#define	XGMI0_PCS_GOPX16_PCS_ERROR_STATUS__BERExceededErr__SHIFT							0x6
#define	XGMI0_PCS_GOPX16_PCS_ERROR_STATUS__TxMetaDataErr__SHIFT								0x7
#define	XGMI0_PCS_GOPX16_PCS_ERROR_STATUS__ReplayBufParityErr__SHIFT							0x8
#define	XGMI0_PCS_GOPX16_PCS_ERROR_STATUS__DataParityErr__SHIFT								0x9
#define	XGMI0_PCS_GOPX16_PCS_ERROR_STATUS__ReplayFifoOverflowErr__SHIFT							0xa
#define	XGMI0_PCS_GOPX16_PCS_ERROR_STATUS__ReplayFifoUnderflowErr__SHIFT						0xb
#define	XGMI0_PCS_GOPX16_PCS_ERROR_STATUS__ElasticFifoOverflowErr__SHIFT						0xc
#define	XGMI0_PCS_GOPX16_PCS_ERROR_STATUS__DeskewErr__SHIFT								0xd
#define	XGMI0_PCS_GOPX16_PCS_ERROR_STATUS__DataStartupLimitErr__SHIFT							0xf
#define	XGMI0_PCS_GOPX16_PCS_ERROR_STATUS__FCInitTimeoutErr__SHIFT							0x10
#define	XGMI0_PCS_GOPX16_PCS_ERROR_STATUS__RecoveryTimeoutErr__SHIFT							0x11
#define	XGMI0_PCS_GOPX16_PCS_ERROR_STATUS__ReadySerialTimeoutErr__SHIFT							0x12
#define	XGMI0_PCS_GOPX16_PCS_ERROR_STATUS__ReadySerialAttemptErr__SHIFT							0x13
#define	XGMI0_PCS_GOPX16_PCS_ERROR_STATUS__RecoveryAttemptErr__SHIFT							0x14
#define	XGMI0_PCS_GOPX16_PCS_ERROR_STATUS__RecoveryRelockAttemptErr__SHIFT						0x15
#define	XGMI0_PCS_GOPX16_PCS_ERROR_STATUS__ClearBERAccum__SHIFT								0x17
#define	XGMI0_PCS_GOPX16_PCS_ERROR_STATUS__BERAccumulator__SHIFT							0x18
#define	XGMI0_PCS_GOPX16_PCS_ERROR_STATUS__DataLossErr_MASK								0x00000001L
#define	XGMI0_PCS_GOPX16_PCS_ERROR_STATUS__TrainingErr_MASK								0x00000002L
#define	XGMI0_PCS_GOPX16_PCS_ERROR_STATUS__CRCErr_MASK									0x00000020L
#define	XGMI0_PCS_GOPX16_PCS_ERROR_STATUS__BERExceededErr_MASK								0x00000040L
#define	XGMI0_PCS_GOPX16_PCS_ERROR_STATUS__TxMetaDataErr_MASK								0x00000080L
#define	XGMI0_PCS_GOPX16_PCS_ERROR_STATUS__ReplayBufParityErr_MASK							0x00000100L
#define	XGMI0_PCS_GOPX16_PCS_ERROR_STATUS__DataParityErr_MASK								0x00000200L
#define	XGMI0_PCS_GOPX16_PCS_ERROR_STATUS__ReplayFifoOverflowErr_MASK							0x00000400L
#define	XGMI0_PCS_GOPX16_PCS_ERROR_STATUS__ReplayFifoUnderflowErr_MASK							0x00000800L
#define	XGMI0_PCS_GOPX16_PCS_ERROR_STATUS__ElasticFifoOverflowErr_MASK							0x00001000L
#define	XGMI0_PCS_GOPX16_PCS_ERROR_STATUS__DeskewErr_MASK								0x00002000L
#define	XGMI0_PCS_GOPX16_PCS_ERROR_STATUS__DataStartupLimitErr_MASK							0x00008000L
#define	XGMI0_PCS_GOPX16_PCS_ERROR_STATUS__FCInitTimeoutErr_MASK							0x00010000L
#define	XGMI0_PCS_GOPX16_PCS_ERROR_STATUS__RecoveryTimeoutErr_MASK							0x00020000L
#define	XGMI0_PCS_GOPX16_PCS_ERROR_STATUS__ReadySerialTimeoutErr_MASK							0x00040000L
#define	XGMI0_PCS_GOPX16_PCS_ERROR_STATUS__ReadySerialAttemptErr_MASK							0x00080000L
#define	XGMI0_PCS_GOPX16_PCS_ERROR_STATUS__RecoveryAttemptErr_MASK							0x00100000L
#define	XGMI0_PCS_GOPX16_PCS_ERROR_STATUS__RecoveryRelockAttemptErr_MASK						0x00200000L
#define	XGMI0_PCS_GOPX16_PCS_ERROR_STATUS__ClearBERAccum_MASK								0x00800000L
#define	XGMI0_PCS_GOPX16_PCS_ERROR_STATUS__BERAccumulator_MASK								0xFF000000L

#endif
