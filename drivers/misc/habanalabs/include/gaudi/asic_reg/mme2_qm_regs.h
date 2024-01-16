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

#ifndef ASIC_REG_MME2_QM_REGS_H_
#define ASIC_REG_MME2_QM_REGS_H_

/*
 *****************************************
 *   MME2_QM (Prototype: QMAN)
 *****************************************
 */

#define mmMME2_QM_GLBL_CFG0                                          0x168000

#define mmMME2_QM_GLBL_CFG1                                          0x168004

#define mmMME2_QM_GLBL_PROT                                          0x168008

#define mmMME2_QM_GLBL_ERR_CFG                                       0x16800C

#define mmMME2_QM_GLBL_SECURE_PROPS_0                                0x168010

#define mmMME2_QM_GLBL_SECURE_PROPS_1                                0x168014

#define mmMME2_QM_GLBL_SECURE_PROPS_2                                0x168018

#define mmMME2_QM_GLBL_SECURE_PROPS_3                                0x16801C

#define mmMME2_QM_GLBL_SECURE_PROPS_4                                0x168020

#define mmMME2_QM_GLBL_NON_SECURE_PROPS_0                            0x168024

#define mmMME2_QM_GLBL_NON_SECURE_PROPS_1                            0x168028

#define mmMME2_QM_GLBL_NON_SECURE_PROPS_2                            0x16802C

#define mmMME2_QM_GLBL_NON_SECURE_PROPS_3                            0x168030

#define mmMME2_QM_GLBL_NON_SECURE_PROPS_4                            0x168034

#define mmMME2_QM_GLBL_STS0                                          0x168038

#define mmMME2_QM_GLBL_STS1_0                                        0x168040

#define mmMME2_QM_GLBL_STS1_1                                        0x168044

#define mmMME2_QM_GLBL_STS1_2                                        0x168048

#define mmMME2_QM_GLBL_STS1_3                                        0x16804C

#define mmMME2_QM_GLBL_STS1_4                                        0x168050

#define mmMME2_QM_GLBL_MSG_EN_0                                      0x168054

#define mmMME2_QM_GLBL_MSG_EN_1                                      0x168058

#define mmMME2_QM_GLBL_MSG_EN_2                                      0x16805C

#define mmMME2_QM_GLBL_MSG_EN_3                                      0x168060

#define mmMME2_QM_GLBL_MSG_EN_4                                      0x168068

#define mmMME2_QM_PQ_BASE_LO_0                                       0x168070

#define mmMME2_QM_PQ_BASE_LO_1                                       0x168074

#define mmMME2_QM_PQ_BASE_LO_2                                       0x168078

#define mmMME2_QM_PQ_BASE_LO_3                                       0x16807C

#define mmMME2_QM_PQ_BASE_HI_0                                       0x168080

#define mmMME2_QM_PQ_BASE_HI_1                                       0x168084

#define mmMME2_QM_PQ_BASE_HI_2                                       0x168088

#define mmMME2_QM_PQ_BASE_HI_3                                       0x16808C

#define mmMME2_QM_PQ_SIZE_0                                          0x168090

#define mmMME2_QM_PQ_SIZE_1                                          0x168094

#define mmMME2_QM_PQ_SIZE_2                                          0x168098

#define mmMME2_QM_PQ_SIZE_3                                          0x16809C

#define mmMME2_QM_PQ_PI_0                                            0x1680A0

#define mmMME2_QM_PQ_PI_1                                            0x1680A4

#define mmMME2_QM_PQ_PI_2                                            0x1680A8

#define mmMME2_QM_PQ_PI_3                                            0x1680AC

#define mmMME2_QM_PQ_CI_0                                            0x1680B0

#define mmMME2_QM_PQ_CI_1                                            0x1680B4

#define mmMME2_QM_PQ_CI_2                                            0x1680B8

#define mmMME2_QM_PQ_CI_3                                            0x1680BC

#define mmMME2_QM_PQ_CFG0_0                                          0x1680C0

#define mmMME2_QM_PQ_CFG0_1                                          0x1680C4

#define mmMME2_QM_PQ_CFG0_2                                          0x1680C8

#define mmMME2_QM_PQ_CFG0_3                                          0x1680CC

#define mmMME2_QM_PQ_CFG1_0                                          0x1680D0

#define mmMME2_QM_PQ_CFG1_1                                          0x1680D4

#define mmMME2_QM_PQ_CFG1_2                                          0x1680D8

#define mmMME2_QM_PQ_CFG1_3                                          0x1680DC

#define mmMME2_QM_PQ_ARUSER_31_11_0                                  0x1680E0

#define mmMME2_QM_PQ_ARUSER_31_11_1                                  0x1680E4

#define mmMME2_QM_PQ_ARUSER_31_11_2                                  0x1680E8

#define mmMME2_QM_PQ_ARUSER_31_11_3                                  0x1680EC

#define mmMME2_QM_PQ_STS0_0                                          0x1680F0

#define mmMME2_QM_PQ_STS0_1                                          0x1680F4

#define mmMME2_QM_PQ_STS0_2                                          0x1680F8

#define mmMME2_QM_PQ_STS0_3                                          0x1680FC

#define mmMME2_QM_PQ_STS1_0                                          0x168100

#define mmMME2_QM_PQ_STS1_1                                          0x168104

#define mmMME2_QM_PQ_STS1_2                                          0x168108

#define mmMME2_QM_PQ_STS1_3                                          0x16810C

#define mmMME2_QM_CQ_CFG0_0                                          0x168110

#define mmMME2_QM_CQ_CFG0_1                                          0x168114

#define mmMME2_QM_CQ_CFG0_2                                          0x168118

#define mmMME2_QM_CQ_CFG0_3                                          0x16811C

#define mmMME2_QM_CQ_CFG0_4                                          0x168120

#define mmMME2_QM_CQ_CFG1_0                                          0x168124

#define mmMME2_QM_CQ_CFG1_1                                          0x168128

#define mmMME2_QM_CQ_CFG1_2                                          0x16812C

#define mmMME2_QM_CQ_CFG1_3                                          0x168130

#define mmMME2_QM_CQ_CFG1_4                                          0x168134

#define mmMME2_QM_CQ_ARUSER_31_11_0                                  0x168138

#define mmMME2_QM_CQ_ARUSER_31_11_1                                  0x16813C

#define mmMME2_QM_CQ_ARUSER_31_11_2                                  0x168140

#define mmMME2_QM_CQ_ARUSER_31_11_3                                  0x168144

#define mmMME2_QM_CQ_ARUSER_31_11_4                                  0x168148

#define mmMME2_QM_CQ_STS0_0                                          0x16814C

#define mmMME2_QM_CQ_STS0_1                                          0x168150

#define mmMME2_QM_CQ_STS0_2                                          0x168154

#define mmMME2_QM_CQ_STS0_3                                          0x168158

#define mmMME2_QM_CQ_STS0_4                                          0x16815C

#define mmMME2_QM_CQ_STS1_0                                          0x168160

#define mmMME2_QM_CQ_STS1_1                                          0x168164

#define mmMME2_QM_CQ_STS1_2                                          0x168168

#define mmMME2_QM_CQ_STS1_3                                          0x16816C

#define mmMME2_QM_CQ_STS1_4                                          0x168170

#define mmMME2_QM_CQ_PTR_LO_0                                        0x168174

#define mmMME2_QM_CQ_PTR_HI_0                                        0x168178

#define mmMME2_QM_CQ_TSIZE_0                                         0x16817C

#define mmMME2_QM_CQ_CTL_0                                           0x168180

#define mmMME2_QM_CQ_PTR_LO_1                                        0x168184

#define mmMME2_QM_CQ_PTR_HI_1                                        0x168188

#define mmMME2_QM_CQ_TSIZE_1                                         0x16818C

#define mmMME2_QM_CQ_CTL_1                                           0x168190

#define mmMME2_QM_CQ_PTR_LO_2                                        0x168194

#define mmMME2_QM_CQ_PTR_HI_2                                        0x168198

#define mmMME2_QM_CQ_TSIZE_2                                         0x16819C

#define mmMME2_QM_CQ_CTL_2                                           0x1681A0

#define mmMME2_QM_CQ_PTR_LO_3                                        0x1681A4

#define mmMME2_QM_CQ_PTR_HI_3                                        0x1681A8

#define mmMME2_QM_CQ_TSIZE_3                                         0x1681AC

#define mmMME2_QM_CQ_CTL_3                                           0x1681B0

#define mmMME2_QM_CQ_PTR_LO_4                                        0x1681B4

#define mmMME2_QM_CQ_PTR_HI_4                                        0x1681B8

#define mmMME2_QM_CQ_TSIZE_4                                         0x1681BC

#define mmMME2_QM_CQ_CTL_4                                           0x1681C0

#define mmMME2_QM_CQ_PTR_LO_STS_0                                    0x1681C4

#define mmMME2_QM_CQ_PTR_LO_STS_1                                    0x1681C8

#define mmMME2_QM_CQ_PTR_LO_STS_2                                    0x1681CC

#define mmMME2_QM_CQ_PTR_LO_STS_3                                    0x1681D0

#define mmMME2_QM_CQ_PTR_LO_STS_4                                    0x1681D4

#define mmMME2_QM_CQ_PTR_HI_STS_0                                    0x1681D8

#define mmMME2_QM_CQ_PTR_HI_STS_1                                    0x1681DC

#define mmMME2_QM_CQ_PTR_HI_STS_2                                    0x1681E0

#define mmMME2_QM_CQ_PTR_HI_STS_3                                    0x1681E4

#define mmMME2_QM_CQ_PTR_HI_STS_4                                    0x1681E8

#define mmMME2_QM_CQ_TSIZE_STS_0                                     0x1681EC

#define mmMME2_QM_CQ_TSIZE_STS_1                                     0x1681F0

#define mmMME2_QM_CQ_TSIZE_STS_2                                     0x1681F4

#define mmMME2_QM_CQ_TSIZE_STS_3                                     0x1681F8

#define mmMME2_QM_CQ_TSIZE_STS_4                                     0x1681FC

#define mmMME2_QM_CQ_CTL_STS_0                                       0x168200

#define mmMME2_QM_CQ_CTL_STS_1                                       0x168204

#define mmMME2_QM_CQ_CTL_STS_2                                       0x168208

#define mmMME2_QM_CQ_CTL_STS_3                                       0x16820C

#define mmMME2_QM_CQ_CTL_STS_4                                       0x168210

#define mmMME2_QM_CQ_IFIFO_CNT_0                                     0x168214

#define mmMME2_QM_CQ_IFIFO_CNT_1                                     0x168218

#define mmMME2_QM_CQ_IFIFO_CNT_2                                     0x16821C

#define mmMME2_QM_CQ_IFIFO_CNT_3                                     0x168220

#define mmMME2_QM_CQ_IFIFO_CNT_4                                     0x168224

#define mmMME2_QM_CP_MSG_BASE0_ADDR_LO_0                             0x168228

#define mmMME2_QM_CP_MSG_BASE0_ADDR_LO_1                             0x16822C

#define mmMME2_QM_CP_MSG_BASE0_ADDR_LO_2                             0x168230

#define mmMME2_QM_CP_MSG_BASE0_ADDR_LO_3                             0x168234

#define mmMME2_QM_CP_MSG_BASE0_ADDR_LO_4                             0x168238

#define mmMME2_QM_CP_MSG_BASE0_ADDR_HI_0                             0x16823C

#define mmMME2_QM_CP_MSG_BASE0_ADDR_HI_1                             0x168240

#define mmMME2_QM_CP_MSG_BASE0_ADDR_HI_2                             0x168244

#define mmMME2_QM_CP_MSG_BASE0_ADDR_HI_3                             0x168248

#define mmMME2_QM_CP_MSG_BASE0_ADDR_HI_4                             0x16824C

#define mmMME2_QM_CP_MSG_BASE1_ADDR_LO_0                             0x168250

#define mmMME2_QM_CP_MSG_BASE1_ADDR_LO_1                             0x168254

#define mmMME2_QM_CP_MSG_BASE1_ADDR_LO_2                             0x168258

#define mmMME2_QM_CP_MSG_BASE1_ADDR_LO_3                             0x16825C

#define mmMME2_QM_CP_MSG_BASE1_ADDR_LO_4                             0x168260

#define mmMME2_QM_CP_MSG_BASE1_ADDR_HI_0                             0x168264

#define mmMME2_QM_CP_MSG_BASE1_ADDR_HI_1                             0x168268

#define mmMME2_QM_CP_MSG_BASE1_ADDR_HI_2                             0x16826C

#define mmMME2_QM_CP_MSG_BASE1_ADDR_HI_3                             0x168270

#define mmMME2_QM_CP_MSG_BASE1_ADDR_HI_4                             0x168274

#define mmMME2_QM_CP_MSG_BASE2_ADDR_LO_0                             0x168278

#define mmMME2_QM_CP_MSG_BASE2_ADDR_LO_1                             0x16827C

#define mmMME2_QM_CP_MSG_BASE2_ADDR_LO_2                             0x168280

#define mmMME2_QM_CP_MSG_BASE2_ADDR_LO_3                             0x168284

#define mmMME2_QM_CP_MSG_BASE2_ADDR_LO_4                             0x168288

#define mmMME2_QM_CP_MSG_BASE2_ADDR_HI_0                             0x16828C

#define mmMME2_QM_CP_MSG_BASE2_ADDR_HI_1                             0x168290

#define mmMME2_QM_CP_MSG_BASE2_ADDR_HI_2                             0x168294

#define mmMME2_QM_CP_MSG_BASE2_ADDR_HI_3                             0x168298

#define mmMME2_QM_CP_MSG_BASE2_ADDR_HI_4                             0x16829C

#define mmMME2_QM_CP_MSG_BASE3_ADDR_LO_0                             0x1682A0

#define mmMME2_QM_CP_MSG_BASE3_ADDR_LO_1                             0x1682A4

#define mmMME2_QM_CP_MSG_BASE3_ADDR_LO_2                             0x1682A8

#define mmMME2_QM_CP_MSG_BASE3_ADDR_LO_3                             0x1682AC

#define mmMME2_QM_CP_MSG_BASE3_ADDR_LO_4                             0x1682B0

#define mmMME2_QM_CP_MSG_BASE3_ADDR_HI_0                             0x1682B4

#define mmMME2_QM_CP_MSG_BASE3_ADDR_HI_1                             0x1682B8

#define mmMME2_QM_CP_MSG_BASE3_ADDR_HI_2                             0x1682BC

#define mmMME2_QM_CP_MSG_BASE3_ADDR_HI_3                             0x1682C0

#define mmMME2_QM_CP_MSG_BASE3_ADDR_HI_4                             0x1682C4

#define mmMME2_QM_CP_LDMA_TSIZE_OFFSET_0                             0x1682C8

#define mmMME2_QM_CP_LDMA_TSIZE_OFFSET_1                             0x1682CC

#define mmMME2_QM_CP_LDMA_TSIZE_OFFSET_2                             0x1682D0

#define mmMME2_QM_CP_LDMA_TSIZE_OFFSET_3                             0x1682D4

#define mmMME2_QM_CP_LDMA_TSIZE_OFFSET_4                             0x1682D8

#define mmMME2_QM_CP_LDMA_SRC_BASE_LO_OFFSET_0                       0x1682E0

#define mmMME2_QM_CP_LDMA_SRC_BASE_LO_OFFSET_1                       0x1682E4

#define mmMME2_QM_CP_LDMA_SRC_BASE_LO_OFFSET_2                       0x1682E8

#define mmMME2_QM_CP_LDMA_SRC_BASE_LO_OFFSET_3                       0x1682EC

#define mmMME2_QM_CP_LDMA_SRC_BASE_LO_OFFSET_4                       0x1682F0

#define mmMME2_QM_CP_LDMA_DST_BASE_LO_OFFSET_0                       0x1682F4

#define mmMME2_QM_CP_LDMA_DST_BASE_LO_OFFSET_1                       0x1682F8

#define mmMME2_QM_CP_LDMA_DST_BASE_LO_OFFSET_2                       0x1682FC

#define mmMME2_QM_CP_LDMA_DST_BASE_LO_OFFSET_3                       0x168300

#define mmMME2_QM_CP_LDMA_DST_BASE_LO_OFFSET_4                       0x168304

#define mmMME2_QM_CP_FENCE0_RDATA_0                                  0x168308

#define mmMME2_QM_CP_FENCE0_RDATA_1                                  0x16830C

#define mmMME2_QM_CP_FENCE0_RDATA_2                                  0x168310

#define mmMME2_QM_CP_FENCE0_RDATA_3                                  0x168314

#define mmMME2_QM_CP_FENCE0_RDATA_4                                  0x168318

#define mmMME2_QM_CP_FENCE1_RDATA_0                                  0x16831C

#define mmMME2_QM_CP_FENCE1_RDATA_1                                  0x168320

#define mmMME2_QM_CP_FENCE1_RDATA_2                                  0x168324

#define mmMME2_QM_CP_FENCE1_RDATA_3                                  0x168328

#define mmMME2_QM_CP_FENCE1_RDATA_4                                  0x16832C

#define mmMME2_QM_CP_FENCE2_RDATA_0                                  0x168330

#define mmMME2_QM_CP_FENCE2_RDATA_1                                  0x168334

#define mmMME2_QM_CP_FENCE2_RDATA_2                                  0x168338

#define mmMME2_QM_CP_FENCE2_RDATA_3                                  0x16833C

#define mmMME2_QM_CP_FENCE2_RDATA_4                                  0x168340

#define mmMME2_QM_CP_FENCE3_RDATA_0                                  0x168344

#define mmMME2_QM_CP_FENCE3_RDATA_1                                  0x168348

#define mmMME2_QM_CP_FENCE3_RDATA_2                                  0x16834C

#define mmMME2_QM_CP_FENCE3_RDATA_3                                  0x168350

#define mmMME2_QM_CP_FENCE3_RDATA_4                                  0x168354

#define mmMME2_QM_CP_FENCE0_CNT_0                                    0x168358

#define mmMME2_QM_CP_FENCE0_CNT_1                                    0x16835C

#define mmMME2_QM_CP_FENCE0_CNT_2                                    0x168360

#define mmMME2_QM_CP_FENCE0_CNT_3                                    0x168364

#define mmMME2_QM_CP_FENCE0_CNT_4                                    0x168368

#define mmMME2_QM_CP_FENCE1_CNT_0                                    0x16836C

#define mmMME2_QM_CP_FENCE1_CNT_1                                    0x168370

#define mmMME2_QM_CP_FENCE1_CNT_2                                    0x168374

#define mmMME2_QM_CP_FENCE1_CNT_3                                    0x168378

#define mmMME2_QM_CP_FENCE1_CNT_4                                    0x16837C

#define mmMME2_QM_CP_FENCE2_CNT_0                                    0x168380

#define mmMME2_QM_CP_FENCE2_CNT_1                                    0x168384

#define mmMME2_QM_CP_FENCE2_CNT_2                                    0x168388

#define mmMME2_QM_CP_FENCE2_CNT_3                                    0x16838C

#define mmMME2_QM_CP_FENCE2_CNT_4                                    0x168390

#define mmMME2_QM_CP_FENCE3_CNT_0                                    0x168394

#define mmMME2_QM_CP_FENCE3_CNT_1                                    0x168398

#define mmMME2_QM_CP_FENCE3_CNT_2                                    0x16839C

#define mmMME2_QM_CP_FENCE3_CNT_3                                    0x1683A0

#define mmMME2_QM_CP_FENCE3_CNT_4                                    0x1683A4

#define mmMME2_QM_CP_STS_0                                           0x1683A8

#define mmMME2_QM_CP_STS_1                                           0x1683AC

#define mmMME2_QM_CP_STS_2                                           0x1683B0

#define mmMME2_QM_CP_STS_3                                           0x1683B4

#define mmMME2_QM_CP_STS_4                                           0x1683B8

#define mmMME2_QM_CP_CURRENT_INST_LO_0                               0x1683BC

#define mmMME2_QM_CP_CURRENT_INST_LO_1                               0x1683C0

#define mmMME2_QM_CP_CURRENT_INST_LO_2                               0x1683C4

#define mmMME2_QM_CP_CURRENT_INST_LO_3                               0x1683C8

#define mmMME2_QM_CP_CURRENT_INST_LO_4                               0x1683CC

#define mmMME2_QM_CP_CURRENT_INST_HI_0                               0x1683D0

#define mmMME2_QM_CP_CURRENT_INST_HI_1                               0x1683D4

#define mmMME2_QM_CP_CURRENT_INST_HI_2                               0x1683D8

#define mmMME2_QM_CP_CURRENT_INST_HI_3                               0x1683DC

#define mmMME2_QM_CP_CURRENT_INST_HI_4                               0x1683E0

#define mmMME2_QM_CP_BARRIER_CFG_0                                   0x1683F4

#define mmMME2_QM_CP_BARRIER_CFG_1                                   0x1683F8

#define mmMME2_QM_CP_BARRIER_CFG_2                                   0x1683FC

#define mmMME2_QM_CP_BARRIER_CFG_3                                   0x168400

#define mmMME2_QM_CP_BARRIER_CFG_4                                   0x168404

#define mmMME2_QM_CP_DBG_0_0                                         0x168408

#define mmMME2_QM_CP_DBG_0_1                                         0x16840C

#define mmMME2_QM_CP_DBG_0_2                                         0x168410

#define mmMME2_QM_CP_DBG_0_3                                         0x168414

#define mmMME2_QM_CP_DBG_0_4                                         0x168418

#define mmMME2_QM_CP_ARUSER_31_11_0                                  0x16841C

#define mmMME2_QM_CP_ARUSER_31_11_1                                  0x168420

#define mmMME2_QM_CP_ARUSER_31_11_2                                  0x168424

#define mmMME2_QM_CP_ARUSER_31_11_3                                  0x168428

#define mmMME2_QM_CP_ARUSER_31_11_4                                  0x16842C

#define mmMME2_QM_CP_AWUSER_31_11_0                                  0x168430

#define mmMME2_QM_CP_AWUSER_31_11_1                                  0x168434

#define mmMME2_QM_CP_AWUSER_31_11_2                                  0x168438

#define mmMME2_QM_CP_AWUSER_31_11_3                                  0x16843C

#define mmMME2_QM_CP_AWUSER_31_11_4                                  0x168440

#define mmMME2_QM_ARB_CFG_0                                          0x168A00

#define mmMME2_QM_ARB_CHOISE_Q_PUSH                                  0x168A04

#define mmMME2_QM_ARB_WRR_WEIGHT_0                                   0x168A08

#define mmMME2_QM_ARB_WRR_WEIGHT_1                                   0x168A0C

#define mmMME2_QM_ARB_WRR_WEIGHT_2                                   0x168A10

#define mmMME2_QM_ARB_WRR_WEIGHT_3                                   0x168A14

#define mmMME2_QM_ARB_CFG_1                                          0x168A18

#define mmMME2_QM_ARB_MST_AVAIL_CRED_0                               0x168A20

#define mmMME2_QM_ARB_MST_AVAIL_CRED_1                               0x168A24

#define mmMME2_QM_ARB_MST_AVAIL_CRED_2                               0x168A28

#define mmMME2_QM_ARB_MST_AVAIL_CRED_3                               0x168A2C

#define mmMME2_QM_ARB_MST_AVAIL_CRED_4                               0x168A30

#define mmMME2_QM_ARB_MST_AVAIL_CRED_5                               0x168A34

#define mmMME2_QM_ARB_MST_AVAIL_CRED_6                               0x168A38

#define mmMME2_QM_ARB_MST_AVAIL_CRED_7                               0x168A3C

#define mmMME2_QM_ARB_MST_AVAIL_CRED_8                               0x168A40

#define mmMME2_QM_ARB_MST_AVAIL_CRED_9                               0x168A44

#define mmMME2_QM_ARB_MST_AVAIL_CRED_10                              0x168A48

#define mmMME2_QM_ARB_MST_AVAIL_CRED_11                              0x168A4C

#define mmMME2_QM_ARB_MST_AVAIL_CRED_12                              0x168A50

#define mmMME2_QM_ARB_MST_AVAIL_CRED_13                              0x168A54

#define mmMME2_QM_ARB_MST_AVAIL_CRED_14                              0x168A58

#define mmMME2_QM_ARB_MST_AVAIL_CRED_15                              0x168A5C

#define mmMME2_QM_ARB_MST_AVAIL_CRED_16                              0x168A60

#define mmMME2_QM_ARB_MST_AVAIL_CRED_17                              0x168A64

#define mmMME2_QM_ARB_MST_AVAIL_CRED_18                              0x168A68

#define mmMME2_QM_ARB_MST_AVAIL_CRED_19                              0x168A6C

#define mmMME2_QM_ARB_MST_AVAIL_CRED_20                              0x168A70

#define mmMME2_QM_ARB_MST_AVAIL_CRED_21                              0x168A74

#define mmMME2_QM_ARB_MST_AVAIL_CRED_22                              0x168A78

#define mmMME2_QM_ARB_MST_AVAIL_CRED_23                              0x168A7C

#define mmMME2_QM_ARB_MST_AVAIL_CRED_24                              0x168A80

#define mmMME2_QM_ARB_MST_AVAIL_CRED_25                              0x168A84

#define mmMME2_QM_ARB_MST_AVAIL_CRED_26                              0x168A88

#define mmMME2_QM_ARB_MST_AVAIL_CRED_27                              0x168A8C

#define mmMME2_QM_ARB_MST_AVAIL_CRED_28                              0x168A90

#define mmMME2_QM_ARB_MST_AVAIL_CRED_29                              0x168A94

#define mmMME2_QM_ARB_MST_AVAIL_CRED_30                              0x168A98

#define mmMME2_QM_ARB_MST_AVAIL_CRED_31                              0x168A9C

#define mmMME2_QM_ARB_MST_CRED_INC                                   0x168AA0

#define mmMME2_QM_ARB_MST_CHOISE_PUSH_OFST_0                         0x168AA4

#define mmMME2_QM_ARB_MST_CHOISE_PUSH_OFST_1                         0x168AA8

#define mmMME2_QM_ARB_MST_CHOISE_PUSH_OFST_2                         0x168AAC

#define mmMME2_QM_ARB_MST_CHOISE_PUSH_OFST_3                         0x168AB0

#define mmMME2_QM_ARB_MST_CHOISE_PUSH_OFST_4                         0x168AB4

#define mmMME2_QM_ARB_MST_CHOISE_PUSH_OFST_5                         0x168AB8

#define mmMME2_QM_ARB_MST_CHOISE_PUSH_OFST_6                         0x168ABC

#define mmMME2_QM_ARB_MST_CHOISE_PUSH_OFST_7                         0x168AC0

#define mmMME2_QM_ARB_MST_CHOISE_PUSH_OFST_8                         0x168AC4

#define mmMME2_QM_ARB_MST_CHOISE_PUSH_OFST_9                         0x168AC8

#define mmMME2_QM_ARB_MST_CHOISE_PUSH_OFST_10                        0x168ACC

#define mmMME2_QM_ARB_MST_CHOISE_PUSH_OFST_11                        0x168AD0

#define mmMME2_QM_ARB_MST_CHOISE_PUSH_OFST_12                        0x168AD4

#define mmMME2_QM_ARB_MST_CHOISE_PUSH_OFST_13                        0x168AD8

#define mmMME2_QM_ARB_MST_CHOISE_PUSH_OFST_14                        0x168ADC

#define mmMME2_QM_ARB_MST_CHOISE_PUSH_OFST_15                        0x168AE0

#define mmMME2_QM_ARB_MST_CHOISE_PUSH_OFST_16                        0x168AE4

#define mmMME2_QM_ARB_MST_CHOISE_PUSH_OFST_17                        0x168AE8

#define mmMME2_QM_ARB_MST_CHOISE_PUSH_OFST_18                        0x168AEC

#define mmMME2_QM_ARB_MST_CHOISE_PUSH_OFST_19                        0x168AF0

#define mmMME2_QM_ARB_MST_CHOISE_PUSH_OFST_20                        0x168AF4

#define mmMME2_QM_ARB_MST_CHOISE_PUSH_OFST_21                        0x168AF8

#define mmMME2_QM_ARB_MST_CHOISE_PUSH_OFST_22                        0x168AFC

#define mmMME2_QM_ARB_MST_CHOISE_PUSH_OFST_23                        0x168B00

#define mmMME2_QM_ARB_MST_CHOISE_PUSH_OFST_24                        0x168B04

#define mmMME2_QM_ARB_MST_CHOISE_PUSH_OFST_25                        0x168B08

#define mmMME2_QM_ARB_MST_CHOISE_PUSH_OFST_26                        0x168B0C

#define mmMME2_QM_ARB_MST_CHOISE_PUSH_OFST_27                        0x168B10

#define mmMME2_QM_ARB_MST_CHOISE_PUSH_OFST_28                        0x168B14

#define mmMME2_QM_ARB_MST_CHOISE_PUSH_OFST_29                        0x168B18

#define mmMME2_QM_ARB_MST_CHOISE_PUSH_OFST_30                        0x168B1C

#define mmMME2_QM_ARB_MST_CHOISE_PUSH_OFST_31                        0x168B20

#define mmMME2_QM_ARB_SLV_MASTER_INC_CRED_OFST                       0x168B28

#define mmMME2_QM_ARB_MST_SLAVE_EN                                   0x168B2C

#define mmMME2_QM_ARB_MST_QUIET_PER                                  0x168B34

#define mmMME2_QM_ARB_SLV_CHOISE_WDT                                 0x168B38

#define mmMME2_QM_ARB_SLV_ID                                         0x168B3C

#define mmMME2_QM_ARB_MSG_MAX_INFLIGHT                               0x168B44

#define mmMME2_QM_ARB_MSG_AWUSER_31_11                               0x168B48

#define mmMME2_QM_ARB_MSG_AWUSER_SEC_PROP                            0x168B4C

#define mmMME2_QM_ARB_MSG_AWUSER_NON_SEC_PROP                        0x168B50

#define mmMME2_QM_ARB_BASE_LO                                        0x168B54

#define mmMME2_QM_ARB_BASE_HI                                        0x168B58

#define mmMME2_QM_ARB_STATE_STS                                      0x168B80

#define mmMME2_QM_ARB_CHOISE_FULLNESS_STS                            0x168B84

#define mmMME2_QM_ARB_MSG_STS                                        0x168B88

#define mmMME2_QM_ARB_SLV_CHOISE_Q_HEAD                              0x168B8C

#define mmMME2_QM_ARB_ERR_CAUSE                                      0x168B9C

#define mmMME2_QM_ARB_ERR_MSG_EN                                     0x168BA0

#define mmMME2_QM_ARB_ERR_STS_DRP                                    0x168BA8

#define mmMME2_QM_ARB_MST_CRED_STS_0                                 0x168BB0

#define mmMME2_QM_ARB_MST_CRED_STS_1                                 0x168BB4

#define mmMME2_QM_ARB_MST_CRED_STS_2                                 0x168BB8

#define mmMME2_QM_ARB_MST_CRED_STS_3                                 0x168BBC

#define mmMME2_QM_ARB_MST_CRED_STS_4                                 0x168BC0

#define mmMME2_QM_ARB_MST_CRED_STS_5                                 0x168BC4

#define mmMME2_QM_ARB_MST_CRED_STS_6                                 0x168BC8

#define mmMME2_QM_ARB_MST_CRED_STS_7                                 0x168BCC

#define mmMME2_QM_ARB_MST_CRED_STS_8                                 0x168BD0

#define mmMME2_QM_ARB_MST_CRED_STS_9                                 0x168BD4

#define mmMME2_QM_ARB_MST_CRED_STS_10                                0x168BD8

#define mmMME2_QM_ARB_MST_CRED_STS_11                                0x168BDC

#define mmMME2_QM_ARB_MST_CRED_STS_12                                0x168BE0

#define mmMME2_QM_ARB_MST_CRED_STS_13                                0x168BE4

#define mmMME2_QM_ARB_MST_CRED_STS_14                                0x168BE8

#define mmMME2_QM_ARB_MST_CRED_STS_15                                0x168BEC

#define mmMME2_QM_ARB_MST_CRED_STS_16                                0x168BF0

#define mmMME2_QM_ARB_MST_CRED_STS_17                                0x168BF4

#define mmMME2_QM_ARB_MST_CRED_STS_18                                0x168BF8

#define mmMME2_QM_ARB_MST_CRED_STS_19                                0x168BFC

#define mmMME2_QM_ARB_MST_CRED_STS_20                                0x168C00

#define mmMME2_QM_ARB_MST_CRED_STS_21                                0x168C04

#define mmMME2_QM_ARB_MST_CRED_STS_22                                0x168C08

#define mmMME2_QM_ARB_MST_CRED_STS_23                                0x168C0C

#define mmMME2_QM_ARB_MST_CRED_STS_24                                0x168C10

#define mmMME2_QM_ARB_MST_CRED_STS_25                                0x168C14

#define mmMME2_QM_ARB_MST_CRED_STS_26                                0x168C18

#define mmMME2_QM_ARB_MST_CRED_STS_27                                0x168C1C

#define mmMME2_QM_ARB_MST_CRED_STS_28                                0x168C20

#define mmMME2_QM_ARB_MST_CRED_STS_29                                0x168C24

#define mmMME2_QM_ARB_MST_CRED_STS_30                                0x168C28

#define mmMME2_QM_ARB_MST_CRED_STS_31                                0x168C2C

#define mmMME2_QM_CGM_CFG                                            0x168C70

#define mmMME2_QM_CGM_STS                                            0x168C74

#define mmMME2_QM_CGM_CFG1                                           0x168C78

#define mmMME2_QM_LOCAL_RANGE_BASE                                   0x168C80

#define mmMME2_QM_LOCAL_RANGE_SIZE                                   0x168C84

#define mmMME2_QM_CSMR_STRICT_PRIO_CFG                               0x168C90

#define mmMME2_QM_HBW_RD_RATE_LIM_CFG_1                              0x168C94

#define mmMME2_QM_LBW_WR_RATE_LIM_CFG_0                              0x168C98

#define mmMME2_QM_LBW_WR_RATE_LIM_CFG_1                              0x168C9C

#define mmMME2_QM_HBW_RD_RATE_LIM_CFG_0                              0x168CA0

#define mmMME2_QM_GLBL_AXCACHE                                       0x168CA4

#define mmMME2_QM_IND_GW_APB_CFG                                     0x168CB0

#define mmMME2_QM_IND_GW_APB_WDATA                                   0x168CB4

#define mmMME2_QM_IND_GW_APB_RDATA                                   0x168CB8

#define mmMME2_QM_IND_GW_APB_STATUS                                  0x168CBC

#define mmMME2_QM_GLBL_ERR_ADDR_LO                                   0x168CD0

#define mmMME2_QM_GLBL_ERR_ADDR_HI                                   0x168CD4

#define mmMME2_QM_GLBL_ERR_WDATA                                     0x168CD8

#define mmMME2_QM_GLBL_MEM_INIT_BUSY                                 0x168D00

#endif /* ASIC_REG_MME2_QM_REGS_H_ */
