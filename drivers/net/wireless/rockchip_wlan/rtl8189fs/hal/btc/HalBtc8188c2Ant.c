/* SPDX-License-Identifier: GPL-2.0 */
//============================================================
// Description:
//
// This file is for 92CE/92CU BT 1 Antenna Co-exist mechanism
//
// By cosa 02/11/2011
//
//============================================================

//============================================================
// include files
//============================================================
#include "Mp_Precomp.h"

#if WPP_SOFTWARE_TRACE
#include "HalBtc8188c2Ant.tmh"
#endif

#if(BT_30_SUPPORT == 1)
//============================================================
// Global variables, these are static variables
//============================================================
static COEX_DM_8188C_2ANT	GLCoexDm8188c2Ant;
static PCOEX_DM_8188C_2ANT 	pCoexDm=&GLCoexDm8188c2Ant;
static COEX_STA_8188C_2ANT	GLCoexSta8188c2Ant;
static PCOEX_STA_8188C_2ANT	pCoexSta=&GLCoexSta8188c2Ant;

//============================================================
// local function start with btdm_
//============================================================
u1Byte
halbtc8188c2ant_WifiRssiState(
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
			if(wifiRssi >= (rssiThresh+BTC_RSSI_COEX_THRESH_TOL_8188C_2ANT))
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
			if(wifiRssi >= (rssiThresh+BTC_RSSI_COEX_THRESH_TOL_8188C_2ANT))
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
			if(wifiRssi >= (rssiThresh1+BTC_RSSI_COEX_THRESH_TOL_8188C_2ANT))
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

u1Byte
halbtc8188c2ant_ActionAlgorithm(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	PBTC_STACK_INFO		pStackInfo=&pBtCoexist->stackInfo;
	u1Byte				algorithm=BT_8188C_2ANT_COEX_ALGO_UNDEFINED;
	u1Byte				numOfDiffProfile=0;
	
	if(!pStackInfo->bBtLinkExist)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], No profile exists!!!\n"));
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
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], SCO algorithm\n"));
		algorithm = BT_8188C_2ANT_COEX_ALGO_SCO;
	}
	else
	{
		if(numOfDiffProfile == 1)
		{
			if(pStackInfo->bHidExist)
			{
				RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], HID only\n"));
				algorithm = BT_8188C_2ANT_COEX_ALGO_HID;
			}
			else if(pStackInfo->bA2dpExist)
			{
				RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], A2DP only\n"));
				algorithm = BT_8188C_2ANT_COEX_ALGO_A2DP;
			}
			else if(pStackInfo->bPanExist)
			{
				RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], PAN only\n"));
				algorithm = BT_8188C_2ANT_COEX_ALGO_PAN;
			}
		}
		else
		{
			if( pStackInfo->bHidExist &&
				pStackInfo->bA2dpExist )
			{
				RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], HID + A2DP\n"));
				algorithm = BT_8188C_2ANT_COEX_ALGO_HID_A2DP;
			}
			else if( pStackInfo->bHidExist &&
				pStackInfo->bPanExist )
			{				
				RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], HID + PAN\n"));
				algorithm = BT_8188C_2ANT_COEX_ALGO_HID_PAN;
			}
			else if( pStackInfo->bPanExist &&
				pStackInfo->bA2dpExist )
			{				
				RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], PAN + A2DP\n"));
				algorithm = BT_8188C_2ANT_COEX_ALGO_PAN_A2DP;
			}
		}		
	}
	return algorithm;
}

VOID
halbtc8188c2ant_SetFwBalance(
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
	
	RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], Balance=[%s:%dms:%dms], write 0xc=0x%x\n", 
		bBalanceOn?"ON":"OFF", ms0, ms1,
		H2C_Parameter[0]<<16|H2C_Parameter[1]<<8|H2C_Parameter[2]));

	pBtCoexist->fBtcFillH2c(pBtCoexist, 0xc, 3, H2C_Parameter);	
}

VOID
halbtc8188c2ant_Balance(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bForceExec,
	IN	BOOLEAN			bBalanceOn,
	IN	u1Byte			ms0,
	IN	u1Byte			ms1
	)
{
	RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], %s turn Balance %s\n", 
		(bForceExec? "force to":""), (bBalanceOn? "ON":"OFF")));
	pCoexDm->bCurBalanceOn = bBalanceOn;

	if(!bForceExec)
	{
		if(pCoexDm->bPreBalanceOn == pCoexDm->bCurBalanceOn)
			return;
	}
	halbtc8188c2ant_SetFwBalance(pBtCoexist, bBalanceOn, ms0, ms1);

	pCoexDm->bPreBalanceOn = pCoexDm->bCurBalanceOn;
}

VOID
halbtc8188c2ant_SetFwDiminishWifi(
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
		RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], DiminishWiFi 0x20 original, but set 0x18 for Low RSSI!\n"));
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

	RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], bDacOn=%s, bInterruptOn=%s, bNavOn=%s, write 0xe=0x%x\n", 
		(bDacOn?"ON":"OFF"), (bInterruptOn?"ON":"OFF"), (bNavOn?"ON":"OFF"),
		(H2C_Parameter[0]<<16|H2C_Parameter[1]<<8|H2C_Parameter[2])));
	pBtCoexist->fBtcFillH2c(pBtCoexist, 0xe, 3, H2C_Parameter);
}

VOID
halbtc8188c2ant_DiminishWifi(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bForceExec,
	IN	BOOLEAN			bDacOn,
	IN	BOOLEAN			bInterruptOn,
	IN	u1Byte			fwDacSwingLvl,
	IN	BOOLEAN			bNavOn
	)
{
	RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], %s set Diminish Wifi, bDacOn=%s, bInterruptOn=%s, fwDacSwingLvl=%d, bNavOn=%s\n", 
		(bForceExec? "force to":""), (bDacOn? "ON":"OFF"), (bInterruptOn? "ON":"OFF"), fwDacSwingLvl, (bNavOn? "ON":"OFF")));

	pCoexDm->bCurDacOn = bDacOn;
	pCoexDm->bCurInterruptOn = bInterruptOn;
	pCoexDm->curFwDacSwingLvl = fwDacSwingLvl;
	pCoexDm->bCurNavOn = bNavOn;

	if(!bForceExec)
	{
		if( (pCoexDm->bPreDacOn==pCoexDm->bCurDacOn) &&
			(pCoexDm->bPreInterruptOn==pCoexDm->bCurInterruptOn) &&
			(pCoexDm->preFwDacSwingLvl==pCoexDm->curFwDacSwingLvl) &&
			(pCoexDm->bPreNavOn==pCoexDm->bCurNavOn) )
			return;
	}
	halbtc8188c2ant_SetFwDiminishWifi(pBtCoexist, bDacOn, bInterruptOn, fwDacSwingLvl, bNavOn);

	pCoexDm->bPreDacOn = pCoexDm->bCurDacOn;
	pCoexDm->bPreInterruptOn = pCoexDm->bCurInterruptOn;
	pCoexDm->preFwDacSwingLvl = pCoexDm->curFwDacSwingLvl;
	pCoexDm->bPreNavOn = pCoexDm->bCurNavOn;
}

VOID
halbtc8188c2ant_SetSwRfRxLpfCorner(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bRxRfShrinkOn
	)
{
	if(bRxRfShrinkOn)
	{
		//Shrink RF Rx LPF corner
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Shrink RF Rx LPF corner!!\n"));
		pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1e, 0xf0, 0xf);		
	}
	else
	{
		//Resume RF Rx LPF corner
		// After initialized, we can use pCoexDm->btRf0x1eBackup
		if(pBtCoexist->bInitilized)
		{
			RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Resume RF Rx LPF corner!!\n"));
			pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1e, 0xf0, pCoexDm->btRf0x1eBackup);
		}
	}
}

VOID
halbtc8188c2ant_RfShrink(
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
	halbtc8188c2ant_SetSwRfRxLpfCorner(pBtCoexist, pCoexDm->bCurRfRxLpfShrink);

	pCoexDm->bPreRfRxLpfShrink = pCoexDm->bCurRfRxLpfShrink;
}

VOID
halbtc8188c2ant_SetSwPenaltyTxRateAdaptive(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bLowPenaltyRa
	)
{
	u1Byte	tmpU1;

	tmpU1 = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x4fd);
	if(bLowPenaltyRa)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Tx rate adaptive, set low penalty!!\n"));
		tmpU1 &= ~BIT2;
	}
	else
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Tx rate adaptive, set normal!!\n"));
		tmpU1 |= BIT2;
	}
	pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x4fd, tmpU1);
}

VOID
halbtc8188c2ant_LowPenaltyRa(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bForceExec,
	IN	BOOLEAN			bLowPenaltyRa
	)
{
	RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], %s turn LowPenaltyRA = %s\n",  
		(bForceExec? "force to":""), ((bLowPenaltyRa)? "ON":"OFF")));
	pCoexDm->bCurLowPenaltyRa = bLowPenaltyRa;

	if(!bForceExec)
	{
		if(pCoexDm->bPreLowPenaltyRa == pCoexDm->bCurLowPenaltyRa) 
			return;
	}
	halbtc8188c2ant_SetSwPenaltyTxRateAdaptive(pBtCoexist, pCoexDm->bCurLowPenaltyRa);

	pCoexDm->bPreLowPenaltyRa = pCoexDm->bCurLowPenaltyRa;
}

VOID
halbtc8188c2ant_SetSwFullTimeDacSwing(
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
halbtc8188c2ant_DacSwing(
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
	halbtc8188c2ant_SetSwFullTimeDacSwing(pBtCoexist, bDacSwingOn, dacSwingLvl);

	pCoexDm->bPreDacSwingOn = pCoexDm->bCurDacSwingOn;
	pCoexDm->preDacSwingLvl = pCoexDm->curDacSwingLvl;
}

VOID
halbtc8188c2ant_SetAdcBackOff(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bAdcBackOff
	)
{
	if(bAdcBackOff)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BB BackOff Level On!\n"));
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc04,0x3a07611);
	}
	else
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BB BackOff Level Off!\n"));		
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc04,0x3a05611);
	}
}

VOID
halbtc8188c2ant_AdcBackOff(
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
	halbtc8188c2ant_SetAdcBackOff(pBtCoexist, pCoexDm->bCurAdcBackOff);

	pCoexDm->bPreAdcBackOff = pCoexDm->bCurAdcBackOff;
}

VOID
halbtc8188c2ant_SetAgcTable(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bAgcTableEn
	)
{
	u1Byte		rssiAdjustVal=0;

	if(bAgcTableEn)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Agc Table On!\n"));
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78,0x4e1c0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78,0x4d1d0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78,0x4c1e0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78,0x4b1f0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78,0x4a200001);

		pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x12, 0xfffff, 0xdc000);
		pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x12, 0xfffff, 0x90000);
		pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x12, 0xfffff, 0x51000);
		pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x12, 0xfffff, 0x12000);
		pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1a, 0xfffff, 0x00255);
	}
	else
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Agc Table Off!\n"));
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78,0x641c0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78,0x631d0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78,0x621e0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78,0x611f0001);
		pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0xc78,0x60200001);

		pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x12, 0xfffff, 0x32000);
		pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x12, 0xfffff, 0x71000);
		pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x12, 0xfffff, 0xb0000);
		pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x12, 0xfffff, 0xfc000);
		pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1a, 0xfffff, 0x10255);
	}

	// set rssiAdjustVal for wifi module.
	pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_U1_RSSI_ADJ_VAL_FOR_AGC_TABLE_ON, &rssiAdjustVal);
}


VOID
halbtc8188c2ant_AgcTable(
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
	halbtc8188c2ant_SetAgcTable(pBtCoexist, bAgcTableEn);

	pCoexDm->bPreAgcTableEn = pCoexDm->bCurAgcTableEn;
}

VOID
halbtc8188c2ant_SetCoexTable(
	IN	PBTC_COEXIST	pBtCoexist,
	IN	u4Byte		val0x6c4,
	IN	u4Byte		val0x6c8,
	IN	u4Byte		val0x6cc
	)
{
	RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], set coex table, set 0x6c4=0x%x\n", val0x6c4));
	pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x6c4, val0x6c4);

	RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], set coex table, set 0x6c8=0x%x\n", val0x6c8));
	pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x6c8, val0x6c8);

	RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], set coex table, set 0x6cc=0x%x\n", val0x6cc));
	pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x6cc, val0x6cc);
}

VOID
halbtc8188c2ant_CoexTable(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bForceExec,
	IN	u4Byte			val0x6c4,
	IN	u4Byte			val0x6c8,
	IN	u4Byte			val0x6cc
	)
{
	RT_TRACE(COMP_COEX, DBG_TRACE, ("[BTCoex], %s write Coex Table 0x6c4=0x%x, 0x6c8=0x%x, 0x6cc=0x%x\n", 
		(bForceExec? "force to":""), val0x6c4, val0x6c8, val0x6cc));
	pCoexDm->curVal0x6c4 = val0x6c4;
	pCoexDm->curVal0x6c8 = val0x6c8;
	pCoexDm->curVal0x6cc = val0x6cc;

	if(!bForceExec)
	{	
		if( (pCoexDm->preVal0x6c4 == pCoexDm->curVal0x6c4) &&
			(pCoexDm->preVal0x6c8 == pCoexDm->curVal0x6c8) &&
			(pCoexDm->preVal0x6cc == pCoexDm->curVal0x6cc) )
			return;
	}
	halbtc8188c2ant_SetCoexTable(pBtCoexist, val0x6c4, val0x6c8, val0x6cc);

	pCoexDm->preVal0x6c4 = pCoexDm->curVal0x6c4;
	pCoexDm->preVal0x6c8 = pCoexDm->curVal0x6c8;
	pCoexDm->preVal0x6cc = pCoexDm->curVal0x6cc;
}

VOID
halbtc8188c2ant_CoexAllOff(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	// fw mechanism
	halbtc8188c2ant_Balance(pBtCoexist, NORMAL_EXEC, FALSE, 0, 0);
	halbtc8188c2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0, FALSE);

	// sw mechanism
	halbtc8188c2ant_AgcTable(pBtCoexist, NORMAL_EXEC, FALSE);
	halbtc8188c2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, FALSE);
	halbtc8188c2ant_DacSwing(pBtCoexist, NORMAL_EXEC, FALSE, 0x30);
}
VOID
halbtc8188c2ant_InitCoexDm(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
}


VOID
halbtc8188c2ant_MonitorBtState(
	IN	PBTC_COEXIST			pBtCoexist
	)
{
	BOOLEAN			stateChange=FALSE;
	u4Byte 			BT_Polling, Ratio_Act, Ratio_STA;
	u4Byte 			BT_Active, BT_State;
	u4Byte			regBTActive=0, regBTState=0, regBTPolling=0;
	u4Byte			btBusyThresh=0;
	u4Byte			fwVer=0;
	static BOOLEAN	bBtBusyTraffic=FALSE;
	BOOLEAN			bRejApAggPkt=FALSE;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_FW_VER, &fwVer);
	RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], FirmwareVersion = 0x%x(%d)\n", fwVer, fwVer));
	if(fwVer < 62)
	{
		regBTActive = 0x488;
		regBTState = 0x48c;
		regBTPolling = 0x490;
	}
	else
	{
		regBTActive = 0x444;
		regBTState = 0x448;
		if(fwVer >= 74)
			regBTPolling = 0x44c;
		else
			regBTPolling = 0x700;
	}
	btBusyThresh = 60;
	
	BT_Active = pBtCoexist->fBtcRead4Byte(pBtCoexist, regBTActive);
	RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BT_Active(0x%x)=0x%x\n", regBTActive, BT_Active));
	BT_Active = BT_Active & 0x00ffffff;

	BT_State = pBtCoexist->fBtcRead4Byte(pBtCoexist, regBTState);
	RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BT_State(0x%x)=0x%x\n", regBTState, BT_State));
	BT_State = BT_State & 0x00ffffff;

	BT_Polling = pBtCoexist->fBtcRead4Byte(pBtCoexist, regBTPolling);
	RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], BT_Polling(0x%x)=0x%x\n", regBTPolling, BT_Polling));

	if(BT_Active==0xffffffff && BT_State==0xffffffff && BT_Polling==0xffffffff )
		return;

	// 2011/05/04 MH For Slim combo test meet a problem. Surprise remove and WLAN is running
	// DHCP process. At the same time, the register read value might be zero. And cause BSOD 0x7f
	// EXCEPTION_DIVIDED_BY_ZERO. In This case, the stack content may always be wrong due to 
	// HW divide trap.
	if (BT_Polling==0)
		return;
	
	Ratio_Act = BT_Active*1000/BT_Polling;
	Ratio_STA = BT_State*1000/BT_Polling;
		
	RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Ratio_Act=%d\n", Ratio_Act));
	RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Ratio_STA=%d\n", Ratio_STA));

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
			halbtc8188c2ant_LowPenaltyRa(pBtCoexist, NORMAL_EXEC, FALSE);
			halbtc8188c2ant_RfShrink(pBtCoexist, NORMAL_EXEC, FALSE);
			halbtc8188c2ant_CoexAllOff(pBtCoexist);
			pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x40, 0x0);
		}
		else
		{
			halbtc8188c2ant_LowPenaltyRa(pBtCoexist, NORMAL_EXEC, TRUE);
			halbtc8188c2ant_RfShrink(pBtCoexist, NORMAL_EXEC, TRUE);
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
halbtc8188c2ant_ActionA2dpBc4(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	u1Byte			wifiRssiState;
	u4Byte			wifiBw, wifiTrafficDir;

	pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x40, 0x0);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_BW, &wifiBw);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_TRAFFIC_DIRECTION, &wifiTrafficDir);
	
	if(pCoexSta->bBtBusy)
	{
		if(BTC_WIFI_BW_HT40 == wifiBw)
		{
			// fw mechanism first
			if(BTC_WIFI_TRAFFIC_TX == wifiTrafficDir)
			{
				halbtc8188c2ant_Balance(pBtCoexist, NORMAL_EXEC, TRUE, 0xc, 0x18);
				halbtc8188c2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0x20, FALSE);
			}
			else if(BTC_WIFI_TRAFFIC_RX == wifiTrafficDir)
			{
				halbtc8188c2ant_Balance(pBtCoexist, NORMAL_EXEC, FALSE, 0, 0);
				halbtc8188c2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0, FALSE);
			}

			// sw mechanism 
			halbtc8188c2ant_AgcTable(pBtCoexist, NORMAL_EXEC, FALSE);
			halbtc8188c2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, TRUE);
			halbtc8188c2ant_DacSwing(pBtCoexist, NORMAL_EXEC, TRUE, 0x20);
		}
		else
		{
			wifiRssiState = halbtc8188c2ant_WifiRssiState(pBtCoexist, 0, 2, 47, 0);

			if(BTC_WIFI_TRAFFIC_TX == wifiTrafficDir)
			{
				halbtc8188c2ant_Balance(pBtCoexist, NORMAL_EXEC, TRUE, 0xc, 0x18);
				halbtc8188c2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0x20, FALSE);
			}
			else if(BTC_WIFI_TRAFFIC_RX == wifiTrafficDir)
			{
				halbtc8188c2ant_Balance(pBtCoexist, NORMAL_EXEC, FALSE, 0, 0);
				halbtc8188c2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0, FALSE);
			}

			// sw mechanism 
			if( (wifiRssiState == BTC_RSSI_STATE_HIGH) ||
				(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH) )
			{
				halbtc8188c2ant_AgcTable(pBtCoexist, NORMAL_EXEC, TRUE);
				halbtc8188c2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, TRUE);
				halbtc8188c2ant_DacSwing(pBtCoexist, NORMAL_EXEC, TRUE, 0x20);
			}
			else
			{
				halbtc8188c2ant_AgcTable(pBtCoexist, NORMAL_EXEC, FALSE);
				halbtc8188c2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, FALSE);
				halbtc8188c2ant_DacSwing(pBtCoexist, NORMAL_EXEC, TRUE, 0x20);
			}
		}
	}
	else
	{
		halbtc8188c2ant_CoexAllOff(pBtCoexist);
	}
}

VOID
halbtc8188c2ant_ActionA2dpBc8(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	u1Byte			wifiRssiState;
	u4Byte			wifiBw, wifiTrafficDir;
	BOOLEAN			bWifiBusy=FALSE;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_BUSY, &bWifiBusy);
	if(pCoexSta->bA2dpBusy && bWifiBusy)
	{
		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_BW, &wifiBw);
		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_TRAFFIC_DIRECTION, &wifiTrafficDir);
		wifiRssiState = halbtc8188c2ant_WifiRssiState(pBtCoexist, 0, 2, 47, 0);

		// fw mechanism first
		if(BTC_WIFI_TRAFFIC_TX == wifiTrafficDir)
		{
			halbtc8188c2ant_Balance(pBtCoexist, NORMAL_EXEC, TRUE, 0xc, 0x18);
			halbtc8188c2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, TRUE, FALSE, 0x20, FALSE);
		}
		else if(BTC_WIFI_TRAFFIC_RX == wifiTrafficDir)
		{
			halbtc8188c2ant_Balance(pBtCoexist, NORMAL_EXEC, TRUE, 0x10, 0x18);
			halbtc8188c2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, TRUE, FALSE, 0x20, FALSE);
		}

		// sw mechanism 
		if(BTC_WIFI_BW_HT40 == wifiBw)
		{
			halbtc8188c2ant_AgcTable(pBtCoexist, NORMAL_EXEC, FALSE);
			halbtc8188c2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, TRUE);
			halbtc8188c2ant_DacSwing(pBtCoexist, NORMAL_EXEC, FALSE, 0x30);
		}
		else
		{
			if( (wifiRssiState == BTC_RSSI_STATE_HIGH) ||
				(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH) )
			{
				halbtc8188c2ant_AgcTable(pBtCoexist, NORMAL_EXEC, TRUE);
				halbtc8188c2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, TRUE);
				halbtc8188c2ant_DacSwing(pBtCoexist, NORMAL_EXEC, FALSE, 0x30);
			}
			else
			{
				halbtc8188c2ant_AgcTable(pBtCoexist, NORMAL_EXEC, FALSE);
				halbtc8188c2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, FALSE);
				halbtc8188c2ant_DacSwing(pBtCoexist, NORMAL_EXEC, FALSE, 0x30);
			}
		}
	}
	else if(pCoexSta->bA2dpBusy)
	{
		// fw mechanism first
		halbtc8188c2ant_Balance(pBtCoexist, NORMAL_EXEC, FALSE, 0, 0);
		halbtc8188c2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, TRUE, TRUE, 0x18, FALSE);

		// sw mechanism 
		halbtc8188c2ant_AgcTable(pBtCoexist, NORMAL_EXEC, FALSE);
		halbtc8188c2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, FALSE);
		halbtc8188c2ant_DacSwing(pBtCoexist, NORMAL_EXEC, FALSE, 0x30);
	}
	else
	{
		halbtc8188c2ant_CoexAllOff(pBtCoexist);
	}
}

VOID
halbtc8188c2ant_ActionA2dp(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	if(BTC_CHIP_CSR_BC4 == pBtCoexist->boardInfo.btChipType)
	{
		halbtc8188c2ant_ActionA2dpBc4(pBtCoexist);
	}
	else if(BTC_CHIP_CSR_BC8 == pBtCoexist->boardInfo.btChipType)
	{
		halbtc8188c2ant_ActionA2dpBc8(pBtCoexist);
	}
}

VOID
halbtc8188c2ant_ActionPanBc4(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	BOOLEAN		bBtHsOn=FALSE, bWifiBusy=FALSE;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_HS_OPERATION, &bBtHsOn);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_BUSY, &bWifiBusy);
	pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x40, 0x0);
	if(bBtHsOn)
	{
		halbtc8188c2ant_Balance(pBtCoexist, NORMAL_EXEC, FALSE, 0, 0);
		halbtc8188c2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0x0, FALSE);
	}
	else
	{
		if(pCoexSta->bBtBusy && bWifiBusy)
		{
			halbtc8188c2ant_Balance(pBtCoexist, NORMAL_EXEC, TRUE, 0x20, 0x10);
			halbtc8188c2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, TRUE, FALSE, 0x20, FALSE);
		}
		else
		{
			halbtc8188c2ant_Balance(pBtCoexist, NORMAL_EXEC, FALSE, 0, 0);
			halbtc8188c2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0x0, FALSE);
		}
	}
	// sw mechanism
	halbtc8188c2ant_AgcTable(pBtCoexist, NORMAL_EXEC, FALSE);
	halbtc8188c2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, FALSE);
	halbtc8188c2ant_DacSwing(pBtCoexist, NORMAL_EXEC, FALSE, 0x30);
}

VOID
halbtc8188c2ant_ActionPanBc8(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	BOOLEAN		bBtHsOn=FALSE, bWifiBusy=FALSE;
	u1Byte		wifiRssiState;
	u4Byte		wifiBw, wifiTrafficDir;
	s4Byte		wifiRssi;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_BW, &wifiBw);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_HS_OPERATION, &bBtHsOn);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_BUSY, &bWifiBusy);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_TRAFFIC_DIRECTION, &wifiTrafficDir);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_S4_WIFI_RSSI, &wifiRssi);
	
	if(bBtHsOn)
	{
		halbtc8188c2ant_CoexAllOff(pBtCoexist);
	}
	else
	{
		wifiRssiState = halbtc8188c2ant_WifiRssiState(pBtCoexist, 0, 3, 25, 50);

		if(pCoexSta->bBtBusy && bWifiBusy)
		{
			if( (wifiRssiState == BTC_RSSI_STATE_HIGH) ||
				(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH) )
			{
				// fw mechanism first
				if(pCoexSta->bBtUplink)
				{
					halbtc8188c2ant_Balance(pBtCoexist, NORMAL_EXEC, TRUE, 0x20, 0x20);
					halbtc8188c2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, TRUE, FALSE, 0x20, FALSE);
				}
				else
				{
					halbtc8188c2ant_Balance(pBtCoexist, NORMAL_EXEC, FALSE, 0, 0);
					halbtc8188c2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0, FALSE);
				}
				// sw mechanism 
				if(BTC_WIFI_BW_HT40 == wifiBw)
				{
					halbtc8188c2ant_AgcTable(pBtCoexist, NORMAL_EXEC, FALSE);
				}
				else
				{
					halbtc8188c2ant_AgcTable(pBtCoexist, NORMAL_EXEC, TRUE);
				}
				halbtc8188c2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, TRUE);
				if(pCoexSta->bBtUplink)
				{
					halbtc8188c2ant_DacSwing(pBtCoexist, NORMAL_EXEC, FALSE, 0x30);
				}
				else
				{
					halbtc8188c2ant_DacSwing(pBtCoexist, NORMAL_EXEC, TRUE, 0x20);
				}
			}
			else if( (wifiRssiState == BTC_RSSI_STATE_MEDIUM) ||
				(wifiRssiState == BTC_RSSI_STATE_STAY_MEDIUM) )
			{
				// fw mechanism first
				halbtc8188c2ant_Balance(pBtCoexist, NORMAL_EXEC, TRUE, 0x20, 0x20);

				if(BTC_WIFI_TRAFFIC_TX == wifiTrafficDir)
				{
					halbtc8188c2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, TRUE, FALSE, 0x20, FALSE);
				}
				else if(BTC_WIFI_TRAFFIC_RX == wifiTrafficDir)
				{
					if(BTC_WIFI_BW_HT40 == wifiBw)
						halbtc8188c2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, TRUE, FALSE, 0x20, FALSE);//BT_FW_NAV_ON);
					else
						halbtc8188c2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, TRUE, FALSE, 0x20, FALSE);
				}
				// sw mechanism 
				if(BTC_WIFI_BW_HT40 == wifiBw)
				{
					halbtc8188c2ant_AgcTable(pBtCoexist, NORMAL_EXEC, FALSE);
				}
				else
				{
					halbtc8188c2ant_AgcTable(pBtCoexist, NORMAL_EXEC, TRUE);
				}
				halbtc8188c2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, TRUE);
				halbtc8188c2ant_DacSwing(pBtCoexist, NORMAL_EXEC, FALSE, 0x30);
			}
			else
			{
				// fw mechanism first
				halbtc8188c2ant_Balance(pBtCoexist, NORMAL_EXEC, TRUE, 0x20, 0x20);
				
				if(BTC_WIFI_TRAFFIC_TX == wifiTrafficDir)
				{
					halbtc8188c2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, TRUE, FALSE, 0x20, FALSE);
				}
				else if(BTC_WIFI_TRAFFIC_RX == wifiTrafficDir)
				{
					if(pCoexSta->bBtUplink)
					{
						if(BTC_WIFI_BW_HT40 == wifiBw)
						{
							halbtc8188c2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, TRUE, FALSE, 0x20, FALSE);//BT_FW_NAV_ON);
						}
						else
						{
							halbtc8188c2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, TRUE, FALSE, 0x20, FALSE);
						}						
					}
					else
					{
						halbtc8188c2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, TRUE, FALSE, 0x20, FALSE);
					}
				}
				// sw mechanism 
				halbtc8188c2ant_AgcTable(pBtCoexist, NORMAL_EXEC, FALSE);
				halbtc8188c2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, FALSE);
				halbtc8188c2ant_DacSwing(pBtCoexist, NORMAL_EXEC, FALSE, 0x30);
			}
		}
		else if(pCoexSta->bBtBusy && !bWifiBusy && (wifiRssi < 30))
		{
			// fw mechanism first
			halbtc8188c2ant_Balance(pBtCoexist, NORMAL_EXEC, TRUE, 0x0a, 0x20);
			halbtc8188c2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, TRUE, FALSE, 0x20, FALSE);
			// sw mechanism 
			halbtc8188c2ant_AgcTable(pBtCoexist, NORMAL_EXEC, FALSE);
			halbtc8188c2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, FALSE);
			halbtc8188c2ant_DacSwing(pBtCoexist, NORMAL_EXEC, FALSE, 0x30);
		}
		else
		{
			halbtc8188c2ant_CoexAllOff(pBtCoexist);
		}
	}
}

VOID
halbtc8188c2ant_ActionPan(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	if(BTC_CHIP_CSR_BC4 == pBtCoexist->boardInfo.btChipType)
	{
		halbtc8188c2ant_ActionPanBc4(pBtCoexist);
	}
	else if(BTC_CHIP_CSR_BC8 == pBtCoexist->boardInfo.btChipType)
	{
		halbtc8188c2ant_ActionPanBc8(pBtCoexist);
	}
}

VOID
halbtc8188c2ant_ActionHid(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	u4Byte		wifiBw, wifiTrafficDir;
	BOOLEAN 	bWifiBusy=FALSE;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_BW, &wifiBw);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_BUSY, &bWifiBusy);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_TRAFFIC_DIRECTION, &wifiTrafficDir);
	if(BTC_WIFI_BW_LEGACY == wifiBw)
	{
		halbtc8188c2ant_Balance(pBtCoexist, NORMAL_EXEC, FALSE, 0, 0);
		halbtc8188c2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0x0, FALSE);

		halbtc8188c2ant_CoexTable(pBtCoexist, NORMAL_EXEC, 0x55555555, 0x000000f0, 0x40000010);
		pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x40, 0xa0);
	}
	else if(!bWifiBusy)
	{
		pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x40, 0x0);
		halbtc8188c2ant_Balance(pBtCoexist, NORMAL_EXEC, FALSE, 0, 0);
		halbtc8188c2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0x0, FALSE);
	}
	else if(BTC_WIFI_TRAFFIC_TX == wifiTrafficDir)
	{
		halbtc8188c2ant_Balance(pBtCoexist, NORMAL_EXEC, FALSE, 0, 0);
		halbtc8188c2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0x0, FALSE);

		halbtc8188c2ant_CoexTable(pBtCoexist, NORMAL_EXEC, 0x55555555, 0x000000f0, 0x40000010);
		pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x40, 0xa0);
	}
	else if(BTC_WIFI_TRAFFIC_RX == wifiTrafficDir)
	{
		pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x40, 0x0);
		halbtc8188c2ant_Balance(pBtCoexist, NORMAL_EXEC, FALSE, 0, 0);
		halbtc8188c2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0x0, FALSE);
	}
	// sw mechanism
	halbtc8188c2ant_AgcTable(pBtCoexist, NORMAL_EXEC, FALSE);
	halbtc8188c2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, FALSE);
	halbtc8188c2ant_DacSwing(pBtCoexist, NORMAL_EXEC, FALSE, 0x30);
}


VOID
halbtc8188c2ant_ActionSco(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	u1Byte	wifiRssiState;
	u4Byte	wifiBw;

	if(BTC_CHIP_CSR_BC4 == pBtCoexist->boardInfo.btChipType)
	{
		pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x40, 0x0);
		
		// fw mechanism
		halbtc8188c2ant_Balance(pBtCoexist, NORMAL_EXEC, FALSE, 0, 0);
		halbtc8188c2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0x0, FALSE);

		// sw mechanism
		halbtc8188c2ant_AgcTable(pBtCoexist, NORMAL_EXEC, FALSE);
		halbtc8188c2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, FALSE);
		halbtc8188c2ant_DacSwing(pBtCoexist, NORMAL_EXEC, FALSE, 0x30);
	}
	else if(BTC_CHIP_CSR_BC8 == pBtCoexist->boardInfo.btChipType)
	{
		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_BW, &wifiBw);
		if(BTC_WIFI_BW_HT40 == wifiBw)
		{
			// fw mechanism first
			halbtc8188c2ant_Balance(pBtCoexist, NORMAL_EXEC, FALSE, 0, 0);
			halbtc8188c2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0, FALSE);

			// sw mechanism 
			halbtc8188c2ant_AgcTable(pBtCoexist, NORMAL_EXEC, FALSE);
			halbtc8188c2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, TRUE);
			halbtc8188c2ant_DacSwing(pBtCoexist, NORMAL_EXEC, FALSE, 0x30);
		}
		else
		{
			wifiRssiState = halbtc8188c2ant_WifiRssiState(pBtCoexist, 0, 2, 47, 0);
			// fw mechanism first
			halbtc8188c2ant_Balance(pBtCoexist, NORMAL_EXEC, FALSE, 0, 0);
			halbtc8188c2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0, FALSE);

			// sw mechanism 
			if( (wifiRssiState == BTC_RSSI_STATE_HIGH) ||
				(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH) )
			{
				halbtc8188c2ant_AgcTable(pBtCoexist, NORMAL_EXEC, TRUE);
				halbtc8188c2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, TRUE);
				halbtc8188c2ant_DacSwing(pBtCoexist, NORMAL_EXEC, FALSE, 0x30);
			}
			else
			{			
				halbtc8188c2ant_AgcTable(pBtCoexist, NORMAL_EXEC, FALSE);
				halbtc8188c2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, FALSE);
				halbtc8188c2ant_DacSwing(pBtCoexist, NORMAL_EXEC, FALSE, 0x30);	
			}
		}
	}
}

VOID
halbtc8188c2ant_ActionHidA2dpBc4(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	u1Byte		wifiRssiState;
	u4Byte		wifiBw, wifiTrafficDir;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_BW, &wifiBw);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_TRAFFIC_DIRECTION, &wifiTrafficDir);

	if(pCoexSta->bBtBusy)
	{
		if(BTC_WIFI_BW_HT40 == wifiBw)
		{
			// fw mechanism first
			if(BTC_WIFI_TRAFFIC_TX == wifiTrafficDir)
			{
				halbtc8188c2ant_Balance(pBtCoexist, NORMAL_EXEC, TRUE, 0x7, 0x20);
				halbtc8188c2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0x20, FALSE);
			}
			else if(BTC_WIFI_TRAFFIC_RX == wifiTrafficDir)
			{
				halbtc8188c2ant_Balance(pBtCoexist, NORMAL_EXEC, FALSE, 0, 0);
				halbtc8188c2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0, FALSE);
			}

			// sw mechanism 
			halbtc8188c2ant_AgcTable(pBtCoexist, NORMAL_EXEC, FALSE);
			halbtc8188c2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, TRUE);
			halbtc8188c2ant_DacSwing(pBtCoexist, NORMAL_EXEC, TRUE, 0x18);
		}
		else
		{
			wifiRssiState = halbtc8188c2ant_WifiRssiState(pBtCoexist, 0, 2, 47, 0);
			// fw mechanism first
			if(BTC_WIFI_TRAFFIC_TX == wifiTrafficDir)
			{
				halbtc8188c2ant_Balance(pBtCoexist, NORMAL_EXEC, TRUE, 0x7, 0x20);
				halbtc8188c2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0x20, FALSE);
			}
			else if(BTC_WIFI_TRAFFIC_RX == wifiTrafficDir)
			{
				halbtc8188c2ant_Balance(pBtCoexist, NORMAL_EXEC, FALSE, 0, 0);
				halbtc8188c2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0, FALSE);
			}

			// sw mechanism 
			if( (wifiRssiState == BTC_RSSI_STATE_HIGH) ||
				(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH) )
			{
				halbtc8188c2ant_AgcTable(pBtCoexist, NORMAL_EXEC, TRUE);
				halbtc8188c2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, TRUE);
				halbtc8188c2ant_DacSwing(pBtCoexist, NORMAL_EXEC, TRUE, 0x18);
			}
			else
			{
				halbtc8188c2ant_AgcTable(pBtCoexist, NORMAL_EXEC, FALSE);
				halbtc8188c2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, FALSE);
				halbtc8188c2ant_DacSwing(pBtCoexist, NORMAL_EXEC, TRUE, 0x18);
			}
		}
	}
	else
	{
		halbtc8188c2ant_CoexAllOff(pBtCoexist);
	}
}
VOID
halbtc8188c2ant_ActionHidA2dpBc8(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	u1Byte		wifiRssiState;
	u4Byte		wifiBw;
	
	if(pCoexSta->bBtBusy)
	{
		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_BW, &wifiBw);
		if(BTC_WIFI_BW_HT40 == wifiBw)
		{
			// fw mechanism first
			halbtc8188c2ant_Balance(pBtCoexist, NORMAL_EXEC, FALSE, 0, 0);
			halbtc8188c2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0, FALSE);

			// sw mechanism 
			halbtc8188c2ant_AgcTable(pBtCoexist, NORMAL_EXEC, FALSE);
			halbtc8188c2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, TRUE);
			halbtc8188c2ant_DacSwing(pBtCoexist, NORMAL_EXEC, TRUE, 0x18);
		}
		else
		{
			wifiRssiState = halbtc8188c2ant_WifiRssiState(pBtCoexist, 0, 2, 47, 0);
			// fw mechanism
			halbtc8188c2ant_Balance(pBtCoexist, NORMAL_EXEC, FALSE, 0, 0);
			halbtc8188c2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0, FALSE);

			// sw mechanism 
			if( (wifiRssiState == BTC_RSSI_STATE_HIGH) ||
				(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH) )
			{
				halbtc8188c2ant_AgcTable(pBtCoexist, NORMAL_EXEC, TRUE);
				halbtc8188c2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, TRUE);
				halbtc8188c2ant_DacSwing(pBtCoexist, NORMAL_EXEC, TRUE, 0x18);
			}
			else
			{
				halbtc8188c2ant_AgcTable(pBtCoexist, NORMAL_EXEC, FALSE);
				halbtc8188c2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, FALSE);
				halbtc8188c2ant_DacSwing(pBtCoexist, NORMAL_EXEC, TRUE, 0x18);
			}
		}
	}
	else
	{
		halbtc8188c2ant_CoexAllOff(pBtCoexist);
	}
}

VOID
halbtc8188c2ant_ActionHidA2dp(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	if(BTC_CHIP_CSR_BC4 == pBtCoexist->boardInfo.btChipType)
	{
		halbtc8188c2ant_ActionHidA2dpBc4(pBtCoexist);
	}
	else if(BTC_CHIP_CSR_BC8 == pBtCoexist->boardInfo.btChipType)
	{
		halbtc8188c2ant_ActionHidA2dpBc8(pBtCoexist);
	}
}

VOID
halbtc8188c2ant_ActionHidPanBc4(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	BOOLEAN		bBtHsOn=FALSE, bWifiBusy=FALSE;
	u4Byte		wifiBw, wifiTrafficDir;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_HS_OPERATION, &bBtHsOn);
	
	if(bBtHsOn)
	{	
		halbtc8188c2ant_Balance(pBtCoexist, NORMAL_EXEC, FALSE, 0, 0);
		halbtc8188c2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0x0, FALSE);

		halbtc8188c2ant_CoexTable(pBtCoexist, NORMAL_EXEC, 0x55555555, 0x000000f0, 0x40000010);
		pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x40, 0xa0);
	}
	else
	{
		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_BW, &wifiBw);
		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_BUSY, &bWifiBusy);
		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_TRAFFIC_DIRECTION, &wifiTrafficDir);
		if(BTC_WIFI_BW_LEGACY == wifiBw)
		{
			halbtc8188c2ant_Balance(pBtCoexist, NORMAL_EXEC, FALSE, 0, 0);
			halbtc8188c2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0x0, FALSE);

			halbtc8188c2ant_CoexTable(pBtCoexist, NORMAL_EXEC, 0x55555555, 0x000000f0, 0x40000010);
			pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x40, 0xa0);
		}
		else if(BTC_WIFI_TRAFFIC_TX == wifiTrafficDir)
		{
			halbtc8188c2ant_Balance(pBtCoexist, NORMAL_EXEC, FALSE, 0, 0);
			halbtc8188c2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0x0, FALSE);
			
			halbtc8188c2ant_CoexTable(pBtCoexist, NORMAL_EXEC, 0x55555555, 0x000000f0, 0x40000010);
			pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x40, 0xa0);
		}
		else if(BTC_WIFI_TRAFFIC_RX == wifiTrafficDir)
		{
			pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x40, 0x0);
			halbtc8188c2ant_Balance(pBtCoexist, NORMAL_EXEC, TRUE, 0x20, 0x10);					
			halbtc8188c2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, TRUE, FALSE, 0x20, FALSE);
		}
		else if(!bWifiBusy)
		{
			pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x40, 0x0);
			halbtc8188c2ant_Balance(pBtCoexist, NORMAL_EXEC, FALSE, 0, 0);
			halbtc8188c2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0x0, FALSE);
		}			
	}
	halbtc8188c2ant_AgcTable(pBtCoexist, NORMAL_EXEC, FALSE);
	halbtc8188c2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, FALSE);
	halbtc8188c2ant_DacSwing(pBtCoexist, NORMAL_EXEC, FALSE, 0x30);
}
VOID
halbtc8188c2ant_ActionHidPanBc8(	
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
		wifiRssiState = halbtc8188c2ant_WifiRssiState(pBtCoexist, 0, 2, 25, 0);
		if((pCoexSta->bBtBusy && bWifiBusy))
		{
			// fw mechanism first
			if(pCoexSta->bBtUplink)
			{
				halbtc8188c2ant_Balance(pBtCoexist, NORMAL_EXEC, TRUE, 0x15, 0x20);
			}
			else
			{
				halbtc8188c2ant_Balance(pBtCoexist, NORMAL_EXEC, TRUE, 0x10, 0x20);
			}
			halbtc8188c2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, TRUE, FALSE, 0x20, FALSE);

			// sw mechanism 
			if( (wifiRssiState == BTC_RSSI_STATE_HIGH) ||
				(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH) )
			{
				pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_BW, &wifiBw);
				if(BTC_WIFI_BW_HT40 == wifiBw)
				{
					halbtc8188c2ant_AgcTable(pBtCoexist, NORMAL_EXEC, FALSE);
					halbtc8188c2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, TRUE);
					halbtc8188c2ant_DacSwing(pBtCoexist, NORMAL_EXEC, FALSE, 0x30);
				}
				else
				{
					halbtc8188c2ant_AgcTable(pBtCoexist, NORMAL_EXEC, TRUE);
					halbtc8188c2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, TRUE);
					halbtc8188c2ant_DacSwing(pBtCoexist, NORMAL_EXEC, FALSE, 0x30);
				}
			}
			else
			{
				halbtc8188c2ant_AgcTable(pBtCoexist, NORMAL_EXEC, FALSE);
				halbtc8188c2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, FALSE);
				halbtc8188c2ant_DacSwing(pBtCoexist, NORMAL_EXEC, FALSE, 0x30);
			}
		}
		else
		{
			halbtc8188c2ant_CoexAllOff(pBtCoexist);
		}
	}
	else
	{
		if(BTC_INTF_USB == pBtCoexist->chipInterface)
		{	
			pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_TRAFFIC_DIRECTION, &wifiTrafficDir);
			if(BTC_WIFI_TRAFFIC_TX == wifiTrafficDir)
			{
				halbtc8188c2ant_Balance(pBtCoexist, NORMAL_EXEC, FALSE, 0, 0);
				halbtc8188c2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0x0, FALSE);

				halbtc8188c2ant_CoexTable(pBtCoexist, NORMAL_EXEC, 0x55555555, 0x000000f0, 0x40000010);
				pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x40, 0xa0);
				halbtc8188c2ant_DacSwing(pBtCoexist, NORMAL_EXEC, TRUE, 0x18);
			}
			else if(BTC_WIFI_TRAFFIC_RX == wifiTrafficDir)
			{
				halbtc8188c2ant_DacSwing(pBtCoexist, NORMAL_EXEC, TRUE, 0x18);
			}
		}
		else 
		{
			if(pCoexSta->bBtBusy)
			{
				// fw mechanism
				halbtc8188c2ant_Balance(pBtCoexist, NORMAL_EXEC, FALSE, 0, 0);
				halbtc8188c2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0, FALSE);

				// sw mechanism
				halbtc8188c2ant_AgcTable(pBtCoexist, NORMAL_EXEC, FALSE);
				halbtc8188c2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, FALSE);
				halbtc8188c2ant_DacSwing(pBtCoexist, NORMAL_EXEC, TRUE, 0x20);
			}
			else
			{
				halbtc8188c2ant_DacSwing(pBtCoexist, NORMAL_EXEC, FALSE, 0x30);
			}
		}
	}
}

VOID
halbtc8188c2ant_ActionHidPan(
	IN	PBTC_COEXIST		pBtCoexist
	)
{		
	if(BTC_CHIP_CSR_BC4 == pBtCoexist->boardInfo.btChipType)
	{
		halbtc8188c2ant_ActionHidPanBc4(pBtCoexist);
	}
	else if(BTC_CHIP_CSR_BC8 == pBtCoexist->boardInfo.btChipType)
	{
		halbtc8188c2ant_ActionHidPanBc8(pBtCoexist);
	}
}

VOID
halbtc8188c2ant_ActionPanA2dpBc4(
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
			halbtc8188c2ant_Balance(pBtCoexist, NORMAL_EXEC, FALSE, 0, 0);
			halbtc8188c2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0, FALSE);

			// sw mechanism
			halbtc8188c2ant_AgcTable(pBtCoexist, NORMAL_EXEC, FALSE);
			halbtc8188c2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, FALSE);
			halbtc8188c2ant_DacSwing(pBtCoexist, NORMAL_EXEC, TRUE, 0x20);
		}
		else
		{
			halbtc8188c2ant_CoexAllOff(pBtCoexist);
		}
	}
	else
	{
		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_BUSY, &bWifiBusy);
		if(pCoexSta->bBtBusy && bWifiBusy)
		{
			halbtc8188c2ant_Balance(pBtCoexist, NORMAL_EXEC, TRUE, 0x20, 0x10);
			halbtc8188c2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, TRUE, FALSE, 0x20, FALSE);
		}
		else
		{
			halbtc8188c2ant_Balance(pBtCoexist, NORMAL_EXEC, FALSE, 0, 0);
			halbtc8188c2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0x0, FALSE);
		}			
		// sw mechanism
		halbtc8188c2ant_AgcTable(pBtCoexist, NORMAL_EXEC, FALSE);
		halbtc8188c2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, FALSE);
		halbtc8188c2ant_DacSwing(pBtCoexist, NORMAL_EXEC, FALSE, 0x30);
	}
}
VOID
halbtc8188c2ant_ActionPanA2dpBc8(	
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
		wifiRssiState = halbtc8188c2ant_WifiRssiState(pBtCoexist, 0, 2, 25, 0);
		if((pCoexSta->bBtBusy && bWifiBusy))
		{
			// fw mechanism first
			if(pCoexSta->bBtUplink)
			{
				halbtc8188c2ant_Balance(pBtCoexist, NORMAL_EXEC, TRUE, 0x15, 0x20);
			}
			else
			{
				halbtc8188c2ant_Balance(pBtCoexist, NORMAL_EXEC, TRUE, 0x10, 0x20);
			}
			halbtc8188c2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, TRUE, FALSE, 0x20, FALSE);

			// sw mechanism 
			if( (wifiRssiState == BTC_RSSI_STATE_HIGH) ||
				(wifiRssiState == BTC_RSSI_STATE_STAY_HIGH) )
			{
				pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_BW, &wifiBw);
				if(BTC_WIFI_BW_HT40 == wifiBw)
				{
					halbtc8188c2ant_AgcTable(pBtCoexist, NORMAL_EXEC, FALSE);
					halbtc8188c2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, TRUE);
					halbtc8188c2ant_DacSwing(pBtCoexist, NORMAL_EXEC, FALSE, 0x30);
				}
				else	
				{
					halbtc8188c2ant_AgcTable(pBtCoexist, NORMAL_EXEC, TRUE);
					halbtc8188c2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, TRUE);
					halbtc8188c2ant_DacSwing(pBtCoexist, NORMAL_EXEC, FALSE, 0x30);
				}
			}
			else
			{
				halbtc8188c2ant_AgcTable(pBtCoexist, NORMAL_EXEC, FALSE);
				halbtc8188c2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, FALSE);
				halbtc8188c2ant_DacSwing(pBtCoexist, NORMAL_EXEC, FALSE, 0x30);
			}
		}
		else
		{
			halbtc8188c2ant_CoexAllOff(pBtCoexist);
		}
	}
	else
	{
		if(pCoexSta->bBtBusy)
		{
			// fw mechanism
			halbtc8188c2ant_Balance(pBtCoexist, NORMAL_EXEC, FALSE, 0, 0);
			halbtc8188c2ant_DiminishWifi(pBtCoexist, NORMAL_EXEC, FALSE, FALSE, 0, FALSE);

			// sw mechanism			
			halbtc8188c2ant_AgcTable(pBtCoexist, NORMAL_EXEC, FALSE);
			halbtc8188c2ant_AdcBackOff(pBtCoexist, NORMAL_EXEC, FALSE);
			halbtc8188c2ant_DacSwing(pBtCoexist, NORMAL_EXEC, TRUE, 0x20);
		}
		else
		{
			halbtc8188c2ant_DacSwing(pBtCoexist, NORMAL_EXEC, FALSE, 0x30);
		}
	}
}

VOID
halbtc8188c2ant_ActionPanA2dp(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	if(BTC_CHIP_CSR_BC4 == pBtCoexist->boardInfo.btChipType)
	{
		halbtc8188c2ant_ActionPanA2dpBc4(pBtCoexist);
	}
	else if(BTC_CHIP_CSR_BC8 == pBtCoexist->boardInfo.btChipType)
	{
		halbtc8188c2ant_ActionPanA2dpBc8(pBtCoexist);
	}
}

//============================================================
// extern function start with EXhalbtc8188c2ant_
//============================================================
VOID
EXhalbtc8188c2ant_PowerOnSetting(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
}

VOID
EXhalbtc8188c2ant_InitHwConfig(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN				bWifiOnly
	)
{
	u1Byte	u1Tmp=0;

	RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], 2Ant Init HW Config!!\n"));

	// backup rf 0x1e value
	pCoexDm->btRf0x1eBackup = 
		pBtCoexist->fBtcGetRfReg(pBtCoexist, BTC_RF_A, 0x1e, 0xf0);

	if( (BTC_CHIP_CSR_BC4 == pBtCoexist->boardInfo.btChipType) ||
		(BTC_CHIP_CSR_BC8 == pBtCoexist->boardInfo.btChipType) )
	{
		u1Tmp = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x4fd) & BIT0;
		pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x4fd, u1Tmp);
		
		halbtc8188c2ant_CoexTable(pBtCoexist, FORCE_EXEC, 0xaaaa9aaa, 0xffbd0040, 0x40000010);
	}
}

VOID
EXhalbtc8188c2ant_InitCoexDm(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Coex Mechanism Init!!\n"));
	
	halbtc8188c2ant_InitCoexDm(pBtCoexist);
}

VOID
EXhalbtc8188c2ant_DisplayCoexInfo(
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
EXhalbtc8188c2ant_IpsNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			type
	)
{
	if(BTC_IPS_ENTER == type)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], IPS ENTER notify\n"));
		halbtc8188c2ant_CoexAllOff(pBtCoexist);
	}
	else if(BTC_IPS_LEAVE == type)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], IPS LEAVE notify\n"));
		//halbtc8188c2ant_InitCoexDm(pBtCoexist);
	}
}

VOID
EXhalbtc8188c2ant_LpsNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			type
	)
{
	if(BTC_LPS_ENABLE == type)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], LPS ENABLE notify\n"));
		halbtc8188c2ant_CoexAllOff(pBtCoexist);
	}
	else if(BTC_LPS_DISABLE == type)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], LPS DISABLE notify\n"));
		halbtc8188c2ant_InitCoexDm(pBtCoexist);
	}
}

VOID
EXhalbtc8188c2ant_ScanNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			type
	)
{
	if(BTC_SCAN_START == type)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], SCAN START notify\n"));
	}
	else if(BTC_SCAN_FINISH == type)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], SCAN FINISH notify\n"));
	}
}

VOID
EXhalbtc8188c2ant_ConnectNotify(
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
EXhalbtc8188c2ant_MediaStatusNotify(
	IN	PBTC_COEXIST			pBtCoexist,
	IN	u1Byte				type
	)
{	
	if(BTC_MEDIA_CONNECT == type)
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], MEDIA connect notify\n"));
	}
	else
	{
		RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], MEDIA disconnect notify\n"));
	}
	
}

VOID
EXhalbtc8188c2ant_SpecialPacketNotify(
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
EXhalbtc8188c2ant_BtInfoNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	pu1Byte			tmpBuf,
	IN	u1Byte			length
	)
{
}

VOID
EXhalbtc8188c2ant_HaltNotify(
	IN	PBTC_COEXIST			pBtCoexist
	)
{
	RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Halt notify\n"));

	EXhalbtc8188c2ant_MediaStatusNotify(pBtCoexist, BTC_MEDIA_DISCONNECT);
}

VOID
EXhalbtc8188c2ant_Periodical(
	IN	PBTC_COEXIST			pBtCoexist
	)
{
	u1Byte	algorithm;

	RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], 2Ant Periodical!!\n"));

	// NOTE:
	// sw mechanism must be done after fw mechanism
	// 

	if((BTC_CHIP_CSR_BC4 == pBtCoexist->boardInfo.btChipType) ||
		(BTC_CHIP_CSR_BC8 == pBtCoexist->boardInfo.btChipType) )
	{
		pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_ACT_GET_BT_RSSI, NULL);

		halbtc8188c2ant_MonitorBtState(pBtCoexist);
		algorithm = halbtc8188c2ant_ActionAlgorithm(pBtCoexist);	
		pCoexDm->curAlgorithm = algorithm;
		switch(pCoexDm->curAlgorithm)
		{
			case BT_8188C_2ANT_COEX_ALGO_SCO:
				RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Action 2-Ant, algorithm = SCO\n"));
				halbtc8188c2ant_ActionSco(pBtCoexist);
				break;
			case BT_8188C_2ANT_COEX_ALGO_HID:
				RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Action 2-Ant, algorithm = HID\n"));
				halbtc8188c2ant_ActionHid(pBtCoexist);
				break;
			case BT_8188C_2ANT_COEX_ALGO_A2DP:
				RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Action 2-Ant, algorithm = A2DP\n"));
				halbtc8188c2ant_ActionA2dp(pBtCoexist);
				break;
			case BT_8188C_2ANT_COEX_ALGO_PAN:
				RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Action 2-Ant, algorithm = PAN\n"));
				halbtc8188c2ant_ActionPan(pBtCoexist);
				break;
			case BT_8188C_2ANT_COEX_ALGO_HID_A2DP:
				RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Action 2-Ant, algorithm = HID+A2DP\n"));
				halbtc8188c2ant_ActionHidA2dp(pBtCoexist);
				break;
			case BT_8188C_2ANT_COEX_ALGO_HID_PAN:
				RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Action 2-Ant, algorithm = PAN+HID\n"));
				halbtc8188c2ant_ActionHidPan(pBtCoexist);
				break;
			case BT_8188C_2ANT_COEX_ALGO_PAN_A2DP:
				RT_TRACE(COMP_COEX, DBG_LOUD, ("[BTCoex], Action 2-Ant, algorithm = PAN+A2DP\n"));
				halbtc8188c2ant_ActionPanA2dp(pBtCoexist);
				break;
			default:
				break;
		}
	}
}


#endif

