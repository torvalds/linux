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

#ifndef ASIC_REG_MME3_CTRL_REGS_H_
#define ASIC_REG_MME3_CTRL_REGS_H_

/*
 *****************************************
 *   MME3_CTRL (Prototype: MME)
 *****************************************
 */

#define mmMME3_CTRL_ARCH_STATUS                                      0x1E0000

#define mmMME3_CTRL_ARCH_BASE_ADDR_HIGH_S                            0x1E0008

#define mmMME3_CTRL_ARCH_BASE_ADDR_HIGH_L                            0x1E000C

#define mmMME3_CTRL_ARCH_BASE_ADDR_HIGH_O                            0x1E0010

#define mmMME3_CTRL_ARCH_BASE_ADDR_LOW_S                             0x1E0014

#define mmMME3_CTRL_ARCH_BASE_ADDR_LOW_L                             0x1E0018

#define mmMME3_CTRL_ARCH_BASE_ADDR_LOW_O                             0x1E001C

#define mmMME3_CTRL_ARCH_HEADER_LOW                                  0x1E0020

#define mmMME3_CTRL_ARCH_HEADER_HIGH                                 0x1E0024

#define mmMME3_CTRL_ARCH_CONV_KERNEL_SIZE_MINUS_1                    0x1E0028

#define mmMME3_CTRL_ARCH_CONV_ASSOCIATED_DIMS_LOW                    0x1E002C

#define mmMME3_CTRL_ARCH_CONV_ASSOCIATED_DIMS_HIGH                   0x1E0030

#define mmMME3_CTRL_ARCH_NUM_ITERATIONS_MINUS_1                      0x1E0034

#define mmMME3_CTRL_ARCH_OUTER_LOOP                                  0x1E0038

#define mmMME3_CTRL_ARCH_TENSOR_S_VALID_ELEMENTS_0                   0x1E003C

#define mmMME3_CTRL_ARCH_TENSOR_S_VALID_ELEMENTS_1                   0x1E0040

#define mmMME3_CTRL_ARCH_TENSOR_S_VALID_ELEMENTS_2                   0x1E0044

#define mmMME3_CTRL_ARCH_TENSOR_S_VALID_ELEMENTS_3                   0x1E0048

#define mmMME3_CTRL_ARCH_TENSOR_S_VALID_ELEMENTS_4                   0x1E004C

#define mmMME3_CTRL_ARCH_TENSOR_S_LOOP_STRIDE_0                      0x1E0050

#define mmMME3_CTRL_ARCH_TENSOR_S_LOOP_STRIDE_1                      0x1E0054

#define mmMME3_CTRL_ARCH_TENSOR_S_LOOP_STRIDE_2                      0x1E0058

#define mmMME3_CTRL_ARCH_TENSOR_S_LOOP_STRIDE_3                      0x1E005C

#define mmMME3_CTRL_ARCH_TENSOR_S_LOOP_STRIDE_4                      0x1E0060

#define mmMME3_CTRL_ARCH_TENSOR_S_ROI_SIZE_0                         0x1E0064

#define mmMME3_CTRL_ARCH_TENSOR_S_ROI_SIZE_1                         0x1E0068

#define mmMME3_CTRL_ARCH_TENSOR_S_ROI_SIZE_2                         0x1E006C

#define mmMME3_CTRL_ARCH_TENSOR_S_ROI_SIZE_3                         0x1E0070

#define mmMME3_CTRL_ARCH_TENSOR_S_SPATIAL_STRIDES_0                  0x1E0074

#define mmMME3_CTRL_ARCH_TENSOR_S_SPATIAL_STRIDES_1                  0x1E0078

#define mmMME3_CTRL_ARCH_TENSOR_S_SPATIAL_STRIDES_2                  0x1E007C

#define mmMME3_CTRL_ARCH_TENSOR_S_SPATIAL_STRIDES_3                  0x1E0080

#define mmMME3_CTRL_ARCH_TENSOR_S_SPATIAL_SIZE_MINUS_1               0x1E0084

#define mmMME3_CTRL_ARCH_AGU_S_ROI_BASE_OFFSET_0                     0x1E0088

#define mmMME3_CTRL_ARCH_AGU_S_ROI_BASE_OFFSET_1                     0x1E008C

#define mmMME3_CTRL_ARCH_AGU_S_ROI_BASE_OFFSET_2                     0x1E0090

#define mmMME3_CTRL_ARCH_AGU_S_ROI_BASE_OFFSET_3                     0x1E0094

#define mmMME3_CTRL_ARCH_AGU_S_ROI_BASE_OFFSET_4                     0x1E0098

#define mmMME3_CTRL_ARCH_AGU_S_START_OFFSET_0                        0x1E009C

#define mmMME3_CTRL_ARCH_AGU_S_START_OFFSET_1                        0x1E00A0

#define mmMME3_CTRL_ARCH_AGU_S_START_OFFSET_2                        0x1E00A4

#define mmMME3_CTRL_ARCH_AGU_S_START_OFFSET_3                        0x1E00A8

#define mmMME3_CTRL_ARCH_TENSOR_L_VALID_ELEMENTS_0                   0x1E00AC

#define mmMME3_CTRL_ARCH_TENSOR_L_VALID_ELEMENTS_1                   0x1E00B0

#define mmMME3_CTRL_ARCH_TENSOR_L_VALID_ELEMENTS_2                   0x1E00B4

#define mmMME3_CTRL_ARCH_TENSOR_L_VALID_ELEMENTS_3                   0x1E00B8

#define mmMME3_CTRL_ARCH_TENSOR_L_VALID_ELEMENTS_4                   0x1E00BC

#define mmMME3_CTRL_ARCH_TENSOR_L_LOOP_STRIDE_0                      0x1E00C0

#define mmMME3_CTRL_ARCH_TENSOR_L_LOOP_STRIDE_1                      0x1E00C4

#define mmMME3_CTRL_ARCH_TENSOR_L_LOOP_STRIDE_2                      0x1E00C8

#define mmMME3_CTRL_ARCH_TENSOR_L_LOOP_STRIDE_3                      0x1E00CC

#define mmMME3_CTRL_ARCH_TENSOR_L_LOOP_STRIDE_4                      0x1E00D0

#define mmMME3_CTRL_ARCH_TENSOR_L_ROI_SIZE_0                         0x1E00D4

#define mmMME3_CTRL_ARCH_TENSOR_L_ROI_SIZE_1                         0x1E00D8

#define mmMME3_CTRL_ARCH_TENSOR_L_ROI_SIZE_2                         0x1E00DC

#define mmMME3_CTRL_ARCH_TENSOR_L_ROI_SIZE_3                         0x1E00E0

#define mmMME3_CTRL_ARCH_TENSOR_L_SPATIAL_STRIDES_0                  0x1E00E4

#define mmMME3_CTRL_ARCH_TENSOR_L_SPATIAL_STRIDES_1                  0x1E00E8

#define mmMME3_CTRL_ARCH_TENSOR_L_SPATIAL_STRIDES_2                  0x1E00EC

#define mmMME3_CTRL_ARCH_TENSOR_L_SPATIAL_STRIDES_3                  0x1E00F0

#define mmMME3_CTRL_ARCH_TENSOR_L_SPATIAL_SIZE_MINUS_1               0x1E00F4

#define mmMME3_CTRL_ARCH_AGU_L_LOCAL_ROI_BASE_OFFSET_0               0x1E00F8

#define mmMME3_CTRL_ARCH_AGU_L_LOCAL_ROI_BASE_OFFSET_1               0x1E00FC

#define mmMME3_CTRL_ARCH_AGU_L_LOCAL_ROI_BASE_OFFSET_2               0x1E0100

#define mmMME3_CTRL_ARCH_AGU_L_LOCAL_ROI_BASE_OFFSET_3               0x1E0104

#define mmMME3_CTRL_ARCH_AGU_L_LOCAL_ROI_BASE_OFFSET_4               0x1E0108

#define mmMME3_CTRL_ARCH_AGU_L_LOCAL_START_OFFSET_0                  0x1E010C

#define mmMME3_CTRL_ARCH_AGU_L_LOCAL_START_OFFSET_1                  0x1E0110

#define mmMME3_CTRL_ARCH_AGU_L_LOCAL_START_OFFSET_2                  0x1E0114

#define mmMME3_CTRL_ARCH_AGU_L_LOCAL_START_OFFSET_3                  0x1E0118

#define mmMME3_CTRL_ARCH_AGU_L_REMOTE_ROI_BASE_OFFSET_0              0x1E011C

#define mmMME3_CTRL_ARCH_AGU_L_REMOTE_ROI_BASE_OFFSET_1              0x1E0120

#define mmMME3_CTRL_ARCH_AGU_L_REMOTE_ROI_BASE_OFFSET_2              0x1E0124

#define mmMME3_CTRL_ARCH_AGU_L_REMOTE_ROI_BASE_OFFSET_3              0x1E0128

#define mmMME3_CTRL_ARCH_AGU_L_REMOTE_ROI_BASE_OFFSET_4              0x1E012C

#define mmMME3_CTRL_ARCH_AGU_L_REMOTE_START_OFFSET_0                 0x1E0130

#define mmMME3_CTRL_ARCH_AGU_L_REMOTE_START_OFFSET_1                 0x1E0134

#define mmMME3_CTRL_ARCH_AGU_L_REMOTE_START_OFFSET_2                 0x1E0138

#define mmMME3_CTRL_ARCH_AGU_L_REMOTE_START_OFFSET_3                 0x1E013C

#define mmMME3_CTRL_ARCH_TENSOR_O_VALID_ELEMENTS_0                   0x1E0140

#define mmMME3_CTRL_ARCH_TENSOR_O_VALID_ELEMENTS_1                   0x1E0144

#define mmMME3_CTRL_ARCH_TENSOR_O_VALID_ELEMENTS_2                   0x1E0148

#define mmMME3_CTRL_ARCH_TENSOR_O_VALID_ELEMENTS_3                   0x1E014C

#define mmMME3_CTRL_ARCH_TENSOR_O_VALID_ELEMENTS_4                   0x1E0150

#define mmMME3_CTRL_ARCH_TENSOR_O_LOOP_STRIDE_0                      0x1E0154

#define mmMME3_CTRL_ARCH_TENSOR_O_LOOP_STRIDE_1                      0x1E0158

#define mmMME3_CTRL_ARCH_TENSOR_O_LOOP_STRIDE_2                      0x1E015C

#define mmMME3_CTRL_ARCH_TENSOR_O_LOOP_STRIDE_3                      0x1E0160

#define mmMME3_CTRL_ARCH_TENSOR_O_LOOP_STRIDE_4                      0x1E0164

#define mmMME3_CTRL_ARCH_TENSOR_O_ROI_SIZE_0                         0x1E0168

#define mmMME3_CTRL_ARCH_TENSOR_O_ROI_SIZE_1                         0x1E016C

#define mmMME3_CTRL_ARCH_TENSOR_O_ROI_SIZE_2                         0x1E0170

#define mmMME3_CTRL_ARCH_TENSOR_O_ROI_SIZE_3                         0x1E0174

#define mmMME3_CTRL_ARCH_TENSOR_O_SPATIAL_STRIDES_0                  0x1E0178

#define mmMME3_CTRL_ARCH_TENSOR_O_SPATIAL_STRIDES_1                  0x1E017C

#define mmMME3_CTRL_ARCH_TENSOR_O_SPATIAL_STRIDES_2                  0x1E0180

#define mmMME3_CTRL_ARCH_TENSOR_O_SPATIAL_STRIDES_3                  0x1E0184

#define mmMME3_CTRL_ARCH_TENSOR_O_SPATIAL_SIZE_MINUS_1               0x1E0188

#define mmMME3_CTRL_ARCH_AGU_O_LOCAL_ROI_BASE_OFFSET_0               0x1E018C

#define mmMME3_CTRL_ARCH_AGU_O_LOCAL_ROI_BASE_OFFSET_1               0x1E0190

#define mmMME3_CTRL_ARCH_AGU_O_LOCAL_ROI_BASE_OFFSET_2               0x1E0194

#define mmMME3_CTRL_ARCH_AGU_O_LOCAL_ROI_BASE_OFFSET_3               0x1E0198

#define mmMME3_CTRL_ARCH_AGU_O_LOCAL_ROI_BASE_OFFSET_4               0x1E019C

#define mmMME3_CTRL_ARCH_AGU_O_LOCAL_START_OFFSET_0                  0x1E01A0

#define mmMME3_CTRL_ARCH_AGU_O_LOCAL_START_OFFSET_1                  0x1E01A4

#define mmMME3_CTRL_ARCH_AGU_O_LOCAL_START_OFFSET_2                  0x1E01A8

#define mmMME3_CTRL_ARCH_AGU_O_LOCAL_START_OFFSET_3                  0x1E01AC

#define mmMME3_CTRL_ARCH_AGU_O_REMOTE_ROI_BASE_OFFSET_0              0x1E01B0

#define mmMME3_CTRL_ARCH_AGU_O_REMOTE_ROI_BASE_OFFSET_1              0x1E01B4

#define mmMME3_CTRL_ARCH_AGU_O_REMOTE_ROI_BASE_OFFSET_2              0x1E01B8

#define mmMME3_CTRL_ARCH_AGU_O_REMOTE_ROI_BASE_OFFSET_3              0x1E01BC

#define mmMME3_CTRL_ARCH_AGU_O_REMOTE_ROI_BASE_OFFSET_4              0x1E01C0

#define mmMME3_CTRL_ARCH_AGU_O_REMOTE_START_OFFSET_0                 0x1E01C4

#define mmMME3_CTRL_ARCH_AGU_O_REMOTE_START_OFFSET_1                 0x1E01C8

#define mmMME3_CTRL_ARCH_AGU_O_REMOTE_START_OFFSET_2                 0x1E01CC

#define mmMME3_CTRL_ARCH_AGU_O_REMOTE_START_OFFSET_3                 0x1E01D0

#define mmMME3_CTRL_ARCH_DESC_SB_REPEAT                              0x1E01D4

#define mmMME3_CTRL_ARCH_DESC_RATE_LIMITER                           0x1E01D8

#define mmMME3_CTRL_ARCH_DESC_SYNC_OBJECT_ADDR_LOW_LOCAL             0x1E01DC

#define mmMME3_CTRL_ARCH_DESC_SYNC_OBJECT_ADDR_LOW_REMOTE            0x1E01E0

#define mmMME3_CTRL_ARCH_DESC_SYNC_OBJECT_ADDR_HIGH                  0x1E01E4

#define mmMME3_CTRL_ARCH_DESC_SYNC_OBJECT_DATA                       0x1E01E8

#define mmMME3_CTRL_ARCH_DESC_AXI_USER_DATA                          0x1E01EC

#define mmMME3_CTRL_ARCH_DESC_PERF_EVT_S                             0x1E01F0

#define mmMME3_CTRL_ARCH_DESC_PERF_EVT_L_LOCAL                       0x1E01F4

#define mmMME3_CTRL_ARCH_DESC_PERF_EVT_L_REMOTE                      0x1E01F8

#define mmMME3_CTRL_ARCH_DESC_PERF_EVT_O_LOCAL                       0x1E01FC

#define mmMME3_CTRL_ARCH_DESC_PERF_EVT_O_REMOTE                      0x1E0200

#define mmMME3_CTRL_ARCH_DESC_PADDING_VALUE_S                        0x1E0204

#define mmMME3_CTRL_ARCH_DESC_PADDING_VALUE_L                        0x1E0208

#define mmMME3_CTRL_ARCH_DESC_META_DATA_AGU_S                        0x1E020C

#define mmMME3_CTRL_ARCH_DESC_META_DATA_AGU_L_LOCAL                  0x1E0210

#define mmMME3_CTRL_ARCH_DESC_META_DATA_AGU_L_REMOTE                 0x1E0214

#define mmMME3_CTRL_ARCH_DESC_META_DATA_AGU_O_LOCAL                  0x1E0218

#define mmMME3_CTRL_ARCH_DESC_META_DATA_AGU_O_REMOTE                 0x1E021C

#define mmMME3_CTRL_ARCH_DESC_PCU_RL_SATURATION                      0x1E0220

#define mmMME3_CTRL_ARCH_DESC_DUMMY                                  0x1E0224

#define mmMME3_CTRL_CMD                                              0x1E0280

#define mmMME3_CTRL_STATUS1                                          0x1E0284

#define mmMME3_CTRL_RESET                                            0x1E0288

#define mmMME3_CTRL_QM_STALL                                         0x1E028C

#define mmMME3_CTRL_SYNC_OBJECT_FIFO_TH                              0x1E0290

#define mmMME3_CTRL_EUS_ROLLUP_CNT_ADD                               0x1E0294

#define mmMME3_CTRL_INTR_CAUSE                                       0x1E0298

#define mmMME3_CTRL_INTR_MASK                                        0x1E029C

#define mmMME3_CTRL_LOG_SHADOW                                       0x1E02A0

#define mmMME3_CTRL_PCU_RL_DESC0                                     0x1E02A4

#define mmMME3_CTRL_PCU_RL_TOKEN_UPDATE                              0x1E02A8

#define mmMME3_CTRL_PCU_RL_TH                                        0x1E02AC

#define mmMME3_CTRL_PCU_RL_MIN                                       0x1E02B0

#define mmMME3_CTRL_PCU_RL_CTRL_EN                                   0x1E02B4

#define mmMME3_CTRL_PCU_RL_HISTORY_LOG_SIZE                          0x1E02B8

#define mmMME3_CTRL_PCU_DUMMY_A_BF16                                 0x1E02BC

#define mmMME3_CTRL_PCU_DUMMY_B_BF16                                 0x1E02C0

#define mmMME3_CTRL_PCU_DUMMY_A_FP32_ODD                             0x1E02C4

#define mmMME3_CTRL_PCU_DUMMY_A_FP32_EVEN                            0x1E02C8

#define mmMME3_CTRL_PCU_DUMMY_B_FP32_ODD                             0x1E02CC

#define mmMME3_CTRL_PCU_DUMMY_B_FP32_EVEN                            0x1E02D0

#define mmMME3_CTRL_PROT                                             0x1E02D4

#define mmMME3_CTRL_EU_POWER_SAVE_DISABLE                            0x1E02D8

#define mmMME3_CTRL_CS_DBG_BLOCK_ID                                  0x1E02DC

#define mmMME3_CTRL_CS_DBG_STATUS_DROP_CNT                           0x1E02E0

#define mmMME3_CTRL_TE_CLOSE_CGATE                                   0x1E02E4

#define mmMME3_CTRL_AGU_SM_INFLIGHT_CNTR                             0x1E02E8

#define mmMME3_CTRL_AGU_SM_TOTAL_CNTR                                0x1E02EC

#define mmMME3_CTRL_EZSYNC_OUT_CREDIT                                0x1E02F0

#define mmMME3_CTRL_PCU_RL_SAT_SEC                                   0x1E02F4

#define mmMME3_CTRL_AGU_SYNC_MSG_AXI_USER                            0x1E02F8

#define mmMME3_CTRL_QM_SLV_LBW_CLK_EN                                0x1E02FC

#define mmMME3_CTRL_SHADOW_0_STATUS                                  0x1E0400

#define mmMME3_CTRL_SHADOW_0_BASE_ADDR_HIGH_S                        0x1E0408

#define mmMME3_CTRL_SHADOW_0_BASE_ADDR_HIGH_L                        0x1E040C

#define mmMME3_CTRL_SHADOW_0_BASE_ADDR_HIGH_O                        0x1E0410

#define mmMME3_CTRL_SHADOW_0_BASE_ADDR_LOW_S                         0x1E0414

#define mmMME3_CTRL_SHADOW_0_BASE_ADDR_LOW_L                         0x1E0418

#define mmMME3_CTRL_SHADOW_0_BASE_ADDR_LOW_O                         0x1E041C

#define mmMME3_CTRL_SHADOW_0_HEADER_LOW                              0x1E0420

#define mmMME3_CTRL_SHADOW_0_HEADER_HIGH                             0x1E0424

#define mmMME3_CTRL_SHADOW_0_CONV_KERNEL_SIZE_MINUS_1                0x1E0428

#define mmMME3_CTRL_SHADOW_0_CONV_ASSOCIATED_DIMS_LOW                0x1E042C

#define mmMME3_CTRL_SHADOW_0_CONV_ASSOCIATED_DIMS_HIGH               0x1E0430

#define mmMME3_CTRL_SHADOW_0_NUM_ITERATIONS_MINUS_1                  0x1E0434

#define mmMME3_CTRL_SHADOW_0_OUTER_LOOP                              0x1E0438

#define mmMME3_CTRL_SHADOW_0_TENSOR_S_VALID_ELEMENTS_0               0x1E043C

#define mmMME3_CTRL_SHADOW_0_TENSOR_S_VALID_ELEMENTS_1               0x1E0440

#define mmMME3_CTRL_SHADOW_0_TENSOR_S_VALID_ELEMENTS_2               0x1E0444

#define mmMME3_CTRL_SHADOW_0_TENSOR_S_VALID_ELEMENTS_3               0x1E0448

#define mmMME3_CTRL_SHADOW_0_TENSOR_S_VALID_ELEMENTS_4               0x1E044C

#define mmMME3_CTRL_SHADOW_0_TENSOR_S_LOOP_STRIDE_0                  0x1E0450

#define mmMME3_CTRL_SHADOW_0_TENSOR_S_LOOP_STRIDE_1                  0x1E0454

#define mmMME3_CTRL_SHADOW_0_TENSOR_S_LOOP_STRIDE_2                  0x1E0458

#define mmMME3_CTRL_SHADOW_0_TENSOR_S_LOOP_STRIDE_3                  0x1E045C

#define mmMME3_CTRL_SHADOW_0_TENSOR_S_LOOP_STRIDE_4                  0x1E0460

#define mmMME3_CTRL_SHADOW_0_TENSOR_S_ROI_SIZE_0                     0x1E0464

#define mmMME3_CTRL_SHADOW_0_TENSOR_S_ROI_SIZE_1                     0x1E0468

#define mmMME3_CTRL_SHADOW_0_TENSOR_S_ROI_SIZE_2                     0x1E046C

#define mmMME3_CTRL_SHADOW_0_TENSOR_S_ROI_SIZE_3                     0x1E0470

#define mmMME3_CTRL_SHADOW_0_TENSOR_S_SPATIAL_STRIDES_0              0x1E0474

#define mmMME3_CTRL_SHADOW_0_TENSOR_S_SPATIAL_STRIDES_1              0x1E0478

#define mmMME3_CTRL_SHADOW_0_TENSOR_S_SPATIAL_STRIDES_2              0x1E047C

#define mmMME3_CTRL_SHADOW_0_TENSOR_S_SPATIAL_STRIDES_3              0x1E0480

#define mmMME3_CTRL_SHADOW_0_TENSOR_S_SPATIAL_SIZE_MINUS_1           0x1E0484

#define mmMME3_CTRL_SHADOW_0_AGU_S_ROI_BASE_OFFSET_0                 0x1E0488

#define mmMME3_CTRL_SHADOW_0_AGU_S_ROI_BASE_OFFSET_1                 0x1E048C

#define mmMME3_CTRL_SHADOW_0_AGU_S_ROI_BASE_OFFSET_2                 0x1E0490

#define mmMME3_CTRL_SHADOW_0_AGU_S_ROI_BASE_OFFSET_3                 0x1E0494

#define mmMME3_CTRL_SHADOW_0_AGU_S_ROI_BASE_OFFSET_4                 0x1E0498

#define mmMME3_CTRL_SHADOW_0_AGU_S_START_OFFSET_0                    0x1E049C

#define mmMME3_CTRL_SHADOW_0_AGU_S_START_OFFSET_1                    0x1E04A0

#define mmMME3_CTRL_SHADOW_0_AGU_S_START_OFFSET_2                    0x1E04A4

#define mmMME3_CTRL_SHADOW_0_AGU_S_START_OFFSET_3                    0x1E04A8

#define mmMME3_CTRL_SHADOW_0_TENSOR_L_VALID_ELEMENTS_0               0x1E04AC

#define mmMME3_CTRL_SHADOW_0_TENSOR_L_VALID_ELEMENTS_1               0x1E04B0

#define mmMME3_CTRL_SHADOW_0_TENSOR_L_VALID_ELEMENTS_2               0x1E04B4

#define mmMME3_CTRL_SHADOW_0_TENSOR_L_VALID_ELEMENTS_3               0x1E04B8

#define mmMME3_CTRL_SHADOW_0_TENSOR_L_VALID_ELEMENTS_4               0x1E04BC

#define mmMME3_CTRL_SHADOW_0_TENSOR_L_LOOP_STRIDE_0                  0x1E04C0

#define mmMME3_CTRL_SHADOW_0_TENSOR_L_LOOP_STRIDE_1                  0x1E04C4

#define mmMME3_CTRL_SHADOW_0_TENSOR_L_LOOP_STRIDE_2                  0x1E04C8

#define mmMME3_CTRL_SHADOW_0_TENSOR_L_LOOP_STRIDE_3                  0x1E04CC

#define mmMME3_CTRL_SHADOW_0_TENSOR_L_LOOP_STRIDE_4                  0x1E04D0

#define mmMME3_CTRL_SHADOW_0_TENSOR_L_ROI_SIZE_0                     0x1E04D4

#define mmMME3_CTRL_SHADOW_0_TENSOR_L_ROI_SIZE_1                     0x1E04D8

#define mmMME3_CTRL_SHADOW_0_TENSOR_L_ROI_SIZE_2                     0x1E04DC

#define mmMME3_CTRL_SHADOW_0_TENSOR_L_ROI_SIZE_3                     0x1E04E0

#define mmMME3_CTRL_SHADOW_0_TENSOR_L_SPATIAL_STRIDES_0              0x1E04E4

#define mmMME3_CTRL_SHADOW_0_TENSOR_L_SPATIAL_STRIDES_1              0x1E04E8

#define mmMME3_CTRL_SHADOW_0_TENSOR_L_SPATIAL_STRIDES_2              0x1E04EC

#define mmMME3_CTRL_SHADOW_0_TENSOR_L_SPATIAL_STRIDES_3              0x1E04F0

#define mmMME3_CTRL_SHADOW_0_TENSOR_L_SPATIAL_SIZE_MINUS_1           0x1E04F4

#define mmMME3_CTRL_SHADOW_0_AGU_L_LOCAL_ROI_BASE_OFFSET_0           0x1E04F8

#define mmMME3_CTRL_SHADOW_0_AGU_L_LOCAL_ROI_BASE_OFFSET_1           0x1E04FC

#define mmMME3_CTRL_SHADOW_0_AGU_L_LOCAL_ROI_BASE_OFFSET_2           0x1E0500

#define mmMME3_CTRL_SHADOW_0_AGU_L_LOCAL_ROI_BASE_OFFSET_3           0x1E0504

#define mmMME3_CTRL_SHADOW_0_AGU_L_LOCAL_ROI_BASE_OFFSET_4           0x1E0508

#define mmMME3_CTRL_SHADOW_0_AGU_L_LOCAL_START_OFFSET_0              0x1E050C

#define mmMME3_CTRL_SHADOW_0_AGU_L_LOCAL_START_OFFSET_1              0x1E0510

#define mmMME3_CTRL_SHADOW_0_AGU_L_LOCAL_START_OFFSET_2              0x1E0514

#define mmMME3_CTRL_SHADOW_0_AGU_L_LOCAL_START_OFFSET_3              0x1E0518

#define mmMME3_CTRL_SHADOW_0_AGU_L_REMOTE_ROI_BASE_OFFSET_0          0x1E051C

#define mmMME3_CTRL_SHADOW_0_AGU_L_REMOTE_ROI_BASE_OFFSET_1          0x1E0520

#define mmMME3_CTRL_SHADOW_0_AGU_L_REMOTE_ROI_BASE_OFFSET_2          0x1E0524

#define mmMME3_CTRL_SHADOW_0_AGU_L_REMOTE_ROI_BASE_OFFSET_3          0x1E0528

#define mmMME3_CTRL_SHADOW_0_AGU_L_REMOTE_ROI_BASE_OFFSET_4          0x1E052C

#define mmMME3_CTRL_SHADOW_0_AGU_L_REMOTE_START_OFFSET_0             0x1E0530

#define mmMME3_CTRL_SHADOW_0_AGU_L_REMOTE_START_OFFSET_1             0x1E0534

#define mmMME3_CTRL_SHADOW_0_AGU_L_REMOTE_START_OFFSET_2             0x1E0538

#define mmMME3_CTRL_SHADOW_0_AGU_L_REMOTE_START_OFFSET_3             0x1E053C

#define mmMME3_CTRL_SHADOW_0_TENSOR_O_VALID_ELEMENTS_0               0x1E0540

#define mmMME3_CTRL_SHADOW_0_TENSOR_O_VALID_ELEMENTS_1               0x1E0544

#define mmMME3_CTRL_SHADOW_0_TENSOR_O_VALID_ELEMENTS_2               0x1E0548

#define mmMME3_CTRL_SHADOW_0_TENSOR_O_VALID_ELEMENTS_3               0x1E054C

#define mmMME3_CTRL_SHADOW_0_TENSOR_O_VALID_ELEMENTS_4               0x1E0550

#define mmMME3_CTRL_SHADOW_0_TENSOR_O_LOOP_STRIDE_0                  0x1E0554

#define mmMME3_CTRL_SHADOW_0_TENSOR_O_LOOP_STRIDE_1                  0x1E0558

#define mmMME3_CTRL_SHADOW_0_TENSOR_O_LOOP_STRIDE_2                  0x1E055C

#define mmMME3_CTRL_SHADOW_0_TENSOR_O_LOOP_STRIDE_3                  0x1E0560

#define mmMME3_CTRL_SHADOW_0_TENSOR_O_LOOP_STRIDE_4                  0x1E0564

#define mmMME3_CTRL_SHADOW_0_TENSOR_O_ROI_SIZE_0                     0x1E0568

#define mmMME3_CTRL_SHADOW_0_TENSOR_O_ROI_SIZE_1                     0x1E056C

#define mmMME3_CTRL_SHADOW_0_TENSOR_O_ROI_SIZE_2                     0x1E0570

#define mmMME3_CTRL_SHADOW_0_TENSOR_O_ROI_SIZE_3                     0x1E0574

#define mmMME3_CTRL_SHADOW_0_TENSOR_O_SPATIAL_STRIDES_0              0x1E0578

#define mmMME3_CTRL_SHADOW_0_TENSOR_O_SPATIAL_STRIDES_1              0x1E057C

#define mmMME3_CTRL_SHADOW_0_TENSOR_O_SPATIAL_STRIDES_2              0x1E0580

#define mmMME3_CTRL_SHADOW_0_TENSOR_O_SPATIAL_STRIDES_3              0x1E0584

#define mmMME3_CTRL_SHADOW_0_TENSOR_O_SPATIAL_SIZE_MINUS_1           0x1E0588

#define mmMME3_CTRL_SHADOW_0_AGU_O_LOCAL_ROI_BASE_OFFSET_0           0x1E058C

#define mmMME3_CTRL_SHADOW_0_AGU_O_LOCAL_ROI_BASE_OFFSET_1           0x1E0590

#define mmMME3_CTRL_SHADOW_0_AGU_O_LOCAL_ROI_BASE_OFFSET_2           0x1E0594

#define mmMME3_CTRL_SHADOW_0_AGU_O_LOCAL_ROI_BASE_OFFSET_3           0x1E0598

#define mmMME3_CTRL_SHADOW_0_AGU_O_LOCAL_ROI_BASE_OFFSET_4           0x1E059C

#define mmMME3_CTRL_SHADOW_0_AGU_O_LOCAL_START_OFFSET_0              0x1E05A0

#define mmMME3_CTRL_SHADOW_0_AGU_O_LOCAL_START_OFFSET_1              0x1E05A4

#define mmMME3_CTRL_SHADOW_0_AGU_O_LOCAL_START_OFFSET_2              0x1E05A8

#define mmMME3_CTRL_SHADOW_0_AGU_O_LOCAL_START_OFFSET_3              0x1E05AC

#define mmMME3_CTRL_SHADOW_0_AGU_O_REMOTE_ROI_BASE_OFFSET_0          0x1E05B0

#define mmMME3_CTRL_SHADOW_0_AGU_O_REMOTE_ROI_BASE_OFFSET_1          0x1E05B4

#define mmMME3_CTRL_SHADOW_0_AGU_O_REMOTE_ROI_BASE_OFFSET_2          0x1E05B8

#define mmMME3_CTRL_SHADOW_0_AGU_O_REMOTE_ROI_BASE_OFFSET_3          0x1E05BC

#define mmMME3_CTRL_SHADOW_0_AGU_O_REMOTE_ROI_BASE_OFFSET_4          0x1E05C0

#define mmMME3_CTRL_SHADOW_0_AGU_O_REMOTE_START_OFFSET_0             0x1E05C4

#define mmMME3_CTRL_SHADOW_0_AGU_O_REMOTE_START_OFFSET_1             0x1E05C8

#define mmMME3_CTRL_SHADOW_0_AGU_O_REMOTE_START_OFFSET_2             0x1E05CC

#define mmMME3_CTRL_SHADOW_0_AGU_O_REMOTE_START_OFFSET_3             0x1E05D0

#define mmMME3_CTRL_SHADOW_0_DESC_SB_REPEAT                          0x1E05D4

#define mmMME3_CTRL_SHADOW_0_DESC_RATE_LIMITER                       0x1E05D8

#define mmMME3_CTRL_SHADOW_0_DESC_SYNC_OBJECT_ADDR_LOW_LOCAL         0x1E05DC

#define mmMME3_CTRL_SHADOW_0_DESC_SYNC_OBJECT_ADDR_LOW_REMOTE        0x1E05E0

#define mmMME3_CTRL_SHADOW_0_DESC_SYNC_OBJECT_ADDR_HIGH              0x1E05E4

#define mmMME3_CTRL_SHADOW_0_DESC_SYNC_OBJECT_DATA                   0x1E05E8

#define mmMME3_CTRL_SHADOW_0_DESC_AXI_USER_DATA                      0x1E05EC

#define mmMME3_CTRL_SHADOW_0_DESC_PERF_EVT_S                         0x1E05F0

#define mmMME3_CTRL_SHADOW_0_DESC_PERF_EVT_L_LOCAL                   0x1E05F4

#define mmMME3_CTRL_SHADOW_0_DESC_PERF_EVT_L_REMOTE                  0x1E05F8

#define mmMME3_CTRL_SHADOW_0_DESC_PERF_EVT_O_LOCAL                   0x1E05FC

#define mmMME3_CTRL_SHADOW_0_DESC_PERF_EVT_O_REMOTE                  0x1E0600

#define mmMME3_CTRL_SHADOW_0_DESC_PADDING_VALUE_S                    0x1E0604

#define mmMME3_CTRL_SHADOW_0_DESC_PADDING_VALUE_L                    0x1E0608

#define mmMME3_CTRL_SHADOW_0_DESC_META_DATA_AGU_S                    0x1E060C

#define mmMME3_CTRL_SHADOW_0_DESC_META_DATA_AGU_L_LOCAL              0x1E0610

#define mmMME3_CTRL_SHADOW_0_DESC_META_DATA_AGU_L_REMOTE             0x1E0614

#define mmMME3_CTRL_SHADOW_0_DESC_META_DATA_AGU_O_LOCAL              0x1E0618

#define mmMME3_CTRL_SHADOW_0_DESC_META_DATA_AGU_O_REMOTE             0x1E061C

#define mmMME3_CTRL_SHADOW_0_DESC_PCU_RL_SATURATION                  0x1E0620

#define mmMME3_CTRL_SHADOW_0_DESC_DUMMY                              0x1E0624

#define mmMME3_CTRL_SHADOW_1_STATUS                                  0x1E0680

#define mmMME3_CTRL_SHADOW_1_BASE_ADDR_HIGH_S                        0x1E0688

#define mmMME3_CTRL_SHADOW_1_BASE_ADDR_HIGH_L                        0x1E068C

#define mmMME3_CTRL_SHADOW_1_BASE_ADDR_HIGH_O                        0x1E0690

#define mmMME3_CTRL_SHADOW_1_BASE_ADDR_LOW_S                         0x1E0694

#define mmMME3_CTRL_SHADOW_1_BASE_ADDR_LOW_L                         0x1E0698

#define mmMME3_CTRL_SHADOW_1_BASE_ADDR_LOW_O                         0x1E069C

#define mmMME3_CTRL_SHADOW_1_HEADER_LOW                              0x1E06A0

#define mmMME3_CTRL_SHADOW_1_HEADER_HIGH                             0x1E06A4

#define mmMME3_CTRL_SHADOW_1_CONV_KERNEL_SIZE_MINUS_1                0x1E06A8

#define mmMME3_CTRL_SHADOW_1_CONV_ASSOCIATED_DIMS_LOW                0x1E06AC

#define mmMME3_CTRL_SHADOW_1_CONV_ASSOCIATED_DIMS_HIGH               0x1E06B0

#define mmMME3_CTRL_SHADOW_1_NUM_ITERATIONS_MINUS_1                  0x1E06B4

#define mmMME3_CTRL_SHADOW_1_OUTER_LOOP                              0x1E06B8

#define mmMME3_CTRL_SHADOW_1_TENSOR_S_VALID_ELEMENTS_0               0x1E06BC

#define mmMME3_CTRL_SHADOW_1_TENSOR_S_VALID_ELEMENTS_1               0x1E06C0

#define mmMME3_CTRL_SHADOW_1_TENSOR_S_VALID_ELEMENTS_2               0x1E06C4

#define mmMME3_CTRL_SHADOW_1_TENSOR_S_VALID_ELEMENTS_3               0x1E06C8

#define mmMME3_CTRL_SHADOW_1_TENSOR_S_VALID_ELEMENTS_4               0x1E06CC

#define mmMME3_CTRL_SHADOW_1_TENSOR_S_LOOP_STRIDE_0                  0x1E06D0

#define mmMME3_CTRL_SHADOW_1_TENSOR_S_LOOP_STRIDE_1                  0x1E06D4

#define mmMME3_CTRL_SHADOW_1_TENSOR_S_LOOP_STRIDE_2                  0x1E06D8

#define mmMME3_CTRL_SHADOW_1_TENSOR_S_LOOP_STRIDE_3                  0x1E06DC

#define mmMME3_CTRL_SHADOW_1_TENSOR_S_LOOP_STRIDE_4                  0x1E06E0

#define mmMME3_CTRL_SHADOW_1_TENSOR_S_ROI_SIZE_0                     0x1E06E4

#define mmMME3_CTRL_SHADOW_1_TENSOR_S_ROI_SIZE_1                     0x1E06E8

#define mmMME3_CTRL_SHADOW_1_TENSOR_S_ROI_SIZE_2                     0x1E06EC

#define mmMME3_CTRL_SHADOW_1_TENSOR_S_ROI_SIZE_3                     0x1E06F0

#define mmMME3_CTRL_SHADOW_1_TENSOR_S_SPATIAL_STRIDES_0              0x1E06F4

#define mmMME3_CTRL_SHADOW_1_TENSOR_S_SPATIAL_STRIDES_1              0x1E06F8

#define mmMME3_CTRL_SHADOW_1_TENSOR_S_SPATIAL_STRIDES_2              0x1E06FC

#define mmMME3_CTRL_SHADOW_1_TENSOR_S_SPATIAL_STRIDES_3              0x1E0700

#define mmMME3_CTRL_SHADOW_1_TENSOR_S_SPATIAL_SIZE_MINUS_1           0x1E0704

#define mmMME3_CTRL_SHADOW_1_AGU_S_ROI_BASE_OFFSET_0                 0x1E0708

#define mmMME3_CTRL_SHADOW_1_AGU_S_ROI_BASE_OFFSET_1                 0x1E070C

#define mmMME3_CTRL_SHADOW_1_AGU_S_ROI_BASE_OFFSET_2                 0x1E0710

#define mmMME3_CTRL_SHADOW_1_AGU_S_ROI_BASE_OFFSET_3                 0x1E0714

#define mmMME3_CTRL_SHADOW_1_AGU_S_ROI_BASE_OFFSET_4                 0x1E0718

#define mmMME3_CTRL_SHADOW_1_AGU_S_START_OFFSET_0                    0x1E071C

#define mmMME3_CTRL_SHADOW_1_AGU_S_START_OFFSET_1                    0x1E0720

#define mmMME3_CTRL_SHADOW_1_AGU_S_START_OFFSET_2                    0x1E0724

#define mmMME3_CTRL_SHADOW_1_AGU_S_START_OFFSET_3                    0x1E0728

#define mmMME3_CTRL_SHADOW_1_TENSOR_L_VALID_ELEMENTS_0               0x1E072C

#define mmMME3_CTRL_SHADOW_1_TENSOR_L_VALID_ELEMENTS_1               0x1E0730

#define mmMME3_CTRL_SHADOW_1_TENSOR_L_VALID_ELEMENTS_2               0x1E0734

#define mmMME3_CTRL_SHADOW_1_TENSOR_L_VALID_ELEMENTS_3               0x1E0738

#define mmMME3_CTRL_SHADOW_1_TENSOR_L_VALID_ELEMENTS_4               0x1E073C

#define mmMME3_CTRL_SHADOW_1_TENSOR_L_LOOP_STRIDE_0                  0x1E0740

#define mmMME3_CTRL_SHADOW_1_TENSOR_L_LOOP_STRIDE_1                  0x1E0744

#define mmMME3_CTRL_SHADOW_1_TENSOR_L_LOOP_STRIDE_2                  0x1E0748

#define mmMME3_CTRL_SHADOW_1_TENSOR_L_LOOP_STRIDE_3                  0x1E074C

#define mmMME3_CTRL_SHADOW_1_TENSOR_L_LOOP_STRIDE_4                  0x1E0750

#define mmMME3_CTRL_SHADOW_1_TENSOR_L_ROI_SIZE_0                     0x1E0754

#define mmMME3_CTRL_SHADOW_1_TENSOR_L_ROI_SIZE_1                     0x1E0758

#define mmMME3_CTRL_SHADOW_1_TENSOR_L_ROI_SIZE_2                     0x1E075C

#define mmMME3_CTRL_SHADOW_1_TENSOR_L_ROI_SIZE_3                     0x1E0760

#define mmMME3_CTRL_SHADOW_1_TENSOR_L_SPATIAL_STRIDES_0              0x1E0764

#define mmMME3_CTRL_SHADOW_1_TENSOR_L_SPATIAL_STRIDES_1              0x1E0768

#define mmMME3_CTRL_SHADOW_1_TENSOR_L_SPATIAL_STRIDES_2              0x1E076C

#define mmMME3_CTRL_SHADOW_1_TENSOR_L_SPATIAL_STRIDES_3              0x1E0770

#define mmMME3_CTRL_SHADOW_1_TENSOR_L_SPATIAL_SIZE_MINUS_1           0x1E0774

#define mmMME3_CTRL_SHADOW_1_AGU_L_LOCAL_ROI_BASE_OFFSET_0           0x1E0778

#define mmMME3_CTRL_SHADOW_1_AGU_L_LOCAL_ROI_BASE_OFFSET_1           0x1E077C

#define mmMME3_CTRL_SHADOW_1_AGU_L_LOCAL_ROI_BASE_OFFSET_2           0x1E0780

#define mmMME3_CTRL_SHADOW_1_AGU_L_LOCAL_ROI_BASE_OFFSET_3           0x1E0784

#define mmMME3_CTRL_SHADOW_1_AGU_L_LOCAL_ROI_BASE_OFFSET_4           0x1E0788

#define mmMME3_CTRL_SHADOW_1_AGU_L_LOCAL_START_OFFSET_0              0x1E078C

#define mmMME3_CTRL_SHADOW_1_AGU_L_LOCAL_START_OFFSET_1              0x1E0790

#define mmMME3_CTRL_SHADOW_1_AGU_L_LOCAL_START_OFFSET_2              0x1E0794

#define mmMME3_CTRL_SHADOW_1_AGU_L_LOCAL_START_OFFSET_3              0x1E0798

#define mmMME3_CTRL_SHADOW_1_AGU_L_REMOTE_ROI_BASE_OFFSET_0          0x1E079C

#define mmMME3_CTRL_SHADOW_1_AGU_L_REMOTE_ROI_BASE_OFFSET_1          0x1E07A0

#define mmMME3_CTRL_SHADOW_1_AGU_L_REMOTE_ROI_BASE_OFFSET_2          0x1E07A4

#define mmMME3_CTRL_SHADOW_1_AGU_L_REMOTE_ROI_BASE_OFFSET_3          0x1E07A8

#define mmMME3_CTRL_SHADOW_1_AGU_L_REMOTE_ROI_BASE_OFFSET_4          0x1E07AC

#define mmMME3_CTRL_SHADOW_1_AGU_L_REMOTE_START_OFFSET_0             0x1E07B0

#define mmMME3_CTRL_SHADOW_1_AGU_L_REMOTE_START_OFFSET_1             0x1E07B4

#define mmMME3_CTRL_SHADOW_1_AGU_L_REMOTE_START_OFFSET_2             0x1E07B8

#define mmMME3_CTRL_SHADOW_1_AGU_L_REMOTE_START_OFFSET_3             0x1E07BC

#define mmMME3_CTRL_SHADOW_1_TENSOR_O_VALID_ELEMENTS_0               0x1E07C0

#define mmMME3_CTRL_SHADOW_1_TENSOR_O_VALID_ELEMENTS_1               0x1E07C4

#define mmMME3_CTRL_SHADOW_1_TENSOR_O_VALID_ELEMENTS_2               0x1E07C8

#define mmMME3_CTRL_SHADOW_1_TENSOR_O_VALID_ELEMENTS_3               0x1E07CC

#define mmMME3_CTRL_SHADOW_1_TENSOR_O_VALID_ELEMENTS_4               0x1E07D0

#define mmMME3_CTRL_SHADOW_1_TENSOR_O_LOOP_STRIDE_0                  0x1E07D4

#define mmMME3_CTRL_SHADOW_1_TENSOR_O_LOOP_STRIDE_1                  0x1E07D8

#define mmMME3_CTRL_SHADOW_1_TENSOR_O_LOOP_STRIDE_2                  0x1E07DC

#define mmMME3_CTRL_SHADOW_1_TENSOR_O_LOOP_STRIDE_3                  0x1E07E0

#define mmMME3_CTRL_SHADOW_1_TENSOR_O_LOOP_STRIDE_4                  0x1E07E4

#define mmMME3_CTRL_SHADOW_1_TENSOR_O_ROI_SIZE_0                     0x1E07E8

#define mmMME3_CTRL_SHADOW_1_TENSOR_O_ROI_SIZE_1                     0x1E07EC

#define mmMME3_CTRL_SHADOW_1_TENSOR_O_ROI_SIZE_2                     0x1E07F0

#define mmMME3_CTRL_SHADOW_1_TENSOR_O_ROI_SIZE_3                     0x1E07F4

#define mmMME3_CTRL_SHADOW_1_TENSOR_O_SPATIAL_STRIDES_0              0x1E07F8

#define mmMME3_CTRL_SHADOW_1_TENSOR_O_SPATIAL_STRIDES_1              0x1E07FC

#define mmMME3_CTRL_SHADOW_1_TENSOR_O_SPATIAL_STRIDES_2              0x1E0800

#define mmMME3_CTRL_SHADOW_1_TENSOR_O_SPATIAL_STRIDES_3              0x1E0804

#define mmMME3_CTRL_SHADOW_1_TENSOR_O_SPATIAL_SIZE_MINUS_1           0x1E0808

#define mmMME3_CTRL_SHADOW_1_AGU_O_LOCAL_ROI_BASE_OFFSET_0           0x1E080C

#define mmMME3_CTRL_SHADOW_1_AGU_O_LOCAL_ROI_BASE_OFFSET_1           0x1E0810

#define mmMME3_CTRL_SHADOW_1_AGU_O_LOCAL_ROI_BASE_OFFSET_2           0x1E0814

#define mmMME3_CTRL_SHADOW_1_AGU_O_LOCAL_ROI_BASE_OFFSET_3           0x1E0818

#define mmMME3_CTRL_SHADOW_1_AGU_O_LOCAL_ROI_BASE_OFFSET_4           0x1E081C

#define mmMME3_CTRL_SHADOW_1_AGU_O_LOCAL_START_OFFSET_0              0x1E0820

#define mmMME3_CTRL_SHADOW_1_AGU_O_LOCAL_START_OFFSET_1              0x1E0824

#define mmMME3_CTRL_SHADOW_1_AGU_O_LOCAL_START_OFFSET_2              0x1E0828

#define mmMME3_CTRL_SHADOW_1_AGU_O_LOCAL_START_OFFSET_3              0x1E082C

#define mmMME3_CTRL_SHADOW_1_AGU_O_REMOTE_ROI_BASE_OFFSET_0          0x1E0830

#define mmMME3_CTRL_SHADOW_1_AGU_O_REMOTE_ROI_BASE_OFFSET_1          0x1E0834

#define mmMME3_CTRL_SHADOW_1_AGU_O_REMOTE_ROI_BASE_OFFSET_2          0x1E0838

#define mmMME3_CTRL_SHADOW_1_AGU_O_REMOTE_ROI_BASE_OFFSET_3          0x1E083C

#define mmMME3_CTRL_SHADOW_1_AGU_O_REMOTE_ROI_BASE_OFFSET_4          0x1E0840

#define mmMME3_CTRL_SHADOW_1_AGU_O_REMOTE_START_OFFSET_0             0x1E0844

#define mmMME3_CTRL_SHADOW_1_AGU_O_REMOTE_START_OFFSET_1             0x1E0848

#define mmMME3_CTRL_SHADOW_1_AGU_O_REMOTE_START_OFFSET_2             0x1E084C

#define mmMME3_CTRL_SHADOW_1_AGU_O_REMOTE_START_OFFSET_3             0x1E0850

#define mmMME3_CTRL_SHADOW_1_DESC_SB_REPEAT                          0x1E0854

#define mmMME3_CTRL_SHADOW_1_DESC_RATE_LIMITER                       0x1E0858

#define mmMME3_CTRL_SHADOW_1_DESC_SYNC_OBJECT_ADDR_LOW_LOCAL         0x1E085C

#define mmMME3_CTRL_SHADOW_1_DESC_SYNC_OBJECT_ADDR_LOW_REMOTE        0x1E0860

#define mmMME3_CTRL_SHADOW_1_DESC_SYNC_OBJECT_ADDR_HIGH              0x1E0864

#define mmMME3_CTRL_SHADOW_1_DESC_SYNC_OBJECT_DATA                   0x1E0868

#define mmMME3_CTRL_SHADOW_1_DESC_AXI_USER_DATA                      0x1E086C

#define mmMME3_CTRL_SHADOW_1_DESC_PERF_EVT_S                         0x1E0870

#define mmMME3_CTRL_SHADOW_1_DESC_PERF_EVT_L_LOCAL                   0x1E0874

#define mmMME3_CTRL_SHADOW_1_DESC_PERF_EVT_L_REMOTE                  0x1E0878

#define mmMME3_CTRL_SHADOW_1_DESC_PERF_EVT_O_LOCAL                   0x1E087C

#define mmMME3_CTRL_SHADOW_1_DESC_PERF_EVT_O_REMOTE                  0x1E0880

#define mmMME3_CTRL_SHADOW_1_DESC_PADDING_VALUE_S                    0x1E0884

#define mmMME3_CTRL_SHADOW_1_DESC_PADDING_VALUE_L                    0x1E0888

#define mmMME3_CTRL_SHADOW_1_DESC_META_DATA_AGU_S                    0x1E088C

#define mmMME3_CTRL_SHADOW_1_DESC_META_DATA_AGU_L_LOCAL              0x1E0890

#define mmMME3_CTRL_SHADOW_1_DESC_META_DATA_AGU_L_REMOTE             0x1E0894

#define mmMME3_CTRL_SHADOW_1_DESC_META_DATA_AGU_O_LOCAL              0x1E0898

#define mmMME3_CTRL_SHADOW_1_DESC_META_DATA_AGU_O_REMOTE             0x1E089C

#define mmMME3_CTRL_SHADOW_1_DESC_PCU_RL_SATURATION                  0x1E08A0

#define mmMME3_CTRL_SHADOW_1_DESC_DUMMY                              0x1E08A4

#define mmMME3_CTRL_SHADOW_2_STATUS                                  0x1E0900

#define mmMME3_CTRL_SHADOW_2_BASE_ADDR_HIGH_S                        0x1E0908

#define mmMME3_CTRL_SHADOW_2_BASE_ADDR_HIGH_L                        0x1E090C

#define mmMME3_CTRL_SHADOW_2_BASE_ADDR_HIGH_O                        0x1E0910

#define mmMME3_CTRL_SHADOW_2_BASE_ADDR_LOW_S                         0x1E0914

#define mmMME3_CTRL_SHADOW_2_BASE_ADDR_LOW_L                         0x1E0918

#define mmMME3_CTRL_SHADOW_2_BASE_ADDR_LOW_O                         0x1E091C

#define mmMME3_CTRL_SHADOW_2_HEADER_LOW                              0x1E0920

#define mmMME3_CTRL_SHADOW_2_HEADER_HIGH                             0x1E0924

#define mmMME3_CTRL_SHADOW_2_CONV_KERNEL_SIZE_MINUS_1                0x1E0928

#define mmMME3_CTRL_SHADOW_2_CONV_ASSOCIATED_DIMS_LOW                0x1E092C

#define mmMME3_CTRL_SHADOW_2_CONV_ASSOCIATED_DIMS_HIGH               0x1E0930

#define mmMME3_CTRL_SHADOW_2_NUM_ITERATIONS_MINUS_1                  0x1E0934

#define mmMME3_CTRL_SHADOW_2_OUTER_LOOP                              0x1E0938

#define mmMME3_CTRL_SHADOW_2_TENSOR_S_VALID_ELEMENTS_0               0x1E093C

#define mmMME3_CTRL_SHADOW_2_TENSOR_S_VALID_ELEMENTS_1               0x1E0940

#define mmMME3_CTRL_SHADOW_2_TENSOR_S_VALID_ELEMENTS_2               0x1E0944

#define mmMME3_CTRL_SHADOW_2_TENSOR_S_VALID_ELEMENTS_3               0x1E0948

#define mmMME3_CTRL_SHADOW_2_TENSOR_S_VALID_ELEMENTS_4               0x1E094C

#define mmMME3_CTRL_SHADOW_2_TENSOR_S_LOOP_STRIDE_0                  0x1E0950

#define mmMME3_CTRL_SHADOW_2_TENSOR_S_LOOP_STRIDE_1                  0x1E0954

#define mmMME3_CTRL_SHADOW_2_TENSOR_S_LOOP_STRIDE_2                  0x1E0958

#define mmMME3_CTRL_SHADOW_2_TENSOR_S_LOOP_STRIDE_3                  0x1E095C

#define mmMME3_CTRL_SHADOW_2_TENSOR_S_LOOP_STRIDE_4                  0x1E0960

#define mmMME3_CTRL_SHADOW_2_TENSOR_S_ROI_SIZE_0                     0x1E0964

#define mmMME3_CTRL_SHADOW_2_TENSOR_S_ROI_SIZE_1                     0x1E0968

#define mmMME3_CTRL_SHADOW_2_TENSOR_S_ROI_SIZE_2                     0x1E096C

#define mmMME3_CTRL_SHADOW_2_TENSOR_S_ROI_SIZE_3                     0x1E0970

#define mmMME3_CTRL_SHADOW_2_TENSOR_S_SPATIAL_STRIDES_0              0x1E0974

#define mmMME3_CTRL_SHADOW_2_TENSOR_S_SPATIAL_STRIDES_1              0x1E0978

#define mmMME3_CTRL_SHADOW_2_TENSOR_S_SPATIAL_STRIDES_2              0x1E097C

#define mmMME3_CTRL_SHADOW_2_TENSOR_S_SPATIAL_STRIDES_3              0x1E0980

#define mmMME3_CTRL_SHADOW_2_TENSOR_S_SPATIAL_SIZE_MINUS_1           0x1E0984

#define mmMME3_CTRL_SHADOW_2_AGU_S_ROI_BASE_OFFSET_0                 0x1E0988

#define mmMME3_CTRL_SHADOW_2_AGU_S_ROI_BASE_OFFSET_1                 0x1E098C

#define mmMME3_CTRL_SHADOW_2_AGU_S_ROI_BASE_OFFSET_2                 0x1E0990

#define mmMME3_CTRL_SHADOW_2_AGU_S_ROI_BASE_OFFSET_3                 0x1E0994

#define mmMME3_CTRL_SHADOW_2_AGU_S_ROI_BASE_OFFSET_4                 0x1E0998

#define mmMME3_CTRL_SHADOW_2_AGU_S_START_OFFSET_0                    0x1E099C

#define mmMME3_CTRL_SHADOW_2_AGU_S_START_OFFSET_1                    0x1E09A0

#define mmMME3_CTRL_SHADOW_2_AGU_S_START_OFFSET_2                    0x1E09A4

#define mmMME3_CTRL_SHADOW_2_AGU_S_START_OFFSET_3                    0x1E09A8

#define mmMME3_CTRL_SHADOW_2_TENSOR_L_VALID_ELEMENTS_0               0x1E09AC

#define mmMME3_CTRL_SHADOW_2_TENSOR_L_VALID_ELEMENTS_1               0x1E09B0

#define mmMME3_CTRL_SHADOW_2_TENSOR_L_VALID_ELEMENTS_2               0x1E09B4

#define mmMME3_CTRL_SHADOW_2_TENSOR_L_VALID_ELEMENTS_3               0x1E09B8

#define mmMME3_CTRL_SHADOW_2_TENSOR_L_VALID_ELEMENTS_4               0x1E09BC

#define mmMME3_CTRL_SHADOW_2_TENSOR_L_LOOP_STRIDE_0                  0x1E09C0

#define mmMME3_CTRL_SHADOW_2_TENSOR_L_LOOP_STRIDE_1                  0x1E09C4

#define mmMME3_CTRL_SHADOW_2_TENSOR_L_LOOP_STRIDE_2                  0x1E09C8

#define mmMME3_CTRL_SHADOW_2_TENSOR_L_LOOP_STRIDE_3                  0x1E09CC

#define mmMME3_CTRL_SHADOW_2_TENSOR_L_LOOP_STRIDE_4                  0x1E09D0

#define mmMME3_CTRL_SHADOW_2_TENSOR_L_ROI_SIZE_0                     0x1E09D4

#define mmMME3_CTRL_SHADOW_2_TENSOR_L_ROI_SIZE_1                     0x1E09D8

#define mmMME3_CTRL_SHADOW_2_TENSOR_L_ROI_SIZE_2                     0x1E09DC

#define mmMME3_CTRL_SHADOW_2_TENSOR_L_ROI_SIZE_3                     0x1E09E0

#define mmMME3_CTRL_SHADOW_2_TENSOR_L_SPATIAL_STRIDES_0              0x1E09E4

#define mmMME3_CTRL_SHADOW_2_TENSOR_L_SPATIAL_STRIDES_1              0x1E09E8

#define mmMME3_CTRL_SHADOW_2_TENSOR_L_SPATIAL_STRIDES_2              0x1E09EC

#define mmMME3_CTRL_SHADOW_2_TENSOR_L_SPATIAL_STRIDES_3              0x1E09F0

#define mmMME3_CTRL_SHADOW_2_TENSOR_L_SPATIAL_SIZE_MINUS_1           0x1E09F4

#define mmMME3_CTRL_SHADOW_2_AGU_L_LOCAL_ROI_BASE_OFFSET_0           0x1E09F8

#define mmMME3_CTRL_SHADOW_2_AGU_L_LOCAL_ROI_BASE_OFFSET_1           0x1E09FC

#define mmMME3_CTRL_SHADOW_2_AGU_L_LOCAL_ROI_BASE_OFFSET_2           0x1E0A00

#define mmMME3_CTRL_SHADOW_2_AGU_L_LOCAL_ROI_BASE_OFFSET_3           0x1E0A04

#define mmMME3_CTRL_SHADOW_2_AGU_L_LOCAL_ROI_BASE_OFFSET_4           0x1E0A08

#define mmMME3_CTRL_SHADOW_2_AGU_L_LOCAL_START_OFFSET_0              0x1E0A0C

#define mmMME3_CTRL_SHADOW_2_AGU_L_LOCAL_START_OFFSET_1              0x1E0A10

#define mmMME3_CTRL_SHADOW_2_AGU_L_LOCAL_START_OFFSET_2              0x1E0A14

#define mmMME3_CTRL_SHADOW_2_AGU_L_LOCAL_START_OFFSET_3              0x1E0A18

#define mmMME3_CTRL_SHADOW_2_AGU_L_REMOTE_ROI_BASE_OFFSET_0          0x1E0A1C

#define mmMME3_CTRL_SHADOW_2_AGU_L_REMOTE_ROI_BASE_OFFSET_1          0x1E0A20

#define mmMME3_CTRL_SHADOW_2_AGU_L_REMOTE_ROI_BASE_OFFSET_2          0x1E0A24

#define mmMME3_CTRL_SHADOW_2_AGU_L_REMOTE_ROI_BASE_OFFSET_3          0x1E0A28

#define mmMME3_CTRL_SHADOW_2_AGU_L_REMOTE_ROI_BASE_OFFSET_4          0x1E0A2C

#define mmMME3_CTRL_SHADOW_2_AGU_L_REMOTE_START_OFFSET_0             0x1E0A30

#define mmMME3_CTRL_SHADOW_2_AGU_L_REMOTE_START_OFFSET_1             0x1E0A34

#define mmMME3_CTRL_SHADOW_2_AGU_L_REMOTE_START_OFFSET_2             0x1E0A38

#define mmMME3_CTRL_SHADOW_2_AGU_L_REMOTE_START_OFFSET_3             0x1E0A3C

#define mmMME3_CTRL_SHADOW_2_TENSOR_O_VALID_ELEMENTS_0               0x1E0A40

#define mmMME3_CTRL_SHADOW_2_TENSOR_O_VALID_ELEMENTS_1               0x1E0A44

#define mmMME3_CTRL_SHADOW_2_TENSOR_O_VALID_ELEMENTS_2               0x1E0A48

#define mmMME3_CTRL_SHADOW_2_TENSOR_O_VALID_ELEMENTS_3               0x1E0A4C

#define mmMME3_CTRL_SHADOW_2_TENSOR_O_VALID_ELEMENTS_4               0x1E0A50

#define mmMME3_CTRL_SHADOW_2_TENSOR_O_LOOP_STRIDE_0                  0x1E0A54

#define mmMME3_CTRL_SHADOW_2_TENSOR_O_LOOP_STRIDE_1                  0x1E0A58

#define mmMME3_CTRL_SHADOW_2_TENSOR_O_LOOP_STRIDE_2                  0x1E0A5C

#define mmMME3_CTRL_SHADOW_2_TENSOR_O_LOOP_STRIDE_3                  0x1E0A60

#define mmMME3_CTRL_SHADOW_2_TENSOR_O_LOOP_STRIDE_4                  0x1E0A64

#define mmMME3_CTRL_SHADOW_2_TENSOR_O_ROI_SIZE_0                     0x1E0A68

#define mmMME3_CTRL_SHADOW_2_TENSOR_O_ROI_SIZE_1                     0x1E0A6C

#define mmMME3_CTRL_SHADOW_2_TENSOR_O_ROI_SIZE_2                     0x1E0A70

#define mmMME3_CTRL_SHADOW_2_TENSOR_O_ROI_SIZE_3                     0x1E0A74

#define mmMME3_CTRL_SHADOW_2_TENSOR_O_SPATIAL_STRIDES_0              0x1E0A78

#define mmMME3_CTRL_SHADOW_2_TENSOR_O_SPATIAL_STRIDES_1              0x1E0A7C

#define mmMME3_CTRL_SHADOW_2_TENSOR_O_SPATIAL_STRIDES_2              0x1E0A80

#define mmMME3_CTRL_SHADOW_2_TENSOR_O_SPATIAL_STRIDES_3              0x1E0A84

#define mmMME3_CTRL_SHADOW_2_TENSOR_O_SPATIAL_SIZE_MINUS_1           0x1E0A88

#define mmMME3_CTRL_SHADOW_2_AGU_O_LOCAL_ROI_BASE_OFFSET_0           0x1E0A8C

#define mmMME3_CTRL_SHADOW_2_AGU_O_LOCAL_ROI_BASE_OFFSET_1           0x1E0A90

#define mmMME3_CTRL_SHADOW_2_AGU_O_LOCAL_ROI_BASE_OFFSET_2           0x1E0A94

#define mmMME3_CTRL_SHADOW_2_AGU_O_LOCAL_ROI_BASE_OFFSET_3           0x1E0A98

#define mmMME3_CTRL_SHADOW_2_AGU_O_LOCAL_ROI_BASE_OFFSET_4           0x1E0A9C

#define mmMME3_CTRL_SHADOW_2_AGU_O_LOCAL_START_OFFSET_0              0x1E0AA0

#define mmMME3_CTRL_SHADOW_2_AGU_O_LOCAL_START_OFFSET_1              0x1E0AA4

#define mmMME3_CTRL_SHADOW_2_AGU_O_LOCAL_START_OFFSET_2              0x1E0AA8

#define mmMME3_CTRL_SHADOW_2_AGU_O_LOCAL_START_OFFSET_3              0x1E0AAC

#define mmMME3_CTRL_SHADOW_2_AGU_O_REMOTE_ROI_BASE_OFFSET_0          0x1E0AB0

#define mmMME3_CTRL_SHADOW_2_AGU_O_REMOTE_ROI_BASE_OFFSET_1          0x1E0AB4

#define mmMME3_CTRL_SHADOW_2_AGU_O_REMOTE_ROI_BASE_OFFSET_2          0x1E0AB8

#define mmMME3_CTRL_SHADOW_2_AGU_O_REMOTE_ROI_BASE_OFFSET_3          0x1E0ABC

#define mmMME3_CTRL_SHADOW_2_AGU_O_REMOTE_ROI_BASE_OFFSET_4          0x1E0AC0

#define mmMME3_CTRL_SHADOW_2_AGU_O_REMOTE_START_OFFSET_0             0x1E0AC4

#define mmMME3_CTRL_SHADOW_2_AGU_O_REMOTE_START_OFFSET_1             0x1E0AC8

#define mmMME3_CTRL_SHADOW_2_AGU_O_REMOTE_START_OFFSET_2             0x1E0ACC

#define mmMME3_CTRL_SHADOW_2_AGU_O_REMOTE_START_OFFSET_3             0x1E0AD0

#define mmMME3_CTRL_SHADOW_2_DESC_SB_REPEAT                          0x1E0AD4

#define mmMME3_CTRL_SHADOW_2_DESC_RATE_LIMITER                       0x1E0AD8

#define mmMME3_CTRL_SHADOW_2_DESC_SYNC_OBJECT_ADDR_LOW_LOCAL         0x1E0ADC

#define mmMME3_CTRL_SHADOW_2_DESC_SYNC_OBJECT_ADDR_LOW_REMOTE        0x1E0AE0

#define mmMME3_CTRL_SHADOW_2_DESC_SYNC_OBJECT_ADDR_HIGH              0x1E0AE4

#define mmMME3_CTRL_SHADOW_2_DESC_SYNC_OBJECT_DATA                   0x1E0AE8

#define mmMME3_CTRL_SHADOW_2_DESC_AXI_USER_DATA                      0x1E0AEC

#define mmMME3_CTRL_SHADOW_2_DESC_PERF_EVT_S                         0x1E0AF0

#define mmMME3_CTRL_SHADOW_2_DESC_PERF_EVT_L_LOCAL                   0x1E0AF4

#define mmMME3_CTRL_SHADOW_2_DESC_PERF_EVT_L_REMOTE                  0x1E0AF8

#define mmMME3_CTRL_SHADOW_2_DESC_PERF_EVT_O_LOCAL                   0x1E0AFC

#define mmMME3_CTRL_SHADOW_2_DESC_PERF_EVT_O_REMOTE                  0x1E0B00

#define mmMME3_CTRL_SHADOW_2_DESC_PADDING_VALUE_S                    0x1E0B04

#define mmMME3_CTRL_SHADOW_2_DESC_PADDING_VALUE_L                    0x1E0B08

#define mmMME3_CTRL_SHADOW_2_DESC_META_DATA_AGU_S                    0x1E0B0C

#define mmMME3_CTRL_SHADOW_2_DESC_META_DATA_AGU_L_LOCAL              0x1E0B10

#define mmMME3_CTRL_SHADOW_2_DESC_META_DATA_AGU_L_REMOTE             0x1E0B14

#define mmMME3_CTRL_SHADOW_2_DESC_META_DATA_AGU_O_LOCAL              0x1E0B18

#define mmMME3_CTRL_SHADOW_2_DESC_META_DATA_AGU_O_REMOTE             0x1E0B1C

#define mmMME3_CTRL_SHADOW_2_DESC_PCU_RL_SATURATION                  0x1E0B20

#define mmMME3_CTRL_SHADOW_2_DESC_DUMMY                              0x1E0B24

#define mmMME3_CTRL_SHADOW_3_STATUS                                  0x1E0B80

#define mmMME3_CTRL_SHADOW_3_BASE_ADDR_HIGH_S                        0x1E0B88

#define mmMME3_CTRL_SHADOW_3_BASE_ADDR_HIGH_L                        0x1E0B8C

#define mmMME3_CTRL_SHADOW_3_BASE_ADDR_HIGH_O                        0x1E0B90

#define mmMME3_CTRL_SHADOW_3_BASE_ADDR_LOW_S                         0x1E0B94

#define mmMME3_CTRL_SHADOW_3_BASE_ADDR_LOW_L                         0x1E0B98

#define mmMME3_CTRL_SHADOW_3_BASE_ADDR_LOW_O                         0x1E0B9C

#define mmMME3_CTRL_SHADOW_3_HEADER_LOW                              0x1E0BA0

#define mmMME3_CTRL_SHADOW_3_HEADER_HIGH                             0x1E0BA4

#define mmMME3_CTRL_SHADOW_3_CONV_KERNEL_SIZE_MINUS_1                0x1E0BA8

#define mmMME3_CTRL_SHADOW_3_CONV_ASSOCIATED_DIMS_LOW                0x1E0BAC

#define mmMME3_CTRL_SHADOW_3_CONV_ASSOCIATED_DIMS_HIGH               0x1E0BB0

#define mmMME3_CTRL_SHADOW_3_NUM_ITERATIONS_MINUS_1                  0x1E0BB4

#define mmMME3_CTRL_SHADOW_3_OUTER_LOOP                              0x1E0BB8

#define mmMME3_CTRL_SHADOW_3_TENSOR_S_VALID_ELEMENTS_0               0x1E0BBC

#define mmMME3_CTRL_SHADOW_3_TENSOR_S_VALID_ELEMENTS_1               0x1E0BC0

#define mmMME3_CTRL_SHADOW_3_TENSOR_S_VALID_ELEMENTS_2               0x1E0BC4

#define mmMME3_CTRL_SHADOW_3_TENSOR_S_VALID_ELEMENTS_3               0x1E0BC8

#define mmMME3_CTRL_SHADOW_3_TENSOR_S_VALID_ELEMENTS_4               0x1E0BCC

#define mmMME3_CTRL_SHADOW_3_TENSOR_S_LOOP_STRIDE_0                  0x1E0BD0

#define mmMME3_CTRL_SHADOW_3_TENSOR_S_LOOP_STRIDE_1                  0x1E0BD4

#define mmMME3_CTRL_SHADOW_3_TENSOR_S_LOOP_STRIDE_2                  0x1E0BD8

#define mmMME3_CTRL_SHADOW_3_TENSOR_S_LOOP_STRIDE_3                  0x1E0BDC

#define mmMME3_CTRL_SHADOW_3_TENSOR_S_LOOP_STRIDE_4                  0x1E0BE0

#define mmMME3_CTRL_SHADOW_3_TENSOR_S_ROI_SIZE_0                     0x1E0BE4

#define mmMME3_CTRL_SHADOW_3_TENSOR_S_ROI_SIZE_1                     0x1E0BE8

#define mmMME3_CTRL_SHADOW_3_TENSOR_S_ROI_SIZE_2                     0x1E0BEC

#define mmMME3_CTRL_SHADOW_3_TENSOR_S_ROI_SIZE_3                     0x1E0BF0

#define mmMME3_CTRL_SHADOW_3_TENSOR_S_SPATIAL_STRIDES_0              0x1E0BF4

#define mmMME3_CTRL_SHADOW_3_TENSOR_S_SPATIAL_STRIDES_1              0x1E0BF8

#define mmMME3_CTRL_SHADOW_3_TENSOR_S_SPATIAL_STRIDES_2              0x1E0BFC

#define mmMME3_CTRL_SHADOW_3_TENSOR_S_SPATIAL_STRIDES_3              0x1E0C00

#define mmMME3_CTRL_SHADOW_3_TENSOR_S_SPATIAL_SIZE_MINUS_1           0x1E0C04

#define mmMME3_CTRL_SHADOW_3_AGU_S_ROI_BASE_OFFSET_0                 0x1E0C08

#define mmMME3_CTRL_SHADOW_3_AGU_S_ROI_BASE_OFFSET_1                 0x1E0C0C

#define mmMME3_CTRL_SHADOW_3_AGU_S_ROI_BASE_OFFSET_2                 0x1E0C10

#define mmMME3_CTRL_SHADOW_3_AGU_S_ROI_BASE_OFFSET_3                 0x1E0C14

#define mmMME3_CTRL_SHADOW_3_AGU_S_ROI_BASE_OFFSET_4                 0x1E0C18

#define mmMME3_CTRL_SHADOW_3_AGU_S_START_OFFSET_0                    0x1E0C1C

#define mmMME3_CTRL_SHADOW_3_AGU_S_START_OFFSET_1                    0x1E0C20

#define mmMME3_CTRL_SHADOW_3_AGU_S_START_OFFSET_2                    0x1E0C24

#define mmMME3_CTRL_SHADOW_3_AGU_S_START_OFFSET_3                    0x1E0C28

#define mmMME3_CTRL_SHADOW_3_TENSOR_L_VALID_ELEMENTS_0               0x1E0C2C

#define mmMME3_CTRL_SHADOW_3_TENSOR_L_VALID_ELEMENTS_1               0x1E0C30

#define mmMME3_CTRL_SHADOW_3_TENSOR_L_VALID_ELEMENTS_2               0x1E0C34

#define mmMME3_CTRL_SHADOW_3_TENSOR_L_VALID_ELEMENTS_3               0x1E0C38

#define mmMME3_CTRL_SHADOW_3_TENSOR_L_VALID_ELEMENTS_4               0x1E0C3C

#define mmMME3_CTRL_SHADOW_3_TENSOR_L_LOOP_STRIDE_0                  0x1E0C40

#define mmMME3_CTRL_SHADOW_3_TENSOR_L_LOOP_STRIDE_1                  0x1E0C44

#define mmMME3_CTRL_SHADOW_3_TENSOR_L_LOOP_STRIDE_2                  0x1E0C48

#define mmMME3_CTRL_SHADOW_3_TENSOR_L_LOOP_STRIDE_3                  0x1E0C4C

#define mmMME3_CTRL_SHADOW_3_TENSOR_L_LOOP_STRIDE_4                  0x1E0C50

#define mmMME3_CTRL_SHADOW_3_TENSOR_L_ROI_SIZE_0                     0x1E0C54

#define mmMME3_CTRL_SHADOW_3_TENSOR_L_ROI_SIZE_1                     0x1E0C58

#define mmMME3_CTRL_SHADOW_3_TENSOR_L_ROI_SIZE_2                     0x1E0C5C

#define mmMME3_CTRL_SHADOW_3_TENSOR_L_ROI_SIZE_3                     0x1E0C60

#define mmMME3_CTRL_SHADOW_3_TENSOR_L_SPATIAL_STRIDES_0              0x1E0C64

#define mmMME3_CTRL_SHADOW_3_TENSOR_L_SPATIAL_STRIDES_1              0x1E0C68

#define mmMME3_CTRL_SHADOW_3_TENSOR_L_SPATIAL_STRIDES_2              0x1E0C6C

#define mmMME3_CTRL_SHADOW_3_TENSOR_L_SPATIAL_STRIDES_3              0x1E0C70

#define mmMME3_CTRL_SHADOW_3_TENSOR_L_SPATIAL_SIZE_MINUS_1           0x1E0C74

#define mmMME3_CTRL_SHADOW_3_AGU_L_LOCAL_ROI_BASE_OFFSET_0           0x1E0C78

#define mmMME3_CTRL_SHADOW_3_AGU_L_LOCAL_ROI_BASE_OFFSET_1           0x1E0C7C

#define mmMME3_CTRL_SHADOW_3_AGU_L_LOCAL_ROI_BASE_OFFSET_2           0x1E0C80

#define mmMME3_CTRL_SHADOW_3_AGU_L_LOCAL_ROI_BASE_OFFSET_3           0x1E0C84

#define mmMME3_CTRL_SHADOW_3_AGU_L_LOCAL_ROI_BASE_OFFSET_4           0x1E0C88

#define mmMME3_CTRL_SHADOW_3_AGU_L_LOCAL_START_OFFSET_0              0x1E0C8C

#define mmMME3_CTRL_SHADOW_3_AGU_L_LOCAL_START_OFFSET_1              0x1E0C90

#define mmMME3_CTRL_SHADOW_3_AGU_L_LOCAL_START_OFFSET_2              0x1E0C94

#define mmMME3_CTRL_SHADOW_3_AGU_L_LOCAL_START_OFFSET_3              0x1E0C98

#define mmMME3_CTRL_SHADOW_3_AGU_L_REMOTE_ROI_BASE_OFFSET_0          0x1E0C9C

#define mmMME3_CTRL_SHADOW_3_AGU_L_REMOTE_ROI_BASE_OFFSET_1          0x1E0CA0

#define mmMME3_CTRL_SHADOW_3_AGU_L_REMOTE_ROI_BASE_OFFSET_2          0x1E0CA4

#define mmMME3_CTRL_SHADOW_3_AGU_L_REMOTE_ROI_BASE_OFFSET_3          0x1E0CA8

#define mmMME3_CTRL_SHADOW_3_AGU_L_REMOTE_ROI_BASE_OFFSET_4          0x1E0CAC

#define mmMME3_CTRL_SHADOW_3_AGU_L_REMOTE_START_OFFSET_0             0x1E0CB0

#define mmMME3_CTRL_SHADOW_3_AGU_L_REMOTE_START_OFFSET_1             0x1E0CB4

#define mmMME3_CTRL_SHADOW_3_AGU_L_REMOTE_START_OFFSET_2             0x1E0CB8

#define mmMME3_CTRL_SHADOW_3_AGU_L_REMOTE_START_OFFSET_3             0x1E0CBC

#define mmMME3_CTRL_SHADOW_3_TENSOR_O_VALID_ELEMENTS_0               0x1E0CC0

#define mmMME3_CTRL_SHADOW_3_TENSOR_O_VALID_ELEMENTS_1               0x1E0CC4

#define mmMME3_CTRL_SHADOW_3_TENSOR_O_VALID_ELEMENTS_2               0x1E0CC8

#define mmMME3_CTRL_SHADOW_3_TENSOR_O_VALID_ELEMENTS_3               0x1E0CCC

#define mmMME3_CTRL_SHADOW_3_TENSOR_O_VALID_ELEMENTS_4               0x1E0CD0

#define mmMME3_CTRL_SHADOW_3_TENSOR_O_LOOP_STRIDE_0                  0x1E0CD4

#define mmMME3_CTRL_SHADOW_3_TENSOR_O_LOOP_STRIDE_1                  0x1E0CD8

#define mmMME3_CTRL_SHADOW_3_TENSOR_O_LOOP_STRIDE_2                  0x1E0CDC

#define mmMME3_CTRL_SHADOW_3_TENSOR_O_LOOP_STRIDE_3                  0x1E0CE0

#define mmMME3_CTRL_SHADOW_3_TENSOR_O_LOOP_STRIDE_4                  0x1E0CE4

#define mmMME3_CTRL_SHADOW_3_TENSOR_O_ROI_SIZE_0                     0x1E0CE8

#define mmMME3_CTRL_SHADOW_3_TENSOR_O_ROI_SIZE_1                     0x1E0CEC

#define mmMME3_CTRL_SHADOW_3_TENSOR_O_ROI_SIZE_2                     0x1E0CF0

#define mmMME3_CTRL_SHADOW_3_TENSOR_O_ROI_SIZE_3                     0x1E0CF4

#define mmMME3_CTRL_SHADOW_3_TENSOR_O_SPATIAL_STRIDES_0              0x1E0CF8

#define mmMME3_CTRL_SHADOW_3_TENSOR_O_SPATIAL_STRIDES_1              0x1E0CFC

#define mmMME3_CTRL_SHADOW_3_TENSOR_O_SPATIAL_STRIDES_2              0x1E0D00

#define mmMME3_CTRL_SHADOW_3_TENSOR_O_SPATIAL_STRIDES_3              0x1E0D04

#define mmMME3_CTRL_SHADOW_3_TENSOR_O_SPATIAL_SIZE_MINUS_1           0x1E0D08

#define mmMME3_CTRL_SHADOW_3_AGU_O_LOCAL_ROI_BASE_OFFSET_0           0x1E0D0C

#define mmMME3_CTRL_SHADOW_3_AGU_O_LOCAL_ROI_BASE_OFFSET_1           0x1E0D10

#define mmMME3_CTRL_SHADOW_3_AGU_O_LOCAL_ROI_BASE_OFFSET_2           0x1E0D14

#define mmMME3_CTRL_SHADOW_3_AGU_O_LOCAL_ROI_BASE_OFFSET_3           0x1E0D18

#define mmMME3_CTRL_SHADOW_3_AGU_O_LOCAL_ROI_BASE_OFFSET_4           0x1E0D1C

#define mmMME3_CTRL_SHADOW_3_AGU_O_LOCAL_START_OFFSET_0              0x1E0D20

#define mmMME3_CTRL_SHADOW_3_AGU_O_LOCAL_START_OFFSET_1              0x1E0D24

#define mmMME3_CTRL_SHADOW_3_AGU_O_LOCAL_START_OFFSET_2              0x1E0D28

#define mmMME3_CTRL_SHADOW_3_AGU_O_LOCAL_START_OFFSET_3              0x1E0D2C

#define mmMME3_CTRL_SHADOW_3_AGU_O_REMOTE_ROI_BASE_OFFSET_0          0x1E0D30

#define mmMME3_CTRL_SHADOW_3_AGU_O_REMOTE_ROI_BASE_OFFSET_1          0x1E0D34

#define mmMME3_CTRL_SHADOW_3_AGU_O_REMOTE_ROI_BASE_OFFSET_2          0x1E0D38

#define mmMME3_CTRL_SHADOW_3_AGU_O_REMOTE_ROI_BASE_OFFSET_3          0x1E0D3C

#define mmMME3_CTRL_SHADOW_3_AGU_O_REMOTE_ROI_BASE_OFFSET_4          0x1E0D40

#define mmMME3_CTRL_SHADOW_3_AGU_O_REMOTE_START_OFFSET_0             0x1E0D44

#define mmMME3_CTRL_SHADOW_3_AGU_O_REMOTE_START_OFFSET_1             0x1E0D48

#define mmMME3_CTRL_SHADOW_3_AGU_O_REMOTE_START_OFFSET_2             0x1E0D4C

#define mmMME3_CTRL_SHADOW_3_AGU_O_REMOTE_START_OFFSET_3             0x1E0D50

#define mmMME3_CTRL_SHADOW_3_DESC_SB_REPEAT                          0x1E0D54

#define mmMME3_CTRL_SHADOW_3_DESC_RATE_LIMITER                       0x1E0D58

#define mmMME3_CTRL_SHADOW_3_DESC_SYNC_OBJECT_ADDR_LOW_LOCAL         0x1E0D5C

#define mmMME3_CTRL_SHADOW_3_DESC_SYNC_OBJECT_ADDR_LOW_REMOTE        0x1E0D60

#define mmMME3_CTRL_SHADOW_3_DESC_SYNC_OBJECT_ADDR_HIGH              0x1E0D64

#define mmMME3_CTRL_SHADOW_3_DESC_SYNC_OBJECT_DATA                   0x1E0D68

#define mmMME3_CTRL_SHADOW_3_DESC_AXI_USER_DATA                      0x1E0D6C

#define mmMME3_CTRL_SHADOW_3_DESC_PERF_EVT_S                         0x1E0D70

#define mmMME3_CTRL_SHADOW_3_DESC_PERF_EVT_L_LOCAL                   0x1E0D74

#define mmMME3_CTRL_SHADOW_3_DESC_PERF_EVT_L_REMOTE                  0x1E0D78

#define mmMME3_CTRL_SHADOW_3_DESC_PERF_EVT_O_LOCAL                   0x1E0D7C

#define mmMME3_CTRL_SHADOW_3_DESC_PERF_EVT_O_REMOTE                  0x1E0D80

#define mmMME3_CTRL_SHADOW_3_DESC_PADDING_VALUE_S                    0x1E0D84

#define mmMME3_CTRL_SHADOW_3_DESC_PADDING_VALUE_L                    0x1E0D88

#define mmMME3_CTRL_SHADOW_3_DESC_META_DATA_AGU_S                    0x1E0D8C

#define mmMME3_CTRL_SHADOW_3_DESC_META_DATA_AGU_L_LOCAL              0x1E0D90

#define mmMME3_CTRL_SHADOW_3_DESC_META_DATA_AGU_L_REMOTE             0x1E0D94

#define mmMME3_CTRL_SHADOW_3_DESC_META_DATA_AGU_O_LOCAL              0x1E0D98

#define mmMME3_CTRL_SHADOW_3_DESC_META_DATA_AGU_O_REMOTE             0x1E0D9C

#define mmMME3_CTRL_SHADOW_3_DESC_PCU_RL_SATURATION                  0x1E0DA0

#define mmMME3_CTRL_SHADOW_3_DESC_DUMMY                              0x1E0DA4

#endif /* ASIC_REG_MME3_CTRL_REGS_H_ */
