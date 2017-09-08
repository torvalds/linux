/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#ifndef __COMMON_C2H_H__
#define __COMMON_C2H_H__

typedef enum _C2H_EVT {
	C2H_DBG = 0x00,
	C2H_LB = 0x01,
	C2H_TXBF = 0x02,
	C2H_CCX_TX_RPT = 0x03,
	C2H_BT_INFO = 0x09,
	C2H_BT_MP_INFO = 0x0B,
	C2H_RA_RPT = 0x0C,
	C2H_RA_PARA_RPT = 0x0E,
	C2H_FW_CHNL_SWITCH_COMPLETE = 0x10,
	C2H_IQK_FINISH = 0x11,
	C2H_MAILBOX_STATUS = 0x15,
	C2H_P2P_RPORT = 0x16,
	C2H_MAC_HIDDEN_RPT = 0x19,
	C2H_BCN_EARLY_RPT = 0x1E,
	C2H_BT_SCOREBOARD_STATUS = 0x20,
	C2H_EXTEND = 0xff,
} C2H_EVT;

typedef enum _EXTEND_C2H_EVT {
	EXTEND_C2H_DBG_PRINT = 0
} EXTEND_C2H_EVT;

#define MAC_HIDDEN_RPT_LEN 8
int c2h_mac_hidden_rpt_hdl(_adapter *adapter, u8 *data, u8 len);
int hal_read_mac_hidden_rpt(_adapter *adapter);

#endif /* __COMMON_C2H_H__ */

