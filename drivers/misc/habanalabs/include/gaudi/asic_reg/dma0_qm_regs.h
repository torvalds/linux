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

#ifndef ASIC_REG_DMA0_QM_REGS_H_
#define ASIC_REG_DMA0_QM_REGS_H_

/*
 *****************************************
 *   DMA0_QM (Prototype: QMAN)
 *****************************************
 */

#define mmDMA0_QM_GLBL_CFG0                                          0x508000

#define mmDMA0_QM_GLBL_CFG1                                          0x508004

#define mmDMA0_QM_GLBL_PROT                                          0x508008

#define mmDMA0_QM_GLBL_ERR_CFG                                       0x50800C

#define mmDMA0_QM_GLBL_SECURE_PROPS_0                                0x508010

#define mmDMA0_QM_GLBL_SECURE_PROPS_1                                0x508014

#define mmDMA0_QM_GLBL_SECURE_PROPS_2                                0x508018

#define mmDMA0_QM_GLBL_SECURE_PROPS_3                                0x50801C

#define mmDMA0_QM_GLBL_SECURE_PROPS_4                                0x508020

#define mmDMA0_QM_GLBL_NON_SECURE_PROPS_0                            0x508024

#define mmDMA0_QM_GLBL_NON_SECURE_PROPS_1                            0x508028

#define mmDMA0_QM_GLBL_NON_SECURE_PROPS_2                            0x50802C

#define mmDMA0_QM_GLBL_NON_SECURE_PROPS_3                            0x508030

#define mmDMA0_QM_GLBL_NON_SECURE_PROPS_4                            0x508034

#define mmDMA0_QM_GLBL_STS0                                          0x508038

#define mmDMA0_QM_GLBL_STS1_0                                        0x508040

#define mmDMA0_QM_GLBL_STS1_1                                        0x508044

#define mmDMA0_QM_GLBL_STS1_2                                        0x508048

#define mmDMA0_QM_GLBL_STS1_3                                        0x50804C

#define mmDMA0_QM_GLBL_STS1_4                                        0x508050

#define mmDMA0_QM_GLBL_MSG_EN_0                                      0x508054

#define mmDMA0_QM_GLBL_MSG_EN_1                                      0x508058

#define mmDMA0_QM_GLBL_MSG_EN_2                                      0x50805C

#define mmDMA0_QM_GLBL_MSG_EN_3                                      0x508060

#define mmDMA0_QM_GLBL_MSG_EN_4                                      0x508068

#define mmDMA0_QM_PQ_BASE_LO_0                                       0x508070

#define mmDMA0_QM_PQ_BASE_LO_1                                       0x508074

#define mmDMA0_QM_PQ_BASE_LO_2                                       0x508078

#define mmDMA0_QM_PQ_BASE_LO_3                                       0x50807C

#define mmDMA0_QM_PQ_BASE_HI_0                                       0x508080

#define mmDMA0_QM_PQ_BASE_HI_1                                       0x508084

#define mmDMA0_QM_PQ_BASE_HI_2                                       0x508088

#define mmDMA0_QM_PQ_BASE_HI_3                                       0x50808C

#define mmDMA0_QM_PQ_SIZE_0                                          0x508090

#define mmDMA0_QM_PQ_SIZE_1                                          0x508094

#define mmDMA0_QM_PQ_SIZE_2                                          0x508098

#define mmDMA0_QM_PQ_SIZE_3                                          0x50809C

#define mmDMA0_QM_PQ_PI_0                                            0x5080A0

#define mmDMA0_QM_PQ_PI_1                                            0x5080A4

#define mmDMA0_QM_PQ_PI_2                                            0x5080A8

#define mmDMA0_QM_PQ_PI_3                                            0x5080AC

#define mmDMA0_QM_PQ_CI_0                                            0x5080B0

#define mmDMA0_QM_PQ_CI_1                                            0x5080B4

#define mmDMA0_QM_PQ_CI_2                                            0x5080B8

#define mmDMA0_QM_PQ_CI_3                                            0x5080BC

#define mmDMA0_QM_PQ_CFG0_0                                          0x5080C0

#define mmDMA0_QM_PQ_CFG0_1                                          0x5080C4

#define mmDMA0_QM_PQ_CFG0_2                                          0x5080C8

#define mmDMA0_QM_PQ_CFG0_3                                          0x5080CC

#define mmDMA0_QM_PQ_CFG1_0                                          0x5080D0

#define mmDMA0_QM_PQ_CFG1_1                                          0x5080D4

#define mmDMA0_QM_PQ_CFG1_2                                          0x5080D8

#define mmDMA0_QM_PQ_CFG1_3                                          0x5080DC

#define mmDMA0_QM_PQ_ARUSER_31_11_0                                  0x5080E0

#define mmDMA0_QM_PQ_ARUSER_31_11_1                                  0x5080E4

#define mmDMA0_QM_PQ_ARUSER_31_11_2                                  0x5080E8

#define mmDMA0_QM_PQ_ARUSER_31_11_3                                  0x5080EC

#define mmDMA0_QM_PQ_STS0_0                                          0x5080F0

#define mmDMA0_QM_PQ_STS0_1                                          0x5080F4

#define mmDMA0_QM_PQ_STS0_2                                          0x5080F8

#define mmDMA0_QM_PQ_STS0_3                                          0x5080FC

#define mmDMA0_QM_PQ_STS1_0                                          0x508100

#define mmDMA0_QM_PQ_STS1_1                                          0x508104

#define mmDMA0_QM_PQ_STS1_2                                          0x508108

#define mmDMA0_QM_PQ_STS1_3                                          0x50810C

#define mmDMA0_QM_CQ_CFG0_0                                          0x508110

#define mmDMA0_QM_CQ_CFG0_1                                          0x508114

#define mmDMA0_QM_CQ_CFG0_2                                          0x508118

#define mmDMA0_QM_CQ_CFG0_3                                          0x50811C

#define mmDMA0_QM_CQ_CFG0_4                                          0x508120

#define mmDMA0_QM_CQ_CFG1_0                                          0x508124

#define mmDMA0_QM_CQ_CFG1_1                                          0x508128

#define mmDMA0_QM_CQ_CFG1_2                                          0x50812C

#define mmDMA0_QM_CQ_CFG1_3                                          0x508130

#define mmDMA0_QM_CQ_CFG1_4                                          0x508134

#define mmDMA0_QM_CQ_ARUSER_31_11_0                                  0x508138

#define mmDMA0_QM_CQ_ARUSER_31_11_1                                  0x50813C

#define mmDMA0_QM_CQ_ARUSER_31_11_2                                  0x508140

#define mmDMA0_QM_CQ_ARUSER_31_11_3                                  0x508144

#define mmDMA0_QM_CQ_ARUSER_31_11_4                                  0x508148

#define mmDMA0_QM_CQ_STS0_0                                          0x50814C

#define mmDMA0_QM_CQ_STS0_1                                          0x508150

#define mmDMA0_QM_CQ_STS0_2                                          0x508154

#define mmDMA0_QM_CQ_STS0_3                                          0x508158

#define mmDMA0_QM_CQ_STS0_4                                          0x50815C

#define mmDMA0_QM_CQ_STS1_0                                          0x508160

#define mmDMA0_QM_CQ_STS1_1                                          0x508164

#define mmDMA0_QM_CQ_STS1_2                                          0x508168

#define mmDMA0_QM_CQ_STS1_3                                          0x50816C

#define mmDMA0_QM_CQ_STS1_4                                          0x508170

#define mmDMA0_QM_CQ_PTR_LO_0                                        0x508174

#define mmDMA0_QM_CQ_PTR_HI_0                                        0x508178

#define mmDMA0_QM_CQ_TSIZE_0                                         0x50817C

#define mmDMA0_QM_CQ_CTL_0                                           0x508180

#define mmDMA0_QM_CQ_PTR_LO_1                                        0x508184

#define mmDMA0_QM_CQ_PTR_HI_1                                        0x508188

#define mmDMA0_QM_CQ_TSIZE_1                                         0x50818C

#define mmDMA0_QM_CQ_CTL_1                                           0x508190

#define mmDMA0_QM_CQ_PTR_LO_2                                        0x508194

#define mmDMA0_QM_CQ_PTR_HI_2                                        0x508198

#define mmDMA0_QM_CQ_TSIZE_2                                         0x50819C

#define mmDMA0_QM_CQ_CTL_2                                           0x5081A0

#define mmDMA0_QM_CQ_PTR_LO_3                                        0x5081A4

#define mmDMA0_QM_CQ_PTR_HI_3                                        0x5081A8

#define mmDMA0_QM_CQ_TSIZE_3                                         0x5081AC

#define mmDMA0_QM_CQ_CTL_3                                           0x5081B0

#define mmDMA0_QM_CQ_PTR_LO_4                                        0x5081B4

#define mmDMA0_QM_CQ_PTR_HI_4                                        0x5081B8

#define mmDMA0_QM_CQ_TSIZE_4                                         0x5081BC

#define mmDMA0_QM_CQ_CTL_4                                           0x5081C0

#define mmDMA0_QM_CQ_PTR_LO_STS_0                                    0x5081C4

#define mmDMA0_QM_CQ_PTR_LO_STS_1                                    0x5081C8

#define mmDMA0_QM_CQ_PTR_LO_STS_2                                    0x5081CC

#define mmDMA0_QM_CQ_PTR_LO_STS_3                                    0x5081D0

#define mmDMA0_QM_CQ_PTR_LO_STS_4                                    0x5081D4

#define mmDMA0_QM_CQ_PTR_HI_STS_0                                    0x5081D8

#define mmDMA0_QM_CQ_PTR_HI_STS_1                                    0x5081DC

#define mmDMA0_QM_CQ_PTR_HI_STS_2                                    0x5081E0

#define mmDMA0_QM_CQ_PTR_HI_STS_3                                    0x5081E4

#define mmDMA0_QM_CQ_PTR_HI_STS_4                                    0x5081E8

#define mmDMA0_QM_CQ_TSIZE_STS_0                                     0x5081EC

#define mmDMA0_QM_CQ_TSIZE_STS_1                                     0x5081F0

#define mmDMA0_QM_CQ_TSIZE_STS_2                                     0x5081F4

#define mmDMA0_QM_CQ_TSIZE_STS_3                                     0x5081F8

#define mmDMA0_QM_CQ_TSIZE_STS_4                                     0x5081FC

#define mmDMA0_QM_CQ_CTL_STS_0                                       0x508200

#define mmDMA0_QM_CQ_CTL_STS_1                                       0x508204

#define mmDMA0_QM_CQ_CTL_STS_2                                       0x508208

#define mmDMA0_QM_CQ_CTL_STS_3                                       0x50820C

#define mmDMA0_QM_CQ_CTL_STS_4                                       0x508210

#define mmDMA0_QM_CQ_IFIFO_CNT_0                                     0x508214

#define mmDMA0_QM_CQ_IFIFO_CNT_1                                     0x508218

#define mmDMA0_QM_CQ_IFIFO_CNT_2                                     0x50821C

#define mmDMA0_QM_CQ_IFIFO_CNT_3                                     0x508220

#define mmDMA0_QM_CQ_IFIFO_CNT_4                                     0x508224

#define mmDMA0_QM_CP_MSG_BASE0_ADDR_LO_0                             0x508228

#define mmDMA0_QM_CP_MSG_BASE0_ADDR_LO_1                             0x50822C

#define mmDMA0_QM_CP_MSG_BASE0_ADDR_LO_2                             0x508230

#define mmDMA0_QM_CP_MSG_BASE0_ADDR_LO_3                             0x508234

#define mmDMA0_QM_CP_MSG_BASE0_ADDR_LO_4                             0x508238

#define mmDMA0_QM_CP_MSG_BASE0_ADDR_HI_0                             0x50823C

#define mmDMA0_QM_CP_MSG_BASE0_ADDR_HI_1                             0x508240

#define mmDMA0_QM_CP_MSG_BASE0_ADDR_HI_2                             0x508244

#define mmDMA0_QM_CP_MSG_BASE0_ADDR_HI_3                             0x508248

#define mmDMA0_QM_CP_MSG_BASE0_ADDR_HI_4                             0x50824C

#define mmDMA0_QM_CP_MSG_BASE1_ADDR_LO_0                             0x508250

#define mmDMA0_QM_CP_MSG_BASE1_ADDR_LO_1                             0x508254

#define mmDMA0_QM_CP_MSG_BASE1_ADDR_LO_2                             0x508258

#define mmDMA0_QM_CP_MSG_BASE1_ADDR_LO_3                             0x50825C

#define mmDMA0_QM_CP_MSG_BASE1_ADDR_LO_4                             0x508260

#define mmDMA0_QM_CP_MSG_BASE1_ADDR_HI_0                             0x508264

#define mmDMA0_QM_CP_MSG_BASE1_ADDR_HI_1                             0x508268

#define mmDMA0_QM_CP_MSG_BASE1_ADDR_HI_2                             0x50826C

#define mmDMA0_QM_CP_MSG_BASE1_ADDR_HI_3                             0x508270

#define mmDMA0_QM_CP_MSG_BASE1_ADDR_HI_4                             0x508274

#define mmDMA0_QM_CP_MSG_BASE2_ADDR_LO_0                             0x508278

#define mmDMA0_QM_CP_MSG_BASE2_ADDR_LO_1                             0x50827C

#define mmDMA0_QM_CP_MSG_BASE2_ADDR_LO_2                             0x508280

#define mmDMA0_QM_CP_MSG_BASE2_ADDR_LO_3                             0x508284

#define mmDMA0_QM_CP_MSG_BASE2_ADDR_LO_4                             0x508288

#define mmDMA0_QM_CP_MSG_BASE2_ADDR_HI_0                             0x50828C

#define mmDMA0_QM_CP_MSG_BASE2_ADDR_HI_1                             0x508290

#define mmDMA0_QM_CP_MSG_BASE2_ADDR_HI_2                             0x508294

#define mmDMA0_QM_CP_MSG_BASE2_ADDR_HI_3                             0x508298

#define mmDMA0_QM_CP_MSG_BASE2_ADDR_HI_4                             0x50829C

#define mmDMA0_QM_CP_MSG_BASE3_ADDR_LO_0                             0x5082A0

#define mmDMA0_QM_CP_MSG_BASE3_ADDR_LO_1                             0x5082A4

#define mmDMA0_QM_CP_MSG_BASE3_ADDR_LO_2                             0x5082A8

#define mmDMA0_QM_CP_MSG_BASE3_ADDR_LO_3                             0x5082AC

#define mmDMA0_QM_CP_MSG_BASE3_ADDR_LO_4                             0x5082B0

#define mmDMA0_QM_CP_MSG_BASE3_ADDR_HI_0                             0x5082B4

#define mmDMA0_QM_CP_MSG_BASE3_ADDR_HI_1                             0x5082B8

#define mmDMA0_QM_CP_MSG_BASE3_ADDR_HI_2                             0x5082BC

#define mmDMA0_QM_CP_MSG_BASE3_ADDR_HI_3                             0x5082C0

#define mmDMA0_QM_CP_MSG_BASE3_ADDR_HI_4                             0x5082C4

#define mmDMA0_QM_CP_LDMA_TSIZE_OFFSET_0                             0x5082C8

#define mmDMA0_QM_CP_LDMA_TSIZE_OFFSET_1                             0x5082CC

#define mmDMA0_QM_CP_LDMA_TSIZE_OFFSET_2                             0x5082D0

#define mmDMA0_QM_CP_LDMA_TSIZE_OFFSET_3                             0x5082D4

#define mmDMA0_QM_CP_LDMA_TSIZE_OFFSET_4                             0x5082D8

#define mmDMA0_QM_CP_LDMA_SRC_BASE_LO_OFFSET_0                       0x5082E0

#define mmDMA0_QM_CP_LDMA_SRC_BASE_LO_OFFSET_1                       0x5082E4

#define mmDMA0_QM_CP_LDMA_SRC_BASE_LO_OFFSET_2                       0x5082E8

#define mmDMA0_QM_CP_LDMA_SRC_BASE_LO_OFFSET_3                       0x5082EC

#define mmDMA0_QM_CP_LDMA_SRC_BASE_LO_OFFSET_4                       0x5082F0

#define mmDMA0_QM_CP_LDMA_DST_BASE_LO_OFFSET_0                       0x5082F4

#define mmDMA0_QM_CP_LDMA_DST_BASE_LO_OFFSET_1                       0x5082F8

#define mmDMA0_QM_CP_LDMA_DST_BASE_LO_OFFSET_2                       0x5082FC

#define mmDMA0_QM_CP_LDMA_DST_BASE_LO_OFFSET_3                       0x508300

#define mmDMA0_QM_CP_LDMA_DST_BASE_LO_OFFSET_4                       0x508304

#define mmDMA0_QM_CP_FENCE0_RDATA_0                                  0x508308

#define mmDMA0_QM_CP_FENCE0_RDATA_1                                  0x50830C

#define mmDMA0_QM_CP_FENCE0_RDATA_2                                  0x508310

#define mmDMA0_QM_CP_FENCE0_RDATA_3                                  0x508314

#define mmDMA0_QM_CP_FENCE0_RDATA_4                                  0x508318

#define mmDMA0_QM_CP_FENCE1_RDATA_0                                  0x50831C

#define mmDMA0_QM_CP_FENCE1_RDATA_1                                  0x508320

#define mmDMA0_QM_CP_FENCE1_RDATA_2                                  0x508324

#define mmDMA0_QM_CP_FENCE1_RDATA_3                                  0x508328

#define mmDMA0_QM_CP_FENCE1_RDATA_4                                  0x50832C

#define mmDMA0_QM_CP_FENCE2_RDATA_0                                  0x508330

#define mmDMA0_QM_CP_FENCE2_RDATA_1                                  0x508334

#define mmDMA0_QM_CP_FENCE2_RDATA_2                                  0x508338

#define mmDMA0_QM_CP_FENCE2_RDATA_3                                  0x50833C

#define mmDMA0_QM_CP_FENCE2_RDATA_4                                  0x508340

#define mmDMA0_QM_CP_FENCE3_RDATA_0                                  0x508344

#define mmDMA0_QM_CP_FENCE3_RDATA_1                                  0x508348

#define mmDMA0_QM_CP_FENCE3_RDATA_2                                  0x50834C

#define mmDMA0_QM_CP_FENCE3_RDATA_3                                  0x508350

#define mmDMA0_QM_CP_FENCE3_RDATA_4                                  0x508354

#define mmDMA0_QM_CP_FENCE0_CNT_0                                    0x508358

#define mmDMA0_QM_CP_FENCE0_CNT_1                                    0x50835C

#define mmDMA0_QM_CP_FENCE0_CNT_2                                    0x508360

#define mmDMA0_QM_CP_FENCE0_CNT_3                                    0x508364

#define mmDMA0_QM_CP_FENCE0_CNT_4                                    0x508368

#define mmDMA0_QM_CP_FENCE1_CNT_0                                    0x50836C

#define mmDMA0_QM_CP_FENCE1_CNT_1                                    0x508370

#define mmDMA0_QM_CP_FENCE1_CNT_2                                    0x508374

#define mmDMA0_QM_CP_FENCE1_CNT_3                                    0x508378

#define mmDMA0_QM_CP_FENCE1_CNT_4                                    0x50837C

#define mmDMA0_QM_CP_FENCE2_CNT_0                                    0x508380

#define mmDMA0_QM_CP_FENCE2_CNT_1                                    0x508384

#define mmDMA0_QM_CP_FENCE2_CNT_2                                    0x508388

#define mmDMA0_QM_CP_FENCE2_CNT_3                                    0x50838C

#define mmDMA0_QM_CP_FENCE2_CNT_4                                    0x508390

#define mmDMA0_QM_CP_FENCE3_CNT_0                                    0x508394

#define mmDMA0_QM_CP_FENCE3_CNT_1                                    0x508398

#define mmDMA0_QM_CP_FENCE3_CNT_2                                    0x50839C

#define mmDMA0_QM_CP_FENCE3_CNT_3                                    0x5083A0

#define mmDMA0_QM_CP_FENCE3_CNT_4                                    0x5083A4

#define mmDMA0_QM_CP_STS_0                                           0x5083A8

#define mmDMA0_QM_CP_STS_1                                           0x5083AC

#define mmDMA0_QM_CP_STS_2                                           0x5083B0

#define mmDMA0_QM_CP_STS_3                                           0x5083B4

#define mmDMA0_QM_CP_STS_4                                           0x5083B8

#define mmDMA0_QM_CP_CURRENT_INST_LO_0                               0x5083BC

#define mmDMA0_QM_CP_CURRENT_INST_LO_1                               0x5083C0

#define mmDMA0_QM_CP_CURRENT_INST_LO_2                               0x5083C4

#define mmDMA0_QM_CP_CURRENT_INST_LO_3                               0x5083C8

#define mmDMA0_QM_CP_CURRENT_INST_LO_4                               0x5083CC

#define mmDMA0_QM_CP_CURRENT_INST_HI_0                               0x5083D0

#define mmDMA0_QM_CP_CURRENT_INST_HI_1                               0x5083D4

#define mmDMA0_QM_CP_CURRENT_INST_HI_2                               0x5083D8

#define mmDMA0_QM_CP_CURRENT_INST_HI_3                               0x5083DC

#define mmDMA0_QM_CP_CURRENT_INST_HI_4                               0x5083E0

#define mmDMA0_QM_CP_BARRIER_CFG_0                                   0x5083F4

#define mmDMA0_QM_CP_BARRIER_CFG_1                                   0x5083F8

#define mmDMA0_QM_CP_BARRIER_CFG_2                                   0x5083FC

#define mmDMA0_QM_CP_BARRIER_CFG_3                                   0x508400

#define mmDMA0_QM_CP_BARRIER_CFG_4                                   0x508404

#define mmDMA0_QM_CP_DBG_0_0                                         0x508408

#define mmDMA0_QM_CP_DBG_0_1                                         0x50840C

#define mmDMA0_QM_CP_DBG_0_2                                         0x508410

#define mmDMA0_QM_CP_DBG_0_3                                         0x508414

#define mmDMA0_QM_CP_DBG_0_4                                         0x508418

#define mmDMA0_QM_CP_ARUSER_31_11_0                                  0x50841C

#define mmDMA0_QM_CP_ARUSER_31_11_1                                  0x508420

#define mmDMA0_QM_CP_ARUSER_31_11_2                                  0x508424

#define mmDMA0_QM_CP_ARUSER_31_11_3                                  0x508428

#define mmDMA0_QM_CP_ARUSER_31_11_4                                  0x50842C

#define mmDMA0_QM_CP_AWUSER_31_11_0                                  0x508430

#define mmDMA0_QM_CP_AWUSER_31_11_1                                  0x508434

#define mmDMA0_QM_CP_AWUSER_31_11_2                                  0x508438

#define mmDMA0_QM_CP_AWUSER_31_11_3                                  0x50843C

#define mmDMA0_QM_CP_AWUSER_31_11_4                                  0x508440

#define mmDMA0_QM_ARB_CFG_0                                          0x508A00

#define mmDMA0_QM_ARB_CHOISE_Q_PUSH                                  0x508A04

#define mmDMA0_QM_ARB_WRR_WEIGHT_0                                   0x508A08

#define mmDMA0_QM_ARB_WRR_WEIGHT_1                                   0x508A0C

#define mmDMA0_QM_ARB_WRR_WEIGHT_2                                   0x508A10

#define mmDMA0_QM_ARB_WRR_WEIGHT_3                                   0x508A14

#define mmDMA0_QM_ARB_CFG_1                                          0x508A18

#define mmDMA0_QM_ARB_MST_AVAIL_CRED_0                               0x508A20

#define mmDMA0_QM_ARB_MST_AVAIL_CRED_1                               0x508A24

#define mmDMA0_QM_ARB_MST_AVAIL_CRED_2                               0x508A28

#define mmDMA0_QM_ARB_MST_AVAIL_CRED_3                               0x508A2C

#define mmDMA0_QM_ARB_MST_AVAIL_CRED_4                               0x508A30

#define mmDMA0_QM_ARB_MST_AVAIL_CRED_5                               0x508A34

#define mmDMA0_QM_ARB_MST_AVAIL_CRED_6                               0x508A38

#define mmDMA0_QM_ARB_MST_AVAIL_CRED_7                               0x508A3C

#define mmDMA0_QM_ARB_MST_AVAIL_CRED_8                               0x508A40

#define mmDMA0_QM_ARB_MST_AVAIL_CRED_9                               0x508A44

#define mmDMA0_QM_ARB_MST_AVAIL_CRED_10                              0x508A48

#define mmDMA0_QM_ARB_MST_AVAIL_CRED_11                              0x508A4C

#define mmDMA0_QM_ARB_MST_AVAIL_CRED_12                              0x508A50

#define mmDMA0_QM_ARB_MST_AVAIL_CRED_13                              0x508A54

#define mmDMA0_QM_ARB_MST_AVAIL_CRED_14                              0x508A58

#define mmDMA0_QM_ARB_MST_AVAIL_CRED_15                              0x508A5C

#define mmDMA0_QM_ARB_MST_AVAIL_CRED_16                              0x508A60

#define mmDMA0_QM_ARB_MST_AVAIL_CRED_17                              0x508A64

#define mmDMA0_QM_ARB_MST_AVAIL_CRED_18                              0x508A68

#define mmDMA0_QM_ARB_MST_AVAIL_CRED_19                              0x508A6C

#define mmDMA0_QM_ARB_MST_AVAIL_CRED_20                              0x508A70

#define mmDMA0_QM_ARB_MST_AVAIL_CRED_21                              0x508A74

#define mmDMA0_QM_ARB_MST_AVAIL_CRED_22                              0x508A78

#define mmDMA0_QM_ARB_MST_AVAIL_CRED_23                              0x508A7C

#define mmDMA0_QM_ARB_MST_AVAIL_CRED_24                              0x508A80

#define mmDMA0_QM_ARB_MST_AVAIL_CRED_25                              0x508A84

#define mmDMA0_QM_ARB_MST_AVAIL_CRED_26                              0x508A88

#define mmDMA0_QM_ARB_MST_AVAIL_CRED_27                              0x508A8C

#define mmDMA0_QM_ARB_MST_AVAIL_CRED_28                              0x508A90

#define mmDMA0_QM_ARB_MST_AVAIL_CRED_29                              0x508A94

#define mmDMA0_QM_ARB_MST_AVAIL_CRED_30                              0x508A98

#define mmDMA0_QM_ARB_MST_AVAIL_CRED_31                              0x508A9C

#define mmDMA0_QM_ARB_MST_CRED_INC                                   0x508AA0

#define mmDMA0_QM_ARB_MST_CHOISE_PUSH_OFST_0                         0x508AA4

#define mmDMA0_QM_ARB_MST_CHOISE_PUSH_OFST_1                         0x508AA8

#define mmDMA0_QM_ARB_MST_CHOISE_PUSH_OFST_2                         0x508AAC

#define mmDMA0_QM_ARB_MST_CHOISE_PUSH_OFST_3                         0x508AB0

#define mmDMA0_QM_ARB_MST_CHOISE_PUSH_OFST_4                         0x508AB4

#define mmDMA0_QM_ARB_MST_CHOISE_PUSH_OFST_5                         0x508AB8

#define mmDMA0_QM_ARB_MST_CHOISE_PUSH_OFST_6                         0x508ABC

#define mmDMA0_QM_ARB_MST_CHOISE_PUSH_OFST_7                         0x508AC0

#define mmDMA0_QM_ARB_MST_CHOISE_PUSH_OFST_8                         0x508AC4

#define mmDMA0_QM_ARB_MST_CHOISE_PUSH_OFST_9                         0x508AC8

#define mmDMA0_QM_ARB_MST_CHOISE_PUSH_OFST_10                        0x508ACC

#define mmDMA0_QM_ARB_MST_CHOISE_PUSH_OFST_11                        0x508AD0

#define mmDMA0_QM_ARB_MST_CHOISE_PUSH_OFST_12                        0x508AD4

#define mmDMA0_QM_ARB_MST_CHOISE_PUSH_OFST_13                        0x508AD8

#define mmDMA0_QM_ARB_MST_CHOISE_PUSH_OFST_14                        0x508ADC

#define mmDMA0_QM_ARB_MST_CHOISE_PUSH_OFST_15                        0x508AE0

#define mmDMA0_QM_ARB_MST_CHOISE_PUSH_OFST_16                        0x508AE4

#define mmDMA0_QM_ARB_MST_CHOISE_PUSH_OFST_17                        0x508AE8

#define mmDMA0_QM_ARB_MST_CHOISE_PUSH_OFST_18                        0x508AEC

#define mmDMA0_QM_ARB_MST_CHOISE_PUSH_OFST_19                        0x508AF0

#define mmDMA0_QM_ARB_MST_CHOISE_PUSH_OFST_20                        0x508AF4

#define mmDMA0_QM_ARB_MST_CHOISE_PUSH_OFST_21                        0x508AF8

#define mmDMA0_QM_ARB_MST_CHOISE_PUSH_OFST_22                        0x508AFC

#define mmDMA0_QM_ARB_MST_CHOISE_PUSH_OFST_23                        0x508B00

#define mmDMA0_QM_ARB_MST_CHOISE_PUSH_OFST_24                        0x508B04

#define mmDMA0_QM_ARB_MST_CHOISE_PUSH_OFST_25                        0x508B08

#define mmDMA0_QM_ARB_MST_CHOISE_PUSH_OFST_26                        0x508B0C

#define mmDMA0_QM_ARB_MST_CHOISE_PUSH_OFST_27                        0x508B10

#define mmDMA0_QM_ARB_MST_CHOISE_PUSH_OFST_28                        0x508B14

#define mmDMA0_QM_ARB_MST_CHOISE_PUSH_OFST_29                        0x508B18

#define mmDMA0_QM_ARB_MST_CHOISE_PUSH_OFST_30                        0x508B1C

#define mmDMA0_QM_ARB_MST_CHOISE_PUSH_OFST_31                        0x508B20

#define mmDMA0_QM_ARB_SLV_MASTER_INC_CRED_OFST                       0x508B28

#define mmDMA0_QM_ARB_MST_SLAVE_EN                                   0x508B2C

#define mmDMA0_QM_ARB_MST_QUIET_PER                                  0x508B34

#define mmDMA0_QM_ARB_SLV_CHOISE_WDT                                 0x508B38

#define mmDMA0_QM_ARB_SLV_ID                                         0x508B3C

#define mmDMA0_QM_ARB_MSG_MAX_INFLIGHT                               0x508B44

#define mmDMA0_QM_ARB_MSG_AWUSER_31_11                               0x508B48

#define mmDMA0_QM_ARB_MSG_AWUSER_SEC_PROP                            0x508B4C

#define mmDMA0_QM_ARB_MSG_AWUSER_NON_SEC_PROP                        0x508B50

#define mmDMA0_QM_ARB_BASE_LO                                        0x508B54

#define mmDMA0_QM_ARB_BASE_HI                                        0x508B58

#define mmDMA0_QM_ARB_STATE_STS                                      0x508B80

#define mmDMA0_QM_ARB_CHOISE_FULLNESS_STS                            0x508B84

#define mmDMA0_QM_ARB_MSG_STS                                        0x508B88

#define mmDMA0_QM_ARB_SLV_CHOISE_Q_HEAD                              0x508B8C

#define mmDMA0_QM_ARB_ERR_CAUSE                                      0x508B9C

#define mmDMA0_QM_ARB_ERR_MSG_EN                                     0x508BA0

#define mmDMA0_QM_ARB_ERR_STS_DRP                                    0x508BA8

#define mmDMA0_QM_ARB_MST_CRED_STS_0                                 0x508BB0

#define mmDMA0_QM_ARB_MST_CRED_STS_1                                 0x508BB4

#define mmDMA0_QM_ARB_MST_CRED_STS_2                                 0x508BB8

#define mmDMA0_QM_ARB_MST_CRED_STS_3                                 0x508BBC

#define mmDMA0_QM_ARB_MST_CRED_STS_4                                 0x508BC0

#define mmDMA0_QM_ARB_MST_CRED_STS_5                                 0x508BC4

#define mmDMA0_QM_ARB_MST_CRED_STS_6                                 0x508BC8

#define mmDMA0_QM_ARB_MST_CRED_STS_7                                 0x508BCC

#define mmDMA0_QM_ARB_MST_CRED_STS_8                                 0x508BD0

#define mmDMA0_QM_ARB_MST_CRED_STS_9                                 0x508BD4

#define mmDMA0_QM_ARB_MST_CRED_STS_10                                0x508BD8

#define mmDMA0_QM_ARB_MST_CRED_STS_11                                0x508BDC

#define mmDMA0_QM_ARB_MST_CRED_STS_12                                0x508BE0

#define mmDMA0_QM_ARB_MST_CRED_STS_13                                0x508BE4

#define mmDMA0_QM_ARB_MST_CRED_STS_14                                0x508BE8

#define mmDMA0_QM_ARB_MST_CRED_STS_15                                0x508BEC

#define mmDMA0_QM_ARB_MST_CRED_STS_16                                0x508BF0

#define mmDMA0_QM_ARB_MST_CRED_STS_17                                0x508BF4

#define mmDMA0_QM_ARB_MST_CRED_STS_18                                0x508BF8

#define mmDMA0_QM_ARB_MST_CRED_STS_19                                0x508BFC

#define mmDMA0_QM_ARB_MST_CRED_STS_20                                0x508C00

#define mmDMA0_QM_ARB_MST_CRED_STS_21                                0x508C04

#define mmDMA0_QM_ARB_MST_CRED_STS_22                                0x508C08

#define mmDMA0_QM_ARB_MST_CRED_STS_23                                0x508C0C

#define mmDMA0_QM_ARB_MST_CRED_STS_24                                0x508C10

#define mmDMA0_QM_ARB_MST_CRED_STS_25                                0x508C14

#define mmDMA0_QM_ARB_MST_CRED_STS_26                                0x508C18

#define mmDMA0_QM_ARB_MST_CRED_STS_27                                0x508C1C

#define mmDMA0_QM_ARB_MST_CRED_STS_28                                0x508C20

#define mmDMA0_QM_ARB_MST_CRED_STS_29                                0x508C24

#define mmDMA0_QM_ARB_MST_CRED_STS_30                                0x508C28

#define mmDMA0_QM_ARB_MST_CRED_STS_31                                0x508C2C

#define mmDMA0_QM_CGM_CFG                                            0x508C70

#define mmDMA0_QM_CGM_STS                                            0x508C74

#define mmDMA0_QM_CGM_CFG1                                           0x508C78

#define mmDMA0_QM_LOCAL_RANGE_BASE                                   0x508C80

#define mmDMA0_QM_LOCAL_RANGE_SIZE                                   0x508C84

#define mmDMA0_QM_CSMR_STRICT_PRIO_CFG                               0x508C90

#define mmDMA0_QM_HBW_RD_RATE_LIM_CFG_1                              0x508C94

#define mmDMA0_QM_LBW_WR_RATE_LIM_CFG_0                              0x508C98

#define mmDMA0_QM_LBW_WR_RATE_LIM_CFG_1                              0x508C9C

#define mmDMA0_QM_HBW_RD_RATE_LIM_CFG_0                              0x508CA0

#define mmDMA0_QM_GLBL_AXCACHE                                       0x508CA4

#define mmDMA0_QM_IND_GW_APB_CFG                                     0x508CB0

#define mmDMA0_QM_IND_GW_APB_WDATA                                   0x508CB4

#define mmDMA0_QM_IND_GW_APB_RDATA                                   0x508CB8

#define mmDMA0_QM_IND_GW_APB_STATUS                                  0x508CBC

#define mmDMA0_QM_GLBL_ERR_ADDR_LO                                   0x508CD0

#define mmDMA0_QM_GLBL_ERR_ADDR_HI                                   0x508CD4

#define mmDMA0_QM_GLBL_ERR_WDATA                                     0x508CD8

#define mmDMA0_QM_GLBL_MEM_INIT_BUSY                                 0x508D00

#endif /* ASIC_REG_DMA0_QM_REGS_H_ */
