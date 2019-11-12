/******************************************************************************
 *
 * Copyright(c) 2016 - 2019 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 ******************************************************************************/

#ifndef _HALMAC_8822B_CFG_H_
#define _HALMAC_8822B_CFG_H_

#include "../../halmac_hw_cfg.h"
#include "../halmac_88xx_cfg.h"

#if HALMAC_8822B_SUPPORT

#define TX_FIFO_SIZE_8822B	262144
#define RX_FIFO_SIZE_8822B	24576
#define TRX_SHARE_SIZE_8822B	65536

#define RX_DESC_DUMMY_SIZE_8822B	72 /* 8 * 9 Bytes */
#define RX_FIFO_EXPANDING_MODE_PKT_SIZE_MAX_8822B	80 /* 8 Byte alignment*/

/* should be 8 Byte alignment*/
#if (HALMAC_RX_FIFO_EXPANDING_MODE_PKT_SIZE <= \
	RX_FIFO_EXPANDING_MODE_PKT_SIZE_MAX_8822B)
#define RX_FIFO_EXPANDING_UNIT_8822B	(RX_DESC_SIZE_88XX + \
	RX_DESC_DUMMY_SIZE_8822B + HALMAC_RX_FIFO_EXPANDING_MODE_PKT_SIZE)
#else
#define RX_FIFO_EXPANDING_UNIT_8822B (RX_DESC_SIZE_88XX + \
	RX_DESC_DUMMY_SIZE_8822B + RX_FIFO_EXPANDING_MODE_PKT_SIZE_MAX_8822B)
#endif

#define TX_FIFO_SIZE_LA_8822B	(TX_FIFO_SIZE_8822B >>  1)
#define TX_FIFO_SIZE_RX_EXPAND_1BLK_8822B	\
		(TX_FIFO_SIZE_8822B - TRX_SHARE_SIZE_8822B)
#define RX_FIFO_SIZE_RX_EXPAND_1BLK_8822B	\
		((((RX_FIFO_EXPANDING_UNIT_8822B << 8) - 1) >> 10) << 10)
#define TX_FIFO_SIZE_RX_EXPAND_2BLK_8822B	\
		(TX_FIFO_SIZE_8822B - (2 * TRX_SHARE_SIZE_8822B))
#define RX_FIFO_SIZE_RX_EXPAND_2BLK_8822B	\
		(RX_FIFO_SIZE_8822B +  (2 * TRX_SHARE_SIZE_8822B))
#define TX_FIFO_SIZE_RX_EXPAND_3BLK_8822B	\
		(TX_FIFO_SIZE_8822B - (3 * TRX_SHARE_SIZE_8822B))
#define RX_FIFO_SIZE_RX_EXPAND_3BLK_8822B	\
		(RX_FIFO_SIZE_8822B +  (3 * TRX_SHARE_SIZE_8822B))

#define EFUSE_SIZE_8822B		1024
#define EEPROM_SIZE_8822B		768
#define BT_EFUSE_SIZE_8822B		128
#define PRTCT_EFUSE_SIZE_8822B	96

#define SEC_CAM_NUM_8822B		64

#define OQT_ENTRY_AC_8822B		32
#define OQT_ENTRY_NOAC_8822B		32
#define MACID_MAX_8822B			128

#define WLAN_FW_IRAM_MAX_SIZE_8822B	196608
#define WLAN_FW_DRAM_MAX_SIZE_8822B	49152
#define WLAN_FW_ERAM_MAX_SIZE_8822B	0
#define WLAN_FW_MAX_SIZE_8822B		(WLAN_FW_IRAM_MAX_SIZE_8822B + \
	WLAN_FW_DRAM_MAX_SIZE_8822B + WLAN_FW_ERAM_MAX_SIZE_8822B)

#endif /* HALMAC_8822B_SUPPORT*/

#endif
