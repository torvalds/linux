/*
 * ==================================================================
 * MTO.H
 *
 * Copyright (c) 2003 Winbond Electronics Corp. All rights reserved.
 * ==================================================================
 */
#ifndef __MTO_H__
#define __MTO_H__

#include <linux/types.h>

struct wbsoft_priv;

#define MTO_PREAMBLE_LONG               WLAN_PREAMBLE_TYPE_LONG
#define MTO_PREAMBLE_SHORT              WLAN_PREAMBLE_TYPE_SHORT

/* Defines the parameters used in the MAC Throughput Optimization algorithm */
struct wb35_mto_params {
	u32	TxFlowCount; /* to judge what kind the tx flow(sparse or busy) is */

	/* --------- DTO threshold parameters ------------- */
	u16	DTO_PeriodicCheckCycle;
	u16	DTO_RssiThForAntDiv;

	u16	DTO_TxCountThForCalcNewRate;
	u16	DTO_TxRateIncTh;

	u16	DTO_TxRateDecTh;
	u16	DTO_TxRateEqTh;

	u16	DTO_TxRateBackOff;
	u16	DTO_TxRetryRateReduce;

	u16	DTO_TxPowerIndex;		/* 0 ~ 31 */
	u16	reserved_1;
	/* ------------------------------------------------ */

	u8	PowerChangeEnable;
	u8	AntDiversityEnable;
	u8	CCA_Mode;
	u8	CCA_Mode_Setup;
	u8	Preamble_Type;
	u8	PreambleChangeEnable;

	u8	DataRateLevel;
	u8	DataRateChangeEnable;
	u8	FragThresholdLevel;
	u8	FragThresholdChangeEnable;

	u16	RTSThreshold;
	u16	RTSThreshold_Setup;

	u32	AvgIdleSlot;
	u32	Pr_Interf;
	u32	AvgGapBtwnInterf;

	u8	RTSChangeEnable;
	u8	Ant_sel;
	u8	aging_timeout;
	u8	reserved_2;

	u32	Cnt_Ant[2];
	u32	SQ_Ant[2];

	u8	FallbackRateLevel;
	u8	OfdmRateLevel;

	u8	RatePolicy;
	u8	reserved_3[3];

	/* For RSSI turning */
	s32	RSSI_high;
	s32	RSSI_low;
};


#define MTO_DATA()		(adapter->sMtoPara)
#define MTO_HAL()		(&adapter->sHwData)
#define MTO_SET_PREAMBLE_TYPE(x) /* Turbo mark LM_PREAMBLE_TYPE(&pcore_data->lm_data) = (x) */
#define MTO_ENABLE		(adapter->sLocalPara.TxRateMode == RATE_AUTO)
#define MTO_TXPOWER_FROM_EEPROM	(adapter->sHwData.PowerIndexFromEEPROM)
#define LOCAL_ANTENNA_NO()	(adapter->sLocalPara.bAntennaNo)
#define LOCAL_IS_CONNECTED()	(adapter->sLocalPara.wConnectedSTAindex != 0)
#define MTO_INITTXRATE_MODE	(adapter->sHwData.SoftwareSet&0x2) /* bit 1 */

#define MTO_POWER_CHANGE_ENABLE()	MTO_DATA().PowerChangeEnable
#define MTO_CCA_MODE()			MTO_DATA().CCA_Mode
#define MTO_CCA_MODE_SETUP()		MTO_DATA().CCA_Mode_Setup
#define MTO_PREAMBLE_TYPE()		MTO_DATA().Preamble_Type
#define MTO_PREAMBLE_CHANGE_ENABLE()	MTO_DATA().PreambleChangeEnable

#define MTO_RATE_LEVEL()		MTO_DATA().DataRateLevel
#define MTO_OFDM_RATE_LEVEL()		MTO_DATA().OfdmRateLevel
#define MTO_RATE_CHANGE_ENABLE()	MTO_DATA().DataRateChangeEnable
#define MTO_FRAG_TH_LEVEL()		MTO_DATA().FragThresholdLevel
#define MTO_FRAG_CHANGE_ENABLE()	MTO_DATA().FragThresholdChangeEnable
#define MTO_RTS_THRESHOLD()		MTO_DATA().RTSThreshold
#define MTO_RTS_CHANGE_ENABLE()		MTO_DATA().RTSChangeEnable
#define MTO_RTS_THRESHOLD_SETUP()	MTO_DATA().RTSThreshold_Setup

#define MTO_AVG_IDLE_SLOT()		MTO_DATA().AvgIdleSlot
#define MTO_PR_INTERF()			MTO_DATA().Pr_Interf
#define MTO_AVG_GAP_BTWN_INTERF()	MTO_DATA().AvgGapBtwnInterf

#define MTO_CNT_ANT(x)			MTO_DATA().Cnt_Ant[(x)]
#define MTO_SQ_ANT(x)			MTO_DATA().SQ_Ant[(x)]
#define MTO_AGING_TIMEOUT()		MTO_DATA().aging_timeout

#define MTO_TXFLOWCOUNT()		MTO_DATA().TxFlowCount

/* --------- DTO threshold parameters ------------- */
#define	MTOPARA_PERIODIC_CHECK_CYCLE()		MTO_DATA().DTO_PeriodicCheckCycle
#define	MTOPARA_RSSI_TH_FOR_ANTDIV()		MTO_DATA().DTO_RssiThForAntDiv
#define	MTOPARA_TXCOUNT_TH_FOR_CALC_RATE()	MTO_DATA().DTO_TxCountThForCalcNewRate
#define	MTOPARA_TXRATE_INC_TH()			MTO_DATA().DTO_TxRateIncTh
#define	MTOPARA_TXRATE_DEC_TH()			MTO_DATA().DTO_TxRateDecTh
#define MTOPARA_TXRATE_EQ_TH()			MTO_DATA().DTO_TxRateEqTh
#define	MTOPARA_TXRATE_BACKOFF()		MTO_DATA().DTO_TxRateBackOff
#define	MTOPARA_TXRETRYRATE_REDUCE()		MTO_DATA().DTO_TxRetryRateReduce
#define MTOPARA_TXPOWER_INDEX()			MTO_DATA().DTO_TxPowerIndex
/* ------------------------------------------------ */


extern u16 MTO_Frag_Th_Tbl[];

#define MTO_DATA_RATE()			MTO_Data_Rate_Tbl[MTO_RATE_LEVEL()]
#define MTO_FRAG_TH()			MTO_Frag_Th_Tbl[MTO_FRAG_TH_LEVEL()]

void MTO_Init(struct wbsoft_priv *);
void MTO_SetTxCount(struct wbsoft_priv *adapter, u8 t0, u8 index);

#endif /* __MTO_H__ */

