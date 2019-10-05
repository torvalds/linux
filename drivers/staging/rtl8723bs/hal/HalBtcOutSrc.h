/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#ifndef	__HALBTC_OUT_SRC_H__
#define __HALBTC_OUT_SRC_H__

#define NORMAL_EXEC		false
#define FORCE_EXEC		true

#define BTC_RF_OFF		0x0
#define BTC_RF_ON		0x1

#define BTC_RF_A		0x0
#define BTC_RF_B		0x1
#define BTC_RF_C		0x2
#define BTC_RF_D		0x3

#define BTC_SMSP		SINGLEMAC_SINGLEPHY
#define BTC_DMDP		DUALMAC_DUALPHY
#define BTC_DMSP		DUALMAC_SINGLEPHY
#define BTC_MP_UNKNOWN		0xff

#define BT_COEX_ANT_TYPE_PG	0
#define BT_COEX_ANT_TYPE_ANTDIV		1
#define BT_COEX_ANT_TYPE_DETECTED	2

#define BTC_MIMO_PS_STATIC	0	/*  1ss */
#define BTC_MIMO_PS_DYNAMIC	1	/*  2ss */

#define BTC_RATE_DISABLE	0
#define BTC_RATE_ENABLE		1

/*  single Antenna definition */
#define BTC_ANT_PATH_WIFI	0
#define BTC_ANT_PATH_BT		1
#define BTC_ANT_PATH_PTA	2
/*  dual Antenna definition */
#define BTC_ANT_WIFI_AT_MAIN	0
#define BTC_ANT_WIFI_AT_AUX	1
/*  coupler Antenna definition */
#define BTC_ANT_WIFI_AT_CPL_MAIN	0
#define BTC_ANT_WIFI_AT_CPL_AUX		1

typedef enum _BTC_POWERSAVE_TYPE{
	BTC_PS_WIFI_NATIVE	= 0,	/*  wifi original power save behavior */
	BTC_PS_LPS_ON		= 1,
	BTC_PS_LPS_OFF		= 2,
	BTC_PS_MAX
} BTC_POWERSAVE_TYPE, *PBTC_POWERSAVE_TYPE;

typedef enum _BTC_BT_REG_TYPE{
	BTC_BT_REG_RF		= 0,
	BTC_BT_REG_MODEM	= 1,
	BTC_BT_REG_BLUEWIZE	= 2,
	BTC_BT_REG_VENDOR	= 3,
	BTC_BT_REG_LE		= 4,
	BTC_BT_REG_MAX
} BTC_BT_REG_TYPE, *PBTC_BT_REG_TYPE;

typedef enum _BTC_CHIP_INTERFACE{
	BTC_INTF_UNKNOWN	= 0,
	BTC_INTF_PCI		= 1,
	BTC_INTF_USB		= 2,
	BTC_INTF_SDIO		= 3,
	BTC_INTF_MAX
} BTC_CHIP_INTERFACE, *PBTC_CHIP_INTERFACE;

typedef enum _BTC_CHIP_TYPE {
	BTC_CHIP_UNDEF		= 0,
	BTC_CHIP_CSR_BC4	= 1,
	BTC_CHIP_CSR_BC8	= 2,
	BTC_CHIP_RTL8723A	= 3,
	BTC_CHIP_RTL8821	= 4,
	BTC_CHIP_RTL8723B	= 5,
	BTC_CHIP_MAX
} BTC_CHIP_TYPE, *PBTC_CHIP_TYPE;

typedef enum _BTC_MSG_TYPE {
	BTC_MSG_INTERFACE	= 0x0,
	BTC_MSG_ALGORITHM	= 0x1,
	BTC_MSG_MAX
} BTC_MSG_TYPE;
extern u32 		GLBtcDbgType[];

/*  following is for BTC_MSG_INTERFACE */
#define INTF_INIT	BIT0
#define INTF_NOTIFY	BIT2

/*  following is for BTC_ALGORITHM */
#define ALGO_BT_RSSI_STATE				BIT0
#define ALGO_WIFI_RSSI_STATE			BIT1
#define ALGO_BT_MONITOR					BIT2
#define ALGO_TRACE						BIT3
#define ALGO_TRACE_FW					BIT4
#define ALGO_TRACE_FW_DETAIL			BIT5
#define ALGO_TRACE_FW_EXEC				BIT6
#define ALGO_TRACE_SW					BIT7
#define ALGO_TRACE_SW_DETAIL			BIT8
#define ALGO_TRACE_SW_EXEC				BIT9

/*  following is for wifi link status */
#define WIFI_STA_CONNECTED				BIT0
#define WIFI_AP_CONNECTED				BIT1
#define WIFI_HS_CONNECTED				BIT2
#define WIFI_P2P_GO_CONNECTED			BIT3
#define WIFI_P2P_GC_CONNECTED			BIT4

/*  following is for command line utility */
#define CL_SPRINTF	snprintf
#define CL_PRINTF	DCMD_Printf

/*  The following is for dbgview print */
#if DBG
#define BTC_PRINT(dbgtype, dbgflag, printstr)\
{\
	if (GLBtcDbgType[dbgtype] & dbgflag)\
		DbgPrint printstr;\
}

#define BTC_PRINT_F(dbgtype, dbgflag, printstr)\
{\
	if (GLBtcDbgType[dbgtype] & dbgflag) {\
		DbgPrint("%s(): ", __func__);\
		DbgPrint printstr;\
	} \
}

#define BTC_PRINT_ADDR(dbgtype, dbgflag, printstr, _Ptr)\
{\
	if (GLBtcDbgType[dbgtype] & dbgflag) {\
		int __i;\
		u8 *ptr = (u8 *)_Ptr;\
		DbgPrint printstr;\
		DbgPrint(" ");\
		for (__i = 0; __i < 6; __i++)\
			DbgPrint("%02X%s", ptr[__i], (__i == 5) ? "" : "-");\
		DbgPrint("\n");\
	} \
}

#define BTC_PRINT_DATA(dbgtype, dbgflag, _TitleString, _HexData, _HexDataLen)\
{\
	if (GLBtcDbgType[dbgtype] & dbgflag) {\
		int __i;\
		u8 *ptr = (u8 *)_HexData;\
		DbgPrint(_TitleString);\
		for (__i = 0; __i < (int)_HexDataLen; __i++) {\
			DbgPrint("%02X%s", ptr[__i], (((__i + 1) % 4) == 0) ? "  " : " ");\
			if (((__i + 1) % 16) == 0)\
				DbgPrint("\n");\
		} \
		DbgPrint("\n");\
	} \
}

#else
#define BTC_PRINT(dbgtype, dbgflag, printstr)		 no_printk printstr
#define BTC_PRINT_F(dbgtype, dbgflag, printstr)		 no_printk printstr
#define BTC_PRINT_ADDR(dbgtype, dbgflag, printstr, _Ptr) no_printk printstr
#define BTC_PRINT_DATA(dbgtype, dbgflag, _TitleString, _HexData, _HexDataLen) \
			no_printk("%s %p %zu", _TitleString, _HexData, _HexDataLen)
#endif

typedef struct _BTC_BOARD_INFO {
	/*  The following is some board information */
	u8 btChipType;
	u8 pgAntNum;	/*  pg ant number */
	u8 btdmAntNum;	/*  ant number for btdm */
	u8 btdmAntPos;		/* Bryant Add to indicate Antenna Position for (pgAntNum = 2) && (btdmAntNum = 1)  (DPDT+1Ant case) */
	u8 singleAntPath;	/*  current used for 8723b only, 1 =>s0,  0 =>s1 */
	/* bool				bBtExist; */
} BTC_BOARD_INFO, *PBTC_BOARD_INFO;

typedef enum _BTC_DBG_OPCODE {
	BTC_DBG_SET_COEX_NORMAL			    = 0x0,
	BTC_DBG_SET_COEX_WIFI_ONLY		    = 0x1,
	BTC_DBG_SET_COEX_BT_ONLY		    = 0x2,
	BTC_DBG_SET_COEX_DEC_BT_PWR		    = 0x3,
	BTC_DBG_SET_COEX_BT_AFH_MAP		    = 0x4,
	BTC_DBG_SET_COEX_BT_IGNORE_WLAN_ACT	= 0x5,
	BTC_DBG_MAX
} BTC_DBG_OPCODE, *PBTC_DBG_OPCODE;

typedef enum _BTC_RSSI_STATE {
	BTC_RSSI_STATE_HIGH			    = 0x0,
	BTC_RSSI_STATE_MEDIUM			= 0x1,
	BTC_RSSI_STATE_LOW			    = 0x2,
	BTC_RSSI_STATE_STAY_HIGH		= 0x3,
	BTC_RSSI_STATE_STAY_MEDIUM		= 0x4,
	BTC_RSSI_STATE_STAY_LOW			= 0x5,
	BTC_RSSI_MAX
} BTC_RSSI_STATE, *PBTC_RSSI_STATE;
#define BTC_RSSI_HIGH(_rssi_)	((_rssi_ == BTC_RSSI_STATE_HIGH || _rssi_ == BTC_RSSI_STATE_STAY_HIGH) ? true : false)
#define BTC_RSSI_MEDIUM(_rssi_)	((_rssi_ == BTC_RSSI_STATE_MEDIUM || _rssi_ == BTC_RSSI_STATE_STAY_MEDIUM) ? true : false)
#define BTC_RSSI_LOW(_rssi_)	((_rssi_ == BTC_RSSI_STATE_LOW || _rssi_ == BTC_RSSI_STATE_STAY_LOW) ? true : false)

typedef enum _BTC_WIFI_ROLE {
	BTC_ROLE_STATION			= 0x0,
	BTC_ROLE_AP					= 0x1,
	BTC_ROLE_IBSS				= 0x2,
	BTC_ROLE_HS_MODE			= 0x3,
	BTC_ROLE_MAX
} BTC_WIFI_ROLE, *PBTC_WIFI_ROLE;

typedef enum _BTC_WIFI_BW_MODE {
	BTC_WIFI_BW_LEGACY			= 0x0,
	BTC_WIFI_BW_HT20			= 0x1,
	BTC_WIFI_BW_HT40			= 0x2,
	BTC_WIFI_BW_MAX
} BTC_WIFI_BW_MODE, *PBTC_WIFI_BW_MODE;

typedef enum _BTC_WIFI_TRAFFIC_DIR {
	BTC_WIFI_TRAFFIC_TX			= 0x0,
	BTC_WIFI_TRAFFIC_RX			= 0x1,
	BTC_WIFI_TRAFFIC_MAX
} BTC_WIFI_TRAFFIC_DIR, *PBTC_WIFI_TRAFFIC_DIR;

typedef enum _BTC_WIFI_PNP {
	BTC_WIFI_PNP_WAKE_UP		= 0x0,
	BTC_WIFI_PNP_SLEEP			= 0x1,
	BTC_WIFI_PNP_MAX
} BTC_WIFI_PNP, *PBTC_WIFI_PNP;

/* for 8723b-d cut large current issue */
typedef enum _BT_WIFI_COEX_STATE {
	BTC_WIFI_STAT_INIT,
	BTC_WIFI_STAT_IQK,
	BTC_WIFI_STAT_NORMAL_OFF,
	BTC_WIFI_STAT_MP_OFF,
	BTC_WIFI_STAT_NORMAL,
	BTC_WIFI_STAT_ANT_DIV,
	BTC_WIFI_STAT_MAX
} BT_WIFI_COEX_STATE, *PBT_WIFI_COEX_STATE;

/*  defined for BFP_BTC_GET */
typedef enum _BTC_GET_TYPE {
	/*  type bool */
	BTC_GET_BL_HS_OPERATION,
	BTC_GET_BL_HS_CONNECTING,
	BTC_GET_BL_WIFI_CONNECTED,
	BTC_GET_BL_WIFI_BUSY,
	BTC_GET_BL_WIFI_SCAN,
	BTC_GET_BL_WIFI_LINK,
	BTC_GET_BL_WIFI_ROAM,
	BTC_GET_BL_WIFI_4_WAY_PROGRESS,
	BTC_GET_BL_WIFI_UNDER_5G,
	BTC_GET_BL_WIFI_AP_MODE_ENABLE,
	BTC_GET_BL_WIFI_ENABLE_ENCRYPTION,
	BTC_GET_BL_WIFI_UNDER_B_MODE,
	BTC_GET_BL_EXT_SWITCH,
	BTC_GET_BL_WIFI_IS_IN_MP_MODE,

	/*  type s32 */
	BTC_GET_S4_WIFI_RSSI,
	BTC_GET_S4_HS_RSSI,

	/*  type u32 */
	BTC_GET_U4_WIFI_BW,
	BTC_GET_U4_WIFI_TRAFFIC_DIRECTION,
	BTC_GET_U4_WIFI_FW_VER,
	BTC_GET_U4_WIFI_LINK_STATUS,
	BTC_GET_U4_BT_PATCH_VER,

	/*  type u8 */
	BTC_GET_U1_WIFI_DOT11_CHNL,
	BTC_GET_U1_WIFI_CENTRAL_CHNL,
	BTC_GET_U1_WIFI_HS_CHNL,
	BTC_GET_U1_MAC_PHY_MODE,
	BTC_GET_U1_AP_NUM,

	/*  for 1Ant ====== */
	BTC_GET_U1_LPS_MODE,

	BTC_GET_MAX
} BTC_GET_TYPE, *PBTC_GET_TYPE;

/*  defined for BFP_BTC_SET */
typedef enum _BTC_SET_TYPE {
	/*  type bool */
	BTC_SET_BL_BT_DISABLE,
	BTC_SET_BL_BT_TRAFFIC_BUSY,
	BTC_SET_BL_BT_LIMITED_DIG,
	BTC_SET_BL_FORCE_TO_ROAM,
	BTC_SET_BL_TO_REJ_AP_AGG_PKT,
	BTC_SET_BL_BT_CTRL_AGG_SIZE,
	BTC_SET_BL_INC_SCAN_DEV_NUM,
	BTC_SET_BL_BT_TX_RX_MASK,

	/*  type u8 */
	BTC_SET_U1_RSSI_ADJ_VAL_FOR_AGC_TABLE_ON,
	BTC_SET_U1_AGG_BUF_SIZE,

	/*  type trigger some action */
	BTC_SET_ACT_GET_BT_RSSI,
	BTC_SET_ACT_AGGREGATE_CTRL,
	/*  for 1Ant ====== */
	/*  type bool */

	/*  type u8 */
	BTC_SET_U1_RSSI_ADJ_VAL_FOR_1ANT_COEX_TYPE,
	BTC_SET_U1_LPS_VAL,
	BTC_SET_U1_RPWM_VAL,
	/*  type trigger some action */
	BTC_SET_ACT_LEAVE_LPS,
	BTC_SET_ACT_ENTER_LPS,
	BTC_SET_ACT_NORMAL_LPS,
	BTC_SET_ACT_DISABLE_LOW_POWER,
	BTC_SET_ACT_UPDATE_RAMASK,
	BTC_SET_ACT_SEND_MIMO_PS,
	/*  BT Coex related */
	BTC_SET_ACT_CTRL_BT_INFO,
	BTC_SET_ACT_CTRL_BT_COEX,
	BTC_SET_ACT_CTRL_8723B_ANT,
	/*  */
	BTC_SET_MAX
} BTC_SET_TYPE, *PBTC_SET_TYPE;

typedef enum _BTC_DBG_DISP_TYPE {
	BTC_DBG_DISP_COEX_STATISTICS		= 0x0,
	BTC_DBG_DISP_BT_LINK_INFO			= 0x1,
	BTC_DBG_DISP_FW_PWR_MODE_CMD		= 0x2,
	BTC_DBG_DISP_MAX
} BTC_DBG_DISP_TYPE, *PBTC_DBG_DISP_TYPE;

typedef enum _BTC_NOTIFY_TYPE_IPS {
	BTC_IPS_LEAVE						= 0x0,
	BTC_IPS_ENTER						= 0x1,
	BTC_IPS_MAX
} BTC_NOTIFY_TYPE_IPS, *PBTC_NOTIFY_TYPE_IPS;

typedef enum _BTC_NOTIFY_TYPE_LPS {
	BTC_LPS_DISABLE						= 0x0,
	BTC_LPS_ENABLE						= 0x1,
	BTC_LPS_MAX
} BTC_NOTIFY_TYPE_LPS, *PBTC_NOTIFY_TYPE_LPS;

typedef enum _BTC_NOTIFY_TYPE_SCAN {
	BTC_SCAN_FINISH						= 0x0,
	BTC_SCAN_START						= 0x1,
	BTC_SCAN_MAX
} BTC_NOTIFY_TYPE_SCAN, *PBTC_NOTIFY_TYPE_SCAN;

typedef enum _BTC_NOTIFY_TYPE_ASSOCIATE {
	BTC_ASSOCIATE_FINISH				= 0x0,
	BTC_ASSOCIATE_START					= 0x1,
	BTC_ASSOCIATE_MAX
} BTC_NOTIFY_TYPE_ASSOCIATE, *PBTC_NOTIFY_TYPE_ASSOCIATE;

typedef enum _BTC_NOTIFY_TYPE_MEDIA_STATUS {
	BTC_MEDIA_DISCONNECT				= 0x0,
	BTC_MEDIA_CONNECT					= 0x1,
	BTC_MEDIA_MAX
} BTC_NOTIFY_TYPE_MEDIA_STATUS, *PBTC_NOTIFY_TYPE_MEDIA_STATUS;

typedef enum _BTC_NOTIFY_TYPE_SPECIAL_PACKET {
	BTC_PACKET_UNKNOWN					= 0x0,
	BTC_PACKET_DHCP						= 0x1,
	BTC_PACKET_ARP						= 0x2,
	BTC_PACKET_EAPOL					= 0x3,
	BTC_PACKET_MAX
} BTC_NOTIFY_TYPE_SPECIAL_PACKET, *PBTC_NOTIFY_TYPE_SPECIAL_PACKET;

typedef enum _BTC_NOTIFY_TYPE_STACK_OPERATION {
	BTC_STACK_OP_NONE					= 0x0,
	BTC_STACK_OP_INQ_PAGE_PAIR_START	= 0x1,
	BTC_STACK_OP_INQ_PAGE_PAIR_FINISH	= 0x2,
	BTC_STACK_OP_MAX
} BTC_NOTIFY_TYPE_STACK_OPERATION, *PBTC_NOTIFY_TYPE_STACK_OPERATION;

/* Bryant Add */
typedef enum _BTC_ANTENNA_POS {
	BTC_ANTENNA_AT_MAIN_PORT = 0x1,
	BTC_ANTENNA_AT_AUX_PORT  = 0x2,
} BTC_ANTENNA_POS, *PBTC_ANTENNA_POS;

typedef u8 (*BFP_BTC_R1)(void *pBtcContext, u32 RegAddr);
typedef u16(*BFP_BTC_R2)(void *pBtcContext, u32 RegAddr);
typedef u32 (*BFP_BTC_R4)(void *pBtcContext, u32 RegAddr);
typedef void (*BFP_BTC_W1)(void *pBtcContext, u32 RegAddr, u8 Data);
typedef void(*BFP_BTC_W1_BIT_MASK)(
	void *pBtcContext, u32 regAddr, u8 bitMask, u8 data1b
);
typedef void (*BFP_BTC_W2)(void *pBtcContext, u32 RegAddr, u16 Data);
typedef void (*BFP_BTC_W4)(void *pBtcContext, u32 RegAddr, u32 Data);
typedef void (*BFP_BTC_LOCAL_REG_W1)(void *pBtcContext, u32 RegAddr, u8 Data);
typedef void (*BFP_BTC_SET_BB_REG)(
	void *pBtcContext, u32 RegAddr, u32 BitMask, u32 Data
);
typedef u32 (*BFP_BTC_GET_BB_REG)(void *pBtcContext, u32 RegAddr, u32 BitMask);
typedef void (*BFP_BTC_SET_RF_REG)(
	void *pBtcContext, u8 eRFPath, u32 RegAddr, u32 BitMask, u32 Data
);
typedef u32 (*BFP_BTC_GET_RF_REG)(
	void *pBtcContext, u8 eRFPath, u32 RegAddr, u32 BitMask
);
typedef void (*BFP_BTC_FILL_H2C)(
	void *pBtcContext, u8 elementId, u32 cmdLen, u8 *pCmdBuffer
);

typedef	u8 (*BFP_BTC_GET)(void *pBtCoexist, u8 getType, void *pOutBuf);

typedef	u8 (*BFP_BTC_SET)(void *pBtCoexist, u8 setType, void *pInBuf);
typedef void (*BFP_BTC_SET_BT_REG)(
	void *pBtcContext, u8 regType, u32 offset, u32 value
);
typedef u32 (*BFP_BTC_GET_BT_REG)(void *pBtcContext, u8 regType, u32 offset);
typedef void (*BFP_BTC_DISP_DBG_MSG)(void *pBtCoexist, u8 dispType);

typedef struct _BTC_BT_INFO {
	bool bBtDisabled;
	u8 rssiAdjustForAgcTableOn;
	u8 rssiAdjustFor1AntCoexType;
	bool bPreBtCtrlAggBufSize;
	bool bBtCtrlAggBufSize;
	bool bRejectAggPkt;
	bool bIncreaseScanDevNum;
	bool bBtTxRxMask;
	u8 preAggBufSize;
	u8 aggBufSize;
	bool bBtBusy;
	bool bLimitedDig;
	u16 btHciVer;
	u16 btRealFwVer;
	u8 btFwVer;
	u32 getBtFwVerCnt;

	bool bBtDisableLowPwr;

	bool bBtCtrlLps;
	bool bBtLpsOn;
	bool bForceToRoam;	/*  for 1Ant solution */
	u8 lpsVal;
	u8 rpwmVal;
	u32 raMask;
} BTC_BT_INFO, *PBTC_BT_INFO;

typedef struct _BTC_STACK_INFO {
	bool bProfileNotified;
	u16 hciVersion;	/*  stack hci version */
	u8 numOfLink;
	bool bBtLinkExist;
	bool bScoExist;
	bool bAclExist;
	bool bA2dpExist;
	bool bHidExist;
	u8 numOfHid;
	bool bPanExist;
	bool bUnknownAclExist;
	s8 minBtRssi;
} BTC_STACK_INFO, *PBTC_STACK_INFO;

typedef struct _BTC_BT_LINK_INFO {
	bool bBtLinkExist;
	bool bScoExist;
	bool bScoOnly;
	bool bA2dpExist;
	bool bA2dpOnly;
	bool bHidExist;
	bool bHidOnly;
	bool bPanExist;
	bool bPanOnly;
	bool bSlaveRole;
} BTC_BT_LINK_INFO, *PBTC_BT_LINK_INFO;

typedef struct _BTC_STATISTICS {
	u32 cntBind;
	u32 cntPowerOn;
	u32 cntInitHwConfig;
	u32 cntInitCoexDm;
	u32 cntIpsNotify;
	u32 cntLpsNotify;
	u32 cntScanNotify;
	u32 cntConnectNotify;
	u32 cntMediaStatusNotify;
	u32 cntSpecialPacketNotify;
	u32 cntBtInfoNotify;
	u32 cntRfStatusNotify;
	u32 cntPeriodical;
	u32 cntCoexDmSwitch;
	u32 cntStackOperationNotify;
	u32 cntDbgCtrl;
} BTC_STATISTICS, *PBTC_STATISTICS;

typedef struct _BTC_COEXIST {
	bool bBinded;		/*  make sure only one adapter can bind the data context */
	void *Adapter;		/*  default adapter */
	BTC_BOARD_INFO boardInfo;
	BTC_BT_INFO btInfo;		/*  some bt info referenced by non-bt module */
	BTC_STACK_INFO stackInfo;
	BTC_BT_LINK_INFO btLinkInfo;
	BTC_CHIP_INTERFACE chipInterface;

	bool bInitilized;
	bool bStopCoexDm;
	bool bManualControl;
	u8 *cliBuf;
	BTC_STATISTICS statistics;
	u8 pwrModeVal[10];

	/*  function pointers */
	/*  io related */
	BFP_BTC_R1 fBtcRead1Byte;
	BFP_BTC_W1 fBtcWrite1Byte;
	BFP_BTC_W1_BIT_MASK fBtcWrite1ByteBitMask;
	BFP_BTC_R2 fBtcRead2Byte;
	BFP_BTC_W2 fBtcWrite2Byte;
	BFP_BTC_R4 fBtcRead4Byte;
	BFP_BTC_W4 fBtcWrite4Byte;
	BFP_BTC_LOCAL_REG_W1 fBtcWriteLocalReg1Byte;
	/*  read/write bb related */
	BFP_BTC_SET_BB_REG fBtcSetBbReg;
	BFP_BTC_GET_BB_REG fBtcGetBbReg;

	/*  read/write rf related */
	BFP_BTC_SET_RF_REG fBtcSetRfReg;
	BFP_BTC_GET_RF_REG fBtcGetRfReg;

	/*  fill h2c related */
	BFP_BTC_FILL_H2C fBtcFillH2c;
	/*  other */
	BFP_BTC_DISP_DBG_MSG fBtcDispDbgMsg;
	/*  normal get/set related */
	BFP_BTC_GET fBtcGet;
	BFP_BTC_SET fBtcSet;

	BFP_BTC_GET_BT_REG fBtcGetBtReg;
	BFP_BTC_SET_BT_REG fBtcSetBtReg;
} BTC_COEXIST, *PBTC_COEXIST;

extern BTC_COEXIST GLBtCoexist;

void EXhalbtcoutsrc_PowerOnSetting(PBTC_COEXIST pBtCoexist);
void EXhalbtcoutsrc_InitHwConfig(PBTC_COEXIST pBtCoexist, u8 bWifiOnly);
void EXhalbtcoutsrc_InitCoexDm(PBTC_COEXIST pBtCoexist);
void EXhalbtcoutsrc_IpsNotify(PBTC_COEXIST pBtCoexist, u8 type);
void EXhalbtcoutsrc_LpsNotify(PBTC_COEXIST pBtCoexist, u8 type);
void EXhalbtcoutsrc_ScanNotify(PBTC_COEXIST pBtCoexist, u8 type);
void EXhalbtcoutsrc_ConnectNotify(PBTC_COEXIST pBtCoexist, u8 action);
void EXhalbtcoutsrc_MediaStatusNotify(
	PBTC_COEXIST pBtCoexist, RT_MEDIA_STATUS mediaStatus
);
void EXhalbtcoutsrc_SpecialPacketNotify(PBTC_COEXIST pBtCoexist, u8 pktType);
void EXhalbtcoutsrc_BtInfoNotify(
	PBTC_COEXIST pBtCoexist, u8 *tmpBuf, u8 length
);
void EXhalbtcoutsrc_HaltNotify(PBTC_COEXIST pBtCoexist);
void EXhalbtcoutsrc_PnpNotify(PBTC_COEXIST pBtCoexist, u8 pnpState);
void EXhalbtcoutsrc_Periodical(PBTC_COEXIST pBtCoexist);
void EXhalbtcoutsrc_SetChipType(u8 chipType);
void EXhalbtcoutsrc_SetAntNum(u8 type, u8 antNum);
void EXhalbtcoutsrc_SetSingleAntPath(u8 singleAntPath);
void EXhalbtcoutsrc_DisplayBtCoexInfo(PBTC_COEXIST pBtCoexist);

#endif
