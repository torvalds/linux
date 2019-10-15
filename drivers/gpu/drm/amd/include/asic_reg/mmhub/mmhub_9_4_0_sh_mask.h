/*
 * Copyright (C) 2018  Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef _mmhub_9_4_0_SH_MASK_HEADER
#define _mmhub_9_4_0_SH_MASK_HEADER

//MMEA0_SDP_ARB_FINAL
#define MMEA0_SDP_ARB_FINAL__DRAM_BURST_LIMIT__SHIFT                                                          0x0
#define MMEA0_SDP_ARB_FINAL__GMI_BURST_LIMIT__SHIFT                                                           0x5
#define MMEA0_SDP_ARB_FINAL__IO_BURST_LIMIT__SHIFT                                                            0xa
#define MMEA0_SDP_ARB_FINAL__BURST_LIMIT_MULTIPLIER__SHIFT                                                    0xf
#define MMEA0_SDP_ARB_FINAL__RDONLY_VC0__SHIFT                                                                0x11
#define MMEA0_SDP_ARB_FINAL__RDONLY_VC1__SHIFT                                                                0x12
#define MMEA0_SDP_ARB_FINAL__RDONLY_VC2__SHIFT                                                                0x13
#define MMEA0_SDP_ARB_FINAL__RDONLY_VC3__SHIFT                                                                0x14
#define MMEA0_SDP_ARB_FINAL__RDONLY_VC4__SHIFT                                                                0x15
#define MMEA0_SDP_ARB_FINAL__RDONLY_VC5__SHIFT                                                                0x16
#define MMEA0_SDP_ARB_FINAL__RDONLY_VC6__SHIFT                                                                0x17
#define MMEA0_SDP_ARB_FINAL__RDONLY_VC7__SHIFT                                                                0x18
#define MMEA0_SDP_ARB_FINAL__ERREVENT_ON_ERROR__SHIFT                                                         0x19
#define MMEA0_SDP_ARB_FINAL__HALTREQ_ON_ERROR__SHIFT                                                          0x1a
#define MMEA0_SDP_ARB_FINAL__DRAM_BURST_LIMIT_MASK                                                            0x0000001FL
#define MMEA0_SDP_ARB_FINAL__GMI_BURST_LIMIT_MASK                                                             0x000003E0L
#define MMEA0_SDP_ARB_FINAL__IO_BURST_LIMIT_MASK                                                              0x00007C00L
#define MMEA0_SDP_ARB_FINAL__BURST_LIMIT_MULTIPLIER_MASK                                                      0x00018000L
#define MMEA0_SDP_ARB_FINAL__RDONLY_VC0_MASK                                                                  0x00020000L
#define MMEA0_SDP_ARB_FINAL__RDONLY_VC1_MASK                                                                  0x00040000L
#define MMEA0_SDP_ARB_FINAL__RDONLY_VC2_MASK                                                                  0x00080000L
#define MMEA0_SDP_ARB_FINAL__RDONLY_VC3_MASK                                                                  0x00100000L
#define MMEA0_SDP_ARB_FINAL__RDONLY_VC4_MASK                                                                  0x00200000L
#define MMEA0_SDP_ARB_FINAL__RDONLY_VC5_MASK                                                                  0x00400000L
#define MMEA0_SDP_ARB_FINAL__RDONLY_VC6_MASK                                                                  0x00800000L
#define MMEA0_SDP_ARB_FINAL__RDONLY_VC7_MASK                                                                  0x01000000L
#define MMEA0_SDP_ARB_FINAL__ERREVENT_ON_ERROR_MASK                                                           0x02000000L
#define MMEA0_SDP_ARB_FINAL__HALTREQ_ON_ERROR_MASK                                                            0x04000000L
//MMEA0_EDC_CNT
#define MMEA0_EDC_CNT__DRAMRD_CMDMEM_SEC_COUNT__SHIFT                                                         0x0
#define MMEA0_EDC_CNT__DRAMRD_CMDMEM_DED_COUNT__SHIFT                                                         0x2
#define MMEA0_EDC_CNT__DRAMWR_CMDMEM_SEC_COUNT__SHIFT                                                         0x4
#define MMEA0_EDC_CNT__DRAMWR_CMDMEM_DED_COUNT__SHIFT                                                         0x6
#define MMEA0_EDC_CNT__DRAMWR_DATAMEM_SEC_COUNT__SHIFT                                                        0x8
#define MMEA0_EDC_CNT__DRAMWR_DATAMEM_DED_COUNT__SHIFT                                                        0xa
#define MMEA0_EDC_CNT__RRET_TAGMEM_SEC_COUNT__SHIFT                                                           0xc
#define MMEA0_EDC_CNT__RRET_TAGMEM_DED_COUNT__SHIFT                                                           0xe
#define MMEA0_EDC_CNT__WRET_TAGMEM_SEC_COUNT__SHIFT                                                           0x10
#define MMEA0_EDC_CNT__WRET_TAGMEM_DED_COUNT__SHIFT                                                           0x12
#define MMEA0_EDC_CNT__DRAMRD_PAGEMEM_SED_COUNT__SHIFT                                                        0x14
#define MMEA0_EDC_CNT__DRAMWR_PAGEMEM_SED_COUNT__SHIFT                                                        0x16
#define MMEA0_EDC_CNT__IORD_CMDMEM_SED_COUNT__SHIFT                                                           0x18
#define MMEA0_EDC_CNT__IOWR_CMDMEM_SED_COUNT__SHIFT                                                           0x1a
#define MMEA0_EDC_CNT__IOWR_DATAMEM_SED_COUNT__SHIFT                                                          0x1c
#define MMEA0_EDC_CNT__DRAMRD_CMDMEM_SEC_COUNT_MASK                                                           0x00000003L
#define MMEA0_EDC_CNT__DRAMRD_CMDMEM_DED_COUNT_MASK                                                           0x0000000CL
#define MMEA0_EDC_CNT__DRAMWR_CMDMEM_SEC_COUNT_MASK                                                           0x00000030L
#define MMEA0_EDC_CNT__DRAMWR_CMDMEM_DED_COUNT_MASK                                                           0x000000C0L
#define MMEA0_EDC_CNT__DRAMWR_DATAMEM_SEC_COUNT_MASK                                                          0x00000300L
#define MMEA0_EDC_CNT__DRAMWR_DATAMEM_DED_COUNT_MASK                                                          0x00000C00L
#define MMEA0_EDC_CNT__RRET_TAGMEM_SEC_COUNT_MASK                                                             0x00003000L
#define MMEA0_EDC_CNT__RRET_TAGMEM_DED_COUNT_MASK                                                             0x0000C000L
#define MMEA0_EDC_CNT__WRET_TAGMEM_SEC_COUNT_MASK                                                             0x00030000L
#define MMEA0_EDC_CNT__WRET_TAGMEM_DED_COUNT_MASK                                                             0x000C0000L
#define MMEA0_EDC_CNT__DRAMRD_PAGEMEM_SED_COUNT_MASK                                                          0x00300000L
#define MMEA0_EDC_CNT__DRAMWR_PAGEMEM_SED_COUNT_MASK                                                          0x00C00000L
#define MMEA0_EDC_CNT__IORD_CMDMEM_SED_COUNT_MASK                                                             0x03000000L
#define MMEA0_EDC_CNT__IOWR_CMDMEM_SED_COUNT_MASK                                                             0x0C000000L
#define MMEA0_EDC_CNT__IOWR_DATAMEM_SED_COUNT_MASK                                                            0x30000000L
//MMEA0_EDC_CNT2
#define MMEA0_EDC_CNT2__GMIRD_CMDMEM_SEC_COUNT__SHIFT                                                         0x0
#define MMEA0_EDC_CNT2__GMIRD_CMDMEM_DED_COUNT__SHIFT                                                         0x2
#define MMEA0_EDC_CNT2__GMIWR_CMDMEM_SEC_COUNT__SHIFT                                                         0x4
#define MMEA0_EDC_CNT2__GMIWR_CMDMEM_DED_COUNT__SHIFT                                                         0x6
#define MMEA0_EDC_CNT2__GMIWR_DATAMEM_SEC_COUNT__SHIFT                                                        0x8
#define MMEA0_EDC_CNT2__GMIWR_DATAMEM_DED_COUNT__SHIFT                                                        0xa
#define MMEA0_EDC_CNT2__GMIRD_PAGEMEM_SED_COUNT__SHIFT                                                        0xc
#define MMEA0_EDC_CNT2__GMIWR_PAGEMEM_SED_COUNT__SHIFT                                                        0xe
#define MMEA0_EDC_CNT2__MAM_D0MEM_SED_COUNT__SHIFT                                                            0x10
#define MMEA0_EDC_CNT2__MAM_D1MEM_SED_COUNT__SHIFT                                                            0x12
#define MMEA0_EDC_CNT2__MAM_D2MEM_SED_COUNT__SHIFT                                                            0x14
#define MMEA0_EDC_CNT2__MAM_D3MEM_SED_COUNT__SHIFT                                                            0x16
#define MMEA0_EDC_CNT2__GMIRD_CMDMEM_SEC_COUNT_MASK                                                           0x00000003L
#define MMEA0_EDC_CNT2__GMIRD_CMDMEM_DED_COUNT_MASK                                                           0x0000000CL
#define MMEA0_EDC_CNT2__GMIWR_CMDMEM_SEC_COUNT_MASK                                                           0x00000030L
#define MMEA0_EDC_CNT2__GMIWR_CMDMEM_DED_COUNT_MASK                                                           0x000000C0L
#define MMEA0_EDC_CNT2__GMIWR_DATAMEM_SEC_COUNT_MASK                                                          0x00000300L
#define MMEA0_EDC_CNT2__GMIWR_DATAMEM_DED_COUNT_MASK                                                          0x00000C00L
#define MMEA0_EDC_CNT2__GMIRD_PAGEMEM_SED_COUNT_MASK                                                          0x00003000L
#define MMEA0_EDC_CNT2__GMIWR_PAGEMEM_SED_COUNT_MASK                                                          0x0000C000L
#define MMEA0_EDC_CNT2__MAM_D0MEM_SED_COUNT_MASK                                                              0x00030000L
#define MMEA0_EDC_CNT2__MAM_D1MEM_SED_COUNT_MASK                                                              0x000C0000L
#define MMEA0_EDC_CNT2__MAM_D2MEM_SED_COUNT_MASK                                                              0x00300000L
#define MMEA0_EDC_CNT2__MAM_D3MEM_SED_COUNT_MASK                                                              0x00C00000L
//MMEA0_EDC_MODE
#define MMEA0_EDC_MODE__COUNT_FED_OUT__SHIFT                                                                  0x10
#define MMEA0_EDC_MODE__GATE_FUE__SHIFT                                                                       0x11
#define MMEA0_EDC_MODE__DED_MODE__SHIFT                                                                       0x14
#define MMEA0_EDC_MODE__PROP_FED__SHIFT                                                                       0x1d
#define MMEA0_EDC_MODE__BYPASS__SHIFT                                                                         0x1f
#define MMEA0_EDC_MODE__COUNT_FED_OUT_MASK                                                                    0x00010000L
#define MMEA0_EDC_MODE__GATE_FUE_MASK                                                                         0x00020000L
#define MMEA0_EDC_MODE__DED_MODE_MASK                                                                         0x00300000L
#define MMEA0_EDC_MODE__PROP_FED_MASK                                                                         0x20000000L
#define MMEA0_EDC_MODE__BYPASS_MASK                                                                           0x80000000L
//MMEA0_ERR_STATUS
#define MMEA0_ERR_STATUS__SDP_RDRSP_STATUS__SHIFT                                                             0x0
#define MMEA0_ERR_STATUS__SDP_WRRSP_STATUS__SHIFT                                                             0x4
#define MMEA0_ERR_STATUS__SDP_RDRSP_DATASTATUS__SHIFT                                                         0x8
#define MMEA0_ERR_STATUS__SDP_RDRSP_DATAPARITY_ERROR__SHIFT                                                   0xa
#define MMEA0_ERR_STATUS__CLEAR_ERROR_STATUS__SHIFT                                                           0xb
#define MMEA0_ERR_STATUS__BUSY_ON_ERROR__SHIFT                                                                0xc
#define MMEA0_ERR_STATUS__FUE_FLAG__SHIFT                                                                     0xd
#define MMEA0_ERR_STATUS__SDP_RDRSP_STATUS_MASK                                                               0x0000000FL
#define MMEA0_ERR_STATUS__SDP_WRRSP_STATUS_MASK                                                               0x000000F0L
#define MMEA0_ERR_STATUS__SDP_RDRSP_DATASTATUS_MASK                                                           0x00000300L
#define MMEA0_ERR_STATUS__SDP_RDRSP_DATAPARITY_ERROR_MASK                                                     0x00000400L
#define MMEA0_ERR_STATUS__CLEAR_ERROR_STATUS_MASK                                                             0x00000800L
#define MMEA0_ERR_STATUS__BUSY_ON_ERROR_MASK                                                                  0x00001000L
#define MMEA0_ERR_STATUS__FUE_FLAG_MASK                                                                       0x00002000L
//MMEA1_SDP_ARB_FINAL
#define MMEA1_SDP_ARB_FINAL__DRAM_BURST_LIMIT__SHIFT                                                          0x0
#define MMEA1_SDP_ARB_FINAL__GMI_BURST_LIMIT__SHIFT                                                           0x5
#define MMEA1_SDP_ARB_FINAL__IO_BURST_LIMIT__SHIFT                                                            0xa
#define MMEA1_SDP_ARB_FINAL__BURST_LIMIT_MULTIPLIER__SHIFT                                                    0xf
#define MMEA1_SDP_ARB_FINAL__RDONLY_VC0__SHIFT                                                                0x11
#define MMEA1_SDP_ARB_FINAL__RDONLY_VC1__SHIFT                                                                0x12
#define MMEA1_SDP_ARB_FINAL__RDONLY_VC2__SHIFT                                                                0x13
#define MMEA1_SDP_ARB_FINAL__RDONLY_VC3__SHIFT                                                                0x14
#define MMEA1_SDP_ARB_FINAL__RDONLY_VC4__SHIFT                                                                0x15
#define MMEA1_SDP_ARB_FINAL__RDONLY_VC5__SHIFT                                                                0x16
#define MMEA1_SDP_ARB_FINAL__RDONLY_VC6__SHIFT                                                                0x17
#define MMEA1_SDP_ARB_FINAL__RDONLY_VC7__SHIFT                                                                0x18
#define MMEA1_SDP_ARB_FINAL__ERREVENT_ON_ERROR__SHIFT                                                         0x19
#define MMEA1_SDP_ARB_FINAL__HALTREQ_ON_ERROR__SHIFT                                                          0x1a
#define MMEA1_SDP_ARB_FINAL__DRAM_BURST_LIMIT_MASK                                                            0x0000001FL
#define MMEA1_SDP_ARB_FINAL__GMI_BURST_LIMIT_MASK                                                             0x000003E0L
#define MMEA1_SDP_ARB_FINAL__IO_BURST_LIMIT_MASK                                                              0x00007C00L
#define MMEA1_SDP_ARB_FINAL__BURST_LIMIT_MULTIPLIER_MASK                                                      0x00018000L
#define MMEA1_SDP_ARB_FINAL__RDONLY_VC0_MASK                                                                  0x00020000L
#define MMEA1_SDP_ARB_FINAL__RDONLY_VC1_MASK                                                                  0x00040000L
#define MMEA1_SDP_ARB_FINAL__RDONLY_VC2_MASK                                                                  0x00080000L
#define MMEA1_SDP_ARB_FINAL__RDONLY_VC3_MASK                                                                  0x00100000L
#define MMEA1_SDP_ARB_FINAL__RDONLY_VC4_MASK                                                                  0x00200000L
#define MMEA1_SDP_ARB_FINAL__RDONLY_VC5_MASK                                                                  0x00400000L
#define MMEA1_SDP_ARB_FINAL__RDONLY_VC6_MASK                                                                  0x00800000L
#define MMEA1_SDP_ARB_FINAL__RDONLY_VC7_MASK                                                                  0x01000000L
#define MMEA1_SDP_ARB_FINAL__ERREVENT_ON_ERROR_MASK                                                           0x02000000L
#define MMEA1_SDP_ARB_FINAL__HALTREQ_ON_ERROR_MASK                                                            0x04000000L
//MMEA1_EDC_CNT
#define MMEA1_EDC_CNT__DRAMRD_CMDMEM_SEC_COUNT__SHIFT                                                         0x0
#define MMEA1_EDC_CNT__DRAMRD_CMDMEM_DED_COUNT__SHIFT                                                         0x2
#define MMEA1_EDC_CNT__DRAMWR_CMDMEM_SEC_COUNT__SHIFT                                                         0x4
#define MMEA1_EDC_CNT__DRAMWR_CMDMEM_DED_COUNT__SHIFT                                                         0x6
#define MMEA1_EDC_CNT__DRAMWR_DATAMEM_SEC_COUNT__SHIFT                                                        0x8
#define MMEA1_EDC_CNT__DRAMWR_DATAMEM_DED_COUNT__SHIFT                                                        0xa
#define MMEA1_EDC_CNT__RRET_TAGMEM_SEC_COUNT__SHIFT                                                           0xc
#define MMEA1_EDC_CNT__RRET_TAGMEM_DED_COUNT__SHIFT                                                           0xe
#define MMEA1_EDC_CNT__WRET_TAGMEM_SEC_COUNT__SHIFT                                                           0x10
#define MMEA1_EDC_CNT__WRET_TAGMEM_DED_COUNT__SHIFT                                                           0x12
#define MMEA1_EDC_CNT__DRAMRD_PAGEMEM_SED_COUNT__SHIFT                                                        0x14
#define MMEA1_EDC_CNT__DRAMWR_PAGEMEM_SED_COUNT__SHIFT                                                        0x16
#define MMEA1_EDC_CNT__IORD_CMDMEM_SED_COUNT__SHIFT                                                           0x18
#define MMEA1_EDC_CNT__IOWR_CMDMEM_SED_COUNT__SHIFT                                                           0x1a
#define MMEA1_EDC_CNT__IOWR_DATAMEM_SED_COUNT__SHIFT                                                          0x1c
#define MMEA1_EDC_CNT__DRAMRD_CMDMEM_SEC_COUNT_MASK                                                           0x00000003L
#define MMEA1_EDC_CNT__DRAMRD_CMDMEM_DED_COUNT_MASK                                                           0x0000000CL
#define MMEA1_EDC_CNT__DRAMWR_CMDMEM_SEC_COUNT_MASK                                                           0x00000030L
#define MMEA1_EDC_CNT__DRAMWR_CMDMEM_DED_COUNT_MASK                                                           0x000000C0L
#define MMEA1_EDC_CNT__DRAMWR_DATAMEM_SEC_COUNT_MASK                                                          0x00000300L
#define MMEA1_EDC_CNT__DRAMWR_DATAMEM_DED_COUNT_MASK                                                          0x00000C00L
#define MMEA1_EDC_CNT__RRET_TAGMEM_SEC_COUNT_MASK                                                             0x00003000L
#define MMEA1_EDC_CNT__RRET_TAGMEM_DED_COUNT_MASK                                                             0x0000C000L
#define MMEA1_EDC_CNT__WRET_TAGMEM_SEC_COUNT_MASK                                                             0x00030000L
#define MMEA1_EDC_CNT__WRET_TAGMEM_DED_COUNT_MASK                                                             0x000C0000L
#define MMEA1_EDC_CNT__DRAMRD_PAGEMEM_SED_COUNT_MASK                                                          0x00300000L
#define MMEA1_EDC_CNT__DRAMWR_PAGEMEM_SED_COUNT_MASK                                                          0x00C00000L
#define MMEA1_EDC_CNT__IORD_CMDMEM_SED_COUNT_MASK                                                             0x03000000L
#define MMEA1_EDC_CNT__IOWR_CMDMEM_SED_COUNT_MASK                                                             0x0C000000L
#define MMEA1_EDC_CNT__IOWR_DATAMEM_SED_COUNT_MASK                                                            0x30000000L
//MMEA1_EDC_CNT2
#define MMEA1_EDC_CNT2__GMIRD_CMDMEM_SEC_COUNT__SHIFT                                                         0x0
#define MMEA1_EDC_CNT2__GMIRD_CMDMEM_DED_COUNT__SHIFT                                                         0x2
#define MMEA1_EDC_CNT2__GMIWR_CMDMEM_SEC_COUNT__SHIFT                                                         0x4
#define MMEA1_EDC_CNT2__GMIWR_CMDMEM_DED_COUNT__SHIFT                                                         0x6
#define MMEA1_EDC_CNT2__GMIWR_DATAMEM_SEC_COUNT__SHIFT                                                        0x8
#define MMEA1_EDC_CNT2__GMIWR_DATAMEM_DED_COUNT__SHIFT                                                        0xa
#define MMEA1_EDC_CNT2__GMIRD_PAGEMEM_SED_COUNT__SHIFT                                                        0xc
#define MMEA1_EDC_CNT2__GMIWR_PAGEMEM_SED_COUNT__SHIFT                                                        0xe
#define MMEA1_EDC_CNT2__MAM_D0MEM_SED_COUNT__SHIFT                                                            0x10
#define MMEA1_EDC_CNT2__MAM_D1MEM_SED_COUNT__SHIFT                                                            0x12
#define MMEA1_EDC_CNT2__MAM_D2MEM_SED_COUNT__SHIFT                                                            0x14
#define MMEA1_EDC_CNT2__MAM_D3MEM_SED_COUNT__SHIFT                                                            0x16
#define MMEA1_EDC_CNT2__GMIRD_CMDMEM_SEC_COUNT_MASK                                                           0x00000003L
#define MMEA1_EDC_CNT2__GMIRD_CMDMEM_DED_COUNT_MASK                                                           0x0000000CL
#define MMEA1_EDC_CNT2__GMIWR_CMDMEM_SEC_COUNT_MASK                                                           0x00000030L
#define MMEA1_EDC_CNT2__GMIWR_CMDMEM_DED_COUNT_MASK                                                           0x000000C0L
#define MMEA1_EDC_CNT2__GMIWR_DATAMEM_SEC_COUNT_MASK                                                          0x00000300L
#define MMEA1_EDC_CNT2__GMIWR_DATAMEM_DED_COUNT_MASK                                                          0x00000C00L
#define MMEA1_EDC_CNT2__GMIRD_PAGEMEM_SED_COUNT_MASK                                                          0x00003000L
#define MMEA1_EDC_CNT2__GMIWR_PAGEMEM_SED_COUNT_MASK                                                          0x0000C000L
#define MMEA1_EDC_CNT2__MAM_D0MEM_SED_COUNT_MASK                                                              0x00030000L
#define MMEA1_EDC_CNT2__MAM_D1MEM_SED_COUNT_MASK                                                              0x000C0000L
#define MMEA1_EDC_CNT2__MAM_D2MEM_SED_COUNT_MASK                                                              0x00300000L
#define MMEA1_EDC_CNT2__MAM_D3MEM_SED_COUNT_MASK                                                              0x00C00000L
//MMEA1_EDC_MODE
#define MMEA1_EDC_MODE__COUNT_FED_OUT__SHIFT                                                                  0x10
#define MMEA1_EDC_MODE__GATE_FUE__SHIFT                                                                       0x11
#define MMEA1_EDC_MODE__DED_MODE__SHIFT                                                                       0x14
#define MMEA1_EDC_MODE__PROP_FED__SHIFT                                                                       0x1d
#define MMEA1_EDC_MODE__BYPASS__SHIFT                                                                         0x1f
#define MMEA1_EDC_MODE__COUNT_FED_OUT_MASK                                                                    0x00010000L
#define MMEA1_EDC_MODE__GATE_FUE_MASK                                                                         0x00020000L
#define MMEA1_EDC_MODE__DED_MODE_MASK                                                                         0x00300000L
#define MMEA1_EDC_MODE__PROP_FED_MASK                                                                         0x20000000L
#define MMEA1_EDC_MODE__BYPASS_MASK                                                                           0x80000000L
//MMEA1_ERR_STATUS
#define MMEA1_ERR_STATUS__SDP_RDRSP_STATUS__SHIFT                                                             0x0
#define MMEA1_ERR_STATUS__SDP_WRRSP_STATUS__SHIFT                                                             0x4
#define MMEA1_ERR_STATUS__SDP_RDRSP_DATASTATUS__SHIFT                                                         0x8
#define MMEA1_ERR_STATUS__SDP_RDRSP_DATAPARITY_ERROR__SHIFT                                                   0xa
#define MMEA1_ERR_STATUS__CLEAR_ERROR_STATUS__SHIFT                                                           0xb
#define MMEA1_ERR_STATUS__BUSY_ON_ERROR__SHIFT                                                                0xc
#define MMEA1_ERR_STATUS__FUE_FLAG__SHIFT                                                                     0xd
#define MMEA1_ERR_STATUS__SDP_RDRSP_STATUS_MASK                                                               0x0000000FL
#define MMEA1_ERR_STATUS__SDP_WRRSP_STATUS_MASK                                                               0x000000F0L
#define MMEA1_ERR_STATUS__SDP_RDRSP_DATASTATUS_MASK                                                           0x00000300L
#define MMEA1_ERR_STATUS__SDP_RDRSP_DATAPARITY_ERROR_MASK                                                     0x00000400L
#define MMEA1_ERR_STATUS__CLEAR_ERROR_STATUS_MASK                                                             0x00000800L
#define MMEA1_ERR_STATUS__BUSY_ON_ERROR_MASK                                                                  0x00001000L
#define MMEA1_ERR_STATUS__FUE_FLAG_MASK                                                                       0x00002000L

// addressBlock: mmhub_utcl2_vmsharedpfdec
//MC_VM_XGMI_LFB_CNTL
#define MC_VM_XGMI_LFB_CNTL__PF_LFB_REGION__SHIFT                                                             0x0
#define MC_VM_XGMI_LFB_CNTL__PF_MAX_REGION__SHIFT                                                             0x4
#define MC_VM_XGMI_LFB_CNTL__PF_LFB_REGION_MASK                                                               0x00000007L
#define MC_VM_XGMI_LFB_CNTL__PF_MAX_REGION_MASK                                                               0x00000070L
//MC_VM_XGMI_LFB_SIZE
#define MC_VM_XGMI_LFB_SIZE__PF_LFB_SIZE__SHIFT                                                               0x0
#define MC_VM_XGMI_LFB_SIZE__PF_LFB_SIZE_MASK                                                                 0x0000FFFFL

#endif
