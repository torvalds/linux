/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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

#ifndef _xgmi_6_1_0_SH_MASK_HEADER
#define _xgmi_6_1_0_SH_MASK_HEADER

//PCS_XGMI3X16_PCS_ERROR_STATUS
#define PCS_XGMI3X16_PCS_ERROR_STATUS__DataLossErr__SHIFT                                    0x0
#define PCS_XGMI3X16_PCS_ERROR_STATUS__TrainingErr__SHIFT                                    0x1
#define PCS_XGMI3X16_PCS_ERROR_STATUS__FlowCtrlAckErr__SHIFT                                 0x2
#define PCS_XGMI3X16_PCS_ERROR_STATUS__RxFifoUnderflowErr__SHIFT                             0x3
#define PCS_XGMI3X16_PCS_ERROR_STATUS__RxFifoOverflowErr__SHIFT                              0x4
#define PCS_XGMI3X16_PCS_ERROR_STATUS__CRCErr__SHIFT                                         0x5
#define PCS_XGMI3X16_PCS_ERROR_STATUS__BERExceededErr__SHIFT                                 0x6
#define PCS_XGMI3X16_PCS_ERROR_STATUS__TxVcidDataErr__SHIFT                                  0x7
#define PCS_XGMI3X16_PCS_ERROR_STATUS__ReplayBufParityErr__SHIFT                             0x8
#define PCS_XGMI3X16_PCS_ERROR_STATUS__DataParityErr__SHIFT                                  0x9
#define PCS_XGMI3X16_PCS_ERROR_STATUS__ReplayFifoOverflowErr__SHIFT                          0xa
#define PCS_XGMI3X16_PCS_ERROR_STATUS__ReplayFifoUnderflowErr__SHIFT                         0xb
#define PCS_XGMI3X16_PCS_ERROR_STATUS__ElasticFifoOverflowErr__SHIFT                         0xc
#define PCS_XGMI3X16_PCS_ERROR_STATUS__DeskewErr__SHIFT                                      0xd
#define PCS_XGMI3X16_PCS_ERROR_STATUS__FlowCtrlCRCErr__SHIFT                                 0xe
#define PCS_XGMI3X16_PCS_ERROR_STATUS__DataStartupLimitErr__SHIFT                            0xf
#define PCS_XGMI3X16_PCS_ERROR_STATUS__FCInitTimeoutErr__SHIFT                               0x10
#define PCS_XGMI3X16_PCS_ERROR_STATUS__RecoveryTimeoutErr__SHIFT                             0x11
#define PCS_XGMI3X16_PCS_ERROR_STATUS__ReadySerialTimeoutErr__SHIFT                          0x12
#define PCS_XGMI3X16_PCS_ERROR_STATUS__ReadySerialAttemptErr__SHIFT                          0x13
#define PCS_XGMI3X16_PCS_ERROR_STATUS__RecoveryAttemptErr__SHIFT                             0x14
#define PCS_XGMI3X16_PCS_ERROR_STATUS__RecoveryRelockAttemptErr__SHIFT                       0x15
#define PCS_XGMI3X16_PCS_ERROR_STATUS__ReplayAttemptErr__SHIFT                               0x16
#define PCS_XGMI3X16_PCS_ERROR_STATUS__SyncHdrErr__SHIFT                                     0x17
#define PCS_XGMI3X16_PCS_ERROR_STATUS__TxReplayTimeoutErr__SHIFT                             0x18
#define PCS_XGMI3X16_PCS_ERROR_STATUS__RxReplayTimeoutErr__SHIFT                             0x19
#define PCS_XGMI3X16_PCS_ERROR_STATUS__LinkSubTxTimeoutErr__SHIFT                            0x1a
#define PCS_XGMI3X16_PCS_ERROR_STATUS__LinkSubRxTimeoutErr__SHIFT                            0x1b
#define PCS_XGMI3X16_PCS_ERROR_STATUS__RxCMDPktErr__SHIFT                                    0x1c
#define PCS_XGMI3X16_PCS_ERROR_STATUS__DataLossErr_MASK                                      0x00000001L
#define PCS_XGMI3X16_PCS_ERROR_STATUS__TrainingErr_MASK                                      0x00000002L
#define PCS_XGMI3X16_PCS_ERROR_STATUS__FlowCtrlAckErr_MASK                                   0x00000004L
#define PCS_XGMI3X16_PCS_ERROR_STATUS__RxFifoUnderflowErr_MASK                               0x00000008L
#define PCS_XGMI3X16_PCS_ERROR_STATUS__RxFifoOverflowErr_MASK                                0x00000010L
#define PCS_XGMI3X16_PCS_ERROR_STATUS__CRCErr_MASK                                           0x00000020L
#define PCS_XGMI3X16_PCS_ERROR_STATUS__BERExceededErr_MASK                                   0x00000040L
#define PCS_XGMI3X16_PCS_ERROR_STATUS__TxVcidDataErr_MASK                                    0x00000080L
#define PCS_XGMI3X16_PCS_ERROR_STATUS__ReplayBufParityErr_MASK                               0x00000100L
#define PCS_XGMI3X16_PCS_ERROR_STATUS__DataParityErr_MASK                                    0x00000200L
#define PCS_XGMI3X16_PCS_ERROR_STATUS__ReplayFifoOverflowErr_MASK                            0x00000400L
#define PCS_XGMI3X16_PCS_ERROR_STATUS__ReplayFifoUnderflowErr_MASK                           0x00000800L
#define PCS_XGMI3X16_PCS_ERROR_STATUS__ElasticFifoOverflowErr_MASK                           0x00001000L
#define PCS_XGMI3X16_PCS_ERROR_STATUS__DeskewErr_MASK                                        0x00002000L
#define PCS_XGMI3X16_PCS_ERROR_STATUS__FlowCtrlCRCErr_MASK                                   0x00004000L
#define PCS_XGMI3X16_PCS_ERROR_STATUS__DataStartupLimitErr_MASK                              0x00008000L
#define PCS_XGMI3X16_PCS_ERROR_STATUS__FCInitTimeoutErr_MASK                                 0x00010000L
#define PCS_XGMI3X16_PCS_ERROR_STATUS__RecoveryTimeoutErr_MASK                               0x00020000L
#define PCS_XGMI3X16_PCS_ERROR_STATUS__ReadySerialTimeoutErr_MASK                            0x00040000L
#define PCS_XGMI3X16_PCS_ERROR_STATUS__ReadySerialAttemptErr_MASK                            0x00080000L
#define PCS_XGMI3X16_PCS_ERROR_STATUS__RecoveryAttemptErr_MASK                               0x00100000L
#define PCS_XGMI3X16_PCS_ERROR_STATUS__RecoveryRelockAttemptErr_MASK                         0x00200000L
#define PCS_XGMI3X16_PCS_ERROR_STATUS__ReplayAttemptErr_MASK                                 0x00400000L
#define PCS_XGMI3X16_PCS_ERROR_STATUS__SyncHdrErr_MASK                                       0x00800000L
#define PCS_XGMI3X16_PCS_ERROR_STATUS__TxReplayTimeoutErr_MASK                               0x01000000L
#define PCS_XGMI3X16_PCS_ERROR_STATUS__RxReplayTimeoutErr_MASK                               0x02000000L
#define PCS_XGMI3X16_PCS_ERROR_STATUS__LinkSubTxTimeoutErr_MASK                              0x04000000L
#define PCS_XGMI3X16_PCS_ERROR_STATUS__LinkSubRxTimeoutErr_MASK                              0x08000000L
#define PCS_XGMI3X16_PCS_ERROR_STATUS__RxCMDPktErr_MASK                                      0x10000000L

#endif
