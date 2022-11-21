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

#ifndef ASIC_REG_NIC1_QM1_REGS_H_
#define ASIC_REG_NIC1_QM1_REGS_H_

/*
 *****************************************
 *   NIC1_QM1 (Prototype: QMAN)
 *****************************************
 */

#define mmNIC1_QM1_GLBL_CFG0                                         0xD22000

#define mmNIC1_QM1_GLBL_CFG1                                         0xD22004

#define mmNIC1_QM1_GLBL_PROT                                         0xD22008

#define mmNIC1_QM1_GLBL_ERR_CFG                                      0xD2200C

#define mmNIC1_QM1_GLBL_SECURE_PROPS_0                               0xD22010

#define mmNIC1_QM1_GLBL_SECURE_PROPS_1                               0xD22014

#define mmNIC1_QM1_GLBL_SECURE_PROPS_2                               0xD22018

#define mmNIC1_QM1_GLBL_SECURE_PROPS_3                               0xD2201C

#define mmNIC1_QM1_GLBL_SECURE_PROPS_4                               0xD22020

#define mmNIC1_QM1_GLBL_NON_SECURE_PROPS_0                           0xD22024

#define mmNIC1_QM1_GLBL_NON_SECURE_PROPS_1                           0xD22028

#define mmNIC1_QM1_GLBL_NON_SECURE_PROPS_2                           0xD2202C

#define mmNIC1_QM1_GLBL_NON_SECURE_PROPS_3                           0xD22030

#define mmNIC1_QM1_GLBL_NON_SECURE_PROPS_4                           0xD22034

#define mmNIC1_QM1_GLBL_STS0                                         0xD22038

#define mmNIC1_QM1_GLBL_STS1_0                                       0xD22040

#define mmNIC1_QM1_GLBL_STS1_1                                       0xD22044

#define mmNIC1_QM1_GLBL_STS1_2                                       0xD22048

#define mmNIC1_QM1_GLBL_STS1_3                                       0xD2204C

#define mmNIC1_QM1_GLBL_STS1_4                                       0xD22050

#define mmNIC1_QM1_GLBL_MSG_EN_0                                     0xD22054

#define mmNIC1_QM1_GLBL_MSG_EN_1                                     0xD22058

#define mmNIC1_QM1_GLBL_MSG_EN_2                                     0xD2205C

#define mmNIC1_QM1_GLBL_MSG_EN_3                                     0xD22060

#define mmNIC1_QM1_GLBL_MSG_EN_4                                     0xD22068

#define mmNIC1_QM1_PQ_BASE_LO_0                                      0xD22070

#define mmNIC1_QM1_PQ_BASE_LO_1                                      0xD22074

#define mmNIC1_QM1_PQ_BASE_LO_2                                      0xD22078

#define mmNIC1_QM1_PQ_BASE_LO_3                                      0xD2207C

#define mmNIC1_QM1_PQ_BASE_HI_0                                      0xD22080

#define mmNIC1_QM1_PQ_BASE_HI_1                                      0xD22084

#define mmNIC1_QM1_PQ_BASE_HI_2                                      0xD22088

#define mmNIC1_QM1_PQ_BASE_HI_3                                      0xD2208C

#define mmNIC1_QM1_PQ_SIZE_0                                         0xD22090

#define mmNIC1_QM1_PQ_SIZE_1                                         0xD22094

#define mmNIC1_QM1_PQ_SIZE_2                                         0xD22098

#define mmNIC1_QM1_PQ_SIZE_3                                         0xD2209C

#define mmNIC1_QM1_PQ_PI_0                                           0xD220A0

#define mmNIC1_QM1_PQ_PI_1                                           0xD220A4

#define mmNIC1_QM1_PQ_PI_2                                           0xD220A8

#define mmNIC1_QM1_PQ_PI_3                                           0xD220AC

#define mmNIC1_QM1_PQ_CI_0                                           0xD220B0

#define mmNIC1_QM1_PQ_CI_1                                           0xD220B4

#define mmNIC1_QM1_PQ_CI_2                                           0xD220B8

#define mmNIC1_QM1_PQ_CI_3                                           0xD220BC

#define mmNIC1_QM1_PQ_CFG0_0                                         0xD220C0

#define mmNIC1_QM1_PQ_CFG0_1                                         0xD220C4

#define mmNIC1_QM1_PQ_CFG0_2                                         0xD220C8

#define mmNIC1_QM1_PQ_CFG0_3                                         0xD220CC

#define mmNIC1_QM1_PQ_CFG1_0                                         0xD220D0

#define mmNIC1_QM1_PQ_CFG1_1                                         0xD220D4

#define mmNIC1_QM1_PQ_CFG1_2                                         0xD220D8

#define mmNIC1_QM1_PQ_CFG1_3                                         0xD220DC

#define mmNIC1_QM1_PQ_ARUSER_31_11_0                                 0xD220E0

#define mmNIC1_QM1_PQ_ARUSER_31_11_1                                 0xD220E4

#define mmNIC1_QM1_PQ_ARUSER_31_11_2                                 0xD220E8

#define mmNIC1_QM1_PQ_ARUSER_31_11_3                                 0xD220EC

#define mmNIC1_QM1_PQ_STS0_0                                         0xD220F0

#define mmNIC1_QM1_PQ_STS0_1                                         0xD220F4

#define mmNIC1_QM1_PQ_STS0_2                                         0xD220F8

#define mmNIC1_QM1_PQ_STS0_3                                         0xD220FC

#define mmNIC1_QM1_PQ_STS1_0                                         0xD22100

#define mmNIC1_QM1_PQ_STS1_1                                         0xD22104

#define mmNIC1_QM1_PQ_STS1_2                                         0xD22108

#define mmNIC1_QM1_PQ_STS1_3                                         0xD2210C

#define mmNIC1_QM1_CQ_CFG0_0                                         0xD22110

#define mmNIC1_QM1_CQ_CFG0_1                                         0xD22114

#define mmNIC1_QM1_CQ_CFG0_2                                         0xD22118

#define mmNIC1_QM1_CQ_CFG0_3                                         0xD2211C

#define mmNIC1_QM1_CQ_CFG0_4                                         0xD22120

#define mmNIC1_QM1_CQ_CFG1_0                                         0xD22124

#define mmNIC1_QM1_CQ_CFG1_1                                         0xD22128

#define mmNIC1_QM1_CQ_CFG1_2                                         0xD2212C

#define mmNIC1_QM1_CQ_CFG1_3                                         0xD22130

#define mmNIC1_QM1_CQ_CFG1_4                                         0xD22134

#define mmNIC1_QM1_CQ_ARUSER_31_11_0                                 0xD22138

#define mmNIC1_QM1_CQ_ARUSER_31_11_1                                 0xD2213C

#define mmNIC1_QM1_CQ_ARUSER_31_11_2                                 0xD22140

#define mmNIC1_QM1_CQ_ARUSER_31_11_3                                 0xD22144

#define mmNIC1_QM1_CQ_ARUSER_31_11_4                                 0xD22148

#define mmNIC1_QM1_CQ_STS0_0                                         0xD2214C

#define mmNIC1_QM1_CQ_STS0_1                                         0xD22150

#define mmNIC1_QM1_CQ_STS0_2                                         0xD22154

#define mmNIC1_QM1_CQ_STS0_3                                         0xD22158

#define mmNIC1_QM1_CQ_STS0_4                                         0xD2215C

#define mmNIC1_QM1_CQ_STS1_0                                         0xD22160

#define mmNIC1_QM1_CQ_STS1_1                                         0xD22164

#define mmNIC1_QM1_CQ_STS1_2                                         0xD22168

#define mmNIC1_QM1_CQ_STS1_3                                         0xD2216C

#define mmNIC1_QM1_CQ_STS1_4                                         0xD22170

#define mmNIC1_QM1_CQ_PTR_LO_0                                       0xD22174

#define mmNIC1_QM1_CQ_PTR_HI_0                                       0xD22178

#define mmNIC1_QM1_CQ_TSIZE_0                                        0xD2217C

#define mmNIC1_QM1_CQ_CTL_0                                          0xD22180

#define mmNIC1_QM1_CQ_PTR_LO_1                                       0xD22184

#define mmNIC1_QM1_CQ_PTR_HI_1                                       0xD22188

#define mmNIC1_QM1_CQ_TSIZE_1                                        0xD2218C

#define mmNIC1_QM1_CQ_CTL_1                                          0xD22190

#define mmNIC1_QM1_CQ_PTR_LO_2                                       0xD22194

#define mmNIC1_QM1_CQ_PTR_HI_2                                       0xD22198

#define mmNIC1_QM1_CQ_TSIZE_2                                        0xD2219C

#define mmNIC1_QM1_CQ_CTL_2                                          0xD221A0

#define mmNIC1_QM1_CQ_PTR_LO_3                                       0xD221A4

#define mmNIC1_QM1_CQ_PTR_HI_3                                       0xD221A8

#define mmNIC1_QM1_CQ_TSIZE_3                                        0xD221AC

#define mmNIC1_QM1_CQ_CTL_3                                          0xD221B0

#define mmNIC1_QM1_CQ_PTR_LO_4                                       0xD221B4

#define mmNIC1_QM1_CQ_PTR_HI_4                                       0xD221B8

#define mmNIC1_QM1_CQ_TSIZE_4                                        0xD221BC

#define mmNIC1_QM1_CQ_CTL_4                                          0xD221C0

#define mmNIC1_QM1_CQ_PTR_LO_STS_0                                   0xD221C4

#define mmNIC1_QM1_CQ_PTR_LO_STS_1                                   0xD221C8

#define mmNIC1_QM1_CQ_PTR_LO_STS_2                                   0xD221CC

#define mmNIC1_QM1_CQ_PTR_LO_STS_3                                   0xD221D0

#define mmNIC1_QM1_CQ_PTR_LO_STS_4                                   0xD221D4

#define mmNIC1_QM1_CQ_PTR_HI_STS_0                                   0xD221D8

#define mmNIC1_QM1_CQ_PTR_HI_STS_1                                   0xD221DC

#define mmNIC1_QM1_CQ_PTR_HI_STS_2                                   0xD221E0

#define mmNIC1_QM1_CQ_PTR_HI_STS_3                                   0xD221E4

#define mmNIC1_QM1_CQ_PTR_HI_STS_4                                   0xD221E8

#define mmNIC1_QM1_CQ_TSIZE_STS_0                                    0xD221EC

#define mmNIC1_QM1_CQ_TSIZE_STS_1                                    0xD221F0

#define mmNIC1_QM1_CQ_TSIZE_STS_2                                    0xD221F4

#define mmNIC1_QM1_CQ_TSIZE_STS_3                                    0xD221F8

#define mmNIC1_QM1_CQ_TSIZE_STS_4                                    0xD221FC

#define mmNIC1_QM1_CQ_CTL_STS_0                                      0xD22200

#define mmNIC1_QM1_CQ_CTL_STS_1                                      0xD22204

#define mmNIC1_QM1_CQ_CTL_STS_2                                      0xD22208

#define mmNIC1_QM1_CQ_CTL_STS_3                                      0xD2220C

#define mmNIC1_QM1_CQ_CTL_STS_4                                      0xD22210

#define mmNIC1_QM1_CQ_IFIFO_CNT_0                                    0xD22214

#define mmNIC1_QM1_CQ_IFIFO_CNT_1                                    0xD22218

#define mmNIC1_QM1_CQ_IFIFO_CNT_2                                    0xD2221C

#define mmNIC1_QM1_CQ_IFIFO_CNT_3                                    0xD22220

#define mmNIC1_QM1_CQ_IFIFO_CNT_4                                    0xD22224

#define mmNIC1_QM1_CP_MSG_BASE0_ADDR_LO_0                            0xD22228

#define mmNIC1_QM1_CP_MSG_BASE0_ADDR_LO_1                            0xD2222C

#define mmNIC1_QM1_CP_MSG_BASE0_ADDR_LO_2                            0xD22230

#define mmNIC1_QM1_CP_MSG_BASE0_ADDR_LO_3                            0xD22234

#define mmNIC1_QM1_CP_MSG_BASE0_ADDR_LO_4                            0xD22238

#define mmNIC1_QM1_CP_MSG_BASE0_ADDR_HI_0                            0xD2223C

#define mmNIC1_QM1_CP_MSG_BASE0_ADDR_HI_1                            0xD22240

#define mmNIC1_QM1_CP_MSG_BASE0_ADDR_HI_2                            0xD22244

#define mmNIC1_QM1_CP_MSG_BASE0_ADDR_HI_3                            0xD22248

#define mmNIC1_QM1_CP_MSG_BASE0_ADDR_HI_4                            0xD2224C

#define mmNIC1_QM1_CP_MSG_BASE1_ADDR_LO_0                            0xD22250

#define mmNIC1_QM1_CP_MSG_BASE1_ADDR_LO_1                            0xD22254

#define mmNIC1_QM1_CP_MSG_BASE1_ADDR_LO_2                            0xD22258

#define mmNIC1_QM1_CP_MSG_BASE1_ADDR_LO_3                            0xD2225C

#define mmNIC1_QM1_CP_MSG_BASE1_ADDR_LO_4                            0xD22260

#define mmNIC1_QM1_CP_MSG_BASE1_ADDR_HI_0                            0xD22264

#define mmNIC1_QM1_CP_MSG_BASE1_ADDR_HI_1                            0xD22268

#define mmNIC1_QM1_CP_MSG_BASE1_ADDR_HI_2                            0xD2226C

#define mmNIC1_QM1_CP_MSG_BASE1_ADDR_HI_3                            0xD22270

#define mmNIC1_QM1_CP_MSG_BASE1_ADDR_HI_4                            0xD22274

#define mmNIC1_QM1_CP_MSG_BASE2_ADDR_LO_0                            0xD22278

#define mmNIC1_QM1_CP_MSG_BASE2_ADDR_LO_1                            0xD2227C

#define mmNIC1_QM1_CP_MSG_BASE2_ADDR_LO_2                            0xD22280

#define mmNIC1_QM1_CP_MSG_BASE2_ADDR_LO_3                            0xD22284

#define mmNIC1_QM1_CP_MSG_BASE2_ADDR_LO_4                            0xD22288

#define mmNIC1_QM1_CP_MSG_BASE2_ADDR_HI_0                            0xD2228C

#define mmNIC1_QM1_CP_MSG_BASE2_ADDR_HI_1                            0xD22290

#define mmNIC1_QM1_CP_MSG_BASE2_ADDR_HI_2                            0xD22294

#define mmNIC1_QM1_CP_MSG_BASE2_ADDR_HI_3                            0xD22298

#define mmNIC1_QM1_CP_MSG_BASE2_ADDR_HI_4                            0xD2229C

#define mmNIC1_QM1_CP_MSG_BASE3_ADDR_LO_0                            0xD222A0

#define mmNIC1_QM1_CP_MSG_BASE3_ADDR_LO_1                            0xD222A4

#define mmNIC1_QM1_CP_MSG_BASE3_ADDR_LO_2                            0xD222A8

#define mmNIC1_QM1_CP_MSG_BASE3_ADDR_LO_3                            0xD222AC

#define mmNIC1_QM1_CP_MSG_BASE3_ADDR_LO_4                            0xD222B0

#define mmNIC1_QM1_CP_MSG_BASE3_ADDR_HI_0                            0xD222B4

#define mmNIC1_QM1_CP_MSG_BASE3_ADDR_HI_1                            0xD222B8

#define mmNIC1_QM1_CP_MSG_BASE3_ADDR_HI_2                            0xD222BC

#define mmNIC1_QM1_CP_MSG_BASE3_ADDR_HI_3                            0xD222C0

#define mmNIC1_QM1_CP_MSG_BASE3_ADDR_HI_4                            0xD222C4

#define mmNIC1_QM1_CP_LDMA_TSIZE_OFFSET_0                            0xD222C8

#define mmNIC1_QM1_CP_LDMA_TSIZE_OFFSET_1                            0xD222CC

#define mmNIC1_QM1_CP_LDMA_TSIZE_OFFSET_2                            0xD222D0

#define mmNIC1_QM1_CP_LDMA_TSIZE_OFFSET_3                            0xD222D4

#define mmNIC1_QM1_CP_LDMA_TSIZE_OFFSET_4                            0xD222D8

#define mmNIC1_QM1_CP_LDMA_SRC_BASE_LO_OFFSET_0                      0xD222E0

#define mmNIC1_QM1_CP_LDMA_SRC_BASE_LO_OFFSET_1                      0xD222E4

#define mmNIC1_QM1_CP_LDMA_SRC_BASE_LO_OFFSET_2                      0xD222E8

#define mmNIC1_QM1_CP_LDMA_SRC_BASE_LO_OFFSET_3                      0xD222EC

#define mmNIC1_QM1_CP_LDMA_SRC_BASE_LO_OFFSET_4                      0xD222F0

#define mmNIC1_QM1_CP_LDMA_DST_BASE_LO_OFFSET_0                      0xD222F4

#define mmNIC1_QM1_CP_LDMA_DST_BASE_LO_OFFSET_1                      0xD222F8

#define mmNIC1_QM1_CP_LDMA_DST_BASE_LO_OFFSET_2                      0xD222FC

#define mmNIC1_QM1_CP_LDMA_DST_BASE_LO_OFFSET_3                      0xD22300

#define mmNIC1_QM1_CP_LDMA_DST_BASE_LO_OFFSET_4                      0xD22304

#define mmNIC1_QM1_CP_FENCE0_RDATA_0                                 0xD22308

#define mmNIC1_QM1_CP_FENCE0_RDATA_1                                 0xD2230C

#define mmNIC1_QM1_CP_FENCE0_RDATA_2                                 0xD22310

#define mmNIC1_QM1_CP_FENCE0_RDATA_3                                 0xD22314

#define mmNIC1_QM1_CP_FENCE0_RDATA_4                                 0xD22318

#define mmNIC1_QM1_CP_FENCE1_RDATA_0                                 0xD2231C

#define mmNIC1_QM1_CP_FENCE1_RDATA_1                                 0xD22320

#define mmNIC1_QM1_CP_FENCE1_RDATA_2                                 0xD22324

#define mmNIC1_QM1_CP_FENCE1_RDATA_3                                 0xD22328

#define mmNIC1_QM1_CP_FENCE1_RDATA_4                                 0xD2232C

#define mmNIC1_QM1_CP_FENCE2_RDATA_0                                 0xD22330

#define mmNIC1_QM1_CP_FENCE2_RDATA_1                                 0xD22334

#define mmNIC1_QM1_CP_FENCE2_RDATA_2                                 0xD22338

#define mmNIC1_QM1_CP_FENCE2_RDATA_3                                 0xD2233C

#define mmNIC1_QM1_CP_FENCE2_RDATA_4                                 0xD22340

#define mmNIC1_QM1_CP_FENCE3_RDATA_0                                 0xD22344

#define mmNIC1_QM1_CP_FENCE3_RDATA_1                                 0xD22348

#define mmNIC1_QM1_CP_FENCE3_RDATA_2                                 0xD2234C

#define mmNIC1_QM1_CP_FENCE3_RDATA_3                                 0xD22350

#define mmNIC1_QM1_CP_FENCE3_RDATA_4                                 0xD22354

#define mmNIC1_QM1_CP_FENCE0_CNT_0                                   0xD22358

#define mmNIC1_QM1_CP_FENCE0_CNT_1                                   0xD2235C

#define mmNIC1_QM1_CP_FENCE0_CNT_2                                   0xD22360

#define mmNIC1_QM1_CP_FENCE0_CNT_3                                   0xD22364

#define mmNIC1_QM1_CP_FENCE0_CNT_4                                   0xD22368

#define mmNIC1_QM1_CP_FENCE1_CNT_0                                   0xD2236C

#define mmNIC1_QM1_CP_FENCE1_CNT_1                                   0xD22370

#define mmNIC1_QM1_CP_FENCE1_CNT_2                                   0xD22374

#define mmNIC1_QM1_CP_FENCE1_CNT_3                                   0xD22378

#define mmNIC1_QM1_CP_FENCE1_CNT_4                                   0xD2237C

#define mmNIC1_QM1_CP_FENCE2_CNT_0                                   0xD22380

#define mmNIC1_QM1_CP_FENCE2_CNT_1                                   0xD22384

#define mmNIC1_QM1_CP_FENCE2_CNT_2                                   0xD22388

#define mmNIC1_QM1_CP_FENCE2_CNT_3                                   0xD2238C

#define mmNIC1_QM1_CP_FENCE2_CNT_4                                   0xD22390

#define mmNIC1_QM1_CP_FENCE3_CNT_0                                   0xD22394

#define mmNIC1_QM1_CP_FENCE3_CNT_1                                   0xD22398

#define mmNIC1_QM1_CP_FENCE3_CNT_2                                   0xD2239C

#define mmNIC1_QM1_CP_FENCE3_CNT_3                                   0xD223A0

#define mmNIC1_QM1_CP_FENCE3_CNT_4                                   0xD223A4

#define mmNIC1_QM1_CP_STS_0                                          0xD223A8

#define mmNIC1_QM1_CP_STS_1                                          0xD223AC

#define mmNIC1_QM1_CP_STS_2                                          0xD223B0

#define mmNIC1_QM1_CP_STS_3                                          0xD223B4

#define mmNIC1_QM1_CP_STS_4                                          0xD223B8

#define mmNIC1_QM1_CP_CURRENT_INST_LO_0                              0xD223BC

#define mmNIC1_QM1_CP_CURRENT_INST_LO_1                              0xD223C0

#define mmNIC1_QM1_CP_CURRENT_INST_LO_2                              0xD223C4

#define mmNIC1_QM1_CP_CURRENT_INST_LO_3                              0xD223C8

#define mmNIC1_QM1_CP_CURRENT_INST_LO_4                              0xD223CC

#define mmNIC1_QM1_CP_CURRENT_INST_HI_0                              0xD223D0

#define mmNIC1_QM1_CP_CURRENT_INST_HI_1                              0xD223D4

#define mmNIC1_QM1_CP_CURRENT_INST_HI_2                              0xD223D8

#define mmNIC1_QM1_CP_CURRENT_INST_HI_3                              0xD223DC

#define mmNIC1_QM1_CP_CURRENT_INST_HI_4                              0xD223E0

#define mmNIC1_QM1_CP_BARRIER_CFG_0                                  0xD223F4

#define mmNIC1_QM1_CP_BARRIER_CFG_1                                  0xD223F8

#define mmNIC1_QM1_CP_BARRIER_CFG_2                                  0xD223FC

#define mmNIC1_QM1_CP_BARRIER_CFG_3                                  0xD22400

#define mmNIC1_QM1_CP_BARRIER_CFG_4                                  0xD22404

#define mmNIC1_QM1_CP_DBG_0_0                                        0xD22408

#define mmNIC1_QM1_CP_DBG_0_1                                        0xD2240C

#define mmNIC1_QM1_CP_DBG_0_2                                        0xD22410

#define mmNIC1_QM1_CP_DBG_0_3                                        0xD22414

#define mmNIC1_QM1_CP_DBG_0_4                                        0xD22418

#define mmNIC1_QM1_CP_ARUSER_31_11_0                                 0xD2241C

#define mmNIC1_QM1_CP_ARUSER_31_11_1                                 0xD22420

#define mmNIC1_QM1_CP_ARUSER_31_11_2                                 0xD22424

#define mmNIC1_QM1_CP_ARUSER_31_11_3                                 0xD22428

#define mmNIC1_QM1_CP_ARUSER_31_11_4                                 0xD2242C

#define mmNIC1_QM1_CP_AWUSER_31_11_0                                 0xD22430

#define mmNIC1_QM1_CP_AWUSER_31_11_1                                 0xD22434

#define mmNIC1_QM1_CP_AWUSER_31_11_2                                 0xD22438

#define mmNIC1_QM1_CP_AWUSER_31_11_3                                 0xD2243C

#define mmNIC1_QM1_CP_AWUSER_31_11_4                                 0xD22440

#define mmNIC1_QM1_ARB_CFG_0                                         0xD22A00

#define mmNIC1_QM1_ARB_CHOISE_Q_PUSH                                 0xD22A04

#define mmNIC1_QM1_ARB_WRR_WEIGHT_0                                  0xD22A08

#define mmNIC1_QM1_ARB_WRR_WEIGHT_1                                  0xD22A0C

#define mmNIC1_QM1_ARB_WRR_WEIGHT_2                                  0xD22A10

#define mmNIC1_QM1_ARB_WRR_WEIGHT_3                                  0xD22A14

#define mmNIC1_QM1_ARB_CFG_1                                         0xD22A18

#define mmNIC1_QM1_ARB_MST_AVAIL_CRED_0                              0xD22A20

#define mmNIC1_QM1_ARB_MST_AVAIL_CRED_1                              0xD22A24

#define mmNIC1_QM1_ARB_MST_AVAIL_CRED_2                              0xD22A28

#define mmNIC1_QM1_ARB_MST_AVAIL_CRED_3                              0xD22A2C

#define mmNIC1_QM1_ARB_MST_AVAIL_CRED_4                              0xD22A30

#define mmNIC1_QM1_ARB_MST_AVAIL_CRED_5                              0xD22A34

#define mmNIC1_QM1_ARB_MST_AVAIL_CRED_6                              0xD22A38

#define mmNIC1_QM1_ARB_MST_AVAIL_CRED_7                              0xD22A3C

#define mmNIC1_QM1_ARB_MST_AVAIL_CRED_8                              0xD22A40

#define mmNIC1_QM1_ARB_MST_AVAIL_CRED_9                              0xD22A44

#define mmNIC1_QM1_ARB_MST_AVAIL_CRED_10                             0xD22A48

#define mmNIC1_QM1_ARB_MST_AVAIL_CRED_11                             0xD22A4C

#define mmNIC1_QM1_ARB_MST_AVAIL_CRED_12                             0xD22A50

#define mmNIC1_QM1_ARB_MST_AVAIL_CRED_13                             0xD22A54

#define mmNIC1_QM1_ARB_MST_AVAIL_CRED_14                             0xD22A58

#define mmNIC1_QM1_ARB_MST_AVAIL_CRED_15                             0xD22A5C

#define mmNIC1_QM1_ARB_MST_AVAIL_CRED_16                             0xD22A60

#define mmNIC1_QM1_ARB_MST_AVAIL_CRED_17                             0xD22A64

#define mmNIC1_QM1_ARB_MST_AVAIL_CRED_18                             0xD22A68

#define mmNIC1_QM1_ARB_MST_AVAIL_CRED_19                             0xD22A6C

#define mmNIC1_QM1_ARB_MST_AVAIL_CRED_20                             0xD22A70

#define mmNIC1_QM1_ARB_MST_AVAIL_CRED_21                             0xD22A74

#define mmNIC1_QM1_ARB_MST_AVAIL_CRED_22                             0xD22A78

#define mmNIC1_QM1_ARB_MST_AVAIL_CRED_23                             0xD22A7C

#define mmNIC1_QM1_ARB_MST_AVAIL_CRED_24                             0xD22A80

#define mmNIC1_QM1_ARB_MST_AVAIL_CRED_25                             0xD22A84

#define mmNIC1_QM1_ARB_MST_AVAIL_CRED_26                             0xD22A88

#define mmNIC1_QM1_ARB_MST_AVAIL_CRED_27                             0xD22A8C

#define mmNIC1_QM1_ARB_MST_AVAIL_CRED_28                             0xD22A90

#define mmNIC1_QM1_ARB_MST_AVAIL_CRED_29                             0xD22A94

#define mmNIC1_QM1_ARB_MST_AVAIL_CRED_30                             0xD22A98

#define mmNIC1_QM1_ARB_MST_AVAIL_CRED_31                             0xD22A9C

#define mmNIC1_QM1_ARB_MST_CRED_INC                                  0xD22AA0

#define mmNIC1_QM1_ARB_MST_CHOISE_PUSH_OFST_0                        0xD22AA4

#define mmNIC1_QM1_ARB_MST_CHOISE_PUSH_OFST_1                        0xD22AA8

#define mmNIC1_QM1_ARB_MST_CHOISE_PUSH_OFST_2                        0xD22AAC

#define mmNIC1_QM1_ARB_MST_CHOISE_PUSH_OFST_3                        0xD22AB0

#define mmNIC1_QM1_ARB_MST_CHOISE_PUSH_OFST_4                        0xD22AB4

#define mmNIC1_QM1_ARB_MST_CHOISE_PUSH_OFST_5                        0xD22AB8

#define mmNIC1_QM1_ARB_MST_CHOISE_PUSH_OFST_6                        0xD22ABC

#define mmNIC1_QM1_ARB_MST_CHOISE_PUSH_OFST_7                        0xD22AC0

#define mmNIC1_QM1_ARB_MST_CHOISE_PUSH_OFST_8                        0xD22AC4

#define mmNIC1_QM1_ARB_MST_CHOISE_PUSH_OFST_9                        0xD22AC8

#define mmNIC1_QM1_ARB_MST_CHOISE_PUSH_OFST_10                       0xD22ACC

#define mmNIC1_QM1_ARB_MST_CHOISE_PUSH_OFST_11                       0xD22AD0

#define mmNIC1_QM1_ARB_MST_CHOISE_PUSH_OFST_12                       0xD22AD4

#define mmNIC1_QM1_ARB_MST_CHOISE_PUSH_OFST_13                       0xD22AD8

#define mmNIC1_QM1_ARB_MST_CHOISE_PUSH_OFST_14                       0xD22ADC

#define mmNIC1_QM1_ARB_MST_CHOISE_PUSH_OFST_15                       0xD22AE0

#define mmNIC1_QM1_ARB_MST_CHOISE_PUSH_OFST_16                       0xD22AE4

#define mmNIC1_QM1_ARB_MST_CHOISE_PUSH_OFST_17                       0xD22AE8

#define mmNIC1_QM1_ARB_MST_CHOISE_PUSH_OFST_18                       0xD22AEC

#define mmNIC1_QM1_ARB_MST_CHOISE_PUSH_OFST_19                       0xD22AF0

#define mmNIC1_QM1_ARB_MST_CHOISE_PUSH_OFST_20                       0xD22AF4

#define mmNIC1_QM1_ARB_MST_CHOISE_PUSH_OFST_21                       0xD22AF8

#define mmNIC1_QM1_ARB_MST_CHOISE_PUSH_OFST_22                       0xD22AFC

#define mmNIC1_QM1_ARB_MST_CHOISE_PUSH_OFST_23                       0xD22B00

#define mmNIC1_QM1_ARB_MST_CHOISE_PUSH_OFST_24                       0xD22B04

#define mmNIC1_QM1_ARB_MST_CHOISE_PUSH_OFST_25                       0xD22B08

#define mmNIC1_QM1_ARB_MST_CHOISE_PUSH_OFST_26                       0xD22B0C

#define mmNIC1_QM1_ARB_MST_CHOISE_PUSH_OFST_27                       0xD22B10

#define mmNIC1_QM1_ARB_MST_CHOISE_PUSH_OFST_28                       0xD22B14

#define mmNIC1_QM1_ARB_MST_CHOISE_PUSH_OFST_29                       0xD22B18

#define mmNIC1_QM1_ARB_MST_CHOISE_PUSH_OFST_30                       0xD22B1C

#define mmNIC1_QM1_ARB_MST_CHOISE_PUSH_OFST_31                       0xD22B20

#define mmNIC1_QM1_ARB_SLV_MASTER_INC_CRED_OFST                      0xD22B28

#define mmNIC1_QM1_ARB_MST_SLAVE_EN                                  0xD22B2C

#define mmNIC1_QM1_ARB_MST_QUIET_PER                                 0xD22B34

#define mmNIC1_QM1_ARB_SLV_CHOISE_WDT                                0xD22B38

#define mmNIC1_QM1_ARB_SLV_ID                                        0xD22B3C

#define mmNIC1_QM1_ARB_MSG_MAX_INFLIGHT                              0xD22B44

#define mmNIC1_QM1_ARB_MSG_AWUSER_31_11                              0xD22B48

#define mmNIC1_QM1_ARB_MSG_AWUSER_SEC_PROP                           0xD22B4C

#define mmNIC1_QM1_ARB_MSG_AWUSER_NON_SEC_PROP                       0xD22B50

#define mmNIC1_QM1_ARB_BASE_LO                                       0xD22B54

#define mmNIC1_QM1_ARB_BASE_HI                                       0xD22B58

#define mmNIC1_QM1_ARB_STATE_STS                                     0xD22B80

#define mmNIC1_QM1_ARB_CHOISE_FULLNESS_STS                           0xD22B84

#define mmNIC1_QM1_ARB_MSG_STS                                       0xD22B88

#define mmNIC1_QM1_ARB_SLV_CHOISE_Q_HEAD                             0xD22B8C

#define mmNIC1_QM1_ARB_ERR_CAUSE                                     0xD22B9C

#define mmNIC1_QM1_ARB_ERR_MSG_EN                                    0xD22BA0

#define mmNIC1_QM1_ARB_ERR_STS_DRP                                   0xD22BA8

#define mmNIC1_QM1_ARB_MST_CRED_STS_0                                0xD22BB0

#define mmNIC1_QM1_ARB_MST_CRED_STS_1                                0xD22BB4

#define mmNIC1_QM1_ARB_MST_CRED_STS_2                                0xD22BB8

#define mmNIC1_QM1_ARB_MST_CRED_STS_3                                0xD22BBC

#define mmNIC1_QM1_ARB_MST_CRED_STS_4                                0xD22BC0

#define mmNIC1_QM1_ARB_MST_CRED_STS_5                                0xD22BC4

#define mmNIC1_QM1_ARB_MST_CRED_STS_6                                0xD22BC8

#define mmNIC1_QM1_ARB_MST_CRED_STS_7                                0xD22BCC

#define mmNIC1_QM1_ARB_MST_CRED_STS_8                                0xD22BD0

#define mmNIC1_QM1_ARB_MST_CRED_STS_9                                0xD22BD4

#define mmNIC1_QM1_ARB_MST_CRED_STS_10                               0xD22BD8

#define mmNIC1_QM1_ARB_MST_CRED_STS_11                               0xD22BDC

#define mmNIC1_QM1_ARB_MST_CRED_STS_12                               0xD22BE0

#define mmNIC1_QM1_ARB_MST_CRED_STS_13                               0xD22BE4

#define mmNIC1_QM1_ARB_MST_CRED_STS_14                               0xD22BE8

#define mmNIC1_QM1_ARB_MST_CRED_STS_15                               0xD22BEC

#define mmNIC1_QM1_ARB_MST_CRED_STS_16                               0xD22BF0

#define mmNIC1_QM1_ARB_MST_CRED_STS_17                               0xD22BF4

#define mmNIC1_QM1_ARB_MST_CRED_STS_18                               0xD22BF8

#define mmNIC1_QM1_ARB_MST_CRED_STS_19                               0xD22BFC

#define mmNIC1_QM1_ARB_MST_CRED_STS_20                               0xD22C00

#define mmNIC1_QM1_ARB_MST_CRED_STS_21                               0xD22C04

#define mmNIC1_QM1_ARB_MST_CRED_STS_22                               0xD22C08

#define mmNIC1_QM1_ARB_MST_CRED_STS_23                               0xD22C0C

#define mmNIC1_QM1_ARB_MST_CRED_STS_24                               0xD22C10

#define mmNIC1_QM1_ARB_MST_CRED_STS_25                               0xD22C14

#define mmNIC1_QM1_ARB_MST_CRED_STS_26                               0xD22C18

#define mmNIC1_QM1_ARB_MST_CRED_STS_27                               0xD22C1C

#define mmNIC1_QM1_ARB_MST_CRED_STS_28                               0xD22C20

#define mmNIC1_QM1_ARB_MST_CRED_STS_29                               0xD22C24

#define mmNIC1_QM1_ARB_MST_CRED_STS_30                               0xD22C28

#define mmNIC1_QM1_ARB_MST_CRED_STS_31                               0xD22C2C

#define mmNIC1_QM1_CGM_CFG                                           0xD22C70

#define mmNIC1_QM1_CGM_STS                                           0xD22C74

#define mmNIC1_QM1_CGM_CFG1                                          0xD22C78

#define mmNIC1_QM1_LOCAL_RANGE_BASE                                  0xD22C80

#define mmNIC1_QM1_LOCAL_RANGE_SIZE                                  0xD22C84

#define mmNIC1_QM1_CSMR_STRICT_PRIO_CFG                              0xD22C90

#define mmNIC1_QM1_HBW_RD_RATE_LIM_CFG_1                             0xD22C94

#define mmNIC1_QM1_LBW_WR_RATE_LIM_CFG_0                             0xD22C98

#define mmNIC1_QM1_LBW_WR_RATE_LIM_CFG_1                             0xD22C9C

#define mmNIC1_QM1_HBW_RD_RATE_LIM_CFG_0                             0xD22CA0

#define mmNIC1_QM1_GLBL_AXCACHE                                      0xD22CA4

#define mmNIC1_QM1_IND_GW_APB_CFG                                    0xD22CB0

#define mmNIC1_QM1_IND_GW_APB_WDATA                                  0xD22CB4

#define mmNIC1_QM1_IND_GW_APB_RDATA                                  0xD22CB8

#define mmNIC1_QM1_IND_GW_APB_STATUS                                 0xD22CBC

#define mmNIC1_QM1_GLBL_ERR_ADDR_LO                                  0xD22CD0

#define mmNIC1_QM1_GLBL_ERR_ADDR_HI                                  0xD22CD4

#define mmNIC1_QM1_GLBL_ERR_WDATA                                    0xD22CD8

#define mmNIC1_QM1_GLBL_MEM_INIT_BUSY                                0xD22D00

#endif /* ASIC_REG_NIC1_QM1_REGS_H_ */
