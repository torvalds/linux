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

#ifndef ASIC_REG_DMA3_CORE_REGS_H_
#define ASIC_REG_DMA3_CORE_REGS_H_

/*
 *****************************************
 *   DMA3_CORE (Prototype: DMA_CORE)
 *****************************************
 */

#define mmDMA3_CORE_CFG_0                                            0x560000

#define mmDMA3_CORE_CFG_1                                            0x560004

#define mmDMA3_CORE_LBW_MAX_OUTSTAND                                 0x560008

#define mmDMA3_CORE_SRC_BASE_LO                                      0x560014

#define mmDMA3_CORE_SRC_BASE_HI                                      0x560018

#define mmDMA3_CORE_DST_BASE_LO                                      0x56001C

#define mmDMA3_CORE_DST_BASE_HI                                      0x560020

#define mmDMA3_CORE_SRC_TSIZE_1                                      0x56002C

#define mmDMA3_CORE_SRC_STRIDE_1                                     0x560030

#define mmDMA3_CORE_SRC_TSIZE_2                                      0x560034

#define mmDMA3_CORE_SRC_STRIDE_2                                     0x560038

#define mmDMA3_CORE_SRC_TSIZE_3                                      0x56003C

#define mmDMA3_CORE_SRC_STRIDE_3                                     0x560040

#define mmDMA3_CORE_SRC_TSIZE_4                                      0x560044

#define mmDMA3_CORE_SRC_STRIDE_4                                     0x560048

#define mmDMA3_CORE_SRC_TSIZE_0                                      0x56004C

#define mmDMA3_CORE_DST_TSIZE_1                                      0x560054

#define mmDMA3_CORE_DST_STRIDE_1                                     0x560058

#define mmDMA3_CORE_DST_TSIZE_2                                      0x56005C

#define mmDMA3_CORE_DST_STRIDE_2                                     0x560060

#define mmDMA3_CORE_DST_TSIZE_3                                      0x560064

#define mmDMA3_CORE_DST_STRIDE_3                                     0x560068

#define mmDMA3_CORE_DST_TSIZE_4                                      0x56006C

#define mmDMA3_CORE_DST_STRIDE_4                                     0x560070

#define mmDMA3_CORE_DST_TSIZE_0                                      0x560074

#define mmDMA3_CORE_COMMIT                                           0x560078

#define mmDMA3_CORE_WR_COMP_WDATA                                    0x56007C

#define mmDMA3_CORE_WR_COMP_ADDR_LO                                  0x560080

#define mmDMA3_CORE_WR_COMP_ADDR_HI                                  0x560084

#define mmDMA3_CORE_WR_COMP_AWUSER_31_11                             0x560088

#define mmDMA3_CORE_TE_NUMROWS                                       0x560094

#define mmDMA3_CORE_PROT                                             0x5600B8

#define mmDMA3_CORE_SECURE_PROPS                                     0x5600F0

#define mmDMA3_CORE_NON_SECURE_PROPS                                 0x5600F4

#define mmDMA3_CORE_RD_MAX_OUTSTAND                                  0x560100

#define mmDMA3_CORE_RD_MAX_SIZE                                      0x560104

#define mmDMA3_CORE_RD_ARCACHE                                       0x560108

#define mmDMA3_CORE_RD_ARUSER_31_11                                  0x560110

#define mmDMA3_CORE_RD_INFLIGHTS                                     0x560114

#define mmDMA3_CORE_WR_MAX_OUTSTAND                                  0x560120

#define mmDMA3_CORE_WR_MAX_AWID                                      0x560124

#define mmDMA3_CORE_WR_AWCACHE                                       0x560128

#define mmDMA3_CORE_WR_AWUSER_31_11                                  0x560130

#define mmDMA3_CORE_WR_INFLIGHTS                                     0x560134

#define mmDMA3_CORE_RD_RATE_LIM_CFG_0                                0x560150

#define mmDMA3_CORE_RD_RATE_LIM_CFG_1                                0x560154

#define mmDMA3_CORE_WR_RATE_LIM_CFG_0                                0x560158

#define mmDMA3_CORE_WR_RATE_LIM_CFG_1                                0x56015C

#define mmDMA3_CORE_ERR_CFG                                          0x560160

#define mmDMA3_CORE_ERR_CAUSE                                        0x560164

#define mmDMA3_CORE_ERRMSG_ADDR_LO                                   0x560170

#define mmDMA3_CORE_ERRMSG_ADDR_HI                                   0x560174

#define mmDMA3_CORE_ERRMSG_WDATA                                     0x560178

#define mmDMA3_CORE_STS0                                             0x560190

#define mmDMA3_CORE_STS1                                             0x560194

#define mmDMA3_CORE_RD_DBGMEM_ADD                                    0x560200

#define mmDMA3_CORE_RD_DBGMEM_DATA_WR                                0x560204

#define mmDMA3_CORE_RD_DBGMEM_DATA_RD                                0x560208

#define mmDMA3_CORE_RD_DBGMEM_CTRL                                   0x56020C

#define mmDMA3_CORE_RD_DBGMEM_RC                                     0x560210

#define mmDMA3_CORE_DBG_HBW_AXI_AR_CNT                               0x560220

#define mmDMA3_CORE_DBG_HBW_AXI_AW_CNT                               0x560224

#define mmDMA3_CORE_DBG_LBW_AXI_AW_CNT                               0x560228

#define mmDMA3_CORE_DBG_DESC_CNT                                     0x56022C

#define mmDMA3_CORE_DBG_STS                                          0x560230

#define mmDMA3_CORE_DBG_RD_DESC_ID                                   0x560234

#define mmDMA3_CORE_DBG_WR_DESC_ID                                   0x560238

#endif /* ASIC_REG_DMA3_CORE_REGS_H_ */
