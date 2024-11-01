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

#ifndef ASIC_REG_DMA6_CORE_REGS_H_
#define ASIC_REG_DMA6_CORE_REGS_H_

/*
 *****************************************
 *   DMA6_CORE (Prototype: DMA_CORE)
 *****************************************
 */

#define mmDMA6_CORE_CFG_0                                            0x5C0000

#define mmDMA6_CORE_CFG_1                                            0x5C0004

#define mmDMA6_CORE_LBW_MAX_OUTSTAND                                 0x5C0008

#define mmDMA6_CORE_SRC_BASE_LO                                      0x5C0014

#define mmDMA6_CORE_SRC_BASE_HI                                      0x5C0018

#define mmDMA6_CORE_DST_BASE_LO                                      0x5C001C

#define mmDMA6_CORE_DST_BASE_HI                                      0x5C0020

#define mmDMA6_CORE_SRC_TSIZE_1                                      0x5C002C

#define mmDMA6_CORE_SRC_STRIDE_1                                     0x5C0030

#define mmDMA6_CORE_SRC_TSIZE_2                                      0x5C0034

#define mmDMA6_CORE_SRC_STRIDE_2                                     0x5C0038

#define mmDMA6_CORE_SRC_TSIZE_3                                      0x5C003C

#define mmDMA6_CORE_SRC_STRIDE_3                                     0x5C0040

#define mmDMA6_CORE_SRC_TSIZE_4                                      0x5C0044

#define mmDMA6_CORE_SRC_STRIDE_4                                     0x5C0048

#define mmDMA6_CORE_SRC_TSIZE_0                                      0x5C004C

#define mmDMA6_CORE_DST_TSIZE_1                                      0x5C0054

#define mmDMA6_CORE_DST_STRIDE_1                                     0x5C0058

#define mmDMA6_CORE_DST_TSIZE_2                                      0x5C005C

#define mmDMA6_CORE_DST_STRIDE_2                                     0x5C0060

#define mmDMA6_CORE_DST_TSIZE_3                                      0x5C0064

#define mmDMA6_CORE_DST_STRIDE_3                                     0x5C0068

#define mmDMA6_CORE_DST_TSIZE_4                                      0x5C006C

#define mmDMA6_CORE_DST_STRIDE_4                                     0x5C0070

#define mmDMA6_CORE_DST_TSIZE_0                                      0x5C0074

#define mmDMA6_CORE_COMMIT                                           0x5C0078

#define mmDMA6_CORE_WR_COMP_WDATA                                    0x5C007C

#define mmDMA6_CORE_WR_COMP_ADDR_LO                                  0x5C0080

#define mmDMA6_CORE_WR_COMP_ADDR_HI                                  0x5C0084

#define mmDMA6_CORE_WR_COMP_AWUSER_31_11                             0x5C0088

#define mmDMA6_CORE_TE_NUMROWS                                       0x5C0094

#define mmDMA6_CORE_PROT                                             0x5C00B8

#define mmDMA6_CORE_SECURE_PROPS                                     0x5C00F0

#define mmDMA6_CORE_NON_SECURE_PROPS                                 0x5C00F4

#define mmDMA6_CORE_RD_MAX_OUTSTAND                                  0x5C0100

#define mmDMA6_CORE_RD_MAX_SIZE                                      0x5C0104

#define mmDMA6_CORE_RD_ARCACHE                                       0x5C0108

#define mmDMA6_CORE_RD_ARUSER_31_11                                  0x5C0110

#define mmDMA6_CORE_RD_INFLIGHTS                                     0x5C0114

#define mmDMA6_CORE_WR_MAX_OUTSTAND                                  0x5C0120

#define mmDMA6_CORE_WR_MAX_AWID                                      0x5C0124

#define mmDMA6_CORE_WR_AWCACHE                                       0x5C0128

#define mmDMA6_CORE_WR_AWUSER_31_11                                  0x5C0130

#define mmDMA6_CORE_WR_INFLIGHTS                                     0x5C0134

#define mmDMA6_CORE_RD_RATE_LIM_CFG_0                                0x5C0150

#define mmDMA6_CORE_RD_RATE_LIM_CFG_1                                0x5C0154

#define mmDMA6_CORE_WR_RATE_LIM_CFG_0                                0x5C0158

#define mmDMA6_CORE_WR_RATE_LIM_CFG_1                                0x5C015C

#define mmDMA6_CORE_ERR_CFG                                          0x5C0160

#define mmDMA6_CORE_ERR_CAUSE                                        0x5C0164

#define mmDMA6_CORE_ERRMSG_ADDR_LO                                   0x5C0170

#define mmDMA6_CORE_ERRMSG_ADDR_HI                                   0x5C0174

#define mmDMA6_CORE_ERRMSG_WDATA                                     0x5C0178

#define mmDMA6_CORE_STS0                                             0x5C0190

#define mmDMA6_CORE_STS1                                             0x5C0194

#define mmDMA6_CORE_RD_DBGMEM_ADD                                    0x5C0200

#define mmDMA6_CORE_RD_DBGMEM_DATA_WR                                0x5C0204

#define mmDMA6_CORE_RD_DBGMEM_DATA_RD                                0x5C0208

#define mmDMA6_CORE_RD_DBGMEM_CTRL                                   0x5C020C

#define mmDMA6_CORE_RD_DBGMEM_RC                                     0x5C0210

#define mmDMA6_CORE_DBG_HBW_AXI_AR_CNT                               0x5C0220

#define mmDMA6_CORE_DBG_HBW_AXI_AW_CNT                               0x5C0224

#define mmDMA6_CORE_DBG_LBW_AXI_AW_CNT                               0x5C0228

#define mmDMA6_CORE_DBG_DESC_CNT                                     0x5C022C

#define mmDMA6_CORE_DBG_STS                                          0x5C0230

#define mmDMA6_CORE_DBG_RD_DESC_ID                                   0x5C0234

#define mmDMA6_CORE_DBG_WR_DESC_ID                                   0x5C0238

#endif /* ASIC_REG_DMA6_CORE_REGS_H_ */
