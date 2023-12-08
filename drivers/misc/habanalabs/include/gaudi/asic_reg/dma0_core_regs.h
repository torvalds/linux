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

#ifndef ASIC_REG_DMA0_CORE_REGS_H_
#define ASIC_REG_DMA0_CORE_REGS_H_

/*
 *****************************************
 *   DMA0_CORE (Prototype: DMA_CORE)
 *****************************************
 */

#define mmDMA0_CORE_CFG_0                                            0x500000

#define mmDMA0_CORE_CFG_1                                            0x500004

#define mmDMA0_CORE_LBW_MAX_OUTSTAND                                 0x500008

#define mmDMA0_CORE_SRC_BASE_LO                                      0x500014

#define mmDMA0_CORE_SRC_BASE_HI                                      0x500018

#define mmDMA0_CORE_DST_BASE_LO                                      0x50001C

#define mmDMA0_CORE_DST_BASE_HI                                      0x500020

#define mmDMA0_CORE_SRC_TSIZE_1                                      0x50002C

#define mmDMA0_CORE_SRC_STRIDE_1                                     0x500030

#define mmDMA0_CORE_SRC_TSIZE_2                                      0x500034

#define mmDMA0_CORE_SRC_STRIDE_2                                     0x500038

#define mmDMA0_CORE_SRC_TSIZE_3                                      0x50003C

#define mmDMA0_CORE_SRC_STRIDE_3                                     0x500040

#define mmDMA0_CORE_SRC_TSIZE_4                                      0x500044

#define mmDMA0_CORE_SRC_STRIDE_4                                     0x500048

#define mmDMA0_CORE_SRC_TSIZE_0                                      0x50004C

#define mmDMA0_CORE_DST_TSIZE_1                                      0x500054

#define mmDMA0_CORE_DST_STRIDE_1                                     0x500058

#define mmDMA0_CORE_DST_TSIZE_2                                      0x50005C

#define mmDMA0_CORE_DST_STRIDE_2                                     0x500060

#define mmDMA0_CORE_DST_TSIZE_3                                      0x500064

#define mmDMA0_CORE_DST_STRIDE_3                                     0x500068

#define mmDMA0_CORE_DST_TSIZE_4                                      0x50006C

#define mmDMA0_CORE_DST_STRIDE_4                                     0x500070

#define mmDMA0_CORE_DST_TSIZE_0                                      0x500074

#define mmDMA0_CORE_COMMIT                                           0x500078

#define mmDMA0_CORE_WR_COMP_WDATA                                    0x50007C

#define mmDMA0_CORE_WR_COMP_ADDR_LO                                  0x500080

#define mmDMA0_CORE_WR_COMP_ADDR_HI                                  0x500084

#define mmDMA0_CORE_WR_COMP_AWUSER_31_11                             0x500088

#define mmDMA0_CORE_TE_NUMROWS                                       0x500094

#define mmDMA0_CORE_PROT                                             0x5000B8

#define mmDMA0_CORE_SECURE_PROPS                                     0x5000F0

#define mmDMA0_CORE_NON_SECURE_PROPS                                 0x5000F4

#define mmDMA0_CORE_RD_MAX_OUTSTAND                                  0x500100

#define mmDMA0_CORE_RD_MAX_SIZE                                      0x500104

#define mmDMA0_CORE_RD_ARCACHE                                       0x500108

#define mmDMA0_CORE_RD_ARUSER_31_11                                  0x500110

#define mmDMA0_CORE_RD_INFLIGHTS                                     0x500114

#define mmDMA0_CORE_WR_MAX_OUTSTAND                                  0x500120

#define mmDMA0_CORE_WR_MAX_AWID                                      0x500124

#define mmDMA0_CORE_WR_AWCACHE                                       0x500128

#define mmDMA0_CORE_WR_AWUSER_31_11                                  0x500130

#define mmDMA0_CORE_WR_INFLIGHTS                                     0x500134

#define mmDMA0_CORE_RD_RATE_LIM_CFG_0                                0x500150

#define mmDMA0_CORE_RD_RATE_LIM_CFG_1                                0x500154

#define mmDMA0_CORE_WR_RATE_LIM_CFG_0                                0x500158

#define mmDMA0_CORE_WR_RATE_LIM_CFG_1                                0x50015C

#define mmDMA0_CORE_ERR_CFG                                          0x500160

#define mmDMA0_CORE_ERR_CAUSE                                        0x500164

#define mmDMA0_CORE_ERRMSG_ADDR_LO                                   0x500170

#define mmDMA0_CORE_ERRMSG_ADDR_HI                                   0x500174

#define mmDMA0_CORE_ERRMSG_WDATA                                     0x500178

#define mmDMA0_CORE_STS0                                             0x500190

#define mmDMA0_CORE_STS1                                             0x500194

#define mmDMA0_CORE_RD_DBGMEM_ADD                                    0x500200

#define mmDMA0_CORE_RD_DBGMEM_DATA_WR                                0x500204

#define mmDMA0_CORE_RD_DBGMEM_DATA_RD                                0x500208

#define mmDMA0_CORE_RD_DBGMEM_CTRL                                   0x50020C

#define mmDMA0_CORE_RD_DBGMEM_RC                                     0x500210

#define mmDMA0_CORE_DBG_HBW_AXI_AR_CNT                               0x500220

#define mmDMA0_CORE_DBG_HBW_AXI_AW_CNT                               0x500224

#define mmDMA0_CORE_DBG_LBW_AXI_AW_CNT                               0x500228

#define mmDMA0_CORE_DBG_DESC_CNT                                     0x50022C

#define mmDMA0_CORE_DBG_STS                                          0x500230

#define mmDMA0_CORE_DBG_RD_DESC_ID                                   0x500234

#define mmDMA0_CORE_DBG_WR_DESC_ID                                   0x500238

#endif /* ASIC_REG_DMA0_CORE_REGS_H_ */
