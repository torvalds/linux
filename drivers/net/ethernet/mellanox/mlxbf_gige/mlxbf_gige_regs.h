/* SPDX-License-Identifier: GPL-2.0-only OR BSD-3-Clause */

/* Header file for Mellanox BlueField GigE register defines
 *
 * Copyright (C) 2020-2021 NVIDIA CORPORATION & AFFILIATES
 */

#ifndef __MLXBF_GIGE_REGS_H__
#define __MLXBF_GIGE_REGS_H__

#define MLXBF_GIGE_VERSION                            0x0000
#define MLXBF_GIGE_VERSION_BF2                        0x0
#define MLXBF_GIGE_VERSION_BF3                        0x1
#define MLXBF_GIGE_STATUS                             0x0010
#define MLXBF_GIGE_STATUS_READY                       BIT(0)
#define MLXBF_GIGE_INT_STATUS                         0x0028
#define MLXBF_GIGE_INT_STATUS_RX_RECEIVE_PACKET       BIT(0)
#define MLXBF_GIGE_INT_STATUS_RX_MAC_ERROR            BIT(1)
#define MLXBF_GIGE_INT_STATUS_RX_TRN_ERROR            BIT(2)
#define MLXBF_GIGE_INT_STATUS_SW_ACCESS_ERROR         BIT(3)
#define MLXBF_GIGE_INT_STATUS_SW_CONFIG_ERROR         BIT(4)
#define MLXBF_GIGE_INT_STATUS_TX_PI_CI_EXCEED_WQ_SIZE BIT(5)
#define MLXBF_GIGE_INT_STATUS_TX_SMALL_FRAME_SIZE     BIT(6)
#define MLXBF_GIGE_INT_STATUS_TX_CHECKSUM_INPUTS      BIT(7)
#define MLXBF_GIGE_INT_STATUS_HW_ACCESS_ERROR         BIT(8)
#define MLXBF_GIGE_INT_EN                             0x0030
#define MLXBF_GIGE_INT_EN_RX_RECEIVE_PACKET           BIT(0)
#define MLXBF_GIGE_INT_EN_RX_MAC_ERROR                BIT(1)
#define MLXBF_GIGE_INT_EN_RX_TRN_ERROR                BIT(2)
#define MLXBF_GIGE_INT_EN_SW_ACCESS_ERROR             BIT(3)
#define MLXBF_GIGE_INT_EN_SW_CONFIG_ERROR             BIT(4)
#define MLXBF_GIGE_INT_EN_TX_PI_CI_EXCEED_WQ_SIZE     BIT(5)
#define MLXBF_GIGE_INT_EN_TX_SMALL_FRAME_SIZE         BIT(6)
#define MLXBF_GIGE_INT_EN_TX_CHECKSUM_INPUTS          BIT(7)
#define MLXBF_GIGE_INT_EN_HW_ACCESS_ERROR             BIT(8)
#define MLXBF_GIGE_INT_MASK                           0x0038
#define MLXBF_GIGE_INT_MASK_RX_RECEIVE_PACKET         BIT(0)
#define MLXBF_GIGE_CONTROL                            0x0040
#define MLXBF_GIGE_CONTROL_PORT_EN                    BIT(0)
#define MLXBF_GIGE_CONTROL_MAC_ID_RANGE_EN            BIT(1)
#define MLXBF_GIGE_CONTROL_EN_SPECIFIC_MAC            BIT(4)
#define MLXBF_GIGE_CONTROL_CLEAN_PORT_EN              BIT(31)
#define MLXBF_GIGE_RX_WQ_BASE                         0x0200
#define MLXBF_GIGE_RX_WQE_SIZE_LOG2                   0x0208
#define MLXBF_GIGE_RX_WQE_SIZE_LOG2_RESET_VAL         7
#define MLXBF_GIGE_RX_CQ_BASE                         0x0210
#define MLXBF_GIGE_TX_WQ_BASE                         0x0218
#define MLXBF_GIGE_TX_WQ_SIZE_LOG2                    0x0220
#define MLXBF_GIGE_TX_WQ_SIZE_LOG2_RESET_VAL          7
#define MLXBF_GIGE_TX_CI_UPDATE_ADDRESS               0x0228
#define MLXBF_GIGE_RX_WQE_PI                          0x0230
#define MLXBF_GIGE_TX_PRODUCER_INDEX                  0x0238
#define MLXBF_GIGE_RX_MAC_FILTER                      0x0240
#define MLXBF_GIGE_RX_MAC_FILTER_STRIDE               0x0008
#define MLXBF_GIGE_RX_DIN_DROP_COUNTER                0x0260
#define MLXBF_GIGE_TX_CONSUMER_INDEX                  0x0310
#define MLXBF_GIGE_TX_CONTROL                         0x0318
#define MLXBF_GIGE_TX_CONTROL_GRACEFUL_STOP           BIT(0)
#define MLXBF_GIGE_TX_STATUS                          0x0388
#define MLXBF_GIGE_TX_STATUS_DATA_FIFO_FULL           BIT(1)
#define MLXBF_GIGE_RX_MAC_FILTER_DMAC_RANGE_START     0x0520
#define MLXBF_GIGE_RX_MAC_FILTER_DMAC_RANGE_END       0x0528
#define MLXBF_GIGE_RX_MAC_FILTER_COUNT_DISC           0x0540
#define MLXBF_GIGE_RX_MAC_FILTER_COUNT_DISC_EN        BIT(0)
#define MLXBF_GIGE_RX_MAC_FILTER_COUNT_PASS           0x0548
#define MLXBF_GIGE_RX_MAC_FILTER_COUNT_PASS_EN        BIT(0)
#define MLXBF_GIGE_RX_PASS_COUNTER_ALL                0x0550
#define MLXBF_GIGE_RX_DISC_COUNTER_ALL                0x0560
#define MLXBF_GIGE_RX                                 0x0578
#define MLXBF_GIGE_RX_STRIP_CRC_EN                    BIT(1)
#define MLXBF_GIGE_RX_DMA                             0x0580
#define MLXBF_GIGE_RX_DMA_EN                          BIT(0)
#define MLXBF_GIGE_RX_CQE_PACKET_CI                   0x05b0
#define MLXBF_GIGE_MAC_CFG                            0x05e8

/* NOTE: MLXBF_GIGE_MAC_CFG is the last defined register offset,
 * so use that plus size of single register to derive total size
 */
#define MLXBF_GIGE_MMIO_REG_SZ                        (MLXBF_GIGE_MAC_CFG + 8)

#endif /* !defined(__MLXBF_GIGE_REGS_H__) */
