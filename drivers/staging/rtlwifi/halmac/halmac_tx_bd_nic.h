/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2016  Realtek Corporation.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/
#ifndef _HALMAC_TX_BD_NIC_H_
#define _HALMAC_TX_BD_NIC_H_

/*TXBD_DW0*/

#define SET_TX_BD_OWN(__tx_bd, __value)                                        \
	SET_BITS_TO_LE_4BYTE(__tx_bd + 0x00, 31, 1, __value)
#define GET_TX_BD_OWN(__tx_bd) LE_BITS_TO_4BYTE(__tx_bd + 0x00, 31, 1)
#define SET_TX_BD_PSB(__tx_bd, __value)                                        \
	SET_BITS_TO_LE_4BYTE(__tx_bd + 0x00, 16, 8, __value)
#define GET_TX_BD_PSB(__tx_bd) LE_BITS_TO_4BYTE(__tx_bd + 0x00, 16, 8)
#define SET_TX_BD_TX_BUFF_SIZE0(__tx_bd, __value)                              \
	SET_BITS_TO_LE_4BYTE(__tx_bd + 0x00, 0, 16, __value)
#define GET_TX_BD_TX_BUFF_SIZE0(__tx_bd) LE_BITS_TO_4BYTE(__tx_bd + 0x00, 0, 16)

/*TXBD_DW1*/

#define SET_TX_BD_PHYSICAL_ADDR0_LOW(__tx_bd, __value)                         \
	SET_BITS_TO_LE_4BYTE(__tx_bd + 0x04, 0, 32, __value)
#define GET_TX_BD_PHYSICAL_ADDR0_LOW(__tx_bd)                                  \
	LE_BITS_TO_4BYTE(__tx_bd + 0x04, 0, 32)

/*TXBD_DW2*/

#define SET_TX_BD_PHYSICAL_ADDR0_HIGH(__tx_bd, __value)                        \
	SET_BITS_TO_LE_4BYTE(__tx_bd + 0x08, 0, 32, __value)
#define GET_TX_BD_PHYSICAL_ADDR0_HIGH(__tx_bd)                                 \
	LE_BITS_TO_4BYTE(__tx_bd + 0x08, 0, 32)

/*TXBD_DW4*/

#define SET_TX_BD_A1(__tx_bd, __value)                                         \
	SET_BITS_TO_LE_4BYTE(__tx_bd + 0x10, 31, 1, __value)
#define GET_TX_BD_A1(__tx_bd) LE_BITS_TO_4BYTE(__tx_bd + 0x10, 31, 1)
#define SET_TX_BD_TX_BUFF_SIZE1(__tx_bd, __value)                              \
	SET_BITS_TO_LE_4BYTE(__tx_bd + 0x10, 0, 16, __value)
#define GET_TX_BD_TX_BUFF_SIZE1(__tx_bd) LE_BITS_TO_4BYTE(__tx_bd + 0x10, 0, 16)

/*TXBD_DW5*/

#define SET_TX_BD_PHYSICAL_ADDR1_LOW(__tx_bd, __value)                         \
	SET_BITS_TO_LE_4BYTE(__tx_bd + 0x14, 0, 32, __value)
#define GET_TX_BD_PHYSICAL_ADDR1_LOW(__tx_bd)                                  \
	LE_BITS_TO_4BYTE(__tx_bd + 0x14, 0, 32)

/*TXBD_DW6*/

#define SET_TX_BD_PHYSICAL_ADDR1_HIGH(__tx_bd, __value)                        \
	SET_BITS_TO_LE_4BYTE(__tx_bd + 0x18, 0, 32, __value)
#define GET_TX_BD_PHYSICAL_ADDR1_HIGH(__tx_bd)                                 \
	LE_BITS_TO_4BYTE(__tx_bd + 0x18, 0, 32)

/*TXBD_DW8*/

#define SET_TX_BD_A2(__tx_bd, __value)                                         \
	SET_BITS_TO_LE_4BYTE(__tx_bd + 0x20, 31, 1, __value)
#define GET_TX_BD_A2(__tx_bd) LE_BITS_TO_4BYTE(__tx_bd + 0x20, 31, 1)
#define SET_TX_BD_TX_BUFF_SIZE2(__tx_bd, __value)                              \
	SET_BITS_TO_LE_4BYTE(__tx_bd + 0x20, 0, 16, __value)
#define GET_TX_BD_TX_BUFF_SIZE2(__tx_bd) LE_BITS_TO_4BYTE(__tx_bd + 0x20, 0, 16)

/*TXBD_DW9*/

#define SET_TX_BD_PHYSICAL_ADDR2_LOW(__tx_bd, __value)                         \
	SET_BITS_TO_LE_4BYTE(__tx_bd + 0x24, 0, 32, __value)
#define GET_TX_BD_PHYSICAL_ADDR2_LOW(__tx_bd)                                  \
	LE_BITS_TO_4BYTE(__tx_bd + 0x24, 0, 32)

/*TXBD_DW10*/

#define SET_TX_BD_PHYSICAL_ADDR2_HIGH(__tx_bd, __value)                        \
	SET_BITS_TO_LE_4BYTE(__tx_bd + 0x28, 0, 32, __value)
#define GET_TX_BD_PHYSICAL_ADDR2_HIGH(__tx_bd)                                 \
	LE_BITS_TO_4BYTE(__tx_bd + 0x28, 0, 32)

/*TXBD_DW12*/

#define SET_TX_BD_A3(__tx_bd, __value)                                         \
	SET_BITS_TO_LE_4BYTE(__tx_bd + 0x30, 31, 1, __value)
#define GET_TX_BD_A3(__tx_bd) LE_BITS_TO_4BYTE(__tx_bd + 0x30, 31, 1)
#define SET_TX_BD_TX_BUFF_SIZE3(__tx_bd, __value)                              \
	SET_BITS_TO_LE_4BYTE(__tx_bd + 0x30, 0, 16, __value)
#define GET_TX_BD_TX_BUFF_SIZE3(__tx_bd) LE_BITS_TO_4BYTE(__tx_bd + 0x30, 0, 16)

/*TXBD_DW13*/

#define SET_TX_BD_PHYSICAL_ADDR3_LOW(__tx_bd, __value)                         \
	SET_BITS_TO_LE_4BYTE(__tx_bd + 0x34, 0, 32, __value)
#define GET_TX_BD_PHYSICAL_ADDR3_LOW(__tx_bd)                                  \
	LE_BITS_TO_4BYTE(__tx_bd + 0x34, 0, 32)

/*TXBD_DW14*/

#define SET_TX_BD_PHYSICAL_ADDR3_HIGH(__tx_bd, __value)                        \
	SET_BITS_TO_LE_4BYTE(__tx_bd + 0x38, 0, 32, __value)
#define GET_TX_BD_PHYSICAL_ADDR3_HIGH(__tx_bd)                                 \
	LE_BITS_TO_4BYTE(__tx_bd + 0x38, 0, 32)

#endif
