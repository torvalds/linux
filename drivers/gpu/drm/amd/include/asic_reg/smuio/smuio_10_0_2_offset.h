/*
 * Copyright (C) 2023  Advanced Micro Devices, Inc.
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
#ifndef _smuio_10_0_2_OFFSET_HEADER

// addressBlock: smuio_smuio_misc_SmuSmuioDec
// base address: 0x5a000
#define mmSMUIO_MCM_CONFIG                                                                             0x0023
#define mmSMUIO_MCM_CONFIG_BASE_IDX                                                                    0
#define mmIP_DISCOVERY_VERSION                                                                         0x0000
#define mmIP_DISCOVERY_VERSION_BASE_IDX                                                                1
#define mmIO_SMUIO_PINSTRAP                                                                            0x01b1
#define mmIO_SMUIO_PINSTRAP_BASE_IDX                                                                   1
#define mmSCRATCH_REGISTER0                                                                            0x01b2
#define mmSCRATCH_REGISTER0_BASE_IDX                                                                   1
#define mmSCRATCH_REGISTER1                                                                            0x01b3
#define mmSCRATCH_REGISTER1_BASE_IDX                                                                   1
#define mmSCRATCH_REGISTER2                                                                            0x01b4
#define mmSCRATCH_REGISTER2_BASE_IDX                                                                   1
#define mmSCRATCH_REGISTER3                                                                            0x01b5
#define mmSCRATCH_REGISTER3_BASE_IDX                                                                   1
#define mmSCRATCH_REGISTER4                                                                            0x01b6
#define mmSCRATCH_REGISTER4_BASE_IDX                                                                   1
#define mmSCRATCH_REGISTER5                                                                            0x01b7
#define mmSCRATCH_REGISTER5_BASE_IDX                                                                   1
#define mmSCRATCH_REGISTER6                                                                            0x01b8
#define mmSCRATCH_REGISTER6_BASE_IDX                                                                   1
#define mmSCRATCH_REGISTER7                                                                            0x01b9
#define mmSCRATCH_REGISTER7_BASE_IDX                                                                   1


// addressBlock: smuio_smuio_reset_SmuSmuioDec
// base address: 0x5a300
#define mmSMUIO_MP_RESET_INTR                                                                          0x00c1
#define mmSMUIO_MP_RESET_INTR_BASE_IDX                                                                 0
#define mmSMUIO_SOC_HALT                                                                               0x00c2
#define mmSMUIO_SOC_HALT_BASE_IDX                                                                      0
#define mmSMUIO_GFX_MISC_CNTL                                                                          0x00c8
#define mmSMUIO_GFX_MISC_CNTL_BASE_IDX                                                                 0


// addressBlock: smuio_smuio_ccxctrl_SmuSmuioDec
// base address: 0x5a000
#define mmPWROK_REFCLK_GAP_CYCLES                                                                      0x0001
#define mmPWROK_REFCLK_GAP_CYCLES_BASE_IDX                                                             1
#define mmGOLDEN_TSC_INCREMENT_UPPER                                                                   0x0004
#define mmGOLDEN_TSC_INCREMENT_UPPER_BASE_IDX                                                          1
#define mmGOLDEN_TSC_INCREMENT_LOWER                                                                   0x0005
#define mmGOLDEN_TSC_INCREMENT_LOWER_BASE_IDX                                                          1
#define mmGOLDEN_TSC_COUNT_UPPER                                                                       0x0025
#define mmGOLDEN_TSC_COUNT_UPPER_BASE_IDX                                                              1
#define mmGOLDEN_TSC_COUNT_LOWER                                                                       0x0026
#define mmGOLDEN_TSC_COUNT_LOWER_BASE_IDX                                                              1
#define mmGFX_GOLDEN_TSC_SHADOW_UPPER                                                                  0x0029
#define mmGFX_GOLDEN_TSC_SHADOW_UPPER_BASE_IDX                                                         1
#define mmGFX_GOLDEN_TSC_SHADOW_LOWER                                                                  0x002a
#define mmGFX_GOLDEN_TSC_SHADOW_LOWER_BASE_IDX                                                         1
#define mmSOC_GOLDEN_TSC_SHADOW_UPPER                                                                  0x002b
#define mmSOC_GOLDEN_TSC_SHADOW_UPPER_BASE_IDX                                                         1
#define mmSOC_GOLDEN_TSC_SHADOW_LOWER                                                                  0x002c
#define mmSOC_GOLDEN_TSC_SHADOW_LOWER_BASE_IDX                                                         1
#define mmSOC_GAP_PWROK                                                                                0x002d
#define mmSOC_GAP_PWROK_BASE_IDX                                                                       1

// addressBlock: smuio_smuio_swtimer_SmuSmuioDec
// base address: 0x5ac40
#define mmPWR_VIRT_RESET_REQ                                                                           0x0110
#define mmPWR_VIRT_RESET_REQ_BASE_IDX                                                                  1
#define mmPWR_DISP_TIMER_CONTROL                                                                       0x0111
#define mmPWR_DISP_TIMER_CONTROL_BASE_IDX                                                              1
#define mmPWR_DISP_TIMER2_CONTROL                                                                      0x0113
#define mmPWR_DISP_TIMER2_CONTROL_BASE_IDX                                                             1
#define mmPWR_DISP_TIMER_GLOBAL_CONTROL                                                                0x0115
#define mmPWR_DISP_TIMER_GLOBAL_CONTROL_BASE_IDX                                                       1
#define mmPWR_IH_CONTROL                                                                               0x0116
#define mmPWR_IH_CONTROL_BASE_IDX                                                                      1

// addressBlock: smuio_smuio_svi0_SmuSmuioDec
// base address: 0x6f000
#define mmSMUSVI0_TEL_PLANE0                                                                           0x520e
#define mmSMUSVI0_TEL_PLANE0_BASE_IDX                                                                  1
#define mmSMUSVI0_PLANE0_CURRENTVID                                                                    0x5217
#define mmSMUSVI0_PLANE0_CURRENTVID_BASE_IDX                                                           1

#endif
