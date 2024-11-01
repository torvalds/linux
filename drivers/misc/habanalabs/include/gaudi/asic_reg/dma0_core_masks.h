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

#ifndef ASIC_REG_DMA0_CORE_MASKS_H_
#define ASIC_REG_DMA0_CORE_MASKS_H_

/*
 *****************************************
 *   DMA0_CORE (Prototype: DMA_CORE)
 *****************************************
 */

/* DMA0_CORE_CFG_0 */
#define DMA0_CORE_CFG_0_EN_SHIFT                                     0
#define DMA0_CORE_CFG_0_EN_MASK                                      0x1

/* DMA0_CORE_CFG_1 */
#define DMA0_CORE_CFG_1_HALT_SHIFT                                   0
#define DMA0_CORE_CFG_1_HALT_MASK                                    0x1
#define DMA0_CORE_CFG_1_FLUSH_SHIFT                                  1
#define DMA0_CORE_CFG_1_FLUSH_MASK                                   0x2
#define DMA0_CORE_CFG_1_SB_FORCE_MISS_SHIFT                          2
#define DMA0_CORE_CFG_1_SB_FORCE_MISS_MASK                           0x4

/* DMA0_CORE_LBW_MAX_OUTSTAND */
#define DMA0_CORE_LBW_MAX_OUTSTAND_VAL_SHIFT                         0
#define DMA0_CORE_LBW_MAX_OUTSTAND_VAL_MASK                          0x1F

/* DMA0_CORE_SRC_BASE_LO */
#define DMA0_CORE_SRC_BASE_LO_VAL_SHIFT                              0
#define DMA0_CORE_SRC_BASE_LO_VAL_MASK                               0xFFFFFFFF

/* DMA0_CORE_SRC_BASE_HI */
#define DMA0_CORE_SRC_BASE_HI_VAL_SHIFT                              0
#define DMA0_CORE_SRC_BASE_HI_VAL_MASK                               0xFFFFFFFF

/* DMA0_CORE_DST_BASE_LO */
#define DMA0_CORE_DST_BASE_LO_VAL_SHIFT                              0
#define DMA0_CORE_DST_BASE_LO_VAL_MASK                               0xFFFFFFFF

/* DMA0_CORE_DST_BASE_HI */
#define DMA0_CORE_DST_BASE_HI_VAL_SHIFT                              0
#define DMA0_CORE_DST_BASE_HI_VAL_MASK                               0xFFFFFF
#define DMA0_CORE_DST_BASE_HI_CTX_ID_HI_SHIFT                        24
#define DMA0_CORE_DST_BASE_HI_CTX_ID_HI_MASK                         0xFF000000

/* DMA0_CORE_SRC_TSIZE_1 */
#define DMA0_CORE_SRC_TSIZE_1_VAL_SHIFT                              0
#define DMA0_CORE_SRC_TSIZE_1_VAL_MASK                               0xFFFFFFFF

/* DMA0_CORE_SRC_STRIDE_1 */
#define DMA0_CORE_SRC_STRIDE_1_VAL_SHIFT                             0
#define DMA0_CORE_SRC_STRIDE_1_VAL_MASK                              0xFFFFFFFF

/* DMA0_CORE_SRC_TSIZE_2 */
#define DMA0_CORE_SRC_TSIZE_2_VAL_SHIFT                              0
#define DMA0_CORE_SRC_TSIZE_2_VAL_MASK                               0xFFFFFFFF

/* DMA0_CORE_SRC_STRIDE_2 */
#define DMA0_CORE_SRC_STRIDE_2_VAL_SHIFT                             0
#define DMA0_CORE_SRC_STRIDE_2_VAL_MASK                              0xFFFFFFFF

/* DMA0_CORE_SRC_TSIZE_3 */
#define DMA0_CORE_SRC_TSIZE_3_VAL_SHIFT                              0
#define DMA0_CORE_SRC_TSIZE_3_VAL_MASK                               0xFFFFFFFF

/* DMA0_CORE_SRC_STRIDE_3 */
#define DMA0_CORE_SRC_STRIDE_3_VAL_SHIFT                             0
#define DMA0_CORE_SRC_STRIDE_3_VAL_MASK                              0xFFFFFFFF

/* DMA0_CORE_SRC_TSIZE_4 */
#define DMA0_CORE_SRC_TSIZE_4_VAL_SHIFT                              0
#define DMA0_CORE_SRC_TSIZE_4_VAL_MASK                               0xFFFFFFFF

/* DMA0_CORE_SRC_STRIDE_4 */
#define DMA0_CORE_SRC_STRIDE_4_VAL_SHIFT                             0
#define DMA0_CORE_SRC_STRIDE_4_VAL_MASK                              0xFFFFFFFF

/* DMA0_CORE_SRC_TSIZE_0 */
#define DMA0_CORE_SRC_TSIZE_0_VAL_SHIFT                              0
#define DMA0_CORE_SRC_TSIZE_0_VAL_MASK                               0xFFFFFFFF

/* DMA0_CORE_DST_TSIZE_1 */
#define DMA0_CORE_DST_TSIZE_1_VAL_SHIFT                              0
#define DMA0_CORE_DST_TSIZE_1_VAL_MASK                               0xFFFFFFFF

/* DMA0_CORE_DST_STRIDE_1 */
#define DMA0_CORE_DST_STRIDE_1_VAL_SHIFT                             0
#define DMA0_CORE_DST_STRIDE_1_VAL_MASK                              0xFFFFFFFF

/* DMA0_CORE_DST_TSIZE_2 */
#define DMA0_CORE_DST_TSIZE_2_VAL_SHIFT                              0
#define DMA0_CORE_DST_TSIZE_2_VAL_MASK                               0xFFFFFFFF

/* DMA0_CORE_DST_STRIDE_2 */
#define DMA0_CORE_DST_STRIDE_2_VAL_SHIFT                             0
#define DMA0_CORE_DST_STRIDE_2_VAL_MASK                              0xFFFFFFFF

/* DMA0_CORE_DST_TSIZE_3 */
#define DMA0_CORE_DST_TSIZE_3_VAL_SHIFT                              0
#define DMA0_CORE_DST_TSIZE_3_VAL_MASK                               0xFFFFFFFF

/* DMA0_CORE_DST_STRIDE_3 */
#define DMA0_CORE_DST_STRIDE_3_VAL_SHIFT                             0
#define DMA0_CORE_DST_STRIDE_3_VAL_MASK                              0xFFFFFFFF

/* DMA0_CORE_DST_TSIZE_4 */
#define DMA0_CORE_DST_TSIZE_4_VAL_SHIFT                              0
#define DMA0_CORE_DST_TSIZE_4_VAL_MASK                               0xFFFFFFFF

/* DMA0_CORE_DST_STRIDE_4 */
#define DMA0_CORE_DST_STRIDE_4_VAL_SHIFT                             0
#define DMA0_CORE_DST_STRIDE_4_VAL_MASK                              0xFFFFFFFF

/* DMA0_CORE_DST_TSIZE_0 */
#define DMA0_CORE_DST_TSIZE_0_VAL_SHIFT                              0
#define DMA0_CORE_DST_TSIZE_0_VAL_MASK                               0xFFFFFFFF

/* DMA0_CORE_COMMIT */
#define DMA0_CORE_COMMIT_WR_COMP_EN_SHIFT                            0
#define DMA0_CORE_COMMIT_WR_COMP_EN_MASK                             0x1
#define DMA0_CORE_COMMIT_TRANSPOSE_SHIFT                             1
#define DMA0_CORE_COMMIT_TRANSPOSE_MASK                              0x2
#define DMA0_CORE_COMMIT_DTYPE_SHIFT                                 2
#define DMA0_CORE_COMMIT_DTYPE_MASK                                  0x4
#define DMA0_CORE_COMMIT_LIN_SHIFT                                   3
#define DMA0_CORE_COMMIT_LIN_MASK                                    0x8
#define DMA0_CORE_COMMIT_MEM_SET_SHIFT                               4
#define DMA0_CORE_COMMIT_MEM_SET_MASK                                0x10
#define DMA0_CORE_COMMIT_COMPRESS_SHIFT                              5
#define DMA0_CORE_COMMIT_COMPRESS_MASK                               0x20
#define DMA0_CORE_COMMIT_DECOMPRESS_SHIFT                            6
#define DMA0_CORE_COMMIT_DECOMPRESS_MASK                             0x40
#define DMA0_CORE_COMMIT_CTX_ID_SHIFT                                16
#define DMA0_CORE_COMMIT_CTX_ID_MASK                                 0xFF0000

/* DMA0_CORE_WR_COMP_WDATA */
#define DMA0_CORE_WR_COMP_WDATA_VAL_SHIFT                            0
#define DMA0_CORE_WR_COMP_WDATA_VAL_MASK                             0xFFFFFFFF

/* DMA0_CORE_WR_COMP_ADDR_LO */
#define DMA0_CORE_WR_COMP_ADDR_LO_VAL_SHIFT                          0
#define DMA0_CORE_WR_COMP_ADDR_LO_VAL_MASK                           0xFFFFFFFF

/* DMA0_CORE_WR_COMP_ADDR_HI */
#define DMA0_CORE_WR_COMP_ADDR_HI_VAL_SHIFT                          0
#define DMA0_CORE_WR_COMP_ADDR_HI_VAL_MASK                           0xFFFFFFFF

/* DMA0_CORE_WR_COMP_AWUSER_31_11 */
#define DMA0_CORE_WR_COMP_AWUSER_31_11_VAL_SHIFT                     0
#define DMA0_CORE_WR_COMP_AWUSER_31_11_VAL_MASK                      0x1FFFFF

/* DMA0_CORE_TE_NUMROWS */
#define DMA0_CORE_TE_NUMROWS_VAL_SHIFT                               0
#define DMA0_CORE_TE_NUMROWS_VAL_MASK                                0xFFFFFFFF

/* DMA0_CORE_PROT */
#define DMA0_CORE_PROT_VAL_SHIFT                                     0
#define DMA0_CORE_PROT_VAL_MASK                                      0x1
#define DMA0_CORE_PROT_ERR_VAL_SHIFT                                 1
#define DMA0_CORE_PROT_ERR_VAL_MASK                                  0x2

/* DMA0_CORE_SECURE_PROPS */
#define DMA0_CORE_SECURE_PROPS_ASID_SHIFT                            0
#define DMA0_CORE_SECURE_PROPS_ASID_MASK                             0x3FF
#define DMA0_CORE_SECURE_PROPS_MMBP_SHIFT                            10
#define DMA0_CORE_SECURE_PROPS_MMBP_MASK                             0x400

/* DMA0_CORE_NON_SECURE_PROPS */
#define DMA0_CORE_NON_SECURE_PROPS_ASID_SHIFT                        0
#define DMA0_CORE_NON_SECURE_PROPS_ASID_MASK                         0x3FF
#define DMA0_CORE_NON_SECURE_PROPS_MMBP_SHIFT                        10
#define DMA0_CORE_NON_SECURE_PROPS_MMBP_MASK                         0x400

/* DMA0_CORE_RD_MAX_OUTSTAND */
#define DMA0_CORE_RD_MAX_OUTSTAND_VAL_SHIFT                          0
#define DMA0_CORE_RD_MAX_OUTSTAND_VAL_MASK                           0xFFF

/* DMA0_CORE_RD_MAX_SIZE */
#define DMA0_CORE_RD_MAX_SIZE_DATA_SHIFT                             0
#define DMA0_CORE_RD_MAX_SIZE_DATA_MASK                              0x7FF
#define DMA0_CORE_RD_MAX_SIZE_MD_SHIFT                               16
#define DMA0_CORE_RD_MAX_SIZE_MD_MASK                                0x7FF0000

/* DMA0_CORE_RD_ARCACHE */
#define DMA0_CORE_RD_ARCACHE_VAL_SHIFT                               0
#define DMA0_CORE_RD_ARCACHE_VAL_MASK                                0xF

/* DMA0_CORE_RD_ARUSER_31_11 */
#define DMA0_CORE_RD_ARUSER_31_11_VAL_SHIFT                          0
#define DMA0_CORE_RD_ARUSER_31_11_VAL_MASK                           0x1FFFFF

/* DMA0_CORE_RD_INFLIGHTS */
#define DMA0_CORE_RD_INFLIGHTS_VAL_SHIFT                             0
#define DMA0_CORE_RD_INFLIGHTS_VAL_MASK                              0xFFFFFFFF

/* DMA0_CORE_WR_MAX_OUTSTAND */
#define DMA0_CORE_WR_MAX_OUTSTAND_VAL_SHIFT                          0
#define DMA0_CORE_WR_MAX_OUTSTAND_VAL_MASK                           0xFFF

/* DMA0_CORE_WR_MAX_AWID */
#define DMA0_CORE_WR_MAX_AWID_VAL_SHIFT                              0
#define DMA0_CORE_WR_MAX_AWID_VAL_MASK                               0xFFFF

/* DMA0_CORE_WR_AWCACHE */
#define DMA0_CORE_WR_AWCACHE_VAL_SHIFT                               0
#define DMA0_CORE_WR_AWCACHE_VAL_MASK                                0xF

/* DMA0_CORE_WR_AWUSER_31_11 */
#define DMA0_CORE_WR_AWUSER_31_11_VAL_SHIFT                          0
#define DMA0_CORE_WR_AWUSER_31_11_VAL_MASK                           0x1FFFFF

/* DMA0_CORE_WR_INFLIGHTS */
#define DMA0_CORE_WR_INFLIGHTS_VAL_SHIFT                             0
#define DMA0_CORE_WR_INFLIGHTS_VAL_MASK                              0xFFFF

/* DMA0_CORE_RD_RATE_LIM_CFG_0 */
#define DMA0_CORE_RD_RATE_LIM_CFG_0_RST_TOKEN_SHIFT                  0
#define DMA0_CORE_RD_RATE_LIM_CFG_0_RST_TOKEN_MASK                   0xFF
#define DMA0_CORE_RD_RATE_LIM_CFG_0_SAT_SHIFT                        16
#define DMA0_CORE_RD_RATE_LIM_CFG_0_SAT_MASK                         0xFF0000

/* DMA0_CORE_RD_RATE_LIM_CFG_1 */
#define DMA0_CORE_RD_RATE_LIM_CFG_1_TOUT_SHIFT                       0
#define DMA0_CORE_RD_RATE_LIM_CFG_1_TOUT_MASK                        0xFF
#define DMA0_CORE_RD_RATE_LIM_CFG_1_EN_SHIFT                         31
#define DMA0_CORE_RD_RATE_LIM_CFG_1_EN_MASK                          0x80000000

/* DMA0_CORE_WR_RATE_LIM_CFG_0 */
#define DMA0_CORE_WR_RATE_LIM_CFG_0_RST_TOKEN_SHIFT                  0
#define DMA0_CORE_WR_RATE_LIM_CFG_0_RST_TOKEN_MASK                   0xFF
#define DMA0_CORE_WR_RATE_LIM_CFG_0_SAT_SHIFT                        16
#define DMA0_CORE_WR_RATE_LIM_CFG_0_SAT_MASK                         0xFF0000

/* DMA0_CORE_WR_RATE_LIM_CFG_1 */
#define DMA0_CORE_WR_RATE_LIM_CFG_1_TOUT_SHIFT                       0
#define DMA0_CORE_WR_RATE_LIM_CFG_1_TOUT_MASK                        0xFF
#define DMA0_CORE_WR_RATE_LIM_CFG_1_EN_SHIFT                         31
#define DMA0_CORE_WR_RATE_LIM_CFG_1_EN_MASK                          0x80000000

/* DMA0_CORE_ERR_CFG */
#define DMA0_CORE_ERR_CFG_ERR_MSG_EN_SHIFT                           0
#define DMA0_CORE_ERR_CFG_ERR_MSG_EN_MASK                            0x1
#define DMA0_CORE_ERR_CFG_STOP_ON_ERR_SHIFT                          1
#define DMA0_CORE_ERR_CFG_STOP_ON_ERR_MASK                           0x2

/* DMA0_CORE_ERR_CAUSE */
#define DMA0_CORE_ERR_CAUSE_HBW_RD_ERR_SHIFT                         0
#define DMA0_CORE_ERR_CAUSE_HBW_RD_ERR_MASK                          0x1
#define DMA0_CORE_ERR_CAUSE_HBW_WR_ERR_SHIFT                         1
#define DMA0_CORE_ERR_CAUSE_HBW_WR_ERR_MASK                          0x2
#define DMA0_CORE_ERR_CAUSE_LBW_WR_ERR_SHIFT                         2
#define DMA0_CORE_ERR_CAUSE_LBW_WR_ERR_MASK                          0x4
#define DMA0_CORE_ERR_CAUSE_DESC_OVF_SHIFT                           3
#define DMA0_CORE_ERR_CAUSE_DESC_OVF_MASK                            0x8

/* DMA0_CORE_ERRMSG_ADDR_LO */
#define DMA0_CORE_ERRMSG_ADDR_LO_VAL_SHIFT                           0
#define DMA0_CORE_ERRMSG_ADDR_LO_VAL_MASK                            0xFFFFFFFF

/* DMA0_CORE_ERRMSG_ADDR_HI */
#define DMA0_CORE_ERRMSG_ADDR_HI_VAL_SHIFT                           0
#define DMA0_CORE_ERRMSG_ADDR_HI_VAL_MASK                            0xFFFFFFFF

/* DMA0_CORE_ERRMSG_WDATA */
#define DMA0_CORE_ERRMSG_WDATA_VAL_SHIFT                             0
#define DMA0_CORE_ERRMSG_WDATA_VAL_MASK                              0xFFFFFFFF

/* DMA0_CORE_STS0 */
#define DMA0_CORE_STS0_RD_REQ_CNT_SHIFT                              0
#define DMA0_CORE_STS0_RD_REQ_CNT_MASK                               0x7FFF
#define DMA0_CORE_STS0_WR_REQ_CNT_SHIFT                              16
#define DMA0_CORE_STS0_WR_REQ_CNT_MASK                               0x7FFF0000
#define DMA0_CORE_STS0_BUSY_SHIFT                                    31
#define DMA0_CORE_STS0_BUSY_MASK                                     0x80000000

/* DMA0_CORE_STS1 */
#define DMA0_CORE_STS1_IS_HALT_SHIFT                                 0
#define DMA0_CORE_STS1_IS_HALT_MASK                                  0x1

/* DMA0_CORE_RD_DBGMEM_ADD */
#define DMA0_CORE_RD_DBGMEM_ADD_VAL_SHIFT                            0
#define DMA0_CORE_RD_DBGMEM_ADD_VAL_MASK                             0xFFFFFFFF

/* DMA0_CORE_RD_DBGMEM_DATA_WR */
#define DMA0_CORE_RD_DBGMEM_DATA_WR_VAL_SHIFT                        0
#define DMA0_CORE_RD_DBGMEM_DATA_WR_VAL_MASK                         0xFFFFFFFF

/* DMA0_CORE_RD_DBGMEM_DATA_RD */
#define DMA0_CORE_RD_DBGMEM_DATA_RD_VAL_SHIFT                        0
#define DMA0_CORE_RD_DBGMEM_DATA_RD_VAL_MASK                         0xFFFFFFFF

/* DMA0_CORE_RD_DBGMEM_CTRL */
#define DMA0_CORE_RD_DBGMEM_CTRL_WR_NRD_SHIFT                        0
#define DMA0_CORE_RD_DBGMEM_CTRL_WR_NRD_MASK                         0x1

/* DMA0_CORE_RD_DBGMEM_RC */
#define DMA0_CORE_RD_DBGMEM_RC_VALID_SHIFT                           0
#define DMA0_CORE_RD_DBGMEM_RC_VALID_MASK                            0x1

/* DMA0_CORE_DBG_HBW_AXI_AR_CNT */

/* DMA0_CORE_DBG_HBW_AXI_AW_CNT */

/* DMA0_CORE_DBG_LBW_AXI_AW_CNT */

/* DMA0_CORE_DBG_DESC_CNT */
#define DMA0_CORE_DBG_DESC_CNT_RD_STS_CTX_CNT_SHIFT                  0
#define DMA0_CORE_DBG_DESC_CNT_RD_STS_CTX_CNT_MASK                   0xFFFFFFFF

/* DMA0_CORE_DBG_STS */
#define DMA0_CORE_DBG_STS_RD_CTX_FULL_SHIFT                          0
#define DMA0_CORE_DBG_STS_RD_CTX_FULL_MASK                           0x1
#define DMA0_CORE_DBG_STS_WR_CTX_FULL_SHIFT                          1
#define DMA0_CORE_DBG_STS_WR_CTX_FULL_MASK                           0x2
#define DMA0_CORE_DBG_STS_WR_COMP_FULL_SHIFT                         2
#define DMA0_CORE_DBG_STS_WR_COMP_FULL_MASK                          0x4
#define DMA0_CORE_DBG_STS_RD_CTX_EMPTY_SHIFT                         3
#define DMA0_CORE_DBG_STS_RD_CTX_EMPTY_MASK                          0x8
#define DMA0_CORE_DBG_STS_WR_CTX_EMPTY_SHIFT                         4
#define DMA0_CORE_DBG_STS_WR_CTX_EMPTY_MASK                          0x10
#define DMA0_CORE_DBG_STS_WR_COMP_EMPTY_SHIFT                        5
#define DMA0_CORE_DBG_STS_WR_COMP_EMPTY_MASK                         0x20
#define DMA0_CORE_DBG_STS_TE_EMPTY_SHIFT                             6
#define DMA0_CORE_DBG_STS_TE_EMPTY_MASK                              0x40
#define DMA0_CORE_DBG_STS_TE_BUSY_SHIFT                              7
#define DMA0_CORE_DBG_STS_TE_BUSY_MASK                               0x80
#define DMA0_CORE_DBG_STS_GSKT_EMPTY_SHIFT                           8
#define DMA0_CORE_DBG_STS_GSKT_EMPTY_MASK                            0x100
#define DMA0_CORE_DBG_STS_GSKT_FULL_SHIFT                            9
#define DMA0_CORE_DBG_STS_GSKT_FULL_MASK                             0x200
#define DMA0_CORE_DBG_STS_RDBUF_FULLNESS_SHIFT                       20
#define DMA0_CORE_DBG_STS_RDBUF_FULLNESS_MASK                        0x7FF00000

/* DMA0_CORE_DBG_RD_DESC_ID */

/* DMA0_CORE_DBG_WR_DESC_ID */

#endif /* ASIC_REG_DMA0_CORE_MASKS_H_ */
