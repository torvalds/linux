/*
 * ============================================================================
 * MTO.C -
 *
 * Description:
 * MAC Throughput Optimization for W89C33 802.11g WLAN STA.
 *
 * The following MIB attributes or internal variables will be affected
 * while the MTO is being executed:
 *	dot11FragmentationThreshold,
 *	dot11RTSThreshold,
 *	transmission rate and PLCP preamble type,
 *	CCA mode,
 *	antenna diversity.
 *
 * Copyright (c) 2003 Winbond Electronics Corp. All rights reserved.
 * ============================================================================
 */

#include "sme_api.h"
#include "wbhal.h"
#include "wb35reg_f.h"
#include "core.h"
#include "mto.h"

/* Declare SQ3 to rate and fragmentation threshold table */
/* Declare fragmentation threshold table */
#define MTO_MAX_FRAG_TH_LEVELS		5
#define MTO_MAX_DATA_RATE_LEVELS	12

u16 MTO_Frag_Th_Tbl[MTO_MAX_FRAG_TH_LEVELS] = {
	256, 384, 512, 768, 1536
};

/*
 * Declare data rate table:
 * The following table will be changed at anytime if the operation rate
 * supported by AP don't match the table
 */
static u8 MTO_Data_Rate_Tbl[MTO_MAX_DATA_RATE_LEVELS] = {
	2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108
};

/* this record the retry rate at different data rate */
static int retryrate_rec[MTO_MAX_DATA_RATE_LEVELS];

static u8 boSparseTxTraffic;

/*
 * ===========================================================================
 * MTO_Init --
 *
 *  Description:
 *    Initialize MTO parameters.
 *
 *    This function should be invoked during system initialization.
 *
 *  Arguments:
 *    adapter      - The pointer to the Miniport adapter Context
 * ===========================================================================
 */
void MTO_Init(struct wbsoft_priv *adapter)
{
	int i;

	MTO_PREAMBLE_TYPE() = MTO_PREAMBLE_SHORT;   /* for test */

	MTO_CNT_ANT(0)			= 0;
	MTO_CNT_ANT(1)			= 0;
	MTO_SQ_ANT(0)			= 0;
	MTO_SQ_ANT(1)			= 0;

	MTO_AGING_TIMEOUT()		= 0;

	/* The following parameters should be initialized to the values set by user */
	MTO_RATE_LEVEL()		= 0;
	MTO_FRAG_TH_LEVEL()		= 4;
	MTO_RTS_THRESHOLD()		= MTO_FRAG_TH() + 1;
	MTO_RTS_THRESHOLD_SETUP()	= MTO_FRAG_TH() + 1;
	MTO_RATE_CHANGE_ENABLE()	= 1;
	MTO_FRAG_CHANGE_ENABLE()	= 0;
	MTO_POWER_CHANGE_ENABLE()	= 1;
	MTO_PREAMBLE_CHANGE_ENABLE()	= 1;
	MTO_RTS_CHANGE_ENABLE()		= 0;

	for (i = 0; i < MTO_MAX_DATA_RATE_LEVELS; i++)
		retryrate_rec[i] = 5;

	MTO_TXFLOWCOUNT() = 0;
	/* --------- DTO threshold parameters ------------- */
	MTOPARA_PERIODIC_CHECK_CYCLE()		= 10;
	MTOPARA_RSSI_TH_FOR_ANTDIV()		= 10;
	MTOPARA_TXCOUNT_TH_FOR_CALC_RATE()	= 50;
	MTOPARA_TXRATE_INC_TH()			= 10;
	MTOPARA_TXRATE_DEC_TH()			= 30;
	MTOPARA_TXRATE_EQ_TH()			= 40;
	MTOPARA_TXRATE_BACKOFF()		= 12;
	MTOPARA_TXRETRYRATE_REDUCE()		= 6;
	if (MTO_TXPOWER_FROM_EEPROM == 0xff) {
		switch (MTO_HAL()->phy_type) {
		case RF_AIROHA_2230:
		case RF_AIROHA_2230S:
			MTOPARA_TXPOWER_INDEX() = 46; /* MAX-8 @@ Only for AL 2230 */
			break;
		case RF_AIROHA_7230:
			MTOPARA_TXPOWER_INDEX() = 49;
			break;
		case RF_WB_242:
			MTOPARA_TXPOWER_INDEX() = 10;
			break;
		case RF_WB_242_1:
			MTOPARA_TXPOWER_INDEX() = 24;
			break;
		}
	} else { /* follow the setting from EEPROM */
		MTOPARA_TXPOWER_INDEX() = MTO_TXPOWER_FROM_EEPROM;
	}
	RFSynthesizer_SetPowerIndex(MTO_HAL(), (u8) MTOPARA_TXPOWER_INDEX());
	/* ------------------------------------------------ */

	/* For RSSI turning -- Cancel load from EEPROM */
	MTO_DATA().RSSI_high = -41;
	MTO_DATA().RSSI_low = -60;
}

/* ===========================================================================
 * Description:
 *	If we enable DTO, we will ignore the tx count with different tx rate
 *	from DTO rate. This is because when we adjust DTO tx rate, there could
 *	be some packets in the tx queue with previous tx rate
 */

void MTO_SetTxCount(struct wbsoft_priv *adapter, u8 tx_rate, u8 index)
{
	MTO_TXFLOWCOUNT()++;
	if ((MTO_ENABLE == 1) && (MTO_RATE_CHANGE_ENABLE() == 1)) {
		if (tx_rate == MTO_DATA_RATE()) {
			if (index == 0) {
				if (boSparseTxTraffic)
					MTO_HAL()->dto_tx_frag_count += MTOPARA_PERIODIC_CHECK_CYCLE();
				else
					MTO_HAL()->dto_tx_frag_count += 1;
			} else {
				if (index < 8) {
					MTO_HAL()->dto_tx_retry_count += index;
					MTO_HAL()->dto_tx_frag_count += (index + 1);
				} else {
					MTO_HAL()->dto_tx_retry_count += 7;
					MTO_HAL()->dto_tx_frag_count += 7;
				}
			}
		} else if (MTO_DATA_RATE() > 48 && tx_rate == 48) {
			/* for reducing data rate scheme, do not calculate different data rate. 3 is the reducing data rate at retry. */
			if (index < 3) {
				MTO_HAL()->dto_tx_retry_count += index;
				MTO_HAL()->dto_tx_frag_count += (index + 1);
			} else {
				MTO_HAL()->dto_tx_retry_count += 3;
				MTO_HAL()->dto_tx_frag_count += 3;
			}

		}
	} else {
		MTO_HAL()->dto_tx_retry_count += index;
		MTO_HAL()->dto_tx_frag_count += (index + 1);
	}
}
