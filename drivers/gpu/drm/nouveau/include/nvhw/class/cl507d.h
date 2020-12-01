/*
 * Copyright (c) 1993-2014, NVIDIA CORPORATION. All rights reserved.
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
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */


#ifndef _cl507d_h_
#define _cl507d_h_

#define NV_DISP_CORE_NOTIFIER_1                                                      0x00000000
#define NV_DISP_CORE_NOTIFIER_1_SIZEOF                                               0x00000054
#define NV_DISP_CORE_NOTIFIER_1_COMPLETION_0                                         0x00000000
#define NV_DISP_CORE_NOTIFIER_1_COMPLETION_0_DONE                                    0:0
#define NV_DISP_CORE_NOTIFIER_1_COMPLETION_0_DONE_FALSE                              0x00000000
#define NV_DISP_CORE_NOTIFIER_1_COMPLETION_0_DONE_TRUE                               0x00000001
#define NV_DISP_CORE_NOTIFIER_1_COMPLETION_0_R0                                      15:1
#define NV_DISP_CORE_NOTIFIER_1_COMPLETION_0_TIMESTAMP                               29:16
#define NV_DISP_CORE_NOTIFIER_1_CAPABILITIES_1                                       0x00000001
#define NV_DISP_CORE_NOTIFIER_1_CAPABILITIES_1_DONE                                  0:0
#define NV_DISP_CORE_NOTIFIER_1_CAPABILITIES_1_DONE_FALSE                            0x00000000
#define NV_DISP_CORE_NOTIFIER_1_CAPABILITIES_1_DONE_TRUE                             0x00000001

// class methods
#define NV507D_UPDATE                                                           (0x00000080)
#define NV507D_UPDATE_INTERLOCK_WITH_CURSOR0                                    0:0
#define NV507D_UPDATE_INTERLOCK_WITH_CURSOR0_DISABLE                            (0x00000000)
#define NV507D_UPDATE_INTERLOCK_WITH_CURSOR0_ENABLE                             (0x00000001)
#define NV507D_UPDATE_INTERLOCK_WITH_CURSOR1                                    8:8
#define NV507D_UPDATE_INTERLOCK_WITH_CURSOR1_DISABLE                            (0x00000000)
#define NV507D_UPDATE_INTERLOCK_WITH_CURSOR1_ENABLE                             (0x00000001)
#define NV507D_UPDATE_INTERLOCK_WITH_BASE0                                      1:1
#define NV507D_UPDATE_INTERLOCK_WITH_BASE0_DISABLE                              (0x00000000)
#define NV507D_UPDATE_INTERLOCK_WITH_BASE0_ENABLE                               (0x00000001)
#define NV507D_UPDATE_INTERLOCK_WITH_BASE1                                      9:9
#define NV507D_UPDATE_INTERLOCK_WITH_BASE1_DISABLE                              (0x00000000)
#define NV507D_UPDATE_INTERLOCK_WITH_BASE1_ENABLE                               (0x00000001)
#define NV507D_UPDATE_INTERLOCK_WITH_OVERLAY0                                   2:2
#define NV507D_UPDATE_INTERLOCK_WITH_OVERLAY0_DISABLE                           (0x00000000)
#define NV507D_UPDATE_INTERLOCK_WITH_OVERLAY0_ENABLE                            (0x00000001)
#define NV507D_UPDATE_INTERLOCK_WITH_OVERLAY1                                   10:10
#define NV507D_UPDATE_INTERLOCK_WITH_OVERLAY1_DISABLE                           (0x00000000)
#define NV507D_UPDATE_INTERLOCK_WITH_OVERLAY1_ENABLE                            (0x00000001)
#define NV507D_UPDATE_INTERLOCK_WITH_OVERLAY_IMM0                               3:3
#define NV507D_UPDATE_INTERLOCK_WITH_OVERLAY_IMM0_DISABLE                       (0x00000000)
#define NV507D_UPDATE_INTERLOCK_WITH_OVERLAY_IMM0_ENABLE                        (0x00000001)
#define NV507D_UPDATE_INTERLOCK_WITH_OVERLAY_IMM1                               11:11
#define NV507D_UPDATE_INTERLOCK_WITH_OVERLAY_IMM1_DISABLE                       (0x00000000)
#define NV507D_UPDATE_INTERLOCK_WITH_OVERLAY_IMM1_ENABLE                        (0x00000001)
#define NV507D_UPDATE_NOT_DRIVER_FRIENDLY                                       31:31
#define NV507D_UPDATE_NOT_DRIVER_FRIENDLY_FALSE                                 (0x00000000)
#define NV507D_UPDATE_NOT_DRIVER_FRIENDLY_TRUE                                  (0x00000001)
#define NV507D_UPDATE_NOT_DRIVER_UNFRIENDLY                                     30:30
#define NV507D_UPDATE_NOT_DRIVER_UNFRIENDLY_FALSE                               (0x00000000)
#define NV507D_UPDATE_NOT_DRIVER_UNFRIENDLY_TRUE                                (0x00000001)
#define NV507D_UPDATE_INHIBIT_INTERRUPTS                                        29:29
#define NV507D_UPDATE_INHIBIT_INTERRUPTS_FALSE                                  (0x00000000)
#define NV507D_UPDATE_INHIBIT_INTERRUPTS_TRUE                                   (0x00000001)
#define NV507D_SET_NOTIFIER_CONTROL                                             (0x00000084)
#define NV507D_SET_NOTIFIER_CONTROL_MODE                                        30:30
#define NV507D_SET_NOTIFIER_CONTROL_MODE_WRITE                                  (0x00000000)
#define NV507D_SET_NOTIFIER_CONTROL_MODE_WRITE_AWAKEN                           (0x00000001)
#define NV507D_SET_NOTIFIER_CONTROL_OFFSET                                      11:2
#define NV507D_SET_NOTIFIER_CONTROL_NOTIFY                                      31:31
#define NV507D_SET_NOTIFIER_CONTROL_NOTIFY_DISABLE                              (0x00000000)
#define NV507D_SET_NOTIFIER_CONTROL_NOTIFY_ENABLE                               (0x00000001)
#define NV507D_SET_CONTEXT_DMA_NOTIFIER                                         (0x00000088)
#define NV507D_SET_CONTEXT_DMA_NOTIFIER_HANDLE                                  31:0
#define NV507D_GET_CAPABILITIES                                                 (0x0000008C)
#define NV507D_GET_CAPABILITIES_DUMMY                                           31:0

#define NV507D_DAC_SET_CONTROL(a)                                               (0x00000400 + (a)*0x00000080)
#define NV507D_DAC_SET_CONTROL_OWNER                                            3:0
#define NV507D_DAC_SET_CONTROL_OWNER_NONE                                       (0x00000000)
#define NV507D_DAC_SET_CONTROL_OWNER_HEAD0                                      (0x00000001)
#define NV507D_DAC_SET_CONTROL_OWNER_HEAD1                                      (0x00000002)
#define NV507D_DAC_SET_CONTROL_SUB_OWNER                                        5:4
#define NV507D_DAC_SET_CONTROL_SUB_OWNER_NONE                                   (0x00000000)
#define NV507D_DAC_SET_CONTROL_SUB_OWNER_SUBHEAD0                               (0x00000001)
#define NV507D_DAC_SET_CONTROL_SUB_OWNER_SUBHEAD1                               (0x00000002)
#define NV507D_DAC_SET_CONTROL_SUB_OWNER_BOTH                                   (0x00000003)
#define NV507D_DAC_SET_CONTROL_PROTOCOL                                         13:8
#define NV507D_DAC_SET_CONTROL_PROTOCOL_RGB_CRT                                 (0x00000000)
#define NV507D_DAC_SET_CONTROL_PROTOCOL_CPST_NTSC_M                             (0x00000001)
#define NV507D_DAC_SET_CONTROL_PROTOCOL_CPST_NTSC_J                             (0x00000002)
#define NV507D_DAC_SET_CONTROL_PROTOCOL_CPST_PAL_BDGHI                          (0x00000003)
#define NV507D_DAC_SET_CONTROL_PROTOCOL_CPST_PAL_M                              (0x00000004)
#define NV507D_DAC_SET_CONTROL_PROTOCOL_CPST_PAL_N                              (0x00000005)
#define NV507D_DAC_SET_CONTROL_PROTOCOL_CPST_PAL_CN                             (0x00000006)
#define NV507D_DAC_SET_CONTROL_PROTOCOL_COMP_NTSC_M                             (0x00000007)
#define NV507D_DAC_SET_CONTROL_PROTOCOL_COMP_NTSC_J                             (0x00000008)
#define NV507D_DAC_SET_CONTROL_PROTOCOL_COMP_PAL_BDGHI                          (0x00000009)
#define NV507D_DAC_SET_CONTROL_PROTOCOL_COMP_PAL_M                              (0x0000000A)
#define NV507D_DAC_SET_CONTROL_PROTOCOL_COMP_PAL_N                              (0x0000000B)
#define NV507D_DAC_SET_CONTROL_PROTOCOL_COMP_PAL_CN                             (0x0000000C)
#define NV507D_DAC_SET_CONTROL_PROTOCOL_COMP_480P_60                            (0x0000000D)
#define NV507D_DAC_SET_CONTROL_PROTOCOL_COMP_576P_50                            (0x0000000E)
#define NV507D_DAC_SET_CONTROL_PROTOCOL_COMP_720P_50                            (0x0000000F)
#define NV507D_DAC_SET_CONTROL_PROTOCOL_COMP_720P_60                            (0x00000010)
#define NV507D_DAC_SET_CONTROL_PROTOCOL_COMP_1080I_50                           (0x00000011)
#define NV507D_DAC_SET_CONTROL_PROTOCOL_COMP_1080I_60                           (0x00000012)
#define NV507D_DAC_SET_CONTROL_PROTOCOL_CUSTOM                                  (0x0000003F)
#define NV507D_DAC_SET_CONTROL_INVALIDATE_FIRST_FIELD                           14:14
#define NV507D_DAC_SET_CONTROL_INVALIDATE_FIRST_FIELD_FALSE                     (0x00000000)
#define NV507D_DAC_SET_CONTROL_INVALIDATE_FIRST_FIELD_TRUE                      (0x00000001)
#define NV507D_DAC_SET_POLARITY(a)                                              (0x00000404 + (a)*0x00000080)
#define NV507D_DAC_SET_POLARITY_HSYNC                                           0:0
#define NV507D_DAC_SET_POLARITY_HSYNC_POSITIVE_TRUE                             (0x00000000)
#define NV507D_DAC_SET_POLARITY_HSYNC_NEGATIVE_TRUE                             (0x00000001)
#define NV507D_DAC_SET_POLARITY_VSYNC                                           1:1
#define NV507D_DAC_SET_POLARITY_VSYNC_POSITIVE_TRUE                             (0x00000000)
#define NV507D_DAC_SET_POLARITY_VSYNC_NEGATIVE_TRUE                             (0x00000001)
#define NV507D_DAC_SET_POLARITY_RESERVED                                        31:2

#define NV507D_SOR_SET_CONTROL(a)                                               (0x00000600 + (a)*0x00000040)
#define NV507D_SOR_SET_CONTROL_OWNER                                            3:0
#define NV507D_SOR_SET_CONTROL_OWNER_NONE                                       (0x00000000)
#define NV507D_SOR_SET_CONTROL_OWNER_HEAD0                                      (0x00000001)
#define NV507D_SOR_SET_CONTROL_OWNER_HEAD1                                      (0x00000002)
#define NV507D_SOR_SET_CONTROL_SUB_OWNER                                        5:4
#define NV507D_SOR_SET_CONTROL_SUB_OWNER_NONE                                   (0x00000000)
#define NV507D_SOR_SET_CONTROL_SUB_OWNER_SUBHEAD0                               (0x00000001)
#define NV507D_SOR_SET_CONTROL_SUB_OWNER_SUBHEAD1                               (0x00000002)
#define NV507D_SOR_SET_CONTROL_SUB_OWNER_BOTH                                   (0x00000003)
#define NV507D_SOR_SET_CONTROL_PROTOCOL                                         11:8
#define NV507D_SOR_SET_CONTROL_PROTOCOL_LVDS_CUSTOM                             (0x00000000)
#define NV507D_SOR_SET_CONTROL_PROTOCOL_SINGLE_TMDS_A                           (0x00000001)
#define NV507D_SOR_SET_CONTROL_PROTOCOL_SINGLE_TMDS_B                           (0x00000002)
#define NV507D_SOR_SET_CONTROL_PROTOCOL_SINGLE_TMDS_AB                          (0x00000003)
#define NV507D_SOR_SET_CONTROL_PROTOCOL_DUAL_SINGLE_TMDS                        (0x00000004)
#define NV507D_SOR_SET_CONTROL_PROTOCOL_DUAL_TMDS                               (0x00000005)
#define NV507D_SOR_SET_CONTROL_PROTOCOL_DDI_OUT                                 (0x00000007)
#define NV507D_SOR_SET_CONTROL_PROTOCOL_CUSTOM                                  (0x0000000F)
#define NV507D_SOR_SET_CONTROL_HSYNC_POLARITY                                   12:12
#define NV507D_SOR_SET_CONTROL_HSYNC_POLARITY_POSITIVE_TRUE                     (0x00000000)
#define NV507D_SOR_SET_CONTROL_HSYNC_POLARITY_NEGATIVE_TRUE                     (0x00000001)
#define NV507D_SOR_SET_CONTROL_VSYNC_POLARITY                                   13:13
#define NV507D_SOR_SET_CONTROL_VSYNC_POLARITY_POSITIVE_TRUE                     (0x00000000)
#define NV507D_SOR_SET_CONTROL_VSYNC_POLARITY_NEGATIVE_TRUE                     (0x00000001)
#define NV507D_SOR_SET_CONTROL_DE_SYNC_POLARITY                                 14:14
#define NV507D_SOR_SET_CONTROL_DE_SYNC_POLARITY_POSITIVE_TRUE                   (0x00000000)
#define NV507D_SOR_SET_CONTROL_DE_SYNC_POLARITY_NEGATIVE_TRUE                   (0x00000001)

#define NV507D_PIOR_SET_CONTROL(a)                                              (0x00000700 + (a)*0x00000040)
#define NV507D_PIOR_SET_CONTROL_OWNER                                           3:0
#define NV507D_PIOR_SET_CONTROL_OWNER_NONE                                      (0x00000000)
#define NV507D_PIOR_SET_CONTROL_OWNER_HEAD0                                     (0x00000001)
#define NV507D_PIOR_SET_CONTROL_OWNER_HEAD1                                     (0x00000002)
#define NV507D_PIOR_SET_CONTROL_SUB_OWNER                                       5:4
#define NV507D_PIOR_SET_CONTROL_SUB_OWNER_NONE                                  (0x00000000)
#define NV507D_PIOR_SET_CONTROL_SUB_OWNER_SUBHEAD0                              (0x00000001)
#define NV507D_PIOR_SET_CONTROL_SUB_OWNER_SUBHEAD1                              (0x00000002)
#define NV507D_PIOR_SET_CONTROL_SUB_OWNER_BOTH                                  (0x00000003)
#define NV507D_PIOR_SET_CONTROL_PROTOCOL                                        11:8
#define NV507D_PIOR_SET_CONTROL_PROTOCOL_EXT_TMDS_ENC                           (0x00000000)
#define NV507D_PIOR_SET_CONTROL_PROTOCOL_EXT_TV_ENC                             (0x00000001)
#define NV507D_PIOR_SET_CONTROL_HSYNC_POLARITY                                  12:12
#define NV507D_PIOR_SET_CONTROL_HSYNC_POLARITY_POSITIVE_TRUE                    (0x00000000)
#define NV507D_PIOR_SET_CONTROL_HSYNC_POLARITY_NEGATIVE_TRUE                    (0x00000001)
#define NV507D_PIOR_SET_CONTROL_VSYNC_POLARITY                                  13:13
#define NV507D_PIOR_SET_CONTROL_VSYNC_POLARITY_POSITIVE_TRUE                    (0x00000000)
#define NV507D_PIOR_SET_CONTROL_VSYNC_POLARITY_NEGATIVE_TRUE                    (0x00000001)
#define NV507D_PIOR_SET_CONTROL_DE_SYNC_POLARITY                                14:14
#define NV507D_PIOR_SET_CONTROL_DE_SYNC_POLARITY_POSITIVE_TRUE                  (0x00000000)
#define NV507D_PIOR_SET_CONTROL_DE_SYNC_POLARITY_NEGATIVE_TRUE                  (0x00000001)

#define NV507D_HEAD_SET_PIXEL_CLOCK(a)                                          (0x00000804 + (a)*0x00000400)
#define NV507D_HEAD_SET_PIXEL_CLOCK_FREQUENCY                                   21:0
#define NV507D_HEAD_SET_PIXEL_CLOCK_MODE                                        23:22
#define NV507D_HEAD_SET_PIXEL_CLOCK_MODE_CLK_25                                 (0x00000000)
#define NV507D_HEAD_SET_PIXEL_CLOCK_MODE_CLK_28                                 (0x00000001)
#define NV507D_HEAD_SET_PIXEL_CLOCK_MODE_CLK_CUSTOM                             (0x00000002)
#define NV507D_HEAD_SET_PIXEL_CLOCK_ADJ1000DIV1001                              24:24
#define NV507D_HEAD_SET_PIXEL_CLOCK_ADJ1000DIV1001_FALSE                        (0x00000000)
#define NV507D_HEAD_SET_PIXEL_CLOCK_ADJ1000DIV1001_TRUE                         (0x00000001)
#define NV507D_HEAD_SET_PIXEL_CLOCK_NOT_DRIVER                                  25:25
#define NV507D_HEAD_SET_PIXEL_CLOCK_NOT_DRIVER_FALSE                            (0x00000000)
#define NV507D_HEAD_SET_PIXEL_CLOCK_NOT_DRIVER_TRUE                             (0x00000001)
#define NV507D_HEAD_SET_CONTROL(a)                                              (0x00000808 + (a)*0x00000400)
#define NV507D_HEAD_SET_CONTROL_STRUCTURE                                       2:1
#define NV507D_HEAD_SET_CONTROL_STRUCTURE_PROGRESSIVE                           (0x00000000)
#define NV507D_HEAD_SET_CONTROL_STRUCTURE_INTERLACED                            (0x00000001)
#define NV507D_HEAD_SET_OVERSCAN_COLOR(a)                                       (0x00000810 + (a)*0x00000400)
#define NV507D_HEAD_SET_OVERSCAN_COLOR_RED                                      9:0
#define NV507D_HEAD_SET_OVERSCAN_COLOR_GRN                                      19:10
#define NV507D_HEAD_SET_OVERSCAN_COLOR_BLU                                      29:20
#define NV507D_HEAD_SET_RASTER_SIZE(a)                                          (0x00000814 + (a)*0x00000400)
#define NV507D_HEAD_SET_RASTER_SIZE_WIDTH                                       14:0
#define NV507D_HEAD_SET_RASTER_SIZE_HEIGHT                                      30:16
#define NV507D_HEAD_SET_RASTER_SYNC_END(a)                                      (0x00000818 + (a)*0x00000400)
#define NV507D_HEAD_SET_RASTER_SYNC_END_X                                       14:0
#define NV507D_HEAD_SET_RASTER_SYNC_END_Y                                       30:16
#define NV507D_HEAD_SET_RASTER_BLANK_END(a)                                     (0x0000081C + (a)*0x00000400)
#define NV507D_HEAD_SET_RASTER_BLANK_END_X                                      14:0
#define NV507D_HEAD_SET_RASTER_BLANK_END_Y                                      30:16
#define NV507D_HEAD_SET_RASTER_BLANK_START(a)                                   (0x00000820 + (a)*0x00000400)
#define NV507D_HEAD_SET_RASTER_BLANK_START_X                                    14:0
#define NV507D_HEAD_SET_RASTER_BLANK_START_Y                                    30:16
#define NV507D_HEAD_SET_RASTER_VERT_BLANK2(a)                                   (0x00000824 + (a)*0x00000400)
#define NV507D_HEAD_SET_RASTER_VERT_BLANK2_YSTART                               14:0
#define NV507D_HEAD_SET_RASTER_VERT_BLANK2_YEND                                 30:16
#define NV507D_HEAD_SET_RASTER_VERT_BLANK_DMI(a)                                (0x00000828 + (a)*0x00000400)
#define NV507D_HEAD_SET_RASTER_VERT_BLANK_DMI_DURATION                          11:0
#define NV507D_HEAD_SET_DEFAULT_BASE_COLOR(a)                                   (0x0000082C + (a)*0x00000400)
#define NV507D_HEAD_SET_DEFAULT_BASE_COLOR_RED                                  9:0
#define NV507D_HEAD_SET_DEFAULT_BASE_COLOR_GREEN                                19:10
#define NV507D_HEAD_SET_DEFAULT_BASE_COLOR_BLUE                                 29:20
#define NV507D_HEAD_SET_BASE_LUT_LO(a)                                          (0x00000840 + (a)*0x00000400)
#define NV507D_HEAD_SET_BASE_LUT_LO_ENABLE                                      31:31
#define NV507D_HEAD_SET_BASE_LUT_LO_ENABLE_DISABLE                              (0x00000000)
#define NV507D_HEAD_SET_BASE_LUT_LO_ENABLE_ENABLE                               (0x00000001)
#define NV507D_HEAD_SET_BASE_LUT_LO_MODE                                        30:30
#define NV507D_HEAD_SET_BASE_LUT_LO_MODE_LORES                                  (0x00000000)
#define NV507D_HEAD_SET_BASE_LUT_LO_MODE_HIRES                                  (0x00000001)
#define NV507D_HEAD_SET_BASE_LUT_LO_ORIGIN                                      7:2
#define NV507D_HEAD_SET_BASE_LUT_HI(a)                                          (0x00000844 + (a)*0x00000400)
#define NV507D_HEAD_SET_BASE_LUT_HI_ORIGIN                                      31:0
#define NV507D_HEAD_SET_OFFSET(a,b)                                             (0x00000860 + (a)*0x00000400 + (b)*0x00000004)
#define NV507D_HEAD_SET_OFFSET_ORIGIN                                           31:0
#define NV507D_HEAD_SET_SIZE(a)                                                 (0x00000868 + (a)*0x00000400)
#define NV507D_HEAD_SET_SIZE_WIDTH                                              14:0
#define NV507D_HEAD_SET_SIZE_HEIGHT                                             30:16
#define NV507D_HEAD_SET_STORAGE(a)                                              (0x0000086C + (a)*0x00000400)
#define NV507D_HEAD_SET_STORAGE_BLOCK_HEIGHT                                    3:0
#define NV507D_HEAD_SET_STORAGE_BLOCK_HEIGHT_ONE_GOB                            (0x00000000)
#define NV507D_HEAD_SET_STORAGE_BLOCK_HEIGHT_TWO_GOBS                           (0x00000001)
#define NV507D_HEAD_SET_STORAGE_BLOCK_HEIGHT_FOUR_GOBS                          (0x00000002)
#define NV507D_HEAD_SET_STORAGE_BLOCK_HEIGHT_EIGHT_GOBS                         (0x00000003)
#define NV507D_HEAD_SET_STORAGE_BLOCK_HEIGHT_SIXTEEN_GOBS                       (0x00000004)
#define NV507D_HEAD_SET_STORAGE_BLOCK_HEIGHT_THIRTYTWO_GOBS                     (0x00000005)
#define NV507D_HEAD_SET_STORAGE_PITCH                                           17:8
#define NV507D_HEAD_SET_STORAGE_MEMORY_LAYOUT                                   20:20
#define NV507D_HEAD_SET_STORAGE_MEMORY_LAYOUT_BLOCKLINEAR                       (0x00000000)
#define NV507D_HEAD_SET_STORAGE_MEMORY_LAYOUT_PITCH                             (0x00000001)
#define NV507D_HEAD_SET_PARAMS(a)                                               (0x00000870 + (a)*0x00000400)
#define NV507D_HEAD_SET_PARAMS_FORMAT                                           15:8
#define NV507D_HEAD_SET_PARAMS_FORMAT_I8                                        (0x0000001E)
#define NV507D_HEAD_SET_PARAMS_FORMAT_VOID16                                    (0x0000001F)
#define NV507D_HEAD_SET_PARAMS_FORMAT_VOID32                                    (0x0000002E)
#define NV507D_HEAD_SET_PARAMS_FORMAT_RF16_GF16_BF16_AF16                       (0x000000CA)
#define NV507D_HEAD_SET_PARAMS_FORMAT_A8R8G8B8                                  (0x000000CF)
#define NV507D_HEAD_SET_PARAMS_FORMAT_A2B10G10R10                               (0x000000D1)
#define NV507D_HEAD_SET_PARAMS_FORMAT_A8B8G8R8                                  (0x000000D5)
#define NV507D_HEAD_SET_PARAMS_FORMAT_R5G6B5                                    (0x000000E8)
#define NV507D_HEAD_SET_PARAMS_FORMAT_A1R5G5B5                                  (0x000000E9)
#define NV507D_HEAD_SET_PARAMS_KIND                                             22:16
#define NV507D_HEAD_SET_PARAMS_KIND_KIND_PITCH                                  (0x00000000)
#define NV507D_HEAD_SET_PARAMS_KIND_KIND_GENERIC_8BX2                           (0x00000070)
#define NV507D_HEAD_SET_PARAMS_KIND_KIND_GENERIC_8BX2_BANKSWIZ                  (0x00000072)
#define NV507D_HEAD_SET_PARAMS_KIND_KIND_GENERIC_16BX1                          (0x00000074)
#define NV507D_HEAD_SET_PARAMS_KIND_KIND_GENERIC_16BX1_BANKSWIZ                 (0x00000076)
#define NV507D_HEAD_SET_PARAMS_KIND_KIND_C32_MS4                                (0x00000078)
#define NV507D_HEAD_SET_PARAMS_KIND_KIND_C32_MS8                                (0x00000079)
#define NV507D_HEAD_SET_PARAMS_KIND_KIND_C32_MS4_BANKSWIZ                       (0x0000007A)
#define NV507D_HEAD_SET_PARAMS_KIND_KIND_C32_MS8_BANKSWIZ                       (0x0000007B)
#define NV507D_HEAD_SET_PARAMS_KIND_KIND_C64_MS4                                (0x0000007C)
#define NV507D_HEAD_SET_PARAMS_KIND_KIND_C64_MS8                                (0x0000007D)
#define NV507D_HEAD_SET_PARAMS_KIND_KIND_C128_MS4                               (0x0000007E)
#define NV507D_HEAD_SET_PARAMS_KIND_FROM_PTE                                    (0x0000007F)
#define NV507D_HEAD_SET_PARAMS_PART_STRIDE                                      24:24
#define NV507D_HEAD_SET_PARAMS_PART_STRIDE_PARTSTRIDE_256                       (0x00000000)
#define NV507D_HEAD_SET_PARAMS_PART_STRIDE_PARTSTRIDE_1024                      (0x00000001)
#define NV507D_HEAD_SET_CONTEXT_DMA_ISO(a)                                      (0x00000874 + (a)*0x00000400)
#define NV507D_HEAD_SET_CONTEXT_DMA_ISO_HANDLE                                  31:0
#define NV507D_HEAD_SET_CONTROL_CURSOR(a)                                       (0x00000880 + (a)*0x00000400)
#define NV507D_HEAD_SET_CONTROL_CURSOR_ENABLE                                   31:31
#define NV507D_HEAD_SET_CONTROL_CURSOR_ENABLE_DISABLE                           (0x00000000)
#define NV507D_HEAD_SET_CONTROL_CURSOR_ENABLE_ENABLE                            (0x00000001)
#define NV507D_HEAD_SET_CONTROL_CURSOR_FORMAT                                   25:24
#define NV507D_HEAD_SET_CONTROL_CURSOR_FORMAT_A1R5G5B5                          (0x00000000)
#define NV507D_HEAD_SET_CONTROL_CURSOR_FORMAT_A8R8G8B8                          (0x00000001)
#define NV507D_HEAD_SET_CONTROL_CURSOR_SIZE                                     26:26
#define NV507D_HEAD_SET_CONTROL_CURSOR_SIZE_W32_H32                             (0x00000000)
#define NV507D_HEAD_SET_CONTROL_CURSOR_SIZE_W64_H64                             (0x00000001)
#define NV507D_HEAD_SET_CONTROL_CURSOR_HOT_SPOT_X                               13:8
#define NV507D_HEAD_SET_CONTROL_CURSOR_HOT_SPOT_Y                               21:16
#define NV507D_HEAD_SET_CONTROL_CURSOR_COMPOSITION                              29:28
#define NV507D_HEAD_SET_CONTROL_CURSOR_COMPOSITION_ALPHA_BLEND                  (0x00000000)
#define NV507D_HEAD_SET_CONTROL_CURSOR_COMPOSITION_PREMULT_ALPHA_BLEND          (0x00000001)
#define NV507D_HEAD_SET_CONTROL_CURSOR_COMPOSITION_XOR                          (0x00000002)
#define NV507D_HEAD_SET_CONTROL_CURSOR_SUB_OWNER                                5:4
#define NV507D_HEAD_SET_CONTROL_CURSOR_SUB_OWNER_NONE                           (0x00000000)
#define NV507D_HEAD_SET_CONTROL_CURSOR_SUB_OWNER_SUBHEAD0                       (0x00000001)
#define NV507D_HEAD_SET_CONTROL_CURSOR_SUB_OWNER_SUBHEAD1                       (0x00000002)
#define NV507D_HEAD_SET_CONTROL_CURSOR_SUB_OWNER_BOTH                           (0x00000003)
#define NV507D_HEAD_SET_OFFSET_CURSOR(a)                                        (0x00000884 + (a)*0x00000400)
#define NV507D_HEAD_SET_OFFSET_CURSOR_ORIGIN                                    31:0
#define NV507D_HEAD_SET_DITHER_CONTROL(a)                                       (0x000008A0 + (a)*0x00000400)
#define NV507D_HEAD_SET_DITHER_CONTROL_ENABLE                                   0:0
#define NV507D_HEAD_SET_DITHER_CONTROL_ENABLE_DISABLE                           (0x00000000)
#define NV507D_HEAD_SET_DITHER_CONTROL_ENABLE_ENABLE                            (0x00000001)
#define NV507D_HEAD_SET_DITHER_CONTROL_BITS                                     2:1
#define NV507D_HEAD_SET_DITHER_CONTROL_BITS_DITHER_TO_6_BITS                    (0x00000000)
#define NV507D_HEAD_SET_DITHER_CONTROL_BITS_DITHER_TO_8_BITS                    (0x00000001)
#define NV507D_HEAD_SET_DITHER_CONTROL_MODE                                     6:3
#define NV507D_HEAD_SET_DITHER_CONTROL_MODE_DYNAMIC_ERR_ACC                     (0x00000000)
#define NV507D_HEAD_SET_DITHER_CONTROL_MODE_STATIC_ERR_ACC                      (0x00000001)
#define NV507D_HEAD_SET_DITHER_CONTROL_MODE_DYNAMIC_2X2                         (0x00000002)
#define NV507D_HEAD_SET_DITHER_CONTROL_MODE_STATIC_2X2                          (0x00000003)
#define NV507D_HEAD_SET_DITHER_CONTROL_PHASE                                    8:7
#define NV507D_HEAD_SET_CONTROL_OUTPUT_SCALER(a)                                (0x000008A4 + (a)*0x00000400)
#define NV507D_HEAD_SET_CONTROL_OUTPUT_SCALER_VERTICAL_TAPS                     2:0
#define NV507D_HEAD_SET_CONTROL_OUTPUT_SCALER_VERTICAL_TAPS_TAPS_1              (0x00000000)
#define NV507D_HEAD_SET_CONTROL_OUTPUT_SCALER_VERTICAL_TAPS_TAPS_2              (0x00000001)
#define NV507D_HEAD_SET_CONTROL_OUTPUT_SCALER_VERTICAL_TAPS_TAPS_3              (0x00000002)
#define NV507D_HEAD_SET_CONTROL_OUTPUT_SCALER_VERTICAL_TAPS_TAPS_3_ADAPTIVE     (0x00000003)
#define NV507D_HEAD_SET_CONTROL_OUTPUT_SCALER_VERTICAL_TAPS_TAPS_5              (0x00000004)
#define NV507D_HEAD_SET_CONTROL_OUTPUT_SCALER_HORIZONTAL_TAPS                   4:3
#define NV507D_HEAD_SET_CONTROL_OUTPUT_SCALER_HORIZONTAL_TAPS_TAPS_1            (0x00000000)
#define NV507D_HEAD_SET_CONTROL_OUTPUT_SCALER_HORIZONTAL_TAPS_TAPS_2            (0x00000001)
#define NV507D_HEAD_SET_CONTROL_OUTPUT_SCALER_HORIZONTAL_TAPS_TAPS_8            (0x00000002)
#define NV507D_HEAD_SET_CONTROL_OUTPUT_SCALER_HRESPONSE_BIAS                    23:16
#define NV507D_HEAD_SET_CONTROL_OUTPUT_SCALER_VRESPONSE_BIAS                    31:24
#define NV507D_HEAD_SET_PROCAMP(a)                                              (0x000008A8 + (a)*0x00000400)
#define NV507D_HEAD_SET_PROCAMP_COLOR_SPACE                                     1:0
#define NV507D_HEAD_SET_PROCAMP_COLOR_SPACE_RGB                                 (0x00000000)
#define NV507D_HEAD_SET_PROCAMP_COLOR_SPACE_YUV_601                             (0x00000001)
#define NV507D_HEAD_SET_PROCAMP_COLOR_SPACE_YUV_709                             (0x00000002)
#define NV507D_HEAD_SET_PROCAMP_CHROMA_LPF                                      2:2
#define NV507D_HEAD_SET_PROCAMP_CHROMA_LPF_AUTO                                 (0x00000000)
#define NV507D_HEAD_SET_PROCAMP_CHROMA_LPF_ON                                   (0x00000001)
#define NV507D_HEAD_SET_PROCAMP_SAT_COS                                         19:8
#define NV507D_HEAD_SET_PROCAMP_SAT_SINE                                        31:20
#define NV507D_HEAD_SET_PROCAMP_TRANSITION                                      4:3
#define NV507D_HEAD_SET_PROCAMP_TRANSITION_HARD                                 (0x00000000)
#define NV507D_HEAD_SET_PROCAMP_TRANSITION_NTSC                                 (0x00000001)
#define NV507D_HEAD_SET_PROCAMP_TRANSITION_PAL                                  (0x00000002)
#define NV507D_HEAD_SET_VIEWPORT_POINT_IN(a,b)                                  (0x000008C0 + (a)*0x00000400 + (b)*0x00000004)
#define NV507D_HEAD_SET_VIEWPORT_POINT_IN_X                                     14:0
#define NV507D_HEAD_SET_VIEWPORT_POINT_IN_Y                                     30:16
#define NV507D_HEAD_SET_VIEWPORT_SIZE_IN(a)                                     (0x000008C8 + (a)*0x00000400)
#define NV507D_HEAD_SET_VIEWPORT_SIZE_IN_WIDTH                                  14:0
#define NV507D_HEAD_SET_VIEWPORT_SIZE_IN_HEIGHT                                 30:16
#define NV507D_HEAD_SET_VIEWPORT_SIZE_OUT(a)                                    (0x000008D8 + (a)*0x00000400)
#define NV507D_HEAD_SET_VIEWPORT_SIZE_OUT_WIDTH                                 14:0
#define NV507D_HEAD_SET_VIEWPORT_SIZE_OUT_HEIGHT                                30:16
#define NV507D_HEAD_SET_VIEWPORT_SIZE_OUT_MIN(a)                                (0x000008DC + (a)*0x00000400)
#define NV507D_HEAD_SET_VIEWPORT_SIZE_OUT_MIN_WIDTH                             14:0
#define NV507D_HEAD_SET_VIEWPORT_SIZE_OUT_MIN_HEIGHT                            30:16
#define NV507D_HEAD_SET_BASE_CHANNEL_USAGE_BOUNDS(a)                            (0x00000900 + (a)*0x00000400)
#define NV507D_HEAD_SET_BASE_CHANNEL_USAGE_BOUNDS_USABLE                        0:0
#define NV507D_HEAD_SET_BASE_CHANNEL_USAGE_BOUNDS_USABLE_FALSE                  (0x00000000)
#define NV507D_HEAD_SET_BASE_CHANNEL_USAGE_BOUNDS_USABLE_TRUE                   (0x00000001)
#define NV507D_HEAD_SET_BASE_CHANNEL_USAGE_BOUNDS_PIXEL_DEPTH                   11:8
#define NV507D_HEAD_SET_BASE_CHANNEL_USAGE_BOUNDS_PIXEL_DEPTH_BPP_8             (0x00000000)
#define NV507D_HEAD_SET_BASE_CHANNEL_USAGE_BOUNDS_PIXEL_DEPTH_BPP_16            (0x00000001)
#define NV507D_HEAD_SET_BASE_CHANNEL_USAGE_BOUNDS_PIXEL_DEPTH_BPP_32            (0x00000003)
#define NV507D_HEAD_SET_BASE_CHANNEL_USAGE_BOUNDS_PIXEL_DEPTH_BPP_64            (0x00000005)
#define NV507D_HEAD_SET_BASE_CHANNEL_USAGE_BOUNDS_SUPER_SAMPLE                  13:12
#define NV507D_HEAD_SET_BASE_CHANNEL_USAGE_BOUNDS_SUPER_SAMPLE_X1_AA            (0x00000000)
#define NV507D_HEAD_SET_BASE_CHANNEL_USAGE_BOUNDS_SUPER_SAMPLE_X4_AA            (0x00000002)
#define NV507D_HEAD_SET_OVERLAY_USAGE_BOUNDS(a)                                 (0x00000904 + (a)*0x00000400)
#define NV507D_HEAD_SET_OVERLAY_USAGE_BOUNDS_USABLE                             0:0
#define NV507D_HEAD_SET_OVERLAY_USAGE_BOUNDS_USABLE_FALSE                       (0x00000000)
#define NV507D_HEAD_SET_OVERLAY_USAGE_BOUNDS_USABLE_TRUE                        (0x00000001)
#define NV507D_HEAD_SET_OVERLAY_USAGE_BOUNDS_PIXEL_DEPTH                        11:8
#define NV507D_HEAD_SET_OVERLAY_USAGE_BOUNDS_PIXEL_DEPTH_BPP_16                 (0x00000001)
#define NV507D_HEAD_SET_OVERLAY_USAGE_BOUNDS_PIXEL_DEPTH_BPP_32                 (0x00000003)
#endif // _cl507d_h
