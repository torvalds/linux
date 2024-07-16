/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
/*  The following is for 8723B 1ANT BT Co-exist definition */
#define	BT_INFO_8723B_1ANT_B_FTP		BIT7
#define	BT_INFO_8723B_1ANT_B_A2DP		BIT6
#define	BT_INFO_8723B_1ANT_B_HID		BIT5
#define	BT_INFO_8723B_1ANT_B_SCO_BUSY		BIT4
#define	BT_INFO_8723B_1ANT_B_ACL_BUSY		BIT3
#define	BT_INFO_8723B_1ANT_B_INQ_PAGE		BIT2
#define	BT_INFO_8723B_1ANT_B_SCO_ESCO		BIT1
#define	BT_INFO_8723B_1ANT_B_CONNECTION		BIT0

#define	BT_INFO_8723B_1ANT_A2DP_BASIC_RATE(_BT_INFO_EXT_)	\
		(((_BT_INFO_EXT_ & BIT0)) ? true : false)

#define	BTC_RSSI_COEX_THRESH_TOL_8723B_1ANT 2

#define  BT_8723B_1ANT_WIFI_NOISY_THRESH 30   /* max: 255 */

enum {
	BT_INFO_SRC_8723B_1ANT_WIFI_FW			= 0x0,
	BT_INFO_SRC_8723B_1ANT_BT_RSP				= 0x1,
	BT_INFO_SRC_8723B_1ANT_BT_ACTIVE_SEND		= 0x2,
	BT_INFO_SRC_8723B_1ANT_MAX
};

enum {
	BT_8723B_1ANT_BT_STATUS_NON_CONNECTED_IDLE	= 0x0,
	BT_8723B_1ANT_BT_STATUS_CONNECTED_IDLE		= 0x1,
	BT_8723B_1ANT_BT_STATUS_INQ_PAGE				= 0x2,
	BT_8723B_1ANT_BT_STATUS_ACL_BUSY				= 0x3,
	BT_8723B_1ANT_BT_STATUS_SCO_BUSY				= 0x4,
	BT_8723B_1ANT_BT_STATUS_ACL_SCO_BUSY			= 0x5,
	BT_8723B_1ANT_BT_STATUS_MAX
};

enum {
	BT_8723B_1ANT_WIFI_STATUS_NON_CONNECTED_IDLE           = 0x0,
	BT_8723B_1ANT_WIFI_STATUS_NON_CONNECTED_ASSO_AUTH_SCAN = 0x1,
	BT_8723B_1ANT_WIFI_STATUS_CONNECTED_SCAN               = 0x2,
	BT_8723B_1ANT_WIFI_STATUS_CONNECTED_SPECIAL_PKT        = 0x3,
	BT_8723B_1ANT_WIFI_STATUS_CONNECTED_IDLE               = 0x4,
	BT_8723B_1ANT_WIFI_STATUS_CONNECTED_BUSY               = 0x5,
	BT_8723B_1ANT_WIFI_STATUS_MAX
};

enum {
	BT_8723B_1ANT_COEX_ALGO_UNDEFINED		= 0x0,
	BT_8723B_1ANT_COEX_ALGO_SCO				= 0x1,
	BT_8723B_1ANT_COEX_ALGO_HID				= 0x2,
	BT_8723B_1ANT_COEX_ALGO_A2DP			= 0x3,
	BT_8723B_1ANT_COEX_ALGO_A2DP_PANHS		= 0x4,
	BT_8723B_1ANT_COEX_ALGO_PANEDR			= 0x5,
	BT_8723B_1ANT_COEX_ALGO_PANHS			= 0x6,
	BT_8723B_1ANT_COEX_ALGO_PANEDR_A2DP		= 0x7,
	BT_8723B_1ANT_COEX_ALGO_PANEDR_HID		= 0x8,
	BT_8723B_1ANT_COEX_ALGO_HID_A2DP_PANEDR	= 0x9,
	BT_8723B_1ANT_COEX_ALGO_HID_A2DP		= 0xa,
	BT_8723B_1ANT_COEX_ALGO_MAX				= 0xb,
};

struct coex_dm_8723b_1ant {
	/*  fw mechanism */
	bool bCurIgnoreWlanAct;
	bool bPreIgnoreWlanAct;
	u8 prePsTdma;
	u8 curPsTdma;
	u8 psTdmaPara[5];
	u8 psTdmaDuAdjType;
	bool bAutoTdmaAdjust;
	bool bPrePsTdmaOn;
	bool bCurPsTdmaOn;
	bool bPreBtAutoReport;
	bool bCurBtAutoReport;
	u8 preLps;
	u8 curLps;
	u8 preRpwm;
	u8 curRpwm;

	/*  sw mechanism */
	bool bPreLowPenaltyRa;
	bool bCurLowPenaltyRa;
	u32 preVal0x6c0;
	u32 curVal0x6c0;
	u32 preVal0x6c4;
	u32 curVal0x6c4;
	u32 preVal0x6c8;
	u32 curVal0x6c8;
	u8 preVal0x6cc;
	u8 curVal0x6cc;
	bool bLimitedDig;

	u32 backupArfrCnt1;	/*  Auto Rate Fallback Retry cnt */
	u32 backupArfrCnt2;	/*  Auto Rate Fallback Retry cnt */
	u16 backupRetryLimit;
	u8 backupAmpduMaxTime;

	/*  algorithm related */
	u8 preAlgorithm;
	u8 curAlgorithm;
	u8 btStatus;
	u8 wifiChnlInfo[3];

	u32 preRaMask;
	u32 curRaMask;
	u8 preArfrType;
	u8 curArfrType;
	u8 preRetryLimitType;
	u8 curRetryLimitType;
	u8 preAmpduTimeType;
	u8 curAmpduTimeType;
	u32 nArpCnt;

	u8 errorCondition;
};

struct coex_sta_8723b_1ant {
	bool bBtLinkExist;
	bool bScoExist;
	bool bA2dpExist;
	bool bHidExist;
	bool bPanExist;

	bool bUnderLps;
	bool bUnderIps;
	u32 specialPktPeriodCnt;
	u32 highPriorityTx;
	u32 highPriorityRx;
	u32 lowPriorityTx;
	u32 lowPriorityRx;
	s8 btRssi;
	bool bBtTxRxMask;
	u8 preBtRssiState;
	u8 preWifiRssiState[4];
	bool bC2hBtInfoReqSent;
	u8 btInfoC2h[BT_INFO_SRC_8723B_1ANT_MAX][10];
	u32 btInfoC2hCnt[BT_INFO_SRC_8723B_1ANT_MAX];
	bool bC2hBtInquiryPage;
	bool bC2hBtPage; /* Add for win8.1 page out issue */
	bool bWiFiIsHighPriTask; /* Add for win8.1 page out issue */
	u8 btRetryCnt;
	u8 btInfoExt;
	u32 popEventCnt;
	u8 nScanAPNum;

	u32 nCRCOK_CCK;
	u32 nCRCOK_11g;
	u32 nCRCOK_11n;
	u32 nCRCOK_11nAgg;

	u32 nCRCErr_CCK;
	u32 nCRCErr_11g;
	u32 nCRCErr_11n;
	u32 nCRCErr_11nAgg;

	bool bCCKLock;
	bool bPreCCKLock;
	u8 nCoexTableType;

	bool bForceLpsOn;
};

/*  */
/*  The following is interface which will notify coex module. */
/*  */
void EXhalbtc8723b1ant_PowerOnSetting(struct btc_coexist *pBtCoexist);
void EXhalbtc8723b1ant_InitHwConfig(struct btc_coexist *pBtCoexist, bool bWifiOnly);
void EXhalbtc8723b1ant_InitCoexDm(struct btc_coexist *pBtCoexist);
void EXhalbtc8723b1ant_IpsNotify(struct btc_coexist *pBtCoexist, u8 type);
void EXhalbtc8723b1ant_LpsNotify(struct btc_coexist *pBtCoexist, u8 type);
void EXhalbtc8723b1ant_ScanNotify(struct btc_coexist *pBtCoexist, u8 type);
void EXhalbtc8723b1ant_ConnectNotify(struct btc_coexist *pBtCoexist, u8 type);
void EXhalbtc8723b1ant_MediaStatusNotify(struct btc_coexist *pBtCoexist, u8 type);
void EXhalbtc8723b1ant_SpecialPacketNotify(struct btc_coexist *pBtCoexist, u8 type);
void EXhalbtc8723b1ant_BtInfoNotify(
	struct btc_coexist *pBtCoexist, u8 *tmpBuf, u8 length
);
void EXhalbtc8723b1ant_HaltNotify(struct btc_coexist *pBtCoexist);
void EXhalbtc8723b1ant_PnpNotify(struct btc_coexist *pBtCoexist, u8 pnpState);
void EXhalbtc8723b1ant_Periodical(struct btc_coexist *pBtCoexist);
