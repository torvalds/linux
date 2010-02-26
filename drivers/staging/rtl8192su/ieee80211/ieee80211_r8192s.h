#ifndef __IEEE80211_R8192S_H
#define __IEEE80211_R8192S_H

/* added for rtl819x tx procedure */
#define MAX_QUEUE_SIZE		0x10

/* 8190 queue mapping */
enum {
	BK_QUEUE	= 0,
	BE_QUEUE	= 1,
	VI_QUEUE	= 2,
	VO_QUEUE	= 3,
	HCCA_QUEUE	= 4,
	TXCMD_QUEUE	= 5,
	MGNT_QUEUE	= 6,
	HIGH_QUEUE	= 7,
	BEACON_QUEUE	= 8,

	LOW_QUEUE	= BE_QUEUE,
	NORMAL_QUEUE	= MGNT_QUEUE
};

#define SWRF_TIMEOUT		50

/* LEAP related */
/* Flag byte: byte 8, numbered from 0. */
#define IE_CISCO_FLAG_POSITION		0x08
#define SUPPORT_CKIP_MIC		0x08	/* bit3 */
#define SUPPORT_CKIP_PK			0x10	/* bit4 */

/* defined for skb cb field, at most 28 byte */
typedef struct cb_desc {
	/* Tx Desc Related flags (8-9) */
	u8 bLastIniPkt:1;
	u8 bCmdOrInit:1;
	u8 bFirstSeg:1;
	u8 bLastSeg:1;
	u8 bEncrypt:1;
	u8 bTxDisableRateFallBack:1;
	u8 bTxUseDriverAssingedRate:1;
	u8 bHwSec:1; /* indicate whether use Hw security */

	u8 reserved1;

	/* Tx Firmware Relaged flags (10-11) */
	u8 bCTSEnable:1;
	u8 bRTSEnable:1;
	u8 bUseShortGI:1;
	u8 bUseShortPreamble:1;
	u8 bTxEnableFwCalcDur:1;
	u8 bAMPDUEnable:1;
	u8 bRTSSTBC:1;
	u8 RTSSC:1;

	u8 bRTSBW:1;
	u8 bPacketBW:1;
	u8 bRTSUseShortPreamble:1;
	u8 bRTSUseShortGI:1;
	u8 bMulticast:1;
	u8 bBroadcast:1;
	u8 drv_agg_enable:1;
	u8 reserved2:1;

	/* Tx Desc related element(12-19) */
	u8 rata_index;
	u8 queue_index;
	u16 txbuf_size;
	u8 RATRIndex;
	u8 reserved6;
	u8 reserved7;
	u8 reserved8;

	/* Tx firmware related element(20-27) */
	u8 data_rate;
	u8 rts_rate;
	u8 ampdu_factor;
	u8 ampdu_density;
	u8 DrvAggrNum;
	u16 pkt_size;
	u8 reserved12;
} cb_desc, *pcb_desc;

enum {
	MGN_1M		= 0x02,
	MGN_2M		= 0x04,
	MGN_5_5M	= 0x0b,
	MGN_11M		= 0x16,

	MGN_6M		= 0x0c,
	MGN_9M		= 0x12,
	MGN_12M		= 0x18,
	MGN_18M		= 0x24,
	MGN_24M		= 0x30,
	MGN_36M		= 0x48,
	MGN_48M		= 0x60,
	MGN_54M		= 0x6c,

	MGN_MCS0	= 0x80,
	MGN_MCS1	= 0x81,
	MGN_MCS2	= 0x82,
	MGN_MCS3	= 0x83,
	MGN_MCS4	= 0x84,
	MGN_MCS5	= 0x85,
	MGN_MCS6	= 0x86,
	MGN_MCS7	= 0x87,
	MGN_MCS8	= 0x88,
	MGN_MCS9	= 0x89,
	MGN_MCS10	= 0x8a,
	MGN_MCS11	= 0x8b,
	MGN_MCS12	= 0x8c,
	MGN_MCS13	= 0x8d,
	MGN_MCS14	= 0x8e,
	MGN_MCS15	= 0x8f,

	MGN_MCS0_SG	= 0x90,
	MGN_MCS1_SG	= 0x91,
	MGN_MCS2_SG	= 0x92,
	MGN_MCS3_SG	= 0x93,
	MGN_MCS4_SG	= 0x94,
	MGN_MCS5_SG	= 0x95,
	MGN_MCS6_SG	= 0x96,
	MGN_MCS7_SG	= 0x97,
	MGN_MCS8_SG	= 0x98,
	MGN_MCS9_SG	= 0x99,
	MGN_MCS10_SG	= 0x9a,
	MGN_MCS11_SG	= 0x9b,
	MGN_MCS12_SG	= 0x9c,
	MGN_MCS13_SG	= 0x9d,
	MGN_MCS14_SG	= 0x9e,
	MGN_MCS15_SG	= 0x9f,
};

#define FC_QOS_BIT		BIT7

#define IsDataFrame(pdu)	(((pdu[0] & 0x0C) == 0x08) ? true : false)
#define IsLegacyDataFrame(pdu)	(IsDataFrame(pdu) && (!(pdu[0] & FC_QOS_BIT)))
#define IsQoSDataFrame(pframe) \
	((*(u16 *)pframe & (IEEE80211_STYPE_QOS_DATA | IEEE80211_FTYPE_DATA)) \
	 == (IEEE80211_STYPE_QOS_DATA | IEEE80211_FTYPE_DATA))

#define Frame_Order(pframe)	(*(u16 *)pframe & IEEE80211_FCTL_ORDER)

#define SN_LESS(a, b)		(((a - b) & 0x800) != 0)
#define SN_EQUAL(a, b)		(a == b)

#define MAX_DEV_ADDR_SIZE 8

enum {
	/* ACT_CATEGORY */
	ACT_CAT_QOS	= 1,
	ACT_CAT_DLS	= 2,
	ACT_CAT_BA	= 3,
	ACT_CAT_HT	= 7,
	ACT_CAT_WMM	= 17,

	/* TS_ACTION */
	ACT_ADDTSREQ	= 0,
	ACT_ADDTSRSP	= 1,
	ACT_DELTS	= 2,
	ACT_SCHEDULE	= 3,

	/* BA_ACTION */
	ACT_ADDBAREQ	= 0,
	ACT_ADDBARSP	= 1,
	ACT_DELBA	= 2,
};

/* InitialGainOpType */
enum {
	IG_Backup = 0,
	IG_Restore,
	IG_Max
};

typedef enum _LED_CTL_MODE {
	LED_CTL_POWER_ON	 = 1,
	LED_CTL_LINK		 = 2,
	LED_CTL_NO_LINK		 = 3,
	LED_CTL_TX		 = 4,
	LED_CTL_RX		 = 5,
	LED_CTL_SITE_SURVEY	 = 6,
	LED_CTL_POWER_OFF	 = 7,
	LED_CTL_START_TO_LINK	 = 8,
	LED_CTL_START_WPS	 = 9,
	LED_CTL_STOP_WPS	 = 10,
	LED_CTL_START_WPS_BOTTON = 11,
} LED_CTL_MODE;

typedef union _frameqos {
	u16 shortdata;
	u8  chardata[2];
	struct {
		u16 tid:4;
		u16 eosp:1;
		u16 ack_policy:2;
		u16 reserved:1;
		u16 txop:8;
	} field;
} frameqos;

static inline u8 Frame_QoSTID(u8 *buf)
{
	struct ieee80211_hdr_3addr *hdr = (struct ieee80211_hdr_3addr *)buf;
	u16 fc = le16_to_cpu(hdr->frame_control);

	return (u8)((frameqos *)(buf +
		(((fc & IEEE80211_FCTL_TODS) &&
		  (fc & IEEE80211_FCTL_FROMDS)) ? 30 : 24)))->field.tid;
}

enum {
	ERP_NonERPpresent	= 1,
	ERP_UseProtection	= 2,
	ERP_BarkerPreambleMode	= 4,
};

struct bandwidth_autoswitch {
	long threshold_20Mhzto40Mhz;
	long threshold_40Mhzto20Mhz;
	bool bforced_tx20Mhz;
	bool bautoswitch_enable;
};

#define REORDER_WIN_SIZE	128
#define REORDER_ENTRY_NUM	128
typedef struct _RX_REORDER_ENTRY {
	struct list_head	List;
	u16			SeqNum;
	struct ieee80211_rxb	*prxb;
} RX_REORDER_ENTRY, *PRX_REORDER_ENTRY;

typedef enum _Fsync_State{
	Default_Fsync,
	HW_Fsync,
	SW_Fsync
} Fsync_State;

/* Power save mode configured. */
typedef enum _RT_PS_MODE {
	eActive,	/* Active/Continuous access. */
	eMaxPs,		/* Max power save mode. */
	eFastPs		/* Fast power save mode. */
} RT_PS_MODE;

typedef enum _IPS_CALLBACK_FUNCION {
	IPS_CALLBACK_NONE = 0,
	IPS_CALLBACK_MGNT_LINK_REQUEST = 1,
	IPS_CALLBACK_JOIN_REQUEST = 2,
} IPS_CALLBACK_FUNCION;

typedef enum _RT_JOIN_ACTION {
	RT_JOIN_INFRA = 1,
	RT_JOIN_IBSS  = 2,
	RT_START_IBSS = 3,
	RT_NO_ACTION  = 4,
} RT_JOIN_ACTION;

struct ibss_parms {
	u16 atimWin;
};

/* Max num of support rates element: 8,  Max num of ext. support rate: 255. */
#define MAX_NUM_RATES	264

typedef enum _RT_RF_POWER_STATE {
	eRfOn,
	eRfSleep,
	eRfOff
} RT_RF_POWER_STATE;

struct rt_power_save_control {
	/* Inactive Power Save (IPS): disable RF when disconnected */
	bool			bInactivePs;
	bool			bIPSModeBackup;
	bool			bHaltAdapterClkRQ;
	bool			bSwRfProcessing;
	RT_RF_POWER_STATE	eInactivePowerState;
	struct work_struct 	InactivePsWorkItem;
	struct timer_list	InactivePsTimer;

	/* return point for join action */
	IPS_CALLBACK_FUNCION	ReturnPoint;

	/* Recored Parameters for rescheduled JoinRequest */
	bool			bTmpBssDesc;
	RT_JOIN_ACTION		tmpJoinAction;
	struct ieee80211_network tmpBssDesc;

	/* Recored Parameters for rescheduled MgntLinkRequest */
	bool			bTmpScanOnly;
	bool			bTmpActiveScan;
	bool			bTmpFilterHiddenAP;
	bool			bTmpUpdateParms;
	u8			tmpSsidBuf[33];
	OCTET_STRING		tmpSsid2Scan;
	bool			bTmpSsid2Scan;
	u8			tmpNetworkType;
	u8			tmpChannelNumber;
	u16			tmpBcnPeriod;
	u8			tmpDtimPeriod;
	u16			tmpmCap;
	OCTET_STRING		tmpSuppRateSet;
	u8			tmpSuppRateBuf[MAX_NUM_RATES];
	bool			bTmpSuppRate;
	struct ibss_parms	tmpIbpm;
	bool			bTmpIbpm;

	/* Leisre Poswer Save: disable RF if connected but traffic isn't busy */
	bool			bLeisurePs;
	u32			PowerProfile;
	u8			LpsIdleCount;
	u8			RegMaxLPSAwakeIntvl;
	u8			LPSAwakeIntvl;

	/* RF OFF Level */
	u32			CurPsLevel;
	u32			RegRfPsLevel;

	/* Fw Control LPS */
	bool			bFwCtrlLPS;
	u8			FWCtrlPSMode;

	/* Record if there is a link request in IPS RF off progress. */
	bool			LinkReqInIPSRFOffPgs;
	/*
	 * To make sure that connect info should be executed, so we set the
	 * bit to filter the link info which comes after the connect info.
	 */
	bool			BufConnectinfoBefore;
};

enum {
	RF_CHANGE_BY_SW		= BIT31,
	RF_CHANGE_BY_HW		= BIT30,
	RF_CHANGE_BY_PS		= BIT29,
	RF_CHANGE_BY_IPS	= BIT28,
};

/* Firmware related CMD IO. */
typedef enum _FW_CMD_IO_TYPE {
	FW_CMD_DIG_ENABLE = 0,		/* for DIG DM */
	FW_CMD_DIG_DISABLE = 1,
	FW_CMD_DIG_HALT = 2,
	FW_CMD_DIG_RESUME = 3,
	FW_CMD_HIGH_PWR_ENABLE = 4,	/* for High Power DM */
	FW_CMD_HIGH_PWR_DISABLE = 5,
	FW_CMD_RA_RESET = 6,		/* for Rate adaptive DM */
	FW_CMD_RA_ACTIVE = 7,
	FW_CMD_RA_REFRESH_N = 8,
	FW_CMD_RA_REFRESH_BG = 9,
	FW_CMD_IQK_ENABLE = 10,		/* for FW supported IQK */
	FW_CMD_TXPWR_TRACK_ENABLE = 11,	/* Tx power tracking switch */
	FW_CMD_TXPWR_TRACK_DISABLE = 12,/* Tx power tracking switch */
	FW_CMD_PAUSE_DM_BY_SCAN = 13,
	FW_CMD_RESUME_DM_BY_SCAN = 14,
	FW_CMD_MID_HIGH_PWR_ENABLE = 15,
	/* indicate firmware that driver enters LPS, for PS-Poll hardware bug */
	FW_CMD_LPS_ENTER = 16,
	/* indicate firmware that driver leave LPS */
	FW_CMD_LPS_LEAVE = 17,
} FW_CMD_IO_TYPE;

#define RT_MAX_LD_SLOT_NUM	10
struct rt_link_detect {
	u32	NumRecvBcnInPeriod;
	u32	NumRecvDataInPeriod;

	/* number of Rx beacon / CheckForHang_period to determine link status */
	u32	RxBcnNum[RT_MAX_LD_SLOT_NUM];
	/* number of Rx data / CheckForHang_period to determine link status */
	u32	RxDataNum[RT_MAX_LD_SLOT_NUM];
	/* number of CheckForHang period to determine link status */
	u16	SlotNum;
	u16	SlotIndex;

	u32	NumTxOkInPeriod;
	u32	NumRxOkInPeriod;
	bool	bBusyTraffic;
};

/* HT */
#define MAX_RECEIVE_BUFFER_SIZE 9100
extern void HTDebugHTCapability(u8 *CapIE, u8 *TitleString);
extern void HTDebugHTInfo(u8 *InfoIE, u8 *TitleString);

extern void HTSetConnectBwMode(struct ieee80211_device *ieee,
			       HT_CHANNEL_WIDTH Bandwidth,
			       HT_EXTCHNL_OFFSET Offset);
extern void HTUpdateDefaultSetting(struct ieee80211_device *ieee);
extern void HTConstructCapabilityElement(struct ieee80211_device *ieee,
					 u8 *posHTCap, u8 *len, u8 isEncrypt);
extern void HTConstructInfoElement(struct ieee80211_device *ieee,
				   u8 *posHTInfo, u8 *len, u8 isEncrypt);
extern void HTConstructRT2RTAggElement(struct ieee80211_device *ieee,
				       u8 *posRT2RTAgg, u8 *len);
extern void HTOnAssocRsp(struct ieee80211_device *ieee);
extern void HTInitializeHTInfo(struct ieee80211_device *ieee);
extern void HTInitializeBssDesc(PBSS_HT pBssHT);
extern void HTResetSelfAndSavePeerSetting(struct ieee80211_device *ieee,
					  struct ieee80211_network *pNetwork);
extern void HTUpdateSelfAndPeerSetting(struct ieee80211_device *ieee,
				       struct ieee80211_network *pNetwork);
extern u8 HTGetHighestMCSRate(struct ieee80211_device *ieee, u8 *pMCSRateSet,
			      u8 *pMCSFilter);
extern u8 MCS_FILTER_ALL[];
extern u16 MCS_DATA_RATE[2][2][77] ;
extern u8 HTCCheck(struct ieee80211_device *ieee, u8 *pFrame);
extern void HTResetIOTSetting(PRT_HIGH_THROUGHPUT pHTInfo);
extern bool IsHTHalfNmodeAPs(struct ieee80211_device *ieee);
extern u16 HTHalfMcsToDataRate(struct ieee80211_device *ieee, u8 nMcsRate);
extern u16 HTMcsToDataRate(struct ieee80211_device *ieee, u8 nMcsRate);
extern u16  TxCountToDataRate(struct ieee80211_device *ieee, u8 nDataRate);
extern int ieee80211_rx_ADDBAReq(struct ieee80211_device *ieee,
				 struct sk_buff *skb);
extern int ieee80211_rx_ADDBARsp(struct ieee80211_device *ieee,
				 struct sk_buff *skb);
extern int ieee80211_rx_DELBA(struct ieee80211_device *ieee,
			      struct sk_buff *skb);
extern void TsInitAddBA(struct ieee80211_device *ieee, PTX_TS_RECORD pTS,
			u8 Policy, u8 bOverwritePending);
extern void TsInitDelBA(struct ieee80211_device *ieee,
			PTS_COMMON_INFO pTsCommonInfo, TR_SELECT TxRxSelect);
extern void BaSetupTimeOut(unsigned long data);
extern void TxBaInactTimeout(unsigned long data);
extern void RxBaInactTimeout(unsigned long data);
extern void ResetBaEntry(PBA_RECORD pBA);
extern bool GetTs(struct ieee80211_device *ieee, PTS_COMMON_INFO *ppTS,
		  u8 *Addr, u8 TID, TR_SELECT TxRxSelect,  /* Rx:1, Tx:0 */
		  bool bAddNewTs);
extern void TSInitialize(struct ieee80211_device *ieee);
extern void TsStartAddBaProcess(struct ieee80211_device *ieee,
				PTX_TS_RECORD pTxTS);
extern void RemovePeerTS(struct ieee80211_device *ieee, u8 *Addr);
extern void RemoveAllTS(struct ieee80211_device *ieee);

#endif /* __IEEE80211_R8192S_H */
