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

#ifndef ASIC_REG_DMA_QM_2_REGS_H_
#define ASIC_REG_DMA_QM_2_REGS_H_

/*
 *****************************************
 *   DMA_QM_2 (Prototype: QMAN)
 *****************************************
 */

#define mmDMA_QM_2_GLBL_CFG0                                         0x410000

#define mmDMA_QM_2_GLBL_CFG1                                         0x410004

#define mmDMA_QM_2_GLBL_PROT                                         0x410008

#define mmDMA_QM_2_GLBL_ERR_CFG                                      0x41000C

#define mmDMA_QM_2_GLBL_ERR_ADDR_LO                                  0x410010

#define mmDMA_QM_2_GLBL_ERR_ADDR_HI                                  0x410014

#define mmDMA_QM_2_GLBL_ERR_WDATA                                    0x410018

#define mmDMA_QM_2_GLBL_SECURE_PROPS                                 0x41001C

#define mmDMA_QM_2_GLBL_NON_SECURE_PROPS                             0x410020

#define mmDMA_QM_2_GLBL_STS0                                         0x410024

#define mmDMA_QM_2_GLBL_STS1                                         0x410028

#define mmDMA_QM_2_PQ_BASE_LO                                        0x410060

#define mmDMA_QM_2_PQ_BASE_HI                                        0x410064

#define mmDMA_QM_2_PQ_SIZE                                           0x410068

#define mmDMA_QM_2_PQ_PI                                             0x41006C

#define mmDMA_QM_2_PQ_CI                                             0x410070

#define mmDMA_QM_2_PQ_CFG0                                           0x410074

#define mmDMA_QM_2_PQ_CFG1                                           0x410078

#define mmDMA_QM_2_PQ_ARUSER                                         0x41007C

#define mmDMA_QM_2_PQ_PUSH0                                          0x410080

#define mmDMA_QM_2_PQ_PUSH1                                          0x410084

#define mmDMA_QM_2_PQ_PUSH2                                          0x410088

#define mmDMA_QM_2_PQ_PUSH3                                          0x41008C

#define mmDMA_QM_2_PQ_STS0                                           0x410090

#define mmDMA_QM_2_PQ_STS1                                           0x410094

#define mmDMA_QM_2_PQ_RD_RATE_LIM_EN                                 0x4100A0

#define mmDMA_QM_2_PQ_RD_RATE_LIM_RST_TOKEN                          0x4100A4

#define mmDMA_QM_2_PQ_RD_RATE_LIM_SAT                                0x4100A8

#define mmDMA_QM_2_PQ_RD_RATE_LIM_TOUT                               0x4100AC

#define mmDMA_QM_2_CQ_CFG0                                           0x4100B0

#define mmDMA_QM_2_CQ_CFG1                                           0x4100B4

#define mmDMA_QM_2_CQ_ARUSER                                         0x4100B8

#define mmDMA_QM_2_CQ_PTR_LO                                         0x4100C0

#define mmDMA_QM_2_CQ_PTR_HI                                         0x4100C4

#define mmDMA_QM_2_CQ_TSIZE                                          0x4100C8

#define mmDMA_QM_2_CQ_CTL                                            0x4100CC

#define mmDMA_QM_2_CQ_PTR_LO_STS                                     0x4100D4

#define mmDMA_QM_2_CQ_PTR_HI_STS                                     0x4100D8

#define mmDMA_QM_2_CQ_TSIZE_STS                                      0x4100DC

#define mmDMA_QM_2_CQ_CTL_STS                                        0x4100E0

#define mmDMA_QM_2_CQ_STS0                                           0x4100E4

#define mmDMA_QM_2_CQ_STS1                                           0x4100E8

#define mmDMA_QM_2_CQ_RD_RATE_LIM_EN                                 0x4100F0

#define mmDMA_QM_2_CQ_RD_RATE_LIM_RST_TOKEN                          0x4100F4

#define mmDMA_QM_2_CQ_RD_RATE_LIM_SAT                                0x4100F8

#define mmDMA_QM_2_CQ_RD_RATE_LIM_TOUT                               0x4100FC

#define mmDMA_QM_2_CQ_IFIFO_CNT                                      0x410108

#define mmDMA_QM_2_CP_MSG_BASE0_ADDR_LO                              0x410120

#define mmDMA_QM_2_CP_MSG_BASE0_ADDR_HI                              0x410124

#define mmDMA_QM_2_CP_MSG_BASE1_ADDR_LO                              0x410128

#define mmDMA_QM_2_CP_MSG_BASE1_ADDR_HI                              0x41012C

#define mmDMA_QM_2_CP_MSG_BASE2_ADDR_LO                              0x410130

#define mmDMA_QM_2_CP_MSG_BASE2_ADDR_HI                              0x410134

#define mmDMA_QM_2_CP_MSG_BASE3_ADDR_LO                              0x410138

#define mmDMA_QM_2_CP_MSG_BASE3_ADDR_HI                              0x41013C

#define mmDMA_QM_2_CP_LDMA_TSIZE_OFFSET                              0x410140

#define mmDMA_QM_2_CP_LDMA_SRC_BASE_LO_OFFSET                        0x410144

#define mmDMA_QM_2_CP_LDMA_SRC_BASE_HI_OFFSET                        0x410148

#define mmDMA_QM_2_CP_LDMA_DST_BASE_LO_OFFSET                        0x41014C

#define mmDMA_QM_2_CP_LDMA_DST_BASE_HI_OFFSET                        0x410150

#define mmDMA_QM_2_CP_LDMA_COMMIT_OFFSET                             0x410154

#define mmDMA_QM_2_CP_FENCE0_RDATA                                   0x410158

#define mmDMA_QM_2_CP_FENCE1_RDATA                                   0x41015C

#define mmDMA_QM_2_CP_FENCE2_RDATA                                   0x410160

#define mmDMA_QM_2_CP_FENCE3_RDATA                                   0x410164

#define mmDMA_QM_2_CP_FENCE0_CNT                                     0x410168

#define mmDMA_QM_2_CP_FENCE1_CNT                                     0x41016C

#define mmDMA_QM_2_CP_FENCE2_CNT                                     0x410170

#define mmDMA_QM_2_CP_FENCE3_CNT                                     0x410174

#define mmDMA_QM_2_CP_STS                                            0x410178

#define mmDMA_QM_2_CP_CURRENT_INST_LO                                0x41017C

#define mmDMA_QM_2_CP_CURRENT_INST_HI                                0x410180

#define mmDMA_QM_2_CP_BARRIER_CFG                                    0x410184

#define mmDMA_QM_2_CP_DBG_0                                          0x410188

#define mmDMA_QM_2_PQ_BUF_ADDR                                       0x410300

#define mmDMA_QM_2_PQ_BUF_RDATA                                      0x410304

#define mmDMA_QM_2_CQ_BUF_ADDR                                       0x410308

#define mmDMA_QM_2_CQ_BUF_RDATA                                      0x41030C

#endif /* ASIC_REG_DMA_QM_2_REGS_H_ */

