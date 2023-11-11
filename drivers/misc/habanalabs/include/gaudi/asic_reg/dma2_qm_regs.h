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

#ifndef ASIC_REG_DMA2_QM_REGS_H_
#define ASIC_REG_DMA2_QM_REGS_H_

/*
 *****************************************
 *   DMA2_QM (Prototype: QMAN)
 *****************************************
 */

#define mmDMA2_QM_GLBL_CFG0                                          0x548000

#define mmDMA2_QM_GLBL_CFG1                                          0x548004

#define mmDMA2_QM_GLBL_PROT                                          0x548008

#define mmDMA2_QM_GLBL_ERR_CFG                                       0x54800C

#define mmDMA2_QM_GLBL_SECURE_PROPS_0                                0x548010

#define mmDMA2_QM_GLBL_SECURE_PROPS_1                                0x548014

#define mmDMA2_QM_GLBL_SECURE_PROPS_2                                0x548018

#define mmDMA2_QM_GLBL_SECURE_PROPS_3                                0x54801C

#define mmDMA2_QM_GLBL_SECURE_PROPS_4                                0x548020

#define mmDMA2_QM_GLBL_NON_SECURE_PROPS_0                            0x548024

#define mmDMA2_QM_GLBL_NON_SECURE_PROPS_1                            0x548028

#define mmDMA2_QM_GLBL_NON_SECURE_PROPS_2                            0x54802C

#define mmDMA2_QM_GLBL_NON_SECURE_PROPS_3                            0x548030

#define mmDMA2_QM_GLBL_NON_SECURE_PROPS_4                            0x548034

#define mmDMA2_QM_GLBL_STS0                                          0x548038

#define mmDMA2_QM_GLBL_STS1_0                                        0x548040

#define mmDMA2_QM_GLBL_STS1_1                                        0x548044

#define mmDMA2_QM_GLBL_STS1_2                                        0x548048

#define mmDMA2_QM_GLBL_STS1_3                                        0x54804C

#define mmDMA2_QM_GLBL_STS1_4                                        0x548050

#define mmDMA2_QM_GLBL_MSG_EN_0                                      0x548054

#define mmDMA2_QM_GLBL_MSG_EN_1                                      0x548058

#define mmDMA2_QM_GLBL_MSG_EN_2                                      0x54805C

#define mmDMA2_QM_GLBL_MSG_EN_3                                      0x548060

#define mmDMA2_QM_GLBL_MSG_EN_4                                      0x548068

#define mmDMA2_QM_PQ_BASE_LO_0                                       0x548070

#define mmDMA2_QM_PQ_BASE_LO_1                                       0x548074

#define mmDMA2_QM_PQ_BASE_LO_2                                       0x548078

#define mmDMA2_QM_PQ_BASE_LO_3                                       0x54807C

#define mmDMA2_QM_PQ_BASE_HI_0                                       0x548080

#define mmDMA2_QM_PQ_BASE_HI_1                                       0x548084

#define mmDMA2_QM_PQ_BASE_HI_2                                       0x548088

#define mmDMA2_QM_PQ_BASE_HI_3                                       0x54808C

#define mmDMA2_QM_PQ_SIZE_0                                          0x548090

#define mmDMA2_QM_PQ_SIZE_1                                          0x548094

#define mmDMA2_QM_PQ_SIZE_2                                          0x548098

#define mmDMA2_QM_PQ_SIZE_3                                          0x54809C

#define mmDMA2_QM_PQ_PI_0                                            0x5480A0

#define mmDMA2_QM_PQ_PI_1                                            0x5480A4

#define mmDMA2_QM_PQ_PI_2                                            0x5480A8

#define mmDMA2_QM_PQ_PI_3                                            0x5480AC

#define mmDMA2_QM_PQ_CI_0                                            0x5480B0

#define mmDMA2_QM_PQ_CI_1                                            0x5480B4

#define mmDMA2_QM_PQ_CI_2                                            0x5480B8

#define mmDMA2_QM_PQ_CI_3                                            0x5480BC

#define mmDMA2_QM_PQ_CFG0_0                                          0x5480C0

#define mmDMA2_QM_PQ_CFG0_1                                          0x5480C4

#define mmDMA2_QM_PQ_CFG0_2                                          0x5480C8

#define mmDMA2_QM_PQ_CFG0_3                                          0x5480CC

#define mmDMA2_QM_PQ_CFG1_0                                          0x5480D0

#define mmDMA2_QM_PQ_CFG1_1                                          0x5480D4

#define mmDMA2_QM_PQ_CFG1_2                                          0x5480D8

#define mmDMA2_QM_PQ_CFG1_3                                          0x5480DC

#define mmDMA2_QM_PQ_ARUSER_31_11_0                                  0x5480E0

#define mmDMA2_QM_PQ_ARUSER_31_11_1                                  0x5480E4

#define mmDMA2_QM_PQ_ARUSER_31_11_2                                  0x5480E8

#define mmDMA2_QM_PQ_ARUSER_31_11_3                                  0x5480EC

#define mmDMA2_QM_PQ_STS0_0                                          0x5480F0

#define mmDMA2_QM_PQ_STS0_1                                          0x5480F4

#define mmDMA2_QM_PQ_STS0_2                                          0x5480F8

#define mmDMA2_QM_PQ_STS0_3                                          0x5480FC

#define mmDMA2_QM_PQ_STS1_0                                          0x548100

#define mmDMA2_QM_PQ_STS1_1                                          0x548104

#define mmDMA2_QM_PQ_STS1_2                                          0x548108

#define mmDMA2_QM_PQ_STS1_3                                          0x54810C

#define mmDMA2_QM_CQ_CFG0_0                                          0x548110

#define mmDMA2_QM_CQ_CFG0_1                                          0x548114

#define mmDMA2_QM_CQ_CFG0_2                                          0x548118

#define mmDMA2_QM_CQ_CFG0_3                                          0x54811C

#define mmDMA2_QM_CQ_CFG0_4                                          0x548120

#define mmDMA2_QM_CQ_CFG1_0                                          0x548124

#define mmDMA2_QM_CQ_CFG1_1                                          0x548128

#define mmDMA2_QM_CQ_CFG1_2                                          0x54812C

#define mmDMA2_QM_CQ_CFG1_3                                          0x548130

#define mmDMA2_QM_CQ_CFG1_4                                          0x548134

#define mmDMA2_QM_CQ_ARUSER_31_11_0                                  0x548138

#define mmDMA2_QM_CQ_ARUSER_31_11_1                                  0x54813C

#define mmDMA2_QM_CQ_ARUSER_31_11_2                                  0x548140

#define mmDMA2_QM_CQ_ARUSER_31_11_3                                  0x548144

#define mmDMA2_QM_CQ_ARUSER_31_11_4                                  0x548148

#define mmDMA2_QM_CQ_STS0_0                                          0x54814C

#define mmDMA2_QM_CQ_STS0_1                                          0x548150

#define mmDMA2_QM_CQ_STS0_2                                          0x548154

#define mmDMA2_QM_CQ_STS0_3                                          0x548158

#define mmDMA2_QM_CQ_STS0_4                                          0x54815C

#define mmDMA2_QM_CQ_STS1_0                                          0x548160

#define mmDMA2_QM_CQ_STS1_1                                          0x548164

#define mmDMA2_QM_CQ_STS1_2                                          0x548168

#define mmDMA2_QM_CQ_STS1_3                                          0x54816C

#define mmDMA2_QM_CQ_STS1_4                                          0x548170

#define mmDMA2_QM_CQ_PTR_LO_0                                        0x548174

#define mmDMA2_QM_CQ_PTR_HI_0                                        0x548178

#define mmDMA2_QM_CQ_TSIZE_0                                         0x54817C

#define mmDMA2_QM_CQ_CTL_0                                           0x548180

#define mmDMA2_QM_CQ_PTR_LO_1                                        0x548184

#define mmDMA2_QM_CQ_PTR_HI_1                                        0x548188

#define mmDMA2_QM_CQ_TSIZE_1                                         0x54818C

#define mmDMA2_QM_CQ_CTL_1                                           0x548190

#define mmDMA2_QM_CQ_PTR_LO_2                                        0x548194

#define mmDMA2_QM_CQ_PTR_HI_2                                        0x548198

#define mmDMA2_QM_CQ_TSIZE_2                                         0x54819C

#define mmDMA2_QM_CQ_CTL_2                                           0x5481A0

#define mmDMA2_QM_CQ_PTR_LO_3                                        0x5481A4

#define mmDMA2_QM_CQ_PTR_HI_3                                        0x5481A8

#define mmDMA2_QM_CQ_TSIZE_3                                         0x5481AC

#define mmDMA2_QM_CQ_CTL_3                                           0x5481B0

#define mmDMA2_QM_CQ_PTR_LO_4                                        0x5481B4

#define mmDMA2_QM_CQ_PTR_HI_4                                        0x5481B8

#define mmDMA2_QM_CQ_TSIZE_4                                         0x5481BC

#define mmDMA2_QM_CQ_CTL_4                                           0x5481C0

#define mmDMA2_QM_CQ_PTR_LO_STS_0                                    0x5481C4

#define mmDMA2_QM_CQ_PTR_LO_STS_1                                    0x5481C8

#define mmDMA2_QM_CQ_PTR_LO_STS_2                                    0x5481CC

#define mmDMA2_QM_CQ_PTR_LO_STS_3                                    0x5481D0

#define mmDMA2_QM_CQ_PTR_LO_STS_4                                    0x5481D4

#define mmDMA2_QM_CQ_PTR_HI_STS_0                                    0x5481D8

#define mmDMA2_QM_CQ_PTR_HI_STS_1                                    0x5481DC

#define mmDMA2_QM_CQ_PTR_HI_STS_2                                    0x5481E0

#define mmDMA2_QM_CQ_PTR_HI_STS_3                                    0x5481E4

#define mmDMA2_QM_CQ_PTR_HI_STS_4                                    0x5481E8

#define mmDMA2_QM_CQ_TSIZE_STS_0                                     0x5481EC

#define mmDMA2_QM_CQ_TSIZE_STS_1                                     0x5481F0

#define mmDMA2_QM_CQ_TSIZE_STS_2                                     0x5481F4

#define mmDMA2_QM_CQ_TSIZE_STS_3                                     0x5481F8

#define mmDMA2_QM_CQ_TSIZE_STS_4                                     0x5481FC

#define mmDMA2_QM_CQ_CTL_STS_0                                       0x548200

#define mmDMA2_QM_CQ_CTL_STS_1                                       0x548204

#define mmDMA2_QM_CQ_CTL_STS_2                                       0x548208

#define mmDMA2_QM_CQ_CTL_STS_3                                       0x54820C

#define mmDMA2_QM_CQ_CTL_STS_4                                       0x548210

#define mmDMA2_QM_CQ_IFIFO_CNT_0                                     0x548214

#define mmDMA2_QM_CQ_IFIFO_CNT_1                                     0x548218

#define mmDMA2_QM_CQ_IFIFO_CNT_2                                     0x54821C

#define mmDMA2_QM_CQ_IFIFO_CNT_3                                     0x548220

#define mmDMA2_QM_CQ_IFIFO_CNT_4                                     0x548224

#define mmDMA2_QM_CP_MSG_BASE0_ADDR_LO_0                             0x548228

#define mmDMA2_QM_CP_MSG_BASE0_ADDR_LO_1                             0x54822C

#define mmDMA2_QM_CP_MSG_BASE0_ADDR_LO_2                             0x548230

#define mmDMA2_QM_CP_MSG_BASE0_ADDR_LO_3                             0x548234

#define mmDMA2_QM_CP_MSG_BASE0_ADDR_LO_4                             0x548238

#define mmDMA2_QM_CP_MSG_BASE0_ADDR_HI_0                             0x54823C

#define mmDMA2_QM_CP_MSG_BASE0_ADDR_HI_1                             0x548240

#define mmDMA2_QM_CP_MSG_BASE0_ADDR_HI_2                             0x548244

#define mmDMA2_QM_CP_MSG_BASE0_ADDR_HI_3                             0x548248

#define mmDMA2_QM_CP_MSG_BASE0_ADDR_HI_4                             0x54824C

#define mmDMA2_QM_CP_MSG_BASE1_ADDR_LO_0                             0x548250

#define mmDMA2_QM_CP_MSG_BASE1_ADDR_LO_1                             0x548254

#define mmDMA2_QM_CP_MSG_BASE1_ADDR_LO_2                             0x548258

#define mmDMA2_QM_CP_MSG_BASE1_ADDR_LO_3                             0x54825C

#define mmDMA2_QM_CP_MSG_BASE1_ADDR_LO_4                             0x548260

#define mmDMA2_QM_CP_MSG_BASE1_ADDR_HI_0                             0x548264

#define mmDMA2_QM_CP_MSG_BASE1_ADDR_HI_1                             0x548268

#define mmDMA2_QM_CP_MSG_BASE1_ADDR_HI_2                             0x54826C

#define mmDMA2_QM_CP_MSG_BASE1_ADDR_HI_3                             0x548270

#define mmDMA2_QM_CP_MSG_BASE1_ADDR_HI_4                             0x548274

#define mmDMA2_QM_CP_MSG_BASE2_ADDR_LO_0                             0x548278

#define mmDMA2_QM_CP_MSG_BASE2_ADDR_LO_1                             0x54827C

#define mmDMA2_QM_CP_MSG_BASE2_ADDR_LO_2                             0x548280

#define mmDMA2_QM_CP_MSG_BASE2_ADDR_LO_3                             0x548284

#define mmDMA2_QM_CP_MSG_BASE2_ADDR_LO_4                             0x548288

#define mmDMA2_QM_CP_MSG_BASE2_ADDR_HI_0                             0x54828C

#define mmDMA2_QM_CP_MSG_BASE2_ADDR_HI_1                             0x548290

#define mmDMA2_QM_CP_MSG_BASE2_ADDR_HI_2                             0x548294

#define mmDMA2_QM_CP_MSG_BASE2_ADDR_HI_3                             0x548298

#define mmDMA2_QM_CP_MSG_BASE2_ADDR_HI_4                             0x54829C

#define mmDMA2_QM_CP_MSG_BASE3_ADDR_LO_0                             0x5482A0

#define mmDMA2_QM_CP_MSG_BASE3_ADDR_LO_1                             0x5482A4

#define mmDMA2_QM_CP_MSG_BASE3_ADDR_LO_2                             0x5482A8

#define mmDMA2_QM_CP_MSG_BASE3_ADDR_LO_3                             0x5482AC

#define mmDMA2_QM_CP_MSG_BASE3_ADDR_LO_4                             0x5482B0

#define mmDMA2_QM_CP_MSG_BASE3_ADDR_HI_0                             0x5482B4

#define mmDMA2_QM_CP_MSG_BASE3_ADDR_HI_1                             0x5482B8

#define mmDMA2_QM_CP_MSG_BASE3_ADDR_HI_2                             0x5482BC

#define mmDMA2_QM_CP_MSG_BASE3_ADDR_HI_3                             0x5482C0

#define mmDMA2_QM_CP_MSG_BASE3_ADDR_HI_4                             0x5482C4

#define mmDMA2_QM_CP_LDMA_TSIZE_OFFSET_0                             0x5482C8

#define mmDMA2_QM_CP_LDMA_TSIZE_OFFSET_1                             0x5482CC

#define mmDMA2_QM_CP_LDMA_TSIZE_OFFSET_2                             0x5482D0

#define mmDMA2_QM_CP_LDMA_TSIZE_OFFSET_3                             0x5482D4

#define mmDMA2_QM_CP_LDMA_TSIZE_OFFSET_4                             0x5482D8

#define mmDMA2_QM_CP_LDMA_SRC_BASE_LO_OFFSET_0                       0x5482E0

#define mmDMA2_QM_CP_LDMA_SRC_BASE_LO_OFFSET_1                       0x5482E4

#define mmDMA2_QM_CP_LDMA_SRC_BASE_LO_OFFSET_2                       0x5482E8

#define mmDMA2_QM_CP_LDMA_SRC_BASE_LO_OFFSET_3                       0x5482EC

#define mmDMA2_QM_CP_LDMA_SRC_BASE_LO_OFFSET_4                       0x5482F0

#define mmDMA2_QM_CP_LDMA_DST_BASE_LO_OFFSET_0                       0x5482F4

#define mmDMA2_QM_CP_LDMA_DST_BASE_LO_OFFSET_1                       0x5482F8

#define mmDMA2_QM_CP_LDMA_DST_BASE_LO_OFFSET_2                       0x5482FC

#define mmDMA2_QM_CP_LDMA_DST_BASE_LO_OFFSET_3                       0x548300

#define mmDMA2_QM_CP_LDMA_DST_BASE_LO_OFFSET_4                       0x548304

#define mmDMA2_QM_CP_FENCE0_RDATA_0                                  0x548308

#define mmDMA2_QM_CP_FENCE0_RDATA_1                                  0x54830C

#define mmDMA2_QM_CP_FENCE0_RDATA_2                                  0x548310

#define mmDMA2_QM_CP_FENCE0_RDATA_3                                  0x548314

#define mmDMA2_QM_CP_FENCE0_RDATA_4                                  0x548318

#define mmDMA2_QM_CP_FENCE1_RDATA_0                                  0x54831C

#define mmDMA2_QM_CP_FENCE1_RDATA_1                                  0x548320

#define mmDMA2_QM_CP_FENCE1_RDATA_2                                  0x548324

#define mmDMA2_QM_CP_FENCE1_RDATA_3                                  0x548328

#define mmDMA2_QM_CP_FENCE1_RDATA_4                                  0x54832C

#define mmDMA2_QM_CP_FENCE2_RDATA_0                                  0x548330

#define mmDMA2_QM_CP_FENCE2_RDATA_1                                  0x548334

#define mmDMA2_QM_CP_FENCE2_RDATA_2                                  0x548338

#define mmDMA2_QM_CP_FENCE2_RDATA_3                                  0x54833C

#define mmDMA2_QM_CP_FENCE2_RDATA_4                                  0x548340

#define mmDMA2_QM_CP_FENCE3_RDATA_0                                  0x548344

#define mmDMA2_QM_CP_FENCE3_RDATA_1                                  0x548348

#define mmDMA2_QM_CP_FENCE3_RDATA_2                                  0x54834C

#define mmDMA2_QM_CP_FENCE3_RDATA_3                                  0x548350

#define mmDMA2_QM_CP_FENCE3_RDATA_4                                  0x548354

#define mmDMA2_QM_CP_FENCE0_CNT_0                                    0x548358

#define mmDMA2_QM_CP_FENCE0_CNT_1                                    0x54835C

#define mmDMA2_QM_CP_FENCE0_CNT_2                                    0x548360

#define mmDMA2_QM_CP_FENCE0_CNT_3                                    0x548364

#define mmDMA2_QM_CP_FENCE0_CNT_4                                    0x548368

#define mmDMA2_QM_CP_FENCE1_CNT_0                                    0x54836C

#define mmDMA2_QM_CP_FENCE1_CNT_1                                    0x548370

#define mmDMA2_QM_CP_FENCE1_CNT_2                                    0x548374

#define mmDMA2_QM_CP_FENCE1_CNT_3                                    0x548378

#define mmDMA2_QM_CP_FENCE1_CNT_4                                    0x54837C

#define mmDMA2_QM_CP_FENCE2_CNT_0                                    0x548380

#define mmDMA2_QM_CP_FENCE2_CNT_1                                    0x548384

#define mmDMA2_QM_CP_FENCE2_CNT_2                                    0x548388

#define mmDMA2_QM_CP_FENCE2_CNT_3                                    0x54838C

#define mmDMA2_QM_CP_FENCE2_CNT_4                                    0x548390

#define mmDMA2_QM_CP_FENCE3_CNT_0                                    0x548394

#define mmDMA2_QM_CP_FENCE3_CNT_1                                    0x548398

#define mmDMA2_QM_CP_FENCE3_CNT_2                                    0x54839C

#define mmDMA2_QM_CP_FENCE3_CNT_3                                    0x5483A0

#define mmDMA2_QM_CP_FENCE3_CNT_4                                    0x5483A4

#define mmDMA2_QM_CP_STS_0                                           0x5483A8

#define mmDMA2_QM_CP_STS_1                                           0x5483AC

#define mmDMA2_QM_CP_STS_2                                           0x5483B0

#define mmDMA2_QM_CP_STS_3                                           0x5483B4

#define mmDMA2_QM_CP_STS_4                                           0x5483B8

#define mmDMA2_QM_CP_CURRENT_INST_LO_0                               0x5483BC

#define mmDMA2_QM_CP_CURRENT_INST_LO_1                               0x5483C0

#define mmDMA2_QM_CP_CURRENT_INST_LO_2                               0x5483C4

#define mmDMA2_QM_CP_CURRENT_INST_LO_3                               0x5483C8

#define mmDMA2_QM_CP_CURRENT_INST_LO_4                               0x5483CC

#define mmDMA2_QM_CP_CURRENT_INST_HI_0                               0x5483D0

#define mmDMA2_QM_CP_CURRENT_INST_HI_1                               0x5483D4

#define mmDMA2_QM_CP_CURRENT_INST_HI_2                               0x5483D8

#define mmDMA2_QM_CP_CURRENT_INST_HI_3                               0x5483DC

#define mmDMA2_QM_CP_CURRENT_INST_HI_4                               0x5483E0

#define mmDMA2_QM_CP_BARRIER_CFG_0                                   0x5483F4

#define mmDMA2_QM_CP_BARRIER_CFG_1                                   0x5483F8

#define mmDMA2_QM_CP_BARRIER_CFG_2                                   0x5483FC

#define mmDMA2_QM_CP_BARRIER_CFG_3                                   0x548400

#define mmDMA2_QM_CP_BARRIER_CFG_4                                   0x548404

#define mmDMA2_QM_CP_DBG_0_0                                         0x548408

#define mmDMA2_QM_CP_DBG_0_1                                         0x54840C

#define mmDMA2_QM_CP_DBG_0_2                                         0x548410

#define mmDMA2_QM_CP_DBG_0_3                                         0x548414

#define mmDMA2_QM_CP_DBG_0_4                                         0x548418

#define mmDMA2_QM_CP_ARUSER_31_11_0                                  0x54841C

#define mmDMA2_QM_CP_ARUSER_31_11_1                                  0x548420

#define mmDMA2_QM_CP_ARUSER_31_11_2                                  0x548424

#define mmDMA2_QM_CP_ARUSER_31_11_3                                  0x548428

#define mmDMA2_QM_CP_ARUSER_31_11_4                                  0x54842C

#define mmDMA2_QM_CP_AWUSER_31_11_0                                  0x548430

#define mmDMA2_QM_CP_AWUSER_31_11_1                                  0x548434

#define mmDMA2_QM_CP_AWUSER_31_11_2                                  0x548438

#define mmDMA2_QM_CP_AWUSER_31_11_3                                  0x54843C

#define mmDMA2_QM_CP_AWUSER_31_11_4                                  0x548440

#define mmDMA2_QM_ARB_CFG_0                                          0x548A00

#define mmDMA2_QM_ARB_CHOISE_Q_PUSH                                  0x548A04

#define mmDMA2_QM_ARB_WRR_WEIGHT_0                                   0x548A08

#define mmDMA2_QM_ARB_WRR_WEIGHT_1                                   0x548A0C

#define mmDMA2_QM_ARB_WRR_WEIGHT_2                                   0x548A10

#define mmDMA2_QM_ARB_WRR_WEIGHT_3                                   0x548A14

#define mmDMA2_QM_ARB_CFG_1                                          0x548A18

#define mmDMA2_QM_ARB_MST_AVAIL_CRED_0                               0x548A20

#define mmDMA2_QM_ARB_MST_AVAIL_CRED_1                               0x548A24

#define mmDMA2_QM_ARB_MST_AVAIL_CRED_2                               0x548A28

#define mmDMA2_QM_ARB_MST_AVAIL_CRED_3                               0x548A2C

#define mmDMA2_QM_ARB_MST_AVAIL_CRED_4                               0x548A30

#define mmDMA2_QM_ARB_MST_AVAIL_CRED_5                               0x548A34

#define mmDMA2_QM_ARB_MST_AVAIL_CRED_6                               0x548A38

#define mmDMA2_QM_ARB_MST_AVAIL_CRED_7                               0x548A3C

#define mmDMA2_QM_ARB_MST_AVAIL_CRED_8                               0x548A40

#define mmDMA2_QM_ARB_MST_AVAIL_CRED_9                               0x548A44

#define mmDMA2_QM_ARB_MST_AVAIL_CRED_10                              0x548A48

#define mmDMA2_QM_ARB_MST_AVAIL_CRED_11                              0x548A4C

#define mmDMA2_QM_ARB_MST_AVAIL_CRED_12                              0x548A50

#define mmDMA2_QM_ARB_MST_AVAIL_CRED_13                              0x548A54

#define mmDMA2_QM_ARB_MST_AVAIL_CRED_14                              0x548A58

#define mmDMA2_QM_ARB_MST_AVAIL_CRED_15                              0x548A5C

#define mmDMA2_QM_ARB_MST_AVAIL_CRED_16                              0x548A60

#define mmDMA2_QM_ARB_MST_AVAIL_CRED_17                              0x548A64

#define mmDMA2_QM_ARB_MST_AVAIL_CRED_18                              0x548A68

#define mmDMA2_QM_ARB_MST_AVAIL_CRED_19                              0x548A6C

#define mmDMA2_QM_ARB_MST_AVAIL_CRED_20                              0x548A70

#define mmDMA2_QM_ARB_MST_AVAIL_CRED_21                              0x548A74

#define mmDMA2_QM_ARB_MST_AVAIL_CRED_22                              0x548A78

#define mmDMA2_QM_ARB_MST_AVAIL_CRED_23                              0x548A7C

#define mmDMA2_QM_ARB_MST_AVAIL_CRED_24                              0x548A80

#define mmDMA2_QM_ARB_MST_AVAIL_CRED_25                              0x548A84

#define mmDMA2_QM_ARB_MST_AVAIL_CRED_26                              0x548A88

#define mmDMA2_QM_ARB_MST_AVAIL_CRED_27                              0x548A8C

#define mmDMA2_QM_ARB_MST_AVAIL_CRED_28                              0x548A90

#define mmDMA2_QM_ARB_MST_AVAIL_CRED_29                              0x548A94

#define mmDMA2_QM_ARB_MST_AVAIL_CRED_30                              0x548A98

#define mmDMA2_QM_ARB_MST_AVAIL_CRED_31                              0x548A9C

#define mmDMA2_QM_ARB_MST_CRED_INC                                   0x548AA0

#define mmDMA2_QM_ARB_MST_CHOISE_PUSH_OFST_0                         0x548AA4

#define mmDMA2_QM_ARB_MST_CHOISE_PUSH_OFST_1                         0x548AA8

#define mmDMA2_QM_ARB_MST_CHOISE_PUSH_OFST_2                         0x548AAC

#define mmDMA2_QM_ARB_MST_CHOISE_PUSH_OFST_3                         0x548AB0

#define mmDMA2_QM_ARB_MST_CHOISE_PUSH_OFST_4                         0x548AB4

#define mmDMA2_QM_ARB_MST_CHOISE_PUSH_OFST_5                         0x548AB8

#define mmDMA2_QM_ARB_MST_CHOISE_PUSH_OFST_6                         0x548ABC

#define mmDMA2_QM_ARB_MST_CHOISE_PUSH_OFST_7                         0x548AC0

#define mmDMA2_QM_ARB_MST_CHOISE_PUSH_OFST_8                         0x548AC4

#define mmDMA2_QM_ARB_MST_CHOISE_PUSH_OFST_9                         0x548AC8

#define mmDMA2_QM_ARB_MST_CHOISE_PUSH_OFST_10                        0x548ACC

#define mmDMA2_QM_ARB_MST_CHOISE_PUSH_OFST_11                        0x548AD0

#define mmDMA2_QM_ARB_MST_CHOISE_PUSH_OFST_12                        0x548AD4

#define mmDMA2_QM_ARB_MST_CHOISE_PUSH_OFST_13                        0x548AD8

#define mmDMA2_QM_ARB_MST_CHOISE_PUSH_OFST_14                        0x548ADC

#define mmDMA2_QM_ARB_MST_CHOISE_PUSH_OFST_15                        0x548AE0

#define mmDMA2_QM_ARB_MST_CHOISE_PUSH_OFST_16                        0x548AE4

#define mmDMA2_QM_ARB_MST_CHOISE_PUSH_OFST_17                        0x548AE8

#define mmDMA2_QM_ARB_MST_CHOISE_PUSH_OFST_18                        0x548AEC

#define mmDMA2_QM_ARB_MST_CHOISE_PUSH_OFST_19                        0x548AF0

#define mmDMA2_QM_ARB_MST_CHOISE_PUSH_OFST_20                        0x548AF4

#define mmDMA2_QM_ARB_MST_CHOISE_PUSH_OFST_21                        0x548AF8

#define mmDMA2_QM_ARB_MST_CHOISE_PUSH_OFST_22                        0x548AFC

#define mmDMA2_QM_ARB_MST_CHOISE_PUSH_OFST_23                        0x548B00

#define mmDMA2_QM_ARB_MST_CHOISE_PUSH_OFST_24                        0x548B04

#define mmDMA2_QM_ARB_MST_CHOISE_PUSH_OFST_25                        0x548B08

#define mmDMA2_QM_ARB_MST_CHOISE_PUSH_OFST_26                        0x548B0C

#define mmDMA2_QM_ARB_MST_CHOISE_PUSH_OFST_27                        0x548B10

#define mmDMA2_QM_ARB_MST_CHOISE_PUSH_OFST_28                        0x548B14

#define mmDMA2_QM_ARB_MST_CHOISE_PUSH_OFST_29                        0x548B18

#define mmDMA2_QM_ARB_MST_CHOISE_PUSH_OFST_30                        0x548B1C

#define mmDMA2_QM_ARB_MST_CHOISE_PUSH_OFST_31                        0x548B20

#define mmDMA2_QM_ARB_SLV_MASTER_INC_CRED_OFST                       0x548B28

#define mmDMA2_QM_ARB_MST_SLAVE_EN                                   0x548B2C

#define mmDMA2_QM_ARB_MST_QUIET_PER                                  0x548B34

#define mmDMA2_QM_ARB_SLV_CHOISE_WDT                                 0x548B38

#define mmDMA2_QM_ARB_SLV_ID                                         0x548B3C

#define mmDMA2_QM_ARB_MSG_MAX_INFLIGHT                               0x548B44

#define mmDMA2_QM_ARB_MSG_AWUSER_31_11                               0x548B48

#define mmDMA2_QM_ARB_MSG_AWUSER_SEC_PROP                            0x548B4C

#define mmDMA2_QM_ARB_MSG_AWUSER_NON_SEC_PROP                        0x548B50

#define mmDMA2_QM_ARB_BASE_LO                                        0x548B54

#define mmDMA2_QM_ARB_BASE_HI                                        0x548B58

#define mmDMA2_QM_ARB_STATE_STS                                      0x548B80

#define mmDMA2_QM_ARB_CHOISE_FULLNESS_STS                            0x548B84

#define mmDMA2_QM_ARB_MSG_STS                                        0x548B88

#define mmDMA2_QM_ARB_SLV_CHOISE_Q_HEAD                              0x548B8C

#define mmDMA2_QM_ARB_ERR_CAUSE                                      0x548B9C

#define mmDMA2_QM_ARB_ERR_MSG_EN                                     0x548BA0

#define mmDMA2_QM_ARB_ERR_STS_DRP                                    0x548BA8

#define mmDMA2_QM_ARB_MST_CRED_STS_0                                 0x548BB0

#define mmDMA2_QM_ARB_MST_CRED_STS_1                                 0x548BB4

#define mmDMA2_QM_ARB_MST_CRED_STS_2                                 0x548BB8

#define mmDMA2_QM_ARB_MST_CRED_STS_3                                 0x548BBC

#define mmDMA2_QM_ARB_MST_CRED_STS_4                                 0x548BC0

#define mmDMA2_QM_ARB_MST_CRED_STS_5                                 0x548BC4

#define mmDMA2_QM_ARB_MST_CRED_STS_6                                 0x548BC8

#define mmDMA2_QM_ARB_MST_CRED_STS_7                                 0x548BCC

#define mmDMA2_QM_ARB_MST_CRED_STS_8                                 0x548BD0

#define mmDMA2_QM_ARB_MST_CRED_STS_9                                 0x548BD4

#define mmDMA2_QM_ARB_MST_CRED_STS_10                                0x548BD8

#define mmDMA2_QM_ARB_MST_CRED_STS_11                                0x548BDC

#define mmDMA2_QM_ARB_MST_CRED_STS_12                                0x548BE0

#define mmDMA2_QM_ARB_MST_CRED_STS_13                                0x548BE4

#define mmDMA2_QM_ARB_MST_CRED_STS_14                                0x548BE8

#define mmDMA2_QM_ARB_MST_CRED_STS_15                                0x548BEC

#define mmDMA2_QM_ARB_MST_CRED_STS_16                                0x548BF0

#define mmDMA2_QM_ARB_MST_CRED_STS_17                                0x548BF4

#define mmDMA2_QM_ARB_MST_CRED_STS_18                                0x548BF8

#define mmDMA2_QM_ARB_MST_CRED_STS_19                                0x548BFC

#define mmDMA2_QM_ARB_MST_CRED_STS_20                                0x548C00

#define mmDMA2_QM_ARB_MST_CRED_STS_21                                0x548C04

#define mmDMA2_QM_ARB_MST_CRED_STS_22                                0x548C08

#define mmDMA2_QM_ARB_MST_CRED_STS_23                                0x548C0C

#define mmDMA2_QM_ARB_MST_CRED_STS_24                                0x548C10

#define mmDMA2_QM_ARB_MST_CRED_STS_25                                0x548C14

#define mmDMA2_QM_ARB_MST_CRED_STS_26                                0x548C18

#define mmDMA2_QM_ARB_MST_CRED_STS_27                                0x548C1C

#define mmDMA2_QM_ARB_MST_CRED_STS_28                                0x548C20

#define mmDMA2_QM_ARB_MST_CRED_STS_29                                0x548C24

#define mmDMA2_QM_ARB_MST_CRED_STS_30                                0x548C28

#define mmDMA2_QM_ARB_MST_CRED_STS_31                                0x548C2C

#define mmDMA2_QM_CGM_CFG                                            0x548C70

#define mmDMA2_QM_CGM_STS                                            0x548C74

#define mmDMA2_QM_CGM_CFG1                                           0x548C78

#define mmDMA2_QM_LOCAL_RANGE_BASE                                   0x548C80

#define mmDMA2_QM_LOCAL_RANGE_SIZE                                   0x548C84

#define mmDMA2_QM_CSMR_STRICT_PRIO_CFG                               0x548C90

#define mmDMA2_QM_HBW_RD_RATE_LIM_CFG_1                              0x548C94

#define mmDMA2_QM_LBW_WR_RATE_LIM_CFG_0                              0x548C98

#define mmDMA2_QM_LBW_WR_RATE_LIM_CFG_1                              0x548C9C

#define mmDMA2_QM_HBW_RD_RATE_LIM_CFG_0                              0x548CA0

#define mmDMA2_QM_GLBL_AXCACHE                                       0x548CA4

#define mmDMA2_QM_IND_GW_APB_CFG                                     0x548CB0

#define mmDMA2_QM_IND_GW_APB_WDATA                                   0x548CB4

#define mmDMA2_QM_IND_GW_APB_RDATA                                   0x548CB8

#define mmDMA2_QM_IND_GW_APB_STATUS                                  0x548CBC

#define mmDMA2_QM_GLBL_ERR_ADDR_LO                                   0x548CD0

#define mmDMA2_QM_GLBL_ERR_ADDR_HI                                   0x548CD4

#define mmDMA2_QM_GLBL_ERR_WDATA                                     0x548CD8

#define mmDMA2_QM_GLBL_MEM_INIT_BUSY                                 0x548D00

#endif /* ASIC_REG_DMA2_QM_REGS_H_ */
