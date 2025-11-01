/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2015 - 2025 Beijing WangXun Technology Co., Ltd. */

#ifndef _TXGBEVF_TYPE_H_
#define _TXGBEVF_TYPE_H_

/* Device IDs */
#define TXGBEVF_DEV_ID_SP1000                  0x1000
#define TXGBEVF_DEV_ID_WX1820                  0x2000
#define TXGBEVF_DEV_ID_AML500F                 0x500F
#define TXGBEVF_DEV_ID_AML510F                 0x510F
#define TXGBEVF_DEV_ID_AML5024                 0x5024
#define TXGBEVF_DEV_ID_AML5124                 0x5124
#define TXGBEVF_DEV_ID_AML503F                 0x503f
#define TXGBEVF_DEV_ID_AML513F                 0x513f

#define TXGBEVF_MAX_MSIX_VECTORS               2
#define TXGBEVF_MAX_RSS_NUM                    4
#define TXGBEVF_MAX_RX_QUEUES                  4
#define TXGBEVF_MAX_TX_QUEUES                  4
#define TXGBEVF_DEFAULT_TXD                    128
#define TXGBEVF_DEFAULT_RXD                    128
#define TXGBEVF_DEFAULT_TX_WORK                256
#define TXGBEVF_DEFAULT_RX_WORK                256

#endif /* _TXGBEVF_TYPE_H_ */
