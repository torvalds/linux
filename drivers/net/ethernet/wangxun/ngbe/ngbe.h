/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 - 2022 Beijing WangXun Technology Co., Ltd. */

#ifndef _NGBE_H_
#define _NGBE_H_

#define NGBE_MAX_FDIR_INDICES		7

#define NGBE_MAX_RX_QUEUES		(NGBE_MAX_FDIR_INDICES + 1)
#define NGBE_MAX_TX_QUEUES		(NGBE_MAX_FDIR_INDICES + 1)

#define NGBE_ETH_LENGTH_OF_ADDRESS	6
#define NGBE_MAX_MSIX_VECTORS		0x09
#define NGBE_RAR_ENTRIES		32

/* TX/RX descriptor defines */
#define NGBE_DEFAULT_TXD		512 /* default ring size */
#define NGBE_DEFAULT_TX_WORK		256
#define NGBE_MAX_TXD			8192
#define NGBE_MIN_TXD			128

#define NGBE_DEFAULT_RXD		512 /* default ring size */
#define NGBE_DEFAULT_RX_WORK		256
#define NGBE_MAX_RXD			8192
#define NGBE_MIN_RXD			128

#define NGBE_MAC_STATE_DEFAULT		0x1
#define NGBE_MAC_STATE_MODIFIED		0x2
#define NGBE_MAC_STATE_IN_USE		0x4

extern char ngbe_driver_name[];

#endif /* _NGBE_H_ */
