/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
 *****************************************************************************/
#ifndef __COMMON_C2H_H__
#define __COMMON_C2H_H__

#define C2H_TYPE_REG 0
#define C2H_TYPE_PKT 1

/* 
* C2H event format:
* Fields    TRIGGER    PAYLOAD    SEQ    PLEN    ID
* BITS     [127:120]    [119:16]   [15:8]  [7:4]  [3:0]
*/
#define C2H_ID(_c2h)		LE_BITS_TO_1BYTE(((u8*)(_c2h)), 0, 4)
#define C2H_PLEN(_c2h)		LE_BITS_TO_1BYTE(((u8*)(_c2h)), 4, 4)
#define C2H_SEQ(_c2h)		LE_BITS_TO_1BYTE(((u8*)(_c2h)) + 1, 0, 8)
#define C2H_PAYLOAD(_c2h)	(((u8*)(_c2h)) + 2)

#define SET_C2H_ID(_c2h, _val)		SET_BITS_TO_LE_1BYTE(((u8*)(_c2h)), 0, 4, _val)
#define SET_C2H_PLEN(_c2h, _val)	SET_BITS_TO_LE_1BYTE(((u8*)(_c2h)), 4, 4, _val)
#define SET_C2H_SEQ(_c2h, _val)		SET_BITS_TO_LE_1BYTE(((u8*)(_c2h)) + 1 , 0, 8, _val)

/* 
* C2H event format:
* Fields    TRIGGER     PLEN      PAYLOAD    SEQ      ID
* BITS    [127:120]  [119:112]  [111:16]   [15:8]   [7:0]
*/
#define C2H_ID_88XX(_c2h)		LE_BITS_TO_1BYTE(((u8*)(_c2h)), 0, 8)
#define C2H_SEQ_88XX(_c2h)		LE_BITS_TO_1BYTE(((u8*)(_c2h)) + 1, 0, 8)
#define C2H_PAYLOAD_88XX(_c2h)	(((u8*)(_c2h)) + 2)
#define C2H_PLEN_88XX(_c2h)		LE_BITS_TO_1BYTE(((u8*)(_c2h)) + 14, 0, 8)
#define C2H_TRIGGER_88XX(_c2h)	LE_BITS_TO_1BYTE(((u8*)(_c2h)) + 15, 0, 8)

#define SET_C2H_ID_88XX(_c2h, _val)		SET_BITS_TO_LE_1BYTE(((u8*)(_c2h)), 0, 8, _val)
#define SET_C2H_SEQ_88XX(_c2h, _val)	SET_BITS_TO_LE_1BYTE(((u8*)(_c2h)) + 1, 0, 8, _val)
#define SET_C2H_PLEN_88XX(_c2h, _val)	SET_BITS_TO_LE_1BYTE(((u8*)(_c2h)) + 14, 0, 8, _val)

typedef enum _C2H_EVT {
	C2H_DBG = 0x00,
	C2H_LB = 0x01,
	C2H_TXBF = 0x02,
	C2H_CCX_TX_RPT = 0x03,
	C2H_AP_REQ_TXRPT = 0x04,
	C2H_FW_SCAN_COMPLETE = 0x7,
	C2H_BT_INFO = 0x09,
	C2H_BT_MP_INFO = 0x0B,
	C2H_RA_RPT = 0x0C,
	C2H_SPC_STAT = 0x0D,
	C2H_RA_PARA_RPT = 0x0E,
	C2H_FW_CHNL_SWITCH_COMPLETE = 0x10,
	C2H_IQK_FINISH = 0x11,
	C2H_MAILBOX_STATUS = 0x15,
	C2H_P2P_RPORT = 0x16,
	C2H_MCC = 0x17,
	C2H_MAC_HIDDEN_RPT = 0x19,
	C2H_MAC_HIDDEN_RPT_2 = 0x1A,
	C2H_BCN_EARLY_RPT = 0x1E,
	C2H_DEFEATURE_DBG = 0x22,
	C2H_CUSTOMER_STR_RPT = 0x24,
	C2H_CUSTOMER_STR_RPT_2 = 0x25,
	C2H_WLAN_INFO = 0x27,
	C2H_DEFEATURE_RSVD = 0xFD,
	C2H_EXTEND = 0xff,
} C2H_EVT;

typedef enum _EXTEND_C2H_EVT {
	EXTEND_C2H_DBG_PRINT = 0
} EXTEND_C2H_EVT;

#define C2H_REG_LEN 16

/* C2H_IQK_FINISH, 0x11 */
#define IQK_OFFLOAD_LEN 1
void c2h_iqk_offload(_adapter *adapter, u8 *data, u8 len);
int	c2h_iqk_offload_wait(_adapter *adapter, u32 timeout_ms);
#define rtl8812_iqk_wait c2h_iqk_offload_wait /* TODO: remove this after phydm call c2h_iqk_offload_wait instead */

#ifdef CONFIG_RTW_MAC_HIDDEN_RPT
/* C2H_MAC_HIDDEN_RPT, 0x19 */
#define MAC_HIDDEN_RPT_LEN 8
int c2h_mac_hidden_rpt_hdl(_adapter *adapter, u8 *data, u8 len);

/* C2H_MAC_HIDDEN_RPT_2, 0x1A */
#define MAC_HIDDEN_RPT_2_LEN 5
int c2h_mac_hidden_rpt_2_hdl(_adapter *adapter, u8 *data, u8 len);
int hal_read_mac_hidden_rpt(_adapter *adapter);
#endif /* CONFIG_RTW_MAC_HIDDEN_RPT */

/* C2H_DEFEATURE_DBG, 0x22 */
#define DEFEATURE_DBG_LEN 1
int c2h_defeature_dbg_hdl(_adapter *adapter, u8 *data, u8 len);

#ifdef CONFIG_RTW_CUSTOMER_STR
/* C2H_CUSTOMER_STR_RPT, 0x24 */
#define CUSTOMER_STR_RPT_LEN 8
int c2h_customer_str_rpt_hdl(_adapter *adapter, u8 *data, u8 len);

/* C2H_CUSTOMER_STR_RPT_2, 0x25 */
#define CUSTOMER_STR_RPT_2_LEN 8
int c2h_customer_str_rpt_2_hdl(_adapter *adapter, u8 *data, u8 len);
#endif /* CONFIG_RTW_CUSTOMER_STR */

#endif /* __COMMON_C2H_H__ */
