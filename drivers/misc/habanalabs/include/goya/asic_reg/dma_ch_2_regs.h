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

#ifndef ASIC_REG_DMA_CH_2_REGS_H_
#define ASIC_REG_DMA_CH_2_REGS_H_

/*
 *****************************************
 *   DMA_CH_2 (Prototype: DMA_CH)
 *****************************************
 */

#define mmDMA_CH_2_CFG0                                              0x411000

#define mmDMA_CH_2_CFG1                                              0x411004

#define mmDMA_CH_2_ERRMSG_ADDR_LO                                    0x411008

#define mmDMA_CH_2_ERRMSG_ADDR_HI                                    0x41100C

#define mmDMA_CH_2_ERRMSG_WDATA                                      0x411010

#define mmDMA_CH_2_RD_COMP_ADDR_LO                                   0x411014

#define mmDMA_CH_2_RD_COMP_ADDR_HI                                   0x411018

#define mmDMA_CH_2_RD_COMP_WDATA                                     0x41101C

#define mmDMA_CH_2_WR_COMP_ADDR_LO                                   0x411020

#define mmDMA_CH_2_WR_COMP_ADDR_HI                                   0x411024

#define mmDMA_CH_2_WR_COMP_WDATA                                     0x411028

#define mmDMA_CH_2_LDMA_SRC_ADDR_LO                                  0x41102C

#define mmDMA_CH_2_LDMA_SRC_ADDR_HI                                  0x411030

#define mmDMA_CH_2_LDMA_DST_ADDR_LO                                  0x411034

#define mmDMA_CH_2_LDMA_DST_ADDR_HI                                  0x411038

#define mmDMA_CH_2_LDMA_TSIZE                                        0x41103C

#define mmDMA_CH_2_COMIT_TRANSFER                                    0x411040

#define mmDMA_CH_2_STS0                                              0x411044

#define mmDMA_CH_2_STS1                                              0x411048

#define mmDMA_CH_2_STS2                                              0x41104C

#define mmDMA_CH_2_STS3                                              0x411050

#define mmDMA_CH_2_STS4                                              0x411054

#define mmDMA_CH_2_SRC_ADDR_LO_STS                                   0x411058

#define mmDMA_CH_2_SRC_ADDR_HI_STS                                   0x41105C

#define mmDMA_CH_2_SRC_TSIZE_STS                                     0x411060

#define mmDMA_CH_2_DST_ADDR_LO_STS                                   0x411064

#define mmDMA_CH_2_DST_ADDR_HI_STS                                   0x411068

#define mmDMA_CH_2_DST_TSIZE_STS                                     0x41106C

#define mmDMA_CH_2_RD_RATE_LIM_EN                                    0x411070

#define mmDMA_CH_2_RD_RATE_LIM_RST_TOKEN                             0x411074

#define mmDMA_CH_2_RD_RATE_LIM_SAT                                   0x411078

#define mmDMA_CH_2_RD_RATE_LIM_TOUT                                  0x41107C

#define mmDMA_CH_2_WR_RATE_LIM_EN                                    0x411080

#define mmDMA_CH_2_WR_RATE_LIM_RST_TOKEN                             0x411084

#define mmDMA_CH_2_WR_RATE_LIM_SAT                                   0x411088

#define mmDMA_CH_2_WR_RATE_LIM_TOUT                                  0x41108C

#define mmDMA_CH_2_CFG2                                              0x411090

#define mmDMA_CH_2_TDMA_CTL                                          0x411100

#define mmDMA_CH_2_TDMA_SRC_BASE_ADDR_LO                             0x411104

#define mmDMA_CH_2_TDMA_SRC_BASE_ADDR_HI                             0x411108

#define mmDMA_CH_2_TDMA_SRC_ROI_BASE_0                               0x41110C

#define mmDMA_CH_2_TDMA_SRC_ROI_SIZE_0                               0x411110

#define mmDMA_CH_2_TDMA_SRC_VALID_ELEMENTS_0                         0x411114

#define mmDMA_CH_2_TDMA_SRC_START_OFFSET_0                           0x411118

#define mmDMA_CH_2_TDMA_SRC_STRIDE_0                                 0x41111C

#define mmDMA_CH_2_TDMA_SRC_ROI_BASE_1                               0x411120

#define mmDMA_CH_2_TDMA_SRC_ROI_SIZE_1                               0x411124

#define mmDMA_CH_2_TDMA_SRC_VALID_ELEMENTS_1                         0x411128

#define mmDMA_CH_2_TDMA_SRC_START_OFFSET_1                           0x41112C

#define mmDMA_CH_2_TDMA_SRC_STRIDE_1                                 0x411130

#define mmDMA_CH_2_TDMA_SRC_ROI_BASE_2                               0x411134

#define mmDMA_CH_2_TDMA_SRC_ROI_SIZE_2                               0x411138

#define mmDMA_CH_2_TDMA_SRC_VALID_ELEMENTS_2                         0x41113C

#define mmDMA_CH_2_TDMA_SRC_START_OFFSET_2                           0x411140

#define mmDMA_CH_2_TDMA_SRC_STRIDE_2                                 0x411144

#define mmDMA_CH_2_TDMA_SRC_ROI_BASE_3                               0x411148

#define mmDMA_CH_2_TDMA_SRC_ROI_SIZE_3                               0x41114C

#define mmDMA_CH_2_TDMA_SRC_VALID_ELEMENTS_3                         0x411150

#define mmDMA_CH_2_TDMA_SRC_START_OFFSET_3                           0x411154

#define mmDMA_CH_2_TDMA_SRC_STRIDE_3                                 0x411158

#define mmDMA_CH_2_TDMA_SRC_ROI_BASE_4                               0x41115C

#define mmDMA_CH_2_TDMA_SRC_ROI_SIZE_4                               0x411160

#define mmDMA_CH_2_TDMA_SRC_VALID_ELEMENTS_4                         0x411164

#define mmDMA_CH_2_TDMA_SRC_START_OFFSET_4                           0x411168

#define mmDMA_CH_2_TDMA_SRC_STRIDE_4                                 0x41116C

#define mmDMA_CH_2_TDMA_DST_BASE_ADDR_LO                             0x411170

#define mmDMA_CH_2_TDMA_DST_BASE_ADDR_HI                             0x411174

#define mmDMA_CH_2_TDMA_DST_ROI_BASE_0                               0x411178

#define mmDMA_CH_2_TDMA_DST_ROI_SIZE_0                               0x41117C

#define mmDMA_CH_2_TDMA_DST_VALID_ELEMENTS_0                         0x411180

#define mmDMA_CH_2_TDMA_DST_START_OFFSET_0                           0x411184

#define mmDMA_CH_2_TDMA_DST_STRIDE_0                                 0x411188

#define mmDMA_CH_2_TDMA_DST_ROI_BASE_1                               0x41118C

#define mmDMA_CH_2_TDMA_DST_ROI_SIZE_1                               0x411190

#define mmDMA_CH_2_TDMA_DST_VALID_ELEMENTS_1                         0x411194

#define mmDMA_CH_2_TDMA_DST_START_OFFSET_1                           0x411198

#define mmDMA_CH_2_TDMA_DST_STRIDE_1                                 0x41119C

#define mmDMA_CH_2_TDMA_DST_ROI_BASE_2                               0x4111A0

#define mmDMA_CH_2_TDMA_DST_ROI_SIZE_2                               0x4111A4

#define mmDMA_CH_2_TDMA_DST_VALID_ELEMENTS_2                         0x4111A8

#define mmDMA_CH_2_TDMA_DST_START_OFFSET_2                           0x4111AC

#define mmDMA_CH_2_TDMA_DST_STRIDE_2                                 0x4111B0

#define mmDMA_CH_2_TDMA_DST_ROI_BASE_3                               0x4111B4

#define mmDMA_CH_2_TDMA_DST_ROI_SIZE_3                               0x4111B8

#define mmDMA_CH_2_TDMA_DST_VALID_ELEMENTS_3                         0x4111BC

#define mmDMA_CH_2_TDMA_DST_START_OFFSET_3                           0x4111C0

#define mmDMA_CH_2_TDMA_DST_STRIDE_3                                 0x4111C4

#define mmDMA_CH_2_TDMA_DST_ROI_BASE_4                               0x4111C8

#define mmDMA_CH_2_TDMA_DST_ROI_SIZE_4                               0x4111CC

#define mmDMA_CH_2_TDMA_DST_VALID_ELEMENTS_4                         0x4111D0

#define mmDMA_CH_2_TDMA_DST_START_OFFSET_4                           0x4111D4

#define mmDMA_CH_2_TDMA_DST_STRIDE_4                                 0x4111D8

#define mmDMA_CH_2_MEM_INIT_BUSY                                     0x4111FC

#endif /* ASIC_REG_DMA_CH_2_REGS_H_ */

