/*
 * Copyright 2022 Advanced Micro Devices, Inc.
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
#ifndef _smuio_13_0_3_SH_MASK_HEADER
#define _smuio_13_0_3_SH_MASK_HEADER


// addressBlock: aid_smuio_smuio_reset_SmuSmuioDec
//SMUIO_MP_RESET_INTR
#define SMUIO_MP_RESET_INTR__SMUIO_MP_RESET_INTR__SHIFT                                                       0x0
#define SMUIO_MP_RESET_INTR__SMUIO_MP_RESET_INTR_MASK                                                         0x00000001L
//SMUIO_SOC_HALT
#define SMUIO_SOC_HALT__WDT_FORCE_PWROK_EN__SHIFT                                                             0x2
#define SMUIO_SOC_HALT__WDT_FORCE_RESETn_EN__SHIFT                                                            0x3
#define SMUIO_SOC_HALT__WDT_FORCE_PWROK_EN_MASK                                                               0x00000004L
#define SMUIO_SOC_HALT__WDT_FORCE_RESETn_EN_MASK                                                              0x00000008L


// addressBlock: aid_smuio_smuio_tsc_SmuSmuioDec
//PWROK_REFCLK_GAP_CYCLES
#define PWROK_REFCLK_GAP_CYCLES__Pwrok_PreAssertion_clkgap_cycles__SHIFT                                      0x0
#define PWROK_REFCLK_GAP_CYCLES__Pwrok_PostAssertion_clkgap_cycles__SHIFT                                     0x8
#define PWROK_REFCLK_GAP_CYCLES__Pwrok_PreAssertion_clkgap_cycles_MASK                                        0x000000FFL
#define PWROK_REFCLK_GAP_CYCLES__Pwrok_PostAssertion_clkgap_cycles_MASK                                       0x0000FF00L
//GOLDEN_TSC_INCREMENT_UPPER
#define GOLDEN_TSC_INCREMENT_UPPER__GoldenTscIncrementUpper__SHIFT                                            0x0
#define GOLDEN_TSC_INCREMENT_UPPER__GoldenTscIncrementUpper_MASK                                              0x00FFFFFFL
//GOLDEN_TSC_INCREMENT_LOWER
#define GOLDEN_TSC_INCREMENT_LOWER__GoldenTscIncrementLower__SHIFT                                            0x0
#define GOLDEN_TSC_INCREMENT_LOWER__GoldenTscIncrementLower_MASK                                              0xFFFFFFFFL
//GOLDEN_TSC_COUNT_UPPER
#define GOLDEN_TSC_COUNT_UPPER__GoldenTscCountUpper__SHIFT                                                    0x0
#define GOLDEN_TSC_COUNT_UPPER__GoldenTscCountUpper_MASK                                                      0x00FFFFFFL
//GOLDEN_TSC_COUNT_LOWER
#define GOLDEN_TSC_COUNT_LOWER__GoldenTscCountLower__SHIFT                                                    0x0
#define GOLDEN_TSC_COUNT_LOWER__GoldenTscCountLower_MASK                                                      0xFFFFFFFFL
//SOC_GOLDEN_TSC_SHADOW_UPPER
#define SOC_GOLDEN_TSC_SHADOW_UPPER__SocGoldenTscShadowUpper__SHIFT                                           0x0
#define SOC_GOLDEN_TSC_SHADOW_UPPER__SocGoldenTscShadowUpper_MASK                                             0x00FFFFFFL
//SOC_GOLDEN_TSC_SHADOW_LOWER
#define SOC_GOLDEN_TSC_SHADOW_LOWER__SocGoldenTscShadowLower__SHIFT                                           0x0
#define SOC_GOLDEN_TSC_SHADOW_LOWER__SocGoldenTscShadowLower_MASK                                             0xFFFFFFFFL
//SOC_GAP_PWROK
#define SOC_GAP_PWROK__soc_gap_pwrok__SHIFT                                                                   0x0
#define SOC_GAP_PWROK__soc_gap_pwrok_MASK                                                                     0x00000001L


// addressBlock: aid_smuio_smuio_swtimer_SmuSmuioDec
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


// addressBlock: aid_smuio_smuio_misc_SmuSmuioDec
//SMUIO_MCM_CONFIG
#define SMUIO_MCM_CONFIG__DIE_ID__SHIFT                                                                       0x0
#define SMUIO_MCM_CONFIG__PKG_TYPE__SHIFT                                                                     0x2
#define SMUIO_MCM_CONFIG__SOCKET_ID__SHIFT                                                                    0x8
#define SMUIO_MCM_CONFIG__PKG_SUBTYPE__SHIFT                                                                  0xc
#define SMUIO_MCM_CONFIG__CONSOLE_K__SHIFT                                                                    0x10
#define SMUIO_MCM_CONFIG__CONSOLE_A__SHIFT                                                                    0x11
#define SMUIO_MCM_CONFIG__TOPOLOGY_ID__SHIFT                                                                  0x12
#define SMUIO_MCM_CONFIG__DIE_ID_MASK                                                                         0x00000003L
#define SMUIO_MCM_CONFIG__PKG_TYPE_MASK                                                                       0x0000003CL
#define SMUIO_MCM_CONFIG__SOCKET_ID_MASK                                                                      0x00000F00L
#define SMUIO_MCM_CONFIG__PKG_SUBTYPE_MASK                                                                    0x00001000L
#define SMUIO_MCM_CONFIG__CONSOLE_K_MASK                                                                      0x00010000L
#define SMUIO_MCM_CONFIG__CONSOLE_A_MASK                                                                      0x00020000L
#define SMUIO_MCM_CONFIG__TOPOLOGY_ID_MASK                                                                    0x007C0000L
//IP_DISCOVERY_VERSION
#define IP_DISCOVERY_VERSION__IP_DISCOVERY_VERSION__SHIFT                                                     0x0
#define IP_DISCOVERY_VERSION__IP_DISCOVERY_VERSION_MASK                                                       0xFFFFFFFFL
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


// addressBlock: aid_smuio_smuio_gpio_SmuSmuioDec
//SMU_GPIOPAD_SW_INT_STAT
#define SMU_GPIOPAD_SW_INT_STAT__SW_INT_STAT__SHIFT                                                           0x0
#define SMU_GPIOPAD_SW_INT_STAT__SW_INT_STAT_MASK                                                             0x00000001L
//SMU_GPIOPAD_MASK
#define SMU_GPIOPAD_MASK__GPIO_MASK__SHIFT                                                                    0x0
#define SMU_GPIOPAD_MASK__GPIO_MASK_MASK                                                                      0x7FFFFFFFL
//SMU_GPIOPAD_A
#define SMU_GPIOPAD_A__GPIO_A__SHIFT                                                                          0x0
#define SMU_GPIOPAD_A__GPIO_A_MASK                                                                            0x7FFFFFFFL
//SMU_GPIOPAD_TXIMPSEL
#define SMU_GPIOPAD_TXIMPSEL__GPIO_TXIMPSEL__SHIFT                                                            0x0
#define SMU_GPIOPAD_TXIMPSEL__GPIO_TXIMPSEL_MASK                                                              0x7FFFFFFFL
//SMU_GPIOPAD_EN
#define SMU_GPIOPAD_EN__GPIO_EN__SHIFT                                                                        0x0
#define SMU_GPIOPAD_EN__GPIO_EN_MASK                                                                          0x7FFFFFFFL
//SMU_GPIOPAD_Y
#define SMU_GPIOPAD_Y__GPIO_Y__SHIFT                                                                          0x0
#define SMU_GPIOPAD_Y__GPIO_Y_MASK                                                                            0x7FFFFFFFL
//SMU_GPIOPAD_RXEN
#define SMU_GPIOPAD_RXEN__GPIO_RXEN__SHIFT                                                                    0x0
#define SMU_GPIOPAD_RXEN__GPIO_RXEN_MASK                                                                      0x7FFFFFFFL
//SMU_GPIOPAD_RCVR_SEL0
#define SMU_GPIOPAD_RCVR_SEL0__GPIO_RCVR_SEL0__SHIFT                                                          0x0
#define SMU_GPIOPAD_RCVR_SEL0__GPIO_RCVR_SEL0_MASK                                                            0x7FFFFFFFL
//SMU_GPIOPAD_RCVR_SEL1
#define SMU_GPIOPAD_RCVR_SEL1__GPIO_RCVR_SEL1__SHIFT                                                          0x0
#define SMU_GPIOPAD_RCVR_SEL1__GPIO_RCVR_SEL1_MASK                                                            0x7FFFFFFFL
//SMU_GPIOPAD_PU_EN
#define SMU_GPIOPAD_PU_EN__GPIO_PU_EN__SHIFT                                                                  0x0
#define SMU_GPIOPAD_PU_EN__GPIO_PU_EN_MASK                                                                    0x7FFFFFFFL
//SMU_GPIOPAD_PD_EN
#define SMU_GPIOPAD_PD_EN__GPIO_PD_EN__SHIFT                                                                  0x0
#define SMU_GPIOPAD_PD_EN__GPIO_PD_EN_MASK                                                                    0x7FFFFFFFL
//SMU_GPIOPAD_PINSTRAPS
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_0__SHIFT                                                         0x0
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_1__SHIFT                                                         0x1
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_2__SHIFT                                                         0x2
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_3__SHIFT                                                         0x3
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_4__SHIFT                                                         0x4
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_5__SHIFT                                                         0x5
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_6__SHIFT                                                         0x6
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_7__SHIFT                                                         0x7
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_8__SHIFT                                                         0x8
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_9__SHIFT                                                         0x9
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_10__SHIFT                                                        0xa
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_11__SHIFT                                                        0xb
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_12__SHIFT                                                        0xc
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_13__SHIFT                                                        0xd
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_14__SHIFT                                                        0xe
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_15__SHIFT                                                        0xf
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_16__SHIFT                                                        0x10
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_17__SHIFT                                                        0x11
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_18__SHIFT                                                        0x12
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_19__SHIFT                                                        0x13
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_20__SHIFT                                                        0x14
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_21__SHIFT                                                        0x15
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_22__SHIFT                                                        0x16
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_23__SHIFT                                                        0x17
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_24__SHIFT                                                        0x18
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_25__SHIFT                                                        0x19
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_26__SHIFT                                                        0x1a
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_27__SHIFT                                                        0x1b
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_28__SHIFT                                                        0x1c
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_29__SHIFT                                                        0x1d
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_30__SHIFT                                                        0x1e
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_0_MASK                                                           0x00000001L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_1_MASK                                                           0x00000002L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_2_MASK                                                           0x00000004L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_3_MASK                                                           0x00000008L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_4_MASK                                                           0x00000010L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_5_MASK                                                           0x00000020L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_6_MASK                                                           0x00000040L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_7_MASK                                                           0x00000080L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_8_MASK                                                           0x00000100L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_9_MASK                                                           0x00000200L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_10_MASK                                                          0x00000400L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_11_MASK                                                          0x00000800L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_12_MASK                                                          0x00001000L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_13_MASK                                                          0x00002000L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_14_MASK                                                          0x00004000L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_15_MASK                                                          0x00008000L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_16_MASK                                                          0x00010000L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_17_MASK                                                          0x00020000L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_18_MASK                                                          0x00040000L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_19_MASK                                                          0x00080000L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_20_MASK                                                          0x00100000L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_21_MASK                                                          0x00200000L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_22_MASK                                                          0x00400000L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_23_MASK                                                          0x00800000L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_24_MASK                                                          0x01000000L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_25_MASK                                                          0x02000000L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_26_MASK                                                          0x04000000L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_27_MASK                                                          0x08000000L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_28_MASK                                                          0x10000000L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_29_MASK                                                          0x20000000L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_30_MASK                                                          0x40000000L
//DFT_PINSTRAPS
#define DFT_PINSTRAPS__DFT_PINSTRAPS__SHIFT                                                                   0x0
#define DFT_PINSTRAPS__DFT_PINSTRAPS_MASK                                                                     0x000003FFL
//SMU_GPIOPAD_INT_STAT_EN
#define SMU_GPIOPAD_INT_STAT_EN__GPIO_INT_STAT_EN__SHIFT                                                      0x0
#define SMU_GPIOPAD_INT_STAT_EN__SW_INITIATED_INT_STAT_EN__SHIFT                                              0x1f
#define SMU_GPIOPAD_INT_STAT_EN__GPIO_INT_STAT_EN_MASK                                                        0x1FFFFFFFL
#define SMU_GPIOPAD_INT_STAT_EN__SW_INITIATED_INT_STAT_EN_MASK                                                0x80000000L
//SMU_GPIOPAD_INT_STAT
#define SMU_GPIOPAD_INT_STAT__GPIO_INT_STAT__SHIFT                                                            0x0
#define SMU_GPIOPAD_INT_STAT__SW_INITIATED_INT_STAT__SHIFT                                                    0x1f
#define SMU_GPIOPAD_INT_STAT__GPIO_INT_STAT_MASK                                                              0x1FFFFFFFL
#define SMU_GPIOPAD_INT_STAT__SW_INITIATED_INT_STAT_MASK                                                      0x80000000L
//SMU_GPIOPAD_INT_STAT_AK
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_0__SHIFT                                                    0x0
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_1__SHIFT                                                    0x1
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_2__SHIFT                                                    0x2
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_3__SHIFT                                                    0x3
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_4__SHIFT                                                    0x4
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_5__SHIFT                                                    0x5
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_6__SHIFT                                                    0x6
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_7__SHIFT                                                    0x7
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_8__SHIFT                                                    0x8
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_9__SHIFT                                                    0x9
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_10__SHIFT                                                   0xa
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_11__SHIFT                                                   0xb
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_12__SHIFT                                                   0xc
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_13__SHIFT                                                   0xd
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_14__SHIFT                                                   0xe
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_15__SHIFT                                                   0xf
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_16__SHIFT                                                   0x10
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_17__SHIFT                                                   0x11
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_18__SHIFT                                                   0x12
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_19__SHIFT                                                   0x13
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_20__SHIFT                                                   0x14
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_21__SHIFT                                                   0x15
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_22__SHIFT                                                   0x16
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_23__SHIFT                                                   0x17
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_24__SHIFT                                                   0x18
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_25__SHIFT                                                   0x19
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_26__SHIFT                                                   0x1a
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_27__SHIFT                                                   0x1b
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_28__SHIFT                                                   0x1c
#define SMU_GPIOPAD_INT_STAT_AK__SW_INITIATED_INT_STAT_AK__SHIFT                                              0x1f
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_0_MASK                                                      0x00000001L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_1_MASK                                                      0x00000002L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_2_MASK                                                      0x00000004L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_3_MASK                                                      0x00000008L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_4_MASK                                                      0x00000010L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_5_MASK                                                      0x00000020L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_6_MASK                                                      0x00000040L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_7_MASK                                                      0x00000080L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_8_MASK                                                      0x00000100L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_9_MASK                                                      0x00000200L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_10_MASK                                                     0x00000400L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_11_MASK                                                     0x00000800L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_12_MASK                                                     0x00001000L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_13_MASK                                                     0x00002000L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_14_MASK                                                     0x00004000L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_15_MASK                                                     0x00008000L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_16_MASK                                                     0x00010000L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_17_MASK                                                     0x00020000L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_18_MASK                                                     0x00040000L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_19_MASK                                                     0x00080000L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_20_MASK                                                     0x00100000L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_21_MASK                                                     0x00200000L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_22_MASK                                                     0x00400000L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_23_MASK                                                     0x00800000L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_24_MASK                                                     0x01000000L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_25_MASK                                                     0x02000000L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_26_MASK                                                     0x04000000L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_27_MASK                                                     0x08000000L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_28_MASK                                                     0x10000000L
#define SMU_GPIOPAD_INT_STAT_AK__SW_INITIATED_INT_STAT_AK_MASK                                                0x80000000L
//SMU_GPIOPAD_INT_EN
#define SMU_GPIOPAD_INT_EN__GPIO_INT_EN__SHIFT                                                                0x0
#define SMU_GPIOPAD_INT_EN__SW_INITIATED_INT_EN__SHIFT                                                        0x1f
#define SMU_GPIOPAD_INT_EN__GPIO_INT_EN_MASK                                                                  0x1FFFFFFFL
#define SMU_GPIOPAD_INT_EN__SW_INITIATED_INT_EN_MASK                                                          0x80000000L
//SMU_GPIOPAD_INT_TYPE
#define SMU_GPIOPAD_INT_TYPE__GPIO_INT_TYPE__SHIFT                                                            0x0
#define SMU_GPIOPAD_INT_TYPE__SW_INITIATED_INT_TYPE__SHIFT                                                    0x1f
#define SMU_GPIOPAD_INT_TYPE__GPIO_INT_TYPE_MASK                                                              0x1FFFFFFFL
#define SMU_GPIOPAD_INT_TYPE__SW_INITIATED_INT_TYPE_MASK                                                      0x80000000L
//SMU_GPIOPAD_INT_POLARITY
#define SMU_GPIOPAD_INT_POLARITY__GPIO_INT_POLARITY__SHIFT                                                    0x0
#define SMU_GPIOPAD_INT_POLARITY__SW_INITIATED_INT_POLARITY__SHIFT                                            0x1f
#define SMU_GPIOPAD_INT_POLARITY__GPIO_INT_POLARITY_MASK                                                      0x1FFFFFFFL
#define SMU_GPIOPAD_INT_POLARITY__SW_INITIATED_INT_POLARITY_MASK                                              0x80000000L
//SMUIO_PCC_GPIO_SELECT
#define SMUIO_PCC_GPIO_SELECT__GPIO__SHIFT                                                                    0x0
#define SMUIO_PCC_GPIO_SELECT__GPIO_MASK                                                                      0xFFFFFFFFL
//SMU_GPIOPAD_S0
#define SMU_GPIOPAD_S0__GPIO_S0__SHIFT                                                                        0x0
#define SMU_GPIOPAD_S0__GPIO_S0_MASK                                                                          0x7FFFFFFFL
//SMU_GPIOPAD_S1
#define SMU_GPIOPAD_S1__GPIO_S1__SHIFT                                                                        0x0
#define SMU_GPIOPAD_S1__GPIO_S1_MASK                                                                          0x7FFFFFFFL
//SMU_GPIOPAD_SCHMEN
#define SMU_GPIOPAD_SCHMEN__GPIO_SCHMEN__SHIFT                                                                0x0
#define SMU_GPIOPAD_SCHMEN__GPIO_SCHMEN_MASK                                                                  0x7FFFFFFFL
//SMU_GPIOPAD_SCL_EN
#define SMU_GPIOPAD_SCL_EN__GPIO_SCL_EN__SHIFT                                                                0x0
#define SMU_GPIOPAD_SCL_EN__GPIO_SCL_EN_MASK                                                                  0x7FFFFFFFL
//SMU_GPIOPAD_SDA_EN
#define SMU_GPIOPAD_SDA_EN__GPIO_SDA_EN__SHIFT                                                                0x0
#define SMU_GPIOPAD_SDA_EN__GPIO_SDA_EN_MASK                                                                  0x7FFFFFFFL
//SMUIO_GPIO_INT0_SELECT
#define SMUIO_GPIO_INT0_SELECT__GPIO_INT0_SELECT__SHIFT                                                       0x0
#define SMUIO_GPIO_INT0_SELECT__GPIO_INT0_SELECT_MASK                                                         0xFFFFFFFFL
//SMUIO_GPIO_INT1_SELECT
#define SMUIO_GPIO_INT1_SELECT__GPIO_INT1_SELECT__SHIFT                                                       0x0
#define SMUIO_GPIO_INT1_SELECT__GPIO_INT1_SELECT_MASK                                                         0xFFFFFFFFL
//SMUIO_GPIO_INT2_SELECT
#define SMUIO_GPIO_INT2_SELECT__GPIO_INT2_SELECT__SHIFT                                                       0x0
#define SMUIO_GPIO_INT2_SELECT__GPIO_INT2_SELECT_MASK                                                         0xFFFFFFFFL
//SMUIO_GPIO_INT3_SELECT
#define SMUIO_GPIO_INT3_SELECT__GPIO_INT3_SELECT__SHIFT                                                       0x0
#define SMUIO_GPIO_INT3_SELECT__GPIO_INT3_SELECT_MASK                                                         0xFFFFFFFFL
//SMU_GPIOPAD_MP_INT0_STAT
#define SMU_GPIOPAD_MP_INT0_STAT__GPIO_MP_INT0_STAT__SHIFT                                                    0x0
#define SMU_GPIOPAD_MP_INT0_STAT__GPIO_MP_INT0_STAT_MASK                                                      0x1FFFFFFFL
//SMU_GPIOPAD_MP_INT1_STAT
#define SMU_GPIOPAD_MP_INT1_STAT__GPIO_MP_INT1_STAT__SHIFT                                                    0x0
#define SMU_GPIOPAD_MP_INT1_STAT__GPIO_MP_INT1_STAT_MASK                                                      0x1FFFFFFFL
//SMU_GPIOPAD_MP_INT2_STAT
#define SMU_GPIOPAD_MP_INT2_STAT__GPIO_MP_INT2_STAT__SHIFT                                                    0x0
#define SMU_GPIOPAD_MP_INT2_STAT__GPIO_MP_INT2_STAT_MASK                                                      0x1FFFFFFFL
//SMU_GPIOPAD_MP_INT3_STAT
#define SMU_GPIOPAD_MP_INT3_STAT__GPIO_MP_INT3_STAT__SHIFT                                                    0x0
#define SMU_GPIOPAD_MP_INT3_STAT__GPIO_MP_INT3_STAT_MASK                                                      0x1FFFFFFFL
//SMIO_INDEX
#define SMIO_INDEX__SW_SMIO_INDEX__SHIFT                                                                      0x0
#define SMIO_INDEX__SW_SMIO_INDEX_MASK                                                                        0x00000001L
//S0_VID_SMIO_CNTL
#define S0_VID_SMIO_CNTL__S0_SMIO_VALUES__SHIFT                                                               0x0
#define S0_VID_SMIO_CNTL__S0_SMIO_VALUES_MASK                                                                 0xFFFFFFFFL
//S1_VID_SMIO_CNTL
#define S1_VID_SMIO_CNTL__S1_SMIO_VALUES__SHIFT                                                               0x0
#define S1_VID_SMIO_CNTL__S1_SMIO_VALUES_MASK                                                                 0xFFFFFFFFL
//OPEN_DRAIN_SELECT
#define OPEN_DRAIN_SELECT__OPEN_DRAIN_SELECT__SHIFT                                                           0x0
#define OPEN_DRAIN_SELECT__RESERVED__SHIFT                                                                    0x1f
#define OPEN_DRAIN_SELECT__OPEN_DRAIN_SELECT_MASK                                                             0x7FFFFFFFL
#define OPEN_DRAIN_SELECT__RESERVED_MASK                                                                      0x80000000L
//SMIO_ENABLE
#define SMIO_ENABLE__SMIO_ENABLE__SHIFT                                                                       0x0
#define SMIO_ENABLE__SMIO_ENABLE_MASK                                                                         0xFFFFFFFFL

#endif
