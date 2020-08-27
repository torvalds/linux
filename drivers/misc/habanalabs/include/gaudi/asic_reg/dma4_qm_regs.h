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

#ifndef ASIC_REG_DMA4_QM_REGS_H_
#define ASIC_REG_DMA4_QM_REGS_H_

/*
 *****************************************
 *   DMA4_QM (Prototype: QMAN)
 *****************************************
 */

#define mmDMA4_QM_GLBL_CFG0                                          0x588000

#define mmDMA4_QM_GLBL_CFG1                                          0x588004

#define mmDMA4_QM_GLBL_PROT                                          0x588008

#define mmDMA4_QM_GLBL_ERR_CFG                                       0x58800C

#define mmDMA4_QM_GLBL_SECURE_PROPS_0                                0x588010

#define mmDMA4_QM_GLBL_SECURE_PROPS_1                                0x588014

#define mmDMA4_QM_GLBL_SECURE_PROPS_2                                0x588018

#define mmDMA4_QM_GLBL_SECURE_PROPS_3                                0x58801C

#define mmDMA4_QM_GLBL_SECURE_PROPS_4                                0x588020

#define mmDMA4_QM_GLBL_NON_SECURE_PROPS_0                            0x588024

#define mmDMA4_QM_GLBL_NON_SECURE_PROPS_1                            0x588028

#define mmDMA4_QM_GLBL_NON_SECURE_PROPS_2                            0x58802C

#define mmDMA4_QM_GLBL_NON_SECURE_PROPS_3                            0x588030

#define mmDMA4_QM_GLBL_NON_SECURE_PROPS_4                            0x588034

#define mmDMA4_QM_GLBL_STS0                                          0x588038

#define mmDMA4_QM_GLBL_STS1_0                                        0x588040

#define mmDMA4_QM_GLBL_STS1_1                                        0x588044

#define mmDMA4_QM_GLBL_STS1_2                                        0x588048

#define mmDMA4_QM_GLBL_STS1_3                                        0x58804C

#define mmDMA4_QM_GLBL_STS1_4                                        0x588050

#define mmDMA4_QM_GLBL_MSG_EN_0                                      0x588054

#define mmDMA4_QM_GLBL_MSG_EN_1                                      0x588058

#define mmDMA4_QM_GLBL_MSG_EN_2                                      0x58805C

#define mmDMA4_QM_GLBL_MSG_EN_3                                      0x588060

#define mmDMA4_QM_GLBL_MSG_EN_4                                      0x588068

#define mmDMA4_QM_PQ_BASE_LO_0                                       0x588070

#define mmDMA4_QM_PQ_BASE_LO_1                                       0x588074

#define mmDMA4_QM_PQ_BASE_LO_2                                       0x588078

#define mmDMA4_QM_PQ_BASE_LO_3                                       0x58807C

#define mmDMA4_QM_PQ_BASE_HI_0                                       0x588080

#define mmDMA4_QM_PQ_BASE_HI_1                                       0x588084

#define mmDMA4_QM_PQ_BASE_HI_2                                       0x588088

#define mmDMA4_QM_PQ_BASE_HI_3                                       0x58808C

#define mmDMA4_QM_PQ_SIZE_0                                          0x588090

#define mmDMA4_QM_PQ_SIZE_1                                          0x588094

#define mmDMA4_QM_PQ_SIZE_2                                          0x588098

#define mmDMA4_QM_PQ_SIZE_3                                          0x58809C

#define mmDMA4_QM_PQ_PI_0                                            0x5880A0

#define mmDMA4_QM_PQ_PI_1                                            0x5880A4

#define mmDMA4_QM_PQ_PI_2                                            0x5880A8

#define mmDMA4_QM_PQ_PI_3                                            0x5880AC

#define mmDMA4_QM_PQ_CI_0                                            0x5880B0

#define mmDMA4_QM_PQ_CI_1                                            0x5880B4

#define mmDMA4_QM_PQ_CI_2                                            0x5880B8

#define mmDMA4_QM_PQ_CI_3                                            0x5880BC

#define mmDMA4_QM_PQ_CFG0_0                                          0x5880C0

#define mmDMA4_QM_PQ_CFG0_1                                          0x5880C4

#define mmDMA4_QM_PQ_CFG0_2                                          0x5880C8

#define mmDMA4_QM_PQ_CFG0_3                                          0x5880CC

#define mmDMA4_QM_PQ_CFG1_0                                          0x5880D0

#define mmDMA4_QM_PQ_CFG1_1                                          0x5880D4

#define mmDMA4_QM_PQ_CFG1_2                                          0x5880D8

#define mmDMA4_QM_PQ_CFG1_3                                          0x5880DC

#define mmDMA4_QM_PQ_ARUSER_31_11_0                                  0x5880E0

#define mmDMA4_QM_PQ_ARUSER_31_11_1                                  0x5880E4

#define mmDMA4_QM_PQ_ARUSER_31_11_2                                  0x5880E8

#define mmDMA4_QM_PQ_ARUSER_31_11_3                                  0x5880EC

#define mmDMA4_QM_PQ_STS0_0                                          0x5880F0

#define mmDMA4_QM_PQ_STS0_1                                          0x5880F4

#define mmDMA4_QM_PQ_STS0_2                                          0x5880F8

#define mmDMA4_QM_PQ_STS0_3                                          0x5880FC

#define mmDMA4_QM_PQ_STS1_0                                          0x588100

#define mmDMA4_QM_PQ_STS1_1                                          0x588104

#define mmDMA4_QM_PQ_STS1_2                                          0x588108

#define mmDMA4_QM_PQ_STS1_3                                          0x58810C

#define mmDMA4_QM_CQ_CFG0_0                                          0x588110

#define mmDMA4_QM_CQ_CFG0_1                                          0x588114

#define mmDMA4_QM_CQ_CFG0_2                                          0x588118

#define mmDMA4_QM_CQ_CFG0_3                                          0x58811C

#define mmDMA4_QM_CQ_CFG0_4                                          0x588120

#define mmDMA4_QM_CQ_CFG1_0                                          0x588124

#define mmDMA4_QM_CQ_CFG1_1                                          0x588128

#define mmDMA4_QM_CQ_CFG1_2                                          0x58812C

#define mmDMA4_QM_CQ_CFG1_3                                          0x588130

#define mmDMA4_QM_CQ_CFG1_4                                          0x588134

#define mmDMA4_QM_CQ_ARUSER_31_11_0                                  0x588138

#define mmDMA4_QM_CQ_ARUSER_31_11_1                                  0x58813C

#define mmDMA4_QM_CQ_ARUSER_31_11_2                                  0x588140

#define mmDMA4_QM_CQ_ARUSER_31_11_3                                  0x588144

#define mmDMA4_QM_CQ_ARUSER_31_11_4                                  0x588148

#define mmDMA4_QM_CQ_STS0_0                                          0x58814C

#define mmDMA4_QM_CQ_STS0_1                                          0x588150

#define mmDMA4_QM_CQ_STS0_2                                          0x588154

#define mmDMA4_QM_CQ_STS0_3                                          0x588158

#define mmDMA4_QM_CQ_STS0_4                                          0x58815C

#define mmDMA4_QM_CQ_STS1_0                                          0x588160

#define mmDMA4_QM_CQ_STS1_1                                          0x588164

#define mmDMA4_QM_CQ_STS1_2                                          0x588168

#define mmDMA4_QM_CQ_STS1_3                                          0x58816C

#define mmDMA4_QM_CQ_STS1_4                                          0x588170

#define mmDMA4_QM_CQ_PTR_LO_0                                        0x588174

#define mmDMA4_QM_CQ_PTR_HI_0                                        0x588178

#define mmDMA4_QM_CQ_TSIZE_0                                         0x58817C

#define mmDMA4_QM_CQ_CTL_0                                           0x588180

#define mmDMA4_QM_CQ_PTR_LO_1                                        0x588184

#define mmDMA4_QM_CQ_PTR_HI_1                                        0x588188

#define mmDMA4_QM_CQ_TSIZE_1                                         0x58818C

#define mmDMA4_QM_CQ_CTL_1                                           0x588190

#define mmDMA4_QM_CQ_PTR_LO_2                                        0x588194

#define mmDMA4_QM_CQ_PTR_HI_2                                        0x588198

#define mmDMA4_QM_CQ_TSIZE_2                                         0x58819C

#define mmDMA4_QM_CQ_CTL_2                                           0x5881A0

#define mmDMA4_QM_CQ_PTR_LO_3                                        0x5881A4

#define mmDMA4_QM_CQ_PTR_HI_3                                        0x5881A8

#define mmDMA4_QM_CQ_TSIZE_3                                         0x5881AC

#define mmDMA4_QM_CQ_CTL_3                                           0x5881B0

#define mmDMA4_QM_CQ_PTR_LO_4                                        0x5881B4

#define mmDMA4_QM_CQ_PTR_HI_4                                        0x5881B8

#define mmDMA4_QM_CQ_TSIZE_4                                         0x5881BC

#define mmDMA4_QM_CQ_CTL_4                                           0x5881C0

#define mmDMA4_QM_CQ_PTR_LO_STS_0                                    0x5881C4

#define mmDMA4_QM_CQ_PTR_LO_STS_1                                    0x5881C8

#define mmDMA4_QM_CQ_PTR_LO_STS_2                                    0x5881CC

#define mmDMA4_QM_CQ_PTR_LO_STS_3                                    0x5881D0

#define mmDMA4_QM_CQ_PTR_LO_STS_4                                    0x5881D4

#define mmDMA4_QM_CQ_PTR_HI_STS_0                                    0x5881D8

#define mmDMA4_QM_CQ_PTR_HI_STS_1                                    0x5881DC

#define mmDMA4_QM_CQ_PTR_HI_STS_2                                    0x5881E0

#define mmDMA4_QM_CQ_PTR_HI_STS_3                                    0x5881E4

#define mmDMA4_QM_CQ_PTR_HI_STS_4                                    0x5881E8

#define mmDMA4_QM_CQ_TSIZE_STS_0                                     0x5881EC

#define mmDMA4_QM_CQ_TSIZE_STS_1                                     0x5881F0

#define mmDMA4_QM_CQ_TSIZE_STS_2                                     0x5881F4

#define mmDMA4_QM_CQ_TSIZE_STS_3                                     0x5881F8

#define mmDMA4_QM_CQ_TSIZE_STS_4                                     0x5881FC

#define mmDMA4_QM_CQ_CTL_STS_0                                       0x588200

#define mmDMA4_QM_CQ_CTL_STS_1                                       0x588204

#define mmDMA4_QM_CQ_CTL_STS_2                                       0x588208

#define mmDMA4_QM_CQ_CTL_STS_3                                       0x58820C

#define mmDMA4_QM_CQ_CTL_STS_4                                       0x588210

#define mmDMA4_QM_CQ_IFIFO_CNT_0                                     0x588214

#define mmDMA4_QM_CQ_IFIFO_CNT_1                                     0x588218

#define mmDMA4_QM_CQ_IFIFO_CNT_2                                     0x58821C

#define mmDMA4_QM_CQ_IFIFO_CNT_3                                     0x588220

#define mmDMA4_QM_CQ_IFIFO_CNT_4                                     0x588224

#define mmDMA4_QM_CP_MSG_BASE0_ADDR_LO_0                             0x588228

#define mmDMA4_QM_CP_MSG_BASE0_ADDR_LO_1                             0x58822C

#define mmDMA4_QM_CP_MSG_BASE0_ADDR_LO_2                             0x588230

#define mmDMA4_QM_CP_MSG_BASE0_ADDR_LO_3                             0x588234

#define mmDMA4_QM_CP_MSG_BASE0_ADDR_LO_4                             0x588238

#define mmDMA4_QM_CP_MSG_BASE0_ADDR_HI_0                             0x58823C

#define mmDMA4_QM_CP_MSG_BASE0_ADDR_HI_1                             0x588240

#define mmDMA4_QM_CP_MSG_BASE0_ADDR_HI_2                             0x588244

#define mmDMA4_QM_CP_MSG_BASE0_ADDR_HI_3                             0x588248

#define mmDMA4_QM_CP_MSG_BASE0_ADDR_HI_4                             0x58824C

#define mmDMA4_QM_CP_MSG_BASE1_ADDR_LO_0                             0x588250

#define mmDMA4_QM_CP_MSG_BASE1_ADDR_LO_1                             0x588254

#define mmDMA4_QM_CP_MSG_BASE1_ADDR_LO_2                             0x588258

#define mmDMA4_QM_CP_MSG_BASE1_ADDR_LO_3                             0x58825C

#define mmDMA4_QM_CP_MSG_BASE1_ADDR_LO_4                             0x588260

#define mmDMA4_QM_CP_MSG_BASE1_ADDR_HI_0                             0x588264

#define mmDMA4_QM_CP_MSG_BASE1_ADDR_HI_1                             0x588268

#define mmDMA4_QM_CP_MSG_BASE1_ADDR_HI_2                             0x58826C

#define mmDMA4_QM_CP_MSG_BASE1_ADDR_HI_3                             0x588270

#define mmDMA4_QM_CP_MSG_BASE1_ADDR_HI_4                             0x588274

#define mmDMA4_QM_CP_MSG_BASE2_ADDR_LO_0                             0x588278

#define mmDMA4_QM_CP_MSG_BASE2_ADDR_LO_1                             0x58827C

#define mmDMA4_QM_CP_MSG_BASE2_ADDR_LO_2                             0x588280

#define mmDMA4_QM_CP_MSG_BASE2_ADDR_LO_3                             0x588284

#define mmDMA4_QM_CP_MSG_BASE2_ADDR_LO_4                             0x588288

#define mmDMA4_QM_CP_MSG_BASE2_ADDR_HI_0                             0x58828C

#define mmDMA4_QM_CP_MSG_BASE2_ADDR_HI_1                             0x588290

#define mmDMA4_QM_CP_MSG_BASE2_ADDR_HI_2                             0x588294

#define mmDMA4_QM_CP_MSG_BASE2_ADDR_HI_3                             0x588298

#define mmDMA4_QM_CP_MSG_BASE2_ADDR_HI_4                             0x58829C

#define mmDMA4_QM_CP_MSG_BASE3_ADDR_LO_0                             0x5882A0

#define mmDMA4_QM_CP_MSG_BASE3_ADDR_LO_1                             0x5882A4

#define mmDMA4_QM_CP_MSG_BASE3_ADDR_LO_2                             0x5882A8

#define mmDMA4_QM_CP_MSG_BASE3_ADDR_LO_3                             0x5882AC

#define mmDMA4_QM_CP_MSG_BASE3_ADDR_LO_4                             0x5882B0

#define mmDMA4_QM_CP_MSG_BASE3_ADDR_HI_0                             0x5882B4

#define mmDMA4_QM_CP_MSG_BASE3_ADDR_HI_1                             0x5882B8

#define mmDMA4_QM_CP_MSG_BASE3_ADDR_HI_2                             0x5882BC

#define mmDMA4_QM_CP_MSG_BASE3_ADDR_HI_3                             0x5882C0

#define mmDMA4_QM_CP_MSG_BASE3_ADDR_HI_4                             0x5882C4

#define mmDMA4_QM_CP_LDMA_TSIZE_OFFSET_0                             0x5882C8

#define mmDMA4_QM_CP_LDMA_TSIZE_OFFSET_1                             0x5882CC

#define mmDMA4_QM_CP_LDMA_TSIZE_OFFSET_2                             0x5882D0

#define mmDMA4_QM_CP_LDMA_TSIZE_OFFSET_3                             0x5882D4

#define mmDMA4_QM_CP_LDMA_TSIZE_OFFSET_4                             0x5882D8

#define mmDMA4_QM_CP_LDMA_SRC_BASE_LO_OFFSET_0                       0x5882E0

#define mmDMA4_QM_CP_LDMA_SRC_BASE_LO_OFFSET_1                       0x5882E4

#define mmDMA4_QM_CP_LDMA_SRC_BASE_LO_OFFSET_2                       0x5882E8

#define mmDMA4_QM_CP_LDMA_SRC_BASE_LO_OFFSET_3                       0x5882EC

#define mmDMA4_QM_CP_LDMA_SRC_BASE_LO_OFFSET_4                       0x5882F0

#define mmDMA4_QM_CP_LDMA_DST_BASE_LO_OFFSET_0                       0x5882F4

#define mmDMA4_QM_CP_LDMA_DST_BASE_LO_OFFSET_1                       0x5882F8

#define mmDMA4_QM_CP_LDMA_DST_BASE_LO_OFFSET_2                       0x5882FC

#define mmDMA4_QM_CP_LDMA_DST_BASE_LO_OFFSET_3                       0x588300

#define mmDMA4_QM_CP_LDMA_DST_BASE_LO_OFFSET_4                       0x588304

#define mmDMA4_QM_CP_FENCE0_RDATA_0                                  0x588308

#define mmDMA4_QM_CP_FENCE0_RDATA_1                                  0x58830C

#define mmDMA4_QM_CP_FENCE0_RDATA_2                                  0x588310

#define mmDMA4_QM_CP_FENCE0_RDATA_3                                  0x588314

#define mmDMA4_QM_CP_FENCE0_RDATA_4                                  0x588318

#define mmDMA4_QM_CP_FENCE1_RDATA_0                                  0x58831C

#define mmDMA4_QM_CP_FENCE1_RDATA_1                                  0x588320

#define mmDMA4_QM_CP_FENCE1_RDATA_2                                  0x588324

#define mmDMA4_QM_CP_FENCE1_RDATA_3                                  0x588328

#define mmDMA4_QM_CP_FENCE1_RDATA_4                                  0x58832C

#define mmDMA4_QM_CP_FENCE2_RDATA_0                                  0x588330

#define mmDMA4_QM_CP_FENCE2_RDATA_1                                  0x588334

#define mmDMA4_QM_CP_FENCE2_RDATA_2                                  0x588338

#define mmDMA4_QM_CP_FENCE2_RDATA_3                                  0x58833C

#define mmDMA4_QM_CP_FENCE2_RDATA_4                                  0x588340

#define mmDMA4_QM_CP_FENCE3_RDATA_0                                  0x588344

#define mmDMA4_QM_CP_FENCE3_RDATA_1                                  0x588348

#define mmDMA4_QM_CP_FENCE3_RDATA_2                                  0x58834C

#define mmDMA4_QM_CP_FENCE3_RDATA_3                                  0x588350

#define mmDMA4_QM_CP_FENCE3_RDATA_4                                  0x588354

#define mmDMA4_QM_CP_FENCE0_CNT_0                                    0x588358

#define mmDMA4_QM_CP_FENCE0_CNT_1                                    0x58835C

#define mmDMA4_QM_CP_FENCE0_CNT_2                                    0x588360

#define mmDMA4_QM_CP_FENCE0_CNT_3                                    0x588364

#define mmDMA4_QM_CP_FENCE0_CNT_4                                    0x588368

#define mmDMA4_QM_CP_FENCE1_CNT_0                                    0x58836C

#define mmDMA4_QM_CP_FENCE1_CNT_1                                    0x588370

#define mmDMA4_QM_CP_FENCE1_CNT_2                                    0x588374

#define mmDMA4_QM_CP_FENCE1_CNT_3                                    0x588378

#define mmDMA4_QM_CP_FENCE1_CNT_4                                    0x58837C

#define mmDMA4_QM_CP_FENCE2_CNT_0                                    0x588380

#define mmDMA4_QM_CP_FENCE2_CNT_1                                    0x588384

#define mmDMA4_QM_CP_FENCE2_CNT_2                                    0x588388

#define mmDMA4_QM_CP_FENCE2_CNT_3                                    0x58838C

#define mmDMA4_QM_CP_FENCE2_CNT_4                                    0x588390

#define mmDMA4_QM_CP_FENCE3_CNT_0                                    0x588394

#define mmDMA4_QM_CP_FENCE3_CNT_1                                    0x588398

#define mmDMA4_QM_CP_FENCE3_CNT_2                                    0x58839C

#define mmDMA4_QM_CP_FENCE3_CNT_3                                    0x5883A0

#define mmDMA4_QM_CP_FENCE3_CNT_4                                    0x5883A4

#define mmDMA4_QM_CP_STS_0                                           0x5883A8

#define mmDMA4_QM_CP_STS_1                                           0x5883AC

#define mmDMA4_QM_CP_STS_2                                           0x5883B0

#define mmDMA4_QM_CP_STS_3                                           0x5883B4

#define mmDMA4_QM_CP_STS_4                                           0x5883B8

#define mmDMA4_QM_CP_CURRENT_INST_LO_0                               0x5883BC

#define mmDMA4_QM_CP_CURRENT_INST_LO_1                               0x5883C0

#define mmDMA4_QM_CP_CURRENT_INST_LO_2                               0x5883C4

#define mmDMA4_QM_CP_CURRENT_INST_LO_3                               0x5883C8

#define mmDMA4_QM_CP_CURRENT_INST_LO_4                               0x5883CC

#define mmDMA4_QM_CP_CURRENT_INST_HI_0                               0x5883D0

#define mmDMA4_QM_CP_CURRENT_INST_HI_1                               0x5883D4

#define mmDMA4_QM_CP_CURRENT_INST_HI_2                               0x5883D8

#define mmDMA4_QM_CP_CURRENT_INST_HI_3                               0x5883DC

#define mmDMA4_QM_CP_CURRENT_INST_HI_4                               0x5883E0

#define mmDMA4_QM_CP_BARRIER_CFG_0                                   0x5883F4

#define mmDMA4_QM_CP_BARRIER_CFG_1                                   0x5883F8

#define mmDMA4_QM_CP_BARRIER_CFG_2                                   0x5883FC

#define mmDMA4_QM_CP_BARRIER_CFG_3                                   0x588400

#define mmDMA4_QM_CP_BARRIER_CFG_4                                   0x588404

#define mmDMA4_QM_CP_DBG_0_0                                         0x588408

#define mmDMA4_QM_CP_DBG_0_1                                         0x58840C

#define mmDMA4_QM_CP_DBG_0_2                                         0x588410

#define mmDMA4_QM_CP_DBG_0_3                                         0x588414

#define mmDMA4_QM_CP_DBG_0_4                                         0x588418

#define mmDMA4_QM_CP_ARUSER_31_11_0                                  0x58841C

#define mmDMA4_QM_CP_ARUSER_31_11_1                                  0x588420

#define mmDMA4_QM_CP_ARUSER_31_11_2                                  0x588424

#define mmDMA4_QM_CP_ARUSER_31_11_3                                  0x588428

#define mmDMA4_QM_CP_ARUSER_31_11_4                                  0x58842C

#define mmDMA4_QM_CP_AWUSER_31_11_0                                  0x588430

#define mmDMA4_QM_CP_AWUSER_31_11_1                                  0x588434

#define mmDMA4_QM_CP_AWUSER_31_11_2                                  0x588438

#define mmDMA4_QM_CP_AWUSER_31_11_3                                  0x58843C

#define mmDMA4_QM_CP_AWUSER_31_11_4                                  0x588440

#define mmDMA4_QM_ARB_CFG_0                                          0x588A00

#define mmDMA4_QM_ARB_CHOISE_Q_PUSH                                  0x588A04

#define mmDMA4_QM_ARB_WRR_WEIGHT_0                                   0x588A08

#define mmDMA4_QM_ARB_WRR_WEIGHT_1                                   0x588A0C

#define mmDMA4_QM_ARB_WRR_WEIGHT_2                                   0x588A10

#define mmDMA4_QM_ARB_WRR_WEIGHT_3                                   0x588A14

#define mmDMA4_QM_ARB_CFG_1                                          0x588A18

#define mmDMA4_QM_ARB_MST_AVAIL_CRED_0                               0x588A20

#define mmDMA4_QM_ARB_MST_AVAIL_CRED_1                               0x588A24

#define mmDMA4_QM_ARB_MST_AVAIL_CRED_2                               0x588A28

#define mmDMA4_QM_ARB_MST_AVAIL_CRED_3                               0x588A2C

#define mmDMA4_QM_ARB_MST_AVAIL_CRED_4                               0x588A30

#define mmDMA4_QM_ARB_MST_AVAIL_CRED_5                               0x588A34

#define mmDMA4_QM_ARB_MST_AVAIL_CRED_6                               0x588A38

#define mmDMA4_QM_ARB_MST_AVAIL_CRED_7                               0x588A3C

#define mmDMA4_QM_ARB_MST_AVAIL_CRED_8                               0x588A40

#define mmDMA4_QM_ARB_MST_AVAIL_CRED_9                               0x588A44

#define mmDMA4_QM_ARB_MST_AVAIL_CRED_10                              0x588A48

#define mmDMA4_QM_ARB_MST_AVAIL_CRED_11                              0x588A4C

#define mmDMA4_QM_ARB_MST_AVAIL_CRED_12                              0x588A50

#define mmDMA4_QM_ARB_MST_AVAIL_CRED_13                              0x588A54

#define mmDMA4_QM_ARB_MST_AVAIL_CRED_14                              0x588A58

#define mmDMA4_QM_ARB_MST_AVAIL_CRED_15                              0x588A5C

#define mmDMA4_QM_ARB_MST_AVAIL_CRED_16                              0x588A60

#define mmDMA4_QM_ARB_MST_AVAIL_CRED_17                              0x588A64

#define mmDMA4_QM_ARB_MST_AVAIL_CRED_18                              0x588A68

#define mmDMA4_QM_ARB_MST_AVAIL_CRED_19                              0x588A6C

#define mmDMA4_QM_ARB_MST_AVAIL_CRED_20                              0x588A70

#define mmDMA4_QM_ARB_MST_AVAIL_CRED_21                              0x588A74

#define mmDMA4_QM_ARB_MST_AVAIL_CRED_22                              0x588A78

#define mmDMA4_QM_ARB_MST_AVAIL_CRED_23                              0x588A7C

#define mmDMA4_QM_ARB_MST_AVAIL_CRED_24                              0x588A80

#define mmDMA4_QM_ARB_MST_AVAIL_CRED_25                              0x588A84

#define mmDMA4_QM_ARB_MST_AVAIL_CRED_26                              0x588A88

#define mmDMA4_QM_ARB_MST_AVAIL_CRED_27                              0x588A8C

#define mmDMA4_QM_ARB_MST_AVAIL_CRED_28                              0x588A90

#define mmDMA4_QM_ARB_MST_AVAIL_CRED_29                              0x588A94

#define mmDMA4_QM_ARB_MST_AVAIL_CRED_30                              0x588A98

#define mmDMA4_QM_ARB_MST_AVAIL_CRED_31                              0x588A9C

#define mmDMA4_QM_ARB_MST_CRED_INC                                   0x588AA0

#define mmDMA4_QM_ARB_MST_CHOISE_PUSH_OFST_0                         0x588AA4

#define mmDMA4_QM_ARB_MST_CHOISE_PUSH_OFST_1                         0x588AA8

#define mmDMA4_QM_ARB_MST_CHOISE_PUSH_OFST_2                         0x588AAC

#define mmDMA4_QM_ARB_MST_CHOISE_PUSH_OFST_3                         0x588AB0

#define mmDMA4_QM_ARB_MST_CHOISE_PUSH_OFST_4                         0x588AB4

#define mmDMA4_QM_ARB_MST_CHOISE_PUSH_OFST_5                         0x588AB8

#define mmDMA4_QM_ARB_MST_CHOISE_PUSH_OFST_6                         0x588ABC

#define mmDMA4_QM_ARB_MST_CHOISE_PUSH_OFST_7                         0x588AC0

#define mmDMA4_QM_ARB_MST_CHOISE_PUSH_OFST_8                         0x588AC4

#define mmDMA4_QM_ARB_MST_CHOISE_PUSH_OFST_9                         0x588AC8

#define mmDMA4_QM_ARB_MST_CHOISE_PUSH_OFST_10                        0x588ACC

#define mmDMA4_QM_ARB_MST_CHOISE_PUSH_OFST_11                        0x588AD0

#define mmDMA4_QM_ARB_MST_CHOISE_PUSH_OFST_12                        0x588AD4

#define mmDMA4_QM_ARB_MST_CHOISE_PUSH_OFST_13                        0x588AD8

#define mmDMA4_QM_ARB_MST_CHOISE_PUSH_OFST_14                        0x588ADC

#define mmDMA4_QM_ARB_MST_CHOISE_PUSH_OFST_15                        0x588AE0

#define mmDMA4_QM_ARB_MST_CHOISE_PUSH_OFST_16                        0x588AE4

#define mmDMA4_QM_ARB_MST_CHOISE_PUSH_OFST_17                        0x588AE8

#define mmDMA4_QM_ARB_MST_CHOISE_PUSH_OFST_18                        0x588AEC

#define mmDMA4_QM_ARB_MST_CHOISE_PUSH_OFST_19                        0x588AF0

#define mmDMA4_QM_ARB_MST_CHOISE_PUSH_OFST_20                        0x588AF4

#define mmDMA4_QM_ARB_MST_CHOISE_PUSH_OFST_21                        0x588AF8

#define mmDMA4_QM_ARB_MST_CHOISE_PUSH_OFST_22                        0x588AFC

#define mmDMA4_QM_ARB_MST_CHOISE_PUSH_OFST_23                        0x588B00

#define mmDMA4_QM_ARB_MST_CHOISE_PUSH_OFST_24                        0x588B04

#define mmDMA4_QM_ARB_MST_CHOISE_PUSH_OFST_25                        0x588B08

#define mmDMA4_QM_ARB_MST_CHOISE_PUSH_OFST_26                        0x588B0C

#define mmDMA4_QM_ARB_MST_CHOISE_PUSH_OFST_27                        0x588B10

#define mmDMA4_QM_ARB_MST_CHOISE_PUSH_OFST_28                        0x588B14

#define mmDMA4_QM_ARB_MST_CHOISE_PUSH_OFST_29                        0x588B18

#define mmDMA4_QM_ARB_MST_CHOISE_PUSH_OFST_30                        0x588B1C

#define mmDMA4_QM_ARB_MST_CHOISE_PUSH_OFST_31                        0x588B20

#define mmDMA4_QM_ARB_SLV_MASTER_INC_CRED_OFST                       0x588B28

#define mmDMA4_QM_ARB_MST_SLAVE_EN                                   0x588B2C

#define mmDMA4_QM_ARB_MST_QUIET_PER                                  0x588B34

#define mmDMA4_QM_ARB_SLV_CHOISE_WDT                                 0x588B38

#define mmDMA4_QM_ARB_SLV_ID                                         0x588B3C

#define mmDMA4_QM_ARB_MSG_MAX_INFLIGHT                               0x588B44

#define mmDMA4_QM_ARB_MSG_AWUSER_31_11                               0x588B48

#define mmDMA4_QM_ARB_MSG_AWUSER_SEC_PROP                            0x588B4C

#define mmDMA4_QM_ARB_MSG_AWUSER_NON_SEC_PROP                        0x588B50

#define mmDMA4_QM_ARB_BASE_LO                                        0x588B54

#define mmDMA4_QM_ARB_BASE_HI                                        0x588B58

#define mmDMA4_QM_ARB_STATE_STS                                      0x588B80

#define mmDMA4_QM_ARB_CHOISE_FULLNESS_STS                            0x588B84

#define mmDMA4_QM_ARB_MSG_STS                                        0x588B88

#define mmDMA4_QM_ARB_SLV_CHOISE_Q_HEAD                              0x588B8C

#define mmDMA4_QM_ARB_ERR_CAUSE                                      0x588B9C

#define mmDMA4_QM_ARB_ERR_MSG_EN                                     0x588BA0

#define mmDMA4_QM_ARB_ERR_STS_DRP                                    0x588BA8

#define mmDMA4_QM_ARB_MST_CRED_STS_0                                 0x588BB0

#define mmDMA4_QM_ARB_MST_CRED_STS_1                                 0x588BB4

#define mmDMA4_QM_ARB_MST_CRED_STS_2                                 0x588BB8

#define mmDMA4_QM_ARB_MST_CRED_STS_3                                 0x588BBC

#define mmDMA4_QM_ARB_MST_CRED_STS_4                                 0x588BC0

#define mmDMA4_QM_ARB_MST_CRED_STS_5                                 0x588BC4

#define mmDMA4_QM_ARB_MST_CRED_STS_6                                 0x588BC8

#define mmDMA4_QM_ARB_MST_CRED_STS_7                                 0x588BCC

#define mmDMA4_QM_ARB_MST_CRED_STS_8                                 0x588BD0

#define mmDMA4_QM_ARB_MST_CRED_STS_9                                 0x588BD4

#define mmDMA4_QM_ARB_MST_CRED_STS_10                                0x588BD8

#define mmDMA4_QM_ARB_MST_CRED_STS_11                                0x588BDC

#define mmDMA4_QM_ARB_MST_CRED_STS_12                                0x588BE0

#define mmDMA4_QM_ARB_MST_CRED_STS_13                                0x588BE4

#define mmDMA4_QM_ARB_MST_CRED_STS_14                                0x588BE8

#define mmDMA4_QM_ARB_MST_CRED_STS_15                                0x588BEC

#define mmDMA4_QM_ARB_MST_CRED_STS_16                                0x588BF0

#define mmDMA4_QM_ARB_MST_CRED_STS_17                                0x588BF4

#define mmDMA4_QM_ARB_MST_CRED_STS_18                                0x588BF8

#define mmDMA4_QM_ARB_MST_CRED_STS_19                                0x588BFC

#define mmDMA4_QM_ARB_MST_CRED_STS_20                                0x588C00

#define mmDMA4_QM_ARB_MST_CRED_STS_21                                0x588C04

#define mmDMA4_QM_ARB_MST_CRED_STS_22                                0x588C08

#define mmDMA4_QM_ARB_MST_CRED_STS_23                                0x588C0C

#define mmDMA4_QM_ARB_MST_CRED_STS_24                                0x588C10

#define mmDMA4_QM_ARB_MST_CRED_STS_25                                0x588C14

#define mmDMA4_QM_ARB_MST_CRED_STS_26                                0x588C18

#define mmDMA4_QM_ARB_MST_CRED_STS_27                                0x588C1C

#define mmDMA4_QM_ARB_MST_CRED_STS_28                                0x588C20

#define mmDMA4_QM_ARB_MST_CRED_STS_29                                0x588C24

#define mmDMA4_QM_ARB_MST_CRED_STS_30                                0x588C28

#define mmDMA4_QM_ARB_MST_CRED_STS_31                                0x588C2C

#define mmDMA4_QM_CGM_CFG                                            0x588C70

#define mmDMA4_QM_CGM_STS                                            0x588C74

#define mmDMA4_QM_CGM_CFG1                                           0x588C78

#define mmDMA4_QM_LOCAL_RANGE_BASE                                   0x588C80

#define mmDMA4_QM_LOCAL_RANGE_SIZE                                   0x588C84

#define mmDMA4_QM_CSMR_STRICT_PRIO_CFG                               0x588C90

#define mmDMA4_QM_HBW_RD_RATE_LIM_CFG_1                              0x588C94

#define mmDMA4_QM_LBW_WR_RATE_LIM_CFG_0                              0x588C98

#define mmDMA4_QM_LBW_WR_RATE_LIM_CFG_1                              0x588C9C

#define mmDMA4_QM_HBW_RD_RATE_LIM_CFG_0                              0x588CA0

#define mmDMA4_QM_GLBL_AXCACHE                                       0x588CA4

#define mmDMA4_QM_IND_GW_APB_CFG                                     0x588CB0

#define mmDMA4_QM_IND_GW_APB_WDATA                                   0x588CB4

#define mmDMA4_QM_IND_GW_APB_RDATA                                   0x588CB8

#define mmDMA4_QM_IND_GW_APB_STATUS                                  0x588CBC

#define mmDMA4_QM_GLBL_ERR_ADDR_LO                                   0x588CD0

#define mmDMA4_QM_GLBL_ERR_ADDR_HI                                   0x588CD4

#define mmDMA4_QM_GLBL_ERR_WDATA                                     0x588CD8

#define mmDMA4_QM_GLBL_MEM_INIT_BUSY                                 0x588D00

#endif /* ASIC_REG_DMA4_QM_REGS_H_ */
