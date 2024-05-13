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
#ifndef _smuio_13_0_3_OFFSET_HEADER
#define _smuio_13_0_3_OFFSET_HEADER



// addressBlock: aid_smuio_smuio_reset_SmuSmuioDec
// base address: 0x5a300
#define regSMUIO_MP_RESET_INTR                                                                          0x00c1
#define regSMUIO_MP_RESET_INTR_BASE_IDX                                                                 1
#define regSMUIO_SOC_HALT                                                                               0x00c2
#define regSMUIO_SOC_HALT_BASE_IDX                                                                      1


// addressBlock: aid_smuio_smuio_tsc_SmuSmuioDec
// base address: 0x5a8a0
#define regPWROK_REFCLK_GAP_CYCLES                                                                      0x0028
#define regPWROK_REFCLK_GAP_CYCLES_BASE_IDX                                                             2
#define regGOLDEN_TSC_INCREMENT_UPPER                                                                   0x002b
#define regGOLDEN_TSC_INCREMENT_UPPER_BASE_IDX                                                          2
#define regGOLDEN_TSC_INCREMENT_LOWER                                                                   0x002c
#define regGOLDEN_TSC_INCREMENT_LOWER_BASE_IDX                                                          2
#define regGOLDEN_TSC_COUNT_UPPER                                                                       0x002d
#define regGOLDEN_TSC_COUNT_UPPER_BASE_IDX                                                              2
#define regGOLDEN_TSC_COUNT_LOWER                                                                       0x002e
#define regGOLDEN_TSC_COUNT_LOWER_BASE_IDX                                                              2
#define regSOC_GOLDEN_TSC_SHADOW_UPPER                                                                  0x002f
#define regSOC_GOLDEN_TSC_SHADOW_UPPER_BASE_IDX                                                         2
#define regSOC_GOLDEN_TSC_SHADOW_LOWER                                                                  0x0030
#define regSOC_GOLDEN_TSC_SHADOW_LOWER_BASE_IDX                                                         2
#define regSOC_GAP_PWROK                                                                                0x0031
#define regSOC_GAP_PWROK_BASE_IDX                                                                       2


// addressBlock: aid_smuio_smuio_swtimer_SmuSmuioDec
// base address: 0x5ac70
#define regPWR_VIRT_RESET_REQ                                                                           0x011c
#define regPWR_VIRT_RESET_REQ_BASE_IDX                                                                  2
#define regPWR_DISP_TIMER_CONTROL                                                                       0x011d
#define regPWR_DISP_TIMER_CONTROL_BASE_IDX                                                              2
#define regPWR_DISP_TIMER_DEBUG                                                                         0x011e
#define regPWR_DISP_TIMER_DEBUG_BASE_IDX                                                                2
#define regPWR_DISP_TIMER2_CONTROL                                                                      0x011f
#define regPWR_DISP_TIMER2_CONTROL_BASE_IDX                                                             2
#define regPWR_DISP_TIMER2_DEBUG                                                                        0x0120
#define regPWR_DISP_TIMER2_DEBUG_BASE_IDX                                                               2
#define regPWR_DISP_TIMER_GLOBAL_CONTROL                                                                0x0121
#define regPWR_DISP_TIMER_GLOBAL_CONTROL_BASE_IDX                                                       2
#define regPWR_IH_CONTROL                                                                               0x0122
#define regPWR_IH_CONTROL_BASE_IDX                                                                      2


// addressBlock: aid_smuio_smuio_misc_SmuSmuioDec
// base address: 0x5a000
#define regSMUIO_MCM_CONFIG                                                                             0x0023
#define regSMUIO_MCM_CONFIG_BASE_IDX                                                                    1
#define regIP_DISCOVERY_VERSION                                                                         0x0000
#define regIP_DISCOVERY_VERSION_BASE_IDX                                                                2
#define regSCRATCH_REGISTER0                                                                            0x01bd
#define regSCRATCH_REGISTER0_BASE_IDX                                                                   2
#define regSCRATCH_REGISTER1                                                                            0x01be
#define regSCRATCH_REGISTER1_BASE_IDX                                                                   2
#define regSCRATCH_REGISTER2                                                                            0x01bf
#define regSCRATCH_REGISTER2_BASE_IDX                                                                   2
#define regSCRATCH_REGISTER3                                                                            0x01c0
#define regSCRATCH_REGISTER3_BASE_IDX                                                                   2
#define regSCRATCH_REGISTER4                                                                            0x01c1
#define regSCRATCH_REGISTER4_BASE_IDX                                                                   2
#define regSCRATCH_REGISTER5                                                                            0x01c2
#define regSCRATCH_REGISTER5_BASE_IDX                                                                   2
#define regSCRATCH_REGISTER6                                                                            0x01c3
#define regSCRATCH_REGISTER6_BASE_IDX                                                                   2
#define regSCRATCH_REGISTER7                                                                            0x01c4
#define regSCRATCH_REGISTER7_BASE_IDX                                                                   2


// addressBlock: aid_smuio_smuio_gpio_SmuSmuioDec
// base address: 0x5a500
#define regSMU_GPIOPAD_SW_INT_STAT                                                                      0x0140
#define regSMU_GPIOPAD_SW_INT_STAT_BASE_IDX                                                             1
#define regSMU_GPIOPAD_MASK                                                                             0x0141
#define regSMU_GPIOPAD_MASK_BASE_IDX                                                                    1
#define regSMU_GPIOPAD_A                                                                                0x0142
#define regSMU_GPIOPAD_A_BASE_IDX                                                                       1
#define regSMU_GPIOPAD_TXIMPSEL                                                                         0x0143
#define regSMU_GPIOPAD_TXIMPSEL_BASE_IDX                                                                1
#define regSMU_GPIOPAD_EN                                                                               0x0144
#define regSMU_GPIOPAD_EN_BASE_IDX                                                                      1
#define regSMU_GPIOPAD_Y                                                                                0x0145
#define regSMU_GPIOPAD_Y_BASE_IDX                                                                       1
#define regSMU_GPIOPAD_RXEN                                                                             0x0146
#define regSMU_GPIOPAD_RXEN_BASE_IDX                                                                    1
#define regSMU_GPIOPAD_RCVR_SEL0                                                                        0x0147
#define regSMU_GPIOPAD_RCVR_SEL0_BASE_IDX                                                               1
#define regSMU_GPIOPAD_RCVR_SEL1                                                                        0x0148
#define regSMU_GPIOPAD_RCVR_SEL1_BASE_IDX                                                               1
#define regSMU_GPIOPAD_PU_EN                                                                            0x0149
#define regSMU_GPIOPAD_PU_EN_BASE_IDX                                                                   1
#define regSMU_GPIOPAD_PD_EN                                                                            0x014a
#define regSMU_GPIOPAD_PD_EN_BASE_IDX                                                                   1
#define regSMU_GPIOPAD_PINSTRAPS                                                                        0x014b
#define regSMU_GPIOPAD_PINSTRAPS_BASE_IDX                                                               1
#define regDFT_PINSTRAPS                                                                                0x014c
#define regDFT_PINSTRAPS_BASE_IDX                                                                       1
#define regSMU_GPIOPAD_INT_STAT_EN                                                                      0x014d
#define regSMU_GPIOPAD_INT_STAT_EN_BASE_IDX                                                             1
#define regSMU_GPIOPAD_INT_STAT                                                                         0x014e
#define regSMU_GPIOPAD_INT_STAT_BASE_IDX                                                                1
#define regSMU_GPIOPAD_INT_STAT_AK                                                                      0x014f
#define regSMU_GPIOPAD_INT_STAT_AK_BASE_IDX                                                             1
#define regSMU_GPIOPAD_INT_EN                                                                           0x0150
#define regSMU_GPIOPAD_INT_EN_BASE_IDX                                                                  1
#define regSMU_GPIOPAD_INT_TYPE                                                                         0x0151
#define regSMU_GPIOPAD_INT_TYPE_BASE_IDX                                                                1
#define regSMU_GPIOPAD_INT_POLARITY                                                                     0x0152
#define regSMU_GPIOPAD_INT_POLARITY_BASE_IDX                                                            1
#define regSMUIO_PCC_GPIO_SELECT                                                                        0x0155
#define regSMUIO_PCC_GPIO_SELECT_BASE_IDX                                                               1
#define regSMU_GPIOPAD_S0                                                                               0x0156
#define regSMU_GPIOPAD_S0_BASE_IDX                                                                      1
#define regSMU_GPIOPAD_S1                                                                               0x0157
#define regSMU_GPIOPAD_S1_BASE_IDX                                                                      1
#define regSMU_GPIOPAD_SCHMEN                                                                           0x0158
#define regSMU_GPIOPAD_SCHMEN_BASE_IDX                                                                  1
#define regSMU_GPIOPAD_SCL_EN                                                                           0x0159
#define regSMU_GPIOPAD_SCL_EN_BASE_IDX                                                                  1
#define regSMU_GPIOPAD_SDA_EN                                                                           0x015a
#define regSMU_GPIOPAD_SDA_EN_BASE_IDX                                                                  1
#define regSMUIO_GPIO_INT0_SELECT                                                                       0x015b
#define regSMUIO_GPIO_INT0_SELECT_BASE_IDX                                                              1
#define regSMUIO_GPIO_INT1_SELECT                                                                       0x015c
#define regSMUIO_GPIO_INT1_SELECT_BASE_IDX                                                              1
#define regSMUIO_GPIO_INT2_SELECT                                                                       0x015d
#define regSMUIO_GPIO_INT2_SELECT_BASE_IDX                                                              1
#define regSMUIO_GPIO_INT3_SELECT                                                                       0x015e
#define regSMUIO_GPIO_INT3_SELECT_BASE_IDX                                                              1
#define regSMU_GPIOPAD_MP_INT0_STAT                                                                     0x015f
#define regSMU_GPIOPAD_MP_INT0_STAT_BASE_IDX                                                            1
#define regSMU_GPIOPAD_MP_INT1_STAT                                                                     0x0160
#define regSMU_GPIOPAD_MP_INT1_STAT_BASE_IDX                                                            1
#define regSMU_GPIOPAD_MP_INT2_STAT                                                                     0x0161
#define regSMU_GPIOPAD_MP_INT2_STAT_BASE_IDX                                                            1
#define regSMU_GPIOPAD_MP_INT3_STAT                                                                     0x0162
#define regSMU_GPIOPAD_MP_INT3_STAT_BASE_IDX                                                            1
#define regSMIO_INDEX                                                                                   0x0163
#define regSMIO_INDEX_BASE_IDX                                                                          1
#define regS0_VID_SMIO_CNTL                                                                             0x0164
#define regS0_VID_SMIO_CNTL_BASE_IDX                                                                    1
#define regS1_VID_SMIO_CNTL                                                                             0x0165
#define regS1_VID_SMIO_CNTL_BASE_IDX                                                                    1
#define regOPEN_DRAIN_SELECT                                                                            0x0166
#define regOPEN_DRAIN_SELECT_BASE_IDX                                                                   1
#define regSMIO_ENABLE                                                                                  0x0167
#define regSMIO_ENABLE_BASE_IDX                                                                         1

#endif
