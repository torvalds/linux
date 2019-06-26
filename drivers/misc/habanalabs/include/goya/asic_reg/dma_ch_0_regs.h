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

#ifndef ASIC_REG_DMA_CH_0_REGS_H_
#define ASIC_REG_DMA_CH_0_REGS_H_

/*
 *****************************************
 *   DMA_CH_0 (Prototype: DMA_CH)
 *****************************************
 */

#define mmDMA_CH_0_CFG0                                              0x401000

#define mmDMA_CH_0_CFG1                                              0x401004

#define mmDMA_CH_0_ERRMSG_ADDR_LO                                    0x401008

#define mmDMA_CH_0_ERRMSG_ADDR_HI                                    0x40100C

#define mmDMA_CH_0_ERRMSG_WDATA                                      0x401010

#define mmDMA_CH_0_RD_COMP_ADDR_LO                                   0x401014

#define mmDMA_CH_0_RD_COMP_ADDR_HI                                   0x401018

#define mmDMA_CH_0_RD_COMP_WDATA                                     0x40101C

#define mmDMA_CH_0_WR_COMP_ADDR_LO                                   0x401020

#define mmDMA_CH_0_WR_COMP_ADDR_HI                                   0x401024

#define mmDMA_CH_0_WR_COMP_WDATA                                     0x401028

#define mmDMA_CH_0_LDMA_SRC_ADDR_LO                                  0x40102C

#define mmDMA_CH_0_LDMA_SRC_ADDR_HI                                  0x401030

#define mmDMA_CH_0_LDMA_DST_ADDR_LO                                  0x401034

#define mmDMA_CH_0_LDMA_DST_ADDR_HI                                  0x401038

#define mmDMA_CH_0_LDMA_TSIZE                                        0x40103C

#define mmDMA_CH_0_COMIT_TRANSFER                                    0x401040

#define mmDMA_CH_0_STS0                                              0x401044

#define mmDMA_CH_0_STS1                                              0x401048

#define mmDMA_CH_0_STS2                                              0x40104C

#define mmDMA_CH_0_STS3                                              0x401050

#define mmDMA_CH_0_STS4                                              0x401054

#define mmDMA_CH_0_SRC_ADDR_LO_STS                                   0x401058

#define mmDMA_CH_0_SRC_ADDR_HI_STS                                   0x40105C

#define mmDMA_CH_0_SRC_TSIZE_STS                                     0x401060

#define mmDMA_CH_0_DST_ADDR_LO_STS                                   0x401064

#define mmDMA_CH_0_DST_ADDR_HI_STS                                   0x401068

#define mmDMA_CH_0_DST_TSIZE_STS                                     0x40106C

#define mmDMA_CH_0_RD_RATE_LIM_EN                                    0x401070

#define mmDMA_CH_0_RD_RATE_LIM_RST_TOKEN                             0x401074

#define mmDMA_CH_0_RD_RATE_LIM_SAT                                   0x401078

#define mmDMA_CH_0_RD_RATE_LIM_TOUT                                  0x40107C

#define mmDMA_CH_0_WR_RATE_LIM_EN                                    0x401080

#define mmDMA_CH_0_WR_RATE_LIM_RST_TOKEN                             0x401084

#define mmDMA_CH_0_WR_RATE_LIM_SAT                                   0x401088

#define mmDMA_CH_0_WR_RATE_LIM_TOUT                                  0x40108C

#define mmDMA_CH_0_CFG2                                              0x401090

#define mmDMA_CH_0_TDMA_CTL                                          0x401100

#define mmDMA_CH_0_TDMA_SRC_BASE_ADDR_LO                             0x401104

#define mmDMA_CH_0_TDMA_SRC_BASE_ADDR_HI                             0x401108

#define mmDMA_CH_0_TDMA_SRC_ROI_BASE_0                               0x40110C

#define mmDMA_CH_0_TDMA_SRC_ROI_SIZE_0                               0x401110

#define mmDMA_CH_0_TDMA_SRC_VALID_ELEMENTS_0                         0x401114

#define mmDMA_CH_0_TDMA_SRC_START_OFFSET_0                           0x401118

#define mmDMA_CH_0_TDMA_SRC_STRIDE_0                                 0x40111C

#define mmDMA_CH_0_TDMA_SRC_ROI_BASE_1                               0x401120

#define mmDMA_CH_0_TDMA_SRC_ROI_SIZE_1                               0x401124

#define mmDMA_CH_0_TDMA_SRC_VALID_ELEMENTS_1                         0x401128

#define mmDMA_CH_0_TDMA_SRC_START_OFFSET_1                           0x40112C

#define mmDMA_CH_0_TDMA_SRC_STRIDE_1                                 0x401130

#define mmDMA_CH_0_TDMA_SRC_ROI_BASE_2                               0x401134

#define mmDMA_CH_0_TDMA_SRC_ROI_SIZE_2                               0x401138

#define mmDMA_CH_0_TDMA_SRC_VALID_ELEMENTS_2                         0x40113C

#define mmDMA_CH_0_TDMA_SRC_START_OFFSET_2                           0x401140

#define mmDMA_CH_0_TDMA_SRC_STRIDE_2                                 0x401144

#define mmDMA_CH_0_TDMA_SRC_ROI_BASE_3                               0x401148

#define mmDMA_CH_0_TDMA_SRC_ROI_SIZE_3                               0x40114C

#define mmDMA_CH_0_TDMA_SRC_VALID_ELEMENTS_3                         0x401150

#define mmDMA_CH_0_TDMA_SRC_START_OFFSET_3                           0x401154

#define mmDMA_CH_0_TDMA_SRC_STRIDE_3                                 0x401158

#define mmDMA_CH_0_TDMA_SRC_ROI_BASE_4                               0x40115C

#define mmDMA_CH_0_TDMA_SRC_ROI_SIZE_4                               0x401160

#define mmDMA_CH_0_TDMA_SRC_VALID_ELEMENTS_4                         0x401164

#define mmDMA_CH_0_TDMA_SRC_START_OFFSET_4                           0x401168

#define mmDMA_CH_0_TDMA_SRC_STRIDE_4                                 0x40116C

#define mmDMA_CH_0_TDMA_DST_BASE_ADDR_LO                             0x401170

#define mmDMA_CH_0_TDMA_DST_BASE_ADDR_HI                             0x401174

#define mmDMA_CH_0_TDMA_DST_ROI_BASE_0                               0x401178

#define mmDMA_CH_0_TDMA_DST_ROI_SIZE_0                               0x40117C

#define mmDMA_CH_0_TDMA_DST_VALID_ELEMENTS_0                         0x401180

#define mmDMA_CH_0_TDMA_DST_START_OFFSET_0                           0x401184

#define mmDMA_CH_0_TDMA_DST_STRIDE_0                                 0x401188

#define mmDMA_CH_0_TDMA_DST_ROI_BASE_1                               0x40118C

#define mmDMA_CH_0_TDMA_DST_ROI_SIZE_1                               0x401190

#define mmDMA_CH_0_TDMA_DST_VALID_ELEMENTS_1                         0x401194

#define mmDMA_CH_0_TDMA_DST_START_OFFSET_1                           0x401198

#define mmDMA_CH_0_TDMA_DST_STRIDE_1                                 0x40119C

#define mmDMA_CH_0_TDMA_DST_ROI_BASE_2                               0x4011A0

#define mmDMA_CH_0_TDMA_DST_ROI_SIZE_2                               0x4011A4

#define mmDMA_CH_0_TDMA_DST_VALID_ELEMENTS_2                         0x4011A8

#define mmDMA_CH_0_TDMA_DST_START_OFFSET_2                           0x4011AC

#define mmDMA_CH_0_TDMA_DST_STRIDE_2                                 0x4011B0

#define mmDMA_CH_0_TDMA_DST_ROI_BASE_3                               0x4011B4

#define mmDMA_CH_0_TDMA_DST_ROI_SIZE_3                               0x4011B8

#define mmDMA_CH_0_TDMA_DST_VALID_ELEMENTS_3                         0x4011BC

#define mmDMA_CH_0_TDMA_DST_START_OFFSET_3                           0x4011C0

#define mmDMA_CH_0_TDMA_DST_STRIDE_3                                 0x4011C4

#define mmDMA_CH_0_TDMA_DST_ROI_BASE_4                               0x4011C8

#define mmDMA_CH_0_TDMA_DST_ROI_SIZE_4                               0x4011CC

#define mmDMA_CH_0_TDMA_DST_VALID_ELEMENTS_4                         0x4011D0

#define mmDMA_CH_0_TDMA_DST_START_OFFSET_4                           0x4011D4

#define mmDMA_CH_0_TDMA_DST_STRIDE_4                                 0x4011D8

#define mmDMA_CH_0_MEM_INIT_BUSY                                     0x4011FC

#endif /* ASIC_REG_DMA_CH_0_REGS_H_ */
