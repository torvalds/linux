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
#ifndef _vpe_6_1_0_SH_MASK_HEADER
#define _vpe_6_1_0_SH_MASK_HEADER


// addressBlock: vpe_vpedec
//VPEC_DEC_START
#define VPEC_DEC_START__START__SHIFT                                                                          0x0
#define VPEC_DEC_START__START_MASK                                                                            0xFFFFFFFFL
//VPEC_UCODE_ADDR
#define VPEC_UCODE_ADDR__VALUE__SHIFT                                                                         0x0
#define VPEC_UCODE_ADDR__THID__SHIFT                                                                          0xf
#define VPEC_UCODE_ADDR__VALUE_MASK                                                                           0x00001FFFL
#define VPEC_UCODE_ADDR__THID_MASK                                                                            0x00008000L
//VPEC_UCODE_DATA
#define VPEC_UCODE_DATA__VALUE__SHIFT                                                                         0x0
#define VPEC_UCODE_DATA__VALUE_MASK                                                                           0xFFFFFFFFL
//VPEC_F32_CNTL
#define VPEC_F32_CNTL__HALT__SHIFT                                                                            0x0
#define VPEC_F32_CNTL__TH0_CHECKSUM_CLR__SHIFT                                                                0x8
#define VPEC_F32_CNTL__TH0_RESET__SHIFT                                                                       0x9
#define VPEC_F32_CNTL__TH0_ENABLE__SHIFT                                                                      0xa
#define VPEC_F32_CNTL__TH1_CHECKSUM_CLR__SHIFT                                                                0xc
#define VPEC_F32_CNTL__TH1_RESET__SHIFT                                                                       0xd
#define VPEC_F32_CNTL__TH1_ENABLE__SHIFT                                                                      0xe
#define VPEC_F32_CNTL__TH0_PRIORITY__SHIFT                                                                    0x10
#define VPEC_F32_CNTL__TH1_PRIORITY__SHIFT                                                                    0x18
#define VPEC_F32_CNTL__HALT_MASK                                                                              0x00000001L
#define VPEC_F32_CNTL__TH0_CHECKSUM_CLR_MASK                                                                  0x00000100L
#define VPEC_F32_CNTL__TH0_RESET_MASK                                                                         0x00000200L
#define VPEC_F32_CNTL__TH0_ENABLE_MASK                                                                        0x00000400L
#define VPEC_F32_CNTL__TH1_CHECKSUM_CLR_MASK                                                                  0x00001000L
#define VPEC_F32_CNTL__TH1_RESET_MASK                                                                         0x00002000L
#define VPEC_F32_CNTL__TH1_ENABLE_MASK                                                                        0x00004000L
#define VPEC_F32_CNTL__TH0_PRIORITY_MASK                                                                      0x00FF0000L
#define VPEC_F32_CNTL__TH1_PRIORITY_MASK                                                                      0xFF000000L
//VPEC_VPEP_CTRL
#define VPEC_VPEP_CTRL__VPEP_SOCCLK_EN__SHIFT                                                                 0x0
#define VPEC_VPEP_CTRL__VPEP_SW_RESETB__SHIFT                                                                 0x1
#define VPEC_VPEP_CTRL__RESERVED__SHIFT                                                                       0x2
#define VPEC_VPEP_CTRL__SOFT_OVERRIDE_VPEP_SOCCLK__SHIFT                                                      0x1e
#define VPEC_VPEP_CTRL__SOFT_OVERRIDE_VPECLK__SHIFT                                                           0x1f
#define VPEC_VPEP_CTRL__VPEP_SOCCLK_EN_MASK                                                                   0x00000001L
#define VPEC_VPEP_CTRL__VPEP_SW_RESETB_MASK                                                                   0x00000002L
#define VPEC_VPEP_CTRL__RESERVED_MASK                                                                         0x3FFFFFFCL
#define VPEC_VPEP_CTRL__SOFT_OVERRIDE_VPEP_SOCCLK_MASK                                                        0x40000000L
#define VPEC_VPEP_CTRL__SOFT_OVERRIDE_VPECLK_MASK                                                             0x80000000L
//VPEC_CLK_CTRL
#define VPEC_CLK_CTRL__VPECLK_EN__SHIFT                                                                       0x1
#define VPEC_CLK_CTRL__RESERVED__SHIFT                                                                        0x2
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_IP_PIPE0_CLK__SHIFT                                                      0x18
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_IP_PIPE1_CLK__SHIFT                                                      0x19
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_OP_PIPE0_CLK__SHIFT                                                      0x1a
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_PERF_CLK__SHIFT                                                          0x1b
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_CE_CLK__SHIFT                                                            0x1c
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_F32_CLK__SHIFT                                                           0x1d
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_DYN_CLK__SHIFT                                                           0x1e
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_REG_CLK__SHIFT                                                           0x1f
#define VPEC_CLK_CTRL__VPECLK_EN_MASK                                                                         0x00000002L
#define VPEC_CLK_CTRL__RESERVED_MASK                                                                          0x00FFFFFCL
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_IP_PIPE0_CLK_MASK                                                        0x01000000L
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_IP_PIPE1_CLK_MASK                                                        0x02000000L
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_OP_PIPE0_CLK_MASK                                                        0x04000000L
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_PERF_CLK_MASK                                                            0x08000000L
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_CE_CLK_MASK                                                              0x10000000L
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_F32_CLK_MASK                                                             0x20000000L
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_DYN_CLK_MASK                                                             0x40000000L
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_REG_CLK_MASK                                                             0x80000000L
//VPEC_PG_CNTL
#define VPEC_PG_CNTL__PG_EN__SHIFT                                                                            0x0
#define VPEC_PG_CNTL__PG_HYSTERESIS__SHIFT                                                                    0x1
#define VPEC_PG_CNTL__PG_EN_MASK                                                                              0x00000001L
#define VPEC_PG_CNTL__PG_HYSTERESIS_MASK                                                                      0x0000003EL
//VPEC_POWER_CNTL
#define VPEC_POWER_CNTL__LS_ENABLE__SHIFT                                                                     0x8
#define VPEC_POWER_CNTL__LS_ENABLE_MASK                                                                       0x00000100L
//VPEC_CNTL
#define VPEC_CNTL__TRAP_ENABLE__SHIFT                                                                         0x0
#define VPEC_CNTL__RESERVED_2_2__SHIFT                                                                        0x2
#define VPEC_CNTL__DATA_SWAP__SHIFT                                                                           0x3
#define VPEC_CNTL__FENCE_SWAP_ENABLE__SHIFT                                                                   0x5
#define VPEC_CNTL__MIDCMD_PREEMPT_ENABLE__SHIFT                                                               0x6
#define VPEC_CNTL__TMZ_MIDCMD_PREEMPT_ENABLE__SHIFT                                                           0x8
#define VPEC_CNTL__MIDCMD_EXPIRE_ENABLE__SHIFT                                                                0x9
#define VPEC_CNTL__UMSCH_INT_ENABLE__SHIFT                                                                    0xa
#define VPEC_CNTL__RESERVED_13_11__SHIFT                                                                      0xb
#define VPEC_CNTL__NACK_GEN_ERR_INT_ENABLE__SHIFT                                                             0xe
#define VPEC_CNTL__NACK_PRT_INT_ENABLE__SHIFT                                                                 0xf
#define VPEC_CNTL__RESERVED_16_16__SHIFT                                                                      0x10
#define VPEC_CNTL__MIDCMD_WORLDSWITCH_ENABLE__SHIFT                                                           0x11
#define VPEC_CNTL__RESERVED_19_19__SHIFT                                                                      0x13
#define VPEC_CNTL__ZSTATES_ENABLE__SHIFT                                                                      0x14
#define VPEC_CNTL__ZSTATES_HYSTERESIS__SHIFT                                                                  0x15
#define VPEC_CNTL__CTXEMPTY_INT_ENABLE__SHIFT                                                                 0x1c
#define VPEC_CNTL__FROZEN_INT_ENABLE__SHIFT                                                                   0x1d
#define VPEC_CNTL__IB_PREEMPT_INT_ENABLE__SHIFT                                                               0x1e
#define VPEC_CNTL__RB_PREEMPT_INT_ENABLE__SHIFT                                                               0x1f
#define VPEC_CNTL__TRAP_ENABLE_MASK                                                                           0x00000001L
#define VPEC_CNTL__RESERVED_2_2_MASK                                                                          0x00000004L
#define VPEC_CNTL__DATA_SWAP_MASK                                                                             0x00000018L
#define VPEC_CNTL__FENCE_SWAP_ENABLE_MASK                                                                     0x00000020L
#define VPEC_CNTL__MIDCMD_PREEMPT_ENABLE_MASK                                                                 0x00000040L
#define VPEC_CNTL__TMZ_MIDCMD_PREEMPT_ENABLE_MASK                                                             0x00000100L
#define VPEC_CNTL__MIDCMD_EXPIRE_ENABLE_MASK                                                                  0x00000200L
#define VPEC_CNTL__UMSCH_INT_ENABLE_MASK                                                                      0x00000400L
#define VPEC_CNTL__RESERVED_13_11_MASK                                                                        0x00003800L
#define VPEC_CNTL__NACK_GEN_ERR_INT_ENABLE_MASK                                                               0x00004000L
#define VPEC_CNTL__NACK_PRT_INT_ENABLE_MASK                                                                   0x00008000L
#define VPEC_CNTL__RESERVED_16_16_MASK                                                                        0x00010000L
#define VPEC_CNTL__MIDCMD_WORLDSWITCH_ENABLE_MASK                                                             0x00020000L
#define VPEC_CNTL__RESERVED_19_19_MASK                                                                        0x00080000L
#define VPEC_CNTL__ZSTATES_ENABLE_MASK                                                                        0x00100000L
#define VPEC_CNTL__ZSTATES_HYSTERESIS_MASK                                                                    0x03E00000L
#define VPEC_CNTL__CTXEMPTY_INT_ENABLE_MASK                                                                   0x10000000L
#define VPEC_CNTL__FROZEN_INT_ENABLE_MASK                                                                     0x20000000L
#define VPEC_CNTL__IB_PREEMPT_INT_ENABLE_MASK                                                                 0x40000000L
#define VPEC_CNTL__RB_PREEMPT_INT_ENABLE_MASK                                                                 0x80000000L
//VPEC_CNTL1
#define VPEC_CNTL1__RESERVED_3_1__SHIFT                                                                       0x1
#define VPEC_CNTL1__SRBM_POLL_RETRYING__SHIFT                                                                 0x5
#define VPEC_CNTL1__RESERVED_23_10__SHIFT                                                                     0xa
#define VPEC_CNTL1__CG_STATUS_OUTPUT__SHIFT                                                                   0x18
#define VPEC_CNTL1__SW_FREEZE_ENABLE__SHIFT                                                                   0x19
#define VPEC_CNTL1__VPEP_CONFIG_INVALID_CHECK_ENABLE__SHIFT                                                   0x1a
#define VPEC_CNTL1__RESERVED__SHIFT                                                                           0x1b
#define VPEC_CNTL1__RESERVED_3_1_MASK                                                                         0x0000000EL
#define VPEC_CNTL1__SRBM_POLL_RETRYING_MASK                                                                   0x00000020L
#define VPEC_CNTL1__RESERVED_23_10_MASK                                                                       0x00FFFC00L
#define VPEC_CNTL1__CG_STATUS_OUTPUT_MASK                                                                     0x01000000L
#define VPEC_CNTL1__SW_FREEZE_ENABLE_MASK                                                                     0x02000000L
#define VPEC_CNTL1__VPEP_CONFIG_INVALID_CHECK_ENABLE_MASK                                                     0x04000000L
#define VPEC_CNTL1__RESERVED_MASK                                                                             0xF8000000L
//VPEC_CNTL2
#define VPEC_CNTL2__F32_CMD_PROC_DELAY__SHIFT                                                                 0x0
#define VPEC_CNTL2__F32_SEND_POSTCODE_EN__SHIFT                                                               0x4
#define VPEC_CNTL2__UCODE_BUF_DS_EN__SHIFT                                                                    0x6
#define VPEC_CNTL2__UCODE_SELFLOAD_THREAD_OVERLAP__SHIFT                                                      0x7
#define VPEC_CNTL2__RESERVED_11_8__SHIFT                                                                      0x8
#define VPEC_CNTL2__RESERVED_14_12__SHIFT                                                                     0xc
#define VPEC_CNTL2__RESERVED_15__SHIFT                                                                        0xf
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
#define VPEC_CNTL2__RESERVED_11_8_MASK                                                                        0x00000F00L
#define VPEC_CNTL2__RESERVED_14_12_MASK                                                                       0x00007000L
#define VPEC_CNTL2__RESERVED_15_MASK                                                                          0x00008000L
#define VPEC_CNTL2__RB_FIFO_WATERMARK_MASK                                                                    0x00030000L
#define VPEC_CNTL2__IB_FIFO_WATERMARK_MASK                                                                    0x000C0000L
#define VPEC_CNTL2__RESERVED_22_20_MASK                                                                       0x00700000L
#define VPEC_CNTL2__CH_RD_WATERMARK_MASK                                                                      0x01800000L
#define VPEC_CNTL2__CH_WR_WATERMARK_MASK                                                                      0x3E000000L
#define VPEC_CNTL2__CH_WR_WATERMARK_LSB_MASK                                                                  0x40000000L
//VPEC_GB_ADDR_CONFIG
#define VPEC_GB_ADDR_CONFIG__NUM_PIPES__SHIFT                                                                 0x0
#define VPEC_GB_ADDR_CONFIG__PIPE_INTERLEAVE_SIZE__SHIFT                                                      0x3
#define VPEC_GB_ADDR_CONFIG__MAX_COMPRESSED_FRAGS__SHIFT                                                      0x6
#define VPEC_GB_ADDR_CONFIG__NUM_PKRS__SHIFT                                                                  0x8
#define VPEC_GB_ADDR_CONFIG__NUM_SHADER_ENGINES__SHIFT                                                        0x13
#define VPEC_GB_ADDR_CONFIG__NUM_RB_PER_SE__SHIFT                                                             0x1a
#define VPEC_GB_ADDR_CONFIG__NUM_PIPES_MASK                                                                   0x00000007L
#define VPEC_GB_ADDR_CONFIG__PIPE_INTERLEAVE_SIZE_MASK                                                        0x00000038L
#define VPEC_GB_ADDR_CONFIG__MAX_COMPRESSED_FRAGS_MASK                                                        0x000000C0L
#define VPEC_GB_ADDR_CONFIG__NUM_PKRS_MASK                                                                    0x00000700L
#define VPEC_GB_ADDR_CONFIG__NUM_SHADER_ENGINES_MASK                                                          0x00180000L
#define VPEC_GB_ADDR_CONFIG__NUM_RB_PER_SE_MASK                                                               0x0C000000L
//VPEC_GB_ADDR_CONFIG_READ
#define VPEC_GB_ADDR_CONFIG_READ__NUM_PIPES__SHIFT                                                            0x0
#define VPEC_GB_ADDR_CONFIG_READ__PIPE_INTERLEAVE_SIZE__SHIFT                                                 0x3
#define VPEC_GB_ADDR_CONFIG_READ__MAX_COMPRESSED_FRAGS__SHIFT                                                 0x6
#define VPEC_GB_ADDR_CONFIG_READ__NUM_PKRS__SHIFT                                                             0x8
#define VPEC_GB_ADDR_CONFIG_READ__NUM_SHADER_ENGINES__SHIFT                                                   0x13
#define VPEC_GB_ADDR_CONFIG_READ__NUM_RB_PER_SE__SHIFT                                                        0x1a
#define VPEC_GB_ADDR_CONFIG_READ__NUM_PIPES_MASK                                                              0x00000007L
#define VPEC_GB_ADDR_CONFIG_READ__PIPE_INTERLEAVE_SIZE_MASK                                                   0x00000038L
#define VPEC_GB_ADDR_CONFIG_READ__MAX_COMPRESSED_FRAGS_MASK                                                   0x000000C0L
#define VPEC_GB_ADDR_CONFIG_READ__NUM_PKRS_MASK                                                               0x00000700L
#define VPEC_GB_ADDR_CONFIG_READ__NUM_SHADER_ENGINES_MASK                                                     0x00180000L
#define VPEC_GB_ADDR_CONFIG_READ__NUM_RB_PER_SE_MASK                                                          0x0C000000L
//VPEC_PROCESS_QUANTUM0
#define VPEC_PROCESS_QUANTUM0__PROCESS0_QUANTUM__SHIFT                                                        0x0
#define VPEC_PROCESS_QUANTUM0__PROCESS1_QUANTUM__SHIFT                                                        0x8
#define VPEC_PROCESS_QUANTUM0__PROCESS2_QUANTUM__SHIFT                                                        0x10
#define VPEC_PROCESS_QUANTUM0__PROCESS3_QUANTUM__SHIFT                                                        0x18
#define VPEC_PROCESS_QUANTUM0__PROCESS0_QUANTUM_MASK                                                          0x000000FFL
#define VPEC_PROCESS_QUANTUM0__PROCESS1_QUANTUM_MASK                                                          0x0000FF00L
#define VPEC_PROCESS_QUANTUM0__PROCESS2_QUANTUM_MASK                                                          0x00FF0000L
#define VPEC_PROCESS_QUANTUM0__PROCESS3_QUANTUM_MASK                                                          0xFF000000L
//VPEC_PROCESS_QUANTUM1
#define VPEC_PROCESS_QUANTUM1__PROCESS4_QUANTUM__SHIFT                                                        0x0
#define VPEC_PROCESS_QUANTUM1__PROCESS5_QUANTUM__SHIFT                                                        0x8
#define VPEC_PROCESS_QUANTUM1__PROCESS6_QUANTUM__SHIFT                                                        0x10
#define VPEC_PROCESS_QUANTUM1__PROCESS7_QUANTUM__SHIFT                                                        0x18
#define VPEC_PROCESS_QUANTUM1__PROCESS4_QUANTUM_MASK                                                          0x000000FFL
#define VPEC_PROCESS_QUANTUM1__PROCESS5_QUANTUM_MASK                                                          0x0000FF00L
#define VPEC_PROCESS_QUANTUM1__PROCESS6_QUANTUM_MASK                                                          0x00FF0000L
#define VPEC_PROCESS_QUANTUM1__PROCESS7_QUANTUM_MASK                                                          0xFF000000L
//VPEC_CONTEXT_SWITCH_THRESHOLD
#define VPEC_CONTEXT_SWITCH_THRESHOLD__REALTIME_THRESHOLD__SHIFT                                              0x0
#define VPEC_CONTEXT_SWITCH_THRESHOLD__FOCUS_THRESHOLD__SHIFT                                                 0x2
#define VPEC_CONTEXT_SWITCH_THRESHOLD__NORMAL_THRESHOLD__SHIFT                                                0x4
#define VPEC_CONTEXT_SWITCH_THRESHOLD__IDLE_THRESHOLD__SHIFT                                                  0x6
#define VPEC_CONTEXT_SWITCH_THRESHOLD__REALTIME_THRESHOLD_MASK                                                0x00000003L
#define VPEC_CONTEXT_SWITCH_THRESHOLD__FOCUS_THRESHOLD_MASK                                                   0x0000000CL
#define VPEC_CONTEXT_SWITCH_THRESHOLD__NORMAL_THRESHOLD_MASK                                                  0x00000030L
#define VPEC_CONTEXT_SWITCH_THRESHOLD__IDLE_THRESHOLD_MASK                                                    0x000000C0L
//VPEC_GLOBAL_QUANTUM
#define VPEC_GLOBAL_QUANTUM__GLOBAL_FOCUS_QUANTUM__SHIFT                                                      0x0
#define VPEC_GLOBAL_QUANTUM__GLOBAL_NORMAL_QUANTUM__SHIFT                                                     0x8
#define VPEC_GLOBAL_QUANTUM__GLOBAL_FOCUS_QUANTUM_MASK                                                        0x000000FFL
#define VPEC_GLOBAL_QUANTUM__GLOBAL_NORMAL_QUANTUM_MASK                                                       0x0000FF00L
//VPEC_WATCHDOG_CNTL
#define VPEC_WATCHDOG_CNTL__QUEUE_HANG_COUNT__SHIFT                                                           0x0
#define VPEC_WATCHDOG_CNTL__CMD_TIMEOUT_COUNT__SHIFT                                                          0x8
#define VPEC_WATCHDOG_CNTL__QUEUE_HANG_COUNT_MASK                                                             0x000000FFL
#define VPEC_WATCHDOG_CNTL__CMD_TIMEOUT_COUNT_MASK                                                            0x0000FF00L
//VPEC_ATOMIC_CNTL
#define VPEC_ATOMIC_CNTL__LOOP_TIMER__SHIFT                                                                   0x0
#define VPEC_ATOMIC_CNTL__ATOMIC_RTN_INT_ENABLE__SHIFT                                                        0x1f
#define VPEC_ATOMIC_CNTL__LOOP_TIMER_MASK                                                                     0x7FFFFFFFL
#define VPEC_ATOMIC_CNTL__ATOMIC_RTN_INT_ENABLE_MASK                                                          0x80000000L
//VPEC_UCODE_VERSION
#define VPEC_UCODE_VERSION__T0_UCODE_VERSION__SHIFT                                                           0x0
#define VPEC_UCODE_VERSION__T1_UCODE_VERSION__SHIFT                                                           0x10
#define VPEC_UCODE_VERSION__T0_UCODE_VERSION_MASK                                                             0x0000FFFFL
#define VPEC_UCODE_VERSION__T1_UCODE_VERSION_MASK                                                             0xFFFF0000L
//VPEC_MEMREQ_BURST_CNTL
#define VPEC_MEMREQ_BURST_CNTL__DATA_RD_BURST__SHIFT                                                          0x0
#define VPEC_MEMREQ_BURST_CNTL__DATA_WR_BURST__SHIFT                                                          0x2
#define VPEC_MEMREQ_BURST_CNTL__RB_RD_BURST__SHIFT                                                            0x4
#define VPEC_MEMREQ_BURST_CNTL__IB_RD_BURST__SHIFT                                                            0x6
#define VPEC_MEMREQ_BURST_CNTL__WR_BURST_WAIT_CYCLE__SHIFT                                                    0x8
#define VPEC_MEMREQ_BURST_CNTL__DATA_RD_BURST_MASK                                                            0x00000003L
#define VPEC_MEMREQ_BURST_CNTL__DATA_WR_BURST_MASK                                                            0x0000000CL
#define VPEC_MEMREQ_BURST_CNTL__RB_RD_BURST_MASK                                                              0x00000030L
#define VPEC_MEMREQ_BURST_CNTL__IB_RD_BURST_MASK                                                              0x000000C0L
#define VPEC_MEMREQ_BURST_CNTL__WR_BURST_WAIT_CYCLE_MASK                                                      0x00000700L
//VPEC_TIMESTAMP_CNTL
#define VPEC_TIMESTAMP_CNTL__CAPTURE__SHIFT                                                                   0x0
#define VPEC_TIMESTAMP_CNTL__CAPTURE_MASK                                                                     0x00000001L
//VPEC_GLOBAL_TIMESTAMP_LO
#define VPEC_GLOBAL_TIMESTAMP_LO__DATA__SHIFT                                                                 0x0
#define VPEC_GLOBAL_TIMESTAMP_LO__DATA_MASK                                                                   0xFFFFFFFFL
//VPEC_GLOBAL_TIMESTAMP_HI
#define VPEC_GLOBAL_TIMESTAMP_HI__DATA__SHIFT                                                                 0x0
#define VPEC_GLOBAL_TIMESTAMP_HI__DATA_MASK                                                                   0xFFFFFFFFL
//VPEC_FREEZE
#define VPEC_FREEZE__PREEMPT__SHIFT                                                                           0x0
#define VPEC_FREEZE__FREEZE__SHIFT                                                                            0x4
#define VPEC_FREEZE__FROZEN__SHIFT                                                                            0x5
#define VPEC_FREEZE__F32_FREEZE__SHIFT                                                                        0x6
#define VPEC_FREEZE__PREEMPT_MASK                                                                             0x00000001L
#define VPEC_FREEZE__FREEZE_MASK                                                                              0x00000010L
#define VPEC_FREEZE__FROZEN_MASK                                                                              0x00000020L
#define VPEC_FREEZE__F32_FREEZE_MASK                                                                          0x00000040L
//VPEC_CE_CTRL
#define VPEC_CE_CTRL__RD_LUT_WATERMARK__SHIFT                                                                 0x0
#define VPEC_CE_CTRL__RD_LUT_DEPTH__SHIFT                                                                     0x3
#define VPEC_CE_CTRL__WR_AFIFO_WATERMARK__SHIFT                                                               0x5
#define VPEC_CE_CTRL__RESERVED__SHIFT                                                                         0x8
#define VPEC_CE_CTRL__RD_LUT_WATERMARK_MASK                                                                   0x00000007L
#define VPEC_CE_CTRL__RD_LUT_DEPTH_MASK                                                                       0x00000018L
#define VPEC_CE_CTRL__WR_AFIFO_WATERMARK_MASK                                                                 0x000000E0L
#define VPEC_CE_CTRL__RESERVED_MASK                                                                           0xFFFFFF00L
//VPEC_RELAX_ORDERING_LUT
#define VPEC_RELAX_ORDERING_LUT__RESERVED0__SHIFT                                                             0x0
#define VPEC_RELAX_ORDERING_LUT__VPE__SHIFT                                                                   0x1
#define VPEC_RELAX_ORDERING_LUT__RESERVED_2_2__SHIFT                                                          0x2
#define VPEC_RELAX_ORDERING_LUT__RESERVED3__SHIFT                                                             0x3
#define VPEC_RELAX_ORDERING_LUT__RESERVED4__SHIFT                                                             0x4
#define VPEC_RELAX_ORDERING_LUT__FENCE__SHIFT                                                                 0x5
#define VPEC_RELAX_ORDERING_LUT__RESERVED76__SHIFT                                                            0x6
#define VPEC_RELAX_ORDERING_LUT__POLL_MEM__SHIFT                                                              0x8
#define VPEC_RELAX_ORDERING_LUT__COND_EXE__SHIFT                                                              0x9
#define VPEC_RELAX_ORDERING_LUT__ATOMIC__SHIFT                                                                0xa
#define VPEC_RELAX_ORDERING_LUT__RESERVED_11_11__SHIFT                                                        0xb
#define VPEC_RELAX_ORDERING_LUT__RESERVED_12_12__SHIFT                                                        0xc
#define VPEC_RELAX_ORDERING_LUT__TIMESTAMP__SHIFT                                                             0xd
#define VPEC_RELAX_ORDERING_LUT__RESERVED__SHIFT                                                              0xe
#define VPEC_RELAX_ORDERING_LUT__WORLD_SWITCH__SHIFT                                                          0x1b
#define VPEC_RELAX_ORDERING_LUT__RPTR_WRB__SHIFT                                                              0x1c
#define VPEC_RELAX_ORDERING_LUT__RESERVED_29_29__SHIFT                                                        0x1d
#define VPEC_RELAX_ORDERING_LUT__IB_FETCH__SHIFT                                                              0x1e
#define VPEC_RELAX_ORDERING_LUT__RB_FETCH__SHIFT                                                              0x1f
#define VPEC_RELAX_ORDERING_LUT__RESERVED0_MASK                                                               0x00000001L
#define VPEC_RELAX_ORDERING_LUT__VPE_MASK                                                                     0x00000002L
#define VPEC_RELAX_ORDERING_LUT__RESERVED_2_2_MASK                                                            0x00000004L
#define VPEC_RELAX_ORDERING_LUT__RESERVED3_MASK                                                               0x00000008L
#define VPEC_RELAX_ORDERING_LUT__RESERVED4_MASK                                                               0x00000010L
#define VPEC_RELAX_ORDERING_LUT__FENCE_MASK                                                                   0x00000020L
#define VPEC_RELAX_ORDERING_LUT__RESERVED76_MASK                                                              0x000000C0L
#define VPEC_RELAX_ORDERING_LUT__POLL_MEM_MASK                                                                0x00000100L
#define VPEC_RELAX_ORDERING_LUT__COND_EXE_MASK                                                                0x00000200L
#define VPEC_RELAX_ORDERING_LUT__ATOMIC_MASK                                                                  0x00000400L
#define VPEC_RELAX_ORDERING_LUT__RESERVED_11_11_MASK                                                          0x00000800L
#define VPEC_RELAX_ORDERING_LUT__RESERVED_12_12_MASK                                                          0x00001000L
#define VPEC_RELAX_ORDERING_LUT__TIMESTAMP_MASK                                                               0x00002000L
#define VPEC_RELAX_ORDERING_LUT__RESERVED_MASK                                                                0x07FFC000L
#define VPEC_RELAX_ORDERING_LUT__WORLD_SWITCH_MASK                                                            0x08000000L
#define VPEC_RELAX_ORDERING_LUT__RPTR_WRB_MASK                                                                0x10000000L
#define VPEC_RELAX_ORDERING_LUT__RESERVED_29_29_MASK                                                          0x20000000L
#define VPEC_RELAX_ORDERING_LUT__IB_FETCH_MASK                                                                0x40000000L
#define VPEC_RELAX_ORDERING_LUT__RB_FETCH_MASK                                                                0x80000000L
//VPEC_CREDIT_CNTL
#define VPEC_CREDIT_CNTL__MC_WRREQ_CREDIT__SHIFT                                                              0x7
#define VPEC_CREDIT_CNTL__MC_RDREQ_CREDIT__SHIFT                                                              0xd
#define VPEC_CREDIT_CNTL__MC_WRREQ_CREDIT_MASK                                                                0x00001F80L
#define VPEC_CREDIT_CNTL__MC_RDREQ_CREDIT_MASK                                                                0x0007E000L
//VPEC_SCRATCH_RAM_DATA
#define VPEC_SCRATCH_RAM_DATA__DATA__SHIFT                                                                    0x0
#define VPEC_SCRATCH_RAM_DATA__DATA_MASK                                                                      0xFFFFFFFFL
//VPEC_SCRATCH_RAM_ADDR
#define VPEC_SCRATCH_RAM_ADDR__ADDR__SHIFT                                                                    0x0
#define VPEC_SCRATCH_RAM_ADDR__ADDR_MASK                                                                      0x0000007FL
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
//VPEC_PERFCNT_PERFCOUNTER0_CFG
#define VPEC_PERFCNT_PERFCOUNTER0_CFG__PERF_SEL__SHIFT                                                        0x0
#define VPEC_PERFCNT_PERFCOUNTER0_CFG__PERF_SEL_END__SHIFT                                                    0x8
#define VPEC_PERFCNT_PERFCOUNTER0_CFG__PERF_MODE__SHIFT                                                       0x18
#define VPEC_PERFCNT_PERFCOUNTER0_CFG__ENABLE__SHIFT                                                          0x1c
#define VPEC_PERFCNT_PERFCOUNTER0_CFG__CLEAR__SHIFT                                                           0x1d
#define VPEC_PERFCNT_PERFCOUNTER0_CFG__PERF_SEL_MASK                                                          0x000000FFL
#define VPEC_PERFCNT_PERFCOUNTER0_CFG__PERF_SEL_END_MASK                                                      0x0000FF00L
#define VPEC_PERFCNT_PERFCOUNTER0_CFG__PERF_MODE_MASK                                                         0x0F000000L
#define VPEC_PERFCNT_PERFCOUNTER0_CFG__ENABLE_MASK                                                            0x10000000L
#define VPEC_PERFCNT_PERFCOUNTER0_CFG__CLEAR_MASK                                                             0x20000000L
//VPEC_PERFCNT_PERFCOUNTER1_CFG
#define VPEC_PERFCNT_PERFCOUNTER1_CFG__PERF_SEL__SHIFT                                                        0x0
#define VPEC_PERFCNT_PERFCOUNTER1_CFG__PERF_SEL_END__SHIFT                                                    0x8
#define VPEC_PERFCNT_PERFCOUNTER1_CFG__PERF_MODE__SHIFT                                                       0x18
#define VPEC_PERFCNT_PERFCOUNTER1_CFG__ENABLE__SHIFT                                                          0x1c
#define VPEC_PERFCNT_PERFCOUNTER1_CFG__CLEAR__SHIFT                                                           0x1d
#define VPEC_PERFCNT_PERFCOUNTER1_CFG__PERF_SEL_MASK                                                          0x000000FFL
#define VPEC_PERFCNT_PERFCOUNTER1_CFG__PERF_SEL_END_MASK                                                      0x0000FF00L
#define VPEC_PERFCNT_PERFCOUNTER1_CFG__PERF_MODE_MASK                                                         0x0F000000L
#define VPEC_PERFCNT_PERFCOUNTER1_CFG__ENABLE_MASK                                                            0x10000000L
#define VPEC_PERFCNT_PERFCOUNTER1_CFG__CLEAR_MASK                                                             0x20000000L
//VPEC_PERFCNT_PERFCOUNTER_RSLT_CNTL
#define VPEC_PERFCNT_PERFCOUNTER_RSLT_CNTL__PERF_COUNTER_SELECT__SHIFT                                        0x0
#define VPEC_PERFCNT_PERFCOUNTER_RSLT_CNTL__START_TRIGGER__SHIFT                                              0x8
#define VPEC_PERFCNT_PERFCOUNTER_RSLT_CNTL__STOP_TRIGGER__SHIFT                                               0x10
#define VPEC_PERFCNT_PERFCOUNTER_RSLT_CNTL__ENABLE_ANY__SHIFT                                                 0x18
#define VPEC_PERFCNT_PERFCOUNTER_RSLT_CNTL__CLEAR_ALL__SHIFT                                                  0x19
#define VPEC_PERFCNT_PERFCOUNTER_RSLT_CNTL__STOP_ALL_ON_SATURATE__SHIFT                                       0x1a
#define VPEC_PERFCNT_PERFCOUNTER_RSLT_CNTL__PERF_COUNTER_SELECT_MASK                                          0x0000000FL
#define VPEC_PERFCNT_PERFCOUNTER_RSLT_CNTL__START_TRIGGER_MASK                                                0x0000FF00L
#define VPEC_PERFCNT_PERFCOUNTER_RSLT_CNTL__STOP_TRIGGER_MASK                                                 0x00FF0000L
#define VPEC_PERFCNT_PERFCOUNTER_RSLT_CNTL__ENABLE_ANY_MASK                                                   0x01000000L
#define VPEC_PERFCNT_PERFCOUNTER_RSLT_CNTL__CLEAR_ALL_MASK                                                    0x02000000L
#define VPEC_PERFCNT_PERFCOUNTER_RSLT_CNTL__STOP_ALL_ON_SATURATE_MASK                                         0x04000000L
//VPEC_PERFCNT_MISC_CNTL
#define VPEC_PERFCNT_MISC_CNTL__CMD_OP__SHIFT                                                                 0x0
#define VPEC_PERFCNT_MISC_CNTL__MMHUB_REQ_EVENT_SELECT__SHIFT                                                 0x10
#define VPEC_PERFCNT_MISC_CNTL__CMD_OP_MASK                                                                   0x0000FFFFL
#define VPEC_PERFCNT_MISC_CNTL__MMHUB_REQ_EVENT_SELECT_MASK                                                   0x00010000L
//VPEC_PERFCNT_PERFCOUNTER_LO
#define VPEC_PERFCNT_PERFCOUNTER_LO__COUNTER_LO__SHIFT                                                        0x0
#define VPEC_PERFCNT_PERFCOUNTER_LO__COUNTER_LO_MASK                                                          0xFFFFFFFFL
//VPEC_PERFCNT_PERFCOUNTER_HI
#define VPEC_PERFCNT_PERFCOUNTER_HI__COUNTER_HI__SHIFT                                                        0x0
#define VPEC_PERFCNT_PERFCOUNTER_HI__COMPARE_VALUE__SHIFT                                                     0x10
#define VPEC_PERFCNT_PERFCOUNTER_HI__COUNTER_HI_MASK                                                          0x0000FFFFL
#define VPEC_PERFCNT_PERFCOUNTER_HI__COMPARE_VALUE_MASK                                                       0xFFFF0000L
//VPEC_CRC_CTRL
#define VPEC_CRC_CTRL__INDEX__SHIFT                                                                           0x0
#define VPEC_CRC_CTRL__START__SHIFT                                                                           0x1f
#define VPEC_CRC_CTRL__INDEX_MASK                                                                             0x0000FFFFL
#define VPEC_CRC_CTRL__START_MASK                                                                             0x80000000L
//VPEC_CRC_DATA
#define VPEC_CRC_DATA__DATA__SHIFT                                                                            0x0
#define VPEC_CRC_DATA__DATA_MASK                                                                              0xFFFFFFFFL
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
//VPEC_UCODE1_CHECKSUM
#define VPEC_UCODE1_CHECKSUM__DATA__SHIFT                                                                     0x0
#define VPEC_UCODE1_CHECKSUM__DATA_MASK                                                                       0xFFFFFFFFL
//VPEC_VERSION
#define VPEC_VERSION__MINVER__SHIFT                                                                           0x0
#define VPEC_VERSION__MAJVER__SHIFT                                                                           0x8
#define VPEC_VERSION__REV__SHIFT                                                                              0x10
#define VPEC_VERSION__MINVER_MASK                                                                             0x0000007FL
#define VPEC_VERSION__MAJVER_MASK                                                                             0x00007F00L
#define VPEC_VERSION__REV_MASK                                                                                0x003F0000L
//VPEC_UCODE_CHECKSUM
#define VPEC_UCODE_CHECKSUM__DATA__SHIFT                                                                      0x0
#define VPEC_UCODE_CHECKSUM__DATA_MASK                                                                        0xFFFFFFFFL
//VPEC_CLOCK_GATING_STATUS
#define VPEC_CLOCK_GATING_STATUS__DYN_CLK_GATE_STATUS__SHIFT                                                  0x0
#define VPEC_CLOCK_GATING_STATUS__IP_PIPE0_CLK_GATE_STATUS__SHIFT                                             0x2
#define VPEC_CLOCK_GATING_STATUS__IP_PIPE1_CLK_GATE_STATUS__SHIFT                                             0x3
#define VPEC_CLOCK_GATING_STATUS__OP_PIPE0_CLK_GATE_STATUS__SHIFT                                             0x4
#define VPEC_CLOCK_GATING_STATUS__REG_CLK_GATE_STATUS__SHIFT                                                  0x5
#define VPEC_CLOCK_GATING_STATUS__F32_CLK_GATE_STATUS__SHIFT                                                  0x6
#define VPEC_CLOCK_GATING_STATUS__DYN_CLK_GATE_STATUS_MASK                                                    0x00000001L
#define VPEC_CLOCK_GATING_STATUS__IP_PIPE0_CLK_GATE_STATUS_MASK                                               0x00000004L
#define VPEC_CLOCK_GATING_STATUS__IP_PIPE1_CLK_GATE_STATUS_MASK                                               0x00000008L
#define VPEC_CLOCK_GATING_STATUS__OP_PIPE0_CLK_GATE_STATUS_MASK                                               0x00000010L
#define VPEC_CLOCK_GATING_STATUS__REG_CLK_GATE_STATUS_MASK                                                    0x00000020L
#define VPEC_CLOCK_GATING_STATUS__F32_CLK_GATE_STATUS_MASK                                                    0x00000040L
//VPEC_RB_RPTR_FETCH
#define VPEC_RB_RPTR_FETCH__OFFSET__SHIFT                                                                     0x2
#define VPEC_RB_RPTR_FETCH__OFFSET_MASK                                                                       0xFFFFFFFCL
//VPEC_RB_RPTR_FETCH_HI
#define VPEC_RB_RPTR_FETCH_HI__OFFSET__SHIFT                                                                  0x0
#define VPEC_RB_RPTR_FETCH_HI__OFFSET_MASK                                                                    0xFFFFFFFFL
//VPEC_IB_OFFSET_FETCH
#define VPEC_IB_OFFSET_FETCH__OFFSET__SHIFT                                                                   0x2
#define VPEC_IB_OFFSET_FETCH__OFFSET_MASK                                                                     0x003FFFFCL
//VPEC_CMDIB_OFFSET_FETCH
#define VPEC_CMDIB_OFFSET_FETCH__OFFSET__SHIFT                                                                0x2
#define VPEC_CMDIB_OFFSET_FETCH__OFFSET_MASK                                                                  0x003FFFFCL
//VPEC_ATOMIC_PREOP_LO
#define VPEC_ATOMIC_PREOP_LO__DATA__SHIFT                                                                     0x0
#define VPEC_ATOMIC_PREOP_LO__DATA_MASK                                                                       0xFFFFFFFFL
//VPEC_ATOMIC_PREOP_HI
#define VPEC_ATOMIC_PREOP_HI__DATA__SHIFT                                                                     0x0
#define VPEC_ATOMIC_PREOP_HI__DATA_MASK                                                                       0xFFFFFFFFL
//VPEC_CE_BUSY
#define VPEC_CE_BUSY__CE_IP_PIPE0_BUSY__SHIFT                                                                 0x0
#define VPEC_CE_BUSY__CE_IP_PIPE1_BUSY__SHIFT                                                                 0x1
#define VPEC_CE_BUSY__CE_OP_PIPE0_BUSY__SHIFT                                                                 0x10
#define VPEC_CE_BUSY__CE_IP_PIPE0_BUSY_MASK                                                                   0x00000001L
#define VPEC_CE_BUSY__CE_IP_PIPE1_BUSY_MASK                                                                   0x00000002L
#define VPEC_CE_BUSY__CE_OP_PIPE0_BUSY_MASK                                                                   0x00010000L
//VPEC_F32_COUNTER
#define VPEC_F32_COUNTER__VALUE__SHIFT                                                                        0x0
#define VPEC_F32_COUNTER__VALUE_MASK                                                                          0xFFFFFFFFL
//VPEC_HOLE_ADDR_LO
#define VPEC_HOLE_ADDR_LO__VALUE__SHIFT                                                                       0x0
#define VPEC_HOLE_ADDR_LO__VALUE_MASK                                                                         0xFFFFFFFFL
//VPEC_HOLE_ADDR_HI
#define VPEC_HOLE_ADDR_HI__VALUE__SHIFT                                                                       0x0
#define VPEC_HOLE_ADDR_HI__VALUE_MASK                                                                         0xFFFFFFFFL
//VPEC_ERROR_LOG
//VPEC_INT_STATUS
#define VPEC_INT_STATUS__DATA__SHIFT                                                                          0x0
#define VPEC_INT_STATUS__DATA_MASK                                                                            0xFFFFFFFFL
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
#define VPEC_STATUS__RESERVED_11_11__SHIFT                                                                    0xb
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
#define VPEC_STATUS__RESERVED_22_22__SHIFT                                                                    0x16
#define VPEC_STATUS__RESERVED_23_23__SHIFT                                                                    0x17
#define VPEC_STATUS__RESERVED_24_24__SHIFT                                                                    0x18
#define VPEC_STATUS__PREV_CMD_IDLE__SHIFT                                                                     0x19
#define VPEC_STATUS__RESERVED_26_26__SHIFT                                                                    0x1a
#define VPEC_STATUS__RESERVED_27_27__SHIFT                                                                    0x1b
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
#define VPEC_STATUS__RESERVED_11_11_MASK                                                                      0x00000800L
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
#define VPEC_STATUS__RESERVED_22_22_MASK                                                                      0x00400000L
#define VPEC_STATUS__RESERVED_23_23_MASK                                                                      0x00800000L
#define VPEC_STATUS__RESERVED_24_24_MASK                                                                      0x01000000L
#define VPEC_STATUS__PREV_CMD_IDLE_MASK                                                                       0x02000000L
#define VPEC_STATUS__RESERVED_26_26_MASK                                                                      0x04000000L
#define VPEC_STATUS__RESERVED_27_27_MASK                                                                      0x08000000L
#define VPEC_STATUS__RESERVED_29_28_MASK                                                                      0x30000000L
#define VPEC_STATUS__INT_IDLE_MASK                                                                            0x40000000L
#define VPEC_STATUS__INT_REQ_STALL_MASK                                                                       0x80000000L
//VPEC_STATUS1
#define VPEC_STATUS1__CE_IP0_WREQ_IDLE__SHIFT                                                                 0x0
#define VPEC_STATUS1__CE_IP0_WR_IDLE__SHIFT                                                                   0x1
#define VPEC_STATUS1__CE_IP0_SPLIT_IDLE__SHIFT                                                                0x2
#define VPEC_STATUS1__CE_IP0_RREQ_IDLE__SHIFT                                                                 0x3
#define VPEC_STATUS1__CE_IP0_OUT_IDLE__SHIFT                                                                  0x4
#define VPEC_STATUS1__CE_IP0_IN_IDLE__SHIFT                                                                   0x5
#define VPEC_STATUS1__CE_IP0_DST_IDLE__SHIFT                                                                  0x6
#define VPEC_STATUS1__CE_IP0_CMD_IDLE__SHIFT                                                                  0x7
#define VPEC_STATUS1__CE_IP1_WREQ_IDLE__SHIFT                                                                 0x8
#define VPEC_STATUS1__CE_IP1_WR_IDLE__SHIFT                                                                   0x9
#define VPEC_STATUS1__CE_IP1_SPLIT_IDLE__SHIFT                                                                0xa
#define VPEC_STATUS1__CE_IP1_RREQ_IDLE__SHIFT                                                                 0xb
#define VPEC_STATUS1__CE_IP1_OUT_IDLE__SHIFT                                                                  0xc
#define VPEC_STATUS1__CE_IP1_IN_IDLE__SHIFT                                                                   0xd
#define VPEC_STATUS1__CE_IP1_DST_IDLE__SHIFT                                                                  0xe
#define VPEC_STATUS1__CE_IP1_CMD_IDLE__SHIFT                                                                  0xf
#define VPEC_STATUS1__CE_OP0_WR_IDLE__SHIFT                                                                   0x10
#define VPEC_STATUS1__CE_OP0_CMD_IDLE__SHIFT                                                                  0x11
#define VPEC_STATUS1__CE_IP0_AFIFO_FULL__SHIFT                                                                0x12
#define VPEC_STATUS1__CE_IP0_CMD_INFO_FULL__SHIFT                                                             0x13
#define VPEC_STATUS1__CE_IP0_CMD_INFO1_FULL__SHIFT                                                            0x14
#define VPEC_STATUS1__CE_IP1_AFIFO_FULL__SHIFT                                                                0x15
#define VPEC_STATUS1__CE_IP1_CMD_INFO_FULL__SHIFT                                                             0x16
#define VPEC_STATUS1__CE_IP1_CMD_INFO1_FULL__SHIFT                                                            0x17
#define VPEC_STATUS1__EX_START__SHIFT                                                                         0x18
#define VPEC_STATUS1__CE_RD_STALL__SHIFT                                                                      0x19
#define VPEC_STATUS1__CE_IP0_WR_STALL__SHIFT                                                                  0x1a
#define VPEC_STATUS1__CE_IP1_WR_STALL__SHIFT                                                                  0x1b
#define VPEC_STATUS1__RESERVED_28_28__SHIFT                                                                   0x1c
#define VPEC_STATUS1__VPEC_IDLE__SHIFT                                                                        0x1d
#define VPEC_STATUS1__PG_STATUS__SHIFT                                                                        0x1e
#define VPEC_STATUS1__CE_IP0_WREQ_IDLE_MASK                                                                   0x00000001L
#define VPEC_STATUS1__CE_IP0_WR_IDLE_MASK                                                                     0x00000002L
#define VPEC_STATUS1__CE_IP0_SPLIT_IDLE_MASK                                                                  0x00000004L
#define VPEC_STATUS1__CE_IP0_RREQ_IDLE_MASK                                                                   0x00000008L
#define VPEC_STATUS1__CE_IP0_OUT_IDLE_MASK                                                                    0x00000010L
#define VPEC_STATUS1__CE_IP0_IN_IDLE_MASK                                                                     0x00000020L
#define VPEC_STATUS1__CE_IP0_DST_IDLE_MASK                                                                    0x00000040L
#define VPEC_STATUS1__CE_IP0_CMD_IDLE_MASK                                                                    0x00000080L
#define VPEC_STATUS1__CE_IP1_WREQ_IDLE_MASK                                                                   0x00000100L
#define VPEC_STATUS1__CE_IP1_WR_IDLE_MASK                                                                     0x00000200L
#define VPEC_STATUS1__CE_IP1_SPLIT_IDLE_MASK                                                                  0x00000400L
#define VPEC_STATUS1__CE_IP1_RREQ_IDLE_MASK                                                                   0x00000800L
#define VPEC_STATUS1__CE_IP1_OUT_IDLE_MASK                                                                    0x00001000L
#define VPEC_STATUS1__CE_IP1_IN_IDLE_MASK                                                                     0x00002000L
#define VPEC_STATUS1__CE_IP1_DST_IDLE_MASK                                                                    0x00004000L
#define VPEC_STATUS1__CE_IP1_CMD_IDLE_MASK                                                                    0x00008000L
#define VPEC_STATUS1__CE_OP0_WR_IDLE_MASK                                                                     0x00010000L
#define VPEC_STATUS1__CE_OP0_CMD_IDLE_MASK                                                                    0x00020000L
#define VPEC_STATUS1__CE_IP0_AFIFO_FULL_MASK                                                                  0x00040000L
#define VPEC_STATUS1__CE_IP0_CMD_INFO_FULL_MASK                                                               0x00080000L
#define VPEC_STATUS1__CE_IP0_CMD_INFO1_FULL_MASK                                                              0x00100000L
#define VPEC_STATUS1__CE_IP1_AFIFO_FULL_MASK                                                                  0x00200000L
#define VPEC_STATUS1__CE_IP1_CMD_INFO_FULL_MASK                                                               0x00400000L
#define VPEC_STATUS1__CE_IP1_CMD_INFO1_FULL_MASK                                                              0x00800000L
#define VPEC_STATUS1__EX_START_MASK                                                                           0x01000000L
#define VPEC_STATUS1__CE_RD_STALL_MASK                                                                        0x02000000L
#define VPEC_STATUS1__CE_IP0_WR_STALL_MASK                                                                    0x04000000L
#define VPEC_STATUS1__CE_IP1_WR_STALL_MASK                                                                    0x08000000L
#define VPEC_STATUS1__RESERVED_28_28_MASK                                                                     0x10000000L
#define VPEC_STATUS1__VPEC_IDLE_MASK                                                                          0x20000000L
#define VPEC_STATUS1__PG_STATUS_MASK                                                                          0xC0000000L
//VPEC_STATUS2
#define VPEC_STATUS2__ID__SHIFT                                                                               0x0
#define VPEC_STATUS2__TH0F32_INSTR_PTR__SHIFT                                                                 0x2
#define VPEC_STATUS2__CMD_OP__SHIFT                                                                           0x10
#define VPEC_STATUS2__ID_MASK                                                                                 0x00000003L
#define VPEC_STATUS2__TH0F32_INSTR_PTR_MASK                                                                   0x0000FFFCL
#define VPEC_STATUS2__CMD_OP_MASK                                                                             0xFFFF0000L
//VPEC_STATUS3
#define VPEC_STATUS3__CMD_OP_STATUS__SHIFT                                                                    0x0
#define VPEC_STATUS3__RESERVED_19_16__SHIFT                                                                   0x10
#define VPEC_STATUS3__EXCEPTION_IDLE__SHIFT                                                                   0x14
#define VPEC_STATUS3__RESERVED_21_21__SHIFT                                                                   0x15
#define VPEC_STATUS3__RESERVED_22_22__SHIFT                                                                   0x16
#define VPEC_STATUS3__RESERVED_23_23__SHIFT                                                                   0x17
#define VPEC_STATUS3__RESERVED_24_24__SHIFT                                                                   0x18
#define VPEC_STATUS3__RESERVED_25_25__SHIFT                                                                   0x19
#define VPEC_STATUS3__INT_QUEUE_ID__SHIFT                                                                     0x1a
#define VPEC_STATUS3__RESERVED_31_30__SHIFT                                                                   0x1e
#define VPEC_STATUS3__CMD_OP_STATUS_MASK                                                                      0x0000FFFFL
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
//VPEC_INST
#define VPEC_INST__ID__SHIFT                                                                                  0x0
#define VPEC_INST__RESERVED__SHIFT                                                                            0x1
#define VPEC_INST__ID_MASK                                                                                    0x00000001L
#define VPEC_INST__RESERVED_MASK                                                                              0xFFFFFFFEL
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
#define VPEC_QUEUE_HANG_STATUS__INVALID_OPCODE__SHIFT                                                         0x3
#define VPEC_QUEUE_HANG_STATUS__INVALID_VPEP_CONFIG_ADDR__SHIFT                                               0x4
#define VPEC_QUEUE_HANG_STATUS__F30T0_HANG_MASK                                                               0x00000001L
#define VPEC_QUEUE_HANG_STATUS__CE_HANG_MASK                                                                  0x00000002L
#define VPEC_QUEUE_HANG_STATUS__EOF_MISMATCH_MASK                                                             0x00000004L
#define VPEC_QUEUE_HANG_STATUS__INVALID_OPCODE_MASK                                                           0x00000008L
#define VPEC_QUEUE_HANG_STATUS__INVALID_VPEP_CONFIG_ADDR_MASK                                                 0x00000010L
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
#define VPEC_QUEUE0_IB_CNTL__IB_SWAP_ENABLE__SHIFT                                                            0x4
#define VPEC_QUEUE0_IB_CNTL__SWITCH_INSIDE_IB__SHIFT                                                          0x8
#define VPEC_QUEUE0_IB_CNTL__CMD_VMID__SHIFT                                                                  0x10
#define VPEC_QUEUE0_IB_CNTL__IB_ENABLE_MASK                                                                   0x00000001L
#define VPEC_QUEUE0_IB_CNTL__IB_SWAP_ENABLE_MASK                                                              0x00000010L
#define VPEC_QUEUE0_IB_CNTL__SWITCH_INSIDE_IB_MASK                                                            0x00000100L
#define VPEC_QUEUE0_IB_CNTL__CMD_VMID_MASK                                                                    0x000F0000L
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
#define VPEC_QUEUE0_CMDIB_CNTL__IB_ENABLE_MASK                                                                0x00000001L
#define VPEC_QUEUE0_CMDIB_CNTL__IB_SWAP_ENABLE_MASK                                                           0x00000010L
#define VPEC_QUEUE0_CMDIB_CNTL__SWITCH_INSIDE_IB_MASK                                                         0x00000100L
#define VPEC_QUEUE0_CMDIB_CNTL__CMD_VMID_MASK                                                                 0x000F0000L
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
#define VPEC_QUEUE0_CONTEXT_STATUS__PREEMPT_DISABLE_MASK                                                      0x00000400L
#define VPEC_QUEUE0_CONTEXT_STATUS__RPTR_WB_IDLE_MASK                                                         0x00000800L
#define VPEC_QUEUE0_CONTEXT_STATUS__WPTR_UPDATE_PENDING_MASK                                                  0x00001000L
#define VPEC_QUEUE0_CONTEXT_STATUS__WPTR_UPDATE_FAIL_COUNT_MASK                                               0x00FF0000L
//VPEC_QUEUE0_DOORBELL_LOG
//VPEC_QUEUE0_IB_SUB_REMAIN
#define VPEC_QUEUE0_IB_SUB_REMAIN__SIZE__SHIFT                                                                0x0
#define VPEC_QUEUE0_IB_SUB_REMAIN__SIZE_MASK                                                                  0x00003FFFL
//VPEC_QUEUE0_PREEMPT
#define VPEC_QUEUE0_PREEMPT__IB_PREEMPT__SHIFT                                                                0x0
#define VPEC_QUEUE0_PREEMPT__IB_PREEMPT_MASK                                                                  0x00000001L
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
#define VPEC_QUEUE1_IB_CNTL__IB_SWAP_ENABLE__SHIFT                                                            0x4
#define VPEC_QUEUE1_IB_CNTL__SWITCH_INSIDE_IB__SHIFT                                                          0x8
#define VPEC_QUEUE1_IB_CNTL__CMD_VMID__SHIFT                                                                  0x10
#define VPEC_QUEUE1_IB_CNTL__IB_ENABLE_MASK                                                                   0x00000001L
#define VPEC_QUEUE1_IB_CNTL__IB_SWAP_ENABLE_MASK                                                              0x00000010L
#define VPEC_QUEUE1_IB_CNTL__SWITCH_INSIDE_IB_MASK                                                            0x00000100L
#define VPEC_QUEUE1_IB_CNTL__CMD_VMID_MASK                                                                    0x000F0000L
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
#define VPEC_QUEUE1_CMDIB_CNTL__IB_ENABLE_MASK                                                                0x00000001L
#define VPEC_QUEUE1_CMDIB_CNTL__IB_SWAP_ENABLE_MASK                                                           0x00000010L
#define VPEC_QUEUE1_CMDIB_CNTL__SWITCH_INSIDE_IB_MASK                                                         0x00000100L
#define VPEC_QUEUE1_CMDIB_CNTL__CMD_VMID_MASK                                                                 0x000F0000L
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
#define VPEC_QUEUE1_CONTEXT_STATUS__PREEMPT_DISABLE_MASK                                                      0x00000400L
#define VPEC_QUEUE1_CONTEXT_STATUS__RPTR_WB_IDLE_MASK                                                         0x00000800L
#define VPEC_QUEUE1_CONTEXT_STATUS__WPTR_UPDATE_PENDING_MASK                                                  0x00001000L
#define VPEC_QUEUE1_CONTEXT_STATUS__WPTR_UPDATE_FAIL_COUNT_MASK                                               0x00FF0000L
//VPEC_QUEUE1_DOORBELL_LOG
//VPEC_QUEUE1_IB_SUB_REMAIN
#define VPEC_QUEUE1_IB_SUB_REMAIN__SIZE__SHIFT                                                                0x0
#define VPEC_QUEUE1_IB_SUB_REMAIN__SIZE_MASK                                                                  0x00003FFFL
//VPEC_QUEUE1_PREEMPT
#define VPEC_QUEUE1_PREEMPT__IB_PREEMPT__SHIFT                                                                0x0
#define VPEC_QUEUE1_PREEMPT__IB_PREEMPT_MASK                                                                  0x00000001L
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
#define VPEC_QUEUE2_IB_CNTL__IB_SWAP_ENABLE__SHIFT                                                            0x4
#define VPEC_QUEUE2_IB_CNTL__SWITCH_INSIDE_IB__SHIFT                                                          0x8
#define VPEC_QUEUE2_IB_CNTL__CMD_VMID__SHIFT                                                                  0x10
#define VPEC_QUEUE2_IB_CNTL__IB_ENABLE_MASK                                                                   0x00000001L
#define VPEC_QUEUE2_IB_CNTL__IB_SWAP_ENABLE_MASK                                                              0x00000010L
#define VPEC_QUEUE2_IB_CNTL__SWITCH_INSIDE_IB_MASK                                                            0x00000100L
#define VPEC_QUEUE2_IB_CNTL__CMD_VMID_MASK                                                                    0x000F0000L
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
#define VPEC_QUEUE2_CMDIB_CNTL__IB_ENABLE_MASK                                                                0x00000001L
#define VPEC_QUEUE2_CMDIB_CNTL__IB_SWAP_ENABLE_MASK                                                           0x00000010L
#define VPEC_QUEUE2_CMDIB_CNTL__SWITCH_INSIDE_IB_MASK                                                         0x00000100L
#define VPEC_QUEUE2_CMDIB_CNTL__CMD_VMID_MASK                                                                 0x000F0000L
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
#define VPEC_QUEUE2_CONTEXT_STATUS__PREEMPT_DISABLE_MASK                                                      0x00000400L
#define VPEC_QUEUE2_CONTEXT_STATUS__RPTR_WB_IDLE_MASK                                                         0x00000800L
#define VPEC_QUEUE2_CONTEXT_STATUS__WPTR_UPDATE_PENDING_MASK                                                  0x00001000L
#define VPEC_QUEUE2_CONTEXT_STATUS__WPTR_UPDATE_FAIL_COUNT_MASK                                               0x00FF0000L
//VPEC_QUEUE2_DOORBELL_LOG
//VPEC_QUEUE2_IB_SUB_REMAIN
#define VPEC_QUEUE2_IB_SUB_REMAIN__SIZE__SHIFT                                                                0x0
#define VPEC_QUEUE2_IB_SUB_REMAIN__SIZE_MASK                                                                  0x00003FFFL
//VPEC_QUEUE2_PREEMPT
#define VPEC_QUEUE2_PREEMPT__IB_PREEMPT__SHIFT                                                                0x0
#define VPEC_QUEUE2_PREEMPT__IB_PREEMPT_MASK                                                                  0x00000001L
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
#define VPEC_QUEUE3_IB_CNTL__IB_SWAP_ENABLE__SHIFT                                                            0x4
#define VPEC_QUEUE3_IB_CNTL__SWITCH_INSIDE_IB__SHIFT                                                          0x8
#define VPEC_QUEUE3_IB_CNTL__CMD_VMID__SHIFT                                                                  0x10
#define VPEC_QUEUE3_IB_CNTL__IB_ENABLE_MASK                                                                   0x00000001L
#define VPEC_QUEUE3_IB_CNTL__IB_SWAP_ENABLE_MASK                                                              0x00000010L
#define VPEC_QUEUE3_IB_CNTL__SWITCH_INSIDE_IB_MASK                                                            0x00000100L
#define VPEC_QUEUE3_IB_CNTL__CMD_VMID_MASK                                                                    0x000F0000L
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
#define VPEC_QUEUE3_CMDIB_CNTL__IB_ENABLE_MASK                                                                0x00000001L
#define VPEC_QUEUE3_CMDIB_CNTL__IB_SWAP_ENABLE_MASK                                                           0x00000010L
#define VPEC_QUEUE3_CMDIB_CNTL__SWITCH_INSIDE_IB_MASK                                                         0x00000100L
#define VPEC_QUEUE3_CMDIB_CNTL__CMD_VMID_MASK                                                                 0x000F0000L
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
#define VPEC_QUEUE3_CONTEXT_STATUS__PREEMPT_DISABLE_MASK                                                      0x00000400L
#define VPEC_QUEUE3_CONTEXT_STATUS__RPTR_WB_IDLE_MASK                                                         0x00000800L
#define VPEC_QUEUE3_CONTEXT_STATUS__WPTR_UPDATE_PENDING_MASK                                                  0x00001000L
#define VPEC_QUEUE3_CONTEXT_STATUS__WPTR_UPDATE_FAIL_COUNT_MASK                                               0x00FF0000L
//VPEC_QUEUE3_DOORBELL_LOG
//VPEC_QUEUE3_IB_SUB_REMAIN
#define VPEC_QUEUE3_IB_SUB_REMAIN__SIZE__SHIFT                                                                0x0
#define VPEC_QUEUE3_IB_SUB_REMAIN__SIZE_MASK                                                                  0x00003FFFL
//VPEC_QUEUE3_PREEMPT
#define VPEC_QUEUE3_PREEMPT__IB_PREEMPT__SHIFT                                                                0x0
#define VPEC_QUEUE3_PREEMPT__IB_PREEMPT_MASK                                                                  0x00000001L
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
#define VPEC_QUEUE4_IB_CNTL__IB_SWAP_ENABLE__SHIFT                                                            0x4
#define VPEC_QUEUE4_IB_CNTL__SWITCH_INSIDE_IB__SHIFT                                                          0x8
#define VPEC_QUEUE4_IB_CNTL__CMD_VMID__SHIFT                                                                  0x10
#define VPEC_QUEUE4_IB_CNTL__IB_ENABLE_MASK                                                                   0x00000001L
#define VPEC_QUEUE4_IB_CNTL__IB_SWAP_ENABLE_MASK                                                              0x00000010L
#define VPEC_QUEUE4_IB_CNTL__SWITCH_INSIDE_IB_MASK                                                            0x00000100L
#define VPEC_QUEUE4_IB_CNTL__CMD_VMID_MASK                                                                    0x000F0000L
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
#define VPEC_QUEUE4_CMDIB_CNTL__IB_ENABLE_MASK                                                                0x00000001L
#define VPEC_QUEUE4_CMDIB_CNTL__IB_SWAP_ENABLE_MASK                                                           0x00000010L
#define VPEC_QUEUE4_CMDIB_CNTL__SWITCH_INSIDE_IB_MASK                                                         0x00000100L
#define VPEC_QUEUE4_CMDIB_CNTL__CMD_VMID_MASK                                                                 0x000F0000L
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
#define VPEC_QUEUE4_CONTEXT_STATUS__PREEMPT_DISABLE_MASK                                                      0x00000400L
#define VPEC_QUEUE4_CONTEXT_STATUS__RPTR_WB_IDLE_MASK                                                         0x00000800L
#define VPEC_QUEUE4_CONTEXT_STATUS__WPTR_UPDATE_PENDING_MASK                                                  0x00001000L
#define VPEC_QUEUE4_CONTEXT_STATUS__WPTR_UPDATE_FAIL_COUNT_MASK                                               0x00FF0000L
//VPEC_QUEUE4_DOORBELL_LOG
//VPEC_QUEUE4_IB_SUB_REMAIN
#define VPEC_QUEUE4_IB_SUB_REMAIN__SIZE__SHIFT                                                                0x0
#define VPEC_QUEUE4_IB_SUB_REMAIN__SIZE_MASK                                                                  0x00003FFFL
//VPEC_QUEUE4_PREEMPT
#define VPEC_QUEUE4_PREEMPT__IB_PREEMPT__SHIFT                                                                0x0
#define VPEC_QUEUE4_PREEMPT__IB_PREEMPT_MASK                                                                  0x00000001L
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
#define VPEC_QUEUE5_IB_CNTL__IB_SWAP_ENABLE__SHIFT                                                            0x4
#define VPEC_QUEUE5_IB_CNTL__SWITCH_INSIDE_IB__SHIFT                                                          0x8
#define VPEC_QUEUE5_IB_CNTL__CMD_VMID__SHIFT                                                                  0x10
#define VPEC_QUEUE5_IB_CNTL__IB_ENABLE_MASK                                                                   0x00000001L
#define VPEC_QUEUE5_IB_CNTL__IB_SWAP_ENABLE_MASK                                                              0x00000010L
#define VPEC_QUEUE5_IB_CNTL__SWITCH_INSIDE_IB_MASK                                                            0x00000100L
#define VPEC_QUEUE5_IB_CNTL__CMD_VMID_MASK                                                                    0x000F0000L
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
#define VPEC_QUEUE5_CMDIB_CNTL__IB_ENABLE_MASK                                                                0x00000001L
#define VPEC_QUEUE5_CMDIB_CNTL__IB_SWAP_ENABLE_MASK                                                           0x00000010L
#define VPEC_QUEUE5_CMDIB_CNTL__SWITCH_INSIDE_IB_MASK                                                         0x00000100L
#define VPEC_QUEUE5_CMDIB_CNTL__CMD_VMID_MASK                                                                 0x000F0000L
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
#define VPEC_QUEUE5_CONTEXT_STATUS__PREEMPT_DISABLE_MASK                                                      0x00000400L
#define VPEC_QUEUE5_CONTEXT_STATUS__RPTR_WB_IDLE_MASK                                                         0x00000800L
#define VPEC_QUEUE5_CONTEXT_STATUS__WPTR_UPDATE_PENDING_MASK                                                  0x00001000L
#define VPEC_QUEUE5_CONTEXT_STATUS__WPTR_UPDATE_FAIL_COUNT_MASK                                               0x00FF0000L
//VPEC_QUEUE5_DOORBELL_LOG
//VPEC_QUEUE5_IB_SUB_REMAIN
#define VPEC_QUEUE5_IB_SUB_REMAIN__SIZE__SHIFT                                                                0x0
#define VPEC_QUEUE5_IB_SUB_REMAIN__SIZE_MASK                                                                  0x00003FFFL
//VPEC_QUEUE5_PREEMPT
#define VPEC_QUEUE5_PREEMPT__IB_PREEMPT__SHIFT                                                                0x0
#define VPEC_QUEUE5_PREEMPT__IB_PREEMPT_MASK                                                                  0x00000001L
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
#define VPEC_QUEUE6_IB_CNTL__IB_SWAP_ENABLE__SHIFT                                                            0x4
#define VPEC_QUEUE6_IB_CNTL__SWITCH_INSIDE_IB__SHIFT                                                          0x8
#define VPEC_QUEUE6_IB_CNTL__CMD_VMID__SHIFT                                                                  0x10
#define VPEC_QUEUE6_IB_CNTL__IB_ENABLE_MASK                                                                   0x00000001L
#define VPEC_QUEUE6_IB_CNTL__IB_SWAP_ENABLE_MASK                                                              0x00000010L
#define VPEC_QUEUE6_IB_CNTL__SWITCH_INSIDE_IB_MASK                                                            0x00000100L
#define VPEC_QUEUE6_IB_CNTL__CMD_VMID_MASK                                                                    0x000F0000L
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
#define VPEC_QUEUE6_CMDIB_CNTL__IB_ENABLE_MASK                                                                0x00000001L
#define VPEC_QUEUE6_CMDIB_CNTL__IB_SWAP_ENABLE_MASK                                                           0x00000010L
#define VPEC_QUEUE6_CMDIB_CNTL__SWITCH_INSIDE_IB_MASK                                                         0x00000100L
#define VPEC_QUEUE6_CMDIB_CNTL__CMD_VMID_MASK                                                                 0x000F0000L
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
#define VPEC_QUEUE6_CONTEXT_STATUS__PREEMPT_DISABLE_MASK                                                      0x00000400L
#define VPEC_QUEUE6_CONTEXT_STATUS__RPTR_WB_IDLE_MASK                                                         0x00000800L
#define VPEC_QUEUE6_CONTEXT_STATUS__WPTR_UPDATE_PENDING_MASK                                                  0x00001000L
#define VPEC_QUEUE6_CONTEXT_STATUS__WPTR_UPDATE_FAIL_COUNT_MASK                                               0x00FF0000L
//VPEC_QUEUE6_DOORBELL_LOG
//VPEC_QUEUE6_IB_SUB_REMAIN
#define VPEC_QUEUE6_IB_SUB_REMAIN__SIZE__SHIFT                                                                0x0
#define VPEC_QUEUE6_IB_SUB_REMAIN__SIZE_MASK                                                                  0x00003FFFL
//VPEC_QUEUE6_PREEMPT
#define VPEC_QUEUE6_PREEMPT__IB_PREEMPT__SHIFT                                                                0x0
#define VPEC_QUEUE6_PREEMPT__IB_PREEMPT_MASK                                                                  0x00000001L
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
#define VPEC_QUEUE7_IB_CNTL__IB_SWAP_ENABLE__SHIFT                                                            0x4
#define VPEC_QUEUE7_IB_CNTL__SWITCH_INSIDE_IB__SHIFT                                                          0x8
#define VPEC_QUEUE7_IB_CNTL__CMD_VMID__SHIFT                                                                  0x10
#define VPEC_QUEUE7_IB_CNTL__IB_ENABLE_MASK                                                                   0x00000001L
#define VPEC_QUEUE7_IB_CNTL__IB_SWAP_ENABLE_MASK                                                              0x00000010L
#define VPEC_QUEUE7_IB_CNTL__SWITCH_INSIDE_IB_MASK                                                            0x00000100L
#define VPEC_QUEUE7_IB_CNTL__CMD_VMID_MASK                                                                    0x000F0000L
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
#define VPEC_QUEUE7_CMDIB_CNTL__IB_ENABLE_MASK                                                                0x00000001L
#define VPEC_QUEUE7_CMDIB_CNTL__IB_SWAP_ENABLE_MASK                                                           0x00000010L
#define VPEC_QUEUE7_CMDIB_CNTL__SWITCH_INSIDE_IB_MASK                                                         0x00000100L
#define VPEC_QUEUE7_CMDIB_CNTL__CMD_VMID_MASK                                                                 0x000F0000L
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
#define VPEC_QUEUE7_CONTEXT_STATUS__PREEMPT_DISABLE_MASK                                                      0x00000400L
#define VPEC_QUEUE7_CONTEXT_STATUS__RPTR_WB_IDLE_MASK                                                         0x00000800L
#define VPEC_QUEUE7_CONTEXT_STATUS__WPTR_UPDATE_PENDING_MASK                                                  0x00001000L
#define VPEC_QUEUE7_CONTEXT_STATUS__WPTR_UPDATE_FAIL_COUNT_MASK                                               0x00FF0000L
//VPEC_QUEUE7_DOORBELL_LOG
//VPEC_QUEUE7_IB_SUB_REMAIN
#define VPEC_QUEUE7_IB_SUB_REMAIN__SIZE__SHIFT                                                                0x0
#define VPEC_QUEUE7_IB_SUB_REMAIN__SIZE_MASK                                                                  0x00003FFFL
//VPEC_QUEUE7_PREEMPT
#define VPEC_QUEUE7_PREEMPT__IB_PREEMPT__SHIFT                                                                0x0
#define VPEC_QUEUE7_PREEMPT__IB_PREEMPT_MASK                                                                  0x00000001L


// addressBlock: vpe_vpep_vpdpp0_dispdec_vpcnvc_cfg_dispdec
//VPCNVC_SURFACE_PIXEL_FORMAT
#define VPCNVC_SURFACE_PIXEL_FORMAT__VPCNVC_SURFACE_PIXEL_FORMAT__SHIFT                                       0x0
#define VPCNVC_SURFACE_PIXEL_FORMAT__VPCNVC_SURFACE_PIXEL_FORMAT_MASK                                         0x0000007FL
//VPCNVC_FORMAT_CONTROL
#define VPCNVC_FORMAT_CONTROL__FORMAT_EXPANSION_MODE__SHIFT                                                   0x0
#define VPCNVC_FORMAT_CONTROL__FORMAT_CNV16__SHIFT                                                            0x4
#define VPCNVC_FORMAT_CONTROL__ALPHA_EN__SHIFT                                                                0x8
#define VPCNVC_FORMAT_CONTROL__VPCNVC_BYPASS__SHIFT                                                           0xc
#define VPCNVC_FORMAT_CONTROL__VPCNVC_BYPASS_MSB_ALIGN__SHIFT                                                 0xd
#define VPCNVC_FORMAT_CONTROL__CLAMP_POSITIVE__SHIFT                                                          0x10
#define VPCNVC_FORMAT_CONTROL__CLAMP_POSITIVE_C__SHIFT                                                        0x11
#define VPCNVC_FORMAT_CONTROL__VPCNVC_UPDATE_PENDING__SHIFT                                                   0x14
#define VPCNVC_FORMAT_CONTROL__FORMAT_EXPANSION_MODE_MASK                                                     0x00000001L
#define VPCNVC_FORMAT_CONTROL__FORMAT_CNV16_MASK                                                              0x00000010L
#define VPCNVC_FORMAT_CONTROL__ALPHA_EN_MASK                                                                  0x00000100L
#define VPCNVC_FORMAT_CONTROL__VPCNVC_BYPASS_MASK                                                             0x00001000L
#define VPCNVC_FORMAT_CONTROL__VPCNVC_BYPASS_MSB_ALIGN_MASK                                                   0x00002000L
#define VPCNVC_FORMAT_CONTROL__CLAMP_POSITIVE_MASK                                                            0x00010000L
#define VPCNVC_FORMAT_CONTROL__CLAMP_POSITIVE_C_MASK                                                          0x00020000L
#define VPCNVC_FORMAT_CONTROL__VPCNVC_UPDATE_PENDING_MASK                                                     0x00100000L
//VPCNVC_FCNV_FP_BIAS_R
#define VPCNVC_FCNV_FP_BIAS_R__FCNV_FP_BIAS_R__SHIFT                                                          0x0
#define VPCNVC_FCNV_FP_BIAS_R__FCNV_FP_BIAS_R_MASK                                                            0x0007FFFFL
//VPCNVC_FCNV_FP_BIAS_G
#define VPCNVC_FCNV_FP_BIAS_G__FCNV_FP_BIAS_G__SHIFT                                                          0x0
#define VPCNVC_FCNV_FP_BIAS_G__FCNV_FP_BIAS_G_MASK                                                            0x0007FFFFL
//VPCNVC_FCNV_FP_BIAS_B
#define VPCNVC_FCNV_FP_BIAS_B__FCNV_FP_BIAS_B__SHIFT                                                          0x0
#define VPCNVC_FCNV_FP_BIAS_B__FCNV_FP_BIAS_B_MASK                                                            0x0007FFFFL
//VPCNVC_FCNV_FP_SCALE_R
#define VPCNVC_FCNV_FP_SCALE_R__FCNV_FP_SCALE_R__SHIFT                                                        0x0
#define VPCNVC_FCNV_FP_SCALE_R__FCNV_FP_SCALE_R_MASK                                                          0x0007FFFFL
//VPCNVC_FCNV_FP_SCALE_G
#define VPCNVC_FCNV_FP_SCALE_G__FCNV_FP_SCALE_G__SHIFT                                                        0x0
#define VPCNVC_FCNV_FP_SCALE_G__FCNV_FP_SCALE_G_MASK                                                          0x0007FFFFL
//VPCNVC_FCNV_FP_SCALE_B
#define VPCNVC_FCNV_FP_SCALE_B__FCNV_FP_SCALE_B__SHIFT                                                        0x0
#define VPCNVC_FCNV_FP_SCALE_B__FCNV_FP_SCALE_B_MASK                                                          0x0007FFFFL
//VPCNVC_COLOR_KEYER_CONTROL
#define VPCNVC_COLOR_KEYER_CONTROL__COLOR_KEYER_EN__SHIFT                                                     0x0
#define VPCNVC_COLOR_KEYER_CONTROL__COLOR_KEYER_MODE__SHIFT                                                   0x4
#define VPCNVC_COLOR_KEYER_CONTROL__COLOR_KEYER_EN_MASK                                                       0x00000001L
#define VPCNVC_COLOR_KEYER_CONTROL__COLOR_KEYER_MODE_MASK                                                     0x00000030L
//VPCNVC_COLOR_KEYER_ALPHA
#define VPCNVC_COLOR_KEYER_ALPHA__COLOR_KEYER_ALPHA_LOW__SHIFT                                                0x0
#define VPCNVC_COLOR_KEYER_ALPHA__COLOR_KEYER_ALPHA_HIGH__SHIFT                                               0x10
#define VPCNVC_COLOR_KEYER_ALPHA__COLOR_KEYER_ALPHA_LOW_MASK                                                  0x0000FFFFL
#define VPCNVC_COLOR_KEYER_ALPHA__COLOR_KEYER_ALPHA_HIGH_MASK                                                 0xFFFF0000L
//VPCNVC_COLOR_KEYER_RED
#define VPCNVC_COLOR_KEYER_RED__COLOR_KEYER_RED_LOW__SHIFT                                                    0x0
#define VPCNVC_COLOR_KEYER_RED__COLOR_KEYER_RED_HIGH__SHIFT                                                   0x10
#define VPCNVC_COLOR_KEYER_RED__COLOR_KEYER_RED_LOW_MASK                                                      0x0000FFFFL
#define VPCNVC_COLOR_KEYER_RED__COLOR_KEYER_RED_HIGH_MASK                                                     0xFFFF0000L
//VPCNVC_COLOR_KEYER_GREEN
#define VPCNVC_COLOR_KEYER_GREEN__COLOR_KEYER_GREEN_LOW__SHIFT                                                0x0
#define VPCNVC_COLOR_KEYER_GREEN__COLOR_KEYER_GREEN_HIGH__SHIFT                                               0x10
#define VPCNVC_COLOR_KEYER_GREEN__COLOR_KEYER_GREEN_LOW_MASK                                                  0x0000FFFFL
#define VPCNVC_COLOR_KEYER_GREEN__COLOR_KEYER_GREEN_HIGH_MASK                                                 0xFFFF0000L
//VPCNVC_COLOR_KEYER_BLUE
#define VPCNVC_COLOR_KEYER_BLUE__COLOR_KEYER_BLUE_LOW__SHIFT                                                  0x0
#define VPCNVC_COLOR_KEYER_BLUE__COLOR_KEYER_BLUE_HIGH__SHIFT                                                 0x10
#define VPCNVC_COLOR_KEYER_BLUE__COLOR_KEYER_BLUE_LOW_MASK                                                    0x0000FFFFL
#define VPCNVC_COLOR_KEYER_BLUE__COLOR_KEYER_BLUE_HIGH_MASK                                                   0xFFFF0000L
//VPCNVC_ALPHA_2BIT_LUT
#define VPCNVC_ALPHA_2BIT_LUT__ALPHA_2BIT_LUT0__SHIFT                                                         0x0
#define VPCNVC_ALPHA_2BIT_LUT__ALPHA_2BIT_LUT1__SHIFT                                                         0x8
#define VPCNVC_ALPHA_2BIT_LUT__ALPHA_2BIT_LUT2__SHIFT                                                         0x10
#define VPCNVC_ALPHA_2BIT_LUT__ALPHA_2BIT_LUT3__SHIFT                                                         0x18
#define VPCNVC_ALPHA_2BIT_LUT__ALPHA_2BIT_LUT0_MASK                                                           0x000000FFL
#define VPCNVC_ALPHA_2BIT_LUT__ALPHA_2BIT_LUT1_MASK                                                           0x0000FF00L
#define VPCNVC_ALPHA_2BIT_LUT__ALPHA_2BIT_LUT2_MASK                                                           0x00FF0000L
#define VPCNVC_ALPHA_2BIT_LUT__ALPHA_2BIT_LUT3_MASK                                                           0xFF000000L
//VPCNVC_PRE_DEALPHA
#define VPCNVC_PRE_DEALPHA__PRE_DEALPHA_EN__SHIFT                                                             0x0
#define VPCNVC_PRE_DEALPHA__PRE_DEALPHA_ABLND_EN__SHIFT                                                       0x4
#define VPCNVC_PRE_DEALPHA__PRE_DEALPHA_EN_MASK                                                               0x00000001L
#define VPCNVC_PRE_DEALPHA__PRE_DEALPHA_ABLND_EN_MASK                                                         0x00000010L
//VPCNVC_PRE_CSC_MODE
#define VPCNVC_PRE_CSC_MODE__PRE_CSC_MODE__SHIFT                                                              0x0
#define VPCNVC_PRE_CSC_MODE__PRE_CSC_MODE_CURRENT__SHIFT                                                      0x2
#define VPCNVC_PRE_CSC_MODE__PRE_CSC_MODE_MASK                                                                0x00000001L
#define VPCNVC_PRE_CSC_MODE__PRE_CSC_MODE_CURRENT_MASK                                                        0x00000004L
//VPCNVC_PRE_CSC_C11_C12
#define VPCNVC_PRE_CSC_C11_C12__PRE_CSC_C11__SHIFT                                                            0x0
#define VPCNVC_PRE_CSC_C11_C12__PRE_CSC_C12__SHIFT                                                            0x10
#define VPCNVC_PRE_CSC_C11_C12__PRE_CSC_C11_MASK                                                              0x0000FFFFL
#define VPCNVC_PRE_CSC_C11_C12__PRE_CSC_C12_MASK                                                              0xFFFF0000L
//VPCNVC_PRE_CSC_C13_C14
#define VPCNVC_PRE_CSC_C13_C14__PRE_CSC_C13__SHIFT                                                            0x0
#define VPCNVC_PRE_CSC_C13_C14__PRE_CSC_C14__SHIFT                                                            0x10
#define VPCNVC_PRE_CSC_C13_C14__PRE_CSC_C13_MASK                                                              0x0000FFFFL
#define VPCNVC_PRE_CSC_C13_C14__PRE_CSC_C14_MASK                                                              0xFFFF0000L
//VPCNVC_PRE_CSC_C21_C22
#define VPCNVC_PRE_CSC_C21_C22__PRE_CSC_C21__SHIFT                                                            0x0
#define VPCNVC_PRE_CSC_C21_C22__PRE_CSC_C22__SHIFT                                                            0x10
#define VPCNVC_PRE_CSC_C21_C22__PRE_CSC_C21_MASK                                                              0x0000FFFFL
#define VPCNVC_PRE_CSC_C21_C22__PRE_CSC_C22_MASK                                                              0xFFFF0000L
//VPCNVC_PRE_CSC_C23_C24
#define VPCNVC_PRE_CSC_C23_C24__PRE_CSC_C23__SHIFT                                                            0x0
#define VPCNVC_PRE_CSC_C23_C24__PRE_CSC_C24__SHIFT                                                            0x10
#define VPCNVC_PRE_CSC_C23_C24__PRE_CSC_C23_MASK                                                              0x0000FFFFL
#define VPCNVC_PRE_CSC_C23_C24__PRE_CSC_C24_MASK                                                              0xFFFF0000L
//VPCNVC_PRE_CSC_C31_C32
#define VPCNVC_PRE_CSC_C31_C32__PRE_CSC_C31__SHIFT                                                            0x0
#define VPCNVC_PRE_CSC_C31_C32__PRE_CSC_C32__SHIFT                                                            0x10
#define VPCNVC_PRE_CSC_C31_C32__PRE_CSC_C31_MASK                                                              0x0000FFFFL
#define VPCNVC_PRE_CSC_C31_C32__PRE_CSC_C32_MASK                                                              0xFFFF0000L
//VPCNVC_PRE_CSC_C33_C34
#define VPCNVC_PRE_CSC_C33_C34__PRE_CSC_C33__SHIFT                                                            0x0
#define VPCNVC_PRE_CSC_C33_C34__PRE_CSC_C34__SHIFT                                                            0x10
#define VPCNVC_PRE_CSC_C33_C34__PRE_CSC_C33_MASK                                                              0x0000FFFFL
#define VPCNVC_PRE_CSC_C33_C34__PRE_CSC_C34_MASK                                                              0xFFFF0000L
//VPCNVC_COEF_FORMAT
#define VPCNVC_COEF_FORMAT__PRE_CSC_COEF_FORMAT__SHIFT                                                        0x0
#define VPCNVC_COEF_FORMAT__PRE_CSC_COEF_FORMAT_MASK                                                          0x00000001L
//VPCNVC_PRE_DEGAM
#define VPCNVC_PRE_DEGAM__PRE_DEGAM_MODE__SHIFT                                                               0x0
#define VPCNVC_PRE_DEGAM__PRE_DEGAM_SELECT__SHIFT                                                             0x4
#define VPCNVC_PRE_DEGAM__PRE_DEGAM_MODE_MASK                                                                 0x00000003L
#define VPCNVC_PRE_DEGAM__PRE_DEGAM_SELECT_MASK                                                               0x00000070L
//VPCNVC_PRE_REALPHA
#define VPCNVC_PRE_REALPHA__PRE_REALPHA_EN__SHIFT                                                             0x0
#define VPCNVC_PRE_REALPHA__PRE_REALPHA_ABLND_EN__SHIFT                                                       0x4
#define VPCNVC_PRE_REALPHA__PRE_REALPHA_EN_MASK                                                               0x00000001L
#define VPCNVC_PRE_REALPHA__PRE_REALPHA_ABLND_EN_MASK                                                         0x00000010L


// addressBlock: vpe_vpep_vpdpp0_dispdec_vpdscl_dispdec
//VPDSCL_COEF_RAM_TAP_SELECT
#define VPDSCL_COEF_RAM_TAP_SELECT__SCL_COEF_RAM_TAP_PAIR_IDX__SHIFT                                          0x0
#define VPDSCL_COEF_RAM_TAP_SELECT__SCL_COEF_RAM_PHASE__SHIFT                                                 0x8
#define VPDSCL_COEF_RAM_TAP_SELECT__SCL_COEF_RAM_FILTER_TYPE__SHIFT                                           0x10
#define VPDSCL_COEF_RAM_TAP_SELECT__SCL_COEF_RAM_TAP_PAIR_IDX_MASK                                            0x00000003L
#define VPDSCL_COEF_RAM_TAP_SELECT__SCL_COEF_RAM_PHASE_MASK                                                   0x00003F00L
#define VPDSCL_COEF_RAM_TAP_SELECT__SCL_COEF_RAM_FILTER_TYPE_MASK                                             0x00030000L
//VPDSCL_COEF_RAM_TAP_DATA
#define VPDSCL_COEF_RAM_TAP_DATA__SCL_COEF_RAM_EVEN_TAP_COEF__SHIFT                                           0x0
#define VPDSCL_COEF_RAM_TAP_DATA__SCL_COEF_RAM_EVEN_TAP_COEF_EN__SHIFT                                        0xf
#define VPDSCL_COEF_RAM_TAP_DATA__SCL_COEF_RAM_ODD_TAP_COEF__SHIFT                                            0x10
#define VPDSCL_COEF_RAM_TAP_DATA__SCL_COEF_RAM_ODD_TAP_COEF_EN__SHIFT                                         0x1f
#define VPDSCL_COEF_RAM_TAP_DATA__SCL_COEF_RAM_EVEN_TAP_COEF_MASK                                             0x00003FFFL
#define VPDSCL_COEF_RAM_TAP_DATA__SCL_COEF_RAM_EVEN_TAP_COEF_EN_MASK                                          0x00008000L
#define VPDSCL_COEF_RAM_TAP_DATA__SCL_COEF_RAM_ODD_TAP_COEF_MASK                                              0x3FFF0000L
#define VPDSCL_COEF_RAM_TAP_DATA__SCL_COEF_RAM_ODD_TAP_COEF_EN_MASK                                           0x80000000L
//VPDSCL_MODE
#define VPDSCL_MODE__VPDSCL_MODE__SHIFT                                                                       0x0
#define VPDSCL_MODE__SCL_COEF_RAM_SELECT_CURRENT__SHIFT                                                       0xc
#define VPDSCL_MODE__SCL_CHROMA_COEF_MODE__SHIFT                                                              0x10
#define VPDSCL_MODE__SCL_ALPHA_COEF_MODE__SHIFT                                                               0x14
#define VPDSCL_MODE__SCL_COEF_RAM_SELECT_RD__SHIFT                                                            0x18
#define VPDSCL_MODE__VPDSCL_MODE_MASK                                                                         0x00000007L
#define VPDSCL_MODE__SCL_COEF_RAM_SELECT_CURRENT_MASK                                                         0x00001000L
#define VPDSCL_MODE__SCL_CHROMA_COEF_MODE_MASK                                                                0x00010000L
#define VPDSCL_MODE__SCL_ALPHA_COEF_MODE_MASK                                                                 0x00100000L
#define VPDSCL_MODE__SCL_COEF_RAM_SELECT_RD_MASK                                                              0x01000000L
//VPDSCL_TAP_CONTROL
#define VPDSCL_TAP_CONTROL__SCL_V_NUM_TAPS__SHIFT                                                             0x0
#define VPDSCL_TAP_CONTROL__SCL_H_NUM_TAPS__SHIFT                                                             0x4
#define VPDSCL_TAP_CONTROL__SCL_V_NUM_TAPS_C__SHIFT                                                           0x8
#define VPDSCL_TAP_CONTROL__SCL_H_NUM_TAPS_C__SHIFT                                                           0xc
#define VPDSCL_TAP_CONTROL__SCL_V_NUM_TAPS_MASK                                                               0x00000007L
#define VPDSCL_TAP_CONTROL__SCL_H_NUM_TAPS_MASK                                                               0x00000070L
#define VPDSCL_TAP_CONTROL__SCL_V_NUM_TAPS_C_MASK                                                             0x00000700L
#define VPDSCL_TAP_CONTROL__SCL_H_NUM_TAPS_C_MASK                                                             0x00007000L
//VPDSCL_CONTROL
#define VPDSCL_CONTROL__SCL_BOUNDARY_MODE__SHIFT                                                              0x0
#define VPDSCL_CONTROL__SCL_BOUNDARY_MODE_MASK                                                                0x00000001L
//VPDSCL_2TAP_CONTROL
#define VPDSCL_2TAP_CONTROL__SCL_H_2TAP_HARDCODE_COEF_EN__SHIFT                                               0x0
#define VPDSCL_2TAP_CONTROL__SCL_H_2TAP_SHARP_EN__SHIFT                                                       0x4
#define VPDSCL_2TAP_CONTROL__SCL_H_2TAP_SHARP_FACTOR__SHIFT                                                   0x8
#define VPDSCL_2TAP_CONTROL__SCL_V_2TAP_HARDCODE_COEF_EN__SHIFT                                               0x10
#define VPDSCL_2TAP_CONTROL__SCL_V_2TAP_SHARP_EN__SHIFT                                                       0x14
#define VPDSCL_2TAP_CONTROL__SCL_V_2TAP_SHARP_FACTOR__SHIFT                                                   0x18
#define VPDSCL_2TAP_CONTROL__SCL_H_2TAP_HARDCODE_COEF_EN_MASK                                                 0x00000001L
#define VPDSCL_2TAP_CONTROL__SCL_H_2TAP_SHARP_EN_MASK                                                         0x00000010L
#define VPDSCL_2TAP_CONTROL__SCL_H_2TAP_SHARP_FACTOR_MASK                                                     0x00000700L
#define VPDSCL_2TAP_CONTROL__SCL_V_2TAP_HARDCODE_COEF_EN_MASK                                                 0x00010000L
#define VPDSCL_2TAP_CONTROL__SCL_V_2TAP_SHARP_EN_MASK                                                         0x00100000L
#define VPDSCL_2TAP_CONTROL__SCL_V_2TAP_SHARP_FACTOR_MASK                                                     0x07000000L
//VPDSCL_MANUAL_REPLICATE_CONTROL
#define VPDSCL_MANUAL_REPLICATE_CONTROL__SCL_V_MANUAL_REPLICATE_FACTOR__SHIFT                                 0x0
#define VPDSCL_MANUAL_REPLICATE_CONTROL__SCL_H_MANUAL_REPLICATE_FACTOR__SHIFT                                 0x8
#define VPDSCL_MANUAL_REPLICATE_CONTROL__SCL_V_MANUAL_REPLICATE_FACTOR_MASK                                   0x0000000FL
#define VPDSCL_MANUAL_REPLICATE_CONTROL__SCL_H_MANUAL_REPLICATE_FACTOR_MASK                                   0x00000F00L
//VPDSCL_HORZ_FILTER_SCALE_RATIO
#define VPDSCL_HORZ_FILTER_SCALE_RATIO__SCL_H_SCALE_RATIO__SHIFT                                              0x0
#define VPDSCL_HORZ_FILTER_SCALE_RATIO__SCL_H_SCALE_RATIO_MASK                                                0x07FFFFFFL
//VPDSCL_HORZ_FILTER_INIT
#define VPDSCL_HORZ_FILTER_INIT__SCL_H_INIT_FRAC__SHIFT                                                       0x0
#define VPDSCL_HORZ_FILTER_INIT__SCL_H_INIT_INT__SHIFT                                                        0x18
#define VPDSCL_HORZ_FILTER_INIT__SCL_H_INIT_FRAC_MASK                                                         0x00FFFFFFL
#define VPDSCL_HORZ_FILTER_INIT__SCL_H_INIT_INT_MASK                                                          0x0F000000L
//VPDSCL_HORZ_FILTER_SCALE_RATIO_C
#define VPDSCL_HORZ_FILTER_SCALE_RATIO_C__SCL_H_SCALE_RATIO_C__SHIFT                                          0x0
#define VPDSCL_HORZ_FILTER_SCALE_RATIO_C__SCL_H_SCALE_RATIO_C_MASK                                            0x07FFFFFFL
//VPDSCL_HORZ_FILTER_INIT_C
#define VPDSCL_HORZ_FILTER_INIT_C__SCL_H_INIT_FRAC_C__SHIFT                                                   0x0
#define VPDSCL_HORZ_FILTER_INIT_C__SCL_H_INIT_INT_C__SHIFT                                                    0x18
#define VPDSCL_HORZ_FILTER_INIT_C__SCL_H_INIT_FRAC_C_MASK                                                     0x00FFFFFFL
#define VPDSCL_HORZ_FILTER_INIT_C__SCL_H_INIT_INT_C_MASK                                                      0x0F000000L
//VPDSCL_VERT_FILTER_SCALE_RATIO
#define VPDSCL_VERT_FILTER_SCALE_RATIO__SCL_V_SCALE_RATIO__SHIFT                                              0x0
#define VPDSCL_VERT_FILTER_SCALE_RATIO__SCL_V_SCALE_RATIO_MASK                                                0x07FFFFFFL
//VPDSCL_VERT_FILTER_INIT
#define VPDSCL_VERT_FILTER_INIT__SCL_V_INIT_FRAC__SHIFT                                                       0x0
#define VPDSCL_VERT_FILTER_INIT__SCL_V_INIT_INT__SHIFT                                                        0x18
#define VPDSCL_VERT_FILTER_INIT__SCL_V_INIT_FRAC_MASK                                                         0x00FFFFFFL
#define VPDSCL_VERT_FILTER_INIT__SCL_V_INIT_INT_MASK                                                          0x0F000000L
//VPDSCL_VERT_FILTER_INIT_BOT
#define VPDSCL_VERT_FILTER_INIT_BOT__SCL_V_INIT_FRAC_BOT__SHIFT                                               0x0
#define VPDSCL_VERT_FILTER_INIT_BOT__SCL_V_INIT_INT_BOT__SHIFT                                                0x18
#define VPDSCL_VERT_FILTER_INIT_BOT__SCL_V_INIT_FRAC_BOT_MASK                                                 0x00FFFFFFL
#define VPDSCL_VERT_FILTER_INIT_BOT__SCL_V_INIT_INT_BOT_MASK                                                  0x0F000000L
//VPDSCL_VERT_FILTER_SCALE_RATIO_C
#define VPDSCL_VERT_FILTER_SCALE_RATIO_C__SCL_V_SCALE_RATIO_C__SHIFT                                          0x0
#define VPDSCL_VERT_FILTER_SCALE_RATIO_C__SCL_V_SCALE_RATIO_C_MASK                                            0x07FFFFFFL
//VPDSCL_VERT_FILTER_INIT_C
#define VPDSCL_VERT_FILTER_INIT_C__SCL_V_INIT_FRAC_C__SHIFT                                                   0x0
#define VPDSCL_VERT_FILTER_INIT_C__SCL_V_INIT_INT_C__SHIFT                                                    0x18
#define VPDSCL_VERT_FILTER_INIT_C__SCL_V_INIT_FRAC_C_MASK                                                     0x00FFFFFFL
#define VPDSCL_VERT_FILTER_INIT_C__SCL_V_INIT_INT_C_MASK                                                      0x0F000000L
//VPDSCL_VERT_FILTER_INIT_BOT_C
#define VPDSCL_VERT_FILTER_INIT_BOT_C__SCL_V_INIT_FRAC_BOT_C__SHIFT                                           0x0
#define VPDSCL_VERT_FILTER_INIT_BOT_C__SCL_V_INIT_INT_BOT_C__SHIFT                                            0x18
#define VPDSCL_VERT_FILTER_INIT_BOT_C__SCL_V_INIT_FRAC_BOT_C_MASK                                             0x00FFFFFFL
#define VPDSCL_VERT_FILTER_INIT_BOT_C__SCL_V_INIT_INT_BOT_C_MASK                                              0x0F000000L
//VPDSCL_BLACK_COLOR
#define VPDSCL_BLACK_COLOR__SCL_BLACK_COLOR_RGB_Y__SHIFT                                                      0x0
#define VPDSCL_BLACK_COLOR__SCL_BLACK_COLOR_CBCR__SHIFT                                                       0x10
#define VPDSCL_BLACK_COLOR__SCL_BLACK_COLOR_RGB_Y_MASK                                                        0x0000FFFFL
#define VPDSCL_BLACK_COLOR__SCL_BLACK_COLOR_CBCR_MASK                                                         0xFFFF0000L
//VPDSCL_UPDATE
#define VPDSCL_UPDATE__SCL_UPDATE_PENDING__SHIFT                                                              0x0
#define VPDSCL_UPDATE__SCL_UPDATE_PENDING_MASK                                                                0x00000001L
//VPDSCL_AUTOCAL
#define VPDSCL_AUTOCAL__AUTOCAL_MODE__SHIFT                                                                   0x0
#define VPDSCL_AUTOCAL__AUTOCAL_MODE_MASK                                                                     0x00000003L
//VPDSCL_EXT_OVERSCAN_LEFT_RIGHT
#define VPDSCL_EXT_OVERSCAN_LEFT_RIGHT__EXT_OVERSCAN_RIGHT__SHIFT                                             0x0
#define VPDSCL_EXT_OVERSCAN_LEFT_RIGHT__EXT_OVERSCAN_LEFT__SHIFT                                              0x10
#define VPDSCL_EXT_OVERSCAN_LEFT_RIGHT__EXT_OVERSCAN_RIGHT_MASK                                               0x00001FFFL
#define VPDSCL_EXT_OVERSCAN_LEFT_RIGHT__EXT_OVERSCAN_LEFT_MASK                                                0x1FFF0000L
//VPDSCL_EXT_OVERSCAN_TOP_BOTTOM
#define VPDSCL_EXT_OVERSCAN_TOP_BOTTOM__EXT_OVERSCAN_BOTTOM__SHIFT                                            0x0
#define VPDSCL_EXT_OVERSCAN_TOP_BOTTOM__EXT_OVERSCAN_TOP__SHIFT                                               0x10
#define VPDSCL_EXT_OVERSCAN_TOP_BOTTOM__EXT_OVERSCAN_BOTTOM_MASK                                              0x00001FFFL
#define VPDSCL_EXT_OVERSCAN_TOP_BOTTOM__EXT_OVERSCAN_TOP_MASK                                                 0x1FFF0000L
//VPOTG_H_BLANK
#define VPOTG_H_BLANK__OTG_H_BLANK_START__SHIFT                                                               0x0
#define VPOTG_H_BLANK__OTG_H_BLANK_END__SHIFT                                                                 0x10
#define VPOTG_H_BLANK__OTG_H_BLANK_START_MASK                                                                 0x00003FFFL
#define VPOTG_H_BLANK__OTG_H_BLANK_END_MASK                                                                   0x3FFF0000L
//VPOTG_V_BLANK
#define VPOTG_V_BLANK__OTG_V_BLANK_START__SHIFT                                                               0x0
#define VPOTG_V_BLANK__OTG_V_BLANK_END__SHIFT                                                                 0x10
#define VPOTG_V_BLANK__OTG_V_BLANK_START_MASK                                                                 0x00003FFFL
#define VPOTG_V_BLANK__OTG_V_BLANK_END_MASK                                                                   0x3FFF0000L
//VPDSCL_RECOUT_START
#define VPDSCL_RECOUT_START__RECOUT_START_X__SHIFT                                                            0x0
#define VPDSCL_RECOUT_START__RECOUT_START_Y__SHIFT                                                            0x10
#define VPDSCL_RECOUT_START__RECOUT_START_X_MASK                                                              0x00001FFFL
#define VPDSCL_RECOUT_START__RECOUT_START_Y_MASK                                                              0x1FFF0000L
//VPDSCL_RECOUT_SIZE
#define VPDSCL_RECOUT_SIZE__RECOUT_WIDTH__SHIFT                                                               0x0
#define VPDSCL_RECOUT_SIZE__RECOUT_HEIGHT__SHIFT                                                              0x10
#define VPDSCL_RECOUT_SIZE__RECOUT_WIDTH_MASK                                                                 0x00003FFFL
#define VPDSCL_RECOUT_SIZE__RECOUT_HEIGHT_MASK                                                                0x3FFF0000L
//VPMPC_SIZE
#define VPMPC_SIZE__VPMPC_WIDTH__SHIFT                                                                        0x0
#define VPMPC_SIZE__VPMPC_HEIGHT__SHIFT                                                                       0x10
#define VPMPC_SIZE__VPMPC_WIDTH_MASK                                                                          0x00003FFFL
#define VPMPC_SIZE__VPMPC_HEIGHT_MASK                                                                         0x3FFF0000L
//VPLB_DATA_FORMAT
#define VPLB_DATA_FORMAT__ALPHA_EN__SHIFT                                                                     0x4
#define VPLB_DATA_FORMAT__ALPHA_EN_MASK                                                                       0x00000010L
//VPLB_MEMORY_CTRL
#define VPLB_MEMORY_CTRL__MEMORY_CONFIG__SHIFT                                                                0x0
#define VPLB_MEMORY_CTRL__LB_MAX_PARTITIONS__SHIFT                                                            0x8
#define VPLB_MEMORY_CTRL__LB_NUM_PARTITIONS__SHIFT                                                            0x10
#define VPLB_MEMORY_CTRL__LB_NUM_PARTITIONS_C__SHIFT                                                          0x18
#define VPLB_MEMORY_CTRL__MEMORY_CONFIG_MASK                                                                  0x00000003L
#define VPLB_MEMORY_CTRL__LB_MAX_PARTITIONS_MASK                                                              0x00003F00L
#define VPLB_MEMORY_CTRL__LB_NUM_PARTITIONS_MASK                                                              0x007F0000L
#define VPLB_MEMORY_CTRL__LB_NUM_PARTITIONS_C_MASK                                                            0x7F000000L
//VPLB_V_COUNTER
#define VPLB_V_COUNTER__V_COUNTER__SHIFT                                                                      0x0
#define VPLB_V_COUNTER__V_COUNTER_C__SHIFT                                                                    0x10
#define VPLB_V_COUNTER__V_COUNTER_MASK                                                                        0x00001FFFL
#define VPLB_V_COUNTER__V_COUNTER_C_MASK                                                                      0x1FFF0000L
//VPDSCL_MEM_PWR_CTRL
#define VPDSCL_MEM_PWR_CTRL__LUT_MEM_PWR_FORCE__SHIFT                                                         0x0
#define VPDSCL_MEM_PWR_CTRL__LUT_MEM_PWR_DIS__SHIFT                                                           0x2
#define VPDSCL_MEM_PWR_CTRL__LB_G1_MEM_PWR_FORCE__SHIFT                                                       0x4
#define VPDSCL_MEM_PWR_CTRL__LB_G1_MEM_PWR_DIS__SHIFT                                                         0x6
#define VPDSCL_MEM_PWR_CTRL__LB_G2_MEM_PWR_FORCE__SHIFT                                                       0x8
#define VPDSCL_MEM_PWR_CTRL__LB_G2_MEM_PWR_DIS__SHIFT                                                         0xa
#define VPDSCL_MEM_PWR_CTRL__LB_MEM_PWR_MODE__SHIFT                                                           0x1c
#define VPDSCL_MEM_PWR_CTRL__LUT_MEM_PWR_FORCE_MASK                                                           0x00000003L
#define VPDSCL_MEM_PWR_CTRL__LUT_MEM_PWR_DIS_MASK                                                             0x00000004L
#define VPDSCL_MEM_PWR_CTRL__LB_G1_MEM_PWR_FORCE_MASK                                                         0x00000030L
#define VPDSCL_MEM_PWR_CTRL__LB_G1_MEM_PWR_DIS_MASK                                                           0x00000040L
#define VPDSCL_MEM_PWR_CTRL__LB_G2_MEM_PWR_FORCE_MASK                                                         0x00000300L
#define VPDSCL_MEM_PWR_CTRL__LB_G2_MEM_PWR_DIS_MASK                                                           0x00000400L
#define VPDSCL_MEM_PWR_CTRL__LB_MEM_PWR_MODE_MASK                                                             0x10000000L
//VPDSCL_MEM_PWR_STATUS
#define VPDSCL_MEM_PWR_STATUS__LUT_MEM_PWR_STATE__SHIFT                                                       0x0
#define VPDSCL_MEM_PWR_STATUS__LB_G1_MEM_PWR_STATE__SHIFT                                                     0x2
#define VPDSCL_MEM_PWR_STATUS__LB_G2_MEM_PWR_STATE__SHIFT                                                     0x4
#define VPDSCL_MEM_PWR_STATUS__LUT_MEM_PWR_STATE_MASK                                                         0x00000003L
#define VPDSCL_MEM_PWR_STATUS__LB_G1_MEM_PWR_STATE_MASK                                                       0x0000000CL
#define VPDSCL_MEM_PWR_STATUS__LB_G2_MEM_PWR_STATE_MASK                                                       0x00000030L


// addressBlock: vpe_vpep_vpdpp0_dispdec_vpcm_dispdec
//VPCM_CONTROL
#define VPCM_CONTROL__VPCM_BYPASS__SHIFT                                                                      0x0
#define VPCM_CONTROL__VPCM_UPDATE_PENDING__SHIFT                                                              0x8
#define VPCM_CONTROL__VPCM_BYPASS_MASK                                                                        0x00000001L
#define VPCM_CONTROL__VPCM_UPDATE_PENDING_MASK                                                                0x00000100L
//VPCM_POST_CSC_CONTROL
#define VPCM_POST_CSC_CONTROL__VPCM_POST_CSC_MODE__SHIFT                                                      0x0
#define VPCM_POST_CSC_CONTROL__VPCM_POST_CSC_MODE_CURRENT__SHIFT                                              0x2
#define VPCM_POST_CSC_CONTROL__VPCM_POST_CSC_MODE_MASK                                                        0x00000001L
#define VPCM_POST_CSC_CONTROL__VPCM_POST_CSC_MODE_CURRENT_MASK                                                0x00000004L
//VPCM_POST_CSC_C11_C12
#define VPCM_POST_CSC_C11_C12__VPCM_POST_CSC_C11__SHIFT                                                       0x0
#define VPCM_POST_CSC_C11_C12__VPCM_POST_CSC_C12__SHIFT                                                       0x10
#define VPCM_POST_CSC_C11_C12__VPCM_POST_CSC_C11_MASK                                                         0x0000FFFFL
#define VPCM_POST_CSC_C11_C12__VPCM_POST_CSC_C12_MASK                                                         0xFFFF0000L
//VPCM_POST_CSC_C13_C14
#define VPCM_POST_CSC_C13_C14__VPCM_POST_CSC_C13__SHIFT                                                       0x0
#define VPCM_POST_CSC_C13_C14__VPCM_POST_CSC_C14__SHIFT                                                       0x10
#define VPCM_POST_CSC_C13_C14__VPCM_POST_CSC_C13_MASK                                                         0x0000FFFFL
#define VPCM_POST_CSC_C13_C14__VPCM_POST_CSC_C14_MASK                                                         0xFFFF0000L
//VPCM_POST_CSC_C21_C22
#define VPCM_POST_CSC_C21_C22__VPCM_POST_CSC_C21__SHIFT                                                       0x0
#define VPCM_POST_CSC_C21_C22__VPCM_POST_CSC_C22__SHIFT                                                       0x10
#define VPCM_POST_CSC_C21_C22__VPCM_POST_CSC_C21_MASK                                                         0x0000FFFFL
#define VPCM_POST_CSC_C21_C22__VPCM_POST_CSC_C22_MASK                                                         0xFFFF0000L
//VPCM_POST_CSC_C23_C24
#define VPCM_POST_CSC_C23_C24__VPCM_POST_CSC_C23__SHIFT                                                       0x0
#define VPCM_POST_CSC_C23_C24__VPCM_POST_CSC_C24__SHIFT                                                       0x10
#define VPCM_POST_CSC_C23_C24__VPCM_POST_CSC_C23_MASK                                                         0x0000FFFFL
#define VPCM_POST_CSC_C23_C24__VPCM_POST_CSC_C24_MASK                                                         0xFFFF0000L
//VPCM_POST_CSC_C31_C32
#define VPCM_POST_CSC_C31_C32__VPCM_POST_CSC_C31__SHIFT                                                       0x0
#define VPCM_POST_CSC_C31_C32__VPCM_POST_CSC_C32__SHIFT                                                       0x10
#define VPCM_POST_CSC_C31_C32__VPCM_POST_CSC_C31_MASK                                                         0x0000FFFFL
#define VPCM_POST_CSC_C31_C32__VPCM_POST_CSC_C32_MASK                                                         0xFFFF0000L
//VPCM_POST_CSC_C33_C34
#define VPCM_POST_CSC_C33_C34__VPCM_POST_CSC_C33__SHIFT                                                       0x0
#define VPCM_POST_CSC_C33_C34__VPCM_POST_CSC_C34__SHIFT                                                       0x10
#define VPCM_POST_CSC_C33_C34__VPCM_POST_CSC_C33_MASK                                                         0x0000FFFFL
#define VPCM_POST_CSC_C33_C34__VPCM_POST_CSC_C34_MASK                                                         0xFFFF0000L
//VPCM_GAMUT_REMAP_CONTROL
#define VPCM_GAMUT_REMAP_CONTROL__VPCM_GAMUT_REMAP_MODE__SHIFT                                                0x0
#define VPCM_GAMUT_REMAP_CONTROL__VPCM_GAMUT_REMAP_MODE_CURRENT__SHIFT                                        0x2
#define VPCM_GAMUT_REMAP_CONTROL__VPCM_GAMUT_REMAP_MODE_MASK                                                  0x00000001L
#define VPCM_GAMUT_REMAP_CONTROL__VPCM_GAMUT_REMAP_MODE_CURRENT_MASK                                          0x00000004L
//VPCM_GAMUT_REMAP_C11_C12
#define VPCM_GAMUT_REMAP_C11_C12__VPCM_GAMUT_REMAP_C11__SHIFT                                                 0x0
#define VPCM_GAMUT_REMAP_C11_C12__VPCM_GAMUT_REMAP_C12__SHIFT                                                 0x10
#define VPCM_GAMUT_REMAP_C11_C12__VPCM_GAMUT_REMAP_C11_MASK                                                   0x0000FFFFL
#define VPCM_GAMUT_REMAP_C11_C12__VPCM_GAMUT_REMAP_C12_MASK                                                   0xFFFF0000L
//VPCM_GAMUT_REMAP_C13_C14
#define VPCM_GAMUT_REMAP_C13_C14__VPCM_GAMUT_REMAP_C13__SHIFT                                                 0x0
#define VPCM_GAMUT_REMAP_C13_C14__VPCM_GAMUT_REMAP_C14__SHIFT                                                 0x10
#define VPCM_GAMUT_REMAP_C13_C14__VPCM_GAMUT_REMAP_C13_MASK                                                   0x0000FFFFL
#define VPCM_GAMUT_REMAP_C13_C14__VPCM_GAMUT_REMAP_C14_MASK                                                   0xFFFF0000L
//VPCM_GAMUT_REMAP_C21_C22
#define VPCM_GAMUT_REMAP_C21_C22__VPCM_GAMUT_REMAP_C21__SHIFT                                                 0x0
#define VPCM_GAMUT_REMAP_C21_C22__VPCM_GAMUT_REMAP_C22__SHIFT                                                 0x10
#define VPCM_GAMUT_REMAP_C21_C22__VPCM_GAMUT_REMAP_C21_MASK                                                   0x0000FFFFL
#define VPCM_GAMUT_REMAP_C21_C22__VPCM_GAMUT_REMAP_C22_MASK                                                   0xFFFF0000L
//VPCM_GAMUT_REMAP_C23_C24
#define VPCM_GAMUT_REMAP_C23_C24__VPCM_GAMUT_REMAP_C23__SHIFT                                                 0x0
#define VPCM_GAMUT_REMAP_C23_C24__VPCM_GAMUT_REMAP_C24__SHIFT                                                 0x10
#define VPCM_GAMUT_REMAP_C23_C24__VPCM_GAMUT_REMAP_C23_MASK                                                   0x0000FFFFL
#define VPCM_GAMUT_REMAP_C23_C24__VPCM_GAMUT_REMAP_C24_MASK                                                   0xFFFF0000L
//VPCM_GAMUT_REMAP_C31_C32
#define VPCM_GAMUT_REMAP_C31_C32__VPCM_GAMUT_REMAP_C31__SHIFT                                                 0x0
#define VPCM_GAMUT_REMAP_C31_C32__VPCM_GAMUT_REMAP_C32__SHIFT                                                 0x10
#define VPCM_GAMUT_REMAP_C31_C32__VPCM_GAMUT_REMAP_C31_MASK                                                   0x0000FFFFL
#define VPCM_GAMUT_REMAP_C31_C32__VPCM_GAMUT_REMAP_C32_MASK                                                   0xFFFF0000L
//VPCM_GAMUT_REMAP_C33_C34
#define VPCM_GAMUT_REMAP_C33_C34__VPCM_GAMUT_REMAP_C33__SHIFT                                                 0x0
#define VPCM_GAMUT_REMAP_C33_C34__VPCM_GAMUT_REMAP_C34__SHIFT                                                 0x10
#define VPCM_GAMUT_REMAP_C33_C34__VPCM_GAMUT_REMAP_C33_MASK                                                   0x0000FFFFL
#define VPCM_GAMUT_REMAP_C33_C34__VPCM_GAMUT_REMAP_C34_MASK                                                   0xFFFF0000L
//VPCM_BIAS_CR_R
#define VPCM_BIAS_CR_R__VPCM_BIAS_CR_R__SHIFT                                                                 0x0
#define VPCM_BIAS_CR_R__VPCM_BIAS_CR_R_MASK                                                                   0x0000FFFFL
//VPCM_BIAS_Y_G_CB_B
#define VPCM_BIAS_Y_G_CB_B__VPCM_BIAS_Y_G__SHIFT                                                              0x0
#define VPCM_BIAS_Y_G_CB_B__VPCM_BIAS_CB_B__SHIFT                                                             0x10
#define VPCM_BIAS_Y_G_CB_B__VPCM_BIAS_Y_G_MASK                                                                0x0000FFFFL
#define VPCM_BIAS_Y_G_CB_B__VPCM_BIAS_CB_B_MASK                                                               0xFFFF0000L
//VPCM_GAMCOR_CONTROL
#define VPCM_GAMCOR_CONTROL__VPCM_GAMCOR_MODE__SHIFT                                                          0x0
#define VPCM_GAMCOR_CONTROL__VPCM_GAMCOR_PWL_DISABLE__SHIFT                                                   0x3
#define VPCM_GAMCOR_CONTROL__VPCM_GAMCOR_MODE_CURRENT__SHIFT                                                  0x4
#define VPCM_GAMCOR_CONTROL__VPCM_GAMCOR_SELECT_CURRENT__SHIFT                                                0x6
#define VPCM_GAMCOR_CONTROL__VPCM_GAMCOR_MODE_MASK                                                            0x00000003L
#define VPCM_GAMCOR_CONTROL__VPCM_GAMCOR_PWL_DISABLE_MASK                                                     0x00000008L
#define VPCM_GAMCOR_CONTROL__VPCM_GAMCOR_MODE_CURRENT_MASK                                                    0x00000030L
#define VPCM_GAMCOR_CONTROL__VPCM_GAMCOR_SELECT_CURRENT_MASK                                                  0x00000040L
//VPCM_GAMCOR_LUT_INDEX
#define VPCM_GAMCOR_LUT_INDEX__VPCM_GAMCOR_LUT_INDEX__SHIFT                                                   0x0
#define VPCM_GAMCOR_LUT_INDEX__VPCM_GAMCOR_LUT_INDEX_MASK                                                     0x000001FFL
//VPCM_GAMCOR_LUT_DATA
#define VPCM_GAMCOR_LUT_DATA__VPCM_GAMCOR_LUT_DATA__SHIFT                                                     0x0
#define VPCM_GAMCOR_LUT_DATA__VPCM_GAMCOR_LUT_DATA_MASK                                                       0x0003FFFFL
//VPCM_GAMCOR_LUT_CONTROL
#define VPCM_GAMCOR_LUT_CONTROL__VPCM_GAMCOR_LUT_WRITE_COLOR_MASK__SHIFT                                      0x0
#define VPCM_GAMCOR_LUT_CONTROL__VPCM_GAMCOR_LUT_READ_COLOR_SEL__SHIFT                                        0x3
#define VPCM_GAMCOR_LUT_CONTROL__VPCM_GAMCOR_LUT_READ_DBG__SHIFT                                              0x5
#define VPCM_GAMCOR_LUT_CONTROL__VPCM_GAMCOR_LUT_HOST_SEL__SHIFT                                              0x6
#define VPCM_GAMCOR_LUT_CONTROL__VPCM_GAMCOR_LUT_CONFIG_MODE__SHIFT                                           0x7
#define VPCM_GAMCOR_LUT_CONTROL__VPCM_GAMCOR_LUT_WRITE_COLOR_MASK_MASK                                        0x00000007L
#define VPCM_GAMCOR_LUT_CONTROL__VPCM_GAMCOR_LUT_READ_COLOR_SEL_MASK                                          0x00000018L
#define VPCM_GAMCOR_LUT_CONTROL__VPCM_GAMCOR_LUT_READ_DBG_MASK                                                0x00000020L
#define VPCM_GAMCOR_LUT_CONTROL__VPCM_GAMCOR_LUT_HOST_SEL_MASK                                                0x00000040L
#define VPCM_GAMCOR_LUT_CONTROL__VPCM_GAMCOR_LUT_CONFIG_MODE_MASK                                             0x00000080L
//VPCM_GAMCOR_RAMA_START_CNTL_B
#define VPCM_GAMCOR_RAMA_START_CNTL_B__VPCM_GAMCOR_RAMA_EXP_REGION_START_B__SHIFT                             0x0
#define VPCM_GAMCOR_RAMA_START_CNTL_B__VPCM_GAMCOR_RAMA_EXP_REGION_START_SEGMENT_B__SHIFT                     0x14
#define VPCM_GAMCOR_RAMA_START_CNTL_B__VPCM_GAMCOR_RAMA_EXP_REGION_START_B_MASK                               0x0003FFFFL
#define VPCM_GAMCOR_RAMA_START_CNTL_B__VPCM_GAMCOR_RAMA_EXP_REGION_START_SEGMENT_B_MASK                       0x07F00000L
//VPCM_GAMCOR_RAMA_START_CNTL_G
#define VPCM_GAMCOR_RAMA_START_CNTL_G__VPCM_GAMCOR_RAMA_EXP_REGION_START_G__SHIFT                             0x0
#define VPCM_GAMCOR_RAMA_START_CNTL_G__VPCM_GAMCOR_RAMA_EXP_REGION_START_SEGMENT_G__SHIFT                     0x14
#define VPCM_GAMCOR_RAMA_START_CNTL_G__VPCM_GAMCOR_RAMA_EXP_REGION_START_G_MASK                               0x0003FFFFL
#define VPCM_GAMCOR_RAMA_START_CNTL_G__VPCM_GAMCOR_RAMA_EXP_REGION_START_SEGMENT_G_MASK                       0x07F00000L
//VPCM_GAMCOR_RAMA_START_CNTL_R
#define VPCM_GAMCOR_RAMA_START_CNTL_R__VPCM_GAMCOR_RAMA_EXP_REGION_START_R__SHIFT                             0x0
#define VPCM_GAMCOR_RAMA_START_CNTL_R__VPCM_GAMCOR_RAMA_EXP_REGION_START_SEGMENT_R__SHIFT                     0x14
#define VPCM_GAMCOR_RAMA_START_CNTL_R__VPCM_GAMCOR_RAMA_EXP_REGION_START_R_MASK                               0x0003FFFFL
#define VPCM_GAMCOR_RAMA_START_CNTL_R__VPCM_GAMCOR_RAMA_EXP_REGION_START_SEGMENT_R_MASK                       0x07F00000L
//VPCM_GAMCOR_RAMA_START_SLOPE_CNTL_B
#define VPCM_GAMCOR_RAMA_START_SLOPE_CNTL_B__VPCM_GAMCOR_RAMA_EXP_REGION_START_SLOPE_B__SHIFT                 0x0
#define VPCM_GAMCOR_RAMA_START_SLOPE_CNTL_B__VPCM_GAMCOR_RAMA_EXP_REGION_START_SLOPE_B_MASK                   0x0003FFFFL
//VPCM_GAMCOR_RAMA_START_SLOPE_CNTL_G
#define VPCM_GAMCOR_RAMA_START_SLOPE_CNTL_G__VPCM_GAMCOR_RAMA_EXP_REGION_START_SLOPE_G__SHIFT                 0x0
#define VPCM_GAMCOR_RAMA_START_SLOPE_CNTL_G__VPCM_GAMCOR_RAMA_EXP_REGION_START_SLOPE_G_MASK                   0x0003FFFFL
//VPCM_GAMCOR_RAMA_START_SLOPE_CNTL_R
#define VPCM_GAMCOR_RAMA_START_SLOPE_CNTL_R__VPCM_GAMCOR_RAMA_EXP_REGION_START_SLOPE_R__SHIFT                 0x0
#define VPCM_GAMCOR_RAMA_START_SLOPE_CNTL_R__VPCM_GAMCOR_RAMA_EXP_REGION_START_SLOPE_R_MASK                   0x0003FFFFL
//VPCM_GAMCOR_RAMA_START_BASE_CNTL_B
#define VPCM_GAMCOR_RAMA_START_BASE_CNTL_B__VPCM_GAMCOR_RAMA_EXP_REGION_START_BASE_B__SHIFT                   0x0
#define VPCM_GAMCOR_RAMA_START_BASE_CNTL_B__VPCM_GAMCOR_RAMA_EXP_REGION_START_BASE_B_MASK                     0x0003FFFFL
//VPCM_GAMCOR_RAMA_START_BASE_CNTL_G
#define VPCM_GAMCOR_RAMA_START_BASE_CNTL_G__VPCM_GAMCOR_RAMA_EXP_REGION_START_BASE_G__SHIFT                   0x0
#define VPCM_GAMCOR_RAMA_START_BASE_CNTL_G__VPCM_GAMCOR_RAMA_EXP_REGION_START_BASE_G_MASK                     0x0003FFFFL
//VPCM_GAMCOR_RAMA_START_BASE_CNTL_R
#define VPCM_GAMCOR_RAMA_START_BASE_CNTL_R__VPCM_GAMCOR_RAMA_EXP_REGION_START_BASE_R__SHIFT                   0x0
#define VPCM_GAMCOR_RAMA_START_BASE_CNTL_R__VPCM_GAMCOR_RAMA_EXP_REGION_START_BASE_R_MASK                     0x0003FFFFL
//VPCM_GAMCOR_RAMA_END_CNTL1_B
#define VPCM_GAMCOR_RAMA_END_CNTL1_B__VPCM_GAMCOR_RAMA_EXP_REGION_END_BASE_B__SHIFT                           0x0
#define VPCM_GAMCOR_RAMA_END_CNTL1_B__VPCM_GAMCOR_RAMA_EXP_REGION_END_BASE_B_MASK                             0x0003FFFFL
//VPCM_GAMCOR_RAMA_END_CNTL2_B
#define VPCM_GAMCOR_RAMA_END_CNTL2_B__VPCM_GAMCOR_RAMA_EXP_REGION_END_B__SHIFT                                0x0
#define VPCM_GAMCOR_RAMA_END_CNTL2_B__VPCM_GAMCOR_RAMA_EXP_REGION_END_SLOPE_B__SHIFT                          0x10
#define VPCM_GAMCOR_RAMA_END_CNTL2_B__VPCM_GAMCOR_RAMA_EXP_REGION_END_B_MASK                                  0x0000FFFFL
#define VPCM_GAMCOR_RAMA_END_CNTL2_B__VPCM_GAMCOR_RAMA_EXP_REGION_END_SLOPE_B_MASK                            0xFFFF0000L
//VPCM_GAMCOR_RAMA_END_CNTL1_G
#define VPCM_GAMCOR_RAMA_END_CNTL1_G__VPCM_GAMCOR_RAMA_EXP_REGION_END_BASE_G__SHIFT                           0x0
#define VPCM_GAMCOR_RAMA_END_CNTL1_G__VPCM_GAMCOR_RAMA_EXP_REGION_END_BASE_G_MASK                             0x0003FFFFL
//VPCM_GAMCOR_RAMA_END_CNTL2_G
#define VPCM_GAMCOR_RAMA_END_CNTL2_G__VPCM_GAMCOR_RAMA_EXP_REGION_END_G__SHIFT                                0x0
#define VPCM_GAMCOR_RAMA_END_CNTL2_G__VPCM_GAMCOR_RAMA_EXP_REGION_END_SLOPE_G__SHIFT                          0x10
#define VPCM_GAMCOR_RAMA_END_CNTL2_G__VPCM_GAMCOR_RAMA_EXP_REGION_END_G_MASK                                  0x0000FFFFL
#define VPCM_GAMCOR_RAMA_END_CNTL2_G__VPCM_GAMCOR_RAMA_EXP_REGION_END_SLOPE_G_MASK                            0xFFFF0000L
//VPCM_GAMCOR_RAMA_END_CNTL1_R
#define VPCM_GAMCOR_RAMA_END_CNTL1_R__VPCM_GAMCOR_RAMA_EXP_REGION_END_BASE_R__SHIFT                           0x0
#define VPCM_GAMCOR_RAMA_END_CNTL1_R__VPCM_GAMCOR_RAMA_EXP_REGION_END_BASE_R_MASK                             0x0003FFFFL
//VPCM_GAMCOR_RAMA_END_CNTL2_R
#define VPCM_GAMCOR_RAMA_END_CNTL2_R__VPCM_GAMCOR_RAMA_EXP_REGION_END_R__SHIFT                                0x0
#define VPCM_GAMCOR_RAMA_END_CNTL2_R__VPCM_GAMCOR_RAMA_EXP_REGION_END_SLOPE_R__SHIFT                          0x10
#define VPCM_GAMCOR_RAMA_END_CNTL2_R__VPCM_GAMCOR_RAMA_EXP_REGION_END_R_MASK                                  0x0000FFFFL
#define VPCM_GAMCOR_RAMA_END_CNTL2_R__VPCM_GAMCOR_RAMA_EXP_REGION_END_SLOPE_R_MASK                            0xFFFF0000L
//VPCM_GAMCOR_RAMA_OFFSET_B
#define VPCM_GAMCOR_RAMA_OFFSET_B__VPCM_GAMCOR_RAMA_OFFSET_B__SHIFT                                           0x0
#define VPCM_GAMCOR_RAMA_OFFSET_B__VPCM_GAMCOR_RAMA_OFFSET_B_MASK                                             0x0007FFFFL
//VPCM_GAMCOR_RAMA_OFFSET_G
#define VPCM_GAMCOR_RAMA_OFFSET_G__VPCM_GAMCOR_RAMA_OFFSET_G__SHIFT                                           0x0
#define VPCM_GAMCOR_RAMA_OFFSET_G__VPCM_GAMCOR_RAMA_OFFSET_G_MASK                                             0x0007FFFFL
//VPCM_GAMCOR_RAMA_OFFSET_R
#define VPCM_GAMCOR_RAMA_OFFSET_R__VPCM_GAMCOR_RAMA_OFFSET_R__SHIFT                                           0x0
#define VPCM_GAMCOR_RAMA_OFFSET_R__VPCM_GAMCOR_RAMA_OFFSET_R_MASK                                             0x0007FFFFL
//VPCM_GAMCOR_RAMA_REGION_0_1
#define VPCM_GAMCOR_RAMA_REGION_0_1__VPCM_GAMCOR_RAMA_EXP_REGION0_LUT_OFFSET__SHIFT                           0x0
#define VPCM_GAMCOR_RAMA_REGION_0_1__VPCM_GAMCOR_RAMA_EXP_REGION0_NUM_SEGMENTS__SHIFT                         0xc
#define VPCM_GAMCOR_RAMA_REGION_0_1__VPCM_GAMCOR_RAMA_EXP_REGION1_LUT_OFFSET__SHIFT                           0x10
#define VPCM_GAMCOR_RAMA_REGION_0_1__VPCM_GAMCOR_RAMA_EXP_REGION1_NUM_SEGMENTS__SHIFT                         0x1c
#define VPCM_GAMCOR_RAMA_REGION_0_1__VPCM_GAMCOR_RAMA_EXP_REGION0_LUT_OFFSET_MASK                             0x000001FFL
#define VPCM_GAMCOR_RAMA_REGION_0_1__VPCM_GAMCOR_RAMA_EXP_REGION0_NUM_SEGMENTS_MASK                           0x00007000L
#define VPCM_GAMCOR_RAMA_REGION_0_1__VPCM_GAMCOR_RAMA_EXP_REGION1_LUT_OFFSET_MASK                             0x01FF0000L
#define VPCM_GAMCOR_RAMA_REGION_0_1__VPCM_GAMCOR_RAMA_EXP_REGION1_NUM_SEGMENTS_MASK                           0x70000000L
//VPCM_GAMCOR_RAMA_REGION_2_3
#define VPCM_GAMCOR_RAMA_REGION_2_3__VPCM_GAMCOR_RAMA_EXP_REGION2_LUT_OFFSET__SHIFT                           0x0
#define VPCM_GAMCOR_RAMA_REGION_2_3__VPCM_GAMCOR_RAMA_EXP_REGION2_NUM_SEGMENTS__SHIFT                         0xc
#define VPCM_GAMCOR_RAMA_REGION_2_3__VPCM_GAMCOR_RAMA_EXP_REGION3_LUT_OFFSET__SHIFT                           0x10
#define VPCM_GAMCOR_RAMA_REGION_2_3__VPCM_GAMCOR_RAMA_EXP_REGION3_NUM_SEGMENTS__SHIFT                         0x1c
#define VPCM_GAMCOR_RAMA_REGION_2_3__VPCM_GAMCOR_RAMA_EXP_REGION2_LUT_OFFSET_MASK                             0x000001FFL
#define VPCM_GAMCOR_RAMA_REGION_2_3__VPCM_GAMCOR_RAMA_EXP_REGION2_NUM_SEGMENTS_MASK                           0x00007000L
#define VPCM_GAMCOR_RAMA_REGION_2_3__VPCM_GAMCOR_RAMA_EXP_REGION3_LUT_OFFSET_MASK                             0x01FF0000L
#define VPCM_GAMCOR_RAMA_REGION_2_3__VPCM_GAMCOR_RAMA_EXP_REGION3_NUM_SEGMENTS_MASK                           0x70000000L
//VPCM_GAMCOR_RAMA_REGION_4_5
#define VPCM_GAMCOR_RAMA_REGION_4_5__VPCM_GAMCOR_RAMA_EXP_REGION4_LUT_OFFSET__SHIFT                           0x0
#define VPCM_GAMCOR_RAMA_REGION_4_5__VPCM_GAMCOR_RAMA_EXP_REGION4_NUM_SEGMENTS__SHIFT                         0xc
#define VPCM_GAMCOR_RAMA_REGION_4_5__VPCM_GAMCOR_RAMA_EXP_REGION5_LUT_OFFSET__SHIFT                           0x10
#define VPCM_GAMCOR_RAMA_REGION_4_5__VPCM_GAMCOR_RAMA_EXP_REGION5_NUM_SEGMENTS__SHIFT                         0x1c
#define VPCM_GAMCOR_RAMA_REGION_4_5__VPCM_GAMCOR_RAMA_EXP_REGION4_LUT_OFFSET_MASK                             0x000001FFL
#define VPCM_GAMCOR_RAMA_REGION_4_5__VPCM_GAMCOR_RAMA_EXP_REGION4_NUM_SEGMENTS_MASK                           0x00007000L
#define VPCM_GAMCOR_RAMA_REGION_4_5__VPCM_GAMCOR_RAMA_EXP_REGION5_LUT_OFFSET_MASK                             0x01FF0000L
#define VPCM_GAMCOR_RAMA_REGION_4_5__VPCM_GAMCOR_RAMA_EXP_REGION5_NUM_SEGMENTS_MASK                           0x70000000L
//VPCM_GAMCOR_RAMA_REGION_6_7
#define VPCM_GAMCOR_RAMA_REGION_6_7__VPCM_GAMCOR_RAMA_EXP_REGION6_LUT_OFFSET__SHIFT                           0x0
#define VPCM_GAMCOR_RAMA_REGION_6_7__VPCM_GAMCOR_RAMA_EXP_REGION6_NUM_SEGMENTS__SHIFT                         0xc
#define VPCM_GAMCOR_RAMA_REGION_6_7__VPCM_GAMCOR_RAMA_EXP_REGION7_LUT_OFFSET__SHIFT                           0x10
#define VPCM_GAMCOR_RAMA_REGION_6_7__VPCM_GAMCOR_RAMA_EXP_REGION7_NUM_SEGMENTS__SHIFT                         0x1c
#define VPCM_GAMCOR_RAMA_REGION_6_7__VPCM_GAMCOR_RAMA_EXP_REGION6_LUT_OFFSET_MASK                             0x000001FFL
#define VPCM_GAMCOR_RAMA_REGION_6_7__VPCM_GAMCOR_RAMA_EXP_REGION6_NUM_SEGMENTS_MASK                           0x00007000L
#define VPCM_GAMCOR_RAMA_REGION_6_7__VPCM_GAMCOR_RAMA_EXP_REGION7_LUT_OFFSET_MASK                             0x01FF0000L
#define VPCM_GAMCOR_RAMA_REGION_6_7__VPCM_GAMCOR_RAMA_EXP_REGION7_NUM_SEGMENTS_MASK                           0x70000000L
//VPCM_GAMCOR_RAMA_REGION_8_9
#define VPCM_GAMCOR_RAMA_REGION_8_9__VPCM_GAMCOR_RAMA_EXP_REGION8_LUT_OFFSET__SHIFT                           0x0
#define VPCM_GAMCOR_RAMA_REGION_8_9__VPCM_GAMCOR_RAMA_EXP_REGION8_NUM_SEGMENTS__SHIFT                         0xc
#define VPCM_GAMCOR_RAMA_REGION_8_9__VPCM_GAMCOR_RAMA_EXP_REGION9_LUT_OFFSET__SHIFT                           0x10
#define VPCM_GAMCOR_RAMA_REGION_8_9__VPCM_GAMCOR_RAMA_EXP_REGION9_NUM_SEGMENTS__SHIFT                         0x1c
#define VPCM_GAMCOR_RAMA_REGION_8_9__VPCM_GAMCOR_RAMA_EXP_REGION8_LUT_OFFSET_MASK                             0x000001FFL
#define VPCM_GAMCOR_RAMA_REGION_8_9__VPCM_GAMCOR_RAMA_EXP_REGION8_NUM_SEGMENTS_MASK                           0x00007000L
#define VPCM_GAMCOR_RAMA_REGION_8_9__VPCM_GAMCOR_RAMA_EXP_REGION9_LUT_OFFSET_MASK                             0x01FF0000L
#define VPCM_GAMCOR_RAMA_REGION_8_9__VPCM_GAMCOR_RAMA_EXP_REGION9_NUM_SEGMENTS_MASK                           0x70000000L
//VPCM_GAMCOR_RAMA_REGION_10_11
#define VPCM_GAMCOR_RAMA_REGION_10_11__VPCM_GAMCOR_RAMA_EXP_REGION10_LUT_OFFSET__SHIFT                        0x0
#define VPCM_GAMCOR_RAMA_REGION_10_11__VPCM_GAMCOR_RAMA_EXP_REGION10_NUM_SEGMENTS__SHIFT                      0xc
#define VPCM_GAMCOR_RAMA_REGION_10_11__VPCM_GAMCOR_RAMA_EXP_REGION11_LUT_OFFSET__SHIFT                        0x10
#define VPCM_GAMCOR_RAMA_REGION_10_11__VPCM_GAMCOR_RAMA_EXP_REGION11_NUM_SEGMENTS__SHIFT                      0x1c
#define VPCM_GAMCOR_RAMA_REGION_10_11__VPCM_GAMCOR_RAMA_EXP_REGION10_LUT_OFFSET_MASK                          0x000001FFL
#define VPCM_GAMCOR_RAMA_REGION_10_11__VPCM_GAMCOR_RAMA_EXP_REGION10_NUM_SEGMENTS_MASK                        0x00007000L
#define VPCM_GAMCOR_RAMA_REGION_10_11__VPCM_GAMCOR_RAMA_EXP_REGION11_LUT_OFFSET_MASK                          0x01FF0000L
#define VPCM_GAMCOR_RAMA_REGION_10_11__VPCM_GAMCOR_RAMA_EXP_REGION11_NUM_SEGMENTS_MASK                        0x70000000L
//VPCM_GAMCOR_RAMA_REGION_12_13
#define VPCM_GAMCOR_RAMA_REGION_12_13__VPCM_GAMCOR_RAMA_EXP_REGION12_LUT_OFFSET__SHIFT                        0x0
#define VPCM_GAMCOR_RAMA_REGION_12_13__VPCM_GAMCOR_RAMA_EXP_REGION12_NUM_SEGMENTS__SHIFT                      0xc
#define VPCM_GAMCOR_RAMA_REGION_12_13__VPCM_GAMCOR_RAMA_EXP_REGION13_LUT_OFFSET__SHIFT                        0x10
#define VPCM_GAMCOR_RAMA_REGION_12_13__VPCM_GAMCOR_RAMA_EXP_REGION13_NUM_SEGMENTS__SHIFT                      0x1c
#define VPCM_GAMCOR_RAMA_REGION_12_13__VPCM_GAMCOR_RAMA_EXP_REGION12_LUT_OFFSET_MASK                          0x000001FFL
#define VPCM_GAMCOR_RAMA_REGION_12_13__VPCM_GAMCOR_RAMA_EXP_REGION12_NUM_SEGMENTS_MASK                        0x00007000L
#define VPCM_GAMCOR_RAMA_REGION_12_13__VPCM_GAMCOR_RAMA_EXP_REGION13_LUT_OFFSET_MASK                          0x01FF0000L
#define VPCM_GAMCOR_RAMA_REGION_12_13__VPCM_GAMCOR_RAMA_EXP_REGION13_NUM_SEGMENTS_MASK                        0x70000000L
//VPCM_GAMCOR_RAMA_REGION_14_15
#define VPCM_GAMCOR_RAMA_REGION_14_15__VPCM_GAMCOR_RAMA_EXP_REGION14_LUT_OFFSET__SHIFT                        0x0
#define VPCM_GAMCOR_RAMA_REGION_14_15__VPCM_GAMCOR_RAMA_EXP_REGION14_NUM_SEGMENTS__SHIFT                      0xc
#define VPCM_GAMCOR_RAMA_REGION_14_15__VPCM_GAMCOR_RAMA_EXP_REGION15_LUT_OFFSET__SHIFT                        0x10
#define VPCM_GAMCOR_RAMA_REGION_14_15__VPCM_GAMCOR_RAMA_EXP_REGION15_NUM_SEGMENTS__SHIFT                      0x1c
#define VPCM_GAMCOR_RAMA_REGION_14_15__VPCM_GAMCOR_RAMA_EXP_REGION14_LUT_OFFSET_MASK                          0x000001FFL
#define VPCM_GAMCOR_RAMA_REGION_14_15__VPCM_GAMCOR_RAMA_EXP_REGION14_NUM_SEGMENTS_MASK                        0x00007000L
#define VPCM_GAMCOR_RAMA_REGION_14_15__VPCM_GAMCOR_RAMA_EXP_REGION15_LUT_OFFSET_MASK                          0x01FF0000L
#define VPCM_GAMCOR_RAMA_REGION_14_15__VPCM_GAMCOR_RAMA_EXP_REGION15_NUM_SEGMENTS_MASK                        0x70000000L
//VPCM_GAMCOR_RAMA_REGION_16_17
#define VPCM_GAMCOR_RAMA_REGION_16_17__VPCM_GAMCOR_RAMA_EXP_REGION16_LUT_OFFSET__SHIFT                        0x0
#define VPCM_GAMCOR_RAMA_REGION_16_17__VPCM_GAMCOR_RAMA_EXP_REGION16_NUM_SEGMENTS__SHIFT                      0xc
#define VPCM_GAMCOR_RAMA_REGION_16_17__VPCM_GAMCOR_RAMA_EXP_REGION17_LUT_OFFSET__SHIFT                        0x10
#define VPCM_GAMCOR_RAMA_REGION_16_17__VPCM_GAMCOR_RAMA_EXP_REGION17_NUM_SEGMENTS__SHIFT                      0x1c
#define VPCM_GAMCOR_RAMA_REGION_16_17__VPCM_GAMCOR_RAMA_EXP_REGION16_LUT_OFFSET_MASK                          0x000001FFL
#define VPCM_GAMCOR_RAMA_REGION_16_17__VPCM_GAMCOR_RAMA_EXP_REGION16_NUM_SEGMENTS_MASK                        0x00007000L
#define VPCM_GAMCOR_RAMA_REGION_16_17__VPCM_GAMCOR_RAMA_EXP_REGION17_LUT_OFFSET_MASK                          0x01FF0000L
#define VPCM_GAMCOR_RAMA_REGION_16_17__VPCM_GAMCOR_RAMA_EXP_REGION17_NUM_SEGMENTS_MASK                        0x70000000L
//VPCM_GAMCOR_RAMA_REGION_18_19
#define VPCM_GAMCOR_RAMA_REGION_18_19__VPCM_GAMCOR_RAMA_EXP_REGION18_LUT_OFFSET__SHIFT                        0x0
#define VPCM_GAMCOR_RAMA_REGION_18_19__VPCM_GAMCOR_RAMA_EXP_REGION18_NUM_SEGMENTS__SHIFT                      0xc
#define VPCM_GAMCOR_RAMA_REGION_18_19__VPCM_GAMCOR_RAMA_EXP_REGION19_LUT_OFFSET__SHIFT                        0x10
#define VPCM_GAMCOR_RAMA_REGION_18_19__VPCM_GAMCOR_RAMA_EXP_REGION19_NUM_SEGMENTS__SHIFT                      0x1c
#define VPCM_GAMCOR_RAMA_REGION_18_19__VPCM_GAMCOR_RAMA_EXP_REGION18_LUT_OFFSET_MASK                          0x000001FFL
#define VPCM_GAMCOR_RAMA_REGION_18_19__VPCM_GAMCOR_RAMA_EXP_REGION18_NUM_SEGMENTS_MASK                        0x00007000L
#define VPCM_GAMCOR_RAMA_REGION_18_19__VPCM_GAMCOR_RAMA_EXP_REGION19_LUT_OFFSET_MASK                          0x01FF0000L
#define VPCM_GAMCOR_RAMA_REGION_18_19__VPCM_GAMCOR_RAMA_EXP_REGION19_NUM_SEGMENTS_MASK                        0x70000000L
//VPCM_GAMCOR_RAMA_REGION_20_21
#define VPCM_GAMCOR_RAMA_REGION_20_21__VPCM_GAMCOR_RAMA_EXP_REGION20_LUT_OFFSET__SHIFT                        0x0
#define VPCM_GAMCOR_RAMA_REGION_20_21__VPCM_GAMCOR_RAMA_EXP_REGION20_NUM_SEGMENTS__SHIFT                      0xc
#define VPCM_GAMCOR_RAMA_REGION_20_21__VPCM_GAMCOR_RAMA_EXP_REGION21_LUT_OFFSET__SHIFT                        0x10
#define VPCM_GAMCOR_RAMA_REGION_20_21__VPCM_GAMCOR_RAMA_EXP_REGION21_NUM_SEGMENTS__SHIFT                      0x1c
#define VPCM_GAMCOR_RAMA_REGION_20_21__VPCM_GAMCOR_RAMA_EXP_REGION20_LUT_OFFSET_MASK                          0x000001FFL
#define VPCM_GAMCOR_RAMA_REGION_20_21__VPCM_GAMCOR_RAMA_EXP_REGION20_NUM_SEGMENTS_MASK                        0x00007000L
#define VPCM_GAMCOR_RAMA_REGION_20_21__VPCM_GAMCOR_RAMA_EXP_REGION21_LUT_OFFSET_MASK                          0x01FF0000L
#define VPCM_GAMCOR_RAMA_REGION_20_21__VPCM_GAMCOR_RAMA_EXP_REGION21_NUM_SEGMENTS_MASK                        0x70000000L
//VPCM_GAMCOR_RAMA_REGION_22_23
#define VPCM_GAMCOR_RAMA_REGION_22_23__VPCM_GAMCOR_RAMA_EXP_REGION22_LUT_OFFSET__SHIFT                        0x0
#define VPCM_GAMCOR_RAMA_REGION_22_23__VPCM_GAMCOR_RAMA_EXP_REGION22_NUM_SEGMENTS__SHIFT                      0xc
#define VPCM_GAMCOR_RAMA_REGION_22_23__VPCM_GAMCOR_RAMA_EXP_REGION23_LUT_OFFSET__SHIFT                        0x10
#define VPCM_GAMCOR_RAMA_REGION_22_23__VPCM_GAMCOR_RAMA_EXP_REGION23_NUM_SEGMENTS__SHIFT                      0x1c
#define VPCM_GAMCOR_RAMA_REGION_22_23__VPCM_GAMCOR_RAMA_EXP_REGION22_LUT_OFFSET_MASK                          0x000001FFL
#define VPCM_GAMCOR_RAMA_REGION_22_23__VPCM_GAMCOR_RAMA_EXP_REGION22_NUM_SEGMENTS_MASK                        0x00007000L
#define VPCM_GAMCOR_RAMA_REGION_22_23__VPCM_GAMCOR_RAMA_EXP_REGION23_LUT_OFFSET_MASK                          0x01FF0000L
#define VPCM_GAMCOR_RAMA_REGION_22_23__VPCM_GAMCOR_RAMA_EXP_REGION23_NUM_SEGMENTS_MASK                        0x70000000L
//VPCM_GAMCOR_RAMA_REGION_24_25
#define VPCM_GAMCOR_RAMA_REGION_24_25__VPCM_GAMCOR_RAMA_EXP_REGION24_LUT_OFFSET__SHIFT                        0x0
#define VPCM_GAMCOR_RAMA_REGION_24_25__VPCM_GAMCOR_RAMA_EXP_REGION24_NUM_SEGMENTS__SHIFT                      0xc
#define VPCM_GAMCOR_RAMA_REGION_24_25__VPCM_GAMCOR_RAMA_EXP_REGION25_LUT_OFFSET__SHIFT                        0x10
#define VPCM_GAMCOR_RAMA_REGION_24_25__VPCM_GAMCOR_RAMA_EXP_REGION25_NUM_SEGMENTS__SHIFT                      0x1c
#define VPCM_GAMCOR_RAMA_REGION_24_25__VPCM_GAMCOR_RAMA_EXP_REGION24_LUT_OFFSET_MASK                          0x000001FFL
#define VPCM_GAMCOR_RAMA_REGION_24_25__VPCM_GAMCOR_RAMA_EXP_REGION24_NUM_SEGMENTS_MASK                        0x00007000L
#define VPCM_GAMCOR_RAMA_REGION_24_25__VPCM_GAMCOR_RAMA_EXP_REGION25_LUT_OFFSET_MASK                          0x01FF0000L
#define VPCM_GAMCOR_RAMA_REGION_24_25__VPCM_GAMCOR_RAMA_EXP_REGION25_NUM_SEGMENTS_MASK                        0x70000000L
//VPCM_GAMCOR_RAMA_REGION_26_27
#define VPCM_GAMCOR_RAMA_REGION_26_27__VPCM_GAMCOR_RAMA_EXP_REGION26_LUT_OFFSET__SHIFT                        0x0
#define VPCM_GAMCOR_RAMA_REGION_26_27__VPCM_GAMCOR_RAMA_EXP_REGION26_NUM_SEGMENTS__SHIFT                      0xc
#define VPCM_GAMCOR_RAMA_REGION_26_27__VPCM_GAMCOR_RAMA_EXP_REGION27_LUT_OFFSET__SHIFT                        0x10
#define VPCM_GAMCOR_RAMA_REGION_26_27__VPCM_GAMCOR_RAMA_EXP_REGION27_NUM_SEGMENTS__SHIFT                      0x1c
#define VPCM_GAMCOR_RAMA_REGION_26_27__VPCM_GAMCOR_RAMA_EXP_REGION26_LUT_OFFSET_MASK                          0x000001FFL
#define VPCM_GAMCOR_RAMA_REGION_26_27__VPCM_GAMCOR_RAMA_EXP_REGION26_NUM_SEGMENTS_MASK                        0x00007000L
#define VPCM_GAMCOR_RAMA_REGION_26_27__VPCM_GAMCOR_RAMA_EXP_REGION27_LUT_OFFSET_MASK                          0x01FF0000L
#define VPCM_GAMCOR_RAMA_REGION_26_27__VPCM_GAMCOR_RAMA_EXP_REGION27_NUM_SEGMENTS_MASK                        0x70000000L
//VPCM_GAMCOR_RAMA_REGION_28_29
#define VPCM_GAMCOR_RAMA_REGION_28_29__VPCM_GAMCOR_RAMA_EXP_REGION28_LUT_OFFSET__SHIFT                        0x0
#define VPCM_GAMCOR_RAMA_REGION_28_29__VPCM_GAMCOR_RAMA_EXP_REGION28_NUM_SEGMENTS__SHIFT                      0xc
#define VPCM_GAMCOR_RAMA_REGION_28_29__VPCM_GAMCOR_RAMA_EXP_REGION29_LUT_OFFSET__SHIFT                        0x10
#define VPCM_GAMCOR_RAMA_REGION_28_29__VPCM_GAMCOR_RAMA_EXP_REGION29_NUM_SEGMENTS__SHIFT                      0x1c
#define VPCM_GAMCOR_RAMA_REGION_28_29__VPCM_GAMCOR_RAMA_EXP_REGION28_LUT_OFFSET_MASK                          0x000001FFL
#define VPCM_GAMCOR_RAMA_REGION_28_29__VPCM_GAMCOR_RAMA_EXP_REGION28_NUM_SEGMENTS_MASK                        0x00007000L
#define VPCM_GAMCOR_RAMA_REGION_28_29__VPCM_GAMCOR_RAMA_EXP_REGION29_LUT_OFFSET_MASK                          0x01FF0000L
#define VPCM_GAMCOR_RAMA_REGION_28_29__VPCM_GAMCOR_RAMA_EXP_REGION29_NUM_SEGMENTS_MASK                        0x70000000L
//VPCM_GAMCOR_RAMA_REGION_30_31
#define VPCM_GAMCOR_RAMA_REGION_30_31__VPCM_GAMCOR_RAMA_EXP_REGION30_LUT_OFFSET__SHIFT                        0x0
#define VPCM_GAMCOR_RAMA_REGION_30_31__VPCM_GAMCOR_RAMA_EXP_REGION30_NUM_SEGMENTS__SHIFT                      0xc
#define VPCM_GAMCOR_RAMA_REGION_30_31__VPCM_GAMCOR_RAMA_EXP_REGION31_LUT_OFFSET__SHIFT                        0x10
#define VPCM_GAMCOR_RAMA_REGION_30_31__VPCM_GAMCOR_RAMA_EXP_REGION31_NUM_SEGMENTS__SHIFT                      0x1c
#define VPCM_GAMCOR_RAMA_REGION_30_31__VPCM_GAMCOR_RAMA_EXP_REGION30_LUT_OFFSET_MASK                          0x000001FFL
#define VPCM_GAMCOR_RAMA_REGION_30_31__VPCM_GAMCOR_RAMA_EXP_REGION30_NUM_SEGMENTS_MASK                        0x00007000L
#define VPCM_GAMCOR_RAMA_REGION_30_31__VPCM_GAMCOR_RAMA_EXP_REGION31_LUT_OFFSET_MASK                          0x01FF0000L
#define VPCM_GAMCOR_RAMA_REGION_30_31__VPCM_GAMCOR_RAMA_EXP_REGION31_NUM_SEGMENTS_MASK                        0x70000000L
//VPCM_GAMCOR_RAMA_REGION_32_33
#define VPCM_GAMCOR_RAMA_REGION_32_33__VPCM_GAMCOR_RAMA_EXP_REGION32_LUT_OFFSET__SHIFT                        0x0
#define VPCM_GAMCOR_RAMA_REGION_32_33__VPCM_GAMCOR_RAMA_EXP_REGION32_NUM_SEGMENTS__SHIFT                      0xc
#define VPCM_GAMCOR_RAMA_REGION_32_33__VPCM_GAMCOR_RAMA_EXP_REGION33_LUT_OFFSET__SHIFT                        0x10
#define VPCM_GAMCOR_RAMA_REGION_32_33__VPCM_GAMCOR_RAMA_EXP_REGION33_NUM_SEGMENTS__SHIFT                      0x1c
#define VPCM_GAMCOR_RAMA_REGION_32_33__VPCM_GAMCOR_RAMA_EXP_REGION32_LUT_OFFSET_MASK                          0x000001FFL
#define VPCM_GAMCOR_RAMA_REGION_32_33__VPCM_GAMCOR_RAMA_EXP_REGION32_NUM_SEGMENTS_MASK                        0x00007000L
#define VPCM_GAMCOR_RAMA_REGION_32_33__VPCM_GAMCOR_RAMA_EXP_REGION33_LUT_OFFSET_MASK                          0x01FF0000L
#define VPCM_GAMCOR_RAMA_REGION_32_33__VPCM_GAMCOR_RAMA_EXP_REGION33_NUM_SEGMENTS_MASK                        0x70000000L
//VPCM_HDR_MULT_COEF
#define VPCM_HDR_MULT_COEF__VPCM_HDR_MULT_COEF__SHIFT                                                         0x0
#define VPCM_HDR_MULT_COEF__VPCM_HDR_MULT_COEF_MASK                                                           0x0007FFFFL
//VPCM_MEM_PWR_CTRL
#define VPCM_MEM_PWR_CTRL__GAMCOR_MEM_PWR_FORCE__SHIFT                                                        0x0
#define VPCM_MEM_PWR_CTRL__GAMCOR_MEM_PWR_DIS__SHIFT                                                          0x2
#define VPCM_MEM_PWR_CTRL__GAMCOR_MEM_PWR_FORCE_MASK                                                          0x00000003L
#define VPCM_MEM_PWR_CTRL__GAMCOR_MEM_PWR_DIS_MASK                                                            0x00000004L
//VPCM_MEM_PWR_STATUS
#define VPCM_MEM_PWR_STATUS__GAMCOR_MEM_PWR_STATE__SHIFT                                                      0x0
#define VPCM_MEM_PWR_STATUS__GAMCOR_MEM_PWR_STATE_MASK                                                        0x00000003L
//VPCM_DEALPHA
#define VPCM_DEALPHA__VPCM_DEALPHA_EN__SHIFT                                                                  0x0
#define VPCM_DEALPHA__VPCM_DEALPHA_ABLND__SHIFT                                                               0x1
#define VPCM_DEALPHA__VPCM_DEALPHA_EN_MASK                                                                    0x00000001L
#define VPCM_DEALPHA__VPCM_DEALPHA_ABLND_MASK                                                                 0x00000002L
//VPCM_COEF_FORMAT
#define VPCM_COEF_FORMAT__VPCM_BIAS_FORMAT__SHIFT                                                             0x0
#define VPCM_COEF_FORMAT__VPCM_POST_CSC_COEF_FORMAT__SHIFT                                                    0x4
#define VPCM_COEF_FORMAT__VPCM_GAMUT_REMAP_COEF_FORMAT__SHIFT                                                 0x8
#define VPCM_COEF_FORMAT__VPCM_BIAS_FORMAT_MASK                                                               0x00000001L
#define VPCM_COEF_FORMAT__VPCM_POST_CSC_COEF_FORMAT_MASK                                                      0x00000010L
#define VPCM_COEF_FORMAT__VPCM_GAMUT_REMAP_COEF_FORMAT_MASK                                                   0x00000100L
//VPCM_TEST_DEBUG_INDEX
#define VPCM_TEST_DEBUG_INDEX__VPCM_TEST_DEBUG_INDEX__SHIFT                                                   0x0
#define VPCM_TEST_DEBUG_INDEX__VPCM_TEST_DEBUG_WRITE_EN__SHIFT                                                0x8
#define VPCM_TEST_DEBUG_INDEX__VPCM_TEST_DEBUG_INDEX_MASK                                                     0x000000FFL
#define VPCM_TEST_DEBUG_INDEX__VPCM_TEST_DEBUG_WRITE_EN_MASK                                                  0x00000100L
//VPCM_TEST_DEBUG_DATA
#define VPCM_TEST_DEBUG_DATA__VPCM_TEST_DEBUG_DATA__SHIFT                                                     0x0
#define VPCM_TEST_DEBUG_DATA__VPCM_TEST_DEBUG_DATA_MASK                                                       0xFFFFFFFFL


// addressBlock: vpe_vpep_vpdpp0_dispdec_vpdpp_top_dispdec
//VPDPP_CONTROL
#define VPDPP_CONTROL__VPDPP_CLOCK_ENABLE__SHIFT                                                              0x4
#define VPDPP_CONTROL__VPECLK_G_GATE_DISABLE__SHIFT                                                           0x8
#define VPDPP_CONTROL__VPECLK_G_DYN_GATE_DISABLE__SHIFT                                                       0xa
#define VPDPP_CONTROL__VPECLK_G_VPDSCL_GATE_DISABLE__SHIFT                                                    0xc
#define VPDPP_CONTROL__VPECLK_R_GATE_DISABLE__SHIFT                                                           0xe
#define VPDPP_CONTROL__DISPCLK_R_GATE_DISABLE__SHIFT                                                          0x10
#define VPDPP_CONTROL__DISPCLK_G_GATE_DISABLE__SHIFT                                                          0x12
#define VPDPP_CONTROL__VPDPP_FGCG_REP_DIS__SHIFT                                                              0x18
#define VPDPP_CONTROL__VPDPP_TEST_CLK_SEL__SHIFT                                                              0x1c
#define VPDPP_CONTROL__VPDPP_CLOCK_ENABLE_MASK                                                                0x00000010L
#define VPDPP_CONTROL__VPECLK_G_GATE_DISABLE_MASK                                                             0x00000100L
#define VPDPP_CONTROL__VPECLK_G_DYN_GATE_DISABLE_MASK                                                         0x00000400L
#define VPDPP_CONTROL__VPECLK_G_VPDSCL_GATE_DISABLE_MASK                                                      0x00001000L
#define VPDPP_CONTROL__VPECLK_R_GATE_DISABLE_MASK                                                             0x00004000L
#define VPDPP_CONTROL__DISPCLK_R_GATE_DISABLE_MASK                                                            0x00010000L
#define VPDPP_CONTROL__DISPCLK_G_GATE_DISABLE_MASK                                                            0x00040000L
#define VPDPP_CONTROL__VPDPP_FGCG_REP_DIS_MASK                                                                0x01000000L
#define VPDPP_CONTROL__VPDPP_TEST_CLK_SEL_MASK                                                                0x70000000L
//VPDPP_SOFT_RESET
#define VPDPP_SOFT_RESET__VPCNVC_SOFT_RESET__SHIFT                                                            0x0
#define VPDPP_SOFT_RESET__VPDSCL_SOFT_RESET__SHIFT                                                            0x4
#define VPDPP_SOFT_RESET__VPCM_SOFT_RESET__SHIFT                                                              0x8
#define VPDPP_SOFT_RESET__VPOBUF_SOFT_RESET__SHIFT                                                            0xc
#define VPDPP_SOFT_RESET__VPCNVC_SOFT_RESET_MASK                                                              0x00000001L
#define VPDPP_SOFT_RESET__VPDSCL_SOFT_RESET_MASK                                                              0x00000010L
#define VPDPP_SOFT_RESET__VPCM_SOFT_RESET_MASK                                                                0x00000100L
#define VPDPP_SOFT_RESET__VPOBUF_SOFT_RESET_MASK                                                              0x00001000L
//VPDPP_CRC_VAL_R_G
#define VPDPP_CRC_VAL_R_G__VPDPP_CRC_R_CR__SHIFT                                                              0x0
#define VPDPP_CRC_VAL_R_G__VPDPP_CRC_G_Y__SHIFT                                                               0x10
#define VPDPP_CRC_VAL_R_G__VPDPP_CRC_R_CR_MASK                                                                0x0000FFFFL
#define VPDPP_CRC_VAL_R_G__VPDPP_CRC_G_Y_MASK                                                                 0xFFFF0000L
//VPDPP_CRC_VAL_B_A
#define VPDPP_CRC_VAL_B_A__VPDPP_CRC_B_CB__SHIFT                                                              0x0
#define VPDPP_CRC_VAL_B_A__VPDPP_CRC_ALPHA__SHIFT                                                             0x10
#define VPDPP_CRC_VAL_B_A__VPDPP_CRC_B_CB_MASK                                                                0x0000FFFFL
#define VPDPP_CRC_VAL_B_A__VPDPP_CRC_ALPHA_MASK                                                               0xFFFF0000L
//VPDPP_CRC_CTRL
#define VPDPP_CRC_CTRL__VPDPP_CRC_EN__SHIFT                                                                   0x0
#define VPDPP_CRC_CTRL__VPDPP_CRC_CONT_EN__SHIFT                                                              0x1
#define VPDPP_CRC_CTRL__VPDPP_CRC_ONE_SHOT_PENDING__SHIFT                                                     0x2
#define VPDPP_CRC_CTRL__VPDPP_CRC_420_COMP_SEL__SHIFT                                                         0x3
#define VPDPP_CRC_CTRL__VPDPP_CRC_SRC_SEL__SHIFT                                                              0x4
#define VPDPP_CRC_CTRL__VPDPP_CRC_PIX_FORMAT_SEL__SHIFT                                                       0xb
#define VPDPP_CRC_CTRL__VPDPP_CRC_MASK__SHIFT                                                                 0x10
#define VPDPP_CRC_CTRL__VPDPP_CRC_EN_MASK                                                                     0x00000001L
#define VPDPP_CRC_CTRL__VPDPP_CRC_CONT_EN_MASK                                                                0x00000002L
#define VPDPP_CRC_CTRL__VPDPP_CRC_ONE_SHOT_PENDING_MASK                                                       0x00000004L
#define VPDPP_CRC_CTRL__VPDPP_CRC_420_COMP_SEL_MASK                                                           0x00000008L
#define VPDPP_CRC_CTRL__VPDPP_CRC_SRC_SEL_MASK                                                                0x00000030L
#define VPDPP_CRC_CTRL__VPDPP_CRC_PIX_FORMAT_SEL_MASK                                                         0x00003800L
#define VPDPP_CRC_CTRL__VPDPP_CRC_MASK_MASK                                                                   0xFFFF0000L
//VPHOST_READ_CONTROL
#define VPHOST_READ_CONTROL__HOST_READ_RATE_CONTROL__SHIFT                                                    0x0
#define VPHOST_READ_CONTROL__HOST_READ_RATE_CONTROL_MASK                                                      0x000000FFL


// addressBlock: vpe_vpep_vpmpc_vpmpcc0_dispdec
//VPMPCC_TOP_SEL
#define VPMPCC_TOP_SEL__VPMPCC_TOP_SEL__SHIFT                                                                 0x0
#define VPMPCC_TOP_SEL__VPMPCC_TOP_SEL_MASK                                                                   0x0000000FL
//VPMPCC_BOT_SEL
#define VPMPCC_BOT_SEL__VPMPCC_BOT_SEL__SHIFT                                                                 0x0
#define VPMPCC_BOT_SEL__VPMPCC_BOT_SEL_MASK                                                                   0x0000000FL
//VPMPCC_VPOPP_ID
#define VPMPCC_VPOPP_ID__VPMPCC_VPOPP_ID__SHIFT                                                               0x0
#define VPMPCC_VPOPP_ID__VPMPCC_VPOPP_ID_MASK                                                                 0x0000000FL
//VPMPCC_CONTROL
#define VPMPCC_CONTROL__VPMPCC_MODE__SHIFT                                                                    0x0
#define VPMPCC_CONTROL__VPMPCC_ALPHA_BLND_MODE__SHIFT                                                         0x4
#define VPMPCC_CONTROL__VPMPCC_ALPHA_MULTIPLIED_MODE__SHIFT                                                   0x6
#define VPMPCC_CONTROL__VPMPCC_BLND_ACTIVE_OVERLAP_ONLY__SHIFT                                                0x7
#define VPMPCC_CONTROL__VPMPCC_BG_BPC__SHIFT                                                                  0x8
#define VPMPCC_CONTROL__VPMPCC_BOT_GAIN_MODE__SHIFT                                                           0xb
#define VPMPCC_CONTROL__VPMPCC_GLOBAL_ALPHA__SHIFT                                                            0x10
#define VPMPCC_CONTROL__VPMPCC_GLOBAL_GAIN__SHIFT                                                             0x18
#define VPMPCC_CONTROL__VPMPCC_MODE_MASK                                                                      0x00000003L
#define VPMPCC_CONTROL__VPMPCC_ALPHA_BLND_MODE_MASK                                                           0x00000030L
#define VPMPCC_CONTROL__VPMPCC_ALPHA_MULTIPLIED_MODE_MASK                                                     0x00000040L
#define VPMPCC_CONTROL__VPMPCC_BLND_ACTIVE_OVERLAP_ONLY_MASK                                                  0x00000080L
#define VPMPCC_CONTROL__VPMPCC_BG_BPC_MASK                                                                    0x00000700L
#define VPMPCC_CONTROL__VPMPCC_BOT_GAIN_MODE_MASK                                                             0x00000800L
#define VPMPCC_CONTROL__VPMPCC_GLOBAL_ALPHA_MASK                                                              0x00FF0000L
#define VPMPCC_CONTROL__VPMPCC_GLOBAL_GAIN_MASK                                                               0xFF000000L
//VPMPCC_TOP_GAIN
#define VPMPCC_TOP_GAIN__VPMPCC_TOP_GAIN__SHIFT                                                               0x0
#define VPMPCC_TOP_GAIN__VPMPCC_TOP_GAIN_MASK                                                                 0x0007FFFFL
//VPMPCC_BOT_GAIN_INSIDE
#define VPMPCC_BOT_GAIN_INSIDE__VPMPCC_BOT_GAIN_INSIDE__SHIFT                                                 0x0
#define VPMPCC_BOT_GAIN_INSIDE__VPMPCC_BOT_GAIN_INSIDE_MASK                                                   0x0007FFFFL
//VPMPCC_BOT_GAIN_OUTSIDE
#define VPMPCC_BOT_GAIN_OUTSIDE__VPMPCC_BOT_GAIN_OUTSIDE__SHIFT                                               0x0
#define VPMPCC_BOT_GAIN_OUTSIDE__VPMPCC_BOT_GAIN_OUTSIDE_MASK                                                 0x0007FFFFL
//VPMPCC_MOVABLE_CM_LOCATION_CONTROL
#define VPMPCC_MOVABLE_CM_LOCATION_CONTROL__VPMPCC_MOVABLE_CM_LOCATION_CNTL__SHIFT                            0x0
#define VPMPCC_MOVABLE_CM_LOCATION_CONTROL__VPMPCC_MOVABLE_CM_LOCATION_CNTL_CURRENT__SHIFT                    0x4
#define VPMPCC_MOVABLE_CM_LOCATION_CONTROL__VPMPCC_MOVABLE_CM_LOCATION_CNTL_MASK                              0x00000001L
#define VPMPCC_MOVABLE_CM_LOCATION_CONTROL__VPMPCC_MOVABLE_CM_LOCATION_CNTL_CURRENT_MASK                      0x00000010L
//VPMPCC_BG_R_CR
#define VPMPCC_BG_R_CR__VPMPCC_BG_R_CR__SHIFT                                                                 0x0
#define VPMPCC_BG_R_CR__VPMPCC_BG_R_CR_MASK                                                                   0x00000FFFL
//VPMPCC_BG_G_Y
#define VPMPCC_BG_G_Y__VPMPCC_BG_G_Y__SHIFT                                                                   0x0
#define VPMPCC_BG_G_Y__VPMPCC_BG_G_Y_MASK                                                                     0x00000FFFL
//VPMPCC_BG_B_CB
#define VPMPCC_BG_B_CB__VPMPCC_BG_B_CB__SHIFT                                                                 0x0
#define VPMPCC_BG_B_CB__VPMPCC_BG_B_CB_MASK                                                                   0x00000FFFL
//VPMPCC_MEM_PWR_CTRL
#define VPMPCC_MEM_PWR_CTRL__VPMPCC_OGAM_MEM_PWR_FORCE__SHIFT                                                 0x0
#define VPMPCC_MEM_PWR_CTRL__VPMPCC_OGAM_MEM_PWR_DIS__SHIFT                                                   0x2
#define VPMPCC_MEM_PWR_CTRL__VPMPCC_OGAM_MEM_LOW_PWR_MODE__SHIFT                                              0x4
#define VPMPCC_MEM_PWR_CTRL__VPMPCC_OGAM_MEM_PWR_STATE__SHIFT                                                 0x8
#define VPMPCC_MEM_PWR_CTRL__VPMPCC_OGAM_MEM_PWR_FORCE_MASK                                                   0x00000003L
#define VPMPCC_MEM_PWR_CTRL__VPMPCC_OGAM_MEM_PWR_DIS_MASK                                                     0x00000004L
#define VPMPCC_MEM_PWR_CTRL__VPMPCC_OGAM_MEM_LOW_PWR_MODE_MASK                                                0x00000030L
#define VPMPCC_MEM_PWR_CTRL__VPMPCC_OGAM_MEM_PWR_STATE_MASK                                                   0x00000300L
//VPMPCC_STATUS
#define VPMPCC_STATUS__VPMPCC_IDLE__SHIFT                                                                     0x0
#define VPMPCC_STATUS__VPMPCC_BUSY__SHIFT                                                                     0x1
#define VPMPCC_STATUS__VPMPCC_DISABLED__SHIFT                                                                 0x2
#define VPMPCC_STATUS__VPMPCC_IDLE_MASK                                                                       0x00000001L
#define VPMPCC_STATUS__VPMPCC_BUSY_MASK                                                                       0x00000002L
#define VPMPCC_STATUS__VPMPCC_DISABLED_MASK                                                                   0x00000004L


// addressBlock: vpe_vpep_vpmpc_vpmpc_cfg_dispdec
//VPMPC_CLOCK_CONTROL
#define VPMPC_CLOCK_CONTROL__VPECLK_R_GATE_DISABLE__SHIFT                                                     0x1
#define VPMPC_CLOCK_CONTROL__VPMPC_TEST_CLK_SEL__SHIFT                                                        0x4
#define VPMPC_CLOCK_CONTROL__VPECLK_R_GATE_DISABLE_MASK                                                       0x00000002L
#define VPMPC_CLOCK_CONTROL__VPMPC_TEST_CLK_SEL_MASK                                                          0x00000030L
//VPMPC_SOFT_RESET
#define VPMPC_SOFT_RESET__VPMPCC0_SOFT_RESET__SHIFT                                                           0x0
#define VPMPC_SOFT_RESET__VPMPC_SFR0_SOFT_RESET__SHIFT                                                        0xa
#define VPMPC_SOFT_RESET__VPMPC_SFT0_SOFT_RESET__SHIFT                                                        0x14
#define VPMPC_SOFT_RESET__VPMPC_SOFT_RESET__SHIFT                                                             0x1f
#define VPMPC_SOFT_RESET__VPMPCC0_SOFT_RESET_MASK                                                             0x00000001L
#define VPMPC_SOFT_RESET__VPMPC_SFR0_SOFT_RESET_MASK                                                          0x00000400L
#define VPMPC_SOFT_RESET__VPMPC_SFT0_SOFT_RESET_MASK                                                          0x00100000L
#define VPMPC_SOFT_RESET__VPMPC_SOFT_RESET_MASK                                                               0x80000000L
//VPMPC_CRC_CTRL
#define VPMPC_CRC_CTRL__VPMPC_CRC_EN__SHIFT                                                                   0x0
#define VPMPC_CRC_CTRL__VPMPC_CRC_CONT_EN__SHIFT                                                              0x4
#define VPMPC_CRC_CTRL__VPMPC_CRC_SRC_SEL__SHIFT                                                              0x18
#define VPMPC_CRC_CTRL__VPMPC_CRC_ONE_SHOT_PENDING__SHIFT                                                     0x1c
#define VPMPC_CRC_CTRL__VPMPC_CRC_UPDATE_ENABLED__SHIFT                                                       0x1e
#define VPMPC_CRC_CTRL__VPMPC_CRC_UPDATE_LOCK__SHIFT                                                          0x1f
#define VPMPC_CRC_CTRL__VPMPC_CRC_EN_MASK                                                                     0x00000001L
#define VPMPC_CRC_CTRL__VPMPC_CRC_CONT_EN_MASK                                                                0x00000010L
#define VPMPC_CRC_CTRL__VPMPC_CRC_SRC_SEL_MASK                                                                0x03000000L
#define VPMPC_CRC_CTRL__VPMPC_CRC_ONE_SHOT_PENDING_MASK                                                       0x10000000L
#define VPMPC_CRC_CTRL__VPMPC_CRC_UPDATE_ENABLED_MASK                                                         0x40000000L
#define VPMPC_CRC_CTRL__VPMPC_CRC_UPDATE_LOCK_MASK                                                            0x80000000L
//VPMPC_CRC_SEL_CONTROL
#define VPMPC_CRC_SEL_CONTROL__VPMPC_CRC_VPDPP_SEL__SHIFT                                                     0x0
#define VPMPC_CRC_SEL_CONTROL__VPMPC_CRC_VPOPP_SEL__SHIFT                                                     0x4
#define VPMPC_CRC_SEL_CONTROL__VPMPC_CRC_MASK__SHIFT                                                          0x10
#define VPMPC_CRC_SEL_CONTROL__VPMPC_CRC_VPDPP_SEL_MASK                                                       0x0000000FL
#define VPMPC_CRC_SEL_CONTROL__VPMPC_CRC_VPOPP_SEL_MASK                                                       0x000000F0L
#define VPMPC_CRC_SEL_CONTROL__VPMPC_CRC_MASK_MASK                                                            0xFFFF0000L
//VPMPC_CRC_RESULT_AR
#define VPMPC_CRC_RESULT_AR__VPMPC_CRC_RESULT_A__SHIFT                                                        0x0
#define VPMPC_CRC_RESULT_AR__VPMPC_CRC_RESULT_R__SHIFT                                                        0x10
#define VPMPC_CRC_RESULT_AR__VPMPC_CRC_RESULT_A_MASK                                                          0x0000FFFFL
#define VPMPC_CRC_RESULT_AR__VPMPC_CRC_RESULT_R_MASK                                                          0xFFFF0000L
//VPMPC_CRC_RESULT_GB
#define VPMPC_CRC_RESULT_GB__VPMPC_CRC_RESULT_G__SHIFT                                                        0x0
#define VPMPC_CRC_RESULT_GB__VPMPC_CRC_RESULT_B__SHIFT                                                        0x10
#define VPMPC_CRC_RESULT_GB__VPMPC_CRC_RESULT_G_MASK                                                          0x0000FFFFL
#define VPMPC_CRC_RESULT_GB__VPMPC_CRC_RESULT_B_MASK                                                          0xFFFF0000L
//VPMPC_CRC_RESULT_C
#define VPMPC_CRC_RESULT_C__VPMPC_CRC_RESULT_C__SHIFT                                                         0x0
#define VPMPC_CRC_RESULT_C__VPMPC_CRC_RESULT_C_MASK                                                           0x0000FFFFL
//VPMPC_BYPASS_BG_AR
#define VPMPC_BYPASS_BG_AR__VPMPC_BYPASS_BG_ALPHA__SHIFT                                                      0x0
#define VPMPC_BYPASS_BG_AR__VPMPC_BYPASS_BG_R_CR__SHIFT                                                       0x10
#define VPMPC_BYPASS_BG_AR__VPMPC_BYPASS_BG_ALPHA_MASK                                                        0x0000FFFFL
#define VPMPC_BYPASS_BG_AR__VPMPC_BYPASS_BG_R_CR_MASK                                                         0xFFFF0000L
//VPMPC_BYPASS_BG_GB
#define VPMPC_BYPASS_BG_GB__VPMPC_BYPASS_BG_G_Y__SHIFT                                                        0x0
#define VPMPC_BYPASS_BG_GB__VPMPC_BYPASS_BG_B_CB__SHIFT                                                       0x10
#define VPMPC_BYPASS_BG_GB__VPMPC_BYPASS_BG_G_Y_MASK                                                          0x0000FFFFL
#define VPMPC_BYPASS_BG_GB__VPMPC_BYPASS_BG_B_CB_MASK                                                         0xFFFF0000L
//VPMPC_HOST_READ_CONTROL
#define VPMPC_HOST_READ_CONTROL__HOST_READ_RATE_CONTROL__SHIFT                                                0x0
#define VPMPC_HOST_READ_CONTROL__HOST_READ_RATE_CONTROL_MASK                                                  0x000000FFL
//VPMPC_PENDING_STATUS_MISC
#define VPMPC_PENDING_STATUS_MISC__VPMPCC0_CONFIG_UPDATE_PENDING__SHIFT                                       0x8
#define VPMPC_PENDING_STATUS_MISC__VPMPCC0_CONFIG_UPDATE_PENDING_MASK                                         0x00000100L


// addressBlock: vpe_vpep_vpmpc_vpmpcc_ogam0_dispdec
//VPMPCC_OGAM_CONTROL
#define VPMPCC_OGAM_CONTROL__VPMPCC_OGAM_MODE__SHIFT                                                          0x0
#define VPMPCC_OGAM_CONTROL__VPMPCC_OGAM_PWL_DISABLE__SHIFT                                                   0x3
#define VPMPCC_OGAM_CONTROL__VPMPCC_OGAM_MODE_CURRENT__SHIFT                                                  0x7
#define VPMPCC_OGAM_CONTROL__VPMPCC_OGAM_SELECT_CURRENT__SHIFT                                                0x9
#define VPMPCC_OGAM_CONTROL__VPMPCC_OGAM_MODE_MASK                                                            0x00000003L
#define VPMPCC_OGAM_CONTROL__VPMPCC_OGAM_PWL_DISABLE_MASK                                                     0x00000008L
#define VPMPCC_OGAM_CONTROL__VPMPCC_OGAM_MODE_CURRENT_MASK                                                    0x00000180L
#define VPMPCC_OGAM_CONTROL__VPMPCC_OGAM_SELECT_CURRENT_MASK                                                  0x00000200L
//VPMPCC_OGAM_LUT_INDEX
#define VPMPCC_OGAM_LUT_INDEX__VPMPCC_OGAM_LUT_INDEX__SHIFT                                                   0x0
#define VPMPCC_OGAM_LUT_INDEX__VPMPCC_OGAM_LUT_INDEX_MASK                                                     0x000001FFL
//VPMPCC_OGAM_LUT_DATA
#define VPMPCC_OGAM_LUT_DATA__VPMPCC_OGAM_LUT_DATA__SHIFT                                                     0x0
#define VPMPCC_OGAM_LUT_DATA__VPMPCC_OGAM_LUT_DATA_MASK                                                       0x0003FFFFL
//VPMPCC_OGAM_LUT_CONTROL
#define VPMPCC_OGAM_LUT_CONTROL__VPMPCC_OGAM_LUT_WRITE_COLOR_MASK__SHIFT                                      0x0
#define VPMPCC_OGAM_LUT_CONTROL__VPMPCC_OGAM_LUT_READ_COLOR_SEL__SHIFT                                        0x3
#define VPMPCC_OGAM_LUT_CONTROL__VPMPCC_OGAM_LUT_READ_DBG__SHIFT                                              0x5
#define VPMPCC_OGAM_LUT_CONTROL__VPMPCC_OGAM_LUT_HOST_SEL__SHIFT                                              0x6
#define VPMPCC_OGAM_LUT_CONTROL__VPMPCC_OGAM_LUT_CONFIG_MODE__SHIFT                                           0x7
#define VPMPCC_OGAM_LUT_CONTROL__VPMPCC_OGAM_LUT_WRITE_COLOR_MASK_MASK                                        0x00000007L
#define VPMPCC_OGAM_LUT_CONTROL__VPMPCC_OGAM_LUT_READ_COLOR_SEL_MASK                                          0x00000018L
#define VPMPCC_OGAM_LUT_CONTROL__VPMPCC_OGAM_LUT_READ_DBG_MASK                                                0x00000020L
#define VPMPCC_OGAM_LUT_CONTROL__VPMPCC_OGAM_LUT_HOST_SEL_MASK                                                0x00000040L
#define VPMPCC_OGAM_LUT_CONTROL__VPMPCC_OGAM_LUT_CONFIG_MODE_MASK                                             0x00000080L
//VPMPCC_OGAM_RAMA_START_CNTL_B
#define VPMPCC_OGAM_RAMA_START_CNTL_B__VPMPCC_OGAM_RAMA_EXP_REGION_START_B__SHIFT                             0x0
#define VPMPCC_OGAM_RAMA_START_CNTL_B__VPMPCC_OGAM_RAMA_EXP_REGION_START_SEGMENT_B__SHIFT                     0x14
#define VPMPCC_OGAM_RAMA_START_CNTL_B__VPMPCC_OGAM_RAMA_EXP_REGION_START_B_MASK                               0x0003FFFFL
#define VPMPCC_OGAM_RAMA_START_CNTL_B__VPMPCC_OGAM_RAMA_EXP_REGION_START_SEGMENT_B_MASK                       0x07F00000L
//VPMPCC_OGAM_RAMA_START_CNTL_G
#define VPMPCC_OGAM_RAMA_START_CNTL_G__VPMPCC_OGAM_RAMA_EXP_REGION_START_G__SHIFT                             0x0
#define VPMPCC_OGAM_RAMA_START_CNTL_G__VPMPCC_OGAM_RAMA_EXP_REGION_START_SEGMENT_G__SHIFT                     0x14
#define VPMPCC_OGAM_RAMA_START_CNTL_G__VPMPCC_OGAM_RAMA_EXP_REGION_START_G_MASK                               0x0003FFFFL
#define VPMPCC_OGAM_RAMA_START_CNTL_G__VPMPCC_OGAM_RAMA_EXP_REGION_START_SEGMENT_G_MASK                       0x07F00000L
//VPMPCC_OGAM_RAMA_START_CNTL_R
#define VPMPCC_OGAM_RAMA_START_CNTL_R__VPMPCC_OGAM_RAMA_EXP_REGION_START_R__SHIFT                             0x0
#define VPMPCC_OGAM_RAMA_START_CNTL_R__VPMPCC_OGAM_RAMA_EXP_REGION_START_SEGMENT_R__SHIFT                     0x14
#define VPMPCC_OGAM_RAMA_START_CNTL_R__VPMPCC_OGAM_RAMA_EXP_REGION_START_R_MASK                               0x0003FFFFL
#define VPMPCC_OGAM_RAMA_START_CNTL_R__VPMPCC_OGAM_RAMA_EXP_REGION_START_SEGMENT_R_MASK                       0x07F00000L
//VPMPCC_OGAM_RAMA_START_SLOPE_CNTL_B
#define VPMPCC_OGAM_RAMA_START_SLOPE_CNTL_B__VPMPCC_OGAM_RAMA_EXP_REGION_START_SLOPE_B__SHIFT                 0x0
#define VPMPCC_OGAM_RAMA_START_SLOPE_CNTL_B__VPMPCC_OGAM_RAMA_EXP_REGION_START_SLOPE_B_MASK                   0x0003FFFFL
//VPMPCC_OGAM_RAMA_START_SLOPE_CNTL_G
#define VPMPCC_OGAM_RAMA_START_SLOPE_CNTL_G__VPMPCC_OGAM_RAMA_EXP_REGION_START_SLOPE_G__SHIFT                 0x0
#define VPMPCC_OGAM_RAMA_START_SLOPE_CNTL_G__VPMPCC_OGAM_RAMA_EXP_REGION_START_SLOPE_G_MASK                   0x0003FFFFL
//VPMPCC_OGAM_RAMA_START_SLOPE_CNTL_R
#define VPMPCC_OGAM_RAMA_START_SLOPE_CNTL_R__VPMPCC_OGAM_RAMA_EXP_REGION_START_SLOPE_R__SHIFT                 0x0
#define VPMPCC_OGAM_RAMA_START_SLOPE_CNTL_R__VPMPCC_OGAM_RAMA_EXP_REGION_START_SLOPE_R_MASK                   0x0003FFFFL
//VPMPCC_OGAM_RAMA_START_BASE_CNTL_B
#define VPMPCC_OGAM_RAMA_START_BASE_CNTL_B__VPMPCC_OGAM_RAMA_EXP_REGION_START_BASE_B__SHIFT                   0x0
#define VPMPCC_OGAM_RAMA_START_BASE_CNTL_B__VPMPCC_OGAM_RAMA_EXP_REGION_START_BASE_B_MASK                     0x0003FFFFL
//VPMPCC_OGAM_RAMA_START_BASE_CNTL_G
#define VPMPCC_OGAM_RAMA_START_BASE_CNTL_G__VPMPCC_OGAM_RAMA_EXP_REGION_START_BASE_G__SHIFT                   0x0
#define VPMPCC_OGAM_RAMA_START_BASE_CNTL_G__VPMPCC_OGAM_RAMA_EXP_REGION_START_BASE_G_MASK                     0x0003FFFFL
//VPMPCC_OGAM_RAMA_START_BASE_CNTL_R
#define VPMPCC_OGAM_RAMA_START_BASE_CNTL_R__VPMPCC_OGAM_RAMA_EXP_REGION_START_BASE_R__SHIFT                   0x0
#define VPMPCC_OGAM_RAMA_START_BASE_CNTL_R__VPMPCC_OGAM_RAMA_EXP_REGION_START_BASE_R_MASK                     0x0003FFFFL
//VPMPCC_OGAM_RAMA_END_CNTL1_B
#define VPMPCC_OGAM_RAMA_END_CNTL1_B__VPMPCC_OGAM_RAMA_EXP_REGION_END_BASE_B__SHIFT                           0x0
#define VPMPCC_OGAM_RAMA_END_CNTL1_B__VPMPCC_OGAM_RAMA_EXP_REGION_END_BASE_B_MASK                             0x0003FFFFL
//VPMPCC_OGAM_RAMA_END_CNTL2_B
#define VPMPCC_OGAM_RAMA_END_CNTL2_B__VPMPCC_OGAM_RAMA_EXP_REGION_END_B__SHIFT                                0x0
#define VPMPCC_OGAM_RAMA_END_CNTL2_B__VPMPCC_OGAM_RAMA_EXP_REGION_END_SLOPE_B__SHIFT                          0x10
#define VPMPCC_OGAM_RAMA_END_CNTL2_B__VPMPCC_OGAM_RAMA_EXP_REGION_END_B_MASK                                  0x0000FFFFL
#define VPMPCC_OGAM_RAMA_END_CNTL2_B__VPMPCC_OGAM_RAMA_EXP_REGION_END_SLOPE_B_MASK                            0xFFFF0000L
//VPMPCC_OGAM_RAMA_END_CNTL1_G
#define VPMPCC_OGAM_RAMA_END_CNTL1_G__VPMPCC_OGAM_RAMA_EXP_REGION_END_BASE_G__SHIFT                           0x0
#define VPMPCC_OGAM_RAMA_END_CNTL1_G__VPMPCC_OGAM_RAMA_EXP_REGION_END_BASE_G_MASK                             0x0003FFFFL
//VPMPCC_OGAM_RAMA_END_CNTL2_G
#define VPMPCC_OGAM_RAMA_END_CNTL2_G__VPMPCC_OGAM_RAMA_EXP_REGION_END_G__SHIFT                                0x0
#define VPMPCC_OGAM_RAMA_END_CNTL2_G__VPMPCC_OGAM_RAMA_EXP_REGION_END_SLOPE_G__SHIFT                          0x10
#define VPMPCC_OGAM_RAMA_END_CNTL2_G__VPMPCC_OGAM_RAMA_EXP_REGION_END_G_MASK                                  0x0000FFFFL
#define VPMPCC_OGAM_RAMA_END_CNTL2_G__VPMPCC_OGAM_RAMA_EXP_REGION_END_SLOPE_G_MASK                            0xFFFF0000L
//VPMPCC_OGAM_RAMA_END_CNTL1_R
#define VPMPCC_OGAM_RAMA_END_CNTL1_R__VPMPCC_OGAM_RAMA_EXP_REGION_END_BASE_R__SHIFT                           0x0
#define VPMPCC_OGAM_RAMA_END_CNTL1_R__VPMPCC_OGAM_RAMA_EXP_REGION_END_BASE_R_MASK                             0x0003FFFFL
//VPMPCC_OGAM_RAMA_END_CNTL2_R
#define VPMPCC_OGAM_RAMA_END_CNTL2_R__VPMPCC_OGAM_RAMA_EXP_REGION_END_R__SHIFT                                0x0
#define VPMPCC_OGAM_RAMA_END_CNTL2_R__VPMPCC_OGAM_RAMA_EXP_REGION_END_SLOPE_R__SHIFT                          0x10
#define VPMPCC_OGAM_RAMA_END_CNTL2_R__VPMPCC_OGAM_RAMA_EXP_REGION_END_R_MASK                                  0x0000FFFFL
#define VPMPCC_OGAM_RAMA_END_CNTL2_R__VPMPCC_OGAM_RAMA_EXP_REGION_END_SLOPE_R_MASK                            0xFFFF0000L
//VPMPCC_OGAM_RAMA_OFFSET_B
#define VPMPCC_OGAM_RAMA_OFFSET_B__VPMPCC_OGAM_RAMA_OFFSET_B__SHIFT                                           0x0
#define VPMPCC_OGAM_RAMA_OFFSET_B__VPMPCC_OGAM_RAMA_OFFSET_B_MASK                                             0x0007FFFFL
//VPMPCC_OGAM_RAMA_OFFSET_G
#define VPMPCC_OGAM_RAMA_OFFSET_G__VPMPCC_OGAM_RAMA_OFFSET_G__SHIFT                                           0x0
#define VPMPCC_OGAM_RAMA_OFFSET_G__VPMPCC_OGAM_RAMA_OFFSET_G_MASK                                             0x0007FFFFL
//VPMPCC_OGAM_RAMA_OFFSET_R
#define VPMPCC_OGAM_RAMA_OFFSET_R__VPMPCC_OGAM_RAMA_OFFSET_R__SHIFT                                           0x0
#define VPMPCC_OGAM_RAMA_OFFSET_R__VPMPCC_OGAM_RAMA_OFFSET_R_MASK                                             0x0007FFFFL
//VPMPCC_OGAM_RAMA_REGION_0_1
#define VPMPCC_OGAM_RAMA_REGION_0_1__VPMPCC_OGAM_RAMA_EXP_REGION0_LUT_OFFSET__SHIFT                           0x0
#define VPMPCC_OGAM_RAMA_REGION_0_1__VPMPCC_OGAM_RAMA_EXP_REGION0_NUM_SEGMENTS__SHIFT                         0xc
#define VPMPCC_OGAM_RAMA_REGION_0_1__VPMPCC_OGAM_RAMA_EXP_REGION1_LUT_OFFSET__SHIFT                           0x10
#define VPMPCC_OGAM_RAMA_REGION_0_1__VPMPCC_OGAM_RAMA_EXP_REGION1_NUM_SEGMENTS__SHIFT                         0x1c
#define VPMPCC_OGAM_RAMA_REGION_0_1__VPMPCC_OGAM_RAMA_EXP_REGION0_LUT_OFFSET_MASK                             0x000001FFL
#define VPMPCC_OGAM_RAMA_REGION_0_1__VPMPCC_OGAM_RAMA_EXP_REGION0_NUM_SEGMENTS_MASK                           0x00007000L
#define VPMPCC_OGAM_RAMA_REGION_0_1__VPMPCC_OGAM_RAMA_EXP_REGION1_LUT_OFFSET_MASK                             0x01FF0000L
#define VPMPCC_OGAM_RAMA_REGION_0_1__VPMPCC_OGAM_RAMA_EXP_REGION1_NUM_SEGMENTS_MASK                           0x70000000L
//VPMPCC_OGAM_RAMA_REGION_2_3
#define VPMPCC_OGAM_RAMA_REGION_2_3__VPMPCC_OGAM_RAMA_EXP_REGION2_LUT_OFFSET__SHIFT                           0x0
#define VPMPCC_OGAM_RAMA_REGION_2_3__VPMPCC_OGAM_RAMA_EXP_REGION2_NUM_SEGMENTS__SHIFT                         0xc
#define VPMPCC_OGAM_RAMA_REGION_2_3__VPMPCC_OGAM_RAMA_EXP_REGION3_LUT_OFFSET__SHIFT                           0x10
#define VPMPCC_OGAM_RAMA_REGION_2_3__VPMPCC_OGAM_RAMA_EXP_REGION3_NUM_SEGMENTS__SHIFT                         0x1c
#define VPMPCC_OGAM_RAMA_REGION_2_3__VPMPCC_OGAM_RAMA_EXP_REGION2_LUT_OFFSET_MASK                             0x000001FFL
#define VPMPCC_OGAM_RAMA_REGION_2_3__VPMPCC_OGAM_RAMA_EXP_REGION2_NUM_SEGMENTS_MASK                           0x00007000L
#define VPMPCC_OGAM_RAMA_REGION_2_3__VPMPCC_OGAM_RAMA_EXP_REGION3_LUT_OFFSET_MASK                             0x01FF0000L
#define VPMPCC_OGAM_RAMA_REGION_2_3__VPMPCC_OGAM_RAMA_EXP_REGION3_NUM_SEGMENTS_MASK                           0x70000000L
//VPMPCC_OGAM_RAMA_REGION_4_5
#define VPMPCC_OGAM_RAMA_REGION_4_5__VPMPCC_OGAM_RAMA_EXP_REGION4_LUT_OFFSET__SHIFT                           0x0
#define VPMPCC_OGAM_RAMA_REGION_4_5__VPMPCC_OGAM_RAMA_EXP_REGION4_NUM_SEGMENTS__SHIFT                         0xc
#define VPMPCC_OGAM_RAMA_REGION_4_5__VPMPCC_OGAM_RAMA_EXP_REGION5_LUT_OFFSET__SHIFT                           0x10
#define VPMPCC_OGAM_RAMA_REGION_4_5__VPMPCC_OGAM_RAMA_EXP_REGION5_NUM_SEGMENTS__SHIFT                         0x1c
#define VPMPCC_OGAM_RAMA_REGION_4_5__VPMPCC_OGAM_RAMA_EXP_REGION4_LUT_OFFSET_MASK                             0x000001FFL
#define VPMPCC_OGAM_RAMA_REGION_4_5__VPMPCC_OGAM_RAMA_EXP_REGION4_NUM_SEGMENTS_MASK                           0x00007000L
#define VPMPCC_OGAM_RAMA_REGION_4_5__VPMPCC_OGAM_RAMA_EXP_REGION5_LUT_OFFSET_MASK                             0x01FF0000L
#define VPMPCC_OGAM_RAMA_REGION_4_5__VPMPCC_OGAM_RAMA_EXP_REGION5_NUM_SEGMENTS_MASK                           0x70000000L
//VPMPCC_OGAM_RAMA_REGION_6_7
#define VPMPCC_OGAM_RAMA_REGION_6_7__VPMPCC_OGAM_RAMA_EXP_REGION6_LUT_OFFSET__SHIFT                           0x0
#define VPMPCC_OGAM_RAMA_REGION_6_7__VPMPCC_OGAM_RAMA_EXP_REGION6_NUM_SEGMENTS__SHIFT                         0xc
#define VPMPCC_OGAM_RAMA_REGION_6_7__VPMPCC_OGAM_RAMA_EXP_REGION7_LUT_OFFSET__SHIFT                           0x10
#define VPMPCC_OGAM_RAMA_REGION_6_7__VPMPCC_OGAM_RAMA_EXP_REGION7_NUM_SEGMENTS__SHIFT                         0x1c
#define VPMPCC_OGAM_RAMA_REGION_6_7__VPMPCC_OGAM_RAMA_EXP_REGION6_LUT_OFFSET_MASK                             0x000001FFL
#define VPMPCC_OGAM_RAMA_REGION_6_7__VPMPCC_OGAM_RAMA_EXP_REGION6_NUM_SEGMENTS_MASK                           0x00007000L
#define VPMPCC_OGAM_RAMA_REGION_6_7__VPMPCC_OGAM_RAMA_EXP_REGION7_LUT_OFFSET_MASK                             0x01FF0000L
#define VPMPCC_OGAM_RAMA_REGION_6_7__VPMPCC_OGAM_RAMA_EXP_REGION7_NUM_SEGMENTS_MASK                           0x70000000L
//VPMPCC_OGAM_RAMA_REGION_8_9
#define VPMPCC_OGAM_RAMA_REGION_8_9__VPMPCC_OGAM_RAMA_EXP_REGION8_LUT_OFFSET__SHIFT                           0x0
#define VPMPCC_OGAM_RAMA_REGION_8_9__VPMPCC_OGAM_RAMA_EXP_REGION8_NUM_SEGMENTS__SHIFT                         0xc
#define VPMPCC_OGAM_RAMA_REGION_8_9__VPMPCC_OGAM_RAMA_EXP_REGION9_LUT_OFFSET__SHIFT                           0x10
#define VPMPCC_OGAM_RAMA_REGION_8_9__VPMPCC_OGAM_RAMA_EXP_REGION9_NUM_SEGMENTS__SHIFT                         0x1c
#define VPMPCC_OGAM_RAMA_REGION_8_9__VPMPCC_OGAM_RAMA_EXP_REGION8_LUT_OFFSET_MASK                             0x000001FFL
#define VPMPCC_OGAM_RAMA_REGION_8_9__VPMPCC_OGAM_RAMA_EXP_REGION8_NUM_SEGMENTS_MASK                           0x00007000L
#define VPMPCC_OGAM_RAMA_REGION_8_9__VPMPCC_OGAM_RAMA_EXP_REGION9_LUT_OFFSET_MASK                             0x01FF0000L
#define VPMPCC_OGAM_RAMA_REGION_8_9__VPMPCC_OGAM_RAMA_EXP_REGION9_NUM_SEGMENTS_MASK                           0x70000000L
//VPMPCC_OGAM_RAMA_REGION_10_11
#define VPMPCC_OGAM_RAMA_REGION_10_11__VPMPCC_OGAM_RAMA_EXP_REGION10_LUT_OFFSET__SHIFT                        0x0
#define VPMPCC_OGAM_RAMA_REGION_10_11__VPMPCC_OGAM_RAMA_EXP_REGION10_NUM_SEGMENTS__SHIFT                      0xc
#define VPMPCC_OGAM_RAMA_REGION_10_11__VPMPCC_OGAM_RAMA_EXP_REGION11_LUT_OFFSET__SHIFT                        0x10
#define VPMPCC_OGAM_RAMA_REGION_10_11__VPMPCC_OGAM_RAMA_EXP_REGION11_NUM_SEGMENTS__SHIFT                      0x1c
#define VPMPCC_OGAM_RAMA_REGION_10_11__VPMPCC_OGAM_RAMA_EXP_REGION10_LUT_OFFSET_MASK                          0x000001FFL
#define VPMPCC_OGAM_RAMA_REGION_10_11__VPMPCC_OGAM_RAMA_EXP_REGION10_NUM_SEGMENTS_MASK                        0x00007000L
#define VPMPCC_OGAM_RAMA_REGION_10_11__VPMPCC_OGAM_RAMA_EXP_REGION11_LUT_OFFSET_MASK                          0x01FF0000L
#define VPMPCC_OGAM_RAMA_REGION_10_11__VPMPCC_OGAM_RAMA_EXP_REGION11_NUM_SEGMENTS_MASK                        0x70000000L
//VPMPCC_OGAM_RAMA_REGION_12_13
#define VPMPCC_OGAM_RAMA_REGION_12_13__VPMPCC_OGAM_RAMA_EXP_REGION12_LUT_OFFSET__SHIFT                        0x0
#define VPMPCC_OGAM_RAMA_REGION_12_13__VPMPCC_OGAM_RAMA_EXP_REGION12_NUM_SEGMENTS__SHIFT                      0xc
#define VPMPCC_OGAM_RAMA_REGION_12_13__VPMPCC_OGAM_RAMA_EXP_REGION13_LUT_OFFSET__SHIFT                        0x10
#define VPMPCC_OGAM_RAMA_REGION_12_13__VPMPCC_OGAM_RAMA_EXP_REGION13_NUM_SEGMENTS__SHIFT                      0x1c
#define VPMPCC_OGAM_RAMA_REGION_12_13__VPMPCC_OGAM_RAMA_EXP_REGION12_LUT_OFFSET_MASK                          0x000001FFL
#define VPMPCC_OGAM_RAMA_REGION_12_13__VPMPCC_OGAM_RAMA_EXP_REGION12_NUM_SEGMENTS_MASK                        0x00007000L
#define VPMPCC_OGAM_RAMA_REGION_12_13__VPMPCC_OGAM_RAMA_EXP_REGION13_LUT_OFFSET_MASK                          0x01FF0000L
#define VPMPCC_OGAM_RAMA_REGION_12_13__VPMPCC_OGAM_RAMA_EXP_REGION13_NUM_SEGMENTS_MASK                        0x70000000L
//VPMPCC_OGAM_RAMA_REGION_14_15
#define VPMPCC_OGAM_RAMA_REGION_14_15__VPMPCC_OGAM_RAMA_EXP_REGION14_LUT_OFFSET__SHIFT                        0x0
#define VPMPCC_OGAM_RAMA_REGION_14_15__VPMPCC_OGAM_RAMA_EXP_REGION14_NUM_SEGMENTS__SHIFT                      0xc
#define VPMPCC_OGAM_RAMA_REGION_14_15__VPMPCC_OGAM_RAMA_EXP_REGION15_LUT_OFFSET__SHIFT                        0x10
#define VPMPCC_OGAM_RAMA_REGION_14_15__VPMPCC_OGAM_RAMA_EXP_REGION15_NUM_SEGMENTS__SHIFT                      0x1c
#define VPMPCC_OGAM_RAMA_REGION_14_15__VPMPCC_OGAM_RAMA_EXP_REGION14_LUT_OFFSET_MASK                          0x000001FFL
#define VPMPCC_OGAM_RAMA_REGION_14_15__VPMPCC_OGAM_RAMA_EXP_REGION14_NUM_SEGMENTS_MASK                        0x00007000L
#define VPMPCC_OGAM_RAMA_REGION_14_15__VPMPCC_OGAM_RAMA_EXP_REGION15_LUT_OFFSET_MASK                          0x01FF0000L
#define VPMPCC_OGAM_RAMA_REGION_14_15__VPMPCC_OGAM_RAMA_EXP_REGION15_NUM_SEGMENTS_MASK                        0x70000000L
//VPMPCC_OGAM_RAMA_REGION_16_17
#define VPMPCC_OGAM_RAMA_REGION_16_17__VPMPCC_OGAM_RAMA_EXP_REGION16_LUT_OFFSET__SHIFT                        0x0
#define VPMPCC_OGAM_RAMA_REGION_16_17__VPMPCC_OGAM_RAMA_EXP_REGION16_NUM_SEGMENTS__SHIFT                      0xc
#define VPMPCC_OGAM_RAMA_REGION_16_17__VPMPCC_OGAM_RAMA_EXP_REGION17_LUT_OFFSET__SHIFT                        0x10
#define VPMPCC_OGAM_RAMA_REGION_16_17__VPMPCC_OGAM_RAMA_EXP_REGION17_NUM_SEGMENTS__SHIFT                      0x1c
#define VPMPCC_OGAM_RAMA_REGION_16_17__VPMPCC_OGAM_RAMA_EXP_REGION16_LUT_OFFSET_MASK                          0x000001FFL
#define VPMPCC_OGAM_RAMA_REGION_16_17__VPMPCC_OGAM_RAMA_EXP_REGION16_NUM_SEGMENTS_MASK                        0x00007000L
#define VPMPCC_OGAM_RAMA_REGION_16_17__VPMPCC_OGAM_RAMA_EXP_REGION17_LUT_OFFSET_MASK                          0x01FF0000L
#define VPMPCC_OGAM_RAMA_REGION_16_17__VPMPCC_OGAM_RAMA_EXP_REGION17_NUM_SEGMENTS_MASK                        0x70000000L
//VPMPCC_OGAM_RAMA_REGION_18_19
#define VPMPCC_OGAM_RAMA_REGION_18_19__VPMPCC_OGAM_RAMA_EXP_REGION18_LUT_OFFSET__SHIFT                        0x0
#define VPMPCC_OGAM_RAMA_REGION_18_19__VPMPCC_OGAM_RAMA_EXP_REGION18_NUM_SEGMENTS__SHIFT                      0xc
#define VPMPCC_OGAM_RAMA_REGION_18_19__VPMPCC_OGAM_RAMA_EXP_REGION19_LUT_OFFSET__SHIFT                        0x10
#define VPMPCC_OGAM_RAMA_REGION_18_19__VPMPCC_OGAM_RAMA_EXP_REGION19_NUM_SEGMENTS__SHIFT                      0x1c
#define VPMPCC_OGAM_RAMA_REGION_18_19__VPMPCC_OGAM_RAMA_EXP_REGION18_LUT_OFFSET_MASK                          0x000001FFL
#define VPMPCC_OGAM_RAMA_REGION_18_19__VPMPCC_OGAM_RAMA_EXP_REGION18_NUM_SEGMENTS_MASK                        0x00007000L
#define VPMPCC_OGAM_RAMA_REGION_18_19__VPMPCC_OGAM_RAMA_EXP_REGION19_LUT_OFFSET_MASK                          0x01FF0000L
#define VPMPCC_OGAM_RAMA_REGION_18_19__VPMPCC_OGAM_RAMA_EXP_REGION19_NUM_SEGMENTS_MASK                        0x70000000L
//VPMPCC_OGAM_RAMA_REGION_20_21
#define VPMPCC_OGAM_RAMA_REGION_20_21__VPMPCC_OGAM_RAMA_EXP_REGION20_LUT_OFFSET__SHIFT                        0x0
#define VPMPCC_OGAM_RAMA_REGION_20_21__VPMPCC_OGAM_RAMA_EXP_REGION20_NUM_SEGMENTS__SHIFT                      0xc
#define VPMPCC_OGAM_RAMA_REGION_20_21__VPMPCC_OGAM_RAMA_EXP_REGION21_LUT_OFFSET__SHIFT                        0x10
#define VPMPCC_OGAM_RAMA_REGION_20_21__VPMPCC_OGAM_RAMA_EXP_REGION21_NUM_SEGMENTS__SHIFT                      0x1c
#define VPMPCC_OGAM_RAMA_REGION_20_21__VPMPCC_OGAM_RAMA_EXP_REGION20_LUT_OFFSET_MASK                          0x000001FFL
#define VPMPCC_OGAM_RAMA_REGION_20_21__VPMPCC_OGAM_RAMA_EXP_REGION20_NUM_SEGMENTS_MASK                        0x00007000L
#define VPMPCC_OGAM_RAMA_REGION_20_21__VPMPCC_OGAM_RAMA_EXP_REGION21_LUT_OFFSET_MASK                          0x01FF0000L
#define VPMPCC_OGAM_RAMA_REGION_20_21__VPMPCC_OGAM_RAMA_EXP_REGION21_NUM_SEGMENTS_MASK                        0x70000000L
//VPMPCC_OGAM_RAMA_REGION_22_23
#define VPMPCC_OGAM_RAMA_REGION_22_23__VPMPCC_OGAM_RAMA_EXP_REGION22_LUT_OFFSET__SHIFT                        0x0
#define VPMPCC_OGAM_RAMA_REGION_22_23__VPMPCC_OGAM_RAMA_EXP_REGION22_NUM_SEGMENTS__SHIFT                      0xc
#define VPMPCC_OGAM_RAMA_REGION_22_23__VPMPCC_OGAM_RAMA_EXP_REGION23_LUT_OFFSET__SHIFT                        0x10
#define VPMPCC_OGAM_RAMA_REGION_22_23__VPMPCC_OGAM_RAMA_EXP_REGION23_NUM_SEGMENTS__SHIFT                      0x1c
#define VPMPCC_OGAM_RAMA_REGION_22_23__VPMPCC_OGAM_RAMA_EXP_REGION22_LUT_OFFSET_MASK                          0x000001FFL
#define VPMPCC_OGAM_RAMA_REGION_22_23__VPMPCC_OGAM_RAMA_EXP_REGION22_NUM_SEGMENTS_MASK                        0x00007000L
#define VPMPCC_OGAM_RAMA_REGION_22_23__VPMPCC_OGAM_RAMA_EXP_REGION23_LUT_OFFSET_MASK                          0x01FF0000L
#define VPMPCC_OGAM_RAMA_REGION_22_23__VPMPCC_OGAM_RAMA_EXP_REGION23_NUM_SEGMENTS_MASK                        0x70000000L
//VPMPCC_OGAM_RAMA_REGION_24_25
#define VPMPCC_OGAM_RAMA_REGION_24_25__VPMPCC_OGAM_RAMA_EXP_REGION24_LUT_OFFSET__SHIFT                        0x0
#define VPMPCC_OGAM_RAMA_REGION_24_25__VPMPCC_OGAM_RAMA_EXP_REGION24_NUM_SEGMENTS__SHIFT                      0xc
#define VPMPCC_OGAM_RAMA_REGION_24_25__VPMPCC_OGAM_RAMA_EXP_REGION25_LUT_OFFSET__SHIFT                        0x10
#define VPMPCC_OGAM_RAMA_REGION_24_25__VPMPCC_OGAM_RAMA_EXP_REGION25_NUM_SEGMENTS__SHIFT                      0x1c
#define VPMPCC_OGAM_RAMA_REGION_24_25__VPMPCC_OGAM_RAMA_EXP_REGION24_LUT_OFFSET_MASK                          0x000001FFL
#define VPMPCC_OGAM_RAMA_REGION_24_25__VPMPCC_OGAM_RAMA_EXP_REGION24_NUM_SEGMENTS_MASK                        0x00007000L
#define VPMPCC_OGAM_RAMA_REGION_24_25__VPMPCC_OGAM_RAMA_EXP_REGION25_LUT_OFFSET_MASK                          0x01FF0000L
#define VPMPCC_OGAM_RAMA_REGION_24_25__VPMPCC_OGAM_RAMA_EXP_REGION25_NUM_SEGMENTS_MASK                        0x70000000L
//VPMPCC_OGAM_RAMA_REGION_26_27
#define VPMPCC_OGAM_RAMA_REGION_26_27__VPMPCC_OGAM_RAMA_EXP_REGION26_LUT_OFFSET__SHIFT                        0x0
#define VPMPCC_OGAM_RAMA_REGION_26_27__VPMPCC_OGAM_RAMA_EXP_REGION26_NUM_SEGMENTS__SHIFT                      0xc
#define VPMPCC_OGAM_RAMA_REGION_26_27__VPMPCC_OGAM_RAMA_EXP_REGION27_LUT_OFFSET__SHIFT                        0x10
#define VPMPCC_OGAM_RAMA_REGION_26_27__VPMPCC_OGAM_RAMA_EXP_REGION27_NUM_SEGMENTS__SHIFT                      0x1c
#define VPMPCC_OGAM_RAMA_REGION_26_27__VPMPCC_OGAM_RAMA_EXP_REGION26_LUT_OFFSET_MASK                          0x000001FFL
#define VPMPCC_OGAM_RAMA_REGION_26_27__VPMPCC_OGAM_RAMA_EXP_REGION26_NUM_SEGMENTS_MASK                        0x00007000L
#define VPMPCC_OGAM_RAMA_REGION_26_27__VPMPCC_OGAM_RAMA_EXP_REGION27_LUT_OFFSET_MASK                          0x01FF0000L
#define VPMPCC_OGAM_RAMA_REGION_26_27__VPMPCC_OGAM_RAMA_EXP_REGION27_NUM_SEGMENTS_MASK                        0x70000000L
//VPMPCC_OGAM_RAMA_REGION_28_29
#define VPMPCC_OGAM_RAMA_REGION_28_29__VPMPCC_OGAM_RAMA_EXP_REGION28_LUT_OFFSET__SHIFT                        0x0
#define VPMPCC_OGAM_RAMA_REGION_28_29__VPMPCC_OGAM_RAMA_EXP_REGION28_NUM_SEGMENTS__SHIFT                      0xc
#define VPMPCC_OGAM_RAMA_REGION_28_29__VPMPCC_OGAM_RAMA_EXP_REGION29_LUT_OFFSET__SHIFT                        0x10
#define VPMPCC_OGAM_RAMA_REGION_28_29__VPMPCC_OGAM_RAMA_EXP_REGION29_NUM_SEGMENTS__SHIFT                      0x1c
#define VPMPCC_OGAM_RAMA_REGION_28_29__VPMPCC_OGAM_RAMA_EXP_REGION28_LUT_OFFSET_MASK                          0x000001FFL
#define VPMPCC_OGAM_RAMA_REGION_28_29__VPMPCC_OGAM_RAMA_EXP_REGION28_NUM_SEGMENTS_MASK                        0x00007000L
#define VPMPCC_OGAM_RAMA_REGION_28_29__VPMPCC_OGAM_RAMA_EXP_REGION29_LUT_OFFSET_MASK                          0x01FF0000L
#define VPMPCC_OGAM_RAMA_REGION_28_29__VPMPCC_OGAM_RAMA_EXP_REGION29_NUM_SEGMENTS_MASK                        0x70000000L
//VPMPCC_OGAM_RAMA_REGION_30_31
#define VPMPCC_OGAM_RAMA_REGION_30_31__VPMPCC_OGAM_RAMA_EXP_REGION30_LUT_OFFSET__SHIFT                        0x0
#define VPMPCC_OGAM_RAMA_REGION_30_31__VPMPCC_OGAM_RAMA_EXP_REGION30_NUM_SEGMENTS__SHIFT                      0xc
#define VPMPCC_OGAM_RAMA_REGION_30_31__VPMPCC_OGAM_RAMA_EXP_REGION31_LUT_OFFSET__SHIFT                        0x10
#define VPMPCC_OGAM_RAMA_REGION_30_31__VPMPCC_OGAM_RAMA_EXP_REGION31_NUM_SEGMENTS__SHIFT                      0x1c
#define VPMPCC_OGAM_RAMA_REGION_30_31__VPMPCC_OGAM_RAMA_EXP_REGION30_LUT_OFFSET_MASK                          0x000001FFL
#define VPMPCC_OGAM_RAMA_REGION_30_31__VPMPCC_OGAM_RAMA_EXP_REGION30_NUM_SEGMENTS_MASK                        0x00007000L
#define VPMPCC_OGAM_RAMA_REGION_30_31__VPMPCC_OGAM_RAMA_EXP_REGION31_LUT_OFFSET_MASK                          0x01FF0000L
#define VPMPCC_OGAM_RAMA_REGION_30_31__VPMPCC_OGAM_RAMA_EXP_REGION31_NUM_SEGMENTS_MASK                        0x70000000L
//VPMPCC_OGAM_RAMA_REGION_32_33
#define VPMPCC_OGAM_RAMA_REGION_32_33__VPMPCC_OGAM_RAMA_EXP_REGION32_LUT_OFFSET__SHIFT                        0x0
#define VPMPCC_OGAM_RAMA_REGION_32_33__VPMPCC_OGAM_RAMA_EXP_REGION32_NUM_SEGMENTS__SHIFT                      0xc
#define VPMPCC_OGAM_RAMA_REGION_32_33__VPMPCC_OGAM_RAMA_EXP_REGION33_LUT_OFFSET__SHIFT                        0x10
#define VPMPCC_OGAM_RAMA_REGION_32_33__VPMPCC_OGAM_RAMA_EXP_REGION33_NUM_SEGMENTS__SHIFT                      0x1c
#define VPMPCC_OGAM_RAMA_REGION_32_33__VPMPCC_OGAM_RAMA_EXP_REGION32_LUT_OFFSET_MASK                          0x000001FFL
#define VPMPCC_OGAM_RAMA_REGION_32_33__VPMPCC_OGAM_RAMA_EXP_REGION32_NUM_SEGMENTS_MASK                        0x00007000L
#define VPMPCC_OGAM_RAMA_REGION_32_33__VPMPCC_OGAM_RAMA_EXP_REGION33_LUT_OFFSET_MASK                          0x01FF0000L
#define VPMPCC_OGAM_RAMA_REGION_32_33__VPMPCC_OGAM_RAMA_EXP_REGION33_NUM_SEGMENTS_MASK                        0x70000000L
//VPMPCC_GAMUT_REMAP_COEF_FORMAT
#define VPMPCC_GAMUT_REMAP_COEF_FORMAT__VPMPCC_GAMUT_REMAP_COEF_FORMAT__SHIFT                                 0x0
#define VPMPCC_GAMUT_REMAP_COEF_FORMAT__VPMPCC_GAMUT_REMAP_COEF_FORMAT_MASK                                   0x00000001L
//VPMPCC_GAMUT_REMAP_MODE
#define VPMPCC_GAMUT_REMAP_MODE__VPMPCC_GAMUT_REMAP_MODE__SHIFT                                               0x0
#define VPMPCC_GAMUT_REMAP_MODE__VPMPCC_GAMUT_REMAP_MODE_CURRENT__SHIFT                                       0x7
#define VPMPCC_GAMUT_REMAP_MODE__VPMPCC_GAMUT_REMAP_MODE_MASK                                                 0x00000001L
#define VPMPCC_GAMUT_REMAP_MODE__VPMPCC_GAMUT_REMAP_MODE_CURRENT_MASK                                         0x00000080L
//VPMPC_GAMUT_REMAP_C11_C12_A
#define VPMPC_GAMUT_REMAP_C11_C12_A__VPMPCC_GAMUT_REMAP_C11_A__SHIFT                                          0x0
#define VPMPC_GAMUT_REMAP_C11_C12_A__VPMPCC_GAMUT_REMAP_C12_A__SHIFT                                          0x10
#define VPMPC_GAMUT_REMAP_C11_C12_A__VPMPCC_GAMUT_REMAP_C11_A_MASK                                            0x0000FFFFL
#define VPMPC_GAMUT_REMAP_C11_C12_A__VPMPCC_GAMUT_REMAP_C12_A_MASK                                            0xFFFF0000L
//VPMPC_GAMUT_REMAP_C13_C14_A
#define VPMPC_GAMUT_REMAP_C13_C14_A__VPMPCC_GAMUT_REMAP_C13_A__SHIFT                                          0x0
#define VPMPC_GAMUT_REMAP_C13_C14_A__VPMPCC_GAMUT_REMAP_C14_A__SHIFT                                          0x10
#define VPMPC_GAMUT_REMAP_C13_C14_A__VPMPCC_GAMUT_REMAP_C13_A_MASK                                            0x0000FFFFL
#define VPMPC_GAMUT_REMAP_C13_C14_A__VPMPCC_GAMUT_REMAP_C14_A_MASK                                            0xFFFF0000L
//VPMPC_GAMUT_REMAP_C21_C22_A
#define VPMPC_GAMUT_REMAP_C21_C22_A__VPMPCC_GAMUT_REMAP_C21_A__SHIFT                                          0x0
#define VPMPC_GAMUT_REMAP_C21_C22_A__VPMPCC_GAMUT_REMAP_C22_A__SHIFT                                          0x10
#define VPMPC_GAMUT_REMAP_C21_C22_A__VPMPCC_GAMUT_REMAP_C21_A_MASK                                            0x0000FFFFL
#define VPMPC_GAMUT_REMAP_C21_C22_A__VPMPCC_GAMUT_REMAP_C22_A_MASK                                            0xFFFF0000L
//VPMPC_GAMUT_REMAP_C23_C24_A
#define VPMPC_GAMUT_REMAP_C23_C24_A__VPMPCC_GAMUT_REMAP_C23_A__SHIFT                                          0x0
#define VPMPC_GAMUT_REMAP_C23_C24_A__VPMPCC_GAMUT_REMAP_C24_A__SHIFT                                          0x10
#define VPMPC_GAMUT_REMAP_C23_C24_A__VPMPCC_GAMUT_REMAP_C23_A_MASK                                            0x0000FFFFL
#define VPMPC_GAMUT_REMAP_C23_C24_A__VPMPCC_GAMUT_REMAP_C24_A_MASK                                            0xFFFF0000L
//VPMPC_GAMUT_REMAP_C31_C32_A
#define VPMPC_GAMUT_REMAP_C31_C32_A__VPMPCC_GAMUT_REMAP_C31_A__SHIFT                                          0x0
#define VPMPC_GAMUT_REMAP_C31_C32_A__VPMPCC_GAMUT_REMAP_C32_A__SHIFT                                          0x10
#define VPMPC_GAMUT_REMAP_C31_C32_A__VPMPCC_GAMUT_REMAP_C31_A_MASK                                            0x0000FFFFL
#define VPMPC_GAMUT_REMAP_C31_C32_A__VPMPCC_GAMUT_REMAP_C32_A_MASK                                            0xFFFF0000L
//VPMPC_GAMUT_REMAP_C33_C34_A
#define VPMPC_GAMUT_REMAP_C33_C34_A__VPMPCC_GAMUT_REMAP_C33_A__SHIFT                                          0x0
#define VPMPC_GAMUT_REMAP_C33_C34_A__VPMPCC_GAMUT_REMAP_C34_A__SHIFT                                          0x10
#define VPMPC_GAMUT_REMAP_C33_C34_A__VPMPCC_GAMUT_REMAP_C33_A_MASK                                            0x0000FFFFL
#define VPMPC_GAMUT_REMAP_C33_C34_A__VPMPCC_GAMUT_REMAP_C34_A_MASK                                            0xFFFF0000L


// addressBlock: vpe_vpep_vpmpc_vpmpcc_mcm0_dispdec
//VPMPCC_MCM_SHAPER_CONTROL
#define VPMPCC_MCM_SHAPER_CONTROL__VPMPCC_MCM_SHAPER_LUT_MODE__SHIFT                                          0x0
#define VPMPCC_MCM_SHAPER_CONTROL__VPMPCC_MCM_SHAPER_MODE_CURRENT__SHIFT                                      0x2
#define VPMPCC_MCM_SHAPER_CONTROL__VPMPCC_MCM_SHAPER_SELECT_CURRENT__SHIFT                                    0x4
#define VPMPCC_MCM_SHAPER_CONTROL__VPMPCC_MCM_SHAPER_LUT_MODE_MASK                                            0x00000003L
#define VPMPCC_MCM_SHAPER_CONTROL__VPMPCC_MCM_SHAPER_MODE_CURRENT_MASK                                        0x0000000CL
#define VPMPCC_MCM_SHAPER_CONTROL__VPMPCC_MCM_SHAPER_SELECT_CURRENT_MASK                                      0x00000010L
//VPMPCC_MCM_SHAPER_OFFSET_R
#define VPMPCC_MCM_SHAPER_OFFSET_R__VPMPCC_MCM_SHAPER_OFFSET_R__SHIFT                                         0x0
#define VPMPCC_MCM_SHAPER_OFFSET_R__VPMPCC_MCM_SHAPER_OFFSET_R_MASK                                           0x0007FFFFL
//VPMPCC_MCM_SHAPER_OFFSET_G
#define VPMPCC_MCM_SHAPER_OFFSET_G__VPMPCC_MCM_SHAPER_OFFSET_G__SHIFT                                         0x0
#define VPMPCC_MCM_SHAPER_OFFSET_G__VPMPCC_MCM_SHAPER_OFFSET_G_MASK                                           0x0007FFFFL
//VPMPCC_MCM_SHAPER_OFFSET_B
#define VPMPCC_MCM_SHAPER_OFFSET_B__VPMPCC_MCM_SHAPER_OFFSET_B__SHIFT                                         0x0
#define VPMPCC_MCM_SHAPER_OFFSET_B__VPMPCC_MCM_SHAPER_OFFSET_B_MASK                                           0x0007FFFFL
//VPMPCC_MCM_SHAPER_SCALE_R
#define VPMPCC_MCM_SHAPER_SCALE_R__VPMPCC_MCM_SHAPER_SCALE_R__SHIFT                                           0x0
#define VPMPCC_MCM_SHAPER_SCALE_R__VPMPCC_MCM_SHAPER_SCALE_R_MASK                                             0x0000FFFFL
//VPMPCC_MCM_SHAPER_SCALE_G_B
#define VPMPCC_MCM_SHAPER_SCALE_G_B__VPMPCC_MCM_SHAPER_SCALE_G__SHIFT                                         0x0
#define VPMPCC_MCM_SHAPER_SCALE_G_B__VPMPCC_MCM_SHAPER_SCALE_B__SHIFT                                         0x10
#define VPMPCC_MCM_SHAPER_SCALE_G_B__VPMPCC_MCM_SHAPER_SCALE_G_MASK                                           0x0000FFFFL
#define VPMPCC_MCM_SHAPER_SCALE_G_B__VPMPCC_MCM_SHAPER_SCALE_B_MASK                                           0xFFFF0000L
//VPMPCC_MCM_SHAPER_LUT_INDEX
#define VPMPCC_MCM_SHAPER_LUT_INDEX__VPMPCC_MCM_SHAPER_LUT_INDEX__SHIFT                                       0x0
#define VPMPCC_MCM_SHAPER_LUT_INDEX__VPMPCC_MCM_SHAPER_LUT_INDEX_MASK                                         0x000000FFL
//VPMPCC_MCM_SHAPER_LUT_DATA
#define VPMPCC_MCM_SHAPER_LUT_DATA__VPMPCC_MCM_SHAPER_LUT_DATA__SHIFT                                         0x0
#define VPMPCC_MCM_SHAPER_LUT_DATA__VPMPCC_MCM_SHAPER_LUT_DATA_MASK                                           0x00FFFFFFL
//VPMPCC_MCM_SHAPER_LUT_WRITE_EN_MASK
#define VPMPCC_MCM_SHAPER_LUT_WRITE_EN_MASK__VPMPCC_MCM_SHAPER_LUT_WRITE_EN_MASK__SHIFT                       0x0
#define VPMPCC_MCM_SHAPER_LUT_WRITE_EN_MASK__VPMPCC_MCM_SHAPER_LUT_WRITE_SEL__SHIFT                           0x4
#define VPMPCC_MCM_SHAPER_LUT_WRITE_EN_MASK__VPMPCC_MCM_SHAPER_LUT_WRITE_EN_MASK_MASK                         0x00000007L
#define VPMPCC_MCM_SHAPER_LUT_WRITE_EN_MASK__VPMPCC_MCM_SHAPER_LUT_WRITE_SEL_MASK                             0x00000010L
//VPMPCC_MCM_SHAPER_RAMA_START_CNTL_B
#define VPMPCC_MCM_SHAPER_RAMA_START_CNTL_B__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION_START_B__SHIFT                 0x0
#define VPMPCC_MCM_SHAPER_RAMA_START_CNTL_B__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION_START_SEGMENT_B__SHIFT         0x14
#define VPMPCC_MCM_SHAPER_RAMA_START_CNTL_B__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION_START_B_MASK                   0x0003FFFFL
#define VPMPCC_MCM_SHAPER_RAMA_START_CNTL_B__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION_START_SEGMENT_B_MASK           0x07F00000L
//VPMPCC_MCM_SHAPER_RAMA_START_CNTL_G
#define VPMPCC_MCM_SHAPER_RAMA_START_CNTL_G__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION_START_G__SHIFT                 0x0
#define VPMPCC_MCM_SHAPER_RAMA_START_CNTL_G__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION_START_SEGMENT_G__SHIFT         0x14
#define VPMPCC_MCM_SHAPER_RAMA_START_CNTL_G__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION_START_G_MASK                   0x0003FFFFL
#define VPMPCC_MCM_SHAPER_RAMA_START_CNTL_G__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION_START_SEGMENT_G_MASK           0x07F00000L
//VPMPCC_MCM_SHAPER_RAMA_START_CNTL_R
#define VPMPCC_MCM_SHAPER_RAMA_START_CNTL_R__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION_START_R__SHIFT                 0x0
#define VPMPCC_MCM_SHAPER_RAMA_START_CNTL_R__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION_START_SEGMENT_R__SHIFT         0x14
#define VPMPCC_MCM_SHAPER_RAMA_START_CNTL_R__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION_START_R_MASK                   0x0003FFFFL
#define VPMPCC_MCM_SHAPER_RAMA_START_CNTL_R__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION_START_SEGMENT_R_MASK           0x07F00000L
//VPMPCC_MCM_SHAPER_RAMA_END_CNTL_B
#define VPMPCC_MCM_SHAPER_RAMA_END_CNTL_B__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION_END_B__SHIFT                     0x0
#define VPMPCC_MCM_SHAPER_RAMA_END_CNTL_B__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION_END_BASE_B__SHIFT                0x10
#define VPMPCC_MCM_SHAPER_RAMA_END_CNTL_B__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION_END_B_MASK                       0x0000FFFFL
#define VPMPCC_MCM_SHAPER_RAMA_END_CNTL_B__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION_END_BASE_B_MASK                  0x3FFF0000L
//VPMPCC_MCM_SHAPER_RAMA_END_CNTL_G
#define VPMPCC_MCM_SHAPER_RAMA_END_CNTL_G__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION_END_G__SHIFT                     0x0
#define VPMPCC_MCM_SHAPER_RAMA_END_CNTL_G__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION_END_BASE_G__SHIFT                0x10
#define VPMPCC_MCM_SHAPER_RAMA_END_CNTL_G__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION_END_G_MASK                       0x0000FFFFL
#define VPMPCC_MCM_SHAPER_RAMA_END_CNTL_G__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION_END_BASE_G_MASK                  0x3FFF0000L
//VPMPCC_MCM_SHAPER_RAMA_END_CNTL_R
#define VPMPCC_MCM_SHAPER_RAMA_END_CNTL_R__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION_END_R__SHIFT                     0x0
#define VPMPCC_MCM_SHAPER_RAMA_END_CNTL_R__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION_END_BASE_R__SHIFT                0x10
#define VPMPCC_MCM_SHAPER_RAMA_END_CNTL_R__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION_END_R_MASK                       0x0000FFFFL
#define VPMPCC_MCM_SHAPER_RAMA_END_CNTL_R__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION_END_BASE_R_MASK                  0x3FFF0000L
//VPMPCC_MCM_SHAPER_RAMA_REGION_0_1
#define VPMPCC_MCM_SHAPER_RAMA_REGION_0_1__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET__SHIFT               0x0
#define VPMPCC_MCM_SHAPER_RAMA_REGION_0_1__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS__SHIFT             0xc
#define VPMPCC_MCM_SHAPER_RAMA_REGION_0_1__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET__SHIFT               0x10
#define VPMPCC_MCM_SHAPER_RAMA_REGION_0_1__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS__SHIFT             0x1c
#define VPMPCC_MCM_SHAPER_RAMA_REGION_0_1__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET_MASK                 0x000001FFL
#define VPMPCC_MCM_SHAPER_RAMA_REGION_0_1__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS_MASK               0x00007000L
#define VPMPCC_MCM_SHAPER_RAMA_REGION_0_1__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET_MASK                 0x01FF0000L
#define VPMPCC_MCM_SHAPER_RAMA_REGION_0_1__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS_MASK               0x70000000L
//VPMPCC_MCM_SHAPER_RAMA_REGION_2_3
#define VPMPCC_MCM_SHAPER_RAMA_REGION_2_3__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION2_LUT_OFFSET__SHIFT               0x0
#define VPMPCC_MCM_SHAPER_RAMA_REGION_2_3__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION2_NUM_SEGMENTS__SHIFT             0xc
#define VPMPCC_MCM_SHAPER_RAMA_REGION_2_3__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION3_LUT_OFFSET__SHIFT               0x10
#define VPMPCC_MCM_SHAPER_RAMA_REGION_2_3__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION3_NUM_SEGMENTS__SHIFT             0x1c
#define VPMPCC_MCM_SHAPER_RAMA_REGION_2_3__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION2_LUT_OFFSET_MASK                 0x000001FFL
#define VPMPCC_MCM_SHAPER_RAMA_REGION_2_3__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION2_NUM_SEGMENTS_MASK               0x00007000L
#define VPMPCC_MCM_SHAPER_RAMA_REGION_2_3__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION3_LUT_OFFSET_MASK                 0x01FF0000L
#define VPMPCC_MCM_SHAPER_RAMA_REGION_2_3__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION3_NUM_SEGMENTS_MASK               0x70000000L
//VPMPCC_MCM_SHAPER_RAMA_REGION_4_5
#define VPMPCC_MCM_SHAPER_RAMA_REGION_4_5__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION4_LUT_OFFSET__SHIFT               0x0
#define VPMPCC_MCM_SHAPER_RAMA_REGION_4_5__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION4_NUM_SEGMENTS__SHIFT             0xc
#define VPMPCC_MCM_SHAPER_RAMA_REGION_4_5__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION5_LUT_OFFSET__SHIFT               0x10
#define VPMPCC_MCM_SHAPER_RAMA_REGION_4_5__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION5_NUM_SEGMENTS__SHIFT             0x1c
#define VPMPCC_MCM_SHAPER_RAMA_REGION_4_5__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION4_LUT_OFFSET_MASK                 0x000001FFL
#define VPMPCC_MCM_SHAPER_RAMA_REGION_4_5__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION4_NUM_SEGMENTS_MASK               0x00007000L
#define VPMPCC_MCM_SHAPER_RAMA_REGION_4_5__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION5_LUT_OFFSET_MASK                 0x01FF0000L
#define VPMPCC_MCM_SHAPER_RAMA_REGION_4_5__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION5_NUM_SEGMENTS_MASK               0x70000000L
//VPMPCC_MCM_SHAPER_RAMA_REGION_6_7
#define VPMPCC_MCM_SHAPER_RAMA_REGION_6_7__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION6_LUT_OFFSET__SHIFT               0x0
#define VPMPCC_MCM_SHAPER_RAMA_REGION_6_7__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION6_NUM_SEGMENTS__SHIFT             0xc
#define VPMPCC_MCM_SHAPER_RAMA_REGION_6_7__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION7_LUT_OFFSET__SHIFT               0x10
#define VPMPCC_MCM_SHAPER_RAMA_REGION_6_7__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION7_NUM_SEGMENTS__SHIFT             0x1c
#define VPMPCC_MCM_SHAPER_RAMA_REGION_6_7__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION6_LUT_OFFSET_MASK                 0x000001FFL
#define VPMPCC_MCM_SHAPER_RAMA_REGION_6_7__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION6_NUM_SEGMENTS_MASK               0x00007000L
#define VPMPCC_MCM_SHAPER_RAMA_REGION_6_7__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION7_LUT_OFFSET_MASK                 0x01FF0000L
#define VPMPCC_MCM_SHAPER_RAMA_REGION_6_7__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION7_NUM_SEGMENTS_MASK               0x70000000L
//VPMPCC_MCM_SHAPER_RAMA_REGION_8_9
#define VPMPCC_MCM_SHAPER_RAMA_REGION_8_9__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION8_LUT_OFFSET__SHIFT               0x0
#define VPMPCC_MCM_SHAPER_RAMA_REGION_8_9__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION8_NUM_SEGMENTS__SHIFT             0xc
#define VPMPCC_MCM_SHAPER_RAMA_REGION_8_9__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION9_LUT_OFFSET__SHIFT               0x10
#define VPMPCC_MCM_SHAPER_RAMA_REGION_8_9__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION9_NUM_SEGMENTS__SHIFT             0x1c
#define VPMPCC_MCM_SHAPER_RAMA_REGION_8_9__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION8_LUT_OFFSET_MASK                 0x000001FFL
#define VPMPCC_MCM_SHAPER_RAMA_REGION_8_9__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION8_NUM_SEGMENTS_MASK               0x00007000L
#define VPMPCC_MCM_SHAPER_RAMA_REGION_8_9__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION9_LUT_OFFSET_MASK                 0x01FF0000L
#define VPMPCC_MCM_SHAPER_RAMA_REGION_8_9__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION9_NUM_SEGMENTS_MASK               0x70000000L
//VPMPCC_MCM_SHAPER_RAMA_REGION_10_11
#define VPMPCC_MCM_SHAPER_RAMA_REGION_10_11__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION10_LUT_OFFSET__SHIFT            0x0
#define VPMPCC_MCM_SHAPER_RAMA_REGION_10_11__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION10_NUM_SEGMENTS__SHIFT          0xc
#define VPMPCC_MCM_SHAPER_RAMA_REGION_10_11__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION11_LUT_OFFSET__SHIFT            0x10
#define VPMPCC_MCM_SHAPER_RAMA_REGION_10_11__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION11_NUM_SEGMENTS__SHIFT          0x1c
#define VPMPCC_MCM_SHAPER_RAMA_REGION_10_11__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION10_LUT_OFFSET_MASK              0x000001FFL
#define VPMPCC_MCM_SHAPER_RAMA_REGION_10_11__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION10_NUM_SEGMENTS_MASK            0x00007000L
#define VPMPCC_MCM_SHAPER_RAMA_REGION_10_11__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION11_LUT_OFFSET_MASK              0x01FF0000L
#define VPMPCC_MCM_SHAPER_RAMA_REGION_10_11__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION11_NUM_SEGMENTS_MASK            0x70000000L
//VPMPCC_MCM_SHAPER_RAMA_REGION_12_13
#define VPMPCC_MCM_SHAPER_RAMA_REGION_12_13__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION12_LUT_OFFSET__SHIFT            0x0
#define VPMPCC_MCM_SHAPER_RAMA_REGION_12_13__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION12_NUM_SEGMENTS__SHIFT          0xc
#define VPMPCC_MCM_SHAPER_RAMA_REGION_12_13__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION13_LUT_OFFSET__SHIFT            0x10
#define VPMPCC_MCM_SHAPER_RAMA_REGION_12_13__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION13_NUM_SEGMENTS__SHIFT          0x1c
#define VPMPCC_MCM_SHAPER_RAMA_REGION_12_13__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION12_LUT_OFFSET_MASK              0x000001FFL
#define VPMPCC_MCM_SHAPER_RAMA_REGION_12_13__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION12_NUM_SEGMENTS_MASK            0x00007000L
#define VPMPCC_MCM_SHAPER_RAMA_REGION_12_13__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION13_LUT_OFFSET_MASK              0x01FF0000L
#define VPMPCC_MCM_SHAPER_RAMA_REGION_12_13__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION13_NUM_SEGMENTS_MASK            0x70000000L
//VPMPCC_MCM_SHAPER_RAMA_REGION_14_15
#define VPMPCC_MCM_SHAPER_RAMA_REGION_14_15__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION14_LUT_OFFSET__SHIFT            0x0
#define VPMPCC_MCM_SHAPER_RAMA_REGION_14_15__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION14_NUM_SEGMENTS__SHIFT          0xc
#define VPMPCC_MCM_SHAPER_RAMA_REGION_14_15__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION15_LUT_OFFSET__SHIFT            0x10
#define VPMPCC_MCM_SHAPER_RAMA_REGION_14_15__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION15_NUM_SEGMENTS__SHIFT          0x1c
#define VPMPCC_MCM_SHAPER_RAMA_REGION_14_15__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION14_LUT_OFFSET_MASK              0x000001FFL
#define VPMPCC_MCM_SHAPER_RAMA_REGION_14_15__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION14_NUM_SEGMENTS_MASK            0x00007000L
#define VPMPCC_MCM_SHAPER_RAMA_REGION_14_15__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION15_LUT_OFFSET_MASK              0x01FF0000L
#define VPMPCC_MCM_SHAPER_RAMA_REGION_14_15__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION15_NUM_SEGMENTS_MASK            0x70000000L
//VPMPCC_MCM_SHAPER_RAMA_REGION_16_17
#define VPMPCC_MCM_SHAPER_RAMA_REGION_16_17__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION16_LUT_OFFSET__SHIFT            0x0
#define VPMPCC_MCM_SHAPER_RAMA_REGION_16_17__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION16_NUM_SEGMENTS__SHIFT          0xc
#define VPMPCC_MCM_SHAPER_RAMA_REGION_16_17__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION17_LUT_OFFSET__SHIFT            0x10
#define VPMPCC_MCM_SHAPER_RAMA_REGION_16_17__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION17_NUM_SEGMENTS__SHIFT          0x1c
#define VPMPCC_MCM_SHAPER_RAMA_REGION_16_17__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION16_LUT_OFFSET_MASK              0x000001FFL
#define VPMPCC_MCM_SHAPER_RAMA_REGION_16_17__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION16_NUM_SEGMENTS_MASK            0x00007000L
#define VPMPCC_MCM_SHAPER_RAMA_REGION_16_17__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION17_LUT_OFFSET_MASK              0x01FF0000L
#define VPMPCC_MCM_SHAPER_RAMA_REGION_16_17__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION17_NUM_SEGMENTS_MASK            0x70000000L
//VPMPCC_MCM_SHAPER_RAMA_REGION_18_19
#define VPMPCC_MCM_SHAPER_RAMA_REGION_18_19__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION18_LUT_OFFSET__SHIFT            0x0
#define VPMPCC_MCM_SHAPER_RAMA_REGION_18_19__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION18_NUM_SEGMENTS__SHIFT          0xc
#define VPMPCC_MCM_SHAPER_RAMA_REGION_18_19__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION19_LUT_OFFSET__SHIFT            0x10
#define VPMPCC_MCM_SHAPER_RAMA_REGION_18_19__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION19_NUM_SEGMENTS__SHIFT          0x1c
#define VPMPCC_MCM_SHAPER_RAMA_REGION_18_19__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION18_LUT_OFFSET_MASK              0x000001FFL
#define VPMPCC_MCM_SHAPER_RAMA_REGION_18_19__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION18_NUM_SEGMENTS_MASK            0x00007000L
#define VPMPCC_MCM_SHAPER_RAMA_REGION_18_19__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION19_LUT_OFFSET_MASK              0x01FF0000L
#define VPMPCC_MCM_SHAPER_RAMA_REGION_18_19__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION19_NUM_SEGMENTS_MASK            0x70000000L
//VPMPCC_MCM_SHAPER_RAMA_REGION_20_21
#define VPMPCC_MCM_SHAPER_RAMA_REGION_20_21__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION20_LUT_OFFSET__SHIFT            0x0
#define VPMPCC_MCM_SHAPER_RAMA_REGION_20_21__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION20_NUM_SEGMENTS__SHIFT          0xc
#define VPMPCC_MCM_SHAPER_RAMA_REGION_20_21__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION21_LUT_OFFSET__SHIFT            0x10
#define VPMPCC_MCM_SHAPER_RAMA_REGION_20_21__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION21_NUM_SEGMENTS__SHIFT          0x1c
#define VPMPCC_MCM_SHAPER_RAMA_REGION_20_21__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION20_LUT_OFFSET_MASK              0x000001FFL
#define VPMPCC_MCM_SHAPER_RAMA_REGION_20_21__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION20_NUM_SEGMENTS_MASK            0x00007000L
#define VPMPCC_MCM_SHAPER_RAMA_REGION_20_21__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION21_LUT_OFFSET_MASK              0x01FF0000L
#define VPMPCC_MCM_SHAPER_RAMA_REGION_20_21__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION21_NUM_SEGMENTS_MASK            0x70000000L
//VPMPCC_MCM_SHAPER_RAMA_REGION_22_23
#define VPMPCC_MCM_SHAPER_RAMA_REGION_22_23__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION22_LUT_OFFSET__SHIFT            0x0
#define VPMPCC_MCM_SHAPER_RAMA_REGION_22_23__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION22_NUM_SEGMENTS__SHIFT          0xc
#define VPMPCC_MCM_SHAPER_RAMA_REGION_22_23__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION23_LUT_OFFSET__SHIFT            0x10
#define VPMPCC_MCM_SHAPER_RAMA_REGION_22_23__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION23_NUM_SEGMENTS__SHIFT          0x1c
#define VPMPCC_MCM_SHAPER_RAMA_REGION_22_23__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION22_LUT_OFFSET_MASK              0x000001FFL
#define VPMPCC_MCM_SHAPER_RAMA_REGION_22_23__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION22_NUM_SEGMENTS_MASK            0x00007000L
#define VPMPCC_MCM_SHAPER_RAMA_REGION_22_23__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION23_LUT_OFFSET_MASK              0x01FF0000L
#define VPMPCC_MCM_SHAPER_RAMA_REGION_22_23__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION23_NUM_SEGMENTS_MASK            0x70000000L
//VPMPCC_MCM_SHAPER_RAMA_REGION_24_25
#define VPMPCC_MCM_SHAPER_RAMA_REGION_24_25__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION24_LUT_OFFSET__SHIFT            0x0
#define VPMPCC_MCM_SHAPER_RAMA_REGION_24_25__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION24_NUM_SEGMENTS__SHIFT          0xc
#define VPMPCC_MCM_SHAPER_RAMA_REGION_24_25__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION25_LUT_OFFSET__SHIFT            0x10
#define VPMPCC_MCM_SHAPER_RAMA_REGION_24_25__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION25_NUM_SEGMENTS__SHIFT          0x1c
#define VPMPCC_MCM_SHAPER_RAMA_REGION_24_25__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION24_LUT_OFFSET_MASK              0x000001FFL
#define VPMPCC_MCM_SHAPER_RAMA_REGION_24_25__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION24_NUM_SEGMENTS_MASK            0x00007000L
#define VPMPCC_MCM_SHAPER_RAMA_REGION_24_25__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION25_LUT_OFFSET_MASK              0x01FF0000L
#define VPMPCC_MCM_SHAPER_RAMA_REGION_24_25__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION25_NUM_SEGMENTS_MASK            0x70000000L
//VPMPCC_MCM_SHAPER_RAMA_REGION_26_27
#define VPMPCC_MCM_SHAPER_RAMA_REGION_26_27__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION26_LUT_OFFSET__SHIFT            0x0
#define VPMPCC_MCM_SHAPER_RAMA_REGION_26_27__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION26_NUM_SEGMENTS__SHIFT          0xc
#define VPMPCC_MCM_SHAPER_RAMA_REGION_26_27__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION27_LUT_OFFSET__SHIFT            0x10
#define VPMPCC_MCM_SHAPER_RAMA_REGION_26_27__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION27_NUM_SEGMENTS__SHIFT          0x1c
#define VPMPCC_MCM_SHAPER_RAMA_REGION_26_27__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION26_LUT_OFFSET_MASK              0x000001FFL
#define VPMPCC_MCM_SHAPER_RAMA_REGION_26_27__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION26_NUM_SEGMENTS_MASK            0x00007000L
#define VPMPCC_MCM_SHAPER_RAMA_REGION_26_27__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION27_LUT_OFFSET_MASK              0x01FF0000L
#define VPMPCC_MCM_SHAPER_RAMA_REGION_26_27__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION27_NUM_SEGMENTS_MASK            0x70000000L
//VPMPCC_MCM_SHAPER_RAMA_REGION_28_29
#define VPMPCC_MCM_SHAPER_RAMA_REGION_28_29__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION28_LUT_OFFSET__SHIFT            0x0
#define VPMPCC_MCM_SHAPER_RAMA_REGION_28_29__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION28_NUM_SEGMENTS__SHIFT          0xc
#define VPMPCC_MCM_SHAPER_RAMA_REGION_28_29__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION29_LUT_OFFSET__SHIFT            0x10
#define VPMPCC_MCM_SHAPER_RAMA_REGION_28_29__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION29_NUM_SEGMENTS__SHIFT          0x1c
#define VPMPCC_MCM_SHAPER_RAMA_REGION_28_29__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION28_LUT_OFFSET_MASK              0x000001FFL
#define VPMPCC_MCM_SHAPER_RAMA_REGION_28_29__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION28_NUM_SEGMENTS_MASK            0x00007000L
#define VPMPCC_MCM_SHAPER_RAMA_REGION_28_29__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION29_LUT_OFFSET_MASK              0x01FF0000L
#define VPMPCC_MCM_SHAPER_RAMA_REGION_28_29__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION29_NUM_SEGMENTS_MASK            0x70000000L
//VPMPCC_MCM_SHAPER_RAMA_REGION_30_31
#define VPMPCC_MCM_SHAPER_RAMA_REGION_30_31__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION30_LUT_OFFSET__SHIFT            0x0
#define VPMPCC_MCM_SHAPER_RAMA_REGION_30_31__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION30_NUM_SEGMENTS__SHIFT          0xc
#define VPMPCC_MCM_SHAPER_RAMA_REGION_30_31__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION31_LUT_OFFSET__SHIFT            0x10
#define VPMPCC_MCM_SHAPER_RAMA_REGION_30_31__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION31_NUM_SEGMENTS__SHIFT          0x1c
#define VPMPCC_MCM_SHAPER_RAMA_REGION_30_31__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION30_LUT_OFFSET_MASK              0x000001FFL
#define VPMPCC_MCM_SHAPER_RAMA_REGION_30_31__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION30_NUM_SEGMENTS_MASK            0x00007000L
#define VPMPCC_MCM_SHAPER_RAMA_REGION_30_31__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION31_LUT_OFFSET_MASK              0x01FF0000L
#define VPMPCC_MCM_SHAPER_RAMA_REGION_30_31__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION31_NUM_SEGMENTS_MASK            0x70000000L
//VPMPCC_MCM_SHAPER_RAMA_REGION_32_33
#define VPMPCC_MCM_SHAPER_RAMA_REGION_32_33__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION32_LUT_OFFSET__SHIFT            0x0
#define VPMPCC_MCM_SHAPER_RAMA_REGION_32_33__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION32_NUM_SEGMENTS__SHIFT          0xc
#define VPMPCC_MCM_SHAPER_RAMA_REGION_32_33__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION33_LUT_OFFSET__SHIFT            0x10
#define VPMPCC_MCM_SHAPER_RAMA_REGION_32_33__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION33_NUM_SEGMENTS__SHIFT          0x1c
#define VPMPCC_MCM_SHAPER_RAMA_REGION_32_33__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION32_LUT_OFFSET_MASK              0x000001FFL
#define VPMPCC_MCM_SHAPER_RAMA_REGION_32_33__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION32_NUM_SEGMENTS_MASK            0x00007000L
#define VPMPCC_MCM_SHAPER_RAMA_REGION_32_33__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION33_LUT_OFFSET_MASK              0x01FF0000L
#define VPMPCC_MCM_SHAPER_RAMA_REGION_32_33__VPMPCC_MCM_SHAPER_RAMA_EXP_REGION33_NUM_SEGMENTS_MASK            0x70000000L
//VPMPCC_MCM_3DLUT_MODE
#define VPMPCC_MCM_3DLUT_MODE__VPMPCC_MCM_3DLUT_MODE__SHIFT                                                   0x0
#define VPMPCC_MCM_3DLUT_MODE__VPMPCC_MCM_3DLUT_SIZE__SHIFT                                                   0x4
#define VPMPCC_MCM_3DLUT_MODE__VPMPCC_MCM_3DLUT_MODE_CURRENT__SHIFT                                           0x8
#define VPMPCC_MCM_3DLUT_MODE__VPMPCC_MCM_3DLUT_SELECT_CURRENT__SHIFT                                         0xa
#define VPMPCC_MCM_3DLUT_MODE__VPMPCC_MCM_3DLUT_MODE_MASK                                                     0x00000003L
#define VPMPCC_MCM_3DLUT_MODE__VPMPCC_MCM_3DLUT_SIZE_MASK                                                     0x00000010L
#define VPMPCC_MCM_3DLUT_MODE__VPMPCC_MCM_3DLUT_MODE_CURRENT_MASK                                             0x00000300L
#define VPMPCC_MCM_3DLUT_MODE__VPMPCC_MCM_3DLUT_SELECT_CURRENT_MASK                                           0x00000400L
//VPMPCC_MCM_3DLUT_INDEX
#define VPMPCC_MCM_3DLUT_INDEX__VPMPCC_MCM_3DLUT_INDEX__SHIFT                                                 0x0
#define VPMPCC_MCM_3DLUT_INDEX__VPMPCC_MCM_3DLUT_INDEX_MASK                                                   0x000007FFL
//VPMPCC_MCM_3DLUT_DATA
#define VPMPCC_MCM_3DLUT_DATA__VPMPCC_MCM_3DLUT_DATA0__SHIFT                                                  0x0
#define VPMPCC_MCM_3DLUT_DATA__VPMPCC_MCM_3DLUT_DATA1__SHIFT                                                  0x10
#define VPMPCC_MCM_3DLUT_DATA__VPMPCC_MCM_3DLUT_DATA0_MASK                                                    0x0000FFFFL
#define VPMPCC_MCM_3DLUT_DATA__VPMPCC_MCM_3DLUT_DATA1_MASK                                                    0xFFFF0000L
//VPMPCC_MCM_3DLUT_DATA_30BIT
#define VPMPCC_MCM_3DLUT_DATA_30BIT__VPMPCC_MCM_3DLUT_DATA_30BIT__SHIFT                                       0x2
#define VPMPCC_MCM_3DLUT_DATA_30BIT__VPMPCC_MCM_3DLUT_DATA_30BIT_MASK                                         0xFFFFFFFCL
//VPMPCC_MCM_3DLUT_READ_WRITE_CONTROL
#define VPMPCC_MCM_3DLUT_READ_WRITE_CONTROL__VPMPCC_MCM_3DLUT_WRITE_EN_MASK__SHIFT                            0x0
#define VPMPCC_MCM_3DLUT_READ_WRITE_CONTROL__VPMPCC_MCM_3DLUT_RAM_SEL__SHIFT                                  0x4
#define VPMPCC_MCM_3DLUT_READ_WRITE_CONTROL__VPMPCC_MCM_3DLUT_30BIT_EN__SHIFT                                 0x8
#define VPMPCC_MCM_3DLUT_READ_WRITE_CONTROL__VPMPCC_MCM_3DLUT_READ_SEL__SHIFT                                 0x10
#define VPMPCC_MCM_3DLUT_READ_WRITE_CONTROL__VPMPCC_MCM_3DLUT_WRITE_EN_MASK_MASK                              0x0000000FL
#define VPMPCC_MCM_3DLUT_READ_WRITE_CONTROL__VPMPCC_MCM_3DLUT_RAM_SEL_MASK                                    0x00000010L
#define VPMPCC_MCM_3DLUT_READ_WRITE_CONTROL__VPMPCC_MCM_3DLUT_30BIT_EN_MASK                                   0x00000100L
#define VPMPCC_MCM_3DLUT_READ_WRITE_CONTROL__VPMPCC_MCM_3DLUT_READ_SEL_MASK                                   0x00030000L
//VPMPCC_MCM_3DLUT_OUT_NORM_FACTOR
#define VPMPCC_MCM_3DLUT_OUT_NORM_FACTOR__VPMPCC_MCM_3DLUT_OUT_NORM_FACTOR__SHIFT                             0x0
#define VPMPCC_MCM_3DLUT_OUT_NORM_FACTOR__VPMPCC_MCM_3DLUT_OUT_NORM_FACTOR_MASK                               0x0000FFFFL
//VPMPCC_MCM_3DLUT_OUT_OFFSET_R
#define VPMPCC_MCM_3DLUT_OUT_OFFSET_R__VPMPCC_MCM_3DLUT_OUT_OFFSET_R__SHIFT                                   0x0
#define VPMPCC_MCM_3DLUT_OUT_OFFSET_R__VPMPCC_MCM_3DLUT_OUT_SCALE_R__SHIFT                                    0x10
#define VPMPCC_MCM_3DLUT_OUT_OFFSET_R__VPMPCC_MCM_3DLUT_OUT_OFFSET_R_MASK                                     0x0000FFFFL
#define VPMPCC_MCM_3DLUT_OUT_OFFSET_R__VPMPCC_MCM_3DLUT_OUT_SCALE_R_MASK                                      0xFFFF0000L
//VPMPCC_MCM_3DLUT_OUT_OFFSET_G
#define VPMPCC_MCM_3DLUT_OUT_OFFSET_G__VPMPCC_MCM_3DLUT_OUT_OFFSET_G__SHIFT                                   0x0
#define VPMPCC_MCM_3DLUT_OUT_OFFSET_G__VPMPCC_MCM_3DLUT_OUT_SCALE_G__SHIFT                                    0x10
#define VPMPCC_MCM_3DLUT_OUT_OFFSET_G__VPMPCC_MCM_3DLUT_OUT_OFFSET_G_MASK                                     0x0000FFFFL
#define VPMPCC_MCM_3DLUT_OUT_OFFSET_G__VPMPCC_MCM_3DLUT_OUT_SCALE_G_MASK                                      0xFFFF0000L
//VPMPCC_MCM_3DLUT_OUT_OFFSET_B
#define VPMPCC_MCM_3DLUT_OUT_OFFSET_B__VPMPCC_MCM_3DLUT_OUT_OFFSET_B__SHIFT                                   0x0
#define VPMPCC_MCM_3DLUT_OUT_OFFSET_B__VPMPCC_MCM_3DLUT_OUT_SCALE_B__SHIFT                                    0x10
#define VPMPCC_MCM_3DLUT_OUT_OFFSET_B__VPMPCC_MCM_3DLUT_OUT_OFFSET_B_MASK                                     0x0000FFFFL
#define VPMPCC_MCM_3DLUT_OUT_OFFSET_B__VPMPCC_MCM_3DLUT_OUT_SCALE_B_MASK                                      0xFFFF0000L
//VPMPCC_MCM_1DLUT_CONTROL
#define VPMPCC_MCM_1DLUT_CONTROL__VPMPCC_MCM_1DLUT_MODE__SHIFT                                                0x0
#define VPMPCC_MCM_1DLUT_CONTROL__VPMPCC_MCM_1DLUT_PWL_DISABLE__SHIFT                                         0x3
#define VPMPCC_MCM_1DLUT_CONTROL__VPMPCC_MCM_1DLUT_MODE_CURRENT__SHIFT                                        0x4
#define VPMPCC_MCM_1DLUT_CONTROL__VPMPCC_MCM_1DLUT_SELECT_CURRENT__SHIFT                                      0x6
#define VPMPCC_MCM_1DLUT_CONTROL__VPMPCC_MCM_1DLUT_MODE_MASK                                                  0x00000003L
#define VPMPCC_MCM_1DLUT_CONTROL__VPMPCC_MCM_1DLUT_PWL_DISABLE_MASK                                           0x00000008L
#define VPMPCC_MCM_1DLUT_CONTROL__VPMPCC_MCM_1DLUT_MODE_CURRENT_MASK                                          0x00000030L
#define VPMPCC_MCM_1DLUT_CONTROL__VPMPCC_MCM_1DLUT_SELECT_CURRENT_MASK                                        0x00000040L
//VPMPCC_MCM_1DLUT_LUT_INDEX
#define VPMPCC_MCM_1DLUT_LUT_INDEX__VPMPCC_MCM_1DLUT_LUT_INDEX__SHIFT                                         0x0
#define VPMPCC_MCM_1DLUT_LUT_INDEX__VPMPCC_MCM_1DLUT_LUT_INDEX_MASK                                           0x000001FFL
//VPMPCC_MCM_1DLUT_LUT_DATA
#define VPMPCC_MCM_1DLUT_LUT_DATA__VPMPCC_MCM_1DLUT_LUT_DATA__SHIFT                                           0x0
#define VPMPCC_MCM_1DLUT_LUT_DATA__VPMPCC_MCM_1DLUT_LUT_DATA_MASK                                             0x0003FFFFL
//VPMPCC_MCM_1DLUT_LUT_CONTROL
#define VPMPCC_MCM_1DLUT_LUT_CONTROL__VPMPCC_MCM_1DLUT_LUT_WRITE_COLOR_MASK__SHIFT                            0x0
#define VPMPCC_MCM_1DLUT_LUT_CONTROL__VPMPCC_MCM_1DLUT_LUT_READ_COLOR_SEL__SHIFT                              0x3
#define VPMPCC_MCM_1DLUT_LUT_CONTROL__VPMPCC_MCM_1DLUT_LUT_READ_DBG__SHIFT                                    0x5
#define VPMPCC_MCM_1DLUT_LUT_CONTROL__VPMPCC_MCM_1DLUT_LUT_HOST_SEL__SHIFT                                    0x6
#define VPMPCC_MCM_1DLUT_LUT_CONTROL__VPMPCC_MCM_1DLUT_LUT_CONFIG_MODE__SHIFT                                 0x7
#define VPMPCC_MCM_1DLUT_LUT_CONTROL__VPMPCC_MCM_1DLUT_LUT_WRITE_COLOR_MASK_MASK                              0x00000007L
#define VPMPCC_MCM_1DLUT_LUT_CONTROL__VPMPCC_MCM_1DLUT_LUT_READ_COLOR_SEL_MASK                                0x00000018L
#define VPMPCC_MCM_1DLUT_LUT_CONTROL__VPMPCC_MCM_1DLUT_LUT_READ_DBG_MASK                                      0x00000020L
#define VPMPCC_MCM_1DLUT_LUT_CONTROL__VPMPCC_MCM_1DLUT_LUT_HOST_SEL_MASK                                      0x00000040L
#define VPMPCC_MCM_1DLUT_LUT_CONTROL__VPMPCC_MCM_1DLUT_LUT_CONFIG_MODE_MASK                                   0x00000080L
//VPMPCC_MCM_1DLUT_RAMA_START_CNTL_B
#define VPMPCC_MCM_1DLUT_RAMA_START_CNTL_B__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION_START_B__SHIFT                   0x0
#define VPMPCC_MCM_1DLUT_RAMA_START_CNTL_B__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION_START_SEGMENT_B__SHIFT           0x14
#define VPMPCC_MCM_1DLUT_RAMA_START_CNTL_B__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION_START_B_MASK                     0x0003FFFFL
#define VPMPCC_MCM_1DLUT_RAMA_START_CNTL_B__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION_START_SEGMENT_B_MASK             0x07F00000L
//VPMPCC_MCM_1DLUT_RAMA_START_CNTL_G
#define VPMPCC_MCM_1DLUT_RAMA_START_CNTL_G__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION_START_G__SHIFT                   0x0
#define VPMPCC_MCM_1DLUT_RAMA_START_CNTL_G__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION_START_SEGMENT_G__SHIFT           0x14
#define VPMPCC_MCM_1DLUT_RAMA_START_CNTL_G__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION_START_G_MASK                     0x0003FFFFL
#define VPMPCC_MCM_1DLUT_RAMA_START_CNTL_G__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION_START_SEGMENT_G_MASK             0x07F00000L
//VPMPCC_MCM_1DLUT_RAMA_START_CNTL_R
#define VPMPCC_MCM_1DLUT_RAMA_START_CNTL_R__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION_START_R__SHIFT                   0x0
#define VPMPCC_MCM_1DLUT_RAMA_START_CNTL_R__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION_START_SEGMENT_R__SHIFT           0x14
#define VPMPCC_MCM_1DLUT_RAMA_START_CNTL_R__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION_START_R_MASK                     0x0003FFFFL
#define VPMPCC_MCM_1DLUT_RAMA_START_CNTL_R__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION_START_SEGMENT_R_MASK             0x07F00000L
//VPMPCC_MCM_1DLUT_RAMA_START_SLOPE_CNTL_B
#define VPMPCC_MCM_1DLUT_RAMA_START_SLOPE_CNTL_B__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION_START_SLOPE_B__SHIFT       0x0
#define VPMPCC_MCM_1DLUT_RAMA_START_SLOPE_CNTL_B__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION_START_SLOPE_B_MASK         0x0003FFFFL
//VPMPCC_MCM_1DLUT_RAMA_START_SLOPE_CNTL_G
#define VPMPCC_MCM_1DLUT_RAMA_START_SLOPE_CNTL_G__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION_START_SLOPE_G__SHIFT       0x0
#define VPMPCC_MCM_1DLUT_RAMA_START_SLOPE_CNTL_G__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION_START_SLOPE_G_MASK         0x0003FFFFL
//VPMPCC_MCM_1DLUT_RAMA_START_SLOPE_CNTL_R
#define VPMPCC_MCM_1DLUT_RAMA_START_SLOPE_CNTL_R__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION_START_SLOPE_R__SHIFT       0x0
#define VPMPCC_MCM_1DLUT_RAMA_START_SLOPE_CNTL_R__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION_START_SLOPE_R_MASK         0x0003FFFFL
//VPMPCC_MCM_1DLUT_RAMA_START_BASE_CNTL_B
#define VPMPCC_MCM_1DLUT_RAMA_START_BASE_CNTL_B__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION_START_BASE_B__SHIFT         0x0
#define VPMPCC_MCM_1DLUT_RAMA_START_BASE_CNTL_B__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION_START_BASE_B_MASK           0x0003FFFFL
//VPMPCC_MCM_1DLUT_RAMA_START_BASE_CNTL_G
#define VPMPCC_MCM_1DLUT_RAMA_START_BASE_CNTL_G__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION_START_BASE_G__SHIFT         0x0
#define VPMPCC_MCM_1DLUT_RAMA_START_BASE_CNTL_G__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION_START_BASE_G_MASK           0x0003FFFFL
//VPMPCC_MCM_1DLUT_RAMA_START_BASE_CNTL_R
#define VPMPCC_MCM_1DLUT_RAMA_START_BASE_CNTL_R__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION_START_BASE_R__SHIFT         0x0
#define VPMPCC_MCM_1DLUT_RAMA_START_BASE_CNTL_R__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION_START_BASE_R_MASK           0x0003FFFFL
//VPMPCC_MCM_1DLUT_RAMA_END_CNTL1_B
#define VPMPCC_MCM_1DLUT_RAMA_END_CNTL1_B__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION_END_BASE_B__SHIFT                 0x0
#define VPMPCC_MCM_1DLUT_RAMA_END_CNTL1_B__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION_END_BASE_B_MASK                   0x0003FFFFL
//VPMPCC_MCM_1DLUT_RAMA_END_CNTL2_B
#define VPMPCC_MCM_1DLUT_RAMA_END_CNTL2_B__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION_END_B__SHIFT                      0x0
#define VPMPCC_MCM_1DLUT_RAMA_END_CNTL2_B__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION_END_SLOPE_B__SHIFT                0x10
#define VPMPCC_MCM_1DLUT_RAMA_END_CNTL2_B__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION_END_B_MASK                        0x0000FFFFL
#define VPMPCC_MCM_1DLUT_RAMA_END_CNTL2_B__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION_END_SLOPE_B_MASK                  0xFFFF0000L
//VPMPCC_MCM_1DLUT_RAMA_END_CNTL1_G
#define VPMPCC_MCM_1DLUT_RAMA_END_CNTL1_G__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION_END_BASE_G__SHIFT                 0x0
#define VPMPCC_MCM_1DLUT_RAMA_END_CNTL1_G__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION_END_BASE_G_MASK                   0x0003FFFFL
//VPMPCC_MCM_1DLUT_RAMA_END_CNTL2_G
#define VPMPCC_MCM_1DLUT_RAMA_END_CNTL2_G__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION_END_G__SHIFT                      0x0
#define VPMPCC_MCM_1DLUT_RAMA_END_CNTL2_G__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION_END_SLOPE_G__SHIFT                0x10
#define VPMPCC_MCM_1DLUT_RAMA_END_CNTL2_G__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION_END_G_MASK                        0x0000FFFFL
#define VPMPCC_MCM_1DLUT_RAMA_END_CNTL2_G__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION_END_SLOPE_G_MASK                  0xFFFF0000L
//VPMPCC_MCM_1DLUT_RAMA_END_CNTL1_R
#define VPMPCC_MCM_1DLUT_RAMA_END_CNTL1_R__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION_END_BASE_R__SHIFT                 0x0
#define VPMPCC_MCM_1DLUT_RAMA_END_CNTL1_R__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION_END_BASE_R_MASK                   0x0003FFFFL
//VPMPCC_MCM_1DLUT_RAMA_END_CNTL2_R
#define VPMPCC_MCM_1DLUT_RAMA_END_CNTL2_R__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION_END_R__SHIFT                      0x0
#define VPMPCC_MCM_1DLUT_RAMA_END_CNTL2_R__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION_END_SLOPE_R__SHIFT                0x10
#define VPMPCC_MCM_1DLUT_RAMA_END_CNTL2_R__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION_END_R_MASK                        0x0000FFFFL
#define VPMPCC_MCM_1DLUT_RAMA_END_CNTL2_R__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION_END_SLOPE_R_MASK                  0xFFFF0000L
//VPMPCC_MCM_1DLUT_RAMA_OFFSET_B
#define VPMPCC_MCM_1DLUT_RAMA_OFFSET_B__VPMPCC_MCM_1DLUT_RAMA_OFFSET_B__SHIFT                                 0x0
#define VPMPCC_MCM_1DLUT_RAMA_OFFSET_B__VPMPCC_MCM_1DLUT_RAMA_OFFSET_B_MASK                                   0x0007FFFFL
//VPMPCC_MCM_1DLUT_RAMA_OFFSET_G
#define VPMPCC_MCM_1DLUT_RAMA_OFFSET_G__VPMPCC_MCM_1DLUT_RAMA_OFFSET_G__SHIFT                                 0x0
#define VPMPCC_MCM_1DLUT_RAMA_OFFSET_G__VPMPCC_MCM_1DLUT_RAMA_OFFSET_G_MASK                                   0x0007FFFFL
//VPMPCC_MCM_1DLUT_RAMA_OFFSET_R
#define VPMPCC_MCM_1DLUT_RAMA_OFFSET_R__VPMPCC_MCM_1DLUT_RAMA_OFFSET_R__SHIFT                                 0x0
#define VPMPCC_MCM_1DLUT_RAMA_OFFSET_R__VPMPCC_MCM_1DLUT_RAMA_OFFSET_R_MASK                                   0x0007FFFFL
//VPMPCC_MCM_1DLUT_RAMA_REGION_0_1
#define VPMPCC_MCM_1DLUT_RAMA_REGION_0_1__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION0_LUT_OFFSET__SHIFT                 0x0
#define VPMPCC_MCM_1DLUT_RAMA_REGION_0_1__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION0_NUM_SEGMENTS__SHIFT               0xc
#define VPMPCC_MCM_1DLUT_RAMA_REGION_0_1__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION1_LUT_OFFSET__SHIFT                 0x10
#define VPMPCC_MCM_1DLUT_RAMA_REGION_0_1__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION1_NUM_SEGMENTS__SHIFT               0x1c
#define VPMPCC_MCM_1DLUT_RAMA_REGION_0_1__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION0_LUT_OFFSET_MASK                   0x000001FFL
#define VPMPCC_MCM_1DLUT_RAMA_REGION_0_1__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION0_NUM_SEGMENTS_MASK                 0x00007000L
#define VPMPCC_MCM_1DLUT_RAMA_REGION_0_1__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION1_LUT_OFFSET_MASK                   0x01FF0000L
#define VPMPCC_MCM_1DLUT_RAMA_REGION_0_1__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION1_NUM_SEGMENTS_MASK                 0x70000000L
//VPMPCC_MCM_1DLUT_RAMA_REGION_2_3
#define VPMPCC_MCM_1DLUT_RAMA_REGION_2_3__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION2_LUT_OFFSET__SHIFT                 0x0
#define VPMPCC_MCM_1DLUT_RAMA_REGION_2_3__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION2_NUM_SEGMENTS__SHIFT               0xc
#define VPMPCC_MCM_1DLUT_RAMA_REGION_2_3__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION3_LUT_OFFSET__SHIFT                 0x10
#define VPMPCC_MCM_1DLUT_RAMA_REGION_2_3__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION3_NUM_SEGMENTS__SHIFT               0x1c
#define VPMPCC_MCM_1DLUT_RAMA_REGION_2_3__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION2_LUT_OFFSET_MASK                   0x000001FFL
#define VPMPCC_MCM_1DLUT_RAMA_REGION_2_3__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION2_NUM_SEGMENTS_MASK                 0x00007000L
#define VPMPCC_MCM_1DLUT_RAMA_REGION_2_3__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION3_LUT_OFFSET_MASK                   0x01FF0000L
#define VPMPCC_MCM_1DLUT_RAMA_REGION_2_3__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION3_NUM_SEGMENTS_MASK                 0x70000000L
//VPMPCC_MCM_1DLUT_RAMA_REGION_4_5
#define VPMPCC_MCM_1DLUT_RAMA_REGION_4_5__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION4_LUT_OFFSET__SHIFT                 0x0
#define VPMPCC_MCM_1DLUT_RAMA_REGION_4_5__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION4_NUM_SEGMENTS__SHIFT               0xc
#define VPMPCC_MCM_1DLUT_RAMA_REGION_4_5__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION5_LUT_OFFSET__SHIFT                 0x10
#define VPMPCC_MCM_1DLUT_RAMA_REGION_4_5__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION5_NUM_SEGMENTS__SHIFT               0x1c
#define VPMPCC_MCM_1DLUT_RAMA_REGION_4_5__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION4_LUT_OFFSET_MASK                   0x000001FFL
#define VPMPCC_MCM_1DLUT_RAMA_REGION_4_5__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION4_NUM_SEGMENTS_MASK                 0x00007000L
#define VPMPCC_MCM_1DLUT_RAMA_REGION_4_5__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION5_LUT_OFFSET_MASK                   0x01FF0000L
#define VPMPCC_MCM_1DLUT_RAMA_REGION_4_5__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION5_NUM_SEGMENTS_MASK                 0x70000000L
//VPMPCC_MCM_1DLUT_RAMA_REGION_6_7
#define VPMPCC_MCM_1DLUT_RAMA_REGION_6_7__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION6_LUT_OFFSET__SHIFT                 0x0
#define VPMPCC_MCM_1DLUT_RAMA_REGION_6_7__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION6_NUM_SEGMENTS__SHIFT               0xc
#define VPMPCC_MCM_1DLUT_RAMA_REGION_6_7__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION7_LUT_OFFSET__SHIFT                 0x10
#define VPMPCC_MCM_1DLUT_RAMA_REGION_6_7__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION7_NUM_SEGMENTS__SHIFT               0x1c
#define VPMPCC_MCM_1DLUT_RAMA_REGION_6_7__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION6_LUT_OFFSET_MASK                   0x000001FFL
#define VPMPCC_MCM_1DLUT_RAMA_REGION_6_7__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION6_NUM_SEGMENTS_MASK                 0x00007000L
#define VPMPCC_MCM_1DLUT_RAMA_REGION_6_7__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION7_LUT_OFFSET_MASK                   0x01FF0000L
#define VPMPCC_MCM_1DLUT_RAMA_REGION_6_7__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION7_NUM_SEGMENTS_MASK                 0x70000000L
//VPMPCC_MCM_1DLUT_RAMA_REGION_8_9
#define VPMPCC_MCM_1DLUT_RAMA_REGION_8_9__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION8_LUT_OFFSET__SHIFT                 0x0
#define VPMPCC_MCM_1DLUT_RAMA_REGION_8_9__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION8_NUM_SEGMENTS__SHIFT               0xc
#define VPMPCC_MCM_1DLUT_RAMA_REGION_8_9__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION9_LUT_OFFSET__SHIFT                 0x10
#define VPMPCC_MCM_1DLUT_RAMA_REGION_8_9__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION9_NUM_SEGMENTS__SHIFT               0x1c
#define VPMPCC_MCM_1DLUT_RAMA_REGION_8_9__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION8_LUT_OFFSET_MASK                   0x000001FFL
#define VPMPCC_MCM_1DLUT_RAMA_REGION_8_9__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION8_NUM_SEGMENTS_MASK                 0x00007000L
#define VPMPCC_MCM_1DLUT_RAMA_REGION_8_9__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION9_LUT_OFFSET_MASK                   0x01FF0000L
#define VPMPCC_MCM_1DLUT_RAMA_REGION_8_9__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION9_NUM_SEGMENTS_MASK                 0x70000000L
//VPMPCC_MCM_1DLUT_RAMA_REGION_10_11
#define VPMPCC_MCM_1DLUT_RAMA_REGION_10_11__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION10_LUT_OFFSET__SHIFT              0x0
#define VPMPCC_MCM_1DLUT_RAMA_REGION_10_11__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION10_NUM_SEGMENTS__SHIFT            0xc
#define VPMPCC_MCM_1DLUT_RAMA_REGION_10_11__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION11_LUT_OFFSET__SHIFT              0x10
#define VPMPCC_MCM_1DLUT_RAMA_REGION_10_11__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION11_NUM_SEGMENTS__SHIFT            0x1c
#define VPMPCC_MCM_1DLUT_RAMA_REGION_10_11__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION10_LUT_OFFSET_MASK                0x000001FFL
#define VPMPCC_MCM_1DLUT_RAMA_REGION_10_11__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION10_NUM_SEGMENTS_MASK              0x00007000L
#define VPMPCC_MCM_1DLUT_RAMA_REGION_10_11__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION11_LUT_OFFSET_MASK                0x01FF0000L
#define VPMPCC_MCM_1DLUT_RAMA_REGION_10_11__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION11_NUM_SEGMENTS_MASK              0x70000000L
//VPMPCC_MCM_1DLUT_RAMA_REGION_12_13
#define VPMPCC_MCM_1DLUT_RAMA_REGION_12_13__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION12_LUT_OFFSET__SHIFT              0x0
#define VPMPCC_MCM_1DLUT_RAMA_REGION_12_13__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION12_NUM_SEGMENTS__SHIFT            0xc
#define VPMPCC_MCM_1DLUT_RAMA_REGION_12_13__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION13_LUT_OFFSET__SHIFT              0x10
#define VPMPCC_MCM_1DLUT_RAMA_REGION_12_13__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION13_NUM_SEGMENTS__SHIFT            0x1c
#define VPMPCC_MCM_1DLUT_RAMA_REGION_12_13__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION12_LUT_OFFSET_MASK                0x000001FFL
#define VPMPCC_MCM_1DLUT_RAMA_REGION_12_13__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION12_NUM_SEGMENTS_MASK              0x00007000L
#define VPMPCC_MCM_1DLUT_RAMA_REGION_12_13__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION13_LUT_OFFSET_MASK                0x01FF0000L
#define VPMPCC_MCM_1DLUT_RAMA_REGION_12_13__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION13_NUM_SEGMENTS_MASK              0x70000000L
//VPMPCC_MCM_1DLUT_RAMA_REGION_14_15
#define VPMPCC_MCM_1DLUT_RAMA_REGION_14_15__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION14_LUT_OFFSET__SHIFT              0x0
#define VPMPCC_MCM_1DLUT_RAMA_REGION_14_15__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION14_NUM_SEGMENTS__SHIFT            0xc
#define VPMPCC_MCM_1DLUT_RAMA_REGION_14_15__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION15_LUT_OFFSET__SHIFT              0x10
#define VPMPCC_MCM_1DLUT_RAMA_REGION_14_15__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION15_NUM_SEGMENTS__SHIFT            0x1c
#define VPMPCC_MCM_1DLUT_RAMA_REGION_14_15__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION14_LUT_OFFSET_MASK                0x000001FFL
#define VPMPCC_MCM_1DLUT_RAMA_REGION_14_15__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION14_NUM_SEGMENTS_MASK              0x00007000L
#define VPMPCC_MCM_1DLUT_RAMA_REGION_14_15__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION15_LUT_OFFSET_MASK                0x01FF0000L
#define VPMPCC_MCM_1DLUT_RAMA_REGION_14_15__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION15_NUM_SEGMENTS_MASK              0x70000000L
//VPMPCC_MCM_1DLUT_RAMA_REGION_16_17
#define VPMPCC_MCM_1DLUT_RAMA_REGION_16_17__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION16_LUT_OFFSET__SHIFT              0x0
#define VPMPCC_MCM_1DLUT_RAMA_REGION_16_17__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION16_NUM_SEGMENTS__SHIFT            0xc
#define VPMPCC_MCM_1DLUT_RAMA_REGION_16_17__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION17_LUT_OFFSET__SHIFT              0x10
#define VPMPCC_MCM_1DLUT_RAMA_REGION_16_17__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION17_NUM_SEGMENTS__SHIFT            0x1c
#define VPMPCC_MCM_1DLUT_RAMA_REGION_16_17__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION16_LUT_OFFSET_MASK                0x000001FFL
#define VPMPCC_MCM_1DLUT_RAMA_REGION_16_17__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION16_NUM_SEGMENTS_MASK              0x00007000L
#define VPMPCC_MCM_1DLUT_RAMA_REGION_16_17__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION17_LUT_OFFSET_MASK                0x01FF0000L
#define VPMPCC_MCM_1DLUT_RAMA_REGION_16_17__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION17_NUM_SEGMENTS_MASK              0x70000000L
//VPMPCC_MCM_1DLUT_RAMA_REGION_18_19
#define VPMPCC_MCM_1DLUT_RAMA_REGION_18_19__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION18_LUT_OFFSET__SHIFT              0x0
#define VPMPCC_MCM_1DLUT_RAMA_REGION_18_19__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION18_NUM_SEGMENTS__SHIFT            0xc
#define VPMPCC_MCM_1DLUT_RAMA_REGION_18_19__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION19_LUT_OFFSET__SHIFT              0x10
#define VPMPCC_MCM_1DLUT_RAMA_REGION_18_19__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION19_NUM_SEGMENTS__SHIFT            0x1c
#define VPMPCC_MCM_1DLUT_RAMA_REGION_18_19__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION18_LUT_OFFSET_MASK                0x000001FFL
#define VPMPCC_MCM_1DLUT_RAMA_REGION_18_19__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION18_NUM_SEGMENTS_MASK              0x00007000L
#define VPMPCC_MCM_1DLUT_RAMA_REGION_18_19__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION19_LUT_OFFSET_MASK                0x01FF0000L
#define VPMPCC_MCM_1DLUT_RAMA_REGION_18_19__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION19_NUM_SEGMENTS_MASK              0x70000000L
//VPMPCC_MCM_1DLUT_RAMA_REGION_20_21
#define VPMPCC_MCM_1DLUT_RAMA_REGION_20_21__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION20_LUT_OFFSET__SHIFT              0x0
#define VPMPCC_MCM_1DLUT_RAMA_REGION_20_21__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION20_NUM_SEGMENTS__SHIFT            0xc
#define VPMPCC_MCM_1DLUT_RAMA_REGION_20_21__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION21_LUT_OFFSET__SHIFT              0x10
#define VPMPCC_MCM_1DLUT_RAMA_REGION_20_21__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION21_NUM_SEGMENTS__SHIFT            0x1c
#define VPMPCC_MCM_1DLUT_RAMA_REGION_20_21__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION20_LUT_OFFSET_MASK                0x000001FFL
#define VPMPCC_MCM_1DLUT_RAMA_REGION_20_21__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION20_NUM_SEGMENTS_MASK              0x00007000L
#define VPMPCC_MCM_1DLUT_RAMA_REGION_20_21__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION21_LUT_OFFSET_MASK                0x01FF0000L
#define VPMPCC_MCM_1DLUT_RAMA_REGION_20_21__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION21_NUM_SEGMENTS_MASK              0x70000000L
//VPMPCC_MCM_1DLUT_RAMA_REGION_22_23
#define VPMPCC_MCM_1DLUT_RAMA_REGION_22_23__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION22_LUT_OFFSET__SHIFT              0x0
#define VPMPCC_MCM_1DLUT_RAMA_REGION_22_23__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION22_NUM_SEGMENTS__SHIFT            0xc
#define VPMPCC_MCM_1DLUT_RAMA_REGION_22_23__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION23_LUT_OFFSET__SHIFT              0x10
#define VPMPCC_MCM_1DLUT_RAMA_REGION_22_23__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION23_NUM_SEGMENTS__SHIFT            0x1c
#define VPMPCC_MCM_1DLUT_RAMA_REGION_22_23__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION22_LUT_OFFSET_MASK                0x000001FFL
#define VPMPCC_MCM_1DLUT_RAMA_REGION_22_23__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION22_NUM_SEGMENTS_MASK              0x00007000L
#define VPMPCC_MCM_1DLUT_RAMA_REGION_22_23__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION23_LUT_OFFSET_MASK                0x01FF0000L
#define VPMPCC_MCM_1DLUT_RAMA_REGION_22_23__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION23_NUM_SEGMENTS_MASK              0x70000000L
//VPMPCC_MCM_1DLUT_RAMA_REGION_24_25
#define VPMPCC_MCM_1DLUT_RAMA_REGION_24_25__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION24_LUT_OFFSET__SHIFT              0x0
#define VPMPCC_MCM_1DLUT_RAMA_REGION_24_25__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION24_NUM_SEGMENTS__SHIFT            0xc
#define VPMPCC_MCM_1DLUT_RAMA_REGION_24_25__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION25_LUT_OFFSET__SHIFT              0x10
#define VPMPCC_MCM_1DLUT_RAMA_REGION_24_25__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION25_NUM_SEGMENTS__SHIFT            0x1c
#define VPMPCC_MCM_1DLUT_RAMA_REGION_24_25__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION24_LUT_OFFSET_MASK                0x000001FFL
#define VPMPCC_MCM_1DLUT_RAMA_REGION_24_25__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION24_NUM_SEGMENTS_MASK              0x00007000L
#define VPMPCC_MCM_1DLUT_RAMA_REGION_24_25__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION25_LUT_OFFSET_MASK                0x01FF0000L
#define VPMPCC_MCM_1DLUT_RAMA_REGION_24_25__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION25_NUM_SEGMENTS_MASK              0x70000000L
//VPMPCC_MCM_1DLUT_RAMA_REGION_26_27
#define VPMPCC_MCM_1DLUT_RAMA_REGION_26_27__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION26_LUT_OFFSET__SHIFT              0x0
#define VPMPCC_MCM_1DLUT_RAMA_REGION_26_27__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION26_NUM_SEGMENTS__SHIFT            0xc
#define VPMPCC_MCM_1DLUT_RAMA_REGION_26_27__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION27_LUT_OFFSET__SHIFT              0x10
#define VPMPCC_MCM_1DLUT_RAMA_REGION_26_27__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION27_NUM_SEGMENTS__SHIFT            0x1c
#define VPMPCC_MCM_1DLUT_RAMA_REGION_26_27__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION26_LUT_OFFSET_MASK                0x000001FFL
#define VPMPCC_MCM_1DLUT_RAMA_REGION_26_27__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION26_NUM_SEGMENTS_MASK              0x00007000L
#define VPMPCC_MCM_1DLUT_RAMA_REGION_26_27__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION27_LUT_OFFSET_MASK                0x01FF0000L
#define VPMPCC_MCM_1DLUT_RAMA_REGION_26_27__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION27_NUM_SEGMENTS_MASK              0x70000000L
//VPMPCC_MCM_1DLUT_RAMA_REGION_28_29
#define VPMPCC_MCM_1DLUT_RAMA_REGION_28_29__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION28_LUT_OFFSET__SHIFT              0x0
#define VPMPCC_MCM_1DLUT_RAMA_REGION_28_29__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION28_NUM_SEGMENTS__SHIFT            0xc
#define VPMPCC_MCM_1DLUT_RAMA_REGION_28_29__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION29_LUT_OFFSET__SHIFT              0x10
#define VPMPCC_MCM_1DLUT_RAMA_REGION_28_29__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION29_NUM_SEGMENTS__SHIFT            0x1c
#define VPMPCC_MCM_1DLUT_RAMA_REGION_28_29__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION28_LUT_OFFSET_MASK                0x000001FFL
#define VPMPCC_MCM_1DLUT_RAMA_REGION_28_29__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION28_NUM_SEGMENTS_MASK              0x00007000L
#define VPMPCC_MCM_1DLUT_RAMA_REGION_28_29__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION29_LUT_OFFSET_MASK                0x01FF0000L
#define VPMPCC_MCM_1DLUT_RAMA_REGION_28_29__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION29_NUM_SEGMENTS_MASK              0x70000000L
//VPMPCC_MCM_1DLUT_RAMA_REGION_30_31
#define VPMPCC_MCM_1DLUT_RAMA_REGION_30_31__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION30_LUT_OFFSET__SHIFT              0x0
#define VPMPCC_MCM_1DLUT_RAMA_REGION_30_31__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION30_NUM_SEGMENTS__SHIFT            0xc
#define VPMPCC_MCM_1DLUT_RAMA_REGION_30_31__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION31_LUT_OFFSET__SHIFT              0x10
#define VPMPCC_MCM_1DLUT_RAMA_REGION_30_31__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION31_NUM_SEGMENTS__SHIFT            0x1c
#define VPMPCC_MCM_1DLUT_RAMA_REGION_30_31__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION30_LUT_OFFSET_MASK                0x000001FFL
#define VPMPCC_MCM_1DLUT_RAMA_REGION_30_31__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION30_NUM_SEGMENTS_MASK              0x00007000L
#define VPMPCC_MCM_1DLUT_RAMA_REGION_30_31__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION31_LUT_OFFSET_MASK                0x01FF0000L
#define VPMPCC_MCM_1DLUT_RAMA_REGION_30_31__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION31_NUM_SEGMENTS_MASK              0x70000000L
//VPMPCC_MCM_1DLUT_RAMA_REGION_32_33
#define VPMPCC_MCM_1DLUT_RAMA_REGION_32_33__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION32_LUT_OFFSET__SHIFT              0x0
#define VPMPCC_MCM_1DLUT_RAMA_REGION_32_33__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION32_NUM_SEGMENTS__SHIFT            0xc
#define VPMPCC_MCM_1DLUT_RAMA_REGION_32_33__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION33_LUT_OFFSET__SHIFT              0x10
#define VPMPCC_MCM_1DLUT_RAMA_REGION_32_33__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION33_NUM_SEGMENTS__SHIFT            0x1c
#define VPMPCC_MCM_1DLUT_RAMA_REGION_32_33__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION32_LUT_OFFSET_MASK                0x000001FFL
#define VPMPCC_MCM_1DLUT_RAMA_REGION_32_33__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION32_NUM_SEGMENTS_MASK              0x00007000L
#define VPMPCC_MCM_1DLUT_RAMA_REGION_32_33__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION33_LUT_OFFSET_MASK                0x01FF0000L
#define VPMPCC_MCM_1DLUT_RAMA_REGION_32_33__VPMPCC_MCM_1DLUT_RAMA_EXP_REGION33_NUM_SEGMENTS_MASK              0x70000000L
//VPMPCC_MCM_MEM_PWR_CTRL
#define VPMPCC_MCM_MEM_PWR_CTRL__VPMPCC_MCM_SHAPER_MEM_PWR_FORCE__SHIFT                                       0x0
#define VPMPCC_MCM_MEM_PWR_CTRL__VPMPCC_MCM_SHAPER_MEM_PWR_DIS__SHIFT                                         0x2
#define VPMPCC_MCM_MEM_PWR_CTRL__VPMPCC_MCM_SHAPER_MEM_LOW_PWR_MODE__SHIFT                                    0x4
#define VPMPCC_MCM_MEM_PWR_CTRL__VPMPCC_MCM_3DLUT_MEM_PWR_FORCE__SHIFT                                        0x8
#define VPMPCC_MCM_MEM_PWR_CTRL__VPMPCC_MCM_3DLUT_MEM_PWR_DIS__SHIFT                                          0xa
#define VPMPCC_MCM_MEM_PWR_CTRL__VPMPCC_MCM_3DLUT_MEM_LOW_PWR_MODE__SHIFT                                     0xc
#define VPMPCC_MCM_MEM_PWR_CTRL__VPMPCC_MCM_1DLUT_MEM_PWR_FORCE__SHIFT                                        0x10
#define VPMPCC_MCM_MEM_PWR_CTRL__VPMPCC_MCM_1DLUT_MEM_PWR_DIS__SHIFT                                          0x12
#define VPMPCC_MCM_MEM_PWR_CTRL__VPMPCC_MCM_1DLUT_MEM_LOW_PWR_MODE__SHIFT                                     0x14
#define VPMPCC_MCM_MEM_PWR_CTRL__VPMPCC_MCM_SHAPER_MEM_PWR_STATE__SHIFT                                       0x18
#define VPMPCC_MCM_MEM_PWR_CTRL__VPMPCC_MCM_3DLUT_MEM_PWR_STATE__SHIFT                                        0x1a
#define VPMPCC_MCM_MEM_PWR_CTRL__VPMPCC_MCM_1DLUT_MEM_PWR_STATE__SHIFT                                        0x1c
#define VPMPCC_MCM_MEM_PWR_CTRL__VPMPCC_MCM_SHAPER_MEM_PWR_FORCE_MASK                                         0x00000003L
#define VPMPCC_MCM_MEM_PWR_CTRL__VPMPCC_MCM_SHAPER_MEM_PWR_DIS_MASK                                           0x00000004L
#define VPMPCC_MCM_MEM_PWR_CTRL__VPMPCC_MCM_SHAPER_MEM_LOW_PWR_MODE_MASK                                      0x00000030L
#define VPMPCC_MCM_MEM_PWR_CTRL__VPMPCC_MCM_3DLUT_MEM_PWR_FORCE_MASK                                          0x00000300L
#define VPMPCC_MCM_MEM_PWR_CTRL__VPMPCC_MCM_3DLUT_MEM_PWR_DIS_MASK                                            0x00000400L
#define VPMPCC_MCM_MEM_PWR_CTRL__VPMPCC_MCM_3DLUT_MEM_LOW_PWR_MODE_MASK                                       0x00003000L
#define VPMPCC_MCM_MEM_PWR_CTRL__VPMPCC_MCM_1DLUT_MEM_PWR_FORCE_MASK                                          0x00030000L
#define VPMPCC_MCM_MEM_PWR_CTRL__VPMPCC_MCM_1DLUT_MEM_PWR_DIS_MASK                                            0x00040000L
#define VPMPCC_MCM_MEM_PWR_CTRL__VPMPCC_MCM_1DLUT_MEM_LOW_PWR_MODE_MASK                                       0x00300000L
#define VPMPCC_MCM_MEM_PWR_CTRL__VPMPCC_MCM_SHAPER_MEM_PWR_STATE_MASK                                         0x03000000L
#define VPMPCC_MCM_MEM_PWR_CTRL__VPMPCC_MCM_3DLUT_MEM_PWR_STATE_MASK                                          0x0C000000L
#define VPMPCC_MCM_MEM_PWR_CTRL__VPMPCC_MCM_1DLUT_MEM_PWR_STATE_MASK                                          0x30000000L
//VPMPCC_MCM_TEST_DEBUG_INDEX
#define VPMPCC_MCM_TEST_DEBUG_INDEX__VPMPCC_MCM_TEST_DEBUG_INDEX__SHIFT                                       0x0
#define VPMPCC_MCM_TEST_DEBUG_INDEX__VPMPCC_MCM_TEST_DEBUG_WRITE_EN__SHIFT                                    0x8
#define VPMPCC_MCM_TEST_DEBUG_INDEX__VPMPCC_MCM_TEST_DEBUG_INDEX_MASK                                         0x000000FFL
#define VPMPCC_MCM_TEST_DEBUG_INDEX__VPMPCC_MCM_TEST_DEBUG_WRITE_EN_MASK                                      0x00000100L
//VPMPCC_MCM_TEST_DEBUG_DATA
#define VPMPCC_MCM_TEST_DEBUG_DATA__VPMPCC_MCM_TEST_DEBUG_DATA__SHIFT                                         0x0
#define VPMPCC_MCM_TEST_DEBUG_DATA__VPMPCC_MCM_TEST_DEBUG_DATA_MASK                                           0xFFFFFFFFL


// addressBlock: vpe_vpep_vpmpc_vpmpc_ocsc_dispdec
//VPMPC_OUT0_MUX
#define VPMPC_OUT0_MUX__VPMPC_OUT_MUX__SHIFT                                                                  0x0
#define VPMPC_OUT0_MUX__VPMPC_OUT_MUX_MASK                                                                    0x0000000FL
//VPMPC_OUT0_FLOAT_CONTROL
#define VPMPC_OUT0_FLOAT_CONTROL__VPMPC_OUT_FLOAT_EN__SHIFT                                                   0x0
#define VPMPC_OUT0_FLOAT_CONTROL__VPMPC_OUT_FLOAT_EN_MASK                                                     0x00000001L
//VPMPC_OUT0_DENORM_CONTROL
#define VPMPC_OUT0_DENORM_CONTROL__VPMPC_OUT_DENORM_CLAMP_MIN_R_CR__SHIFT                                     0x0
#define VPMPC_OUT0_DENORM_CONTROL__VPMPC_OUT_DENORM_CLAMP_MAX_R_CR__SHIFT                                     0xc
#define VPMPC_OUT0_DENORM_CONTROL__VPMPC_OUT_DENORM_MODE__SHIFT                                               0x18
#define VPMPC_OUT0_DENORM_CONTROL__VPMPC_OUT_DENORM_CLAMP_MIN_R_CR_MASK                                       0x00000FFFL
#define VPMPC_OUT0_DENORM_CONTROL__VPMPC_OUT_DENORM_CLAMP_MAX_R_CR_MASK                                       0x00FFF000L
#define VPMPC_OUT0_DENORM_CONTROL__VPMPC_OUT_DENORM_MODE_MASK                                                 0x07000000L
//VPMPC_OUT0_DENORM_CLAMP_G_Y
#define VPMPC_OUT0_DENORM_CLAMP_G_Y__VPMPC_OUT_DENORM_CLAMP_MIN_G_Y__SHIFT                                    0x0
#define VPMPC_OUT0_DENORM_CLAMP_G_Y__VPMPC_OUT_DENORM_CLAMP_MAX_G_Y__SHIFT                                    0xc
#define VPMPC_OUT0_DENORM_CLAMP_G_Y__VPMPC_OUT_DENORM_CLAMP_MIN_G_Y_MASK                                      0x00000FFFL
#define VPMPC_OUT0_DENORM_CLAMP_G_Y__VPMPC_OUT_DENORM_CLAMP_MAX_G_Y_MASK                                      0x00FFF000L
//VPMPC_OUT0_DENORM_CLAMP_B_CB
#define VPMPC_OUT0_DENORM_CLAMP_B_CB__VPMPC_OUT_DENORM_CLAMP_MIN_B_CB__SHIFT                                  0x0
#define VPMPC_OUT0_DENORM_CLAMP_B_CB__VPMPC_OUT_DENORM_CLAMP_MAX_B_CB__SHIFT                                  0xc
#define VPMPC_OUT0_DENORM_CLAMP_B_CB__VPMPC_OUT_DENORM_CLAMP_MIN_B_CB_MASK                                    0x00000FFFL
#define VPMPC_OUT0_DENORM_CLAMP_B_CB__VPMPC_OUT_DENORM_CLAMP_MAX_B_CB_MASK                                    0x00FFF000L
//VPMPC_OUT_CSC_COEF_FORMAT
#define VPMPC_OUT_CSC_COEF_FORMAT__VPMPC_OCSC0_COEF_FORMAT__SHIFT                                             0x0
#define VPMPC_OUT_CSC_COEF_FORMAT__VPMPC_OCSC0_COEF_FORMAT_MASK                                               0x00000001L
//VPMPC_OUT0_CSC_MODE
#define VPMPC_OUT0_CSC_MODE__VPMPC_OCSC_MODE__SHIFT                                                           0x0
#define VPMPC_OUT0_CSC_MODE__VPMPC_OCSC_MODE_CURRENT__SHIFT                                                   0x7
#define VPMPC_OUT0_CSC_MODE__VPMPC_OCSC_MODE_MASK                                                             0x00000001L
#define VPMPC_OUT0_CSC_MODE__VPMPC_OCSC_MODE_CURRENT_MASK                                                     0x00000080L
//VPMPC_OUT0_CSC_C11_C12_A
#define VPMPC_OUT0_CSC_C11_C12_A__VPMPC_OCSC_C11_A__SHIFT                                                     0x0
#define VPMPC_OUT0_CSC_C11_C12_A__VPMPC_OCSC_C12_A__SHIFT                                                     0x10
#define VPMPC_OUT0_CSC_C11_C12_A__VPMPC_OCSC_C11_A_MASK                                                       0x0000FFFFL
#define VPMPC_OUT0_CSC_C11_C12_A__VPMPC_OCSC_C12_A_MASK                                                       0xFFFF0000L
//VPMPC_OUT0_CSC_C13_C14_A
#define VPMPC_OUT0_CSC_C13_C14_A__VPMPC_OCSC_C13_A__SHIFT                                                     0x0
#define VPMPC_OUT0_CSC_C13_C14_A__VPMPC_OCSC_C14_A__SHIFT                                                     0x10
#define VPMPC_OUT0_CSC_C13_C14_A__VPMPC_OCSC_C13_A_MASK                                                       0x0000FFFFL
#define VPMPC_OUT0_CSC_C13_C14_A__VPMPC_OCSC_C14_A_MASK                                                       0xFFFF0000L
//VPMPC_OUT0_CSC_C21_C22_A
#define VPMPC_OUT0_CSC_C21_C22_A__VPMPC_OCSC_C21_A__SHIFT                                                     0x0
#define VPMPC_OUT0_CSC_C21_C22_A__VPMPC_OCSC_C22_A__SHIFT                                                     0x10
#define VPMPC_OUT0_CSC_C21_C22_A__VPMPC_OCSC_C21_A_MASK                                                       0x0000FFFFL
#define VPMPC_OUT0_CSC_C21_C22_A__VPMPC_OCSC_C22_A_MASK                                                       0xFFFF0000L
//VPMPC_OUT0_CSC_C23_C24_A
#define VPMPC_OUT0_CSC_C23_C24_A__VPMPC_OCSC_C23_A__SHIFT                                                     0x0
#define VPMPC_OUT0_CSC_C23_C24_A__VPMPC_OCSC_C24_A__SHIFT                                                     0x10
#define VPMPC_OUT0_CSC_C23_C24_A__VPMPC_OCSC_C23_A_MASK                                                       0x0000FFFFL
#define VPMPC_OUT0_CSC_C23_C24_A__VPMPC_OCSC_C24_A_MASK                                                       0xFFFF0000L
//VPMPC_OUT0_CSC_C31_C32_A
#define VPMPC_OUT0_CSC_C31_C32_A__VPMPC_OCSC_C31_A__SHIFT                                                     0x0
#define VPMPC_OUT0_CSC_C31_C32_A__VPMPC_OCSC_C32_A__SHIFT                                                     0x10
#define VPMPC_OUT0_CSC_C31_C32_A__VPMPC_OCSC_C31_A_MASK                                                       0x0000FFFFL
#define VPMPC_OUT0_CSC_C31_C32_A__VPMPC_OCSC_C32_A_MASK                                                       0xFFFF0000L
//VPMPC_OUT0_CSC_C33_C34_A
#define VPMPC_OUT0_CSC_C33_C34_A__VPMPC_OCSC_C33_A__SHIFT                                                     0x0
#define VPMPC_OUT0_CSC_C33_C34_A__VPMPC_OCSC_C34_A__SHIFT                                                     0x10
#define VPMPC_OUT0_CSC_C33_C34_A__VPMPC_OCSC_C33_A_MASK                                                       0x0000FFFFL
#define VPMPC_OUT0_CSC_C33_C34_A__VPMPC_OCSC_C34_A_MASK                                                       0xFFFF0000L


// addressBlock: vpe_vpep_vpopp_vpfmt0_dispdec
//VPFMT_CLAMP_COMPONENT_R
#define VPFMT_CLAMP_COMPONENT_R__VPFMT_CLAMP_LOWER_R__SHIFT                                                   0x0
#define VPFMT_CLAMP_COMPONENT_R__VPFMT_CLAMP_UPPER_R__SHIFT                                                   0x10
#define VPFMT_CLAMP_COMPONENT_R__VPFMT_CLAMP_LOWER_R_MASK                                                     0x0000FFFFL
#define VPFMT_CLAMP_COMPONENT_R__VPFMT_CLAMP_UPPER_R_MASK                                                     0xFFFF0000L
//VPFMT_CLAMP_COMPONENT_G
#define VPFMT_CLAMP_COMPONENT_G__VPFMT_CLAMP_LOWER_G__SHIFT                                                   0x0
#define VPFMT_CLAMP_COMPONENT_G__VPFMT_CLAMP_UPPER_G__SHIFT                                                   0x10
#define VPFMT_CLAMP_COMPONENT_G__VPFMT_CLAMP_LOWER_G_MASK                                                     0x0000FFFFL
#define VPFMT_CLAMP_COMPONENT_G__VPFMT_CLAMP_UPPER_G_MASK                                                     0xFFFF0000L
//VPFMT_CLAMP_COMPONENT_B
#define VPFMT_CLAMP_COMPONENT_B__VPFMT_CLAMP_LOWER_B__SHIFT                                                   0x0
#define VPFMT_CLAMP_COMPONENT_B__VPFMT_CLAMP_UPPER_B__SHIFT                                                   0x10
#define VPFMT_CLAMP_COMPONENT_B__VPFMT_CLAMP_LOWER_B_MASK                                                     0x0000FFFFL
#define VPFMT_CLAMP_COMPONENT_B__VPFMT_CLAMP_UPPER_B_MASK                                                     0xFFFF0000L
//VPFMT_DYNAMIC_EXP_CNTL
#define VPFMT_DYNAMIC_EXP_CNTL__VPFMT_DYNAMIC_EXP_EN__SHIFT                                                   0x0
#define VPFMT_DYNAMIC_EXP_CNTL__VPFMT_DYNAMIC_EXP_MODE__SHIFT                                                 0x4
#define VPFMT_DYNAMIC_EXP_CNTL__VPFMT_DYNAMIC_EXP_EN_MASK                                                     0x00000001L
#define VPFMT_DYNAMIC_EXP_CNTL__VPFMT_DYNAMIC_EXP_MODE_MASK                                                   0x00000010L
//VPFMT_CONTROL
#define VPFMT_CONTROL__VPFMT_SPATIAL_DITHER_FRAME_COUNTER_MAX__SHIFT                                          0x8
#define VPFMT_CONTROL__VPFMT_SPATIAL_DITHER_FRAME_COUNTER_BIT_SWAP__SHIFT                                     0xc
#define VPFMT_CONTROL__VPFMT_CBCR_BIT_REDUCTION_BYPASS__SHIFT                                                 0x15
#define VPFMT_CONTROL__VPFMT_DOUBLE_BUFFER_REG_UPDATE_PENDING__SHIFT                                          0x18
#define VPFMT_CONTROL__VPFMT_SPATIAL_DITHER_FRAME_COUNTER_MAX_MASK                                            0x00000F00L
#define VPFMT_CONTROL__VPFMT_SPATIAL_DITHER_FRAME_COUNTER_BIT_SWAP_MASK                                       0x00003000L
#define VPFMT_CONTROL__VPFMT_CBCR_BIT_REDUCTION_BYPASS_MASK                                                   0x00200000L
#define VPFMT_CONTROL__VPFMT_DOUBLE_BUFFER_REG_UPDATE_PENDING_MASK                                            0x01000000L
//VPFMT_BIT_DEPTH_CONTROL
#define VPFMT_BIT_DEPTH_CONTROL__VPFMT_TRUNCATE_EN__SHIFT                                                     0x0
#define VPFMT_BIT_DEPTH_CONTROL__VPFMT_TRUNCATE_MODE__SHIFT                                                   0x1
#define VPFMT_BIT_DEPTH_CONTROL__VPFMT_TRUNCATE_DEPTH__SHIFT                                                  0x4
#define VPFMT_BIT_DEPTH_CONTROL__VPFMT_SPATIAL_DITHER_EN__SHIFT                                               0x8
#define VPFMT_BIT_DEPTH_CONTROL__VPFMT_SPATIAL_DITHER_MODE__SHIFT                                             0x9
#define VPFMT_BIT_DEPTH_CONTROL__VPFMT_SPATIAL_DITHER_DEPTH__SHIFT                                            0xb
#define VPFMT_BIT_DEPTH_CONTROL__VPFMT_FRAME_RANDOM_ENABLE__SHIFT                                             0xd
#define VPFMT_BIT_DEPTH_CONTROL__VPFMT_RGB_RANDOM_ENABLE__SHIFT                                               0xe
#define VPFMT_BIT_DEPTH_CONTROL__VPFMT_HIGHPASS_RANDOM_ENABLE__SHIFT                                          0xf
#define VPFMT_BIT_DEPTH_CONTROL__VPFMT_TRUNCATE_EN_MASK                                                       0x00000001L
#define VPFMT_BIT_DEPTH_CONTROL__VPFMT_TRUNCATE_MODE_MASK                                                     0x00000002L
#define VPFMT_BIT_DEPTH_CONTROL__VPFMT_TRUNCATE_DEPTH_MASK                                                    0x00000030L
#define VPFMT_BIT_DEPTH_CONTROL__VPFMT_SPATIAL_DITHER_EN_MASK                                                 0x00000100L
#define VPFMT_BIT_DEPTH_CONTROL__VPFMT_SPATIAL_DITHER_MODE_MASK                                               0x00000600L
#define VPFMT_BIT_DEPTH_CONTROL__VPFMT_SPATIAL_DITHER_DEPTH_MASK                                              0x00001800L
#define VPFMT_BIT_DEPTH_CONTROL__VPFMT_FRAME_RANDOM_ENABLE_MASK                                               0x00002000L
#define VPFMT_BIT_DEPTH_CONTROL__VPFMT_RGB_RANDOM_ENABLE_MASK                                                 0x00004000L
#define VPFMT_BIT_DEPTH_CONTROL__VPFMT_HIGHPASS_RANDOM_ENABLE_MASK                                            0x00008000L
//VPFMT_DITHER_RAND_R_SEED
#define VPFMT_DITHER_RAND_R_SEED__VPFMT_RAND_R_SEED__SHIFT                                                    0x0
#define VPFMT_DITHER_RAND_R_SEED__VPFMT_OFFSET_R_CR__SHIFT                                                    0x10
#define VPFMT_DITHER_RAND_R_SEED__VPFMT_RAND_R_SEED_MASK                                                      0x000000FFL
#define VPFMT_DITHER_RAND_R_SEED__VPFMT_OFFSET_R_CR_MASK                                                      0xFFFF0000L
//VPFMT_DITHER_RAND_G_SEED
#define VPFMT_DITHER_RAND_G_SEED__VPFMT_RAND_G_SEED__SHIFT                                                    0x0
#define VPFMT_DITHER_RAND_G_SEED__VPFMT_OFFSET_G_Y__SHIFT                                                     0x10
#define VPFMT_DITHER_RAND_G_SEED__VPFMT_RAND_G_SEED_MASK                                                      0x000000FFL
#define VPFMT_DITHER_RAND_G_SEED__VPFMT_OFFSET_G_Y_MASK                                                       0xFFFF0000L
//VPFMT_DITHER_RAND_B_SEED
#define VPFMT_DITHER_RAND_B_SEED__VPFMT_RAND_B_SEED__SHIFT                                                    0x0
#define VPFMT_DITHER_RAND_B_SEED__VPFMT_OFFSET_B_CB__SHIFT                                                    0x10
#define VPFMT_DITHER_RAND_B_SEED__VPFMT_RAND_B_SEED_MASK                                                      0x000000FFL
#define VPFMT_DITHER_RAND_B_SEED__VPFMT_OFFSET_B_CB_MASK                                                      0xFFFF0000L
//VPFMT_CLAMP_CNTL
#define VPFMT_CLAMP_CNTL__VPFMT_CLAMP_DATA_EN__SHIFT                                                          0x0
#define VPFMT_CLAMP_CNTL__VPFMT_CLAMP_COLOR_FORMAT__SHIFT                                                     0x10
#define VPFMT_CLAMP_CNTL__VPFMT_CLAMP_DATA_EN_MASK                                                            0x00000001L
#define VPFMT_CLAMP_CNTL__VPFMT_CLAMP_COLOR_FORMAT_MASK                                                       0x00070000L


// addressBlock: vpe_vpep_vpopp_vpopp_pipe0_dispdec
//VPOPP_PIPE_CONTROL
#define VPOPP_PIPE_CONTROL__VPOPP_PIPE_CLOCK_ON__SHIFT                                                        0x1
#define VPOPP_PIPE_CONTROL__VPOPP_PIPE_DIGITAL_BYPASS_EN__SHIFT                                               0x4
#define VPOPP_PIPE_CONTROL__VPOPP_PIPE_ALPHA__SHIFT                                                           0x10
#define VPOPP_PIPE_CONTROL__VPOPP_PIPE_CLOCK_ON_MASK                                                          0x00000002L
#define VPOPP_PIPE_CONTROL__VPOPP_PIPE_DIGITAL_BYPASS_EN_MASK                                                 0x00000010L
#define VPOPP_PIPE_CONTROL__VPOPP_PIPE_ALPHA_MASK                                                             0xFFFF0000L


// addressBlock: vpe_vpep_vpopp_vpopp_pipe_crc0_dispdec
//VPOPP_PIPE_CRC_CONTROL
#define VPOPP_PIPE_CRC_CONTROL__VPOPP_PIPE_CRC_EN__SHIFT                                                      0x0
#define VPOPP_PIPE_CRC_CONTROL__VPOPP_PIPE_CRC_CONT_EN__SHIFT                                                 0x4
#define VPOPP_PIPE_CRC_CONTROL__VPOPP_PIPE_CRC_PIXEL_SELECT__SHIFT                                            0x14
#define VPOPP_PIPE_CRC_CONTROL__VPOPP_PIPE_CRC_ONE_SHOT_PENDING__SHIFT                                        0x1c
#define VPOPP_PIPE_CRC_CONTROL__VPOPP_PIPE_CRC_EN_MASK                                                        0x00000001L
#define VPOPP_PIPE_CRC_CONTROL__VPOPP_PIPE_CRC_CONT_EN_MASK                                                   0x00000010L
#define VPOPP_PIPE_CRC_CONTROL__VPOPP_PIPE_CRC_PIXEL_SELECT_MASK                                              0x00300000L
#define VPOPP_PIPE_CRC_CONTROL__VPOPP_PIPE_CRC_ONE_SHOT_PENDING_MASK                                          0x10000000L
//VPOPP_PIPE_CRC_MASK
#define VPOPP_PIPE_CRC_MASK__VPOPP_PIPE_CRC_MASK__SHIFT                                                       0x0
#define VPOPP_PIPE_CRC_MASK__VPOPP_PIPE_CRC_MASK_MASK                                                         0x0000FFFFL
//VPOPP_PIPE_CRC_RESULT0
#define VPOPP_PIPE_CRC_RESULT0__VPOPP_PIPE_CRC_RESULT_A__SHIFT                                                0x0
#define VPOPP_PIPE_CRC_RESULT0__VPOPP_PIPE_CRC_RESULT_R__SHIFT                                                0x10
#define VPOPP_PIPE_CRC_RESULT0__VPOPP_PIPE_CRC_RESULT_A_MASK                                                  0x0000FFFFL
#define VPOPP_PIPE_CRC_RESULT0__VPOPP_PIPE_CRC_RESULT_R_MASK                                                  0xFFFF0000L
//VPOPP_PIPE_CRC_RESULT1
#define VPOPP_PIPE_CRC_RESULT1__VPOPP_PIPE_CRC_RESULT_G__SHIFT                                                0x0
#define VPOPP_PIPE_CRC_RESULT1__VPOPP_PIPE_CRC_RESULT_B__SHIFT                                                0x10
#define VPOPP_PIPE_CRC_RESULT1__VPOPP_PIPE_CRC_RESULT_G_MASK                                                  0x0000FFFFL
#define VPOPP_PIPE_CRC_RESULT1__VPOPP_PIPE_CRC_RESULT_B_MASK                                                  0xFFFF0000L
//VPOPP_PIPE_CRC_RESULT2
#define VPOPP_PIPE_CRC_RESULT2__VPOPP_PIPE_CRC_RESULT_C__SHIFT                                                0x0
#define VPOPP_PIPE_CRC_RESULT2__VPOPP_PIPE_CRC_RESULT_C_MASK                                                  0x0000FFFFL


// addressBlock: vpe_vpep_vpopp_vpopp_top_dispdec
//VPOPP_TOP_CLK_CONTROL
#define VPOPP_TOP_CLK_CONTROL__VPOPP_VPECLK_R_GATE_DIS__SHIFT                                                 0x0
#define VPOPP_TOP_CLK_CONTROL__VPOPP_VPECLK_G_GATE_DIS__SHIFT                                                 0x1
#define VPOPP_TOP_CLK_CONTROL__VPOPP_VPECLK_R_GATE_DIS_MASK                                                   0x00000001L
#define VPOPP_TOP_CLK_CONTROL__VPOPP_VPECLK_G_GATE_DIS_MASK                                                   0x00000002L


// addressBlock: vpe_vpep_vpcdc_cdc_dispdec
//VPEP_MGCG_CNTL
#define VPEP_MGCG_CNTL__VPDPP0_CLK_GATE_DIS__SHIFT                                                            0x0
#define VPEP_MGCG_CNTL__VPMPC_CLK_GATE_DIS__SHIFT                                                             0xc
#define VPEP_MGCG_CNTL__VPOPP_CLK_GATE_DIS__SHIFT                                                             0x12
#define VPEP_MGCG_CNTL__VPCDC_SOCCLK_G_GATE_DIS__SHIFT                                                        0x14
#define VPEP_MGCG_CNTL__VPCDC_SOCCLK_R_GATE_DIS__SHIFT                                                        0x15
#define VPEP_MGCG_CNTL__VPCDC_VPECLK_G_GATE_DIS__SHIFT                                                        0x16
#define VPEP_MGCG_CNTL__VPCDC_VPECLK_R_GATE_DIS__SHIFT                                                        0x17
#define VPEP_MGCG_CNTL__VPDPP0_CLK_GATE_DIS_MASK                                                              0x00000007L
#define VPEP_MGCG_CNTL__VPMPC_CLK_GATE_DIS_MASK                                                               0x00003000L
#define VPEP_MGCG_CNTL__VPOPP_CLK_GATE_DIS_MASK                                                               0x000C0000L
#define VPEP_MGCG_CNTL__VPCDC_SOCCLK_G_GATE_DIS_MASK                                                          0x00100000L
#define VPEP_MGCG_CNTL__VPCDC_SOCCLK_R_GATE_DIS_MASK                                                          0x00200000L
#define VPEP_MGCG_CNTL__VPCDC_VPECLK_G_GATE_DIS_MASK                                                          0x00400000L
#define VPEP_MGCG_CNTL__VPCDC_VPECLK_R_GATE_DIS_MASK                                                          0x00800000L
//VPCDC_SOFT_RESET
#define VPCDC_SOFT_RESET__VPCDC_SOCCLK_SOFT_RESET__SHIFT                                                      0x0
#define VPCDC_SOFT_RESET__VPCDC_VPECLK_SOFT_RESET__SHIFT                                                      0x1
#define VPCDC_SOFT_RESET__VPCDC_SOCCLK_SOFT_RESET_MASK                                                        0x00000001L
#define VPCDC_SOFT_RESET__VPCDC_VPECLK_SOFT_RESET_MASK                                                        0x00000002L
//VPCDC_FE0_SURFACE_CONFIG
#define VPCDC_FE0_SURFACE_CONFIG__SURFACE_PIXEL_FORMAT_FE0__SHIFT                                             0x0
#define VPCDC_FE0_SURFACE_CONFIG__ROTATION_ANGLE_FE0__SHIFT                                                   0x8
#define VPCDC_FE0_SURFACE_CONFIG__H_MIRROR_EN_FE0__SHIFT                                                      0xa
#define VPCDC_FE0_SURFACE_CONFIG__PIX_SURFACE_LINEAR_FE0__SHIFT                                               0xb
#define VPCDC_FE0_SURFACE_CONFIG__SURFACE_PIXEL_FORMAT_FE0_MASK                                               0x0000007FL
#define VPCDC_FE0_SURFACE_CONFIG__ROTATION_ANGLE_FE0_MASK                                                     0x00000300L
#define VPCDC_FE0_SURFACE_CONFIG__H_MIRROR_EN_FE0_MASK                                                        0x00000400L
#define VPCDC_FE0_SURFACE_CONFIG__PIX_SURFACE_LINEAR_FE0_MASK                                                 0x00000800L
//VPCDC_FE0_CROSSBAR_CONFIG
#define VPCDC_FE0_CROSSBAR_CONFIG__CROSSBAR_SRC_ALPHA_FE0__SHIFT                                              0x0
#define VPCDC_FE0_CROSSBAR_CONFIG__CROSSBAR_SRC_Y_G_FE0__SHIFT                                                0x2
#define VPCDC_FE0_CROSSBAR_CONFIG__CROSSBAR_SRC_CB_B_FE0__SHIFT                                               0x4
#define VPCDC_FE0_CROSSBAR_CONFIG__CROSSBAR_SRC_CR_R_FE0__SHIFT                                               0x6
#define VPCDC_FE0_CROSSBAR_CONFIG__CROSSBAR_SRC_ALPHA_FE0_MASK                                                0x00000003L
#define VPCDC_FE0_CROSSBAR_CONFIG__CROSSBAR_SRC_Y_G_FE0_MASK                                                  0x0000000CL
#define VPCDC_FE0_CROSSBAR_CONFIG__CROSSBAR_SRC_CB_B_FE0_MASK                                                 0x00000030L
#define VPCDC_FE0_CROSSBAR_CONFIG__CROSSBAR_SRC_CR_R_FE0_MASK                                                 0x000000C0L
//VPCDC_FE0_VIEWPORT_START_CONFIG
#define VPCDC_FE0_VIEWPORT_START_CONFIG__VIEWPORT_X_START_FE0__SHIFT                                          0x0
#define VPCDC_FE0_VIEWPORT_START_CONFIG__VIEWPORT_Y_START_FE0__SHIFT                                          0x10
#define VPCDC_FE0_VIEWPORT_START_CONFIG__VIEWPORT_X_START_FE0_MASK                                            0x00003FFFL
#define VPCDC_FE0_VIEWPORT_START_CONFIG__VIEWPORT_Y_START_FE0_MASK                                            0x3FFF0000L
//VPCDC_FE0_VIEWPORT_DIMENSION_CONFIG
#define VPCDC_FE0_VIEWPORT_DIMENSION_CONFIG__VIEWPORT_WIDTH_FE0__SHIFT                                        0x0
#define VPCDC_FE0_VIEWPORT_DIMENSION_CONFIG__VIEWPORT_HEIGHT_FE0__SHIFT                                       0x10
#define VPCDC_FE0_VIEWPORT_DIMENSION_CONFIG__VIEWPORT_WIDTH_FE0_MASK                                          0x00003FFFL
#define VPCDC_FE0_VIEWPORT_DIMENSION_CONFIG__VIEWPORT_HEIGHT_FE0_MASK                                         0x3FFF0000L
//VPCDC_FE0_VIEWPORT_START_C_CONFIG
#define VPCDC_FE0_VIEWPORT_START_C_CONFIG__VIEWPORT_X_START_C_FE0__SHIFT                                      0x0
#define VPCDC_FE0_VIEWPORT_START_C_CONFIG__VIEWPORT_Y_START_C_FE0__SHIFT                                      0x10
#define VPCDC_FE0_VIEWPORT_START_C_CONFIG__VIEWPORT_X_START_C_FE0_MASK                                        0x00003FFFL
#define VPCDC_FE0_VIEWPORT_START_C_CONFIG__VIEWPORT_Y_START_C_FE0_MASK                                        0x3FFF0000L
//VPCDC_FE0_VIEWPORT_DIMENSION_C_CONFIG
#define VPCDC_FE0_VIEWPORT_DIMENSION_C_CONFIG__VIEWPORT_WIDTH_C_FE0__SHIFT                                    0x0
#define VPCDC_FE0_VIEWPORT_DIMENSION_C_CONFIG__VIEWPORT_HEIGHT_C_FE0__SHIFT                                   0x10
#define VPCDC_FE0_VIEWPORT_DIMENSION_C_CONFIG__VIEWPORT_WIDTH_C_FE0_MASK                                      0x00003FFFL
#define VPCDC_FE0_VIEWPORT_DIMENSION_C_CONFIG__VIEWPORT_HEIGHT_C_FE0_MASK                                     0x3FFF0000L
//VPCDC_BE0_P2B_CONFIG
#define VPCDC_BE0_P2B_CONFIG__VPCDC_BE0_P2B_XBAR_SEL0__SHIFT                                                  0x0
#define VPCDC_BE0_P2B_CONFIG__VPCDC_BE0_P2B_XBAR_SEL1__SHIFT                                                  0x2
#define VPCDC_BE0_P2B_CONFIG__VPCDC_BE0_P2B_XBAR_SEL2__SHIFT                                                  0x4
#define VPCDC_BE0_P2B_CONFIG__VPCDC_BE0_P2B_XBAR_SEL3__SHIFT                                                  0x6
#define VPCDC_BE0_P2B_CONFIG__VPCDC_BE0_P2B_FORMAT_SEL__SHIFT                                                 0x8
#define VPCDC_BE0_P2B_CONFIG__VPCDC_BE0_P2B_XBAR_SEL0_MASK                                                    0x00000003L
#define VPCDC_BE0_P2B_CONFIG__VPCDC_BE0_P2B_XBAR_SEL1_MASK                                                    0x0000000CL
#define VPCDC_BE0_P2B_CONFIG__VPCDC_BE0_P2B_XBAR_SEL2_MASK                                                    0x00000030L
#define VPCDC_BE0_P2B_CONFIG__VPCDC_BE0_P2B_XBAR_SEL3_MASK                                                    0x000000C0L
#define VPCDC_BE0_P2B_CONFIG__VPCDC_BE0_P2B_FORMAT_SEL_MASK                                                   0x00000300L
//VPCDC_BE0_GLOBAL_SYNC_CONFIG
#define VPCDC_BE0_GLOBAL_SYNC_CONFIG__BE0_VUPDATE_OFFSET__SHIFT                                               0x0
#define VPCDC_BE0_GLOBAL_SYNC_CONFIG__BE0_VUPDATE_WIDTH__SHIFT                                                0xa
#define VPCDC_BE0_GLOBAL_SYNC_CONFIG__BE0_VREADY_OFFSET__SHIFT                                                0x14
#define VPCDC_BE0_GLOBAL_SYNC_CONFIG__BE0_VUPDATE_OFFSET_MASK                                                 0x000003FFL
#define VPCDC_BE0_GLOBAL_SYNC_CONFIG__BE0_VUPDATE_WIDTH_MASK                                                  0x000FFC00L
#define VPCDC_BE0_GLOBAL_SYNC_CONFIG__BE0_VREADY_OFFSET_MASK                                                  0x3FF00000L
//VPCDC_GLOBAL_SYNC_TRIGGER
#define VPCDC_GLOBAL_SYNC_TRIGGER__VPBE_GS_TRIG__SHIFT                                                        0x0
#define VPCDC_GLOBAL_SYNC_TRIGGER__VPBE_GS_TRIG_MASK                                                          0x00000003L
//VPCDC_VREADY_STATUS
#define VPCDC_VREADY_STATUS__VPFE_VR_STATUS__SHIFT                                                            0x0
#define VPCDC_VREADY_STATUS__VPFE_VR_STATUS_MASK                                                              0x00000003L
//VPEP_MEM_GLOBAL_PWR_REQ_CNTL
#define VPEP_MEM_GLOBAL_PWR_REQ_CNTL__MEM_GLOBAL_PWR_REQ_DIS__SHIFT                                           0x0
#define VPEP_MEM_GLOBAL_PWR_REQ_CNTL__MEM_GLOBAL_PWR_REQ_DIS_MASK                                             0x00000001L
//VPFE_MEM_PWR_CNTL
#define VPFE_MEM_PWR_CNTL__VPFE0_MEM_PWR_FORCE__SHIFT                                                         0x0
#define VPFE_MEM_PWR_CNTL__VPFE0_MEM_PWR_MODE__SHIFT                                                          0x2
#define VPFE_MEM_PWR_CNTL__VPFE0_MEM_PWR_STATE__SHIFT                                                         0x4
#define VPFE_MEM_PWR_CNTL__VPFE0_MEM_PWR_DIS__SHIFT                                                           0x6
#define VPFE_MEM_PWR_CNTL__VPFE0_MEM_PWR_FORCE_MASK                                                           0x00000003L
#define VPFE_MEM_PWR_CNTL__VPFE0_MEM_PWR_MODE_MASK                                                            0x0000000CL
#define VPFE_MEM_PWR_CNTL__VPFE0_MEM_PWR_STATE_MASK                                                           0x00000030L
#define VPFE_MEM_PWR_CNTL__VPFE0_MEM_PWR_DIS_MASK                                                             0x00000040L
//VPBE_MEM_PWR_CNTL
#define VPBE_MEM_PWR_CNTL__VPBE0_MEM_PWR_FORCE__SHIFT                                                         0x0
#define VPBE_MEM_PWR_CNTL__VPBE0_MEM_PWR_MODE__SHIFT                                                          0x2
#define VPBE_MEM_PWR_CNTL__VPBE0_MEM_PWR_STATE__SHIFT                                                         0x4
#define VPBE_MEM_PWR_CNTL__VPBE0_MEM_PWR_DIS__SHIFT                                                           0x6
#define VPBE_MEM_PWR_CNTL__VPBE0_MEM_PWR_FORCE_MASK                                                           0x00000003L
#define VPBE_MEM_PWR_CNTL__VPBE0_MEM_PWR_MODE_MASK                                                            0x0000000CL
#define VPBE_MEM_PWR_CNTL__VPBE0_MEM_PWR_STATE_MASK                                                           0x00000030L
#define VPBE_MEM_PWR_CNTL__VPBE0_MEM_PWR_DIS_MASK                                                             0x00000040L
//VPEP_RBBMIF_TIMEOUT
#define VPEP_RBBMIF_TIMEOUT__RBBMIF_TIMEOUT_DELAY__SHIFT                                                      0x0
#define VPEP_RBBMIF_TIMEOUT__RBBMIF_TIMEOUT_HOLD__SHIFT                                                       0x14
#define VPEP_RBBMIF_TIMEOUT__RBBMIF_TIMEOUT_DELAY_MASK                                                        0x000FFFFFL
#define VPEP_RBBMIF_TIMEOUT__RBBMIF_TIMEOUT_HOLD_MASK                                                         0xFFF00000L
//VPEP_RBBMIF_STATUS
#define VPEP_RBBMIF_STATUS__RBBMIF_TIMEOUT_CLIENTS_DEC__SHIFT                                                 0x0
#define VPEP_RBBMIF_STATUS__RBBMIF_TIMEOUT_OP__SHIFT                                                          0x1c
#define VPEP_RBBMIF_STATUS__RBBMIF_TIMEOUT_RDWR_STATUS__SHIFT                                                 0x1d
#define VPEP_RBBMIF_STATUS__RBBMIF_TIMEOUT_ACK__SHIFT                                                         0x1e
#define VPEP_RBBMIF_STATUS__RBBMIF_TIMEOUT_MASK__SHIFT                                                        0x1f
#define VPEP_RBBMIF_STATUS__RBBMIF_TIMEOUT_CLIENTS_DEC_MASK                                                   0x0000000FL
#define VPEP_RBBMIF_STATUS__RBBMIF_TIMEOUT_OP_MASK                                                            0x10000000L
#define VPEP_RBBMIF_STATUS__RBBMIF_TIMEOUT_RDWR_STATUS_MASK                                                   0x20000000L
#define VPEP_RBBMIF_STATUS__RBBMIF_TIMEOUT_ACK_MASK                                                           0x40000000L
#define VPEP_RBBMIF_STATUS__RBBMIF_TIMEOUT_MASK_MASK                                                          0x80000000L
//VPEP_RBBMIF_TIMEOUT_DIS
#define VPEP_RBBMIF_TIMEOUT_DIS__CLIENT0_TIMEOUT_DIS__SHIFT                                                   0x0
#define VPEP_RBBMIF_TIMEOUT_DIS__CLIENT1_TIMEOUT_DIS__SHIFT                                                   0x1
#define VPEP_RBBMIF_TIMEOUT_DIS__CLIENT2_TIMEOUT_DIS__SHIFT                                                   0x2
#define VPEP_RBBMIF_TIMEOUT_DIS__CLIENT3_TIMEOUT_DIS__SHIFT                                                   0x3
#define VPEP_RBBMIF_TIMEOUT_DIS__CLIENT0_TIMEOUT_DIS_MASK                                                     0x00000001L
#define VPEP_RBBMIF_TIMEOUT_DIS__CLIENT1_TIMEOUT_DIS_MASK                                                     0x00000002L
#define VPEP_RBBMIF_TIMEOUT_DIS__CLIENT2_TIMEOUT_DIS_MASK                                                     0x00000004L
#define VPEP_RBBMIF_TIMEOUT_DIS__CLIENT3_TIMEOUT_DIS_MASK                                                     0x00000008L


// addressBlock: vpe_vpep_vpcdc_vpcdc_dcperfmon_dc_perfmon_dispdec
//PERFCOUNTER_CNTL
#define PERFCOUNTER_CNTL__PERFCOUNTER_EVENT_SEL__SHIFT                                                        0x0
#define PERFCOUNTER_CNTL__PERFCOUNTER_CVALUE_SEL__SHIFT                                                       0x9
#define PERFCOUNTER_CNTL__PERFCOUNTER_INC_MODE__SHIFT                                                         0xc
#define PERFCOUNTER_CNTL__PERFCOUNTER_HW_CNTL_SEL__SHIFT                                                      0xf
#define PERFCOUNTER_CNTL__PERFCOUNTER_RUNEN_MODE__SHIFT                                                       0x10
#define PERFCOUNTER_CNTL__PERFCOUNTER_CNTOFF_START_DIS__SHIFT                                                 0x16
#define PERFCOUNTER_CNTL__PERFCOUNTER_RESTART_EN__SHIFT                                                       0x17
#define PERFCOUNTER_CNTL__PERFCOUNTER_INT_EN__SHIFT                                                           0x18
#define PERFCOUNTER_CNTL__PERFCOUNTER_OFF_MASK__SHIFT                                                         0x19
#define PERFCOUNTER_CNTL__PERFCOUNTER_ACTIVE__SHIFT                                                           0x1a
#define PERFCOUNTER_CNTL__PERFCOUNTER_CNTL_SEL__SHIFT                                                         0x1d
#define PERFCOUNTER_CNTL__PERFCOUNTER_EVENT_SEL_MASK                                                          0x000001FFL
#define PERFCOUNTER_CNTL__PERFCOUNTER_CVALUE_SEL_MASK                                                         0x00000E00L
#define PERFCOUNTER_CNTL__PERFCOUNTER_INC_MODE_MASK                                                           0x00007000L
#define PERFCOUNTER_CNTL__PERFCOUNTER_HW_CNTL_SEL_MASK                                                        0x00008000L
#define PERFCOUNTER_CNTL__PERFCOUNTER_RUNEN_MODE_MASK                                                         0x00010000L
#define PERFCOUNTER_CNTL__PERFCOUNTER_CNTOFF_START_DIS_MASK                                                   0x00400000L
#define PERFCOUNTER_CNTL__PERFCOUNTER_RESTART_EN_MASK                                                         0x00800000L
#define PERFCOUNTER_CNTL__PERFCOUNTER_INT_EN_MASK                                                             0x01000000L
#define PERFCOUNTER_CNTL__PERFCOUNTER_OFF_MASK_MASK                                                           0x02000000L
#define PERFCOUNTER_CNTL__PERFCOUNTER_ACTIVE_MASK                                                             0x04000000L
#define PERFCOUNTER_CNTL__PERFCOUNTER_CNTL_SEL_MASK                                                           0xE0000000L
//PERFCOUNTER_CNTL2
#define PERFCOUNTER_CNTL2__PERFCOUNTER_COUNTED_VALUE_TYPE__SHIFT                                              0x0
#define PERFCOUNTER_CNTL2__PERFCOUNTER_HW_STOP1_SEL__SHIFT                                                    0x2
#define PERFCOUNTER_CNTL2__PERFCOUNTER_HW_STOP2_SEL__SHIFT                                                    0x3
#define PERFCOUNTER_CNTL2__PERFCOUNTER_CNTOFF_SEL__SHIFT                                                      0x8
#define PERFCOUNTER_CNTL2__PERFCOUNTER_CNTL2_SEL__SHIFT                                                       0x1d
#define PERFCOUNTER_CNTL2__PERFCOUNTER_COUNTED_VALUE_TYPE_MASK                                                0x00000003L
#define PERFCOUNTER_CNTL2__PERFCOUNTER_HW_STOP1_SEL_MASK                                                      0x00000004L
#define PERFCOUNTER_CNTL2__PERFCOUNTER_HW_STOP2_SEL_MASK                                                      0x00000008L
#define PERFCOUNTER_CNTL2__PERFCOUNTER_CNTOFF_SEL_MASK                                                        0x00003F00L
#define PERFCOUNTER_CNTL2__PERFCOUNTER_CNTL2_SEL_MASK                                                         0xE0000000L
//PERFCOUNTER_STATE
#define PERFCOUNTER_STATE__PERFCOUNTER_CNT0_STATE__SHIFT                                                      0x0
#define PERFCOUNTER_STATE__PERFCOUNTER_STATE_SEL0__SHIFT                                                      0x2
#define PERFCOUNTER_STATE__PERFCOUNTER_CNT1_STATE__SHIFT                                                      0x4
#define PERFCOUNTER_STATE__PERFCOUNTER_STATE_SEL1__SHIFT                                                      0x6
#define PERFCOUNTER_STATE__PERFCOUNTER_CNT2_STATE__SHIFT                                                      0x8
#define PERFCOUNTER_STATE__PERFCOUNTER_STATE_SEL2__SHIFT                                                      0xa
#define PERFCOUNTER_STATE__PERFCOUNTER_CNT3_STATE__SHIFT                                                      0xc
#define PERFCOUNTER_STATE__PERFCOUNTER_STATE_SEL3__SHIFT                                                      0xe
#define PERFCOUNTER_STATE__PERFCOUNTER_CNT4_STATE__SHIFT                                                      0x10
#define PERFCOUNTER_STATE__PERFCOUNTER_STATE_SEL4__SHIFT                                                      0x12
#define PERFCOUNTER_STATE__PERFCOUNTER_CNT5_STATE__SHIFT                                                      0x14
#define PERFCOUNTER_STATE__PERFCOUNTER_STATE_SEL5__SHIFT                                                      0x16
#define PERFCOUNTER_STATE__PERFCOUNTER_CNT6_STATE__SHIFT                                                      0x18
#define PERFCOUNTER_STATE__PERFCOUNTER_STATE_SEL6__SHIFT                                                      0x1a
#define PERFCOUNTER_STATE__PERFCOUNTER_CNT7_STATE__SHIFT                                                      0x1c
#define PERFCOUNTER_STATE__PERFCOUNTER_STATE_SEL7__SHIFT                                                      0x1e
#define PERFCOUNTER_STATE__PERFCOUNTER_CNT0_STATE_MASK                                                        0x00000003L
#define PERFCOUNTER_STATE__PERFCOUNTER_STATE_SEL0_MASK                                                        0x00000004L
#define PERFCOUNTER_STATE__PERFCOUNTER_CNT1_STATE_MASK                                                        0x00000030L
#define PERFCOUNTER_STATE__PERFCOUNTER_STATE_SEL1_MASK                                                        0x00000040L
#define PERFCOUNTER_STATE__PERFCOUNTER_CNT2_STATE_MASK                                                        0x00000300L
#define PERFCOUNTER_STATE__PERFCOUNTER_STATE_SEL2_MASK                                                        0x00000400L
#define PERFCOUNTER_STATE__PERFCOUNTER_CNT3_STATE_MASK                                                        0x00003000L
#define PERFCOUNTER_STATE__PERFCOUNTER_STATE_SEL3_MASK                                                        0x00004000L
#define PERFCOUNTER_STATE__PERFCOUNTER_CNT4_STATE_MASK                                                        0x00030000L
#define PERFCOUNTER_STATE__PERFCOUNTER_STATE_SEL4_MASK                                                        0x00040000L
#define PERFCOUNTER_STATE__PERFCOUNTER_CNT5_STATE_MASK                                                        0x00300000L
#define PERFCOUNTER_STATE__PERFCOUNTER_STATE_SEL5_MASK                                                        0x00400000L
#define PERFCOUNTER_STATE__PERFCOUNTER_CNT6_STATE_MASK                                                        0x03000000L
#define PERFCOUNTER_STATE__PERFCOUNTER_STATE_SEL6_MASK                                                        0x04000000L
#define PERFCOUNTER_STATE__PERFCOUNTER_CNT7_STATE_MASK                                                        0x30000000L
#define PERFCOUNTER_STATE__PERFCOUNTER_STATE_SEL7_MASK                                                        0x40000000L
//PERFMON_CNTL
#define PERFMON_CNTL__PERFMON_STATE__SHIFT                                                                    0x0
#define PERFMON_CNTL__PERFMON_RPT_COUNT__SHIFT                                                                0x8
#define PERFMON_CNTL__PERFMON_CNTOFF_AND_OR__SHIFT                                                            0x1c
#define PERFMON_CNTL__PERFMON_CNTOFF_INT_EN__SHIFT                                                            0x1d
#define PERFMON_CNTL__PERFMON_CNTOFF_INT_STATUS__SHIFT                                                        0x1e
#define PERFMON_CNTL__PERFMON_CNTOFF_INT_ACK__SHIFT                                                           0x1f
#define PERFMON_CNTL__PERFMON_STATE_MASK                                                                      0x00000003L
#define PERFMON_CNTL__PERFMON_RPT_COUNT_MASK                                                                  0x0FFFFF00L
#define PERFMON_CNTL__PERFMON_CNTOFF_AND_OR_MASK                                                              0x10000000L
#define PERFMON_CNTL__PERFMON_CNTOFF_INT_EN_MASK                                                              0x20000000L
#define PERFMON_CNTL__PERFMON_CNTOFF_INT_STATUS_MASK                                                          0x40000000L
#define PERFMON_CNTL__PERFMON_CNTOFF_INT_ACK_MASK                                                             0x80000000L
//PERFMON_CNTL2
#define PERFMON_CNTL2__PERFMON_CNTOFF_INT_TYPE__SHIFT                                                         0x0
#define PERFMON_CNTL2__PERFMON_CLK_ENABLE__SHIFT                                                              0x1
#define PERFMON_CNTL2__PERFMON_RUN_ENABLE_START_SEL__SHIFT                                                    0x2
#define PERFMON_CNTL2__PERFMON_RUN_ENABLE_STOP_SEL__SHIFT                                                     0xa
#define PERFMON_CNTL2__PERFMON_CNTOFF_INT_TYPE_MASK                                                           0x00000001L
#define PERFMON_CNTL2__PERFMON_CLK_ENABLE_MASK                                                                0x00000002L
#define PERFMON_CNTL2__PERFMON_RUN_ENABLE_START_SEL_MASK                                                      0x000003FCL
#define PERFMON_CNTL2__PERFMON_RUN_ENABLE_STOP_SEL_MASK                                                       0x0003FC00L
//PERFMON_CVALUE_INT_MISC
#define PERFMON_CVALUE_INT_MISC__PERFCOUNTER_INT0_STATUS__SHIFT                                               0x0
#define PERFMON_CVALUE_INT_MISC__PERFCOUNTER_INT1_STATUS__SHIFT                                               0x1
#define PERFMON_CVALUE_INT_MISC__PERFCOUNTER_INT2_STATUS__SHIFT                                               0x2
#define PERFMON_CVALUE_INT_MISC__PERFCOUNTER_INT3_STATUS__SHIFT                                               0x3
#define PERFMON_CVALUE_INT_MISC__PERFCOUNTER_INT4_STATUS__SHIFT                                               0x4
#define PERFMON_CVALUE_INT_MISC__PERFCOUNTER_INT5_STATUS__SHIFT                                               0x5
#define PERFMON_CVALUE_INT_MISC__PERFCOUNTER_INT6_STATUS__SHIFT                                               0x6
#define PERFMON_CVALUE_INT_MISC__PERFCOUNTER_INT7_STATUS__SHIFT                                               0x7
#define PERFMON_CVALUE_INT_MISC__PERFCOUNTER_INT0_ACK__SHIFT                                                  0x8
#define PERFMON_CVALUE_INT_MISC__PERFCOUNTER_INT1_ACK__SHIFT                                                  0x9
#define PERFMON_CVALUE_INT_MISC__PERFCOUNTER_INT2_ACK__SHIFT                                                  0xa
#define PERFMON_CVALUE_INT_MISC__PERFCOUNTER_INT3_ACK__SHIFT                                                  0xb
#define PERFMON_CVALUE_INT_MISC__PERFCOUNTER_INT4_ACK__SHIFT                                                  0xc
#define PERFMON_CVALUE_INT_MISC__PERFCOUNTER_INT5_ACK__SHIFT                                                  0xd
#define PERFMON_CVALUE_INT_MISC__PERFCOUNTER_INT6_ACK__SHIFT                                                  0xe
#define PERFMON_CVALUE_INT_MISC__PERFCOUNTER_INT7_ACK__SHIFT                                                  0xf
#define PERFMON_CVALUE_INT_MISC__PERFMON_CVALUE_HI__SHIFT                                                     0x10
#define PERFMON_CVALUE_INT_MISC__PERFCOUNTER_INT0_STATUS_MASK                                                 0x00000001L
#define PERFMON_CVALUE_INT_MISC__PERFCOUNTER_INT1_STATUS_MASK                                                 0x00000002L
#define PERFMON_CVALUE_INT_MISC__PERFCOUNTER_INT2_STATUS_MASK                                                 0x00000004L
#define PERFMON_CVALUE_INT_MISC__PERFCOUNTER_INT3_STATUS_MASK                                                 0x00000008L
#define PERFMON_CVALUE_INT_MISC__PERFCOUNTER_INT4_STATUS_MASK                                                 0x00000010L
#define PERFMON_CVALUE_INT_MISC__PERFCOUNTER_INT5_STATUS_MASK                                                 0x00000020L
#define PERFMON_CVALUE_INT_MISC__PERFCOUNTER_INT6_STATUS_MASK                                                 0x00000040L
#define PERFMON_CVALUE_INT_MISC__PERFCOUNTER_INT7_STATUS_MASK                                                 0x00000080L
#define PERFMON_CVALUE_INT_MISC__PERFCOUNTER_INT0_ACK_MASK                                                    0x00000100L
#define PERFMON_CVALUE_INT_MISC__PERFCOUNTER_INT1_ACK_MASK                                                    0x00000200L
#define PERFMON_CVALUE_INT_MISC__PERFCOUNTER_INT2_ACK_MASK                                                    0x00000400L
#define PERFMON_CVALUE_INT_MISC__PERFCOUNTER_INT3_ACK_MASK                                                    0x00000800L
#define PERFMON_CVALUE_INT_MISC__PERFCOUNTER_INT4_ACK_MASK                                                    0x00001000L
#define PERFMON_CVALUE_INT_MISC__PERFCOUNTER_INT5_ACK_MASK                                                    0x00002000L
#define PERFMON_CVALUE_INT_MISC__PERFCOUNTER_INT6_ACK_MASK                                                    0x00004000L
#define PERFMON_CVALUE_INT_MISC__PERFCOUNTER_INT7_ACK_MASK                                                    0x00008000L
#define PERFMON_CVALUE_INT_MISC__PERFMON_CVALUE_HI_MASK                                                       0xFFFF0000L
//PERFMON_CVALUE_LOW
#define PERFMON_CVALUE_LOW__PERFMON_CVALUE_LOW__SHIFT                                                         0x0
#define PERFMON_CVALUE_LOW__PERFMON_CVALUE_LOW_MASK                                                           0xFFFFFFFFL
//PERFMON_HI
#define PERFMON_HI__PERFMON_HI__SHIFT                                                                         0x0
#define PERFMON_HI__PERFMON_READ_SEL__SHIFT                                                                   0x1d
#define PERFMON_HI__PERFMON_HI_MASK                                                                           0x0000FFFFL
#define PERFMON_HI__PERFMON_READ_SEL_MASK                                                                     0xE0000000L
//PERFMON_LOW
#define PERFMON_LOW__PERFMON_LOW__SHIFT                                                                       0x0
#define PERFMON_LOW__PERFMON_LOW_MASK                                                                         0xFFFFFFFFL

#endif
