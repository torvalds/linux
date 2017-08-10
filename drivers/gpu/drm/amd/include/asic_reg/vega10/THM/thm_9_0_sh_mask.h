/*
 * Copyright (C) 2017  Advanced Micro Devices, Inc.
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
#ifndef _thm_9_0_SH_MASK_HEADER
#define _thm_9_0_SH_MASK_HEADER


// addressBlock: thm_thm_SmuThmDec
//THM_TCON_CUR_TMP
#define THM_TCON_CUR_TMP__PER_STEP_TIME_UP__SHIFT                                                             0x0
#define THM_TCON_CUR_TMP__TMP_MAX_DIFF_UP__SHIFT                                                              0x5
#define THM_TCON_CUR_TMP__TMP_SLEW_DN_EN__SHIFT                                                               0x7
#define THM_TCON_CUR_TMP__PER_STEP_TIME_DN__SHIFT                                                             0x8
#define THM_TCON_CUR_TMP__CUR_TEMP_TJ_SEL__SHIFT                                                              0x10
#define THM_TCON_CUR_TMP__CUR_TEMP_TJ_SLEW_SEL__SHIFT                                                         0x12
#define THM_TCON_CUR_TMP__CUR_TEMP_RANGE_SEL__SHIFT                                                           0x13
#define THM_TCON_CUR_TMP__MCM_EN__SHIFT                                                                       0x14
#define THM_TCON_CUR_TMP__CUR_TEMP__SHIFT                                                                     0x15
#define THM_TCON_CUR_TMP__PER_STEP_TIME_UP_MASK                                                               0x0000001FL
#define THM_TCON_CUR_TMP__TMP_MAX_DIFF_UP_MASK                                                                0x00000060L
#define THM_TCON_CUR_TMP__TMP_SLEW_DN_EN_MASK                                                                 0x00000080L
#define THM_TCON_CUR_TMP__PER_STEP_TIME_DN_MASK                                                               0x00001F00L
#define THM_TCON_CUR_TMP__CUR_TEMP_TJ_SEL_MASK                                                                0x00030000L
#define THM_TCON_CUR_TMP__CUR_TEMP_TJ_SLEW_SEL_MASK                                                           0x00040000L
#define THM_TCON_CUR_TMP__CUR_TEMP_RANGE_SEL_MASK                                                             0x00080000L
#define THM_TCON_CUR_TMP__MCM_EN_MASK                                                                         0x00100000L
#define THM_TCON_CUR_TMP__CUR_TEMP_MASK                                                                       0xFFE00000L
//THM_TCON_HTC
#define THM_TCON_HTC__HTC_EN__SHIFT                                                                           0x0
#define THM_TCON_HTC__EXTERNAL_PROCHOT__SHIFT                                                                 0x2
#define THM_TCON_HTC__INTERNAL_PROCHOT__SHIFT                                                                 0x3
#define THM_TCON_HTC__HTC_ACTIVE__SHIFT                                                                       0x4
#define THM_TCON_HTC__HTC_ACTIVE_LOG__SHIFT                                                                   0x5
#define THM_TCON_HTC__HTC_DIAG__SHIFT                                                                         0x8
#define THM_TCON_HTC__DIS_PROCHOT_PIN__SHIFT                                                                  0x9
#define THM_TCON_HTC__HTC_TO_IH_EN__SHIFT                                                                     0xa
#define THM_TCON_HTC__PROCHOT_TO_IH_EN__SHIFT                                                                 0xb
#define THM_TCON_HTC__PROCHOT_EVENT_SRC__SHIFT                                                                0xc
#define THM_TCON_HTC__HTC_TMP_LMT__SHIFT                                                                      0x10
#define THM_TCON_HTC__HTC_HYST_LMT__SHIFT                                                                     0x17
#define THM_TCON_HTC__HTC_SLEW_SEL__SHIFT                                                                     0x1b
#define THM_TCON_HTC__HTC_EN_MASK                                                                             0x00000001L
#define THM_TCON_HTC__EXTERNAL_PROCHOT_MASK                                                                   0x00000004L
#define THM_TCON_HTC__INTERNAL_PROCHOT_MASK                                                                   0x00000008L
#define THM_TCON_HTC__HTC_ACTIVE_MASK                                                                         0x00000010L
#define THM_TCON_HTC__HTC_ACTIVE_LOG_MASK                                                                     0x00000020L
#define THM_TCON_HTC__HTC_DIAG_MASK                                                                           0x00000100L
#define THM_TCON_HTC__DIS_PROCHOT_PIN_MASK                                                                    0x00000200L
#define THM_TCON_HTC__HTC_TO_IH_EN_MASK                                                                       0x00000400L
#define THM_TCON_HTC__PROCHOT_TO_IH_EN_MASK                                                                   0x00000800L
#define THM_TCON_HTC__PROCHOT_EVENT_SRC_MASK                                                                  0x00007000L
#define THM_TCON_HTC__HTC_TMP_LMT_MASK                                                                        0x007F0000L
#define THM_TCON_HTC__HTC_HYST_LMT_MASK                                                                       0x07800000L
#define THM_TCON_HTC__HTC_SLEW_SEL_MASK                                                                       0x18000000L
//THM_TCON_THERM_TRIP
#define THM_TCON_THERM_TRIP__CTF_PAD_POLARITY__SHIFT                                                          0x0
#define THM_TCON_THERM_TRIP__THERM_TP__SHIFT                                                                  0x1
#define THM_TCON_THERM_TRIP__CTF_THRESHOLD_EXCEEDED__SHIFT                                                    0x2
#define THM_TCON_THERM_TRIP__THERM_TP_SENSE__SHIFT                                                            0x3
#define THM_TCON_THERM_TRIP__RSVD2__SHIFT                                                                     0x4
#define THM_TCON_THERM_TRIP__THERM_TP_EN__SHIFT                                                               0x5
#define THM_TCON_THERM_TRIP__THERM_TP_LMT__SHIFT                                                              0x6
#define THM_TCON_THERM_TRIP__RSVD3__SHIFT                                                                     0xe
#define THM_TCON_THERM_TRIP__SW_THERM_TP__SHIFT                                                               0x1f
#define THM_TCON_THERM_TRIP__CTF_PAD_POLARITY_MASK                                                            0x00000001L
#define THM_TCON_THERM_TRIP__THERM_TP_MASK                                                                    0x00000002L
#define THM_TCON_THERM_TRIP__CTF_THRESHOLD_EXCEEDED_MASK                                                      0x00000004L
#define THM_TCON_THERM_TRIP__THERM_TP_SENSE_MASK                                                              0x00000008L
#define THM_TCON_THERM_TRIP__RSVD2_MASK                                                                       0x00000010L
#define THM_TCON_THERM_TRIP__THERM_TP_EN_MASK                                                                 0x00000020L
#define THM_TCON_THERM_TRIP__THERM_TP_LMT_MASK                                                                0x00003FC0L
#define THM_TCON_THERM_TRIP__RSVD3_MASK                                                                       0x7FFFC000L
#define THM_TCON_THERM_TRIP__SW_THERM_TP_MASK                                                                 0x80000000L
//THM_GPIO_PROCHOT_CTRL
#define THM_GPIO_PROCHOT_CTRL__TXIMPSEL__SHIFT                                                                0x0
#define THM_GPIO_PROCHOT_CTRL__PD__SHIFT                                                                      0x1
#define THM_GPIO_PROCHOT_CTRL__PU__SHIFT                                                                      0x2
#define THM_GPIO_PROCHOT_CTRL__SCHMEN__SHIFT                                                                  0x3
#define THM_GPIO_PROCHOT_CTRL__S0__SHIFT                                                                      0x4
#define THM_GPIO_PROCHOT_CTRL__S1__SHIFT                                                                      0x5
#define THM_GPIO_PROCHOT_CTRL__RXEN__SHIFT                                                                    0x6
#define THM_GPIO_PROCHOT_CTRL__RXSEL0__SHIFT                                                                  0x7
#define THM_GPIO_PROCHOT_CTRL__RXSEL1__SHIFT                                                                  0x8
#define THM_GPIO_PROCHOT_CTRL__OE_OVERRIDE__SHIFT                                                             0x10
#define THM_GPIO_PROCHOT_CTRL__OE__SHIFT                                                                      0x11
#define THM_GPIO_PROCHOT_CTRL__A_OVERRIDE__SHIFT                                                              0x12
#define THM_GPIO_PROCHOT_CTRL__A__SHIFT                                                                       0x13
#define THM_GPIO_PROCHOT_CTRL__Y__SHIFT                                                                       0x1f
#define THM_GPIO_PROCHOT_CTRL__TXIMPSEL_MASK                                                                  0x00000001L
#define THM_GPIO_PROCHOT_CTRL__PD_MASK                                                                        0x00000002L
#define THM_GPIO_PROCHOT_CTRL__PU_MASK                                                                        0x00000004L
#define THM_GPIO_PROCHOT_CTRL__SCHMEN_MASK                                                                    0x00000008L
#define THM_GPIO_PROCHOT_CTRL__S0_MASK                                                                        0x00000010L
#define THM_GPIO_PROCHOT_CTRL__S1_MASK                                                                        0x00000020L
#define THM_GPIO_PROCHOT_CTRL__RXEN_MASK                                                                      0x00000040L
#define THM_GPIO_PROCHOT_CTRL__RXSEL0_MASK                                                                    0x00000080L
#define THM_GPIO_PROCHOT_CTRL__RXSEL1_MASK                                                                    0x00000100L
#define THM_GPIO_PROCHOT_CTRL__OE_OVERRIDE_MASK                                                               0x00010000L
#define THM_GPIO_PROCHOT_CTRL__OE_MASK                                                                        0x00020000L
#define THM_GPIO_PROCHOT_CTRL__A_OVERRIDE_MASK                                                                0x00040000L
#define THM_GPIO_PROCHOT_CTRL__A_MASK                                                                         0x00080000L
#define THM_GPIO_PROCHOT_CTRL__Y_MASK                                                                         0x80000000L
//THM_GPIO_THERMTRIP_CTRL
#define THM_GPIO_THERMTRIP_CTRL__TXIMPSEL__SHIFT                                                              0x0
#define THM_GPIO_THERMTRIP_CTRL__PD__SHIFT                                                                    0x1
#define THM_GPIO_THERMTRIP_CTRL__PU__SHIFT                                                                    0x2
#define THM_GPIO_THERMTRIP_CTRL__SCHMEN__SHIFT                                                                0x3
#define THM_GPIO_THERMTRIP_CTRL__S0__SHIFT                                                                    0x4
#define THM_GPIO_THERMTRIP_CTRL__S1__SHIFT                                                                    0x5
#define THM_GPIO_THERMTRIP_CTRL__RXEN__SHIFT                                                                  0x6
#define THM_GPIO_THERMTRIP_CTRL__RXSEL0__SHIFT                                                                0x7
#define THM_GPIO_THERMTRIP_CTRL__RXSEL1__SHIFT                                                                0x8
#define THM_GPIO_THERMTRIP_CTRL__OE_OVERRIDE__SHIFT                                                           0x10
#define THM_GPIO_THERMTRIP_CTRL__OE__SHIFT                                                                    0x11
#define THM_GPIO_THERMTRIP_CTRL__A_OVERRIDE__SHIFT                                                            0x12
#define THM_GPIO_THERMTRIP_CTRL__A__SHIFT                                                                     0x13
#define THM_GPIO_THERMTRIP_CTRL__CTFEN__SHIFT                                                                 0x14
#define THM_GPIO_THERMTRIP_CTRL__Y__SHIFT                                                                     0x1f
#define THM_GPIO_THERMTRIP_CTRL__TXIMPSEL_MASK                                                                0x00000001L
#define THM_GPIO_THERMTRIP_CTRL__PD_MASK                                                                      0x00000002L
#define THM_GPIO_THERMTRIP_CTRL__PU_MASK                                                                      0x00000004L
#define THM_GPIO_THERMTRIP_CTRL__SCHMEN_MASK                                                                  0x00000008L
#define THM_GPIO_THERMTRIP_CTRL__S0_MASK                                                                      0x00000010L
#define THM_GPIO_THERMTRIP_CTRL__S1_MASK                                                                      0x00000020L
#define THM_GPIO_THERMTRIP_CTRL__RXEN_MASK                                                                    0x00000040L
#define THM_GPIO_THERMTRIP_CTRL__RXSEL0_MASK                                                                  0x00000080L
#define THM_GPIO_THERMTRIP_CTRL__RXSEL1_MASK                                                                  0x00000100L
#define THM_GPIO_THERMTRIP_CTRL__OE_OVERRIDE_MASK                                                             0x00010000L
#define THM_GPIO_THERMTRIP_CTRL__OE_MASK                                                                      0x00020000L
#define THM_GPIO_THERMTRIP_CTRL__A_OVERRIDE_MASK                                                              0x00040000L
#define THM_GPIO_THERMTRIP_CTRL__A_MASK                                                                       0x00080000L
#define THM_GPIO_THERMTRIP_CTRL__CTFEN_MASK                                                                   0x00100000L
#define THM_GPIO_THERMTRIP_CTRL__Y_MASK                                                                       0x80000000L
//THM_GPIO_PWM_CTRL
#define THM_GPIO_PWM_CTRL__TXIMPSEL__SHIFT                                                                    0x0
#define THM_GPIO_PWM_CTRL__PD__SHIFT                                                                          0x1
#define THM_GPIO_PWM_CTRL__PU__SHIFT                                                                          0x2
#define THM_GPIO_PWM_CTRL__SCHMEN__SHIFT                                                                      0x3
#define THM_GPIO_PWM_CTRL__S0__SHIFT                                                                          0x4
#define THM_GPIO_PWM_CTRL__S1__SHIFT                                                                          0x5
#define THM_GPIO_PWM_CTRL__RXEN__SHIFT                                                                        0x6
#define THM_GPIO_PWM_CTRL__RXSEL0__SHIFT                                                                      0x7
#define THM_GPIO_PWM_CTRL__RXSEL1__SHIFT                                                                      0x8
#define THM_GPIO_PWM_CTRL__OE_OVERRIDE__SHIFT                                                                 0x10
#define THM_GPIO_PWM_CTRL__OE__SHIFT                                                                          0x11
#define THM_GPIO_PWM_CTRL__A_OVERRIDE__SHIFT                                                                  0x12
#define THM_GPIO_PWM_CTRL__A__SHIFT                                                                           0x13
#define THM_GPIO_PWM_CTRL__Y__SHIFT                                                                           0x1f
#define THM_GPIO_PWM_CTRL__TXIMPSEL_MASK                                                                      0x00000001L
#define THM_GPIO_PWM_CTRL__PD_MASK                                                                            0x00000002L
#define THM_GPIO_PWM_CTRL__PU_MASK                                                                            0x00000004L
#define THM_GPIO_PWM_CTRL__SCHMEN_MASK                                                                        0x00000008L
#define THM_GPIO_PWM_CTRL__S0_MASK                                                                            0x00000010L
#define THM_GPIO_PWM_CTRL__S1_MASK                                                                            0x00000020L
#define THM_GPIO_PWM_CTRL__RXEN_MASK                                                                          0x00000040L
#define THM_GPIO_PWM_CTRL__RXSEL0_MASK                                                                        0x00000080L
#define THM_GPIO_PWM_CTRL__RXSEL1_MASK                                                                        0x00000100L
#define THM_GPIO_PWM_CTRL__OE_OVERRIDE_MASK                                                                   0x00010000L
#define THM_GPIO_PWM_CTRL__OE_MASK                                                                            0x00020000L
#define THM_GPIO_PWM_CTRL__A_OVERRIDE_MASK                                                                    0x00040000L
#define THM_GPIO_PWM_CTRL__A_MASK                                                                             0x00080000L
#define THM_GPIO_PWM_CTRL__Y_MASK                                                                             0x80000000L
//THM_GPIO_TACHIN_CTRL
#define THM_GPIO_TACHIN_CTRL__TXIMPSEL__SHIFT                                                                 0x0
#define THM_GPIO_TACHIN_CTRL__PD__SHIFT                                                                       0x1
#define THM_GPIO_TACHIN_CTRL__PU__SHIFT                                                                       0x2
#define THM_GPIO_TACHIN_CTRL__SCHMEN__SHIFT                                                                   0x3
#define THM_GPIO_TACHIN_CTRL__S0__SHIFT                                                                       0x4
#define THM_GPIO_TACHIN_CTRL__S1__SHIFT                                                                       0x5
#define THM_GPIO_TACHIN_CTRL__RXEN__SHIFT                                                                     0x6
#define THM_GPIO_TACHIN_CTRL__RXSEL0__SHIFT                                                                   0x7
#define THM_GPIO_TACHIN_CTRL__RXSEL1__SHIFT                                                                   0x8
#define THM_GPIO_TACHIN_CTRL__OE_OVERRIDE__SHIFT                                                              0x10
#define THM_GPIO_TACHIN_CTRL__OE__SHIFT                                                                       0x11
#define THM_GPIO_TACHIN_CTRL__A_OVERRIDE__SHIFT                                                               0x12
#define THM_GPIO_TACHIN_CTRL__A__SHIFT                                                                        0x13
#define THM_GPIO_TACHIN_CTRL__Y__SHIFT                                                                        0x1f
#define THM_GPIO_TACHIN_CTRL__TXIMPSEL_MASK                                                                   0x00000001L
#define THM_GPIO_TACHIN_CTRL__PD_MASK                                                                         0x00000002L
#define THM_GPIO_TACHIN_CTRL__PU_MASK                                                                         0x00000004L
#define THM_GPIO_TACHIN_CTRL__SCHMEN_MASK                                                                     0x00000008L
#define THM_GPIO_TACHIN_CTRL__S0_MASK                                                                         0x00000010L
#define THM_GPIO_TACHIN_CTRL__S1_MASK                                                                         0x00000020L
#define THM_GPIO_TACHIN_CTRL__RXEN_MASK                                                                       0x00000040L
#define THM_GPIO_TACHIN_CTRL__RXSEL0_MASK                                                                     0x00000080L
#define THM_GPIO_TACHIN_CTRL__RXSEL1_MASK                                                                     0x00000100L
#define THM_GPIO_TACHIN_CTRL__OE_OVERRIDE_MASK                                                                0x00010000L
#define THM_GPIO_TACHIN_CTRL__OE_MASK                                                                         0x00020000L
#define THM_GPIO_TACHIN_CTRL__A_OVERRIDE_MASK                                                                 0x00040000L
#define THM_GPIO_TACHIN_CTRL__A_MASK                                                                          0x00080000L
#define THM_GPIO_TACHIN_CTRL__Y_MASK                                                                          0x80000000L
//THM_GPIO_PUMPOUT_CTRL
#define THM_GPIO_PUMPOUT_CTRL__TXIMPSEL__SHIFT                                                                0x0
#define THM_GPIO_PUMPOUT_CTRL__PD__SHIFT                                                                      0x1
#define THM_GPIO_PUMPOUT_CTRL__PU__SHIFT                                                                      0x2
#define THM_GPIO_PUMPOUT_CTRL__SCHMEN__SHIFT                                                                  0x3
#define THM_GPIO_PUMPOUT_CTRL__S0__SHIFT                                                                      0x4
#define THM_GPIO_PUMPOUT_CTRL__S1__SHIFT                                                                      0x5
#define THM_GPIO_PUMPOUT_CTRL__RXEN__SHIFT                                                                    0x6
#define THM_GPIO_PUMPOUT_CTRL__RXSEL0__SHIFT                                                                  0x7
#define THM_GPIO_PUMPOUT_CTRL__RXSEL1__SHIFT                                                                  0x8
#define THM_GPIO_PUMPOUT_CTRL__OE_OVERRIDE__SHIFT                                                             0x10
#define THM_GPIO_PUMPOUT_CTRL__OE__SHIFT                                                                      0x11
#define THM_GPIO_PUMPOUT_CTRL__A_OVERRIDE__SHIFT                                                              0x12
#define THM_GPIO_PUMPOUT_CTRL__A__SHIFT                                                                       0x13
#define THM_GPIO_PUMPOUT_CTRL__Y__SHIFT                                                                       0x1f
#define THM_GPIO_PUMPOUT_CTRL__TXIMPSEL_MASK                                                                  0x00000001L
#define THM_GPIO_PUMPOUT_CTRL__PD_MASK                                                                        0x00000002L
#define THM_GPIO_PUMPOUT_CTRL__PU_MASK                                                                        0x00000004L
#define THM_GPIO_PUMPOUT_CTRL__SCHMEN_MASK                                                                    0x00000008L
#define THM_GPIO_PUMPOUT_CTRL__S0_MASK                                                                        0x00000010L
#define THM_GPIO_PUMPOUT_CTRL__S1_MASK                                                                        0x00000020L
#define THM_GPIO_PUMPOUT_CTRL__RXEN_MASK                                                                      0x00000040L
#define THM_GPIO_PUMPOUT_CTRL__RXSEL0_MASK                                                                    0x00000080L
#define THM_GPIO_PUMPOUT_CTRL__RXSEL1_MASK                                                                    0x00000100L
#define THM_GPIO_PUMPOUT_CTRL__OE_OVERRIDE_MASK                                                               0x00010000L
#define THM_GPIO_PUMPOUT_CTRL__OE_MASK                                                                        0x00020000L
#define THM_GPIO_PUMPOUT_CTRL__A_OVERRIDE_MASK                                                                0x00040000L
#define THM_GPIO_PUMPOUT_CTRL__A_MASK                                                                         0x00080000L
#define THM_GPIO_PUMPOUT_CTRL__Y_MASK                                                                         0x80000000L
//THM_GPIO_PUMPIN_CTRL
#define THM_GPIO_PUMPIN_CTRL__TXIMPSEL__SHIFT                                                                 0x0
#define THM_GPIO_PUMPIN_CTRL__PD__SHIFT                                                                       0x1
#define THM_GPIO_PUMPIN_CTRL__PU__SHIFT                                                                       0x2
#define THM_GPIO_PUMPIN_CTRL__SCHMEN__SHIFT                                                                   0x3
#define THM_GPIO_PUMPIN_CTRL__S0__SHIFT                                                                       0x4
#define THM_GPIO_PUMPIN_CTRL__S1__SHIFT                                                                       0x5
#define THM_GPIO_PUMPIN_CTRL__RXEN__SHIFT                                                                     0x6
#define THM_GPIO_PUMPIN_CTRL__RXSEL0__SHIFT                                                                   0x7
#define THM_GPIO_PUMPIN_CTRL__RXSEL1__SHIFT                                                                   0x8
#define THM_GPIO_PUMPIN_CTRL__OE_OVERRIDE__SHIFT                                                              0x10
#define THM_GPIO_PUMPIN_CTRL__OE__SHIFT                                                                       0x11
#define THM_GPIO_PUMPIN_CTRL__A_OVERRIDE__SHIFT                                                               0x12
#define THM_GPIO_PUMPIN_CTRL__A__SHIFT                                                                        0x13
#define THM_GPIO_PUMPIN_CTRL__Y__SHIFT                                                                        0x1f
#define THM_GPIO_PUMPIN_CTRL__TXIMPSEL_MASK                                                                   0x00000001L
#define THM_GPIO_PUMPIN_CTRL__PD_MASK                                                                         0x00000002L
#define THM_GPIO_PUMPIN_CTRL__PU_MASK                                                                         0x00000004L
#define THM_GPIO_PUMPIN_CTRL__SCHMEN_MASK                                                                     0x00000008L
#define THM_GPIO_PUMPIN_CTRL__S0_MASK                                                                         0x00000010L
#define THM_GPIO_PUMPIN_CTRL__S1_MASK                                                                         0x00000020L
#define THM_GPIO_PUMPIN_CTRL__RXEN_MASK                                                                       0x00000040L
#define THM_GPIO_PUMPIN_CTRL__RXSEL0_MASK                                                                     0x00000080L
#define THM_GPIO_PUMPIN_CTRL__RXSEL1_MASK                                                                     0x00000100L
#define THM_GPIO_PUMPIN_CTRL__OE_OVERRIDE_MASK                                                                0x00010000L
#define THM_GPIO_PUMPIN_CTRL__OE_MASK                                                                         0x00020000L
#define THM_GPIO_PUMPIN_CTRL__A_OVERRIDE_MASK                                                                 0x00040000L
#define THM_GPIO_PUMPIN_CTRL__A_MASK                                                                          0x00080000L
#define THM_GPIO_PUMPIN_CTRL__Y_MASK                                                                          0x80000000L
//THM_THERMAL_INT_ENA
#define THM_THERMAL_INT_ENA__THERM_INTH_SET__SHIFT                                                            0x0
#define THM_THERMAL_INT_ENA__THERM_INTL_SET__SHIFT                                                            0x1
#define THM_THERMAL_INT_ENA__THERM_TRIGGER_SET__SHIFT                                                         0x2
#define THM_THERMAL_INT_ENA__THERM_INTH_CLR__SHIFT                                                            0x3
#define THM_THERMAL_INT_ENA__THERM_INTL_CLR__SHIFT                                                            0x4
#define THM_THERMAL_INT_ENA__THERM_TRIGGER_CLR__SHIFT                                                         0x5
#define THM_THERMAL_INT_ENA__THERM_INTH_SET_MASK                                                              0x00000001L
#define THM_THERMAL_INT_ENA__THERM_INTL_SET_MASK                                                              0x00000002L
#define THM_THERMAL_INT_ENA__THERM_TRIGGER_SET_MASK                                                           0x00000004L
#define THM_THERMAL_INT_ENA__THERM_INTH_CLR_MASK                                                              0x00000008L
#define THM_THERMAL_INT_ENA__THERM_INTL_CLR_MASK                                                              0x00000010L
#define THM_THERMAL_INT_ENA__THERM_TRIGGER_CLR_MASK                                                           0x00000020L
//THM_THERMAL_INT_CTRL
#define THM_THERMAL_INT_CTRL__DIG_THERM_INTH__SHIFT                                                           0x0
#define THM_THERMAL_INT_CTRL__DIG_THERM_INTL__SHIFT                                                           0x8
#define THM_THERMAL_INT_CTRL__TEMP_THRESHOLD__SHIFT                                                           0x10
#define THM_THERMAL_INT_CTRL__THERM_INTH_MASK__SHIFT                                                          0x18
#define THM_THERMAL_INT_CTRL__THERM_INTL_MASK__SHIFT                                                          0x19
#define THM_THERMAL_INT_CTRL__THERM_TRIGGER_MASK__SHIFT                                                       0x1a
#define THM_THERMAL_INT_CTRL__THERM_PROCHOT_MASK__SHIFT                                                       0x1b
#define THM_THERMAL_INT_CTRL__THERM_IH_HW_ENA__SHIFT                                                          0x1c
#define THM_THERMAL_INT_CTRL__MAX_IH_CREDIT__SHIFT                                                            0x1d
#define THM_THERMAL_INT_CTRL__DIG_THERM_INTH_MASK                                                             0x000000FFL
#define THM_THERMAL_INT_CTRL__DIG_THERM_INTL_MASK                                                             0x0000FF00L
#define THM_THERMAL_INT_CTRL__TEMP_THRESHOLD_MASK                                                             0x00FF0000L
#define THM_THERMAL_INT_CTRL__THERM_INTH_MASK_MASK                                                            0x01000000L
#define THM_THERMAL_INT_CTRL__THERM_INTL_MASK_MASK                                                            0x02000000L
#define THM_THERMAL_INT_CTRL__THERM_TRIGGER_MASK_MASK                                                         0x04000000L
#define THM_THERMAL_INT_CTRL__THERM_PROCHOT_MASK_MASK                                                         0x08000000L
#define THM_THERMAL_INT_CTRL__THERM_IH_HW_ENA_MASK                                                            0x10000000L
#define THM_THERMAL_INT_CTRL__MAX_IH_CREDIT_MASK                                                              0xE0000000L
//THM_THERMAL_INT_STATUS
#define THM_THERMAL_INT_STATUS__THERM_INTH_DETECT__SHIFT                                                      0x0
#define THM_THERMAL_INT_STATUS__THERM_INTL_DETECT__SHIFT                                                      0x1
#define THM_THERMAL_INT_STATUS__THERM_TRIGGER_DETECT__SHIFT                                                   0x2
#define THM_THERMAL_INT_STATUS__THERM_PROCHOT_DETECT__SHIFT                                                   0x3
#define THM_THERMAL_INT_STATUS__THERM_INTH_DETECT_MASK                                                        0x00000001L
#define THM_THERMAL_INT_STATUS__THERM_INTL_DETECT_MASK                                                        0x00000002L
#define THM_THERMAL_INT_STATUS__THERM_TRIGGER_DETECT_MASK                                                     0x00000004L
#define THM_THERMAL_INT_STATUS__THERM_PROCHOT_DETECT_MASK                                                     0x00000008L
//THM_TMON0_RDIL0_DATA
#define THM_TMON0_RDIL0_DATA__Z__SHIFT                                                                        0x0
#define THM_TMON0_RDIL0_DATA__VALID__SHIFT                                                                    0xb
#define THM_TMON0_RDIL0_DATA__TEMP__SHIFT                                                                     0xc
#define THM_TMON0_RDIL0_DATA__Z_MASK                                                                          0x000007FFL
#define THM_TMON0_RDIL0_DATA__VALID_MASK                                                                      0x00000800L
#define THM_TMON0_RDIL0_DATA__TEMP_MASK                                                                       0x00FFF000L
//THM_TMON0_RDIL1_DATA
#define THM_TMON0_RDIL1_DATA__Z__SHIFT                                                                        0x0
#define THM_TMON0_RDIL1_DATA__VALID__SHIFT                                                                    0xb
#define THM_TMON0_RDIL1_DATA__TEMP__SHIFT                                                                     0xc
#define THM_TMON0_RDIL1_DATA__Z_MASK                                                                          0x000007FFL
#define THM_TMON0_RDIL1_DATA__VALID_MASK                                                                      0x00000800L
#define THM_TMON0_RDIL1_DATA__TEMP_MASK                                                                       0x00FFF000L
//THM_TMON0_RDIL2_DATA
#define THM_TMON0_RDIL2_DATA__Z__SHIFT                                                                        0x0
#define THM_TMON0_RDIL2_DATA__VALID__SHIFT                                                                    0xb
#define THM_TMON0_RDIL2_DATA__TEMP__SHIFT                                                                     0xc
#define THM_TMON0_RDIL2_DATA__Z_MASK                                                                          0x000007FFL
#define THM_TMON0_RDIL2_DATA__VALID_MASK                                                                      0x00000800L
#define THM_TMON0_RDIL2_DATA__TEMP_MASK                                                                       0x00FFF000L
//THM_TMON0_RDIL3_DATA
#define THM_TMON0_RDIL3_DATA__Z__SHIFT                                                                        0x0
#define THM_TMON0_RDIL3_DATA__VALID__SHIFT                                                                    0xb
#define THM_TMON0_RDIL3_DATA__TEMP__SHIFT                                                                     0xc
#define THM_TMON0_RDIL3_DATA__Z_MASK                                                                          0x000007FFL
#define THM_TMON0_RDIL3_DATA__VALID_MASK                                                                      0x00000800L
#define THM_TMON0_RDIL3_DATA__TEMP_MASK                                                                       0x00FFF000L
//THM_TMON0_RDIL4_DATA
#define THM_TMON0_RDIL4_DATA__Z__SHIFT                                                                        0x0
#define THM_TMON0_RDIL4_DATA__VALID__SHIFT                                                                    0xb
#define THM_TMON0_RDIL4_DATA__TEMP__SHIFT                                                                     0xc
#define THM_TMON0_RDIL4_DATA__Z_MASK                                                                          0x000007FFL
#define THM_TMON0_RDIL4_DATA__VALID_MASK                                                                      0x00000800L
#define THM_TMON0_RDIL4_DATA__TEMP_MASK                                                                       0x00FFF000L
//THM_TMON0_RDIL5_DATA
#define THM_TMON0_RDIL5_DATA__Z__SHIFT                                                                        0x0
#define THM_TMON0_RDIL5_DATA__VALID__SHIFT                                                                    0xb
#define THM_TMON0_RDIL5_DATA__TEMP__SHIFT                                                                     0xc
#define THM_TMON0_RDIL5_DATA__Z_MASK                                                                          0x000007FFL
#define THM_TMON0_RDIL5_DATA__VALID_MASK                                                                      0x00000800L
#define THM_TMON0_RDIL5_DATA__TEMP_MASK                                                                       0x00FFF000L
//THM_TMON0_RDIL6_DATA
#define THM_TMON0_RDIL6_DATA__Z__SHIFT                                                                        0x0
#define THM_TMON0_RDIL6_DATA__VALID__SHIFT                                                                    0xb
#define THM_TMON0_RDIL6_DATA__TEMP__SHIFT                                                                     0xc
#define THM_TMON0_RDIL6_DATA__Z_MASK                                                                          0x000007FFL
#define THM_TMON0_RDIL6_DATA__VALID_MASK                                                                      0x00000800L
#define THM_TMON0_RDIL6_DATA__TEMP_MASK                                                                       0x00FFF000L
//THM_TMON0_RDIL7_DATA
#define THM_TMON0_RDIL7_DATA__Z__SHIFT                                                                        0x0
#define THM_TMON0_RDIL7_DATA__VALID__SHIFT                                                                    0xb
#define THM_TMON0_RDIL7_DATA__TEMP__SHIFT                                                                     0xc
#define THM_TMON0_RDIL7_DATA__Z_MASK                                                                          0x000007FFL
#define THM_TMON0_RDIL7_DATA__VALID_MASK                                                                      0x00000800L
#define THM_TMON0_RDIL7_DATA__TEMP_MASK                                                                       0x00FFF000L
//THM_TMON0_RDIL8_DATA
#define THM_TMON0_RDIL8_DATA__Z__SHIFT                                                                        0x0
#define THM_TMON0_RDIL8_DATA__VALID__SHIFT                                                                    0xb
#define THM_TMON0_RDIL8_DATA__TEMP__SHIFT                                                                     0xc
#define THM_TMON0_RDIL8_DATA__Z_MASK                                                                          0x000007FFL
#define THM_TMON0_RDIL8_DATA__VALID_MASK                                                                      0x00000800L
#define THM_TMON0_RDIL8_DATA__TEMP_MASK                                                                       0x00FFF000L
//THM_TMON0_RDIL9_DATA
#define THM_TMON0_RDIL9_DATA__Z__SHIFT                                                                        0x0
#define THM_TMON0_RDIL9_DATA__VALID__SHIFT                                                                    0xb
#define THM_TMON0_RDIL9_DATA__TEMP__SHIFT                                                                     0xc
#define THM_TMON0_RDIL9_DATA__Z_MASK                                                                          0x000007FFL
#define THM_TMON0_RDIL9_DATA__VALID_MASK                                                                      0x00000800L
#define THM_TMON0_RDIL9_DATA__TEMP_MASK                                                                       0x00FFF000L
//THM_TMON0_RDIL10_DATA
#define THM_TMON0_RDIL10_DATA__Z__SHIFT                                                                       0x0
#define THM_TMON0_RDIL10_DATA__VALID__SHIFT                                                                   0xb
#define THM_TMON0_RDIL10_DATA__TEMP__SHIFT                                                                    0xc
#define THM_TMON0_RDIL10_DATA__Z_MASK                                                                         0x000007FFL
#define THM_TMON0_RDIL10_DATA__VALID_MASK                                                                     0x00000800L
#define THM_TMON0_RDIL10_DATA__TEMP_MASK                                                                      0x00FFF000L
//THM_TMON0_RDIL11_DATA
#define THM_TMON0_RDIL11_DATA__Z__SHIFT                                                                       0x0
#define THM_TMON0_RDIL11_DATA__VALID__SHIFT                                                                   0xb
#define THM_TMON0_RDIL11_DATA__TEMP__SHIFT                                                                    0xc
#define THM_TMON0_RDIL11_DATA__Z_MASK                                                                         0x000007FFL
#define THM_TMON0_RDIL11_DATA__VALID_MASK                                                                     0x00000800L
#define THM_TMON0_RDIL11_DATA__TEMP_MASK                                                                      0x00FFF000L
//THM_TMON0_RDIL12_DATA
#define THM_TMON0_RDIL12_DATA__Z__SHIFT                                                                       0x0
#define THM_TMON0_RDIL12_DATA__VALID__SHIFT                                                                   0xb
#define THM_TMON0_RDIL12_DATA__TEMP__SHIFT                                                                    0xc
#define THM_TMON0_RDIL12_DATA__Z_MASK                                                                         0x000007FFL
#define THM_TMON0_RDIL12_DATA__VALID_MASK                                                                     0x00000800L
#define THM_TMON0_RDIL12_DATA__TEMP_MASK                                                                      0x00FFF000L
//THM_TMON0_RDIL13_DATA
#define THM_TMON0_RDIL13_DATA__Z__SHIFT                                                                       0x0
#define THM_TMON0_RDIL13_DATA__VALID__SHIFT                                                                   0xb
#define THM_TMON0_RDIL13_DATA__TEMP__SHIFT                                                                    0xc
#define THM_TMON0_RDIL13_DATA__Z_MASK                                                                         0x000007FFL
#define THM_TMON0_RDIL13_DATA__VALID_MASK                                                                     0x00000800L
#define THM_TMON0_RDIL13_DATA__TEMP_MASK                                                                      0x00FFF000L
//THM_TMON0_RDIL14_DATA
#define THM_TMON0_RDIL14_DATA__Z__SHIFT                                                                       0x0
#define THM_TMON0_RDIL14_DATA__VALID__SHIFT                                                                   0xb
#define THM_TMON0_RDIL14_DATA__TEMP__SHIFT                                                                    0xc
#define THM_TMON0_RDIL14_DATA__Z_MASK                                                                         0x000007FFL
#define THM_TMON0_RDIL14_DATA__VALID_MASK                                                                     0x00000800L
#define THM_TMON0_RDIL14_DATA__TEMP_MASK                                                                      0x00FFF000L
//THM_TMON0_RDIL15_DATA
#define THM_TMON0_RDIL15_DATA__Z__SHIFT                                                                       0x0
#define THM_TMON0_RDIL15_DATA__VALID__SHIFT                                                                   0xb
#define THM_TMON0_RDIL15_DATA__TEMP__SHIFT                                                                    0xc
#define THM_TMON0_RDIL15_DATA__Z_MASK                                                                         0x000007FFL
#define THM_TMON0_RDIL15_DATA__VALID_MASK                                                                     0x00000800L
#define THM_TMON0_RDIL15_DATA__TEMP_MASK                                                                      0x00FFF000L
//THM_TMON0_RDIR0_DATA
#define THM_TMON0_RDIR0_DATA__Z__SHIFT                                                                        0x0
#define THM_TMON0_RDIR0_DATA__VALID__SHIFT                                                                    0xb
#define THM_TMON0_RDIR0_DATA__TEMP__SHIFT                                                                     0xc
#define THM_TMON0_RDIR0_DATA__Z_MASK                                                                          0x000007FFL
#define THM_TMON0_RDIR0_DATA__VALID_MASK                                                                      0x00000800L
#define THM_TMON0_RDIR0_DATA__TEMP_MASK                                                                       0x00FFF000L
//THM_TMON0_RDIR1_DATA
#define THM_TMON0_RDIR1_DATA__Z__SHIFT                                                                        0x0
#define THM_TMON0_RDIR1_DATA__VALID__SHIFT                                                                    0xb
#define THM_TMON0_RDIR1_DATA__TEMP__SHIFT                                                                     0xc
#define THM_TMON0_RDIR1_DATA__Z_MASK                                                                          0x000007FFL
#define THM_TMON0_RDIR1_DATA__VALID_MASK                                                                      0x00000800L
#define THM_TMON0_RDIR1_DATA__TEMP_MASK                                                                       0x00FFF000L
//THM_TMON0_RDIR2_DATA
#define THM_TMON0_RDIR2_DATA__Z__SHIFT                                                                        0x0
#define THM_TMON0_RDIR2_DATA__VALID__SHIFT                                                                    0xb
#define THM_TMON0_RDIR2_DATA__TEMP__SHIFT                                                                     0xc
#define THM_TMON0_RDIR2_DATA__Z_MASK                                                                          0x000007FFL
#define THM_TMON0_RDIR2_DATA__VALID_MASK                                                                      0x00000800L
#define THM_TMON0_RDIR2_DATA__TEMP_MASK                                                                       0x00FFF000L
//THM_TMON0_RDIR3_DATA
#define THM_TMON0_RDIR3_DATA__Z__SHIFT                                                                        0x0
#define THM_TMON0_RDIR3_DATA__VALID__SHIFT                                                                    0xb
#define THM_TMON0_RDIR3_DATA__TEMP__SHIFT                                                                     0xc
#define THM_TMON0_RDIR3_DATA__Z_MASK                                                                          0x000007FFL
#define THM_TMON0_RDIR3_DATA__VALID_MASK                                                                      0x00000800L
#define THM_TMON0_RDIR3_DATA__TEMP_MASK                                                                       0x00FFF000L
//THM_TMON0_RDIR4_DATA
#define THM_TMON0_RDIR4_DATA__Z__SHIFT                                                                        0x0
#define THM_TMON0_RDIR4_DATA__VALID__SHIFT                                                                    0xb
#define THM_TMON0_RDIR4_DATA__TEMP__SHIFT                                                                     0xc
#define THM_TMON0_RDIR4_DATA__Z_MASK                                                                          0x000007FFL
#define THM_TMON0_RDIR4_DATA__VALID_MASK                                                                      0x00000800L
#define THM_TMON0_RDIR4_DATA__TEMP_MASK                                                                       0x00FFF000L
//THM_TMON0_RDIR5_DATA
#define THM_TMON0_RDIR5_DATA__Z__SHIFT                                                                        0x0
#define THM_TMON0_RDIR5_DATA__VALID__SHIFT                                                                    0xb
#define THM_TMON0_RDIR5_DATA__TEMP__SHIFT                                                                     0xc
#define THM_TMON0_RDIR5_DATA__Z_MASK                                                                          0x000007FFL
#define THM_TMON0_RDIR5_DATA__VALID_MASK                                                                      0x00000800L
#define THM_TMON0_RDIR5_DATA__TEMP_MASK                                                                       0x00FFF000L
//THM_TMON0_RDIR6_DATA
#define THM_TMON0_RDIR6_DATA__Z__SHIFT                                                                        0x0
#define THM_TMON0_RDIR6_DATA__VALID__SHIFT                                                                    0xb
#define THM_TMON0_RDIR6_DATA__TEMP__SHIFT                                                                     0xc
#define THM_TMON0_RDIR6_DATA__Z_MASK                                                                          0x000007FFL
#define THM_TMON0_RDIR6_DATA__VALID_MASK                                                                      0x00000800L
#define THM_TMON0_RDIR6_DATA__TEMP_MASK                                                                       0x00FFF000L
//THM_TMON0_RDIR7_DATA
#define THM_TMON0_RDIR7_DATA__Z__SHIFT                                                                        0x0
#define THM_TMON0_RDIR7_DATA__VALID__SHIFT                                                                    0xb
#define THM_TMON0_RDIR7_DATA__TEMP__SHIFT                                                                     0xc
#define THM_TMON0_RDIR7_DATA__Z_MASK                                                                          0x000007FFL
#define THM_TMON0_RDIR7_DATA__VALID_MASK                                                                      0x00000800L
#define THM_TMON0_RDIR7_DATA__TEMP_MASK                                                                       0x00FFF000L
//THM_TMON0_RDIR8_DATA
#define THM_TMON0_RDIR8_DATA__Z__SHIFT                                                                        0x0
#define THM_TMON0_RDIR8_DATA__VALID__SHIFT                                                                    0xb
#define THM_TMON0_RDIR8_DATA__TEMP__SHIFT                                                                     0xc
#define THM_TMON0_RDIR8_DATA__Z_MASK                                                                          0x000007FFL
#define THM_TMON0_RDIR8_DATA__VALID_MASK                                                                      0x00000800L
#define THM_TMON0_RDIR8_DATA__TEMP_MASK                                                                       0x00FFF000L
//THM_TMON0_RDIR9_DATA
#define THM_TMON0_RDIR9_DATA__Z__SHIFT                                                                        0x0
#define THM_TMON0_RDIR9_DATA__VALID__SHIFT                                                                    0xb
#define THM_TMON0_RDIR9_DATA__TEMP__SHIFT                                                                     0xc
#define THM_TMON0_RDIR9_DATA__Z_MASK                                                                          0x000007FFL
#define THM_TMON0_RDIR9_DATA__VALID_MASK                                                                      0x00000800L
#define THM_TMON0_RDIR9_DATA__TEMP_MASK                                                                       0x00FFF000L
//THM_TMON0_RDIR10_DATA
#define THM_TMON0_RDIR10_DATA__Z__SHIFT                                                                       0x0
#define THM_TMON0_RDIR10_DATA__VALID__SHIFT                                                                   0xb
#define THM_TMON0_RDIR10_DATA__TEMP__SHIFT                                                                    0xc
#define THM_TMON0_RDIR10_DATA__Z_MASK                                                                         0x000007FFL
#define THM_TMON0_RDIR10_DATA__VALID_MASK                                                                     0x00000800L
#define THM_TMON0_RDIR10_DATA__TEMP_MASK                                                                      0x00FFF000L
//THM_TMON0_RDIR11_DATA
#define THM_TMON0_RDIR11_DATA__Z__SHIFT                                                                       0x0
#define THM_TMON0_RDIR11_DATA__VALID__SHIFT                                                                   0xb
#define THM_TMON0_RDIR11_DATA__TEMP__SHIFT                                                                    0xc
#define THM_TMON0_RDIR11_DATA__Z_MASK                                                                         0x000007FFL
#define THM_TMON0_RDIR11_DATA__VALID_MASK                                                                     0x00000800L
#define THM_TMON0_RDIR11_DATA__TEMP_MASK                                                                      0x00FFF000L
//THM_TMON0_RDIR12_DATA
#define THM_TMON0_RDIR12_DATA__Z__SHIFT                                                                       0x0
#define THM_TMON0_RDIR12_DATA__VALID__SHIFT                                                                   0xb
#define THM_TMON0_RDIR12_DATA__TEMP__SHIFT                                                                    0xc
#define THM_TMON0_RDIR12_DATA__Z_MASK                                                                         0x000007FFL
#define THM_TMON0_RDIR12_DATA__VALID_MASK                                                                     0x00000800L
#define THM_TMON0_RDIR12_DATA__TEMP_MASK                                                                      0x00FFF000L
//THM_TMON0_RDIR13_DATA
#define THM_TMON0_RDIR13_DATA__Z__SHIFT                                                                       0x0
#define THM_TMON0_RDIR13_DATA__VALID__SHIFT                                                                   0xb
#define THM_TMON0_RDIR13_DATA__TEMP__SHIFT                                                                    0xc
#define THM_TMON0_RDIR13_DATA__Z_MASK                                                                         0x000007FFL
#define THM_TMON0_RDIR13_DATA__VALID_MASK                                                                     0x00000800L
#define THM_TMON0_RDIR13_DATA__TEMP_MASK                                                                      0x00FFF000L
//THM_TMON0_RDIR14_DATA
#define THM_TMON0_RDIR14_DATA__Z__SHIFT                                                                       0x0
#define THM_TMON0_RDIR14_DATA__VALID__SHIFT                                                                   0xb
#define THM_TMON0_RDIR14_DATA__TEMP__SHIFT                                                                    0xc
#define THM_TMON0_RDIR14_DATA__Z_MASK                                                                         0x000007FFL
#define THM_TMON0_RDIR14_DATA__VALID_MASK                                                                     0x00000800L
#define THM_TMON0_RDIR14_DATA__TEMP_MASK                                                                      0x00FFF000L
//THM_TMON0_RDIR15_DATA
#define THM_TMON0_RDIR15_DATA__Z__SHIFT                                                                       0x0
#define THM_TMON0_RDIR15_DATA__VALID__SHIFT                                                                   0xb
#define THM_TMON0_RDIR15_DATA__TEMP__SHIFT                                                                    0xc
#define THM_TMON0_RDIR15_DATA__Z_MASK                                                                         0x000007FFL
#define THM_TMON0_RDIR15_DATA__VALID_MASK                                                                     0x00000800L
#define THM_TMON0_RDIR15_DATA__TEMP_MASK                                                                      0x00FFF000L
//THM_TMON0_INT_DATA
#define THM_TMON0_INT_DATA__Z__SHIFT                                                                          0x0
#define THM_TMON0_INT_DATA__VALID__SHIFT                                                                      0xb
#define THM_TMON0_INT_DATA__TEMP__SHIFT                                                                       0xc
#define THM_TMON0_INT_DATA__Z_MASK                                                                            0x000007FFL
#define THM_TMON0_INT_DATA__VALID_MASK                                                                        0x00000800L
#define THM_TMON0_INT_DATA__TEMP_MASK                                                                         0x00FFF000L
//THM_TMON0_DEBUG
#define THM_TMON0_DEBUG__DEBUG_RDI__SHIFT                                                                     0x0
#define THM_TMON0_DEBUG__DEBUG_Z__SHIFT                                                                       0x5
#define THM_TMON0_DEBUG__DEBUG_RDI_MASK                                                                       0x0000001FL
#define THM_TMON0_DEBUG__DEBUG_Z_MASK                                                                         0x0000FFE0L
//THM_TMON1_RDIL0_DATA
#define THM_TMON1_RDIL0_DATA__Z__SHIFT                                                                        0x0
#define THM_TMON1_RDIL0_DATA__VALID__SHIFT                                                                    0xb
#define THM_TMON1_RDIL0_DATA__TEMP__SHIFT                                                                     0xc
#define THM_TMON1_RDIL0_DATA__Z_MASK                                                                          0x000007FFL
#define THM_TMON1_RDIL0_DATA__VALID_MASK                                                                      0x00000800L
#define THM_TMON1_RDIL0_DATA__TEMP_MASK                                                                       0x00FFF000L
//THM_TMON1_RDIL1_DATA
#define THM_TMON1_RDIL1_DATA__Z__SHIFT                                                                        0x0
#define THM_TMON1_RDIL1_DATA__VALID__SHIFT                                                                    0xb
#define THM_TMON1_RDIL1_DATA__TEMP__SHIFT                                                                     0xc
#define THM_TMON1_RDIL1_DATA__Z_MASK                                                                          0x000007FFL
#define THM_TMON1_RDIL1_DATA__VALID_MASK                                                                      0x00000800L
#define THM_TMON1_RDIL1_DATA__TEMP_MASK                                                                       0x00FFF000L
//THM_TMON1_RDIL2_DATA
#define THM_TMON1_RDIL2_DATA__Z__SHIFT                                                                        0x0
#define THM_TMON1_RDIL2_DATA__VALID__SHIFT                                                                    0xb
#define THM_TMON1_RDIL2_DATA__TEMP__SHIFT                                                                     0xc
#define THM_TMON1_RDIL2_DATA__Z_MASK                                                                          0x000007FFL
#define THM_TMON1_RDIL2_DATA__VALID_MASK                                                                      0x00000800L
#define THM_TMON1_RDIL2_DATA__TEMP_MASK                                                                       0x00FFF000L
//THM_TMON1_RDIL3_DATA
#define THM_TMON1_RDIL3_DATA__Z__SHIFT                                                                        0x0
#define THM_TMON1_RDIL3_DATA__VALID__SHIFT                                                                    0xb
#define THM_TMON1_RDIL3_DATA__TEMP__SHIFT                                                                     0xc
#define THM_TMON1_RDIL3_DATA__Z_MASK                                                                          0x000007FFL
#define THM_TMON1_RDIL3_DATA__VALID_MASK                                                                      0x00000800L
#define THM_TMON1_RDIL3_DATA__TEMP_MASK                                                                       0x00FFF000L
//THM_TMON1_RDIL4_DATA
#define THM_TMON1_RDIL4_DATA__Z__SHIFT                                                                        0x0
#define THM_TMON1_RDIL4_DATA__VALID__SHIFT                                                                    0xb
#define THM_TMON1_RDIL4_DATA__TEMP__SHIFT                                                                     0xc
#define THM_TMON1_RDIL4_DATA__Z_MASK                                                                          0x000007FFL
#define THM_TMON1_RDIL4_DATA__VALID_MASK                                                                      0x00000800L
#define THM_TMON1_RDIL4_DATA__TEMP_MASK                                                                       0x00FFF000L
//THM_TMON1_RDIL5_DATA
#define THM_TMON1_RDIL5_DATA__Z__SHIFT                                                                        0x0
#define THM_TMON1_RDIL5_DATA__VALID__SHIFT                                                                    0xb
#define THM_TMON1_RDIL5_DATA__TEMP__SHIFT                                                                     0xc
#define THM_TMON1_RDIL5_DATA__Z_MASK                                                                          0x000007FFL
#define THM_TMON1_RDIL5_DATA__VALID_MASK                                                                      0x00000800L
#define THM_TMON1_RDIL5_DATA__TEMP_MASK                                                                       0x00FFF000L
//THM_TMON1_RDIL6_DATA
#define THM_TMON1_RDIL6_DATA__Z__SHIFT                                                                        0x0
#define THM_TMON1_RDIL6_DATA__VALID__SHIFT                                                                    0xb
#define THM_TMON1_RDIL6_DATA__TEMP__SHIFT                                                                     0xc
#define THM_TMON1_RDIL6_DATA__Z_MASK                                                                          0x000007FFL
#define THM_TMON1_RDIL6_DATA__VALID_MASK                                                                      0x00000800L
#define THM_TMON1_RDIL6_DATA__TEMP_MASK                                                                       0x00FFF000L
//THM_TMON1_RDIL7_DATA
#define THM_TMON1_RDIL7_DATA__Z__SHIFT                                                                        0x0
#define THM_TMON1_RDIL7_DATA__VALID__SHIFT                                                                    0xb
#define THM_TMON1_RDIL7_DATA__TEMP__SHIFT                                                                     0xc
#define THM_TMON1_RDIL7_DATA__Z_MASK                                                                          0x000007FFL
#define THM_TMON1_RDIL7_DATA__VALID_MASK                                                                      0x00000800L
#define THM_TMON1_RDIL7_DATA__TEMP_MASK                                                                       0x00FFF000L
//THM_TMON1_RDIL8_DATA
#define THM_TMON1_RDIL8_DATA__Z__SHIFT                                                                        0x0
#define THM_TMON1_RDIL8_DATA__VALID__SHIFT                                                                    0xb
#define THM_TMON1_RDIL8_DATA__TEMP__SHIFT                                                                     0xc
#define THM_TMON1_RDIL8_DATA__Z_MASK                                                                          0x000007FFL
#define THM_TMON1_RDIL8_DATA__VALID_MASK                                                                      0x00000800L
#define THM_TMON1_RDIL8_DATA__TEMP_MASK                                                                       0x00FFF000L
//THM_TMON1_RDIL9_DATA
#define THM_TMON1_RDIL9_DATA__Z__SHIFT                                                                        0x0
#define THM_TMON1_RDIL9_DATA__VALID__SHIFT                                                                    0xb
#define THM_TMON1_RDIL9_DATA__TEMP__SHIFT                                                                     0xc
#define THM_TMON1_RDIL9_DATA__Z_MASK                                                                          0x000007FFL
#define THM_TMON1_RDIL9_DATA__VALID_MASK                                                                      0x00000800L
#define THM_TMON1_RDIL9_DATA__TEMP_MASK                                                                       0x00FFF000L
//THM_TMON1_RDIL10_DATA
#define THM_TMON1_RDIL10_DATA__Z__SHIFT                                                                       0x0
#define THM_TMON1_RDIL10_DATA__VALID__SHIFT                                                                   0xb
#define THM_TMON1_RDIL10_DATA__TEMP__SHIFT                                                                    0xc
#define THM_TMON1_RDIL10_DATA__Z_MASK                                                                         0x000007FFL
#define THM_TMON1_RDIL10_DATA__VALID_MASK                                                                     0x00000800L
#define THM_TMON1_RDIL10_DATA__TEMP_MASK                                                                      0x00FFF000L
//THM_TMON1_RDIL11_DATA
#define THM_TMON1_RDIL11_DATA__Z__SHIFT                                                                       0x0
#define THM_TMON1_RDIL11_DATA__VALID__SHIFT                                                                   0xb
#define THM_TMON1_RDIL11_DATA__TEMP__SHIFT                                                                    0xc
#define THM_TMON1_RDIL11_DATA__Z_MASK                                                                         0x000007FFL
#define THM_TMON1_RDIL11_DATA__VALID_MASK                                                                     0x00000800L
#define THM_TMON1_RDIL11_DATA__TEMP_MASK                                                                      0x00FFF000L
//THM_TMON1_RDIL12_DATA
#define THM_TMON1_RDIL12_DATA__Z__SHIFT                                                                       0x0
#define THM_TMON1_RDIL12_DATA__VALID__SHIFT                                                                   0xb
#define THM_TMON1_RDIL12_DATA__TEMP__SHIFT                                                                    0xc
#define THM_TMON1_RDIL12_DATA__Z_MASK                                                                         0x000007FFL
#define THM_TMON1_RDIL12_DATA__VALID_MASK                                                                     0x00000800L
#define THM_TMON1_RDIL12_DATA__TEMP_MASK                                                                      0x00FFF000L
//THM_TMON1_RDIL13_DATA
#define THM_TMON1_RDIL13_DATA__Z__SHIFT                                                                       0x0
#define THM_TMON1_RDIL13_DATA__VALID__SHIFT                                                                   0xb
#define THM_TMON1_RDIL13_DATA__TEMP__SHIFT                                                                    0xc
#define THM_TMON1_RDIL13_DATA__Z_MASK                                                                         0x000007FFL
#define THM_TMON1_RDIL13_DATA__VALID_MASK                                                                     0x00000800L
#define THM_TMON1_RDIL13_DATA__TEMP_MASK                                                                      0x00FFF000L
//THM_TMON1_RDIL14_DATA
#define THM_TMON1_RDIL14_DATA__Z__SHIFT                                                                       0x0
#define THM_TMON1_RDIL14_DATA__VALID__SHIFT                                                                   0xb
#define THM_TMON1_RDIL14_DATA__TEMP__SHIFT                                                                    0xc
#define THM_TMON1_RDIL14_DATA__Z_MASK                                                                         0x000007FFL
#define THM_TMON1_RDIL14_DATA__VALID_MASK                                                                     0x00000800L
#define THM_TMON1_RDIL14_DATA__TEMP_MASK                                                                      0x00FFF000L
//THM_TMON1_RDIL15_DATA
#define THM_TMON1_RDIL15_DATA__Z__SHIFT                                                                       0x0
#define THM_TMON1_RDIL15_DATA__VALID__SHIFT                                                                   0xb
#define THM_TMON1_RDIL15_DATA__TEMP__SHIFT                                                                    0xc
#define THM_TMON1_RDIL15_DATA__Z_MASK                                                                         0x000007FFL
#define THM_TMON1_RDIL15_DATA__VALID_MASK                                                                     0x00000800L
#define THM_TMON1_RDIL15_DATA__TEMP_MASK                                                                      0x00FFF000L
//THM_TMON1_RDIR0_DATA
#define THM_TMON1_RDIR0_DATA__Z__SHIFT                                                                        0x0
#define THM_TMON1_RDIR0_DATA__VALID__SHIFT                                                                    0xb
#define THM_TMON1_RDIR0_DATA__TEMP__SHIFT                                                                     0xc
#define THM_TMON1_RDIR0_DATA__Z_MASK                                                                          0x000007FFL
#define THM_TMON1_RDIR0_DATA__VALID_MASK                                                                      0x00000800L
#define THM_TMON1_RDIR0_DATA__TEMP_MASK                                                                       0x00FFF000L
//THM_TMON1_RDIR1_DATA
#define THM_TMON1_RDIR1_DATA__Z__SHIFT                                                                        0x0
#define THM_TMON1_RDIR1_DATA__VALID__SHIFT                                                                    0xb
#define THM_TMON1_RDIR1_DATA__TEMP__SHIFT                                                                     0xc
#define THM_TMON1_RDIR1_DATA__Z_MASK                                                                          0x000007FFL
#define THM_TMON1_RDIR1_DATA__VALID_MASK                                                                      0x00000800L
#define THM_TMON1_RDIR1_DATA__TEMP_MASK                                                                       0x00FFF000L
//THM_TMON1_RDIR2_DATA
#define THM_TMON1_RDIR2_DATA__Z__SHIFT                                                                        0x0
#define THM_TMON1_RDIR2_DATA__VALID__SHIFT                                                                    0xb
#define THM_TMON1_RDIR2_DATA__TEMP__SHIFT                                                                     0xc
#define THM_TMON1_RDIR2_DATA__Z_MASK                                                                          0x000007FFL
#define THM_TMON1_RDIR2_DATA__VALID_MASK                                                                      0x00000800L
#define THM_TMON1_RDIR2_DATA__TEMP_MASK                                                                       0x00FFF000L
//THM_TMON1_RDIR3_DATA
#define THM_TMON1_RDIR3_DATA__Z__SHIFT                                                                        0x0
#define THM_TMON1_RDIR3_DATA__VALID__SHIFT                                                                    0xb
#define THM_TMON1_RDIR3_DATA__TEMP__SHIFT                                                                     0xc
#define THM_TMON1_RDIR3_DATA__Z_MASK                                                                          0x000007FFL
#define THM_TMON1_RDIR3_DATA__VALID_MASK                                                                      0x00000800L
#define THM_TMON1_RDIR3_DATA__TEMP_MASK                                                                       0x00FFF000L
//THM_TMON1_RDIR4_DATA
#define THM_TMON1_RDIR4_DATA__Z__SHIFT                                                                        0x0
#define THM_TMON1_RDIR4_DATA__VALID__SHIFT                                                                    0xb
#define THM_TMON1_RDIR4_DATA__TEMP__SHIFT                                                                     0xc
#define THM_TMON1_RDIR4_DATA__Z_MASK                                                                          0x000007FFL
#define THM_TMON1_RDIR4_DATA__VALID_MASK                                                                      0x00000800L
#define THM_TMON1_RDIR4_DATA__TEMP_MASK                                                                       0x00FFF000L
//THM_TMON1_RDIR5_DATA
#define THM_TMON1_RDIR5_DATA__Z__SHIFT                                                                        0x0
#define THM_TMON1_RDIR5_DATA__VALID__SHIFT                                                                    0xb
#define THM_TMON1_RDIR5_DATA__TEMP__SHIFT                                                                     0xc
#define THM_TMON1_RDIR5_DATA__Z_MASK                                                                          0x000007FFL
#define THM_TMON1_RDIR5_DATA__VALID_MASK                                                                      0x00000800L
#define THM_TMON1_RDIR5_DATA__TEMP_MASK                                                                       0x00FFF000L
//THM_TMON1_RDIR6_DATA
#define THM_TMON1_RDIR6_DATA__Z__SHIFT                                                                        0x0
#define THM_TMON1_RDIR6_DATA__VALID__SHIFT                                                                    0xb
#define THM_TMON1_RDIR6_DATA__TEMP__SHIFT                                                                     0xc
#define THM_TMON1_RDIR6_DATA__Z_MASK                                                                          0x000007FFL
#define THM_TMON1_RDIR6_DATA__VALID_MASK                                                                      0x00000800L
#define THM_TMON1_RDIR6_DATA__TEMP_MASK                                                                       0x00FFF000L
//THM_TMON1_RDIR7_DATA
#define THM_TMON1_RDIR7_DATA__Z__SHIFT                                                                        0x0
#define THM_TMON1_RDIR7_DATA__VALID__SHIFT                                                                    0xb
#define THM_TMON1_RDIR7_DATA__TEMP__SHIFT                                                                     0xc
#define THM_TMON1_RDIR7_DATA__Z_MASK                                                                          0x000007FFL
#define THM_TMON1_RDIR7_DATA__VALID_MASK                                                                      0x00000800L
#define THM_TMON1_RDIR7_DATA__TEMP_MASK                                                                       0x00FFF000L
//THM_TMON1_RDIR8_DATA
#define THM_TMON1_RDIR8_DATA__Z__SHIFT                                                                        0x0
#define THM_TMON1_RDIR8_DATA__VALID__SHIFT                                                                    0xb
#define THM_TMON1_RDIR8_DATA__TEMP__SHIFT                                                                     0xc
#define THM_TMON1_RDIR8_DATA__Z_MASK                                                                          0x000007FFL
#define THM_TMON1_RDIR8_DATA__VALID_MASK                                                                      0x00000800L
#define THM_TMON1_RDIR8_DATA__TEMP_MASK                                                                       0x00FFF000L
//THM_TMON1_RDIR9_DATA
#define THM_TMON1_RDIR9_DATA__Z__SHIFT                                                                        0x0
#define THM_TMON1_RDIR9_DATA__VALID__SHIFT                                                                    0xb
#define THM_TMON1_RDIR9_DATA__TEMP__SHIFT                                                                     0xc
#define THM_TMON1_RDIR9_DATA__Z_MASK                                                                          0x000007FFL
#define THM_TMON1_RDIR9_DATA__VALID_MASK                                                                      0x00000800L
#define THM_TMON1_RDIR9_DATA__TEMP_MASK                                                                       0x00FFF000L
//THM_TMON1_RDIR10_DATA
#define THM_TMON1_RDIR10_DATA__Z__SHIFT                                                                       0x0
#define THM_TMON1_RDIR10_DATA__VALID__SHIFT                                                                   0xb
#define THM_TMON1_RDIR10_DATA__TEMP__SHIFT                                                                    0xc
#define THM_TMON1_RDIR10_DATA__Z_MASK                                                                         0x000007FFL
#define THM_TMON1_RDIR10_DATA__VALID_MASK                                                                     0x00000800L
#define THM_TMON1_RDIR10_DATA__TEMP_MASK                                                                      0x00FFF000L
//THM_TMON1_RDIR11_DATA
#define THM_TMON1_RDIR11_DATA__Z__SHIFT                                                                       0x0
#define THM_TMON1_RDIR11_DATA__VALID__SHIFT                                                                   0xb
#define THM_TMON1_RDIR11_DATA__TEMP__SHIFT                                                                    0xc
#define THM_TMON1_RDIR11_DATA__Z_MASK                                                                         0x000007FFL
#define THM_TMON1_RDIR11_DATA__VALID_MASK                                                                     0x00000800L
#define THM_TMON1_RDIR11_DATA__TEMP_MASK                                                                      0x00FFF000L
//THM_TMON1_RDIR12_DATA
#define THM_TMON1_RDIR12_DATA__Z__SHIFT                                                                       0x0
#define THM_TMON1_RDIR12_DATA__VALID__SHIFT                                                                   0xb
#define THM_TMON1_RDIR12_DATA__TEMP__SHIFT                                                                    0xc
#define THM_TMON1_RDIR12_DATA__Z_MASK                                                                         0x000007FFL
#define THM_TMON1_RDIR12_DATA__VALID_MASK                                                                     0x00000800L
#define THM_TMON1_RDIR12_DATA__TEMP_MASK                                                                      0x00FFF000L
//THM_TMON1_RDIR13_DATA
#define THM_TMON1_RDIR13_DATA__Z__SHIFT                                                                       0x0
#define THM_TMON1_RDIR13_DATA__VALID__SHIFT                                                                   0xb
#define THM_TMON1_RDIR13_DATA__TEMP__SHIFT                                                                    0xc
#define THM_TMON1_RDIR13_DATA__Z_MASK                                                                         0x000007FFL
#define THM_TMON1_RDIR13_DATA__VALID_MASK                                                                     0x00000800L
#define THM_TMON1_RDIR13_DATA__TEMP_MASK                                                                      0x00FFF000L
//THM_TMON1_RDIR14_DATA
#define THM_TMON1_RDIR14_DATA__Z__SHIFT                                                                       0x0
#define THM_TMON1_RDIR14_DATA__VALID__SHIFT                                                                   0xb
#define THM_TMON1_RDIR14_DATA__TEMP__SHIFT                                                                    0xc
#define THM_TMON1_RDIR14_DATA__Z_MASK                                                                         0x000007FFL
#define THM_TMON1_RDIR14_DATA__VALID_MASK                                                                     0x00000800L
#define THM_TMON1_RDIR14_DATA__TEMP_MASK                                                                      0x00FFF000L
//THM_TMON1_RDIR15_DATA
#define THM_TMON1_RDIR15_DATA__Z__SHIFT                                                                       0x0
#define THM_TMON1_RDIR15_DATA__VALID__SHIFT                                                                   0xb
#define THM_TMON1_RDIR15_DATA__TEMP__SHIFT                                                                    0xc
#define THM_TMON1_RDIR15_DATA__Z_MASK                                                                         0x000007FFL
#define THM_TMON1_RDIR15_DATA__VALID_MASK                                                                     0x00000800L
#define THM_TMON1_RDIR15_DATA__TEMP_MASK                                                                      0x00FFF000L
//THM_TMON1_INT_DATA
#define THM_TMON1_INT_DATA__Z__SHIFT                                                                          0x0
#define THM_TMON1_INT_DATA__VALID__SHIFT                                                                      0xb
#define THM_TMON1_INT_DATA__TEMP__SHIFT                                                                       0xc
#define THM_TMON1_INT_DATA__Z_MASK                                                                            0x000007FFL
#define THM_TMON1_INT_DATA__VALID_MASK                                                                        0x00000800L
#define THM_TMON1_INT_DATA__TEMP_MASK                                                                         0x00FFF000L
//THM_TMON1_DEBUG
#define THM_TMON1_DEBUG__DEBUG_RDI__SHIFT                                                                     0x0
#define THM_TMON1_DEBUG__DEBUG_Z__SHIFT                                                                       0x5
#define THM_TMON1_DEBUG__DEBUG_RDI_MASK                                                                       0x0000001FL
#define THM_TMON1_DEBUG__DEBUG_Z_MASK                                                                         0x0000FFE0L
//THM_DIE1_TEMP
#define THM_DIE1_TEMP__TEMP__SHIFT                                                                            0x0
#define THM_DIE1_TEMP__VALID__SHIFT                                                                           0xb
#define THM_DIE1_TEMP__TEMP_MASK                                                                              0x000007FFL
#define THM_DIE1_TEMP__VALID_MASK                                                                             0x00000800L
//THM_DIE2_TEMP
#define THM_DIE2_TEMP__TEMP__SHIFT                                                                            0x0
#define THM_DIE2_TEMP__VALID__SHIFT                                                                           0xb
#define THM_DIE2_TEMP__TEMP_MASK                                                                              0x000007FFL
#define THM_DIE2_TEMP__VALID_MASK                                                                             0x00000800L
//THM_DIE3_TEMP
#define THM_DIE3_TEMP__TEMP__SHIFT                                                                            0x0
#define THM_DIE3_TEMP__VALID__SHIFT                                                                           0xb
#define THM_DIE3_TEMP__TEMP_MASK                                                                              0x000007FFL
#define THM_DIE3_TEMP__VALID_MASK                                                                             0x00000800L
//CG_MULT_THERMAL_CTRL
#define CG_MULT_THERMAL_CTRL__TS_FILTER__SHIFT                                                                0x0
#define CG_MULT_THERMAL_CTRL__UNUSED__SHIFT                                                                   0x4
#define CG_MULT_THERMAL_CTRL__THERMAL_RANGE_RST__SHIFT                                                        0x9
#define CG_MULT_THERMAL_CTRL__TEMP_SEL__SHIFT                                                                 0x14
#define CG_MULT_THERMAL_CTRL__TS_FILTER_MASK                                                                  0x0000000FL
#define CG_MULT_THERMAL_CTRL__UNUSED_MASK                                                                     0x000001F0L
#define CG_MULT_THERMAL_CTRL__THERMAL_RANGE_RST_MASK                                                          0x00000200L
#define CG_MULT_THERMAL_CTRL__TEMP_SEL_MASK                                                                   0x0FF00000L
//CG_MULT_THERMAL_STATUS
#define CG_MULT_THERMAL_STATUS__ASIC_MAX_TEMP__SHIFT                                                          0x0
#define CG_MULT_THERMAL_STATUS__CTF_TEMP__SHIFT                                                               0x9
#define CG_MULT_THERMAL_STATUS__ASIC_MAX_TEMP_MASK                                                            0x000001FFL
#define CG_MULT_THERMAL_STATUS__CTF_TEMP_MASK                                                                 0x0003FE00L
//THM_TMON0_COEFF
#define THM_TMON0_COEFF__C_OFFSET__SHIFT                                                                      0x0
#define THM_TMON0_COEFF__D__SHIFT                                                                             0xb
#define THM_TMON0_COEFF__C_OFFSET_MASK                                                                        0x000007FFL
#define THM_TMON0_COEFF__D_MASK                                                                               0x0003F800L
//THM_TMON1_COEFF
#define THM_TMON1_COEFF__C_OFFSET__SHIFT                                                                      0x0
#define THM_TMON1_COEFF__D__SHIFT                                                                             0xb
#define THM_TMON1_COEFF__C_OFFSET_MASK                                                                        0x000007FFL
#define THM_TMON1_COEFF__D_MASK                                                                               0x0003F800L
//CG_FDO_CTRL0
#define CG_FDO_CTRL0__FDO_STATIC_DUTY__SHIFT                                                                  0x0
#define CG_FDO_CTRL0__FAN_SPINUP_DUTY__SHIFT                                                                  0x8
#define CG_FDO_CTRL0__FDO_PWM_MANUAL__SHIFT                                                                   0x10
#define CG_FDO_CTRL0__FDO_PWM_HYSTER__SHIFT                                                                   0x11
#define CG_FDO_CTRL0__FDO_PWM_RAMP_EN__SHIFT                                                                  0x17
#define CG_FDO_CTRL0__FDO_PWM_RAMP__SHIFT                                                                     0x18
#define CG_FDO_CTRL0__FDO_STATIC_DUTY_MASK                                                                    0x000000FFL
#define CG_FDO_CTRL0__FAN_SPINUP_DUTY_MASK                                                                    0x0000FF00L
#define CG_FDO_CTRL0__FDO_PWM_MANUAL_MASK                                                                     0x00010000L
#define CG_FDO_CTRL0__FDO_PWM_HYSTER_MASK                                                                     0x007E0000L
#define CG_FDO_CTRL0__FDO_PWM_RAMP_EN_MASK                                                                    0x00800000L
#define CG_FDO_CTRL0__FDO_PWM_RAMP_MASK                                                                       0xFF000000L
//CG_FDO_CTRL1
#define CG_FDO_CTRL1__FMAX_DUTY100__SHIFT                                                                     0x0
#define CG_FDO_CTRL1__FMIN_DUTY__SHIFT                                                                        0x8
#define CG_FDO_CTRL1__M__SHIFT                                                                                0x10
#define CG_FDO_CTRL1__RESERVED__SHIFT                                                                         0x18
#define CG_FDO_CTRL1__FMAX_DUTY100_MASK                                                                       0x000000FFL
#define CG_FDO_CTRL1__FMIN_DUTY_MASK                                                                          0x0000FF00L
#define CG_FDO_CTRL1__M_MASK                                                                                  0x00FF0000L
#define CG_FDO_CTRL1__RESERVED_MASK                                                                           0x3F000000L
//CG_FDO_CTRL2
#define CG_FDO_CTRL2__TMIN__SHIFT                                                                             0x0
#define CG_FDO_CTRL2__FAN_SPINUP_TIME__SHIFT                                                                  0x8
#define CG_FDO_CTRL2__FDO_PWM_MODE__SHIFT                                                                     0xb
#define CG_FDO_CTRL2__TMIN_HYSTER__SHIFT                                                                      0xe
#define CG_FDO_CTRL2__TMAX__SHIFT                                                                             0x11
#define CG_FDO_CTRL2__TACH_PWM_RESP_RATE__SHIFT                                                               0x19
#define CG_FDO_CTRL2__TMIN_MASK                                                                               0x000000FFL
#define CG_FDO_CTRL2__FAN_SPINUP_TIME_MASK                                                                    0x00000700L
#define CG_FDO_CTRL2__FDO_PWM_MODE_MASK                                                                       0x00003800L
#define CG_FDO_CTRL2__TMIN_HYSTER_MASK                                                                        0x0001C000L
#define CG_FDO_CTRL2__TMAX_MASK                                                                               0x01FE0000L
#define CG_FDO_CTRL2__TACH_PWM_RESP_RATE_MASK                                                                 0xFE000000L
//CG_TACH_CTRL
#define CG_TACH_CTRL__EDGE_PER_REV__SHIFT                                                                     0x0
#define CG_TACH_CTRL__TARGET_PERIOD__SHIFT                                                                    0x3
#define CG_TACH_CTRL__EDGE_PER_REV_MASK                                                                       0x00000007L
#define CG_TACH_CTRL__TARGET_PERIOD_MASK                                                                      0xFFFFFFF8L
//CG_TACH_STATUS
#define CG_TACH_STATUS__TACH_PERIOD__SHIFT                                                                    0x0
#define CG_TACH_STATUS__TACH_PERIOD_MASK                                                                      0xFFFFFFFFL
//CG_THERMAL_STATUS
#define CG_THERMAL_STATUS__FDO_PWM_DUTY__SHIFT                                                                0x9
#define CG_THERMAL_STATUS__FDO_PWM_DUTY_MASK                                                                  0x0001FE00L
//CG_PUMP_CTRL0
#define CG_PUMP_CTRL0__PUMP_STATIC_DUTY__SHIFT                                                                0x0
#define CG_PUMP_CTRL0__PUMP_SPINUP_DUTY__SHIFT                                                                0x8
#define CG_PUMP_CTRL0__PUMP_PWM_MANUAL__SHIFT                                                                 0x10
#define CG_PUMP_CTRL0__PUMP_PWM_HYSTER__SHIFT                                                                 0x11
#define CG_PUMP_CTRL0__PUMP_PWM_RAMP_EN__SHIFT                                                                0x17
#define CG_PUMP_CTRL0__PUMP_PWM_RAMP__SHIFT                                                                   0x18
#define CG_PUMP_CTRL0__PUMP_STATIC_DUTY_MASK                                                                  0x000000FFL
#define CG_PUMP_CTRL0__PUMP_SPINUP_DUTY_MASK                                                                  0x0000FF00L
#define CG_PUMP_CTRL0__PUMP_PWM_MANUAL_MASK                                                                   0x00010000L
#define CG_PUMP_CTRL0__PUMP_PWM_HYSTER_MASK                                                                   0x007E0000L
#define CG_PUMP_CTRL0__PUMP_PWM_RAMP_EN_MASK                                                                  0x00800000L
#define CG_PUMP_CTRL0__PUMP_PWM_RAMP_MASK                                                                     0xFF000000L
//CG_PUMP_CTRL1
#define CG_PUMP_CTRL1__PMAX_DUTY100__SHIFT                                                                    0x0
#define CG_PUMP_CTRL1__PMIN_DUTY__SHIFT                                                                       0x8
#define CG_PUMP_CTRL1__M__SHIFT                                                                               0x10
#define CG_PUMP_CTRL1__RESERVED__SHIFT                                                                        0x18
#define CG_PUMP_CTRL1__PMAX_DUTY100_MASK                                                                      0x000000FFL
#define CG_PUMP_CTRL1__PMIN_DUTY_MASK                                                                         0x0000FF00L
#define CG_PUMP_CTRL1__M_MASK                                                                                 0x00FF0000L
#define CG_PUMP_CTRL1__RESERVED_MASK                                                                          0x3F000000L
//CG_PUMP_CTRL2
#define CG_PUMP_CTRL2__TMIN__SHIFT                                                                            0x0
#define CG_PUMP_CTRL2__PUMP_SPINUP_TIME__SHIFT                                                                0x8
#define CG_PUMP_CTRL2__PUMP_PWM_MODE__SHIFT                                                                   0xb
#define CG_PUMP_CTRL2__TMIN_HYSTER__SHIFT                                                                     0xe
#define CG_PUMP_CTRL2__TMAX__SHIFT                                                                            0x11
#define CG_PUMP_CTRL2__TACH_PWM_RESP_RATE__SHIFT                                                              0x19
#define CG_PUMP_CTRL2__TMIN_MASK                                                                              0x000000FFL
#define CG_PUMP_CTRL2__PUMP_SPINUP_TIME_MASK                                                                  0x00000700L
#define CG_PUMP_CTRL2__PUMP_PWM_MODE_MASK                                                                     0x00003800L
#define CG_PUMP_CTRL2__TMIN_HYSTER_MASK                                                                       0x0001C000L
#define CG_PUMP_CTRL2__TMAX_MASK                                                                              0x01FE0000L
#define CG_PUMP_CTRL2__TACH_PWM_RESP_RATE_MASK                                                                0xFE000000L
//CG_PUMP_TACH_CTRL
#define CG_PUMP_TACH_CTRL__EDGE_PER_REV__SHIFT                                                                0x0
#define CG_PUMP_TACH_CTRL__TARGET_PERIOD__SHIFT                                                               0x3
#define CG_PUMP_TACH_CTRL__EDGE_PER_REV_MASK                                                                  0x00000007L
#define CG_PUMP_TACH_CTRL__TARGET_PERIOD_MASK                                                                 0xFFFFFFF8L
//CG_PUMP_TACH_STATUS
#define CG_PUMP_TACH_STATUS__TACH_PERIOD__SHIFT                                                               0x0
#define CG_PUMP_TACH_STATUS__TACH_PERIOD_MASK                                                                 0xFFFFFFFFL
//CG_PUMP_STATUS
#define CG_PUMP_STATUS__PUMP_PWM_DUTY__SHIFT                                                                  0x9
#define CG_PUMP_STATUS__PUMP_PWM_DUTY_MASK                                                                    0x0001FE00L
//THM_TCON_LOCAL0
#define THM_TCON_LOCAL0__TMON0_PwrDn_Dis__SHIFT                                                               0x1
#define THM_TCON_LOCAL0__TMON1_PwrDn_Dis__SHIFT                                                               0x2
#define THM_TCON_LOCAL0__TMON0_PwrDn_Dis_MASK                                                                 0x00000002L
#define THM_TCON_LOCAL0__TMON1_PwrDn_Dis_MASK                                                                 0x00000004L
//THM_TCON_LOCAL1
#define THM_TCON_LOCAL1__Turn_Off_TMON0__SHIFT                                                                0x0
#define THM_TCON_LOCAL1__Turn_Off_TMON1__SHIFT                                                                0x1
#define THM_TCON_LOCAL1__PowerDownTmon0__SHIFT                                                                0x4
#define THM_TCON_LOCAL1__PowerDownTmon1__SHIFT                                                                0x5
#define THM_TCON_LOCAL1__Turn_Off_TMON0_MASK                                                                  0x00000001L
#define THM_TCON_LOCAL1__Turn_Off_TMON1_MASK                                                                  0x00000002L
#define THM_TCON_LOCAL1__PowerDownTmon0_MASK                                                                  0x00000010L
#define THM_TCON_LOCAL1__PowerDownTmon1_MASK                                                                  0x00000020L
//THM_TCON_LOCAL2
#define THM_TCON_LOCAL2__TMON_init_delay__SHIFT                                                               0x0
#define THM_TCON_LOCAL2__TMON_pwrup_stagger_time__SHIFT                                                       0x2
#define THM_TCON_LOCAL2__short_stagger_count__SHIFT                                                           0x5
#define THM_TCON_LOCAL2__sbtsi_use_corrected__SHIFT                                                           0x6
#define THM_TCON_LOCAL2__temp_read_skip_scale__SHIFT                                                          0xa
#define THM_TCON_LOCAL2__skip_scale_correction__SHIFT                                                         0xb
#define THM_TCON_LOCAL2__TMON_init_delay_MASK                                                                 0x00000003L
#define THM_TCON_LOCAL2__TMON_pwrup_stagger_time_MASK                                                         0x0000000CL
#define THM_TCON_LOCAL2__short_stagger_count_MASK                                                             0x00000020L
#define THM_TCON_LOCAL2__sbtsi_use_corrected_MASK                                                             0x00000040L
#define THM_TCON_LOCAL2__temp_read_skip_scale_MASK                                                            0x00000400L
#define THM_TCON_LOCAL2__skip_scale_correction_MASK                                                           0x00000800L
//THM_TCON_LOCAL3
#define THM_TCON_LOCAL3__Global_TMAX__SHIFT                                                                   0x0
#define THM_TCON_LOCAL3__Global_TMAX_MASK                                                                     0x000007FFL
//THM_TCON_LOCAL4
#define THM_TCON_LOCAL4__Global_TMAX_ID__SHIFT                                                                0x0
#define THM_TCON_LOCAL4__Global_TMAX_ID_MASK                                                                  0x000000FFL
//THM_TCON_LOCAL5
#define THM_TCON_LOCAL5__Global_TMIN__SHIFT                                                                   0x0
#define THM_TCON_LOCAL5__Global_TMIN_MASK                                                                     0x000007FFL
//THM_TCON_LOCAL6
#define THM_TCON_LOCAL6__Global_TMIN_ID__SHIFT                                                                0x0
#define THM_TCON_LOCAL6__Global_TMIN_ID_MASK                                                                  0x000000FFL
//THM_TCON_LOCAL7
#define THM_TCON_LOCAL7__THERMID__SHIFT                                                                       0x0
#define THM_TCON_LOCAL7__THERMID_MASK                                                                         0x000000FFL
//THM_TCON_LOCAL8
#define THM_TCON_LOCAL8__THERMMAX__SHIFT                                                                      0x0
#define THM_TCON_LOCAL8__THERMMAX_MASK                                                                        0x000007FFL
//THM_TCON_LOCAL9
#define THM_TCON_LOCAL9__Tj_Max_TMON0__SHIFT                                                                  0x0
#define THM_TCON_LOCAL9__Tj_Max_TMON0_MASK                                                                    0x000007FFL
//THM_TCON_LOCAL10
#define THM_TCON_LOCAL10__TMON0_Tj_Max_RS_ID__SHIFT                                                           0x0
#define THM_TCON_LOCAL10__TMON0_Tj_Max_RS_ID_MASK                                                             0x000000FFL
//THM_TCON_LOCAL11
#define THM_TCON_LOCAL11__Tj_Max_TMON1__SHIFT                                                                 0x0
#define THM_TCON_LOCAL11__Tj_Max_TMON1_MASK                                                                   0x000007FFL
//THM_TCON_LOCAL12
#define THM_TCON_LOCAL12__TMON1_Tj_Max_RS_ID__SHIFT                                                           0x0
#define THM_TCON_LOCAL12__TMON1_Tj_Max_RS_ID_MASK                                                             0x000000FFL
//THM_TCON_LOCAL13
#define THM_TCON_LOCAL13__boot_done__SHIFT                                                                    0x0
#define THM_TCON_LOCAL13__boot_done_MASK                                                                      0x00000001L
//THM_BACO_CNTL
#define THM_BACO_CNTL__BACO_MODE__SHIFT                                                                       0x0
#define THM_BACO_CNTL__BACO_ISO_EN__SHIFT                                                                     0x1
#define THM_BACO_CNTL__BACO_PWROKRAW_CNTL__SHIFT                                                              0x2
#define THM_BACO_CNTL__BACO_RESET_EN__SHIFT                                                                   0x3
#define THM_BACO_CNTL__BACO_SOC_VDCI_RESET__SHIFT                                                             0x4
#define THM_BACO_CNTL__BACO_SMNCLK_MUX__SHIFT                                                                 0x5
#define THM_BACO_CNTL__BACO_SOC_REFCLK_OFF__SHIFT                                                             0x6
#define THM_BACO_CNTL__BACO_AEB_ISO_EN__SHIFT                                                                 0x7
#define THM_BACO_CNTL__BACO_EXIT__SHIFT                                                                       0x8
#define THM_BACO_CNTL__BACO_ANA_ISO_EN__SHIFT                                                                 0x9
#define THM_BACO_CNTL__BACO_SB_AXI_FENCE__SHIFT                                                               0x1e
#define THM_BACO_CNTL__SOC_DOMAIN_IDLE__SHIFT                                                                 0x1f
#define THM_BACO_CNTL__BACO_MODE_MASK                                                                         0x00000001L
#define THM_BACO_CNTL__BACO_ISO_EN_MASK                                                                       0x00000002L
#define THM_BACO_CNTL__BACO_PWROKRAW_CNTL_MASK                                                                0x00000004L
#define THM_BACO_CNTL__BACO_RESET_EN_MASK                                                                     0x00000008L
#define THM_BACO_CNTL__BACO_SOC_VDCI_RESET_MASK                                                               0x00000010L
#define THM_BACO_CNTL__BACO_SMNCLK_MUX_MASK                                                                   0x00000020L
#define THM_BACO_CNTL__BACO_SOC_REFCLK_OFF_MASK                                                               0x00000040L
#define THM_BACO_CNTL__BACO_AEB_ISO_EN_MASK                                                                   0x00000080L
#define THM_BACO_CNTL__BACO_EXIT_MASK                                                                         0x00000100L
#define THM_BACO_CNTL__BACO_ANA_ISO_EN_MASK                                                                   0x00000200L
#define THM_BACO_CNTL__BACO_SB_AXI_FENCE_MASK                                                                 0x40000000L
#define THM_BACO_CNTL__SOC_DOMAIN_IDLE_MASK                                                                   0x80000000L
//THM_BACO_TIMING0
#define THM_BACO_TIMING0__BACO_ISO_EXIT_CNT__SHIFT                                                            0x0
#define THM_BACO_TIMING0__BACO_PWROKRAW_EXIT_CNT__SHIFT                                                       0x8
#define THM_BACO_TIMING0__BACO_RESET_EXIT_CNT__SHIFT                                                          0x10
#define THM_BACO_TIMING0__BACO_VDCI_RESET_EXIT_CNT__SHIFT                                                     0x18
#define THM_BACO_TIMING0__BACO_ISO_EXIT_CNT_MASK                                                              0x000000FFL
#define THM_BACO_TIMING0__BACO_PWROKRAW_EXIT_CNT_MASK                                                         0x0000FF00L
#define THM_BACO_TIMING0__BACO_RESET_EXIT_CNT_MASK                                                            0x00FF0000L
#define THM_BACO_TIMING0__BACO_VDCI_RESET_EXIT_CNT_MASK                                                       0xFF000000L
//THM_BACO_TIMING1
#define THM_BACO_TIMING1__BACO_SMNCLK_EXIT_CNT__SHIFT                                                         0x0
#define THM_BACO_TIMING1__BACO_FENCE_EXIT_CNT__SHIFT                                                          0x8
#define THM_BACO_TIMING1__BACO_REFCLK_EXIT_CNT__SHIFT                                                         0x10
#define THM_BACO_TIMING1__BACO_MODE_EXIT_CNT__SHIFT                                                           0x18
#define THM_BACO_TIMING1__BACO_SMNCLK_EXIT_CNT_MASK                                                           0x000000FFL
#define THM_BACO_TIMING1__BACO_FENCE_EXIT_CNT_MASK                                                            0x0000FF00L
#define THM_BACO_TIMING1__BACO_REFCLK_EXIT_CNT_MASK                                                           0x00FF0000L
#define THM_BACO_TIMING1__BACO_MODE_EXIT_CNT_MASK                                                             0xFF000000L
//XTAL_CNTL
#define XTAL_CNTL__PCIE_REFCLK_SWITCH__SHIFT                                                                  0x0
#define XTAL_CNTL__CORE_XTAL_CLKGEN_CLKEN__SHIFT                                                              0x4
#define XTAL_CNTL__CORE_XTAL_PWDN__SHIFT                                                                      0x8
#define XTAL_CNTL__OSC_GAIN_EN__SHIFT                                                                         0xc
#define XTAL_CNTL__PCIE_REFCLK_SWITCH_MASK                                                                    0x00000001L
#define XTAL_CNTL__CORE_XTAL_CLKGEN_CLKEN_MASK                                                                0x00000010L
#define XTAL_CNTL__CORE_XTAL_PWDN_MASK                                                                        0x00000100L
#define XTAL_CNTL__OSC_GAIN_EN_MASK                                                                           0x00007000L
//SBTSI_REMOTE_TEMP
#define SBTSI_REMOTE_TEMP__RemoteTcenSensor__SHIFT                                                            0x0
#define SBTSI_REMOTE_TEMP__RemoteTcenSensorId__SHIFT                                                          0xb
#define SBTSI_REMOTE_TEMP__RemoteTcenSensorValid__SHIFT                                                       0x13
#define SBTSI_REMOTE_TEMP__RemoteTcenSensor_MASK                                                              0x000007FFL
#define SBTSI_REMOTE_TEMP__RemoteTcenSensorId_MASK                                                            0x0007F800L
#define SBTSI_REMOTE_TEMP__RemoteTcenSensorValid_MASK                                                         0x00080000L
//SBRMI_CONTROL
#define SBRMI_CONTROL__READ_CMD_INT_DIS__SHIFT                                                                0x0
#define SBRMI_CONTROL__DPD__SHIFT                                                                             0x1
#define SBRMI_CONTROL__DbrdySts__SHIFT                                                                        0x2
#define SBRMI_CONTROL__READ_CMD_INT_DIS_MASK                                                                  0x00000001L
#define SBRMI_CONTROL__DPD_MASK                                                                               0x00000002L
#define SBRMI_CONTROL__DbrdySts_MASK                                                                          0x00000004L
//SBRMI_COMMAND
#define SBRMI_COMMAND__Command__SHIFT                                                                         0x0
#define SBRMI_COMMAND__WrDataLen__SHIFT                                                                       0x8
#define SBRMI_COMMAND__RdDataLen__SHIFT                                                                       0x10
#define SBRMI_COMMAND__CommandSent__SHIFT                                                                     0x18
#define SBRMI_COMMAND__CommandNotSupported__SHIFT                                                             0x19
#define SBRMI_COMMAND__CommandAborted__SHIFT                                                                  0x1a
#define SBRMI_COMMAND__Status__SHIFT                                                                          0x1c
#define SBRMI_COMMAND__Command_MASK                                                                           0x000000FFL
#define SBRMI_COMMAND__WrDataLen_MASK                                                                         0x0000FF00L
#define SBRMI_COMMAND__RdDataLen_MASK                                                                         0x00FF0000L
#define SBRMI_COMMAND__CommandSent_MASK                                                                       0x01000000L
#define SBRMI_COMMAND__CommandNotSupported_MASK                                                               0x02000000L
#define SBRMI_COMMAND__CommandAborted_MASK                                                                    0x04000000L
#define SBRMI_COMMAND__Status_MASK                                                                            0xF0000000L
//SBRMI_WRITE_DATA0
#define SBRMI_WRITE_DATA0__WrByte0__SHIFT                                                                     0x0
#define SBRMI_WRITE_DATA0__WrByte1__SHIFT                                                                     0x8
#define SBRMI_WRITE_DATA0__WrByte2__SHIFT                                                                     0x10
#define SBRMI_WRITE_DATA0__WrByte3__SHIFT                                                                     0x18
#define SBRMI_WRITE_DATA0__WrByte0_MASK                                                                       0x000000FFL
#define SBRMI_WRITE_DATA0__WrByte1_MASK                                                                       0x0000FF00L
#define SBRMI_WRITE_DATA0__WrByte2_MASK                                                                       0x00FF0000L
#define SBRMI_WRITE_DATA0__WrByte3_MASK                                                                       0xFF000000L
//SBRMI_WRITE_DATA1
#define SBRMI_WRITE_DATA1__WrByte4__SHIFT                                                                     0x0
#define SBRMI_WRITE_DATA1__WrByte5__SHIFT                                                                     0x8
#define SBRMI_WRITE_DATA1__WrByte6__SHIFT                                                                     0x10
#define SBRMI_WRITE_DATA1__WrByte7__SHIFT                                                                     0x18
#define SBRMI_WRITE_DATA1__WrByte4_MASK                                                                       0x000000FFL
#define SBRMI_WRITE_DATA1__WrByte5_MASK                                                                       0x0000FF00L
#define SBRMI_WRITE_DATA1__WrByte6_MASK                                                                       0x00FF0000L
#define SBRMI_WRITE_DATA1__WrByte7_MASK                                                                       0xFF000000L
//SBRMI_WRITE_DATA2
#define SBRMI_WRITE_DATA2__WrByte8__SHIFT                                                                     0x0
#define SBRMI_WRITE_DATA2__WrByte9__SHIFT                                                                     0x8
#define SBRMI_WRITE_DATA2__WrByte10__SHIFT                                                                    0x10
#define SBRMI_WRITE_DATA2__WrByte11__SHIFT                                                                    0x18
#define SBRMI_WRITE_DATA2__WrByte8_MASK                                                                       0x000000FFL
#define SBRMI_WRITE_DATA2__WrByte9_MASK                                                                       0x0000FF00L
#define SBRMI_WRITE_DATA2__WrByte10_MASK                                                                      0x00FF0000L
#define SBRMI_WRITE_DATA2__WrByte11_MASK                                                                      0xFF000000L
//SBRMI_READ_DATA0
#define SBRMI_READ_DATA0__RdByte0__SHIFT                                                                      0x0
#define SBRMI_READ_DATA0__RdByte1__SHIFT                                                                      0x8
#define SBRMI_READ_DATA0__RdByte2__SHIFT                                                                      0x10
#define SBRMI_READ_DATA0__RdByte3__SHIFT                                                                      0x18
#define SBRMI_READ_DATA0__RdByte0_MASK                                                                        0x000000FFL
#define SBRMI_READ_DATA0__RdByte1_MASK                                                                        0x0000FF00L
#define SBRMI_READ_DATA0__RdByte2_MASK                                                                        0x00FF0000L
#define SBRMI_READ_DATA0__RdByte3_MASK                                                                        0xFF000000L
//SBRMI_READ_DATA1
#define SBRMI_READ_DATA1__RdByte4__SHIFT                                                                      0x0
#define SBRMI_READ_DATA1__RdByte5__SHIFT                                                                      0x8
#define SBRMI_READ_DATA1__RdByte6__SHIFT                                                                      0x10
#define SBRMI_READ_DATA1__RdByte7__SHIFT                                                                      0x18
#define SBRMI_READ_DATA1__RdByte4_MASK                                                                        0x000000FFL
#define SBRMI_READ_DATA1__RdByte5_MASK                                                                        0x0000FF00L
#define SBRMI_READ_DATA1__RdByte6_MASK                                                                        0x00FF0000L
#define SBRMI_READ_DATA1__RdByte7_MASK                                                                        0xFF000000L
//SBRMI_CORE_EN_NUMBER
#define SBRMI_CORE_EN_NUMBER__EnabledCoreNum__SHIFT                                                           0x0
#define SBRMI_CORE_EN_NUMBER__EnabledCoreNum_MASK                                                             0x0000007FL
//SBRMI_CORE_EN_STATUS0
#define SBRMI_CORE_EN_STATUS0__CoreEnStat0__SHIFT                                                             0x0
#define SBRMI_CORE_EN_STATUS0__CoreEnStat0_MASK                                                               0xFFFFFFFFL
//SBRMI_CORE_EN_STATUS1
#define SBRMI_CORE_EN_STATUS1__CoreEnStat1__SHIFT                                                             0x0
#define SBRMI_CORE_EN_STATUS1__CoreEnStat1_MASK                                                               0xFFFFFFFFL
//SBRMI_APIC_STATUS0
#define SBRMI_APIC_STATUS0__APICStat0__SHIFT                                                                  0x0
#define SBRMI_APIC_STATUS0__APICStat0_MASK                                                                    0xFFFFFFFFL
//SBRMI_APIC_STATUS1
#define SBRMI_APIC_STATUS1__APICStat1__SHIFT                                                                  0x0
#define SBRMI_APIC_STATUS1__APICStat1_MASK                                                                    0xFFFFFFFFL
//SBRMI_MCE_STATUS0
#define SBRMI_MCE_STATUS0__MceStat0__SHIFT                                                                    0x0
#define SBRMI_MCE_STATUS0__MceStat0_MASK                                                                      0xFFFFFFFFL
//SBRMI_MCE_STATUS1
#define SBRMI_MCE_STATUS1__MceStat1__SHIFT                                                                    0x0
#define SBRMI_MCE_STATUS1__MceStat1_MASK                                                                      0xFFFFFFFFL
//SMBUS_CNTL0
#define SMBUS_CNTL0__SMB_DEFAULT_SLV_ADDR_OVERRIDE__SHIFT                                                     0x0
#define SMBUS_CNTL0__SMB_DEFAULT_SLV_ADDR__SHIFT                                                              0x1
#define SMBUS_CNTL0__SMB_CPL_DUMMY_BYTE__SHIFT                                                                0x8
#define SMBUS_CNTL0__SMB_NOTIFY_ARP_MAX_TIMES__SHIFT                                                          0x10
#define SMBUS_CNTL0__THM_READY__SHIFT                                                                         0x14
#define SMBUS_CNTL0__SMB_DEFAULT_SLV_ADDR_OVERRIDE_MASK                                                       0x00000001L
#define SMBUS_CNTL0__SMB_DEFAULT_SLV_ADDR_MASK                                                                0x000000FEL
#define SMBUS_CNTL0__SMB_CPL_DUMMY_BYTE_MASK                                                                  0x0000FF00L
#define SMBUS_CNTL0__SMB_NOTIFY_ARP_MAX_TIMES_MASK                                                            0x00070000L
#define SMBUS_CNTL0__THM_READY_MASK                                                                           0x00100000L
//SMBUS_CNTL1
#define SMBUS_CNTL1__SMB_TIMEOUT_EN__SHIFT                                                                    0x0
#define SMBUS_CNTL1__SMB_BLK_WR_CMD_EN__SHIFT                                                                 0x1
#define SMBUS_CNTL1__SMB_BLK_RD_CMD_EN__SHIFT                                                                 0x9
#define SMBUS_CNTL1__SMB_TIMEOUT_EN_MASK                                                                      0x00000001L
#define SMBUS_CNTL1__SMB_BLK_WR_CMD_EN_MASK                                                                   0x000001FEL
#define SMBUS_CNTL1__SMB_BLK_RD_CMD_EN_MASK                                                                   0x0001FE00L
//SMBUS_BLKWR_CMD_CTRL0
#define SMBUS_BLKWR_CMD_CTRL0__SMB_BLK_WR_CMD0__SHIFT                                                         0x0
#define SMBUS_BLKWR_CMD_CTRL0__SMB_BLK_WR_CMD1__SHIFT                                                         0x8
#define SMBUS_BLKWR_CMD_CTRL0__SMB_BLK_WR_CMD2__SHIFT                                                         0x10
#define SMBUS_BLKWR_CMD_CTRL0__SMB_BLK_WR_CMD3__SHIFT                                                         0x18
#define SMBUS_BLKWR_CMD_CTRL0__SMB_BLK_WR_CMD0_MASK                                                           0x000000FFL
#define SMBUS_BLKWR_CMD_CTRL0__SMB_BLK_WR_CMD1_MASK                                                           0x0000FF00L
#define SMBUS_BLKWR_CMD_CTRL0__SMB_BLK_WR_CMD2_MASK                                                           0x00FF0000L
#define SMBUS_BLKWR_CMD_CTRL0__SMB_BLK_WR_CMD3_MASK                                                           0xFF000000L
//SMBUS_BLKWR_CMD_CTRL1
#define SMBUS_BLKWR_CMD_CTRL1__SMB_BLK_WR_CMD4__SHIFT                                                         0x0
#define SMBUS_BLKWR_CMD_CTRL1__SMB_BLK_WR_CMD5__SHIFT                                                         0x8
#define SMBUS_BLKWR_CMD_CTRL1__SMB_BLK_WR_CMD6__SHIFT                                                         0x10
#define SMBUS_BLKWR_CMD_CTRL1__SMB_BLK_WR_CMD7__SHIFT                                                         0x18
#define SMBUS_BLKWR_CMD_CTRL1__SMB_BLK_WR_CMD4_MASK                                                           0x000000FFL
#define SMBUS_BLKWR_CMD_CTRL1__SMB_BLK_WR_CMD5_MASK                                                           0x0000FF00L
#define SMBUS_BLKWR_CMD_CTRL1__SMB_BLK_WR_CMD6_MASK                                                           0x00FF0000L
#define SMBUS_BLKWR_CMD_CTRL1__SMB_BLK_WR_CMD7_MASK                                                           0xFF000000L
//SMBUS_BLKRD_CMD_CTRL0
#define SMBUS_BLKRD_CMD_CTRL0__SMB_BLK_RD_CMD0__SHIFT                                                         0x0
#define SMBUS_BLKRD_CMD_CTRL0__SMB_BLK_RD_CMD1__SHIFT                                                         0x8
#define SMBUS_BLKRD_CMD_CTRL0__SMB_BLK_RD_CMD2__SHIFT                                                         0x10
#define SMBUS_BLKRD_CMD_CTRL0__SMB_BLK_RD_CMD3__SHIFT                                                         0x18
#define SMBUS_BLKRD_CMD_CTRL0__SMB_BLK_RD_CMD0_MASK                                                           0x000000FFL
#define SMBUS_BLKRD_CMD_CTRL0__SMB_BLK_RD_CMD1_MASK                                                           0x0000FF00L
#define SMBUS_BLKRD_CMD_CTRL0__SMB_BLK_RD_CMD2_MASK                                                           0x00FF0000L
#define SMBUS_BLKRD_CMD_CTRL0__SMB_BLK_RD_CMD3_MASK                                                           0xFF000000L
//SMBUS_BLKRD_CMD_CTRL1
#define SMBUS_BLKRD_CMD_CTRL1__SMB_BLK_RD_CMD4__SHIFT                                                         0x0
#define SMBUS_BLKRD_CMD_CTRL1__SMB_BLK_RD_CMD5__SHIFT                                                         0x8
#define SMBUS_BLKRD_CMD_CTRL1__SMB_BLK_RD_CMD6__SHIFT                                                         0x10
#define SMBUS_BLKRD_CMD_CTRL1__SMB_BLK_RD_CMD7__SHIFT                                                         0x18
#define SMBUS_BLKRD_CMD_CTRL1__SMB_BLK_RD_CMD4_MASK                                                           0x000000FFL
#define SMBUS_BLKRD_CMD_CTRL1__SMB_BLK_RD_CMD5_MASK                                                           0x0000FF00L
#define SMBUS_BLKRD_CMD_CTRL1__SMB_BLK_RD_CMD6_MASK                                                           0x00FF0000L
#define SMBUS_BLKRD_CMD_CTRL1__SMB_BLK_RD_CMD7_MASK                                                           0xFF000000L
//SMBUS_TIMING_CNTL0
#define SMBUS_TIMING_CNTL0__SMB_TIMEOUT_MARGIN__SHIFT                                                         0x0
#define SMBUS_TIMING_CNTL0__SMB_FILTER_LEVEL_CONVERT_MARGIN__SHIFT                                            0x16
#define SMBUS_TIMING_CNTL0__SMB_TIMEOUT_MARGIN_MASK                                                           0x003FFFFFL
#define SMBUS_TIMING_CNTL0__SMB_FILTER_LEVEL_CONVERT_MARGIN_MASK                                              0x3FC00000L
//SMBUS_TIMING_CNTL1
#define SMBUS_TIMING_CNTL1__SMB_DAT_SETUP_TIME_MARGIN__SHIFT                                                  0x0
#define SMBUS_TIMING_CNTL1__SMB_DAT_HOLD_TIME_MARGIN__SHIFT                                                   0x5
#define SMBUS_TIMING_CNTL1__SMB_START_AND_STOP_TIMING_MARGIN__SHIFT                                           0xb
#define SMBUS_TIMING_CNTL1__SMB_BUS_FREE_MARGIN__SHIFT                                                        0x14
#define SMBUS_TIMING_CNTL1__SMB_DAT_SETUP_TIME_MARGIN_MASK                                                    0x0000001FL
#define SMBUS_TIMING_CNTL1__SMB_DAT_HOLD_TIME_MARGIN_MASK                                                     0x000007E0L
#define SMBUS_TIMING_CNTL1__SMB_START_AND_STOP_TIMING_MARGIN_MASK                                             0x000FF800L
#define SMBUS_TIMING_CNTL1__SMB_BUS_FREE_MARGIN_MASK                                                          0x3FF00000L
//SMBUS_TIMING_CNTL2
#define SMBUS_TIMING_CNTL2__SMB_SMBCLK_HIGHMAX_MARGIN__SHIFT                                                  0x0
#define SMBUS_TIMING_CNTL2__SMBCLK_LEVEL_CTRL_MARGIN__SHIFT                                                   0xd
#define SMBUS_TIMING_CNTL2__SMB_SMBCLK_HIGHMAX_MARGIN_MASK                                                    0x00001FFFL
#define SMBUS_TIMING_CNTL2__SMBCLK_LEVEL_CTRL_MARGIN_MASK                                                     0x07FFE000L
//SMBUS_TRIGGER_CNTL
#define SMBUS_TRIGGER_CNTL__SMB_SOFT_RESET_TRIGGER__SHIFT                                                     0x0
#define SMBUS_TRIGGER_CNTL__SMB_NOTIFY_ARP_TRIGGER__SHIFT                                                     0x8
#define SMBUS_TRIGGER_CNTL__SMB_SOFT_RESET_TRIGGER_MASK                                                       0x00000001L
#define SMBUS_TRIGGER_CNTL__SMB_NOTIFY_ARP_TRIGGER_MASK                                                       0x00000100L
//SMBUS_UDID_CNTL0
#define SMBUS_UDID_CNTL0__SMB_PRBS_INI_SEED__SHIFT                                                            0x0
#define SMBUS_UDID_CNTL0__SMB_SRST_REGEN_UDID_EN__SHIFT                                                       0x1f
#define SMBUS_UDID_CNTL0__SMB_PRBS_INI_SEED_MASK                                                              0x7FFFFFFFL
#define SMBUS_UDID_CNTL0__SMB_SRST_REGEN_UDID_EN_MASK                                                         0x80000000L
//SMBUS_UDID_CNTL1
#define SMBUS_UDID_CNTL1__SMB_UDID_31_0__SHIFT                                                                0x0
#define SMBUS_UDID_CNTL1__SMB_UDID_31_0_MASK                                                                  0xFFFFFFFFL
//SMBUS_UDID_CNTL2
#define SMBUS_UDID_CNTL2__PEC_SUPPORTED__SHIFT                                                                0x0
#define SMBUS_UDID_CNTL2__UDID_VERSION__SHIFT                                                                 0x1
#define SMBUS_UDID_CNTL2__SMBUS_VERSION__SHIFT                                                                0x4
#define SMBUS_UDID_CNTL2__OEM__SHIFT                                                                          0x8
#define SMBUS_UDID_CNTL2__ASF__SHIFT                                                                          0x9
#define SMBUS_UDID_CNTL2__IPMI__SHIFT                                                                         0xa
#define SMBUS_UDID_CNTL2__PEC_SUPPORTED_MASK                                                                  0x00000001L
#define SMBUS_UDID_CNTL2__UDID_VERSION_MASK                                                                   0x0000000EL
#define SMBUS_UDID_CNTL2__SMBUS_VERSION_MASK                                                                  0x000000F0L
#define SMBUS_UDID_CNTL2__OEM_MASK                                                                            0x00000100L
#define SMBUS_UDID_CNTL2__ASF_MASK                                                                            0x00000200L
#define SMBUS_UDID_CNTL2__IPMI_MASK                                                                           0x00000400L
//SMBUS_BACO_DUMMY
#define SMBUS_BACO_DUMMY__BACO_DUMMY_DATA__SHIFT                                                              0x0
#define SMBUS_BACO_DUMMY__BACO_DUMMY_DATA_MASK                                                                0xFFFFFFFFL
//SMBUS_BACO_ADDR_RANGE0_LOW
#define SMBUS_BACO_ADDR_RANGE0_LOW__BACO_ADDR_RANGE0_LOW__SHIFT                                               0x0
#define SMBUS_BACO_ADDR_RANGE0_LOW__BACO_ADDR_RANGE0_LOW_MASK                                                 0x000FFFFFL
//SMBUS_BACO_ADDR_RANGE0_HIGH
#define SMBUS_BACO_ADDR_RANGE0_HIGH__BACO_ADDR_RANGE0_HIGH__SHIFT                                             0x0
#define SMBUS_BACO_ADDR_RANGE0_HIGH__BACO_ADDR_RANGE0_HIGH_MASK                                               0x000FFFFFL
//SMBUS_BACO_ADDR_RANGE1_LOW
#define SMBUS_BACO_ADDR_RANGE1_LOW__BACO_ADDR_RANGE1_LOW__SHIFT                                               0x0
#define SMBUS_BACO_ADDR_RANGE1_LOW__BACO_ADDR_RANGE1_LOW_MASK                                                 0x000FFFFFL
//SMBUS_BACO_ADDR_RANGE1_HIGH
#define SMBUS_BACO_ADDR_RANGE1_HIGH__BACO_ADDR_RANGE1_HIGH__SHIFT                                             0x0
#define SMBUS_BACO_ADDR_RANGE1_HIGH__BACO_ADDR_RANGE1_HIGH_MASK                                               0x000FFFFFL
//SMBUS_BACO_ADDR_RANGE2_LOW
#define SMBUS_BACO_ADDR_RANGE2_LOW__BACO_ADDR_RANGE2_LOW__SHIFT                                               0x0
#define SMBUS_BACO_ADDR_RANGE2_LOW__BACO_ADDR_RANGE2_LOW_MASK                                                 0x000FFFFFL
//SMBUS_BACO_ADDR_RANGE2_HIGH
#define SMBUS_BACO_ADDR_RANGE2_HIGH__BACO_ADDR_RANGE2_HIGH__SHIFT                                             0x0
#define SMBUS_BACO_ADDR_RANGE2_HIGH__BACO_ADDR_RANGE2_HIGH_MASK                                               0x000FFFFFL
//SMBUS_BACO_ADDR_RANGE3_LOW
#define SMBUS_BACO_ADDR_RANGE3_LOW__BACO_ADDR_RANGE3_LOW__SHIFT                                               0x0
#define SMBUS_BACO_ADDR_RANGE3_LOW__BACO_ADDR_RANGE3_LOW_MASK                                                 0x000FFFFFL
//SMBUS_BACO_ADDR_RANGE3_HIGH
#define SMBUS_BACO_ADDR_RANGE3_HIGH__BACO_ADDR_RANGE3_HIGH__SHIFT                                             0x0
#define SMBUS_BACO_ADDR_RANGE3_HIGH__BACO_ADDR_RANGE3_HIGH_MASK                                               0x000FFFFFL
//SMBUS_BACO_ADDR_RANGE4_LOW
#define SMBUS_BACO_ADDR_RANGE4_LOW__BACO_ADDR_RANGE4_LOW__SHIFT                                               0x0
#define SMBUS_BACO_ADDR_RANGE4_LOW__BACO_ADDR_RANGE4_LOW_MASK                                                 0x000FFFFFL
//SMBUS_BACO_ADDR_RANGE4_HIGH
#define SMBUS_BACO_ADDR_RANGE4_HIGH__BACO_ADDR_RANGE4_HIGH__SHIFT                                             0x0
#define SMBUS_BACO_ADDR_RANGE4_HIGH__BACO_ADDR_RANGE4_HIGH_MASK                                               0x000FFFFFL
//THM_GPIO_MACO_EN_CTRL
#define THM_GPIO_MACO_EN_CTRL__MACO_EN_TXIMPSEL__SHIFT                                                        0x0
#define THM_GPIO_MACO_EN_CTRL__MACO_EN_PD__SHIFT                                                              0x1
#define THM_GPIO_MACO_EN_CTRL__MACO_EN_PU__SHIFT                                                              0x2
#define THM_GPIO_MACO_EN_CTRL__MACO_EN_SCHMEN__SHIFT                                                          0x3
#define THM_GPIO_MACO_EN_CTRL__MACO_EN_S0__SHIFT                                                              0x4
#define THM_GPIO_MACO_EN_CTRL__MACO_EN_S1__SHIFT                                                              0x5
#define THM_GPIO_MACO_EN_CTRL__MACO_EN_RXEN__SHIFT                                                            0x6
#define THM_GPIO_MACO_EN_CTRL__MACO_EN_RXSEL0__SHIFT                                                          0x7
#define THM_GPIO_MACO_EN_CTRL__MACO_EN_RXSEL1__SHIFT                                                          0x8
#define THM_GPIO_MACO_EN_CTRL__MACO_EN_OE_OVERRIDE__SHIFT                                                     0x10
#define THM_GPIO_MACO_EN_CTRL__MACO_EN_OE__SHIFT                                                              0x11
#define THM_GPIO_MACO_EN_CTRL__MACO_EN_A_OVERRIDE__SHIFT                                                      0x12
#define THM_GPIO_MACO_EN_CTRL__MACO_EN_A__SHIFT                                                               0x13
#define THM_GPIO_MACO_EN_CTRL__Y__SHIFT                                                                       0x1f
#define THM_GPIO_MACO_EN_CTRL__MACO_EN_TXIMPSEL_MASK                                                          0x00000001L
#define THM_GPIO_MACO_EN_CTRL__MACO_EN_PD_MASK                                                                0x00000002L
#define THM_GPIO_MACO_EN_CTRL__MACO_EN_PU_MASK                                                                0x00000004L
#define THM_GPIO_MACO_EN_CTRL__MACO_EN_SCHMEN_MASK                                                            0x00000008L
#define THM_GPIO_MACO_EN_CTRL__MACO_EN_S0_MASK                                                                0x00000010L
#define THM_GPIO_MACO_EN_CTRL__MACO_EN_S1_MASK                                                                0x00000020L
#define THM_GPIO_MACO_EN_CTRL__MACO_EN_RXEN_MASK                                                              0x00000040L
#define THM_GPIO_MACO_EN_CTRL__MACO_EN_RXSEL0_MASK                                                            0x00000080L
#define THM_GPIO_MACO_EN_CTRL__MACO_EN_RXSEL1_MASK                                                            0x00000100L
#define THM_GPIO_MACO_EN_CTRL__MACO_EN_OE_OVERRIDE_MASK                                                       0x00010000L
#define THM_GPIO_MACO_EN_CTRL__MACO_EN_OE_MASK                                                                0x00020000L
#define THM_GPIO_MACO_EN_CTRL__MACO_EN_A_OVERRIDE_MASK                                                        0x00040000L
#define THM_GPIO_MACO_EN_CTRL__MACO_EN_A_MASK                                                                 0x00080000L
#define THM_GPIO_MACO_EN_CTRL__Y_MASK                                                                         0x80000000L
//THM_BACO_TIMING2
#define THM_BACO_TIMING2__BACO_AEB_ISO_EXIT_CNT__SHIFT                                                        0x0
#define THM_BACO_TIMING2__BACO_ANA_ISO_EXIT_CNT__SHIFT                                                        0x8
#define THM_BACO_TIMING2__BACO_EXIT_CNT__SHIFT                                                                0x10
#define THM_BACO_TIMING2__BACO_AEB_ISO_EXIT_CNT_MASK                                                          0x000000FFL
#define THM_BACO_TIMING2__BACO_ANA_ISO_EXIT_CNT_MASK                                                          0x0000FF00L
#define THM_BACO_TIMING2__BACO_EXIT_CNT_MASK                                                                  0x00FF0000L
//THM_BACO_TIMING
#define THM_BACO_TIMING__BACO_RESET_DELAY__SHIFT                                                              0x0
#define THM_BACO_TIMING__BACO_RESET_DELAY_MASK                                                                0x0000FFFFL
//THM_TMON0_REMOTE_START
#define THM_TMON0_REMOTE_START__DATA__SHIFT                                                                   0x0
#define THM_TMON0_REMOTE_START__DATA_MASK                                                                     0xFFFFFFFFL
//THM_TMON0_REMOTE_END
#define THM_TMON0_REMOTE_END__DATA__SHIFT                                                                     0x0
#define THM_TMON0_REMOTE_END__DATA_MASK                                                                       0xFFFFFFFFL
//THM_TMON1_REMOTE_START
#define THM_TMON1_REMOTE_START__DATA__SHIFT                                                                   0x0
#define THM_TMON1_REMOTE_START__DATA_MASK                                                                     0xFFFFFFFFL
//THM_TMON1_REMOTE_END
#define THM_TMON1_REMOTE_END__DATA__SHIFT                                                                     0x0
#define THM_TMON1_REMOTE_END__DATA_MASK                                                                       0xFFFFFFFFL
//THM_TMON2_REMOTE_START
#define THM_TMON2_REMOTE_START__DATA__SHIFT                                                                   0x0
#define THM_TMON2_REMOTE_START__DATA_MASK                                                                     0xFFFFFFFFL
//THM_TMON2_REMOTE_END
#define THM_TMON2_REMOTE_END__DATA__SHIFT                                                                     0x0
#define THM_TMON2_REMOTE_END__DATA_MASK                                                                       0xFFFFFFFFL
//THM_TMON3_REMOTE_START
#define THM_TMON3_REMOTE_START__DATA__SHIFT                                                                   0x0
#define THM_TMON3_REMOTE_START__DATA_MASK                                                                     0xFFFFFFFFL
//THM_TMON3_REMOTE_END
#define THM_TMON3_REMOTE_END__DATA__SHIFT                                                                     0x0
#define THM_TMON3_REMOTE_END__DATA_MASK                                                                       0xFFFFFFFFL

#endif
