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

#ifndef ASIC_REG_DMA1_CORE_REGS_H_
#define ASIC_REG_DMA1_CORE_REGS_H_

/*
 *****************************************
 *   DMA1_CORE (Prototype: DMA_CORE)
 *****************************************
 */

#define mmDMA1_CORE_CFG_0                                            0x520000

#define mmDMA1_CORE_CFG_1                                            0x520004

#define mmDMA1_CORE_LBW_MAX_OUTSTAND                                 0x520008

#define mmDMA1_CORE_SRC_BASE_LO                                      0x520014

#define mmDMA1_CORE_SRC_BASE_HI                                      0x520018

#define mmDMA1_CORE_DST_BASE_LO                                      0x52001C

#define mmDMA1_CORE_DST_BASE_HI                                      0x520020

#define mmDMA1_CORE_SRC_TSIZE_1                                      0x52002C

#define mmDMA1_CORE_SRC_STRIDE_1                                     0x520030

#define mmDMA1_CORE_SRC_TSIZE_2                                      0x520034

#define mmDMA1_CORE_SRC_STRIDE_2                                     0x520038

#define mmDMA1_CORE_SRC_TSIZE_3                                      0x52003C

#define mmDMA1_CORE_SRC_STRIDE_3                                     0x520040

#define mmDMA1_CORE_SRC_TSIZE_4                                      0x520044

#define mmDMA1_CORE_SRC_STRIDE_4                                     0x520048

#define mmDMA1_CORE_SRC_TSIZE_0                                      0x52004C

#define mmDMA1_CORE_DST_TSIZE_1                                      0x520054

#define mmDMA1_CORE_DST_STRIDE_1                                     0x520058

#define mmDMA1_CORE_DST_TSIZE_2                                      0x52005C

#define mmDMA1_CORE_DST_STRIDE_2                                     0x520060

#define mmDMA1_CORE_DST_TSIZE_3                                      0x520064

#define mmDMA1_CORE_DST_STRIDE_3                                     0x520068

#define mmDMA1_CORE_DST_TSIZE_4                                      0x52006C

#define mmDMA1_CORE_DST_STRIDE_4                                     0x520070

#define mmDMA1_CORE_DST_TSIZE_0                                      0x520074

#define mmDMA1_CORE_COMMIT                                           0x520078

#define mmDMA1_CORE_WR_COMP_WDATA                                    0x52007C

#define mmDMA1_CORE_WR_COMP_ADDR_LO                                  0x520080

#define mmDMA1_CORE_WR_COMP_ADDR_HI                                  0x520084

#define mmDMA1_CORE_WR_COMP_AWUSER_31_11                             0x520088

#define mmDMA1_CORE_TE_NUMROWS                                       0x520094

#define mmDMA1_CORE_PROT                                             0x5200B8

#define mmDMA1_CORE_SECURE_PROPS                                     0x5200F0

#define mmDMA1_CORE_NON_SECURE_PROPS                                 0x5200F4

#define mmDMA1_CORE_RD_MAX_OUTSTAND                                  0x520100

#define mmDMA1_CORE_RD_MAX_SIZE                                      0x520104

#define mmDMA1_CORE_RD_ARCACHE                                       0x520108

#define mmDMA1_CORE_RD_ARUSER_31_11                                  0x520110

#define mmDMA1_CORE_RD_INFLIGHTS                                     0x520114

#define mmDMA1_CORE_WR_MAX_OUTSTAND                                  0x520120

#define mmDMA1_CORE_WR_MAX_AWID                                      0x520124

#define mmDMA1_CORE_WR_AWCACHE                                       0x520128

#define mmDMA1_CORE_WR_AWUSER_31_11                                  0x520130

#define mmDMA1_CORE_WR_INFLIGHTS                                     0x520134

#define mmDMA1_CORE_RD_RATE_LIM_CFG_0                                0x520150

#define mmDMA1_CORE_RD_RATE_LIM_CFG_1                                0x520154

#define mmDMA1_CORE_WR_RATE_LIM_CFG_0                                0x520158

#define mmDMA1_CORE_WR_RATE_LIM_CFG_1                                0x52015C

#define mmDMA1_CORE_ERR_CFG                                          0x520160

#define mmDMA1_CORE_ERR_CAUSE                                        0x520164

#define mmDMA1_CORE_ERRMSG_ADDR_LO                                   0x520170

#define mmDMA1_CORE_ERRMSG_ADDR_HI                                   0x520174

#define mmDMA1_CORE_ERRMSG_WDATA                                     0x520178

#define mmDMA1_CORE_STS0                                             0x520190

#define mmDMA1_CORE_STS1                                             0x520194

#define mmDMA1_CORE_RD_DBGMEM_ADD                                    0x520200

#define mmDMA1_CORE_RD_DBGMEM_DATA_WR                                0x520204

#define mmDMA1_CORE_RD_DBGMEM_DATA_RD                                0x520208

#define mmDMA1_CORE_RD_DBGMEM_CTRL                                   0x52020C

#define mmDMA1_CORE_RD_DBGMEM_RC                                     0x520210

#define mmDMA1_CORE_DBG_HBW_AXI_AR_CNT                               0x520220

#define mmDMA1_CORE_DBG_HBW_AXI_AW_CNT                               0x520224

#define mmDMA1_CORE_DBG_LBW_AXI_AW_CNT                               0x520228

#define mmDMA1_CORE_DBG_DESC_CNT                                     0x52022C

#define mmDMA1_CORE_DBG_STS                                          0x520230

#define mmDMA1_CORE_DBG_RD_DESC_ID                                   0x520234

#define mmDMA1_CORE_DBG_WR_DESC_ID                                   0x520238

#endif /* ASIC_REG_DMA1_CORE_REGS_H_ */
