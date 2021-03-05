/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
/*  The following is for 8723B 2Ant BT Co-exist definition */
#define	BT_INFO_8723B_2ANT_B_FTP		BIT7
#define	BT_INFO_8723B_2ANT_B_A2DP		BIT6
#define	BT_INFO_8723B_2ANT_B_HID		BIT5
#define	BT_INFO_8723B_2ANT_B_SCO_BUSY		BIT4
#define	BT_INFO_8723B_2ANT_B_ACL_BUSY		BIT3
#define	BT_INFO_8723B_2ANT_B_INQ_PAGE		BIT2
#define	BT_INFO_8723B_2ANT_B_SCO_ESCO		BIT1
#define	BT_INFO_8723B_2ANT_B_CONNECTION		BIT0

#define		BTC_RSSI_COEX_THRESH_TOL_8723B_2ANT		2

enum BT_INFO_SRC_8723B_2ANT {
	BT_INFO_SRC_8723B_2ANT_WIFI_FW        = 0x0,
	BT_INFO_SRC_8723B_2ANT_BT_RSP         = 0x1,
	BT_INFO_SRC_8723B_2ANT_BT_ACTIVE_SEND = 0x2,
	BT_INFO_SRC_8723B_2ANT_MAX
};

enum BT_8723B_2ANT_BT_STATUS {
	BT_8723B_2ANT_BT_STATUS_NON_CONNECTED_IDLE = 0x0,
	BT_8723B_2ANT_BT_STATUS_CONNECTED_IDLE     = 0x1,
	BT_8723B_2ANT_BT_STATUS_INQ_PAGE           = 0x2,
	BT_8723B_2ANT_BT_STATUS_ACL_BUSY           = 0x3,
	BT_8723B_2ANT_BT_STATUS_SCO_BUSY           = 0x4,
	BT_8723B_2ANT_BT_STATUS_ACL_SCO_BUSY       = 0x5,
	BT_8723B_2ANT_BT_STATUS_MAX
};

enum BT_8723B_2ANT_COEX_ALGO {
	BT_8723B_2ANT_COEX_ALGO_UNDEFINED       = 0x0,
	BT_8723B_2ANT_COEX_ALGO_SCO             = 0x1,
	BT_8723B_2ANT_COEX_ALGO_HID             = 0x2,
	BT_8723B_2ANT_COEX_ALGO_A2DP            = 0x3,
	BT_8723B_2ANT_COEX_ALGO_A2DP_PANHS      = 0x4,
	BT_8723B_2ANT_COEX_ALGO_PANEDR          = 0x5,
	BT_8723B_2ANT_COEX_ALGO_PANHS           = 0x6,
	BT_8723B_2ANT_COEX_ALGO_PANEDR_A2DP     = 0x7,
	BT_8723B_2ANT_COEX_ALGO_PANEDR_HID      = 0x8,
	BT_8723B_2ANT_COEX_ALGO_HID_A2DP_PANEDR	= 0x9,
	BT_8723B_2ANT_COEX_ALGO_HID_A2DP        = 0xa,
	BT_8723B_2ANT_COEX_ALGO_MAX             = 0xb,
};

struct COEX_DM_8723B_2ANT {
	/*  fw mechanism */
	u8 preBtDecPwrLvl;
	u8 curBtDecPwrLvl;
	u8 preFwDacSwingLvl;
	u8 curFwDacSwingLvl;
	bool bCurIgnoreWlanAct;
	bool bPreIgnoreWlanAct;
	u8 prePsTdma;
	u8 curPsTdma;
	u8 psTdmaPara[5];
	u8 psTdmaDuAdjType;
	bool bResetTdmaAdjust;
	bool bAutoTdmaAdjust;
	bool bPrePsTdmaOn;
	bool bCurPsTdmaOn;
	bool bPreBtAutoReport;
	bool bCurBtAutoReport;

	/*  sw mechanism */
	bool bPreRfRxLpfShrink;
	bool bCurRfRxLpfShrink;
	u32 btRf0x1eBackup;
	bool bPreLowPenaltyRa;
	bool bCurLowPenaltyRa;
	bool bPreDacSwingOn;
	u32  preDacSwingLvl;
	bool bCurDacSwingOn;
	u32  curDacSwingLvl;
	bool bPreAdcBackOff;
	bool bCurAdcBackOff;
	bool bPreAgcTableEn;
	bool bCurAgcTableEn;
	u32 preVal0x6c0;
	u32 curVal0x6c0;
	u32 preVal0x6c4;
	u32 curVal0x6c4;
	u32 preVal0x6c8;
	u32 curVal0x6c8;
	u8 preVal0x6cc;
	u8 curVal0x6cc;
	bool bLimitedDig;

	/*  algorithm related */
	u8 preAlgorithm;
	u8 curAlgorithm;
	u8 btStatus;
	u8 wifiChnlInfo[3];

	bool bNeedRecover0x948;
	u32 backup0x948;
};

struct COEX_STA_8723B_2ANT {
	bool bBtLinkExist;
	bool bScoExist;
	bool bA2dpExist;
	bool bHidExist;
	bool bPanExist;

	bool bUnderLps;
	bool bUnderIps;
	u32 highPriorityTx;
	u32 highPriorityRx;
	u32 lowPriorityTx;
	u32 lowPriorityRx;
	u8 btRssi;
	bool bBtTxRxMask;
	u8 preBtRssiState;
	u8 preWifiRssiState[4];
	bool bC2hBtInfoReqSent;
	u8 btInfoC2h[BT_INFO_SRC_8723B_2ANT_MAX][10];
	u32 btInfoC2hCnt[BT_INFO_SRC_8723B_2ANT_MAX];
	bool bC2hBtInquiryPage;
	u8 btRetryCnt;
	u8 btInfoExt;
};

/*  */
/*  The following is interface which will notify coex module. */
/*  */
void EXhalbtc8723b2ant_PowerOnSetting(PBTC_COEXIST pBtCoexist);
void EXhalbtc8723b2ant_InitHwConfig(PBTC_COEXIST pBtCoexist, bool bWifiOnly);
void EXhalbtc8723b2ant_InitCoexDm(PBTC_COEXIST pBtCoexist);
void EXhalbtc8723b2ant_IpsNotify(PBTC_COEXIST pBtCoexist, u8 type);
void EXhalbtc8723b2ant_LpsNotify(PBTC_COEXIST pBtCoexist, u8 type);
void EXhalbtc8723b2ant_ScanNotify(PBTC_COEXIST pBtCoexist, u8 type);
void EXhalbtc8723b2ant_ConnectNotify(PBTC_COEXIST pBtCoexist, u8 type);
void EXhalbtc8723b2ant_MediaStatusNotify(PBTC_COEXIST pBtCoexist, u8 type);
void EXhalbtc8723b2ant_SpecialPacketNotify(PBTC_COEXIST pBtCoexist, u8 type);
void EXhalbtc8723b2ant_BtInfoNotify(
	PBTC_COEXIST pBtCoexist, u8 *tmpBuf, u8 length
);
void EXhalbtc8723b2ant_HaltNotify(PBTC_COEXIST pBtCoexist);
void EXhalbtc8723b2ant_PnpNotify(PBTC_COEXIST pBtCoexist, u8 pnpState);
void EXhalbtc8723b2ant_Periodical(PBTC_COEXIST pBtCoexist);
void EXhalbtc8723b2ant_DisplayCoexInfo(PBTC_COEXIST pBtCoexist);
