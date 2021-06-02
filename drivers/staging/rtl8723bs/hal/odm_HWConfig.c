// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/

#include "odm_precomp.h"

#define READ_AND_CONFIG_MP(ic, txt) (ODM_ReadAndConfig_MP_##ic##txt(pDM_Odm))
#define READ_AND_CONFIG     READ_AND_CONFIG_MP
#define GET_VERSION_MP(ic, txt) (ODM_GetVersion_MP_##ic##txt())
#define GET_VERSION(ic, txt) (pDM_Odm->bIsMPChip?GET_VERSION_MP(ic, txt):GET_VERSION_TC(ic, txt))

static u8 odm_QueryRxPwrPercentage(s8 AntPower)
{
	if ((AntPower <= -100) || (AntPower >= 20))
		return	0;
	else if (AntPower >= 0)
		return	100;
	else
		return 100 + AntPower;

}

s32 odm_SignalScaleMapping(struct dm_odm_t *pDM_Odm, s32 CurrSig)
{
	s32 RetSig = 0;

	if (pDM_Odm->SupportInterface  == ODM_ITRF_SDIO) {
		if (CurrSig >= 51 && CurrSig <= 100)
			RetSig = 100;
		else if (CurrSig >= 41 && CurrSig <= 50)
			RetSig = 80 + ((CurrSig - 40)*2);
		else if (CurrSig >= 31 && CurrSig <= 40)
			RetSig = 66 + (CurrSig - 30);
		else if (CurrSig >= 21 && CurrSig <= 30)
			RetSig = 54 + (CurrSig - 20);
		else if (CurrSig >= 10 && CurrSig <= 20)
			RetSig = 42 + (((CurrSig - 10) * 2) / 3);
		else if (CurrSig >= 5 && CurrSig <= 9)
			RetSig = 22 + (((CurrSig - 5) * 3) / 2);
		else if (CurrSig >= 1 && CurrSig <= 4)
			RetSig = 6 + (((CurrSig - 1) * 3) / 2);
		else
			RetSig = CurrSig;
	}

	return RetSig;
}

static u8 odm_EVMdbToPercentage(s8 Value)
{
	/*  */
	/*  -33dB~0dB to 0%~99% */
	/*  */
	s8 ret_val;

	ret_val = Value;
	ret_val /= 2;

	/* DbgPrint("Value =%d\n", Value); */
	/* ODM_RT_DISP(FRX, RX_PHY_SQ, ("EVMdbToPercentage92C Value =%d / %x\n", ret_val, ret_val)); */

	if (ret_val >= 0)
		ret_val = 0;
	if (ret_val <= -33)
		ret_val = -33;

	ret_val = 0 - ret_val;
	ret_val *= 3;

	if (ret_val == 99)
		ret_val = 100;

	return ret_val;
}

static void odm_RxPhyStatus92CSeries_Parsing(
	struct dm_odm_t *pDM_Odm,
	struct odm_phy_info *pPhyInfo,
	u8 *pPhyStatus,
	struct odm_packet_info *pPktinfo
)
{
	u8 i, Max_spatial_stream;
	s8 rx_pwr[4], rx_pwr_all = 0;
	u8 EVM, PWDB_ALL = 0, PWDB_ALL_BT;
	u8 RSSI, total_rssi = 0;
	bool isCCKrate = false;
	u8 rf_rx_num = 0;
	u8 LNA_idx, VGA_idx;
	struct phy_status_rpt_8192cd_t *pPhyStaRpt = (struct phy_status_rpt_8192cd_t *)pPhyStatus;

	isCCKrate = pPktinfo->data_rate <= DESC_RATE11M;
	pPhyInfo->rx_mimo_signal_quality[ODM_RF_PATH_A] = -1;
	pPhyInfo->rx_mimo_signal_quality[ODM_RF_PATH_B] = -1;


	if (isCCKrate) {
		u8 cck_agc_rpt;

		pDM_Odm->PhyDbgInfo.NumQryPhyStatusCCK++;
		/*  */
		/*  (1)Hardware does not provide RSSI for CCK */
		/*  (2)PWDB, Average PWDB calculated by hardware (for rate adaptive) */
		/*  */

		cck_agc_rpt = pPhyStaRpt->cck_agc_rpt_ofdm_cfosho_a;

		/* 2011.11.28 LukeLee: 88E use different LNA & VGA gain table */
		/* The RSSI formula should be modified according to the gain table */
		LNA_idx = ((cck_agc_rpt & 0xE0)>>5);
		VGA_idx = (cck_agc_rpt & 0x1F);
		rx_pwr_all = odm_CCKRSSI_8723B(LNA_idx, VGA_idx);
		PWDB_ALL = odm_QueryRxPwrPercentage(rx_pwr_all);
		if (PWDB_ALL > 100)
			PWDB_ALL = 100;

		pPhyInfo->rx_pwd_ba11 = PWDB_ALL;
		pPhyInfo->bt_rx_rssi_percentage = PWDB_ALL;
		pPhyInfo->recv_signal_power = rx_pwr_all;
		/*  */
		/*  (3) Get Signal Quality (EVM) */
		/*  */
		/* if (pPktinfo->bPacketMatchBSSID) */
		{
			u8 SQ, SQ_rpt;

			if (pPhyInfo->rx_pwd_ba11 > 40 && !pDM_Odm->bInHctTest)
				SQ = 100;
			else {
				SQ_rpt = pPhyStaRpt->cck_sig_qual_ofdm_pwdb_all;

				if (SQ_rpt > 64)
					SQ = 0;
				else if (SQ_rpt < 20)
					SQ = 100;
				else
					SQ = ((64-SQ_rpt) * 100) / 44;

			}

			/* DbgPrint("cck SQ = %d\n", SQ); */
			pPhyInfo->signal_quality = SQ;
			pPhyInfo->rx_mimo_signal_quality[ODM_RF_PATH_A] = SQ;
			pPhyInfo->rx_mimo_signal_quality[ODM_RF_PATH_B] = -1;
		}
	} else { /* is OFDM rate */
		pDM_Odm->PhyDbgInfo.NumQryPhyStatusOFDM++;

		/*  */
		/*  (1)Get RSSI for HT rate */
		/*  */

		for (i = ODM_RF_PATH_A; i < ODM_RF_PATH_MAX; i++) {
			/*  2008/01/30 MH we will judge RF RX path now. */
			if (pDM_Odm->RFPathRxEnable & BIT(i))
				rf_rx_num++;
			/* else */
				/* continue; */

			rx_pwr[i] = ((pPhyStaRpt->path_agc[i].gain&0x3F)*2) - 110;


			pPhyInfo->rx_pwr[i] = rx_pwr[i];

			/* Translate DBM to percentage. */
			RSSI = odm_QueryRxPwrPercentage(rx_pwr[i]);
			total_rssi += RSSI;
			/* RT_DISP(FRX, RX_PHY_SS, ("RF-%d RXPWR =%x RSSI =%d\n", i, rx_pwr[i], RSSI)); */

			pPhyInfo->rx_mimo_signal_strength[i] = (u8) RSSI;

			/* Get Rx snr value in DB */
			pPhyInfo->rx_snr[i] = pDM_Odm->PhyDbgInfo.RxSNRdB[i] = (s32)(pPhyStaRpt->path_rxsnr[i]/2);
		}


		/*  */
		/*  (2)PWDB, Average PWDB calculated by hardware (for rate adaptive) */
		/*  */
		rx_pwr_all = (((pPhyStaRpt->cck_sig_qual_ofdm_pwdb_all) >> 1)&0x7f)-110;

		PWDB_ALL_BT = PWDB_ALL = odm_QueryRxPwrPercentage(rx_pwr_all);
		/* RT_DISP(FRX, RX_PHY_SS, ("PWDB_ALL =%d\n", PWDB_ALL)); */

		pPhyInfo->rx_pwd_ba11 = PWDB_ALL;
		/* ODM_RT_TRACE(pDM_Odm, ODM_COMP_RSSI_MONITOR, ODM_DBG_LOUD, ("ODM OFDM RSSI =%d\n", pPhyInfo->rx_pwd_ba11)); */
		pPhyInfo->bt_rx_rssi_percentage = PWDB_ALL_BT;
		pPhyInfo->rx_power = rx_pwr_all;
		pPhyInfo->recv_signal_power = rx_pwr_all;

		{/* pMgntInfo->CustomerID != RT_CID_819x_Lenovo */
			/*  */
			/*  (3)EVM of HT rate */
			/*  */
			if (pPktinfo->data_rate >= DESC_RATEMCS8 && pPktinfo->data_rate <= DESC_RATEMCS15)
				Max_spatial_stream = 2; /* both spatial stream make sense */
			else
				Max_spatial_stream = 1; /* only spatial stream 1 makes sense */

			for (i = 0; i < Max_spatial_stream; i++) {
				/*  Do not use shift operation like "rx_evmX >>= 1" because the compilor of free build environment */
				/*  fill most significant bit to "zero" when doing shifting operation which may change a negative */
				/*  value to positive one, then the dbm value (which is supposed to be negative)  is not correct anymore. */
				EVM = odm_EVMdbToPercentage((pPhyStaRpt->stream_rxevm[i]));	/* dbm */

				/* RT_DISP(FRX, RX_PHY_SQ, ("RXRATE =%x RXEVM =%x EVM =%s%d\n", */
				/* GET_RX_STATUS_DESC_RX_MCS(pDesc), pDrvInfo->rxevm[i], "%", EVM)); */

				/* if (pPktinfo->bPacketMatchBSSID) */
				{
					if (i == ODM_RF_PATH_A) /*  Fill value in RFD, Get the first spatial stream only */
						pPhyInfo->signal_quality = (u8)(EVM & 0xff);

					pPhyInfo->rx_mimo_signal_quality[i] = (u8)(EVM & 0xff);
				}
			}
		}

		ODM_ParsingCFO(pDM_Odm, pPktinfo, pPhyStaRpt->path_cfotail);

	}

	/* UI BSS List signal strength(in percentage), make it good looking, from 0~100. */
	/* It is assigned to the BSS List in GetValueFromBeaconOrProbeRsp(). */
	if (isCCKrate) {
		pPhyInfo->signal_strength = (u8)(odm_SignalScaleMapping(pDM_Odm, PWDB_ALL));/* PWDB_ALL; */
	} else {
		if (rf_rx_num != 0) {
			pPhyInfo->signal_strength = (u8)(odm_SignalScaleMapping(pDM_Odm, total_rssi /= rf_rx_num));
		}
	}

	/* DbgPrint("isCCKrate = %d, pPhyInfo->rx_pwd_ba11 = %d, pPhyStaRpt->cck_agc_rpt_ofdm_cfosho_a = 0x%x\n", */
		/* isCCKrate, pPhyInfo->rx_pwd_ba11, pPhyStaRpt->cck_agc_rpt_ofdm_cfosho_a); */
}

static void odm_Process_RSSIForDM(
	struct dm_odm_t *pDM_Odm, struct odm_phy_info *pPhyInfo, struct odm_packet_info *pPktinfo
)
{

	s32 UndecoratedSmoothedPWDB, UndecoratedSmoothedCCK, UndecoratedSmoothedOFDM, RSSI_Ave;
	u8 isCCKrate = 0;
	u8 RSSI_max, RSSI_min, i;
	u32 OFDM_pkt = 0;
	u32 Weighting = 0;
	PSTA_INFO_T pEntry;


	if (pPktinfo->station_id == 0xFF)
		return;

	pEntry = pDM_Odm->pODM_StaInfo[pPktinfo->station_id];

	if (!IS_STA_VALID(pEntry))
		return;

	if ((!pPktinfo->bssid_match))
		return;

	if (pPktinfo->is_beacon)
		pDM_Odm->PhyDbgInfo.NumQryBeaconPkt++;

	isCCKrate = ((pPktinfo->data_rate <= DESC_RATE11M)) ? true : false;
	pDM_Odm->RxRate = pPktinfo->data_rate;

	/* Statistic for antenna/path diversity------------------ */
	if (pDM_Odm->SupportAbility & ODM_BB_ANT_DIV) {

	}

	/* Smart Antenna Debug Message------------------ */

	UndecoratedSmoothedCCK = pEntry->rssi_stat.UndecoratedSmoothedCCK;
	UndecoratedSmoothedOFDM = pEntry->rssi_stat.UndecoratedSmoothedOFDM;
	UndecoratedSmoothedPWDB = pEntry->rssi_stat.UndecoratedSmoothedPWDB;

	if (pPktinfo->to_self || pPktinfo->is_beacon) {

		if (!isCCKrate) { /* ofdm rate */
			if (pPhyInfo->rx_mimo_signal_strength[ODM_RF_PATH_B] == 0) {
				RSSI_Ave = pPhyInfo->rx_mimo_signal_strength[ODM_RF_PATH_A];
				pDM_Odm->RSSI_A = pPhyInfo->rx_mimo_signal_strength[ODM_RF_PATH_A];
				pDM_Odm->RSSI_B = 0;
			} else {
				/* DbgPrint("pRfd->Status.rx_mimo_signal_strength[0] = %d, pRfd->Status.rx_mimo_signal_strength[1] = %d\n", */
					/* pRfd->Status.rx_mimo_signal_strength[0], pRfd->Status.rx_mimo_signal_strength[1]); */
				pDM_Odm->RSSI_A =  pPhyInfo->rx_mimo_signal_strength[ODM_RF_PATH_A];
				pDM_Odm->RSSI_B = pPhyInfo->rx_mimo_signal_strength[ODM_RF_PATH_B];

				if (
					pPhyInfo->rx_mimo_signal_strength[ODM_RF_PATH_A] >
					pPhyInfo->rx_mimo_signal_strength[ODM_RF_PATH_B]
				) {
					RSSI_max = pPhyInfo->rx_mimo_signal_strength[ODM_RF_PATH_A];
					RSSI_min = pPhyInfo->rx_mimo_signal_strength[ODM_RF_PATH_B];
				} else {
					RSSI_max = pPhyInfo->rx_mimo_signal_strength[ODM_RF_PATH_B];
					RSSI_min = pPhyInfo->rx_mimo_signal_strength[ODM_RF_PATH_A];
				}

				if ((RSSI_max-RSSI_min) < 3)
					RSSI_Ave = RSSI_max;
				else if ((RSSI_max-RSSI_min) < 6)
					RSSI_Ave = RSSI_max - 1;
				else if ((RSSI_max-RSSI_min) < 10)
					RSSI_Ave = RSSI_max - 2;
				else
					RSSI_Ave = RSSI_max - 3;
			}

			/* 1 Process OFDM RSSI */
			if (UndecoratedSmoothedOFDM <= 0)	/*  initialize */
				UndecoratedSmoothedOFDM = pPhyInfo->rx_pwd_ba11;
			else {
				if (pPhyInfo->rx_pwd_ba11 > (u32)UndecoratedSmoothedOFDM) {
					UndecoratedSmoothedOFDM =
							((UndecoratedSmoothedOFDM*(Rx_Smooth_Factor-1)) +
							RSSI_Ave)/Rx_Smooth_Factor;
					UndecoratedSmoothedOFDM = UndecoratedSmoothedOFDM + 1;
				} else {
					UndecoratedSmoothedOFDM =
							((UndecoratedSmoothedOFDM*(Rx_Smooth_Factor-1)) +
							RSSI_Ave)/Rx_Smooth_Factor;
				}
			}

			pEntry->rssi_stat.PacketMap = (pEntry->rssi_stat.PacketMap<<1) | BIT0;

		} else {
			RSSI_Ave = pPhyInfo->rx_pwd_ba11;
			pDM_Odm->RSSI_A = (u8) pPhyInfo->rx_pwd_ba11;
			pDM_Odm->RSSI_B = 0;

			/* 1 Process CCK RSSI */
			if (UndecoratedSmoothedCCK <= 0)	/*  initialize */
				UndecoratedSmoothedCCK = pPhyInfo->rx_pwd_ba11;
			else {
				if (pPhyInfo->rx_pwd_ba11 > (u32)UndecoratedSmoothedCCK) {
					UndecoratedSmoothedCCK =
							((UndecoratedSmoothedCCK*(Rx_Smooth_Factor-1)) +
							pPhyInfo->rx_pwd_ba11)/Rx_Smooth_Factor;
					UndecoratedSmoothedCCK = UndecoratedSmoothedCCK + 1;
				} else {
					UndecoratedSmoothedCCK =
							((UndecoratedSmoothedCCK*(Rx_Smooth_Factor-1)) +
							pPhyInfo->rx_pwd_ba11)/Rx_Smooth_Factor;
				}
			}
			pEntry->rssi_stat.PacketMap = pEntry->rssi_stat.PacketMap<<1;
		}

		/* if (pEntry) */
		{
			/* 2011.07.28 LukeLee: modified to prevent unstable CCK RSSI */
			if (pEntry->rssi_stat.ValidBit >= 64)
				pEntry->rssi_stat.ValidBit = 64;
			else
				pEntry->rssi_stat.ValidBit++;

			for (i = 0; i < pEntry->rssi_stat.ValidBit; i++)
				OFDM_pkt += (u8)(pEntry->rssi_stat.PacketMap>>i)&BIT0;

			if (pEntry->rssi_stat.ValidBit == 64) {
				Weighting = ((OFDM_pkt<<4) > 64)?64:(OFDM_pkt<<4);
				UndecoratedSmoothedPWDB = (Weighting*UndecoratedSmoothedOFDM+(64-Weighting)*UndecoratedSmoothedCCK)>>6;
			} else {
				if (pEntry->rssi_stat.ValidBit != 0)
					UndecoratedSmoothedPWDB = (OFDM_pkt*UndecoratedSmoothedOFDM+(pEntry->rssi_stat.ValidBit-OFDM_pkt)*UndecoratedSmoothedCCK)/pEntry->rssi_stat.ValidBit;
				else
					UndecoratedSmoothedPWDB = 0;
			}

			pEntry->rssi_stat.UndecoratedSmoothedCCK = UndecoratedSmoothedCCK;
			pEntry->rssi_stat.UndecoratedSmoothedOFDM = UndecoratedSmoothedOFDM;
			pEntry->rssi_stat.UndecoratedSmoothedPWDB = UndecoratedSmoothedPWDB;

			/* DbgPrint("OFDM_pkt =%d, Weighting =%d\n", OFDM_pkt, Weighting); */
			/* DbgPrint("UndecoratedSmoothedOFDM =%d, UndecoratedSmoothedPWDB =%d, UndecoratedSmoothedCCK =%d\n", */
			/* UndecoratedSmoothedOFDM, UndecoratedSmoothedPWDB, UndecoratedSmoothedCCK); */

		}

	}
}


/*  */
/*  Endianness before calling this API */
/*  */
static void ODM_PhyStatusQuery_92CSeries(
	struct dm_odm_t *pDM_Odm,
	struct odm_phy_info *pPhyInfo,
	u8 *pPhyStatus,
	struct odm_packet_info *pPktinfo
)
{

	odm_RxPhyStatus92CSeries_Parsing(pDM_Odm, pPhyInfo, pPhyStatus, pPktinfo);

	if (!pDM_Odm->RSSI_test)
		odm_Process_RSSIForDM(pDM_Odm, pPhyInfo, pPktinfo);
}

void ODM_PhyStatusQuery(
	struct dm_odm_t *pDM_Odm,
	struct odm_phy_info *pPhyInfo,
	u8 *pPhyStatus,
	struct odm_packet_info *pPktinfo
)
{

	ODM_PhyStatusQuery_92CSeries(pDM_Odm, pPhyInfo, pPhyStatus, pPktinfo);
}

/*  */
/*  If you want to add a new IC, Please follow below template and generate a new one. */
/*  */
/*  */

enum hal_status ODM_ConfigRFWithHeaderFile(
	struct dm_odm_t *pDM_Odm,
	enum ODM_RF_Config_Type ConfigType,
	enum odm_rf_radio_path_e eRFPath
)
{
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD,
				("===>ODM_ConfigRFWithHeaderFile (%s)\n", (pDM_Odm->bIsMPChip) ? "MPChip" : "TestChip"));
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD,
				("pDM_Odm->SupportPlatform: 0x%X, pDM_Odm->SupportInterface: 0x%X, pDM_Odm->BoardType: 0x%X\n",
				pDM_Odm->SupportPlatform, pDM_Odm->SupportInterface, pDM_Odm->BoardType));

	if (ConfigType == CONFIG_RF_RADIO)
		READ_AND_CONFIG(8723B, _RadioA);
	else if (ConfigType == CONFIG_RF_TXPWR_LMT)
		READ_AND_CONFIG(8723B, _TXPWR_LMT);

	return HAL_STATUS_SUCCESS;
}

enum hal_status ODM_ConfigRFWithTxPwrTrackHeaderFile(struct dm_odm_t *pDM_Odm)
{
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD,
				 ("===>ODM_ConfigRFWithTxPwrTrackHeaderFile (%s)\n", (pDM_Odm->bIsMPChip) ? "MPChip" : "TestChip"));
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD,
				 ("pDM_Odm->SupportPlatform: 0x%X, pDM_Odm->SupportInterface: 0x%X, pDM_Odm->BoardType: 0x%X\n",
				 pDM_Odm->SupportPlatform, pDM_Odm->SupportInterface, pDM_Odm->BoardType));

	if (pDM_Odm->SupportInterface == ODM_ITRF_SDIO)
		READ_AND_CONFIG(8723B, _TxPowerTrack_SDIO);

	return HAL_STATUS_SUCCESS;
}

enum hal_status ODM_ConfigBBWithHeaderFile(
	struct dm_odm_t *pDM_Odm, enum ODM_BB_Config_Type ConfigType
)
{
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD,
				("===>ODM_ConfigBBWithHeaderFile (%s)\n", (pDM_Odm->bIsMPChip) ? "MPChip" : "TestChip"));
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD,
				("pDM_Odm->SupportPlatform: 0x%X, pDM_Odm->SupportInterface: 0x%X, pDM_Odm->BoardType: 0x%X\n",
				pDM_Odm->SupportPlatform, pDM_Odm->SupportInterface, pDM_Odm->BoardType));

	if (ConfigType == CONFIG_BB_PHY_REG)
		READ_AND_CONFIG(8723B, _PHY_REG);
	else if (ConfigType == CONFIG_BB_AGC_TAB)
		READ_AND_CONFIG(8723B, _AGC_TAB);
	else if (ConfigType == CONFIG_BB_PHY_REG_PG)
		READ_AND_CONFIG(8723B, _PHY_REG_PG);

	return HAL_STATUS_SUCCESS;
}

