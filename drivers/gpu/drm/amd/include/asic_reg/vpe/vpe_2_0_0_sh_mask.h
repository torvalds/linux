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
 */

#ifndef _vpe_2_0_0_SH_MASK_HEADER
#define _vpe_2_0_0_SH_MASK_HEADER


// addressBlock: vpe_vpec_vpecdec
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
//VPEC_MMHUB_CNTL
#define VPEC_MMHUB_CNTL__UNIT_ID__SHIFT                                                                       0x0
#define VPEC_MMHUB_CNTL__UNIT_ID_MASK                                                                         0x0000003FL
//VPEC_MMHUB_TRUSTLVL
#define VPEC_MMHUB_TRUSTLVL__SECLVL0__SHIFT                                                                   0x0
#define VPEC_MMHUB_TRUSTLVL__SECLVL1__SHIFT                                                                   0x4
#define VPEC_MMHUB_TRUSTLVL__SECLVL2__SHIFT                                                                   0x8
#define VPEC_MMHUB_TRUSTLVL__SECLVL3__SHIFT                                                                   0xc
#define VPEC_MMHUB_TRUSTLVL__SECLVL4__SHIFT                                                                   0x10
#define VPEC_MMHUB_TRUSTLVL__SECLVL5__SHIFT                                                                   0x14
#define VPEC_MMHUB_TRUSTLVL__SECLVL6__SHIFT                                                                   0x18
#define VPEC_MMHUB_TRUSTLVL__SECLVL7__SHIFT                                                                   0x1c
#define VPEC_MMHUB_TRUSTLVL__SECLVL0_MASK                                                                     0x0000000FL
#define VPEC_MMHUB_TRUSTLVL__SECLVL1_MASK                                                                     0x000000F0L
#define VPEC_MMHUB_TRUSTLVL__SECLVL2_MASK                                                                     0x00000F00L
#define VPEC_MMHUB_TRUSTLVL__SECLVL3_MASK                                                                     0x0000F000L
#define VPEC_MMHUB_TRUSTLVL__SECLVL4_MASK                                                                     0x000F0000L
#define VPEC_MMHUB_TRUSTLVL__SECLVL5_MASK                                                                     0x00F00000L
#define VPEC_MMHUB_TRUSTLVL__SECLVL6_MASK                                                                     0x0F000000L
#define VPEC_MMHUB_TRUSTLVL__SECLVL7_MASK                                                                     0xF0000000L
//VPEC_VPEP_CTRL
#define VPEC_VPEP_CTRL__VPEP_SOCCLK_EN__SHIFT                                                                 0x0
#define VPEC_VPEP_CTRL__VPEP_SW_RESETB__SHIFT                                                                 0x1
#define VPEC_VPEP_CTRL__RESERVED__SHIFT                                                                       0x2
#define VPEC_VPEP_CTRL__SOFT_OVERRIDE_FGCLKEN_S0P0__SHIFT                                                     0x16
#define VPEC_VPEP_CTRL__SOFT_OVERRIDE_FGCLKEN_S0P1__SHIFT                                                     0x17
#define VPEC_VPEP_CTRL__SOFT_OVERRIDE_FGCLKEN_S0P2__SHIFT                                                     0x18
#define VPEC_VPEP_CTRL__SOFT_OVERRIDE_FGCLKEN_S1P0__SHIFT                                                     0x19
#define VPEC_VPEP_CTRL__SOFT_OVERRIDE_FGCLKEN_S1P1__SHIFT                                                     0x1a
#define VPEC_VPEP_CTRL__SOFT_OVERRIDE_FGCLKEN_S1P2__SHIFT                                                     0x1b
#define VPEC_VPEP_CTRL__SOFT_OVERRIDE_FGCLKEN_3DLUT__SHIFT                                                    0x1c
#define VPEC_VPEP_CTRL__SOFT_OVERRIDE_VPEC_VPEP_REG_FGCLKEN__SHIFT                                            0x1d
#define VPEC_VPEP_CTRL__SOFT_OVERRIDE_VPEP_SOCCLK__SHIFT                                                      0x1e
#define VPEC_VPEP_CTRL__SOFT_OVERRIDE_VPECLK__SHIFT                                                           0x1f
#define VPEC_VPEP_CTRL__VPEP_SOCCLK_EN_MASK                                                                   0x00000001L
#define VPEC_VPEP_CTRL__VPEP_SW_RESETB_MASK                                                                   0x00000002L
#define VPEC_VPEP_CTRL__RESERVED_MASK                                                                         0x003FFFFCL
#define VPEC_VPEP_CTRL__SOFT_OVERRIDE_FGCLKEN_S0P0_MASK                                                       0x00400000L
#define VPEC_VPEP_CTRL__SOFT_OVERRIDE_FGCLKEN_S0P1_MASK                                                       0x00800000L
#define VPEC_VPEP_CTRL__SOFT_OVERRIDE_FGCLKEN_S0P2_MASK                                                       0x01000000L
#define VPEC_VPEP_CTRL__SOFT_OVERRIDE_FGCLKEN_S1P0_MASK                                                       0x02000000L
#define VPEC_VPEP_CTRL__SOFT_OVERRIDE_FGCLKEN_S1P1_MASK                                                       0x04000000L
#define VPEC_VPEP_CTRL__SOFT_OVERRIDE_FGCLKEN_S1P2_MASK                                                       0x08000000L
#define VPEC_VPEP_CTRL__SOFT_OVERRIDE_FGCLKEN_3DLUT_MASK                                                      0x10000000L
#define VPEC_VPEP_CTRL__SOFT_OVERRIDE_VPEC_VPEP_REG_FGCLKEN_MASK                                              0x20000000L
#define VPEC_VPEP_CTRL__SOFT_OVERRIDE_VPEP_SOCCLK_MASK                                                        0x40000000L
#define VPEC_VPEP_CTRL__SOFT_OVERRIDE_VPECLK_MASK                                                             0x80000000L
//VPEC_CLK_CTRL
#define VPEC_CLK_CTRL__VPECLK_EN__SHIFT                                                                       0x1
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_IP_PIPE0_CLK__SHIFT                                                      0x8
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_IP_PIPE1_CLK__SHIFT                                                      0x9
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_IP_PIPE2_CLK__SHIFT                                                      0xa
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_IP_PIPE3_CLK__SHIFT                                                      0xb
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_IP_PIPE4_CLK__SHIFT                                                      0xc
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_IP_PIPE5_CLK__SHIFT                                                      0xd
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_OP_PIPE0_CLK__SHIFT                                                      0x10
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_OP_PIPE1_CLK__SHIFT                                                      0x11
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_OP_PIPE2_CLK__SHIFT                                                      0x12
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_OP_PIPE3_CLK__SHIFT                                                      0x13
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_OP_PIPE4_CLK__SHIFT                                                      0x14
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_OP_PIPE5_CLK__SHIFT                                                      0x15
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_OP_PIPE6_CLK__SHIFT                                                      0x16
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_OP_PIPE7_CLK__SHIFT                                                      0x17
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_OP_PIPE8_CLK__SHIFT                                                      0x18
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_OP_PIPE9_CLK__SHIFT                                                      0x19
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_PERF_CLK__SHIFT                                                          0x1b
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_CE_CLK__SHIFT                                                            0x1c
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_F32_CLK__SHIFT                                                           0x1d
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_DYN_CLK__SHIFT                                                           0x1e
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_REG_CLK__SHIFT                                                           0x1f
#define VPEC_CLK_CTRL__VPECLK_EN_MASK                                                                         0x00000002L
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_IP_PIPE0_CLK_MASK                                                        0x00000100L
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_IP_PIPE1_CLK_MASK                                                        0x00000200L
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_IP_PIPE2_CLK_MASK                                                        0x00000400L
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_IP_PIPE3_CLK_MASK                                                        0x00000800L
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_IP_PIPE4_CLK_MASK                                                        0x00001000L
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_IP_PIPE5_CLK_MASK                                                        0x00002000L
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_OP_PIPE0_CLK_MASK                                                        0x00010000L
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_OP_PIPE1_CLK_MASK                                                        0x00020000L
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_OP_PIPE2_CLK_MASK                                                        0x00040000L
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_OP_PIPE3_CLK_MASK                                                        0x00080000L
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_OP_PIPE4_CLK_MASK                                                        0x00100000L
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_OP_PIPE5_CLK_MASK                                                        0x00200000L
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_OP_PIPE6_CLK_MASK                                                        0x00400000L
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_OP_PIPE7_CLK_MASK                                                        0x00800000L
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_OP_PIPE8_CLK_MASK                                                        0x01000000L
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_OP_PIPE9_CLK_MASK                                                        0x02000000L
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_PERF_CLK_MASK                                                            0x08000000L
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_CE_CLK_MASK                                                              0x10000000L
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_F32_CLK_MASK                                                             0x20000000L
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_DYN_CLK_MASK                                                             0x40000000L
#define VPEC_CLK_CTRL__SOFT_OVERRIDE_REG_CLK_MASK                                                             0x80000000L
//VPEC_COLLABORATE_CNTL
#define VPEC_COLLABORATE_CNTL__COLLABORATE_MODE_EN__SHIFT                                                     0x0
#define VPEC_COLLABORATE_CNTL__COLLABORATE_MODE_EN_MASK                                                       0x00000001L
//VPEC_COLLABORATE_CFG
#define VPEC_COLLABORATE_CFG__MASTER_ID__SHIFT                                                                0x0
#define VPEC_COLLABORATE_CFG__MASTER_EN__SHIFT                                                                0x3
#define VPEC_COLLABORATE_CFG__SLAVE0_ID__SHIFT                                                                0x4
#define VPEC_COLLABORATE_CFG__SLAVE0_EN__SHIFT                                                                0x7
#define VPEC_COLLABORATE_CFG__MASTER_ID_MASK                                                                  0x00000007L
#define VPEC_COLLABORATE_CFG__MASTER_EN_MASK                                                                  0x00000008L
#define VPEC_COLLABORATE_CFG__SLAVE0_ID_MASK                                                                  0x00000070L
#define VPEC_COLLABORATE_CFG__SLAVE0_EN_MASK                                                                  0x00000080L
//VPEC_POWER_CNTL
#define VPEC_POWER_CNTL__LS_ENABLE__SHIFT                                                                     0x0
#define VPEC_POWER_CNTL__UCODE_SRAM_DS_EN__SHIFT                                                              0x1
#define VPEC_POWER_CNTL__FISO__SHIFT                                                                          0x2
#define VPEC_POWER_CNTL__UCODE_SRAM_POWER_UP_RECOVER_DELAY__SHIFT                                             0x8
#define VPEC_POWER_CNTL__UCODE_SRAM_POWER_STATUS_CHANGE_WAKEUP_TIME__SHIFT                                    0xf
#define VPEC_POWER_CNTL__UCODE_SRAM_POWER_STATUS_CHANGE_CLK_FORCE__SHIFT                                      0x12
#define VPEC_POWER_CNTL__SRAM_POWER_LS_CHANGE_DELAY__SHIFT                                                    0x14
#define VPEC_POWER_CNTL__SRAM_POWER_LS_CHANGE_CLK_FORCE__SHIFT                                                0x17
#define VPEC_POWER_CNTL__SRAM_POWER_LS_CHANGE_CLK_FORCE_DELAY__SHIFT                                          0x18
#define VPEC_POWER_CNTL__LS_ENABLE_MASK                                                                       0x00000001L
#define VPEC_POWER_CNTL__UCODE_SRAM_DS_EN_MASK                                                                0x00000002L
#define VPEC_POWER_CNTL__FISO_MASK                                                                            0x00000004L
#define VPEC_POWER_CNTL__UCODE_SRAM_POWER_UP_RECOVER_DELAY_MASK                                               0x00007F00L
#define VPEC_POWER_CNTL__UCODE_SRAM_POWER_STATUS_CHANGE_WAKEUP_TIME_MASK                                      0x00038000L
#define VPEC_POWER_CNTL__UCODE_SRAM_POWER_STATUS_CHANGE_CLK_FORCE_MASK                                        0x00040000L
#define VPEC_POWER_CNTL__SRAM_POWER_LS_CHANGE_DELAY_MASK                                                      0x00700000L
#define VPEC_POWER_CNTL__SRAM_POWER_LS_CHANGE_CLK_FORCE_MASK                                                  0x00800000L
#define VPEC_POWER_CNTL__SRAM_POWER_LS_CHANGE_CLK_FORCE_DELAY_MASK                                            0x03000000L
//VPEC_ZPR_CNTL
#define VPEC_ZPR_CNTL__CLK_UNGATE_DELAY__SHIFT                                                                0x0
#define VPEC_ZPR_CNTL__RESERVED__SHIFT                                                                        0x8
#define VPEC_ZPR_CNTL__CLK_UNGATE_DELAY_MASK                                                                  0x000000FFL
#define VPEC_ZPR_CNTL__RESERVED_MASK                                                                          0xFFFFFF00L
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
#define VPEC_CNTL_DCC__RESERVED_7_6__SHIFT                                                                    0x6
#define VPEC_CNTL_DCC__WDCC_DATA_FORMAT__SHIFT                                                                0x8
#define VPEC_CNTL_DCC__RESERVED_15_13__SHIFT                                                                  0xd
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
#define VPEC_CNTL_DCC__WDCC_MICRO_TILE_MODE_MASK                                                              0x00000030L
#define VPEC_CNTL_DCC__RESERVED_7_6_MASK                                                                      0x000000C0L
#define VPEC_CNTL_DCC__WDCC_DATA_FORMAT_MASK                                                                  0x00001F00L
#define VPEC_CNTL_DCC__RESERVED_15_13_MASK                                                                    0x0000E000L
#define VPEC_CNTL_DCC__WDCC_NUM_FORMAT_EN_MASK                                                                0x00010000L
#define VPEC_CNTL_DCC__RESERVED_19_17_MASK                                                                    0x000E0000L
#define VPEC_CNTL_DCC__WDCC_NUM_TYPE_MASK                                                                     0x00700000L
#define VPEC_CNTL_DCC__RESERVED_23_23_MASK                                                                    0x00800000L
#define VPEC_CNTL_DCC__WDCC_MAX_UNCOMP_SIZE_MASK                                                              0x01000000L
#define VPEC_CNTL_DCC__WDCC_MAX_COMP_SIZE_MASK                                                                0x06000000L
#define VPEC_CNTL_DCC__RESERVED_30_27_MASK                                                                    0x78000000L
#define VPEC_CNTL_DCC__RDCC_COMP_MODE_MASK                                                                    0x80000000L
//VPEC_CE_OP_MULTI_64B_BURST
#define VPEC_CE_OP_MULTI_64B_BURST__EN__SHIFT                                                                 0x0
#define VPEC_CE_OP_MULTI_64B_BURST__RESERVED_3_1__SHIFT                                                       0x1
#define VPEC_CE_OP_MULTI_64B_BURST__LAZY_TIMER_DLY__SHIFT                                                     0x4
#define VPEC_CE_OP_MULTI_64B_BURST__NUM_64B_BURST_ALLOWED__SHIFT                                              0xa
#define VPEC_CE_OP_MULTI_64B_BURST__RESERVED_31_12__SHIFT                                                     0xc
#define VPEC_CE_OP_MULTI_64B_BURST__EN_MASK                                                                   0x00000001L
#define VPEC_CE_OP_MULTI_64B_BURST__RESERVED_3_1_MASK                                                         0x0000000EL
#define VPEC_CE_OP_MULTI_64B_BURST__LAZY_TIMER_DLY_MASK                                                       0x000003F0L
#define VPEC_CE_OP_MULTI_64B_BURST__NUM_64B_BURST_ALLOWED_MASK                                                0x00000C00L
#define VPEC_CE_OP_MULTI_64B_BURST__RESERVED_31_12_MASK                                                       0xFFFFF000L
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
//VPEC_GB_ADDR_CONFIG_META
#define VPEC_GB_ADDR_CONFIG_META__NUM_PIPES__SHIFT                                                            0x0
#define VPEC_GB_ADDR_CONFIG_META__PIPE_INTERLEAVE_SIZE__SHIFT                                                 0x3
#define VPEC_GB_ADDR_CONFIG_META__MAX_COMPRESSED_FRAGS__SHIFT                                                 0x6
#define VPEC_GB_ADDR_CONFIG_META__NUM_PKRS__SHIFT                                                             0x8
#define VPEC_GB_ADDR_CONFIG_META__NUM_SHADER_ENGINES__SHIFT                                                   0x13
#define VPEC_GB_ADDR_CONFIG_META__NUM_RB_PER_SE__SHIFT                                                        0x1a
#define VPEC_GB_ADDR_CONFIG_META__NUM_PIPES_MASK                                                              0x00000007L
#define VPEC_GB_ADDR_CONFIG_META__PIPE_INTERLEAVE_SIZE_MASK                                                   0x00000038L
#define VPEC_GB_ADDR_CONFIG_META__MAX_COMPRESSED_FRAGS_MASK                                                   0x000000C0L
#define VPEC_GB_ADDR_CONFIG_META__NUM_PKRS_MASK                                                               0x00000700L
#define VPEC_GB_ADDR_CONFIG_META__NUM_SHADER_ENGINES_MASK                                                     0x00180000L
#define VPEC_GB_ADDR_CONFIG_META__NUM_RB_PER_SE_MASK                                                          0x0C000000L
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
#define VPEC_RELAX_ORDERING_LUT__NATIVE_FENCE__SHIFT                                                          0xe
#define VPEC_RELAX_ORDERING_LUT__RESERVED__SHIFT                                                              0xf
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
#define VPEC_RELAX_ORDERING_LUT__NATIVE_FENCE_MASK                                                            0x00004000L
#define VPEC_RELAX_ORDERING_LUT__RESERVED_MASK                                                                0x07FF8000L
#define VPEC_RELAX_ORDERING_LUT__WORLD_SWITCH_MASK                                                            0x08000000L
#define VPEC_RELAX_ORDERING_LUT__RPTR_WRB_MASK                                                                0x10000000L
#define VPEC_RELAX_ORDERING_LUT__RESERVED_29_29_MASK                                                          0x20000000L
#define VPEC_RELAX_ORDERING_LUT__IB_FETCH_MASK                                                                0x40000000L
#define VPEC_RELAX_ORDERING_LUT__RB_FETCH_MASK                                                                0x80000000L
//VPEC_CREDIT_CNTL
#define VPEC_CREDIT_CNTL__DRM_CREDIT__SHIFT                                                                   0x0
#define VPEC_CREDIT_CNTL__MC_WRREQ_CREDIT__SHIFT                                                              0x7
#define VPEC_CREDIT_CNTL__MC_RDREQ_CREDIT__SHIFT                                                              0xd
#define VPEC_CREDIT_CNTL__DRM_CREDIT_MASK                                                                     0x0000007FL
#define VPEC_CREDIT_CNTL__MC_WRREQ_CREDIT_MASK                                                                0x00001F80L
#define VPEC_CREDIT_CNTL__MC_RDREQ_CREDIT_MASK                                                                0x0007E000L
//VPEC_SCRATCH_RAM_DATA
#define VPEC_SCRATCH_RAM_DATA__DATA__SHIFT                                                                    0x0
#define VPEC_SCRATCH_RAM_DATA__DATA_MASK                                                                      0xFFFFFFFFL
//VPEC_SCRATCH_RAM_ADDR
#define VPEC_SCRATCH_RAM_ADDR__ADDR__SHIFT                                                                    0x0
#define VPEC_SCRATCH_RAM_ADDR__ADDR_MASK                                                                      0x000000FFL
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
//VPEC_MAILBOX0
#define VPEC_MAILBOX0__VALUE__SHIFT                                                                           0x0
#define VPEC_MAILBOX0__VALUE_MASK                                                                             0xFFFFFFFFL
//VPEC_MAILBOX1
#define VPEC_MAILBOX1__VALUE__SHIFT                                                                           0x0
#define VPEC_MAILBOX1__VALUE_MASK                                                                             0xFFFFFFFFL
//VPEC_MAILBOX2
#define VPEC_MAILBOX2__VALUE__SHIFT                                                                           0x0
#define VPEC_MAILBOX2__VALUE_MASK                                                                             0xFFFFFFFFL
//VPEC_MAILBOX3
#define VPEC_MAILBOX3__VALUE__SHIFT                                                                           0x0
#define VPEC_MAILBOX3__VALUE_MASK                                                                             0xFFFFFFFFL
//VPEC_MAILBOX4
#define VPEC_MAILBOX4__VALUE__SHIFT                                                                           0x0
#define VPEC_MAILBOX4__VALUE_MASK                                                                             0xFFFFFFFFL
//VPEC_MAILBOX5
#define VPEC_MAILBOX5__VALUE__SHIFT                                                                           0x0
#define VPEC_MAILBOX5__VALUE_MASK                                                                             0xFFFFFFFFL
//VPEC_MAILBOX6
#define VPEC_MAILBOX6__VALUE__SHIFT                                                                           0x0
#define VPEC_MAILBOX6__VALUE_MASK                                                                             0xFFFFFFFFL
//VPEC_MAILBOX7
#define VPEC_MAILBOX7__VALUE__SHIFT                                                                           0x0
#define VPEC_MAILBOX7__VALUE_MASK                                                                             0xFFFFFFFFL
//VPEC_MAILBOX8
#define VPEC_MAILBOX8__VALUE__SHIFT                                                                           0x0
#define VPEC_MAILBOX8__VALUE_MASK                                                                             0xFFFFFFFFL
//VPEC_MAILBOX9
#define VPEC_MAILBOX9__VALUE__SHIFT                                                                           0x0
#define VPEC_MAILBOX9__VALUE_MASK                                                                             0xFFFFFFFFL
//VPEC_MAILBOX10
#define VPEC_MAILBOX10__VALUE__SHIFT                                                                          0x0
#define VPEC_MAILBOX10__VALUE_MASK                                                                            0xFFFFFFFFL
//VPEC_MAILBOX11
#define VPEC_MAILBOX11__VALUE__SHIFT                                                                          0x0
#define VPEC_MAILBOX11__VALUE_MASK                                                                            0xFFFFFFFFL
//VPEC_MAILBOX12
#define VPEC_MAILBOX12__VALUE__SHIFT                                                                          0x0
#define VPEC_MAILBOX12__VALUE_MASK                                                                            0xFFFFFFFFL
//VPEC_MAILBOX13
#define VPEC_MAILBOX13__VALUE__SHIFT                                                                          0x0
#define VPEC_MAILBOX13__VALUE_MASK                                                                            0xFFFFFFFFL
//VPEC_MAILBOX14
#define VPEC_MAILBOX14__VALUE__SHIFT                                                                          0x0
#define VPEC_MAILBOX14__VALUE_MASK                                                                            0xFFFFFFFFL
//VPEC_MAILBOX15
#define VPEC_MAILBOX15__VALUE__SHIFT                                                                          0x0
#define VPEC_MAILBOX15__VALUE_MASK                                                                            0xFFFFFFFFL
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
//VPEC_3DLUTIB_OFFSET_FETCH
#define VPEC_3DLUTIB_OFFSET_FETCH__OFFSET__SHIFT                                                              0x2
#define VPEC_3DLUTIB_OFFSET_FETCH__OFFSET_MASK                                                                0x003FFFFCL
//VPEC_ATOMIC_PREOP_LO
#define VPEC_ATOMIC_PREOP_LO__DATA__SHIFT                                                                     0x0
#define VPEC_ATOMIC_PREOP_LO__DATA_MASK                                                                       0xFFFFFFFFL
//VPEC_ATOMIC_PREOP_HI
#define VPEC_ATOMIC_PREOP_HI__DATA__SHIFT                                                                     0x0
#define VPEC_ATOMIC_PREOP_HI__DATA_MASK                                                                       0xFFFFFFFFL
//VPEC_CE_BUSY
#define VPEC_CE_BUSY__CE_IP_PIPE0_BUSY__SHIFT                                                                 0x0
#define VPEC_CE_BUSY__CE_IP_PIPE1_BUSY__SHIFT                                                                 0x1
#define VPEC_CE_BUSY__CE_IP_PIPE2_BUSY__SHIFT                                                                 0x2
#define VPEC_CE_BUSY__CE_IP_PIPE3_BUSY__SHIFT                                                                 0x3
#define VPEC_CE_BUSY__CE_IP_PIPE4_BUSY__SHIFT                                                                 0x4
#define VPEC_CE_BUSY__CE_IP_PIPE5_BUSY__SHIFT                                                                 0x5
#define VPEC_CE_BUSY__CE_OP_PIPE0_BUSY__SHIFT                                                                 0x10
#define VPEC_CE_BUSY__CE_OP_PIPE1_BUSY__SHIFT                                                                 0x11
#define VPEC_CE_BUSY__CE_OP_PIPE2_BUSY__SHIFT                                                                 0x12
#define VPEC_CE_BUSY__CE_OP_PIPE3_BUSY__SHIFT                                                                 0x13
#define VPEC_CE_BUSY__CE_OP_PIPE4_BUSY__SHIFT                                                                 0x14
#define VPEC_CE_BUSY__CE_OP_PIPE5_BUSY__SHIFT                                                                 0x15
#define VPEC_CE_BUSY__CE_OP_PIPE6_BUSY__SHIFT                                                                 0x16
#define VPEC_CE_BUSY__CE_OP_PIPE7_BUSY__SHIFT                                                                 0x17
#define VPEC_CE_BUSY__CE_OP_PIPE8_BUSY__SHIFT                                                                 0x18
#define VPEC_CE_BUSY__CE_OP_PIPE9_BUSY__SHIFT                                                                 0x19
#define VPEC_CE_BUSY__CE_IP_PIPE0_BUSY_MASK                                                                   0x00000001L
#define VPEC_CE_BUSY__CE_IP_PIPE1_BUSY_MASK                                                                   0x00000002L
#define VPEC_CE_BUSY__CE_IP_PIPE2_BUSY_MASK                                                                   0x00000004L
#define VPEC_CE_BUSY__CE_IP_PIPE3_BUSY_MASK                                                                   0x00000008L
#define VPEC_CE_BUSY__CE_IP_PIPE4_BUSY_MASK                                                                   0x00000010L
#define VPEC_CE_BUSY__CE_IP_PIPE5_BUSY_MASK                                                                   0x00000020L
#define VPEC_CE_BUSY__CE_OP_PIPE0_BUSY_MASK                                                                   0x00010000L
#define VPEC_CE_BUSY__CE_OP_PIPE1_BUSY_MASK                                                                   0x00020000L
#define VPEC_CE_BUSY__CE_OP_PIPE2_BUSY_MASK                                                                   0x00040000L
#define VPEC_CE_BUSY__CE_OP_PIPE3_BUSY_MASK                                                                   0x00080000L
#define VPEC_CE_BUSY__CE_OP_PIPE4_BUSY_MASK                                                                   0x00100000L
#define VPEC_CE_BUSY__CE_OP_PIPE5_BUSY_MASK                                                                   0x00200000L
#define VPEC_CE_BUSY__CE_OP_PIPE6_BUSY_MASK                                                                   0x00400000L
#define VPEC_CE_BUSY__CE_OP_PIPE7_BUSY_MASK                                                                   0x00800000L
#define VPEC_CE_BUSY__CE_OP_PIPE8_BUSY_MASK                                                                   0x01000000L
#define VPEC_CE_BUSY__CE_OP_PIPE9_BUSY_MASK                                                                   0x02000000L
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
#define VPEC_ERROR_LOG__OVERRIDE__SHIFT                                                                       0x0
#define VPEC_ERROR_LOG__STATUS__SHIFT                                                                         0x10
#define VPEC_ERROR_LOG__OVERRIDE_MASK                                                                         0x0000FFFFL
#define VPEC_ERROR_LOG__STATUS_MASK                                                                           0xFFFF0000L
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
#define VPEC_STATUS10__RESERVED_31_28_MASK                                                                    0xF0000000L
//VPEC_STATUS_DCC
#define VPEC_STATUS_DCC__CE_IP0_MRQ_IDLE__SHIFT                                                               0x0
#define VPEC_STATUS_DCC__CE_IP0_DCCP_IDLE__SHIFT                                                              0x1
#define VPEC_STATUS_DCC__CE_IP0_DCC_RET_IDLE__SHIFT                                                           0x2
#define VPEC_STATUS_DCC__CE_IP1_MRQ_IDLE__SHIFT                                                               0x3
#define VPEC_STATUS_DCC__CE_IP1_DCCP_IDLE__SHIFT                                                              0x4
#define VPEC_STATUS_DCC__CE_IP1_DCC_RET_IDLE__SHIFT                                                           0x5
#define VPEC_STATUS_DCC__CE_IP2_MRQ_IDLE__SHIFT                                                               0x6
#define VPEC_STATUS_DCC__CE_IP2_DCCP_IDLE__SHIFT                                                              0x7
#define VPEC_STATUS_DCC__CE_IP2_DCC_RET_IDLE__SHIFT                                                           0x8
#define VPEC_STATUS_DCC__CE_IP3_MRQ_IDLE__SHIFT                                                               0x9
#define VPEC_STATUS_DCC__CE_IP3_DCCP_IDLE__SHIFT                                                              0xa
#define VPEC_STATUS_DCC__CE_IP3_DCC_RET_IDLE__SHIFT                                                           0xb
#define VPEC_STATUS_DCC__CE_IP4_MRQ_IDLE__SHIFT                                                               0xc
#define VPEC_STATUS_DCC__CE_IP4_DCCP_IDLE__SHIFT                                                              0xd
#define VPEC_STATUS_DCC__CE_IP4_DCC_RET_IDLE__SHIFT                                                           0xe
#define VPEC_STATUS_DCC__CE_IP5_MRQ_IDLE__SHIFT                                                               0xf
#define VPEC_STATUS_DCC__CE_IP5_DCCP_IDLE__SHIFT                                                              0x10
#define VPEC_STATUS_DCC__CE_IP5_DCC_RET_IDLE__SHIFT                                                           0x11
#define VPEC_STATUS_DCC__RESERVED_31_18__SHIFT                                                                0x12
#define VPEC_STATUS_DCC__CE_IP0_MRQ_IDLE_MASK                                                                 0x00000001L
#define VPEC_STATUS_DCC__CE_IP0_DCCP_IDLE_MASK                                                                0x00000002L
#define VPEC_STATUS_DCC__CE_IP0_DCC_RET_IDLE_MASK                                                             0x00000004L
#define VPEC_STATUS_DCC__CE_IP1_MRQ_IDLE_MASK                                                                 0x00000008L
#define VPEC_STATUS_DCC__CE_IP1_DCCP_IDLE_MASK                                                                0x00000010L
#define VPEC_STATUS_DCC__CE_IP1_DCC_RET_IDLE_MASK                                                             0x00000020L
#define VPEC_STATUS_DCC__CE_IP2_MRQ_IDLE_MASK                                                                 0x00000040L
#define VPEC_STATUS_DCC__CE_IP2_DCCP_IDLE_MASK                                                                0x00000080L
#define VPEC_STATUS_DCC__CE_IP2_DCC_RET_IDLE_MASK                                                             0x00000100L
#define VPEC_STATUS_DCC__CE_IP3_MRQ_IDLE_MASK                                                                 0x00000200L
#define VPEC_STATUS_DCC__CE_IP3_DCCP_IDLE_MASK                                                                0x00000400L
#define VPEC_STATUS_DCC__CE_IP3_DCC_RET_IDLE_MASK                                                             0x00000800L
#define VPEC_STATUS_DCC__CE_IP4_MRQ_IDLE_MASK                                                                 0x00001000L
#define VPEC_STATUS_DCC__CE_IP4_DCCP_IDLE_MASK                                                                0x00002000L
#define VPEC_STATUS_DCC__CE_IP4_DCC_RET_IDLE_MASK                                                             0x00004000L
#define VPEC_STATUS_DCC__CE_IP5_MRQ_IDLE_MASK                                                                 0x00008000L
#define VPEC_STATUS_DCC__CE_IP5_DCCP_IDLE_MASK                                                                0x00010000L
#define VPEC_STATUS_DCC__CE_IP5_DCC_RET_IDLE_MASK                                                             0x00020000L
#define VPEC_STATUS_DCC__RESERVED_31_18_MASK                                                                  0xFFFC0000L
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
//VPEC_INST
#define VPEC_INST__ID__SHIFT                                                                                  0x0
#define VPEC_INST__RESERVED__SHIFT                                                                            0x3
#define VPEC_INST__ID_MASK                                                                                    0x00000007L
#define VPEC_INST__RESERVED_MASK                                                                              0xFFFFFFF8L
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
//VPEC_DPM_IDLE_TIME
#define VPEC_DPM_IDLE_TIME__VALUE__SHIFT                                                                      0x0
#define VPEC_DPM_IDLE_TIME__VALUE_MASK                                                                        0xFFFFFFFFL
//VPEC_DPM_BUSY_TIME
#define VPEC_DPM_BUSY_TIME__VALUE__SHIFT                                                                      0x0
#define VPEC_DPM_BUSY_TIME__VALUE_MASK                                                                        0xFFFFFFFFL
//VPEC_DPM_IDLE_START_LO
#define VPEC_DPM_IDLE_START_LO__VALUE__SHIFT                                                                  0x0
#define VPEC_DPM_IDLE_START_LO__VALUE_MASK                                                                    0xFFFFFFFFL
//VPEC_DPM_IDLE_START_HI
#define VPEC_DPM_IDLE_START_HI__VALUE__SHIFT                                                                  0x0
#define VPEC_DPM_IDLE_START_HI__VALUE_MASK                                                                    0xFFFFFFFFL
//VPEC_DPM_BUSY_START_LO
#define VPEC_DPM_BUSY_START_LO__VALUE__SHIFT                                                                  0x0
#define VPEC_DPM_BUSY_START_LO__VALUE_MASK                                                                    0xFFFFFFFFL
//VPEC_DPM_BUSY_START_HI
#define VPEC_DPM_BUSY_START_HI__VALUE__SHIFT                                                                  0x0
#define VPEC_DPM_BUSY_START_HI__VALUE_MASK                                                                    0xFFFFFFFFL
//VPEC_DPM_LAST_REQ_TIMESTAMP
#define VPEC_DPM_LAST_REQ_TIMESTAMP__VALUE__SHIFT                                                             0x0
#define VPEC_DPM_LAST_REQ_TIMESTAMP__VALUE_MASK                                                               0xFFFFFFFFL
//VPEC_DPM_NEW_JOB_DUMMY3
#define VPEC_DPM_NEW_JOB_DUMMY3__VALUE__SHIFT                                                                 0x0
#define VPEC_DPM_NEW_JOB_DUMMY3__VALUE_MASK                                                                   0xFFFFFFFFL
//VPEC_DPM_STATE
#define VPEC_DPM_STATE__VALUE__SHIFT                                                                          0x0
#define VPEC_DPM_STATE__VALUE_MASK                                                                            0xFFFFFFFFL
//VPEC_DPM0_FREQ
#define VPEC_DPM0_FREQ__VALUE__SHIFT                                                                          0x0
#define VPEC_DPM0_FREQ__VALUE_MASK                                                                            0xFFFFFFFFL
//VPEC_DPM1_FREQ
#define VPEC_DPM1_FREQ__VALUE__SHIFT                                                                          0x0
#define VPEC_DPM1_FREQ__VALUE_MASK                                                                            0xFFFFFFFFL
//VPEC_DPM2_FREQ
#define VPEC_DPM2_FREQ__VALUE__SHIFT                                                                          0x0
#define VPEC_DPM2_FREQ__VALUE_MASK                                                                            0xFFFFFFFFL
//VPEC_DPM3_FREQ
#define VPEC_DPM3_FREQ__VALUE__SHIFT                                                                          0x0
#define VPEC_DPM3_FREQ__VALUE_MASK                                                                            0xFFFFFFFFL
//VPEC_DPM_THRESHOLD_SKIP
#define VPEC_DPM_THRESHOLD_SKIP__VALUE__SHIFT                                                                 0x0
#define VPEC_DPM_THRESHOLD_SKIP__VALUE_MASK                                                                   0xFFFFFFFFL
//VPEC_DPM_THRESHOLD_BUSY_OVERFLOW
#define VPEC_DPM_THRESHOLD_BUSY_OVERFLOW__VALUE__SHIFT                                                        0x0
#define VPEC_DPM_THRESHOLD_BUSY_OVERFLOW__VALUE_MASK                                                          0xFFFFFFFFL
//VPEC_DPM_CALC_BUSY_IN_POSTPROCESS
#define VPEC_DPM_CALC_BUSY_IN_POSTPROCESS__VALUE__SHIFT                                                       0x0
#define VPEC_DPM_CALC_BUSY_IN_POSTPROCESS__VALUE_MASK                                                         0xFFFFFFFFL
//VPEC_DPM_IN_CHECKIDLE_LOOP
#define VPEC_DPM_IN_CHECKIDLE_LOOP__VALUE__SHIFT                                                              0x0
#define VPEC_DPM_IN_CHECKIDLE_LOOP__VALUE_MASK                                                                0xFFFFFFFFL
//VPEC_DPM_THRESHOLD_IDLE_OVERFLOW
#define VPEC_DPM_THRESHOLD_IDLE_OVERFLOW__VALUE__SHIFT                                                        0x0
#define VPEC_DPM_THRESHOLD_IDLE_OVERFLOW__VALUE_MASK                                                          0xFFFFFFFFL
//VPEC_DPM_BUSY_CLAMP_COUNT
#define VPEC_DPM_BUSY_CLAMP_COUNT__VALUE__SHIFT                                                               0x0
#define VPEC_DPM_BUSY_CLAMP_COUNT__VALUE_MASK                                                                 0xFFFFFFFFL
//VPEC_DPM_IDLE_CLAMP_COUNT
#define VPEC_DPM_IDLE_CLAMP_COUNT__VALUE__SHIFT                                                               0x0
#define VPEC_DPM_IDLE_CLAMP_COUNT__VALUE_MASK                                                                 0xFFFFFFFFL
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
//VPEC_PG_STATUS
#define VPEC_PG_STATUS__PG_STATUS__SHIFT                                                                      0x0
#define VPEC_PG_STATUS__PG1_STATUS__SHIFT                                                                     0x2
#define VPEC_PG_STATUS__PG_STATUS_MASK                                                                        0x00000003L
#define VPEC_PG_STATUS__PG1_STATUS_MASK                                                                       0x0000000CL
//VPEC_CLOCK_GATING_STATUS
#define VPEC_CLOCK_GATING_STATUS__DYN_CLK_GATE_STATUS__SHIFT                                                  0x0
#define VPEC_CLOCK_GATING_STATUS__IP_PIPE0_CLK_GATE_STATUS__SHIFT                                             0x2
#define VPEC_CLOCK_GATING_STATUS__IP_PIPE1_CLK_GATE_STATUS__SHIFT                                             0x3
#define VPEC_CLOCK_GATING_STATUS__IP_PIPE2_CLK_GATE_STATUS__SHIFT                                             0x4
#define VPEC_CLOCK_GATING_STATUS__IP_PIPE3_CLK_GATE_STATUS__SHIFT                                             0x5
#define VPEC_CLOCK_GATING_STATUS__OP_PIPE0_CLK_GATE_STATUS__SHIFT                                             0x6
#define VPEC_CLOCK_GATING_STATUS__OP_PIPE1_CLK_GATE_STATUS__SHIFT                                             0x7
#define VPEC_CLOCK_GATING_STATUS__OP_PIPE2_CLK_GATE_STATUS__SHIFT                                             0x8
#define VPEC_CLOCK_GATING_STATUS__OP_PIPE3_CLK_GATE_STATUS__SHIFT                                             0x9
#define VPEC_CLOCK_GATING_STATUS__REG_CLK_GATE_STATUS__SHIFT                                                  0xa
#define VPEC_CLOCK_GATING_STATUS__F32_CLK_GATE_STATUS__SHIFT                                                  0xb
#define VPEC_CLOCK_GATING_STATUS__USRAM_CLK_GATE_STATUS__SHIFT                                                0xc
#define VPEC_CLOCK_GATING_STATUS__OP_PIPE4_CLK_GATE_STATUS__SHIFT                                             0xd
#define VPEC_CLOCK_GATING_STATUS__OP_PIPE5_CLK_GATE_STATUS__SHIFT                                             0xe
#define VPEC_CLOCK_GATING_STATUS__OP_PIPE6_CLK_GATE_STATUS__SHIFT                                             0xf
#define VPEC_CLOCK_GATING_STATUS__OP_PIPE7_CLK_GATE_STATUS__SHIFT                                             0x10
#define VPEC_CLOCK_GATING_STATUS__IP_PIPE4_CLK_GATE_STATUS__SHIFT                                             0x11
#define VPEC_CLOCK_GATING_STATUS__IP_PIPE5_CLK_GATE_STATUS__SHIFT                                             0x12
#define VPEC_CLOCK_GATING_STATUS__OP_PIPE8_CLK_GATE_STATUS__SHIFT                                             0x13
#define VPEC_CLOCK_GATING_STATUS__OP_PIPE9_CLK_GATE_STATUS__SHIFT                                             0x14
#define VPEC_CLOCK_GATING_STATUS__DYN_CLK_GATE_STATUS_MASK                                                    0x00000001L
#define VPEC_CLOCK_GATING_STATUS__IP_PIPE0_CLK_GATE_STATUS_MASK                                               0x00000004L
#define VPEC_CLOCK_GATING_STATUS__IP_PIPE1_CLK_GATE_STATUS_MASK                                               0x00000008L
#define VPEC_CLOCK_GATING_STATUS__IP_PIPE2_CLK_GATE_STATUS_MASK                                               0x00000010L
#define VPEC_CLOCK_GATING_STATUS__IP_PIPE3_CLK_GATE_STATUS_MASK                                               0x00000020L
#define VPEC_CLOCK_GATING_STATUS__OP_PIPE0_CLK_GATE_STATUS_MASK                                               0x00000040L
#define VPEC_CLOCK_GATING_STATUS__OP_PIPE1_CLK_GATE_STATUS_MASK                                               0x00000080L
#define VPEC_CLOCK_GATING_STATUS__OP_PIPE2_CLK_GATE_STATUS_MASK                                               0x00000100L
#define VPEC_CLOCK_GATING_STATUS__OP_PIPE3_CLK_GATE_STATUS_MASK                                               0x00000200L
#define VPEC_CLOCK_GATING_STATUS__REG_CLK_GATE_STATUS_MASK                                                    0x00000400L
#define VPEC_CLOCK_GATING_STATUS__F32_CLK_GATE_STATUS_MASK                                                    0x00000800L
#define VPEC_CLOCK_GATING_STATUS__USRAM_CLK_GATE_STATUS_MASK                                                  0x00001000L
#define VPEC_CLOCK_GATING_STATUS__OP_PIPE4_CLK_GATE_STATUS_MASK                                               0x00002000L
#define VPEC_CLOCK_GATING_STATUS__OP_PIPE5_CLK_GATE_STATUS_MASK                                               0x00004000L
#define VPEC_CLOCK_GATING_STATUS__OP_PIPE6_CLK_GATE_STATUS_MASK                                               0x00008000L
#define VPEC_CLOCK_GATING_STATUS__OP_PIPE7_CLK_GATE_STATUS_MASK                                               0x00010000L
#define VPEC_CLOCK_GATING_STATUS__IP_PIPE4_CLK_GATE_STATUS_MASK                                               0x00020000L
#define VPEC_CLOCK_GATING_STATUS__IP_PIPE5_CLK_GATE_STATUS_MASK                                               0x00040000L
#define VPEC_CLOCK_GATING_STATUS__OP_PIPE8_CLK_GATE_STATUS_MASK                                               0x00080000L
#define VPEC_CLOCK_GATING_STATUS__OP_PIPE9_CLK_GATE_STATUS_MASK                                               0x00100000L
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
