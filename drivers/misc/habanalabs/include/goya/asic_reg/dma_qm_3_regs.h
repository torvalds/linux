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

#ifndef ASIC_REG_DMA_QM_3_REGS_H_
#define ASIC_REG_DMA_QM_3_REGS_H_

/*
 *****************************************
 *   DMA_QM_3 (Prototype: QMAN)
 *****************************************
 */

#define mmDMA_QM_3_GLBL_CFG0                                         0x418000

#define mmDMA_QM_3_GLBL_CFG1                                         0x418004

#define mmDMA_QM_3_GLBL_PROT                                         0x418008

#define mmDMA_QM_3_GLBL_ERR_CFG                                      0x41800C

#define mmDMA_QM_3_GLBL_ERR_ADDR_LO                                  0x418010

#define mmDMA_QM_3_GLBL_ERR_ADDR_HI                                  0x418014

#define mmDMA_QM_3_GLBL_ERR_WDATA                                    0x418018

#define mmDMA_QM_3_GLBL_SECURE_PROPS                                 0x41801C

#define mmDMA_QM_3_GLBL_NON_SECURE_PROPS                             0x418020

#define mmDMA_QM_3_GLBL_STS0                                         0x418024

#define mmDMA_QM_3_GLBL_STS1                                         0x418028

#define mmDMA_QM_3_PQ_BASE_LO                                        0x418060

#define mmDMA_QM_3_PQ_BASE_HI                                        0x418064

#define mmDMA_QM_3_PQ_SIZE                                           0x418068

#define mmDMA_QM_3_PQ_PI                                             0x41806C

#define mmDMA_QM_3_PQ_CI                                             0x418070

#define mmDMA_QM_3_PQ_CFG0                                           0x418074

#define mmDMA_QM_3_PQ_CFG1                                           0x418078

#define mmDMA_QM_3_PQ_ARUSER                                         0x41807C

#define mmDMA_QM_3_PQ_PUSH0                                          0x418080

#define mmDMA_QM_3_PQ_PUSH1                                          0x418084

#define mmDMA_QM_3_PQ_PUSH2                                          0x418088

#define mmDMA_QM_3_PQ_PUSH3                                          0x41808C

#define mmDMA_QM_3_PQ_STS0                                           0x418090

#define mmDMA_QM_3_PQ_STS1                                           0x418094

#define mmDMA_QM_3_PQ_RD_RATE_LIM_EN                                 0x4180A0

#define mmDMA_QM_3_PQ_RD_RATE_LIM_RST_TOKEN                          0x4180A4

#define mmDMA_QM_3_PQ_RD_RATE_LIM_SAT                                0x4180A8

#define mmDMA_QM_3_PQ_RD_RATE_LIM_TOUT                               0x4180AC

#define mmDMA_QM_3_CQ_CFG0                                           0x4180B0

#define mmDMA_QM_3_CQ_CFG1                                           0x4180B4

#define mmDMA_QM_3_CQ_ARUSER                                         0x4180B8

#define mmDMA_QM_3_CQ_PTR_LO                                         0x4180C0

#define mmDMA_QM_3_CQ_PTR_HI                                         0x4180C4

#define mmDMA_QM_3_CQ_TSIZE                                          0x4180C8

#define mmDMA_QM_3_CQ_CTL                                            0x4180CC

#define mmDMA_QM_3_CQ_PTR_LO_STS                                     0x4180D4

#define mmDMA_QM_3_CQ_PTR_HI_STS                                     0x4180D8

#define mmDMA_QM_3_CQ_TSIZE_STS                                      0x4180DC

#define mmDMA_QM_3_CQ_CTL_STS                                        0x4180E0

#define mmDMA_QM_3_CQ_STS0                                           0x4180E4

#define mmDMA_QM_3_CQ_STS1                                           0x4180E8

#define mmDMA_QM_3_CQ_RD_RATE_LIM_EN                                 0x4180F0

#define mmDMA_QM_3_CQ_RD_RATE_LIM_RST_TOKEN                          0x4180F4

#define mmDMA_QM_3_CQ_RD_RATE_LIM_SAT                                0x4180F8

#define mmDMA_QM_3_CQ_RD_RATE_LIM_TOUT                               0x4180FC

#define mmDMA_QM_3_CQ_IFIFO_CNT                                      0x418108

#define mmDMA_QM_3_CP_MSG_BASE0_ADDR_LO                              0x418120

#define mmDMA_QM_3_CP_MSG_BASE0_ADDR_HI                              0x418124

#define mmDMA_QM_3_CP_MSG_BASE1_ADDR_LO                              0x418128

#define mmDMA_QM_3_CP_MSG_BASE1_ADDR_HI                              0x41812C

#define mmDMA_QM_3_CP_MSG_BASE2_ADDR_LO                              0x418130

#define mmDMA_QM_3_CP_MSG_BASE2_ADDR_HI                              0x418134

#define mmDMA_QM_3_CP_MSG_BASE3_ADDR_LO                              0x418138

#define mmDMA_QM_3_CP_MSG_BASE3_ADDR_HI                              0x41813C

#define mmDMA_QM_3_CP_LDMA_TSIZE_OFFSET                              0x418140

#define mmDMA_QM_3_CP_LDMA_SRC_BASE_LO_OFFSET                        0x418144

#define mmDMA_QM_3_CP_LDMA_SRC_BASE_HI_OFFSET                        0x418148

#define mmDMA_QM_3_CP_LDMA_DST_BASE_LO_OFFSET                        0x41814C

#define mmDMA_QM_3_CP_LDMA_DST_BASE_HI_OFFSET                        0x418150

#define mmDMA_QM_3_CP_LDMA_COMMIT_OFFSET                             0x418154

#define mmDMA_QM_3_CP_FENCE0_RDATA                                   0x418158

#define mmDMA_QM_3_CP_FENCE1_RDATA                                   0x41815C

#define mmDMA_QM_3_CP_FENCE2_RDATA                                   0x418160

#define mmDMA_QM_3_CP_FENCE3_RDATA                                   0x418164

#define mmDMA_QM_3_CP_FENCE0_CNT                                     0x418168

#define mmDMA_QM_3_CP_FENCE1_CNT                                     0x41816C

#define mmDMA_QM_3_CP_FENCE2_CNT                                     0x418170

#define mmDMA_QM_3_CP_FENCE3_CNT                                     0x418174

#define mmDMA_QM_3_CP_STS                                            0x418178

#define mmDMA_QM_3_CP_CURRENT_INST_LO                                0x41817C

#define mmDMA_QM_3_CP_CURRENT_INST_HI                                0x418180

#define mmDMA_QM_3_CP_BARRIER_CFG                                    0x418184

#define mmDMA_QM_3_CP_DBG_0                                          0x418188

#define mmDMA_QM_3_PQ_BUF_ADDR                                       0x418300

#define mmDMA_QM_3_PQ_BUF_RDATA                                      0x418304

#define mmDMA_QM_3_CQ_BUF_ADDR                                       0x418308

#define mmDMA_QM_3_CQ_BUF_RDATA                                      0x41830C

#endif /* ASIC_REG_DMA_QM_3_REGS_H_ */

