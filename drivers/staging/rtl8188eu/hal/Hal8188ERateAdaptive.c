/*++
Copyright (c) Realtek Semiconductor Corp. All rights reserved.

Module Name:
	RateAdaptive.c

Abstract:
	Implement Rate Adaptive functions for common operations.

Major Change History:
	When       Who               What
	---------- ---------------   -------------------------------
	2011-08-12 Page            Create.

--*/
#include "odm_precomp.h"

/*  Rate adaptive parameters */

static u8 RETRY_PENALTY[PERENTRY][RETRYSIZE+1] = {
		{5, 4, 3, 2, 0, 3},      /* 92 , idx = 0 */
		{6, 5, 4, 3, 0, 4},      /* 86 , idx = 1 */
		{6, 5, 4, 2, 0, 4},      /* 81 , idx = 2 */
		{8, 7, 6, 4, 0, 6},      /* 75 , idx = 3 */
		{10, 9, 8, 6, 0, 8},     /* 71	, idx = 4 */
		{10, 9, 8, 4, 0, 8},     /* 66	, idx = 5 */
		{10, 9, 8, 2, 0, 8},     /* 62	, idx = 6 */
		{10, 9, 8, 0, 0, 8},     /* 59	, idx = 7 */
		{18, 17, 16, 8, 0, 16},  /* 53 , idx = 8 */
		{26, 25, 24, 16, 0, 24}, /* 50	, idx = 9 */
		{34, 33, 32, 24, 0, 32}, /* 47	, idx = 0x0a */
		{34, 31, 28, 20, 0, 32}, /* 43	, idx = 0x0b */
		{34, 31, 27, 18, 0, 32}, /* 40 , idx = 0x0c */
		{34, 31, 26, 16, 0, 32}, /* 37 , idx = 0x0d */
		{34, 30, 22, 16, 0, 32}, /* 32 , idx = 0x0e */
		{34, 30, 24, 16, 0, 32}, /* 26 , idx = 0x0f */
		{49, 46, 40, 16, 0, 48}, /* 20	, idx = 0x10 */
		{49, 45, 32, 0, 0, 48},  /* 17 , idx = 0x11 */
		{49, 45, 22, 18, 0, 48}, /* 15	, idx = 0x12 */
		{49, 40, 24, 16, 0, 48}, /* 12	, idx = 0x13 */
		{49, 32, 18, 12, 0, 48}, /* 9 , idx = 0x14 */
		{49, 22, 18, 14, 0, 48}, /* 6 , idx = 0x15 */
		{49, 16, 16, 0, 0, 48}
	}; /* 3, idx = 0x16 */

static u8 PT_PENALTY[RETRYSIZE+1] = {34, 31, 30, 24, 0, 32};

/*  wilson modify */
static u8 RETRY_PENALTY_IDX[2][RATESIZE] = {
		{4, 4, 4, 5, 4, 4, 5, 7, 7, 7, 8, 0x0a,	       /*  SS>TH */
		4, 4, 4, 4, 6, 0x0a, 0x0b, 0x0d,
		5, 5, 7, 7, 8, 0x0b, 0x0d, 0x0f},			   /*  0329 R01 */
		{0x0a, 0x0a, 0x0b, 0x0c, 0x0a,
		0x0a, 0x0b, 0x0c, 0x0d, 0x10, 0x13, 0x14,	   /*  SS<TH */
		0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x11, 0x13, 0x15,
		9, 9, 9, 9, 0x0c, 0x0e, 0x11, 0x13}
	};

static u8 RETRY_PENALTY_UP_IDX[RATESIZE] = {
		0x0c, 0x0d, 0x0d, 0x0f, 0x0d, 0x0e,
		0x0f, 0x0f, 0x10, 0x12, 0x13, 0x14,	       /*  SS>TH */
		0x0f, 0x10, 0x10, 0x12, 0x12, 0x13, 0x14, 0x15,
		0x11, 0x11, 0x12, 0x13, 0x13, 0x13, 0x14, 0x15};

static u8 RSSI_THRESHOLD[RATESIZE] = {
		0, 0, 0, 0,
		0, 0, 0, 0, 0, 0x24, 0x26, 0x2a,
		0x18, 0x1a, 0x1d, 0x1f, 0x21, 0x27, 0x29, 0x2a,
		0, 0, 0, 0x1f, 0x23, 0x28, 0x2a, 0x2c};

static u16 N_THRESHOLD_HIGH[RATESIZE] = {
		4, 4, 8, 16,
		24, 36, 48, 72, 96, 144, 192, 216,
		60, 80, 100, 160, 240, 400, 560, 640,
		300, 320, 480, 720, 1000, 1200, 1600, 2000};
static u16 N_THRESHOLD_LOW[RATESIZE] = {
		2, 2, 4, 8,
		12, 18, 24, 36, 48, 72, 96, 108,
		30, 40, 50, 80, 120, 200, 280, 320,
		150, 160, 240, 360, 500, 600, 800, 1000};

static u8 DROPING_NECESSARY[RATESIZE] = {
		1, 1, 1, 1,
		1, 2, 3, 4, 5, 6, 7, 8,
		1, 2, 3, 4, 5, 6, 7, 8,
		5, 6, 7, 8, 9, 10, 11, 12};

static u8 PendingForRateUpFail[5] = {2, 10, 24, 40, 60};
static u16 DynamicTxRPTTiming[6] = {
	0x186a, 0x30d4, 0x493e, 0x61a8, 0x7a12 , 0x927c}; /*  200ms-1200ms */

/*  End Rate adaptive parameters */

static void odm_SetTxRPTTiming_8188E(
		struct odm_dm_struct *dm_odm,
		struct odm_ra_info *pRaInfo,
		u8 extend
	)
{
	u8 idx = 0;

	for (idx = 0; idx < 5; idx++)
		if (DynamicTxRPTTiming[idx] == pRaInfo->RptTime)
			break;

	if (extend == 0) { /*  back to default timing */
		idx = 0;  /* 200ms */
	} else if (extend == 1) {/*  increase the timing */
		idx += 1;
		if (idx > 5)
			idx = 5;
	} else if (extend == 2) {/*  decrease the timing */
		if (idx != 0)
			idx -= 1;
	}
	pRaInfo->RptTime = DynamicTxRPTTiming[idx];

	ODM_RT_TRACE(dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD,
			("pRaInfo->RptTime = 0x%x\n", pRaInfo->RptTime));
}

static int odm_RateDown_8188E(struct odm_dm_struct *dm_odm,
				struct odm_ra_info *pRaInfo)
{
	u8 RateID, LowestRate, HighestRate;
	u8 i;

	ODM_RT_TRACE(dm_odm, ODM_COMP_RATE_ADAPTIVE,
			ODM_DBG_TRACE, ("=====>odm_RateDown_8188E()\n"));
	if (NULL == pRaInfo) {
		ODM_RT_TRACE(dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD,
				("odm_RateDown_8188E(): pRaInfo is NULL\n"));
		return -1;
	}
	RateID = pRaInfo->PreRate;
	LowestRate = pRaInfo->LowestRate;
	HighestRate = pRaInfo->HighestRate;

	ODM_RT_TRACE(dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE,
		     (" RateID =%d LowestRate =%d HighestRate =%d RateSGI =%d\n",
		     RateID, LowestRate, HighestRate, pRaInfo->RateSGI));
	if (RateID > HighestRate) {
		RateID = HighestRate;
	} else if (pRaInfo->RateSGI) {
		pRaInfo->RateSGI = 0;
	} else if (RateID > LowestRate) {
		if (RateID > 0) {
			for (i = RateID-1; i > LowestRate; i--) {
				if (pRaInfo->RAUseRate & BIT(i)) {
					RateID = i;
					goto RateDownFinish;
				}
			}
		}
	} else if (RateID <= LowestRate) {
		RateID = LowestRate;
	}
RateDownFinish:
	if (pRaInfo->RAWaitingCounter == 1) {
		pRaInfo->RAWaitingCounter += 1;
		pRaInfo->RAPendingCounter += 1;
	} else if (pRaInfo->RAWaitingCounter == 0) {
		;
	} else {
		pRaInfo->RAWaitingCounter = 0;
		pRaInfo->RAPendingCounter = 0;
	}

	if (pRaInfo->RAPendingCounter >= 4)
		pRaInfo->RAPendingCounter = 4;

	pRaInfo->DecisionRate = RateID;
	odm_SetTxRPTTiming_8188E(dm_odm, pRaInfo, 2);
	ODM_RT_TRACE(dm_odm, ODM_COMP_RATE_ADAPTIVE,
			ODM_DBG_LOUD, ("Rate down, RPT Timing default\n"));
	ODM_RT_TRACE(dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE,
			("RAWaitingCounter %d, RAPendingCounter %d",
			 pRaInfo->RAWaitingCounter, pRaInfo->RAPendingCounter));
	ODM_RT_TRACE(dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD,
			("Rate down to RateID %d RateSGI %d\n", RateID, pRaInfo->RateSGI));
	ODM_RT_TRACE(dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE,
			("<===== odm_RateDown_8188E()\n"));
	return 0;
}

static int odm_RateUp_8188E(
		struct odm_dm_struct *dm_odm,
		struct odm_ra_info *pRaInfo
	)
{
	u8 RateID, HighestRate;
	u8 i;

	ODM_RT_TRACE(dm_odm, ODM_COMP_RATE_ADAPTIVE,
			ODM_DBG_TRACE, ("=====>odm_RateUp_8188E()\n"));
	if (NULL == pRaInfo) {
		ODM_RT_TRACE(dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD,
				("odm_RateUp_8188E(): pRaInfo is NULL\n"));
		return -1;
	}
	RateID = pRaInfo->PreRate;
	HighestRate = pRaInfo->HighestRate;
	ODM_RT_TRACE(dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE,
		     (" RateID =%d HighestRate =%d\n",
		     RateID, HighestRate));
	if (pRaInfo->RAWaitingCounter == 1) {
		pRaInfo->RAWaitingCounter = 0;
		pRaInfo->RAPendingCounter = 0;
	} else if (pRaInfo->RAWaitingCounter > 1) {
		pRaInfo->PreRssiStaRA = pRaInfo->RssiStaRA;
		goto RateUpfinish;
	}
	odm_SetTxRPTTiming_8188E(dm_odm, pRaInfo, 0);
	ODM_RT_TRACE(dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD,
			("odm_RateUp_8188E():Decrease RPT Timing\n"));

	if (RateID < HighestRate) {
		for (i = RateID+1; i <= HighestRate; i++) {
			if (pRaInfo->RAUseRate & BIT(i)) {
				RateID = i;
				goto RateUpfinish;
			}
		}
	} else if (RateID == HighestRate) {
		if (pRaInfo->SGIEnable && (pRaInfo->RateSGI != 1))
			pRaInfo->RateSGI = 1;
		else if ((pRaInfo->SGIEnable) != 1)
			pRaInfo->RateSGI = 0;
	} else {
		RateID = HighestRate;
	}
RateUpfinish:
	if (pRaInfo->RAWaitingCounter ==
		(4+PendingForRateUpFail[pRaInfo->RAPendingCounter]))
		pRaInfo->RAWaitingCounter = 0;
	else
		pRaInfo->RAWaitingCounter++;

	pRaInfo->DecisionRate = RateID;
	ODM_RT_TRACE(dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD,
			("Rate up to RateID %d\n", RateID));
	ODM_RT_TRACE(dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE,
			("RAWaitingCounter %d, RAPendingCounter %d",
			 pRaInfo->RAWaitingCounter, pRaInfo->RAPendingCounter));
	ODM_RT_TRACE(dm_odm, ODM_COMP_RATE_ADAPTIVE,
			ODM_DBG_TRACE, ("<===== odm_RateUp_8188E()\n"));
	return 0;
}

static void odm_ResetRaCounter_8188E(struct odm_ra_info *pRaInfo)
{
	u8 RateID;

	RateID = pRaInfo->DecisionRate;
	pRaInfo->NscUp = (N_THRESHOLD_HIGH[RateID]+N_THRESHOLD_LOW[RateID])>>1;
	pRaInfo->NscDown = (N_THRESHOLD_HIGH[RateID]+N_THRESHOLD_LOW[RateID])>>1;
}

static void odm_RateDecision_8188E(struct odm_dm_struct *dm_odm,
		struct odm_ra_info *pRaInfo
	)
{
	u8 RateID = 0, RtyPtID = 0, PenaltyID1 = 0, PenaltyID2 = 0, i = 0;
	/* u32 pool_retry; */
	static u8 DynamicTxRPTTimingCounter;

	ODM_RT_TRACE(dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE,
			("=====>odm_RateDecision_8188E()\n"));

	if (pRaInfo->Active && (pRaInfo->TOTAL > 0)) { /*  STA used and data packet exits */
		if ((pRaInfo->RssiStaRA < (pRaInfo->PreRssiStaRA - 3)) ||
		    (pRaInfo->RssiStaRA > (pRaInfo->PreRssiStaRA + 3))) {
			pRaInfo->RAWaitingCounter = 0;
			pRaInfo->RAPendingCounter = 0;
		}
		/*  Start RA decision */
		if (pRaInfo->PreRate > pRaInfo->HighestRate)
			RateID = pRaInfo->HighestRate;
		else
			RateID = pRaInfo->PreRate;
		if (pRaInfo->RssiStaRA > RSSI_THRESHOLD[RateID])
			RtyPtID = 0;
		else
			RtyPtID = 1;
		PenaltyID1 = RETRY_PENALTY_IDX[RtyPtID][RateID]; /* TODO by page */

		ODM_RT_TRACE(dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE,
			     (" NscDown init is %d\n", pRaInfo->NscDown));

		for (i = 0 ; i <= 4 ; i++)
			pRaInfo->NscDown += pRaInfo->RTY[i] * RETRY_PENALTY[PenaltyID1][i];

		ODM_RT_TRACE(dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE,
			     (" NscDown is %d, total*penalty[5] is %d\n", pRaInfo->NscDown,
			      (pRaInfo->TOTAL * RETRY_PENALTY[PenaltyID1][5])));

		if (pRaInfo->NscDown > (pRaInfo->TOTAL * RETRY_PENALTY[PenaltyID1][5]))
			pRaInfo->NscDown -= pRaInfo->TOTAL * RETRY_PENALTY[PenaltyID1][5];
		else
			pRaInfo->NscDown = 0;

		/*  rate up */
		PenaltyID2 = RETRY_PENALTY_UP_IDX[RateID];
		ODM_RT_TRACE(dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE,
			     (" NscUp init is %d\n", pRaInfo->NscUp));

		for (i = 0 ; i <= 4 ; i++)
			pRaInfo->NscUp += pRaInfo->RTY[i] * RETRY_PENALTY[PenaltyID2][i];

		ODM_RT_TRACE(dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE,
			     ("NscUp is %d, total*up[5] is %d\n",
			     pRaInfo->NscUp, (pRaInfo->TOTAL * RETRY_PENALTY[PenaltyID2][5])));

		if (pRaInfo->NscUp > (pRaInfo->TOTAL * RETRY_PENALTY[PenaltyID2][5]))
			pRaInfo->NscUp -= pRaInfo->TOTAL * RETRY_PENALTY[PenaltyID2][5];
		else
			pRaInfo->NscUp = 0;

		ODM_RT_TRACE(dm_odm, ODM_COMP_RATE_ADAPTIVE|ODM_COMP_INIT, ODM_DBG_LOUD,
			     (" RssiStaRa = %d RtyPtID =%d PenaltyID1 = 0x%x  PenaltyID2 = 0x%x RateID =%d NscDown =%d NscUp =%d SGI =%d\n",
			     pRaInfo->RssiStaRA, RtyPtID, PenaltyID1, PenaltyID2, RateID, pRaInfo->NscDown, pRaInfo->NscUp, pRaInfo->RateSGI));
		if ((pRaInfo->NscDown < N_THRESHOLD_LOW[RateID]) ||
		    (pRaInfo->DROP > DROPING_NECESSARY[RateID]))
			odm_RateDown_8188E(dm_odm, pRaInfo);
		else if (pRaInfo->NscUp > N_THRESHOLD_HIGH[RateID])
			odm_RateUp_8188E(dm_odm, pRaInfo);

		if (pRaInfo->DecisionRate > pRaInfo->HighestRate)
			pRaInfo->DecisionRate = pRaInfo->HighestRate;

		if ((pRaInfo->DecisionRate) == (pRaInfo->PreRate))
			DynamicTxRPTTimingCounter += 1;
		else
			DynamicTxRPTTimingCounter = 0;

		if (DynamicTxRPTTimingCounter >= 4) {
			odm_SetTxRPTTiming_8188E(dm_odm, pRaInfo, 1);
			ODM_RT_TRACE(dm_odm, ODM_COMP_RATE_ADAPTIVE,
				     ODM_DBG_LOUD, ("<===== Rate don't change 4 times, Extend RPT Timing\n"));
			DynamicTxRPTTimingCounter = 0;
		}

		pRaInfo->PreRate = pRaInfo->DecisionRate;  /* YJ, add, 120120 */

		odm_ResetRaCounter_8188E(pRaInfo);
	}
	ODM_RT_TRACE(dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE, ("<===== odm_RateDecision_8188E()\n"));
}

static int odm_ARFBRefresh_8188E(struct odm_dm_struct *dm_odm, struct odm_ra_info *pRaInfo)
{  /*  Wilson 2011/10/26 */
	struct adapter *adapt = dm_odm->Adapter;
	u32 MaskFromReg;
	s8 i;

	switch (pRaInfo->RateID) {
	case RATR_INX_WIRELESS_NGB:
		pRaInfo->RAUseRate = (pRaInfo->RateMask)&0x0f8ff015;
		break;
	case RATR_INX_WIRELESS_NG:
		pRaInfo->RAUseRate = (pRaInfo->RateMask)&0x0f8ff010;
		break;
	case RATR_INX_WIRELESS_NB:
		pRaInfo->RAUseRate = (pRaInfo->RateMask)&0x0f8ff005;
		break;
	case RATR_INX_WIRELESS_N:
		pRaInfo->RAUseRate = (pRaInfo->RateMask)&0x0f8ff000;
		break;
	case RATR_INX_WIRELESS_GB:
		pRaInfo->RAUseRate = (pRaInfo->RateMask)&0x00000ff5;
		break;
	case RATR_INX_WIRELESS_G:
		pRaInfo->RAUseRate = (pRaInfo->RateMask)&0x00000ff0;
		break;
	case RATR_INX_WIRELESS_B:
		pRaInfo->RAUseRate = (pRaInfo->RateMask)&0x0000000d;
		break;
	case 12:
		MaskFromReg = usb_read32(adapt, REG_ARFR0);
		pRaInfo->RAUseRate = (pRaInfo->RateMask)&MaskFromReg;
		break;
	case 13:
		MaskFromReg = usb_read32(adapt, REG_ARFR1);
		pRaInfo->RAUseRate = (pRaInfo->RateMask)&MaskFromReg;
		break;
	case 14:
		MaskFromReg = usb_read32(adapt, REG_ARFR2);
		pRaInfo->RAUseRate = (pRaInfo->RateMask)&MaskFromReg;
		break;
	case 15:
		MaskFromReg = usb_read32(adapt, REG_ARFR3);
		pRaInfo->RAUseRate = (pRaInfo->RateMask)&MaskFromReg;
		break;
	default:
		pRaInfo->RAUseRate = (pRaInfo->RateMask);
		break;
	}
	/*  Highest rate */
	if (pRaInfo->RAUseRate) {
		for (i = RATESIZE; i >= 0; i--) {
			if ((pRaInfo->RAUseRate)&BIT(i)) {
				pRaInfo->HighestRate = i;
				break;
			}
		}
	} else {
		pRaInfo->HighestRate = 0;
	}
	/*  Lowest rate */
	if (pRaInfo->RAUseRate) {
		for (i = 0; i < RATESIZE; i++) {
			if ((pRaInfo->RAUseRate) & BIT(i)) {
				pRaInfo->LowestRate = i;
				break;
			}
		}
	} else {
		pRaInfo->LowestRate = 0;
	}
		if (pRaInfo->HighestRate > 0x13)
			pRaInfo->PTModeSS = 3;
		else if (pRaInfo->HighestRate > 0x0b)
			pRaInfo->PTModeSS = 2;
		else if (pRaInfo->HighestRate > 0x0b)
			pRaInfo->PTModeSS = 1;
		else
			pRaInfo->PTModeSS = 0;
	ODM_RT_TRACE(dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD,
		     ("ODM_ARFBRefresh_8188E(): PTModeSS =%d\n", pRaInfo->PTModeSS));

	if (pRaInfo->DecisionRate > pRaInfo->HighestRate)
		pRaInfo->DecisionRate = pRaInfo->HighestRate;

	ODM_RT_TRACE(dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD,
		     ("ODM_ARFBRefresh_8188E(): RateID =%d RateMask =%8.8x RAUseRate =%8.8x HighestRate =%d, DecisionRate =%d\n",
		     pRaInfo->RateID, pRaInfo->RateMask, pRaInfo->RAUseRate, pRaInfo->HighestRate, pRaInfo->DecisionRate));
	return 0;
}

static void odm_PTTryState_8188E(struct odm_ra_info *pRaInfo)
{
	pRaInfo->PTTryState = 0;
	switch (pRaInfo->PTModeSS) {
	case 3:
		if (pRaInfo->DecisionRate >= 0x19)
			pRaInfo->PTTryState = 1;
		break;
	case 2:
		if (pRaInfo->DecisionRate >= 0x11)
			pRaInfo->PTTryState = 1;
		break;
	case 1:
		if (pRaInfo->DecisionRate >= 0x0a)
			pRaInfo->PTTryState = 1;
		break;
	case 0:
		if (pRaInfo->DecisionRate >= 0x03)
			pRaInfo->PTTryState = 1;
		break;
	default:
		pRaInfo->PTTryState = 0;
		break;
	}

	if (pRaInfo->RssiStaRA < 48) {
		pRaInfo->PTStage = 0;
	} else if (pRaInfo->PTTryState == 1) {
		if ((pRaInfo->PTStopCount >= 10) ||
		    (pRaInfo->PTPreRssi > pRaInfo->RssiStaRA + 5) ||
		    (pRaInfo->PTPreRssi < pRaInfo->RssiStaRA - 5) ||
		    (pRaInfo->DecisionRate != pRaInfo->PTPreRate)) {
			if (pRaInfo->PTStage == 0)
				pRaInfo->PTStage = 1;
			else if (pRaInfo->PTStage == 1)
				pRaInfo->PTStage = 3;
			else
				pRaInfo->PTStage = 5;

			pRaInfo->PTPreRssi = pRaInfo->RssiStaRA;
			pRaInfo->PTStopCount = 0;
		} else {
			pRaInfo->RAstage = 0;
			pRaInfo->PTStopCount++;
		}
	} else {
		pRaInfo->PTStage = 0;
		pRaInfo->RAstage = 0;
	}
	pRaInfo->PTPreRate = pRaInfo->DecisionRate;
}

static void odm_PTDecision_8188E(struct odm_ra_info *pRaInfo)
{
	u8 j;
	u8 temp_stage;
	u32 numsc;
	u32 num_total;
	u8 stage_id;

	numsc  = 0;
	num_total = pRaInfo->TOTAL * PT_PENALTY[5];
	for (j = 0; j <= 4; j++) {
		numsc += pRaInfo->RTY[j] * PT_PENALTY[j];
		if (numsc > num_total)
			break;
	}

	j >>= 1;
	temp_stage = (pRaInfo->PTStage + 1) >> 1;
	if (temp_stage > j)
		stage_id = temp_stage-j;
	else
		stage_id = 0;

	pRaInfo->PTSmoothFactor = (pRaInfo->PTSmoothFactor>>1) + (pRaInfo->PTSmoothFactor>>2) + stage_id*16+2;
	if (pRaInfo->PTSmoothFactor > 192)
		pRaInfo->PTSmoothFactor = 192;
	stage_id = pRaInfo->PTSmoothFactor >> 6;
	temp_stage = stage_id*2;
	if (temp_stage != 0)
		temp_stage -= 1;
	if (pRaInfo->DROP > 3)
		temp_stage = 0;
	pRaInfo->PTStage = temp_stage;
}

static void
odm_RATxRPTTimerSetting(
		struct odm_dm_struct *dm_odm,
		u16 minRptTime
)
{
	ODM_RT_TRACE(dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE, (" =====>odm_RATxRPTTimerSetting()\n"));

	if (dm_odm->CurrminRptTime != minRptTime) {
		ODM_RT_TRACE(dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD,
			     (" CurrminRptTime = 0x%04x minRptTime = 0x%04x\n", dm_odm->CurrminRptTime, minRptTime));
		rtw_rpt_timer_cfg_cmd(dm_odm->Adapter, minRptTime);
		dm_odm->CurrminRptTime = minRptTime;
	}
	ODM_RT_TRACE(dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE, (" <===== odm_RATxRPTTimerSetting()\n"));
}

void
ODM_RASupport_Init(
		struct odm_dm_struct *dm_odm
	)
{
	ODM_RT_TRACE(dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("=====>ODM_RASupport_Init()\n"));

	dm_odm->RaSupport88E = true;
}

int ODM_RAInfo_Init(struct odm_dm_struct *dm_odm, u8 macid)
{
	struct odm_ra_info *pRaInfo = &dm_odm->RAInfo[macid];
	u8 WirelessMode = 0xFF; /* invalid value */
	u8 max_rate_idx = 0x13; /* MCS7 */

	if (dm_odm->pWirelessMode != NULL)
		WirelessMode = *(dm_odm->pWirelessMode);

	if (WirelessMode != 0xFF) {
		if (WirelessMode & ODM_WM_N24G)
			max_rate_idx = 0x13;
		else if (WirelessMode & ODM_WM_G)
			max_rate_idx = 0x0b;
		else if (WirelessMode & ODM_WM_B)
			max_rate_idx = 0x03;
	}

	ODM_RT_TRACE(dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD,
		     ("ODM_RAInfo_Init(): WirelessMode:0x%08x , max_raid_idx:0x%02x\n",
		     WirelessMode, max_rate_idx));

	pRaInfo->DecisionRate = max_rate_idx;
	pRaInfo->PreRate = max_rate_idx;
	pRaInfo->HighestRate = max_rate_idx;
	pRaInfo->LowestRate = 0;
	pRaInfo->RateID = 0;
	pRaInfo->RateMask = 0xffffffff;
	pRaInfo->RssiStaRA = 0;
	pRaInfo->PreRssiStaRA = 0;
	pRaInfo->SGIEnable = 0;
	pRaInfo->RAUseRate = 0xffffffff;
	pRaInfo->NscDown = (N_THRESHOLD_HIGH[0x13]+N_THRESHOLD_LOW[0x13])/2;
	pRaInfo->NscUp = (N_THRESHOLD_HIGH[0x13]+N_THRESHOLD_LOW[0x13])/2;
	pRaInfo->RateSGI = 0;
	pRaInfo->Active = 1;	/* Active is not used at present. by page, 110819 */
	pRaInfo->RptTime = 0x927c;
	pRaInfo->DROP = 0;
	pRaInfo->RTY[0] = 0;
	pRaInfo->RTY[1] = 0;
	pRaInfo->RTY[2] = 0;
	pRaInfo->RTY[3] = 0;
	pRaInfo->RTY[4] = 0;
	pRaInfo->TOTAL = 0;
	pRaInfo->RAWaitingCounter = 0;
	pRaInfo->RAPendingCounter = 0;
	pRaInfo->PTActive = 1;   /*  Active when this STA is use */
	pRaInfo->PTTryState = 0;
	pRaInfo->PTStage = 5; /*  Need to fill into HW_PWR_STATUS */
	pRaInfo->PTSmoothFactor = 192;
	pRaInfo->PTStopCount = 0;
	pRaInfo->PTPreRate = 0;
	pRaInfo->PTPreRssi = 0;
	pRaInfo->PTModeSS = 0;
	pRaInfo->RAstage = 0;
	return 0;
}

int ODM_RAInfo_Init_all(struct odm_dm_struct *dm_odm)
{
	u8 macid = 0;

	ODM_RT_TRACE(dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("=====>\n"));
	dm_odm->CurrminRptTime = 0;

	for (macid = 0; macid < ODM_ASSOCIATE_ENTRY_NUM; macid++)
		ODM_RAInfo_Init(dm_odm, macid);

	return 0;
}

u8 ODM_RA_GetShortGI_8188E(struct odm_dm_struct *dm_odm, u8 macid)
{
	if ((NULL == dm_odm) || (macid >= ASSOCIATE_ENTRY_NUM))
		return 0;
	ODM_RT_TRACE(dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE,
		     ("macid =%d SGI =%d\n", macid, dm_odm->RAInfo[macid].RateSGI));
	return dm_odm->RAInfo[macid].RateSGI;
}

u8 ODM_RA_GetDecisionRate_8188E(struct odm_dm_struct *dm_odm, u8 macid)
{
	u8 DecisionRate = 0;

	if ((NULL == dm_odm) || (macid >= ASSOCIATE_ENTRY_NUM))
		return 0;
	DecisionRate = dm_odm->RAInfo[macid].DecisionRate;
	ODM_RT_TRACE(dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE,
		(" macid =%d DecisionRate = 0x%x\n", macid, DecisionRate));
	return DecisionRate;
}

u8 ODM_RA_GetHwPwrStatus_8188E(struct odm_dm_struct *dm_odm, u8 macid)
{
	u8 PTStage = 5;

	if ((NULL == dm_odm) || (macid >= ASSOCIATE_ENTRY_NUM))
		return 0;
	PTStage = dm_odm->RAInfo[macid].PTStage;
	ODM_RT_TRACE(dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE,
		     ("macid =%d PTStage = 0x%x\n", macid, PTStage));
	return PTStage;
}

void ODM_RA_UpdateRateInfo_8188E(struct odm_dm_struct *dm_odm, u8 macid, u8 RateID, u32 RateMask, u8 SGIEnable)
{
	struct odm_ra_info *pRaInfo = NULL;

	ODM_RT_TRACE(dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD,
		     ("macid =%d RateID = 0x%x RateMask = 0x%x SGIEnable =%d\n",
		     macid, RateID, RateMask, SGIEnable));
	if ((NULL == dm_odm) || (macid >= ASSOCIATE_ENTRY_NUM))
		return;

	pRaInfo = &(dm_odm->RAInfo[macid]);
	pRaInfo->RateID = RateID;
	pRaInfo->RateMask = RateMask;
	pRaInfo->SGIEnable = SGIEnable;
	odm_ARFBRefresh_8188E(dm_odm, pRaInfo);
}

void ODM_RA_SetRSSI_8188E(struct odm_dm_struct *dm_odm, u8 macid, u8 Rssi)
{
	struct odm_ra_info *pRaInfo = NULL;

	ODM_RT_TRACE(dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE,
		     (" macid =%d Rssi =%d\n", macid, Rssi));
	if ((NULL == dm_odm) || (macid >= ASSOCIATE_ENTRY_NUM))
		return;

	pRaInfo = &(dm_odm->RAInfo[macid]);
	pRaInfo->RssiStaRA = Rssi;
}

void ODM_RA_Set_TxRPT_Time(struct odm_dm_struct *dm_odm, u16 minRptTime)
{
	struct adapter *adapt = dm_odm->Adapter;

	usb_write16(adapt, REG_TX_RPT_TIME, minRptTime);
}

void ODM_RA_TxRPT2Handle_8188E(struct odm_dm_struct *dm_odm, u8 *TxRPT_Buf, u16 TxRPT_Len, u32 macid_entry0, u32 macid_entry1)
{
	struct odm_ra_info *pRAInfo = NULL;
	u8 MacId = 0;
	u8 *pBuffer = NULL;
	u32 valid = 0, ItemNum = 0;
	u16 minRptTime = 0x927c;

	ODM_RT_TRACE(dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD,
		     ("=====>ODM_RA_TxRPT2Handle_8188E(): valid0 =%d valid1 =%d BufferLength =%d\n",
		     macid_entry0, macid_entry1, TxRPT_Len));

	ItemNum = TxRPT_Len >> 3;
	pBuffer = TxRPT_Buf;

	do {
		if (MacId >= ASSOCIATE_ENTRY_NUM)
			valid = 0;
		else if (MacId >= 32)
			valid = (1 << (MacId - 32)) & macid_entry1;
		else
			valid = (1 << MacId) & macid_entry0;

		pRAInfo = &(dm_odm->RAInfo[MacId]);
		if (valid) {
			pRAInfo->RTY[0] = (u16)GET_TX_REPORT_TYPE1_RERTY_0(pBuffer);
			pRAInfo->RTY[1] = (u16)GET_TX_REPORT_TYPE1_RERTY_1(pBuffer);
			pRAInfo->RTY[2] = (u16)GET_TX_REPORT_TYPE1_RERTY_2(pBuffer);
			pRAInfo->RTY[3] = (u16)GET_TX_REPORT_TYPE1_RERTY_3(pBuffer);
			pRAInfo->RTY[4] = (u16)GET_TX_REPORT_TYPE1_RERTY_4(pBuffer);
			pRAInfo->DROP =   (u16)GET_TX_REPORT_TYPE1_DROP_0(pBuffer);
			pRAInfo->TOTAL = pRAInfo->RTY[0] + pRAInfo->RTY[1] +
					 pRAInfo->RTY[2] + pRAInfo->RTY[3] +
					 pRAInfo->RTY[4] + pRAInfo->DROP;
			if (pRAInfo->TOTAL != 0) {
				ODM_RT_TRACE(dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD,
					     ("macid =%d Total =%d R0 =%d R1 =%d R2 =%d R3 =%d R4 =%d D0 =%d valid0 =%x valid1 =%x\n",
					     MacId, pRAInfo->TOTAL,
					     pRAInfo->RTY[0], pRAInfo->RTY[1],
					     pRAInfo->RTY[2], pRAInfo->RTY[3],
					     pRAInfo->RTY[4], pRAInfo->DROP,
					     macid_entry0 , macid_entry1));
				if (pRAInfo->PTActive) {
					if (pRAInfo->RAstage < 5)
						odm_RateDecision_8188E(dm_odm, pRAInfo);
					else if (pRAInfo->RAstage == 5) /*  Power training try state */
						odm_PTTryState_8188E(pRAInfo);
					else /*  RAstage == 6 */
						odm_PTDecision_8188E(pRAInfo);

					/*  Stage_RA counter */
					if (pRAInfo->RAstage <= 5)
						pRAInfo->RAstage++;
					else
						pRAInfo->RAstage = 0;
				} else {
					odm_RateDecision_8188E(dm_odm, pRAInfo);
				}
				ODM_RT_TRACE(dm_odm, ODM_COMP_INIT, ODM_DBG_LOUD,
					     ("macid =%d R0 =%d R1 =%d R2 =%d R3 =%d R4 =%d drop =%d valid0 =%x RateID =%d SGI =%d\n",
					     MacId,
					     pRAInfo->RTY[0],
					     pRAInfo->RTY[1],
					     pRAInfo->RTY[2],
					     pRAInfo->RTY[3],
					     pRAInfo->RTY[4],
					     pRAInfo->DROP,
					     macid_entry0,
					     pRAInfo->DecisionRate,
					     pRAInfo->RateSGI));
			} else {
				ODM_RT_TRACE(dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, (" TOTAL = 0!!!!\n"));
			}
		}

		if (minRptTime > pRAInfo->RptTime)
			minRptTime = pRAInfo->RptTime;

		pBuffer += TX_RPT2_ITEM_SIZE;
		MacId++;
	} while (MacId < ItemNum);

	odm_RATxRPTTimerSetting(dm_odm, minRptTime);

	ODM_RT_TRACE(dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("<===== ODM_RA_TxRPT2Handle_8188E()\n"));
}
