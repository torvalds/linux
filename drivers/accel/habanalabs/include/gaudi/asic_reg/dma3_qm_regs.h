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

#ifndef ASIC_REG_DMA3_QM_REGS_H_
#define ASIC_REG_DMA3_QM_REGS_H_

/*
 *****************************************
 *   DMA3_QM (Prototype: QMAN)
 *****************************************
 */

#define mmDMA3_QM_GLBL_CFG0                                          0x568000

#define mmDMA3_QM_GLBL_CFG1                                          0x568004

#define mmDMA3_QM_GLBL_PROT                                          0x568008

#define mmDMA3_QM_GLBL_ERR_CFG                                       0x56800C

#define mmDMA3_QM_GLBL_SECURE_PROPS_0                                0x568010

#define mmDMA3_QM_GLBL_SECURE_PROPS_1                                0x568014

#define mmDMA3_QM_GLBL_SECURE_PROPS_2                                0x568018

#define mmDMA3_QM_GLBL_SECURE_PROPS_3                                0x56801C

#define mmDMA3_QM_GLBL_SECURE_PROPS_4                                0x568020

#define mmDMA3_QM_GLBL_NON_SECURE_PROPS_0                            0x568024

#define mmDMA3_QM_GLBL_NON_SECURE_PROPS_1                            0x568028

#define mmDMA3_QM_GLBL_NON_SECURE_PROPS_2                            0x56802C

#define mmDMA3_QM_GLBL_NON_SECURE_PROPS_3                            0x568030

#define mmDMA3_QM_GLBL_NON_SECURE_PROPS_4                            0x568034

#define mmDMA3_QM_GLBL_STS0                                          0x568038

#define mmDMA3_QM_GLBL_STS1_0                                        0x568040

#define mmDMA3_QM_GLBL_STS1_1                                        0x568044

#define mmDMA3_QM_GLBL_STS1_2                                        0x568048

#define mmDMA3_QM_GLBL_STS1_3                                        0x56804C

#define mmDMA3_QM_GLBL_STS1_4                                        0x568050

#define mmDMA3_QM_GLBL_MSG_EN_0                                      0x568054

#define mmDMA3_QM_GLBL_MSG_EN_1                                      0x568058

#define mmDMA3_QM_GLBL_MSG_EN_2                                      0x56805C

#define mmDMA3_QM_GLBL_MSG_EN_3                                      0x568060

#define mmDMA3_QM_GLBL_MSG_EN_4                                      0x568068

#define mmDMA3_QM_PQ_BASE_LO_0                                       0x568070

#define mmDMA3_QM_PQ_BASE_LO_1                                       0x568074

#define mmDMA3_QM_PQ_BASE_LO_2                                       0x568078

#define mmDMA3_QM_PQ_BASE_LO_3                                       0x56807C

#define mmDMA3_QM_PQ_BASE_HI_0                                       0x568080

#define mmDMA3_QM_PQ_BASE_HI_1                                       0x568084

#define mmDMA3_QM_PQ_BASE_HI_2                                       0x568088

#define mmDMA3_QM_PQ_BASE_HI_3                                       0x56808C

#define mmDMA3_QM_PQ_SIZE_0                                          0x568090

#define mmDMA3_QM_PQ_SIZE_1                                          0x568094

#define mmDMA3_QM_PQ_SIZE_2                                          0x568098

#define mmDMA3_QM_PQ_SIZE_3                                          0x56809C

#define mmDMA3_QM_PQ_PI_0                                            0x5680A0

#define mmDMA3_QM_PQ_PI_1                                            0x5680A4

#define mmDMA3_QM_PQ_PI_2                                            0x5680A8

#define mmDMA3_QM_PQ_PI_3                                            0x5680AC

#define mmDMA3_QM_PQ_CI_0                                            0x5680B0

#define mmDMA3_QM_PQ_CI_1                                            0x5680B4

#define mmDMA3_QM_PQ_CI_2                                            0x5680B8

#define mmDMA3_QM_PQ_CI_3                                            0x5680BC

#define mmDMA3_QM_PQ_CFG0_0                                          0x5680C0

#define mmDMA3_QM_PQ_CFG0_1                                          0x5680C4

#define mmDMA3_QM_PQ_CFG0_2                                          0x5680C8

#define mmDMA3_QM_PQ_CFG0_3                                          0x5680CC

#define mmDMA3_QM_PQ_CFG1_0                                          0x5680D0

#define mmDMA3_QM_PQ_CFG1_1                                          0x5680D4

#define mmDMA3_QM_PQ_CFG1_2                                          0x5680D8

#define mmDMA3_QM_PQ_CFG1_3                                          0x5680DC

#define mmDMA3_QM_PQ_ARUSER_31_11_0                                  0x5680E0

#define mmDMA3_QM_PQ_ARUSER_31_11_1                                  0x5680E4

#define mmDMA3_QM_PQ_ARUSER_31_11_2                                  0x5680E8

#define mmDMA3_QM_PQ_ARUSER_31_11_3                                  0x5680EC

#define mmDMA3_QM_PQ_STS0_0                                          0x5680F0

#define mmDMA3_QM_PQ_STS0_1                                          0x5680F4

#define mmDMA3_QM_PQ_STS0_2                                          0x5680F8

#define mmDMA3_QM_PQ_STS0_3                                          0x5680FC

#define mmDMA3_QM_PQ_STS1_0                                          0x568100

#define mmDMA3_QM_PQ_STS1_1                                          0x568104

#define mmDMA3_QM_PQ_STS1_2                                          0x568108

#define mmDMA3_QM_PQ_STS1_3                                          0x56810C

#define mmDMA3_QM_CQ_CFG0_0                                          0x568110

#define mmDMA3_QM_CQ_CFG0_1                                          0x568114

#define mmDMA3_QM_CQ_CFG0_2                                          0x568118

#define mmDMA3_QM_CQ_CFG0_3                                          0x56811C

#define mmDMA3_QM_CQ_CFG0_4                                          0x568120

#define mmDMA3_QM_CQ_CFG1_0                                          0x568124

#define mmDMA3_QM_CQ_CFG1_1                                          0x568128

#define mmDMA3_QM_CQ_CFG1_2                                          0x56812C

#define mmDMA3_QM_CQ_CFG1_3                                          0x568130

#define mmDMA3_QM_CQ_CFG1_4                                          0x568134

#define mmDMA3_QM_CQ_ARUSER_31_11_0                                  0x568138

#define mmDMA3_QM_CQ_ARUSER_31_11_1                                  0x56813C

#define mmDMA3_QM_CQ_ARUSER_31_11_2                                  0x568140

#define mmDMA3_QM_CQ_ARUSER_31_11_3                                  0x568144

#define mmDMA3_QM_CQ_ARUSER_31_11_4                                  0x568148

#define mmDMA3_QM_CQ_STS0_0                                          0x56814C

#define mmDMA3_QM_CQ_STS0_1                                          0x568150

#define mmDMA3_QM_CQ_STS0_2                                          0x568154

#define mmDMA3_QM_CQ_STS0_3                                          0x568158

#define mmDMA3_QM_CQ_STS0_4                                          0x56815C

#define mmDMA3_QM_CQ_STS1_0                                          0x568160

#define mmDMA3_QM_CQ_STS1_1                                          0x568164

#define mmDMA3_QM_CQ_STS1_2                                          0x568168

#define mmDMA3_QM_CQ_STS1_3                                          0x56816C

#define mmDMA3_QM_CQ_STS1_4                                          0x568170

#define mmDMA3_QM_CQ_PTR_LO_0                                        0x568174

#define mmDMA3_QM_CQ_PTR_HI_0                                        0x568178

#define mmDMA3_QM_CQ_TSIZE_0                                         0x56817C

#define mmDMA3_QM_CQ_CTL_0                                           0x568180

#define mmDMA3_QM_CQ_PTR_LO_1                                        0x568184

#define mmDMA3_QM_CQ_PTR_HI_1                                        0x568188

#define mmDMA3_QM_CQ_TSIZE_1                                         0x56818C

#define mmDMA3_QM_CQ_CTL_1                                           0x568190

#define mmDMA3_QM_CQ_PTR_LO_2                                        0x568194

#define mmDMA3_QM_CQ_PTR_HI_2                                        0x568198

#define mmDMA3_QM_CQ_TSIZE_2                                         0x56819C

#define mmDMA3_QM_CQ_CTL_2                                           0x5681A0

#define mmDMA3_QM_CQ_PTR_LO_3                                        0x5681A4

#define mmDMA3_QM_CQ_PTR_HI_3                                        0x5681A8

#define mmDMA3_QM_CQ_TSIZE_3                                         0x5681AC

#define mmDMA3_QM_CQ_CTL_3                                           0x5681B0

#define mmDMA3_QM_CQ_PTR_LO_4                                        0x5681B4

#define mmDMA3_QM_CQ_PTR_HI_4                                        0x5681B8

#define mmDMA3_QM_CQ_TSIZE_4                                         0x5681BC

#define mmDMA3_QM_CQ_CTL_4                                           0x5681C0

#define mmDMA3_QM_CQ_PTR_LO_STS_0                                    0x5681C4

#define mmDMA3_QM_CQ_PTR_LO_STS_1                                    0x5681C8

#define mmDMA3_QM_CQ_PTR_LO_STS_2                                    0x5681CC

#define mmDMA3_QM_CQ_PTR_LO_STS_3                                    0x5681D0

#define mmDMA3_QM_CQ_PTR_LO_STS_4                                    0x5681D4

#define mmDMA3_QM_CQ_PTR_HI_STS_0                                    0x5681D8

#define mmDMA3_QM_CQ_PTR_HI_STS_1                                    0x5681DC

#define mmDMA3_QM_CQ_PTR_HI_STS_2                                    0x5681E0

#define mmDMA3_QM_CQ_PTR_HI_STS_3                                    0x5681E4

#define mmDMA3_QM_CQ_PTR_HI_STS_4                                    0x5681E8

#define mmDMA3_QM_CQ_TSIZE_STS_0                                     0x5681EC

#define mmDMA3_QM_CQ_TSIZE_STS_1                                     0x5681F0

#define mmDMA3_QM_CQ_TSIZE_STS_2                                     0x5681F4

#define mmDMA3_QM_CQ_TSIZE_STS_3                                     0x5681F8

#define mmDMA3_QM_CQ_TSIZE_STS_4                                     0x5681FC

#define mmDMA3_QM_CQ_CTL_STS_0                                       0x568200

#define mmDMA3_QM_CQ_CTL_STS_1                                       0x568204

#define mmDMA3_QM_CQ_CTL_STS_2                                       0x568208

#define mmDMA3_QM_CQ_CTL_STS_3                                       0x56820C

#define mmDMA3_QM_CQ_CTL_STS_4                                       0x568210

#define mmDMA3_QM_CQ_IFIFO_CNT_0                                     0x568214

#define mmDMA3_QM_CQ_IFIFO_CNT_1                                     0x568218

#define mmDMA3_QM_CQ_IFIFO_CNT_2                                     0x56821C

#define mmDMA3_QM_CQ_IFIFO_CNT_3                                     0x568220

#define mmDMA3_QM_CQ_IFIFO_CNT_4                                     0x568224

#define mmDMA3_QM_CP_MSG_BASE0_ADDR_LO_0                             0x568228

#define mmDMA3_QM_CP_MSG_BASE0_ADDR_LO_1                             0x56822C

#define mmDMA3_QM_CP_MSG_BASE0_ADDR_LO_2                             0x568230

#define mmDMA3_QM_CP_MSG_BASE0_ADDR_LO_3                             0x568234

#define mmDMA3_QM_CP_MSG_BASE0_ADDR_LO_4                             0x568238

#define mmDMA3_QM_CP_MSG_BASE0_ADDR_HI_0                             0x56823C

#define mmDMA3_QM_CP_MSG_BASE0_ADDR_HI_1                             0x568240

#define mmDMA3_QM_CP_MSG_BASE0_ADDR_HI_2                             0x568244

#define mmDMA3_QM_CP_MSG_BASE0_ADDR_HI_3                             0x568248

#define mmDMA3_QM_CP_MSG_BASE0_ADDR_HI_4                             0x56824C

#define mmDMA3_QM_CP_MSG_BASE1_ADDR_LO_0                             0x568250

#define mmDMA3_QM_CP_MSG_BASE1_ADDR_LO_1                             0x568254

#define mmDMA3_QM_CP_MSG_BASE1_ADDR_LO_2                             0x568258

#define mmDMA3_QM_CP_MSG_BASE1_ADDR_LO_3                             0x56825C

#define mmDMA3_QM_CP_MSG_BASE1_ADDR_LO_4                             0x568260

#define mmDMA3_QM_CP_MSG_BASE1_ADDR_HI_0                             0x568264

#define mmDMA3_QM_CP_MSG_BASE1_ADDR_HI_1                             0x568268

#define mmDMA3_QM_CP_MSG_BASE1_ADDR_HI_2                             0x56826C

#define mmDMA3_QM_CP_MSG_BASE1_ADDR_HI_3                             0x568270

#define mmDMA3_QM_CP_MSG_BASE1_ADDR_HI_4                             0x568274

#define mmDMA3_QM_CP_MSG_BASE2_ADDR_LO_0                             0x568278

#define mmDMA3_QM_CP_MSG_BASE2_ADDR_LO_1                             0x56827C

#define mmDMA3_QM_CP_MSG_BASE2_ADDR_LO_2                             0x568280

#define mmDMA3_QM_CP_MSG_BASE2_ADDR_LO_3                             0x568284

#define mmDMA3_QM_CP_MSG_BASE2_ADDR_LO_4                             0x568288

#define mmDMA3_QM_CP_MSG_BASE2_ADDR_HI_0                             0x56828C

#define mmDMA3_QM_CP_MSG_BASE2_ADDR_HI_1                             0x568290

#define mmDMA3_QM_CP_MSG_BASE2_ADDR_HI_2                             0x568294

#define mmDMA3_QM_CP_MSG_BASE2_ADDR_HI_3                             0x568298

#define mmDMA3_QM_CP_MSG_BASE2_ADDR_HI_4                             0x56829C

#define mmDMA3_QM_CP_MSG_BASE3_ADDR_LO_0                             0x5682A0

#define mmDMA3_QM_CP_MSG_BASE3_ADDR_LO_1                             0x5682A4

#define mmDMA3_QM_CP_MSG_BASE3_ADDR_LO_2                             0x5682A8

#define mmDMA3_QM_CP_MSG_BASE3_ADDR_LO_3                             0x5682AC

#define mmDMA3_QM_CP_MSG_BASE3_ADDR_LO_4                             0x5682B0

#define mmDMA3_QM_CP_MSG_BASE3_ADDR_HI_0                             0x5682B4

#define mmDMA3_QM_CP_MSG_BASE3_ADDR_HI_1                             0x5682B8

#define mmDMA3_QM_CP_MSG_BASE3_ADDR_HI_2                             0x5682BC

#define mmDMA3_QM_CP_MSG_BASE3_ADDR_HI_3                             0x5682C0

#define mmDMA3_QM_CP_MSG_BASE3_ADDR_HI_4                             0x5682C4

#define mmDMA3_QM_CP_LDMA_TSIZE_OFFSET_0                             0x5682C8

#define mmDMA3_QM_CP_LDMA_TSIZE_OFFSET_1                             0x5682CC

#define mmDMA3_QM_CP_LDMA_TSIZE_OFFSET_2                             0x5682D0

#define mmDMA3_QM_CP_LDMA_TSIZE_OFFSET_3                             0x5682D4

#define mmDMA3_QM_CP_LDMA_TSIZE_OFFSET_4                             0x5682D8

#define mmDMA3_QM_CP_LDMA_SRC_BASE_LO_OFFSET_0                       0x5682E0

#define mmDMA3_QM_CP_LDMA_SRC_BASE_LO_OFFSET_1                       0x5682E4

#define mmDMA3_QM_CP_LDMA_SRC_BASE_LO_OFFSET_2                       0x5682E8

#define mmDMA3_QM_CP_LDMA_SRC_BASE_LO_OFFSET_3                       0x5682EC

#define mmDMA3_QM_CP_LDMA_SRC_BASE_LO_OFFSET_4                       0x5682F0

#define mmDMA3_QM_CP_LDMA_DST_BASE_LO_OFFSET_0                       0x5682F4

#define mmDMA3_QM_CP_LDMA_DST_BASE_LO_OFFSET_1                       0x5682F8

#define mmDMA3_QM_CP_LDMA_DST_BASE_LO_OFFSET_2                       0x5682FC

#define mmDMA3_QM_CP_LDMA_DST_BASE_LO_OFFSET_3                       0x568300

#define mmDMA3_QM_CP_LDMA_DST_BASE_LO_OFFSET_4                       0x568304

#define mmDMA3_QM_CP_FENCE0_RDATA_0                                  0x568308

#define mmDMA3_QM_CP_FENCE0_RDATA_1                                  0x56830C

#define mmDMA3_QM_CP_FENCE0_RDATA_2                                  0x568310

#define mmDMA3_QM_CP_FENCE0_RDATA_3                                  0x568314

#define mmDMA3_QM_CP_FENCE0_RDATA_4                                  0x568318

#define mmDMA3_QM_CP_FENCE1_RDATA_0                                  0x56831C

#define mmDMA3_QM_CP_FENCE1_RDATA_1                                  0x568320

#define mmDMA3_QM_CP_FENCE1_RDATA_2                                  0x568324

#define mmDMA3_QM_CP_FENCE1_RDATA_3                                  0x568328

#define mmDMA3_QM_CP_FENCE1_RDATA_4                                  0x56832C

#define mmDMA3_QM_CP_FENCE2_RDATA_0                                  0x568330

#define mmDMA3_QM_CP_FENCE2_RDATA_1                                  0x568334

#define mmDMA3_QM_CP_FENCE2_RDATA_2                                  0x568338

#define mmDMA3_QM_CP_FENCE2_RDATA_3                                  0x56833C

#define mmDMA3_QM_CP_FENCE2_RDATA_4                                  0x568340

#define mmDMA3_QM_CP_FENCE3_RDATA_0                                  0x568344

#define mmDMA3_QM_CP_FENCE3_RDATA_1                                  0x568348

#define mmDMA3_QM_CP_FENCE3_RDATA_2                                  0x56834C

#define mmDMA3_QM_CP_FENCE3_RDATA_3                                  0x568350

#define mmDMA3_QM_CP_FENCE3_RDATA_4                                  0x568354

#define mmDMA3_QM_CP_FENCE0_CNT_0                                    0x568358

#define mmDMA3_QM_CP_FENCE0_CNT_1                                    0x56835C

#define mmDMA3_QM_CP_FENCE0_CNT_2                                    0x568360

#define mmDMA3_QM_CP_FENCE0_CNT_3                                    0x568364

#define mmDMA3_QM_CP_FENCE0_CNT_4                                    0x568368

#define mmDMA3_QM_CP_FENCE1_CNT_0                                    0x56836C

#define mmDMA3_QM_CP_FENCE1_CNT_1                                    0x568370

#define mmDMA3_QM_CP_FENCE1_CNT_2                                    0x568374

#define mmDMA3_QM_CP_FENCE1_CNT_3                                    0x568378

#define mmDMA3_QM_CP_FENCE1_CNT_4                                    0x56837C

#define mmDMA3_QM_CP_FENCE2_CNT_0                                    0x568380

#define mmDMA3_QM_CP_FENCE2_CNT_1                                    0x568384

#define mmDMA3_QM_CP_FENCE2_CNT_2                                    0x568388

#define mmDMA3_QM_CP_FENCE2_CNT_3                                    0x56838C

#define mmDMA3_QM_CP_FENCE2_CNT_4                                    0x568390

#define mmDMA3_QM_CP_FENCE3_CNT_0                                    0x568394

#define mmDMA3_QM_CP_FENCE3_CNT_1                                    0x568398

#define mmDMA3_QM_CP_FENCE3_CNT_2                                    0x56839C

#define mmDMA3_QM_CP_FENCE3_CNT_3                                    0x5683A0

#define mmDMA3_QM_CP_FENCE3_CNT_4                                    0x5683A4

#define mmDMA3_QM_CP_STS_0                                           0x5683A8

#define mmDMA3_QM_CP_STS_1                                           0x5683AC

#define mmDMA3_QM_CP_STS_2                                           0x5683B0

#define mmDMA3_QM_CP_STS_3                                           0x5683B4

#define mmDMA3_QM_CP_STS_4                                           0x5683B8

#define mmDMA3_QM_CP_CURRENT_INST_LO_0                               0x5683BC

#define mmDMA3_QM_CP_CURRENT_INST_LO_1                               0x5683C0

#define mmDMA3_QM_CP_CURRENT_INST_LO_2                               0x5683C4

#define mmDMA3_QM_CP_CURRENT_INST_LO_3                               0x5683C8

#define mmDMA3_QM_CP_CURRENT_INST_LO_4                               0x5683CC

#define mmDMA3_QM_CP_CURRENT_INST_HI_0                               0x5683D0

#define mmDMA3_QM_CP_CURRENT_INST_HI_1                               0x5683D4

#define mmDMA3_QM_CP_CURRENT_INST_HI_2                               0x5683D8

#define mmDMA3_QM_CP_CURRENT_INST_HI_3                               0x5683DC

#define mmDMA3_QM_CP_CURRENT_INST_HI_4                               0x5683E0

#define mmDMA3_QM_CP_BARRIER_CFG_0                                   0x5683F4

#define mmDMA3_QM_CP_BARRIER_CFG_1                                   0x5683F8

#define mmDMA3_QM_CP_BARRIER_CFG_2                                   0x5683FC

#define mmDMA3_QM_CP_BARRIER_CFG_3                                   0x568400

#define mmDMA3_QM_CP_BARRIER_CFG_4                                   0x568404

#define mmDMA3_QM_CP_DBG_0_0                                         0x568408

#define mmDMA3_QM_CP_DBG_0_1                                         0x56840C

#define mmDMA3_QM_CP_DBG_0_2                                         0x568410

#define mmDMA3_QM_CP_DBG_0_3                                         0x568414

#define mmDMA3_QM_CP_DBG_0_4                                         0x568418

#define mmDMA3_QM_CP_ARUSER_31_11_0                                  0x56841C

#define mmDMA3_QM_CP_ARUSER_31_11_1                                  0x568420

#define mmDMA3_QM_CP_ARUSER_31_11_2                                  0x568424

#define mmDMA3_QM_CP_ARUSER_31_11_3                                  0x568428

#define mmDMA3_QM_CP_ARUSER_31_11_4                                  0x56842C

#define mmDMA3_QM_CP_AWUSER_31_11_0                                  0x568430

#define mmDMA3_QM_CP_AWUSER_31_11_1                                  0x568434

#define mmDMA3_QM_CP_AWUSER_31_11_2                                  0x568438

#define mmDMA3_QM_CP_AWUSER_31_11_3                                  0x56843C

#define mmDMA3_QM_CP_AWUSER_31_11_4                                  0x568440

#define mmDMA3_QM_ARB_CFG_0                                          0x568A00

#define mmDMA3_QM_ARB_CHOISE_Q_PUSH                                  0x568A04

#define mmDMA3_QM_ARB_WRR_WEIGHT_0                                   0x568A08

#define mmDMA3_QM_ARB_WRR_WEIGHT_1                                   0x568A0C

#define mmDMA3_QM_ARB_WRR_WEIGHT_2                                   0x568A10

#define mmDMA3_QM_ARB_WRR_WEIGHT_3                                   0x568A14

#define mmDMA3_QM_ARB_CFG_1                                          0x568A18

#define mmDMA3_QM_ARB_MST_AVAIL_CRED_0                               0x568A20

#define mmDMA3_QM_ARB_MST_AVAIL_CRED_1                               0x568A24

#define mmDMA3_QM_ARB_MST_AVAIL_CRED_2                               0x568A28

#define mmDMA3_QM_ARB_MST_AVAIL_CRED_3                               0x568A2C

#define mmDMA3_QM_ARB_MST_AVAIL_CRED_4                               0x568A30

#define mmDMA3_QM_ARB_MST_AVAIL_CRED_5                               0x568A34

#define mmDMA3_QM_ARB_MST_AVAIL_CRED_6                               0x568A38

#define mmDMA3_QM_ARB_MST_AVAIL_CRED_7                               0x568A3C

#define mmDMA3_QM_ARB_MST_AVAIL_CRED_8                               0x568A40

#define mmDMA3_QM_ARB_MST_AVAIL_CRED_9                               0x568A44

#define mmDMA3_QM_ARB_MST_AVAIL_CRED_10                              0x568A48

#define mmDMA3_QM_ARB_MST_AVAIL_CRED_11                              0x568A4C

#define mmDMA3_QM_ARB_MST_AVAIL_CRED_12                              0x568A50

#define mmDMA3_QM_ARB_MST_AVAIL_CRED_13                              0x568A54

#define mmDMA3_QM_ARB_MST_AVAIL_CRED_14                              0x568A58

#define mmDMA3_QM_ARB_MST_AVAIL_CRED_15                              0x568A5C

#define mmDMA3_QM_ARB_MST_AVAIL_CRED_16                              0x568A60

#define mmDMA3_QM_ARB_MST_AVAIL_CRED_17                              0x568A64

#define mmDMA3_QM_ARB_MST_AVAIL_CRED_18                              0x568A68

#define mmDMA3_QM_ARB_MST_AVAIL_CRED_19                              0x568A6C

#define mmDMA3_QM_ARB_MST_AVAIL_CRED_20                              0x568A70

#define mmDMA3_QM_ARB_MST_AVAIL_CRED_21                              0x568A74

#define mmDMA3_QM_ARB_MST_AVAIL_CRED_22                              0x568A78

#define mmDMA3_QM_ARB_MST_AVAIL_CRED_23                              0x568A7C

#define mmDMA3_QM_ARB_MST_AVAIL_CRED_24                              0x568A80

#define mmDMA3_QM_ARB_MST_AVAIL_CRED_25                              0x568A84

#define mmDMA3_QM_ARB_MST_AVAIL_CRED_26                              0x568A88

#define mmDMA3_QM_ARB_MST_AVAIL_CRED_27                              0x568A8C

#define mmDMA3_QM_ARB_MST_AVAIL_CRED_28                              0x568A90

#define mmDMA3_QM_ARB_MST_AVAIL_CRED_29                              0x568A94

#define mmDMA3_QM_ARB_MST_AVAIL_CRED_30                              0x568A98

#define mmDMA3_QM_ARB_MST_AVAIL_CRED_31                              0x568A9C

#define mmDMA3_QM_ARB_MST_CRED_INC                                   0x568AA0

#define mmDMA3_QM_ARB_MST_CHOISE_PUSH_OFST_0                         0x568AA4

#define mmDMA3_QM_ARB_MST_CHOISE_PUSH_OFST_1                         0x568AA8

#define mmDMA3_QM_ARB_MST_CHOISE_PUSH_OFST_2                         0x568AAC

#define mmDMA3_QM_ARB_MST_CHOISE_PUSH_OFST_3                         0x568AB0

#define mmDMA3_QM_ARB_MST_CHOISE_PUSH_OFST_4                         0x568AB4

#define mmDMA3_QM_ARB_MST_CHOISE_PUSH_OFST_5                         0x568AB8

#define mmDMA3_QM_ARB_MST_CHOISE_PUSH_OFST_6                         0x568ABC

#define mmDMA3_QM_ARB_MST_CHOISE_PUSH_OFST_7                         0x568AC0

#define mmDMA3_QM_ARB_MST_CHOISE_PUSH_OFST_8                         0x568AC4

#define mmDMA3_QM_ARB_MST_CHOISE_PUSH_OFST_9                         0x568AC8

#define mmDMA3_QM_ARB_MST_CHOISE_PUSH_OFST_10                        0x568ACC

#define mmDMA3_QM_ARB_MST_CHOISE_PUSH_OFST_11                        0x568AD0

#define mmDMA3_QM_ARB_MST_CHOISE_PUSH_OFST_12                        0x568AD4

#define mmDMA3_QM_ARB_MST_CHOISE_PUSH_OFST_13                        0x568AD8

#define mmDMA3_QM_ARB_MST_CHOISE_PUSH_OFST_14                        0x568ADC

#define mmDMA3_QM_ARB_MST_CHOISE_PUSH_OFST_15                        0x568AE0

#define mmDMA3_QM_ARB_MST_CHOISE_PUSH_OFST_16                        0x568AE4

#define mmDMA3_QM_ARB_MST_CHOISE_PUSH_OFST_17                        0x568AE8

#define mmDMA3_QM_ARB_MST_CHOISE_PUSH_OFST_18                        0x568AEC

#define mmDMA3_QM_ARB_MST_CHOISE_PUSH_OFST_19                        0x568AF0

#define mmDMA3_QM_ARB_MST_CHOISE_PUSH_OFST_20                        0x568AF4

#define mmDMA3_QM_ARB_MST_CHOISE_PUSH_OFST_21                        0x568AF8

#define mmDMA3_QM_ARB_MST_CHOISE_PUSH_OFST_22                        0x568AFC

#define mmDMA3_QM_ARB_MST_CHOISE_PUSH_OFST_23                        0x568B00

#define mmDMA3_QM_ARB_MST_CHOISE_PUSH_OFST_24                        0x568B04

#define mmDMA3_QM_ARB_MST_CHOISE_PUSH_OFST_25                        0x568B08

#define mmDMA3_QM_ARB_MST_CHOISE_PUSH_OFST_26                        0x568B0C

#define mmDMA3_QM_ARB_MST_CHOISE_PUSH_OFST_27                        0x568B10

#define mmDMA3_QM_ARB_MST_CHOISE_PUSH_OFST_28                        0x568B14

#define mmDMA3_QM_ARB_MST_CHOISE_PUSH_OFST_29                        0x568B18

#define mmDMA3_QM_ARB_MST_CHOISE_PUSH_OFST_30                        0x568B1C

#define mmDMA3_QM_ARB_MST_CHOISE_PUSH_OFST_31                        0x568B20

#define mmDMA3_QM_ARB_SLV_MASTER_INC_CRED_OFST                       0x568B28

#define mmDMA3_QM_ARB_MST_SLAVE_EN                                   0x568B2C

#define mmDMA3_QM_ARB_MST_QUIET_PER                                  0x568B34

#define mmDMA3_QM_ARB_SLV_CHOISE_WDT                                 0x568B38

#define mmDMA3_QM_ARB_SLV_ID                                         0x568B3C

#define mmDMA3_QM_ARB_MSG_MAX_INFLIGHT                               0x568B44

#define mmDMA3_QM_ARB_MSG_AWUSER_31_11                               0x568B48

#define mmDMA3_QM_ARB_MSG_AWUSER_SEC_PROP                            0x568B4C

#define mmDMA3_QM_ARB_MSG_AWUSER_NON_SEC_PROP                        0x568B50

#define mmDMA3_QM_ARB_BASE_LO                                        0x568B54

#define mmDMA3_QM_ARB_BASE_HI                                        0x568B58

#define mmDMA3_QM_ARB_STATE_STS                                      0x568B80

#define mmDMA3_QM_ARB_CHOISE_FULLNESS_STS                            0x568B84

#define mmDMA3_QM_ARB_MSG_STS                                        0x568B88

#define mmDMA3_QM_ARB_SLV_CHOISE_Q_HEAD                              0x568B8C

#define mmDMA3_QM_ARB_ERR_CAUSE                                      0x568B9C

#define mmDMA3_QM_ARB_ERR_MSG_EN                                     0x568BA0

#define mmDMA3_QM_ARB_ERR_STS_DRP                                    0x568BA8

#define mmDMA3_QM_ARB_MST_CRED_STS_0                                 0x568BB0

#define mmDMA3_QM_ARB_MST_CRED_STS_1                                 0x568BB4

#define mmDMA3_QM_ARB_MST_CRED_STS_2                                 0x568BB8

#define mmDMA3_QM_ARB_MST_CRED_STS_3                                 0x568BBC

#define mmDMA3_QM_ARB_MST_CRED_STS_4                                 0x568BC0

#define mmDMA3_QM_ARB_MST_CRED_STS_5                                 0x568BC4

#define mmDMA3_QM_ARB_MST_CRED_STS_6                                 0x568BC8

#define mmDMA3_QM_ARB_MST_CRED_STS_7                                 0x568BCC

#define mmDMA3_QM_ARB_MST_CRED_STS_8                                 0x568BD0

#define mmDMA3_QM_ARB_MST_CRED_STS_9                                 0x568BD4

#define mmDMA3_QM_ARB_MST_CRED_STS_10                                0x568BD8

#define mmDMA3_QM_ARB_MST_CRED_STS_11                                0x568BDC

#define mmDMA3_QM_ARB_MST_CRED_STS_12                                0x568BE0

#define mmDMA3_QM_ARB_MST_CRED_STS_13                                0x568BE4

#define mmDMA3_QM_ARB_MST_CRED_STS_14                                0x568BE8

#define mmDMA3_QM_ARB_MST_CRED_STS_15                                0x568BEC

#define mmDMA3_QM_ARB_MST_CRED_STS_16                                0x568BF0

#define mmDMA3_QM_ARB_MST_CRED_STS_17                                0x568BF4

#define mmDMA3_QM_ARB_MST_CRED_STS_18                                0x568BF8

#define mmDMA3_QM_ARB_MST_CRED_STS_19                                0x568BFC

#define mmDMA3_QM_ARB_MST_CRED_STS_20                                0x568C00

#define mmDMA3_QM_ARB_MST_CRED_STS_21                                0x568C04

#define mmDMA3_QM_ARB_MST_CRED_STS_22                                0x568C08

#define mmDMA3_QM_ARB_MST_CRED_STS_23                                0x568C0C

#define mmDMA3_QM_ARB_MST_CRED_STS_24                                0x568C10

#define mmDMA3_QM_ARB_MST_CRED_STS_25                                0x568C14

#define mmDMA3_QM_ARB_MST_CRED_STS_26                                0x568C18

#define mmDMA3_QM_ARB_MST_CRED_STS_27                                0x568C1C

#define mmDMA3_QM_ARB_MST_CRED_STS_28                                0x568C20

#define mmDMA3_QM_ARB_MST_CRED_STS_29                                0x568C24

#define mmDMA3_QM_ARB_MST_CRED_STS_30                                0x568C28

#define mmDMA3_QM_ARB_MST_CRED_STS_31                                0x568C2C

#define mmDMA3_QM_CGM_CFG                                            0x568C70

#define mmDMA3_QM_CGM_STS                                            0x568C74

#define mmDMA3_QM_CGM_CFG1                                           0x568C78

#define mmDMA3_QM_LOCAL_RANGE_BASE                                   0x568C80

#define mmDMA3_QM_LOCAL_RANGE_SIZE                                   0x568C84

#define mmDMA3_QM_CSMR_STRICT_PRIO_CFG                               0x568C90

#define mmDMA3_QM_HBW_RD_RATE_LIM_CFG_1                              0x568C94

#define mmDMA3_QM_LBW_WR_RATE_LIM_CFG_0                              0x568C98

#define mmDMA3_QM_LBW_WR_RATE_LIM_CFG_1                              0x568C9C

#define mmDMA3_QM_HBW_RD_RATE_LIM_CFG_0                              0x568CA0

#define mmDMA3_QM_GLBL_AXCACHE                                       0x568CA4

#define mmDMA3_QM_IND_GW_APB_CFG                                     0x568CB0

#define mmDMA3_QM_IND_GW_APB_WDATA                                   0x568CB4

#define mmDMA3_QM_IND_GW_APB_RDATA                                   0x568CB8

#define mmDMA3_QM_IND_GW_APB_STATUS                                  0x568CBC

#define mmDMA3_QM_GLBL_ERR_ADDR_LO                                   0x568CD0

#define mmDMA3_QM_GLBL_ERR_ADDR_HI                                   0x568CD4

#define mmDMA3_QM_GLBL_ERR_WDATA                                     0x568CD8

#define mmDMA3_QM_GLBL_MEM_INIT_BUSY                                 0x568D00

#endif /* ASIC_REG_DMA3_QM_REGS_H_ */
