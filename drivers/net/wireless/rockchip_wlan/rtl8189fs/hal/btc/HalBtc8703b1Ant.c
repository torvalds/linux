/* SPDX-License-Identifier: GPL-2.0 */
//============================================================
// Description:
//
// This file is for RTL8703B Co-exist mechanism
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
#include "HalBtc8703b1Ant.tmh"
#endif

#if (RTL8703B_SUPPORT == 1)

#if(BT_30_SUPPORT == 1)
//============================================================
// Global variables, these are static variables
//============================================================
static COEX_DM_8703B_1ANT		GLCoexDm8703b1Ant;
static PCOEX_DM_8703B_1ANT 	pCoexDm=&GLCoexDm8703b1Ant;
static COEX_STA_8703B_1ANT		GLCoexSta8703b1Ant;
static PCOEX_STA_8703B_1ANT	pCoexSta=&GLCoexSta8703b1Ant;
static PSDSCAN_STA_8703B_1ANT	GLPsdScan8703b1Ant;
static PPSDSCAN_STA_8703B_1ANT pPsdScan = &GLPsdScan8703b1Ant;


const char *const GLBtInfoSrc8703b1Ant[]={
	"BT Info[wifi fw]",
	"BT Info[bt rsp]",
	"BT Info[bt auto report]",
};

u4Byte	GLCoexVerDate8703b1Ant=20150904;
u4Byte	GLCoexVer8703b1Ant=0x04;

//============================================================
// local function proto type if needed
//============================================================
//============================================================
// local function start with halbtc8703b1ant_
//============================================================
u1Byte
halbtc8703b1ant_BtRssiState(
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
			if(btRssi >= (rssiThresh+BTC_RSSI_COEX_THRESH_TOL_8703B_1ANT))
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
			if(btRssi >= (rssiThresh+BTC_RSSI_COEX_THRESH_TOL_8703B_1ANT))
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
			if(btRssi >= (rssiThresh1+BTC_RSSI_COEX_THRESH_TOL_8703B_1ANT))
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
halbtc8703b1ant_WifiRssiState(
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
			if(wifiRssi >= (rssiThresh+BTC_RSSI_COEX_THRESH_TOL_8703B_1ANT))
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
			if(wifiRssi >= (rssiThresh+BTC_RSSI_COEX_THRESH_TOL_8703B_1ANT))
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
			if(wifiRssi >= (rssiThresh1+BTC_RSSI_COEX_THRESH_TOL_8703B_1ANT))
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
halbtc8703b1ant_UpdateRaMask(
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
halbtc8703b1ant_AutoRateFallbackRetry(
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
halbtc8703b1ant_RetryLimit(
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
halbtc8703b1ant_AmpduMaxTime(
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
halbtc8703b1ant_LimitedTx(
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
			halbtc8703b1ant_UpdateRaMask(pBtCoexist, bForceExec, 0x0);
			break;
		case 1:	// disable cck 1/2
			halbtc8703b1ant_UpdateRaMask(pBtCoexist, bForceExec, 0x00000003);
			break;
		case 2:	// disable cck 1/2/5.5, ofdm 6/9/12/18/24, mcs 0/1/2/3/4
			halbtc8703b1ant_UpdateRaMask(pBtCoexist, bForceExec, 0x0001f1f7);
			break;
		default:
			break;
	}

	halbtc8703b1ant_AutoRateFallbackRetry(pBtCoexist, bForceExec, arfrType);
	halbtc8703b1ant_RetryLimit(pBtCoexist, bForceExec, retryLimitType);
	halbtc8703b1ant_AmpduMaxTime(pBtCoexist, bForceExec, ampduTimeType);
}

VOID
halbtc8703b1ant_LimitedRx(
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
halbtc8703b1ant_QueryBtInfo(
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

VOID
halbtc8703b1ant_MonitorBtCtr(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	u4Byte 			regHPTxRx, regLPTxRx, u4Tmp, u4Tmp1;
	u4Byte			regHPTx=0, regHPRx=0, regLPTx=0, regLPRx=0;
	u1Byte			u1Tmp, u1Tmp1;
	s4Byte			wifiRssi;
	static u1Byte		NumOfBtCounterChk = 0;
	
       //to avoid 0x76e[3] = 1 (WLAN_Act control by PTA) during IPS
	//if (! (pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x76e) & 0x8) ) 

	if (pCoexSta->bUnderIps)
	{
		//pCoexSta->highPriorityTx = 65535;
		//pCoexSta->highPriorityRx = 65535;
		//pCoexSta->lowPriorityTx = 65535;
		//pCoexSta->lowPriorityRx = 65535;
		//return;
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

	if( (pCoexSta->lowPriorityTx > 1150)  && (!pCoexSta->bC2hBtInquiryPage))
		pCoexSta->popEventCnt++;

	RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Hi-Pri Rx/Tx: %d/%d, Lo-Pri Rx/Tx: %d/%d\n",
		regHPRx, regHPTx, regLPRx, regLPTx));

	// reset counter
	pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x76e, 0xc);

	if ((regHPTx == 0) && (regHPRx ==0) && (regLPTx == 0) && (regLPRx == 0))
	{
		NumOfBtCounterChk++;
		if (NumOfBtCounterChk >= 3)
		{
			halbtc8703b1ant_QueryBtInfo(pBtCoexist);
			NumOfBtCounterChk = 0;
		}
	}
}


VOID
halbtc8703b1ant_MonitorWiFiCtr(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	u4Byte 	u4Tmp;
	u2Byte 	u2Tmp[3];
	s4Byte	wifiRssi=0;
	BOOLEAN bWifiBusy = FALSE, bWifiUnderBMode = FALSE;
	static u1Byte nCCKLockCounter = 0;
	u4Byte	TotalCnt;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_BUSY, &bWifiBusy);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_S4_WIFI_RSSI, &wifiRssi);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_UNDER_B_MODE, &bWifiUnderBMode);

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

	if ( (bWifiBusy) && (wifiRssi >= 30) && (!bWifiUnderBMode))
	{
		TotalCnt = pCoexSta->nCRCOK_CCK + pCoexSta->nCRCOK_11g + pCoexSta->nCRCOK_11n + 
				pCoexSta->nCRCOK_11nAgg;
	
		if ( (pCoexDm->btStatus == BT_8703B_1ANT_BT_STATUS_ACL_BUSY) ||
			(pCoexDm->btStatus == BT_8703B_1ANT_BT_STATUS_ACL_SCO_BUSY) ||
			(pCoexDm->btStatus == BT_8703B_1ANT_BT_STATUS_SCO_BUSY) )
		{
			if (pCoexSta->nCRCOK_CCK >(TotalCnt -pCoexSta->nCRCOK_CCK)) 
			{
				if (nCCKLockCounter < 3)
				 nCCKLockCounter++;
			}
			else
			{
				if (nCCKLockCounter > 0)
				 nCCKLockCounter--;
			}

		}
		else
		{
			if (nCCKLockCounter > 0)
			  nCCKLockCounter--;
		}
	}
	else
	{
		if (nCCKLockCounter > 0)
			nCCKLockCounter--;
	}

	if (!pCoexSta->bPreCCKLock)
	{

		if (nCCKLockCounter >= 3)
		 pCoexSta->bCCKLock = TRUE;
		else
		 pCoexSta->bCCKLock = FALSE;			
	}
	else
	{
		if (nCCKLockCounter == 0)
		 pCoexSta->bCCKLock = FALSE;
		else
		 pCoexSta->bCCKLock = TRUE;		
	}

	if  (pCoexSta->bCCKLock)
		pCoexSta->bCCKEverLock = TRUE;
	
	pCoexSta->bPreCCKLock =  pCoexSta->bCCKLock;	
		

}

BOOLEAN
halbtc8703b1ant_IsWifiStatusChanged(
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
halbtc8703b1ant_UpdateBtLinkInfo(
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
	pBtLinkInfo->bBtHiPriLinkExist = pCoexSta->bBtHiPriLinkExist;

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

VOID
halbtc8703b1ant_UpdateWifiChannelInfo(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte				type
	)
{
	u1Byte			H2C_Parameter[3] ={0};
	u4Byte			wifiBw;
	u1Byte			wifiCentralChnl;
	BOOLEAN			bWifiUnderBMode = FALSE;

	// only 2.4G we need to inform bt the chnl mask
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U1_WIFI_CENTRAL_CHNL, &wifiCentralChnl);
	if( (BTC_MEDIA_CONNECT == type) &&
		(wifiCentralChnl <= 14) )
	{
		H2C_Parameter[0] = 0x1;  //enable BT AFH skip WL channel for 8703b because BT Rx LO interference
		//H2C_Parameter[0] = 0x0;
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
	
	RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], FW write 0x66=0x%x\n", 
		H2C_Parameter[0]<<16|H2C_Parameter[1]<<8|H2C_Parameter[2]));

	pBtCoexist->fBtcFillH2c(pBtCoexist, 0x66, 3, H2C_Parameter);

}

u1Byte
halbtc8703b1ant_ActionAlgorithm(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	PBTC_BT_LINK_INFO	pBtLinkInfo=&pBtCoexist->btLinkInfo;
	BOOLEAN				bBtHsOn=FALSE;
	u1Byte				algorithm=BT_8703B_1ANT_COEX_ALGO_UNDEFINED;
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
			RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BT Profile = SCO only\n"));
			algorithm = BT_8703B_1ANT_COEX_ALGO_SCO;
		}
		else
		{
			if(pBtLinkInfo->bHidExist)
			{
				RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BT Profile = HID only\n"));
				algorithm = BT_8703B_1ANT_COEX_ALGO_HID;
			}
			else if(pBtLinkInfo->bA2dpExist)
			{
				RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BT Profile = A2DP only\n"));
				algorithm = BT_8703B_1ANT_COEX_ALGO_A2DP;
			}
			else if(pBtLinkInfo->bPanExist)
			{
				if(bBtHsOn)
				{
					RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BT Profile = PAN(HS) only\n"));
					algorithm = BT_8703B_1ANT_COEX_ALGO_PANHS;
				}
				else
				{
					RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BT Profile = PAN(EDR) only\n"));
					algorithm = BT_8703B_1ANT_COEX_ALGO_PANEDR;
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
				RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BT Profile = SCO + HID\n"));
				algorithm = BT_8703B_1ANT_COEX_ALGO_HID;
			}
			else if(pBtLinkInfo->bA2dpExist)
			{
				RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BT Profile = SCO + A2DP ==> SCO\n"));
				algorithm = BT_8703B_1ANT_COEX_ALGO_SCO;
			}
			else if(pBtLinkInfo->bPanExist)
			{
				if(bBtHsOn)
				{
					RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BT Profile = SCO + PAN(HS)\n"));
					algorithm = BT_8703B_1ANT_COEX_ALGO_SCO;
				}
				else
				{
					RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BT Profile = SCO + PAN(EDR)\n"));
					algorithm = BT_8703B_1ANT_COEX_ALGO_PANEDR_HID;
				}
			}
		}
		else
		{
			if( pBtLinkInfo->bHidExist &&
				pBtLinkInfo->bA2dpExist )
			{
				RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BT Profile = HID + A2DP\n"));
				algorithm = BT_8703B_1ANT_COEX_ALGO_HID_A2DP;
			}
			else if( pBtLinkInfo->bHidExist &&
				pBtLinkInfo->bPanExist )
			{
				if(bBtHsOn)
				{
					RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BT Profile = HID + PAN(HS)\n"));
					algorithm = BT_8703B_1ANT_COEX_ALGO_HID_A2DP;
				}
				else
				{
					RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BT Profile = HID + PAN(EDR)\n"));
					algorithm = BT_8703B_1ANT_COEX_ALGO_PANEDR_HID;
				}
			}
			else if( pBtLinkInfo->bPanExist &&
				pBtLinkInfo->bA2dpExist )
			{
				if(bBtHsOn)
				{
					RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BT Profile = A2DP + PAN(HS)\n"));
					algorithm = BT_8703B_1ANT_COEX_ALGO_A2DP_PANHS;
				}
				else
				{
					RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BT Profile = A2DP + PAN(EDR)\n"));
					algorithm = BT_8703B_1ANT_COEX_ALGO_PANEDR_A2DP;
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
				RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BT Profile = SCO + HID + A2DP ==> HID\n"));
				algorithm = BT_8703B_1ANT_COEX_ALGO_HID;
			}
			else if( pBtLinkInfo->bHidExist &&
				pBtLinkInfo->bPanExist )
			{
				if(bBtHsOn)
				{
					RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BT Profile = SCO + HID + PAN(HS)\n"));
					algorithm = BT_8703B_1ANT_COEX_ALGO_HID_A2DP;
				}
				else
				{
					RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BT Profile = SCO + HID + PAN(EDR)\n"));
					algorithm = BT_8703B_1ANT_COEX_ALGO_PANEDR_HID;
				}
			}
			else if( pBtLinkInfo->bPanExist &&
				pBtLinkInfo->bA2dpExist )
			{
				if(bBtHsOn)
				{
					RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BT Profile = SCO + A2DP + PAN(HS)\n"));
					algorithm = BT_8703B_1ANT_COEX_ALGO_SCO;
				}
				else
				{
					RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BT Profile = SCO + A2DP + PAN(EDR) ==> HID\n"));
					algorithm = BT_8703B_1ANT_COEX_ALGO_PANEDR_HID;
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
					RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BT Profile = HID + A2DP + PAN(HS)\n"));
					algorithm = BT_8703B_1ANT_COEX_ALGO_HID_A2DP;
				}
				else
				{
					RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BT Profile = HID + A2DP + PAN(EDR)\n"));
					algorithm = BT_8703B_1ANT_COEX_ALGO_HID_A2DP_PANEDR;
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
					RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Error!!! BT Profile = SCO + HID + A2DP + PAN(HS)\n"));

				}
				else
				{
					RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BT Profile = SCO + HID + A2DP + PAN(EDR)==>PAN(EDR)+HID\n"));
					algorithm = BT_8703B_1ANT_COEX_ALGO_PANEDR_HID;
				}
			}
		}
	}

	return algorithm;
}

VOID
halbtc8703b1ant_SetBtAutoReport(
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
halbtc8703b1ant_BtAutoReport(
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
	halbtc8703b1ant_SetBtAutoReport(pBtCoexist, pCoexDm->bCurBtAutoReport);

	pCoexDm->bPreBtAutoReport = pCoexDm->bCurBtAutoReport;
}

VOID halbtc8703b1ant_WriteScoreBoard(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u2Byte				scoreboardval				
	)
{
	u2Byte  val;

	val = (scoreboardval & 0x7fff) | 0x8000;

	pBtCoexist->fBtcWrite2Byte(pBtCoexist, 0xaa, val);

#if 0
	u1Byte			H2C_Parameter[3] ={0,0,0};
	

	// write "Set Status"
	H2C_Parameter[0] = 0x2;   

	// write score board 15-bit value to H2C parameter
	H2C_Parameter[1] = scoreboardval & 0xff;
	H2C_Parameter[2] = (scoreboardval & 0x7f00) >> 8;

	// Set Interrupt to BT
	H2C_Parameter[2] = H2C_Parameter[2] | 0x80; 
	
	RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], Write  BT Scoreboard: H2C 0x71[1:0]= %02x%02x\n", 
	                             H2C_Parameter[2], H2C_Parameter[1]));

	pBtCoexist->fBtcFillH2c(pBtCoexist, 0x71, 3, H2C_Parameter);

#endif 
}

VOID halbtc8703b1ant_ReadScoreBoard(
	IN	PBTC_COEXIST		pBtCoexist,
	IN   u2Byte*				uScoreBoardVal
	)
{

	*uScoreBoardVal = (pBtCoexist->fBtcRead2Byte(pBtCoexist, 0xaa)) & 0x7fff;



#if 0
	u1Byte			H2C_Parameter[3] ={0,0,0};

	// write "Get Status"
	H2C_Parameter[0] = 0x1;   

	RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], Read  BT Scoreboard!!\n"));

	pBtCoexist->fBtcFillH2c(pBtCoexist, 0x71, 3, H2C_Parameter);

	//the BT Scoreboard will be returned by C2H from  EXhalbtc8703b1ant_ScoreBoardStatusNotify()
#endif	
}

VOID halbtc8703b1ant_PostActiveStateToBT(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN				bWifiActive
	)
{

	if(bWifiActive)
	{
		RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], Post WL = Active in Scoreboard!!\n"));
		halbtc8703b1ant_WriteScoreBoard(pBtCoexist, 0x0001);
	}
	else
	{
		RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], Post WL = Non-Active in Scoreboard!!\n"));
		halbtc8703b1ant_WriteScoreBoard(pBtCoexist, 0x0000);
	}

	// The BT should set "No Shunt-down" mode if WL = Active for BT Synthesizer on/off interference WL Lo issue at 8703b b-cut.

}

VOID
halbtc8703b1ant_SetSwPenaltyTxRateAdaptive(
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
		(bLowPenaltyRa? "ON!!":"OFF!!") ));

	pBtCoexist->fBtcFillH2c(pBtCoexist, 0x69, 6, H2C_Parameter);
}

VOID
halbtc8703b1ant_LowPenaltyRa(
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
	halbtc8703b1ant_SetSwPenaltyTxRateAdaptive(pBtCoexist, pCoexDm->bCurLowPenaltyRa);

	pCoexDm->bPreLowPenaltyRa = pCoexDm->bCurLowPenaltyRa;
}

u4Byte
halbtc8703b1ant_LTECoex_InDirectReadReg(
IN	PBTC_COEXIST		pBtCoexist,
IN 	u2Byte				RegAddr
)	
{
	u4Byte j =0;

	
	//wait for ready bit before access 0x7c0		
	pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x7c0, 0x800F0000|RegAddr);

	do
	{
		j++;
	}while( ((pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x7c3) & BIT5) ==0) && (j <BT_8703B_1ANT_LTECOEX_INDIRECTREG_ACCESS_TIMEOUT) );


	return(pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x7c8));   //get read data

}

VOID
halbtc8703b1ant_LTECoex_InDirectWriteReg(
IN	PBTC_COEXIST		pBtCoexist,
IN 	u2Byte				RegAddr,
IN 	u4Byte				BitMask,
IN	u4Byte				RegValue
)
{
	u4Byte val, i=0, j=0, bitpos = 0;
	

	if (BitMask == 0x0)
	 return;	
	if (BitMask == 0xffffffff)
	{
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x7c4, RegValue); //put write data

		//wait for ready bit before access 0x7c0
		do
		{
			j++;
		}while( ((pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x7c3) & BIT5) ==0) && (j <BT_8703B_1ANT_LTECOEX_INDIRECTREG_ACCESS_TIMEOUT) );
		
		
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x7c0, 0xc00F0000|RegAddr);
	}
	else
	{
            for(i=0; i<=31; i++)
            	{
			if ( ((BitMask >> i) & 0x1) == 0x1)
			{
				bitpos = i;
				break;
			}
            	}

		//read back register value before write 
		val = halbtc8703b1ant_LTECoex_InDirectReadReg(pBtCoexist, RegAddr);
		val = (val & (~BitMask)) | (RegValue << bitpos);

		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x7c4, val); //put write data

		//wait for ready bit before access 0x7c0		
		do
		{
			j++;
		}while( ((pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x7c3) & BIT5) ==0) && (j <BT_8703B_1ANT_LTECOEX_INDIRECTREG_ACCESS_TIMEOUT) );
		
		
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x7c0, 0xc00F0000|RegAddr);
		
	}

}

void
halbtc8703b1ant_LTECoex_Enable(
IN	PBTC_COEXIST		pBtCoexist,
IN	BOOLEAN				bEnable
)
{
	u1Byte val;
	
	val =  (bEnable)? 1 : 0; 
	halbtc8703b1ant_LTECoex_InDirectWriteReg(pBtCoexist, 0x38, 0x80, val);  //0x38[7]

}

void
halbtc8703b1ant_LTECoex_PathControlOwner(
IN	PBTC_COEXIST		pBtCoexist,
IN	BOOLEAN				bWiFiControl
)
{
	u1Byte val;
	
	val =  (bWiFiControl)? 1 : 0; 
	pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0x73, 0x4, val); //0x70[26]

}

void
halbtc8703b1ant_LTECoex_Set_GNT_BT(
IN	PBTC_COEXIST		pBtCoexist,
IN	u1Byte				nControlBlock,
IN	BOOLEAN				bSWControl,
IN	u1Byte				nState
)
{
	u4Byte val=0, BitMask;
	
	nState = nState & 0x1;	
	val =  (bSWControl)? ((nState<<1) | 0x1) : 0; 

	switch(nControlBlock)
	{
		case BT_8703B_1ANT_GNT_BLOCK_RFC_BB:	
		default:
			BitMask = 0xc000;
			halbtc8703b1ant_LTECoex_InDirectWriteReg(pBtCoexist, 0x38, BitMask, val); // 0x38[15:14]
			BitMask = 0x0c00;
			halbtc8703b1ant_LTECoex_InDirectWriteReg(pBtCoexist, 0x38, BitMask, val); // 0x38[11:10]						
			break;
		case BT_8703B_1ANT_GNT_BLOCK_RFC:
			BitMask = 0xc000;
			halbtc8703b1ant_LTECoex_InDirectWriteReg(pBtCoexist, 0x38, BitMask, val); // 0x38[15:14]
			break;
		case BT_8703B_1ANT_GNT_BLOCK_BB:
			BitMask = 0x0c00;
			halbtc8703b1ant_LTECoex_InDirectWriteReg(pBtCoexist, 0x38, BitMask, val); // 0x38[11:10]
			break;

	}
	
}

void
halbtc8703b1ant_LTECoex_Set_GNT_WL(
IN	PBTC_COEXIST		pBtCoexist,
IN	u1Byte				nControlBlock,
IN	BOOLEAN				bSWControl,
IN	u1Byte				nState
)
{
	u4Byte val=0, BitMask;
	
	nState = nState & 0x1;	
	val =  (bSWControl)? ((nState<<1) | 0x1) : 0; 

	switch(nControlBlock)
	{
		case BT_8703B_1ANT_GNT_BLOCK_RFC_BB:	
		default:
			BitMask = 0x3000;
			halbtc8703b1ant_LTECoex_InDirectWriteReg(pBtCoexist, 0x38, BitMask, val); // 0x38[13:12]
			BitMask = 0x0300;
			halbtc8703b1ant_LTECoex_InDirectWriteReg(pBtCoexist, 0x38, BitMask, val); // 0x38[9:8]						
			break;
		case BT_8703B_1ANT_GNT_BLOCK_RFC:
			BitMask = 0x3000;
			halbtc8703b1ant_LTECoex_InDirectWriteReg(pBtCoexist, 0x38, BitMask, val); // 0x38[13:12]
			break;
		case BT_8703B_1ANT_GNT_BLOCK_BB:
			BitMask = 0x0300;
			halbtc8703b1ant_LTECoex_InDirectWriteReg(pBtCoexist, 0x38, BitMask, val); // 0x38[9:8]
			break;

	}

}

void
halbtc8703b1ant_LTECoex_Set_CoexTable(
IN	PBTC_COEXIST		pBtCoexist,
IN	u1Byte				nTableType,
IN	u2Byte				nTableContent
)
{
 	u2Byte RegAddr = 0x0000;

	switch(nTableType)
	{
		case BT_8703B_1ANT_CTT_WL_VS_LTE:
			RegAddr = 0xa0;
			break;
		case BT_8703B_1ANT_CTT_BT_VS_LTE:
			RegAddr = 0xa4;
			break;
	}

	if (RegAddr != 0x0000)
		halbtc8703b1ant_LTECoex_InDirectWriteReg(pBtCoexist, RegAddr, 0xffff, nTableContent); // 0xa0[15:0] or 0xa4[15:0]


}


void
halbtc8703b1ant_LTECoex_Set_BreakTable(
IN	PBTC_COEXIST		pBtCoexist,
IN	u1Byte				nTableType,
IN	u1Byte				nTableContent
)
{
	u2Byte RegAddr = 0x0000;

	switch(nTableType)
	{
		case BT_8703B_1ANT_LBTT_WL_BREAK_LTE:
			RegAddr = 0xa8;
			break;
		case BT_8703B_1ANT_LBTT_BT_BREAK_LTE:
			RegAddr = 0xac;
			break;
		case BT_8703B_1ANT_LBTT_LTE_BREAK_WL:
			RegAddr = 0xb0;
			break;
		case BT_8703B_1ANT_LBTT_LTE_BREAK_BT:
			RegAddr = 0xb4;
			break;	
	}

	if (RegAddr != 0x0000)
		halbtc8703b1ant_LTECoex_InDirectWriteReg(pBtCoexist, RegAddr, 0xff, nTableContent); // 0xa8[15:0] or 0xb4[15:0]


}

VOID
halbtc8703b1ant_SetCoexTable(
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
halbtc8703b1ant_CoexTable(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bForceExec,
	IN	u4Byte			val0x6c0,
	IN	u4Byte			val0x6c4,
	IN	u4Byte			val0x6c8,
	IN	u1Byte			val0x6cc
	)
{
	RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], %s write Coex Table 0x6c0=0x%x, 0x6c4=0x%x, 0x6cc=0x%x\n", 
		(bForceExec? "force to":""), val0x6c0, val0x6c4, val0x6cc));
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
	halbtc8703b1ant_SetCoexTable(pBtCoexist, val0x6c0, val0x6c4, val0x6c8, val0x6cc);

	pCoexDm->preVal0x6c0 = pCoexDm->curVal0x6c0;
	pCoexDm->preVal0x6c4 = pCoexDm->curVal0x6c4;
	pCoexDm->preVal0x6c8 = pCoexDm->curVal0x6c8;
	pCoexDm->preVal0x6cc = pCoexDm->curVal0x6cc;
}

VOID
halbtc8703b1ant_CoexTableWithType(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN				bForceExec,
	IN	u1Byte				type
	)
{
	PBTC_BOARD_INFO	pBoardInfo=&pBtCoexist->boardInfo;

	u4Byte	nBreakTable;
	u1Byte	nSelectTable;	


	RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], ********** CoexTable(%d) **********\n", type));
	
	pCoexSta->nCoexTableType = type;

	if (pCoexSta->bConCurrentRxModeOn == TRUE)
	{
		nBreakTable = 0xf0ffffff;  //set WL hi-pri can break BT
		nSelectTable = 0xb; 		//set Tx response = Hi-Pri (ex: Transmitting ACK,BA,CTS)
	}
	else
	{
		nBreakTable = 0xffffff;
		nSelectTable = 0x3;
	}
	
	switch(type)
	{
		case 0:
			halbtc8703b1ant_CoexTable(pBtCoexist, bForceExec, 0x55555555, 0x55555555, nBreakTable, nSelectTable);
			break;
		case 1:
			halbtc8703b1ant_CoexTable(pBtCoexist, bForceExec, 0x55555555, 0x5a5a5a5a, nBreakTable, nSelectTable);
			break;
		case 2:
			halbtc8703b1ant_CoexTable(pBtCoexist, bForceExec, 0xaa5a5a5a, 0xaa5a5a5a, nBreakTable, nSelectTable);
			break;
		case 3:
			halbtc8703b1ant_CoexTable(pBtCoexist, bForceExec, 0xaa555555, 0xaa5a5a5a, nBreakTable, nSelectTable);
			break;
		case 4:
			//if (  (pCoexSta->bCCKEverLock)  &&  (pCoexSta->nScanAPNum <= 5) )
			//	halbtc8703b1ant_CoexTable(pBtCoexist, bForceExec, 0xaa555555, 0xaaaa5a5a, nBreakTable, nSelectTable);
			//else
				halbtc8703b1ant_CoexTable(pBtCoexist, bForceExec, 0xaa555555, 0xaa5a5a5a, nBreakTable, nSelectTable);
			break;
		case 5:
			//if (  (pCoexSta->bCCKEverLock)  &&  (pCoexSta->nScanAPNum <= 5) )
			//	halbtc8703b1ant_CoexTable(pBtCoexist, bForceExec, 0xaa555555, 0xaaaa5a5a, nBreakTable, nSelectTable);
			//else
				halbtc8703b1ant_CoexTable(pBtCoexist, bForceExec, 0x5a5a5a5a, 0x5a5a5a5a, nBreakTable, nSelectTable);
			break;
		case 6:
			halbtc8703b1ant_CoexTable(pBtCoexist, bForceExec, 0x55555555, 0xaaaaaaaa, nBreakTable, nSelectTable);
			break;
		case 7:
			halbtc8703b1ant_CoexTable(pBtCoexist, bForceExec, 0xaaaaaaaa, 0xaaaaaaaa, nBreakTable, nSelectTable);
			break;
		case 8:
			halbtc8703b1ant_CoexTable(pBtCoexist, bForceExec, 0x55dd55dd, 0x5ada5ada, nBreakTable, nSelectTable);
			break;
		case 9:
			halbtc8703b1ant_CoexTable(pBtCoexist, bForceExec, 0x55dd55dd, 0x5ada5ada, nBreakTable, nSelectTable);
			break;
		case 10:
			halbtc8703b1ant_CoexTable(pBtCoexist, bForceExec, 0x55dd55dd, 0x5ada5ada, nBreakTable, nSelectTable);
			break;
		case 11:
			halbtc8703b1ant_CoexTable(pBtCoexist, bForceExec, 0x55dd55dd, 0x5ada5ada, nBreakTable, nSelectTable);
			break;
		case 12:
			halbtc8703b1ant_CoexTable(pBtCoexist, bForceExec, 0x55dd55dd, 0x5ada5ada, nBreakTable, nSelectTable);
			break;
		case 13:
			halbtc8703b1ant_CoexTable(pBtCoexist, bForceExec, 0x5fff5fff, 0xaaaaaaaa, nBreakTable, nSelectTable);
			break;
		case 14:
			halbtc8703b1ant_CoexTable(pBtCoexist, bForceExec, 0x5fff5fff, 0x5ada5ada, nBreakTable, nSelectTable);
			break;
		case 15:
			halbtc8703b1ant_CoexTable(pBtCoexist, bForceExec, 0x55dd55dd, 0xaaaaaaaa, nBreakTable, nSelectTable);
			break;	
		default:
			break;
	}
}

VOID
halbtc8703b1ant_SetFwIgnoreWlanAct(
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
halbtc8703b1ant_IgnoreWlanAct(
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
	halbtc8703b1ant_SetFwIgnoreWlanAct(pBtCoexist, bEnable);

	pCoexDm->bPreIgnoreWlanAct = pCoexDm->bCurIgnoreWlanAct;
}

VOID
halbtc8703b1ant_SetLpsRpwm(
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
halbtc8703b1ant_LpsRpwm(
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
	halbtc8703b1ant_SetLpsRpwm(pBtCoexist, lpsVal, rpwmVal);

	pCoexDm->preLps = pCoexDm->curLps;
	pCoexDm->preRpwm = pCoexDm->curRpwm;
}

VOID
halbtc8703b1ant_SwMechanism(
	IN	PBTC_COEXIST	pBtCoexist,	
	IN	BOOLEAN 		bLowPenaltyRA
	) 
{
	halbtc8703b1ant_LowPenaltyRa(pBtCoexist, NORMAL_EXEC, bLowPenaltyRA);
}

VOID
halbtc8703b1ant_SetAntPath(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte				antPosType,
	IN	BOOLEAN				bForceExec,
	IN	BOOLEAN				bInitHwCfg,
	IN	BOOLEAN				bWifiOff
	)
{
	PBTC_BOARD_INFO pBoardInfo=&pBtCoexist->boardInfo;
	u4Byte			fwVer=0, u4Tmp=0, cntBtCalChk=0;
	BOOLEAN			bPgExtSwitch=FALSE;
	BOOLEAN			bUseExtSwitch=FALSE;
	BOOLEAN			bIsInMpMode = FALSE;
	u1Byte			H2C_Parameter[2] ={0}, u1Tmp = 0;
	u4Byte			u4Tmp1=0, u4Tmp2=0;

	pCoexDm->curAntPosType = antPosType;	

#if 1	
		u4Tmp1 = halbtc8703b1ant_LTECoex_InDirectReadReg(pBtCoexist, 0x38);
		u4Tmp2 = halbtc8703b1ant_LTECoex_InDirectReadReg(pBtCoexist, 0x54);
		u1Tmp  = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x73);

		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], ********** (Before Setup) 0x73 = 0x%x, 0x38= 0x%x, 0x54= 0x%x**********\n", u1Tmp, u4Tmp1, u4Tmp2));
#endif

	if(bInitHwCfg)
	{	
		//Disable LTE Coex Function in WiFi side (this should be on if LTE coex is required)
		halbtc8703b1ant_LTECoex_Enable(pBtCoexist, 0x0);

		//GNT_WL_LTE always = 1 (this should be config if LTE coex is required)
		halbtc8703b1ant_LTECoex_Set_CoexTable(pBtCoexist, BT_8703B_1ANT_CTT_WL_VS_LTE, 0xffff);

		//GNT_BT_LTE always = 1 (this should be config if LTE coex is required)
		halbtc8703b1ant_LTECoex_Set_CoexTable(pBtCoexist, BT_8703B_1ANT_CTT_BT_VS_LTE, 0xffff);

		// Wait If BT IQK running, because Path control owner is at BT during BT IQK (setup by WiFi firmware)  
		while(cntBtCalChk <= 20)
		{
				u1Tmp = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x49d);
				cntBtCalChk++;
				if(u1Tmp & BIT0)
				{
					RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], ########### BT is calibrating (wait cnt=%d) ###########\n", cntBtCalChk));
					delay_ms(50);
				}
				else
				{
					RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], ********** BT is NOT calibrating (wait cnt=%d)**********\n", cntBtCalChk));
					break;
				}
		}
		
		//set Path control owner to WL at initial step
		halbtc8703b1ant_LTECoex_PathControlOwner(pBtCoexist, BT_8703B_1ANT_PCO_WLSIDE);			
	}
	else if(bWifiOff)
	{		
		//Disable LTE Coex Function in WiFi side 
		halbtc8703b1ant_LTECoex_Enable(pBtCoexist, 0x0);

		//if MP mode
		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_IS_IN_MP_MODE, &bIsInMpMode);
		if(bIsInMpMode)	
			halbtc8703b1ant_LTECoex_PathControlOwner(pBtCoexist, BT_8703B_1ANT_PCO_WLSIDE); //set Path control owner to WiFI
		else
			halbtc8703b1ant_LTECoex_PathControlOwner(pBtCoexist, BT_8703B_1ANT_PCO_BTSIDE);//set Path control owner to BT		
	}
	else
	{			
			halbtc8703b1ant_LTECoex_PathControlOwner(pBtCoexist, BT_8703B_1ANT_PCO_WLSIDE);		
	}
	
	
	if(bForceExec || (pCoexDm->curAntPosType != pCoexDm->preAntPosType) || bInitHwCfg || bWifiOff)
	{
		// internal switch setting
		switch(antPosType)
		{
			case BTC_ANT_PATH_WIFI:
				// set GNT_BT to low
				halbtc8703b1ant_LTECoex_Set_GNT_BT(pBtCoexist, BT_8703B_1ANT_GNT_BLOCK_RFC_BB, BT_8703B_1ANT_GNT_TYPE_CTRL_BY_SW, BT_8703B_1ANT_SIG_STA_SET_TO_LOW);
				//Set GNT_WL to high
				halbtc8703b1ant_LTECoex_Set_GNT_WL(pBtCoexist, BT_8703B_1ANT_GNT_BLOCK_RFC_BB, BT_8703B_1ANT_GNT_TYPE_CTRL_BY_SW, BT_8703B_1ANT_SIG_STA_SET_TO_HIGH);
				break;
			case BTC_ANT_PATH_BT:
				halbtc8703b1ant_LTECoex_PathControlOwner(pBtCoexist, BT_8703B_1ANT_PCO_BTSIDE);
				// set GNT_BT to high
				/* halbtc8703b1ant_LTECoex_Set_GNT_BT(pBtCoexist, BT_8703B_1ANT_GNT_BLOCK_RFC_BB, BT_8703B_1ANT_GNT_TYPE_CTRL_BY_SW, BT_8703B_1ANT_SIG_STA_SET_TO_HIGH); */
				//Set GNT_WL to low
				/* halbtc8703b1ant_LTECoex_Set_GNT_WL(pBtCoexist, BT_8703B_1ANT_GNT_BLOCK_RFC_BB, BT_8703B_1ANT_GNT_TYPE_CTRL_BY_SW, BT_8703B_1ANT_SIG_STA_SET_TO_LOW); */
				break;
			default:
			case BTC_ANT_PATH_PTA:
				// set GNT_BT to PTA
				halbtc8703b1ant_LTECoex_Set_GNT_BT(pBtCoexist, BT_8703B_1ANT_GNT_BLOCK_RFC_BB, BT_8703B_1ANT_GNT_TYPE_CTRL_BY_PTA, BT_8703B_1ANT_SIG_STA_SET_BY_HW);
				//Set GNT_WL to PTA
				halbtc8703b1ant_LTECoex_Set_GNT_WL(pBtCoexist, BT_8703B_1ANT_GNT_BLOCK_RFC_BB, BT_8703B_1ANT_GNT_TYPE_CTRL_BY_PTA, BT_8703B_1ANT_SIG_STA_SET_BY_HW);
				break;
		}
	}
	
#if 1
	u4Tmp1 = halbtc8703b1ant_LTECoex_InDirectReadReg(pBtCoexist, 0x38);
	u4Tmp2 = halbtc8703b1ant_LTECoex_InDirectReadReg(pBtCoexist, 0x54);
	u1Tmp  = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x73);

	if(bInitHwCfg)
		{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], ********** (After Init) 0x73 = 0x%x, 0x38= 0x%x, 0x54= 0x%x**********\n", u1Tmp, u4Tmp1, u4Tmp2));
		}
	else if (bWifiOff)
		{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], ********** (After WiFi off) 0x73 = 0x%x, 0x38= 0x%x, 0x54= 0x%x**********\n", u1Tmp, u4Tmp1, u4Tmp2));
		}
	else
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], ********** (After Run time) 0x73 = 0x%x, 0x38= 0x%x, 0x54= 0x%x**********\n", u1Tmp, u4Tmp1, u4Tmp2));
	}
#endif	
	
	pCoexDm->preAntPosType = pCoexDm->curAntPosType;
}


VOID
halbtc8703b1ant_SetFwPstdma(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			byte1,
	IN	u1Byte			byte2,
	IN	u1Byte			byte3,
	IN	u1Byte			byte4,
	IN	u1Byte			byte5
	)
{
	u1Byte			H2C_Parameter[5] ={0};
	u1Byte			realByte1=byte1, realByte5=byte5;
	BOOLEAN			bApEnable=FALSE;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_AP_MODE_ENABLE, &bApEnable);

	if(bApEnable)
	{
		if(byte1&BIT4 && !(byte1&BIT5))
		{			
			RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], FW for 1Ant AP mode\n"));
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
	
	RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], PS-TDMA H2C cmd =0x%x%08x\n", 
		H2C_Parameter[0], 
		H2C_Parameter[1]<<24|H2C_Parameter[2]<<16|H2C_Parameter[3]<<8|H2C_Parameter[4]));

	pBtCoexist->fBtcFillH2c(pBtCoexist, 0x60, 5, H2C_Parameter);
}


VOID
halbtc8703b1ant_PsTdma(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bForceExec,
	IN	BOOLEAN			bTurnOn,
	IN	u1Byte			type
	)
{
	PBTC_BOARD_INFO	pBoardInfo=&pBtCoexist->boardInfo;
	PBTC_BT_LINK_INFO pBtLinkInfo=&pBtCoexist->btLinkInfo;
	BOOLEAN			bTurnOnByCnt=FALSE, bWifiBusy=FALSE, bWiFiNoisy=FALSE;
	u1Byte			psTdmaTypeByCnt=0, rssiAdjustVal=0;
	u1Byte			psTdmaByte4Val = 0x50, psTdmaByte0Val = 0x51, psTdmaByte3Val =  0x10;
	s1Byte			nWiFiDurationAdjust = 0x0;
	static BOOLEAN	 bPreWifiBusy=FALSE;

	pCoexDm->bCurPsTdmaOn = bTurnOn;
	pCoexDm->curPsTdma = type;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_BUSY, &bWifiBusy);	

	if (bWifiBusy != bPreWifiBusy)
	{
		bForceExec = TRUE;	
		bPreWifiBusy = bWifiBusy;
	}

	if (pCoexDm->bCurPsTdmaOn)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], ********** TDMA(on, %d) **********\n", 
				pCoexDm->curPsTdma));
	}
	else
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], ********** TDMA(off, %d) **********\n", 
				pCoexDm->curPsTdma));
	}
			
	if(!bForceExec)
	{	
		if( (pCoexDm->bPrePsTdmaOn == pCoexDm->bCurPsTdmaOn) &&
			(pCoexDm->prePsTdma == pCoexDm->curPsTdma) )
			return;
	}

	// Adjust WiFi slot by AP number
	if (pCoexSta->nScanAPNum <= 5)
		nWiFiDurationAdjust = 5;
	else if  (pCoexSta->nScanAPNum >= 40)
		nWiFiDurationAdjust = -15;	
	else if  (pCoexSta->nScanAPNum >= 20)
		nWiFiDurationAdjust = -10;	
	
	// for A2DP only case, PS-TDMA/ TDMA
	if ((type == 1) || (type == 2) || (type == 9) || (type == 11) || (type == 101)
		|| (type == 102) || (type == 109) || (type == 101) || (type == 7) )
	{
		if (!pCoexSta->bForceLpsOn)  //Native power save TDMA, only for A2DP-only case 1/2/9/11 while wifi noisy threshold > 30
	{
			psTdmaByte0Val = 0x61;  //no null-pkt (TDMA)
		psTdmaByte3Val = 0x11; // no tx-pause at BT-slot

			if (type == 7)
				psTdmaByte4Val = 0x14;  //BT-Auto-Slot
			else
		psTdmaByte4Val = 0x10; // 0x778 = d/1 toggle, no dynamic slot
	}
		else
		{
			psTdmaByte0Val = 0x51;  //null-pkt (PS-TDMA)
			psTdmaByte3Val = 0x10; //tx-pause at BT-slot

			if (type == 7)
				psTdmaByte4Val = 0x14;  //BT-Auto-Slot
			else
			psTdmaByte4Val = 0x50; // 0x778 = d/1 toggle, dynamic slot
		}
	}
	else if ((type == 3) ||(type == 4) || (type == 13) || (type == 14) || (type == 103) || (type == 113) || (type == 114))
	{
		psTdmaByte0Val = 0x51;  //null-pkt (PS-TDMA)
		psTdmaByte3Val = 0x10; //tx-pause at BT-slot
		psTdmaByte4Val = 0x10; // 0x778 = d/1 toggle, no dynamic slot
	}
	else  //native power save case
	{
		psTdmaByte0Val = 0x61;  //no null-pkt  (TDMA)
		psTdmaByte3Val = 0x11; // no tx-pause at BT-slot
		psTdmaByte4Val = 0x11; // 0x778 = d/1 toggle, no dynamic slot
		//psTdmaByte4Va is not defne for 0x778 = d/1, 1/1 case 
		}
		 
	//	for A2DP slave 
	if ((pBtLinkInfo->bSlaveRole == TRUE)	&& (pBtLinkInfo->bA2dpExist))
		psTdmaByte4Val = psTdmaByte4Val | 0x1;  //0x778 = 0x1 at wifi slot (no blocking BT Low-Pri pkts)
		
	//  (for Antenna Detection Mechanism)	
	if (type > 100)
	{
		psTdmaByte0Val = psTdmaByte0Val | 0x82; //set antenna control by SW	
		psTdmaByte3Val = psTdmaByte3Val | 0x60;  //set antenna no toggle, control by antenna diversity
	}

		
	if(bTurnOn)
	{	 
		switch(type)
		{
			default:
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, 0x51, 0x1a, 0x1a, 0x0, psTdmaByte4Val);
				break;
			case 1:
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, psTdmaByte0Val, 0x3a+nWiFiDurationAdjust, 0x03, psTdmaByte3Val, psTdmaByte4Val);								
				break;
			case 2:
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, psTdmaByte0Val, 0x2d+nWiFiDurationAdjust, 0x03, psTdmaByte3Val, psTdmaByte4Val);							
				break;
			case 3:
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, psTdmaByte0Val, 0x3a, 0x03, psTdmaByte3Val, psTdmaByte4Val);
				break;
			case 4:
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, psTdmaByte0Val, 0x21, 0x03, psTdmaByte3Val, psTdmaByte4Val);
				break;
			case 5:
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, psTdmaByte0Val, 0x15, 0x3, psTdmaByte3Val, 0x11);
				break;
			case 6:
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, psTdmaByte0Val, 0x20, 0x3, psTdmaByte3Val, 0x11);
				break;
			case 7:
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, psTdmaByte0Val, 0x1e, 0x03, psTdmaByte3Val, psTdmaByte4Val);
				break;
			case 8:	
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, 0x51, 0x1e, 0x3, 0x10, 0x14);
				break;
			case 9:	
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, psTdmaByte0Val, 0x21, 0x3, psTdmaByte3Val, psTdmaByte4Val);				
				break;
			case 10:	
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, 0x13, 0xa, 0xa, 0x0, 0x40);
				break;
			case 11:	
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, psTdmaByte0Val, 0x21, 0x03, psTdmaByte3Val, psTdmaByte4Val);			
				break;
			case 12:
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, 0x51, 0x0a, 0x0a, 0x0, 0x50);
				break;
			case 13:
				 if (pCoexSta->nScanAPNum <= 3) // for Lenovo CPT test A2DP + OPP
				 	halbtc8703b1ant_SetFwPstdma(pBtCoexist, psTdmaByte0Val, 0x3a, 0x3, psTdmaByte3Val, psTdmaByte4Val);
				 else	
					halbtc8703b1ant_SetFwPstdma(pBtCoexist, psTdmaByte0Val, 0x21, 0x3, psTdmaByte3Val, psTdmaByte4Val);
				break;
			case 14:
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, psTdmaByte0Val, 0x21, 0x3, psTdmaByte3Val, psTdmaByte4Val);				
				break;
			case 15:
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, 0x13, 0xa, 0x3, 0x8, 0x0);
				break;
			case 16:
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, 0x93, 0x15, 0x3, 0x10, 0x0);
				break;
			case 18:
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, 0x93, 0x25, 0x3, 0x10, 0x0);
				break;			
			case 20:
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, psTdmaByte0Val, 0x3f, 0x03, psTdmaByte3Val, 0x10);
				break;
			case 21:
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, 0x61, 0x25, 0x03, 0x11, 0x11);
				break;
			case 22:
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, psTdmaByte0Val, 0x25, 0x03, psTdmaByte3Val, 0x10);
				break;
			case 23:
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, 0xe3, 0x25, 0x3, 0x31, 0x18);
				break;
			case 24:
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, 0xe3, 0x15, 0x3, 0x31, 0x18);
				break;
			case 25:
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, 0xe3, 0xa, 0x3, 0x31, 0x18);
				break;
			case 26:
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, 0xe3, 0xa, 0x3, 0x31, 0x18);
				break;
			case 27:
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, 0xe3, 0x25, 0x3, 0x31, 0x98);
				break;
			case 28:
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, 0x69, 0x25, 0x3, 0x31, 0x0);
				break;
			case 29:
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, 0xab, 0x1a, 0x1a, 0x1, 0x10);
				break;
			case 30:
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, 0x51, 0x30, 0x3, 0x10, 0x10);
				break;
			case 31:
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, 0xd3, 0x1a, 0x1a, 0, 0x58);
				break;
			case 32:
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, psTdmaByte0Val, 0x35, 0x3, psTdmaByte3Val, psTdmaByte4Val);
				break;
			case 33:
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, psTdmaByte0Val, 0x35, 0x3, psTdmaByte3Val, 0x10);
				break;
			case 34:
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, 0x53, 0x1a, 0x1a, 0x0, 0x10);
				break;
			case 35:
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, 0x63, 0x1a, 0x1a, 0x0, 0x10);
				break;
			case 36:
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, 0xd3, 0x12, 0x3, 0x14, 0x50);
				break;
			case 40: // SoftAP only with no sta associated,BT disable ,TDMA mode for power saving
				/* here softap mode screen off will cost 70-80mA for phone */
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, 0x23, 0x18, 0x00, 0x10, 0x24);
				break;	
				
			// 1-Ant translate to 2-Ant (for Antenna Detection Mechanism)
			case 101:
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, psTdmaByte0Val, 0x3a+nWiFiDurationAdjust, 0x03, psTdmaByte3Val, psTdmaByte4Val);								
				break;
			case 102:
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, psTdmaByte0Val, 0x2d+nWiFiDurationAdjust, 0x03, psTdmaByte3Val, psTdmaByte4Val);							
				break;
			case 103:
				//halbtc8703b1ant_SetFwPstdma(pBtCoexist, 0x51, 0x1d, 0x1d, 0x0, psTdmaByte4Val);
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, psTdmaByte0Val, 0x3a, 0x03, psTdmaByte3Val, psTdmaByte4Val);
				break;
			case 105:
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, psTdmaByte0Val, 0x15, 0x3, psTdmaByte3Val, 0x11);
				break;
			case 106:
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, psTdmaByte0Val, 0x20, 0x3, psTdmaByte3Val, 0x11);
				break;	
			case 109:	
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, psTdmaByte0Val, 0x21, 0x3, psTdmaByte3Val, psTdmaByte4Val);				
				break;			
			case 111:	
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, psTdmaByte0Val, 0x21, 0x03, psTdmaByte3Val, psTdmaByte4Val);			
				break;	
			case 113:
				//halbtc8703b1ant_SetFwPstdma(pBtCoexist, 0x51, 0x12, 0x12, 0x0, psTdmaByte4Val);
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, psTdmaByte0Val, 0x21, 0x3, psTdmaByte3Val, psTdmaByte4Val);
				break;
			case 114:
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, psTdmaByte0Val, 0x21, 0x3, psTdmaByte3Val, psTdmaByte4Val);				
				break;	
			case 120:
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, psTdmaByte0Val, 0x3f, 0x03, psTdmaByte3Val, 0x10);
				break;			
			case 122:
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, psTdmaByte0Val, 0x25, 0x03, psTdmaByte3Val, 0x10);
				break;
			case 132:
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, psTdmaByte0Val, 0x25, 0x03, psTdmaByte3Val, psTdmaByte4Val);
				break;	
			case 133:
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, psTdmaByte0Val, 0x25, 0x03, psTdmaByte3Val, 0x11);
				break;		
				
		}
	}
	else
	{		

		// disable PS tdma
		switch(type)
		{
			case 8: //PTA Control
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, 0x8, 0x0, 0x0, 0x0, 0x0);
				break;
			case 0:
			default:  //Software control, Antenna at BT side
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, 0x0, 0x0, 0x0, 0x0, 0x0);
				break;
			case 1: // 2-Ant, 0x778=3, antenna control by antenna diversity
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, 0x0, 0x0, 0x0, 0x48, 0x0);
				break;
#if 0
			case 9:   //Software control, Antenna at WiFi side
				halbtc8703b1ant_SetFwPstdma(pBtCoexist, 0x0, 0x0, 0x0, 0x0, 0x0);				
				halbtc8703b1ant_SetAntPath(pBtCoexist, BTC_ANT_PATH_WIFI, FALSE, FALSE);
				break;			
#endif
		}
	}
	rssiAdjustVal =0;
	pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_U1_RSSI_ADJ_VAL_FOR_1ANT_COEX_TYPE, &rssiAdjustVal);


	RT_TRACE(COMP_COEX, DBG_LOUD, ("############# [BTCoex], 0x948=0x%x, 0x765=0x%x, 0x67=0x%x\n",
		pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x948),   pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x765), pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x67)));

	// update pre state
	pCoexDm->bPrePsTdmaOn = pCoexDm->bCurPsTdmaOn;
	pCoexDm->prePsTdma = pCoexDm->curPsTdma;
}

BOOLEAN
halbtc8703b1ant_IsCommonAction(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	BOOLEAN			bCommon=FALSE, bWifiConnected=FALSE, bWifiBusy=FALSE;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_CONNECTED, &bWifiConnected);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_BUSY, &bWifiBusy);

	if(!bWifiConnected && 
		BT_8703B_1ANT_BT_STATUS_NON_CONNECTED_IDLE == pCoexDm->btStatus)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Wifi non connected-idle + BT non connected-idle!!\n"));
		
 		//halbtc8703b1ant_SwMechanism(pBtCoexist, FALSE);

		bCommon = TRUE;
	}
	else if(bWifiConnected && 
		(BT_8703B_1ANT_BT_STATUS_NON_CONNECTED_IDLE == pCoexDm->btStatus) )
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Wifi connected + BT non connected-idle!!\n"));

		//halbtc8703b1ant_SwMechanism(pBtCoexist, FALSE);

		bCommon = TRUE;
	}
	else if(!bWifiConnected && 
		(BT_8703B_1ANT_BT_STATUS_CONNECTED_IDLE == pCoexDm->btStatus) )
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Wifi non connected-idle + BT connected-idle!!\n"));

		//halbtc8703b1ant_SwMechanism(pBtCoexist, FALSE);

		bCommon = TRUE;
	}
	else if(bWifiConnected && 
		(BT_8703B_1ANT_BT_STATUS_CONNECTED_IDLE == pCoexDm->btStatus) )
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Wifi connected + BT connected-idle!!\n"));

		//halbtc8703b1ant_SwMechanism(pBtCoexist, FALSE);

		bCommon = TRUE;
	}
	else if(!bWifiConnected && 
		(BT_8703B_1ANT_BT_STATUS_CONNECTED_IDLE != pCoexDm->btStatus) )
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Wifi non connected-idle + BT Busy!!\n"));

		//halbtc8703b1ant_SwMechanism(pBtCoexist, FALSE);
		
		bCommon = TRUE;
	}
	else
	{
		if (bWifiBusy)			
		{
			RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Wifi Connected-Busy + BT Busy!!\n"));
		}
		else
		{
			RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Wifi Connected-Idle + BT Busy!!\n"));
		}
		
		bCommon = FALSE;
	}
	
	return bCommon;
}


VOID
halbtc8703b1ant_TdmaDurationAdjustForAcl(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte				wifiStatus
	)
{
	static s4Byte		up,dn,m,n,WaitCount;
	s4Byte			result;   //0: no change, +1: increase WiFi duration, -1: decrease WiFi duration
	u1Byte			retryCount=0, btInfoExt;
	static BOOLEAN	bPreWifiBusy=FALSE;
	BOOLEAN			bWifiBusy = FALSE;

	RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], TdmaDurationAdjustForAcl()\n"));

	if(BT_8703B_1ANT_WIFI_STATUS_CONNECTED_BUSY == wifiStatus) 
		bWifiBusy = TRUE;
	else
		bWifiBusy = FALSE;	 	

	if( (BT_8703B_1ANT_WIFI_STATUS_NON_CONNECTED_ASSO_AUTH_SCAN == wifiStatus) ||
		(BT_8703B_1ANT_WIFI_STATUS_CONNECTED_SCAN == wifiStatus) ||
		(BT_8703B_1ANT_WIFI_STATUS_CONNECTED_SPECIAL_PKT == wifiStatus) )
	{
		if( pCoexDm->curPsTdma != 1 &&
			pCoexDm->curPsTdma != 2 &&
			pCoexDm->curPsTdma != 3 &&
			pCoexDm->curPsTdma != 9 )
		{
			halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 9);
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
		RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], first run TdmaDurationAdjust()!!\n"));

		halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 2);
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

		if ( (pCoexSta->lowPriorityTx) > 1050 ||  (pCoexSta->lowPriorityRx) > 1250 )
			retryCount++;	
		
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

		if(result == -1)
		{
/*			if( (BT_INFO_8703B_1ANT_A2DP_BASIC_RATE(btInfoExt)) &&
				((pCoexDm->curPsTdma == 1) ||(pCoexDm->curPsTdma == 2)) )
			{
				halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 9);
				pCoexDm->psTdmaDuAdjType = 9;
			}
			else */ if(pCoexDm->curPsTdma == 1)
			{
				halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 2);
				pCoexDm->psTdmaDuAdjType = 2;
			}
			else if(pCoexDm->curPsTdma == 2)
			{
				halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 9);
				pCoexDm->psTdmaDuAdjType = 9;
			}
			else if(pCoexDm->curPsTdma == 9)
			{
				halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 11);
				pCoexDm->psTdmaDuAdjType = 11;
			}
		}
		else if(result == 1)
		{
/*			if( (BT_INFO_8703B_1ANT_A2DP_BASIC_RATE(btInfoExt)) &&
				((pCoexDm->curPsTdma == 1) ||(pCoexDm->curPsTdma == 2)) )
			{
				halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 9);
				pCoexDm->psTdmaDuAdjType = 9;
			}
			else */ if(pCoexDm->curPsTdma == 11)
			{
				halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 9);
				pCoexDm->psTdmaDuAdjType = 9;
			}
			else if(pCoexDm->curPsTdma == 9)
			{
				halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 2);
				pCoexDm->psTdmaDuAdjType = 2;
			}
			else if(pCoexDm->curPsTdma == 2)
			{
				halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 1);
				pCoexDm->psTdmaDuAdjType = 1;
			}
		}
		else	  //no change
		{
			/* Bryant Modify	
			if(bWifiBusy != bPreWifiBusy)  //if busy / idle change
			{
				bPreWifiBusy = bWifiBusy;
				halbtc8703b1ant_PsTdma(pBtCoexist, FORCE_EXEC, TRUE, pCoexDm->curPsTdma);
			}
			*/
		
			RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], ********** TDMA(on, %d) **********\n", 
				pCoexDm->curPsTdma));
		}

		if( pCoexDm->curPsTdma != 1 &&
			pCoexDm->curPsTdma != 2 &&
			pCoexDm->curPsTdma != 9 &&
			pCoexDm->curPsTdma != 11 )
		{
			// recover to previous adjust type
			halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, pCoexDm->psTdmaDuAdjType);
		}
	}
}

VOID
halbtc8703b1ant_PsTdmaCheckForPowerSaveState(
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
			halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, FALSE, 8);
		}
	}
	else						// NO PS state
	{
		if(bNewPsState)
		{
			// will enter LPS state, turn off psTdma first
			halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, FALSE, 8);
		}
		else
		{
			// keep state under NO PS state, do nothing.
		}
	}
}

VOID
halbtc8703b1ant_PowerSaveState(
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
			pCoexSta->bForceLpsOn = FALSE;
			bLowPwrDisable = FALSE;
			pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_ACT_DISABLE_LOW_POWER, &bLowPwrDisable);
			pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_ACT_NORMAL_LPS, NULL);
			
			break;
		case BTC_PS_LPS_ON:
			pCoexSta->bForceLpsOn = TRUE;
			halbtc8703b1ant_PsTdmaCheckForPowerSaveState(pBtCoexist, TRUE);
			halbtc8703b1ant_LpsRpwm(pBtCoexist, NORMAL_EXEC, lpsVal, rpwmVal);			
			// when coex force to enter LPS, do not enter 32k low power.
			bLowPwrDisable = TRUE;
			pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_ACT_DISABLE_LOW_POWER, &bLowPwrDisable);
			// power save must executed before psTdma.			
			pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_ACT_ENTER_LPS, NULL);
			
			break;
		case BTC_PS_LPS_OFF:
			pCoexSta->bForceLpsOn = FALSE;
			halbtc8703b1ant_PsTdmaCheckForPowerSaveState(pBtCoexist, FALSE);
			pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_ACT_LEAVE_LPS, NULL);
			
			break;
		default:
			break;
	}
}

VOID
halbtc8703b1ant_ActionWifiOnly(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	halbtc8703b1ant_CoexTableWithType(pBtCoexist, FORCE_EXEC, 0);
	halbtc8703b1ant_PsTdma(pBtCoexist, FORCE_EXEC, FALSE, 8);
	halbtc8703b1ant_SetAntPath(pBtCoexist, BTC_ANT_PATH_PTA, FORCE_EXEC, FALSE, FALSE);
}

VOID
halbtc8703b1ant_MonitorBtEnableDisable(
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
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BT is enabled !!\n"));
	}
	else
	{
		btDisableCnt++;
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], bt all counters=0, %d times!!\n", 
				btDisableCnt));
		if(btDisableCnt >= 2)
		{
			bBtDisabled = TRUE;
			pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_BL_BT_DISABLE, &bBtDisabled);
			RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BT is disabled !!\n"));
			halbtc8703b1ant_ActionWifiOnly(pBtCoexist);
		}
	}
	if(bPreBtDisabled != bBtDisabled)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BT is from %s to %s!!\n", 
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

/*
VOID
halbtc8703b1ant_ActionSco(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	halbtc8703b1ant_SwMechanism(pBtCoexist, TRUE);
}


VOID
halbtc8703b1ant_ActionHid(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	halbtc8703b1ant_SwMechanism(pBtCoexist, TRUE);
}

//A2DP only / PAN(EDR) only/ A2DP+PAN(HS)
VOID
halbtc8703b1ant_ActionA2dp(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	halbtc8703b1ant_SwMechanism(pBtCoexist, FALSE);
}

VOID
halbtc8703b1ant_ActionA2dpPanHs(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	halbtc8703b1ant_SwMechanism(pBtCoexist, FALSE);
}

VOID
halbtc8703b1ant_ActionPanEdr(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	halbtc8703b1ant_SwMechanism(pBtCoexist, FALSE);
}

//PAN(HS) only
VOID
halbtc8703b1ant_ActionPanHs(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	halbtc8703b1ant_SwMechanism(pBtCoexist, FALSE);
}

//PAN(EDR)+A2DP
VOID
halbtc8703b1ant_ActionPanEdrA2dp(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	halbtc8703b1ant_SwMechanism(pBtCoexist, FALSE);
}

VOID
halbtc8703b1ant_ActionPanEdrHid(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	halbtc8703b1ant_SwMechanism(pBtCoexist, TRUE);
}

// HID+A2DP+PAN(EDR)
VOID
halbtc8703b1ant_ActionHidA2dpPanEdr(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	halbtc8703b1ant_SwMechanism(pBtCoexist, TRUE);
}

VOID
halbtc8703b1ant_ActionHidA2dp(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	halbtc8703b1ant_SwMechanism(pBtCoexist, TRUE);
}

*/

//=============================================
//
//	Non-Software Coex Mechanism start
//
//=============================================
VOID
halbtc8703b1ant_ActionBtWhckTest(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	halbtc8703b1ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);
	
	halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, FALSE, 8);
	halbtc8703b1ant_SetAntPath(pBtCoexist, BTC_ANT_PATH_PTA, NORMAL_EXEC, FALSE, FALSE);
	halbtc8703b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 0);
}

VOID
halbtc8703b1ant_ActionWifiMultiPort(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	halbtc8703b1ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);
	
	halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, FALSE, 8);
	halbtc8703b1ant_SetAntPath(pBtCoexist, BTC_ANT_PATH_PTA, NORMAL_EXEC, FALSE, FALSE);
	halbtc8703b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 2);
}

VOID
halbtc8703b1ant_ActionHs(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 5);
	halbtc8703b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 2);
}

VOID
halbtc8703b1ant_ActionBtInquiry(
	IN	PBTC_COEXIST		pBtCoexist
	)
{	
	PBTC_BT_LINK_INFO pBtLinkInfo=&pBtCoexist->btLinkInfo;
	BOOLEAN			bWifiConnected=FALSE, bApEnable=FALSE, bWifiBusy=FALSE, bBtBusy=FALSE;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_AP_MODE_ENABLE, &bApEnable);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_CONNECTED, &bWifiConnected);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_BUSY, &bWifiBusy);
	pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_BL_BT_TRAFFIC_BUSY, &bBtBusy);

	if ( (!bWifiConnected) && (!pCoexSta->bWiFiIsHighPriTask) )
	{
		halbtc8703b1ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);
		halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, FALSE, 8);
		halbtc8703b1ant_SetAntPath(pBtCoexist, BTC_ANT_PATH_PTA, NORMAL_EXEC, FALSE, FALSE);
	 	halbtc8703b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 0);		
	}
	else if( (pBtLinkInfo->bScoExist) || 	(pBtLinkInfo->bHidExist) || 	(pBtLinkInfo->bA2dpExist)  )
	{
		// SCO/HID/A2DP busy
		halbtc8703b1ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);
		halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 32);
		
		halbtc8703b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 4);		
	}
	else if ( (pBtLinkInfo->bPanExist) || (bWifiBusy) )
	{
		halbtc8703b1ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);		

		//for BT inquiry/page fail after S4 resume
		//halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 20);		
		halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 32);
		
		halbtc8703b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 4);
	}
	else
	{
		halbtc8703b1ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);
		
		halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, FALSE, 8);
		halbtc8703b1ant_SetAntPath(pBtCoexist, BTC_ANT_PATH_PTA, NORMAL_EXEC, FALSE, FALSE);
	 	halbtc8703b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 7);		
		

		//halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 32);
		//halbtc8703b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 4);				
	}
}

VOID
halbtc8703b1ant_ActionBtScoHidOnlyBusy(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte				wifiStatus
	)
{
	PBTC_BT_LINK_INFO pBtLinkInfo=&pBtCoexist->btLinkInfo;
	BOOLEAN	bWifiConnected=FALSE;
	u1Byte	wifiRssiState=BTC_RSSI_STATE_HIGH;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_CONNECTED, &bWifiConnected);

	// tdma and coex table

	if(pBtLinkInfo->bScoExist)
	{
		halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 5);
		halbtc8703b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 5);
	}
	else //HID
	{
		halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 6);
		halbtc8703b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 5);
	}
}

VOID
halbtc8703b1ant_ActionWifiConnectedBtAclBusy(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte				wifiStatus
	)
{
	u1Byte		btRssiState;
	
	PBTC_BT_LINK_INFO pBtLinkInfo=&pBtCoexist->btLinkInfo;
	btRssiState = halbtc8703b1ant_BtRssiState(2, 28, 0);	

	if ( (pCoexSta->lowPriorityRx >= 950)  && (!pCoexSta->bUnderIps)  
			&&  (pCoexSta->lowPriorityRx >= pCoexSta->lowPriorityTx)  && (!pCoexSta->bC2hBtInquiryPage))
	{
		pBtLinkInfo->bSlaveRole = TRUE;
	}
	else
	{
		pBtLinkInfo->bSlaveRole = FALSE;
	}

	if(pBtLinkInfo->bHidOnly)  //HID
	{
		halbtc8703b1ant_ActionBtScoHidOnlyBusy(pBtCoexist, wifiStatus);
		pCoexDm->bAutoTdmaAdjust = FALSE;
		return;
	}
	else if(pBtLinkInfo->bA2dpOnly)  //A2DP		
	{
		if(BT_8703B_1ANT_WIFI_STATUS_CONNECTED_IDLE == wifiStatus)
		{
			halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 32);
			halbtc8703b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 4);
			pCoexDm->bAutoTdmaAdjust = FALSE;
		}
		else
		{
			//halbtc8703b1ant_TdmaDurationAdjustForAcl(pBtCoexist, wifiStatus);
			halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 7);
			halbtc8703b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 4);
			pCoexDm->bAutoTdmaAdjust = TRUE;
		}
	}
	else if ( ((pBtLinkInfo->bA2dpExist) && (pBtLinkInfo->bPanExist)) ||
		       (pBtLinkInfo->bHidExist&&pBtLinkInfo->bA2dpExist&&pBtLinkInfo->bPanExist) ) //A2DP+PAN(OPP,FTP), HID+A2DP+PAN(OPP,FTP)
	{
		halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 13);
		halbtc8703b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 4);
		pCoexDm->bAutoTdmaAdjust = FALSE;
	}
	else if(pBtLinkInfo->bHidExist&&pBtLinkInfo->bA2dpExist)  //HID+A2DP
	{
		halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 8);
		pCoexDm->bAutoTdmaAdjust = FALSE;

		halbtc8703b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 4);
	}
	else if( (pBtLinkInfo->bPanOnly) || (pBtLinkInfo->bHidExist&&pBtLinkInfo->bPanExist) ) //PAN(OPP,FTP), HID+PAN(OPP,FTP)			
	{
		if(BT_8703B_1ANT_WIFI_STATUS_CONNECTED_IDLE == wifiStatus)
			halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 4);
		else
		halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 3);
			
		halbtc8703b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 4);
		pCoexDm->bAutoTdmaAdjust = FALSE;
	}
	else
	{		
		//BT no-profile busy (0x9)
		halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 33);	
		halbtc8703b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 4);
		pCoexDm->bAutoTdmaAdjust = FALSE;
	}	
}

VOID
halbtc8703b1ant_ActionWifiNotConnected(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	// power save state
	halbtc8703b1ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);

	// tdma and coex table
	halbtc8703b1ant_PsTdma(pBtCoexist, FORCE_EXEC, FALSE, 8);
	halbtc8703b1ant_SetAntPath(pBtCoexist, BTC_ANT_PATH_PTA, NORMAL_EXEC, FALSE, FALSE);
	halbtc8703b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 0);
}

VOID
halbtc8703b1ant_ActionWifiNotConnectedScan(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	PBTC_BT_LINK_INFO pBtLinkInfo=&pBtCoexist->btLinkInfo;
	
	halbtc8703b1ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);

	// tdma and coex table
	if(BT_8703B_1ANT_BT_STATUS_ACL_BUSY == pCoexDm->btStatus)
	{
		if (pBtLinkInfo->bA2dpExist)
		{
			halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 32);
			halbtc8703b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 4);
		}
		else if (pBtLinkInfo->bA2dpExist && pBtLinkInfo->bPanExist)
		{
			halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 22);
			halbtc8703b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 4);
		}
	 	else
	 	{
		halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 20);
			halbtc8703b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 4);			
		}
	}
	else if( (BT_8703B_1ANT_BT_STATUS_SCO_BUSY == pCoexDm->btStatus) ||
			(BT_8703B_1ANT_BT_STATUS_ACL_SCO_BUSY == pCoexDm->btStatus) )
	{
		halbtc8703b1ant_ActionBtScoHidOnlyBusy(pBtCoexist,
			BT_8703B_1ANT_WIFI_STATUS_CONNECTED_SCAN);
	}
	else
	{
		//Bryant Add
		halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, FALSE, 8);
		halbtc8703b1ant_SetAntPath(pBtCoexist, BTC_ANT_PATH_PTA, NORMAL_EXEC, FALSE, FALSE);
		halbtc8703b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 2);
	}
}

VOID
halbtc8703b1ant_ActionWifiNotConnectedAssoAuth(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	PBTC_BT_LINK_INFO pBtLinkInfo=&pBtCoexist->btLinkInfo;
	
	halbtc8703b1ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);

	// tdma and coex table
	if( (pBtLinkInfo->bScoExist)  || (pBtLinkInfo->bHidExist) ||  (pBtLinkInfo->bA2dpExist) )
	{
		halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 32);
		halbtc8703b1ant_CoexTableWithType(pBtCoexist, FORCE_EXEC, 4);		
	}
	else if (pBtLinkInfo->bPanExist)   			
	{
		halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 20);
		halbtc8703b1ant_CoexTableWithType(pBtCoexist, FORCE_EXEC, 4);		
	}
	else
	{
		halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, FALSE, 8);
		halbtc8703b1ant_SetAntPath(pBtCoexist, BTC_ANT_PATH_PTA, NORMAL_EXEC, FALSE, FALSE);
		halbtc8703b1ant_CoexTableWithType(pBtCoexist, FORCE_EXEC, 2);
	}
}

VOID
halbtc8703b1ant_ActionWifiConnectedScan(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	PBTC_BT_LINK_INFO pBtLinkInfo=&pBtCoexist->btLinkInfo;
	
	halbtc8703b1ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);

	// tdma and coex table
	if(BT_8703B_1ANT_BT_STATUS_ACL_BUSY == pCoexDm->btStatus)
	{
		if (pBtLinkInfo->bA2dpExist)
		{
			halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 32);
			halbtc8703b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 4);
		}
		else if (pBtLinkInfo->bA2dpExist && pBtLinkInfo->bPanExist)
		{
			halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 22);
			halbtc8703b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 4);
		}
	 	else
	 	{
		halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 20);
			halbtc8703b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 4);
	}
	}
	else if( (BT_8703B_1ANT_BT_STATUS_SCO_BUSY == pCoexDm->btStatus) ||
			(BT_8703B_1ANT_BT_STATUS_ACL_SCO_BUSY == pCoexDm->btStatus) )
	{
		halbtc8703b1ant_ActionBtScoHidOnlyBusy(pBtCoexist,
			BT_8703B_1ANT_WIFI_STATUS_CONNECTED_SCAN);
	}
	else
	{
		//Bryant Add
		halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, FALSE, 8);
		halbtc8703b1ant_SetAntPath(pBtCoexist, BTC_ANT_PATH_PTA, NORMAL_EXEC, FALSE, FALSE);
		halbtc8703b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 2);
	}
}

VOID
halbtc8703b1ant_ActionWifiConnectedSpecialPacket(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	PBTC_BT_LINK_INFO pBtLinkInfo=&pBtCoexist->btLinkInfo;
	BOOLEAN bWifiBusy = FALSE;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_BUSY, &bWifiBusy);

	//no special packet process for both WiFi and BT very busy
	if ((bWifiBusy) && ((pBtLinkInfo->bPanExist) || (pCoexSta->nNumOfProfile >= 2)))
	 return;	

	halbtc8703b1ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);

	// tdma and coex table
	if((pBtLinkInfo->bScoExist) || (pBtLinkInfo->bHidExist))
	{
		halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 32);
		halbtc8703b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 5);		
	}
	else if  (pBtLinkInfo->bA2dpExist)
	{
		halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 32);
		halbtc8703b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 4);		
	}
	else if(pBtLinkInfo->bPanExist)
	{
		halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 20);
		halbtc8703b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 4);		
	}
	else
	{
		halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, FALSE, 8);
		halbtc8703b1ant_SetAntPath(pBtCoexist, BTC_ANT_PATH_PTA, NORMAL_EXEC, FALSE, FALSE);
		halbtc8703b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 2);
	}
}

VOID
halbtc8703b1ant_ActionWifiConnected(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	BOOLEAN 	bWifiBusy=FALSE;
	BOOLEAN		bScan=FALSE, bLink=FALSE, bRoam=FALSE;
	BOOLEAN		bUnder4way=FALSE, bApEnable=FALSE;
	u4Byte		wifiBw;

	RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], CoexForWifiConnect()===>\n"));

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_4_WAY_PROGRESS, &bUnder4way);
	if(bUnder4way)
	{
		halbtc8703b1ant_ActionWifiConnectedSpecialPacket(pBtCoexist);
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], CoexForWifiConnect(), return for wifi is under 4way<===\n"));
		return;
	}
	
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_SCAN, &bScan);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_LINK, &bLink);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_ROAM, &bRoam);
	if(bScan || bLink || bRoam)
	{
		if(bScan)	
			halbtc8703b1ant_ActionWifiConnectedScan(pBtCoexist);
		else
			halbtc8703b1ant_ActionWifiConnectedSpecialPacket(pBtCoexist);
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], CoexForWifiConnect(), return for wifi is under scan<===\n"));
		return;
	}

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_AP_MODE_ENABLE, &bApEnable);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_BUSY, &bWifiBusy);
	
	// power save state
	if(!bApEnable && BT_8703B_1ANT_BT_STATUS_ACL_BUSY == pCoexDm->btStatus && !pBtCoexist->btLinkInfo.bHidOnly)
	{
		if(pBtCoexist->btLinkInfo.bA2dpOnly)	//A2DP
			{			
			if(!bWifiBusy)
				halbtc8703b1ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);				
			else //busy
			{
				if  (pCoexSta->nScanAPNum >= BT_8703B_1ANT_WIFI_NOISY_THRESH)  //no force LPS, no PS-TDMA, use pure TDMA
				{
			halbtc8703b1ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);
				}
				else
				{
					halbtc8703b1ant_PowerSaveState(pBtCoexist, BTC_PS_LPS_ON, 0x50, 0x4);					
				}
			}
		}
		else if ((pCoexSta->bPanExist == FALSE) && (pCoexSta->bA2dpExist == FALSE) && (pCoexSta->bHidExist == FALSE))
 			halbtc8703b1ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);
		else
			halbtc8703b1ant_PowerSaveState(pBtCoexist, BTC_PS_LPS_ON, 0x50, 0x4);
	}
	else
		halbtc8703b1ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);

	// tdma and coex table
	if(!bWifiBusy)
	{
		if(BT_8703B_1ANT_BT_STATUS_ACL_BUSY == pCoexDm->btStatus)
		{
			halbtc8703b1ant_ActionWifiConnectedBtAclBusy(pBtCoexist, 
				BT_8703B_1ANT_WIFI_STATUS_CONNECTED_IDLE);
		}
		else if( (BT_8703B_1ANT_BT_STATUS_SCO_BUSY == pCoexDm->btStatus) ||
			(BT_8703B_1ANT_BT_STATUS_ACL_SCO_BUSY == pCoexDm->btStatus) )
		{
			halbtc8703b1ant_ActionBtScoHidOnlyBusy(pBtCoexist,
				BT_8703B_1ANT_WIFI_STATUS_CONNECTED_IDLE);
		}
		else
		{
			halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, FALSE, 8);
			halbtc8703b1ant_SetAntPath(pBtCoexist, BTC_ANT_PATH_PTA, NORMAL_EXEC, FALSE, FALSE);
			halbtc8703b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 2);		 
		}
	}
	else
	{
		if(BT_8703B_1ANT_BT_STATUS_ACL_BUSY == pCoexDm->btStatus)
		{
			halbtc8703b1ant_ActionWifiConnectedBtAclBusy(pBtCoexist,
				BT_8703B_1ANT_WIFI_STATUS_CONNECTED_BUSY);
		}
		else if( (BT_8703B_1ANT_BT_STATUS_SCO_BUSY == pCoexDm->btStatus) ||
			(BT_8703B_1ANT_BT_STATUS_ACL_SCO_BUSY == pCoexDm->btStatus) )
		{
			halbtc8703b1ant_ActionBtScoHidOnlyBusy(pBtCoexist,
				BT_8703B_1ANT_WIFI_STATUS_CONNECTED_BUSY);
		}
		else 
		{
			//halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, FALSE, 8);
			halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 32);
			halbtc8703b1ant_SetAntPath(pBtCoexist, BTC_ANT_PATH_PTA, NORMAL_EXEC, FALSE, FALSE);	
			//halbtc8703b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 2);	
			halbtc8703b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 4);	
		}
	}
}

VOID
halbtc8703b1ant_RunSwCoexistMechanism(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	u1Byte				algorithm=0;

	algorithm = halbtc8703b1ant_ActionAlgorithm(pBtCoexist);
	pCoexDm->curAlgorithm = algorithm;

	if(halbtc8703b1ant_IsCommonAction(pBtCoexist))
	{

	}
	else
	{
		switch(pCoexDm->curAlgorithm)
		{
			case BT_8703B_1ANT_COEX_ALGO_SCO:
				RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Action algorithm = SCO.\n"));
				//halbtc8703b1ant_ActionSco(pBtCoexist);
				break;
			case BT_8703B_1ANT_COEX_ALGO_HID:
				RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Action algorithm = HID.\n"));
				//halbtc8703b1ant_ActionHid(pBtCoexist);
				break;
			case BT_8703B_1ANT_COEX_ALGO_A2DP:
				RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Action algorithm = A2DP.\n"));
				//halbtc8703b1ant_ActionA2dp(pBtCoexist);
				break;
			case BT_8703B_1ANT_COEX_ALGO_A2DP_PANHS:
				RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Action algorithm = A2DP+PAN(HS).\n"));
				//halbtc8703b1ant_ActionA2dpPanHs(pBtCoexist);
				break;
			case BT_8703B_1ANT_COEX_ALGO_PANEDR:
				RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Action algorithm = PAN(EDR).\n"));
				//halbtc8703b1ant_ActionPanEdr(pBtCoexist);
				break;
			case BT_8703B_1ANT_COEX_ALGO_PANHS:
				RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Action algorithm = HS mode.\n"));
				//halbtc8703b1ant_ActionPanHs(pBtCoexist);
				break;
			case BT_8703B_1ANT_COEX_ALGO_PANEDR_A2DP:
				RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Action algorithm = PAN+A2DP.\n"));
				//halbtc8703b1ant_ActionPanEdrA2dp(pBtCoexist);
				break;
			case BT_8703B_1ANT_COEX_ALGO_PANEDR_HID:
				RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Action algorithm = PAN(EDR)+HID.\n"));
				//halbtc8703b1ant_ActionPanEdrHid(pBtCoexist);
				break;
			case BT_8703B_1ANT_COEX_ALGO_HID_A2DP_PANEDR:
				RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Action algorithm = HID+A2DP+PAN.\n"));
				//halbtc8703b1ant_ActionHidA2dpPanEdr(pBtCoexist);
				break;
			case BT_8703B_1ANT_COEX_ALGO_HID_A2DP:
				RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Action algorithm = HID+A2DP.\n"));
				//halbtc8703b1ant_ActionHidA2dp(pBtCoexist);
				break;
			default:
				RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Action algorithm = coexist All Off!!\n"));
				//halbtc8703b1ant_CoexAllOff(pBtCoexist);
				break;
		}
		pCoexDm->preAlgorithm = pCoexDm->curAlgorithm;
	}
}

VOID
halbtc8703b1ant_RunCoexistMechanism(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	PBTC_BT_LINK_INFO pBtLinkInfo=&pBtCoexist->btLinkInfo;
	BOOLEAN	bWifiConnected=FALSE, bBtHsOn=FALSE;
	BOOLEAN	bIncreaseScanDevNum=FALSE;
	BOOLEAN	bBtCtrlAggBufSize=FALSE;
	BOOLEAN	bMiracastPlusBt=FALSE;
	u1Byte	aggBufSize=5;
	u1Byte	wifiRssiState=BTC_RSSI_STATE_HIGH;
	u4Byte	wifiLinkStatus=0;
	u4Byte	numOfWifiLink=0, wifiBw;
	u1Byte	iotPeer=BTC_IOT_PEER_UNKNOWN;

	RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], RunCoexistMechanism()===>\n"));

	if(pBtCoexist->bManualControl)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], RunCoexistMechanism(), return for Manual CTRL <===\n"));
		return;
	}

	if(pBtCoexist->bStopCoexDm)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], RunCoexistMechanism(), return for Stop Coex DM <===\n"));
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
		halbtc8703b1ant_ActionBtWhckTest(pBtCoexist);
		return;
	}

	if( (BT_8703B_1ANT_BT_STATUS_ACL_BUSY == pCoexDm->btStatus) ||
		(BT_8703B_1ANT_BT_STATUS_SCO_BUSY == pCoexDm->btStatus) ||
		(BT_8703B_1ANT_BT_STATUS_ACL_SCO_BUSY == pCoexDm->btStatus) )
	{
		bIncreaseScanDevNum = TRUE;
	}

	pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_BL_INC_SCAN_DEV_NUM, &bIncreaseScanDevNum);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_CONNECTED, &bWifiConnected);

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_LINK_STATUS, &wifiLinkStatus);
	numOfWifiLink = wifiLinkStatus>>16;
	
	if((numOfWifiLink>=2) || (wifiLinkStatus&WIFI_P2P_GO_CONNECTED))
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("############# [BTCoex],  Multi-Port numOfWifiLink = %d, wifiLinkStatus = 0x%x\n", numOfWifiLink,wifiLinkStatus) );

		if(pBtLinkInfo->bBtLinkExist)
		{
			halbtc8703b1ant_LimitedTx(pBtCoexist, NORMAL_EXEC, 1, 1, 0, 1);
			bMiracastPlusBt = TRUE;
		}
		else
		{
		halbtc8703b1ant_LimitedTx(pBtCoexist, NORMAL_EXEC, 0, 0, 0, 0);
			bMiracastPlusBt = FALSE;
		}
		pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_BL_MIRACAST_PLUS_BT, &bMiracastPlusBt);
		halbtc8703b1ant_LimitedRx(pBtCoexist, NORMAL_EXEC, FALSE, bBtCtrlAggBufSize, aggBufSize);

		if ( (pBtLinkInfo->bA2dpExist) && (pCoexSta->bC2hBtInquiryPage) )
		{
			RT_TRACE(COMP_COEX, DBG_LOUD, ("############# [BTCoex],  BT Is Inquirying \n") );	
			halbtc8703b1ant_ActionBtInquiry(pBtCoexist);
		}
		else
		halbtc8703b1ant_ActionWifiMultiPort(pBtCoexist);
		
		return;
	}
	else
	{
		bMiracastPlusBt = FALSE;
		pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_BL_MIRACAST_PLUS_BT, &bMiracastPlusBt);
	}

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_BW, &wifiBw);

	if ( (pBtLinkInfo->bBtLinkExist) && (bWifiConnected) )
	{
		halbtc8703b1ant_LimitedTx(pBtCoexist, NORMAL_EXEC, 1, 1, 0, 1); 

		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U1_IOT_PEER, &iotPeer);

		if(BTC_IOT_PEER_CISCO != iotPeer)
		{
			if(pBtLinkInfo->bScoExist)//if (pBtLinkInfo->bBtHiPriLinkExist)
				halbtc8703b1ant_LimitedRx(pBtCoexist, NORMAL_EXEC, TRUE, FALSE, 0x5);	
			else
				halbtc8703b1ant_LimitedRx(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0x5);
		}
		else
		{
			if(pBtLinkInfo->bScoExist)
				halbtc8703b1ant_LimitedRx(pBtCoexist, NORMAL_EXEC, TRUE, FALSE, 0x5);
			else
			{
				if (BTC_WIFI_BW_HT40==wifiBw)
					halbtc8703b1ant_LimitedRx(pBtCoexist, NORMAL_EXEC, FALSE, TRUE, 0x10);	
				else
					halbtc8703b1ant_LimitedRx(pBtCoexist, NORMAL_EXEC, FALSE, TRUE, 0x8);
			}
		}

		halbtc8703b1ant_SwMechanism(pBtCoexist, TRUE);
		halbtc8703b1ant_RunSwCoexistMechanism(pBtCoexist);  //just print debug message
	}
	else
	{
		halbtc8703b1ant_LimitedTx(pBtCoexist, NORMAL_EXEC, 0, 0, 0, 0);

		halbtc8703b1ant_LimitedRx(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0x5);

		halbtc8703b1ant_SwMechanism(pBtCoexist, FALSE);
		halbtc8703b1ant_RunSwCoexistMechanism(pBtCoexist); ////just print debug message
	}
	
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_HS_OPERATION, &bBtHsOn);
	if(pCoexSta->bC2hBtInquiryPage)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("############# [BTCoex],  BT Is Inquirying \n") );		
		halbtc8703b1ant_ActionBtInquiry(pBtCoexist);
		return;
	}
	else if(bBtHsOn)
	{
		halbtc8703b1ant_ActionHs(pBtCoexist);
		return;
	}

	
	if(!bWifiConnected)
	{
		BOOLEAN	bScan=FALSE, bLink=FALSE, bRoam=FALSE;
		
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], wifi is non connected-idle !!!\n"));

		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_SCAN, &bScan);
		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_LINK, &bLink);
		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_ROAM, &bRoam);

		if(bScan || bLink || bRoam)
		{
			 if (bScan)	
		   		halbtc8703b1ant_ActionWifiNotConnectedScan(pBtCoexist);	
			 else
		    		halbtc8703b1ant_ActionWifiNotConnectedAssoAuth(pBtCoexist);	
		}
		else
			halbtc8703b1ant_ActionWifiNotConnected(pBtCoexist);
	}
	else	// wifi LPS/Busy
	{
		halbtc8703b1ant_ActionWifiConnected(pBtCoexist);
	}
}

u4Byte
halbtc8703b1ant_PSD_Log2Base(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u4Byte				val
	
	)
{
	u1Byte 	i,j;
	u4Byte	tmp, tmp2, val_integerdB=0, tindex, shiftcount=0;
	u4Byte 	result,val_fractiondB=0,Table_fraction[21]= {0,432, 332, 274, 232, 200,
								   174, 151,132,115,100,86,74,62,51,42,
								   32,23,15,7,0};

	if (val == 0)
	 return 0;

	tmp = val;

	while(1)
	{
		if (tmp == 1)
			break;	
		else
		{
			tmp = (tmp >> 1);
			shiftcount++;				
		}
	}
	

	val_integerdB = shiftcount+1;

	tmp2=1;
	for (j=1; j<= val_integerdB;j++)
	  tmp2 = tmp2*2; 		
	
	tmp = (val*100) /tmp2;
	tindex = tmp/5;

	if (tindex > 20)
	 tindex = 20;

	val_fractiondB = Table_fraction[tindex];

	result = val_integerdB*100 - val_fractiondB;			

	return (result);


}

VOID
halbtc8703b1ant_InitCoexDm(
	IN	PBTC_COEXIST		pBtCoexist
	)
{	
	// force to reset coex mechanism

	// sw all off
	halbtc8703b1ant_SwMechanism(pBtCoexist, FALSE);
	
	//halbtc8703b1ant_PsTdma(pBtCoexist, FORCE_EXEC, FALSE, 8);
	//halbtc8703b1ant_CoexTableWithType(pBtCoexist, FORCE_EXEC, 0);

	pCoexSta->popEventCnt = 0;
}

VOID
halbtc8703b1ant_InitHwConfig(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN				bBackUp,
	IN	BOOLEAN				bWifiOnly
	)
{
	PBTC_BOARD_INFO		pBoardInfo=&pBtCoexist->boardInfo;
	u4Byte				u4Tmp=0;//, fwVer;
	u2Byte				u2Tmp=0;
	u1Byte				u1Tmp=0, u1Tmpa=0, u1Tmpb=0;
	u1Byte				H2C_Parameter[2] ={0};

	u4Byte				u4Tmp1=0, u4Tmp2=0;

			
	u4Tmp1 = halbtc8703b1ant_LTECoex_InDirectReadReg(pBtCoexist, 0x38);
	u4Tmp2 = halbtc8703b1ant_LTECoex_InDirectReadReg(pBtCoexist, 0x54);

	RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], ********** (Before Init HW config) 0x38= 0x%x, 0x54= 0x%x**********\n", u4Tmp1, u4Tmp2));

	RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], 1Ant Init HW Config!!\n"));


	pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0x550, 0x8, 0x1);  //enable TBTT nterrupt

	//BT report packet sample rate	
	pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x790, 0x5);
	
	// Enable BT counter statistics
	pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x778, 0x1);		

	//Enable PTA (3-wire function form BT side)
	pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0x40, 0x20, 0x1);
	pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0x41, 0x02, 0x1);

	//Enable PTA (tx/rx signal form WiFi side)
	pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0x4c6, 0x10, 0x1);

	//enable GNT_WL/GNT_BT debug signal to GPIO14/15
	pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0x73, 0x8, 0x1);

	//enable GNT_WL
	pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0x4e, 0x40, 0x0);
	pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0x67, 0x1, 0x0);

	halbtc8703b1ant_PsTdma(pBtCoexist, FORCE_EXEC, FALSE, 8);
	
	//Antenna config
	if(bWifiOnly)
	{
		pCoexSta->bConCurrentRxModeOn = FALSE;
		halbtc8703b1ant_SetAntPath(pBtCoexist, BTC_ANT_PATH_WIFI, FORCE_EXEC, TRUE, FALSE);
		halbtc8703b1ant_SetAntPath(pBtCoexist, BTC_ANT_PATH_WIFI, FORCE_EXEC, FALSE, FALSE);
	}
	else
	{
		pCoexSta->bConCurrentRxModeOn = TRUE;		
	   	pBtCoexist->fBtcWrite1ByteBitMask(pBtCoexist, 0x953, 0x2, 0x1);
		//RF 0x1[0] = 0 -> Set GNT_WL_RF_Rx always = 1 for con-current Rx
		pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1, 0x1, 0x0);
		halbtc8703b1ant_SetAntPath(pBtCoexist, BTC_ANT_PATH_BT, FORCE_EXEC, TRUE, FALSE);
	}

	// PTA parameter
	halbtc8703b1ant_CoexTableWithType(pBtCoexist, FORCE_EXEC, 0);

}



VOID
halbtc8703b1ant_PSD_ShowData(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	pu1Byte		cliBuf=pBtCoexist->cliBuf;
	u4Byte		nDeltaFreqPerPoint;
	u4Byte		freq,freq1,freq2,n=0,i=0, j=0, m=0, PsdRep1, PsdRep2;
	
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n\n============[PSD info]  (%d)============\n",
		 	pPsdScan->nPSDGenCount);
	CL_PRINTF(cliBuf);

	if (pPsdScan->nPSDGenCount == 0)
	{
		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n No Data !!\n");
		CL_PRINTF(cliBuf);
		return;
	}

	if (pPsdScan->nPSDPoint == 0)
		nDeltaFreqPerPoint = 0;
	else			
		nDeltaFreqPerPoint = pPsdScan->nPSDBandWidth/pPsdScan->nPSDPoint;		

	//if (pPsdScan->bIsPSDShowMaxOnly)
	if (0)
	{	
		PsdRep1 = pPsdScan->nPSDMaxValue/100;
		PsdRep2 = pPsdScan->nPSDMaxValue - PsdRep1 * 100;
				
		freq = ((pPsdScan->nRealCentFreq-20) * 1000000 + pPsdScan->nPSDMaxValuePoint * nDeltaFreqPerPoint);
		freq1 = freq/1000000;
		freq2 = freq/1000 - freq1 * 1000;

		if (freq2 < 100)	
			CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n Freq = %d.0%d MHz",  
			  freq1, freq2);
		else
			CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n Freq = %d.%d MHz",  
			  freq1, freq2);

		if (PsdRep2 < 10)	
			CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, ", Value = %d.0%d dB, (%d) \n",  
			  PsdRep1, PsdRep2, pPsdScan->nPSDMaxValue);
		else
			CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, ", Value = %d.%d dB, (%d)\n",  
			  PsdRep1, PsdRep2, pPsdScan->nPSDMaxValue);
		
		CL_PRINTF(cliBuf);
	}
	else
	{
		m = pPsdScan->nPSDStartPoint;
		n = pPsdScan->nPSDStartPoint;
		i = 1;
		j = 1;				

	 while(1)
	 {
		do
		{
			freq = ((pPsdScan->nRealCentFreq-20) * 1000000 + m * nDeltaFreqPerPoint);
			freq1 = freq/1000000;
			freq2 = freq/1000 - freq1 * 1000;
			
			if (i ==1)
			{
				if (freq2 == 0)
					CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n Freq%6d.000", freq1);
				else if (freq2 < 100)	
					CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n Freq%6d.0%2d", freq1,freq2);
				else
					CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n Freq%6d.%3d", freq1,freq2);
			}
			else if  ( (i%8 == 0) || (m == pPsdScan->nPSDStopPoint) )
			{
				if (freq2 == 0)
					CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "%6d.000\n", freq1);
				else if (freq2 < 100)	
					CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "%6d.0%2d\n", freq1,freq2);
				else
					CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "%6d.%3d\n", freq1,freq2);
			}			
			else
			{
				if (freq2 == 0)
					CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "%6d.000", freq1);
				else if (freq2 < 100)	
					CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "%6d.0%2d", freq1,freq2);
				else
					CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "%6d.%3d", freq1,freq2);
			}

			i++;					
			m++;
			CL_PRINTF(cliBuf);

		}while(  (i <= 8) && (m <= pPsdScan->nPSDStopPoint));	
		
		
		do
		{
			PsdRep1 = pPsdScan->nPSDReport_MaxHold[n]/100;
			PsdRep2 = pPsdScan->nPSDReport_MaxHold[n] - PsdRep1 * 100;
				
			if (j ==1)
			{
				if (PsdRep2 <10)
					CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n Val %7d.0%d", PsdRep1,PsdRep2);
				else
					CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n Val %7d.%d", PsdRep1,PsdRep2);
			}
			else if ( (j%8 == 0)  || (n == pPsdScan->nPSDStopPoint) )
			{
				if (PsdRep2 <10)
					CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "%7d.0%d\n", PsdRep1,PsdRep2);
				else
					CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "%7d.%d\n", PsdRep1,PsdRep2);
			}
			else
			{
				if (PsdRep2 <10)
					CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "%7d.0%d", PsdRep1,PsdRep2);
				else
					CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "%7d.%d", PsdRep1,PsdRep2);
			}

			j++;
			n++;
			CL_PRINTF(cliBuf);
			
		} while(  (j <= 8) && (n <= pPsdScan->nPSDStopPoint));		

		if  ( (m > pPsdScan->nPSDStopPoint) || (n > pPsdScan->nPSDStopPoint) )
		 break;
		else
		{
			i = 1;
			j = 1;
		}
		
	 }	
	}


}

VOID
halbtc8703b1ant_PSD_MaxHoldData(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u4Byte				GenCount
	)
{
		 u4Byte	i=0, i_max=0, val_max=0, j;
		 
		if (GenCount== 1)
		{
			memcpy(pPsdScan->nPSDReport_MaxHold, pPsdScan->nPSDReport, BT_8703B_1ANT_ANTDET_PSD_POINTS*sizeof(u4Byte));

			for (i= pPsdScan->nPSDStartPoint; i<=pPsdScan->nPSDStopPoint; i++)
			{
				//RT_TRACE(COMP_COEX, DBG_LOUD, ("xxxxxxxxxxxxxxxx SweepPSDPoint(), Max_Hold i = %d, PSDReport = %d dB\n", i,  pPsdScan->nPSDReport_MaxHold[i]));
			}

			pPsdScan->nPSDMaxValuePoint = 0;
			pPsdScan->nPSDMaxValue = 0;

		}	
		else
		{
			for (i= pPsdScan->nPSDStartPoint; i<=pPsdScan->nPSDStopPoint; i++)
			{
				if (pPsdScan->nPSDReport[i] > pPsdScan->nPSDReport_MaxHold[i])
			 	pPsdScan->nPSDReport_MaxHold[i] = pPsdScan->nPSDReport[i];	

				//search Max Value
				if (i ==pPsdScan->nPSDStartPoint )
				{
					i_max = i;
					val_max = pPsdScan->nPSDReport_MaxHold[i];
				}
				else
				{
					if (pPsdScan->nPSDReport_MaxHold[i] > val_max)
					{
						i_max = i;
						val_max = pPsdScan->nPSDReport_MaxHold[i];
					}
				}

				//RT_TRACE(COMP_COEX, DBG_LOUD, ("xxxxxxxxxxxxxxxx SweepPSDPoint(), Max_Hold i = %d, PSDReport = %d dB\n", i,  pPsdScan->nPSDReport_MaxHold[i]));

			}
	
			pPsdScan->nPSDMaxValuePoint = i_max;
			pPsdScan->nPSDMaxValue = val_max;

			//RT_TRACE(COMP_COEX, DBG_LOUD, ("xxxxxxxxxxxxxxxx SweepPSDPoint(), Max_Hold i_Max = %d, PSDReport_Max = %d dB\n", pPsdScan->nPSDMaxValuePoint
			//		,pPsdScan->nPSDMaxValue));
		}


}

u4Byte
halbtc8703b1ant_PSD_GetData(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u4Byte				nPoint
	)
{
	//reg 0x808[9:0]: FFT data x
	//reg 0x808[22]: 0-->1 to get 1 FFT data y
	//reg 0x8b4[15:0]: FFT data y report

	u4Byte val = 0, psd_report =0;
	
	val = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x808);

	val &= 0xffbffc00;
	val |= nPoint;
	
	 pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x808, val);

	val |= 0x00400000;
	 pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x808, val);


	val = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x8b4);

	psd_report = val & 0x0000ffff;
	 
	return psd_report;
}


VOID
halbtc8703b1ant_PSD_SweepPoint(
IN	PBTC_COEXIST			pBtCoexist,
	IN	u4Byte				centFreq,
	IN	s4Byte				offset,
	IN	u4Byte				span,
	IN	u4Byte				points,
	IN	u4Byte				avgnum
	)
{
	u4Byte	 i,val,n,k=0;
	u4Byte	nPoints=0, psd_report=0;
	u4Byte	nStartP=0, nStopP=0, nDeltaFreqPerPoint=156250;
	u4Byte    nPSDCenterFreq=20*10^6, freq,freq1,freq2;	
	BOOLEAN outloop = FALSE;
	u1Byte	 flag = 0;	
	u4Byte 	tmp, PsdRep1, PsdRep2;
	u4Byte	WiFi_OriginalChannel = 1;

	pPsdScan->bIsPSDRunning = TRUE;
	
	do
	{
		switch(flag)
		{
		   case 0:  //Get PSD parameters
		   default:	
			//RT_TRACE(COMP_COEX, DBG_LOUD, ("xxxxxxxxxxxxxxxx SweepPSDPoint(), centFreq=0x%x, offset=0x%x, span=0x%x\n", 
		       //     centFreq, offset, span));

			pPsdScan->nPSDBandWidth = 40*1000000;
			pPsdScan->nPSDPoint = points;
			pPsdScan->nPSDStartBase = points/2; 
			pPsdScan->nPSDAvgNum = avgnum;
			pPsdScan->nRealCentFreq = centFreq;
			pPsdScan->nRealOffset = offset;
			pPsdScan->nRealSpan = span;
		
			
			nPoints = pPsdScan->nPSDPoint;
			nDeltaFreqPerPoint = pPsdScan->nPSDBandWidth/pPsdScan->nPSDPoint;

			//PSD point setup
			val = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x808);
			val &= 0xffff0fff;				
			
			switch(pPsdScan->nPSDPoint)
			{
				case 128:
					val |= 0x0;
					break;
				case 256:
				default:	
					val |=0x00004000;
				 	break;
				case 512:	
					val |= 0x00008000;
					break;
				case 1024:	
					val |= 0x0000c000;
					break;
			}			
					
			switch(pPsdScan->nPSDAvgNum)
			{
				case 1:
					val |= 0x0;
					break;
				case 8:				
					val |=0x00001000;
				 	break;
				case 16:	
					val |= 0x00002000;
					break;
				case 32:
				default:	
					val |= 0x00003000;
					break;
			}
			 pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x808, val);

			//RT_TRACE(COMP_COEX, DBG_LOUD, ("xxxxxxxxxxxxxxxx SweepPSDPoint(), PSD BW= %d, DeltaFreq=%d\n"
		 	//	, pPsdScan->nPSDBandWidth, nDeltaFreqPerPoint));
			flag = 1;
			break;
		  case 1:	  //calculate the PSD point index from freq/offset/span
		  	nPSDCenterFreq = pPsdScan->nPSDBandWidth /2 +offset*(1000000);
			//RT_TRACE(COMP_COEX, DBG_LOUD, ("xxxxxxxxxxxxxxxx SweepPSDPoint(), PSD Center Freq = %d\n", (centFreq + offset)));
			
			nStartP = pPsdScan->nPSDStartBase + (nPSDCenterFreq - span *(1000000)/2) /nDeltaFreqPerPoint;
			pPsdScan->nPSDStartPoint = nStartP - pPsdScan->nPSDStartBase;
			//RT_TRACE(COMP_COEX, DBG_LOUD, ("xxxxxxxxxxxxxxxx SweepPSDPoint(), Start PSD Poin Matrix Index = %d\n", pPsdScan->nPSDStartPoint));

			nStopP = pPsdScan->nPSDStartBase + (nPSDCenterFreq + span *(1000000)/2) /nDeltaFreqPerPoint;
			pPsdScan->nPSDStopPoint = nStopP - pPsdScan->nPSDStartBase-1;
			//RT_TRACE(COMP_COEX, DBG_LOUD, ("xxxxxxxxxxxxxxxx SweepPSDPoint(), Stop PSD Poin Matrix Index = %d\n",pPsdScan->nPSDStopPoint));

			flag = 2;
			break;
		  case 2:  //set RF channel/BW/Mode

		  	//set 3-wire off
		  	val = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x88c);
			val |= 0x00300000;
			 pBtCoexist->fBtcWrite4Byte(pBtCoexist,0x88c,val);

			//CCK off
			val = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x800);
			val &= 0xfeffffff;
			pBtCoexist->fBtcWrite4Byte(pBtCoexist,0x800,val);		

			//store WiFi original channel
			WiFi_OriginalChannel = pBtCoexist->fBtcGetRfReg(pBtCoexist, BTC_RF_A, 0x18, 0x3ff);

			//Set RF channel
		       if (centFreq == 2484)
				pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x18, 0x3ff, 0xe); 
			else
				pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x18, 0x3ff, (centFreq-2412)/5 + 1); //WiFi TRx Mask on

			//Set  RF mode = Rx, RF Gain = 0x8a0
			pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x0, 0xfffff, 0x308a0);

			//Set RF Rx filter corner
			pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1e, 0xfffff, 0x3e4);

			//Set TRx mask off
			//un-lock TRx Mask setup
			pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0xdd, 0x80, 0x1);
			pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0xdf, 0x1, 0x1);

			pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);

			flag = 3;
			break;
		  case 3:
		  	memset(pPsdScan->nPSDReport,0, pPsdScan->nPSDPoint*sizeof(u4Byte));
			nStartP = pPsdScan->nPSDStartPoint + pPsdScan->nPSDStartBase;
			nStopP = pPsdScan->nPSDStopPoint +  pPsdScan->nPSDStartBase + 1;
				
			i = nStartP;

			while (i < nStopP)
			{
				if (i >= nPoints)
				{
					psd_report = halbtc8703b1ant_PSD_GetData(pBtCoexist,i-nPoints);
				}
				else
				{
					psd_report = halbtc8703b1ant_PSD_GetData(pBtCoexist,i);
				}

				if (psd_report == 0)
					tmp = 0;
				else
					//tmp =  20*log10((double)psd_report);
					//20*log2(x)/log2(10), log2Base return theresult of the psd_report*100
					tmp = 6 * halbtc8703b1ant_PSD_Log2Base(pBtCoexist, psd_report);				

				n = i-pPsdScan->nPSDStartBase;
				pPsdScan->nPSDReport[n] =  tmp;
				PsdRep1 = pPsdScan->nPSDReport[n] /100;
				PsdRep2 = pPsdScan->nPSDReport[n] - PsdRep1 * 100;
				
				freq =  ((centFreq-20) * 1000000 + n * nDeltaFreqPerPoint);
				freq1 = freq/1000000;
				freq2 = freq/1000 - freq1 * 1000;
/*
				if (freq2 < 100)
					RT_TRACE(COMP_COEX, DBG_LOUD, ("xxxxxxxxxxxxxxxx SweepPSDPoint(), i = %d (%d.0%d MHz)", n, freq1, freq2));
				else
					RT_TRACE(COMP_COEX, DBG_LOUD, ("xxxxxxxxxxxxxxxx SweepPSDPoint(), i = %d (%d.%d MHz)", n, freq1, freq2));

				if (PsdRep2 < 10)
					RT_TRACE(COMP_COEX, DBG_LOUD, (", PSDReport = %d (%d.0%d dB)\n",psd_report, PsdRep1, PsdRep2));
				else
					RT_TRACE(COMP_COEX, DBG_LOUD, (", PSDReport = %d (%d.%d dB)\n",psd_report, PsdRep1,PsdRep2));	
*/
				i++;

				k=0;

				//Add Delay between PSD point
				while(1)
				{
					if (k++ > 20000)
					 break;
				}

				//RT_TRACE(COMP_COEX, DBG_LOUD, ("xxxxxxxxxxxxxxxx SweepPSDPoint()==============\n"));
			}	

			flag = 100;
			break;
		  case 99:	//error

			outloop = TRUE;
			break;
		  case 100: //recovery 

		  	//set 3-wire on
		  	val = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x88c);
			val &=0xffcfffff;
			pBtCoexist->fBtcWrite4Byte(pBtCoexist,0x88c,val);

			//CCK on
			val = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x800);
			val |= 0x01000000;
			pBtCoexist->fBtcWrite4Byte(pBtCoexist,0x800,val);	

			//PSD off
			val = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x808);
			val &=0xffbfffff;
			pBtCoexist->fBtcWrite4Byte(pBtCoexist,0x808,val);
			
		  	//TRx Mask on
			pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1, 0xfffff, 0x780);

			//lock TRx Mask setup
		  	pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0xdd, 0x80, 0x0);
			pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0xdf, 0x1, 0x0);

			//Set RF Rx filter corner
			pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1e, 0xfffff, 0x0);

			//restore WiFi original channel
			pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x18, 0x3ff, WiFi_OriginalChannel); 
			
			outloop = TRUE;
			break;

		}		
			
	}while (!outloop);



	pPsdScan->bIsPSDRunning = FALSE;


}

//============================================================
// work around function start with wa_halbtc8703b1ant_
//============================================================
//============================================================
// extern function start with EXhalbtc8703b1ant_
//============================================================
VOID
EXhalbtc8703b1ant_PowerOnSetting(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	PBTC_BOARD_INFO 	pBoardInfo=&pBtCoexist->boardInfo;
	u1Byte u1Tmp=0x0;
	u2Byte u2Tmp=0x0;

	RT_TRACE(COMP_COEX, DBG_LOUD, ("xxxxxxxxxxxxxxxx Execute 8703b 1-Ant PowerOn Setting xxxxxxxxxxxxxxxx!!\n"));

	RT_TRACE(COMP_COEX, DBG_LOUD, ("Ant Det Finish = %s, Ant Det Number  = %d\n",  
		 (pBoardInfo->btdmAntDetFinish? "Yes":"No"), pBoardInfo->btdmAntNumByAntDet));

	pBtCoexist->bStopCoexDm = TRUE;

	// enable BB, REG_SYS_FUNC_EN such that we can write 0x948 correctly.
	u2Tmp = pBtCoexist->fBtcRead2Byte(pBtCoexist, 0x2);
	pBtCoexist->fBtcWrite2Byte(pBtCoexist, 0x2, u2Tmp|BIT0|BIT1);

	//set Path control owner to WiFi
	halbtc8703b1ant_LTECoex_PathControlOwner(pBtCoexist, BT_8703B_1ANT_PCO_WLSIDE);

	// set GNT_BT to high
	halbtc8703b1ant_LTECoex_Set_GNT_BT(pBtCoexist, BT_8703B_1ANT_GNT_BLOCK_RFC_BB, BT_8703B_1ANT_GNT_TYPE_CTRL_BY_SW, BT_8703B_1ANT_SIG_STA_SET_TO_HIGH);
	//Set GNT_WL to low
	halbtc8703b1ant_LTECoex_Set_GNT_WL(pBtCoexist, BT_8703B_1ANT_GNT_BLOCK_RFC_BB, BT_8703B_1ANT_GNT_TYPE_CTRL_BY_SW, BT_8703B_1ANT_SIG_STA_SET_TO_LOW);

	// set WLAN_ACT = 0
	pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x76e, 0x4);	

	// 
	// S0 or S1 setting and Local register setting(By the setting fw can get ant number, S0/S1, ... info)
	// Local setting bit define
	//	BIT0: "0" for no antenna inverse; "1" for antenna inverse 
	//	BIT1: "0" for internal switch; "1" for external switch
	//	BIT2: "0" for one antenna; "1" for two antenna
	// NOTE: here default all internal switch and 1-antenna ==> BIT1=0 and BIT2=0

	u1Tmp = 0;
	pBoardInfo->btdmAntPos = BTC_ANTENNA_AT_MAIN_PORT;

	if(pBtCoexist->chipInterface == BTC_INTF_USB)
	{		
		pBtCoexist->fBtcWriteLocalReg1Byte(pBtCoexist, 0xfe08, u1Tmp);
	}
	else if(pBtCoexist->chipInterface == BTC_INTF_SDIO)
	{
		pBtCoexist->fBtcWriteLocalReg1Byte(pBtCoexist, 0x60, u1Tmp);
	}

	RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], **********  LTE coex Reg 0x38 (Power-On) = 0x%x**********\n", halbtc8703b1ant_LTECoex_InDirectReadReg(pBtCoexist, 0x38)));
		

#if 0	
	if(pBtCoexist->chipInterface == BTC_INTF_USB)
	{
		// fixed at S0 for USB interface
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x948, 0x0);

	 	u1Tmp |= 0x1;	// antenna inverse
		pBtCoexist->fBtcWriteLocalReg1Byte(pBtCoexist, 0xfe08, u1Tmp);

		pBoardInfo->btdmAntPos = BTC_ANTENNA_AT_AUX_PORT;
	}
	else
	{

		// for PCIE and SDIO interface, we check efuse 0xc3[6]
		if(pBoardInfo->singleAntPath == 0)
		{
			// set to S1
			pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x948, 0x280);
			pBoardInfo->btdmAntPos = BTC_ANTENNA_AT_MAIN_PORT;
		}
		else if(pBoardInfo->singleAntPath == 1)
		{
			// set to S0
			pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x948, 0x0);
			u1Tmp |= 0x1;	// antenna inverse
			pBoardInfo->btdmAntPos = BTC_ANTENNA_AT_AUX_PORT;
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

#endif	
}

VOID
EXhalbtc8703b1ant_PreLoadFirmware(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
}

VOID
EXhalbtc8703b1ant_InitHwConfig(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN				bWifiOnly
	)
{
	halbtc8703b1ant_InitHwConfig(pBtCoexist, TRUE, bWifiOnly);
	pBtCoexist->bStopCoexDm = FALSE;
}

VOID
EXhalbtc8703b1ant_InitCoexDm(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Coex Mechanism Init!!\n"));

	pBtCoexist->bStopCoexDm = FALSE;
	
	halbtc8703b1ant_InitCoexDm(pBtCoexist);

	halbtc8703b1ant_QueryBtInfo(pBtCoexist);
}

VOID
EXhalbtc8703b1ant_DisplayCoexInfo(
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
	if(pBtCoexist->bStopCoexDm)
	{
		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n ============[Coex is STOPPED]============");
		CL_PRINTF(cliBuf);
		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n ==========================================");
		CL_PRINTF(cliBuf);
	}

	if (pPsdScan->bAntDet_TryCount == 0)
	{
		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d", "Ant PG Num/ Mech/ Pos",
		pBoardInfo->pgAntNum, pBoardInfo->btdmAntNum, pBoardInfo->btdmAntPos);
	CL_PRINTF(cliBuf);	
	}
	else
	{
		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d  (%d/%d/%d)", "Ant PG Num/ Mech(Ant_Det)/ Pos", 
		pBoardInfo->pgAntNum, pBoardInfo->btdmAntNumByAntDet, pBoardInfo->btdmAntPos, 
		pPsdScan->bAntDet_TryCount, pPsdScan->bAntDet_FailCount, pPsdScan->nAntDet_Result);		
		CL_PRINTF(cliBuf);

		if (pBoardInfo->btdmAntDetFinish)
		{
			CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s", "Ant Det PSD Value",  pPsdScan->nAntDet_PeakVal);		
			CL_PRINTF(cliBuf);
		}
	}
	
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s / %d", "BT stack/ hci ext ver", \
		((pStackInfo->bProfileNotified)? "Yes":"No"), pStackInfo->hciVersion);
	CL_PRINTF(cliBuf);

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_BT_PATCH_VER, &btPatchVer);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_FW_VER, &fwVer);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d_%x/ 0x%x/ 0x%x(%d)", "CoexVer/ FwVer/ PatchVer", \
		GLCoexVerDate8703b1Ant, GLCoexVer8703b1Ant, fwVer, btPatchVer, btPatchVer);
	CL_PRINTF(cliBuf);

	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %02x %02x %02x ", "Wifi channel informed to BT", \
		pCoexDm->wifiChnlInfo[0], pCoexDm->wifiChnlInfo[1],
		pCoexDm->wifiChnlInfo[2]);
	CL_PRINTF(cliBuf);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s/ %s/ %s", "WifibHiPri/ Ccklock/ CckEverLock", \
		(pCoexSta->bWiFiIsHighPriTask? "Yes":"No"),
		(pCoexSta->bCCKLock? "Yes":"No"),
		(pCoexSta->bCCKEverLock? "Yes":"No"));
	CL_PRINTF(cliBuf);

	// wifi status
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "============[Wifi Status]============");
	CL_PRINTF(cliBuf);
	pBtCoexist->fBtcDispDbgMsg(pBtCoexist, BTC_DBG_DISP_WIFI_STATUS);

	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "============[BT Status]============");
	CL_PRINTF(cliBuf);
	
	PopReportIn10s++;
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = [%s/ %d/ %d/ %d] ", "BT [status/ rssi/ retryCnt/ popCnt]", \
		((pBtCoexist->btInfo.bBtDisabled)? ("disabled"):	((pCoexSta->bC2hBtInquiryPage)?("inquiry/page scan"):((BT_8703B_1ANT_BT_STATUS_NON_CONNECTED_IDLE == pCoexDm->btStatus)? "non-connected idle":
		(  (BT_8703B_1ANT_BT_STATUS_CONNECTED_IDLE == pCoexDm->btStatus)? "connected-idle":"busy")))),
		pCoexSta->btRssi, pCoexSta->btRetryCnt, pCoexSta->popEventCnt);
	CL_PRINTF(cliBuf);
	
	if (PopReportIn10s >= 5)
	{
		pCoexSta->popEventCnt = 0;	
		PopReportIn10s = 0;
	}
	
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d / %d / %d / %d", "SCO/HID/PAN/A2DP/Hi-Pri", \
		pBtLinkInfo->bScoExist, pBtLinkInfo->bHidExist, pBtLinkInfo->bPanExist, pBtLinkInfo->bA2dpExist, pBtLinkInfo->bBtHiPriLinkExist);
	CL_PRINTF(cliBuf);

	if (pStackInfo->bProfileNotified)
	{
		pBtCoexist->fBtcDispDbgMsg(pBtCoexist, BTC_DBG_DISP_BT_LINK_INFO);	
	}
	else
	{
		btInfoExt = pCoexSta->btInfoExt;
		
		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s/ %s", "BT Role/A2DP rate", \
		(pBtLinkInfo->bSlaveRole )? "Slave":"Master", (btInfoExt&BIT0)? "BR":"EDR");
		CL_PRINTF(cliBuf);	
	}	


	for(i=0; i<BT_INFO_SRC_8703B_1ANT_MAX; i++)
	{
		if(pCoexSta->btInfoC2hCnt[i])
		{				
			CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %02x %02x %02x %02x %02x %02x %02x(%d)", GLBtInfoSrc8703b1Ant[i], \
				pCoexSta->btInfoC2h[i][0], pCoexSta->btInfoC2h[i][1],
				pCoexSta->btInfoC2h[i][2], pCoexSta->btInfoC2h[i][3],
				pCoexSta->btInfoC2h[i][4], pCoexSta->btInfoC2h[i][5],
				pCoexSta->btInfoC2h[i][6], pCoexSta->btInfoC2hCnt[i]);
			CL_PRINTF(cliBuf);
		}
	}

	
	if(pBtCoexist->bManualControl)
	{			
		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "============[Sw mechanism] (before Manual)============");			
	}
	else
	{
		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "============[Sw mechanism]============");
	}	

		CL_PRINTF(cliBuf);
	
		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ 0x%x", "SM[LowPenaltyRA]/RA Mask", \
			pCoexDm->bCurLowPenaltyRa, pBtCoexist->btInfo.raMask);
		CL_PRINTF(cliBuf);

		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s/ %s/ %d ", "NoAggr/ CtrlAggr/ AggrSize", \
			(pBtCoexist->btInfo.bRejectAggPkt? "Yes":"No"), (pBtCoexist->btInfo.bBtCtrlAggBufSize? "Yes":"No"),
				pBtCoexist->btInfo.aggBufSize);
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
		
		CL_PRINTF(cliBuf);	

		psTdmaCase = pCoexDm->curPsTdma;
		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %02x %02x %02x %02x %02x case-%d (%s,%s)", "PS TDMA", \
			pCoexDm->psTdmaPara[0], pCoexDm->psTdmaPara[1],
			pCoexDm->psTdmaPara[2], pCoexDm->psTdmaPara[3],
			pCoexDm->psTdmaPara[4], psTdmaCase, 
			(pCoexDm->bCurPsTdmaOn? "On":"Off"),
			(pCoexDm->bAutoTdmaAdjust? "Adj":"Fix") );
		
		CL_PRINTF(cliBuf);
		
		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d", "WL/BT Coex Table Type", \
			pCoexSta->nCoexTableType);
		CL_PRINTF(cliBuf);

		u4Tmp[0] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x6c0);
		u4Tmp[1] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x6c4);
		u4Tmp[2] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x6c8);
		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x/ 0x%x", "0x6c0/0x6c4/0x6c8(coexTable)", \
					u4Tmp[0], u4Tmp[1], u4Tmp[2]);
		CL_PRINTF(cliBuf);

		u1Tmp[0] = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x778);
		u4Tmp[0] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x6cc);
		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x/ 0x%x", "0x778/0x6cc/IgnWlanAct", \
		u1Tmp[0], u4Tmp[0],  pCoexDm->bCurIgnoreWlanAct);
		CL_PRINTF(cliBuf);

		u4Tmp[0] = halbtc8703b1ant_LTECoex_InDirectReadReg(pBtCoexist, 0xa0);
		u4Tmp[1] = halbtc8703b1ant_LTECoex_InDirectReadReg(pBtCoexist, 0xa4);

		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x", "LTE Coex Table W_L/B_L", \
			u4Tmp[0]&0xffff, u4Tmp[1]&0xffff);
		CL_PRINTF(cliBuf);

		u4Tmp[0] = halbtc8703b1ant_LTECoex_InDirectReadReg(pBtCoexist, 0xa8);
		u4Tmp[1] = halbtc8703b1ant_LTECoex_InDirectReadReg(pBtCoexist, 0xac);
		u4Tmp[2] = halbtc8703b1ant_LTECoex_InDirectReadReg(pBtCoexist, 0xb0);
		u4Tmp[3] = halbtc8703b1ant_LTECoex_InDirectReadReg(pBtCoexist, 0xb4);

		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x/ 0x%x/ 0x%x", "LTE Break Table W_L/B_L/L_W/L_B", \
			u4Tmp[0]&0xffff, u4Tmp[1]&0xffff, u4Tmp[2]&0xffff, u4Tmp[3]&0xffff);
		CL_PRINTF(cliBuf);
				
		// Hw setting		
		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "============[Hw setting]============");
		CL_PRINTF(cliBuf);
	

	u4Tmp[0] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x430);
	u4Tmp[1] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x434);
	u2Tmp[0] = pBtCoexist->fBtcRead2Byte(pBtCoexist, 0x42a);
	u1Tmp[0] = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x456);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/0x%x/0x%x/0x%x", "0x430/0x434/0x42a/0x456", \
		u4Tmp[0], u4Tmp[1], u2Tmp[0], u1Tmp[0]);
	CL_PRINTF(cliBuf);


	u4Tmp[0] = halbtc8703b1ant_LTECoex_InDirectReadReg(pBtCoexist, 0x38);
	u1Tmp[0] = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x73);

	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %s", "LTE CoexOn/Path Ctrl Owner", \
	 ((u4Tmp[0]&BIT7)>> 7), ((u1Tmp[0]&BIT2)? "WL":"BT"));
	CL_PRINTF(cliBuf);

	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d/ %d", "LTE 3Wire/OPMode/UART/UARTMode", \
	 ((u4Tmp[0]&BIT6)>> 6), ((u4Tmp[0]&(BIT5|BIT4))>> 4),((u4Tmp[0]&BIT3)>> 3), (u4Tmp[0]&(BIT2|BIT1|BIT0)));
	CL_PRINTF(cliBuf);

	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %s", "GNT_WL_SWCtrl/GNT_BT_SWCtrl/Dbg", \
	 ((u4Tmp[0]&BIT12)>> 12), ((u4Tmp[0]&BIT14)>> 14), ((u1Tmp[0]&BIT3)? "On":"Off"));
	CL_PRINTF(cliBuf);
	
	u4Tmp[0] = halbtc8703b1ant_LTECoex_InDirectReadReg(pBtCoexist, 0x54);

	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d/ %d", "GNT_WL/GNT_BT/LTE_Busy/UART_Busy", \
		((u4Tmp[0]&BIT2)>> 2), ((u4Tmp[0]&BIT3)>> 3),  ((u4Tmp[0]&BIT1)>> 1),  (u4Tmp[0]&BIT0));
	CL_PRINTF(cliBuf);


	u1Tmp[0] = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x4c6);
	u1Tmp[1] = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x40);

	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x", "0x4c6[4]/0x40[5] (WL/BT PTA)", \
		((u1Tmp[0] & BIT4)>>4), ((u1Tmp[1] & BIT5)>>5));
	CL_PRINTF(cliBuf);

	u4Tmp[0] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x550);
	u1Tmp[0] = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x522);
	u1Tmp[1] = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x953);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x/ %s", "0x550(bcn ctrl)/0x522/4-RxAGC", \
		u4Tmp[0], u1Tmp[0], (u1Tmp[1]&0x2)? "On": "Off");
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

	u4Tmp[1] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0xc50);
	
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x/ 0x%x/ 0x%x", "0xc50/OFDM-CCA/OFDM-FA/CCK-FA", \
		u4Tmp[1]&0xff, u4Tmp[0]&0xffff, faOfdm, faCck);
	CL_PRINTF(cliBuf);


	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d/ %d", "CRC_OK CCK/11g/11n/11n-Agg", \
		pCoexSta->nCRCOK_CCK, pCoexSta->nCRCOK_11g, pCoexSta->nCRCOK_11n, pCoexSta->nCRCOK_11nAgg);
	CL_PRINTF(cliBuf);

	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d/ %d", "CRC_Err CCK/11g/11n/11n-Agg", \
		pCoexSta->nCRCErr_CCK, pCoexSta->nCRCErr_11g, pCoexSta->nCRCErr_11n, pCoexSta->nCRCErr_11nAgg);
	CL_PRINTF(cliBuf);	

	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d", "0x770(high-pri rx/tx)", \
		pCoexSta->highPriorityRx, pCoexSta->highPriorityTx);
	CL_PRINTF(cliBuf);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d", "0x774(low-pri rx/tx)", \
		pCoexSta->lowPriorityRx, pCoexSta->lowPriorityTx);
	CL_PRINTF(cliBuf);

	halbtc8703b1ant_ReadScoreBoard(pBtCoexist, 	&u2Tmp[0]);
	
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %04x", "ScoreBoard[14:0] (from BT)", u2Tmp[0]);
	CL_PRINTF(cliBuf);

#if(BT_AUTO_REPORT_ONLY_8703B_1ANT == 1)
	//halbtc8703b1ant_MonitorBtCtr(pBtCoexist);
#endif
	pBtCoexist->fBtcDispDbgMsg(pBtCoexist, BTC_DBG_DISP_COEX_STATISTICS);
}


VOID
EXhalbtc8703b1ant_IpsNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			type
	)
{
	u4Byte	u4Tmp=0;

	if(pBtCoexist->bManualControl ||	pBtCoexist->bStopCoexDm)
		return;

	if(BTC_IPS_ENTER == type)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], IPS ENTER notify\n"));
		pCoexSta->bUnderIps = TRUE;
		
		//Write WL "Active" in Score-board for LPS off
		halbtc8703b1ant_PostActiveStateToBT(pBtCoexist, FALSE);
		
		halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, FALSE, 0);
		halbtc8703b1ant_SetAntPath(pBtCoexist, BTC_ANT_PATH_BT, FORCE_EXEC, FALSE, TRUE);
		halbtc8703b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 0);	
	}
	else if(BTC_IPS_LEAVE == type)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], IPS LEAVE notify\n"));
		halbtc8703b1ant_PostActiveStateToBT(pBtCoexist, TRUE);

		halbtc8703b1ant_InitHwConfig(pBtCoexist, FALSE, FALSE);
		halbtc8703b1ant_InitCoexDm(pBtCoexist);
		halbtc8703b1ant_QueryBtInfo(pBtCoexist);

		pCoexSta->bUnderIps = FALSE;
	}
}

VOID
EXhalbtc8703b1ant_LpsNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			type
	)
{
	if(pBtCoexist->bManualControl || pBtCoexist->bStopCoexDm)
		return;

	if(BTC_LPS_ENABLE == type)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], LPS ENABLE notify\n"));
		pCoexSta->bUnderLps = TRUE;

		if (pCoexSta->bForceLpsOn == TRUE)  // LPS No-32K
		{
			//Write WL "Active" in Score-board for PS-TDMA
			halbtc8703b1ant_PostActiveStateToBT(pBtCoexist, TRUE);
	
		}
		else   //  LPS-32K, need check if this h2c 0x71 can work?? (2015/08/28)
		{
			//Write WL "Non-Active" in Score-board for Native-PS
			halbtc8703b1ant_PostActiveStateToBT(pBtCoexist, FALSE);

		}
	}
	else if(BTC_LPS_DISABLE == type)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], LPS DISABLE notify\n"));
		pCoexSta->bUnderLps = FALSE;

		
		//Write WL "Active" in Score-board for LPS off
		halbtc8703b1ant_PostActiveStateToBT(pBtCoexist, TRUE);
		
	}
}

VOID
EXhalbtc8703b1ant_ScanNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			type
	)
{
	BOOLEAN bWifiConnected=FALSE, bBtHsOn=FALSE;	
	u4Byte	wifiLinkStatus=0;
	u4Byte	numOfWifiLink=0;
	BOOLEAN	bBtCtrlAggBufSize=FALSE;
	u1Byte	aggBufSize=5;

	u1Byte u1Tmpa, u1Tmpb;
	u4Byte u4Tmp;

	if(pBtCoexist->bManualControl ||
		pBtCoexist->bStopCoexDm )
		return;

	if(BTC_SCAN_START == type)
	{
		pCoexSta->bWiFiIsHighPriTask = TRUE;
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], SCAN START notify\n"));
		halbtc8703b1ant_PostActiveStateToBT(pBtCoexist, TRUE);
		halbtc8703b1ant_PsTdma(pBtCoexist, FORCE_EXEC, FALSE, 8);  //Force antenna setup for no scan result issue
		halbtc8703b1ant_SetAntPath(pBtCoexist, BTC_ANT_PATH_PTA, FORCE_EXEC, FALSE, FALSE);		
	}
	else
	{
		pCoexSta->bWiFiIsHighPriTask = FALSE;	
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], SCAN FINISH notify\n"));

		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U1_AP_NUM, &pCoexSta->nScanAPNum);		
	}

	if(pBtCoexist->btInfo.bBtDisabled)
		return;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_HS_OPERATION, &bBtHsOn);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_CONNECTED, &bWifiConnected);

	halbtc8703b1ant_QueryBtInfo(pBtCoexist);
	
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_LINK_STATUS, &wifiLinkStatus);
	numOfWifiLink = wifiLinkStatus>>16;
	if(numOfWifiLink >= 2)
	{
		halbtc8703b1ant_LimitedTx(pBtCoexist, NORMAL_EXEC, 0, 0, 0, 0);
		halbtc8703b1ant_LimitedRx(pBtCoexist, NORMAL_EXEC, FALSE, bBtCtrlAggBufSize, aggBufSize);
		halbtc8703b1ant_ActionWifiMultiPort(pBtCoexist);
		return;
	}
	
	if(pCoexSta->bC2hBtInquiryPage)
	{
		halbtc8703b1ant_ActionBtInquiry(pBtCoexist);
		return;
	}
	else if(bBtHsOn)
	{
		halbtc8703b1ant_ActionHs(pBtCoexist);
		return;
	}

	if(BTC_SCAN_START == type)
	{	
		//RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], SCAN START notify\n"));
		if(!bWifiConnected)	// non-connected scan
		{
			halbtc8703b1ant_ActionWifiNotConnectedScan(pBtCoexist);
		}
		else	// wifi is connected
		{
			halbtc8703b1ant_ActionWifiConnectedScan(pBtCoexist);
		}
	}
	else if(BTC_SCAN_FINISH == type)
	{
		//RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], SCAN FINISH notify\n"));
		if(!bWifiConnected)	// non-connected scan
		{
			halbtc8703b1ant_ActionWifiNotConnected(pBtCoexist);
		}
		else
		{
			halbtc8703b1ant_ActionWifiConnected(pBtCoexist);
		}
	}
}

VOID
EXhalbtc8703b1ant_ConnectNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			type
	)
{
	BOOLEAN	bWifiConnected=FALSE, bBtHsOn=FALSE;	
	u4Byte	wifiLinkStatus=0;
	u4Byte	numOfWifiLink=0;
	BOOLEAN	bBtCtrlAggBufSize=FALSE;
	u1Byte	aggBufSize=5;

	if(pBtCoexist->bManualControl ||
		pBtCoexist->bStopCoexDm ||
		pBtCoexist->btInfo.bBtDisabled )
		return;

	if(BTC_ASSOCIATE_START == type)
	{
		pCoexSta->bWiFiIsHighPriTask = TRUE;
		halbtc8703b1ant_PostActiveStateToBT(pBtCoexist, TRUE);
		halbtc8703b1ant_PsTdma(pBtCoexist, FORCE_EXEC, FALSE, 8);  //Force antenna setup for no scan result issue
		halbtc8703b1ant_SetAntPath(pBtCoexist, BTC_ANT_PATH_PTA, FORCE_EXEC, FALSE, FALSE);
		 RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], CONNECT START notify\n"));	
		 pCoexDm->nArpCnt = 0;
	}
	else
	{
		pCoexSta->bWiFiIsHighPriTask = FALSE;	
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], CONNECT FINISH notify\n"));	
		//pCoexDm->nArpCnt = 0;
	}

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_LINK_STATUS, &wifiLinkStatus);
	numOfWifiLink = wifiLinkStatus>>16;
	if(numOfWifiLink >= 2)
	{
		halbtc8703b1ant_LimitedTx(pBtCoexist, NORMAL_EXEC, 0, 0, 0, 0);
		halbtc8703b1ant_LimitedRx(pBtCoexist, NORMAL_EXEC, FALSE, bBtCtrlAggBufSize, aggBufSize);
		halbtc8703b1ant_ActionWifiMultiPort(pBtCoexist);
		return;
	}

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_HS_OPERATION, &bBtHsOn);
	if(pCoexSta->bC2hBtInquiryPage)
	{
		halbtc8703b1ant_ActionBtInquiry(pBtCoexist);
		return;
	}
	else if(bBtHsOn)
	{
		halbtc8703b1ant_ActionHs(pBtCoexist);
		return;
	}

	if(BTC_ASSOCIATE_START == type)
	{
		//RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], CONNECT START notify\n"));		
		halbtc8703b1ant_ActionWifiNotConnectedAssoAuth(pBtCoexist);
	}
	else if(BTC_ASSOCIATE_FINISH == type)
	{
		//RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], CONNECT FINISH notify\n"));
		
		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_CONNECTED, &bWifiConnected);
		if(!bWifiConnected) // non-connected scan
		{
			halbtc8703b1ant_ActionWifiNotConnected(pBtCoexist);
		}
		else
		{
			halbtc8703b1ant_ActionWifiConnected(pBtCoexist);
		}
	}
}

VOID
EXhalbtc8703b1ant_MediaStatusNotify(
	IN	PBTC_COEXIST			pBtCoexist,
	IN	u1Byte				type
	)
{
	u1Byte			H2C_Parameter[3] ={0};
	u4Byte			wifiBw;
	u1Byte			wifiCentralChnl;
	BOOLEAN			bWifiUnderBMode = FALSE;

	if(pBtCoexist->bManualControl ||
		pBtCoexist->bStopCoexDm ||
		pBtCoexist->btInfo.bBtDisabled )
		return;

	if(BTC_MEDIA_CONNECT == type)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], MEDIA connect notify\n"));
		halbtc8703b1ant_PostActiveStateToBT(pBtCoexist, TRUE);
		halbtc8703b1ant_PsTdma(pBtCoexist, FORCE_EXEC, FALSE, 8);  //Force antenna setup for no scan result issue
		halbtc8703b1ant_SetAntPath(pBtCoexist, BTC_ANT_PATH_PTA, FORCE_EXEC, FALSE, FALSE);
		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_UNDER_B_MODE, &bWifiUnderBMode);

		//Set CCK Tx/Rx high Pri except 11b mode
		if (bWifiUnderBMode)
		{
			pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x6cd, 0x00); //CCK Tx
			pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x6cf, 0x00); //CCK Rx
		}
		else
		{
			//pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x6cd, 0x10); //CCK Tx
			//pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x6cf, 0x10); //CCK Rx
			pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x6cd, 0x00); //CCK Tx
			pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x6cf, 0x10); //CCK Rx
		}
		
		pCoexDm->backupArfrCnt1 = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x430);
		pCoexDm->backupArfrCnt2 = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x434);
		pCoexDm->backupRetryLimit = pBtCoexist->fBtcRead2Byte(pBtCoexist, 0x42a);
		pCoexDm->backupAmpduMaxTime = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x456);
	}
	else
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], MEDIA disconnect notify\n"));
		halbtc8703b1ant_PostActiveStateToBT(pBtCoexist, FALSE);
		pCoexDm->nArpCnt = 0;

		pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x6cd, 0x0); //CCK Tx
		pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x6cf, 0x0); //CCK Rx

		pCoexSta->bCCKEverLock = FALSE;
	}

	halbtc8703b1ant_UpdateWifiChannelInfo(pBtCoexist, type);	
	
}

VOID
EXhalbtc8703b1ant_SpecialPacketNotify(
	IN	PBTC_COEXIST			pBtCoexist,
	IN	u1Byte				type
	)
{
	BOOLEAN	bBtHsOn=FALSE;
	u4Byte	wifiLinkStatus=0;
	u4Byte	numOfWifiLink=0;
	BOOLEAN	bBtCtrlAggBufSize=FALSE, bUnder4way=FALSE;
	u1Byte	aggBufSize=5;
	
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_4_WAY_PROGRESS, &bUnder4way);

	if(pBtCoexist->bManualControl ||
		pBtCoexist->bStopCoexDm ||
		pBtCoexist->btInfo.bBtDisabled )
		return;

	if( BTC_PACKET_DHCP == type || 
		BTC_PACKET_EAPOL == type ||
		BTC_PACKET_ARP == type )
	{
		halbtc8703b1ant_PostActiveStateToBT(pBtCoexist, TRUE);
		
		if (BTC_PACKET_ARP == type)
		{
			RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], special Packet ARP notify\n"));		 

			pCoexDm->nArpCnt++;
			RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], ARP Packet Count = %d\n", pCoexDm->nArpCnt));
			
			if((pCoexDm->nArpCnt >= 10) && (!bUnder4way)) // if APR PKT > 10 after connect, do not go to ActionWifiConnectedSpecialPacket(pBtCoexist) 
			{
				pCoexSta->bWiFiIsHighPriTask = FALSE;					
			}			
			else
			{
				pCoexSta->bWiFiIsHighPriTask = TRUE;
			}
		}
		else
		{
			pCoexSta->bWiFiIsHighPriTask = TRUE;
			RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], special Packet DHCP or EAPOL notify\n"));		 
		}
	} 
	else
	{
		pCoexSta->bWiFiIsHighPriTask = FALSE;
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], special Packet [Type = %d] notify\n", type));
	}

	pCoexSta->specialPktPeriodCnt = 0;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_LINK_STATUS, &wifiLinkStatus);
	numOfWifiLink = wifiLinkStatus>>16;
	if(numOfWifiLink >= 2)
	{
		halbtc8703b1ant_LimitedTx(pBtCoexist, NORMAL_EXEC, 0, 0, 0, 0);
		halbtc8703b1ant_LimitedRx(pBtCoexist, NORMAL_EXEC, FALSE, bBtCtrlAggBufSize, aggBufSize);
		halbtc8703b1ant_ActionWifiMultiPort(pBtCoexist);
		return;
	}

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_HS_OPERATION, &bBtHsOn);
	if(pCoexSta->bC2hBtInquiryPage)
	{
		halbtc8703b1ant_ActionBtInquiry(pBtCoexist);
		return;
	}
	else if(bBtHsOn)
	{
		halbtc8703b1ant_ActionHs(pBtCoexist);
		return;
	}

	if( BTC_PACKET_DHCP == type ||
		BTC_PACKET_EAPOL == type || 
		( (BTC_PACKET_ARP == type ) && (pCoexSta->bWiFiIsHighPriTask) ) )
		{
		halbtc8703b1ant_ActionWifiConnectedSpecialPacket(pBtCoexist);
	}
}

VOID
EXhalbtc8703b1ant_BtInfoNotify(
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
	PBTC_BOARD_INFO 	pBoardInfo=&pBtCoexist->boardInfo;
	
	pCoexSta->bC2hBtInfoReqSent = FALSE;

	rspSource = tmpBuf[0]&0xf;
	if(rspSource >= BT_INFO_SRC_8703B_1ANT_MAX)
		rspSource = BT_INFO_SRC_8703B_1ANT_WIFI_FW;
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

	// if 0xff, it means BT is under WHCK test
	if (btInfo == 0xff)
		pCoexSta->bBtWhckTest = TRUE;
	else
		pCoexSta->bBtWhckTest = FALSE;

	if(BT_INFO_SRC_8703B_1ANT_WIFI_FW != rspSource)
	{
		pCoexSta->btRetryCnt =	// [3:0]
			pCoexSta->btInfoC2h[rspSource][2]&0xf;

		if (pCoexSta->btRetryCnt >= 1)
			pCoexSta->popEventCnt++;

		if (pCoexSta->btInfoC2h[rspSource][2]&0x20)
			pCoexSta->bC2hBtPage = TRUE;
		else
			pCoexSta->bC2hBtPage = FALSE;			

		pCoexSta->btRssi =
			pCoexSta->btInfoC2h[rspSource][3]*2-90;
			//pCoexSta->btInfoC2h[rspSource][3]*2+10;

		pCoexSta->btInfoExt = 
			pCoexSta->btInfoC2h[rspSource][4];
		
		pCoexSta->bBtTxRxMask = (pCoexSta->btInfoC2h[rspSource][2]&0x40);
		pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_BL_BT_TX_RX_MASK, &pCoexSta->bBtTxRxMask);

				
		// Here we need to resend some wifi info to BT
		// because bt is reset and loss of the info.
		if(pCoexSta->btInfoExt & BIT1)
		{			
			RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BT ext info bit1 check, send wifi BW&Chnl to BT!!\n"));
			pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_CONNECTED, &bWifiConnected);
			if(bWifiConnected)
			{
				halbtc8703b1ant_UpdateWifiChannelInfo(pBtCoexist, BTC_MEDIA_CONNECT);				
			}
			else
			{
				halbtc8703b1ant_UpdateWifiChannelInfo(pBtCoexist, BTC_MEDIA_DISCONNECT);					
			}
		}
				
		if(pCoexSta->btInfoExt & BIT3)
		{
			if(!pBtCoexist->bManualControl && !pBtCoexist->bStopCoexDm)
			{
				RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BT ext info bit3 check, set BT NOT to ignore Wlan active!!\n"));
				halbtc8703b1ant_IgnoreWlanAct(pBtCoexist, FORCE_EXEC, FALSE);
			}
		}
		else
		{
			// BT already NOT ignore Wlan active, do nothing here.
		}
#if(BT_AUTO_REPORT_ONLY_8703B_1ANT == 0)
		if( (pCoexSta->btInfoExt & BIT4) )
		{
			// BT auto report already enabled, do nothing
		}
		else
		{
			halbtc8703b1ant_BtAutoReport(pBtCoexist, FORCE_EXEC, TRUE);
		}
#endif
	}
		
	// check BIT2 first ==> check if bt is under inquiry or page scan
	if(btInfo & BT_INFO_8703B_1ANT_B_INQ_PAGE)
		pCoexSta->bC2hBtInquiryPage = TRUE;
	else
		pCoexSta->bC2hBtInquiryPage = FALSE;

	pCoexSta->nNumOfProfile = 0;

	// set link exist status
	if(!(btInfo&BT_INFO_8703B_1ANT_B_CONNECTION))
	{
		pCoexSta->bBtLinkExist = FALSE;
		pCoexSta->bPanExist = FALSE;
		pCoexSta->bA2dpExist = FALSE;
		pCoexSta->bHidExist = FALSE;
		pCoexSta->bScoExist = FALSE;

		pCoexSta->bBtHiPriLinkExist = FALSE;		
	}
	else	// connection exists
	{		
		pCoexSta->bBtLinkExist = TRUE;
		if(btInfo & BT_INFO_8703B_1ANT_B_FTP)
		{
			pCoexSta->bPanExist = TRUE;
			pCoexSta->nNumOfProfile++;
		}
		else
			pCoexSta->bPanExist = FALSE;
		if(btInfo & BT_INFO_8703B_1ANT_B_A2DP)
		{
			pCoexSta->bA2dpExist = TRUE;
			pCoexSta->nNumOfProfile++;
		}
		else
			pCoexSta->bA2dpExist = FALSE;
		if(btInfo & BT_INFO_8703B_1ANT_B_HID)
		{
			pCoexSta->bHidExist = TRUE;
			pCoexSta->nNumOfProfile++;
		}
		else
			pCoexSta->bHidExist = FALSE;
		if(btInfo & BT_INFO_8703B_1ANT_B_SCO_ESCO)
		{
			pCoexSta->bScoExist = TRUE;
			pCoexSta->nNumOfProfile++;
		}
		else
			pCoexSta->bScoExist = FALSE;

		if ((pCoexSta->bHidExist == FALSE) && (pCoexSta->bC2hBtInquiryPage == FALSE) &&( pCoexSta->bScoExist == FALSE))
		{
			if (pCoexSta->highPriorityTx  + pCoexSta->highPriorityRx >= 160) 		
			{
				pCoexSta->bHidExist = TRUE;
				pCoexSta->wrongProfileNotification++;
				pCoexSta->nNumOfProfile++;
				btInfo = btInfo | 0x28;
				RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BtInfoNotify(), BT HID = true (Hi-Pri > 160)!\n"));
			}
		}

		//Add Hi-Pri Tx/Rx counter to avoid false detection
		if (((pCoexSta->bHidExist) || (pCoexSta->bScoExist)) && (pCoexSta->highPriorityTx + pCoexSta->highPriorityRx >= 160)
			 && (!pCoexSta->bC2hBtInquiryPage))
			pCoexSta->bBtHiPriLinkExist = TRUE;
		else
			pCoexSta->bBtHiPriLinkExist = FALSE;
			
		if((btInfo&BT_INFO_8703B_1ANT_B_ACL_BUSY) && (pCoexSta->nNumOfProfile == 0))
		{
			if (pCoexSta->lowPriorityTx + pCoexSta->lowPriorityRx >= 160)
			{
				pCoexSta->bPanExist = TRUE;
				pCoexSta->nNumOfProfile++;
				pCoexSta->wrongProfileNotification++;
				btInfo = btInfo | 0x88;				
			}
		}
	}

	halbtc8703b1ant_UpdateBtLinkInfo(pBtCoexist);
	
	btInfo = btInfo & 0x1f;  //mask profile bit for connect-ilde identification ( for CSR case: A2DP idle --> 0x41)
	
	if(!(btInfo&BT_INFO_8703B_1ANT_B_CONNECTION))
	{
		pCoexDm->btStatus = BT_8703B_1ANT_BT_STATUS_NON_CONNECTED_IDLE;
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BtInfoNotify(), BT Non-Connected idle!!!\n"));
	}
	else if(btInfo == BT_INFO_8703B_1ANT_B_CONNECTION)	// connection exists but no busy
	{
		pCoexDm->btStatus = BT_8703B_1ANT_BT_STATUS_CONNECTED_IDLE;
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BtInfoNotify(), BT Connected-idle!!!\n"));
	}
	else if((btInfo&BT_INFO_8703B_1ANT_B_SCO_ESCO) ||
		(btInfo&BT_INFO_8703B_1ANT_B_SCO_BUSY))
	{
		pCoexDm->btStatus = BT_8703B_1ANT_BT_STATUS_SCO_BUSY;
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BtInfoNotify(), BT SCO busy!!!\n"));
	}
	else if(btInfo&BT_INFO_8703B_1ANT_B_ACL_BUSY)
	{
		if(BT_8703B_1ANT_BT_STATUS_ACL_BUSY != pCoexDm->btStatus)
			pCoexDm->bAutoTdmaAdjust = FALSE;
		pCoexDm->btStatus = BT_8703B_1ANT_BT_STATUS_ACL_BUSY;
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BtInfoNotify(), BT ACL busy!!!\n"));
	}
	else
	{
		pCoexDm->btStatus = BT_8703B_1ANT_BT_STATUS_MAX;
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BtInfoNotify(), BT Non-Defined state!!!\n"));
	}

	if( (BT_8703B_1ANT_BT_STATUS_ACL_BUSY == pCoexDm->btStatus) ||
		(BT_8703B_1ANT_BT_STATUS_SCO_BUSY == pCoexDm->btStatus) ||
		(BT_8703B_1ANT_BT_STATUS_ACL_SCO_BUSY == pCoexDm->btStatus) )
		bBtBusy = TRUE;
	else
		bBtBusy = FALSE;
	pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_BL_BT_TRAFFIC_BUSY, &bBtBusy);

	halbtc8703b1ant_RunCoexistMechanism(pBtCoexist);
}

VOID
EXhalbtc8703b1ant_RfStatusNotify(
	IN	PBTC_COEXIST			pBtCoexist,
	IN	u1Byte					type
	)
{
	u4Byte	u4Tmp;
	u1Byte	u1Tmpa,u1Tmpb, u1Tmpc;
	
	RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], RF Status notify\n"));

	if(BTC_RF_ON == type)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], RF is turned ON!!\n"));
		halbtc8703b1ant_PostActiveStateToBT(pBtCoexist, TRUE);
		halbtc8703b1ant_InitHwConfig(pBtCoexist, FALSE, FALSE);
		pBtCoexist->bStopCoexDm = FALSE;
	}
	else if(BTC_RF_OFF == type)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], RF is turned OFF!!\n"));
		halbtc8703b1ant_PostActiveStateToBT(pBtCoexist, FALSE);
		
		halbtc8703b1ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);
		halbtc8703b1ant_PsTdma(pBtCoexist, FORCE_EXEC, FALSE, 0);
		halbtc8703b1ant_SetAntPath(pBtCoexist, BTC_ANT_PATH_BT, FORCE_EXEC, FALSE, TRUE);		
		halbtc8703b1ant_IgnoreWlanAct(pBtCoexist, FORCE_EXEC, TRUE);
		pBtCoexist->bStopCoexDm = TRUE;

	}
}

VOID
EXhalbtc8703b1ant_HaltNotify(
	IN	PBTC_COEXIST			pBtCoexist
	)
{
	u4Byte	u4Tmp;
	
	RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Halt notify\n"));

	halbtc8703b1ant_PostActiveStateToBT(pBtCoexist, FALSE);

	halbtc8703b1ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);
	halbtc8703b1ant_PsTdma(pBtCoexist, FORCE_EXEC, FALSE, 0);
	halbtc8703b1ant_SetAntPath(pBtCoexist, BTC_ANT_PATH_BT, FORCE_EXEC, FALSE, TRUE);	

	halbtc8703b1ant_IgnoreWlanAct(pBtCoexist, FORCE_EXEC, TRUE);

	EXhalbtc8703b1ant_MediaStatusNotify(pBtCoexist, BTC_MEDIA_DISCONNECT);

	pBtCoexist->bStopCoexDm = TRUE;	
}

VOID
EXhalbtc8703b1ant_PnpNotify(
	IN	PBTC_COEXIST			pBtCoexist,
	IN	u1Byte				pnpState
	)
{
	RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Pnp notify\n"));

	if(BTC_WIFI_PNP_SLEEP == pnpState)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Pnp notify to SLEEP\n"));

		halbtc8703b1ant_PostActiveStateToBT(pBtCoexist, FALSE);

		halbtc8703b1ant_PowerSaveState(pBtCoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);
		halbtc8703b1ant_PsTdma(pBtCoexist, NORMAL_EXEC, FALSE, 0);
		halbtc8703b1ant_SetAntPath(pBtCoexist, BTC_ANT_PATH_BT, FORCE_EXEC, FALSE, TRUE);
		halbtc8703b1ant_CoexTableWithType(pBtCoexist, NORMAL_EXEC, 2);		

		pBtCoexist->bStopCoexDm = TRUE;
	}
	else if(BTC_WIFI_PNP_WAKE_UP == pnpState)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Pnp notify to WAKE UP\n"));
		halbtc8703b1ant_PostActiveStateToBT(pBtCoexist, TRUE);
		
		pBtCoexist->bStopCoexDm = FALSE;
		halbtc8703b1ant_InitHwConfig(pBtCoexist, FALSE, FALSE);
		halbtc8703b1ant_InitCoexDm(pBtCoexist);
		halbtc8703b1ant_QueryBtInfo(pBtCoexist);
	}
}


VOID
EXhalbtc8703b1ant_ScoreBoardStatusNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	pu1Byte			tmpBuf,
	IN	u1Byte			length
	)
{
	//


}

VOID
EXhalbtc8703b1ant_CoexDmReset(
	IN	PBTC_COEXIST			pBtCoexist
	)
{
	RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], *****************Coex DM Reset*****************\n"));

	halbtc8703b1ant_InitHwConfig(pBtCoexist, FALSE, FALSE);
	//pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);
	//pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x2, 0xfffff, 0x0);
	halbtc8703b1ant_InitCoexDm(pBtCoexist);
}

VOID
EXhalbtc8703b1ant_Periodical(
	IN	PBTC_COEXIST			pBtCoexist
	)
{
	static u1Byte		disVerInfoCnt=0;
	u4Byte				fwVer=0, btPatchVer=0;
	PBTC_BOARD_INFO		pBoardInfo=&pBtCoexist->boardInfo;
	PBTC_STACK_INFO		pStackInfo=&pBtCoexist->stackInfo;

	RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], ==========================Periodical===========================\n"));

	if(disVerInfoCnt <= 5)
	{
		disVerInfoCnt += 1;
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], ****************************************************************\n"));
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Ant PG Num/ Ant Mech/ Ant Pos = %d/ %d/ %d\n", 
			pBoardInfo->pgAntNum, pBoardInfo->btdmAntNum, pBoardInfo->btdmAntPos));
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BT stack/ hci ext ver = %s / %d\n", 
			((pStackInfo->bProfileNotified)? "Yes":"No"), pStackInfo->hciVersion));
		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_BT_PATCH_VER, &btPatchVer);
		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_FW_VER, &fwVer);
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], CoexVer/ FwVer/ PatchVer = %d_%x/ 0x%x/ 0x%x(%d)\n",
			GLCoexVerDate8703b1Ant, GLCoexVer8703b1Ant, fwVer, btPatchVer, btPatchVer));
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], ****************************************************************\n"));
	}

#if(BT_AUTO_REPORT_ONLY_8703B_1ANT == 0)
	halbtc8703b1ant_QueryBtInfo(pBtCoexist);
	halbtc8703b1ant_MonitorBtEnableDisable(pBtCoexist);
#else
	halbtc8703b1ant_MonitorBtCtr(pBtCoexist);
	halbtc8703b1ant_MonitorWiFiCtr(pBtCoexist);
#if BT_8703B_1ANT_ANTDET_ENABLE
	halbtc8703b1ant_MonitorBtEnableDisable(pBtCoexist);
#endif

	if( halbtc8703b1ant_IsWifiStatusChanged(pBtCoexist) ||
		pCoexDm->bAutoTdmaAdjust )
	{

		halbtc8703b1ant_RunCoexistMechanism(pBtCoexist);	
	}

	pCoexSta->specialPktPeriodCnt++;

#endif
}

VOID
EXhalbtc8703b1ant_AntennaDetection(
	IN	PBTC_COEXIST			pBtCoexist,
	IN	u4Byte					centFreq,
	IN	u4Byte					offset,
	IN	u4Byte					span,
	IN	u4Byte					seconds
	)
{
	  //No Antenna Detection required because 8730b is only 1-Ant
}

VOID
EXhalbtc8703b1ant_AntennaIsolation(
	IN	PBTC_COEXIST			pBtCoexist,
	IN	u4Byte					centFreq,
	IN	u4Byte					offset,
	IN	u4Byte					span,
	IN	u4Byte					seconds
	)
{


}

VOID
EXhalbtc8703b1ant_PSDScan(
	IN	PBTC_COEXIST			pBtCoexist,
	IN	u4Byte					centFreq,
	IN	u4Byte					offset,
	IN	u4Byte					span,
	IN	u4Byte					seconds
	)
{


}

VOID
EXhalbtc8703b1ant_DisplayAntDetection(
	IN	PBTC_COEXIST			pBtCoexist
	)
{

}

#endif

#else	// #if (RTL8723B_SUPPORT == 1)
VOID
EXhalbtc8703b1ant_PowerOnSetting(
	IN	PBTC_COEXIST		pBtCoexist
	){}
VOID
EXhalbtc8703b1ant_PreLoadFirmware(
	IN	PBTC_COEXIST		pBtCoexist
	){}
VOID
EXhalbtc8703b1ant_InitHwConfig(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN				bWifiOnly
	){}
VOID
EXhalbtc8703b1ant_InitCoexDm(
	IN	PBTC_COEXIST		pBtCoexist
	){}
VOID
EXhalbtc8703b1ant_IpsNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			type
	){}
VOID
EXhalbtc8703b1ant_LpsNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			type
	){}
VOID
EXhalbtc8703b1ant_ScanNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			type
	){}
VOID
EXhalbtc8703b1ant_ConnectNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			type
	){}
VOID
EXhalbtc8703b1ant_MediaStatusNotify(
	IN	PBTC_COEXIST			pBtCoexist,
	IN	u1Byte				type
	){}
VOID
EXhalbtc8703b1ant_SpecialPacketNotify(
	IN	PBTC_COEXIST			pBtCoexist,
	IN	u1Byte				type
	){}
VOID
EXhalbtc8703b1ant_BtInfoNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	pu1Byte			tmpBuf,
	IN	u1Byte			length
	){}
VOID
EXhalbtc8703b1ant_RfStatusNotify(
	IN	PBTC_COEXIST			pBtCoexist,
	IN	u1Byte					type
	){}
VOID
EXhalbtc8703b1ant_HaltNotify(
	IN	PBTC_COEXIST			pBtCoexist
	){}
VOID
EXhalbtc8703b1ant_PnpNotify(
	IN	PBTC_COEXIST			pBtCoexist,
	IN	u1Byte				pnpState
	){}
VOID
EXhalbtc8703b1ant_ScoreBoardStatusNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	pu1Byte			tmpBuf,
	IN	u1Byte			length
	){}
VOID
EXhalbtc8703b1ant_CoexDmReset(
	IN	PBTC_COEXIST			pBtCoexist
	){}
VOID
EXhalbtc8703b1ant_Periodical(
	IN	PBTC_COEXIST			pBtCoexist
	){}
VOID
EXhalbtc8703b1ant_DisplayCoexInfo(
	IN	PBTC_COEXIST		pBtCoexist
	){}
VOID
EXhalbtc8703b1ant_AntennaDetection(
	IN	PBTC_COEXIST			pBtCoexist,
	IN	u4Byte					centFreq,
	IN	u4Byte					offset,
	IN	u4Byte					span,
	IN	u4Byte					seconds
	){}
VOID
EXhalbtc8703b1ant_DisplayAntDetection(
	IN	PBTC_COEXIST			pBtCoexist
	){}

#endif
