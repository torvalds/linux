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

#ifndef ASIC_REG_DMA5_CORE_REGS_H_
#define ASIC_REG_DMA5_CORE_REGS_H_

/*
 *****************************************
 *   DMA5_CORE (Prototype: DMA_CORE)
 *****************************************
 */

#define mmDMA5_CORE_CFG_0                                            0x5A0000

#define mmDMA5_CORE_CFG_1                                            0x5A0004

#define mmDMA5_CORE_LBW_MAX_OUTSTAND                                 0x5A0008

#define mmDMA5_CORE_SRC_BASE_LO                                      0x5A0014

#define mmDMA5_CORE_SRC_BASE_HI                                      0x5A0018

#define mmDMA5_CORE_DST_BASE_LO                                      0x5A001C

#define mmDMA5_CORE_DST_BASE_HI                                      0x5A0020

#define mmDMA5_CORE_SRC_TSIZE_1                                      0x5A002C

#define mmDMA5_CORE_SRC_STRIDE_1                                     0x5A0030

#define mmDMA5_CORE_SRC_TSIZE_2                                      0x5A0034

#define mmDMA5_CORE_SRC_STRIDE_2                                     0x5A0038

#define mmDMA5_CORE_SRC_TSIZE_3                                      0x5A003C

#define mmDMA5_CORE_SRC_STRIDE_3                                     0x5A0040

#define mmDMA5_CORE_SRC_TSIZE_4                                      0x5A0044

#define mmDMA5_CORE_SRC_STRIDE_4                                     0x5A0048

#define mmDMA5_CORE_SRC_TSIZE_0                                      0x5A004C

#define mmDMA5_CORE_DST_TSIZE_1                                      0x5A0054

#define mmDMA5_CORE_DST_STRIDE_1                                     0x5A0058

#define mmDMA5_CORE_DST_TSIZE_2                                      0x5A005C

#define mmDMA5_CORE_DST_STRIDE_2                                     0x5A0060

#define mmDMA5_CORE_DST_TSIZE_3                                      0x5A0064

#define mmDMA5_CORE_DST_STRIDE_3                                     0x5A0068

#define mmDMA5_CORE_DST_TSIZE_4                                      0x5A006C

#define mmDMA5_CORE_DST_STRIDE_4                                     0x5A0070

#define mmDMA5_CORE_DST_TSIZE_0                                      0x5A0074

#define mmDMA5_CORE_COMMIT                                           0x5A0078

#define mmDMA5_CORE_WR_COMP_WDATA                                    0x5A007C

#define mmDMA5_CORE_WR_COMP_ADDR_LO                                  0x5A0080

#define mmDMA5_CORE_WR_COMP_ADDR_HI                                  0x5A0084

#define mmDMA5_CORE_WR_COMP_AWUSER_31_11                             0x5A0088

#define mmDMA5_CORE_TE_NUMROWS                                       0x5A0094

#define mmDMA5_CORE_PROT                                             0x5A00B8

#define mmDMA5_CORE_SECURE_PROPS                                     0x5A00F0

#define mmDMA5_CORE_NON_SECURE_PROPS                                 0x5A00F4

#define mmDMA5_CORE_RD_MAX_OUTSTAND                                  0x5A0100

#define mmDMA5_CORE_RD_MAX_SIZE                                      0x5A0104

#define mmDMA5_CORE_RD_ARCACHE                                       0x5A0108

#define mmDMA5_CORE_RD_ARUSER_31_11                                  0x5A0110

#define mmDMA5_CORE_RD_INFLIGHTS                                     0x5A0114

#define mmDMA5_CORE_WR_MAX_OUTSTAND                                  0x5A0120

#define mmDMA5_CORE_WR_MAX_AWID                                      0x5A0124

#define mmDMA5_CORE_WR_AWCACHE                                       0x5A0128

#define mmDMA5_CORE_WR_AWUSER_31_11                                  0x5A0130

#define mmDMA5_CORE_WR_INFLIGHTS                                     0x5A0134

#define mmDMA5_CORE_RD_RATE_LIM_CFG_0                                0x5A0150

#define mmDMA5_CORE_RD_RATE_LIM_CFG_1                                0x5A0154

#define mmDMA5_CORE_WR_RATE_LIM_CFG_0                                0x5A0158

#define mmDMA5_CORE_WR_RATE_LIM_CFG_1                                0x5A015C

#define mmDMA5_CORE_ERR_CFG                                          0x5A0160

#define mmDMA5_CORE_ERR_CAUSE                                        0x5A0164

#define mmDMA5_CORE_ERRMSG_ADDR_LO                                   0x5A0170

#define mmDMA5_CORE_ERRMSG_ADDR_HI                                   0x5A0174

#define mmDMA5_CORE_ERRMSG_WDATA                                     0x5A0178

#define mmDMA5_CORE_STS0                                             0x5A0190

#define mmDMA5_CORE_STS1                                             0x5A0194

#define mmDMA5_CORE_RD_DBGMEM_ADD                                    0x5A0200

#define mmDMA5_CORE_RD_DBGMEM_DATA_WR                                0x5A0204

#define mmDMA5_CORE_RD_DBGMEM_DATA_RD                                0x5A0208

#define mmDMA5_CORE_RD_DBGMEM_CTRL                                   0x5A020C

#define mmDMA5_CORE_RD_DBGMEM_RC                                     0x5A0210

#define mmDMA5_CORE_DBG_HBW_AXI_AR_CNT                               0x5A0220

#define mmDMA5_CORE_DBG_HBW_AXI_AW_CNT                               0x5A0224

#define mmDMA5_CORE_DBG_LBW_AXI_AW_CNT                               0x5A0228

#define mmDMA5_CORE_DBG_DESC_CNT                                     0x5A022C

#define mmDMA5_CORE_DBG_STS                                          0x5A0230

#define mmDMA5_CORE_DBG_RD_DESC_ID                                   0x5A0234

#define mmDMA5_CORE_DBG_WR_DESC_ID                                   0x5A0238

#endif /* ASIC_REG_DMA5_CORE_REGS_H_ */
