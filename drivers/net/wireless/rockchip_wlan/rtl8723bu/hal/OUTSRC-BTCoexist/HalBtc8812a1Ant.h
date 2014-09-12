//===========================================
// The following is for 8812A_1ANT BT Co-exist definition
//===========================================
#define	BT_INFO_8812A_1ANT_B_FTP						BIT7
#define	BT_INFO_8812A_1ANT_B_A2DP					BIT6
#define	BT_INFO_8812A_1ANT_B_HID						BIT5
#define	BT_INFO_8812A_1ANT_B_SCO_BUSY				BIT4
#define	BT_INFO_8812A_1ANT_B_ACL_BUSY				BIT3
#define	BT_INFO_8812A_1ANT_B_INQ_PAGE				BIT2
#define	BT_INFO_8812A_1ANT_B_SCO_ESCO				BIT1
#define	BT_INFO_8812A_1ANT_B_CONNECTION				BIT0

#define	BT_INFO_8812A_1ANT_A2DP_BASIC_RATE(_BT_INFO_EXT_)	\
		(((_BT_INFO_EXT_&BIT0))? TRUE:FALSE)

#define	BTC_RSSI_COEX_THRESH_TOL_8812A_1ANT		2

#define	BTC_8812A_1ANT_SWITCH_TO_WIFI				0
#define	BTC_8812A_1ANT_SWITCH_TO_BT					1

typedef enum _BT_INFO_SRC_8812A_1ANT{
	BT_INFO_SRC_8812A_1ANT_WIFI_FW			= 0x0,
	BT_INFO_SRC_8812A_1ANT_BT_RSP				= 0x1,
	BT_INFO_SRC_8812A_1ANT_BT_ACTIVE_SEND		= 0x2,
	BT_INFO_SRC_8812A_1ANT_MAX
}BT_INFO_SRC_8812A_1ANT,*PBT_INFO_SRC_8812A_1ANT;

typedef enum _BT_8812A_1ANT_BT_STATUS{
	BT_8812A_1ANT_BT_STATUS_NON_CONNECTED_IDLE	= 0x0,
	BT_8812A_1ANT_BT_STATUS_CONNECTED_IDLE		= 0x1,
	BT_8812A_1ANT_BT_STATUS_INQ_PAGE				= 0x2,
	BT_8812A_1ANT_BT_STATUS_ACL_BUSY				= 0x3,
	BT_8812A_1ANT_BT_STATUS_SCO_BUSY				= 0x4,
	BT_8812A_1ANT_BT_STATUS_ACL_SCO_BUSY			= 0x5,
	BT_8812A_1ANT_BT_STATUS_MAX
}BT_8812A_1ANT_BT_STATUS,*PBT_8812A_1ANT_BT_STATUS;

typedef enum _BT_8812A_1ANT_WIFI_STATUS{
	BT_8812A_1ANT_WIFI_STATUS_NON_CONNECTED_IDLE				= 0x0,
	BT_8812A_1ANT_WIFI_STATUS_NON_CONNECTED_ASSO_AUTH_SCAN		= 0x1,
	BT_8812A_1ANT_WIFI_STATUS_CONNECTED_SCAN					= 0x2,
	BT_8812A_1ANT_WIFI_STATUS_CONNECTED_SPECIAL_PKT				= 0x3,
	BT_8812A_1ANT_WIFI_STATUS_CONNECTED_IDLE					= 0x4,
	BT_8812A_1ANT_WIFI_STATUS_CONNECTED_BUSY					= 0x5,
	BT_8812A_1ANT_WIFI_STATUS_MAX
}BT_8812A_1ANT_WIFI_STATUS,*PBT_8812A_1ANT_WIFI_STATUS;

typedef enum _BT_8812A_1ANT_COEX_ALGO{
	BT_8812A_1ANT_COEX_ALGO_UNDEFINED			= 0x0,
	BT_8812A_1ANT_COEX_ALGO_SCO				= 0x1,
	BT_8812A_1ANT_COEX_ALGO_HID				= 0x2,
	BT_8812A_1ANT_COEX_ALGO_A2DP				= 0x3,
	BT_8812A_1ANT_COEX_ALGO_A2DP_PANHS		= 0x4,
	BT_8812A_1ANT_COEX_ALGO_PANEDR			= 0x5,
	BT_8812A_1ANT_COEX_ALGO_PANHS			= 0x6,
	BT_8812A_1ANT_COEX_ALGO_PANEDR_A2DP		= 0x7,
	BT_8812A_1ANT_COEX_ALGO_PANEDR_HID		= 0x8,
	BT_8812A_1ANT_COEX_ALGO_HID_A2DP_PANEDR	= 0x9,
	BT_8812A_1ANT_COEX_ALGO_HID_A2DP			= 0xa,
	BT_8812A_1ANT_COEX_ALGO_MAX				= 0xb,
}BT_8812A_1ANT_COEX_ALGO,*PBT_8812A_1ANT_COEX_ALGO;

typedef struct _COEX_DM_8812A_1ANT{
	// fw mechanism
	BOOLEAN		bCurIgnoreWlanAct;
	BOOLEAN		bPreIgnoreWlanAct;
	u1Byte		prePsTdma;
	u1Byte		curPsTdma;
	u1Byte		psTdmaPara[5];
	u1Byte		psTdmaDuAdjType;
	BOOLEAN		bResetTdmaAdjust;
	BOOLEAN		bPrePsTdmaOn;
	BOOLEAN		bCurPsTdmaOn;
	BOOLEAN		bPreBtAutoReport;
	BOOLEAN		bCurBtAutoReport;
	u1Byte		preLps;
	u1Byte		curLps;
	u1Byte		preRpwm;
	u1Byte		curRpwm;

	// sw mechanism
	BOOLEAN 	bPreLowPenaltyRa;
	BOOLEAN		bCurLowPenaltyRa;
	BOOLEAN		bPreDacSwingOn;
	u4Byte		preVal0x6c0;
	u4Byte		curVal0x6c0;
	u4Byte		preVal0x6c4;
	u4Byte		curVal0x6c4;
	u4Byte		preVal0x6c8;
	u4Byte		curVal0x6c8;
	u1Byte		preVal0x6cc;
	u1Byte		curVal0x6cc;

	// algorithm related
	u1Byte		preAlgorithm;
	u1Byte		curAlgorithm;
	u1Byte		btStatus;
	u1Byte		wifiChnlInfo[3];

	u4Byte		preRaMask;
	u4Byte		curRaMask;

	u1Byte		errorCondition;
} COEX_DM_8812A_1ANT, *PCOEX_DM_8812A_1ANT;

typedef struct _COEX_STA_8812A_1ANT{
	BOOLEAN					bBtLinkExist;
	BOOLEAN					bScoExist;
	BOOLEAN					bA2dpExist;
	BOOLEAN					bHidExist;
	BOOLEAN					bPanExist;

	BOOLEAN					bUnderLps;
	BOOLEAN					bUnderIps;
	u4Byte					highPriorityTx;
	u4Byte					highPriorityRx;
	u4Byte					lowPriorityTx;
	u4Byte					lowPriorityRx;
	u1Byte					btRssi;
	u1Byte					preBtRssiState;
	u1Byte					preWifiRssiState[4];
	BOOLEAN					bC2hBtInfoReqSent;
	u1Byte					btInfoC2h[BT_INFO_SRC_8812A_1ANT_MAX][10];
	u4Byte					btInfoC2hCnt[BT_INFO_SRC_8812A_1ANT_MAX];
	u4Byte					btInfoQueryCnt;
	BOOLEAN					bC2hBtInquiryPage;
	u1Byte					btRetryCnt;
	u1Byte					btInfoExt;
}COEX_STA_8812A_1ANT, *PCOEX_STA_8812A_1ANT;

//===========================================
// The following is interface which will notify coex module.
//===========================================
VOID
EXhalbtc8812a1ant_InitHwConfig(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN				bWifiOnly
	);
VOID
EXhalbtc8812a1ant_InitCoexDm(
	IN	PBTC_COEXIST		pBtCoexist
	);
VOID
EXhalbtc8812a1ant_IpsNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			type
	);
VOID
EXhalbtc8812a1ant_LpsNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			type
	);
VOID
EXhalbtc8812a1ant_ScanNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			type
	);
VOID
EXhalbtc8812a1ant_ConnectNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			type
	);
VOID
EXhalbtc8812a1ant_MediaStatusNotify(
	IN	PBTC_COEXIST			pBtCoexist,
	IN	u1Byte				type
	);
VOID
EXhalbtc8812a1ant_SpecialPacketNotify(
	IN	PBTC_COEXIST			pBtCoexist,
	IN	u1Byte				type
	);
VOID
EXhalbtc8812a1ant_BtInfoNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	pu1Byte			tmpBuf,
	IN	u1Byte			length
	);
VOID
EXhalbtc8812a1ant_HaltNotify(
	IN	PBTC_COEXIST			pBtCoexist
	);
VOID
EXhalbtc8812a1ant_PnpNotify(
	IN	PBTC_COEXIST			pBtCoexist,
	IN	u1Byte				pnpState
	);
VOID
EXhalbtc8812a1ant_Periodical(
	IN	PBTC_COEXIST			pBtCoexist
	);
VOID
EXhalbtc8812a1ant_DisplayCoexInfo(
	IN	PBTC_COEXIST		pBtCoexist
	);
VOID
EXhalbtc8812a1ant_DbgControl(
	IN	PBTC_COEXIST			pBtCoexist,
	IN	u1Byte				opCode,
	IN	u1Byte				opLen,
	IN	pu1Byte 			pData
	);
