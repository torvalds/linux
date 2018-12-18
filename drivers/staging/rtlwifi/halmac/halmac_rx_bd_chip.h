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
#ifndef _HALMAC_RX_BD_CHIP_H_
#define _HALMAC_RX_BD_CHIP_H_

/*TXBD_DW0*/

#define GET_RX_BD_RXFAIL_8822B(__rx_bd) GET_RX_BD_RXFAIL(__rx_bd)
#define GET_RX_BD_TOTALRXPKTSIZE_8822B(__rx_bd)                                \
	GET_RX_BD_TOTALRXPKTSIZE(__rx_bd)
#define GET_RX_BD_RXTAG_8822B(__rx_bd) GET_RX_BD_RXTAG(__rx_bd)
#define GET_RX_BD_FS_8822B(__rx_bd) GET_RX_BD_FS(__rx_bd)
#define GET_RX_BD_LS_8822B(__rx_bd) GET_RX_BD_LS(__rx_bd)
#define GET_RX_BD_RXBUFFSIZE_8822B(__rx_bd) GET_RX_BD_RXBUFFSIZE(__rx_bd)

/*TXBD_DW1*/

#define GET_RX_BD_PHYSICAL_ADDR_LOW_8822B(__rx_bd)                             \
	GET_RX_BD_PHYSICAL_ADDR_LOW(__rx_bd)

/*TXBD_DW2*/

#define GET_RX_BD_PHYSICAL_ADDR_HIGH_8822B(__rx_bd)                            \
	GET_RX_BD_PHYSICAL_ADDR_HIGH(__rx_bd)

#endif
