/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef _csi_rx_defs_h
#define _csi_rx_defs_h

//#include "rx_csi_common_defs.h"

#define MIPI_PKT_DATA_WIDTH                         32
//#define CLK_CROSSING_FIFO_DEPTH                     16
#define _CSI_RX_REG_ALIGN                            4

//define number of IRQ (see below for definition of each IRQ bits)
#define CSI_RX_NOF_IRQS_BYTE_DOMAIN                11
#define CSI_RX_NOF_IRQS_ISP_DOMAIN                 15 // CSI_RX_NOF_IRQS_BYTE_DOMAIN + remaining from Dphy_rx already on ISP clock domain

// REGISTER DESCRIPTION
//#define _HRT_CSI_RX_SOFTRESET_REG_IDX                0
#define _HRT_CSI_RX_ENABLE_REG_IDX                   0
#define _HRT_CSI_RX_NOF_ENABLED_LANES_REG_IDX        1
#define _HRT_CSI_RX_ERROR_HANDLING_REG_IDX           2
#define _HRT_CSI_RX_STATUS_REG_IDX                   3
#define _HRT_CSI_RX_STATUS_DLANE_HS_REG_IDX          4
#define _HRT_CSI_RX_STATUS_DLANE_LP_REG_IDX          5
//#define _HRT_CSI_RX_IRQ_CONFIG_REG_IDX               6
#define _HRT_CSI_RX_DLY_CNT_TERMEN_CLANE_REG_IDX     6
#define _HRT_CSI_RX_DLY_CNT_SETTLE_CLANE_REG_IDX     7
#define _HRT_CSI_RX_DLY_CNT_TERMEN_DLANE_REG_IDX(lane_idx)    (8 + (2 * lane_idx))
#define _HRT_CSI_RX_DLY_CNT_SETTLE_DLANE_REG_IDX(lane_idx)    (8 + (2 * lane_idx) + 1)

#define _HRT_CSI_RX_NOF_REGISTERS(nof_dlanes)      (8 + 2 * (nof_dlanes))

//#define _HRT_CSI_RX_SOFTRESET_REG_WIDTH              1
#define _HRT_CSI_RX_ENABLE_REG_WIDTH                 1
#define _HRT_CSI_RX_NOF_ENABLED_LANES_REG_WIDTH      3
#define _HRT_CSI_RX_ERROR_HANDLING_REG_WIDTH         4
#define _HRT_CSI_RX_STATUS_REG_WIDTH                 1
#define _HRT_CSI_RX_STATUS_DLANE_HS_REG_WIDTH        8
#define _HRT_CSI_RX_STATUS_DLANE_LP_REG_WIDTH        24
#define _HRT_CSI_RX_IRQ_CONFIG_REG_WIDTH             (CSI_RX_NOF_IRQS_ISP_DOMAIN)
#define _HRT_CSI_RX_DLY_CNT_REG_WIDTH                24
//#define _HRT_CSI_RX_IRQ_STATUS_REG_WIDTH            NOF_IRQS
//#define _HRT_CSI_RX_IRQ_CLEAR_REG_WIDTH             0

#define ONE_LANE_ENABLED                             0
#define TWO_LANES_ENABLED                            1
#define THREE_LANES_ENABLED                          2
#define FOUR_LANES_ENABLED                           3

// Error handling reg bit positions
#define ERR_DECISION_BIT      0
#define DISC_RESERVED_SP_BIT  1
#define DISC_RESERVED_LP_BIT  2
#define DIS_INCOMP_PKT_CHK_BIT	3

#define _HRT_CSI_RX_IRQ_CONFIG_REG_VAL_POSEDGE      0
#define _HRT_CSI_RX_IRQ_CONFIG_REG_VAL_ORIGINAL     1

// Interrupt bits
#define _HRT_RX_CSI_IRQ_SINGLE_PH_ERROR_CORRECTED   0
#define _HRT_RX_CSI_IRQ_MULTIPLE_PH_ERROR_DETECTED  1
#define _HRT_RX_CSI_IRQ_PAYLOAD_CHECKSUM_ERROR      2
#define _HRT_RX_CSI_IRQ_FIFO_FULL_ERROR             3
#define _HRT_RX_CSI_IRQ_RESERVED_SP_DETECTED        4
#define _HRT_RX_CSI_IRQ_RESERVED_LP_DETECTED        5
//#define _HRT_RX_CSI_IRQ_PREMATURE_SOP               6
#define _HRT_RX_CSI_IRQ_INCOMPLETE_PACKET           6
#define _HRT_RX_CSI_IRQ_FRAME_SYNC_ERROR            7
#define _HRT_RX_CSI_IRQ_LINE_SYNC_ERROR             8
#define _HRT_RX_CSI_IRQ_DLANE_HS_SOT_ERROR          9
#define _HRT_RX_CSI_IRQ_DLANE_HS_SOT_SYNC_ERROR    10

#define _HRT_RX_CSI_IRQ_DLANE_ESC_ERROR            11
#define _HRT_RX_CSI_IRQ_DLANE_TRIGGERESC           12
#define _HRT_RX_CSI_IRQ_DLANE_ULPSESC              13
#define _HRT_RX_CSI_IRQ_CLANE_ULPSCLKNOT           14

/* OLD ARASAN FRONTEND IRQs
#define _HRT_RX_CSI_IRQ_OVERRUN_BIT                0
#define _HRT_RX_CSI_IRQ_RESERVED_BIT               1
#define _HRT_RX_CSI_IRQ_SLEEP_MODE_ENTRY_BIT       2
#define _HRT_RX_CSI_IRQ_SLEEP_MODE_EXIT_BIT        3
#define _HRT_RX_CSI_IRQ_ERR_SOT_HS_BIT             4
#define _HRT_RX_CSI_IRQ_ERR_SOT_SYNC_HS_BIT        5
#define _HRT_RX_CSI_IRQ_ERR_CONTROL_BIT            6
#define _HRT_RX_CSI_IRQ_ERR_ECC_DOUBLE_BIT         7
#define _HRT_RX_CSI_IRQ_ERR_ECC_CORRECTED_BIT      8
#define _HRT_RX_CSI_IRQ_ERR_ECC_NO_CORRECTION_BIT  9
#define _HRT_RX_CSI_IRQ_ERR_CRC_BIT               10
#define _HRT_RX_CSI_IRQ_ERR_ID_BIT                11
#define _HRT_RX_CSI_IRQ_ERR_FRAME_SYNC_BIT        12
#define _HRT_RX_CSI_IRQ_ERR_FRAME_DATA_BIT        13
#define _HRT_RX_CSI_IRQ_DATA_TIMEOUT_BIT          14
#define _HRT_RX_CSI_IRQ_ERR_ESCAPE_BIT            15
#define _HRT_RX_CSI_IRQ_ERR_LINE_SYNC_BIT         16
*/

////Bit Description for reg _HRT_CSI_RX_STATUS_DLANE_HS_REG_IDX
#define _HRT_CSI_RX_STATUS_DLANE_HS_SOT_ERR_LANE0        0
#define _HRT_CSI_RX_STATUS_DLANE_HS_SOT_ERR_LANE1        1
#define _HRT_CSI_RX_STATUS_DLANE_HS_SOT_ERR_LANE2        2
#define _HRT_CSI_RX_STATUS_DLANE_HS_SOT_ERR_LANE3        3
#define _HRT_CSI_RX_STATUS_DLANE_HS_SOT_SYNC_ERR_LANE0   4
#define _HRT_CSI_RX_STATUS_DLANE_HS_SOT_SYNC_ERR_LANE1   5
#define _HRT_CSI_RX_STATUS_DLANE_HS_SOT_SYNC_ERR_LANE2   6
#define _HRT_CSI_RX_STATUS_DLANE_HS_SOT_SYNC_ERR_LANE3   7

////Bit Description for reg _HRT_CSI_RX_STATUS_DLANE_LP_REG_IDX
#define _HRT_CSI_RX_STATUS_DLANE_LP_ESC_ERR_LANE0        0
#define _HRT_CSI_RX_STATUS_DLANE_LP_ESC_ERR_LANE1        1
#define _HRT_CSI_RX_STATUS_DLANE_LP_ESC_ERR_LANE2        2
#define _HRT_CSI_RX_STATUS_DLANE_LP_ESC_ERR_LANE3        3
#define _HRT_CSI_RX_STATUS_DLANE_LP_TRIGGERESC0_LANE0    4
#define _HRT_CSI_RX_STATUS_DLANE_LP_TRIGGERESC1_LANE0    5
#define _HRT_CSI_RX_STATUS_DLANE_LP_TRIGGERESC2_LANE0    6
#define _HRT_CSI_RX_STATUS_DLANE_LP_TRIGGERESC3_LANE0    7
#define _HRT_CSI_RX_STATUS_DLANE_LP_TRIGGERESC0_LANE1    8
#define _HRT_CSI_RX_STATUS_DLANE_LP_TRIGGERESC1_LANE1    9
#define _HRT_CSI_RX_STATUS_DLANE_LP_TRIGGERESC2_LANE1    10
#define _HRT_CSI_RX_STATUS_DLANE_LP_TRIGGERESC3_LANE1    11
#define _HRT_CSI_RX_STATUS_DLANE_LP_TRIGGERESC0_LANE2    12
#define _HRT_CSI_RX_STATUS_DLANE_LP_TRIGGERESC1_LANE2    13
#define _HRT_CSI_RX_STATUS_DLANE_LP_TRIGGERESC2_LANE2    14
#define _HRT_CSI_RX_STATUS_DLANE_LP_TRIGGERESC3_LANE2    15
#define _HRT_CSI_RX_STATUS_DLANE_LP_TRIGGERESC0_LANE3    16
#define _HRT_CSI_RX_STATUS_DLANE_LP_TRIGGERESC1_LANE3    17
#define _HRT_CSI_RX_STATUS_DLANE_LP_TRIGGERESC2_LANE3    18
#define _HRT_CSI_RX_STATUS_DLANE_LP_TRIGGERESC3_LANE3    19
#define _HRT_CSI_RX_STATUS_DLANE_LP_ULPSESC_LANE0        20
#define _HRT_CSI_RX_STATUS_DLANE_LP_ULPSESC_LANE1        21
#define _HRT_CSI_RX_STATUS_DLANE_LP_ULPSESC_LANE2        22
#define _HRT_CSI_RX_STATUS_DLANE_LP_ULPSESC_LANE3        23

/*********************************************************/
/*** Relevant declarations from rx_csi_common_defs.h *****/
/*********************************************************/
/* packet bit definition */
#define _HRT_RX_CSI_PKT_SOP_BITPOS                       32
#define _HRT_RX_CSI_PKT_EOP_BITPOS                       33
#define _HRT_RX_CSI_PKT_PAYLOAD_BITPOS                    0
#define _HRT_RX_CSI_PH_CH_ID_BITPOS                      22
#define _HRT_RX_CSI_PH_FMT_ID_BITPOS                     16
#define _HRT_RX_CSI_PH_DATA_FIELD_BITPOS                  0

#define _HRT_RX_CSI_PKT_SOP_BITS                          1
#define _HRT_RX_CSI_PKT_EOP_BITS                          1
#define _HRT_RX_CSI_PKT_PAYLOAD_BITS                     32
#define _HRT_RX_CSI_PH_CH_ID_BITS                         2
#define _HRT_RX_CSI_PH_FMT_ID_BITS                        6
#define _HRT_RX_CSI_PH_DATA_FIELD_BITS                   16

/* Definition of data format ID at the interface CSS_receiver units */
#define _HRT_RX_CSI_DATA_FORMAT_ID_SOF                0   /* 00 0000    frame start                                      */
#define _HRT_RX_CSI_DATA_FORMAT_ID_EOF                1   /* 00 0001    frame end                                        */
#define _HRT_RX_CSI_DATA_FORMAT_ID_SOL                2   /* 00 0010    line start                                       */
#define _HRT_RX_CSI_DATA_FORMAT_ID_EOL                3   /* 00 0011    line end                                         */

#endif /* _csi_rx_defs_h */
