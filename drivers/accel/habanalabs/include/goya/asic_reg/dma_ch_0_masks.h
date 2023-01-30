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

#ifndef ASIC_REG_DMA_CH_0_MASKS_H_
#define ASIC_REG_DMA_CH_0_MASKS_H_

/*
 *****************************************
 *   DMA_CH_0 (Prototype: DMA_CH)
 *****************************************
 */

/* DMA_CH_0_CFG0 */
#define DMA_CH_0_CFG0_RD_MAX_OUTSTAND_SHIFT                          0
#define DMA_CH_0_CFG0_RD_MAX_OUTSTAND_MASK                           0x3FF
#define DMA_CH_0_CFG0_WR_MAX_OUTSTAND_SHIFT                          16
#define DMA_CH_0_CFG0_WR_MAX_OUTSTAND_MASK                           0xFFF0000

/* DMA_CH_0_CFG1 */
#define DMA_CH_0_CFG1_RD_BUF_MAX_SIZE_SHIFT                          0
#define DMA_CH_0_CFG1_RD_BUF_MAX_SIZE_MASK                           0x3FF

/* DMA_CH_0_ERRMSG_ADDR_LO */
#define DMA_CH_0_ERRMSG_ADDR_LO_VAL_SHIFT                            0
#define DMA_CH_0_ERRMSG_ADDR_LO_VAL_MASK                             0xFFFFFFFF

/* DMA_CH_0_ERRMSG_ADDR_HI */
#define DMA_CH_0_ERRMSG_ADDR_HI_VAL_SHIFT                            0
#define DMA_CH_0_ERRMSG_ADDR_HI_VAL_MASK                             0xFFFFFFFF

/* DMA_CH_0_ERRMSG_WDATA */
#define DMA_CH_0_ERRMSG_WDATA_VAL_SHIFT                              0
#define DMA_CH_0_ERRMSG_WDATA_VAL_MASK                               0xFFFFFFFF

/* DMA_CH_0_RD_COMP_ADDR_LO */
#define DMA_CH_0_RD_COMP_ADDR_LO_VAL_SHIFT                           0
#define DMA_CH_0_RD_COMP_ADDR_LO_VAL_MASK                            0xFFFFFFFF

/* DMA_CH_0_RD_COMP_ADDR_HI */
#define DMA_CH_0_RD_COMP_ADDR_HI_VAL_SHIFT                           0
#define DMA_CH_0_RD_COMP_ADDR_HI_VAL_MASK                            0xFFFFFFFF

/* DMA_CH_0_RD_COMP_WDATA */
#define DMA_CH_0_RD_COMP_WDATA_VAL_SHIFT                             0
#define DMA_CH_0_RD_COMP_WDATA_VAL_MASK                              0xFFFFFFFF

/* DMA_CH_0_WR_COMP_ADDR_LO */
#define DMA_CH_0_WR_COMP_ADDR_LO_VAL_SHIFT                           0
#define DMA_CH_0_WR_COMP_ADDR_LO_VAL_MASK                            0xFFFFFFFF

/* DMA_CH_0_WR_COMP_ADDR_HI */
#define DMA_CH_0_WR_COMP_ADDR_HI_VAL_SHIFT                           0
#define DMA_CH_0_WR_COMP_ADDR_HI_VAL_MASK                            0xFFFFFFFF

/* DMA_CH_0_WR_COMP_WDATA */
#define DMA_CH_0_WR_COMP_WDATA_VAL_SHIFT                             0
#define DMA_CH_0_WR_COMP_WDATA_VAL_MASK                              0xFFFFFFFF

/* DMA_CH_0_LDMA_SRC_ADDR_LO */
#define DMA_CH_0_LDMA_SRC_ADDR_LO_VAL_SHIFT                          0
#define DMA_CH_0_LDMA_SRC_ADDR_LO_VAL_MASK                           0xFFFFFFFF

/* DMA_CH_0_LDMA_SRC_ADDR_HI */
#define DMA_CH_0_LDMA_SRC_ADDR_HI_VAL_SHIFT                          0
#define DMA_CH_0_LDMA_SRC_ADDR_HI_VAL_MASK                           0xFFFFFFFF

/* DMA_CH_0_LDMA_DST_ADDR_LO */
#define DMA_CH_0_LDMA_DST_ADDR_LO_VAL_SHIFT                          0
#define DMA_CH_0_LDMA_DST_ADDR_LO_VAL_MASK                           0xFFFFFFFF

/* DMA_CH_0_LDMA_DST_ADDR_HI */
#define DMA_CH_0_LDMA_DST_ADDR_HI_VAL_SHIFT                          0
#define DMA_CH_0_LDMA_DST_ADDR_HI_VAL_MASK                           0xFFFFFFFF

/* DMA_CH_0_LDMA_TSIZE */
#define DMA_CH_0_LDMA_TSIZE_VAL_SHIFT                                0
#define DMA_CH_0_LDMA_TSIZE_VAL_MASK                                 0xFFFFFFFF

/* DMA_CH_0_COMIT_TRANSFER */
#define DMA_CH_0_COMIT_TRANSFER_PCI_UPS_WKORDR_SHIFT                 0
#define DMA_CH_0_COMIT_TRANSFER_PCI_UPS_WKORDR_MASK                  0x1
#define DMA_CH_0_COMIT_TRANSFER_RD_COMP_EN_SHIFT                     1
#define DMA_CH_0_COMIT_TRANSFER_RD_COMP_EN_MASK                      0x2
#define DMA_CH_0_COMIT_TRANSFER_WR_COMP_EN_SHIFT                     2
#define DMA_CH_0_COMIT_TRANSFER_WR_COMP_EN_MASK                      0x4
#define DMA_CH_0_COMIT_TRANSFER_NOSNOOP_SHIFT                        3
#define DMA_CH_0_COMIT_TRANSFER_NOSNOOP_MASK                         0x8
#define DMA_CH_0_COMIT_TRANSFER_SRC_ADDR_INC_DIS_SHIFT               4
#define DMA_CH_0_COMIT_TRANSFER_SRC_ADDR_INC_DIS_MASK                0x10
#define DMA_CH_0_COMIT_TRANSFER_DST_ADDR_INC_DIS_SHIFT               5
#define DMA_CH_0_COMIT_TRANSFER_DST_ADDR_INC_DIS_MASK                0x20
#define DMA_CH_0_COMIT_TRANSFER_MEM_SET_SHIFT                        6
#define DMA_CH_0_COMIT_TRANSFER_MEM_SET_MASK                         0x40
#define DMA_CH_0_COMIT_TRANSFER_MOD_TENSOR_SHIFT                     15
#define DMA_CH_0_COMIT_TRANSFER_MOD_TENSOR_MASK                      0x8000
#define DMA_CH_0_COMIT_TRANSFER_CTL_SHIFT                            16
#define DMA_CH_0_COMIT_TRANSFER_CTL_MASK                             0xFFFF0000

/* DMA_CH_0_STS0 */
#define DMA_CH_0_STS0_DMA_BUSY_SHIFT                                 0
#define DMA_CH_0_STS0_DMA_BUSY_MASK                                  0x1
#define DMA_CH_0_STS0_RD_STS_CTX_FULL_SHIFT                          1
#define DMA_CH_0_STS0_RD_STS_CTX_FULL_MASK                           0x2
#define DMA_CH_0_STS0_WR_STS_CTX_FULL_SHIFT                          2
#define DMA_CH_0_STS0_WR_STS_CTX_FULL_MASK                           0x4

/* DMA_CH_0_STS1 */
#define DMA_CH_0_STS1_RD_STS_CTX_CNT_SHIFT                           0
#define DMA_CH_0_STS1_RD_STS_CTX_CNT_MASK                            0xFFFFFFFF

/* DMA_CH_0_STS2 */
#define DMA_CH_0_STS2_WR_STS_CTX_CNT_SHIFT                           0
#define DMA_CH_0_STS2_WR_STS_CTX_CNT_MASK                            0xFFFFFFFF

/* DMA_CH_0_STS3 */
#define DMA_CH_0_STS3_RD_STS_TRN_CNT_SHIFT                           0
#define DMA_CH_0_STS3_RD_STS_TRN_CNT_MASK                            0xFFFFFFFF

/* DMA_CH_0_STS4 */
#define DMA_CH_0_STS4_WR_STS_TRN_CNT_SHIFT                           0
#define DMA_CH_0_STS4_WR_STS_TRN_CNT_MASK                            0xFFFFFFFF

/* DMA_CH_0_SRC_ADDR_LO_STS */
#define DMA_CH_0_SRC_ADDR_LO_STS_VAL_SHIFT                           0
#define DMA_CH_0_SRC_ADDR_LO_STS_VAL_MASK                            0xFFFFFFFF

/* DMA_CH_0_SRC_ADDR_HI_STS */
#define DMA_CH_0_SRC_ADDR_HI_STS_VAL_SHIFT                           0
#define DMA_CH_0_SRC_ADDR_HI_STS_VAL_MASK                            0xFFFFFFFF

/* DMA_CH_0_SRC_TSIZE_STS */
#define DMA_CH_0_SRC_TSIZE_STS_VAL_SHIFT                             0
#define DMA_CH_0_SRC_TSIZE_STS_VAL_MASK                              0xFFFFFFFF

/* DMA_CH_0_DST_ADDR_LO_STS */
#define DMA_CH_0_DST_ADDR_LO_STS_VAL_SHIFT                           0
#define DMA_CH_0_DST_ADDR_LO_STS_VAL_MASK                            0xFFFFFFFF

/* DMA_CH_0_DST_ADDR_HI_STS */
#define DMA_CH_0_DST_ADDR_HI_STS_VAL_SHIFT                           0
#define DMA_CH_0_DST_ADDR_HI_STS_VAL_MASK                            0xFFFFFFFF

/* DMA_CH_0_DST_TSIZE_STS */
#define DMA_CH_0_DST_TSIZE_STS_VAL_SHIFT                             0
#define DMA_CH_0_DST_TSIZE_STS_VAL_MASK                              0xFFFFFFFF

/* DMA_CH_0_RD_RATE_LIM_EN */
#define DMA_CH_0_RD_RATE_LIM_EN_VAL_SHIFT                            0
#define DMA_CH_0_RD_RATE_LIM_EN_VAL_MASK                             0x1

/* DMA_CH_0_RD_RATE_LIM_RST_TOKEN */
#define DMA_CH_0_RD_RATE_LIM_RST_TOKEN_VAL_SHIFT                     0
#define DMA_CH_0_RD_RATE_LIM_RST_TOKEN_VAL_MASK                      0xFFFF

/* DMA_CH_0_RD_RATE_LIM_SAT */
#define DMA_CH_0_RD_RATE_LIM_SAT_VAL_SHIFT                           0
#define DMA_CH_0_RD_RATE_LIM_SAT_VAL_MASK                            0xFFFF

/* DMA_CH_0_RD_RATE_LIM_TOUT */
#define DMA_CH_0_RD_RATE_LIM_TOUT_VAL_SHIFT                          0
#define DMA_CH_0_RD_RATE_LIM_TOUT_VAL_MASK                           0x7FFFFFFF

/* DMA_CH_0_WR_RATE_LIM_EN */
#define DMA_CH_0_WR_RATE_LIM_EN_VAL_SHIFT                            0
#define DMA_CH_0_WR_RATE_LIM_EN_VAL_MASK                             0x1

/* DMA_CH_0_WR_RATE_LIM_RST_TOKEN */
#define DMA_CH_0_WR_RATE_LIM_RST_TOKEN_VAL_SHIFT                     0
#define DMA_CH_0_WR_RATE_LIM_RST_TOKEN_VAL_MASK                      0xFFFF

/* DMA_CH_0_WR_RATE_LIM_SAT */
#define DMA_CH_0_WR_RATE_LIM_SAT_VAL_SHIFT                           0
#define DMA_CH_0_WR_RATE_LIM_SAT_VAL_MASK                            0xFFFF

/* DMA_CH_0_WR_RATE_LIM_TOUT */
#define DMA_CH_0_WR_RATE_LIM_TOUT_VAL_SHIFT                          0
#define DMA_CH_0_WR_RATE_LIM_TOUT_VAL_MASK                           0x7FFFFFFF

/* DMA_CH_0_CFG2 */
#define DMA_CH_0_CFG2_FORCE_WORD_SHIFT                               0
#define DMA_CH_0_CFG2_FORCE_WORD_MASK                                0x1

/* DMA_CH_0_TDMA_CTL */
#define DMA_CH_0_TDMA_CTL_DTYPE_SHIFT                                0
#define DMA_CH_0_TDMA_CTL_DTYPE_MASK                                 0x7

/* DMA_CH_0_TDMA_SRC_BASE_ADDR_LO */
#define DMA_CH_0_TDMA_SRC_BASE_ADDR_LO_VAL_SHIFT                     0
#define DMA_CH_0_TDMA_SRC_BASE_ADDR_LO_VAL_MASK                      0xFFFFFFFF

/* DMA_CH_0_TDMA_SRC_BASE_ADDR_HI */
#define DMA_CH_0_TDMA_SRC_BASE_ADDR_HI_VAL_SHIFT                     0
#define DMA_CH_0_TDMA_SRC_BASE_ADDR_HI_VAL_MASK                      0xFFFFFFFF

/* DMA_CH_0_TDMA_SRC_ROI_BASE_0 */
#define DMA_CH_0_TDMA_SRC_ROI_BASE_0_VAL_SHIFT                       0
#define DMA_CH_0_TDMA_SRC_ROI_BASE_0_VAL_MASK                        0xFFFFFFFF

/* DMA_CH_0_TDMA_SRC_ROI_SIZE_0 */
#define DMA_CH_0_TDMA_SRC_ROI_SIZE_0_VAL_SHIFT                       0
#define DMA_CH_0_TDMA_SRC_ROI_SIZE_0_VAL_MASK                        0xFFFFFFFF

/* DMA_CH_0_TDMA_SRC_VALID_ELEMENTS_0 */
#define DMA_CH_0_TDMA_SRC_VALID_ELEMENTS_0_VAL_SHIFT                 0
#define DMA_CH_0_TDMA_SRC_VALID_ELEMENTS_0_VAL_MASK                  0xFFFFFFFF

/* DMA_CH_0_TDMA_SRC_START_OFFSET_0 */
#define DMA_CH_0_TDMA_SRC_START_OFFSET_0_VAL_SHIFT                   0
#define DMA_CH_0_TDMA_SRC_START_OFFSET_0_VAL_MASK                    0xFFFFFFFF

/* DMA_CH_0_TDMA_SRC_STRIDE_0 */
#define DMA_CH_0_TDMA_SRC_STRIDE_0_VAL_SHIFT                         0
#define DMA_CH_0_TDMA_SRC_STRIDE_0_VAL_MASK                          0xFFFFFFFF

/* DMA_CH_0_TDMA_SRC_ROI_BASE_1 */
#define DMA_CH_0_TDMA_SRC_ROI_BASE_1_VAL_SHIFT                       0
#define DMA_CH_0_TDMA_SRC_ROI_BASE_1_VAL_MASK                        0xFFFFFFFF

/* DMA_CH_0_TDMA_SRC_ROI_SIZE_1 */
#define DMA_CH_0_TDMA_SRC_ROI_SIZE_1_VAL_SHIFT                       0
#define DMA_CH_0_TDMA_SRC_ROI_SIZE_1_VAL_MASK                        0xFFFFFFFF

/* DMA_CH_0_TDMA_SRC_VALID_ELEMENTS_1 */
#define DMA_CH_0_TDMA_SRC_VALID_ELEMENTS_1_VAL_SHIFT                 0
#define DMA_CH_0_TDMA_SRC_VALID_ELEMENTS_1_VAL_MASK                  0xFFFFFFFF

/* DMA_CH_0_TDMA_SRC_START_OFFSET_1 */
#define DMA_CH_0_TDMA_SRC_START_OFFSET_1_VAL_SHIFT                   0
#define DMA_CH_0_TDMA_SRC_START_OFFSET_1_VAL_MASK                    0xFFFFFFFF

/* DMA_CH_0_TDMA_SRC_STRIDE_1 */
#define DMA_CH_0_TDMA_SRC_STRIDE_1_VAL_SHIFT                         0
#define DMA_CH_0_TDMA_SRC_STRIDE_1_VAL_MASK                          0xFFFFFFFF

/* DMA_CH_0_TDMA_SRC_ROI_BASE_2 */
#define DMA_CH_0_TDMA_SRC_ROI_BASE_2_VAL_SHIFT                       0
#define DMA_CH_0_TDMA_SRC_ROI_BASE_2_VAL_MASK                        0xFFFFFFFF

/* DMA_CH_0_TDMA_SRC_ROI_SIZE_2 */
#define DMA_CH_0_TDMA_SRC_ROI_SIZE_2_VAL_SHIFT                       0
#define DMA_CH_0_TDMA_SRC_ROI_SIZE_2_VAL_MASK                        0xFFFFFFFF

/* DMA_CH_0_TDMA_SRC_VALID_ELEMENTS_2 */
#define DMA_CH_0_TDMA_SRC_VALID_ELEMENTS_2_VAL_SHIFT                 0
#define DMA_CH_0_TDMA_SRC_VALID_ELEMENTS_2_VAL_MASK                  0xFFFFFFFF

/* DMA_CH_0_TDMA_SRC_START_OFFSET_2 */
#define DMA_CH_0_TDMA_SRC_START_OFFSET_2_VAL_SHIFT                   0
#define DMA_CH_0_TDMA_SRC_START_OFFSET_2_VAL_MASK                    0xFFFFFFFF

/* DMA_CH_0_TDMA_SRC_STRIDE_2 */
#define DMA_CH_0_TDMA_SRC_STRIDE_2_VAL_SHIFT                         0
#define DMA_CH_0_TDMA_SRC_STRIDE_2_VAL_MASK                          0xFFFFFFFF

/* DMA_CH_0_TDMA_SRC_ROI_BASE_3 */
#define DMA_CH_0_TDMA_SRC_ROI_BASE_3_VAL_SHIFT                       0
#define DMA_CH_0_TDMA_SRC_ROI_BASE_3_VAL_MASK                        0xFFFFFFFF

/* DMA_CH_0_TDMA_SRC_ROI_SIZE_3 */
#define DMA_CH_0_TDMA_SRC_ROI_SIZE_3_VAL_SHIFT                       0
#define DMA_CH_0_TDMA_SRC_ROI_SIZE_3_VAL_MASK                        0xFFFFFFFF

/* DMA_CH_0_TDMA_SRC_VALID_ELEMENTS_3 */
#define DMA_CH_0_TDMA_SRC_VALID_ELEMENTS_3_VAL_SHIFT                 0
#define DMA_CH_0_TDMA_SRC_VALID_ELEMENTS_3_VAL_MASK                  0xFFFFFFFF

/* DMA_CH_0_TDMA_SRC_START_OFFSET_3 */
#define DMA_CH_0_TDMA_SRC_START_OFFSET_3_VAL_SHIFT                   0
#define DMA_CH_0_TDMA_SRC_START_OFFSET_3_VAL_MASK                    0xFFFFFFFF

/* DMA_CH_0_TDMA_SRC_STRIDE_3 */
#define DMA_CH_0_TDMA_SRC_STRIDE_3_VAL_SHIFT                         0
#define DMA_CH_0_TDMA_SRC_STRIDE_3_VAL_MASK                          0xFFFFFFFF

/* DMA_CH_0_TDMA_SRC_ROI_BASE_4 */
#define DMA_CH_0_TDMA_SRC_ROI_BASE_4_VAL_SHIFT                       0
#define DMA_CH_0_TDMA_SRC_ROI_BASE_4_VAL_MASK                        0xFFFFFFFF

/* DMA_CH_0_TDMA_SRC_ROI_SIZE_4 */
#define DMA_CH_0_TDMA_SRC_ROI_SIZE_4_VAL_SHIFT                       0
#define DMA_CH_0_TDMA_SRC_ROI_SIZE_4_VAL_MASK                        0xFFFFFFFF

/* DMA_CH_0_TDMA_SRC_VALID_ELEMENTS_4 */
#define DMA_CH_0_TDMA_SRC_VALID_ELEMENTS_4_VAL_SHIFT                 0
#define DMA_CH_0_TDMA_SRC_VALID_ELEMENTS_4_VAL_MASK                  0xFFFFFFFF

/* DMA_CH_0_TDMA_SRC_START_OFFSET_4 */
#define DMA_CH_0_TDMA_SRC_START_OFFSET_4_VAL_SHIFT                   0
#define DMA_CH_0_TDMA_SRC_START_OFFSET_4_VAL_MASK                    0xFFFFFFFF

/* DMA_CH_0_TDMA_SRC_STRIDE_4 */
#define DMA_CH_0_TDMA_SRC_STRIDE_4_VAL_SHIFT                         0
#define DMA_CH_0_TDMA_SRC_STRIDE_4_VAL_MASK                          0xFFFFFFFF

/* DMA_CH_0_TDMA_DST_BASE_ADDR_LO */
#define DMA_CH_0_TDMA_DST_BASE_ADDR_LO_VAL_SHIFT                     0
#define DMA_CH_0_TDMA_DST_BASE_ADDR_LO_VAL_MASK                      0xFFFFFFFF

/* DMA_CH_0_TDMA_DST_BASE_ADDR_HI */
#define DMA_CH_0_TDMA_DST_BASE_ADDR_HI_VAL_SHIFT                     0
#define DMA_CH_0_TDMA_DST_BASE_ADDR_HI_VAL_MASK                      0xFFFFFFFF

/* DMA_CH_0_TDMA_DST_ROI_BASE_0 */
#define DMA_CH_0_TDMA_DST_ROI_BASE_0_VAL_SHIFT                       0
#define DMA_CH_0_TDMA_DST_ROI_BASE_0_VAL_MASK                        0xFFFFFFFF

/* DMA_CH_0_TDMA_DST_ROI_SIZE_0 */
#define DMA_CH_0_TDMA_DST_ROI_SIZE_0_VAL_SHIFT                       0
#define DMA_CH_0_TDMA_DST_ROI_SIZE_0_VAL_MASK                        0xFFFFFFFF

/* DMA_CH_0_TDMA_DST_VALID_ELEMENTS_0 */
#define DMA_CH_0_TDMA_DST_VALID_ELEMENTS_0_VAL_SHIFT                 0
#define DMA_CH_0_TDMA_DST_VALID_ELEMENTS_0_VAL_MASK                  0xFFFFFFFF

/* DMA_CH_0_TDMA_DST_START_OFFSET_0 */
#define DMA_CH_0_TDMA_DST_START_OFFSET_0_VAL_SHIFT                   0
#define DMA_CH_0_TDMA_DST_START_OFFSET_0_VAL_MASK                    0xFFFFFFFF

/* DMA_CH_0_TDMA_DST_STRIDE_0 */
#define DMA_CH_0_TDMA_DST_STRIDE_0_VAL_SHIFT                         0
#define DMA_CH_0_TDMA_DST_STRIDE_0_VAL_MASK                          0xFFFFFFFF

/* DMA_CH_0_TDMA_DST_ROI_BASE_1 */
#define DMA_CH_0_TDMA_DST_ROI_BASE_1_VAL_SHIFT                       0
#define DMA_CH_0_TDMA_DST_ROI_BASE_1_VAL_MASK                        0xFFFFFFFF

/* DMA_CH_0_TDMA_DST_ROI_SIZE_1 */
#define DMA_CH_0_TDMA_DST_ROI_SIZE_1_VAL_SHIFT                       0
#define DMA_CH_0_TDMA_DST_ROI_SIZE_1_VAL_MASK                        0xFFFFFFFF

/* DMA_CH_0_TDMA_DST_VALID_ELEMENTS_1 */
#define DMA_CH_0_TDMA_DST_VALID_ELEMENTS_1_VAL_SHIFT                 0
#define DMA_CH_0_TDMA_DST_VALID_ELEMENTS_1_VAL_MASK                  0xFFFFFFFF

/* DMA_CH_0_TDMA_DST_START_OFFSET_1 */
#define DMA_CH_0_TDMA_DST_START_OFFSET_1_VAL_SHIFT                   0
#define DMA_CH_0_TDMA_DST_START_OFFSET_1_VAL_MASK                    0xFFFFFFFF

/* DMA_CH_0_TDMA_DST_STRIDE_1 */
#define DMA_CH_0_TDMA_DST_STRIDE_1_VAL_SHIFT                         0
#define DMA_CH_0_TDMA_DST_STRIDE_1_VAL_MASK                          0xFFFFFFFF

/* DMA_CH_0_TDMA_DST_ROI_BASE_2 */
#define DMA_CH_0_TDMA_DST_ROI_BASE_2_VAL_SHIFT                       0
#define DMA_CH_0_TDMA_DST_ROI_BASE_2_VAL_MASK                        0xFFFFFFFF

/* DMA_CH_0_TDMA_DST_ROI_SIZE_2 */
#define DMA_CH_0_TDMA_DST_ROI_SIZE_2_VAL_SHIFT                       0
#define DMA_CH_0_TDMA_DST_ROI_SIZE_2_VAL_MASK                        0xFFFFFFFF

/* DMA_CH_0_TDMA_DST_VALID_ELEMENTS_2 */
#define DMA_CH_0_TDMA_DST_VALID_ELEMENTS_2_VAL_SHIFT                 0
#define DMA_CH_0_TDMA_DST_VALID_ELEMENTS_2_VAL_MASK                  0xFFFFFFFF

/* DMA_CH_0_TDMA_DST_START_OFFSET_2 */
#define DMA_CH_0_TDMA_DST_START_OFFSET_2_VAL_SHIFT                   0
#define DMA_CH_0_TDMA_DST_START_OFFSET_2_VAL_MASK                    0xFFFFFFFF

/* DMA_CH_0_TDMA_DST_STRIDE_2 */
#define DMA_CH_0_TDMA_DST_STRIDE_2_VAL_SHIFT                         0
#define DMA_CH_0_TDMA_DST_STRIDE_2_VAL_MASK                          0xFFFFFFFF

/* DMA_CH_0_TDMA_DST_ROI_BASE_3 */
#define DMA_CH_0_TDMA_DST_ROI_BASE_3_VAL_SHIFT                       0
#define DMA_CH_0_TDMA_DST_ROI_BASE_3_VAL_MASK                        0xFFFFFFFF

/* DMA_CH_0_TDMA_DST_ROI_SIZE_3 */
#define DMA_CH_0_TDMA_DST_ROI_SIZE_3_VAL_SHIFT                       0
#define DMA_CH_0_TDMA_DST_ROI_SIZE_3_VAL_MASK                        0xFFFFFFFF

/* DMA_CH_0_TDMA_DST_VALID_ELEMENTS_3 */
#define DMA_CH_0_TDMA_DST_VALID_ELEMENTS_3_VAL_SHIFT                 0
#define DMA_CH_0_TDMA_DST_VALID_ELEMENTS_3_VAL_MASK                  0xFFFFFFFF

/* DMA_CH_0_TDMA_DST_START_OFFSET_3 */
#define DMA_CH_0_TDMA_DST_START_OFFSET_3_VAL_SHIFT                   0
#define DMA_CH_0_TDMA_DST_START_OFFSET_3_VAL_MASK                    0xFFFFFFFF

/* DMA_CH_0_TDMA_DST_STRIDE_3 */
#define DMA_CH_0_TDMA_DST_STRIDE_3_VAL_SHIFT                         0
#define DMA_CH_0_TDMA_DST_STRIDE_3_VAL_MASK                          0xFFFFFFFF

/* DMA_CH_0_TDMA_DST_ROI_BASE_4 */
#define DMA_CH_0_TDMA_DST_ROI_BASE_4_VAL_SHIFT                       0
#define DMA_CH_0_TDMA_DST_ROI_BASE_4_VAL_MASK                        0xFFFFFFFF

/* DMA_CH_0_TDMA_DST_ROI_SIZE_4 */
#define DMA_CH_0_TDMA_DST_ROI_SIZE_4_VAL_SHIFT                       0
#define DMA_CH_0_TDMA_DST_ROI_SIZE_4_VAL_MASK                        0xFFFFFFFF

/* DMA_CH_0_TDMA_DST_VALID_ELEMENTS_4 */
#define DMA_CH_0_TDMA_DST_VALID_ELEMENTS_4_VAL_SHIFT                 0
#define DMA_CH_0_TDMA_DST_VALID_ELEMENTS_4_VAL_MASK                  0xFFFFFFFF

/* DMA_CH_0_TDMA_DST_START_OFFSET_4 */
#define DMA_CH_0_TDMA_DST_START_OFFSET_4_VAL_SHIFT                   0
#define DMA_CH_0_TDMA_DST_START_OFFSET_4_VAL_MASK                    0xFFFFFFFF

/* DMA_CH_0_TDMA_DST_STRIDE_4 */
#define DMA_CH_0_TDMA_DST_STRIDE_4_VAL_SHIFT                         0
#define DMA_CH_0_TDMA_DST_STRIDE_4_VAL_MASK                          0xFFFFFFFF

/* DMA_CH_0_MEM_INIT_BUSY */
#define DMA_CH_0_MEM_INIT_BUSY_SBC_DATA_SHIFT                        0
#define DMA_CH_0_MEM_INIT_BUSY_SBC_DATA_MASK                         0xFF
#define DMA_CH_0_MEM_INIT_BUSY_SBC_MD_SHIFT                          8
#define DMA_CH_0_MEM_INIT_BUSY_SBC_MD_MASK                           0x100

#endif /* ASIC_REG_DMA_CH_0_MASKS_H_ */
