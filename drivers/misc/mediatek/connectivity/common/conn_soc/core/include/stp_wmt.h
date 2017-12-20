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

#ifndef _STP_WMT_H
#define _STP_WMT_H

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

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

typedef enum {
	BTM_RST_OP = 0,
	BTM_DMP_OP = 1,
	BTM_GET_AEE_SUPPORT_FLAG = 2,
	BTM_MAX_OP,
} MTKSTP_BTM_WMT_OP_T;

typedef enum {
	SLEEP = 0,
	HOST_AWAKE,
	WAKEUP,
	EIRQ,
	ROLL_BACK,
	STP_PSM_MAX_ACTION
} MTKSTP_PSM_ACTION_T;

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
extern MTK_WCN_BOOL wmt_lib_btm_cb(MTKSTP_BTM_WMT_OP_T op);

extern INT32 wmt_lib_ps_stp_cb(MTKSTP_PSM_ACTION_T action);
extern MTK_WCN_BOOL wmt_lib_is_quick_ps_support(VOID);

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

#endif /* _STP_WMT_H_ */
