/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/

/*  include files */

#include "odm_precomp.h"

#define READ_AND_CONFIG     READ_AND_CONFIG_MP

#define READ_AND_CONFIG_MP(ic, txt) (ODM_ReadAndConfig##txt##ic(dm_odm))
#define READ_AND_CONFIG_TC(ic, txt) (ODM_ReadAndConfig_TC##txt##ic(dm_odm))

static u8 odm_QueryRxPwrPercentage(s8 AntPower)
{
	if ((AntPower <= -100) || (AntPower >= 20))
		return	0;
	else if (AntPower >= 0)
		return	100;
	else
		return 100+AntPower;
}

/*  2012/01/12 MH MOve some signal strength smooth method to MP HAL layer. */
/*  IF other SW team do not support the feature, remove this section.?? */
static s32 odm_SignalScaleMapping_92CSeries(struct odm_dm_struct *dm_odm, s32 CurrSig)
{
	s32 RetSig = 0;

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
	return RetSig;
}

static s32 odm_SignalScaleMapping(struct odm_dm_struct *dm_odm, s32 CurrSig)
{
	return odm_SignalScaleMapping_92CSeries(dm_odm, CurrSig);
}

static u8 odm_EVMdbToPercentage(s8 Value)
{
	/*  -33dB~0dB to 0%~99% */
	s8 ret_val;

	ret_val = Value;

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

static void odm_RxPhyStatus92CSeries_Parsing(struct odm_dm_struct *dm_odm,
			struct odm_phy_status_info *pPhyInfo,
			u8 *pPhyStatus,
			struct odm_per_pkt_info *pPktinfo)
{
	struct sw_ant_switch *pDM_SWAT_Table = &dm_odm->DM_SWAT_Table;
	u8 i, Max_spatial_stream;
	s8 rx_pwr[4], rx_pwr_all = 0;
	u8 EVM, PWDB_ALL = 0, PWDB_ALL_BT;
	u8 RSSI, total_rssi = 0;
	u8 isCCKrate = 0;
	u8 rf_rx_num = 0;
	u8 cck_highpwr = 0;
	u8 LNA_idx, VGA_idx;

	struct phy_status_rpt *pPhyStaRpt = (struct phy_status_rpt *)pPhyStatus;

	isCCKrate = ((pPktinfo->Rate >= DESC92C_RATE1M) && (pPktinfo->Rate <= DESC92C_RATE11M)) ? true : false;

	pPhyInfo->RxMIMOSignalQuality[RF_PATH_A] = -1;
	pPhyInfo->RxMIMOSignalQuality[RF_PATH_B] = -1;

	if (isCCKrate) {
		u8 cck_agc_rpt;

		dm_odm->PhyDbgInfo.NumQryPhyStatusCCK++;
		/*  (1)Hardware does not provide RSSI for CCK */
		/*  (2)PWDB, Average PWDB cacluated by hardware (for rate adaptive) */

		cck_highpwr = dm_odm->bCckHighPower;

		cck_agc_rpt =  pPhyStaRpt->cck_agc_rpt_ofdm_cfosho_a ;

		/* 2011.11.28 LukeLee: 88E use different LNA & VGA gain table */
		/* The RSSI formula should be modified according to the gain table */
		/* In 88E, cck_highpwr is always set to 1 */
		LNA_idx = ((cck_agc_rpt & 0xE0) >> 5);
		VGA_idx = (cck_agc_rpt & 0x1F);
		switch (LNA_idx) {
		case 7:
			if (VGA_idx <= 27)
				rx_pwr_all = -100 + 2*(27-VGA_idx); /* VGA_idx = 27~2 */
			else
				rx_pwr_all = -100;
			break;
		case 6:
			rx_pwr_all = -48 + 2*(2-VGA_idx); /* VGA_idx = 2~0 */
			break;
		case 5:
			rx_pwr_all = -42 + 2*(7-VGA_idx); /* VGA_idx = 7~5 */
			break;
		case 4:
			rx_pwr_all = -36 + 2*(7-VGA_idx); /* VGA_idx = 7~4 */
			break;
		case 3:
			rx_pwr_all = -24 + 2*(7-VGA_idx); /* VGA_idx = 7~0 */
			break;
		case 2:
			if (cck_highpwr)
				rx_pwr_all = -12 + 2*(5-VGA_idx); /* VGA_idx = 5~0 */
			else
				rx_pwr_all = -6 + 2*(5-VGA_idx);
			break;
		case 1:
			rx_pwr_all = 8-2*VGA_idx;
			break;
		case 0:
			rx_pwr_all = 14-2*VGA_idx;
			break;
		default:
			break;
		}
		rx_pwr_all += 6;
		PWDB_ALL = odm_QueryRxPwrPercentage(rx_pwr_all);
		if (!cck_highpwr) {
			if (PWDB_ALL >= 80)
				PWDB_ALL = ((PWDB_ALL-80)<<1)+((PWDB_ALL-80)>>1)+80;
			else if ((PWDB_ALL <= 78) && (PWDB_ALL >= 20))
				PWDB_ALL += 3;
			if (PWDB_ALL > 100)
				PWDB_ALL = 100;
		}

		pPhyInfo->RxPWDBAll = PWDB_ALL;
		pPhyInfo->BTRxRSSIPercentage = PWDB_ALL;
		pPhyInfo->RecvSignalPower = rx_pwr_all;
		/*  (3) Get Signal Quality (EVM) */
		if (pPktinfo->bPacketMatchBSSID) {
			u8 SQ, SQ_rpt;

			if (pPhyInfo->RxPWDBAll > 40 && !dm_odm->bInHctTest) {
				SQ = 100;
			} else {
				SQ_rpt = pPhyStaRpt->cck_sig_qual_ofdm_pwdb_all;

				if (SQ_rpt > 64)
					SQ = 0;
				else if (SQ_rpt < 20)
					SQ = 100;
				else
					SQ = ((64-SQ_rpt) * 100) / 44;
			}
			pPhyInfo->SignalQuality = SQ;
			pPhyInfo->RxMIMOSignalQuality[RF_PATH_A] = SQ;
			pPhyInfo->RxMIMOSignalQuality[RF_PATH_B] = -1;
		}
	} else { /* is OFDM rate */
		dm_odm->PhyDbgInfo.NumQryPhyStatusOFDM++;

		/*  (1)Get RSSI for HT rate */

		 for (i = RF_PATH_A; i < RF_PATH_MAX; i++) {
			/*  2008/01/30 MH we will judge RF RX path now. */
			if (dm_odm->RFPathRxEnable & BIT(i))
				rf_rx_num++;

			rx_pwr[i] = ((pPhyStaRpt->path_agc[i].gain & 0x3F)*2) - 110;

			pPhyInfo->RxPwr[i] = rx_pwr[i];

			/* Translate DBM to percentage. */
			RSSI = odm_QueryRxPwrPercentage(rx_pwr[i]);
			total_rssi += RSSI;

			/* Modification for ext-LNA board */
			if (dm_odm->BoardType == ODM_BOARD_HIGHPWR) {
				if ((pPhyStaRpt->path_agc[i].trsw) == 1)
					RSSI = (RSSI > 94) ? 100 : (RSSI + 6);
				else
					RSSI = (RSSI <= 16) ? (RSSI >> 3) : (RSSI - 16);

				if ((RSSI <= 34) && (RSSI >= 4))
					RSSI -= 4;
			}

			pPhyInfo->RxMIMOSignalStrength[i] = (u8)RSSI;

			/* Get Rx snr value in DB */
			pPhyInfo->RxSNR[i] = (s32)(pPhyStaRpt->path_rxsnr[i]/2);
			dm_odm->PhyDbgInfo.RxSNRdB[i] = (s32)(pPhyStaRpt->path_rxsnr[i]/2);
		}
		/*  (2)PWDB, Average PWDB cacluated by hardware (for rate adaptive) */
		rx_pwr_all = (((pPhyStaRpt->cck_sig_qual_ofdm_pwdb_all) >> 1) & 0x7f) - 110;

		PWDB_ALL = odm_QueryRxPwrPercentage(rx_pwr_all);
		PWDB_ALL_BT = PWDB_ALL;

		pPhyInfo->RxPWDBAll = PWDB_ALL;
		pPhyInfo->BTRxRSSIPercentage = PWDB_ALL_BT;
		pPhyInfo->RxPower = rx_pwr_all;
		pPhyInfo->RecvSignalPower = rx_pwr_all;

		/*  (3)EVM of HT rate */
		if (pPktinfo->Rate >= DESC92C_RATEMCS8 && pPktinfo->Rate <= DESC92C_RATEMCS15)
			Max_spatial_stream = 2; /* both spatial stream make sense */
		else
			Max_spatial_stream = 1; /* only spatial stream 1 makes sense */

		for (i = 0; i < Max_spatial_stream; i++) {
			/*  Do not use shift operation like "rx_evmX >>= 1" because the compilor of free build environment */
			/*  fill most significant bit to "zero" when doing shifting operation which may change a negative */
			/*  value to positive one, then the dbm value (which is supposed to be negative)  is not correct anymore. */
			EVM = odm_EVMdbToPercentage((pPhyStaRpt->stream_rxevm[i]));	/* dbm */

			if (pPktinfo->bPacketMatchBSSID) {
				if (i == RF_PATH_A) /*  Fill value in RFD, Get the first spatial stream only */
					pPhyInfo->SignalQuality = (u8)(EVM & 0xff);
				pPhyInfo->RxMIMOSignalQuality[i] = (u8)(EVM & 0xff);
			}
		}
	}
	/* UI BSS List signal strength(in percentage), make it good looking, from 0~100. */
	/* It is assigned to the BSS List in GetValueFromBeaconOrProbeRsp(). */
	if (isCCKrate) {
		pPhyInfo->SignalStrength = (u8)(odm_SignalScaleMapping(dm_odm, PWDB_ALL));/* PWDB_ALL; */
	} else {
		if (rf_rx_num != 0)
			pPhyInfo->SignalStrength = (u8)(odm_SignalScaleMapping(dm_odm, total_rssi /= rf_rx_num));
	}

	/* For 92C/92D HW (Hybrid) Antenna Diversity */
	pDM_SWAT_Table->antsel = pPhyStaRpt->ant_sel;
	/* For 88E HW Antenna Diversity */
	dm_odm->DM_FatTable.antsel_rx_keep_0 = pPhyStaRpt->ant_sel;
	dm_odm->DM_FatTable.antsel_rx_keep_1 = pPhyStaRpt->ant_sel_b;
	dm_odm->DM_FatTable.antsel_rx_keep_2 = pPhyStaRpt->antsel_rx_keep_2;
}

void odm_Init_RSSIForDM(struct odm_dm_struct *dm_odm)
{
}

static void odm_Process_RSSIForDM(struct odm_dm_struct *dm_odm,
				  struct odm_phy_status_info *pPhyInfo,
				  struct odm_per_pkt_info *pPktinfo)
{
	s32 UndecoratedSmoothedPWDB, UndecoratedSmoothedCCK;
	s32 UndecoratedSmoothedOFDM, RSSI_Ave;
	u8 isCCKrate = 0;
	u8 RSSI_max, RSSI_min, i;
	u32 OFDM_pkt = 0;
	u32 Weighting = 0;
	struct sta_info *pEntry;
	u8 antsel_tr_mux;
	struct fast_ant_train *pDM_FatTable = &dm_odm->DM_FatTable;

	if (pPktinfo->StationID == 0xFF)
		return;
	pEntry = dm_odm->pODM_StaInfo[pPktinfo->StationID];
	if (!IS_STA_VALID(pEntry))
		return;
	if ((!pPktinfo->bPacketMatchBSSID))
		return;

	isCCKrate = ((pPktinfo->Rate >= DESC92C_RATE1M) && (pPktinfo->Rate <= DESC92C_RATE11M)) ? true : false;

	/* Smart Antenna Debug Message------------------  */

	if (dm_odm->AntDivType == CG_TRX_SMART_ANTDIV) {
		if (pDM_FatTable->FAT_State == FAT_TRAINING_STATE) {
			if (pPktinfo->bPacketToSelf) {
				antsel_tr_mux = (pDM_FatTable->antsel_rx_keep_2<<2) |
						(pDM_FatTable->antsel_rx_keep_1<<1) |
						pDM_FatTable->antsel_rx_keep_0;
				pDM_FatTable->antSumRSSI[antsel_tr_mux] += pPhyInfo->RxPWDBAll;
				pDM_FatTable->antRSSIcnt[antsel_tr_mux]++;
			}
		}
	} else if ((dm_odm->AntDivType == CG_TRX_HW_ANTDIV) || (dm_odm->AntDivType == CGCS_RX_HW_ANTDIV)) {
		if (pPktinfo->bPacketToSelf || pPktinfo->bPacketBeacon) {
			antsel_tr_mux = (pDM_FatTable->antsel_rx_keep_2<<2) |
					(pDM_FatTable->antsel_rx_keep_1<<1) | pDM_FatTable->antsel_rx_keep_0;
			ODM_AntselStatistics_88E(dm_odm, antsel_tr_mux, pPktinfo->StationID, pPhyInfo->RxPWDBAll);
		}
	}
	/* Smart Antenna Debug Message------------------ */

	UndecoratedSmoothedCCK =  pEntry->rssi_stat.UndecoratedSmoothedCCK;
	UndecoratedSmoothedOFDM = pEntry->rssi_stat.UndecoratedSmoothedOFDM;
	UndecoratedSmoothedPWDB = pEntry->rssi_stat.UndecoratedSmoothedPWDB;

	if (pPktinfo->bPacketToSelf || pPktinfo->bPacketBeacon) {
		if (!isCCKrate) { /* ofdm rate */
			if (pPhyInfo->RxMIMOSignalStrength[RF_PATH_B] == 0) {
				RSSI_Ave = pPhyInfo->RxMIMOSignalStrength[RF_PATH_A];
			} else {
				if (pPhyInfo->RxMIMOSignalStrength[RF_PATH_A] > pPhyInfo->RxMIMOSignalStrength[RF_PATH_B]) {
					RSSI_max = pPhyInfo->RxMIMOSignalStrength[RF_PATH_A];
					RSSI_min = pPhyInfo->RxMIMOSignalStrength[RF_PATH_B];
				} else {
					RSSI_max = pPhyInfo->RxMIMOSignalStrength[RF_PATH_B];
					RSSI_min = pPhyInfo->RxMIMOSignalStrength[RF_PATH_A];
				}
				if ((RSSI_max - RSSI_min) < 3)
					RSSI_Ave = RSSI_max;
				else if ((RSSI_max - RSSI_min) < 6)
					RSSI_Ave = RSSI_max - 1;
				else if ((RSSI_max - RSSI_min) < 10)
					RSSI_Ave = RSSI_max - 2;
				else
					RSSI_Ave = RSSI_max - 3;
			}

			/* 1 Process OFDM RSSI */
			if (UndecoratedSmoothedOFDM <= 0) {	/*  initialize */
				UndecoratedSmoothedOFDM = pPhyInfo->RxPWDBAll;
			} else {
				if (pPhyInfo->RxPWDBAll > (u32)UndecoratedSmoothedOFDM) {
					UndecoratedSmoothedOFDM =
							(((UndecoratedSmoothedOFDM)*(Rx_Smooth_Factor-1)) +
							(RSSI_Ave)) / (Rx_Smooth_Factor);
					UndecoratedSmoothedOFDM = UndecoratedSmoothedOFDM + 1;
				} else {
					UndecoratedSmoothedOFDM =
							(((UndecoratedSmoothedOFDM)*(Rx_Smooth_Factor-1)) +
							(RSSI_Ave)) / (Rx_Smooth_Factor);
				}
			}

			pEntry->rssi_stat.PacketMap = (pEntry->rssi_stat.PacketMap<<1) | BIT0;

		} else {
			RSSI_Ave = pPhyInfo->RxPWDBAll;

			/* 1 Process CCK RSSI */
			if (UndecoratedSmoothedCCK <= 0) {	/*  initialize */
				UndecoratedSmoothedCCK = pPhyInfo->RxPWDBAll;
			} else {
				if (pPhyInfo->RxPWDBAll > (u32)UndecoratedSmoothedCCK) {
					UndecoratedSmoothedCCK =
							((UndecoratedSmoothedCCK * (Rx_Smooth_Factor-1)) +
							pPhyInfo->RxPWDBAll) / Rx_Smooth_Factor;
					UndecoratedSmoothedCCK = UndecoratedSmoothedCCK + 1;
				} else {
					UndecoratedSmoothedCCK =
							((UndecoratedSmoothedCCK * (Rx_Smooth_Factor-1)) +
							pPhyInfo->RxPWDBAll) / Rx_Smooth_Factor;
				}
			}
			pEntry->rssi_stat.PacketMap = pEntry->rssi_stat.PacketMap<<1;
		}
		/* 2011.07.28 LukeLee: modified to prevent unstable CCK RSSI */
		if (pEntry->rssi_stat.ValidBit >= 64)
			pEntry->rssi_stat.ValidBit = 64;
		else
			pEntry->rssi_stat.ValidBit++;

		for (i = 0; i < pEntry->rssi_stat.ValidBit; i++)
			OFDM_pkt += (u8)(pEntry->rssi_stat.PacketMap>>i)&BIT0;

		if (pEntry->rssi_stat.ValidBit == 64) {
			Weighting = ((OFDM_pkt<<4) > 64) ? 64 : (OFDM_pkt<<4);
			UndecoratedSmoothedPWDB = (Weighting*UndecoratedSmoothedOFDM+(64-Weighting)*UndecoratedSmoothedCCK)>>6;
		} else {
			if (pEntry->rssi_stat.ValidBit != 0)
				UndecoratedSmoothedPWDB = (OFDM_pkt * UndecoratedSmoothedOFDM +
							  (pEntry->rssi_stat.ValidBit-OFDM_pkt) *
							  UndecoratedSmoothedCCK)/pEntry->rssi_stat.ValidBit;
			else
				UndecoratedSmoothedPWDB = 0;
		}
		pEntry->rssi_stat.UndecoratedSmoothedCCK = UndecoratedSmoothedCCK;
		pEntry->rssi_stat.UndecoratedSmoothedOFDM = UndecoratedSmoothedOFDM;
		pEntry->rssi_stat.UndecoratedSmoothedPWDB = UndecoratedSmoothedPWDB;
	}
}

/*  Endianness before calling this API */
static void ODM_PhyStatusQuery_92CSeries(struct odm_dm_struct *dm_odm,
					 struct odm_phy_status_info *pPhyInfo,
					 u8 *pPhyStatus,
					 struct odm_per_pkt_info *pPktinfo)
{
	odm_RxPhyStatus92CSeries_Parsing(dm_odm, pPhyInfo, pPhyStatus,
					 pPktinfo);
	if (dm_odm->RSSI_test) {
		;/*  Select the packets to do RSSI checking for antenna switching. */
	} else {
		odm_Process_RSSIForDM(dm_odm, pPhyInfo, pPktinfo);
	}
}

void ODM_PhyStatusQuery(struct odm_dm_struct *dm_odm,
			struct odm_phy_status_info *pPhyInfo,
			u8 *pPhyStatus, struct odm_per_pkt_info *pPktinfo)
{
	ODM_PhyStatusQuery_92CSeries(dm_odm, pPhyInfo, pPhyStatus, pPktinfo);
}

/*  For future use. */
void ODM_MacStatusQuery(struct odm_dm_struct *dm_odm, u8 *mac_stat,
			u8 macid, bool pkt_match_bssid,
			bool pkttoself, bool pkt_beacon)
{
	/*  2011/10/19 Driver team will handle in the future. */
}

enum HAL_STATUS ODM_ConfigRFWithHeaderFile(struct odm_dm_struct *dm_odm,
					   enum rf_radio_path content,
					   enum rf_radio_path rfpath)
{
	ODM_RT_TRACE(dm_odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("===>ODM_ConfigRFWithHeaderFile\n"));
	if (rfpath == RF_PATH_A)
		READ_AND_CONFIG(8188E, _RadioA_1T_);
	ODM_RT_TRACE(dm_odm, ODM_COMP_INIT, ODM_DBG_LOUD, (" ===> ODM_ConfigRFWithHeaderFile() Radio_A:Rtl8188ERadioA_1TArray\n"));
	ODM_RT_TRACE(dm_odm, ODM_COMP_INIT, ODM_DBG_LOUD, (" ===> ODM_ConfigRFWithHeaderFile() Radio_B:Rtl8188ERadioB_1TArray\n"));

	ODM_RT_TRACE(dm_odm, ODM_COMP_INIT, ODM_DBG_TRACE, ("ODM_ConfigRFWithHeaderFile: Radio No %x\n", rfpath));
	return HAL_STATUS_SUCCESS;
}

enum HAL_STATUS ODM_ConfigBBWithHeaderFile(struct odm_dm_struct *dm_odm,
					   enum odm_bb_config_type config_tp)
{
	if (config_tp == CONFIG_BB_PHY_REG) {
		READ_AND_CONFIG(8188E, _PHY_REG_1T_);
	} else if (config_tp == CONFIG_BB_AGC_TAB) {
		READ_AND_CONFIG(8188E, _AGC_TAB_1T_);
	} else if (config_tp == CONFIG_BB_PHY_REG_PG) {
		READ_AND_CONFIG(8188E, _PHY_REG_PG_);
		ODM_RT_TRACE(dm_odm, ODM_COMP_INIT, ODM_DBG_LOUD,
			     (" ===> phy_ConfigBBWithHeaderFile() agc:Rtl8188EPHY_REG_PGArray\n"));
	}
	return HAL_STATUS_SUCCESS;
}

enum HAL_STATUS ODM_ConfigMACWithHeaderFile(struct odm_dm_struct *dm_odm)
{
	u8 result = HAL_STATUS_SUCCESS;
	result = READ_AND_CONFIG(8188E, _MAC_REG_);
	return result;
}
