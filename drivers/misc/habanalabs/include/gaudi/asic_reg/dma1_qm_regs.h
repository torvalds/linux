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

#ifndef ASIC_REG_DMA1_QM_REGS_H_
#define ASIC_REG_DMA1_QM_REGS_H_

/*
 *****************************************
 *   DMA1_QM (Prototype: QMAN)
 *****************************************
 */

#define mmDMA1_QM_GLBL_CFG0                                          0x528000

#define mmDMA1_QM_GLBL_CFG1                                          0x528004

#define mmDMA1_QM_GLBL_PROT                                          0x528008

#define mmDMA1_QM_GLBL_ERR_CFG                                       0x52800C

#define mmDMA1_QM_GLBL_SECURE_PROPS_0                                0x528010

#define mmDMA1_QM_GLBL_SECURE_PROPS_1                                0x528014

#define mmDMA1_QM_GLBL_SECURE_PROPS_2                                0x528018

#define mmDMA1_QM_GLBL_SECURE_PROPS_3                                0x52801C

#define mmDMA1_QM_GLBL_SECURE_PROPS_4                                0x528020

#define mmDMA1_QM_GLBL_NON_SECURE_PROPS_0                            0x528024

#define mmDMA1_QM_GLBL_NON_SECURE_PROPS_1                            0x528028

#define mmDMA1_QM_GLBL_NON_SECURE_PROPS_2                            0x52802C

#define mmDMA1_QM_GLBL_NON_SECURE_PROPS_3                            0x528030

#define mmDMA1_QM_GLBL_NON_SECURE_PROPS_4                            0x528034

#define mmDMA1_QM_GLBL_STS0                                          0x528038

#define mmDMA1_QM_GLBL_STS1_0                                        0x528040

#define mmDMA1_QM_GLBL_STS1_1                                        0x528044

#define mmDMA1_QM_GLBL_STS1_2                                        0x528048

#define mmDMA1_QM_GLBL_STS1_3                                        0x52804C

#define mmDMA1_QM_GLBL_STS1_4                                        0x528050

#define mmDMA1_QM_GLBL_MSG_EN_0                                      0x528054

#define mmDMA1_QM_GLBL_MSG_EN_1                                      0x528058

#define mmDMA1_QM_GLBL_MSG_EN_2                                      0x52805C

#define mmDMA1_QM_GLBL_MSG_EN_3                                      0x528060

#define mmDMA1_QM_GLBL_MSG_EN_4                                      0x528068

#define mmDMA1_QM_PQ_BASE_LO_0                                       0x528070

#define mmDMA1_QM_PQ_BASE_LO_1                                       0x528074

#define mmDMA1_QM_PQ_BASE_LO_2                                       0x528078

#define mmDMA1_QM_PQ_BASE_LO_3                                       0x52807C

#define mmDMA1_QM_PQ_BASE_HI_0                                       0x528080

#define mmDMA1_QM_PQ_BASE_HI_1                                       0x528084

#define mmDMA1_QM_PQ_BASE_HI_2                                       0x528088

#define mmDMA1_QM_PQ_BASE_HI_3                                       0x52808C

#define mmDMA1_QM_PQ_SIZE_0                                          0x528090

#define mmDMA1_QM_PQ_SIZE_1                                          0x528094

#define mmDMA1_QM_PQ_SIZE_2                                          0x528098

#define mmDMA1_QM_PQ_SIZE_3                                          0x52809C

#define mmDMA1_QM_PQ_PI_0                                            0x5280A0

#define mmDMA1_QM_PQ_PI_1                                            0x5280A4

#define mmDMA1_QM_PQ_PI_2                                            0x5280A8

#define mmDMA1_QM_PQ_PI_3                                            0x5280AC

#define mmDMA1_QM_PQ_CI_0                                            0x5280B0

#define mmDMA1_QM_PQ_CI_1                                            0x5280B4

#define mmDMA1_QM_PQ_CI_2                                            0x5280B8

#define mmDMA1_QM_PQ_CI_3                                            0x5280BC

#define mmDMA1_QM_PQ_CFG0_0                                          0x5280C0

#define mmDMA1_QM_PQ_CFG0_1                                          0x5280C4

#define mmDMA1_QM_PQ_CFG0_2                                          0x5280C8

#define mmDMA1_QM_PQ_CFG0_3                                          0x5280CC

#define mmDMA1_QM_PQ_CFG1_0                                          0x5280D0

#define mmDMA1_QM_PQ_CFG1_1                                          0x5280D4

#define mmDMA1_QM_PQ_CFG1_2                                          0x5280D8

#define mmDMA1_QM_PQ_CFG1_3                                          0x5280DC

#define mmDMA1_QM_PQ_ARUSER_31_11_0                                  0x5280E0

#define mmDMA1_QM_PQ_ARUSER_31_11_1                                  0x5280E4

#define mmDMA1_QM_PQ_ARUSER_31_11_2                                  0x5280E8

#define mmDMA1_QM_PQ_ARUSER_31_11_3                                  0x5280EC

#define mmDMA1_QM_PQ_STS0_0                                          0x5280F0

#define mmDMA1_QM_PQ_STS0_1                                          0x5280F4

#define mmDMA1_QM_PQ_STS0_2                                          0x5280F8

#define mmDMA1_QM_PQ_STS0_3                                          0x5280FC

#define mmDMA1_QM_PQ_STS1_0                                          0x528100

#define mmDMA1_QM_PQ_STS1_1                                          0x528104

#define mmDMA1_QM_PQ_STS1_2                                          0x528108

#define mmDMA1_QM_PQ_STS1_3                                          0x52810C

#define mmDMA1_QM_CQ_CFG0_0                                          0x528110

#define mmDMA1_QM_CQ_CFG0_1                                          0x528114

#define mmDMA1_QM_CQ_CFG0_2                                          0x528118

#define mmDMA1_QM_CQ_CFG0_3                                          0x52811C

#define mmDMA1_QM_CQ_CFG0_4                                          0x528120

#define mmDMA1_QM_CQ_CFG1_0                                          0x528124

#define mmDMA1_QM_CQ_CFG1_1                                          0x528128

#define mmDMA1_QM_CQ_CFG1_2                                          0x52812C

#define mmDMA1_QM_CQ_CFG1_3                                          0x528130

#define mmDMA1_QM_CQ_CFG1_4                                          0x528134

#define mmDMA1_QM_CQ_ARUSER_31_11_0                                  0x528138

#define mmDMA1_QM_CQ_ARUSER_31_11_1                                  0x52813C

#define mmDMA1_QM_CQ_ARUSER_31_11_2                                  0x528140

#define mmDMA1_QM_CQ_ARUSER_31_11_3                                  0x528144

#define mmDMA1_QM_CQ_ARUSER_31_11_4                                  0x528148

#define mmDMA1_QM_CQ_STS0_0                                          0x52814C

#define mmDMA1_QM_CQ_STS0_1                                          0x528150

#define mmDMA1_QM_CQ_STS0_2                                          0x528154

#define mmDMA1_QM_CQ_STS0_3                                          0x528158

#define mmDMA1_QM_CQ_STS0_4                                          0x52815C

#define mmDMA1_QM_CQ_STS1_0                                          0x528160

#define mmDMA1_QM_CQ_STS1_1                                          0x528164

#define mmDMA1_QM_CQ_STS1_2                                          0x528168

#define mmDMA1_QM_CQ_STS1_3                                          0x52816C

#define mmDMA1_QM_CQ_STS1_4                                          0x528170

#define mmDMA1_QM_CQ_PTR_LO_0                                        0x528174

#define mmDMA1_QM_CQ_PTR_HI_0                                        0x528178

#define mmDMA1_QM_CQ_TSIZE_0                                         0x52817C

#define mmDMA1_QM_CQ_CTL_0                                           0x528180

#define mmDMA1_QM_CQ_PTR_LO_1                                        0x528184

#define mmDMA1_QM_CQ_PTR_HI_1                                        0x528188

#define mmDMA1_QM_CQ_TSIZE_1                                         0x52818C

#define mmDMA1_QM_CQ_CTL_1                                           0x528190

#define mmDMA1_QM_CQ_PTR_LO_2                                        0x528194

#define mmDMA1_QM_CQ_PTR_HI_2                                        0x528198

#define mmDMA1_QM_CQ_TSIZE_2                                         0x52819C

#define mmDMA1_QM_CQ_CTL_2                                           0x5281A0

#define mmDMA1_QM_CQ_PTR_LO_3                                        0x5281A4

#define mmDMA1_QM_CQ_PTR_HI_3                                        0x5281A8

#define mmDMA1_QM_CQ_TSIZE_3                                         0x5281AC

#define mmDMA1_QM_CQ_CTL_3                                           0x5281B0

#define mmDMA1_QM_CQ_PTR_LO_4                                        0x5281B4

#define mmDMA1_QM_CQ_PTR_HI_4                                        0x5281B8

#define mmDMA1_QM_CQ_TSIZE_4                                         0x5281BC

#define mmDMA1_QM_CQ_CTL_4                                           0x5281C0

#define mmDMA1_QM_CQ_PTR_LO_STS_0                                    0x5281C4

#define mmDMA1_QM_CQ_PTR_LO_STS_1                                    0x5281C8

#define mmDMA1_QM_CQ_PTR_LO_STS_2                                    0x5281CC

#define mmDMA1_QM_CQ_PTR_LO_STS_3                                    0x5281D0

#define mmDMA1_QM_CQ_PTR_LO_STS_4                                    0x5281D4

#define mmDMA1_QM_CQ_PTR_HI_STS_0                                    0x5281D8

#define mmDMA1_QM_CQ_PTR_HI_STS_1                                    0x5281DC

#define mmDMA1_QM_CQ_PTR_HI_STS_2                                    0x5281E0

#define mmDMA1_QM_CQ_PTR_HI_STS_3                                    0x5281E4

#define mmDMA1_QM_CQ_PTR_HI_STS_4                                    0x5281E8

#define mmDMA1_QM_CQ_TSIZE_STS_0                                     0x5281EC

#define mmDMA1_QM_CQ_TSIZE_STS_1                                     0x5281F0

#define mmDMA1_QM_CQ_TSIZE_STS_2                                     0x5281F4

#define mmDMA1_QM_CQ_TSIZE_STS_3                                     0x5281F8

#define mmDMA1_QM_CQ_TSIZE_STS_4                                     0x5281FC

#define mmDMA1_QM_CQ_CTL_STS_0                                       0x528200

#define mmDMA1_QM_CQ_CTL_STS_1                                       0x528204

#define mmDMA1_QM_CQ_CTL_STS_2                                       0x528208

#define mmDMA1_QM_CQ_CTL_STS_3                                       0x52820C

#define mmDMA1_QM_CQ_CTL_STS_4                                       0x528210

#define mmDMA1_QM_CQ_IFIFO_CNT_0                                     0x528214

#define mmDMA1_QM_CQ_IFIFO_CNT_1                                     0x528218

#define mmDMA1_QM_CQ_IFIFO_CNT_2                                     0x52821C

#define mmDMA1_QM_CQ_IFIFO_CNT_3                                     0x528220

#define mmDMA1_QM_CQ_IFIFO_CNT_4                                     0x528224

#define mmDMA1_QM_CP_MSG_BASE0_ADDR_LO_0                             0x528228

#define mmDMA1_QM_CP_MSG_BASE0_ADDR_LO_1                             0x52822C

#define mmDMA1_QM_CP_MSG_BASE0_ADDR_LO_2                             0x528230

#define mmDMA1_QM_CP_MSG_BASE0_ADDR_LO_3                             0x528234

#define mmDMA1_QM_CP_MSG_BASE0_ADDR_LO_4                             0x528238

#define mmDMA1_QM_CP_MSG_BASE0_ADDR_HI_0                             0x52823C

#define mmDMA1_QM_CP_MSG_BASE0_ADDR_HI_1                             0x528240

#define mmDMA1_QM_CP_MSG_BASE0_ADDR_HI_2                             0x528244

#define mmDMA1_QM_CP_MSG_BASE0_ADDR_HI_3                             0x528248

#define mmDMA1_QM_CP_MSG_BASE0_ADDR_HI_4                             0x52824C

#define mmDMA1_QM_CP_MSG_BASE1_ADDR_LO_0                             0x528250

#define mmDMA1_QM_CP_MSG_BASE1_ADDR_LO_1                             0x528254

#define mmDMA1_QM_CP_MSG_BASE1_ADDR_LO_2                             0x528258

#define mmDMA1_QM_CP_MSG_BASE1_ADDR_LO_3                             0x52825C

#define mmDMA1_QM_CP_MSG_BASE1_ADDR_LO_4                             0x528260

#define mmDMA1_QM_CP_MSG_BASE1_ADDR_HI_0                             0x528264

#define mmDMA1_QM_CP_MSG_BASE1_ADDR_HI_1                             0x528268

#define mmDMA1_QM_CP_MSG_BASE1_ADDR_HI_2                             0x52826C

#define mmDMA1_QM_CP_MSG_BASE1_ADDR_HI_3                             0x528270

#define mmDMA1_QM_CP_MSG_BASE1_ADDR_HI_4                             0x528274

#define mmDMA1_QM_CP_MSG_BASE2_ADDR_LO_0                             0x528278

#define mmDMA1_QM_CP_MSG_BASE2_ADDR_LO_1                             0x52827C

#define mmDMA1_QM_CP_MSG_BASE2_ADDR_LO_2                             0x528280

#define mmDMA1_QM_CP_MSG_BASE2_ADDR_LO_3                             0x528284

#define mmDMA1_QM_CP_MSG_BASE2_ADDR_LO_4                             0x528288

#define mmDMA1_QM_CP_MSG_BASE2_ADDR_HI_0                             0x52828C

#define mmDMA1_QM_CP_MSG_BASE2_ADDR_HI_1                             0x528290

#define mmDMA1_QM_CP_MSG_BASE2_ADDR_HI_2                             0x528294

#define mmDMA1_QM_CP_MSG_BASE2_ADDR_HI_3                             0x528298

#define mmDMA1_QM_CP_MSG_BASE2_ADDR_HI_4                             0x52829C

#define mmDMA1_QM_CP_MSG_BASE3_ADDR_LO_0                             0x5282A0

#define mmDMA1_QM_CP_MSG_BASE3_ADDR_LO_1                             0x5282A4

#define mmDMA1_QM_CP_MSG_BASE3_ADDR_LO_2                             0x5282A8

#define mmDMA1_QM_CP_MSG_BASE3_ADDR_LO_3                             0x5282AC

#define mmDMA1_QM_CP_MSG_BASE3_ADDR_LO_4                             0x5282B0

#define mmDMA1_QM_CP_MSG_BASE3_ADDR_HI_0                             0x5282B4

#define mmDMA1_QM_CP_MSG_BASE3_ADDR_HI_1                             0x5282B8

#define mmDMA1_QM_CP_MSG_BASE3_ADDR_HI_2                             0x5282BC

#define mmDMA1_QM_CP_MSG_BASE3_ADDR_HI_3                             0x5282C0

#define mmDMA1_QM_CP_MSG_BASE3_ADDR_HI_4                             0x5282C4

#define mmDMA1_QM_CP_LDMA_TSIZE_OFFSET_0                             0x5282C8

#define mmDMA1_QM_CP_LDMA_TSIZE_OFFSET_1                             0x5282CC

#define mmDMA1_QM_CP_LDMA_TSIZE_OFFSET_2                             0x5282D0

#define mmDMA1_QM_CP_LDMA_TSIZE_OFFSET_3                             0x5282D4

#define mmDMA1_QM_CP_LDMA_TSIZE_OFFSET_4                             0x5282D8

#define mmDMA1_QM_CP_LDMA_SRC_BASE_LO_OFFSET_0                       0x5282E0

#define mmDMA1_QM_CP_LDMA_SRC_BASE_LO_OFFSET_1                       0x5282E4

#define mmDMA1_QM_CP_LDMA_SRC_BASE_LO_OFFSET_2                       0x5282E8

#define mmDMA1_QM_CP_LDMA_SRC_BASE_LO_OFFSET_3                       0x5282EC

#define mmDMA1_QM_CP_LDMA_SRC_BASE_LO_OFFSET_4                       0x5282F0

#define mmDMA1_QM_CP_LDMA_DST_BASE_LO_OFFSET_0                       0x5282F4

#define mmDMA1_QM_CP_LDMA_DST_BASE_LO_OFFSET_1                       0x5282F8

#define mmDMA1_QM_CP_LDMA_DST_BASE_LO_OFFSET_2                       0x5282FC

#define mmDMA1_QM_CP_LDMA_DST_BASE_LO_OFFSET_3                       0x528300

#define mmDMA1_QM_CP_LDMA_DST_BASE_LO_OFFSET_4                       0x528304

#define mmDMA1_QM_CP_FENCE0_RDATA_0                                  0x528308

#define mmDMA1_QM_CP_FENCE0_RDATA_1                                  0x52830C

#define mmDMA1_QM_CP_FENCE0_RDATA_2                                  0x528310

#define mmDMA1_QM_CP_FENCE0_RDATA_3                                  0x528314

#define mmDMA1_QM_CP_FENCE0_RDATA_4                                  0x528318

#define mmDMA1_QM_CP_FENCE1_RDATA_0                                  0x52831C

#define mmDMA1_QM_CP_FENCE1_RDATA_1                                  0x528320

#define mmDMA1_QM_CP_FENCE1_RDATA_2                                  0x528324

#define mmDMA1_QM_CP_FENCE1_RDATA_3                                  0x528328

#define mmDMA1_QM_CP_FENCE1_RDATA_4                                  0x52832C

#define mmDMA1_QM_CP_FENCE2_RDATA_0                                  0x528330

#define mmDMA1_QM_CP_FENCE2_RDATA_1                                  0x528334

#define mmDMA1_QM_CP_FENCE2_RDATA_2                                  0x528338

#define mmDMA1_QM_CP_FENCE2_RDATA_3                                  0x52833C

#define mmDMA1_QM_CP_FENCE2_RDATA_4                                  0x528340

#define mmDMA1_QM_CP_FENCE3_RDATA_0                                  0x528344

#define mmDMA1_QM_CP_FENCE3_RDATA_1                                  0x528348

#define mmDMA1_QM_CP_FENCE3_RDATA_2                                  0x52834C

#define mmDMA1_QM_CP_FENCE3_RDATA_3                                  0x528350

#define mmDMA1_QM_CP_FENCE3_RDATA_4                                  0x528354

#define mmDMA1_QM_CP_FENCE0_CNT_0                                    0x528358

#define mmDMA1_QM_CP_FENCE0_CNT_1                                    0x52835C

#define mmDMA1_QM_CP_FENCE0_CNT_2                                    0x528360

#define mmDMA1_QM_CP_FENCE0_CNT_3                                    0x528364

#define mmDMA1_QM_CP_FENCE0_CNT_4                                    0x528368

#define mmDMA1_QM_CP_FENCE1_CNT_0                                    0x52836C

#define mmDMA1_QM_CP_FENCE1_CNT_1                                    0x528370

#define mmDMA1_QM_CP_FENCE1_CNT_2                                    0x528374

#define mmDMA1_QM_CP_FENCE1_CNT_3                                    0x528378

#define mmDMA1_QM_CP_FENCE1_CNT_4                                    0x52837C

#define mmDMA1_QM_CP_FENCE2_CNT_0                                    0x528380

#define mmDMA1_QM_CP_FENCE2_CNT_1                                    0x528384

#define mmDMA1_QM_CP_FENCE2_CNT_2                                    0x528388

#define mmDMA1_QM_CP_FENCE2_CNT_3                                    0x52838C

#define mmDMA1_QM_CP_FENCE2_CNT_4                                    0x528390

#define mmDMA1_QM_CP_FENCE3_CNT_0                                    0x528394

#define mmDMA1_QM_CP_FENCE3_CNT_1                                    0x528398

#define mmDMA1_QM_CP_FENCE3_CNT_2                                    0x52839C

#define mmDMA1_QM_CP_FENCE3_CNT_3                                    0x5283A0

#define mmDMA1_QM_CP_FENCE3_CNT_4                                    0x5283A4

#define mmDMA1_QM_CP_STS_0                                           0x5283A8

#define mmDMA1_QM_CP_STS_1                                           0x5283AC

#define mmDMA1_QM_CP_STS_2                                           0x5283B0

#define mmDMA1_QM_CP_STS_3                                           0x5283B4

#define mmDMA1_QM_CP_STS_4                                           0x5283B8

#define mmDMA1_QM_CP_CURRENT_INST_LO_0                               0x5283BC

#define mmDMA1_QM_CP_CURRENT_INST_LO_1                               0x5283C0

#define mmDMA1_QM_CP_CURRENT_INST_LO_2                               0x5283C4

#define mmDMA1_QM_CP_CURRENT_INST_LO_3                               0x5283C8

#define mmDMA1_QM_CP_CURRENT_INST_LO_4                               0x5283CC

#define mmDMA1_QM_CP_CURRENT_INST_HI_0                               0x5283D0

#define mmDMA1_QM_CP_CURRENT_INST_HI_1                               0x5283D4

#define mmDMA1_QM_CP_CURRENT_INST_HI_2                               0x5283D8

#define mmDMA1_QM_CP_CURRENT_INST_HI_3                               0x5283DC

#define mmDMA1_QM_CP_CURRENT_INST_HI_4                               0x5283E0

#define mmDMA1_QM_CP_BARRIER_CFG_0                                   0x5283F4

#define mmDMA1_QM_CP_BARRIER_CFG_1                                   0x5283F8

#define mmDMA1_QM_CP_BARRIER_CFG_2                                   0x5283FC

#define mmDMA1_QM_CP_BARRIER_CFG_3                                   0x528400

#define mmDMA1_QM_CP_BARRIER_CFG_4                                   0x528404

#define mmDMA1_QM_CP_DBG_0_0                                         0x528408

#define mmDMA1_QM_CP_DBG_0_1                                         0x52840C

#define mmDMA1_QM_CP_DBG_0_2                                         0x528410

#define mmDMA1_QM_CP_DBG_0_3                                         0x528414

#define mmDMA1_QM_CP_DBG_0_4                                         0x528418

#define mmDMA1_QM_CP_ARUSER_31_11_0                                  0x52841C

#define mmDMA1_QM_CP_ARUSER_31_11_1                                  0x528420

#define mmDMA1_QM_CP_ARUSER_31_11_2                                  0x528424

#define mmDMA1_QM_CP_ARUSER_31_11_3                                  0x528428

#define mmDMA1_QM_CP_ARUSER_31_11_4                                  0x52842C

#define mmDMA1_QM_CP_AWUSER_31_11_0                                  0x528430

#define mmDMA1_QM_CP_AWUSER_31_11_1                                  0x528434

#define mmDMA1_QM_CP_AWUSER_31_11_2                                  0x528438

#define mmDMA1_QM_CP_AWUSER_31_11_3                                  0x52843C

#define mmDMA1_QM_CP_AWUSER_31_11_4                                  0x528440

#define mmDMA1_QM_ARB_CFG_0                                          0x528A00

#define mmDMA1_QM_ARB_CHOISE_Q_PUSH                                  0x528A04

#define mmDMA1_QM_ARB_WRR_WEIGHT_0                                   0x528A08

#define mmDMA1_QM_ARB_WRR_WEIGHT_1                                   0x528A0C

#define mmDMA1_QM_ARB_WRR_WEIGHT_2                                   0x528A10

#define mmDMA1_QM_ARB_WRR_WEIGHT_3                                   0x528A14

#define mmDMA1_QM_ARB_CFG_1                                          0x528A18

#define mmDMA1_QM_ARB_MST_AVAIL_CRED_0                               0x528A20

#define mmDMA1_QM_ARB_MST_AVAIL_CRED_1                               0x528A24

#define mmDMA1_QM_ARB_MST_AVAIL_CRED_2                               0x528A28

#define mmDMA1_QM_ARB_MST_AVAIL_CRED_3                               0x528A2C

#define mmDMA1_QM_ARB_MST_AVAIL_CRED_4                               0x528A30

#define mmDMA1_QM_ARB_MST_AVAIL_CRED_5                               0x528A34

#define mmDMA1_QM_ARB_MST_AVAIL_CRED_6                               0x528A38

#define mmDMA1_QM_ARB_MST_AVAIL_CRED_7                               0x528A3C

#define mmDMA1_QM_ARB_MST_AVAIL_CRED_8                               0x528A40

#define mmDMA1_QM_ARB_MST_AVAIL_CRED_9                               0x528A44

#define mmDMA1_QM_ARB_MST_AVAIL_CRED_10                              0x528A48

#define mmDMA1_QM_ARB_MST_AVAIL_CRED_11                              0x528A4C

#define mmDMA1_QM_ARB_MST_AVAIL_CRED_12                              0x528A50

#define mmDMA1_QM_ARB_MST_AVAIL_CRED_13                              0x528A54

#define mmDMA1_QM_ARB_MST_AVAIL_CRED_14                              0x528A58

#define mmDMA1_QM_ARB_MST_AVAIL_CRED_15                              0x528A5C

#define mmDMA1_QM_ARB_MST_AVAIL_CRED_16                              0x528A60

#define mmDMA1_QM_ARB_MST_AVAIL_CRED_17                              0x528A64

#define mmDMA1_QM_ARB_MST_AVAIL_CRED_18                              0x528A68

#define mmDMA1_QM_ARB_MST_AVAIL_CRED_19                              0x528A6C

#define mmDMA1_QM_ARB_MST_AVAIL_CRED_20                              0x528A70

#define mmDMA1_QM_ARB_MST_AVAIL_CRED_21                              0x528A74

#define mmDMA1_QM_ARB_MST_AVAIL_CRED_22                              0x528A78

#define mmDMA1_QM_ARB_MST_AVAIL_CRED_23                              0x528A7C

#define mmDMA1_QM_ARB_MST_AVAIL_CRED_24                              0x528A80

#define mmDMA1_QM_ARB_MST_AVAIL_CRED_25                              0x528A84

#define mmDMA1_QM_ARB_MST_AVAIL_CRED_26                              0x528A88

#define mmDMA1_QM_ARB_MST_AVAIL_CRED_27                              0x528A8C

#define mmDMA1_QM_ARB_MST_AVAIL_CRED_28                              0x528A90

#define mmDMA1_QM_ARB_MST_AVAIL_CRED_29                              0x528A94

#define mmDMA1_QM_ARB_MST_AVAIL_CRED_30                              0x528A98

#define mmDMA1_QM_ARB_MST_AVAIL_CRED_31                              0x528A9C

#define mmDMA1_QM_ARB_MST_CRED_INC                                   0x528AA0

#define mmDMA1_QM_ARB_MST_CHOISE_PUSH_OFST_0                         0x528AA4

#define mmDMA1_QM_ARB_MST_CHOISE_PUSH_OFST_1                         0x528AA8

#define mmDMA1_QM_ARB_MST_CHOISE_PUSH_OFST_2                         0x528AAC

#define mmDMA1_QM_ARB_MST_CHOISE_PUSH_OFST_3                         0x528AB0

#define mmDMA1_QM_ARB_MST_CHOISE_PUSH_OFST_4                         0x528AB4

#define mmDMA1_QM_ARB_MST_CHOISE_PUSH_OFST_5                         0x528AB8

#define mmDMA1_QM_ARB_MST_CHOISE_PUSH_OFST_6                         0x528ABC

#define mmDMA1_QM_ARB_MST_CHOISE_PUSH_OFST_7                         0x528AC0

#define mmDMA1_QM_ARB_MST_CHOISE_PUSH_OFST_8                         0x528AC4

#define mmDMA1_QM_ARB_MST_CHOISE_PUSH_OFST_9                         0x528AC8

#define mmDMA1_QM_ARB_MST_CHOISE_PUSH_OFST_10                        0x528ACC

#define mmDMA1_QM_ARB_MST_CHOISE_PUSH_OFST_11                        0x528AD0

#define mmDMA1_QM_ARB_MST_CHOISE_PUSH_OFST_12                        0x528AD4

#define mmDMA1_QM_ARB_MST_CHOISE_PUSH_OFST_13                        0x528AD8

#define mmDMA1_QM_ARB_MST_CHOISE_PUSH_OFST_14                        0x528ADC

#define mmDMA1_QM_ARB_MST_CHOISE_PUSH_OFST_15                        0x528AE0

#define mmDMA1_QM_ARB_MST_CHOISE_PUSH_OFST_16                        0x528AE4

#define mmDMA1_QM_ARB_MST_CHOISE_PUSH_OFST_17                        0x528AE8

#define mmDMA1_QM_ARB_MST_CHOISE_PUSH_OFST_18                        0x528AEC

#define mmDMA1_QM_ARB_MST_CHOISE_PUSH_OFST_19                        0x528AF0

#define mmDMA1_QM_ARB_MST_CHOISE_PUSH_OFST_20                        0x528AF4

#define mmDMA1_QM_ARB_MST_CHOISE_PUSH_OFST_21                        0x528AF8

#define mmDMA1_QM_ARB_MST_CHOISE_PUSH_OFST_22                        0x528AFC

#define mmDMA1_QM_ARB_MST_CHOISE_PUSH_OFST_23                        0x528B00

#define mmDMA1_QM_ARB_MST_CHOISE_PUSH_OFST_24                        0x528B04

#define mmDMA1_QM_ARB_MST_CHOISE_PUSH_OFST_25                        0x528B08

#define mmDMA1_QM_ARB_MST_CHOISE_PUSH_OFST_26                        0x528B0C

#define mmDMA1_QM_ARB_MST_CHOISE_PUSH_OFST_27                        0x528B10

#define mmDMA1_QM_ARB_MST_CHOISE_PUSH_OFST_28                        0x528B14

#define mmDMA1_QM_ARB_MST_CHOISE_PUSH_OFST_29                        0x528B18

#define mmDMA1_QM_ARB_MST_CHOISE_PUSH_OFST_30                        0x528B1C

#define mmDMA1_QM_ARB_MST_CHOISE_PUSH_OFST_31                        0x528B20

#define mmDMA1_QM_ARB_SLV_MASTER_INC_CRED_OFST                       0x528B28

#define mmDMA1_QM_ARB_MST_SLAVE_EN                                   0x528B2C

#define mmDMA1_QM_ARB_MST_QUIET_PER                                  0x528B34

#define mmDMA1_QM_ARB_SLV_CHOISE_WDT                                 0x528B38

#define mmDMA1_QM_ARB_SLV_ID                                         0x528B3C

#define mmDMA1_QM_ARB_MSG_MAX_INFLIGHT                               0x528B44

#define mmDMA1_QM_ARB_MSG_AWUSER_31_11                               0x528B48

#define mmDMA1_QM_ARB_MSG_AWUSER_SEC_PROP                            0x528B4C

#define mmDMA1_QM_ARB_MSG_AWUSER_NON_SEC_PROP                        0x528B50

#define mmDMA1_QM_ARB_BASE_LO                                        0x528B54

#define mmDMA1_QM_ARB_BASE_HI                                        0x528B58

#define mmDMA1_QM_ARB_STATE_STS                                      0x528B80

#define mmDMA1_QM_ARB_CHOISE_FULLNESS_STS                            0x528B84

#define mmDMA1_QM_ARB_MSG_STS                                        0x528B88

#define mmDMA1_QM_ARB_SLV_CHOISE_Q_HEAD                              0x528B8C

#define mmDMA1_QM_ARB_ERR_CAUSE                                      0x528B9C

#define mmDMA1_QM_ARB_ERR_MSG_EN                                     0x528BA0

#define mmDMA1_QM_ARB_ERR_STS_DRP                                    0x528BA8

#define mmDMA1_QM_ARB_MST_CRED_STS_0                                 0x528BB0

#define mmDMA1_QM_ARB_MST_CRED_STS_1                                 0x528BB4

#define mmDMA1_QM_ARB_MST_CRED_STS_2                                 0x528BB8

#define mmDMA1_QM_ARB_MST_CRED_STS_3                                 0x528BBC

#define mmDMA1_QM_ARB_MST_CRED_STS_4                                 0x528BC0

#define mmDMA1_QM_ARB_MST_CRED_STS_5                                 0x528BC4

#define mmDMA1_QM_ARB_MST_CRED_STS_6                                 0x528BC8

#define mmDMA1_QM_ARB_MST_CRED_STS_7                                 0x528BCC

#define mmDMA1_QM_ARB_MST_CRED_STS_8                                 0x528BD0

#define mmDMA1_QM_ARB_MST_CRED_STS_9                                 0x528BD4

#define mmDMA1_QM_ARB_MST_CRED_STS_10                                0x528BD8

#define mmDMA1_QM_ARB_MST_CRED_STS_11                                0x528BDC

#define mmDMA1_QM_ARB_MST_CRED_STS_12                                0x528BE0

#define mmDMA1_QM_ARB_MST_CRED_STS_13                                0x528BE4

#define mmDMA1_QM_ARB_MST_CRED_STS_14                                0x528BE8

#define mmDMA1_QM_ARB_MST_CRED_STS_15                                0x528BEC

#define mmDMA1_QM_ARB_MST_CRED_STS_16                                0x528BF0

#define mmDMA1_QM_ARB_MST_CRED_STS_17                                0x528BF4

#define mmDMA1_QM_ARB_MST_CRED_STS_18                                0x528BF8

#define mmDMA1_QM_ARB_MST_CRED_STS_19                                0x528BFC

#define mmDMA1_QM_ARB_MST_CRED_STS_20                                0x528C00

#define mmDMA1_QM_ARB_MST_CRED_STS_21                                0x528C04

#define mmDMA1_QM_ARB_MST_CRED_STS_22                                0x528C08

#define mmDMA1_QM_ARB_MST_CRED_STS_23                                0x528C0C

#define mmDMA1_QM_ARB_MST_CRED_STS_24                                0x528C10

#define mmDMA1_QM_ARB_MST_CRED_STS_25                                0x528C14

#define mmDMA1_QM_ARB_MST_CRED_STS_26                                0x528C18

#define mmDMA1_QM_ARB_MST_CRED_STS_27                                0x528C1C

#define mmDMA1_QM_ARB_MST_CRED_STS_28                                0x528C20

#define mmDMA1_QM_ARB_MST_CRED_STS_29                                0x528C24

#define mmDMA1_QM_ARB_MST_CRED_STS_30                                0x528C28

#define mmDMA1_QM_ARB_MST_CRED_STS_31                                0x528C2C

#define mmDMA1_QM_CGM_CFG                                            0x528C70

#define mmDMA1_QM_CGM_STS                                            0x528C74

#define mmDMA1_QM_CGM_CFG1                                           0x528C78

#define mmDMA1_QM_LOCAL_RANGE_BASE                                   0x528C80

#define mmDMA1_QM_LOCAL_RANGE_SIZE                                   0x528C84

#define mmDMA1_QM_CSMR_STRICT_PRIO_CFG                               0x528C90

#define mmDMA1_QM_HBW_RD_RATE_LIM_CFG_1                              0x528C94

#define mmDMA1_QM_LBW_WR_RATE_LIM_CFG_0                              0x528C98

#define mmDMA1_QM_LBW_WR_RATE_LIM_CFG_1                              0x528C9C

#define mmDMA1_QM_HBW_RD_RATE_LIM_CFG_0                              0x528CA0

#define mmDMA1_QM_GLBL_AXCACHE                                       0x528CA4

#define mmDMA1_QM_IND_GW_APB_CFG                                     0x528CB0

#define mmDMA1_QM_IND_GW_APB_WDATA                                   0x528CB4

#define mmDMA1_QM_IND_GW_APB_RDATA                                   0x528CB8

#define mmDMA1_QM_IND_GW_APB_STATUS                                  0x528CBC

#define mmDMA1_QM_GLBL_ERR_ADDR_LO                                   0x528CD0

#define mmDMA1_QM_GLBL_ERR_ADDR_HI                                   0x528CD4

#define mmDMA1_QM_GLBL_ERR_WDATA                                     0x528CD8

#define mmDMA1_QM_GLBL_MEM_INIT_BUSY                                 0x528D00

#endif /* ASIC_REG_DMA1_QM_REGS_H_ */
