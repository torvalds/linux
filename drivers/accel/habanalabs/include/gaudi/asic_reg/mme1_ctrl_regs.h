/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2016-2018 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

/************************************
 ** This is an auto-generated file **
 **       DO NOT EDIT BELOW        **
 ************************************/

#ifndef ASIC_REG_MME1_CTRL_REGS_H_
#define ASIC_REG_MME1_CTRL_REGS_H_

/*
 *****************************************
 *   MME1_CTRL (Prototype: MME)
 *****************************************
 */

#define mmMME1_CTRL_ARCH_STATUS                                      0xE0000

#define mmMME1_CTRL_ARCH_BASE_ADDR_HIGH_S                            0xE0008

#define mmMME1_CTRL_ARCH_BASE_ADDR_HIGH_L                            0xE000C

#define mmMME1_CTRL_ARCH_BASE_ADDR_HIGH_O                            0xE0010

#define mmMME1_CTRL_ARCH_BASE_ADDR_LOW_S                             0xE0014

#define mmMME1_CTRL_ARCH_BASE_ADDR_LOW_L                             0xE0018

#define mmMME1_CTRL_ARCH_BASE_ADDR_LOW_O                             0xE001C

#define mmMME1_CTRL_ARCH_HEADER_LOW                                  0xE0020

#define mmMME1_CTRL_ARCH_HEADER_HIGH                                 0xE0024

#define mmMME1_CTRL_ARCH_CONV_KERNEL_SIZE_MINUS_1                    0xE0028

#define mmMME1_CTRL_ARCH_CONV_ASSOCIATED_DIMS_LOW                    0xE002C

#define mmMME1_CTRL_ARCH_CONV_ASSOCIATED_DIMS_HIGH                   0xE0030

#define mmMME1_CTRL_ARCH_NUM_ITERATIONS_MINUS_1                      0xE0034

#define mmMME1_CTRL_ARCH_OUTER_LOOP                                  0xE0038

#define mmMME1_CTRL_ARCH_TENSOR_S_VALID_ELEMENTS_0                   0xE003C

#define mmMME1_CTRL_ARCH_TENSOR_S_VALID_ELEMENTS_1                   0xE0040

#define mmMME1_CTRL_ARCH_TENSOR_S_VALID_ELEMENTS_2                   0xE0044

#define mmMME1_CTRL_ARCH_TENSOR_S_VALID_ELEMENTS_3                   0xE0048

#define mmMME1_CTRL_ARCH_TENSOR_S_VALID_ELEMENTS_4                   0xE004C

#define mmMME1_CTRL_ARCH_TENSOR_S_LOOP_STRIDE_0                      0xE0050

#define mmMME1_CTRL_ARCH_TENSOR_S_LOOP_STRIDE_1                      0xE0054

#define mmMME1_CTRL_ARCH_TENSOR_S_LOOP_STRIDE_2                      0xE0058

#define mmMME1_CTRL_ARCH_TENSOR_S_LOOP_STRIDE_3                      0xE005C

#define mmMME1_CTRL_ARCH_TENSOR_S_LOOP_STRIDE_4                      0xE0060

#define mmMME1_CTRL_ARCH_TENSOR_S_ROI_SIZE_0                         0xE0064

#define mmMME1_CTRL_ARCH_TENSOR_S_ROI_SIZE_1                         0xE0068

#define mmMME1_CTRL_ARCH_TENSOR_S_ROI_SIZE_2                         0xE006C

#define mmMME1_CTRL_ARCH_TENSOR_S_ROI_SIZE_3                         0xE0070

#define mmMME1_CTRL_ARCH_TENSOR_S_SPATIAL_STRIDES_0                  0xE0074

#define mmMME1_CTRL_ARCH_TENSOR_S_SPATIAL_STRIDES_1                  0xE0078

#define mmMME1_CTRL_ARCH_TENSOR_S_SPATIAL_STRIDES_2                  0xE007C

#define mmMME1_CTRL_ARCH_TENSOR_S_SPATIAL_STRIDES_3                  0xE0080

#define mmMME1_CTRL_ARCH_TENSOR_S_SPATIAL_SIZE_MINUS_1               0xE0084

#define mmMME1_CTRL_ARCH_AGU_S_ROI_BASE_OFFSET_0                     0xE0088

#define mmMME1_CTRL_ARCH_AGU_S_ROI_BASE_OFFSET_1                     0xE008C

#define mmMME1_CTRL_ARCH_AGU_S_ROI_BASE_OFFSET_2                     0xE0090

#define mmMME1_CTRL_ARCH_AGU_S_ROI_BASE_OFFSET_3                     0xE0094

#define mmMME1_CTRL_ARCH_AGU_S_ROI_BASE_OFFSET_4                     0xE0098

#define mmMME1_CTRL_ARCH_AGU_S_START_OFFSET_0                        0xE009C

#define mmMME1_CTRL_ARCH_AGU_S_START_OFFSET_1                        0xE00A0

#define mmMME1_CTRL_ARCH_AGU_S_START_OFFSET_2                        0xE00A4

#define mmMME1_CTRL_ARCH_AGU_S_START_OFFSET_3                        0xE00A8

#define mmMME1_CTRL_ARCH_TENSOR_L_VALID_ELEMENTS_0                   0xE00AC

#define mmMME1_CTRL_ARCH_TENSOR_L_VALID_ELEMENTS_1                   0xE00B0

#define mmMME1_CTRL_ARCH_TENSOR_L_VALID_ELEMENTS_2                   0xE00B4

#define mmMME1_CTRL_ARCH_TENSOR_L_VALID_ELEMENTS_3                   0xE00B8

#define mmMME1_CTRL_ARCH_TENSOR_L_VALID_ELEMENTS_4                   0xE00BC

#define mmMME1_CTRL_ARCH_TENSOR_L_LOOP_STRIDE_0                      0xE00C0

#define mmMME1_CTRL_ARCH_TENSOR_L_LOOP_STRIDE_1                      0xE00C4

#define mmMME1_CTRL_ARCH_TENSOR_L_LOOP_STRIDE_2                      0xE00C8

#define mmMME1_CTRL_ARCH_TENSOR_L_LOOP_STRIDE_3                      0xE00CC

#define mmMME1_CTRL_ARCH_TENSOR_L_LOOP_STRIDE_4                      0xE00D0

#define mmMME1_CTRL_ARCH_TENSOR_L_ROI_SIZE_0                         0xE00D4

#define mmMME1_CTRL_ARCH_TENSOR_L_ROI_SIZE_1                         0xE00D8

#define mmMME1_CTRL_ARCH_TENSOR_L_ROI_SIZE_2                         0xE00DC

#define mmMME1_CTRL_ARCH_TENSOR_L_ROI_SIZE_3                         0xE00E0

#define mmMME1_CTRL_ARCH_TENSOR_L_SPATIAL_STRIDES_0                  0xE00E4

#define mmMME1_CTRL_ARCH_TENSOR_L_SPATIAL_STRIDES_1                  0xE00E8

#define mmMME1_CTRL_ARCH_TENSOR_L_SPATIAL_STRIDES_2                  0xE00EC

#define mmMME1_CTRL_ARCH_TENSOR_L_SPATIAL_STRIDES_3                  0xE00F0

#define mmMME1_CTRL_ARCH_TENSOR_L_SPATIAL_SIZE_MINUS_1               0xE00F4

#define mmMME1_CTRL_ARCH_AGU_L_LOCAL_ROI_BASE_OFFSET_0               0xE00F8

#define mmMME1_CTRL_ARCH_AGU_L_LOCAL_ROI_BASE_OFFSET_1               0xE00FC

#define mmMME1_CTRL_ARCH_AGU_L_LOCAL_ROI_BASE_OFFSET_2               0xE0100

#define mmMME1_CTRL_ARCH_AGU_L_LOCAL_ROI_BASE_OFFSET_3               0xE0104

#define mmMME1_CTRL_ARCH_AGU_L_LOCAL_ROI_BASE_OFFSET_4               0xE0108

#define mmMME1_CTRL_ARCH_AGU_L_LOCAL_START_OFFSET_0                  0xE010C

#define mmMME1_CTRL_ARCH_AGU_L_LOCAL_START_OFFSET_1                  0xE0110

#define mmMME1_CTRL_ARCH_AGU_L_LOCAL_START_OFFSET_2                  0xE0114

#define mmMME1_CTRL_ARCH_AGU_L_LOCAL_START_OFFSET_3                  0xE0118

#define mmMME1_CTRL_ARCH_AGU_L_REMOTE_ROI_BASE_OFFSET_0              0xE011C

#define mmMME1_CTRL_ARCH_AGU_L_REMOTE_ROI_BASE_OFFSET_1              0xE0120

#define mmMME1_CTRL_ARCH_AGU_L_REMOTE_ROI_BASE_OFFSET_2              0xE0124

#define mmMME1_CTRL_ARCH_AGU_L_REMOTE_ROI_BASE_OFFSET_3              0xE0128

#define mmMME1_CTRL_ARCH_AGU_L_REMOTE_ROI_BASE_OFFSET_4              0xE012C

#define mmMME1_CTRL_ARCH_AGU_L_REMOTE_START_OFFSET_0                 0xE0130

#define mmMME1_CTRL_ARCH_AGU_L_REMOTE_START_OFFSET_1                 0xE0134

#define mmMME1_CTRL_ARCH_AGU_L_REMOTE_START_OFFSET_2                 0xE0138

#define mmMME1_CTRL_ARCH_AGU_L_REMOTE_START_OFFSET_3                 0xE013C

#define mmMME1_CTRL_ARCH_TENSOR_O_VALID_ELEMENTS_0                   0xE0140

#define mmMME1_CTRL_ARCH_TENSOR_O_VALID_ELEMENTS_1                   0xE0144

#define mmMME1_CTRL_ARCH_TENSOR_O_VALID_ELEMENTS_2                   0xE0148

#define mmMME1_CTRL_ARCH_TENSOR_O_VALID_ELEMENTS_3                   0xE014C

#define mmMME1_CTRL_ARCH_TENSOR_O_VALID_ELEMENTS_4                   0xE0150

#define mmMME1_CTRL_ARCH_TENSOR_O_LOOP_STRIDE_0                      0xE0154

#define mmMME1_CTRL_ARCH_TENSOR_O_LOOP_STRIDE_1                      0xE0158

#define mmMME1_CTRL_ARCH_TENSOR_O_LOOP_STRIDE_2                      0xE015C

#define mmMME1_CTRL_ARCH_TENSOR_O_LOOP_STRIDE_3                      0xE0160

#define mmMME1_CTRL_ARCH_TENSOR_O_LOOP_STRIDE_4                      0xE0164

#define mmMME1_CTRL_ARCH_TENSOR_O_ROI_SIZE_0                         0xE0168

#define mmMME1_CTRL_ARCH_TENSOR_O_ROI_SIZE_1                         0xE016C

#define mmMME1_CTRL_ARCH_TENSOR_O_ROI_SIZE_2                         0xE0170

#define mmMME1_CTRL_ARCH_TENSOR_O_ROI_SIZE_3                         0xE0174

#define mmMME1_CTRL_ARCH_TENSOR_O_SPATIAL_STRIDES_0                  0xE0178

#define mmMME1_CTRL_ARCH_TENSOR_O_SPATIAL_STRIDES_1                  0xE017C

#define mmMME1_CTRL_ARCH_TENSOR_O_SPATIAL_STRIDES_2                  0xE0180

#define mmMME1_CTRL_ARCH_TENSOR_O_SPATIAL_STRIDES_3                  0xE0184

#define mmMME1_CTRL_ARCH_TENSOR_O_SPATIAL_SIZE_MINUS_1               0xE0188

#define mmMME1_CTRL_ARCH_AGU_O_LOCAL_ROI_BASE_OFFSET_0               0xE018C

#define mmMME1_CTRL_ARCH_AGU_O_LOCAL_ROI_BASE_OFFSET_1               0xE0190

#define mmMME1_CTRL_ARCH_AGU_O_LOCAL_ROI_BASE_OFFSET_2               0xE0194

#define mmMME1_CTRL_ARCH_AGU_O_LOCAL_ROI_BASE_OFFSET_3               0xE0198

#define mmMME1_CTRL_ARCH_AGU_O_LOCAL_ROI_BASE_OFFSET_4               0xE019C

#define mmMME1_CTRL_ARCH_AGU_O_LOCAL_START_OFFSET_0                  0xE01A0

#define mmMME1_CTRL_ARCH_AGU_O_LOCAL_START_OFFSET_1                  0xE01A4

#define mmMME1_CTRL_ARCH_AGU_O_LOCAL_START_OFFSET_2                  0xE01A8

#define mmMME1_CTRL_ARCH_AGU_O_LOCAL_START_OFFSET_3                  0xE01AC

#define mmMME1_CTRL_ARCH_AGU_O_REMOTE_ROI_BASE_OFFSET_0              0xE01B0

#define mmMME1_CTRL_ARCH_AGU_O_REMOTE_ROI_BASE_OFFSET_1              0xE01B4

#define mmMME1_CTRL_ARCH_AGU_O_REMOTE_ROI_BASE_OFFSET_2              0xE01B8

#define mmMME1_CTRL_ARCH_AGU_O_REMOTE_ROI_BASE_OFFSET_3              0xE01BC

#define mmMME1_CTRL_ARCH_AGU_O_REMOTE_ROI_BASE_OFFSET_4              0xE01C0

#define mmMME1_CTRL_ARCH_AGU_O_REMOTE_START_OFFSET_0                 0xE01C4

#define mmMME1_CTRL_ARCH_AGU_O_REMOTE_START_OFFSET_1                 0xE01C8

#define mmMME1_CTRL_ARCH_AGU_O_REMOTE_START_OFFSET_2                 0xE01CC

#define mmMME1_CTRL_ARCH_AGU_O_REMOTE_START_OFFSET_3                 0xE01D0

#define mmMME1_CTRL_ARCH_DESC_SB_REPEAT                              0xE01D4

#define mmMME1_CTRL_ARCH_DESC_RATE_LIMITER                           0xE01D8

#define mmMME1_CTRL_ARCH_DESC_SYNC_OBJECT_ADDR_LOW_LOCAL             0xE01DC

#define mmMME1_CTRL_ARCH_DESC_SYNC_OBJECT_ADDR_LOW_REMOTE            0xE01E0

#define mmMME1_CTRL_ARCH_DESC_SYNC_OBJECT_ADDR_HIGH                  0xE01E4

#define mmMME1_CTRL_ARCH_DESC_SYNC_OBJECT_DATA                       0xE01E8

#define mmMME1_CTRL_ARCH_DESC_AXI_USER_DATA                          0xE01EC

#define mmMME1_CTRL_ARCH_DESC_PERF_EVT_S                             0xE01F0

#define mmMME1_CTRL_ARCH_DESC_PERF_EVT_L_LOCAL                       0xE01F4

#define mmMME1_CTRL_ARCH_DESC_PERF_EVT_L_REMOTE                      0xE01F8

#define mmMME1_CTRL_ARCH_DESC_PERF_EVT_O_LOCAL                       0xE01FC

#define mmMME1_CTRL_ARCH_DESC_PERF_EVT_O_REMOTE                      0xE0200

#define mmMME1_CTRL_ARCH_DESC_PADDING_VALUE_S                        0xE0204

#define mmMME1_CTRL_ARCH_DESC_PADDING_VALUE_L                        0xE0208

#define mmMME1_CTRL_ARCH_DESC_META_DATA_AGU_S                        0xE020C

#define mmMME1_CTRL_ARCH_DESC_META_DATA_AGU_L_LOCAL                  0xE0210

#define mmMME1_CTRL_ARCH_DESC_META_DATA_AGU_L_REMOTE                 0xE0214

#define mmMME1_CTRL_ARCH_DESC_META_DATA_AGU_O_LOCAL                  0xE0218

#define mmMME1_CTRL_ARCH_DESC_META_DATA_AGU_O_REMOTE                 0xE021C

#define mmMME1_CTRL_ARCH_DESC_PCU_RL_SATURATION                      0xE0220

#define mmMME1_CTRL_ARCH_DESC_DUMMY                                  0xE0224

#define mmMME1_CTRL_CMD                                              0xE0280

#define mmMME1_CTRL_STATUS1                                          0xE0284

#define mmMME1_CTRL_RESET                                            0xE0288

#define mmMME1_CTRL_QM_STALL                                         0xE028C

#define mmMME1_CTRL_SYNC_OBJECT_FIFO_TH                              0xE0290

#define mmMME1_CTRL_EUS_ROLLUP_CNT_ADD                               0xE0294

#define mmMME1_CTRL_INTR_CAUSE                                       0xE0298

#define mmMME1_CTRL_INTR_MASK                                        0xE029C

#define mmMME1_CTRL_LOG_SHADOW                                       0xE02A0

#define mmMME1_CTRL_PCU_RL_DESC0                                     0xE02A4

#define mmMME1_CTRL_PCU_RL_TOKEN_UPDATE                              0xE02A8

#define mmMME1_CTRL_PCU_RL_TH                                        0xE02AC

#define mmMME1_CTRL_PCU_RL_MIN                                       0xE02B0

#define mmMME1_CTRL_PCU_RL_CTRL_EN                                   0xE02B4

#define mmMME1_CTRL_PCU_RL_HISTORY_LOG_SIZE                          0xE02B8

#define mmMME1_CTRL_PCU_DUMMY_A_BF16                                 0xE02BC

#define mmMME1_CTRL_PCU_DUMMY_B_BF16                                 0xE02C0

#define mmMME1_CTRL_PCU_DUMMY_A_FP32_ODD                             0xE02C4

#define mmMME1_CTRL_PCU_DUMMY_A_FP32_EVEN                            0xE02C8

#define mmMME1_CTRL_PCU_DUMMY_B_FP32_ODD                             0xE02CC

#define mmMME1_CTRL_PCU_DUMMY_B_FP32_EVEN                            0xE02D0

#define mmMME1_CTRL_PROT                                             0xE02D4

#define mmMME1_CTRL_EU_POWER_SAVE_DISABLE                            0xE02D8

#define mmMME1_CTRL_CS_DBG_BLOCK_ID                                  0xE02DC

#define mmMME1_CTRL_CS_DBG_STATUS_DROP_CNT                           0xE02E0

#define mmMME1_CTRL_TE_CLOSE_CGATE                                   0xE02E4

#define mmMME1_CTRL_AGU_SM_INFLIGHT_CNTR                             0xE02E8

#define mmMME1_CTRL_AGU_SM_TOTAL_CNTR                                0xE02EC

#define mmMME1_CTRL_EZSYNC_OUT_CREDIT                                0xE02F0

#define mmMME1_CTRL_PCU_RL_SAT_SEC                                   0xE02F4

#define mmMME1_CTRL_AGU_SYNC_MSG_AXI_USER                            0xE02F8

#define mmMME1_CTRL_QM_SLV_LBW_CLK_EN                                0xE02FC

#define mmMME1_CTRL_SHADOW_0_STATUS                                  0xE0400

#define mmMME1_CTRL_SHADOW_0_BASE_ADDR_HIGH_S                        0xE0408

#define mmMME1_CTRL_SHADOW_0_BASE_ADDR_HIGH_L                        0xE040C

#define mmMME1_CTRL_SHADOW_0_BASE_ADDR_HIGH_O                        0xE0410

#define mmMME1_CTRL_SHADOW_0_BASE_ADDR_LOW_S                         0xE0414

#define mmMME1_CTRL_SHADOW_0_BASE_ADDR_LOW_L                         0xE0418

#define mmMME1_CTRL_SHADOW_0_BASE_ADDR_LOW_O                         0xE041C

#define mmMME1_CTRL_SHADOW_0_HEADER_LOW                              0xE0420

#define mmMME1_CTRL_SHADOW_0_HEADER_HIGH                             0xE0424

#define mmMME1_CTRL_SHADOW_0_CONV_KERNEL_SIZE_MINUS_1                0xE0428

#define mmMME1_CTRL_SHADOW_0_CONV_ASSOCIATED_DIMS_LOW                0xE042C

#define mmMME1_CTRL_SHADOW_0_CONV_ASSOCIATED_DIMS_HIGH               0xE0430

#define mmMME1_CTRL_SHADOW_0_NUM_ITERATIONS_MINUS_1                  0xE0434

#define mmMME1_CTRL_SHADOW_0_OUTER_LOOP                              0xE0438

#define mmMME1_CTRL_SHADOW_0_TENSOR_S_VALID_ELEMENTS_0               0xE043C

#define mmMME1_CTRL_SHADOW_0_TENSOR_S_VALID_ELEMENTS_1               0xE0440

#define mmMME1_CTRL_SHADOW_0_TENSOR_S_VALID_ELEMENTS_2               0xE0444

#define mmMME1_CTRL_SHADOW_0_TENSOR_S_VALID_ELEMENTS_3               0xE0448

#define mmMME1_CTRL_SHADOW_0_TENSOR_S_VALID_ELEMENTS_4               0xE044C

#define mmMME1_CTRL_SHADOW_0_TENSOR_S_LOOP_STRIDE_0                  0xE0450

#define mmMME1_CTRL_SHADOW_0_TENSOR_S_LOOP_STRIDE_1                  0xE0454

#define mmMME1_CTRL_SHADOW_0_TENSOR_S_LOOP_STRIDE_2                  0xE0458

#define mmMME1_CTRL_SHADOW_0_TENSOR_S_LOOP_STRIDE_3                  0xE045C

#define mmMME1_CTRL_SHADOW_0_TENSOR_S_LOOP_STRIDE_4                  0xE0460

#define mmMME1_CTRL_SHADOW_0_TENSOR_S_ROI_SIZE_0                     0xE0464

#define mmMME1_CTRL_SHADOW_0_TENSOR_S_ROI_SIZE_1                     0xE0468

#define mmMME1_CTRL_SHADOW_0_TENSOR_S_ROI_SIZE_2                     0xE046C

#define mmMME1_CTRL_SHADOW_0_TENSOR_S_ROI_SIZE_3                     0xE0470

#define mmMME1_CTRL_SHADOW_0_TENSOR_S_SPATIAL_STRIDES_0              0xE0474

#define mmMME1_CTRL_SHADOW_0_TENSOR_S_SPATIAL_STRIDES_1              0xE0478

#define mmMME1_CTRL_SHADOW_0_TENSOR_S_SPATIAL_STRIDES_2              0xE047C

#define mmMME1_CTRL_SHADOW_0_TENSOR_S_SPATIAL_STRIDES_3              0xE0480

#define mmMME1_CTRL_SHADOW_0_TENSOR_S_SPATIAL_SIZE_MINUS_1           0xE0484

#define mmMME1_CTRL_SHADOW_0_AGU_S_ROI_BASE_OFFSET_0                 0xE0488

#define mmMME1_CTRL_SHADOW_0_AGU_S_ROI_BASE_OFFSET_1                 0xE048C

#define mmMME1_CTRL_SHADOW_0_AGU_S_ROI_BASE_OFFSET_2                 0xE0490

#define mmMME1_CTRL_SHADOW_0_AGU_S_ROI_BASE_OFFSET_3                 0xE0494

#define mmMME1_CTRL_SHADOW_0_AGU_S_ROI_BASE_OFFSET_4                 0xE0498

#define mmMME1_CTRL_SHADOW_0_AGU_S_START_OFFSET_0                    0xE049C

#define mmMME1_CTRL_SHADOW_0_AGU_S_START_OFFSET_1                    0xE04A0

#define mmMME1_CTRL_SHADOW_0_AGU_S_START_OFFSET_2                    0xE04A4

#define mmMME1_CTRL_SHADOW_0_AGU_S_START_OFFSET_3                    0xE04A8

#define mmMME1_CTRL_SHADOW_0_TENSOR_L_VALID_ELEMENTS_0               0xE04AC

#define mmMME1_CTRL_SHADOW_0_TENSOR_L_VALID_ELEMENTS_1               0xE04B0

#define mmMME1_CTRL_SHADOW_0_TENSOR_L_VALID_ELEMENTS_2               0xE04B4

#define mmMME1_CTRL_SHADOW_0_TENSOR_L_VALID_ELEMENTS_3               0xE04B8

#define mmMME1_CTRL_SHADOW_0_TENSOR_L_VALID_ELEMENTS_4               0xE04BC

#define mmMME1_CTRL_SHADOW_0_TENSOR_L_LOOP_STRIDE_0                  0xE04C0

#define mmMME1_CTRL_SHADOW_0_TENSOR_L_LOOP_STRIDE_1                  0xE04C4

#define mmMME1_CTRL_SHADOW_0_TENSOR_L_LOOP_STRIDE_2                  0xE04C8

#define mmMME1_CTRL_SHADOW_0_TENSOR_L_LOOP_STRIDE_3                  0xE04CC

#define mmMME1_CTRL_SHADOW_0_TENSOR_L_LOOP_STRIDE_4                  0xE04D0

#define mmMME1_CTRL_SHADOW_0_TENSOR_L_ROI_SIZE_0                     0xE04D4

#define mmMME1_CTRL_SHADOW_0_TENSOR_L_ROI_SIZE_1                     0xE04D8

#define mmMME1_CTRL_SHADOW_0_TENSOR_L_ROI_SIZE_2                     0xE04DC

#define mmMME1_CTRL_SHADOW_0_TENSOR_L_ROI_SIZE_3                     0xE04E0

#define mmMME1_CTRL_SHADOW_0_TENSOR_L_SPATIAL_STRIDES_0              0xE04E4

#define mmMME1_CTRL_SHADOW_0_TENSOR_L_SPATIAL_STRIDES_1              0xE04E8

#define mmMME1_CTRL_SHADOW_0_TENSOR_L_SPATIAL_STRIDES_2              0xE04EC

#define mmMME1_CTRL_SHADOW_0_TENSOR_L_SPATIAL_STRIDES_3              0xE04F0

#define mmMME1_CTRL_SHADOW_0_TENSOR_L_SPATIAL_SIZE_MINUS_1           0xE04F4

#define mmMME1_CTRL_SHADOW_0_AGU_L_LOCAL_ROI_BASE_OFFSET_0           0xE04F8

#define mmMME1_CTRL_SHADOW_0_AGU_L_LOCAL_ROI_BASE_OFFSET_1           0xE04FC

#define mmMME1_CTRL_SHADOW_0_AGU_L_LOCAL_ROI_BASE_OFFSET_2           0xE0500

#define mmMME1_CTRL_SHADOW_0_AGU_L_LOCAL_ROI_BASE_OFFSET_3           0xE0504

#define mmMME1_CTRL_SHADOW_0_AGU_L_LOCAL_ROI_BASE_OFFSET_4           0xE0508

#define mmMME1_CTRL_SHADOW_0_AGU_L_LOCAL_START_OFFSET_0              0xE050C

#define mmMME1_CTRL_SHADOW_0_AGU_L_LOCAL_START_OFFSET_1              0xE0510

#define mmMME1_CTRL_SHADOW_0_AGU_L_LOCAL_START_OFFSET_2              0xE0514

#define mmMME1_CTRL_SHADOW_0_AGU_L_LOCAL_START_OFFSET_3              0xE0518

#define mmMME1_CTRL_SHADOW_0_AGU_L_REMOTE_ROI_BASE_OFFSET_0          0xE051C

#define mmMME1_CTRL_SHADOW_0_AGU_L_REMOTE_ROI_BASE_OFFSET_1          0xE0520

#define mmMME1_CTRL_SHADOW_0_AGU_L_REMOTE_ROI_BASE_OFFSET_2          0xE0524

#define mmMME1_CTRL_SHADOW_0_AGU_L_REMOTE_ROI_BASE_OFFSET_3          0xE0528

#define mmMME1_CTRL_SHADOW_0_AGU_L_REMOTE_ROI_BASE_OFFSET_4          0xE052C

#define mmMME1_CTRL_SHADOW_0_AGU_L_REMOTE_START_OFFSET_0             0xE0530

#define mmMME1_CTRL_SHADOW_0_AGU_L_REMOTE_START_OFFSET_1             0xE0534

#define mmMME1_CTRL_SHADOW_0_AGU_L_REMOTE_START_OFFSET_2             0xE0538

#define mmMME1_CTRL_SHADOW_0_AGU_L_REMOTE_START_OFFSET_3             0xE053C

#define mmMME1_CTRL_SHADOW_0_TENSOR_O_VALID_ELEMENTS_0               0xE0540

#define mmMME1_CTRL_SHADOW_0_TENSOR_O_VALID_ELEMENTS_1               0xE0544

#define mmMME1_CTRL_SHADOW_0_TENSOR_O_VALID_ELEMENTS_2               0xE0548

#define mmMME1_CTRL_SHADOW_0_TENSOR_O_VALID_ELEMENTS_3               0xE054C

#define mmMME1_CTRL_SHADOW_0_TENSOR_O_VALID_ELEMENTS_4               0xE0550

#define mmMME1_CTRL_SHADOW_0_TENSOR_O_LOOP_STRIDE_0                  0xE0554

#define mmMME1_CTRL_SHADOW_0_TENSOR_O_LOOP_STRIDE_1                  0xE0558

#define mmMME1_CTRL_SHADOW_0_TENSOR_O_LOOP_STRIDE_2                  0xE055C

#define mmMME1_CTRL_SHADOW_0_TENSOR_O_LOOP_STRIDE_3                  0xE0560

#define mmMME1_CTRL_SHADOW_0_TENSOR_O_LOOP_STRIDE_4                  0xE0564

#define mmMME1_CTRL_SHADOW_0_TENSOR_O_ROI_SIZE_0                     0xE0568

#define mmMME1_CTRL_SHADOW_0_TENSOR_O_ROI_SIZE_1                     0xE056C

#define mmMME1_CTRL_SHADOW_0_TENSOR_O_ROI_SIZE_2                     0xE0570

#define mmMME1_CTRL_SHADOW_0_TENSOR_O_ROI_SIZE_3                     0xE0574

#define mmMME1_CTRL_SHADOW_0_TENSOR_O_SPATIAL_STRIDES_0              0xE0578

#define mmMME1_CTRL_SHADOW_0_TENSOR_O_SPATIAL_STRIDES_1              0xE057C

#define mmMME1_CTRL_SHADOW_0_TENSOR_O_SPATIAL_STRIDES_2              0xE0580

#define mmMME1_CTRL_SHADOW_0_TENSOR_O_SPATIAL_STRIDES_3              0xE0584

#define mmMME1_CTRL_SHADOW_0_TENSOR_O_SPATIAL_SIZE_MINUS_1           0xE0588

#define mmMME1_CTRL_SHADOW_0_AGU_O_LOCAL_ROI_BASE_OFFSET_0           0xE058C

#define mmMME1_CTRL_SHADOW_0_AGU_O_LOCAL_ROI_BASE_OFFSET_1           0xE0590

#define mmMME1_CTRL_SHADOW_0_AGU_O_LOCAL_ROI_BASE_OFFSET_2           0xE0594

#define mmMME1_CTRL_SHADOW_0_AGU_O_LOCAL_ROI_BASE_OFFSET_3           0xE0598

#define mmMME1_CTRL_SHADOW_0_AGU_O_LOCAL_ROI_BASE_OFFSET_4           0xE059C

#define mmMME1_CTRL_SHADOW_0_AGU_O_LOCAL_START_OFFSET_0              0xE05A0

#define mmMME1_CTRL_SHADOW_0_AGU_O_LOCAL_START_OFFSET_1              0xE05A4

#define mmMME1_CTRL_SHADOW_0_AGU_O_LOCAL_START_OFFSET_2              0xE05A8

#define mmMME1_CTRL_SHADOW_0_AGU_O_LOCAL_START_OFFSET_3              0xE05AC

#define mmMME1_CTRL_SHADOW_0_AGU_O_REMOTE_ROI_BASE_OFFSET_0          0xE05B0

#define mmMME1_CTRL_SHADOW_0_AGU_O_REMOTE_ROI_BASE_OFFSET_1          0xE05B4

#define mmMME1_CTRL_SHADOW_0_AGU_O_REMOTE_ROI_BASE_OFFSET_2          0xE05B8

#define mmMME1_CTRL_SHADOW_0_AGU_O_REMOTE_ROI_BASE_OFFSET_3          0xE05BC

#define mmMME1_CTRL_SHADOW_0_AGU_O_REMOTE_ROI_BASE_OFFSET_4          0xE05C0

#define mmMME1_CTRL_SHADOW_0_AGU_O_REMOTE_START_OFFSET_0             0xE05C4

#define mmMME1_CTRL_SHADOW_0_AGU_O_REMOTE_START_OFFSET_1             0xE05C8

#define mmMME1_CTRL_SHADOW_0_AGU_O_REMOTE_START_OFFSET_2             0xE05CC

#define mmMME1_CTRL_SHADOW_0_AGU_O_REMOTE_START_OFFSET_3             0xE05D0

#define mmMME1_CTRL_SHADOW_0_DESC_SB_REPEAT                          0xE05D4

#define mmMME1_CTRL_SHADOW_0_DESC_RATE_LIMITER                       0xE05D8

#define mmMME1_CTRL_SHADOW_0_DESC_SYNC_OBJECT_ADDR_LOW_LOCAL         0xE05DC

#define mmMME1_CTRL_SHADOW_0_DESC_SYNC_OBJECT_ADDR_LOW_REMOTE        0xE05E0

#define mmMME1_CTRL_SHADOW_0_DESC_SYNC_OBJECT_ADDR_HIGH              0xE05E4

#define mmMME1_CTRL_SHADOW_0_DESC_SYNC_OBJECT_DATA                   0xE05E8

#define mmMME1_CTRL_SHADOW_0_DESC_AXI_USER_DATA                      0xE05EC

#define mmMME1_CTRL_SHADOW_0_DESC_PERF_EVT_S                         0xE05F0

#define mmMME1_CTRL_SHADOW_0_DESC_PERF_EVT_L_LOCAL                   0xE05F4

#define mmMME1_CTRL_SHADOW_0_DESC_PERF_EVT_L_REMOTE                  0xE05F8

#define mmMME1_CTRL_SHADOW_0_DESC_PERF_EVT_O_LOCAL                   0xE05FC

#define mmMME1_CTRL_SHADOW_0_DESC_PERF_EVT_O_REMOTE                  0xE0600

#define mmMME1_CTRL_SHADOW_0_DESC_PADDING_VALUE_S                    0xE0604

#define mmMME1_CTRL_SHADOW_0_DESC_PADDING_VALUE_L                    0xE0608

#define mmMME1_CTRL_SHADOW_0_DESC_META_DATA_AGU_S                    0xE060C

#define mmMME1_CTRL_SHADOW_0_DESC_META_DATA_AGU_L_LOCAL              0xE0610

#define mmMME1_CTRL_SHADOW_0_DESC_META_DATA_AGU_L_REMOTE             0xE0614

#define mmMME1_CTRL_SHADOW_0_DESC_META_DATA_AGU_O_LOCAL              0xE0618

#define mmMME1_CTRL_SHADOW_0_DESC_META_DATA_AGU_O_REMOTE             0xE061C

#define mmMME1_CTRL_SHADOW_0_DESC_PCU_RL_SATURATION                  0xE0620

#define mmMME1_CTRL_SHADOW_0_DESC_DUMMY                              0xE0624

#define mmMME1_CTRL_SHADOW_1_STATUS                                  0xE0680

#define mmMME1_CTRL_SHADOW_1_BASE_ADDR_HIGH_S                        0xE0688

#define mmMME1_CTRL_SHADOW_1_BASE_ADDR_HIGH_L                        0xE068C

#define mmMME1_CTRL_SHADOW_1_BASE_ADDR_HIGH_O                        0xE0690

#define mmMME1_CTRL_SHADOW_1_BASE_ADDR_LOW_S                         0xE0694

#define mmMME1_CTRL_SHADOW_1_BASE_ADDR_LOW_L                         0xE0698

#define mmMME1_CTRL_SHADOW_1_BASE_ADDR_LOW_O                         0xE069C

#define mmMME1_CTRL_SHADOW_1_HEADER_LOW                              0xE06A0

#define mmMME1_CTRL_SHADOW_1_HEADER_HIGH                             0xE06A4

#define mmMME1_CTRL_SHADOW_1_CONV_KERNEL_SIZE_MINUS_1                0xE06A8

#define mmMME1_CTRL_SHADOW_1_CONV_ASSOCIATED_DIMS_LOW                0xE06AC

#define mmMME1_CTRL_SHADOW_1_CONV_ASSOCIATED_DIMS_HIGH               0xE06B0

#define mmMME1_CTRL_SHADOW_1_NUM_ITERATIONS_MINUS_1                  0xE06B4

#define mmMME1_CTRL_SHADOW_1_OUTER_LOOP                              0xE06B8

#define mmMME1_CTRL_SHADOW_1_TENSOR_S_VALID_ELEMENTS_0               0xE06BC

#define mmMME1_CTRL_SHADOW_1_TENSOR_S_VALID_ELEMENTS_1               0xE06C0

#define mmMME1_CTRL_SHADOW_1_TENSOR_S_VALID_ELEMENTS_2               0xE06C4

#define mmMME1_CTRL_SHADOW_1_TENSOR_S_VALID_ELEMENTS_3               0xE06C8

#define mmMME1_CTRL_SHADOW_1_TENSOR_S_VALID_ELEMENTS_4               0xE06CC

#define mmMME1_CTRL_SHADOW_1_TENSOR_S_LOOP_STRIDE_0                  0xE06D0

#define mmMME1_CTRL_SHADOW_1_TENSOR_S_LOOP_STRIDE_1                  0xE06D4

#define mmMME1_CTRL_SHADOW_1_TENSOR_S_LOOP_STRIDE_2                  0xE06D8

#define mmMME1_CTRL_SHADOW_1_TENSOR_S_LOOP_STRIDE_3                  0xE06DC

#define mmMME1_CTRL_SHADOW_1_TENSOR_S_LOOP_STRIDE_4                  0xE06E0

#define mmMME1_CTRL_SHADOW_1_TENSOR_S_ROI_SIZE_0                     0xE06E4

#define mmMME1_CTRL_SHADOW_1_TENSOR_S_ROI_SIZE_1                     0xE06E8

#define mmMME1_CTRL_SHADOW_1_TENSOR_S_ROI_SIZE_2                     0xE06EC

#define mmMME1_CTRL_SHADOW_1_TENSOR_S_ROI_SIZE_3                     0xE06F0

#define mmMME1_CTRL_SHADOW_1_TENSOR_S_SPATIAL_STRIDES_0              0xE06F4

#define mmMME1_CTRL_SHADOW_1_TENSOR_S_SPATIAL_STRIDES_1              0xE06F8

#define mmMME1_CTRL_SHADOW_1_TENSOR_S_SPATIAL_STRIDES_2              0xE06FC

#define mmMME1_CTRL_SHADOW_1_TENSOR_S_SPATIAL_STRIDES_3              0xE0700

#define mmMME1_CTRL_SHADOW_1_TENSOR_S_SPATIAL_SIZE_MINUS_1           0xE0704

#define mmMME1_CTRL_SHADOW_1_AGU_S_ROI_BASE_OFFSET_0                 0xE0708

#define mmMME1_CTRL_SHADOW_1_AGU_S_ROI_BASE_OFFSET_1                 0xE070C

#define mmMME1_CTRL_SHADOW_1_AGU_S_ROI_BASE_OFFSET_2                 0xE0710

#define mmMME1_CTRL_SHADOW_1_AGU_S_ROI_BASE_OFFSET_3                 0xE0714

#define mmMME1_CTRL_SHADOW_1_AGU_S_ROI_BASE_OFFSET_4                 0xE0718

#define mmMME1_CTRL_SHADOW_1_AGU_S_START_OFFSET_0                    0xE071C

#define mmMME1_CTRL_SHADOW_1_AGU_S_START_OFFSET_1                    0xE0720

#define mmMME1_CTRL_SHADOW_1_AGU_S_START_OFFSET_2                    0xE0724

#define mmMME1_CTRL_SHADOW_1_AGU_S_START_OFFSET_3                    0xE0728

#define mmMME1_CTRL_SHADOW_1_TENSOR_L_VALID_ELEMENTS_0               0xE072C

#define mmMME1_CTRL_SHADOW_1_TENSOR_L_VALID_ELEMENTS_1               0xE0730

#define mmMME1_CTRL_SHADOW_1_TENSOR_L_VALID_ELEMENTS_2               0xE0734

#define mmMME1_CTRL_SHADOW_1_TENSOR_L_VALID_ELEMENTS_3               0xE0738

#define mmMME1_CTRL_SHADOW_1_TENSOR_L_VALID_ELEMENTS_4               0xE073C

#define mmMME1_CTRL_SHADOW_1_TENSOR_L_LOOP_STRIDE_0                  0xE0740

#define mmMME1_CTRL_SHADOW_1_TENSOR_L_LOOP_STRIDE_1                  0xE0744

#define mmMME1_CTRL_SHADOW_1_TENSOR_L_LOOP_STRIDE_2                  0xE0748

#define mmMME1_CTRL_SHADOW_1_TENSOR_L_LOOP_STRIDE_3                  0xE074C

#define mmMME1_CTRL_SHADOW_1_TENSOR_L_LOOP_STRIDE_4                  0xE0750

#define mmMME1_CTRL_SHADOW_1_TENSOR_L_ROI_SIZE_0                     0xE0754

#define mmMME1_CTRL_SHADOW_1_TENSOR_L_ROI_SIZE_1                     0xE0758

#define mmMME1_CTRL_SHADOW_1_TENSOR_L_ROI_SIZE_2                     0xE075C

#define mmMME1_CTRL_SHADOW_1_TENSOR_L_ROI_SIZE_3                     0xE0760

#define mmMME1_CTRL_SHADOW_1_TENSOR_L_SPATIAL_STRIDES_0              0xE0764

#define mmMME1_CTRL_SHADOW_1_TENSOR_L_SPATIAL_STRIDES_1              0xE0768

#define mmMME1_CTRL_SHADOW_1_TENSOR_L_SPATIAL_STRIDES_2              0xE076C

#define mmMME1_CTRL_SHADOW_1_TENSOR_L_SPATIAL_STRIDES_3              0xE0770

#define mmMME1_CTRL_SHADOW_1_TENSOR_L_SPATIAL_SIZE_MINUS_1           0xE0774

#define mmMME1_CTRL_SHADOW_1_AGU_L_LOCAL_ROI_BASE_OFFSET_0           0xE0778

#define mmMME1_CTRL_SHADOW_1_AGU_L_LOCAL_ROI_BASE_OFFSET_1           0xE077C

#define mmMME1_CTRL_SHADOW_1_AGU_L_LOCAL_ROI_BASE_OFFSET_2           0xE0780

#define mmMME1_CTRL_SHADOW_1_AGU_L_LOCAL_ROI_BASE_OFFSET_3           0xE0784

#define mmMME1_CTRL_SHADOW_1_AGU_L_LOCAL_ROI_BASE_OFFSET_4           0xE0788

#define mmMME1_CTRL_SHADOW_1_AGU_L_LOCAL_START_OFFSET_0              0xE078C

#define mmMME1_CTRL_SHADOW_1_AGU_L_LOCAL_START_OFFSET_1              0xE0790

#define mmMME1_CTRL_SHADOW_1_AGU_L_LOCAL_START_OFFSET_2              0xE0794

#define mmMME1_CTRL_SHADOW_1_AGU_L_LOCAL_START_OFFSET_3              0xE0798

#define mmMME1_CTRL_SHADOW_1_AGU_L_REMOTE_ROI_BASE_OFFSET_0          0xE079C

#define mmMME1_CTRL_SHADOW_1_AGU_L_REMOTE_ROI_BASE_OFFSET_1          0xE07A0

#define mmMME1_CTRL_SHADOW_1_AGU_L_REMOTE_ROI_BASE_OFFSET_2          0xE07A4

#define mmMME1_CTRL_SHADOW_1_AGU_L_REMOTE_ROI_BASE_OFFSET_3          0xE07A8

#define mmMME1_CTRL_SHADOW_1_AGU_L_REMOTE_ROI_BASE_OFFSET_4          0xE07AC

#define mmMME1_CTRL_SHADOW_1_AGU_L_REMOTE_START_OFFSET_0             0xE07B0

#define mmMME1_CTRL_SHADOW_1_AGU_L_REMOTE_START_OFFSET_1             0xE07B4

#define mmMME1_CTRL_SHADOW_1_AGU_L_REMOTE_START_OFFSET_2             0xE07B8

#define mmMME1_CTRL_SHADOW_1_AGU_L_REMOTE_START_OFFSET_3             0xE07BC

#define mmMME1_CTRL_SHADOW_1_TENSOR_O_VALID_ELEMENTS_0               0xE07C0

#define mmMME1_CTRL_SHADOW_1_TENSOR_O_VALID_ELEMENTS_1               0xE07C4

#define mmMME1_CTRL_SHADOW_1_TENSOR_O_VALID_ELEMENTS_2               0xE07C8

#define mmMME1_CTRL_SHADOW_1_TENSOR_O_VALID_ELEMENTS_3               0xE07CC

#define mmMME1_CTRL_SHADOW_1_TENSOR_O_VALID_ELEMENTS_4               0xE07D0

#define mmMME1_CTRL_SHADOW_1_TENSOR_O_LOOP_STRIDE_0                  0xE07D4

#define mmMME1_CTRL_SHADOW_1_TENSOR_O_LOOP_STRIDE_1                  0xE07D8

#define mmMME1_CTRL_SHADOW_1_TENSOR_O_LOOP_STRIDE_2                  0xE07DC

#define mmMME1_CTRL_SHADOW_1_TENSOR_O_LOOP_STRIDE_3                  0xE07E0

#define mmMME1_CTRL_SHADOW_1_TENSOR_O_LOOP_STRIDE_4                  0xE07E4

#define mmMME1_CTRL_SHADOW_1_TENSOR_O_ROI_SIZE_0                     0xE07E8

#define mmMME1_CTRL_SHADOW_1_TENSOR_O_ROI_SIZE_1                     0xE07EC

#define mmMME1_CTRL_SHADOW_1_TENSOR_O_ROI_SIZE_2                     0xE07F0

#define mmMME1_CTRL_SHADOW_1_TENSOR_O_ROI_SIZE_3                     0xE07F4

#define mmMME1_CTRL_SHADOW_1_TENSOR_O_SPATIAL_STRIDES_0              0xE07F8

#define mmMME1_CTRL_SHADOW_1_TENSOR_O_SPATIAL_STRIDES_1              0xE07FC

#define mmMME1_CTRL_SHADOW_1_TENSOR_O_SPATIAL_STRIDES_2              0xE0800

#define mmMME1_CTRL_SHADOW_1_TENSOR_O_SPATIAL_STRIDES_3              0xE0804

#define mmMME1_CTRL_SHADOW_1_TENSOR_O_SPATIAL_SIZE_MINUS_1           0xE0808

#define mmMME1_CTRL_SHADOW_1_AGU_O_LOCAL_ROI_BASE_OFFSET_0           0xE080C

#define mmMME1_CTRL_SHADOW_1_AGU_O_LOCAL_ROI_BASE_OFFSET_1           0xE0810

#define mmMME1_CTRL_SHADOW_1_AGU_O_LOCAL_ROI_BASE_OFFSET_2           0xE0814

#define mmMME1_CTRL_SHADOW_1_AGU_O_LOCAL_ROI_BASE_OFFSET_3           0xE0818

#define mmMME1_CTRL_SHADOW_1_AGU_O_LOCAL_ROI_BASE_OFFSET_4           0xE081C

#define mmMME1_CTRL_SHADOW_1_AGU_O_LOCAL_START_OFFSET_0              0xE0820

#define mmMME1_CTRL_SHADOW_1_AGU_O_LOCAL_START_OFFSET_1              0xE0824

#define mmMME1_CTRL_SHADOW_1_AGU_O_LOCAL_START_OFFSET_2              0xE0828

#define mmMME1_CTRL_SHADOW_1_AGU_O_LOCAL_START_OFFSET_3              0xE082C

#define mmMME1_CTRL_SHADOW_1_AGU_O_REMOTE_ROI_BASE_OFFSET_0          0xE0830

#define mmMME1_CTRL_SHADOW_1_AGU_O_REMOTE_ROI_BASE_OFFSET_1          0xE0834

#define mmMME1_CTRL_SHADOW_1_AGU_O_REMOTE_ROI_BASE_OFFSET_2          0xE0838

#define mmMME1_CTRL_SHADOW_1_AGU_O_REMOTE_ROI_BASE_OFFSET_3          0xE083C

#define mmMME1_CTRL_SHADOW_1_AGU_O_REMOTE_ROI_BASE_OFFSET_4          0xE0840

#define mmMME1_CTRL_SHADOW_1_AGU_O_REMOTE_START_OFFSET_0             0xE0844

#define mmMME1_CTRL_SHADOW_1_AGU_O_REMOTE_START_OFFSET_1             0xE0848

#define mmMME1_CTRL_SHADOW_1_AGU_O_REMOTE_START_OFFSET_2             0xE084C

#define mmMME1_CTRL_SHADOW_1_AGU_O_REMOTE_START_OFFSET_3             0xE0850

#define mmMME1_CTRL_SHADOW_1_DESC_SB_REPEAT                          0xE0854

#define mmMME1_CTRL_SHADOW_1_DESC_RATE_LIMITER                       0xE0858

#define mmMME1_CTRL_SHADOW_1_DESC_SYNC_OBJECT_ADDR_LOW_LOCAL         0xE085C

#define mmMME1_CTRL_SHADOW_1_DESC_SYNC_OBJECT_ADDR_LOW_REMOTE        0xE0860

#define mmMME1_CTRL_SHADOW_1_DESC_SYNC_OBJECT_ADDR_HIGH              0xE0864

#define mmMME1_CTRL_SHADOW_1_DESC_SYNC_OBJECT_DATA                   0xE0868

#define mmMME1_CTRL_SHADOW_1_DESC_AXI_USER_DATA                      0xE086C

#define mmMME1_CTRL_SHADOW_1_DESC_PERF_EVT_S                         0xE0870

#define mmMME1_CTRL_SHADOW_1_DESC_PERF_EVT_L_LOCAL                   0xE0874

#define mmMME1_CTRL_SHADOW_1_DESC_PERF_EVT_L_REMOTE                  0xE0878

#define mmMME1_CTRL_SHADOW_1_DESC_PERF_EVT_O_LOCAL                   0xE087C

#define mmMME1_CTRL_SHADOW_1_DESC_PERF_EVT_O_REMOTE                  0xE0880

#define mmMME1_CTRL_SHADOW_1_DESC_PADDING_VALUE_S                    0xE0884

#define mmMME1_CTRL_SHADOW_1_DESC_PADDING_VALUE_L                    0xE0888

#define mmMME1_CTRL_SHADOW_1_DESC_META_DATA_AGU_S                    0xE088C

#define mmMME1_CTRL_SHADOW_1_DESC_META_DATA_AGU_L_LOCAL              0xE0890

#define mmMME1_CTRL_SHADOW_1_DESC_META_DATA_AGU_L_REMOTE             0xE0894

#define mmMME1_CTRL_SHADOW_1_DESC_META_DATA_AGU_O_LOCAL              0xE0898

#define mmMME1_CTRL_SHADOW_1_DESC_META_DATA_AGU_O_REMOTE             0xE089C

#define mmMME1_CTRL_SHADOW_1_DESC_PCU_RL_SATURATION                  0xE08A0

#define mmMME1_CTRL_SHADOW_1_DESC_DUMMY                              0xE08A4

#define mmMME1_CTRL_SHADOW_2_STATUS                                  0xE0900

#define mmMME1_CTRL_SHADOW_2_BASE_ADDR_HIGH_S                        0xE0908

#define mmMME1_CTRL_SHADOW_2_BASE_ADDR_HIGH_L                        0xE090C

#define mmMME1_CTRL_SHADOW_2_BASE_ADDR_HIGH_O                        0xE0910

#define mmMME1_CTRL_SHADOW_2_BASE_ADDR_LOW_S                         0xE0914

#define mmMME1_CTRL_SHADOW_2_BASE_ADDR_LOW_L                         0xE0918

#define mmMME1_CTRL_SHADOW_2_BASE_ADDR_LOW_O                         0xE091C

#define mmMME1_CTRL_SHADOW_2_HEADER_LOW                              0xE0920

#define mmMME1_CTRL_SHADOW_2_HEADER_HIGH                             0xE0924

#define mmMME1_CTRL_SHADOW_2_CONV_KERNEL_SIZE_MINUS_1                0xE0928

#define mmMME1_CTRL_SHADOW_2_CONV_ASSOCIATED_DIMS_LOW                0xE092C

#define mmMME1_CTRL_SHADOW_2_CONV_ASSOCIATED_DIMS_HIGH               0xE0930

#define mmMME1_CTRL_SHADOW_2_NUM_ITERATIONS_MINUS_1                  0xE0934

#define mmMME1_CTRL_SHADOW_2_OUTER_LOOP                              0xE0938

#define mmMME1_CTRL_SHADOW_2_TENSOR_S_VALID_ELEMENTS_0               0xE093C

#define mmMME1_CTRL_SHADOW_2_TENSOR_S_VALID_ELEMENTS_1               0xE0940

#define mmMME1_CTRL_SHADOW_2_TENSOR_S_VALID_ELEMENTS_2               0xE0944

#define mmMME1_CTRL_SHADOW_2_TENSOR_S_VALID_ELEMENTS_3               0xE0948

#define mmMME1_CTRL_SHADOW_2_TENSOR_S_VALID_ELEMENTS_4               0xE094C

#define mmMME1_CTRL_SHADOW_2_TENSOR_S_LOOP_STRIDE_0                  0xE0950

#define mmMME1_CTRL_SHADOW_2_TENSOR_S_LOOP_STRIDE_1                  0xE0954

#define mmMME1_CTRL_SHADOW_2_TENSOR_S_LOOP_STRIDE_2                  0xE0958

#define mmMME1_CTRL_SHADOW_2_TENSOR_S_LOOP_STRIDE_3                  0xE095C

#define mmMME1_CTRL_SHADOW_2_TENSOR_S_LOOP_STRIDE_4                  0xE0960

#define mmMME1_CTRL_SHADOW_2_TENSOR_S_ROI_SIZE_0                     0xE0964

#define mmMME1_CTRL_SHADOW_2_TENSOR_S_ROI_SIZE_1                     0xE0968

#define mmMME1_CTRL_SHADOW_2_TENSOR_S_ROI_SIZE_2                     0xE096C

#define mmMME1_CTRL_SHADOW_2_TENSOR_S_ROI_SIZE_3                     0xE0970

#define mmMME1_CTRL_SHADOW_2_TENSOR_S_SPATIAL_STRIDES_0              0xE0974

#define mmMME1_CTRL_SHADOW_2_TENSOR_S_SPATIAL_STRIDES_1              0xE0978

#define mmMME1_CTRL_SHADOW_2_TENSOR_S_SPATIAL_STRIDES_2              0xE097C

#define mmMME1_CTRL_SHADOW_2_TENSOR_S_SPATIAL_STRIDES_3              0xE0980

#define mmMME1_CTRL_SHADOW_2_TENSOR_S_SPATIAL_SIZE_MINUS_1           0xE0984

#define mmMME1_CTRL_SHADOW_2_AGU_S_ROI_BASE_OFFSET_0                 0xE0988

#define mmMME1_CTRL_SHADOW_2_AGU_S_ROI_BASE_OFFSET_1                 0xE098C

#define mmMME1_CTRL_SHADOW_2_AGU_S_ROI_BASE_OFFSET_2                 0xE0990

#define mmMME1_CTRL_SHADOW_2_AGU_S_ROI_BASE_OFFSET_3                 0xE0994

#define mmMME1_CTRL_SHADOW_2_AGU_S_ROI_BASE_OFFSET_4                 0xE0998

#define mmMME1_CTRL_SHADOW_2_AGU_S_START_OFFSET_0                    0xE099C

#define mmMME1_CTRL_SHADOW_2_AGU_S_START_OFFSET_1                    0xE09A0

#define mmMME1_CTRL_SHADOW_2_AGU_S_START_OFFSET_2                    0xE09A4

#define mmMME1_CTRL_SHADOW_2_AGU_S_START_OFFSET_3                    0xE09A8

#define mmMME1_CTRL_SHADOW_2_TENSOR_L_VALID_ELEMENTS_0               0xE09AC

#define mmMME1_CTRL_SHADOW_2_TENSOR_L_VALID_ELEMENTS_1               0xE09B0

#define mmMME1_CTRL_SHADOW_2_TENSOR_L_VALID_ELEMENTS_2               0xE09B4

#define mmMME1_CTRL_SHADOW_2_TENSOR_L_VALID_ELEMENTS_3               0xE09B8

#define mmMME1_CTRL_SHADOW_2_TENSOR_L_VALID_ELEMENTS_4               0xE09BC

#define mmMME1_CTRL_SHADOW_2_TENSOR_L_LOOP_STRIDE_0                  0xE09C0

#define mmMME1_CTRL_SHADOW_2_TENSOR_L_LOOP_STRIDE_1                  0xE09C4

#define mmMME1_CTRL_SHADOW_2_TENSOR_L_LOOP_STRIDE_2                  0xE09C8

#define mmMME1_CTRL_SHADOW_2_TENSOR_L_LOOP_STRIDE_3                  0xE09CC

#define mmMME1_CTRL_SHADOW_2_TENSOR_L_LOOP_STRIDE_4                  0xE09D0

#define mmMME1_CTRL_SHADOW_2_TENSOR_L_ROI_SIZE_0                     0xE09D4

#define mmMME1_CTRL_SHADOW_2_TENSOR_L_ROI_SIZE_1                     0xE09D8

#define mmMME1_CTRL_SHADOW_2_TENSOR_L_ROI_SIZE_2                     0xE09DC

#define mmMME1_CTRL_SHADOW_2_TENSOR_L_ROI_SIZE_3                     0xE09E0

#define mmMME1_CTRL_SHADOW_2_TENSOR_L_SPATIAL_STRIDES_0              0xE09E4

#define mmMME1_CTRL_SHADOW_2_TENSOR_L_SPATIAL_STRIDES_1              0xE09E8

#define mmMME1_CTRL_SHADOW_2_TENSOR_L_SPATIAL_STRIDES_2              0xE09EC

#define mmMME1_CTRL_SHADOW_2_TENSOR_L_SPATIAL_STRIDES_3              0xE09F0

#define mmMME1_CTRL_SHADOW_2_TENSOR_L_SPATIAL_SIZE_MINUS_1           0xE09F4

#define mmMME1_CTRL_SHADOW_2_AGU_L_LOCAL_ROI_BASE_OFFSET_0           0xE09F8

#define mmMME1_CTRL_SHADOW_2_AGU_L_LOCAL_ROI_BASE_OFFSET_1           0xE09FC

#define mmMME1_CTRL_SHADOW_2_AGU_L_LOCAL_ROI_BASE_OFFSET_2           0xE0A00

#define mmMME1_CTRL_SHADOW_2_AGU_L_LOCAL_ROI_BASE_OFFSET_3           0xE0A04

#define mmMME1_CTRL_SHADOW_2_AGU_L_LOCAL_ROI_BASE_OFFSET_4           0xE0A08

#define mmMME1_CTRL_SHADOW_2_AGU_L_LOCAL_START_OFFSET_0              0xE0A0C

#define mmMME1_CTRL_SHADOW_2_AGU_L_LOCAL_START_OFFSET_1              0xE0A10

#define mmMME1_CTRL_SHADOW_2_AGU_L_LOCAL_START_OFFSET_2              0xE0A14

#define mmMME1_CTRL_SHADOW_2_AGU_L_LOCAL_START_OFFSET_3              0xE0A18

#define mmMME1_CTRL_SHADOW_2_AGU_L_REMOTE_ROI_BASE_OFFSET_0          0xE0A1C

#define mmMME1_CTRL_SHADOW_2_AGU_L_REMOTE_ROI_BASE_OFFSET_1          0xE0A20

#define mmMME1_CTRL_SHADOW_2_AGU_L_REMOTE_ROI_BASE_OFFSET_2          0xE0A24

#define mmMME1_CTRL_SHADOW_2_AGU_L_REMOTE_ROI_BASE_OFFSET_3          0xE0A28

#define mmMME1_CTRL_SHADOW_2_AGU_L_REMOTE_ROI_BASE_OFFSET_4          0xE0A2C

#define mmMME1_CTRL_SHADOW_2_AGU_L_REMOTE_START_OFFSET_0             0xE0A30

#define mmMME1_CTRL_SHADOW_2_AGU_L_REMOTE_START_OFFSET_1             0xE0A34

#define mmMME1_CTRL_SHADOW_2_AGU_L_REMOTE_START_OFFSET_2             0xE0A38

#define mmMME1_CTRL_SHADOW_2_AGU_L_REMOTE_START_OFFSET_3             0xE0A3C

#define mmMME1_CTRL_SHADOW_2_TENSOR_O_VALID_ELEMENTS_0               0xE0A40

#define mmMME1_CTRL_SHADOW_2_TENSOR_O_VALID_ELEMENTS_1               0xE0A44

#define mmMME1_CTRL_SHADOW_2_TENSOR_O_VALID_ELEMENTS_2               0xE0A48

#define mmMME1_CTRL_SHADOW_2_TENSOR_O_VALID_ELEMENTS_3               0xE0A4C

#define mmMME1_CTRL_SHADOW_2_TENSOR_O_VALID_ELEMENTS_4               0xE0A50

#define mmMME1_CTRL_SHADOW_2_TENSOR_O_LOOP_STRIDE_0                  0xE0A54

#define mmMME1_CTRL_SHADOW_2_TENSOR_O_LOOP_STRIDE_1                  0xE0A58

#define mmMME1_CTRL_SHADOW_2_TENSOR_O_LOOP_STRIDE_2                  0xE0A5C

#define mmMME1_CTRL_SHADOW_2_TENSOR_O_LOOP_STRIDE_3                  0xE0A60

#define mmMME1_CTRL_SHADOW_2_TENSOR_O_LOOP_STRIDE_4                  0xE0A64

#define mmMME1_CTRL_SHADOW_2_TENSOR_O_ROI_SIZE_0                     0xE0A68

#define mmMME1_CTRL_SHADOW_2_TENSOR_O_ROI_SIZE_1                     0xE0A6C

#define mmMME1_CTRL_SHADOW_2_TENSOR_O_ROI_SIZE_2                     0xE0A70

#define mmMME1_CTRL_SHADOW_2_TENSOR_O_ROI_SIZE_3                     0xE0A74

#define mmMME1_CTRL_SHADOW_2_TENSOR_O_SPATIAL_STRIDES_0              0xE0A78

#define mmMME1_CTRL_SHADOW_2_TENSOR_O_SPATIAL_STRIDES_1              0xE0A7C

#define mmMME1_CTRL_SHADOW_2_TENSOR_O_SPATIAL_STRIDES_2              0xE0A80

#define mmMME1_CTRL_SHADOW_2_TENSOR_O_SPATIAL_STRIDES_3              0xE0A84

#define mmMME1_CTRL_SHADOW_2_TENSOR_O_SPATIAL_SIZE_MINUS_1           0xE0A88

#define mmMME1_CTRL_SHADOW_2_AGU_O_LOCAL_ROI_BASE_OFFSET_0           0xE0A8C

#define mmMME1_CTRL_SHADOW_2_AGU_O_LOCAL_ROI_BASE_OFFSET_1           0xE0A90

#define mmMME1_CTRL_SHADOW_2_AGU_O_LOCAL_ROI_BASE_OFFSET_2           0xE0A94

#define mmMME1_CTRL_SHADOW_2_AGU_O_LOCAL_ROI_BASE_OFFSET_3           0xE0A98

#define mmMME1_CTRL_SHADOW_2_AGU_O_LOCAL_ROI_BASE_OFFSET_4           0xE0A9C

#define mmMME1_CTRL_SHADOW_2_AGU_O_LOCAL_START_OFFSET_0              0xE0AA0

#define mmMME1_CTRL_SHADOW_2_AGU_O_LOCAL_START_OFFSET_1              0xE0AA4

#define mmMME1_CTRL_SHADOW_2_AGU_O_LOCAL_START_OFFSET_2              0xE0AA8

#define mmMME1_CTRL_SHADOW_2_AGU_O_LOCAL_START_OFFSET_3              0xE0AAC

#define mmMME1_CTRL_SHADOW_2_AGU_O_REMOTE_ROI_BASE_OFFSET_0          0xE0AB0

#define mmMME1_CTRL_SHADOW_2_AGU_O_REMOTE_ROI_BASE_OFFSET_1          0xE0AB4

#define mmMME1_CTRL_SHADOW_2_AGU_O_REMOTE_ROI_BASE_OFFSET_2          0xE0AB8

#define mmMME1_CTRL_SHADOW_2_AGU_O_REMOTE_ROI_BASE_OFFSET_3          0xE0ABC

#define mmMME1_CTRL_SHADOW_2_AGU_O_REMOTE_ROI_BASE_OFFSET_4          0xE0AC0

#define mmMME1_CTRL_SHADOW_2_AGU_O_REMOTE_START_OFFSET_0             0xE0AC4

#define mmMME1_CTRL_SHADOW_2_AGU_O_REMOTE_START_OFFSET_1             0xE0AC8

#define mmMME1_CTRL_SHADOW_2_AGU_O_REMOTE_START_OFFSET_2             0xE0ACC

#define mmMME1_CTRL_SHADOW_2_AGU_O_REMOTE_START_OFFSET_3             0xE0AD0

#define mmMME1_CTRL_SHADOW_2_DESC_SB_REPEAT                          0xE0AD4

#define mmMME1_CTRL_SHADOW_2_DESC_RATE_LIMITER                       0xE0AD8

#define mmMME1_CTRL_SHADOW_2_DESC_SYNC_OBJECT_ADDR_LOW_LOCAL         0xE0ADC

#define mmMME1_CTRL_SHADOW_2_DESC_SYNC_OBJECT_ADDR_LOW_REMOTE        0xE0AE0

#define mmMME1_CTRL_SHADOW_2_DESC_SYNC_OBJECT_ADDR_HIGH              0xE0AE4

#define mmMME1_CTRL_SHADOW_2_DESC_SYNC_OBJECT_DATA                   0xE0AE8

#define mmMME1_CTRL_SHADOW_2_DESC_AXI_USER_DATA                      0xE0AEC

#define mmMME1_CTRL_SHADOW_2_DESC_PERF_EVT_S                         0xE0AF0

#define mmMME1_CTRL_SHADOW_2_DESC_PERF_EVT_L_LOCAL                   0xE0AF4

#define mmMME1_CTRL_SHADOW_2_DESC_PERF_EVT_L_REMOTE                  0xE0AF8

#define mmMME1_CTRL_SHADOW_2_DESC_PERF_EVT_O_LOCAL                   0xE0AFC

#define mmMME1_CTRL_SHADOW_2_DESC_PERF_EVT_O_REMOTE                  0xE0B00

#define mmMME1_CTRL_SHADOW_2_DESC_PADDING_VALUE_S                    0xE0B04

#define mmMME1_CTRL_SHADOW_2_DESC_PADDING_VALUE_L                    0xE0B08

#define mmMME1_CTRL_SHADOW_2_DESC_META_DATA_AGU_S                    0xE0B0C

#define mmMME1_CTRL_SHADOW_2_DESC_META_DATA_AGU_L_LOCAL              0xE0B10

#define mmMME1_CTRL_SHADOW_2_DESC_META_DATA_AGU_L_REMOTE             0xE0B14

#define mmMME1_CTRL_SHADOW_2_DESC_META_DATA_AGU_O_LOCAL              0xE0B18

#define mmMME1_CTRL_SHADOW_2_DESC_META_DATA_AGU_O_REMOTE             0xE0B1C

#define mmMME1_CTRL_SHADOW_2_DESC_PCU_RL_SATURATION                  0xE0B20

#define mmMME1_CTRL_SHADOW_2_DESC_DUMMY                              0xE0B24

#define mmMME1_CTRL_SHADOW_3_STATUS                                  0xE0B80

#define mmMME1_CTRL_SHADOW_3_BASE_ADDR_HIGH_S                        0xE0B88

#define mmMME1_CTRL_SHADOW_3_BASE_ADDR_HIGH_L                        0xE0B8C

#define mmMME1_CTRL_SHADOW_3_BASE_ADDR_HIGH_O                        0xE0B90

#define mmMME1_CTRL_SHADOW_3_BASE_ADDR_LOW_S                         0xE0B94

#define mmMME1_CTRL_SHADOW_3_BASE_ADDR_LOW_L                         0xE0B98

#define mmMME1_CTRL_SHADOW_3_BASE_ADDR_LOW_O                         0xE0B9C

#define mmMME1_CTRL_SHADOW_3_HEADER_LOW                              0xE0BA0

#define mmMME1_CTRL_SHADOW_3_HEADER_HIGH                             0xE0BA4

#define mmMME1_CTRL_SHADOW_3_CONV_KERNEL_SIZE_MINUS_1                0xE0BA8

#define mmMME1_CTRL_SHADOW_3_CONV_ASSOCIATED_DIMS_LOW                0xE0BAC

#define mmMME1_CTRL_SHADOW_3_CONV_ASSOCIATED_DIMS_HIGH               0xE0BB0

#define mmMME1_CTRL_SHADOW_3_NUM_ITERATIONS_MINUS_1                  0xE0BB4

#define mmMME1_CTRL_SHADOW_3_OUTER_LOOP                              0xE0BB8

#define mmMME1_CTRL_SHADOW_3_TENSOR_S_VALID_ELEMENTS_0               0xE0BBC

#define mmMME1_CTRL_SHADOW_3_TENSOR_S_VALID_ELEMENTS_1               0xE0BC0

#define mmMME1_CTRL_SHADOW_3_TENSOR_S_VALID_ELEMENTS_2               0xE0BC4

#define mmMME1_CTRL_SHADOW_3_TENSOR_S_VALID_ELEMENTS_3               0xE0BC8

#define mmMME1_CTRL_SHADOW_3_TENSOR_S_VALID_ELEMENTS_4               0xE0BCC

#define mmMME1_CTRL_SHADOW_3_TENSOR_S_LOOP_STRIDE_0                  0xE0BD0

#define mmMME1_CTRL_SHADOW_3_TENSOR_S_LOOP_STRIDE_1                  0xE0BD4

#define mmMME1_CTRL_SHADOW_3_TENSOR_S_LOOP_STRIDE_2                  0xE0BD8

#define mmMME1_CTRL_SHADOW_3_TENSOR_S_LOOP_STRIDE_3                  0xE0BDC

#define mmMME1_CTRL_SHADOW_3_TENSOR_S_LOOP_STRIDE_4                  0xE0BE0

#define mmMME1_CTRL_SHADOW_3_TENSOR_S_ROI_SIZE_0                     0xE0BE4

#define mmMME1_CTRL_SHADOW_3_TENSOR_S_ROI_SIZE_1                     0xE0BE8

#define mmMME1_CTRL_SHADOW_3_TENSOR_S_ROI_SIZE_2                     0xE0BEC

#define mmMME1_CTRL_SHADOW_3_TENSOR_S_ROI_SIZE_3                     0xE0BF0

#define mmMME1_CTRL_SHADOW_3_TENSOR_S_SPATIAL_STRIDES_0              0xE0BF4

#define mmMME1_CTRL_SHADOW_3_TENSOR_S_SPATIAL_STRIDES_1              0xE0BF8

#define mmMME1_CTRL_SHADOW_3_TENSOR_S_SPATIAL_STRIDES_2              0xE0BFC

#define mmMME1_CTRL_SHADOW_3_TENSOR_S_SPATIAL_STRIDES_3              0xE0C00

#define mmMME1_CTRL_SHADOW_3_TENSOR_S_SPATIAL_SIZE_MINUS_1           0xE0C04

#define mmMME1_CTRL_SHADOW_3_AGU_S_ROI_BASE_OFFSET_0                 0xE0C08

#define mmMME1_CTRL_SHADOW_3_AGU_S_ROI_BASE_OFFSET_1                 0xE0C0C

#define mmMME1_CTRL_SHADOW_3_AGU_S_ROI_BASE_OFFSET_2                 0xE0C10

#define mmMME1_CTRL_SHADOW_3_AGU_S_ROI_BASE_OFFSET_3                 0xE0C14

#define mmMME1_CTRL_SHADOW_3_AGU_S_ROI_BASE_OFFSET_4                 0xE0C18

#define mmMME1_CTRL_SHADOW_3_AGU_S_START_OFFSET_0                    0xE0C1C

#define mmMME1_CTRL_SHADOW_3_AGU_S_START_OFFSET_1                    0xE0C20

#define mmMME1_CTRL_SHADOW_3_AGU_S_START_OFFSET_2                    0xE0C24

#define mmMME1_CTRL_SHADOW_3_AGU_S_START_OFFSET_3                    0xE0C28

#define mmMME1_CTRL_SHADOW_3_TENSOR_L_VALID_ELEMENTS_0               0xE0C2C

#define mmMME1_CTRL_SHADOW_3_TENSOR_L_VALID_ELEMENTS_1               0xE0C30

#define mmMME1_CTRL_SHADOW_3_TENSOR_L_VALID_ELEMENTS_2               0xE0C34

#define mmMME1_CTRL_SHADOW_3_TENSOR_L_VALID_ELEMENTS_3               0xE0C38

#define mmMME1_CTRL_SHADOW_3_TENSOR_L_VALID_ELEMENTS_4               0xE0C3C

#define mmMME1_CTRL_SHADOW_3_TENSOR_L_LOOP_STRIDE_0                  0xE0C40

#define mmMME1_CTRL_SHADOW_3_TENSOR_L_LOOP_STRIDE_1                  0xE0C44

#define mmMME1_CTRL_SHADOW_3_TENSOR_L_LOOP_STRIDE_2                  0xE0C48

#define mmMME1_CTRL_SHADOW_3_TENSOR_L_LOOP_STRIDE_3                  0xE0C4C

#define mmMME1_CTRL_SHADOW_3_TENSOR_L_LOOP_STRIDE_4                  0xE0C50

#define mmMME1_CTRL_SHADOW_3_TENSOR_L_ROI_SIZE_0                     0xE0C54

#define mmMME1_CTRL_SHADOW_3_TENSOR_L_ROI_SIZE_1                     0xE0C58

#define mmMME1_CTRL_SHADOW_3_TENSOR_L_ROI_SIZE_2                     0xE0C5C

#define mmMME1_CTRL_SHADOW_3_TENSOR_L_ROI_SIZE_3                     0xE0C60

#define mmMME1_CTRL_SHADOW_3_TENSOR_L_SPATIAL_STRIDES_0              0xE0C64

#define mmMME1_CTRL_SHADOW_3_TENSOR_L_SPATIAL_STRIDES_1              0xE0C68

#define mmMME1_CTRL_SHADOW_3_TENSOR_L_SPATIAL_STRIDES_2              0xE0C6C

#define mmMME1_CTRL_SHADOW_3_TENSOR_L_SPATIAL_STRIDES_3              0xE0C70

#define mmMME1_CTRL_SHADOW_3_TENSOR_L_SPATIAL_SIZE_MINUS_1           0xE0C74

#define mmMME1_CTRL_SHADOW_3_AGU_L_LOCAL_ROI_BASE_OFFSET_0           0xE0C78

#define mmMME1_CTRL_SHADOW_3_AGU_L_LOCAL_ROI_BASE_OFFSET_1           0xE0C7C

#define mmMME1_CTRL_SHADOW_3_AGU_L_LOCAL_ROI_BASE_OFFSET_2           0xE0C80

#define mmMME1_CTRL_SHADOW_3_AGU_L_LOCAL_ROI_BASE_OFFSET_3           0xE0C84

#define mmMME1_CTRL_SHADOW_3_AGU_L_LOCAL_ROI_BASE_OFFSET_4           0xE0C88

#define mmMME1_CTRL_SHADOW_3_AGU_L_LOCAL_START_OFFSET_0              0xE0C8C

#define mmMME1_CTRL_SHADOW_3_AGU_L_LOCAL_START_OFFSET_1              0xE0C90

#define mmMME1_CTRL_SHADOW_3_AGU_L_LOCAL_START_OFFSET_2              0xE0C94

#define mmMME1_CTRL_SHADOW_3_AGU_L_LOCAL_START_OFFSET_3              0xE0C98

#define mmMME1_CTRL_SHADOW_3_AGU_L_REMOTE_ROI_BASE_OFFSET_0          0xE0C9C

#define mmMME1_CTRL_SHADOW_3_AGU_L_REMOTE_ROI_BASE_OFFSET_1          0xE0CA0

#define mmMME1_CTRL_SHADOW_3_AGU_L_REMOTE_ROI_BASE_OFFSET_2          0xE0CA4

#define mmMME1_CTRL_SHADOW_3_AGU_L_REMOTE_ROI_BASE_OFFSET_3          0xE0CA8

#define mmMME1_CTRL_SHADOW_3_AGU_L_REMOTE_ROI_BASE_OFFSET_4          0xE0CAC

#define mmMME1_CTRL_SHADOW_3_AGU_L_REMOTE_START_OFFSET_0             0xE0CB0

#define mmMME1_CTRL_SHADOW_3_AGU_L_REMOTE_START_OFFSET_1             0xE0CB4

#define mmMME1_CTRL_SHADOW_3_AGU_L_REMOTE_START_OFFSET_2             0xE0CB8

#define mmMME1_CTRL_SHADOW_3_AGU_L_REMOTE_START_OFFSET_3             0xE0CBC

#define mmMME1_CTRL_SHADOW_3_TENSOR_O_VALID_ELEMENTS_0               0xE0CC0

#define mmMME1_CTRL_SHADOW_3_TENSOR_O_VALID_ELEMENTS_1               0xE0CC4

#define mmMME1_CTRL_SHADOW_3_TENSOR_O_VALID_ELEMENTS_2               0xE0CC8

#define mmMME1_CTRL_SHADOW_3_TENSOR_O_VALID_ELEMENTS_3               0xE0CCC

#define mmMME1_CTRL_SHADOW_3_TENSOR_O_VALID_ELEMENTS_4               0xE0CD0

#define mmMME1_CTRL_SHADOW_3_TENSOR_O_LOOP_STRIDE_0                  0xE0CD4

#define mmMME1_CTRL_SHADOW_3_TENSOR_O_LOOP_STRIDE_1                  0xE0CD8

#define mmMME1_CTRL_SHADOW_3_TENSOR_O_LOOP_STRIDE_2                  0xE0CDC

#define mmMME1_CTRL_SHADOW_3_TENSOR_O_LOOP_STRIDE_3                  0xE0CE0

#define mmMME1_CTRL_SHADOW_3_TENSOR_O_LOOP_STRIDE_4                  0xE0CE4

#define mmMME1_CTRL_SHADOW_3_TENSOR_O_ROI_SIZE_0                     0xE0CE8

#define mmMME1_CTRL_SHADOW_3_TENSOR_O_ROI_SIZE_1                     0xE0CEC

#define mmMME1_CTRL_SHADOW_3_TENSOR_O_ROI_SIZE_2                     0xE0CF0

#define mmMME1_CTRL_SHADOW_3_TENSOR_O_ROI_SIZE_3                     0xE0CF4

#define mmMME1_CTRL_SHADOW_3_TENSOR_O_SPATIAL_STRIDES_0              0xE0CF8

#define mmMME1_CTRL_SHADOW_3_TENSOR_O_SPATIAL_STRIDES_1              0xE0CFC

#define mmMME1_CTRL_SHADOW_3_TENSOR_O_SPATIAL_STRIDES_2              0xE0D00

#define mmMME1_CTRL_SHADOW_3_TENSOR_O_SPATIAL_STRIDES_3              0xE0D04

#define mmMME1_CTRL_SHADOW_3_TENSOR_O_SPATIAL_SIZE_MINUS_1           0xE0D08

#define mmMME1_CTRL_SHADOW_3_AGU_O_LOCAL_ROI_BASE_OFFSET_0           0xE0D0C

#define mmMME1_CTRL_SHADOW_3_AGU_O_LOCAL_ROI_BASE_OFFSET_1           0xE0D10

#define mmMME1_CTRL_SHADOW_3_AGU_O_LOCAL_ROI_BASE_OFFSET_2           0xE0D14

#define mmMME1_CTRL_SHADOW_3_AGU_O_LOCAL_ROI_BASE_OFFSET_3           0xE0D18

#define mmMME1_CTRL_SHADOW_3_AGU_O_LOCAL_ROI_BASE_OFFSET_4           0xE0D1C

#define mmMME1_CTRL_SHADOW_3_AGU_O_LOCAL_START_OFFSET_0              0xE0D20

#define mmMME1_CTRL_SHADOW_3_AGU_O_LOCAL_START_OFFSET_1              0xE0D24

#define mmMME1_CTRL_SHADOW_3_AGU_O_LOCAL_START_OFFSET_2              0xE0D28

#define mmMME1_CTRL_SHADOW_3_AGU_O_LOCAL_START_OFFSET_3              0xE0D2C

#define mmMME1_CTRL_SHADOW_3_AGU_O_REMOTE_ROI_BASE_OFFSET_0          0xE0D30

#define mmMME1_CTRL_SHADOW_3_AGU_O_REMOTE_ROI_BASE_OFFSET_1          0xE0D34

#define mmMME1_CTRL_SHADOW_3_AGU_O_REMOTE_ROI_BASE_OFFSET_2          0xE0D38

#define mmMME1_CTRL_SHADOW_3_AGU_O_REMOTE_ROI_BASE_OFFSET_3          0xE0D3C

#define mmMME1_CTRL_SHADOW_3_AGU_O_REMOTE_ROI_BASE_OFFSET_4          0xE0D40

#define mmMME1_CTRL_SHADOW_3_AGU_O_REMOTE_START_OFFSET_0             0xE0D44

#define mmMME1_CTRL_SHADOW_3_AGU_O_REMOTE_START_OFFSET_1             0xE0D48

#define mmMME1_CTRL_SHADOW_3_AGU_O_REMOTE_START_OFFSET_2             0xE0D4C

#define mmMME1_CTRL_SHADOW_3_AGU_O_REMOTE_START_OFFSET_3             0xE0D50

#define mmMME1_CTRL_SHADOW_3_DESC_SB_REPEAT                          0xE0D54

#define mmMME1_CTRL_SHADOW_3_DESC_RATE_LIMITER                       0xE0D58

#define mmMME1_CTRL_SHADOW_3_DESC_SYNC_OBJECT_ADDR_LOW_LOCAL         0xE0D5C

#define mmMME1_CTRL_SHADOW_3_DESC_SYNC_OBJECT_ADDR_LOW_REMOTE        0xE0D60

#define mmMME1_CTRL_SHADOW_3_DESC_SYNC_OBJECT_ADDR_HIGH              0xE0D64

#define mmMME1_CTRL_SHADOW_3_DESC_SYNC_OBJECT_DATA                   0xE0D68

#define mmMME1_CTRL_SHADOW_3_DESC_AXI_USER_DATA                      0xE0D6C

#define mmMME1_CTRL_SHADOW_3_DESC_PERF_EVT_S                         0xE0D70

#define mmMME1_CTRL_SHADOW_3_DESC_PERF_EVT_L_LOCAL                   0xE0D74

#define mmMME1_CTRL_SHADOW_3_DESC_PERF_EVT_L_REMOTE                  0xE0D78

#define mmMME1_CTRL_SHADOW_3_DESC_PERF_EVT_O_LOCAL                   0xE0D7C

#define mmMME1_CTRL_SHADOW_3_DESC_PERF_EVT_O_REMOTE                  0xE0D80

#define mmMME1_CTRL_SHADOW_3_DESC_PADDING_VALUE_S                    0xE0D84

#define mmMME1_CTRL_SHADOW_3_DESC_PADDING_VALUE_L                    0xE0D88

#define mmMME1_CTRL_SHADOW_3_DESC_META_DATA_AGU_S                    0xE0D8C

#define mmMME1_CTRL_SHADOW_3_DESC_META_DATA_AGU_L_LOCAL              0xE0D90

#define mmMME1_CTRL_SHADOW_3_DESC_META_DATA_AGU_L_REMOTE             0xE0D94

#define mmMME1_CTRL_SHADOW_3_DESC_META_DATA_AGU_O_LOCAL              0xE0D98

#define mmMME1_CTRL_SHADOW_3_DESC_META_DATA_AGU_O_REMOTE             0xE0D9C

#define mmMME1_CTRL_SHADOW_3_DESC_PCU_RL_SATURATION                  0xE0DA0

#define mmMME1_CTRL_SHADOW_3_DESC_DUMMY                              0xE0DA4

#endif /* ASIC_REG_MME1_CTRL_REGS_H_ */
