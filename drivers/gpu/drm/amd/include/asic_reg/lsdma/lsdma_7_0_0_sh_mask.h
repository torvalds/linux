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
#ifndef _lsdma_7_0_0_SH_MASK_HEADER
#define _lsdma_7_0_0_SH_MASK_HEADER


// addressBlock: lsdma0_lsdma0dec
//LSDMA_UCODE_ADDR
#define LSDMA_UCODE_ADDR__VALUE__SHIFT                                                                        0x0
#define LSDMA_UCODE_ADDR__VALUE_MASK                                                                          0x00001FFFL
//LSDMA_UCODE_DATA
#define LSDMA_UCODE_DATA__VALUE__SHIFT                                                                        0x0
#define LSDMA_UCODE_DATA__VALUE_MASK                                                                          0xFFFFFFFFL
//LSDMA_ERROR_INJECT_CNTL
#define LSDMA_ERROR_INJECT_CNTL__ENABLE_IRRITATION__SHIFT                                                     0x0
#define LSDMA_ERROR_INJECT_CNTL__ENABLE_SINGLE_WRITE__SHIFT                                                   0x1
#define LSDMA_ERROR_INJECT_CNTL__ENABLE_ERROR_INJECT__SHIFT                                                   0x2
#define LSDMA_ERROR_INJECT_CNTL__ENABLE_MEMHUB_READ_POISON_INJECT__SHIFT                                      0x8
#define LSDMA_ERROR_INJECT_CNTL__ENABLE_MEMHUB_ATOMIC_POISON_INJECT__SHIFT                                    0x9
#define LSDMA_ERROR_INJECT_CNTL__ENABLE_IRRITATION_MASK                                                       0x00000001L
#define LSDMA_ERROR_INJECT_CNTL__ENABLE_SINGLE_WRITE_MASK                                                     0x00000002L
#define LSDMA_ERROR_INJECT_CNTL__ENABLE_ERROR_INJECT_MASK                                                     0x0000000CL
//LSDMA_ERROR_INJECT_SELECT
#define LSDMA_ERROR_INJECT_SELECT__MBANK_DATA_BUF0__SHIFT                                                     0x0
#define LSDMA_ERROR_INJECT_SELECT__MBANK_DATA_BUF1__SHIFT                                                     0x1
#define LSDMA_ERROR_INJECT_SELECT__MBANK_DATA_BUF2__SHIFT                                                     0x2
#define LSDMA_ERROR_INJECT_SELECT__MBANK_DATA_BUF3__SHIFT                                                     0x3
#define LSDMA_ERROR_INJECT_SELECT__MBANK_DATA_BUF4__SHIFT                                                     0x4
#define LSDMA_ERROR_INJECT_SELECT__MBANK_DATA_BUF5__SHIFT                                                     0x5
#define LSDMA_ERROR_INJECT_SELECT__MBANK_DATA_BUF6__SHIFT                                                     0x6
#define LSDMA_ERROR_INJECT_SELECT__MBANK_DATA_BUF7__SHIFT                                                     0x7
#define LSDMA_ERROR_INJECT_SELECT__MBANK_DATA_BUF8__SHIFT                                                     0x8
#define LSDMA_ERROR_INJECT_SELECT__MBANK_DATA_BUF9__SHIFT                                                     0x9
#define LSDMA_ERROR_INJECT_SELECT__MBANK_DATA_BUF10__SHIFT                                                    0xa
#define LSDMA_ERROR_INJECT_SELECT__MBANK_DATA_BUF11__SHIFT                                                    0xb
#define LSDMA_ERROR_INJECT_SELECT__MBANK_DATA_BUF12__SHIFT                                                    0xc
#define LSDMA_ERROR_INJECT_SELECT__MBANK_DATA_BUF13__SHIFT                                                    0xd
#define LSDMA_ERROR_INJECT_SELECT__MBANK_DATA_BUF14__SHIFT                                                    0xe
#define LSDMA_ERROR_INJECT_SELECT__MBANK_DATA_BUF15__SHIFT                                                    0xf
#define LSDMA_ERROR_INJECT_SELECT__UCODE_BUF__SHIFT                                                           0x10
#define LSDMA_ERROR_INJECT_SELECT__RB_CMD_BUF__SHIFT                                                          0x11
#define LSDMA_ERROR_INJECT_SELECT__IB_CMD_BUF__SHIFT                                                          0x12
#define LSDMA_ERROR_INJECT_SELECT__UTCL1_RD_FIFO__SHIFT                                                       0x13
#define LSDMA_ERROR_INJECT_SELECT__UTCL1_RDBST_FIFO__SHIFT                                                    0x14
#define LSDMA_ERROR_INJECT_SELECT__UTCL1_WR_FIFO__SHIFT                                                       0x15
#define LSDMA_ERROR_INJECT_SELECT__DATA_LUT_FIFO__SHIFT                                                       0x16
#define LSDMA_ERROR_INJECT_SELECT__SPLIT_DATA_FIFO__SHIFT                                                     0x17
#define LSDMA_ERROR_INJECT_SELECT__MC_WR_ADDR_FIFO__SHIFT                                                     0x18
#define LSDMA_ERROR_INJECT_SELECT__MC_RDRET_BUF__SHIFT                                                        0x19
#define LSDMA_ERROR_INJECT_SELECT__MBANK_DATA_BUF0_MASK                                                       0x00000001L
#define LSDMA_ERROR_INJECT_SELECT__MBANK_DATA_BUF1_MASK                                                       0x00000002L
#define LSDMA_ERROR_INJECT_SELECT__MBANK_DATA_BUF2_MASK                                                       0x00000004L
#define LSDMA_ERROR_INJECT_SELECT__MBANK_DATA_BUF3_MASK                                                       0x00000008L
#define LSDMA_ERROR_INJECT_SELECT__MBANK_DATA_BUF4_MASK                                                       0x00000010L
#define LSDMA_ERROR_INJECT_SELECT__MBANK_DATA_BUF5_MASK                                                       0x00000020L
#define LSDMA_ERROR_INJECT_SELECT__MBANK_DATA_BUF6_MASK                                                       0x00000040L
#define LSDMA_ERROR_INJECT_SELECT__MBANK_DATA_BUF7_MASK                                                       0x00000080L
#define LSDMA_ERROR_INJECT_SELECT__MBANK_DATA_BUF8_MASK                                                       0x00000100L
#define LSDMA_ERROR_INJECT_SELECT__MBANK_DATA_BUF9_MASK                                                       0x00000200L
#define LSDMA_ERROR_INJECT_SELECT__MBANK_DATA_BUF10_MASK                                                      0x00000400L
#define LSDMA_ERROR_INJECT_SELECT__MBANK_DATA_BUF11_MASK                                                      0x00000800L
#define LSDMA_ERROR_INJECT_SELECT__MBANK_DATA_BUF12_MASK                                                      0x00001000L
#define LSDMA_ERROR_INJECT_SELECT__MBANK_DATA_BUF13_MASK                                                      0x00002000L
#define LSDMA_ERROR_INJECT_SELECT__MBANK_DATA_BUF14_MASK                                                      0x00004000L
#define LSDMA_ERROR_INJECT_SELECT__MBANK_DATA_BUF15_MASK                                                      0x00008000L
#define LSDMA_ERROR_INJECT_SELECT__UCODE_BUF_MASK                                                             0x00010000L
#define LSDMA_ERROR_INJECT_SELECT__RB_CMD_BUF_MASK                                                            0x00020000L
#define LSDMA_ERROR_INJECT_SELECT__IB_CMD_BUF_MASK                                                            0x00040000L
#define LSDMA_ERROR_INJECT_SELECT__UTCL1_RD_FIFO_MASK                                                         0x00080000L
#define LSDMA_ERROR_INJECT_SELECT__UTCL1_RDBST_FIFO_MASK                                                      0x00100000L
#define LSDMA_ERROR_INJECT_SELECT__UTCL1_WR_FIFO_MASK                                                         0x00200000L
#define LSDMA_ERROR_INJECT_SELECT__DATA_LUT_FIFO_MASK                                                         0x00400000L
#define LSDMA_ERROR_INJECT_SELECT__SPLIT_DATA_FIFO_MASK                                                       0x00800000L
#define LSDMA_ERROR_INJECT_SELECT__MC_WR_ADDR_FIFO_MASK                                                       0x01000000L
#define LSDMA_ERROR_INJECT_SELECT__MC_RDRET_BUF_MASK                                                          0x02000000L
#define LSDMA_PUB_REG_TYPE0__LSDMA_UCODE_ADDR__SHIFT                                                          0x0
#define LSDMA_PUB_REG_TYPE0__LSDMA_UCODE_DATA__SHIFT                                                          0x1
#define LSDMA_PUB_REG_TYPE0__LSDMA_UCODE_ADDR_MASK                                                            0x00000001L
#define LSDMA_PUB_REG_TYPE0__LSDMA_UCODE_DATA_MASK                                                            0x00000002L
#define LSDMA_PUB_REG_TYPE3__LSDMA_CLK_CTRL__SHIFT                                                            0xb
#define LSDMA_PUB_REG_TYPE3__LSDMA_CLK_CTRL_MASK                                                              0x00000800L
//LSDMA_CONTEXT_GROUP_BOUNDARY
#define LSDMA_CONTEXT_GROUP_BOUNDARY__RESERVED__SHIFT                                                         0x0
#define LSDMA_CONTEXT_GROUP_BOUNDARY__RESERVED_MASK                                                           0xFFFFFFFFL
//LSDMA_RB_RPTR_FETCH_HI
#define LSDMA_RB_RPTR_FETCH_HI__OFFSET__SHIFT                                                                 0x0
#define LSDMA_RB_RPTR_FETCH_HI__OFFSET_MASK                                                                   0xFFFFFFFFL
//LSDMA_SEM_WAIT_FAIL_TIMER_CNTL
#define LSDMA_SEM_WAIT_FAIL_TIMER_CNTL__TIMER__SHIFT                                                          0x0
#define LSDMA_SEM_WAIT_FAIL_TIMER_CNTL__TIMER_MASK                                                            0xFFFFFFFFL
//LSDMA_RB_RPTR_FETCH
#define LSDMA_RB_RPTR_FETCH__OFFSET__SHIFT                                                                    0x2
#define LSDMA_RB_RPTR_FETCH__OFFSET_MASK                                                                      0xFFFFFFFCL
//LSDMA_IB_OFFSET_FETCH
#define LSDMA_IB_OFFSET_FETCH__OFFSET__SHIFT                                                                  0x2
#define LSDMA_IB_OFFSET_FETCH__OFFSET_MASK                                                                    0x003FFFFCL
//LSDMA_PROGRAM
#define LSDMA_PROGRAM__STREAM__SHIFT                                                                          0x0
#define LSDMA_PROGRAM__STREAM_MASK                                                                            0xFFFFFFFFL
//LSDMA_STATUS_REG
#define LSDMA_STATUS_REG__IDLE__SHIFT                                                                         0x0
#define LSDMA_STATUS_REG__REG_IDLE__SHIFT                                                                     0x1
#define LSDMA_STATUS_REG__RB_EMPTY__SHIFT                                                                     0x2
#define LSDMA_STATUS_REG__RB_FULL__SHIFT                                                                      0x3
#define LSDMA_STATUS_REG__RB_CMD_IDLE__SHIFT                                                                  0x4
#define LSDMA_STATUS_REG__RB_CMD_FULL__SHIFT                                                                  0x5
#define LSDMA_STATUS_REG__IB_CMD_IDLE__SHIFT                                                                  0x6
#define LSDMA_STATUS_REG__IB_CMD_FULL__SHIFT                                                                  0x7
#define LSDMA_STATUS_REG__BLOCK_IDLE__SHIFT                                                                   0x8
#define LSDMA_STATUS_REG__INSIDE_IB__SHIFT                                                                    0x9
#define LSDMA_STATUS_REG__EX_IDLE__SHIFT                                                                      0xa
#define LSDMA_STATUS_REG__EX_IDLE_POLL_TIMER_EXPIRE__SHIFT                                                    0xb
#define LSDMA_STATUS_REG__PACKET_READY__SHIFT                                                                 0xc
#define LSDMA_STATUS_REG__MC_WR_IDLE__SHIFT                                                                   0xd
#define LSDMA_STATUS_REG__SRBM_IDLE__SHIFT                                                                    0xe
#define LSDMA_STATUS_REG__CONTEXT_EMPTY__SHIFT                                                                0xf
#define LSDMA_STATUS_REG__DELTA_RPTR_FULL__SHIFT                                                              0x10
#define LSDMA_STATUS_REG__RB_MC_RREQ_IDLE__SHIFT                                                              0x11
#define LSDMA_STATUS_REG__IB_MC_RREQ_IDLE__SHIFT                                                              0x12
#define LSDMA_STATUS_REG__MC_RD_IDLE__SHIFT                                                                   0x13
#define LSDMA_STATUS_REG__DELTA_RPTR_EMPTY__SHIFT                                                             0x14
#define LSDMA_STATUS_REG__MC_RD_RET_STALL__SHIFT                                                              0x15
#define LSDMA_STATUS_REG__MC_RD_NO_POLL_IDLE__SHIFT                                                           0x16
#define LSDMA_STATUS_REG__DRM_IDLE__SHIFT                                                                     0x17
#define LSDMA_STATUS_REG__Reserved__SHIFT                                                                     0x18
#define LSDMA_STATUS_REG__PREV_CMD_IDLE__SHIFT                                                                0x19
#define LSDMA_STATUS_REG__SEM_IDLE__SHIFT                                                                     0x1a
#define LSDMA_STATUS_REG__SEM_REQ_STALL__SHIFT                                                                0x1b
#define LSDMA_STATUS_REG__SEM_RESP_STATE__SHIFT                                                               0x1c
#define LSDMA_STATUS_REG__INT_IDLE__SHIFT                                                                     0x1e
#define LSDMA_STATUS_REG__INT_REQ_STALL__SHIFT                                                                0x1f
#define LSDMA_STATUS_REG__IDLE_MASK                                                                           0x00000001L
#define LSDMA_STATUS_REG__REG_IDLE_MASK                                                                       0x00000002L
#define LSDMA_STATUS_REG__RB_EMPTY_MASK                                                                       0x00000004L
#define LSDMA_STATUS_REG__RB_FULL_MASK                                                                        0x00000008L
#define LSDMA_STATUS_REG__RB_CMD_IDLE_MASK                                                                    0x00000010L
#define LSDMA_STATUS_REG__RB_CMD_FULL_MASK                                                                    0x00000020L
#define LSDMA_STATUS_REG__IB_CMD_IDLE_MASK                                                                    0x00000040L
#define LSDMA_STATUS_REG__IB_CMD_FULL_MASK                                                                    0x00000080L
#define LSDMA_STATUS_REG__BLOCK_IDLE_MASK                                                                     0x00000100L
#define LSDMA_STATUS_REG__INSIDE_IB_MASK                                                                      0x00000200L
#define LSDMA_STATUS_REG__EX_IDLE_MASK                                                                        0x00000400L
#define LSDMA_STATUS_REG__EX_IDLE_POLL_TIMER_EXPIRE_MASK                                                      0x00000800L
#define LSDMA_STATUS_REG__PACKET_READY_MASK                                                                   0x00001000L
#define LSDMA_STATUS_REG__MC_WR_IDLE_MASK                                                                     0x00002000L
#define LSDMA_STATUS_REG__SRBM_IDLE_MASK                                                                      0x00004000L
#define LSDMA_STATUS_REG__CONTEXT_EMPTY_MASK                                                                  0x00008000L
#define LSDMA_STATUS_REG__DELTA_RPTR_FULL_MASK                                                                0x00010000L
#define LSDMA_STATUS_REG__RB_MC_RREQ_IDLE_MASK                                                                0x00020000L
#define LSDMA_STATUS_REG__IB_MC_RREQ_IDLE_MASK                                                                0x00040000L
#define LSDMA_STATUS_REG__MC_RD_IDLE_MASK                                                                     0x00080000L
#define LSDMA_STATUS_REG__DELTA_RPTR_EMPTY_MASK                                                               0x00100000L
#define LSDMA_STATUS_REG__MC_RD_RET_STALL_MASK                                                                0x00200000L
#define LSDMA_STATUS_REG__MC_RD_NO_POLL_IDLE_MASK                                                             0x00400000L
#define LSDMA_STATUS_REG__Reserved_MASK                                                                       0x01000000L
#define LSDMA_STATUS_REG__PREV_CMD_IDLE_MASK                                                                  0x02000000L
#define LSDMA_STATUS_REG__SEM_IDLE_MASK                                                                       0x04000000L
#define LSDMA_STATUS_REG__SEM_REQ_STALL_MASK                                                                  0x08000000L
#define LSDMA_STATUS_REG__SEM_RESP_STATE_MASK                                                                 0x30000000L
#define LSDMA_STATUS_REG__INT_IDLE_MASK                                                                       0x40000000L
#define LSDMA_STATUS_REG__INT_REQ_STALL_MASK                                                                  0x80000000L
//LSDMA_STATUS1_REG
#define LSDMA_STATUS1_REG__CE_WREQ_IDLE__SHIFT                                                                0x0
#define LSDMA_STATUS1_REG__CE_WR_IDLE__SHIFT                                                                  0x1
#define LSDMA_STATUS1_REG__CE_SPLIT_IDLE__SHIFT                                                               0x2
#define LSDMA_STATUS1_REG__CE_RREQ_IDLE__SHIFT                                                                0x3
#define LSDMA_STATUS1_REG__CE_OUT_IDLE__SHIFT                                                                 0x4
#define LSDMA_STATUS1_REG__CE_IN_IDLE__SHIFT                                                                  0x5
#define LSDMA_STATUS1_REG__CE_DST_IDLE__SHIFT                                                                 0x6
#define LSDMA_STATUS1_REG__CE_DRM_IDLE__SHIFT                                                                 0x7
#define LSDMA_STATUS1_REG__CE_DRM1_IDLE__SHIFT                                                                0x8
#define LSDMA_STATUS1_REG__CE_CMD_IDLE__SHIFT                                                                 0x9
#define LSDMA_STATUS1_REG__CE_AFIFO_FULL__SHIFT                                                               0xa
#define LSDMA_STATUS1_REG__CE_INFO_FULL__SHIFT                                                                0xb
#define LSDMA_STATUS1_REG__CE_INFO1_FULL__SHIFT                                                               0xc
#define LSDMA_STATUS1_REG__EX_START__SHIFT                                                                    0xd
#define LSDMA_STATUS1_REG__DRM_CTX_RESTORE__SHIFT                                                             0xe
#define LSDMA_STATUS1_REG__CE_RD_STALL__SHIFT                                                                 0xf
#define LSDMA_STATUS1_REG__CE_WR_STALL__SHIFT                                                                 0x10
#define LSDMA_STATUS1_REG__CE_WREQ_IDLE_MASK                                                                  0x00000001L
#define LSDMA_STATUS1_REG__CE_WR_IDLE_MASK                                                                    0x00000002L
#define LSDMA_STATUS1_REG__CE_SPLIT_IDLE_MASK                                                                 0x00000004L
#define LSDMA_STATUS1_REG__CE_RREQ_IDLE_MASK                                                                  0x00000008L
#define LSDMA_STATUS1_REG__CE_OUT_IDLE_MASK                                                                   0x00000010L
#define LSDMA_STATUS1_REG__CE_IN_IDLE_MASK                                                                    0x00000020L
#define LSDMA_STATUS1_REG__CE_DST_IDLE_MASK                                                                   0x00000040L
#define LSDMA_STATUS1_REG__CE_CMD_IDLE_MASK                                                                   0x00000200L
#define LSDMA_STATUS1_REG__CE_AFIFO_FULL_MASK                                                                 0x00000400L
#define LSDMA_STATUS1_REG__CE_INFO_FULL_MASK                                                                  0x00000800L
#define LSDMA_STATUS1_REG__CE_INFO1_FULL_MASK                                                                 0x00001000L
#define LSDMA_STATUS1_REG__EX_START_MASK                                                                      0x00002000L
#define LSDMA_STATUS1_REG__CE_RD_STALL_MASK                                                                   0x00008000L
#define LSDMA_STATUS1_REG__CE_WR_STALL_MASK                                                                   0x00010000L
//LSDMA_RD_BURST_CNTL
#define LSDMA_RD_BURST_CNTL__RD_BURST__SHIFT                                                                  0x0
#define LSDMA_RD_BURST_CNTL__CMD_BUFFER_RD_BURST__SHIFT                                                       0x2
#define LSDMA_RD_BURST_CNTL__RD_BURST_MASK                                                                    0x00000003L
#define LSDMA_RD_BURST_CNTL__CMD_BUFFER_RD_BURST_MASK                                                         0x0000000CL
//LSDMA_HBM_PAGE_CONFIG
#define LSDMA_HBM_PAGE_CONFIG__PAGE_SIZE_EXPONENT__SHIFT                                                      0x0
#define LSDMA_HBM_PAGE_CONFIG__PAGE_SIZE_EXPONENT_MASK                                                        0x00000003L
//LSDMA_UCODE_CHECKSUM
#define LSDMA_UCODE_CHECKSUM__DATA__SHIFT                                                                     0x0
#define LSDMA_UCODE_CHECKSUM__DATA_MASK                                                                       0xFFFFFFFFL
//LSDMA_FREEZE
#define LSDMA_FREEZE__PREEMPT__SHIFT                                                                          0x0
#define LSDMA_FREEZE__FREEZE__SHIFT                                                                           0x4
#define LSDMA_FREEZE__FROZEN__SHIFT                                                                           0x5
#define LSDMA_FREEZE__F32_FREEZE__SHIFT                                                                       0x6
#define LSDMA_FREEZE__PREEMPT_MASK                                                                            0x00000001L
#define LSDMA_FREEZE__FREEZE_MASK                                                                             0x00000010L
#define LSDMA_FREEZE__FROZEN_MASK                                                                             0x00000020L
#define LSDMA_FREEZE__F32_FREEZE_MASK                                                                         0x00000040L
//LSDMA_DCC_CNTL
#define LSDMA_DCC_CNTL__DCC_FORCE_BYPASS__SHIFT                                                               0x0
#define LSDMA_DCC_CNTL__DCC_FORCE_BYPASS_MASK                                                                 0x00000001L
//LSDMA_POWER_GATING
#define LSDMA_POWER_GATING__LSDMA_POWER_OFF_CONDITION__SHIFT                                                  0x0
#define LSDMA_POWER_GATING__LSDMA_POWER_ON_CONDITION__SHIFT                                                   0x1
#define LSDMA_POWER_GATING__LSDMA_POWER_OFF_REQ__SHIFT                                                        0x2
#define LSDMA_POWER_GATING__LSDMA_POWER_ON_REQ__SHIFT                                                         0x3
#define LSDMA_POWER_GATING__PG_CNTL_STATUS__SHIFT                                                             0x4
#define LSDMA_POWER_GATING__LSDMA_POWER_OFF_CONDITION_MASK                                                    0x00000001L
#define LSDMA_POWER_GATING__LSDMA_POWER_ON_CONDITION_MASK                                                     0x00000002L
#define LSDMA_POWER_GATING__LSDMA_POWER_OFF_REQ_MASK                                                          0x00000004L
#define LSDMA_POWER_GATING__LSDMA_POWER_ON_REQ_MASK                                                           0x00000008L
#define LSDMA_POWER_GATING__PG_CNTL_STATUS_MASK                                                               0x00000030L
//LSDMA_PGFSM_CONFIG
#define LSDMA_PGFSM_CONFIG__FSM_ADDR__SHIFT                                                                   0x0
#define LSDMA_PGFSM_CONFIG__POWER_DOWN__SHIFT                                                                 0x8
#define LSDMA_PGFSM_CONFIG__POWER_UP__SHIFT                                                                   0x9
#define LSDMA_PGFSM_CONFIG__P1_SELECT__SHIFT                                                                  0xa
#define LSDMA_PGFSM_CONFIG__P2_SELECT__SHIFT                                                                  0xb
#define LSDMA_PGFSM_CONFIG__WRITE__SHIFT                                                                      0xc
#define LSDMA_PGFSM_CONFIG__READ__SHIFT                                                                       0xd
#define LSDMA_PGFSM_CONFIG__SRBM_OVERRIDE__SHIFT                                                              0x1b
#define LSDMA_PGFSM_CONFIG__REG_ADDR__SHIFT                                                                   0x1c
#define LSDMA_PGFSM_CONFIG__FSM_ADDR_MASK                                                                     0x000000FFL
#define LSDMA_PGFSM_CONFIG__POWER_DOWN_MASK                                                                   0x00000100L
#define LSDMA_PGFSM_CONFIG__POWER_UP_MASK                                                                     0x00000200L
#define LSDMA_PGFSM_CONFIG__P1_SELECT_MASK                                                                    0x00000400L
#define LSDMA_PGFSM_CONFIG__P2_SELECT_MASK                                                                    0x00000800L
#define LSDMA_PGFSM_CONFIG__WRITE_MASK                                                                        0x00001000L
#define LSDMA_PGFSM_CONFIG__READ_MASK                                                                         0x00002000L
#define LSDMA_PGFSM_CONFIG__SRBM_OVERRIDE_MASK                                                                0x08000000L
#define LSDMA_PGFSM_CONFIG__REG_ADDR_MASK                                                                     0xF0000000L
//LSDMA_PGFSM_WRITE
#define LSDMA_PGFSM_WRITE__VALUE__SHIFT                                                                       0x0
#define LSDMA_PGFSM_WRITE__VALUE_MASK                                                                         0xFFFFFFFFL
//LSDMA_PGFSM_READ
#define LSDMA_PGFSM_READ__VALUE__SHIFT                                                                        0x0
#define LSDMA_PGFSM_READ__VALUE_MASK                                                                          0x00FFFFFFL
//LSDMA_BA_THRESHOLD
#define LSDMA_BA_THRESHOLD__READ_THRES__SHIFT                                                                 0x0
#define LSDMA_BA_THRESHOLD__WRITE_THRES__SHIFT                                                                0x10
#define LSDMA_BA_THRESHOLD__READ_THRES_MASK                                                                   0x000003FFL
#define LSDMA_BA_THRESHOLD__WRITE_THRES_MASK                                                                  0x03FF0000L
//LSDMA_ID
#define LSDMA_ID__DEVICE_ID__SHIFT                                                                            0x0
#define LSDMA_ID__DEVICE_ID_MASK                                                                              0x000000FFL
//LSDMA_VERSION
#define LSDMA_VERSION__MINVER__SHIFT                                                                          0x0
#define LSDMA_VERSION__MAJVER__SHIFT                                                                          0x8
#define LSDMA_VERSION__REV__SHIFT                                                                             0x10
#define LSDMA_VERSION__MINVER_MASK                                                                            0x0000007FL
#define LSDMA_VERSION__MAJVER_MASK                                                                            0x00007F00L
#define LSDMA_VERSION__REV_MASK                                                                               0x003F0000L
//LSDMA_EDC_COUNTER
#define LSDMA_EDC_COUNTER__LSDMA_MBANK_DATA_BUF0_SED__SHIFT                                                   0x0
#define LSDMA_EDC_COUNTER__LSDMA_MBANK_DATA_BUF1_SED__SHIFT                                                   0x2
#define LSDMA_EDC_COUNTER__LSDMA_MBANK_DATA_BUF2_SED__SHIFT                                                   0x4
#define LSDMA_EDC_COUNTER__LSDMA_MBANK_DATA_BUF3_SED__SHIFT                                                   0x6
#define LSDMA_EDC_COUNTER__LSDMA_MBANK_DATA_BUF4_SED__SHIFT                                                   0x8
#define LSDMA_EDC_COUNTER__LSDMA_MBANK_DATA_BUF5_SED__SHIFT                                                   0xa
#define LSDMA_EDC_COUNTER__LSDMA_MBANK_DATA_BUF6_SED__SHIFT                                                   0xc
#define LSDMA_EDC_COUNTER__LSDMA_MBANK_DATA_BUF7_SED__SHIFT                                                   0xe
#define LSDMA_EDC_COUNTER__LSDMA_MBANK_DATA_BUF8_SED__SHIFT                                                   0x10
#define LSDMA_EDC_COUNTER__LSDMA_MBANK_DATA_BUF9_SED__SHIFT                                                   0x12
#define LSDMA_EDC_COUNTER__LSDMA_MBANK_DATA_BUF10_SED__SHIFT                                                  0x14
#define LSDMA_EDC_COUNTER__LSDMA_MBANK_DATA_BUF11_SED__SHIFT                                                  0x16
#define LSDMA_EDC_COUNTER__LSDMA_MBANK_DATA_BUF12_SED__SHIFT                                                  0x18
#define LSDMA_EDC_COUNTER__LSDMA_MBANK_DATA_BUF13_SED__SHIFT                                                  0x1a
#define LSDMA_EDC_COUNTER__LSDMA_MBANK_DATA_BUF14_SED__SHIFT                                                  0x1c
#define LSDMA_EDC_COUNTER__LSDMA_MBANK_DATA_BUF15_SED__SHIFT                                                  0x1e
#define LSDMA_EDC_COUNTER__LSDMA_MBANK_DATA_BUF0_SED_MASK                                                     0x00000003L
#define LSDMA_EDC_COUNTER__LSDMA_MBANK_DATA_BUF1_SED_MASK                                                     0x0000000CL
#define LSDMA_EDC_COUNTER__LSDMA_MBANK_DATA_BUF2_SED_MASK                                                     0x00000030L
#define LSDMA_EDC_COUNTER__LSDMA_MBANK_DATA_BUF3_SED_MASK                                                     0x000000C0L
#define LSDMA_EDC_COUNTER__LSDMA_MBANK_DATA_BUF4_SED_MASK                                                     0x00000300L
#define LSDMA_EDC_COUNTER__LSDMA_MBANK_DATA_BUF5_SED_MASK                                                     0x00000C00L
#define LSDMA_EDC_COUNTER__LSDMA_MBANK_DATA_BUF6_SED_MASK                                                     0x00003000L
#define LSDMA_EDC_COUNTER__LSDMA_MBANK_DATA_BUF7_SED_MASK                                                     0x0000C000L
#define LSDMA_EDC_COUNTER__LSDMA_MBANK_DATA_BUF8_SED_MASK                                                     0x00030000L
#define LSDMA_EDC_COUNTER__LSDMA_MBANK_DATA_BUF9_SED_MASK                                                     0x000C0000L
#define LSDMA_EDC_COUNTER__LSDMA_MBANK_DATA_BUF10_SED_MASK                                                    0x00300000L
#define LSDMA_EDC_COUNTER__LSDMA_MBANK_DATA_BUF11_SED_MASK                                                    0x00C00000L
#define LSDMA_EDC_COUNTER__LSDMA_MBANK_DATA_BUF12_SED_MASK                                                    0x03000000L
#define LSDMA_EDC_COUNTER__LSDMA_MBANK_DATA_BUF13_SED_MASK                                                    0x0C000000L
#define LSDMA_EDC_COUNTER__LSDMA_MBANK_DATA_BUF14_SED_MASK                                                    0x30000000L
#define LSDMA_EDC_COUNTER__LSDMA_MBANK_DATA_BUF15_SED_MASK                                                    0xC0000000L
//LSDMA_EDC_COUNTER2
#define LSDMA_EDC_COUNTER2__LSDMA_UCODE_BUF_SED__SHIFT                                                        0x0
#define LSDMA_EDC_COUNTER2__LSDMA_RB_CMD_BUF_SED__SHIFT                                                       0x2
#define LSDMA_EDC_COUNTER2__LSDMA_IB_CMD_BUF_SED__SHIFT                                                       0x4
#define LSDMA_EDC_COUNTER2__LSDMA_UTCL1_RD_FIFO_SED__SHIFT                                                    0x6
#define LSDMA_EDC_COUNTER2__LSDMA_UTCL1_RDBST_FIFO_SED__SHIFT                                                 0x8
#define LSDMA_EDC_COUNTER2__LSDMA_UTCL1_WR_FIFO_SED__SHIFT                                                    0xa
#define LSDMA_EDC_COUNTER2__LSDMA_DATA_LUT_FIFO_SED__SHIFT                                                    0xc
#define LSDMA_EDC_COUNTER2__LSDMA_SPLIT_DATA_BUF_SED__SHIFT                                                   0xe
#define LSDMA_EDC_COUNTER2__LSDMA_MC_WR_ADDR_FIFO_SED__SHIFT                                                  0x10
#define LSDMA_EDC_COUNTER2__LSDMA_MC_RDRET_BUF_SED__SHIFT                                                     0x12
#define LSDMA_EDC_COUNTER2__LSDMA_UCODE_BUF_SED_MASK                                                          0x00000003L
#define LSDMA_EDC_COUNTER2__LSDMA_RB_CMD_BUF_SED_MASK                                                         0x0000000CL
#define LSDMA_EDC_COUNTER2__LSDMA_IB_CMD_BUF_SED_MASK                                                         0x00000030L
#define LSDMA_EDC_COUNTER2__LSDMA_UTCL1_RD_FIFO_SED_MASK                                                      0x000000C0L
#define LSDMA_EDC_COUNTER2__LSDMA_UTCL1_RDBST_FIFO_SED_MASK                                                   0x00000300L
#define LSDMA_EDC_COUNTER2__LSDMA_UTCL1_WR_FIFO_SED_MASK                                                      0x00000C00L
#define LSDMA_EDC_COUNTER2__LSDMA_DATA_LUT_FIFO_SED_MASK                                                      0x00003000L
#define LSDMA_EDC_COUNTER2__LSDMA_SPLIT_DATA_BUF_SED_MASK                                                     0x0000C000L
#define LSDMA_EDC_COUNTER2__LSDMA_MC_WR_ADDR_FIFO_SED_MASK                                                    0x00030000L
#define LSDMA_EDC_COUNTER2__LSDMA_MC_RDRET_BUF_SED_MASK                                                       0x000C0000L
//LSDMA_STATUS2_REG
#define LSDMA_STATUS2_REG__ID__SHIFT                                                                          0x0
#define LSDMA_STATUS2_REG__F32_INSTR_PTR__SHIFT                                                               0x3
#define LSDMA_STATUS2_REG__CMD_OP__SHIFT                                                                      0x10
#define LSDMA_STATUS2_REG__ID_MASK                                                                            0x00000007L
#define LSDMA_STATUS2_REG__F32_INSTR_PTR_MASK                                                                 0x0000FFF8L
#define LSDMA_STATUS2_REG__CMD_OP_MASK                                                                        0xFFFF0000L
//LSDMA_ATOMIC_CNTL
#define LSDMA_ATOMIC_CNTL__LOOP_TIMER__SHIFT                                                                  0x0
#define LSDMA_ATOMIC_CNTL__LOOP_TIMER_MASK                                                                    0x7FFFFFFFL
//LSDMA_ATOMIC_PREOP_LO
#define LSDMA_ATOMIC_PREOP_LO__DATA__SHIFT                                                                    0x0
#define LSDMA_ATOMIC_PREOP_LO__DATA_MASK                                                                      0xFFFFFFFFL
//LSDMA_ATOMIC_PREOP_HI
#define LSDMA_ATOMIC_PREOP_HI__DATA__SHIFT                                                                    0x0
#define LSDMA_ATOMIC_PREOP_HI__DATA_MASK                                                                      0xFFFFFFFFL
//LSDMA_UTCL1_CNTL
#define LSDMA_UTCL1_CNTL__REDO_ENABLE__SHIFT                                                                  0x0
#define LSDMA_UTCL1_CNTL__REDO_DELAY__SHIFT                                                                   0x1
#define LSDMA_UTCL1_CNTL__REDO_WATERMK__SHIFT                                                                 0xb
#define LSDMA_UTCL1_CNTL__INVACK_DELAY__SHIFT                                                                 0xe
#define LSDMA_UTCL1_CNTL__REQL2_CREDIT__SHIFT                                                                 0x18
#define LSDMA_UTCL1_CNTL__VADDR_WATERMK__SHIFT                                                                0x1d
#define LSDMA_UTCL1_CNTL__REDO_ENABLE_MASK                                                                    0x00000001L
#define LSDMA_UTCL1_CNTL__REDO_DELAY_MASK                                                                     0x000007FEL
#define LSDMA_UTCL1_CNTL__REDO_WATERMK_MASK                                                                   0x00003800L
#define LSDMA_UTCL1_CNTL__INVACK_DELAY_MASK                                                                   0x00FFC000L
#define LSDMA_UTCL1_CNTL__REQL2_CREDIT_MASK                                                                   0x1F000000L
#define LSDMA_UTCL1_CNTL__VADDR_WATERMK_MASK                                                                  0xE0000000L
//LSDMA_UTCL1_WATERMK
#define LSDMA_UTCL1_WATERMK__REQ_WATERMK__SHIFT                                                               0x0
#define LSDMA_UTCL1_WATERMK__REQ_DEPTH__SHIFT                                                                 0x3
#define LSDMA_UTCL1_WATERMK__PAGE_WATERMK__SHIFT                                                              0x5
#define LSDMA_UTCL1_WATERMK__INVREQ_WATERMK__SHIFT                                                            0x8
#define LSDMA_UTCL1_WATERMK__RESERVED__SHIFT                                                                  0x10
#define LSDMA_UTCL1_WATERMK__REQ_WATERMK_MASK                                                                 0x00000007L
#define LSDMA_UTCL1_WATERMK__REQ_DEPTH_MASK                                                                   0x00000018L
#define LSDMA_UTCL1_WATERMK__PAGE_WATERMK_MASK                                                                0x000000E0L
#define LSDMA_UTCL1_WATERMK__INVREQ_WATERMK_MASK                                                              0x0000FF00L
#define LSDMA_UTCL1_WATERMK__RESERVED_MASK                                                                    0xFFFF0000L
//LSDMA_UTCL1_RD_STATUS
#define LSDMA_UTCL1_RD_STATUS__RQMC_RET_ADDR_FIFO_EMPTY__SHIFT                                                0x0
#define LSDMA_UTCL1_RD_STATUS__RQMC_REQ_FIFO_EMPTY__SHIFT                                                     0x1
#define LSDMA_UTCL1_RD_STATUS__RTPG_RET_BUF_EMPTY__SHIFT                                                      0x2
#define LSDMA_UTCL1_RD_STATUS__RTPG_VADDR_FIFO_EMPTY__SHIFT                                                   0x3
#define LSDMA_UTCL1_RD_STATUS__RQPG_HEAD_VIRT_FIFO_EMPTY__SHIFT                                               0x4
#define LSDMA_UTCL1_RD_STATUS__RQPG_REDO_FIFO_EMPTY__SHIFT                                                    0x5
#define LSDMA_UTCL1_RD_STATUS__RQPG_REQPAGE_FIFO_EMPTY__SHIFT                                                 0x6
#define LSDMA_UTCL1_RD_STATUS__RQPG_XNACK_FIFO_EMPTY__SHIFT                                                   0x7
#define LSDMA_UTCL1_RD_STATUS__RQPG_INVREQ_FIFO_EMPTY__SHIFT                                                  0x8
#define LSDMA_UTCL1_RD_STATUS__RQMC_RET_ADDR_FIFO_FULL__SHIFT                                                 0x9
#define LSDMA_UTCL1_RD_STATUS__RQMC_REQ_FIFO_FULL__SHIFT                                                      0xa
#define LSDMA_UTCL1_RD_STATUS__RTPG_RET_BUF_FULL__SHIFT                                                       0xb
#define LSDMA_UTCL1_RD_STATUS__RTPG_VADDR_FIFO_FULL__SHIFT                                                    0xc
#define LSDMA_UTCL1_RD_STATUS__RQPG_HEAD_VIRT_FIFO_FULL__SHIFT                                                0xd
#define LSDMA_UTCL1_RD_STATUS__RQPG_REDO_FIFO_FULL__SHIFT                                                     0xe
#define LSDMA_UTCL1_RD_STATUS__RQPG_REQPAGE_FIFO_FULL__SHIFT                                                  0xf
#define LSDMA_UTCL1_RD_STATUS__RQPG_XNACK_FIFO_FULL__SHIFT                                                    0x10
#define LSDMA_UTCL1_RD_STATUS__RQPG_INVREQ_FIFO_FULL__SHIFT                                                   0x11
#define LSDMA_UTCL1_RD_STATUS__PAGE_FAULT__SHIFT                                                              0x12
#define LSDMA_UTCL1_RD_STATUS__PAGE_NULL__SHIFT                                                               0x13
#define LSDMA_UTCL1_RD_STATUS__REQL2_IDLE__SHIFT                                                              0x14
#define LSDMA_UTCL1_RD_STATUS__CE_L1_STALL__SHIFT                                                             0x15
#define LSDMA_UTCL1_RD_STATUS__NEXT_RD_VECTOR__SHIFT                                                          0x16
#define LSDMA_UTCL1_RD_STATUS__MERGE_STATE__SHIFT                                                             0x1a
#define LSDMA_UTCL1_RD_STATUS__ADDR_RD_RTR__SHIFT                                                             0x1d
#define LSDMA_UTCL1_RD_STATUS__WPTR_POLLING__SHIFT                                                            0x1e
#define LSDMA_UTCL1_RD_STATUS__INVREQ_SIZE__SHIFT                                                             0x1f
#define LSDMA_UTCL1_RD_STATUS__RQMC_RET_ADDR_FIFO_EMPTY_MASK                                                  0x00000001L
#define LSDMA_UTCL1_RD_STATUS__RQMC_REQ_FIFO_EMPTY_MASK                                                       0x00000002L
#define LSDMA_UTCL1_RD_STATUS__RTPG_RET_BUF_EMPTY_MASK                                                        0x00000004L
#define LSDMA_UTCL1_RD_STATUS__RTPG_VADDR_FIFO_EMPTY_MASK                                                     0x00000008L
#define LSDMA_UTCL1_RD_STATUS__RQPG_HEAD_VIRT_FIFO_EMPTY_MASK                                                 0x00000010L
#define LSDMA_UTCL1_RD_STATUS__RQPG_REDO_FIFO_EMPTY_MASK                                                      0x00000020L
#define LSDMA_UTCL1_RD_STATUS__RQPG_REQPAGE_FIFO_EMPTY_MASK                                                   0x00000040L
#define LSDMA_UTCL1_RD_STATUS__RQPG_XNACK_FIFO_EMPTY_MASK                                                     0x00000080L
#define LSDMA_UTCL1_RD_STATUS__RQPG_INVREQ_FIFO_EMPTY_MASK                                                    0x00000100L
#define LSDMA_UTCL1_RD_STATUS__RQMC_RET_ADDR_FIFO_FULL_MASK                                                   0x00000200L
#define LSDMA_UTCL1_RD_STATUS__RQMC_REQ_FIFO_FULL_MASK                                                        0x00000400L
#define LSDMA_UTCL1_RD_STATUS__RTPG_RET_BUF_FULL_MASK                                                         0x00000800L
#define LSDMA_UTCL1_RD_STATUS__RTPG_VADDR_FIFO_FULL_MASK                                                      0x00001000L
#define LSDMA_UTCL1_RD_STATUS__RQPG_HEAD_VIRT_FIFO_FULL_MASK                                                  0x00002000L
#define LSDMA_UTCL1_RD_STATUS__RQPG_REDO_FIFO_FULL_MASK                                                       0x00004000L
#define LSDMA_UTCL1_RD_STATUS__RQPG_REQPAGE_FIFO_FULL_MASK                                                    0x00008000L
#define LSDMA_UTCL1_RD_STATUS__RQPG_XNACK_FIFO_FULL_MASK                                                      0x00010000L
#define LSDMA_UTCL1_RD_STATUS__RQPG_INVREQ_FIFO_FULL_MASK                                                     0x00020000L
#define LSDMA_UTCL1_RD_STATUS__PAGE_FAULT_MASK                                                                0x00040000L
#define LSDMA_UTCL1_RD_STATUS__PAGE_NULL_MASK                                                                 0x00080000L
#define LSDMA_UTCL1_RD_STATUS__REQL2_IDLE_MASK                                                                0x00100000L
#define LSDMA_UTCL1_RD_STATUS__CE_L1_STALL_MASK                                                               0x00200000L
#define LSDMA_UTCL1_RD_STATUS__NEXT_RD_VECTOR_MASK                                                            0x03C00000L
#define LSDMA_UTCL1_RD_STATUS__MERGE_STATE_MASK                                                               0x1C000000L
#define LSDMA_UTCL1_RD_STATUS__ADDR_RD_RTR_MASK                                                               0x20000000L
#define LSDMA_UTCL1_RD_STATUS__WPTR_POLLING_MASK                                                              0x40000000L
#define LSDMA_UTCL1_RD_STATUS__INVREQ_SIZE_MASK                                                               0x80000000L
//LSDMA_UTCL1_WR_STATUS
#define LSDMA_UTCL1_WR_STATUS__RQMC_RET_ADDR_FIFO_EMPTY__SHIFT                                                0x0
#define LSDMA_UTCL1_WR_STATUS__RQMC_REQ_FIFO_EMPTY__SHIFT                                                     0x1
#define LSDMA_UTCL1_WR_STATUS__RTPG_RET_BUF_EMPTY__SHIFT                                                      0x2
#define LSDMA_UTCL1_WR_STATUS__RTPG_VADDR_FIFO_EMPTY__SHIFT                                                   0x3
#define LSDMA_UTCL1_WR_STATUS__RQPG_HEAD_VIRT_FIFO_EMPTY__SHIFT                                               0x4
#define LSDMA_UTCL1_WR_STATUS__RQPG_REDO_FIFO_EMPTY__SHIFT                                                    0x5
#define LSDMA_UTCL1_WR_STATUS__RQPG_REQPAGE_FIFO_EMPTY__SHIFT                                                 0x6
#define LSDMA_UTCL1_WR_STATUS__RQPG_XNACK_FIFO_EMPTY__SHIFT                                                   0x7
#define LSDMA_UTCL1_WR_STATUS__RQPG_INVREQ_FIFO_EMPTY__SHIFT                                                  0x8
#define LSDMA_UTCL1_WR_STATUS__RQMC_RET_ADDR_FIFO_FULL__SHIFT                                                 0x9
#define LSDMA_UTCL1_WR_STATUS__RQMC_REQ_FIFO_FULL__SHIFT                                                      0xa
#define LSDMA_UTCL1_WR_STATUS__RTPG_RET_BUF_FULL__SHIFT                                                       0xb
#define LSDMA_UTCL1_WR_STATUS__RTPG_VADDR_FIFO_FULL__SHIFT                                                    0xc
#define LSDMA_UTCL1_WR_STATUS__RQPG_HEAD_VIRT_FIFO_FULL__SHIFT                                                0xd
#define LSDMA_UTCL1_WR_STATUS__RQPG_REDO_FIFO_FULL__SHIFT                                                     0xe
#define LSDMA_UTCL1_WR_STATUS__RQPG_REQPAGE_FIFO_FULL__SHIFT                                                  0xf
#define LSDMA_UTCL1_WR_STATUS__RQPG_XNACK_FIFO_FULL__SHIFT                                                    0x10
#define LSDMA_UTCL1_WR_STATUS__RQPG_INVREQ_FIFO_FULL__SHIFT                                                   0x11
#define LSDMA_UTCL1_WR_STATUS__PAGE_FAULT__SHIFT                                                              0x12
#define LSDMA_UTCL1_WR_STATUS__PAGE_NULL__SHIFT                                                               0x13
#define LSDMA_UTCL1_WR_STATUS__REQL2_IDLE__SHIFT                                                              0x14
#define LSDMA_UTCL1_WR_STATUS__F32_WR_RTR__SHIFT                                                              0x15
#define LSDMA_UTCL1_WR_STATUS__NEXT_WR_VECTOR__SHIFT                                                          0x16
#define LSDMA_UTCL1_WR_STATUS__MERGE_STATE__SHIFT                                                             0x19
#define LSDMA_UTCL1_WR_STATUS__RPTR_DATA_FIFO_EMPTY__SHIFT                                                    0x1c
#define LSDMA_UTCL1_WR_STATUS__RPTR_DATA_FIFO_FULL__SHIFT                                                     0x1d
#define LSDMA_UTCL1_WR_STATUS__WRREQ_DATA_FIFO_EMPTY__SHIFT                                                   0x1e
#define LSDMA_UTCL1_WR_STATUS__WRREQ_DATA_FIFO_FULL__SHIFT                                                    0x1f
#define LSDMA_UTCL1_WR_STATUS__RQMC_RET_ADDR_FIFO_EMPTY_MASK                                                  0x00000001L
#define LSDMA_UTCL1_WR_STATUS__RQMC_REQ_FIFO_EMPTY_MASK                                                       0x00000002L
#define LSDMA_UTCL1_WR_STATUS__RTPG_RET_BUF_EMPTY_MASK                                                        0x00000004L
#define LSDMA_UTCL1_WR_STATUS__RTPG_VADDR_FIFO_EMPTY_MASK                                                     0x00000008L
#define LSDMA_UTCL1_WR_STATUS__RQPG_HEAD_VIRT_FIFO_EMPTY_MASK                                                 0x00000010L
#define LSDMA_UTCL1_WR_STATUS__RQPG_REDO_FIFO_EMPTY_MASK                                                      0x00000020L
#define LSDMA_UTCL1_WR_STATUS__RQPG_REQPAGE_FIFO_EMPTY_MASK                                                   0x00000040L
#define LSDMA_UTCL1_WR_STATUS__RQPG_XNACK_FIFO_EMPTY_MASK                                                     0x00000080L
#define LSDMA_UTCL1_WR_STATUS__RQPG_INVREQ_FIFO_EMPTY_MASK                                                    0x00000100L
#define LSDMA_UTCL1_WR_STATUS__RQMC_RET_ADDR_FIFO_FULL_MASK                                                   0x00000200L
#define LSDMA_UTCL1_WR_STATUS__RQMC_REQ_FIFO_FULL_MASK                                                        0x00000400L
#define LSDMA_UTCL1_WR_STATUS__RTPG_RET_BUF_FULL_MASK                                                         0x00000800L
#define LSDMA_UTCL1_WR_STATUS__RTPG_VADDR_FIFO_FULL_MASK                                                      0x00001000L
#define LSDMA_UTCL1_WR_STATUS__RQPG_HEAD_VIRT_FIFO_FULL_MASK                                                  0x00002000L
#define LSDMA_UTCL1_WR_STATUS__RQPG_REDO_FIFO_FULL_MASK                                                       0x00004000L
#define LSDMA_UTCL1_WR_STATUS__RQPG_REQPAGE_FIFO_FULL_MASK                                                    0x00008000L
#define LSDMA_UTCL1_WR_STATUS__RQPG_XNACK_FIFO_FULL_MASK                                                      0x00010000L
#define LSDMA_UTCL1_WR_STATUS__RQPG_INVREQ_FIFO_FULL_MASK                                                     0x00020000L
#define LSDMA_UTCL1_WR_STATUS__PAGE_FAULT_MASK                                                                0x00040000L
#define LSDMA_UTCL1_WR_STATUS__PAGE_NULL_MASK                                                                 0x00080000L
#define LSDMA_UTCL1_WR_STATUS__REQL2_IDLE_MASK                                                                0x00100000L
#define LSDMA_UTCL1_WR_STATUS__F32_WR_RTR_MASK                                                                0x00200000L
#define LSDMA_UTCL1_WR_STATUS__NEXT_WR_VECTOR_MASK                                                            0x01C00000L
#define LSDMA_UTCL1_WR_STATUS__MERGE_STATE_MASK                                                               0x0E000000L
#define LSDMA_UTCL1_WR_STATUS__RPTR_DATA_FIFO_EMPTY_MASK                                                      0x10000000L
#define LSDMA_UTCL1_WR_STATUS__RPTR_DATA_FIFO_FULL_MASK                                                       0x20000000L
#define LSDMA_UTCL1_WR_STATUS__WRREQ_DATA_FIFO_EMPTY_MASK                                                     0x40000000L
#define LSDMA_UTCL1_WR_STATUS__WRREQ_DATA_FIFO_FULL_MASK                                                      0x80000000L
//LSDMA_UTCL1_INV0
#define LSDMA_UTCL1_INV0__INV_MIDDLE__SHIFT                                                                   0x0
#define LSDMA_UTCL1_INV0__RD_TIMEOUT__SHIFT                                                                   0x1
#define LSDMA_UTCL1_INV0__WR_TIMEOUT__SHIFT                                                                   0x2
#define LSDMA_UTCL1_INV0__RD_IN_INVADR__SHIFT                                                                 0x3
#define LSDMA_UTCL1_INV0__WR_IN_INVADR__SHIFT                                                                 0x4
#define LSDMA_UTCL1_INV0__PAGE_NULL_SW__SHIFT                                                                 0x5
#define LSDMA_UTCL1_INV0__XNACK_IS_INVADR__SHIFT                                                              0x6
#define LSDMA_UTCL1_INV0__INVREQ_ENABLE__SHIFT                                                                0x7
#define LSDMA_UTCL1_INV0__NACK_TIMEOUT_SW__SHIFT                                                              0x8
#define LSDMA_UTCL1_INV0__NFLUSH_INV_IDLE__SHIFT                                                              0x9
#define LSDMA_UTCL1_INV0__FLUSH_INV_IDLE__SHIFT                                                               0xa
#define LSDMA_UTCL1_INV0__INV_FLUSHTYPE__SHIFT                                                                0xb
#define LSDMA_UTCL1_INV0__INV_VMID_VEC__SHIFT                                                                 0xc
#define LSDMA_UTCL1_INV0__INV_ADDR_HI__SHIFT                                                                  0x1c
#define LSDMA_UTCL1_INV0__INV_MIDDLE_MASK                                                                     0x00000001L
#define LSDMA_UTCL1_INV0__RD_TIMEOUT_MASK                                                                     0x00000002L
#define LSDMA_UTCL1_INV0__WR_TIMEOUT_MASK                                                                     0x00000004L
#define LSDMA_UTCL1_INV0__RD_IN_INVADR_MASK                                                                   0x00000008L
#define LSDMA_UTCL1_INV0__WR_IN_INVADR_MASK                                                                   0x00000010L
#define LSDMA_UTCL1_INV0__PAGE_NULL_SW_MASK                                                                   0x00000020L
#define LSDMA_UTCL1_INV0__XNACK_IS_INVADR_MASK                                                                0x00000040L
#define LSDMA_UTCL1_INV0__INVREQ_ENABLE_MASK                                                                  0x00000080L
#define LSDMA_UTCL1_INV0__NACK_TIMEOUT_SW_MASK                                                                0x00000100L
#define LSDMA_UTCL1_INV0__NFLUSH_INV_IDLE_MASK                                                                0x00000200L
#define LSDMA_UTCL1_INV0__FLUSH_INV_IDLE_MASK                                                                 0x00000400L
#define LSDMA_UTCL1_INV0__INV_FLUSHTYPE_MASK                                                                  0x00000800L
#define LSDMA_UTCL1_INV0__INV_VMID_VEC_MASK                                                                   0x0FFFF000L
#define LSDMA_UTCL1_INV0__INV_ADDR_HI_MASK                                                                    0xF0000000L
//LSDMA_UTCL1_INV1
#define LSDMA_UTCL1_INV1__INV_ADDR_LO__SHIFT                                                                  0x0
#define LSDMA_UTCL1_INV1__INV_ADDR_LO_MASK                                                                    0xFFFFFFFFL
//LSDMA_UTCL1_INV2
#define LSDMA_UTCL1_INV2__INV_NFLUSH_VMID_VEC__SHIFT                                                          0x0
#define LSDMA_UTCL1_INV2__INV_NFLUSH_VMID_VEC_MASK                                                            0xFFFFFFFFL
//LSDMA_UTCL1_RD_XNACK0
#define LSDMA_UTCL1_RD_XNACK0__XNACK_ADDR_LO__SHIFT                                                           0x0
#define LSDMA_UTCL1_RD_XNACK0__XNACK_ADDR_LO_MASK                                                             0xFFFFFFFFL
//LSDMA_UTCL1_RD_XNACK1
#define LSDMA_UTCL1_RD_XNACK1__XNACK_ADDR_HI__SHIFT                                                           0x0
#define LSDMA_UTCL1_RD_XNACK1__XNACK_VMID__SHIFT                                                              0x4
#define LSDMA_UTCL1_RD_XNACK1__XNACK_VECTOR__SHIFT                                                            0x8
#define LSDMA_UTCL1_RD_XNACK1__IS_XNACK__SHIFT                                                                0x1a
#define LSDMA_UTCL1_RD_XNACK1__XNACK_ADDR_HI_MASK                                                             0x0000000FL
#define LSDMA_UTCL1_RD_XNACK1__XNACK_VMID_MASK                                                                0x000000F0L
#define LSDMA_UTCL1_RD_XNACK1__XNACK_VECTOR_MASK                                                              0x03FFFF00L
#define LSDMA_UTCL1_RD_XNACK1__IS_XNACK_MASK                                                                  0x0C000000L
//LSDMA_UTCL1_WR_XNACK0
#define LSDMA_UTCL1_WR_XNACK0__XNACK_ADDR_LO__SHIFT                                                           0x0
#define LSDMA_UTCL1_WR_XNACK0__XNACK_ADDR_LO_MASK                                                             0xFFFFFFFFL
//LSDMA_UTCL1_WR_XNACK1
#define LSDMA_UTCL1_WR_XNACK1__XNACK_ADDR_HI__SHIFT                                                           0x0
#define LSDMA_UTCL1_WR_XNACK1__XNACK_VMID__SHIFT                                                              0x4
#define LSDMA_UTCL1_WR_XNACK1__XNACK_VECTOR__SHIFT                                                            0x8
#define LSDMA_UTCL1_WR_XNACK1__IS_XNACK__SHIFT                                                                0x1a
#define LSDMA_UTCL1_WR_XNACK1__XNACK_ADDR_HI_MASK                                                             0x0000000FL
#define LSDMA_UTCL1_WR_XNACK1__XNACK_VMID_MASK                                                                0x000000F0L
#define LSDMA_UTCL1_WR_XNACK1__XNACK_VECTOR_MASK                                                              0x03FFFF00L
#define LSDMA_UTCL1_WR_XNACK1__IS_XNACK_MASK                                                                  0x0C000000L
//LSDMA_UTCL1_TIMEOUT
#define LSDMA_UTCL1_TIMEOUT__RD_XNACK_LIMIT__SHIFT                                                            0x0
#define LSDMA_UTCL1_TIMEOUT__WR_XNACK_LIMIT__SHIFT                                                            0x10
#define LSDMA_UTCL1_TIMEOUT__RD_XNACK_LIMIT_MASK                                                              0x0000FFFFL
#define LSDMA_UTCL1_TIMEOUT__WR_XNACK_LIMIT_MASK                                                              0xFFFF0000L
//LSDMA_UTCL1_PAGE
#define LSDMA_UTCL1_PAGE__INVALID_ADDR__SHIFT                                                                 0x0
#define LSDMA_UTCL1_PAGE__REQ_TYPE__SHIFT                                                                     0x1
#define LSDMA_UTCL1_PAGE__TMZ_ENABLE__SHIFT                                                                   0x5
#define LSDMA_UTCL1_PAGE__USE_MTYPE__SHIFT                                                                    0x6
#define LSDMA_UTCL1_PAGE__USE_PT_SNOOP__SHIFT                                                                 0x9
#define LSDMA_UTCL1_PAGE__REQ_TYPE_MASK                                                                       0x0000001EL
#define LSDMA_UTCL1_PAGE__TMZ_ENABLE_MASK                                                                     0x00000020L
#define LSDMA_UTCL1_PAGE__USE_MTYPE_MASK                                                                      0x000001C0L
#define LSDMA_UTCL1_PAGE__USE_PT_SNOOP_MASK                                                                   0x00000200L
//LSDMA_RELAX_ORDERING_LUT
#define LSDMA_RELAX_ORDERING_LUT__RESERVED0__SHIFT                                                            0x0
#define LSDMA_RELAX_ORDERING_LUT__COPY__SHIFT                                                                 0x1
#define LSDMA_RELAX_ORDERING_LUT__WRITE__SHIFT                                                                0x2
#define LSDMA_RELAX_ORDERING_LUT__RESERVED3__SHIFT                                                            0x3
#define LSDMA_RELAX_ORDERING_LUT__RESERVED4__SHIFT                                                            0x4
#define LSDMA_RELAX_ORDERING_LUT__FENCE__SHIFT                                                                0x5
#define LSDMA_RELAX_ORDERING_LUT__RESERVED76__SHIFT                                                           0x6
#define LSDMA_RELAX_ORDERING_LUT__POLL_MEM__SHIFT                                                             0x8
#define LSDMA_RELAX_ORDERING_LUT__COND_EXE__SHIFT                                                             0x9
#define LSDMA_RELAX_ORDERING_LUT__ATOMIC__SHIFT                                                               0xa
#define LSDMA_RELAX_ORDERING_LUT__CONST_FILL__SHIFT                                                           0xb
#define LSDMA_RELAX_ORDERING_LUT__PTEPDE__SHIFT                                                               0xc
#define LSDMA_RELAX_ORDERING_LUT__TIMESTAMP__SHIFT                                                            0xd
#define LSDMA_RELAX_ORDERING_LUT__RESERVED__SHIFT                                                             0xe
#define LSDMA_RELAX_ORDERING_LUT__WORLD_SWITCH__SHIFT                                                         0x1b
#define LSDMA_RELAX_ORDERING_LUT__RPTR_WRB__SHIFT                                                             0x1c
#define LSDMA_RELAX_ORDERING_LUT__WPTR_POLL__SHIFT                                                            0x1d
#define LSDMA_RELAX_ORDERING_LUT__IB_FETCH__SHIFT                                                             0x1e
#define LSDMA_RELAX_ORDERING_LUT__RB_FETCH__SHIFT                                                             0x1f
#define LSDMA_RELAX_ORDERING_LUT__RESERVED0_MASK                                                              0x00000001L
#define LSDMA_RELAX_ORDERING_LUT__COPY_MASK                                                                   0x00000002L
#define LSDMA_RELAX_ORDERING_LUT__WRITE_MASK                                                                  0x00000004L
#define LSDMA_RELAX_ORDERING_LUT__RESERVED3_MASK                                                              0x00000008L
#define LSDMA_RELAX_ORDERING_LUT__RESERVED4_MASK                                                              0x00000010L
#define LSDMA_RELAX_ORDERING_LUT__FENCE_MASK                                                                  0x00000020L
#define LSDMA_RELAX_ORDERING_LUT__RESERVED76_MASK                                                             0x000000C0L
#define LSDMA_RELAX_ORDERING_LUT__POLL_MEM_MASK                                                               0x00000100L
#define LSDMA_RELAX_ORDERING_LUT__COND_EXE_MASK                                                               0x00000200L
#define LSDMA_RELAX_ORDERING_LUT__ATOMIC_MASK                                                                 0x00000400L
#define LSDMA_RELAX_ORDERING_LUT__CONST_FILL_MASK                                                             0x00000800L
#define LSDMA_RELAX_ORDERING_LUT__PTEPDE_MASK                                                                 0x00001000L
#define LSDMA_RELAX_ORDERING_LUT__TIMESTAMP_MASK                                                              0x00002000L
#define LSDMA_RELAX_ORDERING_LUT__RESERVED_MASK                                                               0x07FFC000L
#define LSDMA_RELAX_ORDERING_LUT__WORLD_SWITCH_MASK                                                           0x08000000L
#define LSDMA_RELAX_ORDERING_LUT__RPTR_WRB_MASK                                                               0x10000000L
#define LSDMA_RELAX_ORDERING_LUT__WPTR_POLL_MASK                                                              0x20000000L
#define LSDMA_RELAX_ORDERING_LUT__IB_FETCH_MASK                                                               0x40000000L
#define LSDMA_RELAX_ORDERING_LUT__RB_FETCH_MASK                                                               0x80000000L
//LSDMA_CHICKEN_BITS_2
#define LSDMA_CHICKEN_BITS_2__F32_CMD_PROC_DELAY__SHIFT                                                       0x0
#define LSDMA_CHICKEN_BITS_2__F32_SEND_POSTCODE_EN__SHIFT                                                     0x4
#define LSDMA_CHICKEN_BITS_2__F32_CMD_PROC_DELAY_MASK                                                         0x0000000FL
#define LSDMA_CHICKEN_BITS_2__F32_SEND_POSTCODE_EN_MASK                                                       0x00000010L
//LSDMA_STATUS3_REG
#define LSDMA_STATUS3_REG__CMD_OP_STATUS__SHIFT                                                               0x0
#define LSDMA_STATUS3_REG__PREV_VM_CMD__SHIFT                                                                 0x10
#define LSDMA_STATUS3_REG__EXCEPTION_IDLE__SHIFT                                                              0x14
#define LSDMA_STATUS3_REG__QUEUE_ID_MATCH__SHIFT                                                              0x15
#define LSDMA_STATUS3_REG__INT_QUEUE_ID__SHIFT                                                                0x16
#define LSDMA_STATUS3_REG__CMD_OP_STATUS_MASK                                                                 0x0000FFFFL
#define LSDMA_STATUS3_REG__PREV_VM_CMD_MASK                                                                   0x000F0000L
#define LSDMA_STATUS3_REG__EXCEPTION_IDLE_MASK                                                                0x00100000L
#define LSDMA_STATUS3_REG__QUEUE_ID_MATCH_MASK                                                                0x00200000L
#define LSDMA_STATUS3_REG__INT_QUEUE_ID_MASK                                                                  0x03C00000L
//LSDMA_PHYSICAL_ADDR_LO
#define LSDMA_PHYSICAL_ADDR_LO__D_VALID__SHIFT                                                                0x0
#define LSDMA_PHYSICAL_ADDR_LO__DIRTY__SHIFT                                                                  0x1
#define LSDMA_PHYSICAL_ADDR_LO__PHY_VALID__SHIFT                                                              0x2
#define LSDMA_PHYSICAL_ADDR_LO__ADDR__SHIFT                                                                   0xc
#define LSDMA_PHYSICAL_ADDR_LO__D_VALID_MASK                                                                  0x00000001L
#define LSDMA_PHYSICAL_ADDR_LO__DIRTY_MASK                                                                    0x00000002L
#define LSDMA_PHYSICAL_ADDR_LO__PHY_VALID_MASK                                                                0x00000004L
#define LSDMA_PHYSICAL_ADDR_LO__ADDR_MASK                                                                     0xFFFFF000L
//LSDMA_PHYSICAL_ADDR_HI
#define LSDMA_PHYSICAL_ADDR_HI__ADDR__SHIFT                                                                   0x0
#define LSDMA_PHYSICAL_ADDR_HI__ADDR_MASK                                                                     0x0000FFFFL
//LSDMA_ECC_CNTL
#define LSDMA_ECC_CNTL__ECC_DISABLE__SHIFT                                                                    0x0
#define LSDMA_ECC_CNTL__ECC_DISABLE_MASK                                                                      0x00000001L
//LSDMA_ERROR_LOG
#define LSDMA_ERROR_LOG__OVERRIDE__SHIFT                                                                      0x0
#define LSDMA_ERROR_LOG__STATUS__SHIFT                                                                        0x10
#define LSDMA_ERROR_LOG__OVERRIDE_MASK                                                                        0x0000FFFFL
#define LSDMA_ERROR_LOG__STATUS_MASK                                                                          0xFFFF0000L
//LSDMA_PUB_DUMMY0
#define LSDMA_PUB_DUMMY0__DUMMY__SHIFT                                                                        0x0
#define LSDMA_PUB_DUMMY0__DUMMY_MASK                                                                          0xFFFFFFFFL
//LSDMA_PUB_DUMMY1
#define LSDMA_PUB_DUMMY1__DUMMY__SHIFT                                                                        0x0
#define LSDMA_PUB_DUMMY1__DUMMY_MASK                                                                          0xFFFFFFFFL
//LSDMA_PUB_DUMMY2
#define LSDMA_PUB_DUMMY2__DUMMY__SHIFT                                                                        0x0
#define LSDMA_PUB_DUMMY2__DUMMY_MASK                                                                          0xFFFFFFFFL
//LSDMA_PUB_DUMMY3
#define LSDMA_PUB_DUMMY3__DUMMY__SHIFT                                                                        0x0
#define LSDMA_PUB_DUMMY3__DUMMY_MASK                                                                          0xFFFFFFFFL
//LSDMA_F32_COUNTER
#define LSDMA_F32_COUNTER__VALUE__SHIFT                                                                       0x0
#define LSDMA_F32_COUNTER__VALUE_MASK                                                                         0xFFFFFFFFL
//LSDMA_PERFCNT_PERFCOUNTER0_CFG
#define LSDMA_PERFCNT_PERFCOUNTER0_CFG__PERF_SEL__SHIFT                                                       0x0
#define LSDMA_PERFCNT_PERFCOUNTER0_CFG__PERF_SEL_END__SHIFT                                                   0x8
#define LSDMA_PERFCNT_PERFCOUNTER0_CFG__PERF_MODE__SHIFT                                                      0x18
#define LSDMA_PERFCNT_PERFCOUNTER0_CFG__ENABLE__SHIFT                                                         0x1c
#define LSDMA_PERFCNT_PERFCOUNTER0_CFG__CLEAR__SHIFT                                                          0x1d
#define LSDMA_PERFCNT_PERFCOUNTER0_CFG__PERF_SEL_MASK                                                         0x000000FFL
#define LSDMA_PERFCNT_PERFCOUNTER0_CFG__PERF_SEL_END_MASK                                                     0x0000FF00L
#define LSDMA_PERFCNT_PERFCOUNTER0_CFG__PERF_MODE_MASK                                                        0x0F000000L
#define LSDMA_PERFCNT_PERFCOUNTER0_CFG__ENABLE_MASK                                                           0x10000000L
#define LSDMA_PERFCNT_PERFCOUNTER0_CFG__CLEAR_MASK                                                            0x20000000L
//LSDMA_PERFCNT_PERFCOUNTER1_CFG
#define LSDMA_PERFCNT_PERFCOUNTER1_CFG__PERF_SEL__SHIFT                                                       0x0
#define LSDMA_PERFCNT_PERFCOUNTER1_CFG__PERF_SEL_END__SHIFT                                                   0x8
#define LSDMA_PERFCNT_PERFCOUNTER1_CFG__PERF_MODE__SHIFT                                                      0x18
#define LSDMA_PERFCNT_PERFCOUNTER1_CFG__ENABLE__SHIFT                                                         0x1c
#define LSDMA_PERFCNT_PERFCOUNTER1_CFG__CLEAR__SHIFT                                                          0x1d
#define LSDMA_PERFCNT_PERFCOUNTER1_CFG__PERF_SEL_MASK                                                         0x000000FFL
#define LSDMA_PERFCNT_PERFCOUNTER1_CFG__PERF_SEL_END_MASK                                                     0x0000FF00L
#define LSDMA_PERFCNT_PERFCOUNTER1_CFG__PERF_MODE_MASK                                                        0x0F000000L
#define LSDMA_PERFCNT_PERFCOUNTER1_CFG__ENABLE_MASK                                                           0x10000000L
#define LSDMA_PERFCNT_PERFCOUNTER1_CFG__CLEAR_MASK                                                            0x20000000L
//LSDMA_PERFCNT_PERFCOUNTER_RSLT_CNTL
#define LSDMA_PERFCNT_PERFCOUNTER_RSLT_CNTL__PERF_COUNTER_SELECT__SHIFT                                       0x0
#define LSDMA_PERFCNT_PERFCOUNTER_RSLT_CNTL__START_TRIGGER__SHIFT                                             0x8
#define LSDMA_PERFCNT_PERFCOUNTER_RSLT_CNTL__STOP_TRIGGER__SHIFT                                              0x10
#define LSDMA_PERFCNT_PERFCOUNTER_RSLT_CNTL__ENABLE_ANY__SHIFT                                                0x18
#define LSDMA_PERFCNT_PERFCOUNTER_RSLT_CNTL__CLEAR_ALL__SHIFT                                                 0x19
#define LSDMA_PERFCNT_PERFCOUNTER_RSLT_CNTL__STOP_ALL_ON_SATURATE__SHIFT                                      0x1a
#define LSDMA_PERFCNT_PERFCOUNTER_RSLT_CNTL__PERF_COUNTER_SELECT_MASK                                         0x0000000FL
#define LSDMA_PERFCNT_PERFCOUNTER_RSLT_CNTL__START_TRIGGER_MASK                                               0x0000FF00L
#define LSDMA_PERFCNT_PERFCOUNTER_RSLT_CNTL__STOP_TRIGGER_MASK                                                0x00FF0000L
#define LSDMA_PERFCNT_PERFCOUNTER_RSLT_CNTL__ENABLE_ANY_MASK                                                  0x01000000L
#define LSDMA_PERFCNT_PERFCOUNTER_RSLT_CNTL__CLEAR_ALL_MASK                                                   0x02000000L
#define LSDMA_PERFCNT_PERFCOUNTER_RSLT_CNTL__STOP_ALL_ON_SATURATE_MASK                                        0x04000000L
//LSDMA_PERFCNT_MISC_CNTL
#define LSDMA_PERFCNT_MISC_CNTL__CMD_OP__SHIFT                                                                0x0
#define LSDMA_PERFCNT_MISC_CNTL__MMHUB_REQ_EVENT_SELECT__SHIFT                                                0x10
#define LSDMA_PERFCNT_MISC_CNTL__CMD_OP_MASK                                                                  0x0000FFFFL
#define LSDMA_PERFCNT_MISC_CNTL__MMHUB_REQ_EVENT_SELECT_MASK                                                  0x00010000L
//LSDMA_PERFCNT_PERFCOUNTER_LO
#define LSDMA_PERFCNT_PERFCOUNTER_LO__COUNTER_LO__SHIFT                                                       0x0
#define LSDMA_PERFCNT_PERFCOUNTER_LO__COUNTER_LO_MASK                                                         0xFFFFFFFFL
//LSDMA_PERFCNT_PERFCOUNTER_HI
#define LSDMA_PERFCNT_PERFCOUNTER_HI__COUNTER_HI__SHIFT                                                       0x0
#define LSDMA_PERFCNT_PERFCOUNTER_HI__COMPARE_VALUE__SHIFT                                                    0x10
#define LSDMA_PERFCNT_PERFCOUNTER_HI__COUNTER_HI_MASK                                                         0x0000FFFFL
#define LSDMA_PERFCNT_PERFCOUNTER_HI__COMPARE_VALUE_MASK                                                      0xFFFF0000L
//LSDMA_CRD_CNTL
#define LSDMA_CRD_CNTL__DRM_CREDIT__SHIFT                                                                     0x0
#define LSDMA_CRD_CNTL__MC_WRREQ_CREDIT__SHIFT                                                                0x7
#define LSDMA_CRD_CNTL__MC_RDREQ_CREDIT__SHIFT                                                                0xd
#define LSDMA_CRD_CNTL__MC_WRREQ_CREDIT_MASK                                                                  0x00001F80L
#define LSDMA_CRD_CNTL__MC_RDREQ_CREDIT_MASK                                                                  0x0007E000L
//LSDMA_ULV_CNTL
#define LSDMA_ULV_CNTL__HYSTERESIS__SHIFT                                                                     0x0
#define LSDMA_ULV_CNTL__ENTER_ULV_INT_CLR__SHIFT                                                              0x1b
#define LSDMA_ULV_CNTL__EXIT_ULV_INT_CLR__SHIFT                                                               0x1c
#define LSDMA_ULV_CNTL__ENTER_ULV_INT__SHIFT                                                                  0x1d
#define LSDMA_ULV_CNTL__EXIT_ULV_INT__SHIFT                                                                   0x1e
#define LSDMA_ULV_CNTL__ULV_STATUS__SHIFT                                                                     0x1f
#define LSDMA_ULV_CNTL__HYSTERESIS_MASK                                                                       0x0000001FL
#define LSDMA_ULV_CNTL__ENTER_ULV_INT_CLR_MASK                                                                0x08000000L
#define LSDMA_ULV_CNTL__EXIT_ULV_INT_CLR_MASK                                                                 0x10000000L
#define LSDMA_ULV_CNTL__ENTER_ULV_INT_MASK                                                                    0x20000000L
#define LSDMA_ULV_CNTL__EXIT_ULV_INT_MASK                                                                     0x40000000L
#define LSDMA_ULV_CNTL__ULV_STATUS_MASK                                                                       0x80000000L
//LSDMA_EA_DBIT_ADDR_DATA
#define LSDMA_EA_DBIT_ADDR_DATA__VALUE__SHIFT                                                                 0x0
#define LSDMA_EA_DBIT_ADDR_DATA__VALUE_MASK                                                                   0xFFFFFFFFL
//LSDMA_EA_DBIT_ADDR_INDEX
#define LSDMA_EA_DBIT_ADDR_INDEX__VALUE__SHIFT                                                                0x0
#define LSDMA_EA_DBIT_ADDR_INDEX__VALUE_MASK                                                                  0x00000007L
//LSDMA_STATUS4_REG
#define LSDMA_STATUS4_REG__IDLE__SHIFT                                                                        0x0
#define LSDMA_STATUS4_REG__IH_OUTSTANDING__SHIFT                                                              0x2
#define LSDMA_STATUS4_REG__SEM_OUTSTANDING__SHIFT                                                             0x3
#define LSDMA_STATUS4_REG__MMHUB_RD_OUTSTANDING__SHIFT                                                        0x4
#define LSDMA_STATUS4_REG__MMHUB_WR_OUTSTANDING__SHIFT                                                        0x5
#define LSDMA_STATUS4_REG__UTCL2_RD_OUTSTANDING__SHIFT                                                        0x6
#define LSDMA_STATUS4_REG__UTCL2_WR_OUTSTANDING__SHIFT                                                        0x7
#define LSDMA_STATUS4_REG__REG_POLLING__SHIFT                                                                 0x8
#define LSDMA_STATUS4_REG__MEM_POLLING__SHIFT                                                                 0x9
#define LSDMA_STATUS4_REG__UTCL2_RD_XNACK__SHIFT                                                              0xa
#define LSDMA_STATUS4_REG__UTCL2_WR_XNACK__SHIFT                                                              0xc
#define LSDMA_STATUS4_REG__ACTIVE_QUEUE_ID__SHIFT                                                             0xe
#define LSDMA_STATUS4_REG__SRIOV_WATING_RLCV_CMD__SHIFT                                                       0x12
#define LSDMA_STATUS4_REG__SRIOV_LSDMA_EXECUTING_CMD__SHIFT                                                   0x13
#define LSDMA_STATUS4_REG__IDLE_MASK                                                                          0x00000001L
#define LSDMA_STATUS4_REG__IH_OUTSTANDING_MASK                                                                0x00000004L
#define LSDMA_STATUS4_REG__SEM_OUTSTANDING_MASK                                                               0x00000008L
#define LSDMA_STATUS4_REG__MMHUB_RD_OUTSTANDING_MASK                                                          0x00000010L
#define LSDMA_STATUS4_REG__MMHUB_WR_OUTSTANDING_MASK                                                          0x00000020L
#define LSDMA_STATUS4_REG__UTCL2_RD_OUTSTANDING_MASK                                                          0x00000040L
#define LSDMA_STATUS4_REG__UTCL2_WR_OUTSTANDING_MASK                                                          0x00000080L
#define LSDMA_STATUS4_REG__REG_POLLING_MASK                                                                   0x00000100L
#define LSDMA_STATUS4_REG__MEM_POLLING_MASK                                                                   0x00000200L
#define LSDMA_STATUS4_REG__UTCL2_RD_XNACK_MASK                                                                0x00000C00L
#define LSDMA_STATUS4_REG__UTCL2_WR_XNACK_MASK                                                                0x00003000L
#define LSDMA_STATUS4_REG__ACTIVE_QUEUE_ID_MASK                                                               0x0003C000L
#define LSDMA_STATUS4_REG__SRIOV_WATING_RLCV_CMD_MASK                                                         0x00040000L
#define LSDMA_STATUS4_REG__SRIOV_LSDMA_EXECUTING_CMD_MASK                                                     0x00080000L
//LSDMA_CE_CTRL
#define LSDMA_CE_CTRL__RD_LUT_WATERMARK__SHIFT                                                                0x0
#define LSDMA_CE_CTRL__RD_LUT_DEPTH__SHIFT                                                                    0x3
#define LSDMA_CE_CTRL__RESERVED_7_5__SHIFT                                                                    0x5
#define LSDMA_CE_CTRL__RESERVED__SHIFT                                                                        0x8
#define LSDMA_CE_CTRL__RD_LUT_WATERMARK_MASK                                                                  0x00000007L
#define LSDMA_CE_CTRL__RD_LUT_DEPTH_MASK                                                                      0x00000018L
#define LSDMA_CE_CTRL__RESERVED_7_5_MASK                                                                      0x000000E0L
#define LSDMA_CE_CTRL__RESERVED_MASK                                                                          0xFFFFFF00L
//LSDMA_EXCEPTION_STATUS
#define LSDMA_EXCEPTION_STATUS__RB_FETCH_ECC__SHIFT                                                           0x0
#define LSDMA_EXCEPTION_STATUS__IB_FETCH_ECC__SHIFT                                                           0x1
#define LSDMA_EXCEPTION_STATUS__COPY_CMD_ECC__SHIFT                                                           0x2
#define LSDMA_EXCEPTION_STATUS__NON_COPY_CMD_ECC__SHIFT                                                       0x3
#define LSDMA_EXCEPTION_STATUS__SRAM_ECC__SHIFT                                                               0x6
#define LSDMA_EXCEPTION_STATUS__RB_FETCH_NACK_GEN_ERR__SHIFT                                                  0x8
#define LSDMA_EXCEPTION_STATUS__IB_FETCH_NACK_GEN_ERR__SHIFT                                                  0x9
#define LSDMA_EXCEPTION_STATUS__COPY_CMD_NACK_GEN_ERR__SHIFT                                                  0xa
#define LSDMA_EXCEPTION_STATUS__NON_COPY_CMD_NACK_GEN_ERR__SHIFT                                              0xb
#define LSDMA_EXCEPTION_STATUS__RPTR_WB_NACK_GEN_ERR__SHIFT                                                   0xd
#define LSDMA_EXCEPTION_STATUS__RB_FETCH_NACK_PRT__SHIFT                                                      0x10
#define LSDMA_EXCEPTION_STATUS__IB_FETCH_NACK_PRT__SHIFT                                                      0x11
#define LSDMA_EXCEPTION_STATUS__COPY_CMD_NACK_PRT__SHIFT                                                      0x12
#define LSDMA_EXCEPTION_STATUS__NON_COPY_CMD_NACK_PRT__SHIFT                                                  0x13
#define LSDMA_EXCEPTION_STATUS__RPTR_WB_NACK_PRT__SHIFT                                                       0x15
#define LSDMA_EXCEPTION_STATUS__INVALID_ADDR__SHIFT                                                           0x18
#define LSDMA_EXCEPTION_STATUS__RB_FETCH_ECC_MASK                                                             0x00000001L
#define LSDMA_EXCEPTION_STATUS__IB_FETCH_ECC_MASK                                                             0x00000002L
#define LSDMA_EXCEPTION_STATUS__COPY_CMD_ECC_MASK                                                             0x00000004L
#define LSDMA_EXCEPTION_STATUS__NON_COPY_CMD_ECC_MASK                                                         0x00000008L
#define LSDMA_EXCEPTION_STATUS__SRAM_ECC_MASK                                                                 0x00000040L
#define LSDMA_EXCEPTION_STATUS__RB_FETCH_NACK_GEN_ERR_MASK                                                    0x00000100L
#define LSDMA_EXCEPTION_STATUS__IB_FETCH_NACK_GEN_ERR_MASK                                                    0x00000200L
#define LSDMA_EXCEPTION_STATUS__COPY_CMD_NACK_GEN_ERR_MASK                                                    0x00000400L
#define LSDMA_EXCEPTION_STATUS__NON_COPY_CMD_NACK_GEN_ERR_MASK                                                0x00000800L
#define LSDMA_EXCEPTION_STATUS__RPTR_WB_NACK_GEN_ERR_MASK                                                     0x00002000L
#define LSDMA_EXCEPTION_STATUS__RB_FETCH_NACK_PRT_MASK                                                        0x00010000L
#define LSDMA_EXCEPTION_STATUS__IB_FETCH_NACK_PRT_MASK                                                        0x00020000L
#define LSDMA_EXCEPTION_STATUS__COPY_CMD_NACK_PRT_MASK                                                        0x00040000L
#define LSDMA_EXCEPTION_STATUS__NON_COPY_CMD_NACK_PRT_MASK                                                    0x00080000L
#define LSDMA_EXCEPTION_STATUS__RPTR_WB_NACK_PRT_MASK                                                         0x00200000L
//LSDMA_INT_CNTL
#define LSDMA_INT_CNTL__ATOMIC_RTN_DONE_INT_ENABLE__SHIFT                                                     0x0
#define LSDMA_INT_CNTL__TRAP_INT_ENABLE__SHIFT                                                                0x1
#define LSDMA_INT_CNTL__SRBM_WRITE_INT_ENABLE__SHIFT                                                          0x2
#define LSDMA_INT_CNTL__CTX_EMPTY_INT_ENABLE__SHIFT                                                           0x3
#define LSDMA_INT_CNTL__FROZEN_INT_ENABLE__SHIFT                                                              0x4
#define LSDMA_INT_CNTL__PREEMPT_INT_ENABLE__SHIFT                                                             0x5
#define LSDMA_INT_CNTL__IB_PREEMPT_INT_ENABLE__SHIFT                                                          0x6
#define LSDMA_INT_CNTL__ATOMIC_TIMEOUT_INT_ENABLE__SHIFT                                                      0x7
#define LSDMA_INT_CNTL__POLL_TIMEOUT_INT_ENABLE__SHIFT                                                        0x8
#define LSDMA_INT_CNTL__INVALID_ADDR_INT_ENABLE__SHIFT                                                        0x9
#define LSDMA_INT_CNTL__NACK_GEN_ERR_INT_ENABLE__SHIFT                                                        0xa
#define LSDMA_INT_CNTL__NACK_PRT_INT_ENABLE__SHIFT                                                            0xb
#define LSDMA_INT_CNTL__ECC_INT_ENABLE__SHIFT                                                                 0xc
#define LSDMA_INT_CNTL__ATOMIC_RTN_DONE_INT_ENABLE_MASK                                                       0x00000001L
#define LSDMA_INT_CNTL__TRAP_INT_ENABLE_MASK                                                                  0x00000002L
#define LSDMA_INT_CNTL__SRBM_WRITE_INT_ENABLE_MASK                                                            0x00000004L
#define LSDMA_INT_CNTL__CTX_EMPTY_INT_ENABLE_MASK                                                             0x00000008L
#define LSDMA_INT_CNTL__FROZEN_INT_ENABLE_MASK                                                                0x00000010L
#define LSDMA_INT_CNTL__PREEMPT_INT_ENABLE_MASK                                                               0x00000020L
#define LSDMA_INT_CNTL__IB_PREEMPT_INT_ENABLE_MASK                                                            0x00000040L
#define LSDMA_INT_CNTL__ATOMIC_TIMEOUT_INT_ENABLE_MASK                                                        0x00000080L
#define LSDMA_INT_CNTL__POLL_TIMEOUT_INT_ENABLE_MASK                                                          0x00000100L
#define LSDMA_INT_CNTL__NACK_GEN_ERR_INT_ENABLE_MASK                                                          0x00000400L
#define LSDMA_INT_CNTL__NACK_PRT_INT_ENABLE_MASK                                                              0x00000800L
#define LSDMA_INT_CNTL__ECC_INT_ENABLE_MASK                                                                   0x00001000L
//LSDMA_MEM_POWER_CTRL
#define LSDMA_MEM_POWER_CTRL__MEM_POWER_CTRL_EN__SHIFT                                                        0x0
#define LSDMA_MEM_POWER_CTRL__MEM_POWER_CTRL_EN_MASK                                                          0x00000001L
//LSDMA_CLK_CTRL
#define LSDMA_CLK_CTRL__RESERVED__SHIFT                                                                       0x1
#define LSDMA_CLK_CTRL__SOFT_OVERRIDE7__SHIFT                                                                 0x18
#define LSDMA_CLK_CTRL__SOFT_OVERRIDE6__SHIFT                                                                 0x19
#define LSDMA_CLK_CTRL__SOFT_OVERRIDE5__SHIFT                                                                 0x1a
#define LSDMA_CLK_CTRL__SOFT_OVERRIDE4__SHIFT                                                                 0x1b
#define LSDMA_CLK_CTRL__SOFT_OVERRIDE3__SHIFT                                                                 0x1c
#define LSDMA_CLK_CTRL__SOFT_OVERRIDE2__SHIFT                                                                 0x1d
#define LSDMA_CLK_CTRL__SOFT_OVERRIDE1__SHIFT                                                                 0x1e
#define LSDMA_CLK_CTRL__SOFT_OVERRIDE0__SHIFT                                                                 0x1f
#define LSDMA_CLK_CTRL__RESERVED_MASK                                                                         0x00FFFFFEL
#define LSDMA_CLK_CTRL__SOFT_OVERRIDE7_MASK                                                                   0x01000000L
#define LSDMA_CLK_CTRL__SOFT_OVERRIDE6_MASK                                                                   0x02000000L
#define LSDMA_CLK_CTRL__SOFT_OVERRIDE5_MASK                                                                   0x04000000L
#define LSDMA_CLK_CTRL__SOFT_OVERRIDE4_MASK                                                                   0x08000000L
#define LSDMA_CLK_CTRL__SOFT_OVERRIDE3_MASK                                                                   0x10000000L
#define LSDMA_CLK_CTRL__SOFT_OVERRIDE2_MASK                                                                   0x20000000L
#define LSDMA_CLK_CTRL__SOFT_OVERRIDE1_MASK                                                                   0x40000000L
#define LSDMA_CLK_CTRL__SOFT_OVERRIDE0_MASK                                                                   0x80000000L
//LSDMA_CNTL
#define LSDMA_CNTL__UTC_L1_ENABLE__SHIFT                                                                      0x1
#define LSDMA_CNTL__SEM_WAIT_INT_ENABLE__SHIFT                                                                0x2
#define LSDMA_CNTL__DATA_SWAP_ENABLE__SHIFT                                                                   0x3
#define LSDMA_CNTL__FENCE_SWAP_ENABLE__SHIFT                                                                  0x4
#define LSDMA_CNTL__MIDCMD_PREEMPT_ENABLE__SHIFT                                                              0x5
#define LSDMA_CNTL__MIDCMD_EXPIRE_ENABLE__SHIFT                                                               0x6
#define LSDMA_CNTL__MIDCMD_WORLDSWITCH_ENABLE__SHIFT                                                          0x11
#define LSDMA_CNTL__AUTO_CTXSW_ENABLE__SHIFT                                                                  0x12
#define LSDMA_CNTL__DRM_RESTORE_ENABLE__SHIFT                                                                 0x13
#define LSDMA_CNTL__UTC_L1_ENABLE_MASK                                                                        0x00000002L
#define LSDMA_CNTL__SEM_WAIT_INT_ENABLE_MASK                                                                  0x00000004L
#define LSDMA_CNTL__DATA_SWAP_ENABLE_MASK                                                                     0x00000008L
#define LSDMA_CNTL__FENCE_SWAP_ENABLE_MASK                                                                    0x00000010L
#define LSDMA_CNTL__MIDCMD_PREEMPT_ENABLE_MASK                                                                0x00000020L
#define LSDMA_CNTL__MIDCMD_EXPIRE_ENABLE_MASK                                                                 0x00000040L
#define LSDMA_CNTL__MIDCMD_WORLDSWITCH_ENABLE_MASK                                                            0x00020000L
#define LSDMA_CNTL__AUTO_CTXSW_ENABLE_MASK                                                                    0x00040000L
//LSDMA_CHICKEN_BITS
#define LSDMA_CHICKEN_BITS__STALL_ON_TRANS_FULL_ENABLE__SHIFT                                                 0x1
#define LSDMA_CHICKEN_BITS__STALL_ON_NO_FREE_DATA_BUFFER_ENABLE__SHIFT                                        0x2
#define LSDMA_CHICKEN_BITS__F32_MGCG_ENABLE__SHIFT                                                            0x3
#define LSDMA_CHICKEN_BITS__WRITE_BURST_LENGTH__SHIFT                                                         0x8
#define LSDMA_CHICKEN_BITS__WRITE_BURST_WAIT_CYCLE__SHIFT                                                     0xa
#define LSDMA_CHICKEN_BITS__COPY_OVERLAP_ENABLE__SHIFT                                                        0x10
#define LSDMA_CHICKEN_BITS__RAW_CHECK_ENABLE__SHIFT                                                           0x11
#define LSDMA_CHICKEN_BITS__T2L_256B_ENABLE__SHIFT                                                            0x12
#define LSDMA_CHICKEN_BITS__SRBM_POLL_RETRYING__SHIFT                                                         0x14
#define LSDMA_CHICKEN_BITS__CG_STATUS_OUTPUT__SHIFT                                                           0x17
#define LSDMA_CHICKEN_BITS__DRAM_ECC_COPY_MODE_CNTL__SHIFT                                                    0x18
#define LSDMA_CHICKEN_BITS__DRAM_ECC_NACK_F32_RESET_ENABLE__SHIFT                                             0x19
#define LSDMA_CHICKEN_BITS__STALL_ON_TRANS_FULL_ENABLE_MASK                                                   0x00000002L
#define LSDMA_CHICKEN_BITS__STALL_ON_NO_FREE_DATA_BUFFER_ENABLE_MASK                                          0x00000004L
#define LSDMA_CHICKEN_BITS__F32_MGCG_ENABLE_MASK                                                              0x00000008L
#define LSDMA_CHICKEN_BITS__WRITE_BURST_LENGTH_MASK                                                           0x00000300L
#define LSDMA_CHICKEN_BITS__WRITE_BURST_WAIT_CYCLE_MASK                                                       0x00001C00L
#define LSDMA_CHICKEN_BITS__COPY_OVERLAP_ENABLE_MASK                                                          0x00010000L
#define LSDMA_CHICKEN_BITS__RAW_CHECK_ENABLE_MASK                                                             0x00020000L
#define LSDMA_CHICKEN_BITS__T2L_256B_ENABLE_MASK                                                              0x00040000L
#define LSDMA_CHICKEN_BITS__SRBM_POLL_RETRYING_MASK                                                           0x00100000L
#define LSDMA_CHICKEN_BITS__CG_STATUS_OUTPUT_MASK                                                             0x00800000L
//LSDMA_PIO_SRC_ADDR_LO
#define LSDMA_PIO_SRC_ADDR_LO__SRC_ADDR_LO__SHIFT                                                             0x0
#define LSDMA_PIO_SRC_ADDR_LO__SRC_ADDR_LO_MASK                                                               0xFFFFFFFFL
//LSDMA_PIO_SRC_ADDR_HI
#define LSDMA_PIO_SRC_ADDR_HI__SRC_ADDR_HI__SHIFT                                                             0x0
#define LSDMA_PIO_SRC_ADDR_HI__SRC_ADDR_HI_MASK                                                               0xFFFFFFFFL
//LSDMA_PIO_DST_ADDR_LO
#define LSDMA_PIO_DST_ADDR_LO__DST_ADDR_LO__SHIFT                                                             0x0
#define LSDMA_PIO_DST_ADDR_LO__DST_ADDR_LO_MASK                                                               0xFFFFFFFFL
//LSDMA_PIO_DST_ADDR_HI
#define LSDMA_PIO_DST_ADDR_HI__DST_ADDR_HI__SHIFT                                                             0x0
#define LSDMA_PIO_DST_ADDR_HI__DST_ADDR_HI_MASK                                                               0xFFFFFFFFL
//LSDMA_PIO_COMMAND
#define LSDMA_PIO_COMMAND__BYTE_COUNT__SHIFT                                                                  0x0
#define LSDMA_PIO_COMMAND__SRC_LOCATION__SHIFT                                                                0x1a
#define LSDMA_PIO_COMMAND__DST_LOCATION__SHIFT                                                                0x1b
#define LSDMA_PIO_COMMAND__SRC_ADDR_INC__SHIFT                                                                0x1c
#define LSDMA_PIO_COMMAND__DST_ADDR_INC__SHIFT                                                                0x1d
#define LSDMA_PIO_COMMAND__OVERLAP_DISABLE__SHIFT                                                             0x1e
#define LSDMA_PIO_COMMAND__CONSTANT_FILL__SHIFT                                                               0x1f
#define LSDMA_PIO_COMMAND__BYTE_COUNT_MASK                                                                    0x03FFFFFFL
#define LSDMA_PIO_COMMAND__SRC_LOCATION_MASK                                                                  0x04000000L
#define LSDMA_PIO_COMMAND__DST_LOCATION_MASK                                                                  0x08000000L
#define LSDMA_PIO_COMMAND__SRC_ADDR_INC_MASK                                                                  0x10000000L
#define LSDMA_PIO_COMMAND__DST_ADDR_INC_MASK                                                                  0x20000000L
#define LSDMA_PIO_COMMAND__OVERLAP_DISABLE_MASK                                                               0x40000000L
#define LSDMA_PIO_COMMAND__CONSTANT_FILL_MASK                                                                 0x80000000L
//LSDMA_PIO_CONSTFILL_DATA
#define LSDMA_PIO_CONSTFILL_DATA__DATA__SHIFT                                                                 0x0
#define LSDMA_PIO_CONSTFILL_DATA__DATA_MASK                                                                   0xFFFFFFFFL
//LSDMA_PIO_CONTROL
#define LSDMA_PIO_CONTROL__VMID__SHIFT                                                                        0x0
#define LSDMA_PIO_CONTROL__DST_GPA__SHIFT                                                                     0x4
#define LSDMA_PIO_CONTROL__DST_SYS__SHIFT                                                                     0x5
#define LSDMA_PIO_CONTROL__DST_GCC__SHIFT                                                                     0x6
#define LSDMA_PIO_CONTROL__DST_SNOOP__SHIFT                                                                   0x7
#define LSDMA_PIO_CONTROL__DST_REUSE_HINT__SHIFT                                                              0x8
#define LSDMA_PIO_CONTROL__DST_COMP_EN__SHIFT                                                                 0xa
#define LSDMA_PIO_CONTROL__SRC_GPA__SHIFT                                                                     0x14
#define LSDMA_PIO_CONTROL__SRC_SYS__SHIFT                                                                     0x15
#define LSDMA_PIO_CONTROL__SRC_SNOOP__SHIFT                                                                   0x17
#define LSDMA_PIO_CONTROL__SRC_REUSE_HINT__SHIFT                                                              0x18
#define LSDMA_PIO_CONTROL__SRC_COMP_EN__SHIFT                                                                 0x1a
#define LSDMA_PIO_CONTROL__VMID_MASK                                                                          0x0000000FL
#define LSDMA_PIO_CONTROL__DST_GPA_MASK                                                                       0x00000010L
#define LSDMA_PIO_CONTROL__DST_SYS_MASK                                                                       0x00000020L
#define LSDMA_PIO_CONTROL__DST_GCC_MASK                                                                       0x00000040L
#define LSDMA_PIO_CONTROL__DST_SNOOP_MASK                                                                     0x00000080L
#define LSDMA_PIO_CONTROL__DST_REUSE_HINT_MASK                                                                0x00000300L
#define LSDMA_PIO_CONTROL__DST_COMP_EN_MASK                                                                   0x00000400L
#define LSDMA_PIO_CONTROL__SRC_GPA_MASK                                                                       0x00100000L
#define LSDMA_PIO_CONTROL__SRC_SYS_MASK                                                                       0x00200000L
#define LSDMA_PIO_CONTROL__SRC_SNOOP_MASK                                                                     0x00800000L
#define LSDMA_PIO_CONTROL__SRC_REUSE_HINT_MASK                                                                0x03000000L
#define LSDMA_PIO_CONTROL__SRC_COMP_EN_MASK                                                                   0x04000000L
//LSDMA_PIO_STATUS
#define LSDMA_PIO_STATUS__CMD_IN_FIFO__SHIFT                                                                  0x0
#define LSDMA_PIO_STATUS__CMD_PROCESSING__SHIFT                                                               0x3
#define LSDMA_PIO_STATUS__ERROR_INVALID_ADDR__SHIFT                                                           0x8
#define LSDMA_PIO_STATUS__ERROR_ZERO_COUNT__SHIFT                                                             0x9
#define LSDMA_PIO_STATUS__ERROR_DRAM_ECC__SHIFT                                                               0xa
#define LSDMA_PIO_STATUS__ERROR_SRAM_ECC__SHIFT                                                               0xb
#define LSDMA_PIO_STATUS__ERROR_WRRET_NACK_GEN_ERR__SHIFT                                                     0xf
#define LSDMA_PIO_STATUS__ERROR_RDRET_NACK_GEN_ERR__SHIFT                                                     0x10
#define LSDMA_PIO_STATUS__ERROR_WRRET_NACK_PRT__SHIFT                                                         0x11
#define LSDMA_PIO_STATUS__ERROR_RDRET_NACK_PRT__SHIFT                                                         0x12
#define LSDMA_PIO_STATUS__PIO_FIFO_EMPTY__SHIFT                                                               0x1c
#define LSDMA_PIO_STATUS__PIO_FIFO_FULL__SHIFT                                                                0x1d
#define LSDMA_PIO_STATUS__PIO_IDLE__SHIFT                                                                     0x1f
#define LSDMA_PIO_STATUS__CMD_IN_FIFO_MASK                                                                    0x00000007L
#define LSDMA_PIO_STATUS__CMD_PROCESSING_MASK                                                                 0x000000F8L
#define LSDMA_PIO_STATUS__ERROR_INVALID_ADDR_MASK                                                             0x00000100L
#define LSDMA_PIO_STATUS__ERROR_ZERO_COUNT_MASK                                                               0x00000200L
#define LSDMA_PIO_STATUS__ERROR_DRAM_ECC_MASK                                                                 0x00000400L
#define LSDMA_PIO_STATUS__ERROR_SRAM_ECC_MASK                                                                 0x00000800L
#define LSDMA_PIO_STATUS__ERROR_WRRET_NACK_GEN_ERR_MASK                                                       0x00008000L
#define LSDMA_PIO_STATUS__ERROR_RDRET_NACK_GEN_ERR_MASK                                                       0x00010000L
#define LSDMA_PIO_STATUS__ERROR_WRRET_NACK_PRT_MASK                                                           0x00020000L
#define LSDMA_PIO_STATUS__ERROR_RDRET_NACK_PRT_MASK                                                           0x00040000L
#define LSDMA_PIO_STATUS__PIO_FIFO_EMPTY_MASK                                                                 0x10000000L
#define LSDMA_PIO_STATUS__PIO_FIFO_FULL_MASK                                                                  0x20000000L
#define LSDMA_PIO_STATUS__PIO_IDLE_MASK                                                                       0x80000000L
//LSDMA_PF_PIO_STATUS
#define LSDMA_PF_PIO_STATUS__CMD_IN_FIFO__SHIFT                                                               0x0
#define LSDMA_PF_PIO_STATUS__CMD_PROCESSING__SHIFT                                                            0x3
#define LSDMA_PF_PIO_STATUS__ERROR_INVALID_ADDR__SHIFT                                                        0x8
#define LSDMA_PF_PIO_STATUS__ERROR_ZERO_COUNT__SHIFT                                                          0x9
#define LSDMA_PF_PIO_STATUS__ERROR_DRAM_ECC__SHIFT                                                            0xa
#define LSDMA_PF_PIO_STATUS__ERROR_SRAM_ECC__SHIFT                                                            0xb
#define LSDMA_PF_PIO_STATUS__ERROR_WRRET_NACK_GEN_ERR__SHIFT                                                  0xf
#define LSDMA_PF_PIO_STATUS__ERROR_RDRET_NACK_GEN_ERR__SHIFT                                                  0x10
#define LSDMA_PF_PIO_STATUS__ERROR_WRRET_NACK_PRT__SHIFT                                                      0x11
#define LSDMA_PF_PIO_STATUS__ERROR_RDRET_NACK_PRT__SHIFT                                                      0x12
#define LSDMA_PF_PIO_STATUS__PIO_FIFO_EMPTY__SHIFT                                                            0x1c
#define LSDMA_PF_PIO_STATUS__PIO_FIFO_FULL__SHIFT                                                             0x1d
#define LSDMA_PF_PIO_STATUS__PIO_IDLE__SHIFT                                                                  0x1f
#define LSDMA_PF_PIO_STATUS__CMD_IN_FIFO_MASK                                                                 0x00000007L
#define LSDMA_PF_PIO_STATUS__CMD_PROCESSING_MASK                                                              0x000000F8L
#define LSDMA_PF_PIO_STATUS__ERROR_ZERO_COUNT_MASK                                                            0x00000200L
#define LSDMA_PF_PIO_STATUS__ERROR_DRAM_ECC_MASK                                                              0x00000400L
#define LSDMA_PF_PIO_STATUS__ERROR_SRAM_ECC_MASK                                                              0x00000800L
#define LSDMA_PF_PIO_STATUS__ERROR_WRRET_NACK_GEN_ERR_MASK                                                    0x00008000L
#define LSDMA_PF_PIO_STATUS__ERROR_RDRET_NACK_GEN_ERR_MASK                                                    0x00010000L
#define LSDMA_PF_PIO_STATUS__ERROR_WRRET_NACK_PRT_MASK                                                        0x00020000L
#define LSDMA_PF_PIO_STATUS__ERROR_RDRET_NACK_PRT_MASK                                                        0x00040000L
#define LSDMA_PF_PIO_STATUS__PIO_FIFO_EMPTY_MASK                                                              0x10000000L
#define LSDMA_PF_PIO_STATUS__PIO_FIFO_FULL_MASK                                                               0x20000000L
#define LSDMA_PF_PIO_STATUS__PIO_IDLE_MASK                                                                    0x80000000L
//LSDMA_QUEUE0_RB_CNTL
#define LSDMA_QUEUE0_RB_CNTL__RB_ENABLE__SHIFT                                                                0x0
#define LSDMA_QUEUE0_RB_CNTL__RB_SIZE__SHIFT                                                                  0x1
#define LSDMA_QUEUE0_RB_CNTL__RB_SWAP_ENABLE__SHIFT                                                           0x9
#define LSDMA_QUEUE0_RB_CNTL__RPTR_WRITEBACK_ENABLE__SHIFT                                                    0xc
#define LSDMA_QUEUE0_RB_CNTL__RPTR_WRITEBACK_SWAP_ENABLE__SHIFT                                               0xd
#define LSDMA_QUEUE0_RB_CNTL__RPTR_WRITEBACK_TIMER__SHIFT                                                     0x10
#define LSDMA_QUEUE0_RB_CNTL__RB_PRIV__SHIFT                                                                  0x17
#define LSDMA_QUEUE0_RB_CNTL__RB_VMID__SHIFT                                                                  0x18
#define LSDMA_QUEUE0_RB_CNTL__RB_ENABLE_MASK                                                                  0x00000001L
#define LSDMA_QUEUE0_RB_CNTL__RB_SIZE_MASK                                                                    0x0000003EL
#define LSDMA_QUEUE0_RB_CNTL__RB_SWAP_ENABLE_MASK                                                             0x00000200L
#define LSDMA_QUEUE0_RB_CNTL__RPTR_WRITEBACK_ENABLE_MASK                                                      0x00001000L
#define LSDMA_QUEUE0_RB_CNTL__RPTR_WRITEBACK_SWAP_ENABLE_MASK                                                 0x00002000L
#define LSDMA_QUEUE0_RB_CNTL__RPTR_WRITEBACK_TIMER_MASK                                                       0x001F0000L
#define LSDMA_QUEUE0_RB_CNTL__RB_VMID_MASK                                                                    0x0F000000L
//LSDMA_QUEUE0_RB_BASE
#define LSDMA_QUEUE0_RB_BASE__ADDR__SHIFT                                                                     0x0
#define LSDMA_QUEUE0_RB_BASE__ADDR_MASK                                                                       0xFFFFFFFFL
//LSDMA_QUEUE0_RB_BASE_HI
#define LSDMA_QUEUE0_RB_BASE_HI__ADDR__SHIFT                                                                  0x0
#define LSDMA_QUEUE0_RB_BASE_HI__ADDR_MASK                                                                    0x00FFFFFFL
//LSDMA_QUEUE0_RB_RPTR
#define LSDMA_QUEUE0_RB_RPTR__OFFSET__SHIFT                                                                   0x0
#define LSDMA_QUEUE0_RB_RPTR__OFFSET_MASK                                                                     0xFFFFFFFFL
//LSDMA_QUEUE0_RB_RPTR_HI
#define LSDMA_QUEUE0_RB_RPTR_HI__OFFSET__SHIFT                                                                0x0
#define LSDMA_QUEUE0_RB_RPTR_HI__OFFSET_MASK                                                                  0xFFFFFFFFL
//LSDMA_QUEUE0_RB_WPTR
#define LSDMA_QUEUE0_RB_WPTR__OFFSET__SHIFT                                                                   0x0
#define LSDMA_QUEUE0_RB_WPTR__OFFSET_MASK                                                                     0xFFFFFFFFL
//LSDMA_QUEUE0_RB_WPTR_HI
#define LSDMA_QUEUE0_RB_WPTR_HI__OFFSET__SHIFT                                                                0x0
#define LSDMA_QUEUE0_RB_WPTR_HI__OFFSET_MASK                                                                  0xFFFFFFFFL
//LSDMA_QUEUE0_RB_WPTR_POLL_CNTL
#define LSDMA_QUEUE0_RB_WPTR_POLL_CNTL__ENABLE__SHIFT                                                         0x0
#define LSDMA_QUEUE0_RB_WPTR_POLL_CNTL__SWAP_ENABLE__SHIFT                                                    0x1
#define LSDMA_QUEUE0_RB_WPTR_POLL_CNTL__F32_POLL_ENABLE__SHIFT                                                0x2
#define LSDMA_QUEUE0_RB_WPTR_POLL_CNTL__FREQUENCY__SHIFT                                                      0x4
#define LSDMA_QUEUE0_RB_WPTR_POLL_CNTL__IDLE_POLL_COUNT__SHIFT                                                0x10
#define LSDMA_QUEUE0_RB_WPTR_POLL_CNTL__ENABLE_MASK                                                           0x00000001L
#define LSDMA_QUEUE0_RB_WPTR_POLL_CNTL__SWAP_ENABLE_MASK                                                      0x00000002L
#define LSDMA_QUEUE0_RB_WPTR_POLL_CNTL__F32_POLL_ENABLE_MASK                                                  0x00000004L
#define LSDMA_QUEUE0_RB_WPTR_POLL_CNTL__FREQUENCY_MASK                                                        0x0000FFF0L
#define LSDMA_QUEUE0_RB_WPTR_POLL_CNTL__IDLE_POLL_COUNT_MASK                                                  0xFFFF0000L
//LSDMA_QUEUE0_RB_WPTR_POLL_ADDR_HI
#define LSDMA_QUEUE0_RB_WPTR_POLL_ADDR_HI__ADDR__SHIFT                                                        0x0
#define LSDMA_QUEUE0_RB_WPTR_POLL_ADDR_HI__ADDR_MASK                                                          0xFFFFFFFFL
//LSDMA_QUEUE0_RB_WPTR_POLL_ADDR_LO
#define LSDMA_QUEUE0_RB_WPTR_POLL_ADDR_LO__ADDR__SHIFT                                                        0x2
#define LSDMA_QUEUE0_RB_WPTR_POLL_ADDR_LO__ADDR_MASK                                                          0xFFFFFFFCL
//LSDMA_QUEUE0_RB_RPTR_ADDR_HI
#define LSDMA_QUEUE0_RB_RPTR_ADDR_HI__ADDR__SHIFT                                                             0x0
#define LSDMA_QUEUE0_RB_RPTR_ADDR_HI__ADDR_MASK                                                               0xFFFFFFFFL
//LSDMA_QUEUE0_RB_RPTR_ADDR_LO
#define LSDMA_QUEUE0_RB_RPTR_ADDR_LO__RPTR_WB_IDLE__SHIFT                                                     0x0
#define LSDMA_QUEUE0_RB_RPTR_ADDR_LO__ADDR__SHIFT                                                             0x2
#define LSDMA_QUEUE0_RB_RPTR_ADDR_LO__RPTR_WB_IDLE_MASK                                                       0x00000001L
#define LSDMA_QUEUE0_RB_RPTR_ADDR_LO__ADDR_MASK                                                               0xFFFFFFFCL
//LSDMA_QUEUE0_IB_CNTL
#define LSDMA_QUEUE0_IB_CNTL__IB_ENABLE__SHIFT                                                                0x0
#define LSDMA_QUEUE0_IB_CNTL__IB_SWAP_ENABLE__SHIFT                                                           0x4
#define LSDMA_QUEUE0_IB_CNTL__SWITCH_INSIDE_IB__SHIFT                                                         0x8
#define LSDMA_QUEUE0_IB_CNTL__CMD_VMID__SHIFT                                                                 0x10
#define LSDMA_QUEUE0_IB_CNTL__IB_PRIV__SHIFT                                                                  0x1f
#define LSDMA_QUEUE0_IB_CNTL__IB_ENABLE_MASK                                                                  0x00000001L
#define LSDMA_QUEUE0_IB_CNTL__IB_SWAP_ENABLE_MASK                                                             0x00000010L
#define LSDMA_QUEUE0_IB_CNTL__SWITCH_INSIDE_IB_MASK                                                           0x00000100L
#define LSDMA_QUEUE0_IB_CNTL__CMD_VMID_MASK                                                                   0x000F0000L
//LSDMA_QUEUE0_IB_RPTR
#define LSDMA_QUEUE0_IB_RPTR__OFFSET__SHIFT                                                                   0x2
#define LSDMA_QUEUE0_IB_RPTR__OFFSET_MASK                                                                     0x003FFFFCL
//LSDMA_QUEUE0_IB_OFFSET
#define LSDMA_QUEUE0_IB_OFFSET__OFFSET__SHIFT                                                                 0x2
#define LSDMA_QUEUE0_IB_OFFSET__OFFSET_MASK                                                                   0x003FFFFCL
//LSDMA_QUEUE0_IB_BASE_LO
#define LSDMA_QUEUE0_IB_BASE_LO__ADDR__SHIFT                                                                  0x5
#define LSDMA_QUEUE0_IB_BASE_LO__ADDR_MASK                                                                    0xFFFFFFE0L
//LSDMA_QUEUE0_IB_BASE_HI
#define LSDMA_QUEUE0_IB_BASE_HI__ADDR__SHIFT                                                                  0x0
#define LSDMA_QUEUE0_IB_BASE_HI__ADDR_MASK                                                                    0xFFFFFFFFL
//LSDMA_QUEUE0_IB_SIZE
#define LSDMA_QUEUE0_IB_SIZE__SIZE__SHIFT                                                                     0x0
#define LSDMA_QUEUE0_IB_SIZE__SIZE_MASK                                                                       0x000FFFFFL
//LSDMA_QUEUE0_SKIP_CNTL
#define LSDMA_QUEUE0_SKIP_CNTL__SKIP_COUNT__SHIFT                                                             0x0
#define LSDMA_QUEUE0_SKIP_CNTL__SKIP_COUNT_MASK                                                               0x000FFFFFL
//LSDMA_QUEUE0_CSA_ADDR_LO
#define LSDMA_QUEUE0_CSA_ADDR_LO__ADDR__SHIFT                                                                 0x2
#define LSDMA_QUEUE0_CSA_ADDR_LO__ADDR_MASK                                                                   0xFFFFFFFCL
//LSDMA_QUEUE0_CSA_ADDR_HI
#define LSDMA_QUEUE0_CSA_ADDR_HI__ADDR__SHIFT                                                                 0x0
#define LSDMA_QUEUE0_CSA_ADDR_HI__ADDR_MASK                                                                   0xFFFFFFFFL
//LSDMA_QUEUE0_RB_AQL_CNTL
#define LSDMA_QUEUE0_RB_AQL_CNTL__AQL_ENABLE__SHIFT                                                           0x0
#define LSDMA_QUEUE0_RB_AQL_CNTL__AQL_PACKET_SIZE__SHIFT                                                      0x1
#define LSDMA_QUEUE0_RB_AQL_CNTL__PACKET_STEP__SHIFT                                                          0x8
#define LSDMA_QUEUE0_RB_AQL_CNTL__AQL_ENABLE_MASK                                                             0x00000001L
#define LSDMA_QUEUE0_RB_AQL_CNTL__AQL_PACKET_SIZE_MASK                                                        0x000000FEL
#define LSDMA_QUEUE0_RB_AQL_CNTL__PACKET_STEP_MASK                                                            0x0000FF00L
//LSDMA_QUEUE0_MINOR_PTR_UPDATE
#define LSDMA_QUEUE0_MINOR_PTR_UPDATE__ENABLE__SHIFT                                                          0x0
#define LSDMA_QUEUE0_MINOR_PTR_UPDATE__ENABLE_MASK                                                            0x00000001L
//LSDMA_QUEUE0_CNTL
#define LSDMA_QUEUE0_CNTL__QUANTUM__SHIFT                                                                     0x0
#define LSDMA_QUEUE0_CNTL__QUANTUM_MASK                                                                       0x000000FFL
//LSDMA_QUEUE0_RB_PREEMPT
#define LSDMA_QUEUE0_RB_PREEMPT__PREEMPT_REQ__SHIFT                                                           0x0
#define LSDMA_QUEUE0_RB_PREEMPT__PREEMPT_REQ_MASK                                                             0x00000001L
//LSDMA_QUEUE0_IB_SUB_REMAIN
#define LSDMA_QUEUE0_IB_SUB_REMAIN__SIZE__SHIFT                                                               0x0
#define LSDMA_QUEUE0_IB_SUB_REMAIN__SIZE_MASK                                                                 0x000FFFFFL
//LSDMA_QUEUE0_PREEMPT
#define LSDMA_QUEUE0_PREEMPT__IB_PREEMPT__SHIFT                                                               0x0
#define LSDMA_QUEUE0_PREEMPT__IB_PREEMPT_MASK                                                                 0x00000001L
//LSDMA_QUEUE0_CONTEXT_STATUS
#define LSDMA_QUEUE0_CONTEXT_STATUS__SELECTED__SHIFT                                                          0x0
#define LSDMA_QUEUE0_CONTEXT_STATUS__IDLE__SHIFT                                                              0x2
#define LSDMA_QUEUE0_CONTEXT_STATUS__EXPIRED__SHIFT                                                           0x3
#define LSDMA_QUEUE0_CONTEXT_STATUS__EXCEPTION__SHIFT                                                         0x4
#define LSDMA_QUEUE0_CONTEXT_STATUS__CTXSW_ABLE__SHIFT                                                        0x7
#define LSDMA_QUEUE0_CONTEXT_STATUS__CTXSW_READY__SHIFT                                                       0x8
#define LSDMA_QUEUE0_CONTEXT_STATUS__PREEMPTED__SHIFT                                                         0x9
#define LSDMA_QUEUE0_CONTEXT_STATUS__PREEMPT_DISABLE__SHIFT                                                   0xa
#define LSDMA_QUEUE0_CONTEXT_STATUS__SELECTED_MASK                                                            0x00000001L
#define LSDMA_QUEUE0_CONTEXT_STATUS__IDLE_MASK                                                                0x00000004L
#define LSDMA_QUEUE0_CONTEXT_STATUS__EXPIRED_MASK                                                             0x00000008L
#define LSDMA_QUEUE0_CONTEXT_STATUS__EXCEPTION_MASK                                                           0x00000070L
#define LSDMA_QUEUE0_CONTEXT_STATUS__CTXSW_ABLE_MASK                                                          0x00000080L
#define LSDMA_QUEUE0_CONTEXT_STATUS__CTXSW_READY_MASK                                                         0x00000100L
#define LSDMA_QUEUE0_CONTEXT_STATUS__PREEMPTED_MASK                                                           0x00000200L
#define LSDMA_QUEUE0_CONTEXT_STATUS__PREEMPT_DISABLE_MASK                                                     0x00000400L
//LSDMA_QUEUE0_STATUS
#define LSDMA_QUEUE0_STATUS__WPTR_UPDATE_FAIL_COUNT__SHIFT                                                    0x0
#define LSDMA_QUEUE0_STATUS__WPTR_UPDATE_PENDING__SHIFT                                                       0x8
#define LSDMA_QUEUE0_STATUS__WPTR_UPDATE_FAIL_COUNT_MASK                                                      0x000000FFL
#define LSDMA_QUEUE0_STATUS__WPTR_UPDATE_PENDING_MASK                                                         0x00000100L
//LSDMA_QUEUE0_DOORBELL
#define LSDMA_QUEUE0_DOORBELL__ENABLE__SHIFT                                                                  0x1c
#define LSDMA_QUEUE0_DOORBELL__CAPTURED__SHIFT                                                                0x1e
#define LSDMA_QUEUE0_DOORBELL__ENABLE_MASK                                                                    0x10000000L
#define LSDMA_QUEUE0_DOORBELL__CAPTURED_MASK                                                                  0x40000000L
//LSDMA_QUEUE0_DOORBELL_OFFSET
#define LSDMA_QUEUE0_DOORBELL_OFFSET__OFFSET__SHIFT                                                           0x2
#define LSDMA_QUEUE0_DOORBELL_OFFSET__OFFSET_MASK                                                             0x0FFFFFFCL
//LSDMA_QUEUE0_DOORBELL_LOG
#define LSDMA_QUEUE0_DOORBELL_LOG__BE_ERROR__SHIFT                                                            0x0
#define LSDMA_QUEUE0_DOORBELL_LOG__DATA__SHIFT                                                                0x2
#define LSDMA_QUEUE0_DOORBELL_LOG__BE_ERROR_MASK                                                              0x00000001L
#define LSDMA_QUEUE0_DOORBELL_LOG__DATA_MASK                                                                  0xFFFFFFFCL
//LSDMA_QUEUE0_WATERMARK
#define LSDMA_QUEUE0_WATERMARK__RD_OUTSTANDING__SHIFT                                                         0x0
#define LSDMA_QUEUE0_WATERMARK__WR_OUTSTANDING__SHIFT                                                         0x10
#define LSDMA_QUEUE0_WATERMARK__RD_OUTSTANDING_MASK                                                           0x00000FFFL
#define LSDMA_QUEUE0_WATERMARK__WR_OUTSTANDING_MASK                                                           0x03FF0000L
//LSDMA_QUEUE0_DUMMY0
#define LSDMA_QUEUE0_DUMMY0__DUMMY__SHIFT                                                                     0x0
#define LSDMA_QUEUE0_DUMMY0__DUMMY_MASK                                                                       0xFFFFFFFFL
//LSDMA_QUEUE0_DUMMY1
#define LSDMA_QUEUE0_DUMMY1__DUMMY__SHIFT                                                                     0x0
#define LSDMA_QUEUE0_DUMMY1__DUMMY_MASK                                                                       0xFFFFFFFFL
//LSDMA_QUEUE0_DUMMY2
#define LSDMA_QUEUE0_DUMMY2__DUMMY__SHIFT                                                                     0x0
#define LSDMA_QUEUE0_DUMMY2__DUMMY_MASK                                                                       0xFFFFFFFFL
//LSDMA_QUEUE0_MIDCMD_DATA0
#define LSDMA_QUEUE0_MIDCMD_DATA0__DATA0__SHIFT                                                               0x0
#define LSDMA_QUEUE0_MIDCMD_DATA0__DATA0_MASK                                                                 0xFFFFFFFFL
//LSDMA_QUEUE0_MIDCMD_DATA1
#define LSDMA_QUEUE0_MIDCMD_DATA1__DATA1__SHIFT                                                               0x0
#define LSDMA_QUEUE0_MIDCMD_DATA1__DATA1_MASK                                                                 0xFFFFFFFFL
//LSDMA_QUEUE0_MIDCMD_DATA2
#define LSDMA_QUEUE0_MIDCMD_DATA2__DATA2__SHIFT                                                               0x0
#define LSDMA_QUEUE0_MIDCMD_DATA2__DATA2_MASK                                                                 0xFFFFFFFFL
//LSDMA_QUEUE0_MIDCMD_DATA3
#define LSDMA_QUEUE0_MIDCMD_DATA3__DATA3__SHIFT                                                               0x0
#define LSDMA_QUEUE0_MIDCMD_DATA3__DATA3_MASK                                                                 0xFFFFFFFFL
//LSDMA_QUEUE0_MIDCMD_DATA4
#define LSDMA_QUEUE0_MIDCMD_DATA4__DATA4__SHIFT                                                               0x0
#define LSDMA_QUEUE0_MIDCMD_DATA4__DATA4_MASK                                                                 0xFFFFFFFFL
//LSDMA_QUEUE0_MIDCMD_DATA5
#define LSDMA_QUEUE0_MIDCMD_DATA5__DATA5__SHIFT                                                               0x0
#define LSDMA_QUEUE0_MIDCMD_DATA5__DATA5_MASK                                                                 0xFFFFFFFFL
//LSDMA_QUEUE0_MIDCMD_DATA6
#define LSDMA_QUEUE0_MIDCMD_DATA6__DATA6__SHIFT                                                               0x0
#define LSDMA_QUEUE0_MIDCMD_DATA6__DATA6_MASK                                                                 0xFFFFFFFFL
//LSDMA_QUEUE0_MIDCMD_DATA7
#define LSDMA_QUEUE0_MIDCMD_DATA7__DATA7__SHIFT                                                               0x0
#define LSDMA_QUEUE0_MIDCMD_DATA7__DATA7_MASK                                                                 0xFFFFFFFFL
//LSDMA_QUEUE0_MIDCMD_DATA8
#define LSDMA_QUEUE0_MIDCMD_DATA8__DATA8__SHIFT                                                               0x0
#define LSDMA_QUEUE0_MIDCMD_DATA8__DATA8_MASK                                                                 0xFFFFFFFFL
//LSDMA_QUEUE0_MIDCMD_DATA9
#define LSDMA_QUEUE0_MIDCMD_DATA9__DATA9__SHIFT                                                               0x0
#define LSDMA_QUEUE0_MIDCMD_DATA9__DATA9_MASK                                                                 0xFFFFFFFFL
//LSDMA_QUEUE0_MIDCMD_DATA10
#define LSDMA_QUEUE0_MIDCMD_DATA10__DATA10__SHIFT                                                             0x0
#define LSDMA_QUEUE0_MIDCMD_DATA10__DATA10_MASK                                                               0xFFFFFFFFL
//LSDMA_QUEUE0_MIDCMD_CNTL
#define LSDMA_QUEUE0_MIDCMD_CNTL__DATA_VALID__SHIFT                                                           0x0
#define LSDMA_QUEUE0_MIDCMD_CNTL__COPY_MODE__SHIFT                                                            0x1
#define LSDMA_QUEUE0_MIDCMD_CNTL__SPLIT_STATE__SHIFT                                                          0x4
#define LSDMA_QUEUE0_MIDCMD_CNTL__ALLOW_PREEMPT__SHIFT                                                        0x8
#define LSDMA_QUEUE0_MIDCMD_CNTL__DATA_VALID_MASK                                                             0x00000001L
#define LSDMA_QUEUE0_MIDCMD_CNTL__COPY_MODE_MASK                                                              0x00000002L
#define LSDMA_QUEUE0_MIDCMD_CNTL__SPLIT_STATE_MASK                                                            0x000000F0L
#define LSDMA_QUEUE0_MIDCMD_CNTL__ALLOW_PREEMPT_MASK                                                          0x00000100L
//LSDMA_QUEUE1_RB_CNTL
#define LSDMA_QUEUE1_RB_CNTL__RB_ENABLE__SHIFT                                                                0x0
#define LSDMA_QUEUE1_RB_CNTL__RB_SIZE__SHIFT                                                                  0x1
#define LSDMA_QUEUE1_RB_CNTL__RB_SWAP_ENABLE__SHIFT                                                           0x9
#define LSDMA_QUEUE1_RB_CNTL__RPTR_WRITEBACK_ENABLE__SHIFT                                                    0xc
#define LSDMA_QUEUE1_RB_CNTL__RPTR_WRITEBACK_SWAP_ENABLE__SHIFT                                               0xd
#define LSDMA_QUEUE1_RB_CNTL__RPTR_WRITEBACK_TIMER__SHIFT                                                     0x10
#define LSDMA_QUEUE1_RB_CNTL__RB_PRIV__SHIFT                                                                  0x17
#define LSDMA_QUEUE1_RB_CNTL__RB_VMID__SHIFT                                                                  0x18
#define LSDMA_QUEUE1_RB_CNTL__RB_ENABLE_MASK                                                                  0x00000001L
#define LSDMA_QUEUE1_RB_CNTL__RB_SIZE_MASK                                                                    0x0000003EL
#define LSDMA_QUEUE1_RB_CNTL__RB_SWAP_ENABLE_MASK                                                             0x00000200L
#define LSDMA_QUEUE1_RB_CNTL__RPTR_WRITEBACK_ENABLE_MASK                                                      0x00001000L
#define LSDMA_QUEUE1_RB_CNTL__RPTR_WRITEBACK_SWAP_ENABLE_MASK                                                 0x00002000L
#define LSDMA_QUEUE1_RB_CNTL__RPTR_WRITEBACK_TIMER_MASK                                                       0x001F0000L
#define LSDMA_QUEUE1_RB_CNTL__RB_VMID_MASK                                                                    0x0F000000L
//LSDMA_QUEUE1_RB_BASE
#define LSDMA_QUEUE1_RB_BASE__ADDR__SHIFT                                                                     0x0
#define LSDMA_QUEUE1_RB_BASE__ADDR_MASK                                                                       0xFFFFFFFFL
//LSDMA_QUEUE1_RB_BASE_HI
#define LSDMA_QUEUE1_RB_BASE_HI__ADDR__SHIFT                                                                  0x0
#define LSDMA_QUEUE1_RB_BASE_HI__ADDR_MASK                                                                    0x00FFFFFFL
//LSDMA_QUEUE1_RB_RPTR
#define LSDMA_QUEUE1_RB_RPTR__OFFSET__SHIFT                                                                   0x0
#define LSDMA_QUEUE1_RB_RPTR__OFFSET_MASK                                                                     0xFFFFFFFFL
//LSDMA_QUEUE1_RB_RPTR_HI
#define LSDMA_QUEUE1_RB_RPTR_HI__OFFSET__SHIFT                                                                0x0
#define LSDMA_QUEUE1_RB_RPTR_HI__OFFSET_MASK                                                                  0xFFFFFFFFL
//LSDMA_QUEUE1_RB_WPTR
#define LSDMA_QUEUE1_RB_WPTR__OFFSET__SHIFT                                                                   0x0
#define LSDMA_QUEUE1_RB_WPTR__OFFSET_MASK                                                                     0xFFFFFFFFL
//LSDMA_QUEUE1_RB_WPTR_HI
#define LSDMA_QUEUE1_RB_WPTR_HI__OFFSET__SHIFT                                                                0x0
#define LSDMA_QUEUE1_RB_WPTR_HI__OFFSET_MASK                                                                  0xFFFFFFFFL
//LSDMA_QUEUE1_RB_WPTR_POLL_CNTL
#define LSDMA_QUEUE1_RB_WPTR_POLL_CNTL__ENABLE__SHIFT                                                         0x0
#define LSDMA_QUEUE1_RB_WPTR_POLL_CNTL__SWAP_ENABLE__SHIFT                                                    0x1
#define LSDMA_QUEUE1_RB_WPTR_POLL_CNTL__F32_POLL_ENABLE__SHIFT                                                0x2
#define LSDMA_QUEUE1_RB_WPTR_POLL_CNTL__FREQUENCY__SHIFT                                                      0x4
#define LSDMA_QUEUE1_RB_WPTR_POLL_CNTL__IDLE_POLL_COUNT__SHIFT                                                0x10
#define LSDMA_QUEUE1_RB_WPTR_POLL_CNTL__ENABLE_MASK                                                           0x00000001L
#define LSDMA_QUEUE1_RB_WPTR_POLL_CNTL__SWAP_ENABLE_MASK                                                      0x00000002L
#define LSDMA_QUEUE1_RB_WPTR_POLL_CNTL__F32_POLL_ENABLE_MASK                                                  0x00000004L
#define LSDMA_QUEUE1_RB_WPTR_POLL_CNTL__FREQUENCY_MASK                                                        0x0000FFF0L
#define LSDMA_QUEUE1_RB_WPTR_POLL_CNTL__IDLE_POLL_COUNT_MASK                                                  0xFFFF0000L
//LSDMA_QUEUE1_RB_WPTR_POLL_ADDR_HI
#define LSDMA_QUEUE1_RB_WPTR_POLL_ADDR_HI__ADDR__SHIFT                                                        0x0
#define LSDMA_QUEUE1_RB_WPTR_POLL_ADDR_HI__ADDR_MASK                                                          0xFFFFFFFFL
//LSDMA_QUEUE1_RB_WPTR_POLL_ADDR_LO
#define LSDMA_QUEUE1_RB_WPTR_POLL_ADDR_LO__ADDR__SHIFT                                                        0x2
#define LSDMA_QUEUE1_RB_WPTR_POLL_ADDR_LO__ADDR_MASK                                                          0xFFFFFFFCL
//LSDMA_QUEUE1_RB_RPTR_ADDR_HI
#define LSDMA_QUEUE1_RB_RPTR_ADDR_HI__ADDR__SHIFT                                                             0x0
#define LSDMA_QUEUE1_RB_RPTR_ADDR_HI__ADDR_MASK                                                               0xFFFFFFFFL
//LSDMA_QUEUE1_RB_RPTR_ADDR_LO
#define LSDMA_QUEUE1_RB_RPTR_ADDR_LO__RPTR_WB_IDLE__SHIFT                                                     0x0
#define LSDMA_QUEUE1_RB_RPTR_ADDR_LO__ADDR__SHIFT                                                             0x2
#define LSDMA_QUEUE1_RB_RPTR_ADDR_LO__RPTR_WB_IDLE_MASK                                                       0x00000001L
#define LSDMA_QUEUE1_RB_RPTR_ADDR_LO__ADDR_MASK                                                               0xFFFFFFFCL
//LSDMA_QUEUE1_IB_CNTL
#define LSDMA_QUEUE1_IB_CNTL__IB_ENABLE__SHIFT                                                                0x0
#define LSDMA_QUEUE1_IB_CNTL__IB_SWAP_ENABLE__SHIFT                                                           0x4
#define LSDMA_QUEUE1_IB_CNTL__SWITCH_INSIDE_IB__SHIFT                                                         0x8
#define LSDMA_QUEUE1_IB_CNTL__CMD_VMID__SHIFT                                                                 0x10
#define LSDMA_QUEUE1_IB_CNTL__IB_PRIV__SHIFT                                                                  0x1f
#define LSDMA_QUEUE1_IB_CNTL__IB_ENABLE_MASK                                                                  0x00000001L
#define LSDMA_QUEUE1_IB_CNTL__IB_SWAP_ENABLE_MASK                                                             0x00000010L
#define LSDMA_QUEUE1_IB_CNTL__SWITCH_INSIDE_IB_MASK                                                           0x00000100L
#define LSDMA_QUEUE1_IB_CNTL__CMD_VMID_MASK                                                                   0x000F0000L
//LSDMA_QUEUE1_IB_RPTR
#define LSDMA_QUEUE1_IB_RPTR__OFFSET__SHIFT                                                                   0x2
#define LSDMA_QUEUE1_IB_RPTR__OFFSET_MASK                                                                     0x003FFFFCL
//LSDMA_QUEUE1_IB_OFFSET
#define LSDMA_QUEUE1_IB_OFFSET__OFFSET__SHIFT                                                                 0x2
#define LSDMA_QUEUE1_IB_OFFSET__OFFSET_MASK                                                                   0x003FFFFCL
//LSDMA_QUEUE1_IB_BASE_LO
#define LSDMA_QUEUE1_IB_BASE_LO__ADDR__SHIFT                                                                  0x5
#define LSDMA_QUEUE1_IB_BASE_LO__ADDR_MASK                                                                    0xFFFFFFE0L
//LSDMA_QUEUE1_IB_BASE_HI
#define LSDMA_QUEUE1_IB_BASE_HI__ADDR__SHIFT                                                                  0x0
#define LSDMA_QUEUE1_IB_BASE_HI__ADDR_MASK                                                                    0xFFFFFFFFL
//LSDMA_QUEUE1_IB_SIZE
#define LSDMA_QUEUE1_IB_SIZE__SIZE__SHIFT                                                                     0x0
#define LSDMA_QUEUE1_IB_SIZE__SIZE_MASK                                                                       0x000FFFFFL
//LSDMA_QUEUE1_SKIP_CNTL
#define LSDMA_QUEUE1_SKIP_CNTL__SKIP_COUNT__SHIFT                                                             0x0
#define LSDMA_QUEUE1_SKIP_CNTL__SKIP_COUNT_MASK                                                               0x000FFFFFL
//LSDMA_QUEUE1_CSA_ADDR_LO
#define LSDMA_QUEUE1_CSA_ADDR_LO__ADDR__SHIFT                                                                 0x2
#define LSDMA_QUEUE1_CSA_ADDR_LO__ADDR_MASK                                                                   0xFFFFFFFCL
//LSDMA_QUEUE1_CSA_ADDR_HI
#define LSDMA_QUEUE1_CSA_ADDR_HI__ADDR__SHIFT                                                                 0x0
#define LSDMA_QUEUE1_CSA_ADDR_HI__ADDR_MASK                                                                   0xFFFFFFFFL
//LSDMA_QUEUE1_RB_AQL_CNTL
#define LSDMA_QUEUE1_RB_AQL_CNTL__AQL_ENABLE__SHIFT                                                           0x0
#define LSDMA_QUEUE1_RB_AQL_CNTL__AQL_PACKET_SIZE__SHIFT                                                      0x1
#define LSDMA_QUEUE1_RB_AQL_CNTL__PACKET_STEP__SHIFT                                                          0x8
#define LSDMA_QUEUE1_RB_AQL_CNTL__AQL_ENABLE_MASK                                                             0x00000001L
#define LSDMA_QUEUE1_RB_AQL_CNTL__AQL_PACKET_SIZE_MASK                                                        0x000000FEL
#define LSDMA_QUEUE1_RB_AQL_CNTL__PACKET_STEP_MASK                                                            0x0000FF00L
//LSDMA_QUEUE1_MINOR_PTR_UPDATE
#define LSDMA_QUEUE1_MINOR_PTR_UPDATE__ENABLE__SHIFT                                                          0x0
#define LSDMA_QUEUE1_MINOR_PTR_UPDATE__ENABLE_MASK                                                            0x00000001L
//LSDMA_QUEUE1_CNTL
#define LSDMA_QUEUE1_CNTL__QUANTUM__SHIFT                                                                     0x0
#define LSDMA_QUEUE1_CNTL__QUANTUM_MASK                                                                       0x000000FFL
//LSDMA_QUEUE1_RB_PREEMPT
#define LSDMA_QUEUE1_RB_PREEMPT__PREEMPT_REQ__SHIFT                                                           0x0
#define LSDMA_QUEUE1_RB_PREEMPT__PREEMPT_REQ_MASK                                                             0x00000001L
//LSDMA_QUEUE1_IB_SUB_REMAIN
#define LSDMA_QUEUE1_IB_SUB_REMAIN__SIZE__SHIFT                                                               0x0
#define LSDMA_QUEUE1_IB_SUB_REMAIN__SIZE_MASK                                                                 0x000FFFFFL
//LSDMA_QUEUE1_PREEMPT
#define LSDMA_QUEUE1_PREEMPT__IB_PREEMPT__SHIFT                                                               0x0
#define LSDMA_QUEUE1_PREEMPT__IB_PREEMPT_MASK                                                                 0x00000001L
//LSDMA_QUEUE1_CONTEXT_STATUS
#define LSDMA_QUEUE1_CONTEXT_STATUS__SELECTED__SHIFT                                                          0x0
#define LSDMA_QUEUE1_CONTEXT_STATUS__IDLE__SHIFT                                                              0x2
#define LSDMA_QUEUE1_CONTEXT_STATUS__EXPIRED__SHIFT                                                           0x3
#define LSDMA_QUEUE1_CONTEXT_STATUS__EXCEPTION__SHIFT                                                         0x4
#define LSDMA_QUEUE1_CONTEXT_STATUS__CTXSW_ABLE__SHIFT                                                        0x7
#define LSDMA_QUEUE1_CONTEXT_STATUS__CTXSW_READY__SHIFT                                                       0x8
#define LSDMA_QUEUE1_CONTEXT_STATUS__PREEMPTED__SHIFT                                                         0x9
#define LSDMA_QUEUE1_CONTEXT_STATUS__PREEMPT_DISABLE__SHIFT                                                   0xa
#define LSDMA_QUEUE1_CONTEXT_STATUS__SELECTED_MASK                                                            0x00000001L
#define LSDMA_QUEUE1_CONTEXT_STATUS__IDLE_MASK                                                                0x00000004L
#define LSDMA_QUEUE1_CONTEXT_STATUS__EXPIRED_MASK                                                             0x00000008L
#define LSDMA_QUEUE1_CONTEXT_STATUS__EXCEPTION_MASK                                                           0x00000070L
#define LSDMA_QUEUE1_CONTEXT_STATUS__CTXSW_ABLE_MASK                                                          0x00000080L
#define LSDMA_QUEUE1_CONTEXT_STATUS__CTXSW_READY_MASK                                                         0x00000100L
#define LSDMA_QUEUE1_CONTEXT_STATUS__PREEMPTED_MASK                                                           0x00000200L
#define LSDMA_QUEUE1_CONTEXT_STATUS__PREEMPT_DISABLE_MASK                                                     0x00000400L
//LSDMA_QUEUE1_STATUS
#define LSDMA_QUEUE1_STATUS__WPTR_UPDATE_FAIL_COUNT__SHIFT                                                    0x0
#define LSDMA_QUEUE1_STATUS__WPTR_UPDATE_PENDING__SHIFT                                                       0x8
#define LSDMA_QUEUE1_STATUS__WPTR_UPDATE_FAIL_COUNT_MASK                                                      0x000000FFL
#define LSDMA_QUEUE1_STATUS__WPTR_UPDATE_PENDING_MASK                                                         0x00000100L
//LSDMA_QUEUE1_DOORBELL
#define LSDMA_QUEUE1_DOORBELL__ENABLE__SHIFT                                                                  0x1c
#define LSDMA_QUEUE1_DOORBELL__CAPTURED__SHIFT                                                                0x1e
#define LSDMA_QUEUE1_DOORBELL__ENABLE_MASK                                                                    0x10000000L
#define LSDMA_QUEUE1_DOORBELL__CAPTURED_MASK                                                                  0x40000000L
//LSDMA_QUEUE1_DOORBELL_OFFSET
#define LSDMA_QUEUE1_DOORBELL_OFFSET__OFFSET__SHIFT                                                           0x2
#define LSDMA_QUEUE1_DOORBELL_OFFSET__OFFSET_MASK                                                             0x0FFFFFFCL
//LSDMA_QUEUE1_DOORBELL_LOG
#define LSDMA_QUEUE1_DOORBELL_LOG__BE_ERROR__SHIFT                                                            0x0
#define LSDMA_QUEUE1_DOORBELL_LOG__DATA__SHIFT                                                                0x2
#define LSDMA_QUEUE1_DOORBELL_LOG__BE_ERROR_MASK                                                              0x00000001L
#define LSDMA_QUEUE1_DOORBELL_LOG__DATA_MASK                                                                  0xFFFFFFFCL
//LSDMA_QUEUE1_WATERMARK
#define LSDMA_QUEUE1_WATERMARK__RD_OUTSTANDING__SHIFT                                                         0x0
#define LSDMA_QUEUE1_WATERMARK__WR_OUTSTANDING__SHIFT                                                         0x10
#define LSDMA_QUEUE1_WATERMARK__RD_OUTSTANDING_MASK                                                           0x00000FFFL
#define LSDMA_QUEUE1_WATERMARK__WR_OUTSTANDING_MASK                                                           0x03FF0000L
//LSDMA_QUEUE1_DUMMY0
#define LSDMA_QUEUE1_DUMMY0__DUMMY__SHIFT                                                                     0x0
#define LSDMA_QUEUE1_DUMMY0__DUMMY_MASK                                                                       0xFFFFFFFFL
//LSDMA_QUEUE1_DUMMY1
#define LSDMA_QUEUE1_DUMMY1__DUMMY__SHIFT                                                                     0x0
#define LSDMA_QUEUE1_DUMMY1__DUMMY_MASK                                                                       0xFFFFFFFFL
//LSDMA_QUEUE1_DUMMY2
#define LSDMA_QUEUE1_DUMMY2__DUMMY__SHIFT                                                                     0x0
#define LSDMA_QUEUE1_DUMMY2__DUMMY_MASK                                                                       0xFFFFFFFFL
//LSDMA_QUEUE1_MIDCMD_DATA0
#define LSDMA_QUEUE1_MIDCMD_DATA0__DATA0__SHIFT                                                               0x0
#define LSDMA_QUEUE1_MIDCMD_DATA0__DATA0_MASK                                                                 0xFFFFFFFFL
//LSDMA_QUEUE1_MIDCMD_DATA1
#define LSDMA_QUEUE1_MIDCMD_DATA1__DATA1__SHIFT                                                               0x0
#define LSDMA_QUEUE1_MIDCMD_DATA1__DATA1_MASK                                                                 0xFFFFFFFFL
//LSDMA_QUEUE1_MIDCMD_DATA2
#define LSDMA_QUEUE1_MIDCMD_DATA2__DATA2__SHIFT                                                               0x0
#define LSDMA_QUEUE1_MIDCMD_DATA2__DATA2_MASK                                                                 0xFFFFFFFFL
//LSDMA_QUEUE1_MIDCMD_DATA3
#define LSDMA_QUEUE1_MIDCMD_DATA3__DATA3__SHIFT                                                               0x0
#define LSDMA_QUEUE1_MIDCMD_DATA3__DATA3_MASK                                                                 0xFFFFFFFFL
//LSDMA_QUEUE1_MIDCMD_DATA4
#define LSDMA_QUEUE1_MIDCMD_DATA4__DATA4__SHIFT                                                               0x0
#define LSDMA_QUEUE1_MIDCMD_DATA4__DATA4_MASK                                                                 0xFFFFFFFFL
//LSDMA_QUEUE1_MIDCMD_DATA5
#define LSDMA_QUEUE1_MIDCMD_DATA5__DATA5__SHIFT                                                               0x0
#define LSDMA_QUEUE1_MIDCMD_DATA5__DATA5_MASK                                                                 0xFFFFFFFFL
//LSDMA_QUEUE1_MIDCMD_DATA6
#define LSDMA_QUEUE1_MIDCMD_DATA6__DATA6__SHIFT                                                               0x0
#define LSDMA_QUEUE1_MIDCMD_DATA6__DATA6_MASK                                                                 0xFFFFFFFFL
//LSDMA_QUEUE1_MIDCMD_DATA7
#define LSDMA_QUEUE1_MIDCMD_DATA7__DATA7__SHIFT                                                               0x0
#define LSDMA_QUEUE1_MIDCMD_DATA7__DATA7_MASK                                                                 0xFFFFFFFFL
//LSDMA_QUEUE1_MIDCMD_DATA8
#define LSDMA_QUEUE1_MIDCMD_DATA8__DATA8__SHIFT                                                               0x0
#define LSDMA_QUEUE1_MIDCMD_DATA8__DATA8_MASK                                                                 0xFFFFFFFFL
//LSDMA_QUEUE1_MIDCMD_DATA9
#define LSDMA_QUEUE1_MIDCMD_DATA9__DATA9__SHIFT                                                               0x0
#define LSDMA_QUEUE1_MIDCMD_DATA9__DATA9_MASK                                                                 0xFFFFFFFFL
//LSDMA_QUEUE1_MIDCMD_DATA10
#define LSDMA_QUEUE1_MIDCMD_DATA10__DATA10__SHIFT                                                             0x0
#define LSDMA_QUEUE1_MIDCMD_DATA10__DATA10_MASK                                                               0xFFFFFFFFL
//LSDMA_QUEUE1_MIDCMD_CNTL
#define LSDMA_QUEUE1_MIDCMD_CNTL__DATA_VALID__SHIFT                                                           0x0
#define LSDMA_QUEUE1_MIDCMD_CNTL__COPY_MODE__SHIFT                                                            0x1
#define LSDMA_QUEUE1_MIDCMD_CNTL__SPLIT_STATE__SHIFT                                                          0x4
#define LSDMA_QUEUE1_MIDCMD_CNTL__ALLOW_PREEMPT__SHIFT                                                        0x8
#define LSDMA_QUEUE1_MIDCMD_CNTL__DATA_VALID_MASK                                                             0x00000001L
#define LSDMA_QUEUE1_MIDCMD_CNTL__COPY_MODE_MASK                                                              0x00000002L
#define LSDMA_QUEUE1_MIDCMD_CNTL__SPLIT_STATE_MASK                                                            0x000000F0L
#define LSDMA_QUEUE1_MIDCMD_CNTL__ALLOW_PREEMPT_MASK                                                          0x00000100L

#endif
