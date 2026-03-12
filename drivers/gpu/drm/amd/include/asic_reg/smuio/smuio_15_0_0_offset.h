/*
 * Copyright (C) 2025  Advanced Micro Devices, Inc.
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
#ifndef _smuio_15_0_0_OFFSET_HEADER
#define _smuio_15_0_0_OFFSET_HEADER



// addressBlock: smuio_smuio_misc_SmuSmuioDec
// base address: 0x5a000
#define regSMUIO_MCM_CONFIG                                                                             0x0023
#define regSMUIO_MCM_CONFIG_BASE_IDX                                                                    0
#define regIP_DISCOVERY_VERSION                                                                         0x0000
#define regIP_DISCOVERY_VERSION_BASE_IDX                                                                1
#define regSCRATCH_REGISTER0                                                                            0x01c6
#define regSCRATCH_REGISTER0_BASE_IDX                                                                   1
#define regSCRATCH_REGISTER1                                                                            0x01c7
#define regSCRATCH_REGISTER1_BASE_IDX                                                                   1
#define regSCRATCH_REGISTER2                                                                            0x01c8
#define regSCRATCH_REGISTER2_BASE_IDX                                                                   1
#define regSCRATCH_REGISTER3                                                                            0x01c9
#define regSCRATCH_REGISTER3_BASE_IDX                                                                   1
#define regSCRATCH_REGISTER4                                                                            0x01ca
#define regSCRATCH_REGISTER4_BASE_IDX                                                                   1
#define regSCRATCH_REGISTER5                                                                            0x01cb
#define regSCRATCH_REGISTER5_BASE_IDX                                                                   1
#define regSCRATCH_REGISTER6                                                                            0x01cc
#define regSCRATCH_REGISTER6_BASE_IDX                                                                   1
#define regSCRATCH_REGISTER7                                                                            0x01cd
#define regSCRATCH_REGISTER7_BASE_IDX                                                                   1
#define regIO_SMUIO_PINSTRAP                                                                            0x01ce
#define regIO_SMUIO_PINSTRAP_BASE_IDX                                                                   1


// addressBlock: smuio_smuio_reset_SmuSmuioDec
// base address: 0x5a300
#define regSMUIO_GFX_MISC_CNTL                                                                          0x00c5
#define regSMUIO_GFX_MISC_CNTL_BASE_IDX                                                                 0


// addressBlock: smuio_smuio_tsc_SmuSmuioDec
// base address: 0x5a8a0
#define regPWROK_REFCLK_GAP_CYCLES                                                                      0x0028
#define regPWROK_REFCLK_GAP_CYCLES_BASE_IDX                                                             1
#define regGOLDEN_TSC_INCREMENT_UPPER                                                                   0x002b
#define regGOLDEN_TSC_INCREMENT_UPPER_BASE_IDX                                                          1
#define regGOLDEN_TSC_INCREMENT_LOWER                                                                   0x002c
#define regGOLDEN_TSC_INCREMENT_LOWER_BASE_IDX                                                          1
#define regGOLDEN_TSC_COUNT_UPPER                                                                       0x0030
#define regGOLDEN_TSC_COUNT_UPPER_BASE_IDX                                                              1
#define regGOLDEN_TSC_COUNT_LOWER                                                                       0x0031
#define regGOLDEN_TSC_COUNT_LOWER_BASE_IDX                                                              1
#define regSOC_GOLDEN_TSC_SHADOW_UPPER                                                                  0x0032
#define regSOC_GOLDEN_TSC_SHADOW_UPPER_BASE_IDX                                                         1
#define regSOC_GOLDEN_TSC_SHADOW_LOWER                                                                  0x0033
#define regSOC_GOLDEN_TSC_SHADOW_LOWER_BASE_IDX                                                         1
#define regSOC_GAP_PWROK                                                                                0x0034
#define regSOC_GAP_PWROK_BASE_IDX                                                                       1


// addressBlock: smuio_smuio_swtimer_SmuSmuioDec
// base address: 0x5aca8
#define regPWR_VIRT_RESET_REQ                                                                           0x012a
#define regPWR_VIRT_RESET_REQ_BASE_IDX                                                                  1
#define regPWR_DISP_TIMER_CONTROL                                                                       0x012b
#define regPWR_DISP_TIMER_CONTROL_BASE_IDX                                                              1
#define regPWR_DISP_TIMER_DEBUG                                                                         0x012c
#define regPWR_DISP_TIMER_DEBUG_BASE_IDX                                                                1
#define regPWR_DISP_TIMER_ELAPSED_CONTROL                                                               0x012d
#define regPWR_DISP_TIMER_ELAPSED_CONTROL_BASE_IDX                                                      1
#define regPWR_DISP_TIMER2_CONTROL                                                                      0x012e
#define regPWR_DISP_TIMER2_CONTROL_BASE_IDX                                                             1
#define regPWR_DISP_TIMER2_DEBUG                                                                        0x012f
#define regPWR_DISP_TIMER2_DEBUG_BASE_IDX                                                               1
#define regPWR_DISP_TIMER2_ELAPSED_CONTROL                                                              0x0130
#define regPWR_DISP_TIMER2_ELAPSED_CONTROL_BASE_IDX                                                     1
#define regPWR_DISP_TIMER_GLOBAL_CONTROL                                                                0x0131
#define regPWR_DISP_TIMER_GLOBAL_CONTROL_BASE_IDX                                                       1
#define regPWR_IH_CONTROL                                                                               0x0132
#define regPWR_IH_CONTROL_BASE_IDX                                                                      1


#endif
