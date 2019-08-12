// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/

#include "Mp_Precomp.h"

/*  Global variables, these are static variables */
static COEX_DM_8723B_1ANT GLCoexDm8723b1Ant;
static PCOEX_DM_8723B_1ANT pCoexDm = &GLCoexDm8723b1Ant;
static COEX_STA_8723B_1ANT GLCoexSta8723b1Ant;
static PCOEX_STA_8723B_1ANT	pCoexSta = &GLCoexSta8723b1Ant;

static const char *const GLBtInfoSrc8723b1Ant[] = {
	"BT Info[wifi fw]",
	"BT Info[bt rsp]",
	"BT Info[bt auto report]",
};

static u32 GLCoexVerDate8723b1Ant = 20140507;
static u32 GLCoexVer8723b1Ant = 0x4e;

/*  local function proto type if needed */
/*  local function start with halbtc8723b1ant_ */
static u8 halbtc8723b1ant_BtRssiState(
	u8 levelNum, u8 rssiThresh, u8 rssiThresh1
)
{
	s32 btRssi = 0;
	u8 btRssiState = pCoexSta->preBtRssiState;

	btRssi = pCoexSta->btRssi;

	if (levelNum == 2) {
		if (
			(pCoexSta->preBtRssiState == BTC_RSSI_STATE_LOW) ||
			(pCoexSta->preBtRssiState == BTC_RSSI_STATE_STAY_LOW)
		) {
			if (btRssi >= (rssiThresh+BTC_RSSI_COEX_THRESH_TOL_8723B_1ANT)) {

				btRssiState = BTC_RSSI_STATE_HIGH;
				BTC_PRINT(
					BTC_MSG_ALGORITHM,
					ALGO_BT_RSSI_STATE,
					("[BTCoex], BT Rssi state switch to High\n")
				);
			} else {
				btRssiState = BTC_RSSI_STATE_STAY_LOW;
				BTC_PRINT(
					BTC_MSG_ALGORITHM,
					ALGO_BT_RSSI_STATE,
					("[BTCoex], BT Rssi state stay at Low\n")
				);
			}
		} else {
			if (btRssi < rssiThresh) {
				btRssiState = BTC_RSSI_STATE_LOW;
				BTC_PRINT(
					BTC_MSG_ALGORITHM,
					ALGO_BT_RSSI_STATE,
					("[BTCoex], BT Rssi state switch to Low\n")
				);
			} else {
				btRssiState = BTC_RSSI_STATE_STAY_HIGH;
				BTC_PRINT(
					BTC_MSG_ALGORITHM,
					ALGO_BT_RSSI_STATE,
					("[BTCoex], BT Rssi state stay at High\n")
				);
			}
		}
	} else if (levelNum == 3) {
		if (rssiThresh > rssiThresh1) {
			BTC_PRINT(
				BTC_MSG_ALGORITHM,
				ALGO_BT_RSSI_STATE,
				("[BTCoex], BT Rssi thresh error!!\n")
			);
			return pCoexSta->preBtRssiState;
		}

		if (
			(pCoexSta->preBtRssiState == BTC_RSSI_STATE_LOW) ||
			(pCoexSta->preBtRssiState == BTC_RSSI_STATE_STAY_LOW)
		) {
			if (btRssi >= (rssiThresh+BTC_RSSI_COEX_THRESH_TOL_8723B_1ANT)) {
				btRssiState = BTC_RSSI_STATE_MEDIUM;
				BTC_PRINT(
					BTC_MSG_ALGORITHM,
					ALGO_BT_RSSI_STATE,
					("[BTCoex], BT Rssi state switch to Medium\n")
				);
			} else {
				btRssiState = BTC_RSSI_STATE_STAY_LOW;
				BTC_PRINT(
					BTC_MSG_ALGORITHM,
					ALGO_BT_RSSI_STATE,
					("[BTCoex], BT Rssi state stay at Low\n")
				);
			}
		} else if (
			(pCoexSta->preBtRssiState == BTC_RSSI_STATE_MEDIUM) ||
			(pCoexSta->preBtRssiState == BTC_RSSI_STATE_STAY_MEDIUM)
		) {
			if (btRssi >= (rssiThresh1+BTC_RSSI_COEX_THRESH_TOL_8723B_1ANT)) {
				btRssiState = BTC_RSSI_STATE_HIGH;
				BTC_PRINT(
					BTC_MSG_ALGORITHM,
					ALGO_BT_RSSI_STATE,
					("[BTCoex], BT Rssi state switch to High\n")
				);
			} else if (btRssi < rssiThresh) {
				btRssiState = BTC_RSSI_STATE_LOW;
				BTC_PRINT(
					BTC_MSG_ALGORITHM,
					ALGO_BT_RSSI_STATE,
					("[BTCoex], BT Rssi state switch to Low\n")
				);
			} else {
				btRssiState = BTC_RSSI_STATE_STAY_MEDIUM;
				BTC_PRINT(
					BTC_MSG_ALGORITHM,
					ALGO_BT_RSSI_STATE,
					("[BTCoex], BT Rssi state stay at Medium\n")
				);
			}
		} else {
			if (btRssi < rssiThresh1) {
				btRssiState = BTC_RSSI_STATE_MEDIUM;
				BTC_PRINT(
					BTC_MSG_ALGORITHM,
					ALGO_BT_RSSI_STATE,
					("[BTCoex], BT Rssi state switch to Medium\n")
				);
			} else {
				btRssiState = BTC_RSSI_STATE_STAY_HIGH;
				BTC_PRINT(
					BTC_MSG_ALGORITHM,
					ALGO_BT_RSSI_STATE,
					("[BTCoex], BT Rssi state stay at High\n")
				);
			}
		}
	}

	pCoexSta->preBtRssiState = btRssiState;

	return btRssiState;
}

static void halbtc8723b1ant_UpdateRaMask(
	PBTC_COEXIST pBtCoexist, bool bForceExec, u32 disRateMask
)
{
	pCoexDm->curRaMask = disRateMask;

	if (bForceExec || (pCoexDm->preRaMask != pCoexDm->curRaMask))
		pBtCoexist->fBtcSet(
			pBtCoexist,
			BTC_SET_ACT_UPDATE_RAMASK,
			&pCoexDm->curRaMask
		);
	pCoexDm->preRaMask = pCoexDm->curRaMask;
}

static void halbtc8723b1ant_AutoRateFallbackRetry(
	PBTC_COEXIST pBtCoexist, bool bForceExec, u8 type
)
{
	bool bWifiUnderBMode = false;

	pCoexDm->curArfrType = type;

	if (bForceExec || (pCoexDm->preArfrType != pCoexDm->curArfrType)) {
		switch (pCoexDm->curArfrType) {
		case 0:	/*  normal mode */
			pBtCoexist->fBtcWrite4Byte(
				pBtCoexist, 0x430, pCoexDm->backupArfrCnt1
			);
			pBtCoexist->fBtcWrite4Byte(
				pBtCoexist, 0x434, pCoexDm->backupArfrCnt2
			);
			break;
		case 1:
			pBtCoexist->fBtcGet(
				pBtCoexist, BTC_GET_BL_WIFI_UNDER_B_MODE, &bWifiUnderBMode
			);
			if (bWifiUnderBMode) {
				pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x430, 0x0);
				pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x434, 0x01010101);
			} else {
				pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x430, 0x0);
				pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x434, 0x04030201);
			}
			break;
		default:
			break;
		}
	}

	pCoexDm->preArfrType = pCoexDm->curArfrType;
}

static void halbtc8723b1ant_RetryLimit(
	PBTC_COEXIST pBtCoexist, bool bForceExec, u8 type
)
{
	pCoexDm->curRetryLimitType = type;

	if (
		bForceExec ||
		(pCoexDm->preRetryLimitType != pCoexDm->curRetryLimitType)
	) {
		switch (pCoexDm->curRetryLimitType) {
		case 0:	/*  normal mode */
			pBtCoexist->fBtcWrite2Byte(
				pBtCoexist, 0x42a, pCoexDm->backupRetryLimit
			);
			break;
		case 1:	/*  retry limit =8 */
			pBtCoexist->fBtcWrite2Byte(pBtCoexist, 0x42a, 0x0808);
			break;
		default:
			break;
		}
	}

	pCoexDm->preRetryLimitType = pCoexDm->curRetryLimitType;
}

static void halbtc8723b1ant_AmpduMaxTime(
	PBTC_COEXIST pBtCoexist, bool bForceExec, u8 type
)
{
	pCoexDm->curAmpduTimeType = type;

	if (
		bForceExec || (pCoexDm->preAmpduTimeType != pCoexDm->curAmpduTimeType)
	) {
		switch (pCoexDm->curAmpduTimeType) {
		case 0:	/*  normal mode */
			pBtCoexist->fBtcWrite1Byte(
				pBtCoexist, 0x456, pCoexDm->backupAmpduMaxTime
			);
			break;
		case 1:	/*  AMPDU timw = 0x38 * 32us */
			pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x456, 0x38);
			break;
		default:
			break;
		}
	}

	pCoexDm->preAmpduTimeType = pCoexDm->curAmpduTimeType;
}

static void halbtc8723b1ant_LimitedTx(
	PBTC_COEXIST pBtCoexist,
	bool bForceExec,
	u8 raMaskType,
	u8 arfrType,
	u8 retryLimitType,
	u8 ampduTimeType
)
{
	switch (raMaskType) {
	case 0:	/*  normal mode */
		halbtc8723b1ant_UpdateRaMask(pBtCoexist, bForceExec, 0x0);
		break;
	case 1:	/*  disable cck 1/2 */
		halbtc8723b1ant_UpdateRaMask(pBtCoexist, bForceExec, 0x00000003);
		break;
	case 2:	/*  disable cck 1/2/5.5, ofdm 6/9/12/18/24, mcs 0/1/2/3/4 */
		halbtc8723b1ant_UpdateRaMask(pBtCoexist, bForceExec, 0x0001f1f7);
		break;
	default:
		break;
	}

	halbtc8723b1ant_AutoRateFallbackRetry(pBtCoexist, bForceExec, arfrType);
	halbtc8723b1ant_RetryLimit(pBtCoexist, bForceExec, retryLimitType);
	halbtc8723b1ant_AmpduMaxTime(pBtCoexist, bForceExec, ampduTimeType);
}

static void halbtc8723b1ant_LimitedRx(
	PBTC_COEXIST pBtCoexist,
	bool bForceExec,
	bool bRejApAggPkt,
	bool bBtCtrlAggBufSize,
	u8 aggBufSize
)
{
	bool bRejectRxAgg = bRejApAggPkt;
	bool bBtCtrlRxAggSize = bBtCtrlAggBufSize;
	u8 rxAggSize = aggBufSize;

	/*  */
	/* 	Rx Aggregation related setting */
	/*  */
	pBtCoexist->fBtcSet(
		pBtCoexist, BTC_SET_BL_TO_REJ_AP_AGG_PKT, &bRejectRxAgg
	);
	/*  decide BT control aggregation buf size or not */
	pBtCoexist->fBtcSet(
		pBtCoexist, BTC_SET_BL_BT_CTRL_AGG_SIZE, &bBtCtrlRxAggSize
	);
	/*  aggregation buf size, only work when BT control Rx aggregation size. */
	pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_U1_AGG_BUF_SIZE, &rxAggSize);
	/*  real update aggregation setting */
	pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_ACT_AGGREGATE_CTRL, NULL);


}

static void halbtc8723b1ant_QueryBtInfo(PBTC_COEXIST pBtCoexist)
{
	u8 	H2C_Parameter[1] = {0};

	pCoexSta->bC2hBtInfoReqSent = true;

	H2C_Parameter[0] |= BIT0;	/*  trigger */

	BTC_PRINT(
		BTC_MSG_ALGORITHM,
		ALGO_TRACE_FW_EXEC,
		("[BTCoex], Query Bt Info, FW write 0x61 = 0x%x\n", H2C_Parameter[0])
	);

	pBtCoexist->fBtcFillH2c(pBtCoexist, 0x61, 1, H2C_Parameter);
}

static void halbtc8723b1ant_MonitorBtCtr(PBTC_COEXIST pBtCoexist)
{
	u32 regHPTxRx, regLPTxRx, u4Tmp;
	u32 regHPTx = 0, regHPRx = 0, regLPTx = 0, regLPRx = 0;
	static u8 NumOfBtCounterChk;

       /* to avoid 0x76e[3] = 1 (WLAN_Act control by PTA) during IPS */
	/* if (! (pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x76e) & 0x8)) */

	if (pCoexSta->bUnderIps) {
		pCoexSta->highPriorityTx = 65535;
		pCoexSta->highPriorityRx = 65535;
		pCoexSta->lowPriorityTx = 65535;
		pCoexSta->lowPriorityRx = 65535;
		return;
	}

	regHPTxRx = 0x770;
	regLPTxRx = 0x774;

	u4Tmp = pBtCoexist->fBtcRead4Byte(pBtCoexist, regHPTxRx);
	regHPTx = u4Tmp & bMaskLWord;
	regHPRx = (u4Tmp & bMaskHWord)>>16;

	u4Tmp = pBtCoexist->fBtcRead4Byte(pBtCoexist, regLPTxRx);
	regLPTx = u4Tmp & bMaskLWord;
	regLPRx = (u4Tmp & bMaskHWord)>>16;

	pCoexSta->highPriorityTx = regHPTx;
	pCoexSta->highPriorityRx = regHPRx;
	pCoexSta->lowPriorityTx = regLPTx;
	pCoexSta->lowPriorityRx = regLPRx;

	if ((pCoexSta->lowPriorityTx >= 1050) && (!pCoexSta->bC2hBtInquiryPage))
		pCoexSta->popEventCnt++;

	BTC_PRINT(
		BTC_MSG_ALGORITHM,
		ALGO_TRACE,
		(
			"[BTCoex], Hi-Pri Rx/Tx: %d/%d, Lo-Pri Rx/Tx: %d/%d\n",
			regHPRx,
			regHPTx,
			regLPRx,
			regLPTx
		)
	);

	/*  reset counter */
	pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x76e, 0xc);

	if ((regHPTx == 0) && (regHPRx == 0) && (regLPTx == 0) && (regLPRx == 0)) {
		NumOfBtCounterChk++;
		if (NumOfBtCounterChk >= 3) {
			halbtc8723b1ant_QueryBtInfo(pBtCoexist);
			NumOfBtCounterChk = 0;
		}
	}
}


static void halbtc8723b1ant_MonitorWiFiCtr(PBTC_COEXIST pBtCoexist)
{
	s32	wifiRssi = 0;
	bool bWifiBusy = false, bWifiUnderBMode = false;
	static u8 nCCKLockCounter;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_BUSY, &bWifiBusy);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_S4_WIFI_RSSI, &wifiRssi);
	pBtCoexist->fBtcGet(
		pBtCoexist, BTC_GET_BL_WIFI_UNDER_B_MODE, &bWifiUnderBMode
	);

	if (pCoexSta->bUnderIps) {
		pCoexSta->nCRCOK_CCK = 0;
		pCoexSta->nCRCOK_11g = 0;
		pCoexSta->nCRCOK_11n = 0;
		pCoexSta->nCRCOK_11nAgg = 0;

		pCoexSta->nCRCErr_CCK = 0;
		pCoexSta->nCRCErr_11g = 0;
		pCoexSta->nCRCErr_11n = 0;
		pCoexSta->nCRCErr_11nAgg = 0;
	} else {
		pCoexSta->nCRCOK_CCK	= pBtCoexist->fBtcRead4Byte(pBtCoexist, 0xf88);
		pCoexSta->nCRCOK_11g	= pBtCoexist->fBtcRead2Byte(pBtCoexist, 0xf94);
		pCoexSta->nCRCOK_11n	= pBtCoexist->fBtcRead2Byte(pBtCoexist, 0xf90);
		pCoexSta->nCRCOK_11nAgg = pBtCoexist->fBtcRead2Byte(pBtCoexist, 0xfb8);

		pCoexSta->nCRCErr_CCK	 = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0xf84);
		pCoexSta->nCRCErr_11g	 = pBtCoexist->fBtcRead2Byte(pBtCoexist, 0xf96);
		pCoexSta->nCRCErr_11n	 = pBtCoexist->fBtcRead2Byte(pBtCoexist, 0xf92);
		pCoexSta->nCRCErr_11nAgg = pBtCoexist->fBtcRead2Byte(pBtCoexist, 0xfba);
	}


	/* reset counter */
	pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0xf16, 0x1, 0x1);
	pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0xf16, 0x1, 0x0);

	if (bWifiBusy && (wifiRssi >= 30) && !bWifiUnderBMode) {
		if (
			(pCoexDm->btStatus == BT_8723B_1ANT_BT_STATUS_ACL_BUSY) ||
			(pCoexDm->btStatus == BT_8723B_1ANT_BT_STATUS_ACL_SCO_BUSY) ||
			(pCoexDm->btStatus == BT_8723B_1ANT_BT_STATUS_SCO_BUSY)
		) {
			if (
				pCoexSta->nCRCOK_CCK > (
					pCoexSta->nCRCOK_11g +
					pCoexSta->nCRCOK_11n +
					pCoexSta->nCRCOK_11nAgg
				)
			) {
				if (nCCKLockCounter < 5)
				 nCCKLockCounter++;
			} else {
				if (nCCKLockCounter > 0)
				 nCCKLockCounter--;
			}

		} else {
			if (nCCKLockCounter > 0)
			  nCCKLockCounter--;
		}
	} else {
		if (nCCKLockCounter > 0)
			nCCKLockCounter--;
	}

	if (!pCoexSta->bPreCCKLock) {

		if (nCCKLockCounter >= 5)
		 pCoexSta->bCCKLock = true;
		else
		 pCoexSta->bCCKLock = false;
	} else {
		if (nCCKLockCounter == 0)
		 pCoexSta->bCCKLock = false;
		else
		 pCoexSta->bCCKLock = true;
	}

	pCoexSta->bPreCCKLock =  pCoexSta->bCCKLock;


}

static bool halbtc8723b1ant_IsWifiStatusChanged(PBTC_COEXIST pBtCoexist)
{
	static bool	bPreWifiBusy, bPreUnder4way, bPreBtHsOn;
	bool bWifiBusy = false, bUnder4way = false, bBtHsOn = false;
	bool bWifiConnected = false;

	pBtCoexist->fBtcGet(
		pBtCoexist, BTC_GET_BL_WIFI_CONNECTED, &bWifiConnected
	);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_BUSY, &bWifiBusy);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_HS_OPERATION, &bBtHsOn);
	pBtCoexist->fBtcGet(
		pBtCoexist, BTC_GET_BL_WIFI_4_WAY_PROGRESS, &bUnder4way
	);

	if (bWifiConnected) {
		if (bWifiBusy != bPreWifiBusy) {
			bPreWifiBusy = bWifiBusy;
			return true;
		}

		if (bUnder4way != bPreUnder4way) {
			bPreUnder4way = bUnder4way;
			return true;
		}

		if (bBtHsOn != bPreBtHsOn) {
			bPreBtHsOn = bBtHsOn;
			return true;
		}
	}

	return false;
}

static void halbtc8723b1ant_UpdateBtLinkInfo(PBTC_COEXIST pBtCoexist)
{
	PBTC_BT_LINK_INFO pBtLinkInfo = &pBtCoexist->btLinkInfo;
	bool bBtHsOn = false;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_HS_OPERATION, &bBtHsOn);

	pBtLinkInfo->bBtLinkExist = pCoexSta->bBtLinkExist;
	pBtLinkInfo->bScoExist = pCoexSta->bScoExist;
	pBtLinkInfo->bA2dpExist = pCoexSta->bA2dpExist;
	pBtLinkInfo->bPanExist = pCoexSta->bPanExist;
	pBtLinkInfo->bHidExist = pCoexSta->bHidExist;

	/*  work around for HS mode. */
	if (bBtHsOn) {
		pBtLinkInfo->bPanExist = true;
		pBtLinkInfo->bBtLinkExist = true;
	}

	/*  check if Sco only */
	if (
		pBtLinkInfo->bScoExist &&
		!pBtLinkInfo->bA2dpExist &&
		!pBtLinkInfo->bPanExist &&
		!pBtLinkInfo->bHidExist
	)
		pBtLinkInfo->bScoOnly = true;
	else
		pBtLinkInfo->bScoOnly = false;

	/*  check if A2dp only */
	if (
		!pBtLinkInfo->bScoExist &&
		pBtLinkInfo->bA2dpExist &&
		!pBtLinkInfo->bPanExist &&
		!pBtLinkInfo->bHidExist
	)
		pBtLinkInfo->bA2dpOnly = true;
	else
		pBtLinkInfo->bA2dpOnly = false;

	/*  check if Pan only */
	if (
		!pBtLinkInfo->bScoExist &&
		!pBtLinkInfo->bA2dpExist &&
		pBtLinkInfo->bPanExist &&
		!pBtLinkInfo->bHidExist
	)
		pBtLinkInfo->bPanOnly = true;
	else
		pBtLinkInfo->bPanOnly = false;

	/*  check if Hid only */
	if (
		!pBtLinkInfo->bScoExist &&
		!pBtLinkInfo->bA2dpExist &&
		!pBtLinkInfo->bPanExist &&
		pBtLinkInfo->bHidExist
	)
		pBtLinkInfo->bHidOnly = true;
	else
		pBtLinkInfo->bHidOnly = false;
}

static u8 halbtc8723b1ant_ActionAlgorithm(PBTC_COEXIST pBtCoexist)
{
	PBTC_BT_LINK_INFO pBtLinkInfo = &pBtCoexist->btLinkInfo;
	bool bBtHsOn = false;
	u8 algorithm = BT_8723B_1ANT_COEX_ALGO_UNDEFINED;
	u8 numOfDiffProfile = 0;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_HS_OPERATION, &bBtHsOn);

	if (!pBtLinkInfo->bBtLinkExist) {
		BTC_PRINT(
			BTC_MSG_ALGORITHM,
			ALGO_TRACE,
			("[BTCoex], No BT link exists!!!\n")
		);
		return algorithm;
	}

	if (pBtLinkInfo->bScoExist)
		numOfDiffProfile++;
	if (pBtLinkInfo->bHidExist)
		numOfDiffProfile++;
	if (pBtLinkInfo->bPanExist)
		numOfDiffProfile++;
	if (pBtLinkInfo->bA2dpExist)
		numOfDiffProfile++;

	if (numOfDiffProfile == 1) {
		if (pBtLinkInfo->bScoExist) {
			BTC_PRINT(
				BTC_MSG_ALGORITHM,
				ALGO_TRACE,
				("[BTCoex], BT Profile = SCO only\n")
			);
			algorithm = BT_8723B_1ANT_COEX_ALGO_SCO;
		} else {
			if (pBtLinkInfo->bHidExist) {
				BTC_PRINT(
					BTC_MSG_ALGORITHM,
					ALGO_TRACE,
					("[BTCoex], BT Profile = HID only\n")
				);
				algorithm = BT_8723B_1ANT_COEX_ALGO_HID;
			} else if (pBtLinkInfo->bA2dpExist) {
				BTC_PRINT(
					BTC_MSG_ALGORITHM,
					ALGO_TRACE,
					("[BTCoex], BT Profile = A2DP only\n")
				);
				algorithm = BT_8723B_1ANT_COEX_ALGO_A2DP;
			} else if (pBtLinkInfo->bPanExist) {
				if (bBtHsOn) {
					BTC_PRINT(
						BTC_MSG_ALGORITHM,
						ALGO_TRACE,
						("[BTCoex], BT Profile = PAN(HS) only\n")
					);
					algorithm = BT_8723B_1ANT_COEX_ALGO_PANHS;
				} else {
					BTC_PRINT(
						BTC_MSG_ALGORITHM,
						ALGO_TRACE,
						("[BTCoex], BT Profile = PAN(EDR) only\n")
					);
					algorithm = BT_8723B_1ANT_COEX_ALGO_PANEDR;
				}
			}
		}
	} else if (numOfDiffProfile == 2) {
		if (pBtLinkInfo->bScoExist) {
			if (pBtLinkInfo->bHidExist) {
				BTC_PRINT(
					BTC_MSG_ALGORITHM,
					ALGO_TRACE,
					("[BTCoex], BT Profile = SCO + HID\n")
				);
				algorithm = BT_8723B_1ANT_COEX_ALGO_HID;
			} else if (pBtLinkInfo->bA2dpExist) {
				BTC_PRINT(
					BTC_MSG_ALGORITHM,
					ALGO_TRACE,
					("[BTCoex], BT Profile = SCO + A2DP ==> SCO\n")
				);
				algorithm = BT_8723B_1ANT_COEX_ALGO_SCO;
			} else if (pBtLinkInfo->bPanExist) {
				if (bBtHsOn) {
					BTC_PRINT(
						BTC_MSG_ALGORITHM,
						ALGO_TRACE,
						("[BTCoex], BT Profile = SCO + PAN(HS)\n")
					);
					algorithm = BT_8723B_1ANT_COEX_ALGO_SCO;
				} else {
					BTC_PRINT(
						BTC_MSG_ALGORITHM,
						ALGO_TRACE,
						("[BTCoex], BT Profile = SCO + PAN(EDR)\n")
					);
					algorithm = BT_8723B_1ANT_COEX_ALGO_PANEDR_HID;
				}
			}
		} else {
			if (pBtLinkInfo->bHidExist && pBtLinkInfo->bA2dpExist) {
				BTC_PRINT(
					BTC_MSG_ALGORITHM,
					ALGO_TRACE,
					("[BTCoex], BT Profile = HID + A2DP\n")
				);
				algorithm = BT_8723B_1ANT_COEX_ALGO_HID_A2DP;
			} else if (pBtLinkInfo->bHidExist && pBtLinkInfo->bPanExist) {
				if (bBtHsOn) {
					BTC_PRINT(
						BTC_MSG_ALGORITHM,
						ALGO_TRACE,
						("[BTCoex], BT Profile = HID + PAN(HS)\n")
					);
					algorithm = BT_8723B_1ANT_COEX_ALGO_HID_A2DP;
				} else {
					BTC_PRINT(
						BTC_MSG_ALGORITHM,
						ALGO_TRACE,
						("[BTCoex], BT Profile = HID + PAN(EDR)\n")
					);
					algorithm = BT_8723B_1ANT_COEX_ALGO_PANEDR_HID;
				}
			} else if (pBtLinkInfo->bPanExist && pBtLinkInfo->bA2dpExist) {
				if (bBtHsOn) {
					BTC_PRINT(
						BTC_MSG_ALGORITHM,
						ALGO_TRACE,
						("[BTCoex], BT Profile = A2DP + PAN(HS)\n")
					);
					algorithm = BT_8723B_1ANT_COEX_ALGO_A2DP_PANHS;
				} else {
					BTC_PRINT(
						BTC_MSG_ALGORITHM,
						ALGO_TRACE,
						("[BTCoex], BT Profile = A2DP + PAN(EDR)\n")
					);
					algorithm = BT_8723B_1ANT_COEX_ALGO_PANEDR_A2DP;
				}
			}
		}
	} else if (numOfDiffProfile == 3) {
		if (pBtLinkInfo->bScoExist) {
			if (pBtLinkInfo->bHidExist && pBtLinkInfo->bA2dpExist) {
				BTC_PRINT(
					BTC_MSG_ALGORITHM,
					ALGO_TRACE,
					("[BTCoex], BT Profile = SCO + HID + A2DP ==> HID\n")
				);
				algorithm = BT_8723B_1ANT_COEX_ALGO_HID;
			} else if (
				pBtLinkInfo->bHidExist && pBtLinkInfo->bPanExist
			) {
				if (bBtHsOn) {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BT Profile = SCO + HID + PAN(HS)\n"));
					algorithm = BT_8723B_1ANT_COEX_ALGO_HID_A2DP;
				} else {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BT Profile = SCO + HID + PAN(EDR)\n"));
					algorithm = BT_8723B_1ANT_COEX_ALGO_PANEDR_HID;
				}
			} else if (pBtLinkInfo->bPanExist && pBtLinkInfo->bA2dpExist) {
				if (bBtHsOn) {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BT Profile = SCO + A2DP + PAN(HS)\n"));
					algorithm = BT_8723B_1ANT_COEX_ALGO_SCO;
				} else {
					BTC_PRINT(
						BTC_MSG_ALGORITHM,
						ALGO_TRACE,
						("[BTCoex], BT Profile = SCO + A2DP + PAN(EDR) ==> HID\n")
					);
					algorithm = BT_8723B_1ANT_COEX_ALGO_PANEDR_HID;
				}
			}
		} else {
			if (
				pBtLinkInfo->bHidExist &&
				pBtLinkInfo->bPanExist &&
				pBtLinkInfo->bA2dpExist
			) {
				if (bBtHsOn) {
					BTC_PRINT(
						BTC_MSG_ALGORITHM,
						ALGO_TRACE,
						("[BTCoex], BT Profile = HID + A2DP + PAN(HS)\n")
					);
					algorithm = BT_8723B_1ANT_COEX_ALGO_HID_A2DP;
				} else {
					BTC_PRINT(
						BTC_MSG_ALGORITHM,
						ALGO_TRACE,
						("[BTCoex], BT Profile = HID + A2DP + PAN(EDR)\n")
					);
					algorithm = BT_8723B_1ANT_COEX_ALGO_HID_A2DP_PANEDR;
				}
			}
		}
	} else if (numOfDiffProfile >= 3) {
		if (pBtLinkInfo->bScoExist) {
			if (
				pBtLinkInfo->bHidExist &&
				pBtLinkInfo->bPanExist &&
				pBtLinkInfo->bA2dpExist
			) {
				if (bBtHsOn) {
					BTC_PRINT(
						BTC_MSG_ALGORITHM,
						ALGO_TRACE,
						("[BTCoex], Error!!! BT Profile = SCO + HID + A2DP + PAN(HS)\n")
					);

				} else {
					BTC_PRINT(
						BTC_MSG_ALGORITHM,
						ALGO_TRACE,
						("[BTCoex], BT Profile = SCO + HID + A2DP + PAN(EDR) ==>PAN(EDR)+HID\n")
					);
					algorithm = BT_8723B_1ANT_COEX_ALGO_PANEDR_HID;
				}
			}
		}
	}

	return algorithm;
}

static void halbtc8723b1ant_SetSwPenaltyTxRateAdaptive(
	PBTC_COEXIST pBtCoexist, bool bLowPenaltyRa
)
{
	u8 	H2C_Parameter[6] = {0};

	H2C_Parameter[0] = 0x6;	/*  opCode, 0x6 = Retry_Penalty */

	if (bLowPenaltyRa) {
		H2C_Parameter[1] |= BIT0;
		H2C_Parameter[2] = 0x00;  /* normal rate except MCS7/6/5, OFDM54/48/36 */
		H2C_Parameter[3] = 0xf7;  /* MCS7 or OFDM54 */
		H2C_Parameter[4] = 0xf8;  /* MCS6 or OFDM48 */
		H2C_Parameter[5] = 0xf9;	/* MCS5 or OFDM36 */
	}

	BTC_PRINT(
		BTC_MSG_ALGORITHM,
		ALGO_TRACE_FW_EXEC,
		(
			"[BTCoex], set WiFi Low-Penalty Retry: %s",
			(bLowPenaltyRa ? "ON!!" : "OFF!!")
		)
	);

	pBtCoexist->fBtcFillH2c(pBtCoexist, 0x69, 6, H2C_Parameter);
}

static void halbtc8723b1ant_LowPenaltyRa(
	PBTC_COEXIST pBtCoexist, bool bForceExec, bool bLowPenaltyRa
)
{
	pCoexDm->bCurLowPenaltyRa = bLowPenaltyRa;

	if (!bForceExec) {
		if (pCoexDm->bPreLowPenaltyRa == pCoexDm->bCurLowPenaltyRa)
			return;
	}
	halbtc8723b1ant_SetSwPenaltyTxRateAdaptive(
		pBtCoexist, pCoexDm->bCurLowPenaltyRa
	);

	pCoexDm->bPreLowPenaltyRa = pCoexDm->bCurLowPenaltyRa;
}

static void halbtc8723b1ant_SetCoexTable(
	PBTC_COEXIST pBtCoexist,
	u32 val0x6c0,
	u32 val0x6c4,
	u32 val0x6c8,
	u8 val0x6cc
)
{
	BTC_PRINT(
		BTC_MSG_ALGORITHM,
		ALGO_TRACE_SW_EXEC,
		("[BTCoex], set coex table, set 0x6c0 = 0x%x\n", val0x6c0)
	);
	pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x6c0, val0x6c0);

	BTC_PRINT(
		BTC_MSG_ALGORITHM,
		ALGO_TRACE_SW_EXEC,
		("[BTCoex], set coex table, set 0x6c4 = 0x%x\n", val0x6c4)
	);
	pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x6c4, val0x6c4);

	BTC_PRINT(
		BTC_MSG_ALGORITHM,
		ALGO_TRACE_SW_EXEC,
		("[BTCoex], set coex table, set 0x6c8 = 0x%x\n", val0x6c8)
	);
	pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x6c8, val0x6c8);

	BTC_PRINT(
		BTC_MSG_ALGORITHM,
		ALGO_TRACE_SW_EXEC,
		("[BTCoex], set coex table, set 0x6cc = 0x%x\n", val0x6cc)
	);
	pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x6cc, val0x6cc);
}

static void halbtc8723b1ant_CoexTable(
	PBTC_COEXIST pBtCoexist,
	bool bForceExec,
	u32 val0x6c0,
	u32 val0x6c4,
	u32 val0x6c8,
	u8 val0x6cc
)
{
	BTC_PRINT(
		BTC_MSG_ALGORITHM,
		ALGO_TRACE_SW,
		(
			"[BTCoex], %s write Coex Table 0x6c0 = 0x%x, 0x6c4 = 0x%x, 0x6cc = 0x%x\n",
			(bForceExec ? "force to" : ""),
			val0x6c0, val0x6c4, val0x6cc
		)
	);
	pCoexDm->curVal0x6c0 = val0x6c0;
	pCoexDm->curVal0x6c4 = val0x6c4;
	pCoexDm->curVal0x6c8 = val0x6c8;
	pCoexDm->curVal0x6cc = val0x6cc;

	if (!bForceExec) {
		if (
			(pCoexDm->preVal0x6c0 == pCoexDm->curVal0x6c0) &&
		    (pCoexDm->preVal0x6c4 == pCoexDm->curVal0x6c4) &&
		    (pCoexDm->preVal0x6c8 == pCoexDm->curVal0x6c8) &&
		    (pCoexDm->preVal0x6cc == pCoexDm->curVal0x6cc)
		)
			return;
	}

	halbtc8723b1ant_SetCoexTable(
		pBtCoexist, val0x6c0, val0x6c4, val0x6c8, val0x6cc
	);

	pCoexDm->preVal0x6c0 = pCoexDm->curVal0x6c0;
	pCoexDm->preVal0x6c4 = pCoexDm->curVal0x6c4;
	pCoexDm->preVal0x6c8 = pCoexDm->curVal0x6c8;
	pCoexDm->preVal0x6cc = pCoexDm->curVal0x6cc;
}

static void halbtc8723b1ant_CoexTableWithType(
	PBTC_COEXIST pBtCoexist, bool bForceExec, u8 type
)
{
	BTC_PRINT(
		BTC_MSG_ALGORITHM,
		ALGO_TRACE,
		("[BTCoex], ********** CoexTable(%d) **********\n", type)
	);

	pCoexSta->nCoexTableType = type;

	switch (type) {
	case 0:
		halbtc8723b1ant_CoexTable(
			pBtCoexist, bForceExec, 0x55555555, 0x55555555, 0xffffff, 0x3
		);
		break;
	case 1:
		halbtc8723b1ant_CoexTable(
			pBtCoexist, bForceExec, 0x55555555, 0x5a5a5a5a, 0xffffff, 0x3
		);
		break;
	case 2:
		halbtc8723b1ant_CoexTable(
			pBtCoexist, bForceExec, 0x5a5a5a5a, 0x5a5a5a5a, 0xffffff, 0x3
		);
		break;
	case 3:
		halbtc8723b1ant_CoexTable(
			pBtCoexist, bForceExec, 0xaaaa5555, 0xaaaa5a5a, 0xffffff, 0x3
		);
		break;
	case 4:
		halbtc8723b1ant_CoexTable(
			pBtCoexist, bForceExec, 0x55555555, 0xaaaa5a5a, 0xffffff, 0x3
		);
		break;
	case 5:
		halbtc8723b1ant_CoexTable(
			pBtCoexist, bForceExec, 0x5a5a5a5a, 0xaaaa5a5a, 0xffffff, 0x3
		);
		break;
	case 6:
		halbtc8723b1ant_CoexTable(
			pBtCoexist, bForceExec, 0x55555555, 0xaaaaaaaa, 0xffffff, 0x3
		);
		break;
	case 7:
		halbtc8723b1ant_CoexTable(
			pBtCoexist, bForceExec, 0xaaaaaaaa, 0xaaaaaaaa, 0xffffff, 0x3
		);
		break;
	default:
		break;
	}
}

static void halbtc8723b1ant_SetFwIgnoreWlanAct(
	PBTC_COEXIST pBtCoexist, bool bEnable
)
{
	u8 H2C_Parameter[1] = {0};

	if (bEnable)
		H2C_Parameter[0] |= BIT0; /* function enable */

	BTC_PRINT(
		BTC_MSG_ALGORITHM,
		ALGO_TRACE_FW_EXEC,
		(
			"[BTCoex], set FW for BT Ignore Wlan_Act, FW write 0x63 = 0x%x\n",
			H2C_Parameter[0]
		)
	);

	pBtCoexist->fBtcFillH2c(pBtCoexist, 0x63, 1, H2C_Parameter);
}

static void halbtc8723b1ant_IgnoreWlanAct(
	PBTC_COEXIST pBtCoexist, bool bForceExec, bool bEnable
)
{
	BTC_PRINT(
		BTC_MSG_ALGORITHM,
		ALGO_TRACE_FW,
		(
			"[BTCoex], %s turn Ignore WlanAct %s\n",
			(bForceExec ? "force to" : ""),
			(bEnable ? "ON" : "OFF")
		)
	);
	pCoexDm->bCurIgnoreWlanAct = bEnable;

	if (!bForceExec) {
		BTC_PRINT(
			BTC_MSG_ALGORITHM,
			ALGO_TRACE_FW_DETAIL,
			(
				"[BTCoex], bPreIgnoreWlanAct = %d, bCurIgnoreWlanAct = %d!!\n",
				pCoexDm->bPreIgnoreWlanAct,
				pCoexDm->bCurIgnoreWlanAct
			)
		);

		if (pCoexDm->bPreIgnoreWlanAct == pCoexDm->bCurIgnoreWlanAct)
			return;
	}
	halbtc8723b1ant_SetFwIgnoreWlanAct(pBtCoexist, bEnable);

	pCoexDm->bPreIgnoreWlanAct = pCoexDm->bCurIgnoreWlanAct;
}

static void halbtc8723b1ant_SetLpsRpwm(
	PBTC_COEXIST pBtCoexist, u8 lpsVal, u8 rpwmVal
)
{
	u8 lps = lpsVal;
	u8 rpwm = rpwmVal;

	pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_U1_LPS_VAL, &lps);
	pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_U1_RPWM_VAL, &rpwm);
}

static void halbtc8723b1ant_LpsRpwm(
	PBTC_COEXIST pBtCoexist, bool bForceExec, u8 lpsVal, u8 rpwmVal
)
{
	BTC_PRINT(
		BTC_MSG_ALGORITHM,
		ALGO_TRACE_FW,
		(
			"[BTCoex], %s set lps/rpwm = 0x%x/0x%x\n",
			(bForceExec ? "force to" : ""),
			lpsVal,
			rpwmVal
		)
	);
	pCoexDm->curLps = lpsVal;
	pCoexDm->curRpwm = rpwmVal;

	if (!bForceExec) {
		BTC_PRINT(
			BTC_MSG_ALGORITHM,
			ALGO_TRACE_FW_DETAIL,
			(
				"[BTCoex], LPS-RxBeaconMode = 0x%x , LPS-RPWM = 0x%x!!\n",
				pCoexDm->curLps,
				pCoexDm->curRpwm
			)
		);

		if (
			(pCoexDm->preLps == pCoexDm->curLps) &&
			(pCoexDm->preRpwm == pCoexDm->curRpwm)
		) {
			BTC_PRINT(
				BTC_MSG_ALGORITHM,
				ALGO_TRACE_FW_DETAIL,
				(
					"[BTCoex], LPS-RPWM_Last = 0x%x , LPS-RPWM_Now = 0x%x!!\n",
					pCoexDm->preRpwm,
					pCoexDm->curRpwm
				)
			);

			return;
		}
	}
	halbtc8723b1ant_SetLpsRpwm(pBtCoexist, lpsVal, rpwmVal);

	pCoexDm->preLps = pCoexDm->curLps;
	pCoexDm->preRpwm = pCoexDm->curRpwm;
}

static void halbtc8723b1ant_SwMechanism(
	PBTC_COEXIST pBtCoexist, bool bLowPenaltyRA
)
{
	BTC_PRINT(
		BTC_MSG_ALGORITHM,
		ALGO_BT_MONITOR,
		("[BTCoex], SM[LpRA] = %d\n", bLowPenaltyRA)
	);

	halbtc8723b1ant_LowPenaltyRa(pBtCoexist, NORMAL_EXEC, bLowPenaltyRA);
}

static void halbtc8723b1ant_SetAntPath(
	PBTC_COEXIST pBtCoexist, u8 antPosType, bool bInitHwCfg, bool bWifiOff
)
{
	PBTC_BOARD_INFO pBoardInfo = &pBtCoexist->boardInfo;
	u32 fwVer = 0, u4Tmp = 0, cntBtCalChk = 0;
	bool bPgExtSwitch = false;
	bool bUseExtSwitch = false;
	bool bIsInMpMode = false;
	u8 H2C_Parameter[2] = {0}, u1Tmp = 0;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_EXT_SWITCH, &bPgExtSwitch);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_FW_VER, &fwVer); /*  [31:16]=fw ver, [15:0]=fw sub ver */

	if ((fwVer > 0 && fwVer < 0xc0000) || bPgExtSwitch)
		bUseExtSwitch = true;

	if (bInitHwCfg) {
		pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1, 0xfffff, 0x780); /* WiFi TRx Mask on */
		pBtCoexist->fBtcSetBtReg(pBtCoexist, BTC_BT_REG_RF, 0x3c, 0x15); /* BT TRx Mask on */

		if (fwVer >= 0x180000) {
			/* Use H2C to set GNT_BT to HIGH */
			H2C_Parameter[0] = 1;
			pBtCoexist->fBtcFillH2c(pBtCoexist, 0x6E, 1, H2C_Parameter);
		} else /*  set grant_bt to high */
			pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x765, 0x18);

		/* set wlan_act control by PTA */
		pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x76e, 0x4);

		pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0x67, 0x20, 0x1); /* BT select s0/s1 is controlled by WiFi */

		pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0x39, 0x8, 0x1);
		pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x974, 0xff);
		pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0x944, 0x3, 0x3);
		pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x930, 0x77);
	} else if (bWifiOff) {
		if (fwVer >= 0x180000) {
			/* Use H2C to set GNT_BT to HIGH */
			H2C_Parameter[0] = 1;
			pBtCoexist->fBtcFillH2c(pBtCoexist, 0x6E, 1, H2C_Parameter);
		} else /*  set grant_bt to high */
			pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x765, 0x18);

		/* set wlan_act to always low */
		pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x76e, 0x4);

		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_IS_IN_MP_MODE, &bIsInMpMode);
		if (!bIsInMpMode)
			pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0x67, 0x20, 0x0); /* BT select s0/s1 is controlled by BT */
		else
			pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0x67, 0x20, 0x1); /* BT select s0/s1 is controlled by WiFi */

		/*  0x4c[24:23]= 00, Set Antenna control by BT_RFE_CTRL	BT Vendor 0xac = 0xf002 */
		u4Tmp = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x4c);
		u4Tmp &= ~BIT23;
		u4Tmp &= ~BIT24;
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x4c, u4Tmp);
	} else {
		/* Use H2C to set GNT_BT to LOW */
		if (fwVer >= 0x180000) {
			if (pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x765) != 0) {
				H2C_Parameter[0] = 0;
				pBtCoexist->fBtcFillH2c(pBtCoexist, 0x6E, 1, H2C_Parameter);
			}
		} else {
			/*  BT calibration check */
			while (cntBtCalChk <= 20) {
				u1Tmp = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x49d);
				cntBtCalChk++;

				if (u1Tmp & BIT0) {
					BTC_PRINT(BTC_MSG_INTERFACE, INTF_INIT, ("[BTCoex], ########### BT is calibrating (wait cnt =%d) ###########\n", cntBtCalChk));
					mdelay(50);
				} else {
					BTC_PRINT(BTC_MSG_INTERFACE, INTF_INIT, ("[BTCoex], ********** BT is NOT calibrating (wait cnt =%d)**********\n", cntBtCalChk));
					break;
				}
			}

			/*  set grant_bt to PTA */
			pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x765, 0x0);
		}

		if (pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x76e) != 0xc)
			/* set wlan_act control by PTA */
			pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x76e, 0xc);
	}

	if (bUseExtSwitch) {
		if (bInitHwCfg) {
			/*  0x4c[23]= 0, 0x4c[24]= 1  Antenna control by WL/BT */
			u4Tmp = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x4c);
			u4Tmp &= ~BIT23;
			u4Tmp |= BIT24;
			pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x4c, u4Tmp);

			pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x948, 0x0); /*  fixed internal switch S1->WiFi, S0->BT */

			if (pBoardInfo->btdmAntPos == BTC_ANTENNA_AT_MAIN_PORT) {
				/* tell firmware "no antenna inverse" */
				H2C_Parameter[0] = 0;
				H2C_Parameter[1] = 1;  /* ext switch type */
				pBtCoexist->fBtcFillH2c(pBtCoexist, 0x65, 2, H2C_Parameter);
			} else {
				/* tell firmware "antenna inverse" */
				H2C_Parameter[0] = 1;
				H2C_Parameter[1] = 1;  /* ext switch type */
				pBtCoexist->fBtcFillH2c(pBtCoexist, 0x65, 2, H2C_Parameter);
			}
		}


		/*  ext switch setting */
		switch (antPosType) {
		case BTC_ANT_PATH_WIFI:
			if (pBoardInfo->btdmAntPos == BTC_ANTENNA_AT_MAIN_PORT)
				pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0x92c, 0x3, 0x1);
			else
				pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0x92c, 0x3, 0x2);
			break;
		case BTC_ANT_PATH_BT:
			if (pBoardInfo->btdmAntPos == BTC_ANTENNA_AT_MAIN_PORT)
				pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0x92c, 0x3, 0x2);
			else
				pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0x92c, 0x3, 0x1);
			break;
		default:
		case BTC_ANT_PATH_PTA:
			if (pBoardInfo->btdmAntPos == BTC_ANTENNA_AT_MAIN_PORT)
				pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0x92c, 0x3, 0x1);
			else
				pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0x92c, 0x3, 0x2);
			break;
		}

	} else {
		if (bInitHwCfg) {
			/*  0x4c[23]= 1, 0x4c[24]= 0  Antenna control by 0x64 */
			u4Tmp = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x4c);
			u4Tmp |= BIT23;
			u4Tmp &= ~BIT24;
			pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x4c, u4Tmp);

			/* Fix Ext switch Main->S1, Aux->S0 */
			pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0x64, 0x1, 0x0);

			if (pBoardInfo->btdmAntPos == BTC_ANTENNA_AT_MAIN_PORT) {

				/* tell firmware "no antenna inverse" */
				H2C_Parameter[0] = 0;
				H2C_Parameter[1] = 0;  /* internal switch type */
				pBtCoexist->fBtcFillH2c(pBtCoexist, 0x65, 2, H2C_Parameter);
			} else {

				/* tell firmware "antenna inverse" */
				H2C_Parameter[0] = 1;
				H2C_Parameter[1] = 0;  /* internal switch type */
				pBtCoexist->fBtcFillH2c(pBtCoexist, 0x65, 2, H2C_Parameter);
			}
		}


		/*  internal switch setting */
		switch (antPosType) {
		case BTC_ANT_PATH_WIFI:
			if (pBoardInfo->btdmAntPos == BTC_ANTENNA_AT_MAIN_PORT)
				pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x948, 0x0);
			else
				pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x948, 0x280);
			break;
		case BTC_ANT_PATH_BT:
			if (pBoardInfo->btdmAntPos == BTC_ANTENNA_AT_MAIN_PORT)
				pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x948, 0x280);
			else
				pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x948, 0x0);
			break;
		default:
		case BTC_ANT_PATH_PTA:
			if (pBoardInfo->btdmAntPos == BTC_ANTENNA_AT_MAIN_PORT)
				pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x948, 0x200);
			else
				pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x948, 0x80);
			break;
		}
	}
}

static void halbtc8723b1ant_SetFwPstdma(
	PBTC_COEXIST pBtCoexist, u8 byte1, u8 byte2, u8 byte3, u8 byte4, u8 byte5
)
{
	u8 H2C_Parameter[5] = {0};
	u8 realByte1 = byte1, realByte5 = byte5;
	bool bApEnable = false;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_AP_MODE_ENABLE, &bApEnable);

	if (bApEnable) {
		if (byte1&BIT4 && !(byte1&BIT5)) {
			BTC_PRINT(
				BTC_MSG_INTERFACE,
				INTF_NOTIFY,
				("[BTCoex], FW for 1Ant AP mode\n")
			);
			realByte1 &= ~BIT4;
			realByte1 |= BIT5;

			realByte5 |= BIT5;
			realByte5 &= ~BIT6;
		}
	}

	H2C_Parameter[0] = realByte1;
	H2C_Parameter[1] = byte2;
	H2C_Parameter[2] = byte3;
	H2C_Parameter[3] = byte4;
	H2C_Parameter[4] = realByte5;

	pCoexDm->psTdmaPara[0] = realByte1;
	pCoexDm->psTdmaPara[1] = byte2;
	pCoexDm->psTdmaPara[2] = byte3;
	pCoexDm->psTdmaPara[3] = byte4;
	pCoexDm->psTdmaPara[4] = realByte5;

	BTC_PRINT(
		BTC_MSG_ALGORITHM,
		ALGO_TRACE_FW_EXEC,
		(
			"[BTCoex], PS-TDMA H2C cmd = 0x%x%08x\n",
			H2C_Parameter[0],
			H2C_Parameter[1]<<24|
			H2C_Parameter[2]<<16|
			H2C_Parameter[3]<<8|
			H2C_Parameter[4]
		)
	);

	pBtCoexist->fBtcFillH2c(pBtCoexist, 0x60, 5, H2C_Parameter);
}


static void halbtc8723b1ant_PsTdma(
	PBTC_COEXIST pBtCoexist, bool bForceExec, bool bTurnOn, u8 type
)
{
	PBTC_BT_LINK_INFO pBtLinkInfo = &pBtCoexist->btLinkInfo;
	bool bWifiBusy = false;
	u8 rssiAdjustVal = 0;
	u8 psTdmaByte4Val = 0x50, psTdmaByte0Val = 0x51, psTdmaByte3Val =  0x10;
	s8 nWiFiDurationAdjust = 0x0;
	/* u32 		fwVer = 0; */

	/* BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW, ("[BTCoex], %s turn %s PS TDMA, type =%d\n", */
	/* 	(bForceExec? "force to":""), (bTurnOn? "ON":"OFF"), type)); */
	pCoexDm->bCurPsTdmaOn = bTurnOn;
	pCoexDm->curPsTdma = type;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_BUSY, &bWifiBusy);

	if (pCoexDm->bCurPsTdmaOn) {
		BTC_PRINT(
			BTC_MSG_ALGORITHM,
			ALGO_TRACE,
			(
				"[BTCoex], ********** TDMA(on, %d) **********\n",
				pCoexDm->curPsTdma
			)
		);
	} else {
		BTC_PRINT(
			BTC_MSG_ALGORITHM,
			ALGO_TRACE,
			(
				"[BTCoex], ********** TDMA(off, %d) **********\n",
				pCoexDm->curPsTdma
			)
		);
	}

	if (!bForceExec) {
		if (
			(pCoexDm->bPrePsTdmaOn == pCoexDm->bCurPsTdmaOn) &&
			(pCoexDm->prePsTdma == pCoexDm->curPsTdma)
		)
			return;
	}

	if (pCoexSta->nScanAPNum <= 5)
		nWiFiDurationAdjust = 5;
	else if  (pCoexSta->nScanAPNum >= 40)
		nWiFiDurationAdjust = -15;
	else if  (pCoexSta->nScanAPNum >= 20)
		nWiFiDurationAdjust = -10;

	if (!pCoexSta->bForceLpsOn) { /* only for A2DP-only case 1/2/9/11 */
		psTdmaByte0Val = 0x61;  /* no null-pkt */
		psTdmaByte3Val = 0x11; /*  no tx-pause at BT-slot */
		psTdmaByte4Val = 0x10; /*  0x778 = d/1 toggle */
	}


	if (bTurnOn) {
		if (pBtLinkInfo->bSlaveRole)
			psTdmaByte4Val = psTdmaByte4Val | 0x1;  /* 0x778 = 0x1 at wifi slot (no blocking BT Low-Pri pkts) */


		switch (type) {
		default:
			halbtc8723b1ant_SetFwPstdma(
				pBtCoexist, 0x51, 0x1a, 0x1a, 0x0, psTdmaByte4Val
			);
			break;
		case 1:
			halbtc8723b1ant_SetFwPstdma(
				pBtCoexist,
				psTdmaByte0Val,
				0x3a+nWiFiDurationAdjust,
				0x03,
				psTdmaByte3Val,
				psTdmaByte4Val
			);
			break;
		case 2:
			halbtc8723b1ant_SetFwPstdma(
				pBtCoexist,
				psTdmaByte0Val,
				0x2d+nWiFiDurationAdjust,
				0x03,
				psTdmaByte3Val,
				psTdmaByte4Val
			);
			break;
		case 3:
			halbtc8723b1ant_SetFwPstdma(
				pBtCoexist, 0x51, 0x1d, 0x1d, 0x0, 0x10
			);
			break;
		case 4:
			halbtc8723b1ant_SetFwPstdma(
				pBtCoexist, 0x93, 0x15, 0x3, 0x14, 0x0
			);
			break;
		case 5:
			halbtc8723b1ant_SetFwPstdma(
				pBtCoexist, 0x61, 0x15, 0x3, 0x11, 0x10
			);
			break;
		case 6:
			halbtc8723b1ant_SetFwPstdma(
				pBtCoexist, 0x61, 0x20, 0x3, 0x11, 0x11
			);
			break;
		case 7:
			halbtc8723b1ant_SetFwPstdma(pBtCoexist, 0x13, 0xc, 0x5, 0x0, 0x0);
			break;
		case 8:
			halbtc8723b1ant_SetFwPstdma(
				pBtCoexist, 0x93, 0x25, 0x3, 0x10, 0x0
			);
			break;
		case 9:
			halbtc8723b1ant_SetFwPstdma(
				pBtCoexist,
				psTdmaByte0Val,
				0x21,
				0x3,
				psTdmaByte3Val,
				psTdmaByte4Val
			);
			break;
		case 10:
			halbtc8723b1ant_SetFwPstdma(pBtCoexist, 0x13, 0xa, 0xa, 0x0, 0x40);
			break;
		case 11:
			halbtc8723b1ant_SetFwPstdma(
				pBtCoexist,
				psTdmaByte0Val,
				0x21,
				0x03,
				psTdmaByte3Val,
				psTdmaByte4Val
			);
			break;
		case 12:
			halbtc8723b1ant_SetFwPstdma(
				pBtCoexist, 0x51, 0x0a, 0x0a, 0x0, 0x50
			);
			break;
		case 13:
			halbtc8723b1ant_SetFwPstdma(
				pBtCoexist, 0x51, 0x12, 0x12, 0x0, 0x10
			);
			break;
		case 14:
			halbtc8723b1ant_SetFwPstdma(
				pBtCoexist, 0x51, 0x21, 0x3, 0x10, psTdmaByte4Val
			);
			break;
		case 15:
			halbtc8723b1ant_SetFwPstdma(
				pBtCoexist, 0x13, 0xa, 0x3, 0x8, 0x0
			);
			break;
		case 16:
			halbtc8723b1ant_SetFwPstdma(
				pBtCoexist, 0x93, 0x15, 0x3, 0x10, 0x0
			);
			break;
		case 18:
			halbtc8723b1ant_SetFwPstdma(
				pBtCoexist, 0x93, 0x25, 0x3, 0x10, 0x0
			);
			break;
		case 20:
			halbtc8723b1ant_SetFwPstdma(
				pBtCoexist, 0x61, 0x3f, 0x03, 0x11, 0x10

			);
			break;
		case 21:
			halbtc8723b1ant_SetFwPstdma(
				pBtCoexist, 0x61, 0x25, 0x03, 0x11, 0x11
			);
			break;
		case 22:
			halbtc8723b1ant_SetFwPstdma(
				pBtCoexist, 0x61, 0x25, 0x03, 0x11, 0x10
			);
			break;
		case 23:
			halbtc8723b1ant_SetFwPstdma(
				pBtCoexist, 0xe3, 0x25, 0x3, 0x31, 0x18
			);
			break;
		case 24:
			halbtc8723b1ant_SetFwPstdma(
				pBtCoexist, 0xe3, 0x15, 0x3, 0x31, 0x18
			);
			break;
		case 25:
			halbtc8723b1ant_SetFwPstdma(
				pBtCoexist, 0xe3, 0xa, 0x3, 0x31, 0x18
			);
			break;
		case 26:
			halbtc8723b1ant_SetFwPstdma(
				pBtCoexist, 0xe3, 0xa, 0x3, 0x31, 0x18
			);
			break;
		case 27:
			halbtc8723b1ant_SetFwPstdma(
				pBtCoexist, 0xe3, 0x25, 0x3, 0x31, 0x98
			);
			break;
		case 28:
			halbtc8723b1ant_SetFwPstdma(
				pBtCoexist, 0x69, 0x25, 0x3, 0x31, 0x0
			);
			break;
		case 29:
			halbtc8723b1ant_SetFwPstdma(
				pBtCoexist, 0xab, 0x1a, 0x1a, 0x1, 0x10
			);
			break;
		case 30:
			halbtc8723b1ant_SetFwPstdma(
				pBtCoexist, 0x51, 0x30, 0x3, 0x10, 0x10
			);
			break;
		case 31:
			halbtc8723b1ant_SetFwPstdma(
				pBtCoexist, 0xd3, 0x1a, 0x1a, 0x0, 0x58
			);
			break;
		case 32:
			halbtc8723b1ant_SetFwPstdma(
				pBtCoexist, 0x61, 0x35, 0x3, 0x11, 0x11
			);
			break;
		case 33:
			halbtc8723b1ant_SetFwPstdma(
				pBtCoexist, 0xa3, 0x25, 0x3, 0x30, 0x90
			);
			break;
		case 34:
			halbtc8723b1ant_SetFwPstdma(
				pBtCoexist, 0x53, 0x1a, 0x1a, 0x0, 0x10
			);
			break;
		case 35:
			halbtc8723b1ant_SetFwPstdma(
				pBtCoexist, 0x63, 0x1a, 0x1a, 0x0, 0x10
			);
			break;
		case 36:
			halbtc8723b1ant_SetFwPstdma(
				pBtCoexist, 0xd3, 0x12, 0x3, 0x14, 0x50
			);
			break;
		case 40: /*  SoftAP only with no sta associated, BT disable , TDMA mode for power saving */
			/* here softap mode screen off will cost 70-80mA for phone */
			halbtc8723b1ant_SetFwPstdma(
				pBtCoexist, 0x23, 0x18, 0x00, 0x10, 0x24
			);
			break;
		}
	} else {

		/*  disable PS tdma */
		switch (type) {
		case 8: /* PTA Control */
			halbtc8723b1ant_SetFwPstdma(pBtCoexist, 0x8, 0x0, 0x0, 0x0, 0x0);
			halbtc8723b1ant_SetAntPath(
				pBtCoexist, BTC_ANT_PATH_PTA, false, false
			);
			break;
		case 0:
		default:  /* Software control, Antenna at BT side */
			halbtc8723b1ant_SetFwPstdma(pBtCoexist, 0x0, 0x0, 0x0, 0x0, 0x0);
			halbtc8723b1ant_SetAntPath(
				pBtCoexist, BTC_ANT_PATH_BT, false, false
			);
			break;
		case 9:   /* Software control, Antenna at WiFi side */
			halbtc8723b1ant_SetFwPstdma(pBtCoexist, 0x0, 0x0, 0x0, 0x0, 0x0);
			halbtc8723b1ant_SetAntPath(
				pBtCoexist, BTC_ANT_PATH_WIFI, false, false
			);
			break;
		}
	}

	rssiAdjustVal = 0;
	pBtCoexist->fBtcSet(
		pBtCoexist, BTC_SET_U1_RSSI_ADJ_VAL_FOR_1ANT_COEX_TYPE, &rssiAdjustVal
	);

	/*  update pre state */
	pCoexDm->bPrePsTdmaOn = pCoexDm->bCurPsTdmaOn;
	pCoexDm->prePsTdma = pCoexDm->curPsTdma;
}

static bool halbtc8723b1ant_IsCommonAction(PBTC_COEXIST pBtCoexist)
{
	bool bCommon = false, bWifiConnected = false, bWifiBusy = false;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_CONNECTED, &bWifiConnected);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_BUSY, &bWifiBusy);

	if (
		!bWifiConnected &&
		BT_8723B_1ANT_BT_STATUS_NON_CONNECTED_IDLE == pCoexDm->btStatus
	) {
		BTC_PRINT(
			BTC_MSG_ALGORITHM,
			ALGO_TRACE,
			("[BTCoex], Wifi non connected-idle + BT non connected-idle!!\n")
		);

		/* halbtc8723b1ant_SwMechanism(pBtCoexist, false); */

		bCommon = true;
	} else if (
		bWifiConnected &&
		(BT_8723B_1ANT_BT_STATUS_NON_CONNECTED_IDLE == pCoexDm->btStatus)
	) {
		BTC_PRINT(
			BTC_MSG_ALGORITHM,
			ALGO_TRACE,
			("[BTCoex], Wifi connected + BT non connected-idle!!\n")
		);

		/* halbtc8723b1ant_SwMechanism(pBtCoexist, false); */

		bCommon = true;
	} else if (
		!bWifiConnected &&
		(BT_8723B_1ANT_BT_STATUS_CONNECTED_IDLE == pCoexDm->btStatus)
	) {
		BTC_PRINT(
			BTC_MSG_ALGORITHM,
			ALGO_TRACE,
			("[BTCoex], Wifi non connected-idle + BT connected-idle!!\n")
		);

		/* halbtc8723b1ant_SwMechanism(pBtCoexist, false); */

		bCommon = true;
	} else if (
		bWifiConnected &&
		(BT_8723B_1ANT_BT_STATUS_CONNECTED_IDLE == pCoexDm->btStatus)
	) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Wifi connected + BT connected-idle!!\n"));

		/* halbtc8723b1ant_SwMechanism(pBtCoexist, false); */

		bCommon = true;
	} else if (
		!bWifiConnected &&
		(BT_8723B_1ANT_BT_STATUS_CONNECTED_IDLE != pCoexDm->btStatus)
	) {
		BTC_PRINT(
			BTC_MSG_ALGORITHM,
			ALGO_TRACE,
			("[BTCoex], Wifi non connected-idle + BT Busy!!\n")
		);

		/* halbtc8723b1ant_SwMechanism(pBtCoexist, false); */

		bCommon = true;
	} else {
		if (bWifiBusy) {
			BTC_PRINT(
				BTC_MSG_ALGORITHM,
				ALGO_TRACE,
				("[BTCoex], Wifi Connected-Busy + BT Busy!!\n")
			);
		} else {
			BTC_PRINT(
				BTC_MSG_ALGORITHM,
				ALGO_TRACE,
				("[BTCoex], Wifi Connected-Idle + BT Busy!!\n")
			);
		}

		bCommon = false;
	}

	return bCommon;
}


static void halbtc8723b1ant_TdmaDurationAdjustForAcl(
	PBTC_COEXIST pBtCoexist, u8 wifiStatus
)
{
	static s32 up, dn, m, n, WaitCount;
	s32 result;   /* 0: no change, +1: increase WiFi duration, -1: decrease WiFi duration */
	u8 retryCount = 0, btInfoExt;
	bool bWifiBusy = false;

	BTC_PRINT(
		BTC_MSG_ALGORITHM,
		ALGO_TRACE_FW,
		("[BTCoex], TdmaDurationAdjustForAcl()\n")
	);

	if (BT_8723B_1ANT_WIFI_STATUS_CONNECTED_BUSY == wifiStatus)
		bWifiBusy = true;
	else
		bWifiBusy = false;

	if (
		(BT_8723B_1ANT_WIFI_STATUS_NON_CONNECTED_ASSO_AUTH_SCAN == wifiStatus) ||
		(BT_8723B_1ANT_WIFI_STATUS_CONNECTED_SCAN == wifiStatus) ||
		(BT_8723B_1ANT_WIFI_STATUS_CONNECTED_SPECIAL_PKT == wifiStatus)
	) {
		if (
			pCoexDm->curPsTdma != 1 &&
			pCoexDm->curPsTdma != 2 &&
			pCoexDm->curPsTdma != 3 &&
			pCoexDm->curPsTdma != 9
		) {
			halbtc8723b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, true, 9);
			pCoexDm->psTdmaDuAdjType = 9;

			up = 0;
			dn = 0;
			m = 1;
			n = 3;
			result = 0;
			WaitCount = 0;
		}
		return;
	}

	if (!pCoexDm->bAutoTdmaAdjust) {
		pCoexDm->bAutoTdmaAdjust = true;
		BTC_PRINT(
			BTC_MSG_ALGORITHM,
			ALGO_TRACE_FW_DETAIL,
			("[BTCoex], first run TdmaDurationAdjust()!!\n")
		);

		halbtc8723b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, true, 2);
		pCoexDm->psTdmaDuAdjType = 2;
		/*  */
		up = 0;
		dn = 0;
		m = 1;
		n = 3;
		result = 0;
		WaitCount = 0;
	} else {
		/* accquire the BT TRx retry count from BT_Info byte2 */
		retryCount = pCoexSta->btRetryCnt;
		btInfoExt = pCoexSta->btInfoExt;
		/* BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], retryCount = %d\n", retryCount)); */
		/* BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], up =%d, dn =%d, m =%d, n =%d, WaitCount =%d\n", */
		/* 	up, dn, m, n, WaitCount)); */

		if (pCoexSta->lowPriorityTx > 1050 || pCoexSta->lowPriorityRx > 1250)
			retryCount++;

		result = 0;
		WaitCount++;

		if (retryCount == 0) { /*  no retry in the last 2-second duration */
			up++;
			dn--;

			if (dn <= 0)
				dn = 0;

			if (up >= n) { /*  if 連續 n 個2秒 retry count為0, 則調寬WiFi duration */
				WaitCount = 0;
				n = 3;
				up = 0;
				dn = 0;
				result = 1;
				BTC_PRINT(
					BTC_MSG_ALGORITHM,
					ALGO_TRACE_FW_DETAIL,
					("[BTCoex], Increase wifi duration!!\n")
				);
			}
		} else if (retryCount <= 3) { /*  <=3 retry in the last 2-second duration */
			up--;
			dn++;

			if (up <= 0)
				up = 0;

			if (dn == 2) { /*  if 連續 2 個2秒 retry count< 3, 則調窄WiFi duration */
				if (WaitCount <= 2)
					m++; /*  避免一直在兩個level中來回 */
				else
					m = 1;

				if (m >= 20) /* m 最大值 = 20 ' 最大120秒 recheck是否調整 WiFi duration. */
					m = 20;

				n = 3*m;
				up = 0;
				dn = 0;
				WaitCount = 0;
				result = -1;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], Decrease wifi duration for retryCounter<3!!\n"));
			}
		} else { /* retry count > 3, 只要1次 retry count > 3, 則調窄WiFi duration */
			if (WaitCount == 1)
				m++; /*  避免一直在兩個level中來回 */
			else
				m = 1;

			if (m >= 20) /* m 最大值 = 20 ' 最大120秒 recheck是否調整 WiFi duration. */
				m = 20;

			n = 3*m;
			up = 0;
			dn = 0;
			WaitCount = 0;
			result = -1;
			BTC_PRINT(
				BTC_MSG_ALGORITHM,
				ALGO_TRACE_FW_DETAIL,
				("[BTCoex], Decrease wifi duration for retryCounter>3!!\n")
			);
		}

		if (result == -1) {
			if (
				BT_INFO_8723B_1ANT_A2DP_BASIC_RATE(btInfoExt) &&
				((pCoexDm->curPsTdma == 1) || (pCoexDm->curPsTdma == 2))
			) {
				halbtc8723b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, true, 9);
				pCoexDm->psTdmaDuAdjType = 9;
			} else if (pCoexDm->curPsTdma == 1) {
				halbtc8723b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, true, 2);
				pCoexDm->psTdmaDuAdjType = 2;
			} else if (pCoexDm->curPsTdma == 2) {
				halbtc8723b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, true, 9);
				pCoexDm->psTdmaDuAdjType = 9;
			} else if (pCoexDm->curPsTdma == 9) {
				halbtc8723b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, true, 11);
				pCoexDm->psTdmaDuAdjType = 11;
			}
		} else if (result == 1) {
			if (
				BT_INFO_8723B_1ANT_A2DP_BASIC_RATE(btInfoExt) &&
				((pCoexDm->curPsTdma == 1) || (pCoexDm->curPsTdma == 2))
			) {
				halbtc8723b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, true, 9);
				pCoexDm->psTdmaDuAdjType = 9;
			} else if (pCoexDm->curPsTdma == 11) {
				halbtc8723b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, true, 9);
				pCoexDm->psTdmaDuAdjType = 9;
			} else if (pCoexDm->curPsTdma == 9) {
				halbtc8723b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, true, 2);
				pCoexDm->psTdmaDuAdjType = 2;
			} else if (pCoexDm->curPsTdma == 2) {
				halbtc8723b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, true, 1);
				pCoexDm->psTdmaDuAdjType = 1;
			}
		} else {	  /* no change */
			BTC_PRINT(
				BTC_MSG_ALGORITHM,
				ALGO_TRACE_FW_DETAIL,
				(
					"[BTCoex], ********** TDMA(on, %d) **********\n",
					pCoexDm->curPsTdma
				)
			);
		}

		if (
			pCoexDm->curPsTdma != 1 &&
			pCoexDm->curPsTdma != 2 &&
			pCoexDm->curPsTdma != 9 &&
			pCoexDm->curPsTdma != 11
		) /*  recover to previous adjust type */
			halbtc8723b1ant_PsTdma(
				pBtCoexist, NORMAL_EXEC, true, pCoexDm->psTdmaDuAdjType
			);
	}
}

static void halbtc8723b1ant_PsTdmaCheckForPowerSaveState(
	PBTC_COEXIST pBtCoexist, bool bNewPsState
)
{
	u8 lpsMode = 0x0;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U1_LPS_MODE, &lpsMode);

	if (lpsMode) {	/*  already under LPS state */
		if (bNewPsState) {
			/*  keep state under LPS, do nothing. */
		} else /*  will leave LPS state, turn off psTdma first */
			halbtc8723b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, false, 0);
	} else {						/*  NO PS state */
		if (bNewPsState) /*  will enter LPS state, turn off psTdma first */
			halbtc8723b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, false, 0);
		else {
			/*  keep state under NO PS state, do nothing. */
		}
	}
}

static void halbtc8723b1ant_PowerSaveState(
	PBTC_COEXIST pBtCoexist, u8 psType, u8 lpsVal, u8 rpwmVal
)
{
	bool bLowPwrDisable = false;

	switch (psType) {
	case BTC_PS_WIFI_NATIVE:
		/*  recover to original 32k low power setting */
		bLowPwrDisable = false;
		pBtCoexist->fBtcSet(
			pBtCoexist, BTC_SET_ACT_DISABLE_LOW_POWER, &bLowPwrDisable
		);
		pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_ACT_NORMAL_LPS, NULL);
		pCoexSta->bForceLpsOn = false;
		break;
	case BTC_PS_LPS_ON:
		halbtc8723b1ant_PsTdmaCheckForPowerSaveState(pBtCoexist, true);
		halbtc8723b1ant_LpsRpwm(pBtCoexist, NORMAL_EXEC, lpsVal, rpwmVal);
		/*  when coex force to enter LPS, do not enter 32k low power. */
		bLowPwrDisable = true;
		pBtCoexist->fBtcSet(
			pBtCoexist, BTC_SET_ACT_DISABLE_LOW_POWER, &bLowPwrDisable
		);
		/*  power save must executed before psTdma. */
		pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_ACT_ENTER_LPS, NULL);
		pCoexSta->bForceLpsOn = true;
		break;
	case BTC_PS_LPS_OFF:
		halbtc8723b1ant_PsTdmaCheckForPowerSaveState(pBtCoexist, false);
		pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_ACT_LEAVE_LPS, NULL);
		pCoexSta->bForceLpsOn = false;
		break;
	default:
		break;
	}
}

/*  */
/*  */
/* 	Software Coex Mechanism start */
/*  */
/*  */

/*  */
/*  */
/* 	Non-Software Coex Mechanism start */
/*  */
/*  */
static void halbtc8723b1ant_ActionWifiMultiPort(PBTC_COEXIST pBtCoexist)
{
	halbtc8723b1ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);

	halbtc8723b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, false, 8);
	halbtc8723b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 2);
}

static void halbtc8723b1ant_ActionHs(PBTC_COEXIST pBtCoexist)
{
	halbtc8723b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, true, 5);
	halbtc8723b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 2);
}

static void halbtc8723b1ant_ActionBtInquiry(PBTC_COEXIST pBtCoexist)
{
	PBTC_BT_LINK_INFO pBtLinkInfo = &pBtCoexist->btLinkInfo;
	bool bWifiConnected = false;
	bool bApEnable = false;
	bool bWifiBusy = false;
	bool bBtBusy = false;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_AP_MODE_ENABLE, &bApEnable);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_CONNECTED, &bWifiConnected);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_BUSY, &bWifiBusy);
	pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_BL_BT_TRAFFIC_BUSY, &bBtBusy);

	if (!bWifiConnected && !pCoexSta->bWiFiIsHighPriTask) {
		halbtc8723b1ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);
		halbtc8723b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, false, 8);

		halbtc8723b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 0);
	} else if (
		pBtLinkInfo->bScoExist ||
		pBtLinkInfo->bHidExist ||
		pBtLinkInfo->bA2dpExist
	) {
		/*  SCO/HID/A2DP busy */
		halbtc8723b1ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);
		halbtc8723b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, true, 32);

		halbtc8723b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 4);
	} else if (pBtLinkInfo->bPanExist || bWifiBusy) {
		halbtc8723b1ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);
		halbtc8723b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, true, 20);

		halbtc8723b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 4);
	} else {
		halbtc8723b1ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);
		halbtc8723b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, false, 8);

		halbtc8723b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 7);
	}
}

static void halbtc8723b1ant_ActionBtScoHidOnlyBusy(
	PBTC_COEXIST pBtCoexist, u8 wifiStatus
)
{
	PBTC_BT_LINK_INFO pBtLinkInfo = &pBtCoexist->btLinkInfo;
	bool bWifiConnected = false;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_CONNECTED, &bWifiConnected);

	/*  tdma and coex table */

	if (pBtLinkInfo->bScoExist) {
		halbtc8723b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, true, 5);
		halbtc8723b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 5);
	} else { /* HID */
		halbtc8723b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, true, 6);
		halbtc8723b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 5);
	}
}

static void halbtc8723b1ant_ActionWifiConnectedBtAclBusy(
	PBTC_COEXIST pBtCoexist, u8 wifiStatus
)
{
	u8 btRssiState;

	PBTC_BT_LINK_INFO pBtLinkInfo = &pBtCoexist->btLinkInfo;
	btRssiState = halbtc8723b1ant_BtRssiState(2, 28, 0);

	if ((pCoexSta->lowPriorityRx >= 1000) && (pCoexSta->lowPriorityRx != 65535))
		pBtLinkInfo->bSlaveRole = true;
	else
		pBtLinkInfo->bSlaveRole = false;

	if (pBtLinkInfo->bHidOnly) { /* HID */
		halbtc8723b1ant_ActionBtScoHidOnlyBusy(pBtCoexist, wifiStatus);
		pCoexDm->bAutoTdmaAdjust = false;
		return;
	} else if (pBtLinkInfo->bA2dpOnly) { /* A2DP */
		if (BT_8723B_1ANT_WIFI_STATUS_CONNECTED_IDLE == wifiStatus) {
			halbtc8723b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, true, 32);
			halbtc8723b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 4);
			pCoexDm->bAutoTdmaAdjust = false;
		} else {
			halbtc8723b1ant_TdmaDurationAdjustForAcl(pBtCoexist, wifiStatus);
			halbtc8723b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 4);
			pCoexDm->bAutoTdmaAdjust = true;
		}
	} else if (pBtLinkInfo->bHidExist && pBtLinkInfo->bA2dpExist) { /* HID+A2DP */
		halbtc8723b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, true, 14);
		pCoexDm->bAutoTdmaAdjust = false;

		halbtc8723b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 4);
	} else if (
		pBtLinkInfo->bPanOnly ||
		(pBtLinkInfo->bHidExist && pBtLinkInfo->bPanExist)
	) { /* PAN(OPP, FTP), HID+PAN(OPP, FTP) */
		halbtc8723b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, true, 3);
		halbtc8723b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 4);
		pCoexDm->bAutoTdmaAdjust = false;
	} else if (
		(pBtLinkInfo->bA2dpExist && pBtLinkInfo->bPanExist) ||
		(pBtLinkInfo->bHidExist && pBtLinkInfo->bA2dpExist && pBtLinkInfo->bPanExist)
	) { /* A2DP+PAN(OPP, FTP), HID+A2DP+PAN(OPP, FTP) */
		halbtc8723b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, true, 13);
		halbtc8723b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 4);
		pCoexDm->bAutoTdmaAdjust = false;
	} else {
		/* BT no-profile busy (0x9) */
		halbtc8723b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, true, 32);
		halbtc8723b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 4);
		pCoexDm->bAutoTdmaAdjust = false;
	}
}

static void halbtc8723b1ant_ActionWifiNotConnected(PBTC_COEXIST pBtCoexist)
{
	/*  power save state */
	halbtc8723b1ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);

	/*  tdma and coex table */
	halbtc8723b1ant_PsTdma(pBtCoexist, FORCE_EXEC, false, 8);
	halbtc8723b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 0);
}

static void halbtc8723b1ant_ActionWifiNotConnectedScan(
	PBTC_COEXIST pBtCoexist
)
{
	PBTC_BT_LINK_INFO pBtLinkInfo = &pBtCoexist->btLinkInfo;

	halbtc8723b1ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);

	/*  tdma and coex table */
	if (BT_8723B_1ANT_BT_STATUS_ACL_BUSY == pCoexDm->btStatus) {
		if (pBtLinkInfo->bA2dpExist) {
			halbtc8723b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, true, 32);
			halbtc8723b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 4);
		} else if (pBtLinkInfo->bA2dpExist && pBtLinkInfo->bPanExist) {
			halbtc8723b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, true, 22);
			halbtc8723b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 4);
		} else {
			halbtc8723b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, true, 20);
			halbtc8723b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 4);
		}
	} else if (
		(BT_8723B_1ANT_BT_STATUS_SCO_BUSY == pCoexDm->btStatus) ||
		(BT_8723B_1ANT_BT_STATUS_ACL_SCO_BUSY == pCoexDm->btStatus)
	) {
		halbtc8723b1ant_ActionBtScoHidOnlyBusy(
			pBtCoexist, BT_8723B_1ANT_WIFI_STATUS_CONNECTED_SCAN
		);
	} else {
		/* Bryant Add */
		halbtc8723b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, false, 8);
		halbtc8723b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 2);
	}
}

static void halbtc8723b1ant_ActionWifiNotConnectedAssoAuth(
	PBTC_COEXIST pBtCoexist
)
{
	PBTC_BT_LINK_INFO pBtLinkInfo = &pBtCoexist->btLinkInfo;

	halbtc8723b1ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);

	/*  tdma and coex table */
	if (
		(pBtLinkInfo->bScoExist) ||
		(pBtLinkInfo->bHidExist) ||
		(pBtLinkInfo->bA2dpExist)
	) {
		halbtc8723b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, true, 32);
		halbtc8723b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 4);
	} else if (pBtLinkInfo->bPanExist) {
		halbtc8723b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, true, 20);
		halbtc8723b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 4);
	} else {
		halbtc8723b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, false, 8);
		halbtc8723b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 2);
	}
}

static void halbtc8723b1ant_ActionWifiConnectedScan(PBTC_COEXIST pBtCoexist)
{
	PBTC_BT_LINK_INFO pBtLinkInfo = &pBtCoexist->btLinkInfo;

	halbtc8723b1ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);

	/*  tdma and coex table */
	if (BT_8723B_1ANT_BT_STATUS_ACL_BUSY == pCoexDm->btStatus) {
		if (pBtLinkInfo->bA2dpExist) {
			halbtc8723b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, true, 32);
			halbtc8723b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 4);
		} else if (pBtLinkInfo->bA2dpExist && pBtLinkInfo->bPanExist) {
			halbtc8723b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, true, 22);
			halbtc8723b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 4);
		} else {
			halbtc8723b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, true, 20);
			halbtc8723b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 4);
		}
	} else if (
		(BT_8723B_1ANT_BT_STATUS_SCO_BUSY == pCoexDm->btStatus) ||
		(BT_8723B_1ANT_BT_STATUS_ACL_SCO_BUSY == pCoexDm->btStatus)
	) {
		halbtc8723b1ant_ActionBtScoHidOnlyBusy(
			pBtCoexist, BT_8723B_1ANT_WIFI_STATUS_CONNECTED_SCAN
		);
	} else {
		/* Bryant Add */
		halbtc8723b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, false, 8);
		halbtc8723b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 2);
	}
}

static void halbtc8723b1ant_ActionWifiConnectedSpecialPacket(
	PBTC_COEXIST pBtCoexist
)
{
	PBTC_BT_LINK_INFO pBtLinkInfo = &pBtCoexist->btLinkInfo;

	halbtc8723b1ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);

	/*  tdma and coex table */
	if (
		(pBtLinkInfo->bScoExist) ||
		(pBtLinkInfo->bHidExist) ||
		(pBtLinkInfo->bA2dpExist)
	) {
		halbtc8723b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, true, 32);
		halbtc8723b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 4);
	} else if (pBtLinkInfo->bPanExist) {
		halbtc8723b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, true, 20);
		halbtc8723b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 4);
	} else {
		halbtc8723b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, false, 8);
		halbtc8723b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 2);
	}
}

static void halbtc8723b1ant_ActionWifiConnected(PBTC_COEXIST pBtCoexist)
{
	bool bWifiBusy = false;
	bool bScan = false, bLink = false, bRoam = false;
	bool bUnder4way = false, bApEnable = false;

	BTC_PRINT(
		BTC_MSG_ALGORITHM,
		ALGO_TRACE,
		("[BTCoex], CoexForWifiConnect() ===>\n")
	);

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_4_WAY_PROGRESS, &bUnder4way);
	if (bUnder4way) {
		halbtc8723b1ant_ActionWifiConnectedSpecialPacket(pBtCoexist);
		BTC_PRINT(
			BTC_MSG_ALGORITHM,
			ALGO_TRACE,
			("[BTCoex], CoexForWifiConnect(), return for wifi is under 4way<===\n")
		);
		return;
	}

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_SCAN, &bScan);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_LINK, &bLink);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_ROAM, &bRoam);
	if (bScan || bLink || bRoam) {
		if (bScan)
			halbtc8723b1ant_ActionWifiConnectedScan(pBtCoexist);
		else
			halbtc8723b1ant_ActionWifiConnectedSpecialPacket(pBtCoexist);
		BTC_PRINT(
			BTC_MSG_ALGORITHM,
			ALGO_TRACE,
			("[BTCoex], CoexForWifiConnect(), return for wifi is under scan<===\n")
		);
		return;
	}

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_AP_MODE_ENABLE, &bApEnable);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_BUSY, &bWifiBusy);

	/*  power save state */
	if (
		!bApEnable &&
		BT_8723B_1ANT_BT_STATUS_ACL_BUSY == pCoexDm->btStatus &&
		!pBtCoexist->btLinkInfo.bHidOnly
	) {
		if (pBtCoexist->btLinkInfo.bA2dpOnly) { /* A2DP */
			if (!bWifiBusy)
				halbtc8723b1ant_PowerSaveState(
					pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0
				);
			else { /* busy */
				if  (pCoexSta->nScanAPNum >= BT_8723B_1ANT_WIFI_NOISY_THRESH)  /* no force LPS, no PS-TDMA, use pure TDMA */
					halbtc8723b1ant_PowerSaveState(
						pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0
					);
				else
					halbtc8723b1ant_PowerSaveState(
						pBtCoexist, BTC_PS_LPS_ON, 0x50, 0x4
					);
			}
		} else if (
			(!pCoexSta->bPanExist) &&
			(!pCoexSta->bA2dpExist) &&
			(!pCoexSta->bHidExist)
		)
			halbtc8723b1ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);
		else
			halbtc8723b1ant_PowerSaveState(pBtCoexist, BTC_PS_LPS_ON, 0x50, 0x4);
	} else
		halbtc8723b1ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);

	/*  tdma and coex table */
	if (!bWifiBusy) {
		if (BT_8723B_1ANT_BT_STATUS_ACL_BUSY == pCoexDm->btStatus) {
			halbtc8723b1ant_ActionWifiConnectedBtAclBusy(
				pBtCoexist,
				BT_8723B_1ANT_WIFI_STATUS_CONNECTED_IDLE
			);
		} else if (
			(BT_8723B_1ANT_BT_STATUS_SCO_BUSY == pCoexDm->btStatus) ||
			(BT_8723B_1ANT_BT_STATUS_ACL_SCO_BUSY == pCoexDm->btStatus)
		) {
			halbtc8723b1ant_ActionBtScoHidOnlyBusy(pBtCoexist,
				BT_8723B_1ANT_WIFI_STATUS_CONNECTED_IDLE);
		} else {
			halbtc8723b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, false, 8);

			if ((pCoexSta->highPriorityTx) + (pCoexSta->highPriorityRx) <= 60)
				halbtc8723b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 2);
			else
				halbtc8723b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 7);
		}
	} else {
		if (BT_8723B_1ANT_BT_STATUS_ACL_BUSY == pCoexDm->btStatus) {
			halbtc8723b1ant_ActionWifiConnectedBtAclBusy(
				pBtCoexist,
				BT_8723B_1ANT_WIFI_STATUS_CONNECTED_BUSY
			);
		} else if (
			(BT_8723B_1ANT_BT_STATUS_SCO_BUSY == pCoexDm->btStatus) ||
			(BT_8723B_1ANT_BT_STATUS_ACL_SCO_BUSY == pCoexDm->btStatus)
		) {
			halbtc8723b1ant_ActionBtScoHidOnlyBusy(
				pBtCoexist,
				BT_8723B_1ANT_WIFI_STATUS_CONNECTED_BUSY
			);
		} else {
			halbtc8723b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, false, 8);

			if ((pCoexSta->highPriorityTx) + (pCoexSta->highPriorityRx) <= 60)
				halbtc8723b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 2);
			else
				halbtc8723b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 7);
		}
	}
}

static void halbtc8723b1ant_RunSwCoexistMechanism(PBTC_COEXIST pBtCoexist)
{
	u8 algorithm = 0;

	algorithm = halbtc8723b1ant_ActionAlgorithm(pBtCoexist);
	pCoexDm->curAlgorithm = algorithm;

	if (halbtc8723b1ant_IsCommonAction(pBtCoexist)) {

	} else {
		switch (pCoexDm->curAlgorithm) {
		case BT_8723B_1ANT_COEX_ALGO_SCO:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action algorithm = SCO.\n"));
			/* halbtc8723b1ant_ActionSco(pBtCoexist); */
			break;
		case BT_8723B_1ANT_COEX_ALGO_HID:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action algorithm = HID.\n"));
			/* halbtc8723b1ant_ActionHid(pBtCoexist); */
			break;
		case BT_8723B_1ANT_COEX_ALGO_A2DP:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action algorithm = A2DP.\n"));
			/* halbtc8723b1ant_ActionA2dp(pBtCoexist); */
			break;
		case BT_8723B_1ANT_COEX_ALGO_A2DP_PANHS:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action algorithm = A2DP+PAN(HS).\n"));
			/* halbtc8723b1ant_ActionA2dpPanHs(pBtCoexist); */
			break;
		case BT_8723B_1ANT_COEX_ALGO_PANEDR:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action algorithm = PAN(EDR).\n"));
			/* halbtc8723b1ant_ActionPanEdr(pBtCoexist); */
			break;
		case BT_8723B_1ANT_COEX_ALGO_PANHS:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action algorithm = HS mode.\n"));
			/* halbtc8723b1ant_ActionPanHs(pBtCoexist); */
			break;
		case BT_8723B_1ANT_COEX_ALGO_PANEDR_A2DP:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action algorithm = PAN+A2DP.\n"));
			/* halbtc8723b1ant_ActionPanEdrA2dp(pBtCoexist); */
			break;
		case BT_8723B_1ANT_COEX_ALGO_PANEDR_HID:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action algorithm = PAN(EDR)+HID.\n"));
			/* halbtc8723b1ant_ActionPanEdrHid(pBtCoexist); */
			break;
		case BT_8723B_1ANT_COEX_ALGO_HID_A2DP_PANEDR:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action algorithm = HID+A2DP+PAN.\n"));
			/* halbtc8723b1ant_ActionHidA2dpPanEdr(pBtCoexist); */
			break;
		case BT_8723B_1ANT_COEX_ALGO_HID_A2DP:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action algorithm = HID+A2DP.\n"));
			/* halbtc8723b1ant_ActionHidA2dp(pBtCoexist); */
			break;
		default:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action algorithm = coexist All Off!!\n"));
			break;
		}
		pCoexDm->preAlgorithm = pCoexDm->curAlgorithm;
	}
}

static void halbtc8723b1ant_RunCoexistMechanism(PBTC_COEXIST pBtCoexist)
{
	PBTC_BT_LINK_INFO pBtLinkInfo = &pBtCoexist->btLinkInfo;
	bool bWifiConnected = false, bBtHsOn = false;
	bool bIncreaseScanDevNum = false;
	bool bBtCtrlAggBufSize = false;
	u8 aggBufSize = 5;
	u32 wifiLinkStatus = 0;
	u32 numOfWifiLink = 0;

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], RunCoexistMechanism() ===>\n"));

	if (pBtCoexist->bManualControl) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], RunCoexistMechanism(), return for Manual CTRL <===\n"));
		return;
	}

	if (pBtCoexist->bStopCoexDm) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], RunCoexistMechanism(), return for Stop Coex DM <===\n"));
		return;
	}

	if (pCoexSta->bUnderIps) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], wifi is under IPS !!!\n"));
		return;
	}

	if (
		(BT_8723B_1ANT_BT_STATUS_ACL_BUSY == pCoexDm->btStatus) ||
		(BT_8723B_1ANT_BT_STATUS_SCO_BUSY == pCoexDm->btStatus) ||
		(BT_8723B_1ANT_BT_STATUS_ACL_SCO_BUSY == pCoexDm->btStatus)
	){
		bIncreaseScanDevNum = true;
	}

	pBtCoexist->fBtcSet(
		pBtCoexist,
		BTC_SET_BL_INC_SCAN_DEV_NUM,
		&bIncreaseScanDevNum
	);
	pBtCoexist->fBtcGet(
		pBtCoexist,
		BTC_GET_BL_WIFI_CONNECTED,
		&bWifiConnected
	);

	pBtCoexist->fBtcGet(
		pBtCoexist,
		BTC_GET_U4_WIFI_LINK_STATUS,
		&wifiLinkStatus
	);
	numOfWifiLink = wifiLinkStatus>>16;

	if ((numOfWifiLink >= 2) || (wifiLinkStatus&WIFI_P2P_GO_CONNECTED)) {
		BTC_PRINT(
			BTC_MSG_INTERFACE,
			INTF_NOTIFY,
			(
				"############# [BTCoex],  Multi-Port numOfWifiLink = %d, wifiLinkStatus = 0x%x\n",
				numOfWifiLink,
				wifiLinkStatus
			)
		);
		halbtc8723b1ant_LimitedTx(pBtCoexist, NORMAL_EXEC, 0, 0, 0, 0);
		halbtc8723b1ant_LimitedRx(pBtCoexist, NORMAL_EXEC, false, bBtCtrlAggBufSize, aggBufSize);

		if ((pBtLinkInfo->bA2dpExist) && (pCoexSta->bC2hBtInquiryPage)) {
			BTC_PRINT(
				BTC_MSG_INTERFACE,
				INTF_NOTIFY,
				("############# [BTCoex],  BT Is Inquirying\n")
			);
			halbtc8723b1ant_ActionBtInquiry(pBtCoexist);
		} else
			halbtc8723b1ant_ActionWifiMultiPort(pBtCoexist);

		return;
	}

	if ((pBtLinkInfo->bBtLinkExist) && (bWifiConnected)) {
		halbtc8723b1ant_LimitedTx(pBtCoexist, NORMAL_EXEC, 1, 1, 0, 1);

		if (pBtLinkInfo->bScoExist)
			halbtc8723b1ant_LimitedRx(pBtCoexist, NORMAL_EXEC, false, true, 0x5);
		else
			halbtc8723b1ant_LimitedRx(pBtCoexist, NORMAL_EXEC, false, true, 0x8);

		halbtc8723b1ant_SwMechanism(pBtCoexist, true);
		halbtc8723b1ant_RunSwCoexistMechanism(pBtCoexist);  /* just print debug message */
	} else {
		halbtc8723b1ant_LimitedTx(pBtCoexist, NORMAL_EXEC, 0, 0, 0, 0);

		halbtc8723b1ant_LimitedRx(pBtCoexist, NORMAL_EXEC, false, false, 0x5);

		halbtc8723b1ant_SwMechanism(pBtCoexist, false);
		halbtc8723b1ant_RunSwCoexistMechanism(pBtCoexist); /* just print debug message */
	}

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_HS_OPERATION, &bBtHsOn);
	if (pCoexSta->bC2hBtInquiryPage) {
		BTC_PRINT(
			BTC_MSG_INTERFACE,
			INTF_NOTIFY,
			("############# [BTCoex],  BT Is Inquirying\n")
		);
		halbtc8723b1ant_ActionBtInquiry(pBtCoexist);
		return;
	} else if (bBtHsOn) {
		halbtc8723b1ant_ActionHs(pBtCoexist);
		return;
	}


	if (!bWifiConnected) {
		bool bScan = false, bLink = false, bRoam = false;

		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], wifi is non connected-idle !!!\n"));

		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_SCAN, &bScan);
		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_LINK, &bLink);
		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_ROAM, &bRoam);

		if (bScan || bLink || bRoam) {
			 if (bScan)
				halbtc8723b1ant_ActionWifiNotConnectedScan(pBtCoexist);
			 else
				halbtc8723b1ant_ActionWifiNotConnectedAssoAuth(pBtCoexist);
		} else
			halbtc8723b1ant_ActionWifiNotConnected(pBtCoexist);
	} else /*  wifi LPS/Busy */
		halbtc8723b1ant_ActionWifiConnected(pBtCoexist);
}

static void halbtc8723b1ant_InitCoexDm(PBTC_COEXIST pBtCoexist)
{
	/*  force to reset coex mechanism */

	/*  sw all off */
	halbtc8723b1ant_SwMechanism(pBtCoexist, false);

	/* halbtc8723b1ant_PsTdma(pBtCoexist, FORCE_EXEC, false, 8); */
	halbtc8723b1ant_CoexTableWithType(pBtCoexist, FORCE_EXEC, 0);

	pCoexSta->popEventCnt = 0;
}

static void halbtc8723b1ant_InitHwConfig(
	PBTC_COEXIST pBtCoexist,
	bool bBackUp,
	bool bWifiOnly
)
{
	u32 u4Tmp = 0;/*  fwVer; */
	u8 u1Tmpa = 0, u1Tmpb = 0;

	BTC_PRINT(
		BTC_MSG_INTERFACE,
		INTF_INIT,
		("[BTCoex], 1Ant Init HW Config!!\n")
	);

	pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0x550, 0x8, 0x1);  /* enable TBTT nterrupt */

	/*  0x790[5:0]= 0x5 */
	pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x790, 0x5);

	/*  Enable counter statistics */
	pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x778, 0x1);
	pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0x40, 0x20, 0x1);

	/* Antenna config */
	if (bWifiOnly) {
		halbtc8723b1ant_SetAntPath(pBtCoexist, BTC_ANT_PATH_WIFI, true, false);
		halbtc8723b1ant_PsTdma(pBtCoexist, FORCE_EXEC, false, 9);
	} else
		halbtc8723b1ant_SetAntPath(pBtCoexist, BTC_ANT_PATH_BT, true, false);

	/*  PTA parameter */
	halbtc8723b1ant_CoexTableWithType(pBtCoexist, FORCE_EXEC, 0);

	u4Tmp = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x948);
	u1Tmpa = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x765);
	u1Tmpb = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x67);

	BTC_PRINT(
		BTC_MSG_INTERFACE,
		INTF_NOTIFY,
		(
			"############# [BTCoex], 0x948 = 0x%x, 0x765 = 0x%x, 0x67 = 0x%x\n",
			u4Tmp,
			u1Tmpa,
			u1Tmpb
		)
	);
}

/*  */
/*  work around function start with wa_halbtc8723b1ant_ */
/*  */
/*  */
/*  extern function start with EXhalbtc8723b1ant_ */
/*  */
void EXhalbtc8723b1ant_PowerOnSetting(PBTC_COEXIST pBtCoexist)
{
	PBTC_BOARD_INFO pBoardInfo = &pBtCoexist->boardInfo;
	u8 u1Tmp = 0x0;
	u16 u2Tmp = 0x0;

	pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x67, 0x20);

	/*  enable BB, REG_SYS_FUNC_EN such that we can write 0x948 correctly. */
	u2Tmp = pBtCoexist->fBtcRead2Byte(pBtCoexist, 0x2);
	pBtCoexist->fBtcWrite2Byte(pBtCoexist, 0x2, u2Tmp|BIT0|BIT1);

	/*  set GRAN_BT = 1 */
	pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x765, 0x18);
	/*  set WLAN_ACT = 0 */
	pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x76e, 0x4);

	/*  */
	/*  S0 or S1 setting and Local register setting(By the setting fw can get ant number, S0/S1, ... info) */
	/*  Local setting bit define */
	/* 	BIT0: "0" for no antenna inverse; "1" for antenna inverse */
	/* 	BIT1: "0" for internal switch; "1" for external switch */
	/* 	BIT2: "0" for one antenna; "1" for two antenna */
	/*  NOTE: here default all internal switch and 1-antenna ==> BIT1 = 0 and BIT2 = 0 */
	if (pBtCoexist->chipInterface == BTC_INTF_USB) {
		/*  fixed at S0 for USB interface */
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x948, 0x0);

		u1Tmp |= 0x1;	/*  antenna inverse */
		pBtCoexist->fBtcWriteLocalReg1Byte(pBtCoexist, 0xfe08, u1Tmp);

		pBoardInfo->btdmAntPos = BTC_ANTENNA_AT_AUX_PORT;
	} else {
		/*  for PCIE and SDIO interface, we check efuse 0xc3[6] */
		if (pBoardInfo->singleAntPath == 0) {
			/*  set to S1 */
			pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x948, 0x280);
			pBoardInfo->btdmAntPos = BTC_ANTENNA_AT_MAIN_PORT;
		} else if (pBoardInfo->singleAntPath == 1) {
			/*  set to S0 */
			pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x948, 0x0);
			u1Tmp |= 0x1;	/*  antenna inverse */
			pBoardInfo->btdmAntPos = BTC_ANTENNA_AT_AUX_PORT;
		}

		if (pBtCoexist->chipInterface == BTC_INTF_PCI)
			pBtCoexist->fBtcWriteLocalReg1Byte(pBtCoexist, 0x384, u1Tmp);
		else if (pBtCoexist->chipInterface == BTC_INTF_SDIO)
			pBtCoexist->fBtcWriteLocalReg1Byte(pBtCoexist, 0x60, u1Tmp);
	}
}

void EXhalbtc8723b1ant_InitHwConfig(PBTC_COEXIST pBtCoexist, bool bWifiOnly)
{
	halbtc8723b1ant_InitHwConfig(pBtCoexist, true, bWifiOnly);
}

void EXhalbtc8723b1ant_InitCoexDm(PBTC_COEXIST pBtCoexist)
{
	BTC_PRINT(
		BTC_MSG_INTERFACE,
		INTF_INIT,
		("[BTCoex], Coex Mechanism Init!!\n")
	);

	pBtCoexist->bStopCoexDm = false;

	halbtc8723b1ant_InitCoexDm(pBtCoexist);

	halbtc8723b1ant_QueryBtInfo(pBtCoexist);
}

void EXhalbtc8723b1ant_DisplayCoexInfo(PBTC_COEXIST pBtCoexist)
{
	PBTC_BOARD_INFO pBoardInfo = &pBtCoexist->boardInfo;
	PBTC_STACK_INFO pStackInfo = &pBtCoexist->stackInfo;
	PBTC_BT_LINK_INFO pBtLinkInfo = &pBtCoexist->btLinkInfo;
	u8 *cliBuf = pBtCoexist->cliBuf;
	u8 u1Tmp[4], i, btInfoExt, psTdmaCase = 0;
	u16 u2Tmp[4];
	u32 u4Tmp[4];
	bool bRoam = false;
	bool bScan = false;
	bool bLink = false;
	bool bWifiUnder5G = false;
	bool bWifiUnderBMode = false;
	bool bBtHsOn = false;
	bool bWifiBusy = false;
	s32 wifiRssi = 0, btHsRssi = 0;
	u32 wifiBw, wifiTrafficDir, faOfdm, faCck, wifiLinkStatus;
	u8 wifiDot11Chnl, wifiHsChnl;
	u32 fwVer = 0, btPatchVer = 0;
	static u8 PopReportIn10s;

	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n ============[BT Coexist info]============"
	);
	CL_PRINTF(cliBuf);

	if (pBtCoexist->bManualControl) {
		CL_SPRINTF(
			cliBuf,
			BT_TMP_BUF_SIZE,
			"\r\n ============[Under Manual Control]============"
		);
		CL_PRINTF(cliBuf);
		CL_SPRINTF(cliBuf,
			BT_TMP_BUF_SIZE,
			"\r\n =========================================="
		);
		CL_PRINTF(cliBuf);
	}
	if (pBtCoexist->bStopCoexDm) {
		CL_SPRINTF(
			cliBuf,
			BT_TMP_BUF_SIZE,
			"\r\n ============[Coex is STOPPED]============"
		);
		CL_PRINTF(cliBuf);
		CL_SPRINTF(
			cliBuf,
			BT_TMP_BUF_SIZE,
			"\r\n =========================================="
		);
		CL_PRINTF(cliBuf);
	}

	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = %d/ %d/ %d", "Ant PG Num/ Ant Mech/ Ant Pos:", \
		pBoardInfo->pgAntNum,
		pBoardInfo->btdmAntNum,
		pBoardInfo->btdmAntPos
	);
	CL_PRINTF(cliBuf);

	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = %s / %d", "BT stack/ hci ext ver", \
		((pStackInfo->bProfileNotified) ? "Yes" : "No"),
		pStackInfo->hciVersion
	);
	CL_PRINTF(cliBuf);

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_BT_PATCH_VER, &btPatchVer);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_FW_VER, &fwVer);
	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = %d_%x/ 0x%x/ 0x%x(%d)", "CoexVer/ FwVer/ PatchVer", \
		GLCoexVerDate8723b1Ant,
		GLCoexVer8723b1Ant,
		fwVer,
		btPatchVer,
		btPatchVer
	);
	CL_PRINTF(cliBuf);

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_HS_OPERATION, &bBtHsOn);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U1_WIFI_DOT11_CHNL, &wifiDot11Chnl);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U1_WIFI_HS_CHNL, &wifiHsChnl);
	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = %d / %d(%d)", "Dot11 channel / HsChnl(HsMode)", \
		wifiDot11Chnl,
		wifiHsChnl,
		bBtHsOn
	);
	CL_PRINTF(cliBuf);

	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = %02x %02x %02x ", "H2C Wifi inform bt chnl Info", \
		pCoexDm->wifiChnlInfo[0],
		pCoexDm->wifiChnlInfo[1],
		pCoexDm->wifiChnlInfo[2]
	);
	CL_PRINTF(cliBuf);

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_S4_WIFI_RSSI, &wifiRssi);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_S4_HS_RSSI, &btHsRssi);
	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = %d/ %d", "Wifi rssi/ HS rssi", \
		wifiRssi-100, btHsRssi-100
	);
	CL_PRINTF(cliBuf);

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_SCAN, &bScan);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_LINK, &bLink);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_ROAM, &bRoam);
	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = %d/ %d/ %d/ %s", "Wifi bLink/ bRoam/ bScan/ bHi-Pri", \
		bLink, bRoam, bScan, ((pCoexSta->bWiFiIsHighPriTask) ? "1" : "0")
	);
	CL_PRINTF(cliBuf);

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_UNDER_5G, &bWifiUnder5G);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_BW, &wifiBw);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_BUSY, &bWifiBusy);
	pBtCoexist->fBtcGet(
		pBtCoexist, BTC_GET_U4_WIFI_TRAFFIC_DIRECTION, &wifiTrafficDir
	);
	pBtCoexist->fBtcGet(
		pBtCoexist, BTC_GET_BL_WIFI_UNDER_B_MODE, &bWifiUnderBMode
	);

	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = %s / %s/ %s/ AP =%d/ %s ", "Wifi status", \
		(bWifiUnder5G ? "5G" : "2.4G"),
		((bWifiUnderBMode) ? "11b" : ((BTC_WIFI_BW_LEGACY == wifiBw) ? "11bg" : (((BTC_WIFI_BW_HT40 == wifiBw) ? "HT40" : "HT20")))),
		((!bWifiBusy) ? "idle" : ((BTC_WIFI_TRAFFIC_TX == wifiTrafficDir) ? "uplink" : "downlink")),
		pCoexSta->nScanAPNum,
		(pCoexSta->bCCKLock) ? "Lock" : "noLock"
	);
	CL_PRINTF(cliBuf);

	pBtCoexist->fBtcGet(
		pBtCoexist, BTC_GET_U4_WIFI_LINK_STATUS, &wifiLinkStatus
	);
	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = %d/ %d/ %d/ %d/ %d", "sta/vwifi/hs/p2pGo/p2pGc", \
		((wifiLinkStatus&WIFI_STA_CONNECTED) ? 1 : 0),
		((wifiLinkStatus&WIFI_AP_CONNECTED) ? 1 : 0),
		((wifiLinkStatus&WIFI_HS_CONNECTED) ? 1 : 0),
		((wifiLinkStatus&WIFI_P2P_GO_CONNECTED) ? 1 : 0),
		((wifiLinkStatus&WIFI_P2P_GC_CONNECTED) ? 1 : 0)
	);
	CL_PRINTF(cliBuf);


	PopReportIn10s++;
	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = [%s/ %d/ %d/ %d] ", "BT [status/ rssi/ retryCnt/ popCnt]", \
		((pBtCoexist->btInfo.bBtDisabled) ? ("disabled") : ((pCoexSta->bC2hBtInquiryPage) ? ("inquiry/page scan") : ((BT_8723B_1ANT_BT_STATUS_NON_CONNECTED_IDLE == pCoexDm->btStatus) ? "non-connected idle" :
		((BT_8723B_1ANT_BT_STATUS_CONNECTED_IDLE == pCoexDm->btStatus) ? "connected-idle" : "busy")))),
		pCoexSta->btRssi, pCoexSta->btRetryCnt, pCoexSta->popEventCnt
	);
	CL_PRINTF(cliBuf);

	if (PopReportIn10s >= 5) {
		pCoexSta->popEventCnt = 0;
		PopReportIn10s = 0;
	}


	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = %d / %d / %d / %d", "SCO/HID/PAN/A2DP", \
		pBtLinkInfo->bScoExist,
		pBtLinkInfo->bHidExist,
		pBtLinkInfo->bPanExist,
		pBtLinkInfo->bA2dpExist
	);
	CL_PRINTF(cliBuf);

	if (pStackInfo->bProfileNotified) {
		pBtCoexist->fBtcDispDbgMsg(pBtCoexist, BTC_DBG_DISP_BT_LINK_INFO);
	} else {
		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s", "BT Role", \
		(pBtLinkInfo->bSlaveRole) ? "Slave" : "Master");
		CL_PRINTF(cliBuf);
	}


	btInfoExt = pCoexSta->btInfoExt;
	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = %s", "BT Info A2DP rate", \
		(btInfoExt&BIT0) ? "Basic rate" : "EDR rate"
	);
	CL_PRINTF(cliBuf);

	for (i = 0; i < BT_INFO_SRC_8723B_1ANT_MAX; i++) {
		if (pCoexSta->btInfoC2hCnt[i]) {
			CL_SPRINTF(
				cliBuf,
				BT_TMP_BUF_SIZE,
				"\r\n %-35s = %02x %02x %02x %02x %02x %02x %02x(%d)", GLBtInfoSrc8723b1Ant[i], \
				pCoexSta->btInfoC2h[i][0], pCoexSta->btInfoC2h[i][1],
				pCoexSta->btInfoC2h[i][2], pCoexSta->btInfoC2h[i][3],
				pCoexSta->btInfoC2h[i][4], pCoexSta->btInfoC2h[i][5],
				pCoexSta->btInfoC2h[i][6], pCoexSta->btInfoC2hCnt[i]
			);
			CL_PRINTF(cliBuf);
		}
	}
	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = %s/%s, (0x%x/0x%x)", "PS state, IPS/LPS, (lps/rpwm)", \
		(pCoexSta->bUnderIps ? "IPS ON" : "IPS OFF"),
		(pCoexSta->bUnderLps ? "LPS ON" : "LPS OFF"),
		pBtCoexist->btInfo.lpsVal,
		pBtCoexist->btInfo.rpwmVal
	);
	CL_PRINTF(cliBuf);
	pBtCoexist->fBtcDispDbgMsg(pBtCoexist, BTC_DBG_DISP_FW_PWR_MODE_CMD);

	if (!pBtCoexist->bManualControl) {
		/*  Sw mechanism */
		CL_SPRINTF(
			cliBuf,
			BT_TMP_BUF_SIZE,
			"\r\n %-35s", "============[Sw mechanism]============"
		);
		CL_PRINTF(cliBuf);

		CL_SPRINTF(
			cliBuf,
			BT_TMP_BUF_SIZE,
			"\r\n %-35s = %d", "SM[LowPenaltyRA]", \
			pCoexDm->bCurLowPenaltyRa
		);
		CL_PRINTF(cliBuf);

		CL_SPRINTF(
			cliBuf,
			BT_TMP_BUF_SIZE,
			"\r\n %-35s = %s/ %s/ %d ", "DelBA/ BtCtrlAgg/ AggSize", \
			(pBtCoexist->btInfo.bRejectAggPkt ? "Yes" : "No"),
			(pBtCoexist->btInfo.bBtCtrlAggBufSize ? "Yes" : "No"),
			pBtCoexist->btInfo.aggBufSize
		);
		CL_PRINTF(cliBuf);
		CL_SPRINTF(
			cliBuf,
			BT_TMP_BUF_SIZE,
			"\r\n %-35s = 0x%x ", "Rate Mask", \
			pBtCoexist->btInfo.raMask
		);
		CL_PRINTF(cliBuf);

		/*  Fw mechanism */
		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "============[Fw mechanism]============");
		CL_PRINTF(cliBuf);

		psTdmaCase = pCoexDm->curPsTdma;
		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %02x %02x %02x %02x %02x case-%d (auto:%d)", "PS TDMA", \
			pCoexDm->psTdmaPara[0], pCoexDm->psTdmaPara[1],
			pCoexDm->psTdmaPara[2], pCoexDm->psTdmaPara[3],
			pCoexDm->psTdmaPara[4], psTdmaCase, pCoexDm->bAutoTdmaAdjust);
		CL_PRINTF(cliBuf);

		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d", "Coex Table Type", \
			pCoexSta->nCoexTableType);
		CL_PRINTF(cliBuf);

		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d", "IgnWlanAct", \
			pCoexDm->bCurIgnoreWlanAct);
		CL_PRINTF(cliBuf);

		/*
		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x ", "Latest error condition(should be 0)", \
			pCoexDm->errorCondition);
		CL_PRINTF(cliBuf);
		*/
	}

	/*  Hw setting */
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "============[Hw setting]============");
	CL_PRINTF(cliBuf);

	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/0x%x/0x%x/0x%x", "backup ARFR1/ARFR2/RL/AMaxTime", \
		pCoexDm->backupArfrCnt1, pCoexDm->backupArfrCnt2, pCoexDm->backupRetryLimit, pCoexDm->backupAmpduMaxTime);
	CL_PRINTF(cliBuf);

	u4Tmp[0] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x430);
	u4Tmp[1] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x434);
	u2Tmp[0] = pBtCoexist->fBtcRead2Byte(pBtCoexist, 0x42a);
	u1Tmp[0] = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x456);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/0x%x/0x%x/0x%x", "0x430/0x434/0x42a/0x456", \
		u4Tmp[0], u4Tmp[1], u2Tmp[0], u1Tmp[0]);
	CL_PRINTF(cliBuf);

	u1Tmp[0] = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x778);
	u4Tmp[0] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x6cc);
	u4Tmp[1] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x880);
	CL_SPRINTF(
		cliBuf, BT_TMP_BUF_SIZE,
		"\r\n %-35s = 0x%x/ 0x%x/ 0x%x", "0x778/0x6cc/0x880[29:25]", \
		u1Tmp[0], u4Tmp[0],  (u4Tmp[1]&0x3e000000) >> 25
	);
	CL_PRINTF(cliBuf);

	u4Tmp[0] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x948);
	u1Tmp[0] = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x67);
	u4Tmp[1] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x764);
	u1Tmp[1] = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x76e);
	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = 0x%x/ 0x%x/ 0x%x/ 0x%x", "0x948/ 0x67[5] / 0x764 / 0x76e", \
		u4Tmp[0], ((u1Tmp[0]&0x20) >> 5), (u4Tmp[1] & 0xffff), u1Tmp[1]
	);
	CL_PRINTF(cliBuf);

	u4Tmp[0] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x92c);
	u4Tmp[1] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x930);
	u4Tmp[2] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x944);
	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = 0x%x/ 0x%x/ 0x%x", "0x92c[1:0]/ 0x930[7:0]/0x944[1:0]", \
		u4Tmp[0]&0x3, u4Tmp[1]&0xff, u4Tmp[2]&0x3
	);
	CL_PRINTF(cliBuf);

	u1Tmp[0] = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x39);
	u1Tmp[1] = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x40);
	u4Tmp[0] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x4c);
	u1Tmp[2] = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x64);
	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = 0x%x/ 0x%x/ 0x%x/ 0x%x", "0x38[11]/0x40/0x4c[24:23]/0x64[0]", \
		((u1Tmp[0] & 0x8)>>3),
		u1Tmp[1],
		((u4Tmp[0]&0x01800000)>>23),
		u1Tmp[2]&0x1
	);
	CL_PRINTF(cliBuf);

	u4Tmp[0] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x550);
	u1Tmp[0] = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x522);
	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = 0x%x/ 0x%x", "0x550(bcn ctrl)/0x522", \
		u4Tmp[0], u1Tmp[0]
	);
	CL_PRINTF(cliBuf);

	u4Tmp[0] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0xc50);
	u1Tmp[0] = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x49c);
	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = 0x%x/ 0x%x", "0xc50(dig)/0x49c(null-drop)", \
		u4Tmp[0]&0xff, u1Tmp[0]
	);
	CL_PRINTF(cliBuf);

	u4Tmp[0] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0xda0);
	u4Tmp[1] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0xda4);
	u4Tmp[2] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0xda8);
	u4Tmp[3] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0xcf0);

	u1Tmp[0] = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0xa5b);
	u1Tmp[1] = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0xa5c);

	faOfdm =
		((u4Tmp[0]&0xffff0000) >> 16) +
		((u4Tmp[1]&0xffff0000) >> 16) +
		(u4Tmp[1] & 0xffff) +  (u4Tmp[2] & 0xffff) + \
		((u4Tmp[3]&0xffff0000) >> 16) + (u4Tmp[3] & 0xffff);
	faCck = (u1Tmp[0] << 8) + u1Tmp[1];

	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = 0x%x/ 0x%x/ 0x%x", "OFDM-CCA/OFDM-FA/CCK-FA", \
		u4Tmp[0]&0xffff, faOfdm, faCck
	);
	CL_PRINTF(cliBuf);


	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = %d/ %d/ %d/ %d", "CRC_OK CCK/11g/11n/11n-Agg", \
		pCoexSta->nCRCOK_CCK,
		pCoexSta->nCRCOK_11g,
		pCoexSta->nCRCOK_11n,
		pCoexSta->nCRCOK_11nAgg
	);
	CL_PRINTF(cliBuf);

	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = %d/ %d/ %d/ %d", "CRC_Err CCK/11g/11n/11n-Agg", \
		pCoexSta->nCRCErr_CCK,
		pCoexSta->nCRCErr_11g,
		pCoexSta->nCRCErr_11n,
		pCoexSta->nCRCErr_11nAgg
	);
	CL_PRINTF(cliBuf);

	u4Tmp[0] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x6c0);
	u4Tmp[1] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x6c4);
	u4Tmp[2] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x6c8);
	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = 0x%x/ 0x%x/ 0x%x", "0x6c0/0x6c4/0x6c8(coexTable)", \
		u4Tmp[0], u4Tmp[1], u4Tmp[2]);
	CL_PRINTF(cliBuf);

	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = %d/ %d", "0x770(high-pri rx/tx)", \
		pCoexSta->highPriorityRx, pCoexSta->highPriorityTx
	);
	CL_PRINTF(cliBuf);
	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = %d/ %d", "0x774(low-pri rx/tx)", \
		pCoexSta->lowPriorityRx, pCoexSta->lowPriorityTx
	);
	CL_PRINTF(cliBuf);

	pBtCoexist->fBtcDispDbgMsg(pBtCoexist, BTC_DBG_DISP_COEX_STATISTICS);
}


void EXhalbtc8723b1ant_IpsNotify(PBTC_COEXIST pBtCoexist, u8 type)
{
	if (pBtCoexist->bManualControl ||	pBtCoexist->bStopCoexDm)
		return;

	if (BTC_IPS_ENTER == type) {
		BTC_PRINT(
			BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], IPS ENTER notify\n")
		);
		pCoexSta->bUnderIps = true;

		halbtc8723b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, false, 0);
		halbtc8723b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 0);
		halbtc8723b1ant_SetAntPath(pBtCoexist, BTC_ANT_PATH_BT, false, true);
	} else if (BTC_IPS_LEAVE == type) {
		BTC_PRINT(
			BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], IPS LEAVE notify\n")
		);
		pCoexSta->bUnderIps = false;

		halbtc8723b1ant_InitHwConfig(pBtCoexist, false, false);
		halbtc8723b1ant_InitCoexDm(pBtCoexist);
		halbtc8723b1ant_QueryBtInfo(pBtCoexist);
	}
}

void EXhalbtc8723b1ant_LpsNotify(PBTC_COEXIST pBtCoexist, u8 type)
{
	if (pBtCoexist->bManualControl || pBtCoexist->bStopCoexDm)
		return;

	if (BTC_LPS_ENABLE == type) {
		BTC_PRINT(
			BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], LPS ENABLE notify\n")
		);
		pCoexSta->bUnderLps = true;
	} else if (BTC_LPS_DISABLE == type) {
		BTC_PRINT(
			BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], LPS DISABLE notify\n")
		);
		pCoexSta->bUnderLps = false;
	}
}

void EXhalbtc8723b1ant_ScanNotify(PBTC_COEXIST pBtCoexist, u8 type)
{
	bool bWifiConnected = false, bBtHsOn = false;
	u32 wifiLinkStatus = 0;
	u32 numOfWifiLink = 0;
	bool bBtCtrlAggBufSize = false;
	u8 aggBufSize = 5;

	u8 u1Tmpa, u1Tmpb;
	u32 u4Tmp;

	if (pBtCoexist->bManualControl || pBtCoexist->bStopCoexDm)
		return;

	if (BTC_SCAN_START == type) {
		pCoexSta->bWiFiIsHighPriTask = true;
		BTC_PRINT(
			BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], SCAN START notify\n")
		);

		halbtc8723b1ant_PsTdma(pBtCoexist, FORCE_EXEC, false, 8);  /* Force antenna setup for no scan result issue */
		u4Tmp = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x948);
		u1Tmpa = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x765);
		u1Tmpb = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x67);


		BTC_PRINT(
			BTC_MSG_INTERFACE,
			INTF_NOTIFY,
			(
				"[BTCoex], 0x948 = 0x%x, 0x765 = 0x%x, 0x67 = 0x%x\n",
				u4Tmp,
				u1Tmpa,
				u1Tmpb
			)
		);
	} else {
		pCoexSta->bWiFiIsHighPriTask = false;
		BTC_PRINT(
			BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], SCAN FINISH notify\n")
		);

		pBtCoexist->fBtcGet(
			pBtCoexist, BTC_GET_U1_AP_NUM, &pCoexSta->nScanAPNum
		);
	}

	if (pBtCoexist->btInfo.bBtDisabled)
		return;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_HS_OPERATION, &bBtHsOn);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_CONNECTED, &bWifiConnected);

	halbtc8723b1ant_QueryBtInfo(pBtCoexist);

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_LINK_STATUS, &wifiLinkStatus);
	numOfWifiLink = wifiLinkStatus>>16;

	if (numOfWifiLink >= 2) {
		halbtc8723b1ant_LimitedTx(pBtCoexist, NORMAL_EXEC, 0, 0, 0, 0);
		halbtc8723b1ant_LimitedRx(
			pBtCoexist, NORMAL_EXEC, false, bBtCtrlAggBufSize, aggBufSize
		);
		halbtc8723b1ant_ActionWifiMultiPort(pBtCoexist);
		return;
	}

	if (pCoexSta->bC2hBtInquiryPage) {
		halbtc8723b1ant_ActionBtInquiry(pBtCoexist);
		return;
	} else if (bBtHsOn) {
		halbtc8723b1ant_ActionHs(pBtCoexist);
		return;
	}

	if (BTC_SCAN_START == type) {
		/* BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], SCAN START notify\n")); */
		if (!bWifiConnected)	/*  non-connected scan */
			halbtc8723b1ant_ActionWifiNotConnectedScan(pBtCoexist);
		else	/*  wifi is connected */
			halbtc8723b1ant_ActionWifiConnectedScan(pBtCoexist);
	} else if (BTC_SCAN_FINISH == type) {
		/* BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], SCAN FINISH notify\n")); */
		if (!bWifiConnected)	/*  non-connected scan */
			halbtc8723b1ant_ActionWifiNotConnected(pBtCoexist);
		else
			halbtc8723b1ant_ActionWifiConnected(pBtCoexist);
	}
}

void EXhalbtc8723b1ant_ConnectNotify(PBTC_COEXIST pBtCoexist, u8 type)
{
	bool bWifiConnected = false, bBtHsOn = false;
	u32 wifiLinkStatus = 0;
	u32 numOfWifiLink = 0;
	bool bBtCtrlAggBufSize = false;
	u8 aggBufSize = 5;

	if (
		pBtCoexist->bManualControl ||
		pBtCoexist->bStopCoexDm ||
		pBtCoexist->btInfo.bBtDisabled
	)
		return;

	if (BTC_ASSOCIATE_START == type) {
		pCoexSta->bWiFiIsHighPriTask = true;
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], CONNECT START notify\n"));
		 pCoexDm->nArpCnt = 0;
	} else {
		pCoexSta->bWiFiIsHighPriTask = false;
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], CONNECT FINISH notify\n"));
		/* pCoexDm->nArpCnt = 0; */
	}

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_LINK_STATUS, &wifiLinkStatus);
	numOfWifiLink = wifiLinkStatus>>16;
	if (numOfWifiLink >= 2) {
		halbtc8723b1ant_LimitedTx(pBtCoexist, NORMAL_EXEC, 0, 0, 0, 0);
		halbtc8723b1ant_LimitedRx(pBtCoexist, NORMAL_EXEC, false, bBtCtrlAggBufSize, aggBufSize);
		halbtc8723b1ant_ActionWifiMultiPort(pBtCoexist);
		return;
	}

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_HS_OPERATION, &bBtHsOn);
	if (pCoexSta->bC2hBtInquiryPage) {
		halbtc8723b1ant_ActionBtInquiry(pBtCoexist);
		return;
	} else if (bBtHsOn) {
		halbtc8723b1ant_ActionHs(pBtCoexist);
		return;
	}

	if (BTC_ASSOCIATE_START == type) {
		/* BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], CONNECT START notify\n")); */
		halbtc8723b1ant_ActionWifiNotConnectedAssoAuth(pBtCoexist);
	} else if (BTC_ASSOCIATE_FINISH == type) {
		/* BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], CONNECT FINISH notify\n")); */

		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_CONNECTED, &bWifiConnected);
		if (!bWifiConnected) /*  non-connected scan */
			halbtc8723b1ant_ActionWifiNotConnected(pBtCoexist);
		else
			halbtc8723b1ant_ActionWifiConnected(pBtCoexist);
	}
}

void EXhalbtc8723b1ant_MediaStatusNotify(PBTC_COEXIST pBtCoexist, u8 type)
{
	u8 H2C_Parameter[3] = {0};
	u32 wifiBw;
	u8 wifiCentralChnl;
	bool bWifiUnderBMode = false;

	if (
		pBtCoexist->bManualControl ||
		pBtCoexist->bStopCoexDm ||
		pBtCoexist->btInfo.bBtDisabled
	)
		return;

	if (BTC_MEDIA_CONNECT == type) {
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], MEDIA connect notify\n"));

		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_UNDER_B_MODE, &bWifiUnderBMode);

		/* Set CCK Tx/Rx high Pri except 11b mode */
		if (bWifiUnderBMode) {
			pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x6cd, 0x00); /* CCK Tx */
			pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x6cf, 0x00); /* CCK Rx */
		} else {
			pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x6cd, 0x10); /* CCK Tx */
			pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x6cf, 0x10); /* CCK Rx */
		}

		pCoexDm->backupArfrCnt1 = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x430);
		pCoexDm->backupArfrCnt2 = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x434);
		pCoexDm->backupRetryLimit = pBtCoexist->fBtcRead2Byte(pBtCoexist, 0x42a);
		pCoexDm->backupAmpduMaxTime = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x456);
	} else {
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], MEDIA disconnect notify\n"));
		pCoexDm->nArpCnt = 0;

		pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x6cd, 0x0); /* CCK Tx */
		pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x6cf, 0x0); /* CCK Rx */
	}

	/*  only 2.4G we need to inform bt the chnl mask */
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U1_WIFI_CENTRAL_CHNL, &wifiCentralChnl);
	if ((BTC_MEDIA_CONNECT == type) && (wifiCentralChnl <= 14)) {
		/* H2C_Parameter[0] = 0x1; */
		H2C_Parameter[0] = 0x0;
		H2C_Parameter[1] = wifiCentralChnl;
		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_BW, &wifiBw);

		if (BTC_WIFI_BW_HT40 == wifiBw)
			H2C_Parameter[2] = 0x30;
		else
			H2C_Parameter[2] = 0x20;
	}

	pCoexDm->wifiChnlInfo[0] = H2C_Parameter[0];
	pCoexDm->wifiChnlInfo[1] = H2C_Parameter[1];
	pCoexDm->wifiChnlInfo[2] = H2C_Parameter[2];

	BTC_PRINT(
		BTC_MSG_ALGORITHM,
		ALGO_TRACE_FW_EXEC,
		(
			"[BTCoex], FW write 0x66 = 0x%x\n",
			H2C_Parameter[0]<<16 | H2C_Parameter[1]<<8 | H2C_Parameter[2]
		)
	);

	pBtCoexist->fBtcFillH2c(pBtCoexist, 0x66, 3, H2C_Parameter);
}

void EXhalbtc8723b1ant_SpecialPacketNotify(PBTC_COEXIST pBtCoexist, u8 type)
{
	bool bBtHsOn = false;
	u32 wifiLinkStatus = 0;
	u32 numOfWifiLink = 0;
	bool bBtCtrlAggBufSize = false;
	u8 aggBufSize = 5;

	if (
		pBtCoexist->bManualControl ||
		pBtCoexist->bStopCoexDm ||
		pBtCoexist->btInfo.bBtDisabled
	)
		return;

	if (
		BTC_PACKET_DHCP == type ||
		BTC_PACKET_EAPOL == type ||
		BTC_PACKET_ARP == type
	) {
		if (BTC_PACKET_ARP == type) {
			BTC_PRINT(
				BTC_MSG_INTERFACE,
				INTF_NOTIFY,
				("[BTCoex], special Packet ARP notify\n")
			);

			pCoexDm->nArpCnt++;
			BTC_PRINT(
				BTC_MSG_INTERFACE,
				INTF_NOTIFY,
				("[BTCoex], ARP Packet Count = %d\n", pCoexDm->nArpCnt)
			);

			if (pCoexDm->nArpCnt >= 10) /*  if APR PKT > 10 after connect, do not go to ActionWifiConnectedSpecialPacket(pBtCoexist) */
				pCoexSta->bWiFiIsHighPriTask = false;
			else
				pCoexSta->bWiFiIsHighPriTask = true;
		} else {
			pCoexSta->bWiFiIsHighPriTask = true;
			BTC_PRINT(
				BTC_MSG_INTERFACE,
				INTF_NOTIFY,
				("[BTCoex], special Packet DHCP or EAPOL notify\n")
			);
		}
	} else {
		pCoexSta->bWiFiIsHighPriTask = false;
		BTC_PRINT(
			BTC_MSG_INTERFACE,
			INTF_NOTIFY,
			("[BTCoex], special Packet [Type = %d] notify\n", type)
		);
	}

	pCoexSta->specialPktPeriodCnt = 0;

	pBtCoexist->fBtcGet(
		pBtCoexist, BTC_GET_U4_WIFI_LINK_STATUS, &wifiLinkStatus
	);
	numOfWifiLink = wifiLinkStatus>>16;

	if (numOfWifiLink >= 2) {
		halbtc8723b1ant_LimitedTx(pBtCoexist, NORMAL_EXEC, 0, 0, 0, 0);
		halbtc8723b1ant_LimitedRx(
			pBtCoexist, NORMAL_EXEC, false, bBtCtrlAggBufSize, aggBufSize
		);
		halbtc8723b1ant_ActionWifiMultiPort(pBtCoexist);
		return;
	}

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_HS_OPERATION, &bBtHsOn);
	if (pCoexSta->bC2hBtInquiryPage) {
		halbtc8723b1ant_ActionBtInquiry(pBtCoexist);
		return;
	} else if (bBtHsOn) {
		halbtc8723b1ant_ActionHs(pBtCoexist);
		return;
	}

	if (
		BTC_PACKET_DHCP == type ||
		BTC_PACKET_EAPOL == type ||
		((BTC_PACKET_ARP == type) && (pCoexSta->bWiFiIsHighPriTask))
	)
		halbtc8723b1ant_ActionWifiConnectedSpecialPacket(pBtCoexist);
}

void EXhalbtc8723b1ant_BtInfoNotify(
	PBTC_COEXIST pBtCoexist, u8 *tmpBuf, u8 length
)
{
	u8 btInfo = 0;
	u8 i, rspSource = 0;
	bool bWifiConnected = false;
	bool bBtBusy = false;

	pCoexSta->bC2hBtInfoReqSent = false;

	rspSource = tmpBuf[0]&0xf;
	if (rspSource >= BT_INFO_SRC_8723B_1ANT_MAX)
		rspSource = BT_INFO_SRC_8723B_1ANT_WIFI_FW;
	pCoexSta->btInfoC2hCnt[rspSource]++;

	BTC_PRINT(
		BTC_MSG_INTERFACE,
		INTF_NOTIFY,
		("[BTCoex], Bt info[%d], length =%d, hex data =[",
		rspSource,
		length)
	);
	for (i = 0; i < length; i++) {
		pCoexSta->btInfoC2h[rspSource][i] = tmpBuf[i];
		if (i == 1)
			btInfo = tmpBuf[i];
		if (i == length-1)
			BTC_PRINT(
				BTC_MSG_INTERFACE, INTF_NOTIFY, ("0x%02x]\n", tmpBuf[i])
			);
		else
			BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("0x%02x, ", tmpBuf[i]));
	}

	if (BT_INFO_SRC_8723B_1ANT_WIFI_FW != rspSource) {
		pCoexSta->btRetryCnt = pCoexSta->btInfoC2h[rspSource][2]&0xf;

		if (pCoexSta->btRetryCnt >= 1)
			pCoexSta->popEventCnt++;

		if (pCoexSta->btInfoC2h[rspSource][2]&0x20)
			pCoexSta->bC2hBtPage = true;
		else
			pCoexSta->bC2hBtPage = false;

		pCoexSta->btRssi = pCoexSta->btInfoC2h[rspSource][3]*2-90;
		/* pCoexSta->btInfoC2h[rspSource][3]*2+10; */

		pCoexSta->btInfoExt = pCoexSta->btInfoC2h[rspSource][4];

		pCoexSta->bBtTxRxMask = (pCoexSta->btInfoC2h[rspSource][2]&0x40);
		pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_BL_BT_TX_RX_MASK, &pCoexSta->bBtTxRxMask);

		if (!pCoexSta->bBtTxRxMask) {
			/* BT into is responded by BT FW and BT RF REG 0x3C != 0x15 => Need to switch BT TRx Mask */
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Switch BT TRx Mask since BT RF REG 0x3C != 0x15\n"));
			pBtCoexist->fBtcSetBtReg(pBtCoexist, BTC_BT_REG_RF, 0x3c, 0x15);
		}

		/*  Here we need to resend some wifi info to BT */
		/*  because bt is reset and loss of the info. */
		if (pCoexSta->btInfoExt & BIT1) {
			BTC_PRINT(
				BTC_MSG_ALGORITHM,
				ALGO_TRACE,
				("[BTCoex], BT ext info bit1 check, send wifi BW&Chnl to BT!!\n")
			);
			pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_CONNECTED, &bWifiConnected);
			if (bWifiConnected)
				EXhalbtc8723b1ant_MediaStatusNotify(pBtCoexist, BTC_MEDIA_CONNECT);
			else
				EXhalbtc8723b1ant_MediaStatusNotify(pBtCoexist, BTC_MEDIA_DISCONNECT);
		}

		if (pCoexSta->btInfoExt & BIT3) {
			if (!pBtCoexist->bManualControl && !pBtCoexist->bStopCoexDm) {
				BTC_PRINT(
					BTC_MSG_ALGORITHM,
					ALGO_TRACE,
					("[BTCoex], BT ext info bit3 check, set BT NOT to ignore Wlan active!!\n")
				);
				halbtc8723b1ant_IgnoreWlanAct(pBtCoexist, FORCE_EXEC, false);
			}
		} else {
			/*  BT already NOT ignore Wlan active, do nothing here. */
		}
	}

	/*  check BIT2 first ==> check if bt is under inquiry or page scan */
	if (btInfo & BT_INFO_8723B_1ANT_B_INQ_PAGE)
		pCoexSta->bC2hBtInquiryPage = true;
	else
		pCoexSta->bC2hBtInquiryPage = false;

	/*  set link exist status */
	if (!(btInfo&BT_INFO_8723B_1ANT_B_CONNECTION)) {
		pCoexSta->bBtLinkExist = false;
		pCoexSta->bPanExist = false;
		pCoexSta->bA2dpExist = false;
		pCoexSta->bHidExist = false;
		pCoexSta->bScoExist = false;
	} else {	/*  connection exists */
		pCoexSta->bBtLinkExist = true;
		if (btInfo & BT_INFO_8723B_1ANT_B_FTP)
			pCoexSta->bPanExist = true;
		else
			pCoexSta->bPanExist = false;

		if (btInfo & BT_INFO_8723B_1ANT_B_A2DP)
			pCoexSta->bA2dpExist = true;
		else
			pCoexSta->bA2dpExist = false;

		if (btInfo & BT_INFO_8723B_1ANT_B_HID)
			pCoexSta->bHidExist = true;
		else
			pCoexSta->bHidExist = false;

		if (btInfo & BT_INFO_8723B_1ANT_B_SCO_ESCO)
			pCoexSta->bScoExist = true;
		else
			pCoexSta->bScoExist = false;
	}

	halbtc8723b1ant_UpdateBtLinkInfo(pBtCoexist);

	btInfo = btInfo & 0x1f;  /* mask profile bit for connect-ilde identification (for CSR case: A2DP idle --> 0x41) */

	if (!(btInfo&BT_INFO_8723B_1ANT_B_CONNECTION)) {
		pCoexDm->btStatus = BT_8723B_1ANT_BT_STATUS_NON_CONNECTED_IDLE;
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BtInfoNotify(), BT Non-Connected idle!!!\n"));
	} else if (btInfo == BT_INFO_8723B_1ANT_B_CONNECTION)	{
		/*  connection exists but no busy */
		pCoexDm->btStatus = BT_8723B_1ANT_BT_STATUS_CONNECTED_IDLE;
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BtInfoNotify(), BT Connected-idle!!!\n"));
	} else if (
		(btInfo&BT_INFO_8723B_1ANT_B_SCO_ESCO) ||
		(btInfo&BT_INFO_8723B_1ANT_B_SCO_BUSY)
	) {
		pCoexDm->btStatus = BT_8723B_1ANT_BT_STATUS_SCO_BUSY;
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BtInfoNotify(), BT SCO busy!!!\n"));
	} else if (btInfo&BT_INFO_8723B_1ANT_B_ACL_BUSY) {
		if (BT_8723B_1ANT_BT_STATUS_ACL_BUSY != pCoexDm->btStatus)
			pCoexDm->bAutoTdmaAdjust = false;

		pCoexDm->btStatus = BT_8723B_1ANT_BT_STATUS_ACL_BUSY;
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BtInfoNotify(), BT ACL busy!!!\n"));
	} else {
		pCoexDm->btStatus = BT_8723B_1ANT_BT_STATUS_MAX;
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BtInfoNotify(), BT Non-Defined state!!!\n"));
	}

	if (
		(BT_8723B_1ANT_BT_STATUS_ACL_BUSY == pCoexDm->btStatus) ||
		(BT_8723B_1ANT_BT_STATUS_SCO_BUSY == pCoexDm->btStatus) ||
		(BT_8723B_1ANT_BT_STATUS_ACL_SCO_BUSY == pCoexDm->btStatus)
	)
		bBtBusy = true;
	else
		bBtBusy = false;
	pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_BL_BT_TRAFFIC_BUSY, &bBtBusy);

	halbtc8723b1ant_RunCoexistMechanism(pBtCoexist);
}

void EXhalbtc8723b1ant_HaltNotify(PBTC_COEXIST pBtCoexist)
{
	BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], Halt notify\n"));

	halbtc8723b1ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);
	halbtc8723b1ant_PsTdma(pBtCoexist, FORCE_EXEC, false, 0);
	halbtc8723b1ant_SetAntPath(pBtCoexist, BTC_ANT_PATH_BT, false, true);

	halbtc8723b1ant_IgnoreWlanAct(pBtCoexist, FORCE_EXEC, true);

	EXhalbtc8723b1ant_MediaStatusNotify(pBtCoexist, BTC_MEDIA_DISCONNECT);

	pBtCoexist->bStopCoexDm = true;
}

void EXhalbtc8723b1ant_PnpNotify(PBTC_COEXIST pBtCoexist, u8 pnpState)
{
	BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], Pnp notify\n"));

	if (BTC_WIFI_PNP_SLEEP == pnpState) {
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], Pnp notify to SLEEP\n"));

		halbtc8723b1ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);
		halbtc8723b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, false, 0);
		halbtc8723b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 2);
		halbtc8723b1ant_SetAntPath(pBtCoexist, BTC_ANT_PATH_BT, false, true);

		pBtCoexist->bStopCoexDm = true;
	} else if (BTC_WIFI_PNP_WAKE_UP == pnpState) {
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], Pnp notify to WAKE UP\n"));
		pBtCoexist->bStopCoexDm = false;
		halbtc8723b1ant_InitHwConfig(pBtCoexist, false, false);
		halbtc8723b1ant_InitCoexDm(pBtCoexist);
		halbtc8723b1ant_QueryBtInfo(pBtCoexist);
	}
}

void EXhalbtc8723b1ant_Periodical(PBTC_COEXIST pBtCoexist)
{
	static u8 disVerInfoCnt;
	u32 fwVer = 0, btPatchVer = 0;

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], ==========================Periodical ===========================\n"));

	if (disVerInfoCnt <= 5) {
		disVerInfoCnt += 1;
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_INIT, ("[BTCoex], ****************************************************************\n"));
		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_BT_PATCH_VER, &btPatchVer);
		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_FW_VER, &fwVer);
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_INIT, ("[BTCoex], CoexVer/ FwVer/ PatchVer = %d_%x/ 0x%x/ 0x%x(%d)\n", \
			GLCoexVerDate8723b1Ant, GLCoexVer8723b1Ant, fwVer, btPatchVer, btPatchVer));
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_INIT, ("[BTCoex], ****************************************************************\n"));
	}

	halbtc8723b1ant_MonitorBtCtr(pBtCoexist);
	halbtc8723b1ant_MonitorWiFiCtr(pBtCoexist);

	if (
		halbtc8723b1ant_IsWifiStatusChanged(pBtCoexist) ||
		pCoexDm->bAutoTdmaAdjust
	)
		halbtc8723b1ant_RunCoexistMechanism(pBtCoexist);

	pCoexSta->specialPktPeriodCnt++;
}
