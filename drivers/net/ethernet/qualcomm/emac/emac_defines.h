/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2013-2016, The Linux Foundation. All rights reserved.
 *
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __EMAC_DEFINES_H__
#define __EMAC_DEFINES_H__

/* EMAC_DMA_MAS_CTRL */
#define DEV_ID_NUM_BMSK                                      0x7f000000
#define DEV_ID_NUM_SHFT                                              24
#define DEV_REV_NUM_BMSK                                       0xff0000
#define DEV_REV_NUM_SHFT                                             16
#define INT_RD_CLR_EN                                            0x4000
#define IRQ_MODERATOR2_EN                                         0x800
#define IRQ_MODERATOR_EN                                          0x400
#define LPW_CLK_SEL                                                0x80
#define LPW_STATE                                                  0x20
#define LPW_MODE                                                   0x10
#define SOFT_RST                                                    0x1

/* EMAC_IRQ_MOD_TIM_INIT */
#define IRQ_MODERATOR2_INIT_BMSK                             0xffff0000
#define IRQ_MODERATOR2_INIT_SHFT                                     16
#define IRQ_MODERATOR_INIT_BMSK                                  0xffff
#define IRQ_MODERATOR_INIT_SHFT                                       0

/* EMAC_MDIO_CTRL */
#define MDIO_MODE                                            0x40000000
#define MDIO_PR                                              0x20000000
#define MDIO_AP_EN                                           0x10000000
#define MDIO_BUSY                                             0x8000000
#define MDIO_CLK_SEL_BMSK                                     0x7000000
#define MDIO_CLK_SEL_SHFT                                            24
#define MDIO_START                                             0x800000
#define SUP_PREAMBLE                                           0x400000
#define MDIO_RD_NWR                                            0x200000
#define MDIO_REG_ADDR_BMSK                                     0x1f0000
#define MDIO_REG_ADDR_SHFT                                           16
#define MDIO_DATA_BMSK                                           0xffff
#define MDIO_DATA_SHFT                                                0

/* EMAC_PHY_STS */
#define PHY_ADDR_BMSK                                          0x1f0000
#define PHY_ADDR_SHFT                                                16

/* EMAC_MDIO_EX_CTRL */
#define DEVAD_BMSK                                             0x1f0000
#define DEVAD_SHFT                                                   16
#define EX_REG_ADDR_BMSK                                         0xffff
#define EX_REG_ADDR_SHFT                                              0

/* EMAC_MAC_CTRL */
#define SINGLE_PAUSE_MODE                                    0x10000000
#define DEBUG_MODE                                            0x8000000
#define BROAD_EN                                              0x4000000
#define MULTI_ALL                                             0x2000000
#define RX_CHKSUM_EN                                          0x1000000
#define HUGE                                                   0x800000
#define SPEED(x)					(((x) & 0x3) << 20)
#define SPEED_MASK					SPEED(0x3)
#define SIMR                                                    0x80000
#define TPAUSE                                                  0x10000
#define PROM_MODE                                                0x8000
#define VLAN_STRIP                                               0x4000
#define PRLEN_BMSK                                               0x3c00
#define PRLEN_SHFT                                                   10
#define HUGEN                                                     0x200
#define FLCHK                                                     0x100
#define PCRCE                                                      0x80
#define CRCE                                                       0x40
#define FULLD                                                      0x20
#define MAC_LP_EN                                                  0x10
#define RXFC                                                        0x8
#define TXFC                                                        0x4
#define RXEN                                                        0x2
#define TXEN                                                        0x1

/* EMAC_WOL_CTRL0 */
#define LK_CHG_PME                                                 0x20
#define LK_CHG_EN                                                  0x10
#define MG_FRAME_PME                                                0x8
#define MG_FRAME_EN                                                 0x4
#define WK_FRAME_EN                                                 0x1

/* EMAC_DESC_CTRL_3 */
#define RFD_RING_SIZE_BMSK                                        0xfff

/* EMAC_DESC_CTRL_4 */
#define RX_BUFFER_SIZE_BMSK                                      0xffff

/* EMAC_DESC_CTRL_6 */
#define RRD_RING_SIZE_BMSK                                        0xfff

/* EMAC_DESC_CTRL_9 */
#define TPD_RING_SIZE_BMSK                                       0xffff

/* EMAC_TXQ_CTRL_0 */
#define NUM_TXF_BURST_PREF_BMSK                              0xffff0000
#define NUM_TXF_BURST_PREF_SHFT                                      16
#define LS_8023_SP                                                 0x80
#define TXQ_MODE                                                   0x40
#define TXQ_EN                                                     0x20
#define IP_OP_SP                                                   0x10
#define NUM_TPD_BURST_PREF_BMSK                                     0xf
#define NUM_TPD_BURST_PREF_SHFT                                       0

/* EMAC_TXQ_CTRL_1 */
#define JUMBO_TASK_OFFLOAD_THRESHOLD_BMSK                         0x7ff

/* EMAC_TXQ_CTRL_2 */
#define TXF_HWM_BMSK                                          0xfff0000
#define TXF_LWM_BMSK                                              0xfff

/* EMAC_RXQ_CTRL_0 */
#define RXQ_EN                                               0x80000000
#define CUT_THRU_EN                                          0x40000000
#define RSS_HASH_EN                                          0x20000000
#define NUM_RFD_BURST_PREF_BMSK                               0x3f00000
#define NUM_RFD_BURST_PREF_SHFT                                      20
#define IDT_TABLE_SIZE_BMSK                                     0x1ff00
#define IDT_TABLE_SIZE_SHFT                                           8
#define SP_IPV6                                                    0x80

/* EMAC_RXQ_CTRL_1 */
#define JUMBO_1KAH_BMSK                                          0xf000
#define JUMBO_1KAH_SHFT                                              12
#define RFD_PREF_LOW_THRESHOLD_BMSK                               0xfc0
#define RFD_PREF_LOW_THRESHOLD_SHFT                                   6
#define RFD_PREF_UP_THRESHOLD_BMSK                                 0x3f
#define RFD_PREF_UP_THRESHOLD_SHFT                                    0

/* EMAC_RXQ_CTRL_2 */
#define RXF_DOF_THRESHOLD_BMSK                                0xfff0000
#define RXF_DOF_THRESHOLD_SHFT                                       16
#define RXF_UOF_THRESHOLD_BMSK                                    0xfff
#define RXF_UOF_THRESHOLD_SHFT                                        0

/* EMAC_RXQ_CTRL_3 */
#define RXD_TIMER_BMSK                                       0xffff0000
#define RXD_THRESHOLD_BMSK                                        0xfff
#define RXD_THRESHOLD_SHFT                                            0

/* EMAC_DMA_CTRL */
#define DMAW_DLY_CNT_BMSK                                       0xf0000
#define DMAW_DLY_CNT_SHFT                                            16
#define DMAR_DLY_CNT_BMSK                                        0xf800
#define DMAR_DLY_CNT_SHFT                                            11
#define DMAR_REQ_PRI                                              0x400
#define REGWRBLEN_BMSK                                            0x380
#define REGWRBLEN_SHFT                                                7
#define REGRDBLEN_BMSK                                             0x70
#define REGRDBLEN_SHFT                                                4
#define OUT_ORDER_MODE                                              0x4
#define ENH_ORDER_MODE                                              0x2
#define IN_ORDER_MODE                                               0x1

/* EMAC_MAILBOX_13 */
#define RFD3_PROC_IDX_BMSK                                    0xfff0000
#define RFD3_PROC_IDX_SHFT                                           16
#define RFD3_PROD_IDX_BMSK                                        0xfff
#define RFD3_PROD_IDX_SHFT                                            0

/* EMAC_MAILBOX_2 */
#define NTPD_CONS_IDX_BMSK                                   0xffff0000
#define NTPD_CONS_IDX_SHFT                                           16

/* EMAC_MAILBOX_3 */
#define RFD0_CONS_IDX_BMSK                                        0xfff
#define RFD0_CONS_IDX_SHFT                                            0

/* EMAC_INT_STATUS */
#define DIS_INT                                                BIT(31)
#define PTP_INT                                                BIT(30)
#define RFD4_UR_INT                                            BIT(29)
#define TX_PKT_INT3                                            BIT(26)
#define TX_PKT_INT2                                            BIT(25)
#define TX_PKT_INT1                                            BIT(24)
#define RX_PKT_INT3                                            BIT(19)
#define RX_PKT_INT2                                            BIT(18)
#define RX_PKT_INT1                                            BIT(17)
#define RX_PKT_INT0                                            BIT(16)
#define TX_PKT_INT                                             BIT(15)
#define TXQ_TO_INT                                             BIT(14)
#define GPHY_WAKEUP_INT                                        BIT(13)
#define GPHY_LINK_DOWN_INT                                     BIT(12)
#define GPHY_LINK_UP_INT                                       BIT(11)
#define DMAW_TO_INT                                            BIT(10)
#define DMAR_TO_INT                                             BIT(9)
#define TXF_UR_INT                                              BIT(8)
#define RFD3_UR_INT                                             BIT(7)
#define RFD2_UR_INT                                             BIT(6)
#define RFD1_UR_INT                                             BIT(5)
#define RFD0_UR_INT                                             BIT(4)
#define RXF_OF_INT                                              BIT(3)
#define SW_MAN_INT                                              BIT(2)

/* EMAC_INT_RETRIG_INIT */
#define INT_RETRIG_TIME_BMSK                                     0xffff

/* EMAC_MAILBOX_11 */
#define H3TPD_PROD_IDX_BMSK                                  0xffff0000
#define H3TPD_PROD_IDX_SHFT                                          16

/* EMAC_AXI_MAST_CTRL */
#define DATA_BYTE_SWAP                                              0x8
#define MAX_BOUND                                                   0x2
#define MAX_BTYPE                                                   0x1

/* EMAC_MAILBOX_12 */
#define H3TPD_CONS_IDX_BMSK                                  0xffff0000
#define H3TPD_CONS_IDX_SHFT                                          16

/* EMAC_MAILBOX_9 */
#define H2TPD_PROD_IDX_BMSK                                      0xffff
#define H2TPD_PROD_IDX_SHFT                                           0

/* EMAC_MAILBOX_10 */
#define H1TPD_CONS_IDX_BMSK                                  0xffff0000
#define H1TPD_CONS_IDX_SHFT                                          16
#define H2TPD_CONS_IDX_BMSK                                      0xffff
#define H2TPD_CONS_IDX_SHFT                                           0

/* EMAC_ATHR_HEADER_CTRL */
#define HEADER_CNT_EN                                               0x2
#define HEADER_ENABLE                                               0x1

/* EMAC_MAILBOX_0 */
#define RFD0_PROC_IDX_BMSK                                    0xfff0000
#define RFD0_PROC_IDX_SHFT                                           16
#define RFD0_PROD_IDX_BMSK                                        0xfff
#define RFD0_PROD_IDX_SHFT                                            0

/* EMAC_MAILBOX_5 */
#define RFD1_PROC_IDX_BMSK                                    0xfff0000
#define RFD1_PROC_IDX_SHFT                                           16
#define RFD1_PROD_IDX_BMSK                                        0xfff
#define RFD1_PROD_IDX_SHFT                                            0

/* EMAC_MAILBOX_6 */
#define RFD2_PROC_IDX_BMSK                                    0xfff0000
#define RFD2_PROC_IDX_SHFT                                           16
#define RFD2_PROD_IDX_BMSK                                        0xfff
#define RFD2_PROD_IDX_SHFT                                            0

/* EMAC_CORE_HW_VERSION */
#define MAJOR_BMSK                                           0xf0000000
#define MAJOR_SHFT                                                   28
#define MINOR_BMSK                                            0xfff0000
#define MINOR_SHFT                                                   16
#define STEP_BMSK                                                0xffff
#define STEP_SHFT                                                     0

/* EMAC_MISC_CTRL */
#define RX_UNCPL_INT_EN                                             0x1

/* EMAC_MAILBOX_7 */
#define RFD2_CONS_IDX_BMSK                                    0xfff0000
#define RFD2_CONS_IDX_SHFT                                           16
#define RFD1_CONS_IDX_BMSK                                        0xfff
#define RFD1_CONS_IDX_SHFT                                            0

/* EMAC_MAILBOX_8 */
#define RFD3_CONS_IDX_BMSK                                        0xfff
#define RFD3_CONS_IDX_SHFT                                            0

/* EMAC_MAILBOX_15 */
#define NTPD_PROD_IDX_BMSK                                       0xffff
#define NTPD_PROD_IDX_SHFT                                            0

/* EMAC_MAILBOX_16 */
#define H1TPD_PROD_IDX_BMSK                                      0xffff
#define H1TPD_PROD_IDX_SHFT                                           0

/* EMAC_EMAC_WRAPPER_CSR1 */
#define TX_INDX_FIFO_SYNC_RST                                  BIT(23)
#define TX_TS_FIFO_SYNC_RST                                    BIT(22)
#define RX_TS_FIFO2_SYNC_RST                                   BIT(21)
#define RX_TS_FIFO1_SYNC_RST                                   BIT(20)
#define TX_TS_ENABLE                                           BIT(16)
#define DIS_1588_CLKS                                          BIT(11)
#define FREQ_MODE                                               BIT(9)
#define ENABLE_RRD_TIMESTAMP                                    BIT(3)

/* EMAC_EMAC_WRAPPER_CSR2 */
#define HDRIVE_BMSK                                              0x3000
#define HDRIVE_SHFT                                                  12
#define SLB_EN                                                    0x200
#define PLB_EN                                                    0x100
#define WOL_EN                                                     0x80
#define CKEDGE_SEL                                                 0x40
#define TX_ID_EN_L                                                 0x20
#define RX_ID_EN_L                                                 0x10
#define RGMII_PHY_MODE_BMSK                                         0x6
#define RGMII_PHY_MODE_SHFT                                           1
#define PHY_RESET                                                   0x1

/* EMAC_EMAC_WRAPPER_CSR3 */
#define PLL_RESET                                             0x1000000
#define PLL_L_VAL_5_0_BMSK                                     0xfc0000
#define PLL_L_VAL_5_0_SHFT                                           18
#define BYPASSNL                                                0x10000

/* EMAC_EMAC_WRAPPER_CSR5 */
#define RMII_125_CLK_EN                                            0x20

/* EMAC_EMAC_WRAPPER_CSR10 */
#define RD_CLR_1588                                                 0x2
#define DIS_1588                                                    0x1

/* EMAC_EMAC_WRAPPER_STATUS */
#define PLL_LOCK_DET                                                0x1

/* EMAC_EMAC_WRAPPER_TX_TS_INX */
#define EMAC_WRAPPER_TX_TS_EMPTY                             0x80000000
#define EMAC_WRAPPER_TX_TS_INX_BMSK                              0xffff

/* EMAC_P1588_CTRL_REG */
#define ATTACH_EN                                                  0x10
#define BYPASS_O                                                    0x8
#define CLOCK_MODE_BMSK                                             0x6
#define CLOCK_MODE_SHFT                                               1
#define ETH_MODE_SW                                                 0x1

/* EMAC_P1588_TX_LATENCY */
#define TX_LATENCY_BMSK                                          0xffff
#define TX_LATENCY_SHFT                                               0

/* EMAC_P1588_INC_VALUE_2 */
#define INC_VALUE_2_BMSK                                         0xffff

/* EMAC_P1588_INC_VALUE_1 */
#define INC_VALUE_1_BMSK                                         0xffff

/* EMAC_P1588_NANO_OFFSET_2 */
#define NANO_OFFSET_2_BMSK                                       0xffff

/* EMAC_P1588_NANO_OFFSET_1 */
#define NANO_OFFSET_1_BMSK                                       0xffff

/* EMAC_P1588_SEC_OFFSET_2 */
#define SEC_OFFSET_2_BMSK                                        0xffff

/* EMAC_P1588_SEC_OFFSET_1 */
#define SEC_OFFSET_1_BMSK                                        0xffff

/* EMAC_P1588_REAL_TIME_5 */
#define REAL_TIME_5_BMSK                                         0xffff
#define REAL_TIME_5_SHFT                                              0

/* EMAC_P1588_REAL_TIME_4 */
#define REAL_TIME_4_BMSK                                         0xffff
#define REAL_TIME_4_SHFT                                              0

/* EMAC_P1588_REAL_TIME_3 */
#define REAL_TIME_3_BMSK                                         0xffff
#define REAL_TIME_3_SHFT                                              0

/* EMAC_P1588_REAL_TIME_2 */
#define REAL_TIME_2_BMSK                                         0xffff
#define REAL_TIME_2_SHFT                                              0

/* EMAC_P1588_REAL_TIME_1 */
#define REAL_TIME_1_BMSK                                         0xffff
#define REAL_TIME_1_SHFT                                              0

/* EMAC_P1588_EXPANDED_INT_STATUS */
#define PPS_IN                                                     0x20

/* EMAC_P1588_RTC_EXPANDED_CONFIG */
#define RTC_READ_MODE                                              0x20
#define RTC_SNAPSHOT                                               0x10
#define LOAD_RTC                                                    0x1

/* EMAC_P1588_RTC_PRELOADED_4 */
#define RTC_PRELOADED_4_BMSK                                     0xffff

/* EMAC_P1588_RTC_PRELOADED_3 */
#define RTC_PRELOADED_3_BMSK                                     0xffff

/* EMAC_P1588_RTC_PRELOADED_2 */
#define RTC_PRELOADED_2_BMSK                                     0xffff

/* EMAC_P1588_RTC_PRELOADED_1 */
#define RTC_PRELOADED_1_BMSK                                     0xffff

/* EMAC_P1588_GRAND_MASTER_CONFIG_0 */
#define GRANDMASTER_MODE                                           0x40
#define GM_PPS_SYNC                                                0x20

#endif /* __EMAC_DEFINES_H__ */
