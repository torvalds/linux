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

#ifndef ASIC_REG_MME0_CTRL_REGS_H_
#define ASIC_REG_MME0_CTRL_REGS_H_

/*
 *****************************************
 *   MME0_CTRL (Prototype: MME)
 *****************************************
 */

#define mmMME0_CTRL_ARCH_STATUS                                      0x60000

#define mmMME0_CTRL_ARCH_BASE_ADDR_HIGH_S                            0x60008

#define mmMME0_CTRL_ARCH_BASE_ADDR_HIGH_L                            0x6000C

#define mmMME0_CTRL_ARCH_BASE_ADDR_HIGH_O                            0x60010

#define mmMME0_CTRL_ARCH_BASE_ADDR_LOW_S                             0x60014

#define mmMME0_CTRL_ARCH_BASE_ADDR_LOW_L                             0x60018

#define mmMME0_CTRL_ARCH_BASE_ADDR_LOW_O                             0x6001C

#define mmMME0_CTRL_ARCH_HEADER_LOW                                  0x60020

#define mmMME0_CTRL_ARCH_HEADER_HIGH                                 0x60024

#define mmMME0_CTRL_ARCH_CONV_KERNEL_SIZE_MINUS_1                    0x60028

#define mmMME0_CTRL_ARCH_CONV_ASSOCIATED_DIMS_LOW                    0x6002C

#define mmMME0_CTRL_ARCH_CONV_ASSOCIATED_DIMS_HIGH                   0x60030

#define mmMME0_CTRL_ARCH_NUM_ITERATIONS_MINUS_1                      0x60034

#define mmMME0_CTRL_ARCH_OUTER_LOOP                                  0x60038

#define mmMME0_CTRL_ARCH_TENSOR_S_VALID_ELEMENTS_0                   0x6003C

#define mmMME0_CTRL_ARCH_TENSOR_S_VALID_ELEMENTS_1                   0x60040

#define mmMME0_CTRL_ARCH_TENSOR_S_VALID_ELEMENTS_2                   0x60044

#define mmMME0_CTRL_ARCH_TENSOR_S_VALID_ELEMENTS_3                   0x60048

#define mmMME0_CTRL_ARCH_TENSOR_S_VALID_ELEMENTS_4                   0x6004C

#define mmMME0_CTRL_ARCH_TENSOR_S_LOOP_STRIDE_0                      0x60050

#define mmMME0_CTRL_ARCH_TENSOR_S_LOOP_STRIDE_1                      0x60054

#define mmMME0_CTRL_ARCH_TENSOR_S_LOOP_STRIDE_2                      0x60058

#define mmMME0_CTRL_ARCH_TENSOR_S_LOOP_STRIDE_3                      0x6005C

#define mmMME0_CTRL_ARCH_TENSOR_S_LOOP_STRIDE_4                      0x60060

#define mmMME0_CTRL_ARCH_TENSOR_S_ROI_SIZE_0                         0x60064

#define mmMME0_CTRL_ARCH_TENSOR_S_ROI_SIZE_1                         0x60068

#define mmMME0_CTRL_ARCH_TENSOR_S_ROI_SIZE_2                         0x6006C

#define mmMME0_CTRL_ARCH_TENSOR_S_ROI_SIZE_3                         0x60070

#define mmMME0_CTRL_ARCH_TENSOR_S_SPATIAL_STRIDES_0                  0x60074

#define mmMME0_CTRL_ARCH_TENSOR_S_SPATIAL_STRIDES_1                  0x60078

#define mmMME0_CTRL_ARCH_TENSOR_S_SPATIAL_STRIDES_2                  0x6007C

#define mmMME0_CTRL_ARCH_TENSOR_S_SPATIAL_STRIDES_3                  0x60080

#define mmMME0_CTRL_ARCH_TENSOR_S_SPATIAL_SIZE_MINUS_1               0x60084

#define mmMME0_CTRL_ARCH_AGU_S_ROI_BASE_OFFSET_0                     0x60088

#define mmMME0_CTRL_ARCH_AGU_S_ROI_BASE_OFFSET_1                     0x6008C

#define mmMME0_CTRL_ARCH_AGU_S_ROI_BASE_OFFSET_2                     0x60090

#define mmMME0_CTRL_ARCH_AGU_S_ROI_BASE_OFFSET_3                     0x60094

#define mmMME0_CTRL_ARCH_AGU_S_ROI_BASE_OFFSET_4                     0x60098

#define mmMME0_CTRL_ARCH_AGU_S_START_OFFSET_0                        0x6009C

#define mmMME0_CTRL_ARCH_AGU_S_START_OFFSET_1                        0x600A0

#define mmMME0_CTRL_ARCH_AGU_S_START_OFFSET_2                        0x600A4

#define mmMME0_CTRL_ARCH_AGU_S_START_OFFSET_3                        0x600A8

#define mmMME0_CTRL_ARCH_TENSOR_L_VALID_ELEMENTS_0                   0x600AC

#define mmMME0_CTRL_ARCH_TENSOR_L_VALID_ELEMENTS_1                   0x600B0

#define mmMME0_CTRL_ARCH_TENSOR_L_VALID_ELEMENTS_2                   0x600B4

#define mmMME0_CTRL_ARCH_TENSOR_L_VALID_ELEMENTS_3                   0x600B8

#define mmMME0_CTRL_ARCH_TENSOR_L_VALID_ELEMENTS_4                   0x600BC

#define mmMME0_CTRL_ARCH_TENSOR_L_LOOP_STRIDE_0                      0x600C0

#define mmMME0_CTRL_ARCH_TENSOR_L_LOOP_STRIDE_1                      0x600C4

#define mmMME0_CTRL_ARCH_TENSOR_L_LOOP_STRIDE_2                      0x600C8

#define mmMME0_CTRL_ARCH_TENSOR_L_LOOP_STRIDE_3                      0x600CC

#define mmMME0_CTRL_ARCH_TENSOR_L_LOOP_STRIDE_4                      0x600D0

#define mmMME0_CTRL_ARCH_TENSOR_L_ROI_SIZE_0                         0x600D4

#define mmMME0_CTRL_ARCH_TENSOR_L_ROI_SIZE_1                         0x600D8

#define mmMME0_CTRL_ARCH_TENSOR_L_ROI_SIZE_2                         0x600DC

#define mmMME0_CTRL_ARCH_TENSOR_L_ROI_SIZE_3                         0x600E0

#define mmMME0_CTRL_ARCH_TENSOR_L_SPATIAL_STRIDES_0                  0x600E4

#define mmMME0_CTRL_ARCH_TENSOR_L_SPATIAL_STRIDES_1                  0x600E8

#define mmMME0_CTRL_ARCH_TENSOR_L_SPATIAL_STRIDES_2                  0x600EC

#define mmMME0_CTRL_ARCH_TENSOR_L_SPATIAL_STRIDES_3                  0x600F0

#define mmMME0_CTRL_ARCH_TENSOR_L_SPATIAL_SIZE_MINUS_1               0x600F4

#define mmMME0_CTRL_ARCH_AGU_L_LOCAL_ROI_BASE_OFFSET_0               0x600F8

#define mmMME0_CTRL_ARCH_AGU_L_LOCAL_ROI_BASE_OFFSET_1               0x600FC

#define mmMME0_CTRL_ARCH_AGU_L_LOCAL_ROI_BASE_OFFSET_2               0x60100

#define mmMME0_CTRL_ARCH_AGU_L_LOCAL_ROI_BASE_OFFSET_3               0x60104

#define mmMME0_CTRL_ARCH_AGU_L_LOCAL_ROI_BASE_OFFSET_4               0x60108

#define mmMME0_CTRL_ARCH_AGU_L_LOCAL_START_OFFSET_0                  0x6010C

#define mmMME0_CTRL_ARCH_AGU_L_LOCAL_START_OFFSET_1                  0x60110

#define mmMME0_CTRL_ARCH_AGU_L_LOCAL_START_OFFSET_2                  0x60114

#define mmMME0_CTRL_ARCH_AGU_L_LOCAL_START_OFFSET_3                  0x60118

#define mmMME0_CTRL_ARCH_AGU_L_REMOTE_ROI_BASE_OFFSET_0              0x6011C

#define mmMME0_CTRL_ARCH_AGU_L_REMOTE_ROI_BASE_OFFSET_1              0x60120

#define mmMME0_CTRL_ARCH_AGU_L_REMOTE_ROI_BASE_OFFSET_2              0x60124

#define mmMME0_CTRL_ARCH_AGU_L_REMOTE_ROI_BASE_OFFSET_3              0x60128

#define mmMME0_CTRL_ARCH_AGU_L_REMOTE_ROI_BASE_OFFSET_4              0x6012C

#define mmMME0_CTRL_ARCH_AGU_L_REMOTE_START_OFFSET_0                 0x60130

#define mmMME0_CTRL_ARCH_AGU_L_REMOTE_START_OFFSET_1                 0x60134

#define mmMME0_CTRL_ARCH_AGU_L_REMOTE_START_OFFSET_2                 0x60138

#define mmMME0_CTRL_ARCH_AGU_L_REMOTE_START_OFFSET_3                 0x6013C

#define mmMME0_CTRL_ARCH_TENSOR_O_VALID_ELEMENTS_0                   0x60140

#define mmMME0_CTRL_ARCH_TENSOR_O_VALID_ELEMENTS_1                   0x60144

#define mmMME0_CTRL_ARCH_TENSOR_O_VALID_ELEMENTS_2                   0x60148

#define mmMME0_CTRL_ARCH_TENSOR_O_VALID_ELEMENTS_3                   0x6014C

#define mmMME0_CTRL_ARCH_TENSOR_O_VALID_ELEMENTS_4                   0x60150

#define mmMME0_CTRL_ARCH_TENSOR_O_LOOP_STRIDE_0                      0x60154

#define mmMME0_CTRL_ARCH_TENSOR_O_LOOP_STRIDE_1                      0x60158

#define mmMME0_CTRL_ARCH_TENSOR_O_LOOP_STRIDE_2                      0x6015C

#define mmMME0_CTRL_ARCH_TENSOR_O_LOOP_STRIDE_3                      0x60160

#define mmMME0_CTRL_ARCH_TENSOR_O_LOOP_STRIDE_4                      0x60164

#define mmMME0_CTRL_ARCH_TENSOR_O_ROI_SIZE_0                         0x60168

#define mmMME0_CTRL_ARCH_TENSOR_O_ROI_SIZE_1                         0x6016C

#define mmMME0_CTRL_ARCH_TENSOR_O_ROI_SIZE_2                         0x60170

#define mmMME0_CTRL_ARCH_TENSOR_O_ROI_SIZE_3                         0x60174

#define mmMME0_CTRL_ARCH_TENSOR_O_SPATIAL_STRIDES_0                  0x60178

#define mmMME0_CTRL_ARCH_TENSOR_O_SPATIAL_STRIDES_1                  0x6017C

#define mmMME0_CTRL_ARCH_TENSOR_O_SPATIAL_STRIDES_2                  0x60180

#define mmMME0_CTRL_ARCH_TENSOR_O_SPATIAL_STRIDES_3                  0x60184

#define mmMME0_CTRL_ARCH_TENSOR_O_SPATIAL_SIZE_MINUS_1               0x60188

#define mmMME0_CTRL_ARCH_AGU_O_LOCAL_ROI_BASE_OFFSET_0               0x6018C

#define mmMME0_CTRL_ARCH_AGU_O_LOCAL_ROI_BASE_OFFSET_1               0x60190

#define mmMME0_CTRL_ARCH_AGU_O_LOCAL_ROI_BASE_OFFSET_2               0x60194

#define mmMME0_CTRL_ARCH_AGU_O_LOCAL_ROI_BASE_OFFSET_3               0x60198

#define mmMME0_CTRL_ARCH_AGU_O_LOCAL_ROI_BASE_OFFSET_4               0x6019C

#define mmMME0_CTRL_ARCH_AGU_O_LOCAL_START_OFFSET_0                  0x601A0

#define mmMME0_CTRL_ARCH_AGU_O_LOCAL_START_OFFSET_1                  0x601A4

#define mmMME0_CTRL_ARCH_AGU_O_LOCAL_START_OFFSET_2                  0x601A8

#define mmMME0_CTRL_ARCH_AGU_O_LOCAL_START_OFFSET_3                  0x601AC

#define mmMME0_CTRL_ARCH_AGU_O_REMOTE_ROI_BASE_OFFSET_0              0x601B0

#define mmMME0_CTRL_ARCH_AGU_O_REMOTE_ROI_BASE_OFFSET_1              0x601B4

#define mmMME0_CTRL_ARCH_AGU_O_REMOTE_ROI_BASE_OFFSET_2              0x601B8

#define mmMME0_CTRL_ARCH_AGU_O_REMOTE_ROI_BASE_OFFSET_3              0x601BC

#define mmMME0_CTRL_ARCH_AGU_O_REMOTE_ROI_BASE_OFFSET_4              0x601C0

#define mmMME0_CTRL_ARCH_AGU_O_REMOTE_START_OFFSET_0                 0x601C4

#define mmMME0_CTRL_ARCH_AGU_O_REMOTE_START_OFFSET_1                 0x601C8

#define mmMME0_CTRL_ARCH_AGU_O_REMOTE_START_OFFSET_2                 0x601CC

#define mmMME0_CTRL_ARCH_AGU_O_REMOTE_START_OFFSET_3                 0x601D0

#define mmMME0_CTRL_ARCH_DESC_SB_REPEAT                              0x601D4

#define mmMME0_CTRL_ARCH_DESC_RATE_LIMITER                           0x601D8

#define mmMME0_CTRL_ARCH_DESC_SYNC_OBJECT_ADDR_LOW_LOCAL             0x601DC

#define mmMME0_CTRL_ARCH_DESC_SYNC_OBJECT_ADDR_LOW_REMOTE            0x601E0

#define mmMME0_CTRL_ARCH_DESC_SYNC_OBJECT_ADDR_HIGH                  0x601E4

#define mmMME0_CTRL_ARCH_DESC_SYNC_OBJECT_DATA                       0x601E8

#define mmMME0_CTRL_ARCH_DESC_AXI_USER_DATA                          0x601EC

#define mmMME0_CTRL_ARCH_DESC_PERF_EVT_S                             0x601F0

#define mmMME0_CTRL_ARCH_DESC_PERF_EVT_L_LOCAL                       0x601F4

#define mmMME0_CTRL_ARCH_DESC_PERF_EVT_L_REMOTE                      0x601F8

#define mmMME0_CTRL_ARCH_DESC_PERF_EVT_O_LOCAL                       0x601FC

#define mmMME0_CTRL_ARCH_DESC_PERF_EVT_O_REMOTE                      0x60200

#define mmMME0_CTRL_ARCH_DESC_PADDING_VALUE_S                        0x60204

#define mmMME0_CTRL_ARCH_DESC_PADDING_VALUE_L                        0x60208

#define mmMME0_CTRL_ARCH_DESC_META_DATA_AGU_S                        0x6020C

#define mmMME0_CTRL_ARCH_DESC_META_DATA_AGU_L_LOCAL                  0x60210

#define mmMME0_CTRL_ARCH_DESC_META_DATA_AGU_L_REMOTE                 0x60214

#define mmMME0_CTRL_ARCH_DESC_META_DATA_AGU_O_LOCAL                  0x60218

#define mmMME0_CTRL_ARCH_DESC_META_DATA_AGU_O_REMOTE                 0x6021C

#define mmMME0_CTRL_ARCH_DESC_PCU_RL_SATURATION                      0x60220

#define mmMME0_CTRL_ARCH_DESC_DUMMY                                  0x60224

#define mmMME0_CTRL_CMD                                              0x60280

#define mmMME0_CTRL_STATUS1                                          0x60284

#define mmMME0_CTRL_RESET                                            0x60288

#define mmMME0_CTRL_QM_STALL                                         0x6028C

#define mmMME0_CTRL_SYNC_OBJECT_FIFO_TH                              0x60290

#define mmMME0_CTRL_EUS_ROLLUP_CNT_ADD                               0x60294

#define mmMME0_CTRL_INTR_CAUSE                                       0x60298

#define mmMME0_CTRL_INTR_MASK                                        0x6029C

#define mmMME0_CTRL_LOG_SHADOW                                       0x602A0

#define mmMME0_CTRL_PCU_RL_DESC0                                     0x602A4

#define mmMME0_CTRL_PCU_RL_TOKEN_UPDATE                              0x602A8

#define mmMME0_CTRL_PCU_RL_TH                                        0x602AC

#define mmMME0_CTRL_PCU_RL_MIN                                       0x602B0

#define mmMME0_CTRL_PCU_RL_CTRL_EN                                   0x602B4

#define mmMME0_CTRL_PCU_RL_HISTORY_LOG_SIZE                          0x602B8

#define mmMME0_CTRL_PCU_DUMMY_A_BF16                                 0x602BC

#define mmMME0_CTRL_PCU_DUMMY_B_BF16                                 0x602C0

#define mmMME0_CTRL_PCU_DUMMY_A_FP32_ODD                             0x602C4

#define mmMME0_CTRL_PCU_DUMMY_A_FP32_EVEN                            0x602C8

#define mmMME0_CTRL_PCU_DUMMY_B_FP32_ODD                             0x602CC

#define mmMME0_CTRL_PCU_DUMMY_B_FP32_EVEN                            0x602D0

#define mmMME0_CTRL_PROT                                             0x602D4

#define mmMME0_CTRL_EU_POWER_SAVE_DISABLE                            0x602D8

#define mmMME0_CTRL_CS_DBG_BLOCK_ID                                  0x602DC

#define mmMME0_CTRL_CS_DBG_STATUS_DROP_CNT                           0x602E0

#define mmMME0_CTRL_TE_CLOSE_CGATE                                   0x602E4

#define mmMME0_CTRL_AGU_SM_INFLIGHT_CNTR                             0x602E8

#define mmMME0_CTRL_AGU_SM_TOTAL_CNTR                                0x602EC

#define mmMME0_CTRL_EZSYNC_OUT_CREDIT                                0x602F0

#define mmMME0_CTRL_PCU_RL_SAT_SEC                                   0x602F4

#define mmMME0_CTRL_AGU_SYNC_MSG_AXI_USER                            0x602F8

#define mmMME0_CTRL_QM_SLV_LBW_CLK_EN                                0x602FC

#define mmMME0_CTRL_SHADOW_0_STATUS                                  0x60400

#define mmMME0_CTRL_SHADOW_0_BASE_ADDR_HIGH_S                        0x60408

#define mmMME0_CTRL_SHADOW_0_BASE_ADDR_HIGH_L                        0x6040C

#define mmMME0_CTRL_SHADOW_0_BASE_ADDR_HIGH_O                        0x60410

#define mmMME0_CTRL_SHADOW_0_BASE_ADDR_LOW_S                         0x60414

#define mmMME0_CTRL_SHADOW_0_BASE_ADDR_LOW_L                         0x60418

#define mmMME0_CTRL_SHADOW_0_BASE_ADDR_LOW_O                         0x6041C

#define mmMME0_CTRL_SHADOW_0_HEADER_LOW                              0x60420

#define mmMME0_CTRL_SHADOW_0_HEADER_HIGH                             0x60424

#define mmMME0_CTRL_SHADOW_0_CONV_KERNEL_SIZE_MINUS_1                0x60428

#define mmMME0_CTRL_SHADOW_0_CONV_ASSOCIATED_DIMS_LOW                0x6042C

#define mmMME0_CTRL_SHADOW_0_CONV_ASSOCIATED_DIMS_HIGH               0x60430

#define mmMME0_CTRL_SHADOW_0_NUM_ITERATIONS_MINUS_1                  0x60434

#define mmMME0_CTRL_SHADOW_0_OUTER_LOOP                              0x60438

#define mmMME0_CTRL_SHADOW_0_TENSOR_S_VALID_ELEMENTS_0               0x6043C

#define mmMME0_CTRL_SHADOW_0_TENSOR_S_VALID_ELEMENTS_1               0x60440

#define mmMME0_CTRL_SHADOW_0_TENSOR_S_VALID_ELEMENTS_2               0x60444

#define mmMME0_CTRL_SHADOW_0_TENSOR_S_VALID_ELEMENTS_3               0x60448

#define mmMME0_CTRL_SHADOW_0_TENSOR_S_VALID_ELEMENTS_4               0x6044C

#define mmMME0_CTRL_SHADOW_0_TENSOR_S_LOOP_STRIDE_0                  0x60450

#define mmMME0_CTRL_SHADOW_0_TENSOR_S_LOOP_STRIDE_1                  0x60454

#define mmMME0_CTRL_SHADOW_0_TENSOR_S_LOOP_STRIDE_2                  0x60458

#define mmMME0_CTRL_SHADOW_0_TENSOR_S_LOOP_STRIDE_3                  0x6045C

#define mmMME0_CTRL_SHADOW_0_TENSOR_S_LOOP_STRIDE_4                  0x60460

#define mmMME0_CTRL_SHADOW_0_TENSOR_S_ROI_SIZE_0                     0x60464

#define mmMME0_CTRL_SHADOW_0_TENSOR_S_ROI_SIZE_1                     0x60468

#define mmMME0_CTRL_SHADOW_0_TENSOR_S_ROI_SIZE_2                     0x6046C

#define mmMME0_CTRL_SHADOW_0_TENSOR_S_ROI_SIZE_3                     0x60470

#define mmMME0_CTRL_SHADOW_0_TENSOR_S_SPATIAL_STRIDES_0              0x60474

#define mmMME0_CTRL_SHADOW_0_TENSOR_S_SPATIAL_STRIDES_1              0x60478

#define mmMME0_CTRL_SHADOW_0_TENSOR_S_SPATIAL_STRIDES_2              0x6047C

#define mmMME0_CTRL_SHADOW_0_TENSOR_S_SPATIAL_STRIDES_3              0x60480

#define mmMME0_CTRL_SHADOW_0_TENSOR_S_SPATIAL_SIZE_MINUS_1           0x60484

#define mmMME0_CTRL_SHADOW_0_AGU_S_ROI_BASE_OFFSET_0                 0x60488

#define mmMME0_CTRL_SHADOW_0_AGU_S_ROI_BASE_OFFSET_1                 0x6048C

#define mmMME0_CTRL_SHADOW_0_AGU_S_ROI_BASE_OFFSET_2                 0x60490

#define mmMME0_CTRL_SHADOW_0_AGU_S_ROI_BASE_OFFSET_3                 0x60494

#define mmMME0_CTRL_SHADOW_0_AGU_S_ROI_BASE_OFFSET_4                 0x60498

#define mmMME0_CTRL_SHADOW_0_AGU_S_START_OFFSET_0                    0x6049C

#define mmMME0_CTRL_SHADOW_0_AGU_S_START_OFFSET_1                    0x604A0

#define mmMME0_CTRL_SHADOW_0_AGU_S_START_OFFSET_2                    0x604A4

#define mmMME0_CTRL_SHADOW_0_AGU_S_START_OFFSET_3                    0x604A8

#define mmMME0_CTRL_SHADOW_0_TENSOR_L_VALID_ELEMENTS_0               0x604AC

#define mmMME0_CTRL_SHADOW_0_TENSOR_L_VALID_ELEMENTS_1               0x604B0

#define mmMME0_CTRL_SHADOW_0_TENSOR_L_VALID_ELEMENTS_2               0x604B4

#define mmMME0_CTRL_SHADOW_0_TENSOR_L_VALID_ELEMENTS_3               0x604B8

#define mmMME0_CTRL_SHADOW_0_TENSOR_L_VALID_ELEMENTS_4               0x604BC

#define mmMME0_CTRL_SHADOW_0_TENSOR_L_LOOP_STRIDE_0                  0x604C0

#define mmMME0_CTRL_SHADOW_0_TENSOR_L_LOOP_STRIDE_1                  0x604C4

#define mmMME0_CTRL_SHADOW_0_TENSOR_L_LOOP_STRIDE_2                  0x604C8

#define mmMME0_CTRL_SHADOW_0_TENSOR_L_LOOP_STRIDE_3                  0x604CC

#define mmMME0_CTRL_SHADOW_0_TENSOR_L_LOOP_STRIDE_4                  0x604D0

#define mmMME0_CTRL_SHADOW_0_TENSOR_L_ROI_SIZE_0                     0x604D4

#define mmMME0_CTRL_SHADOW_0_TENSOR_L_ROI_SIZE_1                     0x604D8

#define mmMME0_CTRL_SHADOW_0_TENSOR_L_ROI_SIZE_2                     0x604DC

#define mmMME0_CTRL_SHADOW_0_TENSOR_L_ROI_SIZE_3                     0x604E0

#define mmMME0_CTRL_SHADOW_0_TENSOR_L_SPATIAL_STRIDES_0              0x604E4

#define mmMME0_CTRL_SHADOW_0_TENSOR_L_SPATIAL_STRIDES_1              0x604E8

#define mmMME0_CTRL_SHADOW_0_TENSOR_L_SPATIAL_STRIDES_2              0x604EC

#define mmMME0_CTRL_SHADOW_0_TENSOR_L_SPATIAL_STRIDES_3              0x604F0

#define mmMME0_CTRL_SHADOW_0_TENSOR_L_SPATIAL_SIZE_MINUS_1           0x604F4

#define mmMME0_CTRL_SHADOW_0_AGU_L_LOCAL_ROI_BASE_OFFSET_0           0x604F8

#define mmMME0_CTRL_SHADOW_0_AGU_L_LOCAL_ROI_BASE_OFFSET_1           0x604FC

#define mmMME0_CTRL_SHADOW_0_AGU_L_LOCAL_ROI_BASE_OFFSET_2           0x60500

#define mmMME0_CTRL_SHADOW_0_AGU_L_LOCAL_ROI_BASE_OFFSET_3           0x60504

#define mmMME0_CTRL_SHADOW_0_AGU_L_LOCAL_ROI_BASE_OFFSET_4           0x60508

#define mmMME0_CTRL_SHADOW_0_AGU_L_LOCAL_START_OFFSET_0              0x6050C

#define mmMME0_CTRL_SHADOW_0_AGU_L_LOCAL_START_OFFSET_1              0x60510

#define mmMME0_CTRL_SHADOW_0_AGU_L_LOCAL_START_OFFSET_2              0x60514

#define mmMME0_CTRL_SHADOW_0_AGU_L_LOCAL_START_OFFSET_3              0x60518

#define mmMME0_CTRL_SHADOW_0_AGU_L_REMOTE_ROI_BASE_OFFSET_0          0x6051C

#define mmMME0_CTRL_SHADOW_0_AGU_L_REMOTE_ROI_BASE_OFFSET_1          0x60520

#define mmMME0_CTRL_SHADOW_0_AGU_L_REMOTE_ROI_BASE_OFFSET_2          0x60524

#define mmMME0_CTRL_SHADOW_0_AGU_L_REMOTE_ROI_BASE_OFFSET_3          0x60528

#define mmMME0_CTRL_SHADOW_0_AGU_L_REMOTE_ROI_BASE_OFFSET_4          0x6052C

#define mmMME0_CTRL_SHADOW_0_AGU_L_REMOTE_START_OFFSET_0             0x60530

#define mmMME0_CTRL_SHADOW_0_AGU_L_REMOTE_START_OFFSET_1             0x60534

#define mmMME0_CTRL_SHADOW_0_AGU_L_REMOTE_START_OFFSET_2             0x60538

#define mmMME0_CTRL_SHADOW_0_AGU_L_REMOTE_START_OFFSET_3             0x6053C

#define mmMME0_CTRL_SHADOW_0_TENSOR_O_VALID_ELEMENTS_0               0x60540

#define mmMME0_CTRL_SHADOW_0_TENSOR_O_VALID_ELEMENTS_1               0x60544

#define mmMME0_CTRL_SHADOW_0_TENSOR_O_VALID_ELEMENTS_2               0x60548

#define mmMME0_CTRL_SHADOW_0_TENSOR_O_VALID_ELEMENTS_3               0x6054C

#define mmMME0_CTRL_SHADOW_0_TENSOR_O_VALID_ELEMENTS_4               0x60550

#define mmMME0_CTRL_SHADOW_0_TENSOR_O_LOOP_STRIDE_0                  0x60554

#define mmMME0_CTRL_SHADOW_0_TENSOR_O_LOOP_STRIDE_1                  0x60558

#define mmMME0_CTRL_SHADOW_0_TENSOR_O_LOOP_STRIDE_2                  0x6055C

#define mmMME0_CTRL_SHADOW_0_TENSOR_O_LOOP_STRIDE_3                  0x60560

#define mmMME0_CTRL_SHADOW_0_TENSOR_O_LOOP_STRIDE_4                  0x60564

#define mmMME0_CTRL_SHADOW_0_TENSOR_O_ROI_SIZE_0                     0x60568

#define mmMME0_CTRL_SHADOW_0_TENSOR_O_ROI_SIZE_1                     0x6056C

#define mmMME0_CTRL_SHADOW_0_TENSOR_O_ROI_SIZE_2                     0x60570

#define mmMME0_CTRL_SHADOW_0_TENSOR_O_ROI_SIZE_3                     0x60574

#define mmMME0_CTRL_SHADOW_0_TENSOR_O_SPATIAL_STRIDES_0              0x60578

#define mmMME0_CTRL_SHADOW_0_TENSOR_O_SPATIAL_STRIDES_1              0x6057C

#define mmMME0_CTRL_SHADOW_0_TENSOR_O_SPATIAL_STRIDES_2              0x60580

#define mmMME0_CTRL_SHADOW_0_TENSOR_O_SPATIAL_STRIDES_3              0x60584

#define mmMME0_CTRL_SHADOW_0_TENSOR_O_SPATIAL_SIZE_MINUS_1           0x60588

#define mmMME0_CTRL_SHADOW_0_AGU_O_LOCAL_ROI_BASE_OFFSET_0           0x6058C

#define mmMME0_CTRL_SHADOW_0_AGU_O_LOCAL_ROI_BASE_OFFSET_1           0x60590

#define mmMME0_CTRL_SHADOW_0_AGU_O_LOCAL_ROI_BASE_OFFSET_2           0x60594

#define mmMME0_CTRL_SHADOW_0_AGU_O_LOCAL_ROI_BASE_OFFSET_3           0x60598

#define mmMME0_CTRL_SHADOW_0_AGU_O_LOCAL_ROI_BASE_OFFSET_4           0x6059C

#define mmMME0_CTRL_SHADOW_0_AGU_O_LOCAL_START_OFFSET_0              0x605A0

#define mmMME0_CTRL_SHADOW_0_AGU_O_LOCAL_START_OFFSET_1              0x605A4

#define mmMME0_CTRL_SHADOW_0_AGU_O_LOCAL_START_OFFSET_2              0x605A8

#define mmMME0_CTRL_SHADOW_0_AGU_O_LOCAL_START_OFFSET_3              0x605AC

#define mmMME0_CTRL_SHADOW_0_AGU_O_REMOTE_ROI_BASE_OFFSET_0          0x605B0

#define mmMME0_CTRL_SHADOW_0_AGU_O_REMOTE_ROI_BASE_OFFSET_1          0x605B4

#define mmMME0_CTRL_SHADOW_0_AGU_O_REMOTE_ROI_BASE_OFFSET_2          0x605B8

#define mmMME0_CTRL_SHADOW_0_AGU_O_REMOTE_ROI_BASE_OFFSET_3          0x605BC

#define mmMME0_CTRL_SHADOW_0_AGU_O_REMOTE_ROI_BASE_OFFSET_4          0x605C0

#define mmMME0_CTRL_SHADOW_0_AGU_O_REMOTE_START_OFFSET_0             0x605C4

#define mmMME0_CTRL_SHADOW_0_AGU_O_REMOTE_START_OFFSET_1             0x605C8

#define mmMME0_CTRL_SHADOW_0_AGU_O_REMOTE_START_OFFSET_2             0x605CC

#define mmMME0_CTRL_SHADOW_0_AGU_O_REMOTE_START_OFFSET_3             0x605D0

#define mmMME0_CTRL_SHADOW_0_DESC_SB_REPEAT                          0x605D4

#define mmMME0_CTRL_SHADOW_0_DESC_RATE_LIMITER                       0x605D8

#define mmMME0_CTRL_SHADOW_0_DESC_SYNC_OBJECT_ADDR_LOW_LOCAL         0x605DC

#define mmMME0_CTRL_SHADOW_0_DESC_SYNC_OBJECT_ADDR_LOW_REMOTE        0x605E0

#define mmMME0_CTRL_SHADOW_0_DESC_SYNC_OBJECT_ADDR_HIGH              0x605E4

#define mmMME0_CTRL_SHADOW_0_DESC_SYNC_OBJECT_DATA                   0x605E8

#define mmMME0_CTRL_SHADOW_0_DESC_AXI_USER_DATA                      0x605EC

#define mmMME0_CTRL_SHADOW_0_DESC_PERF_EVT_S                         0x605F0

#define mmMME0_CTRL_SHADOW_0_DESC_PERF_EVT_L_LOCAL                   0x605F4

#define mmMME0_CTRL_SHADOW_0_DESC_PERF_EVT_L_REMOTE                  0x605F8

#define mmMME0_CTRL_SHADOW_0_DESC_PERF_EVT_O_LOCAL                   0x605FC

#define mmMME0_CTRL_SHADOW_0_DESC_PERF_EVT_O_REMOTE                  0x60600

#define mmMME0_CTRL_SHADOW_0_DESC_PADDING_VALUE_S                    0x60604

#define mmMME0_CTRL_SHADOW_0_DESC_PADDING_VALUE_L                    0x60608

#define mmMME0_CTRL_SHADOW_0_DESC_META_DATA_AGU_S                    0x6060C

#define mmMME0_CTRL_SHADOW_0_DESC_META_DATA_AGU_L_LOCAL              0x60610

#define mmMME0_CTRL_SHADOW_0_DESC_META_DATA_AGU_L_REMOTE             0x60614

#define mmMME0_CTRL_SHADOW_0_DESC_META_DATA_AGU_O_LOCAL              0x60618

#define mmMME0_CTRL_SHADOW_0_DESC_META_DATA_AGU_O_REMOTE             0x6061C

#define mmMME0_CTRL_SHADOW_0_DESC_PCU_RL_SATURATION                  0x60620

#define mmMME0_CTRL_SHADOW_0_DESC_DUMMY                              0x60624

#define mmMME0_CTRL_SHADOW_1_STATUS                                  0x60680

#define mmMME0_CTRL_SHADOW_1_BASE_ADDR_HIGH_S                        0x60688

#define mmMME0_CTRL_SHADOW_1_BASE_ADDR_HIGH_L                        0x6068C

#define mmMME0_CTRL_SHADOW_1_BASE_ADDR_HIGH_O                        0x60690

#define mmMME0_CTRL_SHADOW_1_BASE_ADDR_LOW_S                         0x60694

#define mmMME0_CTRL_SHADOW_1_BASE_ADDR_LOW_L                         0x60698

#define mmMME0_CTRL_SHADOW_1_BASE_ADDR_LOW_O                         0x6069C

#define mmMME0_CTRL_SHADOW_1_HEADER_LOW                              0x606A0

#define mmMME0_CTRL_SHADOW_1_HEADER_HIGH                             0x606A4

#define mmMME0_CTRL_SHADOW_1_CONV_KERNEL_SIZE_MINUS_1                0x606A8

#define mmMME0_CTRL_SHADOW_1_CONV_ASSOCIATED_DIMS_LOW                0x606AC

#define mmMME0_CTRL_SHADOW_1_CONV_ASSOCIATED_DIMS_HIGH               0x606B0

#define mmMME0_CTRL_SHADOW_1_NUM_ITERATIONS_MINUS_1                  0x606B4

#define mmMME0_CTRL_SHADOW_1_OUTER_LOOP                              0x606B8

#define mmMME0_CTRL_SHADOW_1_TENSOR_S_VALID_ELEMENTS_0               0x606BC

#define mmMME0_CTRL_SHADOW_1_TENSOR_S_VALID_ELEMENTS_1               0x606C0

#define mmMME0_CTRL_SHADOW_1_TENSOR_S_VALID_ELEMENTS_2               0x606C4

#define mmMME0_CTRL_SHADOW_1_TENSOR_S_VALID_ELEMENTS_3               0x606C8

#define mmMME0_CTRL_SHADOW_1_TENSOR_S_VALID_ELEMENTS_4               0x606CC

#define mmMME0_CTRL_SHADOW_1_TENSOR_S_LOOP_STRIDE_0                  0x606D0

#define mmMME0_CTRL_SHADOW_1_TENSOR_S_LOOP_STRIDE_1                  0x606D4

#define mmMME0_CTRL_SHADOW_1_TENSOR_S_LOOP_STRIDE_2                  0x606D8

#define mmMME0_CTRL_SHADOW_1_TENSOR_S_LOOP_STRIDE_3                  0x606DC

#define mmMME0_CTRL_SHADOW_1_TENSOR_S_LOOP_STRIDE_4                  0x606E0

#define mmMME0_CTRL_SHADOW_1_TENSOR_S_ROI_SIZE_0                     0x606E4

#define mmMME0_CTRL_SHADOW_1_TENSOR_S_ROI_SIZE_1                     0x606E8

#define mmMME0_CTRL_SHADOW_1_TENSOR_S_ROI_SIZE_2                     0x606EC

#define mmMME0_CTRL_SHADOW_1_TENSOR_S_ROI_SIZE_3                     0x606F0

#define mmMME0_CTRL_SHADOW_1_TENSOR_S_SPATIAL_STRIDES_0              0x606F4

#define mmMME0_CTRL_SHADOW_1_TENSOR_S_SPATIAL_STRIDES_1              0x606F8

#define mmMME0_CTRL_SHADOW_1_TENSOR_S_SPATIAL_STRIDES_2              0x606FC

#define mmMME0_CTRL_SHADOW_1_TENSOR_S_SPATIAL_STRIDES_3              0x60700

#define mmMME0_CTRL_SHADOW_1_TENSOR_S_SPATIAL_SIZE_MINUS_1           0x60704

#define mmMME0_CTRL_SHADOW_1_AGU_S_ROI_BASE_OFFSET_0                 0x60708

#define mmMME0_CTRL_SHADOW_1_AGU_S_ROI_BASE_OFFSET_1                 0x6070C

#define mmMME0_CTRL_SHADOW_1_AGU_S_ROI_BASE_OFFSET_2                 0x60710

#define mmMME0_CTRL_SHADOW_1_AGU_S_ROI_BASE_OFFSET_3                 0x60714

#define mmMME0_CTRL_SHADOW_1_AGU_S_ROI_BASE_OFFSET_4                 0x60718

#define mmMME0_CTRL_SHADOW_1_AGU_S_START_OFFSET_0                    0x6071C

#define mmMME0_CTRL_SHADOW_1_AGU_S_START_OFFSET_1                    0x60720

#define mmMME0_CTRL_SHADOW_1_AGU_S_START_OFFSET_2                    0x60724

#define mmMME0_CTRL_SHADOW_1_AGU_S_START_OFFSET_3                    0x60728

#define mmMME0_CTRL_SHADOW_1_TENSOR_L_VALID_ELEMENTS_0               0x6072C

#define mmMME0_CTRL_SHADOW_1_TENSOR_L_VALID_ELEMENTS_1               0x60730

#define mmMME0_CTRL_SHADOW_1_TENSOR_L_VALID_ELEMENTS_2               0x60734

#define mmMME0_CTRL_SHADOW_1_TENSOR_L_VALID_ELEMENTS_3               0x60738

#define mmMME0_CTRL_SHADOW_1_TENSOR_L_VALID_ELEMENTS_4               0x6073C

#define mmMME0_CTRL_SHADOW_1_TENSOR_L_LOOP_STRIDE_0                  0x60740

#define mmMME0_CTRL_SHADOW_1_TENSOR_L_LOOP_STRIDE_1                  0x60744

#define mmMME0_CTRL_SHADOW_1_TENSOR_L_LOOP_STRIDE_2                  0x60748

#define mmMME0_CTRL_SHADOW_1_TENSOR_L_LOOP_STRIDE_3                  0x6074C

#define mmMME0_CTRL_SHADOW_1_TENSOR_L_LOOP_STRIDE_4                  0x60750

#define mmMME0_CTRL_SHADOW_1_TENSOR_L_ROI_SIZE_0                     0x60754

#define mmMME0_CTRL_SHADOW_1_TENSOR_L_ROI_SIZE_1                     0x60758

#define mmMME0_CTRL_SHADOW_1_TENSOR_L_ROI_SIZE_2                     0x6075C

#define mmMME0_CTRL_SHADOW_1_TENSOR_L_ROI_SIZE_3                     0x60760

#define mmMME0_CTRL_SHADOW_1_TENSOR_L_SPATIAL_STRIDES_0              0x60764

#define mmMME0_CTRL_SHADOW_1_TENSOR_L_SPATIAL_STRIDES_1              0x60768

#define mmMME0_CTRL_SHADOW_1_TENSOR_L_SPATIAL_STRIDES_2              0x6076C

#define mmMME0_CTRL_SHADOW_1_TENSOR_L_SPATIAL_STRIDES_3              0x60770

#define mmMME0_CTRL_SHADOW_1_TENSOR_L_SPATIAL_SIZE_MINUS_1           0x60774

#define mmMME0_CTRL_SHADOW_1_AGU_L_LOCAL_ROI_BASE_OFFSET_0           0x60778

#define mmMME0_CTRL_SHADOW_1_AGU_L_LOCAL_ROI_BASE_OFFSET_1           0x6077C

#define mmMME0_CTRL_SHADOW_1_AGU_L_LOCAL_ROI_BASE_OFFSET_2           0x60780

#define mmMME0_CTRL_SHADOW_1_AGU_L_LOCAL_ROI_BASE_OFFSET_3           0x60784

#define mmMME0_CTRL_SHADOW_1_AGU_L_LOCAL_ROI_BASE_OFFSET_4           0x60788

#define mmMME0_CTRL_SHADOW_1_AGU_L_LOCAL_START_OFFSET_0              0x6078C

#define mmMME0_CTRL_SHADOW_1_AGU_L_LOCAL_START_OFFSET_1              0x60790

#define mmMME0_CTRL_SHADOW_1_AGU_L_LOCAL_START_OFFSET_2              0x60794

#define mmMME0_CTRL_SHADOW_1_AGU_L_LOCAL_START_OFFSET_3              0x60798

#define mmMME0_CTRL_SHADOW_1_AGU_L_REMOTE_ROI_BASE_OFFSET_0          0x6079C

#define mmMME0_CTRL_SHADOW_1_AGU_L_REMOTE_ROI_BASE_OFFSET_1          0x607A0

#define mmMME0_CTRL_SHADOW_1_AGU_L_REMOTE_ROI_BASE_OFFSET_2          0x607A4

#define mmMME0_CTRL_SHADOW_1_AGU_L_REMOTE_ROI_BASE_OFFSET_3          0x607A8

#define mmMME0_CTRL_SHADOW_1_AGU_L_REMOTE_ROI_BASE_OFFSET_4          0x607AC

#define mmMME0_CTRL_SHADOW_1_AGU_L_REMOTE_START_OFFSET_0             0x607B0

#define mmMME0_CTRL_SHADOW_1_AGU_L_REMOTE_START_OFFSET_1             0x607B4

#define mmMME0_CTRL_SHADOW_1_AGU_L_REMOTE_START_OFFSET_2             0x607B8

#define mmMME0_CTRL_SHADOW_1_AGU_L_REMOTE_START_OFFSET_3             0x607BC

#define mmMME0_CTRL_SHADOW_1_TENSOR_O_VALID_ELEMENTS_0               0x607C0

#define mmMME0_CTRL_SHADOW_1_TENSOR_O_VALID_ELEMENTS_1               0x607C4

#define mmMME0_CTRL_SHADOW_1_TENSOR_O_VALID_ELEMENTS_2               0x607C8

#define mmMME0_CTRL_SHADOW_1_TENSOR_O_VALID_ELEMENTS_3               0x607CC

#define mmMME0_CTRL_SHADOW_1_TENSOR_O_VALID_ELEMENTS_4               0x607D0

#define mmMME0_CTRL_SHADOW_1_TENSOR_O_LOOP_STRIDE_0                  0x607D4

#define mmMME0_CTRL_SHADOW_1_TENSOR_O_LOOP_STRIDE_1                  0x607D8

#define mmMME0_CTRL_SHADOW_1_TENSOR_O_LOOP_STRIDE_2                  0x607DC

#define mmMME0_CTRL_SHADOW_1_TENSOR_O_LOOP_STRIDE_3                  0x607E0

#define mmMME0_CTRL_SHADOW_1_TENSOR_O_LOOP_STRIDE_4                  0x607E4

#define mmMME0_CTRL_SHADOW_1_TENSOR_O_ROI_SIZE_0                     0x607E8

#define mmMME0_CTRL_SHADOW_1_TENSOR_O_ROI_SIZE_1                     0x607EC

#define mmMME0_CTRL_SHADOW_1_TENSOR_O_ROI_SIZE_2                     0x607F0

#define mmMME0_CTRL_SHADOW_1_TENSOR_O_ROI_SIZE_3                     0x607F4

#define mmMME0_CTRL_SHADOW_1_TENSOR_O_SPATIAL_STRIDES_0              0x607F8

#define mmMME0_CTRL_SHADOW_1_TENSOR_O_SPATIAL_STRIDES_1              0x607FC

#define mmMME0_CTRL_SHADOW_1_TENSOR_O_SPATIAL_STRIDES_2              0x60800

#define mmMME0_CTRL_SHADOW_1_TENSOR_O_SPATIAL_STRIDES_3              0x60804

#define mmMME0_CTRL_SHADOW_1_TENSOR_O_SPATIAL_SIZE_MINUS_1           0x60808

#define mmMME0_CTRL_SHADOW_1_AGU_O_LOCAL_ROI_BASE_OFFSET_0           0x6080C

#define mmMME0_CTRL_SHADOW_1_AGU_O_LOCAL_ROI_BASE_OFFSET_1           0x60810

#define mmMME0_CTRL_SHADOW_1_AGU_O_LOCAL_ROI_BASE_OFFSET_2           0x60814

#define mmMME0_CTRL_SHADOW_1_AGU_O_LOCAL_ROI_BASE_OFFSET_3           0x60818

#define mmMME0_CTRL_SHADOW_1_AGU_O_LOCAL_ROI_BASE_OFFSET_4           0x6081C

#define mmMME0_CTRL_SHADOW_1_AGU_O_LOCAL_START_OFFSET_0              0x60820

#define mmMME0_CTRL_SHADOW_1_AGU_O_LOCAL_START_OFFSET_1              0x60824

#define mmMME0_CTRL_SHADOW_1_AGU_O_LOCAL_START_OFFSET_2              0x60828

#define mmMME0_CTRL_SHADOW_1_AGU_O_LOCAL_START_OFFSET_3              0x6082C

#define mmMME0_CTRL_SHADOW_1_AGU_O_REMOTE_ROI_BASE_OFFSET_0          0x60830

#define mmMME0_CTRL_SHADOW_1_AGU_O_REMOTE_ROI_BASE_OFFSET_1          0x60834

#define mmMME0_CTRL_SHADOW_1_AGU_O_REMOTE_ROI_BASE_OFFSET_2          0x60838

#define mmMME0_CTRL_SHADOW_1_AGU_O_REMOTE_ROI_BASE_OFFSET_3          0x6083C

#define mmMME0_CTRL_SHADOW_1_AGU_O_REMOTE_ROI_BASE_OFFSET_4          0x60840

#define mmMME0_CTRL_SHADOW_1_AGU_O_REMOTE_START_OFFSET_0             0x60844

#define mmMME0_CTRL_SHADOW_1_AGU_O_REMOTE_START_OFFSET_1             0x60848

#define mmMME0_CTRL_SHADOW_1_AGU_O_REMOTE_START_OFFSET_2             0x6084C

#define mmMME0_CTRL_SHADOW_1_AGU_O_REMOTE_START_OFFSET_3             0x60850

#define mmMME0_CTRL_SHADOW_1_DESC_SB_REPEAT                          0x60854

#define mmMME0_CTRL_SHADOW_1_DESC_RATE_LIMITER                       0x60858

#define mmMME0_CTRL_SHADOW_1_DESC_SYNC_OBJECT_ADDR_LOW_LOCAL         0x6085C

#define mmMME0_CTRL_SHADOW_1_DESC_SYNC_OBJECT_ADDR_LOW_REMOTE        0x60860

#define mmMME0_CTRL_SHADOW_1_DESC_SYNC_OBJECT_ADDR_HIGH              0x60864

#define mmMME0_CTRL_SHADOW_1_DESC_SYNC_OBJECT_DATA                   0x60868

#define mmMME0_CTRL_SHADOW_1_DESC_AXI_USER_DATA                      0x6086C

#define mmMME0_CTRL_SHADOW_1_DESC_PERF_EVT_S                         0x60870

#define mmMME0_CTRL_SHADOW_1_DESC_PERF_EVT_L_LOCAL                   0x60874

#define mmMME0_CTRL_SHADOW_1_DESC_PERF_EVT_L_REMOTE                  0x60878

#define mmMME0_CTRL_SHADOW_1_DESC_PERF_EVT_O_LOCAL                   0x6087C

#define mmMME0_CTRL_SHADOW_1_DESC_PERF_EVT_O_REMOTE                  0x60880

#define mmMME0_CTRL_SHADOW_1_DESC_PADDING_VALUE_S                    0x60884

#define mmMME0_CTRL_SHADOW_1_DESC_PADDING_VALUE_L                    0x60888

#define mmMME0_CTRL_SHADOW_1_DESC_META_DATA_AGU_S                    0x6088C

#define mmMME0_CTRL_SHADOW_1_DESC_META_DATA_AGU_L_LOCAL              0x60890

#define mmMME0_CTRL_SHADOW_1_DESC_META_DATA_AGU_L_REMOTE             0x60894

#define mmMME0_CTRL_SHADOW_1_DESC_META_DATA_AGU_O_LOCAL              0x60898

#define mmMME0_CTRL_SHADOW_1_DESC_META_DATA_AGU_O_REMOTE             0x6089C

#define mmMME0_CTRL_SHADOW_1_DESC_PCU_RL_SATURATION                  0x608A0

#define mmMME0_CTRL_SHADOW_1_DESC_DUMMY                              0x608A4

#define mmMME0_CTRL_SHADOW_2_STATUS                                  0x60900

#define mmMME0_CTRL_SHADOW_2_BASE_ADDR_HIGH_S                        0x60908

#define mmMME0_CTRL_SHADOW_2_BASE_ADDR_HIGH_L                        0x6090C

#define mmMME0_CTRL_SHADOW_2_BASE_ADDR_HIGH_O                        0x60910

#define mmMME0_CTRL_SHADOW_2_BASE_ADDR_LOW_S                         0x60914

#define mmMME0_CTRL_SHADOW_2_BASE_ADDR_LOW_L                         0x60918

#define mmMME0_CTRL_SHADOW_2_BASE_ADDR_LOW_O                         0x6091C

#define mmMME0_CTRL_SHADOW_2_HEADER_LOW                              0x60920

#define mmMME0_CTRL_SHADOW_2_HEADER_HIGH                             0x60924

#define mmMME0_CTRL_SHADOW_2_CONV_KERNEL_SIZE_MINUS_1                0x60928

#define mmMME0_CTRL_SHADOW_2_CONV_ASSOCIATED_DIMS_LOW                0x6092C

#define mmMME0_CTRL_SHADOW_2_CONV_ASSOCIATED_DIMS_HIGH               0x60930

#define mmMME0_CTRL_SHADOW_2_NUM_ITERATIONS_MINUS_1                  0x60934

#define mmMME0_CTRL_SHADOW_2_OUTER_LOOP                              0x60938

#define mmMME0_CTRL_SHADOW_2_TENSOR_S_VALID_ELEMENTS_0               0x6093C

#define mmMME0_CTRL_SHADOW_2_TENSOR_S_VALID_ELEMENTS_1               0x60940

#define mmMME0_CTRL_SHADOW_2_TENSOR_S_VALID_ELEMENTS_2               0x60944

#define mmMME0_CTRL_SHADOW_2_TENSOR_S_VALID_ELEMENTS_3               0x60948

#define mmMME0_CTRL_SHADOW_2_TENSOR_S_VALID_ELEMENTS_4               0x6094C

#define mmMME0_CTRL_SHADOW_2_TENSOR_S_LOOP_STRIDE_0                  0x60950

#define mmMME0_CTRL_SHADOW_2_TENSOR_S_LOOP_STRIDE_1                  0x60954

#define mmMME0_CTRL_SHADOW_2_TENSOR_S_LOOP_STRIDE_2                  0x60958

#define mmMME0_CTRL_SHADOW_2_TENSOR_S_LOOP_STRIDE_3                  0x6095C

#define mmMME0_CTRL_SHADOW_2_TENSOR_S_LOOP_STRIDE_4                  0x60960

#define mmMME0_CTRL_SHADOW_2_TENSOR_S_ROI_SIZE_0                     0x60964

#define mmMME0_CTRL_SHADOW_2_TENSOR_S_ROI_SIZE_1                     0x60968

#define mmMME0_CTRL_SHADOW_2_TENSOR_S_ROI_SIZE_2                     0x6096C

#define mmMME0_CTRL_SHADOW_2_TENSOR_S_ROI_SIZE_3                     0x60970

#define mmMME0_CTRL_SHADOW_2_TENSOR_S_SPATIAL_STRIDES_0              0x60974

#define mmMME0_CTRL_SHADOW_2_TENSOR_S_SPATIAL_STRIDES_1              0x60978

#define mmMME0_CTRL_SHADOW_2_TENSOR_S_SPATIAL_STRIDES_2              0x6097C

#define mmMME0_CTRL_SHADOW_2_TENSOR_S_SPATIAL_STRIDES_3              0x60980

#define mmMME0_CTRL_SHADOW_2_TENSOR_S_SPATIAL_SIZE_MINUS_1           0x60984

#define mmMME0_CTRL_SHADOW_2_AGU_S_ROI_BASE_OFFSET_0                 0x60988

#define mmMME0_CTRL_SHADOW_2_AGU_S_ROI_BASE_OFFSET_1                 0x6098C

#define mmMME0_CTRL_SHADOW_2_AGU_S_ROI_BASE_OFFSET_2                 0x60990

#define mmMME0_CTRL_SHADOW_2_AGU_S_ROI_BASE_OFFSET_3                 0x60994

#define mmMME0_CTRL_SHADOW_2_AGU_S_ROI_BASE_OFFSET_4                 0x60998

#define mmMME0_CTRL_SHADOW_2_AGU_S_START_OFFSET_0                    0x6099C

#define mmMME0_CTRL_SHADOW_2_AGU_S_START_OFFSET_1                    0x609A0

#define mmMME0_CTRL_SHADOW_2_AGU_S_START_OFFSET_2                    0x609A4

#define mmMME0_CTRL_SHADOW_2_AGU_S_START_OFFSET_3                    0x609A8

#define mmMME0_CTRL_SHADOW_2_TENSOR_L_VALID_ELEMENTS_0               0x609AC

#define mmMME0_CTRL_SHADOW_2_TENSOR_L_VALID_ELEMENTS_1               0x609B0

#define mmMME0_CTRL_SHADOW_2_TENSOR_L_VALID_ELEMENTS_2               0x609B4

#define mmMME0_CTRL_SHADOW_2_TENSOR_L_VALID_ELEMENTS_3               0x609B8

#define mmMME0_CTRL_SHADOW_2_TENSOR_L_VALID_ELEMENTS_4               0x609BC

#define mmMME0_CTRL_SHADOW_2_TENSOR_L_LOOP_STRIDE_0                  0x609C0

#define mmMME0_CTRL_SHADOW_2_TENSOR_L_LOOP_STRIDE_1                  0x609C4

#define mmMME0_CTRL_SHADOW_2_TENSOR_L_LOOP_STRIDE_2                  0x609C8

#define mmMME0_CTRL_SHADOW_2_TENSOR_L_LOOP_STRIDE_3                  0x609CC

#define mmMME0_CTRL_SHADOW_2_TENSOR_L_LOOP_STRIDE_4                  0x609D0

#define mmMME0_CTRL_SHADOW_2_TENSOR_L_ROI_SIZE_0                     0x609D4

#define mmMME0_CTRL_SHADOW_2_TENSOR_L_ROI_SIZE_1                     0x609D8

#define mmMME0_CTRL_SHADOW_2_TENSOR_L_ROI_SIZE_2                     0x609DC

#define mmMME0_CTRL_SHADOW_2_TENSOR_L_ROI_SIZE_3                     0x609E0

#define mmMME0_CTRL_SHADOW_2_TENSOR_L_SPATIAL_STRIDES_0              0x609E4

#define mmMME0_CTRL_SHADOW_2_TENSOR_L_SPATIAL_STRIDES_1              0x609E8

#define mmMME0_CTRL_SHADOW_2_TENSOR_L_SPATIAL_STRIDES_2              0x609EC

#define mmMME0_CTRL_SHADOW_2_TENSOR_L_SPATIAL_STRIDES_3              0x609F0

#define mmMME0_CTRL_SHADOW_2_TENSOR_L_SPATIAL_SIZE_MINUS_1           0x609F4

#define mmMME0_CTRL_SHADOW_2_AGU_L_LOCAL_ROI_BASE_OFFSET_0           0x609F8

#define mmMME0_CTRL_SHADOW_2_AGU_L_LOCAL_ROI_BASE_OFFSET_1           0x609FC

#define mmMME0_CTRL_SHADOW_2_AGU_L_LOCAL_ROI_BASE_OFFSET_2           0x60A00

#define mmMME0_CTRL_SHADOW_2_AGU_L_LOCAL_ROI_BASE_OFFSET_3           0x60A04

#define mmMME0_CTRL_SHADOW_2_AGU_L_LOCAL_ROI_BASE_OFFSET_4           0x60A08

#define mmMME0_CTRL_SHADOW_2_AGU_L_LOCAL_START_OFFSET_0              0x60A0C

#define mmMME0_CTRL_SHADOW_2_AGU_L_LOCAL_START_OFFSET_1              0x60A10

#define mmMME0_CTRL_SHADOW_2_AGU_L_LOCAL_START_OFFSET_2              0x60A14

#define mmMME0_CTRL_SHADOW_2_AGU_L_LOCAL_START_OFFSET_3              0x60A18

#define mmMME0_CTRL_SHADOW_2_AGU_L_REMOTE_ROI_BASE_OFFSET_0          0x60A1C

#define mmMME0_CTRL_SHADOW_2_AGU_L_REMOTE_ROI_BASE_OFFSET_1          0x60A20

#define mmMME0_CTRL_SHADOW_2_AGU_L_REMOTE_ROI_BASE_OFFSET_2          0x60A24

#define mmMME0_CTRL_SHADOW_2_AGU_L_REMOTE_ROI_BASE_OFFSET_3          0x60A28

#define mmMME0_CTRL_SHADOW_2_AGU_L_REMOTE_ROI_BASE_OFFSET_4          0x60A2C

#define mmMME0_CTRL_SHADOW_2_AGU_L_REMOTE_START_OFFSET_0             0x60A30

#define mmMME0_CTRL_SHADOW_2_AGU_L_REMOTE_START_OFFSET_1             0x60A34

#define mmMME0_CTRL_SHADOW_2_AGU_L_REMOTE_START_OFFSET_2             0x60A38

#define mmMME0_CTRL_SHADOW_2_AGU_L_REMOTE_START_OFFSET_3             0x60A3C

#define mmMME0_CTRL_SHADOW_2_TENSOR_O_VALID_ELEMENTS_0               0x60A40

#define mmMME0_CTRL_SHADOW_2_TENSOR_O_VALID_ELEMENTS_1               0x60A44

#define mmMME0_CTRL_SHADOW_2_TENSOR_O_VALID_ELEMENTS_2               0x60A48

#define mmMME0_CTRL_SHADOW_2_TENSOR_O_VALID_ELEMENTS_3               0x60A4C

#define mmMME0_CTRL_SHADOW_2_TENSOR_O_VALID_ELEMENTS_4               0x60A50

#define mmMME0_CTRL_SHADOW_2_TENSOR_O_LOOP_STRIDE_0                  0x60A54

#define mmMME0_CTRL_SHADOW_2_TENSOR_O_LOOP_STRIDE_1                  0x60A58

#define mmMME0_CTRL_SHADOW_2_TENSOR_O_LOOP_STRIDE_2                  0x60A5C

#define mmMME0_CTRL_SHADOW_2_TENSOR_O_LOOP_STRIDE_3                  0x60A60

#define mmMME0_CTRL_SHADOW_2_TENSOR_O_LOOP_STRIDE_4                  0x60A64

#define mmMME0_CTRL_SHADOW_2_TENSOR_O_ROI_SIZE_0                     0x60A68

#define mmMME0_CTRL_SHADOW_2_TENSOR_O_ROI_SIZE_1                     0x60A6C

#define mmMME0_CTRL_SHADOW_2_TENSOR_O_ROI_SIZE_2                     0x60A70

#define mmMME0_CTRL_SHADOW_2_TENSOR_O_ROI_SIZE_3                     0x60A74

#define mmMME0_CTRL_SHADOW_2_TENSOR_O_SPATIAL_STRIDES_0              0x60A78

#define mmMME0_CTRL_SHADOW_2_TENSOR_O_SPATIAL_STRIDES_1              0x60A7C

#define mmMME0_CTRL_SHADOW_2_TENSOR_O_SPATIAL_STRIDES_2              0x60A80

#define mmMME0_CTRL_SHADOW_2_TENSOR_O_SPATIAL_STRIDES_3              0x60A84

#define mmMME0_CTRL_SHADOW_2_TENSOR_O_SPATIAL_SIZE_MINUS_1           0x60A88

#define mmMME0_CTRL_SHADOW_2_AGU_O_LOCAL_ROI_BASE_OFFSET_0           0x60A8C

#define mmMME0_CTRL_SHADOW_2_AGU_O_LOCAL_ROI_BASE_OFFSET_1           0x60A90

#define mmMME0_CTRL_SHADOW_2_AGU_O_LOCAL_ROI_BASE_OFFSET_2           0x60A94

#define mmMME0_CTRL_SHADOW_2_AGU_O_LOCAL_ROI_BASE_OFFSET_3           0x60A98

#define mmMME0_CTRL_SHADOW_2_AGU_O_LOCAL_ROI_BASE_OFFSET_4           0x60A9C

#define mmMME0_CTRL_SHADOW_2_AGU_O_LOCAL_START_OFFSET_0              0x60AA0

#define mmMME0_CTRL_SHADOW_2_AGU_O_LOCAL_START_OFFSET_1              0x60AA4

#define mmMME0_CTRL_SHADOW_2_AGU_O_LOCAL_START_OFFSET_2              0x60AA8

#define mmMME0_CTRL_SHADOW_2_AGU_O_LOCAL_START_OFFSET_3              0x60AAC

#define mmMME0_CTRL_SHADOW_2_AGU_O_REMOTE_ROI_BASE_OFFSET_0          0x60AB0

#define mmMME0_CTRL_SHADOW_2_AGU_O_REMOTE_ROI_BASE_OFFSET_1          0x60AB4

#define mmMME0_CTRL_SHADOW_2_AGU_O_REMOTE_ROI_BASE_OFFSET_2          0x60AB8

#define mmMME0_CTRL_SHADOW_2_AGU_O_REMOTE_ROI_BASE_OFFSET_3          0x60ABC

#define mmMME0_CTRL_SHADOW_2_AGU_O_REMOTE_ROI_BASE_OFFSET_4          0x60AC0

#define mmMME0_CTRL_SHADOW_2_AGU_O_REMOTE_START_OFFSET_0             0x60AC4

#define mmMME0_CTRL_SHADOW_2_AGU_O_REMOTE_START_OFFSET_1             0x60AC8

#define mmMME0_CTRL_SHADOW_2_AGU_O_REMOTE_START_OFFSET_2             0x60ACC

#define mmMME0_CTRL_SHADOW_2_AGU_O_REMOTE_START_OFFSET_3             0x60AD0

#define mmMME0_CTRL_SHADOW_2_DESC_SB_REPEAT                          0x60AD4

#define mmMME0_CTRL_SHADOW_2_DESC_RATE_LIMITER                       0x60AD8

#define mmMME0_CTRL_SHADOW_2_DESC_SYNC_OBJECT_ADDR_LOW_LOCAL         0x60ADC

#define mmMME0_CTRL_SHADOW_2_DESC_SYNC_OBJECT_ADDR_LOW_REMOTE        0x60AE0

#define mmMME0_CTRL_SHADOW_2_DESC_SYNC_OBJECT_ADDR_HIGH              0x60AE4

#define mmMME0_CTRL_SHADOW_2_DESC_SYNC_OBJECT_DATA                   0x60AE8

#define mmMME0_CTRL_SHADOW_2_DESC_AXI_USER_DATA                      0x60AEC

#define mmMME0_CTRL_SHADOW_2_DESC_PERF_EVT_S                         0x60AF0

#define mmMME0_CTRL_SHADOW_2_DESC_PERF_EVT_L_LOCAL                   0x60AF4

#define mmMME0_CTRL_SHADOW_2_DESC_PERF_EVT_L_REMOTE                  0x60AF8

#define mmMME0_CTRL_SHADOW_2_DESC_PERF_EVT_O_LOCAL                   0x60AFC

#define mmMME0_CTRL_SHADOW_2_DESC_PERF_EVT_O_REMOTE                  0x60B00

#define mmMME0_CTRL_SHADOW_2_DESC_PADDING_VALUE_S                    0x60B04

#define mmMME0_CTRL_SHADOW_2_DESC_PADDING_VALUE_L                    0x60B08

#define mmMME0_CTRL_SHADOW_2_DESC_META_DATA_AGU_S                    0x60B0C

#define mmMME0_CTRL_SHADOW_2_DESC_META_DATA_AGU_L_LOCAL              0x60B10

#define mmMME0_CTRL_SHADOW_2_DESC_META_DATA_AGU_L_REMOTE             0x60B14

#define mmMME0_CTRL_SHADOW_2_DESC_META_DATA_AGU_O_LOCAL              0x60B18

#define mmMME0_CTRL_SHADOW_2_DESC_META_DATA_AGU_O_REMOTE             0x60B1C

#define mmMME0_CTRL_SHADOW_2_DESC_PCU_RL_SATURATION                  0x60B20

#define mmMME0_CTRL_SHADOW_2_DESC_DUMMY                              0x60B24

#define mmMME0_CTRL_SHADOW_3_STATUS                                  0x60B80

#define mmMME0_CTRL_SHADOW_3_BASE_ADDR_HIGH_S                        0x60B88

#define mmMME0_CTRL_SHADOW_3_BASE_ADDR_HIGH_L                        0x60B8C

#define mmMME0_CTRL_SHADOW_3_BASE_ADDR_HIGH_O                        0x60B90

#define mmMME0_CTRL_SHADOW_3_BASE_ADDR_LOW_S                         0x60B94

#define mmMME0_CTRL_SHADOW_3_BASE_ADDR_LOW_L                         0x60B98

#define mmMME0_CTRL_SHADOW_3_BASE_ADDR_LOW_O                         0x60B9C

#define mmMME0_CTRL_SHADOW_3_HEADER_LOW                              0x60BA0

#define mmMME0_CTRL_SHADOW_3_HEADER_HIGH                             0x60BA4

#define mmMME0_CTRL_SHADOW_3_CONV_KERNEL_SIZE_MINUS_1                0x60BA8

#define mmMME0_CTRL_SHADOW_3_CONV_ASSOCIATED_DIMS_LOW                0x60BAC

#define mmMME0_CTRL_SHADOW_3_CONV_ASSOCIATED_DIMS_HIGH               0x60BB0

#define mmMME0_CTRL_SHADOW_3_NUM_ITERATIONS_MINUS_1                  0x60BB4

#define mmMME0_CTRL_SHADOW_3_OUTER_LOOP                              0x60BB8

#define mmMME0_CTRL_SHADOW_3_TENSOR_S_VALID_ELEMENTS_0               0x60BBC

#define mmMME0_CTRL_SHADOW_3_TENSOR_S_VALID_ELEMENTS_1               0x60BC0

#define mmMME0_CTRL_SHADOW_3_TENSOR_S_VALID_ELEMENTS_2               0x60BC4

#define mmMME0_CTRL_SHADOW_3_TENSOR_S_VALID_ELEMENTS_3               0x60BC8

#define mmMME0_CTRL_SHADOW_3_TENSOR_S_VALID_ELEMENTS_4               0x60BCC

#define mmMME0_CTRL_SHADOW_3_TENSOR_S_LOOP_STRIDE_0                  0x60BD0

#define mmMME0_CTRL_SHADOW_3_TENSOR_S_LOOP_STRIDE_1                  0x60BD4

#define mmMME0_CTRL_SHADOW_3_TENSOR_S_LOOP_STRIDE_2                  0x60BD8

#define mmMME0_CTRL_SHADOW_3_TENSOR_S_LOOP_STRIDE_3                  0x60BDC

#define mmMME0_CTRL_SHADOW_3_TENSOR_S_LOOP_STRIDE_4                  0x60BE0

#define mmMME0_CTRL_SHADOW_3_TENSOR_S_ROI_SIZE_0                     0x60BE4

#define mmMME0_CTRL_SHADOW_3_TENSOR_S_ROI_SIZE_1                     0x60BE8

#define mmMME0_CTRL_SHADOW_3_TENSOR_S_ROI_SIZE_2                     0x60BEC

#define mmMME0_CTRL_SHADOW_3_TENSOR_S_ROI_SIZE_3                     0x60BF0

#define mmMME0_CTRL_SHADOW_3_TENSOR_S_SPATIAL_STRIDES_0              0x60BF4

#define mmMME0_CTRL_SHADOW_3_TENSOR_S_SPATIAL_STRIDES_1              0x60BF8

#define mmMME0_CTRL_SHADOW_3_TENSOR_S_SPATIAL_STRIDES_2              0x60BFC

#define mmMME0_CTRL_SHADOW_3_TENSOR_S_SPATIAL_STRIDES_3              0x60C00

#define mmMME0_CTRL_SHADOW_3_TENSOR_S_SPATIAL_SIZE_MINUS_1           0x60C04

#define mmMME0_CTRL_SHADOW_3_AGU_S_ROI_BASE_OFFSET_0                 0x60C08

#define mmMME0_CTRL_SHADOW_3_AGU_S_ROI_BASE_OFFSET_1                 0x60C0C

#define mmMME0_CTRL_SHADOW_3_AGU_S_ROI_BASE_OFFSET_2                 0x60C10

#define mmMME0_CTRL_SHADOW_3_AGU_S_ROI_BASE_OFFSET_3                 0x60C14

#define mmMME0_CTRL_SHADOW_3_AGU_S_ROI_BASE_OFFSET_4                 0x60C18

#define mmMME0_CTRL_SHADOW_3_AGU_S_START_OFFSET_0                    0x60C1C

#define mmMME0_CTRL_SHADOW_3_AGU_S_START_OFFSET_1                    0x60C20

#define mmMME0_CTRL_SHADOW_3_AGU_S_START_OFFSET_2                    0x60C24

#define mmMME0_CTRL_SHADOW_3_AGU_S_START_OFFSET_3                    0x60C28

#define mmMME0_CTRL_SHADOW_3_TENSOR_L_VALID_ELEMENTS_0               0x60C2C

#define mmMME0_CTRL_SHADOW_3_TENSOR_L_VALID_ELEMENTS_1               0x60C30

#define mmMME0_CTRL_SHADOW_3_TENSOR_L_VALID_ELEMENTS_2               0x60C34

#define mmMME0_CTRL_SHADOW_3_TENSOR_L_VALID_ELEMENTS_3               0x60C38

#define mmMME0_CTRL_SHADOW_3_TENSOR_L_VALID_ELEMENTS_4               0x60C3C

#define mmMME0_CTRL_SHADOW_3_TENSOR_L_LOOP_STRIDE_0                  0x60C40

#define mmMME0_CTRL_SHADOW_3_TENSOR_L_LOOP_STRIDE_1                  0x60C44

#define mmMME0_CTRL_SHADOW_3_TENSOR_L_LOOP_STRIDE_2                  0x60C48

#define mmMME0_CTRL_SHADOW_3_TENSOR_L_LOOP_STRIDE_3                  0x60C4C

#define mmMME0_CTRL_SHADOW_3_TENSOR_L_LOOP_STRIDE_4                  0x60C50

#define mmMME0_CTRL_SHADOW_3_TENSOR_L_ROI_SIZE_0                     0x60C54

#define mmMME0_CTRL_SHADOW_3_TENSOR_L_ROI_SIZE_1                     0x60C58

#define mmMME0_CTRL_SHADOW_3_TENSOR_L_ROI_SIZE_2                     0x60C5C

#define mmMME0_CTRL_SHADOW_3_TENSOR_L_ROI_SIZE_3                     0x60C60

#define mmMME0_CTRL_SHADOW_3_TENSOR_L_SPATIAL_STRIDES_0              0x60C64

#define mmMME0_CTRL_SHADOW_3_TENSOR_L_SPATIAL_STRIDES_1              0x60C68

#define mmMME0_CTRL_SHADOW_3_TENSOR_L_SPATIAL_STRIDES_2              0x60C6C

#define mmMME0_CTRL_SHADOW_3_TENSOR_L_SPATIAL_STRIDES_3              0x60C70

#define mmMME0_CTRL_SHADOW_3_TENSOR_L_SPATIAL_SIZE_MINUS_1           0x60C74

#define mmMME0_CTRL_SHADOW_3_AGU_L_LOCAL_ROI_BASE_OFFSET_0           0x60C78

#define mmMME0_CTRL_SHADOW_3_AGU_L_LOCAL_ROI_BASE_OFFSET_1           0x60C7C

#define mmMME0_CTRL_SHADOW_3_AGU_L_LOCAL_ROI_BASE_OFFSET_2           0x60C80

#define mmMME0_CTRL_SHADOW_3_AGU_L_LOCAL_ROI_BASE_OFFSET_3           0x60C84

#define mmMME0_CTRL_SHADOW_3_AGU_L_LOCAL_ROI_BASE_OFFSET_4           0x60C88

#define mmMME0_CTRL_SHADOW_3_AGU_L_LOCAL_START_OFFSET_0              0x60C8C

#define mmMME0_CTRL_SHADOW_3_AGU_L_LOCAL_START_OFFSET_1              0x60C90

#define mmMME0_CTRL_SHADOW_3_AGU_L_LOCAL_START_OFFSET_2              0x60C94

#define mmMME0_CTRL_SHADOW_3_AGU_L_LOCAL_START_OFFSET_3              0x60C98

#define mmMME0_CTRL_SHADOW_3_AGU_L_REMOTE_ROI_BASE_OFFSET_0          0x60C9C

#define mmMME0_CTRL_SHADOW_3_AGU_L_REMOTE_ROI_BASE_OFFSET_1          0x60CA0

#define mmMME0_CTRL_SHADOW_3_AGU_L_REMOTE_ROI_BASE_OFFSET_2          0x60CA4

#define mmMME0_CTRL_SHADOW_3_AGU_L_REMOTE_ROI_BASE_OFFSET_3          0x60CA8

#define mmMME0_CTRL_SHADOW_3_AGU_L_REMOTE_ROI_BASE_OFFSET_4          0x60CAC

#define mmMME0_CTRL_SHADOW_3_AGU_L_REMOTE_START_OFFSET_0             0x60CB0

#define mmMME0_CTRL_SHADOW_3_AGU_L_REMOTE_START_OFFSET_1             0x60CB4

#define mmMME0_CTRL_SHADOW_3_AGU_L_REMOTE_START_OFFSET_2             0x60CB8

#define mmMME0_CTRL_SHADOW_3_AGU_L_REMOTE_START_OFFSET_3             0x60CBC

#define mmMME0_CTRL_SHADOW_3_TENSOR_O_VALID_ELEMENTS_0               0x60CC0

#define mmMME0_CTRL_SHADOW_3_TENSOR_O_VALID_ELEMENTS_1               0x60CC4

#define mmMME0_CTRL_SHADOW_3_TENSOR_O_VALID_ELEMENTS_2               0x60CC8

#define mmMME0_CTRL_SHADOW_3_TENSOR_O_VALID_ELEMENTS_3               0x60CCC

#define mmMME0_CTRL_SHADOW_3_TENSOR_O_VALID_ELEMENTS_4               0x60CD0

#define mmMME0_CTRL_SHADOW_3_TENSOR_O_LOOP_STRIDE_0                  0x60CD4

#define mmMME0_CTRL_SHADOW_3_TENSOR_O_LOOP_STRIDE_1                  0x60CD8

#define mmMME0_CTRL_SHADOW_3_TENSOR_O_LOOP_STRIDE_2                  0x60CDC

#define mmMME0_CTRL_SHADOW_3_TENSOR_O_LOOP_STRIDE_3                  0x60CE0

#define mmMME0_CTRL_SHADOW_3_TENSOR_O_LOOP_STRIDE_4                  0x60CE4

#define mmMME0_CTRL_SHADOW_3_TENSOR_O_ROI_SIZE_0                     0x60CE8

#define mmMME0_CTRL_SHADOW_3_TENSOR_O_ROI_SIZE_1                     0x60CEC

#define mmMME0_CTRL_SHADOW_3_TENSOR_O_ROI_SIZE_2                     0x60CF0

#define mmMME0_CTRL_SHADOW_3_TENSOR_O_ROI_SIZE_3                     0x60CF4

#define mmMME0_CTRL_SHADOW_3_TENSOR_O_SPATIAL_STRIDES_0              0x60CF8

#define mmMME0_CTRL_SHADOW_3_TENSOR_O_SPATIAL_STRIDES_1              0x60CFC

#define mmMME0_CTRL_SHADOW_3_TENSOR_O_SPATIAL_STRIDES_2              0x60D00

#define mmMME0_CTRL_SHADOW_3_TENSOR_O_SPATIAL_STRIDES_3              0x60D04

#define mmMME0_CTRL_SHADOW_3_TENSOR_O_SPATIAL_SIZE_MINUS_1           0x60D08

#define mmMME0_CTRL_SHADOW_3_AGU_O_LOCAL_ROI_BASE_OFFSET_0           0x60D0C

#define mmMME0_CTRL_SHADOW_3_AGU_O_LOCAL_ROI_BASE_OFFSET_1           0x60D10

#define mmMME0_CTRL_SHADOW_3_AGU_O_LOCAL_ROI_BASE_OFFSET_2           0x60D14

#define mmMME0_CTRL_SHADOW_3_AGU_O_LOCAL_ROI_BASE_OFFSET_3           0x60D18

#define mmMME0_CTRL_SHADOW_3_AGU_O_LOCAL_ROI_BASE_OFFSET_4           0x60D1C

#define mmMME0_CTRL_SHADOW_3_AGU_O_LOCAL_START_OFFSET_0              0x60D20

#define mmMME0_CTRL_SHADOW_3_AGU_O_LOCAL_START_OFFSET_1              0x60D24

#define mmMME0_CTRL_SHADOW_3_AGU_O_LOCAL_START_OFFSET_2              0x60D28

#define mmMME0_CTRL_SHADOW_3_AGU_O_LOCAL_START_OFFSET_3              0x60D2C

#define mmMME0_CTRL_SHADOW_3_AGU_O_REMOTE_ROI_BASE_OFFSET_0          0x60D30

#define mmMME0_CTRL_SHADOW_3_AGU_O_REMOTE_ROI_BASE_OFFSET_1          0x60D34

#define mmMME0_CTRL_SHADOW_3_AGU_O_REMOTE_ROI_BASE_OFFSET_2          0x60D38

#define mmMME0_CTRL_SHADOW_3_AGU_O_REMOTE_ROI_BASE_OFFSET_3          0x60D3C

#define mmMME0_CTRL_SHADOW_3_AGU_O_REMOTE_ROI_BASE_OFFSET_4          0x60D40

#define mmMME0_CTRL_SHADOW_3_AGU_O_REMOTE_START_OFFSET_0             0x60D44

#define mmMME0_CTRL_SHADOW_3_AGU_O_REMOTE_START_OFFSET_1             0x60D48

#define mmMME0_CTRL_SHADOW_3_AGU_O_REMOTE_START_OFFSET_2             0x60D4C

#define mmMME0_CTRL_SHADOW_3_AGU_O_REMOTE_START_OFFSET_3             0x60D50

#define mmMME0_CTRL_SHADOW_3_DESC_SB_REPEAT                          0x60D54

#define mmMME0_CTRL_SHADOW_3_DESC_RATE_LIMITER                       0x60D58

#define mmMME0_CTRL_SHADOW_3_DESC_SYNC_OBJECT_ADDR_LOW_LOCAL         0x60D5C

#define mmMME0_CTRL_SHADOW_3_DESC_SYNC_OBJECT_ADDR_LOW_REMOTE        0x60D60

#define mmMME0_CTRL_SHADOW_3_DESC_SYNC_OBJECT_ADDR_HIGH              0x60D64

#define mmMME0_CTRL_SHADOW_3_DESC_SYNC_OBJECT_DATA                   0x60D68

#define mmMME0_CTRL_SHADOW_3_DESC_AXI_USER_DATA                      0x60D6C

#define mmMME0_CTRL_SHADOW_3_DESC_PERF_EVT_S                         0x60D70

#define mmMME0_CTRL_SHADOW_3_DESC_PERF_EVT_L_LOCAL                   0x60D74

#define mmMME0_CTRL_SHADOW_3_DESC_PERF_EVT_L_REMOTE                  0x60D78

#define mmMME0_CTRL_SHADOW_3_DESC_PERF_EVT_O_LOCAL                   0x60D7C

#define mmMME0_CTRL_SHADOW_3_DESC_PERF_EVT_O_REMOTE                  0x60D80

#define mmMME0_CTRL_SHADOW_3_DESC_PADDING_VALUE_S                    0x60D84

#define mmMME0_CTRL_SHADOW_3_DESC_PADDING_VALUE_L                    0x60D88

#define mmMME0_CTRL_SHADOW_3_DESC_META_DATA_AGU_S                    0x60D8C

#define mmMME0_CTRL_SHADOW_3_DESC_META_DATA_AGU_L_LOCAL              0x60D90

#define mmMME0_CTRL_SHADOW_3_DESC_META_DATA_AGU_L_REMOTE             0x60D94

#define mmMME0_CTRL_SHADOW_3_DESC_META_DATA_AGU_O_LOCAL              0x60D98

#define mmMME0_CTRL_SHADOW_3_DESC_META_DATA_AGU_O_REMOTE             0x60D9C

#define mmMME0_CTRL_SHADOW_3_DESC_PCU_RL_SATURATION                  0x60DA0

#define mmMME0_CTRL_SHADOW_3_DESC_DUMMY                              0x60DA4

#endif /* ASIC_REG_MME0_CTRL_REGS_H_ */
