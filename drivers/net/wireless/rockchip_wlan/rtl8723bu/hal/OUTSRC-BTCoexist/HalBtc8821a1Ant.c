//============================================================
// Description:
//
// This file is for RTL8821A Co-exist mechanism
//
// History
// 2012/11/15 Cosa first check in.
//
//============================================================

//============================================================
// include files
//============================================================
#include "Mp_Precomp.h"
#if(BT_30_SUPPORT == 1)
//============================================================
// Global variables, these are static variables
//============================================================
static COEX_DM_8821A_1ANT		GLCoexDm8821a1Ant;
static PCOEX_DM_8821A_1ANT 	pCoexDm=&GLCoexDm8821a1Ant;
static COEX_STA_8821A_1ANT		GLCoexSta8821a1Ant;
static PCOEX_STA_8821A_1ANT	pCoexSta=&GLCoexSta8821a1Ant;

const char *const GLBtInfoSrc8821a1Ant[]={
	"BT Info[wifi fw]",
	"BT Info[bt rsp]",
	"BT Info[bt auto report]",
};

u4Byte	GLCoexVerDate8821a1Ant=20130816;
u4Byte	GLCoexVer8821a1Ant=0x41;

//============================================================
// local function proto type if needed
//============================================================
//============================================================
// local function start with halbtc8821a1ant_
//============================================================
u1Byte
halbtc8821a1ant_BtRssiState(
	u1Byte			levelNum,
	u1Byte			rssiThresh,
	u1Byte			rssiThresh1
	)
{
	s4Byte			btRssi=0;
	u1Byte			btRssiState=pCoexSta->preBtRssiState;

	btRssi = pCoexSta->btRssi;

	if(levelNum == 2)
	{			
		if( (pCoexSta->preBtRssiState == BTC_RSSI_STATE_LOW) ||
			(pCoexSta->preBtRssiState == BTC_RSSI_STATE_STAY_LOW))
		{
			if(btRssi >= (rssiThresh+BTC_RSSI_COEX_THRESH_TOL_8821A_1ANT))
			{
				btRssiState = BTC_RSSI_STATE_HIGH;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE, ("[BTCoex], BT Rssi state switch to High\n"));
			}
			else
			{
				btRssiState = BTC_RSSI_STATE_STAY_LOW;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE, ("[BTCoex], BT Rssi state stay at Low\n"));
			}
		}
		else
		{
			if(btRssi < rssiThresh)
			{
				btRssiState = BTC_RSSI_STATE_LOW;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE, ("[BTCoex], BT Rssi state switch to Low\n"));
			}
			else
			{
				btRssiState = BTC_RSSI_STATE_STAY_HIGH;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE, ("[BTCoex], BT Rssi state stay at High\n"));
			}
		}
	}
	else if(levelNum == 3)
	{
		if(rssiThresh > rssiThresh1)
		{
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE, ("[BTCoex], BT Rssi thresh error!!\n"));
			return pCoexSta->preBtRssiState;
		}
		
		if( (pCoexSta->preBtRssiState == BTC_RSSI_STATE_LOW) ||
			(pCoexSta->preBtRssiState == BTC_RSSI_STATE_STAY_LOW))
		{
			if(btRssi >= (rssiThresh+BTC_RSSI_COEX_THRESH_TOL_8821A_1ANT))
			{
				btRssiState = BTC_RSSI_STATE_MEDIUM;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE, ("[BTCoex], BT Rssi state switch to Medium\n"));
			}
			else
			{
				btRssiState = BTC_RSSI_STATE_STAY_LOW;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE, ("[BTCoex], BT Rssi state stay at Low\n"));
			}
		}
		else if( (pCoexSta->preBtRssiState == BTC_RSSI_STATE_MEDIUM) ||
			(pCoexSta->preBtRssiState == BTC_RSSI_STATE_STAY_MEDIUM))
		{
			if(btRssi >= (rssiThresh1+BTC_RSSI_COEX_THRESH_TOL_8821A_1ANT))
			{
				btRssiState = BTC_RSSI_STATE_HIGH;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE, ("[BTCoex], BT Rssi state switch to High\n"));
			}
			else if(btRssi < rssiThresh)
			{
				btRssiState = BTC_RSSI_STATE_LOW;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE, ("[BTCoex], BT Rssi state switch to Low\n"));
			}
			else
			{
				btRssiState = BTC_RSSI_STATE_STAY_MEDIUM;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE, ("[BTCoex], BT Rssi state stay at Medium\n"));
			}
		}
		else
		{
			if(btRssi < rssiThresh1)
			{
				btRssiState = BTC_RSSI_STATE_MEDIUM;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE, ("[BTCoex], BT Rssi state switch to Medium\n"));
			}
			else
			{
				btRssiState = BTC_RSSI_STATE_STAY_HIGH;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE, ("[BTCoex], BT Rssi state stay at High\n"));
			}
		}
	}
		
	pCoexSta->preBtRssiState = btRssiState;

	return btRssiState;
}

u1Byte
halbtc8821a1ant_WifiRssiState(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			index,
	IN	u1Byte			levelNum,
	IN	u1Byte			rssiThresh,
	IN	u1Byte			rssiThresh1
	)
{
	s4Byte			wifiRssi=0;
	u1Byte			wifiRssiState=pCoexSta->preWifiRssiState[index];

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_S4_WIFI_RSSI, &wifiRssi);
	
	if(levelNum == 2)
	{
		if( (pCoexSta->preWifiRssiState[index] == BTC_RSSI_STATE_LOW) ||
			(pCoexSta->preWifiRssiState[index] == BTC_RSSI_STATE_STAY_LOW))
		{
			if(wifiRssi >= (rssiThresh+BTC_RSSI_COEX_THRESH_TOL_8821A_1ANT))
			{
				wifiRssiState = BTC_RSSI_STATE_HIGH;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_WIFI_RSSI_STATE, ("[BTCoex], wifi RSSI state switch to High\n"));
			}
			else
			{
				wifiRssiState = BTC_RSSI_STATE_STAY_LOW;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_WIFI_RSSI_STATE, ("[BTCoex], wifi RSSI state stay at Low\n"));
			}
		}
		else
		{
			if(wifiRssi < rssiThresh)
			{
				wifiRssiState = BTC_RSSI_STATE_LOW;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_WIFI_RSSI_STATE, ("[BTCoex], wifi RSSI state switch to Low\n"));
			}
			else
			{
				wifiRssiState = BTC_RSSI_STATE_STAY_HIGH;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_WIFI_RSSI_STATE, ("[BTCoex], wifi RSSI state stay at High\n"));
			}
		}
	}
	else if(levelNum == 3)
	{
		if(rssiThresh > rssiThresh1)
		{
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_WIFI_RSSI_STATE, ("[BTCoex], wifi RSSI thresh error!!\n"));
			return pCoexSta->preWifiRssiState[index];
		}
		
		if( (pCoexSta->preWifiRssiState[index] == BTC_RSSI_STATE_LOW) ||
			(pCoexSta->preWifiRssiState[index] == BTC_RSSI_STATE_STAY_LOW))
		{
			if(wifiRssi >= (rssiThresh+BTC_RSSI_COEX_THRESH_TOL_8821A_1ANT))
			{
				wifiRssiState = BTC_RSSI_STATE_MEDIUM;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_WIFI_RSSI_STATE, ("[BTCoex], wifi RSSI state switch to Medium\n"));
			}
			else
			{
				wifiRssiState = BTC_RSSI_STATE_STAY_LOW;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_WIFI_RSSI_STATE, ("[BTCoex], wifi RSSI state stay at Low\n"));
			}
		}
		else if( (pCoexSta->preWifiRssiState[index] == BTC_RSSI_STATE_MEDIUM) ||
			(pCoexSta->preWifiRssiState[index] == BTC_RSSI_STATE_STAY_MEDIUM))
		{
			if(wifiRssi >= (rssiThresh1+BTC_RSSI_COEX_THRESH_TOL_8821A_1ANT))
			{
				wifiRssiState = BTC_RSSI_STATE_HIGH;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_WIFI_RSSI_STATE, ("[BTCoex], wifi RSSI state switch to High\n"));
			}
			else if(wifiRssi < rssiThresh)
			{
				wifiRssiState = BTC_RSSI_STATE_LOW;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_WIFI_RSSI_STATE, ("[BTCoex], wifi RSSI state switch to Low\n"));
			}
			else
			{
				wifiRssiState = BTC_RSSI_STATE_STAY_MEDIUM;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_WIFI_RSSI_STATE, ("[BTCoex], wifi RSSI state stay at Medium\n"));
			}
		}
		else
		{
			if(wifiRssi < rssiThresh1)
			{
				wifiRssiState = BTC_RSSI_STATE_MEDIUM;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_WIFI_RSSI_STATE, ("[BTCoex], wifi RSSI state switch to Medium\n"));
			}
			else
			{
				wifiRssiState = BTC_RSSI_STATE_STAY_HIGH;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_WIFI_RSSI_STATE, ("[BTCoex], wifi RSSI state stay at High\n"));
			}
		}
	}
		
	pCoexSta->preWifiRssiState[index] = wifiRssiState;

	return wifiRssiState;
}

VOID
halbtc8821a1ant_UpdateRaMask(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN				bForceExec,
	IN	u4Byte				disRateMask
	)
{
	pCoexDm->curRaMask = disRateMask;
	
	if( bForceExec || (pCoexDm->preRaMask != pCoexDm->curRaMask))
	{
		pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_ACT_UPDATE_RAMASK, &pCoexDm->curRaMask);
	}
	pCoexDm->preRaMask = pCoexDm->curRaMask;
}

VOID
halbtc8821a1ant_AutoRateFallbackRetry(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN				bForceExec,
	IN	u1Byte				type
	)
{
	BOOLEAN	bWifiUnderBMode=FALSE;
	
	pCoexDm->curArfrType = type;

	if( bForceExec || (pCoexDm->preArfrType != pCoexDm->curArfrType))
	{
		switch(pCoexDm->curArfrType)
		{
			case 0:	// normal mode
				pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x430, pCoexDm->backupArfrCnt1);
				pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x434, pCoexDm->backupArfrCnt2);
				break;
			case 1:	
				pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_UNDER_B_MODE, &bWifiUnderBMode);
				if(bWifiUnderBMode)
				{
					pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x430, 0x0);
					pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x434, 0x01010101);
				}
				else
				{
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

VOID
halbtc8821a1ant_RetryLimit(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN				bForceExec,
	IN	u1Byte				type
	)
{
	pCoexDm->curRetryLimitType = type;

	if( bForceExec || (pCoexDm->preRetryLimitType != pCoexDm->curRetryLimitType))
	{
		switch(pCoexDm->curRetryLimitType)
		{
			case 0:	// normal mode
				pBtCoexist->fBtcWrite2Byte(pBtCoexist, 0x42a, pCoexDm->backupRetryLimit);
				break;
			case 1:	// retry limit=8
				pBtCoexist->fBtcWrite2Byte(pBtCoexist, 0x42a, 0x0808);
				break;
			default:
				break;
		}
	}

	pCoexDm->preRetryLimitType = pCoexDm->curRetryLimitType;
}

VOID
halbtc8821a1ant_AmpduMaxTime(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN				bForceExec,
	IN	u1Byte				type
	)
{
	pCoexDm->curAmpduTimeType = type;

	if( bForceExec || (pCoexDm->preAmpduTimeType != pCoexDm->curAmpduTimeType))
	{
		switch(pCoexDm->curAmpduTimeType)
		{
			case 0:	// normal mode
				pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x456, pCoexDm->backupAmpduMaxTime);
				break;
			case 1:	// AMPDU timw = 0x38 * 32us
				pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x456, 0x38);
				break;
			default:
				break;
		}
	}

	pCoexDm->preAmpduTimeType = pCoexDm->curAmpduTimeType;
}

VOID
halbtc8821a1ant_LimitedTx(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN				bForceExec,
	IN	u1Byte				raMaskType,
	IN	u1Byte				arfrType,
	IN	u1Byte				retryLimitType,
	IN	u1Byte				ampduTimeType
	)
{
	switch(raMaskType)
	{
		case 0:	// normal mode
			halbtc8821a1ant_UpdateRaMask(pBtCoexist, bForceExec, 0x0);
			break;
		case 1:	// disable cck 1/2
			halbtc8821a1ant_UpdateRaMask(pBtCoexist, bForceExec, 0x00000003);
			break;
		case 2:	// disable cck 1/2/5.5, ofdm 6/9/12/18/24, mcs 0/1/2/3/4
			halbtc8821a1ant_UpdateRaMask(pBtCoexist, bForceExec, 0x0001f1f7);
			break;
		default:
			break;
	}

	halbtc8821a1ant_AutoRateFallbackRetry(pBtCoexist, bForceExec, arfrType);
	halbtc8821a1ant_RetryLimit(pBtCoexist, bForceExec, retryLimitType);
	halbtc8821a1ant_AmpduMaxTime(pBtCoexist, bForceExec, ampduTimeType);
}

VOID
halbtc8821a1ant_LimitedRx(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN				bForceExec,
	IN	BOOLEAN				bRejApAggPkt,
	IN	BOOLEAN				bBtCtrlAggBufSize,
	IN	u1Byte				aggBufSize
	)
{
	BOOLEAN	bRejectRxAgg=bRejApAggPkt;
	BOOLEAN	bBtCtrlRxAggSize=bBtCtrlAggBufSize;
	u1Byte	rxAggSize=aggBufSize;

	//============================================
	//	Rx Aggregation related setting
	//============================================
	pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_BL_TO_REJ_AP_AGG_PKT, &bRejectRxAgg);
	// decide BT control aggregation buf size or not
	pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_BL_BT_CTRL_AGG_SIZE, &bBtCtrlRxAggSize);
	// aggregation buf size, only work when BT control Rx aggregation size.
	pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_U1_AGG_BUF_SIZE, &rxAggSize);
	// real update aggregation setting
	pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_ACT_AGGREGATE_CTRL, NULL);


}

VOID
halbtc8821a1ant_MonitorBtCtr(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	u4Byte 			regHPTxRx, regLPTxRx, u4Tmp, u4Tmp1;
	u4Byte			regHPTx=0, regHPRx=0, regLPTx=0, regLPRx=0;
	u1Byte			u1Tmp, u1Tmp1;
	s4Byte			wifiRssi;
	
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

	// reset counter
	pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x76e, 0xc);
}

VOID
halbtc8821a1ant_QueryBtInfo(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	u1Byte			H2C_Parameter[1] ={0};

	pCoexSta->bC2hBtInfoReqSent = TRUE;

	H2C_Parameter[0] |= BIT0;	// trigger

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_EXEC, ("[BTCoex], Query Bt Info, FW write 0x61=0x%x\n", 
		H2C_Parameter[0]));

	pBtCoexist->fBtcFillH2c(pBtCoexist, 0x61, 1, H2C_Parameter);
}

BOOLEAN
halbtc8821a1ant_IsWifiStatusChanged(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	static BOOLEAN	bPreWifiBusy=FALSE, bPreUnder4way=FALSE, bPreBtHsOn=FALSE;
	BOOLEAN bWifiBusy=FALSE, bUnder4way=FALSE, bBtHsOn=FALSE;
	BOOLEAN bWifiConnected=FALSE;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_CONNECTED, &bWifiConnected);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_BUSY, &bWifiBusy);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_HS_OPERATION, &bBtHsOn);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_4_WAY_PROGRESS, &bUnder4way);

	if(bWifiConnected)
	{
		if(bWifiBusy != bPreWifiBusy)
		{
			bPreWifiBusy = bWifiBusy;
			return TRUE;
		}
		if(bUnder4way != bPreUnder4way)
		{
			bPreUnder4way = bUnder4way;
			return TRUE;
		}
		if(bBtHsOn != bPreBtHsOn)
		{
			bPreBtHsOn = bBtHsOn;
			return TRUE;
		}
	}

	return FALSE;
}

VOID
halbtc8821a1ant_UpdateBtLinkInfo(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	PBTC_BT_LINK_INFO	pBtLinkInfo=&pBtCoexist->btLinkInfo;
	BOOLEAN				bBtHsOn=FALSE;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_HS_OPERATION, &bBtHsOn);

	pBtLinkInfo->bBtLinkExist = pCoexSta->bBtLinkExist;
	pBtLinkInfo->bScoExist = pCoexSta->bScoExist;
	pBtLinkInfo->bA2dpExist = pCoexSta->bA2dpExist;
	pBtLinkInfo->bPanExist = pCoexSta->bPanExist;
	pBtLinkInfo->bHidExist = pCoexSta->bHidExist;

	// work around for HS mode.
	if(bBtHsOn)
	{
		pBtLinkInfo->bPanExist = TRUE;
		pBtLinkInfo->bBtLinkExist = TRUE;
	}

	// check if Sco only
	if( pBtLinkInfo->bScoExist &&
		!pBtLinkInfo->bA2dpExist &&
		!pBtLinkInfo->bPanExist &&
		!pBtLinkInfo->bHidExist )
		pBtLinkInfo->bScoOnly = TRUE;
	else
		pBtLinkInfo->bScoOnly = FALSE;

	// check if A2dp only
	if( !pBtLinkInfo->bScoExist &&
		pBtLinkInfo->bA2dpExist &&
		!pBtLinkInfo->bPanExist &&
		!pBtLinkInfo->bHidExist )
		pBtLinkInfo->bA2dpOnly = TRUE;
	else
		pBtLinkInfo->bA2dpOnly = FALSE;

	// check if Pan only
	if( !pBtLinkInfo->bScoExist &&
		!pBtLinkInfo->bA2dpExist &&
		pBtLinkInfo->bPanExist &&
		!pBtLinkInfo->bHidExist )
		pBtLinkInfo->bPanOnly = TRUE;
	else
		pBtLinkInfo->bPanOnly = FALSE;
	
	// check if Hid only
	if( !pBtLinkInfo->bScoExist &&
		!pBtLinkInfo->bA2dpExist &&
		!pBtLinkInfo->bPanExist &&
		pBtLinkInfo->bHidExist )
		pBtLinkInfo->bHidOnly = TRUE;
	else
		pBtLinkInfo->bHidOnly = FALSE;
}

u1Byte
halbtc8821a1ant_ActionAlgorithm(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	PBTC_BT_LINK_INFO	pBtLinkInfo=&pBtCoexist->btLinkInfo;
	BOOLEAN				bBtHsOn=FALSE;
	u1Byte				algorithm=BT_8821A_1ANT_COEX_ALGO_UNDEFINED;
	u1Byte				numOfDiffProfile=0;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_HS_OPERATION, &bBtHsOn);

	if(!pBtLinkInfo->bBtLinkExist)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], No BT link exists!!!\n"));
		return algorithm;
	}

	if(pBtLinkInfo->bScoExist)
		numOfDiffProfile++;
	if(pBtLinkInfo->bHidExist)
		numOfDiffProfile++;
	if(pBtLinkInfo->bPanExist)
		numOfDiffProfile++;
	if(pBtLinkInfo->bA2dpExist)
		numOfDiffProfile++;
	
	if(numOfDiffProfile == 1)
	{
		if(pBtLinkInfo->bScoExist)
		{
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BT Profile = SCO only\n"));
			algorithm = BT_8821A_1ANT_COEX_ALGO_SCO;
		}
		else
		{
			if(pBtLinkInfo->bHidExist)
			{
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BT Profile = HID only\n"));
				algorithm = BT_8821A_1ANT_COEX_ALGO_HID;
			}
			else if(pBtLinkInfo->bA2dpExist)
			{
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BT Profile = A2DP only\n"));
				algorithm = BT_8821A_1ANT_COEX_ALGO_A2DP;
			}
			else if(pBtLinkInfo->bPanExist)
			{
				if(bBtHsOn)
				{
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BT Profile = PAN(HS) only\n"));
					algorithm = BT_8821A_1ANT_COEX_ALGO_PANHS;
				}
				else
				{
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BT Profile = PAN(EDR) only\n"));
					algorithm = BT_8821A_1ANT_COEX_ALGO_PANEDR;
				}
			}
		}
	}
	else if(numOfDiffProfile == 2)
	{
		if(pBtLinkInfo->bScoExist)
		{
			if(pBtLinkInfo->bHidExist)
			{
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BT Profile = SCO + HID\n"));
				algorithm = BT_8821A_1ANT_COEX_ALGO_HID;
			}
			else if(pBtLinkInfo->bA2dpExist)
			{
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BT Profile = SCO + A2DP ==> SCO\n"));
				algorithm = BT_8821A_1ANT_COEX_ALGO_SCO;
			}
			else if(pBtLinkInfo->bPanExist)
			{
				if(bBtHsOn)
				{
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BT Profile = SCO + PAN(HS)\n"));
					algorithm = BT_8821A_1ANT_COEX_ALGO_SCO;
				}
				else
				{
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BT Profile = SCO + PAN(EDR)\n"));
					algorithm = BT_8821A_1ANT_COEX_ALGO_PANEDR_HID;
				}
			}
		}
		else
		{
			if( pBtLinkInfo->bHidExist &&
				pBtLinkInfo->bA2dpExist )
			{
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BT Profile = HID + A2DP\n"));
				algorithm = BT_8821A_1ANT_COEX_ALGO_HID_A2DP;
			}
			else if( pBtLinkInfo->bHidExist &&
				pBtLinkInfo->bPanExist )
			{
				if(bBtHsOn)
				{
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BT Profile = HID + PAN(HS)\n"));
					algorithm = BT_8821A_1ANT_COEX_ALGO_HID_A2DP;
				}
				else
				{
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BT Profile = HID + PAN(EDR)\n"));
					algorithm = BT_8821A_1ANT_COEX_ALGO_PANEDR_HID;
				}
			}
			else if( pBtLinkInfo->bPanExist &&
				pBtLinkInfo->bA2dpExist )
			{
				if(bBtHsOn)
				{
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BT Profile = A2DP + PAN(HS)\n"));
					algorithm = BT_8821A_1ANT_COEX_ALGO_A2DP_PANHS;
				}
				else
				{
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BT Profile = A2DP + PAN(EDR)\n"));
					algorithm = BT_8821A_1ANT_COEX_ALGO_PANEDR_A2DP;
				}
			}
		}
	}
	else if(numOfDiffProfile == 3)
	{
		if(pBtLinkInfo->bScoExist)
		{
			if( pBtLinkInfo->bHidExist &&
				pBtLinkInfo->bA2dpExist )
			{
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BT Profile = SCO + HID + A2DP ==> HID\n"));
				algorithm = BT_8821A_1ANT_COEX_ALGO_HID;
			}
			else if( pBtLinkInfo->bHidExist &&
				pBtLinkInfo->bPanExist )
			{
				if(bBtHsOn)
				{
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BT Profile = SCO + HID + PAN(HS)\n"));
					algorithm = BT_8821A_1ANT_COEX_ALGO_HID_A2DP;
				}
				else
				{
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BT Profile = SCO + HID + PAN(EDR)\n"));
					algorithm = BT_8821A_1ANT_COEX_ALGO_PANEDR_HID;
				}
			}
			else if( pBtLinkInfo->bPanExist &&
				pBtLinkInfo->bA2dpExist )
			{
				if(bBtHsOn)
				{
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BT Profile = SCO + A2DP + PAN(HS)\n"));
					algorithm = BT_8821A_1ANT_COEX_ALGO_SCO;
				}
				else
				{
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BT Profile = SCO + A2DP + PAN(EDR) ==> HID\n"));
					algorithm = BT_8821A_1ANT_COEX_ALGO_PANEDR_HID;
				}
			}
		}
		else
		{
			if( pBtLinkInfo->bHidExist &&
				pBtLinkInfo->bPanExist &&
				pBtLinkInfo->bA2dpExist )
			{
				if(bBtHsOn)
				{
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BT Profile = HID + A2DP + PAN(HS)\n"));
					algorithm = BT_8821A_1ANT_COEX_ALGO_HID_A2DP;
				}
				else
				{
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BT Profile = HID + A2DP + PAN(EDR)\n"));
					algorithm = BT_8821A_1ANT_COEX_ALGO_HID_A2DP_PANEDR;
				}
			}
		}
	}
	else if(numOfDiffProfile >= 3)
	{
		if(pBtLinkInfo->bScoExist)
		{
			if( pBtLinkInfo->bHidExist &&
				pBtLinkInfo->bPanExist &&
				pBtLinkInfo->bA2dpExist )
			{
				if(bBtHsOn)
				{
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Error!!! BT Profile = SCO + HID + A2DP + PAN(HS)\n"));

				}
				else
				{
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BT Profile = SCO + HID + A2DP + PAN(EDR)==>PAN(EDR)+HID\n"));
					algorithm = BT_8821A_1ANT_COEX_ALGO_PANEDR_HID;
				}
			}
		}
	}

	return algorithm;
}

VOID
halbtc8821a1ant_SetBtAutoReport(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bEnableAutoReport
	)
{
	u1Byte			H2C_Parameter[1] ={0};
	
	H2C_Parameter[0] = 0;

	if(bEnableAutoReport)
	{
		H2C_Parameter[0] |= BIT0;
	}

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_EXEC, ("[BTCoex], BT FW auto report : %s, FW write 0x68=0x%x\n", 
		(bEnableAutoReport? "Enabled!!":"Disabled!!"), H2C_Parameter[0]));

	pBtCoexist->fBtcFillH2c(pBtCoexist, 0x68, 1, H2C_Parameter);	
}

VOID
halbtc8821a1ant_BtAutoReport(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bForceExec,
	IN	BOOLEAN			bEnableAutoReport
	)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW, ("[BTCoex], %s BT Auto report = %s\n",  
		(bForceExec? "force to":""), ((bEnableAutoReport)? "Enabled":"Disabled")));
	pCoexDm->bCurBtAutoReport = bEnableAutoReport;

	if(!bForceExec)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], bPreBtAutoReport=%d, bCurBtAutoReport=%d\n", 
			pCoexDm->bPreBtAutoReport, pCoexDm->bCurBtAutoReport));

		if(pCoexDm->bPreBtAutoReport == pCoexDm->bCurBtAutoReport) 
			return;
	}
	halbtc8821a1ant_SetBtAutoReport(pBtCoexist, pCoexDm->bCurBtAutoReport);

	pCoexDm->bPreBtAutoReport = pCoexDm->bCurBtAutoReport;
}

VOID
halbtc8821a1ant_SetSwPenaltyTxRateAdaptive(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bLowPenaltyRa
	)
{
	u1Byte			H2C_Parameter[6] ={0};
	
	H2C_Parameter[0] = 0x6;	// opCode, 0x6= Retry_Penalty

	if(bLowPenaltyRa)
	{
		H2C_Parameter[1] |= BIT0;
		H2C_Parameter[2] = 0x00;  //normal rate except MCS7/6/5, OFDM54/48/36
		H2C_Parameter[3] = 0xf7;  //MCS7 or OFDM54
		H2C_Parameter[4] = 0xf8;  //MCS6 or OFDM48
		H2C_Parameter[5] = 0xf9;	//MCS5 or OFDM36	
	}

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_EXEC, ("[BTCoex], set WiFi Low-Penalty Retry: %s", 
		(bLowPenaltyRa? "ON!!":"OFF!!") ));

	pBtCoexist->fBtcFillH2c(pBtCoexist, 0x69, 6, H2C_Parameter);
}

VOID
halbtc8821a1ant_LowPenaltyRa(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bForceExec,
	IN	BOOLEAN			bLowPenaltyRa
	)
{
	pCoexDm->bCurLowPenaltyRa = bLowPenaltyRa;

	if(!bForceExec)
	{
		if(pCoexDm->bPreLowPenaltyRa == pCoexDm->bCurLowPenaltyRa) 
			return;
	}
	halbtc8821a1ant_SetSwPenaltyTxRateAdaptive(pBtCoexist, pCoexDm->bCurLowPenaltyRa);

	pCoexDm->bPreLowPenaltyRa = pCoexDm->bCurLowPenaltyRa;
}

VOID
halbtc8821a1ant_SetCoexTable(
	IN	PBTC_COEXIST	pBtCoexist,
	IN	u4Byte		val0x6c0,
	IN	u4Byte		val0x6c4,
	IN	u4Byte		val0x6c8,
	IN	u1Byte		val0x6cc
	)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC, ("[BTCoex], set coex table, set 0x6c0=0x%x\n", val0x6c0));
	pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x6c0, val0x6c0);

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC, ("[BTCoex], set coex table, set 0x6c4=0x%x\n", val0x6c4));
	pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x6c4, val0x6c4);

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC, ("[BTCoex], set coex table, set 0x6c8=0x%x\n", val0x6c8));
	pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x6c8, val0x6c8);

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC, ("[BTCoex], set coex table, set 0x6cc=0x%x\n", val0x6cc));
	pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x6cc, val0x6cc);
}

VOID
halbtc8821a1ant_CoexTable(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bForceExec,
	IN	u4Byte			val0x6c0,
	IN	u4Byte			val0x6c4,
	IN	u4Byte			val0x6c8,
	IN	u1Byte			val0x6cc
	)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW, ("[BTCoex], %s write Coex Table 0x6c0=0x%x, 0x6c4=0x%x, 0x6c8=0x%x, 0x6cc=0x%x\n", 
		(bForceExec? "force to":""), val0x6c0, val0x6c4, val0x6c8, val0x6cc));
	pCoexDm->curVal0x6c0 = val0x6c0;
	pCoexDm->curVal0x6c4 = val0x6c4;
	pCoexDm->curVal0x6c8 = val0x6c8;
	pCoexDm->curVal0x6cc = val0x6cc;

	if(!bForceExec)
	{
		if( (pCoexDm->preVal0x6c0 == pCoexDm->curVal0x6c0) &&
			(pCoexDm->preVal0x6c4 == pCoexDm->curVal0x6c4) &&
			(pCoexDm->preVal0x6c8 == pCoexDm->curVal0x6c8) &&
			(pCoexDm->preVal0x6cc == pCoexDm->curVal0x6cc) )
			return;
	}
	halbtc8821a1ant_SetCoexTable(pBtCoexist, val0x6c0, val0x6c4, val0x6c8, val0x6cc);

	pCoexDm->preVal0x6c0 = pCoexDm->curVal0x6c0;
	pCoexDm->preVal0x6c4 = pCoexDm->curVal0x6c4;
	pCoexDm->preVal0x6c8 = pCoexDm->curVal0x6c8;
	pCoexDm->preVal0x6cc = pCoexDm->curVal0x6cc;
}

VOID
halbtc8821a1ant_CoexTableWithType(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN				bForceExec,
	IN	u1Byte				type
	)
{
	switch(type)
	{
		case 0:
			halbtc8821a1ant_CoexTable(pBtCoexist, bForceExec, 0x55555555, 0x55555555, 0xffffff, 0x3);
			break;
		case 1:
			halbtc8821a1ant_CoexTable(pBtCoexist, bForceExec, 0x55555555, 0x5a5a5a5a, 0xffffff, 0x3);
			break;
		case 2:
			halbtc8821a1ant_CoexTable(pBtCoexist, bForceExec, 0x5a5a5a5a, 0x5a5a5a5a, 0xffffff, 0x3);
			break;
		case 3:
			halbtc8821a1ant_CoexTable(pBtCoexist, bForceExec, 0x55555555, 0xaaaaaaaa, 0xffffff, 0x3);
			break;
		case 4:
			halbtc8821a1ant_CoexTable(pBtCoexist, bForceExec, 0xffffffff, 0xffffffff, 0xffffff, 0x3);
			break;
		case 5:
			halbtc8821a1ant_CoexTable(pBtCoexist, bForceExec, 0x5fff5fff, 0x5fff5fff, 0xffffff, 0x3);
			break;
		case 6:
			halbtc8821a1ant_CoexTable(pBtCoexist, bForceExec, 0x55ff55ff, 0x5a5a5a5a, 0xffffff, 0x3);
			break;
		case 7:
			halbtc8821a1ant_CoexTable(pBtCoexist, bForceExec, 0x5afa5afa, 0x5afa5afa, 0xffffff, 0x3);
			break;
		default:
			break;
	}
}

VOID
halbtc8821a1ant_SetFwIgnoreWlanAct(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bEnable
	)
{
	u1Byte			H2C_Parameter[1] ={0};
		
	if(bEnable)
	{
		H2C_Parameter[0] |= BIT0;		// function enable
	}
	
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_EXEC, ("[BTCoex], set FW for BT Ignore Wlan_Act, FW write 0x63=0x%x\n", 
		H2C_Parameter[0]));

	pBtCoexist->fBtcFillH2c(pBtCoexist, 0x63, 1, H2C_Parameter);
}

VOID
halbtc8821a1ant_IgnoreWlanAct(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bForceExec,
	IN	BOOLEAN			bEnable
	)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW, ("[BTCoex], %s turn Ignore WlanAct %s\n", 
		(bForceExec? "force to":""), (bEnable? "ON":"OFF")));
	pCoexDm->bCurIgnoreWlanAct = bEnable;

	if(!bForceExec)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], bPreIgnoreWlanAct = %d, bCurIgnoreWlanAct = %d!!\n", 
			pCoexDm->bPreIgnoreWlanAct, pCoexDm->bCurIgnoreWlanAct));

		if(pCoexDm->bPreIgnoreWlanAct == pCoexDm->bCurIgnoreWlanAct)
			return;
	}
	halbtc8821a1ant_SetFwIgnoreWlanAct(pBtCoexist, bEnable);

	pCoexDm->bPreIgnoreWlanAct = pCoexDm->bCurIgnoreWlanAct;
}

VOID
halbtc8821a1ant_SetFwPstdma(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			byte1,
	IN	u1Byte			byte2,
	IN	u1Byte			byte3,
	IN	u1Byte			byte4,
	IN	u1Byte			byte5
	)
{
	u1Byte			H2C_Parameter[5] ={0};

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
	
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_EXEC, ("[BTCoex], PS-TDMA H2C cmd =0x%x%08x\n", 
		H2C_Parameter[0], 
		H2C_Parameter[1]<<24|H2C_Parameter[2]<<16|H2C_Parameter[3]<<8|H2C_Parameter[4]));

	pBtCoexist->fBtcFillH2c(pBtCoexist, 0x60, 5, H2C_Parameter);
}

VOID
halbtc8821a1ant_SetLpsRpwm(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			lpsVal,
	IN	u1Byte			rpwmVal
	)
{
	u1Byte	lps=lpsVal;
	u1Byte	rpwm=rpwmVal;
	
	pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_U1_LPS_VAL, &lps);
	pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_U1_RPWM_VAL, &rpwm);
}

VOID
halbtc8821a1ant_LpsRpwm(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bForceExec,
	IN	u1Byte			lpsVal,
	IN	u1Byte			rpwmVal
	)
{
	BOOLEAN	bForceExecPwrCmd=FALSE;
	
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW, ("[BTCoex], %s set lps/rpwm=0x%x/0x%x \n", 
		(bForceExec? "force to":""), lpsVal, rpwmVal));
	pCoexDm->curLps = lpsVal;
	pCoexDm->curRpwm = rpwmVal;

	if(!bForceExec)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], LPS-RxBeaconMode=0x%x , LPS-RPWM=0x%x!!\n", 
			 pCoexDm->curLps, pCoexDm->curRpwm));

		if( (pCoexDm->preLps == pCoexDm->curLps) &&
			(pCoexDm->preRpwm == pCoexDm->curRpwm) )
		{
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], LPS-RPWM_Last=0x%x , LPS-RPWM_Now=0x%x!!\n", 
				 pCoexDm->preRpwm, pCoexDm->curRpwm));

			return;
		}
	}
	halbtc8821a1ant_SetLpsRpwm(pBtCoexist, lpsVal, rpwmVal);

	pCoexDm->preLps = pCoexDm->curLps;
	pCoexDm->preRpwm = pCoexDm->curRpwm;
}

VOID
halbtc8821a1ant_SwMechanism(
	IN	PBTC_COEXIST	pBtCoexist,	
	IN	BOOLEAN 	bLowPenaltyRA
	) 
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_MONITOR, ("[BTCoex], SM[LpRA] = %d \n", bLowPenaltyRA));
	
	halbtc8821a1ant_LowPenaltyRa(pBtCoexist, NORMAL_EXEC, bLowPenaltyRA);
}

VOID
halbtc8821a1ant_SetAntPath(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte				antPosType,
	IN	BOOLEAN				bInitHwCfg,
	IN	BOOLEAN				bWifiOff
	)
{
	PBTC_BOARD_INFO pBoardInfo=&pBtCoexist->boardInfo;
	u4Byte			fwVer=0, u4Tmp=0;
	u1Byte			H2C_Parameter[2] ={0};
	
	if(bInitHwCfg)
	{
		// 0x4c[23]=0, 0x4c[24]=1  Antenna control by WL/BT
		u4Tmp = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x4c);
		u4Tmp &=~BIT23;
		u4Tmp |= BIT24;
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x4c, u4Tmp);

		pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0x975, 0x3, 0x3);
		pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0xcb4, 0x77);

		if(pBoardInfo->btdmAntPos == BTC_ANTENNA_AT_MAIN_PORT) 
		{
			//tell firmware "antenna inverse"  ==> WRONG firmware antenna control code.==>need fw to fix
			H2C_Parameter[0] = 1;
			H2C_Parameter[1] = 1;
			pBtCoexist->fBtcFillH2c(pBtCoexist, 0x65, 2, H2C_Parameter);

			pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0x64, 0x1, 0x1); //Main Ant to  BT for IPS case 0x4c[23]=1
		}
		else
		{
			//tell firmware "no antenna inverse" ==> WRONG firmware antenna control code.==>need fw to fix
			H2C_Parameter[0] = 0;
			H2C_Parameter[1] = 1;
			pBtCoexist->fBtcFillH2c(pBtCoexist, 0x65, 2, H2C_Parameter);

			pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0x64, 0x1, 0x0); //Aux Ant to  BT for IPS case 0x4c[23]=1
		}
	}
	else if(bWifiOff)
	{
		// 0x4c[24:23]=00, Set Antenna control by BT_RFE_CTRL	BT Vendor 0xac=0xf002
		u4Tmp = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x4c);
		u4Tmp &= ~BIT23;
		u4Tmp &= ~BIT24;
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x4c, u4Tmp);
	}
	
	// ext switch setting
	switch(antPosType)
	{
		case BTC_ANT_PATH_WIFI:
			if(pBoardInfo->btdmAntPos == BTC_ANTENNA_AT_MAIN_PORT)
				pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0xcb7, 0x30, 0x1);
			else
				pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0xcb7, 0x30, 0x2);
			break;
		case BTC_ANT_PATH_BT:
			if(pBoardInfo->btdmAntPos == BTC_ANTENNA_AT_MAIN_PORT)
				pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0xcb7, 0x30, 0x2);
			else
				pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0xcb7, 0x30, 0x1);
			break;
		default:
		case BTC_ANT_PATH_PTA:
			if(pBoardInfo->btdmAntPos == BTC_ANTENNA_AT_MAIN_PORT)
				pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0xcb7, 0x30, 0x1);
			else
				pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0xcb7, 0x30, 0x2);
			break;
	}
}

VOID
halbtc8821a1ant_PsTdma(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bForceExec,
	IN	BOOLEAN			bTurnOn,
	IN	u1Byte			type
	)
{
	PBTC_BOARD_INFO	pBoardInfo=&pBtCoexist->boardInfo;
	BOOLEAN			bTurnOnByCnt=FALSE;
	u1Byte			psTdmaTypeByCnt=0, rssiAdjustVal=0;
	u4Byte			fwVer=0;

	pCoexDm->bCurPsTdmaOn = bTurnOn;
	pCoexDm->curPsTdma = type;

	if(!bForceExec)
	{
		if (pCoexDm->bCurPsTdmaOn)
		{
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], ********** TDMA(on, %d) **********\n", 
				pCoexDm->curPsTdma));
		}
		else
		{
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], ********** TDMA(off, %d) **********\n", 
				pCoexDm->curPsTdma));
		}
			

		if( (pCoexDm->bPrePsTdmaOn == pCoexDm->bCurPsTdmaOn) &&
			(pCoexDm->prePsTdma == pCoexDm->curPsTdma) )
			return;
	}
	if(bTurnOn)
	{
		switch(type)
		{
			default:
				halbtc8821a1ant_SetFwPstdma(pBtCoexist, 0x51, 0x1a, 0x1a, 0x0, 0x50);
				break;
			case 1:
				halbtc8821a1ant_SetFwPstdma(pBtCoexist, 0x51, 0x3a, 0x03, 0x10, 0x50);
				rssiAdjustVal = 11;
				break;
			case 2:
				halbtc8821a1ant_SetFwPstdma(pBtCoexist, 0x51, 0x2b, 0x03, 0x10, 0x50);
				rssiAdjustVal = 14;
				break;
			case 3:
				halbtc8821a1ant_SetFwPstdma(pBtCoexist, 0x51, 0x1d, 0x1d, 0x0, 0x10);
				break;
			case 4:
				halbtc8821a1ant_SetFwPstdma(pBtCoexist, 0x93, 0x15, 0x3, 0x14, 0x0);
				rssiAdjustVal = 17;
				break;
			case 5:
				halbtc8821a1ant_SetFwPstdma(pBtCoexist, 0x61, 0x15, 0x3, 0x11, 0x10);
				break;
			case 6:
				halbtc8821a1ant_SetFwPstdma(pBtCoexist, 0x13, 0xa, 0x3, 0x0, 0x0);
				break;
			case 7:
				halbtc8821a1ant_SetFwPstdma(pBtCoexist, 0x13, 0xc, 0x5, 0x0, 0x0);
				break;
			case 8:	
				halbtc8821a1ant_SetFwPstdma(pBtCoexist, 0x93, 0x25, 0x3, 0x10, 0x0);
				break;
			case 9:	
				halbtc8821a1ant_SetFwPstdma(pBtCoexist, 0x51, 0x21, 0x3, 0x10, 0x50);
				rssiAdjustVal = 18;
				break;
			case 10:	
				halbtc8821a1ant_SetFwPstdma(pBtCoexist, 0x13, 0xa, 0xa, 0x0, 0x40);
				break;
			case 11:	
				halbtc8821a1ant_SetFwPstdma(pBtCoexist, 0x51, 0x14, 0x03, 0x10, 0x10);
				rssiAdjustVal = 20;
				break;
			case 12:
				halbtc8821a1ant_SetFwPstdma(pBtCoexist, 0x51, 0x0a, 0x0a, 0x0, 0x50);
				break;
			case 13:
				halbtc8821a1ant_SetFwPstdma(pBtCoexist, 0x51, 0x18, 0x18, 0x0, 0x10);
				break;
			case 14:
				halbtc8821a1ant_SetFwPstdma(pBtCoexist, 0x51, 0x21, 0x3, 0x10, 0x10);
				break;
			case 15:
				halbtc8821a1ant_SetFwPstdma(pBtCoexist, 0x13, 0xa, 0x3, 0x8, 0x0);
				break;
			case 16:
				halbtc8821a1ant_SetFwPstdma(pBtCoexist, 0x93, 0x15, 0x3, 0x10, 0x0);
				rssiAdjustVal = 18;
				break;
			case 18:
				halbtc8821a1ant_SetFwPstdma(pBtCoexist, 0x93, 0x25, 0x3, 0x10, 0x0);
				rssiAdjustVal = 14;
				break;			
			case 20:
				halbtc8821a1ant_SetFwPstdma(pBtCoexist, 0x61, 0x35, 0x03, 0x11, 0x10);
				break;
			case 21:
				halbtc8821a1ant_SetFwPstdma(pBtCoexist, 0x61, 0x15, 0x03, 0x11, 0x10);
				break;
			case 22:
				halbtc8821a1ant_SetFwPstdma(pBtCoexist, 0x61, 0x25, 0x03, 0x11, 0x10);
				break;
			case 23:
				halbtc8821a1ant_SetFwPstdma(pBtCoexist, 0xe3, 0x25, 0x3, 0x31, 0x18);
				rssiAdjustVal = 22;
				break;
			case 24:
				halbtc8821a1ant_SetFwPstdma(pBtCoexist, 0xe3, 0x15, 0x3, 0x31, 0x18);
				rssiAdjustVal = 22;
				break;
			case 25:
				halbtc8821a1ant_SetFwPstdma(pBtCoexist, 0xe3, 0xa, 0x3, 0x31, 0x18);
				rssiAdjustVal = 22;
				break;
			case 26:
				halbtc8821a1ant_SetFwPstdma(pBtCoexist, 0xe3, 0xa, 0x3, 0x31, 0x18);
				rssiAdjustVal = 22;
				break;
			case 27:
				halbtc8821a1ant_SetFwPstdma(pBtCoexist, 0xe3, 0x25, 0x3, 0x31, 0x98);
				rssiAdjustVal = 22;
				break;
			case 28:
				halbtc8821a1ant_SetFwPstdma(pBtCoexist, 0x69, 0x25, 0x3, 0x31, 0x0);
				break;
			case 29:
				halbtc8821a1ant_SetFwPstdma(pBtCoexist, 0xab, 0x1a, 0x1a, 0x1, 0x10);
				break;
			case 30:
				halbtc8821a1ant_SetFwPstdma(pBtCoexist, 0x51, 0x14, 0x3, 0x10, 0x50);
				break;
			case 31:
				halbtc8821a1ant_SetFwPstdma(pBtCoexist, 0xd3, 0x1a, 0x1a, 0, 0x58);
				break;
			case 32:
				halbtc8821a1ant_SetFwPstdma(pBtCoexist, 0x61, 0xa, 0x3, 0x10, 0x0);
				break;
			case 33:
				halbtc8821a1ant_SetFwPstdma(pBtCoexist, 0xa3, 0x25, 0x3, 0x30, 0x90);
				break;
			case 34:
				halbtc8821a1ant_SetFwPstdma(pBtCoexist, 0x53, 0x1a, 0x1a, 0x0, 0x10);
				break;
			case 35:
				halbtc8821a1ant_SetFwPstdma(pBtCoexist, 0x63, 0x1a, 0x1a, 0x0, 0x10);
				break;
			case 36:
				halbtc8821a1ant_SetFwPstdma(pBtCoexist, 0xd3, 0x12, 0x3, 0x14, 0x50);
				break;
		}
	}
	else
	{
		// disable PS tdma
		switch(type)
		{
			case 8: //PTA Control
				halbtc8821a1ant_SetFwPstdma(pBtCoexist, 0x8, 0x0, 0x0, 0x0, 0x0);
				halbtc8821a1ant_SetAntPath(pBtCoexist, BTC_ANT_PATH_PTA, FALSE, FALSE);
				break;
			case 0:
			default:  //Software control, Antenna at BT side
				halbtc8821a1ant_SetFwPstdma(pBtCoexist, 0x0, 0x0, 0x0, 0x0, 0x0);
				halbtc8821a1ant_SetAntPath(pBtCoexist, BTC_ANT_PATH_BT, FALSE, FALSE);
				break;
			case 9:   //Software control, Antenna at WiFi side
				halbtc8821a1ant_SetFwPstdma(pBtCoexist, 0x0, 0x0, 0x0, 0x0, 0x0);
				halbtc8821a1ant_SetAntPath(pBtCoexist, BTC_ANT_PATH_WIFI, FALSE, FALSE);
				break;
			case 10:	// under 5G
				halbtc8821a1ant_SetFwPstdma(pBtCoexist, 0x0, 0x0, 0x0, 0x8, 0x0);
				halbtc8821a1ant_SetAntPath(pBtCoexist, BTC_ANT_PATH_BT, FALSE, FALSE);
				break;
		}
	}
	rssiAdjustVal =0;
	pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_U1_RSSI_ADJ_VAL_FOR_1ANT_COEX_TYPE, &rssiAdjustVal);

	// update pre state
	pCoexDm->bPrePsTdmaOn = pCoexDm->bCurPsTdmaOn;
	pCoexDm->prePsTdma = pCoexDm->curPsTdma;
}

VOID
halbtc8821a1ant_CoexAllOff(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	// sw all off
	halbtc8821a1ant_SwMechanism(pBtCoexist, FALSE);

	// hw all off
	halbtc8821a1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 0);
}

BOOLEAN
halbtc8821a1ant_IsCommonAction(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	BOOLEAN			bCommon=FALSE, bWifiConnected=FALSE, bWifiBusy=FALSE;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_CONNECTED, &bWifiConnected);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_BUSY, &bWifiBusy);

	if(!bWifiConnected && 
		BT_8821A_1ANT_BT_STATUS_NON_CONNECTED_IDLE == pCoexDm->btStatus)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Wifi non connected-idle + BT non connected-idle!!\n"));
 		halbtc8821a1ant_SwMechanism(pBtCoexist, FALSE);

		bCommon = TRUE;
	}
	else if(bWifiConnected && 
			(BT_8821A_1ANT_BT_STATUS_NON_CONNECTED_IDLE == pCoexDm->btStatus) )
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Wifi connected + BT non connected-idle!!\n"));
		halbtc8821a1ant_SwMechanism(pBtCoexist, FALSE);

		bCommon = TRUE;
	}
	else if(!bWifiConnected && 
		(BT_8821A_1ANT_BT_STATUS_CONNECTED_IDLE == pCoexDm->btStatus) )
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Wifi non connected-idle + BT connected-idle!!\n"));
		halbtc8821a1ant_SwMechanism(pBtCoexist, FALSE);

		bCommon = TRUE;
	}
	else if(bWifiConnected && 
		(BT_8821A_1ANT_BT_STATUS_CONNECTED_IDLE == pCoexDm->btStatus) )
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Wifi connected + BT connected-idle!!\n"));
		halbtc8821a1ant_SwMechanism(pBtCoexist, FALSE);

		bCommon = TRUE;
	}
	else if(!bWifiConnected && 
		(BT_8821A_1ANT_BT_STATUS_CONNECTED_IDLE != pCoexDm->btStatus) )
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Wifi non connected-idle + BT Busy!!\n"));
		halbtc8821a1ant_SwMechanism(pBtCoexist, FALSE);
		
		bCommon = TRUE;
	}
	else
	{
		if (bWifiBusy)			
		{
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Wifi Connected-Busy + BT Busy!!\n"));
		}
		else
		{
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Wifi Connected-Idle + BT Busy!!\n"));
		}
		
		bCommon = FALSE;
	}
	
	return bCommon;
}


VOID
halbtc8821a1ant_TdmaDurationAdjustForAcl(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte				wifiStatus
	)
{
	static s4Byte		up,dn,m,n,WaitCount;
	s4Byte			result;   //0: no change, +1: increase WiFi duration, -1: decrease WiFi duration
	u1Byte			retryCount=0, btInfoExt;

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW, ("[BTCoex], TdmaDurationAdjustForAcl()\n"));

	if( (BT_8821A_1ANT_WIFI_STATUS_NON_CONNECTED_ASSO_AUTH_SCAN == wifiStatus) ||
		(BT_8821A_1ANT_WIFI_STATUS_CONNECTED_SCAN == wifiStatus) ||
		(BT_8821A_1ANT_WIFI_STATUS_CONNECTED_SPECIAL_PKT == wifiStatus) )
	{
		if( pCoexDm->curPsTdma != 1 &&
			pCoexDm->curPsTdma != 2 &&
			pCoexDm->curPsTdma != 3 &&
			pCoexDm->curPsTdma != 9 )
		{
			halbtc8821a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 9);
			pCoexDm->psTdmaDuAdjType = 9;

			up = 0;
			dn = 0;
			m = 1;
			n= 3;
			result = 0;
			WaitCount = 0;
		}		
		return;
	}
	
	if(!pCoexDm->bAutoTdmaAdjust)
	{
		pCoexDm->bAutoTdmaAdjust = TRUE;
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], first run TdmaDurationAdjust()!!\n"));

		halbtc8821a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 2);
		pCoexDm->psTdmaDuAdjType = 2;
		//============
		up = 0;
		dn = 0;
		m = 1;
		n= 3;
		result = 0;
		WaitCount = 0;
	}
	else
	{
		//accquire the BT TRx retry count from BT_Info byte2
		retryCount = pCoexSta->btRetryCnt;
		btInfoExt = pCoexSta->btInfoExt;
		//BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], retryCount = %d\n", retryCount));
		//BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], up=%d, dn=%d, m=%d, n=%d, WaitCount=%d\n", 
		//	up, dn, m, n, WaitCount));
		result = 0;
		WaitCount++; 
		  
		if(retryCount == 0)  // no retry in the last 2-second duration
		{
			up++;
			dn--;

			if (dn <= 0)
				dn = 0;				 

			if(up >= n)	// if 連續 n 個2秒 retry count為0, 則調寬WiFi duration
			{
				WaitCount = 0; 
				n = 3;
				up = 0;
				dn = 0;
				result = 1; 
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], Increase wifi duration!!\n"));
			}
		}
		else if (retryCount <= 3)	// <=3 retry in the last 2-second duration
		{
			up--; 
			dn++;

			if (up <= 0)
				up = 0;

			if (dn == 2)	// if 連續 2 個2秒 retry count< 3, 則調窄WiFi duration
			{
				if (WaitCount <= 2)
					m++; // 避免一直在兩個level中來回
				else
					m = 1;

				if ( m >= 20) //m 最大值 = 20 ' 最大120秒 recheck是否調整 WiFi duration.
					m = 20;

				n = 3*m;
				up = 0;
				dn = 0;
				WaitCount = 0;
				result = -1; 
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], Decrease wifi duration for retryCounter<3!!\n"));
			}
		}
		else  //retry count > 3, 只要1次 retry count > 3, 則調窄WiFi duration
		{
			if (WaitCount == 1)
				m++; // 避免一直在兩個level中來回
			else
				m = 1;

			if ( m >= 20) //m 最大值 = 20 ' 最大120秒 recheck是否調整 WiFi duration.
				m = 20;

			n = 3*m;
			up = 0;
			dn = 0;
			WaitCount = 0; 
			result = -1;
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], Decrease wifi duration for retryCounter>3!!\n"));
		}

		if(result == -1)
		{
			if( (BT_INFO_8821A_1ANT_A2DP_BASIC_RATE(btInfoExt)) &&
				((pCoexDm->curPsTdma == 1) ||(pCoexDm->curPsTdma == 2)) )
			{
				halbtc8821a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 9);
				pCoexDm->psTdmaDuAdjType = 9;
			}
			else if(pCoexDm->curPsTdma == 1)
			{
				halbtc8821a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 2);
				pCoexDm->psTdmaDuAdjType = 2;
			}
			else if(pCoexDm->curPsTdma == 2)
			{
				halbtc8821a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 9);
				pCoexDm->psTdmaDuAdjType = 9;
			}
			else if(pCoexDm->curPsTdma == 9)
			{
				halbtc8821a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 11);
				pCoexDm->psTdmaDuAdjType = 11;
			}
		}
		else if(result == 1)
		{
			if( (BT_INFO_8821A_1ANT_A2DP_BASIC_RATE(btInfoExt)) &&
				((pCoexDm->curPsTdma == 1) ||(pCoexDm->curPsTdma == 2)) )
			{
				halbtc8821a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 9);
				pCoexDm->psTdmaDuAdjType = 9;
			}
			else if(pCoexDm->curPsTdma == 11)
			{
				halbtc8821a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 9);
				pCoexDm->psTdmaDuAdjType = 9;
			}
			else if(pCoexDm->curPsTdma == 9)
			{
				halbtc8821a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 2);
				pCoexDm->psTdmaDuAdjType = 2;
			}
			else if(pCoexDm->curPsTdma == 2)
			{
				halbtc8821a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 1);
				pCoexDm->psTdmaDuAdjType = 1;
			}
		}
		else	  //no change
		{
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], ********** TDMA(on, %d) **********\n", 
				pCoexDm->curPsTdma));
		}

		if( pCoexDm->curPsTdma != 1 &&
			pCoexDm->curPsTdma != 2 &&
			pCoexDm->curPsTdma != 9 &&
			pCoexDm->curPsTdma != 11 )
		{
			// recover to previous adjust type
			halbtc8821a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, pCoexDm->psTdmaDuAdjType);
		}
	}
}

VOID
halbtc8821a1ant_PsTdmaCheckForPowerSaveState(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bNewPsState
	)
{
	u1Byte	lpsMode=0x0;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U1_LPS_MODE, &lpsMode);
	
	if(lpsMode)	// already under LPS state
	{
		if(bNewPsState)		
		{
			// keep state under LPS, do nothing.
		}
		else
		{
			// will leave LPS state, turn off psTdma first
			halbtc8821a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, FALSE, 0);
		}
	}
	else						// NO PS state
	{
		if(bNewPsState)
		{
			// will enter LPS state, turn off psTdma first
			halbtc8821a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, FALSE, 0);
		}
		else
		{
			// keep state under NO PS state, do nothing.
		}
	}
}

VOID
halbtc8821a1ant_PowerSaveState(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte				psType,
	IN	u1Byte				lpsVal,
	IN	u1Byte				rpwmVal
	)
{
	BOOLEAN		bLowPwrDisable=FALSE;

	switch(psType)
	{
		case BTC_PS_WIFI_NATIVE:
			// recover to original 32k low power setting
			bLowPwrDisable = FALSE;
			pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_ACT_DISABLE_LOW_POWER, &bLowPwrDisable);
			pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_ACT_NORMAL_LPS, NULL);
			break;
		case BTC_PS_LPS_ON:
			halbtc8821a1ant_PsTdmaCheckForPowerSaveState(pBtCoexist, TRUE);
			halbtc8821a1ant_LpsRpwm(pBtCoexist, NORMAL_EXEC, lpsVal, rpwmVal);
			// when coex force to enter LPS, do not enter 32k low power.
			bLowPwrDisable = TRUE;
			pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_ACT_DISABLE_LOW_POWER, &bLowPwrDisable);
			// power save must executed before psTdma.
			pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_ACT_ENTER_LPS, NULL);
			break;
		case BTC_PS_LPS_OFF:
			halbtc8821a1ant_PsTdmaCheckForPowerSaveState(pBtCoexist, FALSE);
			pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_ACT_LEAVE_LPS, NULL);
			break;
		default:
			break;
	}
}

VOID
halbtc8821a1ant_CoexUnder5G(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	halbtc8821a1ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);

	halbtc8821a1ant_IgnoreWlanAct(pBtCoexist, NORMAL_EXEC, TRUE);

	halbtc8821a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, FALSE, 10);

	halbtc8821a1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 0);

	halbtc8821a1ant_LimitedTx(pBtCoexist, NORMAL_EXEC, 0, 0, 0, 0);

	halbtc8821a1ant_LimitedRx(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 5);
}

VOID
halbtc8821a1ant_ActionWifiOnly(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	halbtc8821a1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 0);
	halbtc8821a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, FALSE, 9);	
}

VOID
halbtc8821a1ant_MonitorBtEnableDisable(
	IN 	PBTC_COEXIST		pBtCoexist
	)
{
	static BOOLEAN	bPreBtDisabled=FALSE;
	static u4Byte		btDisableCnt=0;
	BOOLEAN			bBtActive=TRUE, bBtDisabled=FALSE;

	// This function check if bt is disabled

	if(	pCoexSta->highPriorityTx == 0 &&
		pCoexSta->highPriorityRx == 0 &&
		pCoexSta->lowPriorityTx == 0 &&
		pCoexSta->lowPriorityRx == 0)
	{
		bBtActive = FALSE;
	}
	if(	pCoexSta->highPriorityTx == 0xffff &&
		pCoexSta->highPriorityRx == 0xffff &&
		pCoexSta->lowPriorityTx == 0xffff &&
		pCoexSta->lowPriorityRx == 0xffff)
	{
		bBtActive = FALSE;
	}
	if(bBtActive)
	{
		btDisableCnt = 0;
		bBtDisabled = FALSE;
		pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_BL_BT_DISABLE, &bBtDisabled);
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_MONITOR, ("[BTCoex], BT is enabled !!\n"));
	}
	else
	{
		btDisableCnt++;
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_MONITOR, ("[BTCoex], bt all counters=0, %d times!!\n", 
				btDisableCnt));
		if(btDisableCnt >= 2)
		{
			bBtDisabled = TRUE;
			pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_BL_BT_DISABLE, &bBtDisabled);
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_MONITOR, ("[BTCoex], BT is disabled !!\n"));
			halbtc8821a1ant_ActionWifiOnly(pBtCoexist);
		}
	}
	if(bPreBtDisabled != bBtDisabled)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_MONITOR, ("[BTCoex], BT is from %s to %s!!\n", 
			(bPreBtDisabled ? "disabled":"enabled"), 
			(bBtDisabled ? "disabled":"enabled")));
		bPreBtDisabled = bBtDisabled;
		if(!bBtDisabled)
		{
		}
		else
		{
			pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_ACT_LEAVE_LPS, NULL);
			pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_ACT_NORMAL_LPS, NULL);
		}
	}
}

//=============================================
//
//	Software Coex Mechanism start
//
//=============================================

// SCO only or SCO+PAN(HS)
VOID
halbtc8821a1ant_ActionSco(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	halbtc8821a1ant_SwMechanism(pBtCoexist, TRUE);
}

VOID
halbtc8821a1ant_ActionHid(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	halbtc8821a1ant_SwMechanism(pBtCoexist, TRUE);
}

//A2DP only / PAN(EDR) only/ A2DP+PAN(HS)
VOID
halbtc8821a1ant_ActionA2dp(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	halbtc8821a1ant_SwMechanism(pBtCoexist, FALSE);
}

VOID
halbtc8821a1ant_ActionA2dpPanHs(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	halbtc8821a1ant_SwMechanism(pBtCoexist, FALSE);
}

VOID
halbtc8821a1ant_ActionPanEdr(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	halbtc8821a1ant_SwMechanism(pBtCoexist, FALSE);
}

//PAN(HS) only
VOID
halbtc8821a1ant_ActionPanHs(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	halbtc8821a1ant_SwMechanism(pBtCoexist, FALSE);
}

//PAN(EDR)+A2DP
VOID
halbtc8821a1ant_ActionPanEdrA2dp(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	halbtc8821a1ant_SwMechanism(pBtCoexist, FALSE);
}

VOID
halbtc8821a1ant_ActionPanEdrHid(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	halbtc8821a1ant_SwMechanism(pBtCoexist, TRUE);
}

// HID+A2DP+PAN(EDR)
VOID
halbtc8821a1ant_ActionHidA2dpPanEdr(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	halbtc8821a1ant_SwMechanism(pBtCoexist, TRUE);
}

VOID
halbtc8821a1ant_ActionHidA2dp(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	halbtc8821a1ant_SwMechanism(pBtCoexist, TRUE);
}

//=============================================
//
//	Non-Software Coex Mechanism start
//
//=============================================

VOID
halbtc8821a1ant_ActionHs(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	halbtc8821a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 5);
	halbtc8821a1ant_CoexTableWithType(pBtCoexist, FORCE_EXEC, 2);
}

VOID
halbtc8821a1ant_ActionBtInquiry(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	PBTC_BT_LINK_INFO pBtLinkInfo=&pBtCoexist->btLinkInfo;
	BOOLEAN			bWifiConnected=FALSE;
	
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_CONNECTED, &bWifiConnected);

	if(!bWifiConnected)
	{
		halbtc8821a1ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);
		halbtc8821a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 5);
		halbtc8821a1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 1);
	}
	else if( (pBtLinkInfo->bScoExist) ||
			(pBtLinkInfo->bHidOnly) )
	{
		// SCO/HID-only busy
		halbtc8821a1ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);
		halbtc8821a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 32);
		halbtc8821a1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 1);
	}
	else
	{
		halbtc8821a1ant_PowerSaveState(pBtCoexist, BTC_PS_LPS_ON, 0x50, 0x4);
		halbtc8821a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 30);
		halbtc8821a1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 1);
	}
}

VOID
halbtc8821a1ant_ActionBtScoHidOnlyBusy(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte				wifiStatus
	)
{
	// tdma and coex table
	halbtc8821a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 5);

	if(BT_8821A_1ANT_WIFI_STATUS_NON_CONNECTED_ASSO_AUTH_SCAN == wifiStatus)
		halbtc8821a1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 1);		
	else
		halbtc8821a1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 1);
}

VOID
halbtc8821a1ant_ActionWifiConnectedBtAclBusy(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte				wifiStatus
	)
{
	u1Byte		btRssiState;
	
	PBTC_BT_LINK_INFO pBtLinkInfo=&pBtCoexist->btLinkInfo;
	btRssiState = halbtc8821a1ant_BtRssiState(2, 28, 0);	

	if(pBtLinkInfo->bHidOnly)  //HID
	{
		halbtc8821a1ant_ActionBtScoHidOnlyBusy(pBtCoexist, wifiStatus);
		pCoexDm->bAutoTdmaAdjust = FALSE;
		return;
	}
	else if(pBtLinkInfo->bA2dpOnly)  //A2DP		
	{
		if( (btRssiState == BTC_RSSI_STATE_HIGH) ||
			(btRssiState == BTC_RSSI_STATE_STAY_HIGH) )	
		{
 			 halbtc8821a1ant_TdmaDurationAdjustForAcl(pBtCoexist, wifiStatus);
		}
		else //for low BT RSSI
		{
			halbtc8821a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 11);
			pCoexDm->bAutoTdmaAdjust = FALSE;
		}		   	

		halbtc8821a1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 1);
	}
	else if(pBtLinkInfo->bHidExist&&pBtLinkInfo->bA2dpExist)  //HID+A2DP
	{
		if( (btRssiState == BTC_RSSI_STATE_HIGH) ||
			(btRssiState == BTC_RSSI_STATE_STAY_HIGH) )	
		{
			halbtc8821a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 14);
			pCoexDm->bAutoTdmaAdjust = FALSE;
		}
		else //for low BT RSSI
		{
			halbtc8821a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 11);
			pCoexDm->bAutoTdmaAdjust = FALSE;
		}		   	

		halbtc8821a1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 1);
	}
	else if( (pBtLinkInfo->bPanOnly) || (pBtLinkInfo->bHidExist&&pBtLinkInfo->bPanExist) ) //PAN(OPP,FTP), HID+PAN(OPP,FTP)			
	{
		halbtc8821a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 3);
		halbtc8821a1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 1);
		pCoexDm->bAutoTdmaAdjust = FALSE;
	}
	else if ( ((pBtLinkInfo->bA2dpExist) && (pBtLinkInfo->bPanExist)) ||
		       (pBtLinkInfo->bHidExist&&pBtLinkInfo->bA2dpExist&&pBtLinkInfo->bPanExist) ) //A2DP+PAN(OPP,FTP), HID+A2DP+PAN(OPP,FTP)
	{
		halbtc8821a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 13);
		halbtc8821a1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 1);
		pCoexDm->bAutoTdmaAdjust = FALSE;
	}
	else
	{		
		halbtc8821a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 11);
		halbtc8821a1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 1);
		pCoexDm->bAutoTdmaAdjust = FALSE;
	}	
}

VOID
halbtc8821a1ant_ActionWifiNotConnected(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	// power save state
	halbtc8821a1ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);

	// tdma and coex table	
	halbtc8821a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, FALSE, 8);
	halbtc8821a1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 0);
}

VOID
halbtc8821a1ant_ActionWifiNotConnectedAssoAuthScan(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	halbtc8821a1ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);


	halbtc8821a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 22);
	halbtc8821a1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 1);
}

VOID
halbtc8821a1ant_ActionWifiConnectedScan(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	PBTC_BT_LINK_INFO pBtLinkInfo=&pBtCoexist->btLinkInfo;
	
	// power save state
	halbtc8821a1ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);

	// tdma and coex table
	if(BT_8821A_1ANT_BT_STATUS_ACL_BUSY == pCoexDm->btStatus)
	{
		if(pBtLinkInfo->bA2dpExist && pBtLinkInfo->bPanExist)
	 	{
			halbtc8821a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 22);
			halbtc8821a1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 1);
	 	}
	 	else
	 	{
		halbtc8821a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 20);
		halbtc8821a1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 1);
	}
	}
	else if( (BT_8821A_1ANT_BT_STATUS_SCO_BUSY == pCoexDm->btStatus) ||
			(BT_8821A_1ANT_BT_STATUS_ACL_SCO_BUSY == pCoexDm->btStatus) )
	{
		halbtc8821a1ant_ActionBtScoHidOnlyBusy(pBtCoexist,
			BT_8821A_1ANT_WIFI_STATUS_CONNECTED_SCAN);
	}
	else
	{
		halbtc8821a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 20);
		halbtc8821a1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 1);
	}
}

VOID
halbtc8821a1ant_ActionWifiConnectedSpecialPacket(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	BOOLEAN 		bHsConnecting=FALSE;
	PBTC_BT_LINK_INFO pBtLinkInfo=&pBtCoexist->btLinkInfo;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_HS_CONNECTING, &bHsConnecting);

	halbtc8821a1ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);

	// tdma and coex table
	if(BT_8821A_1ANT_BT_STATUS_ACL_BUSY == pCoexDm->btStatus)
	{
		if(pBtLinkInfo->bA2dpExist && pBtLinkInfo->bPanExist)
	 	{
			halbtc8821a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 22);
			halbtc8821a1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 1);
	 	}
	 	else
	 	{
			halbtc8821a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 20);
			halbtc8821a1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 1);
	 	}
	}
	else
	{
		halbtc8821a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 20);
		halbtc8821a1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 1);
	}
}

VOID
halbtc8821a1ant_ActionWifiConnected(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	BOOLEAN 	bWifiBusy=FALSE;
	BOOLEAN 	bScan=FALSE, bLink=FALSE, bRoam=FALSE;
	BOOLEAN 	bUnder4way=FALSE;
	u4Byte		wifiBw;

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], CoexForWifiConnect()===>\n"));

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_4_WAY_PROGRESS, &bUnder4way);
	if(bUnder4way)
	{
		halbtc8821a1ant_ActionWifiConnectedSpecialPacket(pBtCoexist);
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], CoexForWifiConnect(), return for wifi is under 4way<===\n"));
		return;
	}
	
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_SCAN, &bScan);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_LINK, &bLink);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_ROAM, &bRoam);
	if(bScan || bLink || bRoam)
	{
		halbtc8821a1ant_ActionWifiConnectedScan(pBtCoexist);
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], CoexForWifiConnect(), return for wifi is under scan<===\n"));
		return;
	}

	// power save state
	if(BT_8821A_1ANT_BT_STATUS_ACL_BUSY == pCoexDm->btStatus && !pBtCoexist->btLinkInfo.bHidOnly)
		halbtc8821a1ant_PowerSaveState(pBtCoexist, BTC_PS_LPS_ON, 0x50, 0x4);
	else
		halbtc8821a1ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);

	// tdma and coex table
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_BUSY, &bWifiBusy);	
	if(!bWifiBusy)
	{
		if(BT_8821A_1ANT_BT_STATUS_ACL_BUSY == pCoexDm->btStatus)
		{
			halbtc8821a1ant_ActionWifiConnectedBtAclBusy(pBtCoexist, 
				BT_8821A_1ANT_WIFI_STATUS_CONNECTED_IDLE);
		}
		else if( (BT_8821A_1ANT_BT_STATUS_SCO_BUSY == pCoexDm->btStatus) ||
			(BT_8821A_1ANT_BT_STATUS_ACL_SCO_BUSY == pCoexDm->btStatus) )
		{
			halbtc8821a1ant_ActionBtScoHidOnlyBusy(pBtCoexist,
				BT_8821A_1ANT_WIFI_STATUS_CONNECTED_IDLE);
		}
		else
		{
			halbtc8821a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 5);
			halbtc8821a1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 2);
		}
	}
	else
	{
		if(BT_8821A_1ANT_BT_STATUS_ACL_BUSY == pCoexDm->btStatus)
		{
			halbtc8821a1ant_ActionWifiConnectedBtAclBusy(pBtCoexist,
				BT_8821A_1ANT_WIFI_STATUS_CONNECTED_BUSY);
		}
		else if( (BT_8821A_1ANT_BT_STATUS_SCO_BUSY == pCoexDm->btStatus) ||
			(BT_8821A_1ANT_BT_STATUS_ACL_SCO_BUSY == pCoexDm->btStatus) )
		{
			halbtc8821a1ant_ActionBtScoHidOnlyBusy(pBtCoexist,
				BT_8821A_1ANT_WIFI_STATUS_CONNECTED_BUSY);
		}
		else 
		{
			halbtc8821a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 5);
			halbtc8821a1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 2);
		}
	}
}

VOID
halbtc8821a1ant_RunSwCoexistMechanism(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	u1Byte	algorithm=0;

	algorithm = halbtc8821a1ant_ActionAlgorithm(pBtCoexist);
	pCoexDm->curAlgorithm = algorithm;

	if(halbtc8821a1ant_IsCommonAction(pBtCoexist))
	{

	}
	else
	{
		switch(pCoexDm->curAlgorithm)
		{
			case BT_8821A_1ANT_COEX_ALGO_SCO:
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action algorithm = SCO.\n"));
				halbtc8821a1ant_ActionSco(pBtCoexist);
				break;
			case BT_8821A_1ANT_COEX_ALGO_HID:
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action algorithm = HID.\n"));
				halbtc8821a1ant_ActionHid(pBtCoexist);
				break;
			case BT_8821A_1ANT_COEX_ALGO_A2DP:
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action algorithm = A2DP.\n"));
				halbtc8821a1ant_ActionA2dp(pBtCoexist);
				break;
			case BT_8821A_1ANT_COEX_ALGO_A2DP_PANHS:
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action algorithm = A2DP+PAN(HS).\n"));
				halbtc8821a1ant_ActionA2dpPanHs(pBtCoexist);
				break;
			case BT_8821A_1ANT_COEX_ALGO_PANEDR:
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action algorithm = PAN(EDR).\n"));
				halbtc8821a1ant_ActionPanEdr(pBtCoexist);
				break;
			case BT_8821A_1ANT_COEX_ALGO_PANHS:
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action algorithm = HS mode.\n"));
				halbtc8821a1ant_ActionPanHs(pBtCoexist);
				break;
			case BT_8821A_1ANT_COEX_ALGO_PANEDR_A2DP:
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action algorithm = PAN+A2DP.\n"));
				halbtc8821a1ant_ActionPanEdrA2dp(pBtCoexist);
				break;
			case BT_8821A_1ANT_COEX_ALGO_PANEDR_HID:
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action algorithm = PAN(EDR)+HID.\n"));
				halbtc8821a1ant_ActionPanEdrHid(pBtCoexist);
				break;
			case BT_8821A_1ANT_COEX_ALGO_HID_A2DP_PANEDR:
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action algorithm = HID+A2DP+PAN.\n"));
				halbtc8821a1ant_ActionHidA2dpPanEdr(pBtCoexist);
				break;
			case BT_8821A_1ANT_COEX_ALGO_HID_A2DP:
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action algorithm = HID+A2DP.\n"));
				halbtc8821a1ant_ActionHidA2dp(pBtCoexist);
				break;
			default:
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action algorithm = coexist All Off!!\n"));
				//halbtc8821a1ant_CoexAllOff(pBtCoexist);
				break;
		}
		pCoexDm->preAlgorithm = pCoexDm->curAlgorithm;
	}
}

VOID
halbtc8821a1ant_RunCoexistMechanism(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	PBTC_BT_LINK_INFO pBtLinkInfo=&pBtCoexist->btLinkInfo;
	BOOLEAN	bWifiConnected=FALSE, bBtHsOn=FALSE;
	BOOLEAN	bIncreaseScanDevNum=FALSE;
	BOOLEAN	bBtCtrlAggBufSize=FALSE;
	u1Byte	aggBufSize=5;
	u1Byte	wifiRssiState=BTC_RSSI_STATE_HIGH;
	BOOLEAN	bWifiUnder5G=FALSE;

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], RunCoexistMechanism()===>\n"));

	if(pBtCoexist->bManualControl)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], RunCoexistMechanism(), return for Manual CTRL <===\n"));
		return;
	}

	if(pBtCoexist->bStopCoexDm)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], RunCoexistMechanism(), return for Stop Coex DM <===\n"));
		return;
	}

	if(pCoexSta->bUnderIps)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], wifi is under IPS !!!\n"));
		return;
	}

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_UNDER_5G, &bWifiUnder5G);
	if(bWifiUnder5G)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], RunCoexistMechanism(), return for 5G <===\n"));
		halbtc8821a1ant_CoexUnder5G(pBtCoexist);
		return;
	}

	if( (BT_8821A_1ANT_BT_STATUS_ACL_BUSY == pCoexDm->btStatus) ||
		(BT_8821A_1ANT_BT_STATUS_SCO_BUSY == pCoexDm->btStatus) ||
		(BT_8821A_1ANT_BT_STATUS_ACL_SCO_BUSY == pCoexDm->btStatus) )
	{
		bIncreaseScanDevNum = TRUE;
	}

	pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_BL_INC_SCAN_DEV_NUM, &bIncreaseScanDevNum);

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_CONNECTED, &bWifiConnected);

	if(!pBtLinkInfo->bScoExist && !pBtLinkInfo->bHidExist)
	{
		halbtc8821a1ant_LimitedTx(pBtCoexist, NORMAL_EXEC, 0, 0, 0, 0);
	}
	else
	{
		if(bWifiConnected)
		{
			wifiRssiState = halbtc8821a1ant_WifiRssiState(pBtCoexist, 1, 2, 30, 0);
			if( (wifiRssiState == BTC_RSSI_STATE_HIGH) ||
				(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH) )
			{
				halbtc8821a1ant_LimitedTx(pBtCoexist, NORMAL_EXEC, 1, 1, 1, 1);
			}
			else
			{
				halbtc8821a1ant_LimitedTx(pBtCoexist, NORMAL_EXEC, 1, 1, 1, 1);
			}
		}
		else
		{
			halbtc8821a1ant_LimitedTx(pBtCoexist, NORMAL_EXEC, 0, 0, 0, 0);
		}

	}

	if(pBtLinkInfo->bScoExist)
	{
		bBtCtrlAggBufSize = TRUE;
		aggBufSize = 0x3;
	}
	else if(pBtLinkInfo->bHidExist)
	{
		bBtCtrlAggBufSize = TRUE;
		aggBufSize = 0x5;
	}
	else if(pBtLinkInfo->bA2dpExist || pBtLinkInfo->bPanExist)
	{
		bBtCtrlAggBufSize = TRUE;
		aggBufSize = 0x8;
	}
	halbtc8821a1ant_LimitedRx(pBtCoexist, NORMAL_EXEC, FALSE, bBtCtrlAggBufSize, aggBufSize);

	halbtc8821a1ant_RunSwCoexistMechanism(pBtCoexist);

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_HS_OPERATION, &bBtHsOn);
	if(pCoexSta->bC2hBtInquiryPage)
	{
		halbtc8821a1ant_ActionBtInquiry(pBtCoexist);
		return;
	}
	else if(bBtHsOn)
	{
		halbtc8821a1ant_ActionHs(pBtCoexist);
		return;
	}

	
	if(!bWifiConnected)
	{
		BOOLEAN	bScan=FALSE, bLink=FALSE, bRoam=FALSE;
		
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], wifi is non connected-idle !!!\n"));

		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_SCAN, &bScan);
		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_LINK, &bLink);
		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_ROAM, &bRoam);

		if(bScan || bLink || bRoam)
			halbtc8821a1ant_ActionWifiNotConnectedAssoAuthScan(pBtCoexist);
		else
			halbtc8821a1ant_ActionWifiNotConnected(pBtCoexist);
	}
	else	// wifi LPS/Busy
	{	
		halbtc8821a1ant_ActionWifiConnected(pBtCoexist);
	}
}

VOID
halbtc8821a1ant_InitCoexDm(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	// force to reset coex mechanism
	// sw all off
	halbtc8821a1ant_SwMechanism(pBtCoexist, FALSE);

	halbtc8821a1ant_PsTdma(pBtCoexist, FORCE_EXEC, FALSE, 8);
	halbtc8821a1ant_CoexTableWithType(pBtCoexist, FORCE_EXEC, 0);
}

VOID
halbtc8821a1ant_InitHwConfig(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN				bBackUp
	)
{
	PBTC_BOARD_INFO		pBoardInfo=&pBtCoexist->boardInfo;
	u4Byte	u4Tmp=0;
	u2Byte				u2Tmp=0;
	u1Byte	u1Tmp=0;
	u1Byte				H2C_Parameter[2] ={0};
	BOOLEAN			bWifiUnder5G=FALSE;
		

	BTC_PRINT(BTC_MSG_INTERFACE, INTF_INIT, ("[BTCoex], 1Ant Init HW Config!!\n"));

	if(bBackUp)
	{
		pCoexDm->backupArfrCnt1 = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x430);
		pCoexDm->backupArfrCnt2 = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x434);
		pCoexDm->backupRetryLimit = pBtCoexist->fBtcRead2Byte(pBtCoexist, 0x42a);
		pCoexDm->backupAmpduMaxTime = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x456);
	}

	// 0x790[5:0]=0x5
	u1Tmp = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x790);
	u1Tmp &= 0xc0;
	u1Tmp |= 0x5;
	pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x790, u1Tmp);

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_UNDER_5G, &bWifiUnder5G);

	//Antenna config
	if(bWifiUnder5G)
		halbtc8821a1ant_SetAntPath(pBtCoexist, BTC_ANT_PATH_BT, TRUE, FALSE);
	else
		halbtc8821a1ant_SetAntPath(pBtCoexist, BTC_ANT_PATH_PTA, TRUE, FALSE);
	// PTA parameter
	halbtc8821a1ant_CoexTableWithType(pBtCoexist, FORCE_EXEC, 0);

	// Enable counter statistics
	pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x76e, 0xc); //0x76e[3] =1, WLAN_Act control by PTA
	pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x778, 0x3);
	pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0x40, 0x20, 0x1);
}

//============================================================
// work around function start with wa_halbtc8821a1ant_
//============================================================
//============================================================
// extern function start with EXhalbtc8821a1ant_
//============================================================
VOID
EXhalbtc8821a1ant_InitHwConfig(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN				bWifiOnly
	)
{
	halbtc8821a1ant_InitHwConfig(pBtCoexist, TRUE);
}

VOID
EXhalbtc8821a1ant_InitCoexDm(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	BTC_PRINT(BTC_MSG_INTERFACE, INTF_INIT, ("[BTCoex], Coex Mechanism Init!!\n"));

	pBtCoexist->bStopCoexDm = FALSE;
	
	halbtc8821a1ant_InitCoexDm(pBtCoexist);

	halbtc8821a1ant_QueryBtInfo(pBtCoexist);
}

VOID
EXhalbtc8821a1ant_DisplayCoexInfo(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	PBTC_BOARD_INFO		pBoardInfo=&pBtCoexist->boardInfo;
	PBTC_STACK_INFO		pStackInfo=&pBtCoexist->stackInfo;
	PBTC_BT_LINK_INFO	pBtLinkInfo=&pBtCoexist->btLinkInfo;
	pu1Byte				cliBuf=pBtCoexist->cliBuf;
	u1Byte				u1Tmp[4], i, btInfoExt, psTdmaCase=0;
	u2Byte				u2Tmp[4];
	u4Byte				u4Tmp[4];
	BOOLEAN				bRoam=FALSE, bScan=FALSE, bLink=FALSE, bWifiUnder5G=FALSE;
	BOOLEAN				bBtHsOn=FALSE, bWifiBusy=FALSE;
	s4Byte				wifiRssi=0, btHsRssi=0;
	u4Byte				wifiBw, wifiTrafficDir;
	u1Byte				wifiDot11Chnl, wifiHsChnl;
	u4Byte				fwVer=0, btPatchVer=0;

	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n ============[BT Coexist info]============");
	CL_PRINTF(cliBuf);

	if(pBtCoexist->bManualControl)
	{
		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n ============[Under Manual Control]============");
		CL_PRINTF(cliBuf);
		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n ==========================================");
		CL_PRINTF(cliBuf);
	}
	if(pBtCoexist->bStopCoexDm)
	{
		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n ============[Coex is STOPPED]============");
		CL_PRINTF(cliBuf);
		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n ==========================================");
		CL_PRINTF(cliBuf);
	}

	if(!pBoardInfo->bBtExist)
	{
		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n BT not exists !!!");
		CL_PRINTF(cliBuf);
		return;
	}

	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d", "Ant PG Num/ Ant Mech/ Ant Pos:", \
		pBoardInfo->pgAntNum, pBoardInfo->btdmAntNum, pBoardInfo->btdmAntPos);
	CL_PRINTF(cliBuf);	
	
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s / %d", "BT stack/ hci ext ver", \
		((pStackInfo->bProfileNotified)? "Yes":"No"), pStackInfo->hciVersion);
	CL_PRINTF(cliBuf);

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_BT_PATCH_VER, &btPatchVer);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_FW_VER, &fwVer);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d_%x/ 0x%x/ 0x%x(%d)", "CoexVer/ FwVer/ PatchVer", \
		GLCoexVerDate8821a1Ant, GLCoexVer8821a1Ant, fwVer, btPatchVer, btPatchVer);
	CL_PRINTF(cliBuf);

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_HS_OPERATION, &bBtHsOn);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U1_WIFI_DOT11_CHNL, &wifiDot11Chnl);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U1_WIFI_HS_CHNL, &wifiHsChnl);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d(%d)", "Dot11 channel / HsChnl(HsMode)", \
		wifiDot11Chnl, wifiHsChnl, bBtHsOn);
	CL_PRINTF(cliBuf);

	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %02x %02x %02x ", "H2C Wifi inform bt chnl Info", \
		pCoexDm->wifiChnlInfo[0], pCoexDm->wifiChnlInfo[1],
		pCoexDm->wifiChnlInfo[2]);
	CL_PRINTF(cliBuf);

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_S4_WIFI_RSSI, &wifiRssi);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_S4_HS_RSSI, &btHsRssi);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d", "Wifi rssi/ HS rssi", \
		wifiRssi, btHsRssi);
	CL_PRINTF(cliBuf);

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_SCAN, &bScan);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_LINK, &bLink);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_ROAM, &bRoam);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d ", "Wifi bLink/ bRoam/ bScan", \
		bLink, bRoam, bScan);
	CL_PRINTF(cliBuf);

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_UNDER_5G, &bWifiUnder5G);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_BW, &wifiBw);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_BUSY, &bWifiBusy);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_TRAFFIC_DIRECTION, &wifiTrafficDir);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s / %s/ %s ", "Wifi status", \
		(bWifiUnder5G? "5G":"2.4G"),
		((BTC_WIFI_BW_LEGACY==wifiBw)? "Legacy": (((BTC_WIFI_BW_HT40==wifiBw)? "HT40":"HT20"))),
		((!bWifiBusy)? "idle": ((BTC_WIFI_TRAFFIC_TX==wifiTrafficDir)? "uplink":"downlink")));
	CL_PRINTF(cliBuf);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = [%s/ %d/ %d] ", "BT [status/ rssi/ retryCnt]", \
		((pBtCoexist->btInfo.bBtDisabled)? ("disabled"):	((pCoexSta->bC2hBtInquiryPage)?("inquiry/page scan"):((BT_8821A_1ANT_BT_STATUS_NON_CONNECTED_IDLE == pCoexDm->btStatus)? "non-connected idle":
		(  (BT_8821A_1ANT_BT_STATUS_CONNECTED_IDLE == pCoexDm->btStatus)? "connected-idle":"busy")))),
		pCoexSta->btRssi, pCoexSta->btRetryCnt);
	CL_PRINTF(cliBuf);
	
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d / %d / %d", "SCO/HID/PAN/A2DP", \
		pBtLinkInfo->bScoExist, pBtLinkInfo->bHidExist, pBtLinkInfo->bPanExist, pBtLinkInfo->bA2dpExist);
	CL_PRINTF(cliBuf);	
	pBtCoexist->fBtcDispDbgMsg(pBtCoexist, BTC_DBG_DISP_BT_LINK_INFO);

	btInfoExt = pCoexSta->btInfoExt;
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s", "BT Info A2DP rate", \
		(btInfoExt&BIT0)? "Basic rate":"EDR rate");
	CL_PRINTF(cliBuf);	

	for(i=0; i<BT_INFO_SRC_8821A_1ANT_MAX; i++)
	{
		if(pCoexSta->btInfoC2hCnt[i])
		{				
			CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %02x %02x %02x %02x %02x %02x %02x(%d)", GLBtInfoSrc8821a1Ant[i], \
				pCoexSta->btInfoC2h[i][0], pCoexSta->btInfoC2h[i][1],
				pCoexSta->btInfoC2h[i][2], pCoexSta->btInfoC2h[i][3],
				pCoexSta->btInfoC2h[i][4], pCoexSta->btInfoC2h[i][5],
				pCoexSta->btInfoC2h[i][6], pCoexSta->btInfoC2hCnt[i]);
			CL_PRINTF(cliBuf);
		}
	}
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s/%s, (0x%x/0x%x)", "PS state, IPS/LPS, (lps/rpwm)", \
		((pCoexSta->bUnderIps? "IPS ON":"IPS OFF")),
		((pCoexSta->bUnderLps? "LPS ON":"LPS OFF")), 
		pBtCoexist->btInfo.lpsVal, 
		pBtCoexist->btInfo.rpwmVal);
	CL_PRINTF(cliBuf);
	pBtCoexist->fBtcDispDbgMsg(pBtCoexist, BTC_DBG_DISP_FW_PWR_MODE_CMD);

	if(!pBtCoexist->bManualControl)
	{
	// Sw mechanism	
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "============[Sw mechanism]============");
	CL_PRINTF(cliBuf);
	
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d", "SM[LowPenaltyRA]", \
		pCoexDm->bCurLowPenaltyRa);
	CL_PRINTF(cliBuf);

	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s/ %s/ %d ", "DelBA/ BtCtrlAgg/ AggSize", \
		(pBtCoexist->btInfo.bRejectAggPkt? "Yes":"No"), (pBtCoexist->btInfo.bBtCtrlAggBufSize? "Yes":"No"),
			pBtCoexist->btInfo.aggBufSize);
	CL_PRINTF(cliBuf);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x ", "Rate Mask", \
			pBtCoexist->btInfo.raMask);
	CL_PRINTF(cliBuf);	

	// Fw mechanism		
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "============[Fw mechanism]============");
	CL_PRINTF(cliBuf);	
	
		psTdmaCase = pCoexDm->curPsTdma;
		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %02x %02x %02x %02x %02x case-%d (auto:%d)", "PS TDMA", \
			pCoexDm->psTdmaPara[0], pCoexDm->psTdmaPara[1],
			pCoexDm->psTdmaPara[2], pCoexDm->psTdmaPara[3],
			pCoexDm->psTdmaPara[4], psTdmaCase, pCoexDm->bAutoTdmaAdjust);
		CL_PRINTF(cliBuf);
	
		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x ", "Latest error condition(should be 0)", \
			pCoexDm->errorCondition);
		CL_PRINTF(cliBuf);
		
		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d ", "IgnWlanAct", \
			pCoexDm->bCurIgnoreWlanAct);
		CL_PRINTF(cliBuf);
	}

	// Hw setting		
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
	u4Tmp[0] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0xc58);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x", "0x778/ 0xc58[29:25]", \
		u1Tmp[0], (u4Tmp[0]&0x3e000000) >> 25);
	CL_PRINTF(cliBuf);

	u1Tmp[0] = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x8db);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0x8db[6:5]", \
		((u1Tmp[0]&0x60)>>5));
	CL_PRINTF(cliBuf); 
	
	u1Tmp[0] = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x975);
	u4Tmp[0] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0xcb4);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x/ 0x%x", "0xcb4[29:28]/0xcb4[7:0]/0x974[9:8]", \
		(u4Tmp[0]&0x30000000)>>28, u4Tmp[0]&0xff, u1Tmp[0]& 0x3);
	CL_PRINTF(cliBuf);


	u1Tmp[0] = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x40);
	u4Tmp[0] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x4c);
	u1Tmp[1] = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x64);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x/ 0x%x", "0x40/0x4c[24:23]/0x64[0]", \
		u1Tmp[0], ((u4Tmp[0]&0x01800000)>>23), u1Tmp[1]&0x1);
	CL_PRINTF(cliBuf);

	u4Tmp[0] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x550);
	u1Tmp[0] = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x522);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x", "0x550(bcn ctrl)/0x522", \
		u4Tmp[0], u1Tmp[0]);
	CL_PRINTF(cliBuf);

	u4Tmp[0] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0xc50);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0xc50(dig)", \
		u4Tmp[0]&0xff);
	CL_PRINTF(cliBuf);

	u4Tmp[0] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0xf48);
	u1Tmp[0] = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0xa5d);
	u1Tmp[1] = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0xa5c);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x", "OFDM-FA/ CCK-FA", \
		u4Tmp[0], (u1Tmp[0]<<8) + u1Tmp[1]  );
	CL_PRINTF(cliBuf);

	u4Tmp[0] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x6c0);
	u4Tmp[1] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x6c4);
	u4Tmp[2] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x6c8);
	u1Tmp[0] = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x6cc);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x/ 0x%x/ 0x%x", "0x6c0/0x6c4/0x6c8/0x6cc(coexTable)", \
		u4Tmp[0], u4Tmp[1], u4Tmp[2], u1Tmp[0]);
	CL_PRINTF(cliBuf);

	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d", "0x770(high-pri rx/tx)", \
		pCoexSta->highPriorityRx, pCoexSta->highPriorityTx);
	CL_PRINTF(cliBuf);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d", "0x774(low-pri rx/tx)", \
		pCoexSta->lowPriorityRx, pCoexSta->lowPriorityTx);
	CL_PRINTF(cliBuf);
#if(BT_AUTO_REPORT_ONLY_8821A_1ANT == 1)
	halbtc8821a1ant_MonitorBtCtr(pBtCoexist);
#endif
	pBtCoexist->fBtcDispDbgMsg(pBtCoexist, BTC_DBG_DISP_COEX_STATISTICS);
}


VOID
EXhalbtc8821a1ant_IpsNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			type
	)
{
	u4Byte	u4Tmp=0;

	if(pBtCoexist->bManualControl ||	pBtCoexist->bStopCoexDm)
		return;

	if(BTC_IPS_ENTER == type)
	{
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], IPS ENTER notify\n"));
		pCoexSta->bUnderIps = TRUE;
		halbtc8821a1ant_SetAntPath(pBtCoexist, BTC_ANT_PATH_BT, FALSE, TRUE);
		//set PTA control
		halbtc8821a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, FALSE, 8);
		halbtc8821a1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 0);
	}
	else if(BTC_IPS_LEAVE == type)
	{
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], IPS LEAVE notify\n"));
		pCoexSta->bUnderIps = FALSE;

		halbtc8821a1ant_RunCoexistMechanism(pBtCoexist);
	}
}

VOID
EXhalbtc8821a1ant_LpsNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			type
	)
{
	if(pBtCoexist->bManualControl || pBtCoexist->bStopCoexDm)
		return;

	if(BTC_LPS_ENABLE == type)
	{
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], LPS ENABLE notify\n"));
		pCoexSta->bUnderLps = TRUE;
	}
	else if(BTC_LPS_DISABLE == type)
	{
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], LPS DISABLE notify\n"));
		pCoexSta->bUnderLps = FALSE;
	}
}

VOID
EXhalbtc8821a1ant_ScanNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			type
	)
{
	BOOLEAN bWifiConnected=FALSE, bBtHsOn=FALSE;	

	if(pBtCoexist->bManualControl ||
		pBtCoexist->bStopCoexDm ||
		pBtCoexist->btInfo.bBtDisabled )
		return;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_HS_OPERATION, &bBtHsOn);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_CONNECTED, &bWifiConnected);

	halbtc8821a1ant_QueryBtInfo(pBtCoexist);
	
	if(pCoexSta->bC2hBtInquiryPage)
	{
		halbtc8821a1ant_ActionBtInquiry(pBtCoexist);
		return;
	}
	else if(bBtHsOn)
	{
		halbtc8821a1ant_ActionHs(pBtCoexist);
		return;
	}

	if(BTC_SCAN_START == type)
	{
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], SCAN START notify\n"));
		if(!bWifiConnected)	// non-connected scan
		{
			halbtc8821a1ant_ActionWifiNotConnectedAssoAuthScan(pBtCoexist);
		}
		else	// wifi is connected
		{
			halbtc8821a1ant_ActionWifiConnectedScan(pBtCoexist);
		}
	}
	else if(BTC_SCAN_FINISH == type)
	{
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], SCAN FINISH notify\n"));
		if(!bWifiConnected)	// non-connected scan
		{
			halbtc8821a1ant_ActionWifiNotConnected(pBtCoexist);
		}
		else
		{
			halbtc8821a1ant_ActionWifiConnected(pBtCoexist);
		}
	}
}

VOID
EXhalbtc8821a1ant_ConnectNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			type
	)
{
	BOOLEAN	bWifiConnected=FALSE, bBtHsOn=FALSE;	

	if(pBtCoexist->bManualControl ||
		pBtCoexist->bStopCoexDm ||
		pBtCoexist->btInfo.bBtDisabled )
		return;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_HS_OPERATION, &bBtHsOn);
	if(pCoexSta->bC2hBtInquiryPage)
	{
		halbtc8821a1ant_ActionBtInquiry(pBtCoexist);
		return;
	}
	else if(bBtHsOn)
	{
		halbtc8821a1ant_ActionHs(pBtCoexist);
		return;
	}

	if(BTC_ASSOCIATE_START == type)
	{
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], CONNECT START notify\n"));
		halbtc8821a1ant_ActionWifiNotConnectedAssoAuthScan(pBtCoexist);
	}
	else if(BTC_ASSOCIATE_FINISH == type)
	{
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], CONNECT FINISH notify\n"));
		
		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_CONNECTED, &bWifiConnected);
		if(!bWifiConnected) // non-connected scan
		{
			halbtc8821a1ant_ActionWifiNotConnected(pBtCoexist);
		}
		else
		{
			halbtc8821a1ant_ActionWifiConnected(pBtCoexist);
		}
	}
}

VOID
EXhalbtc8821a1ant_MediaStatusNotify(
	IN	PBTC_COEXIST			pBtCoexist,
	IN	u1Byte				type
	)
{
	u1Byte			H2C_Parameter[3] ={0};
	u4Byte			wifiBw;
	u1Byte			wifiCentralChnl;

	if(pBtCoexist->bManualControl ||
		pBtCoexist->bStopCoexDm ||
		pBtCoexist->btInfo.bBtDisabled )
		return;

	if(BTC_MEDIA_CONNECT == type)
	{
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], MEDIA connect notify\n"));
	}
	else
	{
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], MEDIA disconnect notify\n"));
	}

	// only 2.4G we need to inform bt the chnl mask
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U1_WIFI_CENTRAL_CHNL, &wifiCentralChnl);
	if( (BTC_MEDIA_CONNECT == type) &&
		(wifiCentralChnl <= 14) )
	{
		//H2C_Parameter[0] = 0x1;
		H2C_Parameter[0] = 0x0;
		H2C_Parameter[1] = wifiCentralChnl;
		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_BW, &wifiBw);
		if(BTC_WIFI_BW_HT40 == wifiBw)
			H2C_Parameter[2] = 0x30;
		else
			H2C_Parameter[2] = 0x20;
	}
		
	pCoexDm->wifiChnlInfo[0] = H2C_Parameter[0];
	pCoexDm->wifiChnlInfo[1] = H2C_Parameter[1];
	pCoexDm->wifiChnlInfo[2] = H2C_Parameter[2];
	
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_EXEC, ("[BTCoex], FW write 0x66=0x%x\n", 
		H2C_Parameter[0]<<16|H2C_Parameter[1]<<8|H2C_Parameter[2]));

	pBtCoexist->fBtcFillH2c(pBtCoexist, 0x66, 3, H2C_Parameter);
}

VOID
EXhalbtc8821a1ant_SpecialPacketNotify(
	IN	PBTC_COEXIST			pBtCoexist,
	IN	u1Byte				type
	)
{
	BOOLEAN	bBtHsOn=FALSE;
	
	if(pBtCoexist->bManualControl ||
		pBtCoexist->bStopCoexDm ||
		pBtCoexist->btInfo.bBtDisabled )
		return;

	pCoexSta->specialPktPeriodCnt = 0;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_HS_OPERATION, &bBtHsOn);
	if(pCoexSta->bC2hBtInquiryPage)
	{
		halbtc8821a1ant_ActionBtInquiry(pBtCoexist);
		return;
	}
	else if(bBtHsOn)
	{
		halbtc8821a1ant_ActionHs(pBtCoexist);
		return;
	}

	if( BTC_PACKET_DHCP == type ||
		BTC_PACKET_EAPOL == type )
	{
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], special Packet(%d) notify\n", type));
		halbtc8821a1ant_ActionWifiConnectedSpecialPacket(pBtCoexist);
	}
}

VOID
EXhalbtc8821a1ant_BtInfoNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	pu1Byte			tmpBuf,
	IN	u1Byte			length
	)
{
	PBTC_BT_LINK_INFO	pBtLinkInfo=&pBtCoexist->btLinkInfo;
	u1Byte				btInfo=0;
	u1Byte				i, rspSource=0;
	BOOLEAN				bWifiConnected=FALSE;
	BOOLEAN				bBtBusy=FALSE;
	BOOLEAN				bWifiUnder5G=FALSE;

	pCoexSta->bC2hBtInfoReqSent = FALSE;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_UNDER_5G, &bWifiUnder5G);

	rspSource = tmpBuf[0]&0xf;
	if(rspSource >= BT_INFO_SRC_8821A_1ANT_MAX)
		rspSource = BT_INFO_SRC_8821A_1ANT_WIFI_FW;
	pCoexSta->btInfoC2hCnt[rspSource]++;

	BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], Bt info[%d], length=%d, hex data=[", rspSource, length));
	for(i=0; i<length; i++)
	{
		pCoexSta->btInfoC2h[rspSource][i] = tmpBuf[i];
		if(i == 1)
			btInfo = tmpBuf[i];
		if(i == length-1)
		{
			BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("0x%02x]\n", tmpBuf[i]));
		}
		else
		{
			BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("0x%02x, ", tmpBuf[i]));
		}
	}

	if(BT_INFO_SRC_8821A_1ANT_WIFI_FW != rspSource)
	{
		pCoexSta->btRetryCnt =	// [3:0]
			pCoexSta->btInfoC2h[rspSource][2]&0xf;

		pCoexSta->btRssi =
			pCoexSta->btInfoC2h[rspSource][3]*2+10;

		pCoexSta->btInfoExt = 
			pCoexSta->btInfoC2h[rspSource][4];

		// Here we need to resend some wifi info to BT
		// because bt is reset and loss of the info.
		if(pCoexSta->btInfoExt & BIT1)
		{			
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BT ext info bit1 check, send wifi BW&Chnl to BT!!\n"));
			pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_CONNECTED, &bWifiConnected);
			if(bWifiConnected)
			{
				EXhalbtc8821a1ant_MediaStatusNotify(pBtCoexist, BTC_MEDIA_CONNECT);
			}
			else
			{
				EXhalbtc8821a1ant_MediaStatusNotify(pBtCoexist, BTC_MEDIA_DISCONNECT);
			}
		}

		if( (pCoexSta->btInfoExt & BIT3) && !bWifiUnder5G)
		{
			if(!pBtCoexist->bManualControl && !pBtCoexist->bStopCoexDm)
			{
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BT ext info bit3 check, set BT NOT to ignore Wlan active!!\n"));
				halbtc8821a1ant_IgnoreWlanAct(pBtCoexist, FORCE_EXEC, FALSE);
			}
		}
		else
		{
			// BT already NOT ignore Wlan active, do nothing here.
		}
#if(BT_AUTO_REPORT_ONLY_8821A_1ANT == 0)
		if( (pCoexSta->btInfoExt & BIT4) )
		{
			// BT auto report already enabled, do nothing
		}
		else
		{
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BT ext info bit4 check, set BT to enable Auto Report!!\n"));
			halbtc8821a1ant_BtAutoReport(pBtCoexist, FORCE_EXEC, TRUE);
		}
#endif
	}
		
	// check BIT2 first ==> check if bt is under inquiry or page scan
	if(btInfo & BT_INFO_8821A_1ANT_B_INQ_PAGE)
		pCoexSta->bC2hBtInquiryPage = TRUE;
	else
		pCoexSta->bC2hBtInquiryPage = FALSE;

	// set link exist status
	if(!(btInfo&BT_INFO_8821A_1ANT_B_CONNECTION))
	{
		pCoexSta->bBtLinkExist = FALSE;
		pCoexSta->bPanExist = FALSE;
		pCoexSta->bA2dpExist = FALSE;
		pCoexSta->bHidExist = FALSE;
		pCoexSta->bScoExist = FALSE;
	}
	else	// connection exists
	{
		pCoexSta->bBtLinkExist = TRUE;
		if(btInfo & BT_INFO_8821A_1ANT_B_FTP)
			pCoexSta->bPanExist = TRUE;
		else
			pCoexSta->bPanExist = FALSE;
		if(btInfo & BT_INFO_8821A_1ANT_B_A2DP)
			pCoexSta->bA2dpExist = TRUE;
		else
			pCoexSta->bA2dpExist = FALSE;
		if(btInfo & BT_INFO_8821A_1ANT_B_HID)
			pCoexSta->bHidExist = TRUE;
		else
			pCoexSta->bHidExist = FALSE;
		if(btInfo & BT_INFO_8821A_1ANT_B_SCO_ESCO)
			pCoexSta->bScoExist = TRUE;
		else
			pCoexSta->bScoExist = FALSE;
	}

	halbtc8821a1ant_UpdateBtLinkInfo(pBtCoexist);
		
	if(!(btInfo&BT_INFO_8821A_1ANT_B_CONNECTION))
	{
		pCoexDm->btStatus = BT_8821A_1ANT_BT_STATUS_NON_CONNECTED_IDLE;
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BtInfoNotify(), BT Non-Connected idle!!!\n"));
	}
	else if(btInfo == BT_INFO_8821A_1ANT_B_CONNECTION)	// connection exists but no busy
	{
		pCoexDm->btStatus = BT_8821A_1ANT_BT_STATUS_CONNECTED_IDLE;
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BtInfoNotify(), BT Connected-idle!!!\n"));
	}
	else if((btInfo&BT_INFO_8821A_1ANT_B_SCO_ESCO) ||
		(btInfo&BT_INFO_8821A_1ANT_B_SCO_BUSY))
	{
		pCoexDm->btStatus = BT_8821A_1ANT_BT_STATUS_SCO_BUSY;
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BtInfoNotify(), BT SCO busy!!!\n"));
	}
	else if(btInfo&BT_INFO_8821A_1ANT_B_ACL_BUSY)
	{
		if(BT_8821A_1ANT_BT_STATUS_ACL_BUSY != pCoexDm->btStatus)
			pCoexDm->bAutoTdmaAdjust = FALSE;
		pCoexDm->btStatus = BT_8821A_1ANT_BT_STATUS_ACL_BUSY;
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BtInfoNotify(), BT ACL busy!!!\n"));
	}
	else
	{
		pCoexDm->btStatus = BT_8821A_1ANT_BT_STATUS_MAX;
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BtInfoNotify(), BT Non-Defined state!!!\n"));
	}

	if( (BT_8821A_1ANT_BT_STATUS_ACL_BUSY == pCoexDm->btStatus) ||
		(BT_8821A_1ANT_BT_STATUS_SCO_BUSY == pCoexDm->btStatus) ||
		(BT_8821A_1ANT_BT_STATUS_ACL_SCO_BUSY == pCoexDm->btStatus) )
		bBtBusy = TRUE;
	else
		bBtBusy = FALSE;
	pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_BL_BT_TRAFFIC_BUSY, &bBtBusy);

	halbtc8821a1ant_RunCoexistMechanism(pBtCoexist);
}

VOID
EXhalbtc8821a1ant_HaltNotify(
	IN	PBTC_COEXIST			pBtCoexist
	)
{
	u4Byte	u4Tmp;
	
	BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], Halt notify\n"));

	pBtCoexist->bStopCoexDm = TRUE;

	halbtc8821a1ant_SetAntPath(pBtCoexist, BTC_ANT_PATH_BT, FALSE, TRUE);
	halbtc8821a1ant_IgnoreWlanAct(pBtCoexist, FORCE_EXEC, TRUE);

	halbtc8821a1ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);
	halbtc8821a1ant_PsTdma(pBtCoexist, FORCE_EXEC, FALSE, 0);

	EXhalbtc8821a1ant_MediaStatusNotify(pBtCoexist, BTC_MEDIA_DISCONNECT);
}

VOID
EXhalbtc8821a1ant_PnpNotify(
	IN	PBTC_COEXIST			pBtCoexist,
	IN	u1Byte				pnpState
	)
{
	BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], Pnp notify\n"));

	if(BTC_WIFI_PNP_SLEEP == pnpState)
	{
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], Pnp notify to SLEEP\n"));
		pBtCoexist->bStopCoexDm = TRUE;
		halbtc8821a1ant_IgnoreWlanAct(pBtCoexist, FORCE_EXEC, TRUE);
		halbtc8821a1ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);
		halbtc8821a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, FALSE, 9);	
	}
	else if(BTC_WIFI_PNP_WAKE_UP == pnpState)
	{
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], Pnp notify to WAKE UP\n"));
		pBtCoexist->bStopCoexDm = FALSE;
		halbtc8821a1ant_InitHwConfig(pBtCoexist, FALSE);
		halbtc8821a1ant_InitCoexDm(pBtCoexist);
		halbtc8821a1ant_QueryBtInfo(pBtCoexist);
	}
}

VOID
EXhalbtc8821a1ant_Periodical(
	IN	PBTC_COEXIST			pBtCoexist
	)
{
	static u1Byte		disVerInfoCnt=0;
	u4Byte				fwVer=0, btPatchVer=0;
	PBTC_BOARD_INFO		pBoardInfo=&pBtCoexist->boardInfo;
	PBTC_STACK_INFO		pStackInfo=&pBtCoexist->stackInfo;

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], ==========================Periodical===========================\n"));

	if(disVerInfoCnt <= 5)
	{
		disVerInfoCnt += 1;
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_INIT, ("[BTCoex], ****************************************************************\n"));
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_INIT, ("[BTCoex], Ant PG Num/ Ant Mech/ Ant Pos = %d/ %d/ %d\n", \
			pBoardInfo->pgAntNum, pBoardInfo->btdmAntNum, pBoardInfo->btdmAntPos));
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_INIT, ("[BTCoex], BT stack/ hci ext ver = %s / %d\n", \
			((pStackInfo->bProfileNotified)? "Yes":"No"), pStackInfo->hciVersion));
		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_BT_PATCH_VER, &btPatchVer);
		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_FW_VER, &fwVer);
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_INIT, ("[BTCoex], CoexVer/ FwVer/ PatchVer = %d_%x/ 0x%x/ 0x%x(%d)\n", \
			GLCoexVerDate8821a1Ant, GLCoexVer8821a1Ant, fwVer, btPatchVer, btPatchVer));
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_INIT, ("[BTCoex], ****************************************************************\n"));
	}

#if(BT_AUTO_REPORT_ONLY_8821A_1ANT == 0)
	halbtc8821a1ant_QueryBtInfo(pBtCoexist);
	halbtc8821a1ant_MonitorBtCtr(pBtCoexist);
	halbtc8821a1ant_MonitorBtEnableDisable(pBtCoexist);
#else
	if( halbtc8821a1ant_IsWifiStatusChanged(pBtCoexist) ||
		pCoexDm->bAutoTdmaAdjust )
	{
		if(pCoexSta->specialPktPeriodCnt > 2)
		{
			halbtc8821a1ant_RunCoexistMechanism(pBtCoexist);	
		}
	}

	pCoexSta->specialPktPeriodCnt++;
#endif
}


#endif

