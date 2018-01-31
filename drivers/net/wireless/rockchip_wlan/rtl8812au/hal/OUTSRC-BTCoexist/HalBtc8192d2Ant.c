/* SPDX-License-Identifier: GPL-2.0 */
//============================================================
// Description:
//
// This file is for 92D BT 2 Antenna Co-exist mechanism
//
// By cosa 02/11/2011
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
static COEX_DM_8192D_2ANT	GLCoexDm8192d2Ant;
static PCOEX_DM_8192D_2ANT 	pCoexDm=&GLCoexDm8192d2Ant;
static COEX_STA_8192D_2ANT	GLCoexSta8192d2Ant;
static PCOEX_STA_8192D_2ANT	pCoexSta=&GLCoexSta8192d2Ant;

//============================================================
// local function start with btdm_
//============================================================
u1Byte
halbtc8192d2ant_WifiRssiState(
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
			if(wifiRssi >= (rssiThresh+BTC_RSSI_COEX_THRESH_TOL_8192D_2ANT))
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
			if(wifiRssi >= (rssiThresh+BTC_RSSI_COEX_THRESH_TOL_8192D_2ANT))
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
			if(wifiRssi >= (rssiThresh1+BTC_RSSI_COEX_THRESH_TOL_8192D_2ANT))
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

u1Byte
halbtc8192d2ant_ActionAlgorithm(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	PBTC_STACK_INFO		pStackInfo=&pBtCoexist->stackInfo;
	BOOLEAN				bBtHsOn=FALSE;
	u1Byte				algorithm=BT_8192D_2ANT_COEX_ALGO_UNDEFINED;
	u1Byte				numOfDiffProfile=0;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_HS_OPERATION, &bBtHsOn);
	
	if(!pStackInfo->bBtLinkExist)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], No profile exists!!!\n"));
		return algorithm;
	}

	if(pStackInfo->bScoExist)
		numOfDiffProfile++;
	if(pStackInfo->bHidExist)
		numOfDiffProfile++;
	if(pStackInfo->bPanExist)
		numOfDiffProfile++;
	if(pStackInfo->bA2dpExist)
		numOfDiffProfile++;

	if(pStackInfo->bScoExist)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], SCO algorithm\n"));
		algorithm = BT_8192D_2ANT_COEX_ALGO_SCO;
	}
	else
	{
		if(numOfDiffProfile == 1)
		{
			if(pStackInfo->bHidExist)
			{
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], HID only\n"));
				algorithm = BT_8192D_2ANT_COEX_ALGO_HID;
			}
			else if(pStackInfo->bA2dpExist)
			{
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], A2DP only\n"));
				algorithm = BT_8192D_2ANT_COEX_ALGO_A2DP;
			}
			else if(pStackInfo->bPanExist)
			{
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], PAN only\n"));
				algorithm = BT_8192D_2ANT_COEX_ALGO_PAN;
			}
		}
		else
		{
			if( pStackInfo->bHidExist &&
				pStackInfo->bA2dpExist )
			{
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], HID + A2DP\n"));
				algorithm = BT_8192D_2ANT_COEX_ALGO_HID_A2DP;
			}
			else if( pStackInfo->bHidExist &&
				pStackInfo->bPanExist )
			{				
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], HID + PAN\n"));
				algorithm = BT_8192D_2ANT_COEX_ALGO_HID_PAN;
			}
			else if( pStackInfo->bPanExist &&
				pStackInfo->bA2dpExist )
			{				
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], PAN + A2DP\n"));
				algorithm = BT_8192D_2ANT_COEX_ALGO_PAN_A2DP;
			}
		}		
	}
	return algorithm;
}

VOID
halbtc8192d2ant_SetFwBalance(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bBalanceOn,
	IN	u1Byte			ms0,
	IN	u1Byte			ms1
	)
{
	u1Byte	H2C_Parameter[3] ={0};

	if(bBalanceOn)
	{
		H2C_Parameter[2] = 1;
		H2C_Parameter[1] = ms1;
		H2C_Parameter[0] = ms0;
	}
	else
	{
		H2C_Parameter[2] = 0;
		H2C_Parameter[1] = 0;
		H2C_Parameter[0] = 0;
	}
	
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_EXEC, ("[BTCoex], Balance=[%s:%dms:%dms], write 0xc=0x%x\n", 
		bBalanceOn?"ON":"OFF", ms0, ms1,
		H2C_Parameter[0]<<16|H2C_Parameter[1]<<8|H2C_Parameter[2]));

	pBtCoexist->fBtcFillH2c(pBtCoexist, 0xc, 3, H2C_Parameter);	
}

VOID
halbtc8192d2ant_Balance(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bForceExec,
	IN	BOOLEAN			bBalanceOn,
	IN	u1Byte			ms0,
	IN	u1Byte			ms1
	)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW, ("[BTCoex], %s turn Balance %s\n", 
		(bForceExec? "force to":""), (bBalanceOn? "ON":"OFF")));
	pCoexDm->bCurBalanceOn = bBalanceOn;

	if(!bForceExec)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], bPreBalanceOn = %d, bCurBalanceOn = %d!!\n", 
			pCoexDm->bPreBalanceOn, pCoexDm->bCurBalanceOn));

		if(pCoexDm->bPreBalanceOn == pCoexDm->bCurBalanceOn)
			return;
	}
	halbtc8192d2ant_SetFwBalance(pBtCoexist, bBalanceOn, ms0, ms1);

	pCoexDm->bPreBalanceOn = pCoexDm->bCurBalanceOn;
}

VOID
halbtc8192d2ant_SetFwDiminishWifi(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN 		bDacOn,
	IN	BOOLEAN 		bInterruptOn,
	IN	u1Byte			fwDacSwingLvl,
	IN	BOOLEAN 		bNavOn
	)
{
	u1Byte			H2C_Parameter[3] ={0};

	if((pBtCoexist->stackInfo.minBtRssi <= -5) && (fwDacSwingLvl == 0x20))
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_EXEC, ("[BTCoex], DiminishWiFi 0x20 original, but set 0x18 for Low RSSI!\n"));
		fwDacSwingLvl = 0x18;
	}

	H2C_Parameter[2] = 0;
	H2C_Parameter[1] = fwDacSwingLvl;
	H2C_Parameter[0] = 0;
	if(bDacOn)
	{
		H2C_Parameter[2] |= 0x01;	//BIT0
		if(bInterruptOn)
		{
			H2C_Parameter[2] |= 0x02;	//BIT1
		}
	}
	if(bNavOn)
	{
		H2C_Parameter[2] |= 0x08;	//BIT3
	}
		
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_EXEC, ("[BTCoex], bDacOn=%s, bInterruptOn=%s, bNavOn=%s, write 0x12=0x%x\n", 
		(bDacOn?"ON":"OFF"), (bInterruptOn?"ON":"OFF"), (bNavOn?"ON":"OFF"),
		(H2C_Parameter[0]<<16|H2C_Parameter[1]<<8|H2C_Parameter[2])));		
	pBtCoexist->fBtcFillH2c(pBtCoexist, 0x12, 3, H2C_Parameter);
}


VOID
halbtc8192d2ant_DiminishWifi(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bForceExec,
	IN	BOOLEAN			bDacOn,
	IN	BOOLEAN			bInterruptOn,
	IN	u1Byte			fwDacSwingLvl,
	IN	BOOLEAN			bNavOn
	)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW, ("[BTCoex], %s set Diminish Wifi, bDacOn=%s, bInterruptOn=%s, fwDacSwingLvl=%d, bNavOn=%s\n", 
		(bForceExec? "force to":""), (bDacOn? "ON":"OFF"), (bInterruptOn? "ON":"OFF"), fwDacSwingLvl, (bNavOn? "ON":"OFF")));

	pCoexDm->bCurDacOn = bDacOn;
	pCoexDm->bCurInterruptOn = bInterruptOn;
	pCoexDm->curFwDacSwingLvl = fwDacSwingLvl;
	pCoexDm->bCurNavOn = bNavOn;

	if(!bForceExec)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], bPreDacOn=%d, bCurDacOn=%d!!\n", 
			pCoexDm->bPreDacOn, pCoexDm->bCurDacOn));
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], bPreInterruptOn=%d, bCurInterruptOn=%d!!\n", 
			pCoexDm->bPreInterruptOn, pCoexDm->bCurInterruptOn));
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], preFwDacSwingLvl=%d, curFwDacSwingLvl=%d!!\n", 
			pCoexDm->preFwDacSwingLvl, pCoexDm->curFwDacSwingLvl));
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], bPreNavOn=%d, bCurNavOn=%d!!\n", 
			pCoexDm->bPreNavOn, pCoexDm->bCurNavOn));


		if( (pCoexDm->bPreDacOn==pCoexDm->bCurDacOn) &&
			(pCoexDm->bPreInterruptOn==pCoexDm->bCurInterruptOn) &&
			(pCoexDm->preFwDacSwingLvl==pCoexDm->curFwDacSwingLvl) &&
			(pCoexDm->bPreNavOn==pCoexDm->bCurNavOn) )
			return;
	}
	halbtc8192d2ant_SetFwDiminishWifi(pBtCoexist, bDacOn, bInterruptOn, fwDacSwingLvl, bNavOn);

	pCoexDm->bPreDacOn = pCoexDm->bCurDacOn;
	pCoexDm->bPreInterruptOn = pCoexDm->bCurInterruptOn;
	pCoexDm->preFwDacSwingLvl = pCoexDm->curFwDacSwingLvl;
	pCoexDm->bPreNavOn = pCoexDm->bCurNavOn;
}

VOID
halbtc8192d2ant_SetSwRfRxLpfCorner(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bRxRfShrinkOn
	)
{
	if(bRxRfShrinkOn)
	{
		//Shrink RF Rx LPF corner
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC, ("[BTCoex], Shrink RF Rx LPF corner!!\n"));
		pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1e, 0xfffff, 0xf2ff7);
	}
	else
	{
		//Resume RF Rx LPF corner
		// After initialized, we can use pCoexDm->btRf0x1eBackup
		if(pBtCoexist->bInitilized)
		{
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC, ("[BTCoex], Resume RF Rx LPF corner!!\n"));
			pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1e, 0xfffff, pCoexDm->btRf0x1eBackup);
		}
	}
}


VOID
halbtc8192d2ant_RfShrink(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bForceExec,
	IN	BOOLEAN			bRxRfShrinkOn
	)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW, ("[BTCoex], %s turn Rx RF Shrink = %s\n",  
		(bForceExec? "force to":""), ((bRxRfShrinkOn)? "ON":"OFF")));
	pCoexDm->bCurRfRxLpfShrink = bRxRfShrinkOn;

	if(!bForceExec)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_DETAIL, ("[BTCoex], bPreRfRxLpfShrink=%d, bCurRfRxLpfShrink=%d\n", 
			pCoexDm->bPreRfRxLpfShrink, pCoexDm->bCurRfRxLpfShrink));

		if(pCoexDm->bPreRfRxLpfShrink == pCoexDm->bCurRfRxLpfShrink) 
			return;
	}
	halbtc8192d2ant_SetSwRfRxLpfCorner(pBtCoexist, pCoexDm->bCurRfRxLpfShrink);

	pCoexDm->bPreRfRxLpfShrink = pCoexDm->bCurRfRxLpfShrink;
}

VOID
halbtc8192d2ant_SetSwPenaltyTxRateAdaptive(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bLowPenaltyRa
	)
{
	u1Byte	tmpU1;

	tmpU1 = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x4fd);
	if(bLowPenaltyRa)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC, ("[BTCoex], Tx rate adaptive, set low penalty!!\n"));
		tmpU1 &= ~BIT2;
	}
	else
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC, ("[BTCoex], Tx rate adaptive, set normal!!\n"));
		tmpU1 |= BIT2;
	}
	pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x4fd, tmpU1);
}

VOID
halbtc8192d2ant_LowPenaltyRa(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bForceExec,
	IN	BOOLEAN			bLowPenaltyRa
	)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW, ("[BTCoex], %s turn LowPenaltyRA = %s\n",  
		(bForceExec? "force to":""), ((bLowPenaltyRa)? "ON":"OFF")));
	pCoexDm->bCurLowPenaltyRa = bLowPenaltyRa;

	if(!bForceExec)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_DETAIL, ("[BTCoex], bPreLowPenaltyRa=%d, bCurLowPenaltyRa=%d\n", 
			pCoexDm->bPreLowPenaltyRa, pCoexDm->bCurLowPenaltyRa));

		if(pCoexDm->bPreLowPenaltyRa == pCoexDm->bCurLowPenaltyRa) 
			return;
	}
	halbtc8192d2ant_SetSwPenaltyTxRateAdaptive(pBtCoexist, pCoexDm->bCurLowPenaltyRa);

	pCoexDm->bPreLowPenaltyRa = pCoexDm->bCurLowPenaltyRa;
}

VOID
halbtc8192d2ant_SetSwFullTimeDacSwing(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bSwDacSwingOn,
	IN	u4Byte			swDacSwingLvl
	)
{
	u4Byte	dacSwingLvl;

	if(bSwDacSwingOn)
	{		
		if((pBtCoexist->stackInfo.minBtRssi <= -5) && (swDacSwingLvl == 0x20))
		{
			dacSwingLvl = 0x18;
		}
		else
		{
			dacSwingLvl = swDacSwingLvl;
		}
		pBtCoexist->fBtcSetBbReg(pBtCoexist, 0x880, 0xfc000000, dacSwingLvl);
	}
	else
	{
		pBtCoexist->fBtcSetBbReg(pBtCoexist, 0x880, 0xfc000000, 0x30);
	}
}

VOID
halbtc8192d2ant_DacSwing(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bForceExec,
	IN	BOOLEAN			bDacSwingOn,
	IN	u4Byte			dacSwingLvl
	)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW, ("[BTCoex], %s turn DacSwing=%s, dacSwingLvl=0x%x\n",  
		(bForceExec? "force to":""), ((bDacSwingOn)? "ON":"OFF"), dacSwingLvl));
	pCoexDm->bCurDacSwingOn = bDacSwingOn;
	pCoexDm->curDacSwingLvl = dacSwingLvl;

	if(!bForceExec)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_DETAIL, ("[BTCoex], bPreDacSwingOn=%d, preDacSwingLvl=0x%x, bCurDacSwingOn=%d, curDacSwingLvl=0x%x\n", 
			pCoexDm->bPreDacSwingOn, pCoexDm->preDacSwingLvl,
			pCoexDm->bCurDacSwingOn, pCoexDm->curDacSwingLvl));

		if( (pCoexDm->bPreDacSwingOn == pCoexDm->bCurDacSwingOn) &&
			(pCoexDm->preDacSwingLvl == pCoexDm->curDacSwingLvl) )
			return;
	}
	delay_ms(30);
	halbtc8192d2ant_SetSwFullTimeDacSwing(pBtCoexist, bDacSwingOn, dacSwingLvl);

	pCoexDm->bPreDacSwingOn = pCoexDm->bCurDacSwingOn;
	pCoexDm->preDacSwingLvl = pCoexDm->curDacSwingLvl;
}

VOID
halbtc8192d2ant_SetAdcBackOff(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bAdcBackOff
	)
{
	if(bAdcBackOff)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC, ("[BTCoex], BB BackOff Level On!\n"));
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc04,0x3a07611);
	}
	else
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC, ("[BTCoex], BB BackOff Level Off!\n"));		
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc04,0x3a05611);
	}
}

VOID
halbtc8192d2ant_AdcBackOff(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bForceExec,
	IN	BOOLEAN			bAdcBackOff
	)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW, ("[BTCoex], %s turn AdcBackOff = %s\n",  
		(bForceExec? "force to":""), ((bAdcBackOff)? "ON":"OFF")));
	pCoexDm->bCurAdcBackOff = bAdcBackOff;

	if(!bForceExec)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_DETAIL, ("[BTCoex], bPreAdcBackOff=%d, bCurAdcBackOff=%d\n", 
			pCoexDm->bPreAdcBackOff, pCoexDm->bCurAdcBackOff));

		if(pCoexDm->bPreAdcBackOff == pCoexDm->bCurAdcBackOff) 
			return;
	}
	halbtc8192d2ant_SetAdcBackOff(pBtCoexist, pCoexDm->bCurAdcBackOff);

	pCoexDm->bPreAdcBackOff = pCoexDm->bCurAdcBackOff;
}

VOID
halbtc8192d2ant_SetAgcTable(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bAgcTableEn
	)
{
	u1Byte		rssiAdjustVal=0;

	if(bAgcTableEn)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC, ("[BTCoex], Agc Table On!\n"));
		pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1a, 0xfffff, 0xa99);
		pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x12, 0xfffff, 0xd4000);
		
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x7b000001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x7b010001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x7b020001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x7b030001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x7b040001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x7b050001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x7b060001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x7b070001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x7b080001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x7b090001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x7b0A0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x7b0B0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x7a0C0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x790D0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x780E0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x770F0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x76100001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x75110001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x74120001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x73130001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x72140001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x71150001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x70160001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x6f170001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x6e180001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x6d190001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x6c1A0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x6b1B0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x6a1C0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x691D0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x4f1E0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x4e1F0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x4d200001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x4c210001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x4b220001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x4a230001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x49240001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x48250001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x47260001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x46270001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x45280001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x44290001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x432A0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x422B0001);

		rssiAdjustVal = 12;
	}
	else
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC, ("[BTCoex], Agc Table Off!\n"));
		pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1a, 0xfffff, 0x30a99);
		pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x12, 0xfffff, 0xdc000);

		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x7B000001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x7B010001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x7B020001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x7B030001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x7B040001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x7B050001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x7B060001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x7A070001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x79080001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x78090001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x770A0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x760B0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x750C0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x740D0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x730E0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x720F0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x71100001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x70110001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x6F120001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x6E130001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x6D140001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x6C150001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x6B160001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x6A170001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x69180001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x68190001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x671A0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x661B0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x651C0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x641D0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x631E0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x621F0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x61200001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x60210001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x49220001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x48230001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x47240001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x46250001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x45260001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x44270001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x43280001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x42290001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x412A0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78, 0x402B0001);
	}

	// set rssiAdjustVal for wifi module.
	pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_U1_RSSI_ADJ_VAL_FOR_AGC_TABLE_ON, &rssiAdjustVal);
}



VOID
halbtc8192d2ant_AgcTable(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bForceExec,
	IN	BOOLEAN			bAgcTableEn
	)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW, ("[BTCoex], %s %s Agc Table\n",  
		(bForceExec? "force to":""), ((bAgcTableEn)? "Enable":"Disable")));
	pCoexDm->bCurAgcTableEn = bAgcTableEn;

	if(!bForceExec)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_DETAIL, ("[BTCoex], bPreAgcTableEn=%d, bCurAgcTableEn=%d\n", 
			pCoexDm->bPreAgcTableEn, pCoexDm->bCurAgcTableEn));

		if(pCoexDm->bPreAgcTableEn == pCoexDm->bCurAgcTableEn) 
			return;
	}
	halbtc8192d2ant_SetAgcTable(pBtCoexist, bAgcTableEn);

	pCoexDm->bPreAgcTableEn = pCoexDm->bCurAgcTableEn;
}

VOID
halbtc8192d2ant_SetCoexTable(
	IN	PBTC_COEXIST	pBtCoexist,
	IN	u4Byte		val0x6c4,
	IN	u4Byte		val0x6c8,
	IN	u4Byte		val0x6cc
	)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC, ("[BTCoex], set coex table, set 0x6c4=0x%x\n", val0x6c4));
	pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x6c4, val0x6c4);

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC, ("[BTCoex], set coex table, set 0x6c8=0x%x\n", val0x6c8));
	pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x6c8, val0x6c8);

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC, ("[BTCoex], set coex table, set 0x6cc=0x%x\n", val0x6cc));
	pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x6cc, val0x6cc);
}

VOID
halbtc8192d2ant_CoexTable(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bForceExec,
	IN	u4Byte			val0x6c4,
	IN	u4Byte			val0x6c8,
	IN	u4Byte			val0x6cc
	)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW, ("[BTCoex], %s write Coex Table 0x6c4=0x%x, 0x6c8=0x%x, 0x6cc=0x%x\n", 
		(bForceExec? "force to":""), val0x6c4, val0x6c8, val0x6cc));
	pCoexDm->curVal0x6c4 = val0x6c4;
	pCoexDm->curVal0x6c8 = val0x6c8;
	pCoexDm->curVal0x6cc = val0x6cc;

	if(!bForceExec)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_DETAIL, ("[BTCoex], preVal0x6c4=0x%x, preVal0x6c8=0x%x, preVal0x6cc=0x%x !!\n", 
			pCoexDm->preVal0x6c4, pCoexDm->preVal0x6c8, pCoexDm->preVal0x6cc));
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_DETAIL, ("[BTCoex], curVal0x6c4=0x%x, curVal0x6c8=0x%x, curVal0x6cc=0x%x !!\n", 
			pCoexDm->curVal0x6c4, pCoexDm->curVal0x6c8, pCoexDm->curVal0x6cc));
	
		if( (pCoexDm->preVal0x6c4 == pCoexDm->curVal0x6c4) &&
			(pCoexDm->preVal0x6c8 == pCoexDm->curVal0x6c8) &&
			(pCoexDm->preVal0x6cc == pCoexDm->curVal0x6cc) )
			return;
	}
	halbtc8192d2ant_SetCoexTable(pBtCoexist, val0x6c4, val0x6c8, val0x6cc);

	pCoexDm->preVal0x6c4 = pCoexDm->curVal0x6c4;
	pCoexDm->preVal0x6c8 = pCoexDm->curVal0x6c8;
	pCoexDm->preVal0x6cc = pCoexDm->curVal0x6cc;
}

VOID
halbtc8192d2ant_CoexAllOff(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	// fw mechanism
	halbtc8192d2ant_Balance(pBtCoexist, NORMAL_EXEC, FALSE, 0, 0);
	halbtc8192d2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0, FALSE);

	// sw mechanism
	halbtc8192d2ant_AgcTable(pBtCoexist, NORMAL_EXEC, FALSE);
	halbtc8192d2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, FALSE);
	halbtc8192d2ant_DacSwing(pBtCoexist, NORMAL_EXEC, FALSE, 0x30);
}
VOID
halbtc8192d2ant_InitCoexDm(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
}

VOID
halbtc8192d2ant_MonitorBtEnableDisable(
	IN 	PBTC_COEXIST		pBtCoexist,
	IN	u4Byte			btActive
	)
{
	static BOOLEAN	bPreBtDisabled=FALSE;
	static u4Byte		btDisableCnt=0;
	BOOLEAN			bBtDisabled=FALSE, bForceToRoam=FALSE;
	u4Byte			u4Tmp=0;

	// This function check if bt is disabled
	if(btActive)
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
		}
	}
	if(bPreBtDisabled != bBtDisabled)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_MONITOR, ("[BTCoex], BT is from %s to %s!!\n", 
			(bPreBtDisabled ? "disabled":"enabled"), 
			(bBtDisabled ? "disabled":"enabled")));

		bForceToRoam = TRUE;
		pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_BL_FORCE_TO_ROAM, &bForceToRoam);

		bPreBtDisabled = bBtDisabled;
	}
}

VOID
halbtc8192d2ant_MonitorBtState(
	IN	PBTC_COEXIST			pBtCoexist
	)
{
	BOOLEAN 		stateChange=FALSE;
	u4Byte			BT_Polling, Ratio_Act, Ratio_STA;
	u4Byte			BT_Active, BT_State;
	u4Byte			regBTActive=0, regBTState=0, regBTPolling=0;
	u4Byte			btBusyThresh=0;
	u4Byte			fwVer=0;
	static BOOLEAN	bBtBusyTraffic=FALSE;
	BOOLEAN 		bRejApAggPkt=FALSE;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_FW_VER, &fwVer);

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], FirmwareVersion = 0x%x(%d)\n", fwVer, fwVer));
	
	regBTActive = 0x444;
	regBTState = 0x448;
	regBTPolling = 0x44c;	
	
	btBusyThresh = 40;
	
	BT_Active = pBtCoexist->fBtcRead4Byte(pBtCoexist, regBTActive);
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_MONITOR, ("[BTCoex], BT_Active(0x%x)=0x%x\n", regBTActive, BT_Active));
	BT_Active = BT_Active & 0x00ffffff;

	BT_State = pBtCoexist->fBtcRead4Byte(pBtCoexist, regBTState);
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_MONITOR, ("[BTCoex], BT_State(0x%x)=0x%x\n", regBTState, BT_State));
	BT_State = BT_State & 0x00ffffff;

	BT_Polling = pBtCoexist->fBtcRead4Byte(pBtCoexist, regBTPolling);
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_MONITOR, ("[BTCoex], BT_Polling(0x%x)=0x%x\n", regBTPolling, BT_Polling));

	if(BT_Active==0xffffffff && BT_State==0xffffffff && BT_Polling==0xffffffff )
		return;

	// 2011/05/04 MH For Slim combo test meet a problem. Surprise remove and WLAN is running
	// DHCP process. At the same time, the register read value might be zero. And cause BSOD 0x7f
	// EXCEPTION_DIVIDED_BY_ZERO. In This case, the stack content may always be wrong due to 
	// HW divide trap.
	if (BT_Polling==0)
		return;

	halbtc8192d2ant_MonitorBtEnableDisable(pBtCoexist, BT_Active);
	
	Ratio_Act = BT_Active*1000/BT_Polling;
	Ratio_STA = BT_State*1000/BT_Polling;
		
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_MONITOR, ("[BTCoex], Ratio_Act=%d\n", Ratio_Act));
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_MONITOR, ("[BTCoex], Ratio_STA=%d\n", Ratio_STA));

	if(BTC_CHIP_CSR_BC8 == pBtCoexist->boardInfo.btChipType)
	{
		if(Ratio_STA < 60)	// BT PAN idle
		{
		}
		else
		{
			// Check if BT PAN (under BT 2.1) is uplink or downlink
			if((Ratio_Act/Ratio_STA) < 2)
			{	// BT PAN Uplink
				pCoexSta->bBtUplink = TRUE;
			}
			else
			{	// BT PAN downlink
				pCoexSta->bBtUplink = FALSE;
			}
		}
	}	
	
	// Check BT is idle or not
	if(!pBtCoexist->stackInfo.bBtLinkExist)
	{
		pCoexSta->bBtBusy = FALSE;
	}
	else
	{
		if(BTC_CHIP_CSR_BC4 == pBtCoexist->boardInfo.btChipType)
		{
			if(Ratio_Act<20)
			{
				pCoexSta->bBtBusy = FALSE;
			}
			else
			{
				pCoexSta->bBtBusy = TRUE;
			}
		}
		else if(BTC_CHIP_CSR_BC8 == pBtCoexist->boardInfo.btChipType)
		{
			if(Ratio_STA < btBusyThresh)
			{
				pCoexSta->bBtBusy = FALSE;
			}
			else
			{
				pCoexSta->bBtBusy = TRUE;
			}

			if( (Ratio_STA < btBusyThresh) ||
				(Ratio_Act<180 && Ratio_STA<130) )
			{
				pCoexSta->bA2dpBusy = FALSE;
			}
			else
			{
				pCoexSta->bA2dpBusy = TRUE;
			}
		}
	}

	pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_BL_BT_TRAFFIC_BUSY, &pCoexSta->bBtBusy);
	pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_BL_BT_LIMITED_DIG, &pCoexSta->bBtBusy);
	
	if(bBtBusyTraffic != pCoexSta->bBtBusy)
	{	// BT idle or BT non-idle
		bBtBusyTraffic = pCoexSta->bBtBusy;
		stateChange = TRUE;
	}

	if(stateChange)
	{
		if(!pCoexSta->bBtBusy)
		{
			halbtc8192d2ant_LowPenaltyRa(pBtCoexist, NORMAL_EXEC, FALSE);
			halbtc8192d2ant_RfShrink(pBtCoexist, NORMAL_EXEC, FALSE);
			halbtc8192d2ant_CoexAllOff(pBtCoexist);
			pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x40, 0x0);
		}
		else
		{
			halbtc8192d2ant_LowPenaltyRa(pBtCoexist, NORMAL_EXEC, TRUE);
			halbtc8192d2ant_RfShrink(pBtCoexist, NORMAL_EXEC, TRUE);
		}
	}

	if(stateChange)
	{
		bRejApAggPkt = pCoexSta->bBtBusy;
		pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_BL_TO_REJ_AP_AGG_PKT, &bRejApAggPkt);
		pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_ACT_AGGREGATE_CTRL, NULL);
	}
}

VOID
halbtc8192d2ant_ActionA2dp(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	u1Byte			wifiRssiState, wifiRssiState1=BTC_RSSI_STATE_HIGH;
	u4Byte			wifiBw, wifiTrafficDir;
	BOOLEAN 		bWifiBusy=FALSE;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_BW, &wifiBw);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_BUSY, &bWifiBusy);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_TRAFFIC_DIRECTION, &wifiTrafficDir);

	wifiRssiState = halbtc8192d2ant_WifiRssiState(pBtCoexist, 0, 2, 47, 0);
	if(pCoexSta->bA2dpBusy && bWifiBusy)
	{
		if(BTC_WIFI_BW_HT40 == wifiBw)
		{
			wifiRssiState1 = halbtc8192d2ant_WifiRssiState(pBtCoexist, 1, 2, 47, 0);
		}
		else
		{
			if(BTC_WIFI_TRAFFIC_TX == wifiTrafficDir)
			{
				wifiRssiState1 = halbtc8192d2ant_WifiRssiState(pBtCoexist, 1, 2, 25, 0);
			}
			else if(BTC_WIFI_TRAFFIC_RX == wifiTrafficDir)
			{
				wifiRssiState1 = halbtc8192d2ant_WifiRssiState(pBtCoexist, 1, 2, 40, 0);
			}
		}

		// fw mechanism first
		if(BTC_WIFI_TRAFFIC_TX == wifiTrafficDir)
		{
			halbtc8192d2ant_Balance(pBtCoexist, NORMAL_EXEC, TRUE, 0xc, 0x18);
			halbtc8192d2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, TRUE, FALSE, 0x20, FALSE);
		}
		else if(BTC_WIFI_TRAFFIC_RX == wifiTrafficDir)
		{
			halbtc8192d2ant_Balance(pBtCoexist, NORMAL_EXEC, TRUE, 0x10, 0x18);
			halbtc8192d2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, TRUE, FALSE, 0x20, FALSE);
		}

		// sw mechanism 
		if( (wifiRssiState1 == BTC_RSSI_STATE_HIGH) ||
			(wifiRssiState1 == BTC_RSSI_STATE_STAY_HIGH) )
		{
			halbtc8192d2ant_AgcTable(pBtCoexist, NORMAL_EXEC, TRUE);
		}
		else
		{
			halbtc8192d2ant_AgcTable(pBtCoexist, NORMAL_EXEC, FALSE);
		}
		
		if(BTC_WIFI_BW_HT40 == wifiBw)
		{
			halbtc8192d2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, TRUE);
			halbtc8192d2ant_DacSwing(pBtCoexist, NORMAL_EXEC, FALSE, 0x30);
		}
		else
		{
			if( (wifiRssiState == BTC_RSSI_STATE_HIGH) ||
				(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH) )
			{
				halbtc8192d2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, TRUE);
				halbtc8192d2ant_DacSwing(pBtCoexist, NORMAL_EXEC, FALSE, 0x30);
			}
			else
			{
				halbtc8192d2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, FALSE);
				halbtc8192d2ant_DacSwing(pBtCoexist, NORMAL_EXEC, FALSE, 0x30);
			}
		}
	}
	else if(pCoexSta->bA2dpBusy)
	{
		// fw mechanism first
		halbtc8192d2ant_Balance(pBtCoexist, NORMAL_EXEC, FALSE, 0, 0);
		halbtc8192d2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, TRUE, TRUE, 0x18, FALSE);

		// sw mechanism 
		halbtc8192d2ant_AgcTable(pBtCoexist, NORMAL_EXEC, FALSE);
		halbtc8192d2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, FALSE);
		halbtc8192d2ant_DacSwing(pBtCoexist, NORMAL_EXEC, FALSE, 0x30);
	}
	else
	{
		halbtc8192d2ant_CoexAllOff(pBtCoexist);
	}
}

VOID
halbtc8192d2ant_ActionPan(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	BOOLEAN 	bBtHsOn=FALSE, bWifiBusy=FALSE;
	u1Byte		wifiRssiState, wifiRssiState1;
	u4Byte		wifiBw, wifiTrafficDir;
	s4Byte		wifiRssi;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_BW, &wifiBw);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_HS_OPERATION, &bBtHsOn);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_BUSY, &bWifiBusy);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_TRAFFIC_DIRECTION, &wifiTrafficDir);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_S4_WIFI_RSSI, &wifiRssi);

	if(bBtHsOn)
	{
		halbtc8192d2ant_CoexAllOff(pBtCoexist);
	}
	else
	{
		wifiRssiState = halbtc8192d2ant_WifiRssiState(pBtCoexist, 0, 3, 25, 50);
		if(BTC_WIFI_BW_HT40 == wifiBw)
		{
			wifiRssiState1 = halbtc8192d2ant_WifiRssiState(pBtCoexist, 1, 2, 47, 0);
		}
		else
		{			
			wifiRssiState1 = halbtc8192d2ant_WifiRssiState(pBtCoexist, 1, 2, 25, 0);
		}

		if(pCoexSta->bBtBusy && bWifiBusy)
		{
			if( (wifiRssiState == BTC_RSSI_STATE_HIGH) ||
				(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH) )
			{
				// fw mechanism first
				if(pCoexSta->bBtUplink)
				{
					halbtc8192d2ant_Balance(pBtCoexist, NORMAL_EXEC, TRUE, 0x20, 0x20);
					halbtc8192d2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, TRUE, FALSE, 0x20, FALSE);
				}
				else
				{
					halbtc8192d2ant_Balance(pBtCoexist, NORMAL_EXEC, FALSE, 0, 0);
					halbtc8192d2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0, FALSE);
				}
				// sw mechanism 
				if( (wifiRssiState1 == BTC_RSSI_STATE_HIGH) ||
					(wifiRssiState1 == BTC_RSSI_STATE_STAY_HIGH) )
				{
					halbtc8192d2ant_AgcTable(pBtCoexist, NORMAL_EXEC, TRUE);
				}
				else
				{
					halbtc8192d2ant_AgcTable(pBtCoexist, NORMAL_EXEC, FALSE);
				}
				halbtc8192d2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, TRUE);
				if(pCoexSta->bBtUplink)
				{
					halbtc8192d2ant_DacSwing(pBtCoexist, NORMAL_EXEC, FALSE, 0x30);
				}
				else
				{
					halbtc8192d2ant_DacSwing(pBtCoexist, NORMAL_EXEC, TRUE, 0x20);
				}
			}
			else if( (wifiRssiState == BTC_RSSI_STATE_MEDIUM) ||
				(wifiRssiState == BTC_RSSI_STATE_STAY_MEDIUM) )
			{
				// fw mechanism first
				halbtc8192d2ant_Balance(pBtCoexist, NORMAL_EXEC, TRUE, 0x20, 0x20);

				if(BTC_WIFI_TRAFFIC_TX == wifiTrafficDir)
				{
					halbtc8192d2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, TRUE, FALSE, 0x20, FALSE);
				}
				else if(BTC_WIFI_TRAFFIC_RX == wifiTrafficDir)
				{
					if(BTC_WIFI_BW_HT40 == wifiBw)
						halbtc8192d2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, TRUE, FALSE, 0x20, FALSE);//BT_FW_NAV_ON);
					else
						halbtc8192d2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, TRUE, FALSE, 0x20, FALSE);
				}
				// sw mechanism 
				if( (wifiRssiState1 == BTC_RSSI_STATE_HIGH) ||
					(wifiRssiState1 == BTC_RSSI_STATE_STAY_HIGH) )
				{
					halbtc8192d2ant_AgcTable(pBtCoexist, NORMAL_EXEC, TRUE);
				}
				else
				{
					halbtc8192d2ant_AgcTable(pBtCoexist, NORMAL_EXEC, FALSE);
				}
				halbtc8192d2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, TRUE);
				halbtc8192d2ant_DacSwing(pBtCoexist, NORMAL_EXEC, FALSE, 0x30);
			}
			else
			{
				// fw mechanism first
				halbtc8192d2ant_Balance(pBtCoexist, NORMAL_EXEC, TRUE, 0x20, 0x20);
				
				if(BTC_WIFI_TRAFFIC_TX == wifiTrafficDir)
				{
					halbtc8192d2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, TRUE, FALSE, 0x20, FALSE);
				}
				else if(BTC_WIFI_TRAFFIC_RX == wifiTrafficDir)
				{
					if(pCoexSta->bBtUplink)
					{
						if(BTC_WIFI_BW_HT40 == wifiBw)
						{
							halbtc8192d2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, TRUE, FALSE, 0x20, FALSE);//BT_FW_NAV_ON);
						}
						else
						{
							halbtc8192d2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, TRUE, FALSE, 0x20, FALSE);
						}						
					}
					else
					{
						halbtc8192d2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, TRUE, FALSE, 0x20, FALSE);
					}
				}
				// sw mechanism 
				if( (wifiRssiState1 == BTC_RSSI_STATE_HIGH) ||
					(wifiRssiState1 == BTC_RSSI_STATE_STAY_HIGH) )
				{
					halbtc8192d2ant_AgcTable(pBtCoexist, NORMAL_EXEC, TRUE);
				}
				else
				{
					halbtc8192d2ant_AgcTable(pBtCoexist, NORMAL_EXEC, FALSE);
				}
				halbtc8192d2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, FALSE);
				halbtc8192d2ant_DacSwing(pBtCoexist, NORMAL_EXEC, FALSE, 0x30);
			}
		}
		else if(pCoexSta->bBtBusy && 
				!bWifiBusy &&
				(wifiRssi < 30))
		{
			// fw mechanism first
			halbtc8192d2ant_Balance(pBtCoexist, NORMAL_EXEC, TRUE, 0x0a, 0x20);
			halbtc8192d2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, TRUE, FALSE, 0x20, FALSE);
			// sw mechanism 
			halbtc8192d2ant_AgcTable(pBtCoexist, NORMAL_EXEC, FALSE);
			halbtc8192d2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, FALSE);
			halbtc8192d2ant_DacSwing(pBtCoexist, NORMAL_EXEC, FALSE, 0x30);
		}
		else
		{
			halbtc8192d2ant_CoexAllOff(pBtCoexist);
		}
	}
}


VOID
halbtc8192d2ant_ActionHid(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	u1Byte		wifiRssiState=BTC_RSSI_STATE_HIGH;
	u4Byte		wifiTrafficDir;
	BOOLEAN 	bWifiBusy=FALSE;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_BUSY, &bWifiBusy);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_TRAFFIC_DIRECTION, &wifiTrafficDir);
	if(BTC_WIFI_TRAFFIC_TX == wifiTrafficDir)
	{
		wifiRssiState = halbtc8192d2ant_WifiRssiState(pBtCoexist, 0, 2, 45, 0);
	}
	else if(BTC_WIFI_TRAFFIC_RX == wifiTrafficDir)
	{
		wifiRssiState = halbtc8192d2ant_WifiRssiState(pBtCoexist, 0, 2, 20, 0);
	}
		
	if(pCoexSta->bBtBusy && bWifiBusy)
	{
		if( (wifiRssiState == BTC_RSSI_STATE_HIGH) ||
			(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH) )
		{
			// fw mechanism first
			halbtc8192d2ant_Balance(pBtCoexist, NORMAL_EXEC, FALSE, 0, 0);
			halbtc8192d2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0, FALSE);
			
			// sw mechanism 			
			halbtc8192d2ant_AgcTable(pBtCoexist, NORMAL_EXEC, FALSE);
			halbtc8192d2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, FALSE);
			halbtc8192d2ant_DacSwing(pBtCoexist, NORMAL_EXEC, TRUE, 0x20);
		}
		else
		{
			// fw mechanism first
			if(BTC_WIFI_TRAFFIC_TX == wifiTrafficDir)
			{
				halbtc8192d2ant_Balance(pBtCoexist, NORMAL_EXEC, FALSE, 0, 0);
				halbtc8192d2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, TRUE, TRUE, 0x18, FALSE);
			}
			else if(BTC_WIFI_TRAFFIC_RX == wifiTrafficDir)
			{
				halbtc8192d2ant_Balance(pBtCoexist, NORMAL_EXEC, TRUE, 0x15, 0x15);
				halbtc8192d2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, TRUE, FALSE, 0x30, FALSE);
			}			
			// sw mechanism 
			halbtc8192d2ant_AgcTable(pBtCoexist, NORMAL_EXEC, FALSE);
			halbtc8192d2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, FALSE);
			halbtc8192d2ant_DacSwing(pBtCoexist, NORMAL_EXEC, FALSE, 0x30);
		}
	}
	else
	{
		halbtc8192d2ant_CoexAllOff(pBtCoexist);
	}
}



VOID
halbtc8192d2ant_ActionSco(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	u1Byte	wifiRssiState;
	u4Byte	wifiBw;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_BW, &wifiBw);
	wifiRssiState = halbtc8192d2ant_WifiRssiState(pBtCoexist, 0, 2, 47, 0);

	if(BTC_WIFI_BW_HT40 == wifiBw)
	{
		// fw mechanism first
		halbtc8192d2ant_Balance(pBtCoexist, NORMAL_EXEC, FALSE, 0, 0);
		halbtc8192d2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0, FALSE);

		// sw mechanism 		
		halbtc8192d2ant_AgcTable(pBtCoexist, NORMAL_EXEC, FALSE);
		halbtc8192d2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, TRUE);
		halbtc8192d2ant_DacSwing(pBtCoexist, NORMAL_EXEC, FALSE, 0x30);
	}
	else
	{
		// fw mechanism first
		halbtc8192d2ant_Balance(pBtCoexist, NORMAL_EXEC, FALSE, 0, 0);
		halbtc8192d2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0, FALSE);
		
		// sw mechanism
		if( (wifiRssiState == BTC_RSSI_STATE_HIGH) ||
			(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH) )
		{
			halbtc8192d2ant_AgcTable(pBtCoexist, NORMAL_EXEC, TRUE);
			halbtc8192d2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, TRUE);
			halbtc8192d2ant_DacSwing(pBtCoexist, NORMAL_EXEC, FALSE, 0x30);
		}
		else
		{
			halbtc8192d2ant_AgcTable(pBtCoexist, NORMAL_EXEC, FALSE);
			halbtc8192d2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, FALSE);
			halbtc8192d2ant_DacSwing(pBtCoexist, NORMAL_EXEC, FALSE, 0x30);
		}
	}
}

VOID
halbtc8192d2ant_ActionHidA2dp(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	u1Byte		wifiRssiState, wifiRssiState1;
	u4Byte		wifiBw;

	if(pCoexSta->bBtBusy)
	{
		wifiRssiState1 = halbtc8192d2ant_WifiRssiState(pBtCoexist, 1, 2, 35, 0);
		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_BW, &wifiBw);
		if(BTC_WIFI_BW_HT40 == wifiBw)
		{
			// fw mechanism first
			halbtc8192d2ant_Balance(pBtCoexist, NORMAL_EXEC, FALSE, 0, 0);
			halbtc8192d2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0, FALSE);


			// sw mechanism 
			if( (wifiRssiState1 == BTC_RSSI_STATE_HIGH) ||
				(wifiRssiState1 == BTC_RSSI_STATE_STAY_HIGH) )
			{
				halbtc8192d2ant_AgcTable(pBtCoexist, NORMAL_EXEC, TRUE);
			}
			else
			{
				halbtc8192d2ant_AgcTable(pBtCoexist, NORMAL_EXEC, FALSE);
			}
			halbtc8192d2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, TRUE);
			halbtc8192d2ant_DacSwing(pBtCoexist, NORMAL_EXEC, TRUE, 0x18);
		}
		else
		{
			wifiRssiState = halbtc8192d2ant_WifiRssiState(pBtCoexist, 0, 2, 47, 0);
			// fw mechanism
			halbtc8192d2ant_Balance(pBtCoexist, NORMAL_EXEC, FALSE, 0, 0);
			halbtc8192d2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0, FALSE);

			// sw mechanism 
			if( (wifiRssiState1 == BTC_RSSI_STATE_HIGH) ||
				(wifiRssiState1 == BTC_RSSI_STATE_STAY_HIGH) )
			{
				halbtc8192d2ant_AgcTable(pBtCoexist, NORMAL_EXEC, TRUE);
			}
			else
			{
				halbtc8192d2ant_AgcTable(pBtCoexist, NORMAL_EXEC, FALSE);
			}
			if( (wifiRssiState == BTC_RSSI_STATE_HIGH) ||
				(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH) )
			{
				halbtc8192d2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, TRUE);
				halbtc8192d2ant_DacSwing(pBtCoexist, NORMAL_EXEC, TRUE, 0x18);
			}
			else
			{
				halbtc8192d2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, FALSE);
				halbtc8192d2ant_DacSwing(pBtCoexist, NORMAL_EXEC, TRUE, 0x18);
			}
		}
	}
	else
	{
		halbtc8192d2ant_CoexAllOff(pBtCoexist);
	}
}


VOID
halbtc8192d2ant_ActionHidPanBc4(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	BOOLEAN		bBtHsOn=FALSE, bWifiBusy=FALSE;
	u4Byte		wifiBw, wifiTrafficDir;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_HS_OPERATION, &bBtHsOn);
	
	if(bBtHsOn)
	{
		halbtc8192d2ant_Balance(pBtCoexist, NORMAL_EXEC, FALSE, 0, 0);
		halbtc8192d2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0x0, FALSE);

		halbtc8192d2ant_CoexTable(pBtCoexist, NORMAL_EXEC, 0x55555555, 0x000000f0, 0x40000010);
		pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x40, 0xa0);
	}
	else
	{
		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_BW, &wifiBw);
		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_BUSY, &bWifiBusy);
		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_TRAFFIC_DIRECTION, &wifiTrafficDir);
		if(BTC_WIFI_BW_LEGACY == wifiBw)
		{
			halbtc8192d2ant_Balance(pBtCoexist, NORMAL_EXEC, FALSE, 0, 0);
			halbtc8192d2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0x0, FALSE);

			halbtc8192d2ant_CoexTable(pBtCoexist, NORMAL_EXEC, 0x55555555, 0x000000f0, 0x40000010);
			pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x40, 0xa0);
		}
		else if(BTC_WIFI_TRAFFIC_TX == wifiTrafficDir)
		{
			halbtc8192d2ant_Balance(pBtCoexist, NORMAL_EXEC, FALSE, 0, 0);
			halbtc8192d2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0x0, FALSE);
			
			halbtc8192d2ant_CoexTable(pBtCoexist, NORMAL_EXEC, 0x55555555, 0x000000f0, 0x40000010);
			pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x40, 0xa0);
		}
		else if(BTC_WIFI_TRAFFIC_RX == wifiTrafficDir)
		{
			pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x40, 0x0);
			halbtc8192d2ant_Balance(pBtCoexist, NORMAL_EXEC, TRUE, 0x20, 0x10);					
			halbtc8192d2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, TRUE, FALSE, 0x20, FALSE);
		}
		else if(!bWifiBusy)
		{
			pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x40, 0x0);
			halbtc8192d2ant_Balance(pBtCoexist, NORMAL_EXEC, FALSE, 0, 0);
			halbtc8192d2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0x0, FALSE);
		}			
	}
	halbtc8192d2ant_AgcTable(pBtCoexist, NORMAL_EXEC, FALSE);
	halbtc8192d2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, FALSE);
	halbtc8192d2ant_DacSwing(pBtCoexist, NORMAL_EXEC, FALSE, 0x30);
}
VOID
halbtc8192d2ant_ActionHidPanBc8(	
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	BOOLEAN		bBtHsOn=FALSE, bWifiBusy=FALSE;
	u1Byte		wifiRssiState;
	u4Byte		wifiBw, wifiTrafficDir;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_HS_OPERATION, &bBtHsOn);

	if(!bBtHsOn)
	{
		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_BUSY, &bWifiBusy);
		wifiRssiState = halbtc8192d2ant_WifiRssiState(pBtCoexist, 0, 2, 25, 0);
		if((pCoexSta->bBtBusy && bWifiBusy))
		{
			// fw mechanism first
			if(pCoexSta->bBtUplink)
			{
				halbtc8192d2ant_Balance(pBtCoexist, NORMAL_EXEC, TRUE, 0x15, 0x20);
			}
			else
			{
				halbtc8192d2ant_Balance(pBtCoexist, NORMAL_EXEC, TRUE, 0x10, 0x20);
			}
			halbtc8192d2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, TRUE, FALSE, 0x20, FALSE);

			// sw mechanism 
			if( (wifiRssiState == BTC_RSSI_STATE_HIGH) ||
				(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH) )
			{
				pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_BW, &wifiBw);
				if(BTC_WIFI_BW_HT40 == wifiBw)
				{
					halbtc8192d2ant_AgcTable(pBtCoexist, NORMAL_EXEC, FALSE);
					halbtc8192d2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, TRUE);
					halbtc8192d2ant_DacSwing(pBtCoexist, NORMAL_EXEC, FALSE, 0x30);
				}
				else
				{
					halbtc8192d2ant_AgcTable(pBtCoexist, NORMAL_EXEC, TRUE);
					halbtc8192d2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, TRUE);
					halbtc8192d2ant_DacSwing(pBtCoexist, NORMAL_EXEC, FALSE, 0x30);
				}
			}
			else
			{
				halbtc8192d2ant_AgcTable(pBtCoexist, NORMAL_EXEC, FALSE);
				halbtc8192d2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, FALSE);
				halbtc8192d2ant_DacSwing(pBtCoexist, NORMAL_EXEC, FALSE, 0x30);
			}
		}
		else
		{
			halbtc8192d2ant_CoexAllOff(pBtCoexist);
		}
	}
	else
	{
		if(BTC_INTF_USB == pBtCoexist->chipInterface)
		{	
			pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_TRAFFIC_DIRECTION, &wifiTrafficDir);
			if(BTC_WIFI_TRAFFIC_TX == wifiTrafficDir)
			{
				halbtc8192d2ant_Balance(pBtCoexist, NORMAL_EXEC, FALSE, 0, 0);
				halbtc8192d2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0x0, FALSE);

				halbtc8192d2ant_CoexTable(pBtCoexist, NORMAL_EXEC, 0x55555555, 0x000000f0, 0x40000010);
				pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x40, 0xa0);
				halbtc8192d2ant_DacSwing(pBtCoexist, NORMAL_EXEC, TRUE, 0x18);
			}
			else if(BTC_WIFI_TRAFFIC_RX == wifiTrafficDir)
			{
				halbtc8192d2ant_DacSwing(pBtCoexist, NORMAL_EXEC, TRUE, 0x18);
			}
		}
		else 
		{
			if(pCoexSta->bBtBusy)
			{
				// fw mechanism
				halbtc8192d2ant_Balance(pBtCoexist, NORMAL_EXEC, FALSE, 0, 0);
				halbtc8192d2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0, FALSE);

				// sw mechanism
				halbtc8192d2ant_AgcTable(pBtCoexist, NORMAL_EXEC, FALSE);
				halbtc8192d2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, FALSE);
				halbtc8192d2ant_DacSwing(pBtCoexist, NORMAL_EXEC, TRUE, 0x20);
			}
			else
			{
				halbtc8192d2ant_DacSwing(pBtCoexist, NORMAL_EXEC, FALSE, 0x30);
			}
		}
	}
}

VOID
halbtc8192d2ant_ActionHidPan(
	IN	PBTC_COEXIST		pBtCoexist
	)
{		
	if(BTC_CHIP_CSR_BC4 == pBtCoexist->boardInfo.btChipType)
	{
		halbtc8192d2ant_ActionHidPanBc4(pBtCoexist);
	}
	else if(BTC_CHIP_CSR_BC8 == pBtCoexist->boardInfo.btChipType)
	{
		halbtc8192d2ant_ActionHidPanBc8(pBtCoexist);
	}
}

VOID
halbtc8192d2ant_ActionPanA2dpBc4(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	BOOLEAN		bBtHsOn=FALSE, bWifiBusy=FALSE;
	u1Byte		wifiRssiState;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_HS_OPERATION, &bBtHsOn);

	pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x40, 0x0);
	if(bBtHsOn)
	{
		if(pCoexSta->bBtBusy)
		{
			// fw mechanism
			halbtc8192d2ant_Balance(pBtCoexist, NORMAL_EXEC, FALSE, 0, 0);
			halbtc8192d2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0, FALSE);

			// sw mechanism
			halbtc8192d2ant_AgcTable(pBtCoexist, NORMAL_EXEC, FALSE);
			halbtc8192d2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, FALSE);
			halbtc8192d2ant_DacSwing(pBtCoexist, NORMAL_EXEC, TRUE, 0x20);
		}
		else
		{
			halbtc8192d2ant_CoexAllOff(pBtCoexist);
		}
	}
	else
	{
		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_BUSY, &bWifiBusy);
		if(pCoexSta->bBtBusy && bWifiBusy)
		{
			halbtc8192d2ant_Balance(pBtCoexist, NORMAL_EXEC, TRUE, 0x20, 0x10);
			halbtc8192d2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, TRUE, FALSE, 0x20, FALSE);
		}
		else
		{
			halbtc8192d2ant_Balance(pBtCoexist, NORMAL_EXEC, FALSE, 0, 0);
			halbtc8192d2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0x0, FALSE);
		}			
		// sw mechanism
		halbtc8192d2ant_AgcTable(pBtCoexist, NORMAL_EXEC, FALSE);
		halbtc8192d2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, FALSE);
		halbtc8192d2ant_DacSwing(pBtCoexist, NORMAL_EXEC, FALSE, 0x30);
	}
}
VOID
halbtc8192d2ant_ActionPanA2dpBc8(	
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	BOOLEAN		bBtHsOn=FALSE, bWifiBusy=FALSE;
	u1Byte		wifiRssiState;
	u4Byte		wifiBw;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_HS_OPERATION, &bBtHsOn);

	if(!bBtHsOn)
	{
		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_BUSY, &bWifiBusy);
		wifiRssiState = halbtc8192d2ant_WifiRssiState(pBtCoexist, 0, 2, 25, 0);
		if((pCoexSta->bBtBusy && bWifiBusy))
		{
			// fw mechanism first
			if(pCoexSta->bBtUplink)
			{
				halbtc8192d2ant_Balance(pBtCoexist, NORMAL_EXEC, TRUE, 0x15, 0x20);
			}
			else
			{
				halbtc8192d2ant_Balance(pBtCoexist, NORMAL_EXEC, TRUE, 0x10, 0x20);
			}
			halbtc8192d2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, TRUE, FALSE, 0x20, FALSE);

			// sw mechanism 
			if( (wifiRssiState == BTC_RSSI_STATE_HIGH) ||
				(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH) )
			{
				pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_BW, &wifiBw);
				if(BTC_WIFI_BW_HT40 == wifiBw)
				{
					halbtc8192d2ant_AgcTable(pBtCoexist, NORMAL_EXEC, FALSE);
					halbtc8192d2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, TRUE);
					halbtc8192d2ant_DacSwing(pBtCoexist, NORMAL_EXEC, FALSE, 0x30);
				}
				else	
				{
					halbtc8192d2ant_AgcTable(pBtCoexist, NORMAL_EXEC, TRUE);
					halbtc8192d2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, TRUE);
					halbtc8192d2ant_DacSwing(pBtCoexist, NORMAL_EXEC, FALSE, 0x30);
				}
			}
			else
			{
				halbtc8192d2ant_AgcTable(pBtCoexist, NORMAL_EXEC, FALSE);
				halbtc8192d2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, FALSE);
				halbtc8192d2ant_DacSwing(pBtCoexist, NORMAL_EXEC, FALSE, 0x30);
			}
		}
		else
		{
			halbtc8192d2ant_CoexAllOff(pBtCoexist);
		}
	}
	else
	{
		if(pCoexSta->bBtBusy)
		{
			// fw mechanism
			halbtc8192d2ant_Balance(pBtCoexist, NORMAL_EXEC, FALSE, 0, 0);
			halbtc8192d2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0, FALSE);

			// sw mechanism			
			halbtc8192d2ant_AgcTable(pBtCoexist, NORMAL_EXEC, FALSE);
			halbtc8192d2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, FALSE);
			halbtc8192d2ant_DacSwing(pBtCoexist, NORMAL_EXEC, TRUE, 0x20);
		}
		else
		{
			halbtc8192d2ant_DacSwing(pBtCoexist, NORMAL_EXEC, FALSE, 0x30);
		}
	}
}

VOID
halbtc8192d2ant_ActionPanA2dp(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	if(BTC_CHIP_CSR_BC4 == pBtCoexist->boardInfo.btChipType)
	{
		halbtc8192d2ant_ActionPanA2dpBc4(pBtCoexist);
	}
	else if(BTC_CHIP_CSR_BC8 == pBtCoexist->boardInfo.btChipType)
	{
		halbtc8192d2ant_ActionPanA2dpBc8(pBtCoexist);
	}
}

BOOLEAN
halbtc8192d2ant_IsBtCoexistEnter(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	u1Byte			macPhyMode;
	BOOLEAN			bRet=TRUE;
	BOOLEAN			bWifiUnder5G=FALSE;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U1_MAC_PHY_MODE, &macPhyMode);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_UNDER_5G, &bWifiUnder5G);
	
	if(BTC_SMSP != macPhyMode)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Only support single mac single phy!!\n"));
		bRet = FALSE;
	}

	if(bWifiUnder5G)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], wifi is under 5G or A band\n"));
		halbtc8192d2ant_CoexAllOff(pBtCoexist);
		bRet = FALSE;
	}

	return bRet;
}

//============================================================
// extern function start with EXhalbtc8192d2ant_
//============================================================
VOID
EXhalbtc8192d2ant_PowerOnSetting(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
}

VOID
EXhalbtc8192d2ant_InitHwConfig(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN				bWifiOnly
	)
{
	u1Byte	u1Tmp=0;

	BTC_PRINT(BTC_MSG_INTERFACE, INTF_INIT, ("[BTCoex], 2Ant Init HW Config!!\n"));

	// backup rf 0x1e value
	pCoexDm->btRf0x1eBackup = 
		pBtCoexist->fBtcGetRfReg(pBtCoexist, BTC_RF_A, 0x1e, 0xfffff);

	if( (BTC_CHIP_CSR_BC4 == pBtCoexist->boardInfo.btChipType) ||
		(BTC_CHIP_CSR_BC8 == pBtCoexist->boardInfo.btChipType) )
	{
		u1Tmp =  pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x4fd) & BIT0;
		pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x4fd, u1Tmp);
		
		halbtc8192d2ant_CoexTable(pBtCoexist, FORCE_EXEC, 0xaaaa9aaa, 0xffbd0040, 0x40000010);

		// switch control, here we set pathA to control
		// 0x878[13] = 1, 0:pathB, 1:pathA(default)
		pBtCoexist->fBtcSetBbReg(pBtCoexist, 0x878, BIT13, 0x1);
		
		// antsel control, here we use phy0 and enable antsel.
		// 0x87c[16:15] = b'11, enable antsel, antsel output pin
		// 0x87c[30] = 0, 0: phy0, 1:phy 1
		pBtCoexist->fBtcSetBbReg(pBtCoexist, 0x87c, bMaskDWord, 0x1fff8);
		
		// antsel to Bt or Wifi, it depends Bt on/off.
		// 0x860[9:8] = 'b10, b10:Bt On, WL2G off(default), b01:Bt off, WL2G on.
		pBtCoexist->fBtcSetBbReg(pBtCoexist, 0x860, BIT9|BIT8, 0x2);
		
		// sw/hw control switch, here we set sw control
		// 0x870[9:8] = 'b11 sw control, 'b00 hw control
		pBtCoexist->fBtcSetBbReg(pBtCoexist, 0x870, BIT9|BIT8, 0x3);
	}
}

VOID
EXhalbtc8192d2ant_InitCoexDm(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	BTC_PRINT(BTC_MSG_INTERFACE, INTF_INIT, ("[BTCoex], Coex Mechanism Init!!\n"));
	
	halbtc8192d2ant_InitCoexDm(pBtCoexist);
}

VOID
EXhalbtc8192d2ant_DisplayCoexInfo(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	PBTC_BOARD_INFO		pBoardInfo=&pBtCoexist->boardInfo;
	PBTC_STACK_INFO		pStackInfo=&pBtCoexist->stackInfo;
	pu1Byte				cliBuf=pBtCoexist->cliBuf;
	u1Byte				u1Tmp[4], i, btInfoExt, psTdmaCase=0;
	u4Byte				u4Tmp[4];

	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n ============[BT Coexist info]============");
	CL_PRINTF(cliBuf);

	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d ", "Ant PG number/ Ant mechanism:", \
		pBoardInfo->pgAntNum, pBoardInfo->btdmAntNum);
	CL_PRINTF(cliBuf);	
	
	if(pBtCoexist->bManualControl)
	{
		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "[Action Manual control]!!");
		CL_PRINTF(cliBuf);
	}
	
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s / %d", "BT stack/ hci ext ver", \
		((pStackInfo->bProfileNotified)? "Yes":"No"), pStackInfo->hciVersion);
	CL_PRINTF(cliBuf);

	// wifi status
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "============[Wifi Status]============");
	CL_PRINTF(cliBuf);
	pBtCoexist->fBtcDispDbgMsg(pBtCoexist, BTC_DBG_DISP_WIFI_STATUS);

	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "============[BT Status]============");
	CL_PRINTF(cliBuf);
	
	if(pStackInfo->bProfileNotified)
	{			
		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d / %d / %d", "SCO/HID/PAN/A2DP", \
			pStackInfo->bScoExist, pStackInfo->bHidExist, pStackInfo->bPanExist, pStackInfo->bA2dpExist);
		CL_PRINTF(cliBuf);	

		pBtCoexist->fBtcDispDbgMsg(pBtCoexist, BTC_DBG_DISP_BT_LINK_INFO);
	}
	
	// Sw mechanism	
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "============[Sw mechanism]============");
	CL_PRINTF(cliBuf);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d(0x%x) ", "SM2[AgcT/ AdcB/ SwDacSwing(lvl)]", \
		pCoexDm->bCurAgcTableEn, pCoexDm->bCurAdcBackOff, pCoexDm->bCurDacSwingOn, pCoexDm->curDacSwingLvl);
	CL_PRINTF(cliBuf);

	// Fw mechanism		
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "============[Fw mechanism]============");
	CL_PRINTF(cliBuf);

	// Hw setting		
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "============[Hw setting]============");
	CL_PRINTF(cliBuf);	

	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "RF-A, 0x1e initVal", \
		pCoexDm->btRf0x1eBackup);
	CL_PRINTF(cliBuf);

	u1Tmp[0] = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x40);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0x40", \
		u1Tmp[0]);
	CL_PRINTF(cliBuf);

	u4Tmp[0] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0xc50);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0xc50(dig)", \
		u4Tmp[0]);
	CL_PRINTF(cliBuf);

	u4Tmp[0] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x6c4);
	u4Tmp[1] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x6c8);
	u4Tmp[2] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x6cc);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x/ 0x%x", "0x6c4/0x6c8/0x6cc(coexTable)", \
		u4Tmp[0], u4Tmp[1], u4Tmp[2]);
	CL_PRINTF(cliBuf);

	pBtCoexist->fBtcDispDbgMsg(pBtCoexist, BTC_DBG_DISP_COEX_STATISTICS);
}


VOID
EXhalbtc8192d2ant_IpsNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			type
	)
{
	if(BTC_IPS_ENTER == type)
	{
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], IPS ENTER notify\n"));
		halbtc8192d2ant_CoexAllOff(pBtCoexist);
	}
	else if(BTC_IPS_LEAVE == type)
	{
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], IPS LEAVE notify\n"));
		//halbtc8192d2ant_InitCoexDm(pBtCoexist);
	}
}

VOID
EXhalbtc8192d2ant_LpsNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			type
	)
{
	if(BTC_LPS_ENABLE == type)
	{
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], LPS ENABLE notify\n"));
		halbtc8192d2ant_CoexAllOff(pBtCoexist);
	}
	else if(BTC_LPS_DISABLE == type)
	{
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], LPS DISABLE notify\n"));
		halbtc8192d2ant_InitCoexDm(pBtCoexist);
	}
}

VOID
EXhalbtc8192d2ant_ScanNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			type
	)
{
	if(BTC_SCAN_START == type)
	{
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], SCAN START notify\n"));
	}
	else if(BTC_SCAN_FINISH == type)
	{
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], SCAN FINISH notify\n"));
	}
}

VOID
EXhalbtc8192d2ant_ConnectNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			type
	)
{
	if(BTC_ASSOCIATE_START == type)
	{
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], CONNECT START notify\n"));
	}
	else if(BTC_ASSOCIATE_FINISH == type)
	{
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], CONNECT FINISH notify\n"));
	}
}

VOID
EXhalbtc8192d2ant_MediaStatusNotify(
	IN	PBTC_COEXIST			pBtCoexist,
	IN	u1Byte				type
	)
{	
	if(BTC_MEDIA_CONNECT == type)
	{
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], MEDIA connect notify\n"));
	}
	else
	{
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], MEDIA disconnect notify\n"));
	}	
}

VOID
EXhalbtc8192d2ant_SpecialPacketNotify(
	IN	PBTC_COEXIST			pBtCoexist,
	IN	u1Byte				type
	)
{
	if(type == BTC_PACKET_DHCP)
	{
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], DHCP Packet notify\n"));
	}
}

VOID
EXhalbtc8192d2ant_BtInfoNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	pu1Byte			tmpBuf,
	IN	u1Byte			length
	)
{
}

VOID
EXhalbtc8192d2ant_HaltNotify(
	IN	PBTC_COEXIST			pBtCoexist
	)
{
	BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], Halt notify\n"));

	EXhalbtc8192d2ant_MediaStatusNotify(pBtCoexist, BTC_MEDIA_DISCONNECT);
}

VOID
EXhalbtc8192d2ant_Periodical(
	IN	PBTC_COEXIST			pBtCoexist
	)
{
	u1Byte	algorithm;

	BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], 2Ant Periodical!!\n"));

	// NOTE:
	// sw mechanism must be done after fw mechanism
	// 
	if(!halbtc8192d2ant_IsBtCoexistEnter(pBtCoexist))
		return;

	if(BTC_CHIP_CSR_BC8 == pBtCoexist->boardInfo.btChipType)
	{
		pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_ACT_GET_BT_RSSI, NULL);

		halbtc8192d2ant_MonitorBtState(pBtCoexist);
		algorithm = halbtc8192d2ant_ActionAlgorithm(pBtCoexist);	
		pCoexDm->curAlgorithm = algorithm;
		switch(pCoexDm->curAlgorithm)
		{
			case BT_8192D_2ANT_COEX_ALGO_SCO:
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action 2-Ant, algorithm = SCO\n"));
				halbtc8192d2ant_ActionSco(pBtCoexist);
				break;
			case BT_8192D_2ANT_COEX_ALGO_HID:
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action 2-Ant, algorithm = HID\n"));
				halbtc8192d2ant_ActionHid(pBtCoexist);
				break;
			case BT_8192D_2ANT_COEX_ALGO_A2DP:
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action 2-Ant, algorithm = A2DP\n"));
				halbtc8192d2ant_ActionA2dp(pBtCoexist);
				break;
			case BT_8192D_2ANT_COEX_ALGO_PAN:
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action 2-Ant, algorithm = PAN\n"));
				halbtc8192d2ant_ActionPan(pBtCoexist);
				break;
			case BT_8192D_2ANT_COEX_ALGO_HID_A2DP:
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action 2-Ant, algorithm = HID+A2DP\n"));
				halbtc8192d2ant_ActionHidA2dp(pBtCoexist);
				break;
			case BT_8192D_2ANT_COEX_ALGO_HID_PAN:
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action 2-Ant, algorithm = PAN+HID\n"));
				halbtc8192d2ant_ActionHidPan(pBtCoexist);
				break;
			case BT_8192D_2ANT_COEX_ALGO_PAN_A2DP:
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action 2-Ant, algorithm = PAN+A2DP\n"));
				halbtc8192d2ant_ActionPanA2dp(pBtCoexist);
				break;
			default:
				break;
		}
	}
}

#endif

