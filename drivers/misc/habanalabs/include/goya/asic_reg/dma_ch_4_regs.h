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

#ifndef ASIC_REG_DMA_CH_4_REGS_H_
#define ASIC_REG_DMA_CH_4_REGS_H_

/*
 *****************************************
 *   DMA_CH_4 (Prototype: DMA_CH)
 *****************************************
 */

#define mmDMA_CH_4_CFG0                                              0x421000

#define mmDMA_CH_4_CFG1                                              0x421004

#define mmDMA_CH_4_ERRMSG_ADDR_LO                                    0x421008

#define mmDMA_CH_4_ERRMSG_ADDR_HI                                    0x42100C

#define mmDMA_CH_4_ERRMSG_WDATA                                      0x421010

#define mmDMA_CH_4_RD_COMP_ADDR_LO                                   0x421014

#define mmDMA_CH_4_RD_COMP_ADDR_HI                                   0x421018

#define mmDMA_CH_4_RD_COMP_WDATA                                     0x42101C

#define mmDMA_CH_4_WR_COMP_ADDR_LO                                   0x421020

#define mmDMA_CH_4_WR_COMP_ADDR_HI                                   0x421024

#define mmDMA_CH_4_WR_COMP_WDATA                                     0x421028

#define mmDMA_CH_4_LDMA_SRC_ADDR_LO                                  0x42102C

#define mmDMA_CH_4_LDMA_SRC_ADDR_HI                                  0x421030

#define mmDMA_CH_4_LDMA_DST_ADDR_LO                                  0x421034

#define mmDMA_CH_4_LDMA_DST_ADDR_HI                                  0x421038

#define mmDMA_CH_4_LDMA_TSIZE                                        0x42103C

#define mmDMA_CH_4_COMIT_TRANSFER                                    0x421040

#define mmDMA_CH_4_STS0                                              0x421044

#define mmDMA_CH_4_STS1                                              0x421048

#define mmDMA_CH_4_STS2                                              0x42104C

#define mmDMA_CH_4_STS3                                              0x421050

#define mmDMA_CH_4_STS4                                              0x421054

#define mmDMA_CH_4_SRC_ADDR_LO_STS                                   0x421058

#define mmDMA_CH_4_SRC_ADDR_HI_STS                                   0x42105C

#define mmDMA_CH_4_SRC_TSIZE_STS                                     0x421060

#define mmDMA_CH_4_DST_ADDR_LO_STS                                   0x421064

#define mmDMA_CH_4_DST_ADDR_HI_STS                                   0x421068

#define mmDMA_CH_4_DST_TSIZE_STS                                     0x42106C

#define mmDMA_CH_4_RD_RATE_LIM_EN                                    0x421070

#define mmDMA_CH_4_RD_RATE_LIM_RST_TOKEN                             0x421074

#define mmDMA_CH_4_RD_RATE_LIM_SAT                                   0x421078

#define mmDMA_CH_4_RD_RATE_LIM_TOUT                                  0x42107C

#define mmDMA_CH_4_WR_RATE_LIM_EN                                    0x421080

#define mmDMA_CH_4_WR_RATE_LIM_RST_TOKEN                             0x421084

#define mmDMA_CH_4_WR_RATE_LIM_SAT                                   0x421088

#define mmDMA_CH_4_WR_RATE_LIM_TOUT                                  0x42108C

#define mmDMA_CH_4_CFG2                                              0x421090

#define mmDMA_CH_4_TDMA_CTL                                          0x421100

#define mmDMA_CH_4_TDMA_SRC_BASE_ADDR_LO                             0x421104

#define mmDMA_CH_4_TDMA_SRC_BASE_ADDR_HI                             0x421108

#define mmDMA_CH_4_TDMA_SRC_ROI_BASE_0                               0x42110C

#define mmDMA_CH_4_TDMA_SRC_ROI_SIZE_0                               0x421110

#define mmDMA_CH_4_TDMA_SRC_VALID_ELEMENTS_0                         0x421114

#define mmDMA_CH_4_TDMA_SRC_START_OFFSET_0                           0x421118

#define mmDMA_CH_4_TDMA_SRC_STRIDE_0                                 0x42111C

#define mmDMA_CH_4_TDMA_SRC_ROI_BASE_1                               0x421120

#define mmDMA_CH_4_TDMA_SRC_ROI_SIZE_1                               0x421124

#define mmDMA_CH_4_TDMA_SRC_VALID_ELEMENTS_1                         0x421128

#define mmDMA_CH_4_TDMA_SRC_START_OFFSET_1                           0x42112C

#define mmDMA_CH_4_TDMA_SRC_STRIDE_1                                 0x421130

#define mmDMA_CH_4_TDMA_SRC_ROI_BASE_2                               0x421134

#define mmDMA_CH_4_TDMA_SRC_ROI_SIZE_2                               0x421138

#define mmDMA_CH_4_TDMA_SRC_VALID_ELEMENTS_2                         0x42113C

#define mmDMA_CH_4_TDMA_SRC_START_OFFSET_2                           0x421140

#define mmDMA_CH_4_TDMA_SRC_STRIDE_2                                 0x421144

#define mmDMA_CH_4_TDMA_SRC_ROI_BASE_3                               0x421148

#define mmDMA_CH_4_TDMA_SRC_ROI_SIZE_3                               0x42114C

#define mmDMA_CH_4_TDMA_SRC_VALID_ELEMENTS_3                         0x421150

#define mmDMA_CH_4_TDMA_SRC_START_OFFSET_3                           0x421154

#define mmDMA_CH_4_TDMA_SRC_STRIDE_3                                 0x421158

#define mmDMA_CH_4_TDMA_SRC_ROI_BASE_4                               0x42115C

#define mmDMA_CH_4_TDMA_SRC_ROI_SIZE_4                               0x421160

#define mmDMA_CH_4_TDMA_SRC_VALID_ELEMENTS_4                         0x421164

#define mmDMA_CH_4_TDMA_SRC_START_OFFSET_4                           0x421168

#define mmDMA_CH_4_TDMA_SRC_STRIDE_4                                 0x42116C

#define mmDMA_CH_4_TDMA_DST_BASE_ADDR_LO                             0x421170

#define mmDMA_CH_4_TDMA_DST_BASE_ADDR_HI                             0x421174

#define mmDMA_CH_4_TDMA_DST_ROI_BASE_0                               0x421178

#define mmDMA_CH_4_TDMA_DST_ROI_SIZE_0                               0x42117C

#define mmDMA_CH_4_TDMA_DST_VALID_ELEMENTS_0                         0x421180

#define mmDMA_CH_4_TDMA_DST_START_OFFSET_0                           0x421184

#define mmDMA_CH_4_TDMA_DST_STRIDE_0                                 0x421188

#define mmDMA_CH_4_TDMA_DST_ROI_BASE_1                               0x42118C

#define mmDMA_CH_4_TDMA_DST_ROI_SIZE_1                               0x421190

#define mmDMA_CH_4_TDMA_DST_VALID_ELEMENTS_1                         0x421194

#define mmDMA_CH_4_TDMA_DST_START_OFFSET_1                           0x421198

#define mmDMA_CH_4_TDMA_DST_STRIDE_1                                 0x42119C

#define mmDMA_CH_4_TDMA_DST_ROI_BASE_2                               0x4211A0

#define mmDMA_CH_4_TDMA_DST_ROI_SIZE_2                               0x4211A4

#define mmDMA_CH_4_TDMA_DST_VALID_ELEMENTS_2                         0x4211A8

#define mmDMA_CH_4_TDMA_DST_START_OFFSET_2                           0x4211AC

#define mmDMA_CH_4_TDMA_DST_STRIDE_2                                 0x4211B0

#define mmDMA_CH_4_TDMA_DST_ROI_BASE_3                               0x4211B4

#define mmDMA_CH_4_TDMA_DST_ROI_SIZE_3                               0x4211B8

#define mmDMA_CH_4_TDMA_DST_VALID_ELEMENTS_3                         0x4211BC

#define mmDMA_CH_4_TDMA_DST_START_OFFSET_3                           0x4211C0

#define mmDMA_CH_4_TDMA_DST_STRIDE_3                                 0x4211C4

#define mmDMA_CH_4_TDMA_DST_ROI_BASE_4                               0x4211C8

#define mmDMA_CH_4_TDMA_DST_ROI_SIZE_4                               0x4211CC

#define mmDMA_CH_4_TDMA_DST_VALID_ELEMENTS_4                         0x4211D0

#define mmDMA_CH_4_TDMA_DST_START_OFFSET_4                           0x4211D4

#define mmDMA_CH_4_TDMA_DST_STRIDE_4                                 0x4211D8

#define mmDMA_CH_4_MEM_INIT_BUSY                                     0x4211FC

#endif /* ASIC_REG_DMA_CH_4_REGS_H_ */

