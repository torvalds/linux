/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Advanced Micro Devices, Inc. All rights reserved.
 */

#ifndef _dpcs_6_0_0_SH_MASK_HEADER
#define _dpcs_6_0_0_SH_MASK_HEADER

// addressBlock: dpcssys_dcio_dcio_dispdec
//HPD_CTRL
#define HPD_CTRL__HPD1_Y_POL_INVERT__SHIFT                                                                    0x0
#define HPD_CTRL__HPD2_Y_POL_INVERT__SHIFT                                                                    0x1
#define HPD_CTRL__HPD3_Y_POL_INVERT__SHIFT                                                                    0x2
#define HPD_CTRL__HPD4_Y_POL_INVERT__SHIFT                                                                    0x3
#define HPD_CTRL__HPD1_Y_POL_INVERT_MASK                                                                      0x00000001L
#define HPD_CTRL__HPD2_Y_POL_INVERT_MASK                                                                      0x00000002L
#define HPD_CTRL__HPD3_Y_POL_INVERT_MASK                                                                      0x00000004L
#define HPD_CTRL__HPD4_Y_POL_INVERT_MASK                                                                      0x00000008L
//DC_PINSTRAPS
#define DC_PINSTRAPS__DC_PINSTRAPS_AUDIO__SHIFT                                                               0xe
#define DC_PINSTRAPS__DC_PINSTRAPS_AUDIO_MASK                                                                 0x0000C000L

// addressBlock: dpcssys_dcio_dcio_chip_dispdec
//DC_GPIO_DDC1_MASK
#define DC_GPIO_DDC1_MASK__AUX_PAD1_MODE__SHIFT                                                               0x10
#define DC_GPIO_DDC1_MASK__AUX_PAD1_MODE_MASK                                                                 0x00010000L

// addressBlock: dpcssys_dcio_i3c_pad_control_ddc1_dc_i3c_dispdec
//DC_I3C0_DC_I3CPAD_CONTROL0
#define DC_I3C0_DC_I3CPAD_CONTROL0__DC_I3CPAD_DDCCLK_MASK__SHIFT                                              0x0
#define DC_I3C0_DC_I3CPAD_CONTROL0__DC_I3CPAD_DDCDATA_MASK__SHIFT                                             0x1
#define DC_I3C0_DC_I3CPAD_CONTROL0__DC_I3CPAD_PD_EN__SHIFT                                                    0x3
#define DC_I3C0_DC_I3CPAD_CONTROL0__DC_I3CPAD_CLK_A__SHIFT                                                    0x5
#define DC_I3C0_DC_I3CPAD_CONTROL0__DC_I3CPAD_DATA_A__SHIFT                                                   0x8
#define DC_I3C0_DC_I3CPAD_CONTROL0__DC_I3CPAD_CLK_EN__SHIFT                                                   0xc
#define DC_I3C0_DC_I3CPAD_CONTROL0__DC_I3CPAD_DATA_EN__SHIFT                                                  0x10
#define DC_I3C0_DC_I3CPAD_CONTROL0__DC_I3CPAD_CLK_Y__SHIFT                                                    0x14
#define DC_I3C0_DC_I3CPAD_CONTROL0__DC_I3CPAD_DATA_Y__SHIFT                                                   0x18
#define DC_I3C0_DC_I3CPAD_CONTROL0__DC_I3CPAD_DDCCLK_MASK_MASK                                                0x00000001L
#define DC_I3C0_DC_I3CPAD_CONTROL0__DC_I3CPAD_DDCDATA_MASK_MASK                                               0x00000002L
#define DC_I3C0_DC_I3CPAD_CONTROL0__DC_I3CPAD_PD_EN_MASK                                                      0x00000018L
#define DC_I3C0_DC_I3CPAD_CONTROL0__DC_I3CPAD_CLK_A_MASK                                                      0x00000020L
#define DC_I3C0_DC_I3CPAD_CONTROL0__DC_I3CPAD_DATA_A_MASK                                                     0x00000100L
#define DC_I3C0_DC_I3CPAD_CONTROL0__DC_I3CPAD_CLK_EN_MASK                                                     0x00001000L
#define DC_I3C0_DC_I3CPAD_CONTROL0__DC_I3CPAD_DATA_EN_MASK                                                    0x00010000L
#define DC_I3C0_DC_I3CPAD_CONTROL0__DC_I3CPAD_CLK_Y_MASK                                                      0x00100000L
#define DC_I3C0_DC_I3CPAD_CONTROL0__DC_I3CPAD_DATA_Y_MASK                                                     0x01000000L
//DC_I3C0_DC_I3CPAD_CONTROL1
#define DC_I3C0_DC_I3CPAD_CONTROL1__DC_I3CPAD_STR__SHIFT                                                      0x0
#define DC_I3C0_DC_I3CPAD_CONTROL1__DC_I3CPAD_RXSEL__SHIFT                                                    0x6
#define DC_I3C0_DC_I3CPAD_CONTROL1__DC_I3CPAD_STR_MASK                                                        0x0000000FL
#define DC_I3C0_DC_I3CPAD_CONTROL1__DC_I3CPAD_RXSEL_MASK                                                      0x000000C0L

#endif
