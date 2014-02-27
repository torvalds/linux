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
#ifndef __RTW_BTCOEX_H__
#define __RTW_BTCOEX_H__

#include <drv_types.h>


#define	PACKET_NORMAL			0
#define	PACKET_DHCP				1
#define	PACKET_ARP				2
#define	PACKET_EAPOL			3

void rtw_btcoex_Initialize(PADAPTER);
void rtw_btcoex_HAL_Initialize(PADAPTER padapter);
void rtw_btcoex_IpsNotify(PADAPTER, u8 type);
void rtw_btcoex_LpsNotify(PADAPTER, u8 type);
void rtw_btcoex_ScanNotify(PADAPTER, u8 type);
void rtw_btcoex_ConnectNotify(PADAPTER, u8 action);
void rtw_btcoex_MediaStatusNotify(PADAPTER, u8 mediaStatus);
void rtw_btcoex_SpecialPacketNotify(PADAPTER, u8 pktType);
void rtw_btcoex_BtInfoNotify(PADAPTER, u8 length, u8 *tmpBuf);
void rtw_btcoex_SuspendNotify(PADAPTER, u8 state);
void rtw_btcoex_HaltNotify(PADAPTER);
void rtw_btcoex_SwitchGntBt(PADAPTER);
void rtw_btcoex_Switch(PADAPTER, u8 enable);
u8 rtw_btcoex_IsBtDisabled(PADAPTER);
void rtw_btcoex_Handler(PADAPTER);
s32 rtw_btcoex_IsBTCoexCtrlAMPDUSize(PADAPTER);
u32 rtw_btcoex_GetAMPDUSize(PADAPTER);
void rtw_btcoex_SetManualControl(PADAPTER, u8 bmanual);
u8 rtw_btcoex_1Ant(PADAPTER);
u8 rtw_btcoex_IsBtControlLps(PADAPTER);
u8 rtw_btcoex_IsLpsOn(PADAPTER);
u8 rtw_btcoex_RpwmVal(PADAPTER);
u8 rtw_btcoex_LpsVal(PADAPTER);
void rtw_btcoex_SetBTCoexist(PADAPTER, u8 bBtExist);
void rtw_btcoex_SetChipType(PADAPTER, u8 chipType);
void rtw_btcoex_SetPGAntNum(PADAPTER, u8 antNum);
u8 rtw_btcoex_GetPGAntNum(PADAPTER);
u32 rtw_btcoex_GetRaMask(PADAPTER);
void rtw_btcoex_RecordPwrMode(PADAPTER, u8 *pCmdBuf, u8 cmdLen);
void rtw_btcoex_DisplayBtCoexInfo(PADAPTER, u8 *pbuf, u32 bufsize);
void rtw_btcoex_SetDBG(PADAPTER, u32 *pDbgModule);
u32 rtw_btcoex_GetDBG(PADAPTER, u8 *pStrBuf, u32 bufSize);
u8 rtw_btcoex_IncreaseScanDeviceNum(PADAPTER);
u8 rtw_btcoex_IsBtLinkExist(PADAPTER);

// ==================================================
// Below Functions are called by BT-Coex
// ==================================================
void rtw_btcoex_RejectApAggregatedPacket(PADAPTER, u8 enable);
void rtw_btcoex_LPS_Enter(PADAPTER);
void rtw_btcoex_LPS_Leave(PADAPTER);

#endif // __RTW_BTCOEX_H__

