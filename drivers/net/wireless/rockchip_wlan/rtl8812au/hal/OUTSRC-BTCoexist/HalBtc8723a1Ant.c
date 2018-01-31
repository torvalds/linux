/* SPDX-License-Identifier: GPL-2.0 */
//============================================================
// Description:
//
// This file is for RTL8723A Co-exist mechanism
//
// History
// 2012/08/22 Cosa first check in.
// 2012/11/14 Cosa Revise for 8723A 1Ant out sourcing.
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
static COEX_DM_8723A_1ANT	GLCoexDm8723a1Ant;
static PCOEX_DM_8723A_1ANT 	pCoexDm=&GLCoexDm8723a1Ant;
static COEX_STA_8723A_1ANT	GLCoexSta8723a1Ant;
static PCOEX_STA_8723A_1ANT	pCoexSta=&GLCoexSta8723a1Ant;

const char *const GLBtInfoSrc8723a1Ant[]={
	"BT Info[wifi fw]",
	"BT Info[bt rsp]",
	"BT Info[bt auto report]",
};

//============================================================
// local function proto type if needed
//============================================================
//============================================================
// local function start with halbtc8723a1ant_
//============================================================
VOID
halbtc8723a1ant_Reg0x550Bit3(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bSet
	)
{
	u1Byte	u1tmp=0;
	
	u1tmp = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x550);
	if(bSet)
	{
		u1tmp |= BIT3;
	}
	else
	{
		u1tmp &= ~BIT3;
	}
	pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x550, u1tmp);
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], set 0x550[3]=%d\n", (bSet? 1:0)));
}

VOID
halbtc8723a1ant_NotifyFwScan(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			scanType
	)
{
	u1Byte			H2C_Parameter[1] ={0};
	
	if(BTC_SCAN_START == scanType)
		H2C_Parameter[0] = 0x1;

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_EXEC, ("[BTCoex], Notify FW for wifi scan, write 0x3b=0x%x\n", 
		H2C_Parameter[0]));

	pBtCoexist->fBtcFillH2c(pBtCoexist, 0x3b, 1, H2C_Parameter);
}

VOID
halbtc8723a1ant_QueryBtInfo(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	u1Byte			H2C_Parameter[1] ={0};

	pCoexSta->bC2hBtInfoReqSent = TRUE;

	H2C_Parameter[0] |= BIT0;	// trigger

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_EXEC, ("[BTCoex], Query Bt Info, FW write 0x38=0x%x\n", 
		H2C_Parameter[0]));

	pBtCoexist->fBtcFillH2c(pBtCoexist, 0x38, 1, H2C_Parameter);
}

VOID
halbtc8723a1ant_SetSwRfRxLpfCorner(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bRxRfShrinkOn
	)
{
	if(bRxRfShrinkOn)
	{
		//Shrink RF Rx LPF corner
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC, ("[BTCoex], Shrink RF Rx LPF corner!!\n"));
		pBtCoexist->fBtcSetRfReg(pBtCoexist, BTC_RF_A, 0x1e, 0xfffff, 0xf0ff7);
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
halbtc8723a1ant_RfShrink(
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
	halbtc8723a1ant_SetSwRfRxLpfCorner(pBtCoexist, pCoexDm->bCurRfRxLpfShrink);

	pCoexDm->bPreRfRxLpfShrink = pCoexDm->bCurRfRxLpfShrink;
}

VOID
halbtc8723a1ant_SetSwPenaltyTxRateAdaptive(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bLowPenaltyRa
	)
{
	u1Byte	tmpU1;

	tmpU1 = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x4fd);
	tmpU1 |= BIT0;
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
halbtc8723a1ant_LowPenaltyRa(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bForceExec,
	IN	BOOLEAN			bLowPenaltyRa
	)
{
	return;
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
	halbtc8723a1ant_SetSwPenaltyTxRateAdaptive(pBtCoexist, pCoexDm->bCurLowPenaltyRa);

	pCoexDm->bPreLowPenaltyRa = pCoexDm->bCurLowPenaltyRa;
}

VOID
halbtc8723a1ant_SetCoexTable(
	IN	PBTC_COEXIST	pBtCoexist,
	IN	u4Byte		val0x6c0,
	IN	u4Byte		val0x6c8,
	IN	u1Byte		val0x6cc
	)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC, ("[BTCoex], set coex table, set 0x6c0=0x%x\n", val0x6c0));
	pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x6c0, val0x6c0);

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC, ("[BTCoex], set coex table, set 0x6c8=0x%x\n", val0x6c8));
	pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x6c8, val0x6c8);

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC, ("[BTCoex], set coex table, set 0x6cc=0x%x\n", val0x6cc));
	pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x6cc, val0x6cc);
}

VOID
halbtc8723a1ant_CoexTable(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bForceExec,
	IN	u4Byte			val0x6c0,
	IN	u4Byte			val0x6c8,
	IN	u1Byte			val0x6cc
	)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW, ("[BTCoex], %s write Coex Table 0x6c0=0x%x, 0x6c8=0x%x, 0x6cc=0x%x\n", 
		(bForceExec? "force to":""), val0x6c0, val0x6c8, val0x6cc));
	pCoexDm->curVal0x6c0 = val0x6c0;
	pCoexDm->curVal0x6c8 = val0x6c8;
	pCoexDm->curVal0x6cc = val0x6cc;

	if(!bForceExec)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_DETAIL, ("[BTCoex], preVal0x6c0=0x%x, preVal0x6c8=0x%x, preVal0x6cc=0x%x !!\n", 
			pCoexDm->preVal0x6c0, pCoexDm->preVal0x6c8, pCoexDm->preVal0x6cc));
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_DETAIL, ("[BTCoex], curVal0x6c0=0x%x, curVal0x6c8=0x%x, curVal0x6cc=0x%x !!\n", 
			pCoexDm->curVal0x6c0, pCoexDm->curVal0x6c8, pCoexDm->curVal0x6cc));
	
		if( (pCoexDm->preVal0x6c0 == pCoexDm->curVal0x6c0) &&
			(pCoexDm->preVal0x6c8 == pCoexDm->curVal0x6c8) &&
			(pCoexDm->preVal0x6cc == pCoexDm->curVal0x6cc) )
			return;
	}
	halbtc8723a1ant_SetCoexTable(pBtCoexist, val0x6c0, val0x6c8, val0x6cc);

	pCoexDm->preVal0x6c0 = pCoexDm->curVal0x6c0;
	pCoexDm->preVal0x6c8 = pCoexDm->curVal0x6c8;
	pCoexDm->preVal0x6cc = pCoexDm->curVal0x6cc;
}

VOID
halbtc8723a1ant_SetFwIgnoreWlanAct(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bEnable
	)
{
	u1Byte			H2C_Parameter[1] ={0};
		
	if(bEnable)
	{
		H2C_Parameter[0] |= BIT0;		// function enable
	}
	
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_EXEC, ("[BTCoex], set FW for BT Ignore Wlan_Act, FW write 0x25=0x%x\n", 
		H2C_Parameter[0]));

	pBtCoexist->fBtcFillH2c(pBtCoexist, 0x25, 1, H2C_Parameter);	
}

VOID
halbtc8723a1ant_IgnoreWlanAct(
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
	halbtc8723a1ant_SetFwIgnoreWlanAct(pBtCoexist, bEnable);

	pCoexDm->bPreIgnoreWlanAct = pCoexDm->bCurIgnoreWlanAct;
}

VOID
halbtc8723a1ant_SetFwPstdma(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			type,
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

	// byte1[1:0] != 0 means enable pstdma
	// for 2Ant bt coexist, if byte1 != 0 means enable pstdma
	if(byte1)
	{
		if(bApEnable)
		{
			if(type != 5 && type != 12)
			{
				BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], FW for 1Ant AP mode\n"));
				realByte1 &= ~BIT4;
				realByte1 |= BIT5;

				realByte5 |= BIT5;
				realByte5 &= ~BIT6;
			}
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
	
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_EXEC, ("[BTCoex], FW write 0x3a(5bytes)=0x%x%08x\n", 
		H2C_Parameter[0], 
		H2C_Parameter[1]<<24|H2C_Parameter[2]<<16|H2C_Parameter[3]<<8|H2C_Parameter[4]));

	pBtCoexist->fBtcFillH2c(pBtCoexist, 0x3a, 5, H2C_Parameter);
}

VOID
halbtc8723a1ant_PsTdma(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	BOOLEAN			bForceExec,
	IN	BOOLEAN			bTurnOn,
	IN	u1Byte			type
	)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW, ("[BTCoex], %s turn %s PS TDMA, type=%d\n", 
		(bForceExec? "force to":""), (bTurnOn? "ON":"OFF"), type));		
	pCoexDm->bCurPsTdmaOn = bTurnOn;
	pCoexDm->curPsTdma = type;

	if(!bForceExec)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], bPrePsTdmaOn = %d, bCurPsTdmaOn = %d!!\n", 
			pCoexDm->bPrePsTdmaOn, pCoexDm->bCurPsTdmaOn));
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], prePsTdma = %d, curPsTdma = %d!!\n", 
			pCoexDm->prePsTdma, pCoexDm->curPsTdma));

		if( (pCoexDm->bPrePsTdmaOn == pCoexDm->bCurPsTdmaOn) &&
			(pCoexDm->prePsTdma == pCoexDm->curPsTdma) )
			return;
	}	
	if(pCoexDm->bCurPsTdmaOn)
	{
		switch(pCoexDm->curPsTdma)
		{
			case 1:
			default:
				halbtc8723a1ant_SetFwPstdma(pBtCoexist, type, 0x13, 0x1a, 0x1a, 0x0, 0x40);
				break;
			case 2:
				halbtc8723a1ant_SetFwPstdma(pBtCoexist, type, 0x13, 0x12, 0x12, 0x0, 0x40);
				break;
			case 3:
				halbtc8723a1ant_SetFwPstdma(pBtCoexist, type, 0x93, 0x3f, 0x3, 0x10, 0x40);
				break;
			case 4:
				halbtc8723a1ant_SetFwPstdma(pBtCoexist, type, 0x93, 0x15, 0x3, 0x10, 0x0);
				break;
			case 5:
				halbtc8723a1ant_SetFwPstdma(pBtCoexist, type, 0xa9, 0x15, 0x3, 0x35, 0xc0);
				break;
			
			case 8: 
				halbtc8723a1ant_SetFwPstdma(pBtCoexist, type, 0x93, 0x25, 0x3, 0x10, 0x0);
				break;
			case 9: 
				halbtc8723a1ant_SetFwPstdma(pBtCoexist, type, 0x13, 0xa, 0xa, 0x0, 0x40);
				break;
			case 10:	
				halbtc8723a1ant_SetFwPstdma(pBtCoexist, type, 0x13, 0xa, 0xa, 0x0, 0x40);
				break;
			case 11:	
				halbtc8723a1ant_SetFwPstdma(pBtCoexist, type, 0x13, 0x5, 0x5, 0x0, 0x40);
				break;
			case 12:
				halbtc8723a1ant_SetFwPstdma(pBtCoexist, type, 0xa9, 0xa, 0x3, 0x15, 0xc0);
				break;
	
			case 18:
				halbtc8723a1ant_SetFwPstdma(pBtCoexist, type, 0x93, 0x25, 0x3, 0x10, 0x0);
				break;			

			case 20:
				halbtc8723a1ant_SetFwPstdma(pBtCoexist, type, 0x13, 0x2a, 0x2a, 0x0, 0x0);
				break;
			case 21:
				halbtc8723a1ant_SetFwPstdma(pBtCoexist, type, 0x93, 0x20, 0x3, 0x10, 0x40);
				break;
			case 22:
				halbtc8723a1ant_SetFwPstdma(pBtCoexist, type, 0x13, 0x1a, 0x1a, 0x2, 0x40);
				break;
			case 23:
				halbtc8723a1ant_SetFwPstdma(pBtCoexist, type, 0x13, 0x12, 0x12, 0x2, 0x40);
				break;
			case 24:
				halbtc8723a1ant_SetFwPstdma(pBtCoexist, type, 0x13, 0xa, 0xa, 0x2, 0x40);
				break;
			case 25:
				halbtc8723a1ant_SetFwPstdma(pBtCoexist, type, 0x13, 0x5, 0x5, 0x2, 0x40);
				break;
			case 26:
				halbtc8723a1ant_SetFwPstdma(pBtCoexist, type, 0x93, 0x25, 0x3, 0x10, 0x0);
				break;
			case 27:
				halbtc8723a1ant_SetFwPstdma(pBtCoexist, type, 0x13, 0x5, 0x5, 0x2, 0x40);
				break;
			case 28:
				halbtc8723a1ant_SetFwPstdma(pBtCoexist, type, 0x3, 0x2f, 0x2f, 0x0, 0x0);
				break;

		}
	}
	else
	{
		// disable PS tdma
		switch(pCoexDm->curPsTdma)
		{
			case 8:
				halbtc8723a1ant_SetFwPstdma(pBtCoexist, type, 0x8, 0x0, 0x0, 0x0, 0x0);		
				break;
			case 0:
			default:
				halbtc8723a1ant_SetFwPstdma(pBtCoexist, type, 0x0, 0x0, 0x0, 0x0, 0x0);
				pBtCoexist->fBtcWrite2Byte(pBtCoexist, 0x860, 0x210);
				break;
			case 9:
				halbtc8723a1ant_SetFwPstdma(pBtCoexist, type, 0x0, 0x0, 0x0, 0x0, 0x0);
				pBtCoexist->fBtcWrite2Byte(pBtCoexist, 0x860, 0x110);
				break;

		}
	}

	// update pre state
	pCoexDm->bPrePsTdmaOn = pCoexDm->bCurPsTdmaOn;
	pCoexDm->prePsTdma = pCoexDm->curPsTdma;
}


VOID
halbtc8723a1ant_CoexAllOff(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	// fw all off
	halbtc8723a1ant_IgnoreWlanAct(pBtCoexist, NORMAL_EXEC, FALSE);
	halbtc8723a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, FALSE, 0);

	// sw all off
	halbtc8723a1ant_LowPenaltyRa(pBtCoexist, NORMAL_EXEC, FALSE);
	halbtc8723a1ant_RfShrink(pBtCoexist, NORMAL_EXEC, FALSE);

	// hw all off
	halbtc8723a1ant_CoexTable(pBtCoexist, NORMAL_EXEC, 0x55555555, 0xffff, 0x3);
}

VOID
halbtc8723a1ant_InitCoexDm(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	// force to reset coex mechanism
	halbtc8723a1ant_IgnoreWlanAct(pBtCoexist, FORCE_EXEC, FALSE);
}

VOID
halbtc8723a1ant_BtEnableAction(
	IN 	PBTC_COEXIST		pBtCoexist
	)
{
	halbtc8723a1ant_IgnoreWlanAct(pBtCoexist, FORCE_EXEC, FALSE);
}

VOID
halbtc8723a1ant_MonitorBtCtr(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	u4Byte 			regHPTxRx, regLPTxRx, u4Tmp;
	u4Byte			regHPTx=0, regHPRx=0, regLPTx=0, regLPRx=0;
	u1Byte			u1Tmp;
	
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

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_MONITOR, ("[BTCoex], High Priority Tx/Rx (reg 0x%x)=0x%x(%d)/0x%x(%d)\n", 
		regHPTxRx, regHPTx, regHPTx, regHPRx, regHPRx));
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_MONITOR, ("[BTCoex], Low Priority Tx/Rx (reg 0x%x)=0x%x(%d)/0x%x(%d)\n", 
		regLPTxRx, regLPTx, regLPTx, regLPRx, regLPRx));

	// reset counter
	pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x76e, 0xc);
}

VOID
halbtc8723a1ant_MonitorBtEnableDisable(
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
		bPreBtDisabled = bBtDisabled;
		if(!bBtDisabled)
		{
			halbtc8723a1ant_BtEnableAction(pBtCoexist);
		}
		else
		{
			pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_ACT_NORMAL_LPS, NULL);
		}
	}
}

VOID
halbtc8723a1ant_TdmaDurationAdjust(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	static s4Byte		up,dn,m,n,WaitCount;
	s4Byte			result;   //0: no change, +1: increase WiFi duration, -1: decrease WiFi duration
	u1Byte			retryCount=0;
	u1Byte			btState;
	BOOLEAN			bScan=FALSE, bLink=FALSE, bRoam=FALSE;
	u4Byte			wifiBw;
	
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_U4_WIFI_BW, &wifiBw);
	btState = pCoexDm->btStatus;

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], TdmaDurationAdjust()\n"));
	if(pCoexDm->psTdmaGlobalCnt != pCoexDm->psTdmaMonitorCnt)
	{
		pCoexDm->psTdmaMonitorCnt = 0;
		pCoexDm->psTdmaGlobalCnt = 0;
	}
	if(pCoexDm->psTdmaMonitorCnt == 0)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], first run BT A2DP + WiFi busy state!!\n"));
		if(btState == BT_STATE_8723A_1ANT_ACL_ONLY_BUSY)
		{
			halbtc8723a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 1);
			pCoexDm->psTdmaDuAdjType = 1;
		}
		else
		{
			halbtc8723a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 22);
			pCoexDm->psTdmaDuAdjType = 22;
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
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], retryCount = %d\n", retryCount));
		result = 0;
		WaitCount++; 
		  
		if(retryCount == 0)  // no retry in the last 2-second duration
		{
			up++;
			dn--;

			if (dn <= 0)
				dn = 0; 			 

			if(up >= n) // if 連續 n 個2秒 retry count為0, 則調寬WiFi duration
			{
				WaitCount = 0; 
				n = 3;
				up = 0;
				dn = 0;
				result = 1; 
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Increase wifi duration!!\n"));
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
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Decrease wifi duration for retryCounter<3!!\n"));
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
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Decrease wifi duration for retryCounter>3!!\n"));
		}
		
		{
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BT TxRx counter H+L <= 1200\n"));
			if(btState != BT_STATE_8723A_1ANT_ACL_ONLY_BUSY)
			{
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], NOT ACL only busy!\n"));
				if(BTC_WIFI_BW_HT40 != wifiBw)
				{
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], 20MHz\n"));
					if(result == -1)
					{
						if(pCoexDm->curPsTdma == 22)
						{
							halbtc8723a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 23);
							pCoexDm->psTdmaDuAdjType = 23;
						}
						else if(pCoexDm->curPsTdma == 23)
						{
							halbtc8723a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 24);
							pCoexDm->psTdmaDuAdjType = 24;
						}
						else if(pCoexDm->curPsTdma == 24)
						{
							halbtc8723a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 25);
							pCoexDm->psTdmaDuAdjType = 25;
						}
					} 
					else if (result == 1)
					{
						if(pCoexDm->curPsTdma == 25)
						{
							halbtc8723a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 24);
							pCoexDm->psTdmaDuAdjType = 24;
						}
						else if(pCoexDm->curPsTdma == 24)
						{
							halbtc8723a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 23);
							pCoexDm->psTdmaDuAdjType = 23;
						}
						else if(pCoexDm->curPsTdma == 23)
						{
							halbtc8723a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 22);
							pCoexDm->psTdmaDuAdjType = 22;
						}
					}
					// error handle, if not in the following state,
					// set psTdma again.
					if( (pCoexDm->psTdmaDuAdjType != 22) &&
						(pCoexDm->psTdmaDuAdjType != 23) &&
						(pCoexDm->psTdmaDuAdjType != 24) &&
						(pCoexDm->psTdmaDuAdjType != 25) )
					{
						BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], duration case out of handle!!\n"));
						halbtc8723a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 23);
						pCoexDm->psTdmaDuAdjType = 23;
					}
				}
				else
				{
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], 40MHz\n"));
					if(result == -1)
					{
						if(pCoexDm->curPsTdma == 23)
						{
							halbtc8723a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 24);
							pCoexDm->psTdmaDuAdjType = 24;
						}
						else if(pCoexDm->curPsTdma == 24)
						{
							halbtc8723a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 25);
							pCoexDm->psTdmaDuAdjType = 25;
						}
						else if(pCoexDm->curPsTdma == 25)
						{
							halbtc8723a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 27);
							pCoexDm->psTdmaDuAdjType = 27;
						}
					} 
					else if (result == 1)
					{
						if(pCoexDm->curPsTdma == 27)
						{
							halbtc8723a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 25);
							pCoexDm->psTdmaDuAdjType = 25;
						}
						else if(pCoexDm->curPsTdma == 25)
						{
							halbtc8723a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 24);
							pCoexDm->psTdmaDuAdjType = 24;
						}
						else if(pCoexDm->curPsTdma == 24)
						{
							halbtc8723a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 23);
							pCoexDm->psTdmaDuAdjType = 23;
						}
					}
					// error handle, if not in the following state,
					// set psTdma again.
					if( (pCoexDm->psTdmaDuAdjType != 23) &&
						(pCoexDm->psTdmaDuAdjType != 24) &&
						(pCoexDm->psTdmaDuAdjType != 25) &&
						(pCoexDm->psTdmaDuAdjType != 27) )
					{
						BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], duration case out of handle!!\n"));
						halbtc8723a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 24);
						pCoexDm->psTdmaDuAdjType = 24;
					}
				}
			}
			else
			{
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], ACL only busy\n"));
				if (result == -1)
				{
					if(pCoexDm->curPsTdma == 1)
					{
						halbtc8723a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 2);
						pCoexDm->psTdmaDuAdjType = 2;
					}
					else if(pCoexDm->curPsTdma == 2)
					{
						halbtc8723a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 9);
						pCoexDm->psTdmaDuAdjType = 9;
					}
					else if(pCoexDm->curPsTdma == 9)
					{
						halbtc8723a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 11);
						pCoexDm->psTdmaDuAdjType = 11;
					}
				}
				else if (result == 1)
				{
					if(pCoexDm->curPsTdma == 11)
					{
						halbtc8723a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 9);
						pCoexDm->psTdmaDuAdjType = 9;
					}
					else if(pCoexDm->curPsTdma == 9)
					{
						halbtc8723a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 2);
						pCoexDm->psTdmaDuAdjType = 2;
					}
					else if(pCoexDm->curPsTdma == 2)
					{
						halbtc8723a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 1);
						pCoexDm->psTdmaDuAdjType = 1;
					}
				}

				// error handle, if not in the following state,
				// set psTdma again.
				if( (pCoexDm->psTdmaDuAdjType != 1) &&
					(pCoexDm->psTdmaDuAdjType != 2) &&
					(pCoexDm->psTdmaDuAdjType != 9) &&
					(pCoexDm->psTdmaDuAdjType != 11) )
				{
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], duration case out of handle!!\n"));
					halbtc8723a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 2);
					pCoexDm->psTdmaDuAdjType = 2;
				}
			}
		}
	}

	// if current PsTdma not match with the recorded one (when scan, dhcp...), 
	// then we have to adjust it back to the previous record one.
	if(pCoexDm->curPsTdma != pCoexDm->psTdmaDuAdjType)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], PsTdma type dismatch!!!, curPsTdma=%d, recordPsTdma=%d\n", 
			pCoexDm->curPsTdma, pCoexDm->psTdmaDuAdjType));

		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_SCAN, &bScan);
		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_LINK, &bLink);
		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_ROAM, &bRoam);

		if( !bScan && !bLink &&	!bRoam)
		{
			halbtc8723a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, pCoexDm->psTdmaDuAdjType);
		}
		else
		{
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], roaming/link/scan is under progress, will adjust next time!!!\n"));
		}
	}
	pCoexDm->psTdmaMonitorCnt++;
}


VOID
halbtc8723a1ant_CoexForWifiConnect(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	BOOLEAN		bWifiConnected=FALSE, bWifiBusy=FALSE;
	u1Byte		btState, btInfoOriginal=0;

	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_CONNECTED, &bWifiConnected);

	btState = pCoexDm->btStatus;
	btInfoOriginal = pCoexSta->btInfoC2h[BT_INFO_SRC_8723A_1ANT_BT_RSP][0];

	if(bWifiConnected)
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], wifi connected!!\n"));
		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_BUSY, &bWifiBusy);
		
		if( !bWifiBusy &&
			((BT_STATE_8723A_1ANT_NO_CONNECTION == btState) ||
			(BT_STATE_8723A_1ANT_CONNECT_IDLE == btState)) )
		{
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], [Wifi is idle] or [Bt is non connected idle or Bt is connected idle]!!\n"));

			if(BT_STATE_8723A_1ANT_NO_CONNECTION == btState)
				halbtc8723a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, FALSE, 9);
			else if(BT_STATE_8723A_1ANT_CONNECT_IDLE == btState)
				halbtc8723a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, FALSE, 0);

			pBtCoexist->fBtcSetBbReg(pBtCoexist, 0x880, 0xff000000, 0xc0);
		}
		else
		{
			if( (BT_STATE_8723A_1ANT_SCO_ONLY_BUSY == btState) ||
				(BT_STATE_8723A_1ANT_ACL_SCO_BUSY == btState) ||
				(BT_STATE_8723A_1ANT_HID_BUSY == btState) ||
				(BT_STATE_8723A_1ANT_HID_SCO_BUSY == btState) )
			{
				pBtCoexist->fBtcSetBbReg(pBtCoexist, 0x880, 0xff000000, 0x60);
			}
			else
			{
				pBtCoexist->fBtcSetBbReg(pBtCoexist, 0x880, 0xff000000, 0xc0);
			}
			switch(btState)
			{
				case BT_STATE_8723A_1ANT_NO_CONNECTION:
					halbtc8723a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 5);
					break;
				case BT_STATE_8723A_1ANT_CONNECT_IDLE:
					halbtc8723a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 12);
					break;
				case BT_STATE_8723A_1ANT_INQ_OR_PAG:
					halbtc8723a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 10);
					break;
				case BT_STATE_8723A_1ANT_SCO_ONLY_BUSY:
				case BT_STATE_8723A_1ANT_ACL_SCO_BUSY:
				case BT_STATE_8723A_1ANT_HID_BUSY:
				case BT_STATE_8723A_1ANT_HID_SCO_BUSY:
					halbtc8723a1ant_TdmaDurationAdjust(pBtCoexist);
					break;
				case BT_STATE_8723A_1ANT_ACL_ONLY_BUSY:
					if (btInfoOriginal&BT_INFO_8723A_1ANT_B_A2DP)
					{
						halbtc8723a1ant_TdmaDurationAdjust(pBtCoexist);
					}
					else if(btInfoOriginal&BT_INFO_8723A_1ANT_B_FTP)
					{
						halbtc8723a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 1);
					}
					else if( (btInfoOriginal&BT_INFO_8723A_1ANT_B_A2DP) &&
							(btInfoOriginal&BT_INFO_8723A_1ANT_B_FTP) )
					{
						halbtc8723a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 6);
					}
					else
					{
						halbtc8723a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 1);
					}
					break;
				default:
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], error!!!, undefined case in halbtc8723a1ant_CoexForWifiConnect()!!\n"));
					break;
			}
		}
	}
	else
	{
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], wifi is disconnected!!\n"));
	}

	pCoexDm->psTdmaGlobalCnt++;
}

//============================================================
// work around function start with wa_halbtc8723a1ant_
//============================================================
VOID
wa_halbtc8723a1ant_MonitorC2h(
	IN	PBTC_COEXIST			pBtCoexist
	)
{
	u1Byte	tmp1b=0x0;
	u4Byte	curC2hTotalCnt=0x0;
	static u4Byte	preC2hTotalCnt=0x0, sameCntPollingTime=0x0;

	curC2hTotalCnt+=pCoexSta->btInfoC2hCnt[BT_INFO_SRC_8723A_1ANT_BT_RSP];

	if(curC2hTotalCnt == preC2hTotalCnt)
	{
		sameCntPollingTime++;
	}
	else
	{
		preC2hTotalCnt = curC2hTotalCnt;
		sameCntPollingTime = 0;
	}

	if(sameCntPollingTime >= 2)
	{
		tmp1b = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x1af);
		if(tmp1b != 0x0)
		{
			pCoexSta->c2hHangDetectCnt++;
			pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x1af, 0x0);
		}
	}
}

//============================================================
// extern function start with EXhalbtc8723a1ant_
//============================================================
VOID
EXhalbtc8723a1ant_InitHwConfig(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	BTC_PRINT(BTC_MSG_INTERFACE, INTF_INIT, ("[BTCoex], 1Ant Init HW Config!!\n"));

	// backup rf 0x1e value
	pCoexDm->btRf0x1eBackup = 
		pBtCoexist->fBtcGetRfReg(pBtCoexist, BTC_RF_A, 0x1e, 0xfffff);

	pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x40, 0x20);

	// enable counter statistics
	pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x76e, 0x4);
	
	// coex table
	pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x6cc, 0x0);			// 1-Ant coex
	pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x6c8, 0xffff);		// wifi break table
	pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x6c4, 0x55555555);	//coex table

	// antenna switch control parameter
	pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x858, 0xaaaaaaaa);
	
	pBtCoexist->fBtcWrite2Byte(pBtCoexist, 0x860, 0x210);	//set antenna at wifi side if ANTSW is software control
	pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x870, 0x300);	//SPDT(connected with TRSW) control by hardware PTA
	pBtCoexist->fBtcWrite4Byte(pBtCoexist, 0x874, 0x22804000);	//ANTSW keep by GNT_BT

	// coexistence parameters
	pBtCoexist->fBtcWrite1Byte(pBtCoexist, 0x778, 0x1);	// enable RTK mode PTA
}

VOID
EXhalbtc8723a1ant_InitCoexDm(
	IN	PBTC_COEXIST		pBtCoexist
	)
{
	BTC_PRINT(BTC_MSG_INTERFACE, INTF_INIT, ("[BTCoex], Coex Mechanism Init!!\n"));
	
	halbtc8723a1ant_InitCoexDm(pBtCoexist);
}

VOID
EXhalbtc8723a1ant_DisplayCoexInfo(
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

	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = [%s/ %d/ %d] ", "BT [status/ rssi/ retryCnt]", \
		((pCoexSta->bC2hBtInquiryPage)?("inquiry/page scan"):((BT_8723A_1ANT_BT_STATUS_IDLE == pCoexDm->btStatus)? "idle":(  (BT_8723A_1ANT_BT_STATUS_CONNECTED_IDLE == pCoexDm->btStatus)? "connected-idle":"busy"))),
		pCoexSta->btRssi, pCoexSta->btRetryCnt);
	CL_PRINTF(cliBuf);
	
	if(pStackInfo->bProfileNotified)
	{			
		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d / %d / %d", "SCO/HID/PAN/A2DP", \
			pStackInfo->bScoExist, pStackInfo->bHidExist, pStackInfo->bPanExist, pStackInfo->bA2dpExist);
		CL_PRINTF(cliBuf);	

		pBtCoexist->fBtcDispDbgMsg(pBtCoexist, BTC_DBG_DISP_BT_LINK_INFO);
	}

	btInfoExt = pCoexSta->btInfoExt;
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s", "BT Info A2DP rate", \
		(btInfoExt&BIT0)? "Basic rate":"EDR rate");
	CL_PRINTF(cliBuf);	

	for(i=0; i<BT_INFO_SRC_8723A_1ANT_MAX; i++)
	{
		if(pCoexSta->btInfoC2hCnt[i])
		{				
			CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %02x %02x %02x %02x %02x %02x %02x(%d)", GLBtInfoSrc8723a1Ant[i], \
				pCoexSta->btInfoC2h[i][0], pCoexSta->btInfoC2h[i][1],
				pCoexSta->btInfoC2h[i][2], pCoexSta->btInfoC2h[i][3],
				pCoexSta->btInfoC2h[i][4], pCoexSta->btInfoC2h[i][5],
				pCoexSta->btInfoC2h[i][6], pCoexSta->btInfoC2hCnt[i]);
			CL_PRINTF(cliBuf);
		}
	}

	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d", "write 0x1af=0x0 num", \
		pCoexSta->c2hHangDetectCnt);
	CL_PRINTF(cliBuf);
	
	// Sw mechanism	
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "============[Sw mechanism]============");
	CL_PRINTF(cliBuf);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d", "SM1[ShRf/ LpRA/ LimDig]", \
		pCoexDm->bCurRfRxLpfShrink, pCoexDm->bCurLowPenaltyRa, pCoexDm->bLimitedDig);
	CL_PRINTF(cliBuf);

	// Fw mechanism		
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "============[Fw mechanism]============");
	CL_PRINTF(cliBuf);	
	
	if(!pBtCoexist->bManualControl)
	{
		psTdmaCase = pCoexDm->curPsTdma;
		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %02x %02x %02x %02x %02x case-%d", "PS TDMA", \
			pCoexDm->psTdmaPara[0], pCoexDm->psTdmaPara[1],
			pCoexDm->psTdmaPara[2], pCoexDm->psTdmaPara[3],
			pCoexDm->psTdmaPara[4], psTdmaCase);
		CL_PRINTF(cliBuf);
	
		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d ", "IgnWlanAct", \
			pCoexDm->bCurIgnoreWlanAct);
		CL_PRINTF(cliBuf);
	}

	// Hw setting		
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "============[Hw setting]============");
	CL_PRINTF(cliBuf);	

	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "RF-A, 0x1e initVal", \
		pCoexDm->btRf0x1eBackup);
	CL_PRINTF(cliBuf);

	u1Tmp[0] = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x778);
	u1Tmp[1] = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x783);
	u1Tmp[2] = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x796);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x/ 0x%x", "0x778/ 0x783/ 0x796", \
		u1Tmp[0], u1Tmp[1], u1Tmp[2]);
	CL_PRINTF(cliBuf);

	u4Tmp[0] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x880);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0x880", \
		u4Tmp[0]);
	CL_PRINTF(cliBuf);

	u1Tmp[0] = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x40);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0x40", \
		u1Tmp[0]);
	CL_PRINTF(cliBuf);

	u4Tmp[0] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x550);
	u1Tmp[0] = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x522);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x", "0x550(bcn ctrl)/0x522", \
		u4Tmp[0], u1Tmp[0]);
	CL_PRINTF(cliBuf);

	u4Tmp[0] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x484);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0x484(rate adaptive)", \
		u4Tmp[0]);
	CL_PRINTF(cliBuf);

	u4Tmp[0] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0xc50);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0xc50(dig)", \
		u4Tmp[0]);
	CL_PRINTF(cliBuf);

	u4Tmp[0] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0xda0);
	u4Tmp[1] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0xda4);
	u4Tmp[2] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0xda8);
	u4Tmp[3] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0xdac);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x/ 0x%x/ 0x%x", "0xda0/0xda4/0xda8/0xdac(FA cnt)", \
		u4Tmp[0], u4Tmp[1], u4Tmp[2], u4Tmp[3]);
	CL_PRINTF(cliBuf);

	u4Tmp[0] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x6c0);
	u4Tmp[1] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x6c4);
	u4Tmp[2] = pBtCoexist->fBtcRead4Byte(pBtCoexist, 0x6c8);
	u1Tmp[0] = pBtCoexist->fBtcRead1Byte(pBtCoexist, 0x6cc);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x/ 0x%x/ 0x%x", "0x6c0/0x6c4/0x6c8/0x6cc(coexTable)", \
		u4Tmp[0], u4Tmp[1], u4Tmp[2], u1Tmp[0]);
	CL_PRINTF(cliBuf);

	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d", "0x770 (hp rx[31:16]/tx[15:0])", \
		pCoexSta->highPriorityRx, pCoexSta->highPriorityTx);
	CL_PRINTF(cliBuf);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d", "0x774(lp rx[31:16]/tx[15:0])", \
		pCoexSta->lowPriorityRx, pCoexSta->lowPriorityTx);
	CL_PRINTF(cliBuf);

	pBtCoexist->fBtcDispDbgMsg(pBtCoexist, BTC_DBG_DISP_COEX_STATISTICS);
}


VOID
EXhalbtc8723a1ant_IpsNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			type
	)
{
	if(BTC_IPS_ENTER == type)
	{
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], IPS ENTER notify\n"));
		halbtc8723a1ant_CoexAllOff(pBtCoexist);
	}
	else if(BTC_IPS_LEAVE == type)
	{
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], IPS LEAVE notify\n"));
		//halbtc8723a1ant_InitCoexDm(pBtCoexist);
	}
}

VOID
EXhalbtc8723a1ant_LpsNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			type
	)
{
	if(BTC_LPS_ENABLE == type)
	{
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], LPS ENABLE notify\n"));
	}
	else if(BTC_LPS_DISABLE == type)
	{
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], LPS DISABLE notify\n"));
		halbtc8723a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, FALSE, 8);
	}
}

VOID
EXhalbtc8723a1ant_ScanNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			type
	)
{
	BOOLEAN		bWifiConnected=FALSE;
	
	halbtc8723a1ant_NotifyFwScan(pBtCoexist, type);

	if(pBtCoexist->btInfo.bBtDisabled)
	{
		halbtc8723a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, FALSE, 9); 
	}
	else
	{
		pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_CONNECTED, &bWifiConnected);
		if(BTC_SCAN_START == type)
		{
			BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], SCAN START notify\n"));
			if(!bWifiConnected)	// non-connected scan
			{
				//set 0x550[3]=1 before PsTdma
				halbtc8723a1ant_Reg0x550Bit3(pBtCoexist, TRUE);
			}

			halbtc8723a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 4);
		}
		else if(BTC_SCAN_FINISH == type)
		{
			BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], SCAN FINISH notify\n"));
			if(!bWifiConnected)	// non-connected scan
			{
				halbtc8723a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, FALSE, 0); 
			}
			else
			{
				halbtc8723a1ant_CoexForWifiConnect(pBtCoexist);
			}
		}
	}
}

VOID
EXhalbtc8723a1ant_ConnectNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	u1Byte			type
	)
{
	BOOLEAN		bWifiConnected=FALSE;
		
	if(pBtCoexist->btInfo.bBtDisabled)
	{
		halbtc8723a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, FALSE, 9); 
	}
	else
	{
		if(BTC_ASSOCIATE_START == type)
		{
			BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], CONNECT START notify\n"));
			//set 0x550[3]=1 before PsTdma
			halbtc8723a1ant_Reg0x550Bit3(pBtCoexist, TRUE);
			halbtc8723a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 8);	// extend wifi slot	
		}
		else if(BTC_ASSOCIATE_FINISH == type)
		{
			BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], CONNECT FINISH notify\n"));
			pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_CONNECTED, &bWifiConnected);
			if(!bWifiConnected)	// non-connected scan
			{
				halbtc8723a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, FALSE, 0);
			}
			else
			{
				halbtc8723a1ant_CoexForWifiConnect(pBtCoexist);
			}
		}
	}
}

VOID
EXhalbtc8723a1ant_MediaStatusNotify(
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
EXhalbtc8723a1ant_SpecialPacketNotify(
	IN	PBTC_COEXIST			pBtCoexist,
	IN	u1Byte				type
	)
{
	if(type == BTC_PACKET_DHCP)
	{
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], DHCP Packet notify\n"));
		if(pBtCoexist->btInfo.bBtDisabled)
		{
			halbtc8723a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, FALSE, 9);	
		}
		else
		{
			halbtc8723a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, TRUE, 18);
		}		
	}
}

VOID
EXhalbtc8723a1ant_BtInfoNotify(
	IN	PBTC_COEXIST		pBtCoexist,
	IN	pu1Byte			tmpBuf,
	IN	u1Byte			length
	)
{
	u1Byte			btInfo=0;
	u1Byte			i, rspSource=0;
	BOOLEAN			bBtHsOn=FALSE, bBtBusy=FALSE, bForceLps=FALSE;

	pCoexSta->bC2hBtInfoReqSent = FALSE;
	
	rspSource = BT_INFO_SRC_8723A_1ANT_BT_RSP;
	pCoexSta->btInfoC2hCnt[rspSource]++;

	BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], Bt info[%d], length=%d, hex data=[", rspSource, length));
	for(i=0; i<length; i++)
	{
		pCoexSta->btInfoC2h[rspSource][i] = tmpBuf[i];
		if(i == 0)
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

	if(BT_INFO_SRC_8723A_1ANT_WIFI_FW != rspSource)
	{
		pCoexSta->btRetryCnt =
			pCoexSta->btInfoC2h[rspSource][1];

		pCoexSta->btRssi =
			pCoexSta->btInfoC2h[rspSource][2]*2+10;

		pCoexSta->btInfoExt = 
			pCoexSta->btInfoC2h[rspSource][3];
	}
		
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_HS_OPERATION, &bBtHsOn);
	// check BIT2 first ==> check if bt is under inquiry or page scan
	if(btInfo & BT_INFO_8723A_1ANT_B_INQ_PAGE)
	{
		pCoexSta->bC2hBtInquiryPage = TRUE;
	}
	else
	{
		pCoexSta->bC2hBtInquiryPage = FALSE;
	}
	btInfo &= ~BIT2;
	if(!(btInfo & BIT0))
	{
		pCoexDm->btStatus = BT_STATE_8723A_1ANT_NO_CONNECTION;
		bForceLps = FALSE;
	}
	else
	{
		bForceLps = TRUE;
		if(btInfo == 0x1)
		{
			pCoexDm->btStatus = BT_STATE_8723A_1ANT_CONNECT_IDLE;
		}
		else if(btInfo == 0x9)
		{
			pCoexDm->btStatus = BT_STATE_8723A_1ANT_ACL_ONLY_BUSY;
			bBtBusy = TRUE;
		}
		else if(btInfo == 0x13)
		{
			pCoexDm->btStatus = BT_STATE_8723A_1ANT_SCO_ONLY_BUSY;
			bBtBusy = TRUE;
		}
		else if(btInfo == 0x1b)
		{
			pCoexDm->btStatus = BT_STATE_8723A_1ANT_ACL_SCO_BUSY;
			bBtBusy = TRUE;
		}
		else if(btInfo == 0x29)
		{
			pCoexDm->btStatus = BT_STATE_8723A_1ANT_HID_BUSY;
			bBtBusy = TRUE;
		}
		else if(btInfo == 0x3b)
		{
			pCoexDm->btStatus = BT_STATE_8723A_1ANT_HID_SCO_BUSY;
			bBtBusy = TRUE;
		}
	}
	pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_BL_BT_TRAFFIC_BUSY, &bBtBusy);
	pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_BL_BT_LIMITED_DIG, &bBtBusy);
	if(bForceLps)
		pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_ACT_ENTER_LPS, NULL);
	else
		pBtCoexist->fBtcSet(pBtCoexist, BTC_SET_ACT_NORMAL_LPS, NULL);

	if( (BT_STATE_8723A_1ANT_NO_CONNECTION == pCoexDm->btStatus) ||
		(BT_STATE_8723A_1ANT_CONNECT_IDLE == pCoexDm->btStatus) )
	{
		if(pCoexSta->bC2hBtInquiryPage)
			pCoexDm->btStatus = BT_STATE_8723A_1ANT_INQ_OR_PAG;
	}
}

VOID
EXhalbtc8723a1ant_HaltNotify(
	IN	PBTC_COEXIST			pBtCoexist
	)
{
	halbtc8723a1ant_PsTdma(pBtCoexist, FORCE_EXEC, FALSE, 0);
	
	halbtc8723a1ant_LowPenaltyRa(pBtCoexist, FORCE_EXEC, FALSE);
	halbtc8723a1ant_RfShrink(pBtCoexist, FORCE_EXEC, FALSE);

	halbtc8723a1ant_IgnoreWlanAct(pBtCoexist, FORCE_EXEC, TRUE);
	EXhalbtc8723a1ant_MediaStatusNotify(pBtCoexist, BTC_MEDIA_DISCONNECT);
}

VOID
EXhalbtc8723a1ant_Periodical(
	IN	PBTC_COEXIST			pBtCoexist
	)
{
	BOOLEAN		bScan=FALSE, bLink=FALSE, bRoam=FALSE, bWifiConnected=FALSE;
	
	BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], 1Ant Periodical!!\n"));
	
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_SCAN, &bScan);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_LINK, &bLink);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_ROAM, &bRoam);
	pBtCoexist->fBtcGet(pBtCoexist, BTC_GET_BL_WIFI_CONNECTED, &bWifiConnected);

	// work around for c2h hang
	wa_halbtc8723a1ant_MonitorC2h(pBtCoexist);	

	halbtc8723a1ant_QueryBtInfo(pBtCoexist);
	halbtc8723a1ant_MonitorBtCtr(pBtCoexist);
	halbtc8723a1ant_MonitorBtEnableDisable(pBtCoexist);

	
	if(bScan)
		return;
	if(bLink)
		return;

	if(bWifiConnected)
	{
		if(pBtCoexist->btInfo.bBtDisabled)
		{
			halbtc8723a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, FALSE, 9);
			
			halbtc8723a1ant_LowPenaltyRa(pBtCoexist, NORMAL_EXEC, FALSE);
			halbtc8723a1ant_RfShrink(pBtCoexist, NORMAL_EXEC, FALSE);
		}
		else
		{
			halbtc8723a1ant_LowPenaltyRa(pBtCoexist, NORMAL_EXEC, TRUE);
			halbtc8723a1ant_RfShrink(pBtCoexist, NORMAL_EXEC, FALSE);
			halbtc8723a1ant_CoexForWifiConnect(pBtCoexist);
		}
	}
	else
	{
		halbtc8723a1ant_PsTdma(pBtCoexist, NORMAL_EXEC, FALSE, 0);

		halbtc8723a1ant_LowPenaltyRa(pBtCoexist, NORMAL_EXEC, FALSE);
		halbtc8723a1ant_RfShrink(pBtCoexist, NORMAL_EXEC, FALSE);
	}
}


#endif

