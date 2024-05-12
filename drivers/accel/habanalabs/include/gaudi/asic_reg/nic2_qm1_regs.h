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

#ifndef ASIC_REG_NIC2_QM1_REGS_H_
#define ASIC_REG_NIC2_QM1_REGS_H_

/*
 *****************************************
 *   NIC2_QM1 (Prototype: QMAN)
 *****************************************
 */

#define mmNIC2_QM1_GLBL_CFG0                                         0xD62000

#define mmNIC2_QM1_GLBL_CFG1                                         0xD62004

#define mmNIC2_QM1_GLBL_PROT                                         0xD62008

#define mmNIC2_QM1_GLBL_ERR_CFG                                      0xD6200C

#define mmNIC2_QM1_GLBL_SECURE_PROPS_0                               0xD62010

#define mmNIC2_QM1_GLBL_SECURE_PROPS_1                               0xD62014

#define mmNIC2_QM1_GLBL_SECURE_PROPS_2                               0xD62018

#define mmNIC2_QM1_GLBL_SECURE_PROPS_3                               0xD6201C

#define mmNIC2_QM1_GLBL_SECURE_PROPS_4                               0xD62020

#define mmNIC2_QM1_GLBL_NON_SECURE_PROPS_0                           0xD62024

#define mmNIC2_QM1_GLBL_NON_SECURE_PROPS_1                           0xD62028

#define mmNIC2_QM1_GLBL_NON_SECURE_PROPS_2                           0xD6202C

#define mmNIC2_QM1_GLBL_NON_SECURE_PROPS_3                           0xD62030

#define mmNIC2_QM1_GLBL_NON_SECURE_PROPS_4                           0xD62034

#define mmNIC2_QM1_GLBL_STS0                                         0xD62038

#define mmNIC2_QM1_GLBL_STS1_0                                       0xD62040

#define mmNIC2_QM1_GLBL_STS1_1                                       0xD62044

#define mmNIC2_QM1_GLBL_STS1_2                                       0xD62048

#define mmNIC2_QM1_GLBL_STS1_3                                       0xD6204C

#define mmNIC2_QM1_GLBL_STS1_4                                       0xD62050

#define mmNIC2_QM1_GLBL_MSG_EN_0                                     0xD62054

#define mmNIC2_QM1_GLBL_MSG_EN_1                                     0xD62058

#define mmNIC2_QM1_GLBL_MSG_EN_2                                     0xD6205C

#define mmNIC2_QM1_GLBL_MSG_EN_3                                     0xD62060

#define mmNIC2_QM1_GLBL_MSG_EN_4                                     0xD62068

#define mmNIC2_QM1_PQ_BASE_LO_0                                      0xD62070

#define mmNIC2_QM1_PQ_BASE_LO_1                                      0xD62074

#define mmNIC2_QM1_PQ_BASE_LO_2                                      0xD62078

#define mmNIC2_QM1_PQ_BASE_LO_3                                      0xD6207C

#define mmNIC2_QM1_PQ_BASE_HI_0                                      0xD62080

#define mmNIC2_QM1_PQ_BASE_HI_1                                      0xD62084

#define mmNIC2_QM1_PQ_BASE_HI_2                                      0xD62088

#define mmNIC2_QM1_PQ_BASE_HI_3                                      0xD6208C

#define mmNIC2_QM1_PQ_SIZE_0                                         0xD62090

#define mmNIC2_QM1_PQ_SIZE_1                                         0xD62094

#define mmNIC2_QM1_PQ_SIZE_2                                         0xD62098

#define mmNIC2_QM1_PQ_SIZE_3                                         0xD6209C

#define mmNIC2_QM1_PQ_PI_0                                           0xD620A0

#define mmNIC2_QM1_PQ_PI_1                                           0xD620A4

#define mmNIC2_QM1_PQ_PI_2                                           0xD620A8

#define mmNIC2_QM1_PQ_PI_3                                           0xD620AC

#define mmNIC2_QM1_PQ_CI_0                                           0xD620B0

#define mmNIC2_QM1_PQ_CI_1                                           0xD620B4

#define mmNIC2_QM1_PQ_CI_2                                           0xD620B8

#define mmNIC2_QM1_PQ_CI_3                                           0xD620BC

#define mmNIC2_QM1_PQ_CFG0_0                                         0xD620C0

#define mmNIC2_QM1_PQ_CFG0_1                                         0xD620C4

#define mmNIC2_QM1_PQ_CFG0_2                                         0xD620C8

#define mmNIC2_QM1_PQ_CFG0_3                                         0xD620CC

#define mmNIC2_QM1_PQ_CFG1_0                                         0xD620D0

#define mmNIC2_QM1_PQ_CFG1_1                                         0xD620D4

#define mmNIC2_QM1_PQ_CFG1_2                                         0xD620D8

#define mmNIC2_QM1_PQ_CFG1_3                                         0xD620DC

#define mmNIC2_QM1_PQ_ARUSER_31_11_0                                 0xD620E0

#define mmNIC2_QM1_PQ_ARUSER_31_11_1                                 0xD620E4

#define mmNIC2_QM1_PQ_ARUSER_31_11_2                                 0xD620E8

#define mmNIC2_QM1_PQ_ARUSER_31_11_3                                 0xD620EC

#define mmNIC2_QM1_PQ_STS0_0                                         0xD620F0

#define mmNIC2_QM1_PQ_STS0_1                                         0xD620F4

#define mmNIC2_QM1_PQ_STS0_2                                         0xD620F8

#define mmNIC2_QM1_PQ_STS0_3                                         0xD620FC

#define mmNIC2_QM1_PQ_STS1_0                                         0xD62100

#define mmNIC2_QM1_PQ_STS1_1                                         0xD62104

#define mmNIC2_QM1_PQ_STS1_2                                         0xD62108

#define mmNIC2_QM1_PQ_STS1_3                                         0xD6210C

#define mmNIC2_QM1_CQ_CFG0_0                                         0xD62110

#define mmNIC2_QM1_CQ_CFG0_1                                         0xD62114

#define mmNIC2_QM1_CQ_CFG0_2                                         0xD62118

#define mmNIC2_QM1_CQ_CFG0_3                                         0xD6211C

#define mmNIC2_QM1_CQ_CFG0_4                                         0xD62120

#define mmNIC2_QM1_CQ_CFG1_0                                         0xD62124

#define mmNIC2_QM1_CQ_CFG1_1                                         0xD62128

#define mmNIC2_QM1_CQ_CFG1_2                                         0xD6212C

#define mmNIC2_QM1_CQ_CFG1_3                                         0xD62130

#define mmNIC2_QM1_CQ_CFG1_4                                         0xD62134

#define mmNIC2_QM1_CQ_ARUSER_31_11_0                                 0xD62138

#define mmNIC2_QM1_CQ_ARUSER_31_11_1                                 0xD6213C

#define mmNIC2_QM1_CQ_ARUSER_31_11_2                                 0xD62140

#define mmNIC2_QM1_CQ_ARUSER_31_11_3                                 0xD62144

#define mmNIC2_QM1_CQ_ARUSER_31_11_4                                 0xD62148

#define mmNIC2_QM1_CQ_STS0_0                                         0xD6214C

#define mmNIC2_QM1_CQ_STS0_1                                         0xD62150

#define mmNIC2_QM1_CQ_STS0_2                                         0xD62154

#define mmNIC2_QM1_CQ_STS0_3                                         0xD62158

#define mmNIC2_QM1_CQ_STS0_4                                         0xD6215C

#define mmNIC2_QM1_CQ_STS1_0                                         0xD62160

#define mmNIC2_QM1_CQ_STS1_1                                         0xD62164

#define mmNIC2_QM1_CQ_STS1_2                                         0xD62168

#define mmNIC2_QM1_CQ_STS1_3                                         0xD6216C

#define mmNIC2_QM1_CQ_STS1_4                                         0xD62170

#define mmNIC2_QM1_CQ_PTR_LO_0                                       0xD62174

#define mmNIC2_QM1_CQ_PTR_HI_0                                       0xD62178

#define mmNIC2_QM1_CQ_TSIZE_0                                        0xD6217C

#define mmNIC2_QM1_CQ_CTL_0                                          0xD62180

#define mmNIC2_QM1_CQ_PTR_LO_1                                       0xD62184

#define mmNIC2_QM1_CQ_PTR_HI_1                                       0xD62188

#define mmNIC2_QM1_CQ_TSIZE_1                                        0xD6218C

#define mmNIC2_QM1_CQ_CTL_1                                          0xD62190

#define mmNIC2_QM1_CQ_PTR_LO_2                                       0xD62194

#define mmNIC2_QM1_CQ_PTR_HI_2                                       0xD62198

#define mmNIC2_QM1_CQ_TSIZE_2                                        0xD6219C

#define mmNIC2_QM1_CQ_CTL_2                                          0xD621A0

#define mmNIC2_QM1_CQ_PTR_LO_3                                       0xD621A4

#define mmNIC2_QM1_CQ_PTR_HI_3                                       0xD621A8

#define mmNIC2_QM1_CQ_TSIZE_3                                        0xD621AC

#define mmNIC2_QM1_CQ_CTL_3                                          0xD621B0

#define mmNIC2_QM1_CQ_PTR_LO_4                                       0xD621B4

#define mmNIC2_QM1_CQ_PTR_HI_4                                       0xD621B8

#define mmNIC2_QM1_CQ_TSIZE_4                                        0xD621BC

#define mmNIC2_QM1_CQ_CTL_4                                          0xD621C0

#define mmNIC2_QM1_CQ_PTR_LO_STS_0                                   0xD621C4

#define mmNIC2_QM1_CQ_PTR_LO_STS_1                                   0xD621C8

#define mmNIC2_QM1_CQ_PTR_LO_STS_2                                   0xD621CC

#define mmNIC2_QM1_CQ_PTR_LO_STS_3                                   0xD621D0

#define mmNIC2_QM1_CQ_PTR_LO_STS_4                                   0xD621D4

#define mmNIC2_QM1_CQ_PTR_HI_STS_0                                   0xD621D8

#define mmNIC2_QM1_CQ_PTR_HI_STS_1                                   0xD621DC

#define mmNIC2_QM1_CQ_PTR_HI_STS_2                                   0xD621E0

#define mmNIC2_QM1_CQ_PTR_HI_STS_3                                   0xD621E4

#define mmNIC2_QM1_CQ_PTR_HI_STS_4                                   0xD621E8

#define mmNIC2_QM1_CQ_TSIZE_STS_0                                    0xD621EC

#define mmNIC2_QM1_CQ_TSIZE_STS_1                                    0xD621F0

#define mmNIC2_QM1_CQ_TSIZE_STS_2                                    0xD621F4

#define mmNIC2_QM1_CQ_TSIZE_STS_3                                    0xD621F8

#define mmNIC2_QM1_CQ_TSIZE_STS_4                                    0xD621FC

#define mmNIC2_QM1_CQ_CTL_STS_0                                      0xD62200

#define mmNIC2_QM1_CQ_CTL_STS_1                                      0xD62204

#define mmNIC2_QM1_CQ_CTL_STS_2                                      0xD62208

#define mmNIC2_QM1_CQ_CTL_STS_3                                      0xD6220C

#define mmNIC2_QM1_CQ_CTL_STS_4                                      0xD62210

#define mmNIC2_QM1_CQ_IFIFO_CNT_0                                    0xD62214

#define mmNIC2_QM1_CQ_IFIFO_CNT_1                                    0xD62218

#define mmNIC2_QM1_CQ_IFIFO_CNT_2                                    0xD6221C

#define mmNIC2_QM1_CQ_IFIFO_CNT_3                                    0xD62220

#define mmNIC2_QM1_CQ_IFIFO_CNT_4                                    0xD62224

#define mmNIC2_QM1_CP_MSG_BASE0_ADDR_LO_0                            0xD62228

#define mmNIC2_QM1_CP_MSG_BASE0_ADDR_LO_1                            0xD6222C

#define mmNIC2_QM1_CP_MSG_BASE0_ADDR_LO_2                            0xD62230

#define mmNIC2_QM1_CP_MSG_BASE0_ADDR_LO_3                            0xD62234

#define mmNIC2_QM1_CP_MSG_BASE0_ADDR_LO_4                            0xD62238

#define mmNIC2_QM1_CP_MSG_BASE0_ADDR_HI_0                            0xD6223C

#define mmNIC2_QM1_CP_MSG_BASE0_ADDR_HI_1                            0xD62240

#define mmNIC2_QM1_CP_MSG_BASE0_ADDR_HI_2                            0xD62244

#define mmNIC2_QM1_CP_MSG_BASE0_ADDR_HI_3                            0xD62248

#define mmNIC2_QM1_CP_MSG_BASE0_ADDR_HI_4                            0xD6224C

#define mmNIC2_QM1_CP_MSG_BASE1_ADDR_LO_0                            0xD62250

#define mmNIC2_QM1_CP_MSG_BASE1_ADDR_LO_1                            0xD62254

#define mmNIC2_QM1_CP_MSG_BASE1_ADDR_LO_2                            0xD62258

#define mmNIC2_QM1_CP_MSG_BASE1_ADDR_LO_3                            0xD6225C

#define mmNIC2_QM1_CP_MSG_BASE1_ADDR_LO_4                            0xD62260

#define mmNIC2_QM1_CP_MSG_BASE1_ADDR_HI_0                            0xD62264

#define mmNIC2_QM1_CP_MSG_BASE1_ADDR_HI_1                            0xD62268

#define mmNIC2_QM1_CP_MSG_BASE1_ADDR_HI_2                            0xD6226C

#define mmNIC2_QM1_CP_MSG_BASE1_ADDR_HI_3                            0xD62270

#define mmNIC2_QM1_CP_MSG_BASE1_ADDR_HI_4                            0xD62274

#define mmNIC2_QM1_CP_MSG_BASE2_ADDR_LO_0                            0xD62278

#define mmNIC2_QM1_CP_MSG_BASE2_ADDR_LO_1                            0xD6227C

#define mmNIC2_QM1_CP_MSG_BASE2_ADDR_LO_2                            0xD62280

#define mmNIC2_QM1_CP_MSG_BASE2_ADDR_LO_3                            0xD62284

#define mmNIC2_QM1_CP_MSG_BASE2_ADDR_LO_4                            0xD62288

#define mmNIC2_QM1_CP_MSG_BASE2_ADDR_HI_0                            0xD6228C

#define mmNIC2_QM1_CP_MSG_BASE2_ADDR_HI_1                            0xD62290

#define mmNIC2_QM1_CP_MSG_BASE2_ADDR_HI_2                            0xD62294

#define mmNIC2_QM1_CP_MSG_BASE2_ADDR_HI_3                            0xD62298

#define mmNIC2_QM1_CP_MSG_BASE2_ADDR_HI_4                            0xD6229C

#define mmNIC2_QM1_CP_MSG_BASE3_ADDR_LO_0                            0xD622A0

#define mmNIC2_QM1_CP_MSG_BASE3_ADDR_LO_1                            0xD622A4

#define mmNIC2_QM1_CP_MSG_BASE3_ADDR_LO_2                            0xD622A8

#define mmNIC2_QM1_CP_MSG_BASE3_ADDR_LO_3                            0xD622AC

#define mmNIC2_QM1_CP_MSG_BASE3_ADDR_LO_4                            0xD622B0

#define mmNIC2_QM1_CP_MSG_BASE3_ADDR_HI_0                            0xD622B4

#define mmNIC2_QM1_CP_MSG_BASE3_ADDR_HI_1                            0xD622B8

#define mmNIC2_QM1_CP_MSG_BASE3_ADDR_HI_2                            0xD622BC

#define mmNIC2_QM1_CP_MSG_BASE3_ADDR_HI_3                            0xD622C0

#define mmNIC2_QM1_CP_MSG_BASE3_ADDR_HI_4                            0xD622C4

#define mmNIC2_QM1_CP_LDMA_TSIZE_OFFSET_0                            0xD622C8

#define mmNIC2_QM1_CP_LDMA_TSIZE_OFFSET_1                            0xD622CC

#define mmNIC2_QM1_CP_LDMA_TSIZE_OFFSET_2                            0xD622D0

#define mmNIC2_QM1_CP_LDMA_TSIZE_OFFSET_3                            0xD622D4

#define mmNIC2_QM1_CP_LDMA_TSIZE_OFFSET_4                            0xD622D8

#define mmNIC2_QM1_CP_LDMA_SRC_BASE_LO_OFFSET_0                      0xD622E0

#define mmNIC2_QM1_CP_LDMA_SRC_BASE_LO_OFFSET_1                      0xD622E4

#define mmNIC2_QM1_CP_LDMA_SRC_BASE_LO_OFFSET_2                      0xD622E8

#define mmNIC2_QM1_CP_LDMA_SRC_BASE_LO_OFFSET_3                      0xD622EC

#define mmNIC2_QM1_CP_LDMA_SRC_BASE_LO_OFFSET_4                      0xD622F0

#define mmNIC2_QM1_CP_LDMA_DST_BASE_LO_OFFSET_0                      0xD622F4

#define mmNIC2_QM1_CP_LDMA_DST_BASE_LO_OFFSET_1                      0xD622F8

#define mmNIC2_QM1_CP_LDMA_DST_BASE_LO_OFFSET_2                      0xD622FC

#define mmNIC2_QM1_CP_LDMA_DST_BASE_LO_OFFSET_3                      0xD62300

#define mmNIC2_QM1_CP_LDMA_DST_BASE_LO_OFFSET_4                      0xD62304

#define mmNIC2_QM1_CP_FENCE0_RDATA_0                                 0xD62308

#define mmNIC2_QM1_CP_FENCE0_RDATA_1                                 0xD6230C

#define mmNIC2_QM1_CP_FENCE0_RDATA_2                                 0xD62310

#define mmNIC2_QM1_CP_FENCE0_RDATA_3                                 0xD62314

#define mmNIC2_QM1_CP_FENCE0_RDATA_4                                 0xD62318

#define mmNIC2_QM1_CP_FENCE1_RDATA_0                                 0xD6231C

#define mmNIC2_QM1_CP_FENCE1_RDATA_1                                 0xD62320

#define mmNIC2_QM1_CP_FENCE1_RDATA_2                                 0xD62324

#define mmNIC2_QM1_CP_FENCE1_RDATA_3                                 0xD62328

#define mmNIC2_QM1_CP_FENCE1_RDATA_4                                 0xD6232C

#define mmNIC2_QM1_CP_FENCE2_RDATA_0                                 0xD62330

#define mmNIC2_QM1_CP_FENCE2_RDATA_1                                 0xD62334

#define mmNIC2_QM1_CP_FENCE2_RDATA_2                                 0xD62338

#define mmNIC2_QM1_CP_FENCE2_RDATA_3                                 0xD6233C

#define mmNIC2_QM1_CP_FENCE2_RDATA_4                                 0xD62340

#define mmNIC2_QM1_CP_FENCE3_RDATA_0                                 0xD62344

#define mmNIC2_QM1_CP_FENCE3_RDATA_1                                 0xD62348

#define mmNIC2_QM1_CP_FENCE3_RDATA_2                                 0xD6234C

#define mmNIC2_QM1_CP_FENCE3_RDATA_3                                 0xD62350

#define mmNIC2_QM1_CP_FENCE3_RDATA_4                                 0xD62354

#define mmNIC2_QM1_CP_FENCE0_CNT_0                                   0xD62358

#define mmNIC2_QM1_CP_FENCE0_CNT_1                                   0xD6235C

#define mmNIC2_QM1_CP_FENCE0_CNT_2                                   0xD62360

#define mmNIC2_QM1_CP_FENCE0_CNT_3                                   0xD62364

#define mmNIC2_QM1_CP_FENCE0_CNT_4                                   0xD62368

#define mmNIC2_QM1_CP_FENCE1_CNT_0                                   0xD6236C

#define mmNIC2_QM1_CP_FENCE1_CNT_1                                   0xD62370

#define mmNIC2_QM1_CP_FENCE1_CNT_2                                   0xD62374

#define mmNIC2_QM1_CP_FENCE1_CNT_3                                   0xD62378

#define mmNIC2_QM1_CP_FENCE1_CNT_4                                   0xD6237C

#define mmNIC2_QM1_CP_FENCE2_CNT_0                                   0xD62380

#define mmNIC2_QM1_CP_FENCE2_CNT_1                                   0xD62384

#define mmNIC2_QM1_CP_FENCE2_CNT_2                                   0xD62388

#define mmNIC2_QM1_CP_FENCE2_CNT_3                                   0xD6238C

#define mmNIC2_QM1_CP_FENCE2_CNT_4                                   0xD62390

#define mmNIC2_QM1_CP_FENCE3_CNT_0                                   0xD62394

#define mmNIC2_QM1_CP_FENCE3_CNT_1                                   0xD62398

#define mmNIC2_QM1_CP_FENCE3_CNT_2                                   0xD6239C

#define mmNIC2_QM1_CP_FENCE3_CNT_3                                   0xD623A0

#define mmNIC2_QM1_CP_FENCE3_CNT_4                                   0xD623A4

#define mmNIC2_QM1_CP_STS_0                                          0xD623A8

#define mmNIC2_QM1_CP_STS_1                                          0xD623AC

#define mmNIC2_QM1_CP_STS_2                                          0xD623B0

#define mmNIC2_QM1_CP_STS_3                                          0xD623B4

#define mmNIC2_QM1_CP_STS_4                                          0xD623B8

#define mmNIC2_QM1_CP_CURRENT_INST_LO_0                              0xD623BC

#define mmNIC2_QM1_CP_CURRENT_INST_LO_1                              0xD623C0

#define mmNIC2_QM1_CP_CURRENT_INST_LO_2                              0xD623C4

#define mmNIC2_QM1_CP_CURRENT_INST_LO_3                              0xD623C8

#define mmNIC2_QM1_CP_CURRENT_INST_LO_4                              0xD623CC

#define mmNIC2_QM1_CP_CURRENT_INST_HI_0                              0xD623D0

#define mmNIC2_QM1_CP_CURRENT_INST_HI_1                              0xD623D4

#define mmNIC2_QM1_CP_CURRENT_INST_HI_2                              0xD623D8

#define mmNIC2_QM1_CP_CURRENT_INST_HI_3                              0xD623DC

#define mmNIC2_QM1_CP_CURRENT_INST_HI_4                              0xD623E0

#define mmNIC2_QM1_CP_BARRIER_CFG_0                                  0xD623F4

#define mmNIC2_QM1_CP_BARRIER_CFG_1                                  0xD623F8

#define mmNIC2_QM1_CP_BARRIER_CFG_2                                  0xD623FC

#define mmNIC2_QM1_CP_BARRIER_CFG_3                                  0xD62400

#define mmNIC2_QM1_CP_BARRIER_CFG_4                                  0xD62404

#define mmNIC2_QM1_CP_DBG_0_0                                        0xD62408

#define mmNIC2_QM1_CP_DBG_0_1                                        0xD6240C

#define mmNIC2_QM1_CP_DBG_0_2                                        0xD62410

#define mmNIC2_QM1_CP_DBG_0_3                                        0xD62414

#define mmNIC2_QM1_CP_DBG_0_4                                        0xD62418

#define mmNIC2_QM1_CP_ARUSER_31_11_0                                 0xD6241C

#define mmNIC2_QM1_CP_ARUSER_31_11_1                                 0xD62420

#define mmNIC2_QM1_CP_ARUSER_31_11_2                                 0xD62424

#define mmNIC2_QM1_CP_ARUSER_31_11_3                                 0xD62428

#define mmNIC2_QM1_CP_ARUSER_31_11_4                                 0xD6242C

#define mmNIC2_QM1_CP_AWUSER_31_11_0                                 0xD62430

#define mmNIC2_QM1_CP_AWUSER_31_11_1                                 0xD62434

#define mmNIC2_QM1_CP_AWUSER_31_11_2                                 0xD62438

#define mmNIC2_QM1_CP_AWUSER_31_11_3                                 0xD6243C

#define mmNIC2_QM1_CP_AWUSER_31_11_4                                 0xD62440

#define mmNIC2_QM1_ARB_CFG_0                                         0xD62A00

#define mmNIC2_QM1_ARB_CHOISE_Q_PUSH                                 0xD62A04

#define mmNIC2_QM1_ARB_WRR_WEIGHT_0                                  0xD62A08

#define mmNIC2_QM1_ARB_WRR_WEIGHT_1                                  0xD62A0C

#define mmNIC2_QM1_ARB_WRR_WEIGHT_2                                  0xD62A10

#define mmNIC2_QM1_ARB_WRR_WEIGHT_3                                  0xD62A14

#define mmNIC2_QM1_ARB_CFG_1                                         0xD62A18

#define mmNIC2_QM1_ARB_MST_AVAIL_CRED_0                              0xD62A20

#define mmNIC2_QM1_ARB_MST_AVAIL_CRED_1                              0xD62A24

#define mmNIC2_QM1_ARB_MST_AVAIL_CRED_2                              0xD62A28

#define mmNIC2_QM1_ARB_MST_AVAIL_CRED_3                              0xD62A2C

#define mmNIC2_QM1_ARB_MST_AVAIL_CRED_4                              0xD62A30

#define mmNIC2_QM1_ARB_MST_AVAIL_CRED_5                              0xD62A34

#define mmNIC2_QM1_ARB_MST_AVAIL_CRED_6                              0xD62A38

#define mmNIC2_QM1_ARB_MST_AVAIL_CRED_7                              0xD62A3C

#define mmNIC2_QM1_ARB_MST_AVAIL_CRED_8                              0xD62A40

#define mmNIC2_QM1_ARB_MST_AVAIL_CRED_9                              0xD62A44

#define mmNIC2_QM1_ARB_MST_AVAIL_CRED_10                             0xD62A48

#define mmNIC2_QM1_ARB_MST_AVAIL_CRED_11                             0xD62A4C

#define mmNIC2_QM1_ARB_MST_AVAIL_CRED_12                             0xD62A50

#define mmNIC2_QM1_ARB_MST_AVAIL_CRED_13                             0xD62A54

#define mmNIC2_QM1_ARB_MST_AVAIL_CRED_14                             0xD62A58

#define mmNIC2_QM1_ARB_MST_AVAIL_CRED_15                             0xD62A5C

#define mmNIC2_QM1_ARB_MST_AVAIL_CRED_16                             0xD62A60

#define mmNIC2_QM1_ARB_MST_AVAIL_CRED_17                             0xD62A64

#define mmNIC2_QM1_ARB_MST_AVAIL_CRED_18                             0xD62A68

#define mmNIC2_QM1_ARB_MST_AVAIL_CRED_19                             0xD62A6C

#define mmNIC2_QM1_ARB_MST_AVAIL_CRED_20                             0xD62A70

#define mmNIC2_QM1_ARB_MST_AVAIL_CRED_21                             0xD62A74

#define mmNIC2_QM1_ARB_MST_AVAIL_CRED_22                             0xD62A78

#define mmNIC2_QM1_ARB_MST_AVAIL_CRED_23                             0xD62A7C

#define mmNIC2_QM1_ARB_MST_AVAIL_CRED_24                             0xD62A80

#define mmNIC2_QM1_ARB_MST_AVAIL_CRED_25                             0xD62A84

#define mmNIC2_QM1_ARB_MST_AVAIL_CRED_26                             0xD62A88

#define mmNIC2_QM1_ARB_MST_AVAIL_CRED_27                             0xD62A8C

#define mmNIC2_QM1_ARB_MST_AVAIL_CRED_28                             0xD62A90

#define mmNIC2_QM1_ARB_MST_AVAIL_CRED_29                             0xD62A94

#define mmNIC2_QM1_ARB_MST_AVAIL_CRED_30                             0xD62A98

#define mmNIC2_QM1_ARB_MST_AVAIL_CRED_31                             0xD62A9C

#define mmNIC2_QM1_ARB_MST_CRED_INC                                  0xD62AA0

#define mmNIC2_QM1_ARB_MST_CHOISE_PUSH_OFST_0                        0xD62AA4

#define mmNIC2_QM1_ARB_MST_CHOISE_PUSH_OFST_1                        0xD62AA8

#define mmNIC2_QM1_ARB_MST_CHOISE_PUSH_OFST_2                        0xD62AAC

#define mmNIC2_QM1_ARB_MST_CHOISE_PUSH_OFST_3                        0xD62AB0

#define mmNIC2_QM1_ARB_MST_CHOISE_PUSH_OFST_4                        0xD62AB4

#define mmNIC2_QM1_ARB_MST_CHOISE_PUSH_OFST_5                        0xD62AB8

#define mmNIC2_QM1_ARB_MST_CHOISE_PUSH_OFST_6                        0xD62ABC

#define mmNIC2_QM1_ARB_MST_CHOISE_PUSH_OFST_7                        0xD62AC0

#define mmNIC2_QM1_ARB_MST_CHOISE_PUSH_OFST_8                        0xD62AC4

#define mmNIC2_QM1_ARB_MST_CHOISE_PUSH_OFST_9                        0xD62AC8

#define mmNIC2_QM1_ARB_MST_CHOISE_PUSH_OFST_10                       0xD62ACC

#define mmNIC2_QM1_ARB_MST_CHOISE_PUSH_OFST_11                       0xD62AD0

#define mmNIC2_QM1_ARB_MST_CHOISE_PUSH_OFST_12                       0xD62AD4

#define mmNIC2_QM1_ARB_MST_CHOISE_PUSH_OFST_13                       0xD62AD8

#define mmNIC2_QM1_ARB_MST_CHOISE_PUSH_OFST_14                       0xD62ADC

#define mmNIC2_QM1_ARB_MST_CHOISE_PUSH_OFST_15                       0xD62AE0

#define mmNIC2_QM1_ARB_MST_CHOISE_PUSH_OFST_16                       0xD62AE4

#define mmNIC2_QM1_ARB_MST_CHOISE_PUSH_OFST_17                       0xD62AE8

#define mmNIC2_QM1_ARB_MST_CHOISE_PUSH_OFST_18                       0xD62AEC

#define mmNIC2_QM1_ARB_MST_CHOISE_PUSH_OFST_19                       0xD62AF0

#define mmNIC2_QM1_ARB_MST_CHOISE_PUSH_OFST_20                       0xD62AF4

#define mmNIC2_QM1_ARB_MST_CHOISE_PUSH_OFST_21                       0xD62AF8

#define mmNIC2_QM1_ARB_MST_CHOISE_PUSH_OFST_22                       0xD62AFC

#define mmNIC2_QM1_ARB_MST_CHOISE_PUSH_OFST_23                       0xD62B00

#define mmNIC2_QM1_ARB_MST_CHOISE_PUSH_OFST_24                       0xD62B04

#define mmNIC2_QM1_ARB_MST_CHOISE_PUSH_OFST_25                       0xD62B08

#define mmNIC2_QM1_ARB_MST_CHOISE_PUSH_OFST_26                       0xD62B0C

#define mmNIC2_QM1_ARB_MST_CHOISE_PUSH_OFST_27                       0xD62B10

#define mmNIC2_QM1_ARB_MST_CHOISE_PUSH_OFST_28                       0xD62B14

#define mmNIC2_QM1_ARB_MST_CHOISE_PUSH_OFST_29                       0xD62B18

#define mmNIC2_QM1_ARB_MST_CHOISE_PUSH_OFST_30                       0xD62B1C

#define mmNIC2_QM1_ARB_MST_CHOISE_PUSH_OFST_31                       0xD62B20

#define mmNIC2_QM1_ARB_SLV_MASTER_INC_CRED_OFST                      0xD62B28

#define mmNIC2_QM1_ARB_MST_SLAVE_EN                                  0xD62B2C

#define mmNIC2_QM1_ARB_MST_QUIET_PER                                 0xD62B34

#define mmNIC2_QM1_ARB_SLV_CHOISE_WDT                                0xD62B38

#define mmNIC2_QM1_ARB_SLV_ID                                        0xD62B3C

#define mmNIC2_QM1_ARB_MSG_MAX_INFLIGHT                              0xD62B44

#define mmNIC2_QM1_ARB_MSG_AWUSER_31_11                              0xD62B48

#define mmNIC2_QM1_ARB_MSG_AWUSER_SEC_PROP                           0xD62B4C

#define mmNIC2_QM1_ARB_MSG_AWUSER_NON_SEC_PROP                       0xD62B50

#define mmNIC2_QM1_ARB_BASE_LO                                       0xD62B54

#define mmNIC2_QM1_ARB_BASE_HI                                       0xD62B58

#define mmNIC2_QM1_ARB_STATE_STS                                     0xD62B80

#define mmNIC2_QM1_ARB_CHOISE_FULLNESS_STS                           0xD62B84

#define mmNIC2_QM1_ARB_MSG_STS                                       0xD62B88

#define mmNIC2_QM1_ARB_SLV_CHOISE_Q_HEAD                             0xD62B8C

#define mmNIC2_QM1_ARB_ERR_CAUSE                                     0xD62B9C

#define mmNIC2_QM1_ARB_ERR_MSG_EN                                    0xD62BA0

#define mmNIC2_QM1_ARB_ERR_STS_DRP                                   0xD62BA8

#define mmNIC2_QM1_ARB_MST_CRED_STS_0                                0xD62BB0

#define mmNIC2_QM1_ARB_MST_CRED_STS_1                                0xD62BB4

#define mmNIC2_QM1_ARB_MST_CRED_STS_2                                0xD62BB8

#define mmNIC2_QM1_ARB_MST_CRED_STS_3                                0xD62BBC

#define mmNIC2_QM1_ARB_MST_CRED_STS_4                                0xD62BC0

#define mmNIC2_QM1_ARB_MST_CRED_STS_5                                0xD62BC4

#define mmNIC2_QM1_ARB_MST_CRED_STS_6                                0xD62BC8

#define mmNIC2_QM1_ARB_MST_CRED_STS_7                                0xD62BCC

#define mmNIC2_QM1_ARB_MST_CRED_STS_8                                0xD62BD0

#define mmNIC2_QM1_ARB_MST_CRED_STS_9                                0xD62BD4

#define mmNIC2_QM1_ARB_MST_CRED_STS_10                               0xD62BD8

#define mmNIC2_QM1_ARB_MST_CRED_STS_11                               0xD62BDC

#define mmNIC2_QM1_ARB_MST_CRED_STS_12                               0xD62BE0

#define mmNIC2_QM1_ARB_MST_CRED_STS_13                               0xD62BE4

#define mmNIC2_QM1_ARB_MST_CRED_STS_14                               0xD62BE8

#define mmNIC2_QM1_ARB_MST_CRED_STS_15                               0xD62BEC

#define mmNIC2_QM1_ARB_MST_CRED_STS_16                               0xD62BF0

#define mmNIC2_QM1_ARB_MST_CRED_STS_17                               0xD62BF4

#define mmNIC2_QM1_ARB_MST_CRED_STS_18                               0xD62BF8

#define mmNIC2_QM1_ARB_MST_CRED_STS_19                               0xD62BFC

#define mmNIC2_QM1_ARB_MST_CRED_STS_20                               0xD62C00

#define mmNIC2_QM1_ARB_MST_CRED_STS_21                               0xD62C04

#define mmNIC2_QM1_ARB_MST_CRED_STS_22                               0xD62C08

#define mmNIC2_QM1_ARB_MST_CRED_STS_23                               0xD62C0C

#define mmNIC2_QM1_ARB_MST_CRED_STS_24                               0xD62C10

#define mmNIC2_QM1_ARB_MST_CRED_STS_25                               0xD62C14

#define mmNIC2_QM1_ARB_MST_CRED_STS_26                               0xD62C18

#define mmNIC2_QM1_ARB_MST_CRED_STS_27                               0xD62C1C

#define mmNIC2_QM1_ARB_MST_CRED_STS_28                               0xD62C20

#define mmNIC2_QM1_ARB_MST_CRED_STS_29                               0xD62C24

#define mmNIC2_QM1_ARB_MST_CRED_STS_30                               0xD62C28

#define mmNIC2_QM1_ARB_MST_CRED_STS_31                               0xD62C2C

#define mmNIC2_QM1_CGM_CFG                                           0xD62C70

#define mmNIC2_QM1_CGM_STS                                           0xD62C74

#define mmNIC2_QM1_CGM_CFG1                                          0xD62C78

#define mmNIC2_QM1_LOCAL_RANGE_BASE                                  0xD62C80

#define mmNIC2_QM1_LOCAL_RANGE_SIZE                                  0xD62C84

#define mmNIC2_QM1_CSMR_STRICT_PRIO_CFG                              0xD62C90

#define mmNIC2_QM1_HBW_RD_RATE_LIM_CFG_1                             0xD62C94

#define mmNIC2_QM1_LBW_WR_RATE_LIM_CFG_0                             0xD62C98

#define mmNIC2_QM1_LBW_WR_RATE_LIM_CFG_1                             0xD62C9C

#define mmNIC2_QM1_HBW_RD_RATE_LIM_CFG_0                             0xD62CA0

#define mmNIC2_QM1_GLBL_AXCACHE                                      0xD62CA4

#define mmNIC2_QM1_IND_GW_APB_CFG                                    0xD62CB0

#define mmNIC2_QM1_IND_GW_APB_WDATA                                  0xD62CB4

#define mmNIC2_QM1_IND_GW_APB_RDATA                                  0xD62CB8

#define mmNIC2_QM1_IND_GW_APB_STATUS                                 0xD62CBC

#define mmNIC2_QM1_GLBL_ERR_ADDR_LO                                  0xD62CD0

#define mmNIC2_QM1_GLBL_ERR_ADDR_HI                                  0xD62CD4

#define mmNIC2_QM1_GLBL_ERR_WDATA                                    0xD62CD8

#define mmNIC2_QM1_GLBL_MEM_INIT_BUSY                                0xD62D00

#endif /* ASIC_REG_NIC2_QM1_REGS_H_ */
