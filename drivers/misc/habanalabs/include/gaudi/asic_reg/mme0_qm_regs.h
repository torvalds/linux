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

#ifndef ASIC_REG_MME0_QM_REGS_H_
#define ASIC_REG_MME0_QM_REGS_H_

/*
 *****************************************
 *   MME0_QM (Prototype: QMAN)
 *****************************************
 */

#define mmMME0_QM_GLBL_CFG0                                          0x68000

#define mmMME0_QM_GLBL_CFG1                                          0x68004

#define mmMME0_QM_GLBL_PROT                                          0x68008

#define mmMME0_QM_GLBL_ERR_CFG                                       0x6800C

#define mmMME0_QM_GLBL_SECURE_PROPS_0                                0x68010

#define mmMME0_QM_GLBL_SECURE_PROPS_1                                0x68014

#define mmMME0_QM_GLBL_SECURE_PROPS_2                                0x68018

#define mmMME0_QM_GLBL_SECURE_PROPS_3                                0x6801C

#define mmMME0_QM_GLBL_SECURE_PROPS_4                                0x68020

#define mmMME0_QM_GLBL_NON_SECURE_PROPS_0                            0x68024

#define mmMME0_QM_GLBL_NON_SECURE_PROPS_1                            0x68028

#define mmMME0_QM_GLBL_NON_SECURE_PROPS_2                            0x6802C

#define mmMME0_QM_GLBL_NON_SECURE_PROPS_3                            0x68030

#define mmMME0_QM_GLBL_NON_SECURE_PROPS_4                            0x68034

#define mmMME0_QM_GLBL_STS0                                          0x68038

#define mmMME0_QM_GLBL_STS1_0                                        0x68040

#define mmMME0_QM_GLBL_STS1_1                                        0x68044

#define mmMME0_QM_GLBL_STS1_2                                        0x68048

#define mmMME0_QM_GLBL_STS1_3                                        0x6804C

#define mmMME0_QM_GLBL_STS1_4                                        0x68050

#define mmMME0_QM_GLBL_MSG_EN_0                                      0x68054

#define mmMME0_QM_GLBL_MSG_EN_1                                      0x68058

#define mmMME0_QM_GLBL_MSG_EN_2                                      0x6805C

#define mmMME0_QM_GLBL_MSG_EN_3                                      0x68060

#define mmMME0_QM_GLBL_MSG_EN_4                                      0x68068

#define mmMME0_QM_PQ_BASE_LO_0                                       0x68070

#define mmMME0_QM_PQ_BASE_LO_1                                       0x68074

#define mmMME0_QM_PQ_BASE_LO_2                                       0x68078

#define mmMME0_QM_PQ_BASE_LO_3                                       0x6807C

#define mmMME0_QM_PQ_BASE_HI_0                                       0x68080

#define mmMME0_QM_PQ_BASE_HI_1                                       0x68084

#define mmMME0_QM_PQ_BASE_HI_2                                       0x68088

#define mmMME0_QM_PQ_BASE_HI_3                                       0x6808C

#define mmMME0_QM_PQ_SIZE_0                                          0x68090

#define mmMME0_QM_PQ_SIZE_1                                          0x68094

#define mmMME0_QM_PQ_SIZE_2                                          0x68098

#define mmMME0_QM_PQ_SIZE_3                                          0x6809C

#define mmMME0_QM_PQ_PI_0                                            0x680A0

#define mmMME0_QM_PQ_PI_1                                            0x680A4

#define mmMME0_QM_PQ_PI_2                                            0x680A8

#define mmMME0_QM_PQ_PI_3                                            0x680AC

#define mmMME0_QM_PQ_CI_0                                            0x680B0

#define mmMME0_QM_PQ_CI_1                                            0x680B4

#define mmMME0_QM_PQ_CI_2                                            0x680B8

#define mmMME0_QM_PQ_CI_3                                            0x680BC

#define mmMME0_QM_PQ_CFG0_0                                          0x680C0

#define mmMME0_QM_PQ_CFG0_1                                          0x680C4

#define mmMME0_QM_PQ_CFG0_2                                          0x680C8

#define mmMME0_QM_PQ_CFG0_3                                          0x680CC

#define mmMME0_QM_PQ_CFG1_0                                          0x680D0

#define mmMME0_QM_PQ_CFG1_1                                          0x680D4

#define mmMME0_QM_PQ_CFG1_2                                          0x680D8

#define mmMME0_QM_PQ_CFG1_3                                          0x680DC

#define mmMME0_QM_PQ_ARUSER_31_11_0                                  0x680E0

#define mmMME0_QM_PQ_ARUSER_31_11_1                                  0x680E4

#define mmMME0_QM_PQ_ARUSER_31_11_2                                  0x680E8

#define mmMME0_QM_PQ_ARUSER_31_11_3                                  0x680EC

#define mmMME0_QM_PQ_STS0_0                                          0x680F0

#define mmMME0_QM_PQ_STS0_1                                          0x680F4

#define mmMME0_QM_PQ_STS0_2                                          0x680F8

#define mmMME0_QM_PQ_STS0_3                                          0x680FC

#define mmMME0_QM_PQ_STS1_0                                          0x68100

#define mmMME0_QM_PQ_STS1_1                                          0x68104

#define mmMME0_QM_PQ_STS1_2                                          0x68108

#define mmMME0_QM_PQ_STS1_3                                          0x6810C

#define mmMME0_QM_CQ_CFG0_0                                          0x68110

#define mmMME0_QM_CQ_CFG0_1                                          0x68114

#define mmMME0_QM_CQ_CFG0_2                                          0x68118

#define mmMME0_QM_CQ_CFG0_3                                          0x6811C

#define mmMME0_QM_CQ_CFG0_4                                          0x68120

#define mmMME0_QM_CQ_CFG1_0                                          0x68124

#define mmMME0_QM_CQ_CFG1_1                                          0x68128

#define mmMME0_QM_CQ_CFG1_2                                          0x6812C

#define mmMME0_QM_CQ_CFG1_3                                          0x68130

#define mmMME0_QM_CQ_CFG1_4                                          0x68134

#define mmMME0_QM_CQ_ARUSER_31_11_0                                  0x68138

#define mmMME0_QM_CQ_ARUSER_31_11_1                                  0x6813C

#define mmMME0_QM_CQ_ARUSER_31_11_2                                  0x68140

#define mmMME0_QM_CQ_ARUSER_31_11_3                                  0x68144

#define mmMME0_QM_CQ_ARUSER_31_11_4                                  0x68148

#define mmMME0_QM_CQ_STS0_0                                          0x6814C

#define mmMME0_QM_CQ_STS0_1                                          0x68150

#define mmMME0_QM_CQ_STS0_2                                          0x68154

#define mmMME0_QM_CQ_STS0_3                                          0x68158

#define mmMME0_QM_CQ_STS0_4                                          0x6815C

#define mmMME0_QM_CQ_STS1_0                                          0x68160

#define mmMME0_QM_CQ_STS1_1                                          0x68164

#define mmMME0_QM_CQ_STS1_2                                          0x68168

#define mmMME0_QM_CQ_STS1_3                                          0x6816C

#define mmMME0_QM_CQ_STS1_4                                          0x68170

#define mmMME0_QM_CQ_PTR_LO_0                                        0x68174

#define mmMME0_QM_CQ_PTR_HI_0                                        0x68178

#define mmMME0_QM_CQ_TSIZE_0                                         0x6817C

#define mmMME0_QM_CQ_CTL_0                                           0x68180

#define mmMME0_QM_CQ_PTR_LO_1                                        0x68184

#define mmMME0_QM_CQ_PTR_HI_1                                        0x68188

#define mmMME0_QM_CQ_TSIZE_1                                         0x6818C

#define mmMME0_QM_CQ_CTL_1                                           0x68190

#define mmMME0_QM_CQ_PTR_LO_2                                        0x68194

#define mmMME0_QM_CQ_PTR_HI_2                                        0x68198

#define mmMME0_QM_CQ_TSIZE_2                                         0x6819C

#define mmMME0_QM_CQ_CTL_2                                           0x681A0

#define mmMME0_QM_CQ_PTR_LO_3                                        0x681A4

#define mmMME0_QM_CQ_PTR_HI_3                                        0x681A8

#define mmMME0_QM_CQ_TSIZE_3                                         0x681AC

#define mmMME0_QM_CQ_CTL_3                                           0x681B0

#define mmMME0_QM_CQ_PTR_LO_4                                        0x681B4

#define mmMME0_QM_CQ_PTR_HI_4                                        0x681B8

#define mmMME0_QM_CQ_TSIZE_4                                         0x681BC

#define mmMME0_QM_CQ_CTL_4                                           0x681C0

#define mmMME0_QM_CQ_PTR_LO_STS_0                                    0x681C4

#define mmMME0_QM_CQ_PTR_LO_STS_1                                    0x681C8

#define mmMME0_QM_CQ_PTR_LO_STS_2                                    0x681CC

#define mmMME0_QM_CQ_PTR_LO_STS_3                                    0x681D0

#define mmMME0_QM_CQ_PTR_LO_STS_4                                    0x681D4

#define mmMME0_QM_CQ_PTR_HI_STS_0                                    0x681D8

#define mmMME0_QM_CQ_PTR_HI_STS_1                                    0x681DC

#define mmMME0_QM_CQ_PTR_HI_STS_2                                    0x681E0

#define mmMME0_QM_CQ_PTR_HI_STS_3                                    0x681E4

#define mmMME0_QM_CQ_PTR_HI_STS_4                                    0x681E8

#define mmMME0_QM_CQ_TSIZE_STS_0                                     0x681EC

#define mmMME0_QM_CQ_TSIZE_STS_1                                     0x681F0

#define mmMME0_QM_CQ_TSIZE_STS_2                                     0x681F4

#define mmMME0_QM_CQ_TSIZE_STS_3                                     0x681F8

#define mmMME0_QM_CQ_TSIZE_STS_4                                     0x681FC

#define mmMME0_QM_CQ_CTL_STS_0                                       0x68200

#define mmMME0_QM_CQ_CTL_STS_1                                       0x68204

#define mmMME0_QM_CQ_CTL_STS_2                                       0x68208

#define mmMME0_QM_CQ_CTL_STS_3                                       0x6820C

#define mmMME0_QM_CQ_CTL_STS_4                                       0x68210

#define mmMME0_QM_CQ_IFIFO_CNT_0                                     0x68214

#define mmMME0_QM_CQ_IFIFO_CNT_1                                     0x68218

#define mmMME0_QM_CQ_IFIFO_CNT_2                                     0x6821C

#define mmMME0_QM_CQ_IFIFO_CNT_3                                     0x68220

#define mmMME0_QM_CQ_IFIFO_CNT_4                                     0x68224

#define mmMME0_QM_CP_MSG_BASE0_ADDR_LO_0                             0x68228

#define mmMME0_QM_CP_MSG_BASE0_ADDR_LO_1                             0x6822C

#define mmMME0_QM_CP_MSG_BASE0_ADDR_LO_2                             0x68230

#define mmMME0_QM_CP_MSG_BASE0_ADDR_LO_3                             0x68234

#define mmMME0_QM_CP_MSG_BASE0_ADDR_LO_4                             0x68238

#define mmMME0_QM_CP_MSG_BASE0_ADDR_HI_0                             0x6823C

#define mmMME0_QM_CP_MSG_BASE0_ADDR_HI_1                             0x68240

#define mmMME0_QM_CP_MSG_BASE0_ADDR_HI_2                             0x68244

#define mmMME0_QM_CP_MSG_BASE0_ADDR_HI_3                             0x68248

#define mmMME0_QM_CP_MSG_BASE0_ADDR_HI_4                             0x6824C

#define mmMME0_QM_CP_MSG_BASE1_ADDR_LO_0                             0x68250

#define mmMME0_QM_CP_MSG_BASE1_ADDR_LO_1                             0x68254

#define mmMME0_QM_CP_MSG_BASE1_ADDR_LO_2                             0x68258

#define mmMME0_QM_CP_MSG_BASE1_ADDR_LO_3                             0x6825C

#define mmMME0_QM_CP_MSG_BASE1_ADDR_LO_4                             0x68260

#define mmMME0_QM_CP_MSG_BASE1_ADDR_HI_0                             0x68264

#define mmMME0_QM_CP_MSG_BASE1_ADDR_HI_1                             0x68268

#define mmMME0_QM_CP_MSG_BASE1_ADDR_HI_2                             0x6826C

#define mmMME0_QM_CP_MSG_BASE1_ADDR_HI_3                             0x68270

#define mmMME0_QM_CP_MSG_BASE1_ADDR_HI_4                             0x68274

#define mmMME0_QM_CP_MSG_BASE2_ADDR_LO_0                             0x68278

#define mmMME0_QM_CP_MSG_BASE2_ADDR_LO_1                             0x6827C

#define mmMME0_QM_CP_MSG_BASE2_ADDR_LO_2                             0x68280

#define mmMME0_QM_CP_MSG_BASE2_ADDR_LO_3                             0x68284

#define mmMME0_QM_CP_MSG_BASE2_ADDR_LO_4                             0x68288

#define mmMME0_QM_CP_MSG_BASE2_ADDR_HI_0                             0x6828C

#define mmMME0_QM_CP_MSG_BASE2_ADDR_HI_1                             0x68290

#define mmMME0_QM_CP_MSG_BASE2_ADDR_HI_2                             0x68294

#define mmMME0_QM_CP_MSG_BASE2_ADDR_HI_3                             0x68298

#define mmMME0_QM_CP_MSG_BASE2_ADDR_HI_4                             0x6829C

#define mmMME0_QM_CP_MSG_BASE3_ADDR_LO_0                             0x682A0

#define mmMME0_QM_CP_MSG_BASE3_ADDR_LO_1                             0x682A4

#define mmMME0_QM_CP_MSG_BASE3_ADDR_LO_2                             0x682A8

#define mmMME0_QM_CP_MSG_BASE3_ADDR_LO_3                             0x682AC

#define mmMME0_QM_CP_MSG_BASE3_ADDR_LO_4                             0x682B0

#define mmMME0_QM_CP_MSG_BASE3_ADDR_HI_0                             0x682B4

#define mmMME0_QM_CP_MSG_BASE3_ADDR_HI_1                             0x682B8

#define mmMME0_QM_CP_MSG_BASE3_ADDR_HI_2                             0x682BC

#define mmMME0_QM_CP_MSG_BASE3_ADDR_HI_3                             0x682C0

#define mmMME0_QM_CP_MSG_BASE3_ADDR_HI_4                             0x682C4

#define mmMME0_QM_CP_LDMA_TSIZE_OFFSET_0                             0x682C8

#define mmMME0_QM_CP_LDMA_TSIZE_OFFSET_1                             0x682CC

#define mmMME0_QM_CP_LDMA_TSIZE_OFFSET_2                             0x682D0

#define mmMME0_QM_CP_LDMA_TSIZE_OFFSET_3                             0x682D4

#define mmMME0_QM_CP_LDMA_TSIZE_OFFSET_4                             0x682D8

#define mmMME0_QM_CP_LDMA_SRC_BASE_LO_OFFSET_0                       0x682E0

#define mmMME0_QM_CP_LDMA_SRC_BASE_LO_OFFSET_1                       0x682E4

#define mmMME0_QM_CP_LDMA_SRC_BASE_LO_OFFSET_2                       0x682E8

#define mmMME0_QM_CP_LDMA_SRC_BASE_LO_OFFSET_3                       0x682EC

#define mmMME0_QM_CP_LDMA_SRC_BASE_LO_OFFSET_4                       0x682F0

#define mmMME0_QM_CP_LDMA_DST_BASE_LO_OFFSET_0                       0x682F4

#define mmMME0_QM_CP_LDMA_DST_BASE_LO_OFFSET_1                       0x682F8

#define mmMME0_QM_CP_LDMA_DST_BASE_LO_OFFSET_2                       0x682FC

#define mmMME0_QM_CP_LDMA_DST_BASE_LO_OFFSET_3                       0x68300

#define mmMME0_QM_CP_LDMA_DST_BASE_LO_OFFSET_4                       0x68304

#define mmMME0_QM_CP_FENCE0_RDATA_0                                  0x68308

#define mmMME0_QM_CP_FENCE0_RDATA_1                                  0x6830C

#define mmMME0_QM_CP_FENCE0_RDATA_2                                  0x68310

#define mmMME0_QM_CP_FENCE0_RDATA_3                                  0x68314

#define mmMME0_QM_CP_FENCE0_RDATA_4                                  0x68318

#define mmMME0_QM_CP_FENCE1_RDATA_0                                  0x6831C

#define mmMME0_QM_CP_FENCE1_RDATA_1                                  0x68320

#define mmMME0_QM_CP_FENCE1_RDATA_2                                  0x68324

#define mmMME0_QM_CP_FENCE1_RDATA_3                                  0x68328

#define mmMME0_QM_CP_FENCE1_RDATA_4                                  0x6832C

#define mmMME0_QM_CP_FENCE2_RDATA_0                                  0x68330

#define mmMME0_QM_CP_FENCE2_RDATA_1                                  0x68334

#define mmMME0_QM_CP_FENCE2_RDATA_2                                  0x68338

#define mmMME0_QM_CP_FENCE2_RDATA_3                                  0x6833C

#define mmMME0_QM_CP_FENCE2_RDATA_4                                  0x68340

#define mmMME0_QM_CP_FENCE3_RDATA_0                                  0x68344

#define mmMME0_QM_CP_FENCE3_RDATA_1                                  0x68348

#define mmMME0_QM_CP_FENCE3_RDATA_2                                  0x6834C

#define mmMME0_QM_CP_FENCE3_RDATA_3                                  0x68350

#define mmMME0_QM_CP_FENCE3_RDATA_4                                  0x68354

#define mmMME0_QM_CP_FENCE0_CNT_0                                    0x68358

#define mmMME0_QM_CP_FENCE0_CNT_1                                    0x6835C

#define mmMME0_QM_CP_FENCE0_CNT_2                                    0x68360

#define mmMME0_QM_CP_FENCE0_CNT_3                                    0x68364

#define mmMME0_QM_CP_FENCE0_CNT_4                                    0x68368

#define mmMME0_QM_CP_FENCE1_CNT_0                                    0x6836C

#define mmMME0_QM_CP_FENCE1_CNT_1                                    0x68370

#define mmMME0_QM_CP_FENCE1_CNT_2                                    0x68374

#define mmMME0_QM_CP_FENCE1_CNT_3                                    0x68378

#define mmMME0_QM_CP_FENCE1_CNT_4                                    0x6837C

#define mmMME0_QM_CP_FENCE2_CNT_0                                    0x68380

#define mmMME0_QM_CP_FENCE2_CNT_1                                    0x68384

#define mmMME0_QM_CP_FENCE2_CNT_2                                    0x68388

#define mmMME0_QM_CP_FENCE2_CNT_3                                    0x6838C

#define mmMME0_QM_CP_FENCE2_CNT_4                                    0x68390

#define mmMME0_QM_CP_FENCE3_CNT_0                                    0x68394

#define mmMME0_QM_CP_FENCE3_CNT_1                                    0x68398

#define mmMME0_QM_CP_FENCE3_CNT_2                                    0x6839C

#define mmMME0_QM_CP_FENCE3_CNT_3                                    0x683A0

#define mmMME0_QM_CP_FENCE3_CNT_4                                    0x683A4

#define mmMME0_QM_CP_STS_0                                           0x683A8

#define mmMME0_QM_CP_STS_1                                           0x683AC

#define mmMME0_QM_CP_STS_2                                           0x683B0

#define mmMME0_QM_CP_STS_3                                           0x683B4

#define mmMME0_QM_CP_STS_4                                           0x683B8

#define mmMME0_QM_CP_CURRENT_INST_LO_0                               0x683BC

#define mmMME0_QM_CP_CURRENT_INST_LO_1                               0x683C0

#define mmMME0_QM_CP_CURRENT_INST_LO_2                               0x683C4

#define mmMME0_QM_CP_CURRENT_INST_LO_3                               0x683C8

#define mmMME0_QM_CP_CURRENT_INST_LO_4                               0x683CC

#define mmMME0_QM_CP_CURRENT_INST_HI_0                               0x683D0

#define mmMME0_QM_CP_CURRENT_INST_HI_1                               0x683D4

#define mmMME0_QM_CP_CURRENT_INST_HI_2                               0x683D8

#define mmMME0_QM_CP_CURRENT_INST_HI_3                               0x683DC

#define mmMME0_QM_CP_CURRENT_INST_HI_4                               0x683E0

#define mmMME0_QM_CP_BARRIER_CFG_0                                   0x683F4

#define mmMME0_QM_CP_BARRIER_CFG_1                                   0x683F8

#define mmMME0_QM_CP_BARRIER_CFG_2                                   0x683FC

#define mmMME0_QM_CP_BARRIER_CFG_3                                   0x68400

#define mmMME0_QM_CP_BARRIER_CFG_4                                   0x68404

#define mmMME0_QM_CP_DBG_0_0                                         0x68408

#define mmMME0_QM_CP_DBG_0_1                                         0x6840C

#define mmMME0_QM_CP_DBG_0_2                                         0x68410

#define mmMME0_QM_CP_DBG_0_3                                         0x68414

#define mmMME0_QM_CP_DBG_0_4                                         0x68418

#define mmMME0_QM_CP_ARUSER_31_11_0                                  0x6841C

#define mmMME0_QM_CP_ARUSER_31_11_1                                  0x68420

#define mmMME0_QM_CP_ARUSER_31_11_2                                  0x68424

#define mmMME0_QM_CP_ARUSER_31_11_3                                  0x68428

#define mmMME0_QM_CP_ARUSER_31_11_4                                  0x6842C

#define mmMME0_QM_CP_AWUSER_31_11_0                                  0x68430

#define mmMME0_QM_CP_AWUSER_31_11_1                                  0x68434

#define mmMME0_QM_CP_AWUSER_31_11_2                                  0x68438

#define mmMME0_QM_CP_AWUSER_31_11_3                                  0x6843C

#define mmMME0_QM_CP_AWUSER_31_11_4                                  0x68440

#define mmMME0_QM_ARB_CFG_0                                          0x68A00

#define mmMME0_QM_ARB_CHOISE_Q_PUSH                                  0x68A04

#define mmMME0_QM_ARB_WRR_WEIGHT_0                                   0x68A08

#define mmMME0_QM_ARB_WRR_WEIGHT_1                                   0x68A0C

#define mmMME0_QM_ARB_WRR_WEIGHT_2                                   0x68A10

#define mmMME0_QM_ARB_WRR_WEIGHT_3                                   0x68A14

#define mmMME0_QM_ARB_CFG_1                                          0x68A18

#define mmMME0_QM_ARB_MST_AVAIL_CRED_0                               0x68A20

#define mmMME0_QM_ARB_MST_AVAIL_CRED_1                               0x68A24

#define mmMME0_QM_ARB_MST_AVAIL_CRED_2                               0x68A28

#define mmMME0_QM_ARB_MST_AVAIL_CRED_3                               0x68A2C

#define mmMME0_QM_ARB_MST_AVAIL_CRED_4                               0x68A30

#define mmMME0_QM_ARB_MST_AVAIL_CRED_5                               0x68A34

#define mmMME0_QM_ARB_MST_AVAIL_CRED_6                               0x68A38

#define mmMME0_QM_ARB_MST_AVAIL_CRED_7                               0x68A3C

#define mmMME0_QM_ARB_MST_AVAIL_CRED_8                               0x68A40

#define mmMME0_QM_ARB_MST_AVAIL_CRED_9                               0x68A44

#define mmMME0_QM_ARB_MST_AVAIL_CRED_10                              0x68A48

#define mmMME0_QM_ARB_MST_AVAIL_CRED_11                              0x68A4C

#define mmMME0_QM_ARB_MST_AVAIL_CRED_12                              0x68A50

#define mmMME0_QM_ARB_MST_AVAIL_CRED_13                              0x68A54

#define mmMME0_QM_ARB_MST_AVAIL_CRED_14                              0x68A58

#define mmMME0_QM_ARB_MST_AVAIL_CRED_15                              0x68A5C

#define mmMME0_QM_ARB_MST_AVAIL_CRED_16                              0x68A60

#define mmMME0_QM_ARB_MST_AVAIL_CRED_17                              0x68A64

#define mmMME0_QM_ARB_MST_AVAIL_CRED_18                              0x68A68

#define mmMME0_QM_ARB_MST_AVAIL_CRED_19                              0x68A6C

#define mmMME0_QM_ARB_MST_AVAIL_CRED_20                              0x68A70

#define mmMME0_QM_ARB_MST_AVAIL_CRED_21                              0x68A74

#define mmMME0_QM_ARB_MST_AVAIL_CRED_22                              0x68A78

#define mmMME0_QM_ARB_MST_AVAIL_CRED_23                              0x68A7C

#define mmMME0_QM_ARB_MST_AVAIL_CRED_24                              0x68A80

#define mmMME0_QM_ARB_MST_AVAIL_CRED_25                              0x68A84

#define mmMME0_QM_ARB_MST_AVAIL_CRED_26                              0x68A88

#define mmMME0_QM_ARB_MST_AVAIL_CRED_27                              0x68A8C

#define mmMME0_QM_ARB_MST_AVAIL_CRED_28                              0x68A90

#define mmMME0_QM_ARB_MST_AVAIL_CRED_29                              0x68A94

#define mmMME0_QM_ARB_MST_AVAIL_CRED_30                              0x68A98

#define mmMME0_QM_ARB_MST_AVAIL_CRED_31                              0x68A9C

#define mmMME0_QM_ARB_MST_CRED_INC                                   0x68AA0

#define mmMME0_QM_ARB_MST_CHOISE_PUSH_OFST_0                         0x68AA4

#define mmMME0_QM_ARB_MST_CHOISE_PUSH_OFST_1                         0x68AA8

#define mmMME0_QM_ARB_MST_CHOISE_PUSH_OFST_2                         0x68AAC

#define mmMME0_QM_ARB_MST_CHOISE_PUSH_OFST_3                         0x68AB0

#define mmMME0_QM_ARB_MST_CHOISE_PUSH_OFST_4                         0x68AB4

#define mmMME0_QM_ARB_MST_CHOISE_PUSH_OFST_5                         0x68AB8

#define mmMME0_QM_ARB_MST_CHOISE_PUSH_OFST_6                         0x68ABC

#define mmMME0_QM_ARB_MST_CHOISE_PUSH_OFST_7                         0x68AC0

#define mmMME0_QM_ARB_MST_CHOISE_PUSH_OFST_8                         0x68AC4

#define mmMME0_QM_ARB_MST_CHOISE_PUSH_OFST_9                         0x68AC8

#define mmMME0_QM_ARB_MST_CHOISE_PUSH_OFST_10                        0x68ACC

#define mmMME0_QM_ARB_MST_CHOISE_PUSH_OFST_11                        0x68AD0

#define mmMME0_QM_ARB_MST_CHOISE_PUSH_OFST_12                        0x68AD4

#define mmMME0_QM_ARB_MST_CHOISE_PUSH_OFST_13                        0x68AD8

#define mmMME0_QM_ARB_MST_CHOISE_PUSH_OFST_14                        0x68ADC

#define mmMME0_QM_ARB_MST_CHOISE_PUSH_OFST_15                        0x68AE0

#define mmMME0_QM_ARB_MST_CHOISE_PUSH_OFST_16                        0x68AE4

#define mmMME0_QM_ARB_MST_CHOISE_PUSH_OFST_17                        0x68AE8

#define mmMME0_QM_ARB_MST_CHOISE_PUSH_OFST_18                        0x68AEC

#define mmMME0_QM_ARB_MST_CHOISE_PUSH_OFST_19                        0x68AF0

#define mmMME0_QM_ARB_MST_CHOISE_PUSH_OFST_20                        0x68AF4

#define mmMME0_QM_ARB_MST_CHOISE_PUSH_OFST_21                        0x68AF8

#define mmMME0_QM_ARB_MST_CHOISE_PUSH_OFST_22                        0x68AFC

#define mmMME0_QM_ARB_MST_CHOISE_PUSH_OFST_23                        0x68B00

#define mmMME0_QM_ARB_MST_CHOISE_PUSH_OFST_24                        0x68B04

#define mmMME0_QM_ARB_MST_CHOISE_PUSH_OFST_25                        0x68B08

#define mmMME0_QM_ARB_MST_CHOISE_PUSH_OFST_26                        0x68B0C

#define mmMME0_QM_ARB_MST_CHOISE_PUSH_OFST_27                        0x68B10

#define mmMME0_QM_ARB_MST_CHOISE_PUSH_OFST_28                        0x68B14

#define mmMME0_QM_ARB_MST_CHOISE_PUSH_OFST_29                        0x68B18

#define mmMME0_QM_ARB_MST_CHOISE_PUSH_OFST_30                        0x68B1C

#define mmMME0_QM_ARB_MST_CHOISE_PUSH_OFST_31                        0x68B20

#define mmMME0_QM_ARB_SLV_MASTER_INC_CRED_OFST                       0x68B28

#define mmMME0_QM_ARB_MST_SLAVE_EN                                   0x68B2C

#define mmMME0_QM_ARB_MST_QUIET_PER                                  0x68B34

#define mmMME0_QM_ARB_SLV_CHOISE_WDT                                 0x68B38

#define mmMME0_QM_ARB_SLV_ID                                         0x68B3C

#define mmMME0_QM_ARB_MSG_MAX_INFLIGHT                               0x68B44

#define mmMME0_QM_ARB_MSG_AWUSER_31_11                               0x68B48

#define mmMME0_QM_ARB_MSG_AWUSER_SEC_PROP                            0x68B4C

#define mmMME0_QM_ARB_MSG_AWUSER_NON_SEC_PROP                        0x68B50

#define mmMME0_QM_ARB_BASE_LO                                        0x68B54

#define mmMME0_QM_ARB_BASE_HI                                        0x68B58

#define mmMME0_QM_ARB_STATE_STS                                      0x68B80

#define mmMME0_QM_ARB_CHOISE_FULLNESS_STS                            0x68B84

#define mmMME0_QM_ARB_MSG_STS                                        0x68B88

#define mmMME0_QM_ARB_SLV_CHOISE_Q_HEAD                              0x68B8C

#define mmMME0_QM_ARB_ERR_CAUSE                                      0x68B9C

#define mmMME0_QM_ARB_ERR_MSG_EN                                     0x68BA0

#define mmMME0_QM_ARB_ERR_STS_DRP                                    0x68BA8

#define mmMME0_QM_ARB_MST_CRED_STS_0                                 0x68BB0

#define mmMME0_QM_ARB_MST_CRED_STS_1                                 0x68BB4

#define mmMME0_QM_ARB_MST_CRED_STS_2                                 0x68BB8

#define mmMME0_QM_ARB_MST_CRED_STS_3                                 0x68BBC

#define mmMME0_QM_ARB_MST_CRED_STS_4                                 0x68BC0

#define mmMME0_QM_ARB_MST_CRED_STS_5                                 0x68BC4

#define mmMME0_QM_ARB_MST_CRED_STS_6                                 0x68BC8

#define mmMME0_QM_ARB_MST_CRED_STS_7                                 0x68BCC

#define mmMME0_QM_ARB_MST_CRED_STS_8                                 0x68BD0

#define mmMME0_QM_ARB_MST_CRED_STS_9                                 0x68BD4

#define mmMME0_QM_ARB_MST_CRED_STS_10                                0x68BD8

#define mmMME0_QM_ARB_MST_CRED_STS_11                                0x68BDC

#define mmMME0_QM_ARB_MST_CRED_STS_12                                0x68BE0

#define mmMME0_QM_ARB_MST_CRED_STS_13                                0x68BE4

#define mmMME0_QM_ARB_MST_CRED_STS_14                                0x68BE8

#define mmMME0_QM_ARB_MST_CRED_STS_15                                0x68BEC

#define mmMME0_QM_ARB_MST_CRED_STS_16                                0x68BF0

#define mmMME0_QM_ARB_MST_CRED_STS_17                                0x68BF4

#define mmMME0_QM_ARB_MST_CRED_STS_18                                0x68BF8

#define mmMME0_QM_ARB_MST_CRED_STS_19                                0x68BFC

#define mmMME0_QM_ARB_MST_CRED_STS_20                                0x68C00

#define mmMME0_QM_ARB_MST_CRED_STS_21                                0x68C04

#define mmMME0_QM_ARB_MST_CRED_STS_22                                0x68C08

#define mmMME0_QM_ARB_MST_CRED_STS_23                                0x68C0C

#define mmMME0_QM_ARB_MST_CRED_STS_24                                0x68C10

#define mmMME0_QM_ARB_MST_CRED_STS_25                                0x68C14

#define mmMME0_QM_ARB_MST_CRED_STS_26                                0x68C18

#define mmMME0_QM_ARB_MST_CRED_STS_27                                0x68C1C

#define mmMME0_QM_ARB_MST_CRED_STS_28                                0x68C20

#define mmMME0_QM_ARB_MST_CRED_STS_29                                0x68C24

#define mmMME0_QM_ARB_MST_CRED_STS_30                                0x68C28

#define mmMME0_QM_ARB_MST_CRED_STS_31                                0x68C2C

#define mmMME0_QM_CGM_CFG                                            0x68C70

#define mmMME0_QM_CGM_STS                                            0x68C74

#define mmMME0_QM_CGM_CFG1                                           0x68C78

#define mmMME0_QM_LOCAL_RANGE_BASE                                   0x68C80

#define mmMME0_QM_LOCAL_RANGE_SIZE                                   0x68C84

#define mmMME0_QM_CSMR_STRICT_PRIO_CFG                               0x68C90

#define mmMME0_QM_HBW_RD_RATE_LIM_CFG_1                              0x68C94

#define mmMME0_QM_LBW_WR_RATE_LIM_CFG_0                              0x68C98

#define mmMME0_QM_LBW_WR_RATE_LIM_CFG_1                              0x68C9C

#define mmMME0_QM_HBW_RD_RATE_LIM_CFG_0                              0x68CA0

#define mmMME0_QM_GLBL_AXCACHE                                       0x68CA4

#define mmMME0_QM_IND_GW_APB_CFG                                     0x68CB0

#define mmMME0_QM_IND_GW_APB_WDATA                                   0x68CB4

#define mmMME0_QM_IND_GW_APB_RDATA                                   0x68CB8

#define mmMME0_QM_IND_GW_APB_STATUS                                  0x68CBC

#define mmMME0_QM_GLBL_ERR_ADDR_LO                                   0x68CD0

#define mmMME0_QM_GLBL_ERR_ADDR_HI                                   0x68CD4

#define mmMME0_QM_GLBL_ERR_WDATA                                     0x68CD8

#define mmMME0_QM_GLBL_MEM_INIT_BUSY                                 0x68D00

#endif /* ASIC_REG_MME0_QM_REGS_H_ */
