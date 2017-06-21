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
#include "mp_precomp.h"

#include "../phydm_precomp.h"


#if (RATE_ADAPTIVE_SUPPORT == 1)
// Rate adaptive parameters
#define		RA_RATE_LEVEL			2


static u1Byte RETRY_PENALTY[PERENTRY][RETRYSIZE+1] = {{5,4,3,2,0,3},//92 , idx=0
													{6,5,4,3,0,4},//86 , idx=1
													{6,5,4,2,0,4},//81 , idx=2
													{8,7,6,4,0,6},//75 , idx=3
													{10,9,8,6,0,8},//71	, idx=4
													{10,9,8,4,0,8},//66	, idx=5
													{10,9,8,2,0,8},//62	, idx=6
													{10,9,8,0,0,8},//59	, idx=7
													{18,17,16,8,0,16},//53 , idx=8
													{26,25,24,16,0,24},//50	, idx=9
													{34,33,32,24,0,32},//47	, idx=0x0a
													//{34,33,32,16,0,32},//43	, idx=0x0b
													//{34,33,32,8,0,32},//40 , idx=0x0c
													//{34,33,28,8,0,32},//37 , idx=0x0d
													//{34,33,20,8,0,32},//32 , idx=0x0e
													//{34,32,24,8,0,32},//26 , idx=0x0f
													//{49,48,32,16,0,48},//20	, idx=0x10
													//{49,48,24,0,0,48},//17 , idx=0x11
													//{49,47,16,16,0,48},//15	, idx=0x12
													//{49,44,16,16,0,48},//12	, idx=0x13
													//{49,40,16,0,0,48},//9 , idx=0x14
													{34,31,28,20,0,32},//43	, idx=0x0b
													{34,31,27,18,0,32},//40 , idx=0x0c
													{34,31,26,16,0,32},//37 , idx=0x0d
													{34,30,22,16,0,32},//32 , idx=0x0e
													{34,30,24,16,0,32},//26 , idx=0x0f
													{49,46,40,16,0,48},//20	, idx=0x10
													{49,45,32,0,0,48},//17 , idx=0x11
													{49,45,22,18,0,48},//15	, idx=0x12
#if (DM_ODM_SUPPORT_TYPE == ODM_AP)													
													{49,40,28,18,0,48},//12 , idx=0x13
													{49,34,20,16,0,48},//9 , idx=0x14
#else
													{49,40,24,16,0,48},//12	, idx=0x13
													{49,32,18,12,0,48},//9 , idx=0x14
#endif													
													{49,22,18,14,0,48},//6 , idx=0x15
													{49,16,16,0,0,48}};//3 //3, idx=0x16

static u1Byte	RETRY_PENALTY_UP[RETRYSIZE+1]={49,44,16,16,0,48};  // 12% for rate up

static u1Byte PT_PENALTY[RETRYSIZE+1]={34,31,30,24,0,32};

#if 0
static u1Byte	RETRY_PENALTY_IDX[2][RATESIZE] = {{4,4,4,5,4,4,5,7,7,7,8,0x0a,	       // SS>TH
													4,4,4,4,6,0x0a,0x0b,0x0d,
													5,5,7,7,8,0x0b,0x0d,0x0f},	 		   // 0329 R01
													{4,4,4,5,7,7,9,9,0x0c,0x0e,0x10,0x12,	   // SS<TH
													4,4,5,5,6,0x0a,0x11,0x13,
													9,9,9,9,0x0c,0x0e,0x11,0x13}};	
#endif


#if (DM_ODM_SUPPORT_TYPE & ODM_AP)	
static u1Byte	RETRY_PENALTY_IDX[2][RATESIZE] = 	{{4,4,4,5,4,4,5,7,7,7,8,0x0a,	       // SS>TH
													4,4,4,4,6,0x0a,0x0b,0x0d,
													5,5,7,7,8,0x0b,0x0d,0x0f},	 		   // 0329 R01
													{0x0a,0x0a,0x0a,0x0a,0x0c,0x0c,0x0e,0x10,0x11,0x12,0x12,0x13,	   // SS<TH
													0x0e,0x0f,0x10,0x10,0x11,0x14,0x14,0x15,
													9,9,9,9,0x0c,0x0e,0x11,0x13}};	

static u1Byte	RETRY_PENALTY_UP_IDX[RATESIZE] = 	{0x10,0x10,0x10,0x10,0x11,0x11,0x12,0x12,0x12,0x13,0x13,0x14,	       // SS>TH
													0x13,0x13,0x14,0x14,0x15,0x15,0x15,0x15,
													0x11,0x11,0x12,0x13,0x13,0x13,0x14,0x15};	

static u1Byte	RSSI_THRESHOLD[RATESIZE] = 			{0,0,0,0,
													0,0,0,0,0,0x24,0x26,0x2a,
													0x13,0x15,0x17,0x18,0x1a,0x1c,0x1d,0x1f,
													0,0,0,0x1f,0x23,0x28,0x2a,0x2c};
#else

// wilson modify
/*static u1Byte	RETRY_PENALTY_IDX[2][RATESIZE] = {{4,4,4,5,4,4,5,7,7,7,8,0x0a,	       // SS>TH
													4,4,4,4,6,0x0a,0x0b,0x0d,
													5,5,7,7,8,0x0b,0x0d,0x0f},	 		   // 0329 R01
													{0x0a,0x0a,0x0b,0x0c,0x0a,0x0a,0x0b,0x0c,0x0d,0x10,0x13,0x14,	   // SS<TH
													0x0b,0x0c,0x0d,0x0e,0x0f,0x11,0x13,0x15,
													9,9,9,9,0x0c,0x0e,0x11,0x13}};	*/
													
static u1Byte	RETRY_PENALTY_IDX[2][RATESIZE] = {{4,4,4,5,4,4,5,7,7,7,8,0x0a,	       // SS>TH
													4,4,4,4,6,0x0a,0x0b,0x0d,
													5,5,7,7,8,0x0b,0x0d,0x0f},	 		   // 0329 R01
													{0x0a,0x0a,0x0b,0x0c,0x0a,0x0a,0x0b,0x0c,0x0d,0x10,0x13,0x13,	   // SS<TH
													0x0b,0x0c,0x0d,0x0e,0x0f,0x11,0x13,0x13,
													9,9,9,9,0x0c,0x0e,0x11,0x13}};	
#if (RA_RATE_LEVEL == 0)
static u1Byte	RETRY_PENALTY_UP_IDX[RATESIZE] = {0x0c,0x0d,0x0d,0x0f,0x0d,0x0e,0x0f,0x0f,0x10,0x12,0x13,0x14,	       // SS>TH
													0x0f,0x10,0x10,0x12,0x12,0x13,0x14,0x15,
													0x11,0x11,0x12,0x13,0x13,0x13,0x14,0x15};	
#elif (RA_RATE_LEVEL == 1)
static u1Byte	RETRY_PENALTY_UP_IDX[RATESIZE] = {0x0c,0x0d,0x0d,0x0f,0x0d,0x0e,0x0f,0x0f,0x10,0x12,0x13,0x13,	       // SS>TH
													0x0f,0x10,0x10,0x12,0x12,0x13,0x13,0x14,
													0x11,0x11,0x12,0x13,0x13,0x13,0x13,0x14};
#elif (RA_RATE_LEVEL == 2)
static u1Byte	RETRY_PENALTY_UP_IDX[RATESIZE] = {0x0c,0x0d,0x0d,0x0f,0x0d,0x0e,0x0f,0x0f,0x10,0x12,0x13,0x13,	       // SS>TH
													0x0f,0x10,0x10,0x12,0x12,0x12,0x12,0x13,
													0x11,0x11,0x12,0x12,0x12,0x12,0x12,0x13};
#endif

static u1Byte	RSSI_THRESHOLD[RATESIZE] = 			{0,0,0,0,
													0,0,0,0,0,0x24,0x26,0x2a,						
													0x18,0x1a,0x1d,0x1f,0x21,0x27,0x29,0x2a,												
													0,0,0,0x1f,0x23,0x28,0x2a,0x2c};

#endif	

/*static u1Byte	RSSI_THRESHOLD[RATESIZE] = {0,0,0,0,
													0,0,0,0,0,0x24,0x26,0x2a,
													0x1a,0x1c,0x1e,0x21,0x24,0x2a,0x2b,0x2d,
													0,0,0,0x1f,0x23,0x28,0x2a,0x2c};*/
/*static u2Byte	N_THRESHOLD_HIGH[RATESIZE] = {4,4,8,16,
													24,36,48,72,96,144,192,216,
													60,80,100,160,240,400,560,640,
													300,320,480,720,1000,1200,1600,2000};
static u2Byte 	N_THRESHOLD_LOW[RATESIZE] = {2,2,4,8,
													12,18,24,36,48,72,96,108,
													30,40,50,80,120,200,280,320,
													150,160,240,360,500,600,800,1000};*/
static u2Byte	N_THRESHOLD_HIGH[RATESIZE] = {4,4,8,16,
													24,36,48,72,96,144,192,216,
													60,80,100,160,240,400,600,800,
													300,320,480,720,1000,1200,1600,2000};
static u2Byte 	N_THRESHOLD_LOW[RATESIZE] = {2,2,4,8,
													12,18,24,36,48,72,96,108,
													30,40,50,80,120,200,300,400,
													150,160,240,360,500,600,800,1000};

static u1Byte	 TRYING_NECESSARY[RATESIZE] = {2,2,2,2,
													2,2,3,3,4,4,5,7,
													4,4,7,10,10,12,12,18,
													5,7,7,8,11,18,36,60};  // 0329 // 1207
#if 0
static u1Byte	 POOL_RETRY_TH[RATESIZE] = {30,30,30,30,
													30,30,25,25,20,15,15,10,
													30,25,25,20,15,10,10,10,
													30,25,25,20,15,10,10,10}; 		
#endif

static u1Byte	DROPING_NECESSARY[RATESIZE] = {1,1,1,1,
													1,2,3,4,5,6,7,8,
													1,2,3,4,5,6,7,8,
													5,6,7,8,9,10,11,12};


static u4Byte	INIT_RATE_FALLBACK_TABLE[16]={0x0f8ff015,  // 0: 40M BGN mode
											0x0f8ff010,   // 1: 40M GN mode
											0x0f8ff005,   // 2: BN mode/ 40M BGN mode
											0x0f8ff000,   // 3: N mode
											0x00000ff5,   // 4: BG mode
											0x00000ff0,   // 5: G mode
											0x0000000d,   // 6: B mode
											0,			// 7:
											0,			// 8:
											0,			// 9:
											0,			// 10:
											0,			// 11:
											0,			// 12:
											0,			// 13:
											0,			// 14:
											0,			// 15:
											
	};
static u1Byte PendingForRateUpFail[5]={2,10,24,36,48};
static u2Byte DynamicTxRPTTiming[6]={0x186a, 0x30d4, 0x493e, 0x61a8, 0x7a12 ,0x927c}; // 200ms 400 / 600 / 8000 / 1000 -1200ms

// End Rate adaptive parameters

static void 
odm_SetTxRPTTiming_8188E(
	IN	PDM_ODM_T		pDM_Odm,
	IN 	PODM_RA_INFO_T  	pRaInfo, 
	IN	u1Byte 				extend
	)
{
	u1Byte idx = 0;

	for(idx=0; idx<5; idx++)
		if(DynamicTxRPTTiming[idx] == pRaInfo->RptTime)
			break;

	if (extend==0) // back to default timing
		idx=0;  //200ms
	else if (extend==1) {// increase the timing
		idx+=1;
		if (idx>5)
			idx=5;
	}
	else if (extend==2) {// decrease the timing
		if(idx!=0)
			idx-=1;
	}
	pRaInfo->RptTime=DynamicTxRPTTiming[idx];
	
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("pRaInfo->RptTime=0x%x\n", pRaInfo->RptTime));
}

static int 
odm_RateDown_8188E(
	IN	PDM_ODM_T		pDM_Odm,
	IN 	PODM_RA_INFO_T  pRaInfo
	)
{
	u1Byte RateID, LowestRate, HighestRate;
	u1Byte i;

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE, ("=====>odm_RateDown_8188E()\n"));
	if(NULL == pRaInfo)
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("odm_RateDown_8188E(): pRaInfo is NULL\n"));
		return -1;
	}
	RateID = pRaInfo->PreRate;
	LowestRate = pRaInfo->LowestRate;
	HighestRate = pRaInfo->HighestRate;

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE, 
				(" RateID=%d LowestRate=%d HighestRate=%d RateSGI=%d\n", 
				RateID, LowestRate, HighestRate, pRaInfo->RateSGI));
	if (RateID > HighestRate)
	{
		RateID=HighestRate;
	}
	else if(pRaInfo->RateSGI)
	{
		pRaInfo->RateSGI=0;
	}
	else if (RateID > LowestRate)
	{
		if (RateID > 0)
		{
			for (i=RateID-1; i>LowestRate;i--)
			{
				if (pRaInfo->RAUseRate & BIT(i))
				{
					RateID=i;
					goto RateDownFinish;
					
				}
			}
		}
	}
	else if (RateID <= LowestRate)
	{
		RateID = LowestRate;
	}
RateDownFinish:
	if (pRaInfo->RAWaitingCounter==1){
		pRaInfo->RAWaitingCounter+=1;
		pRaInfo->RAPendingCounter+=1;
	}
	else if(pRaInfo->RAWaitingCounter==0){
	}
	else{
		pRaInfo->RAWaitingCounter=0;
		pRaInfo->RAPendingCounter=0;
	}

	if(pRaInfo->RAPendingCounter>=4)
		pRaInfo->RAPendingCounter=4;
	
	pRaInfo->DecisionRate=RateID;
	odm_SetTxRPTTiming_8188E(pDM_Odm,pRaInfo, 2);
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("Rate down, RPT Timing default\n"));
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE, ("RAWaitingCounter %d, RAPendingCounter %d",pRaInfo->RAWaitingCounter,pRaInfo->RAPendingCounter));
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("Rate down to RateID %d RateSGI %d\n", RateID, pRaInfo->RateSGI));
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE, ("<=====odm_RateDown_8188E() \n"));
	return 0;
}

static int 
odm_RateUp_8188E(
	IN	PDM_ODM_T		pDM_Odm,
	IN 	PODM_RA_INFO_T  pRaInfo
	)
{
	u1Byte RateID, HighestRate;
	u1Byte i;

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE, ("=====>odm_RateUp_8188E() \n"));
	if(NULL == pRaInfo)
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("odm_RateUp_8188E(): pRaInfo is NULL\n"));
		return -1;
	}
	RateID = pRaInfo->PreRate;
	HighestRate = pRaInfo->HighestRate;
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE, 
				(" RateID=%d HighestRate=%d\n", 
				RateID, HighestRate));
	if (pRaInfo->RAWaitingCounter==1){
		pRaInfo->RAWaitingCounter=0;
		pRaInfo->RAPendingCounter=0;
	}	
	else if (pRaInfo->RAWaitingCounter>1){
		pRaInfo->PreRssiStaRA=pRaInfo->RssiStaRA;
		goto RateUpfinish;
	}
	odm_SetTxRPTTiming_8188E(pDM_Odm,pRaInfo, 0);
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("odm_RateUp_8188E():Decrease RPT Timing\n"));
	
	if (RateID < HighestRate)
	{
		for (i=RateID+1; i<=HighestRate; i++)
		{
			if (pRaInfo->RAUseRate & BIT(i))
			{
				RateID=i;
				goto RateUpfinish;
			}
		}
	}
	else if(RateID == HighestRate)
	{
		if (pRaInfo->SGIEnable && (pRaInfo->RateSGI != 1))
			pRaInfo->RateSGI = 1;
		else if((pRaInfo->SGIEnable) !=1 )
			pRaInfo->RateSGI = 0;
	}
	else //if((sta_info_ra->Decision_rate) > (sta_info_ra->Highest_rate))
	{
		RateID = HighestRate;
		
	}
RateUpfinish:
	//if(pRaInfo->RAWaitingCounter==10)
	if(pRaInfo->RAWaitingCounter==(4+PendingForRateUpFail[pRaInfo->RAPendingCounter]))
		pRaInfo->RAWaitingCounter=0;
	else
		pRaInfo->RAWaitingCounter++;

	pRaInfo->DecisionRate=RateID;
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("Rate up to RateID %d\n", RateID));
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE, ("RAWaitingCounter %d, RAPendingCounter %d",pRaInfo->RAWaitingCounter,pRaInfo->RAPendingCounter));
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE, ("<=====odm_RateUp_8188E() \n"));
	return 0;
}

static void odm_ResetRaCounter_8188E( IN PODM_RA_INFO_T  pRaInfo){
	u1Byte RateID;
	RateID=pRaInfo->DecisionRate;
	pRaInfo->NscUp=(N_THRESHOLD_HIGH[RateID]+N_THRESHOLD_LOW[RateID])>>1;
	pRaInfo->NscDown=(N_THRESHOLD_HIGH[RateID]+N_THRESHOLD_LOW[RateID])>>1;
}

static void 
odm_RateDecision_8188E(
	IN	PDM_ODM_T		pDM_Odm,
	IN 	PODM_RA_INFO_T  pRaInfo
	)
{
	u1Byte RateID = 0, RtyPtID = 0, PenaltyID1 = 0, PenaltyID2 = 0;
	//u4Byte pool_retry;
	static u1Byte DynamicTxRPTTimingCounter=0;
	
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE, ("=====>odm_RateDecision_8188E() \n"));
	
	if (pRaInfo->Active && (pRaInfo->TOTAL > 0)) // STA used and data packet exits
	{
		if ( (pRaInfo->RssiStaRA<(pRaInfo->PreRssiStaRA-3))|| (pRaInfo->RssiStaRA>(pRaInfo->PreRssiStaRA+3))){
			pRaInfo->RAWaitingCounter=0;
			pRaInfo->RAPendingCounter=0;
		}
		// Start RA decision
		if (pRaInfo->PreRate > pRaInfo->HighestRate)
			RateID = pRaInfo->HighestRate;
		else 
			RateID = pRaInfo->PreRate;
		if (pRaInfo->RssiStaRA > RSSI_THRESHOLD[RateID])
			RtyPtID=0;
		else
			RtyPtID=1;
		PenaltyID1 = RETRY_PENALTY_IDX[RtyPtID][RateID]; //TODO by page
		
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE, 
					(" NscDown init is %d\n", pRaInfo->NscDown));
		//pool_retry=pRaInfo->RTY[2]+pRaInfo->RTY[3]+pRaInfo->RTY[4]+pRaInfo->DROP;
		pRaInfo->NscDown += pRaInfo->RTY[0] * RETRY_PENALTY[PenaltyID1][0];
		pRaInfo->NscDown += pRaInfo->RTY[1] * RETRY_PENALTY[PenaltyID1][1];
		pRaInfo->NscDown += pRaInfo->RTY[2] * RETRY_PENALTY[PenaltyID1][2];
		pRaInfo->NscDown += pRaInfo->RTY[3] * RETRY_PENALTY[PenaltyID1][3];
		pRaInfo->NscDown += pRaInfo->RTY[4] * RETRY_PENALTY[PenaltyID1][4];
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE, 
					(" NscDown is %d, total*penalty[5] is %d\n", 
					pRaInfo->NscDown, (pRaInfo->TOTAL * RETRY_PENALTY[PenaltyID1][5])));
		if (pRaInfo->NscDown > (pRaInfo->TOTAL * RETRY_PENALTY[PenaltyID1][5]))
			pRaInfo->NscDown -= pRaInfo->TOTAL * RETRY_PENALTY[PenaltyID1][5];
		else
			pRaInfo->NscDown=0;
		
		// rate up
		PenaltyID2 = RETRY_PENALTY_UP_IDX[RateID];
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE, 
					(" NscUp init is %d\n", pRaInfo->NscUp));
		pRaInfo->NscUp += pRaInfo->RTY[0] * RETRY_PENALTY[PenaltyID2][0];
		pRaInfo->NscUp += pRaInfo->RTY[1] * RETRY_PENALTY[PenaltyID2][1];
		pRaInfo->NscUp += pRaInfo->RTY[2] * RETRY_PENALTY[PenaltyID2][2];
		pRaInfo->NscUp += pRaInfo->RTY[3] * RETRY_PENALTY[PenaltyID2][3];
		pRaInfo->NscUp += pRaInfo->RTY[4] * RETRY_PENALTY[PenaltyID2][4];
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE, 
					("NscUp is %d, total*up[5] is %d\n", 
					pRaInfo->NscUp, (pRaInfo->TOTAL * RETRY_PENALTY[PenaltyID2][5])));
		if (pRaInfo->NscUp > (pRaInfo->TOTAL * RETRY_PENALTY[PenaltyID2][5]))
			pRaInfo->NscUp -= pRaInfo->TOTAL * RETRY_PENALTY[PenaltyID2][5];
		else
			pRaInfo->NscUp = 0;
		
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_RATE_ADAPTIVE|ODM_COMP_INIT, ODM_DBG_LOUD, 
					(" RssiStaRa= %d RtyPtID=%d PenaltyID1=0x%x  PenaltyID2=0x%x RateID=%d NscDown=%d NscUp=%d SGI=%d\n", 
					pRaInfo->RssiStaRA,RtyPtID, PenaltyID1,PenaltyID2, RateID, pRaInfo->NscDown, pRaInfo->NscUp, pRaInfo->RateSGI));
		if ((pRaInfo->NscDown < N_THRESHOLD_LOW[RateID]) ||(pRaInfo->DROP>DROPING_NECESSARY[RateID]))
			odm_RateDown_8188E(pDM_Odm,pRaInfo);
		//else if ((pRaInfo->NscUp > N_THRESHOLD_HIGH[RateID])&&(pool_retry<POOL_RETRY_TH[RateID]))
		else if (pRaInfo->NscUp > N_THRESHOLD_HIGH[RateID])
			odm_RateUp_8188E(pDM_Odm,pRaInfo);

		if(pRaInfo->DecisionRate > pRaInfo->HighestRate)
			pRaInfo->DecisionRate = pRaInfo->HighestRate;
		
		if ((pRaInfo->DecisionRate)==(pRaInfo->PreRate)) 
			DynamicTxRPTTimingCounter+=1;
		else
			DynamicTxRPTTimingCounter=0;

		if (DynamicTxRPTTimingCounter>=4) {
			odm_SetTxRPTTiming_8188E(pDM_Odm,pRaInfo, 1);
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("<=====Rate don't change 4 times, Extend RPT Timing\n"));
			DynamicTxRPTTimingCounter=0;
		}

		pRaInfo->PreRate = pRaInfo->DecisionRate;  //YJ,add,120120

		odm_ResetRaCounter_8188E(  pRaInfo);
	}
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE, ("<=====odm_RateDecision_8188E() \n"));
}

static int 
odm_ARFBRefresh_8188E(
	IN	PDM_ODM_T 		pDM_Odm, 
	IN 	PODM_RA_INFO_T  pRaInfo
	)
{  // Wilson 2011/10/26
	u4Byte MaskFromReg;
	s1Byte i;

	switch(pRaInfo->RateID){
		case RATR_INX_WIRELESS_NGB:
			pRaInfo->RAUseRate=(pRaInfo->RateMask)&0x0f8ff015;
			break;
		case RATR_INX_WIRELESS_NG:
			pRaInfo->RAUseRate=(pRaInfo->RateMask)&0x0f8ff010;
			break;
		case RATR_INX_WIRELESS_NB:
			pRaInfo->RAUseRate=(pRaInfo->RateMask)&0x0f8ff005;
			break;
		case RATR_INX_WIRELESS_N:
			pRaInfo->RAUseRate=(pRaInfo->RateMask)&0x0f8ff000;
			break;
		case RATR_INX_WIRELESS_GB:
			pRaInfo->RAUseRate=(pRaInfo->RateMask)&0x00000ff5;
			break;
		case RATR_INX_WIRELESS_G:
			pRaInfo->RAUseRate=(pRaInfo->RateMask)&0x00000ff0;
			break;
		case RATR_INX_WIRELESS_B:
			pRaInfo->RAUseRate=(pRaInfo->RateMask)&0x0000000d;
			break;
		case 12:			
			MaskFromReg=ODM_Read4Byte(pDM_Odm, REG_ARFR0);
			pRaInfo->RAUseRate=(pRaInfo->RateMask)&MaskFromReg;
			break;
		case 13:
			MaskFromReg=ODM_Read4Byte(pDM_Odm, REG_ARFR1);
			pRaInfo->RAUseRate=(pRaInfo->RateMask)&MaskFromReg;
			break;
		case 14:
			MaskFromReg=ODM_Read4Byte(pDM_Odm, REG_ARFR2);
			pRaInfo->RAUseRate=(pRaInfo->RateMask)&MaskFromReg;
			break;
		case 15:
			MaskFromReg=ODM_Read4Byte(pDM_Odm, REG_ARFR3);
			pRaInfo->RAUseRate=(pRaInfo->RateMask)&MaskFromReg;
			break;
		
		default:
			pRaInfo->RAUseRate=(pRaInfo->RateMask);
			break;
	}
	// Highest rate
	if (pRaInfo->RAUseRate){
		for (i=RATESIZE-1;i>=0;i--)
		{
			if((pRaInfo->RAUseRate)&BIT(i)){
				pRaInfo->HighestRate=i;
				break;
			}
		}
	}
	else{
		pRaInfo->HighestRate=0;
	}
	// Lowest rate
	if (pRaInfo->RAUseRate){
		for (i=0;i<RATESIZE;i++)
		{
			if((pRaInfo->RAUseRate)&BIT(i))
			{
				pRaInfo->LowestRate=i;
				break;
			}
		}
	}
	else{
		pRaInfo->LowestRate=0;
	}
	
#if POWER_TRAINING_ACTIVE == 1
		if (pRaInfo->HighestRate >0x13)
			pRaInfo->PTModeSS=3;
		else if(pRaInfo->HighestRate >0x0b)
			pRaInfo->PTModeSS=2;
		else if(pRaInfo->HighestRate >0x0b)
			pRaInfo->PTModeSS=1;
		else
			pRaInfo->PTModeSS=0;
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, 
				("ODM_ARFBRefresh_8188E(): PTModeSS=%d\n", pRaInfo->PTModeSS));
		
#endif

	if(pRaInfo->DecisionRate > pRaInfo->HighestRate)
		pRaInfo->DecisionRate = pRaInfo->HighestRate;
	
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, 
				("ODM_ARFBRefresh_8188E(): RateID=%d RateMask=%8.8x RAUseRate=%8.8x HighestRate=%d,DecisionRate=%d \n", 
				pRaInfo->RateID, pRaInfo->RateMask, pRaInfo->RAUseRate, pRaInfo->HighestRate,pRaInfo->DecisionRate));
	return 0;
}

#if POWER_TRAINING_ACTIVE == 1
static void 
odm_PTTryState_8188E(
	IN	PDM_ODM_T		pDM_Odm,
	IN 	PODM_RA_INFO_T 	pRaInfo
	)
{
	pRaInfo->PTTryState=0;
	switch (pRaInfo->PTModeSS)
	{
		case 3: 
			if (pRaInfo->DecisionRate>=0x19) 
				pRaInfo->PTTryState=1;
			break;
		case 2:
			if (pRaInfo->DecisionRate>=0x11)
				pRaInfo->PTTryState=1;
			break;	
		case 1:
			if (pRaInfo->DecisionRate>=0x0a)
				pRaInfo->PTTryState=1;
			break;	
		case 0:
			if (pRaInfo->DecisionRate>=0x03)
				pRaInfo->PTTryState=1;
			break;
		default:
			pRaInfo->PTTryState=0;
	}

	if (pRaInfo->RssiStaRA<48)
	{
		pRaInfo->PTStage=0;
	}
	else if (pRaInfo->PTTryState==1)
	{
		if ((pRaInfo->PTStopCount>=10)||(pRaInfo->PTPreRssi>pRaInfo->RssiStaRA+5)
			||(pRaInfo->PTPreRssi<pRaInfo->RssiStaRA-5)||(pRaInfo->DecisionRate!=pRaInfo->PTPreRate))
		{
			if (pRaInfo->PTStage==0)
				pRaInfo->PTStage=1;
			else if(pRaInfo->PTStage==1)
				pRaInfo->PTStage=3;
			else
				pRaInfo->PTStage=5;

			pRaInfo->PTPreRssi=pRaInfo->RssiStaRA;
			pRaInfo->PTStopCount=0;
				
		}
		else{
			pRaInfo->RAstage=0;
			pRaInfo->PTStopCount++;
		}
	}
	else{
		pRaInfo->PTStage=0;
		pRaInfo->RAstage=0;
	}
	pRaInfo->PTPreRate=pRaInfo->DecisionRate;

#if(DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))
	// Disable power training when noisy environment
	if(pDM_Odm->bDisablePowerTraining)
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_RA_MASK, ODM_DBG_LOUD,("odm_PTTryState_8188E(): Disable power training when noisy environment\n"));
		pRaInfo->PTStage = 0;
		pRaInfo->RAstage = 0;
		pRaInfo->PTStopCount = 0;
	}
#endif
}

static void 
odm_PTDecision_8188E(
	IN 	PODM_RA_INFO_T  	pRaInfo
	)
{
	u1Byte stage_BUF;
	u1Byte j;
	u1Byte temp_stage;
	u4Byte numsc;
	u4Byte num_total;
	u1Byte stage_id;
	
	stage_BUF=pRaInfo->PTStage;
	numsc  = 0;
	num_total= pRaInfo->TOTAL* PT_PENALTY[5];
	for(j=0;j<=4;j++)
	{
		numsc += pRaInfo->RTY[j] * PT_PENALTY[j];
		if(numsc>num_total)
			break;
	}

	j=j>>1;
	temp_stage= (pRaInfo->PTStage +1)>>1;
	if (temp_stage>j)
		stage_id=temp_stage-j;
	else
		stage_id=0;
	
	pRaInfo->PTSmoothFactor=(pRaInfo->PTSmoothFactor>>1) + (pRaInfo->PTSmoothFactor>>2) + stage_id*16+2;
	if (pRaInfo->PTSmoothFactor>192)
		pRaInfo->PTSmoothFactor=192;
	stage_id =pRaInfo->PTSmoothFactor>>6;
	temp_stage=stage_id*2;
	if (temp_stage!=0)
		temp_stage-=1;
	if (pRaInfo->DROP>3)
		temp_stage=0;
	pRaInfo->PTStage=temp_stage;

}
#endif

static VOID
odm_RATxRPTTimerSetting(
	IN	PDM_ODM_T		pDM_Odm,
	IN	u2Byte 			minRptTime
)
{
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE,(" =====>odm_RATxRPTTimerSetting()\n"));
	
	
	if(pDM_Odm->CurrminRptTime != minRptTime){
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, 
		(" CurrminRptTime =0x%04x minRptTime=0x%04x\n", pDM_Odm->CurrminRptTime, minRptTime));
		#if(DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_AP))
		ODM_RA_Set_TxRPT_Time(pDM_Odm,minRptTime);	
		#else
		rtw_rpt_timer_cfg_cmd(pDM_Odm->Adapter,minRptTime);
		#endif	
		pDM_Odm->CurrminRptTime = minRptTime;
	}
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE,(" <=====odm_RATxRPTTimerSetting()\n"));
}
	

VOID
ODM_RASupport_Init(
	IN	PDM_ODM_T	pDM_Odm
	)
{	
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("=====>ODM_RASupport_Init()\n"));

	// 2012/02/14 MH Be noticed, the init must be after IC type is recognized!!!!!
	if (pDM_Odm->SupportICType == ODM_RTL8188E)
		pDM_Odm->RaSupport88E = TRUE;
			
		}



int 
ODM_RAInfo_Init(
	IN 	PDM_ODM_T 	pDM_Odm,
	IN 	u1Byte 		MacID	
	)
{
	PODM_RA_INFO_T pRaInfo = &pDM_Odm->RAInfo[MacID];
	#if 1
	u1Byte WirelessMode=0xFF; //invalid value
	u1Byte max_rate_idx = 0x13; //MCS7
	if(pDM_Odm->pWirelessMode!=NULL){
		WirelessMode=*(pDM_Odm->pWirelessMode);			
	}

	if(WirelessMode != 0xFF ){
		if(WirelessMode & ODM_WM_N24G)
			max_rate_idx = 0x13;
		else if(WirelessMode & ODM_WM_G)
			max_rate_idx = 0x0b;
		else if(WirelessMode & ODM_WM_B)
			max_rate_idx = 0x03;
	}
	
	//printk("%s ==>WirelessMode:0x%08x ,max_raid_idx:0x%02x\n ",__FUNCTION__,WirelessMode,max_rate_idx);
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, 
				("ODM_RAInfo_Init(): WirelessMode:0x%08x ,max_raid_idx:0x%02x \n", 
				WirelessMode,max_rate_idx));
		
	pRaInfo->DecisionRate = max_rate_idx;
	pRaInfo->PreRate = max_rate_idx;
	pRaInfo->HighestRate=max_rate_idx;
	#else	
	pRaInfo->DecisionRate = 0x13;
	pRaInfo->PreRate = 0x13;
	pRaInfo->HighestRate=0x13;
	#endif
	pRaInfo->LowestRate=0;
	pRaInfo->RateID=0;
	pRaInfo->RateMask=0xffffffff;
	pRaInfo->RssiStaRA=0;
	pRaInfo->PreRssiStaRA=0;
	pRaInfo->SGIEnable=0;
	pRaInfo->RAUseRate=0xffffffff;
	pRaInfo->NscDown=(N_THRESHOLD_HIGH[0x13]+N_THRESHOLD_LOW[0x13])/2;
	pRaInfo->NscUp=(N_THRESHOLD_HIGH[0x13]+N_THRESHOLD_LOW[0x13])/2;
	pRaInfo->RateSGI=0;
	pRaInfo->Active=1;	//Active is not used at present. by page, 110819
	pRaInfo->RptTime = 0x927c;
	pRaInfo->DROP=0;
	pRaInfo->RTY[0]=0;
	pRaInfo->RTY[1]=0;
	pRaInfo->RTY[2]=0;
	pRaInfo->RTY[3]=0;
	pRaInfo->RTY[4]=0;
	pRaInfo->TOTAL=0;
	pRaInfo->RAWaitingCounter=0;
	pRaInfo->RAPendingCounter=0;
#if POWER_TRAINING_ACTIVE == 1
	pRaInfo->PTActive=1;   // Active when this STA is use
	pRaInfo->PTTryState=0;
	pRaInfo->PTStage=5; // Need to fill into HW_PWR_STATUS
	pRaInfo->PTSmoothFactor=192;
	pRaInfo->PTStopCount=0;
	pRaInfo->PTPreRate=0;
	pRaInfo->PTPreRssi=0;
	pRaInfo->PTModeSS=0;
	pRaInfo->RAstage=0;
#endif
	return 0;
}

int 
ODM_RAInfo_Init_all(
	IN    PDM_ODM_T		pDM_Odm
	)
{
	u1Byte MacID = 0;

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("=====>\n"));
	pDM_Odm->CurrminRptTime = 0;

	for(MacID=0; MacID<ODM_ASSOCIATE_ENTRY_NUM; MacID++)
		ODM_RAInfo_Init(pDM_Odm,MacID);

	//Redifine arrays for I-cut NIC
	if (pDM_Odm->CutVersion == ODM_CUT_I)
	{
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)

		u1Byte i;
		u1Byte RETRY_PENALTY_IDX_S[2][RATESIZE] = {{4,4,4,5,
															4,4,5,7,7,7,8,0x0a,	       // SS>TH
															4,4,4,4,6,0x0a,0x0b,0x0d,
															5,5,7,7,8,0x0b,0x0d,0x0f},	 		   // 0329 R01
															{0x0a,0x0a,0x0b,0x0c,
															0x0a,0x0a,0x0b,0x0c,0x0d,0x10,0x13,0x13,	   // SS<TH
															0x06,0x07,0x08,0x0d,0x0e,0x11,0x11,0x11,
															9,9,9,9,0x0c,0x0e,0x11,0x13}};	

		u1Byte RETRY_PENALTY_UP_IDX_S[RATESIZE] = {0x0c,0x0d,0x0d,0x0f,
															0x0d,0x0e,0x0f,0x0f,0x10,0x12,0x13,0x14,	       // SS>TH
															0x0b,0x0b,0x11,0x11,0x12,0x12,0x12,0x12,
															0x11,0x11,0x12,0x13,0x13,0x13,0x14,0x15};	
		
		for( i=0; i<RATESIZE; i++ )
		{
			RETRY_PENALTY_IDX[0][i] = RETRY_PENALTY_IDX_S[0][i];
			RETRY_PENALTY_IDX[1][i] = RETRY_PENALTY_IDX_S[1][i];
			
			RETRY_PENALTY_UP_IDX[i] = RETRY_PENALTY_UP_IDX_S[i];
		}
		return 0;
#endif
	}
	

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)//This is for non-I-cut
{
	PADAPTER	Adapter = pDM_Odm->Adapter;

	//DbgPrint("Adapter->MgntInfo.RegRALvl = %d\n", Adapter->MgntInfo.RegRALvl);
	
	//
	// 2012/09/14 MH Add for different Ra pattern init. For TPLINK case, we
	// need to to adjust different RA pattern for middle range RA. 20-30dB degarde
	// 88E rate adptve will raise too slow.
	//	
	if (Adapter->MgntInfo.RegRALvl == 0)
	{
		RETRY_PENALTY_UP_IDX[11] = 0x14;
		
		RETRY_PENALTY_UP_IDX[17] = 0x13;
		RETRY_PENALTY_UP_IDX[18] = 0x14;
		RETRY_PENALTY_UP_IDX[19] = 0x15;
		
		RETRY_PENALTY_UP_IDX[23] = 0x13;
		RETRY_PENALTY_UP_IDX[24] = 0x13;
		RETRY_PENALTY_UP_IDX[25] = 0x13;
		RETRY_PENALTY_UP_IDX[26] = 0x14;
		RETRY_PENALTY_UP_IDX[27] = 0x15;
	}
	else if (Adapter->MgntInfo.RegRALvl == 1)
	{
		RETRY_PENALTY_UP_IDX[17] = 0x13;
		RETRY_PENALTY_UP_IDX[18] = 0x13;
		RETRY_PENALTY_UP_IDX[19] = 0x14;
		
		RETRY_PENALTY_UP_IDX[23] = 0x12;
		RETRY_PENALTY_UP_IDX[24] = 0x13;
		RETRY_PENALTY_UP_IDX[25] = 0x13;
		RETRY_PENALTY_UP_IDX[26] = 0x13;
		RETRY_PENALTY_UP_IDX[27] = 0x14;
	}
	else if (Adapter->MgntInfo.RegRALvl == 2)
	{
		// Compile flag default is lvl2, we need not to update.
	}
	else if (Adapter->MgntInfo.RegRALvl >= 0x80)
	{
		u1Byte	index = 0, offset = Adapter->MgntInfo.RegRALvl - 0x80;

		// Reset to default rate adaptive value.
		RETRY_PENALTY_UP_IDX[11] = 0x14;
		
		RETRY_PENALTY_UP_IDX[17] = 0x13;
		RETRY_PENALTY_UP_IDX[18] = 0x14;
		RETRY_PENALTY_UP_IDX[19] = 0x15;
		
		RETRY_PENALTY_UP_IDX[23] = 0x13;
		RETRY_PENALTY_UP_IDX[24] = 0x13;
		RETRY_PENALTY_UP_IDX[25] = 0x13;
		RETRY_PENALTY_UP_IDX[26] = 0x14;
		RETRY_PENALTY_UP_IDX[27] = 0x15;

		if (Adapter->MgntInfo.RegRALvl >= 0x90)
		{
			offset = Adapter->MgntInfo.RegRALvl - 0x90;
			// Lazy mode.
			for (index = 0; index < 28; index++)
			{
				RETRY_PENALTY_UP_IDX[index] += (offset);
			}
		}
		else
		{
			// Aggrasive side.
			for (index = 0; index < 28; index++)
			{
				RETRY_PENALTY_UP_IDX[index] -= (offset);
			}
		}		
		
	}
}
#endif

	return 0;
}


u1Byte
ODM_RA_GetShortGI_8188E(
	IN 	PDM_ODM_T 	pDM_Odm,
	IN 	u1Byte 		MacID
)
{
	if((NULL == pDM_Odm) || (MacID >= ASSOCIATE_ENTRY_NUM))
		return 0;
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE, 
		("MacID=%d SGI=%d\n", MacID, pDM_Odm->RAInfo[MacID].RateSGI));
	return pDM_Odm->RAInfo[MacID].RateSGI;
}

u1Byte 
ODM_RA_GetDecisionRate_8188E(
	IN 	PDM_ODM_T 	pDM_Odm, 
	IN 	u1Byte 		MacID
	)
{
	u1Byte DecisionRate = 0;

	if((NULL == pDM_Odm) || (MacID >= ASSOCIATE_ENTRY_NUM))
		return 0;
	DecisionRate = (pDM_Odm->RAInfo[MacID].DecisionRate);
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE, 
		(" MacID=%d DecisionRate=0x%x\n", MacID, DecisionRate));
	return DecisionRate;
}

u1Byte
ODM_RA_GetHwPwrStatus_8188E(
	IN 	PDM_ODM_T 	pDM_Odm, 
	IN 	u1Byte 		MacID
	)
{
	u1Byte PTStage = 5;
	if((NULL == pDM_Odm) || (MacID >= ASSOCIATE_ENTRY_NUM))
		return 0;
	PTStage = (pDM_Odm->RAInfo[MacID].PTStage);
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE, 
		("MacID=%d PTStage=0x%x\n", MacID, PTStage));
	return PTStage;
}

VOID 
ODM_RA_UpdateRateInfo_8188E(
	IN PDM_ODM_T pDM_Odm,
	IN u1Byte MacID,
	IN u1Byte RateID, 
	IN u4Byte RateMask,
	IN u1Byte SGIEnable
	)
{
	PODM_RA_INFO_T pRaInfo = NULL;
	
	if((NULL == pDM_Odm) || (MacID >= ASSOCIATE_ENTRY_NUM))
		return;
	
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, 
		("MacID=%d RateID=0x%x RateMask=0x%x SGIEnable=%d\n", 
		MacID, RateID, RateMask, SGIEnable));	
	
	pRaInfo = &(pDM_Odm->RAInfo[MacID]);
	pRaInfo->RateID = RateID;
	pRaInfo->RateMask = RateMask;
	pRaInfo->SGIEnable = SGIEnable;
	odm_ARFBRefresh_8188E(pDM_Odm, pRaInfo);
}

VOID 
ODM_RA_SetRSSI_8188E(
	IN 	PDM_ODM_T 		pDM_Odm, 
	IN 	u1Byte 			MacID, 
	IN 	u1Byte 			Rssi
	)
{
	PODM_RA_INFO_T pRaInfo = NULL;

	if((NULL == pDM_Odm) || (MacID >= ASSOCIATE_ENTRY_NUM))
		return;
	
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE, 
		(" MacID=%d Rssi=%d\n", MacID, Rssi));

	pRaInfo = &(pDM_Odm->RAInfo[MacID]);
	pRaInfo->RssiStaRA = Rssi;
}

VOID 
ODM_RA_Set_TxRPT_Time(
	IN	PDM_ODM_T		pDM_Odm,
	IN	u2Byte 			minRptTime
	)
{
#if(DM_ODM_SUPPORT_TYPE & (ODM_AP))
	if (minRptTime != 0xffff)
	{
#if defined(CONFIG_PCI_HCI)
		ODM_Write2Byte(pDM_Odm, REG_TX_RPT_TIME, minRptTime);
#elif defined(CONFIG_USB_HCI) || defined(CONFIG_SDIO_HCI)
		notify_tx_report_interval_change(pDM_Odm->priv, minRptTime);
#endif
	}
#else
	ODM_Write2Byte(pDM_Odm, REG_TX_RPT_TIME, minRptTime);
#endif

}


VOID
ODM_RA_TxRPT2Handle_8188E(	
	IN	PDM_ODM_T		pDM_Odm,
	IN	pu1Byte			TxRPT_Buf,
	IN	u2Byte			TxRPT_Len,
	IN	u4Byte			MacIDValidEntry0,
	IN	u4Byte			MacIDValidEntry1
	)
{
	PODM_RA_INFO_T pRAInfo = NULL;
	u1Byte			MacId = 0;
	pu1Byte			pBuffer = NULL;
	u4Byte			valid = 0, ItemNum = 0;
	u2Byte 			minRptTime = 0x927c;

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("=====>ODM_RA_TxRPT2Handle_8188E(): valid0=%d valid1=%d BufferLength=%d\n",
		MacIDValidEntry0, MacIDValidEntry1, TxRPT_Len));
	
	ItemNum = TxRPT_Len >> 3;
	pBuffer = TxRPT_Buf;

	do
	{
		valid = 0;
		if(MacId < 32)
			valid = (1<<MacId) & MacIDValidEntry0;
		else if(MacId < 64)
			valid = (1<<(MacId-32)) & MacIDValidEntry1;

		if (MacId >= ODM_ASSOCIATE_ENTRY_NUM)
			valid = 0;

		if(valid)
		{
			pRAInfo = &(pDM_Odm->RAInfo[MacId]);

			pRAInfo->RTY[0] = (u2Byte)GET_TX_REPORT_TYPE1_RERTY_0(pBuffer);
			pRAInfo->RTY[1] = (u2Byte)GET_TX_REPORT_TYPE1_RERTY_1(pBuffer);
			pRAInfo->RTY[2] = (u2Byte)GET_TX_REPORT_TYPE1_RERTY_2(pBuffer);
			pRAInfo->RTY[3] = (u2Byte)GET_TX_REPORT_TYPE1_RERTY_3(pBuffer);
			pRAInfo->RTY[4] = (u2Byte)GET_TX_REPORT_TYPE1_RERTY_4(pBuffer);
			pRAInfo->DROP =   (u2Byte)GET_TX_REPORT_TYPE1_DROP_0(pBuffer);

			pRAInfo->TOTAL = pRAInfo->RTY[0] + \
							  pRAInfo->RTY[1] + \
							  pRAInfo->RTY[2] + \
							  pRAInfo->RTY[3] + \
							  pRAInfo->RTY[4] + \
							  pRAInfo->DROP;
#if defined(TXRETRY_CNT)
			extern struct stat_info *get_macidinfo(struct rtl8192cd_priv *priv, unsigned int aid);

			{
				struct stat_info *pstat = get_macidinfo(pDM_Odm->priv, MacId);
				if (pstat) {
					pstat->cur_tx_ok += pRAInfo->RTY[0];
					pstat->cur_tx_retry_pkts += pRAInfo->RTY[1] + pRAInfo->RTY[2] + pRAInfo->RTY[3] + pRAInfo->RTY[4];
					pstat->cur_tx_retry_cnt += pRAInfo->RTY[1] + pRAInfo->RTY[2] * 2 + pRAInfo->RTY[3] * 3 + pRAInfo->RTY[4] * 4;
					pstat->total_tx_retry_cnt += pstat->cur_tx_retry_cnt;
					pstat->total_tx_retry_pkts += pstat->cur_tx_retry_pkts;
					pstat->cur_tx_fail += pRAInfo->DROP;
				}
			}
#endif
			if(pRAInfo->TOTAL != 0)
			{
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, 
							("macid=%d Total=%d R0=%d R1=%d R2=%d R3=%d R4=%d D0=%d valid0=%x valid1=%x\n", 
							MacId,
							pRAInfo->TOTAL,
							pRAInfo->RTY[0],
							pRAInfo->RTY[1],
							pRAInfo->RTY[2],
							pRAInfo->RTY[3],
							pRAInfo->RTY[4],
							pRAInfo->DROP,
							MacIDValidEntry0 ,
							MacIDValidEntry1));
#if POWER_TRAINING_ACTIVE == 1
				if (pRAInfo->PTActive){
					if(pRAInfo->RAstage<5){
						odm_RateDecision_8188E(pDM_Odm,pRAInfo);
					}
					else if(pRAInfo->RAstage==5){  // Power training try state
						odm_PTTryState_8188E(pDM_Odm, pRAInfo);
					}
					else {// RAstage==6
						odm_PTDecision_8188E(pRAInfo);
					}

					// Stage_RA counter
					if (pRAInfo->RAstage<=5)
						pRAInfo->RAstage++;
					else
						pRAInfo->RAstage=0;
				}
				else{
					odm_RateDecision_8188E(pDM_Odm,pRAInfo);
				}
#else
				odm_RateDecision_8188E(pDM_Odm, pRAInfo);
#endif

#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
				extern void RTL8188E_SetStationTxRateInfo(PDM_ODM_T, PODM_RA_INFO_T, int);
				RTL8188E_SetStationTxRateInfo(pDM_Odm, pRAInfo, MacId);
#ifdef DETECT_STA_EXISTANCE
				void RTL8188E_DetectSTAExistance(PDM_ODM_T	pDM_Odm, PODM_RA_INFO_T pRAInfo, int MacID);
				RTL8188E_DetectSTAExistance(pDM_Odm, pRAInfo, MacId);
#endif			
#endif

				ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, 
							("macid=%d R0=%d R1=%d R2=%d R3=%d R4=%d drop=%d valid0=%x RateID=%d SGI=%d\n", 
							MacId,
							pRAInfo->RTY[0],
							pRAInfo->RTY[1],
							pRAInfo->RTY[2],
							pRAInfo->RTY[3],
							pRAInfo->RTY[4],
							pRAInfo->DROP,
							MacIDValidEntry0,
							pRAInfo->DecisionRate,
							pRAInfo->RateSGI));

				if(minRptTime > pRAInfo->RptTime)
					minRptTime = pRAInfo->RptTime;
			}
			else
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, (" TOTAL=0!!!!\n"));
		}

		pBuffer += TX_RPT2_ITEM_SIZE;
		MacId++;
	}while(MacId < ItemNum);
	
        odm_RATxRPTTimerSetting(pDM_Odm,minRptTime);
	

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("<===== ODM_RA_TxRPT2Handle_8188E()\n"));
}

#else

static VOID
odm_RATxRPTTimerSetting(
	IN	PDM_ODM_T		pDM_Odm,
	IN	u2Byte 			minRptTime
)
{
	return;
}


VOID
ODM_RASupport_Init(
	IN	PDM_ODM_T	pDM_Odm
	)
{
	return;
}

int 
ODM_RAInfo_Init(
	IN 	PDM_ODM_T 	pDM_Odm,
	IN 	u1Byte 		MacID		
	)
{
	return 0;
}

int 
ODM_RAInfo_Init_all(
	IN    PDM_ODM_T		pDM_Odm
	)
{
	return 0;
}

u1Byte 
ODM_RA_GetShortGI_8188E(
	IN 	PDM_ODM_T 	pDM_Odm, 
	IN 	u1Byte 		MacID
	)
{
	return 0;
}

u1Byte 
ODM_RA_GetDecisionRate_8188E(
	IN 	PDM_ODM_T 	pDM_Odm, 
	IN 	u1Byte 		MacID
	)
{
	return 0;
}
u1Byte
ODM_RA_GetHwPwrStatus_8188E(
	IN 	PDM_ODM_T 	pDM_Odm, 
	IN 	u1Byte 		MacID
	)
{
	return 0;
}

VOID 
ODM_RA_UpdateRateInfo_8188E(
	IN PDM_ODM_T pDM_Odm,
	IN u1Byte MacID,
	IN u1Byte RateID, 
	IN u4Byte RateMask,
	IN u1Byte SGIEnable
	)
{
	return;
}

VOID 
ODM_RA_SetRSSI_8188E(
	IN 	PDM_ODM_T 		pDM_Odm, 
	IN 	u1Byte 			MacID, 
	IN 	u1Byte 			Rssi
	)
{
	return;
}

VOID 
ODM_RA_Set_TxRPT_Time(
	IN	PDM_ODM_T		pDM_Odm,
	IN	u2Byte 			minRptTime
	)
{
	return;
}

VOID
ODM_RA_TxRPT2Handle_8188E(	
	IN	PDM_ODM_T		pDM_Odm,
	IN	pu1Byte			TxRPT_Buf,
	IN	u2Byte			TxRPT_Len,
	IN	u4Byte			MacIDValidEntry0,
	IN	u4Byte			MacIDValidEntry1
	)
{
	return;
}
	

#endif

