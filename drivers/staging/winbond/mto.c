//============================================================================
//  MTO.C -
//
//  Description:
//    MAC Throughput Optimization for W89C33 802.11g WLAN STA.
//
//    The following MIB attributes or internal variables will be affected
//    while the MTO is being executed:
//       dot11FragmentationThreshold,
//       dot11RTSThreshold,
//       transmission rate and PLCP preamble type,
//       CCA mode,
//       antenna diversity.
//
//  Revision history:
//  --------------------------------------------------------------------------
//           20031227  UN20 Pete Chao
//                     First draft
//  20031229           Turbo                copy from PD43
//  20040210           Kevin                revised
//  Copyright (c) 2003 Winbond Electronics Corp. All rights reserved.
//============================================================================

// LA20040210_DTO kevin
#include "sysdef.h"
#include "sme_api.h"
#include "wbhal_f.h"

// Declare SQ3 to rate and fragmentation threshold table
// Declare fragmentation thresholds table
#define MTO_MAX_FRAG_TH_LEVELS                  5
#define MTO_MAX_DATA_RATE_LEVELS                12

u16 MTO_Frag_Th_Tbl[MTO_MAX_FRAG_TH_LEVELS] =
{
    256, 384, 512, 768, 1536
};

// Declare data rate table
//The following table will be changed at anytime if the opration rate supported by AP don't
//match the table
static u8 MTO_Data_Rate_Tbl[MTO_MAX_DATA_RATE_LEVELS] = {
    2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108
};

static int TotalTxPkt = 0;
static int TotalTxPktRetry = 0;
static int retryrate_rec[MTO_MAX_DATA_RATE_LEVELS];//this record the retry rate at different data rate

static int PeriodTotalTxPkt = 0;
static int PeriodTotalTxPktRetry = 0;

static u8 boSparseTxTraffic = false;

void MTO_Init(struct wbsoft_priv *adapter);
void TxRateReductionCtrl(struct wbsoft_priv *adapter);
/** 1.1.31.1000 Turbo modify */
void MTO_SetTxCount(struct wbsoft_priv *adapter, u8 t0, u8 index);
void MTO_TxFailed(struct wbsoft_priv *adapter);
void hal_get_dto_para(struct wbsoft_priv *adapter, char *buffer);

//===========================================================================
//  MTO_Init --
//
//  Description:
//    Initialize MTO parameters.
//
//    This function should be invoked during system initialization.
//
//  Arguments:
//    adapter      - The pointer to the Miniport adapter Context
//
//  Return Value:
//    None
//============================================================================
void MTO_Init(struct wbsoft_priv *adapter)
{
    int i;
	//[WKCHEN]pMTOcore_data = pcore_data;
// 20040510 Turbo add for global variable
    MTO_TMR_CNT()       = 0;
    MTO_TOGGLE_STATE()  = TOGGLE_STATE_IDLE;
    MTO_TX_RATE_REDUCTION_STATE() = RATE_CHGSTATE_IDLE;
    MTO_BACKOFF_TMR()   = 0;
    MTO_LAST_RATE()     = 11;
    MTO_CO_EFFICENT()   = 0;

    //MTO_TH_FIXANT()     = MTO_DEFAULT_TH_FIXANT;
    MTO_TH_CNT()        = MTO_DEFAULT_TH_CNT;
    MTO_TH_SQ3()        = MTO_DEFAULT_TH_SQ3;
    MTO_TH_IDLE_SLOT()  = MTO_DEFAULT_TH_IDLE_SLOT;
    MTO_TH_PR_INTERF()  = MTO_DEFAULT_TH_PR_INTERF;

    MTO_TMR_AGING()     = MTO_DEFAULT_TMR_AGING;
    MTO_TMR_PERIODIC()  = MTO_DEFAULT_TMR_PERIODIC;

    //[WKCHEN]MTO_CCA_MODE_SETUP()= (u8) hal_get_cca_mode(MTO_HAL());
    //[WKCHEN]MTO_CCA_MODE()      = MTO_CCA_MODE_SETUP();

    //MTO_PREAMBLE_TYPE() = MTO_PREAMBLE_LONG;
    MTO_PREAMBLE_TYPE() = MTO_PREAMBLE_SHORT;   // for test

    MTO_ANT_SEL()       = hal_get_antenna_number(MTO_HAL());
    MTO_ANT_MAC()       = MTO_ANT_SEL();
    MTO_CNT_ANT(0)      = 0;
    MTO_CNT_ANT(1)      = 0;
    MTO_SQ_ANT(0)       = 0;
    MTO_SQ_ANT(1)       = 0;
    MTO_ANT_DIVERSITY() = MTO_ANTENNA_DIVERSITY_ON;
    //CardSet_AntennaDiversity(adapter, MTO_ANT_DIVERSITY());
    //PLMESetAntennaDiversity( adapter, MTO_ANT_DIVERSITY());

    MTO_AGING_TIMEOUT() = 0;//MTO_TMR_AGING() / MTO_TMR_PERIODIC();

    // The following parameters should be initialized to the values set by user
    //
    //MTO_RATE_LEVEL()            = 10;
    MTO_RATE_LEVEL()            = 0;
    MTO_FRAG_TH_LEVEL()         = 4;
    /** 1.1.23.1000 Turbo modify from -1 to +1
	MTO_RTS_THRESHOLD()         = MTO_FRAG_TH() - 1;
    MTO_RTS_THRESHOLD_SETUP()   = MTO_FRAG_TH() - 1;
	*/
	MTO_RTS_THRESHOLD()         = MTO_FRAG_TH() + 1;
    MTO_RTS_THRESHOLD_SETUP()   = MTO_FRAG_TH() + 1;
    // 1.1.23.1000 Turbo add for mto change preamble from 0 to 1
	MTO_RATE_CHANGE_ENABLE()    = 1;
    MTO_FRAG_CHANGE_ENABLE()    = 0;          // 1.1.29.1000 Turbo add don't support frag
	//The default valud of ANTDIV_DEFAULT_ON will be decided by EEPROM
	//#ifdef ANTDIV_DEFAULT_ON
    //MTO_ANT_DIVERSITY_ENABLE()  = 1;
	//#else
    //MTO_ANT_DIVERSITY_ENABLE()  = 0;
	//#endif
    MTO_POWER_CHANGE_ENABLE()   = 1;
	MTO_PREAMBLE_CHANGE_ENABLE()= 1;
    MTO_RTS_CHANGE_ENABLE()     = 0;          // 1.1.29.1000 Turbo add don't support frag
    // 20040512 Turbo add
	//old_antenna[0] = 1;
	//old_antenna[1] = 0;
	//old_antenna[2] = 1;
	//old_antenna[3] = 0;
	for (i=0;i<MTO_MAX_DATA_RATE_LEVELS;i++)
		retryrate_rec[i]=5;

	MTO_TXFLOWCOUNT() = 0;
	//--------- DTO threshold parameters -------------
	//MTOPARA_PERIODIC_CHECK_CYCLE() = 50;
	MTOPARA_PERIODIC_CHECK_CYCLE() = 10;
	MTOPARA_RSSI_TH_FOR_ANTDIV() = 10;
	MTOPARA_TXCOUNT_TH_FOR_CALC_RATE() = 50;
	MTOPARA_TXRATE_INC_TH()	= 10;
	MTOPARA_TXRATE_DEC_TH() = 30;
	MTOPARA_TXRATE_EQ_TH() = 40;
	MTOPARA_TXRATE_BACKOFF() = 12;
	MTOPARA_TXRETRYRATE_REDUCE() = 6;
	if ( MTO_TXPOWER_FROM_EEPROM == 0xff)
	{
		switch( MTO_HAL()->phy_type)
		{
			case RF_AIROHA_2230:
			case RF_AIROHA_2230S: // 20060420 Add this
				MTOPARA_TXPOWER_INDEX() = 46; // MAX-8 // @@ Only for AL 2230
				break;
			case RF_AIROHA_7230:
				MTOPARA_TXPOWER_INDEX() = 49;
				break;
			case RF_WB_242:
				MTOPARA_TXPOWER_INDEX() = 10;
				break;
			case RF_WB_242_1:
				MTOPARA_TXPOWER_INDEX() = 24; // ->10 20060316.1 modify
				break;
		}
	}
	else	//follow the setting from EEPROM
		MTOPARA_TXPOWER_INDEX() = MTO_TXPOWER_FROM_EEPROM;
	hal_set_rf_power(MTO_HAL(), (u8)MTOPARA_TXPOWER_INDEX());
	//------------------------------------------------

	// For RSSI turning 20060808.4 Cancel load from EEPROM
	MTO_DATA().RSSI_high = -41;
	MTO_DATA().RSSI_low = -60;
}

//===========================================================================
//  Description:
//      If we enable DTO, we will ignore the tx count with different tx rate from
//      DTO rate. This is because when we adjust DTO tx rate, there could be some
//      packets in the tx queue with previous tx rate
void MTO_SetTxCount(struct wbsoft_priv *adapter, u8 tx_rate, u8 index)
{
	MTO_TXFLOWCOUNT()++;
	if ((MTO_ENABLE==1) && (MTO_RATE_CHANGE_ENABLE()==1))
	{
		if(tx_rate == MTO_DATA_RATE())
		{
			if (index == 0)
			{
				if (boSparseTxTraffic)
					MTO_HAL()->dto_tx_frag_count += MTOPARA_PERIODIC_CHECK_CYCLE();
				else
					MTO_HAL()->dto_tx_frag_count += 1;
			}
			else
			{
				if (index<8)
				{
					MTO_HAL()->dto_tx_retry_count += index;
					MTO_HAL()->dto_tx_frag_count += (index+1);
				}
				else
				{
					MTO_HAL()->dto_tx_retry_count += 7;
					MTO_HAL()->dto_tx_frag_count += 7;
				}
			}
		}
		else if(MTO_DATA_RATE()>48 && tx_rate ==48)
		{//ALFRED
			if (index<3) //for reduciing data rate scheme ,
				         //do not calcu different data rate
						 //3 is the reducing data rate at retry
			{
				MTO_HAL()->dto_tx_retry_count += index;
				MTO_HAL()->dto_tx_frag_count += (index+1);
			}
			else
			{
				MTO_HAL()->dto_tx_retry_count += 3;
				MTO_HAL()->dto_tx_frag_count += 3;
			}

		}
	}
	else
	{
		MTO_HAL()->dto_tx_retry_count += index;
		MTO_HAL()->dto_tx_frag_count += (index+1);
	}
	TotalTxPkt ++;
	TotalTxPktRetry += (index+1);

	PeriodTotalTxPkt ++;
	PeriodTotalTxPktRetry += (index+1);
}
