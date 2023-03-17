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

#ifndef ASIC_REG_TPC4_QM_REGS_H_
#define ASIC_REG_TPC4_QM_REGS_H_

/*
 *****************************************
 *   TPC4_QM (Prototype: QMAN)
 *****************************************
 */

#define mmTPC4_QM_GLBL_CFG0                                          0xF08000

#define mmTPC4_QM_GLBL_CFG1                                          0xF08004

#define mmTPC4_QM_GLBL_PROT                                          0xF08008

#define mmTPC4_QM_GLBL_ERR_CFG                                       0xF0800C

#define mmTPC4_QM_GLBL_SECURE_PROPS_0                                0xF08010

#define mmTPC4_QM_GLBL_SECURE_PROPS_1                                0xF08014

#define mmTPC4_QM_GLBL_SECURE_PROPS_2                                0xF08018

#define mmTPC4_QM_GLBL_SECURE_PROPS_3                                0xF0801C

#define mmTPC4_QM_GLBL_SECURE_PROPS_4                                0xF08020

#define mmTPC4_QM_GLBL_NON_SECURE_PROPS_0                            0xF08024

#define mmTPC4_QM_GLBL_NON_SECURE_PROPS_1                            0xF08028

#define mmTPC4_QM_GLBL_NON_SECURE_PROPS_2                            0xF0802C

#define mmTPC4_QM_GLBL_NON_SECURE_PROPS_3                            0xF08030

#define mmTPC4_QM_GLBL_NON_SECURE_PROPS_4                            0xF08034

#define mmTPC4_QM_GLBL_STS0                                          0xF08038

#define mmTPC4_QM_GLBL_STS1_0                                        0xF08040

#define mmTPC4_QM_GLBL_STS1_1                                        0xF08044

#define mmTPC4_QM_GLBL_STS1_2                                        0xF08048

#define mmTPC4_QM_GLBL_STS1_3                                        0xF0804C

#define mmTPC4_QM_GLBL_STS1_4                                        0xF08050

#define mmTPC4_QM_GLBL_MSG_EN_0                                      0xF08054

#define mmTPC4_QM_GLBL_MSG_EN_1                                      0xF08058

#define mmTPC4_QM_GLBL_MSG_EN_2                                      0xF0805C

#define mmTPC4_QM_GLBL_MSG_EN_3                                      0xF08060

#define mmTPC4_QM_GLBL_MSG_EN_4                                      0xF08068

#define mmTPC4_QM_PQ_BASE_LO_0                                       0xF08070

#define mmTPC4_QM_PQ_BASE_LO_1                                       0xF08074

#define mmTPC4_QM_PQ_BASE_LO_2                                       0xF08078

#define mmTPC4_QM_PQ_BASE_LO_3                                       0xF0807C

#define mmTPC4_QM_PQ_BASE_HI_0                                       0xF08080

#define mmTPC4_QM_PQ_BASE_HI_1                                       0xF08084

#define mmTPC4_QM_PQ_BASE_HI_2                                       0xF08088

#define mmTPC4_QM_PQ_BASE_HI_3                                       0xF0808C

#define mmTPC4_QM_PQ_SIZE_0                                          0xF08090

#define mmTPC4_QM_PQ_SIZE_1                                          0xF08094

#define mmTPC4_QM_PQ_SIZE_2                                          0xF08098

#define mmTPC4_QM_PQ_SIZE_3                                          0xF0809C

#define mmTPC4_QM_PQ_PI_0                                            0xF080A0

#define mmTPC4_QM_PQ_PI_1                                            0xF080A4

#define mmTPC4_QM_PQ_PI_2                                            0xF080A8

#define mmTPC4_QM_PQ_PI_3                                            0xF080AC

#define mmTPC4_QM_PQ_CI_0                                            0xF080B0

#define mmTPC4_QM_PQ_CI_1                                            0xF080B4

#define mmTPC4_QM_PQ_CI_2                                            0xF080B8

#define mmTPC4_QM_PQ_CI_3                                            0xF080BC

#define mmTPC4_QM_PQ_CFG0_0                                          0xF080C0

#define mmTPC4_QM_PQ_CFG0_1                                          0xF080C4

#define mmTPC4_QM_PQ_CFG0_2                                          0xF080C8

#define mmTPC4_QM_PQ_CFG0_3                                          0xF080CC

#define mmTPC4_QM_PQ_CFG1_0                                          0xF080D0

#define mmTPC4_QM_PQ_CFG1_1                                          0xF080D4

#define mmTPC4_QM_PQ_CFG1_2                                          0xF080D8

#define mmTPC4_QM_PQ_CFG1_3                                          0xF080DC

#define mmTPC4_QM_PQ_ARUSER_31_11_0                                  0xF080E0

#define mmTPC4_QM_PQ_ARUSER_31_11_1                                  0xF080E4

#define mmTPC4_QM_PQ_ARUSER_31_11_2                                  0xF080E8

#define mmTPC4_QM_PQ_ARUSER_31_11_3                                  0xF080EC

#define mmTPC4_QM_PQ_STS0_0                                          0xF080F0

#define mmTPC4_QM_PQ_STS0_1                                          0xF080F4

#define mmTPC4_QM_PQ_STS0_2                                          0xF080F8

#define mmTPC4_QM_PQ_STS0_3                                          0xF080FC

#define mmTPC4_QM_PQ_STS1_0                                          0xF08100

#define mmTPC4_QM_PQ_STS1_1                                          0xF08104

#define mmTPC4_QM_PQ_STS1_2                                          0xF08108

#define mmTPC4_QM_PQ_STS1_3                                          0xF0810C

#define mmTPC4_QM_CQ_CFG0_0                                          0xF08110

#define mmTPC4_QM_CQ_CFG0_1                                          0xF08114

#define mmTPC4_QM_CQ_CFG0_2                                          0xF08118

#define mmTPC4_QM_CQ_CFG0_3                                          0xF0811C

#define mmTPC4_QM_CQ_CFG0_4                                          0xF08120

#define mmTPC4_QM_CQ_CFG1_0                                          0xF08124

#define mmTPC4_QM_CQ_CFG1_1                                          0xF08128

#define mmTPC4_QM_CQ_CFG1_2                                          0xF0812C

#define mmTPC4_QM_CQ_CFG1_3                                          0xF08130

#define mmTPC4_QM_CQ_CFG1_4                                          0xF08134

#define mmTPC4_QM_CQ_ARUSER_31_11_0                                  0xF08138

#define mmTPC4_QM_CQ_ARUSER_31_11_1                                  0xF0813C

#define mmTPC4_QM_CQ_ARUSER_31_11_2                                  0xF08140

#define mmTPC4_QM_CQ_ARUSER_31_11_3                                  0xF08144

#define mmTPC4_QM_CQ_ARUSER_31_11_4                                  0xF08148

#define mmTPC4_QM_CQ_STS0_0                                          0xF0814C

#define mmTPC4_QM_CQ_STS0_1                                          0xF08150

#define mmTPC4_QM_CQ_STS0_2                                          0xF08154

#define mmTPC4_QM_CQ_STS0_3                                          0xF08158

#define mmTPC4_QM_CQ_STS0_4                                          0xF0815C

#define mmTPC4_QM_CQ_STS1_0                                          0xF08160

#define mmTPC4_QM_CQ_STS1_1                                          0xF08164

#define mmTPC4_QM_CQ_STS1_2                                          0xF08168

#define mmTPC4_QM_CQ_STS1_3                                          0xF0816C

#define mmTPC4_QM_CQ_STS1_4                                          0xF08170

#define mmTPC4_QM_CQ_PTR_LO_0                                        0xF08174

#define mmTPC4_QM_CQ_PTR_HI_0                                        0xF08178

#define mmTPC4_QM_CQ_TSIZE_0                                         0xF0817C

#define mmTPC4_QM_CQ_CTL_0                                           0xF08180

#define mmTPC4_QM_CQ_PTR_LO_1                                        0xF08184

#define mmTPC4_QM_CQ_PTR_HI_1                                        0xF08188

#define mmTPC4_QM_CQ_TSIZE_1                                         0xF0818C

#define mmTPC4_QM_CQ_CTL_1                                           0xF08190

#define mmTPC4_QM_CQ_PTR_LO_2                                        0xF08194

#define mmTPC4_QM_CQ_PTR_HI_2                                        0xF08198

#define mmTPC4_QM_CQ_TSIZE_2                                         0xF0819C

#define mmTPC4_QM_CQ_CTL_2                                           0xF081A0

#define mmTPC4_QM_CQ_PTR_LO_3                                        0xF081A4

#define mmTPC4_QM_CQ_PTR_HI_3                                        0xF081A8

#define mmTPC4_QM_CQ_TSIZE_3                                         0xF081AC

#define mmTPC4_QM_CQ_CTL_3                                           0xF081B0

#define mmTPC4_QM_CQ_PTR_LO_4                                        0xF081B4

#define mmTPC4_QM_CQ_PTR_HI_4                                        0xF081B8

#define mmTPC4_QM_CQ_TSIZE_4                                         0xF081BC

#define mmTPC4_QM_CQ_CTL_4                                           0xF081C0

#define mmTPC4_QM_CQ_PTR_LO_STS_0                                    0xF081C4

#define mmTPC4_QM_CQ_PTR_LO_STS_1                                    0xF081C8

#define mmTPC4_QM_CQ_PTR_LO_STS_2                                    0xF081CC

#define mmTPC4_QM_CQ_PTR_LO_STS_3                                    0xF081D0

#define mmTPC4_QM_CQ_PTR_LO_STS_4                                    0xF081D4

#define mmTPC4_QM_CQ_PTR_HI_STS_0                                    0xF081D8

#define mmTPC4_QM_CQ_PTR_HI_STS_1                                    0xF081DC

#define mmTPC4_QM_CQ_PTR_HI_STS_2                                    0xF081E0

#define mmTPC4_QM_CQ_PTR_HI_STS_3                                    0xF081E4

#define mmTPC4_QM_CQ_PTR_HI_STS_4                                    0xF081E8

#define mmTPC4_QM_CQ_TSIZE_STS_0                                     0xF081EC

#define mmTPC4_QM_CQ_TSIZE_STS_1                                     0xF081F0

#define mmTPC4_QM_CQ_TSIZE_STS_2                                     0xF081F4

#define mmTPC4_QM_CQ_TSIZE_STS_3                                     0xF081F8

#define mmTPC4_QM_CQ_TSIZE_STS_4                                     0xF081FC

#define mmTPC4_QM_CQ_CTL_STS_0                                       0xF08200

#define mmTPC4_QM_CQ_CTL_STS_1                                       0xF08204

#define mmTPC4_QM_CQ_CTL_STS_2                                       0xF08208

#define mmTPC4_QM_CQ_CTL_STS_3                                       0xF0820C

#define mmTPC4_QM_CQ_CTL_STS_4                                       0xF08210

#define mmTPC4_QM_CQ_IFIFO_CNT_0                                     0xF08214

#define mmTPC4_QM_CQ_IFIFO_CNT_1                                     0xF08218

#define mmTPC4_QM_CQ_IFIFO_CNT_2                                     0xF0821C

#define mmTPC4_QM_CQ_IFIFO_CNT_3                                     0xF08220

#define mmTPC4_QM_CQ_IFIFO_CNT_4                                     0xF08224

#define mmTPC4_QM_CP_MSG_BASE0_ADDR_LO_0                             0xF08228

#define mmTPC4_QM_CP_MSG_BASE0_ADDR_LO_1                             0xF0822C

#define mmTPC4_QM_CP_MSG_BASE0_ADDR_LO_2                             0xF08230

#define mmTPC4_QM_CP_MSG_BASE0_ADDR_LO_3                             0xF08234

#define mmTPC4_QM_CP_MSG_BASE0_ADDR_LO_4                             0xF08238

#define mmTPC4_QM_CP_MSG_BASE0_ADDR_HI_0                             0xF0823C

#define mmTPC4_QM_CP_MSG_BASE0_ADDR_HI_1                             0xF08240

#define mmTPC4_QM_CP_MSG_BASE0_ADDR_HI_2                             0xF08244

#define mmTPC4_QM_CP_MSG_BASE0_ADDR_HI_3                             0xF08248

#define mmTPC4_QM_CP_MSG_BASE0_ADDR_HI_4                             0xF0824C

#define mmTPC4_QM_CP_MSG_BASE1_ADDR_LO_0                             0xF08250

#define mmTPC4_QM_CP_MSG_BASE1_ADDR_LO_1                             0xF08254

#define mmTPC4_QM_CP_MSG_BASE1_ADDR_LO_2                             0xF08258

#define mmTPC4_QM_CP_MSG_BASE1_ADDR_LO_3                             0xF0825C

#define mmTPC4_QM_CP_MSG_BASE1_ADDR_LO_4                             0xF08260

#define mmTPC4_QM_CP_MSG_BASE1_ADDR_HI_0                             0xF08264

#define mmTPC4_QM_CP_MSG_BASE1_ADDR_HI_1                             0xF08268

#define mmTPC4_QM_CP_MSG_BASE1_ADDR_HI_2                             0xF0826C

#define mmTPC4_QM_CP_MSG_BASE1_ADDR_HI_3                             0xF08270

#define mmTPC4_QM_CP_MSG_BASE1_ADDR_HI_4                             0xF08274

#define mmTPC4_QM_CP_MSG_BASE2_ADDR_LO_0                             0xF08278

#define mmTPC4_QM_CP_MSG_BASE2_ADDR_LO_1                             0xF0827C

#define mmTPC4_QM_CP_MSG_BASE2_ADDR_LO_2                             0xF08280

#define mmTPC4_QM_CP_MSG_BASE2_ADDR_LO_3                             0xF08284

#define mmTPC4_QM_CP_MSG_BASE2_ADDR_LO_4                             0xF08288

#define mmTPC4_QM_CP_MSG_BASE2_ADDR_HI_0                             0xF0828C

#define mmTPC4_QM_CP_MSG_BASE2_ADDR_HI_1                             0xF08290

#define mmTPC4_QM_CP_MSG_BASE2_ADDR_HI_2                             0xF08294

#define mmTPC4_QM_CP_MSG_BASE2_ADDR_HI_3                             0xF08298

#define mmTPC4_QM_CP_MSG_BASE2_ADDR_HI_4                             0xF0829C

#define mmTPC4_QM_CP_MSG_BASE3_ADDR_LO_0                             0xF082A0

#define mmTPC4_QM_CP_MSG_BASE3_ADDR_LO_1                             0xF082A4

#define mmTPC4_QM_CP_MSG_BASE3_ADDR_LO_2                             0xF082A8

#define mmTPC4_QM_CP_MSG_BASE3_ADDR_LO_3                             0xF082AC

#define mmTPC4_QM_CP_MSG_BASE3_ADDR_LO_4                             0xF082B0

#define mmTPC4_QM_CP_MSG_BASE3_ADDR_HI_0                             0xF082B4

#define mmTPC4_QM_CP_MSG_BASE3_ADDR_HI_1                             0xF082B8

#define mmTPC4_QM_CP_MSG_BASE3_ADDR_HI_2                             0xF082BC

#define mmTPC4_QM_CP_MSG_BASE3_ADDR_HI_3                             0xF082C0

#define mmTPC4_QM_CP_MSG_BASE3_ADDR_HI_4                             0xF082C4

#define mmTPC4_QM_CP_LDMA_TSIZE_OFFSET_0                             0xF082C8

#define mmTPC4_QM_CP_LDMA_TSIZE_OFFSET_1                             0xF082CC

#define mmTPC4_QM_CP_LDMA_TSIZE_OFFSET_2                             0xF082D0

#define mmTPC4_QM_CP_LDMA_TSIZE_OFFSET_3                             0xF082D4

#define mmTPC4_QM_CP_LDMA_TSIZE_OFFSET_4                             0xF082D8

#define mmTPC4_QM_CP_LDMA_SRC_BASE_LO_OFFSET_0                       0xF082E0

#define mmTPC4_QM_CP_LDMA_SRC_BASE_LO_OFFSET_1                       0xF082E4

#define mmTPC4_QM_CP_LDMA_SRC_BASE_LO_OFFSET_2                       0xF082E8

#define mmTPC4_QM_CP_LDMA_SRC_BASE_LO_OFFSET_3                       0xF082EC

#define mmTPC4_QM_CP_LDMA_SRC_BASE_LO_OFFSET_4                       0xF082F0

#define mmTPC4_QM_CP_LDMA_DST_BASE_LO_OFFSET_0                       0xF082F4

#define mmTPC4_QM_CP_LDMA_DST_BASE_LO_OFFSET_1                       0xF082F8

#define mmTPC4_QM_CP_LDMA_DST_BASE_LO_OFFSET_2                       0xF082FC

#define mmTPC4_QM_CP_LDMA_DST_BASE_LO_OFFSET_3                       0xF08300

#define mmTPC4_QM_CP_LDMA_DST_BASE_LO_OFFSET_4                       0xF08304

#define mmTPC4_QM_CP_FENCE0_RDATA_0                                  0xF08308

#define mmTPC4_QM_CP_FENCE0_RDATA_1                                  0xF0830C

#define mmTPC4_QM_CP_FENCE0_RDATA_2                                  0xF08310

#define mmTPC4_QM_CP_FENCE0_RDATA_3                                  0xF08314

#define mmTPC4_QM_CP_FENCE0_RDATA_4                                  0xF08318

#define mmTPC4_QM_CP_FENCE1_RDATA_0                                  0xF0831C

#define mmTPC4_QM_CP_FENCE1_RDATA_1                                  0xF08320

#define mmTPC4_QM_CP_FENCE1_RDATA_2                                  0xF08324

#define mmTPC4_QM_CP_FENCE1_RDATA_3                                  0xF08328

#define mmTPC4_QM_CP_FENCE1_RDATA_4                                  0xF0832C

#define mmTPC4_QM_CP_FENCE2_RDATA_0                                  0xF08330

#define mmTPC4_QM_CP_FENCE2_RDATA_1                                  0xF08334

#define mmTPC4_QM_CP_FENCE2_RDATA_2                                  0xF08338

#define mmTPC4_QM_CP_FENCE2_RDATA_3                                  0xF0833C

#define mmTPC4_QM_CP_FENCE2_RDATA_4                                  0xF08340

#define mmTPC4_QM_CP_FENCE3_RDATA_0                                  0xF08344

#define mmTPC4_QM_CP_FENCE3_RDATA_1                                  0xF08348

#define mmTPC4_QM_CP_FENCE3_RDATA_2                                  0xF0834C

#define mmTPC4_QM_CP_FENCE3_RDATA_3                                  0xF08350

#define mmTPC4_QM_CP_FENCE3_RDATA_4                                  0xF08354

#define mmTPC4_QM_CP_FENCE0_CNT_0                                    0xF08358

#define mmTPC4_QM_CP_FENCE0_CNT_1                                    0xF0835C

#define mmTPC4_QM_CP_FENCE0_CNT_2                                    0xF08360

#define mmTPC4_QM_CP_FENCE0_CNT_3                                    0xF08364

#define mmTPC4_QM_CP_FENCE0_CNT_4                                    0xF08368

#define mmTPC4_QM_CP_FENCE1_CNT_0                                    0xF0836C

#define mmTPC4_QM_CP_FENCE1_CNT_1                                    0xF08370

#define mmTPC4_QM_CP_FENCE1_CNT_2                                    0xF08374

#define mmTPC4_QM_CP_FENCE1_CNT_3                                    0xF08378

#define mmTPC4_QM_CP_FENCE1_CNT_4                                    0xF0837C

#define mmTPC4_QM_CP_FENCE2_CNT_0                                    0xF08380

#define mmTPC4_QM_CP_FENCE2_CNT_1                                    0xF08384

#define mmTPC4_QM_CP_FENCE2_CNT_2                                    0xF08388

#define mmTPC4_QM_CP_FENCE2_CNT_3                                    0xF0838C

#define mmTPC4_QM_CP_FENCE2_CNT_4                                    0xF08390

#define mmTPC4_QM_CP_FENCE3_CNT_0                                    0xF08394

#define mmTPC4_QM_CP_FENCE3_CNT_1                                    0xF08398

#define mmTPC4_QM_CP_FENCE3_CNT_2                                    0xF0839C

#define mmTPC4_QM_CP_FENCE3_CNT_3                                    0xF083A0

#define mmTPC4_QM_CP_FENCE3_CNT_4                                    0xF083A4

#define mmTPC4_QM_CP_STS_0                                           0xF083A8

#define mmTPC4_QM_CP_STS_1                                           0xF083AC

#define mmTPC4_QM_CP_STS_2                                           0xF083B0

#define mmTPC4_QM_CP_STS_3                                           0xF083B4

#define mmTPC4_QM_CP_STS_4                                           0xF083B8

#define mmTPC4_QM_CP_CURRENT_INST_LO_0                               0xF083BC

#define mmTPC4_QM_CP_CURRENT_INST_LO_1                               0xF083C0

#define mmTPC4_QM_CP_CURRENT_INST_LO_2                               0xF083C4

#define mmTPC4_QM_CP_CURRENT_INST_LO_3                               0xF083C8

#define mmTPC4_QM_CP_CURRENT_INST_LO_4                               0xF083CC

#define mmTPC4_QM_CP_CURRENT_INST_HI_0                               0xF083D0

#define mmTPC4_QM_CP_CURRENT_INST_HI_1                               0xF083D4

#define mmTPC4_QM_CP_CURRENT_INST_HI_2                               0xF083D8

#define mmTPC4_QM_CP_CURRENT_INST_HI_3                               0xF083DC

#define mmTPC4_QM_CP_CURRENT_INST_HI_4                               0xF083E0

#define mmTPC4_QM_CP_BARRIER_CFG_0                                   0xF083F4

#define mmTPC4_QM_CP_BARRIER_CFG_1                                   0xF083F8

#define mmTPC4_QM_CP_BARRIER_CFG_2                                   0xF083FC

#define mmTPC4_QM_CP_BARRIER_CFG_3                                   0xF08400

#define mmTPC4_QM_CP_BARRIER_CFG_4                                   0xF08404

#define mmTPC4_QM_CP_DBG_0_0                                         0xF08408

#define mmTPC4_QM_CP_DBG_0_1                                         0xF0840C

#define mmTPC4_QM_CP_DBG_0_2                                         0xF08410

#define mmTPC4_QM_CP_DBG_0_3                                         0xF08414

#define mmTPC4_QM_CP_DBG_0_4                                         0xF08418

#define mmTPC4_QM_CP_ARUSER_31_11_0                                  0xF0841C

#define mmTPC4_QM_CP_ARUSER_31_11_1                                  0xF08420

#define mmTPC4_QM_CP_ARUSER_31_11_2                                  0xF08424

#define mmTPC4_QM_CP_ARUSER_31_11_3                                  0xF08428

#define mmTPC4_QM_CP_ARUSER_31_11_4                                  0xF0842C

#define mmTPC4_QM_CP_AWUSER_31_11_0                                  0xF08430

#define mmTPC4_QM_CP_AWUSER_31_11_1                                  0xF08434

#define mmTPC4_QM_CP_AWUSER_31_11_2                                  0xF08438

#define mmTPC4_QM_CP_AWUSER_31_11_3                                  0xF0843C

#define mmTPC4_QM_CP_AWUSER_31_11_4                                  0xF08440

#define mmTPC4_QM_ARB_CFG_0                                          0xF08A00

#define mmTPC4_QM_ARB_CHOISE_Q_PUSH                                  0xF08A04

#define mmTPC4_QM_ARB_WRR_WEIGHT_0                                   0xF08A08

#define mmTPC4_QM_ARB_WRR_WEIGHT_1                                   0xF08A0C

#define mmTPC4_QM_ARB_WRR_WEIGHT_2                                   0xF08A10

#define mmTPC4_QM_ARB_WRR_WEIGHT_3                                   0xF08A14

#define mmTPC4_QM_ARB_CFG_1                                          0xF08A18

#define mmTPC4_QM_ARB_MST_AVAIL_CRED_0                               0xF08A20

#define mmTPC4_QM_ARB_MST_AVAIL_CRED_1                               0xF08A24

#define mmTPC4_QM_ARB_MST_AVAIL_CRED_2                               0xF08A28

#define mmTPC4_QM_ARB_MST_AVAIL_CRED_3                               0xF08A2C

#define mmTPC4_QM_ARB_MST_AVAIL_CRED_4                               0xF08A30

#define mmTPC4_QM_ARB_MST_AVAIL_CRED_5                               0xF08A34

#define mmTPC4_QM_ARB_MST_AVAIL_CRED_6                               0xF08A38

#define mmTPC4_QM_ARB_MST_AVAIL_CRED_7                               0xF08A3C

#define mmTPC4_QM_ARB_MST_AVAIL_CRED_8                               0xF08A40

#define mmTPC4_QM_ARB_MST_AVAIL_CRED_9                               0xF08A44

#define mmTPC4_QM_ARB_MST_AVAIL_CRED_10                              0xF08A48

#define mmTPC4_QM_ARB_MST_AVAIL_CRED_11                              0xF08A4C

#define mmTPC4_QM_ARB_MST_AVAIL_CRED_12                              0xF08A50

#define mmTPC4_QM_ARB_MST_AVAIL_CRED_13                              0xF08A54

#define mmTPC4_QM_ARB_MST_AVAIL_CRED_14                              0xF08A58

#define mmTPC4_QM_ARB_MST_AVAIL_CRED_15                              0xF08A5C

#define mmTPC4_QM_ARB_MST_AVAIL_CRED_16                              0xF08A60

#define mmTPC4_QM_ARB_MST_AVAIL_CRED_17                              0xF08A64

#define mmTPC4_QM_ARB_MST_AVAIL_CRED_18                              0xF08A68

#define mmTPC4_QM_ARB_MST_AVAIL_CRED_19                              0xF08A6C

#define mmTPC4_QM_ARB_MST_AVAIL_CRED_20                              0xF08A70

#define mmTPC4_QM_ARB_MST_AVAIL_CRED_21                              0xF08A74

#define mmTPC4_QM_ARB_MST_AVAIL_CRED_22                              0xF08A78

#define mmTPC4_QM_ARB_MST_AVAIL_CRED_23                              0xF08A7C

#define mmTPC4_QM_ARB_MST_AVAIL_CRED_24                              0xF08A80

#define mmTPC4_QM_ARB_MST_AVAIL_CRED_25                              0xF08A84

#define mmTPC4_QM_ARB_MST_AVAIL_CRED_26                              0xF08A88

#define mmTPC4_QM_ARB_MST_AVAIL_CRED_27                              0xF08A8C

#define mmTPC4_QM_ARB_MST_AVAIL_CRED_28                              0xF08A90

#define mmTPC4_QM_ARB_MST_AVAIL_CRED_29                              0xF08A94

#define mmTPC4_QM_ARB_MST_AVAIL_CRED_30                              0xF08A98

#define mmTPC4_QM_ARB_MST_AVAIL_CRED_31                              0xF08A9C

#define mmTPC4_QM_ARB_MST_CRED_INC                                   0xF08AA0

#define mmTPC4_QM_ARB_MST_CHOISE_PUSH_OFST_0                         0xF08AA4

#define mmTPC4_QM_ARB_MST_CHOISE_PUSH_OFST_1                         0xF08AA8

#define mmTPC4_QM_ARB_MST_CHOISE_PUSH_OFST_2                         0xF08AAC

#define mmTPC4_QM_ARB_MST_CHOISE_PUSH_OFST_3                         0xF08AB0

#define mmTPC4_QM_ARB_MST_CHOISE_PUSH_OFST_4                         0xF08AB4

#define mmTPC4_QM_ARB_MST_CHOISE_PUSH_OFST_5                         0xF08AB8

#define mmTPC4_QM_ARB_MST_CHOISE_PUSH_OFST_6                         0xF08ABC

#define mmTPC4_QM_ARB_MST_CHOISE_PUSH_OFST_7                         0xF08AC0

#define mmTPC4_QM_ARB_MST_CHOISE_PUSH_OFST_8                         0xF08AC4

#define mmTPC4_QM_ARB_MST_CHOISE_PUSH_OFST_9                         0xF08AC8

#define mmTPC4_QM_ARB_MST_CHOISE_PUSH_OFST_10                        0xF08ACC

#define mmTPC4_QM_ARB_MST_CHOISE_PUSH_OFST_11                        0xF08AD0

#define mmTPC4_QM_ARB_MST_CHOISE_PUSH_OFST_12                        0xF08AD4

#define mmTPC4_QM_ARB_MST_CHOISE_PUSH_OFST_13                        0xF08AD8

#define mmTPC4_QM_ARB_MST_CHOISE_PUSH_OFST_14                        0xF08ADC

#define mmTPC4_QM_ARB_MST_CHOISE_PUSH_OFST_15                        0xF08AE0

#define mmTPC4_QM_ARB_MST_CHOISE_PUSH_OFST_16                        0xF08AE4

#define mmTPC4_QM_ARB_MST_CHOISE_PUSH_OFST_17                        0xF08AE8

#define mmTPC4_QM_ARB_MST_CHOISE_PUSH_OFST_18                        0xF08AEC

#define mmTPC4_QM_ARB_MST_CHOISE_PUSH_OFST_19                        0xF08AF0

#define mmTPC4_QM_ARB_MST_CHOISE_PUSH_OFST_20                        0xF08AF4

#define mmTPC4_QM_ARB_MST_CHOISE_PUSH_OFST_21                        0xF08AF8

#define mmTPC4_QM_ARB_MST_CHOISE_PUSH_OFST_22                        0xF08AFC

#define mmTPC4_QM_ARB_MST_CHOISE_PUSH_OFST_23                        0xF08B00

#define mmTPC4_QM_ARB_MST_CHOISE_PUSH_OFST_24                        0xF08B04

#define mmTPC4_QM_ARB_MST_CHOISE_PUSH_OFST_25                        0xF08B08

#define mmTPC4_QM_ARB_MST_CHOISE_PUSH_OFST_26                        0xF08B0C

#define mmTPC4_QM_ARB_MST_CHOISE_PUSH_OFST_27                        0xF08B10

#define mmTPC4_QM_ARB_MST_CHOISE_PUSH_OFST_28                        0xF08B14

#define mmTPC4_QM_ARB_MST_CHOISE_PUSH_OFST_29                        0xF08B18

#define mmTPC4_QM_ARB_MST_CHOISE_PUSH_OFST_30                        0xF08B1C

#define mmTPC4_QM_ARB_MST_CHOISE_PUSH_OFST_31                        0xF08B20

#define mmTPC4_QM_ARB_SLV_MASTER_INC_CRED_OFST                       0xF08B28

#define mmTPC4_QM_ARB_MST_SLAVE_EN                                   0xF08B2C

#define mmTPC4_QM_ARB_MST_QUIET_PER                                  0xF08B34

#define mmTPC4_QM_ARB_SLV_CHOISE_WDT                                 0xF08B38

#define mmTPC4_QM_ARB_SLV_ID                                         0xF08B3C

#define mmTPC4_QM_ARB_MSG_MAX_INFLIGHT                               0xF08B44

#define mmTPC4_QM_ARB_MSG_AWUSER_31_11                               0xF08B48

#define mmTPC4_QM_ARB_MSG_AWUSER_SEC_PROP                            0xF08B4C

#define mmTPC4_QM_ARB_MSG_AWUSER_NON_SEC_PROP                        0xF08B50

#define mmTPC4_QM_ARB_BASE_LO                                        0xF08B54

#define mmTPC4_QM_ARB_BASE_HI                                        0xF08B58

#define mmTPC4_QM_ARB_STATE_STS                                      0xF08B80

#define mmTPC4_QM_ARB_CHOISE_FULLNESS_STS                            0xF08B84

#define mmTPC4_QM_ARB_MSG_STS                                        0xF08B88

#define mmTPC4_QM_ARB_SLV_CHOISE_Q_HEAD                              0xF08B8C

#define mmTPC4_QM_ARB_ERR_CAUSE                                      0xF08B9C

#define mmTPC4_QM_ARB_ERR_MSG_EN                                     0xF08BA0

#define mmTPC4_QM_ARB_ERR_STS_DRP                                    0xF08BA8

#define mmTPC4_QM_ARB_MST_CRED_STS_0                                 0xF08BB0

#define mmTPC4_QM_ARB_MST_CRED_STS_1                                 0xF08BB4

#define mmTPC4_QM_ARB_MST_CRED_STS_2                                 0xF08BB8

#define mmTPC4_QM_ARB_MST_CRED_STS_3                                 0xF08BBC

#define mmTPC4_QM_ARB_MST_CRED_STS_4                                 0xF08BC0

#define mmTPC4_QM_ARB_MST_CRED_STS_5                                 0xF08BC4

#define mmTPC4_QM_ARB_MST_CRED_STS_6                                 0xF08BC8

#define mmTPC4_QM_ARB_MST_CRED_STS_7                                 0xF08BCC

#define mmTPC4_QM_ARB_MST_CRED_STS_8                                 0xF08BD0

#define mmTPC4_QM_ARB_MST_CRED_STS_9                                 0xF08BD4

#define mmTPC4_QM_ARB_MST_CRED_STS_10                                0xF08BD8

#define mmTPC4_QM_ARB_MST_CRED_STS_11                                0xF08BDC

#define mmTPC4_QM_ARB_MST_CRED_STS_12                                0xF08BE0

#define mmTPC4_QM_ARB_MST_CRED_STS_13                                0xF08BE4

#define mmTPC4_QM_ARB_MST_CRED_STS_14                                0xF08BE8

#define mmTPC4_QM_ARB_MST_CRED_STS_15                                0xF08BEC

#define mmTPC4_QM_ARB_MST_CRED_STS_16                                0xF08BF0

#define mmTPC4_QM_ARB_MST_CRED_STS_17                                0xF08BF4

#define mmTPC4_QM_ARB_MST_CRED_STS_18                                0xF08BF8

#define mmTPC4_QM_ARB_MST_CRED_STS_19                                0xF08BFC

#define mmTPC4_QM_ARB_MST_CRED_STS_20                                0xF08C00

#define mmTPC4_QM_ARB_MST_CRED_STS_21                                0xF08C04

#define mmTPC4_QM_ARB_MST_CRED_STS_22                                0xF08C08

#define mmTPC4_QM_ARB_MST_CRED_STS_23                                0xF08C0C

#define mmTPC4_QM_ARB_MST_CRED_STS_24                                0xF08C10

#define mmTPC4_QM_ARB_MST_CRED_STS_25                                0xF08C14

#define mmTPC4_QM_ARB_MST_CRED_STS_26                                0xF08C18

#define mmTPC4_QM_ARB_MST_CRED_STS_27                                0xF08C1C

#define mmTPC4_QM_ARB_MST_CRED_STS_28                                0xF08C20

#define mmTPC4_QM_ARB_MST_CRED_STS_29                                0xF08C24

#define mmTPC4_QM_ARB_MST_CRED_STS_30                                0xF08C28

#define mmTPC4_QM_ARB_MST_CRED_STS_31                                0xF08C2C

#define mmTPC4_QM_CGM_CFG                                            0xF08C70

#define mmTPC4_QM_CGM_STS                                            0xF08C74

#define mmTPC4_QM_CGM_CFG1                                           0xF08C78

#define mmTPC4_QM_LOCAL_RANGE_BASE                                   0xF08C80

#define mmTPC4_QM_LOCAL_RANGE_SIZE                                   0xF08C84

#define mmTPC4_QM_CSMR_STRICT_PRIO_CFG                               0xF08C90

#define mmTPC4_QM_HBW_RD_RATE_LIM_CFG_1                              0xF08C94

#define mmTPC4_QM_LBW_WR_RATE_LIM_CFG_0                              0xF08C98

#define mmTPC4_QM_LBW_WR_RATE_LIM_CFG_1                              0xF08C9C

#define mmTPC4_QM_HBW_RD_RATE_LIM_CFG_0                              0xF08CA0

#define mmTPC4_QM_GLBL_AXCACHE                                       0xF08CA4

#define mmTPC4_QM_IND_GW_APB_CFG                                     0xF08CB0

#define mmTPC4_QM_IND_GW_APB_WDATA                                   0xF08CB4

#define mmTPC4_QM_IND_GW_APB_RDATA                                   0xF08CB8

#define mmTPC4_QM_IND_GW_APB_STATUS                                  0xF08CBC

#define mmTPC4_QM_GLBL_ERR_ADDR_LO                                   0xF08CD0

#define mmTPC4_QM_GLBL_ERR_ADDR_HI                                   0xF08CD4

#define mmTPC4_QM_GLBL_ERR_WDATA                                     0xF08CD8

#define mmTPC4_QM_GLBL_MEM_INIT_BUSY                                 0xF08D00

#endif /* ASIC_REG_TPC4_QM_REGS_H_ */
