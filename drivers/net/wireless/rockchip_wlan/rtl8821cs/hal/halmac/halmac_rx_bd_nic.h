/* SPDX-License-Identifier: GPL-2.0 */
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

#ifndef _HALMAC_RX_BD_NIC_H_
#define _HALMAC_RX_BD_NIC_H_
#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
	HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||\
	HALMAC_8192F_SUPPORT || HALMAC_8197G_SUPPORT || HALMAC_8814B_SUPPORT)

/*TXBD_DW0*/

#define GET_RX_BD_RXFAIL(rxbd) LE_BITS_TO_4BYTE(rxbd + 0x00, 31, 1)
#define GET_RX_BD_TOTALRXPKTSIZE(rxbd) LE_BITS_TO_4BYTE(rxbd + 0x00, 16, 13)
#define GET_RX_BD_RXTAG(rxbd) LE_BITS_TO_4BYTE(rxbd + 0x00, 16, 13)
#define GET_RX_BD_FS(rxbd) LE_BITS_TO_4BYTE(rxbd + 0x00, 15, 1)
#define GET_RX_BD_LS(rxbd) LE_BITS_TO_4BYTE(rxbd + 0x00, 14, 1)
#define GET_RX_BD_RXBUFFSIZE(rxbd) LE_BITS_TO_4BYTE(rxbd + 0x00, 0, 14)

/*TXBD_DW1*/

#define GET_RX_BD_PHYSICAL_ADDR_LOW(rxbd) LE_BITS_TO_4BYTE(rxbd + 0x04, 0, 32)

/*TXBD_DW2*/

#define GET_RX_BD_PHYSICAL_ADDR_HIGH(rxbd) LE_BITS_TO_4BYTE(rxbd + 0x08, 0, 32)

#endif

#endif
