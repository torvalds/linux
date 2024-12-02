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

#ifndef ASIC_REG_NIC2_QM0_REGS_H_
#define ASIC_REG_NIC2_QM0_REGS_H_

/*
 *****************************************
 *   NIC2_QM0 (Prototype: QMAN)
 *****************************************
 */

#define mmNIC2_QM0_GLBL_CFG0                                         0xD60000

#define mmNIC2_QM0_GLBL_CFG1                                         0xD60004

#define mmNIC2_QM0_GLBL_PROT                                         0xD60008

#define mmNIC2_QM0_GLBL_ERR_CFG                                      0xD6000C

#define mmNIC2_QM0_GLBL_SECURE_PROPS_0                               0xD60010

#define mmNIC2_QM0_GLBL_SECURE_PROPS_1                               0xD60014

#define mmNIC2_QM0_GLBL_SECURE_PROPS_2                               0xD60018

#define mmNIC2_QM0_GLBL_SECURE_PROPS_3                               0xD6001C

#define mmNIC2_QM0_GLBL_SECURE_PROPS_4                               0xD60020

#define mmNIC2_QM0_GLBL_NON_SECURE_PROPS_0                           0xD60024

#define mmNIC2_QM0_GLBL_NON_SECURE_PROPS_1                           0xD60028

#define mmNIC2_QM0_GLBL_NON_SECURE_PROPS_2                           0xD6002C

#define mmNIC2_QM0_GLBL_NON_SECURE_PROPS_3                           0xD60030

#define mmNIC2_QM0_GLBL_NON_SECURE_PROPS_4                           0xD60034

#define mmNIC2_QM0_GLBL_STS0                                         0xD60038

#define mmNIC2_QM0_GLBL_STS1_0                                       0xD60040

#define mmNIC2_QM0_GLBL_STS1_1                                       0xD60044

#define mmNIC2_QM0_GLBL_STS1_2                                       0xD60048

#define mmNIC2_QM0_GLBL_STS1_3                                       0xD6004C

#define mmNIC2_QM0_GLBL_STS1_4                                       0xD60050

#define mmNIC2_QM0_GLBL_MSG_EN_0                                     0xD60054

#define mmNIC2_QM0_GLBL_MSG_EN_1                                     0xD60058

#define mmNIC2_QM0_GLBL_MSG_EN_2                                     0xD6005C

#define mmNIC2_QM0_GLBL_MSG_EN_3                                     0xD60060

#define mmNIC2_QM0_GLBL_MSG_EN_4                                     0xD60068

#define mmNIC2_QM0_PQ_BASE_LO_0                                      0xD60070

#define mmNIC2_QM0_PQ_BASE_LO_1                                      0xD60074

#define mmNIC2_QM0_PQ_BASE_LO_2                                      0xD60078

#define mmNIC2_QM0_PQ_BASE_LO_3                                      0xD6007C

#define mmNIC2_QM0_PQ_BASE_HI_0                                      0xD60080

#define mmNIC2_QM0_PQ_BASE_HI_1                                      0xD60084

#define mmNIC2_QM0_PQ_BASE_HI_2                                      0xD60088

#define mmNIC2_QM0_PQ_BASE_HI_3                                      0xD6008C

#define mmNIC2_QM0_PQ_SIZE_0                                         0xD60090

#define mmNIC2_QM0_PQ_SIZE_1                                         0xD60094

#define mmNIC2_QM0_PQ_SIZE_2                                         0xD60098

#define mmNIC2_QM0_PQ_SIZE_3                                         0xD6009C

#define mmNIC2_QM0_PQ_PI_0                                           0xD600A0

#define mmNIC2_QM0_PQ_PI_1                                           0xD600A4

#define mmNIC2_QM0_PQ_PI_2                                           0xD600A8

#define mmNIC2_QM0_PQ_PI_3                                           0xD600AC

#define mmNIC2_QM0_PQ_CI_0                                           0xD600B0

#define mmNIC2_QM0_PQ_CI_1                                           0xD600B4

#define mmNIC2_QM0_PQ_CI_2                                           0xD600B8

#define mmNIC2_QM0_PQ_CI_3                                           0xD600BC

#define mmNIC2_QM0_PQ_CFG0_0                                         0xD600C0

#define mmNIC2_QM0_PQ_CFG0_1                                         0xD600C4

#define mmNIC2_QM0_PQ_CFG0_2                                         0xD600C8

#define mmNIC2_QM0_PQ_CFG0_3                                         0xD600CC

#define mmNIC2_QM0_PQ_CFG1_0                                         0xD600D0

#define mmNIC2_QM0_PQ_CFG1_1                                         0xD600D4

#define mmNIC2_QM0_PQ_CFG1_2                                         0xD600D8

#define mmNIC2_QM0_PQ_CFG1_3                                         0xD600DC

#define mmNIC2_QM0_PQ_ARUSER_31_11_0                                 0xD600E0

#define mmNIC2_QM0_PQ_ARUSER_31_11_1                                 0xD600E4

#define mmNIC2_QM0_PQ_ARUSER_31_11_2                                 0xD600E8

#define mmNIC2_QM0_PQ_ARUSER_31_11_3                                 0xD600EC

#define mmNIC2_QM0_PQ_STS0_0                                         0xD600F0

#define mmNIC2_QM0_PQ_STS0_1                                         0xD600F4

#define mmNIC2_QM0_PQ_STS0_2                                         0xD600F8

#define mmNIC2_QM0_PQ_STS0_3                                         0xD600FC

#define mmNIC2_QM0_PQ_STS1_0                                         0xD60100

#define mmNIC2_QM0_PQ_STS1_1                                         0xD60104

#define mmNIC2_QM0_PQ_STS1_2                                         0xD60108

#define mmNIC2_QM0_PQ_STS1_3                                         0xD6010C

#define mmNIC2_QM0_CQ_CFG0_0                                         0xD60110

#define mmNIC2_QM0_CQ_CFG0_1                                         0xD60114

#define mmNIC2_QM0_CQ_CFG0_2                                         0xD60118

#define mmNIC2_QM0_CQ_CFG0_3                                         0xD6011C

#define mmNIC2_QM0_CQ_CFG0_4                                         0xD60120

#define mmNIC2_QM0_CQ_CFG1_0                                         0xD60124

#define mmNIC2_QM0_CQ_CFG1_1                                         0xD60128

#define mmNIC2_QM0_CQ_CFG1_2                                         0xD6012C

#define mmNIC2_QM0_CQ_CFG1_3                                         0xD60130

#define mmNIC2_QM0_CQ_CFG1_4                                         0xD60134

#define mmNIC2_QM0_CQ_ARUSER_31_11_0                                 0xD60138

#define mmNIC2_QM0_CQ_ARUSER_31_11_1                                 0xD6013C

#define mmNIC2_QM0_CQ_ARUSER_31_11_2                                 0xD60140

#define mmNIC2_QM0_CQ_ARUSER_31_11_3                                 0xD60144

#define mmNIC2_QM0_CQ_ARUSER_31_11_4                                 0xD60148

#define mmNIC2_QM0_CQ_STS0_0                                         0xD6014C

#define mmNIC2_QM0_CQ_STS0_1                                         0xD60150

#define mmNIC2_QM0_CQ_STS0_2                                         0xD60154

#define mmNIC2_QM0_CQ_STS0_3                                         0xD60158

#define mmNIC2_QM0_CQ_STS0_4                                         0xD6015C

#define mmNIC2_QM0_CQ_STS1_0                                         0xD60160

#define mmNIC2_QM0_CQ_STS1_1                                         0xD60164

#define mmNIC2_QM0_CQ_STS1_2                                         0xD60168

#define mmNIC2_QM0_CQ_STS1_3                                         0xD6016C

#define mmNIC2_QM0_CQ_STS1_4                                         0xD60170

#define mmNIC2_QM0_CQ_PTR_LO_0                                       0xD60174

#define mmNIC2_QM0_CQ_PTR_HI_0                                       0xD60178

#define mmNIC2_QM0_CQ_TSIZE_0                                        0xD6017C

#define mmNIC2_QM0_CQ_CTL_0                                          0xD60180

#define mmNIC2_QM0_CQ_PTR_LO_1                                       0xD60184

#define mmNIC2_QM0_CQ_PTR_HI_1                                       0xD60188

#define mmNIC2_QM0_CQ_TSIZE_1                                        0xD6018C

#define mmNIC2_QM0_CQ_CTL_1                                          0xD60190

#define mmNIC2_QM0_CQ_PTR_LO_2                                       0xD60194

#define mmNIC2_QM0_CQ_PTR_HI_2                                       0xD60198

#define mmNIC2_QM0_CQ_TSIZE_2                                        0xD6019C

#define mmNIC2_QM0_CQ_CTL_2                                          0xD601A0

#define mmNIC2_QM0_CQ_PTR_LO_3                                       0xD601A4

#define mmNIC2_QM0_CQ_PTR_HI_3                                       0xD601A8

#define mmNIC2_QM0_CQ_TSIZE_3                                        0xD601AC

#define mmNIC2_QM0_CQ_CTL_3                                          0xD601B0

#define mmNIC2_QM0_CQ_PTR_LO_4                                       0xD601B4

#define mmNIC2_QM0_CQ_PTR_HI_4                                       0xD601B8

#define mmNIC2_QM0_CQ_TSIZE_4                                        0xD601BC

#define mmNIC2_QM0_CQ_CTL_4                                          0xD601C0

#define mmNIC2_QM0_CQ_PTR_LO_STS_0                                   0xD601C4

#define mmNIC2_QM0_CQ_PTR_LO_STS_1                                   0xD601C8

#define mmNIC2_QM0_CQ_PTR_LO_STS_2                                   0xD601CC

#define mmNIC2_QM0_CQ_PTR_LO_STS_3                                   0xD601D0

#define mmNIC2_QM0_CQ_PTR_LO_STS_4                                   0xD601D4

#define mmNIC2_QM0_CQ_PTR_HI_STS_0                                   0xD601D8

#define mmNIC2_QM0_CQ_PTR_HI_STS_1                                   0xD601DC

#define mmNIC2_QM0_CQ_PTR_HI_STS_2                                   0xD601E0

#define mmNIC2_QM0_CQ_PTR_HI_STS_3                                   0xD601E4

#define mmNIC2_QM0_CQ_PTR_HI_STS_4                                   0xD601E8

#define mmNIC2_QM0_CQ_TSIZE_STS_0                                    0xD601EC

#define mmNIC2_QM0_CQ_TSIZE_STS_1                                    0xD601F0

#define mmNIC2_QM0_CQ_TSIZE_STS_2                                    0xD601F4

#define mmNIC2_QM0_CQ_TSIZE_STS_3                                    0xD601F8

#define mmNIC2_QM0_CQ_TSIZE_STS_4                                    0xD601FC

#define mmNIC2_QM0_CQ_CTL_STS_0                                      0xD60200

#define mmNIC2_QM0_CQ_CTL_STS_1                                      0xD60204

#define mmNIC2_QM0_CQ_CTL_STS_2                                      0xD60208

#define mmNIC2_QM0_CQ_CTL_STS_3                                      0xD6020C

#define mmNIC2_QM0_CQ_CTL_STS_4                                      0xD60210

#define mmNIC2_QM0_CQ_IFIFO_CNT_0                                    0xD60214

#define mmNIC2_QM0_CQ_IFIFO_CNT_1                                    0xD60218

#define mmNIC2_QM0_CQ_IFIFO_CNT_2                                    0xD6021C

#define mmNIC2_QM0_CQ_IFIFO_CNT_3                                    0xD60220

#define mmNIC2_QM0_CQ_IFIFO_CNT_4                                    0xD60224

#define mmNIC2_QM0_CP_MSG_BASE0_ADDR_LO_0                            0xD60228

#define mmNIC2_QM0_CP_MSG_BASE0_ADDR_LO_1                            0xD6022C

#define mmNIC2_QM0_CP_MSG_BASE0_ADDR_LO_2                            0xD60230

#define mmNIC2_QM0_CP_MSG_BASE0_ADDR_LO_3                            0xD60234

#define mmNIC2_QM0_CP_MSG_BASE0_ADDR_LO_4                            0xD60238

#define mmNIC2_QM0_CP_MSG_BASE0_ADDR_HI_0                            0xD6023C

#define mmNIC2_QM0_CP_MSG_BASE0_ADDR_HI_1                            0xD60240

#define mmNIC2_QM0_CP_MSG_BASE0_ADDR_HI_2                            0xD60244

#define mmNIC2_QM0_CP_MSG_BASE0_ADDR_HI_3                            0xD60248

#define mmNIC2_QM0_CP_MSG_BASE0_ADDR_HI_4                            0xD6024C

#define mmNIC2_QM0_CP_MSG_BASE1_ADDR_LO_0                            0xD60250

#define mmNIC2_QM0_CP_MSG_BASE1_ADDR_LO_1                            0xD60254

#define mmNIC2_QM0_CP_MSG_BASE1_ADDR_LO_2                            0xD60258

#define mmNIC2_QM0_CP_MSG_BASE1_ADDR_LO_3                            0xD6025C

#define mmNIC2_QM0_CP_MSG_BASE1_ADDR_LO_4                            0xD60260

#define mmNIC2_QM0_CP_MSG_BASE1_ADDR_HI_0                            0xD60264

#define mmNIC2_QM0_CP_MSG_BASE1_ADDR_HI_1                            0xD60268

#define mmNIC2_QM0_CP_MSG_BASE1_ADDR_HI_2                            0xD6026C

#define mmNIC2_QM0_CP_MSG_BASE1_ADDR_HI_3                            0xD60270

#define mmNIC2_QM0_CP_MSG_BASE1_ADDR_HI_4                            0xD60274

#define mmNIC2_QM0_CP_MSG_BASE2_ADDR_LO_0                            0xD60278

#define mmNIC2_QM0_CP_MSG_BASE2_ADDR_LO_1                            0xD6027C

#define mmNIC2_QM0_CP_MSG_BASE2_ADDR_LO_2                            0xD60280

#define mmNIC2_QM0_CP_MSG_BASE2_ADDR_LO_3                            0xD60284

#define mmNIC2_QM0_CP_MSG_BASE2_ADDR_LO_4                            0xD60288

#define mmNIC2_QM0_CP_MSG_BASE2_ADDR_HI_0                            0xD6028C

#define mmNIC2_QM0_CP_MSG_BASE2_ADDR_HI_1                            0xD60290

#define mmNIC2_QM0_CP_MSG_BASE2_ADDR_HI_2                            0xD60294

#define mmNIC2_QM0_CP_MSG_BASE2_ADDR_HI_3                            0xD60298

#define mmNIC2_QM0_CP_MSG_BASE2_ADDR_HI_4                            0xD6029C

#define mmNIC2_QM0_CP_MSG_BASE3_ADDR_LO_0                            0xD602A0

#define mmNIC2_QM0_CP_MSG_BASE3_ADDR_LO_1                            0xD602A4

#define mmNIC2_QM0_CP_MSG_BASE3_ADDR_LO_2                            0xD602A8

#define mmNIC2_QM0_CP_MSG_BASE3_ADDR_LO_3                            0xD602AC

#define mmNIC2_QM0_CP_MSG_BASE3_ADDR_LO_4                            0xD602B0

#define mmNIC2_QM0_CP_MSG_BASE3_ADDR_HI_0                            0xD602B4

#define mmNIC2_QM0_CP_MSG_BASE3_ADDR_HI_1                            0xD602B8

#define mmNIC2_QM0_CP_MSG_BASE3_ADDR_HI_2                            0xD602BC

#define mmNIC2_QM0_CP_MSG_BASE3_ADDR_HI_3                            0xD602C0

#define mmNIC2_QM0_CP_MSG_BASE3_ADDR_HI_4                            0xD602C4

#define mmNIC2_QM0_CP_LDMA_TSIZE_OFFSET_0                            0xD602C8

#define mmNIC2_QM0_CP_LDMA_TSIZE_OFFSET_1                            0xD602CC

#define mmNIC2_QM0_CP_LDMA_TSIZE_OFFSET_2                            0xD602D0

#define mmNIC2_QM0_CP_LDMA_TSIZE_OFFSET_3                            0xD602D4

#define mmNIC2_QM0_CP_LDMA_TSIZE_OFFSET_4                            0xD602D8

#define mmNIC2_QM0_CP_LDMA_SRC_BASE_LO_OFFSET_0                      0xD602E0

#define mmNIC2_QM0_CP_LDMA_SRC_BASE_LO_OFFSET_1                      0xD602E4

#define mmNIC2_QM0_CP_LDMA_SRC_BASE_LO_OFFSET_2                      0xD602E8

#define mmNIC2_QM0_CP_LDMA_SRC_BASE_LO_OFFSET_3                      0xD602EC

#define mmNIC2_QM0_CP_LDMA_SRC_BASE_LO_OFFSET_4                      0xD602F0

#define mmNIC2_QM0_CP_LDMA_DST_BASE_LO_OFFSET_0                      0xD602F4

#define mmNIC2_QM0_CP_LDMA_DST_BASE_LO_OFFSET_1                      0xD602F8

#define mmNIC2_QM0_CP_LDMA_DST_BASE_LO_OFFSET_2                      0xD602FC

#define mmNIC2_QM0_CP_LDMA_DST_BASE_LO_OFFSET_3                      0xD60300

#define mmNIC2_QM0_CP_LDMA_DST_BASE_LO_OFFSET_4                      0xD60304

#define mmNIC2_QM0_CP_FENCE0_RDATA_0                                 0xD60308

#define mmNIC2_QM0_CP_FENCE0_RDATA_1                                 0xD6030C

#define mmNIC2_QM0_CP_FENCE0_RDATA_2                                 0xD60310

#define mmNIC2_QM0_CP_FENCE0_RDATA_3                                 0xD60314

#define mmNIC2_QM0_CP_FENCE0_RDATA_4                                 0xD60318

#define mmNIC2_QM0_CP_FENCE1_RDATA_0                                 0xD6031C

#define mmNIC2_QM0_CP_FENCE1_RDATA_1                                 0xD60320

#define mmNIC2_QM0_CP_FENCE1_RDATA_2                                 0xD60324

#define mmNIC2_QM0_CP_FENCE1_RDATA_3                                 0xD60328

#define mmNIC2_QM0_CP_FENCE1_RDATA_4                                 0xD6032C

#define mmNIC2_QM0_CP_FENCE2_RDATA_0                                 0xD60330

#define mmNIC2_QM0_CP_FENCE2_RDATA_1                                 0xD60334

#define mmNIC2_QM0_CP_FENCE2_RDATA_2                                 0xD60338

#define mmNIC2_QM0_CP_FENCE2_RDATA_3                                 0xD6033C

#define mmNIC2_QM0_CP_FENCE2_RDATA_4                                 0xD60340

#define mmNIC2_QM0_CP_FENCE3_RDATA_0                                 0xD60344

#define mmNIC2_QM0_CP_FENCE3_RDATA_1                                 0xD60348

#define mmNIC2_QM0_CP_FENCE3_RDATA_2                                 0xD6034C

#define mmNIC2_QM0_CP_FENCE3_RDATA_3                                 0xD60350

#define mmNIC2_QM0_CP_FENCE3_RDATA_4                                 0xD60354

#define mmNIC2_QM0_CP_FENCE0_CNT_0                                   0xD60358

#define mmNIC2_QM0_CP_FENCE0_CNT_1                                   0xD6035C

#define mmNIC2_QM0_CP_FENCE0_CNT_2                                   0xD60360

#define mmNIC2_QM0_CP_FENCE0_CNT_3                                   0xD60364

#define mmNIC2_QM0_CP_FENCE0_CNT_4                                   0xD60368

#define mmNIC2_QM0_CP_FENCE1_CNT_0                                   0xD6036C

#define mmNIC2_QM0_CP_FENCE1_CNT_1                                   0xD60370

#define mmNIC2_QM0_CP_FENCE1_CNT_2                                   0xD60374

#define mmNIC2_QM0_CP_FENCE1_CNT_3                                   0xD60378

#define mmNIC2_QM0_CP_FENCE1_CNT_4                                   0xD6037C

#define mmNIC2_QM0_CP_FENCE2_CNT_0                                   0xD60380

#define mmNIC2_QM0_CP_FENCE2_CNT_1                                   0xD60384

#define mmNIC2_QM0_CP_FENCE2_CNT_2                                   0xD60388

#define mmNIC2_QM0_CP_FENCE2_CNT_3                                   0xD6038C

#define mmNIC2_QM0_CP_FENCE2_CNT_4                                   0xD60390

#define mmNIC2_QM0_CP_FENCE3_CNT_0                                   0xD60394

#define mmNIC2_QM0_CP_FENCE3_CNT_1                                   0xD60398

#define mmNIC2_QM0_CP_FENCE3_CNT_2                                   0xD6039C

#define mmNIC2_QM0_CP_FENCE3_CNT_3                                   0xD603A0

#define mmNIC2_QM0_CP_FENCE3_CNT_4                                   0xD603A4

#define mmNIC2_QM0_CP_STS_0                                          0xD603A8

#define mmNIC2_QM0_CP_STS_1                                          0xD603AC

#define mmNIC2_QM0_CP_STS_2                                          0xD603B0

#define mmNIC2_QM0_CP_STS_3                                          0xD603B4

#define mmNIC2_QM0_CP_STS_4                                          0xD603B8

#define mmNIC2_QM0_CP_CURRENT_INST_LO_0                              0xD603BC

#define mmNIC2_QM0_CP_CURRENT_INST_LO_1                              0xD603C0

#define mmNIC2_QM0_CP_CURRENT_INST_LO_2                              0xD603C4

#define mmNIC2_QM0_CP_CURRENT_INST_LO_3                              0xD603C8

#define mmNIC2_QM0_CP_CURRENT_INST_LO_4                              0xD603CC

#define mmNIC2_QM0_CP_CURRENT_INST_HI_0                              0xD603D0

#define mmNIC2_QM0_CP_CURRENT_INST_HI_1                              0xD603D4

#define mmNIC2_QM0_CP_CURRENT_INST_HI_2                              0xD603D8

#define mmNIC2_QM0_CP_CURRENT_INST_HI_3                              0xD603DC

#define mmNIC2_QM0_CP_CURRENT_INST_HI_4                              0xD603E0

#define mmNIC2_QM0_CP_BARRIER_CFG_0                                  0xD603F4

#define mmNIC2_QM0_CP_BARRIER_CFG_1                                  0xD603F8

#define mmNIC2_QM0_CP_BARRIER_CFG_2                                  0xD603FC

#define mmNIC2_QM0_CP_BARRIER_CFG_3                                  0xD60400

#define mmNIC2_QM0_CP_BARRIER_CFG_4                                  0xD60404

#define mmNIC2_QM0_CP_DBG_0_0                                        0xD60408

#define mmNIC2_QM0_CP_DBG_0_1                                        0xD6040C

#define mmNIC2_QM0_CP_DBG_0_2                                        0xD60410

#define mmNIC2_QM0_CP_DBG_0_3                                        0xD60414

#define mmNIC2_QM0_CP_DBG_0_4                                        0xD60418

#define mmNIC2_QM0_CP_ARUSER_31_11_0                                 0xD6041C

#define mmNIC2_QM0_CP_ARUSER_31_11_1                                 0xD60420

#define mmNIC2_QM0_CP_ARUSER_31_11_2                                 0xD60424

#define mmNIC2_QM0_CP_ARUSER_31_11_3                                 0xD60428

#define mmNIC2_QM0_CP_ARUSER_31_11_4                                 0xD6042C

#define mmNIC2_QM0_CP_AWUSER_31_11_0                                 0xD60430

#define mmNIC2_QM0_CP_AWUSER_31_11_1                                 0xD60434

#define mmNIC2_QM0_CP_AWUSER_31_11_2                                 0xD60438

#define mmNIC2_QM0_CP_AWUSER_31_11_3                                 0xD6043C

#define mmNIC2_QM0_CP_AWUSER_31_11_4                                 0xD60440

#define mmNIC2_QM0_ARB_CFG_0                                         0xD60A00

#define mmNIC2_QM0_ARB_CHOISE_Q_PUSH                                 0xD60A04

#define mmNIC2_QM0_ARB_WRR_WEIGHT_0                                  0xD60A08

#define mmNIC2_QM0_ARB_WRR_WEIGHT_1                                  0xD60A0C

#define mmNIC2_QM0_ARB_WRR_WEIGHT_2                                  0xD60A10

#define mmNIC2_QM0_ARB_WRR_WEIGHT_3                                  0xD60A14

#define mmNIC2_QM0_ARB_CFG_1                                         0xD60A18

#define mmNIC2_QM0_ARB_MST_AVAIL_CRED_0                              0xD60A20

#define mmNIC2_QM0_ARB_MST_AVAIL_CRED_1                              0xD60A24

#define mmNIC2_QM0_ARB_MST_AVAIL_CRED_2                              0xD60A28

#define mmNIC2_QM0_ARB_MST_AVAIL_CRED_3                              0xD60A2C

#define mmNIC2_QM0_ARB_MST_AVAIL_CRED_4                              0xD60A30

#define mmNIC2_QM0_ARB_MST_AVAIL_CRED_5                              0xD60A34

#define mmNIC2_QM0_ARB_MST_AVAIL_CRED_6                              0xD60A38

#define mmNIC2_QM0_ARB_MST_AVAIL_CRED_7                              0xD60A3C

#define mmNIC2_QM0_ARB_MST_AVAIL_CRED_8                              0xD60A40

#define mmNIC2_QM0_ARB_MST_AVAIL_CRED_9                              0xD60A44

#define mmNIC2_QM0_ARB_MST_AVAIL_CRED_10                             0xD60A48

#define mmNIC2_QM0_ARB_MST_AVAIL_CRED_11                             0xD60A4C

#define mmNIC2_QM0_ARB_MST_AVAIL_CRED_12                             0xD60A50

#define mmNIC2_QM0_ARB_MST_AVAIL_CRED_13                             0xD60A54

#define mmNIC2_QM0_ARB_MST_AVAIL_CRED_14                             0xD60A58

#define mmNIC2_QM0_ARB_MST_AVAIL_CRED_15                             0xD60A5C

#define mmNIC2_QM0_ARB_MST_AVAIL_CRED_16                             0xD60A60

#define mmNIC2_QM0_ARB_MST_AVAIL_CRED_17                             0xD60A64

#define mmNIC2_QM0_ARB_MST_AVAIL_CRED_18                             0xD60A68

#define mmNIC2_QM0_ARB_MST_AVAIL_CRED_19                             0xD60A6C

#define mmNIC2_QM0_ARB_MST_AVAIL_CRED_20                             0xD60A70

#define mmNIC2_QM0_ARB_MST_AVAIL_CRED_21                             0xD60A74

#define mmNIC2_QM0_ARB_MST_AVAIL_CRED_22                             0xD60A78

#define mmNIC2_QM0_ARB_MST_AVAIL_CRED_23                             0xD60A7C

#define mmNIC2_QM0_ARB_MST_AVAIL_CRED_24                             0xD60A80

#define mmNIC2_QM0_ARB_MST_AVAIL_CRED_25                             0xD60A84

#define mmNIC2_QM0_ARB_MST_AVAIL_CRED_26                             0xD60A88

#define mmNIC2_QM0_ARB_MST_AVAIL_CRED_27                             0xD60A8C

#define mmNIC2_QM0_ARB_MST_AVAIL_CRED_28                             0xD60A90

#define mmNIC2_QM0_ARB_MST_AVAIL_CRED_29                             0xD60A94

#define mmNIC2_QM0_ARB_MST_AVAIL_CRED_30                             0xD60A98

#define mmNIC2_QM0_ARB_MST_AVAIL_CRED_31                             0xD60A9C

#define mmNIC2_QM0_ARB_MST_CRED_INC                                  0xD60AA0

#define mmNIC2_QM0_ARB_MST_CHOISE_PUSH_OFST_0                        0xD60AA4

#define mmNIC2_QM0_ARB_MST_CHOISE_PUSH_OFST_1                        0xD60AA8

#define mmNIC2_QM0_ARB_MST_CHOISE_PUSH_OFST_2                        0xD60AAC

#define mmNIC2_QM0_ARB_MST_CHOISE_PUSH_OFST_3                        0xD60AB0

#define mmNIC2_QM0_ARB_MST_CHOISE_PUSH_OFST_4                        0xD60AB4

#define mmNIC2_QM0_ARB_MST_CHOISE_PUSH_OFST_5                        0xD60AB8

#define mmNIC2_QM0_ARB_MST_CHOISE_PUSH_OFST_6                        0xD60ABC

#define mmNIC2_QM0_ARB_MST_CHOISE_PUSH_OFST_7                        0xD60AC0

#define mmNIC2_QM0_ARB_MST_CHOISE_PUSH_OFST_8                        0xD60AC4

#define mmNIC2_QM0_ARB_MST_CHOISE_PUSH_OFST_9                        0xD60AC8

#define mmNIC2_QM0_ARB_MST_CHOISE_PUSH_OFST_10                       0xD60ACC

#define mmNIC2_QM0_ARB_MST_CHOISE_PUSH_OFST_11                       0xD60AD0

#define mmNIC2_QM0_ARB_MST_CHOISE_PUSH_OFST_12                       0xD60AD4

#define mmNIC2_QM0_ARB_MST_CHOISE_PUSH_OFST_13                       0xD60AD8

#define mmNIC2_QM0_ARB_MST_CHOISE_PUSH_OFST_14                       0xD60ADC

#define mmNIC2_QM0_ARB_MST_CHOISE_PUSH_OFST_15                       0xD60AE0

#define mmNIC2_QM0_ARB_MST_CHOISE_PUSH_OFST_16                       0xD60AE4

#define mmNIC2_QM0_ARB_MST_CHOISE_PUSH_OFST_17                       0xD60AE8

#define mmNIC2_QM0_ARB_MST_CHOISE_PUSH_OFST_18                       0xD60AEC

#define mmNIC2_QM0_ARB_MST_CHOISE_PUSH_OFST_19                       0xD60AF0

#define mmNIC2_QM0_ARB_MST_CHOISE_PUSH_OFST_20                       0xD60AF4

#define mmNIC2_QM0_ARB_MST_CHOISE_PUSH_OFST_21                       0xD60AF8

#define mmNIC2_QM0_ARB_MST_CHOISE_PUSH_OFST_22                       0xD60AFC

#define mmNIC2_QM0_ARB_MST_CHOISE_PUSH_OFST_23                       0xD60B00

#define mmNIC2_QM0_ARB_MST_CHOISE_PUSH_OFST_24                       0xD60B04

#define mmNIC2_QM0_ARB_MST_CHOISE_PUSH_OFST_25                       0xD60B08

#define mmNIC2_QM0_ARB_MST_CHOISE_PUSH_OFST_26                       0xD60B0C

#define mmNIC2_QM0_ARB_MST_CHOISE_PUSH_OFST_27                       0xD60B10

#define mmNIC2_QM0_ARB_MST_CHOISE_PUSH_OFST_28                       0xD60B14

#define mmNIC2_QM0_ARB_MST_CHOISE_PUSH_OFST_29                       0xD60B18

#define mmNIC2_QM0_ARB_MST_CHOISE_PUSH_OFST_30                       0xD60B1C

#define mmNIC2_QM0_ARB_MST_CHOISE_PUSH_OFST_31                       0xD60B20

#define mmNIC2_QM0_ARB_SLV_MASTER_INC_CRED_OFST                      0xD60B28

#define mmNIC2_QM0_ARB_MST_SLAVE_EN                                  0xD60B2C

#define mmNIC2_QM0_ARB_MST_QUIET_PER                                 0xD60B34

#define mmNIC2_QM0_ARB_SLV_CHOISE_WDT                                0xD60B38

#define mmNIC2_QM0_ARB_SLV_ID                                        0xD60B3C

#define mmNIC2_QM0_ARB_MSG_MAX_INFLIGHT                              0xD60B44

#define mmNIC2_QM0_ARB_MSG_AWUSER_31_11                              0xD60B48

#define mmNIC2_QM0_ARB_MSG_AWUSER_SEC_PROP                           0xD60B4C

#define mmNIC2_QM0_ARB_MSG_AWUSER_NON_SEC_PROP                       0xD60B50

#define mmNIC2_QM0_ARB_BASE_LO                                       0xD60B54

#define mmNIC2_QM0_ARB_BASE_HI                                       0xD60B58

#define mmNIC2_QM0_ARB_STATE_STS                                     0xD60B80

#define mmNIC2_QM0_ARB_CHOISE_FULLNESS_STS                           0xD60B84

#define mmNIC2_QM0_ARB_MSG_STS                                       0xD60B88

#define mmNIC2_QM0_ARB_SLV_CHOISE_Q_HEAD                             0xD60B8C

#define mmNIC2_QM0_ARB_ERR_CAUSE                                     0xD60B9C

#define mmNIC2_QM0_ARB_ERR_MSG_EN                                    0xD60BA0

#define mmNIC2_QM0_ARB_ERR_STS_DRP                                   0xD60BA8

#define mmNIC2_QM0_ARB_MST_CRED_STS_0                                0xD60BB0

#define mmNIC2_QM0_ARB_MST_CRED_STS_1                                0xD60BB4

#define mmNIC2_QM0_ARB_MST_CRED_STS_2                                0xD60BB8

#define mmNIC2_QM0_ARB_MST_CRED_STS_3                                0xD60BBC

#define mmNIC2_QM0_ARB_MST_CRED_STS_4                                0xD60BC0

#define mmNIC2_QM0_ARB_MST_CRED_STS_5                                0xD60BC4

#define mmNIC2_QM0_ARB_MST_CRED_STS_6                                0xD60BC8

#define mmNIC2_QM0_ARB_MST_CRED_STS_7                                0xD60BCC

#define mmNIC2_QM0_ARB_MST_CRED_STS_8                                0xD60BD0

#define mmNIC2_QM0_ARB_MST_CRED_STS_9                                0xD60BD4

#define mmNIC2_QM0_ARB_MST_CRED_STS_10                               0xD60BD8

#define mmNIC2_QM0_ARB_MST_CRED_STS_11                               0xD60BDC

#define mmNIC2_QM0_ARB_MST_CRED_STS_12                               0xD60BE0

#define mmNIC2_QM0_ARB_MST_CRED_STS_13                               0xD60BE4

#define mmNIC2_QM0_ARB_MST_CRED_STS_14                               0xD60BE8

#define mmNIC2_QM0_ARB_MST_CRED_STS_15                               0xD60BEC

#define mmNIC2_QM0_ARB_MST_CRED_STS_16                               0xD60BF0

#define mmNIC2_QM0_ARB_MST_CRED_STS_17                               0xD60BF4

#define mmNIC2_QM0_ARB_MST_CRED_STS_18                               0xD60BF8

#define mmNIC2_QM0_ARB_MST_CRED_STS_19                               0xD60BFC

#define mmNIC2_QM0_ARB_MST_CRED_STS_20                               0xD60C00

#define mmNIC2_QM0_ARB_MST_CRED_STS_21                               0xD60C04

#define mmNIC2_QM0_ARB_MST_CRED_STS_22                               0xD60C08

#define mmNIC2_QM0_ARB_MST_CRED_STS_23                               0xD60C0C

#define mmNIC2_QM0_ARB_MST_CRED_STS_24                               0xD60C10

#define mmNIC2_QM0_ARB_MST_CRED_STS_25                               0xD60C14

#define mmNIC2_QM0_ARB_MST_CRED_STS_26                               0xD60C18

#define mmNIC2_QM0_ARB_MST_CRED_STS_27                               0xD60C1C

#define mmNIC2_QM0_ARB_MST_CRED_STS_28                               0xD60C20

#define mmNIC2_QM0_ARB_MST_CRED_STS_29                               0xD60C24

#define mmNIC2_QM0_ARB_MST_CRED_STS_30                               0xD60C28

#define mmNIC2_QM0_ARB_MST_CRED_STS_31                               0xD60C2C

#define mmNIC2_QM0_CGM_CFG                                           0xD60C70

#define mmNIC2_QM0_CGM_STS                                           0xD60C74

#define mmNIC2_QM0_CGM_CFG1                                          0xD60C78

#define mmNIC2_QM0_LOCAL_RANGE_BASE                                  0xD60C80

#define mmNIC2_QM0_LOCAL_RANGE_SIZE                                  0xD60C84

#define mmNIC2_QM0_CSMR_STRICT_PRIO_CFG                              0xD60C90

#define mmNIC2_QM0_HBW_RD_RATE_LIM_CFG_1                             0xD60C94

#define mmNIC2_QM0_LBW_WR_RATE_LIM_CFG_0                             0xD60C98

#define mmNIC2_QM0_LBW_WR_RATE_LIM_CFG_1                             0xD60C9C

#define mmNIC2_QM0_HBW_RD_RATE_LIM_CFG_0                             0xD60CA0

#define mmNIC2_QM0_GLBL_AXCACHE                                      0xD60CA4

#define mmNIC2_QM0_IND_GW_APB_CFG                                    0xD60CB0

#define mmNIC2_QM0_IND_GW_APB_WDATA                                  0xD60CB4

#define mmNIC2_QM0_IND_GW_APB_RDATA                                  0xD60CB8

#define mmNIC2_QM0_IND_GW_APB_STATUS                                 0xD60CBC

#define mmNIC2_QM0_GLBL_ERR_ADDR_LO                                  0xD60CD0

#define mmNIC2_QM0_GLBL_ERR_ADDR_HI                                  0xD60CD4

#define mmNIC2_QM0_GLBL_ERR_WDATA                                    0xD60CD8

#define mmNIC2_QM0_GLBL_MEM_INIT_BUSY                                0xD60D00

#endif /* ASIC_REG_NIC2_QM0_REGS_H_ */
