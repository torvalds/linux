/******************************************************************************
 *
 * Copyright(c) 2013 Realtek Corporation. All rights reserved.
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
#ifndef __HAL_BTCOEX_H__
#define __HAL_BTCOEX_H__

#include <drv_types.h>

// Some variables can't get from outsrc BT-Coex,
// so we need to save here
typedef struct _BT_COEXIST
{
	u8 bBtExist;
	u8 btTotalAntNum;
	u8 btChipType;
	u8 bInitlized;
} BT_COEXIST, *PBT_COEXIST;

void DBG_BT_INFO(u8 *dbgmsg);

void hal_btcoex_SetBTCoexist(PADAPTER padapter, u8 bBtExist);
u8 hal_btcoex_IsBtExist(PADAPTER padapter);
u8 hal_btcoex_IsBtDisabled(PADAPTER);
void hal_btcoex_SetChipType(PADAPTER padapter, u8 chipType);
u8 hal_btcoex_GetChipType(PADAPTER padapter);
void hal_btcoex_SetPgAntNum(PADAPTER padapter, u8 antNum);
u8 hal_btcoex_GetPgAntNum(PADAPTER padapter);
void hal_btcoex_SetSingleAntPath(PADAPTER padapter, u8 singleAntPath);

u8 hal_btcoex_Initialize(PADAPTER padapter);
void hal_btcoex_PowerOnSetting(PADAPTER padapter);
void hal_btcoex_InitHwConfig(PADAPTER padapter, u8 bWifiOnly);

void hal_btcoex_IpsNotify(PADAPTER padapter, u8 type);
void hal_btcoex_LpsNotify(PADAPTER padapter, u8 type);
void hal_btcoex_ScanNotify(PADAPTER padapter, u8 type);
void hal_btcoex_ConnectNotify(PADAPTER padapter, u8 action);
void hal_btcoex_MediaStatusNotify(PADAPTER padapter, u8 mediaStatus);
void hal_btcoex_SpecialPacketNotify(PADAPTER padapter, u8 pktType);
void hal_btcoex_IQKNotify(PADAPTER padapter, u8 state);
void hal_btcoex_BtInfoNotify(PADAPTER padapter, u8 length, u8 *tmpBuf);
void hal_btcoex_SuspendNotify(PADAPTER padapter, u8 state);
void hal_btcoex_HaltNotify(PADAPTER padapter);
void hal_btcoex_SwitchBtTRxMask(PADAPTER padapter);

void hal_btcoex_Hanlder(PADAPTER padapter);

s32 hal_btcoex_IsBTCoexCtrlAMPDUSize(PADAPTER padapter);
u32 hal_btcoex_GetAMPDUSize(PADAPTER padapter);
void hal_btcoex_SetManualControl(PADAPTER padapter, u8 bmanual);
u8 hal_btcoex_1Ant(PADAPTER padapter);
u8 hal_btcoex_IsBtControlLps(PADAPTER);
u8 hal_btcoex_IsLpsOn(PADAPTER);
u8 hal_btcoex_RpwmVal(PADAPTER);
u8 hal_btcoex_LpsVal(PADAPTER);
u32 hal_btcoex_GetRaMask(PADAPTER);
void hal_btcoex_RecordPwrMode(PADAPTER padapter, u8 *pCmdBuf, u8 cmdLen);
void hal_btcoex_DisplayBtCoexInfo(PADAPTER, u8 *pbuf, u32 bufsize);
void hal_btcoex_SetDBG(PADAPTER, u32 *pDbgModule);
u32 hal_btcoex_GetDBG(PADAPTER, u8 *pStrBuf, u32 bufSize);
u8 hal_btcoex_IncreaseScanDeviceNum(PADAPTER);
u8 hal_btcoex_IsBtLinkExist(PADAPTER);

#endif // !__HAL_BTCOEX_H__

