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

#ifndef ASIC_REG_NIC1_QM0_REGS_H_
#define ASIC_REG_NIC1_QM0_REGS_H_

/*
 *****************************************
 *   NIC1_QM0 (Prototype: QMAN)
 *****************************************
 */

#define mmNIC1_QM0_GLBL_CFG0                                         0xD20000

#define mmNIC1_QM0_GLBL_CFG1                                         0xD20004

#define mmNIC1_QM0_GLBL_PROT                                         0xD20008

#define mmNIC1_QM0_GLBL_ERR_CFG                                      0xD2000C

#define mmNIC1_QM0_GLBL_SECURE_PROPS_0                               0xD20010

#define mmNIC1_QM0_GLBL_SECURE_PROPS_1                               0xD20014

#define mmNIC1_QM0_GLBL_SECURE_PROPS_2                               0xD20018

#define mmNIC1_QM0_GLBL_SECURE_PROPS_3                               0xD2001C

#define mmNIC1_QM0_GLBL_SECURE_PROPS_4                               0xD20020

#define mmNIC1_QM0_GLBL_NON_SECURE_PROPS_0                           0xD20024

#define mmNIC1_QM0_GLBL_NON_SECURE_PROPS_1                           0xD20028

#define mmNIC1_QM0_GLBL_NON_SECURE_PROPS_2                           0xD2002C

#define mmNIC1_QM0_GLBL_NON_SECURE_PROPS_3                           0xD20030

#define mmNIC1_QM0_GLBL_NON_SECURE_PROPS_4                           0xD20034

#define mmNIC1_QM0_GLBL_STS0                                         0xD20038

#define mmNIC1_QM0_GLBL_STS1_0                                       0xD20040

#define mmNIC1_QM0_GLBL_STS1_1                                       0xD20044

#define mmNIC1_QM0_GLBL_STS1_2                                       0xD20048

#define mmNIC1_QM0_GLBL_STS1_3                                       0xD2004C

#define mmNIC1_QM0_GLBL_STS1_4                                       0xD20050

#define mmNIC1_QM0_GLBL_MSG_EN_0                                     0xD20054

#define mmNIC1_QM0_GLBL_MSG_EN_1                                     0xD20058

#define mmNIC1_QM0_GLBL_MSG_EN_2                                     0xD2005C

#define mmNIC1_QM0_GLBL_MSG_EN_3                                     0xD20060

#define mmNIC1_QM0_GLBL_MSG_EN_4                                     0xD20068

#define mmNIC1_QM0_PQ_BASE_LO_0                                      0xD20070

#define mmNIC1_QM0_PQ_BASE_LO_1                                      0xD20074

#define mmNIC1_QM0_PQ_BASE_LO_2                                      0xD20078

#define mmNIC1_QM0_PQ_BASE_LO_3                                      0xD2007C

#define mmNIC1_QM0_PQ_BASE_HI_0                                      0xD20080

#define mmNIC1_QM0_PQ_BASE_HI_1                                      0xD20084

#define mmNIC1_QM0_PQ_BASE_HI_2                                      0xD20088

#define mmNIC1_QM0_PQ_BASE_HI_3                                      0xD2008C

#define mmNIC1_QM0_PQ_SIZE_0                                         0xD20090

#define mmNIC1_QM0_PQ_SIZE_1                                         0xD20094

#define mmNIC1_QM0_PQ_SIZE_2                                         0xD20098

#define mmNIC1_QM0_PQ_SIZE_3                                         0xD2009C

#define mmNIC1_QM0_PQ_PI_0                                           0xD200A0

#define mmNIC1_QM0_PQ_PI_1                                           0xD200A4

#define mmNIC1_QM0_PQ_PI_2                                           0xD200A8

#define mmNIC1_QM0_PQ_PI_3                                           0xD200AC

#define mmNIC1_QM0_PQ_CI_0                                           0xD200B0

#define mmNIC1_QM0_PQ_CI_1                                           0xD200B4

#define mmNIC1_QM0_PQ_CI_2                                           0xD200B8

#define mmNIC1_QM0_PQ_CI_3                                           0xD200BC

#define mmNIC1_QM0_PQ_CFG0_0                                         0xD200C0

#define mmNIC1_QM0_PQ_CFG0_1                                         0xD200C4

#define mmNIC1_QM0_PQ_CFG0_2                                         0xD200C8

#define mmNIC1_QM0_PQ_CFG0_3                                         0xD200CC

#define mmNIC1_QM0_PQ_CFG1_0                                         0xD200D0

#define mmNIC1_QM0_PQ_CFG1_1                                         0xD200D4

#define mmNIC1_QM0_PQ_CFG1_2                                         0xD200D8

#define mmNIC1_QM0_PQ_CFG1_3                                         0xD200DC

#define mmNIC1_QM0_PQ_ARUSER_31_11_0                                 0xD200E0

#define mmNIC1_QM0_PQ_ARUSER_31_11_1                                 0xD200E4

#define mmNIC1_QM0_PQ_ARUSER_31_11_2                                 0xD200E8

#define mmNIC1_QM0_PQ_ARUSER_31_11_3                                 0xD200EC

#define mmNIC1_QM0_PQ_STS0_0                                         0xD200F0

#define mmNIC1_QM0_PQ_STS0_1                                         0xD200F4

#define mmNIC1_QM0_PQ_STS0_2                                         0xD200F8

#define mmNIC1_QM0_PQ_STS0_3                                         0xD200FC

#define mmNIC1_QM0_PQ_STS1_0                                         0xD20100

#define mmNIC1_QM0_PQ_STS1_1                                         0xD20104

#define mmNIC1_QM0_PQ_STS1_2                                         0xD20108

#define mmNIC1_QM0_PQ_STS1_3                                         0xD2010C

#define mmNIC1_QM0_CQ_CFG0_0                                         0xD20110

#define mmNIC1_QM0_CQ_CFG0_1                                         0xD20114

#define mmNIC1_QM0_CQ_CFG0_2                                         0xD20118

#define mmNIC1_QM0_CQ_CFG0_3                                         0xD2011C

#define mmNIC1_QM0_CQ_CFG0_4                                         0xD20120

#define mmNIC1_QM0_CQ_CFG1_0                                         0xD20124

#define mmNIC1_QM0_CQ_CFG1_1                                         0xD20128

#define mmNIC1_QM0_CQ_CFG1_2                                         0xD2012C

#define mmNIC1_QM0_CQ_CFG1_3                                         0xD20130

#define mmNIC1_QM0_CQ_CFG1_4                                         0xD20134

#define mmNIC1_QM0_CQ_ARUSER_31_11_0                                 0xD20138

#define mmNIC1_QM0_CQ_ARUSER_31_11_1                                 0xD2013C

#define mmNIC1_QM0_CQ_ARUSER_31_11_2                                 0xD20140

#define mmNIC1_QM0_CQ_ARUSER_31_11_3                                 0xD20144

#define mmNIC1_QM0_CQ_ARUSER_31_11_4                                 0xD20148

#define mmNIC1_QM0_CQ_STS0_0                                         0xD2014C

#define mmNIC1_QM0_CQ_STS0_1                                         0xD20150

#define mmNIC1_QM0_CQ_STS0_2                                         0xD20154

#define mmNIC1_QM0_CQ_STS0_3                                         0xD20158

#define mmNIC1_QM0_CQ_STS0_4                                         0xD2015C

#define mmNIC1_QM0_CQ_STS1_0                                         0xD20160

#define mmNIC1_QM0_CQ_STS1_1                                         0xD20164

#define mmNIC1_QM0_CQ_STS1_2                                         0xD20168

#define mmNIC1_QM0_CQ_STS1_3                                         0xD2016C

#define mmNIC1_QM0_CQ_STS1_4                                         0xD20170

#define mmNIC1_QM0_CQ_PTR_LO_0                                       0xD20174

#define mmNIC1_QM0_CQ_PTR_HI_0                                       0xD20178

#define mmNIC1_QM0_CQ_TSIZE_0                                        0xD2017C

#define mmNIC1_QM0_CQ_CTL_0                                          0xD20180

#define mmNIC1_QM0_CQ_PTR_LO_1                                       0xD20184

#define mmNIC1_QM0_CQ_PTR_HI_1                                       0xD20188

#define mmNIC1_QM0_CQ_TSIZE_1                                        0xD2018C

#define mmNIC1_QM0_CQ_CTL_1                                          0xD20190

#define mmNIC1_QM0_CQ_PTR_LO_2                                       0xD20194

#define mmNIC1_QM0_CQ_PTR_HI_2                                       0xD20198

#define mmNIC1_QM0_CQ_TSIZE_2                                        0xD2019C

#define mmNIC1_QM0_CQ_CTL_2                                          0xD201A0

#define mmNIC1_QM0_CQ_PTR_LO_3                                       0xD201A4

#define mmNIC1_QM0_CQ_PTR_HI_3                                       0xD201A8

#define mmNIC1_QM0_CQ_TSIZE_3                                        0xD201AC

#define mmNIC1_QM0_CQ_CTL_3                                          0xD201B0

#define mmNIC1_QM0_CQ_PTR_LO_4                                       0xD201B4

#define mmNIC1_QM0_CQ_PTR_HI_4                                       0xD201B8

#define mmNIC1_QM0_CQ_TSIZE_4                                        0xD201BC

#define mmNIC1_QM0_CQ_CTL_4                                          0xD201C0

#define mmNIC1_QM0_CQ_PTR_LO_STS_0                                   0xD201C4

#define mmNIC1_QM0_CQ_PTR_LO_STS_1                                   0xD201C8

#define mmNIC1_QM0_CQ_PTR_LO_STS_2                                   0xD201CC

#define mmNIC1_QM0_CQ_PTR_LO_STS_3                                   0xD201D0

#define mmNIC1_QM0_CQ_PTR_LO_STS_4                                   0xD201D4

#define mmNIC1_QM0_CQ_PTR_HI_STS_0                                   0xD201D8

#define mmNIC1_QM0_CQ_PTR_HI_STS_1                                   0xD201DC

#define mmNIC1_QM0_CQ_PTR_HI_STS_2                                   0xD201E0

#define mmNIC1_QM0_CQ_PTR_HI_STS_3                                   0xD201E4

#define mmNIC1_QM0_CQ_PTR_HI_STS_4                                   0xD201E8

#define mmNIC1_QM0_CQ_TSIZE_STS_0                                    0xD201EC

#define mmNIC1_QM0_CQ_TSIZE_STS_1                                    0xD201F0

#define mmNIC1_QM0_CQ_TSIZE_STS_2                                    0xD201F4

#define mmNIC1_QM0_CQ_TSIZE_STS_3                                    0xD201F8

#define mmNIC1_QM0_CQ_TSIZE_STS_4                                    0xD201FC

#define mmNIC1_QM0_CQ_CTL_STS_0                                      0xD20200

#define mmNIC1_QM0_CQ_CTL_STS_1                                      0xD20204

#define mmNIC1_QM0_CQ_CTL_STS_2                                      0xD20208

#define mmNIC1_QM0_CQ_CTL_STS_3                                      0xD2020C

#define mmNIC1_QM0_CQ_CTL_STS_4                                      0xD20210

#define mmNIC1_QM0_CQ_IFIFO_CNT_0                                    0xD20214

#define mmNIC1_QM0_CQ_IFIFO_CNT_1                                    0xD20218

#define mmNIC1_QM0_CQ_IFIFO_CNT_2                                    0xD2021C

#define mmNIC1_QM0_CQ_IFIFO_CNT_3                                    0xD20220

#define mmNIC1_QM0_CQ_IFIFO_CNT_4                                    0xD20224

#define mmNIC1_QM0_CP_MSG_BASE0_ADDR_LO_0                            0xD20228

#define mmNIC1_QM0_CP_MSG_BASE0_ADDR_LO_1                            0xD2022C

#define mmNIC1_QM0_CP_MSG_BASE0_ADDR_LO_2                            0xD20230

#define mmNIC1_QM0_CP_MSG_BASE0_ADDR_LO_3                            0xD20234

#define mmNIC1_QM0_CP_MSG_BASE0_ADDR_LO_4                            0xD20238

#define mmNIC1_QM0_CP_MSG_BASE0_ADDR_HI_0                            0xD2023C

#define mmNIC1_QM0_CP_MSG_BASE0_ADDR_HI_1                            0xD20240

#define mmNIC1_QM0_CP_MSG_BASE0_ADDR_HI_2                            0xD20244

#define mmNIC1_QM0_CP_MSG_BASE0_ADDR_HI_3                            0xD20248

#define mmNIC1_QM0_CP_MSG_BASE0_ADDR_HI_4                            0xD2024C

#define mmNIC1_QM0_CP_MSG_BASE1_ADDR_LO_0                            0xD20250

#define mmNIC1_QM0_CP_MSG_BASE1_ADDR_LO_1                            0xD20254

#define mmNIC1_QM0_CP_MSG_BASE1_ADDR_LO_2                            0xD20258

#define mmNIC1_QM0_CP_MSG_BASE1_ADDR_LO_3                            0xD2025C

#define mmNIC1_QM0_CP_MSG_BASE1_ADDR_LO_4                            0xD20260

#define mmNIC1_QM0_CP_MSG_BASE1_ADDR_HI_0                            0xD20264

#define mmNIC1_QM0_CP_MSG_BASE1_ADDR_HI_1                            0xD20268

#define mmNIC1_QM0_CP_MSG_BASE1_ADDR_HI_2                            0xD2026C

#define mmNIC1_QM0_CP_MSG_BASE1_ADDR_HI_3                            0xD20270

#define mmNIC1_QM0_CP_MSG_BASE1_ADDR_HI_4                            0xD20274

#define mmNIC1_QM0_CP_MSG_BASE2_ADDR_LO_0                            0xD20278

#define mmNIC1_QM0_CP_MSG_BASE2_ADDR_LO_1                            0xD2027C

#define mmNIC1_QM0_CP_MSG_BASE2_ADDR_LO_2                            0xD20280

#define mmNIC1_QM0_CP_MSG_BASE2_ADDR_LO_3                            0xD20284

#define mmNIC1_QM0_CP_MSG_BASE2_ADDR_LO_4                            0xD20288

#define mmNIC1_QM0_CP_MSG_BASE2_ADDR_HI_0                            0xD2028C

#define mmNIC1_QM0_CP_MSG_BASE2_ADDR_HI_1                            0xD20290

#define mmNIC1_QM0_CP_MSG_BASE2_ADDR_HI_2                            0xD20294

#define mmNIC1_QM0_CP_MSG_BASE2_ADDR_HI_3                            0xD20298

#define mmNIC1_QM0_CP_MSG_BASE2_ADDR_HI_4                            0xD2029C

#define mmNIC1_QM0_CP_MSG_BASE3_ADDR_LO_0                            0xD202A0

#define mmNIC1_QM0_CP_MSG_BASE3_ADDR_LO_1                            0xD202A4

#define mmNIC1_QM0_CP_MSG_BASE3_ADDR_LO_2                            0xD202A8

#define mmNIC1_QM0_CP_MSG_BASE3_ADDR_LO_3                            0xD202AC

#define mmNIC1_QM0_CP_MSG_BASE3_ADDR_LO_4                            0xD202B0

#define mmNIC1_QM0_CP_MSG_BASE3_ADDR_HI_0                            0xD202B4

#define mmNIC1_QM0_CP_MSG_BASE3_ADDR_HI_1                            0xD202B8

#define mmNIC1_QM0_CP_MSG_BASE3_ADDR_HI_2                            0xD202BC

#define mmNIC1_QM0_CP_MSG_BASE3_ADDR_HI_3                            0xD202C0

#define mmNIC1_QM0_CP_MSG_BASE3_ADDR_HI_4                            0xD202C4

#define mmNIC1_QM0_CP_LDMA_TSIZE_OFFSET_0                            0xD202C8

#define mmNIC1_QM0_CP_LDMA_TSIZE_OFFSET_1                            0xD202CC

#define mmNIC1_QM0_CP_LDMA_TSIZE_OFFSET_2                            0xD202D0

#define mmNIC1_QM0_CP_LDMA_TSIZE_OFFSET_3                            0xD202D4

#define mmNIC1_QM0_CP_LDMA_TSIZE_OFFSET_4                            0xD202D8

#define mmNIC1_QM0_CP_LDMA_SRC_BASE_LO_OFFSET_0                      0xD202E0

#define mmNIC1_QM0_CP_LDMA_SRC_BASE_LO_OFFSET_1                      0xD202E4

#define mmNIC1_QM0_CP_LDMA_SRC_BASE_LO_OFFSET_2                      0xD202E8

#define mmNIC1_QM0_CP_LDMA_SRC_BASE_LO_OFFSET_3                      0xD202EC

#define mmNIC1_QM0_CP_LDMA_SRC_BASE_LO_OFFSET_4                      0xD202F0

#define mmNIC1_QM0_CP_LDMA_DST_BASE_LO_OFFSET_0                      0xD202F4

#define mmNIC1_QM0_CP_LDMA_DST_BASE_LO_OFFSET_1                      0xD202F8

#define mmNIC1_QM0_CP_LDMA_DST_BASE_LO_OFFSET_2                      0xD202FC

#define mmNIC1_QM0_CP_LDMA_DST_BASE_LO_OFFSET_3                      0xD20300

#define mmNIC1_QM0_CP_LDMA_DST_BASE_LO_OFFSET_4                      0xD20304

#define mmNIC1_QM0_CP_FENCE0_RDATA_0                                 0xD20308

#define mmNIC1_QM0_CP_FENCE0_RDATA_1                                 0xD2030C

#define mmNIC1_QM0_CP_FENCE0_RDATA_2                                 0xD20310

#define mmNIC1_QM0_CP_FENCE0_RDATA_3                                 0xD20314

#define mmNIC1_QM0_CP_FENCE0_RDATA_4                                 0xD20318

#define mmNIC1_QM0_CP_FENCE1_RDATA_0                                 0xD2031C

#define mmNIC1_QM0_CP_FENCE1_RDATA_1                                 0xD20320

#define mmNIC1_QM0_CP_FENCE1_RDATA_2                                 0xD20324

#define mmNIC1_QM0_CP_FENCE1_RDATA_3                                 0xD20328

#define mmNIC1_QM0_CP_FENCE1_RDATA_4                                 0xD2032C

#define mmNIC1_QM0_CP_FENCE2_RDATA_0                                 0xD20330

#define mmNIC1_QM0_CP_FENCE2_RDATA_1                                 0xD20334

#define mmNIC1_QM0_CP_FENCE2_RDATA_2                                 0xD20338

#define mmNIC1_QM0_CP_FENCE2_RDATA_3                                 0xD2033C

#define mmNIC1_QM0_CP_FENCE2_RDATA_4                                 0xD20340

#define mmNIC1_QM0_CP_FENCE3_RDATA_0                                 0xD20344

#define mmNIC1_QM0_CP_FENCE3_RDATA_1                                 0xD20348

#define mmNIC1_QM0_CP_FENCE3_RDATA_2                                 0xD2034C

#define mmNIC1_QM0_CP_FENCE3_RDATA_3                                 0xD20350

#define mmNIC1_QM0_CP_FENCE3_RDATA_4                                 0xD20354

#define mmNIC1_QM0_CP_FENCE0_CNT_0                                   0xD20358

#define mmNIC1_QM0_CP_FENCE0_CNT_1                                   0xD2035C

#define mmNIC1_QM0_CP_FENCE0_CNT_2                                   0xD20360

#define mmNIC1_QM0_CP_FENCE0_CNT_3                                   0xD20364

#define mmNIC1_QM0_CP_FENCE0_CNT_4                                   0xD20368

#define mmNIC1_QM0_CP_FENCE1_CNT_0                                   0xD2036C

#define mmNIC1_QM0_CP_FENCE1_CNT_1                                   0xD20370

#define mmNIC1_QM0_CP_FENCE1_CNT_2                                   0xD20374

#define mmNIC1_QM0_CP_FENCE1_CNT_3                                   0xD20378

#define mmNIC1_QM0_CP_FENCE1_CNT_4                                   0xD2037C

#define mmNIC1_QM0_CP_FENCE2_CNT_0                                   0xD20380

#define mmNIC1_QM0_CP_FENCE2_CNT_1                                   0xD20384

#define mmNIC1_QM0_CP_FENCE2_CNT_2                                   0xD20388

#define mmNIC1_QM0_CP_FENCE2_CNT_3                                   0xD2038C

#define mmNIC1_QM0_CP_FENCE2_CNT_4                                   0xD20390

#define mmNIC1_QM0_CP_FENCE3_CNT_0                                   0xD20394

#define mmNIC1_QM0_CP_FENCE3_CNT_1                                   0xD20398

#define mmNIC1_QM0_CP_FENCE3_CNT_2                                   0xD2039C

#define mmNIC1_QM0_CP_FENCE3_CNT_3                                   0xD203A0

#define mmNIC1_QM0_CP_FENCE3_CNT_4                                   0xD203A4

#define mmNIC1_QM0_CP_STS_0                                          0xD203A8

#define mmNIC1_QM0_CP_STS_1                                          0xD203AC

#define mmNIC1_QM0_CP_STS_2                                          0xD203B0

#define mmNIC1_QM0_CP_STS_3                                          0xD203B4

#define mmNIC1_QM0_CP_STS_4                                          0xD203B8

#define mmNIC1_QM0_CP_CURRENT_INST_LO_0                              0xD203BC

#define mmNIC1_QM0_CP_CURRENT_INST_LO_1                              0xD203C0

#define mmNIC1_QM0_CP_CURRENT_INST_LO_2                              0xD203C4

#define mmNIC1_QM0_CP_CURRENT_INST_LO_3                              0xD203C8

#define mmNIC1_QM0_CP_CURRENT_INST_LO_4                              0xD203CC

#define mmNIC1_QM0_CP_CURRENT_INST_HI_0                              0xD203D0

#define mmNIC1_QM0_CP_CURRENT_INST_HI_1                              0xD203D4

#define mmNIC1_QM0_CP_CURRENT_INST_HI_2                              0xD203D8

#define mmNIC1_QM0_CP_CURRENT_INST_HI_3                              0xD203DC

#define mmNIC1_QM0_CP_CURRENT_INST_HI_4                              0xD203E0

#define mmNIC1_QM0_CP_BARRIER_CFG_0                                  0xD203F4

#define mmNIC1_QM0_CP_BARRIER_CFG_1                                  0xD203F8

#define mmNIC1_QM0_CP_BARRIER_CFG_2                                  0xD203FC

#define mmNIC1_QM0_CP_BARRIER_CFG_3                                  0xD20400

#define mmNIC1_QM0_CP_BARRIER_CFG_4                                  0xD20404

#define mmNIC1_QM0_CP_DBG_0_0                                        0xD20408

#define mmNIC1_QM0_CP_DBG_0_1                                        0xD2040C

#define mmNIC1_QM0_CP_DBG_0_2                                        0xD20410

#define mmNIC1_QM0_CP_DBG_0_3                                        0xD20414

#define mmNIC1_QM0_CP_DBG_0_4                                        0xD20418

#define mmNIC1_QM0_CP_ARUSER_31_11_0                                 0xD2041C

#define mmNIC1_QM0_CP_ARUSER_31_11_1                                 0xD20420

#define mmNIC1_QM0_CP_ARUSER_31_11_2                                 0xD20424

#define mmNIC1_QM0_CP_ARUSER_31_11_3                                 0xD20428

#define mmNIC1_QM0_CP_ARUSER_31_11_4                                 0xD2042C

#define mmNIC1_QM0_CP_AWUSER_31_11_0                                 0xD20430

#define mmNIC1_QM0_CP_AWUSER_31_11_1                                 0xD20434

#define mmNIC1_QM0_CP_AWUSER_31_11_2                                 0xD20438

#define mmNIC1_QM0_CP_AWUSER_31_11_3                                 0xD2043C

#define mmNIC1_QM0_CP_AWUSER_31_11_4                                 0xD20440

#define mmNIC1_QM0_ARB_CFG_0                                         0xD20A00

#define mmNIC1_QM0_ARB_CHOISE_Q_PUSH                                 0xD20A04

#define mmNIC1_QM0_ARB_WRR_WEIGHT_0                                  0xD20A08

#define mmNIC1_QM0_ARB_WRR_WEIGHT_1                                  0xD20A0C

#define mmNIC1_QM0_ARB_WRR_WEIGHT_2                                  0xD20A10

#define mmNIC1_QM0_ARB_WRR_WEIGHT_3                                  0xD20A14

#define mmNIC1_QM0_ARB_CFG_1                                         0xD20A18

#define mmNIC1_QM0_ARB_MST_AVAIL_CRED_0                              0xD20A20

#define mmNIC1_QM0_ARB_MST_AVAIL_CRED_1                              0xD20A24

#define mmNIC1_QM0_ARB_MST_AVAIL_CRED_2                              0xD20A28

#define mmNIC1_QM0_ARB_MST_AVAIL_CRED_3                              0xD20A2C

#define mmNIC1_QM0_ARB_MST_AVAIL_CRED_4                              0xD20A30

#define mmNIC1_QM0_ARB_MST_AVAIL_CRED_5                              0xD20A34

#define mmNIC1_QM0_ARB_MST_AVAIL_CRED_6                              0xD20A38

#define mmNIC1_QM0_ARB_MST_AVAIL_CRED_7                              0xD20A3C

#define mmNIC1_QM0_ARB_MST_AVAIL_CRED_8                              0xD20A40

#define mmNIC1_QM0_ARB_MST_AVAIL_CRED_9                              0xD20A44

#define mmNIC1_QM0_ARB_MST_AVAIL_CRED_10                             0xD20A48

#define mmNIC1_QM0_ARB_MST_AVAIL_CRED_11                             0xD20A4C

#define mmNIC1_QM0_ARB_MST_AVAIL_CRED_12                             0xD20A50

#define mmNIC1_QM0_ARB_MST_AVAIL_CRED_13                             0xD20A54

#define mmNIC1_QM0_ARB_MST_AVAIL_CRED_14                             0xD20A58

#define mmNIC1_QM0_ARB_MST_AVAIL_CRED_15                             0xD20A5C

#define mmNIC1_QM0_ARB_MST_AVAIL_CRED_16                             0xD20A60

#define mmNIC1_QM0_ARB_MST_AVAIL_CRED_17                             0xD20A64

#define mmNIC1_QM0_ARB_MST_AVAIL_CRED_18                             0xD20A68

#define mmNIC1_QM0_ARB_MST_AVAIL_CRED_19                             0xD20A6C

#define mmNIC1_QM0_ARB_MST_AVAIL_CRED_20                             0xD20A70

#define mmNIC1_QM0_ARB_MST_AVAIL_CRED_21                             0xD20A74

#define mmNIC1_QM0_ARB_MST_AVAIL_CRED_22                             0xD20A78

#define mmNIC1_QM0_ARB_MST_AVAIL_CRED_23                             0xD20A7C

#define mmNIC1_QM0_ARB_MST_AVAIL_CRED_24                             0xD20A80

#define mmNIC1_QM0_ARB_MST_AVAIL_CRED_25                             0xD20A84

#define mmNIC1_QM0_ARB_MST_AVAIL_CRED_26                             0xD20A88

#define mmNIC1_QM0_ARB_MST_AVAIL_CRED_27                             0xD20A8C

#define mmNIC1_QM0_ARB_MST_AVAIL_CRED_28                             0xD20A90

#define mmNIC1_QM0_ARB_MST_AVAIL_CRED_29                             0xD20A94

#define mmNIC1_QM0_ARB_MST_AVAIL_CRED_30                             0xD20A98

#define mmNIC1_QM0_ARB_MST_AVAIL_CRED_31                             0xD20A9C

#define mmNIC1_QM0_ARB_MST_CRED_INC                                  0xD20AA0

#define mmNIC1_QM0_ARB_MST_CHOISE_PUSH_OFST_0                        0xD20AA4

#define mmNIC1_QM0_ARB_MST_CHOISE_PUSH_OFST_1                        0xD20AA8

#define mmNIC1_QM0_ARB_MST_CHOISE_PUSH_OFST_2                        0xD20AAC

#define mmNIC1_QM0_ARB_MST_CHOISE_PUSH_OFST_3                        0xD20AB0

#define mmNIC1_QM0_ARB_MST_CHOISE_PUSH_OFST_4                        0xD20AB4

#define mmNIC1_QM0_ARB_MST_CHOISE_PUSH_OFST_5                        0xD20AB8

#define mmNIC1_QM0_ARB_MST_CHOISE_PUSH_OFST_6                        0xD20ABC

#define mmNIC1_QM0_ARB_MST_CHOISE_PUSH_OFST_7                        0xD20AC0

#define mmNIC1_QM0_ARB_MST_CHOISE_PUSH_OFST_8                        0xD20AC4

#define mmNIC1_QM0_ARB_MST_CHOISE_PUSH_OFST_9                        0xD20AC8

#define mmNIC1_QM0_ARB_MST_CHOISE_PUSH_OFST_10                       0xD20ACC

#define mmNIC1_QM0_ARB_MST_CHOISE_PUSH_OFST_11                       0xD20AD0

#define mmNIC1_QM0_ARB_MST_CHOISE_PUSH_OFST_12                       0xD20AD4

#define mmNIC1_QM0_ARB_MST_CHOISE_PUSH_OFST_13                       0xD20AD8

#define mmNIC1_QM0_ARB_MST_CHOISE_PUSH_OFST_14                       0xD20ADC

#define mmNIC1_QM0_ARB_MST_CHOISE_PUSH_OFST_15                       0xD20AE0

#define mmNIC1_QM0_ARB_MST_CHOISE_PUSH_OFST_16                       0xD20AE4

#define mmNIC1_QM0_ARB_MST_CHOISE_PUSH_OFST_17                       0xD20AE8

#define mmNIC1_QM0_ARB_MST_CHOISE_PUSH_OFST_18                       0xD20AEC

#define mmNIC1_QM0_ARB_MST_CHOISE_PUSH_OFST_19                       0xD20AF0

#define mmNIC1_QM0_ARB_MST_CHOISE_PUSH_OFST_20                       0xD20AF4

#define mmNIC1_QM0_ARB_MST_CHOISE_PUSH_OFST_21                       0xD20AF8

#define mmNIC1_QM0_ARB_MST_CHOISE_PUSH_OFST_22                       0xD20AFC

#define mmNIC1_QM0_ARB_MST_CHOISE_PUSH_OFST_23                       0xD20B00

#define mmNIC1_QM0_ARB_MST_CHOISE_PUSH_OFST_24                       0xD20B04

#define mmNIC1_QM0_ARB_MST_CHOISE_PUSH_OFST_25                       0xD20B08

#define mmNIC1_QM0_ARB_MST_CHOISE_PUSH_OFST_26                       0xD20B0C

#define mmNIC1_QM0_ARB_MST_CHOISE_PUSH_OFST_27                       0xD20B10

#define mmNIC1_QM0_ARB_MST_CHOISE_PUSH_OFST_28                       0xD20B14

#define mmNIC1_QM0_ARB_MST_CHOISE_PUSH_OFST_29                       0xD20B18

#define mmNIC1_QM0_ARB_MST_CHOISE_PUSH_OFST_30                       0xD20B1C

#define mmNIC1_QM0_ARB_MST_CHOISE_PUSH_OFST_31                       0xD20B20

#define mmNIC1_QM0_ARB_SLV_MASTER_INC_CRED_OFST                      0xD20B28

#define mmNIC1_QM0_ARB_MST_SLAVE_EN                                  0xD20B2C

#define mmNIC1_QM0_ARB_MST_QUIET_PER                                 0xD20B34

#define mmNIC1_QM0_ARB_SLV_CHOISE_WDT                                0xD20B38

#define mmNIC1_QM0_ARB_SLV_ID                                        0xD20B3C

#define mmNIC1_QM0_ARB_MSG_MAX_INFLIGHT                              0xD20B44

#define mmNIC1_QM0_ARB_MSG_AWUSER_31_11                              0xD20B48

#define mmNIC1_QM0_ARB_MSG_AWUSER_SEC_PROP                           0xD20B4C

#define mmNIC1_QM0_ARB_MSG_AWUSER_NON_SEC_PROP                       0xD20B50

#define mmNIC1_QM0_ARB_BASE_LO                                       0xD20B54

#define mmNIC1_QM0_ARB_BASE_HI                                       0xD20B58

#define mmNIC1_QM0_ARB_STATE_STS                                     0xD20B80

#define mmNIC1_QM0_ARB_CHOISE_FULLNESS_STS                           0xD20B84

#define mmNIC1_QM0_ARB_MSG_STS                                       0xD20B88

#define mmNIC1_QM0_ARB_SLV_CHOISE_Q_HEAD                             0xD20B8C

#define mmNIC1_QM0_ARB_ERR_CAUSE                                     0xD20B9C

#define mmNIC1_QM0_ARB_ERR_MSG_EN                                    0xD20BA0

#define mmNIC1_QM0_ARB_ERR_STS_DRP                                   0xD20BA8

#define mmNIC1_QM0_ARB_MST_CRED_STS_0                                0xD20BB0

#define mmNIC1_QM0_ARB_MST_CRED_STS_1                                0xD20BB4

#define mmNIC1_QM0_ARB_MST_CRED_STS_2                                0xD20BB8

#define mmNIC1_QM0_ARB_MST_CRED_STS_3                                0xD20BBC

#define mmNIC1_QM0_ARB_MST_CRED_STS_4                                0xD20BC0

#define mmNIC1_QM0_ARB_MST_CRED_STS_5                                0xD20BC4

#define mmNIC1_QM0_ARB_MST_CRED_STS_6                                0xD20BC8

#define mmNIC1_QM0_ARB_MST_CRED_STS_7                                0xD20BCC

#define mmNIC1_QM0_ARB_MST_CRED_STS_8                                0xD20BD0

#define mmNIC1_QM0_ARB_MST_CRED_STS_9                                0xD20BD4

#define mmNIC1_QM0_ARB_MST_CRED_STS_10                               0xD20BD8

#define mmNIC1_QM0_ARB_MST_CRED_STS_11                               0xD20BDC

#define mmNIC1_QM0_ARB_MST_CRED_STS_12                               0xD20BE0

#define mmNIC1_QM0_ARB_MST_CRED_STS_13                               0xD20BE4

#define mmNIC1_QM0_ARB_MST_CRED_STS_14                               0xD20BE8

#define mmNIC1_QM0_ARB_MST_CRED_STS_15                               0xD20BEC

#define mmNIC1_QM0_ARB_MST_CRED_STS_16                               0xD20BF0

#define mmNIC1_QM0_ARB_MST_CRED_STS_17                               0xD20BF4

#define mmNIC1_QM0_ARB_MST_CRED_STS_18                               0xD20BF8

#define mmNIC1_QM0_ARB_MST_CRED_STS_19                               0xD20BFC

#define mmNIC1_QM0_ARB_MST_CRED_STS_20                               0xD20C00

#define mmNIC1_QM0_ARB_MST_CRED_STS_21                               0xD20C04

#define mmNIC1_QM0_ARB_MST_CRED_STS_22                               0xD20C08

#define mmNIC1_QM0_ARB_MST_CRED_STS_23                               0xD20C0C

#define mmNIC1_QM0_ARB_MST_CRED_STS_24                               0xD20C10

#define mmNIC1_QM0_ARB_MST_CRED_STS_25                               0xD20C14

#define mmNIC1_QM0_ARB_MST_CRED_STS_26                               0xD20C18

#define mmNIC1_QM0_ARB_MST_CRED_STS_27                               0xD20C1C

#define mmNIC1_QM0_ARB_MST_CRED_STS_28                               0xD20C20

#define mmNIC1_QM0_ARB_MST_CRED_STS_29                               0xD20C24

#define mmNIC1_QM0_ARB_MST_CRED_STS_30                               0xD20C28

#define mmNIC1_QM0_ARB_MST_CRED_STS_31                               0xD20C2C

#define mmNIC1_QM0_CGM_CFG                                           0xD20C70

#define mmNIC1_QM0_CGM_STS                                           0xD20C74

#define mmNIC1_QM0_CGM_CFG1                                          0xD20C78

#define mmNIC1_QM0_LOCAL_RANGE_BASE                                  0xD20C80

#define mmNIC1_QM0_LOCAL_RANGE_SIZE                                  0xD20C84

#define mmNIC1_QM0_CSMR_STRICT_PRIO_CFG                              0xD20C90

#define mmNIC1_QM0_HBW_RD_RATE_LIM_CFG_1                             0xD20C94

#define mmNIC1_QM0_LBW_WR_RATE_LIM_CFG_0                             0xD20C98

#define mmNIC1_QM0_LBW_WR_RATE_LIM_CFG_1                             0xD20C9C

#define mmNIC1_QM0_HBW_RD_RATE_LIM_CFG_0                             0xD20CA0

#define mmNIC1_QM0_GLBL_AXCACHE                                      0xD20CA4

#define mmNIC1_QM0_IND_GW_APB_CFG                                    0xD20CB0

#define mmNIC1_QM0_IND_GW_APB_WDATA                                  0xD20CB4

#define mmNIC1_QM0_IND_GW_APB_RDATA                                  0xD20CB8

#define mmNIC1_QM0_IND_GW_APB_STATUS                                 0xD20CBC

#define mmNIC1_QM0_GLBL_ERR_ADDR_LO                                  0xD20CD0

#define mmNIC1_QM0_GLBL_ERR_ADDR_HI                                  0xD20CD4

#define mmNIC1_QM0_GLBL_ERR_WDATA                                    0xD20CD8

#define mmNIC1_QM0_GLBL_MEM_INIT_BUSY                                0xD20D00

#endif /* ASIC_REG_NIC1_QM0_REGS_H_ */
