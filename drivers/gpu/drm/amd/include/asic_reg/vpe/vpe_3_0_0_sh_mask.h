/*
 * Copyright 2026 Advanced Micro Devices, Inc.
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
#ifndef _vpe_3_0_0_SH_MASK_HEADER
#define _vpe_3_0_0_SH_MASK_HEADER


// addressBlock: vpe_vpec_vpecdec
//VPEC_UCODE_ADDR
#define VPEC_UCODE_ADDR__VALUE__SHIFT                                                                         0x0
#define VPEC_UCODE_ADDR__THID__SHIFT                                                                          0xf
#define VPEC_UCODE_ADDR__VALUE_MASK                                                                           0x00003FFFL
#define VPEC_UCODE_ADDR__THID_MASK                                                                            0x00008000L
//VPEC_UCODE_DATA
#define VPEC_UCODE_DATA__VALUE__SHIFT                                                                         0x0
#define VPEC_UCODE_DATA__VALUE_MASK                                                                           0xFFFFFFFFL
//VPEC_F32_CNTL
#define VPEC_F32_CNTL__HALT__SHIFT                                                                            0x0
#define VPEC_F32_CNTL__DBG_SELECT_BITS__SHIFT                                                                 0x2
#define VPEC_F32_CNTL__TH0_CHECKSUM_CLR__SHIFT                                                                0x8
#define VPEC_F32_CNTL__TH0_RESET__SHIFT                                                                       0x9
#define VPEC_F32_CNTL__TH0_ENABLE__SHIFT                                                                      0xa
#define VPEC_F32_CNTL__TH1_CHECKSUM_CLR__SHIFT                                                                0xc
#define VPEC_F32_CNTL__TH1_RESET__SHIFT                                                                       0xd
#define VPEC_F32_CNTL__TH1_ENABLE__SHIFT                                                                      0xe
#define VPEC_F32_CNTL__TH0_PRIORITY__SHIFT                                                                    0x10
#define VPEC_F32_CNTL__TH1_PRIORITY__SHIFT                                                                    0x18
#define VPEC_F32_CNTL__HALT_MASK                                                                              0x00000001L
#define VPEC_F32_CNTL__DBG_SELECT_BITS_MASK                                                                   0x000000FCL
#define VPEC_F32_CNTL__TH0_CHECKSUM_CLR_MASK                                                                  0x00000100L
#define VPEC_F32_CNTL__TH0_RESET_MASK                                                                         0x00000200L
#define VPEC_F32_CNTL__TH0_ENABLE_MASK                                                                        0x00000400L
#define VPEC_F32_CNTL__TH1_CHECKSUM_CLR_MASK                                                                  0x00001000L
#define VPEC_F32_CNTL__TH1_RESET_MASK                                                                         0x00002000L
#define VPEC_F32_CNTL__TH1_ENABLE_MASK                                                                        0x00004000L
#define VPEC_F32_CNTL__TH0_PRIORITY_MASK                                                                      0x00FF0000L
#define VPEC_F32_CNTL__TH1_PRIORITY_MASK                                                                      0xFF000000L
//VPEC_CNTL
#define VPEC_CNTL__TRAP_ENABLE__SHIFT                                                                         0x0
#define VPEC_CNTL__RESERVED_2_2__SHIFT                                                                        0x2
#define VPEC_CNTL__DATA_SWAP__SHIFT                                                                           0x3
#define VPEC_CNTL__FENCE_SWAP_ENABLE__SHIFT                                                                   0x5
#define VPEC_CNTL__MIDCMD_PREEMPT_ENABLE__SHIFT                                                               0x6
#define VPEC_CNTL__MIDCMD_EXPIRE_ENABLE__SHIFT                                                                0x9
#define VPEC_CNTL__UMSCH_INT_ENABLE__SHIFT                                                                    0xa
#define VPEC_CNTL__RESERVED_13_11__SHIFT                                                                      0xb
#define VPEC_CNTL__NACK_GEN_ERR_INT_ENABLE__SHIFT                                                             0xe
#define VPEC_CNTL__NACK_PRT_INT_ENABLE__SHIFT                                                                 0xf
#define VPEC_CNTL__RESERVED_16_16__SHIFT                                                                      0x10
#define VPEC_CNTL__MIDCMD_WORLDSWITCH_ENABLE__SHIFT                                                           0x11
#define VPEC_CNTL__RESERVED_19_19__SHIFT                                                                      0x13
#define VPEC_CNTL__CTXEMPTY_INT_ENABLE__SHIFT                                                                 0x1c
#define VPEC_CNTL__FROZEN_INT_ENABLE__SHIFT                                                                   0x1d
#define VPEC_CNTL__IB_PREEMPT_INT_ENABLE__SHIFT                                                               0x1e
#define VPEC_CNTL__RB_PREEMPT_INT_ENABLE__SHIFT                                                               0x1f
#define VPEC_CNTL__TRAP_ENABLE_MASK                                                                           0x00000001L
#define VPEC_CNTL__RESERVED_2_2_MASK                                                                          0x00000004L
#define VPEC_CNTL__DATA_SWAP_MASK                                                                             0x00000018L
#define VPEC_CNTL__FENCE_SWAP_ENABLE_MASK                                                                     0x00000020L
#define VPEC_CNTL__MIDCMD_PREEMPT_ENABLE_MASK                                                                 0x00000040L
#define VPEC_CNTL__MIDCMD_EXPIRE_ENABLE_MASK                                                                  0x00000200L
#define VPEC_CNTL__UMSCH_INT_ENABLE_MASK                                                                      0x00000400L
#define VPEC_CNTL__RESERVED_13_11_MASK                                                                        0x00003800L
#define VPEC_CNTL__NACK_GEN_ERR_INT_ENABLE_MASK                                                               0x00004000L
#define VPEC_CNTL__NACK_PRT_INT_ENABLE_MASK                                                                   0x00008000L
#define VPEC_CNTL__RESERVED_16_16_MASK                                                                        0x00010000L
#define VPEC_CNTL__MIDCMD_WORLDSWITCH_ENABLE_MASK                                                             0x00020000L
#define VPEC_CNTL__RESERVED_19_19_MASK                                                                        0x00080000L
#define VPEC_CNTL__CTXEMPTY_INT_ENABLE_MASK                                                                   0x10000000L
#define VPEC_CNTL__FROZEN_INT_ENABLE_MASK                                                                     0x20000000L
#define VPEC_CNTL__IB_PREEMPT_INT_ENABLE_MASK                                                                 0x40000000L
#define VPEC_CNTL__RB_PREEMPT_INT_ENABLE_MASK                                                                 0x80000000L
//VPEC_CNTL_DCC
#define VPEC_CNTL_DCC__WDCC_COMP_MODE__SHIFT                                                                  0x0
#define VPEC_CNTL_DCC__RESERVED_3_2__SHIFT                                                                    0x2
#define VPEC_CNTL_DCC__WDCC_MICRO_TILE_MODE__SHIFT                                                            0x4
#define VPEC_CNTL_DCC__RESERVED_7_6__SHIFT                                                                    0x7
#define VPEC_CNTL_DCC__WDCC_DATA_FORMAT__SHIFT                                                                0x8
#define VPEC_CNTL_DCC__RESERVED_15_13__SHIFT                                                                  0xe
#define VPEC_CNTL_DCC__WDCC_NUM_FORMAT_EN__SHIFT                                                              0x10
#define VPEC_CNTL_DCC__RESERVED_19_17__SHIFT                                                                  0x11
#define VPEC_CNTL_DCC__WDCC_NUM_TYPE__SHIFT                                                                   0x14
#define VPEC_CNTL_DCC__RESERVED_23_23__SHIFT                                                                  0x17
#define VPEC_CNTL_DCC__WDCC_MAX_UNCOMP_SIZE__SHIFT                                                            0x18
#define VPEC_CNTL_DCC__WDCC_MAX_COMP_SIZE__SHIFT                                                              0x19
#define VPEC_CNTL_DCC__RESERVED_30_27__SHIFT                                                                  0x1b
#define VPEC_CNTL_DCC__RDCC_COMP_MODE__SHIFT                                                                  0x1f
#define VPEC_CNTL_DCC__WDCC_COMP_MODE_MASK                                                                    0x00000003L
#define VPEC_CNTL_DCC__RESERVED_3_2_MASK                                                                      0x0000000CL
#define VPEC_CNTL_DCC__WDCC_MICRO_TILE_MODE_MASK                                                              0x00000070L
#define VPEC_CNTL_DCC__RESERVED_7_6_MASK                                                                      0x00000080L
#define VPEC_CNTL_DCC__WDCC_DATA_FORMAT_MASK                                                                  0x00003F00L
#define VPEC_CNTL_DCC__RESERVED_15_13_MASK                                                                    0x0000C000L
#define VPEC_CNTL_DCC__WDCC_NUM_FORMAT_EN_MASK                                                                0x00010000L
#define VPEC_CNTL_DCC__RESERVED_19_17_MASK                                                                    0x000E0000L
#define VPEC_CNTL_DCC__WDCC_NUM_TYPE_MASK                                                                     0x00700000L
#define VPEC_CNTL_DCC__RESERVED_23_23_MASK                                                                    0x00800000L
#define VPEC_CNTL_DCC__WDCC_MAX_UNCOMP_SIZE_MASK                                                              0x01000000L
#define VPEC_CNTL_DCC__WDCC_MAX_COMP_SIZE_MASK                                                                0x06000000L
#define VPEC_CNTL_DCC__RESERVED_30_27_MASK                                                                    0x78000000L
#define VPEC_CNTL_DCC__RDCC_COMP_MODE_MASK                                                                    0x80000000L
//VPEC_CNTL1
#define VPEC_CNTL1__RESERVED_3_1__SHIFT                                                                       0x1
#define VPEC_CNTL1__SRBM_POLL_RETRYING__SHIFT                                                                 0x5
#define VPEC_CNTL1__RESERVED_23_10__SHIFT                                                                     0xa
#define VPEC_CNTL1__CG_STATUS_OUTPUT__SHIFT                                                                   0x18
#define VPEC_CNTL1__SW_FREEZE_ENABLE__SHIFT                                                                   0x19
#define VPEC_CNTL1__VPEP_CONFIG_INVALID_CHECK_ENABLE__SHIFT                                                   0x1a
#define VPEC_CNTL1__RSMU_ACCESS_OFF_VPEP_RETURN_ERROR_ENABLE__SHIFT                                           0x1b
#define VPEC_CNTL1__RSMU_ACCESS_OFF_VPEP_REPORT_ERROR_ENABLE__SHIFT                                           0x1c
#define VPEC_CNTL1__RESERVED__SHIFT                                                                           0x1d
#define VPEC_CNTL1__RESERVED_3_1_MASK                                                                         0x0000000EL
#define VPEC_CNTL1__SRBM_POLL_RETRYING_MASK                                                                   0x00000020L
#define VPEC_CNTL1__RESERVED_23_10_MASK                                                                       0x00FFFC00L
#define VPEC_CNTL1__CG_STATUS_OUTPUT_MASK                                                                     0x01000000L
#define VPEC_CNTL1__SW_FREEZE_ENABLE_MASK                                                                     0x02000000L
#define VPEC_CNTL1__VPEP_CONFIG_INVALID_CHECK_ENABLE_MASK                                                     0x04000000L
#define VPEC_CNTL1__RSMU_ACCESS_OFF_VPEP_RETURN_ERROR_ENABLE_MASK                                             0x08000000L
#define VPEC_CNTL1__RSMU_ACCESS_OFF_VPEP_REPORT_ERROR_ENABLE_MASK                                             0x10000000L
#define VPEC_CNTL1__RESERVED_MASK                                                                             0xE0000000L
//VPEC_CNTL2
#define VPEC_CNTL2__F32_CMD_PROC_DELAY__SHIFT                                                                 0x0
#define VPEC_CNTL2__F32_SEND_POSTCODE_EN__SHIFT                                                               0x4
#define VPEC_CNTL2__UCODE_BUF_DS_EN__SHIFT                                                                    0x6
#define VPEC_CNTL2__UCODE_SELFLOAD_THREAD_OVERLAP__SHIFT                                                      0x7
#define VPEC_CNTL2__LUTIB_FIFO_WATERMARK__SHIFT                                                               0x8
#define VPEC_CNTL2__CMDIB_FIFO_WATERMARK__SHIFT                                                               0xa
#define VPEC_CNTL2__RESERVED_14_12__SHIFT                                                                     0xc
#define VPEC_CNTL2__IMPROVE_CE_IP_ARBITER__SHIFT                                                              0xf
#define VPEC_CNTL2__RB_FIFO_WATERMARK__SHIFT                                                                  0x10
#define VPEC_CNTL2__IB_FIFO_WATERMARK__SHIFT                                                                  0x12
#define VPEC_CNTL2__RESERVED_22_20__SHIFT                                                                     0x14
#define VPEC_CNTL2__CH_RD_WATERMARK__SHIFT                                                                    0x17
#define VPEC_CNTL2__CH_WR_WATERMARK__SHIFT                                                                    0x19
#define VPEC_CNTL2__CH_WR_WATERMARK_LSB__SHIFT                                                                0x1e
#define VPEC_CNTL2__F32_CMD_PROC_DELAY_MASK                                                                   0x0000000FL
#define VPEC_CNTL2__F32_SEND_POSTCODE_EN_MASK                                                                 0x00000010L
#define VPEC_CNTL2__UCODE_BUF_DS_EN_MASK                                                                      0x00000040L
#define VPEC_CNTL2__UCODE_SELFLOAD_THREAD_OVERLAP_MASK                                                        0x00000080L
#define VPEC_CNTL2__LUTIB_FIFO_WATERMARK_MASK                                                                 0x00000300L
#define VPEC_CNTL2__CMDIB_FIFO_WATERMARK_MASK                                                                 0x00000C00L
#define VPEC_CNTL2__RESERVED_14_12_MASK                                                                       0x00007000L
#define VPEC_CNTL2__IMPROVE_CE_IP_ARBITER_MASK                                                                0x00008000L
#define VPEC_CNTL2__RB_FIFO_WATERMARK_MASK                                                                    0x00030000L
#define VPEC_CNTL2__IB_FIFO_WATERMARK_MASK                                                                    0x000C0000L
#define VPEC_CNTL2__RESERVED_22_20_MASK                                                                       0x00700000L
#define VPEC_CNTL2__CH_RD_WATERMARK_MASK                                                                      0x01800000L
#define VPEC_CNTL2__CH_WR_WATERMARK_MASK                                                                      0x3E000000L
#define VPEC_CNTL2__CH_WR_WATERMARK_LSB_MASK                                                                  0x40000000L
//VPEC_QUEUE_RESET_REQ
#define VPEC_QUEUE_RESET_REQ__QUEUE0_RESET__SHIFT                                                             0x0
#define VPEC_QUEUE_RESET_REQ__QUEUE1_RESET__SHIFT                                                             0x1
#define VPEC_QUEUE_RESET_REQ__QUEUE2_RESET__SHIFT                                                             0x2
#define VPEC_QUEUE_RESET_REQ__QUEUE3_RESET__SHIFT                                                             0x3
#define VPEC_QUEUE_RESET_REQ__QUEUE4_RESET__SHIFT                                                             0x4
#define VPEC_QUEUE_RESET_REQ__QUEUE5_RESET__SHIFT                                                             0x5
#define VPEC_QUEUE_RESET_REQ__QUEUE6_RESET__SHIFT                                                             0x6
#define VPEC_QUEUE_RESET_REQ__QUEUE7_RESET__SHIFT                                                             0x7
#define VPEC_QUEUE_RESET_REQ__RESERVED__SHIFT                                                                 0x8
#define VPEC_QUEUE_RESET_REQ__QUEUE0_RESET_MASK                                                               0x00000001L
#define VPEC_QUEUE_RESET_REQ__QUEUE1_RESET_MASK                                                               0x00000002L
#define VPEC_QUEUE_RESET_REQ__QUEUE2_RESET_MASK                                                               0x00000004L
#define VPEC_QUEUE_RESET_REQ__QUEUE3_RESET_MASK                                                               0x00000008L
#define VPEC_QUEUE_RESET_REQ__QUEUE4_RESET_MASK                                                               0x00000010L
#define VPEC_QUEUE_RESET_REQ__QUEUE5_RESET_MASK                                                               0x00000020L
#define VPEC_QUEUE_RESET_REQ__QUEUE6_RESET_MASK                                                               0x00000040L
#define VPEC_QUEUE_RESET_REQ__QUEUE7_RESET_MASK                                                               0x00000080L
#define VPEC_QUEUE_RESET_REQ__RESERVED_MASK                                                                   0xFFFFFF00L
//VPEC_PUB_DUMMY0
#define VPEC_PUB_DUMMY0__VALUE__SHIFT                                                                         0x0
#define VPEC_PUB_DUMMY0__VALUE_MASK                                                                           0xFFFFFFFFL
//VPEC_PUB_DUMMY1
#define VPEC_PUB_DUMMY1__VALUE__SHIFT                                                                         0x0
#define VPEC_PUB_DUMMY1__VALUE_MASK                                                                           0xFFFFFFFFL
//VPEC_PUB_DUMMY2
#define VPEC_PUB_DUMMY2__VALUE__SHIFT                                                                         0x0
#define VPEC_PUB_DUMMY2__VALUE_MASK                                                                           0xFFFFFFFFL
//VPEC_PUB_DUMMY3
#define VPEC_PUB_DUMMY3__VALUE__SHIFT                                                                         0x0
#define VPEC_PUB_DUMMY3__VALUE_MASK                                                                           0xFFFFFFFFL
//VPEC_PUB_DUMMY4
#define VPEC_PUB_DUMMY4__VALUE__SHIFT                                                                         0x0
#define VPEC_PUB_DUMMY4__VALUE_MASK                                                                           0xFFFFFFFFL
//VPEC_PUB_DUMMY5
#define VPEC_PUB_DUMMY5__VALUE__SHIFT                                                                         0x0
#define VPEC_PUB_DUMMY5__VALUE_MASK                                                                           0xFFFFFFFFL
//VPEC_PUB_DUMMY6
#define VPEC_PUB_DUMMY6__VALUE__SHIFT                                                                         0x0
#define VPEC_PUB_DUMMY6__VALUE_MASK                                                                           0xFFFFFFFFL
//VPEC_PUB_DUMMY7
#define VPEC_PUB_DUMMY7__VALUE__SHIFT                                                                         0x0
#define VPEC_PUB_DUMMY7__VALUE_MASK                                                                           0xFFFFFFFFL
//VPEC_PUB_DUMMY8
#define VPEC_PUB_DUMMY8__VALUE__SHIFT                                                                         0x0
#define VPEC_PUB_DUMMY8__VALUE_MASK                                                                           0xFFFFFFFFL
//VPEC_PUB_DUMMY9
#define VPEC_PUB_DUMMY9__VALUE__SHIFT                                                                         0x0
#define VPEC_PUB_DUMMY9__VALUE_MASK                                                                           0xFFFFFFFFL
//VPEC_PUB_DUMMY10
#define VPEC_PUB_DUMMY10__VALUE__SHIFT                                                                        0x0
#define VPEC_PUB_DUMMY10__VALUE_MASK                                                                          0xFFFFFFFFL
//VPEC_PUB_DUMMY11
#define VPEC_PUB_DUMMY11__VALUE__SHIFT                                                                        0x0
#define VPEC_PUB_DUMMY11__VALUE_MASK                                                                          0xFFFFFFFFL
//VPEC_UCODE1_CHECKSUM
#define VPEC_UCODE1_CHECKSUM__DATA__SHIFT                                                                     0x0
#define VPEC_UCODE1_CHECKSUM__DATA_MASK                                                                       0xFFFFFFFFL
//VPEC_UCODE_CHECKSUM
#define VPEC_UCODE_CHECKSUM__DATA__SHIFT                                                                      0x0
#define VPEC_UCODE_CHECKSUM__DATA_MASK                                                                        0xFFFFFFFFL
//VPEC_STATUS
#define VPEC_STATUS__IDLE__SHIFT                                                                              0x0
#define VPEC_STATUS__REG_IDLE__SHIFT                                                                          0x1
#define VPEC_STATUS__RB_EMPTY__SHIFT                                                                          0x2
#define VPEC_STATUS__RB_FULL__SHIFT                                                                           0x3
#define VPEC_STATUS__RB_CMD_IDLE__SHIFT                                                                       0x4
#define VPEC_STATUS__RB_CMD_FULL__SHIFT                                                                       0x5
#define VPEC_STATUS__IB_CMD_IDLE__SHIFT                                                                       0x6
#define VPEC_STATUS__IB_CMD_FULL__SHIFT                                                                       0x7
#define VPEC_STATUS__BLOCK_IDLE__SHIFT                                                                        0x8
#define VPEC_STATUS__INSIDE_VPEP_CONFIG__SHIFT                                                                0x9
#define VPEC_STATUS__EX_IDLE__SHIFT                                                                           0xa
#define VPEC_STATUS__INSIDE_VPEP_3DLUT_CONFIG__SHIFT                                                          0xb
#define VPEC_STATUS__PACKET_READY__SHIFT                                                                      0xc
#define VPEC_STATUS__MC_WR_IDLE__SHIFT                                                                        0xd
#define VPEC_STATUS__SRBM_IDLE__SHIFT                                                                         0xe
#define VPEC_STATUS__CONTEXT_EMPTY__SHIFT                                                                     0xf
#define VPEC_STATUS__INSIDE_IB__SHIFT                                                                         0x10
#define VPEC_STATUS__RB_MC_RREQ_IDLE__SHIFT                                                                   0x11
#define VPEC_STATUS__IB_MC_RREQ_IDLE__SHIFT                                                                   0x12
#define VPEC_STATUS__MC_RD_IDLE__SHIFT                                                                        0x13
#define VPEC_STATUS__DELTA_RPTR_EMPTY__SHIFT                                                                  0x14
#define VPEC_STATUS__MC_RD_RET_STALL__SHIFT                                                                   0x15
#define VPEC_STATUS__LUTIB_CMD_IDLE__SHIFT                                                                    0x16
#define VPEC_STATUS__LUTIB_CMD_FULL__SHIFT                                                                    0x17
#define VPEC_STATUS__CMDIB_MC_RREQ_IDLE__SHIFT                                                                0x18
#define VPEC_STATUS__PREV_CMD_IDLE__SHIFT                                                                     0x19
#define VPEC_STATUS__CMDIB_CMD_IDLE__SHIFT                                                                    0x1a
#define VPEC_STATUS__CMDIB_CMD_FULL__SHIFT                                                                    0x1b
#define VPEC_STATUS__RESERVED_29_28__SHIFT                                                                    0x1c
#define VPEC_STATUS__INT_IDLE__SHIFT                                                                          0x1e
#define VPEC_STATUS__INT_REQ_STALL__SHIFT                                                                     0x1f
#define VPEC_STATUS__IDLE_MASK                                                                                0x00000001L
#define VPEC_STATUS__REG_IDLE_MASK                                                                            0x00000002L
#define VPEC_STATUS__RB_EMPTY_MASK                                                                            0x00000004L
#define VPEC_STATUS__RB_FULL_MASK                                                                             0x00000008L
#define VPEC_STATUS__RB_CMD_IDLE_MASK                                                                         0x00000010L
#define VPEC_STATUS__RB_CMD_FULL_MASK                                                                         0x00000020L
#define VPEC_STATUS__IB_CMD_IDLE_MASK                                                                         0x00000040L
#define VPEC_STATUS__IB_CMD_FULL_MASK                                                                         0x00000080L
#define VPEC_STATUS__BLOCK_IDLE_MASK                                                                          0x00000100L
#define VPEC_STATUS__INSIDE_VPEP_CONFIG_MASK                                                                  0x00000200L
#define VPEC_STATUS__EX_IDLE_MASK                                                                             0x00000400L
#define VPEC_STATUS__INSIDE_VPEP_3DLUT_CONFIG_MASK                                                            0x00000800L
#define VPEC_STATUS__PACKET_READY_MASK                                                                        0x00001000L
#define VPEC_STATUS__MC_WR_IDLE_MASK                                                                          0x00002000L
#define VPEC_STATUS__SRBM_IDLE_MASK                                                                           0x00004000L
#define VPEC_STATUS__CONTEXT_EMPTY_MASK                                                                       0x00008000L
#define VPEC_STATUS__INSIDE_IB_MASK                                                                           0x00010000L
#define VPEC_STATUS__RB_MC_RREQ_IDLE_MASK                                                                     0x00020000L
#define VPEC_STATUS__IB_MC_RREQ_IDLE_MASK                                                                     0x00040000L
#define VPEC_STATUS__MC_RD_IDLE_MASK                                                                          0x00080000L
#define VPEC_STATUS__DELTA_RPTR_EMPTY_MASK                                                                    0x00100000L
#define VPEC_STATUS__MC_RD_RET_STALL_MASK                                                                     0x00200000L
#define VPEC_STATUS__LUTIB_CMD_IDLE_MASK                                                                      0x00400000L
#define VPEC_STATUS__LUTIB_CMD_FULL_MASK                                                                      0x00800000L
#define VPEC_STATUS__CMDIB_MC_RREQ_IDLE_MASK                                                                  0x01000000L
#define VPEC_STATUS__PREV_CMD_IDLE_MASK                                                                       0x02000000L
#define VPEC_STATUS__CMDIB_CMD_IDLE_MASK                                                                      0x04000000L
#define VPEC_STATUS__CMDIB_CMD_FULL_MASK                                                                      0x08000000L
#define VPEC_STATUS__RESERVED_29_28_MASK                                                                      0x30000000L
#define VPEC_STATUS__INT_IDLE_MASK                                                                            0x40000000L
#define VPEC_STATUS__INT_REQ_STALL_MASK                                                                       0x80000000L
//VPEC_STATUS1
#define VPEC_STATUS1__EX_START__SHIFT                                                                         0x0
#define VPEC_STATUS1__VPEC_IDLE__SHIFT                                                                        0x1
#define VPEC_STATUS1__RESERVED_31_2__SHIFT                                                                    0x2
#define VPEC_STATUS1__EX_START_MASK                                                                           0x00000001L
#define VPEC_STATUS1__VPEC_IDLE_MASK                                                                          0x00000002L
#define VPEC_STATUS1__RESERVED_31_2_MASK                                                                      0xFFFFFFFCL
//VPEC_STATUS2
#define VPEC_STATUS2__ID__SHIFT                                                                               0x0
#define VPEC_STATUS2__TH0F32_INSTR_PTR__SHIFT                                                                 0x2
#define VPEC_STATUS2__CMD_OP__SHIFT                                                                           0x10
#define VPEC_STATUS2__ID_MASK                                                                                 0x00000003L
#define VPEC_STATUS2__TH0F32_INSTR_PTR_MASK                                                                   0x0000FFFCL
#define VPEC_STATUS2__CMD_OP_MASK                                                                             0xFFFF0000L
//VPEC_STATUS3
#define VPEC_STATUS3__RESERVED_15_0__SHIFT                                                                    0x0
#define VPEC_STATUS3__RESERVED_19_16__SHIFT                                                                   0x10
#define VPEC_STATUS3__EXCEPTION_IDLE__SHIFT                                                                   0x14
#define VPEC_STATUS3__RESERVED_21_21__SHIFT                                                                   0x15
#define VPEC_STATUS3__RESERVED_22_22__SHIFT                                                                   0x16
#define VPEC_STATUS3__RESERVED_23_23__SHIFT                                                                   0x17
#define VPEC_STATUS3__RESERVED_24_24__SHIFT                                                                   0x18
#define VPEC_STATUS3__RESERVED_25_25__SHIFT                                                                   0x19
#define VPEC_STATUS3__INT_QUEUE_ID__SHIFT                                                                     0x1a
#define VPEC_STATUS3__RESERVED_31_30__SHIFT                                                                   0x1e
#define VPEC_STATUS3__RESERVED_15_0_MASK                                                                      0x0000FFFFL
#define VPEC_STATUS3__RESERVED_19_16_MASK                                                                     0x000F0000L
#define VPEC_STATUS3__EXCEPTION_IDLE_MASK                                                                     0x00100000L
#define VPEC_STATUS3__RESERVED_21_21_MASK                                                                     0x00200000L
#define VPEC_STATUS3__RESERVED_22_22_MASK                                                                     0x00400000L
#define VPEC_STATUS3__RESERVED_23_23_MASK                                                                     0x00800000L
#define VPEC_STATUS3__RESERVED_24_24_MASK                                                                     0x01000000L
#define VPEC_STATUS3__RESERVED_25_25_MASK                                                                     0x02000000L
#define VPEC_STATUS3__INT_QUEUE_ID_MASK                                                                       0x3C000000L
#define VPEC_STATUS3__RESERVED_31_30_MASK                                                                     0xC0000000L
//VPEC_STATUS4
#define VPEC_STATUS4__IDLE__SHIFT                                                                             0x0
#define VPEC_STATUS4__IH_OUTSTANDING__SHIFT                                                                   0x2
#define VPEC_STATUS4__RESERVED_3_3__SHIFT                                                                     0x3
#define VPEC_STATUS4__CH_RD_OUTSTANDING__SHIFT                                                                0x4
#define VPEC_STATUS4__CH_WR_OUTSTANDING__SHIFT                                                                0x5
#define VPEC_STATUS4__RESERVED_6_6__SHIFT                                                                     0x6
#define VPEC_STATUS4__RESERVED_7_7__SHIFT                                                                     0x7
#define VPEC_STATUS4__RESERVED_8_8__SHIFT                                                                     0x8
#define VPEC_STATUS4__RESERVED_9_9__SHIFT                                                                     0x9
#define VPEC_STATUS4__REG_POLLING__SHIFT                                                                      0xa
#define VPEC_STATUS4__MEM_POLLING__SHIFT                                                                      0xb
#define VPEC_STATUS4__VPEP_REG_RD_OUTSTANDING__SHIFT                                                          0xc
#define VPEC_STATUS4__VPEP_REG_WR_OUTSTANDING__SHIFT                                                          0xd
#define VPEC_STATUS4__RESERVED_15_14__SHIFT                                                                   0xe
#define VPEC_STATUS4__ACTIVE_QUEUE_ID__SHIFT                                                                  0x10
#define VPEC_STATUS4__RESERVED_27_20__SHIFT                                                                   0x14
#define VPEC_STATUS4__IDLE_MASK                                                                               0x00000001L
#define VPEC_STATUS4__IH_OUTSTANDING_MASK                                                                     0x00000004L
#define VPEC_STATUS4__RESERVED_3_3_MASK                                                                       0x00000008L
#define VPEC_STATUS4__CH_RD_OUTSTANDING_MASK                                                                  0x00000010L
#define VPEC_STATUS4__CH_WR_OUTSTANDING_MASK                                                                  0x00000020L
#define VPEC_STATUS4__RESERVED_6_6_MASK                                                                       0x00000040L
#define VPEC_STATUS4__RESERVED_7_7_MASK                                                                       0x00000080L
#define VPEC_STATUS4__RESERVED_8_8_MASK                                                                       0x00000100L
#define VPEC_STATUS4__RESERVED_9_9_MASK                                                                       0x00000200L
#define VPEC_STATUS4__REG_POLLING_MASK                                                                        0x00000400L
#define VPEC_STATUS4__MEM_POLLING_MASK                                                                        0x00000800L
#define VPEC_STATUS4__VPEP_REG_RD_OUTSTANDING_MASK                                                            0x00001000L
#define VPEC_STATUS4__VPEP_REG_WR_OUTSTANDING_MASK                                                            0x00002000L
#define VPEC_STATUS4__RESERVED_15_14_MASK                                                                     0x0000C000L
#define VPEC_STATUS4__ACTIVE_QUEUE_ID_MASK                                                                    0x000F0000L
#define VPEC_STATUS4__RESERVED_27_20_MASK                                                                     0x0FF00000L
//VPEC_STATUS5
#define VPEC_STATUS5__QUEUE0_RB_ENABLE_STATUS__SHIFT                                                          0x0
#define VPEC_STATUS5__QUEUE1_RB_ENABLE_STATUS__SHIFT                                                          0x1
#define VPEC_STATUS5__QUEUE2_RB_ENABLE_STATUS__SHIFT                                                          0x2
#define VPEC_STATUS5__QUEUE3_RB_ENABLE_STATUS__SHIFT                                                          0x3
#define VPEC_STATUS5__QUEUE4_RB_ENABLE_STATUS__SHIFT                                                          0x4
#define VPEC_STATUS5__QUEUE5_RB_ENABLE_STATUS__SHIFT                                                          0x5
#define VPEC_STATUS5__QUEUE6_RB_ENABLE_STATUS__SHIFT                                                          0x6
#define VPEC_STATUS5__QUEUE7_RB_ENABLE_STATUS__SHIFT                                                          0x7
#define VPEC_STATUS5__RESERVED_27_16__SHIFT                                                                   0x10
#define VPEC_STATUS5__QUEUE0_RB_ENABLE_STATUS_MASK                                                            0x00000001L
#define VPEC_STATUS5__QUEUE1_RB_ENABLE_STATUS_MASK                                                            0x00000002L
#define VPEC_STATUS5__QUEUE2_RB_ENABLE_STATUS_MASK                                                            0x00000004L
#define VPEC_STATUS5__QUEUE3_RB_ENABLE_STATUS_MASK                                                            0x00000008L
#define VPEC_STATUS5__QUEUE4_RB_ENABLE_STATUS_MASK                                                            0x00000010L
#define VPEC_STATUS5__QUEUE5_RB_ENABLE_STATUS_MASK                                                            0x00000020L
#define VPEC_STATUS5__QUEUE6_RB_ENABLE_STATUS_MASK                                                            0x00000040L
#define VPEC_STATUS5__QUEUE7_RB_ENABLE_STATUS_MASK                                                            0x00000080L
#define VPEC_STATUS5__RESERVED_27_16_MASK                                                                     0x000F0000L
//VPEC_STATUS6
#define VPEC_STATUS6__ID__SHIFT                                                                               0x0
#define VPEC_STATUS6__TH1F32_INSTR_PTR__SHIFT                                                                 0x2
#define VPEC_STATUS6__TH1_EXCEPTION__SHIFT                                                                    0x10
#define VPEC_STATUS6__ID_MASK                                                                                 0x00000003L
#define VPEC_STATUS6__TH1F32_INSTR_PTR_MASK                                                                   0x0000FFFCL
#define VPEC_STATUS6__TH1_EXCEPTION_MASK                                                                      0xFFFF0000L
//VPEC_STATUS7
#define VPEC_STATUS7__TH0_DBG_STATUS__SHIFT                                                                   0x0
#define VPEC_STATUS7__TH0_DBG_STATUS_MASK                                                                     0xFFFFFFFFL
//VPEC_STATUS8
#define VPEC_STATUS8__CE_IP0_WREQ_IDLE__SHIFT                                                                 0x0
#define VPEC_STATUS8__CE_IP0_WR_IDLE__SHIFT                                                                   0x1
#define VPEC_STATUS8__CE_IP0_SPLIT_RD_IDLE__SHIFT                                                             0x2
#define VPEC_STATUS8__CE_IP0_SPLIT_WR_IDLE__SHIFT                                                             0x3
#define VPEC_STATUS8__CE_IP0_RREQ_IDLE__SHIFT                                                                 0x4
#define VPEC_STATUS8__CE_IP0_OUT_IDLE__SHIFT                                                                  0x5
#define VPEC_STATUS8__CE_IP0_IN_IDLE__SHIFT                                                                   0x6
#define VPEC_STATUS8__CE_IP0_DST_IDLE__SHIFT                                                                  0x7
#define VPEC_STATUS8__CE_IP0_CMD_IDLE__SHIFT                                                                  0x8
#define VPEC_STATUS8__CE_IP1_WREQ_IDLE__SHIFT                                                                 0x9
#define VPEC_STATUS8__CE_IP1_WR_IDLE__SHIFT                                                                   0xa
#define VPEC_STATUS8__CE_IP1_SPLIT_RD_IDLE__SHIFT                                                             0xb
#define VPEC_STATUS8__CE_IP1_SPLIT_WR_IDLE__SHIFT                                                             0xc
#define VPEC_STATUS8__CE_IP1_RREQ_IDLE__SHIFT                                                                 0xd
#define VPEC_STATUS8__CE_IP1_OUT_IDLE__SHIFT                                                                  0xe
#define VPEC_STATUS8__CE_IP1_IN_IDLE__SHIFT                                                                   0xf
#define VPEC_STATUS8__CE_IP1_DST_IDLE__SHIFT                                                                  0x10
#define VPEC_STATUS8__CE_IP1_CMD_IDLE__SHIFT                                                                  0x11
#define VPEC_STATUS8__CE_IP0_AFIFO_FULL__SHIFT                                                                0x12
#define VPEC_STATUS8__CE_IP0_CMD_INFO_FULL__SHIFT                                                             0x13
#define VPEC_STATUS8__CE_IP0_CMD_INFO1_FULL__SHIFT                                                            0x14
#define VPEC_STATUS8__CE_IP1_AFIFO_FULL__SHIFT                                                                0x15
#define VPEC_STATUS8__CE_IP1_CMD_INFO_FULL__SHIFT                                                             0x16
#define VPEC_STATUS8__CE_IP1_CMD_INFO1_FULL__SHIFT                                                            0x17
#define VPEC_STATUS8__CE_IP0_WR_STALL__SHIFT                                                                  0x18
#define VPEC_STATUS8__CE_IP1_WR_STALL__SHIFT                                                                  0x19
#define VPEC_STATUS8__CE_IP0_RD_STALL__SHIFT                                                                  0x1a
#define VPEC_STATUS8__CE_IP1_RD_STALL__SHIFT                                                                  0x1b
#define VPEC_STATUS8__RESERVED_31_28__SHIFT                                                                   0x1c
#define VPEC_STATUS8__CE_IP0_WREQ_IDLE_MASK                                                                   0x00000001L
#define VPEC_STATUS8__CE_IP0_WR_IDLE_MASK                                                                     0x00000002L
#define VPEC_STATUS8__CE_IP0_SPLIT_RD_IDLE_MASK                                                               0x00000004L
#define VPEC_STATUS8__CE_IP0_SPLIT_WR_IDLE_MASK                                                               0x00000008L
#define VPEC_STATUS8__CE_IP0_RREQ_IDLE_MASK                                                                   0x00000010L
#define VPEC_STATUS8__CE_IP0_OUT_IDLE_MASK                                                                    0x00000020L
#define VPEC_STATUS8__CE_IP0_IN_IDLE_MASK                                                                     0x00000040L
#define VPEC_STATUS8__CE_IP0_DST_IDLE_MASK                                                                    0x00000080L
#define VPEC_STATUS8__CE_IP0_CMD_IDLE_MASK                                                                    0x00000100L
#define VPEC_STATUS8__CE_IP1_WREQ_IDLE_MASK                                                                   0x00000200L
#define VPEC_STATUS8__CE_IP1_WR_IDLE_MASK                                                                     0x00000400L
#define VPEC_STATUS8__CE_IP1_SPLIT_RD_IDLE_MASK                                                               0x00000800L
#define VPEC_STATUS8__CE_IP1_SPLIT_WR_IDLE_MASK                                                               0x00001000L
#define VPEC_STATUS8__CE_IP1_RREQ_IDLE_MASK                                                                   0x00002000L
#define VPEC_STATUS8__CE_IP1_OUT_IDLE_MASK                                                                    0x00004000L
#define VPEC_STATUS8__CE_IP1_IN_IDLE_MASK                                                                     0x00008000L
#define VPEC_STATUS8__CE_IP1_DST_IDLE_MASK                                                                    0x00010000L
#define VPEC_STATUS8__CE_IP1_CMD_IDLE_MASK                                                                    0x00020000L
#define VPEC_STATUS8__CE_IP0_AFIFO_FULL_MASK                                                                  0x00040000L
#define VPEC_STATUS8__CE_IP0_CMD_INFO_FULL_MASK                                                               0x00080000L
#define VPEC_STATUS8__CE_IP0_CMD_INFO1_FULL_MASK                                                              0x00100000L
#define VPEC_STATUS8__CE_IP1_AFIFO_FULL_MASK                                                                  0x00200000L
#define VPEC_STATUS8__CE_IP1_CMD_INFO_FULL_MASK                                                               0x00400000L
#define VPEC_STATUS8__CE_IP1_CMD_INFO1_FULL_MASK                                                              0x00800000L
#define VPEC_STATUS8__CE_IP0_WR_STALL_MASK                                                                    0x01000000L
#define VPEC_STATUS8__CE_IP1_WR_STALL_MASK                                                                    0x02000000L
#define VPEC_STATUS8__CE_IP0_RD_STALL_MASK                                                                    0x04000000L
#define VPEC_STATUS8__CE_IP1_RD_STALL_MASK                                                                    0x08000000L
#define VPEC_STATUS8__RESERVED_31_28_MASK                                                                     0xF0000000L
//VPEC_STATUS9
#define VPEC_STATUS9__CE_IP2_WREQ_IDLE__SHIFT                                                                 0x0
#define VPEC_STATUS9__CE_IP2_WR_IDLE__SHIFT                                                                   0x1
#define VPEC_STATUS9__CE_IP2_SPLIT_RD_IDLE__SHIFT                                                             0x2
#define VPEC_STATUS9__CE_IP2_SPLIT_WR_IDLE__SHIFT                                                             0x3
#define VPEC_STATUS9__CE_IP2_RREQ_IDLE__SHIFT                                                                 0x4
#define VPEC_STATUS9__CE_IP2_OUT_IDLE__SHIFT                                                                  0x5
#define VPEC_STATUS9__CE_IP2_IN_IDLE__SHIFT                                                                   0x6
#define VPEC_STATUS9__CE_IP2_DST_IDLE__SHIFT                                                                  0x7
#define VPEC_STATUS9__CE_IP2_CMD_IDLE__SHIFT                                                                  0x8
#define VPEC_STATUS9__CE_IP3_WREQ_IDLE__SHIFT                                                                 0x9
#define VPEC_STATUS9__CE_IP3_WR_IDLE__SHIFT                                                                   0xa
#define VPEC_STATUS9__CE_IP3_SPLIT_RD_IDLE__SHIFT                                                             0xb
#define VPEC_STATUS9__CE_IP3_SPLIT_WR_IDLE__SHIFT                                                             0xc
#define VPEC_STATUS9__CE_IP3_RREQ_IDLE__SHIFT                                                                 0xd
#define VPEC_STATUS9__CE_IP3_OUT_IDLE__SHIFT                                                                  0xe
#define VPEC_STATUS9__CE_IP3_IN_IDLE__SHIFT                                                                   0xf
#define VPEC_STATUS9__CE_IP3_DST_IDLE__SHIFT                                                                  0x10
#define VPEC_STATUS9__CE_IP3_CMD_IDLE__SHIFT                                                                  0x11
#define VPEC_STATUS9__CE_IP2_AFIFO_FULL__SHIFT                                                                0x12
#define VPEC_STATUS9__CE_IP2_CMD_INFO_FULL__SHIFT                                                             0x13
#define VPEC_STATUS9__CE_IP2_CMD_INFO1_FULL__SHIFT                                                            0x14
#define VPEC_STATUS9__CE_IP3_AFIFO_FULL__SHIFT                                                                0x15
#define VPEC_STATUS9__CE_IP3_CMD_INFO_FULL__SHIFT                                                             0x16
#define VPEC_STATUS9__CE_IP3_CMD_INFO1_FULL__SHIFT                                                            0x17
#define VPEC_STATUS9__CE_IP2_WR_STALL__SHIFT                                                                  0x18
#define VPEC_STATUS9__CE_IP3_WR_STALL__SHIFT                                                                  0x19
#define VPEC_STATUS9__CE_IP2_RD_STALL__SHIFT                                                                  0x1a
#define VPEC_STATUS9__CE_IP3_RD_STALL__SHIFT                                                                  0x1b
#define VPEC_STATUS9__RESERVED_31_28__SHIFT                                                                   0x1c
#define VPEC_STATUS9__CE_IP2_WREQ_IDLE_MASK                                                                   0x00000001L
#define VPEC_STATUS9__CE_IP2_WR_IDLE_MASK                                                                     0x00000002L
#define VPEC_STATUS9__CE_IP2_SPLIT_RD_IDLE_MASK                                                               0x00000004L
#define VPEC_STATUS9__CE_IP2_SPLIT_WR_IDLE_MASK                                                               0x00000008L
#define VPEC_STATUS9__CE_IP2_RREQ_IDLE_MASK                                                                   0x00000010L
#define VPEC_STATUS9__CE_IP2_OUT_IDLE_MASK                                                                    0x00000020L
#define VPEC_STATUS9__CE_IP2_IN_IDLE_MASK                                                                     0x00000040L
#define VPEC_STATUS9__CE_IP2_DST_IDLE_MASK                                                                    0x00000080L
#define VPEC_STATUS9__CE_IP2_CMD_IDLE_MASK                                                                    0x00000100L
#define VPEC_STATUS9__CE_IP3_WREQ_IDLE_MASK                                                                   0x00000200L
#define VPEC_STATUS9__CE_IP3_WR_IDLE_MASK                                                                     0x00000400L
#define VPEC_STATUS9__CE_IP3_SPLIT_RD_IDLE_MASK                                                               0x00000800L
#define VPEC_STATUS9__CE_IP3_SPLIT_WR_IDLE_MASK                                                               0x00001000L
#define VPEC_STATUS9__CE_IP3_RREQ_IDLE_MASK                                                                   0x00002000L
#define VPEC_STATUS9__CE_IP3_OUT_IDLE_MASK                                                                    0x00004000L
#define VPEC_STATUS9__CE_IP3_IN_IDLE_MASK                                                                     0x00008000L
#define VPEC_STATUS9__CE_IP3_DST_IDLE_MASK                                                                    0x00010000L
#define VPEC_STATUS9__CE_IP3_CMD_IDLE_MASK                                                                    0x00020000L
#define VPEC_STATUS9__CE_IP2_AFIFO_FULL_MASK                                                                  0x00040000L
#define VPEC_STATUS9__CE_IP2_CMD_INFO_FULL_MASK                                                               0x00080000L
#define VPEC_STATUS9__CE_IP2_CMD_INFO1_FULL_MASK                                                              0x00100000L
#define VPEC_STATUS9__CE_IP3_AFIFO_FULL_MASK                                                                  0x00200000L
#define VPEC_STATUS9__CE_IP3_CMD_INFO_FULL_MASK                                                               0x00400000L
#define VPEC_STATUS9__CE_IP3_CMD_INFO1_FULL_MASK                                                              0x00800000L
#define VPEC_STATUS9__CE_IP2_WR_STALL_MASK                                                                    0x01000000L
#define VPEC_STATUS9__CE_IP3_WR_STALL_MASK                                                                    0x02000000L
#define VPEC_STATUS9__CE_IP2_RD_STALL_MASK                                                                    0x04000000L
#define VPEC_STATUS9__CE_IP3_RD_STALL_MASK                                                                    0x08000000L
#define VPEC_STATUS9__RESERVED_31_28_MASK                                                                     0xF0000000L
//VPEC_STATUS10
#define VPEC_STATUS10__CE_OP0_WR_IDLE__SHIFT                                                                  0x0
#define VPEC_STATUS10__CE_OP0_CMD_IDLE__SHIFT                                                                 0x1
#define VPEC_STATUS10__CE_OP1_WR_IDLE__SHIFT                                                                  0x2
#define VPEC_STATUS10__CE_OP1_CMD_IDLE__SHIFT                                                                 0x3
#define VPEC_STATUS10__CE_OP2_WR_IDLE__SHIFT                                                                  0x4
#define VPEC_STATUS10__CE_OP2_CMD_IDLE__SHIFT                                                                 0x5
#define VPEC_STATUS10__CE_OP3_WR_IDLE__SHIFT                                                                  0x6
#define VPEC_STATUS10__CE_OP3_CMD_IDLE__SHIFT                                                                 0x7
#define VPEC_STATUS10__CE_OP4_WR_IDLE__SHIFT                                                                  0x8
#define VPEC_STATUS10__CE_OP4_CMD_IDLE__SHIFT                                                                 0x9
#define VPEC_STATUS10__CE_OP5_WR_IDLE__SHIFT                                                                  0xa
#define VPEC_STATUS10__CE_OP5_CMD_IDLE__SHIFT                                                                 0xb
#define VPEC_STATUS10__CE_OP6_WR_IDLE__SHIFT                                                                  0xc
#define VPEC_STATUS10__CE_OP6_CMD_IDLE__SHIFT                                                                 0xd
#define VPEC_STATUS10__CE_OP7_WR_IDLE__SHIFT                                                                  0xe
#define VPEC_STATUS10__CE_OP7_CMD_IDLE__SHIFT                                                                 0xf
#define VPEC_STATUS10__CE_OP8_WR_IDLE__SHIFT                                                                  0x10
#define VPEC_STATUS10__CE_OP8_CMD_IDLE__SHIFT                                                                 0x11
#define VPEC_STATUS10__CE_OP9_WR_IDLE__SHIFT                                                                  0x12
#define VPEC_STATUS10__CE_OP9_CMD_IDLE__SHIFT                                                                 0x13
#define VPEC_STATUS10__CE_OP10_WR_IDLE__SHIFT                                                                 0x14
#define VPEC_STATUS10__CE_OP10_CMD_IDLE__SHIFT                                                                0x15
#define VPEC_STATUS10__RESERVED_31_28__SHIFT                                                                  0x1c
#define VPEC_STATUS10__CE_OP0_WR_IDLE_MASK                                                                    0x00000001L
#define VPEC_STATUS10__CE_OP0_CMD_IDLE_MASK                                                                   0x00000002L
#define VPEC_STATUS10__CE_OP1_WR_IDLE_MASK                                                                    0x00000004L
#define VPEC_STATUS10__CE_OP1_CMD_IDLE_MASK                                                                   0x00000008L
#define VPEC_STATUS10__CE_OP2_WR_IDLE_MASK                                                                    0x00000010L
#define VPEC_STATUS10__CE_OP2_CMD_IDLE_MASK                                                                   0x00000020L
#define VPEC_STATUS10__CE_OP3_WR_IDLE_MASK                                                                    0x00000040L
#define VPEC_STATUS10__CE_OP3_CMD_IDLE_MASK                                                                   0x00000080L
#define VPEC_STATUS10__CE_OP4_WR_IDLE_MASK                                                                    0x00000100L
#define VPEC_STATUS10__CE_OP4_CMD_IDLE_MASK                                                                   0x00000200L
#define VPEC_STATUS10__CE_OP5_WR_IDLE_MASK                                                                    0x00000400L
#define VPEC_STATUS10__CE_OP5_CMD_IDLE_MASK                                                                   0x00000800L
#define VPEC_STATUS10__CE_OP6_WR_IDLE_MASK                                                                    0x00001000L
#define VPEC_STATUS10__CE_OP6_CMD_IDLE_MASK                                                                   0x00002000L
#define VPEC_STATUS10__CE_OP7_WR_IDLE_MASK                                                                    0x00004000L
#define VPEC_STATUS10__CE_OP7_CMD_IDLE_MASK                                                                   0x00008000L
#define VPEC_STATUS10__CE_OP8_WR_IDLE_MASK                                                                    0x00010000L
#define VPEC_STATUS10__CE_OP8_CMD_IDLE_MASK                                                                   0x00020000L
#define VPEC_STATUS10__CE_OP9_WR_IDLE_MASK                                                                    0x00040000L
#define VPEC_STATUS10__CE_OP9_CMD_IDLE_MASK                                                                   0x00080000L
#define VPEC_STATUS10__CE_OP10_WR_IDLE_MASK                                                                   0x00100000L
#define VPEC_STATUS10__CE_OP10_CMD_IDLE_MASK                                                                  0x00200000L
#define VPEC_STATUS10__RESERVED_31_28_MASK                                                                    0xF0000000L
//VPEC_STATUS11
#define VPEC_STATUS11__CE_IP4_WREQ_IDLE__SHIFT                                                                0x0
#define VPEC_STATUS11__CE_IP4_WR_IDLE__SHIFT                                                                  0x1
#define VPEC_STATUS11__CE_IP4_SPLIT_RD_IDLE__SHIFT                                                            0x2
#define VPEC_STATUS11__CE_IP4_SPLIT_WR_IDLE__SHIFT                                                            0x3
#define VPEC_STATUS11__CE_IP4_RREQ_IDLE__SHIFT                                                                0x4
#define VPEC_STATUS11__CE_IP4_OUT_IDLE__SHIFT                                                                 0x5
#define VPEC_STATUS11__CE_IP4_IN_IDLE__SHIFT                                                                  0x6
#define VPEC_STATUS11__CE_IP4_DST_IDLE__SHIFT                                                                 0x7
#define VPEC_STATUS11__CE_IP4_CMD_IDLE__SHIFT                                                                 0x8
#define VPEC_STATUS11__CE_IP5_WREQ_IDLE__SHIFT                                                                0x9
#define VPEC_STATUS11__CE_IP5_WR_IDLE__SHIFT                                                                  0xa
#define VPEC_STATUS11__CE_IP5_SPLIT_RD_IDLE__SHIFT                                                            0xb
#define VPEC_STATUS11__CE_IP5_SPLIT_WR_IDLE__SHIFT                                                            0xc
#define VPEC_STATUS11__CE_IP5_RREQ_IDLE__SHIFT                                                                0xd
#define VPEC_STATUS11__CE_IP5_OUT_IDLE__SHIFT                                                                 0xe
#define VPEC_STATUS11__CE_IP5_IN_IDLE__SHIFT                                                                  0xf
#define VPEC_STATUS11__CE_IP5_DST_IDLE__SHIFT                                                                 0x10
#define VPEC_STATUS11__CE_IP5_CMD_IDLE__SHIFT                                                                 0x11
#define VPEC_STATUS11__CE_IP4_AFIFO_FULL__SHIFT                                                               0x12
#define VPEC_STATUS11__CE_IP4_CMD_INFO_FULL__SHIFT                                                            0x13
#define VPEC_STATUS11__CE_IP4_CMD_INFO1_FULL__SHIFT                                                           0x14
#define VPEC_STATUS11__CE_IP5_AFIFO_FULL__SHIFT                                                               0x15
#define VPEC_STATUS11__CE_IP5_CMD_INFO_FULL__SHIFT                                                            0x16
#define VPEC_STATUS11__CE_IP5_CMD_INFO1_FULL__SHIFT                                                           0x17
#define VPEC_STATUS11__CE_IP4_WR_STALL__SHIFT                                                                 0x18
#define VPEC_STATUS11__CE_IP5_WR_STALL__SHIFT                                                                 0x19
#define VPEC_STATUS11__CE_IP4_RD_STALL__SHIFT                                                                 0x1a
#define VPEC_STATUS11__CE_IP5_RD_STALL__SHIFT                                                                 0x1b
#define VPEC_STATUS11__RESERVED_31_28__SHIFT                                                                  0x1c
#define VPEC_STATUS11__CE_IP4_WREQ_IDLE_MASK                                                                  0x00000001L
#define VPEC_STATUS11__CE_IP4_WR_IDLE_MASK                                                                    0x00000002L
#define VPEC_STATUS11__CE_IP4_SPLIT_RD_IDLE_MASK                                                              0x00000004L
#define VPEC_STATUS11__CE_IP4_SPLIT_WR_IDLE_MASK                                                              0x00000008L
#define VPEC_STATUS11__CE_IP4_RREQ_IDLE_MASK                                                                  0x00000010L
#define VPEC_STATUS11__CE_IP4_OUT_IDLE_MASK                                                                   0x00000020L
#define VPEC_STATUS11__CE_IP4_IN_IDLE_MASK                                                                    0x00000040L
#define VPEC_STATUS11__CE_IP4_DST_IDLE_MASK                                                                   0x00000080L
#define VPEC_STATUS11__CE_IP4_CMD_IDLE_MASK                                                                   0x00000100L
#define VPEC_STATUS11__CE_IP5_WREQ_IDLE_MASK                                                                  0x00000200L
#define VPEC_STATUS11__CE_IP5_WR_IDLE_MASK                                                                    0x00000400L
#define VPEC_STATUS11__CE_IP5_SPLIT_RD_IDLE_MASK                                                              0x00000800L
#define VPEC_STATUS11__CE_IP5_SPLIT_WR_IDLE_MASK                                                              0x00001000L
#define VPEC_STATUS11__CE_IP5_RREQ_IDLE_MASK                                                                  0x00002000L
#define VPEC_STATUS11__CE_IP5_OUT_IDLE_MASK                                                                   0x00004000L
#define VPEC_STATUS11__CE_IP5_IN_IDLE_MASK                                                                    0x00008000L
#define VPEC_STATUS11__CE_IP5_DST_IDLE_MASK                                                                   0x00010000L
#define VPEC_STATUS11__CE_IP5_CMD_IDLE_MASK                                                                   0x00020000L
#define VPEC_STATUS11__CE_IP4_AFIFO_FULL_MASK                                                                 0x00040000L
#define VPEC_STATUS11__CE_IP4_CMD_INFO_FULL_MASK                                                              0x00080000L
#define VPEC_STATUS11__CE_IP4_CMD_INFO1_FULL_MASK                                                             0x00100000L
#define VPEC_STATUS11__CE_IP5_AFIFO_FULL_MASK                                                                 0x00200000L
#define VPEC_STATUS11__CE_IP5_CMD_INFO_FULL_MASK                                                              0x00400000L
#define VPEC_STATUS11__CE_IP5_CMD_INFO1_FULL_MASK                                                             0x00800000L
#define VPEC_STATUS11__CE_IP4_WR_STALL_MASK                                                                   0x01000000L
#define VPEC_STATUS11__CE_IP5_WR_STALL_MASK                                                                   0x02000000L
#define VPEC_STATUS11__CE_IP4_RD_STALL_MASK                                                                   0x04000000L
#define VPEC_STATUS11__CE_IP5_RD_STALL_MASK                                                                   0x08000000L
#define VPEC_STATUS11__RESERVED_31_28_MASK                                                                    0xF0000000L
//VPEC_STATUS12
#define VPEC_STATUS12__CE_IP6_WREQ_IDLE__SHIFT                                                                0x0
#define VPEC_STATUS12__CE_IP6_WR_IDLE__SHIFT                                                                  0x1
#define VPEC_STATUS12__CE_IP6_SPLIT_RD_IDLE__SHIFT                                                            0x2
#define VPEC_STATUS12__CE_IP6_SPLIT_WR_IDLE__SHIFT                                                            0x3
#define VPEC_STATUS12__CE_IP6_RREQ_IDLE__SHIFT                                                                0x4
#define VPEC_STATUS12__CE_IP6_OUT_IDLE__SHIFT                                                                 0x5
#define VPEC_STATUS12__CE_IP6_IN_IDLE__SHIFT                                                                  0x6
#define VPEC_STATUS12__CE_IP6_DST_IDLE__SHIFT                                                                 0x7
#define VPEC_STATUS12__CE_IP6_CMD_IDLE__SHIFT                                                                 0x8
#define VPEC_STATUS12__CE_IP7_WREQ_IDLE__SHIFT                                                                0x9
#define VPEC_STATUS12__CE_IP7_WR_IDLE__SHIFT                                                                  0xa
#define VPEC_STATUS12__CE_IP7_SPLIT_RD_IDLE__SHIFT                                                            0xb
#define VPEC_STATUS12__CE_IP7_SPLIT_WR_IDLE__SHIFT                                                            0xc
#define VPEC_STATUS12__CE_IP7_RREQ_IDLE__SHIFT                                                                0xd
#define VPEC_STATUS12__CE_IP7_OUT_IDLE__SHIFT                                                                 0xe
#define VPEC_STATUS12__CE_IP7_IN_IDLE__SHIFT                                                                  0xf
#define VPEC_STATUS12__CE_IP7_DST_IDLE__SHIFT                                                                 0x10
#define VPEC_STATUS12__CE_IP7_CMD_IDLE__SHIFT                                                                 0x11
#define VPEC_STATUS12__CE_IP6_AFIFO_FULL__SHIFT                                                               0x12
#define VPEC_STATUS12__CE_IP6_CMD_INFO_FULL__SHIFT                                                            0x13
#define VPEC_STATUS12__CE_IP6_CMD_INFO1_FULL__SHIFT                                                           0x14
#define VPEC_STATUS12__CE_IP7_AFIFO_FULL__SHIFT                                                               0x15
#define VPEC_STATUS12__CE_IP7_CMD_INFO_FULL__SHIFT                                                            0x16
#define VPEC_STATUS12__CE_IP7_CMD_INFO1_FULL__SHIFT                                                           0x17
#define VPEC_STATUS12__CE_IP6_WR_STALL__SHIFT                                                                 0x18
#define VPEC_STATUS12__CE_IP7_WR_STALL__SHIFT                                                                 0x19
#define VPEC_STATUS12__CE_IP6_RD_STALL__SHIFT                                                                 0x1a
#define VPEC_STATUS12__CE_IP7_RD_STALL__SHIFT                                                                 0x1b
#define VPEC_STATUS12__RESERVED_31_28__SHIFT                                                                  0x1c
#define VPEC_STATUS12__CE_IP6_WREQ_IDLE_MASK                                                                  0x00000001L
#define VPEC_STATUS12__CE_IP6_WR_IDLE_MASK                                                                    0x00000002L
#define VPEC_STATUS12__CE_IP6_SPLIT_RD_IDLE_MASK                                                              0x00000004L
#define VPEC_STATUS12__CE_IP6_SPLIT_WR_IDLE_MASK                                                              0x00000008L
#define VPEC_STATUS12__CE_IP6_RREQ_IDLE_MASK                                                                  0x00000010L
#define VPEC_STATUS12__CE_IP6_OUT_IDLE_MASK                                                                   0x00000020L
#define VPEC_STATUS12__CE_IP6_IN_IDLE_MASK                                                                    0x00000040L
#define VPEC_STATUS12__CE_IP6_DST_IDLE_MASK                                                                   0x00000080L
#define VPEC_STATUS12__CE_IP6_CMD_IDLE_MASK                                                                   0x00000100L
#define VPEC_STATUS12__CE_IP7_WREQ_IDLE_MASK                                                                  0x00000200L
#define VPEC_STATUS12__CE_IP7_WR_IDLE_MASK                                                                    0x00000400L
#define VPEC_STATUS12__CE_IP7_SPLIT_RD_IDLE_MASK                                                              0x00000800L
#define VPEC_STATUS12__CE_IP7_SPLIT_WR_IDLE_MASK                                                              0x00001000L
#define VPEC_STATUS12__CE_IP7_RREQ_IDLE_MASK                                                                  0x00002000L
#define VPEC_STATUS12__CE_IP7_OUT_IDLE_MASK                                                                   0x00004000L
#define VPEC_STATUS12__CE_IP7_IN_IDLE_MASK                                                                    0x00008000L
#define VPEC_STATUS12__CE_IP7_DST_IDLE_MASK                                                                   0x00010000L
#define VPEC_STATUS12__CE_IP7_CMD_IDLE_MASK                                                                   0x00020000L
#define VPEC_STATUS12__CE_IP6_AFIFO_FULL_MASK                                                                 0x00040000L
#define VPEC_STATUS12__CE_IP6_CMD_INFO_FULL_MASK                                                              0x00080000L
#define VPEC_STATUS12__CE_IP6_CMD_INFO1_FULL_MASK                                                             0x00100000L
#define VPEC_STATUS12__CE_IP7_AFIFO_FULL_MASK                                                                 0x00200000L
#define VPEC_STATUS12__CE_IP7_CMD_INFO_FULL_MASK                                                              0x00400000L
#define VPEC_STATUS12__CE_IP7_CMD_INFO1_FULL_MASK                                                             0x00800000L
#define VPEC_STATUS12__CE_IP6_WR_STALL_MASK                                                                   0x01000000L
#define VPEC_STATUS12__CE_IP7_WR_STALL_MASK                                                                   0x02000000L
#define VPEC_STATUS12__CE_IP6_RD_STALL_MASK                                                                   0x04000000L
#define VPEC_STATUS12__CE_IP7_RD_STALL_MASK                                                                   0x08000000L
#define VPEC_STATUS12__RESERVED_31_28_MASK                                                                    0xF0000000L
//VPEC_STATUS13
#define VPEC_STATUS13__CE_IP8_WREQ_IDLE__SHIFT                                                                0x0
#define VPEC_STATUS13__CE_IP8_WR_IDLE__SHIFT                                                                  0x1
#define VPEC_STATUS13__CE_IP8_SPLIT_RD_IDLE__SHIFT                                                            0x2
#define VPEC_STATUS13__CE_IP8_SPLIT_WR_IDLE__SHIFT                                                            0x3
#define VPEC_STATUS13__CE_IP8_RREQ_IDLE__SHIFT                                                                0x4
#define VPEC_STATUS13__CE_IP8_OUT_IDLE__SHIFT                                                                 0x5
#define VPEC_STATUS13__CE_IP8_IN_IDLE__SHIFT                                                                  0x6
#define VPEC_STATUS13__CE_IP8_DST_IDLE__SHIFT                                                                 0x7
#define VPEC_STATUS13__CE_IP8_CMD_IDLE__SHIFT                                                                 0x8
#define VPEC_STATUS13__CE_IP8_AFIFO_FULL__SHIFT                                                               0x12
#define VPEC_STATUS13__CE_IP8_CMD_INFO_FULL__SHIFT                                                            0x13
#define VPEC_STATUS13__CE_IP8_CMD_INFO1_FULL__SHIFT                                                           0x14
#define VPEC_STATUS13__CE_IP8_WR_STALL__SHIFT                                                                 0x18
#define VPEC_STATUS13__CE_IP8_RD_STALL__SHIFT                                                                 0x1a
#define VPEC_STATUS13__RESERVED_31_28__SHIFT                                                                  0x1c
#define VPEC_STATUS13__CE_IP8_WREQ_IDLE_MASK                                                                  0x00000001L
#define VPEC_STATUS13__CE_IP8_WR_IDLE_MASK                                                                    0x00000002L
#define VPEC_STATUS13__CE_IP8_SPLIT_RD_IDLE_MASK                                                              0x00000004L
#define VPEC_STATUS13__CE_IP8_SPLIT_WR_IDLE_MASK                                                              0x00000008L
#define VPEC_STATUS13__CE_IP8_RREQ_IDLE_MASK                                                                  0x00000010L
#define VPEC_STATUS13__CE_IP8_OUT_IDLE_MASK                                                                   0x00000020L
#define VPEC_STATUS13__CE_IP8_IN_IDLE_MASK                                                                    0x00000040L
#define VPEC_STATUS13__CE_IP8_DST_IDLE_MASK                                                                   0x00000080L
#define VPEC_STATUS13__CE_IP8_CMD_IDLE_MASK                                                                   0x00000100L
#define VPEC_STATUS13__CE_IP8_AFIFO_FULL_MASK                                                                 0x00040000L
#define VPEC_STATUS13__CE_IP8_CMD_INFO_FULL_MASK                                                              0x00080000L
#define VPEC_STATUS13__CE_IP8_CMD_INFO1_FULL_MASK                                                             0x00100000L
#define VPEC_STATUS13__CE_IP8_WR_STALL_MASK                                                                   0x01000000L
#define VPEC_STATUS13__CE_IP8_RD_STALL_MASK                                                                   0x04000000L
#define VPEC_STATUS13__RESERVED_31_28_MASK                                                                    0xF0000000L
//VPEC_QUEUE_STATUS0
#define VPEC_QUEUE_STATUS0__QUEUE0_STATUS__SHIFT                                                              0x0
#define VPEC_QUEUE_STATUS0__QUEUE1_STATUS__SHIFT                                                              0x4
#define VPEC_QUEUE_STATUS0__QUEUE2_STATUS__SHIFT                                                              0x8
#define VPEC_QUEUE_STATUS0__QUEUE3_STATUS__SHIFT                                                              0xc
#define VPEC_QUEUE_STATUS0__QUEUE4_STATUS__SHIFT                                                              0x10
#define VPEC_QUEUE_STATUS0__QUEUE5_STATUS__SHIFT                                                              0x14
#define VPEC_QUEUE_STATUS0__QUEUE6_STATUS__SHIFT                                                              0x18
#define VPEC_QUEUE_STATUS0__QUEUE7_STATUS__SHIFT                                                              0x1c
#define VPEC_QUEUE_STATUS0__QUEUE0_STATUS_MASK                                                                0x0000000FL
#define VPEC_QUEUE_STATUS0__QUEUE1_STATUS_MASK                                                                0x000000F0L
#define VPEC_QUEUE_STATUS0__QUEUE2_STATUS_MASK                                                                0x00000F00L
#define VPEC_QUEUE_STATUS0__QUEUE3_STATUS_MASK                                                                0x0000F000L
#define VPEC_QUEUE_STATUS0__QUEUE4_STATUS_MASK                                                                0x000F0000L
#define VPEC_QUEUE_STATUS0__QUEUE5_STATUS_MASK                                                                0x00F00000L
#define VPEC_QUEUE_STATUS0__QUEUE6_STATUS_MASK                                                                0x0F000000L
#define VPEC_QUEUE_STATUS0__QUEUE7_STATUS_MASK                                                                0xF0000000L
//VPEC_QUEUE_HANG_STATUS
#define VPEC_QUEUE_HANG_STATUS__F30T0_HANG__SHIFT                                                             0x0
#define VPEC_QUEUE_HANG_STATUS__CE_HANG__SHIFT                                                                0x1
#define VPEC_QUEUE_HANG_STATUS__EOF_MISMATCH__SHIFT                                                           0x2
#define VPEC_QUEUE_HANG_STATUS__INVALID_PKT_FIELD__SHIFT                                                      0x3
#define VPEC_QUEUE_HANG_STATUS__INVALID_VPEP_CONFIG_ADDR__SHIFT                                               0x4
#define VPEC_QUEUE_HANG_STATUS__F32_ACCESS_OFF_VPDPP1__SHIFT                                                  0x5
#define VPEC_QUEUE_HANG_STATUS__RSMU_ACCESS_OFF_VPDPP1__SHIFT                                                 0x6
#define VPEC_QUEUE_HANG_STATUS__EOH_MISMATCH__SHIFT                                                           0x7
#define VPEC_QUEUE_HANG_STATUS__F30T0_HANG_MASK                                                               0x00000001L
#define VPEC_QUEUE_HANG_STATUS__CE_HANG_MASK                                                                  0x00000002L
#define VPEC_QUEUE_HANG_STATUS__EOF_MISMATCH_MASK                                                             0x00000004L
#define VPEC_QUEUE_HANG_STATUS__INVALID_PKT_FIELD_MASK                                                        0x00000008L
#define VPEC_QUEUE_HANG_STATUS__INVALID_VPEP_CONFIG_ADDR_MASK                                                 0x00000010L
#define VPEC_QUEUE_HANG_STATUS__F32_ACCESS_OFF_VPDPP1_MASK                                                    0x00000020L
#define VPEC_QUEUE_HANG_STATUS__RSMU_ACCESS_OFF_VPDPP1_MASK                                                   0x00000040L
#define VPEC_QUEUE_HANG_STATUS__EOH_MISMATCH_MASK                                                             0x00000080L
//VPEC_PG_CNTL
#define VPEC_PG_CNTL__PG_EN__SHIFT                                                                            0x0
#define VPEC_PG_CNTL__PG_HYSTERESIS__SHIFT                                                                    0x1
#define VPEC_PG_CNTL__PG1_EN__SHIFT                                                                           0x8
#define VPEC_PG_CNTL__PG1_HYSTERESIS__SHIFT                                                                   0x9
#define VPEC_PG_CNTL__ZSTATES_ENABLE__SHIFT                                                                   0x10
#define VPEC_PG_CNTL__ZSTATES_HYSTERESIS__SHIFT                                                               0x11
#define VPEC_PG_CNTL__FENCE_HYSTERESIS__SHIFT                                                                 0x18
#define VPEC_PG_CNTL__CHECK_RSMU_UPON_POWER_UP__SHIFT                                                         0x1c
#define VPEC_PG_CNTL__PG_EN_MASK                                                                              0x00000001L
#define VPEC_PG_CNTL__PG_HYSTERESIS_MASK                                                                      0x0000003EL
#define VPEC_PG_CNTL__PG1_EN_MASK                                                                             0x00000100L
#define VPEC_PG_CNTL__PG1_HYSTERESIS_MASK                                                                     0x00003E00L
#define VPEC_PG_CNTL__ZSTATES_ENABLE_MASK                                                                     0x00010000L
#define VPEC_PG_CNTL__ZSTATES_HYSTERESIS_MASK                                                                 0x003E0000L
#define VPEC_PG_CNTL__FENCE_HYSTERESIS_MASK                                                                   0x0F000000L
#define VPEC_PG_CNTL__CHECK_RSMU_UPON_POWER_UP_MASK                                                           0x10000000L
//VPEC_QUEUE0_RB_CNTL
#define VPEC_QUEUE0_RB_CNTL__RB_ENABLE__SHIFT                                                                 0x0
#define VPEC_QUEUE0_RB_CNTL__RB_SIZE__SHIFT                                                                   0x1
#define VPEC_QUEUE0_RB_CNTL__WPTR_POLL_ENABLE__SHIFT                                                          0x8
#define VPEC_QUEUE0_RB_CNTL__RB_SWAP_ENABLE__SHIFT                                                            0x9
#define VPEC_QUEUE0_RB_CNTL__WPTR_POLL_SWAP_ENABLE__SHIFT                                                     0xa
#define VPEC_QUEUE0_RB_CNTL__F32_WPTR_POLL_ENABLE__SHIFT                                                      0xb
#define VPEC_QUEUE0_RB_CNTL__RPTR_WRITEBACK_ENABLE__SHIFT                                                     0xc
#define VPEC_QUEUE0_RB_CNTL__RPTR_WRITEBACK_SWAP_ENABLE__SHIFT                                                0xd
#define VPEC_QUEUE0_RB_CNTL__RPTR_WRITEBACK_TIMER__SHIFT                                                      0x10
#define VPEC_QUEUE0_RB_CNTL__RB_PRIV__SHIFT                                                                   0x17
#define VPEC_QUEUE0_RB_CNTL__RB_VMID__SHIFT                                                                   0x18
#define VPEC_QUEUE0_RB_CNTL__RB_ENABLE_MASK                                                                   0x00000001L
#define VPEC_QUEUE0_RB_CNTL__RB_SIZE_MASK                                                                     0x0000003EL
#define VPEC_QUEUE0_RB_CNTL__WPTR_POLL_ENABLE_MASK                                                            0x00000100L
#define VPEC_QUEUE0_RB_CNTL__RB_SWAP_ENABLE_MASK                                                              0x00000200L
#define VPEC_QUEUE0_RB_CNTL__WPTR_POLL_SWAP_ENABLE_MASK                                                       0x00000400L
#define VPEC_QUEUE0_RB_CNTL__F32_WPTR_POLL_ENABLE_MASK                                                        0x00000800L
#define VPEC_QUEUE0_RB_CNTL__RPTR_WRITEBACK_ENABLE_MASK                                                       0x00001000L
#define VPEC_QUEUE0_RB_CNTL__RPTR_WRITEBACK_SWAP_ENABLE_MASK                                                  0x00002000L
#define VPEC_QUEUE0_RB_CNTL__RPTR_WRITEBACK_TIMER_MASK                                                        0x001F0000L
#define VPEC_QUEUE0_RB_CNTL__RB_PRIV_MASK                                                                     0x00800000L
#define VPEC_QUEUE0_RB_CNTL__RB_VMID_MASK                                                                     0x0F000000L
//VPEC_QUEUE0_SCHEDULE_CNTL
#define VPEC_QUEUE0_SCHEDULE_CNTL__GLOBAL_ID__SHIFT                                                           0x0
#define VPEC_QUEUE0_SCHEDULE_CNTL__PROCESS_ID__SHIFT                                                          0x2
#define VPEC_QUEUE0_SCHEDULE_CNTL__LOCAL_ID__SHIFT                                                            0x6
#define VPEC_QUEUE0_SCHEDULE_CNTL__CONTEXT_QUANTUM__SHIFT                                                     0x8
#define VPEC_QUEUE0_SCHEDULE_CNTL__GLOBAL_ID_MASK                                                             0x00000003L
#define VPEC_QUEUE0_SCHEDULE_CNTL__PROCESS_ID_MASK                                                            0x0000001CL
#define VPEC_QUEUE0_SCHEDULE_CNTL__LOCAL_ID_MASK                                                              0x000000C0L
#define VPEC_QUEUE0_SCHEDULE_CNTL__CONTEXT_QUANTUM_MASK                                                       0x0000FF00L
//VPEC_QUEUE0_RB_BASE
#define VPEC_QUEUE0_RB_BASE__ADDR__SHIFT                                                                      0x0
#define VPEC_QUEUE0_RB_BASE__ADDR_MASK                                                                        0xFFFFFFFFL
//VPEC_QUEUE0_RB_BASE_HI
#define VPEC_QUEUE0_RB_BASE_HI__ADDR__SHIFT                                                                   0x0
#define VPEC_QUEUE0_RB_BASE_HI__ADDR_MASK                                                                     0x00FFFFFFL
//VPEC_QUEUE0_RB_RPTR
#define VPEC_QUEUE0_RB_RPTR__OFFSET__SHIFT                                                                    0x0
#define VPEC_QUEUE0_RB_RPTR__OFFSET_MASK                                                                      0xFFFFFFFFL
//VPEC_QUEUE0_RB_RPTR_HI
#define VPEC_QUEUE0_RB_RPTR_HI__OFFSET__SHIFT                                                                 0x0
#define VPEC_QUEUE0_RB_RPTR_HI__OFFSET_MASK                                                                   0xFFFFFFFFL
//VPEC_QUEUE0_RB_WPTR
#define VPEC_QUEUE0_RB_WPTR__OFFSET__SHIFT                                                                    0x0
#define VPEC_QUEUE0_RB_WPTR__OFFSET_MASK                                                                      0xFFFFFFFFL
//VPEC_QUEUE0_RB_WPTR_HI
#define VPEC_QUEUE0_RB_WPTR_HI__OFFSET__SHIFT                                                                 0x0
#define VPEC_QUEUE0_RB_WPTR_HI__OFFSET_MASK                                                                   0xFFFFFFFFL
//VPEC_QUEUE0_RB_RPTR_ADDR_HI
#define VPEC_QUEUE0_RB_RPTR_ADDR_HI__ADDR__SHIFT                                                              0x0
#define VPEC_QUEUE0_RB_RPTR_ADDR_HI__ADDR_MASK                                                                0xFFFFFFFFL
//VPEC_QUEUE0_RB_RPTR_ADDR_LO
#define VPEC_QUEUE0_RB_RPTR_ADDR_LO__ADDR__SHIFT                                                              0x2
#define VPEC_QUEUE0_RB_RPTR_ADDR_LO__ADDR_MASK                                                                0xFFFFFFFCL
//VPEC_QUEUE0_RB_AQL_CNTL
#define VPEC_QUEUE0_RB_AQL_CNTL__AQL_ENABLE__SHIFT                                                            0x0
#define VPEC_QUEUE0_RB_AQL_CNTL__AQL_PACKET_SIZE__SHIFT                                                       0x1
#define VPEC_QUEUE0_RB_AQL_CNTL__PACKET_STEP__SHIFT                                                           0x8
#define VPEC_QUEUE0_RB_AQL_CNTL__MIDCMD_PREEMPT_ENABLE__SHIFT                                                 0x10
#define VPEC_QUEUE0_RB_AQL_CNTL__MIDCMD_PREEMPT_DATA_RESTORE__SHIFT                                           0x11
#define VPEC_QUEUE0_RB_AQL_CNTL__OVERLAP_ENABLE__SHIFT                                                        0x12
#define VPEC_QUEUE0_RB_AQL_CNTL__AQL_ENABLE_MASK                                                              0x00000001L
#define VPEC_QUEUE0_RB_AQL_CNTL__AQL_PACKET_SIZE_MASK                                                         0x000000FEL
#define VPEC_QUEUE0_RB_AQL_CNTL__PACKET_STEP_MASK                                                             0x0000FF00L
#define VPEC_QUEUE0_RB_AQL_CNTL__MIDCMD_PREEMPT_ENABLE_MASK                                                   0x00010000L
#define VPEC_QUEUE0_RB_AQL_CNTL__MIDCMD_PREEMPT_DATA_RESTORE_MASK                                             0x00020000L
#define VPEC_QUEUE0_RB_AQL_CNTL__OVERLAP_ENABLE_MASK                                                          0x00040000L
//VPEC_QUEUE0_MINOR_PTR_UPDATE
#define VPEC_QUEUE0_MINOR_PTR_UPDATE__ENABLE__SHIFT                                                           0x0
#define VPEC_QUEUE0_MINOR_PTR_UPDATE__ENABLE_MASK                                                             0x00000001L
//VPEC_QUEUE0_CD_INFO
#define VPEC_QUEUE0_CD_INFO__CD_INFO__SHIFT                                                                   0x0
#define VPEC_QUEUE0_CD_INFO__CD_INFO_MASK                                                                     0xFFFFFFFFL
//VPEC_QUEUE0_RB_PREEMPT
#define VPEC_QUEUE0_RB_PREEMPT__PREEMPT_REQ__SHIFT                                                            0x0
#define VPEC_QUEUE0_RB_PREEMPT__PREEMPT_REQ_MASK                                                              0x00000001L
//VPEC_QUEUE0_SKIP_CNTL
#define VPEC_QUEUE0_SKIP_CNTL__SKIP_COUNT__SHIFT                                                              0x0
#define VPEC_QUEUE0_SKIP_CNTL__SKIP_COUNT_MASK                                                                0x000FFFFFL
//VPEC_QUEUE0_DOORBELL
#define VPEC_QUEUE0_DOORBELL__ENABLE__SHIFT                                                                   0x1c
#define VPEC_QUEUE0_DOORBELL__CAPTURED__SHIFT                                                                 0x1e
#define VPEC_QUEUE0_DOORBELL__ENABLE_MASK                                                                     0x10000000L
#define VPEC_QUEUE0_DOORBELL__CAPTURED_MASK                                                                   0x40000000L
//VPEC_QUEUE0_DOORBELL_OFFSET
#define VPEC_QUEUE0_DOORBELL_OFFSET__OFFSET__SHIFT                                                            0x2
#define VPEC_QUEUE0_DOORBELL_OFFSET__OFFSET_MASK                                                              0x0FFFFFFCL
//VPEC_QUEUE0_DUMMY0
#define VPEC_QUEUE0_DUMMY0__DUMMY__SHIFT                                                                      0x0
#define VPEC_QUEUE0_DUMMY0__DUMMY_MASK                                                                        0xFFFFFFFFL
//VPEC_QUEUE0_DUMMY1
#define VPEC_QUEUE0_DUMMY1__VALUE__SHIFT                                                                      0x0
#define VPEC_QUEUE0_DUMMY1__VALUE_MASK                                                                        0xFFFFFFFFL
//VPEC_QUEUE0_DUMMY2
#define VPEC_QUEUE0_DUMMY2__VALUE__SHIFT                                                                      0x0
#define VPEC_QUEUE0_DUMMY2__VALUE_MASK                                                                        0xFFFFFFFFL
//VPEC_QUEUE0_DUMMY3
#define VPEC_QUEUE0_DUMMY3__VALUE__SHIFT                                                                      0x0
#define VPEC_QUEUE0_DUMMY3__VALUE_MASK                                                                        0xFFFFFFFFL
//VPEC_QUEUE0_DUMMY4
#define VPEC_QUEUE0_DUMMY4__VALUE__SHIFT                                                                      0x0
#define VPEC_QUEUE0_DUMMY4__VALUE_MASK                                                                        0xFFFFFFFFL
//VPEC_QUEUE0_IB_CNTL
#define VPEC_QUEUE0_IB_CNTL__IB_ENABLE__SHIFT                                                                 0x0
#define VPEC_QUEUE0_IB_CNTL__SWITCH_INSIDE_IB__SHIFT                                                          0x8
#define VPEC_QUEUE0_IB_CNTL__CMD_VMID__SHIFT                                                                  0x10
#define VPEC_QUEUE0_IB_CNTL__IB_PRIV__SHIFT                                                                   0x1f
#define VPEC_QUEUE0_IB_CNTL__IB_ENABLE_MASK                                                                   0x00000001L
#define VPEC_QUEUE0_IB_CNTL__SWITCH_INSIDE_IB_MASK                                                            0x00000100L
#define VPEC_QUEUE0_IB_CNTL__CMD_VMID_MASK                                                                    0x000F0000L
#define VPEC_QUEUE0_IB_CNTL__IB_PRIV_MASK                                                                     0x80000000L
//VPEC_QUEUE0_IB_RPTR
#define VPEC_QUEUE0_IB_RPTR__OFFSET__SHIFT                                                                    0x2
#define VPEC_QUEUE0_IB_RPTR__OFFSET_MASK                                                                      0x003FFFFCL
//VPEC_QUEUE0_IB_OFFSET
#define VPEC_QUEUE0_IB_OFFSET__OFFSET__SHIFT                                                                  0x2
#define VPEC_QUEUE0_IB_OFFSET__OFFSET_MASK                                                                    0x003FFFFCL
//VPEC_QUEUE0_IB_BASE_LO
#define VPEC_QUEUE0_IB_BASE_LO__ADDR__SHIFT                                                                   0x5
#define VPEC_QUEUE0_IB_BASE_LO__ADDR_MASK                                                                     0xFFFFFFE0L
//VPEC_QUEUE0_IB_BASE_HI
#define VPEC_QUEUE0_IB_BASE_HI__ADDR__SHIFT                                                                   0x0
#define VPEC_QUEUE0_IB_BASE_HI__ADDR_MASK                                                                     0xFFFFFFFFL
//VPEC_QUEUE0_IB_SIZE
#define VPEC_QUEUE0_IB_SIZE__SIZE__SHIFT                                                                      0x0
#define VPEC_QUEUE0_IB_SIZE__SIZE_MASK                                                                        0x000FFFFFL
//VPEC_QUEUE0_CMDIB_CNTL
#define VPEC_QUEUE0_CMDIB_CNTL__IB_ENABLE__SHIFT                                                              0x0
#define VPEC_QUEUE0_CMDIB_CNTL__IB_SWAP_ENABLE__SHIFT                                                         0x4
#define VPEC_QUEUE0_CMDIB_CNTL__SWITCH_INSIDE_IB__SHIFT                                                       0x8
#define VPEC_QUEUE0_CMDIB_CNTL__CMD_VMID__SHIFT                                                               0x10
#define VPEC_QUEUE0_CMDIB_CNTL__IB_PRIV__SHIFT                                                                0x1f
#define VPEC_QUEUE0_CMDIB_CNTL__IB_ENABLE_MASK                                                                0x00000001L
#define VPEC_QUEUE0_CMDIB_CNTL__IB_SWAP_ENABLE_MASK                                                           0x00000010L
#define VPEC_QUEUE0_CMDIB_CNTL__SWITCH_INSIDE_IB_MASK                                                         0x00000100L
#define VPEC_QUEUE0_CMDIB_CNTL__CMD_VMID_MASK                                                                 0x000F0000L
#define VPEC_QUEUE0_CMDIB_CNTL__IB_PRIV_MASK                                                                  0x80000000L
//VPEC_QUEUE0_CMDIB_RPTR
#define VPEC_QUEUE0_CMDIB_RPTR__OFFSET__SHIFT                                                                 0x2
#define VPEC_QUEUE0_CMDIB_RPTR__OFFSET_MASK                                                                   0x003FFFFCL
//VPEC_QUEUE0_CMDIB_OFFSET
#define VPEC_QUEUE0_CMDIB_OFFSET__OFFSET__SHIFT                                                               0x2
#define VPEC_QUEUE0_CMDIB_OFFSET__OFFSET_MASK                                                                 0x003FFFFCL
//VPEC_QUEUE0_CMDIB_BASE_LO
#define VPEC_QUEUE0_CMDIB_BASE_LO__ADDR__SHIFT                                                                0x5
#define VPEC_QUEUE0_CMDIB_BASE_LO__ADDR_MASK                                                                  0xFFFFFFE0L
//VPEC_QUEUE0_CMDIB_BASE_HI
#define VPEC_QUEUE0_CMDIB_BASE_HI__ADDR__SHIFT                                                                0x0
#define VPEC_QUEUE0_CMDIB_BASE_HI__ADDR_MASK                                                                  0xFFFFFFFFL
//VPEC_QUEUE0_CMDIB_SIZE
#define VPEC_QUEUE0_CMDIB_SIZE__SIZE__SHIFT                                                                   0x0
#define VPEC_QUEUE0_CMDIB_SIZE__SIZE_MASK                                                                     0x000FFFFFL
//VPEC_QUEUE0_3DLUTIB_CNTL
#define VPEC_QUEUE0_3DLUTIB_CNTL__IB_ENABLE__SHIFT                                                            0x0
#define VPEC_QUEUE0_3DLUTIB_CNTL__SWITCH_INSIDE_IB__SHIFT                                                     0x8
#define VPEC_QUEUE0_3DLUTIB_CNTL__CMD_VMID__SHIFT                                                             0x10
#define VPEC_QUEUE0_3DLUTIB_CNTL__IB_PRIV__SHIFT                                                              0x1f
#define VPEC_QUEUE0_3DLUTIB_CNTL__IB_ENABLE_MASK                                                              0x00000001L
#define VPEC_QUEUE0_3DLUTIB_CNTL__SWITCH_INSIDE_IB_MASK                                                       0x00000100L
#define VPEC_QUEUE0_3DLUTIB_CNTL__CMD_VMID_MASK                                                               0x000F0000L
#define VPEC_QUEUE0_3DLUTIB_CNTL__IB_PRIV_MASK                                                                0x80000000L
//VPEC_QUEUE0_3DLUTIB_RPTR
#define VPEC_QUEUE0_3DLUTIB_RPTR__OFFSET__SHIFT                                                               0x2
#define VPEC_QUEUE0_3DLUTIB_RPTR__OFFSET_MASK                                                                 0x003FFFFCL
//VPEC_QUEUE0_3DLUTIB_OFFSET
#define VPEC_QUEUE0_3DLUTIB_OFFSET__OFFSET__SHIFT                                                             0x2
#define VPEC_QUEUE0_3DLUTIB_OFFSET__OFFSET_MASK                                                               0x003FFFFCL
//VPEC_QUEUE0_3DLUTIB_BASE_LO
#define VPEC_QUEUE0_3DLUTIB_BASE_LO__ADDR__SHIFT                                                              0x5
#define VPEC_QUEUE0_3DLUTIB_BASE_LO__ADDR_MASK                                                                0xFFFFFFE0L
//VPEC_QUEUE0_3DLUTIB_BASE_HI
#define VPEC_QUEUE0_3DLUTIB_BASE_HI__ADDR__SHIFT                                                              0x0
#define VPEC_QUEUE0_3DLUTIB_BASE_HI__ADDR_MASK                                                                0xFFFFFFFFL
//VPEC_QUEUE0_3DLUTIB_SIZE
#define VPEC_QUEUE0_3DLUTIB_SIZE__SIZE__SHIFT                                                                 0x0
#define VPEC_QUEUE0_3DLUTIB_SIZE__SIZE_MASK                                                                   0x000FFFFFL
//VPEC_QUEUE0_CSA_ADDR_LO
#define VPEC_QUEUE0_CSA_ADDR_LO__ADDR__SHIFT                                                                  0x0
#define VPEC_QUEUE0_CSA_ADDR_LO__ADDR_MASK                                                                    0xFFFFFFFFL
//VPEC_QUEUE0_CSA_ADDR_HI
#define VPEC_QUEUE0_CSA_ADDR_HI__ADDR__SHIFT                                                                  0x0
#define VPEC_QUEUE0_CSA_ADDR_HI__ADDR_MASK                                                                    0xFFFFFFFFL
//VPEC_QUEUE0_CONTEXT_STATUS
#define VPEC_QUEUE0_CONTEXT_STATUS__SELECTED__SHIFT                                                           0x0
#define VPEC_QUEUE0_CONTEXT_STATUS__USE_IB__SHIFT                                                             0x1
#define VPEC_QUEUE0_CONTEXT_STATUS__IDLE__SHIFT                                                               0x2
#define VPEC_QUEUE0_CONTEXT_STATUS__EXPIRED__SHIFT                                                            0x3
#define VPEC_QUEUE0_CONTEXT_STATUS__EXCEPTION__SHIFT                                                          0x4
#define VPEC_QUEUE0_CONTEXT_STATUS__CTXSW_ABLE__SHIFT                                                         0x7
#define VPEC_QUEUE0_CONTEXT_STATUS__USE_3DLUTIB__SHIFT                                                        0x8
#define VPEC_QUEUE0_CONTEXT_STATUS__PREEMPT_DISABLE__SHIFT                                                    0xa
#define VPEC_QUEUE0_CONTEXT_STATUS__RPTR_WB_IDLE__SHIFT                                                       0xb
#define VPEC_QUEUE0_CONTEXT_STATUS__WPTR_UPDATE_PENDING__SHIFT                                                0xc
#define VPEC_QUEUE0_CONTEXT_STATUS__WPTR_UPDATE_FAIL_COUNT__SHIFT                                             0x10
#define VPEC_QUEUE0_CONTEXT_STATUS__SELECTED_MASK                                                             0x00000001L
#define VPEC_QUEUE0_CONTEXT_STATUS__USE_IB_MASK                                                               0x00000002L
#define VPEC_QUEUE0_CONTEXT_STATUS__IDLE_MASK                                                                 0x00000004L
#define VPEC_QUEUE0_CONTEXT_STATUS__EXPIRED_MASK                                                              0x00000008L
#define VPEC_QUEUE0_CONTEXT_STATUS__EXCEPTION_MASK                                                            0x00000070L
#define VPEC_QUEUE0_CONTEXT_STATUS__CTXSW_ABLE_MASK                                                           0x00000080L
#define VPEC_QUEUE0_CONTEXT_STATUS__USE_3DLUTIB_MASK                                                          0x00000100L
#define VPEC_QUEUE0_CONTEXT_STATUS__PREEMPT_DISABLE_MASK                                                      0x00000400L
#define VPEC_QUEUE0_CONTEXT_STATUS__RPTR_WB_IDLE_MASK                                                         0x00000800L
#define VPEC_QUEUE0_CONTEXT_STATUS__WPTR_UPDATE_PENDING_MASK                                                  0x00001000L
#define VPEC_QUEUE0_CONTEXT_STATUS__WPTR_UPDATE_FAIL_COUNT_MASK                                               0x00FF0000L
//VPEC_QUEUE0_DOORBELL_LOG
#define VPEC_QUEUE0_DOORBELL_LOG__BE_ERROR__SHIFT                                                             0x0
#define VPEC_QUEUE0_DOORBELL_LOG__DATA__SHIFT                                                                 0x2
#define VPEC_QUEUE0_DOORBELL_LOG__BE_ERROR_MASK                                                               0x00000001L
#define VPEC_QUEUE0_DOORBELL_LOG__DATA_MASK                                                                   0xFFFFFFFCL
//VPEC_QUEUE0_IB_SUB_REMAIN
#define VPEC_QUEUE0_IB_SUB_REMAIN__SIZE__SHIFT                                                                0x0
#define VPEC_QUEUE0_IB_SUB_REMAIN__SIZE_MASK                                                                  0x00003FFFL
//VPEC_QUEUE0_PREEMPT
#define VPEC_QUEUE0_PREEMPT__IB_PREEMPT__SHIFT                                                                0x0
#define VPEC_QUEUE0_PREEMPT__IB_PREEMPT_MASK                                                                  0x00000001L
//VPEC_QUEUE0_LOG0BUFFER_CFG
#define VPEC_QUEUE0_LOG0BUFFER_CFG__ENABLE__SHIFT                                                             0x0
#define VPEC_QUEUE0_LOG0BUFFER_CFG__FIRST_FREE_ENTRY__SHIFT                                                   0x4
#define VPEC_QUEUE0_LOG0BUFFER_CFG__LAST_FREE_ENTRY__SHIFT                                                    0xc
#define VPEC_QUEUE0_LOG0BUFFER_CFG__RESERVED__SHIFT                                                           0x14
#define VPEC_QUEUE0_LOG0BUFFER_CFG__ENABLE_MASK                                                               0x00000001L
#define VPEC_QUEUE0_LOG0BUFFER_CFG__FIRST_FREE_ENTRY_MASK                                                     0x00000FF0L
#define VPEC_QUEUE0_LOG0BUFFER_CFG__LAST_FREE_ENTRY_MASK                                                      0x000FF000L
#define VPEC_QUEUE0_LOG0BUFFER_CFG__RESERVED_MASK                                                             0xFFF00000L
//VPEC_QUEUE0_LOG1BUFFER_CFG
#define VPEC_QUEUE0_LOG1BUFFER_CFG__ENABLE__SHIFT                                                             0x0
#define VPEC_QUEUE0_LOG1BUFFER_CFG__PARTIAL_ENTRY__SHIFT                                                      0x1
#define VPEC_QUEUE0_LOG1BUFFER_CFG__FIRST_FREE_ENTRY__SHIFT                                                   0x4
#define VPEC_QUEUE0_LOG1BUFFER_CFG__LAST_FREE_ENTRY__SHIFT                                                    0xc
#define VPEC_QUEUE0_LOG1BUFFER_CFG__RESERVED__SHIFT                                                           0x14
#define VPEC_QUEUE0_LOG1BUFFER_CFG__ENABLE_MASK                                                               0x00000001L
#define VPEC_QUEUE0_LOG1BUFFER_CFG__PARTIAL_ENTRY_MASK                                                        0x00000002L
#define VPEC_QUEUE0_LOG1BUFFER_CFG__FIRST_FREE_ENTRY_MASK                                                     0x00000FF0L
#define VPEC_QUEUE0_LOG1BUFFER_CFG__LAST_FREE_ENTRY_MASK                                                      0x000FF000L
#define VPEC_QUEUE0_LOG1BUFFER_CFG__RESERVED_MASK                                                             0xFFF00000L
//VPEC_QUEUE1_RB_CNTL
#define VPEC_QUEUE1_RB_CNTL__RB_ENABLE__SHIFT                                                                 0x0
#define VPEC_QUEUE1_RB_CNTL__RB_SIZE__SHIFT                                                                   0x1
#define VPEC_QUEUE1_RB_CNTL__WPTR_POLL_ENABLE__SHIFT                                                          0x8
#define VPEC_QUEUE1_RB_CNTL__RB_SWAP_ENABLE__SHIFT                                                            0x9
#define VPEC_QUEUE1_RB_CNTL__WPTR_POLL_SWAP_ENABLE__SHIFT                                                     0xa
#define VPEC_QUEUE1_RB_CNTL__F32_WPTR_POLL_ENABLE__SHIFT                                                      0xb
#define VPEC_QUEUE1_RB_CNTL__RPTR_WRITEBACK_ENABLE__SHIFT                                                     0xc
#define VPEC_QUEUE1_RB_CNTL__RPTR_WRITEBACK_SWAP_ENABLE__SHIFT                                                0xd
#define VPEC_QUEUE1_RB_CNTL__RPTR_WRITEBACK_TIMER__SHIFT                                                      0x10
#define VPEC_QUEUE1_RB_CNTL__RB_PRIV__SHIFT                                                                   0x17
#define VPEC_QUEUE1_RB_CNTL__RB_VMID__SHIFT                                                                   0x18
#define VPEC_QUEUE1_RB_CNTL__RB_ENABLE_MASK                                                                   0x00000001L
#define VPEC_QUEUE1_RB_CNTL__RB_SIZE_MASK                                                                     0x0000003EL
#define VPEC_QUEUE1_RB_CNTL__WPTR_POLL_ENABLE_MASK                                                            0x00000100L
#define VPEC_QUEUE1_RB_CNTL__RB_SWAP_ENABLE_MASK                                                              0x00000200L
#define VPEC_QUEUE1_RB_CNTL__WPTR_POLL_SWAP_ENABLE_MASK                                                       0x00000400L
#define VPEC_QUEUE1_RB_CNTL__F32_WPTR_POLL_ENABLE_MASK                                                        0x00000800L
#define VPEC_QUEUE1_RB_CNTL__RPTR_WRITEBACK_ENABLE_MASK                                                       0x00001000L
#define VPEC_QUEUE1_RB_CNTL__RPTR_WRITEBACK_SWAP_ENABLE_MASK                                                  0x00002000L
#define VPEC_QUEUE1_RB_CNTL__RPTR_WRITEBACK_TIMER_MASK                                                        0x001F0000L
#define VPEC_QUEUE1_RB_CNTL__RB_PRIV_MASK                                                                     0x00800000L
#define VPEC_QUEUE1_RB_CNTL__RB_VMID_MASK                                                                     0x0F000000L
//VPEC_QUEUE1_SCHEDULE_CNTL
#define VPEC_QUEUE1_SCHEDULE_CNTL__GLOBAL_ID__SHIFT                                                           0x0
#define VPEC_QUEUE1_SCHEDULE_CNTL__PROCESS_ID__SHIFT                                                          0x2
#define VPEC_QUEUE1_SCHEDULE_CNTL__LOCAL_ID__SHIFT                                                            0x6
#define VPEC_QUEUE1_SCHEDULE_CNTL__CONTEXT_QUANTUM__SHIFT                                                     0x8
#define VPEC_QUEUE1_SCHEDULE_CNTL__GLOBAL_ID_MASK                                                             0x00000003L
#define VPEC_QUEUE1_SCHEDULE_CNTL__PROCESS_ID_MASK                                                            0x0000001CL
#define VPEC_QUEUE1_SCHEDULE_CNTL__LOCAL_ID_MASK                                                              0x000000C0L
#define VPEC_QUEUE1_SCHEDULE_CNTL__CONTEXT_QUANTUM_MASK                                                       0x0000FF00L
//VPEC_QUEUE1_RB_BASE
#define VPEC_QUEUE1_RB_BASE__ADDR__SHIFT                                                                      0x0
#define VPEC_QUEUE1_RB_BASE__ADDR_MASK                                                                        0xFFFFFFFFL
//VPEC_QUEUE1_RB_BASE_HI
#define VPEC_QUEUE1_RB_BASE_HI__ADDR__SHIFT                                                                   0x0
#define VPEC_QUEUE1_RB_BASE_HI__ADDR_MASK                                                                     0x00FFFFFFL
//VPEC_QUEUE1_RB_RPTR
#define VPEC_QUEUE1_RB_RPTR__OFFSET__SHIFT                                                                    0x0
#define VPEC_QUEUE1_RB_RPTR__OFFSET_MASK                                                                      0xFFFFFFFFL
//VPEC_QUEUE1_RB_RPTR_HI
#define VPEC_QUEUE1_RB_RPTR_HI__OFFSET__SHIFT                                                                 0x0
#define VPEC_QUEUE1_RB_RPTR_HI__OFFSET_MASK                                                                   0xFFFFFFFFL
//VPEC_QUEUE1_RB_WPTR
#define VPEC_QUEUE1_RB_WPTR__OFFSET__SHIFT                                                                    0x0
#define VPEC_QUEUE1_RB_WPTR__OFFSET_MASK                                                                      0xFFFFFFFFL
//VPEC_QUEUE1_RB_WPTR_HI
#define VPEC_QUEUE1_RB_WPTR_HI__OFFSET__SHIFT                                                                 0x0
#define VPEC_QUEUE1_RB_WPTR_HI__OFFSET_MASK                                                                   0xFFFFFFFFL
//VPEC_QUEUE1_RB_RPTR_ADDR_HI
#define VPEC_QUEUE1_RB_RPTR_ADDR_HI__ADDR__SHIFT                                                              0x0
#define VPEC_QUEUE1_RB_RPTR_ADDR_HI__ADDR_MASK                                                                0xFFFFFFFFL
//VPEC_QUEUE1_RB_RPTR_ADDR_LO
#define VPEC_QUEUE1_RB_RPTR_ADDR_LO__ADDR__SHIFT                                                              0x2
#define VPEC_QUEUE1_RB_RPTR_ADDR_LO__ADDR_MASK                                                                0xFFFFFFFCL
//VPEC_QUEUE1_RB_AQL_CNTL
#define VPEC_QUEUE1_RB_AQL_CNTL__AQL_ENABLE__SHIFT                                                            0x0
#define VPEC_QUEUE1_RB_AQL_CNTL__AQL_PACKET_SIZE__SHIFT                                                       0x1
#define VPEC_QUEUE1_RB_AQL_CNTL__PACKET_STEP__SHIFT                                                           0x8
#define VPEC_QUEUE1_RB_AQL_CNTL__MIDCMD_PREEMPT_ENABLE__SHIFT                                                 0x10
#define VPEC_QUEUE1_RB_AQL_CNTL__MIDCMD_PREEMPT_DATA_RESTORE__SHIFT                                           0x11
#define VPEC_QUEUE1_RB_AQL_CNTL__OVERLAP_ENABLE__SHIFT                                                        0x12
#define VPEC_QUEUE1_RB_AQL_CNTL__AQL_ENABLE_MASK                                                              0x00000001L
#define VPEC_QUEUE1_RB_AQL_CNTL__AQL_PACKET_SIZE_MASK                                                         0x000000FEL
#define VPEC_QUEUE1_RB_AQL_CNTL__PACKET_STEP_MASK                                                             0x0000FF00L
#define VPEC_QUEUE1_RB_AQL_CNTL__MIDCMD_PREEMPT_ENABLE_MASK                                                   0x00010000L
#define VPEC_QUEUE1_RB_AQL_CNTL__MIDCMD_PREEMPT_DATA_RESTORE_MASK                                             0x00020000L
#define VPEC_QUEUE1_RB_AQL_CNTL__OVERLAP_ENABLE_MASK                                                          0x00040000L
//VPEC_QUEUE1_MINOR_PTR_UPDATE
#define VPEC_QUEUE1_MINOR_PTR_UPDATE__ENABLE__SHIFT                                                           0x0
#define VPEC_QUEUE1_MINOR_PTR_UPDATE__ENABLE_MASK                                                             0x00000001L
//VPEC_QUEUE1_CD_INFO
#define VPEC_QUEUE1_CD_INFO__CD_INFO__SHIFT                                                                   0x0
#define VPEC_QUEUE1_CD_INFO__CD_INFO_MASK                                                                     0xFFFFFFFFL
//VPEC_QUEUE1_RB_PREEMPT
#define VPEC_QUEUE1_RB_PREEMPT__PREEMPT_REQ__SHIFT                                                            0x0
#define VPEC_QUEUE1_RB_PREEMPT__PREEMPT_REQ_MASK                                                              0x00000001L
//VPEC_QUEUE1_SKIP_CNTL
#define VPEC_QUEUE1_SKIP_CNTL__SKIP_COUNT__SHIFT                                                              0x0
#define VPEC_QUEUE1_SKIP_CNTL__SKIP_COUNT_MASK                                                                0x000FFFFFL
//VPEC_QUEUE1_DOORBELL
#define VPEC_QUEUE1_DOORBELL__ENABLE__SHIFT                                                                   0x1c
#define VPEC_QUEUE1_DOORBELL__CAPTURED__SHIFT                                                                 0x1e
#define VPEC_QUEUE1_DOORBELL__ENABLE_MASK                                                                     0x10000000L
#define VPEC_QUEUE1_DOORBELL__CAPTURED_MASK                                                                   0x40000000L
//VPEC_QUEUE1_DOORBELL_OFFSET
#define VPEC_QUEUE1_DOORBELL_OFFSET__OFFSET__SHIFT                                                            0x2
#define VPEC_QUEUE1_DOORBELL_OFFSET__OFFSET_MASK                                                              0x0FFFFFFCL
//VPEC_QUEUE1_DUMMY0
#define VPEC_QUEUE1_DUMMY0__DUMMY__SHIFT                                                                      0x0
#define VPEC_QUEUE1_DUMMY0__DUMMY_MASK                                                                        0xFFFFFFFFL
//VPEC_QUEUE1_DUMMY1
#define VPEC_QUEUE1_DUMMY1__VALUE__SHIFT                                                                      0x0
#define VPEC_QUEUE1_DUMMY1__VALUE_MASK                                                                        0xFFFFFFFFL
//VPEC_QUEUE1_DUMMY2
#define VPEC_QUEUE1_DUMMY2__VALUE__SHIFT                                                                      0x0
#define VPEC_QUEUE1_DUMMY2__VALUE_MASK                                                                        0xFFFFFFFFL
//VPEC_QUEUE1_DUMMY3
#define VPEC_QUEUE1_DUMMY3__VALUE__SHIFT                                                                      0x0
#define VPEC_QUEUE1_DUMMY3__VALUE_MASK                                                                        0xFFFFFFFFL
//VPEC_QUEUE1_DUMMY4
#define VPEC_QUEUE1_DUMMY4__VALUE__SHIFT                                                                      0x0
#define VPEC_QUEUE1_DUMMY4__VALUE_MASK                                                                        0xFFFFFFFFL
//VPEC_QUEUE1_IB_CNTL
#define VPEC_QUEUE1_IB_CNTL__IB_ENABLE__SHIFT                                                                 0x0
#define VPEC_QUEUE1_IB_CNTL__SWITCH_INSIDE_IB__SHIFT                                                          0x8
#define VPEC_QUEUE1_IB_CNTL__CMD_VMID__SHIFT                                                                  0x10
#define VPEC_QUEUE1_IB_CNTL__IB_PRIV__SHIFT                                                                   0x1f
#define VPEC_QUEUE1_IB_CNTL__IB_ENABLE_MASK                                                                   0x00000001L
#define VPEC_QUEUE1_IB_CNTL__SWITCH_INSIDE_IB_MASK                                                            0x00000100L
#define VPEC_QUEUE1_IB_CNTL__CMD_VMID_MASK                                                                    0x000F0000L
#define VPEC_QUEUE1_IB_CNTL__IB_PRIV_MASK                                                                     0x80000000L
//VPEC_QUEUE1_IB_RPTR
#define VPEC_QUEUE1_IB_RPTR__OFFSET__SHIFT                                                                    0x2
#define VPEC_QUEUE1_IB_RPTR__OFFSET_MASK                                                                      0x003FFFFCL
//VPEC_QUEUE1_IB_OFFSET
#define VPEC_QUEUE1_IB_OFFSET__OFFSET__SHIFT                                                                  0x2
#define VPEC_QUEUE1_IB_OFFSET__OFFSET_MASK                                                                    0x003FFFFCL
//VPEC_QUEUE1_IB_BASE_LO
#define VPEC_QUEUE1_IB_BASE_LO__ADDR__SHIFT                                                                   0x5
#define VPEC_QUEUE1_IB_BASE_LO__ADDR_MASK                                                                     0xFFFFFFE0L
//VPEC_QUEUE1_IB_BASE_HI
#define VPEC_QUEUE1_IB_BASE_HI__ADDR__SHIFT                                                                   0x0
#define VPEC_QUEUE1_IB_BASE_HI__ADDR_MASK                                                                     0xFFFFFFFFL
//VPEC_QUEUE1_IB_SIZE
#define VPEC_QUEUE1_IB_SIZE__SIZE__SHIFT                                                                      0x0
#define VPEC_QUEUE1_IB_SIZE__SIZE_MASK                                                                        0x000FFFFFL
//VPEC_QUEUE1_CMDIB_CNTL
#define VPEC_QUEUE1_CMDIB_CNTL__IB_ENABLE__SHIFT                                                              0x0
#define VPEC_QUEUE1_CMDIB_CNTL__IB_SWAP_ENABLE__SHIFT                                                         0x4
#define VPEC_QUEUE1_CMDIB_CNTL__SWITCH_INSIDE_IB__SHIFT                                                       0x8
#define VPEC_QUEUE1_CMDIB_CNTL__CMD_VMID__SHIFT                                                               0x10
#define VPEC_QUEUE1_CMDIB_CNTL__IB_PRIV__SHIFT                                                                0x1f
#define VPEC_QUEUE1_CMDIB_CNTL__IB_ENABLE_MASK                                                                0x00000001L
#define VPEC_QUEUE1_CMDIB_CNTL__IB_SWAP_ENABLE_MASK                                                           0x00000010L
#define VPEC_QUEUE1_CMDIB_CNTL__SWITCH_INSIDE_IB_MASK                                                         0x00000100L
#define VPEC_QUEUE1_CMDIB_CNTL__CMD_VMID_MASK                                                                 0x000F0000L
#define VPEC_QUEUE1_CMDIB_CNTL__IB_PRIV_MASK                                                                  0x80000000L
//VPEC_QUEUE1_CMDIB_RPTR
#define VPEC_QUEUE1_CMDIB_RPTR__OFFSET__SHIFT                                                                 0x2
#define VPEC_QUEUE1_CMDIB_RPTR__OFFSET_MASK                                                                   0x003FFFFCL
//VPEC_QUEUE1_CMDIB_OFFSET
#define VPEC_QUEUE1_CMDIB_OFFSET__OFFSET__SHIFT                                                               0x2
#define VPEC_QUEUE1_CMDIB_OFFSET__OFFSET_MASK                                                                 0x003FFFFCL
//VPEC_QUEUE1_CMDIB_BASE_LO
#define VPEC_QUEUE1_CMDIB_BASE_LO__ADDR__SHIFT                                                                0x5
#define VPEC_QUEUE1_CMDIB_BASE_LO__ADDR_MASK                                                                  0xFFFFFFE0L
//VPEC_QUEUE1_CMDIB_BASE_HI
#define VPEC_QUEUE1_CMDIB_BASE_HI__ADDR__SHIFT                                                                0x0
#define VPEC_QUEUE1_CMDIB_BASE_HI__ADDR_MASK                                                                  0xFFFFFFFFL
//VPEC_QUEUE1_CMDIB_SIZE
#define VPEC_QUEUE1_CMDIB_SIZE__SIZE__SHIFT                                                                   0x0
#define VPEC_QUEUE1_CMDIB_SIZE__SIZE_MASK                                                                     0x000FFFFFL
//VPEC_QUEUE1_3DLUTIB_CNTL
#define VPEC_QUEUE1_3DLUTIB_CNTL__IB_ENABLE__SHIFT                                                            0x0
#define VPEC_QUEUE1_3DLUTIB_CNTL__SWITCH_INSIDE_IB__SHIFT                                                     0x8
#define VPEC_QUEUE1_3DLUTIB_CNTL__CMD_VMID__SHIFT                                                             0x10
#define VPEC_QUEUE1_3DLUTIB_CNTL__IB_PRIV__SHIFT                                                              0x1f
#define VPEC_QUEUE1_3DLUTIB_CNTL__IB_ENABLE_MASK                                                              0x00000001L
#define VPEC_QUEUE1_3DLUTIB_CNTL__SWITCH_INSIDE_IB_MASK                                                       0x00000100L
#define VPEC_QUEUE1_3DLUTIB_CNTL__CMD_VMID_MASK                                                               0x000F0000L
#define VPEC_QUEUE1_3DLUTIB_CNTL__IB_PRIV_MASK                                                                0x80000000L
//VPEC_QUEUE1_3DLUTIB_RPTR
#define VPEC_QUEUE1_3DLUTIB_RPTR__OFFSET__SHIFT                                                               0x2
#define VPEC_QUEUE1_3DLUTIB_RPTR__OFFSET_MASK                                                                 0x003FFFFCL
//VPEC_QUEUE1_3DLUTIB_OFFSET
#define VPEC_QUEUE1_3DLUTIB_OFFSET__OFFSET__SHIFT                                                             0x2
#define VPEC_QUEUE1_3DLUTIB_OFFSET__OFFSET_MASK                                                               0x003FFFFCL
//VPEC_QUEUE1_3DLUTIB_BASE_LO
#define VPEC_QUEUE1_3DLUTIB_BASE_LO__ADDR__SHIFT                                                              0x5
#define VPEC_QUEUE1_3DLUTIB_BASE_LO__ADDR_MASK                                                                0xFFFFFFE0L
//VPEC_QUEUE1_3DLUTIB_BASE_HI
#define VPEC_QUEUE1_3DLUTIB_BASE_HI__ADDR__SHIFT                                                              0x0
#define VPEC_QUEUE1_3DLUTIB_BASE_HI__ADDR_MASK                                                                0xFFFFFFFFL
//VPEC_QUEUE1_3DLUTIB_SIZE
#define VPEC_QUEUE1_3DLUTIB_SIZE__SIZE__SHIFT                                                                 0x0
#define VPEC_QUEUE1_3DLUTIB_SIZE__SIZE_MASK                                                                   0x000FFFFFL
//VPEC_QUEUE1_CSA_ADDR_LO
#define VPEC_QUEUE1_CSA_ADDR_LO__ADDR__SHIFT                                                                  0x0
#define VPEC_QUEUE1_CSA_ADDR_LO__ADDR_MASK                                                                    0xFFFFFFFFL
//VPEC_QUEUE1_CSA_ADDR_HI
#define VPEC_QUEUE1_CSA_ADDR_HI__ADDR__SHIFT                                                                  0x0
#define VPEC_QUEUE1_CSA_ADDR_HI__ADDR_MASK                                                                    0xFFFFFFFFL
//VPEC_QUEUE1_CONTEXT_STATUS
#define VPEC_QUEUE1_CONTEXT_STATUS__SELECTED__SHIFT                                                           0x0
#define VPEC_QUEUE1_CONTEXT_STATUS__USE_IB__SHIFT                                                             0x1
#define VPEC_QUEUE1_CONTEXT_STATUS__IDLE__SHIFT                                                               0x2
#define VPEC_QUEUE1_CONTEXT_STATUS__EXPIRED__SHIFT                                                            0x3
#define VPEC_QUEUE1_CONTEXT_STATUS__EXCEPTION__SHIFT                                                          0x4
#define VPEC_QUEUE1_CONTEXT_STATUS__CTXSW_ABLE__SHIFT                                                         0x7
#define VPEC_QUEUE1_CONTEXT_STATUS__USE_3DLUTIB__SHIFT                                                        0x8
#define VPEC_QUEUE1_CONTEXT_STATUS__PREEMPT_DISABLE__SHIFT                                                    0xa
#define VPEC_QUEUE1_CONTEXT_STATUS__RPTR_WB_IDLE__SHIFT                                                       0xb
#define VPEC_QUEUE1_CONTEXT_STATUS__WPTR_UPDATE_PENDING__SHIFT                                                0xc
#define VPEC_QUEUE1_CONTEXT_STATUS__WPTR_UPDATE_FAIL_COUNT__SHIFT                                             0x10
#define VPEC_QUEUE1_CONTEXT_STATUS__SELECTED_MASK                                                             0x00000001L
#define VPEC_QUEUE1_CONTEXT_STATUS__USE_IB_MASK                                                               0x00000002L
#define VPEC_QUEUE1_CONTEXT_STATUS__IDLE_MASK                                                                 0x00000004L
#define VPEC_QUEUE1_CONTEXT_STATUS__EXPIRED_MASK                                                              0x00000008L
#define VPEC_QUEUE1_CONTEXT_STATUS__EXCEPTION_MASK                                                            0x00000070L
#define VPEC_QUEUE1_CONTEXT_STATUS__CTXSW_ABLE_MASK                                                           0x00000080L
#define VPEC_QUEUE1_CONTEXT_STATUS__USE_3DLUTIB_MASK                                                          0x00000100L
#define VPEC_QUEUE1_CONTEXT_STATUS__PREEMPT_DISABLE_MASK                                                      0x00000400L
#define VPEC_QUEUE1_CONTEXT_STATUS__RPTR_WB_IDLE_MASK                                                         0x00000800L
#define VPEC_QUEUE1_CONTEXT_STATUS__WPTR_UPDATE_PENDING_MASK                                                  0x00001000L
#define VPEC_QUEUE1_CONTEXT_STATUS__WPTR_UPDATE_FAIL_COUNT_MASK                                               0x00FF0000L
//VPEC_QUEUE1_DOORBELL_LOG
#define VPEC_QUEUE1_DOORBELL_LOG__BE_ERROR__SHIFT                                                             0x0
#define VPEC_QUEUE1_DOORBELL_LOG__DATA__SHIFT                                                                 0x2
#define VPEC_QUEUE1_DOORBELL_LOG__BE_ERROR_MASK                                                               0x00000001L
#define VPEC_QUEUE1_DOORBELL_LOG__DATA_MASK                                                                   0xFFFFFFFCL
//VPEC_QUEUE1_IB_SUB_REMAIN
#define VPEC_QUEUE1_IB_SUB_REMAIN__SIZE__SHIFT                                                                0x0
#define VPEC_QUEUE1_IB_SUB_REMAIN__SIZE_MASK                                                                  0x00003FFFL
//VPEC_QUEUE1_PREEMPT
#define VPEC_QUEUE1_PREEMPT__IB_PREEMPT__SHIFT                                                                0x0
#define VPEC_QUEUE1_PREEMPT__IB_PREEMPT_MASK                                                                  0x00000001L
//VPEC_QUEUE1_LOG0BUFFER_CFG
#define VPEC_QUEUE1_LOG0BUFFER_CFG__ENABLE__SHIFT                                                             0x0
#define VPEC_QUEUE1_LOG0BUFFER_CFG__FIRST_FREE_ENTRY__SHIFT                                                   0x4
#define VPEC_QUEUE1_LOG0BUFFER_CFG__LAST_FREE_ENTRY__SHIFT                                                    0xc
#define VPEC_QUEUE1_LOG0BUFFER_CFG__RESERVED__SHIFT                                                           0x14
#define VPEC_QUEUE1_LOG0BUFFER_CFG__ENABLE_MASK                                                               0x00000001L
#define VPEC_QUEUE1_LOG0BUFFER_CFG__FIRST_FREE_ENTRY_MASK                                                     0x00000FF0L
#define VPEC_QUEUE1_LOG0BUFFER_CFG__LAST_FREE_ENTRY_MASK                                                      0x000FF000L
#define VPEC_QUEUE1_LOG0BUFFER_CFG__RESERVED_MASK                                                             0xFFF00000L
//VPEC_QUEUE1_LOG1BUFFER_CFG
#define VPEC_QUEUE1_LOG1BUFFER_CFG__ENABLE__SHIFT                                                             0x0
#define VPEC_QUEUE1_LOG1BUFFER_CFG__PARTIAL_ENTRY__SHIFT                                                      0x1
#define VPEC_QUEUE1_LOG1BUFFER_CFG__FIRST_FREE_ENTRY__SHIFT                                                   0x4
#define VPEC_QUEUE1_LOG1BUFFER_CFG__LAST_FREE_ENTRY__SHIFT                                                    0xc
#define VPEC_QUEUE1_LOG1BUFFER_CFG__RESERVED__SHIFT                                                           0x14
#define VPEC_QUEUE1_LOG1BUFFER_CFG__ENABLE_MASK                                                               0x00000001L
#define VPEC_QUEUE1_LOG1BUFFER_CFG__PARTIAL_ENTRY_MASK                                                        0x00000002L
#define VPEC_QUEUE1_LOG1BUFFER_CFG__FIRST_FREE_ENTRY_MASK                                                     0x00000FF0L
#define VPEC_QUEUE1_LOG1BUFFER_CFG__LAST_FREE_ENTRY_MASK                                                      0x000FF000L
#define VPEC_QUEUE1_LOG1BUFFER_CFG__RESERVED_MASK                                                             0xFFF00000L
//VPEC_QUEUE2_RB_CNTL
#define VPEC_QUEUE2_RB_CNTL__RB_ENABLE__SHIFT                                                                 0x0
#define VPEC_QUEUE2_RB_CNTL__RB_SIZE__SHIFT                                                                   0x1
#define VPEC_QUEUE2_RB_CNTL__WPTR_POLL_ENABLE__SHIFT                                                          0x8
#define VPEC_QUEUE2_RB_CNTL__RB_SWAP_ENABLE__SHIFT                                                            0x9
#define VPEC_QUEUE2_RB_CNTL__WPTR_POLL_SWAP_ENABLE__SHIFT                                                     0xa
#define VPEC_QUEUE2_RB_CNTL__F32_WPTR_POLL_ENABLE__SHIFT                                                      0xb
#define VPEC_QUEUE2_RB_CNTL__RPTR_WRITEBACK_ENABLE__SHIFT                                                     0xc
#define VPEC_QUEUE2_RB_CNTL__RPTR_WRITEBACK_SWAP_ENABLE__SHIFT                                                0xd
#define VPEC_QUEUE2_RB_CNTL__RPTR_WRITEBACK_TIMER__SHIFT                                                      0x10
#define VPEC_QUEUE2_RB_CNTL__RB_PRIV__SHIFT                                                                   0x17
#define VPEC_QUEUE2_RB_CNTL__RB_VMID__SHIFT                                                                   0x18
#define VPEC_QUEUE2_RB_CNTL__RB_ENABLE_MASK                                                                   0x00000001L
#define VPEC_QUEUE2_RB_CNTL__RB_SIZE_MASK                                                                     0x0000003EL
#define VPEC_QUEUE2_RB_CNTL__WPTR_POLL_ENABLE_MASK                                                            0x00000100L
#define VPEC_QUEUE2_RB_CNTL__RB_SWAP_ENABLE_MASK                                                              0x00000200L
#define VPEC_QUEUE2_RB_CNTL__WPTR_POLL_SWAP_ENABLE_MASK                                                       0x00000400L
#define VPEC_QUEUE2_RB_CNTL__F32_WPTR_POLL_ENABLE_MASK                                                        0x00000800L
#define VPEC_QUEUE2_RB_CNTL__RPTR_WRITEBACK_ENABLE_MASK                                                       0x00001000L
#define VPEC_QUEUE2_RB_CNTL__RPTR_WRITEBACK_SWAP_ENABLE_MASK                                                  0x00002000L
#define VPEC_QUEUE2_RB_CNTL__RPTR_WRITEBACK_TIMER_MASK                                                        0x001F0000L
#define VPEC_QUEUE2_RB_CNTL__RB_PRIV_MASK                                                                     0x00800000L
#define VPEC_QUEUE2_RB_CNTL__RB_VMID_MASK                                                                     0x0F000000L
//VPEC_QUEUE2_SCHEDULE_CNTL
#define VPEC_QUEUE2_SCHEDULE_CNTL__GLOBAL_ID__SHIFT                                                           0x0
#define VPEC_QUEUE2_SCHEDULE_CNTL__PROCESS_ID__SHIFT                                                          0x2
#define VPEC_QUEUE2_SCHEDULE_CNTL__LOCAL_ID__SHIFT                                                            0x6
#define VPEC_QUEUE2_SCHEDULE_CNTL__CONTEXT_QUANTUM__SHIFT                                                     0x8
#define VPEC_QUEUE2_SCHEDULE_CNTL__GLOBAL_ID_MASK                                                             0x00000003L
#define VPEC_QUEUE2_SCHEDULE_CNTL__PROCESS_ID_MASK                                                            0x0000001CL
#define VPEC_QUEUE2_SCHEDULE_CNTL__LOCAL_ID_MASK                                                              0x000000C0L
#define VPEC_QUEUE2_SCHEDULE_CNTL__CONTEXT_QUANTUM_MASK                                                       0x0000FF00L
//VPEC_QUEUE2_RB_BASE
#define VPEC_QUEUE2_RB_BASE__ADDR__SHIFT                                                                      0x0
#define VPEC_QUEUE2_RB_BASE__ADDR_MASK                                                                        0xFFFFFFFFL
//VPEC_QUEUE2_RB_BASE_HI
#define VPEC_QUEUE2_RB_BASE_HI__ADDR__SHIFT                                                                   0x0
#define VPEC_QUEUE2_RB_BASE_HI__ADDR_MASK                                                                     0x00FFFFFFL
//VPEC_QUEUE2_RB_RPTR
#define VPEC_QUEUE2_RB_RPTR__OFFSET__SHIFT                                                                    0x0
#define VPEC_QUEUE2_RB_RPTR__OFFSET_MASK                                                                      0xFFFFFFFFL
//VPEC_QUEUE2_RB_RPTR_HI
#define VPEC_QUEUE2_RB_RPTR_HI__OFFSET__SHIFT                                                                 0x0
#define VPEC_QUEUE2_RB_RPTR_HI__OFFSET_MASK                                                                   0xFFFFFFFFL
//VPEC_QUEUE2_RB_WPTR
#define VPEC_QUEUE2_RB_WPTR__OFFSET__SHIFT                                                                    0x0
#define VPEC_QUEUE2_RB_WPTR__OFFSET_MASK                                                                      0xFFFFFFFFL
//VPEC_QUEUE2_RB_WPTR_HI
#define VPEC_QUEUE2_RB_WPTR_HI__OFFSET__SHIFT                                                                 0x0
#define VPEC_QUEUE2_RB_WPTR_HI__OFFSET_MASK                                                                   0xFFFFFFFFL
//VPEC_QUEUE2_RB_RPTR_ADDR_HI
#define VPEC_QUEUE2_RB_RPTR_ADDR_HI__ADDR__SHIFT                                                              0x0
#define VPEC_QUEUE2_RB_RPTR_ADDR_HI__ADDR_MASK                                                                0xFFFFFFFFL
//VPEC_QUEUE2_RB_RPTR_ADDR_LO
#define VPEC_QUEUE2_RB_RPTR_ADDR_LO__ADDR__SHIFT                                                              0x2
#define VPEC_QUEUE2_RB_RPTR_ADDR_LO__ADDR_MASK                                                                0xFFFFFFFCL
//VPEC_QUEUE2_RB_AQL_CNTL
#define VPEC_QUEUE2_RB_AQL_CNTL__AQL_ENABLE__SHIFT                                                            0x0
#define VPEC_QUEUE2_RB_AQL_CNTL__AQL_PACKET_SIZE__SHIFT                                                       0x1
#define VPEC_QUEUE2_RB_AQL_CNTL__PACKET_STEP__SHIFT                                                           0x8
#define VPEC_QUEUE2_RB_AQL_CNTL__MIDCMD_PREEMPT_ENABLE__SHIFT                                                 0x10
#define VPEC_QUEUE2_RB_AQL_CNTL__MIDCMD_PREEMPT_DATA_RESTORE__SHIFT                                           0x11
#define VPEC_QUEUE2_RB_AQL_CNTL__OVERLAP_ENABLE__SHIFT                                                        0x12
#define VPEC_QUEUE2_RB_AQL_CNTL__AQL_ENABLE_MASK                                                              0x00000001L
#define VPEC_QUEUE2_RB_AQL_CNTL__AQL_PACKET_SIZE_MASK                                                         0x000000FEL
#define VPEC_QUEUE2_RB_AQL_CNTL__PACKET_STEP_MASK                                                             0x0000FF00L
#define VPEC_QUEUE2_RB_AQL_CNTL__MIDCMD_PREEMPT_ENABLE_MASK                                                   0x00010000L
#define VPEC_QUEUE2_RB_AQL_CNTL__MIDCMD_PREEMPT_DATA_RESTORE_MASK                                             0x00020000L
#define VPEC_QUEUE2_RB_AQL_CNTL__OVERLAP_ENABLE_MASK                                                          0x00040000L
//VPEC_QUEUE2_MINOR_PTR_UPDATE
#define VPEC_QUEUE2_MINOR_PTR_UPDATE__ENABLE__SHIFT                                                           0x0
#define VPEC_QUEUE2_MINOR_PTR_UPDATE__ENABLE_MASK                                                             0x00000001L
//VPEC_QUEUE2_CD_INFO
#define VPEC_QUEUE2_CD_INFO__CD_INFO__SHIFT                                                                   0x0
#define VPEC_QUEUE2_CD_INFO__CD_INFO_MASK                                                                     0xFFFFFFFFL
//VPEC_QUEUE2_RB_PREEMPT
#define VPEC_QUEUE2_RB_PREEMPT__PREEMPT_REQ__SHIFT                                                            0x0
#define VPEC_QUEUE2_RB_PREEMPT__PREEMPT_REQ_MASK                                                              0x00000001L
//VPEC_QUEUE2_SKIP_CNTL
#define VPEC_QUEUE2_SKIP_CNTL__SKIP_COUNT__SHIFT                                                              0x0
#define VPEC_QUEUE2_SKIP_CNTL__SKIP_COUNT_MASK                                                                0x000FFFFFL
//VPEC_QUEUE2_DOORBELL
#define VPEC_QUEUE2_DOORBELL__ENABLE__SHIFT                                                                   0x1c
#define VPEC_QUEUE2_DOORBELL__CAPTURED__SHIFT                                                                 0x1e
#define VPEC_QUEUE2_DOORBELL__ENABLE_MASK                                                                     0x10000000L
#define VPEC_QUEUE2_DOORBELL__CAPTURED_MASK                                                                   0x40000000L
//VPEC_QUEUE2_DOORBELL_OFFSET
#define VPEC_QUEUE2_DOORBELL_OFFSET__OFFSET__SHIFT                                                            0x2
#define VPEC_QUEUE2_DOORBELL_OFFSET__OFFSET_MASK                                                              0x0FFFFFFCL
//VPEC_QUEUE2_DUMMY0
#define VPEC_QUEUE2_DUMMY0__DUMMY__SHIFT                                                                      0x0
#define VPEC_QUEUE2_DUMMY0__DUMMY_MASK                                                                        0xFFFFFFFFL
//VPEC_QUEUE2_DUMMY1
#define VPEC_QUEUE2_DUMMY1__VALUE__SHIFT                                                                      0x0
#define VPEC_QUEUE2_DUMMY1__VALUE_MASK                                                                        0xFFFFFFFFL
//VPEC_QUEUE2_DUMMY2
#define VPEC_QUEUE2_DUMMY2__VALUE__SHIFT                                                                      0x0
#define VPEC_QUEUE2_DUMMY2__VALUE_MASK                                                                        0xFFFFFFFFL
//VPEC_QUEUE2_DUMMY3
#define VPEC_QUEUE2_DUMMY3__VALUE__SHIFT                                                                      0x0
#define VPEC_QUEUE2_DUMMY3__VALUE_MASK                                                                        0xFFFFFFFFL
//VPEC_QUEUE2_DUMMY4
#define VPEC_QUEUE2_DUMMY4__VALUE__SHIFT                                                                      0x0
#define VPEC_QUEUE2_DUMMY4__VALUE_MASK                                                                        0xFFFFFFFFL
//VPEC_QUEUE2_IB_CNTL
#define VPEC_QUEUE2_IB_CNTL__IB_ENABLE__SHIFT                                                                 0x0
#define VPEC_QUEUE2_IB_CNTL__SWITCH_INSIDE_IB__SHIFT                                                          0x8
#define VPEC_QUEUE2_IB_CNTL__CMD_VMID__SHIFT                                                                  0x10
#define VPEC_QUEUE2_IB_CNTL__IB_PRIV__SHIFT                                                                   0x1f
#define VPEC_QUEUE2_IB_CNTL__IB_ENABLE_MASK                                                                   0x00000001L
#define VPEC_QUEUE2_IB_CNTL__SWITCH_INSIDE_IB_MASK                                                            0x00000100L
#define VPEC_QUEUE2_IB_CNTL__CMD_VMID_MASK                                                                    0x000F0000L
#define VPEC_QUEUE2_IB_CNTL__IB_PRIV_MASK                                                                     0x80000000L
//VPEC_QUEUE2_IB_RPTR
#define VPEC_QUEUE2_IB_RPTR__OFFSET__SHIFT                                                                    0x2
#define VPEC_QUEUE2_IB_RPTR__OFFSET_MASK                                                                      0x003FFFFCL
//VPEC_QUEUE2_IB_OFFSET
#define VPEC_QUEUE2_IB_OFFSET__OFFSET__SHIFT                                                                  0x2
#define VPEC_QUEUE2_IB_OFFSET__OFFSET_MASK                                                                    0x003FFFFCL
//VPEC_QUEUE2_IB_BASE_LO
#define VPEC_QUEUE2_IB_BASE_LO__ADDR__SHIFT                                                                   0x5
#define VPEC_QUEUE2_IB_BASE_LO__ADDR_MASK                                                                     0xFFFFFFE0L
//VPEC_QUEUE2_IB_BASE_HI
#define VPEC_QUEUE2_IB_BASE_HI__ADDR__SHIFT                                                                   0x0
#define VPEC_QUEUE2_IB_BASE_HI__ADDR_MASK                                                                     0xFFFFFFFFL
//VPEC_QUEUE2_IB_SIZE
#define VPEC_QUEUE2_IB_SIZE__SIZE__SHIFT                                                                      0x0
#define VPEC_QUEUE2_IB_SIZE__SIZE_MASK                                                                        0x000FFFFFL
//VPEC_QUEUE2_CMDIB_CNTL
#define VPEC_QUEUE2_CMDIB_CNTL__IB_ENABLE__SHIFT                                                              0x0
#define VPEC_QUEUE2_CMDIB_CNTL__IB_SWAP_ENABLE__SHIFT                                                         0x4
#define VPEC_QUEUE2_CMDIB_CNTL__SWITCH_INSIDE_IB__SHIFT                                                       0x8
#define VPEC_QUEUE2_CMDIB_CNTL__CMD_VMID__SHIFT                                                               0x10
#define VPEC_QUEUE2_CMDIB_CNTL__IB_PRIV__SHIFT                                                                0x1f
#define VPEC_QUEUE2_CMDIB_CNTL__IB_ENABLE_MASK                                                                0x00000001L
#define VPEC_QUEUE2_CMDIB_CNTL__IB_SWAP_ENABLE_MASK                                                           0x00000010L
#define VPEC_QUEUE2_CMDIB_CNTL__SWITCH_INSIDE_IB_MASK                                                         0x00000100L
#define VPEC_QUEUE2_CMDIB_CNTL__CMD_VMID_MASK                                                                 0x000F0000L
#define VPEC_QUEUE2_CMDIB_CNTL__IB_PRIV_MASK                                                                  0x80000000L
//VPEC_QUEUE2_CMDIB_RPTR
#define VPEC_QUEUE2_CMDIB_RPTR__OFFSET__SHIFT                                                                 0x2
#define VPEC_QUEUE2_CMDIB_RPTR__OFFSET_MASK                                                                   0x003FFFFCL
//VPEC_QUEUE2_CMDIB_OFFSET
#define VPEC_QUEUE2_CMDIB_OFFSET__OFFSET__SHIFT                                                               0x2
#define VPEC_QUEUE2_CMDIB_OFFSET__OFFSET_MASK                                                                 0x003FFFFCL
//VPEC_QUEUE2_CMDIB_BASE_LO
#define VPEC_QUEUE2_CMDIB_BASE_LO__ADDR__SHIFT                                                                0x5
#define VPEC_QUEUE2_CMDIB_BASE_LO__ADDR_MASK                                                                  0xFFFFFFE0L
//VPEC_QUEUE2_CMDIB_BASE_HI
#define VPEC_QUEUE2_CMDIB_BASE_HI__ADDR__SHIFT                                                                0x0
#define VPEC_QUEUE2_CMDIB_BASE_HI__ADDR_MASK                                                                  0xFFFFFFFFL
//VPEC_QUEUE2_CMDIB_SIZE
#define VPEC_QUEUE2_CMDIB_SIZE__SIZE__SHIFT                                                                   0x0
#define VPEC_QUEUE2_CMDIB_SIZE__SIZE_MASK                                                                     0x000FFFFFL
//VPEC_QUEUE2_3DLUTIB_CNTL
#define VPEC_QUEUE2_3DLUTIB_CNTL__IB_ENABLE__SHIFT                                                            0x0
#define VPEC_QUEUE2_3DLUTIB_CNTL__SWITCH_INSIDE_IB__SHIFT                                                     0x8
#define VPEC_QUEUE2_3DLUTIB_CNTL__CMD_VMID__SHIFT                                                             0x10
#define VPEC_QUEUE2_3DLUTIB_CNTL__IB_PRIV__SHIFT                                                              0x1f
#define VPEC_QUEUE2_3DLUTIB_CNTL__IB_ENABLE_MASK                                                              0x00000001L
#define VPEC_QUEUE2_3DLUTIB_CNTL__SWITCH_INSIDE_IB_MASK                                                       0x00000100L
#define VPEC_QUEUE2_3DLUTIB_CNTL__CMD_VMID_MASK                                                               0x000F0000L
#define VPEC_QUEUE2_3DLUTIB_CNTL__IB_PRIV_MASK                                                                0x80000000L
//VPEC_QUEUE2_3DLUTIB_RPTR
#define VPEC_QUEUE2_3DLUTIB_RPTR__OFFSET__SHIFT                                                               0x2
#define VPEC_QUEUE2_3DLUTIB_RPTR__OFFSET_MASK                                                                 0x003FFFFCL
//VPEC_QUEUE2_3DLUTIB_OFFSET
#define VPEC_QUEUE2_3DLUTIB_OFFSET__OFFSET__SHIFT                                                             0x2
#define VPEC_QUEUE2_3DLUTIB_OFFSET__OFFSET_MASK                                                               0x003FFFFCL
//VPEC_QUEUE2_3DLUTIB_BASE_LO
#define VPEC_QUEUE2_3DLUTIB_BASE_LO__ADDR__SHIFT                                                              0x5
#define VPEC_QUEUE2_3DLUTIB_BASE_LO__ADDR_MASK                                                                0xFFFFFFE0L
//VPEC_QUEUE2_3DLUTIB_BASE_HI
#define VPEC_QUEUE2_3DLUTIB_BASE_HI__ADDR__SHIFT                                                              0x0
#define VPEC_QUEUE2_3DLUTIB_BASE_HI__ADDR_MASK                                                                0xFFFFFFFFL
//VPEC_QUEUE2_3DLUTIB_SIZE
#define VPEC_QUEUE2_3DLUTIB_SIZE__SIZE__SHIFT                                                                 0x0
#define VPEC_QUEUE2_3DLUTIB_SIZE__SIZE_MASK                                                                   0x000FFFFFL
//VPEC_QUEUE2_CSA_ADDR_LO
#define VPEC_QUEUE2_CSA_ADDR_LO__ADDR__SHIFT                                                                  0x0
#define VPEC_QUEUE2_CSA_ADDR_LO__ADDR_MASK                                                                    0xFFFFFFFFL
//VPEC_QUEUE2_CSA_ADDR_HI
#define VPEC_QUEUE2_CSA_ADDR_HI__ADDR__SHIFT                                                                  0x0
#define VPEC_QUEUE2_CSA_ADDR_HI__ADDR_MASK                                                                    0xFFFFFFFFL
//VPEC_QUEUE2_CONTEXT_STATUS
#define VPEC_QUEUE2_CONTEXT_STATUS__SELECTED__SHIFT                                                           0x0
#define VPEC_QUEUE2_CONTEXT_STATUS__USE_IB__SHIFT                                                             0x1
#define VPEC_QUEUE2_CONTEXT_STATUS__IDLE__SHIFT                                                               0x2
#define VPEC_QUEUE2_CONTEXT_STATUS__EXPIRED__SHIFT                                                            0x3
#define VPEC_QUEUE2_CONTEXT_STATUS__EXCEPTION__SHIFT                                                          0x4
#define VPEC_QUEUE2_CONTEXT_STATUS__CTXSW_ABLE__SHIFT                                                         0x7
#define VPEC_QUEUE2_CONTEXT_STATUS__USE_3DLUTIB__SHIFT                                                        0x8
#define VPEC_QUEUE2_CONTEXT_STATUS__PREEMPT_DISABLE__SHIFT                                                    0xa
#define VPEC_QUEUE2_CONTEXT_STATUS__RPTR_WB_IDLE__SHIFT                                                       0xb
#define VPEC_QUEUE2_CONTEXT_STATUS__WPTR_UPDATE_PENDING__SHIFT                                                0xc
#define VPEC_QUEUE2_CONTEXT_STATUS__WPTR_UPDATE_FAIL_COUNT__SHIFT                                             0x10
#define VPEC_QUEUE2_CONTEXT_STATUS__SELECTED_MASK                                                             0x00000001L
#define VPEC_QUEUE2_CONTEXT_STATUS__USE_IB_MASK                                                               0x00000002L
#define VPEC_QUEUE2_CONTEXT_STATUS__IDLE_MASK                                                                 0x00000004L
#define VPEC_QUEUE2_CONTEXT_STATUS__EXPIRED_MASK                                                              0x00000008L
#define VPEC_QUEUE2_CONTEXT_STATUS__EXCEPTION_MASK                                                            0x00000070L
#define VPEC_QUEUE2_CONTEXT_STATUS__CTXSW_ABLE_MASK                                                           0x00000080L
#define VPEC_QUEUE2_CONTEXT_STATUS__USE_3DLUTIB_MASK                                                          0x00000100L
#define VPEC_QUEUE2_CONTEXT_STATUS__PREEMPT_DISABLE_MASK                                                      0x00000400L
#define VPEC_QUEUE2_CONTEXT_STATUS__RPTR_WB_IDLE_MASK                                                         0x00000800L
#define VPEC_QUEUE2_CONTEXT_STATUS__WPTR_UPDATE_PENDING_MASK                                                  0x00001000L
#define VPEC_QUEUE2_CONTEXT_STATUS__WPTR_UPDATE_FAIL_COUNT_MASK                                               0x00FF0000L
//VPEC_QUEUE2_DOORBELL_LOG
#define VPEC_QUEUE2_DOORBELL_LOG__BE_ERROR__SHIFT                                                             0x0
#define VPEC_QUEUE2_DOORBELL_LOG__DATA__SHIFT                                                                 0x2
#define VPEC_QUEUE2_DOORBELL_LOG__BE_ERROR_MASK                                                               0x00000001L
#define VPEC_QUEUE2_DOORBELL_LOG__DATA_MASK                                                                   0xFFFFFFFCL
//VPEC_QUEUE2_IB_SUB_REMAIN
#define VPEC_QUEUE2_IB_SUB_REMAIN__SIZE__SHIFT                                                                0x0
#define VPEC_QUEUE2_IB_SUB_REMAIN__SIZE_MASK                                                                  0x00003FFFL
//VPEC_QUEUE2_PREEMPT
#define VPEC_QUEUE2_PREEMPT__IB_PREEMPT__SHIFT                                                                0x0
#define VPEC_QUEUE2_PREEMPT__IB_PREEMPT_MASK                                                                  0x00000001L
//VPEC_QUEUE2_LOG0BUFFER_CFG
#define VPEC_QUEUE2_LOG0BUFFER_CFG__ENABLE__SHIFT                                                             0x0
#define VPEC_QUEUE2_LOG0BUFFER_CFG__FIRST_FREE_ENTRY__SHIFT                                                   0x4
#define VPEC_QUEUE2_LOG0BUFFER_CFG__LAST_FREE_ENTRY__SHIFT                                                    0xc
#define VPEC_QUEUE2_LOG0BUFFER_CFG__RESERVED__SHIFT                                                           0x14
#define VPEC_QUEUE2_LOG0BUFFER_CFG__ENABLE_MASK                                                               0x00000001L
#define VPEC_QUEUE2_LOG0BUFFER_CFG__FIRST_FREE_ENTRY_MASK                                                     0x00000FF0L
#define VPEC_QUEUE2_LOG0BUFFER_CFG__LAST_FREE_ENTRY_MASK                                                      0x000FF000L
#define VPEC_QUEUE2_LOG0BUFFER_CFG__RESERVED_MASK                                                             0xFFF00000L
//VPEC_QUEUE2_LOG1BUFFER_CFG
#define VPEC_QUEUE2_LOG1BUFFER_CFG__ENABLE__SHIFT                                                             0x0
#define VPEC_QUEUE2_LOG1BUFFER_CFG__PARTIAL_ENTRY__SHIFT                                                      0x1
#define VPEC_QUEUE2_LOG1BUFFER_CFG__FIRST_FREE_ENTRY__SHIFT                                                   0x4
#define VPEC_QUEUE2_LOG1BUFFER_CFG__LAST_FREE_ENTRY__SHIFT                                                    0xc
#define VPEC_QUEUE2_LOG1BUFFER_CFG__RESERVED__SHIFT                                                           0x14
#define VPEC_QUEUE2_LOG1BUFFER_CFG__ENABLE_MASK                                                               0x00000001L
#define VPEC_QUEUE2_LOG1BUFFER_CFG__PARTIAL_ENTRY_MASK                                                        0x00000002L
#define VPEC_QUEUE2_LOG1BUFFER_CFG__FIRST_FREE_ENTRY_MASK                                                     0x00000FF0L
#define VPEC_QUEUE2_LOG1BUFFER_CFG__LAST_FREE_ENTRY_MASK                                                      0x000FF000L
#define VPEC_QUEUE2_LOG1BUFFER_CFG__RESERVED_MASK                                                             0xFFF00000L
//VPEC_QUEUE3_RB_CNTL
#define VPEC_QUEUE3_RB_CNTL__RB_ENABLE__SHIFT                                                                 0x0
#define VPEC_QUEUE3_RB_CNTL__RB_SIZE__SHIFT                                                                   0x1
#define VPEC_QUEUE3_RB_CNTL__WPTR_POLL_ENABLE__SHIFT                                                          0x8
#define VPEC_QUEUE3_RB_CNTL__RB_SWAP_ENABLE__SHIFT                                                            0x9
#define VPEC_QUEUE3_RB_CNTL__WPTR_POLL_SWAP_ENABLE__SHIFT                                                     0xa
#define VPEC_QUEUE3_RB_CNTL__F32_WPTR_POLL_ENABLE__SHIFT                                                      0xb
#define VPEC_QUEUE3_RB_CNTL__RPTR_WRITEBACK_ENABLE__SHIFT                                                     0xc
#define VPEC_QUEUE3_RB_CNTL__RPTR_WRITEBACK_SWAP_ENABLE__SHIFT                                                0xd
#define VPEC_QUEUE3_RB_CNTL__RPTR_WRITEBACK_TIMER__SHIFT                                                      0x10
#define VPEC_QUEUE3_RB_CNTL__RB_PRIV__SHIFT                                                                   0x17
#define VPEC_QUEUE3_RB_CNTL__RB_VMID__SHIFT                                                                   0x18
#define VPEC_QUEUE3_RB_CNTL__RB_ENABLE_MASK                                                                   0x00000001L
#define VPEC_QUEUE3_RB_CNTL__RB_SIZE_MASK                                                                     0x0000003EL
#define VPEC_QUEUE3_RB_CNTL__WPTR_POLL_ENABLE_MASK                                                            0x00000100L
#define VPEC_QUEUE3_RB_CNTL__RB_SWAP_ENABLE_MASK                                                              0x00000200L
#define VPEC_QUEUE3_RB_CNTL__WPTR_POLL_SWAP_ENABLE_MASK                                                       0x00000400L
#define VPEC_QUEUE3_RB_CNTL__F32_WPTR_POLL_ENABLE_MASK                                                        0x00000800L
#define VPEC_QUEUE3_RB_CNTL__RPTR_WRITEBACK_ENABLE_MASK                                                       0x00001000L
#define VPEC_QUEUE3_RB_CNTL__RPTR_WRITEBACK_SWAP_ENABLE_MASK                                                  0x00002000L
#define VPEC_QUEUE3_RB_CNTL__RPTR_WRITEBACK_TIMER_MASK                                                        0x001F0000L
#define VPEC_QUEUE3_RB_CNTL__RB_PRIV_MASK                                                                     0x00800000L
#define VPEC_QUEUE3_RB_CNTL__RB_VMID_MASK                                                                     0x0F000000L
//VPEC_QUEUE3_SCHEDULE_CNTL
#define VPEC_QUEUE3_SCHEDULE_CNTL__GLOBAL_ID__SHIFT                                                           0x0
#define VPEC_QUEUE3_SCHEDULE_CNTL__PROCESS_ID__SHIFT                                                          0x2
#define VPEC_QUEUE3_SCHEDULE_CNTL__LOCAL_ID__SHIFT                                                            0x6
#define VPEC_QUEUE3_SCHEDULE_CNTL__CONTEXT_QUANTUM__SHIFT                                                     0x8
#define VPEC_QUEUE3_SCHEDULE_CNTL__GLOBAL_ID_MASK                                                             0x00000003L
#define VPEC_QUEUE3_SCHEDULE_CNTL__PROCESS_ID_MASK                                                            0x0000001CL
#define VPEC_QUEUE3_SCHEDULE_CNTL__LOCAL_ID_MASK                                                              0x000000C0L
#define VPEC_QUEUE3_SCHEDULE_CNTL__CONTEXT_QUANTUM_MASK                                                       0x0000FF00L
//VPEC_QUEUE3_RB_BASE
#define VPEC_QUEUE3_RB_BASE__ADDR__SHIFT                                                                      0x0
#define VPEC_QUEUE3_RB_BASE__ADDR_MASK                                                                        0xFFFFFFFFL
//VPEC_QUEUE3_RB_BASE_HI
#define VPEC_QUEUE3_RB_BASE_HI__ADDR__SHIFT                                                                   0x0
#define VPEC_QUEUE3_RB_BASE_HI__ADDR_MASK                                                                     0x00FFFFFFL
//VPEC_QUEUE3_RB_RPTR
#define VPEC_QUEUE3_RB_RPTR__OFFSET__SHIFT                                                                    0x0
#define VPEC_QUEUE3_RB_RPTR__OFFSET_MASK                                                                      0xFFFFFFFFL
//VPEC_QUEUE3_RB_RPTR_HI
#define VPEC_QUEUE3_RB_RPTR_HI__OFFSET__SHIFT                                                                 0x0
#define VPEC_QUEUE3_RB_RPTR_HI__OFFSET_MASK                                                                   0xFFFFFFFFL
//VPEC_QUEUE3_RB_WPTR
#define VPEC_QUEUE3_RB_WPTR__OFFSET__SHIFT                                                                    0x0
#define VPEC_QUEUE3_RB_WPTR__OFFSET_MASK                                                                      0xFFFFFFFFL
//VPEC_QUEUE3_RB_WPTR_HI
#define VPEC_QUEUE3_RB_WPTR_HI__OFFSET__SHIFT                                                                 0x0
#define VPEC_QUEUE3_RB_WPTR_HI__OFFSET_MASK                                                                   0xFFFFFFFFL
//VPEC_QUEUE3_RB_RPTR_ADDR_HI
#define VPEC_QUEUE3_RB_RPTR_ADDR_HI__ADDR__SHIFT                                                              0x0
#define VPEC_QUEUE3_RB_RPTR_ADDR_HI__ADDR_MASK                                                                0xFFFFFFFFL
//VPEC_QUEUE3_RB_RPTR_ADDR_LO
#define VPEC_QUEUE3_RB_RPTR_ADDR_LO__ADDR__SHIFT                                                              0x2
#define VPEC_QUEUE3_RB_RPTR_ADDR_LO__ADDR_MASK                                                                0xFFFFFFFCL
//VPEC_QUEUE3_RB_AQL_CNTL
#define VPEC_QUEUE3_RB_AQL_CNTL__AQL_ENABLE__SHIFT                                                            0x0
#define VPEC_QUEUE3_RB_AQL_CNTL__AQL_PACKET_SIZE__SHIFT                                                       0x1
#define VPEC_QUEUE3_RB_AQL_CNTL__PACKET_STEP__SHIFT                                                           0x8
#define VPEC_QUEUE3_RB_AQL_CNTL__MIDCMD_PREEMPT_ENABLE__SHIFT                                                 0x10
#define VPEC_QUEUE3_RB_AQL_CNTL__MIDCMD_PREEMPT_DATA_RESTORE__SHIFT                                           0x11
#define VPEC_QUEUE3_RB_AQL_CNTL__OVERLAP_ENABLE__SHIFT                                                        0x12
#define VPEC_QUEUE3_RB_AQL_CNTL__AQL_ENABLE_MASK                                                              0x00000001L
#define VPEC_QUEUE3_RB_AQL_CNTL__AQL_PACKET_SIZE_MASK                                                         0x000000FEL
#define VPEC_QUEUE3_RB_AQL_CNTL__PACKET_STEP_MASK                                                             0x0000FF00L
#define VPEC_QUEUE3_RB_AQL_CNTL__MIDCMD_PREEMPT_ENABLE_MASK                                                   0x00010000L
#define VPEC_QUEUE3_RB_AQL_CNTL__MIDCMD_PREEMPT_DATA_RESTORE_MASK                                             0x00020000L
#define VPEC_QUEUE3_RB_AQL_CNTL__OVERLAP_ENABLE_MASK                                                          0x00040000L
//VPEC_QUEUE3_MINOR_PTR_UPDATE
#define VPEC_QUEUE3_MINOR_PTR_UPDATE__ENABLE__SHIFT                                                           0x0
#define VPEC_QUEUE3_MINOR_PTR_UPDATE__ENABLE_MASK                                                             0x00000001L
//VPEC_QUEUE3_CD_INFO
#define VPEC_QUEUE3_CD_INFO__CD_INFO__SHIFT                                                                   0x0
#define VPEC_QUEUE3_CD_INFO__CD_INFO_MASK                                                                     0xFFFFFFFFL
//VPEC_QUEUE3_RB_PREEMPT
#define VPEC_QUEUE3_RB_PREEMPT__PREEMPT_REQ__SHIFT                                                            0x0
#define VPEC_QUEUE3_RB_PREEMPT__PREEMPT_REQ_MASK                                                              0x00000001L
//VPEC_QUEUE3_SKIP_CNTL
#define VPEC_QUEUE3_SKIP_CNTL__SKIP_COUNT__SHIFT                                                              0x0
#define VPEC_QUEUE3_SKIP_CNTL__SKIP_COUNT_MASK                                                                0x000FFFFFL
//VPEC_QUEUE3_DOORBELL
#define VPEC_QUEUE3_DOORBELL__ENABLE__SHIFT                                                                   0x1c
#define VPEC_QUEUE3_DOORBELL__CAPTURED__SHIFT                                                                 0x1e
#define VPEC_QUEUE3_DOORBELL__ENABLE_MASK                                                                     0x10000000L
#define VPEC_QUEUE3_DOORBELL__CAPTURED_MASK                                                                   0x40000000L
//VPEC_QUEUE3_DOORBELL_OFFSET
#define VPEC_QUEUE3_DOORBELL_OFFSET__OFFSET__SHIFT                                                            0x2
#define VPEC_QUEUE3_DOORBELL_OFFSET__OFFSET_MASK                                                              0x0FFFFFFCL
//VPEC_QUEUE3_DUMMY0
#define VPEC_QUEUE3_DUMMY0__DUMMY__SHIFT                                                                      0x0
#define VPEC_QUEUE3_DUMMY0__DUMMY_MASK                                                                        0xFFFFFFFFL
//VPEC_QUEUE3_DUMMY1
#define VPEC_QUEUE3_DUMMY1__VALUE__SHIFT                                                                      0x0
#define VPEC_QUEUE3_DUMMY1__VALUE_MASK                                                                        0xFFFFFFFFL
//VPEC_QUEUE3_DUMMY2
#define VPEC_QUEUE3_DUMMY2__VALUE__SHIFT                                                                      0x0
#define VPEC_QUEUE3_DUMMY2__VALUE_MASK                                                                        0xFFFFFFFFL
//VPEC_QUEUE3_DUMMY3
#define VPEC_QUEUE3_DUMMY3__VALUE__SHIFT                                                                      0x0
#define VPEC_QUEUE3_DUMMY3__VALUE_MASK                                                                        0xFFFFFFFFL
//VPEC_QUEUE3_DUMMY4
#define VPEC_QUEUE3_DUMMY4__VALUE__SHIFT                                                                      0x0
#define VPEC_QUEUE3_DUMMY4__VALUE_MASK                                                                        0xFFFFFFFFL
//VPEC_QUEUE3_IB_CNTL
#define VPEC_QUEUE3_IB_CNTL__IB_ENABLE__SHIFT                                                                 0x0
#define VPEC_QUEUE3_IB_CNTL__SWITCH_INSIDE_IB__SHIFT                                                          0x8
#define VPEC_QUEUE3_IB_CNTL__CMD_VMID__SHIFT                                                                  0x10
#define VPEC_QUEUE3_IB_CNTL__IB_PRIV__SHIFT                                                                   0x1f
#define VPEC_QUEUE3_IB_CNTL__IB_ENABLE_MASK                                                                   0x00000001L
#define VPEC_QUEUE3_IB_CNTL__SWITCH_INSIDE_IB_MASK                                                            0x00000100L
#define VPEC_QUEUE3_IB_CNTL__CMD_VMID_MASK                                                                    0x000F0000L
#define VPEC_QUEUE3_IB_CNTL__IB_PRIV_MASK                                                                     0x80000000L
//VPEC_QUEUE3_IB_RPTR
#define VPEC_QUEUE3_IB_RPTR__OFFSET__SHIFT                                                                    0x2
#define VPEC_QUEUE3_IB_RPTR__OFFSET_MASK                                                                      0x003FFFFCL
//VPEC_QUEUE3_IB_OFFSET
#define VPEC_QUEUE3_IB_OFFSET__OFFSET__SHIFT                                                                  0x2
#define VPEC_QUEUE3_IB_OFFSET__OFFSET_MASK                                                                    0x003FFFFCL
//VPEC_QUEUE3_IB_BASE_LO
#define VPEC_QUEUE3_IB_BASE_LO__ADDR__SHIFT                                                                   0x5
#define VPEC_QUEUE3_IB_BASE_LO__ADDR_MASK                                                                     0xFFFFFFE0L
//VPEC_QUEUE3_IB_BASE_HI
#define VPEC_QUEUE3_IB_BASE_HI__ADDR__SHIFT                                                                   0x0
#define VPEC_QUEUE3_IB_BASE_HI__ADDR_MASK                                                                     0xFFFFFFFFL
//VPEC_QUEUE3_IB_SIZE
#define VPEC_QUEUE3_IB_SIZE__SIZE__SHIFT                                                                      0x0
#define VPEC_QUEUE3_IB_SIZE__SIZE_MASK                                                                        0x000FFFFFL
//VPEC_QUEUE3_CMDIB_CNTL
#define VPEC_QUEUE3_CMDIB_CNTL__IB_ENABLE__SHIFT                                                              0x0
#define VPEC_QUEUE3_CMDIB_CNTL__IB_SWAP_ENABLE__SHIFT                                                         0x4
#define VPEC_QUEUE3_CMDIB_CNTL__SWITCH_INSIDE_IB__SHIFT                                                       0x8
#define VPEC_QUEUE3_CMDIB_CNTL__CMD_VMID__SHIFT                                                               0x10
#define VPEC_QUEUE3_CMDIB_CNTL__IB_PRIV__SHIFT                                                                0x1f
#define VPEC_QUEUE3_CMDIB_CNTL__IB_ENABLE_MASK                                                                0x00000001L
#define VPEC_QUEUE3_CMDIB_CNTL__IB_SWAP_ENABLE_MASK                                                           0x00000010L
#define VPEC_QUEUE3_CMDIB_CNTL__SWITCH_INSIDE_IB_MASK                                                         0x00000100L
#define VPEC_QUEUE3_CMDIB_CNTL__CMD_VMID_MASK                                                                 0x000F0000L
#define VPEC_QUEUE3_CMDIB_CNTL__IB_PRIV_MASK                                                                  0x80000000L
//VPEC_QUEUE3_CMDIB_RPTR
#define VPEC_QUEUE3_CMDIB_RPTR__OFFSET__SHIFT                                                                 0x2
#define VPEC_QUEUE3_CMDIB_RPTR__OFFSET_MASK                                                                   0x003FFFFCL
//VPEC_QUEUE3_CMDIB_OFFSET
#define VPEC_QUEUE3_CMDIB_OFFSET__OFFSET__SHIFT                                                               0x2
#define VPEC_QUEUE3_CMDIB_OFFSET__OFFSET_MASK                                                                 0x003FFFFCL
//VPEC_QUEUE3_CMDIB_BASE_LO
#define VPEC_QUEUE3_CMDIB_BASE_LO__ADDR__SHIFT                                                                0x5
#define VPEC_QUEUE3_CMDIB_BASE_LO__ADDR_MASK                                                                  0xFFFFFFE0L
//VPEC_QUEUE3_CMDIB_BASE_HI
#define VPEC_QUEUE3_CMDIB_BASE_HI__ADDR__SHIFT                                                                0x0
#define VPEC_QUEUE3_CMDIB_BASE_HI__ADDR_MASK                                                                  0xFFFFFFFFL
//VPEC_QUEUE3_CMDIB_SIZE
#define VPEC_QUEUE3_CMDIB_SIZE__SIZE__SHIFT                                                                   0x0
#define VPEC_QUEUE3_CMDIB_SIZE__SIZE_MASK                                                                     0x000FFFFFL
//VPEC_QUEUE3_3DLUTIB_CNTL
#define VPEC_QUEUE3_3DLUTIB_CNTL__IB_ENABLE__SHIFT                                                            0x0
#define VPEC_QUEUE3_3DLUTIB_CNTL__SWITCH_INSIDE_IB__SHIFT                                                     0x8
#define VPEC_QUEUE3_3DLUTIB_CNTL__CMD_VMID__SHIFT                                                             0x10
#define VPEC_QUEUE3_3DLUTIB_CNTL__IB_PRIV__SHIFT                                                              0x1f
#define VPEC_QUEUE3_3DLUTIB_CNTL__IB_ENABLE_MASK                                                              0x00000001L
#define VPEC_QUEUE3_3DLUTIB_CNTL__SWITCH_INSIDE_IB_MASK                                                       0x00000100L
#define VPEC_QUEUE3_3DLUTIB_CNTL__CMD_VMID_MASK                                                               0x000F0000L
#define VPEC_QUEUE3_3DLUTIB_CNTL__IB_PRIV_MASK                                                                0x80000000L
//VPEC_QUEUE3_3DLUTIB_RPTR
#define VPEC_QUEUE3_3DLUTIB_RPTR__OFFSET__SHIFT                                                               0x2
#define VPEC_QUEUE3_3DLUTIB_RPTR__OFFSET_MASK                                                                 0x003FFFFCL
//VPEC_QUEUE3_3DLUTIB_OFFSET
#define VPEC_QUEUE3_3DLUTIB_OFFSET__OFFSET__SHIFT                                                             0x2
#define VPEC_QUEUE3_3DLUTIB_OFFSET__OFFSET_MASK                                                               0x003FFFFCL
//VPEC_QUEUE3_3DLUTIB_BASE_LO
#define VPEC_QUEUE3_3DLUTIB_BASE_LO__ADDR__SHIFT                                                              0x5
#define VPEC_QUEUE3_3DLUTIB_BASE_LO__ADDR_MASK                                                                0xFFFFFFE0L
//VPEC_QUEUE3_3DLUTIB_BASE_HI
#define VPEC_QUEUE3_3DLUTIB_BASE_HI__ADDR__SHIFT                                                              0x0
#define VPEC_QUEUE3_3DLUTIB_BASE_HI__ADDR_MASK                                                                0xFFFFFFFFL
//VPEC_QUEUE3_3DLUTIB_SIZE
#define VPEC_QUEUE3_3DLUTIB_SIZE__SIZE__SHIFT                                                                 0x0
#define VPEC_QUEUE3_3DLUTIB_SIZE__SIZE_MASK                                                                   0x000FFFFFL
//VPEC_QUEUE3_CSA_ADDR_LO
#define VPEC_QUEUE3_CSA_ADDR_LO__ADDR__SHIFT                                                                  0x0
#define VPEC_QUEUE3_CSA_ADDR_LO__ADDR_MASK                                                                    0xFFFFFFFFL
//VPEC_QUEUE3_CSA_ADDR_HI
#define VPEC_QUEUE3_CSA_ADDR_HI__ADDR__SHIFT                                                                  0x0
#define VPEC_QUEUE3_CSA_ADDR_HI__ADDR_MASK                                                                    0xFFFFFFFFL
//VPEC_QUEUE3_CONTEXT_STATUS
#define VPEC_QUEUE3_CONTEXT_STATUS__SELECTED__SHIFT                                                           0x0
#define VPEC_QUEUE3_CONTEXT_STATUS__USE_IB__SHIFT                                                             0x1
#define VPEC_QUEUE3_CONTEXT_STATUS__IDLE__SHIFT                                                               0x2
#define VPEC_QUEUE3_CONTEXT_STATUS__EXPIRED__SHIFT                                                            0x3
#define VPEC_QUEUE3_CONTEXT_STATUS__EXCEPTION__SHIFT                                                          0x4
#define VPEC_QUEUE3_CONTEXT_STATUS__CTXSW_ABLE__SHIFT                                                         0x7
#define VPEC_QUEUE3_CONTEXT_STATUS__USE_3DLUTIB__SHIFT                                                        0x8
#define VPEC_QUEUE3_CONTEXT_STATUS__PREEMPT_DISABLE__SHIFT                                                    0xa
#define VPEC_QUEUE3_CONTEXT_STATUS__RPTR_WB_IDLE__SHIFT                                                       0xb
#define VPEC_QUEUE3_CONTEXT_STATUS__WPTR_UPDATE_PENDING__SHIFT                                                0xc
#define VPEC_QUEUE3_CONTEXT_STATUS__WPTR_UPDATE_FAIL_COUNT__SHIFT                                             0x10
#define VPEC_QUEUE3_CONTEXT_STATUS__SELECTED_MASK                                                             0x00000001L
#define VPEC_QUEUE3_CONTEXT_STATUS__USE_IB_MASK                                                               0x00000002L
#define VPEC_QUEUE3_CONTEXT_STATUS__IDLE_MASK                                                                 0x00000004L
#define VPEC_QUEUE3_CONTEXT_STATUS__EXPIRED_MASK                                                              0x00000008L
#define VPEC_QUEUE3_CONTEXT_STATUS__EXCEPTION_MASK                                                            0x00000070L
#define VPEC_QUEUE3_CONTEXT_STATUS__CTXSW_ABLE_MASK                                                           0x00000080L
#define VPEC_QUEUE3_CONTEXT_STATUS__USE_3DLUTIB_MASK                                                          0x00000100L
#define VPEC_QUEUE3_CONTEXT_STATUS__PREEMPT_DISABLE_MASK                                                      0x00000400L
#define VPEC_QUEUE3_CONTEXT_STATUS__RPTR_WB_IDLE_MASK                                                         0x00000800L
#define VPEC_QUEUE3_CONTEXT_STATUS__WPTR_UPDATE_PENDING_MASK                                                  0x00001000L
#define VPEC_QUEUE3_CONTEXT_STATUS__WPTR_UPDATE_FAIL_COUNT_MASK                                               0x00FF0000L
//VPEC_QUEUE3_DOORBELL_LOG
#define VPEC_QUEUE3_DOORBELL_LOG__BE_ERROR__SHIFT                                                             0x0
#define VPEC_QUEUE3_DOORBELL_LOG__DATA__SHIFT                                                                 0x2
#define VPEC_QUEUE3_DOORBELL_LOG__BE_ERROR_MASK                                                               0x00000001L
#define VPEC_QUEUE3_DOORBELL_LOG__DATA_MASK                                                                   0xFFFFFFFCL
//VPEC_QUEUE3_IB_SUB_REMAIN
#define VPEC_QUEUE3_IB_SUB_REMAIN__SIZE__SHIFT                                                                0x0
#define VPEC_QUEUE3_IB_SUB_REMAIN__SIZE_MASK                                                                  0x00003FFFL
//VPEC_QUEUE3_PREEMPT
#define VPEC_QUEUE3_PREEMPT__IB_PREEMPT__SHIFT                                                                0x0
#define VPEC_QUEUE3_PREEMPT__IB_PREEMPT_MASK                                                                  0x00000001L
//VPEC_QUEUE3_LOG0BUFFER_CFG
#define VPEC_QUEUE3_LOG0BUFFER_CFG__ENABLE__SHIFT                                                             0x0
#define VPEC_QUEUE3_LOG0BUFFER_CFG__FIRST_FREE_ENTRY__SHIFT                                                   0x4
#define VPEC_QUEUE3_LOG0BUFFER_CFG__LAST_FREE_ENTRY__SHIFT                                                    0xc
#define VPEC_QUEUE3_LOG0BUFFER_CFG__RESERVED__SHIFT                                                           0x14
#define VPEC_QUEUE3_LOG0BUFFER_CFG__ENABLE_MASK                                                               0x00000001L
#define VPEC_QUEUE3_LOG0BUFFER_CFG__FIRST_FREE_ENTRY_MASK                                                     0x00000FF0L
#define VPEC_QUEUE3_LOG0BUFFER_CFG__LAST_FREE_ENTRY_MASK                                                      0x000FF000L
#define VPEC_QUEUE3_LOG0BUFFER_CFG__RESERVED_MASK                                                             0xFFF00000L
//VPEC_QUEUE3_LOG1BUFFER_CFG
#define VPEC_QUEUE3_LOG1BUFFER_CFG__ENABLE__SHIFT                                                             0x0
#define VPEC_QUEUE3_LOG1BUFFER_CFG__PARTIAL_ENTRY__SHIFT                                                      0x1
#define VPEC_QUEUE3_LOG1BUFFER_CFG__FIRST_FREE_ENTRY__SHIFT                                                   0x4
#define VPEC_QUEUE3_LOG1BUFFER_CFG__LAST_FREE_ENTRY__SHIFT                                                    0xc
#define VPEC_QUEUE3_LOG1BUFFER_CFG__RESERVED__SHIFT                                                           0x14
#define VPEC_QUEUE3_LOG1BUFFER_CFG__ENABLE_MASK                                                               0x00000001L
#define VPEC_QUEUE3_LOG1BUFFER_CFG__PARTIAL_ENTRY_MASK                                                        0x00000002L
#define VPEC_QUEUE3_LOG1BUFFER_CFG__FIRST_FREE_ENTRY_MASK                                                     0x00000FF0L
#define VPEC_QUEUE3_LOG1BUFFER_CFG__LAST_FREE_ENTRY_MASK                                                      0x000FF000L
#define VPEC_QUEUE3_LOG1BUFFER_CFG__RESERVED_MASK                                                             0xFFF00000L
//VPEC_QUEUE4_RB_CNTL
#define VPEC_QUEUE4_RB_CNTL__RB_ENABLE__SHIFT                                                                 0x0
#define VPEC_QUEUE4_RB_CNTL__RB_SIZE__SHIFT                                                                   0x1
#define VPEC_QUEUE4_RB_CNTL__WPTR_POLL_ENABLE__SHIFT                                                          0x8
#define VPEC_QUEUE4_RB_CNTL__RB_SWAP_ENABLE__SHIFT                                                            0x9
#define VPEC_QUEUE4_RB_CNTL__WPTR_POLL_SWAP_ENABLE__SHIFT                                                     0xa
#define VPEC_QUEUE4_RB_CNTL__F32_WPTR_POLL_ENABLE__SHIFT                                                      0xb
#define VPEC_QUEUE4_RB_CNTL__RPTR_WRITEBACK_ENABLE__SHIFT                                                     0xc
#define VPEC_QUEUE4_RB_CNTL__RPTR_WRITEBACK_SWAP_ENABLE__SHIFT                                                0xd
#define VPEC_QUEUE4_RB_CNTL__RPTR_WRITEBACK_TIMER__SHIFT                                                      0x10
#define VPEC_QUEUE4_RB_CNTL__RB_PRIV__SHIFT                                                                   0x17
#define VPEC_QUEUE4_RB_CNTL__RB_VMID__SHIFT                                                                   0x18
#define VPEC_QUEUE4_RB_CNTL__RB_ENABLE_MASK                                                                   0x00000001L
#define VPEC_QUEUE4_RB_CNTL__RB_SIZE_MASK                                                                     0x0000003EL
#define VPEC_QUEUE4_RB_CNTL__WPTR_POLL_ENABLE_MASK                                                            0x00000100L
#define VPEC_QUEUE4_RB_CNTL__RB_SWAP_ENABLE_MASK                                                              0x00000200L
#define VPEC_QUEUE4_RB_CNTL__WPTR_POLL_SWAP_ENABLE_MASK                                                       0x00000400L
#define VPEC_QUEUE4_RB_CNTL__F32_WPTR_POLL_ENABLE_MASK                                                        0x00000800L
#define VPEC_QUEUE4_RB_CNTL__RPTR_WRITEBACK_ENABLE_MASK                                                       0x00001000L
#define VPEC_QUEUE4_RB_CNTL__RPTR_WRITEBACK_SWAP_ENABLE_MASK                                                  0x00002000L
#define VPEC_QUEUE4_RB_CNTL__RPTR_WRITEBACK_TIMER_MASK                                                        0x001F0000L
#define VPEC_QUEUE4_RB_CNTL__RB_PRIV_MASK                                                                     0x00800000L
#define VPEC_QUEUE4_RB_CNTL__RB_VMID_MASK                                                                     0x0F000000L
//VPEC_QUEUE4_SCHEDULE_CNTL
#define VPEC_QUEUE4_SCHEDULE_CNTL__GLOBAL_ID__SHIFT                                                           0x0
#define VPEC_QUEUE4_SCHEDULE_CNTL__PROCESS_ID__SHIFT                                                          0x2
#define VPEC_QUEUE4_SCHEDULE_CNTL__LOCAL_ID__SHIFT                                                            0x6
#define VPEC_QUEUE4_SCHEDULE_CNTL__CONTEXT_QUANTUM__SHIFT                                                     0x8
#define VPEC_QUEUE4_SCHEDULE_CNTL__GLOBAL_ID_MASK                                                             0x00000003L
#define VPEC_QUEUE4_SCHEDULE_CNTL__PROCESS_ID_MASK                                                            0x0000001CL
#define VPEC_QUEUE4_SCHEDULE_CNTL__LOCAL_ID_MASK                                                              0x000000C0L
#define VPEC_QUEUE4_SCHEDULE_CNTL__CONTEXT_QUANTUM_MASK                                                       0x0000FF00L
//VPEC_QUEUE4_RB_BASE
#define VPEC_QUEUE4_RB_BASE__ADDR__SHIFT                                                                      0x0
#define VPEC_QUEUE4_RB_BASE__ADDR_MASK                                                                        0xFFFFFFFFL
//VPEC_QUEUE4_RB_BASE_HI
#define VPEC_QUEUE4_RB_BASE_HI__ADDR__SHIFT                                                                   0x0
#define VPEC_QUEUE4_RB_BASE_HI__ADDR_MASK                                                                     0x00FFFFFFL
//VPEC_QUEUE4_RB_RPTR
#define VPEC_QUEUE4_RB_RPTR__OFFSET__SHIFT                                                                    0x0
#define VPEC_QUEUE4_RB_RPTR__OFFSET_MASK                                                                      0xFFFFFFFFL
//VPEC_QUEUE4_RB_RPTR_HI
#define VPEC_QUEUE4_RB_RPTR_HI__OFFSET__SHIFT                                                                 0x0
#define VPEC_QUEUE4_RB_RPTR_HI__OFFSET_MASK                                                                   0xFFFFFFFFL
//VPEC_QUEUE4_RB_WPTR
#define VPEC_QUEUE4_RB_WPTR__OFFSET__SHIFT                                                                    0x0
#define VPEC_QUEUE4_RB_WPTR__OFFSET_MASK                                                                      0xFFFFFFFFL
//VPEC_QUEUE4_RB_WPTR_HI
#define VPEC_QUEUE4_RB_WPTR_HI__OFFSET__SHIFT                                                                 0x0
#define VPEC_QUEUE4_RB_WPTR_HI__OFFSET_MASK                                                                   0xFFFFFFFFL
//VPEC_QUEUE4_RB_RPTR_ADDR_HI
#define VPEC_QUEUE4_RB_RPTR_ADDR_HI__ADDR__SHIFT                                                              0x0
#define VPEC_QUEUE4_RB_RPTR_ADDR_HI__ADDR_MASK                                                                0xFFFFFFFFL
//VPEC_QUEUE4_RB_RPTR_ADDR_LO
#define VPEC_QUEUE4_RB_RPTR_ADDR_LO__ADDR__SHIFT                                                              0x2
#define VPEC_QUEUE4_RB_RPTR_ADDR_LO__ADDR_MASK                                                                0xFFFFFFFCL
//VPEC_QUEUE4_RB_AQL_CNTL
#define VPEC_QUEUE4_RB_AQL_CNTL__AQL_ENABLE__SHIFT                                                            0x0
#define VPEC_QUEUE4_RB_AQL_CNTL__AQL_PACKET_SIZE__SHIFT                                                       0x1
#define VPEC_QUEUE4_RB_AQL_CNTL__PACKET_STEP__SHIFT                                                           0x8
#define VPEC_QUEUE4_RB_AQL_CNTL__MIDCMD_PREEMPT_ENABLE__SHIFT                                                 0x10
#define VPEC_QUEUE4_RB_AQL_CNTL__MIDCMD_PREEMPT_DATA_RESTORE__SHIFT                                           0x11
#define VPEC_QUEUE4_RB_AQL_CNTL__OVERLAP_ENABLE__SHIFT                                                        0x12
#define VPEC_QUEUE4_RB_AQL_CNTL__AQL_ENABLE_MASK                                                              0x00000001L
#define VPEC_QUEUE4_RB_AQL_CNTL__AQL_PACKET_SIZE_MASK                                                         0x000000FEL
#define VPEC_QUEUE4_RB_AQL_CNTL__PACKET_STEP_MASK                                                             0x0000FF00L
#define VPEC_QUEUE4_RB_AQL_CNTL__MIDCMD_PREEMPT_ENABLE_MASK                                                   0x00010000L
#define VPEC_QUEUE4_RB_AQL_CNTL__MIDCMD_PREEMPT_DATA_RESTORE_MASK                                             0x00020000L
#define VPEC_QUEUE4_RB_AQL_CNTL__OVERLAP_ENABLE_MASK                                                          0x00040000L
//VPEC_QUEUE4_MINOR_PTR_UPDATE
#define VPEC_QUEUE4_MINOR_PTR_UPDATE__ENABLE__SHIFT                                                           0x0
#define VPEC_QUEUE4_MINOR_PTR_UPDATE__ENABLE_MASK                                                             0x00000001L
//VPEC_QUEUE4_CD_INFO
#define VPEC_QUEUE4_CD_INFO__CD_INFO__SHIFT                                                                   0x0
#define VPEC_QUEUE4_CD_INFO__CD_INFO_MASK                                                                     0xFFFFFFFFL
//VPEC_QUEUE4_RB_PREEMPT
#define VPEC_QUEUE4_RB_PREEMPT__PREEMPT_REQ__SHIFT                                                            0x0
#define VPEC_QUEUE4_RB_PREEMPT__PREEMPT_REQ_MASK                                                              0x00000001L
//VPEC_QUEUE4_SKIP_CNTL
#define VPEC_QUEUE4_SKIP_CNTL__SKIP_COUNT__SHIFT                                                              0x0
#define VPEC_QUEUE4_SKIP_CNTL__SKIP_COUNT_MASK                                                                0x000FFFFFL
//VPEC_QUEUE4_DOORBELL
#define VPEC_QUEUE4_DOORBELL__ENABLE__SHIFT                                                                   0x1c
#define VPEC_QUEUE4_DOORBELL__CAPTURED__SHIFT                                                                 0x1e
#define VPEC_QUEUE4_DOORBELL__ENABLE_MASK                                                                     0x10000000L
#define VPEC_QUEUE4_DOORBELL__CAPTURED_MASK                                                                   0x40000000L
//VPEC_QUEUE4_DOORBELL_OFFSET
#define VPEC_QUEUE4_DOORBELL_OFFSET__OFFSET__SHIFT                                                            0x2
#define VPEC_QUEUE4_DOORBELL_OFFSET__OFFSET_MASK                                                              0x0FFFFFFCL
//VPEC_QUEUE4_DUMMY0
#define VPEC_QUEUE4_DUMMY0__DUMMY__SHIFT                                                                      0x0
#define VPEC_QUEUE4_DUMMY0__DUMMY_MASK                                                                        0xFFFFFFFFL
//VPEC_QUEUE4_DUMMY1
#define VPEC_QUEUE4_DUMMY1__VALUE__SHIFT                                                                      0x0
#define VPEC_QUEUE4_DUMMY1__VALUE_MASK                                                                        0xFFFFFFFFL
//VPEC_QUEUE4_DUMMY2
#define VPEC_QUEUE4_DUMMY2__VALUE__SHIFT                                                                      0x0
#define VPEC_QUEUE4_DUMMY2__VALUE_MASK                                                                        0xFFFFFFFFL
//VPEC_QUEUE4_DUMMY3
#define VPEC_QUEUE4_DUMMY3__VALUE__SHIFT                                                                      0x0
#define VPEC_QUEUE4_DUMMY3__VALUE_MASK                                                                        0xFFFFFFFFL
//VPEC_QUEUE4_DUMMY4
#define VPEC_QUEUE4_DUMMY4__VALUE__SHIFT                                                                      0x0
#define VPEC_QUEUE4_DUMMY4__VALUE_MASK                                                                        0xFFFFFFFFL
//VPEC_QUEUE4_IB_CNTL
#define VPEC_QUEUE4_IB_CNTL__IB_ENABLE__SHIFT                                                                 0x0
#define VPEC_QUEUE4_IB_CNTL__SWITCH_INSIDE_IB__SHIFT                                                          0x8
#define VPEC_QUEUE4_IB_CNTL__CMD_VMID__SHIFT                                                                  0x10
#define VPEC_QUEUE4_IB_CNTL__IB_PRIV__SHIFT                                                                   0x1f
#define VPEC_QUEUE4_IB_CNTL__IB_ENABLE_MASK                                                                   0x00000001L
#define VPEC_QUEUE4_IB_CNTL__SWITCH_INSIDE_IB_MASK                                                            0x00000100L
#define VPEC_QUEUE4_IB_CNTL__CMD_VMID_MASK                                                                    0x000F0000L
#define VPEC_QUEUE4_IB_CNTL__IB_PRIV_MASK                                                                     0x80000000L
//VPEC_QUEUE4_IB_RPTR
#define VPEC_QUEUE4_IB_RPTR__OFFSET__SHIFT                                                                    0x2
#define VPEC_QUEUE4_IB_RPTR__OFFSET_MASK                                                                      0x003FFFFCL
//VPEC_QUEUE4_IB_OFFSET
#define VPEC_QUEUE4_IB_OFFSET__OFFSET__SHIFT                                                                  0x2
#define VPEC_QUEUE4_IB_OFFSET__OFFSET_MASK                                                                    0x003FFFFCL
//VPEC_QUEUE4_IB_BASE_LO
#define VPEC_QUEUE4_IB_BASE_LO__ADDR__SHIFT                                                                   0x5
#define VPEC_QUEUE4_IB_BASE_LO__ADDR_MASK                                                                     0xFFFFFFE0L
//VPEC_QUEUE4_IB_BASE_HI
#define VPEC_QUEUE4_IB_BASE_HI__ADDR__SHIFT                                                                   0x0
#define VPEC_QUEUE4_IB_BASE_HI__ADDR_MASK                                                                     0xFFFFFFFFL
//VPEC_QUEUE4_IB_SIZE
#define VPEC_QUEUE4_IB_SIZE__SIZE__SHIFT                                                                      0x0
#define VPEC_QUEUE4_IB_SIZE__SIZE_MASK                                                                        0x000FFFFFL
//VPEC_QUEUE4_CMDIB_CNTL
#define VPEC_QUEUE4_CMDIB_CNTL__IB_ENABLE__SHIFT                                                              0x0
#define VPEC_QUEUE4_CMDIB_CNTL__IB_SWAP_ENABLE__SHIFT                                                         0x4
#define VPEC_QUEUE4_CMDIB_CNTL__SWITCH_INSIDE_IB__SHIFT                                                       0x8
#define VPEC_QUEUE4_CMDIB_CNTL__CMD_VMID__SHIFT                                                               0x10
#define VPEC_QUEUE4_CMDIB_CNTL__IB_PRIV__SHIFT                                                                0x1f
#define VPEC_QUEUE4_CMDIB_CNTL__IB_ENABLE_MASK                                                                0x00000001L
#define VPEC_QUEUE4_CMDIB_CNTL__IB_SWAP_ENABLE_MASK                                                           0x00000010L
#define VPEC_QUEUE4_CMDIB_CNTL__SWITCH_INSIDE_IB_MASK                                                         0x00000100L
#define VPEC_QUEUE4_CMDIB_CNTL__CMD_VMID_MASK                                                                 0x000F0000L
#define VPEC_QUEUE4_CMDIB_CNTL__IB_PRIV_MASK                                                                  0x80000000L
//VPEC_QUEUE4_CMDIB_RPTR
#define VPEC_QUEUE4_CMDIB_RPTR__OFFSET__SHIFT                                                                 0x2
#define VPEC_QUEUE4_CMDIB_RPTR__OFFSET_MASK                                                                   0x003FFFFCL
//VPEC_QUEUE4_CMDIB_OFFSET
#define VPEC_QUEUE4_CMDIB_OFFSET__OFFSET__SHIFT                                                               0x2
#define VPEC_QUEUE4_CMDIB_OFFSET__OFFSET_MASK                                                                 0x003FFFFCL
//VPEC_QUEUE4_CMDIB_BASE_LO
#define VPEC_QUEUE4_CMDIB_BASE_LO__ADDR__SHIFT                                                                0x5
#define VPEC_QUEUE4_CMDIB_BASE_LO__ADDR_MASK                                                                  0xFFFFFFE0L
//VPEC_QUEUE4_CMDIB_BASE_HI
#define VPEC_QUEUE4_CMDIB_BASE_HI__ADDR__SHIFT                                                                0x0
#define VPEC_QUEUE4_CMDIB_BASE_HI__ADDR_MASK                                                                  0xFFFFFFFFL
//VPEC_QUEUE4_CMDIB_SIZE
#define VPEC_QUEUE4_CMDIB_SIZE__SIZE__SHIFT                                                                   0x0
#define VPEC_QUEUE4_CMDIB_SIZE__SIZE_MASK                                                                     0x000FFFFFL
//VPEC_QUEUE4_3DLUTIB_CNTL
#define VPEC_QUEUE4_3DLUTIB_CNTL__IB_ENABLE__SHIFT                                                            0x0
#define VPEC_QUEUE4_3DLUTIB_CNTL__SWITCH_INSIDE_IB__SHIFT                                                     0x8
#define VPEC_QUEUE4_3DLUTIB_CNTL__CMD_VMID__SHIFT                                                             0x10
#define VPEC_QUEUE4_3DLUTIB_CNTL__IB_PRIV__SHIFT                                                              0x1f
#define VPEC_QUEUE4_3DLUTIB_CNTL__IB_ENABLE_MASK                                                              0x00000001L
#define VPEC_QUEUE4_3DLUTIB_CNTL__SWITCH_INSIDE_IB_MASK                                                       0x00000100L
#define VPEC_QUEUE4_3DLUTIB_CNTL__CMD_VMID_MASK                                                               0x000F0000L
#define VPEC_QUEUE4_3DLUTIB_CNTL__IB_PRIV_MASK                                                                0x80000000L
//VPEC_QUEUE4_3DLUTIB_RPTR
#define VPEC_QUEUE4_3DLUTIB_RPTR__OFFSET__SHIFT                                                               0x2
#define VPEC_QUEUE4_3DLUTIB_RPTR__OFFSET_MASK                                                                 0x003FFFFCL
//VPEC_QUEUE4_3DLUTIB_OFFSET
#define VPEC_QUEUE4_3DLUTIB_OFFSET__OFFSET__SHIFT                                                             0x2
#define VPEC_QUEUE4_3DLUTIB_OFFSET__OFFSET_MASK                                                               0x003FFFFCL
//VPEC_QUEUE4_3DLUTIB_BASE_LO
#define VPEC_QUEUE4_3DLUTIB_BASE_LO__ADDR__SHIFT                                                              0x5
#define VPEC_QUEUE4_3DLUTIB_BASE_LO__ADDR_MASK                                                                0xFFFFFFE0L
//VPEC_QUEUE4_3DLUTIB_BASE_HI
#define VPEC_QUEUE4_3DLUTIB_BASE_HI__ADDR__SHIFT                                                              0x0
#define VPEC_QUEUE4_3DLUTIB_BASE_HI__ADDR_MASK                                                                0xFFFFFFFFL
//VPEC_QUEUE4_3DLUTIB_SIZE
#define VPEC_QUEUE4_3DLUTIB_SIZE__SIZE__SHIFT                                                                 0x0
#define VPEC_QUEUE4_3DLUTIB_SIZE__SIZE_MASK                                                                   0x000FFFFFL
//VPEC_QUEUE4_CSA_ADDR_LO
#define VPEC_QUEUE4_CSA_ADDR_LO__ADDR__SHIFT                                                                  0x0
#define VPEC_QUEUE4_CSA_ADDR_LO__ADDR_MASK                                                                    0xFFFFFFFFL
//VPEC_QUEUE4_CSA_ADDR_HI
#define VPEC_QUEUE4_CSA_ADDR_HI__ADDR__SHIFT                                                                  0x0
#define VPEC_QUEUE4_CSA_ADDR_HI__ADDR_MASK                                                                    0xFFFFFFFFL
//VPEC_QUEUE4_CONTEXT_STATUS
#define VPEC_QUEUE4_CONTEXT_STATUS__SELECTED__SHIFT                                                           0x0
#define VPEC_QUEUE4_CONTEXT_STATUS__USE_IB__SHIFT                                                             0x1
#define VPEC_QUEUE4_CONTEXT_STATUS__IDLE__SHIFT                                                               0x2
#define VPEC_QUEUE4_CONTEXT_STATUS__EXPIRED__SHIFT                                                            0x3
#define VPEC_QUEUE4_CONTEXT_STATUS__EXCEPTION__SHIFT                                                          0x4
#define VPEC_QUEUE4_CONTEXT_STATUS__CTXSW_ABLE__SHIFT                                                         0x7
#define VPEC_QUEUE4_CONTEXT_STATUS__USE_3DLUTIB__SHIFT                                                        0x8
#define VPEC_QUEUE4_CONTEXT_STATUS__PREEMPT_DISABLE__SHIFT                                                    0xa
#define VPEC_QUEUE4_CONTEXT_STATUS__RPTR_WB_IDLE__SHIFT                                                       0xb
#define VPEC_QUEUE4_CONTEXT_STATUS__WPTR_UPDATE_PENDING__SHIFT                                                0xc
#define VPEC_QUEUE4_CONTEXT_STATUS__WPTR_UPDATE_FAIL_COUNT__SHIFT                                             0x10
#define VPEC_QUEUE4_CONTEXT_STATUS__SELECTED_MASK                                                             0x00000001L
#define VPEC_QUEUE4_CONTEXT_STATUS__USE_IB_MASK                                                               0x00000002L
#define VPEC_QUEUE4_CONTEXT_STATUS__IDLE_MASK                                                                 0x00000004L
#define VPEC_QUEUE4_CONTEXT_STATUS__EXPIRED_MASK                                                              0x00000008L
#define VPEC_QUEUE4_CONTEXT_STATUS__EXCEPTION_MASK                                                            0x00000070L
#define VPEC_QUEUE4_CONTEXT_STATUS__CTXSW_ABLE_MASK                                                           0x00000080L
#define VPEC_QUEUE4_CONTEXT_STATUS__USE_3DLUTIB_MASK                                                          0x00000100L
#define VPEC_QUEUE4_CONTEXT_STATUS__PREEMPT_DISABLE_MASK                                                      0x00000400L
#define VPEC_QUEUE4_CONTEXT_STATUS__RPTR_WB_IDLE_MASK                                                         0x00000800L
#define VPEC_QUEUE4_CONTEXT_STATUS__WPTR_UPDATE_PENDING_MASK                                                  0x00001000L
#define VPEC_QUEUE4_CONTEXT_STATUS__WPTR_UPDATE_FAIL_COUNT_MASK                                               0x00FF0000L
//VPEC_QUEUE4_DOORBELL_LOG
#define VPEC_QUEUE4_DOORBELL_LOG__BE_ERROR__SHIFT                                                             0x0
#define VPEC_QUEUE4_DOORBELL_LOG__DATA__SHIFT                                                                 0x2
#define VPEC_QUEUE4_DOORBELL_LOG__BE_ERROR_MASK                                                               0x00000001L
#define VPEC_QUEUE4_DOORBELL_LOG__DATA_MASK                                                                   0xFFFFFFFCL
//VPEC_QUEUE4_IB_SUB_REMAIN
#define VPEC_QUEUE4_IB_SUB_REMAIN__SIZE__SHIFT                                                                0x0
#define VPEC_QUEUE4_IB_SUB_REMAIN__SIZE_MASK                                                                  0x00003FFFL
//VPEC_QUEUE4_PREEMPT
#define VPEC_QUEUE4_PREEMPT__IB_PREEMPT__SHIFT                                                                0x0
#define VPEC_QUEUE4_PREEMPT__IB_PREEMPT_MASK                                                                  0x00000001L
//VPEC_QUEUE4_LOG0BUFFER_CFG
#define VPEC_QUEUE4_LOG0BUFFER_CFG__ENABLE__SHIFT                                                             0x0
#define VPEC_QUEUE4_LOG0BUFFER_CFG__FIRST_FREE_ENTRY__SHIFT                                                   0x4
#define VPEC_QUEUE4_LOG0BUFFER_CFG__LAST_FREE_ENTRY__SHIFT                                                    0xc
#define VPEC_QUEUE4_LOG0BUFFER_CFG__RESERVED__SHIFT                                                           0x14
#define VPEC_QUEUE4_LOG0BUFFER_CFG__ENABLE_MASK                                                               0x00000001L
#define VPEC_QUEUE4_LOG0BUFFER_CFG__FIRST_FREE_ENTRY_MASK                                                     0x00000FF0L
#define VPEC_QUEUE4_LOG0BUFFER_CFG__LAST_FREE_ENTRY_MASK                                                      0x000FF000L
#define VPEC_QUEUE4_LOG0BUFFER_CFG__RESERVED_MASK                                                             0xFFF00000L
//VPEC_QUEUE4_LOG1BUFFER_CFG
#define VPEC_QUEUE4_LOG1BUFFER_CFG__ENABLE__SHIFT                                                             0x0
#define VPEC_QUEUE4_LOG1BUFFER_CFG__PARTIAL_ENTRY__SHIFT                                                      0x1
#define VPEC_QUEUE4_LOG1BUFFER_CFG__FIRST_FREE_ENTRY__SHIFT                                                   0x4
#define VPEC_QUEUE4_LOG1BUFFER_CFG__LAST_FREE_ENTRY__SHIFT                                                    0xc
#define VPEC_QUEUE4_LOG1BUFFER_CFG__RESERVED__SHIFT                                                           0x14
#define VPEC_QUEUE4_LOG1BUFFER_CFG__ENABLE_MASK                                                               0x00000001L
#define VPEC_QUEUE4_LOG1BUFFER_CFG__PARTIAL_ENTRY_MASK                                                        0x00000002L
#define VPEC_QUEUE4_LOG1BUFFER_CFG__FIRST_FREE_ENTRY_MASK                                                     0x00000FF0L
#define VPEC_QUEUE4_LOG1BUFFER_CFG__LAST_FREE_ENTRY_MASK                                                      0x000FF000L
#define VPEC_QUEUE4_LOG1BUFFER_CFG__RESERVED_MASK                                                             0xFFF00000L
//VPEC_QUEUE5_RB_CNTL
#define VPEC_QUEUE5_RB_CNTL__RB_ENABLE__SHIFT                                                                 0x0
#define VPEC_QUEUE5_RB_CNTL__RB_SIZE__SHIFT                                                                   0x1
#define VPEC_QUEUE5_RB_CNTL__WPTR_POLL_ENABLE__SHIFT                                                          0x8
#define VPEC_QUEUE5_RB_CNTL__RB_SWAP_ENABLE__SHIFT                                                            0x9
#define VPEC_QUEUE5_RB_CNTL__WPTR_POLL_SWAP_ENABLE__SHIFT                                                     0xa
#define VPEC_QUEUE5_RB_CNTL__F32_WPTR_POLL_ENABLE__SHIFT                                                      0xb
#define VPEC_QUEUE5_RB_CNTL__RPTR_WRITEBACK_ENABLE__SHIFT                                                     0xc
#define VPEC_QUEUE5_RB_CNTL__RPTR_WRITEBACK_SWAP_ENABLE__SHIFT                                                0xd
#define VPEC_QUEUE5_RB_CNTL__RPTR_WRITEBACK_TIMER__SHIFT                                                      0x10
#define VPEC_QUEUE5_RB_CNTL__RB_PRIV__SHIFT                                                                   0x17
#define VPEC_QUEUE5_RB_CNTL__RB_VMID__SHIFT                                                                   0x18
#define VPEC_QUEUE5_RB_CNTL__RB_ENABLE_MASK                                                                   0x00000001L
#define VPEC_QUEUE5_RB_CNTL__RB_SIZE_MASK                                                                     0x0000003EL
#define VPEC_QUEUE5_RB_CNTL__WPTR_POLL_ENABLE_MASK                                                            0x00000100L
#define VPEC_QUEUE5_RB_CNTL__RB_SWAP_ENABLE_MASK                                                              0x00000200L
#define VPEC_QUEUE5_RB_CNTL__WPTR_POLL_SWAP_ENABLE_MASK                                                       0x00000400L
#define VPEC_QUEUE5_RB_CNTL__F32_WPTR_POLL_ENABLE_MASK                                                        0x00000800L
#define VPEC_QUEUE5_RB_CNTL__RPTR_WRITEBACK_ENABLE_MASK                                                       0x00001000L
#define VPEC_QUEUE5_RB_CNTL__RPTR_WRITEBACK_SWAP_ENABLE_MASK                                                  0x00002000L
#define VPEC_QUEUE5_RB_CNTL__RPTR_WRITEBACK_TIMER_MASK                                                        0x001F0000L
#define VPEC_QUEUE5_RB_CNTL__RB_PRIV_MASK                                                                     0x00800000L
#define VPEC_QUEUE5_RB_CNTL__RB_VMID_MASK                                                                     0x0F000000L
//VPEC_QUEUE5_SCHEDULE_CNTL
#define VPEC_QUEUE5_SCHEDULE_CNTL__GLOBAL_ID__SHIFT                                                           0x0
#define VPEC_QUEUE5_SCHEDULE_CNTL__PROCESS_ID__SHIFT                                                          0x2
#define VPEC_QUEUE5_SCHEDULE_CNTL__LOCAL_ID__SHIFT                                                            0x6
#define VPEC_QUEUE5_SCHEDULE_CNTL__CONTEXT_QUANTUM__SHIFT                                                     0x8
#define VPEC_QUEUE5_SCHEDULE_CNTL__GLOBAL_ID_MASK                                                             0x00000003L
#define VPEC_QUEUE5_SCHEDULE_CNTL__PROCESS_ID_MASK                                                            0x0000001CL
#define VPEC_QUEUE5_SCHEDULE_CNTL__LOCAL_ID_MASK                                                              0x000000C0L
#define VPEC_QUEUE5_SCHEDULE_CNTL__CONTEXT_QUANTUM_MASK                                                       0x0000FF00L
//VPEC_QUEUE5_RB_BASE
#define VPEC_QUEUE5_RB_BASE__ADDR__SHIFT                                                                      0x0
#define VPEC_QUEUE5_RB_BASE__ADDR_MASK                                                                        0xFFFFFFFFL
//VPEC_QUEUE5_RB_BASE_HI
#define VPEC_QUEUE5_RB_BASE_HI__ADDR__SHIFT                                                                   0x0
#define VPEC_QUEUE5_RB_BASE_HI__ADDR_MASK                                                                     0x00FFFFFFL
//VPEC_QUEUE5_RB_RPTR
#define VPEC_QUEUE5_RB_RPTR__OFFSET__SHIFT                                                                    0x0
#define VPEC_QUEUE5_RB_RPTR__OFFSET_MASK                                                                      0xFFFFFFFFL
//VPEC_QUEUE5_RB_RPTR_HI
#define VPEC_QUEUE5_RB_RPTR_HI__OFFSET__SHIFT                                                                 0x0
#define VPEC_QUEUE5_RB_RPTR_HI__OFFSET_MASK                                                                   0xFFFFFFFFL
//VPEC_QUEUE5_RB_WPTR
#define VPEC_QUEUE5_RB_WPTR__OFFSET__SHIFT                                                                    0x0
#define VPEC_QUEUE5_RB_WPTR__OFFSET_MASK                                                                      0xFFFFFFFFL
//VPEC_QUEUE5_RB_WPTR_HI
#define VPEC_QUEUE5_RB_WPTR_HI__OFFSET__SHIFT                                                                 0x0
#define VPEC_QUEUE5_RB_WPTR_HI__OFFSET_MASK                                                                   0xFFFFFFFFL
//VPEC_QUEUE5_RB_RPTR_ADDR_HI
#define VPEC_QUEUE5_RB_RPTR_ADDR_HI__ADDR__SHIFT                                                              0x0
#define VPEC_QUEUE5_RB_RPTR_ADDR_HI__ADDR_MASK                                                                0xFFFFFFFFL
//VPEC_QUEUE5_RB_RPTR_ADDR_LO
#define VPEC_QUEUE5_RB_RPTR_ADDR_LO__ADDR__SHIFT                                                              0x2
#define VPEC_QUEUE5_RB_RPTR_ADDR_LO__ADDR_MASK                                                                0xFFFFFFFCL
//VPEC_QUEUE5_RB_AQL_CNTL
#define VPEC_QUEUE5_RB_AQL_CNTL__AQL_ENABLE__SHIFT                                                            0x0
#define VPEC_QUEUE5_RB_AQL_CNTL__AQL_PACKET_SIZE__SHIFT                                                       0x1
#define VPEC_QUEUE5_RB_AQL_CNTL__PACKET_STEP__SHIFT                                                           0x8
#define VPEC_QUEUE5_RB_AQL_CNTL__MIDCMD_PREEMPT_ENABLE__SHIFT                                                 0x10
#define VPEC_QUEUE5_RB_AQL_CNTL__MIDCMD_PREEMPT_DATA_RESTORE__SHIFT                                           0x11
#define VPEC_QUEUE5_RB_AQL_CNTL__OVERLAP_ENABLE__SHIFT                                                        0x12
#define VPEC_QUEUE5_RB_AQL_CNTL__AQL_ENABLE_MASK                                                              0x00000001L
#define VPEC_QUEUE5_RB_AQL_CNTL__AQL_PACKET_SIZE_MASK                                                         0x000000FEL
#define VPEC_QUEUE5_RB_AQL_CNTL__PACKET_STEP_MASK                                                             0x0000FF00L
#define VPEC_QUEUE5_RB_AQL_CNTL__MIDCMD_PREEMPT_ENABLE_MASK                                                   0x00010000L
#define VPEC_QUEUE5_RB_AQL_CNTL__MIDCMD_PREEMPT_DATA_RESTORE_MASK                                             0x00020000L
#define VPEC_QUEUE5_RB_AQL_CNTL__OVERLAP_ENABLE_MASK                                                          0x00040000L
//VPEC_QUEUE5_MINOR_PTR_UPDATE
#define VPEC_QUEUE5_MINOR_PTR_UPDATE__ENABLE__SHIFT                                                           0x0
#define VPEC_QUEUE5_MINOR_PTR_UPDATE__ENABLE_MASK                                                             0x00000001L
//VPEC_QUEUE5_CD_INFO
#define VPEC_QUEUE5_CD_INFO__CD_INFO__SHIFT                                                                   0x0
#define VPEC_QUEUE5_CD_INFO__CD_INFO_MASK                                                                     0xFFFFFFFFL
//VPEC_QUEUE5_RB_PREEMPT
#define VPEC_QUEUE5_RB_PREEMPT__PREEMPT_REQ__SHIFT                                                            0x0
#define VPEC_QUEUE5_RB_PREEMPT__PREEMPT_REQ_MASK                                                              0x00000001L
//VPEC_QUEUE5_SKIP_CNTL
#define VPEC_QUEUE5_SKIP_CNTL__SKIP_COUNT__SHIFT                                                              0x0
#define VPEC_QUEUE5_SKIP_CNTL__SKIP_COUNT_MASK                                                                0x000FFFFFL
//VPEC_QUEUE5_DOORBELL
#define VPEC_QUEUE5_DOORBELL__ENABLE__SHIFT                                                                   0x1c
#define VPEC_QUEUE5_DOORBELL__CAPTURED__SHIFT                                                                 0x1e
#define VPEC_QUEUE5_DOORBELL__ENABLE_MASK                                                                     0x10000000L
#define VPEC_QUEUE5_DOORBELL__CAPTURED_MASK                                                                   0x40000000L
//VPEC_QUEUE5_DOORBELL_OFFSET
#define VPEC_QUEUE5_DOORBELL_OFFSET__OFFSET__SHIFT                                                            0x2
#define VPEC_QUEUE5_DOORBELL_OFFSET__OFFSET_MASK                                                              0x0FFFFFFCL
//VPEC_QUEUE5_DUMMY0
#define VPEC_QUEUE5_DUMMY0__DUMMY__SHIFT                                                                      0x0
#define VPEC_QUEUE5_DUMMY0__DUMMY_MASK                                                                        0xFFFFFFFFL
//VPEC_QUEUE5_DUMMY1
#define VPEC_QUEUE5_DUMMY1__VALUE__SHIFT                                                                      0x0
#define VPEC_QUEUE5_DUMMY1__VALUE_MASK                                                                        0xFFFFFFFFL
//VPEC_QUEUE5_DUMMY2
#define VPEC_QUEUE5_DUMMY2__VALUE__SHIFT                                                                      0x0
#define VPEC_QUEUE5_DUMMY2__VALUE_MASK                                                                        0xFFFFFFFFL
//VPEC_QUEUE5_DUMMY3
#define VPEC_QUEUE5_DUMMY3__VALUE__SHIFT                                                                      0x0
#define VPEC_QUEUE5_DUMMY3__VALUE_MASK                                                                        0xFFFFFFFFL
//VPEC_QUEUE5_DUMMY4
#define VPEC_QUEUE5_DUMMY4__VALUE__SHIFT                                                                      0x0
#define VPEC_QUEUE5_DUMMY4__VALUE_MASK                                                                        0xFFFFFFFFL
//VPEC_QUEUE5_IB_CNTL
#define VPEC_QUEUE5_IB_CNTL__IB_ENABLE__SHIFT                                                                 0x0
#define VPEC_QUEUE5_IB_CNTL__SWITCH_INSIDE_IB__SHIFT                                                          0x8
#define VPEC_QUEUE5_IB_CNTL__CMD_VMID__SHIFT                                                                  0x10
#define VPEC_QUEUE5_IB_CNTL__IB_PRIV__SHIFT                                                                   0x1f
#define VPEC_QUEUE5_IB_CNTL__IB_ENABLE_MASK                                                                   0x00000001L
#define VPEC_QUEUE5_IB_CNTL__SWITCH_INSIDE_IB_MASK                                                            0x00000100L
#define VPEC_QUEUE5_IB_CNTL__CMD_VMID_MASK                                                                    0x000F0000L
#define VPEC_QUEUE5_IB_CNTL__IB_PRIV_MASK                                                                     0x80000000L
//VPEC_QUEUE5_IB_RPTR
#define VPEC_QUEUE5_IB_RPTR__OFFSET__SHIFT                                                                    0x2
#define VPEC_QUEUE5_IB_RPTR__OFFSET_MASK                                                                      0x003FFFFCL
//VPEC_QUEUE5_IB_OFFSET
#define VPEC_QUEUE5_IB_OFFSET__OFFSET__SHIFT                                                                  0x2
#define VPEC_QUEUE5_IB_OFFSET__OFFSET_MASK                                                                    0x003FFFFCL
//VPEC_QUEUE5_IB_BASE_LO
#define VPEC_QUEUE5_IB_BASE_LO__ADDR__SHIFT                                                                   0x5
#define VPEC_QUEUE5_IB_BASE_LO__ADDR_MASK                                                                     0xFFFFFFE0L
//VPEC_QUEUE5_IB_BASE_HI
#define VPEC_QUEUE5_IB_BASE_HI__ADDR__SHIFT                                                                   0x0
#define VPEC_QUEUE5_IB_BASE_HI__ADDR_MASK                                                                     0xFFFFFFFFL
//VPEC_QUEUE5_IB_SIZE
#define VPEC_QUEUE5_IB_SIZE__SIZE__SHIFT                                                                      0x0
#define VPEC_QUEUE5_IB_SIZE__SIZE_MASK                                                                        0x000FFFFFL
//VPEC_QUEUE5_CMDIB_CNTL
#define VPEC_QUEUE5_CMDIB_CNTL__IB_ENABLE__SHIFT                                                              0x0
#define VPEC_QUEUE5_CMDIB_CNTL__IB_SWAP_ENABLE__SHIFT                                                         0x4
#define VPEC_QUEUE5_CMDIB_CNTL__SWITCH_INSIDE_IB__SHIFT                                                       0x8
#define VPEC_QUEUE5_CMDIB_CNTL__CMD_VMID__SHIFT                                                               0x10
#define VPEC_QUEUE5_CMDIB_CNTL__IB_PRIV__SHIFT                                                                0x1f
#define VPEC_QUEUE5_CMDIB_CNTL__IB_ENABLE_MASK                                                                0x00000001L
#define VPEC_QUEUE5_CMDIB_CNTL__IB_SWAP_ENABLE_MASK                                                           0x00000010L
#define VPEC_QUEUE5_CMDIB_CNTL__SWITCH_INSIDE_IB_MASK                                                         0x00000100L
#define VPEC_QUEUE5_CMDIB_CNTL__CMD_VMID_MASK                                                                 0x000F0000L
#define VPEC_QUEUE5_CMDIB_CNTL__IB_PRIV_MASK                                                                  0x80000000L
//VPEC_QUEUE5_CMDIB_RPTR
#define VPEC_QUEUE5_CMDIB_RPTR__OFFSET__SHIFT                                                                 0x2
#define VPEC_QUEUE5_CMDIB_RPTR__OFFSET_MASK                                                                   0x003FFFFCL
//VPEC_QUEUE5_CMDIB_OFFSET
#define VPEC_QUEUE5_CMDIB_OFFSET__OFFSET__SHIFT                                                               0x2
#define VPEC_QUEUE5_CMDIB_OFFSET__OFFSET_MASK                                                                 0x003FFFFCL
//VPEC_QUEUE5_CMDIB_BASE_LO
#define VPEC_QUEUE5_CMDIB_BASE_LO__ADDR__SHIFT                                                                0x5
#define VPEC_QUEUE5_CMDIB_BASE_LO__ADDR_MASK                                                                  0xFFFFFFE0L
//VPEC_QUEUE5_CMDIB_BASE_HI
#define VPEC_QUEUE5_CMDIB_BASE_HI__ADDR__SHIFT                                                                0x0
#define VPEC_QUEUE5_CMDIB_BASE_HI__ADDR_MASK                                                                  0xFFFFFFFFL
//VPEC_QUEUE5_CMDIB_SIZE
#define VPEC_QUEUE5_CMDIB_SIZE__SIZE__SHIFT                                                                   0x0
#define VPEC_QUEUE5_CMDIB_SIZE__SIZE_MASK                                                                     0x000FFFFFL
//VPEC_QUEUE5_3DLUTIB_CNTL
#define VPEC_QUEUE5_3DLUTIB_CNTL__IB_ENABLE__SHIFT                                                            0x0
#define VPEC_QUEUE5_3DLUTIB_CNTL__SWITCH_INSIDE_IB__SHIFT                                                     0x8
#define VPEC_QUEUE5_3DLUTIB_CNTL__CMD_VMID__SHIFT                                                             0x10
#define VPEC_QUEUE5_3DLUTIB_CNTL__IB_PRIV__SHIFT                                                              0x1f
#define VPEC_QUEUE5_3DLUTIB_CNTL__IB_ENABLE_MASK                                                              0x00000001L
#define VPEC_QUEUE5_3DLUTIB_CNTL__SWITCH_INSIDE_IB_MASK                                                       0x00000100L
#define VPEC_QUEUE5_3DLUTIB_CNTL__CMD_VMID_MASK                                                               0x000F0000L
#define VPEC_QUEUE5_3DLUTIB_CNTL__IB_PRIV_MASK                                                                0x80000000L
//VPEC_QUEUE5_3DLUTIB_RPTR
#define VPEC_QUEUE5_3DLUTIB_RPTR__OFFSET__SHIFT                                                               0x2
#define VPEC_QUEUE5_3DLUTIB_RPTR__OFFSET_MASK                                                                 0x003FFFFCL
//VPEC_QUEUE5_3DLUTIB_OFFSET
#define VPEC_QUEUE5_3DLUTIB_OFFSET__OFFSET__SHIFT                                                             0x2
#define VPEC_QUEUE5_3DLUTIB_OFFSET__OFFSET_MASK                                                               0x003FFFFCL
//VPEC_QUEUE5_3DLUTIB_BASE_LO
#define VPEC_QUEUE5_3DLUTIB_BASE_LO__ADDR__SHIFT                                                              0x5
#define VPEC_QUEUE5_3DLUTIB_BASE_LO__ADDR_MASK                                                                0xFFFFFFE0L
//VPEC_QUEUE5_3DLUTIB_BASE_HI
#define VPEC_QUEUE5_3DLUTIB_BASE_HI__ADDR__SHIFT                                                              0x0
#define VPEC_QUEUE5_3DLUTIB_BASE_HI__ADDR_MASK                                                                0xFFFFFFFFL
//VPEC_QUEUE5_3DLUTIB_SIZE
#define VPEC_QUEUE5_3DLUTIB_SIZE__SIZE__SHIFT                                                                 0x0
#define VPEC_QUEUE5_3DLUTIB_SIZE__SIZE_MASK                                                                   0x000FFFFFL
//VPEC_QUEUE5_CSA_ADDR_LO
#define VPEC_QUEUE5_CSA_ADDR_LO__ADDR__SHIFT                                                                  0x0
#define VPEC_QUEUE5_CSA_ADDR_LO__ADDR_MASK                                                                    0xFFFFFFFFL
//VPEC_QUEUE5_CSA_ADDR_HI
#define VPEC_QUEUE5_CSA_ADDR_HI__ADDR__SHIFT                                                                  0x0
#define VPEC_QUEUE5_CSA_ADDR_HI__ADDR_MASK                                                                    0xFFFFFFFFL
//VPEC_QUEUE5_CONTEXT_STATUS
#define VPEC_QUEUE5_CONTEXT_STATUS__SELECTED__SHIFT                                                           0x0
#define VPEC_QUEUE5_CONTEXT_STATUS__USE_IB__SHIFT                                                             0x1
#define VPEC_QUEUE5_CONTEXT_STATUS__IDLE__SHIFT                                                               0x2
#define VPEC_QUEUE5_CONTEXT_STATUS__EXPIRED__SHIFT                                                            0x3
#define VPEC_QUEUE5_CONTEXT_STATUS__EXCEPTION__SHIFT                                                          0x4
#define VPEC_QUEUE5_CONTEXT_STATUS__CTXSW_ABLE__SHIFT                                                         0x7
#define VPEC_QUEUE5_CONTEXT_STATUS__USE_3DLUTIB__SHIFT                                                        0x8
#define VPEC_QUEUE5_CONTEXT_STATUS__PREEMPT_DISABLE__SHIFT                                                    0xa
#define VPEC_QUEUE5_CONTEXT_STATUS__RPTR_WB_IDLE__SHIFT                                                       0xb
#define VPEC_QUEUE5_CONTEXT_STATUS__WPTR_UPDATE_PENDING__SHIFT                                                0xc
#define VPEC_QUEUE5_CONTEXT_STATUS__WPTR_UPDATE_FAIL_COUNT__SHIFT                                             0x10
#define VPEC_QUEUE5_CONTEXT_STATUS__SELECTED_MASK                                                             0x00000001L
#define VPEC_QUEUE5_CONTEXT_STATUS__USE_IB_MASK                                                               0x00000002L
#define VPEC_QUEUE5_CONTEXT_STATUS__IDLE_MASK                                                                 0x00000004L
#define VPEC_QUEUE5_CONTEXT_STATUS__EXPIRED_MASK                                                              0x00000008L
#define VPEC_QUEUE5_CONTEXT_STATUS__EXCEPTION_MASK                                                            0x00000070L
#define VPEC_QUEUE5_CONTEXT_STATUS__CTXSW_ABLE_MASK                                                           0x00000080L
#define VPEC_QUEUE5_CONTEXT_STATUS__USE_3DLUTIB_MASK                                                          0x00000100L
#define VPEC_QUEUE5_CONTEXT_STATUS__PREEMPT_DISABLE_MASK                                                      0x00000400L
#define VPEC_QUEUE5_CONTEXT_STATUS__RPTR_WB_IDLE_MASK                                                         0x00000800L
#define VPEC_QUEUE5_CONTEXT_STATUS__WPTR_UPDATE_PENDING_MASK                                                  0x00001000L
#define VPEC_QUEUE5_CONTEXT_STATUS__WPTR_UPDATE_FAIL_COUNT_MASK                                               0x00FF0000L
//VPEC_QUEUE5_DOORBELL_LOG
#define VPEC_QUEUE5_DOORBELL_LOG__BE_ERROR__SHIFT                                                             0x0
#define VPEC_QUEUE5_DOORBELL_LOG__DATA__SHIFT                                                                 0x2
#define VPEC_QUEUE5_DOORBELL_LOG__BE_ERROR_MASK                                                               0x00000001L
#define VPEC_QUEUE5_DOORBELL_LOG__DATA_MASK                                                                   0xFFFFFFFCL
//VPEC_QUEUE5_IB_SUB_REMAIN
#define VPEC_QUEUE5_IB_SUB_REMAIN__SIZE__SHIFT                                                                0x0
#define VPEC_QUEUE5_IB_SUB_REMAIN__SIZE_MASK                                                                  0x00003FFFL
//VPEC_QUEUE5_PREEMPT
#define VPEC_QUEUE5_PREEMPT__IB_PREEMPT__SHIFT                                                                0x0
#define VPEC_QUEUE5_PREEMPT__IB_PREEMPT_MASK                                                                  0x00000001L
//VPEC_QUEUE5_LOG0BUFFER_CFG
#define VPEC_QUEUE5_LOG0BUFFER_CFG__ENABLE__SHIFT                                                             0x0
#define VPEC_QUEUE5_LOG0BUFFER_CFG__FIRST_FREE_ENTRY__SHIFT                                                   0x4
#define VPEC_QUEUE5_LOG0BUFFER_CFG__LAST_FREE_ENTRY__SHIFT                                                    0xc
#define VPEC_QUEUE5_LOG0BUFFER_CFG__RESERVED__SHIFT                                                           0x14
#define VPEC_QUEUE5_LOG0BUFFER_CFG__ENABLE_MASK                                                               0x00000001L
#define VPEC_QUEUE5_LOG0BUFFER_CFG__FIRST_FREE_ENTRY_MASK                                                     0x00000FF0L
#define VPEC_QUEUE5_LOG0BUFFER_CFG__LAST_FREE_ENTRY_MASK                                                      0x000FF000L
#define VPEC_QUEUE5_LOG0BUFFER_CFG__RESERVED_MASK                                                             0xFFF00000L
//VPEC_QUEUE5_LOG1BUFFER_CFG
#define VPEC_QUEUE5_LOG1BUFFER_CFG__ENABLE__SHIFT                                                             0x0
#define VPEC_QUEUE5_LOG1BUFFER_CFG__PARTIAL_ENTRY__SHIFT                                                      0x1
#define VPEC_QUEUE5_LOG1BUFFER_CFG__FIRST_FREE_ENTRY__SHIFT                                                   0x4
#define VPEC_QUEUE5_LOG1BUFFER_CFG__LAST_FREE_ENTRY__SHIFT                                                    0xc
#define VPEC_QUEUE5_LOG1BUFFER_CFG__RESERVED__SHIFT                                                           0x14
#define VPEC_QUEUE5_LOG1BUFFER_CFG__ENABLE_MASK                                                               0x00000001L
#define VPEC_QUEUE5_LOG1BUFFER_CFG__PARTIAL_ENTRY_MASK                                                        0x00000002L
#define VPEC_QUEUE5_LOG1BUFFER_CFG__FIRST_FREE_ENTRY_MASK                                                     0x00000FF0L
#define VPEC_QUEUE5_LOG1BUFFER_CFG__LAST_FREE_ENTRY_MASK                                                      0x000FF000L
#define VPEC_QUEUE5_LOG1BUFFER_CFG__RESERVED_MASK                                                             0xFFF00000L
//VPEC_QUEUE6_RB_CNTL
#define VPEC_QUEUE6_RB_CNTL__RB_ENABLE__SHIFT                                                                 0x0
#define VPEC_QUEUE6_RB_CNTL__RB_SIZE__SHIFT                                                                   0x1
#define VPEC_QUEUE6_RB_CNTL__WPTR_POLL_ENABLE__SHIFT                                                          0x8
#define VPEC_QUEUE6_RB_CNTL__RB_SWAP_ENABLE__SHIFT                                                            0x9
#define VPEC_QUEUE6_RB_CNTL__WPTR_POLL_SWAP_ENABLE__SHIFT                                                     0xa
#define VPEC_QUEUE6_RB_CNTL__F32_WPTR_POLL_ENABLE__SHIFT                                                      0xb
#define VPEC_QUEUE6_RB_CNTL__RPTR_WRITEBACK_ENABLE__SHIFT                                                     0xc
#define VPEC_QUEUE6_RB_CNTL__RPTR_WRITEBACK_SWAP_ENABLE__SHIFT                                                0xd
#define VPEC_QUEUE6_RB_CNTL__RPTR_WRITEBACK_TIMER__SHIFT                                                      0x10
#define VPEC_QUEUE6_RB_CNTL__RB_PRIV__SHIFT                                                                   0x17
#define VPEC_QUEUE6_RB_CNTL__RB_VMID__SHIFT                                                                   0x18
#define VPEC_QUEUE6_RB_CNTL__RB_ENABLE_MASK                                                                   0x00000001L
#define VPEC_QUEUE6_RB_CNTL__RB_SIZE_MASK                                                                     0x0000003EL
#define VPEC_QUEUE6_RB_CNTL__WPTR_POLL_ENABLE_MASK                                                            0x00000100L
#define VPEC_QUEUE6_RB_CNTL__RB_SWAP_ENABLE_MASK                                                              0x00000200L
#define VPEC_QUEUE6_RB_CNTL__WPTR_POLL_SWAP_ENABLE_MASK                                                       0x00000400L
#define VPEC_QUEUE6_RB_CNTL__F32_WPTR_POLL_ENABLE_MASK                                                        0x00000800L
#define VPEC_QUEUE6_RB_CNTL__RPTR_WRITEBACK_ENABLE_MASK                                                       0x00001000L
#define VPEC_QUEUE6_RB_CNTL__RPTR_WRITEBACK_SWAP_ENABLE_MASK                                                  0x00002000L
#define VPEC_QUEUE6_RB_CNTL__RPTR_WRITEBACK_TIMER_MASK                                                        0x001F0000L
#define VPEC_QUEUE6_RB_CNTL__RB_PRIV_MASK                                                                     0x00800000L
#define VPEC_QUEUE6_RB_CNTL__RB_VMID_MASK                                                                     0x0F000000L
//VPEC_QUEUE6_SCHEDULE_CNTL
#define VPEC_QUEUE6_SCHEDULE_CNTL__GLOBAL_ID__SHIFT                                                           0x0
#define VPEC_QUEUE6_SCHEDULE_CNTL__PROCESS_ID__SHIFT                                                          0x2
#define VPEC_QUEUE6_SCHEDULE_CNTL__LOCAL_ID__SHIFT                                                            0x6
#define VPEC_QUEUE6_SCHEDULE_CNTL__CONTEXT_QUANTUM__SHIFT                                                     0x8
#define VPEC_QUEUE6_SCHEDULE_CNTL__GLOBAL_ID_MASK                                                             0x00000003L
#define VPEC_QUEUE6_SCHEDULE_CNTL__PROCESS_ID_MASK                                                            0x0000001CL
#define VPEC_QUEUE6_SCHEDULE_CNTL__LOCAL_ID_MASK                                                              0x000000C0L
#define VPEC_QUEUE6_SCHEDULE_CNTL__CONTEXT_QUANTUM_MASK                                                       0x0000FF00L
//VPEC_QUEUE6_RB_BASE
#define VPEC_QUEUE6_RB_BASE__ADDR__SHIFT                                                                      0x0
#define VPEC_QUEUE6_RB_BASE__ADDR_MASK                                                                        0xFFFFFFFFL
//VPEC_QUEUE6_RB_BASE_HI
#define VPEC_QUEUE6_RB_BASE_HI__ADDR__SHIFT                                                                   0x0
#define VPEC_QUEUE6_RB_BASE_HI__ADDR_MASK                                                                     0x00FFFFFFL
//VPEC_QUEUE6_RB_RPTR
#define VPEC_QUEUE6_RB_RPTR__OFFSET__SHIFT                                                                    0x0
#define VPEC_QUEUE6_RB_RPTR__OFFSET_MASK                                                                      0xFFFFFFFFL
//VPEC_QUEUE6_RB_RPTR_HI
#define VPEC_QUEUE6_RB_RPTR_HI__OFFSET__SHIFT                                                                 0x0
#define VPEC_QUEUE6_RB_RPTR_HI__OFFSET_MASK                                                                   0xFFFFFFFFL
//VPEC_QUEUE6_RB_WPTR
#define VPEC_QUEUE6_RB_WPTR__OFFSET__SHIFT                                                                    0x0
#define VPEC_QUEUE6_RB_WPTR__OFFSET_MASK                                                                      0xFFFFFFFFL
//VPEC_QUEUE6_RB_WPTR_HI
#define VPEC_QUEUE6_RB_WPTR_HI__OFFSET__SHIFT                                                                 0x0
#define VPEC_QUEUE6_RB_WPTR_HI__OFFSET_MASK                                                                   0xFFFFFFFFL
//VPEC_QUEUE6_RB_RPTR_ADDR_HI
#define VPEC_QUEUE6_RB_RPTR_ADDR_HI__ADDR__SHIFT                                                              0x0
#define VPEC_QUEUE6_RB_RPTR_ADDR_HI__ADDR_MASK                                                                0xFFFFFFFFL
//VPEC_QUEUE6_RB_RPTR_ADDR_LO
#define VPEC_QUEUE6_RB_RPTR_ADDR_LO__ADDR__SHIFT                                                              0x2
#define VPEC_QUEUE6_RB_RPTR_ADDR_LO__ADDR_MASK                                                                0xFFFFFFFCL
//VPEC_QUEUE6_RB_AQL_CNTL
#define VPEC_QUEUE6_RB_AQL_CNTL__AQL_ENABLE__SHIFT                                                            0x0
#define VPEC_QUEUE6_RB_AQL_CNTL__AQL_PACKET_SIZE__SHIFT                                                       0x1
#define VPEC_QUEUE6_RB_AQL_CNTL__PACKET_STEP__SHIFT                                                           0x8
#define VPEC_QUEUE6_RB_AQL_CNTL__MIDCMD_PREEMPT_ENABLE__SHIFT                                                 0x10
#define VPEC_QUEUE6_RB_AQL_CNTL__MIDCMD_PREEMPT_DATA_RESTORE__SHIFT                                           0x11
#define VPEC_QUEUE6_RB_AQL_CNTL__OVERLAP_ENABLE__SHIFT                                                        0x12
#define VPEC_QUEUE6_RB_AQL_CNTL__AQL_ENABLE_MASK                                                              0x00000001L
#define VPEC_QUEUE6_RB_AQL_CNTL__AQL_PACKET_SIZE_MASK                                                         0x000000FEL
#define VPEC_QUEUE6_RB_AQL_CNTL__PACKET_STEP_MASK                                                             0x0000FF00L
#define VPEC_QUEUE6_RB_AQL_CNTL__MIDCMD_PREEMPT_ENABLE_MASK                                                   0x00010000L
#define VPEC_QUEUE6_RB_AQL_CNTL__MIDCMD_PREEMPT_DATA_RESTORE_MASK                                             0x00020000L
#define VPEC_QUEUE6_RB_AQL_CNTL__OVERLAP_ENABLE_MASK                                                          0x00040000L
//VPEC_QUEUE6_MINOR_PTR_UPDATE
#define VPEC_QUEUE6_MINOR_PTR_UPDATE__ENABLE__SHIFT                                                           0x0
#define VPEC_QUEUE6_MINOR_PTR_UPDATE__ENABLE_MASK                                                             0x00000001L
//VPEC_QUEUE6_CD_INFO
#define VPEC_QUEUE6_CD_INFO__CD_INFO__SHIFT                                                                   0x0
#define VPEC_QUEUE6_CD_INFO__CD_INFO_MASK                                                                     0xFFFFFFFFL
//VPEC_QUEUE6_RB_PREEMPT
#define VPEC_QUEUE6_RB_PREEMPT__PREEMPT_REQ__SHIFT                                                            0x0
#define VPEC_QUEUE6_RB_PREEMPT__PREEMPT_REQ_MASK                                                              0x00000001L
//VPEC_QUEUE6_SKIP_CNTL
#define VPEC_QUEUE6_SKIP_CNTL__SKIP_COUNT__SHIFT                                                              0x0
#define VPEC_QUEUE6_SKIP_CNTL__SKIP_COUNT_MASK                                                                0x000FFFFFL
//VPEC_QUEUE6_DOORBELL
#define VPEC_QUEUE6_DOORBELL__ENABLE__SHIFT                                                                   0x1c
#define VPEC_QUEUE6_DOORBELL__CAPTURED__SHIFT                                                                 0x1e
#define VPEC_QUEUE6_DOORBELL__ENABLE_MASK                                                                     0x10000000L
#define VPEC_QUEUE6_DOORBELL__CAPTURED_MASK                                                                   0x40000000L
//VPEC_QUEUE6_DOORBELL_OFFSET
#define VPEC_QUEUE6_DOORBELL_OFFSET__OFFSET__SHIFT                                                            0x2
#define VPEC_QUEUE6_DOORBELL_OFFSET__OFFSET_MASK                                                              0x0FFFFFFCL
//VPEC_QUEUE6_DUMMY0
#define VPEC_QUEUE6_DUMMY0__DUMMY__SHIFT                                                                      0x0
#define VPEC_QUEUE6_DUMMY0__DUMMY_MASK                                                                        0xFFFFFFFFL
//VPEC_QUEUE6_DUMMY1
#define VPEC_QUEUE6_DUMMY1__VALUE__SHIFT                                                                      0x0
#define VPEC_QUEUE6_DUMMY1__VALUE_MASK                                                                        0xFFFFFFFFL
//VPEC_QUEUE6_DUMMY2
#define VPEC_QUEUE6_DUMMY2__VALUE__SHIFT                                                                      0x0
#define VPEC_QUEUE6_DUMMY2__VALUE_MASK                                                                        0xFFFFFFFFL
//VPEC_QUEUE6_DUMMY3
#define VPEC_QUEUE6_DUMMY3__VALUE__SHIFT                                                                      0x0
#define VPEC_QUEUE6_DUMMY3__VALUE_MASK                                                                        0xFFFFFFFFL
//VPEC_QUEUE6_DUMMY4
#define VPEC_QUEUE6_DUMMY4__VALUE__SHIFT                                                                      0x0
#define VPEC_QUEUE6_DUMMY4__VALUE_MASK                                                                        0xFFFFFFFFL
//VPEC_QUEUE6_IB_CNTL
#define VPEC_QUEUE6_IB_CNTL__IB_ENABLE__SHIFT                                                                 0x0
#define VPEC_QUEUE6_IB_CNTL__SWITCH_INSIDE_IB__SHIFT                                                          0x8
#define VPEC_QUEUE6_IB_CNTL__CMD_VMID__SHIFT                                                                  0x10
#define VPEC_QUEUE6_IB_CNTL__IB_PRIV__SHIFT                                                                   0x1f
#define VPEC_QUEUE6_IB_CNTL__IB_ENABLE_MASK                                                                   0x00000001L
#define VPEC_QUEUE6_IB_CNTL__SWITCH_INSIDE_IB_MASK                                                            0x00000100L
#define VPEC_QUEUE6_IB_CNTL__CMD_VMID_MASK                                                                    0x000F0000L
#define VPEC_QUEUE6_IB_CNTL__IB_PRIV_MASK                                                                     0x80000000L
//VPEC_QUEUE6_IB_RPTR
#define VPEC_QUEUE6_IB_RPTR__OFFSET__SHIFT                                                                    0x2
#define VPEC_QUEUE6_IB_RPTR__OFFSET_MASK                                                                      0x003FFFFCL
//VPEC_QUEUE6_IB_OFFSET
#define VPEC_QUEUE6_IB_OFFSET__OFFSET__SHIFT                                                                  0x2
#define VPEC_QUEUE6_IB_OFFSET__OFFSET_MASK                                                                    0x003FFFFCL
//VPEC_QUEUE6_IB_BASE_LO
#define VPEC_QUEUE6_IB_BASE_LO__ADDR__SHIFT                                                                   0x5
#define VPEC_QUEUE6_IB_BASE_LO__ADDR_MASK                                                                     0xFFFFFFE0L
//VPEC_QUEUE6_IB_BASE_HI
#define VPEC_QUEUE6_IB_BASE_HI__ADDR__SHIFT                                                                   0x0
#define VPEC_QUEUE6_IB_BASE_HI__ADDR_MASK                                                                     0xFFFFFFFFL
//VPEC_QUEUE6_IB_SIZE
#define VPEC_QUEUE6_IB_SIZE__SIZE__SHIFT                                                                      0x0
#define VPEC_QUEUE6_IB_SIZE__SIZE_MASK                                                                        0x000FFFFFL
//VPEC_QUEUE6_CMDIB_CNTL
#define VPEC_QUEUE6_CMDIB_CNTL__IB_ENABLE__SHIFT                                                              0x0
#define VPEC_QUEUE6_CMDIB_CNTL__IB_SWAP_ENABLE__SHIFT                                                         0x4
#define VPEC_QUEUE6_CMDIB_CNTL__SWITCH_INSIDE_IB__SHIFT                                                       0x8
#define VPEC_QUEUE6_CMDIB_CNTL__CMD_VMID__SHIFT                                                               0x10
#define VPEC_QUEUE6_CMDIB_CNTL__IB_PRIV__SHIFT                                                                0x1f
#define VPEC_QUEUE6_CMDIB_CNTL__IB_ENABLE_MASK                                                                0x00000001L
#define VPEC_QUEUE6_CMDIB_CNTL__IB_SWAP_ENABLE_MASK                                                           0x00000010L
#define VPEC_QUEUE6_CMDIB_CNTL__SWITCH_INSIDE_IB_MASK                                                         0x00000100L
#define VPEC_QUEUE6_CMDIB_CNTL__CMD_VMID_MASK                                                                 0x000F0000L
#define VPEC_QUEUE6_CMDIB_CNTL__IB_PRIV_MASK                                                                  0x80000000L
//VPEC_QUEUE6_CMDIB_RPTR
#define VPEC_QUEUE6_CMDIB_RPTR__OFFSET__SHIFT                                                                 0x2
#define VPEC_QUEUE6_CMDIB_RPTR__OFFSET_MASK                                                                   0x003FFFFCL
//VPEC_QUEUE6_CMDIB_OFFSET
#define VPEC_QUEUE6_CMDIB_OFFSET__OFFSET__SHIFT                                                               0x2
#define VPEC_QUEUE6_CMDIB_OFFSET__OFFSET_MASK                                                                 0x003FFFFCL
//VPEC_QUEUE6_CMDIB_BASE_LO
#define VPEC_QUEUE6_CMDIB_BASE_LO__ADDR__SHIFT                                                                0x5
#define VPEC_QUEUE6_CMDIB_BASE_LO__ADDR_MASK                                                                  0xFFFFFFE0L
//VPEC_QUEUE6_CMDIB_BASE_HI
#define VPEC_QUEUE6_CMDIB_BASE_HI__ADDR__SHIFT                                                                0x0
#define VPEC_QUEUE6_CMDIB_BASE_HI__ADDR_MASK                                                                  0xFFFFFFFFL
//VPEC_QUEUE6_CMDIB_SIZE
#define VPEC_QUEUE6_CMDIB_SIZE__SIZE__SHIFT                                                                   0x0
#define VPEC_QUEUE6_CMDIB_SIZE__SIZE_MASK                                                                     0x000FFFFFL
//VPEC_QUEUE6_3DLUTIB_CNTL
#define VPEC_QUEUE6_3DLUTIB_CNTL__IB_ENABLE__SHIFT                                                            0x0
#define VPEC_QUEUE6_3DLUTIB_CNTL__SWITCH_INSIDE_IB__SHIFT                                                     0x8
#define VPEC_QUEUE6_3DLUTIB_CNTL__CMD_VMID__SHIFT                                                             0x10
#define VPEC_QUEUE6_3DLUTIB_CNTL__IB_PRIV__SHIFT                                                              0x1f
#define VPEC_QUEUE6_3DLUTIB_CNTL__IB_ENABLE_MASK                                                              0x00000001L
#define VPEC_QUEUE6_3DLUTIB_CNTL__SWITCH_INSIDE_IB_MASK                                                       0x00000100L
#define VPEC_QUEUE6_3DLUTIB_CNTL__CMD_VMID_MASK                                                               0x000F0000L
#define VPEC_QUEUE6_3DLUTIB_CNTL__IB_PRIV_MASK                                                                0x80000000L
//VPEC_QUEUE6_3DLUTIB_RPTR
#define VPEC_QUEUE6_3DLUTIB_RPTR__OFFSET__SHIFT                                                               0x2
#define VPEC_QUEUE6_3DLUTIB_RPTR__OFFSET_MASK                                                                 0x003FFFFCL
//VPEC_QUEUE6_3DLUTIB_OFFSET
#define VPEC_QUEUE6_3DLUTIB_OFFSET__OFFSET__SHIFT                                                             0x2
#define VPEC_QUEUE6_3DLUTIB_OFFSET__OFFSET_MASK                                                               0x003FFFFCL
//VPEC_QUEUE6_3DLUTIB_BASE_LO
#define VPEC_QUEUE6_3DLUTIB_BASE_LO__ADDR__SHIFT                                                              0x5
#define VPEC_QUEUE6_3DLUTIB_BASE_LO__ADDR_MASK                                                                0xFFFFFFE0L
//VPEC_QUEUE6_3DLUTIB_BASE_HI
#define VPEC_QUEUE6_3DLUTIB_BASE_HI__ADDR__SHIFT                                                              0x0
#define VPEC_QUEUE6_3DLUTIB_BASE_HI__ADDR_MASK                                                                0xFFFFFFFFL
//VPEC_QUEUE6_3DLUTIB_SIZE
#define VPEC_QUEUE6_3DLUTIB_SIZE__SIZE__SHIFT                                                                 0x0
#define VPEC_QUEUE6_3DLUTIB_SIZE__SIZE_MASK                                                                   0x000FFFFFL
//VPEC_QUEUE6_CSA_ADDR_LO
#define VPEC_QUEUE6_CSA_ADDR_LO__ADDR__SHIFT                                                                  0x0
#define VPEC_QUEUE6_CSA_ADDR_LO__ADDR_MASK                                                                    0xFFFFFFFFL
//VPEC_QUEUE6_CSA_ADDR_HI
#define VPEC_QUEUE6_CSA_ADDR_HI__ADDR__SHIFT                                                                  0x0
#define VPEC_QUEUE6_CSA_ADDR_HI__ADDR_MASK                                                                    0xFFFFFFFFL
//VPEC_QUEUE6_CONTEXT_STATUS
#define VPEC_QUEUE6_CONTEXT_STATUS__SELECTED__SHIFT                                                           0x0
#define VPEC_QUEUE6_CONTEXT_STATUS__USE_IB__SHIFT                                                             0x1
#define VPEC_QUEUE6_CONTEXT_STATUS__IDLE__SHIFT                                                               0x2
#define VPEC_QUEUE6_CONTEXT_STATUS__EXPIRED__SHIFT                                                            0x3
#define VPEC_QUEUE6_CONTEXT_STATUS__EXCEPTION__SHIFT                                                          0x4
#define VPEC_QUEUE6_CONTEXT_STATUS__CTXSW_ABLE__SHIFT                                                         0x7
#define VPEC_QUEUE6_CONTEXT_STATUS__USE_3DLUTIB__SHIFT                                                        0x8
#define VPEC_QUEUE6_CONTEXT_STATUS__PREEMPT_DISABLE__SHIFT                                                    0xa
#define VPEC_QUEUE6_CONTEXT_STATUS__RPTR_WB_IDLE__SHIFT                                                       0xb
#define VPEC_QUEUE6_CONTEXT_STATUS__WPTR_UPDATE_PENDING__SHIFT                                                0xc
#define VPEC_QUEUE6_CONTEXT_STATUS__WPTR_UPDATE_FAIL_COUNT__SHIFT                                             0x10
#define VPEC_QUEUE6_CONTEXT_STATUS__SELECTED_MASK                                                             0x00000001L
#define VPEC_QUEUE6_CONTEXT_STATUS__USE_IB_MASK                                                               0x00000002L
#define VPEC_QUEUE6_CONTEXT_STATUS__IDLE_MASK                                                                 0x00000004L
#define VPEC_QUEUE6_CONTEXT_STATUS__EXPIRED_MASK                                                              0x00000008L
#define VPEC_QUEUE6_CONTEXT_STATUS__EXCEPTION_MASK                                                            0x00000070L
#define VPEC_QUEUE6_CONTEXT_STATUS__CTXSW_ABLE_MASK                                                           0x00000080L
#define VPEC_QUEUE6_CONTEXT_STATUS__USE_3DLUTIB_MASK                                                          0x00000100L
#define VPEC_QUEUE6_CONTEXT_STATUS__PREEMPT_DISABLE_MASK                                                      0x00000400L
#define VPEC_QUEUE6_CONTEXT_STATUS__RPTR_WB_IDLE_MASK                                                         0x00000800L
#define VPEC_QUEUE6_CONTEXT_STATUS__WPTR_UPDATE_PENDING_MASK                                                  0x00001000L
#define VPEC_QUEUE6_CONTEXT_STATUS__WPTR_UPDATE_FAIL_COUNT_MASK                                               0x00FF0000L
//VPEC_QUEUE6_DOORBELL_LOG
#define VPEC_QUEUE6_DOORBELL_LOG__BE_ERROR__SHIFT                                                             0x0
#define VPEC_QUEUE6_DOORBELL_LOG__DATA__SHIFT                                                                 0x2
#define VPEC_QUEUE6_DOORBELL_LOG__BE_ERROR_MASK                                                               0x00000001L
#define VPEC_QUEUE6_DOORBELL_LOG__DATA_MASK                                                                   0xFFFFFFFCL
//VPEC_QUEUE6_IB_SUB_REMAIN
#define VPEC_QUEUE6_IB_SUB_REMAIN__SIZE__SHIFT                                                                0x0
#define VPEC_QUEUE6_IB_SUB_REMAIN__SIZE_MASK                                                                  0x00003FFFL
//VPEC_QUEUE6_PREEMPT
#define VPEC_QUEUE6_PREEMPT__IB_PREEMPT__SHIFT                                                                0x0
#define VPEC_QUEUE6_PREEMPT__IB_PREEMPT_MASK                                                                  0x00000001L
//VPEC_QUEUE6_LOG0BUFFER_CFG
#define VPEC_QUEUE6_LOG0BUFFER_CFG__ENABLE__SHIFT                                                             0x0
#define VPEC_QUEUE6_LOG0BUFFER_CFG__FIRST_FREE_ENTRY__SHIFT                                                   0x4
#define VPEC_QUEUE6_LOG0BUFFER_CFG__LAST_FREE_ENTRY__SHIFT                                                    0xc
#define VPEC_QUEUE6_LOG0BUFFER_CFG__RESERVED__SHIFT                                                           0x14
#define VPEC_QUEUE6_LOG0BUFFER_CFG__ENABLE_MASK                                                               0x00000001L
#define VPEC_QUEUE6_LOG0BUFFER_CFG__FIRST_FREE_ENTRY_MASK                                                     0x00000FF0L
#define VPEC_QUEUE6_LOG0BUFFER_CFG__LAST_FREE_ENTRY_MASK                                                      0x000FF000L
#define VPEC_QUEUE6_LOG0BUFFER_CFG__RESERVED_MASK                                                             0xFFF00000L
//VPEC_QUEUE6_LOG1BUFFER_CFG
#define VPEC_QUEUE6_LOG1BUFFER_CFG__ENABLE__SHIFT                                                             0x0
#define VPEC_QUEUE6_LOG1BUFFER_CFG__PARTIAL_ENTRY__SHIFT                                                      0x1
#define VPEC_QUEUE6_LOG1BUFFER_CFG__FIRST_FREE_ENTRY__SHIFT                                                   0x4
#define VPEC_QUEUE6_LOG1BUFFER_CFG__LAST_FREE_ENTRY__SHIFT                                                    0xc
#define VPEC_QUEUE6_LOG1BUFFER_CFG__RESERVED__SHIFT                                                           0x14
#define VPEC_QUEUE6_LOG1BUFFER_CFG__ENABLE_MASK                                                               0x00000001L
#define VPEC_QUEUE6_LOG1BUFFER_CFG__PARTIAL_ENTRY_MASK                                                        0x00000002L
#define VPEC_QUEUE6_LOG1BUFFER_CFG__FIRST_FREE_ENTRY_MASK                                                     0x00000FF0L
#define VPEC_QUEUE6_LOG1BUFFER_CFG__LAST_FREE_ENTRY_MASK                                                      0x000FF000L
#define VPEC_QUEUE6_LOG1BUFFER_CFG__RESERVED_MASK                                                             0xFFF00000L
//VPEC_QUEUE7_RB_CNTL
#define VPEC_QUEUE7_RB_CNTL__RB_ENABLE__SHIFT                                                                 0x0
#define VPEC_QUEUE7_RB_CNTL__RB_SIZE__SHIFT                                                                   0x1
#define VPEC_QUEUE7_RB_CNTL__WPTR_POLL_ENABLE__SHIFT                                                          0x8
#define VPEC_QUEUE7_RB_CNTL__RB_SWAP_ENABLE__SHIFT                                                            0x9
#define VPEC_QUEUE7_RB_CNTL__WPTR_POLL_SWAP_ENABLE__SHIFT                                                     0xa
#define VPEC_QUEUE7_RB_CNTL__F32_WPTR_POLL_ENABLE__SHIFT                                                      0xb
#define VPEC_QUEUE7_RB_CNTL__RPTR_WRITEBACK_ENABLE__SHIFT                                                     0xc
#define VPEC_QUEUE7_RB_CNTL__RPTR_WRITEBACK_SWAP_ENABLE__SHIFT                                                0xd
#define VPEC_QUEUE7_RB_CNTL__RPTR_WRITEBACK_TIMER__SHIFT                                                      0x10
#define VPEC_QUEUE7_RB_CNTL__RB_PRIV__SHIFT                                                                   0x17
#define VPEC_QUEUE7_RB_CNTL__RB_VMID__SHIFT                                                                   0x18
#define VPEC_QUEUE7_RB_CNTL__RB_ENABLE_MASK                                                                   0x00000001L
#define VPEC_QUEUE7_RB_CNTL__RB_SIZE_MASK                                                                     0x0000003EL
#define VPEC_QUEUE7_RB_CNTL__WPTR_POLL_ENABLE_MASK                                                            0x00000100L
#define VPEC_QUEUE7_RB_CNTL__RB_SWAP_ENABLE_MASK                                                              0x00000200L
#define VPEC_QUEUE7_RB_CNTL__WPTR_POLL_SWAP_ENABLE_MASK                                                       0x00000400L
#define VPEC_QUEUE7_RB_CNTL__F32_WPTR_POLL_ENABLE_MASK                                                        0x00000800L
#define VPEC_QUEUE7_RB_CNTL__RPTR_WRITEBACK_ENABLE_MASK                                                       0x00001000L
#define VPEC_QUEUE7_RB_CNTL__RPTR_WRITEBACK_SWAP_ENABLE_MASK                                                  0x00002000L
#define VPEC_QUEUE7_RB_CNTL__RPTR_WRITEBACK_TIMER_MASK                                                        0x001F0000L
#define VPEC_QUEUE7_RB_CNTL__RB_PRIV_MASK                                                                     0x00800000L
#define VPEC_QUEUE7_RB_CNTL__RB_VMID_MASK                                                                     0x0F000000L
//VPEC_QUEUE7_SCHEDULE_CNTL
#define VPEC_QUEUE7_SCHEDULE_CNTL__GLOBAL_ID__SHIFT                                                           0x0
#define VPEC_QUEUE7_SCHEDULE_CNTL__PROCESS_ID__SHIFT                                                          0x2
#define VPEC_QUEUE7_SCHEDULE_CNTL__LOCAL_ID__SHIFT                                                            0x6
#define VPEC_QUEUE7_SCHEDULE_CNTL__CONTEXT_QUANTUM__SHIFT                                                     0x8
#define VPEC_QUEUE7_SCHEDULE_CNTL__GLOBAL_ID_MASK                                                             0x00000003L
#define VPEC_QUEUE7_SCHEDULE_CNTL__PROCESS_ID_MASK                                                            0x0000001CL
#define VPEC_QUEUE7_SCHEDULE_CNTL__LOCAL_ID_MASK                                                              0x000000C0L
#define VPEC_QUEUE7_SCHEDULE_CNTL__CONTEXT_QUANTUM_MASK                                                       0x0000FF00L
//VPEC_QUEUE7_RB_BASE
#define VPEC_QUEUE7_RB_BASE__ADDR__SHIFT                                                                      0x0
#define VPEC_QUEUE7_RB_BASE__ADDR_MASK                                                                        0xFFFFFFFFL
//VPEC_QUEUE7_RB_BASE_HI
#define VPEC_QUEUE7_RB_BASE_HI__ADDR__SHIFT                                                                   0x0
#define VPEC_QUEUE7_RB_BASE_HI__ADDR_MASK                                                                     0x00FFFFFFL
//VPEC_QUEUE7_RB_RPTR
#define VPEC_QUEUE7_RB_RPTR__OFFSET__SHIFT                                                                    0x0
#define VPEC_QUEUE7_RB_RPTR__OFFSET_MASK                                                                      0xFFFFFFFFL
//VPEC_QUEUE7_RB_RPTR_HI
#define VPEC_QUEUE7_RB_RPTR_HI__OFFSET__SHIFT                                                                 0x0
#define VPEC_QUEUE7_RB_RPTR_HI__OFFSET_MASK                                                                   0xFFFFFFFFL
//VPEC_QUEUE7_RB_WPTR
#define VPEC_QUEUE7_RB_WPTR__OFFSET__SHIFT                                                                    0x0
#define VPEC_QUEUE7_RB_WPTR__OFFSET_MASK                                                                      0xFFFFFFFFL
//VPEC_QUEUE7_RB_WPTR_HI
#define VPEC_QUEUE7_RB_WPTR_HI__OFFSET__SHIFT                                                                 0x0
#define VPEC_QUEUE7_RB_WPTR_HI__OFFSET_MASK                                                                   0xFFFFFFFFL
//VPEC_QUEUE7_RB_RPTR_ADDR_HI
#define VPEC_QUEUE7_RB_RPTR_ADDR_HI__ADDR__SHIFT                                                              0x0
#define VPEC_QUEUE7_RB_RPTR_ADDR_HI__ADDR_MASK                                                                0xFFFFFFFFL
//VPEC_QUEUE7_RB_RPTR_ADDR_LO
#define VPEC_QUEUE7_RB_RPTR_ADDR_LO__ADDR__SHIFT                                                              0x2
#define VPEC_QUEUE7_RB_RPTR_ADDR_LO__ADDR_MASK                                                                0xFFFFFFFCL
//VPEC_QUEUE7_RB_AQL_CNTL
#define VPEC_QUEUE7_RB_AQL_CNTL__AQL_ENABLE__SHIFT                                                            0x0
#define VPEC_QUEUE7_RB_AQL_CNTL__AQL_PACKET_SIZE__SHIFT                                                       0x1
#define VPEC_QUEUE7_RB_AQL_CNTL__PACKET_STEP__SHIFT                                                           0x8
#define VPEC_QUEUE7_RB_AQL_CNTL__MIDCMD_PREEMPT_ENABLE__SHIFT                                                 0x10
#define VPEC_QUEUE7_RB_AQL_CNTL__MIDCMD_PREEMPT_DATA_RESTORE__SHIFT                                           0x11
#define VPEC_QUEUE7_RB_AQL_CNTL__OVERLAP_ENABLE__SHIFT                                                        0x12
#define VPEC_QUEUE7_RB_AQL_CNTL__AQL_ENABLE_MASK                                                              0x00000001L
#define VPEC_QUEUE7_RB_AQL_CNTL__AQL_PACKET_SIZE_MASK                                                         0x000000FEL
#define VPEC_QUEUE7_RB_AQL_CNTL__PACKET_STEP_MASK                                                             0x0000FF00L
#define VPEC_QUEUE7_RB_AQL_CNTL__MIDCMD_PREEMPT_ENABLE_MASK                                                   0x00010000L
#define VPEC_QUEUE7_RB_AQL_CNTL__MIDCMD_PREEMPT_DATA_RESTORE_MASK                                             0x00020000L
#define VPEC_QUEUE7_RB_AQL_CNTL__OVERLAP_ENABLE_MASK                                                          0x00040000L
//VPEC_QUEUE7_MINOR_PTR_UPDATE
#define VPEC_QUEUE7_MINOR_PTR_UPDATE__ENABLE__SHIFT                                                           0x0
#define VPEC_QUEUE7_MINOR_PTR_UPDATE__ENABLE_MASK                                                             0x00000001L
//VPEC_QUEUE7_CD_INFO
#define VPEC_QUEUE7_CD_INFO__CD_INFO__SHIFT                                                                   0x0
#define VPEC_QUEUE7_CD_INFO__CD_INFO_MASK                                                                     0xFFFFFFFFL
//VPEC_QUEUE7_RB_PREEMPT
#define VPEC_QUEUE7_RB_PREEMPT__PREEMPT_REQ__SHIFT                                                            0x0
#define VPEC_QUEUE7_RB_PREEMPT__PREEMPT_REQ_MASK                                                              0x00000001L
//VPEC_QUEUE7_SKIP_CNTL
#define VPEC_QUEUE7_SKIP_CNTL__SKIP_COUNT__SHIFT                                                              0x0
#define VPEC_QUEUE7_SKIP_CNTL__SKIP_COUNT_MASK                                                                0x000FFFFFL
//VPEC_QUEUE7_DOORBELL
#define VPEC_QUEUE7_DOORBELL__ENABLE__SHIFT                                                                   0x1c
#define VPEC_QUEUE7_DOORBELL__CAPTURED__SHIFT                                                                 0x1e
#define VPEC_QUEUE7_DOORBELL__ENABLE_MASK                                                                     0x10000000L
#define VPEC_QUEUE7_DOORBELL__CAPTURED_MASK                                                                   0x40000000L
//VPEC_QUEUE7_DOORBELL_OFFSET
#define VPEC_QUEUE7_DOORBELL_OFFSET__OFFSET__SHIFT                                                            0x2
#define VPEC_QUEUE7_DOORBELL_OFFSET__OFFSET_MASK                                                              0x0FFFFFFCL
//VPEC_QUEUE7_DUMMY0
#define VPEC_QUEUE7_DUMMY0__DUMMY__SHIFT                                                                      0x0
#define VPEC_QUEUE7_DUMMY0__DUMMY_MASK                                                                        0xFFFFFFFFL
//VPEC_QUEUE7_DUMMY1
#define VPEC_QUEUE7_DUMMY1__VALUE__SHIFT                                                                      0x0
#define VPEC_QUEUE7_DUMMY1__VALUE_MASK                                                                        0xFFFFFFFFL
//VPEC_QUEUE7_DUMMY2
#define VPEC_QUEUE7_DUMMY2__VALUE__SHIFT                                                                      0x0
#define VPEC_QUEUE7_DUMMY2__VALUE_MASK                                                                        0xFFFFFFFFL
//VPEC_QUEUE7_DUMMY3
#define VPEC_QUEUE7_DUMMY3__VALUE__SHIFT                                                                      0x0
#define VPEC_QUEUE7_DUMMY3__VALUE_MASK                                                                        0xFFFFFFFFL
//VPEC_QUEUE7_DUMMY4
#define VPEC_QUEUE7_DUMMY4__VALUE__SHIFT                                                                      0x0
#define VPEC_QUEUE7_DUMMY4__VALUE_MASK                                                                        0xFFFFFFFFL
//VPEC_QUEUE7_IB_CNTL
#define VPEC_QUEUE7_IB_CNTL__IB_ENABLE__SHIFT                                                                 0x0
#define VPEC_QUEUE7_IB_CNTL__SWITCH_INSIDE_IB__SHIFT                                                          0x8
#define VPEC_QUEUE7_IB_CNTL__CMD_VMID__SHIFT                                                                  0x10
#define VPEC_QUEUE7_IB_CNTL__IB_PRIV__SHIFT                                                                   0x1f
#define VPEC_QUEUE7_IB_CNTL__IB_ENABLE_MASK                                                                   0x00000001L
#define VPEC_QUEUE7_IB_CNTL__SWITCH_INSIDE_IB_MASK                                                            0x00000100L
#define VPEC_QUEUE7_IB_CNTL__CMD_VMID_MASK                                                                    0x000F0000L
#define VPEC_QUEUE7_IB_CNTL__IB_PRIV_MASK                                                                     0x80000000L
//VPEC_QUEUE7_IB_RPTR
#define VPEC_QUEUE7_IB_RPTR__OFFSET__SHIFT                                                                    0x2
#define VPEC_QUEUE7_IB_RPTR__OFFSET_MASK                                                                      0x003FFFFCL
//VPEC_QUEUE7_IB_OFFSET
#define VPEC_QUEUE7_IB_OFFSET__OFFSET__SHIFT                                                                  0x2
#define VPEC_QUEUE7_IB_OFFSET__OFFSET_MASK                                                                    0x003FFFFCL
//VPEC_QUEUE7_IB_BASE_LO
#define VPEC_QUEUE7_IB_BASE_LO__ADDR__SHIFT                                                                   0x5
#define VPEC_QUEUE7_IB_BASE_LO__ADDR_MASK                                                                     0xFFFFFFE0L
//VPEC_QUEUE7_IB_BASE_HI
#define VPEC_QUEUE7_IB_BASE_HI__ADDR__SHIFT                                                                   0x0
#define VPEC_QUEUE7_IB_BASE_HI__ADDR_MASK                                                                     0xFFFFFFFFL
//VPEC_QUEUE7_IB_SIZE
#define VPEC_QUEUE7_IB_SIZE__SIZE__SHIFT                                                                      0x0
#define VPEC_QUEUE7_IB_SIZE__SIZE_MASK                                                                        0x000FFFFFL
//VPEC_QUEUE7_CMDIB_CNTL
#define VPEC_QUEUE7_CMDIB_CNTL__IB_ENABLE__SHIFT                                                              0x0
#define VPEC_QUEUE7_CMDIB_CNTL__IB_SWAP_ENABLE__SHIFT                                                         0x4
#define VPEC_QUEUE7_CMDIB_CNTL__SWITCH_INSIDE_IB__SHIFT                                                       0x8
#define VPEC_QUEUE7_CMDIB_CNTL__CMD_VMID__SHIFT                                                               0x10
#define VPEC_QUEUE7_CMDIB_CNTL__IB_PRIV__SHIFT                                                                0x1f
#define VPEC_QUEUE7_CMDIB_CNTL__IB_ENABLE_MASK                                                                0x00000001L
#define VPEC_QUEUE7_CMDIB_CNTL__IB_SWAP_ENABLE_MASK                                                           0x00000010L
#define VPEC_QUEUE7_CMDIB_CNTL__SWITCH_INSIDE_IB_MASK                                                         0x00000100L
#define VPEC_QUEUE7_CMDIB_CNTL__CMD_VMID_MASK                                                                 0x000F0000L
#define VPEC_QUEUE7_CMDIB_CNTL__IB_PRIV_MASK                                                                  0x80000000L
//VPEC_QUEUE7_CMDIB_RPTR
#define VPEC_QUEUE7_CMDIB_RPTR__OFFSET__SHIFT                                                                 0x2
#define VPEC_QUEUE7_CMDIB_RPTR__OFFSET_MASK                                                                   0x003FFFFCL
//VPEC_QUEUE7_CMDIB_OFFSET
#define VPEC_QUEUE7_CMDIB_OFFSET__OFFSET__SHIFT                                                               0x2
#define VPEC_QUEUE7_CMDIB_OFFSET__OFFSET_MASK                                                                 0x003FFFFCL
//VPEC_QUEUE7_CMDIB_BASE_LO
#define VPEC_QUEUE7_CMDIB_BASE_LO__ADDR__SHIFT                                                                0x5
#define VPEC_QUEUE7_CMDIB_BASE_LO__ADDR_MASK                                                                  0xFFFFFFE0L
//VPEC_QUEUE7_CMDIB_BASE_HI
#define VPEC_QUEUE7_CMDIB_BASE_HI__ADDR__SHIFT                                                                0x0
#define VPEC_QUEUE7_CMDIB_BASE_HI__ADDR_MASK                                                                  0xFFFFFFFFL
//VPEC_QUEUE7_CMDIB_SIZE
#define VPEC_QUEUE7_CMDIB_SIZE__SIZE__SHIFT                                                                   0x0
#define VPEC_QUEUE7_CMDIB_SIZE__SIZE_MASK                                                                     0x000FFFFFL
//VPEC_QUEUE7_3DLUTIB_CNTL
#define VPEC_QUEUE7_3DLUTIB_CNTL__IB_ENABLE__SHIFT                                                            0x0
#define VPEC_QUEUE7_3DLUTIB_CNTL__SWITCH_INSIDE_IB__SHIFT                                                     0x8
#define VPEC_QUEUE7_3DLUTIB_CNTL__CMD_VMID__SHIFT                                                             0x10
#define VPEC_QUEUE7_3DLUTIB_CNTL__IB_PRIV__SHIFT                                                              0x1f
#define VPEC_QUEUE7_3DLUTIB_CNTL__IB_ENABLE_MASK                                                              0x00000001L
#define VPEC_QUEUE7_3DLUTIB_CNTL__SWITCH_INSIDE_IB_MASK                                                       0x00000100L
#define VPEC_QUEUE7_3DLUTIB_CNTL__CMD_VMID_MASK                                                               0x000F0000L
#define VPEC_QUEUE7_3DLUTIB_CNTL__IB_PRIV_MASK                                                                0x80000000L
//VPEC_QUEUE7_3DLUTIB_RPTR
#define VPEC_QUEUE7_3DLUTIB_RPTR__OFFSET__SHIFT                                                               0x2
#define VPEC_QUEUE7_3DLUTIB_RPTR__OFFSET_MASK                                                                 0x003FFFFCL
//VPEC_QUEUE7_3DLUTIB_OFFSET
#define VPEC_QUEUE7_3DLUTIB_OFFSET__OFFSET__SHIFT                                                             0x2
#define VPEC_QUEUE7_3DLUTIB_OFFSET__OFFSET_MASK                                                               0x003FFFFCL
//VPEC_QUEUE7_3DLUTIB_BASE_LO
#define VPEC_QUEUE7_3DLUTIB_BASE_LO__ADDR__SHIFT                                                              0x5
#define VPEC_QUEUE7_3DLUTIB_BASE_LO__ADDR_MASK                                                                0xFFFFFFE0L
//VPEC_QUEUE7_3DLUTIB_BASE_HI
#define VPEC_QUEUE7_3DLUTIB_BASE_HI__ADDR__SHIFT                                                              0x0
#define VPEC_QUEUE7_3DLUTIB_BASE_HI__ADDR_MASK                                                                0xFFFFFFFFL
//VPEC_QUEUE7_3DLUTIB_SIZE
#define VPEC_QUEUE7_3DLUTIB_SIZE__SIZE__SHIFT                                                                 0x0
#define VPEC_QUEUE7_3DLUTIB_SIZE__SIZE_MASK                                                                   0x000FFFFFL
//VPEC_QUEUE7_CSA_ADDR_LO
#define VPEC_QUEUE7_CSA_ADDR_LO__ADDR__SHIFT                                                                  0x0
#define VPEC_QUEUE7_CSA_ADDR_LO__ADDR_MASK                                                                    0xFFFFFFFFL
//VPEC_QUEUE7_CSA_ADDR_HI
#define VPEC_QUEUE7_CSA_ADDR_HI__ADDR__SHIFT                                                                  0x0
#define VPEC_QUEUE7_CSA_ADDR_HI__ADDR_MASK                                                                    0xFFFFFFFFL
//VPEC_QUEUE7_CONTEXT_STATUS
#define VPEC_QUEUE7_CONTEXT_STATUS__SELECTED__SHIFT                                                           0x0
#define VPEC_QUEUE7_CONTEXT_STATUS__USE_IB__SHIFT                                                             0x1
#define VPEC_QUEUE7_CONTEXT_STATUS__IDLE__SHIFT                                                               0x2
#define VPEC_QUEUE7_CONTEXT_STATUS__EXPIRED__SHIFT                                                            0x3
#define VPEC_QUEUE7_CONTEXT_STATUS__EXCEPTION__SHIFT                                                          0x4
#define VPEC_QUEUE7_CONTEXT_STATUS__CTXSW_ABLE__SHIFT                                                         0x7
#define VPEC_QUEUE7_CONTEXT_STATUS__USE_3DLUTIB__SHIFT                                                        0x8
#define VPEC_QUEUE7_CONTEXT_STATUS__PREEMPT_DISABLE__SHIFT                                                    0xa
#define VPEC_QUEUE7_CONTEXT_STATUS__RPTR_WB_IDLE__SHIFT                                                       0xb
#define VPEC_QUEUE7_CONTEXT_STATUS__WPTR_UPDATE_PENDING__SHIFT                                                0xc
#define VPEC_QUEUE7_CONTEXT_STATUS__WPTR_UPDATE_FAIL_COUNT__SHIFT                                             0x10
#define VPEC_QUEUE7_CONTEXT_STATUS__SELECTED_MASK                                                             0x00000001L
#define VPEC_QUEUE7_CONTEXT_STATUS__USE_IB_MASK                                                               0x00000002L
#define VPEC_QUEUE7_CONTEXT_STATUS__IDLE_MASK                                                                 0x00000004L
#define VPEC_QUEUE7_CONTEXT_STATUS__EXPIRED_MASK                                                              0x00000008L
#define VPEC_QUEUE7_CONTEXT_STATUS__EXCEPTION_MASK                                                            0x00000070L
#define VPEC_QUEUE7_CONTEXT_STATUS__CTXSW_ABLE_MASK                                                           0x00000080L
#define VPEC_QUEUE7_CONTEXT_STATUS__USE_3DLUTIB_MASK                                                          0x00000100L
#define VPEC_QUEUE7_CONTEXT_STATUS__PREEMPT_DISABLE_MASK                                                      0x00000400L
#define VPEC_QUEUE7_CONTEXT_STATUS__RPTR_WB_IDLE_MASK                                                         0x00000800L
#define VPEC_QUEUE7_CONTEXT_STATUS__WPTR_UPDATE_PENDING_MASK                                                  0x00001000L
#define VPEC_QUEUE7_CONTEXT_STATUS__WPTR_UPDATE_FAIL_COUNT_MASK                                               0x00FF0000L
//VPEC_QUEUE7_DOORBELL_LOG
#define VPEC_QUEUE7_DOORBELL_LOG__BE_ERROR__SHIFT                                                             0x0
#define VPEC_QUEUE7_DOORBELL_LOG__DATA__SHIFT                                                                 0x2
#define VPEC_QUEUE7_DOORBELL_LOG__BE_ERROR_MASK                                                               0x00000001L
#define VPEC_QUEUE7_DOORBELL_LOG__DATA_MASK                                                                   0xFFFFFFFCL
//VPEC_QUEUE7_IB_SUB_REMAIN
#define VPEC_QUEUE7_IB_SUB_REMAIN__SIZE__SHIFT                                                                0x0
#define VPEC_QUEUE7_IB_SUB_REMAIN__SIZE_MASK                                                                  0x00003FFFL
//VPEC_QUEUE7_PREEMPT
#define VPEC_QUEUE7_PREEMPT__IB_PREEMPT__SHIFT                                                                0x0
#define VPEC_QUEUE7_PREEMPT__IB_PREEMPT_MASK                                                                  0x00000001L
//VPEC_QUEUE7_LOG0BUFFER_CFG
#define VPEC_QUEUE7_LOG0BUFFER_CFG__ENABLE__SHIFT                                                             0x0
#define VPEC_QUEUE7_LOG0BUFFER_CFG__FIRST_FREE_ENTRY__SHIFT                                                   0x4
#define VPEC_QUEUE7_LOG0BUFFER_CFG__LAST_FREE_ENTRY__SHIFT                                                    0xc
#define VPEC_QUEUE7_LOG0BUFFER_CFG__RESERVED__SHIFT                                                           0x14
#define VPEC_QUEUE7_LOG0BUFFER_CFG__ENABLE_MASK                                                               0x00000001L
#define VPEC_QUEUE7_LOG0BUFFER_CFG__FIRST_FREE_ENTRY_MASK                                                     0x00000FF0L
#define VPEC_QUEUE7_LOG0BUFFER_CFG__LAST_FREE_ENTRY_MASK                                                      0x000FF000L
#define VPEC_QUEUE7_LOG0BUFFER_CFG__RESERVED_MASK                                                             0xFFF00000L
//VPEC_QUEUE7_LOG1BUFFER_CFG
#define VPEC_QUEUE7_LOG1BUFFER_CFG__ENABLE__SHIFT                                                             0x0
#define VPEC_QUEUE7_LOG1BUFFER_CFG__PARTIAL_ENTRY__SHIFT                                                      0x1
#define VPEC_QUEUE7_LOG1BUFFER_CFG__FIRST_FREE_ENTRY__SHIFT                                                   0x4
#define VPEC_QUEUE7_LOG1BUFFER_CFG__LAST_FREE_ENTRY__SHIFT                                                    0xc
#define VPEC_QUEUE7_LOG1BUFFER_CFG__RESERVED__SHIFT                                                           0x14
#define VPEC_QUEUE7_LOG1BUFFER_CFG__ENABLE_MASK                                                               0x00000001L
#define VPEC_QUEUE7_LOG1BUFFER_CFG__PARTIAL_ENTRY_MASK                                                        0x00000002L
#define VPEC_QUEUE7_LOG1BUFFER_CFG__FIRST_FREE_ENTRY_MASK                                                     0x00000FF0L
#define VPEC_QUEUE7_LOG1BUFFER_CFG__LAST_FREE_ENTRY_MASK                                                      0x000FF000L
#define VPEC_QUEUE7_LOG1BUFFER_CFG__RESERVED_MASK                                                             0xFFF00000L


#endif
