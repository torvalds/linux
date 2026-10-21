/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Advanced Micro Devices, Inc. All rights reserved.
 */


#ifndef _dpcs_6_0_0_OFFSET_HEADER
#define _dpcs_6_0_0_OFFSET_HEADER

// addressBlock: dpcssys_dcio_dcio_dispdec
// base address: 0x0
#define regHPD_CTRL                                                                                     0x286c
#define regHPD_CTRL_BASE_IDX                                                                            2
#define regDC_PINSTRAPS                                                                                 0x2880
#define regDC_PINSTRAPS_BASE_IDX                                                                        2

// addressBlock: dpcssys_dcio_dcio_chip_dispdec
// base address: 0x0
#define regDC_GPIO_DDC1_MASK                                                                            0x28d0
#define regDC_GPIO_DDC1_MASK_BASE_IDX                                                                   2
#define regDC_GPIO_DDC2_MASK                                                                            0x28d4
#define regDC_GPIO_DDC2_MASK_BASE_IDX                                                                   2
#define regDC_GPIO_DDC3_MASK                                                                            0x28d8
#define regDC_GPIO_DDC3_MASK_BASE_IDX                                                                   2
#define regDC_GPIO_DDC4_MASK                                                                            0x28dc
#define regDC_GPIO_DDC4_MASK_BASE_IDX                                                                   2
#define regPHY_AUX_CNTL                                                                                 0x28ff
#define regPHY_AUX_CNTL_BASE_IDX                                                                        2
#define regDC_GPIO_AUX_CTRL_5                                                                           0x291d
#define regDC_GPIO_AUX_CTRL_5_BASE_IDX                                                                  2

// addressBlock: dpcssys_dcio_i3c_pad_control_ddc1_dc_i3c_dispdec
// base address: 0x0
#define regDC_I3C0_DC_I3CPAD_CONTROL0                                                                   0x2f6c
#define regDC_I3C0_DC_I3CPAD_CONTROL0_BASE_IDX                                                          2
#define regDC_I3C0_DC_I3CPAD_CONTROL1                                                                   0x2f6d
#define regDC_I3C0_DC_I3CPAD_CONTROL1_BASE_IDX                                                          2

// addressBlock: dpcssys_dcio_i3c_pad_control_ddc2_dc_i3c_dispdec
// base address: 0x8
#define regDC_I3C1_DC_I3CPAD_CONTROL0                                                                   0x2f6e
#define regDC_I3C1_DC_I3CPAD_CONTROL0_BASE_IDX                                                          2
#define regDC_I3C1_DC_I3CPAD_CONTROL1                                                                   0x2f6f
#define regDC_I3C1_DC_I3CPAD_CONTROL1_BASE_IDX                                                          2

#endif
