/*
 * Copyright (c) 2015 Qualcomm Atheros Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef REG_MCI_H
#define REG_MCI_H

#define AR_MCI_COMMAND0                                 0x1800
#define AR_MCI_COMMAND0_HEADER                          0xFF
#define AR_MCI_COMMAND0_HEADER_S                        0
#define AR_MCI_COMMAND0_LEN                             0x1f00
#define AR_MCI_COMMAND0_LEN_S                           8
#define AR_MCI_COMMAND0_DISABLE_TIMESTAMP               0x2000
#define AR_MCI_COMMAND0_DISABLE_TIMESTAMP_S             13

#define AR_MCI_COMMAND1                                 0x1804

#define AR_MCI_COMMAND2                                 0x1808
#define AR_MCI_COMMAND2_RESET_TX                        0x01
#define AR_MCI_COMMAND2_RESET_TX_S                      0
#define AR_MCI_COMMAND2_RESET_RX                        0x02
#define AR_MCI_COMMAND2_RESET_RX_S                      1
#define AR_MCI_COMMAND2_RESET_RX_NUM_CYCLES             0x3FC
#define AR_MCI_COMMAND2_RESET_RX_NUM_CYCLES_S           2
#define AR_MCI_COMMAND2_RESET_REQ_WAKEUP                0x400
#define AR_MCI_COMMAND2_RESET_REQ_WAKEUP_S              10

#define AR_MCI_RX_CTRL                                  0x180c

#define AR_MCI_TX_CTRL                                  0x1810
/*
 * 0 = no division,
 * 1 = divide by 2,
 * 2 = divide by 4,
 * 3 = divide by 8
 */
#define AR_MCI_TX_CTRL_CLK_DIV                          0x03
#define AR_MCI_TX_CTRL_CLK_DIV_S                        0
#define AR_MCI_TX_CTRL_DISABLE_LNA_UPDATE               0x04
#define AR_MCI_TX_CTRL_DISABLE_LNA_UPDATE_S             2
#define AR_MCI_TX_CTRL_GAIN_UPDATE_FREQ                 0xFFFFF8
#define AR_MCI_TX_CTRL_GAIN_UPDATE_FREQ_S               3
#define AR_MCI_TX_CTRL_GAIN_UPDATE_NUM                  0xF000000
#define AR_MCI_TX_CTRL_GAIN_UPDATE_NUM_S                24

#define AR_MCI_MSG_ATTRIBUTES_TABLE                     0x1814
#define AR_MCI_MSG_ATTRIBUTES_TABLE_CHECKSUM            0xFFFF
#define AR_MCI_MSG_ATTRIBUTES_TABLE_CHECKSUM_S          0
#define AR_MCI_MSG_ATTRIBUTES_TABLE_INVALID_HDR         0xFFFF0000
#define AR_MCI_MSG_ATTRIBUTES_TABLE_INVALID_HDR_S       16

#define AR_MCI_SCHD_TABLE_0                             0x1818
#define AR_MCI_SCHD_TABLE_1                             0x181c
#define AR_MCI_GPM_0                                    0x1820
#define AR_MCI_GPM_1                                    0x1824
#define AR_MCI_GPM_WRITE_PTR                            0xFFFF0000
#define AR_MCI_GPM_WRITE_PTR_S                          16
#define AR_MCI_GPM_BUF_LEN                              0x0000FFFF
#define AR_MCI_GPM_BUF_LEN_S                            0

#define AR_MCI_INTERRUPT_RAW                            0x1828

#define AR_MCI_INTERRUPT_EN                             0x182c
#define AR_MCI_INTERRUPT_SW_MSG_DONE                    0x00000001
#define AR_MCI_INTERRUPT_SW_MSG_DONE_S                  0
#define AR_MCI_INTERRUPT_CPU_INT_MSG                    0x00000002
#define AR_MCI_INTERRUPT_CPU_INT_MSG_S                  1
#define AR_MCI_INTERRUPT_RX_CKSUM_FAIL                  0x00000004
#define AR_MCI_INTERRUPT_RX_CKSUM_FAIL_S                2
#define AR_MCI_INTERRUPT_RX_INVALID_HDR                 0x00000008
#define AR_MCI_INTERRUPT_RX_INVALID_HDR_S               3
#define AR_MCI_INTERRUPT_RX_HW_MSG_FAIL                 0x00000010
#define AR_MCI_INTERRUPT_RX_HW_MSG_FAIL_S               4
#define AR_MCI_INTERRUPT_RX_SW_MSG_FAIL                 0x00000020
#define AR_MCI_INTERRUPT_RX_SW_MSG_FAIL_S               5
#define AR_MCI_INTERRUPT_TX_HW_MSG_FAIL                 0x00000080
#define AR_MCI_INTERRUPT_TX_HW_MSG_FAIL_S               7
#define AR_MCI_INTERRUPT_TX_SW_MSG_FAIL                 0x00000100
#define AR_MCI_INTERRUPT_TX_SW_MSG_FAIL_S               8
#define AR_MCI_INTERRUPT_RX_MSG                         0x00000200
#define AR_MCI_INTERRUPT_RX_MSG_S                       9
#define AR_MCI_INTERRUPT_REMOTE_SLEEP_UPDATE            0x00000400
#define AR_MCI_INTERRUPT_REMOTE_SLEEP_UPDATE_S          10
#define AR_MCI_INTERRUPT_BT_PRI                         0x07fff800
#define AR_MCI_INTERRUPT_BT_PRI_S                       11
#define AR_MCI_INTERRUPT_BT_PRI_THRESH                  0x08000000
#define AR_MCI_INTERRUPT_BT_PRI_THRESH_S                27
#define AR_MCI_INTERRUPT_BT_FREQ                        0x10000000
#define AR_MCI_INTERRUPT_BT_FREQ_S                      28
#define AR_MCI_INTERRUPT_BT_STOMP                       0x20000000
#define AR_MCI_INTERRUPT_BT_STOMP_S                     29
#define AR_MCI_INTERRUPT_BB_AIC_IRQ                     0x40000000
#define AR_MCI_INTERRUPT_BB_AIC_IRQ_S                   30
#define AR_MCI_INTERRUPT_CONT_INFO_TIMEOUT              0x80000000
#define AR_MCI_INTERRUPT_CONT_INFO_TIMEOUT_S            31

#define AR_MCI_REMOTE_CPU_INT                           0x1830
#define AR_MCI_REMOTE_CPU_INT_EN                        0x1834
#define AR_MCI_INTERRUPT_RX_MSG_RAW                     0x1838
#define AR_MCI_INTERRUPT_RX_MSG_EN                      0x183c
#define AR_MCI_INTERRUPT_RX_MSG_REMOTE_RESET            0x00000001
#define AR_MCI_INTERRUPT_RX_MSG_REMOTE_RESET_S          0
#define AR_MCI_INTERRUPT_RX_MSG_LNA_CONTROL             0x00000002
#define AR_MCI_INTERRUPT_RX_MSG_LNA_CONTROL_S           1
#define AR_MCI_INTERRUPT_RX_MSG_CONT_NACK               0x00000004
#define AR_MCI_INTERRUPT_RX_MSG_CONT_NACK_S             2
#define AR_MCI_INTERRUPT_RX_MSG_CONT_INFO               0x00000008
#define AR_MCI_INTERRUPT_RX_MSG_CONT_INFO_S             3
#define AR_MCI_INTERRUPT_RX_MSG_CONT_RST                0x00000010
#define AR_MCI_INTERRUPT_RX_MSG_CONT_RST_S              4
#define AR_MCI_INTERRUPT_RX_MSG_SCHD_INFO               0x00000020
#define AR_MCI_INTERRUPT_RX_MSG_SCHD_INFO_S             5
#define AR_MCI_INTERRUPT_RX_MSG_CPU_INT                 0x00000040
#define AR_MCI_INTERRUPT_RX_MSG_CPU_INT_S               6
#define AR_MCI_INTERRUPT_RX_MSG_GPM                     0x00000100
#define AR_MCI_INTERRUPT_RX_MSG_GPM_S                   8
#define AR_MCI_INTERRUPT_RX_MSG_LNA_INFO                0x00000200
#define AR_MCI_INTERRUPT_RX_MSG_LNA_INFO_S              9
#define AR_MCI_INTERRUPT_RX_MSG_SYS_SLEEPING            0x00000400
#define AR_MCI_INTERRUPT_RX_MSG_SYS_SLEEPING_S          10
#define AR_MCI_INTERRUPT_RX_MSG_SYS_WAKING              0x00000800
#define AR_MCI_INTERRUPT_RX_MSG_SYS_WAKING_S            11
#define AR_MCI_INTERRUPT_RX_MSG_REQ_WAKE                0x00001000
#define AR_MCI_INTERRUPT_RX_MSG_REQ_WAKE_S              12

#define AR_MCI_CPU_INT                                  0x1840

#define AR_MCI_RX_STATUS                                0x1844
#define AR_MCI_RX_LAST_SCHD_MSG_INDEX                   0x00000F00
#define AR_MCI_RX_LAST_SCHD_MSG_INDEX_S                 8
#define AR_MCI_RX_REMOTE_SLEEP                          0x00001000
#define AR_MCI_RX_REMOTE_SLEEP_S                        12
#define AR_MCI_RX_MCI_CLK_REQ                           0x00002000
#define AR_MCI_RX_MCI_CLK_REQ_S                         13

#define AR_MCI_CONT_STATUS                              0x1848
#define AR_MCI_CONT_RSSI_POWER                          0x000000FF
#define AR_MCI_CONT_RSSI_POWER_S                        0
#define AR_MCI_CONT_PRIORITY                            0x0000FF00
#define AR_MCI_CONT_PRIORITY_S                          8
#define AR_MCI_CONT_TXRX                                0x00010000
#define AR_MCI_CONT_TXRX_S                              16

#define AR_MCI_BT_PRI0                                  0x184c
#define AR_MCI_BT_PRI1                                  0x1850
#define AR_MCI_BT_PRI2                                  0x1854
#define AR_MCI_BT_PRI3                                  0x1858
#define AR_MCI_BT_PRI                                   0x185c
#define AR_MCI_WL_FREQ0                                 0x1860
#define AR_MCI_WL_FREQ1                                 0x1864
#define AR_MCI_WL_FREQ2                                 0x1868
#define AR_MCI_GAIN                                     0x186c
#define AR_MCI_WBTIMER1                                 0x1870
#define AR_MCI_WBTIMER2                                 0x1874
#define AR_MCI_WBTIMER3                                 0x1878
#define AR_MCI_WBTIMER4                                 0x187c
#define AR_MCI_MAXGAIN                                  0x1880
#define AR_MCI_HW_SCHD_TBL_CTL                          0x1884
#define AR_MCI_HW_SCHD_TBL_D0                           0x1888
#define AR_MCI_HW_SCHD_TBL_D1                           0x188c
#define AR_MCI_HW_SCHD_TBL_D2                           0x1890
#define AR_MCI_HW_SCHD_TBL_D3                           0x1894
#define AR_MCI_TX_PAYLOAD0                              0x1898
#define AR_MCI_TX_PAYLOAD1                              0x189c
#define AR_MCI_TX_PAYLOAD2                              0x18a0
#define AR_MCI_TX_PAYLOAD3                              0x18a4
#define AR_BTCOEX_WBTIMER                               0x18a8

#define AR_BTCOEX_CTRL                                  0x18ac
#define AR_BTCOEX_CTRL_AR9462_MODE                      0x00000001
#define AR_BTCOEX_CTRL_AR9462_MODE_S                    0
#define AR_BTCOEX_CTRL_WBTIMER_EN                       0x00000002
#define AR_BTCOEX_CTRL_WBTIMER_EN_S                     1
#define AR_BTCOEX_CTRL_MCI_MODE_EN                      0x00000004
#define AR_BTCOEX_CTRL_MCI_MODE_EN_S                    2
#define AR_BTCOEX_CTRL_LNA_SHARED                       0x00000008
#define AR_BTCOEX_CTRL_LNA_SHARED_S                     3
#define AR_BTCOEX_CTRL_PA_SHARED                        0x00000010
#define AR_BTCOEX_CTRL_PA_SHARED_S                      4
#define AR_BTCOEX_CTRL_ONE_STEP_LOOK_AHEAD_EN           0x00000020
#define AR_BTCOEX_CTRL_ONE_STEP_LOOK_AHEAD_EN_S         5
#define AR_BTCOEX_CTRL_TIME_TO_NEXT_BT_THRESH_EN        0x00000040
#define AR_BTCOEX_CTRL_TIME_TO_NEXT_BT_THRESH_EN_S      6
#define AR_BTCOEX_CTRL_NUM_ANTENNAS                     0x00000180
#define AR_BTCOEX_CTRL_NUM_ANTENNAS_S                   7
#define AR_BTCOEX_CTRL_RX_CHAIN_MASK                    0x00000E00
#define AR_BTCOEX_CTRL_RX_CHAIN_MASK_S                  9
#define AR_BTCOEX_CTRL_AGGR_THRESH                      0x00007000
#define AR_BTCOEX_CTRL_AGGR_THRESH_S                    12
#define AR_BTCOEX_CTRL_1_CHAIN_BCN                      0x00080000
#define AR_BTCOEX_CTRL_1_CHAIN_BCN_S                    19
#define AR_BTCOEX_CTRL_1_CHAIN_ACK                      0x00100000
#define AR_BTCOEX_CTRL_1_CHAIN_ACK_S                    20
#define AR_BTCOEX_CTRL_WAIT_BA_MARGIN                   0x1FE00000
#define AR_BTCOEX_CTRL_WAIT_BA_MARGIN_S                 28
#define AR_BTCOEX_CTRL_REDUCE_TXPWR                     0x20000000
#define AR_BTCOEX_CTRL_REDUCE_TXPWR_S                   29
#define AR_BTCOEX_CTRL_SPDT_ENABLE_10                   0x40000000
#define AR_BTCOEX_CTRL_SPDT_ENABLE_10_S                 30
#define AR_BTCOEX_CTRL_SPDT_POLARITY                    0x80000000
#define AR_BTCOEX_CTRL_SPDT_POLARITY_S                  31

#define AR_BTCOEX_WL_WEIGHTS0                           0x18b0
#define AR_BTCOEX_WL_WEIGHTS1                           0x18b4
#define AR_BTCOEX_WL_WEIGHTS2                           0x18b8
#define AR_BTCOEX_WL_WEIGHTS3                           0x18bc

#define AR_BTCOEX_MAX_TXPWR(_x)                         (0x18c0 + ((_x) << 2))
#define AR_BTCOEX_WL_LNA                                0x1940
#define AR_BTCOEX_RFGAIN_CTRL                           0x1944
#define AR_BTCOEX_WL_LNA_TIMEOUT                        0x003FFFFF
#define AR_BTCOEX_WL_LNA_TIMEOUT_S                      0

#define AR_BTCOEX_CTRL2                                 0x1948
#define AR_BTCOEX_CTRL2_TXPWR_THRESH                    0x0007F800
#define AR_BTCOEX_CTRL2_TXPWR_THRESH_S                  11
#define AR_BTCOEX_CTRL2_TX_CHAIN_MASK                   0x00380000
#define AR_BTCOEX_CTRL2_TX_CHAIN_MASK_S                 19
#define AR_BTCOEX_CTRL2_RX_DEWEIGHT                     0x00400000
#define AR_BTCOEX_CTRL2_RX_DEWEIGHT_S                   22
#define AR_BTCOEX_CTRL2_GPIO_OBS_SEL                    0x00800000
#define AR_BTCOEX_CTRL2_GPIO_OBS_SEL_S                  23
#define AR_BTCOEX_CTRL2_MAC_BB_OBS_SEL                  0x01000000
#define AR_BTCOEX_CTRL2_MAC_BB_OBS_SEL_S                24
#define AR_BTCOEX_CTRL2_DESC_BASED_TXPWR_ENABLE         0x02000000
#define AR_BTCOEX_CTRL2_DESC_BASED_TXPWR_ENABLE_S       25

#define AR_BTCOEX_CTRL_SPDT_ENABLE                      0x00000001
#define AR_BTCOEX_CTRL_SPDT_ENABLE_S                    0
#define AR_BTCOEX_CTRL_BT_OWN_SPDT_CTRL                 0x00000002
#define AR_BTCOEX_CTRL_BT_OWN_SPDT_CTRL_S               1
#define AR_BTCOEX_CTRL_USE_LATCHED_BT_ANT               0x00000004
#define AR_BTCOEX_CTRL_USE_LATCHED_BT_ANT_S             2
#define AR_GLB_WLAN_UART_INTF_EN                        0x00020000
#define AR_GLB_WLAN_UART_INTF_EN_S                      17
#define AR_GLB_DS_JTAG_DISABLE                          0x00040000
#define AR_GLB_DS_JTAG_DISABLE_S                        18

#define AR_BTCOEX_RC                                    0x194c
#define AR_BTCOEX_MAX_RFGAIN(_x)                        (0x1950 + ((_x) << 2))
#define AR_BTCOEX_DBG                                   0x1a50
#define AR_MCI_LAST_HW_MSG_HDR                          0x1a54
#define AR_MCI_LAST_HW_MSG_BDY                          0x1a58

#define AR_MCI_SCHD_TABLE_2                             0x1a5c
#define AR_MCI_SCHD_TABLE_2_MEM_BASED                   0x00000001
#define AR_MCI_SCHD_TABLE_2_MEM_BASED_S                 0
#define AR_MCI_SCHD_TABLE_2_HW_BASED                    0x00000002
#define AR_MCI_SCHD_TABLE_2_HW_BASED_S                  1

#define AR_BTCOEX_CTRL3                                 0x1a60
#define AR_BTCOEX_CTRL3_CONT_INFO_TIMEOUT               0x00000fff
#define AR_BTCOEX_CTRL3_CONT_INFO_TIMEOUT_S             0

#define AR_GLB_SWREG_DISCONT_MODE                       0x2002c
#define AR_GLB_SWREG_DISCONT_EN_BT_WLAN                 0x3

#define AR_MCI_MISC                                     0x1a74
#define AR_MCI_MISC_HW_FIX_EN                           0x00000001
#define AR_MCI_MISC_HW_FIX_EN_S                         0

#define AR_MCI_DBG_CNT_CTRL                             0x1a78
#define AR_MCI_DBG_CNT_CTRL_ENABLE                      0x00000001
#define AR_MCI_DBG_CNT_CTRL_ENABLE_S                    0
#define AR_MCI_DBG_CNT_CTRL_BT_LINKID                   0x000007f8
#define AR_MCI_DBG_CNT_CTRL_BT_LINKID_S                 3

#define MCI_STAT_ALL_BT_LINKID                          0xffff

#define AR_MCI_INTERRUPT_DEFAULT (AR_MCI_INTERRUPT_SW_MSG_DONE         | \
				  AR_MCI_INTERRUPT_RX_INVALID_HDR      | \
				  AR_MCI_INTERRUPT_RX_HW_MSG_FAIL      | \
				  AR_MCI_INTERRUPT_RX_SW_MSG_FAIL      | \
				  AR_MCI_INTERRUPT_TX_HW_MSG_FAIL      | \
				  AR_MCI_INTERRUPT_TX_SW_MSG_FAIL      | \
				  AR_MCI_INTERRUPT_RX_MSG              | \
				  AR_MCI_INTERRUPT_REMOTE_SLEEP_UPDATE | \
				  AR_MCI_INTERRUPT_CONT_INFO_TIMEOUT)

#define AR_MCI_INTERRUPT_MSG_FAIL_MASK (AR_MCI_INTERRUPT_RX_HW_MSG_FAIL | \
                                        AR_MCI_INTERRUPT_RX_SW_MSG_FAIL | \
                                        AR_MCI_INTERRUPT_TX_HW_MSG_FAIL | \
                                        AR_MCI_INTERRUPT_TX_SW_MSG_FAIL)

#define AR_MCI_INTERRUPT_RX_HW_MSG_MASK (AR_MCI_INTERRUPT_RX_MSG_SCHD_INFO   | \
					 AR_MCI_INTERRUPT_RX_MSG_LNA_CONTROL | \
					 AR_MCI_INTERRUPT_RX_MSG_LNA_INFO    | \
					 AR_MCI_INTERRUPT_RX_MSG_CONT_NACK   | \
					 AR_MCI_INTERRUPT_RX_MSG_CONT_INFO   | \
					 AR_MCI_INTERRUPT_RX_MSG_CONT_RST)

#define AR_MCI_INTERRUPT_RX_MSG_DEFAULT (AR_MCI_INTERRUPT_RX_MSG_GPM           | \
                                         AR_MCI_INTERRUPT_RX_MSG_REMOTE_RESET  | \
                                         AR_MCI_INTERRUPT_RX_MSG_SYS_WAKING    | \
                                         AR_MCI_INTERRUPT_RX_MSG_SYS_SLEEPING  | \
                                         AR_MCI_INTERRUPT_RX_MSG_REQ_WAKE)

#endif /* REG_MCI_H */
