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
#ifndef _smuio_15_0_3_SH_MASK_HEADER
#define _smuio_15_0_3_SH_MASK_HEADER


// addressBlock: smuio_smuio_swtimer_SmuSmuioDec
//PWR_VIRT_RESET_REQ
#define PWR_VIRT_RESET_REQ__VF_FLR__SHIFT                                                                     0x0
#define PWR_VIRT_RESET_REQ__PF_FLR__SHIFT                                                                     0x1f
#define PWR_VIRT_RESET_REQ__VF_FLR_MASK                                                                       0x7FFFFFFFL
#define PWR_VIRT_RESET_REQ__PF_FLR_MASK                                                                       0x80000000L
//PWR_DISP_TIMER_CONTROL
#define PWR_DISP_TIMER_CONTROL__DISP_TIMER_INT_COUNT__SHIFT                                                   0x0
#define PWR_DISP_TIMER_CONTROL__DISP_TIMER_INT_ENABLE__SHIFT                                                  0x19
#define PWR_DISP_TIMER_CONTROL__DISP_TIMER_INT_DISABLE__SHIFT                                                 0x1a
#define PWR_DISP_TIMER_CONTROL__DISP_TIMER_INT_MASK__SHIFT                                                    0x1b
#define PWR_DISP_TIMER_CONTROL__DISP_TIMER_INT_STAT_AK__SHIFT                                                 0x1c
#define PWR_DISP_TIMER_CONTROL__DISP_TIMER_INT_TYPE__SHIFT                                                    0x1d
#define PWR_DISP_TIMER_CONTROL__DISP_TIMER_INT_MODE__SHIFT                                                    0x1e
#define PWR_DISP_TIMER_CONTROL__DISP_TIMER_INT_COUNT_MASK                                                     0x01FFFFFFL
#define PWR_DISP_TIMER_CONTROL__DISP_TIMER_INT_ENABLE_MASK                                                    0x02000000L
#define PWR_DISP_TIMER_CONTROL__DISP_TIMER_INT_DISABLE_MASK                                                   0x04000000L
#define PWR_DISP_TIMER_CONTROL__DISP_TIMER_INT_MASK_MASK                                                      0x08000000L
#define PWR_DISP_TIMER_CONTROL__DISP_TIMER_INT_STAT_AK_MASK                                                   0x10000000L
#define PWR_DISP_TIMER_CONTROL__DISP_TIMER_INT_TYPE_MASK                                                      0x20000000L
#define PWR_DISP_TIMER_CONTROL__DISP_TIMER_INT_MODE_MASK                                                      0x40000000L
//PWR_DISP_TIMER_DEBUG
#define PWR_DISP_TIMER_DEBUG__DISP_TIMER_INT_RUNNING__SHIFT                                                   0x0
#define PWR_DISP_TIMER_DEBUG__DISP_TIMER_INT_STAT__SHIFT                                                      0x1
#define PWR_DISP_TIMER_DEBUG__DISP_TIMER_INT__SHIFT                                                           0x2
#define PWR_DISP_TIMER_DEBUG__DISP_TIMER_RUN_VAL__SHIFT                                                       0x7
#define PWR_DISP_TIMER_DEBUG__DISP_TIMER_INT_RUNNING_MASK                                                     0x00000001L
#define PWR_DISP_TIMER_DEBUG__DISP_TIMER_INT_STAT_MASK                                                        0x00000002L
#define PWR_DISP_TIMER_DEBUG__DISP_TIMER_INT_MASK                                                             0x00000004L
#define PWR_DISP_TIMER_DEBUG__DISP_TIMER_RUN_VAL_MASK                                                         0xFFFFFF80L
//PWR_DISP_TIMER2_CONTROL
#define PWR_DISP_TIMER2_CONTROL__DISP_TIMER_INT_COUNT__SHIFT                                                  0x0
#define PWR_DISP_TIMER2_CONTROL__DISP_TIMER_INT_ENABLE__SHIFT                                                 0x19
#define PWR_DISP_TIMER2_CONTROL__DISP_TIMER_INT_DISABLE__SHIFT                                                0x1a
#define PWR_DISP_TIMER2_CONTROL__DISP_TIMER_INT_MASK__SHIFT                                                   0x1b
#define PWR_DISP_TIMER2_CONTROL__DISP_TIMER_INT_STAT_AK__SHIFT                                                0x1c
#define PWR_DISP_TIMER2_CONTROL__DISP_TIMER_INT_TYPE__SHIFT                                                   0x1d
#define PWR_DISP_TIMER2_CONTROL__DISP_TIMER_INT_MODE__SHIFT                                                   0x1e
#define PWR_DISP_TIMER2_CONTROL__DISP_TIMER_INT_COUNT_MASK                                                    0x01FFFFFFL
#define PWR_DISP_TIMER2_CONTROL__DISP_TIMER_INT_ENABLE_MASK                                                   0x02000000L
#define PWR_DISP_TIMER2_CONTROL__DISP_TIMER_INT_DISABLE_MASK                                                  0x04000000L
#define PWR_DISP_TIMER2_CONTROL__DISP_TIMER_INT_MASK_MASK                                                     0x08000000L
#define PWR_DISP_TIMER2_CONTROL__DISP_TIMER_INT_STAT_AK_MASK                                                  0x10000000L
#define PWR_DISP_TIMER2_CONTROL__DISP_TIMER_INT_TYPE_MASK                                                     0x20000000L
#define PWR_DISP_TIMER2_CONTROL__DISP_TIMER_INT_MODE_MASK                                                     0x40000000L
//PWR_DISP_TIMER2_DEBUG
#define PWR_DISP_TIMER2_DEBUG__DISP_TIMER_INT_RUNNING__SHIFT                                                  0x0
#define PWR_DISP_TIMER2_DEBUG__DISP_TIMER_INT_STAT__SHIFT                                                     0x1
#define PWR_DISP_TIMER2_DEBUG__DISP_TIMER_INT__SHIFT                                                          0x2
#define PWR_DISP_TIMER2_DEBUG__DISP_TIMER_RUN_VAL__SHIFT                                                      0x7
#define PWR_DISP_TIMER2_DEBUG__DISP_TIMER_INT_RUNNING_MASK                                                    0x00000001L
#define PWR_DISP_TIMER2_DEBUG__DISP_TIMER_INT_STAT_MASK                                                       0x00000002L
#define PWR_DISP_TIMER2_DEBUG__DISP_TIMER_INT_MASK                                                            0x00000004L
#define PWR_DISP_TIMER2_DEBUG__DISP_TIMER_RUN_VAL_MASK                                                        0xFFFFFF80L
//PWR_DISP_TIMER_GLOBAL_CONTROL
#define PWR_DISP_TIMER_GLOBAL_CONTROL__DISP_TIMER_PULSE_WIDTH__SHIFT                                          0x0
#define PWR_DISP_TIMER_GLOBAL_CONTROL__DISP_TIMER_PULSE_EN__SHIFT                                             0xa
#define PWR_DISP_TIMER_GLOBAL_CONTROL__DISP_TIMER_PULSE_WIDTH_MASK                                            0x000003FFL
#define PWR_DISP_TIMER_GLOBAL_CONTROL__DISP_TIMER_PULSE_EN_MASK                                               0x00000400L
//PWR_IH_CONTROL
#define PWR_IH_CONTROL__MAX_CREDIT__SHIFT                                                                     0x0
#define PWR_IH_CONTROL__DISP_TIMER_TRIGGER_MASK__SHIFT                                                        0x5
#define PWR_IH_CONTROL__DISP_TIMER2_TRIGGER_MASK__SHIFT                                                       0x6
#define PWR_IH_CONTROL__PWR_IH_CLK_GATE_EN__SHIFT                                                             0x1f
#define PWR_IH_CONTROL__MAX_CREDIT_MASK                                                                       0x0000001FL
#define PWR_IH_CONTROL__DISP_TIMER_TRIGGER_MASK_MASK                                                          0x00000020L
#define PWR_IH_CONTROL__DISP_TIMER2_TRIGGER_MASK_MASK                                                         0x00000040L
#define PWR_IH_CONTROL__PWR_IH_CLK_GATE_EN_MASK                                                               0x80000000L


// addressBlock: smuio_smuio_misc_SmuSmuioDec
//SMUIO_MCM_CONFIG
#define SMUIO_MCM_CONFIG__DIE_ID__SHIFT                                                                       0x0
#define SMUIO_MCM_CONFIG__PKG_TYPE__SHIFT                                                                     0x2
#define SMUIO_MCM_CONFIG__SOCKET_ID__SHIFT                                                                    0x8
#define SMUIO_MCM_CONFIG__DIE_CONFIG__SHIFT                                                                   0xd
#define SMUIO_MCM_CONFIG__PKG_SUBTYPE__SHIFT                                                                  0x12
#define SMUIO_MCM_CONFIG__DIE_ID_MASK                                                                         0x00000003L
#define SMUIO_MCM_CONFIG__PKG_TYPE_MASK                                                                       0x0000003CL
#define SMUIO_MCM_CONFIG__SOCKET_ID_MASK                                                                      0x00000100L
#define SMUIO_MCM_CONFIG__DIE_CONFIG_MASK                                                                     0x0000E000L
#define SMUIO_MCM_CONFIG__PKG_SUBTYPE_MASK                                                                    0x00040000L
//IP_DISCOVERY_VERSION
#define IP_DISCOVERY_VERSION__IP_DISCOVERY_VERSION__SHIFT                                                     0x0
#define IP_DISCOVERY_VERSION__IP_DISCOVERY_VERSION_MASK                                                       0xFFFFFFFFL
//SMUIO_PCC_CONTROL
#define SMUIO_PCC_CONTROL__PCC_ENABLE__SHIFT                                                                  0x0
#define SMUIO_PCC_CONTROL__PCC_POLARITY__SHIFT                                                                0x1
#define SMUIO_PCC_CONTROL__PCC_A__SHIFT                                                                       0x2
#define SMUIO_PCC_CONTROL__PCC_OE__SHIFT                                                                      0x3
#define SMUIO_PCC_CONTROL__PCC_PU__SHIFT                                                                      0x4
#define SMUIO_PCC_CONTROL__PCC_PD__SHIFT                                                                      0x5
#define SMUIO_PCC_CONTROL__PCC_S0__SHIFT                                                                      0x6
#define SMUIO_PCC_CONTROL__PCC_S1__SHIFT                                                                      0x7
#define SMUIO_PCC_CONTROL__PCC_SCHMEN__SHIFT                                                                  0x8
#define SMUIO_PCC_CONTROL__PCC_DFTRXEN__SHIFT                                                                 0x9
#define SMUIO_PCC_CONTROL__PCC_ENABLE_MASK                                                                    0x00000001L
#define SMUIO_PCC_CONTROL__PCC_POLARITY_MASK                                                                  0x00000002L
#define SMUIO_PCC_CONTROL__PCC_A_MASK                                                                         0x00000004L
#define SMUIO_PCC_CONTROL__PCC_OE_MASK                                                                        0x00000008L
#define SMUIO_PCC_CONTROL__PCC_PU_MASK                                                                        0x00000010L
#define SMUIO_PCC_CONTROL__PCC_PD_MASK                                                                        0x00000020L
#define SMUIO_PCC_CONTROL__PCC_S0_MASK                                                                        0x00000040L
#define SMUIO_PCC_CONTROL__PCC_S1_MASK                                                                        0x00000080L
#define SMUIO_PCC_CONTROL__PCC_SCHMEN_MASK                                                                    0x00000100L
#define SMUIO_PCC_CONTROL__PCC_DFTRXEN_MASK                                                                   0x00000200L
//SCRATCH_REGISTER0
#define SCRATCH_REGISTER0__ScratchPad0__SHIFT                                                                 0x0
#define SCRATCH_REGISTER0__ScratchPad0_MASK                                                                   0xFFFFFFFFL
//SCRATCH_REGISTER1
#define SCRATCH_REGISTER1__ScratchPad1__SHIFT                                                                 0x0
#define SCRATCH_REGISTER1__ScratchPad1_MASK                                                                   0xFFFFFFFFL
//SCRATCH_REGISTER2
#define SCRATCH_REGISTER2__ScratchPad2__SHIFT                                                                 0x0
#define SCRATCH_REGISTER2__ScratchPad2_MASK                                                                   0xFFFFFFFFL
//SCRATCH_REGISTER3
#define SCRATCH_REGISTER3__ScratchPad3__SHIFT                                                                 0x0
#define SCRATCH_REGISTER3__ScratchPad3_MASK                                                                   0xFFFFFFFFL
//SCRATCH_REGISTER4
#define SCRATCH_REGISTER4__ScratchPad4__SHIFT                                                                 0x0
#define SCRATCH_REGISTER4__ScratchPad4_MASK                                                                   0xFFFFFFFFL
//SCRATCH_REGISTER5
#define SCRATCH_REGISTER5__ScratchPad5__SHIFT                                                                 0x0
#define SCRATCH_REGISTER5__ScratchPad5_MASK                                                                   0xFFFFFFFFL
//SCRATCH_REGISTER6
#define SCRATCH_REGISTER6__ScratchPad6__SHIFT                                                                 0x0
#define SCRATCH_REGISTER6__ScratchPad6_MASK                                                                   0xFFFFFFFFL
//SCRATCH_REGISTER7
#define SCRATCH_REGISTER7__ScratchPad7__SHIFT                                                                 0x0
#define SCRATCH_REGISTER7__ScratchPad7_MASK                                                                   0xFFFFFFFFL
//IO_SMUIO_PINSTRAP
#define IO_SMUIO_PINSTRAP__AUD_PORT_CONN__SHIFT                                                               0x0
#define IO_SMUIO_PINSTRAP__AUD__SHIFT                                                                         0x3
#define IO_SMUIO_PINSTRAP__AUD_PORT_CONN_MASK                                                                 0x00000007L
#define IO_SMUIO_PINSTRAP__AUD_MASK                                                                           0x00000018L


#endif
