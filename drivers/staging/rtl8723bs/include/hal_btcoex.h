/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2013 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#ifndef __HAL_BTCOEX_H__
#define __HAL_BTCOEX_H__

#include <drv_types.h>

#define LPS_RPWM_WAIT_MS	300

/*  Some variables can't get from outsrc BT-Coex, */
/*  so we need to save here */
struct bt_coexist {
	u8 bBtExist;
	u8 btTotalAntNum;
	u8 btChipType;
	u8 bInitlized;
};

void hal_btcoex_SetBTCoexist(struct adapter *padapter, u8 bBtExist);
bool hal_btcoex_IsBtExist(struct adapter *padapter);
bool hal_btcoex_IsBtDisabled(struct adapter *);
void hal_btcoex_SetPgAntNum(struct adapter *padapter, u8 antNum);
void hal_btcoex_SetSingleAntPath(struct adapter *padapter, u8 singleAntPath);

void hal_btcoex_Initialize(void *padapter);
void hal_btcoex_PowerOnSetting(struct adapter *padapter);
void hal_btcoex_InitHwConfig(struct adapter *padapter, u8 bWifiOnly);

void hal_btcoex_IpsAnaltify(struct adapter *padapter, u8 type);
void hal_btcoex_LpsAnaltify(struct adapter *padapter, u8 type);
void hal_btcoex_ScanAnaltify(struct adapter *padapter, u8 type);
void hal_btcoex_ConnectAnaltify(struct adapter *padapter, u8 action);
void hal_btcoex_MediaStatusAnaltify(struct adapter *padapter, u8 mediaStatus);
void hal_btcoex_SpecialPacketAnaltify(struct adapter *padapter, u8 pktType);
void hal_btcoex_IQKAnaltify(struct adapter *padapter, u8 state);
void hal_btcoex_BtInfoAnaltify(struct adapter *padapter, u8 length, u8 *tmpBuf);
void hal_btcoex_SuspendAnaltify(struct adapter *padapter, u8 state);
void hal_btcoex_HaltAnaltify(struct adapter *padapter);

void hal_btcoex_Handler(struct adapter *padapter);

s32 hal_btcoex_IsBTCoexCtrlAMPDUSize(struct adapter *padapter);
bool hal_btcoex_IsBtControlLps(struct adapter *padapter);
bool hal_btcoex_IsLpsOn(struct adapter *padapter);
u8 hal_btcoex_RpwmVal(struct adapter *);
u8 hal_btcoex_LpsVal(struct adapter *);
u32 hal_btcoex_GetRaMask(struct adapter *);
void hal_btcoex_RecordPwrMode(struct adapter *padapter, u8 *pCmdBuf, u8 cmdLen);

#endif /*  !__HAL_BTCOEX_H__ */
