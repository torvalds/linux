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
#ifndef _HALMAC_RX_BD_NIC_H_
#define _HALMAC_RX_BD_NIC_H_

/*TXBD_DW0*/

#define GET_RX_BD_RXFAIL(__rx_bd) LE_BITS_TO_4BYTE(__rx_bd + 0x00, 31, 1)
#define GET_RX_BD_TOTALRXPKTSIZE(__rx_bd)                                      \
	LE_BITS_TO_4BYTE(__rx_bd + 0x00, 16, 13)
#define GET_RX_BD_RXTAG(__rx_bd) LE_BITS_TO_4BYTE(__rx_bd + 0x00, 16, 13)
#define GET_RX_BD_FS(__rx_bd) LE_BITS_TO_4BYTE(__rx_bd + 0x00, 15, 1)
#define GET_RX_BD_LS(__rx_bd) LE_BITS_TO_4BYTE(__rx_bd + 0x00, 14, 1)
#define GET_RX_BD_RXBUFFSIZE(__rx_bd) LE_BITS_TO_4BYTE(__rx_bd + 0x00, 0, 14)

/*TXBD_DW1*/

#define GET_RX_BD_PHYSICAL_ADDR_LOW(__rx_bd)                                   \
	LE_BITS_TO_4BYTE(__rx_bd + 0x04, 0, 32)

/*TXBD_DW2*/

#define GET_RX_BD_PHYSICAL_ADDR_HIGH(__rx_bd)                                  \
	LE_BITS_TO_4BYTE(__rx_bd + 0x08, 0, 32)

#endif
