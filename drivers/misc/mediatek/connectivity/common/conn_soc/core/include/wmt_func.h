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

#ifndef _WMT_FUNC_H_
#define _WMT_FUNC_H_

#include "osal_typedef.h"
#include "osal.h"
#include "wmt_core.h"
#include "wmt_plat.h"
/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

#if 1	/* defined(CONFIG_MTK_COMBO_HCI_DRIVER) || defined(CONFIG_MTK_COMBO_BT) */
#define CFG_FUNC_BT_SUPPORT 1
#else
#define CFG_FUNC_BT_SUPPORT 0
#endif

#if 1	/* defined(CONFIG_MTK_COMBO_FM) */
#define CFG_FUNC_FM_SUPPORT 1
#else
#define CFG_FUNC_FM_SUPPORT 0
#endif

#if 1	/* defined(CONFIG_MTK_COMBO_GPS) */
#define CFG_FUNC_GPS_SUPPORT 1
#else
#define CFG_FUNC_GPS_SUPPORT 0
#endif

#if 1	/* defined(CONFIG_MTK_COMBO_WIFI) */
#define CFG_FUNC_WIFI_SUPPORT 1
#else
#define CFG_FUNC_WIFI_SUPPORT 0
#endif

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

typedef INT32(*SUBSYS_FUNC_ON) (P_WMT_IC_OPS pOps, P_WMT_GEN_CONF pConf);
typedef INT32(*SUBSYS_FUNC_OFF) (P_WMT_IC_OPS pOps, P_WMT_GEN_CONF pConf);

typedef struct _WMT_FUNC_OPS_ {
	SUBSYS_FUNC_ON func_on;
	SUBSYS_FUNC_OFF func_off;
} WMT_FUNC_OPS, *P_WMT_FUNC_OPS;

typedef struct _CMB_PIN_CTRL_REG_ {
	UINT32 regAddr;
	UINT32 regValue;
	UINT32 regMask;

} CMB_PIN_CTRL_REG, *P_CMB_PIN_CTRL_REG;

typedef struct _CMB_PIN_CTRL_ {
	UINT32 pinId;
	UINT32 regNum;
	P_CMB_PIN_CTRL_REG pFuncOnArray;
	P_CMB_PIN_CTRL_REG pFuncOffArray;

} CMB_PIN_CTRL, *P_CMB_PIN_CTRL;

typedef enum _ENUM_CMP_PIN_ID_ {
	CMB_PIN_EEDI_ID = 0,
	CMB_PIN_EEDO_ID = 1,
	CMB_PIN_GSYNC_ID = 2,
} ENUM_CMP_PIN_ID, *P_ENUM_CMP_PIN_ID;

/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/
#if CFG_FUNC_BT_SUPPORT
extern WMT_FUNC_OPS wmt_func_bt_ops;
#endif

#if CFG_FUNC_FM_SUPPORT
extern WMT_FUNC_OPS wmt_func_fm_ops;
#endif

#if CFG_FUNC_GPS_SUPPORT
extern WMT_FUNC_OPS wmt_func_gps_ops;
#endif

#if CFG_FUNC_WIFI_SUPPORT
extern WMT_FUNC_OPS wmt_func_wifi_ops;
#endif
/*******************************************************************************
*                           P R I V A T E   D A T A
********************************************************************************
*/

/*******************************************************************************
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

#endif /* _WMT_FUNC_H_ */
