/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Advanced Micro Devices, Inc. */

#ifndef _dpcs_4_0_0_SH_MASK_HEADER
#define _dpcs_4_0_0_SH_MASK_HEADER

// addressBlock: dpcssys_dcio_dcio_dispdec
//DC_GENERICA
#define DC_GENERICA__GENERICA_SEL__SHIFT                                                                      0x7
#define DC_GENERICA__GENERICA_SEL_MASK                                                                        0x00000F80L
//DC_GENERICB
#define DC_GENERICB__GENERICB_SEL__SHIFT                                                                      0x8
#define DC_GENERICB__GENERICB_SEL_MASK                                                                        0x00000F00L
//DCIO_CLOCK_CNTL
#define DCIO_CLOCK_CNTL__DCIO_TEST_CLK_SEL__SHIFT                                                             0x0
#define DCIO_CLOCK_CNTL__DISPCLK_R_DCIO_GATE_DIS__SHIFT                                                       0x5
#define DCIO_CLOCK_CNTL__DCIO_TEST_CLK_SEL_MASK                                                               0x0000001FL
#define DCIO_CLOCK_CNTL__DISPCLK_R_DCIO_GATE_DIS_MASK                                                         0x00000020L
//DC_REF_CLK_CNTL
#define DC_REF_CLK_CNTL__GENLK_CLK_OUTPUT_SEL__SHIFT                                                          0x8
#define DC_REF_CLK_CNTL__GENLK_CLK_OUTPUT_SEL_MASK                                                            0x00000300L
//UNIPHYA_LINK_CNTL
#define UNIPHYA_LINK_CNTL__UNIPHY_CHANNEL0_INVERT__SHIFT                                                      0xc
#define UNIPHYA_LINK_CNTL__UNIPHY_CHANNEL1_INVERT__SHIFT                                                      0xd
#define UNIPHYA_LINK_CNTL__UNIPHY_CHANNEL2_INVERT__SHIFT                                                      0xe
#define UNIPHYA_LINK_CNTL__UNIPHY_CHANNEL3_INVERT__SHIFT                                                      0xf
#define UNIPHYA_LINK_CNTL__UNIPHY_CHANNEL0_INVERT_MASK                                                        0x00001000L
#define UNIPHYA_LINK_CNTL__UNIPHY_CHANNEL1_INVERT_MASK                                                        0x00002000L
#define UNIPHYA_LINK_CNTL__UNIPHY_CHANNEL2_INVERT_MASK                                                        0x00004000L
#define UNIPHYA_LINK_CNTL__UNIPHY_CHANNEL3_INVERT_MASK                                                        0x00008000L
//UNIPHYA_CHANNEL_XBAR_CNTL
#define UNIPHYA_CHANNEL_XBAR_CNTL__UNIPHY_CHANNEL0_XBAR_SOURCE__SHIFT                                         0x0
#define UNIPHYA_CHANNEL_XBAR_CNTL__UNIPHY_CHANNEL1_XBAR_SOURCE__SHIFT                                         0x8
#define UNIPHYA_CHANNEL_XBAR_CNTL__UNIPHY_CHANNEL2_XBAR_SOURCE__SHIFT                                         0x10
#define UNIPHYA_CHANNEL_XBAR_CNTL__UNIPHY_CHANNEL3_XBAR_SOURCE__SHIFT                                         0x18
#define UNIPHYA_CHANNEL_XBAR_CNTL__DOUT_PHY_CHANNEL0_EN__SHIFT                                                0x1c
#define UNIPHYA_CHANNEL_XBAR_CNTL__DOUT_PHY_CHANNEL1_EN__SHIFT                                                0x1d
#define UNIPHYA_CHANNEL_XBAR_CNTL__DOUT_PHY_CHANNEL2_EN__SHIFT                                                0x1e
#define UNIPHYA_CHANNEL_XBAR_CNTL__DOUT_PHY_CHANNEL3_EN__SHIFT                                                0x1f
#define UNIPHYA_CHANNEL_XBAR_CNTL__UNIPHY_CHANNEL0_XBAR_SOURCE_MASK                                           0x00000003L
#define UNIPHYA_CHANNEL_XBAR_CNTL__UNIPHY_CHANNEL1_XBAR_SOURCE_MASK                                           0x00000300L
#define UNIPHYA_CHANNEL_XBAR_CNTL__UNIPHY_CHANNEL2_XBAR_SOURCE_MASK                                           0x00030000L
#define UNIPHYA_CHANNEL_XBAR_CNTL__UNIPHY_CHANNEL3_XBAR_SOURCE_MASK                                           0x03000000L
#define UNIPHYA_CHANNEL_XBAR_CNTL__DOUT_PHY_CHANNEL0_EN_MASK                                                  0x10000000L
#define UNIPHYA_CHANNEL_XBAR_CNTL__DOUT_PHY_CHANNEL1_EN_MASK                                                  0x20000000L
#define UNIPHYA_CHANNEL_XBAR_CNTL__DOUT_PHY_CHANNEL2_EN_MASK                                                  0x40000000L
#define UNIPHYA_CHANNEL_XBAR_CNTL__DOUT_PHY_CHANNEL3_EN_MASK                                                  0x80000000L
//UNIPHYB_LINK_CNTL
#define UNIPHYB_LINK_CNTL__UNIPHY_CHANNEL0_INVERT__SHIFT                                                      0xc
#define UNIPHYB_LINK_CNTL__UNIPHY_CHANNEL1_INVERT__SHIFT                                                      0xd
#define UNIPHYB_LINK_CNTL__UNIPHY_CHANNEL2_INVERT__SHIFT                                                      0xe
#define UNIPHYB_LINK_CNTL__UNIPHY_CHANNEL3_INVERT__SHIFT                                                      0xf
#define UNIPHYB_LINK_CNTL__UNIPHY_CHANNEL0_INVERT_MASK                                                        0x00001000L
#define UNIPHYB_LINK_CNTL__UNIPHY_CHANNEL1_INVERT_MASK                                                        0x00002000L
#define UNIPHYB_LINK_CNTL__UNIPHY_CHANNEL2_INVERT_MASK                                                        0x00004000L
#define UNIPHYB_LINK_CNTL__UNIPHY_CHANNEL3_INVERT_MASK                                                        0x00008000L
//UNIPHYB_CHANNEL_XBAR_CNTL
#define UNIPHYB_CHANNEL_XBAR_CNTL__UNIPHY_CHANNEL0_XBAR_SOURCE__SHIFT                                         0x0
#define UNIPHYB_CHANNEL_XBAR_CNTL__UNIPHY_CHANNEL1_XBAR_SOURCE__SHIFT                                         0x8
#define UNIPHYB_CHANNEL_XBAR_CNTL__UNIPHY_CHANNEL2_XBAR_SOURCE__SHIFT                                         0x10
#define UNIPHYB_CHANNEL_XBAR_CNTL__UNIPHY_CHANNEL3_XBAR_SOURCE__SHIFT                                         0x18
#define UNIPHYB_CHANNEL_XBAR_CNTL__DOUT_PHY_CHANNEL0_EN__SHIFT                                                0x1c
#define UNIPHYB_CHANNEL_XBAR_CNTL__DOUT_PHY_CHANNEL1_EN__SHIFT                                                0x1d
#define UNIPHYB_CHANNEL_XBAR_CNTL__DOUT_PHY_CHANNEL2_EN__SHIFT                                                0x1e
#define UNIPHYB_CHANNEL_XBAR_CNTL__DOUT_PHY_CHANNEL3_EN__SHIFT                                                0x1f
#define UNIPHYB_CHANNEL_XBAR_CNTL__UNIPHY_CHANNEL0_XBAR_SOURCE_MASK                                           0x00000003L
#define UNIPHYB_CHANNEL_XBAR_CNTL__UNIPHY_CHANNEL1_XBAR_SOURCE_MASK                                           0x00000300L
#define UNIPHYB_CHANNEL_XBAR_CNTL__UNIPHY_CHANNEL2_XBAR_SOURCE_MASK                                           0x00030000L
#define UNIPHYB_CHANNEL_XBAR_CNTL__UNIPHY_CHANNEL3_XBAR_SOURCE_MASK                                           0x03000000L
#define UNIPHYB_CHANNEL_XBAR_CNTL__DOUT_PHY_CHANNEL0_EN_MASK                                                  0x10000000L
#define UNIPHYB_CHANNEL_XBAR_CNTL__DOUT_PHY_CHANNEL1_EN_MASK                                                  0x20000000L
#define UNIPHYB_CHANNEL_XBAR_CNTL__DOUT_PHY_CHANNEL2_EN_MASK                                                  0x40000000L
#define UNIPHYB_CHANNEL_XBAR_CNTL__DOUT_PHY_CHANNEL3_EN_MASK                                                  0x80000000L
//UNIPHYC_LINK_CNTL
#define UNIPHYC_LINK_CNTL__UNIPHY_CHANNEL0_INVERT__SHIFT                                                      0xc
#define UNIPHYC_LINK_CNTL__UNIPHY_CHANNEL1_INVERT__SHIFT                                                      0xd
#define UNIPHYC_LINK_CNTL__UNIPHY_CHANNEL2_INVERT__SHIFT                                                      0xe
#define UNIPHYC_LINK_CNTL__UNIPHY_CHANNEL3_INVERT__SHIFT                                                      0xf
#define UNIPHYC_LINK_CNTL__UNIPHY_CHANNEL0_INVERT_MASK                                                        0x00001000L
#define UNIPHYC_LINK_CNTL__UNIPHY_CHANNEL1_INVERT_MASK                                                        0x00002000L
#define UNIPHYC_LINK_CNTL__UNIPHY_CHANNEL2_INVERT_MASK                                                        0x00004000L
#define UNIPHYC_LINK_CNTL__UNIPHY_CHANNEL3_INVERT_MASK                                                        0x00008000L
//UNIPHYC_CHANNEL_XBAR_CNTL
#define UNIPHYC_CHANNEL_XBAR_CNTL__UNIPHY_CHANNEL0_XBAR_SOURCE__SHIFT                                         0x0
#define UNIPHYC_CHANNEL_XBAR_CNTL__UNIPHY_CHANNEL1_XBAR_SOURCE__SHIFT                                         0x8
#define UNIPHYC_CHANNEL_XBAR_CNTL__UNIPHY_CHANNEL2_XBAR_SOURCE__SHIFT                                         0x10
#define UNIPHYC_CHANNEL_XBAR_CNTL__UNIPHY_CHANNEL3_XBAR_SOURCE__SHIFT                                         0x18
#define UNIPHYC_CHANNEL_XBAR_CNTL__DOUT_PHY_CHANNEL0_EN__SHIFT                                                0x1c
#define UNIPHYC_CHANNEL_XBAR_CNTL__DOUT_PHY_CHANNEL1_EN__SHIFT                                                0x1d
#define UNIPHYC_CHANNEL_XBAR_CNTL__DOUT_PHY_CHANNEL2_EN__SHIFT                                                0x1e
#define UNIPHYC_CHANNEL_XBAR_CNTL__DOUT_PHY_CHANNEL3_EN__SHIFT                                                0x1f
#define UNIPHYC_CHANNEL_XBAR_CNTL__UNIPHY_CHANNEL0_XBAR_SOURCE_MASK                                           0x00000003L
#define UNIPHYC_CHANNEL_XBAR_CNTL__UNIPHY_CHANNEL1_XBAR_SOURCE_MASK                                           0x00000300L
#define UNIPHYC_CHANNEL_XBAR_CNTL__UNIPHY_CHANNEL2_XBAR_SOURCE_MASK                                           0x00030000L
#define UNIPHYC_CHANNEL_XBAR_CNTL__UNIPHY_CHANNEL3_XBAR_SOURCE_MASK                                           0x03000000L
#define UNIPHYC_CHANNEL_XBAR_CNTL__DOUT_PHY_CHANNEL0_EN_MASK                                                  0x10000000L
#define UNIPHYC_CHANNEL_XBAR_CNTL__DOUT_PHY_CHANNEL1_EN_MASK                                                  0x20000000L
#define UNIPHYC_CHANNEL_XBAR_CNTL__DOUT_PHY_CHANNEL2_EN_MASK                                                  0x40000000L
#define UNIPHYC_CHANNEL_XBAR_CNTL__DOUT_PHY_CHANNEL3_EN_MASK                                                  0x80000000L
//UNIPHYD_LINK_CNTL
#define UNIPHYD_LINK_CNTL__UNIPHY_CHANNEL0_INVERT__SHIFT                                                      0xc
#define UNIPHYD_LINK_CNTL__UNIPHY_CHANNEL1_INVERT__SHIFT                                                      0xd
#define UNIPHYD_LINK_CNTL__UNIPHY_CHANNEL2_INVERT__SHIFT                                                      0xe
#define UNIPHYD_LINK_CNTL__UNIPHY_CHANNEL3_INVERT__SHIFT                                                      0xf
#define UNIPHYD_LINK_CNTL__UNIPHY_CHANNEL0_INVERT_MASK                                                        0x00001000L
#define UNIPHYD_LINK_CNTL__UNIPHY_CHANNEL1_INVERT_MASK                                                        0x00002000L
#define UNIPHYD_LINK_CNTL__UNIPHY_CHANNEL2_INVERT_MASK                                                        0x00004000L
#define UNIPHYD_LINK_CNTL__UNIPHY_CHANNEL3_INVERT_MASK                                                        0x00008000L
//UNIPHYD_CHANNEL_XBAR_CNTL
#define UNIPHYD_CHANNEL_XBAR_CNTL__UNIPHY_CHANNEL0_XBAR_SOURCE__SHIFT                                         0x0
#define UNIPHYD_CHANNEL_XBAR_CNTL__UNIPHY_CHANNEL1_XBAR_SOURCE__SHIFT                                         0x8
#define UNIPHYD_CHANNEL_XBAR_CNTL__UNIPHY_CHANNEL2_XBAR_SOURCE__SHIFT                                         0x10
#define UNIPHYD_CHANNEL_XBAR_CNTL__UNIPHY_CHANNEL3_XBAR_SOURCE__SHIFT                                         0x18
#define UNIPHYD_CHANNEL_XBAR_CNTL__DOUT_PHY_CHANNEL0_EN__SHIFT                                                0x1c
#define UNIPHYD_CHANNEL_XBAR_CNTL__DOUT_PHY_CHANNEL1_EN__SHIFT                                                0x1d
#define UNIPHYD_CHANNEL_XBAR_CNTL__DOUT_PHY_CHANNEL2_EN__SHIFT                                                0x1e
#define UNIPHYD_CHANNEL_XBAR_CNTL__DOUT_PHY_CHANNEL3_EN__SHIFT                                                0x1f
#define UNIPHYD_CHANNEL_XBAR_CNTL__UNIPHY_CHANNEL0_XBAR_SOURCE_MASK                                           0x00000003L
#define UNIPHYD_CHANNEL_XBAR_CNTL__UNIPHY_CHANNEL1_XBAR_SOURCE_MASK                                           0x00000300L
#define UNIPHYD_CHANNEL_XBAR_CNTL__UNIPHY_CHANNEL2_XBAR_SOURCE_MASK                                           0x00030000L
#define UNIPHYD_CHANNEL_XBAR_CNTL__UNIPHY_CHANNEL3_XBAR_SOURCE_MASK                                           0x03000000L
#define UNIPHYD_CHANNEL_XBAR_CNTL__DOUT_PHY_CHANNEL0_EN_MASK                                                  0x10000000L
#define UNIPHYD_CHANNEL_XBAR_CNTL__DOUT_PHY_CHANNEL1_EN_MASK                                                  0x20000000L
#define UNIPHYD_CHANNEL_XBAR_CNTL__DOUT_PHY_CHANNEL2_EN_MASK                                                  0x40000000L
#define UNIPHYD_CHANNEL_XBAR_CNTL__DOUT_PHY_CHANNEL3_EN_MASK                                                  0x80000000L
//UNIPHYE_LINK_CNTL
#define UNIPHYE_LINK_CNTL__UNIPHY_CHANNEL0_INVERT__SHIFT                                                      0xc
#define UNIPHYE_LINK_CNTL__UNIPHY_CHANNEL1_INVERT__SHIFT                                                      0xd
#define UNIPHYE_LINK_CNTL__UNIPHY_CHANNEL2_INVERT__SHIFT                                                      0xe
#define UNIPHYE_LINK_CNTL__UNIPHY_CHANNEL3_INVERT__SHIFT                                                      0xf
#define UNIPHYE_LINK_CNTL__UNIPHY_CHANNEL0_INVERT_MASK                                                        0x00001000L
#define UNIPHYE_LINK_CNTL__UNIPHY_CHANNEL1_INVERT_MASK                                                        0x00002000L
#define UNIPHYE_LINK_CNTL__UNIPHY_CHANNEL2_INVERT_MASK                                                        0x00004000L
#define UNIPHYE_LINK_CNTL__UNIPHY_CHANNEL3_INVERT_MASK                                                        0x00008000L
//UNIPHYE_CHANNEL_XBAR_CNTL
#define UNIPHYE_CHANNEL_XBAR_CNTL__UNIPHY_CHANNEL0_XBAR_SOURCE__SHIFT                                         0x0
#define UNIPHYE_CHANNEL_XBAR_CNTL__UNIPHY_CHANNEL1_XBAR_SOURCE__SHIFT                                         0x8
#define UNIPHYE_CHANNEL_XBAR_CNTL__UNIPHY_CHANNEL2_XBAR_SOURCE__SHIFT                                         0x10
#define UNIPHYE_CHANNEL_XBAR_CNTL__UNIPHY_CHANNEL3_XBAR_SOURCE__SHIFT                                         0x18
#define UNIPHYE_CHANNEL_XBAR_CNTL__DOUT_PHY_CHANNEL0_EN__SHIFT                                                0x1c
#define UNIPHYE_CHANNEL_XBAR_CNTL__DOUT_PHY_CHANNEL1_EN__SHIFT                                                0x1d
#define UNIPHYE_CHANNEL_XBAR_CNTL__DOUT_PHY_CHANNEL2_EN__SHIFT                                                0x1e
#define UNIPHYE_CHANNEL_XBAR_CNTL__DOUT_PHY_CHANNEL3_EN__SHIFT                                                0x1f
#define UNIPHYE_CHANNEL_XBAR_CNTL__UNIPHY_CHANNEL0_XBAR_SOURCE_MASK                                           0x00000003L
#define UNIPHYE_CHANNEL_XBAR_CNTL__UNIPHY_CHANNEL1_XBAR_SOURCE_MASK                                           0x00000300L
#define UNIPHYE_CHANNEL_XBAR_CNTL__UNIPHY_CHANNEL2_XBAR_SOURCE_MASK                                           0x00030000L
#define UNIPHYE_CHANNEL_XBAR_CNTL__UNIPHY_CHANNEL3_XBAR_SOURCE_MASK                                           0x03000000L
#define UNIPHYE_CHANNEL_XBAR_CNTL__DOUT_PHY_CHANNEL0_EN_MASK                                                  0x10000000L
#define UNIPHYE_CHANNEL_XBAR_CNTL__DOUT_PHY_CHANNEL1_EN_MASK                                                  0x20000000L
#define UNIPHYE_CHANNEL_XBAR_CNTL__DOUT_PHY_CHANNEL2_EN_MASK                                                  0x40000000L
#define UNIPHYE_CHANNEL_XBAR_CNTL__DOUT_PHY_CHANNEL3_EN_MASK                                                  0x80000000L
//DCIO_WRCMD_DELAY
#define DCIO_WRCMD_DELAY__UNIPHY_DELAY__SHIFT                                                                 0x18
#define DCIO_WRCMD_DELAY__UNIPHY_DELAY_MASK                                                                   0xFF000000L
//DC_PINSTRAPS
#define DC_PINSTRAPS__DC_PINSTRAPS_SMS_EN_HARD__SHIFT                                                         0xd
#define DC_PINSTRAPS__DC_PINSTRAPS_AUDIO__SHIFT                                                               0xe
#define DC_PINSTRAPS__DC_PINSTRAPS_CCBYPASS__SHIFT                                                            0x10
#define DC_PINSTRAPS__DC_PINSTRAPS_SMS_EN_HARD_MASK                                                           0x00002000L
#define DC_PINSTRAPS__DC_PINSTRAPS_AUDIO_MASK                                                                 0x0000C000L
#define DC_PINSTRAPS__DC_PINSTRAPS_CCBYPASS_MASK                                                              0x00010000L
//DCIO_SPARE
#define DCIO_SPARE__DCIO_SPARE__SHIFT                                                                         0x0
#define DCIO_SPARE__DCIO_SPARE_MASK                                                                           0xFFFFFFFFL
//INTERCEPT_STATE
#define INTERCEPT_STATE__PWRSEQ0_INTERCEPTB_STATE__SHIFT                                                      0x0
#define INTERCEPT_STATE__PWRSEQ1_INTERCEPTB_STATE__SHIFT                                                      0x1
#define INTERCEPT_STATE__DLPC_INTERCEPTB_STATE__SHIFT                                                         0x2
#define INTERCEPT_STATE__DPCS0_INTERCEPTB_STATE__SHIFT                                                        0x4
#define INTERCEPT_STATE__DPCS1_INTERCEPTB_STATE__SHIFT                                                        0x5
#define INTERCEPT_STATE__DPCS2_INTERCEPTB_STATE__SHIFT                                                        0x6
#define INTERCEPT_STATE__DPCS3_INTERCEPTB_STATE__SHIFT                                                        0x7
#define INTERCEPT_STATE__DPCS4_INTERCEPTB_STATE__SHIFT                                                        0x8
#define INTERCEPT_STATE__DPCS5_INTERCEPTB_STATE__SHIFT                                                        0x9
#define INTERCEPT_STATE__DPCS6_INTERCEPTB_STATE__SHIFT                                                        0xa
#define INTERCEPT_STATE__DPCS0_DC_INTERCEPTB_STATE__SHIFT                                                     0x10
#define INTERCEPT_STATE__DPCS1_DC_INTERCEPTB_STATE__SHIFT                                                     0x11
#define INTERCEPT_STATE__DPCS2_DC_INTERCEPTB_STATE__SHIFT                                                     0x12
#define INTERCEPT_STATE__DPCS3_DC_INTERCEPTB_STATE__SHIFT                                                     0x13
#define INTERCEPT_STATE__DPCS4_DC_INTERCEPTB_STATE__SHIFT                                                     0x14
#define INTERCEPT_STATE__DPCS5_DC_INTERCEPTB_STATE__SHIFT                                                     0x15
#define INTERCEPT_STATE__DPCS6_DC_INTERCEPTB_STATE__SHIFT                                                     0x16
#define INTERCEPT_STATE__PWRSEQ0_INTERCEPTB_STATE_MASK                                                        0x00000001L
#define INTERCEPT_STATE__PWRSEQ1_INTERCEPTB_STATE_MASK                                                        0x00000002L
#define INTERCEPT_STATE__DLPC_INTERCEPTB_STATE_MASK                                                           0x00000004L
#define INTERCEPT_STATE__DPCS0_INTERCEPTB_STATE_MASK                                                          0x00000010L
#define INTERCEPT_STATE__DPCS1_INTERCEPTB_STATE_MASK                                                          0x00000020L
#define INTERCEPT_STATE__DPCS2_INTERCEPTB_STATE_MASK                                                          0x00000040L
#define INTERCEPT_STATE__DPCS3_INTERCEPTB_STATE_MASK                                                          0x00000080L
#define INTERCEPT_STATE__DPCS4_INTERCEPTB_STATE_MASK                                                          0x00000100L
#define INTERCEPT_STATE__DPCS5_INTERCEPTB_STATE_MASK                                                          0x00000200L
#define INTERCEPT_STATE__DPCS6_INTERCEPTB_STATE_MASK                                                          0x00000400L
#define INTERCEPT_STATE__DPCS0_DC_INTERCEPTB_STATE_MASK                                                       0x00010000L
#define INTERCEPT_STATE__DPCS1_DC_INTERCEPTB_STATE_MASK                                                       0x00020000L
#define INTERCEPT_STATE__DPCS2_DC_INTERCEPTB_STATE_MASK                                                       0x00040000L
#define INTERCEPT_STATE__DPCS3_DC_INTERCEPTB_STATE_MASK                                                       0x00080000L
#define INTERCEPT_STATE__DPCS4_DC_INTERCEPTB_STATE_MASK                                                       0x00100000L
#define INTERCEPT_STATE__DPCS5_DC_INTERCEPTB_STATE_MASK                                                       0x00200000L
#define INTERCEPT_STATE__DPCS6_DC_INTERCEPTB_STATE_MASK                                                       0x00400000L
//DCIO_PATTERN_GEN_PAT
#define DCIO_PATTERN_GEN_PAT__DCIO_PATTERN_GEN_PAT__SHIFT                                                     0x0
#define DCIO_PATTERN_GEN_PAT__DCIO_PATTERN_GEN_PAT_MASK                                                       0xFFFFFFFFL
//DCIO_PATTERN_GEN_EN
#define DCIO_PATTERN_GEN_EN__DCIO_PATTERN_GEN_EN__SHIFT                                                       0x0
#define DCIO_PATTERN_GEN_EN__DCIO_PATTERN_GEN_EN_MASK                                                         0x00000001L
//DCIO_BL_PWM_FRAME_START_DISP_SEL
#define DCIO_BL_PWM_FRAME_START_DISP_SEL__BL_PWM0_GRP1_FRAME_START_DISP_SEL__SHIFT                            0x0
#define DCIO_BL_PWM_FRAME_START_DISP_SEL__BL_PWM1_GRP1_FRAME_START_DISP_SEL__SHIFT                            0x4
#define DCIO_BL_PWM_FRAME_START_DISP_SEL__BL_PWM0_GRP1_FRAME_START_DISP_SEL_MASK                              0x00000007L
#define DCIO_BL_PWM_FRAME_START_DISP_SEL__BL_PWM1_GRP1_FRAME_START_DISP_SEL_MASK                              0x00000070L
//DCIO_GSL_GENLK_PAD_CNTL
#define DCIO_GSL_GENLK_PAD_CNTL__DCIO_GENLK_CLK_GSL_FLIP_READY_SEL__SHIFT                                     0x4
#define DCIO_GSL_GENLK_PAD_CNTL__DCIO_GENLK_CLK_GSL_MASK__SHIFT                                               0x8
#define DCIO_GSL_GENLK_PAD_CNTL__DCIO_GENLK_VSYNC_GSL_FLIP_READY_SEL__SHIFT                                   0x14
#define DCIO_GSL_GENLK_PAD_CNTL__DCIO_GENLK_VSYNC_GSL_MASK__SHIFT                                             0x18
#define DCIO_GSL_GENLK_PAD_CNTL__DCIO_GENLK_CLK_GSL_FLIP_READY_SEL_MASK                                       0x00000030L
#define DCIO_GSL_GENLK_PAD_CNTL__DCIO_GENLK_CLK_GSL_MASK_MASK                                                 0x00000300L
#define DCIO_GSL_GENLK_PAD_CNTL__DCIO_GENLK_VSYNC_GSL_FLIP_READY_SEL_MASK                                     0x00300000L
#define DCIO_GSL_GENLK_PAD_CNTL__DCIO_GENLK_VSYNC_GSL_MASK_MASK                                               0x03000000L
//DCIO_GSL_SWAPLOCK_PAD_CNTL
#define DCIO_GSL_SWAPLOCK_PAD_CNTL__DCIO_SWAPLOCK_A_GSL_FLIP_READY_SEL__SHIFT                                 0x4
#define DCIO_GSL_SWAPLOCK_PAD_CNTL__DCIO_SWAPLOCK_A_GSL_MASK__SHIFT                                           0x8
#define DCIO_GSL_SWAPLOCK_PAD_CNTL__DCIO_SWAPLOCK_B_GSL_FLIP_READY_SEL__SHIFT                                 0x14
#define DCIO_GSL_SWAPLOCK_PAD_CNTL__DCIO_SWAPLOCK_B_GSL_MASK__SHIFT                                           0x18
#define DCIO_GSL_SWAPLOCK_PAD_CNTL__DCIO_SWAPLOCK_A_GSL_FLIP_READY_SEL_MASK                                   0x00000030L
#define DCIO_GSL_SWAPLOCK_PAD_CNTL__DCIO_SWAPLOCK_A_GSL_MASK_MASK                                             0x00000300L
#define DCIO_GSL_SWAPLOCK_PAD_CNTL__DCIO_SWAPLOCK_B_GSL_FLIP_READY_SEL_MASK                                   0x00300000L
#define DCIO_GSL_SWAPLOCK_PAD_CNTL__DCIO_SWAPLOCK_B_GSL_MASK_MASK                                             0x03000000L
//DPCS_DCIO_TEST_CLK_SRC
#define DPCS_DCIO_TEST_CLK_SRC__DPCS_TEST_CLK_SRC_SEL__SHIFT                                                  0x0
#define DPCS_DCIO_TEST_CLK_SRC__DPCS_TEST_CLK_SRC_SEL_MASK                                                    0x00000007L
//DCIO_DEBUG
#define DCIO_DEBUG__DCIO_DEBUG__SHIFT                                                                         0x0
#define DCIO_DEBUG__DCIO_DEBUG_MASK                                                                           0xFFFFFFFFL
//DCIO_TEST_DEBUG_INDEX
#define DCIO_TEST_DEBUG_INDEX__DCIO_TEST_DEBUG_INDEX__SHIFT                                                   0x0
#define DCIO_TEST_DEBUG_INDEX__DCIO_TEST_DEBUG_WRITE_EN__SHIFT                                                0x8
#define DCIO_TEST_DEBUG_INDEX__DCIO_TEST_DEBUG_INDEX_MASK                                                     0x000000FFL
#define DCIO_TEST_DEBUG_INDEX__DCIO_TEST_DEBUG_WRITE_EN_MASK                                                  0x00000100L
//DCIO_TEST_DEBUG_DATA
#define DCIO_TEST_DEBUG_DATA__DCIO_TEST_DEBUG_DATA__SHIFT                                                     0x0
#define DCIO_TEST_DEBUG_DATA__DCIO_TEST_DEBUG_DATA_MASK                                                       0xFFFFFFFFL
//DBG_OUT_CNTL
#define DBG_OUT_CNTL__DBG_OUT_BLK_SEL__SHIFT                                                                  0x0
#define DBG_OUT_CNTL__DBG_OUT_4BIT_SEL__SHIFT                                                                 0x5
#define DBG_OUT_CNTL__DBG_OUT_12BIT_TEST_DATA__SHIFT                                                          0xc
#define DBG_OUT_CNTL__DBG_OUT_BLK_SEL_MASK                                                                    0x00000003L
#define DBG_OUT_CNTL__DBG_OUT_4BIT_SEL_MASK                                                                   0x000000E0L
#define DBG_OUT_CNTL__DBG_OUT_12BIT_TEST_DATA_MASK                                                            0x00FFF000L
//DCIO_DEBUG_CONFIG
#define DCIO_DEBUG_CONFIG__DCIO_DBG_EN__SHIFT                                                                 0x0
#define DCIO_DEBUG_CONFIG__DCIO_DBG_EN_MASK                                                                   0x00000001L
//DCIO_SOFT_RESET
#define DCIO_SOFT_RESET__UNIPHYA_SOFT_RESET__SHIFT                                                            0x0
#define DCIO_SOFT_RESET__UNIPHYB_SOFT_RESET__SHIFT                                                            0x1
#define DCIO_SOFT_RESET__UNIPHYC_SOFT_RESET__SHIFT                                                            0x2
#define DCIO_SOFT_RESET__UNIPHYD_SOFT_RESET__SHIFT                                                            0x3
#define DCIO_SOFT_RESET__UNIPHYE_SOFT_RESET__SHIFT                                                            0x4
#define DCIO_SOFT_RESET__UNIPHYF_SOFT_RESET__SHIFT                                                            0x5
#define DCIO_SOFT_RESET__UNIPHYG_SOFT_RESET__SHIFT                                                            0x6
#define DCIO_SOFT_RESET__PWRSEQ0_SOFT_RESET__SHIFT                                                            0x10
#define DCIO_SOFT_RESET__PWRSEQ1_SOFT_RESET__SHIFT                                                            0x11
#define DCIO_SOFT_RESET__DLPC_SOFT_RESET__SHIFT                                                               0x14
#define DCIO_SOFT_RESET__UNIPHYA_SOFT_RESET_MASK                                                              0x00000001L
#define DCIO_SOFT_RESET__UNIPHYB_SOFT_RESET_MASK                                                              0x00000002L
#define DCIO_SOFT_RESET__UNIPHYC_SOFT_RESET_MASK                                                              0x00000004L
#define DCIO_SOFT_RESET__UNIPHYD_SOFT_RESET_MASK                                                              0x00000008L
#define DCIO_SOFT_RESET__UNIPHYE_SOFT_RESET_MASK                                                              0x00000010L
#define DCIO_SOFT_RESET__UNIPHYF_SOFT_RESET_MASK                                                              0x00000020L
#define DCIO_SOFT_RESET__UNIPHYG_SOFT_RESET_MASK                                                              0x00000040L
#define DCIO_SOFT_RESET__PWRSEQ0_SOFT_RESET_MASK                                                              0x00010000L
#define DCIO_SOFT_RESET__PWRSEQ1_SOFT_RESET_MASK                                                              0x00020000L
#define DCIO_SOFT_RESET__DLPC_SOFT_RESET_MASK                                                                 0x00100000L


// addressBlock: dpcssys_dcio_dcio_chip_dispdec
//DC_GPIO_DDC1_MASK
#define DC_GPIO_DDC1_MASK__DC_GPIO_DDC1CLK_MASK__SHIFT                                                        0x0
#define DC_GPIO_DDC1_MASK__DC_GPIO_DDC1CLK_PD_EN__SHIFT                                                       0x4
#define DC_GPIO_DDC1_MASK__DC_GPIO_DDC1CLK_RECV__SHIFT                                                        0x6
#define DC_GPIO_DDC1_MASK__DC_GPIO_DDC1DATA_MASK__SHIFT                                                       0x8
#define DC_GPIO_DDC1_MASK__DC_GPIO_DDC1DATA_PD_EN__SHIFT                                                      0xc
#define DC_GPIO_DDC1_MASK__DC_GPIO_DDC1DATA_RECV__SHIFT                                                       0xe
#define DC_GPIO_DDC1_MASK__AUX_PAD1_MODE__SHIFT                                                               0x10
#define DC_GPIO_DDC1_MASK__AUX1_POL__SHIFT                                                                    0x14
#define DC_GPIO_DDC1_MASK__ALLOW_HW_DDC1_PD_EN__SHIFT                                                         0x16
#define DC_GPIO_DDC1_MASK__DC_GPIO_AUX1_PD_HP_EN__SHIFT                                                       0x18
#define DC_GPIO_DDC1_MASK__DC_GPIO_DDC1CLK_MASK_MASK                                                          0x00000001L
#define DC_GPIO_DDC1_MASK__DC_GPIO_DDC1CLK_PD_EN_MASK                                                         0x00000010L
#define DC_GPIO_DDC1_MASK__DC_GPIO_DDC1CLK_RECV_MASK                                                          0x00000040L
#define DC_GPIO_DDC1_MASK__DC_GPIO_DDC1DATA_MASK_MASK                                                         0x00000100L
#define DC_GPIO_DDC1_MASK__DC_GPIO_DDC1DATA_PD_EN_MASK                                                        0x00001000L
#define DC_GPIO_DDC1_MASK__DC_GPIO_DDC1DATA_RECV_MASK                                                         0x00004000L
#define DC_GPIO_DDC1_MASK__AUX_PAD1_MODE_MASK                                                                 0x00010000L
#define DC_GPIO_DDC1_MASK__AUX1_POL_MASK                                                                      0x00100000L
#define DC_GPIO_DDC1_MASK__ALLOW_HW_DDC1_PD_EN_MASK                                                           0x00400000L
#define DC_GPIO_DDC1_MASK__DC_GPIO_AUX1_PD_HP_EN_MASK                                                         0x01000000L
//DC_GPIO_DDC1_A
#define DC_GPIO_DDC1_A__DC_GPIO_DDC1CLK_A__SHIFT                                                              0x0
#define DC_GPIO_DDC1_A__DC_GPIO_DDC1DATA_A__SHIFT                                                             0x8
#define DC_GPIO_DDC1_A__DC_GPIO_DDC1CLK_A_MASK                                                                0x00000001L
#define DC_GPIO_DDC1_A__DC_GPIO_DDC1DATA_A_MASK                                                               0x00000100L
//DC_GPIO_DDC1_EN
#define DC_GPIO_DDC1_EN__DC_GPIO_DDC1CLK_EN__SHIFT                                                            0x0
#define DC_GPIO_DDC1_EN__DC_GPIO_DDC1DATA_EN__SHIFT                                                           0x8
#define DC_GPIO_DDC1_EN__DC_GPIO_DDC1CLK_EN_MASK                                                              0x00000001L
#define DC_GPIO_DDC1_EN__DC_GPIO_DDC1DATA_EN_MASK                                                             0x00000100L
//DC_GPIO_DDC1_Y
#define DC_GPIO_DDC1_Y__DC_GPIO_DDC1CLK_Y__SHIFT                                                              0x0
#define DC_GPIO_DDC1_Y__DC_GPIO_DDC1DATA_Y__SHIFT                                                             0x8
#define DC_GPIO_DDC1_Y__DC_GPIO_DDC1CLK_Y_MASK                                                                0x00000001L
#define DC_GPIO_DDC1_Y__DC_GPIO_DDC1DATA_Y_MASK                                                               0x00000100L
//DC_GPIO_DDC2_MASK
#define DC_GPIO_DDC2_MASK__DC_GPIO_DDC2CLK_MASK__SHIFT                                                        0x0
#define DC_GPIO_DDC2_MASK__DC_GPIO_DDC2CLK_PD_EN__SHIFT                                                       0x4
#define DC_GPIO_DDC2_MASK__DC_GPIO_DDC2CLK_RECV__SHIFT                                                        0x6
#define DC_GPIO_DDC2_MASK__DC_GPIO_DDC2DATA_MASK__SHIFT                                                       0x8
#define DC_GPIO_DDC2_MASK__DC_GPIO_DDC2DATA_PD_EN__SHIFT                                                      0xc
#define DC_GPIO_DDC2_MASK__DC_GPIO_DDC2DATA_RECV__SHIFT                                                       0xe
#define DC_GPIO_DDC2_MASK__AUX_PAD2_MODE__SHIFT                                                               0x10
#define DC_GPIO_DDC2_MASK__AUX2_POL__SHIFT                                                                    0x14
#define DC_GPIO_DDC2_MASK__ALLOW_HW_DDC2_PD_EN__SHIFT                                                         0x16
#define DC_GPIO_DDC2_MASK__DC_GPIO_AUX2_PD_HP_EN__SHIFT                                                       0x18
#define DC_GPIO_DDC2_MASK__DC_GPIO_DDC2CLK_MASK_MASK                                                          0x00000001L
#define DC_GPIO_DDC2_MASK__DC_GPIO_DDC2CLK_PD_EN_MASK                                                         0x00000010L
#define DC_GPIO_DDC2_MASK__DC_GPIO_DDC2CLK_RECV_MASK                                                          0x00000040L
#define DC_GPIO_DDC2_MASK__DC_GPIO_DDC2DATA_MASK_MASK                                                         0x00000100L
#define DC_GPIO_DDC2_MASK__DC_GPIO_DDC2DATA_PD_EN_MASK                                                        0x00001000L
#define DC_GPIO_DDC2_MASK__DC_GPIO_DDC2DATA_RECV_MASK                                                         0x00004000L
#define DC_GPIO_DDC2_MASK__AUX_PAD2_MODE_MASK                                                                 0x00010000L
#define DC_GPIO_DDC2_MASK__AUX2_POL_MASK                                                                      0x00100000L
#define DC_GPIO_DDC2_MASK__ALLOW_HW_DDC2_PD_EN_MASK                                                           0x00400000L
#define DC_GPIO_DDC2_MASK__DC_GPIO_AUX2_PD_HP_EN_MASK                                                         0x01000000L
//DC_GPIO_DDC2_A
#define DC_GPIO_DDC2_A__DC_GPIO_DDC2CLK_A__SHIFT                                                              0x0
#define DC_GPIO_DDC2_A__DC_GPIO_DDC2DATA_A__SHIFT                                                             0x8
#define DC_GPIO_DDC2_A__DC_GPIO_DDC2CLK_A_MASK                                                                0x00000001L
#define DC_GPIO_DDC2_A__DC_GPIO_DDC2DATA_A_MASK                                                               0x00000100L
//DC_GPIO_DDC2_EN
#define DC_GPIO_DDC2_EN__DC_GPIO_DDC2CLK_EN__SHIFT                                                            0x0
#define DC_GPIO_DDC2_EN__DC_GPIO_DDC2DATA_EN__SHIFT                                                           0x8
#define DC_GPIO_DDC2_EN__DC_GPIO_DDC2CLK_EN_MASK                                                              0x00000001L
#define DC_GPIO_DDC2_EN__DC_GPIO_DDC2DATA_EN_MASK                                                             0x00000100L
//DC_GPIO_DDC2_Y
#define DC_GPIO_DDC2_Y__DC_GPIO_DDC2CLK_Y__SHIFT                                                              0x0
#define DC_GPIO_DDC2_Y__DC_GPIO_DDC2DATA_Y__SHIFT                                                             0x8
#define DC_GPIO_DDC2_Y__DC_GPIO_DDC2CLK_Y_MASK                                                                0x00000001L
#define DC_GPIO_DDC2_Y__DC_GPIO_DDC2DATA_Y_MASK                                                               0x00000100L
//DC_GPIO_DDC3_MASK
#define DC_GPIO_DDC3_MASK__DC_GPIO_DDC3CLK_MASK__SHIFT                                                        0x0
#define DC_GPIO_DDC3_MASK__DC_GPIO_DDC3CLK_PD_EN__SHIFT                                                       0x4
#define DC_GPIO_DDC3_MASK__DC_GPIO_DDC3CLK_RECV__SHIFT                                                        0x6
#define DC_GPIO_DDC3_MASK__DC_GPIO_DDC3DATA_MASK__SHIFT                                                       0x8
#define DC_GPIO_DDC3_MASK__DC_GPIO_DDC3DATA_PD_EN__SHIFT                                                      0xc
#define DC_GPIO_DDC3_MASK__DC_GPIO_DDC3DATA_RECV__SHIFT                                                       0xe
#define DC_GPIO_DDC3_MASK__AUX_PAD3_MODE__SHIFT                                                               0x10
#define DC_GPIO_DDC3_MASK__AUX3_POL__SHIFT                                                                    0x14
#define DC_GPIO_DDC3_MASK__ALLOW_HW_DDC3_PD_EN__SHIFT                                                         0x16
#define DC_GPIO_DDC3_MASK__DC_GPIO_AUX3_PD_HP_EN__SHIFT                                                       0x18
#define DC_GPIO_DDC3_MASK__DC_GPIO_DDC3CLK_MASK_MASK                                                          0x00000001L
#define DC_GPIO_DDC3_MASK__DC_GPIO_DDC3CLK_PD_EN_MASK                                                         0x00000010L
#define DC_GPIO_DDC3_MASK__DC_GPIO_DDC3CLK_RECV_MASK                                                          0x00000040L
#define DC_GPIO_DDC3_MASK__DC_GPIO_DDC3DATA_MASK_MASK                                                         0x00000100L
#define DC_GPIO_DDC3_MASK__DC_GPIO_DDC3DATA_PD_EN_MASK                                                        0x00001000L
#define DC_GPIO_DDC3_MASK__DC_GPIO_DDC3DATA_RECV_MASK                                                         0x00004000L
#define DC_GPIO_DDC3_MASK__AUX_PAD3_MODE_MASK                                                                 0x00010000L
#define DC_GPIO_DDC3_MASK__AUX3_POL_MASK                                                                      0x00100000L
#define DC_GPIO_DDC3_MASK__ALLOW_HW_DDC3_PD_EN_MASK                                                           0x00400000L
#define DC_GPIO_DDC3_MASK__DC_GPIO_AUX3_PD_HP_EN_MASK                                                         0x01000000L
//DC_GPIO_DDC3_A
#define DC_GPIO_DDC3_A__DC_GPIO_DDC3CLK_A__SHIFT                                                              0x0
#define DC_GPIO_DDC3_A__DC_GPIO_DDC3DATA_A__SHIFT                                                             0x8
#define DC_GPIO_DDC3_A__DC_GPIO_DDC3CLK_A_MASK                                                                0x00000001L
#define DC_GPIO_DDC3_A__DC_GPIO_DDC3DATA_A_MASK                                                               0x00000100L
//DC_GPIO_DDC3_EN
#define DC_GPIO_DDC3_EN__DC_GPIO_DDC3CLK_EN__SHIFT                                                            0x0
#define DC_GPIO_DDC3_EN__DC_GPIO_DDC3DATA_EN__SHIFT                                                           0x8
#define DC_GPIO_DDC3_EN__DC_GPIO_DDC3CLK_EN_MASK                                                              0x00000001L
#define DC_GPIO_DDC3_EN__DC_GPIO_DDC3DATA_EN_MASK                                                             0x00000100L
//DC_GPIO_DDC3_Y
#define DC_GPIO_DDC3_Y__DC_GPIO_DDC3CLK_Y__SHIFT                                                              0x0
#define DC_GPIO_DDC3_Y__DC_GPIO_DDC3DATA_Y__SHIFT                                                             0x8
#define DC_GPIO_DDC3_Y__DC_GPIO_DDC3CLK_Y_MASK                                                                0x00000001L
#define DC_GPIO_DDC3_Y__DC_GPIO_DDC3DATA_Y_MASK                                                               0x00000100L
//DC_GPIO_DDC4_MASK
#define DC_GPIO_DDC4_MASK__DC_GPIO_DDC4CLK_MASK__SHIFT                                                        0x0
#define DC_GPIO_DDC4_MASK__DC_GPIO_DDC4CLK_PD_EN__SHIFT                                                       0x4
#define DC_GPIO_DDC4_MASK__DC_GPIO_DDC4CLK_RECV__SHIFT                                                        0x6
#define DC_GPIO_DDC4_MASK__DC_GPIO_DDC4DATA_MASK__SHIFT                                                       0x8
#define DC_GPIO_DDC4_MASK__DC_GPIO_DDC4DATA_PD_EN__SHIFT                                                      0xc
#define DC_GPIO_DDC4_MASK__DC_GPIO_DDC4DATA_RECV__SHIFT                                                       0xe
#define DC_GPIO_DDC4_MASK__AUX_PAD4_MODE__SHIFT                                                               0x10
#define DC_GPIO_DDC4_MASK__AUX4_POL__SHIFT                                                                    0x14
#define DC_GPIO_DDC4_MASK__ALLOW_HW_DDC4_PD_EN__SHIFT                                                         0x16
#define DC_GPIO_DDC4_MASK__DC_GPIO_AUX4_PD_HP_EN__SHIFT                                                       0x18
#define DC_GPIO_DDC4_MASK__DC_GPIO_DDC4CLK_MASK_MASK                                                          0x00000001L
#define DC_GPIO_DDC4_MASK__DC_GPIO_DDC4CLK_PD_EN_MASK                                                         0x00000010L
#define DC_GPIO_DDC4_MASK__DC_GPIO_DDC4CLK_RECV_MASK                                                          0x00000040L
#define DC_GPIO_DDC4_MASK__DC_GPIO_DDC4DATA_MASK_MASK                                                         0x00000100L
#define DC_GPIO_DDC4_MASK__DC_GPIO_DDC4DATA_PD_EN_MASK                                                        0x00001000L
#define DC_GPIO_DDC4_MASK__DC_GPIO_DDC4DATA_RECV_MASK                                                         0x00004000L
#define DC_GPIO_DDC4_MASK__AUX_PAD4_MODE_MASK                                                                 0x00010000L
#define DC_GPIO_DDC4_MASK__AUX4_POL_MASK                                                                      0x00100000L
#define DC_GPIO_DDC4_MASK__ALLOW_HW_DDC4_PD_EN_MASK                                                           0x00400000L
#define DC_GPIO_DDC4_MASK__DC_GPIO_AUX4_PD_HP_EN_MASK                                                         0x01000000L
//DC_GPIO_DDC4_A
#define DC_GPIO_DDC4_A__DC_GPIO_DDC4CLK_A__SHIFT                                                              0x0
#define DC_GPIO_DDC4_A__DC_GPIO_DDC4DATA_A__SHIFT                                                             0x8
#define DC_GPIO_DDC4_A__DC_GPIO_DDC4CLK_A_MASK                                                                0x00000001L
#define DC_GPIO_DDC4_A__DC_GPIO_DDC4DATA_A_MASK                                                               0x00000100L
//DC_GPIO_DDC4_EN
#define DC_GPIO_DDC4_EN__DC_GPIO_DDC4CLK_EN__SHIFT                                                            0x0
#define DC_GPIO_DDC4_EN__DC_GPIO_DDC4DATA_EN__SHIFT                                                           0x8
#define DC_GPIO_DDC4_EN__DC_GPIO_DDC4CLK_EN_MASK                                                              0x00000001L
#define DC_GPIO_DDC4_EN__DC_GPIO_DDC4DATA_EN_MASK                                                             0x00000100L
//DC_GPIO_DDC4_Y
#define DC_GPIO_DDC4_Y__DC_GPIO_DDC4CLK_Y__SHIFT                                                              0x0
#define DC_GPIO_DDC4_Y__DC_GPIO_DDC4DATA_Y__SHIFT                                                             0x8
#define DC_GPIO_DDC4_Y__DC_GPIO_DDC4CLK_Y_MASK                                                                0x00000001L
#define DC_GPIO_DDC4_Y__DC_GPIO_DDC4DATA_Y_MASK                                                               0x00000100L
//DC_GPIO_DDC5_MASK
#define DC_GPIO_DDC5_MASK__DC_GPIO_DDC5CLK_MASK__SHIFT                                                        0x0
#define DC_GPIO_DDC5_MASK__DC_GPIO_DDC5CLK_PD_EN__SHIFT                                                       0x4
#define DC_GPIO_DDC5_MASK__DC_GPIO_DDC5CLK_RECV__SHIFT                                                        0x6
#define DC_GPIO_DDC5_MASK__DC_GPIO_DDC5DATA_MASK__SHIFT                                                       0x8
#define DC_GPIO_DDC5_MASK__DC_GPIO_DDC5DATA_PD_EN__SHIFT                                                      0xc
#define DC_GPIO_DDC5_MASK__DC_GPIO_DDC5DATA_RECV__SHIFT                                                       0xe
#define DC_GPIO_DDC5_MASK__AUX_PAD5_MODE__SHIFT                                                               0x10
#define DC_GPIO_DDC5_MASK__AUX5_POL__SHIFT                                                                    0x14
#define DC_GPIO_DDC5_MASK__ALLOW_HW_DDC5_PD_EN__SHIFT                                                         0x16
#define DC_GPIO_DDC5_MASK__DC_GPIO_AUX5_PD_HP_EN__SHIFT                                                       0x18
#define DC_GPIO_DDC5_MASK__DC_GPIO_DDC5CLK_MASK_MASK                                                          0x00000001L
#define DC_GPIO_DDC5_MASK__DC_GPIO_DDC5CLK_PD_EN_MASK                                                         0x00000010L
#define DC_GPIO_DDC5_MASK__DC_GPIO_DDC5CLK_RECV_MASK                                                          0x00000040L
#define DC_GPIO_DDC5_MASK__DC_GPIO_DDC5DATA_MASK_MASK                                                         0x00000100L
#define DC_GPIO_DDC5_MASK__DC_GPIO_DDC5DATA_PD_EN_MASK                                                        0x00001000L
#define DC_GPIO_DDC5_MASK__DC_GPIO_DDC5DATA_RECV_MASK                                                         0x00004000L
#define DC_GPIO_DDC5_MASK__AUX_PAD5_MODE_MASK                                                                 0x00010000L
#define DC_GPIO_DDC5_MASK__AUX5_POL_MASK                                                                      0x00100000L
#define DC_GPIO_DDC5_MASK__ALLOW_HW_DDC5_PD_EN_MASK                                                           0x00400000L
#define DC_GPIO_DDC5_MASK__DC_GPIO_AUX5_PD_HP_EN_MASK                                                         0x01000000L
//DC_GPIO_DDC5_A
#define DC_GPIO_DDC5_A__DC_GPIO_DDC5CLK_A__SHIFT                                                              0x0
#define DC_GPIO_DDC5_A__DC_GPIO_DDC5DATA_A__SHIFT                                                             0x8
#define DC_GPIO_DDC5_A__DC_GPIO_DDC5CLK_A_MASK                                                                0x00000001L
#define DC_GPIO_DDC5_A__DC_GPIO_DDC5DATA_A_MASK                                                               0x00000100L
//DC_GPIO_DDC5_EN
#define DC_GPIO_DDC5_EN__DC_GPIO_DDC5CLK_EN__SHIFT                                                            0x0
#define DC_GPIO_DDC5_EN__DC_GPIO_DDC5DATA_EN__SHIFT                                                           0x8
#define DC_GPIO_DDC5_EN__DC_GPIO_DDC5CLK_EN_MASK                                                              0x00000001L
#define DC_GPIO_DDC5_EN__DC_GPIO_DDC5DATA_EN_MASK                                                             0x00000100L
//DC_GPIO_DDC5_Y
#define DC_GPIO_DDC5_Y__DC_GPIO_DDC5CLK_Y__SHIFT                                                              0x0
#define DC_GPIO_DDC5_Y__DC_GPIO_DDC5DATA_Y__SHIFT                                                             0x8
#define DC_GPIO_DDC5_Y__DC_GPIO_DDC5CLK_Y_MASK                                                                0x00000001L
#define DC_GPIO_DDC5_Y__DC_GPIO_DDC5DATA_Y_MASK                                                               0x00000100L
//DC_GPIO_DDCVGA_MASK
#define DC_GPIO_DDCVGA_MASK__DC_GPIO_DDCVGACLK_MASK__SHIFT                                                    0x0
#define DC_GPIO_DDCVGA_MASK__DDCVGA_INVERT_INPUT_POLARITY__SHIFT                                              0x4
#define DC_GPIO_DDCVGA_MASK__DC_GPIO_DDCVGACLK_RECV__SHIFT                                                    0x6
#define DC_GPIO_DDCVGA_MASK__DC_GPIO_DDCVGADATA_MASK__SHIFT                                                   0x8
#define DC_GPIO_DDCVGA_MASK__DC_GPIO_DDCVGADATA_PD_EN__SHIFT                                                  0xc
#define DC_GPIO_DDCVGA_MASK__DC_GPIO_DDCVGADATA_RECV__SHIFT                                                   0xe
#define DC_GPIO_DDCVGA_MASK__ALLOW_HW_DDCVGA_PD_EN__SHIFT                                                     0x16
#define DC_GPIO_DDCVGA_MASK__DC_GPIO_DDCVGADATA_STR__SHIFT                                                    0x1c
#define DC_GPIO_DDCVGA_MASK__DC_GPIO_DDCVGACLK_MASK_MASK                                                      0x00000001L
#define DC_GPIO_DDCVGA_MASK__DDCVGA_INVERT_INPUT_POLARITY_MASK                                                0x00000010L
#define DC_GPIO_DDCVGA_MASK__DC_GPIO_DDCVGACLK_RECV_MASK                                                      0x00000040L
#define DC_GPIO_DDCVGA_MASK__DC_GPIO_DDCVGADATA_MASK_MASK                                                     0x00000100L
#define DC_GPIO_DDCVGA_MASK__DC_GPIO_DDCVGADATA_PD_EN_MASK                                                    0x00001000L
#define DC_GPIO_DDCVGA_MASK__DC_GPIO_DDCVGADATA_RECV_MASK                                                     0x00004000L
#define DC_GPIO_DDCVGA_MASK__ALLOW_HW_DDCVGA_PD_EN_MASK                                                       0x00400000L
#define DC_GPIO_DDCVGA_MASK__DC_GPIO_DDCVGADATA_STR_MASK                                                      0xF0000000L
//DC_GPIO_DDCVGA_A
#define DC_GPIO_DDCVGA_A__DC_GPIO_DDCVGACLK_A__SHIFT                                                          0x0
#define DC_GPIO_DDCVGA_A__DC_GPIO_DDCVGADATA_A__SHIFT                                                         0x8
#define DC_GPIO_DDCVGA_A__DC_GPIO_DDCVGACLK_A_MASK                                                            0x00000001L
#define DC_GPIO_DDCVGA_A__DC_GPIO_DDCVGADATA_A_MASK                                                           0x00000100L
//DC_GPIO_DDCVGA_EN
#define DC_GPIO_DDCVGA_EN__DC_GPIO_DDCVGACLK_EN__SHIFT                                                        0x0
#define DC_GPIO_DDCVGA_EN__DC_GPIO_DDCVGADATA_EN__SHIFT                                                       0x8
#define DC_GPIO_DDCVGA_EN__DC_GPIO_DDCVGACLK_EN_MASK                                                          0x00000001L
#define DC_GPIO_DDCVGA_EN__DC_GPIO_DDCVGADATA_EN_MASK                                                         0x00000100L
//DC_GPIO_DDCVGA_Y
#define DC_GPIO_DDCVGA_Y__DC_GPIO_DDCVGACLK_Y__SHIFT                                                          0x0
#define DC_GPIO_DDCVGA_Y__DC_GPIO_DDCVGADATA_Y__SHIFT                                                         0x8
#define DC_GPIO_DDCVGA_Y__DC_GPIO_DDCVGACLK_Y_MASK                                                            0x00000001L
#define DC_GPIO_DDCVGA_Y__DC_GPIO_DDCVGADATA_Y_MASK                                                           0x00000100L
//DC_GPIO_PWRSEQ0_EN
#define DC_GPIO_PWRSEQ0_EN__DC_GPIO_VARY_BL_OTG_VSYNC_EN__SHIFT                                               0x14
#define DC_GPIO_PWRSEQ0_EN__DC_GPIO_VARY_BL_OTG_VSYNC_SEL__SHIFT                                              0x15
#define DC_GPIO_PWRSEQ0_EN__DC_GPIO_BLON_OTG_VSYNC_EN__SHIFT                                                  0x19
#define DC_GPIO_PWRSEQ0_EN__DC_GPIO_BLON_OTG_VSYNC_SEL__SHIFT                                                 0x1a
#define DC_GPIO_PWRSEQ0_EN__DC_GPIO_VARY_BL_GENERICA_EN__SHIFT                                                0x1d
#define DC_GPIO_PWRSEQ0_EN__DC_GPIO_VARY_BL_OTG_VSYNC_EN_MASK                                                 0x00100000L
#define DC_GPIO_PWRSEQ0_EN__DC_GPIO_VARY_BL_OTG_VSYNC_SEL_MASK                                                0x00E00000L
#define DC_GPIO_PWRSEQ0_EN__DC_GPIO_BLON_OTG_VSYNC_EN_MASK                                                    0x02000000L
#define DC_GPIO_PWRSEQ0_EN__DC_GPIO_BLON_OTG_VSYNC_SEL_MASK                                                   0x1C000000L
#define DC_GPIO_PWRSEQ0_EN__DC_GPIO_VARY_BL_GENERICA_EN_MASK                                                  0x20000000L
//DC_GPIO_PAD_STRENGTH_1
#define DC_GPIO_PAD_STRENGTH_1__GENLK_STRENGTH_SN__SHIFT                                                      0x0
#define DC_GPIO_PAD_STRENGTH_1__GENLK_STRENGTH_SP__SHIFT                                                      0x4
#define DC_GPIO_PAD_STRENGTH_1__SYNC_STRENGTH_SN__SHIFT                                                       0x18
#define DC_GPIO_PAD_STRENGTH_1__SYNC_STRENGTH_SP__SHIFT                                                       0x1c
#define DC_GPIO_PAD_STRENGTH_1__GENLK_STRENGTH_SN_MASK                                                        0x0000000FL
#define DC_GPIO_PAD_STRENGTH_1__GENLK_STRENGTH_SP_MASK                                                        0x000000F0L
#define DC_GPIO_PAD_STRENGTH_1__SYNC_STRENGTH_SN_MASK                                                         0x0F000000L
#define DC_GPIO_PAD_STRENGTH_1__SYNC_STRENGTH_SP_MASK                                                         0xF0000000L
//PHY_AUX_CNTL
#define PHY_AUX_CNTL__AUX_PAD_WAKE__SHIFT                                                                     0x9
#define PHY_AUX_CNTL__AUX1_PAD_RXSEL__SHIFT                                                                   0xa
#define PHY_AUX_CNTL__AUX2_PAD_RXSEL__SHIFT                                                                   0xc
#define PHY_AUX_CNTL__AUX3_PAD_RXSEL__SHIFT                                                                   0xe
#define PHY_AUX_CNTL__AUX4_PAD_RXSEL__SHIFT                                                                   0x10
#define PHY_AUX_CNTL__AUX5_PAD_RXSEL__SHIFT                                                                   0x12
#define PHY_AUX_CNTL__AUX6_PAD_RXSEL__SHIFT                                                                   0x14
#define PHY_AUX_CNTL__AUX_PAD_WAKE_MASK                                                                       0x00000200L
#define PHY_AUX_CNTL__AUX1_PAD_RXSEL_MASK                                                                     0x00000C00L
#define PHY_AUX_CNTL__AUX2_PAD_RXSEL_MASK                                                                     0x00003000L
#define PHY_AUX_CNTL__AUX3_PAD_RXSEL_MASK                                                                     0x0000C000L
#define PHY_AUX_CNTL__AUX4_PAD_RXSEL_MASK                                                                     0x00030000L
#define PHY_AUX_CNTL__AUX5_PAD_RXSEL_MASK                                                                     0x000C0000L
#define PHY_AUX_CNTL__AUX6_PAD_RXSEL_MASK                                                                     0x00300000L
//DC_GPIO_PWRSEQ1_EN
#define DC_GPIO_PWRSEQ1_EN__DC_GPIO_VARY_BL_OTG_VSYNC_EN__SHIFT                                               0x14
#define DC_GPIO_PWRSEQ1_EN__DC_GPIO_VARY_BL_OTG_VSYNC_SEL__SHIFT                                              0x15
#define DC_GPIO_PWRSEQ1_EN__DC_GPIO_BLON_OTG_VSYNC_EN__SHIFT                                                  0x19
#define DC_GPIO_PWRSEQ1_EN__DC_GPIO_BLON_OTG_VSYNC_SEL__SHIFT                                                 0x1a
#define DC_GPIO_PWRSEQ1_EN__DC_GPIO_VARY_BL_GENERICA_EN__SHIFT                                                0x1d
#define DC_GPIO_PWRSEQ1_EN__DC_GPIO_VARY_BL_OTG_VSYNC_EN_MASK                                                 0x00100000L
#define DC_GPIO_PWRSEQ1_EN__DC_GPIO_VARY_BL_OTG_VSYNC_SEL_MASK                                                0x00E00000L
#define DC_GPIO_PWRSEQ1_EN__DC_GPIO_BLON_OTG_VSYNC_EN_MASK                                                    0x02000000L
#define DC_GPIO_PWRSEQ1_EN__DC_GPIO_BLON_OTG_VSYNC_SEL_MASK                                                   0x1C000000L
#define DC_GPIO_PWRSEQ1_EN__DC_GPIO_VARY_BL_GENERICA_EN_MASK                                                  0x20000000L
//DC_GPIO_AUX_CTRL_0
#define DC_GPIO_AUX_CTRL_0__DC_GPIO_DDCVGA_FALLSLEWSEL__SHIFT                                                 0xc
#define DC_GPIO_AUX_CTRL_0__DC_GPIO_DDCVGA_SPIKERCEN__SHIFT                                                   0x16
#define DC_GPIO_AUX_CTRL_0__DC_GPIO_DDCVGA_SPIKERCSEL__SHIFT                                                  0x1e
#define DC_GPIO_AUX_CTRL_0__DC_GPIO_DDCVGA_FALLSLEWSEL_MASK                                                   0x00003000L
#define DC_GPIO_AUX_CTRL_0__DC_GPIO_DDCVGA_SPIKERCEN_MASK                                                     0x00C00000L
#define DC_GPIO_AUX_CTRL_0__DC_GPIO_DDCVGA_SPIKERCSEL_MASK                                                    0xC0000000L
//DC_GPIO_AUX_CTRL_1
#define DC_GPIO_AUX_CTRL_1__DC_GPIO_I2C_CSEL_0P9__SHIFT                                                       0x4
#define DC_GPIO_AUX_CTRL_1__DC_GPIO_I2C_CSEL_1P1__SHIFT                                                       0x5
#define DC_GPIO_AUX_CTRL_1__DC_GPIO_I2C_RSEL_0P9__SHIFT                                                       0x6
#define DC_GPIO_AUX_CTRL_1__DC_GPIO_I2C_RSEL_1P1__SHIFT                                                       0x7
#define DC_GPIO_AUX_CTRL_1__DC_GPIO_I2C_RESBIASEN__SHIFT                                                      0xb
#define DC_GPIO_AUX_CTRL_1__DC_GPIO_AUX1_COMPSEL__SHIFT                                                       0xd
#define DC_GPIO_AUX_CTRL_1__DC_GPIO_DDCVGA_SPARE__SHIFT                                                       0xe
#define DC_GPIO_AUX_CTRL_1__DC_GPIO_I2C_BIASCRTEN__SHIFT                                                      0x10
#define DC_GPIO_AUX_CTRL_1__DC_GPIO_DDCVGA_SLEWN__SHIFT                                                       0x12
#define DC_GPIO_AUX_CTRL_1__DC_GPIO_DDCVGA_RXSEL__SHIFT                                                       0x14
#define DC_GPIO_AUX_CTRL_1__DC_GPIO_AUX2_COMPSEL__SHIFT                                                       0x19
#define DC_GPIO_AUX_CTRL_1__DC_GPIO_AUX3_COMPSEL__SHIFT                                                       0x1a
#define DC_GPIO_AUX_CTRL_1__DC_GPIO_AUX4_COMPSEL__SHIFT                                                       0x1b
#define DC_GPIO_AUX_CTRL_1__DC_GPIO_AUX5_COMPSEL__SHIFT                                                       0x1c
#define DC_GPIO_AUX_CTRL_1__DC_GPIO_AUX6_COMPSEL__SHIFT                                                       0x1d
#define DC_GPIO_AUX_CTRL_1__DC_GPIO_DDCVGA_COMPSEL__SHIFT                                                     0x1e
#define DC_GPIO_AUX_CTRL_1__DC_GPIO_I2C_CSEL_0P9_MASK                                                         0x00000010L
#define DC_GPIO_AUX_CTRL_1__DC_GPIO_I2C_CSEL_1P1_MASK                                                         0x00000020L
#define DC_GPIO_AUX_CTRL_1__DC_GPIO_I2C_RSEL_0P9_MASK                                                         0x00000040L
#define DC_GPIO_AUX_CTRL_1__DC_GPIO_I2C_RSEL_1P1_MASK                                                         0x00000080L
#define DC_GPIO_AUX_CTRL_1__DC_GPIO_I2C_RESBIASEN_MASK                                                        0x00001800L
#define DC_GPIO_AUX_CTRL_1__DC_GPIO_AUX1_COMPSEL_MASK                                                         0x00002000L
#define DC_GPIO_AUX_CTRL_1__DC_GPIO_DDCVGA_SPARE_MASK                                                         0x0000C000L
#define DC_GPIO_AUX_CTRL_1__DC_GPIO_I2C_BIASCRTEN_MASK                                                        0x00030000L
#define DC_GPIO_AUX_CTRL_1__DC_GPIO_DDCVGA_SLEWN_MASK                                                         0x000C0000L
#define DC_GPIO_AUX_CTRL_1__DC_GPIO_DDCVGA_RXSEL_MASK                                                         0x00300000L
#define DC_GPIO_AUX_CTRL_1__DC_GPIO_AUX2_COMPSEL_MASK                                                         0x02000000L
#define DC_GPIO_AUX_CTRL_1__DC_GPIO_AUX3_COMPSEL_MASK                                                         0x04000000L
#define DC_GPIO_AUX_CTRL_1__DC_GPIO_AUX4_COMPSEL_MASK                                                         0x08000000L
#define DC_GPIO_AUX_CTRL_1__DC_GPIO_AUX5_COMPSEL_MASK                                                         0x10000000L
#define DC_GPIO_AUX_CTRL_1__DC_GPIO_AUX6_COMPSEL_MASK                                                         0x20000000L
#define DC_GPIO_AUX_CTRL_1__DC_GPIO_DDCVGA_COMPSEL_MASK                                                       0xC0000000L
//DC_GPIO_AUX_CTRL_3
#define DC_GPIO_AUX_CTRL_3__AUX1_NEN_RTERM__SHIFT                                                             0x0
#define DC_GPIO_AUX_CTRL_3__AUX2_NEN_RTERM__SHIFT                                                             0x1
#define DC_GPIO_AUX_CTRL_3__AUX3_NEN_RTERM__SHIFT                                                             0x2
#define DC_GPIO_AUX_CTRL_3__AUX4_NEN_RTERM__SHIFT                                                             0x3
#define DC_GPIO_AUX_CTRL_3__AUX5_NEN_RTERM__SHIFT                                                             0x4
#define DC_GPIO_AUX_CTRL_3__AUX6_NEN_RTERM__SHIFT                                                             0x5
#define DC_GPIO_AUX_CTRL_3__AUX1_DP_DN_SWAP__SHIFT                                                            0x8
#define DC_GPIO_AUX_CTRL_3__AUX2_DP_DN_SWAP__SHIFT                                                            0x9
#define DC_GPIO_AUX_CTRL_3__AUX3_DP_DN_SWAP__SHIFT                                                            0xa
#define DC_GPIO_AUX_CTRL_3__AUX4_DP_DN_SWAP__SHIFT                                                            0xb
#define DC_GPIO_AUX_CTRL_3__AUX5_DP_DN_SWAP__SHIFT                                                            0xc
#define DC_GPIO_AUX_CTRL_3__AUX6_DP_DN_SWAP__SHIFT                                                            0xd
#define DC_GPIO_AUX_CTRL_3__AUX1_HYS_TUNE__SHIFT                                                              0x10
#define DC_GPIO_AUX_CTRL_3__AUX2_HYS_TUNE__SHIFT                                                              0x12
#define DC_GPIO_AUX_CTRL_3__AUX3_HYS_TUNE__SHIFT                                                              0x14
#define DC_GPIO_AUX_CTRL_3__AUX4_HYS_TUNE__SHIFT                                                              0x16
#define DC_GPIO_AUX_CTRL_3__AUX5_HYS_TUNE__SHIFT                                                              0x18
#define DC_GPIO_AUX_CTRL_3__AUX6_HYS_TUNE__SHIFT                                                              0x1a
#define DC_GPIO_AUX_CTRL_3__AUX1_NEN_RTERM_MASK                                                               0x00000001L
#define DC_GPIO_AUX_CTRL_3__AUX2_NEN_RTERM_MASK                                                               0x00000002L
#define DC_GPIO_AUX_CTRL_3__AUX3_NEN_RTERM_MASK                                                               0x00000004L
#define DC_GPIO_AUX_CTRL_3__AUX4_NEN_RTERM_MASK                                                               0x00000008L
#define DC_GPIO_AUX_CTRL_3__AUX5_NEN_RTERM_MASK                                                               0x00000010L
#define DC_GPIO_AUX_CTRL_3__AUX6_NEN_RTERM_MASK                                                               0x00000020L
#define DC_GPIO_AUX_CTRL_3__AUX1_DP_DN_SWAP_MASK                                                              0x00000100L
#define DC_GPIO_AUX_CTRL_3__AUX2_DP_DN_SWAP_MASK                                                              0x00000200L
#define DC_GPIO_AUX_CTRL_3__AUX3_DP_DN_SWAP_MASK                                                              0x00000400L
#define DC_GPIO_AUX_CTRL_3__AUX4_DP_DN_SWAP_MASK                                                              0x00000800L
#define DC_GPIO_AUX_CTRL_3__AUX5_DP_DN_SWAP_MASK                                                              0x00001000L
#define DC_GPIO_AUX_CTRL_3__AUX6_DP_DN_SWAP_MASK                                                              0x00002000L
#define DC_GPIO_AUX_CTRL_3__AUX1_HYS_TUNE_MASK                                                                0x00030000L
#define DC_GPIO_AUX_CTRL_3__AUX2_HYS_TUNE_MASK                                                                0x000C0000L
#define DC_GPIO_AUX_CTRL_3__AUX3_HYS_TUNE_MASK                                                                0x00300000L
#define DC_GPIO_AUX_CTRL_3__AUX4_HYS_TUNE_MASK                                                                0x00C00000L
#define DC_GPIO_AUX_CTRL_3__AUX5_HYS_TUNE_MASK                                                                0x03000000L
#define DC_GPIO_AUX_CTRL_3__AUX6_HYS_TUNE_MASK                                                                0x0C000000L
//DC_GPIO_AUX_CTRL_4
#define DC_GPIO_AUX_CTRL_4__AUX1_AUX_CTRL__SHIFT                                                              0x0
#define DC_GPIO_AUX_CTRL_4__AUX2_AUX_CTRL__SHIFT                                                              0x4
#define DC_GPIO_AUX_CTRL_4__AUX3_AUX_CTRL__SHIFT                                                              0x8
#define DC_GPIO_AUX_CTRL_4__AUX4_AUX_CTRL__SHIFT                                                              0xc
#define DC_GPIO_AUX_CTRL_4__AUX5_AUX_CTRL__SHIFT                                                              0x10
#define DC_GPIO_AUX_CTRL_4__AUX6_AUX_CTRL__SHIFT                                                              0x14
#define DC_GPIO_AUX_CTRL_4__AUX1_AUX_CTRL_MASK                                                                0x0000000FL
#define DC_GPIO_AUX_CTRL_4__AUX2_AUX_CTRL_MASK                                                                0x000000F0L
#define DC_GPIO_AUX_CTRL_4__AUX3_AUX_CTRL_MASK                                                                0x00000F00L
#define DC_GPIO_AUX_CTRL_4__AUX4_AUX_CTRL_MASK                                                                0x0000F000L
#define DC_GPIO_AUX_CTRL_4__AUX5_AUX_CTRL_MASK                                                                0x000F0000L
#define DC_GPIO_AUX_CTRL_4__AUX6_AUX_CTRL_MASK                                                                0x00F00000L
//DC_GPIO_AUX_CTRL_5
#define DC_GPIO_AUX_CTRL_5__AUX1_VOD_TUNE__SHIFT                                                              0x0
#define DC_GPIO_AUX_CTRL_5__AUX2_VOD_TUNE__SHIFT                                                              0x2
#define DC_GPIO_AUX_CTRL_5__AUX3_VOD_TUNE__SHIFT                                                              0x4
#define DC_GPIO_AUX_CTRL_5__AUX4_VOD_TUNE__SHIFT                                                              0x6
#define DC_GPIO_AUX_CTRL_5__AUX5_VOD_TUNE__SHIFT                                                              0x8
#define DC_GPIO_AUX_CTRL_5__AUX6_VOD_TUNE__SHIFT                                                              0xa
#define DC_GPIO_AUX_CTRL_5__DDC_PAD1_I2CMODE__SHIFT                                                           0xc
#define DC_GPIO_AUX_CTRL_5__DDC_PAD2_I2CMODE__SHIFT                                                           0xd
#define DC_GPIO_AUX_CTRL_5__DDC_PAD3_I2CMODE__SHIFT                                                           0xe
#define DC_GPIO_AUX_CTRL_5__DDC_PAD4_I2CMODE__SHIFT                                                           0xf
#define DC_GPIO_AUX_CTRL_5__DDC_PAD5_I2CMODE__SHIFT                                                           0x10
#define DC_GPIO_AUX_CTRL_5__DDC_PAD6_I2CMODE__SHIFT                                                           0x11
#define DC_GPIO_AUX_CTRL_5__DDC1_I2C_VPH_1V2_EN__SHIFT                                                        0x12
#define DC_GPIO_AUX_CTRL_5__DDC2_I2C_VPH_1V2_EN__SHIFT                                                        0x13
#define DC_GPIO_AUX_CTRL_5__DDC3_I2C_VPH_1V2_EN__SHIFT                                                        0x14
#define DC_GPIO_AUX_CTRL_5__DDC4_I2C_VPH_1V2_EN__SHIFT                                                        0x15
#define DC_GPIO_AUX_CTRL_5__DDC5_I2C_VPH_1V2_EN__SHIFT                                                        0x16
#define DC_GPIO_AUX_CTRL_5__DDC6_I2C_VPH_1V2_EN__SHIFT                                                        0x17
#define DC_GPIO_AUX_CTRL_5__DDC1_PAD_I2C_CTRL__SHIFT                                                          0x18
#define DC_GPIO_AUX_CTRL_5__DDC2_PAD_I2C_CTRL__SHIFT                                                          0x19
#define DC_GPIO_AUX_CTRL_5__DDC3_PAD_I2C_CTRL__SHIFT                                                          0x1a
#define DC_GPIO_AUX_CTRL_5__DDC4_PAD_I2C_CTRL__SHIFT                                                          0x1b
#define DC_GPIO_AUX_CTRL_5__DDC5_PAD_I2C_CTRL__SHIFT                                                          0x1c
#define DC_GPIO_AUX_CTRL_5__DDC6_PAD_I2C_CTRL__SHIFT                                                          0x1d
#define DC_GPIO_AUX_CTRL_5__AUX1_VOD_TUNE_MASK                                                                0x00000003L
#define DC_GPIO_AUX_CTRL_5__AUX2_VOD_TUNE_MASK                                                                0x0000000CL
#define DC_GPIO_AUX_CTRL_5__AUX3_VOD_TUNE_MASK                                                                0x00000030L
#define DC_GPIO_AUX_CTRL_5__AUX4_VOD_TUNE_MASK                                                                0x000000C0L
#define DC_GPIO_AUX_CTRL_5__AUX5_VOD_TUNE_MASK                                                                0x00000300L
#define DC_GPIO_AUX_CTRL_5__AUX6_VOD_TUNE_MASK                                                                0x00000C00L
#define DC_GPIO_AUX_CTRL_5__DDC_PAD1_I2CMODE_MASK                                                             0x00001000L
#define DC_GPIO_AUX_CTRL_5__DDC_PAD2_I2CMODE_MASK                                                             0x00002000L
#define DC_GPIO_AUX_CTRL_5__DDC_PAD3_I2CMODE_MASK                                                             0x00004000L
#define DC_GPIO_AUX_CTRL_5__DDC_PAD4_I2CMODE_MASK                                                             0x00008000L
#define DC_GPIO_AUX_CTRL_5__DDC_PAD5_I2CMODE_MASK                                                             0x00010000L
#define DC_GPIO_AUX_CTRL_5__DDC_PAD6_I2CMODE_MASK                                                             0x00020000L
#define DC_GPIO_AUX_CTRL_5__DDC1_I2C_VPH_1V2_EN_MASK                                                          0x00040000L
#define DC_GPIO_AUX_CTRL_5__DDC2_I2C_VPH_1V2_EN_MASK                                                          0x00080000L
#define DC_GPIO_AUX_CTRL_5__DDC3_I2C_VPH_1V2_EN_MASK                                                          0x00100000L
#define DC_GPIO_AUX_CTRL_5__DDC4_I2C_VPH_1V2_EN_MASK                                                          0x00200000L
#define DC_GPIO_AUX_CTRL_5__DDC5_I2C_VPH_1V2_EN_MASK                                                          0x00400000L
#define DC_GPIO_AUX_CTRL_5__DDC6_I2C_VPH_1V2_EN_MASK                                                          0x00800000L
#define DC_GPIO_AUX_CTRL_5__DDC1_PAD_I2C_CTRL_MASK                                                            0x01000000L
#define DC_GPIO_AUX_CTRL_5__DDC2_PAD_I2C_CTRL_MASK                                                            0x02000000L
#define DC_GPIO_AUX_CTRL_5__DDC3_PAD_I2C_CTRL_MASK                                                            0x04000000L
#define DC_GPIO_AUX_CTRL_5__DDC4_PAD_I2C_CTRL_MASK                                                            0x08000000L
#define DC_GPIO_AUX_CTRL_5__DDC5_PAD_I2C_CTRL_MASK                                                            0x10000000L
#define DC_GPIO_AUX_CTRL_5__DDC6_PAD_I2C_CTRL_MASK                                                            0x20000000L
//AUXI2C_PAD_ALL_PWR_OK
#define AUXI2C_PAD_ALL_PWR_OK__AUXI2C_PHY1_ALL_PWR_OK__SHIFT                                                  0x0
#define AUXI2C_PAD_ALL_PWR_OK__AUXI2C_PHY2_ALL_PWR_OK__SHIFT                                                  0x1
#define AUXI2C_PAD_ALL_PWR_OK__AUXI2C_PHY3_ALL_PWR_OK__SHIFT                                                  0x2
#define AUXI2C_PAD_ALL_PWR_OK__AUXI2C_PHY4_ALL_PWR_OK__SHIFT                                                  0x3
#define AUXI2C_PAD_ALL_PWR_OK__AUXI2C_PHY5_ALL_PWR_OK__SHIFT                                                  0x4
#define AUXI2C_PAD_ALL_PWR_OK__AUXI2C_PHY6_ALL_PWR_OK__SHIFT                                                  0x5
#define AUXI2C_PAD_ALL_PWR_OK__AUXI2C_PHY1_ALL_PWR_OK_MASK                                                    0x00000001L
#define AUXI2C_PAD_ALL_PWR_OK__AUXI2C_PHY2_ALL_PWR_OK_MASK                                                    0x00000002L
#define AUXI2C_PAD_ALL_PWR_OK__AUXI2C_PHY3_ALL_PWR_OK_MASK                                                    0x00000004L
#define AUXI2C_PAD_ALL_PWR_OK__AUXI2C_PHY4_ALL_PWR_OK_MASK                                                    0x00000008L
#define AUXI2C_PAD_ALL_PWR_OK__AUXI2C_PHY5_ALL_PWR_OK_MASK                                                    0x00000010L
#define AUXI2C_PAD_ALL_PWR_OK__AUXI2C_PHY6_ALL_PWR_OK_MASK                                                    0x00000020L

#endif
