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

#ifndef ASIC_REG_DMA_CH_1_REGS_H_
#define ASIC_REG_DMA_CH_1_REGS_H_

/*
 *****************************************
 *   DMA_CH_1 (Prototype: DMA_CH)
 *****************************************
 */

#define mmDMA_CH_1_CFG0                                              0x409000

#define mmDMA_CH_1_CFG1                                              0x409004

#define mmDMA_CH_1_ERRMSG_ADDR_LO                                    0x409008

#define mmDMA_CH_1_ERRMSG_ADDR_HI                                    0x40900C

#define mmDMA_CH_1_ERRMSG_WDATA                                      0x409010

#define mmDMA_CH_1_RD_COMP_ADDR_LO                                   0x409014

#define mmDMA_CH_1_RD_COMP_ADDR_HI                                   0x409018

#define mmDMA_CH_1_RD_COMP_WDATA                                     0x40901C

#define mmDMA_CH_1_WR_COMP_ADDR_LO                                   0x409020

#define mmDMA_CH_1_WR_COMP_ADDR_HI                                   0x409024

#define mmDMA_CH_1_WR_COMP_WDATA                                     0x409028

#define mmDMA_CH_1_LDMA_SRC_ADDR_LO                                  0x40902C

#define mmDMA_CH_1_LDMA_SRC_ADDR_HI                                  0x409030

#define mmDMA_CH_1_LDMA_DST_ADDR_LO                                  0x409034

#define mmDMA_CH_1_LDMA_DST_ADDR_HI                                  0x409038

#define mmDMA_CH_1_LDMA_TSIZE                                        0x40903C

#define mmDMA_CH_1_COMIT_TRANSFER                                    0x409040

#define mmDMA_CH_1_STS0                                              0x409044

#define mmDMA_CH_1_STS1                                              0x409048

#define mmDMA_CH_1_STS2                                              0x40904C

#define mmDMA_CH_1_STS3                                              0x409050

#define mmDMA_CH_1_STS4                                              0x409054

#define mmDMA_CH_1_SRC_ADDR_LO_STS                                   0x409058

#define mmDMA_CH_1_SRC_ADDR_HI_STS                                   0x40905C

#define mmDMA_CH_1_SRC_TSIZE_STS                                     0x409060

#define mmDMA_CH_1_DST_ADDR_LO_STS                                   0x409064

#define mmDMA_CH_1_DST_ADDR_HI_STS                                   0x409068

#define mmDMA_CH_1_DST_TSIZE_STS                                     0x40906C

#define mmDMA_CH_1_RD_RATE_LIM_EN                                    0x409070

#define mmDMA_CH_1_RD_RATE_LIM_RST_TOKEN                             0x409074

#define mmDMA_CH_1_RD_RATE_LIM_SAT                                   0x409078

#define mmDMA_CH_1_RD_RATE_LIM_TOUT                                  0x40907C

#define mmDMA_CH_1_WR_RATE_LIM_EN                                    0x409080

#define mmDMA_CH_1_WR_RATE_LIM_RST_TOKEN                             0x409084

#define mmDMA_CH_1_WR_RATE_LIM_SAT                                   0x409088

#define mmDMA_CH_1_WR_RATE_LIM_TOUT                                  0x40908C

#define mmDMA_CH_1_CFG2                                              0x409090

#define mmDMA_CH_1_TDMA_CTL                                          0x409100

#define mmDMA_CH_1_TDMA_SRC_BASE_ADDR_LO                             0x409104

#define mmDMA_CH_1_TDMA_SRC_BASE_ADDR_HI                             0x409108

#define mmDMA_CH_1_TDMA_SRC_ROI_BASE_0                               0x40910C

#define mmDMA_CH_1_TDMA_SRC_ROI_SIZE_0                               0x409110

#define mmDMA_CH_1_TDMA_SRC_VALID_ELEMENTS_0                         0x409114

#define mmDMA_CH_1_TDMA_SRC_START_OFFSET_0                           0x409118

#define mmDMA_CH_1_TDMA_SRC_STRIDE_0                                 0x40911C

#define mmDMA_CH_1_TDMA_SRC_ROI_BASE_1                               0x409120

#define mmDMA_CH_1_TDMA_SRC_ROI_SIZE_1                               0x409124

#define mmDMA_CH_1_TDMA_SRC_VALID_ELEMENTS_1                         0x409128

#define mmDMA_CH_1_TDMA_SRC_START_OFFSET_1                           0x40912C

#define mmDMA_CH_1_TDMA_SRC_STRIDE_1                                 0x409130

#define mmDMA_CH_1_TDMA_SRC_ROI_BASE_2                               0x409134

#define mmDMA_CH_1_TDMA_SRC_ROI_SIZE_2                               0x409138

#define mmDMA_CH_1_TDMA_SRC_VALID_ELEMENTS_2                         0x40913C

#define mmDMA_CH_1_TDMA_SRC_START_OFFSET_2                           0x409140

#define mmDMA_CH_1_TDMA_SRC_STRIDE_2                                 0x409144

#define mmDMA_CH_1_TDMA_SRC_ROI_BASE_3                               0x409148

#define mmDMA_CH_1_TDMA_SRC_ROI_SIZE_3                               0x40914C

#define mmDMA_CH_1_TDMA_SRC_VALID_ELEMENTS_3                         0x409150

#define mmDMA_CH_1_TDMA_SRC_START_OFFSET_3                           0x409154

#define mmDMA_CH_1_TDMA_SRC_STRIDE_3                                 0x409158

#define mmDMA_CH_1_TDMA_SRC_ROI_BASE_4                               0x40915C

#define mmDMA_CH_1_TDMA_SRC_ROI_SIZE_4                               0x409160

#define mmDMA_CH_1_TDMA_SRC_VALID_ELEMENTS_4                         0x409164

#define mmDMA_CH_1_TDMA_SRC_START_OFFSET_4                           0x409168

#define mmDMA_CH_1_TDMA_SRC_STRIDE_4                                 0x40916C

#define mmDMA_CH_1_TDMA_DST_BASE_ADDR_LO                             0x409170

#define mmDMA_CH_1_TDMA_DST_BASE_ADDR_HI                             0x409174

#define mmDMA_CH_1_TDMA_DST_ROI_BASE_0                               0x409178

#define mmDMA_CH_1_TDMA_DST_ROI_SIZE_0                               0x40917C

#define mmDMA_CH_1_TDMA_DST_VALID_ELEMENTS_0                         0x409180

#define mmDMA_CH_1_TDMA_DST_START_OFFSET_0                           0x409184

#define mmDMA_CH_1_TDMA_DST_STRIDE_0                                 0x409188

#define mmDMA_CH_1_TDMA_DST_ROI_BASE_1                               0x40918C

#define mmDMA_CH_1_TDMA_DST_ROI_SIZE_1                               0x409190

#define mmDMA_CH_1_TDMA_DST_VALID_ELEMENTS_1                         0x409194

#define mmDMA_CH_1_TDMA_DST_START_OFFSET_1                           0x409198

#define mmDMA_CH_1_TDMA_DST_STRIDE_1                                 0x40919C

#define mmDMA_CH_1_TDMA_DST_ROI_BASE_2                               0x4091A0

#define mmDMA_CH_1_TDMA_DST_ROI_SIZE_2                               0x4091A4

#define mmDMA_CH_1_TDMA_DST_VALID_ELEMENTS_2                         0x4091A8

#define mmDMA_CH_1_TDMA_DST_START_OFFSET_2                           0x4091AC

#define mmDMA_CH_1_TDMA_DST_STRIDE_2                                 0x4091B0

#define mmDMA_CH_1_TDMA_DST_ROI_BASE_3                               0x4091B4

#define mmDMA_CH_1_TDMA_DST_ROI_SIZE_3                               0x4091B8

#define mmDMA_CH_1_TDMA_DST_VALID_ELEMENTS_3                         0x4091BC

#define mmDMA_CH_1_TDMA_DST_START_OFFSET_3                           0x4091C0

#define mmDMA_CH_1_TDMA_DST_STRIDE_3                                 0x4091C4

#define mmDMA_CH_1_TDMA_DST_ROI_BASE_4                               0x4091C8

#define mmDMA_CH_1_TDMA_DST_ROI_SIZE_4                               0x4091CC

#define mmDMA_CH_1_TDMA_DST_VALID_ELEMENTS_4                         0x4091D0

#define mmDMA_CH_1_TDMA_DST_START_OFFSET_4                           0x4091D4

#define mmDMA_CH_1_TDMA_DST_STRIDE_4                                 0x4091D8

#define mmDMA_CH_1_MEM_INIT_BUSY                                     0x4091FC

#endif /* ASIC_REG_DMA_CH_1_REGS_H_ */

