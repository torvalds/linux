/*
** Id: stats.h#1
*/

/*! \file stats.h
    \brief This file includes statistics support.
*/

/*
** Log: stats.h
 *
 * 07 17 2014 samp.lin
 * NULL
 * Initial version.
 */

/*******************************************************************************
 *						C O M P I L E R	 F L A G S
 ********************************************************************************
 */

/*******************************************************************************
 *						E X T E R N A L	R E F E R E N C E S
 ********************************************************************************
 */
extern UINT_64 u8DrvOwnStart, u8DrvOwnEnd;
extern UINT32 u4DrvOwnMax;
extern BOOLEAN fgIsUnderSuspend;

/*******************************************************************************
*						C O N S T A N T S
********************************************************************************
*/

/* Command to TDLS core module */
typedef enum _STATS_CMD_CORE_ID {
	STATS_CORE_CMD_ENV_REQUEST = 0x00
} STATS_CMD_CORE_ID;

typedef enum _STATS_EVENT_HOST_ID {
	STATS_HOST_EVENT_ENV_REPORT = 0x00,
	STATS_HOST_EVENT_RX_DROP
} STATS_EVENT_HOST_ID;

#define CFG_ARP BIT(0)
#define CFG_DNS BIT(1)
#define CFG_TCP BIT(2)
#define CFG_UDP BIT(3)
#define CFG_EAPOL BIT(4)
#define CFG_DHCP BIT(5)
#define CFG_ICMP BIT(6)

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/
typedef struct _STATS_CMD_CORE_T {

	UINT32 u4Command;	/* STATS_CMD_CORE_ID */

	UINT8 ucStaRecIdx;
	UINT8 ucReserved[3];

	UINT32 u4Reserved[4];

#define STATS_CMD_CORE_RESERVED_SIZE					50
	union {
		UINT8 Reserved[STATS_CMD_CORE_RESERVED_SIZE];
	} Content;

} STATS_CMD_CORE_T;

typedef struct _STATS_INFO_ENV_T {

	BOOLEAN fgIsUsed;	/* TRUE: used */

	/* ------------------- TX ------------------- */
	BOOLEAN fgTxIsRtsUsed;	/* TRUE: we use RTS/CTS currently */
	BOOLEAN fgTxIsRtsEverUsed;	/* TRUE: we ever use RTS/CTS */
	BOOLEAN fgTxIsCtsSelfUsed;	/* TRUE: we use CTS-self */

#define STATS_INFO_TX_PARAM_HW_BW40_OFFSET			0
#define STATS_INFO_TX_PARAM_HW_SHORT_GI20_OFFSET	1
#define STATS_INFO_TX_PARAM_HW_SHORT_GI40_OFFSET	2
#define STATS_INFO_TX_PARAM_USE_BW40_OFFSET			3
#define STATS_INFO_TX_PARAM_USE_SHORT_GI_OFFSET		4
#define STATS_INFO_TX_PARAM_NO_ACK_OFFSET			5
	UINT_8 ucTxParam;

	UINT_8 ucStaRecIdx;
	UINT_8 ucReserved1[2];

	UINT32 u4TxDataCntAll;	/* total tx count from host */
	UINT32 u4TxDataCntOK;	/* total tx ok count to air */
	UINT32 u4TxDataCntErr;	/* total tx err count to air */

	/* WLAN_STATUS_BUFFER_RETAINED ~ WLAN_STATUS_PACKET_LIFETIME_ERROR */
	UINT32 u4TxDataCntErrType[6];	/* total tx err count for different type to air */

	UINT_8 ucTxRate1NonHTMax;
	UINT_8 ucTxRate1HTMax;
	UINT32 u4TxRateCntNonHT[16];	/* tx done rate */
	UINT32 u4TxRateCntHT[16];	/* tx done rate */

	UINT_8 ucTxAggBitmap;	/* TX BA sessions TID0 ~ TID7 */
	UINT_8 ucTxPeerAggMaxSize;

	/* ------------------- RX ------------------- */
	BOOLEAN fgRxIsRtsUsed;	/* TRUE: peer uses RTS/CTS currently */
	BOOLEAN fgRxIsRtsEverUsed;	/* TRUE: peer ever uses RTS/CTS */

	UINT_8 ucRcvRcpi;
	UINT_8 ucHwChanNum;
	BOOLEAN fgRxIsShortGI;
	UINT_8 ucReserved2[1];

	UINT32 u4RxDataCntAll;	/* total rx count from peer */
	UINT32 u4RxDataCntErr;	/* total rx err count */
	UINT32 u4RxRateCnt[3][16];	/* [0]:CCK, [1]:OFDM, [2]:MIXED (skip green mode) */

	UINT_8 ucRxAggBitmap;	/* RX BA sessions TID0 ~ TID7 */
	UINT_8 ucRxAggMaxSize;

#define STATS_INFO_PHY_MODE_CCK					0
#define STATS_INFO_PHY_MODE_OFDM				1
#define STATS_INFO_PHY_MODE_HT					2
#define STATS_INFO_PHY_MODE_VHT					3
	UINT_8 ucBssSupPhyMode;	/* CCK, OFDM, HT, or VHT BSS */

	UINT_8 ucVersion;	/* the version of statistics info environment */

	/* ------------------- Delay ------------------- */
#define STATS_AIR_DELAY_INT						500	/* 500 byte */

	/* delay in firmware from host to MAC */
	/* unit: us, for 500B, 1000B, max */
	UINT32 u4StayIntMaxH2M[3], u4StayIntMinH2M[3], u4StayIntAvgH2M[3];

	/* delay in firmware from MAC to TX done */
	/* unit: 32us, for 500B, 1000B, max */
	UINT32 u4AirDelayMax[3], u4AirDelayMin[3], u4AirDelayAvg[3];

	/* delay in firmware from host to TX done */
	/* unit: us, for 500B, 1000B, max */
	UINT32 u4StayIntMax[3], u4StayIntMin[3], u4StayIntAvg[3];
	UINT32 u4StayIntMaxSysTime[3];

	/* delay in firmware from driver to TX done */
	/* unit: us, for 500B, 1000B, max */
	UINT32 u4StayIntMaxD2T[3], u4StayIntMinD2T[3], u4StayIntAvgD2T[3];

	/* delay count in firmware from host to TX done */
	/* u4StayIntByConst: divide 4 fix partitions to count each delay in firmware */
#define STATS_STAY_INT_CONST					1	/* 1ms */
#define STATS_STAY_INT_CONST_2					5
#define STATS_STAY_INT_CONST_3					10
#define STATS_STAY_INT_CONST_4					15
#define STATS_STAY_INT_CONST_NUM				4
	UINT32 u4StayIntByConst[STATS_STAY_INT_CONST_NUM];

	/*
	   u4StayIntMaxPast: past maximum delay in firmware
	   u4StayIntCnt[]: divide 4 partitions to count each delay in firmware
	 */
#define STATS_STAY_INT_NUM						4
	UINT32 u4StayIntMaxPast;
	UINT32 u4StayIntCnt[STATS_STAY_INT_NUM + 1];

	/* delay count in firmware from driver to HIF */
	/* u4StayIntD2HByConst: divide 4 fix partitions to count each delay in firmware */
#define STATS_STAY_INT_D2H_CONST				10	/* 10ms */
#define STATS_STAY_INT_D2H_CONST_2				20
#define STATS_STAY_INT_D2H_CONST_3				30
#define STATS_STAY_INT_D2H_CONST_4				40
#define STATS_STAY_INT_D2H_CONST_NUM			4
	UINT32 u4StayIntD2HByConst[STATS_STAY_INT_D2H_CONST_NUM];

	/* unit: us, for 500B, 1000B, max */
	UINT32 u4StayIntMaxRx[3], u4StayIntMinRx[3], u4StayIntAvgRx[3];

	/* ------------------- Others ------------------- */
	UINT32 u4NumOfChanChange;	/* total channel change count */
	UINT32 u4NumOfRetryCnt;	/* total TX retry count */
	UINT32 u4RxFifoFullCnt;	/* counter of the number of the packets which
				   pass RFCR but are dropped due to FIFO full. */
	UINT32 u4PsIntMax;	/* maximum time from ps to active */
	UINT_8 ucNumOfPsChange;	/* peer power save change count */
	UINT_8 ucReserved3[3];

	UINT32 u4ReportSysTime;	/* firmware system time */
	UINT32 u4RxDataCntOk;	/* total rx count to hif */

	/* V4 */
	UINT32 u4RxRateRetryCnt[3][16];	/* [0]:CCK, [1]:OFDM, [2]:MIXED (skip green mode) */
	UINT32 au4ChanIdleCnt[10];	/* past Channel idle count in unit of slot */

	/* V5 */
	UINT32 u4BtContUseTime;	/* the air time that BT continuous occypy */

	/* V6 */
	UINT32 u4LastTxOkTime;	/* last time we tx ok to the station */

	/* V7 */
	UINT_8 ucBtWfCoexGrantCnt[8];	/* [0]:WF Rx Grant Cnt[1]: WF Tx Grant Cnt[2]: WF Grant with Priority1 */
	/* [4]:BT Rx Grant Cnt[5]: BT Tx Grant Cnt[6]: BT Grant with Priority1 */

	/* V8 */
	UINT_32 u4RxMacFreeDescCnt[6];
	UINT_32 u4RxHifFreeDescCnt[6];

	/* V9 */
#define STATS_MAX_RX_DROP_TYPE			20
	UINT32 u4NumOfRxDrop[STATS_MAX_RX_DROP_TYPE];

	/* V10 */
	UINT_32 u4NumOfTxDone;	/* number of all packets (data/man/ctrl) tx done */
	UINT_32 u4NumOfTxDoneFixRate;	/* number of done rate = 0 */
	UINT_32 u4NumOfTxDoneErrRate;	/* number of error done rate */
	UINT_32 u4NumOfNullTxDone;	/* number of null tx done */
	UINT_32 u4NumOfQoSNullTxDone;	/* number of QoS-null tx done */

	/* V11 */
	/* delay in firmware from HIF RX to HIF RX Done */
	/* unit: us, for 500B, 1000B, max */
	UINT32 u4StayIntMaxHR2HRD[3], u4StayIntMinHR2HRD[3], u4StayIntAvgHR2HRD[3];

	/* V12 */
	UINT32 u4AirDelayTotal;	/* agg all the air delay */

	/* V13 */
	UINT32 u4CurrChnlInfo; /* add current channel information */

	UINT_8 ucReserved_rate[4];	/* the field must be the last one */
} STATS_INFO_ENV_T;

/*******************************************************************************
*						M A C R O   D E C L A R A T I O N S
********************************************************************************
*/
#if (CFG_SUPPORT_STATISTICS == 1)

#define STATS_ENV_REPORT_DETECT				statsEnvReportDetect

#define STATS_RX_REORDER_FALL_AHEAD_INC(__StaRec__) \
{ \
	(__StaRec__)->u4RxReorderFallAheadCnt++; \
}

#define STATS_RX_REORDER_FALL_BEHIND_INC(__StaRec__) \
{ \
	(__StaRec__)->u4RxReorderFallBehindCnt++; \
}

#define STATS_RX_REORDER_HOLE_INC(__StaRec__) \
{ \
	(__StaRec__)->u4RxReorderHoleCnt++; \
}

#define STATS_RX_REORDER_HOLE_TIMEOUT_INC(__StaRec__, __IsTimeout__) \
{ \
	if ((__IsTimeout__) == TRUE) \
		(__StaRec__)->u4RxReorderHoleTimeoutCnt++; \
}

#define STATS_RX_ARRIVE_TIME_RECORD(__SwRfb__) \
{ \
	(__SwRfb__)->rRxTime = StatsEnvTimeGet(); \
}

#define STATS_RX_PASS2OS_INC				StatsEnvRxDone

#define STATS_RX_PKT_INFO_DISPLAY			StatsRxPktInfoDisplay

#define STATS_TX_TIME_ARRIVE(__Skb__)										\
do {														\
	UINT_64 __SysTime;											\
	__SysTime = StatsEnvTimeGet(); /* us */									\
	GLUE_SET_PKT_XTIME(__Skb__, __SysTime);									\
} while (FALSE)

#define STATS_TX_TIME_TO_HIF				StatsEnvTxTime2Hif

#define STATS_TX_PKT_CALLBACK				StatsTxPktCallBack
#define STATS_TX_PKT_DONE_INFO_DISPLAY			StatsTxPktDoneInfoDisplay

#define STATS_DRIVER_OWN_RESET() \
{ \
	u4DrvOwnMax = 0; \
}
#define STATS_DRIVER_OWN_START_RECORD() \
{ \
	u8DrvOwnStart = StatsEnvTimeGet(); \
}
#define STATS_DRIVER_OWN_END_RECORD() \
{ \
	u8DrvOwnEnd = StatsEnvTimeGet(); \
}
#define STATS_DRIVER_OWN_STOP()				\
do {							\
	UINT32 __Diff;					\
	__Diff = (UINT32)(u8DrvOwnEnd - u8DrvOwnStart);	\
	if (__Diff > u4DrvOwnMax)			\
		u4DrvOwnMax = __Diff;			\
} while (FALSE)

#else

#define STATS_ENV_REPORT_DETECT(__Adapter__, __StaRecIndex__)

#define STATS_RX_REORDER_FALL_AHEAD_INC(__StaRec__)
#define STATS_RX_REORDER_FALL_BEHIND_INC(__StaRec__)
#define STATS_RX_REORDER_HOLE_INC(__StaRec__)
#define STATS_RX_REORDER_HOLE_TIMEOUT_INC(__StaRec__, __IsTimeout__)
#define STATS_RX_PASS2OS_INC(__StaRec__, __SwRfb__)
#define STATS_RX_PKT_INFO_DISPLAY(__Pkt__)

#define STATS_TX_TIME_ARRIVE(__Skb__)
#define STATS_TX_TIME_TO_HIF(__MsduInfo__, __HwTxHeader__)
#define STATS_TX_PKT_CALLBACK(__Pkt__, __fgIsNeedAck__)
#define STATS_TX_PKT_DONE_INFO_DISPLAY(__Adapter__, __Event__)

#define STATS_DRIVER_OWN_RESET()
#define STATS_DRIVER_OWN_START_RECORD()
#define STATS_DRIVER_OWN_END_RECORD()
#define STATS_DRIVER_OWN_STOP()
#endif /* CFG_SUPPORT_STATISTICS */

/*******************************************************************************
*						F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

/*******************************************************************************
*						P R I V A T E   D A T A
********************************************************************************
*/

/*******************************************************************************
*						P R I V A T E  F U N C T I O N S
********************************************************************************
*/

/*******************************************************************************
*						P U B L I C  F U N C T I O N S
********************************************************************************
*/

VOID statsEnvReportDetect(ADAPTER_T *prAdapter, UINT8 ucStaRecIndex);

VOID StatsEnvRxDone(STA_RECORD_T *prStaRec, SW_RFB_T *prSwRfb);

UINT_64 StatsEnvTimeGet(VOID);

VOID StatsEnvTxTime2Hif(MSDU_INFO_T *prMsduInfo, HIF_TX_HEADER_T *prHwTxHeader);

VOID statsEventHandle(GLUE_INFO_T *prGlueInfo, UINT8 *prInBuf, UINT32 u4InBufLen);

VOID StatsRxPktInfoDisplay(UINT_8 *pPkt);

VOID StatsTxPktCallBack(UINT_8 *pPkt, P_MSDU_INFO_T prMsduInfo);

VOID StatsTxPktDoneInfoDisplay(ADAPTER_T *prAdapter, UINT_8 *pucEvtBuf);

VOID StatsSetCfgTxDone(UINT_16 u2Cfg, BOOLEAN fgSet);

UINT_16 StatsGetCfgTxDone(VOID);

/* End of stats.h */
