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

#ifndef ASIC_REG_DMA7_CORE_REGS_H_
#define ASIC_REG_DMA7_CORE_REGS_H_

/*
 *****************************************
 *   DMA7_CORE (Prototype: DMA_CORE)
 *****************************************
 */

#define mmDMA7_CORE_CFG_0                                            0x5E0000

#define mmDMA7_CORE_CFG_1                                            0x5E0004

#define mmDMA7_CORE_LBW_MAX_OUTSTAND                                 0x5E0008

#define mmDMA7_CORE_SRC_BASE_LO                                      0x5E0014

#define mmDMA7_CORE_SRC_BASE_HI                                      0x5E0018

#define mmDMA7_CORE_DST_BASE_LO                                      0x5E001C

#define mmDMA7_CORE_DST_BASE_HI                                      0x5E0020

#define mmDMA7_CORE_SRC_TSIZE_1                                      0x5E002C

#define mmDMA7_CORE_SRC_STRIDE_1                                     0x5E0030

#define mmDMA7_CORE_SRC_TSIZE_2                                      0x5E0034

#define mmDMA7_CORE_SRC_STRIDE_2                                     0x5E0038

#define mmDMA7_CORE_SRC_TSIZE_3                                      0x5E003C

#define mmDMA7_CORE_SRC_STRIDE_3                                     0x5E0040

#define mmDMA7_CORE_SRC_TSIZE_4                                      0x5E0044

#define mmDMA7_CORE_SRC_STRIDE_4                                     0x5E0048

#define mmDMA7_CORE_SRC_TSIZE_0                                      0x5E004C

#define mmDMA7_CORE_DST_TSIZE_1                                      0x5E0054

#define mmDMA7_CORE_DST_STRIDE_1                                     0x5E0058

#define mmDMA7_CORE_DST_TSIZE_2                                      0x5E005C

#define mmDMA7_CORE_DST_STRIDE_2                                     0x5E0060

#define mmDMA7_CORE_DST_TSIZE_3                                      0x5E0064

#define mmDMA7_CORE_DST_STRIDE_3                                     0x5E0068

#define mmDMA7_CORE_DST_TSIZE_4                                      0x5E006C

#define mmDMA7_CORE_DST_STRIDE_4                                     0x5E0070

#define mmDMA7_CORE_DST_TSIZE_0                                      0x5E0074

#define mmDMA7_CORE_COMMIT                                           0x5E0078

#define mmDMA7_CORE_WR_COMP_WDATA                                    0x5E007C

#define mmDMA7_CORE_WR_COMP_ADDR_LO                                  0x5E0080

#define mmDMA7_CORE_WR_COMP_ADDR_HI                                  0x5E0084

#define mmDMA7_CORE_WR_COMP_AWUSER_31_11                             0x5E0088

#define mmDMA7_CORE_TE_NUMROWS                                       0x5E0094

#define mmDMA7_CORE_PROT                                             0x5E00B8

#define mmDMA7_CORE_SECURE_PROPS                                     0x5E00F0

#define mmDMA7_CORE_NON_SECURE_PROPS                                 0x5E00F4

#define mmDMA7_CORE_RD_MAX_OUTSTAND                                  0x5E0100

#define mmDMA7_CORE_RD_MAX_SIZE                                      0x5E0104

#define mmDMA7_CORE_RD_ARCACHE                                       0x5E0108

#define mmDMA7_CORE_RD_ARUSER_31_11                                  0x5E0110

#define mmDMA7_CORE_RD_INFLIGHTS                                     0x5E0114

#define mmDMA7_CORE_WR_MAX_OUTSTAND                                  0x5E0120

#define mmDMA7_CORE_WR_MAX_AWID                                      0x5E0124

#define mmDMA7_CORE_WR_AWCACHE                                       0x5E0128

#define mmDMA7_CORE_WR_AWUSER_31_11                                  0x5E0130

#define mmDMA7_CORE_WR_INFLIGHTS                                     0x5E0134

#define mmDMA7_CORE_RD_RATE_LIM_CFG_0                                0x5E0150

#define mmDMA7_CORE_RD_RATE_LIM_CFG_1                                0x5E0154

#define mmDMA7_CORE_WR_RATE_LIM_CFG_0                                0x5E0158

#define mmDMA7_CORE_WR_RATE_LIM_CFG_1                                0x5E015C

#define mmDMA7_CORE_ERR_CFG                                          0x5E0160

#define mmDMA7_CORE_ERR_CAUSE                                        0x5E0164

#define mmDMA7_CORE_ERRMSG_ADDR_LO                                   0x5E0170

#define mmDMA7_CORE_ERRMSG_ADDR_HI                                   0x5E0174

#define mmDMA7_CORE_ERRMSG_WDATA                                     0x5E0178

#define mmDMA7_CORE_STS0                                             0x5E0190

#define mmDMA7_CORE_STS1                                             0x5E0194

#define mmDMA7_CORE_RD_DBGMEM_ADD                                    0x5E0200

#define mmDMA7_CORE_RD_DBGMEM_DATA_WR                                0x5E0204

#define mmDMA7_CORE_RD_DBGMEM_DATA_RD                                0x5E0208

#define mmDMA7_CORE_RD_DBGMEM_CTRL                                   0x5E020C

#define mmDMA7_CORE_RD_DBGMEM_RC                                     0x5E0210

#define mmDMA7_CORE_DBG_HBW_AXI_AR_CNT                               0x5E0220

#define mmDMA7_CORE_DBG_HBW_AXI_AW_CNT                               0x5E0224

#define mmDMA7_CORE_DBG_LBW_AXI_AW_CNT                               0x5E0228

#define mmDMA7_CORE_DBG_DESC_CNT                                     0x5E022C

#define mmDMA7_CORE_DBG_STS                                          0x5E0230

#define mmDMA7_CORE_DBG_RD_DESC_ID                                   0x5E0234

#define mmDMA7_CORE_DBG_WR_DESC_ID                                   0x5E0238

#endif /* ASIC_REG_DMA7_CORE_REGS_H_ */
