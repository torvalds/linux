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

/*! \file
    \brief  Declaration of library functions

    Any definitions in this file will be shared among GLUE Layer and internal Driver Stack.
*/

#ifndef _BTM_CORE_H
#define _BTM_CORE_H

#include "osal_typedef.h"
#include "osal.h"
#include "stp_wmt.h"
#include "wmt_plat.h"
#include "wmt_idc.h"
#include "mtk_btif_exp.h"

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/
#define STP_BTM_OPERATION_FAIL    (-1)
#define STP_BTM_OPERATION_SUCCESS (0)

#define STP_BTM_OP_BUF_SIZE (64)

#define BTM_THREAD_NAME "mtk_stp_btm"

#define STP_PAGED_DUMP_TIME_LIMIT 3500
#define STP_FULL_DUMP_TIME 3
/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/
typedef enum _ENUM_STP_BTM_OPID_T {
	STP_OPID_BTM_RETRY = 0x0,
	STP_OPID_BTM_RST = 0x1,
	STP_OPID_BTM_DBG_DUMP = 0x2,
	STP_OPID_BTM_DUMP_TIMEOUT = 0x3,
	STP_OPID_BTM_POLL_CPUPCR = 0x4,
	STP_OPID_BTM_PAGED_DUMP = 0x5,
	STP_OPID_BTM_FULL_DUMP = 0x6,
	STP_OPID_BTM_PAGED_TRACE = 0x7,
	STP_OPID_BTM_FORCE_FW_ASSERT = 0x8,
#if CFG_WMT_LTE_COEX_HANDLING
	STP_OPID_BTM_WMT_LTE_COEX = 0x9,
#endif
	STP_OPID_BTM_EXIT,
	STP_OPID_BTM_NUM
} ENUM_STP_BTM_OPID_T, *P_ENUM_STP_BTM_OPID_T;

typedef OSAL_OP_DAT STP_BTM_OP;
typedef P_OSAL_OP_DAT P_STP_BTM_OP;

typedef struct mtk_stp_btm {
	OSAL_THREAD BTMd;	/* main thread (wmtd) handle */
	OSAL_EVENT STPd_event;
	OSAL_UNSLEEPABLE_LOCK wq_spinlock;

	OSAL_OP_Q rFreeOpQ;	/* free op queue */
	OSAL_OP_Q rActiveOpQ;	/* active op queue */
	OSAL_OP arQue[STP_BTM_OP_BUF_SIZE];	/* real op instances */

	/*wmt_notify */
	INT32 (*wmt_notify)(MTKSTP_BTM_WMT_OP_T);
} MTKSTP_BTM_T;
/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/

/*******************************************************************************
*                           P R I V A T E   D A T A
********************************************************************************
*/

/*******************************************************************************
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

INT32 stp_btm_notify_wmt_rst_wq(MTKSTP_BTM_T *stp_btm);
INT32 stp_btm_notify_stp_retry_wq(MTKSTP_BTM_T *stp_btm);
INT32 stp_btm_notify_coredump_timeout_wq(MTKSTP_BTM_T *stp_btm);
INT32 stp_btm_notify_wmt_dmp_wq(MTKSTP_BTM_T *stp_btm);
INT32 stp_btm_deinit(MTKSTP_BTM_T *stp_btm);
INT32 stp_btm_reset_btm_wq(MTKSTP_BTM_T *stp_btm);
INT32 stp_notify_btm_poll_cpupcr(MTKSTP_BTM_T *stp_btm, UINT32 times, UINT32 sleep);
INT32 stp_notify_btm_poll_cpupcr_ctrl(UINT32 en);
INT32 stp_btm_notify_wmt_trace_wq(MTKSTP_BTM_T *stp_btm);
INT32 stp_notify_btm_do_fw_assert_via_emi(MTKSTP_BTM_T *stp_btm);
INT32 stp_notify_btm_handle_wmt_lte_coex(MTKSTP_BTM_T *stp_btm);
INT32 wcn_psm_flag_trigger_collect_ftrace(void);
#if BTIF_RXD_BE_BLOCKED_DETECT
INT32 wcn_btif_rxd_blocked_collect_ftrace(void);
MTK_WCN_BOOL is_btif_rxd_be_blocked(void);
#endif
MTKSTP_BTM_T *stp_btm_init(void);
extern unsigned int g_coredump_mode;
/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

#endif
