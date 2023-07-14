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

#ifndef ASIC_REG_DMA4_CORE_REGS_H_
#define ASIC_REG_DMA4_CORE_REGS_H_

/*
 *****************************************
 *   DMA4_CORE (Prototype: DMA_CORE)
 *****************************************
 */

#define mmDMA4_CORE_CFG_0                                            0x580000

#define mmDMA4_CORE_CFG_1                                            0x580004

#define mmDMA4_CORE_LBW_MAX_OUTSTAND                                 0x580008

#define mmDMA4_CORE_SRC_BASE_LO                                      0x580014

#define mmDMA4_CORE_SRC_BASE_HI                                      0x580018

#define mmDMA4_CORE_DST_BASE_LO                                      0x58001C

#define mmDMA4_CORE_DST_BASE_HI                                      0x580020

#define mmDMA4_CORE_SRC_TSIZE_1                                      0x58002C

#define mmDMA4_CORE_SRC_STRIDE_1                                     0x580030

#define mmDMA4_CORE_SRC_TSIZE_2                                      0x580034

#define mmDMA4_CORE_SRC_STRIDE_2                                     0x580038

#define mmDMA4_CORE_SRC_TSIZE_3                                      0x58003C

#define mmDMA4_CORE_SRC_STRIDE_3                                     0x580040

#define mmDMA4_CORE_SRC_TSIZE_4                                      0x580044

#define mmDMA4_CORE_SRC_STRIDE_4                                     0x580048

#define mmDMA4_CORE_SRC_TSIZE_0                                      0x58004C

#define mmDMA4_CORE_DST_TSIZE_1                                      0x580054

#define mmDMA4_CORE_DST_STRIDE_1                                     0x580058

#define mmDMA4_CORE_DST_TSIZE_2                                      0x58005C

#define mmDMA4_CORE_DST_STRIDE_2                                     0x580060

#define mmDMA4_CORE_DST_TSIZE_3                                      0x580064

#define mmDMA4_CORE_DST_STRIDE_3                                     0x580068

#define mmDMA4_CORE_DST_TSIZE_4                                      0x58006C

#define mmDMA4_CORE_DST_STRIDE_4                                     0x580070

#define mmDMA4_CORE_DST_TSIZE_0                                      0x580074

#define mmDMA4_CORE_COMMIT                                           0x580078

#define mmDMA4_CORE_WR_COMP_WDATA                                    0x58007C

#define mmDMA4_CORE_WR_COMP_ADDR_LO                                  0x580080

#define mmDMA4_CORE_WR_COMP_ADDR_HI                                  0x580084

#define mmDMA4_CORE_WR_COMP_AWUSER_31_11                             0x580088

#define mmDMA4_CORE_TE_NUMROWS                                       0x580094

#define mmDMA4_CORE_PROT                                             0x5800B8

#define mmDMA4_CORE_SECURE_PROPS                                     0x5800F0

#define mmDMA4_CORE_NON_SECURE_PROPS                                 0x5800F4

#define mmDMA4_CORE_RD_MAX_OUTSTAND                                  0x580100

#define mmDMA4_CORE_RD_MAX_SIZE                                      0x580104

#define mmDMA4_CORE_RD_ARCACHE                                       0x580108

#define mmDMA4_CORE_RD_ARUSER_31_11                                  0x580110

#define mmDMA4_CORE_RD_INFLIGHTS                                     0x580114

#define mmDMA4_CORE_WR_MAX_OUTSTAND                                  0x580120

#define mmDMA4_CORE_WR_MAX_AWID                                      0x580124

#define mmDMA4_CORE_WR_AWCACHE                                       0x580128

#define mmDMA4_CORE_WR_AWUSER_31_11                                  0x580130

#define mmDMA4_CORE_WR_INFLIGHTS                                     0x580134

#define mmDMA4_CORE_RD_RATE_LIM_CFG_0                                0x580150

#define mmDMA4_CORE_RD_RATE_LIM_CFG_1                                0x580154

#define mmDMA4_CORE_WR_RATE_LIM_CFG_0                                0x580158

#define mmDMA4_CORE_WR_RATE_LIM_CFG_1                                0x58015C

#define mmDMA4_CORE_ERR_CFG                                          0x580160

#define mmDMA4_CORE_ERR_CAUSE                                        0x580164

#define mmDMA4_CORE_ERRMSG_ADDR_LO                                   0x580170

#define mmDMA4_CORE_ERRMSG_ADDR_HI                                   0x580174

#define mmDMA4_CORE_ERRMSG_WDATA                                     0x580178

#define mmDMA4_CORE_STS0                                             0x580190

#define mmDMA4_CORE_STS1                                             0x580194

#define mmDMA4_CORE_RD_DBGMEM_ADD                                    0x580200

#define mmDMA4_CORE_RD_DBGMEM_DATA_WR                                0x580204

#define mmDMA4_CORE_RD_DBGMEM_DATA_RD                                0x580208

#define mmDMA4_CORE_RD_DBGMEM_CTRL                                   0x58020C

#define mmDMA4_CORE_RD_DBGMEM_RC                                     0x580210

#define mmDMA4_CORE_DBG_HBW_AXI_AR_CNT                               0x580220

#define mmDMA4_CORE_DBG_HBW_AXI_AW_CNT                               0x580224

#define mmDMA4_CORE_DBG_LBW_AXI_AW_CNT                               0x580228

#define mmDMA4_CORE_DBG_DESC_CNT                                     0x58022C

#define mmDMA4_CORE_DBG_STS                                          0x580230

#define mmDMA4_CORE_DBG_RD_DESC_ID                                   0x580234

#define mmDMA4_CORE_DBG_WR_DESC_ID                                   0x580238

#endif /* ASIC_REG_DMA4_CORE_REGS_H_ */
