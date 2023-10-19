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

#ifndef ASIC_REG_MME2_CTRL_REGS_H_
#define ASIC_REG_MME2_CTRL_REGS_H_

/*
 *****************************************
 *   MME2_CTRL (Prototype: MME)
 *****************************************
 */

#define mmMME2_CTRL_ARCH_STATUS                                      0x160000

#define mmMME2_CTRL_ARCH_BASE_ADDR_HIGH_S                            0x160008

#define mmMME2_CTRL_ARCH_BASE_ADDR_HIGH_L                            0x16000C

#define mmMME2_CTRL_ARCH_BASE_ADDR_HIGH_O                            0x160010

#define mmMME2_CTRL_ARCH_BASE_ADDR_LOW_S                             0x160014

#define mmMME2_CTRL_ARCH_BASE_ADDR_LOW_L                             0x160018

#define mmMME2_CTRL_ARCH_BASE_ADDR_LOW_O                             0x16001C

#define mmMME2_CTRL_ARCH_HEADER_LOW                                  0x160020

#define mmMME2_CTRL_ARCH_HEADER_HIGH                                 0x160024

#define mmMME2_CTRL_ARCH_CONV_KERNEL_SIZE_MINUS_1                    0x160028

#define mmMME2_CTRL_ARCH_CONV_ASSOCIATED_DIMS_LOW                    0x16002C

#define mmMME2_CTRL_ARCH_CONV_ASSOCIATED_DIMS_HIGH                   0x160030

#define mmMME2_CTRL_ARCH_NUM_ITERATIONS_MINUS_1                      0x160034

#define mmMME2_CTRL_ARCH_OUTER_LOOP                                  0x160038

#define mmMME2_CTRL_ARCH_TENSOR_S_VALID_ELEMENTS_0                   0x16003C

#define mmMME2_CTRL_ARCH_TENSOR_S_VALID_ELEMENTS_1                   0x160040

#define mmMME2_CTRL_ARCH_TENSOR_S_VALID_ELEMENTS_2                   0x160044

#define mmMME2_CTRL_ARCH_TENSOR_S_VALID_ELEMENTS_3                   0x160048

#define mmMME2_CTRL_ARCH_TENSOR_S_VALID_ELEMENTS_4                   0x16004C

#define mmMME2_CTRL_ARCH_TENSOR_S_LOOP_STRIDE_0                      0x160050

#define mmMME2_CTRL_ARCH_TENSOR_S_LOOP_STRIDE_1                      0x160054

#define mmMME2_CTRL_ARCH_TENSOR_S_LOOP_STRIDE_2                      0x160058

#define mmMME2_CTRL_ARCH_TENSOR_S_LOOP_STRIDE_3                      0x16005C

#define mmMME2_CTRL_ARCH_TENSOR_S_LOOP_STRIDE_4                      0x160060

#define mmMME2_CTRL_ARCH_TENSOR_S_ROI_SIZE_0                         0x160064

#define mmMME2_CTRL_ARCH_TENSOR_S_ROI_SIZE_1                         0x160068

#define mmMME2_CTRL_ARCH_TENSOR_S_ROI_SIZE_2                         0x16006C

#define mmMME2_CTRL_ARCH_TENSOR_S_ROI_SIZE_3                         0x160070

#define mmMME2_CTRL_ARCH_TENSOR_S_SPATIAL_STRIDES_0                  0x160074

#define mmMME2_CTRL_ARCH_TENSOR_S_SPATIAL_STRIDES_1                  0x160078

#define mmMME2_CTRL_ARCH_TENSOR_S_SPATIAL_STRIDES_2                  0x16007C

#define mmMME2_CTRL_ARCH_TENSOR_S_SPATIAL_STRIDES_3                  0x160080

#define mmMME2_CTRL_ARCH_TENSOR_S_SPATIAL_SIZE_MINUS_1               0x160084

#define mmMME2_CTRL_ARCH_AGU_S_ROI_BASE_OFFSET_0                     0x160088

#define mmMME2_CTRL_ARCH_AGU_S_ROI_BASE_OFFSET_1                     0x16008C

#define mmMME2_CTRL_ARCH_AGU_S_ROI_BASE_OFFSET_2                     0x160090

#define mmMME2_CTRL_ARCH_AGU_S_ROI_BASE_OFFSET_3                     0x160094

#define mmMME2_CTRL_ARCH_AGU_S_ROI_BASE_OFFSET_4                     0x160098

#define mmMME2_CTRL_ARCH_AGU_S_START_OFFSET_0                        0x16009C

#define mmMME2_CTRL_ARCH_AGU_S_START_OFFSET_1                        0x1600A0

#define mmMME2_CTRL_ARCH_AGU_S_START_OFFSET_2                        0x1600A4

#define mmMME2_CTRL_ARCH_AGU_S_START_OFFSET_3                        0x1600A8

#define mmMME2_CTRL_ARCH_TENSOR_L_VALID_ELEMENTS_0                   0x1600AC

#define mmMME2_CTRL_ARCH_TENSOR_L_VALID_ELEMENTS_1                   0x1600B0

#define mmMME2_CTRL_ARCH_TENSOR_L_VALID_ELEMENTS_2                   0x1600B4

#define mmMME2_CTRL_ARCH_TENSOR_L_VALID_ELEMENTS_3                   0x1600B8

#define mmMME2_CTRL_ARCH_TENSOR_L_VALID_ELEMENTS_4                   0x1600BC

#define mmMME2_CTRL_ARCH_TENSOR_L_LOOP_STRIDE_0                      0x1600C0

#define mmMME2_CTRL_ARCH_TENSOR_L_LOOP_STRIDE_1                      0x1600C4

#define mmMME2_CTRL_ARCH_TENSOR_L_LOOP_STRIDE_2                      0x1600C8

#define mmMME2_CTRL_ARCH_TENSOR_L_LOOP_STRIDE_3                      0x1600CC

#define mmMME2_CTRL_ARCH_TENSOR_L_LOOP_STRIDE_4                      0x1600D0

#define mmMME2_CTRL_ARCH_TENSOR_L_ROI_SIZE_0                         0x1600D4

#define mmMME2_CTRL_ARCH_TENSOR_L_ROI_SIZE_1                         0x1600D8

#define mmMME2_CTRL_ARCH_TENSOR_L_ROI_SIZE_2                         0x1600DC

#define mmMME2_CTRL_ARCH_TENSOR_L_ROI_SIZE_3                         0x1600E0

#define mmMME2_CTRL_ARCH_TENSOR_L_SPATIAL_STRIDES_0                  0x1600E4

#define mmMME2_CTRL_ARCH_TENSOR_L_SPATIAL_STRIDES_1                  0x1600E8

#define mmMME2_CTRL_ARCH_TENSOR_L_SPATIAL_STRIDES_2                  0x1600EC

#define mmMME2_CTRL_ARCH_TENSOR_L_SPATIAL_STRIDES_3                  0x1600F0

#define mmMME2_CTRL_ARCH_TENSOR_L_SPATIAL_SIZE_MINUS_1               0x1600F4

#define mmMME2_CTRL_ARCH_AGU_L_LOCAL_ROI_BASE_OFFSET_0               0x1600F8

#define mmMME2_CTRL_ARCH_AGU_L_LOCAL_ROI_BASE_OFFSET_1               0x1600FC

#define mmMME2_CTRL_ARCH_AGU_L_LOCAL_ROI_BASE_OFFSET_2               0x160100

#define mmMME2_CTRL_ARCH_AGU_L_LOCAL_ROI_BASE_OFFSET_3               0x160104

#define mmMME2_CTRL_ARCH_AGU_L_LOCAL_ROI_BASE_OFFSET_4               0x160108

#define mmMME2_CTRL_ARCH_AGU_L_LOCAL_START_OFFSET_0                  0x16010C

#define mmMME2_CTRL_ARCH_AGU_L_LOCAL_START_OFFSET_1                  0x160110

#define mmMME2_CTRL_ARCH_AGU_L_LOCAL_START_OFFSET_2                  0x160114

#define mmMME2_CTRL_ARCH_AGU_L_LOCAL_START_OFFSET_3                  0x160118

#define mmMME2_CTRL_ARCH_AGU_L_REMOTE_ROI_BASE_OFFSET_0              0x16011C

#define mmMME2_CTRL_ARCH_AGU_L_REMOTE_ROI_BASE_OFFSET_1              0x160120

#define mmMME2_CTRL_ARCH_AGU_L_REMOTE_ROI_BASE_OFFSET_2              0x160124

#define mmMME2_CTRL_ARCH_AGU_L_REMOTE_ROI_BASE_OFFSET_3              0x160128

#define mmMME2_CTRL_ARCH_AGU_L_REMOTE_ROI_BASE_OFFSET_4              0x16012C

#define mmMME2_CTRL_ARCH_AGU_L_REMOTE_START_OFFSET_0                 0x160130

#define mmMME2_CTRL_ARCH_AGU_L_REMOTE_START_OFFSET_1                 0x160134

#define mmMME2_CTRL_ARCH_AGU_L_REMOTE_START_OFFSET_2                 0x160138

#define mmMME2_CTRL_ARCH_AGU_L_REMOTE_START_OFFSET_3                 0x16013C

#define mmMME2_CTRL_ARCH_TENSOR_O_VALID_ELEMENTS_0                   0x160140

#define mmMME2_CTRL_ARCH_TENSOR_O_VALID_ELEMENTS_1                   0x160144

#define mmMME2_CTRL_ARCH_TENSOR_O_VALID_ELEMENTS_2                   0x160148

#define mmMME2_CTRL_ARCH_TENSOR_O_VALID_ELEMENTS_3                   0x16014C

#define mmMME2_CTRL_ARCH_TENSOR_O_VALID_ELEMENTS_4                   0x160150

#define mmMME2_CTRL_ARCH_TENSOR_O_LOOP_STRIDE_0                      0x160154

#define mmMME2_CTRL_ARCH_TENSOR_O_LOOP_STRIDE_1                      0x160158

#define mmMME2_CTRL_ARCH_TENSOR_O_LOOP_STRIDE_2                      0x16015C

#define mmMME2_CTRL_ARCH_TENSOR_O_LOOP_STRIDE_3                      0x160160

#define mmMME2_CTRL_ARCH_TENSOR_O_LOOP_STRIDE_4                      0x160164

#define mmMME2_CTRL_ARCH_TENSOR_O_ROI_SIZE_0                         0x160168

#define mmMME2_CTRL_ARCH_TENSOR_O_ROI_SIZE_1                         0x16016C

#define mmMME2_CTRL_ARCH_TENSOR_O_ROI_SIZE_2                         0x160170

#define mmMME2_CTRL_ARCH_TENSOR_O_ROI_SIZE_3                         0x160174

#define mmMME2_CTRL_ARCH_TENSOR_O_SPATIAL_STRIDES_0                  0x160178

#define mmMME2_CTRL_ARCH_TENSOR_O_SPATIAL_STRIDES_1                  0x16017C

#define mmMME2_CTRL_ARCH_TENSOR_O_SPATIAL_STRIDES_2                  0x160180

#define mmMME2_CTRL_ARCH_TENSOR_O_SPATIAL_STRIDES_3                  0x160184

#define mmMME2_CTRL_ARCH_TENSOR_O_SPATIAL_SIZE_MINUS_1               0x160188

#define mmMME2_CTRL_ARCH_AGU_O_LOCAL_ROI_BASE_OFFSET_0               0x16018C

#define mmMME2_CTRL_ARCH_AGU_O_LOCAL_ROI_BASE_OFFSET_1               0x160190

#define mmMME2_CTRL_ARCH_AGU_O_LOCAL_ROI_BASE_OFFSET_2               0x160194

#define mmMME2_CTRL_ARCH_AGU_O_LOCAL_ROI_BASE_OFFSET_3               0x160198

#define mmMME2_CTRL_ARCH_AGU_O_LOCAL_ROI_BASE_OFFSET_4               0x16019C

#define mmMME2_CTRL_ARCH_AGU_O_LOCAL_START_OFFSET_0                  0x1601A0

#define mmMME2_CTRL_ARCH_AGU_O_LOCAL_START_OFFSET_1                  0x1601A4

#define mmMME2_CTRL_ARCH_AGU_O_LOCAL_START_OFFSET_2                  0x1601A8

#define mmMME2_CTRL_ARCH_AGU_O_LOCAL_START_OFFSET_3                  0x1601AC

#define mmMME2_CTRL_ARCH_AGU_O_REMOTE_ROI_BASE_OFFSET_0              0x1601B0

#define mmMME2_CTRL_ARCH_AGU_O_REMOTE_ROI_BASE_OFFSET_1              0x1601B4

#define mmMME2_CTRL_ARCH_AGU_O_REMOTE_ROI_BASE_OFFSET_2              0x1601B8

#define mmMME2_CTRL_ARCH_AGU_O_REMOTE_ROI_BASE_OFFSET_3              0x1601BC

#define mmMME2_CTRL_ARCH_AGU_O_REMOTE_ROI_BASE_OFFSET_4              0x1601C0

#define mmMME2_CTRL_ARCH_AGU_O_REMOTE_START_OFFSET_0                 0x1601C4

#define mmMME2_CTRL_ARCH_AGU_O_REMOTE_START_OFFSET_1                 0x1601C8

#define mmMME2_CTRL_ARCH_AGU_O_REMOTE_START_OFFSET_2                 0x1601CC

#define mmMME2_CTRL_ARCH_AGU_O_REMOTE_START_OFFSET_3                 0x1601D0

#define mmMME2_CTRL_ARCH_DESC_SB_REPEAT                              0x1601D4

#define mmMME2_CTRL_ARCH_DESC_RATE_LIMITER                           0x1601D8

#define mmMME2_CTRL_ARCH_DESC_SYNC_OBJECT_ADDR_LOW_LOCAL             0x1601DC

#define mmMME2_CTRL_ARCH_DESC_SYNC_OBJECT_ADDR_LOW_REMOTE            0x1601E0

#define mmMME2_CTRL_ARCH_DESC_SYNC_OBJECT_ADDR_HIGH                  0x1601E4

#define mmMME2_CTRL_ARCH_DESC_SYNC_OBJECT_DATA                       0x1601E8

#define mmMME2_CTRL_ARCH_DESC_AXI_USER_DATA                          0x1601EC

#define mmMME2_CTRL_ARCH_DESC_PERF_EVT_S                             0x1601F0

#define mmMME2_CTRL_ARCH_DESC_PERF_EVT_L_LOCAL                       0x1601F4

#define mmMME2_CTRL_ARCH_DESC_PERF_EVT_L_REMOTE                      0x1601F8

#define mmMME2_CTRL_ARCH_DESC_PERF_EVT_O_LOCAL                       0x1601FC

#define mmMME2_CTRL_ARCH_DESC_PERF_EVT_O_REMOTE                      0x160200

#define mmMME2_CTRL_ARCH_DESC_PADDING_VALUE_S                        0x160204

#define mmMME2_CTRL_ARCH_DESC_PADDING_VALUE_L                        0x160208

#define mmMME2_CTRL_ARCH_DESC_META_DATA_AGU_S                        0x16020C

#define mmMME2_CTRL_ARCH_DESC_META_DATA_AGU_L_LOCAL                  0x160210

#define mmMME2_CTRL_ARCH_DESC_META_DATA_AGU_L_REMOTE                 0x160214

#define mmMME2_CTRL_ARCH_DESC_META_DATA_AGU_O_LOCAL                  0x160218

#define mmMME2_CTRL_ARCH_DESC_META_DATA_AGU_O_REMOTE                 0x16021C

#define mmMME2_CTRL_ARCH_DESC_PCU_RL_SATURATION                      0x160220

#define mmMME2_CTRL_ARCH_DESC_DUMMY                                  0x160224

#define mmMME2_CTRL_CMD                                              0x160280

#define mmMME2_CTRL_STATUS1                                          0x160284

#define mmMME2_CTRL_RESET                                            0x160288

#define mmMME2_CTRL_QM_STALL                                         0x16028C

#define mmMME2_CTRL_SYNC_OBJECT_FIFO_TH                              0x160290

#define mmMME2_CTRL_EUS_ROLLUP_CNT_ADD                               0x160294

#define mmMME2_CTRL_INTR_CAUSE                                       0x160298

#define mmMME2_CTRL_INTR_MASK                                        0x16029C

#define mmMME2_CTRL_LOG_SHADOW                                       0x1602A0

#define mmMME2_CTRL_PCU_RL_DESC0                                     0x1602A4

#define mmMME2_CTRL_PCU_RL_TOKEN_UPDATE                              0x1602A8

#define mmMME2_CTRL_PCU_RL_TH                                        0x1602AC

#define mmMME2_CTRL_PCU_RL_MIN                                       0x1602B0

#define mmMME2_CTRL_PCU_RL_CTRL_EN                                   0x1602B4

#define mmMME2_CTRL_PCU_RL_HISTORY_LOG_SIZE                          0x1602B8

#define mmMME2_CTRL_PCU_DUMMY_A_BF16                                 0x1602BC

#define mmMME2_CTRL_PCU_DUMMY_B_BF16                                 0x1602C0

#define mmMME2_CTRL_PCU_DUMMY_A_FP32_ODD                             0x1602C4

#define mmMME2_CTRL_PCU_DUMMY_A_FP32_EVEN                            0x1602C8

#define mmMME2_CTRL_PCU_DUMMY_B_FP32_ODD                             0x1602CC

#define mmMME2_CTRL_PCU_DUMMY_B_FP32_EVEN                            0x1602D0

#define mmMME2_CTRL_PROT                                             0x1602D4

#define mmMME2_CTRL_EU_POWER_SAVE_DISABLE                            0x1602D8

#define mmMME2_CTRL_CS_DBG_BLOCK_ID                                  0x1602DC

#define mmMME2_CTRL_CS_DBG_STATUS_DROP_CNT                           0x1602E0

#define mmMME2_CTRL_TE_CLOSE_CGATE                                   0x1602E4

#define mmMME2_CTRL_AGU_SM_INFLIGHT_CNTR                             0x1602E8

#define mmMME2_CTRL_AGU_SM_TOTAL_CNTR                                0x1602EC

#define mmMME2_CTRL_EZSYNC_OUT_CREDIT                                0x1602F0

#define mmMME2_CTRL_PCU_RL_SAT_SEC                                   0x1602F4

#define mmMME2_CTRL_AGU_SYNC_MSG_AXI_USER                            0x1602F8

#define mmMME2_CTRL_QM_SLV_LBW_CLK_EN                                0x1602FC

#define mmMME2_CTRL_SHADOW_0_STATUS                                  0x160400

#define mmMME2_CTRL_SHADOW_0_BASE_ADDR_HIGH_S                        0x160408

#define mmMME2_CTRL_SHADOW_0_BASE_ADDR_HIGH_L                        0x16040C

#define mmMME2_CTRL_SHADOW_0_BASE_ADDR_HIGH_O                        0x160410

#define mmMME2_CTRL_SHADOW_0_BASE_ADDR_LOW_S                         0x160414

#define mmMME2_CTRL_SHADOW_0_BASE_ADDR_LOW_L                         0x160418

#define mmMME2_CTRL_SHADOW_0_BASE_ADDR_LOW_O                         0x16041C

#define mmMME2_CTRL_SHADOW_0_HEADER_LOW                              0x160420

#define mmMME2_CTRL_SHADOW_0_HEADER_HIGH                             0x160424

#define mmMME2_CTRL_SHADOW_0_CONV_KERNEL_SIZE_MINUS_1                0x160428

#define mmMME2_CTRL_SHADOW_0_CONV_ASSOCIATED_DIMS_LOW                0x16042C

#define mmMME2_CTRL_SHADOW_0_CONV_ASSOCIATED_DIMS_HIGH               0x160430

#define mmMME2_CTRL_SHADOW_0_NUM_ITERATIONS_MINUS_1                  0x160434

#define mmMME2_CTRL_SHADOW_0_OUTER_LOOP                              0x160438

#define mmMME2_CTRL_SHADOW_0_TENSOR_S_VALID_ELEMENTS_0               0x16043C

#define mmMME2_CTRL_SHADOW_0_TENSOR_S_VALID_ELEMENTS_1               0x160440

#define mmMME2_CTRL_SHADOW_0_TENSOR_S_VALID_ELEMENTS_2               0x160444

#define mmMME2_CTRL_SHADOW_0_TENSOR_S_VALID_ELEMENTS_3               0x160448

#define mmMME2_CTRL_SHADOW_0_TENSOR_S_VALID_ELEMENTS_4               0x16044C

#define mmMME2_CTRL_SHADOW_0_TENSOR_S_LOOP_STRIDE_0                  0x160450

#define mmMME2_CTRL_SHADOW_0_TENSOR_S_LOOP_STRIDE_1                  0x160454

#define mmMME2_CTRL_SHADOW_0_TENSOR_S_LOOP_STRIDE_2                  0x160458

#define mmMME2_CTRL_SHADOW_0_TENSOR_S_LOOP_STRIDE_3                  0x16045C

#define mmMME2_CTRL_SHADOW_0_TENSOR_S_LOOP_STRIDE_4                  0x160460

#define mmMME2_CTRL_SHADOW_0_TENSOR_S_ROI_SIZE_0                     0x160464

#define mmMME2_CTRL_SHADOW_0_TENSOR_S_ROI_SIZE_1                     0x160468

#define mmMME2_CTRL_SHADOW_0_TENSOR_S_ROI_SIZE_2                     0x16046C

#define mmMME2_CTRL_SHADOW_0_TENSOR_S_ROI_SIZE_3                     0x160470

#define mmMME2_CTRL_SHADOW_0_TENSOR_S_SPATIAL_STRIDES_0              0x160474

#define mmMME2_CTRL_SHADOW_0_TENSOR_S_SPATIAL_STRIDES_1              0x160478

#define mmMME2_CTRL_SHADOW_0_TENSOR_S_SPATIAL_STRIDES_2              0x16047C

#define mmMME2_CTRL_SHADOW_0_TENSOR_S_SPATIAL_STRIDES_3              0x160480

#define mmMME2_CTRL_SHADOW_0_TENSOR_S_SPATIAL_SIZE_MINUS_1           0x160484

#define mmMME2_CTRL_SHADOW_0_AGU_S_ROI_BASE_OFFSET_0                 0x160488

#define mmMME2_CTRL_SHADOW_0_AGU_S_ROI_BASE_OFFSET_1                 0x16048C

#define mmMME2_CTRL_SHADOW_0_AGU_S_ROI_BASE_OFFSET_2                 0x160490

#define mmMME2_CTRL_SHADOW_0_AGU_S_ROI_BASE_OFFSET_3                 0x160494

#define mmMME2_CTRL_SHADOW_0_AGU_S_ROI_BASE_OFFSET_4                 0x160498

#define mmMME2_CTRL_SHADOW_0_AGU_S_START_OFFSET_0                    0x16049C

#define mmMME2_CTRL_SHADOW_0_AGU_S_START_OFFSET_1                    0x1604A0

#define mmMME2_CTRL_SHADOW_0_AGU_S_START_OFFSET_2                    0x1604A4

#define mmMME2_CTRL_SHADOW_0_AGU_S_START_OFFSET_3                    0x1604A8

#define mmMME2_CTRL_SHADOW_0_TENSOR_L_VALID_ELEMENTS_0               0x1604AC

#define mmMME2_CTRL_SHADOW_0_TENSOR_L_VALID_ELEMENTS_1               0x1604B0

#define mmMME2_CTRL_SHADOW_0_TENSOR_L_VALID_ELEMENTS_2               0x1604B4

#define mmMME2_CTRL_SHADOW_0_TENSOR_L_VALID_ELEMENTS_3               0x1604B8

#define mmMME2_CTRL_SHADOW_0_TENSOR_L_VALID_ELEMENTS_4               0x1604BC

#define mmMME2_CTRL_SHADOW_0_TENSOR_L_LOOP_STRIDE_0                  0x1604C0

#define mmMME2_CTRL_SHADOW_0_TENSOR_L_LOOP_STRIDE_1                  0x1604C4

#define mmMME2_CTRL_SHADOW_0_TENSOR_L_LOOP_STRIDE_2                  0x1604C8

#define mmMME2_CTRL_SHADOW_0_TENSOR_L_LOOP_STRIDE_3                  0x1604CC

#define mmMME2_CTRL_SHADOW_0_TENSOR_L_LOOP_STRIDE_4                  0x1604D0

#define mmMME2_CTRL_SHADOW_0_TENSOR_L_ROI_SIZE_0                     0x1604D4

#define mmMME2_CTRL_SHADOW_0_TENSOR_L_ROI_SIZE_1                     0x1604D8

#define mmMME2_CTRL_SHADOW_0_TENSOR_L_ROI_SIZE_2                     0x1604DC

#define mmMME2_CTRL_SHADOW_0_TENSOR_L_ROI_SIZE_3                     0x1604E0

#define mmMME2_CTRL_SHADOW_0_TENSOR_L_SPATIAL_STRIDES_0              0x1604E4

#define mmMME2_CTRL_SHADOW_0_TENSOR_L_SPATIAL_STRIDES_1              0x1604E8

#define mmMME2_CTRL_SHADOW_0_TENSOR_L_SPATIAL_STRIDES_2              0x1604EC

#define mmMME2_CTRL_SHADOW_0_TENSOR_L_SPATIAL_STRIDES_3              0x1604F0

#define mmMME2_CTRL_SHADOW_0_TENSOR_L_SPATIAL_SIZE_MINUS_1           0x1604F4

#define mmMME2_CTRL_SHADOW_0_AGU_L_LOCAL_ROI_BASE_OFFSET_0           0x1604F8

#define mmMME2_CTRL_SHADOW_0_AGU_L_LOCAL_ROI_BASE_OFFSET_1           0x1604FC

#define mmMME2_CTRL_SHADOW_0_AGU_L_LOCAL_ROI_BASE_OFFSET_2           0x160500

#define mmMME2_CTRL_SHADOW_0_AGU_L_LOCAL_ROI_BASE_OFFSET_3           0x160504

#define mmMME2_CTRL_SHADOW_0_AGU_L_LOCAL_ROI_BASE_OFFSET_4           0x160508

#define mmMME2_CTRL_SHADOW_0_AGU_L_LOCAL_START_OFFSET_0              0x16050C

#define mmMME2_CTRL_SHADOW_0_AGU_L_LOCAL_START_OFFSET_1              0x160510

#define mmMME2_CTRL_SHADOW_0_AGU_L_LOCAL_START_OFFSET_2              0x160514

#define mmMME2_CTRL_SHADOW_0_AGU_L_LOCAL_START_OFFSET_3              0x160518

#define mmMME2_CTRL_SHADOW_0_AGU_L_REMOTE_ROI_BASE_OFFSET_0          0x16051C

#define mmMME2_CTRL_SHADOW_0_AGU_L_REMOTE_ROI_BASE_OFFSET_1          0x160520

#define mmMME2_CTRL_SHADOW_0_AGU_L_REMOTE_ROI_BASE_OFFSET_2          0x160524

#define mmMME2_CTRL_SHADOW_0_AGU_L_REMOTE_ROI_BASE_OFFSET_3          0x160528

#define mmMME2_CTRL_SHADOW_0_AGU_L_REMOTE_ROI_BASE_OFFSET_4          0x16052C

#define mmMME2_CTRL_SHADOW_0_AGU_L_REMOTE_START_OFFSET_0             0x160530

#define mmMME2_CTRL_SHADOW_0_AGU_L_REMOTE_START_OFFSET_1             0x160534

#define mmMME2_CTRL_SHADOW_0_AGU_L_REMOTE_START_OFFSET_2             0x160538

#define mmMME2_CTRL_SHADOW_0_AGU_L_REMOTE_START_OFFSET_3             0x16053C

#define mmMME2_CTRL_SHADOW_0_TENSOR_O_VALID_ELEMENTS_0               0x160540

#define mmMME2_CTRL_SHADOW_0_TENSOR_O_VALID_ELEMENTS_1               0x160544

#define mmMME2_CTRL_SHADOW_0_TENSOR_O_VALID_ELEMENTS_2               0x160548

#define mmMME2_CTRL_SHADOW_0_TENSOR_O_VALID_ELEMENTS_3               0x16054C

#define mmMME2_CTRL_SHADOW_0_TENSOR_O_VALID_ELEMENTS_4               0x160550

#define mmMME2_CTRL_SHADOW_0_TENSOR_O_LOOP_STRIDE_0                  0x160554

#define mmMME2_CTRL_SHADOW_0_TENSOR_O_LOOP_STRIDE_1                  0x160558

#define mmMME2_CTRL_SHADOW_0_TENSOR_O_LOOP_STRIDE_2                  0x16055C

#define mmMME2_CTRL_SHADOW_0_TENSOR_O_LOOP_STRIDE_3                  0x160560

#define mmMME2_CTRL_SHADOW_0_TENSOR_O_LOOP_STRIDE_4                  0x160564

#define mmMME2_CTRL_SHADOW_0_TENSOR_O_ROI_SIZE_0                     0x160568

#define mmMME2_CTRL_SHADOW_0_TENSOR_O_ROI_SIZE_1                     0x16056C

#define mmMME2_CTRL_SHADOW_0_TENSOR_O_ROI_SIZE_2                     0x160570

#define mmMME2_CTRL_SHADOW_0_TENSOR_O_ROI_SIZE_3                     0x160574

#define mmMME2_CTRL_SHADOW_0_TENSOR_O_SPATIAL_STRIDES_0              0x160578

#define mmMME2_CTRL_SHADOW_0_TENSOR_O_SPATIAL_STRIDES_1              0x16057C

#define mmMME2_CTRL_SHADOW_0_TENSOR_O_SPATIAL_STRIDES_2              0x160580

#define mmMME2_CTRL_SHADOW_0_TENSOR_O_SPATIAL_STRIDES_3              0x160584

#define mmMME2_CTRL_SHADOW_0_TENSOR_O_SPATIAL_SIZE_MINUS_1           0x160588

#define mmMME2_CTRL_SHADOW_0_AGU_O_LOCAL_ROI_BASE_OFFSET_0           0x16058C

#define mmMME2_CTRL_SHADOW_0_AGU_O_LOCAL_ROI_BASE_OFFSET_1           0x160590

#define mmMME2_CTRL_SHADOW_0_AGU_O_LOCAL_ROI_BASE_OFFSET_2           0x160594

#define mmMME2_CTRL_SHADOW_0_AGU_O_LOCAL_ROI_BASE_OFFSET_3           0x160598

#define mmMME2_CTRL_SHADOW_0_AGU_O_LOCAL_ROI_BASE_OFFSET_4           0x16059C

#define mmMME2_CTRL_SHADOW_0_AGU_O_LOCAL_START_OFFSET_0              0x1605A0

#define mmMME2_CTRL_SHADOW_0_AGU_O_LOCAL_START_OFFSET_1              0x1605A4

#define mmMME2_CTRL_SHADOW_0_AGU_O_LOCAL_START_OFFSET_2              0x1605A8

#define mmMME2_CTRL_SHADOW_0_AGU_O_LOCAL_START_OFFSET_3              0x1605AC

#define mmMME2_CTRL_SHADOW_0_AGU_O_REMOTE_ROI_BASE_OFFSET_0          0x1605B0

#define mmMME2_CTRL_SHADOW_0_AGU_O_REMOTE_ROI_BASE_OFFSET_1          0x1605B4

#define mmMME2_CTRL_SHADOW_0_AGU_O_REMOTE_ROI_BASE_OFFSET_2          0x1605B8

#define mmMME2_CTRL_SHADOW_0_AGU_O_REMOTE_ROI_BASE_OFFSET_3          0x1605BC

#define mmMME2_CTRL_SHADOW_0_AGU_O_REMOTE_ROI_BASE_OFFSET_4          0x1605C0

#define mmMME2_CTRL_SHADOW_0_AGU_O_REMOTE_START_OFFSET_0             0x1605C4

#define mmMME2_CTRL_SHADOW_0_AGU_O_REMOTE_START_OFFSET_1             0x1605C8

#define mmMME2_CTRL_SHADOW_0_AGU_O_REMOTE_START_OFFSET_2             0x1605CC

#define mmMME2_CTRL_SHADOW_0_AGU_O_REMOTE_START_OFFSET_3             0x1605D0

#define mmMME2_CTRL_SHADOW_0_DESC_SB_REPEAT                          0x1605D4

#define mmMME2_CTRL_SHADOW_0_DESC_RATE_LIMITER                       0x1605D8

#define mmMME2_CTRL_SHADOW_0_DESC_SYNC_OBJECT_ADDR_LOW_LOCAL         0x1605DC

#define mmMME2_CTRL_SHADOW_0_DESC_SYNC_OBJECT_ADDR_LOW_REMOTE        0x1605E0

#define mmMME2_CTRL_SHADOW_0_DESC_SYNC_OBJECT_ADDR_HIGH              0x1605E4

#define mmMME2_CTRL_SHADOW_0_DESC_SYNC_OBJECT_DATA                   0x1605E8

#define mmMME2_CTRL_SHADOW_0_DESC_AXI_USER_DATA                      0x1605EC

#define mmMME2_CTRL_SHADOW_0_DESC_PERF_EVT_S                         0x1605F0

#define mmMME2_CTRL_SHADOW_0_DESC_PERF_EVT_L_LOCAL                   0x1605F4

#define mmMME2_CTRL_SHADOW_0_DESC_PERF_EVT_L_REMOTE                  0x1605F8

#define mmMME2_CTRL_SHADOW_0_DESC_PERF_EVT_O_LOCAL                   0x1605FC

#define mmMME2_CTRL_SHADOW_0_DESC_PERF_EVT_O_REMOTE                  0x160600

#define mmMME2_CTRL_SHADOW_0_DESC_PADDING_VALUE_S                    0x160604

#define mmMME2_CTRL_SHADOW_0_DESC_PADDING_VALUE_L                    0x160608

#define mmMME2_CTRL_SHADOW_0_DESC_META_DATA_AGU_S                    0x16060C

#define mmMME2_CTRL_SHADOW_0_DESC_META_DATA_AGU_L_LOCAL              0x160610

#define mmMME2_CTRL_SHADOW_0_DESC_META_DATA_AGU_L_REMOTE             0x160614

#define mmMME2_CTRL_SHADOW_0_DESC_META_DATA_AGU_O_LOCAL              0x160618

#define mmMME2_CTRL_SHADOW_0_DESC_META_DATA_AGU_O_REMOTE             0x16061C

#define mmMME2_CTRL_SHADOW_0_DESC_PCU_RL_SATURATION                  0x160620

#define mmMME2_CTRL_SHADOW_0_DESC_DUMMY                              0x160624

#define mmMME2_CTRL_SHADOW_1_STATUS                                  0x160680

#define mmMME2_CTRL_SHADOW_1_BASE_ADDR_HIGH_S                        0x160688

#define mmMME2_CTRL_SHADOW_1_BASE_ADDR_HIGH_L                        0x16068C

#define mmMME2_CTRL_SHADOW_1_BASE_ADDR_HIGH_O                        0x160690

#define mmMME2_CTRL_SHADOW_1_BASE_ADDR_LOW_S                         0x160694

#define mmMME2_CTRL_SHADOW_1_BASE_ADDR_LOW_L                         0x160698

#define mmMME2_CTRL_SHADOW_1_BASE_ADDR_LOW_O                         0x16069C

#define mmMME2_CTRL_SHADOW_1_HEADER_LOW                              0x1606A0

#define mmMME2_CTRL_SHADOW_1_HEADER_HIGH                             0x1606A4

#define mmMME2_CTRL_SHADOW_1_CONV_KERNEL_SIZE_MINUS_1                0x1606A8

#define mmMME2_CTRL_SHADOW_1_CONV_ASSOCIATED_DIMS_LOW                0x1606AC

#define mmMME2_CTRL_SHADOW_1_CONV_ASSOCIATED_DIMS_HIGH               0x1606B0

#define mmMME2_CTRL_SHADOW_1_NUM_ITERATIONS_MINUS_1                  0x1606B4

#define mmMME2_CTRL_SHADOW_1_OUTER_LOOP                              0x1606B8

#define mmMME2_CTRL_SHADOW_1_TENSOR_S_VALID_ELEMENTS_0               0x1606BC

#define mmMME2_CTRL_SHADOW_1_TENSOR_S_VALID_ELEMENTS_1               0x1606C0

#define mmMME2_CTRL_SHADOW_1_TENSOR_S_VALID_ELEMENTS_2               0x1606C4

#define mmMME2_CTRL_SHADOW_1_TENSOR_S_VALID_ELEMENTS_3               0x1606C8

#define mmMME2_CTRL_SHADOW_1_TENSOR_S_VALID_ELEMENTS_4               0x1606CC

#define mmMME2_CTRL_SHADOW_1_TENSOR_S_LOOP_STRIDE_0                  0x1606D0

#define mmMME2_CTRL_SHADOW_1_TENSOR_S_LOOP_STRIDE_1                  0x1606D4

#define mmMME2_CTRL_SHADOW_1_TENSOR_S_LOOP_STRIDE_2                  0x1606D8

#define mmMME2_CTRL_SHADOW_1_TENSOR_S_LOOP_STRIDE_3                  0x1606DC

#define mmMME2_CTRL_SHADOW_1_TENSOR_S_LOOP_STRIDE_4                  0x1606E0

#define mmMME2_CTRL_SHADOW_1_TENSOR_S_ROI_SIZE_0                     0x1606E4

#define mmMME2_CTRL_SHADOW_1_TENSOR_S_ROI_SIZE_1                     0x1606E8

#define mmMME2_CTRL_SHADOW_1_TENSOR_S_ROI_SIZE_2                     0x1606EC

#define mmMME2_CTRL_SHADOW_1_TENSOR_S_ROI_SIZE_3                     0x1606F0

#define mmMME2_CTRL_SHADOW_1_TENSOR_S_SPATIAL_STRIDES_0              0x1606F4

#define mmMME2_CTRL_SHADOW_1_TENSOR_S_SPATIAL_STRIDES_1              0x1606F8

#define mmMME2_CTRL_SHADOW_1_TENSOR_S_SPATIAL_STRIDES_2              0x1606FC

#define mmMME2_CTRL_SHADOW_1_TENSOR_S_SPATIAL_STRIDES_3              0x160700

#define mmMME2_CTRL_SHADOW_1_TENSOR_S_SPATIAL_SIZE_MINUS_1           0x160704

#define mmMME2_CTRL_SHADOW_1_AGU_S_ROI_BASE_OFFSET_0                 0x160708

#define mmMME2_CTRL_SHADOW_1_AGU_S_ROI_BASE_OFFSET_1                 0x16070C

#define mmMME2_CTRL_SHADOW_1_AGU_S_ROI_BASE_OFFSET_2                 0x160710

#define mmMME2_CTRL_SHADOW_1_AGU_S_ROI_BASE_OFFSET_3                 0x160714

#define mmMME2_CTRL_SHADOW_1_AGU_S_ROI_BASE_OFFSET_4                 0x160718

#define mmMME2_CTRL_SHADOW_1_AGU_S_START_OFFSET_0                    0x16071C

#define mmMME2_CTRL_SHADOW_1_AGU_S_START_OFFSET_1                    0x160720

#define mmMME2_CTRL_SHADOW_1_AGU_S_START_OFFSET_2                    0x160724

#define mmMME2_CTRL_SHADOW_1_AGU_S_START_OFFSET_3                    0x160728

#define mmMME2_CTRL_SHADOW_1_TENSOR_L_VALID_ELEMENTS_0               0x16072C

#define mmMME2_CTRL_SHADOW_1_TENSOR_L_VALID_ELEMENTS_1               0x160730

#define mmMME2_CTRL_SHADOW_1_TENSOR_L_VALID_ELEMENTS_2               0x160734

#define mmMME2_CTRL_SHADOW_1_TENSOR_L_VALID_ELEMENTS_3               0x160738

#define mmMME2_CTRL_SHADOW_1_TENSOR_L_VALID_ELEMENTS_4               0x16073C

#define mmMME2_CTRL_SHADOW_1_TENSOR_L_LOOP_STRIDE_0                  0x160740

#define mmMME2_CTRL_SHADOW_1_TENSOR_L_LOOP_STRIDE_1                  0x160744

#define mmMME2_CTRL_SHADOW_1_TENSOR_L_LOOP_STRIDE_2                  0x160748

#define mmMME2_CTRL_SHADOW_1_TENSOR_L_LOOP_STRIDE_3                  0x16074C

#define mmMME2_CTRL_SHADOW_1_TENSOR_L_LOOP_STRIDE_4                  0x160750

#define mmMME2_CTRL_SHADOW_1_TENSOR_L_ROI_SIZE_0                     0x160754

#define mmMME2_CTRL_SHADOW_1_TENSOR_L_ROI_SIZE_1                     0x160758

#define mmMME2_CTRL_SHADOW_1_TENSOR_L_ROI_SIZE_2                     0x16075C

#define mmMME2_CTRL_SHADOW_1_TENSOR_L_ROI_SIZE_3                     0x160760

#define mmMME2_CTRL_SHADOW_1_TENSOR_L_SPATIAL_STRIDES_0              0x160764

#define mmMME2_CTRL_SHADOW_1_TENSOR_L_SPATIAL_STRIDES_1              0x160768

#define mmMME2_CTRL_SHADOW_1_TENSOR_L_SPATIAL_STRIDES_2              0x16076C

#define mmMME2_CTRL_SHADOW_1_TENSOR_L_SPATIAL_STRIDES_3              0x160770

#define mmMME2_CTRL_SHADOW_1_TENSOR_L_SPATIAL_SIZE_MINUS_1           0x160774

#define mmMME2_CTRL_SHADOW_1_AGU_L_LOCAL_ROI_BASE_OFFSET_0           0x160778

#define mmMME2_CTRL_SHADOW_1_AGU_L_LOCAL_ROI_BASE_OFFSET_1           0x16077C

#define mmMME2_CTRL_SHADOW_1_AGU_L_LOCAL_ROI_BASE_OFFSET_2           0x160780

#define mmMME2_CTRL_SHADOW_1_AGU_L_LOCAL_ROI_BASE_OFFSET_3           0x160784

#define mmMME2_CTRL_SHADOW_1_AGU_L_LOCAL_ROI_BASE_OFFSET_4           0x160788

#define mmMME2_CTRL_SHADOW_1_AGU_L_LOCAL_START_OFFSET_0              0x16078C

#define mmMME2_CTRL_SHADOW_1_AGU_L_LOCAL_START_OFFSET_1              0x160790

#define mmMME2_CTRL_SHADOW_1_AGU_L_LOCAL_START_OFFSET_2              0x160794

#define mmMME2_CTRL_SHADOW_1_AGU_L_LOCAL_START_OFFSET_3              0x160798

#define mmMME2_CTRL_SHADOW_1_AGU_L_REMOTE_ROI_BASE_OFFSET_0          0x16079C

#define mmMME2_CTRL_SHADOW_1_AGU_L_REMOTE_ROI_BASE_OFFSET_1          0x1607A0

#define mmMME2_CTRL_SHADOW_1_AGU_L_REMOTE_ROI_BASE_OFFSET_2          0x1607A4

#define mmMME2_CTRL_SHADOW_1_AGU_L_REMOTE_ROI_BASE_OFFSET_3          0x1607A8

#define mmMME2_CTRL_SHADOW_1_AGU_L_REMOTE_ROI_BASE_OFFSET_4          0x1607AC

#define mmMME2_CTRL_SHADOW_1_AGU_L_REMOTE_START_OFFSET_0             0x1607B0

#define mmMME2_CTRL_SHADOW_1_AGU_L_REMOTE_START_OFFSET_1             0x1607B4

#define mmMME2_CTRL_SHADOW_1_AGU_L_REMOTE_START_OFFSET_2             0x1607B8

#define mmMME2_CTRL_SHADOW_1_AGU_L_REMOTE_START_OFFSET_3             0x1607BC

#define mmMME2_CTRL_SHADOW_1_TENSOR_O_VALID_ELEMENTS_0               0x1607C0

#define mmMME2_CTRL_SHADOW_1_TENSOR_O_VALID_ELEMENTS_1               0x1607C4

#define mmMME2_CTRL_SHADOW_1_TENSOR_O_VALID_ELEMENTS_2               0x1607C8

#define mmMME2_CTRL_SHADOW_1_TENSOR_O_VALID_ELEMENTS_3               0x1607CC

#define mmMME2_CTRL_SHADOW_1_TENSOR_O_VALID_ELEMENTS_4               0x1607D0

#define mmMME2_CTRL_SHADOW_1_TENSOR_O_LOOP_STRIDE_0                  0x1607D4

#define mmMME2_CTRL_SHADOW_1_TENSOR_O_LOOP_STRIDE_1                  0x1607D8

#define mmMME2_CTRL_SHADOW_1_TENSOR_O_LOOP_STRIDE_2                  0x1607DC

#define mmMME2_CTRL_SHADOW_1_TENSOR_O_LOOP_STRIDE_3                  0x1607E0

#define mmMME2_CTRL_SHADOW_1_TENSOR_O_LOOP_STRIDE_4                  0x1607E4

#define mmMME2_CTRL_SHADOW_1_TENSOR_O_ROI_SIZE_0                     0x1607E8

#define mmMME2_CTRL_SHADOW_1_TENSOR_O_ROI_SIZE_1                     0x1607EC

#define mmMME2_CTRL_SHADOW_1_TENSOR_O_ROI_SIZE_2                     0x1607F0

#define mmMME2_CTRL_SHADOW_1_TENSOR_O_ROI_SIZE_3                     0x1607F4

#define mmMME2_CTRL_SHADOW_1_TENSOR_O_SPATIAL_STRIDES_0              0x1607F8

#define mmMME2_CTRL_SHADOW_1_TENSOR_O_SPATIAL_STRIDES_1              0x1607FC

#define mmMME2_CTRL_SHADOW_1_TENSOR_O_SPATIAL_STRIDES_2              0x160800

#define mmMME2_CTRL_SHADOW_1_TENSOR_O_SPATIAL_STRIDES_3              0x160804

#define mmMME2_CTRL_SHADOW_1_TENSOR_O_SPATIAL_SIZE_MINUS_1           0x160808

#define mmMME2_CTRL_SHADOW_1_AGU_O_LOCAL_ROI_BASE_OFFSET_0           0x16080C

#define mmMME2_CTRL_SHADOW_1_AGU_O_LOCAL_ROI_BASE_OFFSET_1           0x160810

#define mmMME2_CTRL_SHADOW_1_AGU_O_LOCAL_ROI_BASE_OFFSET_2           0x160814

#define mmMME2_CTRL_SHADOW_1_AGU_O_LOCAL_ROI_BASE_OFFSET_3           0x160818

#define mmMME2_CTRL_SHADOW_1_AGU_O_LOCAL_ROI_BASE_OFFSET_4           0x16081C

#define mmMME2_CTRL_SHADOW_1_AGU_O_LOCAL_START_OFFSET_0              0x160820

#define mmMME2_CTRL_SHADOW_1_AGU_O_LOCAL_START_OFFSET_1              0x160824

#define mmMME2_CTRL_SHADOW_1_AGU_O_LOCAL_START_OFFSET_2              0x160828

#define mmMME2_CTRL_SHADOW_1_AGU_O_LOCAL_START_OFFSET_3              0x16082C

#define mmMME2_CTRL_SHADOW_1_AGU_O_REMOTE_ROI_BASE_OFFSET_0          0x160830

#define mmMME2_CTRL_SHADOW_1_AGU_O_REMOTE_ROI_BASE_OFFSET_1          0x160834

#define mmMME2_CTRL_SHADOW_1_AGU_O_REMOTE_ROI_BASE_OFFSET_2          0x160838

#define mmMME2_CTRL_SHADOW_1_AGU_O_REMOTE_ROI_BASE_OFFSET_3          0x16083C

#define mmMME2_CTRL_SHADOW_1_AGU_O_REMOTE_ROI_BASE_OFFSET_4          0x160840

#define mmMME2_CTRL_SHADOW_1_AGU_O_REMOTE_START_OFFSET_0             0x160844

#define mmMME2_CTRL_SHADOW_1_AGU_O_REMOTE_START_OFFSET_1             0x160848

#define mmMME2_CTRL_SHADOW_1_AGU_O_REMOTE_START_OFFSET_2             0x16084C

#define mmMME2_CTRL_SHADOW_1_AGU_O_REMOTE_START_OFFSET_3             0x160850

#define mmMME2_CTRL_SHADOW_1_DESC_SB_REPEAT                          0x160854

#define mmMME2_CTRL_SHADOW_1_DESC_RATE_LIMITER                       0x160858

#define mmMME2_CTRL_SHADOW_1_DESC_SYNC_OBJECT_ADDR_LOW_LOCAL         0x16085C

#define mmMME2_CTRL_SHADOW_1_DESC_SYNC_OBJECT_ADDR_LOW_REMOTE        0x160860

#define mmMME2_CTRL_SHADOW_1_DESC_SYNC_OBJECT_ADDR_HIGH              0x160864

#define mmMME2_CTRL_SHADOW_1_DESC_SYNC_OBJECT_DATA                   0x160868

#define mmMME2_CTRL_SHADOW_1_DESC_AXI_USER_DATA                      0x16086C

#define mmMME2_CTRL_SHADOW_1_DESC_PERF_EVT_S                         0x160870

#define mmMME2_CTRL_SHADOW_1_DESC_PERF_EVT_L_LOCAL                   0x160874

#define mmMME2_CTRL_SHADOW_1_DESC_PERF_EVT_L_REMOTE                  0x160878

#define mmMME2_CTRL_SHADOW_1_DESC_PERF_EVT_O_LOCAL                   0x16087C

#define mmMME2_CTRL_SHADOW_1_DESC_PERF_EVT_O_REMOTE                  0x160880

#define mmMME2_CTRL_SHADOW_1_DESC_PADDING_VALUE_S                    0x160884

#define mmMME2_CTRL_SHADOW_1_DESC_PADDING_VALUE_L                    0x160888

#define mmMME2_CTRL_SHADOW_1_DESC_META_DATA_AGU_S                    0x16088C

#define mmMME2_CTRL_SHADOW_1_DESC_META_DATA_AGU_L_LOCAL              0x160890

#define mmMME2_CTRL_SHADOW_1_DESC_META_DATA_AGU_L_REMOTE             0x160894

#define mmMME2_CTRL_SHADOW_1_DESC_META_DATA_AGU_O_LOCAL              0x160898

#define mmMME2_CTRL_SHADOW_1_DESC_META_DATA_AGU_O_REMOTE             0x16089C

#define mmMME2_CTRL_SHADOW_1_DESC_PCU_RL_SATURATION                  0x1608A0

#define mmMME2_CTRL_SHADOW_1_DESC_DUMMY                              0x1608A4

#define mmMME2_CTRL_SHADOW_2_STATUS                                  0x160900

#define mmMME2_CTRL_SHADOW_2_BASE_ADDR_HIGH_S                        0x160908

#define mmMME2_CTRL_SHADOW_2_BASE_ADDR_HIGH_L                        0x16090C

#define mmMME2_CTRL_SHADOW_2_BASE_ADDR_HIGH_O                        0x160910

#define mmMME2_CTRL_SHADOW_2_BASE_ADDR_LOW_S                         0x160914

#define mmMME2_CTRL_SHADOW_2_BASE_ADDR_LOW_L                         0x160918

#define mmMME2_CTRL_SHADOW_2_BASE_ADDR_LOW_O                         0x16091C

#define mmMME2_CTRL_SHADOW_2_HEADER_LOW                              0x160920

#define mmMME2_CTRL_SHADOW_2_HEADER_HIGH                             0x160924

#define mmMME2_CTRL_SHADOW_2_CONV_KERNEL_SIZE_MINUS_1                0x160928

#define mmMME2_CTRL_SHADOW_2_CONV_ASSOCIATED_DIMS_LOW                0x16092C

#define mmMME2_CTRL_SHADOW_2_CONV_ASSOCIATED_DIMS_HIGH               0x160930

#define mmMME2_CTRL_SHADOW_2_NUM_ITERATIONS_MINUS_1                  0x160934

#define mmMME2_CTRL_SHADOW_2_OUTER_LOOP                              0x160938

#define mmMME2_CTRL_SHADOW_2_TENSOR_S_VALID_ELEMENTS_0               0x16093C

#define mmMME2_CTRL_SHADOW_2_TENSOR_S_VALID_ELEMENTS_1               0x160940

#define mmMME2_CTRL_SHADOW_2_TENSOR_S_VALID_ELEMENTS_2               0x160944

#define mmMME2_CTRL_SHADOW_2_TENSOR_S_VALID_ELEMENTS_3               0x160948

#define mmMME2_CTRL_SHADOW_2_TENSOR_S_VALID_ELEMENTS_4               0x16094C

#define mmMME2_CTRL_SHADOW_2_TENSOR_S_LOOP_STRIDE_0                  0x160950

#define mmMME2_CTRL_SHADOW_2_TENSOR_S_LOOP_STRIDE_1                  0x160954

#define mmMME2_CTRL_SHADOW_2_TENSOR_S_LOOP_STRIDE_2                  0x160958

#define mmMME2_CTRL_SHADOW_2_TENSOR_S_LOOP_STRIDE_3                  0x16095C

#define mmMME2_CTRL_SHADOW_2_TENSOR_S_LOOP_STRIDE_4                  0x160960

#define mmMME2_CTRL_SHADOW_2_TENSOR_S_ROI_SIZE_0                     0x160964

#define mmMME2_CTRL_SHADOW_2_TENSOR_S_ROI_SIZE_1                     0x160968

#define mmMME2_CTRL_SHADOW_2_TENSOR_S_ROI_SIZE_2                     0x16096C

#define mmMME2_CTRL_SHADOW_2_TENSOR_S_ROI_SIZE_3                     0x160970

#define mmMME2_CTRL_SHADOW_2_TENSOR_S_SPATIAL_STRIDES_0              0x160974

#define mmMME2_CTRL_SHADOW_2_TENSOR_S_SPATIAL_STRIDES_1              0x160978

#define mmMME2_CTRL_SHADOW_2_TENSOR_S_SPATIAL_STRIDES_2              0x16097C

#define mmMME2_CTRL_SHADOW_2_TENSOR_S_SPATIAL_STRIDES_3              0x160980

#define mmMME2_CTRL_SHADOW_2_TENSOR_S_SPATIAL_SIZE_MINUS_1           0x160984

#define mmMME2_CTRL_SHADOW_2_AGU_S_ROI_BASE_OFFSET_0                 0x160988

#define mmMME2_CTRL_SHADOW_2_AGU_S_ROI_BASE_OFFSET_1                 0x16098C

#define mmMME2_CTRL_SHADOW_2_AGU_S_ROI_BASE_OFFSET_2                 0x160990

#define mmMME2_CTRL_SHADOW_2_AGU_S_ROI_BASE_OFFSET_3                 0x160994

#define mmMME2_CTRL_SHADOW_2_AGU_S_ROI_BASE_OFFSET_4                 0x160998

#define mmMME2_CTRL_SHADOW_2_AGU_S_START_OFFSET_0                    0x16099C

#define mmMME2_CTRL_SHADOW_2_AGU_S_START_OFFSET_1                    0x1609A0

#define mmMME2_CTRL_SHADOW_2_AGU_S_START_OFFSET_2                    0x1609A4

#define mmMME2_CTRL_SHADOW_2_AGU_S_START_OFFSET_3                    0x1609A8

#define mmMME2_CTRL_SHADOW_2_TENSOR_L_VALID_ELEMENTS_0               0x1609AC

#define mmMME2_CTRL_SHADOW_2_TENSOR_L_VALID_ELEMENTS_1               0x1609B0

#define mmMME2_CTRL_SHADOW_2_TENSOR_L_VALID_ELEMENTS_2               0x1609B4

#define mmMME2_CTRL_SHADOW_2_TENSOR_L_VALID_ELEMENTS_3               0x1609B8

#define mmMME2_CTRL_SHADOW_2_TENSOR_L_VALID_ELEMENTS_4               0x1609BC

#define mmMME2_CTRL_SHADOW_2_TENSOR_L_LOOP_STRIDE_0                  0x1609C0

#define mmMME2_CTRL_SHADOW_2_TENSOR_L_LOOP_STRIDE_1                  0x1609C4

#define mmMME2_CTRL_SHADOW_2_TENSOR_L_LOOP_STRIDE_2                  0x1609C8

#define mmMME2_CTRL_SHADOW_2_TENSOR_L_LOOP_STRIDE_3                  0x1609CC

#define mmMME2_CTRL_SHADOW_2_TENSOR_L_LOOP_STRIDE_4                  0x1609D0

#define mmMME2_CTRL_SHADOW_2_TENSOR_L_ROI_SIZE_0                     0x1609D4

#define mmMME2_CTRL_SHADOW_2_TENSOR_L_ROI_SIZE_1                     0x1609D8

#define mmMME2_CTRL_SHADOW_2_TENSOR_L_ROI_SIZE_2                     0x1609DC

#define mmMME2_CTRL_SHADOW_2_TENSOR_L_ROI_SIZE_3                     0x1609E0

#define mmMME2_CTRL_SHADOW_2_TENSOR_L_SPATIAL_STRIDES_0              0x1609E4

#define mmMME2_CTRL_SHADOW_2_TENSOR_L_SPATIAL_STRIDES_1              0x1609E8

#define mmMME2_CTRL_SHADOW_2_TENSOR_L_SPATIAL_STRIDES_2              0x1609EC

#define mmMME2_CTRL_SHADOW_2_TENSOR_L_SPATIAL_STRIDES_3              0x1609F0

#define mmMME2_CTRL_SHADOW_2_TENSOR_L_SPATIAL_SIZE_MINUS_1           0x1609F4

#define mmMME2_CTRL_SHADOW_2_AGU_L_LOCAL_ROI_BASE_OFFSET_0           0x1609F8

#define mmMME2_CTRL_SHADOW_2_AGU_L_LOCAL_ROI_BASE_OFFSET_1           0x1609FC

#define mmMME2_CTRL_SHADOW_2_AGU_L_LOCAL_ROI_BASE_OFFSET_2           0x160A00

#define mmMME2_CTRL_SHADOW_2_AGU_L_LOCAL_ROI_BASE_OFFSET_3           0x160A04

#define mmMME2_CTRL_SHADOW_2_AGU_L_LOCAL_ROI_BASE_OFFSET_4           0x160A08

#define mmMME2_CTRL_SHADOW_2_AGU_L_LOCAL_START_OFFSET_0              0x160A0C

#define mmMME2_CTRL_SHADOW_2_AGU_L_LOCAL_START_OFFSET_1              0x160A10

#define mmMME2_CTRL_SHADOW_2_AGU_L_LOCAL_START_OFFSET_2              0x160A14

#define mmMME2_CTRL_SHADOW_2_AGU_L_LOCAL_START_OFFSET_3              0x160A18

#define mmMME2_CTRL_SHADOW_2_AGU_L_REMOTE_ROI_BASE_OFFSET_0          0x160A1C

#define mmMME2_CTRL_SHADOW_2_AGU_L_REMOTE_ROI_BASE_OFFSET_1          0x160A20

#define mmMME2_CTRL_SHADOW_2_AGU_L_REMOTE_ROI_BASE_OFFSET_2          0x160A24

#define mmMME2_CTRL_SHADOW_2_AGU_L_REMOTE_ROI_BASE_OFFSET_3          0x160A28

#define mmMME2_CTRL_SHADOW_2_AGU_L_REMOTE_ROI_BASE_OFFSET_4          0x160A2C

#define mmMME2_CTRL_SHADOW_2_AGU_L_REMOTE_START_OFFSET_0             0x160A30

#define mmMME2_CTRL_SHADOW_2_AGU_L_REMOTE_START_OFFSET_1             0x160A34

#define mmMME2_CTRL_SHADOW_2_AGU_L_REMOTE_START_OFFSET_2             0x160A38

#define mmMME2_CTRL_SHADOW_2_AGU_L_REMOTE_START_OFFSET_3             0x160A3C

#define mmMME2_CTRL_SHADOW_2_TENSOR_O_VALID_ELEMENTS_0               0x160A40

#define mmMME2_CTRL_SHADOW_2_TENSOR_O_VALID_ELEMENTS_1               0x160A44

#define mmMME2_CTRL_SHADOW_2_TENSOR_O_VALID_ELEMENTS_2               0x160A48

#define mmMME2_CTRL_SHADOW_2_TENSOR_O_VALID_ELEMENTS_3               0x160A4C

#define mmMME2_CTRL_SHADOW_2_TENSOR_O_VALID_ELEMENTS_4               0x160A50

#define mmMME2_CTRL_SHADOW_2_TENSOR_O_LOOP_STRIDE_0                  0x160A54

#define mmMME2_CTRL_SHADOW_2_TENSOR_O_LOOP_STRIDE_1                  0x160A58

#define mmMME2_CTRL_SHADOW_2_TENSOR_O_LOOP_STRIDE_2                  0x160A5C

#define mmMME2_CTRL_SHADOW_2_TENSOR_O_LOOP_STRIDE_3                  0x160A60

#define mmMME2_CTRL_SHADOW_2_TENSOR_O_LOOP_STRIDE_4                  0x160A64

#define mmMME2_CTRL_SHADOW_2_TENSOR_O_ROI_SIZE_0                     0x160A68

#define mmMME2_CTRL_SHADOW_2_TENSOR_O_ROI_SIZE_1                     0x160A6C

#define mmMME2_CTRL_SHADOW_2_TENSOR_O_ROI_SIZE_2                     0x160A70

#define mmMME2_CTRL_SHADOW_2_TENSOR_O_ROI_SIZE_3                     0x160A74

#define mmMME2_CTRL_SHADOW_2_TENSOR_O_SPATIAL_STRIDES_0              0x160A78

#define mmMME2_CTRL_SHADOW_2_TENSOR_O_SPATIAL_STRIDES_1              0x160A7C

#define mmMME2_CTRL_SHADOW_2_TENSOR_O_SPATIAL_STRIDES_2              0x160A80

#define mmMME2_CTRL_SHADOW_2_TENSOR_O_SPATIAL_STRIDES_3              0x160A84

#define mmMME2_CTRL_SHADOW_2_TENSOR_O_SPATIAL_SIZE_MINUS_1           0x160A88

#define mmMME2_CTRL_SHADOW_2_AGU_O_LOCAL_ROI_BASE_OFFSET_0           0x160A8C

#define mmMME2_CTRL_SHADOW_2_AGU_O_LOCAL_ROI_BASE_OFFSET_1           0x160A90

#define mmMME2_CTRL_SHADOW_2_AGU_O_LOCAL_ROI_BASE_OFFSET_2           0x160A94

#define mmMME2_CTRL_SHADOW_2_AGU_O_LOCAL_ROI_BASE_OFFSET_3           0x160A98

#define mmMME2_CTRL_SHADOW_2_AGU_O_LOCAL_ROI_BASE_OFFSET_4           0x160A9C

#define mmMME2_CTRL_SHADOW_2_AGU_O_LOCAL_START_OFFSET_0              0x160AA0

#define mmMME2_CTRL_SHADOW_2_AGU_O_LOCAL_START_OFFSET_1              0x160AA4

#define mmMME2_CTRL_SHADOW_2_AGU_O_LOCAL_START_OFFSET_2              0x160AA8

#define mmMME2_CTRL_SHADOW_2_AGU_O_LOCAL_START_OFFSET_3              0x160AAC

#define mmMME2_CTRL_SHADOW_2_AGU_O_REMOTE_ROI_BASE_OFFSET_0          0x160AB0

#define mmMME2_CTRL_SHADOW_2_AGU_O_REMOTE_ROI_BASE_OFFSET_1          0x160AB4

#define mmMME2_CTRL_SHADOW_2_AGU_O_REMOTE_ROI_BASE_OFFSET_2          0x160AB8

#define mmMME2_CTRL_SHADOW_2_AGU_O_REMOTE_ROI_BASE_OFFSET_3          0x160ABC

#define mmMME2_CTRL_SHADOW_2_AGU_O_REMOTE_ROI_BASE_OFFSET_4          0x160AC0

#define mmMME2_CTRL_SHADOW_2_AGU_O_REMOTE_START_OFFSET_0             0x160AC4

#define mmMME2_CTRL_SHADOW_2_AGU_O_REMOTE_START_OFFSET_1             0x160AC8

#define mmMME2_CTRL_SHADOW_2_AGU_O_REMOTE_START_OFFSET_2             0x160ACC

#define mmMME2_CTRL_SHADOW_2_AGU_O_REMOTE_START_OFFSET_3             0x160AD0

#define mmMME2_CTRL_SHADOW_2_DESC_SB_REPEAT                          0x160AD4

#define mmMME2_CTRL_SHADOW_2_DESC_RATE_LIMITER                       0x160AD8

#define mmMME2_CTRL_SHADOW_2_DESC_SYNC_OBJECT_ADDR_LOW_LOCAL         0x160ADC

#define mmMME2_CTRL_SHADOW_2_DESC_SYNC_OBJECT_ADDR_LOW_REMOTE        0x160AE0

#define mmMME2_CTRL_SHADOW_2_DESC_SYNC_OBJECT_ADDR_HIGH              0x160AE4

#define mmMME2_CTRL_SHADOW_2_DESC_SYNC_OBJECT_DATA                   0x160AE8

#define mmMME2_CTRL_SHADOW_2_DESC_AXI_USER_DATA                      0x160AEC

#define mmMME2_CTRL_SHADOW_2_DESC_PERF_EVT_S                         0x160AF0

#define mmMME2_CTRL_SHADOW_2_DESC_PERF_EVT_L_LOCAL                   0x160AF4

#define mmMME2_CTRL_SHADOW_2_DESC_PERF_EVT_L_REMOTE                  0x160AF8

#define mmMME2_CTRL_SHADOW_2_DESC_PERF_EVT_O_LOCAL                   0x160AFC

#define mmMME2_CTRL_SHADOW_2_DESC_PERF_EVT_O_REMOTE                  0x160B00

#define mmMME2_CTRL_SHADOW_2_DESC_PADDING_VALUE_S                    0x160B04

#define mmMME2_CTRL_SHADOW_2_DESC_PADDING_VALUE_L                    0x160B08

#define mmMME2_CTRL_SHADOW_2_DESC_META_DATA_AGU_S                    0x160B0C

#define mmMME2_CTRL_SHADOW_2_DESC_META_DATA_AGU_L_LOCAL              0x160B10

#define mmMME2_CTRL_SHADOW_2_DESC_META_DATA_AGU_L_REMOTE             0x160B14

#define mmMME2_CTRL_SHADOW_2_DESC_META_DATA_AGU_O_LOCAL              0x160B18

#define mmMME2_CTRL_SHADOW_2_DESC_META_DATA_AGU_O_REMOTE             0x160B1C

#define mmMME2_CTRL_SHADOW_2_DESC_PCU_RL_SATURATION                  0x160B20

#define mmMME2_CTRL_SHADOW_2_DESC_DUMMY                              0x160B24

#define mmMME2_CTRL_SHADOW_3_STATUS                                  0x160B80

#define mmMME2_CTRL_SHADOW_3_BASE_ADDR_HIGH_S                        0x160B88

#define mmMME2_CTRL_SHADOW_3_BASE_ADDR_HIGH_L                        0x160B8C

#define mmMME2_CTRL_SHADOW_3_BASE_ADDR_HIGH_O                        0x160B90

#define mmMME2_CTRL_SHADOW_3_BASE_ADDR_LOW_S                         0x160B94

#define mmMME2_CTRL_SHADOW_3_BASE_ADDR_LOW_L                         0x160B98

#define mmMME2_CTRL_SHADOW_3_BASE_ADDR_LOW_O                         0x160B9C

#define mmMME2_CTRL_SHADOW_3_HEADER_LOW                              0x160BA0

#define mmMME2_CTRL_SHADOW_3_HEADER_HIGH                             0x160BA4

#define mmMME2_CTRL_SHADOW_3_CONV_KERNEL_SIZE_MINUS_1                0x160BA8

#define mmMME2_CTRL_SHADOW_3_CONV_ASSOCIATED_DIMS_LOW                0x160BAC

#define mmMME2_CTRL_SHADOW_3_CONV_ASSOCIATED_DIMS_HIGH               0x160BB0

#define mmMME2_CTRL_SHADOW_3_NUM_ITERATIONS_MINUS_1                  0x160BB4

#define mmMME2_CTRL_SHADOW_3_OUTER_LOOP                              0x160BB8

#define mmMME2_CTRL_SHADOW_3_TENSOR_S_VALID_ELEMENTS_0               0x160BBC

#define mmMME2_CTRL_SHADOW_3_TENSOR_S_VALID_ELEMENTS_1               0x160BC0

#define mmMME2_CTRL_SHADOW_3_TENSOR_S_VALID_ELEMENTS_2               0x160BC4

#define mmMME2_CTRL_SHADOW_3_TENSOR_S_VALID_ELEMENTS_3               0x160BC8

#define mmMME2_CTRL_SHADOW_3_TENSOR_S_VALID_ELEMENTS_4               0x160BCC

#define mmMME2_CTRL_SHADOW_3_TENSOR_S_LOOP_STRIDE_0                  0x160BD0

#define mmMME2_CTRL_SHADOW_3_TENSOR_S_LOOP_STRIDE_1                  0x160BD4

#define mmMME2_CTRL_SHADOW_3_TENSOR_S_LOOP_STRIDE_2                  0x160BD8

#define mmMME2_CTRL_SHADOW_3_TENSOR_S_LOOP_STRIDE_3                  0x160BDC

#define mmMME2_CTRL_SHADOW_3_TENSOR_S_LOOP_STRIDE_4                  0x160BE0

#define mmMME2_CTRL_SHADOW_3_TENSOR_S_ROI_SIZE_0                     0x160BE4

#define mmMME2_CTRL_SHADOW_3_TENSOR_S_ROI_SIZE_1                     0x160BE8

#define mmMME2_CTRL_SHADOW_3_TENSOR_S_ROI_SIZE_2                     0x160BEC

#define mmMME2_CTRL_SHADOW_3_TENSOR_S_ROI_SIZE_3                     0x160BF0

#define mmMME2_CTRL_SHADOW_3_TENSOR_S_SPATIAL_STRIDES_0              0x160BF4

#define mmMME2_CTRL_SHADOW_3_TENSOR_S_SPATIAL_STRIDES_1              0x160BF8

#define mmMME2_CTRL_SHADOW_3_TENSOR_S_SPATIAL_STRIDES_2              0x160BFC

#define mmMME2_CTRL_SHADOW_3_TENSOR_S_SPATIAL_STRIDES_3              0x160C00

#define mmMME2_CTRL_SHADOW_3_TENSOR_S_SPATIAL_SIZE_MINUS_1           0x160C04

#define mmMME2_CTRL_SHADOW_3_AGU_S_ROI_BASE_OFFSET_0                 0x160C08

#define mmMME2_CTRL_SHADOW_3_AGU_S_ROI_BASE_OFFSET_1                 0x160C0C

#define mmMME2_CTRL_SHADOW_3_AGU_S_ROI_BASE_OFFSET_2                 0x160C10

#define mmMME2_CTRL_SHADOW_3_AGU_S_ROI_BASE_OFFSET_3                 0x160C14

#define mmMME2_CTRL_SHADOW_3_AGU_S_ROI_BASE_OFFSET_4                 0x160C18

#define mmMME2_CTRL_SHADOW_3_AGU_S_START_OFFSET_0                    0x160C1C

#define mmMME2_CTRL_SHADOW_3_AGU_S_START_OFFSET_1                    0x160C20

#define mmMME2_CTRL_SHADOW_3_AGU_S_START_OFFSET_2                    0x160C24

#define mmMME2_CTRL_SHADOW_3_AGU_S_START_OFFSET_3                    0x160C28

#define mmMME2_CTRL_SHADOW_3_TENSOR_L_VALID_ELEMENTS_0               0x160C2C

#define mmMME2_CTRL_SHADOW_3_TENSOR_L_VALID_ELEMENTS_1               0x160C30

#define mmMME2_CTRL_SHADOW_3_TENSOR_L_VALID_ELEMENTS_2               0x160C34

#define mmMME2_CTRL_SHADOW_3_TENSOR_L_VALID_ELEMENTS_3               0x160C38

#define mmMME2_CTRL_SHADOW_3_TENSOR_L_VALID_ELEMENTS_4               0x160C3C

#define mmMME2_CTRL_SHADOW_3_TENSOR_L_LOOP_STRIDE_0                  0x160C40

#define mmMME2_CTRL_SHADOW_3_TENSOR_L_LOOP_STRIDE_1                  0x160C44

#define mmMME2_CTRL_SHADOW_3_TENSOR_L_LOOP_STRIDE_2                  0x160C48

#define mmMME2_CTRL_SHADOW_3_TENSOR_L_LOOP_STRIDE_3                  0x160C4C

#define mmMME2_CTRL_SHADOW_3_TENSOR_L_LOOP_STRIDE_4                  0x160C50

#define mmMME2_CTRL_SHADOW_3_TENSOR_L_ROI_SIZE_0                     0x160C54

#define mmMME2_CTRL_SHADOW_3_TENSOR_L_ROI_SIZE_1                     0x160C58

#define mmMME2_CTRL_SHADOW_3_TENSOR_L_ROI_SIZE_2                     0x160C5C

#define mmMME2_CTRL_SHADOW_3_TENSOR_L_ROI_SIZE_3                     0x160C60

#define mmMME2_CTRL_SHADOW_3_TENSOR_L_SPATIAL_STRIDES_0              0x160C64

#define mmMME2_CTRL_SHADOW_3_TENSOR_L_SPATIAL_STRIDES_1              0x160C68

#define mmMME2_CTRL_SHADOW_3_TENSOR_L_SPATIAL_STRIDES_2              0x160C6C

#define mmMME2_CTRL_SHADOW_3_TENSOR_L_SPATIAL_STRIDES_3              0x160C70

#define mmMME2_CTRL_SHADOW_3_TENSOR_L_SPATIAL_SIZE_MINUS_1           0x160C74

#define mmMME2_CTRL_SHADOW_3_AGU_L_LOCAL_ROI_BASE_OFFSET_0           0x160C78

#define mmMME2_CTRL_SHADOW_3_AGU_L_LOCAL_ROI_BASE_OFFSET_1           0x160C7C

#define mmMME2_CTRL_SHADOW_3_AGU_L_LOCAL_ROI_BASE_OFFSET_2           0x160C80

#define mmMME2_CTRL_SHADOW_3_AGU_L_LOCAL_ROI_BASE_OFFSET_3           0x160C84

#define mmMME2_CTRL_SHADOW_3_AGU_L_LOCAL_ROI_BASE_OFFSET_4           0x160C88

#define mmMME2_CTRL_SHADOW_3_AGU_L_LOCAL_START_OFFSET_0              0x160C8C

#define mmMME2_CTRL_SHADOW_3_AGU_L_LOCAL_START_OFFSET_1              0x160C90

#define mmMME2_CTRL_SHADOW_3_AGU_L_LOCAL_START_OFFSET_2              0x160C94

#define mmMME2_CTRL_SHADOW_3_AGU_L_LOCAL_START_OFFSET_3              0x160C98

#define mmMME2_CTRL_SHADOW_3_AGU_L_REMOTE_ROI_BASE_OFFSET_0          0x160C9C

#define mmMME2_CTRL_SHADOW_3_AGU_L_REMOTE_ROI_BASE_OFFSET_1          0x160CA0

#define mmMME2_CTRL_SHADOW_3_AGU_L_REMOTE_ROI_BASE_OFFSET_2          0x160CA4

#define mmMME2_CTRL_SHADOW_3_AGU_L_REMOTE_ROI_BASE_OFFSET_3          0x160CA8

#define mmMME2_CTRL_SHADOW_3_AGU_L_REMOTE_ROI_BASE_OFFSET_4          0x160CAC

#define mmMME2_CTRL_SHADOW_3_AGU_L_REMOTE_START_OFFSET_0             0x160CB0

#define mmMME2_CTRL_SHADOW_3_AGU_L_REMOTE_START_OFFSET_1             0x160CB4

#define mmMME2_CTRL_SHADOW_3_AGU_L_REMOTE_START_OFFSET_2             0x160CB8

#define mmMME2_CTRL_SHADOW_3_AGU_L_REMOTE_START_OFFSET_3             0x160CBC

#define mmMME2_CTRL_SHADOW_3_TENSOR_O_VALID_ELEMENTS_0               0x160CC0

#define mmMME2_CTRL_SHADOW_3_TENSOR_O_VALID_ELEMENTS_1               0x160CC4

#define mmMME2_CTRL_SHADOW_3_TENSOR_O_VALID_ELEMENTS_2               0x160CC8

#define mmMME2_CTRL_SHADOW_3_TENSOR_O_VALID_ELEMENTS_3               0x160CCC

#define mmMME2_CTRL_SHADOW_3_TENSOR_O_VALID_ELEMENTS_4               0x160CD0

#define mmMME2_CTRL_SHADOW_3_TENSOR_O_LOOP_STRIDE_0                  0x160CD4

#define mmMME2_CTRL_SHADOW_3_TENSOR_O_LOOP_STRIDE_1                  0x160CD8

#define mmMME2_CTRL_SHADOW_3_TENSOR_O_LOOP_STRIDE_2                  0x160CDC

#define mmMME2_CTRL_SHADOW_3_TENSOR_O_LOOP_STRIDE_3                  0x160CE0

#define mmMME2_CTRL_SHADOW_3_TENSOR_O_LOOP_STRIDE_4                  0x160CE4

#define mmMME2_CTRL_SHADOW_3_TENSOR_O_ROI_SIZE_0                     0x160CE8

#define mmMME2_CTRL_SHADOW_3_TENSOR_O_ROI_SIZE_1                     0x160CEC

#define mmMME2_CTRL_SHADOW_3_TENSOR_O_ROI_SIZE_2                     0x160CF0

#define mmMME2_CTRL_SHADOW_3_TENSOR_O_ROI_SIZE_3                     0x160CF4

#define mmMME2_CTRL_SHADOW_3_TENSOR_O_SPATIAL_STRIDES_0              0x160CF8

#define mmMME2_CTRL_SHADOW_3_TENSOR_O_SPATIAL_STRIDES_1              0x160CFC

#define mmMME2_CTRL_SHADOW_3_TENSOR_O_SPATIAL_STRIDES_2              0x160D00

#define mmMME2_CTRL_SHADOW_3_TENSOR_O_SPATIAL_STRIDES_3              0x160D04

#define mmMME2_CTRL_SHADOW_3_TENSOR_O_SPATIAL_SIZE_MINUS_1           0x160D08

#define mmMME2_CTRL_SHADOW_3_AGU_O_LOCAL_ROI_BASE_OFFSET_0           0x160D0C

#define mmMME2_CTRL_SHADOW_3_AGU_O_LOCAL_ROI_BASE_OFFSET_1           0x160D10

#define mmMME2_CTRL_SHADOW_3_AGU_O_LOCAL_ROI_BASE_OFFSET_2           0x160D14

#define mmMME2_CTRL_SHADOW_3_AGU_O_LOCAL_ROI_BASE_OFFSET_3           0x160D18

#define mmMME2_CTRL_SHADOW_3_AGU_O_LOCAL_ROI_BASE_OFFSET_4           0x160D1C

#define mmMME2_CTRL_SHADOW_3_AGU_O_LOCAL_START_OFFSET_0              0x160D20

#define mmMME2_CTRL_SHADOW_3_AGU_O_LOCAL_START_OFFSET_1              0x160D24

#define mmMME2_CTRL_SHADOW_3_AGU_O_LOCAL_START_OFFSET_2              0x160D28

#define mmMME2_CTRL_SHADOW_3_AGU_O_LOCAL_START_OFFSET_3              0x160D2C

#define mmMME2_CTRL_SHADOW_3_AGU_O_REMOTE_ROI_BASE_OFFSET_0          0x160D30

#define mmMME2_CTRL_SHADOW_3_AGU_O_REMOTE_ROI_BASE_OFFSET_1          0x160D34

#define mmMME2_CTRL_SHADOW_3_AGU_O_REMOTE_ROI_BASE_OFFSET_2          0x160D38

#define mmMME2_CTRL_SHADOW_3_AGU_O_REMOTE_ROI_BASE_OFFSET_3          0x160D3C

#define mmMME2_CTRL_SHADOW_3_AGU_O_REMOTE_ROI_BASE_OFFSET_4          0x160D40

#define mmMME2_CTRL_SHADOW_3_AGU_O_REMOTE_START_OFFSET_0             0x160D44

#define mmMME2_CTRL_SHADOW_3_AGU_O_REMOTE_START_OFFSET_1             0x160D48

#define mmMME2_CTRL_SHADOW_3_AGU_O_REMOTE_START_OFFSET_2             0x160D4C

#define mmMME2_CTRL_SHADOW_3_AGU_O_REMOTE_START_OFFSET_3             0x160D50

#define mmMME2_CTRL_SHADOW_3_DESC_SB_REPEAT                          0x160D54

#define mmMME2_CTRL_SHADOW_3_DESC_RATE_LIMITER                       0x160D58

#define mmMME2_CTRL_SHADOW_3_DESC_SYNC_OBJECT_ADDR_LOW_LOCAL         0x160D5C

#define mmMME2_CTRL_SHADOW_3_DESC_SYNC_OBJECT_ADDR_LOW_REMOTE        0x160D60

#define mmMME2_CTRL_SHADOW_3_DESC_SYNC_OBJECT_ADDR_HIGH              0x160D64

#define mmMME2_CTRL_SHADOW_3_DESC_SYNC_OBJECT_DATA                   0x160D68

#define mmMME2_CTRL_SHADOW_3_DESC_AXI_USER_DATA                      0x160D6C

#define mmMME2_CTRL_SHADOW_3_DESC_PERF_EVT_S                         0x160D70

#define mmMME2_CTRL_SHADOW_3_DESC_PERF_EVT_L_LOCAL                   0x160D74

#define mmMME2_CTRL_SHADOW_3_DESC_PERF_EVT_L_REMOTE                  0x160D78

#define mmMME2_CTRL_SHADOW_3_DESC_PERF_EVT_O_LOCAL                   0x160D7C

#define mmMME2_CTRL_SHADOW_3_DESC_PERF_EVT_O_REMOTE                  0x160D80

#define mmMME2_CTRL_SHADOW_3_DESC_PADDING_VALUE_S                    0x160D84

#define mmMME2_CTRL_SHADOW_3_DESC_PADDING_VALUE_L                    0x160D88

#define mmMME2_CTRL_SHADOW_3_DESC_META_DATA_AGU_S                    0x160D8C

#define mmMME2_CTRL_SHADOW_3_DESC_META_DATA_AGU_L_LOCAL              0x160D90

#define mmMME2_CTRL_SHADOW_3_DESC_META_DATA_AGU_L_REMOTE             0x160D94

#define mmMME2_CTRL_SHADOW_3_DESC_META_DATA_AGU_O_LOCAL              0x160D98

#define mmMME2_CTRL_SHADOW_3_DESC_META_DATA_AGU_O_REMOTE             0x160D9C

#define mmMME2_CTRL_SHADOW_3_DESC_PCU_RL_SATURATION                  0x160DA0

#define mmMME2_CTRL_SHADOW_3_DESC_DUMMY                              0x160DA4

#endif /* ASIC_REG_MME2_CTRL_REGS_H_ */
