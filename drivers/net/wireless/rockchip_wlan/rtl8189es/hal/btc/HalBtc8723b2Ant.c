//============================================================
// Description:
//
// This file is for RTL8723B Co-exist mechanism
//
// History
// 2012/11/15 Cosa first check in.
//
//============================================================

//============================================================
// include files
//============================================================
#include "Mp_Precomp.h"

#if WPP_SOFTWARE_TRACE
#include "HalBtc8723b2Ant.tmh"
#endif

#if(BT_30_SUPPORT == 1)
//============================================================
// Global variables, these are static variables
//============================================================
static COEX_DM_8723B_2ANT		GLCoexDm8723b2Ant;
static PCOEX_DM_8723B_2ANT 	pCoexDm=&GLCoexDm8723b2Ant;
static COEX_STA_8723B_2ANT		GLCoexSta8723b2Ant;
static PCOEX_STA_8723B_2ANT	pCoexSta=&GLCoexSta8723b2Ant;

const char *const GLBtInfoSrc8723b2Ant[]={
	"BT Info[wifi fw]",
	"BT Info[bt rsp]",
	"BT Info[bt auto report]",
};

u4Byte	GLCoexVerDate8723b2Ant=20150119;
u4Byte	GLCoexVer8723b2Ant=0x44;

//============================================================
// local function proto type if needed
//============================================================
//============================================================
// local function start with halbtc8723b2ant_
//============================================================
u1Byte
halbtc8723b2ant_BtRssiState(
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
			if(btRssi >= (rssiThresh+BTC_RSSI_COEX_THRESH_TOL_8723B_2ANT))
			{
				btRssiState = BTC_RSSI_STATE_HIGH;
			}
			else
			{
				btRssiState = BTC_RSSI_STATE_STAY_LOW;
			}
		}
		else
		{
			if(btRssi < rssiThresh)
			{
				btRssiState = BTC_RSSI_STATE_LOW;
			}
			else
			{
				btRssiState = BTC_RSSI_STATE_STAY_HIGH;
			}
		}
	}
	else if(levelNum == 3)
	{
		if(rssiThresh > rssiThresh1)
		{
			RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BT Rssi thresh error!!\n"));
			return pCoexSta->preBtRssiState;
		}
		
		if( (pCoexSta->preBtRssiState == BTC_RSSI_STATE_LOW) ||
			(pCoexSta->preBtRssiState == BTC_RSSI_STATE_STAY_LOW))
		{
			if(btRssi >= (rssiThresh+BTC_RSSI_COEX_THRESH_TOL_8723B_2ANT))
			{
				btRssiState = BTC_RSSI_STATE_MEDIUM;
			}
			else
			{
				btRssiState = BTC_RSSI_STATE_STAY_LOW;
			}
		}
		else if( (pCoexSta->preBtRssiState == BTC_RSSI_STATE_MEDIUM) ||
			(pCoexSta->preBtRssiState == BTC_RSSI_STATE_STAY_MEDIUM))
		{
			if(btRssi >= (rssiThresh1+BTC_RSSI_COEX_THRESH_TOL_8723B_2ANT))
			{
				btRssiState = BTC_RSSI_STATE_HIGH;
			}
			else if(btRssi < rssiThresh)
			{
				btRssiState = BTC_RSSI_STATE_LOW;
			}
			else
			{
				btRssiState = BTC_RSSI_STATE_STAY_MEDIUM;
			}
		}
		else
		{
			if(btRssi < rssiThresh1)
			{
				btRssiState = BTC_RSSI_STATE_MEDIUM;
			}
			else
			{
				btRssiState = BTC_RSSI_STATE_STAY_HIGH;
			}
		}
	}
		
	pCoexSta->preBtRssiState = btRssiState;

	return btRssiState;
}

u1Byte
halbtc8723b2ant_WifiRssiState(
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
			if(wifiRssi >= (rssiThresh+BTC_RSSI_COEX_THRESH_TOL_8723B_2ANT))
			{
				wifiRssiState = BTC_RSSI_STATE_HIGH;
			}
			else
			{
				wifiRssiState = BTC_RSSI_STATE_STAY_LOW;
			}
		}
		else
		{
			if(wifiRssi < rssiThresh)
			{
				wifiRssiState = BTC_RSSI_STATE_LOW;
			}
			else
			{
				wifiRssiState = BTC_RSSI_STATE_STAY_HIGH;
			}
		}
	}
	else if(levelNum == 3)
	{
		if(rssiThresh > rssiThresh1)
		{
			RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], wifi RSSI thresh error!!\n"));
			return pCoexSta->preWifiRssiState[index];
		}
		
		if( (pCoexSta->preWifiRssiState[index] == BTC_RSSI_STATE_LOW) ||
			(pCoexSta->preWifiRssiState[index] == BTC_RSSI_STATE_STAY_LOW))
		{
			if(wifiRssi >= (rssiThresh+BTC_RSSI_COEX_THRESH_TOL_8723B_2ANT))
			{
				wifiRssiState = BTC_RSSI_STATE_MEDIUM;
			}
			else
			{
				wifiRssiState = BTC_RSSI_STATE_STAY_LOW;
			}
		}
		else if( (pCoexSta->preWifiRssiState[index] == BTC_RSSI_STATE_MEDIUM) ||
			(pCoexSta->preWifiRssiState[index] == BTC_RSSI_STATE_STAY_MEDIUM))
		{
			if(wifiRssi >= (rssiThresh1+BTC_RSSI_COEX_THRESH_TOL_8723B_2ANT))
			{
				wifiRssiState = BTC_RSSI_STATE_HIGH;
			}
			else if(wifiRssi < rssiThresh)
			{
				wifiRssiState = BTC_RSSI_STATE_LOW;
			}
			else
			{
				wifiRssiState = BTC_RSSI_STATE_STAY_MEDIUM;
			}
		}
		else
		{
			if(wifiRssi < rssiThresh1)
			{
				wifiRssiState = BTC_RSSI_STATE_MEDIUM;
			}
			else
			{
				wifiRssiState = BTC_RSSI_STATE_STAY_HIGH;
			}
		}
	}
		
	pCoexSta->preWifiRssiState[index] = wifiRssiState;

	return wifiRssiState;
}

VOID
halbtc8723b2ant_MonitorBtEnableDisable(
	IN 	PBTC_COEXIST		pBtCoexist
	)
{
	static BOOLEAN	bPreBtDisabled=FALSE;
	static u4Byte	btDisableCnt=0;
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
	}
	else
	{
		btDisableCnt++;
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], bt all counters=0, %d times!!\n",  btDisableCnt));
		if(btDisableCnt >= 2)
		{
			bBtDisabled = TRUE;
		}
	}
	if(bPreBtDisabled != bBtDisabled)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BT is from %s to %s!!\n",  (bPreBtDisabled ? "disabled":"enabled"), 
			(bBtDisabled ? "disabled":"enabled")));

		bPreBtDisabled = bBtDisabled;
		pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_BL_BT_DISABLE, &bBtDisabled);
		if(bBtDisabled)
		{
			pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_ACT_LEAVE_LPS, NULL);
			pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_ACT_NORMAL_LPS, NULL);
		}
	}
}


VOID
halbtc8723b2ant_LimitedRx(
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
halbtc8723b2ant_MonitorBtCtr(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	u4Byte 			regHPTxRx, regLPTxRx, u4Tmp;
	u4Byte			regHPTx=0, regHPRx=0, regLPTx=0, regLPRx=0;
	u1Byte			u1Tmp;
	
	PBTC_BT_LINK_INFO pBtLinkInfo=&pBtCoexist->btLinkInfo;
	
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

	if( (pCoexSta->lowPriorityTx > 1050)  && (!pCoexSta->bC2hBtInquiryPage))
		pCoexSta->popEventCnt++;

	if ( (pCoexSta->lowPriorityRx >= 950)  &&  (pCoexSta->lowPriorityRx >= pCoexSta->lowPriorityTx) && (!pCoexSta->bUnderIps) )
	{
		pBtLinkInfo->bSlaveRole = TRUE;
	}
	else
	{
		pBtLinkInfo->bSlaveRole = FALSE;
	}

	RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], High Priority Tx/Rx (reg 0x%x)=0x%x(%d)/0x%x(%d)\n", 
		regHPTxRx, regHPTx, regHPTx, regHPRx, regHPRx));
	RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Low Priority Tx/Rx (reg 0x%x)=0x%x(%d)/0x%x(%d)\n", 
		regLPTxRx, regLPTx, regLPTx, regLPRx, regLPRx));

	// reset counter
	pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x76e, 0xc);
}

VOID
halbtc8723b2ant_MonitorWiFiCtr(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	u4Byte 	u4Tmp;
	u2Byte 	u2Tmp[3];
	s4Byte	wifiRssi=0;
	BOOLEAN bWifiBusy = FALSE, bWifiUnderBMode = FALSE;
	static u1Byte nCCKLockCounter = 0;


	if (pCoexSta->bUnderIps)
	{
		pCoexSta->nCRCOK_CCK = 0;
		pCoexSta->nCRCOK_11g = 0;
		pCoexSta->nCRCOK_11n = 0;
		pCoexSta->nCRCOK_11nAgg = 0;

		pCoexSta->nCRCErr_CCK = 0;
		pCoexSta->nCRCErr_11g = 0;
		pCoexSta->nCRCErr_11n = 0;
		pCoexSta->nCRCErr_11nAgg = 0;	
	}
	else
	{
		pCoexSta->nCRCOK_CCK	= pBtCoexist->fBtcRead4Byte(pBtCoexist, 0xf88);
		pCoexSta->nCRCOK_11g 	= pBtCoexist->fBtcRead2Byte(pBtCoexist, 0xf94);
		pCoexSta->nCRCOK_11n	= pBtCoexist->fBtcRead2Byte(pBtCoexist, 0xf90);
		pCoexSta->nCRCOK_11nAgg= pBtCoexist->fBtcRead2Byte(pBtCoexist, 0xfb8);

		pCoexSta->nCRCErr_CCK 	 = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0xf84);
		pCoexSta->nCRCErr_11g 	 = pBtCoexist->fBtcRead2Byte(pBtCoexist, 0xf96);
		pCoexSta->nCRCErr_11n 	 = pBtCoexist->fBtcRead2Byte(pBtCoexist, 0xf92);
		pCoexSta->nCRCErr_11nAgg = pBtCoexist->fBtcRead2Byte(pBtCoexist, 0xfba);		
	}

	//reset counter
	pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0xf16, 0x1, 0x1);
	pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0xf16, 0x1, 0x0);
}

VOID
halbtc8723b2ant_QueryBtInfo(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	u1Byte			H2C_Parameter[1] ={0};

	pCoexSta->bC2hBtInfoReqSent = TRUE;

	H2C_Parameter[0] |= BIT0;	// trigger

	RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], Query Bt Info, FW write 0x61=0x%x\n", 
		H2C_Parameter[0]));

	pBtCoexist->fBtcFillH2c(pBtCoexist, 0x61, 1, H2C_Parameter);
}

BOOLEAN
halbtc8723b2ant_IsWifiStatusChanged(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	static BOOLEAN	bPreWifiBusy=FALSE, bPreUnder4way=FALSE, bPreBtHsOn=FALSE;
	BOOLEAN	bWifiBusy=FALSE, bUnder4way=FALSE, bBtHsOn=FALSE;
	BOOLEAN	bWifiConnected=FALSE;
	u1Byte			wifiRssiState=BTC_RSSI_STATE_HIGH;
	

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


		wifiRssiState = halbtc8723b2ant_WifiRssiState(pBtCoexist,3, 2, BT_8723B_2ANT_WIFI_RSSI_COEXSWITCH_THRES, 0);	

		if ( (BTC_RSSI_STATE_HIGH ==wifiRssiState ) ||  (BTC_RSSI_STATE_LOW ==wifiRssiState ))
		{
			return TRUE;
		}
	
	}

	return FALSE;
}

VOID
halbtc8723b2ant_UpdateBtLinkInfo(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	PBTC_STACK_INFO 	pStackInfo=&pBtCoexist->stackInfo;
	PBTC_BT_LINK_INFO	pBtLinkInfo=&pBtCoexist->btLinkInfo;
	BOOLEAN				bBtHsOn=FALSE;

#if(BT_AUTO_REPORT_ONLY_8723B_2ANT == 1)	// profile from bt patch
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
#else	// profile from bt stack
	pBtLinkInfo->bBtLinkExist = pStackInfo->bBtLinkExist;
	pBtLinkInfo->bScoExist = pStackInfo->bScoExist;
	pBtLinkInfo->bA2dpExist = pStackInfo->bA2dpExist;
	pBtLinkInfo->bPanExist = pStackInfo->bPanExist;
	pBtLinkInfo->bHidExist = pStackInfo->bHidExist;

	//for win-8 stack HID report error
	if(!pStackInfo->bHidExist)
		pStackInfo->bHidExist = pCoexSta->bHidExist;  //sync  BTInfo with BT firmware and stack
	// when stack HID report error, here we use the info from bt fw.
	if(!pStackInfo->bBtLinkExist)
		pStackInfo->bBtLinkExist = pCoexSta->bBtLinkExist;	
#endif
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
halbtc8723b2ant_ActionAlgorithm(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	PBTC_BT_LINK_INFO	pBtLinkInfo=&pBtCoexist->btLinkInfo;
	BOOLEAN				bBtHsOn=FALSE;
	u1Byte				algorithm=BT_8723B_2ANT_COEX_ALGO_UNDEFINED;
	u1Byte				numOfDiffProfile=0;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_HS_OPERATION, &bBtHsOn);
		
	if(!pBtLinkInfo->bBtLinkExist)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], No BT link exists!!!\n"));
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
			RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], SCO only\n"));
			algorithm = BT_8723B_2ANT_COEX_ALGO_SCO;
		}
		else
		{
			if(pBtLinkInfo->bHidExist)
			{
				RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], HID only\n"));
				algorithm = BT_8723B_2ANT_COEX_ALGO_HID;
			}
			else if(pBtLinkInfo->bA2dpExist)
			{
				RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], A2DP only\n"));
				algorithm = BT_8723B_2ANT_COEX_ALGO_A2DP;
			}
			else if(pBtLinkInfo->bPanExist)
			{
				if(bBtHsOn)
				{
					RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], PAN(HS) only\n"));
					algorithm = BT_8723B_2ANT_COEX_ALGO_PANHS;
				}
				else
				{
					RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], PAN(EDR) only\n"));
					algorithm = BT_8723B_2ANT_COEX_ALGO_PANEDR;
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
				RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], SCO + HID\n"));
				algorithm = BT_8723B_2ANT_COEX_ALGO_PANEDR_HID;
			}
			else if(pBtLinkInfo->bA2dpExist)
			{
				RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], SCO + A2DP ==> SCO\n"));
				algorithm = BT_8723B_2ANT_COEX_ALGO_PANEDR_HID;
			}
			else if(pBtLinkInfo->bPanExist)
			{
				if(bBtHsOn)
				{
					RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], SCO + PAN(HS)\n"));
					algorithm = BT_8723B_2ANT_COEX_ALGO_SCO;
				}
				else
				{
					RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], SCO + PAN(EDR)\n"));
					algorithm = BT_8723B_2ANT_COEX_ALGO_PANEDR_HID;
				}
			}
		}
		else
		{
			if( pBtLinkInfo->bHidExist &&
				pBtLinkInfo->bA2dpExist )
			{
#if 0
				if(pStackInfo->numOfHid >= 2)
				{
					RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], HID*2 + A2DP\n"));
					algorithm = BT_8723B_2ANT_COEX_ALGO_HID_A2DP_PANEDR;
				}
				else
#endif
				{			
					RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], HID + A2DP\n"));
					algorithm = BT_8723B_2ANT_COEX_ALGO_HID_A2DP;
				}
			}
			else if( pBtLinkInfo->bHidExist &&
				pBtLinkInfo->bPanExist )
			{
				if(bBtHsOn)
				{
					RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], HID + PAN(HS)\n"));
					algorithm = BT_8723B_2ANT_COEX_ALGO_HID;
				}
				else
				{
					RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], HID + PAN(EDR)\n"));
					algorithm = BT_8723B_2ANT_COEX_ALGO_PANEDR_HID;
				}
			}
			else if( pBtLinkInfo->bPanExist &&
				pBtLinkInfo->bA2dpExist )
			{
				if(bBtHsOn)
				{
					RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], A2DP + PAN(HS)\n"));
					algorithm = BT_8723B_2ANT_COEX_ALGO_A2DP_PANHS;
				}
				else
				{
					RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], A2DP + PAN(EDR)\n"));
					algorithm = BT_8723B_2ANT_COEX_ALGO_PANEDR_A2DP;
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
				RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], SCO + HID + A2DP ==> HID\n"));
				algorithm = BT_8723B_2ANT_COEX_ALGO_PANEDR_HID;
			}
			else if( pBtLinkInfo->bHidExist &&
				pBtLinkInfo->bPanExist )
			{
				if(bBtHsOn)
				{
					RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], SCO + HID + PAN(HS)\n"));
					algorithm = BT_8723B_2ANT_COEX_ALGO_PANEDR_HID;
				}
				else
				{
					RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], SCO + HID + PAN(EDR)\n"));
					algorithm = BT_8723B_2ANT_COEX_ALGO_PANEDR_HID;
				}
			}
			else if( pBtLinkInfo->bPanExist &&
				pBtLinkInfo->bA2dpExist )
			{
				if(bBtHsOn)
				{
					RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], SCO + A2DP + PAN(HS)\n"));
					algorithm = BT_8723B_2ANT_COEX_ALGO_PANEDR_HID;
				}
				else
				{
					RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], SCO + A2DP + PAN(EDR) ==> HID\n"));
					algorithm = BT_8723B_2ANT_COEX_ALGO_PANEDR_HID;
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
					RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], HID + A2DP + PAN(HS)\n"));
					algorithm = BT_8723B_2ANT_COEX_ALGO_HID_A2DP;
				}
				else
				{
					RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], HID + A2DP + PAN(EDR)\n"));
					algorithm = BT_8723B_2ANT_COEX_ALGO_HID_A2DP_PANEDR;
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
					RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Error!!! SCO + HID + A2DP + PAN(HS)\n"));

				}
				else
				{
					RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], SCO + HID + A2DP + PAN(EDR)==>PAN(EDR)+HID\n"));
					algorithm = BT_8723B_2ANT_COEX_ALGO_PANEDR_HID;
				}
			}
		}
	}

	return algorithm;
}

VOID
halbtc8723b2ant_SetFwDacSwingLevel(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			dacSwingLvl
	)
{
	u1Byte			H2C_Parameter[1] ={0};

	// There are several type of dacswing
	// 0x18/ 0x10/ 0xc/ 0x8/ 0x4/ 0x6
	H2C_Parameter[0] = dacSwingLvl;

	RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], Set Dac Swing Level=0x%x\n", dacSwingLvl));
	RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], FW write 0x64=0x%x\n", H2C_Parameter[0]));

	pBtCoexist->fBtcFillH2c(pBtCoexist, 0x64, 1, H2C_Parameter);
}

VOID
halbtc8723b2ant_SetFwDecBtPwr(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte				decBtPwrLvl
	)
{
	u1Byte			H2C_Parameter[1] ={0};
	
	H2C_Parameter[0] = decBtPwrLvl;

	RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], decrease Bt Power level = %d, FW write 0x62=0x%x\n", 
		decBtPwrLvl, H2C_Parameter[0]));

	pBtCoexist->fBtcFillH2c(pBtCoexist, 0x62, 1, H2C_Parameter);	
}

VOID
halbtc8723b2ant_DecBtPwr(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bForceExec,
	IN	u1Byte				decBtPwrLvl
	)
{
	RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], %s Dec BT power level = %d\n",  
		(bForceExec? "force to":""), decBtPwrLvl));
	pCoexDm->curBtDecPwrLvl = decBtPwrLvl;

	if(!bForceExec)
	{
		if(pCoexDm->preBtDecPwrLvl == pCoexDm->curBtDecPwrLvl) 
			return;
	}
	halbtc8723b2ant_SetFwDecBtPwr(pBtCoexist, pCoexDm->curBtDecPwrLvl);

	pCoexDm->preBtDecPwrLvl = pCoexDm->curBtDecPwrLvl;
}

VOID
halbtc8723b2ant_SetBtAutoReport(
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

	RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], BT FW auto report : %s, FW write 0x68=0x%x\n", 
		(bEnableAutoReport? "Enabled!!":"Disabled!!"), H2C_Parameter[0]));

	pBtCoexist->fBtcFillH2c(pBtCoexist, 0x68, 1, H2C_Parameter);	
}

VOID
halbtc8723b2ant_BtAutoReport(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bForceExec,
	IN	BOOLEAN			bEnableAutoReport
	)
{
	RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], %s BT Auto report = %s\n",  
		(bForceExec? "force to":""), ((bEnableAutoReport)? "Enabled":"Disabled")));
	pCoexDm->bCurBtAutoReport = bEnableAutoReport;

	if(!bForceExec)
	{
		if(pCoexDm->bPreBtAutoReport == pCoexDm->bCurBtAutoReport) 
			return;
	}
	halbtc8723b2ant_SetBtAutoReport(pBtCoexist, pCoexDm->bCurBtAutoReport);

	pCoexDm->bPreBtAutoReport = pCoexDm->bCurBtAutoReport;
}

VOID
halbtc8723b2ant_FwDacSwingLvl(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bForceExec,
	IN	u1Byte			fwDacSwingLvl
	)
{
	RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], %s set FW Dac Swing level = %d\n",  
		(bForceExec? "force to":""), fwDacSwingLvl));
	pCoexDm->curFwDacSwingLvl = fwDacSwingLvl;

	if(!bForceExec)
	{
		if(pCoexDm->preFwDacSwingLvl == pCoexDm->curFwDacSwingLvl) 
			return;
	}

	halbtc8723b2ant_SetFwDacSwingLevel(pBtCoexist, pCoexDm->curFwDacSwingLvl);

	pCoexDm->preFwDacSwingLvl = pCoexDm->curFwDacSwingLvl;
}

VOID
halbtc8723b2ant_SetSwRfRxLpfCorner(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bRxRfShrinkOn
	)
{
	if(bRxRfShrinkOn)
	{
		//Shrink RF Rx LPF corner
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Shrink RF Rx LPF corner!!\n"));
		pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1e, 0xfffff, 0xffffc);
	}
	else
	{
		//Resume RF Rx LPF corner
		// After initialized, we can use pCoexDm->btRf0x1eBackup
		if(pBtCoexist->bInitilized)
		{
			RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Resume RF Rx LPF corner!!\n"));
			pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1e, 0xfffff, pCoexDm->btRf0x1eBackup);
		}
	}
}

VOID
halbtc8723b2ant_RfShrink(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bForceExec,
	IN	BOOLEAN			bRxRfShrinkOn
	)
{
	RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], %s turn Rx RF Shrink = %s\n",  
		(bForceExec? "force to":""), ((bRxRfShrinkOn)? "ON":"OFF")));
	pCoexDm->bCurRfRxLpfShrink = bRxRfShrinkOn;

	if(!bForceExec)
	{
		if(pCoexDm->bPreRfRxLpfShrink == pCoexDm->bCurRfRxLpfShrink) 
			return;
	}
	halbtc8723b2ant_SetSwRfRxLpfCorner(pBtCoexist, pCoexDm->bCurRfRxLpfShrink);

	pCoexDm->bPreRfRxLpfShrink = pCoexDm->bCurRfRxLpfShrink;
}

VOID
halbtc8723b2ant_SetSwPenaltyTxRateAdaptive(
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

	RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], set WiFi Low-Penalty Retry: %s", 
		(bLowPenaltyRa? "ON!!":"OFF!!")) );

	pBtCoexist->fBtcFillH2c(pBtCoexist, 0x69, 6, H2C_Parameter);
}

VOID
halbtc8723b2ant_LowPenaltyRa(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bForceExec,
	IN	BOOLEAN			bLowPenaltyRa
	)
{
	//return;
	RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], %s turn LowPenaltyRA = %s\n",  
		(bForceExec? "force to":""), ((bLowPenaltyRa)? "ON":"OFF")));
	pCoexDm->bCurLowPenaltyRa = bLowPenaltyRa;

	if(!bForceExec)
	{
		if(pCoexDm->bPreLowPenaltyRa == pCoexDm->bCurLowPenaltyRa) 
			return;
	}
	halbtc8723b2ant_SetSwPenaltyTxRateAdaptive(pBtCoexist, pCoexDm->bCurLowPenaltyRa);

	pCoexDm->bPreLowPenaltyRa = pCoexDm->bCurLowPenaltyRa;
}

VOID
halbtc8723b2ant_SetDacSwingReg(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u4Byte			level
	)
{
	u1Byte	val=(u1Byte)level;

	RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Write SwDacSwing = 0x%x\n", level));
	pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0x883, 0x3e, val);
}

VOID
halbtc8723b2ant_SetSwFullTimeDacSwing(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bSwDacSwingOn,
	IN	u4Byte			swDacSwingLvl
	)
{
	if(bSwDacSwingOn)
	{
		halbtc8723b2ant_SetDacSwingReg(pBtCoexist, swDacSwingLvl);
	}
	else
	{
		halbtc8723b2ant_SetDacSwingReg(pBtCoexist, 0x18);
	}
}


VOID
halbtc8723b2ant_DacSwing(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bForceExec,
	IN	BOOLEAN			bDacSwingOn,
	IN	u4Byte			dacSwingLvl
	)
{
	RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], %s turn DacSwing=%s, dacSwingLvl=0x%x\n",  
		(bForceExec? "force to":""), ((bDacSwingOn)? "ON":"OFF"), dacSwingLvl));
	pCoexDm->bCurDacSwingOn = bDacSwingOn;
	pCoexDm->curDacSwingLvl = dacSwingLvl;

	if(!bForceExec)
	{
		if( (pCoexDm->bPreDacSwingOn == pCoexDm->bCurDacSwingOn) &&
			(pCoexDm->preDacSwingLvl == pCoexDm->curDacSwingLvl) )
			return;
	}
	delay_ms(30);
	halbtc8723b2ant_SetSwFullTimeDacSwing(pBtCoexist, bDacSwingOn, dacSwingLvl);

	pCoexDm->bPreDacSwingOn = pCoexDm->bCurDacSwingOn;
	pCoexDm->preDacSwingLvl = pCoexDm->curDacSwingLvl;
}

VOID
halbtc8723b2ant_SetAdcBackOff(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bAdcBackOff
	)
{
	if(bAdcBackOff)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BB BackOff Level On!\n"));
		pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0xc05, 0x30, 0x3);
	}
	else
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BB BackOff Level Off!\n"));
		pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0xc05, 0x30, 0x1);
	}
}

VOID
halbtc8723b2ant_AdcBackOff(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bForceExec,
	IN	BOOLEAN			bAdcBackOff
	)
{
	RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], %s turn AdcBackOff = %s\n",  
		(bForceExec? "force to":""), ((bAdcBackOff)? "ON":"OFF")));
	pCoexDm->bCurAdcBackOff = bAdcBackOff;

	if(!bForceExec)
	{
		if(pCoexDm->bPreAdcBackOff == pCoexDm->bCurAdcBackOff) 
			return;
	}
	halbtc8723b2ant_SetAdcBackOff(pBtCoexist, pCoexDm->bCurAdcBackOff);

	pCoexDm->bPreAdcBackOff = pCoexDm->bCurAdcBackOff;
}

VOID
halbtc8723b2ant_SetAgcTable(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bAgcTableEn
	)
{
	u1Byte		rssiAdjustVal=0;

	//=================BB AGC Gain Table
	if(bAgcTableEn)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BB Agc Table On!\n"));
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x6e1A0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x6d1B0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x6c1C0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x6b1D0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x6a1E0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x691F0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x68200001);
	}
	else
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BB Agc Table Off!\n"));
	 	pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0xaa1A0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0xa91B0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0xa81C0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0xa71D0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0xa61E0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0xa51F0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0xa4200001);
	}
	
	
	//=================RF Gain
	pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0xef, 0xfffff, 0x02000);
	if(bAgcTableEn)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Agc Table On!\n"));
		pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x3b, 0xfffff, 0x38fff);
		pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x3b, 0xfffff, 0x38ffe);
	}
	else
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Agc Table Off!\n"));
		pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x3b, 0xfffff, 0x380c3);
		pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x3b, 0xfffff, 0x28ce6);
	}
	pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0xef, 0xfffff, 0x0);

	pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0xed, 0xfffff, 0x1);
	if(bAgcTableEn)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Agc Table On!\n"));
		pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x40, 0xfffff, 0x38fff);
		pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x40, 0xfffff, 0x38ffe);
	}
	else
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Agc Table Off!\n"));
		pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x40, 0xfffff, 0x380c3);
		pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x40, 0xfffff, 0x28ce6);
	}
	pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0xed, 0xfffff, 0x0);

	// set rssiAdjustVal for wifi module.
	if(bAgcTableEn)
	{
		rssiAdjustVal = 8;
	}
	pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_U1_RSSI_ADJ_VAL_FOR_AGC_TABLE_ON, &rssiAdjustVal);
}

VOID
halbtc8723b2ant_AgcTable(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bForceExec,
	IN	BOOLEAN			bAgcTableEn
	)
{
	RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], %s %s Agc Table\n",  
		(bForceExec? "force to":""), ((bAgcTableEn)? "Enable":"Disable")));
	pCoexDm->bCurAgcTableEn = bAgcTableEn;

	if(!bForceExec)
	{
		if(pCoexDm->bPreAgcTableEn == pCoexDm->bCurAgcTableEn) 
			return;
	}
	halbtc8723b2ant_SetAgcTable(pBtCoexist, bAgcTableEn);

	pCoexDm->bPreAgcTableEn = pCoexDm->bCurAgcTableEn;
}

VOID
halbtc8723b2ant_SetCoexTable(
	IN	PBTC_COEXIST	pBtCoexist,
	IN	u4Byte		val0x6c0,
	IN	u4Byte		val0x6c4,
	IN	u4Byte		val0x6c8,
	IN	u1Byte		val0x6cc
	)
{
	RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], set coex table, set 0x6c0=0x%x\n", val0x6c0));
	pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x6c0, val0x6c0);

	RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], set coex table, set 0x6c4=0x%x\n", val0x6c4));
	pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x6c4, val0x6c4);

	RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], set coex table, set 0x6c8=0x%x\n", val0x6c8));
	pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x6c8, val0x6c8);

	RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], set coex table, set 0x6cc=0x%x\n", val0x6cc));
	pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x6cc, val0x6cc);
}

VOID
halbtc8723b2ant_CoexTable(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bForceExec,
	IN	u4Byte			val0x6c0,
	IN	u4Byte			val0x6c4,
	IN	u4Byte			val0x6c8,
	IN	u1Byte			val0x6cc
	)
{
	RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], %s write Coex Table 0x6c0=0x%x, 0x6c4=0x%x, 0x6c8=0x%x, 0x6cc=0x%x\n", 
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
	halbtc8723b2ant_SetCoexTable(pBtCoexist, val0x6c0, val0x6c4, val0x6c8, val0x6cc);

	pCoexDm->preVal0x6c0 = pCoexDm->curVal0x6c0;
	pCoexDm->preVal0x6c4 = pCoexDm->curVal0x6c4;
	pCoexDm->preVal0x6c8 = pCoexDm->curVal0x6c8;
	pCoexDm->preVal0x6cc = pCoexDm->curVal0x6cc;
}

VOID
halbtc8723b2ant_CoexTableWithType(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN				bForceExec,
	IN	u1Byte				type
	)
{
	pCoexSta->nCoexTableType = type;
	
	switch(type)
	{
		case 0:
			halbtc8723b2ant_CoexTable(pBtCoexist, bForceExec, 0x55555555, 0x55555555, 0xffffff, 0x3);
			break;
		case 1:
			halbtc8723b2ant_CoexTable(pBtCoexist, bForceExec, 0x55555555, 0x5afa5afa, 0xffffff, 0x3);
			break;
		case 2:
			halbtc8723b2ant_CoexTable(pBtCoexist, bForceExec, 0x5ada5ada, 0x5ada5ada, 0xffffff, 0x3);
			break;
		case 3:
			halbtc8723b2ant_CoexTable(pBtCoexist, bForceExec, 0xaaaaaaaa, 0xaaaaaaaa, 0xffffff, 0x3);
			break;
		case 4:
			halbtc8723b2ant_CoexTable(pBtCoexist, bForceExec, 0xffffffff, 0xffffffff, 0xffffff, 0x3);
			break;
		case 5:
			halbtc8723b2ant_CoexTable(pBtCoexist, bForceExec, 0x5fff5fff, 0x5fff5fff, 0xffffff, 0x3);
			break;
		case 6:
			halbtc8723b2ant_CoexTable(pBtCoexist, bForceExec, 0x55ff55ff, 0x5a5a5a5a, 0xffffff, 0x3);
			break;
		case 7:
			halbtc8723b2ant_CoexTable(pBtCoexist, bForceExec, 0x55dd55dd, 0x5ada5ada, 0xffffff, 0x3);
			break;
		case 8:
			halbtc8723b2ant_CoexTable(pBtCoexist, bForceExec, 0x55dd55dd, 0x5ada5ada, 0xffffff, 0x3);
			break;
		case 9:
			halbtc8723b2ant_CoexTable(pBtCoexist, bForceExec, 0x55dd55dd, 0x5ada5ada, 0xffffff, 0x3);
			break;
		case 10:
			halbtc8723b2ant_CoexTable(pBtCoexist, bForceExec, 0x55dd55dd, 0x5ada5ada, 0xffffff, 0x3);
			break;
		case 11:
			halbtc8723b2ant_CoexTable(pBtCoexist, bForceExec, 0x55dd55dd, 0x5ada5ada, 0xffffff, 0x3);
			break;
		case 12:
			halbtc8723b2ant_CoexTable(pBtCoexist, bForceExec, 0x55dd55dd, 0x5ada5ada, 0xffffff, 0x3);
			break;
		case 13:
			halbtc8723b2ant_CoexTable(pBtCoexist, bForceExec, 0x5fff5fff, 0xaaaaaaaa, 0xffffff, 0x3);
			break;
		case 14:
			halbtc8723b2ant_CoexTable(pBtCoexist, bForceExec, 0x5fff5fff, 0x5ada5ada, 0xffffff, 0x3);
			break;
		case 15:
			halbtc8723b2ant_CoexTable(pBtCoexist, bForceExec, 0x55dd55dd, 0xaaaaaaaa, 0xffffff, 0x3);
			break;
		default:
			break;
	}
}

VOID
halbtc8723b2ant_SetFwIgnoreWlanAct(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bEnable
	)
{
	u1Byte			H2C_Parameter[1] ={0};
		
	if(bEnable)
	{
		H2C_Parameter[0] |= BIT0;		// function enable
	}
	
	RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], set FW for BT Ignore Wlan_Act, FW write 0x63=0x%x\n", 
		H2C_Parameter[0]));

	pBtCoexist->fBtcFillH2c(pBtCoexist, 0x63, 1, H2C_Parameter);
}

VOID
halbtc8723b2ant_SetLpsRpwm(
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
halbtc8723b2ant_LpsRpwm(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bForceExec,
	IN	u1Byte			lpsVal,
	IN	u1Byte			rpwmVal
	)
{
	BOOLEAN	bForceExecPwrCmd=FALSE;
	
	RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], %s set lps/rpwm=0x%x/0x%x \n", 
		(bForceExec? "force to":""), lpsVal, rpwmVal));
	pCoexDm->curLps = lpsVal;
	pCoexDm->curRpwm = rpwmVal;

	if(!bForceExec)
	{
		if( (pCoexDm->preLps == pCoexDm->curLps) &&
			(pCoexDm->preRpwm == pCoexDm->curRpwm) )
		{
			return;
		}
	}
	halbtc8723b2ant_SetLpsRpwm(pBtCoexist, lpsVal, rpwmVal);

	pCoexDm->preLps = pCoexDm->curLps;
	pCoexDm->preRpwm = pCoexDm->curRpwm;
}

VOID
halbtc8723b2ant_IgnoreWlanAct(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bForceExec,
	IN	BOOLEAN			bEnable
	)
{
	RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], %s turn Ignore WlanAct %s\n", 
		(bForceExec? "force to":""), (bEnable? "ON":"OFF")));
	pCoexDm->bCurIgnoreWlanAct = bEnable;

	if(!bForceExec)
	{
		if(pCoexDm->bPreIgnoreWlanAct == pCoexDm->bCurIgnoreWlanAct)
			return;
	}
	halbtc8723b2ant_SetFwIgnoreWlanAct(pBtCoexist, bEnable);

	pCoexDm->bPreIgnoreWlanAct = pCoexDm->bCurIgnoreWlanAct;
}

VOID
halbtc8723b2ant_SetFwPstdma(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			byte1,
	IN	u1Byte			byte2,
	IN	u1Byte			byte3,
	IN	u1Byte			byte4,
	IN	u1Byte			byte5
	)
{
	u1Byte			H2C_Parameter[5] ={0};


	 if ( (pCoexSta->bA2dpExist) && (pCoexSta->bHidExist) )
	 {
		byte5 = byte5 | 0x1;
	 }
	 
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
	
	RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], FW write 0x60(5bytes)=0x%x%08x\n", 
		H2C_Parameter[0], 
		H2C_Parameter[1]<<24|H2C_Parameter[2]<<16|H2C_Parameter[3]<<8|H2C_Parameter[4]));

	pBtCoexist->fBtcFillH2c(pBtCoexist, 0x60, 5, H2C_Parameter);
}

VOID
halbtc8723b2ant_SwMechanism1(
	IN	PBTC_COEXIST	pBtCoexist,	
	IN	BOOLEAN		bShrinkRxLPF,
	IN	BOOLEAN 	bLowPenaltyRA,
	IN	BOOLEAN		bLimitedDIG, 
	IN	BOOLEAN		bBTLNAConstrain
	) 
{
	/*
	u4Byte	wifiBw;
	
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_BW, &wifiBw);
	
	if(BTC_WIFI_BW_HT40 != wifiBw)  //only shrink RF Rx LPF for HT40
	{
		if (bShrinkRxLPF)
			bShrinkRxLPF = FALSE;
	}
	*/
	
	//halbtc8723b2ant_RfShrink(pBtCoexist, NORMAL_EXEC, bShrinkRxLPF);
	halbtc8723b2ant_LowPenaltyRa(pBtCoexist, NORMAL_EXEC, bLowPenaltyRA);
}

VOID
halbtc8723b2ant_SwMechanism2(
	IN	PBTC_COEXIST	pBtCoexist,	
	IN	BOOLEAN		bAGCTableShift,
	IN	BOOLEAN 	bADCBackOff,
	IN	BOOLEAN		bSWDACSwing,
	IN	u4Byte		dacSwingLvl
	) 
{
	//halbtc8723b2ant_AgcTable(pBtCoexist, NORMAL_EXEC, bAGCTableShift);
	//halbtc8723b2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, bADCBackOff);
	//halbtc8723b2ant_DacSwing(pBtCoexist, NORMAL_EXEC, bSWDACSwing, dacSwingLvl);
}

VOID
halbtc8723b2ant_SetAntPath(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte				antPosType,
	IN	BOOLEAN				bInitHwCfg,
	IN	BOOLEAN				bWifiOff
	)
{
	PBTC_BOARD_INFO pBoardInfo=&pBtCoexist->boardInfo;
	u4Byte			fwVer=0, u4Tmp=0;
	BOOLEAN			bPgExtSwitch=FALSE;
	BOOLEAN			bUseExtSwitch=FALSE;
	u1Byte			H2C_Parameter[2] ={0};

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_EXT_SWITCH, &bPgExtSwitch);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_FW_VER, &fwVer);	// [31:16]=fw ver, [15:0]=fw sub ver

	if((fwVer>0 && fwVer<0xc0000) || bPgExtSwitch)
		bUseExtSwitch = TRUE;

	if(bInitHwCfg)
	{
		pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0x39, 0x8, 0x1);
		pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x974, 0xff);
		pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0x944, 0x3, 0x3);
		pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x930, 0x77);
		pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0x67, 0x20, 0x1);

		if(fwVer >= 0x180000)
		{
			/* Use H2C to set GNT_BT to High to avoid A2DP click */
			H2C_Parameter[0] = 1;
		pBtCoexist->fBtcFillH2c(pBtCoexist, 0x6E, 1, H2C_Parameter);
		}
		else
		{
			pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x765, 0x18);
		}
		
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x948, 0x0);

		pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1, 0xfffff, 0x0); //WiFi TRx Mask off
		//remove due to interrupt is disabled that polling c2h will fail and delay 100ms.
		//pBtCoexist->fBtcSetBtReg(pBtCoexist, BTC_BT_REG_RF, 0x3c, 0x01); //BT TRx Mask off

		if(pBoardInfo->btdmAntPos == BTC_ANTENNA_AT_MAIN_PORT)
		{
			//tell firmware "no antenna inverse"
			H2C_Parameter[0] = 0;
		}
		else
		{
			//tell firmware "antenna inverse"
			H2C_Parameter[0] = 1;
		}

		if (bUseExtSwitch)
		{
			//ext switch type
			H2C_Parameter[1] = 1;
		}
		else
		{
			//int switch type
			H2C_Parameter[1] = 0;
		}
		pBtCoexist->fBtcFillH2c(pBtCoexist, 0x65, 2, H2C_Parameter);
	}
	else
	{
		if(fwVer >= 0x180000)
		{
			/* Use H2C to set GNT_BT to "Control by PTA"*/
			H2C_Parameter[0] = 0;
			pBtCoexist->fBtcFillH2c(pBtCoexist, 0x6E, 1, H2C_Parameter);			
		}
		else
		{
			pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x765, 0x0);
		}
	}

	// ext switch setting
	if(bUseExtSwitch)
	{
		if (bInitHwCfg)
		{
			// 0x4c[23]=0, 0x4c[24]=1  Antenna control by WL/BT
			u4Tmp = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x4c);
			u4Tmp &=~BIT23;
			u4Tmp |= BIT24;
			pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x4c, u4Tmp);
		}
		
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x948, 0x0); // fixed internal switch S1->WiFi, S0->BT
		switch(antPosType)
		{
			case BTC_ANT_WIFI_AT_MAIN:
				pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0x92c, 0x3, 0x1);	// ext switch main at wifi
				break;
			case BTC_ANT_WIFI_AT_AUX:
				pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0x92c, 0x3, 0x2);	// ext switch aux at wifi
				break;
		}	
	}
	else	// internal switch
	{
		if (bInitHwCfg)
		{
			// 0x4c[23]=0, 0x4c[24]=1  Antenna control by WL/BT
			u4Tmp = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x4c);
			u4Tmp |= BIT23;
			u4Tmp &=~BIT24;
			pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x4c, u4Tmp);
		}
		
		pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0x64, 0x1, 0x0); //fixed external switch S1->Main, S0->Aux
		switch(antPosType)
		{
			case BTC_ANT_WIFI_AT_MAIN:
				pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x948, 0x0); // fixed internal switch S1->WiFi, S0->BT
				break;
			case BTC_ANT_WIFI_AT_AUX:
				pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x948, 0x280); // fixed internal switch S0->WiFi, S1->BT
				break;
		}
	}
}

VOID
halbtc8723b2ant_PsTdma(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bForceExec,
	IN	BOOLEAN			bTurnOn,
	IN	u1Byte			type
	)
{
	BOOLEAN			bTurnOnByCnt=FALSE;
	u1Byte			psTdmaTypeByCnt=0;
	u1Byte			wifiRssiState1, btRssiState;
	s1Byte			nWiFiDurationAdjust = 0x0;
	u1Byte			psTdmaByte4Modify = 0x0;
	PBTC_BT_LINK_INFO pBtLinkInfo=&pBtCoexist->btLinkInfo;

	
	wifiRssiState1 = halbtc8723b2ant_WifiRssiState(pBtCoexist, 1, 2, BT_8723B_2ANT_WIFI_RSSI_COEXSWITCH_THRES, 0);
	btRssiState = halbtc8723b2ant_BtRssiState(2, BT_8723B_2ANT_BT_RSSI_COEXSWITCH_THRES, 0);

	RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], %s turn %s PS TDMA, type=%d\n", 
		(bForceExec? "force to":""), (bTurnOn? "ON":"OFF"), type));
	pCoexDm->bCurPsTdmaOn = bTurnOn;
	pCoexDm->curPsTdma = type;

	if (!(BTC_RSSI_HIGH(wifiRssiState1) && BTC_RSSI_HIGH(btRssiState)) && bTurnOn)
	{
		type = type +100;  //for WiFi RSSI low or BT RSSI low
		pCoexDm->bIsSwitchTo1dot5Ant = TRUE;
	}
	else
	{
		pCoexDm->bIsSwitchTo1dot5Ant = FALSE;
	}


	if(!bForceExec)
	{
		if( (pCoexDm->bPrePsTdmaOn == pCoexDm->bCurPsTdmaOn) &&
			(pCoexDm->prePsTdma == pCoexDm->curPsTdma) )
			return;
	}	

	if (pCoexSta->nScanAPNum <= 5)
	{	       	
		if (pCoexSta->nA2DPBitPool >= 45)
		  nWiFiDurationAdjust = -15;
		else if (pCoexSta->nA2DPBitPool >= 35)
		  nWiFiDurationAdjust = -10;	
		else
 		  nWiFiDurationAdjust = 5;		
	}
	else  if  (pCoexSta->nScanAPNum <= 20)
	{		   	
		if (pCoexSta->nA2DPBitPool >= 45)
		  nWiFiDurationAdjust = -15;
		else if (pCoexSta->nA2DPBitPool >= 35)
		  nWiFiDurationAdjust = -10;
		else
 		 nWiFiDurationAdjust = 0;	    	
	}
	else if  (pCoexSta->nScanAPNum <= 40)
	{
	 	if (pCoexSta->nA2DPBitPool >= 45)
		  nWiFiDurationAdjust = -15;
		else if (pCoexSta->nA2DPBitPool >= 35)
		  nWiFiDurationAdjust = -10;	
		else
 		  nWiFiDurationAdjust = -5;	
	}
	else
	{
	 	if (pCoexSta->nA2DPBitPool >= 45)
		  nWiFiDurationAdjust = -15;
		else if (pCoexSta->nA2DPBitPool >= 35)
		  nWiFiDurationAdjust = -10;	
		else
 		  nWiFiDurationAdjust = -10;	
	}

	if ( (pBtLinkInfo->bSlaveRole == TRUE)	&& (pBtLinkInfo->bA2dpExist) )
		psTdmaByte4Modify = 0x1;  //0x778 = 0x1 at wifi slot (no blocking BT Low-Pri pkts)
		
	
	if(bTurnOn)
	{
		switch(type)
		{
			case 1:
			default:
				halbtc8723b2ant_SetFwPstdma(pBtCoexist, 0xe3, 0x3c+nWiFiDurationAdjust, 0x03, 0xf1, 0x90|psTdmaByte4Modify);
				break;
			case 2:
				halbtc8723b2ant_SetFwPstdma(pBtCoexist, 0xe3, 0x2d+nWiFiDurationAdjust, 0x03, 0xf1,  0x90|psTdmaByte4Modify);
				break;
			case 3:
				halbtc8723b2ant_SetFwPstdma(pBtCoexist, 0xe3, 0x1c, 0x3, 0xf1,  0x90|psTdmaByte4Modify);
				break;
			case 4:
				halbtc8723b2ant_SetFwPstdma(pBtCoexist, 0xe3, 0x10, 0x03, 0xf1,  0x90|psTdmaByte4Modify);
				break;
			case 5:
				halbtc8723b2ant_SetFwPstdma(pBtCoexist, 0xe3, 0x3c+nWiFiDurationAdjust, 0x3, 0x70,  0x90|psTdmaByte4Modify);				
				break;
			case 6:
				halbtc8723b2ant_SetFwPstdma(pBtCoexist, 0xe3, 0x2d+nWiFiDurationAdjust, 0x3, 0x70,  0x90|psTdmaByte4Modify);
				break;
			case 7:
				halbtc8723b2ant_SetFwPstdma(pBtCoexist, 0xe3, 0x1c, 0x3, 0x70,  0x90|psTdmaByte4Modify);
				break;
			case 8:	
				halbtc8723b2ant_SetFwPstdma(pBtCoexist, 0xa3, 0x10, 0x3, 0x70,  0x90|psTdmaByte4Modify);
				break;
			case 9:	
				halbtc8723b2ant_SetFwPstdma(pBtCoexist, 0xe3, 0x3c+nWiFiDurationAdjust, 0x03, 0xf1,  0x90|psTdmaByte4Modify);
				break;
			case 10:	
				halbtc8723b2ant_SetFwPstdma(pBtCoexist, 0xe3, 0x2d+nWiFiDurationAdjust, 0x03, 0xf1,  0x90|psTdmaByte4Modify);
				break;
			case 11:	
				halbtc8723b2ant_SetFwPstdma(pBtCoexist, 0xe3, 0x1c, 0x3, 0xf1,  0x90|psTdmaByte4Modify);
				break;
			case 12:
				halbtc8723b2ant_SetFwPstdma(pBtCoexist, 0xe3, 0x10, 0x3, 0xf1,  0x90|psTdmaByte4Modify);
				break;
			case 13:
				halbtc8723b2ant_SetFwPstdma(pBtCoexist, 0xe3, 0x3c+nWiFiDurationAdjust, 0x3, 0x70,  0x90|psTdmaByte4Modify);		
				break;
			case 14:
				halbtc8723b2ant_SetFwPstdma(pBtCoexist, 0xe3, 0x2d+nWiFiDurationAdjust, 0x3, 0x70,  0x90|psTdmaByte4Modify);		
				break;
			case 15:
				halbtc8723b2ant_SetFwPstdma(pBtCoexist, 0xe3, 0x1c, 0x3, 0x70,  0x90|psTdmaByte4Modify);		
				break;
			case 16:
				halbtc8723b2ant_SetFwPstdma(pBtCoexist, 0xe3, 0x10, 0x3, 0x70,  0x90|psTdmaByte4Modify);		
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
				halbtc8723b2ant_SetFwPstdma(pBtCoexist, 0xe3, 0x3c+nWiFiDurationAdjust, 0x03, 0xf1, 0x90);
				break;
			case 101:
			case 105:
			case 171:	
				halbtc8723b2ant_SetFwPstdma(pBtCoexist, 0xd3, 0x3a+nWiFiDurationAdjust, 0x03, 0x70, 0x50|psTdmaByte4Modify);
				break;
			case 102:
			case 106:
			case 110:
			case 114:	
				halbtc8723b2ant_SetFwPstdma(pBtCoexist, 0xd3, 0x2d+nWiFiDurationAdjust, 0x03, 0x70, 0x50|psTdmaByte4Modify);
				break;	
			case 103:
			case 107:
			case 111:
			case 115:	
				halbtc8723b2ant_SetFwPstdma(pBtCoexist, 0xd3, 0x1c, 0x03, 0x70, 0x50|psTdmaByte4Modify);
				break;		
			case 104:
			case 108:
			case 112:
			case 116:	
				halbtc8723b2ant_SetFwPstdma(pBtCoexist, 0xd3, 0x10, 0x03, 0x70, 0x50|psTdmaByte4Modify);
				break;	
			case 109:
				halbtc8723b2ant_SetFwPstdma(pBtCoexist, 0xe3, 0x3c, 0x03, 0xf1, 0x90|psTdmaByte4Modify);
				break;
			case 113:	
				halbtc8723b2ant_SetFwPstdma(pBtCoexist, 0xe3, 0x3c, 0x03, 0x70, 0x90|psTdmaByte4Modify);
				break;
			case 121:	
				halbtc8723b2ant_SetFwPstdma(pBtCoexist, 0xe3, 0x15, 0x03, 0x70, 0x90|psTdmaByte4Modify);
				break;	
			case 22:
			case 122:
				halbtc8723b2ant_SetFwPstdma(pBtCoexist, 0xe3, 0x35, 0x03, 0x71, 0x11);
				break;
		}
	}
	else
	{
		// disable PS tdma
		switch(type)
		{
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

	// update pre state
	pCoexDm->bPrePsTdmaOn = pCoexDm->bCurPsTdmaOn;
	pCoexDm->prePsTdma = pCoexDm->curPsTdma;
}

VOID
halbtc8723b2ant_PsTdmaCheckForPowerSaveState(
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
			halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, FALSE, 1);
		}
	}
	else						// NO PS state
	{
		if(bNewPsState)
		{
			// will enter LPS state, turn off psTdma first
			halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, FALSE, 1);
		}
		else
		{
			// keep state under NO PS state, do nothing.
		}
	}
}

VOID
halbtc8723b2ant_PowerSaveState(
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
			pCoexSta->bForceLpsOn = FALSE;
			break;
		case BTC_PS_LPS_ON:
			halbtc8723b2ant_PsTdmaCheckForPowerSaveState(pBtCoexist, TRUE);
			halbtc8723b2ant_LpsRpwm(pBtCoexist, NORMAL_EXEC, lpsVal, rpwmVal);			
			// when coex force to enter LPS, do not enter 32k low power.
			bLowPwrDisable = TRUE;
			pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_ACT_DISABLE_LOW_POWER, &bLowPwrDisable);
			// power save must executed before psTdma.			
			pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_ACT_ENTER_LPS, NULL);
			pCoexSta->bForceLpsOn = TRUE;
			break;
		case BTC_PS_LPS_OFF:
			halbtc8723b2ant_PsTdmaCheckForPowerSaveState(pBtCoexist, FALSE);
			pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_ACT_LEAVE_LPS, NULL);
			pCoexSta->bForceLpsOn = FALSE;
			break;
		default:
			break;
	}
}


VOID
halbtc8723b2ant_CoexAllOff(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	// fw all off
	halbtc8723b2ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);		
	halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, FALSE, 1);
	halbtc8723b2ant_FwDacSwingLvl(pBtCoexist, NORMAL_EXEC, 6);
	halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 0);

	// sw all off
	halbtc8723b2ant_SwMechanism1(pBtCoexist,FALSE,FALSE,FALSE,FALSE);
	halbtc8723b2ant_SwMechanism2(pBtCoexist,FALSE,FALSE,FALSE,0x18);

	// hw all off
	//pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);
	halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 0);
}

VOID
halbtc8723b2ant_InitCoexDm(
	IN	PBTC_COEXIST		pBtCoexist
	)
{	
	// force to reset coex mechanism
	halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 0);
	
	halbtc8723b2ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);		
	halbtc8723b2ant_PsTdma(pBtCoexist, FORCE_EXEC, FALSE, 1);
	halbtc8723b2ant_FwDacSwingLvl(pBtCoexist, FORCE_EXEC, 6);
	halbtc8723b2ant_DecBtPwr(pBtCoexist, FORCE_EXEC, 0);

	halbtc8723b2ant_SwMechanism1(pBtCoexist,FALSE,FALSE,FALSE,FALSE);
	halbtc8723b2ant_SwMechanism2(pBtCoexist,FALSE,FALSE,FALSE,0x18);

	pCoexSta->popEventCnt = 0;

}

VOID
halbtc8723b2ant_ActionBtInquiry(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	u1Byte		wifiRssiState, wifiRssiState1, btRssiState;
	BOOLEAN	bWifiConnected=FALSE;
	BOOLEAN	bLowPwrDisable=TRUE;
	BOOLEAN		bScan=FALSE, bLink=FALSE, bRoam=FALSE;


	wifiRssiState = halbtc8723b2ant_WifiRssiState(pBtCoexist, 0, 2, 15, 0);
	wifiRssiState1 = halbtc8723b2ant_WifiRssiState(pBtCoexist, 1, 2, BT_8723B_2ANT_WIFI_RSSI_COEXSWITCH_THRES, 0);
	btRssiState = halbtc8723b2ant_BtRssiState(2, BT_8723B_2ANT_BT_RSSI_COEXSWITCH_THRES, 0);
	
	pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_ACT_DISABLE_LOW_POWER, &bLowPwrDisable);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_CONNECTED, &bWifiConnected);

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_SCAN, &bScan);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_LINK, &bLink);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_ROAM, &bRoam);
		
	
	halbtc8723b2ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);		

	if(bScan || bLink || bRoam)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Wifi link process + BT Inq/Page!!\n"));
		halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 15);		
		halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 22);
	}
	else if(bWifiConnected)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Wifi connected + BT Inq/Page!!\n"));
		halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 15);		
		halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 22);
	}
	else
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Wifi no-link + BT Inq/Page!!\n"));
		halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 0);
		halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, FALSE, 1);
	}	
	
	halbtc8723b2ant_FwDacSwingLvl(pBtCoexist, FORCE_EXEC, 6);
	halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 0);

	halbtc8723b2ant_SwMechanism1(pBtCoexist,FALSE,FALSE,FALSE,FALSE);
	halbtc8723b2ant_SwMechanism2(pBtCoexist,FALSE,FALSE,FALSE,0x18);
/*
	pCoexDm->bNeedRecover0x948 = TRUE;
	pCoexDm->backup0x948 = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x948);

	halbtc8723b2ant_SetAntPath(pBtCoexist, BTC_ANT_WIFI_AT_AUX, FALSE, FALSE);
*/	
}


VOID
halbtc8723b2ant_ActionWiFiLinkProcess(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	u4Byte 	u4Tmp;
	u1Byte 	u1Tmpa, u1Tmpb;
	
	halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 15);		
	halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 22);

	halbtc8723b2ant_SwMechanism1(pBtCoexist,FALSE,FALSE,FALSE,FALSE);
	halbtc8723b2ant_SwMechanism2(pBtCoexist,FALSE,FALSE,FALSE,0x18);


	 u4Tmp = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x948);
        u1Tmpa = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x765);
	 u1Tmpb = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x76e);

	 RT_TRACE(COMP_COEX, DBG_LOUD, ("############# [BTCoex], 0x948=0x%x, 0x765=0x%x, 0x76e=0x%x\n",
	   	      u4Tmp,  u1Tmpa, u1Tmpb));
}

BOOLEAN
halbtc8723b2ant_ActionWifiIdleProcess(
	IN	PBTC_COEXIST		pBtCoexist
	)
{	
	u1Byte		wifiRssiState, wifiRssiState1, btRssiState;
	u4Byte		wifiBw;
	u1Byte		apNum=0;

	wifiRssiState = halbtc8723b2ant_WifiRssiState(pBtCoexist, 0, 2, 15, 0);
	//wifiRssiState1 = halbtc8723b2ant_WifiRssiState(pBtCoexist, 1, 2, BT_8723B_2ANT_WIFI_RSSI_COEXSWITCH_THRES, 0);
	wifiRssiState1 = halbtc8723b2ant_WifiRssiState(pBtCoexist, 1, 2, BT_8723B_2ANT_WIFI_RSSI_COEXSWITCH_THRES-20, 0);
	btRssiState = halbtc8723b2ant_BtRssiState(2, BT_8723B_2ANT_BT_RSSI_COEXSWITCH_THRES, 0);

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U1_AP_NUM, &apNum);

	// define the office environment
	if(BTC_RSSI_HIGH(wifiRssiState1) && 
			(pCoexSta->bHidExist == TRUE) && (pCoexSta->bA2dpExist == TRUE))
	{

		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Wifi  idle process for BT HID+A2DP exist!!\n"));
		
		halbtc8723b2ant_DacSwing(pBtCoexist, NORMAL_EXEC, TRUE, 0x6);
		halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 0);

		// sw all off
		halbtc8723b2ant_SwMechanism1(pBtCoexist,FALSE,FALSE,FALSE,FALSE);
		halbtc8723b2ant_SwMechanism2(pBtCoexist,FALSE,FALSE,FALSE,0x18);

		halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 0);

		halbtc8723b2ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);		
		halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, FALSE, 1);

	  	return TRUE;
	}
	else
	{
		halbtc8723b2ant_DacSwing(pBtCoexist, NORMAL_EXEC, TRUE, 0x18);
		return FALSE;	
	}
	
	
}



BOOLEAN
halbtc8723b2ant_IsCommonAction(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	u1Byte			btRssiState=BTC_RSSI_STATE_HIGH;
	BOOLEAN			bCommon=FALSE, bWifiConnected=FALSE, bWifiBusy=FALSE;
	BOOLEAN			bBtHsOn=FALSE, bLowPwrDisable=FALSE;
	BOOLEAN			bAsus8723b=FALSE;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_HS_OPERATION, &bBtHsOn);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_CONNECTED, &bWifiConnected);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_BUSY, &bWifiBusy);

	if(!bWifiConnected)
	{
		bLowPwrDisable = FALSE;
		pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_ACT_DISABLE_LOW_POWER, &bLowPwrDisable);
		halbtc8723b2ant_LimitedRx(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0x8);

		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Wifi non-connected idle!!\n"));

		pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);
		halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 0);
		
		halbtc8723b2ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);			
		halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, FALSE, 1);
		halbtc8723b2ant_FwDacSwingLvl(pBtCoexist, NORMAL_EXEC, 6);
		halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 0);
		
 		halbtc8723b2ant_SwMechanism1(pBtCoexist,FALSE,FALSE,FALSE,FALSE);
		halbtc8723b2ant_SwMechanism2(pBtCoexist,FALSE,FALSE,FALSE,0x18);

		bCommon = TRUE;
	}
	else
	{
		if(BT_8723B_2ANT_BT_STATUS_NON_CONNECTED_IDLE == pCoexDm->btStatus)
		{
			bLowPwrDisable = FALSE;
			pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_ACT_DISABLE_LOW_POWER, &bLowPwrDisable);
			halbtc8723b2ant_LimitedRx(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0x8);

			RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Wifi connected + BT non connected-idle!!\n"));

			pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);
			halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 0);

			halbtc8723b2ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);			
			halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, FALSE, 1);		
			halbtc8723b2ant_FwDacSwingLvl(pBtCoexist, NORMAL_EXEC, 0xb);
			halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 0);

	      	halbtc8723b2ant_SwMechanism1(pBtCoexist,FALSE,FALSE,FALSE,FALSE);
			halbtc8723b2ant_SwMechanism2(pBtCoexist,FALSE,FALSE,FALSE,0x18);

			bCommon = TRUE;
		}
		else if(BT_8723B_2ANT_BT_STATUS_CONNECTED_IDLE == pCoexDm->btStatus)
		{
			bLowPwrDisable = TRUE;
			pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_ACT_DISABLE_LOW_POWER, &bLowPwrDisable);

			if(bBtHsOn)
				return FALSE;
			RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Wifi connected + BT connected-idle!!\n"));
			halbtc8723b2ant_LimitedRx(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0x8);

			pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);
			halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 0);

			halbtc8723b2ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);			
			halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, FALSE, 1);
			halbtc8723b2ant_FwDacSwingLvl(pBtCoexist, NORMAL_EXEC, 0xb);
			halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 0);

			halbtc8723b2ant_SwMechanism1(pBtCoexist,TRUE,FALSE,FALSE,FALSE);
			halbtc8723b2ant_SwMechanism2(pBtCoexist,FALSE,FALSE,FALSE,0x18);

			bCommon = TRUE;
		}
		else
		{
			bLowPwrDisable = TRUE;
			pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_ACT_DISABLE_LOW_POWER, &bLowPwrDisable);

			if(bWifiBusy)
			{
				RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Wifi Connected-Busy + BT Busy!!\n"));
				pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_IS_ASUS_8723B, &bAsus8723b);
				if(!bAsus8723b)
					bCommon = FALSE;
				else
					bCommon = halbtc8723b2ant_ActionWifiIdleProcess(pBtCoexist);	
			}
			else
			{
				RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Wifi Connected-Idle + BT Busy!!\n"));
				//bCommon = FALSE;	
				bCommon = halbtc8723b2ant_ActionWifiIdleProcess(pBtCoexist);			
			}
		}	
	}

	return bCommon;
}
VOID
halbtc8723b2ant_TdmaDurationAdjust(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bScoHid,
	IN	BOOLEAN			bTxPause,
	IN	u1Byte			maxInterval
	)
{
	static s4Byte		up,dn,m,n,WaitCount;
	s4Byte			result;   //0: no change, +1: increase WiFi duration, -1: decrease WiFi duration
	u1Byte			retryCount=0;

	RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], TdmaDurationAdjust()\n"));

	if(!pCoexDm->bAutoTdmaAdjust)
	{
		pCoexDm->bAutoTdmaAdjust = TRUE;
		RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], first run TdmaDurationAdjust()!!\n"));
		{
			if(bScoHid)
			{
				if(bTxPause)
				{
					if(maxInterval == 1)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 13);
						pCoexDm->psTdmaDuAdjType = 13;	
					}
					else if(maxInterval == 2)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 14);
						pCoexDm->psTdmaDuAdjType = 14;	
					}
					else if(maxInterval == 3)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 15);
						pCoexDm->psTdmaDuAdjType = 15;	
					}
					else
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 15);
						pCoexDm->psTdmaDuAdjType = 15;
					}
				}
				else
				{
					if(maxInterval == 1)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 9);
						pCoexDm->psTdmaDuAdjType = 9;	
					}
					else if(maxInterval == 2)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 10);
						pCoexDm->psTdmaDuAdjType = 10;	
					}
					else if(maxInterval == 3)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 11);
						pCoexDm->psTdmaDuAdjType = 11;
					}
					else
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 11);
						pCoexDm->psTdmaDuAdjType = 11;
					}
				}
			}
			else
			{
				if(bTxPause)
				{
					if(maxInterval == 1)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 5);
						pCoexDm->psTdmaDuAdjType = 5;	
					}
					else if(maxInterval == 2)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 6);
						pCoexDm->psTdmaDuAdjType = 6;	
					}
					else if(maxInterval == 3)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 7);
						pCoexDm->psTdmaDuAdjType = 7;
					}
					else
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 7);
						pCoexDm->psTdmaDuAdjType = 7;
					}
				}
				else
				{
					if(maxInterval == 1)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 1);
						pCoexDm->psTdmaDuAdjType = 1;	
					}
					else if(maxInterval == 2)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 2);
						pCoexDm->psTdmaDuAdjType = 2;	
					}
					else if(maxInterval == 3)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 3);
						pCoexDm->psTdmaDuAdjType = 3;
					}
					else
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 3);
						pCoexDm->psTdmaDuAdjType = 3;
					}
				}
			}
		}
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

		if ( (pCoexSta->lowPriorityTx) > 1050 ||  (pCoexSta->lowPriorityRx) > 1250 )
			retryCount++;
		
		RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], retryCount = %d\n", retryCount));
		RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], up=%d, dn=%d, m=%d, n=%d, WaitCount=%d\n", 
			up, dn, m, n, WaitCount));
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
				RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], Increase wifi duration!!\n"));
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
				RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], Decrease wifi duration for retryCounter<3!!\n"));
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
			RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], Decrease wifi duration for retryCounter>3!!\n"));
		}

		RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], max Interval = %d\n", maxInterval));
		if(maxInterval == 1)
		{
			if(bTxPause)
			{
				RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], TxPause = 1\n"));

				if(pCoexDm->curPsTdma == 71)
				{
					halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 5);
					pCoexDm->psTdmaDuAdjType = 5;
				}
				else if(pCoexDm->curPsTdma == 1)
				{
					halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 5);
					pCoexDm->psTdmaDuAdjType = 5;
				}
				else if(pCoexDm->curPsTdma == 2)
				{
					halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 6);
					pCoexDm->psTdmaDuAdjType = 6;
				}
				else if(pCoexDm->curPsTdma == 3)
				{
					halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 7);
					pCoexDm->psTdmaDuAdjType = 7;
				}
				else if(pCoexDm->curPsTdma == 4)
				{
					halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 8);
					pCoexDm->psTdmaDuAdjType = 8;
				}
				if(pCoexDm->curPsTdma == 9)
				{
					halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 13);
					pCoexDm->psTdmaDuAdjType = 13;
				}
				else if(pCoexDm->curPsTdma == 10)
				{
					halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 14);
					pCoexDm->psTdmaDuAdjType = 14;
				}
				else if(pCoexDm->curPsTdma == 11)
				{
					halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 15);
					pCoexDm->psTdmaDuAdjType = 15;
				}
				else if(pCoexDm->curPsTdma == 12)
				{
					halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 16);
					pCoexDm->psTdmaDuAdjType = 16;
				}
				
				if(result == -1)
				{					
					if(pCoexDm->curPsTdma == 5)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 6);
						pCoexDm->psTdmaDuAdjType = 6;
					}
					else if(pCoexDm->curPsTdma == 6)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 7);
						pCoexDm->psTdmaDuAdjType = 7;
					}
					else if(pCoexDm->curPsTdma == 7)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 8);
						pCoexDm->psTdmaDuAdjType = 8;
					}
					else if(pCoexDm->curPsTdma == 13)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 14);
						pCoexDm->psTdmaDuAdjType = 14;
					}
					else if(pCoexDm->curPsTdma == 14)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 15);
						pCoexDm->psTdmaDuAdjType = 15;
					}
					else if(pCoexDm->curPsTdma == 15)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 16);
						pCoexDm->psTdmaDuAdjType = 16;
					}
				} 
				else if (result == 1)
				{
					if(pCoexDm->curPsTdma == 8)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 7);
						pCoexDm->psTdmaDuAdjType = 7;
					}
					else if(pCoexDm->curPsTdma == 7)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 6);
						pCoexDm->psTdmaDuAdjType = 6;
					}
					else if(pCoexDm->curPsTdma == 6)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 5);
						pCoexDm->psTdmaDuAdjType = 5;
					}
					else if(pCoexDm->curPsTdma == 16)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 15);
						pCoexDm->psTdmaDuAdjType = 15;
					}
					else if(pCoexDm->curPsTdma == 15)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 14);
						pCoexDm->psTdmaDuAdjType = 14;
					}
					else if(pCoexDm->curPsTdma == 14)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 13);
						pCoexDm->psTdmaDuAdjType = 13;
					}
				}
			}
			else
			{
				RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], TxPause = 0\n"));
				if(pCoexDm->curPsTdma == 5)
				{
					halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 71);
					pCoexDm->psTdmaDuAdjType = 71;
				}
				else if(pCoexDm->curPsTdma == 6)
				{
					halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 2);
					pCoexDm->psTdmaDuAdjType = 2;
				}
				else if(pCoexDm->curPsTdma == 7)
				{
					halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 3);
					pCoexDm->psTdmaDuAdjType = 3;
				}
				else if(pCoexDm->curPsTdma == 8)
				{
					halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 4);
					pCoexDm->psTdmaDuAdjType = 4;
				}
				if(pCoexDm->curPsTdma == 13)
				{
					halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 9);
					pCoexDm->psTdmaDuAdjType = 9;
				}
				else if(pCoexDm->curPsTdma == 14)
				{
					halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 10);
					pCoexDm->psTdmaDuAdjType = 10;
				}
				else if(pCoexDm->curPsTdma == 15)
				{
					halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 11);
					pCoexDm->psTdmaDuAdjType = 11;
				}
				else if(pCoexDm->curPsTdma == 16)
				{
					halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 12);
					pCoexDm->psTdmaDuAdjType = 12;
				}
				
				if(result == -1)
				{
					if(pCoexDm->curPsTdma == 71)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 1);
						pCoexDm->psTdmaDuAdjType = 1;
					}
					else if(pCoexDm->curPsTdma == 1)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 2);
						pCoexDm->psTdmaDuAdjType = 2;
					}
					else if(pCoexDm->curPsTdma == 2)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 3);
						pCoexDm->psTdmaDuAdjType = 3;
					}
					else if(pCoexDm->curPsTdma == 3)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 4);
						pCoexDm->psTdmaDuAdjType = 4;
					}
					else if(pCoexDm->curPsTdma == 9)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 10);
						pCoexDm->psTdmaDuAdjType = 10;
					}
					else if(pCoexDm->curPsTdma == 10)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 11);
						pCoexDm->psTdmaDuAdjType = 11;
					}
					else if(pCoexDm->curPsTdma == 11)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 12);
						pCoexDm->psTdmaDuAdjType = 12;
					}
				} 
				else if (result == 1)
				{
					if(pCoexDm->curPsTdma == 4)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 3);
						pCoexDm->psTdmaDuAdjType = 3;
					}
					else if(pCoexDm->curPsTdma == 3)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 2);
						pCoexDm->psTdmaDuAdjType = 2;
					}
					else if(pCoexDm->curPsTdma == 2)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 1);
						pCoexDm->psTdmaDuAdjType = 1;
					}
					else if(pCoexDm->curPsTdma == 1)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 71);
						pCoexDm->psTdmaDuAdjType = 71;
					}
					else if(pCoexDm->curPsTdma == 12)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 11);
						pCoexDm->psTdmaDuAdjType = 11;
					}
					else if(pCoexDm->curPsTdma == 11)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 10);
						pCoexDm->psTdmaDuAdjType = 10;
					}
					else if(pCoexDm->curPsTdma == 10)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 9);
						pCoexDm->psTdmaDuAdjType = 9;
					}
				}
			}
		}
		else if(maxInterval == 2)
		{
			if(bTxPause)
			{
				RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], TxPause = 1\n"));
				if(pCoexDm->curPsTdma == 1)
				{
					halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 6);
					pCoexDm->psTdmaDuAdjType = 6;
				}
				else if(pCoexDm->curPsTdma == 2)
				{
					halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 6);
					pCoexDm->psTdmaDuAdjType = 6;
				}
				else if(pCoexDm->curPsTdma == 3)
				{
					halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 7);
					pCoexDm->psTdmaDuAdjType = 7;
				}
				else if(pCoexDm->curPsTdma == 4)
				{
					halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 8);
					pCoexDm->psTdmaDuAdjType = 8;
				}
				if(pCoexDm->curPsTdma == 9)
				{
					halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 14);
					pCoexDm->psTdmaDuAdjType = 14;
				}
				else if(pCoexDm->curPsTdma == 10)
				{
					halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 14);
					pCoexDm->psTdmaDuAdjType = 14;
				}
				else if(pCoexDm->curPsTdma == 11)
				{
					halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 15);
					pCoexDm->psTdmaDuAdjType = 15;
				}
				else if(pCoexDm->curPsTdma == 12)
				{
					halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 16);
					pCoexDm->psTdmaDuAdjType = 16;
				}
				if(result == -1)
				{
					if(pCoexDm->curPsTdma == 5) 
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 6);
						pCoexDm->psTdmaDuAdjType = 6;
					}
					else if(pCoexDm->curPsTdma == 6)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 7);
						pCoexDm->psTdmaDuAdjType = 7;
					}
					else if(pCoexDm->curPsTdma == 7)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 8);
						pCoexDm->psTdmaDuAdjType = 8;
					}
					else if(pCoexDm->curPsTdma == 13)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 14);
						pCoexDm->psTdmaDuAdjType = 14;
					}
					else if(pCoexDm->curPsTdma == 14)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 15);
						pCoexDm->psTdmaDuAdjType = 15;
					}
					else if(pCoexDm->curPsTdma == 15)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 16);
						pCoexDm->psTdmaDuAdjType = 16;
					}
				} 
				else if (result == 1)
				{
					if(pCoexDm->curPsTdma == 8)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 7);
						pCoexDm->psTdmaDuAdjType = 7;
					}
					else if(pCoexDm->curPsTdma == 7)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 6);
						pCoexDm->psTdmaDuAdjType = 6;
					}
					else if(pCoexDm->curPsTdma == 6)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 6);
						pCoexDm->psTdmaDuAdjType = 6;
					}					
					else if(pCoexDm->curPsTdma == 16)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 15);
						pCoexDm->psTdmaDuAdjType = 15;
					}
					else if(pCoexDm->curPsTdma == 15)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 14);
						pCoexDm->psTdmaDuAdjType = 14;
					}
					else if(pCoexDm->curPsTdma == 14)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 14);
						pCoexDm->psTdmaDuAdjType = 14;
					}
				}
			}
			else
			{
				RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], TxPause = 0\n"));
				if(pCoexDm->curPsTdma == 5)
				{
					halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 2);
					pCoexDm->psTdmaDuAdjType = 2;
				}
				else if(pCoexDm->curPsTdma == 6)
				{
					halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 2);
					pCoexDm->psTdmaDuAdjType = 2;
				}
				else if(pCoexDm->curPsTdma == 7)
				{
					halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 3);
					pCoexDm->psTdmaDuAdjType = 3;
				}
				else if(pCoexDm->curPsTdma == 8)
				{
					halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 4);
					pCoexDm->psTdmaDuAdjType = 4;
				}
				if(pCoexDm->curPsTdma == 13)
				{
					halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 10);
					pCoexDm->psTdmaDuAdjType = 10;
				}
				else if(pCoexDm->curPsTdma == 14)
				{
					halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 10);
					pCoexDm->psTdmaDuAdjType = 10;
				}
				else if(pCoexDm->curPsTdma == 15)
				{
					halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 11);
					pCoexDm->psTdmaDuAdjType = 11;
				}
				else if(pCoexDm->curPsTdma == 16)
				{
					halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 12);
					pCoexDm->psTdmaDuAdjType = 12;
				}
				if(result == -1)
				{
					if(pCoexDm->curPsTdma == 1)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 2);
						pCoexDm->psTdmaDuAdjType = 2;
					}
					else if(pCoexDm->curPsTdma == 2)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 3);
						pCoexDm->psTdmaDuAdjType = 3;
					}
					else if(pCoexDm->curPsTdma == 3)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 4);
						pCoexDm->psTdmaDuAdjType = 4;
					}
					else if(pCoexDm->curPsTdma == 9)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 10);
						pCoexDm->psTdmaDuAdjType = 10;
					}
					else if(pCoexDm->curPsTdma == 10)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 11);
						pCoexDm->psTdmaDuAdjType = 11;
					}
					else if(pCoexDm->curPsTdma == 11)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 12);
						pCoexDm->psTdmaDuAdjType = 12;
					}
				} 
				else if (result == 1)
				{
					if(pCoexDm->curPsTdma == 4)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 3);
						pCoexDm->psTdmaDuAdjType = 3;
					}
					else if(pCoexDm->curPsTdma == 3)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 2);
						pCoexDm->psTdmaDuAdjType = 2;
					}
					else if(pCoexDm->curPsTdma == 2)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 2);
						pCoexDm->psTdmaDuAdjType = 2;
					}
					else if(pCoexDm->curPsTdma == 12)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 11);
						pCoexDm->psTdmaDuAdjType = 11;
					}
					else if(pCoexDm->curPsTdma == 11)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 10);
						pCoexDm->psTdmaDuAdjType = 10;
					}
					else if(pCoexDm->curPsTdma == 10)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 10);
						pCoexDm->psTdmaDuAdjType = 10;
					}
				}
			}
		}
		else if(maxInterval == 3)
		{
			if(bTxPause)
			{
				RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], TxPause = 1\n"));
				if(pCoexDm->curPsTdma == 1)
				{
					halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 7);
					pCoexDm->psTdmaDuAdjType = 7;
				}
				else if(pCoexDm->curPsTdma == 2)
				{
					halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 7);
					pCoexDm->psTdmaDuAdjType = 7;
				}
				else if(pCoexDm->curPsTdma == 3)
				{
					halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 7);
					pCoexDm->psTdmaDuAdjType = 7;
				}
				else if(pCoexDm->curPsTdma == 4)
				{
					halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 8);
					pCoexDm->psTdmaDuAdjType = 8;
				}
				if(pCoexDm->curPsTdma == 9)
				{
					halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 15);
					pCoexDm->psTdmaDuAdjType = 15;
				}
				else if(pCoexDm->curPsTdma == 10)
				{
					halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 15);
					pCoexDm->psTdmaDuAdjType = 15;
				}
				else if(pCoexDm->curPsTdma == 11)
				{
					halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 15);
					pCoexDm->psTdmaDuAdjType = 15;
				}
				else if(pCoexDm->curPsTdma == 12)
				{
					halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 16);
					pCoexDm->psTdmaDuAdjType = 16;
				}
				if(result == -1)
				{
					if(pCoexDm->curPsTdma == 5) 
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 7);
						pCoexDm->psTdmaDuAdjType = 7;
					}
					else if(pCoexDm->curPsTdma == 6)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 7);
						pCoexDm->psTdmaDuAdjType = 7;
					}
					else if(pCoexDm->curPsTdma == 7)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 8);
						pCoexDm->psTdmaDuAdjType = 8;
					}
					else if(pCoexDm->curPsTdma == 13)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 15);
						pCoexDm->psTdmaDuAdjType = 15;
					}
					else if(pCoexDm->curPsTdma == 14)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 15);
						pCoexDm->psTdmaDuAdjType = 15;
					}
					else if(pCoexDm->curPsTdma == 15)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 16);
						pCoexDm->psTdmaDuAdjType = 16;
					}
				} 
				else if (result == 1)
				{
					if(pCoexDm->curPsTdma == 8)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 7);
						pCoexDm->psTdmaDuAdjType = 7;
					}
					else if(pCoexDm->curPsTdma == 7)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 7);
						pCoexDm->psTdmaDuAdjType = 7;
					}
					else if(pCoexDm->curPsTdma == 6)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 7);
						pCoexDm->psTdmaDuAdjType = 7;
					}					
					else if(pCoexDm->curPsTdma == 16)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 15);
						pCoexDm->psTdmaDuAdjType = 15;
					}
					else if(pCoexDm->curPsTdma == 15)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 15);
						pCoexDm->psTdmaDuAdjType = 15;
					}
					else if(pCoexDm->curPsTdma == 14)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 15);
						pCoexDm->psTdmaDuAdjType = 15;
					}
				}
			}
			else
			{
				RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], TxPause = 0\n"));
				if(pCoexDm->curPsTdma == 5)
				{
					halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 3);
					pCoexDm->psTdmaDuAdjType = 3;
				}
				else if(pCoexDm->curPsTdma == 6)
				{
					halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 3);
					pCoexDm->psTdmaDuAdjType = 3;
				}
				else if(pCoexDm->curPsTdma == 7)
				{
					halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 3);
					pCoexDm->psTdmaDuAdjType = 3;
				}
				else if(pCoexDm->curPsTdma == 8)
				{
					halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 4);
					pCoexDm->psTdmaDuAdjType = 4;
				}
				if(pCoexDm->curPsTdma == 13)
				{
					halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 11);
					pCoexDm->psTdmaDuAdjType = 11;
				}
				else if(pCoexDm->curPsTdma == 14)
				{
					halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 11);
					pCoexDm->psTdmaDuAdjType = 11;
				}
				else if(pCoexDm->curPsTdma == 15)
				{
					halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 11);
					pCoexDm->psTdmaDuAdjType = 11;
				}
				else if(pCoexDm->curPsTdma == 16)
				{
					halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 12);
					pCoexDm->psTdmaDuAdjType = 12;
				}
				if(result == -1)
				{
					if(pCoexDm->curPsTdma == 1)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 3);
						pCoexDm->psTdmaDuAdjType = 3;
					}
					else if(pCoexDm->curPsTdma == 2)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 3);
						pCoexDm->psTdmaDuAdjType = 3;
					}
					else if(pCoexDm->curPsTdma == 3)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 4);
						pCoexDm->psTdmaDuAdjType = 4;
					}
					else if(pCoexDm->curPsTdma == 9)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 11);
						pCoexDm->psTdmaDuAdjType = 11;
					}
					else if(pCoexDm->curPsTdma == 10)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 11);
						pCoexDm->psTdmaDuAdjType = 11;
					}
					else if(pCoexDm->curPsTdma == 11)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 12);
						pCoexDm->psTdmaDuAdjType = 12;
					}
				} 
				else if (result == 1)
				{
					if(pCoexDm->curPsTdma == 4)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 3);
						pCoexDm->psTdmaDuAdjType = 3;
					}
					else if(pCoexDm->curPsTdma == 3)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 3);
						pCoexDm->psTdmaDuAdjType = 3;
					}
					else if(pCoexDm->curPsTdma == 2)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 3);
						pCoexDm->psTdmaDuAdjType = 3;
					}
					else if(pCoexDm->curPsTdma == 12)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 11);
						pCoexDm->psTdmaDuAdjType = 11;
					}
					else if(pCoexDm->curPsTdma == 11)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 11);
						pCoexDm->psTdmaDuAdjType = 11;
					}
					else if(pCoexDm->curPsTdma == 10)
					{
						halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 11);
						pCoexDm->psTdmaDuAdjType = 11;
					}
				}
			}
		}
	}

	// if current PsTdma not match with the recorded one (when scan, dhcp...), 
	// then we have to adjust it back to the previous record one.
	if(pCoexDm->curPsTdma != pCoexDm->psTdmaDuAdjType)
	{
		BOOLEAN	bScan=FALSE, bLink=FALSE, bRoam=FALSE;
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], PsTdma type dismatch!!!, curPsTdma=%d, recordPsTdma=%d\n", 
			pCoexDm->curPsTdma, pCoexDm->psTdmaDuAdjType));

		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_SCAN, &bScan);
		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_LINK, &bLink);
		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_ROAM, &bRoam);
		
		if( !bScan && !bLink && !bRoam)
		{
			halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, pCoexDm->psTdmaDuAdjType);
		}
		else
		{
			RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], roaming/link/scan is under progress, will adjust next time!!!\n"));
		}
	}
}

// SCO only or SCO+PAN(HS)
VOID
halbtc8723b2ant_ActionSco(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	u1Byte	wifiRssiState, btRssiState;
	u4Byte	wifiBw;

	wifiRssiState = halbtc8723b2ant_WifiRssiState(pBtCoexist, 0, 2, 15, 0);
	btRssiState = halbtc8723b2ant_BtRssiState(2, BT_8723B_2ANT_BT_RSSI_COEXSWITCH_THRES, 0);

	pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);

	halbtc8723b2ant_LimitedRx(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0x8);
	
	halbtc8723b2ant_FwDacSwingLvl(pBtCoexist, NORMAL_EXEC, 4);

	if(BTC_RSSI_HIGH(btRssiState))
		halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 2);
	else	
		halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 0);
	
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_BW, &wifiBw);

	if (BTC_WIFI_BW_LEGACY == wifiBw) //for SCO quality at 11b/g mode
	{
		halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 2);
	}
	else  //for SCO quality & wifi performance balance at 11n mode
	{
		halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 8);
	}

	halbtc8723b2ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);			
	halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, FALSE, 0); //for voice quality

	// sw mechanism
	if(BTC_WIFI_BW_HT40 == wifiBw)
	{
		if( (wifiRssiState == BTC_RSSI_STATE_HIGH) ||
			(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH) )
		{
			halbtc8723b2ant_SwMechanism1(pBtCoexist,TRUE,TRUE,FALSE,FALSE);
			halbtc8723b2ant_SwMechanism2(pBtCoexist,TRUE,FALSE,TRUE,0x4);			
		}
		else
		{
			halbtc8723b2ant_SwMechanism1(pBtCoexist,TRUE,TRUE,FALSE,FALSE);
			halbtc8723b2ant_SwMechanism2(pBtCoexist,FALSE,FALSE,TRUE,0x4);	
		}		
	}
	else
	{
		if( (wifiRssiState == BTC_RSSI_STATE_HIGH) ||
			(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH) )
		{
			halbtc8723b2ant_SwMechanism1(pBtCoexist,FALSE,TRUE,FALSE,FALSE);
			halbtc8723b2ant_SwMechanism2(pBtCoexist,TRUE,FALSE,TRUE,0x4);
		}
		else
		{
			halbtc8723b2ant_SwMechanism1(pBtCoexist,FALSE,TRUE,FALSE,FALSE);
			halbtc8723b2ant_SwMechanism2(pBtCoexist,FALSE,FALSE,TRUE,0x4);
		}		
	}
}


VOID
halbtc8723b2ant_ActionHid(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	u1Byte	wifiRssiState, btRssiState;	
	u4Byte	wifiBw;

	wifiRssiState = halbtc8723b2ant_WifiRssiState(pBtCoexist, 0, 2, 15, 0);
	btRssiState = halbtc8723b2ant_BtRssiState(2, BT_8723B_2ANT_BT_RSSI_COEXSWITCH_THRES, 0);

	pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);

	halbtc8723b2ant_LimitedRx(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0x8);
	
	halbtc8723b2ant_FwDacSwingLvl(pBtCoexist, NORMAL_EXEC, 6);

	if(BTC_RSSI_HIGH(btRssiState))
		halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 2);
	else	
		halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 0);

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_BW, &wifiBw);

	if (BTC_WIFI_BW_LEGACY == wifiBw) //for HID at 11b/g mode
	{
		halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 7);
	}
	else  //for HID quality & wifi performance balance at 11n mode
	{
		halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 9);
	}

	halbtc8723b2ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);			
	
	if( (btRssiState == BTC_RSSI_STATE_HIGH) ||
		(btRssiState == BTC_RSSI_STATE_STAY_HIGH) )
	{
		halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 9);
	}
	else
	{
		halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 13);
	}

	// sw mechanism
	if(BTC_WIFI_BW_HT40 == wifiBw)
	{
		if( (wifiRssiState == BTC_RSSI_STATE_HIGH) ||
			(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH) )
		{
			halbtc8723b2ant_SwMechanism1(pBtCoexist,TRUE,TRUE,FALSE,FALSE);
			halbtc8723b2ant_SwMechanism2(pBtCoexist,TRUE,FALSE,FALSE,0x18);
		}
		else
		{
 			halbtc8723b2ant_SwMechanism1(pBtCoexist,TRUE,TRUE,FALSE,FALSE);
			halbtc8723b2ant_SwMechanism2(pBtCoexist,FALSE,FALSE,FALSE,0x18);
		}	
	}
	else
	{
		if( (wifiRssiState == BTC_RSSI_STATE_HIGH) ||
			(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH) )
		{
			halbtc8723b2ant_SwMechanism1(pBtCoexist,FALSE,TRUE,FALSE,FALSE);
			halbtc8723b2ant_SwMechanism2(pBtCoexist,TRUE,FALSE,FALSE,0x18);
		}
		else
		{
 			halbtc8723b2ant_SwMechanism1(pBtCoexist,FALSE,TRUE,FALSE,FALSE);
			halbtc8723b2ant_SwMechanism2(pBtCoexist,FALSE,FALSE,FALSE,0x18);
		}		
	}
}

//A2DP only / PAN(EDR) only/ A2DP+PAN(HS)
VOID
halbtc8723b2ant_ActionA2dp(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	u1Byte		wifiRssiState, wifiRssiState1, btRssiState;
	u4Byte		wifiBw;
	u1Byte		apNum=0;

	wifiRssiState = halbtc8723b2ant_WifiRssiState(pBtCoexist, 0, 2, 15, 0);
	wifiRssiState1 = halbtc8723b2ant_WifiRssiState(pBtCoexist, 1, 2, BT_8723B_2ANT_WIFI_RSSI_COEXSWITCH_THRES, 0);
	btRssiState = halbtc8723b2ant_BtRssiState(2, BT_8723B_2ANT_BT_RSSI_COEXSWITCH_THRES, 0);

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U1_AP_NUM, &apNum);

	// define the office environment
	if( (apNum >= 10) && BTC_RSSI_HIGH(wifiRssiState1) && BTC_RSSI_HIGH(btRssiState))
	{
		//DbgPrint(" AP#>10(%d)\n", apNum);
		halbtc8723b2ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);			
		
		pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);	
		halbtc8723b2ant_LimitedRx(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0x8);
		halbtc8723b2ant_FwDacSwingLvl(pBtCoexist, NORMAL_EXEC, 6);	
		halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 2);			
					
		halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 0);

		halbtc8723b2ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);		
		halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, FALSE, 1);

		// sw mechanism
		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_BW, &wifiBw);
		if(BTC_WIFI_BW_HT40 == wifiBw)
		{
			halbtc8723b2ant_SwMechanism1(pBtCoexist,TRUE,FALSE,FALSE,FALSE);
			halbtc8723b2ant_SwMechanism2(pBtCoexist,TRUE,FALSE,TRUE,0x18);
		}
		else
		{
			halbtc8723b2ant_SwMechanism1(pBtCoexist,FALSE,FALSE,FALSE,FALSE);
			halbtc8723b2ant_SwMechanism2(pBtCoexist,TRUE,FALSE,TRUE,0x18);		
		}
		return;
		
	}

	pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);
	halbtc8723b2ant_LimitedRx(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0x8);

	halbtc8723b2ant_FwDacSwingLvl(pBtCoexist, NORMAL_EXEC, 6);

	if(BTC_RSSI_HIGH(btRssiState))
		halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 2);
	else	
		halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 0);

		
	if (BTC_RSSI_HIGH(wifiRssiState1) && BTC_RSSI_HIGH(btRssiState))
	{
	halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 7);
		halbtc8723b2ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);		
	}
	else
	{
		halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 13);
		halbtc8723b2ant_PowerSaveState(pBtCoexist, BTC_PS_LPS_ON, 0x50, 0x4);		
	}
	

	if( (btRssiState == BTC_RSSI_STATE_HIGH) ||
		(btRssiState == BTC_RSSI_STATE_STAY_HIGH) )
	{
		halbtc8723b2ant_TdmaDurationAdjust(pBtCoexist, FALSE, FALSE, 1);
	}
	else
	{
		halbtc8723b2ant_TdmaDurationAdjust(pBtCoexist, FALSE, TRUE, 1);
	}

	// sw mechanism
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_BW, &wifiBw);
	if(BTC_WIFI_BW_HT40 == wifiBw)
	{
		if( (wifiRssiState == BTC_RSSI_STATE_HIGH) ||
			(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH) )
		{
 			halbtc8723b2ant_SwMechanism1(pBtCoexist,TRUE,FALSE,FALSE,FALSE);
			halbtc8723b2ant_SwMechanism2(pBtCoexist,TRUE,FALSE,FALSE,0x18);
		}
		else
		{
			halbtc8723b2ant_SwMechanism1(pBtCoexist,TRUE,FALSE,FALSE,FALSE);
			halbtc8723b2ant_SwMechanism2(pBtCoexist,FALSE,FALSE,FALSE,0x18);
		}
	}
	else
	{
		if( (wifiRssiState == BTC_RSSI_STATE_HIGH) ||
			(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH) )
		{
			halbtc8723b2ant_SwMechanism1(pBtCoexist,FALSE,FALSE,FALSE,FALSE);
			halbtc8723b2ant_SwMechanism2(pBtCoexist,TRUE,FALSE,FALSE,0x18);
		}
		else
		{
			halbtc8723b2ant_SwMechanism1(pBtCoexist,FALSE,FALSE,FALSE,FALSE);
			halbtc8723b2ant_SwMechanism2(pBtCoexist,FALSE,FALSE,FALSE,0x18);
		}		
	}
}

VOID
halbtc8723b2ant_ActionA2dpPanHs(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	u1Byte		wifiRssiState, wifiRssiState1, btRssiState;
	u4Byte		wifiBw;

	wifiRssiState = halbtc8723b2ant_WifiRssiState(pBtCoexist, 0, 2, 15, 0);
	wifiRssiState1 = halbtc8723b2ant_WifiRssiState(pBtCoexist, 1, 2, BT_8723B_2ANT_WIFI_RSSI_COEXSWITCH_THRES, 0);	
	btRssiState = halbtc8723b2ant_BtRssiState(2, BT_8723B_2ANT_BT_RSSI_COEXSWITCH_THRES, 0);

	pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);

	halbtc8723b2ant_LimitedRx(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0x8);
	
	halbtc8723b2ant_FwDacSwingLvl(pBtCoexist, NORMAL_EXEC, 6);

	if(BTC_RSSI_HIGH(btRssiState))
		halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 2);
	else	
		halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 0);

	if (BTC_RSSI_HIGH(wifiRssiState1) && BTC_RSSI_HIGH(btRssiState))
	{
	halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 7);
		halbtc8723b2ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);		
	}
	else
	{
		halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 13);
		halbtc8723b2ant_PowerSaveState(pBtCoexist, BTC_PS_LPS_ON, 0x50, 0x4);		
	}

	halbtc8723b2ant_TdmaDurationAdjust(pBtCoexist, FALSE, TRUE, 2);

	// sw mechanism
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_BW, &wifiBw);
	if(BTC_WIFI_BW_HT40 == wifiBw)
	{
		if( (wifiRssiState == BTC_RSSI_STATE_HIGH) ||
			(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH) )
		{
 			halbtc8723b2ant_SwMechanism1(pBtCoexist,TRUE,FALSE,FALSE,FALSE);
			halbtc8723b2ant_SwMechanism2(pBtCoexist,TRUE,FALSE,FALSE,0x18);
		}
		else
		{
			halbtc8723b2ant_SwMechanism1(pBtCoexist,TRUE,FALSE,FALSE,FALSE);
			halbtc8723b2ant_SwMechanism2(pBtCoexist,FALSE,FALSE,FALSE,0x18);
		}
	}
	else
	{
		if( (wifiRssiState == BTC_RSSI_STATE_HIGH) ||
			(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH) )
		{
			halbtc8723b2ant_SwMechanism1(pBtCoexist,FALSE,FALSE,FALSE,FALSE);
			halbtc8723b2ant_SwMechanism2(pBtCoexist,TRUE,FALSE,FALSE,0x18);
		}
		else
		{
			halbtc8723b2ant_SwMechanism1(pBtCoexist,FALSE,FALSE,FALSE,FALSE);
			halbtc8723b2ant_SwMechanism2(pBtCoexist,FALSE,FALSE,FALSE,0x18);
		}		
	}
}

VOID
halbtc8723b2ant_ActionPanEdr(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	u1Byte		wifiRssiState,wifiRssiState1, btRssiState;
	u4Byte		wifiBw;

	wifiRssiState = halbtc8723b2ant_WifiRssiState(pBtCoexist, 0, 2, 15, 0);
	wifiRssiState1 = halbtc8723b2ant_WifiRssiState(pBtCoexist, 1, 2, BT_8723B_2ANT_WIFI_RSSI_COEXSWITCH_THRES, 0);	
	btRssiState = halbtc8723b2ant_BtRssiState(2, BT_8723B_2ANT_BT_RSSI_COEXSWITCH_THRES, 0);

	pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);

	halbtc8723b2ant_LimitedRx(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0x8);

	halbtc8723b2ant_FwDacSwingLvl(pBtCoexist, NORMAL_EXEC, 6);

	if(BTC_RSSI_HIGH(btRssiState))
		halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 2);
	else	
		halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 0);

	if (BTC_RSSI_HIGH(wifiRssiState1) && BTC_RSSI_HIGH(btRssiState))
	{
	halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 10);
		halbtc8723b2ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);		
	}
	else
	{
		halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 13);
		halbtc8723b2ant_PowerSaveState(pBtCoexist, BTC_PS_LPS_ON, 0x50, 0x4);		
	}

	if( (btRssiState == BTC_RSSI_STATE_HIGH) ||
		(btRssiState == BTC_RSSI_STATE_STAY_HIGH) )
	{
		halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 1);
	}
	else
	{
		halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 5);
	}
	
	// sw mechanism
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_BW, &wifiBw);
	if(BTC_WIFI_BW_HT40 == wifiBw)
	{
		if( (wifiRssiState == BTC_RSSI_STATE_HIGH) ||
			(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH) )
		{
			halbtc8723b2ant_SwMechanism1(pBtCoexist,TRUE,FALSE,FALSE,FALSE);
			halbtc8723b2ant_SwMechanism2(pBtCoexist,TRUE,FALSE,FALSE,0x18);
		}
		else
		{
			halbtc8723b2ant_SwMechanism1(pBtCoexist,TRUE,FALSE,FALSE,FALSE);
			halbtc8723b2ant_SwMechanism2(pBtCoexist,FALSE,FALSE,FALSE,0x18);
		}
	}
	else
	{
		if( (wifiRssiState == BTC_RSSI_STATE_HIGH) ||
			(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH) )
		{
			halbtc8723b2ant_SwMechanism1(pBtCoexist,FALSE,FALSE,FALSE,FALSE);
			halbtc8723b2ant_SwMechanism2(pBtCoexist,TRUE,FALSE,FALSE,0x18);
		}
		else
		{
			halbtc8723b2ant_SwMechanism1(pBtCoexist,FALSE,FALSE,FALSE,FALSE);
			halbtc8723b2ant_SwMechanism2(pBtCoexist,FALSE,FALSE,FALSE,0x18);
		}
	}
}


//PAN(HS) only
VOID
halbtc8723b2ant_ActionPanHs(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	u1Byte		wifiRssiState, wifiRssiState1, btRssiState;
	u4Byte		wifiBw;

	wifiRssiState = halbtc8723b2ant_WifiRssiState(pBtCoexist, 0, 2, 15, 0);
	wifiRssiState1 = halbtc8723b2ant_WifiRssiState(pBtCoexist, 1, 2, BT_8723B_2ANT_WIFI_RSSI_COEXSWITCH_THRES, 0);	
	btRssiState = halbtc8723b2ant_BtRssiState(2, BT_8723B_2ANT_BT_RSSI_COEXSWITCH_THRES, 0);

	pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);

	halbtc8723b2ant_LimitedRx(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0x8);

	halbtc8723b2ant_FwDacSwingLvl(pBtCoexist, NORMAL_EXEC, 6);

	if(BTC_RSSI_HIGH(btRssiState))
		halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 2);
	else
		halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 0);
	
	halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 7);
	
	halbtc8723b2ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);		
	halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, FALSE, 1);

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_BW, &wifiBw);
	if(BTC_WIFI_BW_HT40 == wifiBw)
	{
		if( (wifiRssiState == BTC_RSSI_STATE_HIGH) ||
			(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH) )
		{
			halbtc8723b2ant_SwMechanism1(pBtCoexist,TRUE,FALSE,FALSE,FALSE);
			halbtc8723b2ant_SwMechanism2(pBtCoexist,TRUE,FALSE,FALSE,0x18);
		}
		else
		{
			halbtc8723b2ant_SwMechanism1(pBtCoexist,TRUE,FALSE,FALSE,FALSE);
			halbtc8723b2ant_SwMechanism2(pBtCoexist,FALSE,FALSE,FALSE,0x18);
		}
	}
	else
	{
		if( (wifiRssiState == BTC_RSSI_STATE_HIGH) ||
			(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH) )
		{
			halbtc8723b2ant_SwMechanism1(pBtCoexist,FALSE,FALSE,FALSE,FALSE);
			halbtc8723b2ant_SwMechanism2(pBtCoexist,TRUE,FALSE,FALSE,0x18);
		}
		else
		{
			halbtc8723b2ant_SwMechanism1(pBtCoexist,FALSE,FALSE,FALSE,FALSE);
			halbtc8723b2ant_SwMechanism2(pBtCoexist,FALSE,FALSE,FALSE,0x18);
		}
	}
}

//PAN(EDR)+A2DP
VOID
halbtc8723b2ant_ActionPanEdrA2dp(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	u1Byte		wifiRssiState, wifiRssiState1, btRssiState;
	u4Byte		wifiBw;

	wifiRssiState = halbtc8723b2ant_WifiRssiState(pBtCoexist, 0, 2, 15, 0);
	wifiRssiState1 = halbtc8723b2ant_WifiRssiState(pBtCoexist, 1, 2, BT_8723B_2ANT_WIFI_RSSI_COEXSWITCH_THRES, 0);	
	btRssiState = halbtc8723b2ant_BtRssiState(2, BT_8723B_2ANT_BT_RSSI_COEXSWITCH_THRES, 0);

	pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);

	halbtc8723b2ant_LimitedRx(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0x8);

	halbtc8723b2ant_FwDacSwingLvl(pBtCoexist, NORMAL_EXEC, 6);

	if(BTC_RSSI_HIGH(btRssiState))
		halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 2);
	else	
		halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 0);

	if (BTC_RSSI_HIGH(wifiRssiState1) && BTC_RSSI_HIGH(btRssiState))		
		halbtc8723b2ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);		
	else
		halbtc8723b2ant_PowerSaveState(pBtCoexist, BTC_PS_LPS_ON, 0x50, 0x4);

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_BW, &wifiBw);

	if( (btRssiState == BTC_RSSI_STATE_HIGH) ||
		(btRssiState == BTC_RSSI_STATE_STAY_HIGH) )
	{
		halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 12);
		
		if(BTC_WIFI_BW_HT40 == wifiBw)
			halbtc8723b2ant_TdmaDurationAdjust(pBtCoexist, FALSE, TRUE, 3);
		else
			halbtc8723b2ant_TdmaDurationAdjust(pBtCoexist, FALSE, FALSE, 3);
	}
	else
	{
		halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 13);
		halbtc8723b2ant_TdmaDurationAdjust(pBtCoexist, FALSE, TRUE, 3);
	}
	
	// sw mechanism	
	if(BTC_WIFI_BW_HT40 == wifiBw)
	{
		if( (wifiRssiState == BTC_RSSI_STATE_HIGH) ||
			(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH) )
		{
			halbtc8723b2ant_SwMechanism1(pBtCoexist,TRUE,FALSE,FALSE,FALSE);
			halbtc8723b2ant_SwMechanism2(pBtCoexist,TRUE,FALSE,FALSE,0x18);
		}
		else
		{
			halbtc8723b2ant_SwMechanism1(pBtCoexist,TRUE,FALSE,FALSE,FALSE);
			halbtc8723b2ant_SwMechanism2(pBtCoexist,FALSE,FALSE,FALSE,0x18);
		}
	}
	else
	{
		if( (wifiRssiState == BTC_RSSI_STATE_HIGH) ||
			(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH) )
		{
			halbtc8723b2ant_SwMechanism1(pBtCoexist,FALSE,FALSE,FALSE,FALSE);
			halbtc8723b2ant_SwMechanism2(pBtCoexist,TRUE,FALSE,FALSE,0x18);
		}
		else
		{
			halbtc8723b2ant_SwMechanism1(pBtCoexist,FALSE,FALSE,FALSE,FALSE);
			halbtc8723b2ant_SwMechanism2(pBtCoexist,FALSE,FALSE,FALSE,0x18);
		}
	}
}

VOID
halbtc8723b2ant_ActionPanEdrHid(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	u1Byte		wifiRssiState, wifiRssiState1, btRssiState;
	u4Byte		wifiBw;

	wifiRssiState = halbtc8723b2ant_WifiRssiState(pBtCoexist, 0, 2, 15, 0);
	wifiRssiState1 = halbtc8723b2ant_WifiRssiState(pBtCoexist, 1, 2, BT_8723B_2ANT_WIFI_RSSI_COEXSWITCH_THRES, 0);	
	btRssiState = halbtc8723b2ant_BtRssiState(2, BT_8723B_2ANT_BT_RSSI_COEXSWITCH_THRES, 0);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_BW, &wifiBw);

	halbtc8723b2ant_LimitedRx(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0x8);

	if(BTC_RSSI_HIGH(btRssiState))
		halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 2);
	else	
		halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 0);

	if (BTC_RSSI_HIGH(wifiRssiState1) && BTC_RSSI_HIGH(btRssiState))	
	{
		halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 7);
		halbtc8723b2ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);		
	}
	else
	{
		halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 14);
		halbtc8723b2ant_PowerSaveState(pBtCoexist, BTC_PS_LPS_ON, 0x50, 0x4);
	}

	if( (btRssiState == BTC_RSSI_STATE_HIGH) ||
		(btRssiState == BTC_RSSI_STATE_STAY_HIGH) )
	{
		if(BTC_WIFI_BW_HT40 == wifiBw)
		{
			halbtc8723b2ant_FwDacSwingLvl(pBtCoexist, NORMAL_EXEC, 3);
			//halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 11);
			pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1, 0xfffff, 0x780);
		}
		else
		{
			halbtc8723b2ant_FwDacSwingLvl(pBtCoexist, NORMAL_EXEC, 6);
			//halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 7);
			pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);
		}
		halbtc8723b2ant_TdmaDurationAdjust(pBtCoexist, TRUE, FALSE, 2);
	}
	else
	{
		halbtc8723b2ant_FwDacSwingLvl(pBtCoexist, NORMAL_EXEC, 6);
		//halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 14);
		pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);
		halbtc8723b2ant_TdmaDurationAdjust(pBtCoexist, TRUE, TRUE, 2);
	}
	
	// sw mechanism
	if(BTC_WIFI_BW_HT40 == wifiBw)
	{
		if( (wifiRssiState == BTC_RSSI_STATE_HIGH) ||
			(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH) )
		{
			halbtc8723b2ant_SwMechanism1(pBtCoexist,TRUE,TRUE,FALSE,FALSE);
			halbtc8723b2ant_SwMechanism2(pBtCoexist,TRUE,FALSE,FALSE,0x18);
		}
		else
		{
			halbtc8723b2ant_SwMechanism1(pBtCoexist,TRUE,TRUE,FALSE,FALSE);
			halbtc8723b2ant_SwMechanism2(pBtCoexist,FALSE,FALSE,FALSE,0x18);
		}
	}
	else
	{
		if( (wifiRssiState == BTC_RSSI_STATE_HIGH) ||
			(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH) )
		{
			halbtc8723b2ant_SwMechanism1(pBtCoexist,FALSE,TRUE,FALSE,FALSE);
			halbtc8723b2ant_SwMechanism2(pBtCoexist,TRUE,FALSE,FALSE,0x18);
		}
		else
		{
			halbtc8723b2ant_SwMechanism1(pBtCoexist,FALSE,TRUE,FALSE,FALSE);
			halbtc8723b2ant_SwMechanism2(pBtCoexist,FALSE,FALSE,FALSE,0x18);
		}
	}
}

// HID+A2DP+PAN(EDR)
VOID
halbtc8723b2ant_ActionHidA2dpPanEdr(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	u1Byte		wifiRssiState,wifiRssiState1,  btRssiState;
	u4Byte		wifiBw;

	wifiRssiState = halbtc8723b2ant_WifiRssiState(pBtCoexist, 0, 2, 15, 0);
	wifiRssiState1 = halbtc8723b2ant_WifiRssiState(pBtCoexist, 1, 2, BT_8723B_2ANT_WIFI_RSSI_COEXSWITCH_THRES, 0);	
	btRssiState = halbtc8723b2ant_BtRssiState(2, BT_8723B_2ANT_BT_RSSI_COEXSWITCH_THRES, 0);

	pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);

	halbtc8723b2ant_LimitedRx(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0x8);

	halbtc8723b2ant_FwDacSwingLvl(pBtCoexist, NORMAL_EXEC, 6);

	if(BTC_RSSI_HIGH(btRssiState))
		halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 2);
	else	
		halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 0);

	if (BTC_RSSI_HIGH(wifiRssiState1) && BTC_RSSI_HIGH(btRssiState))
	{
		halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 7);
		halbtc8723b2ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);		
	}
	else
	{
		halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 14);
		halbtc8723b2ant_PowerSaveState(pBtCoexist, BTC_PS_LPS_ON, 0x50, 0x4);		
	}

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_BW, &wifiBw);

	if( (btRssiState == BTC_RSSI_STATE_HIGH) ||
		(btRssiState == BTC_RSSI_STATE_STAY_HIGH) )
	{
		if(BTC_WIFI_BW_HT40 == wifiBw)
			halbtc8723b2ant_TdmaDurationAdjust(pBtCoexist, TRUE, TRUE, 3);
		else
			halbtc8723b2ant_TdmaDurationAdjust(pBtCoexist, TRUE, FALSE, 3);
	}
	else
	{
		halbtc8723b2ant_TdmaDurationAdjust(pBtCoexist, TRUE, TRUE, 3);
	}

	// sw mechanism
	if(BTC_WIFI_BW_HT40 == wifiBw)
	{
		if( (wifiRssiState == BTC_RSSI_STATE_HIGH) ||
			(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH) )
		{
			halbtc8723b2ant_SwMechanism1(pBtCoexist,TRUE,TRUE,FALSE,FALSE);
			halbtc8723b2ant_SwMechanism2(pBtCoexist,TRUE,FALSE,FALSE,0x18);
		}
		else
		{
			halbtc8723b2ant_SwMechanism1(pBtCoexist,TRUE,TRUE,FALSE,FALSE);
			halbtc8723b2ant_SwMechanism2(pBtCoexist,FALSE,FALSE,FALSE,0x18);
		}
	}
	else
	{
		if( (wifiRssiState == BTC_RSSI_STATE_HIGH) ||
			(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH) )
		{
			halbtc8723b2ant_SwMechanism1(pBtCoexist,FALSE,TRUE,FALSE,FALSE);
			halbtc8723b2ant_SwMechanism2(pBtCoexist,TRUE,FALSE,FALSE,0x18);
		}
		else
		{
			halbtc8723b2ant_SwMechanism1(pBtCoexist,FALSE,TRUE,FALSE,FALSE);
			halbtc8723b2ant_SwMechanism2(pBtCoexist,FALSE,FALSE,FALSE,0x18);
		}
	}
}

VOID
halbtc8723b2ant_ActionHidA2dp(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	u1Byte		wifiRssiState, wifiRssiState1, btRssiState;
	u4Byte		wifiBw;
	u1Byte		apNum=0;

	wifiRssiState = halbtc8723b2ant_WifiRssiState(pBtCoexist, 0, 2, 15, 0);
	//btRssiState = halbtc8723b2ant_BtRssiState(2, 29, 0);
	wifiRssiState1 = halbtc8723b2ant_WifiRssiState(pBtCoexist, 1, 2, BT_8723B_2ANT_WIFI_RSSI_COEXSWITCH_THRES, 0);	
	btRssiState = halbtc8723b2ant_BtRssiState(3, BT_8723B_2ANT_BT_RSSI_COEXSWITCH_THRES, 37);

	pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);

	halbtc8723b2ant_LimitedRx(pBtCoexist, NORMAL_EXEC, FALSE, TRUE, 0x5);

	halbtc8723b2ant_FwDacSwingLvl(pBtCoexist, NORMAL_EXEC, 6);

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_BW, &wifiBw);
	if(BTC_WIFI_BW_LEGACY == wifiBw)
	{
		if(BTC_RSSI_HIGH(btRssiState))
			halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 2);
		else if(BTC_RSSI_MEDIUM(btRssiState))
			halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 2);
	else	
			halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 0);
	}
	else	
	{	// only 802.11N mode we have to dec bt power to 4 degree
		if(BTC_RSSI_HIGH(btRssiState))
		{
			pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U1_AP_NUM, &apNum);
			// need to check ap Number of Not
			if(apNum < 10)
				halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 4);
			else
				halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 2);
		}
		else if(BTC_RSSI_MEDIUM(btRssiState))
			halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 2);
		else	
			halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 0);
	}

	if (BTC_RSSI_HIGH(wifiRssiState1) && BTC_RSSI_HIGH(btRssiState))
	{
	halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 7);
		halbtc8723b2ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);		
	}
	else
	{
		halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 14);
		halbtc8723b2ant_PowerSaveState(pBtCoexist, BTC_PS_LPS_ON, 0x50, 0x4);		
	}

	if( (btRssiState == BTC_RSSI_STATE_HIGH) ||
		(btRssiState == BTC_RSSI_STATE_STAY_HIGH) )
	{
		halbtc8723b2ant_TdmaDurationAdjust(pBtCoexist, TRUE, FALSE, 3);
	}
	else
	{
		halbtc8723b2ant_TdmaDurationAdjust(pBtCoexist, TRUE, TRUE, 3);
	}
	
	// sw mechanism
	if(BTC_WIFI_BW_HT40 == wifiBw)
	{
		if( (wifiRssiState == BTC_RSSI_STATE_HIGH) ||
			(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH) )
		{
			halbtc8723b2ant_SwMechanism1(pBtCoexist,TRUE,TRUE,FALSE,FALSE);
			halbtc8723b2ant_SwMechanism2(pBtCoexist,TRUE,FALSE,FALSE,0x18);
		}
		else
		{
			halbtc8723b2ant_SwMechanism1(pBtCoexist,TRUE,TRUE,FALSE,FALSE);
			halbtc8723b2ant_SwMechanism2(pBtCoexist,FALSE,FALSE,FALSE,0x18);
		}
	}
	else
	{
		if( (wifiRssiState == BTC_RSSI_STATE_HIGH) ||
			(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH) )
		{
			halbtc8723b2ant_SwMechanism1(pBtCoexist,FALSE,TRUE,FALSE,FALSE);
			halbtc8723b2ant_SwMechanism2(pBtCoexist,TRUE,FALSE,FALSE,0x18);
		}
		else
		{
			halbtc8723b2ant_SwMechanism1(pBtCoexist,FALSE,TRUE,FALSE,FALSE);
			halbtc8723b2ant_SwMechanism2(pBtCoexist,FALSE,FALSE,FALSE,0x18);
		}
	}
}

VOID
halbtc8723b2ant_ActionBtWhckTest(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 0);

	// sw all off
	halbtc8723b2ant_SwMechanism1(pBtCoexist,FALSE,FALSE,FALSE,FALSE);
	halbtc8723b2ant_SwMechanism2(pBtCoexist,FALSE,FALSE,FALSE,0x18);

	halbtc8723b2ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);
	
	halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, FALSE, 1);
	halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 0);
}

VOID
halbtc8723b2ant_ActionWifiMultiPort(
	IN	PBTC_COEXIST		pBtCoexist
	)
{		
	halbtc8723b2ant_FwDacSwingLvl(pBtCoexist, NORMAL_EXEC, 6);
	halbtc8723b2ant_DecBtPwr(pBtCoexist, NORMAL_EXEC, 0);

	// sw all off
	halbtc8723b2ant_SwMechanism1(pBtCoexist,FALSE,FALSE,FALSE,FALSE);
	halbtc8723b2ant_SwMechanism2(pBtCoexist,FALSE,FALSE,FALSE,0x18);

	// hw all off
	//pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);
	halbtc8723b2ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 0);

	halbtc8723b2ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);		
	halbtc8723b2ant_PsTdma(pBtCoexist, NORMAL_EXEC, FALSE, 1);
}

VOID
halbtc8723b2ant_RunCoexistMechanism(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	BOOLEAN				bWifiUnder5G=FALSE, bBtHsOn=FALSE;
	u1Byte				btInfoOriginal=0, btRetryCnt=0;
	u1Byte				algorithm=0;
	u4Byte				numOfWifiLink=0;
	u4Byte				wifiLinkStatus=0;
	PBTC_BT_LINK_INFO pBtLinkInfo=&pBtCoexist->btLinkInfo;
	BOOLEAN				bMiracastPlusBt=FALSE;
	BOOLEAN				bScan=FALSE, bLink=FALSE, bRoam=FALSE;

	RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], RunCoexistMechanism()===>\n"));

	if(pBtCoexist->bManualControl)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], RunCoexistMechanism(), return for Manual CTRL <===\n"));
		return;
	}

	if(pCoexSta->bUnderIps)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], wifi is under IPS !!!\n"));
		return;
	}

	if(pCoexSta->bBtWhckTest)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BT is under WHCK TEST!!!\n"));
		halbtc8723b2ant_ActionBtWhckTest(pBtCoexist);
		return;
	}

	algorithm = halbtc8723b2ant_ActionAlgorithm(pBtCoexist);
	if(pCoexSta->bC2hBtInquiryPage && (BT_8723B_2ANT_COEX_ALGO_PANHS!=algorithm))
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BT is under inquiry/page scan !!\n"));
		halbtc8723b2ant_ActionBtInquiry(pBtCoexist);
		return;
	}
	else
	{
		/*
		if(pCoexDm->bNeedRecover0x948)
		{
			pCoexDm->bNeedRecover0x948 = FALSE;
			pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x948, pCoexDm->backup0x948);
		}
		*/
	}

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_SCAN, &bScan);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_LINK, &bLink);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_ROAM, &bRoam);

	if(bScan || bLink || bRoam)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], WiFi is under Link Process !!\n"));
		halbtc8723b2ant_ActionWiFiLinkProcess(pBtCoexist);
		return;
	}

	//for P2P

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_LINK_STATUS, &wifiLinkStatus);
	numOfWifiLink = wifiLinkStatus>>16;
	
	if((numOfWifiLink>=2) || (wifiLinkStatus&WIFI_P2P_GO_CONNECTED))
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("############# [BTCoex],  Multi-Port numOfWifiLink = %d, wifiLinkStatus = 0x%x\n", numOfWifiLink,wifiLinkStatus) );

		if(pBtLinkInfo->bBtLinkExist)
		{
			bMiracastPlusBt = TRUE;
		}
		else
		{
			bMiracastPlusBt = FALSE;
		}
		
		pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_BL_MIRACAST_PLUS_BT, &bMiracastPlusBt);
		halbtc8723b2ant_ActionWifiMultiPort(pBtCoexist);
		
		return;
	}
	else
	{
		bMiracastPlusBt = FALSE;
		pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_BL_MIRACAST_PLUS_BT, &bMiracastPlusBt);
	}

	pCoexDm->curAlgorithm = algorithm;
	RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Algorithm = %d \n", pCoexDm->curAlgorithm));

	if(halbtc8723b2ant_IsCommonAction(pBtCoexist))
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Action 2-Ant common.\n"));
		pCoexDm->bAutoTdmaAdjust = FALSE;
	}
	else
	{
		if(pCoexDm->curAlgorithm != pCoexDm->preAlgorithm)
		{
			RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], preAlgorithm=%d, curAlgorithm=%d\n", 
				pCoexDm->preAlgorithm, pCoexDm->curAlgorithm));
			pCoexDm->bAutoTdmaAdjust = FALSE;
		}
		switch(pCoexDm->curAlgorithm)
		{
			case BT_8723B_2ANT_COEX_ALGO_SCO:
				RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Action 2-Ant, algorithm = SCO.\n"));
				halbtc8723b2ant_ActionSco(pBtCoexist);
				break;
			case BT_8723B_2ANT_COEX_ALGO_HID:
				RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Action 2-Ant, algorithm = HID.\n"));
				halbtc8723b2ant_ActionHid(pBtCoexist);
				break;
			case BT_8723B_2ANT_COEX_ALGO_A2DP:
				RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Action 2-Ant, algorithm = A2DP.\n"));
				halbtc8723b2ant_ActionA2dp(pBtCoexist);
				break;
			case BT_8723B_2ANT_COEX_ALGO_A2DP_PANHS:
				RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Action 2-Ant, algorithm = A2DP+PAN(HS).\n"));
				halbtc8723b2ant_ActionA2dpPanHs(pBtCoexist);
				break;
			case BT_8723B_2ANT_COEX_ALGO_PANEDR:
				RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Action 2-Ant, algorithm = PAN(EDR).\n"));
				halbtc8723b2ant_ActionPanEdr(pBtCoexist);
				break;
			case BT_8723B_2ANT_COEX_ALGO_PANHS:
				RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Action 2-Ant, algorithm = HS mode.\n"));
				halbtc8723b2ant_ActionPanHs(pBtCoexist);
				break;
			case BT_8723B_2ANT_COEX_ALGO_PANEDR_A2DP:
				RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Action 2-Ant, algorithm = PAN+A2DP.\n"));
				halbtc8723b2ant_ActionPanEdrA2dp(pBtCoexist);
				break;
			case BT_8723B_2ANT_COEX_ALGO_PANEDR_HID:
				RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Action 2-Ant, algorithm = PAN(EDR)+HID.\n"));
				halbtc8723b2ant_ActionPanEdrHid(pBtCoexist);
				break;
			case BT_8723B_2ANT_COEX_ALGO_HID_A2DP_PANEDR:
				RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Action 2-Ant, algorithm = HID+A2DP+PAN.\n"));
				halbtc8723b2ant_ActionHidA2dpPanEdr(pBtCoexist);
				break;
			case BT_8723B_2ANT_COEX_ALGO_HID_A2DP:
				RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Action 2-Ant, algorithm = HID+A2DP.\n"));
				halbtc8723b2ant_ActionHidA2dp(pBtCoexist);
				break;
			default:
				RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Action 2-Ant, algorithm = coexist All Off!!\n"));
				halbtc8723b2ant_CoexAllOff(pBtCoexist);
				break;
		}
		pCoexDm->preAlgorithm = pCoexDm->curAlgorithm;
	}
}

VOID
halbtc8723b2ant_WifiOffHwCfg(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	BOOLEAN	bIsInMpMode = FALSE;
	u1Byte H2C_Parameter[2] ={0};
	u4Byte fwVer=0;

	// set wlan_act to low
	pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x76e, 0x4);

	pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1, 0xfffff, 0x780); //WiFi goto standby while GNT_BT 0-->1
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_FW_VER, &fwVer);
	if(fwVer >= 0x180000)
	{
	/* Use H2C to set GNT_BT to HIGH */
	H2C_Parameter[0] = 1;
	pBtCoexist->fBtcFillH2c(pBtCoexist, 0x6E, 1, H2C_Parameter);
	}
	else
	{
		pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x765, 0x18);
	}
	
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_IS_IN_MP_MODE, &bIsInMpMode);
	if(!bIsInMpMode)
		pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0x67, 0x20, 0x0); //BT select s0/s1 is controlled by BT
	else
		pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0x67, 0x20, 0x1); //BT select s0/s1 is controlled by WiFi
}

VOID
halbtc8723b2ant_InitHwConfig(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN				bBackUp
	)
{
	PBTC_BOARD_INFO		pBoardInfo=&pBtCoexist->boardInfo;
	u4Byte	u4Tmp=0, fwVer;
	u2Byte				u2Tmp=0;
	u1Byte	u1Tmp=0;
	u1Byte				H2C_Parameter[2] ={0};
		

	RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], 2Ant Init HW Config!!\n"));

	//0xf0[15:12] --> Chip Cut information 
	pCoexSta->nCutVersion = (pBtCoexist->fBtcRead1Byte(pBtCoexist, 0xf1) & 0xf0) >> 4;

	// backup rf 0x1e value
	pCoexDm->btRf0x1eBackup = 
		pBtCoexist->fBtcGetRfReg(pBtCoexist, BTC_RF_A, 0x1e, 0xfffff);	

	// 0x790[5:0]=0x5
	u1Tmp = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x790);
	u1Tmp &= 0xc0;
	u1Tmp |= 0x5;
	pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x790, u1Tmp);

	//Antenna config	
	halbtc8723b2ant_SetAntPath(pBtCoexist, BTC_ANT_WIFI_AT_MAIN, TRUE, FALSE);
	pCoexSta->disVerInfoCnt = 0;

	// PTA parameter
	halbtc8723b2ant_CoexTableWithType(pBtCoexist, FORCE_EXEC, 0);
	
	// Enable counter statistics
	pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x76e, 0x4); //0x76e[3] =1, WLAN_Act control by PTA
	pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x778, 0x3);
	pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0x40, 0x20, 0x1);
}

//============================================================
// work around function start with wa_halbtc8723b2ant_
//============================================================
//============================================================
// extern function start with EXhalbtc8723b2ant_
//============================================================
VOID
EXhalbtc8723b2ant_PowerOnSetting(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	PBTC_BOARD_INFO 	pBoardInfo=&pBtCoexist->boardInfo;
	u2Byte u2Tmp=0x0;

	pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x67, 0x20);

	// enable BB, REG_SYS_FUNC_EN such that we can write 0x948 correctly.
	u2Tmp = pBtCoexist->fBtcRead2Byte(pBtCoexist, 0x2);
	pBtCoexist->fBtcWrite2Byte(pBtCoexist, 0x2, u2Tmp|BIT0|BIT1);

	pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x948, 0x0);

	if(pBtCoexist->chipInterface == BTC_INTF_USB)
	{
		// fixed at S0 for USB interface
		pBoardInfo->btdmAntPos = BTC_ANTENNA_AT_AUX_PORT;
	}
	else
	{
		// for PCIE and SDIO interface, we check efuse 0xc3[6]
		if(pBoardInfo->singleAntPath == 0)
		{
			// set to S1
			pBoardInfo->btdmAntPos = BTC_ANTENNA_AT_MAIN_PORT;
		}
		else if(pBoardInfo->singleAntPath == 1)
		{
			// set to S0
			pBoardInfo->btdmAntPos = BTC_ANTENNA_AT_AUX_PORT;
		}
	}
}

VOID
EXhalbtc8723b2ant_PreLoadFirmware(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	PBTC_BOARD_INFO 	pBoardInfo=&pBtCoexist->boardInfo;
	u1Byte u1Tmp=0x4; /* Set BIT2 by default since it's 2ant case */

	// 
	// S0 or S1 setting and Local register setting(By the setting fw can get ant number, S0/S1, ... info)
	// Local setting bit define
	//	BIT0: "0" for no antenna inverse; "1" for antenna inverse 
	//	BIT1: "0" for internal switch; "1" for external switch
	//	BIT2: "0" for one antenna; "1" for two antenna
	// NOTE: here default all internal switch and 1-antenna ==> BIT1=0 and BIT2=0
	if(pBtCoexist->chipInterface == BTC_INTF_USB)
	{
		// fixed at S0 for USB interface
	 	u1Tmp |= 0x1;	// antenna inverse
		pBtCoexist->fBtcWriteLocalReg1Byte(pBtCoexist, 0xfe08, u1Tmp);
	}
	else
	{
		// for PCIE and SDIO interface, we check efuse 0xc3[6]
		if(pBoardInfo->singleAntPath == 0)
		{
		}
		else if(pBoardInfo->singleAntPath == 1)
		{
			// set to S0
			u1Tmp |= 0x1;	// antenna inverse
		}

		if(pBtCoexist->chipInterface == BTC_INTF_PCI)
		{	
			pBtCoexist->fBtcWriteLocalReg1Byte(pBtCoexist, 0x384, u1Tmp);
		}
		else if(pBtCoexist->chipInterface == BTC_INTF_SDIO)
		{
			pBtCoexist->fBtcWriteLocalReg1Byte(pBtCoexist, 0x60, u1Tmp);
		}			
	}
}

VOID
EXhalbtc8723b2ant_InitHwConfig(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN				bWifiOnly
	)
{
	halbtc8723b2ant_InitHwConfig(pBtCoexist, TRUE);
}

VOID
EXhalbtc8723b2ant_InitCoexDm(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Coex Mechanism Init!!\n"));
	
	halbtc8723b2ant_InitCoexDm(pBtCoexist);
}

VOID
EXhalbtc8723b2ant_DisplayCoexInfo(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	PBTC_BOARD_INFO		pBoardInfo=&pBtCoexist->boardInfo;
	PBTC_STACK_INFO		pStackInfo=&pBtCoexist->stackInfo;
	PBTC_BT_LINK_INFO	pBtLinkInfo=&pBtCoexist->btLinkInfo;
	pu1Byte				cliBuf=pBtCoexist->cliBuf;
	u1Byte				u1Tmp[4], i, btInfoExt, psTdmaCase=0;
	u4Byte				u4Tmp[4];
	u4Byte				faOfdm, faCck;
	u4Byte				fwVer=0, btPatchVer=0;
	static u1Byte			PopReportIn10s = 0;

	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n ============[BT Coexist info]============");
	CL_PRINTF(cliBuf);

	if(pBtCoexist->bManualControl)
	{
		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n ============[Under Manual Control]============");
		CL_PRINTF(cliBuf);
		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n ==========================================");
		CL_PRINTF(cliBuf);
	}

	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d ", "Ant PG number/ Ant mechanism:", \
		pBoardInfo->pgAntNum, pBoardInfo->btdmAntNum);
	CL_PRINTF(cliBuf);
	
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s / %d", "BT stack/ hci ext ver", \
		((pStackInfo->bProfileNotified)? "Yes":"No"), pStackInfo->hciVersion);
	CL_PRINTF(cliBuf);

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_BT_PATCH_VER, &btPatchVer);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_FW_VER, &fwVer);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d_%x/ 0x%x/ 0x%x(%d)/ %c", "Version Coex/ Fw/ Patch/ Cut", \
		GLCoexVerDate8723b2Ant, GLCoexVer8723b2Ant, fwVer, btPatchVer, btPatchVer, pCoexSta->nCutVersion+65);
	CL_PRINTF(cliBuf);

	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %02x %02x %02x ", "Wifi channel informed to BT", \
		pCoexDm->wifiChnlInfo[0], pCoexDm->wifiChnlInfo[1],
		pCoexDm->wifiChnlInfo[2]);
	CL_PRINTF(cliBuf);

	// wifi status
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "============[Wifi Status]============");
	CL_PRINTF(cliBuf);
	pBtCoexist->fBtcDispDbgMsg(pBtCoexist, BTC_DBG_DISP_WIFI_STATUS);

	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "============[BT Status]============");
	CL_PRINTF(cliBuf);

	PopReportIn10s++;
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = [%s/ %d/ %d/ %d] ", "BT [status/ rssi/ retryCnt/ popCnt]", \
		((pBtCoexist->btInfo.bBtDisabled)? ("disabled"):	((pCoexSta->bC2hBtInquiryPage)?("inquiry/page scan"):((BT_8723B_1ANT_BT_STATUS_NON_CONNECTED_IDLE == pCoexDm->btStatus)? "non-connected idle":
		(  (BT_8723B_2ANT_BT_STATUS_CONNECTED_IDLE == pCoexDm->btStatus)? "connected-idle":"busy")))),
		pCoexSta->btRssi-100, pCoexSta->btRetryCnt, pCoexSta->popEventCnt);
	CL_PRINTF(cliBuf);

	if (PopReportIn10s >= 5)
	{
		pCoexSta->popEventCnt = 0;	
		PopReportIn10s = 0;
	}
	

	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d / %d / %d / %d / %d", "SCO/HID/PAN/A2DP/NameReq/WHQL", \
		pBtLinkInfo->bScoExist, pBtLinkInfo->bHidExist, pBtLinkInfo->bPanExist, pBtLinkInfo->bA2dpExist, pCoexSta->bC2hBtRemoteNameReq, pCoexSta->bBtWhckTest );
	CL_PRINTF(cliBuf);

	if (pStackInfo->bProfileNotified)
	{
	pBtCoexist->fBtcDispDbgMsg(pBtCoexist, BTC_DBG_DISP_BT_LINK_INFO);
	}
	else
	{
		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s", "BT Role", \
		(pBtLinkInfo->bSlaveRole )? "Slave":"Master");
		CL_PRINTF(cliBuf);	
	}	

	btInfoExt = pCoexSta->btInfoExt;
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s / %d", "A2DP Rate/Bitpool", \
		(btInfoExt&BIT0)? "BR":"EDR", pCoexSta->nA2DPBitPool);
	CL_PRINTF(cliBuf);	

	for(i=0; i<BT_INFO_SRC_8723B_2ANT_MAX; i++)
	{
		if(pCoexSta->btInfoC2hCnt[i])
		{				
			CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %02x %02x %02x %02x %02x %02x %02x(%d)", GLBtInfoSrc8723b2Ant[i], \
				pCoexSta->btInfoC2h[i][0], pCoexSta->btInfoC2h[i][1],
				pCoexSta->btInfoC2h[i][2], pCoexSta->btInfoC2h[i][3],
				pCoexSta->btInfoC2h[i][4], pCoexSta->btInfoC2h[i][5],
				pCoexSta->btInfoC2h[i][6], pCoexSta->btInfoC2hCnt[i]);
			CL_PRINTF(cliBuf);
		}
	}

	// Sw mechanism	
	if(pBtCoexist->bManualControl)
	{			
		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "============[Sw mechanism] (before Manual)============");			
	}
	else
	{
		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "============[Sw mechanism]============");
	}
	
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d ", "SM1[ShRf/ LpRA/ LimDig]", \
		pCoexDm->bCurRfRxLpfShrink, pCoexDm->bCurLowPenaltyRa, pCoexDm->bLimitedDig);
	CL_PRINTF(cliBuf);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d(0x%x) ", "SM2[AgcT/ AdcB/ SwDacSwing(lvl)]", \
		pCoexDm->bCurAgcTableEn, pCoexDm->bCurAdcBackOff, pCoexDm->bCurDacSwingOn, pCoexDm->curDacSwingLvl);
	CL_PRINTF(cliBuf);

	// Fw mechanism		
	if(pBtCoexist->bManualControl)
	{
		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "============[Fw mechanism] (before Manual) ============");			
	}
	else
	{
		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "============[Fw mechanism]============");
	}

	psTdmaCase = pCoexDm->curPsTdma;

	if (pCoexDm->bIsSwitchTo1dot5Ant)
		psTdmaCase = psTdmaCase + 100;
		
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %02x %02x %02x %02x %02x case-%d (%s,%s)", "PS TDMA", \
		pCoexDm->psTdmaPara[0], pCoexDm->psTdmaPara[1],
		pCoexDm->psTdmaPara[2], pCoexDm->psTdmaPara[3],
			pCoexDm->psTdmaPara[4], psTdmaCase, 
			(pCoexDm->bCurPsTdmaOn? "On":"Off"),
			(pCoexDm->bAutoTdmaAdjust? "Adj":"Fix") );
	CL_PRINTF(cliBuf);

	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d", "Coex Table Type", \
			pCoexSta->nCoexTableType);
		CL_PRINTF(cliBuf);

	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d ", "DecBtPwr/ IgnWlanAct", \
		pCoexDm->curBtDecPwrLvl, pCoexDm->bCurIgnoreWlanAct);
	CL_PRINTF(cliBuf);

	// Hw setting		
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "============[Hw setting]============");
	CL_PRINTF(cliBuf);	

	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "RF-A, 0x1e initVal", \
		pCoexDm->btRf0x1eBackup);
	CL_PRINTF(cliBuf);

	u1Tmp[0] = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x778);
	u4Tmp[0] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x880);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x", "0x778/0x880[29:25]", \
		u1Tmp[0], (u4Tmp[0]&0x3e000000) >> 25);
	CL_PRINTF(cliBuf);


	u4Tmp[0] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x948);
	u1Tmp[0] = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x67);
	u1Tmp[1] = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x765);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x/ 0x%x", "0x948/ 0x67[5] / 0x765", \
		u4Tmp[0], ((u1Tmp[0]&0x20)>> 5), u1Tmp[1]);
	CL_PRINTF(cliBuf);
	
	u4Tmp[0] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x92c);
	u4Tmp[1] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x930);
	u4Tmp[2] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x944);	
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x/ 0x%x", "0x92c[1:0]/ 0x930[7:0]/0x944[1:0]", \
		u4Tmp[0]&0x3, u4Tmp[1]&0xff, u4Tmp[2]&0x3);
	CL_PRINTF(cliBuf);


	u1Tmp[0] = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x39);
	u1Tmp[1] = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x40);
	u4Tmp[0] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x4c);
	u1Tmp[2] = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x64);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x/ 0x%x/ 0x%x", "0x38[11]/0x40/0x4c[24:23]/0x64[0]", \
		((u1Tmp[0] & 0x8)>>3), u1Tmp[1], ((u4Tmp[0]&0x01800000)>>23), u1Tmp[2]&0x1);
	CL_PRINTF(cliBuf);

	u4Tmp[0] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x550);
	u1Tmp[0] = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x522);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x", "0x550(bcn ctrl)/0x522", \
		u4Tmp[0], u1Tmp[0]);
	CL_PRINTF(cliBuf);

	u4Tmp[0] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0xc50);
	u1Tmp[0] = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x49c);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x", "0xc50(dig)/0x49c(null-drop)", \
		u4Tmp[0]&0xff, u1Tmp[0]);
	CL_PRINTF(cliBuf);

	u4Tmp[0] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0xda0);
	u4Tmp[1] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0xda4);
	u4Tmp[2] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0xda8);
	u4Tmp[3] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0xcf0);

	u1Tmp[0] = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0xa5b);
	u1Tmp[1] = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0xa5c);

	faOfdm = ((u4Tmp[0]&0xffff0000) >> 16) +  ((u4Tmp[1]&0xffff0000) >> 16) + (u4Tmp[1] & 0xffff) +  (u4Tmp[2] & 0xffff) + \
		             ((u4Tmp[3]&0xffff0000) >> 16) + (u4Tmp[3] & 0xffff) ;
	faCck = (u1Tmp[0] << 8) + u1Tmp[1];
	
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x/ 0x%x", "OFDM-CCA/OFDM-FA/CCK-FA", \
		u4Tmp[0]&0xffff, faOfdm, faCck);
	CL_PRINTF(cliBuf);

	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d/ %d", "CRC_OK CCK/11g/11n/11n-Agg", \
		pCoexSta->nCRCOK_CCK, pCoexSta->nCRCOK_11g, pCoexSta->nCRCOK_11n, pCoexSta->nCRCOK_11nAgg);
	CL_PRINTF(cliBuf);

	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d/ %d", "CRC_Err CCK/11g/11n/11n-Agg", \
		pCoexSta->nCRCErr_CCK, pCoexSta->nCRCErr_11g, pCoexSta->nCRCErr_11n, pCoexSta->nCRCErr_11nAgg);
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
#if(BT_AUTO_REPORT_ONLY_8723B_2ANT == 1)
	//halbtc8723b2ant_MonitorBtCtr(pBtCoexist);
#endif
	pBtCoexist->fBtcDispDbgMsg(pBtCoexist, BTC_DBG_DISP_COEX_STATISTICS);
}


VOID
EXhalbtc8723b2ant_IpsNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			type
	)
{
	if(BTC_IPS_ENTER == type)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], IPS ENTER notify\n"));
		pCoexSta->bUnderIps = TRUE;
		halbtc8723b2ant_WifiOffHwCfg(pBtCoexist);
		halbtc8723b2ant_IgnoreWlanAct(pBtCoexist, FORCE_EXEC, TRUE);
		halbtc8723b2ant_CoexAllOff(pBtCoexist);
	}
	else if(BTC_IPS_LEAVE == type)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], IPS LEAVE notify\n"));
		pCoexSta->bUnderIps = FALSE;
		halbtc8723b2ant_InitHwConfig(pBtCoexist, FALSE);
		halbtc8723b2ant_InitCoexDm(pBtCoexist);
		halbtc8723b2ant_QueryBtInfo(pBtCoexist);
	}
}

VOID
EXhalbtc8723b2ant_LpsNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			type
	)
{
	if(BTC_LPS_ENABLE == type)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], LPS ENABLE notify\n"));
		pCoexSta->bUnderLps = TRUE;
	}
	else if(BTC_LPS_DISABLE == type)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], LPS DISABLE notify\n"));
		pCoexSta->bUnderLps = FALSE;
	}
}

VOID
EXhalbtc8723b2ant_ScanNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			type
	)
{
	u4Byte 	u4Tmp;
	u1Byte 	u1Tmpa, u1Tmpb;	
	


	u4Tmp = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x948);
	u1Tmpa = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x765);
	u1Tmpb = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x76e);
	
	if(BTC_SCAN_START == type)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], SCAN START notify\n"));
	}
	else if(BTC_SCAN_FINISH == type)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], SCAN FINISH notify\n"));
		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U1_AP_NUM, &pCoexSta->nScanAPNum);		
	}

	RT_TRACE(COMP_COEX, DBG_LOUD, ("############# [BTCoex], 0x948=0x%x, 0x765=0x%x, 0x76e=0x%x\n",
		u4Tmp,  u1Tmpa, u1Tmpb));
}

VOID
EXhalbtc8723b2ant_ConnectNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			type
	)
{
	if(BTC_ASSOCIATE_START == type)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], CONNECT START notify\n"));
	}
	else if(BTC_ASSOCIATE_FINISH == type)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], CONNECT FINISH notify\n"));
	}
}

VOID
EXhalbtc8723b2ant_MediaStatusNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte				type
	)
{
	u1Byte			H2C_Parameter[3] ={0};
	u4Byte			wifiBw;
	u1Byte			wifiCentralChnl;
	u1Byte			apNum=0;

	if(BTC_MEDIA_CONNECT == type)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], MEDIA connect notify\n"));
	}
	else
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], MEDIA disconnect notify\n"));
	}

	// only 2.4G we need to inform bt the chnl mask
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U1_WIFI_CENTRAL_CHNL, &wifiCentralChnl);
	if( (BTC_MEDIA_CONNECT == type) &&
		(wifiCentralChnl <= 14) )
	{
		H2C_Parameter[0] = 0x1;
		H2C_Parameter[1] = wifiCentralChnl;
		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_BW, &wifiBw);
		if(BTC_WIFI_BW_HT40 == wifiBw)
			H2C_Parameter[2] = 0x30;
		else
		{
			pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U1_AP_NUM, &apNum);
			if(apNum < 10)
				H2C_Parameter[2] = 0x30;
			else
				H2C_Parameter[2] = 0x20;
	}
	}
	
	pCoexDm->wifiChnlInfo[0] = H2C_Parameter[0];
	pCoexDm->wifiChnlInfo[1] = H2C_Parameter[1];
	pCoexDm->wifiChnlInfo[2] = H2C_Parameter[2];
	
	RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], FW write 0x66=0x%x\n", 
		H2C_Parameter[0]<<16|H2C_Parameter[1]<<8|H2C_Parameter[2]));

	pBtCoexist->fBtcFillH2c(pBtCoexist, 0x66, 3, H2C_Parameter);	
}

VOID
EXhalbtc8723b2ant_SpecialPacketNotify(
	IN	PBTC_COEXIST			pBtCoexist,
	IN	u1Byte				type
	)
{
	if(type == BTC_PACKET_DHCP)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], DHCP Packet notify\n"));
	}
}

VOID
EXhalbtc8723b2ant_BtInfoNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	pu1Byte			tmpBuf,
	IN	u1Byte			length
	)
{
	PBTC_BT_LINK_INFO	pBtLinkInfo=&pBtCoexist->btLinkInfo;
	u1Byte			btInfo=0;
	u1Byte			i, rspSource=0;
	BOOLEAN			bBtBusy=FALSE, bLimitedDig=FALSE;
	BOOLEAN			bWifiConnected=FALSE;
	static BOOLEAN		bPreScoExist=FALSE;
	u4Byte				raMask=0x0;

	pCoexSta->bC2hBtInfoReqSent = FALSE;

	rspSource = tmpBuf[0]&0xf;
	if(rspSource >= BT_INFO_SRC_8723B_2ANT_MAX)
		rspSource = BT_INFO_SRC_8723B_2ANT_WIFI_FW;
	pCoexSta->btInfoC2hCnt[rspSource]++;

	RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Bt info[%d], length=%d, hex data=[", rspSource, length));
	for(i=0; i<length; i++)
	{
		pCoexSta->btInfoC2h[rspSource][i] = tmpBuf[i];
		if(i == 1)
			btInfo = tmpBuf[i];
		if(i == length-1)
		{
			RT_TRACE(COMP_COEX, DBG_LOUD, ("0x%02x]\n", tmpBuf[i]));
		}
		else
		{
			RT_TRACE(COMP_COEX, DBG_LOUD, ("0x%02x, ", tmpBuf[i]));
		}
	}

	if(pBtCoexist->bManualControl)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BtInfoNotify(), return for Manual CTRL<===\n"));
		return;
	}

	// if 0xff, it means BT is under WHCK test
	if (btInfo == 0xff)
		pCoexSta->bBtWhckTest = TRUE;
	else
		pCoexSta->bBtWhckTest = FALSE;

	if(BT_INFO_SRC_8723B_2ANT_WIFI_FW != rspSource)
	{
		pCoexSta->btRetryCnt =	// [3:0]
			pCoexSta->btInfoC2h[rspSource][2]&0xf;

		if (pCoexSta->btRetryCnt >= 1)
			pCoexSta->popEventCnt++;

		pCoexSta->btRssi =
			pCoexSta->btInfoC2h[rspSource][3]*2+10;

		pCoexSta->btInfoExt = 
			pCoexSta->btInfoC2h[rspSource][4];

		if (pCoexSta->btInfoC2h[rspSource][2]&0x20)
			pCoexSta->bC2hBtRemoteNameReq = TRUE;
		else
			pCoexSta->bC2hBtRemoteNameReq = FALSE;			

		if (pCoexSta->btInfoC2h[rspSource][1] == 0x49)
		{
			pCoexSta->nA2DPBitPool = 
				pCoexSta->btInfoC2h[rspSource][6]; 
		}
		else
			pCoexSta->nA2DPBitPool = 0;

		pCoexSta->bBtTxRxMask = (pCoexSta->btInfoC2h[rspSource][2]&0x40);
		pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_BL_BT_TX_RX_MASK, &pCoexSta->bBtTxRxMask);
		if (pCoexSta->bBtTxRxMask)
		{
			/* BT into is responded by BT FW and BT RF REG 0x3C != 0x01 => Need to switch BT TRx Mask */				
			RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Switch BT TRx Mask since BT RF REG 0x3C != 0x01\n"));
			pBtCoexist->fBtcSetBtReg(pBtCoexist, BTC_BT_REG_RF, 0x3c, 0x01);
		}

		// Here we need to resend some wifi info to BT
		// because bt is reset and loss of the info.
		if( (pCoexSta->btInfoExt & BIT1) )
		{
			RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BT ext info bit1 check, send wifi BW&Chnl to BT!!\n"));
			pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_CONNECTED, &bWifiConnected);
			if(bWifiConnected)
			{
				EXhalbtc8723b2ant_MediaStatusNotify(pBtCoexist, BTC_MEDIA_CONNECT);
			}
			else
			{
				EXhalbtc8723b2ant_MediaStatusNotify(pBtCoexist, BTC_MEDIA_DISCONNECT);
			}
		}
		
		if( (pCoexSta->btInfoExt & BIT3) )
		{
			RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BT ext info bit3 check, set BT NOT to ignore Wlan active!!\n"));
			halbtc8723b2ant_IgnoreWlanAct(pBtCoexist, FORCE_EXEC, FALSE);
		}
		else
		{
			// BT already NOT ignore Wlan active, do nothing here.
		}
#if(BT_AUTO_REPORT_ONLY_8723B_2ANT == 0)
		if( (pCoexSta->btInfoExt & BIT4) )
		{
			// BT auto report already enabled, do nothing
		}
		else
		{
			halbtc8723b2ant_BtAutoReport(pBtCoexist, FORCE_EXEC, TRUE);
		}
#endif
	}

	// check BIT2 first ==> check if bt is under inquiry or page scan
	if(btInfo & BT_INFO_8723B_2ANT_B_INQ_PAGE)
		pCoexSta->bC2hBtInquiryPage = TRUE;
	else
		pCoexSta->bC2hBtInquiryPage = FALSE;

	// set link exist status
	if(!(btInfo&BT_INFO_8723B_2ANT_B_CONNECTION))
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
		if(btInfo & BT_INFO_8723B_2ANT_B_FTP)
			pCoexSta->bPanExist = TRUE;
		else
			pCoexSta->bPanExist = FALSE;
		if(btInfo & BT_INFO_8723B_2ANT_B_A2DP)
			pCoexSta->bA2dpExist = TRUE;
		else
			pCoexSta->bA2dpExist = FALSE;
		if(btInfo & BT_INFO_8723B_2ANT_B_HID)
			pCoexSta->bHidExist = TRUE;
		else
			pCoexSta->bHidExist = FALSE;
		if(btInfo & BT_INFO_8723B_2ANT_B_SCO_ESCO)
			pCoexSta->bScoExist = TRUE;
		else
			pCoexSta->bScoExist = FALSE;

		if ( (pCoexSta->bHidExist == FALSE) && (pCoexSta->bC2hBtInquiryPage == FALSE) && (pCoexSta->bScoExist == FALSE))
		{
			if (pCoexSta->highPriorityTx  + pCoexSta->highPriorityRx >= 160) 		
			{
				pCoexSta->bHidExist = TRUE;
				btInfo = btInfo | 0x28;
			}
		}
	}

	halbtc8723b2ant_UpdateBtLinkInfo(pBtCoexist);
	
	if(!(btInfo&BT_INFO_8723B_2ANT_B_CONNECTION))
	{
		pCoexDm->btStatus = BT_8723B_2ANT_BT_STATUS_NON_CONNECTED_IDLE;
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BtInfoNotify(), BT Non-Connected idle!!!\n"));
	}
	else if(btInfo == BT_INFO_8723B_2ANT_B_CONNECTION)	// connection exists but no busy
	{
		pCoexDm->btStatus = BT_8723B_2ANT_BT_STATUS_CONNECTED_IDLE;
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BtInfoNotify(), BT Connected-idle!!!\n"));
	}
	else if((btInfo&BT_INFO_8723B_2ANT_B_SCO_ESCO) ||
		(btInfo&BT_INFO_8723B_2ANT_B_SCO_BUSY))
	{
		pCoexDm->btStatus = BT_8723B_2ANT_BT_STATUS_SCO_BUSY;
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BtInfoNotify(), BT SCO busy!!!\n"));
	}
	else if(btInfo&BT_INFO_8723B_2ANT_B_ACL_BUSY)
	{
		pCoexDm->btStatus = BT_8723B_2ANT_BT_STATUS_ACL_BUSY;
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BtInfoNotify(), BT ACL busy!!!\n"));
	}
	else
	{
		pCoexDm->btStatus = BT_8723B_2ANT_BT_STATUS_MAX;
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BtInfoNotify(), BT Non-Defined state!!!\n"));
	}
	
	if( (BT_8723B_2ANT_BT_STATUS_ACL_BUSY == pCoexDm->btStatus) ||
		(BT_8723B_2ANT_BT_STATUS_SCO_BUSY == pCoexDm->btStatus) ||
		(BT_8723B_2ANT_BT_STATUS_ACL_SCO_BUSY == pCoexDm->btStatus) )
	{
		bBtBusy = TRUE;
		bLimitedDig = TRUE;
	}
	else
	{
		bBtBusy = FALSE;
		bLimitedDig = FALSE;
	}

	pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_BL_BT_TRAFFIC_BUSY, &bBtBusy);

	pCoexDm->bLimitedDig = bLimitedDig;
	pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_BL_BT_LIMITED_DIG, &bLimitedDig);

	halbtc8723b2ant_RunCoexistMechanism(pBtCoexist);
}

VOID
EXhalbtc8723b2ant_HaltNotify(
	IN	PBTC_COEXIST			pBtCoexist
	)
{
	RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Halt notify\n"));

	halbtc8723b2ant_WifiOffHwCfg(pBtCoexist);
	//remove due to interrupt is disabled that polling c2h will fail and delay 100ms.
	//pBtCoexist->fBtcSetBtReg(pBtCoexist, BTC_BT_REG_RF, 0x3c, 0x15); //BT goto standby while GNT_BT 1-->0
	halbtc8723b2ant_IgnoreWlanAct(pBtCoexist, FORCE_EXEC, TRUE);
	
	EXhalbtc8723b2ant_MediaStatusNotify(pBtCoexist, BTC_MEDIA_DISCONNECT);
}

VOID
EXhalbtc8723b2ant_PnpNotify(
	IN	PBTC_COEXIST			pBtCoexist,
	IN	u1Byte				pnpState
	)
{
	RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Pnp notify\n"));

	if(BTC_WIFI_PNP_SLEEP == pnpState)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Pnp notify to SLEEP\n"));
	}
	else if(BTC_WIFI_PNP_WAKE_UP == pnpState)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Pnp notify to WAKE UP\n"));
		halbtc8723b2ant_InitHwConfig(pBtCoexist, FALSE);
		halbtc8723b2ant_InitCoexDm(pBtCoexist);
		halbtc8723b2ant_QueryBtInfo(pBtCoexist);
	}
}

VOID
EXhalbtc8723b2ant_Periodical(
	IN	PBTC_COEXIST			pBtCoexist
	)
{
	//static u1Byte		disVerInfoCnt=0;
	u4Byte				fwVer=0, btPatchVer=0;
	PBTC_BOARD_INFO		pBoardInfo=&pBtCoexist->boardInfo;
	PBTC_STACK_INFO		pStackInfo=&pBtCoexist->stackInfo;
	PBTC_BT_LINK_INFO	pBtLinkInfo=&pBtCoexist->btLinkInfo;

	RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], ==========================Periodical===========================\n"));

	if(pCoexSta->disVerInfoCnt <= 5)
	{
		pCoexSta->disVerInfoCnt += 1;
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], ****************************************************************\n"));
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Ant PG Num/ Ant Mech/ Ant Pos = %d/ %d/ %d\n",
			pBoardInfo->pgAntNum, pBoardInfo->btdmAntNum, pBoardInfo->btdmAntPos));
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BT stack/ hci ext ver = %s / %d\n",
			((pStackInfo->bProfileNotified)? "Yes":"No"), pStackInfo->hciVersion));
		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_BT_PATCH_VER, &btPatchVer);
		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_FW_VER, &fwVer);
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], CoexVer/ FwVer/ PatchVer = %d_%x/ 0x%x/ 0x%x(%d)\n",
			GLCoexVerDate8723b2Ant, GLCoexVer8723b2Ant, fwVer, btPatchVer, btPatchVer));
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], ****************************************************************\n"));

		if (pCoexSta->disVerInfoCnt == 3)
		{
			//Antenna config to set 0x765 = 0x0 (GNT_BT control by PTA) after initial 
			RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Set GNT_BT control by PTA\n"));
			halbtc8723b2ant_SetAntPath(pBtCoexist, BTC_ANT_WIFI_AT_MAIN, FALSE, FALSE);
		}
	}

#if(BT_AUTO_REPORT_ONLY_8723B_2ANT == 0)
	halbtc8723b2ant_QueryBtInfo(pBtCoexist);
	halbtc8723b2ant_MonitorBtEnableDisable(pBtCoexist);
#else
	halbtc8723b2ant_MonitorBtCtr(pBtCoexist);
	halbtc8723b2ant_MonitorWiFiCtr(pBtCoexist);
	
	//for some BT speaker that Hi-Pri pkt appear begore start play, this will cause HID exist
	if ( (pCoexSta->highPriorityTx  + pCoexSta->highPriorityRx < 50) && (pBtLinkInfo->bHidExist == TRUE))
	{
		pBtLinkInfo->bHidExist  = FALSE;
	}
	
	if( halbtc8723b2ant_IsWifiStatusChanged(pBtCoexist) ||
		pCoexDm->bAutoTdmaAdjust)
	{
		halbtc8723b2ant_RunCoexistMechanism(pBtCoexist);
	}
#endif
}


#endif

