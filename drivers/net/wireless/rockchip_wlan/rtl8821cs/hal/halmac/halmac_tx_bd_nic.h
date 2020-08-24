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

#ifndef _HALMAC_TX_BD_NIC_H_
#define _HALMAC_TX_BD_NIC_H_
#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
	HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||\
	HALMAC_8192F_SUPPORT || HALMAC_8197G_SUPPORT || HALMAC_8814B_SUPPORT)

/*TXBD_DW0*/

#define SET_TX_BD_OWN(txbd, value)                                             \
	SET_BITS_TO_LE_4BYTE(txbd + 0x00, 31, 1, value)
#define GET_TX_BD_OWN(txbd) LE_BITS_TO_4BYTE(txbd + 0x00, 31, 1)
#define SET_TX_BD_PSB(txbd, value)                                             \
	SET_BITS_TO_LE_4BYTE(txbd + 0x00, 16, 8, value)
#define GET_TX_BD_PSB(txbd) LE_BITS_TO_4BYTE(txbd + 0x00, 16, 8)
#define SET_TX_BD_TX_BUFF_SIZE0(txbd, value)                                   \
	SET_BITS_TO_LE_4BYTE(txbd + 0x00, 0, 16, value)
#define GET_TX_BD_TX_BUFF_SIZE0(txbd) LE_BITS_TO_4BYTE(txbd + 0x00, 0, 16)

/*TXBD_DW1*/

#define SET_TX_BD_PHYSICAL_ADDR0_LOW(txbd, value)                              \
	SET_BITS_TO_LE_4BYTE(txbd + 0x04, 0, 32, value)
#define GET_TX_BD_PHYSICAL_ADDR0_LOW(txbd) LE_BITS_TO_4BYTE(txbd + 0x04, 0, 32)

/*TXBD_DW2*/

#define SET_TX_BD_PHYSICAL_ADDR0_HIGH(txbd, value)                             \
	SET_BITS_TO_LE_4BYTE(txbd + 0x08, 0, 32, value)
#define GET_TX_BD_PHYSICAL_ADDR0_HIGH(txbd) LE_BITS_TO_4BYTE(txbd + 0x08, 0, 32)

/*TXBD_DW4*/

#define SET_TX_BD_A1(txbd, value)                                              \
	SET_BITS_TO_LE_4BYTE(txbd + 0x10, 31, 1, value)
#define GET_TX_BD_A1(txbd) LE_BITS_TO_4BYTE(txbd + 0x10, 31, 1)
#define SET_TX_BD_TX_BUFF_SIZE1(txbd, value)                                   \
	SET_BITS_TO_LE_4BYTE(txbd + 0x10, 0, 16, value)
#define GET_TX_BD_TX_BUFF_SIZE1(txbd) LE_BITS_TO_4BYTE(txbd + 0x10, 0, 16)

/*TXBD_DW5*/

#define SET_TX_BD_PHYSICAL_ADDR1_LOW(txbd, value)                              \
	SET_BITS_TO_LE_4BYTE(txbd + 0x14, 0, 32, value)
#define GET_TX_BD_PHYSICAL_ADDR1_LOW(txbd) LE_BITS_TO_4BYTE(txbd + 0x14, 0, 32)

/*TXBD_DW6*/

#define SET_TX_BD_PHYSICAL_ADDR1_HIGH(txbd, value)                             \
	SET_BITS_TO_LE_4BYTE(txbd + 0x18, 0, 32, value)
#define GET_TX_BD_PHYSICAL_ADDR1_HIGH(txbd) LE_BITS_TO_4BYTE(txbd + 0x18, 0, 32)

/*TXBD_DW8*/

#define SET_TX_BD_A2(txbd, value)                                              \
	SET_BITS_TO_LE_4BYTE(txbd + 0x20, 31, 1, value)
#define GET_TX_BD_A2(txbd) LE_BITS_TO_4BYTE(txbd + 0x20, 31, 1)
#define SET_TX_BD_TX_BUFF_SIZE2(txbd, value)                                   \
	SET_BITS_TO_LE_4BYTE(txbd + 0x20, 0, 16, value)
#define GET_TX_BD_TX_BUFF_SIZE2(txbd) LE_BITS_TO_4BYTE(txbd + 0x20, 0, 16)

/*TXBD_DW9*/

#define SET_TX_BD_PHYSICAL_ADDR2_LOW(txbd, value)                              \
	SET_BITS_TO_LE_4BYTE(txbd + 0x24, 0, 32, value)
#define GET_TX_BD_PHYSICAL_ADDR2_LOW(txbd) LE_BITS_TO_4BYTE(txbd + 0x24, 0, 32)

/*TXBD_DW10*/

#define SET_TX_BD_PHYSICAL_ADDR2_HIGH(txbd, value)                             \
	SET_BITS_TO_LE_4BYTE(txbd + 0x28, 0, 32, value)
#define GET_TX_BD_PHYSICAL_ADDR2_HIGH(txbd) LE_BITS_TO_4BYTE(txbd + 0x28, 0, 32)

/*TXBD_DW12*/

#define SET_TX_BD_A3(txbd, value)                                              \
	SET_BITS_TO_LE_4BYTE(txbd + 0x30, 31, 1, value)
#define GET_TX_BD_A3(txbd) LE_BITS_TO_4BYTE(txbd + 0x30, 31, 1)
#define SET_TX_BD_TX_BUFF_SIZE3(txbd, value)                                   \
	SET_BITS_TO_LE_4BYTE(txbd + 0x30, 0, 16, value)
#define GET_TX_BD_TX_BUFF_SIZE3(txbd) LE_BITS_TO_4BYTE(txbd + 0x30, 0, 16)

/*TXBD_DW13*/

#define SET_TX_BD_PHYSICAL_ADDR3_LOW(txbd, value)                              \
	SET_BITS_TO_LE_4BYTE(txbd + 0x34, 0, 32, value)
#define GET_TX_BD_PHYSICAL_ADDR3_LOW(txbd) LE_BITS_TO_4BYTE(txbd + 0x34, 0, 32)

/*TXBD_DW14*/

#define SET_TX_BD_PHYSICAL_ADDR3_HIGH(txbd, value)                             \
	SET_BITS_TO_LE_4BYTE(txbd + 0x38, 0, 32, value)
#define GET_TX_BD_PHYSICAL_ADDR3_HIGH(txbd) LE_BITS_TO_4BYTE(txbd + 0x38, 0, 32)

#endif

#endif
