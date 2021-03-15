// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/

#include "Mp_Precomp.h"

/* defines */
#define HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(val)			      \
do {									      \
	halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, true, val);           \
	pCoexDm->psTdmaDuAdjType = val;                                       \
} while (0)

/*  Global variables, these are static variables */
static struct COEX_DM_8723B_2ANT GLCoexDm8723b2Ant;
static struct COEX_DM_8723B_2ANT *pCoexDm = &GLCoexDm8723b2Ant;
static struct COEX_STA_8723B_2ANT GLCoexSta8723b2Ant;
static struct COEX_STA_8723B_2ANT *pCoexSta = &GLCoexSta8723b2Ant;

static const char *const GLBtInfoSrc8723b2Ant[] = {
	"BT Info[wifi fw]",
	"BT Info[bt rsp]",
	"BT Info[bt auto report]",
};

static u32 GLCoexVerDate8723b2Ant = 20131211;
static u32 GLCoexVer8723b2Ant = 0x40;

/*  local function start with halbtc8723b2ant_ */
static u8 halbtc8723b2ant_BtRssiState(
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
			if (btRssi >= (rssiThresh+BTC_RSSI_COEX_THRESH_TOL_8723B_2ANT)) {
				btRssiState = BTC_RSSI_STATE_HIGH;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE, ("[BTCoex], BT Rssi state switch to High\n"));
			} else {
				btRssiState = BTC_RSSI_STATE_STAY_LOW;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE, ("[BTCoex], BT Rssi state stay at Low\n"));
			}
		} else {
			if (btRssi < rssiThresh) {
				btRssiState = BTC_RSSI_STATE_LOW;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE, ("[BTCoex], BT Rssi state switch to Low\n"));
			} else {
				btRssiState = BTC_RSSI_STATE_STAY_HIGH;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE, ("[BTCoex], BT Rssi state stay at High\n"));
			}
		}
	} else if (levelNum == 3) {
		if (rssiThresh > rssiThresh1) {
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE, ("[BTCoex], BT Rssi thresh error!!\n"));
			return pCoexSta->preBtRssiState;
		}

		if (
			(pCoexSta->preBtRssiState == BTC_RSSI_STATE_LOW) ||
			(pCoexSta->preBtRssiState == BTC_RSSI_STATE_STAY_LOW)
		) {
			if (btRssi >= (rssiThresh+BTC_RSSI_COEX_THRESH_TOL_8723B_2ANT)) {
				btRssiState = BTC_RSSI_STATE_MEDIUM;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE, ("[BTCoex], BT Rssi state switch to Medium\n"));
			} else {
				btRssiState = BTC_RSSI_STATE_STAY_LOW;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE, ("[BTCoex], BT Rssi state stay at Low\n"));
			}
		} else if (
			(pCoexSta->preBtRssiState == BTC_RSSI_STATE_MEDIUM) ||
			(pCoexSta->preBtRssiState == BTC_RSSI_STATE_STAY_MEDIUM)
		) {
			if (btRssi >= (rssiThresh1+BTC_RSSI_COEX_THRESH_TOL_8723B_2ANT)) {
				btRssiState = BTC_RSSI_STATE_HIGH;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE, ("[BTCoex], BT Rssi state switch to High\n"));
			} else if (btRssi < rssiThresh) {
				btRssiState = BTC_RSSI_STATE_LOW;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE, ("[BTCoex], BT Rssi state switch to Low\n"));
			} else {
				btRssiState = BTC_RSSI_STATE_STAY_MEDIUM;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE, ("[BTCoex], BT Rssi state stay at Medium\n"));
			}
		} else {
			if (btRssi < rssiThresh1) {
				btRssiState = BTC_RSSI_STATE_MEDIUM;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE, ("[BTCoex], BT Rssi state switch to Medium\n"));
			} else {
				btRssiState = BTC_RSSI_STATE_STAY_HIGH;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE, ("[BTCoex], BT Rssi state stay at High\n"));
			}
		}
	}

	pCoexSta->preBtRssiState = btRssiState;

	return btRssiState;
}

static u8 halbtc8723b2ant_WifiRssiState(
	struct BTC_COEXIST *pBtCoexist,
	u8 index,
	u8 levelNum,
	u8 rssiThresh,
	u8 rssiThresh1
)
{
	s32 wifiRssi = 0;
	u8 wifiRssiState = pCoexSta->preWifiRssiState[index];

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_S4_WIFI_RSSI, &wifiRssi);

	if (levelNum == 2) {
		if (
			(pCoexSta->preWifiRssiState[index] == BTC_RSSI_STATE_LOW) ||
			(pCoexSta->preWifiRssiState[index] == BTC_RSSI_STATE_STAY_LOW)
		) {
			if (wifiRssi >= (rssiThresh+BTC_RSSI_COEX_THRESH_TOL_8723B_2ANT)) {
				wifiRssiState = BTC_RSSI_STATE_HIGH;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_WIFI_RSSI_STATE, ("[BTCoex], wifi RSSI state switch to High\n"));
			} else {
				wifiRssiState = BTC_RSSI_STATE_STAY_LOW;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_WIFI_RSSI_STATE, ("[BTCoex], wifi RSSI state stay at Low\n"));
			}
		} else {
			if (wifiRssi < rssiThresh) {
				wifiRssiState = BTC_RSSI_STATE_LOW;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_WIFI_RSSI_STATE, ("[BTCoex], wifi RSSI state switch to Low\n"));
			} else {
				wifiRssiState = BTC_RSSI_STATE_STAY_HIGH;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_WIFI_RSSI_STATE, ("[BTCoex], wifi RSSI state stay at High\n"));
			}
		}
	} else if (levelNum == 3) {
		if (rssiThresh > rssiThresh1) {
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_WIFI_RSSI_STATE, ("[BTCoex], wifi RSSI thresh error!!\n"));
			return pCoexSta->preWifiRssiState[index];
		}

		if (
			(pCoexSta->preWifiRssiState[index] == BTC_RSSI_STATE_LOW) ||
			(pCoexSta->preWifiRssiState[index] == BTC_RSSI_STATE_STAY_LOW)
		) {
			if (wifiRssi >= (rssiThresh+BTC_RSSI_COEX_THRESH_TOL_8723B_2ANT)) {
				wifiRssiState = BTC_RSSI_STATE_MEDIUM;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_WIFI_RSSI_STATE, ("[BTCoex], wifi RSSI state switch to Medium\n"));
			} else {
				wifiRssiState = BTC_RSSI_STATE_STAY_LOW;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_WIFI_RSSI_STATE, ("[BTCoex], wifi RSSI state stay at Low\n"));
			}
		} else if (
			(pCoexSta->preWifiRssiState[index] == BTC_RSSI_STATE_MEDIUM) ||
			(pCoexSta->preWifiRssiState[index] == BTC_RSSI_STATE_STAY_MEDIUM)
		) {
			if (wifiRssi >= (rssiThresh1+BTC_RSSI_COEX_THRESH_TOL_8723B_2ANT)) {
				wifiRssiState = BTC_RSSI_STATE_HIGH;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_WIFI_RSSI_STATE, ("[BTCoex], wifi RSSI state switch to High\n"));
			} else if (wifiRssi < rssiThresh) {
				wifiRssiState = BTC_RSSI_STATE_LOW;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_WIFI_RSSI_STATE, ("[BTCoex], wifi RSSI state switch to Low\n"));
			} else {
				wifiRssiState = BTC_RSSI_STATE_STAY_MEDIUM;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_WIFI_RSSI_STATE, ("[BTCoex], wifi RSSI state stay at Medium\n"));
			}
		} else {
			if (wifiRssi < rssiThresh1) {
				wifiRssiState = BTC_RSSI_STATE_MEDIUM;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_WIFI_RSSI_STATE, ("[BTCoex], wifi RSSI state switch to Medium\n"));
			} else {
				wifiRssiState = BTC_RSSI_STATE_STAY_HIGH;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_WIFI_RSSI_STATE, ("[BTCoex], wifi RSSI state stay at High\n"));
			}
		}
	}

	pCoexSta->preWifiRssiState[index] = wifiRssiState;

	return wifiRssiState;
}

static void halbtc8723b2ant_LimitedRx(
	struct BTC_COEXIST *pBtCoexist,
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
	pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_BL_TO_REJ_AP_AGG_PKT, &bRejectRxAgg);
	/*  decide BT control aggregation buf size or not */
	pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_BL_BT_CTRL_AGG_SIZE, &bBtCtrlRxAggSize);
	/*  aggregation buf size, only work when BT control Rx aggregation size. */
	pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_U1_AGG_BUF_SIZE, &rxAggSize);
	/*  real update aggregation setting */
	pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_ACT_AGGREGATE_CTRL, NULL);
}

static void halbtc8723b2ant_MonitorBtCtr(struct BTC_COEXIST *pBtCoexist)
{
	u32 regHPTxRx, regLPTxRx, u4Tmp;
	u32 regHPTx = 0, regHPRx = 0, regLPTx = 0, regLPRx = 0;

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

	BTC_PRINT(
		BTC_MSG_ALGORITHM,
		ALGO_BT_MONITOR,
		(
			"[BTCoex], High Priority Tx/Rx (reg 0x%x) = 0x%x(%d)/0x%x(%d)\n",
			regHPTxRx,
			regHPTx,
			regHPTx,
			regHPRx,
			regHPRx
		)
	);
	BTC_PRINT(
		BTC_MSG_ALGORITHM,
		ALGO_BT_MONITOR,
		(
			"[BTCoex], Low Priority Tx/Rx (reg 0x%x) = 0x%x(%d)/0x%x(%d)\n",
			regLPTxRx,
			regLPTx,
			regLPTx,
			regLPRx,
			regLPRx
		)
	);

	/*  reset counter */
	pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x76e, 0xc);
}

static void halbtc8723b2ant_QueryBtInfo(struct BTC_COEXIST *pBtCoexist)
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

static bool halbtc8723b2ant_IsWifiStatusChanged(struct BTC_COEXIST *pBtCoexist)
{
	static bool	bPreWifiBusy, bPreUnder4way, bPreBtHsOn;
	bool bWifiBusy = false, bUnder4way = false, bBtHsOn = false;
	bool bWifiConnected = false;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_CONNECTED, &bWifiConnected);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_BUSY, &bWifiBusy);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_HS_OPERATION, &bBtHsOn);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_4_WAY_PROGRESS, &bUnder4way);

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

static void halbtc8723b2ant_UpdateBtLinkInfo(struct BTC_COEXIST *pBtCoexist)
{
	struct BTC_BT_LINK_INFO *pBtLinkInfo = &pBtCoexist->btLinkInfo;
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

static u8 halbtc8723b2ant_ActionAlgorithm(struct BTC_COEXIST *pBtCoexist)
{
	struct BTC_BT_LINK_INFO *pBtLinkInfo = &pBtCoexist->btLinkInfo;
	bool bBtHsOn = false;
	u8 algorithm = BT_8723B_2ANT_COEX_ALGO_UNDEFINED;
	u8 numOfDiffProfile = 0;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_HS_OPERATION, &bBtHsOn);

	if (!pBtLinkInfo->bBtLinkExist) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], No BT link exists!!!\n"));
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
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], SCO only\n"));
			algorithm = BT_8723B_2ANT_COEX_ALGO_SCO;
		} else {
			if (pBtLinkInfo->bHidExist) {
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], HID only\n"));
				algorithm = BT_8723B_2ANT_COEX_ALGO_HID;
			} else if (pBtLinkInfo->bA2dpExist) {
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], A2DP only\n"));
				algorithm = BT_8723B_2ANT_COEX_ALGO_A2DP;
			} else if (pBtLinkInfo->bPanExist) {
				if (bBtHsOn) {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], PAN(HS) only\n"));
					algorithm = BT_8723B_2ANT_COEX_ALGO_PANHS;
				} else {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], PAN(EDR) only\n"));
					algorithm = BT_8723B_2ANT_COEX_ALGO_PANEDR;
				}
			}
		}
	} else if (numOfDiffProfile == 2) {
		if (pBtLinkInfo->bScoExist) {
			if (pBtLinkInfo->bHidExist) {
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], SCO + HID\n"));
				algorithm = BT_8723B_2ANT_COEX_ALGO_PANEDR_HID;
			} else if (pBtLinkInfo->bA2dpExist) {
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], SCO + A2DP ==> SCO\n"));
				algorithm = BT_8723B_2ANT_COEX_ALGO_PANEDR_HID;
			} else if (pBtLinkInfo->bPanExist) {
				if (bBtHsOn) {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], SCO + PAN(HS)\n"));
					algorithm = BT_8723B_2ANT_COEX_ALGO_SCO;
				} else {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], SCO + PAN(EDR)\n"));
					algorithm = BT_8723B_2ANT_COEX_ALGO_PANEDR_HID;
				}
			}
		} else {
			if (
				pBtLinkInfo->bHidExist &&
				pBtLinkInfo->bA2dpExist
			) {
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], HID + A2DP\n"));
				algorithm = BT_8723B_2ANT_COEX_ALGO_HID_A2DP;
			} else if (
				pBtLinkInfo->bHidExist &&
				pBtLinkInfo->bPanExist
			) {
				if (bBtHsOn) {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], HID + PAN(HS)\n"));
					algorithm = BT_8723B_2ANT_COEX_ALGO_HID;
				} else {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], HID + PAN(EDR)\n"));
					algorithm = BT_8723B_2ANT_COEX_ALGO_PANEDR_HID;
				}
			} else if (
				pBtLinkInfo->bPanExist &&
				pBtLinkInfo->bA2dpExist
			) {
				if (bBtHsOn) {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], A2DP + PAN(HS)\n"));
					algorithm = BT_8723B_2ANT_COEX_ALGO_A2DP_PANHS;
				} else {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], A2DP + PAN(EDR)\n"));
					algorithm = BT_8723B_2ANT_COEX_ALGO_PANEDR_A2DP;
				}
			}
		}
	} else if (numOfDiffProfile == 3) {
		if (pBtLinkInfo->bScoExist) {
			if (
				pBtLinkInfo->bHidExist &&
				pBtLinkInfo->bA2dpExist
			) {
				BTC_PRINT(
					BTC_MSG_ALGORITHM,
					ALGO_TRACE,
					("[BTCoex], SCO + HID + A2DP ==> HID\n")
				);
				algorithm = BT_8723B_2ANT_COEX_ALGO_PANEDR_HID;
			} else if (
				pBtLinkInfo->bHidExist &&
				pBtLinkInfo->bPanExist
			) {
				if (bBtHsOn) {
					BTC_PRINT(
						BTC_MSG_ALGORITHM,
						ALGO_TRACE,
						("[BTCoex], SCO + HID + PAN(HS)\n")
					);
					algorithm = BT_8723B_2ANT_COEX_ALGO_PANEDR_HID;
				} else {
					BTC_PRINT(
						BTC_MSG_ALGORITHM,
						ALGO_TRACE,
						("[BTCoex], SCO + HID + PAN(EDR)\n")
					);
					algorithm = BT_8723B_2ANT_COEX_ALGO_PANEDR_HID;
				}
			} else if (
				pBtLinkInfo->bPanExist &&
				pBtLinkInfo->bA2dpExist
			) {
				if (bBtHsOn) {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], SCO + A2DP + PAN(HS)\n"));
					algorithm = BT_8723B_2ANT_COEX_ALGO_PANEDR_HID;
				} else {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], SCO + A2DP + PAN(EDR) ==> HID\n"));
					algorithm = BT_8723B_2ANT_COEX_ALGO_PANEDR_HID;
				}
			}
		} else {
			if (
				pBtLinkInfo->bHidExist &&
				pBtLinkInfo->bPanExist &&
				pBtLinkInfo->bA2dpExist
			) {
				if (bBtHsOn) {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], HID + A2DP + PAN(HS)\n"));
					algorithm = BT_8723B_2ANT_COEX_ALGO_HID_A2DP;
				} else {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], HID + A2DP + PAN(EDR)\n"));
					algorithm = BT_8723B_2ANT_COEX_ALGO_HID_A2DP_PANEDR;
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
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Error!!! SCO + HID + A2DP + PAN(HS)\n"));

				} else {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], SCO + HID + A2DP + PAN(EDR) ==>PAN(EDR)+HID\n"));
					algorithm = BT_8723B_2ANT_COEX_ALGO_PANEDR_HID;
				}
			}
		}
	}

	return algorithm;
}

static void halbtc8723b2ant_SetFwDacSwingLevel(
	struct BTC_COEXIST *pBtCoexist, u8 dacSwingLvl
)
{
	u8 	H2C_Parameter[1] = {0};

	/*  There are several type of dacswing */
	/*  0x18/ 0x10/ 0xc/ 0x8/ 0x4/ 0x6 */
	H2C_Parameter[0] = dacSwingLvl;

	BTC_PRINT(
		BTC_MSG_ALGORITHM,
		ALGO_TRACE_FW_EXEC,
		("[BTCoex], Set Dac Swing Level = 0x%x\n", dacSwingLvl)
	);
	BTC_PRINT(
		BTC_MSG_ALGORITHM,
		ALGO_TRACE_FW_EXEC,
		("[BTCoex], FW write 0x64 = 0x%x\n", H2C_Parameter[0])
	);

	pBtCoexist->fBtcFillH2c(pBtCoexist, 0x64, 1, H2C_Parameter);
}

static void halbtc8723b2ant_SetFwDecBtPwr(
	struct BTC_COEXIST *pBtCoexist, u8 decBtPwrLvl
)
{
	u8 	H2C_Parameter[1] = {0};

	H2C_Parameter[0] = decBtPwrLvl;

	BTC_PRINT(
		BTC_MSG_ALGORITHM,
		ALGO_TRACE_FW_EXEC,
		(
			"[BTCoex], decrease Bt Power level = %d, FW write 0x62 = 0x%x\n",
			decBtPwrLvl,
			H2C_Parameter[0]
		)
	);

	pBtCoexist->fBtcFillH2c(pBtCoexist, 0x62, 1, H2C_Parameter);
}

static void halbtc8723b2ant_DecBtPwr(
	struct BTC_COEXIST *pBtCoexist, bool bForceExec, u8 decBtPwrLvl
)
{
	BTC_PRINT(
		BTC_MSG_ALGORITHM,
		ALGO_TRACE_FW,
		(
			"[BTCoex], %s Dec BT power level = %d\n",
			(bForceExec ? "force to" : ""),
			decBtPwrLvl
		)
	);
	pCoexDm->curBtDecPwrLvl = decBtPwrLvl;

	if (!bForceExec) {
		BTC_PRINT(
			BTC_MSG_ALGORITHM,
			ALGO_TRACE_FW_DETAIL,
			(
				"[BTCoex], preBtDecPwrLvl =%d, curBtDecPwrLvl =%d\n",
				pCoexDm->preBtDecPwrLvl,
				pCoexDm->curBtDecPwrLvl
			)
		);

		if (pCoexDm->preBtDecPwrLvl == pCoexDm->curBtDecPwrLvl)
			return;
	}
	halbtc8723b2ant_SetFwDecBtPwr(pBtCoexist, pCoexDm->curBtDecPwrLvl);

	pCoexDm->preBtDecPwrLvl = pCoexDm->curBtDecPwrLvl;
}

static void halbtc8723b2ant_FwDacSwingLvl(
	struct BTC_COEXIST *pBtCoexist, bool bForceExec, u8 fwDacSwingLvl
)
{
	BTC_PRINT(
		BTC_MSG_ALGORITHM,
		ALGO_TRACE_FW,
		(
			"[BTCoex], %s set FW Dac Swing level = %d\n",
			(bForceExec ? "force to" : ""),
			fwDacSwingLvl
		)
	);
	pCoexDm->curFwDacSwingLvl = fwDacSwingLvl;

	if (!bForceExec) {
		BTC_PRINT(
			BTC_MSG_ALGORITHM,
			ALGO_TRACE_FW_DETAIL,
			(
				"[BTCoex], preFwDacSwingLvl =%d, curFwDacSwingLvl =%d\n",
				pCoexDm->preFwDacSwingLvl,
				pCoexDm->curFwDacSwingLvl
			)
		);

		if (pCoexDm->preFwDacSwingLvl == pCoexDm->curFwDacSwingLvl)
			return;
	}

	halbtc8723b2ant_SetFwDacSwingLevel(pBtCoexist, pCoexDm->curFwDacSwingLvl);

	pCoexDm->preFwDacSwingLvl = pCoexDm->curFwDacSwingLvl;
}

static void halbtc8723b2ant_SetSwRfRxLpfCorner(
	struct BTC_COEXIST *pBtCoexist,
	bool bRxRfShrinkOn
)
{
	if (bRxRfShrinkOn) {
		/* Shrink RF Rx LPF corner */
		BTC_PRINT(
			BTC_MSG_ALGORITHM,
			ALGO_TRACE_SW_EXEC,
			("[BTCoex], Shrink RF Rx LPF corner!!\n")
		);
		pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1e, 0xfffff, 0xffffc);
	} else {
		/* Resume RF Rx LPF corner */
		/*  After initialized, we can use pCoexDm->btRf0x1eBackup */
		if (pBtCoexist->bInitilized) {
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC, ("[BTCoex], Resume RF Rx LPF corner!!\n"));
			pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1e, 0xfffff, pCoexDm->btRf0x1eBackup);
		}
	}
}

static void halbtc8723b2ant_RfShrink(
	struct BTC_COEXIST *pBtCoexist, bool bForceExec, bool bRxRfShrinkOn
)
{
	BTC_PRINT(
		BTC_MSG_ALGORITHM,
		ALGO_TRACE_SW,
		(
			"[BTCoex], %s turn Rx RF Shrink = %s\n",
			(bForceExec ? "force to" : ""),
			(bRxRfShrinkOn ? "ON" : "OFF")
		)
	);
	pCoexDm->bCurRfRxLpfShrink = bRxRfShrinkOn;

	if (!bForceExec) {
		BTC_PRINT(
			BTC_MSG_ALGORITHM,
			ALGO_TRACE_SW_DETAIL,
			(
				"[BTCoex], bPreRfRxLpfShrink =%d, bCurRfRxLpfShrink =%d\n",
				pCoexDm->bPreRfRxLpfShrink,
				pCoexDm->bCurRfRxLpfShrink
			)
		);

		if (pCoexDm->bPreRfRxLpfShrink == pCoexDm->bCurRfRxLpfShrink)
			return;
	}
	halbtc8723b2ant_SetSwRfRxLpfCorner(pBtCoexist, pCoexDm->bCurRfRxLpfShrink);

	pCoexDm->bPreRfRxLpfShrink = pCoexDm->bCurRfRxLpfShrink;
}

static void halbtc8723b2ant_SetSwPenaltyTxRateAdaptive(
	struct BTC_COEXIST *pBtCoexist, bool bLowPenaltyRa
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

static void halbtc8723b2ant_LowPenaltyRa(
	struct BTC_COEXIST *pBtCoexist, bool bForceExec, bool bLowPenaltyRa
)
{
	/* return; */
	BTC_PRINT(
		BTC_MSG_ALGORITHM,
		ALGO_TRACE_SW,
		(
			"[BTCoex], %s turn LowPenaltyRA = %s\n",
			(bForceExec ? "force to" : ""),
			(bLowPenaltyRa ? "ON" : "OFF")
		)
	);
	pCoexDm->bCurLowPenaltyRa = bLowPenaltyRa;

	if (!bForceExec) {
		BTC_PRINT(
			BTC_MSG_ALGORITHM,
			ALGO_TRACE_SW_DETAIL,
			(
				"[BTCoex], bPreLowPenaltyRa =%d, bCurLowPenaltyRa =%d\n",
				pCoexDm->bPreLowPenaltyRa,
				pCoexDm->bCurLowPenaltyRa
			)
		);

		if (pCoexDm->bPreLowPenaltyRa == pCoexDm->bCurLowPenaltyRa)
			return;
	}
	halbtc8723b2ant_SetSwPenaltyTxRateAdaptive(pBtCoexist, pCoexDm->bCurLowPenaltyRa);

	pCoexDm->bPreLowPenaltyRa = pCoexDm->bCurLowPenaltyRa;
}

static void halbtc8723b2ant_SetDacSwingReg(struct BTC_COEXIST *pBtCoexist, u32 level)
{
	u8 val = (u8)level;

	BTC_PRINT(
		BTC_MSG_ALGORITHM,
		ALGO_TRACE_SW_EXEC,
		("[BTCoex], Write SwDacSwing = 0x%x\n", level)
	);
	pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0x883, 0x3e, val);
}

static void halbtc8723b2ant_SetSwFullTimeDacSwing(
	struct BTC_COEXIST *pBtCoexist, bool bSwDacSwingOn, u32 swDacSwingLvl
)
{
	if (bSwDacSwingOn)
		halbtc8723b2ant_SetDacSwingReg(pBtCoexist, swDacSwingLvl);
	else
		halbtc8723b2ant_SetDacSwingReg(pBtCoexist, 0x18);
}


static void halbtc8723b2ant_DacSwing(
	struct BTC_COEXIST *pBtCoexist,
	bool bForceExec,
	bool bDacSwingOn,
	u32 dacSwingLvl
)
{
	BTC_PRINT(
		BTC_MSG_ALGORITHM,
		ALGO_TRACE_SW,
		(
			"[BTCoex], %s turn DacSwing =%s, dacSwingLvl = 0x%x\n",
			(bForceExec ? "force to" : ""),
			(bDacSwingOn ? "ON" : "OFF"),
			dacSwingLvl
		)
	);
	pCoexDm->bCurDacSwingOn = bDacSwingOn;
	pCoexDm->curDacSwingLvl = dacSwingLvl;

	if (!bForceExec) {
		BTC_PRINT(
			BTC_MSG_ALGORITHM,
			ALGO_TRACE_SW_DETAIL,
			(
				"[BTCoex], bPreDacSwingOn =%d, preDacSwingLvl = 0x%x, bCurDacSwingOn =%d, curDacSwingLvl = 0x%x\n",
				pCoexDm->bPreDacSwingOn,
				pCoexDm->preDacSwingLvl,
				pCoexDm->bCurDacSwingOn,
				pCoexDm->curDacSwingLvl
			)
		);

		if ((pCoexDm->bPreDacSwingOn == pCoexDm->bCurDacSwingOn) &&
			(pCoexDm->preDacSwingLvl == pCoexDm->curDacSwingLvl))
			return;
	}
	mdelay(30);
	halbtc8723b2ant_SetSwFullTimeDacSwing(pBtCoexist, bDacSwingOn, dacSwingLvl);

	pCoexDm->bPreDacSwingOn = pCoexDm->bCurDacSwingOn;
	pCoexDm->preDacSwingLvl = pCoexDm->curDacSwingLvl;
}

static void halbtc8723b2ant_SetAgcTable(
	struct BTC_COEXIST *pBtCoexist, bool bAgcTableEn
)
{
	u8 rssiAdjustVal = 0;

	/* BB AGC Gain Table */
	if (bAgcTableEn) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC, ("[BTCoex], BB Agc Table On!\n"));
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x6e1A0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x6d1B0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x6c1C0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x6b1D0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x6a1E0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x691F0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x68200001);
	} else {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC, ("[BTCoex], BB Agc Table Off!\n"));
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0xaa1A0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0xa91B0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0xa81C0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0xa71D0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0xa61E0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0xa51F0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0xa4200001);
	}


	/* RF Gain */
	pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0xef, 0xfffff, 0x02000);
	if (bAgcTableEn) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC, ("[BTCoex], Agc Table On!\n"));
		pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x3b, 0xfffff, 0x38fff);
		pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x3b, 0xfffff, 0x38ffe);
	} else {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC, ("[BTCoex], Agc Table Off!\n"));
		pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x3b, 0xfffff, 0x380c3);
		pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x3b, 0xfffff, 0x28ce6);
	}
	pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0xef, 0xfffff, 0x0);

	pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0xed, 0xfffff, 0x1);
	if (bAgcTableEn) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC, ("[BTCoex], Agc Table On!\n"));
		pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x40, 0xfffff, 0x38fff);
		pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x40, 0xfffff, 0x38ffe);
	} else {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC, ("[BTCoex], Agc Table Off!\n"));
		pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x40, 0xfffff, 0x380c3);
		pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x40, 0xfffff, 0x28ce6);
	}
	pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0xed, 0xfffff, 0x0);

	/*  set rssiAdjustVal for wifi module. */
	if (bAgcTableEn)
		rssiAdjustVal = 8;

	pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_U1_RSSI_ADJ_VAL_FOR_AGC_TABLE_ON, &rssiAdjustVal);
}

static void halbtc8723b2ant_AgcTable(
	struct BTC_COEXIST *pBtCoexist, bool bForceExec, bool bAgcTableEn
)
{
	BTC_PRINT(
		BTC_MSG_ALGORITHM,
		ALGO_TRACE_SW,
		(
			"[BTCoex], %s %s Agc Table\n",
			(bForceExec ? "force to" : ""),
			(bAgcTableEn ? "Enable" : "Disable")
		)
	);
	pCoexDm->bCurAgcTableEn = bAgcTableEn;

	if (!bForceExec) {
		BTC_PRINT(
			BTC_MSG_ALGORITHM,
			ALGO_TRACE_SW_DETAIL,
			(
				"[BTCoex], bPreAgcTableEn =%d, bCurAgcTableEn =%d\n",
				pCoexDm->bPreAgcTableEn,
				pCoexDm->bCurAgcTableEn
			)
		);

		if (pCoexDm->bPreAgcTableEn == pCoexDm->bCurAgcTableEn)
			return;
	}
	halbtc8723b2ant_SetAgcTable(pBtCoexist, bAgcTableEn);

	pCoexDm->bPreAgcTableEn = pCoexDm->bCurAgcTableEn;
}

static void halbtc8723b2ant_SetCoexTable(
	struct BTC_COEXIST *pBtCoexist,
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

static void halbtc8723b2ant_CoexTable(
	struct BTC_COEXIST *pBtCoexist,
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
			"[BTCoex], %s write Coex Table 0x6c0 = 0x%x, 0x6c4 = 0x%x, 0x6c8 = 0x%x, 0x6cc = 0x%x\n",
			(bForceExec ? "force to" : ""),
			val0x6c0,
			val0x6c4,
			val0x6c8,
			val0x6cc
		)
	);
	pCoexDm->curVal0x6c0 = val0x6c0;
	pCoexDm->curVal0x6c4 = val0x6c4;
	pCoexDm->curVal0x6c8 = val0x6c8;
	pCoexDm->curVal0x6cc = val0x6cc;

	if (!bForceExec) {
		BTC_PRINT(
			BTC_MSG_ALGORITHM,
			ALGO_TRACE_SW_DETAIL,
			(
				"[BTCoex], preVal0x6c0 = 0x%x, preVal0x6c4 = 0x%x, preVal0x6c8 = 0x%x, preVal0x6cc = 0x%x !!\n",
				pCoexDm->preVal0x6c0,
				pCoexDm->preVal0x6c4,
				pCoexDm->preVal0x6c8,
				pCoexDm->preVal0x6cc
			)
		);
		BTC_PRINT(
			BTC_MSG_ALGORITHM,
			ALGO_TRACE_SW_DETAIL,
			(
				"[BTCoex], curVal0x6c0 = 0x%x, curVal0x6c4 = 0x%x, curVal0x6c8 = 0x%x, curVal0x6cc = 0x%x !!\n",
				pCoexDm->curVal0x6c0,
				pCoexDm->curVal0x6c4,
				pCoexDm->curVal0x6c8,
				pCoexDm->curVal0x6cc
			)
		);

		if (
			(pCoexDm->preVal0x6c0 == pCoexDm->curVal0x6c0) &&
			(pCoexDm->preVal0x6c4 == pCoexDm->curVal0x6c4) &&
			(pCoexDm->preVal0x6c8 == pCoexDm->curVal0x6c8) &&
			(pCoexDm->preVal0x6cc == pCoexDm->curVal0x6cc)
		)
			return;
	}
	halbtc8723b2ant_SetCoexTable(pBtCoexist, val0x6c0, val0x6c4, val0x6c8, val0x6cc);

	pCoexDm->preVal0x6c0 = pCoexDm->curVal0x6c0;
	pCoexDm->preVal0x6c4 = pCoexDm->curVal0x6c4;
	pCoexDm->preVal0x6c8 = pCoexDm->curVal0x6c8;
	pCoexDm->preVal0x6cc = pCoexDm->curVal0x6cc;
}

static void halbtc8723b2ant_CoexTableWithType(
	struct BTC_COEXIST *pBtCoexist, bool bForceExec, u8 type
)
{
	switch (type) {
	case 0:
		halbtc8723b2ant_CoexTable(pBtCoexist, bForceExec, 0x55555555, 0x55555555, 0xffff, 0x3);
		break;
	case 1:
		halbtc8723b2ant_CoexTable(pBtCoexist, bForceExec, 0x55555555, 0x5afa5afa, 0xffff, 0x3);
		break;
	case 2:
		halbtc8723b2ant_CoexTable(pBtCoexist, bForceExec, 0x5a5a5a5a, 0x5a5a5a5a, 0xffff, 0x3);
		break;
	case 3:
		halbtc8723b2ant_CoexTable(pBtCoexist, bForceExec, 0xaaaaaaaa, 0xaaaaaaaa, 0xffff, 0x3);
		break;
	case 4:
		halbtc8723b2ant_CoexTable(pBtCoexist, bForceExec, 0xffffffff, 0xffffffff, 0xffff, 0x3);
		break;
	case 5:
		halbtc8723b2ant_CoexTable(pBtCoexist, bForceExec, 0x5fff5fff, 0x5fff5fff, 0xffff, 0x3);
		break;
	case 6:
		halbtc8723b2ant_CoexTable(pBtCoexist, bForceExec, 0x55ff55ff, 0x5a5a5a5a, 0xffff, 0x3);
		break;
	case 7:
		halbtc8723b2ant_CoexTable(pBtCoexist, bForceExec, 0x55ff55ff, 0xfafafafa, 0xffff, 0x3);
		break;
	case 8:
		halbtc8723b2ant_CoexTable(pBtCoexist, bForceExec, 0x5aea5aea, 0x5aea5aea, 0xffff, 0x3);
		break;
	case 9:
		halbtc8723b2ant_CoexTable(pBtCoexist, bForceExec, 0x55ff55ff, 0x5aea5aea, 0xffff, 0x3);
		break;
	case 10:
		halbtc8723b2ant_CoexTable(pBtCoexist, bForceExec, 0x55ff55ff, 0x5aff5aff, 0xffff, 0x3);
		break;
	case 11:
		halbtc8723b2ant_CoexTable(pBtCoexist, bForceExec, 0x55ff55ff, 0x5a5f5a5f, 0xffff, 0x3);
		break;
	case 12:
		halbtc8723b2ant_CoexTable(pBtCoexist, bForceExec, 0x55ff55ff, 0x5f5f5f5f, 0xffff, 0x3);
		break;
	default:
		break;
	}
}

static void halbtc8723b2ant_SetFwIgnoreWlanAct(
	struct BTC_COEXIST *pBtCoexist, bool bEnable
)
{
	u8 	H2C_Parameter[1] = {0};

	if (bEnable)
		H2C_Parameter[0] |= BIT0;		/*  function enable */

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

static void halbtc8723b2ant_IgnoreWlanAct(
	struct BTC_COEXIST *pBtCoexist, bool bForceExec, bool bEnable
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
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], bPreIgnoreWlanAct = %d, bCurIgnoreWlanAct = %d!!\n",
			pCoexDm->bPreIgnoreWlanAct, pCoexDm->bCurIgnoreWlanAct));

		if (pCoexDm->bPreIgnoreWlanAct == pCoexDm->bCurIgnoreWlanAct)
			return;
	}
	halbtc8723b2ant_SetFwIgnoreWlanAct(pBtCoexist, bEnable);

	pCoexDm->bPreIgnoreWlanAct = pCoexDm->bCurIgnoreWlanAct;
}

static void halbtc8723b2ant_SetFwPstdma(
	struct BTC_COEXIST *pBtCoexist,
	u8 byte1,
	u8 byte2,
	u8 byte3,
	u8 byte4,
	u8 byte5
)
{
	u8 	H2C_Parameter[5] = {0};

	H2C_Parameter[0] = byte1;
	H2C_Parameter[1] = byte2;
	H2C_Parameter[2] = byte3;
	H2C_Parameter[3] = byte4;
	H2C_Parameter[4] = byte5;

	pCoexDm->psTdmaPara[0] = byte1;
	pCoexDm->psTdmaPara[1] = byte2;
	pCoexDm->psTdmaPara[2] = byte3;
	pCoexDm->psTdmaPara[3] = byte4;
	pCoexDm->psTdmaPara[4] = byte5;

	BTC_PRINT(
		BTC_MSG_ALGORITHM,
		ALGO_TRACE_FW_EXEC,
		(
			"[BTCoex], FW write 0x60(5bytes) = 0x%x%08x\n",
			H2C_Parameter[0],
			H2C_Parameter[1]<<24|
			H2C_Parameter[2]<<16|
			H2C_Parameter[3]<<8|
			H2C_Parameter[4]
		)
	);

	pBtCoexist->fBtcFillH2c(pBtCoexist, 0x60, 5, H2C_Parameter);
}

static void halbtc8723b2ant_SwMechanism1(
	struct BTC_COEXIST *pBtCoexist,
	bool bShrinkRxLPF,
	bool bLowPenaltyRA,
	bool bLimitedDIG,
	bool bBTLNAConstrain
)
{
	halbtc8723b2ant_RfShrink(pBtCoexist, NORMAL_EXEC, bShrinkRxLPF);
	halbtc8723b2ant_LowPenaltyRa(pBtCoexist, NORMAL_EXEC, bLowPenaltyRA);
}

static void halbtc8723b2ant_SwMechanism2(
	struct BTC_COEXIST *pBtCoexist,
	bool bAGCTableShift,
	bool bADCBackOff,
	bool bSWDACSwing,
	u32 dacSwingLvl
)
{
	halbtc8723b2ant_AgcTable(pBtCoexist, NORMAL_EXEC, bAGCTableShift);
	halbtc8723b2ant_DacSwing(pBtCoexist, NORMAL_EXEC, bSWDACSwing, dacSwingLvl);
}

static void halbtc8723b2ant_SetAntPath(
	struct BTC_COEXIST *pBtCoexist, u8 antPosType, bool bInitHwCfg, bool bWifiOff
)
{
	struct BTC_BOARD_INFO *pBoardInfo = &pBtCoexist->boardInfo;
	u32 fwVer = 0, u4Tmp = 0;
	bool bPgExtSwitch = false;
	bool bUseExtSwitch = false;
	u8 	H2C_Parameter[2] = {0};

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_EXT_SWITCH, &bPgExtSwitch);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_FW_VER, &fwVer);	/*  [31:16]=fw ver, [15:0]=fw sub ver */

	if ((fwVer > 0 && fwVer < 0xc0000) || bPgExtSwitch)
		bUseExtSwitch = true;

	if (bInitHwCfg) {
		pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0x39, 0x8, 0x1);
		pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x974, 0xff);
		pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0x944, 0x3, 0x3);
		pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x930, 0x77);
		pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0x67, 0x20, 0x1);

		if (fwVer >= 0x180000) {
			/* Use H2C to set GNT_BT to LOW */
			H2C_Parameter[0] = 0;
			pBtCoexist->fBtcFillH2c(pBtCoexist, 0x6E, 1, H2C_Parameter);
		} else {
			pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x765, 0x0);
		}

		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x948, 0x0);

		pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1, 0xfffff, 0x0); /* WiFi TRx Mask off */
		pBtCoexist->fBtcSetBtReg(pBtCoexist, BTC_BT_REG_RF, 0x3c, 0x01); /* BT TRx Mask off */

		if (pBoardInfo->btdmAntPos == BTC_ANTENNA_AT_MAIN_PORT) {
			/* tell firmware "no antenna inverse" */
			H2C_Parameter[0] = 0;
		} else {
			/* tell firmware "antenna inverse" */
			H2C_Parameter[0] = 1;
		}

		if (bUseExtSwitch) {
			/* ext switch type */
			H2C_Parameter[1] = 1;
		} else {
			/* int switch type */
			H2C_Parameter[1] = 0;
		}
		pBtCoexist->fBtcFillH2c(pBtCoexist, 0x65, 2, H2C_Parameter);
	}

	/*  ext switch setting */
	if (bUseExtSwitch) {
		if (bInitHwCfg) {
			/*  0x4c[23]= 0, 0x4c[24]= 1  Antenna control by WL/BT */
			u4Tmp = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x4c);
			u4Tmp &= ~BIT23;
			u4Tmp |= BIT24;
			pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x4c, u4Tmp);
		}

		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x948, 0x0); /*  fixed internal switch S1->WiFi, S0->BT */
		switch (antPosType) {
		case BTC_ANT_WIFI_AT_MAIN:
			pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0x92c, 0x3, 0x1);	/*  ext switch main at wifi */
			break;
		case BTC_ANT_WIFI_AT_AUX:
			pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0x92c, 0x3, 0x2);	/*  ext switch aux at wifi */
			break;
		}
	} else { /*  internal switch */
		if (bInitHwCfg) {
			/*  0x4c[23]= 0, 0x4c[24]= 1  Antenna control by WL/BT */
			u4Tmp = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x4c);
			u4Tmp |= BIT23;
			u4Tmp &= ~BIT24;
			pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x4c, u4Tmp);
		}

		pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0x64, 0x1, 0x0); /* fixed external switch S1->Main, S0->Aux */
		switch (antPosType) {
		case BTC_ANT_WIFI_AT_MAIN:
			pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x948, 0x0); /*  fixed internal switch S1->WiFi, S0->BT */
			break;
		case BTC_ANT_WIFI_AT_AUX:
			pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x948, 0x280); /*  fixed internal switch S0->WiFi, S1->BT */
			break;
		}
	}
}

static void halbtc8723b2ant_PsTdma(
	struct BTC_COEXIST *pBtCoexist, bool bForceExec, bool bTurnOn, u8 type
)
{
	BTC_PRINT(
		BTC_MSG_ALGORITHM,
		ALGO_TRACE_FW,
		(
			"[BTCoex], %s turn %s PS TDMA, type =%d\n",
			(bForceExec ? "force to" : ""),
			(bTurnOn ? "ON" : "OFF"),
			type
		)
	);
	pCoexDm->bCurPsTdmaOn = bTurnOn;
	pCoexDm->curPsTdma = type;

	if (!bForceExec) {
		BTC_PRINT(
			BTC_MSG_ALGORITHM,
			ALGO_TRACE_FW_DETAIL,
			(
				"[BTCoex], bPrePsTdmaOn = %d, bCurPsTdmaOn = %d!!\n",
				pCoexDm->bPrePsTdmaOn,
				pCoexDm->bCurPsTdmaOn
			)
		);
		BTC_PRINT(
			BTC_MSG_ALGORITHM,
			ALGO_TRACE_FW_DETAIL,
			(
				"[BTCoex], prePsTdma = %d, curPsTdma = %d!!\n",
				pCoexDm->prePsTdma, pCoexDm->curPsTdma
			)
		);

		if (
			(pCoexDm->bPrePsTdmaOn == pCoexDm->bCurPsTdmaOn) &&
			(pCoexDm->prePsTdma == pCoexDm->curPsTdma)
		)
			return;
	}

	if (bTurnOn) {
		switch (type) {
		case 1:
		default:
			halbtc8723b2ant_SetFwPstdma(pBtCoexist, 0xe3, 0x1a, 0x1a, 0xe1, 0x90);
			break;
		case 2:
			halbtc8723b2ant_SetFwPstdma(pBtCoexist, 0xe3, 0x12, 0x12, 0xe1, 0x90);
			break;
		case 3:
			halbtc8723b2ant_SetFwPstdma(pBtCoexist, 0xe3, 0x1c, 0x3, 0xf1, 0x90);
			break;
		case 4:
			halbtc8723b2ant_SetFwPstdma(pBtCoexist, 0xe3, 0x10, 0x03, 0xf1, 0x90);
			break;
		case 5:
			halbtc8723b2ant_SetFwPstdma(pBtCoexist, 0xe3, 0x1a, 0x1a, 0x60, 0x90);
			break;
		case 6:
			halbtc8723b2ant_SetFwPstdma(pBtCoexist, 0xe3, 0x12, 0x12, 0x60, 0x90);
			break;
		case 7:
			halbtc8723b2ant_SetFwPstdma(pBtCoexist, 0xe3, 0x1c, 0x3, 0x70, 0x90);
			break;
		case 8:
			halbtc8723b2ant_SetFwPstdma(pBtCoexist, 0xa3, 0x10, 0x3, 0x70, 0x90);
			break;
		case 9:
			halbtc8723b2ant_SetFwPstdma(pBtCoexist, 0xe3, 0x1a, 0x1a, 0xe1, 0x90);
			break;
		case 10:
			halbtc8723b2ant_SetFwPstdma(pBtCoexist, 0xe3, 0x12, 0x12, 0xe1, 0x90);
			break;
		case 11:
			halbtc8723b2ant_SetFwPstdma(pBtCoexist, 0xe3, 0xa, 0xa, 0xe1, 0x90);
			break;
		case 12:
			halbtc8723b2ant_SetFwPstdma(pBtCoexist, 0xe3, 0x5, 0x5, 0xe1, 0x90);
			break;
		case 13:
			halbtc8723b2ant_SetFwPstdma(pBtCoexist, 0xe3, 0x1a, 0x1a, 0x60, 0x90);
			break;
		case 14:
			halbtc8723b2ant_SetFwPstdma(pBtCoexist, 0xe3, 0x12, 0x12, 0x60, 0x90);
			break;
		case 15:
			halbtc8723b2ant_SetFwPstdma(pBtCoexist, 0xe3, 0xa, 0xa, 0x60, 0x90);
			break;
		case 16:
			halbtc8723b2ant_SetFwPstdma(pBtCoexist, 0xe3, 0x5, 0x5, 0x60, 0x90);
			break;
		case 17:
			halbtc8723b2ant_SetFwPstdma(pBtCoexist, 0xa3, 0x2f, 0x2f, 0x60, 0x90);
			break;
		case 18:
			halbtc8723b2ant_SetFwPstdma(pBtCoexist, 0xe3, 0x5, 0x5, 0xe1, 0x90);
			break;
		case 19:
			halbtc8723b2ant_SetFwPstdma(pBtCoexist, 0xe3, 0x25, 0x25, 0xe1, 0x90);
			break;
		case 20:
			halbtc8723b2ant_SetFwPstdma(pBtCoexist, 0xe3, 0x25, 0x25, 0x60, 0x90);
			break;
		case 21:
			halbtc8723b2ant_SetFwPstdma(pBtCoexist, 0xe3, 0x15, 0x03, 0x70, 0x90);
			break;
		case 71:
			halbtc8723b2ant_SetFwPstdma(pBtCoexist, 0xe3, 0x1a, 0x1a, 0xe1, 0x90);
			break;
		}
	} else {
		/*  disable PS tdma */
		switch (type) {
		case 0:
			halbtc8723b2ant_SetFwPstdma(pBtCoexist, 0x0, 0x0, 0x0, 0x40, 0x0);
			break;
		case 1:
			halbtc8723b2ant_SetFwPstdma(pBtCoexist, 0x0, 0x0, 0x0, 0x48, 0x0);
			break;
		default:
			halbtc8723b2ant_SetFwPstdma(pBtCoexist, 0x0, 0x0, 0x0, 0x40, 0x0);
			break;
		}
	}

	/*  update pre state */
	pCoexDm->bPrePsTdmaOn = pCoexDm->bCurPsTdmaOn;
	pCoexDm->prePsTdma = pCoexDm->curPsTdma;
}

static void halbtc8723b2ant_CoexAllOff(struct BTC_COEXIST *pBtCoexist)
{
	/*  fw all off */
	halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, false, 1);
	halbtc8723b2ant_FwDacSwingLvl(pBtCoexist, NORMAL_EXEC, 6);
	halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 0);

	/*  sw all off */
	halbtc8723b2ant_SwMechanism1(pBtCoexist, false, false, false, false);
	halbtc8723b2ant_SwMechanism2(pBtCoexist, false, false, false, 0x18);

	/*  hw all off */
	/* pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1, 0xfffff, 0x0); */
	halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 0);
}

static void halbtc8723b2ant_InitCoexDm(struct BTC_COEXIST *pBtCoexist)
{
	/*  force to reset coex mechanism */

	halbtc8723b2ant_PsTdma(pBtCoexist, FORCE_EXEC, false, 1);
	halbtc8723b2ant_FwDacSwingLvl(pBtCoexist, FORCE_EXEC, 6);
	halbtc8723b2ant_DecBtPwr(pBtCoexist, FORCE_EXEC, 0);

	halbtc8723b2ant_SwMechanism1(pBtCoexist, false, false, false, false);
	halbtc8723b2ant_SwMechanism2(pBtCoexist, false, false, false, 0x18);
}

static void halbtc8723b2ant_ActionBtInquiry(struct BTC_COEXIST *pBtCoexist)
{
	bool bWifiConnected = false;
	bool bLowPwrDisable = true;

	pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_ACT_DISABLE_LOW_POWER, &bLowPwrDisable);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_CONNECTED, &bWifiConnected);

	if (bWifiConnected) {
		halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 7);
		halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, true, 3);
	} else {
		halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 0);
		halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, false, 1);
	}

	halbtc8723b2ant_FwDacSwingLvl(pBtCoexist, FORCE_EXEC, 6);
	halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 0);

	halbtc8723b2ant_SwMechanism1(pBtCoexist, false, false, false, false);
	halbtc8723b2ant_SwMechanism2(pBtCoexist, false, false, false, 0x18);

	pCoexDm->bNeedRecover0x948 = true;
	pCoexDm->backup0x948 = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x948);

	halbtc8723b2ant_SetAntPath(pBtCoexist, BTC_ANT_WIFI_AT_AUX, false, false);
}

static bool halbtc8723b2ant_IsCommonAction(struct BTC_COEXIST *pBtCoexist)
{
	u8 btRssiState = BTC_RSSI_STATE_HIGH;
	bool bCommon = false, bWifiConnected = false, bWifiBusy = false;
	bool bBtHsOn = false, bLowPwrDisable = false;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_HS_OPERATION, &bBtHsOn);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_CONNECTED, &bWifiConnected);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_BUSY, &bWifiBusy);

	if (!bWifiConnected) {
		bLowPwrDisable = false;
		pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_ACT_DISABLE_LOW_POWER, &bLowPwrDisable);
		halbtc8723b2ant_LimitedRx(pBtCoexist, NORMAL_EXEC, false, false, 0x8);

		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Wifi non-connected idle!!\n"));

		pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);
		halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 0);
		halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, false, 1);
		halbtc8723b2ant_FwDacSwingLvl(pBtCoexist, NORMAL_EXEC, 6);
		halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 0);

		halbtc8723b2ant_SwMechanism1(pBtCoexist, false, false, false, false);
		halbtc8723b2ant_SwMechanism2(pBtCoexist, false, false, false, 0x18);

		bCommon = true;
	} else {
		if (BT_8723B_2ANT_BT_STATUS_NON_CONNECTED_IDLE == pCoexDm->btStatus) {
			bLowPwrDisable = false;
			pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_ACT_DISABLE_LOW_POWER, &bLowPwrDisable);
			halbtc8723b2ant_LimitedRx(pBtCoexist, NORMAL_EXEC, false, false, 0x8);

			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Wifi connected + BT non connected-idle!!\n"));

			pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);
			halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 0);
			halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, false, 1);
			halbtc8723b2ant_FwDacSwingLvl(pBtCoexist, NORMAL_EXEC, 0xb);
			halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 0);

			halbtc8723b2ant_SwMechanism1(pBtCoexist, false, false, false, false);
			halbtc8723b2ant_SwMechanism2(pBtCoexist, false, false, false, 0x18);

			bCommon = true;
		} else if (BT_8723B_2ANT_BT_STATUS_CONNECTED_IDLE == pCoexDm->btStatus) {
			bLowPwrDisable = true;
			pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_ACT_DISABLE_LOW_POWER, &bLowPwrDisable);

			if (bBtHsOn)
				return false;

			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Wifi connected + BT connected-idle!!\n"));
			halbtc8723b2ant_LimitedRx(pBtCoexist, NORMAL_EXEC, false, false, 0x8);

			pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);
			halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 0);
			halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, false, 1);
			halbtc8723b2ant_FwDacSwingLvl(pBtCoexist, NORMAL_EXEC, 0xb);
			halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 0);

			halbtc8723b2ant_SwMechanism1(pBtCoexist, true, false, false, false);
			halbtc8723b2ant_SwMechanism2(pBtCoexist, false, false, false, 0x18);

			bCommon = true;
		} else {
			bLowPwrDisable = true;
			pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_ACT_DISABLE_LOW_POWER, &bLowPwrDisable);

			if (bWifiBusy) {
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Wifi Connected-Busy + BT Busy!!\n"));
				bCommon = false;
			} else {
				if (bBtHsOn)
					return false;

				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Wifi Connected-Idle + BT Busy!!\n"));
				btRssiState = halbtc8723b2ant_BtRssiState(2, 29, 0);
				halbtc8723b2ant_LimitedRx(pBtCoexist, NORMAL_EXEC, false, false, 0x8);

				pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);
				halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 7);
				halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, true, 21);
				halbtc8723b2ant_FwDacSwingLvl(pBtCoexist, NORMAL_EXEC, 0xb);

				if (BTC_RSSI_HIGH(btRssiState))
					halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 2);
				else
					halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 0);

				halbtc8723b2ant_SwMechanism1(pBtCoexist, false, false, false, false);
				halbtc8723b2ant_SwMechanism2(pBtCoexist, false, false, false, 0x18);
				bCommon = true;
			}
		}
	}

	return bCommon;
}

static void halbtc8723b2ant_TdmaDurationAdjust(
	struct BTC_COEXIST *pBtCoexist, bool bScoHid, bool bTxPause, u8 maxInterval
)
{
	static s32 up, dn, m, n, WaitCount;
	s32 result;   /* 0: no change, +1: increase WiFi duration, -1: decrease WiFi duration */
	u8 retryCount = 0;

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW, ("[BTCoex], TdmaDurationAdjust()\n"));

	if (!pCoexDm->bAutoTdmaAdjust) {
		pCoexDm->bAutoTdmaAdjust = true;
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], first run TdmaDurationAdjust()!!\n"));
		{
			if (bScoHid) {
				if (bTxPause) {
					if (maxInterval == 1)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(13);
					else if (maxInterval == 2)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(14);
					else
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(15);
				} else {
					if (maxInterval == 1)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(9);
					else if (maxInterval == 2)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(10);
					else
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(11);
				}
			} else {
				if (bTxPause) {
					if (maxInterval == 1)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(5);
					else if (maxInterval == 2)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(6);
					else
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(7);
				} else {
					if (maxInterval == 1)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(1);
					else if (maxInterval == 2)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(2);
					else
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(3);
				}
			}
		}
		/*  */
		up = 0;
		dn = 0;
		m = 1;
		n = 3;
		result = 0;
		WaitCount = 0;
	} else {
		/* acquire the BT TRx retry count from BT_Info byte2 */
		retryCount = pCoexSta->btRetryCnt;
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], retryCount = %d\n", retryCount));
		BTC_PRINT(
			BTC_MSG_ALGORITHM,
			ALGO_TRACE_FW_DETAIL,
			(
				"[BTCoex], up =%d, dn =%d, m =%d, n =%d, WaitCount =%d\n",
				up, dn, m, n, WaitCount
			)
		);
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
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], Increase wifi duration!!\n"));
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
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], Decrease wifi duration for retryCounter>3!!\n"));
		}

		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], max Interval = %d\n", maxInterval));
		if (maxInterval == 1) {
			if (bTxPause) {
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], TxPause = 1\n"));

				if (pCoexDm->curPsTdma == 71)
					HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(5);
				else if (pCoexDm->curPsTdma == 1)
					HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(5);
				else if (pCoexDm->curPsTdma == 2)
					HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(6);
				else if (pCoexDm->curPsTdma == 3)
					HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(7);
				else if (pCoexDm->curPsTdma == 4)
					HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(8);

				if (pCoexDm->curPsTdma == 9)
					HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(13);
				else if (pCoexDm->curPsTdma == 10)
					HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(14);
				else if (pCoexDm->curPsTdma == 11)
					HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(15);
				else if (pCoexDm->curPsTdma == 12)
					HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(16);

				if (result == -1) {
					if (pCoexDm->curPsTdma == 5)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(6);
					else if (pCoexDm->curPsTdma == 6)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(7);
					else if (pCoexDm->curPsTdma == 7)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(8);
					else if (pCoexDm->curPsTdma == 13)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(14);
					else if (pCoexDm->curPsTdma == 14)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(15);
					else if (pCoexDm->curPsTdma == 15)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(16);
				} else if (result == 1) {
					if (pCoexDm->curPsTdma == 8)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(7);
					else if (pCoexDm->curPsTdma == 7)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(6);
					else if (pCoexDm->curPsTdma == 6)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(5);
					else if (pCoexDm->curPsTdma == 16)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(15);
					else if (pCoexDm->curPsTdma == 15)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(14);
					else if (pCoexDm->curPsTdma == 14)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(13);
				}
			} else {
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], TxPause = 0\n"));
				if (pCoexDm->curPsTdma == 5)
					HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(71);
				else if (pCoexDm->curPsTdma == 6)
					HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(2);
				else if (pCoexDm->curPsTdma == 7)
					HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(3);
				else if (pCoexDm->curPsTdma == 8)
					HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(4);

				if (pCoexDm->curPsTdma == 13)
					HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(9);
				else if (pCoexDm->curPsTdma == 14)
					HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(10);
				else if (pCoexDm->curPsTdma == 15)
					HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(11);
				else if (pCoexDm->curPsTdma == 16)
					HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(12);

				if (result == -1) {
					if (pCoexDm->curPsTdma == 71)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(1);
					else if (pCoexDm->curPsTdma == 1)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(2);
					else if (pCoexDm->curPsTdma == 2)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(3);
					else if (pCoexDm->curPsTdma == 3)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(4);
					else if (pCoexDm->curPsTdma == 9)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(10);
					else if (pCoexDm->curPsTdma == 10)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(11);
					else if (pCoexDm->curPsTdma == 11)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(12);
				} else if (result == 1) {
					if (pCoexDm->curPsTdma == 4)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(3);
					else if (pCoexDm->curPsTdma == 3)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(2);
					else if (pCoexDm->curPsTdma == 2)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(1);
					else if (pCoexDm->curPsTdma == 1)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(71);
					else if (pCoexDm->curPsTdma == 12)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(11);
					else if (pCoexDm->curPsTdma == 11)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(10);
					else if (pCoexDm->curPsTdma == 10)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(9);
				}
			}
		} else if (maxInterval == 2) {
			if (bTxPause) {
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], TxPause = 1\n"));
				if (pCoexDm->curPsTdma == 1)
					HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(6);
				else if (pCoexDm->curPsTdma == 2)
					HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(6);
				else if (pCoexDm->curPsTdma == 3)
					HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(7);
				else if (pCoexDm->curPsTdma == 4)
					HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(8);

				if (pCoexDm->curPsTdma == 9)
					HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(14);
				else if (pCoexDm->curPsTdma == 10)
					HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(14);
				else if (pCoexDm->curPsTdma == 11)
					HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(15);
				else if (pCoexDm->curPsTdma == 12)
					HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(16);

				if (result == -1) {
					if (pCoexDm->curPsTdma == 5)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(6);
					else if (pCoexDm->curPsTdma == 6)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(7);
					else if (pCoexDm->curPsTdma == 7)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(8);
					else if (pCoexDm->curPsTdma == 13)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(14);
					else if (pCoexDm->curPsTdma == 14)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(15);
					else if (pCoexDm->curPsTdma == 15)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(16);
				} else if (result == 1) {
					if (pCoexDm->curPsTdma == 8)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(7);
					else if (pCoexDm->curPsTdma == 7)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(6);
					else if (pCoexDm->curPsTdma == 6)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(6);
					else if (pCoexDm->curPsTdma == 16)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(15);
					else if (pCoexDm->curPsTdma == 15)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(14);
					else if (pCoexDm->curPsTdma == 14)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(14);
				}
			} else {
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], TxPause = 0\n"));
				if (pCoexDm->curPsTdma == 5)
					HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(2);
				else if (pCoexDm->curPsTdma == 6)
					HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(2);
				else if (pCoexDm->curPsTdma == 7)
					HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(3);
				else if (pCoexDm->curPsTdma == 8)
					HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(4);

				if (pCoexDm->curPsTdma == 13)
					HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(10);
				else if (pCoexDm->curPsTdma == 14)
					HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(10);
				else if (pCoexDm->curPsTdma == 15)
					HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(11);
				else if (pCoexDm->curPsTdma == 16)
					HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(12);

				if (result == -1) {
					if (pCoexDm->curPsTdma == 1)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(2);
					else if (pCoexDm->curPsTdma == 2)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(3);
					else if (pCoexDm->curPsTdma == 3)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(4);
					else if (pCoexDm->curPsTdma == 9)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(10);
					else if (pCoexDm->curPsTdma == 10)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(11);
					else if (pCoexDm->curPsTdma == 11)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(12);
				} else if (result == 1) {
					if (pCoexDm->curPsTdma == 4)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(3);
					else if (pCoexDm->curPsTdma == 3)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(2);
					else if (pCoexDm->curPsTdma == 2)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(2);
					else if (pCoexDm->curPsTdma == 12)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(11);
					else if (pCoexDm->curPsTdma == 11)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(10);
					else if (pCoexDm->curPsTdma == 10)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(10);
				}
			}
		} else if (maxInterval == 3) {
			if (bTxPause) {
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], TxPause = 1\n"));
				if (pCoexDm->curPsTdma == 1)
					HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(7);
				else if (pCoexDm->curPsTdma == 2)
					HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(7);
				else if (pCoexDm->curPsTdma == 3)
					HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(7);
				else if (pCoexDm->curPsTdma == 4)
					HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(8);

				if (pCoexDm->curPsTdma == 9)
					HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(15);
				else if (pCoexDm->curPsTdma == 10)
					HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(15);
				else if (pCoexDm->curPsTdma == 11)
					HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(15);
				else if (pCoexDm->curPsTdma == 12)
					HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(16);

				if (result == -1) {
					if (pCoexDm->curPsTdma == 5)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(7);
					else if (pCoexDm->curPsTdma == 6)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(7);
					else if (pCoexDm->curPsTdma == 7)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(8);
					else if (pCoexDm->curPsTdma == 13)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(15);
					else if (pCoexDm->curPsTdma == 14)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(15);
					else if (pCoexDm->curPsTdma == 15)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(16);
				} else if (result == 1) {
					if (pCoexDm->curPsTdma == 8)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(7);
					else if (pCoexDm->curPsTdma == 7)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(7);
					else if (pCoexDm->curPsTdma == 6)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(7);
					else if (pCoexDm->curPsTdma == 16)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(15);
					else if (pCoexDm->curPsTdma == 15)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(15);
					else if (pCoexDm->curPsTdma == 14)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(15);
				}
			} else {
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], TxPause = 0\n"));
				if (pCoexDm->curPsTdma == 5)
					HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(3);
				else if (pCoexDm->curPsTdma == 6)
					HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(3);
				else if (pCoexDm->curPsTdma == 7)
					HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(3);
				else if (pCoexDm->curPsTdma == 8)
					HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(4);

				if (pCoexDm->curPsTdma == 13)
					HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(11);
				else if (pCoexDm->curPsTdma == 14)
					HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(11);
				else if (pCoexDm->curPsTdma == 15)
					HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(11);
				else if (pCoexDm->curPsTdma == 16)
					HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(12);

				if (result == -1) {
					if (pCoexDm->curPsTdma == 1)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(3);
					else if (pCoexDm->curPsTdma == 2)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(3);
					else if (pCoexDm->curPsTdma == 3)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(4);
					else if (pCoexDm->curPsTdma == 9)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(11);
					else if (pCoexDm->curPsTdma == 10)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(11);
					else if (pCoexDm->curPsTdma == 11)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(12);
				} else if (result == 1) {
					if (pCoexDm->curPsTdma == 4)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(3);
					else if (pCoexDm->curPsTdma == 3)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(3);
					else if (pCoexDm->curPsTdma == 2)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(3);
					else if (pCoexDm->curPsTdma == 12)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(11);
					else if (pCoexDm->curPsTdma == 11)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(11);
					else if (pCoexDm->curPsTdma == 10)
						HAL_BTC8723B2ANT_DMA_DURATION_ADJUST(11);
				}
			}
		}
	}

	/*  if current PsTdma not match with the recorded one (when scan, dhcp...), */
	/*  then we have to adjust it back to the previous record one. */
	if (pCoexDm->curPsTdma != pCoexDm->psTdmaDuAdjType) {
		bool bScan = false, bLink = false, bRoam = false;
		BTC_PRINT(
			BTC_MSG_ALGORITHM,
			ALGO_TRACE_FW_DETAIL,
			(
				"[BTCoex], PsTdma type mismatch!!!, curPsTdma =%d, recordPsTdma =%d\n",
				pCoexDm->curPsTdma,
				pCoexDm->psTdmaDuAdjType
			)
		);

		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_SCAN, &bScan);
		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_LINK, &bLink);
		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_ROAM, &bRoam);

		if (!bScan && !bLink && !bRoam)
			halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, true, pCoexDm->psTdmaDuAdjType);
		else {
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], roaming/link/scan is under progress, will adjust next time!!!\n"));
		}
	}
}

/*  SCO only or SCO+PAN(HS) */
static void halbtc8723b2ant_ActionSco(struct BTC_COEXIST *pBtCoexist)
{
	u8 wifiRssiState, btRssiState;
	u32 wifiBw;

	wifiRssiState = halbtc8723b2ant_WifiRssiState(pBtCoexist, 0, 2, 15, 0);
	btRssiState = halbtc8723b2ant_BtRssiState(2, 29, 0);

	pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);

	halbtc8723b2ant_LimitedRx(pBtCoexist, NORMAL_EXEC, false, false, 0x8);

	halbtc8723b2ant_FwDacSwingLvl(pBtCoexist, NORMAL_EXEC, 4);

	if (BTC_RSSI_HIGH(btRssiState))
		halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 2);
	else
		halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 0);

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_BW, &wifiBw);

	if (BTC_WIFI_BW_LEGACY == wifiBw) /* for SCO quality at 11b/g mode */
		halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 2);
	else  /* for SCO quality & wifi performance balance at 11n mode */
		halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 8);

	halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, false, 0); /* for voice quality */

	/*  sw mechanism */
	if (BTC_WIFI_BW_HT40 == wifiBw) {
		if (
			(wifiRssiState == BTC_RSSI_STATE_HIGH) ||
			(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH)
		) {
			halbtc8723b2ant_SwMechanism1(pBtCoexist, true, true, false, false);
			halbtc8723b2ant_SwMechanism2(pBtCoexist, true, false, true, 0x4);
		} else {
			halbtc8723b2ant_SwMechanism1(pBtCoexist, true, true, false, false);
			halbtc8723b2ant_SwMechanism2(pBtCoexist, false, false, true, 0x4);
		}
	} else {
		if (
			(wifiRssiState == BTC_RSSI_STATE_HIGH) ||
			(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH)
		) {
			halbtc8723b2ant_SwMechanism1(pBtCoexist, false, true, false, false);
			halbtc8723b2ant_SwMechanism2(pBtCoexist, true, false, true, 0x4);
		} else {
			halbtc8723b2ant_SwMechanism1(pBtCoexist, false, true, false, false);
			halbtc8723b2ant_SwMechanism2(pBtCoexist, false, false, true, 0x4);
		}
	}
}


static void halbtc8723b2ant_ActionHid(struct BTC_COEXIST *pBtCoexist)
{
	u8 wifiRssiState, btRssiState;
	u32 wifiBw;

	wifiRssiState = halbtc8723b2ant_WifiRssiState(pBtCoexist, 0, 2, 15, 0);
	btRssiState = halbtc8723b2ant_BtRssiState(2, 29, 0);

	pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);

	halbtc8723b2ant_LimitedRx(pBtCoexist, NORMAL_EXEC, false, false, 0x8);

	halbtc8723b2ant_FwDacSwingLvl(pBtCoexist, NORMAL_EXEC, 6);

	if (BTC_RSSI_HIGH(btRssiState))
		halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 2);
	else
		halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 0);

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_BW, &wifiBw);

	if (BTC_WIFI_BW_LEGACY == wifiBw) /* for HID at 11b/g mode */
		halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 7);
	else  /* for HID quality & wifi performance balance at 11n mode */
		halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 9);

	if (
		(btRssiState == BTC_RSSI_STATE_HIGH) ||
		(btRssiState == BTC_RSSI_STATE_STAY_HIGH)
	)
		halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, true, 9);
	else
		halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, true, 13);

	/*  sw mechanism */
	if (BTC_WIFI_BW_HT40 == wifiBw) {
		if (
			(wifiRssiState == BTC_RSSI_STATE_HIGH) ||
			(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH)
		) {
			halbtc8723b2ant_SwMechanism1(pBtCoexist, true, true, false, false);
			halbtc8723b2ant_SwMechanism2(pBtCoexist, true, false, false, 0x18);
		} else {
			halbtc8723b2ant_SwMechanism1(pBtCoexist, true, true, false, false);
			halbtc8723b2ant_SwMechanism2(pBtCoexist, false, false, false, 0x18);
		}
	} else {
		if (
			(wifiRssiState == BTC_RSSI_STATE_HIGH) ||
			(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH)
		) {
			halbtc8723b2ant_SwMechanism1(pBtCoexist, false, true, false, false);
			halbtc8723b2ant_SwMechanism2(pBtCoexist, true, false, false, 0x18);
		} else {
			halbtc8723b2ant_SwMechanism1(pBtCoexist, false, true, false, false);
			halbtc8723b2ant_SwMechanism2(pBtCoexist, false, false, false, 0x18);
		}
	}
}

/* A2DP only / PAN(EDR) only/ A2DP+PAN(HS) */
static void halbtc8723b2ant_ActionA2dp(struct BTC_COEXIST *pBtCoexist)
{
	u8 wifiRssiState, wifiRssiState1, btRssiState;
	u32 wifiBw;
	u8 apNum = 0;

	wifiRssiState = halbtc8723b2ant_WifiRssiState(pBtCoexist, 0, 2, 15, 0);
	wifiRssiState1 = halbtc8723b2ant_WifiRssiState(pBtCoexist, 1, 2, 40, 0);
	btRssiState = halbtc8723b2ant_BtRssiState(2, 29, 0);

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U1_AP_NUM, &apNum);

	/*  define the office environment */
	if (apNum >= 10 && BTC_RSSI_HIGH(wifiRssiState1)) {
		/* DbgPrint(" AP#>10(%d)\n", apNum); */
		pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);
		halbtc8723b2ant_LimitedRx(pBtCoexist, NORMAL_EXEC, false, false, 0x8);
		halbtc8723b2ant_FwDacSwingLvl(pBtCoexist, NORMAL_EXEC, 6);
		halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 0);
		halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 0);
		halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, false, 1);

		/*  sw mechanism */
		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_BW, &wifiBw);
		if (BTC_WIFI_BW_HT40 == wifiBw) {
			halbtc8723b2ant_SwMechanism1(pBtCoexist, true, false, false, false);
			halbtc8723b2ant_SwMechanism2(pBtCoexist, true, false, true, 0x18);
		} else {
			halbtc8723b2ant_SwMechanism1(pBtCoexist, false, false, false, false);
			halbtc8723b2ant_SwMechanism2(pBtCoexist, true, false, true, 0x18);
		}
		return;
	}

	pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);
	halbtc8723b2ant_LimitedRx(pBtCoexist, NORMAL_EXEC, false, false, 0x8);

	halbtc8723b2ant_FwDacSwingLvl(pBtCoexist, NORMAL_EXEC, 6);

	if (BTC_RSSI_HIGH(btRssiState))
		halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 2);
	else
		halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 0);

	halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 7);

	if (
		(btRssiState == BTC_RSSI_STATE_HIGH) ||
		(btRssiState == BTC_RSSI_STATE_STAY_HIGH)
	)
		halbtc8723b2ant_TdmaDurationAdjust(pBtCoexist, false, false, 1);
	else
		halbtc8723b2ant_TdmaDurationAdjust(pBtCoexist, false, true, 1);

	/*  sw mechanism */
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_BW, &wifiBw);
	if (BTC_WIFI_BW_HT40 == wifiBw) {
		if (
			(wifiRssiState == BTC_RSSI_STATE_HIGH) ||
			(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH)
		) {
			halbtc8723b2ant_SwMechanism1(pBtCoexist, true, false, false, false);
			halbtc8723b2ant_SwMechanism2(pBtCoexist, true, false, false, 0x18);
		} else {
			halbtc8723b2ant_SwMechanism1(pBtCoexist, true, false, false, false);
			halbtc8723b2ant_SwMechanism2(pBtCoexist, false, false, false, 0x18);
		}
	} else {
		if (
			(wifiRssiState == BTC_RSSI_STATE_HIGH) ||
			(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH)
		) {
			halbtc8723b2ant_SwMechanism1(pBtCoexist, false, false, false, false);
			halbtc8723b2ant_SwMechanism2(pBtCoexist, true, false, false, 0x18);
		} else {
			halbtc8723b2ant_SwMechanism1(pBtCoexist, false, false, false, false);
			halbtc8723b2ant_SwMechanism2(pBtCoexist, false, false, false, 0x18);
		}
	}
}

static void halbtc8723b2ant_ActionA2dpPanHs(struct BTC_COEXIST *pBtCoexist)
{
	u8 wifiRssiState, btRssiState;
	u32 wifiBw;

	wifiRssiState = halbtc8723b2ant_WifiRssiState(pBtCoexist, 0, 2, 15, 0);
	btRssiState = halbtc8723b2ant_BtRssiState(2, 29, 0);

	pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);

	halbtc8723b2ant_LimitedRx(pBtCoexist, NORMAL_EXEC, false, false, 0x8);

	halbtc8723b2ant_FwDacSwingLvl(pBtCoexist, NORMAL_EXEC, 6);

	if (BTC_RSSI_HIGH(btRssiState))
		halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 2);
	else
		halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 0);

	halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 7);

	halbtc8723b2ant_TdmaDurationAdjust(pBtCoexist, false, true, 2);

	/*  sw mechanism */
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_BW, &wifiBw);
	if (BTC_WIFI_BW_HT40 == wifiBw) {
		if (
			(wifiRssiState == BTC_RSSI_STATE_HIGH) ||
			(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH)
		) {
			halbtc8723b2ant_SwMechanism1(pBtCoexist, true, false, false, false);
			halbtc8723b2ant_SwMechanism2(pBtCoexist, true, false, false, 0x18);
		} else {
			halbtc8723b2ant_SwMechanism1(pBtCoexist, true, false, false, false);
			halbtc8723b2ant_SwMechanism2(pBtCoexist, false, false, false, 0x18);
		}
	} else {
		if (
			(wifiRssiState == BTC_RSSI_STATE_HIGH) ||
			(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH)
		) {
			halbtc8723b2ant_SwMechanism1(pBtCoexist, false, false, false, false);
			halbtc8723b2ant_SwMechanism2(pBtCoexist, true, false, false, 0x18);
		} else {
			halbtc8723b2ant_SwMechanism1(pBtCoexist, false, false, false, false);
			halbtc8723b2ant_SwMechanism2(pBtCoexist, false, false, false, 0x18);
		}
	}
}

static void halbtc8723b2ant_ActionPanEdr(struct BTC_COEXIST *pBtCoexist)
{
	u8 wifiRssiState, btRssiState;
	u32 wifiBw;

	wifiRssiState = halbtc8723b2ant_WifiRssiState(pBtCoexist, 0, 2, 15, 0);
	btRssiState = halbtc8723b2ant_BtRssiState(2, 29, 0);

	pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);

	halbtc8723b2ant_LimitedRx(pBtCoexist, NORMAL_EXEC, false, false, 0x8);

	halbtc8723b2ant_FwDacSwingLvl(pBtCoexist, NORMAL_EXEC, 6);

	if (BTC_RSSI_HIGH(btRssiState))
		halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 2);
	else
		halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 0);

	halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 10);

	if (
		(btRssiState == BTC_RSSI_STATE_HIGH) ||
		(btRssiState == BTC_RSSI_STATE_STAY_HIGH)
	)
		halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, true, 1);
	else
		halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, true, 5);

	/*  sw mechanism */
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_BW, &wifiBw);
	if (BTC_WIFI_BW_HT40 == wifiBw) {
		if (
			(wifiRssiState == BTC_RSSI_STATE_HIGH) ||
			(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH)
		) {
			halbtc8723b2ant_SwMechanism1(pBtCoexist, true, false, false, false);
			halbtc8723b2ant_SwMechanism2(pBtCoexist, true, false, false, 0x18);
		} else {
			halbtc8723b2ant_SwMechanism1(pBtCoexist, true, false, false, false);
			halbtc8723b2ant_SwMechanism2(pBtCoexist, false, false, false, 0x18);
		}
	} else {
		if (
			(wifiRssiState == BTC_RSSI_STATE_HIGH) ||
			(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH)
		) {
			halbtc8723b2ant_SwMechanism1(pBtCoexist, false, false, false, false);
			halbtc8723b2ant_SwMechanism2(pBtCoexist, true, false, false, 0x18);
		} else {
			halbtc8723b2ant_SwMechanism1(pBtCoexist, false, false, false, false);
			halbtc8723b2ant_SwMechanism2(pBtCoexist, false, false, false, 0x18);
		}
	}
}


/* PAN(HS) only */
static void halbtc8723b2ant_ActionPanHs(struct BTC_COEXIST *pBtCoexist)
{
	u8 wifiRssiState, btRssiState;
	u32 wifiBw;

	wifiRssiState = halbtc8723b2ant_WifiRssiState(pBtCoexist, 0, 2, 15, 0);
	btRssiState = halbtc8723b2ant_BtRssiState(2, 29, 0);

	pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);

	halbtc8723b2ant_LimitedRx(pBtCoexist, NORMAL_EXEC, false, false, 0x8);

	halbtc8723b2ant_FwDacSwingLvl(pBtCoexist, NORMAL_EXEC, 6);

	if (BTC_RSSI_HIGH(btRssiState))
		halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 2);
	else
		halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 0);

	halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 7);

	halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, false, 1);

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_BW, &wifiBw);
	if (BTC_WIFI_BW_HT40 == wifiBw) {
		if (
			(wifiRssiState == BTC_RSSI_STATE_HIGH) ||
			(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH)
		) {
			halbtc8723b2ant_SwMechanism1(pBtCoexist, true, false, false, false);
			halbtc8723b2ant_SwMechanism2(pBtCoexist, true, false, false, 0x18);
		} else {
			halbtc8723b2ant_SwMechanism1(pBtCoexist, true, false, false, false);
			halbtc8723b2ant_SwMechanism2(pBtCoexist, false, false, false, 0x18);
		}
	} else {
		if (
			(wifiRssiState == BTC_RSSI_STATE_HIGH) ||
			(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH)
		) {
			halbtc8723b2ant_SwMechanism1(pBtCoexist, false, false, false, false);
			halbtc8723b2ant_SwMechanism2(pBtCoexist, true, false, false, 0x18);
		} else {
			halbtc8723b2ant_SwMechanism1(pBtCoexist, false, false, false, false);
			halbtc8723b2ant_SwMechanism2(pBtCoexist, false, false, false, 0x18);
		}
	}
}

/* PAN(EDR)+A2DP */
static void halbtc8723b2ant_ActionPanEdrA2dp(struct BTC_COEXIST *pBtCoexist)
{
	u8 wifiRssiState, btRssiState;
	u32 wifiBw;

	wifiRssiState = halbtc8723b2ant_WifiRssiState(pBtCoexist, 0, 2, 15, 0);
	btRssiState = halbtc8723b2ant_BtRssiState(2, 29, 0);

	pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);

	halbtc8723b2ant_LimitedRx(pBtCoexist, NORMAL_EXEC, false, false, 0x8);

	halbtc8723b2ant_FwDacSwingLvl(pBtCoexist, NORMAL_EXEC, 6);

	if (BTC_RSSI_HIGH(btRssiState))
		halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 2);
	else
		halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 0);

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_BW, &wifiBw);

	if (
		(btRssiState == BTC_RSSI_STATE_HIGH) ||
		(btRssiState == BTC_RSSI_STATE_STAY_HIGH)
	) {
		halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 12);
		if (BTC_WIFI_BW_HT40 == wifiBw)
			halbtc8723b2ant_TdmaDurationAdjust(pBtCoexist, false, true, 3);
		else
			halbtc8723b2ant_TdmaDurationAdjust(pBtCoexist, false, false, 3);
	} else {
		halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 7);
		halbtc8723b2ant_TdmaDurationAdjust(pBtCoexist, false, true, 3);
	}

	/*  sw mechanism */
	if (BTC_WIFI_BW_HT40 == wifiBw) {
		if (
			(wifiRssiState == BTC_RSSI_STATE_HIGH) ||
			(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH)
		) {
			halbtc8723b2ant_SwMechanism1(pBtCoexist, true, false, false, false);
			halbtc8723b2ant_SwMechanism2(pBtCoexist, true, false, false, 0x18);
		} else {
			halbtc8723b2ant_SwMechanism1(pBtCoexist, true, false, false, false);
			halbtc8723b2ant_SwMechanism2(pBtCoexist, false, false, false, 0x18);
		}
	} else {
		if (
			(wifiRssiState == BTC_RSSI_STATE_HIGH) ||
			(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH)
		) {
			halbtc8723b2ant_SwMechanism1(pBtCoexist, false, false, false, false);
			halbtc8723b2ant_SwMechanism2(pBtCoexist, true, false, false, 0x18);
		} else {
			halbtc8723b2ant_SwMechanism1(pBtCoexist, false, false, false, false);
			halbtc8723b2ant_SwMechanism2(pBtCoexist, false, false, false, 0x18);
		}
	}
}

static void halbtc8723b2ant_ActionPanEdrHid(struct BTC_COEXIST *pBtCoexist)
{
	u8 wifiRssiState, btRssiState;
	u32 wifiBw;

	wifiRssiState = halbtc8723b2ant_WifiRssiState(pBtCoexist, 0, 2, 15, 0);
	btRssiState = halbtc8723b2ant_BtRssiState(2, 29, 0);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_BW, &wifiBw);

	halbtc8723b2ant_LimitedRx(pBtCoexist, NORMAL_EXEC, false, false, 0x8);

	if (BTC_RSSI_HIGH(btRssiState))
		halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 2);
	else
		halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 0);

	if (
		(btRssiState == BTC_RSSI_STATE_HIGH) ||
		(btRssiState == BTC_RSSI_STATE_STAY_HIGH)
	) {
		if (BTC_WIFI_BW_HT40 == wifiBw) {
			halbtc8723b2ant_FwDacSwingLvl(pBtCoexist, NORMAL_EXEC, 3);
			halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 11);
			pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1, 0xfffff, 0x780);
		} else {
			halbtc8723b2ant_FwDacSwingLvl(pBtCoexist, NORMAL_EXEC, 6);
			halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 7);
			pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);
		}
		halbtc8723b2ant_TdmaDurationAdjust(pBtCoexist, true, false, 2);
	} else {
		halbtc8723b2ant_FwDacSwingLvl(pBtCoexist, NORMAL_EXEC, 6);
		halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 11);
		pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);
		halbtc8723b2ant_TdmaDurationAdjust(pBtCoexist, true, true, 2);
	}

	/*  sw mechanism */
	if (BTC_WIFI_BW_HT40 == wifiBw) {
		if (
			(wifiRssiState == BTC_RSSI_STATE_HIGH) ||
			(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH)
		) {
			halbtc8723b2ant_SwMechanism1(pBtCoexist, true, true, false, false);
			halbtc8723b2ant_SwMechanism2(pBtCoexist, true, false, false, 0x18);
		} else {
			halbtc8723b2ant_SwMechanism1(pBtCoexist, true, true, false, false);
			halbtc8723b2ant_SwMechanism2(pBtCoexist, false, false, false, 0x18);
		}
	} else {
		if (
			(wifiRssiState == BTC_RSSI_STATE_HIGH) ||
			(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH)
		) {
			halbtc8723b2ant_SwMechanism1(pBtCoexist, false, true, false, false);
			halbtc8723b2ant_SwMechanism2(pBtCoexist, true, false, false, 0x18);
		} else {
			halbtc8723b2ant_SwMechanism1(pBtCoexist, false, true, false, false);
			halbtc8723b2ant_SwMechanism2(pBtCoexist, false, false, false, 0x18);
		}
	}
}

/*  HID+A2DP+PAN(EDR) */
static void halbtc8723b2ant_ActionHidA2dpPanEdr(struct BTC_COEXIST *pBtCoexist)
{
	u8 wifiRssiState, btRssiState;
	u32 wifiBw;

	wifiRssiState = halbtc8723b2ant_WifiRssiState(pBtCoexist, 0, 2, 15, 0);
	btRssiState = halbtc8723b2ant_BtRssiState(2, 29, 0);

	pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);

	halbtc8723b2ant_LimitedRx(pBtCoexist, NORMAL_EXEC, false, false, 0x8);

	halbtc8723b2ant_FwDacSwingLvl(pBtCoexist, NORMAL_EXEC, 6);

	if (BTC_RSSI_HIGH(btRssiState))
		halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 2);
	else
		halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 0);

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_BW, &wifiBw);

	halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 7);

	if (
		(btRssiState == BTC_RSSI_STATE_HIGH) ||
		(btRssiState == BTC_RSSI_STATE_STAY_HIGH)
	) {
		if (BTC_WIFI_BW_HT40 == wifiBw)
			halbtc8723b2ant_TdmaDurationAdjust(pBtCoexist, true, true, 2);
		else
			halbtc8723b2ant_TdmaDurationAdjust(pBtCoexist, true, false, 3);
	} else
		halbtc8723b2ant_TdmaDurationAdjust(pBtCoexist, true, true, 3);

	/*  sw mechanism */
	if (BTC_WIFI_BW_HT40 == wifiBw) {
		if (
			(wifiRssiState == BTC_RSSI_STATE_HIGH) ||
			(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH)
		) {
			halbtc8723b2ant_SwMechanism1(pBtCoexist, true, true, false, false);
			halbtc8723b2ant_SwMechanism2(pBtCoexist, true, false, false, 0x18);
		} else {
			halbtc8723b2ant_SwMechanism1(pBtCoexist, true, true, false, false);
			halbtc8723b2ant_SwMechanism2(pBtCoexist, false, false, false, 0x18);
		}
	} else {
		if (
			(wifiRssiState == BTC_RSSI_STATE_HIGH) ||
			(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH)
		) {
			halbtc8723b2ant_SwMechanism1(pBtCoexist, false, true, false, false);
			halbtc8723b2ant_SwMechanism2(pBtCoexist, true, false, false, 0x18);
		} else {
			halbtc8723b2ant_SwMechanism1(pBtCoexist, false, true, false, false);
			halbtc8723b2ant_SwMechanism2(pBtCoexist, false, false, false, 0x18);
		}
	}
}

static void halbtc8723b2ant_ActionHidA2dp(struct BTC_COEXIST *pBtCoexist)
{
	u8 wifiRssiState, btRssiState;
	u32 wifiBw;
	u8 apNum = 0;

	wifiRssiState = halbtc8723b2ant_WifiRssiState(pBtCoexist, 0, 2, 15, 0);
	/* btRssiState = halbtc8723b2ant_BtRssiState(2, 29, 0); */
	btRssiState = halbtc8723b2ant_BtRssiState(3, 29, 37);

	pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);

	halbtc8723b2ant_LimitedRx(pBtCoexist, NORMAL_EXEC, false, true, 0x5);

	halbtc8723b2ant_FwDacSwingLvl(pBtCoexist, NORMAL_EXEC, 6);

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_BW, &wifiBw);
	if (BTC_WIFI_BW_LEGACY == wifiBw) {
		if (BTC_RSSI_HIGH(btRssiState))
			halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 2);
		else if (BTC_RSSI_MEDIUM(btRssiState))
			halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 2);
		else
			halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 0);
	} else {
		/*  only 802.11N mode we have to dec bt power to 4 degree */
		if (BTC_RSSI_HIGH(btRssiState)) {
			pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U1_AP_NUM, &apNum);
			/*  need to check ap Number of Not */
			if (apNum < 10)
				halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 4);
			else
				halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 2);
		} else if (BTC_RSSI_MEDIUM(btRssiState))
			halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 2);
		else
			halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 0);
	}

	halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 7);

	if (
		(btRssiState == BTC_RSSI_STATE_HIGH) ||
		(btRssiState == BTC_RSSI_STATE_STAY_HIGH)
	)
		halbtc8723b2ant_TdmaDurationAdjust(pBtCoexist, true, false, 2);
	else
		halbtc8723b2ant_TdmaDurationAdjust(pBtCoexist, true, true, 2);

	/*  sw mechanism */
	if (BTC_WIFI_BW_HT40 == wifiBw) {
		if (
			(wifiRssiState == BTC_RSSI_STATE_HIGH) ||
			(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH)
		) {
			halbtc8723b2ant_SwMechanism1(pBtCoexist, true, true, false, false);
			halbtc8723b2ant_SwMechanism2(pBtCoexist, true, false, false, 0x18);
		} else {
			halbtc8723b2ant_SwMechanism1(pBtCoexist, true, true, false, false);
			halbtc8723b2ant_SwMechanism2(pBtCoexist, false, false, false, 0x18);
		}
	} else {
		if (
			(wifiRssiState == BTC_RSSI_STATE_HIGH) ||
			(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH)
		) {
			halbtc8723b2ant_SwMechanism1(pBtCoexist, false, true, false, false);
			halbtc8723b2ant_SwMechanism2(pBtCoexist, true, false, false, 0x18);
		} else {
			halbtc8723b2ant_SwMechanism1(pBtCoexist, false, true, false, false);
			halbtc8723b2ant_SwMechanism2(pBtCoexist, false, false, false, 0x18);
		}
	}
}

static void halbtc8723b2ant_RunCoexistMechanism(struct BTC_COEXIST *pBtCoexist)
{
	u8 algorithm = 0;

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], RunCoexistMechanism() ===>\n"));

	if (pBtCoexist->bManualControl) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], RunCoexistMechanism(), return for Manual CTRL <===\n"));
		return;
	}

	if (pCoexSta->bUnderIps) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], wifi is under IPS !!!\n"));
		return;
	}

	algorithm = halbtc8723b2ant_ActionAlgorithm(pBtCoexist);
	if (pCoexSta->bC2hBtInquiryPage && (BT_8723B_2ANT_COEX_ALGO_PANHS != algorithm)) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BT is under inquiry/page scan !!\n"));
		halbtc8723b2ant_ActionBtInquiry(pBtCoexist);
		return;
	} else {
		if (pCoexDm->bNeedRecover0x948) {
			pCoexDm->bNeedRecover0x948 = false;
			pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x948, pCoexDm->backup0x948);
		}
	}

	pCoexDm->curAlgorithm = algorithm;
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Algorithm = %d\n", pCoexDm->curAlgorithm));

	if (halbtc8723b2ant_IsCommonAction(pBtCoexist)) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action 2-Ant common.\n"));
		pCoexDm->bAutoTdmaAdjust = false;
	} else {
		if (pCoexDm->curAlgorithm != pCoexDm->preAlgorithm) {
			BTC_PRINT(
				BTC_MSG_ALGORITHM,
				ALGO_TRACE,
				(
					"[BTCoex], preAlgorithm =%d, curAlgorithm =%d\n",
					pCoexDm->preAlgorithm,
					pCoexDm->curAlgorithm
				)
			);
			pCoexDm->bAutoTdmaAdjust = false;
		}


		switch (pCoexDm->curAlgorithm) {
		case BT_8723B_2ANT_COEX_ALGO_SCO:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action 2-Ant, algorithm = SCO.\n"));
			halbtc8723b2ant_ActionSco(pBtCoexist);
			break;
		case BT_8723B_2ANT_COEX_ALGO_HID:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action 2-Ant, algorithm = HID.\n"));
			halbtc8723b2ant_ActionHid(pBtCoexist);
			break;
		case BT_8723B_2ANT_COEX_ALGO_A2DP:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action 2-Ant, algorithm = A2DP.\n"));
			halbtc8723b2ant_ActionA2dp(pBtCoexist);
			break;
		case BT_8723B_2ANT_COEX_ALGO_A2DP_PANHS:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action 2-Ant, algorithm = A2DP+PAN(HS).\n"));
			halbtc8723b2ant_ActionA2dpPanHs(pBtCoexist);
			break;
		case BT_8723B_2ANT_COEX_ALGO_PANEDR:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action 2-Ant, algorithm = PAN(EDR).\n"));
			halbtc8723b2ant_ActionPanEdr(pBtCoexist);
			break;
		case BT_8723B_2ANT_COEX_ALGO_PANHS:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action 2-Ant, algorithm = HS mode.\n"));
			halbtc8723b2ant_ActionPanHs(pBtCoexist);
			break;
		case BT_8723B_2ANT_COEX_ALGO_PANEDR_A2DP:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action 2-Ant, algorithm = PAN+A2DP.\n"));
			halbtc8723b2ant_ActionPanEdrA2dp(pBtCoexist);
			break;
		case BT_8723B_2ANT_COEX_ALGO_PANEDR_HID:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action 2-Ant, algorithm = PAN(EDR)+HID.\n"));
			halbtc8723b2ant_ActionPanEdrHid(pBtCoexist);
			break;
		case BT_8723B_2ANT_COEX_ALGO_HID_A2DP_PANEDR:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action 2-Ant, algorithm = HID+A2DP+PAN.\n"));
			halbtc8723b2ant_ActionHidA2dpPanEdr(pBtCoexist);
			break;
		case BT_8723B_2ANT_COEX_ALGO_HID_A2DP:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action 2-Ant, algorithm = HID+A2DP.\n"));
			halbtc8723b2ant_ActionHidA2dp(pBtCoexist);
			break;
		default:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action 2-Ant, algorithm = coexist All Off!!\n"));
			halbtc8723b2ant_CoexAllOff(pBtCoexist);
			break;
		}
		pCoexDm->preAlgorithm = pCoexDm->curAlgorithm;
	}
}

static void halbtc8723b2ant_WifiOffHwCfg(struct BTC_COEXIST *pBtCoexist)
{
	bool bIsInMpMode = false;
	u8 H2C_Parameter[2] = {0};
	u32 fwVer = 0;

	/*  set wlan_act to low */
	pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x76e, 0x4);

	pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1, 0xfffff, 0x780); /* WiFi goto standby while GNT_BT 0-->1 */
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_FW_VER, &fwVer);
	if (fwVer >= 0x180000) {
		/* Use H2C to set GNT_BT to HIGH */
		H2C_Parameter[0] = 1;
		pBtCoexist->fBtcFillH2c(pBtCoexist, 0x6E, 1, H2C_Parameter);
	} else
		pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x765, 0x18);

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_IS_IN_MP_MODE, &bIsInMpMode);
	if (!bIsInMpMode)
		pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0x67, 0x20, 0x0); /* BT select s0/s1 is controlled by BT */
	else
		pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0x67, 0x20, 0x1); /* BT select s0/s1 is controlled by WiFi */
}

static void halbtc8723b2ant_InitHwConfig(struct BTC_COEXIST *pBtCoexist, bool bBackUp)
{
	u8 u1Tmp = 0;

	BTC_PRINT(BTC_MSG_INTERFACE, INTF_INIT, ("[BTCoex], 2Ant Init HW Config!!\n"));

	/*  backup rf 0x1e value */
	pCoexDm->btRf0x1eBackup =
		pBtCoexist->fBtcGetRfReg(pBtCoexist, BTC_RF_A, 0x1e, 0xfffff);

	/*  0x790[5:0]= 0x5 */
	u1Tmp = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x790);
	u1Tmp &= 0xc0;
	u1Tmp |= 0x5;
	pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x790, u1Tmp);

	/* Antenna config */
	halbtc8723b2ant_SetAntPath(pBtCoexist, BTC_ANT_WIFI_AT_MAIN, true, false);

	/*  PTA parameter */
	halbtc8723b2ant_CoexTableWithType(pBtCoexist, FORCE_EXEC, 0);

	/*  Enable counter statistics */
	pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x76e, 0xc); /* 0x76e[3] = 1, WLAN_Act control by PTA */
	pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x778, 0x3);
	pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0x40, 0x20, 0x1);
}

/*  */
/*  work around function start with wa_halbtc8723b2ant_ */
/*  */
/*  */
/*  extern function start with EXhalbtc8723b2ant_ */
/*  */
void EXhalbtc8723b2ant_PowerOnSetting(struct BTC_COEXIST *pBtCoexist)
{
	struct BTC_BOARD_INFO *pBoardInfo = &pBtCoexist->boardInfo;
	u8 u1Tmp = 0x4; /* Set BIT2 by default since it's 2ant case */
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

void EXhalbtc8723b2ant_InitHwConfig(struct BTC_COEXIST *pBtCoexist, bool bWifiOnly)
{
	halbtc8723b2ant_InitHwConfig(pBtCoexist, true);
}

void EXhalbtc8723b2ant_InitCoexDm(struct BTC_COEXIST *pBtCoexist)
{
	BTC_PRINT(BTC_MSG_INTERFACE, INTF_INIT, ("[BTCoex], Coex Mechanism Init!!\n"));

	halbtc8723b2ant_InitCoexDm(pBtCoexist);
}

void EXhalbtc8723b2ant_DisplayCoexInfo(struct BTC_COEXIST *pBtCoexist)
{
	struct BTC_BOARD_INFO *pBoardInfo = &pBtCoexist->boardInfo;
	struct BTC_STACK_INFO *pStackInfo = &pBtCoexist->stackInfo;
	struct BTC_BT_LINK_INFO *pBtLinkInfo = &pBtCoexist->btLinkInfo;
	u8 *cliBuf = pBtCoexist->cliBuf;
	u8 u1Tmp[4], i, btInfoExt, psTdmaCase = 0;
	u32 u4Tmp[4];
	bool bRoam = false, bScan = false, bLink = false, bWifiUnder5G = false;
	bool bBtHsOn = false, bWifiBusy = false;
	s32 wifiRssi = 0, btHsRssi = 0;
	u32 wifiBw, wifiTrafficDir, faOfdm, faCck;
	u8 wifiDot11Chnl, wifiHsChnl;
	u32 fwVer = 0, btPatchVer = 0;
	u8 apNum = 0;

	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n ============[BT Coexist info]============");
	CL_PRINTF(cliBuf);

	if (pBtCoexist->bManualControl) {
		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n ============[Under Manual Control]============");
		CL_PRINTF(cliBuf);
		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n ==========================================");
		CL_PRINTF(cliBuf);
	}

	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = %d/ %d ", "Ant PG number/ Ant mechanism:", \
		pBoardInfo->pgAntNum,
		pBoardInfo->btdmAntNum
	);
	CL_PRINTF(cliBuf);

	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = %s / %d", "BT stack/ hci ext ver", \
		(pStackInfo->bProfileNotified ? "Yes" : "No"),
		pStackInfo->hciVersion
	);
	CL_PRINTF(cliBuf);

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_BT_PATCH_VER, &btPatchVer);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_FW_VER, &fwVer);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d_%x/ 0x%x/ 0x%x(%d)", "CoexVer/ FwVer/ PatchVer", \
		GLCoexVerDate8723b2Ant, GLCoexVer8723b2Ant, fwVer, btPatchVer, btPatchVer);
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
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U1_AP_NUM, &apNum);
	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = %d/ %d/ %d", "Wifi rssi/ HS rssi/ AP#", \
		wifiRssi,
		btHsRssi,
		apNum
	);
	CL_PRINTF(cliBuf);

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_SCAN, &bScan);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_LINK, &bLink);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_ROAM, &bRoam);
	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = %d/ %d/ %d ", "Wifi bLink/ bRoam/ bScan", \
		bLink,
		bRoam,
		bScan
	);
	CL_PRINTF(cliBuf);

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_UNDER_5G, &bWifiUnder5G);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_BW, &wifiBw);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_BUSY, &bWifiBusy);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_TRAFFIC_DIRECTION, &wifiTrafficDir);
	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = %s / %s/ %s ", "Wifi status", \
		(bWifiUnder5G ? "5G" : "2.4G"),
		((BTC_WIFI_BW_LEGACY == wifiBw) ? "Legacy" : (((BTC_WIFI_BW_HT40 == wifiBw) ? "HT40" : "HT20"))),
		((!bWifiBusy) ? "idle" : ((BTC_WIFI_TRAFFIC_TX == wifiTrafficDir) ? "uplink" : "downlink"))
	);
	CL_PRINTF(cliBuf);

	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = [%s/ %d/ %d] ", "BT [status/ rssi/ retryCnt]", \
		((pBtCoexist->btInfo.bBtDisabled) ? ("disabled") : ((pCoexSta->bC2hBtInquiryPage) ? ("inquiry/page scan") : ((BT_8723B_2ANT_BT_STATUS_NON_CONNECTED_IDLE == pCoexDm->btStatus) ? "non-connected idle" :
		((BT_8723B_2ANT_BT_STATUS_CONNECTED_IDLE == pCoexDm->btStatus) ? "connected-idle" : "busy")))),
		pCoexSta->btRssi,
		pCoexSta->btRetryCnt
	);
	CL_PRINTF(cliBuf);

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
	pBtCoexist->fBtcDispDbgMsg(pBtCoexist, BTC_DBG_DISP_BT_LINK_INFO);

	btInfoExt = pCoexSta->btInfoExt;
	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = %s", "BT Info A2DP rate", \
		(btInfoExt&BIT0) ? "Basic rate" : "EDR rate"
	);
	CL_PRINTF(cliBuf);

	for (i = 0; i < BT_INFO_SRC_8723B_2ANT_MAX; i++) {
		if (pCoexSta->btInfoC2hCnt[i]) {
			CL_SPRINTF(
				cliBuf,
				BT_TMP_BUF_SIZE,
				"\r\n %-35s = %02x %02x %02x %02x %02x %02x %02x(%d)", GLBtInfoSrc8723b2Ant[i], \
				pCoexSta->btInfoC2h[i][0],
				pCoexSta->btInfoC2h[i][1],
				pCoexSta->btInfoC2h[i][2],
				pCoexSta->btInfoC2h[i][3],
				pCoexSta->btInfoC2h[i][4],
				pCoexSta->btInfoC2h[i][5],
				pCoexSta->btInfoC2h[i][6],
				pCoexSta->btInfoC2hCnt[i]
			);
			CL_PRINTF(cliBuf);
		}
	}

	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = %s/%s", "PS state, IPS/LPS", \
		((pCoexSta->bUnderIps ? "IPS ON" : "IPS OFF")),
		((pCoexSta->bUnderLps ? "LPS ON" : "LPS OFF"))
	);
	CL_PRINTF(cliBuf);
	pBtCoexist->fBtcDispDbgMsg(pBtCoexist, BTC_DBG_DISP_FW_PWR_MODE_CMD);

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
		"\r\n %-35s = %d/ %d/ %d ", "SM1[ShRf/ LpRA/ LimDig]", \
		pCoexDm->bCurRfRxLpfShrink,
		pCoexDm->bCurLowPenaltyRa,
		pCoexDm->bLimitedDig
	);
	CL_PRINTF(cliBuf);
	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = %d/ %d/ %d(0x%x) ",
		"SM2[AgcT/ AdcB/ SwDacSwing(lvl)]", \
		pCoexDm->bCurAgcTableEn,
		pCoexDm->bCurAdcBackOff,
		pCoexDm->bCurDacSwingOn,
		pCoexDm->curDacSwingLvl
	);
	CL_PRINTF(cliBuf);

	/*  Fw mechanism */
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "============[Fw mechanism]============");
	CL_PRINTF(cliBuf);

	psTdmaCase = pCoexDm->curPsTdma;
	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = %02x %02x %02x %02x %02x case-%d (auto:%d)", "PS TDMA", \
		pCoexDm->psTdmaPara[0],
		pCoexDm->psTdmaPara[1],
		pCoexDm->psTdmaPara[2],
		pCoexDm->psTdmaPara[3],
		pCoexDm->psTdmaPara[4],
		psTdmaCase, pCoexDm->bAutoTdmaAdjust
	);
	CL_PRINTF(cliBuf);

	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = %d/ %d ", "DecBtPwr/ IgnWlanAct", \
		pCoexDm->curBtDecPwrLvl,
		pCoexDm->bCurIgnoreWlanAct
	);
	CL_PRINTF(cliBuf);

	/*  Hw setting */
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "============[Hw setting]============");
	CL_PRINTF(cliBuf);

	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = 0x%x", "RF-A, 0x1e initVal", \
		pCoexDm->btRf0x1eBackup
	);
	CL_PRINTF(cliBuf);

	u1Tmp[0] = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x778);
	u4Tmp[0] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x880);
	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = 0x%x/ 0x%x", "0x778/0x880[29:25]", \
		u1Tmp[0],
		(u4Tmp[0]&0x3e000000) >> 25
	);
	CL_PRINTF(cliBuf);


	u4Tmp[0] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x948);
	u1Tmp[0] = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x67);
	u1Tmp[1] = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x765);
	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = 0x%x/ 0x%x/ 0x%x", "0x948/ 0x67[5] / 0x765", \
		u4Tmp[0],
		((u1Tmp[0]&0x20)>>5),
		u1Tmp[1]
	);
	CL_PRINTF(cliBuf);

	u4Tmp[0] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x92c);
	u4Tmp[1] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x930);
	u4Tmp[2] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x944);
	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = 0x%x/ 0x%x/ 0x%x", "0x92c[1:0]/ 0x930[7:0]/0x944[1:0]", \
		u4Tmp[0]&0x3,
		u4Tmp[1]&0xff,
		u4Tmp[2]&0x3
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
		u4Tmp[0],
		u1Tmp[0]
	);
	CL_PRINTF(cliBuf);

	u4Tmp[0] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0xc50);
	u1Tmp[0] = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x49c);
	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = 0x%x/ 0x%x", "0xc50(dig)/0x49c(null-drop)", \
		u4Tmp[0]&0xff,
		u1Tmp[0]
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
		((u4Tmp[3]&0xffff0000) >> 16) +
		(u4Tmp[3] & 0xffff);

	faCck = (u1Tmp[0] << 8) + u1Tmp[1];

	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = 0x%x/ 0x%x/ 0x%x", "OFDM-CCA/OFDM-FA/CCK-FA", \
		u4Tmp[0]&0xffff,
		faOfdm,
		faCck
	);
	CL_PRINTF(cliBuf);

	u4Tmp[0] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x6c0);
	u4Tmp[1] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x6c4);
	u4Tmp[2] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x6c8);
	u1Tmp[0] = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x6cc);
	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = 0x%x/ 0x%x/ 0x%x/ 0x%x", "0x6c0/0x6c4/0x6c8/0x6cc(coexTable)", \
		u4Tmp[0],
		u4Tmp[1],
		u4Tmp[2],
		u1Tmp[0]
	);
	CL_PRINTF(cliBuf);

	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = %d/ %d", "0x770(high-pri rx/tx)", \
		pCoexSta->highPriorityRx,
		pCoexSta->highPriorityTx
	);
	CL_PRINTF(cliBuf);
	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = %d/ %d", "0x774(low-pri rx/tx)", \
		pCoexSta->lowPriorityRx,
		pCoexSta->lowPriorityTx
	);
	CL_PRINTF(cliBuf);

	halbtc8723b2ant_MonitorBtCtr(pBtCoexist);
	pBtCoexist->fBtcDispDbgMsg(pBtCoexist, BTC_DBG_DISP_COEX_STATISTICS);
}


void EXhalbtc8723b2ant_IpsNotify(struct BTC_COEXIST *pBtCoexist, u8 type)
{
	if (BTC_IPS_ENTER == type) {
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], IPS ENTER notify\n"));
		pCoexSta->bUnderIps = true;
		halbtc8723b2ant_WifiOffHwCfg(pBtCoexist);
		halbtc8723b2ant_IgnoreWlanAct(pBtCoexist, FORCE_EXEC, true);
		halbtc8723b2ant_CoexAllOff(pBtCoexist);
	} else if (BTC_IPS_LEAVE == type) {
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], IPS LEAVE notify\n"));
		pCoexSta->bUnderIps = false;
		halbtc8723b2ant_InitHwConfig(pBtCoexist, false);
		halbtc8723b2ant_InitCoexDm(pBtCoexist);
		halbtc8723b2ant_QueryBtInfo(pBtCoexist);
	}
}

void EXhalbtc8723b2ant_LpsNotify(struct BTC_COEXIST *pBtCoexist, u8 type)
{
	if (BTC_LPS_ENABLE == type) {
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], LPS ENABLE notify\n"));
		pCoexSta->bUnderLps = true;
	} else if (BTC_LPS_DISABLE == type) {
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], LPS DISABLE notify\n"));
		pCoexSta->bUnderLps = false;
	}
}

void EXhalbtc8723b2ant_ScanNotify(struct BTC_COEXIST *pBtCoexist, u8 type)
{
	if (BTC_SCAN_START == type) {
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], SCAN START notify\n"));
	} else if (BTC_SCAN_FINISH == type) {
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], SCAN FINISH notify\n"));
	}
}

void EXhalbtc8723b2ant_ConnectNotify(struct BTC_COEXIST *pBtCoexist, u8 type)
{
	if (BTC_ASSOCIATE_START == type) {
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], CONNECT START notify\n"));
	} else if (BTC_ASSOCIATE_FINISH == type) {
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], CONNECT FINISH notify\n"));
	}
}

void EXhalbtc8723b2ant_MediaStatusNotify(struct BTC_COEXIST *pBtCoexist, u8 type)
{
	u8 H2C_Parameter[3] = {0};
	u32 wifiBw;
	u8 wifiCentralChnl;
	u8 apNum = 0;

	if (BTC_MEDIA_CONNECT == type) {
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], MEDIA connect notify\n"));
	} else {
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], MEDIA disconnect notify\n"));
	}

	/*  only 2.4G we need to inform bt the chnl mask */
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U1_WIFI_CENTRAL_CHNL, &wifiCentralChnl);
	if ((BTC_MEDIA_CONNECT == type) && (wifiCentralChnl <= 14)) {
		H2C_Parameter[0] = 0x1;
		H2C_Parameter[1] = wifiCentralChnl;
		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_BW, &wifiBw);
		if (BTC_WIFI_BW_HT40 == wifiBw)
			H2C_Parameter[2] = 0x30;
		else {
			pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U1_AP_NUM, &apNum);
			if (apNum < 10)
				H2C_Parameter[2] = 0x30;
			else
				H2C_Parameter[2] = 0x20;
		}
	}

	pCoexDm->wifiChnlInfo[0] = H2C_Parameter[0];
	pCoexDm->wifiChnlInfo[1] = H2C_Parameter[1];
	pCoexDm->wifiChnlInfo[2] = H2C_Parameter[2];

	BTC_PRINT(
		BTC_MSG_ALGORITHM,
		ALGO_TRACE_FW_EXEC,
		(
			"[BTCoex], FW write 0x66 = 0x%x\n",
			H2C_Parameter[0]<<16|H2C_Parameter[1]<<8|H2C_Parameter[2]
		)
	);

	pBtCoexist->fBtcFillH2c(pBtCoexist, 0x66, 3, H2C_Parameter);
}

void EXhalbtc8723b2ant_SpecialPacketNotify(struct BTC_COEXIST *pBtCoexist, u8 type)
{
	if (type == BTC_PACKET_DHCP) {
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], DHCP Packet notify\n"));
	}
}

void EXhalbtc8723b2ant_BtInfoNotify(
	struct BTC_COEXIST *pBtCoexist, u8 *tmpBuf, u8 length
)
{
	u8 	btInfo = 0;
	u8 	i, rspSource = 0;
	bool bBtBusy = false, bLimitedDig = false;
	bool bWifiConnected = false;

	pCoexSta->bC2hBtInfoReqSent = false;

	rspSource = tmpBuf[0]&0xf;
	if (rspSource >= BT_INFO_SRC_8723B_2ANT_MAX)
		rspSource = BT_INFO_SRC_8723B_2ANT_WIFI_FW;

	pCoexSta->btInfoC2hCnt[rspSource]++;

	BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], Bt info[%d], length =%d, hex data =[", rspSource, length));
	for (i = 0; i < length; i++) {
		pCoexSta->btInfoC2h[rspSource][i] = tmpBuf[i];
		if (i == 1)
			btInfo = tmpBuf[i];

		if (i == length-1) {
			BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("0x%02x]\n", tmpBuf[i]));
		} else {
			BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("0x%02x, ", tmpBuf[i]));
		}
	}

	if (pBtCoexist->bManualControl) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BtInfoNotify(), return for Manual CTRL<===\n"));
		return;
	}

	if (BT_INFO_SRC_8723B_2ANT_WIFI_FW != rspSource) {
		pCoexSta->btRetryCnt = pCoexSta->btInfoC2h[rspSource][2]&0xf; /* [3:0] */

		pCoexSta->btRssi = pCoexSta->btInfoC2h[rspSource][3]*2+10;

		pCoexSta->btInfoExt = pCoexSta->btInfoC2h[rspSource][4];

		pCoexSta->bBtTxRxMask = (pCoexSta->btInfoC2h[rspSource][2]&0x40);
		pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_BL_BT_TX_RX_MASK, &pCoexSta->bBtTxRxMask);
		if (pCoexSta->bBtTxRxMask) {
			/* BT into is responded by BT FW and BT RF REG 0x3C != 0x01 => Need to switch BT TRx Mask */
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Switch BT TRx Mask since BT RF REG 0x3C != 0x01\n"));
			pBtCoexist->fBtcSetBtReg(pBtCoexist, BTC_BT_REG_RF, 0x3c, 0x01);
		}

		/*  Here we need to resend some wifi info to BT */
		/*  because bt is reset and loss of the info. */
		if ((pCoexSta->btInfoExt & BIT1)) {
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BT ext info bit1 check, send wifi BW&Chnl to BT!!\n"));
			pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_CONNECTED, &bWifiConnected);

			if (bWifiConnected)
				EXhalbtc8723b2ant_MediaStatusNotify(pBtCoexist, BTC_MEDIA_CONNECT);
			else
				EXhalbtc8723b2ant_MediaStatusNotify(pBtCoexist, BTC_MEDIA_DISCONNECT);
		}

		if ((pCoexSta->btInfoExt & BIT3)) {
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BT ext info bit3 check, set BT NOT to ignore Wlan active!!\n"));
			halbtc8723b2ant_IgnoreWlanAct(pBtCoexist, FORCE_EXEC, false);
		} else {
			/*  BT already NOT ignore Wlan active, do nothing here. */
		}
	}

	/*  check BIT2 first ==> check if bt is under inquiry or page scan */
	if (btInfo & BT_INFO_8723B_2ANT_B_INQ_PAGE)
		pCoexSta->bC2hBtInquiryPage = true;
	else
		pCoexSta->bC2hBtInquiryPage = false;

	/*  set link exist status */
	if (!(btInfo&BT_INFO_8723B_2ANT_B_CONNECTION)) {
		pCoexSta->bBtLinkExist = false;
		pCoexSta->bPanExist = false;
		pCoexSta->bA2dpExist = false;
		pCoexSta->bHidExist = false;
		pCoexSta->bScoExist = false;
	} else { /*  connection exists */
		pCoexSta->bBtLinkExist = true;
		if (btInfo & BT_INFO_8723B_2ANT_B_FTP)
			pCoexSta->bPanExist = true;
		else
			pCoexSta->bPanExist = false;
		if (btInfo & BT_INFO_8723B_2ANT_B_A2DP)
			pCoexSta->bA2dpExist = true;
		else
			pCoexSta->bA2dpExist = false;
		if (btInfo & BT_INFO_8723B_2ANT_B_HID)
			pCoexSta->bHidExist = true;
		else
			pCoexSta->bHidExist = false;
		if (btInfo & BT_INFO_8723B_2ANT_B_SCO_ESCO)
			pCoexSta->bScoExist = true;
		else
			pCoexSta->bScoExist = false;
	}

	halbtc8723b2ant_UpdateBtLinkInfo(pBtCoexist);

	if (!(btInfo&BT_INFO_8723B_2ANT_B_CONNECTION)) {
		pCoexDm->btStatus = BT_8723B_2ANT_BT_STATUS_NON_CONNECTED_IDLE;
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BtInfoNotify(), BT Non-Connected idle!!!\n"));
	} else if (btInfo == BT_INFO_8723B_2ANT_B_CONNECTION)	{ /*  connection exists but no busy */
		pCoexDm->btStatus = BT_8723B_2ANT_BT_STATUS_CONNECTED_IDLE;
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BtInfoNotify(), BT Connected-idle!!!\n"));
	} else if (
		(btInfo&BT_INFO_8723B_2ANT_B_SCO_ESCO) ||
		(btInfo&BT_INFO_8723B_2ANT_B_SCO_BUSY)
	) {
		pCoexDm->btStatus = BT_8723B_2ANT_BT_STATUS_SCO_BUSY;
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BtInfoNotify(), BT SCO busy!!!\n"));
	} else if (btInfo&BT_INFO_8723B_2ANT_B_ACL_BUSY) {
		pCoexDm->btStatus = BT_8723B_2ANT_BT_STATUS_ACL_BUSY;
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BtInfoNotify(), BT ACL busy!!!\n"));
	} else {
		pCoexDm->btStatus = BT_8723B_2ANT_BT_STATUS_MAX;
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BtInfoNotify(), BT Non-Defined state!!!\n"));
	}

	if (
		(BT_8723B_2ANT_BT_STATUS_ACL_BUSY == pCoexDm->btStatus) ||
		(BT_8723B_2ANT_BT_STATUS_SCO_BUSY == pCoexDm->btStatus) ||
		(BT_8723B_2ANT_BT_STATUS_ACL_SCO_BUSY == pCoexDm->btStatus)
	) {
		bBtBusy = true;
		bLimitedDig = true;
	} else {
		bBtBusy = false;
		bLimitedDig = false;
	}

	pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_BL_BT_TRAFFIC_BUSY, &bBtBusy);

	pCoexDm->bLimitedDig = bLimitedDig;
	pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_BL_BT_LIMITED_DIG, &bLimitedDig);

	halbtc8723b2ant_RunCoexistMechanism(pBtCoexist);
}

void EXhalbtc8723b2ant_HaltNotify(struct BTC_COEXIST *pBtCoexist)
{
	BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], Halt notify\n"));

	halbtc8723b2ant_WifiOffHwCfg(pBtCoexist);
	pBtCoexist->fBtcSetBtReg(pBtCoexist, BTC_BT_REG_RF, 0x3c, 0x15); /* BT goto standby while GNT_BT 1-->0 */
	halbtc8723b2ant_IgnoreWlanAct(pBtCoexist, FORCE_EXEC, true);

	EXhalbtc8723b2ant_MediaStatusNotify(pBtCoexist, BTC_MEDIA_DISCONNECT);
}

void EXhalbtc8723b2ant_PnpNotify(struct BTC_COEXIST *pBtCoexist, u8 pnpState)
{
	BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], Pnp notify\n"));

	if (BTC_WIFI_PNP_SLEEP == pnpState) {
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], Pnp notify to SLEEP\n"));
	} else if (BTC_WIFI_PNP_WAKE_UP == pnpState) {
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], Pnp notify to WAKE UP\n"));
		halbtc8723b2ant_InitHwConfig(pBtCoexist, false);
		halbtc8723b2ant_InitCoexDm(pBtCoexist);
		halbtc8723b2ant_QueryBtInfo(pBtCoexist);
	}
}

void EXhalbtc8723b2ant_Periodical(struct BTC_COEXIST *pBtCoexist)
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
			GLCoexVerDate8723b2Ant, GLCoexVer8723b2Ant, fwVer, btPatchVer, btPatchVer));
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_INIT, ("[BTCoex], ****************************************************************\n"));
	}

	if (
		halbtc8723b2ant_IsWifiStatusChanged(pBtCoexist) ||
		pCoexDm->bAutoTdmaAdjust
	)
		halbtc8723b2ant_RunCoexistMechanism(pBtCoexist);
}
