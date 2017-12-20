/*
** Id: stats.c#1
*/

/*! \file stats.c
    \brief This file includes statistics support.
*/

/*
** Log: stats.c
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
#include "precomp.h"

enum EVENT_TYPE {
	EVENT_RX,
	EVENT_TX,
	EVENT_TX_DONE
};
/*******************************************************************************
*						C O N S T A N T S
********************************************************************************
*/

/*******************************************************************************
*						F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/
static void statsInfoEnvDisplay(GLUE_INFO_T *prGlueInfo, UINT8 *prInBuf, UINT32 u4InBufLen);

static WLAN_STATUS
statsInfoEnvRequest(ADAPTER_T *prAdapter, VOID *pvSetBuffer, UINT_32 u4SetBufferLen, UINT_32 *pu4SetInfoLen);

/*******************************************************************************
*						P U B L I C   D A T A
********************************************************************************
*/
UINT_64 u8DrvOwnStart, u8DrvOwnEnd;
UINT32 u4DrvOwnMax = 0;
#define CFG_USER_LOAD 0
static UINT_16 su2TxDoneCfg = CFG_DHCP | CFG_ICMP | CFG_EAPOL;
/*******************************************************************************
*						P R I V A T E  F U N C T I O N S
********************************************************************************
*/

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to display all environment log.
*
* \param[in] prGlueInfo		Pointer to the Adapter structure
* \param[in] prInBuf		A pointer to the command string buffer, from u4EventSubId
* \param[in] u4InBufLen	The length of the buffer
* \param[out] None
*
* \retval None
*
*/
/*----------------------------------------------------------------------------*/
static void statsInfoEnvDisplay(GLUE_INFO_T *prGlueInfo, UINT8 *prInBuf, UINT32 u4InBufLen)
{
	P_ADAPTER_T prAdapter;
	STA_RECORD_T *prStaRec;
	UINT32 u4NumOfInfo, u4InfoId;
	UINT32 u4RxErrBitmap;
	STATS_INFO_ENV_T *prInfo;
	UINT32 u4Total, u4RateId;

/*
[wlan] statsInfoEnvRequest: (INIT INFO) statsInfoEnvRequest cmd ok.
[wlan] statsEventHandle: (INIT INFO) <stats> statsEventHandle: Rcv a event
[wlan] statsEventHandle: (INIT INFO) <stats> statsEventHandle: Rcv a event: 0
[wlan] statsInfoEnvDisplay: (INIT INFO) <stats> Display stats for [00:0c:43:31:35:97]:

[wlan] statsInfoEnvDisplay: (INIT INFO) <stats> TPAM(0x0) RTS(0 0) BA(0x1 0) OK(9 9 xxx) ERR(0 0 0 0 0 0 0)
	TPAM (bit0: enable 40M, bit1: enable 20 short GI, bit2: enable 40 short GI,
		bit3: use 40M TX, bit4: use short GI TX, bit5: use no ack)
	RTS (1st: current use RTS/CTS, 2nd: ever use RTS/CTS)
	BA (1st: TX session BA bitmap for TID0 ~ TID7, 2nd: peer receive maximum agg number)
	OK (1st: total number of tx packet from host, 2nd: total number of tx ok, system time last TX OK)
	ERR (1st: total number of tx err, 2nd ~ 7st: total number of
		WLAN_STATUS_BUFFER_RETAINED, WLAN_STATUS_PACKET_FLUSHED, WLAN_STATUS_PACKET_AGING_TIMEOUT,
		WLAN_STATUS_PACKET_MPDU_ERROR, WLAN_STATUS_PACKET_RTS_ERROR, WLAN_STATUS_PACKET_LIFETIME_ERROR)

[wlan] statsInfoEnvDisplay: (INIT INFO) <stats> TRATE (6 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0) (0 0 0 0 0 0 0 3)
	TX rate count (1M 2 5.5 11 NA NA NA NA 48 24 12 6 54 36 18 9) (MCS0 ~ MCS7)

[wlan] statsInfoEnvDisplay: (INIT INFO) <stats> RX(148 1 0) BA(0x1 64) OK(2 2) ERR(0)
	RX (1st: latest RCPI, 2nd: chan num)
	BA (1st: RX session BA bitmap for TID0 ~ TID7, 2nd: our receive maximum agg number)
	OK (number of rx packets without error, number of rx packets to OS)
	ERR (number of rx packets with error)

[wlan] statsInfoEnvDisplay: (INIT INFO) <stats> RCCK (0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0)
	CCK MODE (1 2 5.5 11M)
[wlan] statsInfoEnvDisplay: (INIT INFO) <stats> ROFDM (0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0)
	OFDM MODE (NA NA NA NA 6 9 12 18 24 36 48 54M)
[wlan] statsInfoEnvDisplay: (INIT INFO) <stats> RHT (0 0 0 0 0 0 0 2 0 0 0 0 0 0 0 0)
	MIXED MODE (number of rx packets with MCS0 ~ MCS15)

[wlan] statsInfoEnvDisplay: (INIT INFO) <stats> StayIntH2M us (29 29 32) (0 0 0) (0 0 0)
	delay from HIF to MAC own bit=1 (min, avg, max for 500B) (min, avg, max for 1000B) (min, avg, max for others)

[wlan] statsInfoEnvDisplay: (INIT INFO) <stats> AirTime us (608 864 4480) (0 0 0) (0 0 0)
	delay from MAC start TX to MAC TX done

[wlan] statsInfoEnvDisplay: (INIT INFO) <stats> StayInt us (795 1052 4644_4504) (0 0 0_0) (0 0 0_0)
	delay from HIF to MAC TX done (min, avg, max_system time for 500B)

[wlan] statsInfoEnvDisplay: (INIT INFO) <stats> StayIntD2T us (795 1052 4644) (0 0 0) (0 0 0)
	delay from driver to MAC TX done (min, avg, max for 500B)

[wlan] statsInfoEnvDisplay: (INIT INFO) <stats> StayIntR_M2H us (37 40 58) (0 0 0) (0 0 0)
	delay from MAC to HIF (min, avg, max for 500B)

[wlan] statsInfoEnvDisplay: (INIT INFO) <stats> StayIntR_H2D us (0 0 0) (0 0 0) (0 0 0)
	delay from HIF to Driver OS (min, avg, max for 500B)

[wlan] statsInfoEnvDisplay: (INIT INFO) <stats> StayCntD2H unit:10ms (10 0 0 0)
	delay count from Driver to HIF (count in 0~10ms, 10~20ms, 20~30ms, others)

[wlan] statsInfoEnvDisplay: (INIT INFO) <stats> StayCnt unit:1ms (6 3 0 1)
	delay count from HIF to TX DONE (count in 0~1ms, 1~5ms, 5~10ms, others)

[wlan] statsInfoEnvDisplay: (INIT INFO) <stats> StayCnt (0~1161:7) (1161~2322:2) (2322~3483:0) (3483~4644:0) (4644~:1)
	delay count from HIF to TX DONE (count in 0~1161 ticks, 1161~2322, 2322~3483, 3483~4644, others)

[wlan] statsInfoEnvDisplay: (INIT INFO) <stats> OTHER (61877) (0) (38) (0) (0) (0ms)
	Channel idle time, scan count, channel change count, empty tx quota count,
	power save change count from active to PS, maximum delay from PS to active
*/

	/* init */
	prAdapter = prGlueInfo->prAdapter;
	/*prInfo = &rStatsInfoEnv;*/
	prInfo = kalMemAlloc(sizeof(STATS_INFO_ENV_T), VIR_MEM_TYPE);
	if (prInfo == NULL) {
		DBGLOG(RX, INFO, "prInfo alloc fail");
		return;
	}

	kalMemZero(prInfo, sizeof(STATS_INFO_ENV_T));

	if (u4InBufLen > sizeof(STATS_INFO_ENV_T))
		u4InBufLen = sizeof(STATS_INFO_ENV_T);

	/* parse */
	u4NumOfInfo = *(UINT32 *) prInBuf;
	u4RxErrBitmap = *(UINT32 *) (prInBuf + 4);

	/* print */
	for (u4InfoId = 0; u4InfoId < u4NumOfInfo; u4InfoId++) {
		/*
		   use u4InBufLen, not sizeof(rStatsInfoEnv)
		   because the firmware version maybe not equal to driver version
		 */
		kalMemCopy(prInfo, prInBuf + 8, u4InBufLen);

		prStaRec = cnmGetStaRecByIndex(prAdapter, prInfo->ucStaRecIdx);
		if (prStaRec == NULL)
			continue;

		DBGLOG(RX, INFO, "<stats> Display stats for [%pM]: %uB\n",
				    prStaRec->aucMacAddr, (UINT32) sizeof(STATS_INFO_ENV_T));

		if (prStaRec->ucStatsGenDisplayCnt++ > 10) {
			/* display general statistics information every 10 * (5 or 10s) */
			DBGLOG(RX, INFO, "<stats> TBA(0x%x %u) RBA(0x%x %u)\n",
					    prInfo->ucTxAggBitmap, prInfo->ucTxPeerAggMaxSize,
					    prInfo->ucRxAggBitmap, prInfo->ucRxAggMaxSize);
			prStaRec->ucStatsGenDisplayCnt = 0;
		}

		if (prInfo->u4TxDataCntErr == 0) {
			DBGLOG(RX, INFO, "<stats> TOS(%u) OK(%u %u)\n",
					    (UINT32) prGlueInfo->rNetDevStats.tx_packets,
					    prInfo->u4TxDataCntAll, prInfo->u4TxDataCntOK);
		} else {
			DBGLOG(RX, INFO, "<stats> TOS(%u) OK(%u %u) ERR(%u)\n",
					    (UINT32) prGlueInfo->rNetDevStats.tx_packets,
					    prInfo->u4TxDataCntAll, prInfo->u4TxDataCntOK, prInfo->u4TxDataCntErr);
			DBGLOG(RX, INFO, "<stats> ERR type(%u %u %u %u %u %u)\n",
					    prInfo->u4TxDataCntErrType[0], prInfo->u4TxDataCntErrType[1],
					    prInfo->u4TxDataCntErrType[2], prInfo->u4TxDataCntErrType[3],
					    prInfo->u4TxDataCntErrType[4], prInfo->u4TxDataCntErrType[5]);
		}

		for (u4RateId = 1, u4Total = 0; u4RateId < 16; u4RateId++)
			u4Total += prInfo->u4TxRateCntNonHT[u4RateId];
		if (u4Total > 0) {
			DBGLOG(RX, INFO, "<stats> non-HT TRATE (%u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u)\n",
					    prInfo->u4TxRateCntNonHT[0], prInfo->u4TxRateCntNonHT[1],
					    prInfo->u4TxRateCntNonHT[2], prInfo->u4TxRateCntNonHT[3],
					    prInfo->u4TxRateCntNonHT[4], prInfo->u4TxRateCntNonHT[5],
					    prInfo->u4TxRateCntNonHT[6], prInfo->u4TxRateCntNonHT[7],
					    prInfo->u4TxRateCntNonHT[8], prInfo->u4TxRateCntNonHT[9],
					    prInfo->u4TxRateCntNonHT[10], prInfo->u4TxRateCntNonHT[11],
					    prInfo->u4TxRateCntNonHT[12], prInfo->u4TxRateCntNonHT[13],
					    prInfo->u4TxRateCntNonHT[14], prInfo->u4TxRateCntNonHT[15]);
		}
		if (prInfo->u4TxRateCntNonHT[0] > 0) {
			DBGLOG(RX, INFO, "<stats> HT TRATE (1M %u) (%u %u %u %u %u %u %u %u)\n",
					    prInfo->u4TxRateCntNonHT[0],
					    prInfo->u4TxRateCntHT[0], prInfo->u4TxRateCntHT[1],
					    prInfo->u4TxRateCntHT[2], prInfo->u4TxRateCntHT[3],
					    prInfo->u4TxRateCntHT[4], prInfo->u4TxRateCntHT[5],
					    prInfo->u4TxRateCntHT[6], prInfo->u4TxRateCntHT[7]);
		} else {
			DBGLOG(RX, INFO, "<stats> HT TRATE (%u %u %u %u %u %u %u %u)\n",
					    prInfo->u4TxRateCntHT[0], prInfo->u4TxRateCntHT[1],
					    prInfo->u4TxRateCntHT[2], prInfo->u4TxRateCntHT[3],
					    prInfo->u4TxRateCntHT[4], prInfo->u4TxRateCntHT[5],
					    prInfo->u4TxRateCntHT[6], prInfo->u4TxRateCntHT[7]);
		}

		if ((prStaRec->u4RxReorderFallAheadCnt != 0) ||
		    (prStaRec->u4RxReorderFallBehindCnt != 0) || (prStaRec->u4RxReorderHoleCnt != 0)) {
			DBGLOG(RX, INFO, "<stats> TREORDER (%u %u %u)\n",
					    prStaRec->u4RxReorderFallAheadCnt,
					    prStaRec->u4RxReorderFallBehindCnt, prStaRec->u4RxReorderHoleCnt);
		}

		if (prInfo->u4RxDataCntErr == 0) {
			DBGLOG(RX, INFO, "<stats> ROK(%u %u)\n",
					    prInfo->u4RxDataCntAll, prStaRec->u4StatsRxPassToOsCnt);
		} else {
			DBGLOG(RX, INFO, "<stats> ROK(%u %u) ERR(%u)\n",
					    prInfo->u4RxDataCntAll, prStaRec->u4StatsRxPassToOsCnt,
					    prInfo->u4RxDataCntErr);
		}

		for (u4RateId = 1, u4Total = 0; u4RateId < 16; u4RateId++)
			u4Total += prInfo->u4RxRateCnt[0][u4RateId] + prInfo->u4RxRateRetryCnt[0][u4RateId];
		if (u4Total > 0) {
			for (u4RateId = 0, u4Total = 0; u4RateId < 16; u4RateId++)
				u4Total += prInfo->u4RxRateRetryCnt[0][u4RateId];
			if (u4Total > 0) {
				DBGLOG(RX, INFO,
					"<stats> RCCK (%u %u) (%u %u) (%u %u) (%u %u) (%u %u) (%u %u) (%u %u)\n"
					 "(%u %u)(%u %u) (%u %u) (%u %u) (%u %u) (%u %u) (%u %u) (%u %u) (%u %u)\n",
						    prInfo->u4RxRateCnt[0][0], prInfo->u4RxRateRetryCnt[0][0],
						    prInfo->u4RxRateCnt[0][1], prInfo->u4RxRateRetryCnt[0][1],
						    prInfo->u4RxRateCnt[0][2], prInfo->u4RxRateRetryCnt[0][2],
						    prInfo->u4RxRateCnt[0][3], prInfo->u4RxRateRetryCnt[0][3],
						    prInfo->u4RxRateCnt[0][4], prInfo->u4RxRateRetryCnt[0][4],
						    prInfo->u4RxRateCnt[0][5], prInfo->u4RxRateRetryCnt[0][5],
						    prInfo->u4RxRateCnt[0][6], prInfo->u4RxRateRetryCnt[0][6],
						    prInfo->u4RxRateCnt[0][7], prInfo->u4RxRateRetryCnt[0][7],
						    prInfo->u4RxRateCnt[0][8], prInfo->u4RxRateRetryCnt[0][8],
						    prInfo->u4RxRateCnt[0][9], prInfo->u4RxRateRetryCnt[0][9],
						    prInfo->u4RxRateCnt[0][10], prInfo->u4RxRateRetryCnt[0][10],
						    prInfo->u4RxRateCnt[0][11], prInfo->u4RxRateRetryCnt[0][11],
						    prInfo->u4RxRateCnt[0][12], prInfo->u4RxRateRetryCnt[0][12],
						    prInfo->u4RxRateCnt[0][13], prInfo->u4RxRateRetryCnt[0][13],
						    prInfo->u4RxRateCnt[0][14], prInfo->u4RxRateRetryCnt[0][14],
						    prInfo->u4RxRateCnt[0][15], prInfo->u4RxRateRetryCnt[0][15]);
			} else {
				DBGLOG(RX, INFO, "<stats> RCCK (%u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u)\n",
						    prInfo->u4RxRateCnt[0][0],
						    prInfo->u4RxRateCnt[0][1],
						    prInfo->u4RxRateCnt[0][2],
						    prInfo->u4RxRateCnt[0][3],
						    prInfo->u4RxRateCnt[0][4],
						    prInfo->u4RxRateCnt[0][5],
						    prInfo->u4RxRateCnt[0][6],
						    prInfo->u4RxRateCnt[0][7],
						    prInfo->u4RxRateCnt[0][8],
						    prInfo->u4RxRateCnt[0][9],
						    prInfo->u4RxRateCnt[0][10],
						    prInfo->u4RxRateCnt[0][11],
						    prInfo->u4RxRateCnt[0][12],
						    prInfo->u4RxRateCnt[0][13],
						    prInfo->u4RxRateCnt[0][14], prInfo->u4RxRateCnt[0][15]);
			}
		} else {
			if ((prInfo->u4RxRateCnt[0][0] + prInfo->u4RxRateRetryCnt[0][0]) > 0) {
				DBGLOG(RX, INFO, "<stats> RCCK (%u %u)\n",
						    prInfo->u4RxRateCnt[0][0], prInfo->u4RxRateRetryCnt[0][0]);
			}
		}

		for (u4RateId = 0, u4Total = 0; u4RateId < 16; u4RateId++)
			u4Total += prInfo->u4RxRateCnt[1][u4RateId] + prInfo->u4RxRateRetryCnt[1][u4RateId];
		if (u4Total > 0) {
			for (u4RateId = 0, u4Total = 0; u4RateId < 16; u4RateId++)
				u4Total += prInfo->u4RxRateRetryCnt[1][u4RateId];
			if (u4Total > 0) {
				DBGLOG(RX, INFO,
					"<stats> ROFDM (%u %u) (%u %u) (%u %u) (%u %u) (%u %u) (%u %u) (%u %u)\n"
					 "(%u %u)(%u %u) (%u %u) (%u %u) (%u %u) (%u %u) (%u %u) (%u %u) (%u %u)\n",
						    prInfo->u4RxRateCnt[1][0], prInfo->u4RxRateRetryCnt[1][0],
						    prInfo->u4RxRateCnt[1][1], prInfo->u4RxRateRetryCnt[1][1],
						    prInfo->u4RxRateCnt[1][2], prInfo->u4RxRateRetryCnt[1][2],
						    prInfo->u4RxRateCnt[1][3], prInfo->u4RxRateRetryCnt[1][3],
						    prInfo->u4RxRateCnt[1][4], prInfo->u4RxRateRetryCnt[1][4],
						    prInfo->u4RxRateCnt[1][5], prInfo->u4RxRateRetryCnt[1][5],
						    prInfo->u4RxRateCnt[1][6], prInfo->u4RxRateRetryCnt[1][6],
						    prInfo->u4RxRateCnt[1][7], prInfo->u4RxRateRetryCnt[1][7],
						    prInfo->u4RxRateCnt[1][8], prInfo->u4RxRateRetryCnt[1][8],
						    prInfo->u4RxRateCnt[1][9], prInfo->u4RxRateRetryCnt[1][9],
						    prInfo->u4RxRateCnt[1][10], prInfo->u4RxRateRetryCnt[1][10],
						    prInfo->u4RxRateCnt[1][11], prInfo->u4RxRateRetryCnt[1][11],
						    prInfo->u4RxRateCnt[1][12], prInfo->u4RxRateRetryCnt[1][12],
						    prInfo->u4RxRateCnt[1][13], prInfo->u4RxRateRetryCnt[1][13],
						    prInfo->u4RxRateCnt[1][14], prInfo->u4RxRateRetryCnt[1][14],
						    prInfo->u4RxRateCnt[1][15], prInfo->u4RxRateRetryCnt[1][15]);
			} else {
				DBGLOG(RX, INFO, "<stats> ROFDM (%u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u)\n",
						    prInfo->u4RxRateCnt[1][0],
						    prInfo->u4RxRateCnt[1][1],
						    prInfo->u4RxRateCnt[1][2],
						    prInfo->u4RxRateCnt[1][3],
						    prInfo->u4RxRateCnt[1][4],
						    prInfo->u4RxRateCnt[1][5],
						    prInfo->u4RxRateCnt[1][6],
						    prInfo->u4RxRateCnt[1][7],
						    prInfo->u4RxRateCnt[1][8],
						    prInfo->u4RxRateCnt[1][9],
						    prInfo->u4RxRateCnt[1][10],
						    prInfo->u4RxRateCnt[1][11],
						    prInfo->u4RxRateCnt[1][12],
						    prInfo->u4RxRateCnt[1][13],
						    prInfo->u4RxRateCnt[1][14], prInfo->u4RxRateCnt[1][15]);
			}
		}

		for (u4RateId = 0, u4Total = 0; u4RateId < 16; u4RateId++)
			u4Total += prInfo->u4RxRateCnt[2][u4RateId] + prInfo->u4RxRateRetryCnt[2][u4RateId];
		if (u4Total > 0) {
			for (u4RateId = 0, u4Total = 0; u4RateId < 16; u4RateId++)
				u4Total += prInfo->u4RxRateRetryCnt[2][u4RateId];
			if (u4Total > 0) {
				DBGLOG(RX, INFO, "<stats> RHT\n"
						    "(%u %u) (%u %u) (%u %u) (%u %u) (%u %u) (%u %u) (%u %u) (%u %u)\n",
						    prInfo->u4RxRateCnt[2][0], prInfo->u4RxRateRetryCnt[2][0],
						    prInfo->u4RxRateCnt[2][1], prInfo->u4RxRateRetryCnt[2][1],
						    prInfo->u4RxRateCnt[2][2], prInfo->u4RxRateRetryCnt[2][2],
						    prInfo->u4RxRateCnt[2][3], prInfo->u4RxRateRetryCnt[2][3],
						    prInfo->u4RxRateCnt[2][4], prInfo->u4RxRateRetryCnt[2][4],
						    prInfo->u4RxRateCnt[2][5], prInfo->u4RxRateRetryCnt[2][5],
						    prInfo->u4RxRateCnt[2][6], prInfo->u4RxRateRetryCnt[2][6],
						    prInfo->u4RxRateCnt[2][7], prInfo->u4RxRateRetryCnt[2][7]);
			} else {
				DBGLOG(RX, INFO, "<stats> RHT (%u %u %u %u %u %u %u %u)\n",
						    prInfo->u4RxRateCnt[2][0],
						    prInfo->u4RxRateCnt[2][1],
						    prInfo->u4RxRateCnt[2][2],
						    prInfo->u4RxRateCnt[2][3],
						    prInfo->u4RxRateCnt[2][4],
						    prInfo->u4RxRateCnt[2][5],
						    prInfo->u4RxRateCnt[2][6], prInfo->u4RxRateCnt[2][7]);
			}
		}

		/* RX drop counts */
		for (u4RateId = 0, u4Total = 0; u4RateId < 20; u4RateId++)
			u4Total += prInfo->u4NumOfRxDrop[u4RateId];
		if (u4Total > 0) {
			DBGLOG(RX, INFO, "<stats> RX Drop Count: (%u) (%u) (%u) (%u) (%u) (%u) (%u) (%u)\n"
					    " (%u) (%u) (%u) (%u) (%u) (%u) (%u) (%u) (%u) (%u) (%u) (%u)\n",
					    prInfo->u4NumOfRxDrop[0], prInfo->u4NumOfRxDrop[1],
					    prInfo->u4NumOfRxDrop[2], prInfo->u4NumOfRxDrop[3],
					    prInfo->u4NumOfRxDrop[4], prInfo->u4NumOfRxDrop[5],
					    prInfo->u4NumOfRxDrop[6], prInfo->u4NumOfRxDrop[7],
					    prInfo->u4NumOfRxDrop[8], prInfo->u4NumOfRxDrop[9],
					    prInfo->u4NumOfRxDrop[10], prInfo->u4NumOfRxDrop[11],
					    prInfo->u4NumOfRxDrop[12], prInfo->u4NumOfRxDrop[13],
					    prInfo->u4NumOfRxDrop[14], prInfo->u4NumOfRxDrop[15],
					    prInfo->u4NumOfRxDrop[16], prInfo->u4NumOfRxDrop[17],
					    prInfo->u4NumOfRxDrop[18], prInfo->u4NumOfRxDrop[19]);
		}

		/* delay from HIF RX to HIF RX Done */
		if (((prInfo->u4StayIntMinHR2HRD[1] + prInfo->u4StayIntAvgHR2HRD[1] +
		      prInfo->u4StayIntMaxHR2HRD[1]) > 0) ||
		    ((prInfo->u4StayIntMinHR2HRD[2] + prInfo->u4StayIntAvgHR2HRD[2] +
		      prInfo->u4StayIntMaxHR2HRD[2]) > 0)) {
			DBGLOG(RX, INFO, "<stats> StayIntR_HR2HRD us (%u %u %u) (%u %u %u) (%u %u %u)\n",
					    prInfo->u4StayIntMinHR2HRD[0], prInfo->u4StayIntAvgHR2HRD[0],
					    prInfo->u4StayIntMaxHR2HRD[0],
					    prInfo->u4StayIntMinHR2HRD[1], prInfo->u4StayIntAvgHR2HRD[1],
					    prInfo->u4StayIntMaxHR2HRD[1],
					    prInfo->u4StayIntMinHR2HRD[2], prInfo->u4StayIntAvgHR2HRD[2],
					    prInfo->u4StayIntMaxHR2HRD[2]);
		} else {
			DBGLOG(RX, INFO, "<stats> StayIntR_HR2HRD us (%u %u %u)\n",
					    prInfo->u4StayIntMinHR2HRD[0], prInfo->u4StayIntAvgHR2HRD[0],
					    prInfo->u4StayIntMaxHR2HRD[0]);
		}

		/* others */
		DBGLOG(RX, INFO, "<stats> OTHER (%u) (%u) (%u) (%x)\n",
				    prInfo->u4RxFifoFullCnt, prAdapter->ucScanTime,
				    prInfo->u4NumOfChanChange, prInfo->u4CurrChnlInfo);
#if CFG_SUPPORT_THERMO_THROTTLING
		prAdapter->u4AirDelayTotal = (prInfo->u4AirDelayTotal << 5) / 400000;
#endif
		/* reset */
		kalMemZero(prStaRec->u4StayIntMinRx, sizeof(prStaRec->u4StayIntMinRx));
		kalMemZero(prStaRec->u4StayIntAvgRx, sizeof(prStaRec->u4StayIntAvgRx));
		kalMemZero(prStaRec->u4StayIntMaxRx, sizeof(prStaRec->u4StayIntMaxRx));
		prStaRec->u4StatsRxPassToOsCnt = 0;
		prStaRec->u4RxReorderFallAheadCnt = 0;
		prStaRec->u4RxReorderFallBehindCnt = 0;
		prStaRec->u4RxReorderHoleCnt = 0;
	}

	STATS_DRIVER_OWN_RESET();
	kalMemFree(prInfo, VIR_MEM_TYPE, sizeof(STATS_INFO_ENV_T));
}

#if 0
/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to display all environment log.
*
* \param[in] prGlueInfo		Pointer to the Adapter structure
* \param[in] prInBuf		A pointer to the command string buffer, from u4EventSubId
* \param[in] u4InBufLen	The length of the buffer
* \param[out] None
*
* \retval None
*
*/
/*----------------------------------------------------------------------------*/
static void statsInfoEnvDisplay(GLUE_INFO_T *prGlueInfo, UINT8 *prInBuf, UINT32 u4InBufLen)
{
	P_ADAPTER_T prAdapter;
	STA_RECORD_T *prStaRec;
	UINT32 u4NumOfInfo, u4InfoId;
	UINT32 u4RxErrBitmap;
	STATS_INFO_ENV_T rStatsInfoEnv, *prInfo;

/*
[wlan] statsInfoEnvRequest: (INIT INFO) statsInfoEnvRequest cmd ok.
[wlan] statsEventHandle: (INIT INFO) <stats> statsEventHandle: Rcv a event
[wlan] statsEventHandle: (INIT INFO) <stats> statsEventHandle: Rcv a event: 0
[wlan] statsInfoEnvDisplay: (INIT INFO) <stats> Display stats for [00:0c:43:31:35:97]:

[wlan] statsInfoEnvDisplay: (INIT INFO) <stats> TPAM(0x0) RTS(0 0) BA(0x1 0) OK(9 9 xxx) ERR(0 0 0 0 0 0 0)
	TPAM (bit0: enable 40M, bit1: enable 20 short GI, bit2: enable 40 short GI,
		bit3: use 40M TX, bit4: use short GI TX, bit5: use no ack)
	RTS (1st: current use RTS/CTS, 2nd: ever use RTS/CTS)
	BA (1st: TX session BA bitmap for TID0 ~ TID7, 2nd: peer receive maximum agg number)
	OK (1st: total number of tx packet from host, 2nd: total number of tx ok, system time last TX OK)
	ERR (1st: total number of tx err, 2nd ~ 7st: total number of
		WLAN_STATUS_BUFFER_RETAINED, WLAN_STATUS_PACKET_FLUSHED, WLAN_STATUS_PACKET_AGING_TIMEOUT,
		WLAN_STATUS_PACKET_MPDU_ERROR, WLAN_STATUS_PACKET_RTS_ERROR, WLAN_STATUS_PACKET_LIFETIME_ERROR)

[wlan] statsInfoEnvDisplay: (INIT INFO) <stats> TRATE (6 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0) (0 0 0 0 0 0 0 3)
	TX rate count (1M 2 5.5 11 NA NA NA NA 48 24 12 6 54 36 18 9) (MCS0 ~ MCS7)

[wlan] statsInfoEnvDisplay: (INIT INFO) <stats> RX(148 1 0) BA(0x1 64) OK(2 2) ERR(0)
	RX (1st: latest RCPI, 2nd: chan num)
	BA (1st: RX session BA bitmap for TID0 ~ TID7, 2nd: our receive maximum agg number)
	OK (number of rx packets without error, number of rx packets to OS)
	ERR (number of rx packets with error)

[wlan] statsInfoEnvDisplay: (INIT INFO) <stats> RCCK (0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0)
	CCK MODE (1 2 5.5 11M)
[wlan] statsInfoEnvDisplay: (INIT INFO) <stats> ROFDM (0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0)
	OFDM MODE (NA NA NA NA 6 9 12 18 24 36 48 54M)
[wlan] statsInfoEnvDisplay: (INIT INFO) <stats> RHT (0 0 0 0 0 0 0 2 0 0 0 0 0 0 0 0)
	MIXED MODE (number of rx packets with MCS0 ~ MCS15)

[wlan] statsInfoEnvDisplay: (INIT INFO) <stats> StayIntH2M us (29 29 32) (0 0 0) (0 0 0)
	delay from HIF to MAC own bit=1 (min, avg, max for 500B) (min, avg, max for 1000B) (min, avg, max for others)

[wlan] statsInfoEnvDisplay: (INIT INFO) <stats> AirTime us (608 864 4480) (0 0 0) (0 0 0)
	delay from MAC start TX to MAC TX done

[wlan] statsInfoEnvDisplay: (INIT INFO) <stats> StayInt us (795 1052 4644_4504) (0 0 0_0) (0 0 0_0)
	delay from HIF to MAC TX done (min, avg, max_system time for 500B)

[wlan] statsInfoEnvDisplay: (INIT INFO) <stats> StayIntD2T us (795 1052 4644) (0 0 0) (0 0 0)
	delay from driver to MAC TX done (min, avg, max for 500B)

[wlan] statsInfoEnvDisplay: (INIT INFO) <stats> StayIntR_M2H us (37 40 58) (0 0 0) (0 0 0)
	delay from MAC to HIF (min, avg, max for 500B)

[wlan] statsInfoEnvDisplay: (INIT INFO) <stats> StayIntR_H2D us (0 0 0) (0 0 0) (0 0 0)
	delay from HIF to Driver OS (min, avg, max for 500B)

[wlan] statsInfoEnvDisplay: (INIT INFO) <stats> StayCntD2H unit:10ms (10 0 0 0)
	delay count from Driver to HIF (count in 0~10ms, 10~20ms, 20~30ms, others)

[wlan] statsInfoEnvDisplay: (INIT INFO) <stats> StayCnt unit:1ms (6 3 0 1)
	delay count from HIF to TX DONE (count in 0~1ms, 1~5ms, 5~10ms, others)

[wlan] statsInfoEnvDisplay: (INIT INFO) <stats> StayCnt (0~1161:7) (1161~2322:2) (2322~3483:0) (3483~4644:0) (4644~:1)
	delay count from HIF to TX DONE (count in 0~1161 ticks, 1161~2322, 2322~3483, 3483~4644, others)

[wlan] statsInfoEnvDisplay: (INIT INFO) <stats> OTHER (61877) (0) (38) (0) (0) (0ms)
	Channel idle time, scan count, channel change count, empty tx quota count,
	power save change count from active to PS, maximum delay from PS to active
*/

	/* init */
	prAdapter = prGlueInfo->prAdapter;
	prInfo = &rStatsInfoEnv;
	kalMemZero(&rStatsInfoEnv, sizeof(rStatsInfoEnv));

	if (u4InBufLen > sizeof(rStatsInfoEnv))
		u4InBufLen = sizeof(rStatsInfoEnv);

	/* parse */
	u4NumOfInfo = *(UINT32 *) prInBuf;
	u4RxErrBitmap = *(UINT32 *) (prInBuf + 4);

	/* print */
	for (u4InfoId = 0; u4InfoId < u4NumOfInfo; u4InfoId++) {
		/*
		   use u4InBufLen, not sizeof(rStatsInfoEnv)
		   because the firmware version maybe not equal to driver version
		 */
		kalMemCopy(&rStatsInfoEnv, prInBuf + 8, u4InBufLen);

		prStaRec = cnmGetStaRecByIndex(prAdapter, rStatsInfoEnv.ucStaRecIdx);
		if (prStaRec == NULL)
			continue;

		DBGLOG(RX, INFO, "<stats> Display stats V%d.%d for [%pM]: %uB %ums\n",
				    prInfo->ucFwVer[0], prInfo->ucFwVer[1],
				    (prStaRec->aucMacAddr), (UINT32) sizeof(STATS_INFO_ENV_T),
				    prInfo->u4ReportSysTime);
		DBGLOG(RX, INFO, "<stats>TPAM(0x%x)RTS(%u %u)BA(0x%x %u)OS(%u)OK(%u %u)ERR(%u %u %u %u %u %u %u)\n",
				    prInfo->ucTxParam,
				    prInfo->fgTxIsRtsUsed, prInfo->fgTxIsRtsEverUsed,
				    prInfo->ucTxAggBitmap, prInfo->ucTxPeerAggMaxSize,
				    (UINT32) prGlueInfo->rNetDevStats.tx_packets,
				    prInfo->u4TxDataCntAll, prInfo->u4TxDataCntOK,
				    prInfo->u4TxDataCntErr, prInfo->u4TxDataCntErrType[0],
				    prInfo->u4TxDataCntErrType[1], prInfo->u4TxDataCntErrType[2],
				    prInfo->u4TxDataCntErrType[3], prInfo->u4TxDataCntErrType[4],
				    prInfo->u4TxDataCntErrType[5]));

		DBGLOG(RX, INFO, "TRATE(%u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u)\n",
				    prInfo->u4TxRateCntNonHT[0], prInfo->u4TxRateCntNonHT[1],
				    prInfo->u4TxRateCntNonHT[2], prInfo->u4TxRateCntNonHT[3],
				    prInfo->u4TxRateCntNonHT[4], prInfo->u4TxRateCntNonHT[5],
				    prInfo->u4TxRateCntNonHT[6], prInfo->u4TxRateCntNonHT[7],
				    prInfo->u4TxRateCntNonHT[8], prInfo->u4TxRateCntNonHT[9],
				    prInfo->u4TxRateCntNonHT[10], prInfo->u4TxRateCntNonHT[11],
				    prInfo->u4TxRateCntNonHT[12], prInfo->u4TxRateCntNonHT[13],
				    prInfo->u4TxRateCntNonHT[14], prInfo->u4TxRateCntNonHT[15],
				    prInfo->u4TxRateCntHT[0], prInfo->u4TxRateCntHT[1],
				    prInfo->u4TxRateCntHT[2], prInfo->u4TxRateCntHT[3],
				    prInfo->u4TxRateCntHT[4], prInfo->u4TxRateCntHT[5],
				    prInfo->u4TxRateCntHT[6], prInfo->u4TxRateCntHT[7]));

		DBGLOG(RX, INFO, "<stats> TREORDER (%u %u %u)\n",
				    prStaRec->u4RxReorderFallAheadCnt,
				    prStaRec->u4RxReorderFallBehindCnt, prStaRec->u4RxReorderHoleCnt);

		DBGLOG(RX, INFO, "<stats> RX(%u %u %u) BA(0x%x %u) OK(%u %u) ERR(%u)\n",
				    prInfo->ucRcvRcpi, prInfo->ucHwChanNum, prInfo->fgRxIsShortGI,
				    prInfo->ucRxAggBitmap, prInfo->ucRxAggMaxSize,
				    prInfo->u4RxDataCntAll, prStaRec->u4StatsRxPassToOsCnt, prInfo->u4RxDataCntErr);

		DBGLOG(RX, INFO, "<stats> RX Free MAC DESC(%u %u %u %u %u %u) Free HIF DESC(%u %u %u %u %u %u)\n",
				    prInfo->u4RxMacFreeDescCnt[0], prInfo->u4RxMacFreeDescCnt[1],
				    prInfo->u4RxMacFreeDescCnt[2], prInfo->u4RxMacFreeDescCnt[3],
				    prInfo->u4RxMacFreeDescCnt[4], prInfo->u4RxMacFreeDescCnt[5],
				    prInfo->u4RxHifFreeDescCnt[0], prInfo->u4RxHifFreeDescCnt[1],
				    prInfo->u4RxHifFreeDescCnt[2], prInfo->u4RxHifFreeDescCnt[3],
				    prInfo->u4RxHifFreeDescCnt[4], prInfo->u4RxHifFreeDescCnt[5]));

		DBGLOG(RX, INFO, "<stats> RCCK (%u %u) (%u %u) (%u %u) (%u %u) (%u %u) (%u %u)\n"
				    "(%u %u) (%u %u) (%u %u) (%u %u) (%u %u) (%u %u) (%u %u) (%u %u) (%u %u) (%u %u)\n",
				    prInfo->u4RxRateCnt[0][0], prInfo->u4RxRateRetryCnt[0][0],
				    prInfo->u4RxRateCnt[0][1], prInfo->u4RxRateRetryCnt[0][1],
				    prInfo->u4RxRateCnt[0][2], prInfo->u4RxRateRetryCnt[0][2],
				    prInfo->u4RxRateCnt[0][3], prInfo->u4RxRateRetryCnt[0][3],
				    prInfo->u4RxRateCnt[0][4], prInfo->u4RxRateRetryCnt[0][4],
				    prInfo->u4RxRateCnt[0][5], prInfo->u4RxRateRetryCnt[0][5],
				    prInfo->u4RxRateCnt[0][6], prInfo->u4RxRateRetryCnt[0][6],
				    prInfo->u4RxRateCnt[0][7], prInfo->u4RxRateRetryCnt[0][7],
				    prInfo->u4RxRateCnt[0][8], prInfo->u4RxRateRetryCnt[0][8],
				    prInfo->u4RxRateCnt[0][9], prInfo->u4RxRateRetryCnt[0][9],
				    prInfo->u4RxRateCnt[0][10], prInfo->u4RxRateRetryCnt[0][10],
				    prInfo->u4RxRateCnt[0][11], prInfo->u4RxRateRetryCnt[0][11],
				    prInfo->u4RxRateCnt[0][12], prInfo->u4RxRateRetryCnt[0][12],
				    prInfo->u4RxRateCnt[0][13], prInfo->u4RxRateRetryCnt[0][13],
				    prInfo->u4RxRateCnt[0][14], prInfo->u4RxRateRetryCnt[0][14],
				    prInfo->u4RxRateCnt[0][15], prInfo->u4RxRateRetryCnt[0][15]));
		DBGLOG(RX, INFO, "<stats> ROFDM (%u %u) (%u %u) (%u %u) (%u %u) (%u %u) (%u %u)\n"
				    "(%u %u) (%u %u) (%u %u) (%u %u) (%u %u) (%u %u) (%u %u) (%u %u) (%u %u) (%u %u)\n",
				    prInfo->u4RxRateCnt[1][0], prInfo->u4RxRateRetryCnt[1][0],
				    prInfo->u4RxRateCnt[1][1], prInfo->u4RxRateRetryCnt[1][1],
				    prInfo->u4RxRateCnt[1][2], prInfo->u4RxRateRetryCnt[1][2],
				    prInfo->u4RxRateCnt[1][3], prInfo->u4RxRateRetryCnt[1][3],
				    prInfo->u4RxRateCnt[1][4], prInfo->u4RxRateRetryCnt[1][4],
				    prInfo->u4RxRateCnt[1][5], prInfo->u4RxRateRetryCnt[1][5],
				    prInfo->u4RxRateCnt[1][6], prInfo->u4RxRateRetryCnt[1][6],
				    prInfo->u4RxRateCnt[1][7], prInfo->u4RxRateRetryCnt[1][7],
				    prInfo->u4RxRateCnt[1][8], prInfo->u4RxRateRetryCnt[1][8],
				    prInfo->u4RxRateCnt[1][9], prInfo->u4RxRateRetryCnt[1][9],
				    prInfo->u4RxRateCnt[1][10], prInfo->u4RxRateRetryCnt[1][10],
				    prInfo->u4RxRateCnt[1][11], prInfo->u4RxRateRetryCnt[1][11],
				    prInfo->u4RxRateCnt[1][12], prInfo->u4RxRateRetryCnt[1][12],
				    prInfo->u4RxRateCnt[1][13], prInfo->u4RxRateRetryCnt[1][13],
				    prInfo->u4RxRateCnt[1][14], prInfo->u4RxRateRetryCnt[1][14],
				    prInfo->u4RxRateCnt[1][15], prInfo->u4RxRateRetryCnt[1][15]));
		DBGLOG(RX, INFO, "<stats> RHT (%u %u) (%u %u) (%u %u) (%u %u) (%u %u) (%u %u) (%u %u)\n"
				    "(%u %u) (%u %u) (%u %u) (%u %u) (%u %u) (%u %u) (%u %u) (%u %u) (%u %u)\n",
				    prInfo->u4RxRateCnt[2][0], prInfo->u4RxRateRetryCnt[2][0],
				    prInfo->u4RxRateCnt[2][1], prInfo->u4RxRateRetryCnt[2][1],
				    prInfo->u4RxRateCnt[2][2], prInfo->u4RxRateRetryCnt[2][2],
				    prInfo->u4RxRateCnt[2][3], prInfo->u4RxRateRetryCnt[2][3],
				    prInfo->u4RxRateCnt[2][4], prInfo->u4RxRateRetryCnt[2][4],
				    prInfo->u4RxRateCnt[2][5], prInfo->u4RxRateRetryCnt[2][5],
				    prInfo->u4RxRateCnt[2][6], prInfo->u4RxRateRetryCnt[2][6],
				    prInfo->u4RxRateCnt[2][7], prInfo->u4RxRateRetryCnt[2][7],
				    prInfo->u4RxRateCnt[2][8], prInfo->u4RxRateRetryCnt[2][8],
				    prInfo->u4RxRateCnt[2][9], prInfo->u4RxRateRetryCnt[2][9],
				    prInfo->u4RxRateCnt[2][10], prInfo->u4RxRateRetryCnt[2][10],
				    prInfo->u4RxRateCnt[2][11], prInfo->u4RxRateRetryCnt[2][11],
				    prInfo->u4RxRateCnt[2][12], prInfo->u4RxRateRetryCnt[2][12],
				    prInfo->u4RxRateCnt[2][13], prInfo->u4RxRateRetryCnt[2][13],
				    prInfo->u4RxRateCnt[2][14], prInfo->u4RxRateRetryCnt[2][14],
				    prInfo->u4RxRateCnt[2][15], prInfo->u4RxRateRetryCnt[2][15]));

		/* delay from HIF to MAC */
		DBGLOG(RX, INFO, "<stats> StayIntH2M us (%u %u %u) (%u %u %u) (%u %u %u)\n",
				    prInfo->u4StayIntMinH2M[0], prInfo->u4StayIntAvgH2M[0],
				    prInfo->u4StayIntMaxH2M[0],
				    prInfo->u4StayIntMinH2M[1], prInfo->u4StayIntAvgH2M[1],
				    prInfo->u4StayIntMaxH2M[1],
				    prInfo->u4StayIntMinH2M[2], prInfo->u4StayIntAvgH2M[2],
				    prInfo->u4StayIntMaxH2M[2]));
		/* delay from MAC to TXDONE */
		DBGLOG(RX, INFO, "<stats> AirTime us (%u %u %u) (%u %u %u) (%u %u %u)\n",
				    prInfo->u4AirDelayMin[0] << 5, prInfo->u4AirDelayAvg[0] << 5,
				    prInfo->u4AirDelayMax[0] << 5,
				    prInfo->u4AirDelayMin[1] << 5, prInfo->u4AirDelayAvg[1] << 5,
				    prInfo->u4AirDelayMax[1] << 5,
				    prInfo->u4TxDataCntAll, (prInfo->u4AirDelayAvg[2] << 5) / (prInfo->u4TxDataCntAll),
				    (prInfo->u4AirDelayAvg[2] << 5) / 400000));
		prAdapter->u4AirDelayTotal = (prInfo->u4AirDelayTotal << 5) / 400000;
		/* delay from HIF to TXDONE */
		DBGLOG(RX, INFO, "<stats> StayInt us (%u %u %u_%u) (%u %u %u_%u) (%u %u %u_%u)\n",
				    prInfo->u4StayIntMin[0], prInfo->u4StayIntAvg[0],
				    prInfo->u4StayIntMax[0], prInfo->u4StayIntMaxSysTime[0],
				    prInfo->u4StayIntMin[1], prInfo->u4StayIntAvg[1],
				    prInfo->u4StayIntMax[1], prInfo->u4StayIntMaxSysTime[1],
				    prInfo->u4StayIntMin[2], prInfo->u4StayIntAvg[2],
				    prInfo->u4StayIntMax[2], prInfo->u4StayIntMaxSysTime[2]));
		/* delay from Driver to TXDONE */
		DBGLOG(RX, INFO, "<stats> StayIntD2T us (%u %u %u) (%u %u %u) (%u %u %u)\n",
				    prInfo->u4StayIntMinD2T[0], prInfo->u4StayIntAvgD2T[0],
				    prInfo->u4StayIntMaxD2T[0],
				    prInfo->u4StayIntMinD2T[1], prInfo->u4StayIntAvgD2T[1],
				    prInfo->u4StayIntMaxD2T[1],
				    prInfo->u4StayIntMinD2T[2], prInfo->u4StayIntAvgD2T[2],
				    prInfo->u4StayIntMaxD2T[2]));

		/* delay from RXDONE to HIF */
		DBGLOG(RX, INFO, "<stats> StayIntR_M2H us (%u %u %u) (%u %u %u) (%u %u %u)\n",
				    prInfo->u4StayIntMinRx[0], prInfo->u4StayIntAvgRx[0],
				    prInfo->u4StayIntMaxRx[0],
				    prInfo->u4StayIntMinRx[1], prInfo->u4StayIntAvgRx[1],
				    prInfo->u4StayIntMaxRx[1],
				    prInfo->u4StayIntMinRx[2], prInfo->u4StayIntAvgRx[2], prInfo->u4StayIntMaxRx[2]));
		/* delay from HIF to OS */
		DBGLOG(RX, INFO, "<stats> StayIntR_H2D us (%u %u %u) (%u %u %u) (%u %u %u)\n",
				    prStaRec->u4StayIntMinRx[0], prStaRec->u4StayIntAvgRx[0],
				    prStaRec->u4StayIntMaxRx[0],
				    prStaRec->u4StayIntMinRx[1], prStaRec->u4StayIntAvgRx[1],
				    prStaRec->u4StayIntMaxRx[1],
				    prStaRec->u4StayIntMinRx[2], prStaRec->u4StayIntAvgRx[2],
				    prStaRec->u4StayIntMaxRx[2]));

		/* count based on delay from OS to HIF */
		DBGLOG(RX, INFO, "<stats> StayCntD2H unit:%dms (%d %d %d %d)\n",
				    STATS_STAY_INT_D2H_CONST,
				    prInfo->u4StayIntD2HByConst[0], prInfo->u4StayIntD2HByConst[1],
				    prInfo->u4StayIntD2HByConst[2], prInfo->u4StayIntD2HByConst[3]);

		/* count based on different delay from HIF to TX DONE */
		DBGLOG(RX, INFO, "<stats> StayCnt unit:%dms (%d %d %d %d)\n",
				    STATS_STAY_INT_CONST,
				    prInfo->u4StayIntByConst[0], prInfo->u4StayIntByConst[1],
				    prInfo->u4StayIntByConst[2], prInfo->u4StayIntByConst[3]);
		DBGLOG(RX, INFO, "<stats> StayCnt (%d~%d:%d) (%d~%d:%d) (%d~%d:%d) (%d~%d:%d) (%d~:%d)\n",
				    0, prInfo->u4StayIntMaxPast / 4, prInfo->u4StayIntCnt[0],
				    prInfo->u4StayIntMaxPast / 4, prInfo->u4StayIntMaxPast / 2, prInfo->u4StayIntCnt[1],
				    prInfo->u4StayIntMaxPast / 2, prInfo->u4StayIntMaxPast * 3 / 4,
				    prInfo->u4StayIntCnt[2], prInfo->u4StayIntMaxPast * 3 / 4, prInfo->u4StayIntMaxPast,
				    prInfo->u4StayIntCnt[3], prInfo->u4StayIntMaxPast, prInfo->u4StayIntCnt[4]));

		/* channel idle time */
		DBGLOG(RX, INFO, "<stats> Idle Time (slot): (%u) (%u) (%u) (%u) (%u) (%u) (%u) (%u) (%u) (%u)\n",
				    prInfo->au4ChanIdleCnt[0], prInfo->au4ChanIdleCnt[1],
				    prInfo->au4ChanIdleCnt[2], prInfo->au4ChanIdleCnt[3],
				    prInfo->au4ChanIdleCnt[4], prInfo->au4ChanIdleCnt[5],
				    prInfo->au4ChanIdleCnt[6], prInfo->au4ChanIdleCnt[7],
				    prInfo->au4ChanIdleCnt[8], prInfo->au4ChanIdleCnt[9]));

		/* BT coex */
		DBGLOG(RX, INFO, "<stats> BT coex (0x%x)\n", prInfo->u4BtContUseTime);

		/* others */
		DBGLOG(RX, INFO, "<stats> OTHER (%u) (%u) (%u) (%u) (%u) (%ums) (%uus)\n",
				    prInfo->u4RxFifoFullCnt, prAdapter->ucScanTime,
				    prInfo->u4NumOfChanChange, prStaRec->u4NumOfNoTxQuota,
				    prInfo->ucNumOfPsChange, prInfo->u4PsIntMax, u4DrvOwnMax / 1000);

		/* reset */
		kalMemZero(prStaRec->u4StayIntMinRx, sizeof(prStaRec->u4StayIntMinRx));
		kalMemZero(prStaRec->u4StayIntAvgRx, sizeof(prStaRec->u4StayIntAvgRx));
		kalMemZero(prStaRec->u4StayIntMaxRx, sizeof(prStaRec->u4StayIntMaxRx));
		prStaRec->u4StatsRxPassToOsCnt = 0;
		prStaRec->u4RxReorderFallAheadCnt = 0;
		prStaRec->u4RxReorderFallBehindCnt = 0;
		prStaRec->u4RxReorderHoleCnt = 0;
	}

	STATS_DRIVER_OWN_RESET();
}
#endif

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to request firmware to feedback statistics.
*
* \param[in] prAdapter			Pointer to the Adapter structure
* \param[in] pvSetBuffer		A pointer to the buffer that holds the data to be set
* \param[in] u4SetBufferLen		The length of the set buffer
* \param[out] pu4SetInfoLen	If the call is successful, returns the number of
*	bytes read from the set buffer. If the call failed due to invalid length of
*	the set buffer, returns the amount of storage needed.
*
* \retval TDLS_STATUS_xx
*
*/
/*----------------------------------------------------------------------------*/
static WLAN_STATUS
statsInfoEnvRequest(ADAPTER_T *prAdapter, VOID *pvSetBuffer, UINT_32 u4SetBufferLen, UINT_32 *pu4SetInfoLen)
{
	STATS_CMD_CORE_T *prCmdContent;
	WLAN_STATUS rStatus;

	/* sanity check */
	if (fgIsUnderSuspend == true)
		return WLAN_STATUS_SUCCESS;	/* do not request stats after early suspend */

	/* init command buffer */
	prCmdContent = (STATS_CMD_CORE_T *) pvSetBuffer;
	prCmdContent->u4Command = STATS_CORE_CMD_ENV_REQUEST;

	/* send the command */
	rStatus = wlanSendSetQueryCmd(prAdapter,	/* prAdapter */
				      CMD_ID_STATS,	/* ucCID */
				      TRUE,	/* fgSetQuery */
				      FALSE,	/* fgNeedResp */
				      FALSE,	/* fgIsOid */
				      NULL, NULL,	/* pfCmdTimeoutHandler */
				      sizeof(STATS_CMD_CORE_T),	/* u4SetQueryInfoLen */
				      (PUINT_8) prCmdContent,	/* pucInfoBuffer */
				      NULL,	/* pvSetQueryBuffer */
				      0	/* u4SetQueryBufferLen */
	    );

	if (rStatus != WLAN_STATUS_PENDING) {
		DBGLOG(RX, ERROR, "%s wlanSendSetQueryCmd allocation fail!\n", __func__);
		return WLAN_STATUS_RESOURCES;
	}

	DBGLOG(RX, INFO, "%s cmd ok.\n", __func__);
	return WLAN_STATUS_SUCCESS;
}

/*******************************************************************************
*						P U B L I C  F U N C T I O N S
********************************************************************************
*/

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to handle any statistics event.
*
* \param[in] prGlueInfo		Pointer to the Adapter structure
* \param[in] prInBuf		A pointer to the command string buffer
* \param[in] u4InBufLen	The length of the buffer
* \param[out] None
*
* \retval None
*/
/*----------------------------------------------------------------------------*/
VOID statsEventHandle(GLUE_INFO_T *prGlueInfo, UINT8 *prInBuf, UINT32 u4InBufLen)
{
	UINT32 u4EventId;

	/* sanity check */
/* DBGLOG(RX, INFO, */
/* ("<stats> %s: Rcv a event\n", __FUNCTION__)); */

	if ((prGlueInfo == NULL) || (prInBuf == NULL))
		return;		/* shall not be here */

	/* handle */
	u4EventId = *(UINT32 *) prInBuf;
	u4InBufLen -= 4;

/* DBGLOG(RX, INFO, */
/* ("<stats> %s: Rcv a event: %d\n", __FUNCTION__, u4EventId)); */

	switch (u4EventId) {
	case STATS_HOST_EVENT_ENV_REPORT:
		statsInfoEnvDisplay(prGlueInfo, prInBuf + 4, u4InBufLen);
		break;

	default:
		break;
	}
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to detect if we can request firmware to feedback statistics.
*
* \param[in] prGlueInfo		Pointer to the Adapter structure
* \param[in] ucStaRecIndex	The station index
* \param[out] None
*
* \retval None
*/
/*----------------------------------------------------------------------------*/
VOID statsEnvReportDetect(ADAPTER_T *prAdapter, UINT8 ucStaRecIndex)
{
	STA_RECORD_T *prStaRec;
	OS_SYSTIME rCurTime;
	STATS_CMD_CORE_T rCmd;

	prStaRec = cnmGetStaRecByIndex(prAdapter, ucStaRecIndex);
	if (prStaRec == NULL)
		return;

	prStaRec->u4StatsEnvTxCnt++;
	GET_CURRENT_SYSTIME(&rCurTime);

	if (prStaRec->rStatsEnvTxPeriodLastTime == 0) {
		prStaRec->rStatsEnvTxLastTime = rCurTime;
		prStaRec->rStatsEnvTxPeriodLastTime = rCurTime;
		return;
	}

	if (prStaRec->u4StatsEnvTxCnt > STATS_ENV_TX_CNT_REPORT_TRIGGER) {
		if (CHECK_FOR_TIMEOUT(rCurTime, prStaRec->rStatsEnvTxLastTime,
				      SEC_TO_SYSTIME(STATS_ENV_TX_CNT_REPORT_TRIGGER_SEC))) {
			rCmd.ucStaRecIdx = ucStaRecIndex;
			statsInfoEnvRequest(prAdapter, &rCmd, 0, NULL);

			prStaRec->rStatsEnvTxLastTime = rCurTime;
			prStaRec->rStatsEnvTxPeriodLastTime = rCurTime;
			prStaRec->u4StatsEnvTxCnt = 0;
			return;
		}
	}

	if (CHECK_FOR_TIMEOUT(rCurTime, prStaRec->rStatsEnvTxPeriodLastTime, SEC_TO_SYSTIME(STATS_ENV_TIMEOUT_SEC))) {
		rCmd.ucStaRecIdx = ucStaRecIndex;
		statsInfoEnvRequest(prAdapter, &rCmd, 0, NULL);

		prStaRec->rStatsEnvTxPeriodLastTime = rCurTime;
		return;
	}
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to handle rx done.
*
* \param[in] prStaRec		Pointer to the STA_RECORD_T structure
* \param[in] prSwRfb		Pointer to the received packet
* \param[out] None
*
* \retval None
*/
/*----------------------------------------------------------------------------*/
VOID StatsEnvRxDone(STA_RECORD_T *prStaRec, SW_RFB_T *prSwRfb)
{
	UINT32 u4LenId;
	UINT32 u4CurTime, u4DifTime;

	/* sanity check */
	if (prStaRec == NULL)
		return;

	/* stats: rx done count */
	prStaRec->u4StatsRxPassToOsCnt++;

	/* get length partition ID */
	u4LenId = 0;
	if (prSwRfb->u2PacketLen < STATS_STAY_INT_BYTE_THRESHOLD) {
		u4LenId = 0;
	} else {
		if ((STATS_STAY_INT_BYTE_THRESHOLD <= prSwRfb->u2PacketLen) &&
		    (prSwRfb->u2PacketLen < (STATS_STAY_INT_BYTE_THRESHOLD << 1))) {
			u4LenId = 1;
		} else
			u4LenId = 2;
	}

	/* stats: rx delay */
	u4CurTime = kalGetTimeTick();

	if ((u4CurTime > prSwRfb->rRxTime) && (prSwRfb->rRxTime != 0)) {
		u4DifTime = u4CurTime - prSwRfb->rRxTime;

		if (prStaRec->u4StayIntMinRx[u4LenId] == 0)	/* impossible */
			prStaRec->u4StayIntMinRx[u4LenId] = 0xffffffff;

		if (u4DifTime > prStaRec->u4StayIntMaxRx[u4LenId])
			prStaRec->u4StayIntMaxRx[u4LenId] = u4DifTime;
		else if (u4DifTime < prStaRec->u4StayIntMinRx[u4LenId])
			prStaRec->u4StayIntMinRx[u4LenId] = u4DifTime;

		prStaRec->u4StayIntAvgRx[u4LenId] += u4DifTime;
		if (prStaRec->u4StayIntAvgRx[u4LenId] != u4DifTime)
			prStaRec->u4StayIntAvgRx[u4LenId] >>= 1;
	}
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to handle rx done.
*
* \param[in] prGlueInfo		Pointer to the Adapter structure
* \param[in] prInBuf		A pointer to the command string buffer
* \param[in] u4InBufLen	The length of the buffer
* \param[out] None
*
* \retval None
*/
/*----------------------------------------------------------------------------*/
UINT_64 StatsEnvTimeGet(VOID)
{
	/* TODO: use better API to get time to save time, jiffies unit is 10ms, too large */

/* struct timeval tv; */

/* do_gettimeofday(&tv); */
/* return tv.tv_usec + tv.tv_sec * (UINT_64)1000000; */

	UINT_64 u8Clk;
/* UINT32 *pClk = &u8Clk; */

	u8Clk = sched_clock();	/* unit: naro seconds */
/* printk("<stats> sched_clock() = %x %x %u\n", pClk[0], pClk[1], sizeof(jiffies)); */

	return (UINT_64) u8Clk;	/* sched_clock *//* jiffies size = 4B */
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to handle rx done.
*
* \param[in] prGlueInfo		Pointer to the Adapter structure
* \param[in] prInBuf		A pointer to the command string buffer
* \param[in] u4InBufLen	The length of the buffer
* \param[out] None
*
* \retval None
*/
/*----------------------------------------------------------------------------*/
VOID StatsEnvTxTime2Hif(MSDU_INFO_T *prMsduInfo, HIF_TX_HEADER_T *prHwTxHeader)
{
	UINT_64 u8SysTime, u8SysTimeIn;
	UINT32 u4TimeDiff;

	u8SysTime = StatsEnvTimeGet();
	u8SysTimeIn = GLUE_GET_PKT_XTIME(prMsduInfo->prPacket);

/* printk("<stats> hif: 0x%x %u %u %u\n", */
/* prMsduInfo->prPacket, StatsEnvTimeGet(), u8SysTime, GLUE_GET_PKT_XTIME(prMsduInfo->prPacket)); */

	if ((u8SysTimeIn > 0) && (u8SysTime > u8SysTimeIn)) {
		u8SysTime = u8SysTime - u8SysTimeIn;
		u4TimeDiff = (UINT32) u8SysTime;
		u4TimeDiff = u4TimeDiff / 1000;	/* ns to us */

		/* pass the delay between OS to us and we to HIF */
		if (u4TimeDiff > 0xFFFF)
			*(UINT16 *) prHwTxHeader->aucReserved = (UINT16) 0xFFFF;	/* 65535 us */
		else
			*(UINT16 *) prHwTxHeader->aucReserved = (UINT16) u4TimeDiff;

/* printk("<stats> u4TimeDiff: %u\n", u4TimeDiff); */
	} else {
		prHwTxHeader->aucReserved[0] = 0;
		prHwTxHeader->aucReserved[1] = 0;
	}
}

static VOID statsParsePktInfo(PUINT_8 pucPkt, UINT_8 status, UINT_8 eventType, P_MSDU_INFO_T prMsduInfo)
{
	/* get ethernet protocol */
	UINT_16 u2EtherType = (pucPkt[ETH_TYPE_LEN_OFFSET] << 8) | (pucPkt[ETH_TYPE_LEN_OFFSET + 1]);
	PUINT_8 pucEthBody = &pucPkt[ETH_HLEN];

	switch (u2EtherType) {
	case ETH_P_ARP:
	{
		UINT_16 u2OpCode = (pucEthBody[6] << 8) | pucEthBody[7];
		if (eventType == EVENT_TX)
			prMsduInfo->fgIsBasicRate = TRUE;

		if ((su2TxDoneCfg & CFG_ARP) == 0)
			break;

		switch (eventType) {
		case EVENT_RX:
			if (u2OpCode == ARP_PRO_REQ)
				DBGLOG(RX, INFO, "<RX> Arp Req From IP: %d.%d.%d.%d\n",
					pucEthBody[14], pucEthBody[15], pucEthBody[16], pucEthBody[17]);
			else if (u2OpCode == ARP_PRO_RSP)
				DBGLOG(RX, INFO, "<RX> Arp Rsp from IP: %d.%d.%d.%d\n",
					pucEthBody[14], pucEthBody[15], pucEthBody[16], pucEthBody[17]);
			break;
		case EVENT_TX:
			if (u2OpCode == ARP_PRO_REQ)
				DBGLOG(TX, INFO, "<TX> Arp Req to IP: %d.%d.%d.%d\n",
					pucEthBody[24], pucEthBody[25], pucEthBody[26], pucEthBody[27]);
			else if (u2OpCode == ARP_PRO_RSP)
				DBGLOG(TX, INFO, "<TX> Arp Rsp to IP: %d.%d.%d.%d\n",
					pucEthBody[24], pucEthBody[25], pucEthBody[26], pucEthBody[27]);
			prMsduInfo->fgNeedTxDoneStatus = TRUE;
			break;
		case EVENT_TX_DONE:
			if (u2OpCode == ARP_PRO_REQ)
				DBGLOG(TX, INFO, "<TX status:%d> Arp Req to IP: %d.%d.%d.%d\n", status,
					pucEthBody[24], pucEthBody[25], pucEthBody[26], pucEthBody[27]);
			else if (u2OpCode == ARP_PRO_RSP)
				DBGLOG(TX, INFO, "<TX status:%d> Arp Rsp to IP: %d.%d.%d.%d\n", status,
					pucEthBody[24], pucEthBody[25], pucEthBody[26], pucEthBody[27]);
			break;
		}
		break;
	}
	case ETH_P_IP:
	{
		UINT_8 ucIpProto = pucEthBody[9]; /* IP header without options */
		UINT_8 ucIpVersion = (pucEthBody[0] & IPVH_VERSION_MASK) >> IPVH_VERSION_OFFSET;
		UINT_16 u2IpId = pucEthBody[4]<<8 | pucEthBody[5];

		if (ucIpVersion != IPVERSION)
			break;

		switch (ucIpProto) {
		case IP_PRO_ICMP:
		{
			/* the number of ICMP packets is seldom so we print log here */
			UINT_8 ucIcmpType;
			UINT_16 u2IcmpId, u2IcmpSeq;
			PUINT_8 pucIcmp = &pucEthBody[20];

			ucIcmpType = pucIcmp[0];
			/* don't log network unreachable packet */
			if (((su2TxDoneCfg & CFG_ICMP) == 0) || ucIcmpType == 3)
				break;
			u2IcmpId = *(UINT_16 *) &pucIcmp[4];
			u2IcmpSeq = *(UINT_16 *) &pucIcmp[6];
			switch (eventType) {
			case EVENT_RX:
				DBGLOG(RX, INFO, "<RX> ICMP: Type %d, Id BE 0x%04x, Seq BE 0x%04x\n",
							ucIcmpType, u2IcmpId, u2IcmpSeq);
				break;
			case EVENT_TX:
				DBGLOG(TX, INFO, "<TX> ICMP: Type %d, Id 0x04%x, Seq BE 0x%04x\n",
								ucIcmpType, u2IcmpId, u2IcmpSeq);
				prMsduInfo->fgNeedTxDoneStatus = TRUE;
				break;
			case EVENT_TX_DONE:
				DBGLOG(TX, INFO, "<TX status:%d> Type %d, Id 0x%04x, Seq 0x%04x\n",
						status, ucIcmpType, u2IcmpId, u2IcmpSeq);
				break;
			}
			break;
		}
		case IP_PRO_UDP:
		{
			/* the number of DHCP packets is seldom so we print log here */
			PUINT_8 pucUdp = &pucEthBody[20];
			PUINT_8 pucUdpPayload = &pucUdp[8];
			UINT_16 u2UdpDstPort;
			UINT_16 u2UdpSrcPort;

			u2UdpDstPort = (pucUdp[2] << 8) | pucUdp[3];
			u2UdpSrcPort = (pucUdp[0] << 8) | pucUdp[1];
			/* dhcp */
			if ((u2UdpDstPort == UDP_PORT_DHCPS) || (u2UdpDstPort == UDP_PORT_DHCPC)) {
				UINT_32 u4TransID = pucUdpPayload[4]<<24 | pucUdpPayload[5]<<16 |
						pucUdpPayload[6]<<8  | pucUdpPayload[7];

				switch (eventType) {
				case EVENT_RX:
					DBGLOG(RX, INFO, "<RX> DHCP: IPID 0x%02x, MsgType 0x%x, TransID 0x%08x\n",
									u2IpId, pucUdpPayload[0], u4TransID);
					break;
				case EVENT_TX:
					DBGLOG(TX, INFO, "<TX> DHCP: IPID 0x%02x, MsgType 0x%x, TransID 0x%08x\n",
									u2IpId, pucUdpPayload[0], u4TransID);
					prMsduInfo->fgNeedTxDoneStatus = TRUE;
					prMsduInfo->fgIsBasicRate = TRUE;
					break;
				case EVENT_TX_DONE:
					DBGLOG(TX, INFO,
						"<TX status:%d> DHCP: IPID 0x%02x, MsgType 0x%x, TransID 0x%08x\n",
							status, u2IpId, pucUdpPayload[0], u4TransID);
					break;
				}
			} else if (u2UdpDstPort == UDP_PORT_DNS) { /* tx dns */
				UINT_16 u2TransId = (pucUdpPayload[0] << 8) | pucUdpPayload[1];
				if (eventType == EVENT_TX)
					prMsduInfo->fgIsBasicRate = TRUE;

				if ((su2TxDoneCfg & CFG_DNS) == 0)
					break;
				if (eventType == EVENT_TX) {
					DBGLOG(TX, INFO, "<TX> DNS: IPID 0x%02x, TransID 0x%04x\n", u2IpId, u2TransId);
					prMsduInfo->fgNeedTxDoneStatus = TRUE;
				} else if (eventType == EVENT_TX_DONE)
					DBGLOG(TX, INFO, "<TX status:%d> DNS: IPID 0x%02x, TransID 0x%04x\n",
							status, u2IpId, u2TransId);
			} else if (u2UdpSrcPort == UDP_PORT_DNS && eventType == EVENT_RX) { /* rx dns */
				UINT_16 u2TransId = (pucUdpPayload[0] << 8) | pucUdpPayload[1];

				if ((su2TxDoneCfg & CFG_DNS) == 0)
					break;
				DBGLOG(RX, INFO, "<RX> DNS: IPID 0x%02x, TransID 0x%04x\n", u2IpId, u2TransId);
			} else if ((su2TxDoneCfg & CFG_UDP) != 0) {
				switch (eventType) {
				case EVENT_RX:
					DBGLOG(RX, INFO, "<RX> UDP: IPID 0x%04x\n", u2IpId);
					break;
				case EVENT_TX:
					DBGLOG(TX, INFO, "<TX> UDP: IPID 0x%04x\n", u2IpId);
					prMsduInfo->fgNeedTxDoneStatus = TRUE;
					break;
				case EVENT_TX_DONE:
					DBGLOG(TX, INFO, "<TX status:%d> UDP: IPID 0x%04x\n", status, u2IpId);
					break;
				}
			}
			break;
		}
		case IP_PRO_TCP:
			if ((su2TxDoneCfg & CFG_TCP) == 0)
				break;

			switch (eventType) {
			case EVENT_RX:
				DBGLOG(RX, INFO, "<RX> TCP: IPID 0x%04x\n", u2IpId);
				break;
			case EVENT_TX:
				DBGLOG(TX, INFO, "<TX> TCP: IPID 0x%04x\n", u2IpId);
				prMsduInfo->fgNeedTxDoneStatus = TRUE;
				break;
			case EVENT_TX_DONE:
				DBGLOG(TX, INFO, "<TX status:%d> TCP: IPID 0x%04x\n", status, u2IpId);
				break;
			}
			break;
		}
		break;
	}
	case ETH_P_PRE_1X:
		DBGLOG(RX, INFO, "pre-1x\n");
	case ETH_P_1X:
	{
		PUINT_8 pucEapol = pucEthBody;
		UINT_8 ucEapolType = pucEapol[1];

		switch (ucEapolType) {
		case 0: /* eap packet */
			switch (eventType) {
			case EVENT_RX:
				DBGLOG(RX, INFO, "<RX> EAP Packet: code %d, id %d, type %d\n",
						pucEapol[4], pucEapol[5], pucEapol[7]);
				break;
			case EVENT_TX:
				DBGLOG(TX, INFO, "<TX> EAP Packet: code %d, id %d, type %d\n",
						pucEapol[4], pucEapol[5], pucEapol[7]);
				break;
			case EVENT_TX_DONE:
				DBGLOG(TX, INFO, "<TX status: %d> EAP Packet: code %d, id %d, type %d\n",
						status, pucEapol[4], pucEapol[5], pucEapol[7]);
				break;
			}
			break;
		case 1: /* eapol start */
			switch (eventType) {
			case EVENT_RX:
				DBGLOG(RX, INFO, "<RX> EAPOL: start\n");
				break;
			case EVENT_TX:
				DBGLOG(TX, INFO, "<RX> EAPOL: start\n");
				break;
			case EVENT_TX_DONE:
				DBGLOG(TX, INFO, "<TX status: %d> EAPOL: start\n", status);
				break;
			}
			break;
		case 3: /* key */
		{
			UINT_16 u2KeyInfo = pucEapol[5]<<8 | pucEapol[6];

			switch (eventType) {
			case EVENT_RX:
				DBGLOG(RX, INFO,
					"<RX> EAPOL: key, KeyInfo 0x%04x, Nonce %02x%02x%02x%02x%02x%02x%02x%02x...\n",
					u2KeyInfo, pucEapol[17], pucEapol[18], pucEapol[19], pucEapol[20],
					pucEapol[21], pucEapol[22], pucEapol[23], pucEapol[24]);
				break;
			case EVENT_TX:
				DBGLOG(TX, INFO,
					"<TX> EAPOL: key, KeyInfo 0x%04x, Nonce %02x%02x%02x%02x%02x%02x%02x%02x...\n",
					u2KeyInfo,
					pucEapol[17], pucEapol[18], pucEapol[19], pucEapol[20],
					pucEapol[21], pucEapol[22], pucEapol[23], pucEapol[24]);
				break;
			case EVENT_TX_DONE:
				DBGLOG(TX, INFO,
					"<TX status: %d> EAPOL: key, KeyInfo 0x%04x, Nonce %02x%02x%02x%02x%02x%02x%02x%02x...\n",
					status, u2KeyInfo, pucEapol[17], pucEapol[18], pucEapol[19],
					pucEapol[20], pucEapol[21], pucEapol[22], pucEapol[23], pucEapol[24]);
				break;
			}

			break;
		}
		}
		break;
	}
	case ETH_WPI_1X:
	{
		UINT_8 ucSubType = pucEthBody[3]; /* sub type filed*/
		UINT_16 u2Length = *(PUINT_16)&pucEthBody[6];
		UINT_16 u2Seq = *(PUINT_16)&pucEthBody[8];

		switch (eventType) {
		case EVENT_RX:
			DBGLOG(RX, INFO, "<RX> WAPI: subType %d, Len %d, Seq %d\n",
					ucSubType, u2Length, u2Seq);
			break;
		case EVENT_TX:
			DBGLOG(TX, INFO, "<TX> WAPI: subType %d, Len %d, Seq %d\n",
					ucSubType, u2Length, u2Seq);
			break;
		case EVENT_TX_DONE:
			DBGLOG(TX, INFO, "<TX status: %d> WAPI: subType %d, Len %d, Seq %d\n",
					status, ucSubType, u2Length, u2Seq);
			break;
		}
		break;
	}
	}
}
/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to display rx packet information.
*
* \param[in] pPkt			Pointer to the packet
* \param[out] None
*
* \retval None
*/
/*----------------------------------------------------------------------------*/
VOID StatsRxPktInfoDisplay(UINT_8 *pPkt)
{
	statsParsePktInfo(pPkt, 0, EVENT_RX, NULL);
#if 0				/* carefully! too many ARP */
	if (pucIpHdr[0] == 0x00) {	/* ARP */
		UINT_8 *pucDstIp = (UINT_8 *) pucIpHdr;

		if (pucDstIp[7] == ARP_PRO_REQ) {
			DBGLOG(RX, TRACE, "<rx> OS rx a arp req from %d.%d.%d.%d\n",
					     pucDstIp[14], pucDstIp[15], pucDstIp[16], pucDstIp[17]);
		} else if (pucDstIp[7] == ARP_PRO_RSP) {
			DBGLOG(RX, TRACE, "<rx> OS rx a arp rsp from %d.%d.%d.%d\n",
					     pucDstIp[24], pucDstIp[25], pucDstIp[26], pucDstIp[27]);
		}
	}
#endif

}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to display tx packet information.
*
* \param[in] pPkt			Pointer to the packet
* \param[out] None
*
* \retval None
*/
/*----------------------------------------------------------------------------*/
VOID StatsTxPktCallBack(UINT_8 *pPkt, P_MSDU_INFO_T prMsduInfo)
{
	UINT_16 u2EtherTypeLen;

	u2EtherTypeLen = (pPkt[ETH_TYPE_LEN_OFFSET] << 8) | (pPkt[ETH_TYPE_LEN_OFFSET + 1]);
	statsParsePktInfo(pPkt, 0, EVENT_TX, prMsduInfo);
}

/*----------------------------------------------------------------------------*/
/*! \brief  This routine is called to handle display tx packet tx done information.
*
* \param[in] pPkt			Pointer to the packet
* \param[out] None
*
* \retval None
*/
/*----------------------------------------------------------------------------*/
VOID StatsTxPktDoneInfoDisplay(ADAPTER_T *prAdapter, UINT_8 *pucEvtBuf)
{
	EVENT_TX_DONE_STATUS_T *prTxDone;

	prTxDone = (EVENT_TX_DONE_STATUS_T *) pucEvtBuf;
	/*
	 * Why 65 Bytes:
	 * 8B + wlanheader(40B) + hif_tx_header(16B) + 6B + 6B(LLC) - 12B
	 */
	statsParsePktInfo(&prTxDone->aucPktBuf[64], prTxDone->ucStatus, EVENT_TX_DONE, NULL);
}

VOID StatsSetCfgTxDone(UINT_16 u2Cfg, BOOLEAN fgSet)
{
	if (fgSet)
		su2TxDoneCfg |= u2Cfg;
	else
		su2TxDoneCfg &= ~u2Cfg;
}

UINT_16 StatsGetCfgTxDone(VOID)
{
	return su2TxDoneCfg;
}
