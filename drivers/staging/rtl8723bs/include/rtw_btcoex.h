/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2013 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#ifndef __RTW_BTCOEX_H__
#define __RTW_BTCOEX_H__

#include <drv_types.h>


#define	PACKET_NORMAL			0
#define	PACKET_DHCP				1
#define	PACKET_ARP				2
#define	PACKET_EAPOL			3

void rtw_btcoex_PowerOnSetting(struct adapter *padapter);
void rtw_btcoex_HAL_Initialize(struct adapter *padapter, u8 bWifiOnly);
void rtw_btcoex_IpsNotify(struct adapter *, u8 type);
void rtw_btcoex_LpsNotify(struct adapter *, u8 type);
void rtw_btcoex_ScanNotify(struct adapter *, u8 type);
void rtw_btcoex_ConnectNotify(struct adapter *, u8 action);
void rtw_btcoex_MediaStatusNotify(struct adapter *, u8 mediaStatus);
void rtw_btcoex_SpecialPacketNotify(struct adapter *, u8 pktType);
void rtw_btcoex_IQKNotify(struct adapter *padapter, u8 state);
void rtw_btcoex_BtInfoNotify(struct adapter *, u8 length, u8 *tmpBuf);
void rtw_btcoex_SuspendNotify(struct adapter *, u8 state);
void rtw_btcoex_HaltNotify(struct adapter *);
u8 rtw_btcoex_IsBtDisabled(struct adapter *);
void rtw_btcoex_Handler(struct adapter *);
s32 rtw_btcoex_IsBTCoexCtrlAMPDUSize(struct adapter *);
void rtw_btcoex_SetManualControl(struct adapter *, u8 bmanual);
u8 rtw_btcoex_IsBtControlLps(struct adapter *);
u8 rtw_btcoex_IsLpsOn(struct adapter *);
u8 rtw_btcoex_RpwmVal(struct adapter *);
u8 rtw_btcoex_LpsVal(struct adapter *);
void rtw_btcoex_SetBTCoexist(struct adapter *, u8 bBtExist);
void rtw_btcoex_SetChipType(struct adapter *, u8 chipType);
void rtw_btcoex_SetPGAntNum(struct adapter *, u8 antNum);
void rtw_btcoex_SetSingleAntPath(struct adapter *padapter, u8 singleAntPath);
u32 rtw_btcoex_GetRaMask(struct adapter *);
void rtw_btcoex_RecordPwrMode(struct adapter *, u8 *pCmdBuf, u8 cmdLen);
void rtw_btcoex_DisplayBtCoexInfo(struct adapter *, u8 *pbuf, u32 bufsize);
void rtw_btcoex_SetDBG(struct adapter *, u32 *pDbgModule);
u32 rtw_btcoex_GetDBG(struct adapter *, u8 *pStrBuf, u32 bufSize);

/*  ================================================== */
/*  Below Functions are called by BT-Coex */
/*  ================================================== */
void rtw_btcoex_RejectApAggregatedPacket(struct adapter *, u8 enable);
void rtw_btcoex_LPS_Enter(struct adapter *);
void rtw_btcoex_LPS_Leave(struct adapter *);

#endif /*  __RTW_BTCOEX_H__ */
