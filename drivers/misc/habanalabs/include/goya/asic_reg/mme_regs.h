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

#ifndef ASIC_REG_MME_REGS_H_
#define ASIC_REG_MME_REGS_H_

/*
 *****************************************
 *   MME (Prototype: MME)
 *****************************************
 */

#define mmMME_ARCH_STATUS                                            0xD0000

#define mmMME_ARCH_A_BASE_ADDR_HIGH                                  0xD0008

#define mmMME_ARCH_B_BASE_ADDR_HIGH                                  0xD000C

#define mmMME_ARCH_CIN_BASE_ADDR_HIGH                                0xD0010

#define mmMME_ARCH_COUT_BASE_ADDR_HIGH                               0xD0014

#define mmMME_ARCH_BIAS_BASE_ADDR_HIGH                               0xD0018

#define mmMME_ARCH_A_BASE_ADDR_LOW                                   0xD001C

#define mmMME_ARCH_B_BASE_ADDR_LOW                                   0xD0020

#define mmMME_ARCH_CIN_BASE_ADDR_LOW                                 0xD0024

#define mmMME_ARCH_COUT_BASE_ADDR_LOW                                0xD0028

#define mmMME_ARCH_BIAS_BASE_ADDR_LOW                                0xD002C

#define mmMME_ARCH_HEADER                                            0xD0030

#define mmMME_ARCH_KERNEL_SIZE_MINUS_1                               0xD0034

#define mmMME_ARCH_ASSOCIATED_DIMS_0                                 0xD0038

#define mmMME_ARCH_ASSOCIATED_DIMS_1                                 0xD003C

#define mmMME_ARCH_COUT_SCALE                                        0xD0040

#define mmMME_ARCH_CIN_SCALE                                         0xD0044

#define mmMME_ARCH_GEMMLOWP_ZP                                       0xD0048

#define mmMME_ARCH_GEMMLOWP_EXPONENT                                 0xD004C

#define mmMME_ARCH_A_ROI_BASE_OFFSET_0                               0xD0050

#define mmMME_ARCH_A_ROI_BASE_OFFSET_1                               0xD0054

#define mmMME_ARCH_A_ROI_BASE_OFFSET_2                               0xD0058

#define mmMME_ARCH_A_ROI_BASE_OFFSET_3                               0xD005C

#define mmMME_ARCH_A_ROI_BASE_OFFSET_4                               0xD0060

#define mmMME_ARCH_A_VALID_ELEMENTS_0                                0xD0064

#define mmMME_ARCH_A_VALID_ELEMENTS_1                                0xD0068

#define mmMME_ARCH_A_VALID_ELEMENTS_2                                0xD006C

#define mmMME_ARCH_A_VALID_ELEMENTS_3                                0xD0070

#define mmMME_ARCH_A_VALID_ELEMENTS_4                                0xD0074

#define mmMME_ARCH_A_LOOP_STRIDE_0                                   0xD0078

#define mmMME_ARCH_A_LOOP_STRIDE_1                                   0xD007C

#define mmMME_ARCH_A_LOOP_STRIDE_2                                   0xD0080

#define mmMME_ARCH_A_LOOP_STRIDE_3                                   0xD0084

#define mmMME_ARCH_A_LOOP_STRIDE_4                                   0xD0088

#define mmMME_ARCH_A_ROI_SIZE_0                                      0xD008C

#define mmMME_ARCH_A_ROI_SIZE_1                                      0xD0090

#define mmMME_ARCH_A_ROI_SIZE_2                                      0xD0094

#define mmMME_ARCH_A_ROI_SIZE_3                                      0xD0098

#define mmMME_ARCH_A_SPATIAL_START_OFFSET_0                          0xD009C

#define mmMME_ARCH_A_SPATIAL_START_OFFSET_1                          0xD00A0

#define mmMME_ARCH_A_SPATIAL_START_OFFSET_2                          0xD00A4

#define mmMME_ARCH_A_SPATIAL_START_OFFSET_3                          0xD00A8

#define mmMME_ARCH_A_SPATIAL_STRIDE_0                                0xD00AC

#define mmMME_ARCH_A_SPATIAL_STRIDE_1                                0xD00B0

#define mmMME_ARCH_A_SPATIAL_STRIDE_2                                0xD00B4

#define mmMME_ARCH_A_SPATIAL_STRIDE_3                                0xD00B8

#define mmMME_ARCH_A_SPATIAL_SIZE_MINUS_1                            0xD00BC

#define mmMME_ARCH_B_ROI_BASE_OFFSET_0                               0xD00C0

#define mmMME_ARCH_B_ROI_BASE_OFFSET_1                               0xD00C4

#define mmMME_ARCH_B_ROI_BASE_OFFSET_2                               0xD00C8

#define mmMME_ARCH_B_ROI_BASE_OFFSET_3                               0xD00CC

#define mmMME_ARCH_B_ROI_BASE_OFFSET_4                               0xD00D0

#define mmMME_ARCH_B_VALID_ELEMENTS_0                                0xD00D4

#define mmMME_ARCH_B_VALID_ELEMENTS_1                                0xD00D8

#define mmMME_ARCH_B_VALID_ELEMENTS_2                                0xD00DC

#define mmMME_ARCH_B_VALID_ELEMENTS_3                                0xD00E0

#define mmMME_ARCH_B_VALID_ELEMENTS_4                                0xD00E4

#define mmMME_ARCH_B_LOOP_STRIDE_0                                   0xD00E8

#define mmMME_ARCH_B_LOOP_STRIDE_1                                   0xD00EC

#define mmMME_ARCH_B_LOOP_STRIDE_2                                   0xD00F0

#define mmMME_ARCH_B_LOOP_STRIDE_3                                   0xD00F4

#define mmMME_ARCH_B_LOOP_STRIDE_4                                   0xD00F8

#define mmMME_ARCH_B_ROI_SIZE_0                                      0xD00FC

#define mmMME_ARCH_B_ROI_SIZE_1                                      0xD0100

#define mmMME_ARCH_B_ROI_SIZE_2                                      0xD0104

#define mmMME_ARCH_B_ROI_SIZE_3                                      0xD0108

#define mmMME_ARCH_B_SPATIAL_START_OFFSET_0                          0xD010C

#define mmMME_ARCH_B_SPATIAL_START_OFFSET_1                          0xD0110

#define mmMME_ARCH_B_SPATIAL_START_OFFSET_2                          0xD0114

#define mmMME_ARCH_B_SPATIAL_START_OFFSET_3                          0xD0118

#define mmMME_ARCH_B_SPATIAL_STRIDE_0                                0xD011C

#define mmMME_ARCH_B_SPATIAL_STRIDE_1                                0xD0120

#define mmMME_ARCH_B_SPATIAL_STRIDE_2                                0xD0124

#define mmMME_ARCH_B_SPATIAL_STRIDE_3                                0xD0128

#define mmMME_ARCH_B_SPATIAL_SIZE_MINUS_1                            0xD012C

#define mmMME_ARCH_C_ROI_BASE_OFFSET_0                               0xD0130

#define mmMME_ARCH_C_ROI_BASE_OFFSET_1                               0xD0134

#define mmMME_ARCH_C_ROI_BASE_OFFSET_2                               0xD0138

#define mmMME_ARCH_C_ROI_BASE_OFFSET_3                               0xD013C

#define mmMME_ARCH_C_ROI_BASE_OFFSET_4                               0xD0140

#define mmMME_ARCH_C_VALID_ELEMENTS_0                                0xD0144

#define mmMME_ARCH_C_VALID_ELEMENTS_1                                0xD0148

#define mmMME_ARCH_C_VALID_ELEMENTS_2                                0xD014C

#define mmMME_ARCH_C_VALID_ELEMENTS_3                                0xD0150

#define mmMME_ARCH_C_VALID_ELEMENTS_4                                0xD0154

#define mmMME_ARCH_C_LOOP_STRIDE_0                                   0xD0158

#define mmMME_ARCH_C_LOOP_STRIDE_1                                   0xD015C

#define mmMME_ARCH_C_LOOP_STRIDE_2                                   0xD0160

#define mmMME_ARCH_C_LOOP_STRIDE_3                                   0xD0164

#define mmMME_ARCH_C_LOOP_STRIDE_4                                   0xD0168

#define mmMME_ARCH_C_ROI_SIZE_0                                      0xD016C

#define mmMME_ARCH_C_ROI_SIZE_1                                      0xD0170

#define mmMME_ARCH_C_ROI_SIZE_2                                      0xD0174

#define mmMME_ARCH_C_ROI_SIZE_3                                      0xD0178

#define mmMME_ARCH_C_SPATIAL_START_OFFSET_0                          0xD017C

#define mmMME_ARCH_C_SPATIAL_START_OFFSET_1                          0xD0180

#define mmMME_ARCH_C_SPATIAL_START_OFFSET_2                          0xD0184

#define mmMME_ARCH_C_SPATIAL_START_OFFSET_3                          0xD0188

#define mmMME_ARCH_C_SPATIAL_STRIDE_0                                0xD018C

#define mmMME_ARCH_C_SPATIAL_STRIDE_1                                0xD0190

#define mmMME_ARCH_C_SPATIAL_STRIDE_2                                0xD0194

#define mmMME_ARCH_C_SPATIAL_STRIDE_3                                0xD0198

#define mmMME_ARCH_C_SPATIAL_SIZE_MINUS_1                            0xD019C

#define mmMME_ARCH_SYNC_OBJECT_MESSAGE                               0xD01A0

#define mmMME_ARCH_E_PADDING_VALUE_A                                 0xD01A4

#define mmMME_ARCH_E_NUM_ITERATION_MINUS_1                           0xD01A8

#define mmMME_ARCH_E_BUBBLES_PER_SPLIT                               0xD01AC

#define mmMME_CMD                                                    0xD0200

#define mmMME_DUMMY                                                  0xD0204

#define mmMME_RESET                                                  0xD0208

#define mmMME_STALL                                                  0xD020C

#define mmMME_SM_BASE_ADDRESS_LOW                                    0xD0210

#define mmMME_SM_BASE_ADDRESS_HIGH                                   0xD0214

#define mmMME_DBGMEM_ADD                                             0xD0218

#define mmMME_DBGMEM_DATA_WR                                         0xD021C

#define mmMME_DBGMEM_DATA_RD                                         0xD0220

#define mmMME_DBGMEM_CTRL                                            0xD0224

#define mmMME_DBGMEM_RC                                              0xD0228

#define mmMME_LOG_SHADOW                                             0xD022C

#define mmMME_STORE_MAX_CREDIT                                       0xD0300

#define mmMME_AGU                                                    0xD0304

#define mmMME_SBA                                                    0xD0308

#define mmMME_SBB                                                    0xD030C

#define mmMME_SBC                                                    0xD0310

#define mmMME_WBC                                                    0xD0314

#define mmMME_SBA_CONTROL_DATA                                       0xD0318

#define mmMME_SBB_CONTROL_DATA                                       0xD031C

#define mmMME_SBC_CONTROL_DATA                                       0xD0320

#define mmMME_WBC_CONTROL_DATA                                       0xD0324

#define mmMME_TE                                                     0xD0328

#define mmMME_TE2DEC                                                 0xD032C

#define mmMME_REI_STATUS                                             0xD0330

#define mmMME_REI_MASK                                               0xD0334

#define mmMME_SEI_STATUS                                             0xD0338

#define mmMME_SEI_MASK                                               0xD033C

#define mmMME_SPI_STATUS                                             0xD0340

#define mmMME_SPI_MASK                                               0xD0344

#define mmMME_SHADOW_0_STATUS                                        0xD0400

#define mmMME_SHADOW_0_A_BASE_ADDR_HIGH                              0xD0408

#define mmMME_SHADOW_0_B_BASE_ADDR_HIGH                              0xD040C

#define mmMME_SHADOW_0_CIN_BASE_ADDR_HIGH                            0xD0410

#define mmMME_SHADOW_0_COUT_BASE_ADDR_HIGH                           0xD0414

#define mmMME_SHADOW_0_BIAS_BASE_ADDR_HIGH                           0xD0418

#define mmMME_SHADOW_0_A_BASE_ADDR_LOW                               0xD041C

#define mmMME_SHADOW_0_B_BASE_ADDR_LOW                               0xD0420

#define mmMME_SHADOW_0_CIN_BASE_ADDR_LOW                             0xD0424

#define mmMME_SHADOW_0_COUT_BASE_ADDR_LOW                            0xD0428

#define mmMME_SHADOW_0_BIAS_BASE_ADDR_LOW                            0xD042C

#define mmMME_SHADOW_0_HEADER                                        0xD0430

#define mmMME_SHADOW_0_KERNEL_SIZE_MINUS_1                           0xD0434

#define mmMME_SHADOW_0_ASSOCIATED_DIMS_0                             0xD0438

#define mmMME_SHADOW_0_ASSOCIATED_DIMS_1                             0xD043C

#define mmMME_SHADOW_0_COUT_SCALE                                    0xD0440

#define mmMME_SHADOW_0_CIN_SCALE                                     0xD0444

#define mmMME_SHADOW_0_GEMMLOWP_ZP                                   0xD0448

#define mmMME_SHADOW_0_GEMMLOWP_EXPONENT                             0xD044C

#define mmMME_SHADOW_0_A_ROI_BASE_OFFSET_0                           0xD0450

#define mmMME_SHADOW_0_A_ROI_BASE_OFFSET_1                           0xD0454

#define mmMME_SHADOW_0_A_ROI_BASE_OFFSET_2                           0xD0458

#define mmMME_SHADOW_0_A_ROI_BASE_OFFSET_3                           0xD045C

#define mmMME_SHADOW_0_A_ROI_BASE_OFFSET_4                           0xD0460

#define mmMME_SHADOW_0_A_VALID_ELEMENTS_0                            0xD0464

#define mmMME_SHADOW_0_A_VALID_ELEMENTS_1                            0xD0468

#define mmMME_SHADOW_0_A_VALID_ELEMENTS_2                            0xD046C

#define mmMME_SHADOW_0_A_VALID_ELEMENTS_3                            0xD0470

#define mmMME_SHADOW_0_A_VALID_ELEMENTS_4                            0xD0474

#define mmMME_SHADOW_0_A_LOOP_STRIDE_0                               0xD0478

#define mmMME_SHADOW_0_A_LOOP_STRIDE_1                               0xD047C

#define mmMME_SHADOW_0_A_LOOP_STRIDE_2                               0xD0480

#define mmMME_SHADOW_0_A_LOOP_STRIDE_3                               0xD0484

#define mmMME_SHADOW_0_A_LOOP_STRIDE_4                               0xD0488

#define mmMME_SHADOW_0_A_ROI_SIZE_0                                  0xD048C

#define mmMME_SHADOW_0_A_ROI_SIZE_1                                  0xD0490

#define mmMME_SHADOW_0_A_ROI_SIZE_2                                  0xD0494

#define mmMME_SHADOW_0_A_ROI_SIZE_3                                  0xD0498

#define mmMME_SHADOW_0_A_SPATIAL_START_OFFSET_0                      0xD049C

#define mmMME_SHADOW_0_A_SPATIAL_START_OFFSET_1                      0xD04A0

#define mmMME_SHADOW_0_A_SPATIAL_START_OFFSET_2                      0xD04A4

#define mmMME_SHADOW_0_A_SPATIAL_START_OFFSET_3                      0xD04A8

#define mmMME_SHADOW_0_A_SPATIAL_STRIDE_0                            0xD04AC

#define mmMME_SHADOW_0_A_SPATIAL_STRIDE_1                            0xD04B0

#define mmMME_SHADOW_0_A_SPATIAL_STRIDE_2                            0xD04B4

#define mmMME_SHADOW_0_A_SPATIAL_STRIDE_3                            0xD04B8

#define mmMME_SHADOW_0_A_SPATIAL_SIZE_MINUS_1                        0xD04BC

#define mmMME_SHADOW_0_B_ROI_BASE_OFFSET_0                           0xD04C0

#define mmMME_SHADOW_0_B_ROI_BASE_OFFSET_1                           0xD04C4

#define mmMME_SHADOW_0_B_ROI_BASE_OFFSET_2                           0xD04C8

#define mmMME_SHADOW_0_B_ROI_BASE_OFFSET_3                           0xD04CC

#define mmMME_SHADOW_0_B_ROI_BASE_OFFSET_4                           0xD04D0

#define mmMME_SHADOW_0_B_VALID_ELEMENTS_0                            0xD04D4

#define mmMME_SHADOW_0_B_VALID_ELEMENTS_1                            0xD04D8

#define mmMME_SHADOW_0_B_VALID_ELEMENTS_2                            0xD04DC

#define mmMME_SHADOW_0_B_VALID_ELEMENTS_3                            0xD04E0

#define mmMME_SHADOW_0_B_VALID_ELEMENTS_4                            0xD04E4

#define mmMME_SHADOW_0_B_LOOP_STRIDE_0                               0xD04E8

#define mmMME_SHADOW_0_B_LOOP_STRIDE_1                               0xD04EC

#define mmMME_SHADOW_0_B_LOOP_STRIDE_2                               0xD04F0

#define mmMME_SHADOW_0_B_LOOP_STRIDE_3                               0xD04F4

#define mmMME_SHADOW_0_B_LOOP_STRIDE_4                               0xD04F8

#define mmMME_SHADOW_0_B_ROI_SIZE_0                                  0xD04FC

#define mmMME_SHADOW_0_B_ROI_SIZE_1                                  0xD0500

#define mmMME_SHADOW_0_B_ROI_SIZE_2                                  0xD0504

#define mmMME_SHADOW_0_B_ROI_SIZE_3                                  0xD0508

#define mmMME_SHADOW_0_B_SPATIAL_START_OFFSET_0                      0xD050C

#define mmMME_SHADOW_0_B_SPATIAL_START_OFFSET_1                      0xD0510

#define mmMME_SHADOW_0_B_SPATIAL_START_OFFSET_2                      0xD0514

#define mmMME_SHADOW_0_B_SPATIAL_START_OFFSET_3                      0xD0518

#define mmMME_SHADOW_0_B_SPATIAL_STRIDE_0                            0xD051C

#define mmMME_SHADOW_0_B_SPATIAL_STRIDE_1                            0xD0520

#define mmMME_SHADOW_0_B_SPATIAL_STRIDE_2                            0xD0524

#define mmMME_SHADOW_0_B_SPATIAL_STRIDE_3                            0xD0528

#define mmMME_SHADOW_0_B_SPATIAL_SIZE_MINUS_1                        0xD052C

#define mmMME_SHADOW_0_C_ROI_BASE_OFFSET_0                           0xD0530

#define mmMME_SHADOW_0_C_ROI_BASE_OFFSET_1                           0xD0534

#define mmMME_SHADOW_0_C_ROI_BASE_OFFSET_2                           0xD0538

#define mmMME_SHADOW_0_C_ROI_BASE_OFFSET_3                           0xD053C

#define mmMME_SHADOW_0_C_ROI_BASE_OFFSET_4                           0xD0540

#define mmMME_SHADOW_0_C_VALID_ELEMENTS_0                            0xD0544

#define mmMME_SHADOW_0_C_VALID_ELEMENTS_1                            0xD0548

#define mmMME_SHADOW_0_C_VALID_ELEMENTS_2                            0xD054C

#define mmMME_SHADOW_0_C_VALID_ELEMENTS_3                            0xD0550

#define mmMME_SHADOW_0_C_VALID_ELEMENTS_4                            0xD0554

#define mmMME_SHADOW_0_C_LOOP_STRIDE_0                               0xD0558

#define mmMME_SHADOW_0_C_LOOP_STRIDE_1                               0xD055C

#define mmMME_SHADOW_0_C_LOOP_STRIDE_2                               0xD0560

#define mmMME_SHADOW_0_C_LOOP_STRIDE_3                               0xD0564

#define mmMME_SHADOW_0_C_LOOP_STRIDE_4                               0xD0568

#define mmMME_SHADOW_0_C_ROI_SIZE_0                                  0xD056C

#define mmMME_SHADOW_0_C_ROI_SIZE_1                                  0xD0570

#define mmMME_SHADOW_0_C_ROI_SIZE_2                                  0xD0574

#define mmMME_SHADOW_0_C_ROI_SIZE_3                                  0xD0578

#define mmMME_SHADOW_0_C_SPATIAL_START_OFFSET_0                      0xD057C

#define mmMME_SHADOW_0_C_SPATIAL_START_OFFSET_1                      0xD0580

#define mmMME_SHADOW_0_C_SPATIAL_START_OFFSET_2                      0xD0584

#define mmMME_SHADOW_0_C_SPATIAL_START_OFFSET_3                      0xD0588

#define mmMME_SHADOW_0_C_SPATIAL_STRIDE_0                            0xD058C

#define mmMME_SHADOW_0_C_SPATIAL_STRIDE_1                            0xD0590

#define mmMME_SHADOW_0_C_SPATIAL_STRIDE_2                            0xD0594

#define mmMME_SHADOW_0_C_SPATIAL_STRIDE_3                            0xD0598

#define mmMME_SHADOW_0_C_SPATIAL_SIZE_MINUS_1                        0xD059C

#define mmMME_SHADOW_0_SYNC_OBJECT_MESSAGE                           0xD05A0

#define mmMME_SHADOW_0_E_PADDING_VALUE_A                             0xD05A4

#define mmMME_SHADOW_0_E_NUM_ITERATION_MINUS_1                       0xD05A8

#define mmMME_SHADOW_0_E_BUBBLES_PER_SPLIT                           0xD05AC

#define mmMME_SHADOW_1_STATUS                                        0xD0600

#define mmMME_SHADOW_1_A_BASE_ADDR_HIGH                              0xD0608

#define mmMME_SHADOW_1_B_BASE_ADDR_HIGH                              0xD060C

#define mmMME_SHADOW_1_CIN_BASE_ADDR_HIGH                            0xD0610

#define mmMME_SHADOW_1_COUT_BASE_ADDR_HIGH                           0xD0614

#define mmMME_SHADOW_1_BIAS_BASE_ADDR_HIGH                           0xD0618

#define mmMME_SHADOW_1_A_BASE_ADDR_LOW                               0xD061C

#define mmMME_SHADOW_1_B_BASE_ADDR_LOW                               0xD0620

#define mmMME_SHADOW_1_CIN_BASE_ADDR_LOW                             0xD0624

#define mmMME_SHADOW_1_COUT_BASE_ADDR_LOW                            0xD0628

#define mmMME_SHADOW_1_BIAS_BASE_ADDR_LOW                            0xD062C

#define mmMME_SHADOW_1_HEADER                                        0xD0630

#define mmMME_SHADOW_1_KERNEL_SIZE_MINUS_1                           0xD0634

#define mmMME_SHADOW_1_ASSOCIATED_DIMS_0                             0xD0638

#define mmMME_SHADOW_1_ASSOCIATED_DIMS_1                             0xD063C

#define mmMME_SHADOW_1_COUT_SCALE                                    0xD0640

#define mmMME_SHADOW_1_CIN_SCALE                                     0xD0644

#define mmMME_SHADOW_1_GEMMLOWP_ZP                                   0xD0648

#define mmMME_SHADOW_1_GEMMLOWP_EXPONENT                             0xD064C

#define mmMME_SHADOW_1_A_ROI_BASE_OFFSET_0                           0xD0650

#define mmMME_SHADOW_1_A_ROI_BASE_OFFSET_1                           0xD0654

#define mmMME_SHADOW_1_A_ROI_BASE_OFFSET_2                           0xD0658

#define mmMME_SHADOW_1_A_ROI_BASE_OFFSET_3                           0xD065C

#define mmMME_SHADOW_1_A_ROI_BASE_OFFSET_4                           0xD0660

#define mmMME_SHADOW_1_A_VALID_ELEMENTS_0                            0xD0664

#define mmMME_SHADOW_1_A_VALID_ELEMENTS_1                            0xD0668

#define mmMME_SHADOW_1_A_VALID_ELEMENTS_2                            0xD066C

#define mmMME_SHADOW_1_A_VALID_ELEMENTS_3                            0xD0670

#define mmMME_SHADOW_1_A_VALID_ELEMENTS_4                            0xD0674

#define mmMME_SHADOW_1_A_LOOP_STRIDE_0                               0xD0678

#define mmMME_SHADOW_1_A_LOOP_STRIDE_1                               0xD067C

#define mmMME_SHADOW_1_A_LOOP_STRIDE_2                               0xD0680

#define mmMME_SHADOW_1_A_LOOP_STRIDE_3                               0xD0684

#define mmMME_SHADOW_1_A_LOOP_STRIDE_4                               0xD0688

#define mmMME_SHADOW_1_A_ROI_SIZE_0                                  0xD068C

#define mmMME_SHADOW_1_A_ROI_SIZE_1                                  0xD0690

#define mmMME_SHADOW_1_A_ROI_SIZE_2                                  0xD0694

#define mmMME_SHADOW_1_A_ROI_SIZE_3                                  0xD0698

#define mmMME_SHADOW_1_A_SPATIAL_START_OFFSET_0                      0xD069C

#define mmMME_SHADOW_1_A_SPATIAL_START_OFFSET_1                      0xD06A0

#define mmMME_SHADOW_1_A_SPATIAL_START_OFFSET_2                      0xD06A4

#define mmMME_SHADOW_1_A_SPATIAL_START_OFFSET_3                      0xD06A8

#define mmMME_SHADOW_1_A_SPATIAL_STRIDE_0                            0xD06AC

#define mmMME_SHADOW_1_A_SPATIAL_STRIDE_1                            0xD06B0

#define mmMME_SHADOW_1_A_SPATIAL_STRIDE_2                            0xD06B4

#define mmMME_SHADOW_1_A_SPATIAL_STRIDE_3                            0xD06B8

#define mmMME_SHADOW_1_A_SPATIAL_SIZE_MINUS_1                        0xD06BC

#define mmMME_SHADOW_1_B_ROI_BASE_OFFSET_0                           0xD06C0

#define mmMME_SHADOW_1_B_ROI_BASE_OFFSET_1                           0xD06C4

#define mmMME_SHADOW_1_B_ROI_BASE_OFFSET_2                           0xD06C8

#define mmMME_SHADOW_1_B_ROI_BASE_OFFSET_3                           0xD06CC

#define mmMME_SHADOW_1_B_ROI_BASE_OFFSET_4                           0xD06D0

#define mmMME_SHADOW_1_B_VALID_ELEMENTS_0                            0xD06D4

#define mmMME_SHADOW_1_B_VALID_ELEMENTS_1                            0xD06D8

#define mmMME_SHADOW_1_B_VALID_ELEMENTS_2                            0xD06DC

#define mmMME_SHADOW_1_B_VALID_ELEMENTS_3                            0xD06E0

#define mmMME_SHADOW_1_B_VALID_ELEMENTS_4                            0xD06E4

#define mmMME_SHADOW_1_B_LOOP_STRIDE_0                               0xD06E8

#define mmMME_SHADOW_1_B_LOOP_STRIDE_1                               0xD06EC

#define mmMME_SHADOW_1_B_LOOP_STRIDE_2                               0xD06F0

#define mmMME_SHADOW_1_B_LOOP_STRIDE_3                               0xD06F4

#define mmMME_SHADOW_1_B_LOOP_STRIDE_4                               0xD06F8

#define mmMME_SHADOW_1_B_ROI_SIZE_0                                  0xD06FC

#define mmMME_SHADOW_1_B_ROI_SIZE_1                                  0xD0700

#define mmMME_SHADOW_1_B_ROI_SIZE_2                                  0xD0704

#define mmMME_SHADOW_1_B_ROI_SIZE_3                                  0xD0708

#define mmMME_SHADOW_1_B_SPATIAL_START_OFFSET_0                      0xD070C

#define mmMME_SHADOW_1_B_SPATIAL_START_OFFSET_1                      0xD0710

#define mmMME_SHADOW_1_B_SPATIAL_START_OFFSET_2                      0xD0714

#define mmMME_SHADOW_1_B_SPATIAL_START_OFFSET_3                      0xD0718

#define mmMME_SHADOW_1_B_SPATIAL_STRIDE_0                            0xD071C

#define mmMME_SHADOW_1_B_SPATIAL_STRIDE_1                            0xD0720

#define mmMME_SHADOW_1_B_SPATIAL_STRIDE_2                            0xD0724

#define mmMME_SHADOW_1_B_SPATIAL_STRIDE_3                            0xD0728

#define mmMME_SHADOW_1_B_SPATIAL_SIZE_MINUS_1                        0xD072C

#define mmMME_SHADOW_1_C_ROI_BASE_OFFSET_0                           0xD0730

#define mmMME_SHADOW_1_C_ROI_BASE_OFFSET_1                           0xD0734

#define mmMME_SHADOW_1_C_ROI_BASE_OFFSET_2                           0xD0738

#define mmMME_SHADOW_1_C_ROI_BASE_OFFSET_3                           0xD073C

#define mmMME_SHADOW_1_C_ROI_BASE_OFFSET_4                           0xD0740

#define mmMME_SHADOW_1_C_VALID_ELEMENTS_0                            0xD0744

#define mmMME_SHADOW_1_C_VALID_ELEMENTS_1                            0xD0748

#define mmMME_SHADOW_1_C_VALID_ELEMENTS_2                            0xD074C

#define mmMME_SHADOW_1_C_VALID_ELEMENTS_3                            0xD0750

#define mmMME_SHADOW_1_C_VALID_ELEMENTS_4                            0xD0754

#define mmMME_SHADOW_1_C_LOOP_STRIDE_0                               0xD0758

#define mmMME_SHADOW_1_C_LOOP_STRIDE_1                               0xD075C

#define mmMME_SHADOW_1_C_LOOP_STRIDE_2                               0xD0760

#define mmMME_SHADOW_1_C_LOOP_STRIDE_3                               0xD0764

#define mmMME_SHADOW_1_C_LOOP_STRIDE_4                               0xD0768

#define mmMME_SHADOW_1_C_ROI_SIZE_0                                  0xD076C

#define mmMME_SHADOW_1_C_ROI_SIZE_1                                  0xD0770

#define mmMME_SHADOW_1_C_ROI_SIZE_2                                  0xD0774

#define mmMME_SHADOW_1_C_ROI_SIZE_3                                  0xD0778

#define mmMME_SHADOW_1_C_SPATIAL_START_OFFSET_0                      0xD077C

#define mmMME_SHADOW_1_C_SPATIAL_START_OFFSET_1                      0xD0780

#define mmMME_SHADOW_1_C_SPATIAL_START_OFFSET_2                      0xD0784

#define mmMME_SHADOW_1_C_SPATIAL_START_OFFSET_3                      0xD0788

#define mmMME_SHADOW_1_C_SPATIAL_STRIDE_0                            0xD078C

#define mmMME_SHADOW_1_C_SPATIAL_STRIDE_1                            0xD0790

#define mmMME_SHADOW_1_C_SPATIAL_STRIDE_2                            0xD0794

#define mmMME_SHADOW_1_C_SPATIAL_STRIDE_3                            0xD0798

#define mmMME_SHADOW_1_C_SPATIAL_SIZE_MINUS_1                        0xD079C

#define mmMME_SHADOW_1_SYNC_OBJECT_MESSAGE                           0xD07A0

#define mmMME_SHADOW_1_E_PADDING_VALUE_A                             0xD07A4

#define mmMME_SHADOW_1_E_NUM_ITERATION_MINUS_1                       0xD07A8

#define mmMME_SHADOW_1_E_BUBBLES_PER_SPLIT                           0xD07AC

#define mmMME_SHADOW_2_STATUS                                        0xD0800

#define mmMME_SHADOW_2_A_BASE_ADDR_HIGH                              0xD0808

#define mmMME_SHADOW_2_B_BASE_ADDR_HIGH                              0xD080C

#define mmMME_SHADOW_2_CIN_BASE_ADDR_HIGH                            0xD0810

#define mmMME_SHADOW_2_COUT_BASE_ADDR_HIGH                           0xD0814

#define mmMME_SHADOW_2_BIAS_BASE_ADDR_HIGH                           0xD0818

#define mmMME_SHADOW_2_A_BASE_ADDR_LOW                               0xD081C

#define mmMME_SHADOW_2_B_BASE_ADDR_LOW                               0xD0820

#define mmMME_SHADOW_2_CIN_BASE_ADDR_LOW                             0xD0824

#define mmMME_SHADOW_2_COUT_BASE_ADDR_LOW                            0xD0828

#define mmMME_SHADOW_2_BIAS_BASE_ADDR_LOW                            0xD082C

#define mmMME_SHADOW_2_HEADER                                        0xD0830

#define mmMME_SHADOW_2_KERNEL_SIZE_MINUS_1                           0xD0834

#define mmMME_SHADOW_2_ASSOCIATED_DIMS_0                             0xD0838

#define mmMME_SHADOW_2_ASSOCIATED_DIMS_1                             0xD083C

#define mmMME_SHADOW_2_COUT_SCALE                                    0xD0840

#define mmMME_SHADOW_2_CIN_SCALE                                     0xD0844

#define mmMME_SHADOW_2_GEMMLOWP_ZP                                   0xD0848

#define mmMME_SHADOW_2_GEMMLOWP_EXPONENT                             0xD084C

#define mmMME_SHADOW_2_A_ROI_BASE_OFFSET_0                           0xD0850

#define mmMME_SHADOW_2_A_ROI_BASE_OFFSET_1                           0xD0854

#define mmMME_SHADOW_2_A_ROI_BASE_OFFSET_2                           0xD0858

#define mmMME_SHADOW_2_A_ROI_BASE_OFFSET_3                           0xD085C

#define mmMME_SHADOW_2_A_ROI_BASE_OFFSET_4                           0xD0860

#define mmMME_SHADOW_2_A_VALID_ELEMENTS_0                            0xD0864

#define mmMME_SHADOW_2_A_VALID_ELEMENTS_1                            0xD0868

#define mmMME_SHADOW_2_A_VALID_ELEMENTS_2                            0xD086C

#define mmMME_SHADOW_2_A_VALID_ELEMENTS_3                            0xD0870

#define mmMME_SHADOW_2_A_VALID_ELEMENTS_4                            0xD0874

#define mmMME_SHADOW_2_A_LOOP_STRIDE_0                               0xD0878

#define mmMME_SHADOW_2_A_LOOP_STRIDE_1                               0xD087C

#define mmMME_SHADOW_2_A_LOOP_STRIDE_2                               0xD0880

#define mmMME_SHADOW_2_A_LOOP_STRIDE_3                               0xD0884

#define mmMME_SHADOW_2_A_LOOP_STRIDE_4                               0xD0888

#define mmMME_SHADOW_2_A_ROI_SIZE_0                                  0xD088C

#define mmMME_SHADOW_2_A_ROI_SIZE_1                                  0xD0890

#define mmMME_SHADOW_2_A_ROI_SIZE_2                                  0xD0894

#define mmMME_SHADOW_2_A_ROI_SIZE_3                                  0xD0898

#define mmMME_SHADOW_2_A_SPATIAL_START_OFFSET_0                      0xD089C

#define mmMME_SHADOW_2_A_SPATIAL_START_OFFSET_1                      0xD08A0

#define mmMME_SHADOW_2_A_SPATIAL_START_OFFSET_2                      0xD08A4

#define mmMME_SHADOW_2_A_SPATIAL_START_OFFSET_3                      0xD08A8

#define mmMME_SHADOW_2_A_SPATIAL_STRIDE_0                            0xD08AC

#define mmMME_SHADOW_2_A_SPATIAL_STRIDE_1                            0xD08B0

#define mmMME_SHADOW_2_A_SPATIAL_STRIDE_2                            0xD08B4

#define mmMME_SHADOW_2_A_SPATIAL_STRIDE_3                            0xD08B8

#define mmMME_SHADOW_2_A_SPATIAL_SIZE_MINUS_1                        0xD08BC

#define mmMME_SHADOW_2_B_ROI_BASE_OFFSET_0                           0xD08C0

#define mmMME_SHADOW_2_B_ROI_BASE_OFFSET_1                           0xD08C4

#define mmMME_SHADOW_2_B_ROI_BASE_OFFSET_2                           0xD08C8

#define mmMME_SHADOW_2_B_ROI_BASE_OFFSET_3                           0xD08CC

#define mmMME_SHADOW_2_B_ROI_BASE_OFFSET_4                           0xD08D0

#define mmMME_SHADOW_2_B_VALID_ELEMENTS_0                            0xD08D4

#define mmMME_SHADOW_2_B_VALID_ELEMENTS_1                            0xD08D8

#define mmMME_SHADOW_2_B_VALID_ELEMENTS_2                            0xD08DC

#define mmMME_SHADOW_2_B_VALID_ELEMENTS_3                            0xD08E0

#define mmMME_SHADOW_2_B_VALID_ELEMENTS_4                            0xD08E4

#define mmMME_SHADOW_2_B_LOOP_STRIDE_0                               0xD08E8

#define mmMME_SHADOW_2_B_LOOP_STRIDE_1                               0xD08EC

#define mmMME_SHADOW_2_B_LOOP_STRIDE_2                               0xD08F0

#define mmMME_SHADOW_2_B_LOOP_STRIDE_3                               0xD08F4

#define mmMME_SHADOW_2_B_LOOP_STRIDE_4                               0xD08F8

#define mmMME_SHADOW_2_B_ROI_SIZE_0                                  0xD08FC

#define mmMME_SHADOW_2_B_ROI_SIZE_1                                  0xD0900

#define mmMME_SHADOW_2_B_ROI_SIZE_2                                  0xD0904

#define mmMME_SHADOW_2_B_ROI_SIZE_3                                  0xD0908

#define mmMME_SHADOW_2_B_SPATIAL_START_OFFSET_0                      0xD090C

#define mmMME_SHADOW_2_B_SPATIAL_START_OFFSET_1                      0xD0910

#define mmMME_SHADOW_2_B_SPATIAL_START_OFFSET_2                      0xD0914

#define mmMME_SHADOW_2_B_SPATIAL_START_OFFSET_3                      0xD0918

#define mmMME_SHADOW_2_B_SPATIAL_STRIDE_0                            0xD091C

#define mmMME_SHADOW_2_B_SPATIAL_STRIDE_1                            0xD0920

#define mmMME_SHADOW_2_B_SPATIAL_STRIDE_2                            0xD0924

#define mmMME_SHADOW_2_B_SPATIAL_STRIDE_3                            0xD0928

#define mmMME_SHADOW_2_B_SPATIAL_SIZE_MINUS_1                        0xD092C

#define mmMME_SHADOW_2_C_ROI_BASE_OFFSET_0                           0xD0930

#define mmMME_SHADOW_2_C_ROI_BASE_OFFSET_1                           0xD0934

#define mmMME_SHADOW_2_C_ROI_BASE_OFFSET_2                           0xD0938

#define mmMME_SHADOW_2_C_ROI_BASE_OFFSET_3                           0xD093C

#define mmMME_SHADOW_2_C_ROI_BASE_OFFSET_4                           0xD0940

#define mmMME_SHADOW_2_C_VALID_ELEMENTS_0                            0xD0944

#define mmMME_SHADOW_2_C_VALID_ELEMENTS_1                            0xD0948

#define mmMME_SHADOW_2_C_VALID_ELEMENTS_2                            0xD094C

#define mmMME_SHADOW_2_C_VALID_ELEMENTS_3                            0xD0950

#define mmMME_SHADOW_2_C_VALID_ELEMENTS_4                            0xD0954

#define mmMME_SHADOW_2_C_LOOP_STRIDE_0                               0xD0958

#define mmMME_SHADOW_2_C_LOOP_STRIDE_1                               0xD095C

#define mmMME_SHADOW_2_C_LOOP_STRIDE_2                               0xD0960

#define mmMME_SHADOW_2_C_LOOP_STRIDE_3                               0xD0964

#define mmMME_SHADOW_2_C_LOOP_STRIDE_4                               0xD0968

#define mmMME_SHADOW_2_C_ROI_SIZE_0                                  0xD096C

#define mmMME_SHADOW_2_C_ROI_SIZE_1                                  0xD0970

#define mmMME_SHADOW_2_C_ROI_SIZE_2                                  0xD0974

#define mmMME_SHADOW_2_C_ROI_SIZE_3                                  0xD0978

#define mmMME_SHADOW_2_C_SPATIAL_START_OFFSET_0                      0xD097C

#define mmMME_SHADOW_2_C_SPATIAL_START_OFFSET_1                      0xD0980

#define mmMME_SHADOW_2_C_SPATIAL_START_OFFSET_2                      0xD0984

#define mmMME_SHADOW_2_C_SPATIAL_START_OFFSET_3                      0xD0988

#define mmMME_SHADOW_2_C_SPATIAL_STRIDE_0                            0xD098C

#define mmMME_SHADOW_2_C_SPATIAL_STRIDE_1                            0xD0990

#define mmMME_SHADOW_2_C_SPATIAL_STRIDE_2                            0xD0994

#define mmMME_SHADOW_2_C_SPATIAL_STRIDE_3                            0xD0998

#define mmMME_SHADOW_2_C_SPATIAL_SIZE_MINUS_1                        0xD099C

#define mmMME_SHADOW_2_SYNC_OBJECT_MESSAGE                           0xD09A0

#define mmMME_SHADOW_2_E_PADDING_VALUE_A                             0xD09A4

#define mmMME_SHADOW_2_E_NUM_ITERATION_MINUS_1                       0xD09A8

#define mmMME_SHADOW_2_E_BUBBLES_PER_SPLIT                           0xD09AC

#define mmMME_SHADOW_3_STATUS                                        0xD0A00

#define mmMME_SHADOW_3_A_BASE_ADDR_HIGH                              0xD0A08

#define mmMME_SHADOW_3_B_BASE_ADDR_HIGH                              0xD0A0C

#define mmMME_SHADOW_3_CIN_BASE_ADDR_HIGH                            0xD0A10

#define mmMME_SHADOW_3_COUT_BASE_ADDR_HIGH                           0xD0A14

#define mmMME_SHADOW_3_BIAS_BASE_ADDR_HIGH                           0xD0A18

#define mmMME_SHADOW_3_A_BASE_ADDR_LOW                               0xD0A1C

#define mmMME_SHADOW_3_B_BASE_ADDR_LOW                               0xD0A20

#define mmMME_SHADOW_3_CIN_BASE_ADDR_LOW                             0xD0A24

#define mmMME_SHADOW_3_COUT_BASE_ADDR_LOW                            0xD0A28

#define mmMME_SHADOW_3_BIAS_BASE_ADDR_LOW                            0xD0A2C

#define mmMME_SHADOW_3_HEADER                                        0xD0A30

#define mmMME_SHADOW_3_KERNEL_SIZE_MINUS_1                           0xD0A34

#define mmMME_SHADOW_3_ASSOCIATED_DIMS_0                             0xD0A38

#define mmMME_SHADOW_3_ASSOCIATED_DIMS_1                             0xD0A3C

#define mmMME_SHADOW_3_COUT_SCALE                                    0xD0A40

#define mmMME_SHADOW_3_CIN_SCALE                                     0xD0A44

#define mmMME_SHADOW_3_GEMMLOWP_ZP                                   0xD0A48

#define mmMME_SHADOW_3_GEMMLOWP_EXPONENT                             0xD0A4C

#define mmMME_SHADOW_3_A_ROI_BASE_OFFSET_0                           0xD0A50

#define mmMME_SHADOW_3_A_ROI_BASE_OFFSET_1                           0xD0A54

#define mmMME_SHADOW_3_A_ROI_BASE_OFFSET_2                           0xD0A58

#define mmMME_SHADOW_3_A_ROI_BASE_OFFSET_3                           0xD0A5C

#define mmMME_SHADOW_3_A_ROI_BASE_OFFSET_4                           0xD0A60

#define mmMME_SHADOW_3_A_VALID_ELEMENTS_0                            0xD0A64

#define mmMME_SHADOW_3_A_VALID_ELEMENTS_1                            0xD0A68

#define mmMME_SHADOW_3_A_VALID_ELEMENTS_2                            0xD0A6C

#define mmMME_SHADOW_3_A_VALID_ELEMENTS_3                            0xD0A70

#define mmMME_SHADOW_3_A_VALID_ELEMENTS_4                            0xD0A74

#define mmMME_SHADOW_3_A_LOOP_STRIDE_0                               0xD0A78

#define mmMME_SHADOW_3_A_LOOP_STRIDE_1                               0xD0A7C

#define mmMME_SHADOW_3_A_LOOP_STRIDE_2                               0xD0A80

#define mmMME_SHADOW_3_A_LOOP_STRIDE_3                               0xD0A84

#define mmMME_SHADOW_3_A_LOOP_STRIDE_4                               0xD0A88

#define mmMME_SHADOW_3_A_ROI_SIZE_0                                  0xD0A8C

#define mmMME_SHADOW_3_A_ROI_SIZE_1                                  0xD0A90

#define mmMME_SHADOW_3_A_ROI_SIZE_2                                  0xD0A94

#define mmMME_SHADOW_3_A_ROI_SIZE_3                                  0xD0A98

#define mmMME_SHADOW_3_A_SPATIAL_START_OFFSET_0                      0xD0A9C

#define mmMME_SHADOW_3_A_SPATIAL_START_OFFSET_1                      0xD0AA0

#define mmMME_SHADOW_3_A_SPATIAL_START_OFFSET_2                      0xD0AA4

#define mmMME_SHADOW_3_A_SPATIAL_START_OFFSET_3                      0xD0AA8

#define mmMME_SHADOW_3_A_SPATIAL_STRIDE_0                            0xD0AAC

#define mmMME_SHADOW_3_A_SPATIAL_STRIDE_1                            0xD0AB0

#define mmMME_SHADOW_3_A_SPATIAL_STRIDE_2                            0xD0AB4

#define mmMME_SHADOW_3_A_SPATIAL_STRIDE_3                            0xD0AB8

#define mmMME_SHADOW_3_A_SPATIAL_SIZE_MINUS_1                        0xD0ABC

#define mmMME_SHADOW_3_B_ROI_BASE_OFFSET_0                           0xD0AC0

#define mmMME_SHADOW_3_B_ROI_BASE_OFFSET_1                           0xD0AC4

#define mmMME_SHADOW_3_B_ROI_BASE_OFFSET_2                           0xD0AC8

#define mmMME_SHADOW_3_B_ROI_BASE_OFFSET_3                           0xD0ACC

#define mmMME_SHADOW_3_B_ROI_BASE_OFFSET_4                           0xD0AD0

#define mmMME_SHADOW_3_B_VALID_ELEMENTS_0                            0xD0AD4

#define mmMME_SHADOW_3_B_VALID_ELEMENTS_1                            0xD0AD8

#define mmMME_SHADOW_3_B_VALID_ELEMENTS_2                            0xD0ADC

#define mmMME_SHADOW_3_B_VALID_ELEMENTS_3                            0xD0AE0

#define mmMME_SHADOW_3_B_VALID_ELEMENTS_4                            0xD0AE4

#define mmMME_SHADOW_3_B_LOOP_STRIDE_0                               0xD0AE8

#define mmMME_SHADOW_3_B_LOOP_STRIDE_1                               0xD0AEC

#define mmMME_SHADOW_3_B_LOOP_STRIDE_2                               0xD0AF0

#define mmMME_SHADOW_3_B_LOOP_STRIDE_3                               0xD0AF4

#define mmMME_SHADOW_3_B_LOOP_STRIDE_4                               0xD0AF8

#define mmMME_SHADOW_3_B_ROI_SIZE_0                                  0xD0AFC

#define mmMME_SHADOW_3_B_ROI_SIZE_1                                  0xD0B00

#define mmMME_SHADOW_3_B_ROI_SIZE_2                                  0xD0B04

#define mmMME_SHADOW_3_B_ROI_SIZE_3                                  0xD0B08

#define mmMME_SHADOW_3_B_SPATIAL_START_OFFSET_0                      0xD0B0C

#define mmMME_SHADOW_3_B_SPATIAL_START_OFFSET_1                      0xD0B10

#define mmMME_SHADOW_3_B_SPATIAL_START_OFFSET_2                      0xD0B14

#define mmMME_SHADOW_3_B_SPATIAL_START_OFFSET_3                      0xD0B18

#define mmMME_SHADOW_3_B_SPATIAL_STRIDE_0                            0xD0B1C

#define mmMME_SHADOW_3_B_SPATIAL_STRIDE_1                            0xD0B20

#define mmMME_SHADOW_3_B_SPATIAL_STRIDE_2                            0xD0B24

#define mmMME_SHADOW_3_B_SPATIAL_STRIDE_3                            0xD0B28

#define mmMME_SHADOW_3_B_SPATIAL_SIZE_MINUS_1                        0xD0B2C

#define mmMME_SHADOW_3_C_ROI_BASE_OFFSET_0                           0xD0B30

#define mmMME_SHADOW_3_C_ROI_BASE_OFFSET_1                           0xD0B34

#define mmMME_SHADOW_3_C_ROI_BASE_OFFSET_2                           0xD0B38

#define mmMME_SHADOW_3_C_ROI_BASE_OFFSET_3                           0xD0B3C

#define mmMME_SHADOW_3_C_ROI_BASE_OFFSET_4                           0xD0B40

#define mmMME_SHADOW_3_C_VALID_ELEMENTS_0                            0xD0B44

#define mmMME_SHADOW_3_C_VALID_ELEMENTS_1                            0xD0B48

#define mmMME_SHADOW_3_C_VALID_ELEMENTS_2                            0xD0B4C

#define mmMME_SHADOW_3_C_VALID_ELEMENTS_3                            0xD0B50

#define mmMME_SHADOW_3_C_VALID_ELEMENTS_4                            0xD0B54

#define mmMME_SHADOW_3_C_LOOP_STRIDE_0                               0xD0B58

#define mmMME_SHADOW_3_C_LOOP_STRIDE_1                               0xD0B5C

#define mmMME_SHADOW_3_C_LOOP_STRIDE_2                               0xD0B60

#define mmMME_SHADOW_3_C_LOOP_STRIDE_3                               0xD0B64

#define mmMME_SHADOW_3_C_LOOP_STRIDE_4                               0xD0B68

#define mmMME_SHADOW_3_C_ROI_SIZE_0                                  0xD0B6C

#define mmMME_SHADOW_3_C_ROI_SIZE_1                                  0xD0B70

#define mmMME_SHADOW_3_C_ROI_SIZE_2                                  0xD0B74

#define mmMME_SHADOW_3_C_ROI_SIZE_3                                  0xD0B78

#define mmMME_SHADOW_3_C_SPATIAL_START_OFFSET_0                      0xD0B7C

#define mmMME_SHADOW_3_C_SPATIAL_START_OFFSET_1                      0xD0B80

#define mmMME_SHADOW_3_C_SPATIAL_START_OFFSET_2                      0xD0B84

#define mmMME_SHADOW_3_C_SPATIAL_START_OFFSET_3                      0xD0B88

#define mmMME_SHADOW_3_C_SPATIAL_STRIDE_0                            0xD0B8C

#define mmMME_SHADOW_3_C_SPATIAL_STRIDE_1                            0xD0B90

#define mmMME_SHADOW_3_C_SPATIAL_STRIDE_2                            0xD0B94

#define mmMME_SHADOW_3_C_SPATIAL_STRIDE_3                            0xD0B98

#define mmMME_SHADOW_3_C_SPATIAL_SIZE_MINUS_1                        0xD0B9C

#define mmMME_SHADOW_3_SYNC_OBJECT_MESSAGE                           0xD0BA0

#define mmMME_SHADOW_3_E_PADDING_VALUE_A                             0xD0BA4

#define mmMME_SHADOW_3_E_NUM_ITERATION_MINUS_1                       0xD0BA8

#define mmMME_SHADOW_3_E_BUBBLES_PER_SPLIT                           0xD0BAC

#endif /* ASIC_REG_MME_REGS_H_ */
