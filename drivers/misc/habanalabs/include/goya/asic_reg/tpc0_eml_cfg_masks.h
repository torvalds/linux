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

#ifndef ASIC_REG_TPC0_EML_CFG_MASKS_H_
#define ASIC_REG_TPC0_EML_CFG_MASKS_H_

/*
 *****************************************
 *   TPC0_EML_CFG (Prototype: TPC_EML_CFG)
 *****************************************
 */

/* TPC0_EML_CFG_DBG_CNT */
#define TPC0_EML_CFG_DBG_CNT_DBG_ENTER_SHIFT                         0
#define TPC0_EML_CFG_DBG_CNT_DBG_ENTER_MASK                          0x1
#define TPC0_EML_CFG_DBG_CNT_DBG_EN_SHIFT                            1
#define TPC0_EML_CFG_DBG_CNT_DBG_EN_MASK                             0x2
#define TPC0_EML_CFG_DBG_CNT_CORE_RST_SHIFT                          2
#define TPC0_EML_CFG_DBG_CNT_CORE_RST_MASK                           0x4
#define TPC0_EML_CFG_DBG_CNT_DCACHE_INV_SHIFT                        4
#define TPC0_EML_CFG_DBG_CNT_DCACHE_INV_MASK                         0x10
#define TPC0_EML_CFG_DBG_CNT_ICACHE_INV_SHIFT                        5
#define TPC0_EML_CFG_DBG_CNT_ICACHE_INV_MASK                         0x20
#define TPC0_EML_CFG_DBG_CNT_DBG_EXIT_SHIFT                          6
#define TPC0_EML_CFG_DBG_CNT_DBG_EXIT_MASK                           0x40
#define TPC0_EML_CFG_DBG_CNT_SNG_STEP_SHIFT                          7
#define TPC0_EML_CFG_DBG_CNT_SNG_STEP_MASK                           0x80
#define TPC0_EML_CFG_DBG_CNT_BP_DBGSW_EN_SHIFT                       16
#define TPC0_EML_CFG_DBG_CNT_BP_DBGSW_EN_MASK                        0x10000

/* TPC0_EML_CFG_DBG_STS */
#define TPC0_EML_CFG_DBG_STS_DBG_MODE_SHIFT                          0
#define TPC0_EML_CFG_DBG_STS_DBG_MODE_MASK                           0x1
#define TPC0_EML_CFG_DBG_STS_CORE_READY_SHIFT                        1
#define TPC0_EML_CFG_DBG_STS_CORE_READY_MASK                         0x2
#define TPC0_EML_CFG_DBG_STS_DURING_KERNEL_SHIFT                     2
#define TPC0_EML_CFG_DBG_STS_DURING_KERNEL_MASK                      0x4
#define TPC0_EML_CFG_DBG_STS_ICACHE_IDLE_SHIFT                       3
#define TPC0_EML_CFG_DBG_STS_ICACHE_IDLE_MASK                        0x8
#define TPC0_EML_CFG_DBG_STS_DCACHE_IDLE_SHIFT                       4
#define TPC0_EML_CFG_DBG_STS_DCACHE_IDLE_MASK                        0x10
#define TPC0_EML_CFG_DBG_STS_QM_IDLE_SHIFT                           5
#define TPC0_EML_CFG_DBG_STS_QM_IDLE_MASK                            0x20
#define TPC0_EML_CFG_DBG_STS_WQ_IDLE_SHIFT                           6
#define TPC0_EML_CFG_DBG_STS_WQ_IDLE_MASK                            0x40
#define TPC0_EML_CFG_DBG_STS_MSS_IDLE_SHIFT                          7
#define TPC0_EML_CFG_DBG_STS_MSS_IDLE_MASK                           0x80
#define TPC0_EML_CFG_DBG_STS_DBG_CAUSE_SHIFT                         8
#define TPC0_EML_CFG_DBG_STS_DBG_CAUSE_MASK                          0xFFFFFF00

/* TPC0_EML_CFG_DBG_PADD */
#define TPC0_EML_CFG_DBG_PADD_ADDRESS_SHIFT                          0
#define TPC0_EML_CFG_DBG_PADD_ADDRESS_MASK                           0xFFFFFFFF

/* TPC0_EML_CFG_DBG_PADD_COUNT */
#define TPC0_EML_CFG_DBG_PADD_COUNT_COUNT_SHIFT                      0
#define TPC0_EML_CFG_DBG_PADD_COUNT_COUNT_MASK                       0xFF

/* TPC0_EML_CFG_DBG_PADD_COUNT_MATCH */
#define TPC0_EML_CFG_DBG_PADD_COUNT_MATCH_COUNT_SHIFT                0
#define TPC0_EML_CFG_DBG_PADD_COUNT_MATCH_COUNT_MASK                 0xFF

/* TPC0_EML_CFG_DBG_PADD_EN */
#define TPC0_EML_CFG_DBG_PADD_EN_ENABLE0_SHIFT                       0
#define TPC0_EML_CFG_DBG_PADD_EN_ENABLE0_MASK                        0x1
#define TPC0_EML_CFG_DBG_PADD_EN_ENABLE1_SHIFT                       1
#define TPC0_EML_CFG_DBG_PADD_EN_ENABLE1_MASK                        0x2
#define TPC0_EML_CFG_DBG_PADD_EN_ENABLE2_SHIFT                       2
#define TPC0_EML_CFG_DBG_PADD_EN_ENABLE2_MASK                        0x4
#define TPC0_EML_CFG_DBG_PADD_EN_ENABLE3_SHIFT                       3
#define TPC0_EML_CFG_DBG_PADD_EN_ENABLE3_MASK                        0x8
#define TPC0_EML_CFG_DBG_PADD_EN_ENABLE4_SHIFT                       4
#define TPC0_EML_CFG_DBG_PADD_EN_ENABLE4_MASK                        0x10
#define TPC0_EML_CFG_DBG_PADD_EN_ENABLE5_SHIFT                       5
#define TPC0_EML_CFG_DBG_PADD_EN_ENABLE5_MASK                        0x20
#define TPC0_EML_CFG_DBG_PADD_EN_ENABLE6_SHIFT                       6
#define TPC0_EML_CFG_DBG_PADD_EN_ENABLE6_MASK                        0x40
#define TPC0_EML_CFG_DBG_PADD_EN_ENABLE7_SHIFT                       7
#define TPC0_EML_CFG_DBG_PADD_EN_ENABLE7_MASK                        0x80

/* TPC0_EML_CFG_DBG_VPADD_HIGH */
#define TPC0_EML_CFG_DBG_VPADD_HIGH_ADDRESS_SHIFT                    0
#define TPC0_EML_CFG_DBG_VPADD_HIGH_ADDRESS_MASK                     0x1FF

/* TPC0_EML_CFG_DBG_VPADD_LOW */
#define TPC0_EML_CFG_DBG_VPADD_LOW_ADDRESS_SHIFT                     0
#define TPC0_EML_CFG_DBG_VPADD_LOW_ADDRESS_MASK                      0x1FF

/* TPC0_EML_CFG_DBG_VPADD_COUNT */
#define TPC0_EML_CFG_DBG_VPADD_COUNT_COUNT_SHIFT                     0
#define TPC0_EML_CFG_DBG_VPADD_COUNT_COUNT_MASK                      0xFF

/* TPC0_EML_CFG_DBG_VPADD_COUNT_MATCH */
#define TPC0_EML_CFG_DBG_VPADD_COUNT_MATCH_COUNT_SHIFT               0
#define TPC0_EML_CFG_DBG_VPADD_COUNT_MATCH_COUNT_MASK                0xFF

/* TPC0_EML_CFG_DBG_VPADD_EN */
#define TPC0_EML_CFG_DBG_VPADD_EN_ENABLE0_SHIFT                      0
#define TPC0_EML_CFG_DBG_VPADD_EN_ENABLE0_MASK                       0x1
#define TPC0_EML_CFG_DBG_VPADD_EN_ENABLE1_SHIFT                      1
#define TPC0_EML_CFG_DBG_VPADD_EN_ENABLE1_MASK                       0x2
#define TPC0_EML_CFG_DBG_VPADD_EN_RW_N0_SHIFT                        2
#define TPC0_EML_CFG_DBG_VPADD_EN_RW_N0_MASK                         0x4
#define TPC0_EML_CFG_DBG_VPADD_EN_RW_N1_SHIFT                        3
#define TPC0_EML_CFG_DBG_VPADD_EN_RW_N1_MASK                         0x8

/* TPC0_EML_CFG_DBG_SPADD_HIGH */
#define TPC0_EML_CFG_DBG_SPADD_HIGH_ADDRESS_SHIFT                    0
#define TPC0_EML_CFG_DBG_SPADD_HIGH_ADDRESS_MASK                     0xFF

/* TPC0_EML_CFG_DBG_SPADD_LOW */
#define TPC0_EML_CFG_DBG_SPADD_LOW_ADDRESS_SHIFT                     0
#define TPC0_EML_CFG_DBG_SPADD_LOW_ADDRESS_MASK                      0xFF

/* TPC0_EML_CFG_DBG_SPADD_COUNT */
#define TPC0_EML_CFG_DBG_SPADD_COUNT_COUNT_SHIFT                     0
#define TPC0_EML_CFG_DBG_SPADD_COUNT_COUNT_MASK                      0xFF

/* TPC0_EML_CFG_DBG_SPADD_COUNT_MATCH */
#define TPC0_EML_CFG_DBG_SPADD_COUNT_MATCH_COUNT_SHIFT               0
#define TPC0_EML_CFG_DBG_SPADD_COUNT_MATCH_COUNT_MASK                0xFF

/* TPC0_EML_CFG_DBG_SPADD_EN */
#define TPC0_EML_CFG_DBG_SPADD_EN_ENABLE0_SHIFT                      0
#define TPC0_EML_CFG_DBG_SPADD_EN_ENABLE0_MASK                       0x1
#define TPC0_EML_CFG_DBG_SPADD_EN_ENABLE1_SHIFT                      1
#define TPC0_EML_CFG_DBG_SPADD_EN_ENABLE1_MASK                       0x2
#define TPC0_EML_CFG_DBG_SPADD_EN_RW_N0_SHIFT                        2
#define TPC0_EML_CFG_DBG_SPADD_EN_RW_N0_MASK                         0x4
#define TPC0_EML_CFG_DBG_SPADD_EN_RW_N1_SHIFT                        3
#define TPC0_EML_CFG_DBG_SPADD_EN_RW_N1_MASK                         0x8

/* TPC0_EML_CFG_DBG_AGUADD_MSB_HIGH */
#define TPC0_EML_CFG_DBG_AGUADD_MSB_HIGH_ADDRESS_SHIFT               0
#define TPC0_EML_CFG_DBG_AGUADD_MSB_HIGH_ADDRESS_MASK                0xFFFFFFFF

/* TPC0_EML_CFG_DBG_AGUADD_MSB_LOW */
#define TPC0_EML_CFG_DBG_AGUADD_MSB_LOW_ADDRESS_SHIFT                0
#define TPC0_EML_CFG_DBG_AGUADD_MSB_LOW_ADDRESS_MASK                 0xFFFFFFFF

/* TPC0_EML_CFG_DBG_AGUADD_LSB_HIGH */
#define TPC0_EML_CFG_DBG_AGUADD_LSB_HIGH_ADDRESS_SHIFT               0
#define TPC0_EML_CFG_DBG_AGUADD_LSB_HIGH_ADDRESS_MASK                0xFFFFFFFF

/* TPC0_EML_CFG_DBG_AGUADD_LSB_LOW */
#define TPC0_EML_CFG_DBG_AGUADD_LSB_LOW_ADDRESS_SHIFT                0
#define TPC0_EML_CFG_DBG_AGUADD_LSB_LOW_ADDRESS_MASK                 0xFFFFFFFF

/* TPC0_EML_CFG_DBG_AGUADD_COUNT */
#define TPC0_EML_CFG_DBG_AGUADD_COUNT_COUNT_SHIFT                    0
#define TPC0_EML_CFG_DBG_AGUADD_COUNT_COUNT_MASK                     0xFF

/* TPC0_EML_CFG_DBG_AGUADD_COUNT_MATCH */
#define TPC0_EML_CFG_DBG_AGUADD_COUNT_MATCH_COUNT_SHIFT              0
#define TPC0_EML_CFG_DBG_AGUADD_COUNT_MATCH_COUNT_MASK               0xFF

/* TPC0_EML_CFG_DBG_AGUADD_EN */
#define TPC0_EML_CFG_DBG_AGUADD_EN_ENABLE0_SHIFT                     0
#define TPC0_EML_CFG_DBG_AGUADD_EN_ENABLE0_MASK                      0x1
#define TPC0_EML_CFG_DBG_AGUADD_EN_ENABLE1_SHIFT                     1
#define TPC0_EML_CFG_DBG_AGUADD_EN_ENABLE1_MASK                      0x2
#define TPC0_EML_CFG_DBG_AGUADD_EN_RW_N0_SHIFT                       2
#define TPC0_EML_CFG_DBG_AGUADD_EN_RW_N0_MASK                        0x4
#define TPC0_EML_CFG_DBG_AGUADD_EN_RW_N1_SHIFT                       3
#define TPC0_EML_CFG_DBG_AGUADD_EN_RW_N1_MASK                        0x8

/* TPC0_EML_CFG_DBG_AXIHBWADD_MSB_HIGH */
#define TPC0_EML_CFG_DBG_AXIHBWADD_MSB_HIGH_ADDRESS_SHIFT            0
#define TPC0_EML_CFG_DBG_AXIHBWADD_MSB_HIGH_ADDRESS_MASK             0xFFFFFFFF

/* TPC0_EML_CFG_DBG_AXIHBWADD_MSB_LOW */
#define TPC0_EML_CFG_DBG_AXIHBWADD_MSB_LOW_ADDRESS_SHIFT             0
#define TPC0_EML_CFG_DBG_AXIHBWADD_MSB_LOW_ADDRESS_MASK              0xFFFFFFFF

/* TPC0_EML_CFG_DBG_AXIHBWADD_LSB_HIGH */
#define TPC0_EML_CFG_DBG_AXIHBWADD_LSB_HIGH_ADDRESS_SHIFT            0
#define TPC0_EML_CFG_DBG_AXIHBWADD_LSB_HIGH_ADDRESS_MASK             0xFFFFFFFF

/* TPC0_EML_CFG_DBG_AXIHBWADD_LSB_LOW */
#define TPC0_EML_CFG_DBG_AXIHBWADD_LSB_LOW_ADDRESS_SHIFT             0
#define TPC0_EML_CFG_DBG_AXIHBWADD_LSB_LOW_ADDRESS_MASK              0xFFFFFFFF

/* TPC0_EML_CFG_DBG_AXIHBWADD_COUNT */
#define TPC0_EML_CFG_DBG_AXIHBWADD_COUNT_COUNT_SHIFT                 0
#define TPC0_EML_CFG_DBG_AXIHBWADD_COUNT_COUNT_MASK                  0xFF

/* TPC0_EML_CFG_DBG_AXIHBWADD_COUNT_MATCH */
#define TPC0_EML_CFG_DBG_AXIHBWADD_COUNT_MATCH_MATCH_SHIFT           0
#define TPC0_EML_CFG_DBG_AXIHBWADD_COUNT_MATCH_MATCH_MASK            0xFF

/* TPC0_EML_CFG_DBG_AXIHBWADD_EN */
#define TPC0_EML_CFG_DBG_AXIHBWADD_EN_ENABLE0_SHIFT                  0
#define TPC0_EML_CFG_DBG_AXIHBWADD_EN_ENABLE0_MASK                   0x1
#define TPC0_EML_CFG_DBG_AXIHBWADD_EN_ENABLE1_SHIFT                  1
#define TPC0_EML_CFG_DBG_AXIHBWADD_EN_ENABLE1_MASK                   0x2
#define TPC0_EML_CFG_DBG_AXIHBWADD_EN_RW_N0_SHIFT                    2
#define TPC0_EML_CFG_DBG_AXIHBWADD_EN_RW_N0_MASK                     0x4
#define TPC0_EML_CFG_DBG_AXIHBWADD_EN_RW_N1_SHIFT                    3
#define TPC0_EML_CFG_DBG_AXIHBWADD_EN_RW_N1_MASK                     0x8

/* TPC0_EML_CFG_DBG_AXILBWADD_MSB_HIGH */
#define TPC0_EML_CFG_DBG_AXILBWADD_MSB_HIGH_ADDRESS_SHIFT            0
#define TPC0_EML_CFG_DBG_AXILBWADD_MSB_HIGH_ADDRESS_MASK             0xFFFFFFFF

/* TPC0_EML_CFG_DBG_AXILBWADD_MSB_LOW */
#define TPC0_EML_CFG_DBG_AXILBWADD_MSB_LOW_ADDRESS_SHIFT             0
#define TPC0_EML_CFG_DBG_AXILBWADD_MSB_LOW_ADDRESS_MASK              0xFFFFFFFF

/* TPC0_EML_CFG_DBG_AXILBWADD_LSB_HIGH */
#define TPC0_EML_CFG_DBG_AXILBWADD_LSB_HIGH_ADDRESS_SHIFT            0
#define TPC0_EML_CFG_DBG_AXILBWADD_LSB_HIGH_ADDRESS_MASK             0xFFFFFFFF

/* TPC0_EML_CFG_DBG_AXILBWADD_LSB_LOW */
#define TPC0_EML_CFG_DBG_AXILBWADD_LSB_LOW_ADDRESS_SHIFT             0
#define TPC0_EML_CFG_DBG_AXILBWADD_LSB_LOW_ADDRESS_MASK              0xFFFFFFFF

/* TPC0_EML_CFG_DBG_AXILBWADD_COUNT */
#define TPC0_EML_CFG_DBG_AXILBWADD_COUNT_COUNT_SHIFT                 0
#define TPC0_EML_CFG_DBG_AXILBWADD_COUNT_COUNT_MASK                  0xFF

/* TPC0_EML_CFG_DBG_AXILBWADD_COUNT_MATCH */
#define TPC0_EML_CFG_DBG_AXILBWADD_COUNT_MATCH_MATCH_SHIFT           0
#define TPC0_EML_CFG_DBG_AXILBWADD_COUNT_MATCH_MATCH_MASK            0xFF

/* TPC0_EML_CFG_DBG_AXILBWADD_EN */
#define TPC0_EML_CFG_DBG_AXILBWADD_EN_ENABLE0_SHIFT                  0
#define TPC0_EML_CFG_DBG_AXILBWADD_EN_ENABLE0_MASK                   0x1
#define TPC0_EML_CFG_DBG_AXILBWADD_EN_ENABLE1_SHIFT                  1
#define TPC0_EML_CFG_DBG_AXILBWADD_EN_ENABLE1_MASK                   0x2
#define TPC0_EML_CFG_DBG_AXILBWADD_EN_RW_N0_SHIFT                    2
#define TPC0_EML_CFG_DBG_AXILBWADD_EN_RW_N0_MASK                     0x4
#define TPC0_EML_CFG_DBG_AXILBWADD_EN_RW_N1_SHIFT                    3
#define TPC0_EML_CFG_DBG_AXILBWADD_EN_RW_N1_MASK                     0x8

/* TPC0_EML_CFG_DBG_SPDATA */
#define TPC0_EML_CFG_DBG_SPDATA_DATA_SHIFT                           0
#define TPC0_EML_CFG_DBG_SPDATA_DATA_MASK                            0xFFFFFFFF

/* TPC0_EML_CFG_DBG_SPDATA_COUNT */
#define TPC0_EML_CFG_DBG_SPDATA_COUNT_COUNT_SHIFT                    0
#define TPC0_EML_CFG_DBG_SPDATA_COUNT_COUNT_MASK                     0xFF

/* TPC0_EML_CFG_DBG_SPDATA_COUNT_MATCH */
#define TPC0_EML_CFG_DBG_SPDATA_COUNT_MATCH_MATCH_SHIFT              0
#define TPC0_EML_CFG_DBG_SPDATA_COUNT_MATCH_MATCH_MASK               0xFF

/* TPC0_EML_CFG_DBG_SPDATA_EN */
#define TPC0_EML_CFG_DBG_SPDATA_EN_ENABLE0_SHIFT                     0
#define TPC0_EML_CFG_DBG_SPDATA_EN_ENABLE0_MASK                      0x1
#define TPC0_EML_CFG_DBG_SPDATA_EN_ENABLE1_SHIFT                     1
#define TPC0_EML_CFG_DBG_SPDATA_EN_ENABLE1_MASK                      0x2
#define TPC0_EML_CFG_DBG_SPDATA_EN_RW_N0_SHIFT                       2
#define TPC0_EML_CFG_DBG_SPDATA_EN_RW_N0_MASK                        0x4
#define TPC0_EML_CFG_DBG_SPDATA_EN_RW_N1_SHIFT                       3
#define TPC0_EML_CFG_DBG_SPDATA_EN_RW_N1_MASK                        0x8

/* TPC0_EML_CFG_DBG_AXIHBWDATA */
#define TPC0_EML_CFG_DBG_AXIHBWDATA_DATA_SHIFT                       0
#define TPC0_EML_CFG_DBG_AXIHBWDATA_DATA_MASK                        0xFFFFFFFF

/* TPC0_EML_CFG_DBG_AXIHBWDATA_COUNT */
#define TPC0_EML_CFG_DBG_AXIHBWDATA_COUNT_COUNT_SHIFT                0
#define TPC0_EML_CFG_DBG_AXIHBWDATA_COUNT_COUNT_MASK                 0xFF

/* TPC0_EML_CFG_DBG_AXIHBWDAT_COUNT_MATCH */
#define TPC0_EML_CFG_DBG_AXIHBWDAT_COUNT_MATCH_COUNT_SHIFT           0
#define TPC0_EML_CFG_DBG_AXIHBWDAT_COUNT_MATCH_COUNT_MASK            0xFF

/* TPC0_EML_CFG_DBG_AXIHBWDATA_EN */
#define TPC0_EML_CFG_DBG_AXIHBWDATA_EN_ENABLE_SHIFT                  0
#define TPC0_EML_CFG_DBG_AXIHBWDATA_EN_ENABLE_MASK                   0x1
#define TPC0_EML_CFG_DBG_AXIHBWDATA_EN_RW_N_SHIFT                    1
#define TPC0_EML_CFG_DBG_AXIHBWDATA_EN_RW_N_MASK                     0x2

/* TPC0_EML_CFG_DBG_AXILBWDATA */
#define TPC0_EML_CFG_DBG_AXILBWDATA_DATA_SHIFT                       0
#define TPC0_EML_CFG_DBG_AXILBWDATA_DATA_MASK                        0xFFFFFFFF

/* TPC0_EML_CFG_DBG_AXILBWDATA_COUNT */
#define TPC0_EML_CFG_DBG_AXILBWDATA_COUNT_COUNT_SHIFT                0
#define TPC0_EML_CFG_DBG_AXILBWDATA_COUNT_COUNT_MASK                 0xFF

/* TPC0_EML_CFG_DBG_AXILBWDAT_COUNT_MATCH */
#define TPC0_EML_CFG_DBG_AXILBWDAT_COUNT_MATCH_MATCH_SHIFT           0
#define TPC0_EML_CFG_DBG_AXILBWDAT_COUNT_MATCH_MATCH_MASK            0xFF

/* TPC0_EML_CFG_DBG_AXILBWDATA_EN */
#define TPC0_EML_CFG_DBG_AXILBWDATA_EN_ENABLE_SHIFT                  0
#define TPC0_EML_CFG_DBG_AXILBWDATA_EN_ENABLE_MASK                   0x1
#define TPC0_EML_CFG_DBG_AXILBWDATA_EN_RW_N_SHIFT                    1
#define TPC0_EML_CFG_DBG_AXILBWDATA_EN_RW_N_MASK                     0x2

/* TPC0_EML_CFG_DBG_D0_PC */
#define TPC0_EML_CFG_DBG_D0_PC_PC_SHIFT                              0
#define TPC0_EML_CFG_DBG_D0_PC_PC_MASK                               0xFFFFFFFF

/* TPC0_EML_CFG_RTTCONFIG */
#define TPC0_EML_CFG_RTTCONFIG_TR_EN_SHIFT                           0
#define TPC0_EML_CFG_RTTCONFIG_TR_EN_MASK                            0x1
#define TPC0_EML_CFG_RTTCONFIG_PRIO_SHIFT                            1
#define TPC0_EML_CFG_RTTCONFIG_PRIO_MASK                             0x2

/* TPC0_EML_CFG_RTTPREDICATE */
#define TPC0_EML_CFG_RTTPREDICATE_TR_EN_SHIFT                        0
#define TPC0_EML_CFG_RTTPREDICATE_TR_EN_MASK                         0x1
#define TPC0_EML_CFG_RTTPREDICATE_GEN_SHIFT                          1
#define TPC0_EML_CFG_RTTPREDICATE_GEN_MASK                           0x2
#define TPC0_EML_CFG_RTTPREDICATE_USE_INTERVAL_SHIFT                 2
#define TPC0_EML_CFG_RTTPREDICATE_USE_INTERVAL_MASK                  0x4
#define TPC0_EML_CFG_RTTPREDICATE_SPRF_MASK_SHIFT                    16
#define TPC0_EML_CFG_RTTPREDICATE_SPRF_MASK_MASK                     0xFFFF0000

/* TPC0_EML_CFG_RTTPREDICATE_INTV */
#define TPC0_EML_CFG_RTTPREDICATE_INTV_INTERVAL_SHIFT                0
#define TPC0_EML_CFG_RTTPREDICATE_INTV_INTERVAL_MASK                 0xFFFFFFFF

/* TPC0_EML_CFG_RTTTS */
#define TPC0_EML_CFG_RTTTS_TR_EN_SHIFT                               0
#define TPC0_EML_CFG_RTTTS_TR_EN_MASK                                0x1
#define TPC0_EML_CFG_RTTTS_GEN_SHIFT                                 1
#define TPC0_EML_CFG_RTTTS_GEN_MASK                                  0x2
#define TPC0_EML_CFG_RTTTS_COMPRESS_EN_SHIFT                         2
#define TPC0_EML_CFG_RTTTS_COMPRESS_EN_MASK                          0x4

/* TPC0_EML_CFG_RTTTS_INTV */
#define TPC0_EML_CFG_RTTTS_INTV_INTERVAL_SHIFT                       0
#define TPC0_EML_CFG_RTTTS_INTV_INTERVAL_MASK                        0xFFFFFFFF

/* TPC0_EML_CFG_DBG_INST_INSERT */
#define TPC0_EML_CFG_DBG_INST_INSERT_INST_SHIFT                      0
#define TPC0_EML_CFG_DBG_INST_INSERT_INST_MASK                       0xFFFFFFFF

/* TPC0_EML_CFG_DBG_INST_INSERT_CTL */
#define TPC0_EML_CFG_DBG_INST_INSERT_CTL_INSERT_SHIFT                0
#define TPC0_EML_CFG_DBG_INST_INSERT_CTL_INSERT_MASK                 0x1

#endif /* ASIC_REG_TPC0_EML_CFG_MASKS_H_ */
