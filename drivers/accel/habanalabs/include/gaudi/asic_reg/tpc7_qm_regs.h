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

#ifndef ASIC_REG_TPC7_QM_REGS_H_
#define ASIC_REG_TPC7_QM_REGS_H_

/*
 *****************************************
 *   TPC7_QM (Prototype: QMAN)
 *****************************************
 */

#define mmTPC7_QM_GLBL_CFG0                                          0xFC8000

#define mmTPC7_QM_GLBL_CFG1                                          0xFC8004

#define mmTPC7_QM_GLBL_PROT                                          0xFC8008

#define mmTPC7_QM_GLBL_ERR_CFG                                       0xFC800C

#define mmTPC7_QM_GLBL_SECURE_PROPS_0                                0xFC8010

#define mmTPC7_QM_GLBL_SECURE_PROPS_1                                0xFC8014

#define mmTPC7_QM_GLBL_SECURE_PROPS_2                                0xFC8018

#define mmTPC7_QM_GLBL_SECURE_PROPS_3                                0xFC801C

#define mmTPC7_QM_GLBL_SECURE_PROPS_4                                0xFC8020

#define mmTPC7_QM_GLBL_NON_SECURE_PROPS_0                            0xFC8024

#define mmTPC7_QM_GLBL_NON_SECURE_PROPS_1                            0xFC8028

#define mmTPC7_QM_GLBL_NON_SECURE_PROPS_2                            0xFC802C

#define mmTPC7_QM_GLBL_NON_SECURE_PROPS_3                            0xFC8030

#define mmTPC7_QM_GLBL_NON_SECURE_PROPS_4                            0xFC8034

#define mmTPC7_QM_GLBL_STS0                                          0xFC8038

#define mmTPC7_QM_GLBL_STS1_0                                        0xFC8040

#define mmTPC7_QM_GLBL_STS1_1                                        0xFC8044

#define mmTPC7_QM_GLBL_STS1_2                                        0xFC8048

#define mmTPC7_QM_GLBL_STS1_3                                        0xFC804C

#define mmTPC7_QM_GLBL_STS1_4                                        0xFC8050

#define mmTPC7_QM_GLBL_MSG_EN_0                                      0xFC8054

#define mmTPC7_QM_GLBL_MSG_EN_1                                      0xFC8058

#define mmTPC7_QM_GLBL_MSG_EN_2                                      0xFC805C

#define mmTPC7_QM_GLBL_MSG_EN_3                                      0xFC8060

#define mmTPC7_QM_GLBL_MSG_EN_4                                      0xFC8068

#define mmTPC7_QM_PQ_BASE_LO_0                                       0xFC8070

#define mmTPC7_QM_PQ_BASE_LO_1                                       0xFC8074

#define mmTPC7_QM_PQ_BASE_LO_2                                       0xFC8078

#define mmTPC7_QM_PQ_BASE_LO_3                                       0xFC807C

#define mmTPC7_QM_PQ_BASE_HI_0                                       0xFC8080

#define mmTPC7_QM_PQ_BASE_HI_1                                       0xFC8084

#define mmTPC7_QM_PQ_BASE_HI_2                                       0xFC8088

#define mmTPC7_QM_PQ_BASE_HI_3                                       0xFC808C

#define mmTPC7_QM_PQ_SIZE_0                                          0xFC8090

#define mmTPC7_QM_PQ_SIZE_1                                          0xFC8094

#define mmTPC7_QM_PQ_SIZE_2                                          0xFC8098

#define mmTPC7_QM_PQ_SIZE_3                                          0xFC809C

#define mmTPC7_QM_PQ_PI_0                                            0xFC80A0

#define mmTPC7_QM_PQ_PI_1                                            0xFC80A4

#define mmTPC7_QM_PQ_PI_2                                            0xFC80A8

#define mmTPC7_QM_PQ_PI_3                                            0xFC80AC

#define mmTPC7_QM_PQ_CI_0                                            0xFC80B0

#define mmTPC7_QM_PQ_CI_1                                            0xFC80B4

#define mmTPC7_QM_PQ_CI_2                                            0xFC80B8

#define mmTPC7_QM_PQ_CI_3                                            0xFC80BC

#define mmTPC7_QM_PQ_CFG0_0                                          0xFC80C0

#define mmTPC7_QM_PQ_CFG0_1                                          0xFC80C4

#define mmTPC7_QM_PQ_CFG0_2                                          0xFC80C8

#define mmTPC7_QM_PQ_CFG0_3                                          0xFC80CC

#define mmTPC7_QM_PQ_CFG1_0                                          0xFC80D0

#define mmTPC7_QM_PQ_CFG1_1                                          0xFC80D4

#define mmTPC7_QM_PQ_CFG1_2                                          0xFC80D8

#define mmTPC7_QM_PQ_CFG1_3                                          0xFC80DC

#define mmTPC7_QM_PQ_ARUSER_31_11_0                                  0xFC80E0

#define mmTPC7_QM_PQ_ARUSER_31_11_1                                  0xFC80E4

#define mmTPC7_QM_PQ_ARUSER_31_11_2                                  0xFC80E8

#define mmTPC7_QM_PQ_ARUSER_31_11_3                                  0xFC80EC

#define mmTPC7_QM_PQ_STS0_0                                          0xFC80F0

#define mmTPC7_QM_PQ_STS0_1                                          0xFC80F4

#define mmTPC7_QM_PQ_STS0_2                                          0xFC80F8

#define mmTPC7_QM_PQ_STS0_3                                          0xFC80FC

#define mmTPC7_QM_PQ_STS1_0                                          0xFC8100

#define mmTPC7_QM_PQ_STS1_1                                          0xFC8104

#define mmTPC7_QM_PQ_STS1_2                                          0xFC8108

#define mmTPC7_QM_PQ_STS1_3                                          0xFC810C

#define mmTPC7_QM_CQ_CFG0_0                                          0xFC8110

#define mmTPC7_QM_CQ_CFG0_1                                          0xFC8114

#define mmTPC7_QM_CQ_CFG0_2                                          0xFC8118

#define mmTPC7_QM_CQ_CFG0_3                                          0xFC811C

#define mmTPC7_QM_CQ_CFG0_4                                          0xFC8120

#define mmTPC7_QM_CQ_CFG1_0                                          0xFC8124

#define mmTPC7_QM_CQ_CFG1_1                                          0xFC8128

#define mmTPC7_QM_CQ_CFG1_2                                          0xFC812C

#define mmTPC7_QM_CQ_CFG1_3                                          0xFC8130

#define mmTPC7_QM_CQ_CFG1_4                                          0xFC8134

#define mmTPC7_QM_CQ_ARUSER_31_11_0                                  0xFC8138

#define mmTPC7_QM_CQ_ARUSER_31_11_1                                  0xFC813C

#define mmTPC7_QM_CQ_ARUSER_31_11_2                                  0xFC8140

#define mmTPC7_QM_CQ_ARUSER_31_11_3                                  0xFC8144

#define mmTPC7_QM_CQ_ARUSER_31_11_4                                  0xFC8148

#define mmTPC7_QM_CQ_STS0_0                                          0xFC814C

#define mmTPC7_QM_CQ_STS0_1                                          0xFC8150

#define mmTPC7_QM_CQ_STS0_2                                          0xFC8154

#define mmTPC7_QM_CQ_STS0_3                                          0xFC8158

#define mmTPC7_QM_CQ_STS0_4                                          0xFC815C

#define mmTPC7_QM_CQ_STS1_0                                          0xFC8160

#define mmTPC7_QM_CQ_STS1_1                                          0xFC8164

#define mmTPC7_QM_CQ_STS1_2                                          0xFC8168

#define mmTPC7_QM_CQ_STS1_3                                          0xFC816C

#define mmTPC7_QM_CQ_STS1_4                                          0xFC8170

#define mmTPC7_QM_CQ_PTR_LO_0                                        0xFC8174

#define mmTPC7_QM_CQ_PTR_HI_0                                        0xFC8178

#define mmTPC7_QM_CQ_TSIZE_0                                         0xFC817C

#define mmTPC7_QM_CQ_CTL_0                                           0xFC8180

#define mmTPC7_QM_CQ_PTR_LO_1                                        0xFC8184

#define mmTPC7_QM_CQ_PTR_HI_1                                        0xFC8188

#define mmTPC7_QM_CQ_TSIZE_1                                         0xFC818C

#define mmTPC7_QM_CQ_CTL_1                                           0xFC8190

#define mmTPC7_QM_CQ_PTR_LO_2                                        0xFC8194

#define mmTPC7_QM_CQ_PTR_HI_2                                        0xFC8198

#define mmTPC7_QM_CQ_TSIZE_2                                         0xFC819C

#define mmTPC7_QM_CQ_CTL_2                                           0xFC81A0

#define mmTPC7_QM_CQ_PTR_LO_3                                        0xFC81A4

#define mmTPC7_QM_CQ_PTR_HI_3                                        0xFC81A8

#define mmTPC7_QM_CQ_TSIZE_3                                         0xFC81AC

#define mmTPC7_QM_CQ_CTL_3                                           0xFC81B0

#define mmTPC7_QM_CQ_PTR_LO_4                                        0xFC81B4

#define mmTPC7_QM_CQ_PTR_HI_4                                        0xFC81B8

#define mmTPC7_QM_CQ_TSIZE_4                                         0xFC81BC

#define mmTPC7_QM_CQ_CTL_4                                           0xFC81C0

#define mmTPC7_QM_CQ_PTR_LO_STS_0                                    0xFC81C4

#define mmTPC7_QM_CQ_PTR_LO_STS_1                                    0xFC81C8

#define mmTPC7_QM_CQ_PTR_LO_STS_2                                    0xFC81CC

#define mmTPC7_QM_CQ_PTR_LO_STS_3                                    0xFC81D0

#define mmTPC7_QM_CQ_PTR_LO_STS_4                                    0xFC81D4

#define mmTPC7_QM_CQ_PTR_HI_STS_0                                    0xFC81D8

#define mmTPC7_QM_CQ_PTR_HI_STS_1                                    0xFC81DC

#define mmTPC7_QM_CQ_PTR_HI_STS_2                                    0xFC81E0

#define mmTPC7_QM_CQ_PTR_HI_STS_3                                    0xFC81E4

#define mmTPC7_QM_CQ_PTR_HI_STS_4                                    0xFC81E8

#define mmTPC7_QM_CQ_TSIZE_STS_0                                     0xFC81EC

#define mmTPC7_QM_CQ_TSIZE_STS_1                                     0xFC81F0

#define mmTPC7_QM_CQ_TSIZE_STS_2                                     0xFC81F4

#define mmTPC7_QM_CQ_TSIZE_STS_3                                     0xFC81F8

#define mmTPC7_QM_CQ_TSIZE_STS_4                                     0xFC81FC

#define mmTPC7_QM_CQ_CTL_STS_0                                       0xFC8200

#define mmTPC7_QM_CQ_CTL_STS_1                                       0xFC8204

#define mmTPC7_QM_CQ_CTL_STS_2                                       0xFC8208

#define mmTPC7_QM_CQ_CTL_STS_3                                       0xFC820C

#define mmTPC7_QM_CQ_CTL_STS_4                                       0xFC8210

#define mmTPC7_QM_CQ_IFIFO_CNT_0                                     0xFC8214

#define mmTPC7_QM_CQ_IFIFO_CNT_1                                     0xFC8218

#define mmTPC7_QM_CQ_IFIFO_CNT_2                                     0xFC821C

#define mmTPC7_QM_CQ_IFIFO_CNT_3                                     0xFC8220

#define mmTPC7_QM_CQ_IFIFO_CNT_4                                     0xFC8224

#define mmTPC7_QM_CP_MSG_BASE0_ADDR_LO_0                             0xFC8228

#define mmTPC7_QM_CP_MSG_BASE0_ADDR_LO_1                             0xFC822C

#define mmTPC7_QM_CP_MSG_BASE0_ADDR_LO_2                             0xFC8230

#define mmTPC7_QM_CP_MSG_BASE0_ADDR_LO_3                             0xFC8234

#define mmTPC7_QM_CP_MSG_BASE0_ADDR_LO_4                             0xFC8238

#define mmTPC7_QM_CP_MSG_BASE0_ADDR_HI_0                             0xFC823C

#define mmTPC7_QM_CP_MSG_BASE0_ADDR_HI_1                             0xFC8240

#define mmTPC7_QM_CP_MSG_BASE0_ADDR_HI_2                             0xFC8244

#define mmTPC7_QM_CP_MSG_BASE0_ADDR_HI_3                             0xFC8248

#define mmTPC7_QM_CP_MSG_BASE0_ADDR_HI_4                             0xFC824C

#define mmTPC7_QM_CP_MSG_BASE1_ADDR_LO_0                             0xFC8250

#define mmTPC7_QM_CP_MSG_BASE1_ADDR_LO_1                             0xFC8254

#define mmTPC7_QM_CP_MSG_BASE1_ADDR_LO_2                             0xFC8258

#define mmTPC7_QM_CP_MSG_BASE1_ADDR_LO_3                             0xFC825C

#define mmTPC7_QM_CP_MSG_BASE1_ADDR_LO_4                             0xFC8260

#define mmTPC7_QM_CP_MSG_BASE1_ADDR_HI_0                             0xFC8264

#define mmTPC7_QM_CP_MSG_BASE1_ADDR_HI_1                             0xFC8268

#define mmTPC7_QM_CP_MSG_BASE1_ADDR_HI_2                             0xFC826C

#define mmTPC7_QM_CP_MSG_BASE1_ADDR_HI_3                             0xFC8270

#define mmTPC7_QM_CP_MSG_BASE1_ADDR_HI_4                             0xFC8274

#define mmTPC7_QM_CP_MSG_BASE2_ADDR_LO_0                             0xFC8278

#define mmTPC7_QM_CP_MSG_BASE2_ADDR_LO_1                             0xFC827C

#define mmTPC7_QM_CP_MSG_BASE2_ADDR_LO_2                             0xFC8280

#define mmTPC7_QM_CP_MSG_BASE2_ADDR_LO_3                             0xFC8284

#define mmTPC7_QM_CP_MSG_BASE2_ADDR_LO_4                             0xFC8288

#define mmTPC7_QM_CP_MSG_BASE2_ADDR_HI_0                             0xFC828C

#define mmTPC7_QM_CP_MSG_BASE2_ADDR_HI_1                             0xFC8290

#define mmTPC7_QM_CP_MSG_BASE2_ADDR_HI_2                             0xFC8294

#define mmTPC7_QM_CP_MSG_BASE2_ADDR_HI_3                             0xFC8298

#define mmTPC7_QM_CP_MSG_BASE2_ADDR_HI_4                             0xFC829C

#define mmTPC7_QM_CP_MSG_BASE3_ADDR_LO_0                             0xFC82A0

#define mmTPC7_QM_CP_MSG_BASE3_ADDR_LO_1                             0xFC82A4

#define mmTPC7_QM_CP_MSG_BASE3_ADDR_LO_2                             0xFC82A8

#define mmTPC7_QM_CP_MSG_BASE3_ADDR_LO_3                             0xFC82AC

#define mmTPC7_QM_CP_MSG_BASE3_ADDR_LO_4                             0xFC82B0

#define mmTPC7_QM_CP_MSG_BASE3_ADDR_HI_0                             0xFC82B4

#define mmTPC7_QM_CP_MSG_BASE3_ADDR_HI_1                             0xFC82B8

#define mmTPC7_QM_CP_MSG_BASE3_ADDR_HI_2                             0xFC82BC

#define mmTPC7_QM_CP_MSG_BASE3_ADDR_HI_3                             0xFC82C0

#define mmTPC7_QM_CP_MSG_BASE3_ADDR_HI_4                             0xFC82C4

#define mmTPC7_QM_CP_LDMA_TSIZE_OFFSET_0                             0xFC82C8

#define mmTPC7_QM_CP_LDMA_TSIZE_OFFSET_1                             0xFC82CC

#define mmTPC7_QM_CP_LDMA_TSIZE_OFFSET_2                             0xFC82D0

#define mmTPC7_QM_CP_LDMA_TSIZE_OFFSET_3                             0xFC82D4

#define mmTPC7_QM_CP_LDMA_TSIZE_OFFSET_4                             0xFC82D8

#define mmTPC7_QM_CP_LDMA_SRC_BASE_LO_OFFSET_0                       0xFC82E0

#define mmTPC7_QM_CP_LDMA_SRC_BASE_LO_OFFSET_1                       0xFC82E4

#define mmTPC7_QM_CP_LDMA_SRC_BASE_LO_OFFSET_2                       0xFC82E8

#define mmTPC7_QM_CP_LDMA_SRC_BASE_LO_OFFSET_3                       0xFC82EC

#define mmTPC7_QM_CP_LDMA_SRC_BASE_LO_OFFSET_4                       0xFC82F0

#define mmTPC7_QM_CP_LDMA_DST_BASE_LO_OFFSET_0                       0xFC82F4

#define mmTPC7_QM_CP_LDMA_DST_BASE_LO_OFFSET_1                       0xFC82F8

#define mmTPC7_QM_CP_LDMA_DST_BASE_LO_OFFSET_2                       0xFC82FC

#define mmTPC7_QM_CP_LDMA_DST_BASE_LO_OFFSET_3                       0xFC8300

#define mmTPC7_QM_CP_LDMA_DST_BASE_LO_OFFSET_4                       0xFC8304

#define mmTPC7_QM_CP_FENCE0_RDATA_0                                  0xFC8308

#define mmTPC7_QM_CP_FENCE0_RDATA_1                                  0xFC830C

#define mmTPC7_QM_CP_FENCE0_RDATA_2                                  0xFC8310

#define mmTPC7_QM_CP_FENCE0_RDATA_3                                  0xFC8314

#define mmTPC7_QM_CP_FENCE0_RDATA_4                                  0xFC8318

#define mmTPC7_QM_CP_FENCE1_RDATA_0                                  0xFC831C

#define mmTPC7_QM_CP_FENCE1_RDATA_1                                  0xFC8320

#define mmTPC7_QM_CP_FENCE1_RDATA_2                                  0xFC8324

#define mmTPC7_QM_CP_FENCE1_RDATA_3                                  0xFC8328

#define mmTPC7_QM_CP_FENCE1_RDATA_4                                  0xFC832C

#define mmTPC7_QM_CP_FENCE2_RDATA_0                                  0xFC8330

#define mmTPC7_QM_CP_FENCE2_RDATA_1                                  0xFC8334

#define mmTPC7_QM_CP_FENCE2_RDATA_2                                  0xFC8338

#define mmTPC7_QM_CP_FENCE2_RDATA_3                                  0xFC833C

#define mmTPC7_QM_CP_FENCE2_RDATA_4                                  0xFC8340

#define mmTPC7_QM_CP_FENCE3_RDATA_0                                  0xFC8344

#define mmTPC7_QM_CP_FENCE3_RDATA_1                                  0xFC8348

#define mmTPC7_QM_CP_FENCE3_RDATA_2                                  0xFC834C

#define mmTPC7_QM_CP_FENCE3_RDATA_3                                  0xFC8350

#define mmTPC7_QM_CP_FENCE3_RDATA_4                                  0xFC8354

#define mmTPC7_QM_CP_FENCE0_CNT_0                                    0xFC8358

#define mmTPC7_QM_CP_FENCE0_CNT_1                                    0xFC835C

#define mmTPC7_QM_CP_FENCE0_CNT_2                                    0xFC8360

#define mmTPC7_QM_CP_FENCE0_CNT_3                                    0xFC8364

#define mmTPC7_QM_CP_FENCE0_CNT_4                                    0xFC8368

#define mmTPC7_QM_CP_FENCE1_CNT_0                                    0xFC836C

#define mmTPC7_QM_CP_FENCE1_CNT_1                                    0xFC8370

#define mmTPC7_QM_CP_FENCE1_CNT_2                                    0xFC8374

#define mmTPC7_QM_CP_FENCE1_CNT_3                                    0xFC8378

#define mmTPC7_QM_CP_FENCE1_CNT_4                                    0xFC837C

#define mmTPC7_QM_CP_FENCE2_CNT_0                                    0xFC8380

#define mmTPC7_QM_CP_FENCE2_CNT_1                                    0xFC8384

#define mmTPC7_QM_CP_FENCE2_CNT_2                                    0xFC8388

#define mmTPC7_QM_CP_FENCE2_CNT_3                                    0xFC838C

#define mmTPC7_QM_CP_FENCE2_CNT_4                                    0xFC8390

#define mmTPC7_QM_CP_FENCE3_CNT_0                                    0xFC8394

#define mmTPC7_QM_CP_FENCE3_CNT_1                                    0xFC8398

#define mmTPC7_QM_CP_FENCE3_CNT_2                                    0xFC839C

#define mmTPC7_QM_CP_FENCE3_CNT_3                                    0xFC83A0

#define mmTPC7_QM_CP_FENCE3_CNT_4                                    0xFC83A4

#define mmTPC7_QM_CP_STS_0                                           0xFC83A8

#define mmTPC7_QM_CP_STS_1                                           0xFC83AC

#define mmTPC7_QM_CP_STS_2                                           0xFC83B0

#define mmTPC7_QM_CP_STS_3                                           0xFC83B4

#define mmTPC7_QM_CP_STS_4                                           0xFC83B8

#define mmTPC7_QM_CP_CURRENT_INST_LO_0                               0xFC83BC

#define mmTPC7_QM_CP_CURRENT_INST_LO_1                               0xFC83C0

#define mmTPC7_QM_CP_CURRENT_INST_LO_2                               0xFC83C4

#define mmTPC7_QM_CP_CURRENT_INST_LO_3                               0xFC83C8

#define mmTPC7_QM_CP_CURRENT_INST_LO_4                               0xFC83CC

#define mmTPC7_QM_CP_CURRENT_INST_HI_0                               0xFC83D0

#define mmTPC7_QM_CP_CURRENT_INST_HI_1                               0xFC83D4

#define mmTPC7_QM_CP_CURRENT_INST_HI_2                               0xFC83D8

#define mmTPC7_QM_CP_CURRENT_INST_HI_3                               0xFC83DC

#define mmTPC7_QM_CP_CURRENT_INST_HI_4                               0xFC83E0

#define mmTPC7_QM_CP_BARRIER_CFG_0                                   0xFC83F4

#define mmTPC7_QM_CP_BARRIER_CFG_1                                   0xFC83F8

#define mmTPC7_QM_CP_BARRIER_CFG_2                                   0xFC83FC

#define mmTPC7_QM_CP_BARRIER_CFG_3                                   0xFC8400

#define mmTPC7_QM_CP_BARRIER_CFG_4                                   0xFC8404

#define mmTPC7_QM_CP_DBG_0_0                                         0xFC8408

#define mmTPC7_QM_CP_DBG_0_1                                         0xFC840C

#define mmTPC7_QM_CP_DBG_0_2                                         0xFC8410

#define mmTPC7_QM_CP_DBG_0_3                                         0xFC8414

#define mmTPC7_QM_CP_DBG_0_4                                         0xFC8418

#define mmTPC7_QM_CP_ARUSER_31_11_0                                  0xFC841C

#define mmTPC7_QM_CP_ARUSER_31_11_1                                  0xFC8420

#define mmTPC7_QM_CP_ARUSER_31_11_2                                  0xFC8424

#define mmTPC7_QM_CP_ARUSER_31_11_3                                  0xFC8428

#define mmTPC7_QM_CP_ARUSER_31_11_4                                  0xFC842C

#define mmTPC7_QM_CP_AWUSER_31_11_0                                  0xFC8430

#define mmTPC7_QM_CP_AWUSER_31_11_1                                  0xFC8434

#define mmTPC7_QM_CP_AWUSER_31_11_2                                  0xFC8438

#define mmTPC7_QM_CP_AWUSER_31_11_3                                  0xFC843C

#define mmTPC7_QM_CP_AWUSER_31_11_4                                  0xFC8440

#define mmTPC7_QM_ARB_CFG_0                                          0xFC8A00

#define mmTPC7_QM_ARB_CHOISE_Q_PUSH                                  0xFC8A04

#define mmTPC7_QM_ARB_WRR_WEIGHT_0                                   0xFC8A08

#define mmTPC7_QM_ARB_WRR_WEIGHT_1                                   0xFC8A0C

#define mmTPC7_QM_ARB_WRR_WEIGHT_2                                   0xFC8A10

#define mmTPC7_QM_ARB_WRR_WEIGHT_3                                   0xFC8A14

#define mmTPC7_QM_ARB_CFG_1                                          0xFC8A18

#define mmTPC7_QM_ARB_MST_AVAIL_CRED_0                               0xFC8A20

#define mmTPC7_QM_ARB_MST_AVAIL_CRED_1                               0xFC8A24

#define mmTPC7_QM_ARB_MST_AVAIL_CRED_2                               0xFC8A28

#define mmTPC7_QM_ARB_MST_AVAIL_CRED_3                               0xFC8A2C

#define mmTPC7_QM_ARB_MST_AVAIL_CRED_4                               0xFC8A30

#define mmTPC7_QM_ARB_MST_AVAIL_CRED_5                               0xFC8A34

#define mmTPC7_QM_ARB_MST_AVAIL_CRED_6                               0xFC8A38

#define mmTPC7_QM_ARB_MST_AVAIL_CRED_7                               0xFC8A3C

#define mmTPC7_QM_ARB_MST_AVAIL_CRED_8                               0xFC8A40

#define mmTPC7_QM_ARB_MST_AVAIL_CRED_9                               0xFC8A44

#define mmTPC7_QM_ARB_MST_AVAIL_CRED_10                              0xFC8A48

#define mmTPC7_QM_ARB_MST_AVAIL_CRED_11                              0xFC8A4C

#define mmTPC7_QM_ARB_MST_AVAIL_CRED_12                              0xFC8A50

#define mmTPC7_QM_ARB_MST_AVAIL_CRED_13                              0xFC8A54

#define mmTPC7_QM_ARB_MST_AVAIL_CRED_14                              0xFC8A58

#define mmTPC7_QM_ARB_MST_AVAIL_CRED_15                              0xFC8A5C

#define mmTPC7_QM_ARB_MST_AVAIL_CRED_16                              0xFC8A60

#define mmTPC7_QM_ARB_MST_AVAIL_CRED_17                              0xFC8A64

#define mmTPC7_QM_ARB_MST_AVAIL_CRED_18                              0xFC8A68

#define mmTPC7_QM_ARB_MST_AVAIL_CRED_19                              0xFC8A6C

#define mmTPC7_QM_ARB_MST_AVAIL_CRED_20                              0xFC8A70

#define mmTPC7_QM_ARB_MST_AVAIL_CRED_21                              0xFC8A74

#define mmTPC7_QM_ARB_MST_AVAIL_CRED_22                              0xFC8A78

#define mmTPC7_QM_ARB_MST_AVAIL_CRED_23                              0xFC8A7C

#define mmTPC7_QM_ARB_MST_AVAIL_CRED_24                              0xFC8A80

#define mmTPC7_QM_ARB_MST_AVAIL_CRED_25                              0xFC8A84

#define mmTPC7_QM_ARB_MST_AVAIL_CRED_26                              0xFC8A88

#define mmTPC7_QM_ARB_MST_AVAIL_CRED_27                              0xFC8A8C

#define mmTPC7_QM_ARB_MST_AVAIL_CRED_28                              0xFC8A90

#define mmTPC7_QM_ARB_MST_AVAIL_CRED_29                              0xFC8A94

#define mmTPC7_QM_ARB_MST_AVAIL_CRED_30                              0xFC8A98

#define mmTPC7_QM_ARB_MST_AVAIL_CRED_31                              0xFC8A9C

#define mmTPC7_QM_ARB_MST_CRED_INC                                   0xFC8AA0

#define mmTPC7_QM_ARB_MST_CHOISE_PUSH_OFST_0                         0xFC8AA4

#define mmTPC7_QM_ARB_MST_CHOISE_PUSH_OFST_1                         0xFC8AA8

#define mmTPC7_QM_ARB_MST_CHOISE_PUSH_OFST_2                         0xFC8AAC

#define mmTPC7_QM_ARB_MST_CHOISE_PUSH_OFST_3                         0xFC8AB0

#define mmTPC7_QM_ARB_MST_CHOISE_PUSH_OFST_4                         0xFC8AB4

#define mmTPC7_QM_ARB_MST_CHOISE_PUSH_OFST_5                         0xFC8AB8

#define mmTPC7_QM_ARB_MST_CHOISE_PUSH_OFST_6                         0xFC8ABC

#define mmTPC7_QM_ARB_MST_CHOISE_PUSH_OFST_7                         0xFC8AC0

#define mmTPC7_QM_ARB_MST_CHOISE_PUSH_OFST_8                         0xFC8AC4

#define mmTPC7_QM_ARB_MST_CHOISE_PUSH_OFST_9                         0xFC8AC8

#define mmTPC7_QM_ARB_MST_CHOISE_PUSH_OFST_10                        0xFC8ACC

#define mmTPC7_QM_ARB_MST_CHOISE_PUSH_OFST_11                        0xFC8AD0

#define mmTPC7_QM_ARB_MST_CHOISE_PUSH_OFST_12                        0xFC8AD4

#define mmTPC7_QM_ARB_MST_CHOISE_PUSH_OFST_13                        0xFC8AD8

#define mmTPC7_QM_ARB_MST_CHOISE_PUSH_OFST_14                        0xFC8ADC

#define mmTPC7_QM_ARB_MST_CHOISE_PUSH_OFST_15                        0xFC8AE0

#define mmTPC7_QM_ARB_MST_CHOISE_PUSH_OFST_16                        0xFC8AE4

#define mmTPC7_QM_ARB_MST_CHOISE_PUSH_OFST_17                        0xFC8AE8

#define mmTPC7_QM_ARB_MST_CHOISE_PUSH_OFST_18                        0xFC8AEC

#define mmTPC7_QM_ARB_MST_CHOISE_PUSH_OFST_19                        0xFC8AF0

#define mmTPC7_QM_ARB_MST_CHOISE_PUSH_OFST_20                        0xFC8AF4

#define mmTPC7_QM_ARB_MST_CHOISE_PUSH_OFST_21                        0xFC8AF8

#define mmTPC7_QM_ARB_MST_CHOISE_PUSH_OFST_22                        0xFC8AFC

#define mmTPC7_QM_ARB_MST_CHOISE_PUSH_OFST_23                        0xFC8B00

#define mmTPC7_QM_ARB_MST_CHOISE_PUSH_OFST_24                        0xFC8B04

#define mmTPC7_QM_ARB_MST_CHOISE_PUSH_OFST_25                        0xFC8B08

#define mmTPC7_QM_ARB_MST_CHOISE_PUSH_OFST_26                        0xFC8B0C

#define mmTPC7_QM_ARB_MST_CHOISE_PUSH_OFST_27                        0xFC8B10

#define mmTPC7_QM_ARB_MST_CHOISE_PUSH_OFST_28                        0xFC8B14

#define mmTPC7_QM_ARB_MST_CHOISE_PUSH_OFST_29                        0xFC8B18

#define mmTPC7_QM_ARB_MST_CHOISE_PUSH_OFST_30                        0xFC8B1C

#define mmTPC7_QM_ARB_MST_CHOISE_PUSH_OFST_31                        0xFC8B20

#define mmTPC7_QM_ARB_SLV_MASTER_INC_CRED_OFST                       0xFC8B28

#define mmTPC7_QM_ARB_MST_SLAVE_EN                                   0xFC8B2C

#define mmTPC7_QM_ARB_MST_QUIET_PER                                  0xFC8B34

#define mmTPC7_QM_ARB_SLV_CHOISE_WDT                                 0xFC8B38

#define mmTPC7_QM_ARB_SLV_ID                                         0xFC8B3C

#define mmTPC7_QM_ARB_MSG_MAX_INFLIGHT                               0xFC8B44

#define mmTPC7_QM_ARB_MSG_AWUSER_31_11                               0xFC8B48

#define mmTPC7_QM_ARB_MSG_AWUSER_SEC_PROP                            0xFC8B4C

#define mmTPC7_QM_ARB_MSG_AWUSER_NON_SEC_PROP                        0xFC8B50

#define mmTPC7_QM_ARB_BASE_LO                                        0xFC8B54

#define mmTPC7_QM_ARB_BASE_HI                                        0xFC8B58

#define mmTPC7_QM_ARB_STATE_STS                                      0xFC8B80

#define mmTPC7_QM_ARB_CHOISE_FULLNESS_STS                            0xFC8B84

#define mmTPC7_QM_ARB_MSG_STS                                        0xFC8B88

#define mmTPC7_QM_ARB_SLV_CHOISE_Q_HEAD                              0xFC8B8C

#define mmTPC7_QM_ARB_ERR_CAUSE                                      0xFC8B9C

#define mmTPC7_QM_ARB_ERR_MSG_EN                                     0xFC8BA0

#define mmTPC7_QM_ARB_ERR_STS_DRP                                    0xFC8BA8

#define mmTPC7_QM_ARB_MST_CRED_STS_0                                 0xFC8BB0

#define mmTPC7_QM_ARB_MST_CRED_STS_1                                 0xFC8BB4

#define mmTPC7_QM_ARB_MST_CRED_STS_2                                 0xFC8BB8

#define mmTPC7_QM_ARB_MST_CRED_STS_3                                 0xFC8BBC

#define mmTPC7_QM_ARB_MST_CRED_STS_4                                 0xFC8BC0

#define mmTPC7_QM_ARB_MST_CRED_STS_5                                 0xFC8BC4

#define mmTPC7_QM_ARB_MST_CRED_STS_6                                 0xFC8BC8

#define mmTPC7_QM_ARB_MST_CRED_STS_7                                 0xFC8BCC

#define mmTPC7_QM_ARB_MST_CRED_STS_8                                 0xFC8BD0

#define mmTPC7_QM_ARB_MST_CRED_STS_9                                 0xFC8BD4

#define mmTPC7_QM_ARB_MST_CRED_STS_10                                0xFC8BD8

#define mmTPC7_QM_ARB_MST_CRED_STS_11                                0xFC8BDC

#define mmTPC7_QM_ARB_MST_CRED_STS_12                                0xFC8BE0

#define mmTPC7_QM_ARB_MST_CRED_STS_13                                0xFC8BE4

#define mmTPC7_QM_ARB_MST_CRED_STS_14                                0xFC8BE8

#define mmTPC7_QM_ARB_MST_CRED_STS_15                                0xFC8BEC

#define mmTPC7_QM_ARB_MST_CRED_STS_16                                0xFC8BF0

#define mmTPC7_QM_ARB_MST_CRED_STS_17                                0xFC8BF4

#define mmTPC7_QM_ARB_MST_CRED_STS_18                                0xFC8BF8

#define mmTPC7_QM_ARB_MST_CRED_STS_19                                0xFC8BFC

#define mmTPC7_QM_ARB_MST_CRED_STS_20                                0xFC8C00

#define mmTPC7_QM_ARB_MST_CRED_STS_21                                0xFC8C04

#define mmTPC7_QM_ARB_MST_CRED_STS_22                                0xFC8C08

#define mmTPC7_QM_ARB_MST_CRED_STS_23                                0xFC8C0C

#define mmTPC7_QM_ARB_MST_CRED_STS_24                                0xFC8C10

#define mmTPC7_QM_ARB_MST_CRED_STS_25                                0xFC8C14

#define mmTPC7_QM_ARB_MST_CRED_STS_26                                0xFC8C18

#define mmTPC7_QM_ARB_MST_CRED_STS_27                                0xFC8C1C

#define mmTPC7_QM_ARB_MST_CRED_STS_28                                0xFC8C20

#define mmTPC7_QM_ARB_MST_CRED_STS_29                                0xFC8C24

#define mmTPC7_QM_ARB_MST_CRED_STS_30                                0xFC8C28

#define mmTPC7_QM_ARB_MST_CRED_STS_31                                0xFC8C2C

#define mmTPC7_QM_CGM_CFG                                            0xFC8C70

#define mmTPC7_QM_CGM_STS                                            0xFC8C74

#define mmTPC7_QM_CGM_CFG1                                           0xFC8C78

#define mmTPC7_QM_LOCAL_RANGE_BASE                                   0xFC8C80

#define mmTPC7_QM_LOCAL_RANGE_SIZE                                   0xFC8C84

#define mmTPC7_QM_CSMR_STRICT_PRIO_CFG                               0xFC8C90

#define mmTPC7_QM_HBW_RD_RATE_LIM_CFG_1                              0xFC8C94

#define mmTPC7_QM_LBW_WR_RATE_LIM_CFG_0                              0xFC8C98

#define mmTPC7_QM_LBW_WR_RATE_LIM_CFG_1                              0xFC8C9C

#define mmTPC7_QM_HBW_RD_RATE_LIM_CFG_0                              0xFC8CA0

#define mmTPC7_QM_GLBL_AXCACHE                                       0xFC8CA4

#define mmTPC7_QM_IND_GW_APB_CFG                                     0xFC8CB0

#define mmTPC7_QM_IND_GW_APB_WDATA                                   0xFC8CB4

#define mmTPC7_QM_IND_GW_APB_RDATA                                   0xFC8CB8

#define mmTPC7_QM_IND_GW_APB_STATUS                                  0xFC8CBC

#define mmTPC7_QM_GLBL_ERR_ADDR_LO                                   0xFC8CD0

#define mmTPC7_QM_GLBL_ERR_ADDR_HI                                   0xFC8CD4

#define mmTPC7_QM_GLBL_ERR_WDATA                                     0xFC8CD8

#define mmTPC7_QM_GLBL_MEM_INIT_BUSY                                 0xFC8D00

#endif /* ASIC_REG_TPC7_QM_REGS_H_ */
