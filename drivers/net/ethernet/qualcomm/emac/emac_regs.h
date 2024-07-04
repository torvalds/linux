/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2013-2015, The Linux Foundation. All rights reserved.
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

#ifndef __EMAC_REGS_H__
#define __EMAC_REGS_H__

#define SGMII_PHY_VERSION_1 1
#define SGMII_PHY_VERSION_2 2

/* EMAC register offsets */
#define EMAC_DMA_MAS_CTRL                        0x001400
#define EMAC_TIMER_INIT_VALUE                    0x001404
#define EMAC_IRQ_MOD_TIM_INIT                    0x001408
#define EMAC_BLK_IDLE_STS                        0x00140c
#define EMAC_MDIO_CTRL                           0x001414
#define EMAC_PHY_STS                             0x001418
#define EMAC_PHY_LINK_DELAY                      0x00141c
#define EMAC_SYS_ALIV_CTRL                       0x001434
#define EMAC_MDIO_EX_CTRL                        0x001440
#define EMAC_MAC_CTRL                            0x001480
#define EMAC_MAC_IPGIFG_CTRL                     0x001484
#define EMAC_MAC_STA_ADDR0                       0x001488
#define EMAC_MAC_STA_ADDR1                       0x00148c
#define EMAC_HASH_TAB_REG0                       0x001490
#define EMAC_HASH_TAB_REG1                       0x001494
#define EMAC_MAC_HALF_DPLX_CTRL                  0x001498
#define EMAC_MAX_FRAM_LEN_CTRL                   0x00149c
#define EMAC_WOL_CTRL0                           0x0014a0
#define EMAC_WOL_CTRL1                           0x0014a4
#define EMAC_WOL_CTRL2                           0x0014a8
#define EMAC_RSS_KEY0                            0x0014b0
#define EMAC_RSS_KEY1                            0x0014b4
#define EMAC_RSS_KEY2                            0x0014b8
#define EMAC_RSS_KEY3                            0x0014bc
#define EMAC_RSS_KEY4                            0x0014c0
#define EMAC_RSS_KEY5                            0x0014c4
#define EMAC_RSS_KEY6                            0x0014c8
#define EMAC_RSS_KEY7                            0x0014cc
#define EMAC_RSS_KEY8                            0x0014d0
#define EMAC_RSS_KEY9                            0x0014d4
#define EMAC_H1TPD_BASE_ADDR_LO                  0x0014e0
#define EMAC_H2TPD_BASE_ADDR_LO                  0x0014e4
#define EMAC_H3TPD_BASE_ADDR_LO                  0x0014e8
#define EMAC_INTER_SRAM_PART9                    0x001534
#define EMAC_DESC_CTRL_0                         0x001540
#define EMAC_DESC_CTRL_1                         0x001544
#define EMAC_DESC_CTRL_2                         0x001550
#define EMAC_DESC_CTRL_10                        0x001554
#define EMAC_DESC_CTRL_12                        0x001558
#define EMAC_DESC_CTRL_13                        0x00155c
#define EMAC_DESC_CTRL_3                         0x001560
#define EMAC_DESC_CTRL_4                         0x001564
#define EMAC_DESC_CTRL_5                         0x001568
#define EMAC_DESC_CTRL_14                        0x00156c
#define EMAC_DESC_CTRL_15                        0x001570
#define EMAC_DESC_CTRL_16                        0x001574
#define EMAC_DESC_CTRL_6                         0x001578
#define EMAC_DESC_CTRL_8                         0x001580
#define EMAC_DESC_CTRL_9                         0x001584
#define EMAC_DESC_CTRL_11                        0x001588
#define EMAC_TXQ_CTRL_0                          0x001590
#define EMAC_TXQ_CTRL_1                          0x001594
#define EMAC_TXQ_CTRL_2                          0x001598
#define EMAC_RXQ_CTRL_0                          0x0015a0
#define EMAC_RXQ_CTRL_1                          0x0015a4
#define EMAC_RXQ_CTRL_2                          0x0015a8
#define EMAC_RXQ_CTRL_3                          0x0015ac
#define EMAC_BASE_CPU_NUMBER                     0x0015b8
#define EMAC_DMA_CTRL                            0x0015c0
#define EMAC_MAILBOX_0                           0x0015e0
#define EMAC_MAILBOX_5                           0x0015e4
#define EMAC_MAILBOX_6                           0x0015e8
#define EMAC_MAILBOX_13                          0x0015ec
#define EMAC_MAILBOX_2                           0x0015f4
#define EMAC_MAILBOX_3                           0x0015f8
#define EMAC_INT_STATUS                          0x001600
#define EMAC_INT_MASK                            0x001604
#define EMAC_INT_RETRIG_INIT                     0x001608
#define EMAC_MAILBOX_11                          0x00160c
#define EMAC_AXI_MAST_CTRL                       0x001610
#define EMAC_MAILBOX_12                          0x001614
#define EMAC_MAILBOX_9                           0x001618
#define EMAC_MAILBOX_10                          0x00161c
#define EMAC_ATHR_HEADER_CTRL                    0x001620
#define EMAC_RXMAC_STATC_REG0                    0x001700
#define EMAC_RXMAC_STATC_REG22                   0x001758
#define EMAC_TXMAC_STATC_REG0                    0x001760
#define EMAC_TXMAC_STATC_REG24                   0x0017c0
#define EMAC_CLK_GATE_CTRL                       0x001814
#define EMAC_CORE_HW_VERSION                     0x001974
#define EMAC_MISC_CTRL                           0x001990
#define EMAC_MAILBOX_7                           0x0019e0
#define EMAC_MAILBOX_8                           0x0019e4
#define EMAC_IDT_TABLE0                          0x001b00
#define EMAC_RXMAC_STATC_REG23                   0x001bc8
#define EMAC_RXMAC_STATC_REG24                   0x001bcc
#define EMAC_TXMAC_STATC_REG25                   0x001bd0
#define EMAC_MAILBOX_15                          0x001bd4
#define EMAC_MAILBOX_16                          0x001bd8
#define EMAC_INT1_MASK                           0x001bf0
#define EMAC_INT1_STATUS                         0x001bf4
#define EMAC_INT2_MASK                           0x001bf8
#define EMAC_INT2_STATUS                         0x001bfc
#define EMAC_INT3_MASK                           0x001c00
#define EMAC_INT3_STATUS                         0x001c04

/* EMAC_CSR register offsets */
#define EMAC_EMAC_WRAPPER_CSR1                   0x000000
#define EMAC_EMAC_WRAPPER_CSR2                   0x000004
#define EMAC_EMAC_WRAPPER_CSR3                   0x000008
#define EMAC_EMAC_WRAPPER_CSR5                   0x000010
#define EMAC_EMAC_WRAPPER_CSR10                  0x000024
#define EMAC_EMAC_WRAPPER_CSR18                  0x000044
#define EMAC_EMAC_WRAPPER_STATUS                 0x000100
#define EMAC_EMAC_WRAPPER_TX_TS_LO               0x000104
#define EMAC_EMAC_WRAPPER_TX_TS_HI               0x000108
#define EMAC_EMAC_WRAPPER_TX_TS_INX              0x00010c

/* EMAC_1588 register offsets */
#define EMAC_P1588_CTRL_REG                      0x000048
#define EMAC_P1588_TX_LATENCY                    0x0000d4
#define EMAC_P1588_INC_VALUE_2                   0x0000d8
#define EMAC_P1588_INC_VALUE_1                   0x0000dc
#define EMAC_P1588_NANO_OFFSET_2                 0x0000e0
#define EMAC_P1588_NANO_OFFSET_1                 0x0000e4
#define EMAC_P1588_SEC_OFFSET_3                  0x0000e8
#define EMAC_P1588_SEC_OFFSET_2                  0x0000ec
#define EMAC_P1588_SEC_OFFSET_1                  0x0000f0
#define EMAC_P1588_REAL_TIME_5                   0x0000f4
#define EMAC_P1588_REAL_TIME_4                   0x0000f8
#define EMAC_P1588_REAL_TIME_3                   0x0000fc
#define EMAC_P1588_REAL_TIME_2                   0x000100
#define EMAC_P1588_REAL_TIME_1                   0x000104
#define EMAC_P1588_ADJUST_RTC                    0x000110
#define EMAC_P1588_PTP_EXPANDED_INT_MASK         0x0003c4
#define EMAC_P1588_PTP_EXPANDED_INT_STATUS       0x0003c8
#define EMAC_P1588_RTC_EXPANDED_CONFIG           0x000400
#define EMAC_P1588_RTC_PRELOADED_5               0x000404
#define EMAC_P1588_RTC_PRELOADED_4               0x000408
#define EMAC_P1588_RTC_PRELOADED_3               0x00040c
#define EMAC_P1588_RTC_PRELOADED_2               0x000410
#define EMAC_P1588_RTC_PRELOADED_1               0x000414
#define EMAC_P1588_GRAND_MASTER_CONFIG_0         0x000800
#define EMAC_P1588_GM_PPS_TIMESTAMP_2            0x000814
#define EMAC_P1588_GM_PPS_TIMESTAMP_1            0x000818

#endif /* __EMAC_REGS_H__ */
