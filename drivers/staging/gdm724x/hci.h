/*
 * Copyright (c) 2012 GCT Semiconductor, Inc. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef _HCI_H_
#define _HCI_H_

#define LTE_GET_INFORMATION		0x3002
#define LTE_GET_INFORMATION_RESULT	0xB003
	#define MAC_ADDRESS		0xA2

#define LTE_LINK_ON_OFF_INDICATION	0xB133
#define LTE_PDN_TABLE_IND		0xB143

#define LTE_TX_SDU			0x3200
#define LTE_RX_SDU			0xB201
#define LTE_TX_MULTI_SDU		0x3202
#define LTE_RX_MULTI_SDU		0xB203

#define LTE_DL_SDU_FLOW_CONTROL		0x3305
#define LTE_UL_SDU_FLOW_CONTROL		0xB306

#define LTE_AT_CMD_TO_DEVICE		0x3307
#define LTE_AT_CMD_FROM_DEVICE		0xB308

#define LTE_SDIO_DM_SEND_PKT		0x3312
#define LTE_SDIO_DM_RECV_PKT		0xB313

#define LTE_NV_RESTORE_REQUEST		0xB30C
#define LTE_NV_RESTORE_RESPONSE		0x330D
#define LTE_NV_SAVE_REQUEST		0xB30E
	#define NV_TYPE_LTE_INFO	0x00
	#define NV_TYPE_BOARD_CONFIG	0x01
	#define NV_TYPE_RF_CAL		0x02
	#define NV_TYPE_TEMP		0x03
	#define NV_TYPE_NET_INFO	0x04
	#define NV_TYPE_SAFETY_INFO	0x05
	#define NV_TYPE_CDMA_CAL	0x06
	#define NV_TYPE_VENDOR		0x07
	#define NV_TYPE_ALL		0xff
#define LTE_NV_SAVE_RESPONSE		0x330F

#define LTE_AT_CMD_TO_DEVICE_EXT	0x3323
#define LTE_AT_CMD_FROM_DEVICE_EXT	0xB324

#endif /* _HCI_H_ */
