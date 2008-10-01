//==================================================================
// MTO.H
//
// Revision history
//=================================
//          20030110    UN20 Pete Chao
//                      Initial Release
//
// Copyright (c) 2003 Winbond Electronics Corp. All rights reserved.
//==================================================================
#ifndef __MTO_H__
#define __MTO_H__

#define MTO_DEFAULT_TH_CNT              5
#define MTO_DEFAULT_TH_SQ3              112  //OLD IS 13 reference JohnXu
#define MTO_DEFAULT_TH_IDLE_SLOT        15
#define MTO_DEFAULT_TH_PR_INTERF        30
#define MTO_DEFAULT_TMR_AGING           25  // unit: slot time  10 reference JohnXu
#define MTO_DEFAULT_TMR_PERIODIC        5   // unit: slot time

#define MTO_ANTENNA_DIVERSITY_OFF       0
#define MTO_ANTENNA_DIVERSITY_ON        1

// LA20040210_DTO kevin
//#define MTO_PREAMBLE_LONG               0
//#define MTO_PREAMBLE_SHORT              1
#define MTO_PREAMBLE_LONG               WLAN_PREAMBLE_TYPE_LONG
#define MTO_PREAMBLE_SHORT              WLAN_PREAMBLE_TYPE_SHORT

typedef enum {
    TOGGLE_STATE_IDLE             = 0,
    TOGGLE_STATE_WAIT0            = 1,
    TOGGLE_STATE_WAIT1            = 2,
    TOGGLE_STATE_MAKEDESISION     = 3,
	TOGGLE_STATE_BKOFF            = 4
} TOGGLE_STATE;

typedef enum {
    RATE_CHGSTATE_IDLE         = 0,
    RATE_CHGSTATE_CALCULATE    = 1,
    RATE_CHGSTATE_BACKOFF	   = 2
} TX_RATE_REDUCTION_STATE;

//============================================================================
// struct _MTOParameters --
//
//   Defines the parameters used in the MAC Throughput Optimization algorithm
//============================================================================
typedef struct _MTO_PARAMETERS
{
	u8      Th_Fixant;
	u8      Th_Cnt;
	u8      Th_SQ3;
	u8      Th_IdleSlot;

	u16     Tmr_Aging;
	u8      Th_PrInterf;
	u8      Tmr_Periodic;

	//---------        wkchen added      -------------
	u32		TxFlowCount;	//to judge what kind the tx flow(sparse or busy) is
	//------------------------------------------------

	//--------- DTO threshold parameters -------------
	u16		DTO_PeriodicCheckCycle;
	u16		DTO_RssiThForAntDiv;

	u16		DTO_TxCountThForCalcNewRate;
	u16		DTO_TxRateIncTh;

	u16		DTO_TxRateDecTh;
	u16		DTO_TxRateEqTh;

	u16		DTO_TxRateBackOff;
	u16		DTO_TxRetryRateReduce;

	u16		DTO_TxPowerIndex;	//0 ~ 31
	u16		reserved_1;
	//------------------------------------------------

	u8      PowerChangeEnable;
	u8      AntDiversityEnable;
	u8      Ant_mac;
	u8      Ant_div;

	u8      CCA_Mode;
	u8      CCA_Mode_Setup;
	u8      Preamble_Type;
	u8      PreambleChangeEnable;

	u8      DataRateLevel;
	u8      DataRateChangeEnable;
	u8      FragThresholdLevel;
	u8      FragThresholdChangeEnable;

	u16     RTSThreshold;
	u16     RTSThreshold_Setup;

	u32     AvgIdleSlot;
	u32     Pr_Interf;
	u32     AvgGapBtwnInterf;

	u8	   RTSChangeEnable;
	u8      Ant_sel;
	u8      aging_timeout;
	u8	   reserved_2;

	u32     Cnt_Ant[2];
	u32     SQ_Ant[2];

// 20040510 remove from globe vairable
	u32                     TmrCnt;
	u32                     BackoffTmr;
	TOGGLE_STATE            ToggleState;
	TX_RATE_REDUCTION_STATE TxRateReductionState;

	u8                      Last_Rate;
	u8                      Co_efficent;
	u8		FallbackRateLevel;
	u8		OfdmRateLevel;

	u8		RatePolicy;
	u8		reserved_3[3];

	// For RSSI turning
	s32		RSSI_high;
	s32		RSSI_low;

} MTO_PARAMETERS, *PMTO_PARAMETERS;


#define MTO_FUNC_INPUT              PWB32_ADAPTER	Adapter
#define MTO_FUNC_INPUT_DATA         Adapter
#define MTO_DATA()                  (Adapter->sMtoPara)
#define MTO_HAL()                   (&Adapter->sHwData)
#define MTO_SET_PREAMBLE_TYPE(x)    // 20040511 Turbo mark LM_PREAMBLE_TYPE(&pcore_data->lm_data) = (x)
#define MTO_ENABLE					(Adapter->sLocalPara.TxRateMode == RATE_AUTO)
#define MTO_TXPOWER_FROM_EEPROM		(Adapter->sHwData.PowerIndexFromEEPROM)
#define LOCAL_ANTENNA_NO()			(Adapter->sLocalPara.bAntennaNo)
#define LOCAL_IS_CONNECTED()		(Adapter->sLocalPara.wConnectedSTAindex != 0)
#define LOCAL_IS_IBSS_MODE()		(Adapter->asBSSDescriptElement[Adapter->sLocalPara.wConnectedSTAindex].bBssType == IBSS_NET)
#define MTO_INITTXRATE_MODE			(Adapter->sHwData.SoftwareSet&0x2)	//bit 1
// 20040510 Turbo add
#define MTO_TMR_CNT()               MTO_DATA().TmrCnt
#define MTO_TOGGLE_STATE()          MTO_DATA().ToggleState
#define MTO_TX_RATE_REDUCTION_STATE() MTO_DATA().TxRateReductionState
#define MTO_BACKOFF_TMR()           MTO_DATA().BackoffTmr
#define MTO_LAST_RATE()             MTO_DATA().Last_Rate
#define MTO_CO_EFFICENT()           MTO_DATA().Co_efficent

#define MTO_TH_CNT()                MTO_DATA().Th_Cnt
#define MTO_TH_SQ3()                MTO_DATA().Th_SQ3
#define MTO_TH_IDLE_SLOT()          MTO_DATA().Th_IdleSlot
#define MTO_TH_PR_INTERF()          MTO_DATA().Th_PrInterf

#define MTO_TMR_AGING()             MTO_DATA().Tmr_Aging
#define MTO_TMR_PERIODIC()          MTO_DATA().Tmr_Periodic

#define MTO_POWER_CHANGE_ENABLE()   MTO_DATA().PowerChangeEnable
#define MTO_ANT_DIVERSITY_ENABLE()  Adapter->sLocalPara.boAntennaDiversity
#define MTO_ANT_MAC()               MTO_DATA().Ant_mac
#define MTO_ANT_DIVERSITY()         MTO_DATA().Ant_div
#define MTO_CCA_MODE()              MTO_DATA().CCA_Mode
#define MTO_CCA_MODE_SETUP()        MTO_DATA().CCA_Mode_Setup
#define MTO_PREAMBLE_TYPE()         MTO_DATA().Preamble_Type
#define MTO_PREAMBLE_CHANGE_ENABLE()         MTO_DATA().PreambleChangeEnable

#define MTO_RATE_LEVEL()            MTO_DATA().DataRateLevel
#define MTO_FALLBACK_RATE_LEVEL()	MTO_DATA().FallbackRateLevel
#define MTO_OFDM_RATE_LEVEL()		MTO_DATA().OfdmRateLevel
#define MTO_RATE_CHANGE_ENABLE()    MTO_DATA().DataRateChangeEnable
#define MTO_FRAG_TH_LEVEL()         MTO_DATA().FragThresholdLevel
#define MTO_FRAG_CHANGE_ENABLE()    MTO_DATA().FragThresholdChangeEnable
#define MTO_RTS_THRESHOLD()         MTO_DATA().RTSThreshold
#define MTO_RTS_CHANGE_ENABLE()     MTO_DATA().RTSChangeEnable
#define MTO_RTS_THRESHOLD_SETUP()   MTO_DATA().RTSThreshold_Setup

#define MTO_AVG_IDLE_SLOT()         MTO_DATA().AvgIdleSlot
#define MTO_PR_INTERF()             MTO_DATA().Pr_Interf
#define MTO_AVG_GAP_BTWN_INTERF()   MTO_DATA().AvgGapBtwnInterf

#define MTO_ANT_SEL()               MTO_DATA().Ant_sel
#define MTO_CNT_ANT(x)              MTO_DATA().Cnt_Ant[(x)]
#define MTO_SQ_ANT(x)               MTO_DATA().SQ_Ant[(x)]
#define MTO_AGING_TIMEOUT()         MTO_DATA().aging_timeout


#define MTO_TXFLOWCOUNT()			MTO_DATA().TxFlowCount
//--------- DTO threshold parameters -------------
#define	MTOPARA_PERIODIC_CHECK_CYCLE()		MTO_DATA().DTO_PeriodicCheckCycle
#define	MTOPARA_RSSI_TH_FOR_ANTDIV()		MTO_DATA().DTO_RssiThForAntDiv
#define	MTOPARA_TXCOUNT_TH_FOR_CALC_RATE()	MTO_DATA().DTO_TxCountThForCalcNewRate
#define	MTOPARA_TXRATE_INC_TH()			MTO_DATA().DTO_TxRateIncTh
#define	MTOPARA_TXRATE_DEC_TH()			MTO_DATA().DTO_TxRateDecTh
#define MTOPARA_TXRATE_EQ_TH()			MTO_DATA().DTO_TxRateEqTh
#define	MTOPARA_TXRATE_BACKOFF()		MTO_DATA().DTO_TxRateBackOff
#define	MTOPARA_TXRETRYRATE_REDUCE()		MTO_DATA().DTO_TxRetryRateReduce
#define MTOPARA_TXPOWER_INDEX()			MTO_DATA().DTO_TxPowerIndex
//------------------------------------------------


extern u8   MTO_Data_Rate_Tbl[];
extern u16  MTO_Frag_Th_Tbl[];

#define MTO_DATA_RATE()          MTO_Data_Rate_Tbl[MTO_RATE_LEVEL()]
#define MTO_DATA_FALLBACK_RATE() MTO_Data_Rate_Tbl[MTO_FALLBACK_RATE_LEVEL()]	//next level
#define MTO_FRAG_TH()            MTO_Frag_Th_Tbl[MTO_FRAG_TH_LEVEL()]

typedef struct {
	u8 tx_rate;
	u8 tx_retry_rate;
} TXRETRY_REC;

typedef struct _STATISTICS_INFO {
	u32   Rate54M;
	u32   Rate48M;
	u32   Rate36M;
	u32   Rate24M;
	u32   Rate18M;
	u32   Rate12M;
	u32   Rate9M;
	u32   Rate6M;
	u32   Rate11MS;
	u32   Rate11ML;
	u32   Rate55MS;
	u32   Rate55ML;
	u32   Rate2MS;
	u32   Rate2ML;
	u32   Rate1M;
	u32   Rate54MOK;
	u32   Rate48MOK;
	u32   Rate36MOK;
	u32   Rate24MOK;
	u32   Rate18MOK;
	u32   Rate12MOK;
	u32   Rate9MOK;
	u32   Rate6MOK;
	u32   Rate11MSOK;
	u32   Rate11MLOK;
	u32   Rate55MSOK;
	u32   Rate55MLOK;
	u32   Rate2MSOK;
	u32   Rate2MLOK;
	u32   Rate1MOK;
	u32   SQ3;
	s32   RSSIAVG;
	s32   RSSIMAX;
	s32   TXRATE;
	s32   TxRetryRate;
	s32   BSS_PK_CNT;
	s32   NIDLESLOT;
	s32   SLOT_CNT;
	s32   INTERF_CNT;
	s32   GAP_CNT;
	s32   DS_EVM;
	s32   RcvBeaconNum;
	s32   RXRATE;
	s32   RxBytes;
	s32   TxBytes;
	s32   Antenna;
} STATISTICS_INFO, *PSTATISTICS_INFO;

#endif //__MTO_H__


