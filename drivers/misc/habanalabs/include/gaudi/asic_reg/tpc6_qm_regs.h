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

#ifndef ASIC_REG_TPC6_QM_REGS_H_
#define ASIC_REG_TPC6_QM_REGS_H_

/*
 *****************************************
 *   TPC6_QM (Prototype: QMAN)
 *****************************************
 */

#define mmTPC6_QM_GLBL_CFG0                                          0xF88000

#define mmTPC6_QM_GLBL_CFG1                                          0xF88004

#define mmTPC6_QM_GLBL_PROT                                          0xF88008

#define mmTPC6_QM_GLBL_ERR_CFG                                       0xF8800C

#define mmTPC6_QM_GLBL_SECURE_PROPS_0                                0xF88010

#define mmTPC6_QM_GLBL_SECURE_PROPS_1                                0xF88014

#define mmTPC6_QM_GLBL_SECURE_PROPS_2                                0xF88018

#define mmTPC6_QM_GLBL_SECURE_PROPS_3                                0xF8801C

#define mmTPC6_QM_GLBL_SECURE_PROPS_4                                0xF88020

#define mmTPC6_QM_GLBL_NON_SECURE_PROPS_0                            0xF88024

#define mmTPC6_QM_GLBL_NON_SECURE_PROPS_1                            0xF88028

#define mmTPC6_QM_GLBL_NON_SECURE_PROPS_2                            0xF8802C

#define mmTPC6_QM_GLBL_NON_SECURE_PROPS_3                            0xF88030

#define mmTPC6_QM_GLBL_NON_SECURE_PROPS_4                            0xF88034

#define mmTPC6_QM_GLBL_STS0                                          0xF88038

#define mmTPC6_QM_GLBL_STS1_0                                        0xF88040

#define mmTPC6_QM_GLBL_STS1_1                                        0xF88044

#define mmTPC6_QM_GLBL_STS1_2                                        0xF88048

#define mmTPC6_QM_GLBL_STS1_3                                        0xF8804C

#define mmTPC6_QM_GLBL_STS1_4                                        0xF88050

#define mmTPC6_QM_GLBL_MSG_EN_0                                      0xF88054

#define mmTPC6_QM_GLBL_MSG_EN_1                                      0xF88058

#define mmTPC6_QM_GLBL_MSG_EN_2                                      0xF8805C

#define mmTPC6_QM_GLBL_MSG_EN_3                                      0xF88060

#define mmTPC6_QM_GLBL_MSG_EN_4                                      0xF88068

#define mmTPC6_QM_PQ_BASE_LO_0                                       0xF88070

#define mmTPC6_QM_PQ_BASE_LO_1                                       0xF88074

#define mmTPC6_QM_PQ_BASE_LO_2                                       0xF88078

#define mmTPC6_QM_PQ_BASE_LO_3                                       0xF8807C

#define mmTPC6_QM_PQ_BASE_HI_0                                       0xF88080

#define mmTPC6_QM_PQ_BASE_HI_1                                       0xF88084

#define mmTPC6_QM_PQ_BASE_HI_2                                       0xF88088

#define mmTPC6_QM_PQ_BASE_HI_3                                       0xF8808C

#define mmTPC6_QM_PQ_SIZE_0                                          0xF88090

#define mmTPC6_QM_PQ_SIZE_1                                          0xF88094

#define mmTPC6_QM_PQ_SIZE_2                                          0xF88098

#define mmTPC6_QM_PQ_SIZE_3                                          0xF8809C

#define mmTPC6_QM_PQ_PI_0                                            0xF880A0

#define mmTPC6_QM_PQ_PI_1                                            0xF880A4

#define mmTPC6_QM_PQ_PI_2                                            0xF880A8

#define mmTPC6_QM_PQ_PI_3                                            0xF880AC

#define mmTPC6_QM_PQ_CI_0                                            0xF880B0

#define mmTPC6_QM_PQ_CI_1                                            0xF880B4

#define mmTPC6_QM_PQ_CI_2                                            0xF880B8

#define mmTPC6_QM_PQ_CI_3                                            0xF880BC

#define mmTPC6_QM_PQ_CFG0_0                                          0xF880C0

#define mmTPC6_QM_PQ_CFG0_1                                          0xF880C4

#define mmTPC6_QM_PQ_CFG0_2                                          0xF880C8

#define mmTPC6_QM_PQ_CFG0_3                                          0xF880CC

#define mmTPC6_QM_PQ_CFG1_0                                          0xF880D0

#define mmTPC6_QM_PQ_CFG1_1                                          0xF880D4

#define mmTPC6_QM_PQ_CFG1_2                                          0xF880D8

#define mmTPC6_QM_PQ_CFG1_3                                          0xF880DC

#define mmTPC6_QM_PQ_ARUSER_31_11_0                                  0xF880E0

#define mmTPC6_QM_PQ_ARUSER_31_11_1                                  0xF880E4

#define mmTPC6_QM_PQ_ARUSER_31_11_2                                  0xF880E8

#define mmTPC6_QM_PQ_ARUSER_31_11_3                                  0xF880EC

#define mmTPC6_QM_PQ_STS0_0                                          0xF880F0

#define mmTPC6_QM_PQ_STS0_1                                          0xF880F4

#define mmTPC6_QM_PQ_STS0_2                                          0xF880F8

#define mmTPC6_QM_PQ_STS0_3                                          0xF880FC

#define mmTPC6_QM_PQ_STS1_0                                          0xF88100

#define mmTPC6_QM_PQ_STS1_1                                          0xF88104

#define mmTPC6_QM_PQ_STS1_2                                          0xF88108

#define mmTPC6_QM_PQ_STS1_3                                          0xF8810C

#define mmTPC6_QM_CQ_CFG0_0                                          0xF88110

#define mmTPC6_QM_CQ_CFG0_1                                          0xF88114

#define mmTPC6_QM_CQ_CFG0_2                                          0xF88118

#define mmTPC6_QM_CQ_CFG0_3                                          0xF8811C

#define mmTPC6_QM_CQ_CFG0_4                                          0xF88120

#define mmTPC6_QM_CQ_CFG1_0                                          0xF88124

#define mmTPC6_QM_CQ_CFG1_1                                          0xF88128

#define mmTPC6_QM_CQ_CFG1_2                                          0xF8812C

#define mmTPC6_QM_CQ_CFG1_3                                          0xF88130

#define mmTPC6_QM_CQ_CFG1_4                                          0xF88134

#define mmTPC6_QM_CQ_ARUSER_31_11_0                                  0xF88138

#define mmTPC6_QM_CQ_ARUSER_31_11_1                                  0xF8813C

#define mmTPC6_QM_CQ_ARUSER_31_11_2                                  0xF88140

#define mmTPC6_QM_CQ_ARUSER_31_11_3                                  0xF88144

#define mmTPC6_QM_CQ_ARUSER_31_11_4                                  0xF88148

#define mmTPC6_QM_CQ_STS0_0                                          0xF8814C

#define mmTPC6_QM_CQ_STS0_1                                          0xF88150

#define mmTPC6_QM_CQ_STS0_2                                          0xF88154

#define mmTPC6_QM_CQ_STS0_3                                          0xF88158

#define mmTPC6_QM_CQ_STS0_4                                          0xF8815C

#define mmTPC6_QM_CQ_STS1_0                                          0xF88160

#define mmTPC6_QM_CQ_STS1_1                                          0xF88164

#define mmTPC6_QM_CQ_STS1_2                                          0xF88168

#define mmTPC6_QM_CQ_STS1_3                                          0xF8816C

#define mmTPC6_QM_CQ_STS1_4                                          0xF88170

#define mmTPC6_QM_CQ_PTR_LO_0                                        0xF88174

#define mmTPC6_QM_CQ_PTR_HI_0                                        0xF88178

#define mmTPC6_QM_CQ_TSIZE_0                                         0xF8817C

#define mmTPC6_QM_CQ_CTL_0                                           0xF88180

#define mmTPC6_QM_CQ_PTR_LO_1                                        0xF88184

#define mmTPC6_QM_CQ_PTR_HI_1                                        0xF88188

#define mmTPC6_QM_CQ_TSIZE_1                                         0xF8818C

#define mmTPC6_QM_CQ_CTL_1                                           0xF88190

#define mmTPC6_QM_CQ_PTR_LO_2                                        0xF88194

#define mmTPC6_QM_CQ_PTR_HI_2                                        0xF88198

#define mmTPC6_QM_CQ_TSIZE_2                                         0xF8819C

#define mmTPC6_QM_CQ_CTL_2                                           0xF881A0

#define mmTPC6_QM_CQ_PTR_LO_3                                        0xF881A4

#define mmTPC6_QM_CQ_PTR_HI_3                                        0xF881A8

#define mmTPC6_QM_CQ_TSIZE_3                                         0xF881AC

#define mmTPC6_QM_CQ_CTL_3                                           0xF881B0

#define mmTPC6_QM_CQ_PTR_LO_4                                        0xF881B4

#define mmTPC6_QM_CQ_PTR_HI_4                                        0xF881B8

#define mmTPC6_QM_CQ_TSIZE_4                                         0xF881BC

#define mmTPC6_QM_CQ_CTL_4                                           0xF881C0

#define mmTPC6_QM_CQ_PTR_LO_STS_0                                    0xF881C4

#define mmTPC6_QM_CQ_PTR_LO_STS_1                                    0xF881C8

#define mmTPC6_QM_CQ_PTR_LO_STS_2                                    0xF881CC

#define mmTPC6_QM_CQ_PTR_LO_STS_3                                    0xF881D0

#define mmTPC6_QM_CQ_PTR_LO_STS_4                                    0xF881D4

#define mmTPC6_QM_CQ_PTR_HI_STS_0                                    0xF881D8

#define mmTPC6_QM_CQ_PTR_HI_STS_1                                    0xF881DC

#define mmTPC6_QM_CQ_PTR_HI_STS_2                                    0xF881E0

#define mmTPC6_QM_CQ_PTR_HI_STS_3                                    0xF881E4

#define mmTPC6_QM_CQ_PTR_HI_STS_4                                    0xF881E8

#define mmTPC6_QM_CQ_TSIZE_STS_0                                     0xF881EC

#define mmTPC6_QM_CQ_TSIZE_STS_1                                     0xF881F0

#define mmTPC6_QM_CQ_TSIZE_STS_2                                     0xF881F4

#define mmTPC6_QM_CQ_TSIZE_STS_3                                     0xF881F8

#define mmTPC6_QM_CQ_TSIZE_STS_4                                     0xF881FC

#define mmTPC6_QM_CQ_CTL_STS_0                                       0xF88200

#define mmTPC6_QM_CQ_CTL_STS_1                                       0xF88204

#define mmTPC6_QM_CQ_CTL_STS_2                                       0xF88208

#define mmTPC6_QM_CQ_CTL_STS_3                                       0xF8820C

#define mmTPC6_QM_CQ_CTL_STS_4                                       0xF88210

#define mmTPC6_QM_CQ_IFIFO_CNT_0                                     0xF88214

#define mmTPC6_QM_CQ_IFIFO_CNT_1                                     0xF88218

#define mmTPC6_QM_CQ_IFIFO_CNT_2                                     0xF8821C

#define mmTPC6_QM_CQ_IFIFO_CNT_3                                     0xF88220

#define mmTPC6_QM_CQ_IFIFO_CNT_4                                     0xF88224

#define mmTPC6_QM_CP_MSG_BASE0_ADDR_LO_0                             0xF88228

#define mmTPC6_QM_CP_MSG_BASE0_ADDR_LO_1                             0xF8822C

#define mmTPC6_QM_CP_MSG_BASE0_ADDR_LO_2                             0xF88230

#define mmTPC6_QM_CP_MSG_BASE0_ADDR_LO_3                             0xF88234

#define mmTPC6_QM_CP_MSG_BASE0_ADDR_LO_4                             0xF88238

#define mmTPC6_QM_CP_MSG_BASE0_ADDR_HI_0                             0xF8823C

#define mmTPC6_QM_CP_MSG_BASE0_ADDR_HI_1                             0xF88240

#define mmTPC6_QM_CP_MSG_BASE0_ADDR_HI_2                             0xF88244

#define mmTPC6_QM_CP_MSG_BASE0_ADDR_HI_3                             0xF88248

#define mmTPC6_QM_CP_MSG_BASE0_ADDR_HI_4                             0xF8824C

#define mmTPC6_QM_CP_MSG_BASE1_ADDR_LO_0                             0xF88250

#define mmTPC6_QM_CP_MSG_BASE1_ADDR_LO_1                             0xF88254

#define mmTPC6_QM_CP_MSG_BASE1_ADDR_LO_2                             0xF88258

#define mmTPC6_QM_CP_MSG_BASE1_ADDR_LO_3                             0xF8825C

#define mmTPC6_QM_CP_MSG_BASE1_ADDR_LO_4                             0xF88260

#define mmTPC6_QM_CP_MSG_BASE1_ADDR_HI_0                             0xF88264

#define mmTPC6_QM_CP_MSG_BASE1_ADDR_HI_1                             0xF88268

#define mmTPC6_QM_CP_MSG_BASE1_ADDR_HI_2                             0xF8826C

#define mmTPC6_QM_CP_MSG_BASE1_ADDR_HI_3                             0xF88270

#define mmTPC6_QM_CP_MSG_BASE1_ADDR_HI_4                             0xF88274

#define mmTPC6_QM_CP_MSG_BASE2_ADDR_LO_0                             0xF88278

#define mmTPC6_QM_CP_MSG_BASE2_ADDR_LO_1                             0xF8827C

#define mmTPC6_QM_CP_MSG_BASE2_ADDR_LO_2                             0xF88280

#define mmTPC6_QM_CP_MSG_BASE2_ADDR_LO_3                             0xF88284

#define mmTPC6_QM_CP_MSG_BASE2_ADDR_LO_4                             0xF88288

#define mmTPC6_QM_CP_MSG_BASE2_ADDR_HI_0                             0xF8828C

#define mmTPC6_QM_CP_MSG_BASE2_ADDR_HI_1                             0xF88290

#define mmTPC6_QM_CP_MSG_BASE2_ADDR_HI_2                             0xF88294

#define mmTPC6_QM_CP_MSG_BASE2_ADDR_HI_3                             0xF88298

#define mmTPC6_QM_CP_MSG_BASE2_ADDR_HI_4                             0xF8829C

#define mmTPC6_QM_CP_MSG_BASE3_ADDR_LO_0                             0xF882A0

#define mmTPC6_QM_CP_MSG_BASE3_ADDR_LO_1                             0xF882A4

#define mmTPC6_QM_CP_MSG_BASE3_ADDR_LO_2                             0xF882A8

#define mmTPC6_QM_CP_MSG_BASE3_ADDR_LO_3                             0xF882AC

#define mmTPC6_QM_CP_MSG_BASE3_ADDR_LO_4                             0xF882B0

#define mmTPC6_QM_CP_MSG_BASE3_ADDR_HI_0                             0xF882B4

#define mmTPC6_QM_CP_MSG_BASE3_ADDR_HI_1                             0xF882B8

#define mmTPC6_QM_CP_MSG_BASE3_ADDR_HI_2                             0xF882BC

#define mmTPC6_QM_CP_MSG_BASE3_ADDR_HI_3                             0xF882C0

#define mmTPC6_QM_CP_MSG_BASE3_ADDR_HI_4                             0xF882C4

#define mmTPC6_QM_CP_LDMA_TSIZE_OFFSET_0                             0xF882C8

#define mmTPC6_QM_CP_LDMA_TSIZE_OFFSET_1                             0xF882CC

#define mmTPC6_QM_CP_LDMA_TSIZE_OFFSET_2                             0xF882D0

#define mmTPC6_QM_CP_LDMA_TSIZE_OFFSET_3                             0xF882D4

#define mmTPC6_QM_CP_LDMA_TSIZE_OFFSET_4                             0xF882D8

#define mmTPC6_QM_CP_LDMA_SRC_BASE_LO_OFFSET_0                       0xF882E0

#define mmTPC6_QM_CP_LDMA_SRC_BASE_LO_OFFSET_1                       0xF882E4

#define mmTPC6_QM_CP_LDMA_SRC_BASE_LO_OFFSET_2                       0xF882E8

#define mmTPC6_QM_CP_LDMA_SRC_BASE_LO_OFFSET_3                       0xF882EC

#define mmTPC6_QM_CP_LDMA_SRC_BASE_LO_OFFSET_4                       0xF882F0

#define mmTPC6_QM_CP_LDMA_DST_BASE_LO_OFFSET_0                       0xF882F4

#define mmTPC6_QM_CP_LDMA_DST_BASE_LO_OFFSET_1                       0xF882F8

#define mmTPC6_QM_CP_LDMA_DST_BASE_LO_OFFSET_2                       0xF882FC

#define mmTPC6_QM_CP_LDMA_DST_BASE_LO_OFFSET_3                       0xF88300

#define mmTPC6_QM_CP_LDMA_DST_BASE_LO_OFFSET_4                       0xF88304

#define mmTPC6_QM_CP_FENCE0_RDATA_0                                  0xF88308

#define mmTPC6_QM_CP_FENCE0_RDATA_1                                  0xF8830C

#define mmTPC6_QM_CP_FENCE0_RDATA_2                                  0xF88310

#define mmTPC6_QM_CP_FENCE0_RDATA_3                                  0xF88314

#define mmTPC6_QM_CP_FENCE0_RDATA_4                                  0xF88318

#define mmTPC6_QM_CP_FENCE1_RDATA_0                                  0xF8831C

#define mmTPC6_QM_CP_FENCE1_RDATA_1                                  0xF88320

#define mmTPC6_QM_CP_FENCE1_RDATA_2                                  0xF88324

#define mmTPC6_QM_CP_FENCE1_RDATA_3                                  0xF88328

#define mmTPC6_QM_CP_FENCE1_RDATA_4                                  0xF8832C

#define mmTPC6_QM_CP_FENCE2_RDATA_0                                  0xF88330

#define mmTPC6_QM_CP_FENCE2_RDATA_1                                  0xF88334

#define mmTPC6_QM_CP_FENCE2_RDATA_2                                  0xF88338

#define mmTPC6_QM_CP_FENCE2_RDATA_3                                  0xF8833C

#define mmTPC6_QM_CP_FENCE2_RDATA_4                                  0xF88340

#define mmTPC6_QM_CP_FENCE3_RDATA_0                                  0xF88344

#define mmTPC6_QM_CP_FENCE3_RDATA_1                                  0xF88348

#define mmTPC6_QM_CP_FENCE3_RDATA_2                                  0xF8834C

#define mmTPC6_QM_CP_FENCE3_RDATA_3                                  0xF88350

#define mmTPC6_QM_CP_FENCE3_RDATA_4                                  0xF88354

#define mmTPC6_QM_CP_FENCE0_CNT_0                                    0xF88358

#define mmTPC6_QM_CP_FENCE0_CNT_1                                    0xF8835C

#define mmTPC6_QM_CP_FENCE0_CNT_2                                    0xF88360

#define mmTPC6_QM_CP_FENCE0_CNT_3                                    0xF88364

#define mmTPC6_QM_CP_FENCE0_CNT_4                                    0xF88368

#define mmTPC6_QM_CP_FENCE1_CNT_0                                    0xF8836C

#define mmTPC6_QM_CP_FENCE1_CNT_1                                    0xF88370

#define mmTPC6_QM_CP_FENCE1_CNT_2                                    0xF88374

#define mmTPC6_QM_CP_FENCE1_CNT_3                                    0xF88378

#define mmTPC6_QM_CP_FENCE1_CNT_4                                    0xF8837C

#define mmTPC6_QM_CP_FENCE2_CNT_0                                    0xF88380

#define mmTPC6_QM_CP_FENCE2_CNT_1                                    0xF88384

#define mmTPC6_QM_CP_FENCE2_CNT_2                                    0xF88388

#define mmTPC6_QM_CP_FENCE2_CNT_3                                    0xF8838C

#define mmTPC6_QM_CP_FENCE2_CNT_4                                    0xF88390

#define mmTPC6_QM_CP_FENCE3_CNT_0                                    0xF88394

#define mmTPC6_QM_CP_FENCE3_CNT_1                                    0xF88398

#define mmTPC6_QM_CP_FENCE3_CNT_2                                    0xF8839C

#define mmTPC6_QM_CP_FENCE3_CNT_3                                    0xF883A0

#define mmTPC6_QM_CP_FENCE3_CNT_4                                    0xF883A4

#define mmTPC6_QM_CP_STS_0                                           0xF883A8

#define mmTPC6_QM_CP_STS_1                                           0xF883AC

#define mmTPC6_QM_CP_STS_2                                           0xF883B0

#define mmTPC6_QM_CP_STS_3                                           0xF883B4

#define mmTPC6_QM_CP_STS_4                                           0xF883B8

#define mmTPC6_QM_CP_CURRENT_INST_LO_0                               0xF883BC

#define mmTPC6_QM_CP_CURRENT_INST_LO_1                               0xF883C0

#define mmTPC6_QM_CP_CURRENT_INST_LO_2                               0xF883C4

#define mmTPC6_QM_CP_CURRENT_INST_LO_3                               0xF883C8

#define mmTPC6_QM_CP_CURRENT_INST_LO_4                               0xF883CC

#define mmTPC6_QM_CP_CURRENT_INST_HI_0                               0xF883D0

#define mmTPC6_QM_CP_CURRENT_INST_HI_1                               0xF883D4

#define mmTPC6_QM_CP_CURRENT_INST_HI_2                               0xF883D8

#define mmTPC6_QM_CP_CURRENT_INST_HI_3                               0xF883DC

#define mmTPC6_QM_CP_CURRENT_INST_HI_4                               0xF883E0

#define mmTPC6_QM_CP_BARRIER_CFG_0                                   0xF883F4

#define mmTPC6_QM_CP_BARRIER_CFG_1                                   0xF883F8

#define mmTPC6_QM_CP_BARRIER_CFG_2                                   0xF883FC

#define mmTPC6_QM_CP_BARRIER_CFG_3                                   0xF88400

#define mmTPC6_QM_CP_BARRIER_CFG_4                                   0xF88404

#define mmTPC6_QM_CP_DBG_0_0                                         0xF88408

#define mmTPC6_QM_CP_DBG_0_1                                         0xF8840C

#define mmTPC6_QM_CP_DBG_0_2                                         0xF88410

#define mmTPC6_QM_CP_DBG_0_3                                         0xF88414

#define mmTPC6_QM_CP_DBG_0_4                                         0xF88418

#define mmTPC6_QM_CP_ARUSER_31_11_0                                  0xF8841C

#define mmTPC6_QM_CP_ARUSER_31_11_1                                  0xF88420

#define mmTPC6_QM_CP_ARUSER_31_11_2                                  0xF88424

#define mmTPC6_QM_CP_ARUSER_31_11_3                                  0xF88428

#define mmTPC6_QM_CP_ARUSER_31_11_4                                  0xF8842C

#define mmTPC6_QM_CP_AWUSER_31_11_0                                  0xF88430

#define mmTPC6_QM_CP_AWUSER_31_11_1                                  0xF88434

#define mmTPC6_QM_CP_AWUSER_31_11_2                                  0xF88438

#define mmTPC6_QM_CP_AWUSER_31_11_3                                  0xF8843C

#define mmTPC6_QM_CP_AWUSER_31_11_4                                  0xF88440

#define mmTPC6_QM_ARB_CFG_0                                          0xF88A00

#define mmTPC6_QM_ARB_CHOISE_Q_PUSH                                  0xF88A04

#define mmTPC6_QM_ARB_WRR_WEIGHT_0                                   0xF88A08

#define mmTPC6_QM_ARB_WRR_WEIGHT_1                                   0xF88A0C

#define mmTPC6_QM_ARB_WRR_WEIGHT_2                                   0xF88A10

#define mmTPC6_QM_ARB_WRR_WEIGHT_3                                   0xF88A14

#define mmTPC6_QM_ARB_CFG_1                                          0xF88A18

#define mmTPC6_QM_ARB_MST_AVAIL_CRED_0                               0xF88A20

#define mmTPC6_QM_ARB_MST_AVAIL_CRED_1                               0xF88A24

#define mmTPC6_QM_ARB_MST_AVAIL_CRED_2                               0xF88A28

#define mmTPC6_QM_ARB_MST_AVAIL_CRED_3                               0xF88A2C

#define mmTPC6_QM_ARB_MST_AVAIL_CRED_4                               0xF88A30

#define mmTPC6_QM_ARB_MST_AVAIL_CRED_5                               0xF88A34

#define mmTPC6_QM_ARB_MST_AVAIL_CRED_6                               0xF88A38

#define mmTPC6_QM_ARB_MST_AVAIL_CRED_7                               0xF88A3C

#define mmTPC6_QM_ARB_MST_AVAIL_CRED_8                               0xF88A40

#define mmTPC6_QM_ARB_MST_AVAIL_CRED_9                               0xF88A44

#define mmTPC6_QM_ARB_MST_AVAIL_CRED_10                              0xF88A48

#define mmTPC6_QM_ARB_MST_AVAIL_CRED_11                              0xF88A4C

#define mmTPC6_QM_ARB_MST_AVAIL_CRED_12                              0xF88A50

#define mmTPC6_QM_ARB_MST_AVAIL_CRED_13                              0xF88A54

#define mmTPC6_QM_ARB_MST_AVAIL_CRED_14                              0xF88A58

#define mmTPC6_QM_ARB_MST_AVAIL_CRED_15                              0xF88A5C

#define mmTPC6_QM_ARB_MST_AVAIL_CRED_16                              0xF88A60

#define mmTPC6_QM_ARB_MST_AVAIL_CRED_17                              0xF88A64

#define mmTPC6_QM_ARB_MST_AVAIL_CRED_18                              0xF88A68

#define mmTPC6_QM_ARB_MST_AVAIL_CRED_19                              0xF88A6C

#define mmTPC6_QM_ARB_MST_AVAIL_CRED_20                              0xF88A70

#define mmTPC6_QM_ARB_MST_AVAIL_CRED_21                              0xF88A74

#define mmTPC6_QM_ARB_MST_AVAIL_CRED_22                              0xF88A78

#define mmTPC6_QM_ARB_MST_AVAIL_CRED_23                              0xF88A7C

#define mmTPC6_QM_ARB_MST_AVAIL_CRED_24                              0xF88A80

#define mmTPC6_QM_ARB_MST_AVAIL_CRED_25                              0xF88A84

#define mmTPC6_QM_ARB_MST_AVAIL_CRED_26                              0xF88A88

#define mmTPC6_QM_ARB_MST_AVAIL_CRED_27                              0xF88A8C

#define mmTPC6_QM_ARB_MST_AVAIL_CRED_28                              0xF88A90

#define mmTPC6_QM_ARB_MST_AVAIL_CRED_29                              0xF88A94

#define mmTPC6_QM_ARB_MST_AVAIL_CRED_30                              0xF88A98

#define mmTPC6_QM_ARB_MST_AVAIL_CRED_31                              0xF88A9C

#define mmTPC6_QM_ARB_MST_CRED_INC                                   0xF88AA0

#define mmTPC6_QM_ARB_MST_CHOISE_PUSH_OFST_0                         0xF88AA4

#define mmTPC6_QM_ARB_MST_CHOISE_PUSH_OFST_1                         0xF88AA8

#define mmTPC6_QM_ARB_MST_CHOISE_PUSH_OFST_2                         0xF88AAC

#define mmTPC6_QM_ARB_MST_CHOISE_PUSH_OFST_3                         0xF88AB0

#define mmTPC6_QM_ARB_MST_CHOISE_PUSH_OFST_4                         0xF88AB4

#define mmTPC6_QM_ARB_MST_CHOISE_PUSH_OFST_5                         0xF88AB8

#define mmTPC6_QM_ARB_MST_CHOISE_PUSH_OFST_6                         0xF88ABC

#define mmTPC6_QM_ARB_MST_CHOISE_PUSH_OFST_7                         0xF88AC0

#define mmTPC6_QM_ARB_MST_CHOISE_PUSH_OFST_8                         0xF88AC4

#define mmTPC6_QM_ARB_MST_CHOISE_PUSH_OFST_9                         0xF88AC8

#define mmTPC6_QM_ARB_MST_CHOISE_PUSH_OFST_10                        0xF88ACC

#define mmTPC6_QM_ARB_MST_CHOISE_PUSH_OFST_11                        0xF88AD0

#define mmTPC6_QM_ARB_MST_CHOISE_PUSH_OFST_12                        0xF88AD4

#define mmTPC6_QM_ARB_MST_CHOISE_PUSH_OFST_13                        0xF88AD8

#define mmTPC6_QM_ARB_MST_CHOISE_PUSH_OFST_14                        0xF88ADC

#define mmTPC6_QM_ARB_MST_CHOISE_PUSH_OFST_15                        0xF88AE0

#define mmTPC6_QM_ARB_MST_CHOISE_PUSH_OFST_16                        0xF88AE4

#define mmTPC6_QM_ARB_MST_CHOISE_PUSH_OFST_17                        0xF88AE8

#define mmTPC6_QM_ARB_MST_CHOISE_PUSH_OFST_18                        0xF88AEC

#define mmTPC6_QM_ARB_MST_CHOISE_PUSH_OFST_19                        0xF88AF0

#define mmTPC6_QM_ARB_MST_CHOISE_PUSH_OFST_20                        0xF88AF4

#define mmTPC6_QM_ARB_MST_CHOISE_PUSH_OFST_21                        0xF88AF8

#define mmTPC6_QM_ARB_MST_CHOISE_PUSH_OFST_22                        0xF88AFC

#define mmTPC6_QM_ARB_MST_CHOISE_PUSH_OFST_23                        0xF88B00

#define mmTPC6_QM_ARB_MST_CHOISE_PUSH_OFST_24                        0xF88B04

#define mmTPC6_QM_ARB_MST_CHOISE_PUSH_OFST_25                        0xF88B08

#define mmTPC6_QM_ARB_MST_CHOISE_PUSH_OFST_26                        0xF88B0C

#define mmTPC6_QM_ARB_MST_CHOISE_PUSH_OFST_27                        0xF88B10

#define mmTPC6_QM_ARB_MST_CHOISE_PUSH_OFST_28                        0xF88B14

#define mmTPC6_QM_ARB_MST_CHOISE_PUSH_OFST_29                        0xF88B18

#define mmTPC6_QM_ARB_MST_CHOISE_PUSH_OFST_30                        0xF88B1C

#define mmTPC6_QM_ARB_MST_CHOISE_PUSH_OFST_31                        0xF88B20

#define mmTPC6_QM_ARB_SLV_MASTER_INC_CRED_OFST                       0xF88B28

#define mmTPC6_QM_ARB_MST_SLAVE_EN                                   0xF88B2C

#define mmTPC6_QM_ARB_MST_QUIET_PER                                  0xF88B34

#define mmTPC6_QM_ARB_SLV_CHOISE_WDT                                 0xF88B38

#define mmTPC6_QM_ARB_SLV_ID                                         0xF88B3C

#define mmTPC6_QM_ARB_MSG_MAX_INFLIGHT                               0xF88B44

#define mmTPC6_QM_ARB_MSG_AWUSER_31_11                               0xF88B48

#define mmTPC6_QM_ARB_MSG_AWUSER_SEC_PROP                            0xF88B4C

#define mmTPC6_QM_ARB_MSG_AWUSER_NON_SEC_PROP                        0xF88B50

#define mmTPC6_QM_ARB_BASE_LO                                        0xF88B54

#define mmTPC6_QM_ARB_BASE_HI                                        0xF88B58

#define mmTPC6_QM_ARB_STATE_STS                                      0xF88B80

#define mmTPC6_QM_ARB_CHOISE_FULLNESS_STS                            0xF88B84

#define mmTPC6_QM_ARB_MSG_STS                                        0xF88B88

#define mmTPC6_QM_ARB_SLV_CHOISE_Q_HEAD                              0xF88B8C

#define mmTPC6_QM_ARB_ERR_CAUSE                                      0xF88B9C

#define mmTPC6_QM_ARB_ERR_MSG_EN                                     0xF88BA0

#define mmTPC6_QM_ARB_ERR_STS_DRP                                    0xF88BA8

#define mmTPC6_QM_ARB_MST_CRED_STS_0                                 0xF88BB0

#define mmTPC6_QM_ARB_MST_CRED_STS_1                                 0xF88BB4

#define mmTPC6_QM_ARB_MST_CRED_STS_2                                 0xF88BB8

#define mmTPC6_QM_ARB_MST_CRED_STS_3                                 0xF88BBC

#define mmTPC6_QM_ARB_MST_CRED_STS_4                                 0xF88BC0

#define mmTPC6_QM_ARB_MST_CRED_STS_5                                 0xF88BC4

#define mmTPC6_QM_ARB_MST_CRED_STS_6                                 0xF88BC8

#define mmTPC6_QM_ARB_MST_CRED_STS_7                                 0xF88BCC

#define mmTPC6_QM_ARB_MST_CRED_STS_8                                 0xF88BD0

#define mmTPC6_QM_ARB_MST_CRED_STS_9                                 0xF88BD4

#define mmTPC6_QM_ARB_MST_CRED_STS_10                                0xF88BD8

#define mmTPC6_QM_ARB_MST_CRED_STS_11                                0xF88BDC

#define mmTPC6_QM_ARB_MST_CRED_STS_12                                0xF88BE0

#define mmTPC6_QM_ARB_MST_CRED_STS_13                                0xF88BE4

#define mmTPC6_QM_ARB_MST_CRED_STS_14                                0xF88BE8

#define mmTPC6_QM_ARB_MST_CRED_STS_15                                0xF88BEC

#define mmTPC6_QM_ARB_MST_CRED_STS_16                                0xF88BF0

#define mmTPC6_QM_ARB_MST_CRED_STS_17                                0xF88BF4

#define mmTPC6_QM_ARB_MST_CRED_STS_18                                0xF88BF8

#define mmTPC6_QM_ARB_MST_CRED_STS_19                                0xF88BFC

#define mmTPC6_QM_ARB_MST_CRED_STS_20                                0xF88C00

#define mmTPC6_QM_ARB_MST_CRED_STS_21                                0xF88C04

#define mmTPC6_QM_ARB_MST_CRED_STS_22                                0xF88C08

#define mmTPC6_QM_ARB_MST_CRED_STS_23                                0xF88C0C

#define mmTPC6_QM_ARB_MST_CRED_STS_24                                0xF88C10

#define mmTPC6_QM_ARB_MST_CRED_STS_25                                0xF88C14

#define mmTPC6_QM_ARB_MST_CRED_STS_26                                0xF88C18

#define mmTPC6_QM_ARB_MST_CRED_STS_27                                0xF88C1C

#define mmTPC6_QM_ARB_MST_CRED_STS_28                                0xF88C20

#define mmTPC6_QM_ARB_MST_CRED_STS_29                                0xF88C24

#define mmTPC6_QM_ARB_MST_CRED_STS_30                                0xF88C28

#define mmTPC6_QM_ARB_MST_CRED_STS_31                                0xF88C2C

#define mmTPC6_QM_CGM_CFG                                            0xF88C70

#define mmTPC6_QM_CGM_STS                                            0xF88C74

#define mmTPC6_QM_CGM_CFG1                                           0xF88C78

#define mmTPC6_QM_LOCAL_RANGE_BASE                                   0xF88C80

#define mmTPC6_QM_LOCAL_RANGE_SIZE                                   0xF88C84

#define mmTPC6_QM_CSMR_STRICT_PRIO_CFG                               0xF88C90

#define mmTPC6_QM_HBW_RD_RATE_LIM_CFG_1                              0xF88C94

#define mmTPC6_QM_LBW_WR_RATE_LIM_CFG_0                              0xF88C98

#define mmTPC6_QM_LBW_WR_RATE_LIM_CFG_1                              0xF88C9C

#define mmTPC6_QM_HBW_RD_RATE_LIM_CFG_0                              0xF88CA0

#define mmTPC6_QM_GLBL_AXCACHE                                       0xF88CA4

#define mmTPC6_QM_IND_GW_APB_CFG                                     0xF88CB0

#define mmTPC6_QM_IND_GW_APB_WDATA                                   0xF88CB4

#define mmTPC6_QM_IND_GW_APB_RDATA                                   0xF88CB8

#define mmTPC6_QM_IND_GW_APB_STATUS                                  0xF88CBC

#define mmTPC6_QM_GLBL_ERR_ADDR_LO                                   0xF88CD0

#define mmTPC6_QM_GLBL_ERR_ADDR_HI                                   0xF88CD4

#define mmTPC6_QM_GLBL_ERR_WDATA                                     0xF88CD8

#define mmTPC6_QM_GLBL_MEM_INIT_BUSY                                 0xF88D00

#endif /* ASIC_REG_TPC6_QM_REGS_H_ */
