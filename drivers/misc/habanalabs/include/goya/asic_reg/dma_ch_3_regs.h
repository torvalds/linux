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

#ifndef ASIC_REG_DMA_CH_3_REGS_H_
#define ASIC_REG_DMA_CH_3_REGS_H_

/*
 *****************************************
 *   DMA_CH_3 (Prototype: DMA_CH)
 *****************************************
 */

#define mmDMA_CH_3_CFG0                                              0x419000

#define mmDMA_CH_3_CFG1                                              0x419004

#define mmDMA_CH_3_ERRMSG_ADDR_LO                                    0x419008

#define mmDMA_CH_3_ERRMSG_ADDR_HI                                    0x41900C

#define mmDMA_CH_3_ERRMSG_WDATA                                      0x419010

#define mmDMA_CH_3_RD_COMP_ADDR_LO                                   0x419014

#define mmDMA_CH_3_RD_COMP_ADDR_HI                                   0x419018

#define mmDMA_CH_3_RD_COMP_WDATA                                     0x41901C

#define mmDMA_CH_3_WR_COMP_ADDR_LO                                   0x419020

#define mmDMA_CH_3_WR_COMP_ADDR_HI                                   0x419024

#define mmDMA_CH_3_WR_COMP_WDATA                                     0x419028

#define mmDMA_CH_3_LDMA_SRC_ADDR_LO                                  0x41902C

#define mmDMA_CH_3_LDMA_SRC_ADDR_HI                                  0x419030

#define mmDMA_CH_3_LDMA_DST_ADDR_LO                                  0x419034

#define mmDMA_CH_3_LDMA_DST_ADDR_HI                                  0x419038

#define mmDMA_CH_3_LDMA_TSIZE                                        0x41903C

#define mmDMA_CH_3_COMIT_TRANSFER                                    0x419040

#define mmDMA_CH_3_STS0                                              0x419044

#define mmDMA_CH_3_STS1                                              0x419048

#define mmDMA_CH_3_STS2                                              0x41904C

#define mmDMA_CH_3_STS3                                              0x419050

#define mmDMA_CH_3_STS4                                              0x419054

#define mmDMA_CH_3_SRC_ADDR_LO_STS                                   0x419058

#define mmDMA_CH_3_SRC_ADDR_HI_STS                                   0x41905C

#define mmDMA_CH_3_SRC_TSIZE_STS                                     0x419060

#define mmDMA_CH_3_DST_ADDR_LO_STS                                   0x419064

#define mmDMA_CH_3_DST_ADDR_HI_STS                                   0x419068

#define mmDMA_CH_3_DST_TSIZE_STS                                     0x41906C

#define mmDMA_CH_3_RD_RATE_LIM_EN                                    0x419070

#define mmDMA_CH_3_RD_RATE_LIM_RST_TOKEN                             0x419074

#define mmDMA_CH_3_RD_RATE_LIM_SAT                                   0x419078

#define mmDMA_CH_3_RD_RATE_LIM_TOUT                                  0x41907C

#define mmDMA_CH_3_WR_RATE_LIM_EN                                    0x419080

#define mmDMA_CH_3_WR_RATE_LIM_RST_TOKEN                             0x419084

#define mmDMA_CH_3_WR_RATE_LIM_SAT                                   0x419088

#define mmDMA_CH_3_WR_RATE_LIM_TOUT                                  0x41908C

#define mmDMA_CH_3_CFG2                                              0x419090

#define mmDMA_CH_3_TDMA_CTL                                          0x419100

#define mmDMA_CH_3_TDMA_SRC_BASE_ADDR_LO                             0x419104

#define mmDMA_CH_3_TDMA_SRC_BASE_ADDR_HI                             0x419108

#define mmDMA_CH_3_TDMA_SRC_ROI_BASE_0                               0x41910C

#define mmDMA_CH_3_TDMA_SRC_ROI_SIZE_0                               0x419110

#define mmDMA_CH_3_TDMA_SRC_VALID_ELEMENTS_0                         0x419114

#define mmDMA_CH_3_TDMA_SRC_START_OFFSET_0                           0x419118

#define mmDMA_CH_3_TDMA_SRC_STRIDE_0                                 0x41911C

#define mmDMA_CH_3_TDMA_SRC_ROI_BASE_1                               0x419120

#define mmDMA_CH_3_TDMA_SRC_ROI_SIZE_1                               0x419124

#define mmDMA_CH_3_TDMA_SRC_VALID_ELEMENTS_1                         0x419128

#define mmDMA_CH_3_TDMA_SRC_START_OFFSET_1                           0x41912C

#define mmDMA_CH_3_TDMA_SRC_STRIDE_1                                 0x419130

#define mmDMA_CH_3_TDMA_SRC_ROI_BASE_2                               0x419134

#define mmDMA_CH_3_TDMA_SRC_ROI_SIZE_2                               0x419138

#define mmDMA_CH_3_TDMA_SRC_VALID_ELEMENTS_2                         0x41913C

#define mmDMA_CH_3_TDMA_SRC_START_OFFSET_2                           0x419140

#define mmDMA_CH_3_TDMA_SRC_STRIDE_2                                 0x419144

#define mmDMA_CH_3_TDMA_SRC_ROI_BASE_3                               0x419148

#define mmDMA_CH_3_TDMA_SRC_ROI_SIZE_3                               0x41914C

#define mmDMA_CH_3_TDMA_SRC_VALID_ELEMENTS_3                         0x419150

#define mmDMA_CH_3_TDMA_SRC_START_OFFSET_3                           0x419154

#define mmDMA_CH_3_TDMA_SRC_STRIDE_3                                 0x419158

#define mmDMA_CH_3_TDMA_SRC_ROI_BASE_4                               0x41915C

#define mmDMA_CH_3_TDMA_SRC_ROI_SIZE_4                               0x419160

#define mmDMA_CH_3_TDMA_SRC_VALID_ELEMENTS_4                         0x419164

#define mmDMA_CH_3_TDMA_SRC_START_OFFSET_4                           0x419168

#define mmDMA_CH_3_TDMA_SRC_STRIDE_4                                 0x41916C

#define mmDMA_CH_3_TDMA_DST_BASE_ADDR_LO                             0x419170

#define mmDMA_CH_3_TDMA_DST_BASE_ADDR_HI                             0x419174

#define mmDMA_CH_3_TDMA_DST_ROI_BASE_0                               0x419178

#define mmDMA_CH_3_TDMA_DST_ROI_SIZE_0                               0x41917C

#define mmDMA_CH_3_TDMA_DST_VALID_ELEMENTS_0                         0x419180

#define mmDMA_CH_3_TDMA_DST_START_OFFSET_0                           0x419184

#define mmDMA_CH_3_TDMA_DST_STRIDE_0                                 0x419188

#define mmDMA_CH_3_TDMA_DST_ROI_BASE_1                               0x41918C

#define mmDMA_CH_3_TDMA_DST_ROI_SIZE_1                               0x419190

#define mmDMA_CH_3_TDMA_DST_VALID_ELEMENTS_1                         0x419194

#define mmDMA_CH_3_TDMA_DST_START_OFFSET_1                           0x419198

#define mmDMA_CH_3_TDMA_DST_STRIDE_1                                 0x41919C

#define mmDMA_CH_3_TDMA_DST_ROI_BASE_2                               0x4191A0

#define mmDMA_CH_3_TDMA_DST_ROI_SIZE_2                               0x4191A4

#define mmDMA_CH_3_TDMA_DST_VALID_ELEMENTS_2                         0x4191A8

#define mmDMA_CH_3_TDMA_DST_START_OFFSET_2                           0x4191AC

#define mmDMA_CH_3_TDMA_DST_STRIDE_2                                 0x4191B0

#define mmDMA_CH_3_TDMA_DST_ROI_BASE_3                               0x4191B4

#define mmDMA_CH_3_TDMA_DST_ROI_SIZE_3                               0x4191B8

#define mmDMA_CH_3_TDMA_DST_VALID_ELEMENTS_3                         0x4191BC

#define mmDMA_CH_3_TDMA_DST_START_OFFSET_3                           0x4191C0

#define mmDMA_CH_3_TDMA_DST_STRIDE_3                                 0x4191C4

#define mmDMA_CH_3_TDMA_DST_ROI_BASE_4                               0x4191C8

#define mmDMA_CH_3_TDMA_DST_ROI_SIZE_4                               0x4191CC

#define mmDMA_CH_3_TDMA_DST_VALID_ELEMENTS_4                         0x4191D0

#define mmDMA_CH_3_TDMA_DST_START_OFFSET_4                           0x4191D4

#define mmDMA_CH_3_TDMA_DST_STRIDE_4                                 0x4191D8

#define mmDMA_CH_3_MEM_INIT_BUSY                                     0x4191FC

#endif /* ASIC_REG_DMA_CH_3_REGS_H_ */

