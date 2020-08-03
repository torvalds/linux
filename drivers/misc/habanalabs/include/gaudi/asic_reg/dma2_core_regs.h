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

#ifndef ASIC_REG_DMA2_CORE_REGS_H_
#define ASIC_REG_DMA2_CORE_REGS_H_

/*
 *****************************************
 *   DMA2_CORE (Prototype: DMA_CORE)
 *****************************************
 */

#define mmDMA2_CORE_CFG_0                                            0x540000

#define mmDMA2_CORE_CFG_1                                            0x540004

#define mmDMA2_CORE_LBW_MAX_OUTSTAND                                 0x540008

#define mmDMA2_CORE_SRC_BASE_LO                                      0x540014

#define mmDMA2_CORE_SRC_BASE_HI                                      0x540018

#define mmDMA2_CORE_DST_BASE_LO                                      0x54001C

#define mmDMA2_CORE_DST_BASE_HI                                      0x540020

#define mmDMA2_CORE_SRC_TSIZE_1                                      0x54002C

#define mmDMA2_CORE_SRC_STRIDE_1                                     0x540030

#define mmDMA2_CORE_SRC_TSIZE_2                                      0x540034

#define mmDMA2_CORE_SRC_STRIDE_2                                     0x540038

#define mmDMA2_CORE_SRC_TSIZE_3                                      0x54003C

#define mmDMA2_CORE_SRC_STRIDE_3                                     0x540040

#define mmDMA2_CORE_SRC_TSIZE_4                                      0x540044

#define mmDMA2_CORE_SRC_STRIDE_4                                     0x540048

#define mmDMA2_CORE_SRC_TSIZE_0                                      0x54004C

#define mmDMA2_CORE_DST_TSIZE_1                                      0x540054

#define mmDMA2_CORE_DST_STRIDE_1                                     0x540058

#define mmDMA2_CORE_DST_TSIZE_2                                      0x54005C

#define mmDMA2_CORE_DST_STRIDE_2                                     0x540060

#define mmDMA2_CORE_DST_TSIZE_3                                      0x540064

#define mmDMA2_CORE_DST_STRIDE_3                                     0x540068

#define mmDMA2_CORE_DST_TSIZE_4                                      0x54006C

#define mmDMA2_CORE_DST_STRIDE_4                                     0x540070

#define mmDMA2_CORE_DST_TSIZE_0                                      0x540074

#define mmDMA2_CORE_COMMIT                                           0x540078

#define mmDMA2_CORE_WR_COMP_WDATA                                    0x54007C

#define mmDMA2_CORE_WR_COMP_ADDR_LO                                  0x540080

#define mmDMA2_CORE_WR_COMP_ADDR_HI                                  0x540084

#define mmDMA2_CORE_WR_COMP_AWUSER_31_11                             0x540088

#define mmDMA2_CORE_TE_NUMROWS                                       0x540094

#define mmDMA2_CORE_PROT                                             0x5400B8

#define mmDMA2_CORE_SECURE_PROPS                                     0x5400F0

#define mmDMA2_CORE_NON_SECURE_PROPS                                 0x5400F4

#define mmDMA2_CORE_RD_MAX_OUTSTAND                                  0x540100

#define mmDMA2_CORE_RD_MAX_SIZE                                      0x540104

#define mmDMA2_CORE_RD_ARCACHE                                       0x540108

#define mmDMA2_CORE_RD_ARUSER_31_11                                  0x540110

#define mmDMA2_CORE_RD_INFLIGHTS                                     0x540114

#define mmDMA2_CORE_WR_MAX_OUTSTAND                                  0x540120

#define mmDMA2_CORE_WR_MAX_AWID                                      0x540124

#define mmDMA2_CORE_WR_AWCACHE                                       0x540128

#define mmDMA2_CORE_WR_AWUSER_31_11                                  0x540130

#define mmDMA2_CORE_WR_INFLIGHTS                                     0x540134

#define mmDMA2_CORE_RD_RATE_LIM_CFG_0                                0x540150

#define mmDMA2_CORE_RD_RATE_LIM_CFG_1                                0x540154

#define mmDMA2_CORE_WR_RATE_LIM_CFG_0                                0x540158

#define mmDMA2_CORE_WR_RATE_LIM_CFG_1                                0x54015C

#define mmDMA2_CORE_ERR_CFG                                          0x540160

#define mmDMA2_CORE_ERR_CAUSE                                        0x540164

#define mmDMA2_CORE_ERRMSG_ADDR_LO                                   0x540170

#define mmDMA2_CORE_ERRMSG_ADDR_HI                                   0x540174

#define mmDMA2_CORE_ERRMSG_WDATA                                     0x540178

#define mmDMA2_CORE_STS0                                             0x540190

#define mmDMA2_CORE_STS1                                             0x540194

#define mmDMA2_CORE_RD_DBGMEM_ADD                                    0x540200

#define mmDMA2_CORE_RD_DBGMEM_DATA_WR                                0x540204

#define mmDMA2_CORE_RD_DBGMEM_DATA_RD                                0x540208

#define mmDMA2_CORE_RD_DBGMEM_CTRL                                   0x54020C

#define mmDMA2_CORE_RD_DBGMEM_RC                                     0x540210

#define mmDMA2_CORE_DBG_HBW_AXI_AR_CNT                               0x540220

#define mmDMA2_CORE_DBG_HBW_AXI_AW_CNT                               0x540224

#define mmDMA2_CORE_DBG_LBW_AXI_AW_CNT                               0x540228

#define mmDMA2_CORE_DBG_DESC_CNT                                     0x54022C

#define mmDMA2_CORE_DBG_STS                                          0x540230

#define mmDMA2_CORE_DBG_RD_DESC_ID                                   0x540234

#define mmDMA2_CORE_DBG_WR_DESC_ID                                   0x540238

#endif /* ASIC_REG_DMA2_CORE_REGS_H_ */
