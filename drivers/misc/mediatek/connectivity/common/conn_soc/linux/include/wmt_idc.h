/*
* Copyright (C) 2011-2014 MediaTek Inc.
*
* This program is free software: you can redistribute it and/or modify it under the terms of the
* GNU General Public License version 2 as published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
* without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
* See the GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License along with this program.
* If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef _WMT_IDC_H_
#define _WMT_IDC_H_

#include "osal_typedef.h"
#include "osal.h"
#include "wmt_stp_exp.h"

#if CFG_WMT_LTE_COEX_HANDLING

#include "wmt_stp_exp.h"
#include "conn_md_exp.h"

#define LTE_IDC_BUFFER_MAX_SIZE 1024
/*comment from firmware owner,max pckage num is 5,but should not happened*/
#define WMT_IDC_RX_MAX_LEN 384
#define LTE_MSG_ID_OFFSET 0x30

typedef enum {
	WMT_IDC_TX_OPCODE_MIN = 0,
	WMT_IDC_TX_OPCODE_LTE_PARA = 0x0a,
	WMT_IDC_TX_OPCODE_LTE_FREQ = 0x0b,
	WMT_IDC_TX_OPCODE_WIFI_MAX_POWER = 0x0c,
	WMT_IDC_TX_OPCODE_DEBUG_MONITOR = 0x0e,
	WMT_IDC_TX_OPCODE_SPLIT_FILTER = 0x0f,
	WMT_IDC_TX_OPCODE_LTE_CONNECTION_STAS = 0x16,
	WMT_IDC_TX_OPCODE_LTE_HW_IF_INDICATION = 0x17,
	WMT_IDC_TX_OPCODE_LTE_INDICATION = 0x20,
	WMT_IDC_TX_OPCODE_MAX
} WMT_IDC_TX_OPCODE;

typedef enum {
	WMT_IDC_RX_OPCODE_BTWF_DEF_PARA = 0x0,
	WMT_IDC_RX_OPCODE_BTWF_CHAN_RAN = 0x1,
	/* WMT_IDC_RX_OPCODE_TDM_REQ = 0x10, */
	WMT_IDC_RX_OPCODE_DEBUG_MONITOR = 0x02,
	WMT_IDC_RX_OPCODE_LTE_FREQ_IDX_TABLE = 0x03,
	WMT_IDC_RX_OPCODE_BTWF_PROFILE_IND = 0x04,
	WMT_IDC_RX_OPCODE_UART_PIN_SEL = 0x05,
	WMT_IDC_RX_OPCODE_MAX
} WMT_IDC_RX_OPCODE;

#if (CFG_WMT_LTE_ENABLE_MSGID_MAPPING == 0)
typedef enum {
	IPC_L4C_MSG_ID_INVALID = IPC_L4C_MSG_ID_BEGIN,
	IPC_L4C_MSG_ID_END,
	IPC_EL1_MSG_ID_INVALID = IPC_EL1_MSG_ID_BEGIN,
	/* below are EL1 IPC messages sent from AP */
	IPC_MSG_ID_EL1_LTE_TX_ALLOW_IND,
	IPC_MSG_ID_EL1_WIFIBT_OPER_DEFAULT_PARAM_IND,
	IPC_MSG_ID_EL1_WIFIBT_OPER_FREQ_IND,
	IPC_MSG_ID_EL1_WIFIBT_FREQ_IDX_TABLE_IND,
	IPC_MSG_ID_EL1_WIFIBT_PROFILE_IND,

	/* below are EL1 messages sent to AP */
	IPC_MSG_ID_EL1_LTE_DEFAULT_PARAM_IND,
	IPC_MSG_ID_EL1_LTE_OPER_FREQ_PARAM_IND,
	IPC_MSG_ID_EL1_WIFI_MAX_PWR_IND,
	IPC_MSG_ID_EL1_LTE_TX_IND,
	IPC_MSG_ID_EL1_LTE_CONNECTION_STATUS_IND,
	IPC_MSG_ID_EL1_PIN_TYPE_IND,
	IPC_MSG_ID_EL1_LTE_HW_INTERFACE_IND,
	IPC_MSG_ID_EL1_DUMMY13_IND,
	IPC_MSG_ID_EL1_DUMMY14_IND,
	IPC_MSG_ID_EL1_DUMMY15_IND,
	IPC_EL1_MSG_ID_END,
} IPC_MSG_ID_CODE;
#endif

typedef struct _MTK_WCN_WMT_IDC_INFO_ {
	ipc_ilm_t iit;
	CONN_MD_BRIDGE_OPS ops;
	UINT8 buffer[LTE_IDC_BUFFER_MAX_SIZE];
} MTK_WCN_WMT_IDC_INFO, *P_MTK_WCN_WMT_IDC_INFO;

extern INT32 wmt_idc_init(VOID);
extern INT32 wmt_idc_deinit(VOID);
extern INT32 wmt_idc_msg_from_lte_handing(ipc_ilm_t *ilm);
extern INT32 wmt_idc_msg_to_lte_handing(VOID);
extern UINT32 wmt_idc_msg_to_lte_handing_for_test(UINT8 *p_buf, UINT32 len);

#endif /* endif CFG_WMT_LTE_COEX_HANDLING */

#endif /* _WMT_IDC_H_ */
