/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/

#ifndef	__ODMDYNAMICTXPOWER_H__
#define    __ODMDYNAMICTXPOWER_H__

#define		TX_POWER_NEAR_FIELD_THRESH_LVL2	74
#define		TX_POWER_NEAR_FIELD_THRESH_LVL1	67
#define		TX_POWER_NEAR_FIELD_THRESH_AP		0x3F
#define		TX_POWER_NEAR_FIELD_THRESH_8812	60

#define		TxHighPwrLevel_Normal		0
#define		TxHighPwrLevel_Level1		1
#define		TxHighPwrLevel_Level2		2
#define		TxHighPwrLevel_BT1			3
#define		TxHighPwrLevel_BT2			4
#define		TxHighPwrLevel_15			5
#define		TxHighPwrLevel_35			6
#define		TxHighPwrLevel_50			7
#define		TxHighPwrLevel_70			8
#define		TxHighPwrLevel_100			9

void odm_DynamicTxPowerInit(void *pDM_VOID);

#endif
