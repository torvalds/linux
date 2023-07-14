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

#ifndef ASIC_REG_MME_QM_REGS_H_
#define ASIC_REG_MME_QM_REGS_H_

/*
 *****************************************
 *   MME_QM (Prototype: QMAN)
 *****************************************
 */

#define mmMME_QM_GLBL_CFG0                                           0xD8000

#define mmMME_QM_GLBL_CFG1                                           0xD8004

#define mmMME_QM_GLBL_PROT                                           0xD8008

#define mmMME_QM_GLBL_ERR_CFG                                        0xD800C

#define mmMME_QM_GLBL_ERR_ADDR_LO                                    0xD8010

#define mmMME_QM_GLBL_ERR_ADDR_HI                                    0xD8014

#define mmMME_QM_GLBL_ERR_WDATA                                      0xD8018

#define mmMME_QM_GLBL_SECURE_PROPS                                   0xD801C

#define mmMME_QM_GLBL_NON_SECURE_PROPS                               0xD8020

#define mmMME_QM_GLBL_STS0                                           0xD8024

#define mmMME_QM_GLBL_STS1                                           0xD8028

#define mmMME_QM_PQ_BASE_LO                                          0xD8060

#define mmMME_QM_PQ_BASE_HI                                          0xD8064

#define mmMME_QM_PQ_SIZE                                             0xD8068

#define mmMME_QM_PQ_PI                                               0xD806C

#define mmMME_QM_PQ_CI                                               0xD8070

#define mmMME_QM_PQ_CFG0                                             0xD8074

#define mmMME_QM_PQ_CFG1                                             0xD8078

#define mmMME_QM_PQ_ARUSER                                           0xD807C

#define mmMME_QM_PQ_PUSH0                                            0xD8080

#define mmMME_QM_PQ_PUSH1                                            0xD8084

#define mmMME_QM_PQ_PUSH2                                            0xD8088

#define mmMME_QM_PQ_PUSH3                                            0xD808C

#define mmMME_QM_PQ_STS0                                             0xD8090

#define mmMME_QM_PQ_STS1                                             0xD8094

#define mmMME_QM_PQ_RD_RATE_LIM_EN                                   0xD80A0

#define mmMME_QM_PQ_RD_RATE_LIM_RST_TOKEN                            0xD80A4

#define mmMME_QM_PQ_RD_RATE_LIM_SAT                                  0xD80A8

#define mmMME_QM_PQ_RD_RATE_LIM_TOUT                                 0xD80AC

#define mmMME_QM_CQ_CFG0                                             0xD80B0

#define mmMME_QM_CQ_CFG1                                             0xD80B4

#define mmMME_QM_CQ_ARUSER                                           0xD80B8

#define mmMME_QM_CQ_PTR_LO                                           0xD80C0

#define mmMME_QM_CQ_PTR_HI                                           0xD80C4

#define mmMME_QM_CQ_TSIZE                                            0xD80C8

#define mmMME_QM_CQ_CTL                                              0xD80CC

#define mmMME_QM_CQ_PTR_LO_STS                                       0xD80D4

#define mmMME_QM_CQ_PTR_HI_STS                                       0xD80D8

#define mmMME_QM_CQ_TSIZE_STS                                        0xD80DC

#define mmMME_QM_CQ_CTL_STS                                          0xD80E0

#define mmMME_QM_CQ_STS0                                             0xD80E4

#define mmMME_QM_CQ_STS1                                             0xD80E8

#define mmMME_QM_CQ_RD_RATE_LIM_EN                                   0xD80F0

#define mmMME_QM_CQ_RD_RATE_LIM_RST_TOKEN                            0xD80F4

#define mmMME_QM_CQ_RD_RATE_LIM_SAT                                  0xD80F8

#define mmMME_QM_CQ_RD_RATE_LIM_TOUT                                 0xD80FC

#define mmMME_QM_CQ_IFIFO_CNT                                        0xD8108

#define mmMME_QM_CP_MSG_BASE0_ADDR_LO                                0xD8120

#define mmMME_QM_CP_MSG_BASE0_ADDR_HI                                0xD8124

#define mmMME_QM_CP_MSG_BASE1_ADDR_LO                                0xD8128

#define mmMME_QM_CP_MSG_BASE1_ADDR_HI                                0xD812C

#define mmMME_QM_CP_MSG_BASE2_ADDR_LO                                0xD8130

#define mmMME_QM_CP_MSG_BASE2_ADDR_HI                                0xD8134

#define mmMME_QM_CP_MSG_BASE3_ADDR_LO                                0xD8138

#define mmMME_QM_CP_MSG_BASE3_ADDR_HI                                0xD813C

#define mmMME_QM_CP_LDMA_TSIZE_OFFSET                                0xD8140

#define mmMME_QM_CP_LDMA_SRC_BASE_LO_OFFSET                          0xD8144

#define mmMME_QM_CP_LDMA_SRC_BASE_HI_OFFSET                          0xD8148

#define mmMME_QM_CP_LDMA_DST_BASE_LO_OFFSET                          0xD814C

#define mmMME_QM_CP_LDMA_DST_BASE_HI_OFFSET                          0xD8150

#define mmMME_QM_CP_LDMA_COMMIT_OFFSET                               0xD8154

#define mmMME_QM_CP_FENCE0_RDATA                                     0xD8158

#define mmMME_QM_CP_FENCE1_RDATA                                     0xD815C

#define mmMME_QM_CP_FENCE2_RDATA                                     0xD8160

#define mmMME_QM_CP_FENCE3_RDATA                                     0xD8164

#define mmMME_QM_CP_FENCE0_CNT                                       0xD8168

#define mmMME_QM_CP_FENCE1_CNT                                       0xD816C

#define mmMME_QM_CP_FENCE2_CNT                                       0xD8170

#define mmMME_QM_CP_FENCE3_CNT                                       0xD8174

#define mmMME_QM_CP_STS                                              0xD8178

#define mmMME_QM_CP_CURRENT_INST_LO                                  0xD817C

#define mmMME_QM_CP_CURRENT_INST_HI                                  0xD8180

#define mmMME_QM_CP_BARRIER_CFG                                      0xD8184

#define mmMME_QM_CP_DBG_0                                            0xD8188

#define mmMME_QM_PQ_BUF_ADDR                                         0xD8300

#define mmMME_QM_PQ_BUF_RDATA                                        0xD8304

#define mmMME_QM_CQ_BUF_ADDR                                         0xD8308

#define mmMME_QM_CQ_BUF_RDATA                                        0xD830C

#endif /* ASIC_REG_MME_QM_REGS_H_ */
