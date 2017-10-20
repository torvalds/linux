/******************************************************************************
 *
 * Copyright(c) 2016  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/
#ifndef _HALMAC_TX_BD_CHIP_H_
#define _HALMAC_TX_BD_CHIP_H_

/*TXBD_DW0*/

#define SET_TX_BD_OWN_8822B(__tx_bd, __value) SET_TX_BD_OWN(__tx_bd, __value)
#define GET_TX_BD_OWN_8822B(__tx_bd) GET_TX_BD_OWN(__tx_bd)
#define SET_TX_BD_PSB_8822B(__tx_bd, __value) SET_TX_BD_PSB(__tx_bd, __value)
#define GET_TX_BD_PSB_8822B(__tx_bd) GET_TX_BD_PSB(__tx_bd)
#define SET_TX_BD_TX_BUFF_SIZE0_8822B(__tx_bd, __value)                        \
	SET_TX_BD_TX_BUFF_SIZE0(__tx_bd, __value)
#define GET_TX_BD_TX_BUFF_SIZE0_8822B(__tx_bd) GET_TX_BD_TX_BUFF_SIZE0(__tx_bd)

/*TXBD_DW1*/

#define SET_TX_BD_PHYSICAL_ADDR0_LOW_8822B(__tx_bd, __value)                   \
	SET_TX_BD_PHYSICAL_ADDR0_LOW(__tx_bd, __value)
#define GET_TX_BD_PHYSICAL_ADDR0_LOW_8822B(__tx_bd)                            \
	GET_TX_BD_PHYSICAL_ADDR0_LOW(__tx_bd)

/*TXBD_DW2*/

#define SET_TX_BD_PHYSICAL_ADDR0_HIGH_8822B(__tx_bd, __value)                  \
	SET_TX_BD_PHYSICAL_ADDR0_HIGH(__tx_bd, __value)
#define GET_TX_BD_PHYSICAL_ADDR0_HIGH_8822B(__tx_bd)                           \
	GET_TX_BD_PHYSICAL_ADDR0_HIGH(__tx_bd)

/*TXBD_DW4*/

#define SET_TX_BD_A1_8822B(__tx_bd, __value) SET_TX_BD_A1(__tx_bd, __value)
#define GET_TX_BD_A1_8822B(__tx_bd) GET_TX_BD_A1(__tx_bd)
#define SET_TX_BD_TX_BUFF_SIZE1_8822B(__tx_bd, __value)                        \
	SET_TX_BD_TX_BUFF_SIZE1(__tx_bd, __value)
#define GET_TX_BD_TX_BUFF_SIZE1_8822B(__tx_bd) GET_TX_BD_TX_BUFF_SIZE1(__tx_bd)

/*TXBD_DW5*/

#define SET_TX_BD_PHYSICAL_ADDR1_LOW_8822B(__tx_bd, __value)                   \
	SET_TX_BD_PHYSICAL_ADDR1_LOW(__tx_bd, __value)
#define GET_TX_BD_PHYSICAL_ADDR1_LOW_8822B(__tx_bd)                            \
	GET_TX_BD_PHYSICAL_ADDR1_LOW(__tx_bd)

/*TXBD_DW6*/

#define SET_TX_BD_PHYSICAL_ADDR1_HIGH_8822B(__tx_bd, __value)                  \
	SET_TX_BD_PHYSICAL_ADDR1_HIGH(__tx_bd, __value)
#define GET_TX_BD_PHYSICAL_ADDR1_HIGH_8822B(__tx_bd)                           \
	GET_TX_BD_PHYSICAL_ADDR1_HIGH(__tx_bd)

/*TXBD_DW8*/

#define SET_TX_BD_A2_8822B(__tx_bd, __value) SET_TX_BD_A2(__tx_bd, __value)
#define GET_TX_BD_A2_8822B(__tx_bd) GET_TX_BD_A2(__tx_bd)
#define SET_TX_BD_TX_BUFF_SIZE2_8822B(__tx_bd, __value)                        \
	SET_TX_BD_TX_BUFF_SIZE2(__tx_bd, __value)
#define GET_TX_BD_TX_BUFF_SIZE2_8822B(__tx_bd) GET_TX_BD_TX_BUFF_SIZE2(__tx_bd)

/*TXBD_DW9*/

#define SET_TX_BD_PHYSICAL_ADDR2_LOW_8822B(__tx_bd, __value)                   \
	SET_TX_BD_PHYSICAL_ADDR2_LOW(__tx_bd, __value)
#define GET_TX_BD_PHYSICAL_ADDR2_LOW_8822B(__tx_bd)                            \
	GET_TX_BD_PHYSICAL_ADDR2_LOW(__tx_bd)

/*TXBD_DW10*/

#define SET_TX_BD_PHYSICAL_ADDR2_HIGH_8822B(__tx_bd, __value)                  \
	SET_TX_BD_PHYSICAL_ADDR2_HIGH(__tx_bd, __value)
#define GET_TX_BD_PHYSICAL_ADDR2_HIGH_8822B(__tx_bd)                           \
	GET_TX_BD_PHYSICAL_ADDR2_HIGH(__tx_bd)

/*TXBD_DW12*/

#define SET_TX_BD_A3_8822B(__tx_bd, __value) SET_TX_BD_A3(__tx_bd, __value)
#define GET_TX_BD_A3_8822B(__tx_bd) GET_TX_BD_A3(__tx_bd)
#define SET_TX_BD_TX_BUFF_SIZE3_8822B(__tx_bd, __value)                        \
	SET_TX_BD_TX_BUFF_SIZE3(__tx_bd, __value)
#define GET_TX_BD_TX_BUFF_SIZE3_8822B(__tx_bd) GET_TX_BD_TX_BUFF_SIZE3(__tx_bd)

/*TXBD_DW13*/

#define SET_TX_BD_PHYSICAL_ADDR3_LOW_8822B(__tx_bd, __value)                   \
	SET_TX_BD_PHYSICAL_ADDR3_LOW(__tx_bd, __value)
#define GET_TX_BD_PHYSICAL_ADDR3_LOW_8822B(__tx_bd)                            \
	GET_TX_BD_PHYSICAL_ADDR3_LOW(__tx_bd)

/*TXBD_DW14*/

#define SET_TX_BD_PHYSICAL_ADDR3_HIGH_8822B(__tx_bd, __value)                  \
	SET_TX_BD_PHYSICAL_ADDR3_HIGH(__tx_bd, __value)
#define GET_TX_BD_PHYSICAL_ADDR3_HIGH_8822B(__tx_bd)                           \
	GET_TX_BD_PHYSICAL_ADDR3_HIGH(__tx_bd)

#endif
